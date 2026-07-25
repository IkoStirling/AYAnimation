# AYAnimation Design

> **状态（2026-07-26 更新）**：AN-01 已 ship（最小单 clip 播放器，约 200 行真实代码 + 11 个 test case）。本设计文档自该版起 **重构目标** 明确为"AYAnimation 是 AYResource 的薄消费层"，不再独立维护 skeleton/animation 数据类型。
> 工业级对标：Unreal Engine Animation System / Unity Animator / Godot 4 AnimationTree / O3DE Animation Graph。

---

## 1. 概述

AYAnimation 是 AY Engine 的**动画子系统**，负责：

- 消费 `ayt::resource::ISkeleton` / `ayt::resource::IAnimation`（权威数据源）
- 关键帧采样（lerp + slerp + dot<0 选优）
- 时间管理 + 循环 wrap
- 局部 TRS → 世界矩阵 → 蒙皮矩阵的三阶段 evaluate
- Anim Notify 事件分发（Phase 1.5+）
- Float 参数曲线出口（Phase 1.5+）

**不负责**（由其它模块负责）：
- 骨骼 / 动画数据的加载 / 解析 / 二进制序列化 ── **AYResource**
- 顶点变形计算 ── **CPUSkinning Pass**（Phase 2+）
- 蒙皮矩阵上传 GPU ── **AYRenderer** 的 SkinnedLit Pass
- 物理驱动骨骼 ── **AYPhysics** 的 Jolt integration（Phase 4+）

### 1.1 设计目标

- **零数据平行**：所有 skeleton / animation 数据 **必须** 来自 `ayt::resource::*`，AYAnimation 不维护第二份。
- **薄包装**：`AnimationPlayer` 是 `ISkeleton + IAnimation` 之上的时间管理 + 采样 + 矩阵计算引擎，不存储动画数据本身。
- **三层矩阵**：`local TRS → world matrix → skin matrix = world * inverseBind`，与 Unreal `FCompactPose` / Unity `Animator.bodyPosition` 同构。
- **事件驱动**：Phase 1.5+ 起，宿主通过 sink 订阅 Anim Notify + Float parameter 曲线。

### 1.2 在引擎中的位置（修订版）

```
┌─────────────────────────────────────────────────────────────────┐
│                        Game Engine                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐                                                 │
│  │  AYResource │─── ISkeleton / IAnimation (权威数据)             │
│  │  (资源管理)  │                                                 │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                 AYAnimation (薄消费层)                    │    │
│  │                                                          │    │
│  │  ┌────────────────────────────────────────────────┐     │    │
│  │  │  KeySampler.h                                  │     │    │
│  │  │  sampleTrackVector3 / Quaternion / Float        │     │    │
│  │  │  + binary-search + dot<0 slerp 选优 + monotonic │     │    │
│  │  └────────────────────────────────────────────────┘     │    │
│  │                                                          │    │
│  │  ┌────────────────────────────────────────────────┐     │    │
│  │  │  AnimationPlayer.h                             │     │    │
│  │  │  - 持 const ISkeleton* / const IAnimation*     │     │    │
│  │  │  - tick(dt) / evaluate()                       │     │    │
│  │  │  - 3 阶段: sample → world → skin                │     │    │
│  │  │  - 出口: getSkinMatrices / getFloatCurves       │     │    │
│  │  └────────────────────────────────────────────────┘     │    │
│  │                                                          │    │
│  └──────────────────────┬──────────────────────────────────┘    │
│                         │ FloatCurveSink + SkinMatrices           │
│                         ▼                                        │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │   AYEntity::AnimationSystem (priority 450)              │    │
│  │   - per-frame tick + evaluate                           │    │
│  │   - 转发 skin matrices 到 SkeletonComponent             │    │
│  └──────────────────────┬──────────────────────────────────┘    │
│                         │                                        │
│                         ▼                                        │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │   AYRenderer (SkinnedLit Pass, priority 500)            │    │
│  │   - 读 SkeletonComponent.skinMatrices                   │    │
│  │   - 上传 Skeleton UBO                                   │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心概念（修订版）

### 2.1 蒙皮（Skinning）

蒙皮公式与 AN-01 一致：

```
finalVertex = Σ (boneWeight[i] × boneMatrix[i]) × originalVertex

boneMatrix[i] = boneWorldMatrix[i] × inverseBindMatrix[i]
```

**注意**：本设计严格遵守 AYResource `Bone.inverseBindMatrix = bindWorld.inverse()` 的约定（见 [SuzanneSkinnedDemo.cpp:158](../AYRenderer/demo/SuzanneSkinnedDemo.cpp#L158)），即 IBM 是"绑定姿势世界矩阵的逆"。这意味着：

- 绑定姿势时 skin matrix = identity（`bindWorld * bindWorld.inverse() = I`）── 这是验证矩阵约定的核心 invariant。
- 任何对 IBM 字段语义理解错误（误读为 `localTRS.inverse()`）都会让整个蒙皮结果错位。

### 2.2 两种蒙皮模式（Phase 2+）

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **CPU 蒙皮** | CPU 计算顶点变形，上传 GPU | 程序化动画、调试、需要读取变形数据 |
| **GPU 蒙皮** | 顶点着色器计算 | 大量骨骼、高性能需求 |

**AYAnimation 职责边界**：只输出 `_skin: vector<Float4x4>`（每骨一个 skin matrix），CPU 顶点变形由后续 CPUSkinning Pass 消费；GPU 蒙皮直接由 SkinnedLit shader 消费 `_skin` 上传。

### 2.3 烘焙（Baking）（Phase 4+）

- **常态**：保留绑定数据，运行时计算（AN-01 当前）。
- **烘焙**：预计算动画帧的顶点位置，运行时直接读取（LOD / mobile 场景）。

---

## 3. 核心数据结构（修订版 ── 极薄）

### 3.1 Skeleton ── **不存，引用**

AN-01 曾定义 `ayt::anim::Skeleton`（含 `Bone` + parallel local TRS arrays）。该类型**已在 P0 修复中删除** ── 现在直接消费 `ayt::resource::ISkeleton`：

```cpp
#include <assetsDefs/IAYSkeleton.h>   // AYResource 接口

namespace ayt::anim
{
    // AnimationPlayer 持有:
    const ayt::resource::ISkeleton* _skeleton = nullptr;

    // evaluate() 内部直接读:
    //   _skeleton->getBoneCount()
    //   _skeleton->getBones()[i].parentIndex
    //   _skeleton->getLocalPositions/Rotations/Scales()      // rest pose
    //   _skeleton->getInverseBindMatrices()                 // bind → mesh space
    //   _skeleton->findBone(name)                           // name lookup
}
```

### 3.2 Animation ── **不存，引用**

AN-01 曾定义 `ayt::anim::Animation`（含 `KeyframeTrack`）。该类型**已在 P0 修复中删除** ── 现在直接消费 `ayt::resource::IAnimation`：

```cpp
#include <assetsDefs/IAYAnimation.h>   // AYResource 接口

namespace ayt::anim
{
    // AnimationPlayer 持有:
    const ayt::resource::IAnimation* _anim = nullptr;

    // evaluate() 内部直接读:
    //   _anim->getDuration()
    //   _anim->getTrackCount()
    //   _anim->getTrackNodeName(i) / getTrackProperty(i) / getTrackType(i)
    //   _anim->getTrackTimes(i)              // ticks（需 ÷ tps 转 seconds）
    //   _anim->getTrackVector3Values(i)      // 3-stride FVector3 数组
    //   _anim->getTrackQuaternionValues(i)   // 4-stride FQuaternion 数组
    //   _anim->getTrackFloatValues(i)        // 1-stride float 数组
}
```

**重要约定**：`getTrackTimes()` 返回的 `times` 是 **raw FBX ticks**，不是秒。`AnimationPlayer::setClip` 时按 `ticksPerSecond` 转换为秒并缓存（pre-normalized times）── 避免 hot path 每帧除法。

### 3.3 BoneTransforms ── evaluate 输出

```cpp
struct BoneTransform {
    FVector3    position;
    FQuaternion rotation;
    FVector3    scale;
};

// AnimationPlayer 持有（每帧重算）:
std::vector<ayt::math::Float4x4> _world;   // world = parent.world * localTRS
std::vector<ayt::math::Float4x4> _skin;    // _skin[i] = _world[i] * bones[i].inverseBindMatrix

// 公开出口:
const ayt::math::Float4x4* getBoneSkinMatrices() const { return _skin.data(); }
```

---

## 4. AnimationPlayer（修订版）

### 4.1 接口（AN-01 实际）

```cpp
class AnimationPlayer {
public:
    AnimationPlayer() = default;

    // === 资源绑定（直接消费 AYResource 接口）===
    void setSkeleton(const ayt::resource::ISkeleton* skel);
    void play(const ayt::resource::IAnimation* anim);

    void stop();
    void pause();
    void resume();

    // === 时间控制 ===
    void  setTime(float t);
    void  setPlayRate(float r)        { _playRate = r; }
    void  setLoop(bool enabled)       { _loop = enabled; }

    float getTime() const             { return _time; }
    float getPlayRate() const         { return _playRate; }
    float getDuration() const         { return _anim ? _anim->getDuration() : 0.0f; }
    bool  isPaused() const            { return _paused; }
    bool  isValid() const             { return _skeleton != nullptr && _anim != nullptr; }

    void tick(float dt);
    void evaluate();

    // === 矩阵结果（renderer 读）===
    const ayt::math::Float4x4* getBoneWorldMatrices() const { return _world.data(); }
    const ayt::math::Float4x4* getBoneSkinMatrices()  const { return _skin.data();  }

    // === Float 参数曲线（Phase 1.5+）===
    // 订阅：宿主注册 sink,evaluate() 每帧把 Float track 采样值推入
    void setFloatCurveSink(std::function<void(const char* nodeName, const char* property, float value)> sink);

private:
    const ayt::resource::ISkeleton* _skeleton = nullptr;
    const ayt::resource::IAnimation* _anim     = nullptr;

    float _time     = 0.0f;
    float _playRate = 1.0f;
    bool  _paused   = false;
    bool  _loop     = true;

    std::vector<ayt::math::Float4x4>    _world;
    std::vector<ayt::math::Float4x4>    _skin;
    std::vector<float>                  _localPos;     // per-bone local TRS 工作缓冲
    std::vector<float>                  _localRot;     // （pack 成 raw float 数组,避免每帧 allocate）
    std::vector<float>                  _localScl;

    std::function<void(const char*, const char*, float)> _floatCurveSink;

    // ticks→seconds 预转换缓存（setClip 时一次性算好）
    struct PreNormalizedTrack {
        const float* timesSeconds = nullptr;   // 指向 _preNormTimes 后段
        uint32_t     keyCount     = 0;
    };
    std::vector<float>           _preNormTimes;     // 所有 track 的 pre-normalized times 拼成一段
    std::vector<PreNormalizedTrack> _preNormTracks;  // 每 track 一段切片
};
```

### 4.2 evaluate() 三阶段

```
Phase 1 — sample every track at _time
    读 _anim->getTrackTimes/Values/...
    对每 track:
        const int boneIdx = _skeleton->findBone(track.nodeName);
        if boneIdx < 0: skip
        switch (track.type) {
            case Vector3:    写 _localPos[boneIdx*3..+3]
            case Quaternion: 写 _localRot[boneIdx*4..+4]
            case Float:      推 sink(nodeName, property, value)
        }

Phase 2 — accumulate world = parent.world * localTRS
    for i in [0, n):
        parent = bones[i].parentIndex
        localTRS = Float4x4::fromTRS(_localPos, _localRot, _localScl)
        _world[i] = (parent < 0) ? localTRS : (_world[parent] * localTRS)

Phase 3 — skin matrix = world * inverseBindMatrix
    for i in [0, n):
        _skin[i] = _world[i] * _skeleton->getInverseBindMatrices()[i]
```

### 4.3 时间管理

```cpp
void tick(float dt) {
    if (_paused || _anim == nullptr) return;
    _time += dt * _playRate;
    const float d = _anim->getDuration();
    if (d > 0.0f) {
        if (_loop) {
            _time = _time - std::floor(_time / d) * d;   // wrap to [0, d)
        } else if (_time > d) {
            _time = d;
            _paused = true;                              // clamp to end
        }
    }
}
```

**注意**：`tick` 不防 `dt < 0`。若宿主用 catch-up 时钟倒带，需在更上层 clamp。

### 4.4 ❌ 不在本期（移至 Phase 2 §14）

- crossFade / blend / Additive ── **Phase 2**
- 多 clip 同时播放 / Slot / Layer ── **Phase 2**
- 骨骼遮罩 ── **Phase 2**
- Anim Notify 事件（除 Float track sink 外）── **Phase 1.5**

---

## 5. AnimationStateMachine（Phase 3 ── 未启动）

> **本期状态**：占位,实际不 ship。Phase 3 启动后填实。

L4 状态机架构（Unreal `UAnimInstance` / Unity `Animator`）：

```
AnimationStateMachine
    ├── RootStateMachine
    │   ├── States[]
    │   │   ├── Locomotion (子状态机)
    │   │   │   ├── Idle / Walk / Run / BlendTree
    │   │   ├── Combat
    │   │   └── Jump
    │   ├── Transitions[]
    │   └── BlendRules
    └── BlendTrees
        ├── FullBodyBlend
        └── UpperBodyBlend
```

**Phase 3 启动条件**：P1 全 ship（cross-fade + blend tree 1D/2D）。

---

## 6. IKSolver（Phase 4 ── 未启动）

| 类型 | 适用 |
|------|------|
| TwoBoneSolver | 膝、肘 |
| FABRIKSolver | 手指、触须、多关节 |
| CCDSolver | 通用链式 |

`IKSolver` 接口与 IK 约束设计保留（见 [旧版 §6](../AYAnimation/history/AN-01-design.md)），Phase 4 启动后实装。

---

## 7. 骨骼重定向（Phase 4 ── 未启动）

离线重定向流程 + `BoneMappingTable` + `AnimationRetargeter` 设计保留，Phase 4 启动后实装。

**注**：当前 AYResource 的 `ISkeleton::getRootBoneIndices()` 已为重定向做预留。

---

## 8. 自适应压缩（Phase 5 ── 未启动）

Douglas-Peucker + adaptive keyframe reduction + dual-quat encoding ── 全部位于 **离线 Converter 阶段**（不属于 runtime AYAnimation）。

---

## 9. CPU / GPU 蒙皮（Phase 2+ ── 未启动）

AYAnimation 的 `_skin` 输出是 GPU 蒙皮的输入（直接读 `getBoneSkinMatrices()`）。
CPU 顶点变形由独立 `CPUSkinningPass` 负责（不在 AYAnimation 模块内）。

---

## 10. 目录结构（修订版 ── 反映 AN-01 ship 真实状态）

```
AYAnimation/
├── design.md                       # 本文档
├── CMakeLists.txt                  # target_link_libraries(... AYResource)
├── include/
│   └── ayanimation/
│       ├── AYAnimation.h           # umbrella header
│       │
│       ├── KeySampler.h            # ✅ AN-01 ship: free functions sampleTrack{Vector3,Quaternion,Float}
│       └── AnimationPlayer.h       # ✅ AN-01 ship: 时间管理 + evaluate 三阶段
│
├── src/
│   ├── KeySampler.cpp              # ✅ AN-01 ship: locateSegment + dot<0 slerp 选优 + 单调性 assert
│   └── AnimationPlayer.cpp        # ✅ AN-01 ship: 消费 ISkeleton/IAnimation + ticks→s 预转换
│
└── unittest/
    ├── main.cpp
    ├── AYTest_KeySampler.cpp       # 5+ case: Vec3 lerp / Quat slerp 短弧 / Quat 单 key / Float lerp / dot<0 选优 / 单调性
    └── AYTest_AnimationPlayer.cpp  # 6+ case: rest pose / position lerp / parent-child 组合 / missing track / loop wrap / skin matrix / Float track sink / topology assert / IBM zero safe
```

**已删除（2026-07-26 P0 修复）**：
- ~~Skeleton.h / Skeleton.cpp~~ ── parallel type 取消,改用 `resource::ISkeleton`
- ~~Animation.h / Animation.cpp~~ ── 同上

**未 ship（按 Phase 顺序）**：
- ~~State.h / Transition.h / BlendTree.h~~ ── Phase 3
- ~~IKSolver / TwoBone / FABRIK / CCD / IKConstraint~~ ── Phase 4
- ~~CPUSkinning / GPUSkinning / SkinningFactory~~ ── Phase 2
- ~~AnimationCompressor / AdaptiveKeyframeReducer / CompressionSettings~~ ── Phase 5
- ~~BoneMappingTable / AnimationRetargeter~~ ── Phase 4
- ~~AnimationDebugDraw / AnimationProfiler~~ ── Phase 5

---

## 11. 实现优先级（修订版）

### Phase 1: AN-01 — 最小单 clip 播放器 ── ✅ SHIP

- [x] 消费 `ISkeleton / IAnimation` 接口（2026-07-26 P0 修复后）
- [x] ticks → seconds 预转换
- [x] 基础插值（Linear, Quaternion Slerp + dot<0 选优）
- [x] 局部 TRS → 世界矩阵 → 蒙皮矩阵三阶段
- [x] 时间管理 + 循环 wrap
- [x] 11 unit tests 3-run stable

### Phase 1.5: P0 修复收口 ── 🚧 进行中（2026-07-26 起）

- [x] 删 `ayt::anim::Skeleton` / `ayt::anim::Animation` parallel type
- [x] 删 `AYEntity::adapter::load{Skeleton,Animation}` 适配器
- [x] `AnimationPlayer::play(const IAnimation*)` 改消费 AYResource
- [x] ticks → seconds 在 `setClip` 一次性预转换
- [x] `KeySampler::sampleTrackQuaternion` 加 `dot < 0` 最短弧选优
- [ ] Float track sink 出口 + host subscribe API
- [ ] Topology 顺序 assert（parent index 必先于 child）
- [ ] 矩阵方向约定 lock-test（IBM = bindWorld.inverse()）
- [ ] IBM = 0 NaN-safe 行为验证

### Phase 2: 混合 + 蒙皮 ── ⏳ 排队

- [ ] CrossFade / Blend 1D / Blend 2D
- [ ] Additive 动画层
- [ ] 骨骼遮罩 (Skeleton Mask)
- [ ] 多 Slot / 多 Layer
- [ ] Dual-Quaternion Skinning
- [ ] CPU 蒙皮真输出（CPUSkinning Pass）
- [ ] GPU 蒙皮 Skeleton UBO 上传（与 AYRenderer 接通）

### Phase 3: 状态机 ── ⏳ 排队

- [ ] L1 简单状态机
- [ ] L2 条件转换
- [ ] L3 子状态机
- [ ] L4 MotionMatching 风格状态机

### Phase 4: IK + 重定向 ── ⏳ 排队

- [ ] TwoBoneSolver
- [ ] FABRIKSolver + CCDSolver
- [ ] IK 约束 (angle / distance / rotation)
- [ ] 骨骼重定向（BoneMappingTable + AnimationRetargeter）

### Phase 5: 优化 + 压缩 ── ⏳ 排队

- [ ] 自适应压缩（离线 Converter,非 runtime）
- [ ] AnimationProfiler
- [ ] DebugVisualization
- [ ] LOD 动画系统

---

## 12. 参考

- Unreal Engine Animation System (`UAnimInstance`, `FAnimNode_*`, `UAnimMontage`)
- Unity `Animator` + `AnimatorController` + `AnimationPlayable`
- Godot 4 `AnimationTree` + `AnimationPlayer`
- O3DE Animation Graph

---

## 13. AN-01 ship 状态 vs 工业差距对照表

**审查日期**：2026-07-26
**评估对象**：当前 AN-01 已 ship 的 200 行代码 + 11 个 test case
**评分**：✅ = 已 ship / ⚠️ = 部分 / ❌ = 未 ship / 🔧 = 本期 P0/P1 修

| # | 工业能力 | 工业引擎 | AN-01 | Phase |
|---|---|---|---|---|
| 1 | 单 clip 播放 + 时间控制 | UE/Unity/Godot | ✅ | AN-01 |
| 2 | 多 track 采样 (T+R+S) | UE/Unity | ✅（via ISkeleton parallel arrays）| AN-01 |
| 3 | Quaternion slerp | UE/Unity | ✅（含 dot<0 选优,P0 修）| AN-01 |
| 4 | ticks → seconds 自动归一 | UE | ✅（setClip 预转换）| P0 |
| 5 | 局部 TRS → world 矩阵 | UE `FCompactPose` | ✅ | AN-01 |
| 6 | skin matrix = world × IBM | UE | ✅ | AN-01 |
| 7 | 循环 wrap (mod) | UE/Unity | ✅ | AN-01 |
| 8 | 时间 clamp | UE `bLoop=false` | ✅ | AN-01 |
| 9 | playRate 控制 | UE/Unity | ✅ | AN-01 |
| 10 | **Float track 参数曲线出口** | UE `UAnimInstance::GetCurveValue` | 🔧 | P0 |
| 11 | Anim Notify 事件 | UE `FAnimNotifyEvent` | ❌ | Phase 1.5 |
| 12 | **拓扑序 assert** | UE `RebuildPoseCache` | 🔧 | P0 |
| 13 | **矩阵方向 lock-test** | UE `FAnimationRuntime` | 🔧 | P0 |
| 14 | **IBM = 0 NaN-safe** | UE `Check` macros | 🔧 | P0 |
| 15 | CrossFade | UE `UAnimMontage` | ❌ | Phase 2 |
| 16 | Blend 1D / 2D | UE `BlendSpace` | ❌ | Phase 2 |
| 17 | Additive 动画 | UE `bAdditive` | ❌ | Phase 2 |
| 18 | 骨骼遮罩 Mask | UE `FAnimationRuntime::BlendPosesInGraph` | ❌ | Phase 2 |
| 19 | Montage / Slot / Layer | UE `UAnimMontage` | ❌ | Phase 2 |
| 20 | AnimGraph (Node-based) | UE `UAnimGraphSchema` | ❌ | Phase 3 |
| 21 | L1-L2 状态机 | UE/Unity | ❌ | Phase 3 |
| 22 | L3-L4 状态机 + MotionMatching | UE `UAnimInstance` | ❌ | Phase 3 |
| 23 | TwoBone IK | UE `AnimNode_TwoBoneIK` | ❌ | Phase 4 |
| 24 | FABRIK / CCD IK | UE `AnimNode_Fabrik` | ❌ | Phase 4 |
| 25 | IK 约束 (angle/dist/rot) | UE `AnimNode_LookAt` | ❌ | Phase 4 |
| 26 | FullBody IK | UE `FBIK` | ❌ | Phase 4 |
| 27 | 骨骼重定向 | UE `RetargetSource` | ❌ | Phase 4 |
| 28 | Root Motion | UE `ERootMotionMode` | ❌ | Phase 4 |
| 29 | Dual-Quaternion Skinning | UE `bUseDualQuaternion` | ❌ | Phase 2 |
| 30 | CPU 蒙皮 (顶点变形) | Unity `SkinnedMeshRenderer.BakeMesh` | ❌ | Phase 2 |
| 31 | GPU 蒙皮 + Skeleton UBO | UE/Unity | ⚠️ SkeletonComponent 已 wire,renderer 未读 | Phase 2 |
| 32 | Morph Target / BlendShape | UE `FAnimSequence::MorphTarget` | ❌ | Phase 4 |
| 33 | 自适应压缩 | UE `AnimCompress` | ❌ | Phase 5 (Converter 侧) |
| 34 | LOD 动画 | UE `LODThreshold` | ❌ | Phase 5 |
| 35 | Debug 可视化 | UE `AnimDebugView` | ❌ | Phase 5 |
| 36 | Profiler (hot path 标记) | UE `STAT_Anim` | ❌ | Phase 5 |
| 37 | AnimCurve (Float param track) | UE `UAnimInstance::GetCurveValue` | 🔧 | P0 (sink 出口) |

**统计**：37 项工业能力中，AN-01 已 ship **9**（24%），P0 修 +5（合计 14，38%），Phase 2-5 还需 **23** 项（62%）。

---

## 14. P0-P3 路线图（2026-07-26 修订）

### P0 — 架构债收口（2026-07-26 起，1 PR 量）

| Step | 文件 | 内容 | 测试 |
|---|---|---|---|
| P0.1 | AYAnimation/design.md | 修订（本文档）| — |
| P0.2 | Skeleton.h/cpp + Animation.h/cpp | **删** parallel type | 编译通过 |
| P0.3 | AnimationPlayer.h/cpp | 改消费 `ISkeleton/IAnimation` | 现有 6 case 仍 PASS |
| P0.4 | KeySampler.cpp | `dot<0` slerp 选优 | 现有 Quat slerp 仍 PASS + 新 dot<0 case |
| P0.5 | CMakeLists.txt | `target_link_libraries(... AYResource)` | 编译通过 |
| P0.6 | AYEntity/AYResourceAnimationAdapter.{h,cpp} | **删**（被 AnimationPlayer 直接消费替代）| SkinnedAnimationTest 仍 PASS |
| P0.7 | AnimationPlayer.h/cpp | Float track sink + 预转换 ticks | 新 `float_track_drives_parameter` case |
| P0.8 | AnimationPlayer.cpp | topology assert + matrix 约定 lock-test + IBM=0 safe | 3 个新 case |

**完成定义**：8 步全 ✓ + 16+ tests 3-run stable。

### P1 — 工业基础（~2 PR 量）

| Step | 内容 |
|---|---|
| P1.1 | Anim Notify 事件系统（多播 `function<void(NotifyEvent)>`）|
| P1.2 | Additive 动画层 |
| P1.3 | CrossFade in/out |
| P1.4 | Hot-path 优化：track → boneIndex 预解析（消除每帧 hash + strcmp）|
| P1.5 | Tick cache（多 player 共享 skeleton 时避免重复 evaluate）|

### P2 — 混合 + 蒙皮（~3 PR 量）

| Step | 内容 |
|---|---|
| P2.1 | Blend 1D / Blend 2D（BlendTree 节点类型）|
| P2.2 | 骨骼遮罩 (Skeleton Mask) |
| P2.3 | 多 Slot / 多 Layer |
| P2.4 | Dual-Quaternion Skinning |
| P2.5 | CPUSkinningPass（独立 module，CPU 顶点变形真输出）|
| P2.6 | AYRenderer 改造：SkinnedLit 读 `SkeletonComponent.skinMatrices`（统一渲染路径）|

### P3 — 状态机 + IK + 重定向（~5 PR 量）

| Step | 内容 |
|---|---|
| P3.1 | L1-L2 状态机 |
| P3.2 | L3 子状态机 |
| P3.3 | L4 MotionMatching 风格 |
| P3.4 | TwoBoneSolver + FABRIK + CCD |
| P3.5 | IK 约束 (angle / distance / rotation) |
| P3.6 | 骨骼重定向 (BoneMappingTable) |
| P3.7 | Root Motion 通道 |
| P3.8 | Morph Target / BlendShape |

### P4 — 优化（~3 PR 量）

| Step | 内容 |
|---|---|
| P4.1 | 自适应压缩（Converter 侧）|
| P4.2 | LOD 动画 |
| P4.3 | Debug 可视化 + Profiler |
| P4.4 | AnimGraph 节点编辑器（Editor 集成，跨 module）|

---

## 15. 与旧版的关键差异（migration notes）

| 旧版（pre-P0） | 新版（post-P0） | 原因 |
|---|---|---|
| `ayt::anim::Skeleton` 独立类型 | **删除**,改用 `ayt::resource::ISkeleton` | 平行造轮 → 一份真理源 |
| `ayt::anim::Animation` 独立类型 | **删除**,改用 `ayt::resource::IAnimation` | 同上 |
| `ayt::anim::Bone` 独立类型（仅 3 字段）| **删除**,改用 `ayt::resource::Bone`（含 local TRS）| 字段对齐 |
| `ayt::anim::KeyframeTrack` 独立类型 | **删除**,改用 `ayt::resource::AnimTrack` | 同上 |
| `AYEntity::adapter::loadSkeleton/Animation` 转换层 | **删除**,AYAnimation 直接消费 AYResource 接口 | 减少转译损耗 |
| `AnimationPlayer::play(const Animation*)` | `play(const IAnimation*)` | 签名改 |
| `track.times` 直接当秒用 | 内部预转换 ticks→s,缓存 normalized times | 单位约定统一 |
| Quaternion slerp 无 dot<0 选优 | **修复**（Phase 1.5）| 视觉抽搐根因 |
| Float track 静默丢弃 | **暴露 sink**（Phase 1.5）| silent data loss 修 |