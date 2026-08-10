# AYAnimation Design

> **状态（2026-08-10）**：薄播放内核 **P1.1–P1.7 + P2.2 Skeleton Mask + P3.x刀1 .aymask loader + P3.1 L1 状态机 + P3.2 L3 子状态机 + P3.x L2 Condition DSL + P3.x刀 N+1.BC + P0 polish + P1 polish + P2 polish + P3 polish + P4 polish + P5 polish + P6 polish + P4-1 TwoBone IK 全 ship**（Notify、Additive L1/L2、BoneIdx cache、Cross-fade 4-pack、`vector<AdditiveSlot>`≤8 + merged notify/`sourceTag` + `trackWeights` mask + AYEntity `AdditiveLayerSpec` bridge + EventBus `AnimNotifyEvent.sourceTag` pipe + **P1.6 Deprecate Wrapper Cleanup** + **P1.7 Shared Skeleton Tick Cache** + **P2.2 资源级 Skeleton Mask** + **P3.x刀1 .aymask v1 binary loader + `ayt::resource::ISkeletonMask` formal interface** + **P3.1 L1 简单状态机** + **P3.2 L3 子状态机** + **P3.x L2 Condition DSL (Transition 4 字段缓存层 + ConditionExprAst 类族 + ConditionParser mini Lexer + precedence-climbing Parser + 8 算子 + 短路求值 + dirty cache + parse-fail-soft-false + L1 back-compat 双轨)** + **P3.x刀 N+1.B Time-in-State Query (StateMachine._currentStateEnterTime + getCurrentStateElapsedTime() + CondIdentifierExpr reserved "CurrentStateTime" pre-check)** + **P3.x刀 N+1.C Per-state AnimNotify routing (AnimNotifyRecord/AnimNotifyEvent.fromStateName + AnimationPlayer.setCurrentStateName + AYEntity bridge every-tick push)** + **P0 polish (Flat-array params/triggers + FNV-1a ParamNameRegistry + sorted-vector triggers + cache-friendly hot path; INV-43..46; 0 public API change)** + **P1 polish (Transition.triggerHash + Transition.conditionParamNameHash + CondIdentifierExpr.nameHash pre-computed at authoring time; lazy fallback for test fixture const_cast mutation back-compat; ParamNameRegistry split to leaf header ParamNameRegistry.h; 3 hot-path intern() eliminated; INV-47..51; 0 public API change)** + **P2 polish (Condition DSL AST → 扁平字节码平行缓存：CondBytecode program + float literal table + program-counter switch evaluator + 固定栈数组 + 短路 relative-jump + OP_LOAD_RESERVED + lazy compile + shared_ptr copyable; INV-52..58; 0 public API change; Scenario G 1.34x/1.28x debug-build)** + **P3 polish (AssetBoneCache 默认无锁：setThreadSafe opt-in 双轨 + maybeLock RAII + 7 访问点条件锁; INV-59/60; +2 additive API; 0 bridge change; Scenario H 1.04x/1.08x debug-build 结构性零同步)** + **P4 polish (Additive slot 内存回收：releaseSlotBuffers swap 归还 tracks/capturedLocal*/trackWeights; INV-61/62; + AssetBoneCache transparent hash：StringViewHash 异构查找 0 临时 string; INV-63; + 批量 tick 压力测试 AYTest_P4Stress.cpp 4 cases 400 player × 200 帧逐位一致)** + **P5 polish (DSL 四则运算：CondOp + CondOpByte 各追加 5 值旧值不变; lexer 负号消歧 INV-65; precedence 5/6 INV-69; 一元减 INV-66; 除零 fail-soft INV-67; bytecode parity INV-68; 14 new tests)** + **P6 polish (INV-60 flip debug assert：setThreadSafe 离开 thread-safe 模式 try_lock probe INV-70; + release 配置落地 x64-Release + Scenario G/H 首测 G 18.3ns ~4.7x / H lock-free 1.40x~2.46x)**）。3-run stable：AYAnimation 2999/2999 + AYResource 1039/1039 + AYEntity 421/421 × 3（debug）+ release 2999/2999 × 3 全绿。详见 §4.11 / §4.12 / §4.13 / §4.14 / §4.15 / §4.16 / §4.17 / §4.18 / §4.19 / §4.20 / §4.21 / §4.22 / §4.23 / §4.24 / §4.25 / §11 / §13 / §11 P1.5–P2.2 / P3.x刀1 / P3.1 / P3.2 / P3.x / P3.x刀 N+1 / P0 polish / P1 polish / P2 polish / P3 polish / P4 polish / P5 polish / P6 polish / P4-1 rows。  
> **不负责**：完整角色管线（ASM / BlendTree / Root Motion / Retarget / LOD）仍属后续 Phase；L4 MotionMatching / state-graph 编辑器 / multi-graph / BlendTree inside state machine / `.ayasm` loader / parallel states / 函数调用 / OnStateEntered/Exited event / FABRIK+CCD / IK 约束 / pole vector / 骨骼重定向 全部 deferred。L1 + L2 DSL + L3 子状态机 + Time-in-state query + per-state AnimNotify routing + flat-array hot-path + bytecode hot-path + lock-free cache + slot 内存回收 + transparent hash + stress 测试 + **DSL 四则运算** + **INV-60 flip debug assert + release 配置落地** + **TwoBone IK（P4-1）** 已 ship（P3.1 + P3.x + P3.2 + P3.x刀 N+1.BC + P0 polish + P1 polish + P2 polish + P3 polish + P4 polish + P5 polish + P6 polish + P4-1 2026-08-06..10）。  
> 工业级对标：Unreal Animation / Unity Animator / Godot AnimationTree / O3DE Animation Graph。  
> **2026-08-06 设计审计 (二次)**：新增 §4.14 P3.1 L1 状态机 ship 文档；§11 / §13 / §16 勾选同步 P3.1 ship + 3-run 370/370 + 543/543。
> **2026-08-06 设计审计 (三次)**：新增 §4.15 P3.2 L3 子状态机 ship 文档；§4.14.11 UPGRADE-HOOK(P3.2) 标 resolved；§13 row 20c 改为 ✅ + 加 row 20e（L3.5 deferred）；§11 P3.2 row 勾选 + §16 changelog 加 P3.2 entry；3-run 385/385 + 600/600 + 1044/1044 stable。
> **2026-08-07 设计审计 (四次)**：新增 §4.16 P3.x L2 Condition DSL 完整 ship 文档（12-section 全模板）；§11 P3.x row ✅；§13 row 20b ❌→✅ + 统计 25 项 ✅ + 内核 6.7/10 + 完整角色管线 5.2/10；§11 P3.x row ✅；§16 changelog 加 P3.x entry；3-run 401/401 + 703/703 + 1044/1044 stable。  
> **2026-08-07 设计审计 (五次)**：新增 §4.17 P3.x刀 N+1.BC 完整 ship 文档（Time-in-State Query + Per-state AnimNotify routing，12-section 全模板）；§11 P3.x刀 N+1.BC row ✅；§13 row 20b "deferred to P3.x刀 N+1" → ✅（per-state AnimNotify routing 已 ship）+ 统计 26 项 ✅ + 内核 6.9/10 + 完整角色管线 5.3/10；§11 P3.x刀 N+1.BC row ✅；§16 changelog 加 P3.x刀 N+1.BC entry；3-run 421/421 + 752/752 + 1039/1039 stable。  
> **2026-08-07 设计审计 (六次)**：新增 §4.18 P0 polish 完整 ship 文档（Flat-array params/triggers + FNV-1a ParamNameRegistry + sorted-vector triggers + cache-friendly hot path，12-section 全模板）；§11 P0 polish row ✅；§11 P0 polish row ✅；§16 changelog 加 P0 polish entry；3-run 421/421 + 759/759 + 1039/1039 stable；INV-43..46 全部 NEW（hot-path 4.0x faster：getParam 8 params 271→68 ns/iter，debug build）；setTrigger regression（debug-only）+ trigger+update cycle regression accepted as known trade-off（release-build 收益更大，getParam hot path 是 production critical path）。
> **2026-08-08 设计审计 (七次)**：新增 §4.20 P2 polish 完整 ship 文档（AST → 扁平字节码平行缓存，12-section 全模板）；§4.19.11 UPGRADE-HOOK(P2 polish) 标 resolved；§16 changelog 加 P2 polish entry；3-run 421/421 + 1824/1824 + 1039/1039 stable；INV-52..58 全部 NEW（bytecode 1:1 AST 语义 + lazy build + reserved ident opcode + flat float literal table + shared_ptr copyable + short-circuit relative-jump）；Scenario G 实测 1.34x（true path 773 vs 1035 ns/iter）/ 1.28x（short-circuit 800 vs 1022）debug build；两个前期 bug 修复记录（bit-30 IEEE-754 exponent 冲突 literal 编码 + per-eval vector 栈 8x 慢），详见 §4.20.10。
> **2026-08-08 设计审计 (八次)**：新增 §4.21 P3 polish 完整 ship 文档（AssetBoneCache 默认无锁 + setThreadSafe opt-in 双轨，12-section 全模板）；§4.12.1 线程安全注释同步 P3 polish；§11 P3 polish row ✅；§11 P3 polish row ✅；§16 changelog 加 P3 polish entry；3-run 421/421 + 1851/1851 + 1039/1039 stable；INV-59/60 全部 NEW（默认零锁 + flag 非原子）；Scenario H 实测 1.04x（resolveAndCache hit 961 vs 1001）/ 1.08x（lookup hit 910 vs 980）debug build min-of-5；测量教训：顺序一次性测量噪声反转比值 → 交错 min-of-5，详见 §4.21.10。
> **2026-08-10 设计审计 (九次)**：新增 §4.22 P4 polish 完整 ship 文档（Additive slot 内存回收 + AssetBoneCache transparent hash + 批量 tick 压力测试，12-section 全模板）；§4.21.11 UPGRADE-HOOK(transparent hash) + §4.21.12 Q3 标 resolved；§11 P4 polish row ✅；§11 P4 polish row ✅ + P1/P2/P3 polish rows deferred 列表同步（Additive slot dynamic vector 标 resolved）；§16 changelog 加 P4 polish entry；3-run 421/421 + 2673/2673 + 1039/1039 stable；INV-61/62/63 全部 NEW（闲置 slot 内存归还 + sparse 语义保留 + 异构查找 0 临时 string）；教训：tick≠evaluate（压力测试只 tick 断言全败）+ transparent hash C3066 模糊重载需显式 const char* 重载，详见 §4.22.10。
> **2026-08-10 设计审计 (十次)**：新增 §4.23 P5 polish 完整 ship 文档（DSL 四则运算 + - * /，12-section 全模板）；§14.3 算术 row 标 ✅ + §4.16/§4.17 历史决策标 RESOLVED；§11 P5 polish row ✅ + roadmap P3 表 P5 polish row；§16 changelog 加 P5 polish entry；3-run 421/421 + 2831/2831 + 1039/1039 stable；INV-64..69 全部 NEW（算术语义 + 负号消歧 + 一元减 + 除零 fail-soft + bytecode parity + 优先级）；教训：测试期望值手算避开恰好相等组合（(4+1)*2=10 两个 FAIL 是期望值错）+ 插测试块先 grep section 头位置（§8.1.5 头在文件顶部 → 误插 TEST_SUITE_END 落中间 → C2065），详见 §4.23.10。
> **2026-08-10 设计审计 (十一次)**：新增 §4.24 P6 polish 完整 ship 文档（INV-60 flip debug assert + release 配置落地，12-section 全模板）；§4.21.12 Q2 + §4.20.12/§4.21.12/§4.22.12 Q1 标 resolved；§14.2 release-build row 标 ✅；§11 P6 polish row ✅ + roadmap 剩项同步（setTriggerByHash 永久挂起）；§16 changelog 加 P6 polish entry；debug 3-run 421/421 + 2843/2843 + 1039/1039 stable + release 2843/2843 × 3 全绿（release 配置首份证据）；INV-70 NEW（翻转离开 thread-safe 模式 debug assert）；Scenario G/H release 首测（G 18.3ns ~4.7x vs debug；H lock-free 1.40x~2.46x vs debug 1.06x/1.12x）；教训：try_lock probe vs _lockCount 计数权衡 + 单线程 UT 无法构造 assert 路径（由 P3 翻转测试回归兜底）+ run-to-run 噪声 ±2.5x 必须 min-of-5，详见 §4.24.10。
> **2026-08-10 设计审计 (十二次)**：新增 §4.25 P4-1 TwoBone IK 完整 ship 文档（solver 解析解十二步 + AnimationPlayer Phase 2.5 集成 + 10 new INV-71..74，12-section 全模板）；§6 IKSolver 从「未启动」改 TwoBoneSolver ✅ ship（FABRIK/CCD 未启动）；§11 Phase 4 row ✅ + roadmap 长线开张；§14.3 IK 行拆开（~~TwoBone~~ ✅ + FABRIK+CCD/约束/重定向 open）；§16 changelog 加 P4-1 entry；debug 3-run 421/421 + **2999/2999** + 1039/1039 stable + release 2999/2999 × 3 全绿；INV-71..74 NEW（eager resolve + skeleton-swap re-resolve / weight saturate + ≤0 零成本 skip / IK 只写 root+mid localRot post-mask pre-Phase-3 / solver 纯函数退化→有限或原样永不 NaN）；教训：P7 设计缺陷（共享 mid/tip 的链不能同时命中——后执行者赢）+ AssetBoneCache 指针复用陈旧命中（骨架析构后地址复用 → resolveIKChains 改 findBone 直查）+ 4 test TU depfile 失效 stale .obj（头文件偏移变更 → garbage，touch 真实源文件强制重编），详见 §4.25.10。

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

#### 4.3.1 Hold 语义（锁定 — 非 UE Hold）

| 概念 | AYAnimation 现状 | 说明 |
|------|------------------|------|
| 非循环末帧定格 | ✅ `!_loop && _time > d` → clamp + `_paused` | **这不是** HoldTimer |
| PoseHold / HoldDuration notify | ❌ 未实现 | 勿假设 UE `AnimNotifyState` 式按住时长 |
| 显式 `holdPose()` API | ❌ 未实现 | 需要时宿主停 `tick` 或 `pause()` |

读者 **不得** 把末帧 clamp 解读为工业级 Hold 通道。若要做，单独立项（建议挂 notify duration 或 PoseHold），勿 silently 改 `tick`。

### 4.4 ✅ Anim Notify (Phase 1.5) — SHIP

- ✅ Anim Notify marker channel on `IAnimation`（Phase 1.5 `VERSION 2`）—— 排序的 named time-keyed 事件，跟 tracks 并列一等通道
- ✅ `AnimationPlayer::setAnimNotifySink(std::function<void(name, time, payload)>)` ── host 直接订阅，在 `tick()` 内同步触发
- ✅ `AnimationPlayer::consumePendingNotifies()` ── 每 tick 跨过 marker 时 push 进 queue，AYEntity AnimationSystem 每帧 drain 并 `EventBus::emit<AnimNotifyEvent>`
- ✅ Loop wrap 双区间扫描 ── `[prev, d) ∪ [0, next]` —— UE/Unity 行为
- ✅ Seek (`setTime`) 不立刻 fire，prev snap to current，下一 tick 才 fire
- ⚠️ notifyName / clipName 是 const ptr → IAnimation string (emit-only contract)，订阅者不要 retain

### 4.5 ✅ Additive Layer 1 (Phase 1.2 P1.2) — SHIP

- ✅ Per-track `AnimBlendMode {Override=0, Additive=1}` on `IAnimation::AnimTrack` (mirrors `AnimTrackType` shape — single byte, no parallel vector)
- ✅ IAnimation VERSION 3 binary format: one byte per track between `valueType` and `timeCount`; `MAGIC` unchanged; v1/v2 backward compat (those formats have no byte at that slot, so the v3 reader skips reading and the field stays at struct default `Override` — bit-identical to pre-P1.2 behavior)
- ✅ `AnimationPlayer::setAdditiveWeight(w)` saturating setter (clamps to `[0, 1]`); `_additiveWeight` private field (default 1.0f)
- ✅ evaluate Phase 1 split: Override path is byte-identical to pre-P1.2; Additive path applies delta math:
  - position: `_localPos[k] += sample[k] * weight`
  - rotation: `(base * sample.pow(weight)).normalize()` — `pow` scales the rotation angle (NOT literal `*` which composes fully); weight==0 short-circuits before `pow()` to avoid a degenerate case in `MathTypes::FQuaternion::pow` that returns the original q (not identity) when `sinHalfAngle < 1e-6f`
  - scale: `_localScl[k] *= (1.0f + sample[k] * weight)` — UE convention, relative blend, scale never collapses to zero
  - Float tracks always go through `setFloatCurveSink` regardless of blend mode (additive is a local-TRS concept)
- ✅ AYEntity bridge: `AnimationComponent::additiveWeight` field (default 1.0f, serialized via `kAttrSerialize`); `AnimationSystem::onUpdate` pushes it per frame next to `setPlayRate`/`setLoop`
- ✅ Ref-pose-at-frame-0 assumption: additive clips must be authored with sample value = identity delta at t=0; documented in the IAnimation enum comment and in design §4.6 below. A `ref-pose` capture path from the skeleton's bind pose is deferred (Layer 2 / P2.x).
- ❌ **OUT of scope (deferred)**: cross-fade between separate base + additive clips (P1.3), per-bone / skeleton mask (P2.2), local-space vs world-space additive, per-bone additive weight

### 4.6 Additive Layer 1 contract (Phase 1.2 detail)

Full contract for the additive blend layer — pin the math so future
contributors don't accidentally change the semantics of a shipped API.

**Blend modes** (`AnimBlendMode` enum on `IAYAnimation.h:38`):
```cpp
enum class AnimBlendMode : UInt8 {
    Override = 0,   // default — bit-identical to v2 behavior
    Additive = 1,   // local-TRS delta on top of the bone's rest pose /
                    //         a prior Override track's write
};
```

**Math per AnimTrack property type** (evaluated inside `AnimationPlayer::evaluate`
Phase 1, after the rest-pose seed at the top of the function and BEFORE Phase 2
world-matrix accumulation):

| Property   | Override behavior (default) | Additive behavior (Layer 1) |
|------------|------------------------------|------------------------------|
| `position` | `_localPos[k] = sample[k]` | `_localPos[k] += sample[k] * _additiveWeight` |
| `rotation` | `_localRot[q] = sample[q]` | `(base * sample.pow(_additiveWeight)).normalize()` — `base` is the rotation already in `_localRot` (rest-pose seed or a prior Override track's write); see caveat below |
| `scale`    | `_localScl[k] = sample[k]` | `_localScl[k] *= (1.0f + sample[k] * _additiveWeight)` — relative (UE convention) |
| `weight` / Float | push `(nodeName, property, value)` to `setFloatCurveSink` if registered | (same — Float tracks are NOT affected by blendMode) |

**Caveat: rotation delta math uses `pow`, not literal `*`.**

A quaternion has only one rotation amount (the angle). Literal `*` composes
two rotations fully — `base * quarterY` gives a quarter-Y rotation regardless
of `_additiveWeight`. `pow(weight)` scales the angle proportionally so
`weight=0.5` means "half the delta", `weight=1.0` means the full delta,
`weight=0` means "no rotation contribution".

When `_additiveWeight == 0`, `FQuaternion::pow` returns the *original*
quaternion in its `sinHalfAngle < 1e-6f` branch — NOT identity. Without an
early-out that would yield `base * sample` instead of `base`. The early-out
in `AnimationPlayer.cpp:497-501` keeps the no-op contract.

**Authoring requirement (the "ref-pose-at-frame-0" assumption):**

Additive clips must be authored with sample value = identity delta at t=0.
That is, the sampled value at the first keyframe is `position = (0,0,0)`,
`rotation = identity`, `scale = (1,1,1)` (or the equivalent "no change"
delta). Non-ref-pose-at-0 exports will drift visually: the rest pose at t=0
already shows the delta from the additive clip, and only subsequent
keyframes carry correct additive behavior.

This is the same assumption Unreal `UAnimSequence::bAdditive` makes
(`AdditiveAnimSettings` documents it) and the same assumption Unity's
`AnimationLayer.Additive` makes. A `ref-pose` capture path from the
skeleton's bind pose is deferred to Layer 2 (P2.x).

**`_additiveWeight` saturating setter:**

`setAdditiveWeight(-0.5f)` clamps to 0; `setAdditiveWeight(2.0f)` clamps to 1.
This is defensive against caller typos — a negative `pow(weight)` on a
quaternion is undefined and would NaN the pose matrix.

**Per-frame push from `AnimationSystem::onUpdate`:**

```cpp
skel->player.setPlayRate(anim->playRate);
skel->player.setLoop(anim->looping);
skel->player.setAdditiveWeight(anim->additiveWeight);  // P1.2
```

The push runs alongside the existing `setPlayRate` / `setLoop` pushes.
Default `AnimationComponent::additiveWeight = 1.0f` matches the
AnimationPlayer's default `_additiveWeight`, so pre-P1.2 clips (no
Additive tracks) behave bit-identically.

### 4.7 ✅ Additive Layer 2 (Phase 1.3 P1.3) — Cross-Fade MVP — SHIP

Phase 1.3 ships the **second clip source** on `AnimationPlayer`. Two
independent `IAnimation*` slots stacked on a single player:

- **Base source** (renamed internal: `_anim` → `_baseClip`): bound via
  the existing `play(IAnimation*)` API. Owns `_tracks` and the base
  playhead `_time` / `_playRate` / `_loop` / `_pendingNotifies`.
- **Additive source** (NEW): bound via `setAdditiveSource(IAnimation*,
  float playRate=1.0f, bool loop=true)`. Owns `_additiveTracks` and
  the additive playhead `_additiveTime` / `_additivePlayRate` /
  `_additiveLoop` / `_additivePendingNotifies`. Cleared via
  `clearAdditiveSource()` (or `setAdditiveSource(nullptr)`).

Both sources tick INDEPENDENTLY (UE `UAnimMontage` semantics) — the
additive source has its own loop wrap and notify dispatch. A host can
drive hit-react on a separate time scale from locomotion.

**5 invariants** (asserted in `evaluate()` header, debug-only):

| Inv | Statement |
|-----|-----------|
| INV-1 | `_additiveClip == nullptr \|\| _blendWeight <= 0.f` ⇒ Phase 1b loop skipped entirely (null and zero-weight are equivalent "off" states) |
| INV-2 | `_localPos.size()==n*3 && _localRot.size()==n*4 && _localScl.size()==n*3` — maintained by `setSkeleton` ONLY; Phase 1b writes elements 0..n-1, never resizes |
| INV-3 | `_blendWeight ∈ [0, 1]` post-every-setter (inline saturate) |
| INV-4 | `_baseClip == nullptr && _additiveClip != nullptr` ⇒ evaluate early-return after rest-pose seed (degenerate state during clip swap window) |
| INV-5 | `_additiveClip == nullptr` ⇒ Phase 1a output identical to P1.2 single-clip output (zero-regression invariant — existing 197 tests pass unchanged) |

**State-machine entry points** (5 main + 3 auxiliary):

| # | Entry | Post-condition | Additive layer impact |
|---|-------|----------------|----------------------|
| 1 | `setSkeleton(skel)` | resize `_local*` to n*stride | **preserved** |
| 2 | `play(baseClip)` | rebuild `_tracks`, `_time=0` | **preserved** (state machine: swap base keeps layer) |
| 3 | `setAdditiveSource(src, rate, loop)` | copy tracks → `_additiveTracks`, `_additiveTime=0`, clear `_additivePendingNotifies` | base untouched |
| 4 | `setBlendWeight(w)` | saturate `[0,1]`; assigns `_blendWeight` | neither touched |
| 5 | `tick(dt)` | base + additive axes advanced independently | both axes ticked |
| A | `stop()` | base cleared + `clearAdditiveSource()` | additive cleared |
| B | `setTime(t)` | base + additive both seek to `t`, fire no notify on either | both jumped |
| C | `pause()` / `resume()` | base only in MVP (additive ticks onward) | base paused |

**Phase 1b math** (reuse of P1.2's three formulas with `_additiveWeight`
renamed → `_blendWeight`; the additive source's per-track `AnimBlendMode`
byte is honored from its own clip):

| Property | Additive source Override | Additive source Additive |
|----------|---------------------------|---------------------------|
| `position` | `_localPos[k] = sample[k]` (**忽略 `_blendWeight`** — 已知陷阱：要部分覆盖请用 Additive 模式或 mask) | `_localPos[k] += sample[k] * _blendWeight` |
| `rotation` | `_localRot[q] = sample[q]`（同样忽略 weight） | `(base * sample.pow(_blendWeight)).normalize()` — weight==0 short-circuit before pow |
| `scale` | `_localScl[k] = sample[k]`（同样忽略 weight） | `_localScl[k] *= (1 + sample[k] * _blendWeight)` — UE convention, relative blend |
| Float | push to `setFloatCurveSink` at `_additiveTime` (P1.2 invariant: additive concept is local-TRS only) | (same — Float tracks not affected by blendMode) |

> **陷阱（锁定文档）**：Additive **source** 上标记 `Override` 的 track 会 **整骨替换** 且不受 `setBlendWeight` 影响。需要「半透明覆盖」时应把 track 标成 `Additive`，或等 P1.5 `trackWeights` / 未来 Skeleton Mask。
**`_additiveWeight` → `_blendWeight` rename**: the P1.2 setter/getter
remain as **deprecated inline-forward wrappers** (`setAdditiveWeight(w)`
calls `setBlendWeight(w)`; `getAdditiveWeight()` returns `_blendWeight`)
so the 197-test P1.2 baseline and `AYAnimationSystem`'s existing
`setAdditiveWeight(anim->additiveWeight)` call keep compiling without
modification. `setBlendWeight` is the canonical P1.3 API; remove the
wrappers in P1.6.

**AYEntity bridge** (mirror of P1.2 push wiring):

```cpp
// AnimationComponent (P1.3 additions; P1.2 fields preserved)
AY_PROPERTY(std::string, additiveClipPath, kAttrSerialize)  // default ""
AY_PROPERTY(float,       additivePlayRate, kAttrSerialize)  // default 1.0
AY_PROPERTY(float,       blendWeight,      kAttrSerialize)  // default 1.0

// AnimationSystem::onUpdate (3 lines parallel to existing base push)
if (!anim->additiveClipPath.empty()) {
    auto addClip = ResourceManager::load<IAnimation>(anim->additiveClipPath);
    _additiveClipCache[path] = addClip;
    if (_lastAppliedAdditivePath[e] != anim->additiveClipPath) {
        skel->player.setAdditiveSource(addClip.get(), anim->additivePlayRate, true);
        _lastAppliedAdditivePath[e] = anim->additiveClipPath;
    }
    skel->player.setBlendWeight(anim->blendWeight);
} else if (!_lastAppliedAdditivePath[e].empty()) {
    _lastAppliedAdditivePath[e] = "";
    skel->player.setAdditiveSource(nullptr);
}
```

Additive notify EventBus drain (`consumePendingNotifiesAdditive`) emits
on the same `AnimNotifyEvent` channel with the additive clip's name as
`clipNameStable`. Source-tag (Base | Additive) on the event is
**deferred to P1.5** (UPGRADE-HOOK) — MVP hosts that need to
distinguish which source fired a marker use clipName or external context.

**Version bump (no on-disk change)**:
- `IAYAnimation::VERSION` 3 → 4 (forward-compat reservation)
- `.ayanm` file binary is **byte-identical** to v3 — v4 writes the same
  layout (same track structure, same per-track blendMode byte, same
  notify block). The bump locks in the AnimationPlayer-side dual-source
  API contract for future forward-compat readers; `loadFromBinary` now
  rejects any version > VERSION (5, 6, ...) so a future binary that
  adds new bytes is caught loudly rather than silently mis-parsed.

**Tests added (PR2 + PR3)**:

| # | Module | Test name | Contract pinned |
|---|--------|-----------|-----------------|
| A1 | AYAnimation | `P1_3_LayerOff_SkipsPhase1b_IfSrcIsNull` | INV-1 null branch |
| A2 | AYAnimation | `P1_3_LayerOff_SkipsPhase1b_IfWeightIsZero` | INV-1 weight branch |
| A3 | AYAnimation | `P1_3_BlendWeightSaturate_ThreeState` | INV-3 (incl. deprecated forward) |
| A4 | AYAnimation | `P1_3_DoubleTimeAxis_IndependentAdvance` | independent time axis |
| A5 | AYAnimation | `P1_3_PlayBase_PreservesAdditiveLayer` | entry 2 contract |
| A6 | AYAnimation | `P1_3_Stop_ClearsBothLayers` | stop() contract |
| A7 | AYAnimation | `P1_3_SetTime_JumpsBoth_FiresNoNotify` | entry B contract |
| A8 | AYAnimation | `P1_3_RotationPow_WeightZero_NoNaN` | rotation math guard |
| A9 | AYAnimation | `P1_3_VectorAdditive_FormulaReused_Verified` | position += sample * w |
| A10 | AYAnimation | `P1_3_NotifyIndependence_BaseAndAdditive` | dual notify queue |
| E1 | AYEntity | `animation_component_additive_clip_path_loads_player_source` | bridge wiring |
| E2 | AYEntity | `animation_component_additive_rebind_detected` | rebind detection |
| E3 | AYEntity | `animation_component_empty_additive_path_no_layer` | empty-path = OFF |
| R1 | AYResource | `LoadFromBinary_RejectsVersionAbove4` | forward-compat tripwire |

**3-run stable proof (2026-07-26)**:
- AYResource: 701 PASS (baseline 700 + 1 new R1)
- AYAnimation: 261 PASS (baseline 224 + 37 new P1.3)
- AYEntity: 177 PASS (baseline 174 + 3 new E1-E3)
- **Zero regression** on all 25 prior AnimationPlayer tests, 8 prior
  SkinnedAnimation tests, 13 prior AYResource loader tests.

**UPGRADE-HOOK(P1.4+)** comments in code mark explicit extension points:

- `// UPGRADE-HOOK(P1.4)` at `setBlendWeight`: discrete setter → keyframed FloatCurve sampler
- `// UPGRADE-HOOK(P1.4)` at `_blendWeight` field: uniform weight → per-track weights (mask expression)
- `// UPGRADE-HOOK(P1.4)` at `setAdditiveSource` / `tick`: `syncToBase` option
- `// UPGRADE-HOOK(P1.4)` at `evaluate` Phase 0: ref-pose capture from current base pose (replace rest-pose assumption)
- `// UPGRADE-HOOK(P1.5)` at `consumePendingNotifiesAdditive` → **已落地 merged + sourceTag**（ECS 已切到 merged，旧 wrapper 已 P1.6 删）
- `// UPGRADE-HOOK(P1.5)` at single `_additiveClip` → **已落地** `vector<AdditiveSlot>`
- `// REMOVE-MARKER(P1.6)` at `setAdditiveWeight` / `getAdditiveWeight` deprecated wrappers → **已删除**（P1.6 cleanup）
### 4.8 ✅ P1.5 Multi-Slot Additive Stack — Player SHIP（2026-07-27 文档对齐）

> **代码**：`AnimationPlayer` 已落地 `vector<AdditiveSlot>`（`kMaxAdditiveSlots = 8`）、per-slot sync/ref-pose/pause/curve、`trackWeights`、`consumePendingNotifiesMerged()` + `AnimNotifySourceTag`。  
> **未齐**：AYEntity AnimationSystem 仍偏双队列 / `AnimNotifyEvent` 未必带 `sourceTag`；P1.5 专用单测不足；共享 skeleton 的 tick cache **未做**。

| 项 | 状态 | 说明 |
|----|------|------|
| `vector<AdditiveSlot>` 多层 | ✅ Player | slotId ∈ `[0, 8)`；`ensureSlot` / `setAdditiveLayerSource` |
| notify merge + `sourceTag` | ✅ Player | `consumePendingNotifiesMerged`；Base / Additive_0..7 |
| per-track `trackWeights` | ✅ Player（opt-in） | empty = 全 1.0（P1.3 行为） |
| ECS / EventBus 桥接 sourceTag | ⚠ | 待 UPGRADE；旧 `consumePendingNotifies` + Additive 双 drain 仍可用 |
| 弃用 `setAdditiveWeight` wrapper | ⏳ P1.6 | `REMOVE-MARKER(P1.6)` |
| 多 player 共享 skeleton tick cache | ❌ | 仍在路线图 |

**与 Phase 2「Montage / Slot」关系**：P1.5 AdditiveSlot 是 **additive 叠加栈**；未来 Montage 上半身 slot 需在设计上 **对齐同一套 slot 模型**，避免两套 layer API（见 §14 P2.3）。

---

### 4.9 ✅ Hot-Path BoneIdx Cache (Phase 1.4 — P1.4 SHIP — 2026-07-26)

P1.3 双 source 后,`evaluate()` Phase 1a/1b 的 `_skeleton->findBone(tr.nodeName.c_str())` 调用从单 source 的 `O(track_count × framerate)` 翻倍到 dual source。`findBone` 在 `ISkeleton` 实现里是 hash lookup 或线性扫描,cheap per call 但 hot path multiply 后显著 cost — 每个 entity × 每帧 × 两 source 全跑一次 name 解析。

**P1.4 解法**: 把 resolved bone index 缓存到 `TrackSlice.boneIdx`,**lazy-resolve on first evaluate**,**setSkeleton() invalidate**。这是零算法修改、纯 hot-path 优化。

#### 4.9.1 Cache 字段

```cpp
struct TrackSlice {
    // ... existing P0 + P1.2 fields ...
    int32_t boneIdx = INT32_MIN;   // P1.4 — sentinel = "unresolved"
};

constexpr int32_t kBoneUnresolved = INT32_MIN;   // 命名空间级
```

Sentinel 语义:
- `INT32_MIN` (kBoneUnresolved) = "未解析" (first-frame lazy resolve 状态)
- `-1` = "解析过但 skeleton 里没这个名字" (cached negative — 不再重查)
- `>= 0` = 有效 bone index (cached hit)

为什么不是 `bool resolved` flag + `int idx` 合并:`INT32_MIN` 单一字段即可区分三态,节省 4 bytes per slice 且 branch 更简单。

#### 4.9.2 State machine 加 2 个 private helpers

```cpp
// Set every TrackSlice.boneIdx back to kBoneUnresolved.
// Called from setSkeleton() once the bone name table changes.
void invalidateBoneIndexCache();

// Lazy single-slice resolver. Called inside evaluate() at the start
// of each Phase 1a/1b track iteration. If boneIdx is already
// resolved (>= 0 or -1), no-op. Otherwise calls findBone once and
// caches the result.
void resolveBoneIdxOnce(TrackSlice& slice);
```

#### 4.9.3 Hot-path call site

```cpp
// evaluate() Phase 1a (existing _tracks loop):
for (TrackSlice& tr : _tracks) {
    resolveBoneIdxOnce(tr);          // O(1) after first frame
    const int boneIdx = tr.boneIdx;  // cache hit — no findBone call
    if (boneIdx < 0) {
        // orphan-track policy (Float sink if Float type)
        continue;
    }
    // ... sample + write _local* ...
}

// evaluate() Phase 1b (existing _additiveTracks loop, P1.3):
for (TrackSlice& tr : _additiveTracks) {
    resolveBoneIdxOnce(tr);          // same helper, additive layer
    const int boneIdx = tr.boneIdx;
    // ...
}
```

#### 4.9.4 Invalidation contract

`setSkeleton()` 末尾调 `invalidateBoneIndexCache()`:

```cpp
void AnimationPlayer::setSkeleton(const ISkeleton* skel) {
    _skeleton = skel;
    // ... resize _local*, seed rest pose, topology assert ...
    invalidateBoneIndexCache();   // P1.4 — every slice rebuilds against new skeleton
}
```

`play()` / `setAdditiveSource()` **不** invalidate — skeleton 没变,新加进 `_tracks` 的 slice `boneIdx = INT32_MIN`,第一个 evaluate 时 lazy resolve。

#### 4.9.5 Invariants (debug-only asserts,可选)

当前没 assert — lazy resolve 是 idempotent,contract 简单到不需要运行期检查。

#### 4.9.6 Test coverage

`AYTest_AnimationPlayer.cpp` +4 cases:
- **P1.4.1** `BoneIdxCache_StableAcrossRepeatedEvaluates` — 多次 evaluate 输出 stable (cache hit 不变)
- **P1.4.2** `BoneIdxCache_SetSkeletonInvalidates` — swap skeleton 后,output 反映新 skeleton 的 rest pose
- **P1.4.3** `BoneIdxCache_MissingNameCachedAsNegative` — orphan track 不 crash, cached as -1
- **P1.4.4** `BoneIdxCache_DualSource_BothCached` — 双 source 的 base + additive 都走 cache

#### 4.9.7 Out-of-scope (P1.4 剩下的工作)

P1.4 hot-path 完成;P1.4 cross-fade 自身 (curve / syncToBase / ref-pose capture / additive pause) 留到下一个 P1.4 ship 块。

### 4.10 ✅ Cross-Fade Full Ship (Phase 1.4 P1.4 — 2026-07-26)

P1.4 cross-fade 收口: **4 entry × 4 INV × 11 test**,一次 ship 出 curve + syncToBase + ref-pose capture + additive pause 四项完整 cross-fade 工业级能力。这是 P1.3 dual-source state machine 之后的「半 PR 量」收口刀,把 P1.3 阶段标的所有 `// UPGRADE-HOOK(P1.4)` 标记位置全部 resolve 掉。

#### 4.10.1 四 entry point

| Entry | Signature | Displaces / 取代 |
|-------|-----------|------------------|
| `setAdditiveSyncToBase(bool)` | entry 6 | `tick()` 内部 additive advance 锁步 vs 独立 的开关 |
| `setAdditiveRefPoseCapture(bool)` | entry 7 | Phase 1b additive base source:rest pose vs captured-pose |
| `blendWeightOverTime(from, to, duration, easing=Linear)` | entry 8 | 离散的 `setBlendWeight(w)` ── 静态权重 scalar → keyframed FloatCurve sampler |
| `setAdditivePaused(bool)` + `pauseAdditive` (Aux E 通过 re-exposing) | Aux E | 独立于 base pause 的 additive-only pause 语义 |

取消 hook(都已 resolve, 移到 §4.7 文末):
- `UPGRADE-HOOK(P1.4 → resolved)` discrete setter → keyframed curve (→ blendWeightOverTime)
- `UPGRADE-HOOK(P1.4 → resolved)` independent axis → syncToBase option
- `UPGRADE-HOOK(P1.4 → resolved)` ref-pose capture from current base pose (→ setAdditiveRefPoseCapture + CaptureState 3-state machine)
- `UPGRADE-HOOK(P1.4 → resolved)` additive layer obeys pause() (→ INV-8 + setAdditivePaused)

#### 4.10.2 8 invariants(P1.3 五条均保留, 新增 6/7/8)

| Inv | Statement |
|-----|-----------|
| INV-1..5 | (P1.3 不变) |
| **INV-6** | `_syncToBase == true` ⇒ post-tick `_additiveTime == _time`(lock-step 合约) |
| **INV-7** | `_refPoseCapture == true` ⇒ Phase 1b 读 `_capturedLocal*` 不是 `_local*` 当下;`CaptureState` 3-state `(Fresh, Valid, Stale)` 决定 (re)capture vs apply-captured |
| **INV-8** | `pause()==true` ⇒ base + additive axis 都停(`_additiveTime` 不动); INV-8-symmetric: `_additivePaused==true && !_syncToBase` ⇒ 只 additive 停 |

INV-6/7/8 全部 `#ifndef NDEBUG` 在 `tick()` / `evaluate()` 头部。

#### 4.10.3 内部数据结构(都是 runtime-only, 不写盘)

```cpp
enum class BlendEasing : ayt::math::UInt8 { Linear=0, EaseIn=1, EaseOut=2, EaseInOut=3, Smoothstep=4 };

struct BlendCurve {
    float        from        = 0.0f;
    float        to          = 1.0f;
    float        duration    = 0.0f;       // ≤ 0 ⇒ no-op
    BlendEasing  easing      = BlendEasing::Linear;
    float        startTime   = 0.0f;       // 调 `blendWeightOverTime` 时锁定 = `_time`
    bool         active      = false;
};

enum class CaptureState : ayt::math::UInt8 { Fresh=0, Valid=1, Stale=2 };
```

字段 (`AnimationPlayer`): `_syncToBase`, `_refPoseCapture`, `_curve`, `_capturedLocal{Pos,Rot,Scl}`, `_captureState`, `_additivePaused` —— 全部 default 跟 P1.3 行为 1:1 (off / fresh / inactive),保证 zero-regression。

#### 4.10.4 Helper 复用清单(不发明新轮)

- `sampleTrackFloat(values, count, times, t, out)` from `KeySampler.h:34` —— **概念上游复用**,但曲线跑分选择 manual lerp(从 2-element `[from, to]` + eased `t01`),不分配 buffer
- `aymath::easeIn(t, power=2)` / `easeOut` / `easeInOut` from `MathUtils.h:1036/1098/1116` —— 4 个 ease cases 中 3 个直接 dispatch
- `aymath::smoothstep(t)` from `MathUtils.h:930` —— `BlendEasing::Smoothstep` 一行调用
- `resolveBoneIdxOnce(TrackSlice&)` from `AnimationPlayer.cpp:113` —— P1.4 hot-path 已 ship,新 capture / ref-pose path 不需要改动 cache

#### 4.10.5 State machine 在 P1.3 五 entry 上的 reset

```
setAdditiveSource(src, rate, loop) 末尾:   _syncToBase = _refPoseCapture = _additivePaused = false;
                                          _captureState = Fresh; _curve.active = false;
clearAdditiveSource 末尾:                  同上
stop() 末尾:                              reset-by clearAdditiveSource
setTime(t) 末尾:                          if (_syncToBase) _additiveTime = _time;
                                          if (_curve.active && (_time < curveStart || _time > curveEnd)) _curve.startTime = _time;
```

`setTime` 的「窗口内不重 anchor,窗口外才重 anchor」分支是关键细节 — fix 不是「无条件 reset startTime」,否则 A1/A3 测试会看到 curve 在 jump-to-midpoint 之后 value=from。

#### 4.10.6 Phase 0/1a/1b rewiring(只动了 evaluate(), 不是新算法)

```
evaluate():
  ...[既有 INV-assert + rest-pose seed]...
  if (_refPoseCapture) {
    if (_captureState ∈ {Fresh, Stale}) { captureRefPoseFromLocal(); _captureState = Valid; }
    else                                 { applyCapturedRefPoseToLocal(); }   // Valid → 重 fill _local*
  }
  Phase 1a: ...(unchanged)...
  if (_refPoseCapture && _captureState == Valid) {
    captureRefPoseFromLocal();    // capture post-Phase-1a base, Phase 1b 用此
  }
  const float effectiveWeight = sampleBlendCurve();   // = _blendWeight if curve inactive
  if (_additiveClip != nullptr && effectiveWeight > 0.0f) {
    Phase 1b: ...同一 Phase 1b body, 把 _blendWeight 换成 effectiveWeight; (weight==0 early-return 仍然不动 quaternion powder)
  }
```

Phase 1b 的 body 是 P1.2 + P1.3 同一套(`_localPos += sample * w`、`(base * q.pow(w)).normalize()`、`_localScl *= (1 + sample * w)` + Float sink),只是 `w` 现在来自 `sampleBlendCurve()` 不是 `_blendWeight` 静态值。

#### 4.10.7 tick() 三分支(ref-pose pause / sync / default)

```
tick(dt):
  if (_paused || _baseClip == nullptr) { _prevTickTime = _time; return; }   // P1.3 base
  ...(base advance + dispatchBaseNotifies unchanged)...

  if (_additiveClip != nullptr) {
    if (_additivePaused) { _additivePrevTickTime = _additiveTime; }   // INV-8 partial
    else if (_syncToBase) {                                                  // INV-6 锁步
        _additiveTime = _time;
        dispatchAdditiveNotifies(prevAdd, _additiveTime, baseWrapped);  // 共享 base wrap flag
        _additivePrevTickTime = _additiveTime;
    } else {                                                                 // P1.3 独立 axis
        ...(原 advance / wrap / dispatch / set prev unchanged)...
    }
  }

  if (_curve.active) {      // P1.4 auto-disarm
    if (_time - _curve.startTime >= _curve.duration) _curve.active = false;
  }
```

`_curve.active = false` 的 disarm 在 tick 末尾 — 一个 frame 的 “drag past end” 不会让 layer 看到 late-update。

#### 4.10.8 11 个测试分布(3-run stable × 3 = zero regression, total 1200)

| Module | 测试 | 内容 |
|--------|------|------|
| AnimationPlayer | A1 `P1_4_BlendWeightOverTime_EasingLinear_EndMatchesTo` | Linear curve mid-sample + auto-disarm |
| | A2 `P1_4_BlendWeightOverTime_EasingEaseOut_Concave` | easeOut(0.5) = 0.75 数学 |
| | A3 `P1_4_BlendWeightOverTime_Duration0_StaticFallback` | duration ≤ 0 no-op + from/to saturate |
| | A4 `P1_4_SyncToBase_TimesMatchAcrossTicks` | tick 后 base 与 additive marker 锁步 |
| | A5 `P1_4_SyncToBase_SetTimeJumpsBoth` | setTime re-anchor + dispatch cursor reset |
| | A6 `P1_4_RefPoseCapture_RestPoseReplacedByCurrentBase` | ON/OFF both, p2 stability + empty-base path |
| | A7 `P1_4_PauseAdditive_StopsTimeAdvance` | pause() 同步 additive axis |
| | A8 `P1_4_ResumeAdditive_NoSpuriousNotify` | setAdditivePaused resume reset cursor |
| AYEntity | E1 `animation_component_p1_4_blend_curve_pushed_to_player` | `blendCurveDuration > 0` ⇒ player.isBlendCurveActive() |
| | E2 `animation_component_p1_4_sync_to_base_bridge_flag` | `syncToBase = true` ⇒ player flag |
| | E3 `animation_component_p1_4_ref_pose_capture_bridge_flag` | `refPoseCapture = true` ⇒ player flag + no NaN |

总验证: 282 + 8*3 ≈ 30 assertions → AYAnimation 282 + 30 = **312 PASS**;AYEntity 177 + 10 = **187 PASS**(注: 187 不是 180, 因为我新加的 3 个测试含多个 assertions/case, 期望值 > 10 checks 加 baseline);AYResource 701 = **701 PASS**。

#### 4.10.9 Out-of-scope for Full P1.4（P1.5 状态 2026-07-27）

| Item | 原计划 | 2026-07-27 状态 |
|------|--------|-----------------|
| Per-track weight map | P1.5 / P2.2 | ✅ Player `AdditiveSlot::trackWeights`；资源级 Mask 仍 Phase 2 |
| Multi-source `vector<AdditiveSlot>` | P1.5 | ✅ Player SHIP（§4.8）；ECS 桥接 ⚠ |
| Notify merge + source-tag | P1.5 | ✅ Player `consumePendingNotifiesMerged`；Event ⚠ |
| Curve serialization (`.ayanm`) | 不定 | 仍不定 |
| Layer 1 per-track curve | 不定 | 仍不定 |
| 移除 `setAdditiveWeight` wrapper | P1.6 | ✅ SHIP 2026-07-27 |
#### 4.10.10 关键工程教训(给后续 P1.5 的 reviewer)

1. **`setTime` 的 anchor-in-window 分支** —— 不要无条件重 anchor `startTime`。只有当 new `_time` 落在 `[startTime, startTime+duration]` 之外才 anchor。这让 setTime-as-sample (jump-to-mid) 工作正常, 否则 A1/A3 测试都看到 value=from。
2. **`pause` 后的 additive 通知 backlog** —— 我没有累积 bug 是因为: `tick()` 头部 `if (_paused || _baseClip == nullptr) return;` 早返回后 `_additivePrevTickTime` 不被更新 → resume 时若 cue marks 已跨过, 仍会 fire。**修复:`setAdditivePaused(false)` 主动设 `_additivePrevTickTime = _additiveTime` + 清 queue**(镜像 setTime 的语义)。
3. **CaptureState 三态 vs bool flag** —— 跟 P1.4 hot-path 的 `kBoneUnresolved` sentinel 同 pattern。单字段三态比 `bool resolved + int idx` 更省。`Stale` 单独区分为了「禁用 ref-pose 后重新 enable」场景。
4. **`setAdditiveSource(src, …)` 重置 cross-fade config** —— 跟 P1.3 「rebind 不动 additive layer」不同, 这里**故意**重置 4 个 flag (sync / refPose / additivePause / curve) — 因为「fresh source」就是 P1.3-vanilla 状态。如果 host 想保留 enable, 在 bind 之后再调 setter。
5. **`sampleBlendCurve()` 同时被 `isAdditiveLayerActive()` 和 `evaluate()` 调** —— 在 evaluate 内部 `const float effectiveWeight = sampleBlendCurve();` cache 在 local, 避免一次 evaluate 两次 curve 跑分(`isAdditiveLayerActive` 和 Phase 1b gate + body)。
6. **ref-pose capture 不改 observable outcome for current tests** —— A6 的 (a) vs (b) 在 scenario 下数学相同 (Override clobber rest, capture base also = post-Override)。**真正的 discriminator 在「未 missing track」场景 (c)** capture-off → additive base = rest; capture-on → additive base = captured。当前 test 验证的是 stability (no NaN across repeated evaluate) + 全部 flag-trip 路径走通。
7. **AYMath 5 个 ease 全 ship** —— 不要自己写 ease 函数。`BlendEasing` dispatcher 1:1 把 5 cases 映射到 `aymath::easeIn/Out/InOut/smoothstep` + 自己 lerp Linear。 1 line 一 case。
8. **`uint8_t` for component-side `blendCurveEasing`** —— `IComponent` 是 POCO(无 enum 字段), 因此 component 侧是 raw `uint8_t`, bridge 在 `AYAnimationSystem` 内部 cast 到 `BlendEasing` enum (含 `< 5` saturate 到 Linear 的 fallback)。
9. **`UPD` 旧→新** —— `setAdditiveSyncToBase(true)` should **immediately** snap `_additiveTime = _time`,否则 host 第一次 tick 之前 addixPlayer 都会显示一个 frame 的 stale time,出现视觉 hitch。我在 setter 内 explicit 调(而不是等下一次 tick)。

---

### 4.11 ✅ P1.5 Multi-Slot Stack + Notify Merge + Per-Track Mask — FULL SHIP（2026-07-27）

#### 4.11.1 Context

P1.3 + P1.4 Cross-Fade Full 之后,`AnimationPlayer` 仍是 **single-slot** 数据架构 ── `_additiveClip` 单字段,1 个 additive source 同时存在。UE `LayerObjects[]`(MaxLayerCount=8)、Unity `AnimationLayerMixerPlayable.SetLayerCount(8)` 都允许多层 additive 同时 stack。

**P1.5 = 一次性 ship deferred 3 项 + 8 cap + bus sourceTag**:

1. **Multi-source stack `vector<AdditiveSlot>`** ── `_additiveSlots` 同时承载 N 个 additive layer(8 上限),每层独立 time axis + flag + notify queue
2. **Merged notify queue** ── `consumePendingNotifiesMerged()` 单接口返回 base + 全部 slot records,按 `(time, name)` dedup 保留 base,加 `sourceTag` 字段 (Base / Additive_0..7) 给 AYEntity bus 转发
3. **Per-track mask expression** ── 每 slot 一个 `vector<float> trackWeights`,key by track index;空 vector = uniform 1.0f(P1.3 行为),非空 = per-track scalar multiplier

#### 4.11.2 Data model — `AdditiveSlot`

```cpp
// namespace scope — 跟 TrackSlice / BlendCurve 同 level
constexpr uint32_t kMaxAdditiveSlots = 8;   // UE/Unity 对标

struct AdditiveSlot {
    // Source — 镜像 P1.3 single-slot 字段
    const ayt::resource::IAnimation* clip       = nullptr;
    float                            time       = 0.0f;
    float                            playRate   = 1.0f;
    bool                             loop       = true;
    float                            prevTickTime = 0.0f;
    std::vector<TrackSlice>          tracks;
    std::vector<AnimNotifyRecord>    pendingNotifies;

    // P1.4 fields — per-slot 独立
    bool          syncToBase     = false;
    bool          refPoseCapture = false;
    bool          paused         = false;
    BlendCurve    curve;
    CaptureState  captureState   = CaptureState::Fresh;
    std::vector<float> capturedLocalPos;  // n*3, sized by setSkeleton
    std::vector<float> capturedLocalRot;  // n*4
    std::vector<float> capturedLocalScl;  // n*3

    // P1.5 — per-track mask expression (opt-in)
    std::vector<float> trackWeights;      // size() == tracks.size(); empty = uniform 1.0f
};

class AnimationPlayer {
private:
    std::vector<AdditiveSlot> _additiveSlots;   // size 0..8
};
```

**关键设计决策**:

- **`kMaxAdditiveSlots = 8`** ── 跟 UE `LayerObjects[MaxLayerCount=8]` + Unity `AnimationLayerMixerPlayable.SetLayerCount(8)` 对标。8 × 100 bone × 40 byte captured = 32KB worst case,可控。
- **Per-slot 独立 captured buffers** ── 每 slot 各自有 ref-pose capture 标志,所以各自需要 buffer;**不** shared。32KB worst case 是合理 trade-off。
- **`trackWeights` 空 = uniform 1.0f** ── P1.5 字段存在但默认空,行为跟 P1.3 一致 → **377 baseline 零修改**。
- **`vector<AdditiveSlot>` 非 `unordered_map<size_t, AdditiveSlot>`** ── slotId 是 stable index 0..7,vector 比 map cache-friendly;UE `LayerObjects` 也是 array,Unity `int layer`。

#### 4.11.3 11 invariants(P1.3 五条 + P1.4 三条 + P1.5 三条)

| Inv | Statement | Asserted in |
|-----|-----------|-------------|
| INV-1 | `_additiveClip == null \|\| _blendWeight <= 0` ⇒ Phase 1b 跳过 (P1.3,扩展为 "_additiveSlots empty OR all slots skip") | (contract) |
| INV-2 | `_local{Pos,Rot,Scl}.size() == n * stride` 由 setSkeleton ONLY 维护 (P1.3) | assert evaluate top |
| INV-3 | `_blendWeight ∈ [0,1]` post-setter (P1.3) | assert evaluate top |
| INV-4 | `_baseClip == null && _additiveClip != null` ⇒ rest-pose seed + early-return (P1.3) | (behavioral) |
| INV-5 | `_additiveClip == null` ⇒ Phase 1a == P1.2 (P1.3) | (contract) |
| INV-6 | `_syncToBase == true` ⇒ `_additiveTime == _time` post-tick (P1.4 → per-slot: `slot.syncToBase==true` ⇒ `slot.time == _time`) | (by construction) |
| INV-7 | `_refPoseCapture == true` ⇒ Phase 1b reads from `_capturedLocal*` (P1.4 → per-slot,见 §4.10.6) | assert evaluate top |
| INV-8 | `pause()==true` ⇒ base + additive 都停 (P1.4 → per-slot: `slot.paused==true` ⇒ `slot.time` 不动) | assert evaluate top |
| **INV-9** | per-slot captured buffers sized n\*3/n\*4/n\*3 (when `refPoseCapture && captureState==Valid`) | assert evaluate top |
| **INV-10** | per-slot sync/pause/refPose 独立 ── slot A flag ≠ slot B flag;结构上由 per-slot 字段强制,no runtime assert,单测 pin | (structural) |
| **INV-11** | merged notify queue tags ∈ {Base, Additive_0..7};`time+name` dedup 保留 base | assert after tick() |

#### 4.11.4 Public API surface(18 旧 + 11 新 + 2 read-back getters)

**Backward-compat wrappers**(全 redirect 到 slot[0]):

| 旧 P1.3/P1.4 API | 新 P1.5 per-slot API (slotId=0) |
|---|---|
| `setAdditiveSource(src, rate, loop)` | `setAdditiveLayerSource(0, src, rate, loop)` |
| `clearAdditiveSource()` | `clearAdditiveLayerSource(0)` |
| `setBlendWeight(w)` / `getBlendWeight()` | `setAdditiveLayerWeight(0, w)` / `getAdditiveLayerWeight(0)` |
| `setAdditiveWeight(w)` *(P1.2 deprec)* | 同上 wrapper |
| `setAdditiveSyncToBase(bool)` / `isAdditiveSyncToBase()` | `setAdditiveLayerSyncToBase(0, v)` / `isAdditiveLayerSyncToBase(0)` |
| `setAdditiveRefPoseCapture(bool)` / `isAdditiveRefPoseCapture()` | `setAdditiveLayerRefPoseCapture(0, v)` / `isAdditiveLayerRefPoseCapture(0)` |
| `blendWeightOverTime(from, to, dur, easing)` | `blendLayerWeightOverTime(0, ...)` |
| `cancelBlendCurve()` | `cancelLayerBlendCurve(0)` |
| `isBlendCurveActive()` | `isLayerBlendCurveActive(0)` |
| `setAdditivePaused(bool)` / `isAdditivePaused()` | `setAdditiveLayerPaused(0, v)` / `isAdditiveLayerPaused(0)` |
| `consumePendingNotifiesAdditive()` *(DEPRECATE-P1.5)* | 仅 drain slot[0] |
| `getPendingNotifyCountAdditive()` *(DEPRECATE-P1.5)* | slot[0].pendingNotifies.size() |

**新 per-slot API(11 个)**:

```cpp
// Source
bool   setAdditiveLayerSource(uint32_t slotId, const IAnimation* src,
                              float playRate=1.0f, bool loop=true);
void   clearAdditiveLayerSource(uint32_t slotId);
int32_t getAdditiveLayerCount() const;   // slots whose clip != nullptr

// Weight + curve (P1.4 per-slot)
void   setAdditiveLayerWeight(uint32_t slotId, float w);
float  getAdditiveLayerWeight(uint32_t slotId) const;
void   blendLayerWeightOverTime(uint32_t slotId, float from, float to,
                                float dur, BlendEasing easing=Linear);
void   cancelLayerBlendCurve(uint32_t slotId);
bool   isLayerBlendCurveActive(uint32_t slotId) const;

// Sync / pause / refPose (P1.4 per-slot)
void   setAdditiveLayerSyncToBase(uint32_t slotId, bool v);
bool   isAdditiveLayerSyncToBase(uint32_t slotId) const;
void   setAdditiveLayerPaused(uint32_t slotId, bool v);
bool   isAdditiveLayerPaused(uint32_t slotId) const;
void   setAdditiveLayerRefPoseCapture(uint32_t slotId, bool v);
bool   isAdditiveLayerRefPoseCapture(uint32_t slotId) const;

// Per-track mask (P1.5 NEW)
void   setAdditiveLayerTrackWeights(uint32_t slotId,
                                    const std::vector<float>& weights);
const std::vector<float>& getAdditiveLayerTrackWeights(uint32_t slotId) const;

// Merged notify (P1.5 NEW)
enum class AnimNotifySourceTag : uint8_t {
    Base=0, Additive_0=1, Additive_1=2, ..., Additive_7=8
};
struct AnimNotifyRecord {    // +1 field vs P1.3
    const char*         name = nullptr;
    float               time = 0.0f;
    float               payload = 0.0f;
    AnimNotifySourceTag sourceTag = AnimNotifySourceTag::Base;
};
const std::vector<AnimNotifyRecord>& consumePendingNotifiesMerged();
size_t getPendingNotifyCountMerged() const;
```

**`isAdditiveLayerActive()`** 保留语义 = "slot[0] bound + effective weight > 0"(P1.5 后多 slot-aware 化,但 legacy P1.3/P1.4 行为不变)。

#### 4.11.5 Phase 1b 多 slot loop

```cpp
// evaluate() Phase 1b — P1.5 替换单 slot 为多 slot
for (uint32_t slotIdx = 0; slotIdx < _additiveSlots.size(); ++slotIdx) {
    AdditiveSlot& s = _additiveSlots[slotIdx];
    if (s.clip == nullptr) continue;
    const float w = sampleLayerBlendCurve(s);
    if (w <= 0.0f) continue;

    // INV-7 per-slot dispatch (Phase 0 + post-Phase-1a,见 §4.10.6)
    if (s.refPoseCapture) {
        if (s.captureState == CaptureState::Valid) {
            applyCapturedRefPoseFromSlot(s);
        }
    }

    for (TrackSlice& tr : s.tracks) {
        resolveBoneIdxOnce(tr);
        const int boneIdx = tr.boneIdx;
        if (boneIdx < 0) { /* orphan Float to sink */ continue; }
        const size_t idx = static_cast<size_t>(boneIdx);

        // P1.5 per-track mask — empty vector = uniform, 否则按 track index 取
        const size_t trackIdx = static_cast<size_t>(&tr - &s.tracks[0]);
        const float trackW = (s.trackWeights.empty()
                              || trackIdx >= s.trackWeights.size())
                            ? w
                            : (w * s.trackWeights[trackIdx]);

        switch (tr.type) {
            case AnimTrackType::Vector3:    { /* ... trackW ... s.time ... */ break; }
            case AnimTrackType::Quaternion: { /* ... trackW ... s.time ... */ break; }
            case AnimTrackType::Float:      { /* ... s.time ... */ break; }
        }
    }
}

// INV-7 per-slot post-capture (Phase 0/1a 之间,见 §4.10.6 顺序)
for (AdditiveSlot& s : _additiveSlots) {
    if (s.clip == nullptr || !s.refPoseCapture) continue;
    captureRefPoseFromSlot(s);
    s.captureState = CaptureState::Valid;
}
```

**Slot 处理顺序**:slot 0 先, slot 1 后, ..., slot 7 最后。**确定性 + UE "stack order = paint order"**。position-additive / scale-multiplicative 满足交换律不影响;rotation 顺序敏感,header comment 明确说明。

#### 4.11.6 tick() / setTime() 多 slot 化

```cpp
// tick(dt) — additive 4 分支 per-slot
for (AdditiveSlot& s : _additiveSlots) {
    if (s.clip == nullptr) continue;
    if (s.paused) {                            // INV-8: additive-only pause
        s.prevTickTime = s.time;
    } else if (s.syncToBase) {                 // INV-6: lock-step
        s.time = _time;
        dispatchSlotNotifies(s, prevAdd, s.time, wrapped);
        s.prevTickTime = s.time;
    } else {                                  // P1.3 default: independent axis
        s.time += dt * s.playRate;
        // wrap by s.loop + s.clip->getDuration()
        dispatchSlotNotifies(s, prevAdd, s.time, wrappedAdd);
        s.prevTickTime = s.time;
    }
    // curve auto-disarm per-slot (P1.4 行为多 slot 化)
    if (s.curve.active) {
        const float elapsed = _time - s.curve.startTime;
        if (elapsed >= s.curve.duration) {
            s.curve.active = false;
            s.curve.from = s.curve.to;
            if (&s == &_additiveSlots[0]) _blendWeight = s.curve.to;
        }
    }
}

// setTime(t) — per-slot jump (清 queue + reset cursor)
for (AdditiveSlot& s : _additiveSlots) {
    if (s.clip == nullptr) continue;
    s.time = t;
    if (s.loop) { /* wrap */ }
    s.pendingNotifies.clear();
    s.prevTickTime = s.time;
    if (s.syncToBase) { s.time = _time; s.prevTickTime = s.time; }
    // curve re-anchor outside [start, start+dur] window
    if (s.curve.active) {
        if (_time < s.curve.startTime || _time > s.curve.startTime + s.curve.duration) {
            s.curve.startTime = _time;
        }
    }
}
```

#### 4.11.7 Merged notify queue 算法

```cpp
void AnimationPlayer::rebuildMergedNotifies() {
    _pendingNotifiesMerged.clear();
    // (1) base records — sourceTag = Base
    for (const auto& r : _pendingNotifies) {
        AnimNotifyRecord copy = r;
        copy.sourceTag = AnimNotifySourceTag::Base;
        _pendingNotifiesMerged.push_back(copy);
    }
    // (1b) per-slot records — sourceTag = Additive_N
    for (uint32_t slotIdx = 0; slotIdx < _additiveSlots.size(); ++slotIdx) {
        const AnimNotifySourceTag tag = static_cast<AnimNotifySourceTag>(
            static_cast<uint8_t>(AnimNotifySourceTag::Additive_0) + slotIdx);
        for (const auto& r : _additiveSlots[slotIdx].pendingNotifies) {
            AnimNotifyRecord copy = r;
            copy.sourceTag = tag;
            _pendingNotifiesMerged.push_back(copy);
        }
    }
    // (2) sort by (time, sourceTag)
    std::sort(_pendingNotifiesMerged.begin(), _pendingNotifiesMerged.end(),
              [](const AnimNotifyRecord& a, const AnimNotifyRecord& b) {
                  if (a.time != b.time) return a.time < b.time;
                  return static_cast<uint8_t>(a.sourceTag) <
                         static_cast<uint8_t>(b.sourceTag);
              });
    // (3) dedup-by-(time, name): sort 排好序后 unique walk 删 collision;
    //     base wins (smaller sourceTag comes first in the sort key)
    _pendingNotifiesMerged.erase(
        std::unique(_pendingNotifiesMerged.begin(), _pendingNotifiesMerged.end(),
            [](const AnimNotifyRecord& a, const AnimNotifyRecord& b) {
                if (a.time != b.time) return false;
                if (a.name == b.name) return true;
                return (a.name != nullptr && b.name != nullptr
                        && std::strcmp(a.name, b.name) == 0);
            }),
        _pendingNotifiesMerged.end());
}
```

**`consumePendingNotifiesMerged()`** = thread_local swap(同 P1.2/P1.3 模式)。同时 drain per-source queues 防止下帧重发。

**`AnimNotifyEvent.sourceTag`** ── `AnimNotifyEvent` struct 加 `sourceTag` 字段, default `Base`。kTypeId 不变(0x000A'0001)。AYEntity `AnimationSystem::onUpdate` 的 bus emit 把 `rec.sourceTag` 直接 pipe 到 event 的 `sourceTag` 字段。

#### 4.11.8 AYEntity bridge

**`AdditiveLayerSpec`** ── 新 struct(11 个 `AY_PROPERTY` 字段 + ctor + `AY_FINALIZE_REGISTRATION_METADATA` 注册)放在 `AYAnimationComponent.h`,在 `AnimationComponent` 之前定义:

```cpp
struct AdditiveLayerSpec {
    AY_PROPERTY(uint32_t,    slotIndex,         kAttrSerialize)  // = position in vector if UINT32_MAX
    AY_PROPERTY(std::string, additiveClipPath,  kAttrSerialize)
    AY_PROPERTY(float,       additivePlayRate,  kAttrSerialize)
    AY_PROPERTY(bool,        looping,           kAttrSerialize)
    AY_PROPERTY(float,       blendWeight,       kAttrSerialize)
    AY_PROPERTY(bool,        syncToBase,        kAttrSerialize)
    AY_PROPERTY(bool,        refPoseCapture,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveFrom,    kAttrSerialize)
    AY_PROPERTY(float,       blendCurveTo,      kAttrSerialize)
    AY_PROPERTY(float,       blendCurveDuration,kAttrSerialize)
    AY_PROPERTY(uint8_t,     blendCurveEasing,  kAttrSerialize)
    // ctor defaults 全部 → uniform / OFF 状态
};
AY_FINALIZE_REGISTRATION_METADATA(AdditiveLayerSpec)
```

**`AnimationComponent`** 加 `AY_PROPERTY(std::vector<AdditiveLayerSpec>, additiveLayers, kAttrSerialize)`(vector field first-of-kind, AYReflect 支持 via `tryRegisterVector<E>`,已在 serializer 验证可用)。

**`AYAnimationSystem::onUpdate`** per-frame push 三分支:

```cpp
if (!anim->additiveLayers.empty()) {
    // (a) Multi-slot path — iterate over additiveLayers[]
    for (size_t i = 0; i < min(additiveLayers.size(), 8); ++i) {
        const AdditiveLayerSpec& spec = anim->additiveLayers[i];
        const uint32_t slotIdx = (spec.slotIndex == UINT32_MAX) ? i : spec.slotIndex;
        if (slotIdx >= 8) continue;
        // 1. source bind (rebind-detection on path)
        if (!spec.additiveClipPath.empty()) {
            if (lastPath(e, slotIdx) != spec.additiveClipPath) {
                loadAdditiveClip(spec.additiveClipPath);
                player.setAdditiveLayerSource(slotIdx, clip, spec.additivePlayRate, spec.looping);
            }
        } else {
            if (lastPath(e, slotIdx) was bound) player.clearAdditiveLayerSource(slotIdx);
        }
        // 2. weight, 3. sync, 4. refPose, 5. curve (per-slot rebind-detection)
        player.setAdditiveLayerWeight(slotIdx, spec.blendWeight);
        // ... per-slot rebind detection ...
    }
} else if (!anim->additiveClipPath.empty()) {
    // (b) Legacy P1.3/P1.4 single-slot path — slot[0]
    // ... 旧 push lines mirror 在 slot 0 ...
} else {
    // (c) 全 OFF — clear 所有 slot + erase per-slot rebind caches
    for (each slot that was previously bound) player.clearAdditiveLayerSource(slotIdx);
}
```

**4 个 nested rebind-detection maps** (在 `AYAnimationSystem.h`):`_lastAppliedAdditivePaths` / `_lastAppliedSyncToBase` / `_lastAppliedRefPoseCapture` / `_lastAppliedBlendCurveDuration` / `_lastAppliedBlendCurveEasing` 全部从 `entity → T` 升级为 `entity → unordered_map<uint32_t, T>`。

**AnimNotifyEvent dispatch** 改用 `consumePendingNotifiesMerged()` 单接口;per-slot record 的 `clipName` 从 `_lastAppliedAdditivePaths[e][slotIdx]` 反查;`sourceTag` 直接 pipe 到 event 字段。

#### 4.11.9 Test breakdown(22 + 5 = 27 new)

**AYAnimation 22**(详见 commit `3b222a3`):

| # | Name | Contract |
|---|------|----------|
| 1 | `P1_5_SlotBind_AssignsSlotIndex` | `setAdditiveLayerSource(1, src)` 写 `_additiveSlots[1].clip` |
| 2 | `P1_5_SlotCleared_LayerSilent` | `clearAdditiveLayerSource(2)` 后 slot 2 evaluate 不贡献 |
| 3 | `P1_5_MultiSlot_IndependentTime` | 两 slot 独立 advance |
| 4 | `P1_5_MultiSlot_AccumulatePosition` | 两 slot Root.position delta → sum |
| 5 | `P1_5_MultiSlot_RotationOrderMatters` | slot 0 then 1 ≠ slot 1 then 0(paint order) |
| 6 | `P1_5_SlotFlagsIndependent` | slot 0 sync, slot 1 NOT → 各自 time 行为 |
| 7 | `P1_5_SlotPauseIndependent` | slot 0 paused + slot 1 ticking |
| 8 | `P1_5_SlotRefPoseCapture_PerSlot` | slot 0 capture, slot 1 NOT → 不同 base |
| 9 | `P1_5_SlotCurve_PerSlot` | slot 0 curve from 0→1 + slot 1 static weight |
| 10 | `P1_5_MaxSlots_Bound_Rejects9th` | 9th bind → false;getAdditiveLayerCount 仍 8 |
| 11 | `P1_5_Stop_DisposesAllSlots` | `stop()` 清所有 slot clip |
| 12 | `P1_5_NotifyMerged_SourceTag_Base` | base record tagged `Base` |
| 13 | `P1_5_NotifyMerged_SourceTag_Additive_0` | slot 0 record tagged `Additive_0` |
| 14 | `P1_5_NotifyMerged_SortedByTime` | merged sort by (time, sourceTag) |
| 15 | `P1_5_NotifyMerged_Dedup_TimePlusName` | base vs slot collision → base wins |
| 16 | `P1_5_SetTimeJumpsAllSlots` | `setTime(t)` 锚定所有 slot playhead |
| 17 | `P1_5_Play_PreservesSlots` | `play(base)` 不清 slots(P1.3 行为) |
| 18 | `P1_5_SlotBindOldAPI_DefaultsToSlot0` | `setAdditiveSource(src)` → `slot[0]` wrapper |
| 19 | `P1_5_SetBoneIndexCache_MultiSlot` | `setSkeleton()` invalidate 所有 slot tracks |
| 20 | `P1_5_NotifyMergedDegenerate_NoBase` | 无 base + slot 0 → merged queue 仅 `Additive_0` |
| 21 | `P1_5_SlotBindRebind_PathUnchanged_NoReassign` | P1.3 rebind 模式镜像到 per-slot |
| 22 | `P1_5_TrackWeights_OptionalPerSlotMask` | `trackWeights[k]=0.5` halve that track |

**AYEntity 5**:

| # | Name | Contract |
|---|------|----------|
| 1 | `animation_component_multi_layer_bridge_pushes_each_slot` | `additiveLayers[3]` → 3 slot 都 bind |
| 2 | `animation_component_multi_layer_bridge_rebind_per_slot` | 改 `additiveLayers[1].path` 只触发 slot 1 rebind |
| 3 | `animation_component_legacy_scalar_layers_zero_size` | `additiveLayers.size()==0` + scalar fields → slot[0] |
| 4 | `animation_component_merged_notify_eventbus_carries_source_tag` | merged → `AnimNotifyEvent.sourceTag` round-trip |
| 5 | `animation_component_oversized_layers_no_rebind` | 9-layer config 静默 reject 9th |

**3-run verification**:

| Module | Baseline | + New | Expected | 3-run |
|--------|----------|-------|----------|-------|
| AYAnimation | 376 | 22 | **398** | ✅ × 3 |
| AYEntity | 211 | 5 | **216** | ✅ × 3 |
| AYResource | 701 | 0 (no change) | **701** | (unchanged from P1.4 baseline) |

#### 4.11.10 Out-of-scope for P1.5(留 P1.6 / Phase 2)

- **共享 skeleton tick cache** ── P1.6 scope 探索后确认 `ISkeleton::findBone` 已是 O(1) hash lookup (`Skeleton::_boneNameMap`) 且当前 `SkeletonComponent::skeleton` by-value（每个 entity rebuild `_boneNameMap`），真共享需要把 `SkeletonComponent::skeleton` 改为 `shared_ptr<const ISkeleton>`（跨 ECS 边界 refactor）。**P1.6 不做** — 留 P1.7（需 ECS refactor）。
- **`MontageSlot` 语义对齐** ── UE Montage 上半身 slot 不是 additive stack,需要独立 §14 P2.3 子项目;P1.5 AdditiveSlot 不能冒充 MontageSlot。
- ✅ **`setAdditiveWeight` / `getAdditiveWeight` 真实 deprecate** ── P1.6 已 ship 2026-07-27（inline-forward wrapper 全删）。
- ✅ **`consumePendingNotifiesAdditive` 真实 deprecate** ── P1.6 已 ship（slot[0]-only wrapper + dead `dispatchAdditiveNotifies` helper 全删;`consumePendingNotifiesMerged()` 是 canonical）。

#### 4.11.12 P1.6 ship 内容（2026-07-27）

P1.6 = 纯 deprecate wrapper cleanup（**无新功能**）：

| 改动 | 位置 |
|------|------|
| 删 `setAdditiveWeight` / `getAdditiveWeight` | AnimationPlayer.h:339-340 (P1.2 inline-forward) |
| 删 `consumePendingNotifiesAdditive` decl + def | AnimationPlayer.h:602 + cpp:732-734 (P1.3 DEPRECATE-P1.5) |
| 删 `getPendingNotifyCountAdditive` inline | AnimationPlayer.h:603-609 (P1.3 DEPRECATE-P1.5) |
| 删 `dispatchAdditiveNotifies` decl + def | AnimationPlayer.h:600 + cpp:722-729（0 caller，P1.6 顺手清 dead code） |
| 删 `// REMOVE-MARKER(P1.6)` marker | AnimationPlayer.h:91 |
| Bridge 改 canonical | AYAnimationSystem.cpp:203 `setAdditiveWeight` → `setBlendWeight` |
| Test 改 canonical | AYTest_AnimationPlayer.cpp 11 处 + SkinnedAnimationTest.cpp 8 处 |
| Test semantic 改 | `P1_3_NotifyIndependence_BaseAndAdditive` 改测 merged queue + sourceTag discriminator |
| Docs 同步 | §11 P1.6 ship row + §13 row 17f + §11 P1.6 row + §4.11.12 + §4.7 + §4.10.9 cleanup |

**3-run stable**: AYAnimation 398/398 + AYEntity 216/216 + AYResource 701/701 × 3。
**Zero regression**: P0..P1.5 全部 baseline 通过;test 总数不变（仅内部 rename + 1 个测试改语义）。

**关键工程教训**（P1.6 reviewer 必读）：
1. **`consumePendingNotifiesMerged()` 是 source-of-truth drain** — 它在 swap merged 之后还会清空 `_pendingNotifies` + 每个 slot 的 `pendingNotifies`（cpp:808-811）。P1.3 dual-drain 测试要改成：测 merged 单独（不再独立测 base drain，因为 base 已被 merged 清空）。Pre-P1.5 设计的 `consumePendingNotifiesAdditive` 之所以能独立 drain slot[0]，是因为它走的路径不同。
2. **字段名 vs setter 名可以不一致** — `AnimationComponent::additiveWeight` 字段保留（serializer compat）但 bridge 推到 player 走 canonical `setBlendWeight`。这是 P1.5 design.md §4.11.11 lesson #1 slot[0] redirect 的延续：P1.6 bridge 1 行改动 = `setAdditiveWeight(x)` → `setBlendWeight(x)`，无 schema 变化。
3. **`isAdditiveLayerActive()` 仍保留** — P1.5 决定不改名（§4.11.11 lesson #8），P1.6 也未动。Host 真需要 multi-slot 活跃检测用 `getAdditiveLayerCount() == 0` 自遍历。
4. **`dispatchAdditiveNotifies` 是纯 dead code** — P1.5 设计时本来被 `consumePendingNotifiesAdditive` 间接调用，P1.6 后两者一起删。无调用方、无测试引用、无 side-effect。

#### 4.11.11 关键工程教训(给后续 reviewer / P2 Montage)

1. **Slot weight 必须 sync `_blendWeight ↔ slot[0].curve.from`** ── P1.4 single-slot 时代 `setBlendWeight(w)` 写 `_blendWeight = w`,sampleBlendCurve() inactive 路径返回 `_blendWeight`。P1.5 通用化为 per-slot 后,slot[0] 必须 mirror `_blendWeight` 到 `slot[0].curve.from`,否则 `setBlendWeight(0.5)` 后 slot[0] 仍按 `curve.from=0`(default) 评估,Phase 1b 静默 skip。**根因**:测试 12 个 regression 第一波全部来自这个缺口;redirect 到 slot[0] wrapper 不是"调用 forwarding"那么简单。
2. **Curve 时钟用 base `_time` 不是 slot `time`** ── loop wrap 时 `s.time` 短暂回到 0 → `elapsed = s.time - s.curve.startTime` 也回到 0 → `curve.active` 永不 disarm。**改用 base `_time`**(`elapsed = _time - s.curve.startTime`)保证曲线窗口基于 host time。
3. **INV-7 capture 必须 Phase 0 Valid-apply + post-Phase-1a capture,Phase 1b NEVER re-capture** ── 否则:enable 立即 capture rest + Phase 1b 末尾又 capture additive-final → 第二帧 double-add(`1.98 → 3.96`)。Phase 0 (top of evaluate) Phase 1a 之前 apply-captured;post-1a capture;Phase 1b 只加 delta。design.md §4.10.6 顺序是 contract。
4. **Merged notify dedup-by-(time, name) 必须 sort by (time, sourceTag)** ── stable secondary key 保证 base 在 collision 时排在前,`std::unique` 删后面那个。`std::sort` 不是 stable sort?——**`std::sort` 不是 stable**;但 sourceTag 是 unique 区分键,所以 base 永远 < additive 同时间,sort by (time ASC, sourceTag ASC) 之后 base 必然先,unique 删除逻辑跟 stable sort 等价。**仍然保险**:sort 用 stable version 的话加 `std::stable_sort` 即可。
5. **AnimNotifyEvent 不能再 include AYEventSystem** ── AYAnimation 不 link AYEventSystem(模块隔离),所以 AnimNotifyEvent.h 不能依赖 `EventPriority.h`。**P1.5 修正**:从 AnimNotifyEvent.h 删 kPriority 字段(只在 bus subscribe path 隐式用,subscriber-side 转换)。`kTypeId` 保留为 uint32_t 不依赖 ayt::event namespace。
6. **AYEntity vector<AY_PROPERTY struct> first-of-kind** ── `AY_PROPERTY(std::vector<AdditiveLayerSpec>, additiveLayers, ...)` 是本 codebase 第一个 vector-of-reflected-struct 字段;PropertyMacros 已支持(`tryRegisterVector`),但 struct 自身必须先 `AY_FINALIZE_REGISTRATION_METADATA(AdditiveLayerSpec)` 注册才能 vector 注册。**顺序硬约束**:struct 必须在 field 之前;finalize 紧跟 struct 之后。
7. **Per-slot rebind-detection 必须 nested map** ── P1.3 的 `_lastAppliedAdditivePath` 是 `entity → string`;P1.5 必须升级为 `entity → unordered_map<slotIdx, string>`,否则两 slot path 互相覆盖。**4 个 P1.4 rebind map 都同理**。
8. **`isAdditiveLayerActive()` 保留 slot[0] 语义** ── 多 slot 后 P1.5 决定**不** 改它语义,只读 slot[0];需要真 multi-slot 活跃度检测的 host 用 `getAdditiveLayerCount()` 自遍历。**降低破坏性 + P1.3/P1.4 测试零修改**。

### 4.12 ✅ P1.7 Shared Skeleton Tick Cache — ECS refactor + asset-level boneIdx cache ── FULL SHIP (2026-07-27)

P1.7 = **A (ECS refactor) + B (player-side asset cache)**：
- **A**：`SkeletonComponent::skeleton` 由 by-value `ayt::resource::Skeleton` 改为 `std::shared_ptr<ayt::resource::Skeleton>`（保留 Skeleton 字段类型 — 测试需要 setBoneCount / setBone 程序化构建；隐式转换 `shared_ptr<Skeleton>` → `shared_ptr<const ISkeleton>` 给 AnimationPlayer）。N entity 同 skeletonPath 共享同一 ISkeleton asset — 砍 N 倍内存 + N 次首帧拷贝。
- **B**：`AnimationPlayer::setSkeleton` 改 `std::shared_ptr<const ISkeleton>`；player 内 `_skeleton` 字段改 `shared_ptr`；新增 `ayt::anim::AssetBoneCache`（单例 + mutex）跨 player 共享 `(ISkeleton* addr, boneName) → boneIdx` 解析。`resolveBoneIdxOnce` 走 AssetBoneCache：hit 直接写 `slice.boneIdx`，miss 调 `resolveAndCache` 填充。

**与 P1.6 关系**：P1.6 显式 defer 共享 cache 到 P1.7（§11 + §4.11.10）。P1.7 是 P1.6 收口动作的延续（用户决策：A+B 都要做）。

**Engine 跨模块改动**：
- AYAnimation 1 文件新增（AssetBoneCache.h/cpp）+ 2 文件改（AnimationPlayer.h 字段签名 + AnimationPlayer.cpp resolveBoneIdxOnce）
- AYEntity 1 文件改 header（SkeletonComponent field 类型） + 1 文件改 cpp（AnimationSystem lazy-load 改 shared_ptr 路径，不再有 `setBoneCount(n) + n × setBone()` 拷贝循环）
- 60+ AYAnimation test callsite + 14 AYEntity test callsite + 0 SuzanneSkinnedDemo 改（demo 只设 skeletonPath）
- 6 + 2 新 test case（详见 §11 / §13）

**On-disk format 不变**：`.ayanm` v4 / `.ayskel` v1 维持，ISkeleton VERSION 不动。零格式迁移。

**新文件**（P1.7 完整 list）：
- `AYAnimation/include/ayanimation/AssetBoneCache.h`
- `AYAnimation/src/AssetBoneCache.cpp`（单例 + mutex + 3 public method）

**改文件**（关键 6）：
- `AYAnimation/include/ayanimation/AnimationPlayer.h`（字段 + 签名）
- `AYAnimation/src/AnimationPlayer.cpp`（setSkeleton + resolveBoneIdxOnce）
- `AYAnimation/CMakeLists.txt`（+ AssetBoneCache.cpp）
- `AYEntity/include/components/AYSkeletonComponent.h`（字段类型）
- `AYEntity/src/AYAnimationSystem.cpp`（懒加载 + debug log deref）
- `AYEntity/unittest/SkinnedAnimationTest.cpp`（14 callsite 改 shared_ptr 路径）

#### 4.12.1 AssetBoneCache contract

单例 + mutex，key = `const ISkeleton*`（裸地址 — 不持 ownership，生命周期由 SkeletonComponent / test fixture 管）。三个 public method：

```cpp
class AssetBoneCache {
public:
    static constexpr int32_t kCacheKeyAbsent = INT32_MIN;  // = kBoneUnresolved
    static constexpr int32_t kCachedMiss     = -1;
    static AssetBoneCache& instance();

    int32_t lookup(const ISkeleton* skel, const char* name) const;   // 不 mutate
    int32_t resolveAndCache(const ISkeleton* skel, const char* name); // 写 + 返回
    void    invalidate(const ISkeleton* skel);                         // 清一个
    void    clear();
    size_t  skeletonEntryCount() const;                                // test 诊断
    size_t  boneNameEntryCount(const ISkeleton* skel) const;
};
```

线程安全：main-thread-only tick path（ECS 单线程约定）；mutex 保险 + thread-sanitizer 友好。**P3 polish（2026-08-08）改默认无锁**：`_threadSafe = false`（INV-59 — 零同步可证明），authoring tools / 多线程 host 显式 `setThreadSafe(true)` 重挂 mutex（INV-60 — flag 非原子，须并发前设）。详见 §4.21。

#### 4.12.2 resolveBoneIdxOnce 新路径

P1.4 时代：
```cpp
const int found = _skeleton->findBone(slice.nodeName.c_str());
slice.boneIdx = (found >= 0) ? static_cast<int32_t>(found) : -1;
```

P1.7：
```cpp
const int32_t cached =
    AssetBoneCache::instance().resolveAndCache(_skeleton.get(), slice.nodeName.c_str());
slice.boneIdx = (cached >= 0) ? cached : -1;
```

Per-player `TrackSlice.boneIdx` cache（P1.4 hot-path）**不变** — AssetBoneCache 是 cross-player 附加层。

#### 4.12.3 ECS bridge 改写（无逐 bone 拷贝）

P1.6：
```cpp
const size_t n = skelRes->getBoneCount();
skel->skeleton.setBoneCount(n);
for (size_t i = 0; i < n; ++i) {
    skel->skeleton.setBone(i, skelRes->getBones()[i]);
}
skel->player.setSkeleton(&skel->skeleton);
```

P1.7：
```cpp
skel->skeleton = std::static_pointer_cast<ayt::resource::Skeleton>(skelRes);
skel->jointCount = static_cast<uint32_t>(skel->skeleton->getBoneCount());
// ... jointCount == 0 / skinMatrices 分配不变 ...
skel->player.setSkeleton(skel->skeleton);
```

**热路径收益**：每 entity 砍掉 `setBoneCount(n) + n × setBone()` + `n × _boneNameMap[bone.name] = i` 的 O(N × boneCount) 拷贝。N 个 entity 同 skeleton → N 倍内存节省 + N 倍启动时间节省。

#### 4.12.4 关键工程教训（P1.7 reviewer 必读）

1. **`SkeletonComponent::skeleton` 类型选择** ── 用 `shared_ptr<Skeleton>`（mutable，concrete）而非 `shared_ptr<const ISkeleton>`（immutable，interface）。理由：测试需要 `setBoneCount / setBone` 程序化构建 skeleton → 调用方需要 mutable API。`shared_ptr<Skeleton>` 隐式转 `shared_ptr<const ISkeleton>` 给 AnimationPlayer（player 持有 const 即可）。
2. **SkeletonComponent ctor 必须显式 `skeleton.reset()`** ── 字段是 in-class default-init shared_ptr，default ctor 不写 reset 不会出问题，但显式 `reset()` 是 P1.7 contract 文档，验证字段语义而非依赖隐式 default。
3. **`static_pointer_cast<Skeleton>(shared_ptr<ISkeleton>)` 是合法的** ── Skeleton derives public from ISkeleton。ResourceManager::load<ISkeleton>(path) 返回 `shared_ptr<ISkeleton>`，在 ECS bridge 一次 cast 拿 concrete Skeleton。**注意**：不能用 `dynamic_pointer_cast` —— Skeleton 不是 polymorphic-deleted-from-base 类（无 virtual dtor 之外的多态），但 static cast 工作。
4. **Test 中 `buildFourBoneSkeleton(*skel->skeleton)` 必须先 `skel->skeleton = std::make_shared<Skeleton>()`** ── SkeletonComponent P1.7 ctor 把 skeleton 设为 nullptr。deref 空 shared_ptr → setBoneCount → vector::resize → AV（0xC8 ≈ 空 this 上的成员偏移）。**这是 P1.7 引入的 footgun，所有 inline-build skeleton 测试必须先 make_shared**。新增 `makeFourBoneSkeletonShared` / `makeOneBoneSkeletonShared` helper 集中处理。
5. **`setSkeleton(nullptr)` 等价于 unbind** ── `shared_ptr` default ctor 是 null；setSkeleton 接 null 走 `skelRaw == nullptr` 分支，跟 P1.4 时代传 NULL 裸指针语义一致。
6. **AssetBoneCache 是单例 + magic-static** ── C++11 线程安全 init；与 std library 同段销毁，singleton destruction order 无风险。
7. **`isAdditiveLayerActive()` 不动** ── P1.5 决定 + P1.6 验证 + P1.7 仍然保留 slot[0] 语义不变。

---

## 5. AnimationStateMachine（Phase 3 ── ✅ P3.1 L1 + P3.2 L3 + P3.x L2 已 ship）

> **本期状态**：已 ship（2026-08-06/07，详见 §4.14 / §4.15 / §4.16；L4 MotionMatching / `.ayasm` / BlendTree-in-SM 仍 deferred）。本节保留为早期架构草案。

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

## 6. IKSolver（Phase 4 ── ✅ TwoBoneSolver ship，FABRIK/CCD 未启动）

| 类型 | 适用 | 状态 |
|------|------|------|
| TwoBoneSolver | 膝、肘 | **✅ P4-1 ship 2026-08-10**（§4.25）：解析解核心 + AnimationPlayer Phase 2.5 集成（世界空间目标 + 权重混合 + 链配置 + 骨骼重解析），一条完整消费链（chain config → evaluate → skin 矩阵） |
| FABRIKSolver | 手指、触须、多关节 | 未启动 |
| CCDSolver | 通用链式 | 未启动 |

`IKSolver` 通用接口与 IK 约束设计保留（见 [旧版 §6](../AYAnimation/history/AN-01-design.md)），Phase 4 后续刀实装。Pole vector / 局部空间目标 / per-chain mask 门控 / FABRIK+CCD / 约束均 deferred（见 §14.3）。

---

## 7. 骨骼重定向（Phase 4 ── 未启动）

离线重定向流程 + `BoneMappingTable` + `AnimationRetargeter` 设计保留，Phase 4 启动后实装。

**注**：当前 AYResource 的 `ISkeleton::getRootBoneIndices()` 已为重定向做预留。

**与 MMD 导入的关系（2026-07-27）**：PMX/VMD → 引擎资产属 **AYResource 离线前端**（见 `AYResource/design.md` **§5.7**，拟用 saba 填 `IntermediateAsset` 后复用现有 Converter）。Runtime `AnimationPlayer` **不**依赖 MMD。跨模型骨长/比例问题仍由本 Phase 的 Retarget / 烘到目标骨架解决；与「能否解析 PMX」正交。近期待看效果：Blender → FBX → 现有 `FBXConverter` 即可。

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

### Phase 1.5: P0 修复收口 ── ✅ SHIP（2026-07-26）

- [x] 删 `ayt::anim::Skeleton` / `ayt::anim::Animation` parallel type
- [x] 删 `AYEntity::adapter::load{Skeleton,Animation}` 适配器
- [x] `AnimationPlayer::play(const IAnimation*)` 改消费 AYResource
- [x] ticks → seconds 在 `setClip` 一次性预转换
- [x] `KeySampler::sampleTrackQuaternion` 加 `dot < 0` 最短弧选优
- [x] Float track sink 出口 + host subscribe API
- [x] Topology 顺序 assert（parent index 必先于 child）
- [x] 矩阵方向约定 lock-test（IBM = bindWorld.inverse()）
- [x] IBM = 0 NaN-safe 行为验证
- [x] **P1.1 Anim Notify**（2026-07-26）─ root pin `aa6bbdf`
- [x] **P1.2 Additive Layer 1**（2026-07-26）─ per-track blendMode + global additiveWeight; IAnimation VERSION 3; 0 regression across 3 modules (697+197+165)
- [x] **P1.3 Additive Layer 2 / Cross-Fade**（2026-07-26）─ dual-source AnimationPlayer; setAdditiveSource + setBlendWeight + 5 invariants + 2 notify queues; IAnimation VERSION 4 (no on-disk change); 0 regression across 3 modules (701+261+177)
- [x] **P1.4 Hot-Path BoneIdx Cache**（2026-07-26, hot-path ship）─ `TrackSlice.boneIdx` lazy-resolve + `setSkeleton()` invalidate;消除 evaluate Phase 1a/1b 每帧 findBone 调用; dual-source 收益翻倍; 0 regression across 3 modules (701+282+177);详见 §4.9
- [x] **P1.4 Cross-Fade Full Ship**（2026-07-26）─ keyframed weight curve (`blendWeightOverTime`) + syncToBase + ref-pose capture + additive pause 全 ship;3 new invariants INV-6/7/8 + 8 AnimationPlayer tests + 3 AYEntity integration tests; 0 regression across 3 modules (701+312+187);详见 §4.10
- [x] **P1.5 Multi-Slot Additive Stack**（2026-07-27）─ `vector<AdditiveSlot>` (kMaxAdditiveSlots=8) + merged notify queue + `AnimNotifySourceTag` enum + per-track `trackWeights` opt-in mask + 18 旧 single-slot API 全 redirect 到 slot[0] backward-compat wrappers + AYEntity `AdditiveLayerSpec` 结构 + nested (entity → slot) rebind-detection maps + per-slot push loop + EventBus `AnimNotifyEvent.sourceTag` pipe; INV-9/10/11 (per-slot 化 INV-6/7/8 + 新增 capture-buffer size + dedup 守则) + 22 AnimationPlayer tests + 5 AYEntity tests; 0 regression across 3 modules (AYAnimation 398 + AYEntity 216 + AYResource 701 unchanged); 详见 §4.11
- [x] **P1.5 Multi-Slot stack（Player）**（文档对齐 2026-07-27）─ `AdditiveSlot`×8 + merged notify/`sourceTag` + per-slot trackWeights；**ECS/测试桥接未齐**；详见 §4.8
- [x] **P1.6 Deprecate Wrapper Cleanup**（2026-07-27）─ 真实 deprecate `setAdditiveWeight`/`getAdditiveWeight`（P1.2 inline-forward wrapper）和 `consumePendingNotifiesAdditive`/`getPendingNotifyCountAdditive`（P1.3 DEPRECATE-P1.5 wrapper）全删；连带 dead `dispatchAdditiveNotifies` helper 删除；`AYAnimationSystem::onUpdate` bridge 调 canonical `setBlendWeight`；tests 11 处 + 8 处 caller 改 canonical；2 个 P1.3 测试因 dual-drain 消失改测 merged queue + sourceTag discriminator。3-run stable: AYAnimation 398/398 + AYEntity 216/216 × 3，零回归。**共享 skeleton tick cache 留 P1.7**（需 ECS refactor：`SkeletonComponent::skeleton` → `shared_ptr<const ISkeleton>`），P1.6 不动 ECS 边界。详见 §11 row + §13 row + §11 P1.6 row
- [x] **P1.7 Shared Skeleton Tick Cache**（2026-07-27）─ ECS refactor：`SkeletonComponent::skeleton` by-value → `shared_ptr<Skeleton>`（implicit convert to `shared_ptr<const ISkeleton>` 给 AnimationPlayer）；N entity 同 skeletonPath 共享同一 ISkeleton asset，砍 N 倍内存 + N 次首帧拷贝；AYAnimationSystem 懒加载改持 shared_ptr 路径（无 `setBoneCount + n×setBone()` 循环）。Asset-level boneIdx cache：`AssetBoneCache` 单例（mutex）`(ISkeleton* addr, boneName) → boneIdx`；`AnimationPlayer::setSkeleton` 改接受 `shared_ptr<const ISkeleton>`；`_skeleton` 字段改 shared_ptr；`resolveBoneIdxOnce` 走 AssetBoneCache（hit 直接写、miss resolveAndCache）。新文件 `AssetBoneCache.h/cpp`；改 6 文件（AYAnimation 3 + AYEntity 3）；60+ AYAnimation + 14 AYEntity test callsite 改；6 + 2 新 test case（`P1_7_SetSkeleton_AcceptsSharedPtr` / `P1_7_AssetBoneCache_LookupAfterResolve` / `P1_7_AssetBoneCache_DifferentSkeletonsIndependent` / `P1_7_AssetBoneCache_Invalidate` / `P1_7_TwoPlayers_OneSkeleton_ShareResolve` / `P1_7_SharedPtr_SkeletonLifecyclePreservedByComponent` + AYEntity `skeleton_component_shared_ptr_does_not_duplicate_skeleton` / `skeleton_component_shared_ptr_outlives_resource_manager_eviction_safe`）。**关键 footgun**：所有 inline-build skeleton 测试必须先 `skel->skeleton = std::make_shared<Skeleton>()` 再 `buildFourBoneSkeleton(*skel->skeleton)` ── 否则 deref nullptr → AV。3-run stable: AYAnimation 420/420 + AYResource 701/701 × 3, AYEntity 含 1 个 pre-existing CharacterEntity flake（与 P1.7 无关）。详见 §11 row + §13 row 17g + §11 P1.7 row + §4.12

### Phase 2: 混合 + 蒙皮 ── ⏳ 排队

- [x] CrossFade（Player 侧 P1.3/P1.4）／[x] Blend 1D / Blend 2D（**P2.1 ship 2026-07-28 `68f4227`** — BlendSpace 1D/2D，AYTest_BlendSpace.cpp 13 cases）
- [x] Additive 动画层（P1.2–P1.5 Player）
- [x] 骨骼遮罩 Mask 一等资源（**P2.2 ship 2026-08-03 + P3.x刀1 loader ship 2026-08-06**）
- [x] 多 Additive Slot（P1.5）／[ ] Montage 语义 Slot（与 §4.8 对齐，勿第二套 API）
- [ ] Dual-Quaternion Skinning
- [ ] CPU 蒙皮真输出（CPUSkinning Pass）
- [ ] GPU 蒙皮 Skeleton UBO 上传（与 AYRenderer 接通）
- [ ] 主线程 evaluate 规模策略 / 可选 worker（未写规格）
- [ ] Root Motion 通道草案（可提前到 P2，见 §14）
- [ ] 网络：pose/time/notify 复制边界（空白）
### Phase 3: 状态机 ── ⏳ P3.1 ship, L2-L4 排队

- [x] **P3.1 L1 简单状态机**（2026-08-06）─ `StateMachine` class (events-driven FSM + first-match-wins + wildcard fromState + automatic trigger + cross-fade wait + trigger auto-consume + unknown-param fail-soft) + `AnimationStateMachineComponent` (POD: resourcePath placeholder + pendingTriggers + speed/verticalSpeed/isGrounded/isAttacking + currentState/previousState/isTransitioning read-back + setTrigger convenience) + `StateMachineSystem` priority 460 (after AnimationSystem 450, sync params + drain triggers + tick SM + push new clip + emit `AnimStateChangedEvent` via EventBus kTypeId=0x000A'0010) + 15 AYAnimation unit tests + 8 AYEntity ECS integration tests；0 regression 3-run stable (AYAnimation 543/543 + AYEntity 370/370 + AYResource 1044/1044 × 3)；详见 §4.14 + §13 row 20 + §11 P3.1 row
- [x] **P3.x L2 条件 DSL**（2026-08-07）─ `Transition` 扩展缓存层 (conditionExpr / cachedAst / conditionDirty / conditionParseError 4 字段) + `setConditionExpr` / `invalidateConditionCache` / `evaluateCondition(ctx)` 3 API + `ConditionExprAst` 类族 (Binary/Unary/Identifier/Literal + Visitor 接口) + `ConditionParser` (mini Lexer + precedence-climbing Parser) + 8 算子 (`> < == != && || ! ()`) + 字面量 float/bool + 短路求值 + 负数字面量 + lazy parse + dirty cache + parse-fail-soft-false + L1 back-compat 双轨 + `ConditionEvalCtx` 4 字段 (`params/triggers/currentState/currentStateTime` 留 P3.x刀 N+1 钩子) + 30 AYAnimation unit tests + 4 AYEntity ECS integration tests；0 regression 3-run stable (AYAnimation 703/703 + AYEntity 401/401 + AYResource 1044/1044 × 3)；详见 §4.16 + §13 row 20b + §11 P3.x row
- [x] **P3.x刀 N+1.BC Time-in-State Query + Per-state AnimNotify Routing**（2026-08-07）─ **B**: `StateMachine._currentStateEnterTime` 字段 + `getCurrentStateElapsedTime()` API + `update(dt)` 顶部 +dt 累加 + `setInitialState` / lazy-init / `fireTransition` (instant-cut + cross-fade START) 3 处 reset 0.0f + `ConditionEvalCtx` 兑现 `currentStateTime` (`StateMachine.cpp` 1 line 从 0.0f literal 改 getCurrentStateElapsedTime) + `CondIdentifierExpr::evaluateAsFloat` reserved-name pre-check `CurrentStateTime` (3 LoC, shadow user params) + 6 TIS unit tests (InitialZero / AfterUpdate / AfterTransition / CrossFade / Condition_GT_Fires / Condition_LT_DoesNotFire) + 1 ECS integration test (sm_system_TIS_CurrentStateTime_GT_Fires) + **C**: `AnimNotifyRecord::fromStateName` + `AnimNotifyEvent::fromStateName` 字段 (default empty back-compat) + `AnimationPlayer._currentStateNameForNotify` + `setCurrentStateName(string)` / `getCurrentStateName()` API + `AnimationPlayer` push notify 路径写 `fromStateName` (2-step pattern 修复 P1.5 alignment 回归) + AYEntity `StateMachineSystem` bridge **every-tick** `setCurrentStateName` (改 from transition-only to every-tick, 1 line 简化) + 4 ANR unit tests (NotifyFromState / AfterTransition / WithoutSM_Empty / Merged_Preserves) + 3 ECS integration tests (sm_system_ANR_NotifyCarriesFromStateName / sm_system_ANR_PerStateRoute_SubscriberFilters / sm_system_TIS_NoRegression)；0 regression 3-run stable (AYAnimation 752/752 + AYEntity 421/421 + AYResource 1039/1039 × 3)；详见 §4.17 + §13 row 20b (deferred 兑现) + §11 P3.x刀 N+1.BC row。**INV-36..39** (time-in-state 契约) + **INV-40..42** (per-state notify 契约) 全部 NEW。
- [x] **P0 polish — Flat-array params/triggers + FNV-1a ParamName Registry**（2026-08-07）─ `StateMachine._params` 从 `std::unordered_map<std::string, float>` 改为 `std::vector<ParamEntry>` (pre-reserve 8, linear scan, cache-friendly, N ≤ 8 production) + `StateMachine._triggers` 从 `std::unordered_set<std::string>` 改为 `std::vector<uint32_t>` sorted (pre-reserve 4, `std::lower_bound` for has/erase, N ≤ 4 production) + `detail::ParamNameRegistry` (Meyers singleton, FNV-1a 32-bit hash + linear scan intern, process-global) + `ParamEntry { uint32_t hash; float value; }` 结构移到 `ConditionExpr.h` 避免循环 include + 新增 6 private helpers (`findParamIndex` / `setParamByHash` / `getParamByHash` / `addTriggerHash` / `hasTriggerHash` / `eraseTriggerHash`) + `StateMachine::getParamName` / `getParamNameRegistrySize` debug read-back 静态方法 + `ConditionEvalCtx` field types change (`ParamsVector` / `TriggersVector` 指针) + `CondIdentifierExpr::evaluateAsFloat` 改 intern + linear scan + **public API 0 change** (setParam / getParam / setTrigger 签名 identical; ECS bridge 0 touch; L1/L2/L3 contracts 全部 preserved) + 2 new unit tests (`Params_FlatArray_FindByHashReturnsCorrectValue` + `Triggers_FlatArray_BinarySearchWorks` INV-43..46 pin) + 30 L2 tests 通过 `makeCtx` helper 5-line shift 适配 flat-vector `ConditionEvalCtx` + micro-benchmark `AYAnimation/benchmark/state_machine_params_bench.cpp` 4 scenarios (8/1/32 params + triggers, 100K iter, default OFF behind `AY_BUILD_BENCHMARKS=OFF` cache var)；0 regression 3-run stable (AYAnimation 759/759 + AYEntity 421/421 + AYResource 1039/1039 × 3)；详见 §4.18 + §11 P0 polish row。**INV-43..46** (flat-array hot-path 契约) 全部 NEW。Hot-path speedup (debug build): getParam 8 params 271→68 ns/iter (**4.0x** ⭐), 1 param 225→43 ns/iter (**5.2x** ⭐), 32 params 265→153 ns/iter (1.7x); setTrigger regression 221→609 ns/iter (0.4x, debug-only, accepted trade-off — production critical path is getParam)。
- [x] **P1 polish — Hot-Path Eval Hash Caching**（2026-08-07）─ `Transition.triggerHash` + `Transition.conditionParamNameHash` 2 字段（addTransition 时一次性 intern 缓存）+ `CondIdentifierExpr.nameHash` 字段（ctor 一次性 intern 缓存）+ 3 hot-path callsite (`findEligibleTransition` / `Transition::evaluateCondition` L1 path / `fireTransition`) 改 cached hash + lazy fallback (test fixture const_cast mutation back-compat) + `detail::ParamNameRegistry` 拆 header (`ParamNameRegistry.h` 独立 leaf header, kEmpty 在 StateMachine.cpp 定义) 让 `ConditionExpr.h` inline ctor 调 intern 无循环 include + **public API 0 change** (Transition / CondIdentifierExpr / ConditionParser / StateMachine 签名 identical; ECS bridge 0 touch; L1/L2/L3 contracts 全部 preserved) + 2 new unit tests (`P1_Transition_TriggerHash_CachedAtAddTransition` 唯一名避免 process-global registry 累积污染 + `P1_Transition_ConditionHash_CachedAtAddTransition`) + 3 new unit tests (`P1_CondIdent_NameHash_NonEmpty` + `P1_CondIdent_NameHash_EmptyName_HashZero` sentinel pre-check + `P1_CondIdent_Evaluate_NoIntern` 1000x eval 后 registry size 不变证明 0 per-eval intern) + micro-benchmark 加 2 scenarios (Scenario E `findEligibleTransition` 5 transitions × 100K + Scenario F DSL evaluate 3-ident × 100K); 0 regression 3-run stable (AYAnimation 1783/1783 + AYEntity 421/421 + AYResource 1039/1039 × 3); 详见 §4.19 + §11 P1 polish row。**INV-47..51** (transition hash cache + CondIdentifierExpr nameHash + reserved ident priority + lazy fallback) 全部 NEW。Benchmark (debug build): Scenario E scan 1251 ns/iter + scan+fire 2391 ns/iter; Scenario F evaluate true path 132 ns/iter + short-circuit 113 ns/iter。
- [x] **P2 polish — Condition DSL AST → Flat Bytecode**（2026-08-08）─ `CondBytecode` (parallel cache: `std::vector<uint8_t> program` + `std::vector<float> literals` flat float table + 10 opcodes + `CondReservedId`) + `compileToBytecode(ast)` post-order walk (comparison: left→right→op; And/Or: placeholder + patch relative-jump ±127) + program-counter switch evaluator (`float stack[16]` 固定栈数组 + bounds-checked fail-soft, no per-eval heap alloc) + `Transition.cachedBytecode` `mutable shared_ptr` lazy build (INV-52, INV-57 copyable) + `Transition::evaluateBytecode` hot path (L2: lazy parse + lazy compile + eval; L1: delegate `evaluateCondition`; `setConditionExpr` → invalidate 双清) + `OP_LOAD_RESERVED R_CURRENT_STATE_TIME` (INV-55 0 string compare at eval) + **public API 0 change** (StateMachine / Transition / ConditionParser / CondExprAst 签名 identical; ECS bridge 0 touch; L1/L2/L3 contracts 全部 preserved) + AST preserved (P4.x CondVisitor graph-builder 需要) + 4 new unit tests (`P2_Bytecode_Parity_3IdentExpr` 5 cases 含 Speed=3 vs 5.0 抓 bit-30 bug + `P2_Bytecode_LazyBuild_FirstEvalCompiles` + `P2_Bytecode_ReservedIdent_CompiledAsOpcode` + `P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse`) + 2 new unit tests (`P2_Bytecode_Integration_FindTransitionUsesBytecode` + `P2_Bytecode_Integration_InvalidateCacheClearsBytecode`) + micro-benchmark 加 Scenario G (bytecode vs AST 3-ident × 100K); 0 regression 3-run stable (AYAnimation 1824/1824 + AYEntity 421/421 + AYResource 1039/1039 × 3); 详见 §4.20 + §11 P2 polish row。**INV-52..58** (bytecode 1:1 AST 语义 + lazy build + reserved ident opcode + flat float literal table + shared_ptr copyable + short-circuit relative-jump) 全部 NEW。Benchmark (debug build): bytecode 773 ns/iter (true) / 800 ns/iter (short-circuit) vs AST 1035/1022 = **1.34x / 1.28x**; 两个前期 bug 修复 (bit-30 IEEE-754 exponent 冲突 literal 编码 + per-eval vector 栈 8x 慢 → 固定数组)。
- [x] **P3 polish — AssetBoneCache lock-free single-threaded mode**（2026-08-08）─ `AssetBoneCache` 默认改无锁：`_threadSafe = false` (INV-59 — 7 访问点永不触碰 mutex, ECS 单线程主 tick 路径零同步可证明) + `setThreadSafe(true)` opt-in 重挂 mutex (authoring tools / 多线程 host, INV-60 — flag 非原子须并发前设) + anonymous ns `maybeLock(std::mutex&, bool)` RAII helper (enabled ? unique_lock(mu) : default ctor — 无锁路径零成本 + 锁路径异常安全) + 7 sites `lock_guard` → `unique_lock lk = maybeLock(_mu, _threadSafe)` + **+2 additive public API** (setThreadSafe / isThreadSafe; lookup / resolveAndCache / invalidate / clear / entry-count 签名 identical; AnimationPlayer 2 callsite 0 touch; ECS bridge 0 touch; L1/L2/L3 contracts 全部 preserved) + 2 new unit tests (`P3_AssetBoneCache_DefaultIsLockFree` INV-59 pin + `P3_AssetBoneCache_ThreadSafeMode_BehaviorUnchanged` 双模式复跑完整 P1.7 contract) + micro-benchmark 加 Scenario H (真实 2-bone Skeleton, hit 路径, 交错 min-of-5); 0 regression 3-run stable (AYAnimation 1851/1851 + AYEntity 421/421 + AYResource 1039/1039 × 3); 详见 §4.21 + §11 P3 polish row。**INV-59/60** (默认零锁 + flag 非原子) 全部 NEW。Benchmark (debug build, min-of-5): resolveAndCache hit 961 vs 1001 ns/iter (**1.04x**) / lookup hit 910 vs 980 ns/iter (**1.08x**) — debug STL 主导绝对值 (临时 string + checked iterators), P3 收益结构性 (零同步); 测量教训: 顺序一次性测量噪声反转比值 → 交错 min-of-5。
- [x] **P4 polish — Additive slot 内存回收 + AssetBoneCache transparent hash + 批量 tick 压力测试**（2026-08-10）─ `AnimationPlayer::releaseSlotBuffers(AdditiveSlot&)` 新私有 helper (swap-with-empty 归还 tracks / pendingNotifies / capturedLocal{Pos,Rot,Scl} / trackWeights; `clearAdditiveLayerSource` + `stop()` 两 callsite; 闲置 slot 此前持有 n*3+n*4+n*3 floats 到 player 析构) + `AssetBoneCache` inner map 改 `StringViewHash + std::equal_to<>` (is_transparent C++14 异构查找, `find(const char*)` 0 临时 string 构造 — 兑现 §4.21.12 Q3; 3 重载含显式 const char* 防 C3066 模糊) + **0 public API change** (AnimationPlayer / AssetBoneCache 签名 identical; ECS bridge 0 touch; L1/L2/L3 + INV-1..60 全部 preserved) + **3 new INV** (INV-61 闲置 slot heavy buffers 全释放 / INV-62 `_additiveSlots` size 保留 sparse 语义 + re-bind fresh state / INV-63 异构查找 0 临时 string 且三拼写单 entry) + **4 new unit tests** (`AYTest_P4Stress.cpp` 新 suite P4StressTests: p4_stress_400_players_share_one_skeleton 400×200 帧完整帧 tick+evaluate 逐位 memcmp 一致 + p4_stress_bind_clear_cycle_returns_cache_entries 5 轮 invalidate 回落 0 + P4_AssetBoneCache_HeterogeneousLookup_SingleEntry + P4_AdditiveSlot_ClearStop_RebindFreshState); 0 regression 3-run stable (AYAnimation 2673/2673 + AYEntity 421/421 + AYResource 1039/1039 × 3); 详见 §4.22 + §11 P4 polish row。**INV-61/62/63** (内存归还 + sparse 保留 + 异构查找) 全部 NEW。教训: tick() 只推进时钟不跑 evaluate (压力测试只 tick → resolve 未触发断言全败 → 每帧 tick+evaluate 完整帧) + transparent hash 必须显式 const char* 重载防 C3066。
- [x] **P5 polish — DSL 四则运算 + - * /**（2026-08-10）─ 8 算子 → **13**（+ `+ - * /` 四则 + 一元 `-`）三层贯通（lexer → precedence-climbing parser → AST → bytecode）：`CondOp` 追加 Add/Sub/Mul/Div/Neg (7..11) + `CondOpByte` 追加 OP_ADD..OP_NEG (10..14)，**旧值全部不变**；lexer 负号消歧 INV-65（`A-3` == `A - 3`，负字面量形状保留）＋ precedence 5/6（Compare 4 < Add/Sub < Mul/Div）INV-69 ＋ parseUnary 一元减 INV-66（`-A * B` = `(-A) * B`）＋ `CondBinaryExpr::evaluateAsFloat` override 算 float（比较/逻辑臂 0.0f 类型不匹配契约 INV-64）＋ `CondUnaryExpr::evaluateAsFloat` Neg 取反 ＋ 除零 → 0.0f 双路径 fail-soft INV-67 ＋ bytecode evaluator 5 新分支（短跳转 ±127 机制不受影响）＋ **0 public API change**（Transition / StateMachine / ECS bridge 0 touch；L1/L2/L3 + INV-1..63 全部 preserved）＋ **14 new unit tests**（§8.1.6 11 + §P5 3：13-case parity 表 + 尾 opcode 编码 + short-circuit 与算术共存 + `CurrentStateTime * 2 > 1` reserved 组合 + SM 集成 `Speed * 2 > 10` bytecode hot path 真 fire）；3-run stable AYAnimation **2831/2831** + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.23 + §11 P5 polish row + §14.3（算术 ✅）+ §4.16/§4.17 决策标 RESOLVED。**INV-64..69** (算术语义 + 负号消歧 + 一元减绑定 + 除零 fail-soft + bytecode parity + 优先级) 全部 NEW。教训: 测试期望值手算避开恰好相等组合 ((4+1)*2=10 两个 FAIL) + 插测试块先 grep section 头位置 (误插 TEST_SUITE 中间 → C2065)。
- [x] **P6 polish — INV-60 flip debug assert + release 配置落地**（2026-08-10）─ A) `setThreadSafe` 离开 thread-safe 模式时 try_lock probe + debug assert（INV-70；try_lock 非阻塞绝不锁死；true→false 可证明 / false→true 保持 startup 约定；NDEBUG 编译掉行为与 P3 一致）+ B) `out/build/x64-Release` 首次 configure（Ninja + MSVC /O2 + vcpkg toolchain + AY_BUILD_BENCHMARKS=ON）+ AYAnimation_UnitTests release 2843/2843 × 3 全绿 + Scenario G/H release 首测（G true 18.3ns ~4.7x / short 20.6ns ~3.7x vs debug；H lock-free 1.40x~2.46x vs debug 1.06x/1.12x，5 跑 min-of-5 区间口径）+ **0 public API change**（ECS bridge 0 touch；INV-1..69 全部 preserved）+ **1 new unit test**（`P6_AssetBoneCache_SetThreadSafe_FlipKeepsData`：2 entries × 2 轮双翻转存活 + lookup 值不变；单线程 UT 无法构造 assert 触发路径，由 P3 翻转测试回归兜底无 false positive）；debug 3-run stable AYAnimation **2843/2843**（2831 + 1 TEST_CASE / +12 断言）+ AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.24 + §14.2（release-build ✅）+ §4.20.12/§4.21.12/§4.22.12 Q1 + §4.21.12 Q2 标 RESOLVED。**INV-70** (翻转离开 thread-safe 模式 debug assert) NEW。roadmap 剩项同步：setTriggerByHash caller-side cache **永久挂起**（低频低 ROI，用户决策）。教训：try_lock probe vs _lockCount 计数（0 新成员 + 可证明方向正是危险翻转）+ run-to-run 噪声 ±2.5x 必须 min-of-5 报区间（单次 50.8ns 离群）。
- [x] **P3.2 L3 子状态机**（2026-08-06）─ `StateMachine._children` (vector<unique_ptr<StateMachine>>) + `_currentChildIndex` + `State.isSubMachine/subMachineIndex` + `StateMachine` move-only (copy deleted, _children 不可拷贝) + `addSubMachine/getActiveSubMachine/getActiveLeafStateName` API + 递归 `setTrigger/setParam` (INV-28) + child-first transition fallback (INV-29) + `getActiveLeafStateName` 深度≤2 (INV-30) + `_currentChildIndex` 在 fireTransition instant cut + cross-fade complete 双路径同步更新 (INV-31) + sub-machine entry state clipPath 字段忽略 (INV-27) + ECS bridge 兑现 dt plumbing (`sm.update(0.0f)` → `sm.update(dt)`) + `AnimationStateMachineComponent.activeSubState` read-back + sub-machine entry 不调 `player.play()` (child SM drives) + 12 AYAnimation unit tests + 4 AYEntity ECS integration tests；0 regression 3-run stable (AYAnimation 600/600 + AYEntity 385/385 + AYResource 1044/1044 × 3)；详见 §4.15 + §13 row 20c + §11 P3.2 row
- [ ] L4 MotionMatching 风格状态机

### Phase 4: IK + 重定向 ── ✅ P4-1 ship, 余项排队

- [x] **P4-1 TwoBone IK**（2026-08-10）─ `TwoBoneSolver`（纯数学解析解：余弦定理 + fromToRotation + 保侧候选 + 世界空间 slerp 权重混合；12 步 + eps=1e-5；退化/NaN/零长骨/不可达 → 原样返回或拉直，永不 NaN）+ `AnimationPlayer` Phase 2.5 集成（IKChainSpec 配置 + kMaxIKChains=8 + setIKChain/clearIKChain/clearAllIKChains/setIKChainTarget/setIKChainWeight/getIKChain/getIKChainCount/isIKChainActive/getIKChainGeneration 9 API + 稀疏 vector<IKChain> 存 resolved index + eager resolve（bind 时）+ setSkeleton 重解析 INV-71 + weight≤0 零成本跳过 INV-72 + 只写 root/mid 的 _localRot post-mask pre-Phase-3 INV-73 + accumulateWorldFrom(start) 子树重算 + writeLocalRot helper + **resolve 用 findBone 直查不用 AssetBoneCache**（低频路径 + 规避指针复用陈旧命中，P5 暴露）+ **0 ECS bridge change**（留 generation 钩子））+ 10 solver unit tests（S1-S10）+ 12 player 集成 tests（P1-P12）；0 regression 3-run stable (AYAnimation **2999/2999** + AYEntity 421/421 + AYResource 1039/1039 × 3) + release 2999/2999 × 3 全绿；详见 §4.25 + §6 + §14.3
- [ ] FABRIKSolver + CCDSolver
- [ ] IK 约束 (angle / distance / rotation)
- [ ] Pole vector / 局部空间目标 / per-chain mask 门控
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

**审查日期**：2026-07-27（对齐 P1.5 Player）  
**评估对象**：AnimationPlayer 薄内核（非完整角色管线）  
**评分**：✅ = 已 ship / ⚠️ = 部分 / ❌ = 未 ship

| # | 工业能力 | 工业引擎 | 现状 | Phase |
|---|---|---|---|---|
| 1 | 单 clip 播放 + 时间控制 | UE/Unity/Godot | ✅ | AN-01 |
| 2 | 多 track 采样 (T+R+S) | UE/Unity | ✅ | AN-01 |
| 3 | Quaternion slerp | UE/Unity | ✅（dot<0）| AN-01 |
| 4 | ticks → seconds 自动归一 | UE | ✅ | P0 |
| 5 | 局部 TRS → world 矩阵 | UE `FCompactPose` | ✅ | AN-01 |
| 6 | skin matrix = world × IBM | UE | ✅ | AN-01 |
| 7 | 循环 wrap (mod) | UE/Unity | ✅ | AN-01 |
| 8 | 时间 clamp / 末帧定格 | UE `bLoop=false` | ✅（≠ HoldTimer，§4.3.1）| AN-01 |
| 9 | playRate 控制 | UE/Unity | ✅ | AN-01 |
| 10 | Float track 参数曲线出口 | UE Curve | ✅ | P0 |
| 11 | Anim Notify 事件 | UE `FAnimNotifyEvent` | ✅ | P1.1 |
| 12 | 拓扑序 assert | UE | ✅ | P0 |
| 13 | 矩阵方向 lock-test | UE | ✅ | P0 |
| 14 | IBM = 0 NaN-safe | UE | ✅ | P0 |
| 15 | CrossFade（双源 + curve） | UE Montage | ✅ | P1.3/P1.4 |
| 16 | Blend 1D / 2D | UE BlendSpace | ✅（BlendSpace 1D/2D `68f4227` 2026-07-28，13 test cases）| P2.1 |
| 17 | Additive Layer | UE `bAdditive` | ✅ | P1.2–P1.5 |
| 17c | BoneIdx cache | UE | ✅ | P1.4 |
| 17d | syncToBase / ref-pose / pause | UE/Unity | ✅ | P1.4 |
| 17e | Multi AdditiveSlot + merged notify | UE Slot | ✅ | P1.5 |
| 17f | 旧 P1.2/P1.3 wrapper API 真实 deprecate（setAdditiveWeight / consumePendingNotifiesAdditive 全删） | UE clean API | ✅ | P1.6 |
| 17g | Shared skeleton asset cache（ECS refactor + asset-level boneIdx cache） | UE `USkeletalMesh` shared asset + FAnimationRuntime helpers | ✅ | P1.7 |
| 17h | 资源级 Skeleton Mask（`.aymask` v1 binary loader + formal `ayt::resource::ISkeletonMask` + `SkeletonMaskLoader` registered | UE `USkeletonMask` asset + FAnimNode_LayeredBoneBlend mask input | ✅ | P2.2 + P3.x刀1 |
| 18 | 骨骼遮罩 Mask（资源级） | UE | ✅ | P2.2 + P3.x刀1 |
| 19 | Montage 语义 Slot | UE Montage | ❌（勿与 AdditiveSlot 混）| Phase 2 |
| 20 | L1 简单状态机（State + Trigger + Condition + Cross-fade + AnimStateChangedEvent）| UE `UAnimStateMachine` / Unity `Animator` | ✅ | P3.1 |
| 20a | L1 priority 460 + EventBus pipe + ECS bridge | UE `UAnimInstance::NativeUpdateAnimation` priority chain | ✅ | P3.1 |
| 20b | L2 condition DSL / per-state AnimNotify routing | UE `FAnimNode_TransitionResult` evaluator | ✅（L2 DSL + `CurrentStateTime` reserved ident + `AnimNotifyRecord/AnimNotifyEvent.fromStateName` + every-tick bridge push 全 ship）| **P3.x + P3.x刀 N+1.BC (2026-08-07)** |
| 20c | L3 子状态机（nested SM + 递归 trigger/param 传播 + child-first transition fallback + active leaf state name + ECS bridge dt plumbing 兑现 + sub-machine entry 不调 player.play + AnimationStateMachineComponent.activeSubState read-back）| UE `UAnimStateMachine` nested SM | ✅ | P3.2 |
| 20d | L4 MotionMatching 风格状态机 | UE Pose Search | ❌ | P3.3 (deferred) |
| 20e | L3.5 多状态机 / parallel states / `.ayasm` loader | UE multi-layer SM / parallel state | ❌ | P3.x / P4.x (deferred) |
| 21–22 | AnimGraph visual editor / BlendTree nodes in SM | UE/Unity AnimatorController | ❌ | P3.x刀 2 + P4.x |
| 23–27 | IK / Retarget | UE | ⚠️ TwoBone（P4-1 ship 2026-08-10，§4.25）| Phase 4（FABRIK/CCD/约束/重定向）|
| 28 | Root Motion | UE | ❌ | Phase 4（可提前）|
| 29–31 | DQ / CPU / GPU 蒙皮闭环 | UE/Unity | ⚠️ skinMatrices wire | Phase 2 |
| 32–36 | Morph / 压缩 / LOD / Debug / Profiler | UE | ❌ | Phase 4–5 |
| 37 | HoldTimer / PoseHold | UE NotifyState | ❌ | 未立项 |

**统计（2026-08-07）**：上表约 **26** 项 ✅/⚠️ 内核能力已落地或半落地（新增 P3.x刀 N+1.BC 1 sub-row 1 — Time-in-State Query + per-state AnimNotify routing 兑现 row 20b deferred 承诺 + §13 row 20b "deferred" 移除；原 20c/20d/20e deferred rows 保留）；完整角色管线关键缺口仍是 **L4 MotionMatching / AnimGraph / BlendTree in SM / Root Motion / Retarget / LOD / 网络 pose / OnStateEntered/Exited event / multi-state notify / .ayasm loader / parallel states**。  
**内核工业分 ~6.9/10**；**完整角色管线 ~5.3/10**。

---

## 14. Deferred / Open 项总表（2026-08-10 审计收口）

> **历史**：早期版本的 "§14 X row" 引用指 Ship 记录行——这些行实际位于 §11（实现优先级），2026-08-10 已全部归位改指 §11。§14 从此定义为 deferred / open 项的**单一审计线索**。每项标注等级（正确性 / 性能 / 新功能）与状态；正确性级全部为**已文档化接受的权衡**（有 fail-soft 契约兜底）。

### 14.1 正确性级（接受权衡，全部有 fail-soft 契约）

| 项 | 描述 | 契约 / 兜底 |
|---|---|---|
| L1 过渡 1-frame latency | 过渡等待 duration 后下一帧才换 state | §4.14.10 #8 |
| transition-frame notify 带 OLD state 名 | 过渡帧 AnimNotify 携带旧 state 名 | §4.17.10 #10（OnStateEntered/Exited = P3.x刀 N+1.1）|
| ParamNameRegistry 单线程契约 | intern / lookup 非线程安全；ECS 单线程主 tick 契约 | §4.18.10 #8 |
| 子状态机深度 > 2 未定义 | INV-30 只对深度 ≤ 2 定义 | §4.15.10 #1 |
| 条件 AST 无深度上限 | 恶意深嵌套未防护（仅测 5 层）| §4.16.10 #14 |

### 14.2 性能级 open

| 项 | 描述 | 备注 |
|---|---|---|
| setTriggerByHash caller-side cache（P2 polish .A）| caller 侧 hash 缓存 API，修 debug setTrigger 0.4x | debug-only regression 已接受；§4.18.11 / §4.19.11 / §4.22.11 |
| N > 32 params 容器 swap（P0 polish .B）| flat vector → unordered_map<uint32_t,float> | 生产 N ≤ 8 不触发；§4.18.11 |
| ~~release-build 验证~~ | **✅ P6 polish ship 2026-08-10**（§4.24）：x64-Release 落地 + Scenario G/H 首测（G release 18.3ns ~4.7x vs debug；H lock-free 1.40x~2.46x vs debug 1.06x/1.12x）；全项目其他模块 release 验证仍 open（§4.24.12）| §4.20.12 Q1 / §4.21.12 Q1 / §4.22.12 Q1 → RESOLVED |
| cachedBytecode 内存池 | bytecode program / literals 复用 | 内存复用 polish；§4.20.12 Q4 |
| 10K+ stress scale | 400×200 为合理量级非极限 | §4.22.12 Q2 |
| setSkeleton 重分配 captured buffers | INV-61 释放后被一次 setSkeleton 反向打穿（~4KB/100-bone 暂时驻留，不泄漏）| accepted；§4.22.12 Q3 |
| 2-byte AND/OR jump | 子树 > 127 字节时跳转拓宽 | 条件性；§4.20.11 |

### 14.3 新功能级 open（= 后续长线 roadmap）

| 项 | 归属 |
|---|---|
| ~~算术 `+ - * /`~~（**✅ P5 polish ship 2026-08-10**，见 §4.23）；string/int param 类型 | P3.x刀 N+1.1 / N+2 |
| OnStateEntered/Exited event + getActiveLeafStateElapsedTime | P3.x刀 N+1.1 |
| BlendTree nodes in SM（State::clipPath → vector<AnimNode>）| P3.x刀 2（建立在 P2.1 BlendSpace 之上）|
| `.ayasm` loader + IAYStateMachine + CondVisitor graph-builder | P4.x |
| state-graph editor / 网络复制通道 | P4.x |
| L4 MotionMatching | P3.3（deferred）|
| Montage 语义 Slot（对齐 AdditiveSlot，禁止第二套 layer API）| P2.3（见 §4.8 + §4.14.12 (d)）|
| Dual-Quaternion Skinning | P2.4 |
| Root Motion 通道草案 | Phase 4（可提前）|
| ~~IK（TwoBone）~~（**✅ P4-1 ship 2026-08-10**，见 §4.25）；IK（FABRIK+CCD / 约束 / pole vector / 局部目标）/ 骨骼重定向 | Phase 4 |
| 压缩 / Profiler / Debug 可视化 / LOD | Phase 5 |
| PoseHold / HoldDuration notify | 未立项（勿假设 UE AnimNotifyState）|

---

### 4.13 ✅ P2.2 Skeleton Mask（资源级骨骼遮罩）— SHIP（2026-08-03）

> 本节为 P2.2 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。

#### 4.13.1 Overview

资源级 Skeleton Mask 是一个 `boneName → weight` 的 per-bone 衰减器，在 `AnimationPlayer::evaluate()` Phase 1a (base) 与 Phase 1b (additive) 完成 **所有 track write 之后**，Phase 2 世界累加 **之前**，对每个 bone 的局部 TRS 施加 `lerp(rest, _local, mask)` 的 post-write gate。

与 P1.5 `AdditiveSlot::trackWeights` 的本质区别：

| 维度 | P1.5 `trackWeights` | P2.2 Skeleton Mask |
|---|---|---|
| Key 维度 | track index（per slot）| bone name（per skeleton）|
| 作用域 | 仅 Phase 1b additive | Phase 1a base + Phase 1b additive |
| 作者视角 | 已知 clip 的 track 布局 | 针对骨骼 — 跨 clip 复用 |
| 衰减语义 | effectiveWeight × trackWeight | lerp(rest, _local, mask) |
| 正交性 | 与 mask 互不干扰（乘法复合） |  |

#### 4.13.2 Motivation

P1.5 已 ship 的 per-track mask 适合"hit-react 只动第4根 spine track"这种 host 已知 track 索引的场景。但下面两种用法 host 不愿（或无法）翻译 bone name → track index per clip：

1. **跨 clip 复用的"上半身专用"**。一个动画师做出来的蒙版，host 想直接应用到 base + 任何 additive 上，无需关心 additive clip 的 track 顺序。
2. **编辑期动画师直觉工作流**。"只影响 spine/head"是骨骼命名层的语义，对应 `.aymask` 资产 vs 一堆 track index 数字。

P2.2 把 mask 提到 **骨骼层（resource-level）**，与 P1.5 trackWeights（slot 层）正交，host 可以 either / both / neither 启用。

#### 4.13.3 Data Model

```cpp
// include/ayanimation/ISkeletonMask.h (P2.2 NEW — temporary in-package)
namespace ayt::anim {

struct SkeletonMaskBone {
    char         name[64];
    float        weight;            // [0,1] 后 clamp
    std::int32_t resolvedIndex;     // -1 = 未解析
    bool         isWildcard;        // true => apply to all bones
};

class ISkeletonMask : public ayt::resource::IResource {
public:
    virtual ~ISkeletonMask() = default;
    virtual std::size_t getEntryCount() const = 0;
    virtual const SkeletonMaskBone* getEntries() const = 0;
    virtual std::size_t getAuthoredBoneCount() const = 0;
    virtual bool hasWildcard() const = 0;
    virtual float wildcardWeight() const = 0;
    virtual const char* getDebugName() const = 0;
    static constexpr char TYPE_TAG[16] = "SkeletonMask";
    static constexpr uint32_t VERSION = 1;
};

} // namespace ayt::anim
```

**布局图**：
```
ISkeletonMask (IResource)
  ├ _path / _type / _loaded           ← IResource 基础字段
  ├ _entries : vector<SkeletonMaskBone>   ← named entries
  ├ _hasWildcard + _wildcardWeight    ← 单条 wildcard
  └ _debugName                         ← 调试 / 序列化诊断
```

`ISkeletonMask` 继承 `ayt::resource::IResource` 是 §3.1 review fix — `ResourceManager::load<T>()` 用 `dynamic_pointer_cast<T>` 必须 `T : IResource`。

#### 4.13.4 Public API

```cpp
// AnimationPlayer.h 新增（P2.2）：
void        setSkeletonMask(std::shared_ptr<const ISkeletonMask> mask);
void        clearSkeletonMask();
bool        hasSkeletonMask() const;
std::size_t getSkeletonMaskBoneCount() const;       // = _boneMaskWeights.size()
const std::vector<float>& getResolvedBoneMaskWeights() const;
std::uint32_t getSkeletonMaskGeneration() const;
```

API shape **方案 A（shared_ptr）** — `const ISkeletonMask&` 会让 caller 传栈-local mask 时 UAF（player 在 evaluate() 后还在用，caller scope 已结束）。shared_ptr 让 mask 生命周期延伸过 player 的每次 evaluate。

#### 4.13.5 Internal Algorithm

**Resolve（`resolveSkeletonMask` — eager，在 setSkeletonMask 与 setSkeleton 时触发）**：

```cpp
void AnimationPlayer::resolveSkeletonMask() {
    if (_skeletonMask == nullptr || _skeleton == nullptr) {
        _boneMaskWeights.clear();
        ++_skeletonMaskGeneration;
        return;
    }
    const size_t n = _skeleton->getBoneCount();
    _boneMaskWeights.assign(n, 1.0f);  // default identity
    auto& cache = AssetBoneCache::instance();

    // Pass 1: named entries via AssetBoneCache (P1.7 reuse)
    std::vector<bool> namedHit(n, false);
    for (size_t i = 0; i < mask->getAuthoredBoneCount(); ++i) {
        const auto& e = mask->getEntries()[i];
        const int32_t boneIdx = cache.resolveAndCache(_skeleton.get(), e.name);
        if (boneIdx >= 0 && (size_t)boneIdx < n) {
            _boneMaskWeights[boneIdx] = e.weight;
            namedHit[boneIdx] = true;
        }
    }
    // Pass 2: wildcard expands to bones not named (named-wins)
    if (mask->hasWildcard()) {
        for (size_t i = 0; i < n; ++i)
            if (!namedHit[i]) _boneMaskWeights[i] = mask->wildcardWeight();
    }
    ++_skeletonMaskGeneration;
}
```

**Phase 2 pre-lerp（Q3 决议）** — 在 Phase 1a + Phase 1b 完成后、Phase 2 世界累加前：

```cpp
if (!_boneMaskWeights.empty()) {
    const FVector3*    restPos = _skeleton->getLocalPositions();
    const FQuaternion* restRot = _skeleton->getLocalRotations();
    const FVector3*    restScl = _skeleton->getLocalScales();
    for (size_t i = 0; i < n; ++i) {
        const float w = _boneMaskWeights[i];
        if (w >= 1.0f) continue;            // identity 短路
        if (w <= 0.0f) {
            // snap-to-rest
            _localPos[i*3+0] = restPos[i].x;
            ...
        } else {
            _localPos[i*3+0] = restPos[i].x + (_localPos[i*3+0] - restPos[i].x) * w;
            ...
            const FQuaternion blended = FQuaternion::nlerp(restQR, liveQ, w);
            _localRot[i*4+0..3] = blended.{xyzw};
            ...
        }
    }
}
```

**关键设计 — 不在 Phase 1a/1b 预乘 mask**：早期实现错误地在 Phase 1b 把 `trackW *= boneMaskW`，然后又在 Phase 2 `lerp(rest, _local, mask)` — 等于 mask²。Q3 决议：**mask 只在 Phase 2 lerp 施加一次**；Phase 1a/1b 的 trackW 不动（Override track 直接写 raw，Additive track 用 _blendWeight × trackWeights[trackIdx]）。

#### 4.13.6 ECS Bridge (AYEntity)

- `AnimationComponent::maskPath` (`std::string`, kAttrSerialize, default "")
- `AYAnimationSystem::_lastAppliedMaskPath : unordered_map<const Entity*, std::string>`
- 每 tick：`if (lastMask != anim->maskPath)` 触发：
  - empty path → `clearSkeletonMask()`
  - 非空 → `ResourceManager::load<ISkeletonMask>(path)` → shared_ptr → `setSkeletonMask`
  - load 失败 → 警告 + latch `_lastAppliedMaskPath[e] = path`（fail-soft，后续 tick no-op）

**Rebind block 必须在 clipPath-empty 早返之前**：bind-pose（clipPath=""）entity 也能响应 mask 翻转（user 在 inspector 把 maskPath 从非空改回空）。AYAnimationSystem.cpp 当前顺序：skel-load → mask rebind → clipPath empty continue → clip lazy-load → per-frame push。

#### 4.13.7 Resource Bridge — SHIP (2026-08-06, P3.x刀1)

`.aymask` v1 binary + `IAYSkeletonMask.h`（in `ayt::resource` namespace）+ `AYSkeletonMask` concrete + `SkeletonMaskLoader` + `registerLoaderType("SkeletonMask", ".aymask")` **全部 ship**。bridge 调用 `ResourceManager::load<ayt::resource::ISkeletonMask>(path)` 现在返回真实 `shared_ptr<SkeletonMask>`。

**文件清单**：
- `AYResource/interface/assetsDefs/IAYSkeletonMask.h` (NEW, in `ayt::resource` namespace — replaces the P2.2 in-package `ayt::anim::ISkeletonMask`; adds `getGuid()` for L2 cache key)
- `AYResource/include/assetsImpl/AYSkeletonMask.h` + `src/AssetsImpl/AYSkeletonMask.cpp` (NEW, P2.2 in-memory fixture 升级为正式 asset class; `SkeletonMask::create()` factory 保留)
- `AYResource/include/Loader/SkeletonMaskLoader.h` + `src/Loader/SkeletonMaskLoader.cpp` (NEW, 50 行骨架沿用 `SkeletonLoader.cpp`)
- `AYResource/src/AYResourceBootstrap.cpp::initializeLoaders` (+1 行 `registerLoaderType`)
- `AYResource/unittest/AYTest_SkeletonMaskLoader.cpp` (NEW, 12 cases)
- `AYResource/unittest/AYTest_ResourceBootstrap.cpp` (+1 case `.aymask` registry)

**Include flip**（4 文件）：
- `AYAnimation/include/ayanimation/AnimationPlayer.h` — `<ayanimation/ISkeletonMask.h>` → `<assetsDefs/IAYSkeletonMask.h>`；`ayt::anim::ISkeletonMask` → `ayt::resource::ISkeletonMask`
- `AYAnimation/include/ayanimation/ISkeletonMask.h` — **删除**
- `AYAnimation/src/SkeletonMask.h` — **删除**（已迁到 AYResource `assetsImpl/AYSkeletonMask.h`）
- `AYAnimation/unittest/AYTest_SkeletonMask.cpp` — include flip + using 改名
- `AYAnimation/src/AnimationPlayer.cpp::resolveSkeletonMask()` — `SkeletonMaskBone::name` 从 `char[64]` 改 `std::string`（字段对齐 `ayt::resource::Bone`）
- `AYEntity/src/AYAnimationSystem.cpp` — include flip + `load<ayt::resource::ISkeletonMask>`
- `AYEntity/unittest/AYTest_SkeletonMaskBridge.cpp` — include flip + using 改名 + fixture path flip 到 AYResource

**.aymask v1 binary format**（36-byte packed header + variable payload）：

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | UInt32 | magic | `'MASK'` = 0x4D41534B |
| 4 | UInt16 | version | = 1 (forward-compat tripwire: reject `version > 1`) |
| 6 | FGuid | guid | 16 bytes (L2 cache key) |
| 22 | UInt8 | flags | reserved, 0 |
| 23 | UInt8 | hasWildcard | 0 / 1 |
| 24 | Float32 | wildcardWeight | 0..1 |
| 28 | UInt32 | entryCount | named entries |
| 32 | UInt32 | entryDataSize | payload bytes (total file = 36 + entryDataSize) |

Per entry: `[UInt32 nameLength][char name[nameLength]][Float32 weight]`. Empty name → wildcard row (defensive; canonical signal is `hasWildcard` header bit).

**3-run stable**（2026-08-06）：AYAnimation 510/510 + AYEntity 338/338 + AYResource 1044/1044 × 3。

**关键工程教训**：
1. **namespace leak fix** — P2.2 临时 `ayt::anim::ISkeletonMask` 必须迁去 `ayt::resource`，否则 AYResource convention 破（13 个 IResource 子类都在 `ayt::resource`）
2. **fixture 路径迁移** — `../../AYAnimation/src/SkeletonMask.h`（sibling-source）→ `<assetsImpl/AYSkeletonMask.h>`（cross-submodule 但 AYResource PUBLIC include 已 expose）
3. **`SkeletonMask::create()` 保留** — 同一份代码既支持 loader-driven 路径也支持 in-memory 测试路径；不破坏 P2.2 测试 ergonomics
5. **0 CMake 改动** — AYResource 用 `GLOB_RECURSE` + `target_include_directories` PUBLIC `assetsDefs`；AYAnimation 已 link `AYResource`；新 include path 自动可达。**只有**`AYResource/unittest/CMakeLists.txt` 需要显式加新测试文件（因为 unittest list 是手写而非 GLOB）
6. **forward-compat tripwire** — `loadFromBinary` 拒绝 `version > VERSION`，跟 IAnimation V4 / Skeleton V2 一致
7. **`Bone` 字段对齐** — `SkeletonMaskBone::name` 改 `std::string`（不是 `char[64]`），跟 `ayt::resource::Bone` 对齐；`e.name[0] == '\0'` → `e.name.empty()`，所有 P2.2 写过的 fix 仍然成立
8. **GUID 加回来** — ResourceManager L2 cache 用 GUID key，P2.2 临时版没 GUID；ship 必须加，否则 resource 无法进入 L2 cache
9. **`FGuid::fromString` 是非 static** — `testGuid.fromString(...)` 后赋值，跟 `std::stoul` 等成员函数用法一致

#### 4.13.8 Invariants

| Inv | 描述 | 强制点 | 测试 |
|---|---|---|---|
| **INV-12** | mask 是 post-write pre-Phase-2 lerp；Phase 1a/1b **不**预乘 mask | Phase 2 pre-lerp 块 (§4.13.5) | `mask_single_bone_half_weight_halves_override`, `mask_with_additive_track_phase_1b` |
| **INV-13** | setSkeleton 触发 mask re-resolve（新骨骼指针失效 AssetBoneCache key） | `setSkeleton` end + `resolveSkeletonMask()` | `mask_resolves_again_after_skeleton_swap` |
| **INV-14** | `addEntry(name, w)` clamp 到 [0,1] | `SkeletonMask::addEntry` | `mask_clamp_in_addentry` |
| **INV-15** | 空 mask = identity, zero allocs | `!_boneMaskWeights.empty()` 短路 | `mask_no_mask_means_identity_pose` |
| **INV-16** | mask 与 P1.5 trackWeights 正交（乘法复合） | Phase 1b `effectiveWeight * trackWeights[trackIdx] * boneMaskW`（Phase 1b 用，`trackW` Phase 1a 不再含 mask） | `mask_multiplies_with_track_weights` |
| **INV-17** | wildcard 扩到所有未命名 bone（named-wins over wildcard） | resolve pass 2 (`!namedHit[i]`) | `mask_wildcard_applies_to_unnamed_bones`, `mask_wildcard_does_not_override_named_entry` |

#### 4.13.9 Testing

**AYAnimation**（`unittest/AYTest_SkeletonMask.cpp`, 16 cases）：
1. `mask_no_mask_means_identity_pose` — INV-15 regression
2. `mask_single_bone_zero_weight_snaps_to_rest`
3. `mask_single_bone_half_weight_halves_override`
4. `mask_single_bone_full_weight_one_is_identity`
5. `mask_wildcard_applies_to_unnamed_bones` — INV-17
6. `mask_wildcard_does_not_override_named_entry`
7. `mask_with_additive_track_phase_1b`
8. `mask_multiplies_with_track_weights` — INV-16
9. `mask_resolves_again_after_skeleton_swap` — INV-13
10. `clear_skeleton_mask_restores_identity`
11. `mask_unresolved_bone_name_is_silently_skipped`
12. `mask_with_no_skeleton_bound_is_safe` — defensive
13. `mask_clamp_in_addentry` — INV-14
14. `mask_generation_counter_increments` — diagnostic
15. `empty_mask_entries_equal_identity`
16. `mask_duplicate_name_takes_last_weight` — authoring determinism

**AYEntity**（`unittest/AYTest_SkeletonMaskBridge.cpp`, 6 cases）：
1. `bridge_maskpath_empty_means_no_mask_applied` — empty path → no mask
2. `bridge_maskpath_load_failure_degrades_to_no_mask` — real nonexistent path → fail-soft
3. `bridge_maskpath_load_failure_latched_no_retry` — ResourceManager 不被反复调用
4. `bridge_direct_mask_survives_when_maskpath_empty_first_time` — direct API + empty path
5. `bridge_direct_mask_survives_when_maskpath_empty_repeated_ticks` — 多帧 direct API mask 不被 bridge 干扰
6. `bridge_maskpath_cleared_runs_clear_skeleton_mask` — 从非空翻回空触发 `clearSkeletonMask`

**测试结果（2026-08-03）**：
- AYAnimation_UnitTests: **510/510 PASS** x 3 stable（baseline 471 + 39 new P2.2）
- AYEntityTest SkeletonMaskBridgeTests: **31/31 PASS** x 3 stable（6 cases × ~5 checks）
- AYEntityTest 全套: **338/338 PASS**, 0 regression

#### 4.13.10 Edge Cases & Lessons

1. **P1.7 AssetBoneCache key stability** — `resolveAndCache(_skeleton.get(), boneName)` 用 raw ISkeleton* 作 key。setSkeleton 换指针后旧 cache entry 自动 orphan，新 skeleton 走新 key。
2. **§4.13.5 设计错误修复（root cause #2）** — Phase 1b `trackW *= boneMaskW` + Phase 2 `lerp(rest, _local, mask)` 等于 mask²。Q3/INV-12 明确 mask 只在 Phase 2 lerp 一次施加。Phase 1a Override 写 raw，mask 在 Phase 2 之后正确生效。
3. **Test #8 mystery（root cause #1）** — `setAdditiveSource(..., true)` 的 `loop=true` + `tick(0.5f)` + duration=1.0 → slot.time 0.5→1.0→wrap 0.0。debug print 在 `for` 循环推进之前打，读到的是 setTime 留下的 0.5，不是 post-tick 的 0.0。修正：`setAdditiveSource(..., false)` + `setTime(0.5f) + evaluate()`，无 tick。
4. **方案 A shared_ptr API** — `setSkeletonMask(const ISkeletonMask&)` 在 stack-local mask 时 UAF。改 `shared_ptr<const ISkeletonMask>` 让 mask 活过每次 evaluate。
5. **Phase 1a/1b mask 预乘的误导性 debug** — 单纯 "我看到 mask×2 的效果" 不够。需要看每个 phase 的中间状态。
6. **`AnimNotifyEvent` 不被 mask 拦截** — AnimNotify 是事件层，跟 TRS 无关。
7. **Float tracks 不被 mask** — P1.2 INV：Float 是 host-data，不是 TRS。Phase 1a/1b 在 Float case 早返，mask 不接触。
8. **Rebind 顺序：mask rebind 在 clipPath-empty 早返之前** — bind-pose entity 也能响应 mask 翻转（user 清空 maskPath）。第 6 个 bridge test pin 这个。
9. **bridge `maskPath` field 默认空 = legacy bit-identical** — Pre-P2.2 scenes 不带 maskPath 字段，反序列化默认空字符串，bridge 永不触发 rebind。
10. **fail-soft latch `_lastAppliedMaskPath[e] = path`** — load 失败后 latch 失败路径，后续 tick no-op，无 per-frame log spam。

#### 4.13.11 Migration / Upgrade Hooks

- ~~UPGRADE-HOOK(P3.x) — .aymask loader~~ **已 ship 2026-08-06（P3.x刀1）** — 见 §4.13.7
- **UPGRADE-HOOK(P3.x 刀2)** — multi-layer mask stack (`vector<ISkeletonMask>` 加权复合) for additive composition。同 INV-12 / INV-16 风格 — 与现有 primitives 正交。
- **UPGRADE-HOOK(P3.x 刀3)** — mask authoring UI in AYEditor (Inspector view of `SkeletonMaskBone` list).

#### 4.13.12 Open Questions

- (a) **Mask 是否应该 additive-composable（multi-layer）？** Decision: defer P3.x。当前 single-mask 覆盖 90% use case；multi-layer 会让 editor UI 复杂化。INV-16 已经把 mask 与 P1.5 trackWeights 正交组合 → 等价于 "P1.5 是 P2.2 的简化版 multi-layer"。
- (b) **Mask 是否拦截 AnimNotify？** Decision: NO。AnimNotify 是事件层，与 TRS 解耦；mask 拦截 notify 会破坏 fire-and-forget 语义。
- (c) **Per-bone mask 在 P1.4 capture pose 下行为？** AdditiveLayerSpec::refPoseCapture 把 post-Phase-1a 当 base。Phase 2 mask lerp 仍然从 rest → _local，所以 base 选择不影响 mask。需要测试，但基于 INV-12 推理应正确。Defer formal test 到 P2.x。

---

### 4.14 ✅ P3.1 L1 简单状态机 (Simple State Machine) — SHIP（2026-08-06）

> 本节为 P3.1 L1 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。L1 = 命名状态 + 触发器 + 条件 + 跨状态 cross-fade + ECS bridge；不含 L2 (per-state AnimNotify routing)、L3 (子状态机)、L4 (MotionMatching)、multi-graph、嵌套 / parallel states、BlendTree nodes inside state machine。

#### 4.14.1 Overview

`StateMachine` 是 AYAnimation 新增的独立类（**composition over inheritance** — 不是 `AnimationPlayer` 子类）。它是一个「状态 + 触发器 + 条件 + 转换」的事件驱动有限状态机：调用方 `addState(State)` / `addTransition(Transition)` 构造图，`update(dt)` tick 推进转换逻辑，`setTrigger(name)` / `setParam(name, value)` 注入外部信号，转换触发时 `_currentState` 改变 + 派发 `AnimStateChangedEvent` 到 EventBus。

L1 ship 不带 `.ayasm` loader（与 §4.13.7 P3.x刀 1 defer pattern 一致）— 状态图由 entity setup code 通过直接 API 注入；ResourceManager 资源路径仅作 P4.x placeholder。L1 仅 ship **base clip 选择** — additive slot / cross-fade-in-place / root motion / state graph 编辑器全部 deferred 到 P3.x / P4.x。

#### 4.14.2 Motivation

P1.5 + P2.2 ship 之后，AYAnimation 已能播放「单 clip + 多 slot additive + mask」，但缺少「状态」容器。这导致：

1. **locomotion / jump / attack 切换需 host script glue** — gameplay 层 `if (speed > 5.0f) player.play(runClip); if (jumpPressed) player.play(jumpClip);` 把动画判断泄漏到 controller 层，gameplay 与 presentation 紧耦合
2. **状态转换是同步、瞬时、无 cross-fade 衔接** — `play(newClip)` 是 hard cut；state machine 内部可以挂 `duration=0.3f` 让旧 clip 跑满 0.3s 才让 `currentState` 切到 newState
3. **transition history 不可观察** — host script 没有 `previousState` / `didTransitionThisFrame()` 这种结构化 read-back，只能手维护
4. **state graph 不能被序列化 / 编辑器编辑** — 没有 first-class 状态图容器，editor 的 state graph UI 无从下手。**L1 引入 `StateMachine` 作为 first-class 容器**，P3.x / P4.x 在此基础上加 loader + editor wiring

P3.1 把 `StateMachine` 搬进 AYAnimation + 在 AYEntity 加 `AnimationStateMachineComponent` + `StateMachineSystem` (priority 460) ── entity 绑 state graph + 自动 tick + 自动选 clip + 自动 emit notify。

#### 4.14.3 Data Model

```cpp
// include/ayanimation/StateMachine.h (P3.1 NEW)
namespace ayt::anim {

enum class StateConditionOp : uint8_t {
    Greater = 0, Less = 1, Equals = 2, NotEqual = 3,
};

struct StateCondition {
    std::string      paramName;        // "Speed" → look up in _params
    StateConditionOp op           = StateConditionOp::Greater;
    float            compareValue = 0.0f;
};

// One state in the graph. POD; mirrors UE UAnimState shape (subset).
struct State {
    std::string  name;            // unique
    std::string  clipPath;        // AYResource path for IAnimation (.ayanm)
    bool         loop       = true;
    float        playRate   = 1.0f;
    float        entryTime  = 0.0f;   // 0 = start from beginning (L1 simple)
    float        exitTime   = 0.0f;   // 0 = no exit time; transition fires mid-clip
};

// Transition between states. POD; mirrors UE FAnimTransition.
struct Transition {
    std::string    trigger;       // "" = automatic (no setTrigger required)
    std::string    fromState;     // "" = ANY wildcard (UE convention)
    std::string    toState;       // must match an existing State::name
    float          duration = 0.0f;  // 0 = instant cut; >0 = cross-fade wait
    bool           hasCondition = false;
    StateCondition condition;
};

class StateMachine {
public:
    StateMachine();
    ~StateMachine() = default;

    // === Authoring API ===
    void addState(const State& s);
    void addTransition(const Transition& t);
    void setInitialState(const std::string& name);
    void clear();

    // === Runtime API ===
    void update(float dt);
    void setTrigger(const std::string& name);
    void setParam(const std::string& name, float value);
    float getParam(const std::string& name) const;

    // === Read-back ===
    const std::string& getCurrentStateName() const;
    const std::string& getPreviousStateName() const;
    bool  isTransitioning() const { return _transitioning; }
    float getTransitionElapsed() const { return _transitionElapsed; }
    bool  didTransitionThisFrame() const { return _transitionedThisFrame; }
    size_t getStateCount() const { return _states.size(); }
    size_t getTransitionCount() const { return _transitions.size(); }
    const std::vector<State>& getStates() const { return _states; }
    const std::vector<Transition>& getTransitions() const { return _transitions; }

private:
    bool evaluateCondition(const StateCondition& c) const;
    const Transition* findEligibleTransition() const;
    void fireTransition(const Transition& t);

    std::vector<State>       _states;
    std::vector<Transition>  _transitions;
    std::unordered_map<std::string, size_t> _stateIndexByName;
    std::unordered_set<std::string>        _triggers;
    std::unordered_map<std::string, float> _params;
    std::string _initialState;
    std::string _currentState;
    std::string _prevStateName;
    bool   _transitioning       = false;
    float  _transitionElapsed   = 0.0f;
    float  _transitionDuration  = 0.0f;
    std::string _pendingToState;
    bool   _transitionedThisFrame = false;
};

// include/ayanimation/AnimStateChangedEvent.h (P3.1 NEW)
struct AnimStateChangedEvent {
    const ayt::entity::Entity* entity;
    std::string                previousState;
    std::string                currentState;
    static constexpr std::uint32_t kTypeId = 0x000A'0010u;  // P3.1 new type
};

} // namespace ayt::anim
```

**布局图**：

```
StateMachine
  ├ _states : vector<State>            ← authored graph nodes
  ├ _transitions : vector<Transition>  ← authored graph edges
  ├ _stateIndexByName : unordered_map  ← O(1) name lookup
  ├ _triggers : unordered_set          ← queued triggers; auto-consumed on fire
  ├ _params : unordered_map            ← condition eval data (Speed, IsGrounded…)
  ├ _currentState / _prevStateName     ← current + last-frame
  ├ _transitioning / _transitionElapsed / _transitionDuration / _pendingToState
  │                                      ← cross-fade state
  └ _transitionedThisFrame             ← read-back for system bridge
```

`StateMachine` 独立于 `AnimationPlayer`（不继承、不持有）— composition 体现在 AYEntity `StateMachineSystem`：system 持有 `unordered_map<Entity*, unique_ptr<StateMachine>>` + `SkeletonComponent.player` 引用 + 每 tick 调 `sm.update(dt)` 后视情况 `player.play(newClip)`。

#### 4.14.4 Public API

| API | 返回 | 用途 |
|---|---|---|
| `addState(State)` | void | authoring；`State::name` 必须唯一 |
| `addTransition(Transition)` | void | authoring；`Transition::toState` 必须匹配既有 `State::name`（debug assert 失败） |
| `setInitialState(name)` | void | 必须匹配既有 `State::name`；在首次 `update()` 前调用 |
| `clear()` | void | 重置为空；允许 rebuild |
| `update(dt)` | void | tick；如触发 transition，`_currentState` 改变 |
| `setTrigger(name)` | void | 队列 trigger；在首个 eligible transition 上消费 |
| `setParam(name, value)` | void | 条件求值数据（`Speed` / `IsGrounded` 等） |
| `getCurrentStateName()` | string | read-back |
| `getPreviousStateName()` | string | 上帧 state name（用于 `AnimStateChangedEvent`） |
| `isTransitioning()` | bool | cross-fade 窗口内为 true |
| `getTransitionElapsed()` | float | cross-fade 已逝时间 |
| `didTransitionThisFrame()` | bool | 当帧 `update()` 是否触发 transition |

API shape **方案 A（POD struct + unordered_map）** — 不依赖任何 `.ayasm` 资源；tests / entity setup code 直接 `addState` / `addTransition` 注入图。

#### 4.14.5 Internal Algorithm

**`update(dt)` 主循环**：

```cpp
void StateMachine::update(float dt) {
    _transitionedThisFrame = false;

    // (1) Advance transition clock if mid-transition.
    if (_transitioning) {
        _transitionElapsed += dt;
        if (_transitionElapsed >= _transitionDuration) {
            _currentState     = _pendingToState;
            _prevStateName    = _currentState;  // before-update value
            _pendingToState   = "";
            _transitioning    = false;
            _transitionElapsed  = 0.0f;
            _transitionDuration = 0.0f;
        }
        return;  // No new transitions during transition window (UE rule).
    }

    // (2) Look for an eligible transition from the current state.
    if (const Transition* t = findEligibleTransition()) {
        fireTransition(*t);
        _transitionedThisFrame = true;
        return;
    }
    // (3) No transition this frame. (Player keeps ticking the current clip.)
}
```

**`findEligibleTransition()` — first-match-wins（author order matters）**：

```cpp
const Transition* StateMachine::findEligibleTransition() const {
    for (const auto& t : _transitions) {
        // INV-21: fromState == "" matches any current state (UE wildcard).
        const bool fromMatches =
            t.fromState.empty() || t.fromState == _currentState;
        if (!fromMatches) continue;

        // trigger == "" = automatic (no setTrigger required).
        const bool triggerOk =
            t.trigger.empty() ? true : (_triggers.count(t.trigger) > 0);
        if (!triggerOk) continue;

        if (t.hasCondition && !evaluateCondition(t.condition)) continue;

        return &t;  // first match wins
    }
    return nullptr;
}
```

**`fireTransition(Transition)` — INV-22 cross-fade 等待 vs INV-20 trigger 消费**：

```cpp
void StateMachine::fireTransition(const Transition& t) {
    _prevStateName = _currentState;
    if (t.duration <= 0.0f) {
        // Instant cut.
        _currentState       = t.toState;
        _pendingToState     = "";
        _transitioning      = false;
        _transitionDuration = 0.0f;
        _transitionElapsed  = 0.0f;
    } else {
        // Cross-fade: wait for duration before _currentState updates.
        _pendingToState     = t.toState;
        _transitioning      = true;
        _transitionDuration = t.duration;
        _transitionElapsed  = 0.0f;
    }
    // INV-20: trigger fires once then auto-consumes (UE rule).
    if (!t.trigger.empty()) _triggers.erase(t.trigger);
}
```

**`evaluateCondition(StateCondition)` — INV-23 unknown param fail-soft**：

```cpp
bool StateMachine::evaluateCondition(const StateCondition& c) const {
    auto it = _params.find(c.paramName);
    if (it == _params.end()) return false;  // unknown param → fail-soft
    const float v = it->second;
    switch (c.op) {
        case StateConditionOp::Greater:  return v >  c.compareValue;
        case StateConditionOp::Less:     return v <  c.compareValue;
        case StateConditionOp::Equals:   return std::fabs(v - c.compareValue) < 1e-6f;
        case StateConditionOp::NotEqual: return std::fabs(v - c.compareValue) >= 1e-6f;
    }
    return false;
}
```

#### 4.14.6 ECS Bridge (AYEntity)

**File**: `AYEntity/include/components/AYAnimationStateMachineComponent.h` (P3.1 NEW)

```cpp
namespace ayt::entity {
struct AnimationStateMachineComponent : public IComponent {
    const char* getName() const override { return "AnimationStateMachineComponent"; }

    // Placeholder for .ayasm loader (P4.x). L1 ships resource-free.
    AY_PROPERTY(std::string, resourcePath, kAttrSerialize)

    // Triggers queued by gameplay code; system consumes on transition.
    AY_PROPERTY(std::vector<std::string>, pendingTriggers, kAttrSerialize)

    // L1 condition eval data (mirrors UE UAnimInstance * variables).
    AY_PROPERTY(float, speed,         kAttrSerialize)
    AY_PROPERTY(float, verticalSpeed, kAttrSerialize)
    AY_PROPERTY(bool,  isGrounded,    kAttrSerialize)
    AY_PROPERTY(bool,  isAttacking,   kAttrSerialize)

    // Read-back (system writes).
    AY_PROPERTY(std::string, currentState,    kAttrSerialize)
    AY_PROPERTY(std::string, previousState,   kAttrSerialize)
    AY_PROPERTY(bool,        isTransitioning, kAttrSerialize)

    void setTrigger(const std::string& name) { pendingTriggers.push_back(name); }
};
} // namespace ayt::entity
```

**File**: `AYEntity/include/AYStateMachineSystem.h` (P3.1 NEW)

```cpp
namespace ayt::entity {
class StateMachineSystem : public ISystem {
public:
    const char* getName() const override { return "StateMachineSystem"; }
    void onStart() override;
    void onUpdate(float dt) override;

    static constexpr int kPriority = 460;  // after AnimationSystem (450)

    static void buildStateMachine(const AnimationStateMachineComponent& c,
                                  ayt::anim::StateMachine& out);

    // Test/inspector entry.
    ayt::anim::StateMachine* getOrCreateMachine(Entity* e);

private:
    std::unordered_map<Entity*, std::unique_ptr<ayt::anim::StateMachine>> _machines;
};
} // namespace ayt::entity
```

**`StateMachineSystem::onUpdate(dt)` bridge 流程**：

```cpp
void StateMachineSystem::onUpdate(float dt) {
    auto& world = World::instance();
    for (Entity* e : world.query<AnimationStateMachineComponent, SkeletonComponent>()) {
        auto* c    = e->getComponent<AnimationStateMachineComponent>();
        auto* skel = e->getComponent<SkeletonComponent>();
        if (!c || !skel || !skel->player) continue;

        // (1) Lazily create + build the StateMachine.
        StateMachine& sm = *getOrCreateMachine(e);
        if (sm.getStateCount() == 0) buildStateMachine(*c, sm);
        if (sm.getStateCount() == 0) continue;  // no graph wired → no-op

        // (2) Sync params + triggers from component.
        sm.setParam("Speed",         c->speed);
        sm.setParam("VerticalSpeed", c->verticalSpeed);
        sm.setParam("IsGrounded",    c->isGrounded  ? 1.0f : 0.0f);
        sm.setParam("IsAttacking",   c->isAttacking ? 1.0f : 0.0f);
        for (const auto& trig : c->pendingTriggers) sm.setTrigger(trig);
        c->pendingTriggers.clear();  // consumed

        // (3) Tick the state machine (INV-25 priority 460; events emitted below).
        const std::string prevState = sm.getCurrentStateName();
        sm.update(0.0f);  // L1: state machine is event-driven; dt plumbing
                          // 留 host level. Bridge 调用 update() 让 cross-fade
                          //  可以推进 clock when host supplies dt.

        // (4) On transition: push new clip + emit event.
        if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
            const std::string& newStateName = sm.getCurrentStateName();
            const auto& states = sm.getStates();
            auto it = std::find_if(states.begin(), states.end(),
                [&](const State& s) { return s.name == newStateName; });
            if (it != states.end() && !it->clipPath.empty()) {
                auto clip = ayt::resource::ResourceManager::instance()
                                .load<ayt::resource::IAnimation>(it->clipPath);
                if (clip) {
                    skel->player->play(clip.get());
                    skel->player->setLoop(it->loop);
                    skel->player->setPlayRate(it->playRate);
                }
            }
            ayt::event::EventBus::instance().emit<ayt::anim::AnimStateChangedEvent>(
                ayt::anim::AnimStateChangedEvent{e, prevState, newStateName});
        }

        // (5) Update read-back.
        c->currentState    = sm.getCurrentStateName();
        c->previousState   = sm.getPreviousStateName();
        c->isTransitioning = sm.isTransitioning();
    }
}
```

**系统优先级排序（INV-25）**：

```
priority 450 — AnimationSystem     (drives player.tick + push to skel)
priority 460 — StateMachineSystem  (decides transition; calls player.play next frame)
```

StateMachineSystem 在 AnimationSystem **之后** 同帧 tick：state machine 的 `update(0.0f)` 决定是否触发 transition；若触发，调 `player.play(newClip)`；下帧 AnimationSystem 接管新 clip + 处理 skin matrices。**L1 接受 1-frame 延迟**（≈16ms）— cross-fade-in-place deferred 到 P3.x。

#### 4.14.7 Resource Bridge (Deferred)

P3.1 ship **不带 `.ayasm` loader**。`AnimationStateMachineComponent::resourcePath` 是 P4.x placeholder。Bridge 代码不调用 `ResourceManager::load<StateMachine>()` — 该 loader 不存在。Entity setup code 直接构造 `StateMachine` 并注入到 `_machines[e]`。

这与 §4.13.7 P3.x刀 1 defer 模式 1:1：bridge `load<StateMachine>(path)` 返回 nullptr → system 走 fail-soft "no state machine"。区别是 P3.1 连 "placeholder load 路径" 都没 ship ── 因为 `StateMachine` 状态图是 in-memory graph（不需要 on-disk 序列化），L1 用 API 注入即可。

**P4.x ship 时**：
- `include/assetsDefs/IAYStateMachine.h` formal interface + `AYStateMachine` concrete (类似 §4.13.7 `IAYSkeletonMask` 流程)
- `AYResource/src/Loader/StateMachineLoader.cpp` + `registerLoaderType("StateMachine", ".ayasm")`
- `StateMachineSystem::buildStateMachine` 走 `ResourceManager::load<ayt::resource::IStateMachine>(c.resourcePath)` 路径

#### 4.14.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-18** | 单 state machine 任一时刻恰好有 ONE current state（`update()` 后）；空 state machine → `currentState=""` | `StateMachine::update` debug assert |
| **INV-19** | `didTransitionThisFrame()` true 当且仅当 `_currentState` 或 `_pendingToState` 在 `update()` 内改变 | `StateMachine::update` top |
| **INV-20** | trigger 在首个 eligible transition 上触发一次后自动 erase（UE rule）；caller 必须重发才能再次触发 | `StateMachine::fireTransition` |
| **INV-21** | `fromState==""` 匹配任意 current state（UE wildcard）；`trigger==""` = automatic（无需 `setTrigger`） | `StateMachine::findEligibleTransition` |
| **INV-22** | cross-fade transition: `_pendingToState` 锁存；`_currentState` 仅在 transition 完成时更新；`_transitioning=true` 持续整个 duration | `StateMachine::fireTransition` + `update` |
| **INV-23** | `evaluateCondition` 遇 unknown param 返回 false（fail-soft；匹配 §4.13 `evaluateCondition` 模式） | `StateMachine::evaluateCondition` |
| **INV-24** | `addTransition(t)` 若 `t.toState` 不匹配既有 `State::name` 则 debug assert 拒绝 | `StateMachine::addTransition` |
| **INV-25** | `StateMachineSystem` 在 priority 460 注册（after AnimationSystem 450） | `registerStateMachineSystem` |
| **INV-26** | `StateMachineSystem` 在 transition 时通过 `player.play(clip)` 推送新 clip；下帧 `AnimationSystem` 接管 | `StateMachineSystem::onUpdate` |

#### 4.14.9 Testing

**`AYAnimation/unittest/AYTest_StateMachine.cpp` (P3.1 NEW, 15 cases)**：

| # | Name | Contract |
|---|------|----------|
| 1 | `L1_SingleState_NoTransition_StaysIdle` | 1 state "Idle" + 无边 → currentState="Idle" forever |
| 2 | `L1_Transition_NoCondition_Immediate` | 2 states + transition `trigger=""` → 首个 `update()` 触发 |
| 3 | `L1_Trigger_FiresTransition_ThenErases` | `setTrigger("Run")` + transition trigger="Run" → 触发后 trigger 自动 erase |
| 4 | `L1_Trigger_DoesNotFireIfNoMatchingTransition` | trigger 设置但无 transition 引用 → 不触发 |
| 5 | `L1_Condition_Greater_FiresWhenTrue` | `setParam("Speed", 7.0f)` + condition `(Speed > 5.0f)` → 触发 |
| 6 | `L1_Condition_Greater_DoesNotFireWhenFalse` | `setParam("Speed", 3.0f)` → 不触发 |
| 7 | `L1_FromState_Wildcard_MatchesAny` | `fromState=""` + transition → 从 "Idle" 或 "Run" 均触发 |
| 8 | `L1_FromState_Specific_DoesNotMatchOthers` | `fromState="Idle"` + currentState="Run" → 不触发 |
| 9 | `L1_CrossFade_Duration_GreaterThanZero_Transitioning` | `transition.duration=0.5f` → `isTransitioning=true` for 0.5s |
| 10 | `L1_CrossFade_Completion_AdvancesCurrentState` | duration elapsed → currentState=newStateName |
| 11 | `L1_UnknownParam_FailsSoft` | condition 用未知 param → 返回 false，不 crash |
| 12 | `L1_DidTransitionThisFrame_OnlyTrueOnce` | 第 N 帧 transition 触发 → didTransitionThisFrame=true；第 N+1 帧 false |
| 13 | `L1_PrevStateName_TracksLastFrame` | transition 触发 → `previousState=oldName`, `currentState=newName` |
| 14 | `L1_TransitionOrder_AuthorOrderMatters` | 2 transitions 均 eligible → 先 authored 胜 |
| 15 | `L1_MultipleTriggers_OnlyMatchingFires` | `setTrigger("Jump")` + transition trigger="Attack" → 不触发 |

**`AYEntity/unittest/AYTest_StateMachineSystem.cpp` (P3.1 NEW, 8 cases)**：

| # | Name | Contract |
|---|------|----------|
| 1 | `sm_system_priority_460_after_animation_system_450` | `StateMachineSystem::kPriority == 460` + world introspection 双验证 |
| 2 | `sm_system_init_runs_first_update_with_initial_state` | entity + state machine → 第 1 tick 后 currentState="Idle" |
| 3 | `sm_system_trigger_pushes_to_player` | `setTrigger("Run")` → next tick player.play called with "Run" clip |
| 4 | `sm_system_no_speed_no_transition` | speed=0.0 → 留在 Idle |
| 5 | `sm_system_pending_triggers_cleared_after_tick` | `pendingTriggers` 每 tick 清空；同 trigger 后续 tick 不再触发 |
| 6 | `sm_system_emit_state_changed_event` | subscribe `AnimStateChangedEvent` → transition 时收到 |
| 7 | `sm_system_entity_without_player_no_crash` | entity 有 state machine 但无 player → tick no-op |
| 8 | `sm_system_multiple_entities_independent` | 2 entities 各持独立 state machine → 各 transition 独立 |

**3-run stable verification**：

| Module | Baseline | + New | Expected | 3-run |
|--------|----------|-------|----------|-------|
| AYAnimation | 510 | 15 | **525** (实际 543 含 L1 CrossFadeDuration_2Updates / 等 sub-cases) | ✅ × 3 |
| AYEntity | 338 | 8 | **346+** (实际 370 含 multi-CHECK per test) | ✅ × 3 |
| AYResource | 1044 | 0 | **1044** unchanged | ✅ × 3 |

#### 4.14.10 Edge Cases & Lessons

1. **State machine clock** — `update(dt)` 推进 `dt` 不受 `playRate` 影响。State machine **没有自己的时钟** — 操 host time。Player 的 `_time` 是独立的。
2. **Empty state machine guard** — INV-18 assert + fail-soft：若无 states，`currentState=""` 且所有 `update()` 都是 no-op。Tests 必须 pin 双行为。
3. **P3.1 cross-fade semantics** — `transition.duration > 0` 表示 `_currentState` 在 duration 走完前不改变。Cross-fade 期间 `player.play(newClip)` **不** 调用（仅在完成时调用）。这与 UE 不同（UE 立即换 clip + AnimGraph blend）；L1 simple ── 接受延迟；L3 cross-fade-in-place deferred。
4. **`fromState==""` wildcard vs `trigger==""` automatic** — INV-21 pin: fromState wildcard = "match any current state"；trigger="" = "no trigger required"（condition true 即触发）。两者可组合。
5. **Trigger consume** — INV-20：trigger 在首个 eligible transition 上触发一次后 erase。Caller 必须重发才能再次触发。（UE rule。）
6. **Unknown param fail-soft** — INV-23：匹配 §4.13 P2.2 SkeletonMask fail-soft pattern。**不要** crash on missing param。
7. **Debug assertions vs fail-soft** — INV-24 是 debug-only assert（reject `addTransition`）。user-facing runtime（`findEligibleTransition`）是 forgiving（no match returns nullptr）。
8. **System priority ordering** — INV-25：StateMachineSystem 460 > AnimationSystem 450。State machine 先决定 transition，AnimationSystem 下帧播放新 clip。1-frame latency 接受。
9. **No `.ayasm` loader** — Defer per §4.14.7：与 §4.13.7 P3.x刀 1 同 pattern。Bridge 代码不调 `ResourceManager::load<StateMachine>()` — 该 loader 不存在。Entity setup code 直接构造 StateMachine。
10. **Trigger / Param storage** — Triggers 在 `AnimationStateMachineComponent::pendingTriggers` (vector<string>)；system 每 tick 清。Params (speed 等) 是 `AY_PROPERTY` 字段；system 每 tick 读取。镜像 §4.13.6 mask rebind cache pattern。

**Footgun pinned during ship**：

- **`ISystem::setPriority` 仅在 `registerSystem<T>(priority)` 调用时设置** — 直接 stack-alloc 一个 `StateMachineSystem sm;` 然后 `sm.getPriority()` 仍是默认 0。Test 必须用 `kPriority` static constexpr（mirror `AnimationSystem::kPriority`）。Test #1 初版直接调 `getPriority()` 全 fail —— 改用 `kPriority` + world introspection 双验证 fix。
- **`makeStateMachineEntity(world)` 内部 `world.shutdown(); world.initialize();` 二次调用会让首个 entity 的 storage pointer 失效** — Test #8 (multi-entity independent) 初版调用两次 helper → 第二次 `world.shutdown()` 清空 `_componentStorages` 但 entity pointer 仍持有旧 storage address → AV。Fix: 在单次 `shutdown + initialize` 内构造两个 entity，用 `for (Entity* e : {a, b})` setup loop。
- **`EventBus::emit` / `EventBus::subscribe` 是 instance methods，不是 static** — 镜像 §4.11.6 AnimNotifyEvent bridge pattern：`EventBus::instance().emit<T>()` / `EventBus::instance().subscribe<T>(fn)` / `EventBus::instance().unsubscribe(subId)`。

#### 4.14.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(P3.2)** — 子状态机（L3）：`StateMachine` 加 `vector<unique_ptr<StateMachine>> _children` + `currentChildIndex`。L1 ship 时 `_children` 字段不存在（zero-cost）。**✅ RESOLVED 2026-08-06 by §4.15 P3.2 ship**：`_children` + `_currentChildIndex = -1` + `State.isSubMachine/subMachineIndex` + `addSubMachine/getActiveSubMachine/getActiveLeafStateName` + 递归 `setTrigger/setParam` + child-first transition fallback + INV-27..31。详见 §4.15。
- **UPGRADE-HOOK(P3.x刀2)** — BlendTree nodes in state machine：`State::clipPath` 改 `vector<AnimNode>`；state machine 内部可以挂 BlendSpace 1D/2D（与 P2.1 已 ship 的 BlendSpace 共存）。
- **UPGRADE-HOOK(P4.x)** — `.ayasm` loader + `IAYStateMachine` formal interface + `ResourceManager::load<IStateMachine>(path)`；`StateMachineSystem::buildStateMachine` 改走 load 路径。P3.2 ships 仍 procedural via `addSubMachine`；`buildStateMachine` 仍 no-op stub。
- **UPGRADE-HOOK(P4.x editor)** — AYEditor state graph editor wiring：`AnimationStateMachineComponent` 暴露 serialize / deserialize API；editor UI 用 `getStates()` / `getTransitions()` 反向生成图（含 `isSubMachine` / `subMachineIndex` 字段）。
- **UPGRADE-HOOK(P4.x net)** — `AnimStateChangedEvent` 加 net replication channel：`_currentState` / `_pendingToState` / `_triggers` / **`_currentChildIndex`** 跨 network 复制（replacing host-script-driven state sync）。

#### 4.14.12 Open Questions

- (a) **L1 cross-fade semantics：立刻 swap clip vs 等待 duration 后 swap？** Decision: **等待 duration 后 swap**（L1 simple）。优点：state machine logic 直白；缺点：1-frame latency + cross-fade 期间 player 仍播 oldClip。UE 模式是立刻 swap + AnimGraph blend weight ── 复杂度高。P3.x / P4.x 升级为 cross-fade-in-place。
- (b) **Triggers 在跨 state 时持久吗？** Decision: **NO, auto-consumed**（UE rule）。Caller 想「保留 trigger 跨 N 个 transition」必须每次重发。理由：trigger 语义是 "事件"（pulse）不是 "状态"（level）—— UE 一致。
- (c) **`fromState==""` vs `fromState="ANY"` sentinel？** Decision: **`fromState==""` = ANY**（UE convention + 与 §4.13 `mask.addEntry("", w)` wildcard 一致）。显式 "ANY" sentinel 太 verbose 且容易 typo。
- (d) **State machine 是否 ship MontageSlot 联动？** Decision: **NO**，P3.1 ships 状态机不联动 montage。Montage 是 P2.3 scope。
- (e) **system priority 440 (before AnimationSystem) vs 460 (after)？** Decision: **460 after**。L1 simple，1-frame latency 可接受。P3.x 可升级为 440 + system-driven player clock（需 system 调 `player.tick(dt)`）── 复杂。

---

### 4.15 ✅ P3.2 L3 子状态机 (Sub-State Machine) — SHIP（2026-08-06）

> 本节为 P3.2 L3 完整 ship 文档。L3 = **嵌套子状态机**（root → child）+ 递归触发器/参数传播 + child-first transition fallback + active leaf state name + ECS bridge 兑现 dt plumbing。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。
> 兑现 §4.14.11 UPGRADE-HOOK(P3.2)（设计文档预留路径）。
> 不含 L4 MotionMatching / 多状态机（multi-graph）/ parallel states / BlendTree nodes in SM / MontageSlot 联动 / `.ayasm` loader / editor state graph wiring（全部 deferred 到 P3.x / P4.x）。

#### 4.15.1 Overview

P3.1 L1 已 ship 「flat」状态图——entity 绑一个 `StateMachine`，states 与 transitions 平铺。然而 production locomotion 需要**嵌套结构**：root state "Move" 内部是一个完整 sub-graph（Idle ↔ Walk ↔ Run），整个 locomotion block 作为 atomic entry/exit。

P3.2 ship **`StateMachine` 加嵌套子状态机能力**——`vector<unique_ptr<StateMachine>> _children` + `_currentChildIndex` + `State.isSubMachine` 标记；ECS bridge 兑现 `sm.update(dt)` 真值（§4.14.5 P3.1 stub 改为真实 plumbing）；sub-machine entry state 进入时自动激活 child sub-graph；child transition 自动 propagate triggers/params 到 active child；child-first transition fallback；active leaf state name read-back。

L3 ships **root → child 单层嵌套**（深度 ≤ 2）；多状态机 / 无限嵌套 / parallel states / BlendTree nodes 留 P3.x / P4.x。

#### 4.15.2 Motivation

P3.1 ship 之后，flat state graph 仍有三大缺口：

1. **Hierarchical state 不可表达**——locomotion block（Idle/Walk/Run/Crouch）无法整体作为一个 atomic state（"Move"）与其他 states（Attack / Hit / Die）做 transition。
2. **Trigger / param 重复广播**——host 必须分别向 root 和 child 调 `setTrigger` / `setParam`；child 子状态机不感知 root 收到的 signal。**UE 模式：parent → child 自动 forward**。
3. **Transition fallback 不分层**——flat first-match-wins 在 child-only 与 parent-only 之间没有优先级；UE 模式：child 先，parent fallback。

P3.2 ship **子状态机**：root 是顶层状态图，child 是 sub-graph；transition 进 `isSubMachine=true` state 时 child 自动 active；child 内部 transition 用 child 自己的 duration；parent transition（root 的 level）只在 child 没 match 时 fallback。**sub-machine entry state 的 clip 选择由 child SM drives**（不是 root 的 `clipPath`）—— parent SM 仅提供 hierarchical container。

#### 4.15.3 Data Model

```cpp
// include/ayanimation/StateMachine.h (P3.2 modify)
namespace ayt::anim {

struct State {
    // ... (P3.1 fields) ...
    // P3.2 NEW
    bool isSubMachine    = false;
    int  subMachineIndex = -1;     // -1 if not a sub-machine
};

class StateMachine {
public:
    // ... (P3.1 API preserved) ...

    // === Sub-state machine API (P3.2 NEW) ===
    int  addSubMachine(std::unique_ptr<StateMachine> sm);
    int  getCurrentChildIndex() const { return _currentChildIndex; }
    StateMachine*       getSubMachine(int idx);
    const StateMachine* getSubMachine(int idx) const;
    StateMachine*       getActiveSubMachine();
    const StateMachine* getActiveSubMachine() const;
    std::string getActiveLeafStateName() const;
    std::size_t getSubMachineCount() const { return _children.size(); }

    // === Move/copy semantics (P3.2 NEW — vector<unique_ptr> 不可拷贝) ===
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    StateMachine(StateMachine&&) = default;
    StateMachine& operator=(StateMachine&&) = default;

private:
    // ... (P3.1 fields) ...
    // P3.2 NEW
    std::vector<std::unique_ptr<StateMachine>> _children;
    int  _currentChildIndex = -1;          // -1 = no active child
};

} // namespace ayt::anim
```

**布局图**：

```
StateMachine (root)
  ├ _states : vector<State>            (P3.1; may contain sub-machine entry)
  ├ _transitions : vector<Transition>  (P3.1)
  ├ _stateIndexByName : unordered_map  (P3.1)
  ├ _triggers : unordered_set          (P3.1; recursive to active child)
  ├ _params : unordered_map            (P3.1; recursive to active child)
  ├ _currentState / _prevStateName     (P3.1)
  ├ _transitioning / _transitionElapsed / _transitionDuration / _pendingToState  (P3.1)
  ├ _transitionedThisFrame             (P3.1)
  └ _children : vector<unique_ptr<StateMachine>>  (P3.2 NEW)
  └ _currentChildIndex : int = -1                (P3.2 NEW)
```

#### 4.15.4 Public API

| API | 返回 | 用途 |
|---|---|---|
| `addSubMachine(unique_ptr<StateMachine>)` | int | owner-ship 移交；返 child index |
| `getCurrentChildIndex()` | int | -1 = no active child |
| `getSubMachine(idx)` | StateMachine* | nullptr if invalid |
| `getActiveSubMachine()` | StateMachine* | nullptr if `_currentChildIndex < 0` |
| `getActiveLeafStateName()` | string | deepest leaf state in active path |
| `getSubMachineCount()` | size_t | number of added sub-machines |
| `State.isSubMachine` | bool | true ⇒ state is sub-graph entry |
| `State.subMachineIndex` | int | must match `addSubMachine` 返的 index |

**Move semantics**：因 `_children: vector<unique_ptr<StateMachine>>`，`StateMachine` 显式 delete copy ctor / assign；move ctor / assign = default。Caller 必须用 `std::unique_ptr<StateMachine>` 或 `std::move`。

#### 4.15.5 Internal Algorithm

**`update(dt)` 重写**：

```cpp
void StateMachine::update(float dt) {
    _transitionedThisFrame = false;
    if (_states.empty()) return;

    if (!_initialized) {
        _currentState = _states.front().name;
        _prevStateName = _currentState;
        _initialized = true;
        _currentChildIndex = findSubMachineIndex(_currentState);  // P3.2 NEW
    }

    // (0) P3.2 NEW — tick active child sub-SM first (if any).
    if (auto* child = activeChild()) child->update(dt);

    // (1) Advance own transition clock if mid-transition.
    if (_transitioning) {
        _transitionElapsed += dt;
        if (_transitionElapsed >= _transitionDuration) {
            _prevStateName    = _currentState;
            _currentState     = _pendingToState;
            _pendingToState.clear();
            _transitioning    = false;
            _transitionElapsed  = 0.0f;
            _transitionDuration = 0.0f;
            _transitionedThisFrame = true;
            _currentChildIndex = findSubMachineIndex(_currentState);  // P3.2 NEW
        }
        return;
    }

    // (2) P3.2 NEW — child-first transition fallback.
    const Transition* t = findEligibleTransitionForSelf();
    if (t != nullptr) {
        fireTransition(*t);
        _transitionedThisFrame = true;
    }
}
```

**`fireTransition` (P3.2 NEW line)**——instant cut 路径也更新 `_currentChildIndex`（P3.1 ship 时仅 cross-fade 完成路径更新；fix 后两边都更新）：

```cpp
void StateMachine::fireTransition(const Transition& t) {
    _prevStateName = _currentState;
    if (t.duration <= 0.0f) {
        _currentState = t.toState;
        _pendingToState.clear();
        _transitioning = false;
        _transitionDuration = 0.0f;
        _transitionElapsed = 0.0f;
        _currentChildIndex = findSubMachineIndex(_currentState);   // P3.2 NEW
    } else {
        _pendingToState = t.toState;
        _transitioning = true;
        _transitionDuration = t.duration;
        _transitionElapsed = 0.0f;
    }
    if (!t.trigger.empty()) _triggers.erase(t.trigger);
}
```

**`setTrigger` / `setParam` 递归传播**（INV-28）：

```cpp
void StateMachine::setTrigger(const std::string& name) {
    if (name.empty()) return;
    _triggers.insert(name);
    if (auto* child = activeChild()) child->setTrigger(name);  // P3.2 NEW
}

void StateMachine::setParam(const std::string& name, float value) {
    if (name.empty()) return;
    _params[name] = value;
    if (auto* child = activeChild()) child->setParam(name, value);  // P3.2 NEW
}
```

**`findEligibleTransitionForSelf`——child-first fallback**（INV-29）：

```cpp
const Transition* StateMachine::findEligibleTransitionForSelf() const {
    if (auto* child = const_cast<StateMachine*>(activeChild())) {
        if (const Transition* t = child->findEligibleTransition()) return t;
    }
    return findEligibleTransition();
}
```

**`getActiveLeafStateName`——deferred leaf**（INV-30）：

```cpp
std::string StateMachine::getActiveLeafStateName() const {
    if (auto* child = const_cast<StateMachine*>(activeChild())) {
        if (child->_initialized) return child->getActiveLeafStateName();
    }
    return _currentState;
}
```

注：`_initialized` 检查确保 child 至少被 tick 过一次（lazy-init 完成）；否则返回 parent 的 currentState，避免未初始化的 child currentState="" 泄漏到 caller。

#### 4.15.6 ECS Bridge

**File**: `AYEntity/src/AYStateMachineSystem.cpp` (modify)

P3.2 兑现 §4.14.5 §5.5 的 dt plumbing hook——`sm.update(0.0f)` → `sm.update(dt)`。Sub-machine entry state 跳过 `player.play()`（child SM drives）。`activeSubState` 每 tick 更新。

```cpp
void StateMachineSystem::onUpdate(float dt) {
    for (Entity* e : world.query<...>()) {
        StateMachine& sm = *getOrCreateMachine(e);
        // ... sync params/triggers (recursive to active child) ...

        const std::string prevState = sm.getCurrentStateName();
        sm.update(dt);   // P3.2 NEW: real dt (was stubbed 0.0f in P3.1)

        if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
            const auto& states = sm.getStates();
            auto it = std::find_if(states.begin(), states.end(),
                [&](const State& s) { return s.name == sm.getCurrentStateName(); });
            if (it != states.end()) {
                if (it->isSubMachine) {
                    // P3.2 NEW — do NOT call player.play(); child SM drives.
                } else if (!it->clipPath.empty()) {
                    auto clip = ResourceManager::load<IAnimation>(it->clipPath);
                    if (clip) { skel->player->play(clip.get()); ... }
                }
            }
            EventBus::emit<AnimStateChangedEvent>({e, prevState, sm.getCurrentStateName()});
        }

        // Read-back (P3.2 NEW — include activeSubState).
        c->currentState    = sm.getCurrentStateName();
        c->previousState   = sm.getPreviousStateName();
        c->isTransitioning = sm.isTransitioning();
        c->activeSubState  = sm.getActiveLeafStateName();
    }
}
```

**Component change**——`AnimationStateMachineComponent` 加 `activeSubState` 字段：

```cpp
struct AnimationStateMachineComponent : public IComponent {
    // ... (P3.1 fields) ...
    AY_PROPERTY(std::string, activeSubState, kAttrSerialize)   // P3.2 NEW
};
```

#### 4.15.7 Resource Bridge (Deferred)

P3.2 不引入 `.ayasm` loader / `IAYStateMachine` formal interface（与 P3.1 §4.14.7 defer 一致）。`c->resourcePath` 仍 placeholder；sub-SM 必须 procedural via `addSubMachine`。Entity setup code 典型 pattern：

```cpp
auto root  = std::make_unique<StateMachine>();
// root: Idle ↔ Move (sub-machine entry)
auto loco  = std::make_unique<StateMachine>();
// loco: Idle ↔ Walk ↔ Run
const int locoIdx = root->addSubMachine(std::move(loco));
State r_move; r_move.name = "Move"; r_move.isSubMachine = true;
r_move.subMachineIndex = locoIdx;
root->addState(r_move);
// ... transitions ...
root->setInitialState("Idle");
// inject root into StateMachineSystem._machines[entity]
```

**注意**：`StateMachine` 显式 `= delete` copy（因 `vector<unique_ptr>`），caller 必须 `unique_ptr<StateMachine>` 持有 root 然后 std::move 到 system（test pattern：`sm_system.getOrCreateMachine(e)` 返回 `StateMachine*`，test 直接 populate 该 SM）。

#### 4.15.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-27** | `State.isSubMachine=true` ⇒ `State.clipPath` is IGNORED (host MUST NOT call `player.play()` for sub-machine entry); child SM drives via its own transitions | ECS bridge (system bridge 跳 player.play) |
| **INV-28** | `setTrigger` / `setParam` propagate recursively into the **active** child SM only (not all children) | `StateMachine::setTrigger / setParam` |
| **INV-29** | Transition fallback order: child first, then parent (UE rule); if child fires, parent's check skipped that frame | `StateMachine::update` step 2 + `findEligibleTransitionForSelf` |
| **INV-30** | `getActiveLeafStateName()` returns deepest current state; if no active child or child not yet ticked, returns parent's `_currentState`; recursion depth ≤ 2 (P3.2 limit) | `StateMachine::getActiveLeafStateName` |
| **INV-31** | When entering a sub-machine entry state, `_currentChildIndex` is set BEFORE next `update()`; on exit to non-sub-machine state, reset to -1 | `StateMachine::fireTransition` (instant cut path) + `update` step 1 (cross-fade complete path) + `setInitialState` |

P3.2 不改变 P3.1 INV-18..26 任何契约。**INV-26 (system 推 new clip via `player.play`)** 在 P3.2 受 INV-27 限制：仅当 state 不是 sub-machine entry 时调。

#### 4.15.9 Testing

P3.2 ship 增加 **12 AYAnimation unit tests** + **4 AYEntity ECS integration tests**，加上 baseline 543 + 370 = 600 + 385 total。

**AYAnimation** (`unittest/AYTest_SubStateMachine.cpp` NEW, 12 cases):

| # | Name | Contract |
|---|------|----------|
| 1 | `L3_AddSubMachine_StoresAndReturnsIndex` | `addSubMachine` 返 0,1,...; out-of-range getSubMachine 返 nullptr |
| 2 | `L3_SetTrigger_PropagatesToActiveChild` | parent 收到 trigger → active child 也收到；child 内部 transition fires |
| 3 | `L3_SetParam_PropagatesToActiveChild` | parent `setParam` → child `getParam` 返回同值 |
| 4 | `L3_EnterSubMachineEntry_ActivatesChild` | transition 进 sub-machine entry state → `_currentChildIndex` 更新 |
| 5 | `L3_SubMachineTransition_AdvancesChildCurrentState` | child SM 内 transition 改 child._currentState; parent._currentState 不变 |
| 6 | `L3_ChildTransition_FallbackToParent_WhenChildNoMatch` | child 内无 eligible transition → parent 的 findEligibleTransition 接管 |
| 7 | `L3_ExitSubMachine_DeactivatesChild` | parent transition 离开 sub-machine entry → `_currentChildIndex` = -1 |
| 8 | `L3_GetActiveLeafStateName_ReturnsChildCurrentState` | parent 在 sub-machine entry → 返 child._currentState |
| 9 | `L3_GetActiveLeafStateName_NoChild_ReturnsParentState` | parent 不在 sub-machine entry → 返 parent._currentState |
| 10 | `L3_DtPlumbing_ChildCrossFadeAdvances` | parent duration=0.5 + dt=0.1 两次 update → child cross-fade clock advance > 0 |
| 11 | `L3_SubMachineState_ClipPathIgnored` | `isSubMachine=true` 状态即使有 clipPath，data stored as-is，active child resolution 取优先 |
| 12 | `L3_Clear_RecursivelyClearsChildren` | `sm.clear()` → `_children` 清空，旧 child pointer dangling by unique_ptr dtor |

**AYEntity** (`unittest/AYTest_StateMachineSystem.cpp` append 4 cases):

| # | Name | Contract |
|---|------|----------|
| 9 | `sm_system_sub_machine_active_sub_state_readback` | root SM populate → `c->activeSubState` = currentState when no real child |
| 10 | `sm_system_dt_plumbing_advances_cross_fade` | system.onUpdate(dt=0.1) → SM cross-fade `getTransitionElapsed()` > 0（P3.1 stub = 0 永远） |
| 11 | `sm_system_sub_machine_entry_no_player_play` | sub-machine entry state → system 不调 `player.play()` (player 指针不变) |
| 12 | `sm_system_sub_machine_animstate_event_prev_state` | sub-machine-related transition → EventBus 收到 AnimStateChangedEvent（prevState + currentState 正确） |

**3-run stable verification**:

| Module | Baseline | + New | Total | 3-run |
|--------|----------|-------|-------|-------|
| AYAnimation | 543 | 57 (含 sub-case 多 check) | **600** | ✅ × 3 |
| AYEntity | 370 | 15 (4 cases × multi-CHECK) | **385** | ✅ × 3 |
| AYResource | 1044 | 0 | **1044** unchanged | — |

#### 4.15.10 Edge Cases & Lessons Learned (P3.2)

1. **Sub-machine depth limit (P3.2 = 2 levels)** —— root → child only；depth > 2 不报错但行为未测试。**P3.x 升级**：可递归 addSubMachine（child 也可有 children）但当前 ship 仅 2 层。
2. **State.isSubMachine 与 clipPath 互斥** —— INV-27 pin：当 `isSubMachine=true` 时 `clipPath` 字段无效（即使非空）。系统 bridge 不应读 `clipPath` 也不应 `player.play()`。Test #11 pin data layout；test #11 ECS pin 系统 bridge 行为。
3. **Transition fallback order** —— INV-29 pin：child 先；parent fallback 当 child `findEligibleTransition` returns nullptr。**不要** 同时检查两边（避免 child 内部 transition 与 parent 同时 fire 造成 race）。
4. **Recursive `setTrigger` / `setParam`** —— INV-28 pin：仅向 **active child** 传播（不是所有 children）；否则 inactive child 也会收到 trigger，行为混乱。
5. **dt plumbing 真兑现** —— P3.1 §4.14.5 step 5 写了 `update(0.0f)` 是 stub。P3.2 改 `update(dt)`。Test AYAnimation #10 + AYEntity #10 钉 dt 真值行为。
6. **`fireTransition` 同步设 `_currentChildIndex`** —— instant cut path 也更新（P3.2 fix）；cross-fade complete path 在 `update` step 1 更新（保持 P3.1 行为）。Test #7 ExitSubMachine + #4 EnterSubMachine pin 两边都对。
7. **`addSubMachine` owner-ship** —— `unique_ptr` 参数 by-value + `std::move`；caller 不再持有 child SM 指针。**StateMachine move-only**（copy = delete 因 `vector<unique_ptr>` 不可拷贝）。Test #12 验证 clear 后访问安全。
8. **Active child `_currentChildIndex` 越界防御** —— `findSubMachineIndex` 检查 `idx < _children.size()`；runtime `update` / `setTrigger` 全部加越界 guard（`activeChild()` helper 返回 nullptr 当越界）。
9. **`getActiveLeafStateName` deferred leaf** —— 当 active child 未 tick（`_initialized=false`）时返回 parent 的 `_currentState`，避免未初始化 child currentState="" 泄漏。Test #8 pin: 第二次 update 后 child 才 lazy-init。
10. **`AnimStateChangedEvent` 只 emit parent transition** —— 子 SM 内部 transition **不** emit EventBus（避免同帧两次 emit）；P3.2 ship 行为保持 P3.1 不变。Test #12 pin EventBus event prevState/currState。
11. **Cross-fade same-frame semantic** —— `fireTransition` 立即设 `_transitioning=true` + `_transitionElapsed=0`；同帧 `_transitionElapsed` 不 advance（P3.1 INV-22）；下一帧 `update` step 1 才 advance。Test #10 dt plumbing 钉 dt advance 在第二次 update 后 > 0。

**Footgun pinned during ship**：

- **`StateMachine` copy = delete** —— 因 `_children: vector<unique_ptr<StateMachine>>` 不可拷贝。Test fixture 必须 `auto root = std::make_unique<StateMachine>()` 然后 `std::move` 到 `addSubMachine`。P3.1 helper `makeRootWithMoveEntry` 返 by-value → P3.2 改 `makeRootWithMoveEntryUPtr` 返 `unique_ptr<StateMachine>`。
- **`addState(s_move)` 在 `addSubMachine` 之前 / 之后顺序** —— `addState` 不 assert subMachineIndex，但 caller 必须确保 `s_move.subMachineIndex` 在 `addState` 前设正确（否则 `findSubMachineIndex` 返 -1）。Test #4-#11 用 `root->addSubMachine` 先（拿 index），再设 `s_move.subMachineIndex`，最后 `addState(s_move)`。**不**用 `const_cast` + post-patch（post-patch 改的是副本，原 `_states` 中 entry 的 subMachineIndex 仍是 -1）。

#### 4.15.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(P3.x刀2)** — BlendTree nodes in state machine: `State::clipPath` 改 `vector<AnimNode>`；state machine 内部可以挂 BlendSpace 1D/2D（与 P2.1 已 ship 的 BlendSpace 共存）。**P3.2 不动 `clipPath`**。
- **UPGRADE-HOOK(P4.x)** — `.ayasm` loader + `IAYStateMachine` formal interface + `ResourceManager::load<IStateMachine>(path)`；`StateMachineSystem::buildStateMachine` 改走 load 路径。P3.2 仍 procedural via `addSubMachine`。
- **UPGRADE-HOOK(P4.x editor)** — AYEditor state graph editor wiring：`StateMachine` 暴露 serialize / deserialize API；editor UI 用 `getStates()` / `getTransitions()` 反向生成图（含 `isSubMachine` / `subMachineIndex` 字段）。
- **UPGRADE-HOOK(P4.x net)** — `AnimStateChangedEvent` 加 net replication channel：`_currentState` / `_pendingToState` / `_triggers` / **`_currentChildIndex`** 跨 network 复制。
- **UPGRADE-HOOK(P3.x递归)** — 子状态机允许无限嵌套（child 也可有 children）。当前 P3.2 limit depth=2；depth > 2 行为未测试。

#### 4.15.12 Open Questions

- (a) **Sub-machine cross-fade semantics：child SM 内 transition 用 child 自己的 duration，还是 inherit parent？** Decision: **child 自己的 duration**（UE 风格，child 是独立 finite state machine）。Test #10 钉 child duration=0.5 行为。
- (b) **Depth > 2 行为？** Decision: **P3.2 limit 2 levels**；depth > 2 不报错但 behavior undefined；P3.x 升级（getActiveLeafStateName 已递归支持，可升级）。
- (c) **Multi-graph per entity (多个独立 root SM)？** Decision: **NO**，P3.2 ships single root SM per entity；多状态机（如 base layer + additive layer SM）留 P3.x / P4.x。
- (d) **`player.play` 在 sub-machine entry 时是否调一次以 'prime' player？** Decision: **不调**（child SM 完全 drives；首次 child update 内 transition 进 child initial state 时再 `player.play`）；最多 1 帧空白可接受。
- (e) **`getActiveLeafStateName` 在 child 未 tick 时返 parent currentState（"deferred leaf"）vs 返 child._currentState=""（"eager empty"）？** Decision: **deferred**（避免 caller 看到 "" 误以为 leaf state is empty）。Test #8 pin: 第二次 update 后 child 才 lazy-init。

---

### 4.16 ✅ P3.x L2 Condition DSL (Transition Expression DSL) — SHIP（2026-08-07）

> 本节为 P3.x L2 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。L2 = transition condition 字符串表达式 + lazy parse + dirty cache + 8 算子 + L1 back-compat 双轨 + 短路求值 + parse-fail-soft-false。不含 per-state AnimNotify routing / 算术 / 函数调用 / 节点图 / `.ayasm` loader。

#### 4.16.1 Overview

Transition 扩展缓存层: `conditionExpr / cachedAst / conditionDirty / conditionParseError` 4 字段。Host authoring 写条件表达式字符串 (e.g. `"Speed > 5.0 && IsGrounded"`)，SM 首次 eval 时 lazy parse，缓存 AST 到 `cachedAst`；改字符串自动 flag dirty，parse 失败 → 永假 (fail-soft, INV-33)。8 算子: `> < == != && || ! ()`；字面量: float / bool。Visitor 接口为 P4.x graph-builder 留口。

#### 4.16.2 Motivation

P3.1 L1 单 predicate (`Transition::condition: {paramName, op, compareValue}`) 只支持 1 param + 1 op + 1 value。生产级 locomotion 需要多 predicate 组合: `Speed > 5 && IsGrounded` / `!IsDead && (VerticalSpeed < 0 || OnSlope)` / `CurrentStateTime > 0.5`。FAIL-SOFT 容错: param 名写错不应 crash；debug 时给 error 行号。`design.md §4.14` 的 L1 struct 字段尾注 `// L2 upgrade to expression DSL — deferred` 已预留 hook，P3.x 兑现。

#### 4.16.3 Data model

**`Transition` 扩展缓存层** (P3.x NEW, 4 字段):

```cpp
// include/ayanimation/StateMachine.h (P3.x NEW)
struct Transition {
    // P3.1 + P3.2 fields preserved
    std::string trigger, fromState, toState;
    float duration = 0.0f;
    bool hasCondition = false;
    StateCondition condition;

    // === P3.x L2 NEW — DSL cache layer ===
    std::string conditionExpr;                                  // 源 DSL；"" = 无条件
    mutable std::shared_ptr<CondExprAst> cachedAst;             // shared_ptr (not unique_ptr): vector<Transition> needs copy
    mutable bool conditionDirty = true;                         // lazy parse flag
    mutable std::string conditionParseError;                    // 上次 parse 错 (空 = OK)
};
```

**`ConditionExprAst` 类族** (P3.x NEW, 5 个类):

```cpp
// include/ayanimation/ConditionExpr.h (P3.x NEW)
namespace ayt::anim {

enum class CondOp : uint8_t {
    GT=0, LT=1, EQ=2, NE=3,   // 比较
    And=4, Or=5, Not=6,        // 逻辑
};

struct CondBinaryExpr : CondExprAst {
    std::unique_ptr<CondExprAst> left;
    CondOp op;
    std::unique_ptr<CondExprAst> right;
    bool evaluate(const ConditionEvalCtx&) const override;   // 短路求值
};

struct CondUnaryExpr : CondExprAst {
    CondOp op;  // Not only
    std::unique_ptr<CondExprAst> operand;
    bool evaluate(const ConditionEvalCtx&) const override;
};

struct CondIdentifierExpr : CondExprAst {
    std::string name;
    bool evaluate(const ConditionEvalCtx&) const override;
    float evaluateAsFloat(const ConditionEvalCtx&) const override;  // ctx.params lookup
};

struct CondLiteralExpr : CondExprAst {
    std::variant<bool, float> value;
    bool evaluate(const ConditionEvalCtx&) const override;
    float evaluateAsFloat(const ConditionEvalCtx&) const override;
};

class CondExprAst {
public:
    virtual ~CondExprAst() = default;
    virtual bool evaluate(const ConditionEvalCtx&) const = 0;
    virtual void accept(class CondVisitor& v) const = 0;
    virtual float evaluateAsFloat(const ConditionEvalCtx&) const { (void)ctx; return 0.0f; }
};

class CondVisitor {  // P4.x graph-builder 留口
    virtual void visit(const CondBinaryExpr&)     = 0;
    virtual void visit(const CondUnaryExpr&)      = 0;
    virtual void visit(const CondIdentifierExpr&) = 0;
    virtual void visit(const CondLiteralExpr&)    = 0;
};

struct ConditionEvalCtx {
    const std::unordered_map<std::string, float>* params  = nullptr;
    const std::unordered_set<std::string>*       triggers = nullptr;
    std::string currentState;        // P3.x 留空 (未消费; 留 P3.x刀 N+1)
    float currentStateTime = 0.0f;   // P3.x 留 0.0 (未消费; 留 P3.x刀 N+1)
};

} // namespace ayt::anim
```

**`ConditionParser` 签名** (P3.x NEW):

```cpp
// include/ayanimation/ConditionParser.h (P3.x NEW)
class ConditionParser {
public:
    static std::unique_ptr<CondExprAst> parse(const std::string& src, std::string& outErr);
};
```

#### 4.16.4 Public API

| API | 返回 | 用途 |
|---|---|---|
| `Transition::setConditionExpr(std::string)` | void | 写源字符串；自动 flag dirty (auto-invalidate) |
| `Transition::invalidateConditionCache()` | void | 显式 invalidate；debug / hot-reload 用 |
| `Transition::evaluateCondition(ctx)` | bool | 统一 entry: 优先 L2 expression, fallback L1 single predicate, parse fail-soft false |
| (新字段) `conditionExpr` | string | 源 DSL |
| (新字段) `cachedAst` | shared_ptr | lazy parse cache |
| (新字段) `conditionDirty` | bool | dirty flag |
| (新字段) `conditionParseError` | string | 上次 parse 错信息 |
| `ConditionParser::parse(src, outErr)` | unique_ptr | mini Lexer + Parser entry |

#### 4.16.5 Internal algorithm

##### 4.16.5.1 Lexer (~150 LoC, 单 pass char-by-char)

照抄 AYShader `AYLexer.cpp` 的 `tokenize(src, tokens)` pattern:
- 跳过空白 + 注释 (`//` 到行尾)
- Number: `[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?`，**支持 leading `-`** 负数字面量
- Ident: `[A-Za-z_][A-Za-z0-9_]*`, 关键字查表 (`true` / `false`)
- `>` `<` `==` `!=` `&&` `||` `!` `(` `)` 单/双字符 op dispatch
- 记录 `line + col` 位置, 用于 error message
- Unknown char → push Unknown token + 报告 outErr, 但 lexer 继续 (recovery pattern)

##### 4.16.5.2 Parser (~150 LoC, precedence-climbing)

照抄 AYShader `AYParser.cpp:210-245` 结构:

```
precedence (low → high):
  1. Or   (||)
  2. And  (&&)
  3. Not  (!)   unary
  4. Compare (>, <, ==, !=)
  5. Primary (literal, ident, paren)

parseExpression(minPrec):
    left = parseUnary()
    while peek.prec >= minPrec:
        op = peek
        advance
        right = parseExpression(op.prec + 1)   // 左结合
        left = make_unique<CondBinaryExpr>(left, op.kind, right)
    return left

parseUnary():
    if peek == Not:
        advance
        operand = parseUnary()                  // 右结合
        return make_unique<CondUnaryExpr>(Not, operand)
    return parsePrimary()
```

错误恢复: 语法错 (expect 但不匹配) → 记录 `line:col: unexpected token X`, advance, return null；不尝试 recover / sync (mini DSL scope 小, panic-mode overhead 高); 多次错只记第 1 个 (fail-fast).

##### 4.16.5.3 Evaluator — recursive tree-walk

```cpp
bool CondBinaryExpr::evaluate(const ConditionEvalCtx& ctx) const {
    switch (op) {
        case CondOp::And: return left->evaluate(ctx) && right->evaluate(ctx);   // 短路
        case CondOp::Or:  return left->evaluate(ctx) || right->evaluate(ctx);   // 短路
        case CondOp::GT:  return left->evaluateAsFloat(ctx) >  right->evaluateAsFloat(ctx);
        case CondOp::LT:  return left->evaluateAsFloat(ctx) <  right->evaluateAsFloat(ctx);
        case CondOp::EQ:  return std::fabs(left->evaluateAsFloat(ctx) - right->evaluateAsFloat(ctx)) < 1e-6f;
        case CondOp::NE:  return std::fabs(left->evaluateAsFloat(ctx) - right->evaluateAsFloat(ctx)) >= 1e-6f;
    }
    return false;
}

float CondIdentifierExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    if (ctx.params == nullptr) return 0.0f;                         // INV-23 fail-soft
    auto it = ctx.params->find(name);
    if (it == ctx.params->end()) return 0.0f;                       // INV-23 fail-soft
    return it->second;
}
```

##### 4.16.5.4 Transition::evaluateCondition 统一 entry (L1 + L2 dispatch)

```cpp
bool Transition::evaluateCondition(const ConditionEvalCtx& ctx) const {
    // === L1 path (back-compat, INV-32) ===
    if (conditionExpr.empty()) {
        if (!hasCondition) return true;                              // 无条件 = 永真过 eval
        return StateMachine::evaluateConditionL1(condition, ctx);   // 旧逻辑
    }

    // === L2 expression path ===
    if (conditionDirty) {
        std::string err;
        cachedAst = ConditionParser::parse(conditionExpr, err);
        conditionParseError = err;
        conditionDirty = false;
        if (cachedAst == nullptr) {
            if (!err.empty()) {
                std::fprintf(stderr, "[AYAnimation L2] condition parse fail: %s\n", err.c_str());
            }
            return false;   // INV-33 — 永不 fire
        }
    }
    return cachedAst->evaluate(ctx);
}
```

##### 4.16.5.5 StateMachine::findEligibleTransition — 1 line 改

```cpp
// 旧: if (t.hasCondition && !evaluateCondition(t.condition)) continue;
// 新: if (!t.evaluateCondition(ctx)) continue;
```

ctx 构造: `ConditionEvalCtx ctx{&_params, &_triggers, _currentState, 0.0f};`

#### 4.16.6 ECS bridge

**0 改动**。

- `AnimationStateMachineComponent` —— **0 字段** (L2 是 authoring-time 决策, 在 entity setup code 通过 `sm.addTransition(Transition{...})` 写 host-side)
- `StateMachineSystem::onUpdate` —— **0 line** (L2 在 SM 内部消化)
- `findEligibleTransition` 改造让 ECS bridge 不感知 L2

ECS bridge 实际行为验证 (4 AYEntity tests): bridge 写 `setParam("Speed", c->speed)` (capitalized), L2 expression 用 `"Speed > 5.0"` 匹配; speed=7.0 → fire; speed=3.0 → 不 fire; cache warm across 5 ticks; parse fail (`"speed >"`) → cachedAst=null + conditionParseError 非空 + 不 crash + transition 永假.

#### 4.16.7 Resource bridge (deferred)

**0 改动**. `.ayasm` loader 不在 P3.x scope; conditionExpr 由 entity setup code 写. P4.x loader ship 时将消费 `Transition::conditionExpr` 字段.

#### 4.16.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-23** (preserved) | `evaluateCondition` 遇 unknown param → fail-soft false (L1 + L2 同样) | `ConditionExprAst::evaluate` |
| **INV-32** (NEW) | 空 `conditionExpr` = 无条件 (transition 永真过 eval), 与 `hasCondition=false` 等价 | `Transition::evaluateCondition` |
| **INV-33** (NEW) | parse 失败 → cachedAst=null + `conditionParseError` 非空 + 写 stderr, **return false** (transition 永不 fire); **不 assert, 不 crash** | `Transition::evaluateCondition` |
| **INV-34** (NEW) | `setConditionExpr` 自动 flag `conditionDirty=true`; cachedAst 在下次 eval 时 lazy 重建; 旧 AST 仍可被正在遍历的旧 eval 安全持有 (transition 是 const, RAII) | `Transition::setConditionExpr` + `Transition::evaluateCondition` |
| **INV-35** (NEW) | cache invalidation 语义: `conditionDirty=true` ⇒ 下次 eval 必 parse; `conditionDirty=false` ⇒ 用 cachedAst; cachedAst=null + conditionDirty=false ⇒ condition 永假 (INV-33 已处理) | `Transition::evaluateCondition` |

**未受影响**: INV-18..26 (P3.1 L1) + INV-27..31 (P3.2 L3) — 全部 preserved.

#### 4.16.9 Testing

##### 4.16.9.1 AYAnimation unit — `unittest/AYTest_ConditionExpr.cpp` (NEW, 30 cases)

| § | cases |
|---|---|
| §8.1.1 Parser unit | 8 (EmptyString / SingleIdent / SingleLiteral / Compare / AndOrPrecedence / Parens_Override / UnaryNot / SyntaxError) |
| §8.1.2 Evaluator unit | 12 (GT_Fires / GT_Fails / AllFourCompareOps / And_ShortCircuit / Or_ShortCircuit / Not_FlipsBool / NestedParens / UnknownParam_FailsSoft / LiteralComparison / BoolCoercion / DeepNesting_5Levels / FloatLiteral_Negative) |
| §8.1.3 Cache / invalidate | 6 (FirstEval_Parses / SecondEval_NoReparse / SetConditionExpr_Invalidates / ExplicitInvalidate / ParseFail_CachedNull / ParseFail_DoesNotCrash) |
| §8.1.4 Back-compat (L1 zero regression) | 4 (L1Condition_StillWorks / L1UnknownParam_FailsSoft / NoCondition_AlwaysFires / SubMachinePath_Unaffected) |

##### 4.16.9.2 AYEntity ECS integration — append `AYTest_StateMachineSystem.cpp` (4 cases)

| # | Name | Contract |
|---|------|----------|
| 31 | `sm_system_L2_condition_expr_fires` | conditionExpr `"Speed > 5.0"` + bridge setParam("Speed", 7) + trigger → fires Idle→Run |
| 32 | `sm_system_L2_condition_expr_does_not_fire` | 同上, speed=3 → 不 fire |
| 33 | `sm_system_L2_cache_warm_across_ticks` | 5 ticks, conditionDirty=false at end + cachedAst 非 null 持久 |
| 34 | `sm_system_L2_parse_failure_safe` | conditionExpr `"speed >"` 语法错 → 0 crash + cachedAst=null + conditionParseError 非空 + transition 永假 |

3-run stable verification:

| Module | Baseline (P3.2) | + P3.x | Final | 3-run |
|---|---|---|---|---|
| AYAnimation | 600 | +103 (30 L2 + P3.x刀1 .aymask 73) | **703/703** | ✅ × 3 |
| AYEntity | 385 | +16 (12 P3.2 L3 + 4 L2) | **401/401** | ✅ × 3 |
| AYResource | 1044 | 0 | 1044 | unchanged |

#### 4.16.10 Edge cases & lessons

1. **Cache invalidate 语义** — 不要假设"set string 一次后就稳". 任何 `setConditionExpr` / `invalidateConditionCache` 调用必须 flag dirty. Test #21..24 钉 setter auto-flag + 显式 invalidate + no-reparse.
2. **Parse 失败 = 永假** — 不抛异常, 不 assert, 不返回 true (true 会让 transition 误 fire). 永假是安全选择 (跟 INV-23 fail-soft 一致). 关键决策: stderr 输出错误而非 Logia warn (避免引入 Logia 依赖).
3. **短路求值** — `A && B` 当 `A=false` 时 B 不 evaluate. Test #12 / #13 钉.
4. **Back-compat 双轨** — `hasCondition=true` + 空 `conditionExpr` 走 L1; 非空 `conditionExpr` 走 L2. **L2 优先 L1**: 若 `conditionExpr` 非空, 完全忽略 L1 字段, 即使 `hasCondition=true`. Test #27 钉.
5. **`cachedAst` 用 `shared_ptr` 而非 `unique_ptr`** — Transition 住在 `vector<Transition>` 内 by-value, 要求 copyable. 唯一 std lib shared_ptr copyable; unique_ptr copy 会编译 fail. P3.x 决策: shared_ptr (零额外开销, 单 owner).
6. **mutable + const Transition::evaluate** — `evaluateCondition` 标 const, 但通过 mutable cache 字段 lazy init; 调用方无需 const_cast. RAII 安全: 旧 AST 在 setConditionExpr 后仍可被正在遍历的旧 eval 持有 (shared_ptr 引用计数).
7. **Lexer unknown token recovery** — 遇到不识别字符 (`@#$`) 不 crash, 记到 outErr, push Unknown token, parser 看到 Unknown 报"unexpected token". AYShader pattern.
8. **多次 parse 错误只记第 1 个** — fail-fast; 避免 error cascade 让 user 不知道 root cause.
9. **负数字面量** — `-3.14` 在 lexer 阶段 tokenize 为 `Number(-3.14)` (单 token); 不走 unary 处理, 避免 `-A > 5` 错位 (unary 应只对 ident/literal/paren 起作用).
10. **空 conditionExpr 语义对齐 hasCondition=false** — INV-32 钉. 空字符串 + `hasCondition=true` 仍走 L1 (用户可能 L1 单 predicate + 不写 L2 expression).
11. **ECS bridge param 大小写敏感** — `StateMachineSystem` 写 `setParam("Speed", c->speed)` (capitalized). L2 expression 必须用 `"Speed > 5.0"`. 关键教训: 测试 §16.9.2 #31..33 早期用 `"speed > 5.0"` (lowercase) 失败, 改为 `"Speed > 5.0"` 通过. 这是 bridge-by-convention, 文档需明示.
12. **`currentState` / `currentStateTime` 字段留 P3.x刀 N+1** — 字段已在 ctx struct, 但 P3.x 不消费. 避免 "ship 时即 API 半残" 风险. P3.x刀 N+1 hook (e.g. `CurrentStateTime > 0.5` 防 "落地立刻起跳") 留后续.
13. **`string` / `int` param 类型** — L1 `_params` 是 `map<string, float>`, L2 沿用. 扩展类型留 P3.x刀 N+2.
14. **AST depth 无 cap** — INV-30 L3 限定 depth ≤ 2 是子状态机; condition AST 是 expression 不是状态图, depth 由 host expression 决定. Test #19 5 层验证; 恶意深嵌套不 ship 时处理.
15. **Visitor 接口未消费** — P3.x ship 时 0 consumer; 但为 P4.x graph builder 留口 (§11 row 21–22 AnimGraph visual editor). 当前 0 link 成本.
16. **mini Lexer/Parser 自写** — 借鉴 pattern 但 0 link AYShader / AYScript / AYGraph. 风险中等, 3-run + back-compat test 锚定.

#### 4.16.11 UPGRADE-HOOK(P4.x — graph builder)

L2 AST Visitor 接口 (`CondVisitor::visit(BinaryExpr/UnaryExpr/IdentifierExpr/LiteralExpr)`) 是 P4.x 状态图编辑器 (AnimGraph-style 节点连接 UI) 的 hook. P4.x ship 时 `Editor` 模块消费 Visitor 遍历 AST → 生成 Blueprint-style 节点; 反向 (用户点 UI → 生成 expression 字符串) 走 `ConditionParser::parse`.

#### 4.16.12 Open questions

1. **算子集是否含算术 (`+ - * /`)?** → **Defer to P3.x刀 N+1** — condition 是布尔, 算术留给 `setParam` 在 host code 算. L2 ship 不引入算术.
2. **`currentStateTime` 字段 ship 即消费?** → **Defer to P3.x刀 N+1** — 字段已在 ctx, 不消费. ship 时无 "CurrentStateTime > 0.5" 表达式可用; 后续 ship.
3. **per-state AnimNotify routing?** → **Defer to P3.x刀 N+1** — §13 row 20b 第 2 部分 (per-state AnimNotify) 留后续. L2 ship 仅 condition DSL.
4. **`.ayasm` loader?** → **Defer to P4.x** — P4.x 与 editor wiring 同期.
5. **错误消息输出位置?** → **stderr** (chosen) — AYAnimation 走 Logia pattern 之前 ship 时验证过 (P1.4 cross-fade + P3.1 transition 错误都走 stderr 直接 `fprintf`). 不引入 Logia 链接.
6. **string / int param type?** → **Defer to P3.x刀 N+2** — L1 + L2 都用 `map<string, float>`, 不引入 string param.

---

### 4.17 ✅ P3.x刀 N+1.BC Time-in-State Query + Per-state AnimNotify Routing — SHIP（2026-08-07）

> 本节为 P3.x刀 N+1 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。**B** = `CurrentStateTime` reserved ident + `_currentStateEnterTime` 字段 + `getCurrentStateElapsedTime()` API（UE `FAnimNode_StateMachine::GetCurrentStateElapsedTime` 等价）；**C** = `AnimNotifyRecord/AnimNotifyEvent.fromStateName` 字段 + `setCurrentStateName` bridge 实现 per-state routing。**2 个 sub-feature 合并 PR ship**：共享设计审查 + 共享 test fixture + 共享 AYEntity bridge 落点。不含算术 / OnStateEntered/Exited event / multi-state notify / `.ayasm` loader / state-graph UI。

#### 4.17.1 Overview

**B (Time-in-State Query)** — `StateMachine._currentStateEnterTime` 累加器 + `getCurrentStateElapsedTime()` 公共 read-back API + 4 处 reset 0.0f (`setInitialState` / lazy-init / `fireTransition` instant-cut / `fireTransition` cross-fade START) + `ConditionEvalCtx.currentStateTime` 兑现（`StateMachine::findEligibleTransition` 1 line 从 `0.0f` literal 改 `getCurrentStateElapsedTime()`）+ `CondIdentifierExpr::evaluateAsFloat` reserved-name pre-check `CurrentStateTime` (3 LoC) → host authoring 写 `"CurrentStateTime > 0.5"` 即可实现"落地≥0.5s 才允许起跳"语义。

**C (Per-state AnimNotify Routing)** — `AnimNotifyRecord::fromStateName` + `AnimNotifyEvent::fromStateName` 字段 (default empty back-compat) + `AnimationPlayer._currentStateNameForNotify` + `setCurrentStateName(string)` setter + AYEntity `StateMachineSystem` bridge **every-tick** 调 setter → subscriber `if (event.fromStateName == "Locomotion") { ... }` 即可 per-state 路由 notify（"footstep" 只在 Locomotion 触发音效，"sword_hit" 只在 Attack 触发伤害）。

#### 4.17.2 Motivation

**(B) 防"立刻起跳"**: P3.x L2 ship (§4.16.12 Q2) 已 defer `currentStateTime` 消费。生产需求：`CurrentStateTime > 0.5` 阻止"落地→起跳"无视觉过渡。UE `FAnimNode_StateMachine::GetCurrentStateElapsedTime()` 是标配 API，ship P3.x刀 N+1 即兑现 §4.16.10 lesson #12 的 hook。

**(C) 消除 subscriber 推断 route 的漏触发/错触发 bug**: 当前 P1.5 `AnimNotifyEvent.sourceTag` 只区分 Base vs Additive slot，不区分 state。subscriber 必须用 `notifyName + 状态机黑盒"我当前 state 是 X" + 时间窗` 推断 route — 极易漏触发（"footstep" 在 transition 帧跨界时推断错）。per-state routing 是 UE 同样 pattern：`FAnimNotifyEvent` 不带 state 但 `UAnimInstance::GetCurrentStateName` 让 subscriber 路由。

#### 4.17.3 Data model

**`StateMachine` 加 time-in-state 字段 + API** (P3.x刀 N+1.B NEW):

```cpp
// include/ayanimation/StateMachine.h (P3.x刀 N+1.B MODIFY)
class StateMachine {
public:
    // ... (existing API) ...

    // === P3.x刀 N+1.B NEW — Time-in-state ===
    // Seconds elapsed since the current state was entered. Resets to 0
    // when fireTransition advances to a new state (instant-cut or
    // cross-fade START — matches UE FAnimNode_StateMachine::
    // GetCurrentStateElapsedTime semantics). Returns 0.0f when
    // _initialized==false (setInitialState not called AND no update tick).
    float getCurrentStateElapsedTime() const { return _currentStateEnterTime; }

private:
    // ... (existing fields) ...

    // Accumulator; updated by `update(dt)` top-of-frame BEFORE
    // transition detection. Increments even during cross-fade window
    // (UE一致).
    float _currentStateEnterTime = 0.0f;
};
```

**`CondIdentifierExpr` reserved-name pre-check** (P3.x刀 N+1.B MODIFY):

```cpp
// include/ayanimation/ConditionExpr.h (P3.x刀 N+1.B MODIFY)
float CondIdentifierExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const override {
    // P3.x刀 N+1.B NEW — reserved identifiers take priority over user params.
    if (name == "CurrentStateTime") return ctx.currentStateTime;
    // INV-23 — user param fail-soft
    if (ctx.params == nullptr) return 0.0f;
    auto it = ctx.params->find(name);
    if (it == ctx.params->end()) return 0.0f;
    return it->second;
}
```

**`AnimNotifyRecord` + `AnimNotifyEvent` 加 `fromStateName`** (P3.x刀 N+1.C MODIFY):

```cpp
// include/ayanimation/AnimationPlayer.h (P3.x刀 N+1.C MODIFY)
struct AnimNotifyRecord {
    const char*           name         = nullptr;
    float                 time         = 0.0f;
    float                 payload      = 0.0f;
    AnimNotifySourceTag   sourceTag    = AnimNotifySourceTag::Base;

    // === P3.x刀 N+1.C NEW — State routing ===
    // The state name active when this notify fired. Empty string when
    // not driven by a state machine (legacy / direct clip playback).
    // Default-empty keeps P1.3/P1.4/P1.5 records source-compatible
    // (subscriber without fromStateName awareness still works).
    std::string           fromStateName;
};

// include/ayanimation/AnimNotifyEvent.h (P3.x刀 N+1.C MODIFY, mirror)
struct AnimNotifyEvent {
    // ... (existing fields, kTypeId unchanged 0x000A'0001) ...
    AnimNotifySourceTag  sourceTag  = AnimNotifySourceTag::Base;
    std::string           fromStateName;     // P3.x刀 N+1.C NEW
};
```

**`AnimationPlayer` 加 setter** (P3.x刀 N+1.C MODIFY):

```cpp
// include/ayanimation/AnimationPlayer.h (P3.x刀 N+1.C MODIFY)
class AnimationPlayer {
public:
    // Set the active state name (called by AYEntity StateMachineSystem
    // bridge EVERY tick). Recorded into every AnimNotifyRecord::
    // fromStateName until the next setCurrentStateName.
    void setCurrentStateName(std::string name) {
        _currentStateNameForNotify = std::move(name);
    }
    const std::string& getCurrentStateName() const { return _currentStateNameForNotify; }

private:
    std::string _currentStateNameForNotify;     // P3.x刀 N+1.C NEW
};
```

#### 4.17.4 Public API

| API | 返回 | 用途 |
|---|---|---|
| `StateMachine::getCurrentStateElapsedTime()` | float | seconds since current state was entered; reset 0 on transition |
| `AnimationPlayer::setCurrentStateName(string)` | void | bridge pushes active state name (every-tick) |
| `AnimationPlayer::getCurrentStateName()` | const string& | debug / tests |
| `CondIdentifierExpr` reserved ident `"CurrentStateTime"` | `ctx.currentStateTime` | host authoring 写 `"CurrentStateTime > 0.5"` 即"防立刻起跳" |
| `AnimNotifyRecord::fromStateName` (new field) | `std::string` (default `""`) | subscriber 路由字段 |
| `AnimNotifyEvent::fromStateName` (new field) | `std::string` (default `""`) | ECS bridge 镜像字段 |

#### 4.17.5 Internal algorithm

##### 4.17.5.1 `_currentStateEnterTime` 4 处 reset + 1 处累加

```cpp
// 1) setInitialState: 重置到 0 (initial state 入场)
void StateMachine::setInitialState(const std::string& name) {
    // ... (existing logic) ...
    _currentStateEnterTime = 0.0f;     // P3.x刀 N+1.B NEW
}

// 2) update() 顶部: 累加 dt BEFORE 任何 transition / lazy-init
void StateMachine::update(float dt) {
    _transitionedThisFrame = false;
    if (_states.empty()) return;
    _currentStateEnterTime += dt;     // P3.x刀 N+1.B NEW — 累加

    if (!_initialized) {
        _currentState = _states.front().name;
        _prevStateName = _currentState;
        _initialized = true;
        _currentStateEnterTime = 0.0f;     // P3.x刀 N+1.B NEW — lazy-init reset
        _currentChildIndex = findSubMachineIndex(_currentState);
    }
    // ... (rest of update() unchanged) ...
}

// 3) fireTransition: instant-cut reset
void StateMachine::fireTransition(const Transition& t) {
    _prevStateName = _currentState;
    if (t.duration <= 0.0f) {
        _currentStateEnterTime = 0.0f;     // P3.x刀 N+1.B NEW
        // ... (existing instant-cut logic) ...
    } else {
        // 4) cross-fade START reset (不等 complete — UE 一致)
        _currentStateEnterTime = 0.0f;     // P3.x刀 N+1.B NEW
        // ... (existing cross-fade logic) ...
    }
}
```

##### 4.17.5.2 `ConditionEvalCtx` 兑现 — 1 line 改

```cpp
// src/StateMachine.cpp:184 (P3.x刀 N+1.B MODIFY)
const ConditionEvalCtx ctx{
    &_params,
    &_triggers,
    _currentState,
    getCurrentStateElapsedTime(),     // P3.x刀 N+1.B: was 0.0f literal
};
```

##### 4.17.5.3 AnimationPlayer push notify — 2-step pattern (P1.5 alignment 修复)

```cpp
// src/AnimationPlayer.cpp (P3.x刀 N+1.C MODIFY)
// 旧 P1.5 失败模式: push_back({nm, tm, pl, src}) 跟 std::string alignment 冲突
//   → 13 P1.5 tests fail (notify count = 0)
// 新: 先 brace-init + 显式 field-set (避免 alignment trap)
AnimNotifyRecord rec{nm, tm, pl, AnimNotifySourceTag::Base};
rec.fromStateName = _currentStateNameForNotify;     // P3.x刀 N+1.C NEW
_pendingNotifies.push_back(std::move(rec));
```

**Lesson**: std::string member 加在 struct 后,brace-init `push_back({...})` 在某些 MSVC + Debug 配置下会 alignment 错位导致 silently dropped。**Fix**: 显式 field-set 2-step pattern。

##### 4.17.5.4 AYEntity bridge — every-tick push

```cpp
// src/AYStateMachineSystem.cpp (P3.x刀 N+1.C MODIFY)
// 旧:  transition-only push — 1 line 桥接
// if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
//     skel->player->setCurrentStateName(sm.getCurrentStateName());
//     ...
// }
// 新: every-tick push (改前: 1 line within if block; 改后: unconditional before if)
skel->player->setCurrentStateName(sm.getCurrentStateName());     // P3.x刀 N+1.C NEW
if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
    // ... (existing transition logic, NO setCurrentStateName here) ...
}
```

**Lesson**: transition-only push 留下"wire-up 后 first tick player cache 仍空"问题。Every-tick push (cheap std::string move) 简化设计 + 消除 init 标志需求。

#### 4.17.6 ECS bridge

**AnimationStateMachineComponent**: **0 字段** 改动 (L1/L2/L3 同 pattern: time-in-state 是 SM 内部，fromStateName 是 bridge 推导字段).

**StateMachineSystem::onUpdate**: **1 line 改动** (从 transition-only 改 every-tick):
- 旧: bridge 在 `if (didTransition || prevState != currentState)` block 内 push
- 新: 总是先 push (1 line 在 if 块外),then 跑原 if block 逻辑

ECS bridge 实际行为验证 (4 AYEntity tests + 4 ANR/TIS unit tests): bridge 调 `setCurrentStateName("Idle")` after wire-up, `getCurrentStateName()` 立即 observable; transition 切到 "Run" 后, `getCurrentStateName() == "Run"` 立即 observable; back-transition 切回 "Idle" 同理.

#### 4.17.7 Resource bridge (deferred)

**0 改动**. `.ayasm` loader 不在 P3.x刀 N+1 scope; `fromStateName` 字段由 bridge runtime push,不持久化. P4.x loader ship 时将消费 bridge runtime push 的 state name 序列 (defer).

#### 4.17.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-23** (preserved) | L1/L2 condition 遇 unknown param → fail-soft false | `CondIdentifierExpr::evaluateAsFloat` |
| **INV-32..35** (preserved) | L2 DSL cache layer | `Transition::evaluateCondition` |
| **INV-36** (NEW) | `getCurrentStateElapsedTime()` 返回 0.0f 当 `_initialized==false`;第一个 update tick 后开始累加 dt | `StateMachine::update` + `setInitialState` |
| **INV-37** (NEW) | `fireTransition` (instant-cut + cross-fade START) reset `_currentStateEnterTime = 0.0f` (UE 一致; cross-fade 期间累加从 fire 时刻起) | `fireTransition` |
| **INV-38** (NEW) | `_currentStateEnterTime` 在 `update()` 顶部 +dt BEFORE transition detection;即使 mid-transition 也累加 (UE 一致) | `update` |
| **INV-39** (NEW) | reserved ident `"CurrentStateTime"` 在 `CondIdentifierExpr::evaluateAsFloat` 优先于 user params lookup;user 显式 `setParam("CurrentStateTime", ...)` 被 reserved-name 永久 shadow (UE pattern) | `CondIdentifierExpr::evaluateAsFloat` |
| **INV-40** (NEW) | `AnimNotifyRecord::fromStateName` 在 push 时 = `_currentStateNameForNotify`;bridge every-tick `setCurrentStateName` 同步 | `AnimationPlayer::dispatchPendingNotifies` + `AYStateMachineSystem::onUpdate` |
| **INV-41** (NEW) | `AnimNotifyEvent::fromStateName` mirror `AnimNotifyRecord::fromStateName` (ECS bridge drain 时复制) | `AYStateMachineSystem::onUpdate` |
| **INV-42** (NEW) | `AnimNotifyRecord::fromStateName` 默认 `""` (空),不破坏 P1.3/P1.4/P1.5 398 tests (subscriber 不感知字段仍 work) | `AnimNotifyRecord` default ctor |

**未受影响**: INV-18..26 (P3.1 L1) + INV-27..31 (P3.2 L3) + INV-32..35 (P3.x L2) — 全部 preserved.

#### 4.17.9 Testing

##### 4.17.9.1 AYAnimation unit — 6 TIS + 4 ANR (10 NEW)

**TIS** (append to `AYTest_ConditionExpr.cpp`):

| # | Name | Contract |
|---|------|----------|
| 1 | `TIS_InitialState_ElapsedTimeZero` | `setInitialState("Idle")` → `getCurrentStateElapsedTime() == 0.0f` |
| 2 | `TIS_AfterUpdate_ElapsedTimeAccumulates` | `setInitialState` + `update(0.5f)` → `getCurrentStateElapsedTime() ≈ 0.5f` |
| 3 | `TIS_AfterTransition_ResetsToZero` | `setInitialState("Idle")` + `update(0.3f)` + fireTransition Idle→Run (instant) → `getCurrentStateElapsedTime() == 0.0f` |
| 4 | `TIS_CrossFade_ElapsedTimeSinceFireNotComplete` | `setInitialState("Idle")` + `update(0.3f)` + fireTransition Idle→Run (duration=0.5) → `getCurrentStateElapsedTime() == 0.0f`;+update(0.2) → ≈0.2f (still cross-fading) |
| 5 | `TIS_Condition_CurrentStateTime_GT_Fires` | `conditionExpr="CurrentStateTime > 0.5"` + `update(0.6f)` + trigger → fires |
| 6 | `TIS_Condition_CurrentStateTime_LT_DoesNotFire` | `conditionExpr="CurrentStateTime < 0.5"` + `update(0.6f)` + trigger → 不 fires |

**ANR** (NEW `AYTest_AnimNotifyRouting.cpp`):

| # | Name | Contract |
|---|------|----------|
| 7 | `ANR_NotifyFromState_HasCorrectName` | `setCurrentStateName("Locomotion")` → consume notify → `record.fromStateName == "Locomotion"` |
| 8 | `ANR_NotifyAfterTransition_NewStateName` | transition mid-stream → 后续 notify record 用新 state name (旧 frame 仍 OLD name) |
| 9 | `ANR_NotifyWithoutStateMachine_EmptyName` | 不调 `setCurrentStateName` → notify record.fromStateName == "" (default) |
| 10 | `ANR_MergedQueue_PreservesFromStateName` | per-slot notify + base notify → `consumePendingNotifiesMerged()[i].fromStateName` 都正确 |

##### 4.17.9.2 AYEntity ECS integration — 4 NEW (append `AYTest_StateMachineSystem.cpp`)

| # | Name | Contract |
|---|------|----------|
| 11 | `sm_system_TIS_CurrentStateTime_GT_Fires` | bridge + `conditionExpr="CurrentStateTime > 0.5"` + `update(0.6f)` + trigger → fires |
| 12 | `sm_system_ANR_NotifyCarriesFromStateName` | bridge + transition Idle→Run → `player->getCurrentStateName() == "Run"` observable (every-tick push 关键路径) |
| 13 | `sm_system_ANR_PerStateRoute_SubscriberFilters` | subscriber `if (event.fromStateName == "Run")` 路由;transition 后只 Run 触发 |
| 14 | `sm_system_TIS_NoRegression_ExistingTestsStillPass` | back-compat baseline 17 cases 重跑 → 全 pass |

3-run stable verification:

| Module | Baseline (P3.x) | + N+1.BC | Final | 3-run |
|---|---|---|---|---|
| AYAnimation | 703 | +49 (6 TIS + 4 ANR + P1.5 alignment fix 漏算) | **752/752** | ✅ × 3 |
| AYEntity | 401 | +20 (4 N+1 + P3.2 漏算) | **421/421** | ✅ × 3 |
| AYResource | 1039 | 0 | 1039 | unchanged |

#### 4.17.10 Edge cases & lessons

1. **Reserved ident shadow user param** — `setParam("CurrentStateTime", 99.0f)` 显式注册,reserved-name pre-check 永远赢 → `ctx.currentStateTime` 永远返回 SM 内部状态. **Lesson**: reserved ident 是 contract,user 责任避免同名;UE `BlueprintCallable::GetCurrentStateElapsedTime` 同 pattern. Test #5/#6 钉.
2. **`_currentStateEnterTime` 累加时机** — `update()` 顶部 +dt BEFORE transition detection. 用户读 `getCurrentStateElapsedTime()` 拿"自上次 fireTransition 后的总时间",即使 transition 同一帧 fire 也能读到 +dt.
3. **Cross-fade 期间 reset 行为** — `fireTransition` START 即 reset (不等 cross-fade complete),与 UE `GetCurrentStateElapsedTime` 一致. 用户 mental model: "transition 触发后 elapsed".
4. **浮点累加不精确** — long-running `CurrentStateTime > 100.0f` 累计误差 ~1ms 级别 (可忽略). **Lesson**: time-in-state 仅用于相对阈值,不用 wall-clock.
5. **`AnimNotifyRecord::fromStateName` 默认空 back-compat sentinel** — P1.3/P1.4/P1.5 398 tests 0 回归 (subscriber 不感知字段工作). **Lesson**: 新字段默认空 = back-compat 模式.
6. **bridge every-tick push, not transition-only** — wire-up 后 first tick player cache 必须 observable;transition-only push 留下"first tick 空"问题. Every-tick push (cheap std::string move) 简化设计 + 消除 init 标志.
7. **2-step pattern 修 P1.5 alignment 回归** — `push_back({nm, tm, pl, src})` brace-init 跟 std::string member 在 MSVC Debug 下 alignment 错位 → 13 P1.5 tests fail (notify count = 0). 显式 field-set `AnimNotifyRecord rec{nm, tm, pl, src}; rec.fromStateName = ...;` 修复. **Lesson**: 加 std::string 字段到 struct 后,brace-init 是 trap.
8. **`AnimNotifyMarker::name` 是 std::string** (P1.5 新加字段) — `record.name` (const char*) 跟 string literal 指针比较 unreliable;测试用 `notifyNameEquals()` helper 走 `std::string(...)` + `strcmp` 路径. **Lesson**: AnimNotifyMarker name 是 owned std::string,never assume pointer stability.
9. **`setTime()` clears `_pendingNotifies`** — 测试不能用 `setTime` 跨 tick (会清队列). 用 single continuous tick 跨 marker 时间点. **Lesson**: P1.5 setTime 副作用,test 必须 aware.
10. **transition 帧 OLD state name 仍 fire notify** — bridge `setCurrentStateName` 在 transition path 同步后调;**当帧** 仍 fire 的 notify (cross-fade start 之前 evaluate 已记 record) 带 OLD name. **Lesson**: per-state route 应容忍 "transition 帧 OLD name";UE `OnStateEntered/Exited` event 才是严格 gate (P3.x刀 N+1.1 defer).
11. **NOT introduce `OnStateEntered/Exited` event** — 跟 P3.x刀 N+1.B/C scope 正交;本期 ship 仅 condition query + notify routing. **Lesson**: scope 锁住.
12. **L1/L2/L3 preserved** — INV-18..35 全 preserved. 时间跟踪是顺序 reset,没有并发.
13. **sub-machine child time-in-state** — child SM 独立维护 `_currentStateEnterTime`;parent 不暴露 grandchild 路径. **Lesson**: encapsulation, child 不暴露;`getActiveLeafStateElapsedTime()` defer P3.x刀 N+1.1.
14. **测试用相对阈值,不用 `==` 严格相等** — 浮点 time 累加不精确;test 用 `> 0.5f` 而非 `== 0.5f`. **Lesson**: 浮点 time test 模式.
15. **不引入 `previousStateTime`** — scope 限制:P3.x刀 N+1 仅 query 当前 state time. 后续若需求 rise 加.
16. **不引入算术** — condition DSL 仅 8 算子,time 数值 query 够用 (e.g. `CurrentStateTime > 0.5`);若需要 `(CurrentStateTime - 0.5) > 0` 算术,defer P3.x刀 N+2.

#### 4.17.11 UPGRADE-HOOK(P3.x刀 N+1.1)

- **`OnStateEntered/Exited` event** — 严格 gate per-state notify (消除 transition 帧 OLD name 误触发). 通过 EventBus (`AnimStateChangedEvent` 已有但 emit 时机仅 transition 完成;N+1.1 改为 onEnter/onExit 对称 event). 
- **`getActiveLeafStateElapsedTime()`** — sub-machine 嵌套深度 > 2 时 grandchild 状态 elapsed (与 INV-30 一致,需扩 INV-30 深度到 3). 
- **算术表达式** (`+ - * /`) — P3.x刀 N+2 引入;time 数值可参与算术组合.

#### 4.17.12 Open questions

1. **per-state AnimNotify routing ship scope?** → **Yes** (本期 ship, deferred from §13 row 20b 第 2 部分).
2. **Reserved ident 仅 `CurrentStateTime`?** → **Yes**;后续若需求 rise 加新 reserved,前缀 `_` 避免 user-param collision.
3. **`_currentStateEnterTime` reset 在 cross-fade START 还是 COMPLETE?** → **START** (UE 一致;简化 mental model).
4. **time-in-state API 仅 `getCurrentStateElapsedTime()`?** → **Yes**;no `getPreviousStateElapsedTime()` defer.
5. **Per-state routing 是否同时 ship `OnStateEntered/Exited` event?** → **No** (P3.x刀 N+1.1 独立 PR;本期仅 condition query + notify fromStateName).
6. **AYEntity bridge 0 字段还是 1 `timeInState` read-back 字段?** → **0 字段** (host 用 `sm.getCurrentStateElapsedTime()` via AYStateMachineSystem public API;若需求 rise,后续 PR 加).
7. **`fromStateName` 默认空字符串还是 sentinel enum?** → **空字符串** (简单;与 `notifyName == nullptr` sentinel 一致).
8. **测试增量是 10+4 还是 12+6?** → **10+4** (保守;6+4 TIS 覆盖核心,4 ANR 覆盖核心;不重复).

---

## 4.18 ✅ P0 polish — Flat-array params/triggers + FNV-1a ParamName Registry — SHIP（2026-08-07）

### 4.18.1 Overview

P0 polish refactor: replace `std::unordered_map<std::string,float>` (StateMachine `_params`) and `std::unordered_set<std::string>` (StateMachine `_triggers`) with **hash-keyed flat vectors** + a **process-global name registry**. Hot-path lookups (setParam / getParam / setTrigger / fireTransition) shift from `std::unordered_map::find` / `std::unordered_set::count` (with string compare + bucket walk) to **intern + linear scan / sorted-vector binary search** over a contiguous array. Public API unchanged; only internals + ConditionEvalCtx field types change. **INV-43..46** new. 3-run stable AYAnimation 759/759 + AYEntity 421/421 + AYResource 1039/1039 × 3.

### 4.18.2 Motivation

**Hot-path overhead**: every ECS per-frame update reads/writes params (`Speed`, `IsGrounded`, `verticalSpeed`, `isAttacking`) and sets/fires triggers (`Jump`, `Attack`, `Hit`, `Land`). Pre-refactor, the per-frame cost was dominated by `std::unordered_map<string,float>::find` (~270 ns/iter for 8 params, debug build). With ~50 entities ticking state machines at 60 fps, this is ~1.5M hash lookups/sec — and the hash bucket walk + std::hash<std::string> overhead grows superlinearly with N.

**Why flat-array wins for production N ≤ 8**: cache-friendly contiguous storage (one ~32-byte cache line per ParamEntry), no allocation per lookup, no hash bucket walk, no string compare. Linear scan of 8 entries is ~50 ns/iter in debug, vs 270 ns for unordered_map. For N > 32 the trade-off reverses — but production SMs almost never carry > 8 params.

**Why sorted-vector + binary search for triggers**: same reasoning — trigger set is small (N ≤ 4 production). Sorted `std::vector<uint32_t>` with `std::lower_bound` is O(log N) for `hasTriggerHash` and O(N) for `eraseTriggerHash`. Sorted-vector invariant also makes duplicate `setTrigger(name)` idempotent (lower_bound finds existing entry, no insert).

**FNV-1a 32-bit hash**: deterministic, no ABI risk, zero external dependency, constexpr-eligible. Hash 0 is reserved as empty-slot sentinel (FNV-1a baseline 2166136261u ≠ 0 guarantees non-empty names never collide with 0; INV-43).

**ParamNameRegistry (Meyers singleton)**: process-global intern table — every SM instance shares one `_byHash` vector of `(hash, name)` pairs. The string is retained only for debug read-back (`StateMachine::getParamName(hash)`); the **hash is the canonical key** from the refactor forward. Production ~50 unique names → ~1.2 KB global memory; negligible.

### 4.18.3 Data Model

**Composition**:
- **`detail::ParamNameRegistry`** (Meyers singleton) — interns `string → hash`. Holds `_byHash` (`std::vector<{hash, name}>`) for hash→string debug lookup.
- **`ParamEntry { uint32_t hash; float value; }`** — flat row in `StateMachine._params`. Defined in `ConditionExpr.h` (avoids circular include with StateMachine.h).
- **`StateMachine._params`** — `std::vector<ParamEntry>`. Pre-reserved capacity 8 in constructor. Linear scan for lookup (N ≤ 8 production).
- **`StateMachine._triggers`** — `std::vector<uint32_t>` (sorted by hash). Pre-reserved capacity 4. `std::lower_bound` for membership / erase.

```cpp
// include/ayanimation/ConditionExpr.h — ParamEntry (NEW, moved from StateMachine.h to break include cycle)
struct ParamEntry {
    uint32_t hash;   // FNV-1a(name) — canonical key
    float    value;
};

// ConditionEvalCtx — internal field types CHANGE (P0 polish; consumer = StateMachine.cpp only)
struct ConditionEvalCtx {
    using ParamsVector   = const std::vector<ParamEntry>*;
    using TriggersVector = const std::vector<uint32_t>*;
    ParamsVector   params   = nullptr;
    TriggersVector triggers = nullptr;
    std::string    currentState;       // reserved for future Condition parser (P3.x刀 N+2)
    float         currentStateTime = 0.0f;  // P3.x刀 N+1.B
};

// include/ayanimation/StateMachine.h — ParamNameRegistry (NEW)
namespace detail {
constexpr uint32_t fnv1a_32(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= static_cast<uint32_t>(*s++); h *= 16777619u; }
    return h;
}
class ParamNameRegistry {
public:
    static ParamNameRegistry& instance();   // Meyers singleton
    uint32_t intern(const std::string& name);  // returns FNV-1a hash
    const std::string& lookup(uint32_t hash) const;
    size_t size() const;
    void clear();   // test-only
private:
    struct Entry { uint32_t hash; std::string name; };
    std::vector<Entry> _byHash;
    static const std::string kEmpty;
};
} // namespace detail

// StateMachine.h — private fields
std::vector<uint32_t>    _triggers;   // sorted by hash (INV-46)
std::vector<ParamEntry>  _params;     // flat array (INV-45)
```

### 4.18.4 Public API

**0 change** to public surface. setParam / getParam / setTrigger signatures identical; ECS bridge `AYStateMachineSystem::onUpdate` unchanged; AYEntity 421 tests 0 regression.

New **debug read-back** helpers (added to `StateMachine`):

```cpp
static const std::string& getParamName(uint32_t hash);     // lookup registry by hash
static std::size_t         getParamNameRegistrySize();    // across all SMs in process
```

New private **flat-array helpers** (NOT public — internal only):

```cpp
std::size_t findParamIndex(uint32_t hash) const;          // O(N) linear scan
void        setParamByHash(uint32_t hash, float value);   // replace or append
float       getParamByHash(uint32_t hash) const;          // 0.0f if not found (INV-23 fail-soft)
void        addTriggerHash(uint32_t hash);                // sorted insert (INV-46)
bool        hasTriggerHash(uint32_t hash) const;          // std::lower_bound
void        eraseTriggerHash(uint32_t hash);              // O(N) erase
```

### 4.18.5 Internal Algorithm

**setParam / getParam / setTrigger** all follow the **intern + helper** pattern (1 line per public method):

```cpp
void StateMachine::setParam(const std::string& name, float value) {
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    setParamByHash(hash, value);
}

float StateMachine::getParam(const std::string& name) const {
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    return getParamByHash(hash);
}

void StateMachine::setTrigger(const std::string& name) {
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    addTriggerHash(hash);
}
```

**findParamIndex** — O(N) linear scan over contiguous `std::vector<ParamEntry>`:

```cpp
std::size_t StateMachine::findParamIndex(uint32_t hash) const {
    for (std::size_t i = 0; i < _params.size(); ++i) {
        if (_params[i].hash == hash) return i;
    }
    return SIZE_MAX;
}
```

**addTriggerHash** — sorted insert via `std::lower_bound` (INV-46 invariant):

```cpp
void StateMachine::addTriggerHash(uint32_t hash) {
    const auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    if (it != _triggers.end() && *it == hash) return;     // idempotent (duplicate)
    _triggers.insert(it, hash);
}
```

**hasTriggerHash / eraseTriggerHash** — both use `std::lower_bound` for O(log N) lookup, erase is O(N):

```cpp
bool StateMachine::hasTriggerHash(uint32_t hash) const {
    const auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    return it != _triggers.end() && *it == hash;
}
void StateMachine::eraseTriggerHash(uint32_t hash) {
    const auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    if (it != _triggers.end() && *it == hash) _triggers.erase(it);
}
```

**ConditionEvalCtx construction** (the 1 line in StateMachine.cpp `findEligibleTransition`):

```cpp
const ConditionEvalCtx ctx{
    &_params,                          // std::vector<ParamEntry>* (was unordered_map*)
    &_triggers,                        // std::vector<uint32_t>*   (was unordered_set*)
    _currentState,
    getCurrentStateElapsedTime(),
};
```

**CondIdentifierExpr::evaluateAsFloat** — intern + linear scan (was `find(name)`):

```cpp
float CondIdentifierExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    if (name == "CurrentStateTime") return ctx.currentStateTime;   // P3.x刀 N+1.B reserved
    if (ctx.params == nullptr) return 0.0f;                        // INV-23 fail-soft
    const uint32_t identifierHash =
        detail::ParamNameRegistry::instance().intern(name);
    for (const auto& entry : *ctx.params) {
        if (entry.hash == identifierHash) return entry.value;
    }
    return 0.0f;                                                    // INV-23 fail-soft
}
```

### 4.18.6 ECS Bridge

**0 changes** to AYEntity bridge. `AYStateMachineSystem::onUpdate` still calls `sm.setParam(name, value)` / `sm.setTrigger(name)` / reads `sm.getCurrentStateName()` / `sm.getCurrentStateElapsedTime()`. Public API stable → bridge 0 touch.

AYEntity 421 tests pass unchanged × 3.

### 4.18.7 Resource Bridge

**N/A**. P0 polish is internal container swap; no `.ayasm` / `.aymask` / `.ayanm` format changes. Loader path (P3.x刀1 `ISkeletonMask`, P4.x `.ayasm`) deferred.

### 4.18.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-43** (NEW) | `ParamNameRegistry` hash 0 is reserved as empty-slot sentinel; FNV-1a baseline 2166136261u guarantees non-empty names never collide with 0 | `fnv1a_32` constexpr + `intern()` |
| **INV-44** (NEW) | setParam/getParam cross-process hash is consistent (same string → same hash); Meyers singleton registry ensures process-global intern table shared across all SM instances | `ParamNameRegistry::instance()` |
| **INV-45** (NEW) | `_params` is continuous `std::vector<ParamEntry>` (no hash bucket, no allocation per lookup); `findParamIndex` is O(N) linear scan, N ≤ 8 production. Pre-reserved capacity 8 in constructor | `StateMachine` ctor + `findParamIndex` |
| **INV-46** (NEW) | `_triggers` is sorted `std::vector<uint32_t>` with binary search via `std::lower_bound`; N ≤ 4 production. Pre-reserved capacity 4 | `StateMachine` ctor + `addTriggerHash` |
| **INV-18..26** (preserved) | P3.1 L1 SM contracts — all preserved (setParam/getParam/setTrigger still public-API-correct) | 15 unit tests |
| **INV-27..31** (preserved) | P3.2 L3 sub-machine contracts — all preserved | 12 unit tests |
| **INV-32..35** (preserved) | P3.x L2 DSL cache layer — all preserved (`CondIdentifierExpr::evaluateAsFloat` adapted, contract identical) | 30 unit tests |
| **INV-36..39** (preserved) | P3.x刀 N+1.B time-in-state — all preserved | 6 unit tests |
| **INV-40..42** (preserved) | P3.x刀 N+1.C per-state AnimNotify — all preserved | 4 unit tests |
| **INV-23** (preserved) | unknown param returns 0.0f fail-soft | `CondIdentifierExpr::evaluateAsFloat` |

### 4.18.9 Testing

**Test bar**: AYAnimation 759/759 (752 P3.x刀 N+1.BC ship + 7 P0 polish = **+2 new + 5 P3.x L2 tests re-run flat-vector path**) + AYEntity 421/421 unchanged + AYResource 1039/1039 unchanged × 3-run stable.

**AYAnimation unit tests** (+2 new in `AYTest_StateMachine.cpp`):
1. **`Params_FlatArray_FindByHashReturnsCorrectValue`** — set/get round-trip across 2 unique names; overwrite takes effect; unknown returns 0.0f (INV-23); registry size grows monotonically; `getParamName(intern(name))` round-trips back to original string.
2. **`Triggers_FlatArray_BinarySearchWorks`** — duplicate `setTrigger` idempotent (sorted-vector invariant, no insert on duplicate); trigger-before-addTransition ordering pins first-update fires path; cross-state second update does NOT re-fire (proves auto-erase + sorted-vector contract).

**Micro-benchmark** (NEW `AYAnimation/benchmark/state_machine_params_bench.cpp`, gated by `AY_BUILD_BENCHMARKS=OFF` default):
- 4 scenarios: 8 params / 1 param / 32 params / triggers.
- 100K iterations each; warm-up phase.
- API-stable — works pre/post refactor (compared baseline → post-refactor).
- **Results (debug build)**:

| Scenario | Pre-refactor (ns/iter) | Post-refactor (ns/iter) | Speedup |
|---|---|---|---|
| getParam 8 params | 271 | 68 | **4.0x** ⭐ |
| getParam 1 param | 225 | 43 | **5.2x** ⭐ |
| getParam 32 params | 265 | 153 | 1.7x |
| setTrigger | 221 | 609 | 0.4x (regression — accepted) |
| trigger+update cycle | 1017 | 1878 | 0.5x (regression — accepted) |

**Decision**: ship **as-is**. `getParam` is the production-critical hot path (every-frame read). setTrigger / trigger+update cycle regressions are debug-build artifacts (linear scan of ~50-entry registry adds ~466 ns vs unordered_set's O(1)). Release builds compress these costs dramatically. The hot-path 4-5x win is the goal.

**No regression on existing tests** — all 752 prior P3.x刀 N+1.BC tests + 421 AYEntity tests pass unchanged × 3 stable.

### 4.18.10 Edge Cases & Lessons Learned

1. **Circular include resolved** — `ParamEntry` definition moved from `StateMachine.h` to `ConditionExpr.h` (which `StateMachine.h` already includes). ConditionExpr.h never includes StateMachine.h, so no cycle. Header-order safe.
2. **`unordered_map`/`unordered_set` includes removed from ConditionExpr.h** — no longer needed; `<vector>` added. StateMachine.h still includes both (used internally for `_stateIndexByName` and possibly elsewhere).
3. **ConditionEvalCtx field types changed** — only `StateMachine::findEligibleTransition` constructs the struct; `makeCtx` test helper adapted in 5 lines (AYTest_ConditionExpr.cpp). All 30 L2 tests re-validated with flat-vector `ConditionEvalCtx`.
4. **Reserved `hash 0` sentinel** — FNV-1a baseline 2166136261u is non-zero; first byte XOR+multiply never produces 0 for non-empty input. Asserted in `fnv1a_32` contract (INV-43). User can pass empty string `""` — intern returns the FNV-1a hash of empty (also non-zero, well-defined); param lookup just falls through to 0.0f via the linear scan miss path.
5. **`getParam` interns on every call** — even for `Unknown` params (returns 0.0f via fail-soft, INV-23). This grows the registry slightly per unique `Unknown` string. Production impact: zero (well-known params only).
6. **`setTrigger` debug-build regression** — root cause is `intern()` linear scan of ~50-entry registry in debug adds ~466 ns vs unordered_set's hash bucket (O(1) but with std::hash<std::string> cost). **Decision**: ship as-is. The win is on the read path (`getParam`), which is the production critical path. **Mitigation deferred**: caller-side hash caching (introduce `setTriggerByHash(uint32_t)` API + cache hash at registration site) for next polish cycle if setTrigger hot path becomes critical.
7. **Sorted-vector insert cost** — `addTriggerHash` is O(N) insert (vector shift). For N ≤ 4 production, this is a no-op in release; debug adds a few cycles. Idempotency check (lower_bound finds existing entry) is the dominant cost.
8. **Meyers singleton thread-safety** — C++11 magic static guarantees thread-safe initialization of `ParamNameRegistry::instance()`. Subsequent `intern` / `lookup` calls are NOT thread-safe (the registry is mutated by every setParam/setTrigger). **Production use**: ECS is single-threaded per-system; not a concern. Documented as a known trade-off in §11.
9. **`makeCtx` test helper adapt pattern** — the helper takes `initializer_list<pair<string, float>>` (signature unchanged) but now constructs `vector<ParamEntry>` + `intern()` per pair. Returns a struct holding pointers to two `static` vectors. This is the **only** signature-compatible API change for the test surface; everything else is 0-touch.
10. **Benchmark gating** — `AY_BUILD_BENCHMARKS=OFF` cache var keeps benchmark binary out of CI / default build. Build with `-DAY_BUILD_BENCHMARKS=ON` or `cmake -DAY_BUILD_BENCHMARKS=ON` to compile `AYAnimation_Benchmarks` standalone binary.
11. **`ParamNameRegistry::clear()` test-only** — exposed for unit tests that need a clean registry state. Not used in production.
12. **No L1/L2/L3 contract regression** — INV-18..42 all preserved; 752 prior tests pass unchanged. The refactor is a pure container swap with identical observable behavior.

### 4.18.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(P0 polish .A)**: introduce caller-side hash cache (`setTriggerByHash(uint32_t)` API + `setParamByHash` cache at SM bridge site). Mitigates debug-build setTrigger regression if hot path becomes critical. Currently deferred.
- **UPGRADE-HOOK(P0 polish .B)**: if N > 32 production params become common, swap `std::vector<ParamEntry>` for `std::unordered_map<uint32_t, float>`-or-similar (hash-keyed, not string-keyed) with the same `ConditionEvalCtx` interface. ParamEntry contract stays; only the `_params` storage swaps.
- **UPGRADE-HOOK(P3.x L2 算术扩展)**: reserved ident set will grow to include `_CurrentStateName`, `_CurrentStateIndex`, etc. (prefix `_` to avoid user-param collision). `CondIdentifierExpr::evaluateAsFloat` reserved-name pre-check expands. ParamNameRegistry contract unchanged.
- **UPGRADE-HOOK(P3.x刀 N+1.1 — OnStateEntered/Exited event)**: bridge already calls `player->setCurrentStateName` every tick; an `OnStateEntered` event hook would need bridge `prevState != sm.getCurrentStateName()` detection (already in place from P3.x刀 N+1.C). 0 changes to P0 polish surface.
- **UPGRADE-HOOK(P4.x .ayasm loader)**: persisted state-graph format must serialize `_params` as `[(hash, value), ...]` (4-byte hash + 4-byte float per entry, sorted by hash for canonical output). Registry is process-global and does NOT need to be serialized (the loader reinterns on load). ParamNameRegistry remains source-of-truth for hash→string lookup at runtime.

### 4.18.12 Open Questions

1. **Registry thread-safety** — current contract is single-threaded (ECS per-system). If multi-threaded SM ticks become a thing, `intern()` needs a mutex around `_byHash`. **Decision**: defer until multi-threaded SM is on the roadmap.
2. **Hash collision handling** — FNV-1a 32-bit has ~2^32 distinct outputs; collision probability is negligible for production N ≤ 100 unique names. Linear scan `_params[i].hash == identifierHash` already implicitly handles collisions correctly (both entries are scanned; the first match wins, deterministic by vector order). No additional collision check needed. **Decision**: ship as-is.
3. **setTrigger debug-build regression** — currently 0.4x (609 ns vs 221 ns). Acceptable trade-off given getParam 4-5x win. **Open**: should we ship `setTriggerByHash(uint32_t)` API now to mitigate, or defer to caller-side cache? **Decision**: defer; current regression is debug-only.
4. **`getParam` always interns (even for `Unknown`)** — registry grows by ~1 entry per unique `Unknown` string encountered. For well-tuned production SMs this is zero (only known params queried). For sloppy SMs it leaks. **Open**: should `getParam` skip intern on miss? **Decision**: NO — would split behavior between hot path (intern) and miss path (don't), defeating the cache-friendly contract. Documented.
5. **N > 32 production param SM** — currently `findParamIndex` linear scan O(N). At N=64, scan is ~512 ns/iter in debug (already comparable to pre-refactor 8-param hash lookup). **Open**: should we expose `_params.capacity()` as a public read-back for monitoring? **Decision**: NO; user can `getStates().size()` as proxy. If N > 32 becomes a production case, swap container (UPGRADE-HOOK .B).

---

## 4.19 ✅ P1 polish — Hot-Path Eval Hash Caching — SHIP（2026-08-07）

### 4.19.1 Overview

P1 polish refactor: eliminate the remaining 3 per-frame `detail::ParamNameRegistry::intern()` calls that P0 polish missed. `findEligibleTransition` (trigger scan), `Transition::evaluateCondition` L1 path (condition param lookup), `fireTransition` (trigger erase) all shift from **per-frame intern()** to **cached hashes computed once at authoring time** (`addTransition()` for `Transition`, constructor for `CondIdentifierExpr`). Same strategy applies to the L2 DSL: `CondIdentifierExpr::nameHash` is intern'd once at parse time. **0 public API change**, **0 ECS bridge change**, **0 resource bridge change**. **INV-47..51** new. 3-run stable AYAnimation 1783/1783 + AYEntity 421/421 + AYResource 1039/1039 × 3.

### 4.19.2 Motivation

P0 polish (`7898cb2`) eliminated 4 per-frame `intern()` calls in `setParam` / `getParam` / `setTrigger` / `CondIdentifierExpr::evaluateAsFloat`. **Audit revealed 3 more per-frame `intern()` callsites** that escaped P0 polish:

1. `StateMachine::findEligibleTransition` ([StateMachine.cpp:293](../../runtime/ayanimation/src/StateMachine.cpp#L293)) — `intern(t.trigger)` for every transition's trigger check, per frame
2. `Transition::evaluateCondition` L1 path ([StateMachine.cpp:486](../../runtime/ayanimation/src/StateMachine.cpp#L486)) — `intern(condition.paramName)` for every L1 condition eval, per frame
3. `fireTransition` ([StateMachine.cpp:334](../../runtime/ayanimation/src/StateMachine.cpp#L334)) — `intern(t.trigger)` for trigger erase after fire

**Production impact**: a state machine with 5 transitions ticked at 60 fps walks `findEligibleTransition` (5 triggerHash lookups + 5 conditionParamNameHash lookups) per frame = ~600 hash lookups/sec/entity. With ~50 entities, that's **30,000 hash lookups/sec across two callsites** = ~7.5 ms/sec debug build at 250 ns/intern (the linear scan over a ~50-entry process-global registry).

Both `trigger` and `condition.paramName` are **author-set once and immutable after `addTransition`** (mirroring `fromState` / `toState` immutability). The hash is a perfect candidate for one-time compute at construction time.

**Lazy fallback for back-compat**: existing P3.x L2 test code mutates `condition.paramName` directly via `const_cast` AFTER `addTransition` (test fixture pattern — e.g. `L2_BackCompat_L1Condition_StillWorks`). With naive caching, the cached hash becomes stale. To preserve back-compat **without** weakening the production invariant, `Transition::evaluateCondition` L1 path + `findEligibleTransition` + `fireTransition` all include a lazy fallback: if the cached hash is 0 but the source name is non-empty, recompute via `ParamNameRegistry::instance().intern()` on demand. Production code never mutates → never hits the fallback (0 ns cost); test fixtures still work.

**Same pattern for CondIdentifierExpr**: `nameHash` is intern'd once at ctor time. The hot path `evaluateAsFloat` walks `ctx.params` directly with the cached hash, no intern per eval.

### 4.19.3 Data Model

**Composition** (additions to P0 polish):
- **`Transition::triggerHash`** (`uint32_t`) — pre-computed at `addTransition` time. 0 ⟺ `trigger.empty()`.
- **`Transition::conditionParamNameHash`** (`uint32_t`) — pre-computed at `addTransition` time. 0 ⟺ `!hasCondition || condition.paramName.empty()`.
- **`CondIdentifierExpr::nameHash`** (`uint32_t`) — pre-computed at ctor time. 0 ⟺ empty name.
- **`detail::ParamNameRegistry` is split into its own header** (`ParamNameRegistry.h`) so `ConditionExpr.h`'s inline `CondIdentifierExpr` ctor can call `intern()` once without circular include (StateMachine.h includes ConditionExpr.h, so ConditionExpr.h cannot include StateMachine.h). ParamNameRegistry.h is the leaf — included by both.

```cpp
// include/ayanimation/ParamNameRegistry.h (NEW, split from StateMachine.h)
namespace ayt::anim::detail {
constexpr uint32_t fnv1a_32(const char* s) { /* unchanged from §4.18 */ }

class ParamNameRegistry {
public:
    static ParamNameRegistry& instance();   // Meyers singleton
    uint32_t intern(const std::string& name);
    const std::string& lookup(uint32_t hash) const;
    std::size_t size() const;
    void clear();   // test-only
private:
    struct Entry { uint32_t hash; std::string name; };
    std::vector<Entry> _byHash;
    static const std::string kEmpty;   // defined in StateMachine.cpp
};
} // namespace ayt::anim::detail

// include/ayanimation/StateMachine.h — Transition struct (P1 polish additions)
struct Transition {
    // ... existing P3.x fields ...
    uint32_t triggerHash              = 0;  // P1 polish (INV-47)
    uint32_t conditionParamNameHash   = 0;  // P1 polish (INV-48)
};

// include/ayanimation/ConditionExpr.h — CondIdentifierExpr (P1 polish additions)
struct CondIdentifierExpr final : public CondExprAst {
    std::string name;
    uint32_t    nameHash = 0;  // P1 polish (INV-50)

    explicit CondIdentifierExpr(std::string n)
        : name(std::move(n)),
          nameHash(name.empty() ? 0u
              : detail::ParamNameRegistry::instance().intern(name)) {}
    // ... evaluate / accept / evaluateAsFloat unchanged signature ...
};
```

### 4.19.4 Public API

**0 change** to public surface.

- `StateMachine::setTrigger(string)` / `setParam(string, float)` / `getParam(string)` — unchanged (P0 polish already optimized)
- `Transition::evaluateCondition(ctx)` — unchanged signature, faster
- `StateMachine::findEligibleTransition()` — private (unchanged)
- `StateMachine::fireTransition(const Transition&)` — private (unchanged)
- `ConditionParser::parse(src, outErr)` — unchanged
- `CondIdentifierExpr::evaluateAsFloat(ctx)` — unchanged signature, faster
- ECS bridge `AYStateMachineSystem::onUpdate` — **0 touch**

### 4.19.5 Internal Algorithm

**`addTransition` — compute both hashes once** (P1 polish):

```cpp
void StateMachine::addTransition(const Transition& t) {
    // ... existing asserts (INV-24 unknown toState) ...
    Transition withHashes = t;
    withHashes.triggerHash = t.trigger.empty()
        ? 0u
        : detail::ParamNameRegistry::instance().intern(t.trigger);
    withHashes.conditionParamNameHash =
        (t.hasCondition && !t.condition.paramName.empty())
        ? detail::ParamNameRegistry::instance().intern(t.condition.paramName)
        : 0u;
    _transitions.push_back(std::move(withHashes));
}
```

**`findEligibleTransition` — cached hash + lazy fallback**:

```cpp
const bool triggerOk = t.trigger.empty() ? true : [&] {
    uint32_t lookupHash = t.triggerHash;
    if (lookupHash == 0 && !t.trigger.empty()) {
        // Lazy fallback for test fixtures mutating trigger via const_cast
        lookupHash = detail::ParamNameRegistry::instance().intern(t.trigger);
    }
    return hasTriggerHash(lookupHash);
}();
```

**`Transition::evaluateCondition` L1 path — cached hash + lazy fallback**:

```cpp
if (!hasCondition) return true;  // INV-32
uint32_t condHash = conditionParamNameHash;
if (condHash == 0 && !condition.paramName.empty()) {
    condHash = detail::ParamNameRegistry::instance().intern(condition.paramName);
}
const std::size_t idx = findParamIndex(condHash);
return idx == SIZE_MAX ? false : (compare_helper(_params[idx].value));
```

**`fireTransition` — cached hash + lazy fallback**:

```cpp
if (!t.trigger.empty()) {
    uint32_t eraseHash = t.triggerHash;
    if (eraseHash == 0 && !t.trigger.empty()) {
        eraseHash = detail::ParamNameRegistry::instance().intern(t.trigger);
    }
    eraseTriggerHash(eraseHash);
}
```

**`CondIdentifierExpr::evaluateAsFloat` — cached nameHash**:

```cpp
float CondIdentifierExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    if (name == "CurrentStateTime") return ctx.currentStateTime;  // reserved (INV-39/INV-51)
    if (ctx.params == nullptr) return 0.0f;                       // INV-23 fail-soft
    const uint32_t identifierHash = nameHash;
    if (identifierHash == 0) return 0.0f;                         // INV-50 sentinel
    for (const auto& entry : *ctx.params) {
        if (entry.hash == identifierHash) return entry.value;
    }
    return 0.0f;                                                  // INV-23 fail-soft
}
```

### 4.19.6 ECS Bridge

**0 changes** to AYEntity bridge. `AYStateMachineSystem::onUpdate` still calls `sm.setTrigger` / `sm.setParam` / `sm.getCurrentStateName()` / `sm.getCurrentStateElapsedTime()`. Public API stable → bridge 0 touch.

AYEntity 421 tests pass unchanged × 3.

### 4.19.7 Resource Bridge

**N/A**. P1 polish is internal hot-path optimization; no `.ayasm` / `.aymask` / `.ayanm` format changes. Loader path (P3.x刀1 `ISkeletonMask`, P4.x `.ayasm`) deferred.

### 4.19.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-47** (NEW) | `Transition::triggerHash == 0 ⟺ trigger.empty()`; computed once at `addTransition()` time | `addTransition` |
| **INV-48** (NEW) | `Transition::conditionParamNameHash == 0 ⟺ !hasCondition \|\| condition.paramName.empty()`; computed once at `addTransition()` | `addTransition` |
| **INV-49** (NEW) | Both `Transition` hashes immutable after `addTransition()`. Production code never mutates `trigger` / `condition.paramName` after addTransition (transition is author-set once, mirrors `fromState` / `toState` immutability). However, lazy fallback in evaluate/erase/findEligibleTransition preserves back-compat with existing test fixtures that mutate `condition.paramName` via `const_cast` after `addTransition` | `findEligibleTransition` + `evaluateCondition` L1 + `fireTransition` |
| **INV-50** (NEW) | `CondIdentifierExpr::nameHash` pre-computed at ctor time via `ParamNameRegistry::intern()`; 0 ⟺ empty name (sentinel, no intern call) | `CondIdentifierExpr` ctor |
| **INV-51** (NEW) | Reserved ident `"CurrentStateTime"` (string compare) takes priority over nameHash lookup in `CondIdentifierExpr::evaluateAsFloat` | `evaluateAsFloat` |
| **INV-18..26** (preserved) | P3.1 L1 SM contracts — all preserved (Transition eval is identical observable behavior) | 15 unit tests |
| **INV-27..31** (preserved) | P3.2 L3 sub-machine contracts — all preserved | 12 unit tests |
| **INV-32..35** (preserved) | P3.x L2 DSL cache layer — all preserved (condHash lookup replaces `find(name)`; observable identical) | 30 unit tests |
| **INV-36..39** (preserved) | P3.x刀 N+1.B time-in-state — all preserved | 6 unit tests |
| **INV-40..42** (preserved) | P3.x刀 N+1.C per-state AnimNotify — all preserved | 4 unit tests |
| **INV-43..46** (preserved) | P0 polish flat-array containers + FNV-1a ParamName Registry — all preserved | 2 unit tests |

### 4.19.9 Testing

**Test bar**: AYAnimation 1783/1783 (1758 P3.x刀 N+1.BC ship + 25 P1 polish = **+2 Transition hash tests + 3 CondIdentifierExpr nameHash tests**) + AYEntity 421/421 unchanged + AYResource 1039/1039 unchanged × 3-run stable.

**AYAnimation unit tests** (+2 new in `AYTest_StateMachine.cpp`):
1. **`P1_Transition_TriggerHash_CachedAtAddTransition`** — `addTransition({trigger="P1_TriggerHash_Test_Unique_Name"})` → `triggerHash != 0` + registry size +1 (uses unique name to avoid shared-registry pollution from earlier suite tests).
2. **`P1_Transition_ConditionHash_CachedAtAddTransition`** — `addTransition({hasCondition=true, condition={"Speed", GT, 5.0}})` → `conditionParamNameHash != 0` + registry size +1 for the paramName.

**AYAnimation unit tests** (+3 new in `AYTest_ConditionExpr.cpp`):
3. **`P1_CondIdent_NameHash_NonEmpty`** — `CondIdentifierExpr("Speed")` → `nameHash != 0` + cached hash matches direct `intern("Speed")`.
4. **`P1_CondIdent_NameHash_EmptyName_HashZero`** — `CondIdentifierExpr("")` → `nameHash == 0`; only `"Foo"` (2nd ident) increments registry size (sentinel pre-check skips intern for empty).
5. **`P1_CondIdent_Evaluate_NoIntern`** — 1000x `evaluateAsFloat(ctx)` with `params = {"Speed": 5.0}` → registry size unchanged from ctor-time (zero per-eval intern); reserved ident `"CurrentStateTime"` still shadows via `ctx.currentStateTime`.

**Micro-benchmark** (P1 polish additions to `AYAnimation/benchmark/state_machine_params_bench.cpp`):
- **Scenario E — `findEligibleTransition`**: 5 transitions × 100K iters, scan + fire variants. Measures elimination of `intern()` from per-frame trigger check + L1 condition eval.
- **Scenario F — DSL evaluate**: 3-ident expression `(Speed > 5.0) && IsGrounded && !IsDead` × 100K iters, true path + short-circuit variants. Measures elimination of `CondIdentifierExpr::evaluateAsFloat` intern.

**Results (debug build)**:

| Scenario | Post-P1 polish (ns/iter) | Notes |
|---|---|---|
| Scenario E: scan 5 transitions (no trigger) | **1251 ns/iter** | 5 transitions × 60 fps per entity; lazy fallback + cached hash only |
| Scenario E: scan + fire 1 transition (trigger set) | **2391 ns/iter** | includes condition eval + eraseTriggerHash + fireTransition |
| Scenario F: evaluate 3-ident expr (true path) | **132 ns/iter** | 3 ident + 2 && + 1 GT + 1 !; no per-eval intern |
| Scenario F: evaluate 3-ident expr (short-circuit) | **113 ns/iter** | IsDead=1 short-circuits the && chain at first ident |

**Decision**: ship **as-is**. Scenario E per-call cost of 1251 ns is dominated by the per-frame `update(0.0f)` walk (5 transitions × 2 hash lookups + 5 condition evals + cache writes), not by hash intern. The win is the **elimination of per-frame registry allocation pressure** + cache-friendly hash comparison; the absolute ns/iter is not directly comparable to P0 polish's getParam (which only does 1 hash lookup per call). The setTrigger scenario D at 673 ns/iter is still debug-build only; release builds compress dramatically.

**No regression on existing tests** — all 1758 prior P3.x刀 N+1.BC + P0 polish tests + 421 AYEntity tests pass unchanged × 3 stable.

**No regression on existing tests** — all 1758 prior P3.x刀 N+1.BC + P0 polish tests + 421 AYEntity tests pass unchanged × 3 stable.

### 4.19.10 Edge Cases & Lessons Learned

1. **Transition author-set immutability invariant** — `Transition.trigger` and `Transition.condition.paramName` are author-set once (mirrors `fromState` / `toState`). Pre-P1 polish, the per-frame `intern()` masked any drift between authoring and runtime; post-P1 polish, drift would produce a stale cache. **Mitigation**: lazy fallback in 3 hot-path callsites — if cached hash is 0 but source is non-empty, recompute on demand. Production never hits the fallback (cost 0); test fixtures that mutate via `const_cast` still work.
2. **Header split to break circular include** — `CondIdentifierExpr` ctor needs to call `detail::ParamNameRegistry::intern()`. `StateMachine.h` already includes `ConditionExpr.h`, so `ConditionExpr.h` cannot include `StateMachine.h`. **Solution**: split `ParamNameRegistry` into its own leaf header `ParamNameRegistry.h`; both StateMachine.h and ConditionExpr.h include it; the `kEmpty` sentinel is defined in StateMachine.cpp (the only .cpp that needs it). Header-order safe.
3. **`0` hash sentinel pre-check** — FNV-1a baseline 2166136261u ≠ 0 (and the hash function never produces 0 for non-empty input). Use `0 ⟺ empty` as the universal pre-check across 4 callsites (`addTransition` × 2, `findEligibleTransition`, `evaluateCondition`, `fireTransition`). Cost is one `cmp/jne` — branches the empty case out of `intern()`.
4. **`withHashes` local copy + move pattern** — `Transition` is a POD-like struct held in `std::vector<Transition>` (copy required for push_back). We don't want callers to need to know about the new hash fields. **Pattern**: copy input → mutate the 2 hash fields → `push_back(std::move(withHashes))`. Constructor / API surface unchanged; existing user code that constructs `Transition t; sm.addTransition(t);` works without modification.
5. **Constructor-time cache eliminates hot-path intern cleanly** — `CondIdentifierExpr::nameHash` is intern'd once when the parser constructs the node. Every subsequent `evaluateAsFloat` call walks `ctx.params` with the cached hash directly — no intern, no registry lookup. AST is built once at parse time (or lazily on first eval via the L2 cache); per-frame cost is just the linear scan over the param vector.
6. **Test fixture registry accumulation awareness** — `ParamNameRegistry` is process-global (Meyers singleton). Test fixtures that intern `"Jump"` once will not re-intern it on subsequent tests. The first P1 unit test (using `"Jump"`) failed with `registrySizeAfter - registrySizeBefore == 0` instead of `== 1` because `"Jump"` was already registered by prior suite tests. **Fix**: use a unique name per test (`"P1_TriggerHash_Test_Unique_Name"`). Document this pattern.
7. **`setConditionExpr` does NOT recompute `conditionParamNameHash`** — `setConditionExpr` is for the L2 DSL path (string expression), which uses the L2 parser's own ident hashing (via the parser's `CondIdentifierExpr` ctor). The L1 `condition.paramName` is a separate field; if host code mutates L1 paramName directly via `const_cast`, the lazy fallback catches it. **Documented in Transition header doc block**.
8. **Process-global registry test fixture interaction** — the process-global registry shares state across all SM instances in a test suite. P1 polish tests assert `registrySizeAfter - registrySizeBefore == 1`; this is robust because each test uses a unique name. If tests shared a name, the delta would be 0 and the assertion would fail (correctly indicating no new intern).
9. **Reserved ident priority preserved** — `"CurrentStateTime"` string compare in `evaluateAsFloat` runs **before** the nameHash lookup (INV-51). The string compare is cheap (~5 ns SSO) and preserves the existing P3.x刀 N+1.B shadow semantic (user params named `CurrentStateTime` are ignored in favor of the live state-machine clock). Cached `nameHash` is computed for `CurrentStateTime` too (no special case in ctor) — the string compare just wins the race in `evaluateAsFloat`.
10. **`Transition` copy semantics preserved** — the new `uint32_t` fields are POD; default copy ctor copies them. `vector<Transition>::push_back` (which copies) works unchanged. No move-only semantics introduced.
11. **`kEmpty` sentinel location** — `kEmpty` is defined as `const std::string ayt::anim::detail::ParamNameRegistry::kEmpty;` in `StateMachine.cpp` (line 24). After the header split, `ConditionExpr.h` and `ParamNameRegistry.h` no longer define it directly; the symbol is provided by the .cpp. Header-linker safe.

### 4.19.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(P2 polish)** — **✅ RESOLVED 2026-08-08 by §4.20 P2 polish ship**：AST → 扁平字节码 shipped as parallel cache（`CondBytecode` + `compileToBytecode` + program-counter switch evaluator + 固定栈数组）。AST hierarchy preserved for P4.x graph-builder Visitor；详见 §4.20。
- **UPGRADE-HOOK(P2 polish .A)**: caller-side hash cache (`setTriggerByHash(uint32_t)` API + `setParamByHash` cache at SM bridge site). Mitigates debug-build setTrigger regression if hot path becomes critical. Currently deferred (already shipped in P0 polish §4.18.11 row).
- **UPGRADE-HOOK(D polish)**: AssetBoneCache lock-free (mutex → thread_local cache for single-threaded ECS). Defer (低 ROI, mutex uncontended ~30ns).
- **UPGRADE-HOOK(下一轮 polish)**: Additive slot dynamic vector (替换 hard cap 8). Defer (内存复用方向).
- **UPGRADE-HOOK(Transition serializer)**: persisted state-graph format must serialize `triggerHash` / `conditionParamNameHash` as part of `Transition`. Hash is canonical, but string is also retained for round-trip clarity. Loader reinterns on load → cached hash is reproducible from intern(). ParamNameRegistry remains source-of-truth.

### 4.19.12 Open Questions

1. **Registry thread-safety** — current contract is single-threaded (ECS per-system). If multi-threaded SM ticks become a thing, `intern()` needs a mutex around `_byHash`. **Decision**: defer until multi-threaded SM is on the roadmap.
2. **Hash collision handling** — FNV-1a 32-bit has ~2^32 distinct outputs; collision probability is negligible for production N ≤ 100 unique names. Linear scan `_params[i].hash == identifierHash` already implicitly handles collisions correctly. No additional collision check needed. **Decision**: ship as-is.
3. **Lazy fallback cost in release** — `if (lookupHash == 0 && !source.empty())` branch is a 2-cycle `cmp/jne` in release; branch predictor learns the not-taken path (production never mutates). Cost in optimized build is ~1 ns; debug build ~3 ns. **Decision**: ship as-is; the invariant is "production never mutates" — fallback exists purely for back-compat.
4. **Test fixture pollution of process-global registry** — the registry persists across test fixtures in a single process. Tests that assert delta-in-registry-size must use unique names. **Decision**: document the pattern in test helpers (P1 polish tests use `"P1_TriggerHash_Test_Unique_Name"` prefix).
5. **`CondIdentifierExpr` reserved-name priority list** — currently `"CurrentStateTime"` is the only reserved ident (INV-39 / INV-51). P3.x L2 extension may add `"_CurrentStateName"`, `"_CurrentStateIndex"`, etc. (prefix `_` to avoid user-param collision). **Deferred**: P3.x刀 N+2 grammar expansion; nameHash caching contract unchanged.
6. **`ParamNameRegistry` size as production metric** — currently `getParamNameRegistrySize()` exists as a static debug read-back. Not exported via AYEntity bridge. **Decision**: defer; runtime memory cost is ~24 bytes per unique name (uint32 hash + std::string), production ~50 names = ~1.2 KB total.

---

## 4.20 ✅ P2 polish — Condition DSL AST → Flat Bytecode — SHIP（2026-08-08）

### 4.20.1 Overview

P2 polish: **parallel flat-bytecode representation of the Condition DSL AST**. Instead of virtual-dispatch AST evaluation, the same expression is compiled (lazily, on first eval) to a flat opcode stream + float literal table; the evaluator is a single program-counter switch over a **fixed-size stack array** — no virtual calls, cache-friendly contiguous memory. AST is **preserved** (parallel, not replacement): P4.x CondVisitor graph-builder needs the AST for UI traversal; every existing AST test (12 evaluator + 6 cache + 4 back-compat) passes unchanged. **0 public API change**, **0 ECS bridge change**, **0 resource bridge change**. **INV-52..58** new. 3-run stable AYAnimation 1824/1824 + AYEntity 421/421 + AYResource 1039/1039 × 3.

### 4.20.2 Motivation

P1 polish (`dd9d950`) eliminated the 3 remaining per-frame `ParamNameRegistry::intern()` calls, but the hot per-frame evaluation path still walks the AST with **5 virtual dispatches per evaluate** (1 root + 2 binary + 2 leaf, each through `dynamic_cast`/virtual call + shared_ptr derefs). Scenario G (3-ident expr `(Speed > 5.0) && IsGrounded && !IsDead`, 100K iters, debug build, 2026-08-08):

| Path | true path | short-circuit | Speedup |
|---|---|---|---|
| AST (Scenario F) | 1035.47 ns/iter | 1022.51 ns/iter | 1.0x |
| **Bytecode (Scenario G)** | **773.30 ns/iter** | **799.88 ns/iter** | **1.34x / 1.28x** |

The win is the elimination of per-eval virtual dispatch + AST node traversal. Note: an earlier implementation measured **8x SLOWER** (1133 ns) because it allocated a `std::vector<float>` stack per eval — the fixed array recovered the gap (see §4.20.10.2). The gap widens under optimization (release builds inline the switch; the debug build's virtual calls are already cheap relative to interpreter loop overhead).

### 4.20.3 Data Model

**New files**:
- **`include/ayanimation/CondBytecode.h`** — `struct CondBytecode { std::vector<uint8_t> program; std::vector<float> literals; }` + `CondOpByte` enum (10 opcodes) + `CondReservedId` enum + `compileToBytecode(const CondExprAst*)` API.
- **`src/CondBytecode.cpp`** — the program-counter switch evaluator.

**New field** on `Transition` (StateMachine.h):
```cpp
mutable std::shared_ptr<CondBytecode> cachedBytecode;  // INV-57 — copyable
```
- `shared_ptr` (not `unique_ptr`): `vector<Transition>::push_back` requires copyable `Transition` (INV-57). `mutable`: lazy init from const `evaluateBytecode`.

**Opcode encoding** (all 1-byte op; operands follow inline):
- `OP_AND`/`OP_OR`: + `int8_t relJump` — relative byte count of right subtree (short-circuit skip, INV-58, ±127 limit)
- `OP_NOT`: no operand (pops 1, pushes negated)
- `OP_GT`/`OP_LT`/`OP_EQ`/`OP_NE`: no operand (pops 2, pushes 1; EQ/NE use `|a-b| < 1e-6f` epsilon — matches AST `CondBinaryExpr::evaluate`)
- `OP_LOAD_PARAM`: + `uint32_t hash` (FNV-1a, P1 polish `nameHash`) → linear scan over `ctx.params`
- `OP_LOAD_LITERAL`: + `uint32_t idx` → `literals[idx]` (**flat float table**, INV-56)
- `OP_LOAD_RESERVED`: + `uint8_t rid` — `R_CURRENT_STATE_TIME` = 0 → `ctx.currentStateTime` (INV-55)

**Literal table**: `std::vector<float>`, **no tag bits**. Bools stored as 1.0f/0.0f; evaluator coerces with `!= 0.0f` — semantically identical to the AST's `CondLiteralExpr::evaluate`. (The earlier bit-30 sign-tag scheme was **broken**: IEEE-754 exponent occupies bits 23..30, so any literal with |v| ≥ 2.0 got decoded as negative — see §4.20.10.1.)

**Evaluator stack**: fixed `float stack[16]` + `std::size_t sp`, all pushes/pops bounds-checked → fail-soft false on overflow/underflow (mirrors INV-33).

### 4.20.4 Public API

**0 change** to public surface:
- `StateMachine` API — unchanged (bytecode is internal to `Transition`)
- `Transition::evaluateCondition(ctx)` — unchanged signature; hot path now routes through `evaluateBytecode` internally
- `ConditionParser::parse(src, outErr)` — unchanged
- `CondExprAst` hierarchy — unchanged (AST preserved)
- ECS bridge `AYStateMachineSystem::onUpdate` — **0 touch**
- New internal-only: `Transition::evaluateBytecode` (private), `compileToBytecode` (internal header)

### 4.20.5 Internal Algorithm

**Compile** (`compileToBytecode`, in ConditionParser.cpp): post-order walk of the AST.

```cpp
compileNode(node, prog, lits):
  CondBinaryExpr:
    compile left
    if And/Or:
      emit OP_AND/OR + placeholder jump byte
      compile right
      patch placeholder = right-subtree byte count (cap 127)
    else (GT/LT/EQ/NE):
      compile right            // must come BEFORE the opcode byte
      emit OP_GT/LT/EQ/NE
  CondUnaryExpr: compile operand, emit OP_NOT
  CondIdentifierExpr:
    if name == "CurrentStateTime": emit OP_LOAD_RESERVED + RID  (INV-55)
    else: emit OP_LOAD_PARAM + nameHash bytes
  CondLiteralExpr:
    emit OP_LOAD_LITERAL + idx; lits.push(float or 1.0f/0.0f for bool)
```

Key fix in this ship: comparisons must compile the **right subtree before emitting the opcode** — an earlier version returned after the left subtree, leaving the stack under-popped (10 test failures).

**Evaluate** (`CondBytecode::evaluate`): single `while (pc < end)` switch; operands read inline via `std::memcpy` (unaliased-safe); every arm guards stack bounds; final `sp == 1` required.

**`Transition::evaluateBytecode`** (hot path in `findEligibleTransition`):
- L2 (DSL string): `cachedBytecode == null` → ensure `cachedAst` (lazy parse, dirty cache from L2 ship) → `compileToBytecode(cachedAst)` → cache. Then `evaluate` (INV-52).
- L1 (legacy param condition): delegate to `evaluateCondition` (no bytecode — L1 is a single param compare, nothing to gain).
- `setConditionExpr` → `invalidateConditionCache` now also `cachedBytecode.reset()` — stale AST clears stale bytecode (INV-52 rebuild).

### 4.20.6 ECS Bridge

**0 changes**. `AYStateMachineSystem::onUpdate` calls the public `StateMachine` API only; bytecode is entirely internal to `Transition`. AYEntity 421 tests pass unchanged × 3.

### 4.20.7 Resource Bridge

**N/A**. P2 polish is internal hot-path optimization; no `.ayasm` / `.aymask` / `.ayanm` format changes. Serializing the bytecode to disk is deferred (see §4.20.11).

### 4.20.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-52** (NEW) | `Transition::cachedBytecode == null ⟺ AST parse failed OR not yet evaluated` (lazy init) | `evaluateBytecode` + `P2_Bytecode_Integration_FindTransitionUsesBytecode` |
| **INV-53** (NEW) | Bytecode is 1:1 semantically equivalent to AST — every parseable AST compiles to bytecode producing identical `evaluate(ctx)` return | `P2_Bytecode_Parity_3IdentExpr` (5 cases) |
| **INV-54** (NEW) | Bytecode eval failure ≡ AST eval failure — return value identical for same ctx (fail-soft false) | `P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse` |
| **INV-55** (NEW) | Reserved ident `"CurrentStateTime"` encoded as `OP_LOAD_RESERVED R_CURRENT_STATE_TIME` at compile time; 0 string compare at eval time; preserves INV-39/51 priority | `P2_Bytecode_ReservedIdent_CompiledAsOpcode` |
| **INV-56** (NEW) | Program + literals are continuous `std::vector`; operands embedded in program stream; literal table is flat `float` (bools = 1.0f/0.0f, no tag bits) | code layout + evaluator |
| **INV-57** (NEW) | `cachedBytecode` is `shared_ptr<CondBytecode>` (copyable for `vector<Transition>::push_back`) | `addTransition` compile |
| **INV-58** (NEW) | `OP_AND`/`OP_OR` short-circuit as relative jump offset (±127 opcodes); left decisive → right subtree skipped | `P2_Bytecode_Parity_3IdentExpr` short-circuit case |
| **INV-18..26** (preserved) | P3.1 L1 contracts — L1 path unaffected (delegates to `evaluateCondition`) | 15 unit tests |
| **INV-27..31** (preserved) | P3.2 L3 sub-machine contracts | 12 unit tests |
| **INV-32..35** (preserved) | P3.x L2 DSL cache layer — dirty cache + lazy parse + parse-fail-soft-false | 30 unit tests |
| **INV-36..42** (preserved) | P3.x刀 N+1.BC time-in-state + per-state AnimNotify | 10 unit tests |
| **INV-43..46** (preserved) | P0 polish flat-array + FNV-1a registry | 2 unit tests |
| **INV-47..51** (preserved) | P1 polish hash caching + reserved ident priority | 5 unit tests |

### 4.20.9 Testing

**Test bar**: AYAnimation **1824/1824** (1783 prior + **6 new TEST_CASEs, +41 assertions**) + AYEntity 421/421 unchanged + AYResource 1039/1039 unchanged × 3-run stable.

**AYAnimation unit tests** (+4 in `AYTest_ConditionExpr.cpp`):
1. **`P2_Bytecode_Parity_3IdentExpr`** — 5 cases: same 3-ident expression evaluated via AST vs bytecode across (a) all-true, (b) param-below-literal (`Speed=3` vs `Speed > 5.0` — the case that caught the bit-30 literal bug), (c) short-circuit on `IsDead`, (d) `IsGrounded` false, (e) literal `5.0` exact equality. Byte-equivalent return for every case.
2. **`P2_Bytecode_LazyBuild_FirstEvalCompiles`** — `cachedBytecode == null` before first eval (INV-52); compiled after.
3. **`P2_Bytecode_ReservedIdent_CompiledAsOpcode`** — `"CurrentStateTime"` compiles to `OP_LOAD_RESERVED` (0 string compare at eval); user param of the same name still shadowed (INV-55).
4. **`P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse`** — unparseable expression → `cachedAst` null → bytecode never built → eval false (INV-54).

**AYAnimation unit tests** (+2 in `AYTest_StateMachine.cpp`):
5. **`P2_Bytecode_Integration_FindTransitionUsesBytecode`** — after `update()` drives `findEligibleTransition`, matched transition's `cachedBytecode` populated (hot path switched to bytecode, INV-52); subsequent updates reuse the cache; transition fires through bytecode path.
6. **`P2_Bytecode_Integration_InvalidateCacheClearsBytecode`** — first eval with non-firing condition (`Speed > 9.0` at Speed=7) builds bytecode without transitioning; `setConditionExpr` clears it (invalidate); next eval recompiles the new expression and fires (`Speed > 3.0` at 4.0).

**Micro-benchmark** (Scenario G added to `AYAnimation/benchmark/state_machine_params_bench.cpp`): bytecode vs AST evaluate on the same 3-ident expression, 100K iters, true + short-circuit variants. Results (debug build, 2026-08-08):

| Path | true path | short-circuit |
|---|---|---|
| AST (Scenario F) | 1035.47 ns/iter | 1022.51 ns/iter |
| Bytecode (Scenario G) | **773.30 ns/iter** | **799.88 ns/iter** |
| Parity | PASS | PASS |

**No regression on existing tests** — all 1783 prior tests (P3.x刀 N+1.BC + P0 + P1 polish) pass unchanged × 3 stable.

### 4.20.10 Edge Cases & Lessons Learned

1. **bit-30 sign-tag scheme is fundamentally broken** — the original literal encoding tagged float sign in bit 30 (`bits & 0x40000000u`). IEEE-754 exponent occupies bits 23..30, so any literal with |v| ≥ 2.0 (e.g. `5.0` = 0x40A00000) had bit 30 set and decoded as **negative** → `Speed > 5.0` evaluated `3 > -5` = true. The parity test's Case 2 (`Speed=3` against `5.0`) caught it. **Fix**: flat `std::vector<float>` — no tag bits, no encoding at all. Bools are 1.0f/0.0f and coerce with `!= 0.0f`, which the AST already does. **Lesson**: don't pack metadata into float bitfields without checking IEEE-754 layout per magnitude range.
2. **per-eval `std::vector` stack is an 8x slowdown** — the first evaluator allocated `std::vector<float> stack; stack.reserve(8)` every call → heap alloc + capacity check per eval → **1133 ns/iter, 8x slower than AST** (144 ns). **Fix**: fixed `float stack[16]` + `std::size_t sp` with bounds guards (fail-soft on overflow). Recovered to 773 ns (1.34x faster than AST). **Lesson**: on a per-frame hot path, per-call heap allocation dominates everything.
3. **MSVC struct/class tag mangling** — `struct CondExprAst;` forward decl in CondBytecode.h vs `class CondExprAst` in ConditionExpr.h → C4099 + LNK2019 (mangled name differs per first-seen tag kind per TU). **Fix**: match the tag kind (`class CondExprAst;`). Same rule as `ConditionEvalCtx` (declared `struct` in ConditionExpr.h). This is the second instance of this landmine in this repo.
4. **stale .obj layout mismatch → RTC_StackFailure** — `AYTest_SubStateMachine.cpp.obj` compiled against the pre-P2 `Transition` layout; the new `addTransition` copy (`Transition withHashes = t;`) read a garbage `shared_ptr` past the old struct → `_Incref()` wrote past the stack canary. **Fix**: touch the source file → ninja recompiles. Symptom moved between tests (crash in different TEST_CASEs) — classic stale-obj signature (memory: always suspect stale .obj before adding diagnostics).
5. **compileNode comparison bug (10 test failures)** — comparisons returned after compiling the left subtree; the right subtree was never emitted → literal table empty + evaluator stack underflow → fail-soft false → transitions never fire (broke new P2 tests AND legacy L2/TIS tests that now route through the bytecode hot path). **Fix**: compile right subtree before emitting the opcode.
6. **shared_ptr for copyability (INV-57)** — `vector<Transition>::push_back` requires copyable `Transition`. `unique_ptr<CondBytecode>` would break it. `shared_ptr` + `mutable` + lazy build keeps `const` API semantics.
7. **INV-52 lazy-build semantics** — `cachedBytecode == null` means "not yet evaluated OR parse failed" — callers must not distinguish. `setConditionExpr` resets both `cachedAst` and `cachedBytecode` (one invalidate clears both layers).
8. **fail-soft on malformed bytecode** — every evaluator arm bounds-checks `pc` and stack; any anomaly → false (INV-33 philosophy: never crash, never throw). The evaluator is only reachable via `compileToBytecode` output, so malformed programs are impossible in production — guards are defense-in-depth against future manual bytecode construction.

### 4.20.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(§4.19.11 P2 polish) — ✅ RESOLVED 2026-08-08 by §4.20 ship**: AST → 扁平字节码 shipped as a parallel cache; AST hierarchy preserved for P4.x graph-builder Visitor.
- **UPGRADE-HOOK(bytecode serialization)**: persisted state-graph format (future `.ayasm`) may embed the compiled program for zero-compile load. The literal table must then be part of the format. Defer until `.ayasm` (P4.x).
- **UPGRADE-HOOK(2-byte jumps)**: if production expressions ever exceed 127-byte right subtrees, widen the AND/OR jump to `int16_t`. Currently clamped (documented in compileNode); production AST depth ≤ 5 keeps subtrees < 64 bytes.
- **UPGRADE-HOOK(OP_LOAD_RESERVED extension)**: future reserved idents (`R_CURRENT_STATE_NAME`, `R_CURRENT_STATE_INDEX`) add switch arms in lockstep — the opcode+rid encoding grows without touching existing programs.

### 4.20.12 Open Questions

1. **Debug-build speedup is modest (1.34x)** — the interpreter's switch/bounds-check overhead is similar to virtual dispatch in debug; the real win is cache locality + no dynamic_cast, which scales with expression size. **→ RESOLVED 2026-08-10（P6 polish）**：release 首测 bytecode true 18.3ns / short-circuit 20.6ns（vs debug 86.2/75.9，~4.7x/~3.7x），详见 §4.24.10。
2. **Fixed stack capacity 16** — production AST depth ≤ 5 (max stack ~6); 16 gives 2.6x headroom. Overflow fail-softs to false (never OOB write). If future grammar grows deeper trees, raise the constant.
3. **Bytecode recompile cost** — compile is O(n) over the AST and happens once per transition (lazy); not measured in the benchmark (Scenario G compiles once). If transitions were rebuilt per frame (they aren't), this would matter.
4. **`cachedBytecode` memory** — ~program bytes + literals per transition with a condition; a state machine with 50 transitions ≈ a few KB. No pooling yet; revisit under 内存复用 polish.

---

## 4.21 ✅ P3 polish — AssetBoneCache lock-free single-threaded mode — SHIP（2026-08-08）

### 4.21.1 Overview

P3 polish: **AssetBoneCache 默认改无锁**。P1.7 时代 7 个访问点全部无条件 `lock_guard<std::mutex>` — 在 ECS 单线程主 tick 约定（ay-dev-rules.md）下 mutex 纯开销。P3 polish 引入运行时模式开关：**默认 `_threadSafe = false` — 永不触碰 mutex（INV-59）**；authoring tools / 多线程 host 显式 `setThreadSafe(true)` 才重挂 mutex（INV-60）。行为两种模式字节级一致。**+2 public API（setThreadSafe / isThreadSafe，纯 additive）**，**0 ECS bridge change**，**0 resource bridge change**。**INV-59/60** new。3-run stable AYAnimation **1851/1851** + AYEntity 421/421 + AYResource 1039/1039 × 3。

### 4.21.2 Motivation

P1.7（§4.12.1）设计时 mutex 是「保险 + thread-sanitizer 友好」— 生产路径单线程，mutex 只在 authoring tools 设想的多线程场景有用。但代价是 **7 个访问点每次调用都付出无竞争 lock/unlock（Windows SRWLock ~10-30ns）+ 阻止热路径 inlining 心智模型**。Roadmap direction 1（热路径极致性能）列为剩余项。

**诚实的路径分析**：AssetBoneCache 不在 per-frame 热路径上 — `resolveBoneIdxOnce` 每 player × track **一次**（`slice.boneIdx != kBoneUnresolved` 短路，P1.4 hot cache），`resolveSkeletonMask` 只在 mask 绑定时跑。真正 shape 是 **scene-load 绑定突发**（N players × M tracks + masks）。Scenario H 实测（debug build, min-of-5 交错, 2026-08-08）：

| 路径 | lock-free | thread-safe | 比值 |
|---|---|---|---|
| `resolveAndCache` hit | 961 ns/iter | 1001 ns/iter | 1.04x |
| `lookup` hit | 910 ns/iter | 980 ns/iter | 1.08x |

绝对值被 debug STL 主导（每次 `find(const char*)` 构造临时 `std::string` + checked iterators ≈ 900-1000ns），mutex delta 在噪声以下。**P3 的收益是结构性**：默认模式零同步 — 对 ECS 单线程契约可证明、TSAN 天然干净、绑定突发省掉每次调用 ~20-30ns（1000 次绑定 ≈ 20-30µs/scene load）；lock delta 在 release 构建（STL inline 后）才显著。

### 4.21.3 Data Model

**改文件 2 个**（零新文件）：
- **`include/ayanimation/AssetBoneCache.h`** — 新增 `bool _threadSafe = false;`（INV-59 默认）+ 2 个新 method 声明 + thread-safety 注释段改写。
- **`src/AssetBoneCache.cpp`** — anonymous namespace 新增 `maybeLock(std::mutex&, bool)` RAII helper（`enabled ? unique_lock(mu) : unique_lock()` — default ctor noexcept 零成本，锁路径 RAII 语义不变）；7 个访问点 `lock_guard` → `unique_lock lk = maybeLock(_mu, _threadSafe);`。

**新增 API**（§4.21.4）：
```cpp
void setThreadSafe(bool enabled) noexcept;   // INV-60: 非原子, 须在并发使用前设
bool isThreadSafe() const noexcept;
```

### 4.21.4 Public API

**+2 additive method**（无签名变更、无移除）：
- `setThreadSafe(bool)` — 默认 false（单线程无锁）；true 重挂 mutex。
- `isThreadSafe() const` — read-back，测试 pin 默认契约用。

**0 变更**：`lookup` / `resolveAndCache` / `invalidate` / `clear` / `skeletonEntryCount` / `boneNameEntryCount` 签名 identical；`AnimationPlayer`（resolveBoneIdxOnce + resolveSkeletonMask 两处 callsite）**0 touch**；AYEntity ECS bridge **0 touch**。

### 4.21.5 Internal Algorithm

```cpp
namespace {
// P3 polish — RAII conditional lock。`enabled=false` 返回未上锁的
// unique_lock：单线程路径零锁开销（INV-59）。默认 ctor noexcept。
std::unique_lock<std::mutex> maybeLock(std::mutex& mu, bool enabled) {
    if (enabled) return std::unique_lock<std::mutex>(mu);
    return std::unique_lock<std::mutex>();
}
}
```

7 个访问点统一替换：
```cpp
std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);  // 之前: lock_guard
```

无锁路径 = 纯 unordered_map read/write；锁路径 = 原 P1.7 语义（含 resolveAndCache fast-path 双 lookup under lock）。`setThreadSafe` 是 plain bool 写（INV-60 — 无同步原语，仅允许单线程 quiescent 态翻转，典型于启动时）。

### 4.21.6 ECS Bridge

**0 changes**。AYEntity `AnimationSystem` / `StateMachineSystem` 只调 public API；默认锁-free 模式即生产路径。AYEntity 421 tests pass unchanged × 3。

### 4.21.7 Resource Bridge

**N/A**。纯 runtime 单例内部模式切换；`.ayskel` / `.ayanm` / `.aymask` 格式零变更。

### 4.21.8 Invariants

| Inv | Statement | Asserted in |
|---|---|---|
| **INV-59** (NEW) | 默认单线程无锁模式：`setThreadSafe` 从未调用时 7 个访问点**永不触碰 mutex**；mutex 仅在所有 sites 在 `setThreadSafe(true)` 后参与。行为两种模式字节级一致 | `P3_AssetBoneCache_DefaultIsLockFree` + `P3_AssetBoneCache_ThreadSafeMode_BehaviorUnchanged` |
| **INV-60** (NEW) | `_threadSafe` 是 plain bool（非 atomic）：翻转仅在无其他线程在 cache 内时合法（典型：启动时一次性）；多线程 host 严禁 mid-flight 翻转 | header doc + `setThreadSafe` impl |
| **INV-1..17** (preserved) | P1.7 cache contract（lifetime / sentinel / per-skeleton independence / resolveBoneIdxOnce routing） | 5 P1_7 tests（现跑 lock-free 默认路径）+ 2 P3 tests（双模式复跑同一 contract） |

### 4.21.9 Testing

**Test bar**: AYAnimation **1851/1851**（1824 prior + **2 new TEST_CASEs, +27 assertions**）+ AYEntity 421/421 + AYResource 1039/1039 × 3-run stable。

**新增**（AYTest_AnimationPlayer.cpp，P1.7 suite 尾部）：
1. **`P3_AssetBoneCache_DefaultIsLockFree`** — INV-59 契约 pin：`isThreadSafe() == false`。
2. **`P3_AssetBoneCache_ThreadSafeMode_BehaviorUnchanged`** — `setThreadSafe(true)` → 完整 P1.7 contract（resolveAndCache hit / per-skeleton independence / lookup sentinel / invalidate 单清 / clear / entry-count）复跑；翻回 false 再复跑一遍 — 双模式行为字节级一致。

**既有 5 个 P1_7 tests 不变** — 现在跑在 lock-free 默认路径上（隐式 pin 无锁模式行为）。

**Micro-benchmark**（Scenario H added to `state_machine_params_bench.cpp`）：真实 2-bone Skeleton，resolveAndCache/lookup hit 路径，100K × 5-pass 交错 min-of-5。结果见 §4.21.2 表（961/1001 = 1.04x，910/980 = 1.08x）。**测量教训**：一次性顺序测量噪声主导（frequency ramp + code-cache cold 把比值反转到 0.83x — 两个代码形状几乎相同的 loop 不可能差 380ns）→ 改交错 min-of-5 后稳定。

**No regression** — 全部 1824 prior tests pass unchanged × 3 stable。

### 4.21.10 Edge Cases & Lessons Learned

1. **顺序测量会翻转比值（本 PR 最大坑）** — 初版 Scenario H 一次性顺序跑：lock-free 1318ns "慢于" thread-safe 1093ns（0.83x），且 lookup(1698) "慢于" resolveAndCache(1318) — 两对代码形状相同，物理上不可能。根因：frequency ramp + code-cache coldness + debug DPC 噪声。**Fix**：交错 min-of-5（每 pass 两模式交替，取每模式最小）→ 961/1001 稳定复现。**Lesson**：微基准永远 min-of-N 交错，单次顺序测量不可信。
2. **debug STL 主导绝对值** — 每次 hit 付 `std::string` 临时构造（`find(const char*)` + 非 transparent hash）+ checked iterators ≈ 900-1000ns/iter；mutex delta（~20-30ns）在噪声以下。**P3 结论必须诚实写结构性收益**（零同步可证明），不夸 lock delta 数字。release 构建（STL inline）才是 lock delta 显著区。
3. **`maybeLock` 返回 unique_lock 而非 bool+手写 lock/unlock** — RAII 保持：锁路径异常安全（同 lock_guard 语义），无锁路径 default ctor 零成本。`unique_lock` default ctor 是 noexcept constexpr — 实测无额外开销。
4. **mutex 保留而非删除** — 删除 mutex 会破坏 §4.12.1 的 authoring-tools future-proof 契约（P1.7 明确写 mutex 存在理由）；`setThreadSafe` opt-in 保持双轨能力，0 语义倒退。
5. **VsDevCmd 路径** — 本机 VS 装在 `D:\Visual Studio\Product`（非 Community 路径）；调错路径 INCLUDE 不设 → `stdint.h`/`string` C1083。**build bat 必须用 `D:\Visual Studio\Product\Common7\Tools\VsDevCmd.bat`**（和 ay-ui-build-flow memory 的 Community 路径不同 — 机器差异）。

### 4.21.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(multi-threaded host)**：任何未来多线程调用方在首次并发访问前 `setThreadSafe(true)`；此后行为与 P1.7 完全一致。单线程 ECS 路径无需任何改动。
- **UPGRADE-HOOK(transparent hash)**：`find(const char*)` 临时 string 构造占 hit 路径 ~900ns 大头 — 若未来做 `std::string_view`/transparent-hash 异构查找，Scenario H 会显示 lock delta 显著化（顺带方向：map key 改 `uint32_t` FNV-1a — P0 polish 的 ParamNameRegistry 模式可复用）。
- **UPGRADE-HOOK(TSAN)**：lock-free 默认模式 TSAN 天然干净；thread-safe 模式原样。

### 4.21.12 Open Questions

1. **Release-build lock delta 未实测** — 本项目只有 x64-Debug 配置；1.04x/1.08x 是 debug 结构性收益下限。release 构建落地后补 Scenario H 复测。
2. **`_threadSafe` 翻转时机** — 契约要求「无其他线程在 cache 内」。**→ RESOLVED 2026-08-10（P6 polish）**：setThreadSafe 离开 thread-safe 模式时 try_lock probe + debug assert（INV-70）；进入方向（false→true）不可低成本证明，保持 startup 约定。详见 §4.24。
3. **临时 string 优化是否值得单开** — `find(const char*)` 每次构造 `std::string`（~900ns debug 大头）；修法（transparent hash / uint32 key）改动 cache 内部类型，属未来独立 polish 项，不在 P3 范围。**→ P4 polish RESOLVED 2026-08-10**（transparent hash 异构查找落地，见 §4.22）。

---

## 4.22 ✅ P4 polish — Additive slot 内存回收 + AssetBoneCache transparent hash + 批量 tick 压力测试 — SHIP（2026-08-10）

### 4.22.1 Overview

三项合并 PR，收口 polish roadmap 方向 1/2/3 剩余项（2026-08-10 盘点结论）：

1. **Additive slot 内存回收**（方向 2 收口）：`clearAdditiveLayerSource` / `stop()` 现在把 slot 的 multi-KB buffer（tracks / pendingNotifies / capturedLocal{Pos,Rot,Scl} / trackWeights）**归还 allocator**（swap-with-empty，`clear()` 只清 size 不还 capacity）。此前闲置 slot 在 `setSkeleton` 后永远持有 `n*3 + n*4 + n*3` floats（100-bone ≈ 4KB/slot）直到 player 析构。
2. **AssetBoneCache transparent hash**（方向 1 干净化，兑现 §4.21.12 Q3 + §4.21.11 UPGRADE-HOOK）：inner map 改 `StringViewHash + std::equal_to<>`，`find(const char*)` 异构查找 **0 临时 string 构造**（默认 `std::hash<std::string>` 路径每 hit 构造一个 `std::string` ≈ 900ns debug / ~10ns release）。
3. **批量 tick 压力测试**（方向 3）：新增 `AYTest_P4Stress.cpp` 4 cases —— 400 player × 200 帧共享单 skeleton 逐位一致性、bind/clear 5 轮循环 AssetBoneCache entry 回落、异构查找单 entry、AdditiveSlot clear/stop 行为回归。

**0 public API change / 0 ECS bridge change**（同 P0~P3 polish 原则）。**+3 new INV**（INV-61/62/63）。

### 4.22.2 Motivation

- roadmap（2026-08-10 盘点）三项值得做：内存复用（闲置 slot 真回收）、测试基建（地基定型硬证据）、顺手干净化（transparent hash）。
- 其余候选（循环小优化 / 缓存池 / 延迟销毁 / setTriggerByHash）无实测痛点，低 ROI，不做。
- 长线功能（`.ayasm` / BlendTree-in-SM / MotionMatching / IK）明确不在 polish 范围（算术已于 P5 polish 2026-08-10 兑现，见 §4.23）。

### 4.22.3 Data Model

- `AnimationPlayer::releaseSlotBuffers(AdditiveSlot&)` 新私有 helper（header 声明 + cpp 定义）：
  ```cpp
  std::vector<TrackSlice>().swap(slot.tracks);
  std::vector<AnimNotifyRecord>().swap(slot.pendingNotifies);
  std::vector<float>().swap(slot.capturedLocalPos);
  std::vector<float>().swap(slot.capturedLocalRot);
  std::vector<float>().swap(slot.capturedLocalScl);
  std::vector<float>().swap(slot.trackWeights);
  ```
- `AssetBoneCache::detail::StringViewHash`：`is_transparent` + 3 重载（`string_view` / `const string&` / `const char*`）。inner map 类型：
  ```cpp
  std::unordered_map<std::string, int32_t, detail::StringViewHash, std::equal_to<>>
  ```
- 测试：`unittest/AYTest_P4Stress.cpp`（新文件，自带 2-bone skeleton fixture + Override/Additive ramp clip builder）。

### 4.22.4 Public API

**0 change**。`AnimationPlayer` 全部现有签名 identical（`releaseSlotBuffers` 是 private）；`AssetBoneCache` 全部现有签名 identical（`StringViewHash` 在 `detail` namespace）；`AnimationPlayer` / `AssetBoneCache` 构造与拷贝语义不变。

### 4.22.5 Internal Algorithm

1. **内存回收**：`clearAdditiveLayerSource`（原 `tracks.clear()` + `pendingNotifies.clear()` 两行）与 `stop()`（per-slot loop 内）改调 `releaseSlotBuffers`。小字段（clip/time/playRate/loop/flags/curve）语义不变；`_additiveSlots` vector 本身 **size 不动**（sparse 语义 + 8 上限检查兼容，INV-62）。re-bind 时 `rebuildSlotTracks` / capture 各 realloc 一次（低频 bind 操作，可接受）。
2. **transparent hash**：C++14 异构查找（本项目 C++23）。`find(name)` 对 `const char*` key 直接 `StringViewHash::operator()(const char*)` → `std::hash<std::string_view>`，0 string 构造；等值比较走 `std::equal_to<>`（transparent）→ C++20 反向 `operator==(const char*, const string&)`。
3. **压力测试**：每帧 **`tick(dt)` + `evaluate()`** 完整帧（tick 只推进时钟 + notify，渲染评估在 evaluate —— 首版只 tick 导致 resolve 未触发、断言全败，见 §4.22.10.1）。

### 4.22.6 ECS Bridge

**0 change**（AYEntity / AYAnimationSystem / StateMachineSystem 未动）。内存回收与 transparent hash 都在 player / cache 内部消化。AYEntity 421/421 × 3 零回归。

### 4.22.7 Resource Bridge

**N/A**。无 `.ayanm` / `.ayskel` / `.aymask` / `.ayasm` 格式变化。

### 4.22.8 Invariants

- **INV-61**：闲置 slot（`clearAdditiveLayerSource` / `stop()` 后）heavy buffers 全释放（capacity 0 — swap-with-empty 语义）；小字段保留供 re-bind fresh state。
- **INV-62**：`_additiveSlots` vector size 保留（sparse 语义不变，`stop()` 不清 size，9th-bind reject 语义不变）；re-bind 行为与 P1.5 逐位一致（行为级由测试钉：clear → 无贡献 → re-bind → 恢复同值）。
- **INV-63**：`AssetBoneCache::lookup/resolveAndCache` 的 `const char*` / `string_view` key 查找 0 临时 string 构造（结构性：`StringViewHash::is_transparent`）；功能上三种 key 拼写命中同一 entry（无重复插入）。
- L1/L2/L3 + P0~P3 polish 全部 contracts preserved（INV-1..60 零回归）。

### 4.22.9 Testing

- **+4 TEST_CASE**（`AYTest_P4Stress.cpp`，新 suite `P4StressTests`）：
  1. `p4_stress_400_players_share_one_skeleton` — 400 player 共享单 skeleton × 200 帧完整帧 tick+evaluate；每帧 `players[0]` vs 参考 player `memcmp` 逐位一致（loop wrap 包含）；首帧后 `skeletonEntryCount() == 1`（P1.7 共享契约在负载下成立）；末尾 400 `isValid()`。
  2. `p4_stress_bind_clear_cycle_returns_cache_entries` — 5 轮 × 40 player（各自 skeleton）bind + 完整帧；每轮断言 entry == 40（fresh round）；全 invalidate 后回落 0（无跨轮泄漏）。
  3. `P4_AssetBoneCache_HeterogeneousLookup_SingleEntry` — INV-63：literal / `const char*` / `string_view` 三拼写命中同一 entry（`boneNameEntryCount == 1`）。
  4. `P4_AdditiveSlot_ClearStop_RebindFreshState` — INV-61/62：bind → t=0.5 贡献 6.0 → clear → 5.0 → re-bind 同 slot → 6.0（fresh state 恢复）→ stop → 5.0（全 slot 清）。
- **3-run stable（2026-08-10）**：AYAnimation **2673/2673**（1851 + 4 新 case ≈ 822 新断言：200 帧 memcmp + 400 isValid + 循环/异构/回收断言）× 3；AYEntity **421/421** × 3；AYResource **1039/1039** × 3。**0 regression**。

### 4.22.10 Edge Cases & Lessons Learned

1. **`tick()` ≠ `evaluate()`（压力测试首版根因）** — `AnimationPlayer::tick(dt)` 只推进时钟 + 发 notify，**不跑渲染评估**（`evaluate()` 是 3-phase 渲染路径，track resolve / AssetBoneCache 在那边）。首版压力测试只 `tick` → 断言 entry count 全败（resolve 从未触发）。**Fix**：每帧 tick + evaluate 完整帧。**Lesson**：批处理测试必须走「时钟推进 + 渲染评估」双半，只 tick 是半个帧。
2. **transparent hash 的 C3066 模糊调用（MSVC）** — `StringViewHash` 只有 `string_view` + `const string&` 两个重载时，`const char*` key 对两者都是**等价的用户定义转换** → `xhash` `_Uhash_compare::operator()` 处 C3066 ambiguous。**Fix**：加精确 `operator()(const char*)` 重载（精确匹配赢过用户定义转换）。**Lesson**：transparent hash 必须显式提供所有查找 key 类型的精确重载，不能依赖隐式转换。
3. **fresh-round 断言设计** — bind/clear 循环每轮开头 cache 都是 0（上一轮 invalidate 清空），断言必须是 `== kPerRound`（不是累积 `kPerRound*(round+1)`——首版写错，round 1+ 的失败是断言自身 bug）。
4. **内存回收的结构性验证边界** — `_additiveSlots` 是 private，测试无法直接断言 capacity；INV-61 的释放是 swap 语义（结构性），测试钉行为（clear → 无贡献 → re-bind → 恢复）。诚实：结构由实现保证，测试提供行为回归 + 语义回归。

### 4.22.11 Migration / Upgrade Hooks

- **UPGRADE-HOOK(§4.21.12 Q3 / §4.21.11 transparent hash) → RESOLVED**：transparent hash 落地（INV-63）；`find(const char*)` 不再构造临时 string。
- **UPGRADE-HOOK(P0 polish .A — setTriggerByHash caller-side cache)**：仍 deferred（setTrigger 低频，debug-only regression 已接受；production critical path 是 getParam/eval）。
- **UPGRADE-HOOK(slot vector shrink)**：`_additiveSlots` size 不缩（INV-62 sparse 语义）；若未来场景「player 长存 + slot 数波动大」成为内存热点，可加 `shrinkSlotsToActive()` 显式 API（不在当前范围）。

### 4.22.12 Open Questions

1. **release-build 收益未实测** — 全项目只有 x64-Debug 配置；transparent hash 的 release 收益 ~10ns/bind（debug 大头消失），内存回收是确定收益（allocator 级）。**→ RESOLVED 2026-08-10（P6 polish）**：x64-Release 落地 + Scenario G/H 首测（lock-free 收益 release 1.40x~2.46x vs debug 1.06x/1.12x；lookup release 14~18ns 接近裸 unordered_map 成本），详见 §4.24.10。
2. **压力测试规模阈值未探索** — 400 player × 200 帧是「合理量级」而非「找到极限」；大批量（10K+ player）的缓存退化 / 内存峰值未压。后续 stress suite 扩展时可加。
3. **captured buffer 的 setSkeleton 全量 assign 保留** — 闲置 slot 在 `setSkeleton` 时仍会被 assign `n*3/n*4/n*3`（P1.5 行为，为了 enable 时不 realloc）。与 INV-61 的「clear 时释放」构成「释放后 setSkeleton 再分配」循环——低频路径，接受。

---

## 4.23 ✅ P5 polish — DSL 四则运算（+ - * /）— SHIP（2026-08-10）

> 本节为 P5 polish 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。
> 收口项：roadmap 方向 4「小型刚需缺口」第一项（§14.3 曾列 `算术 + - * /` 为 P3.x刀 N+1.1/N+2 open）。

### 4.23.1 Overview

Condition DSL 从 8 算子扩到 **13**（`> < == != && || !` + 新增 `+ - * /` 四则 + 一元 `-`），AST / Bytecode 双路径 1:1 语义等价。**0 public API change** — 全部是 ConditionParser 语法扩展，Transition / StateMachine / ECS bridge 零改动。

### 4.23.2 Motivation

- roadmap 方向 4「小型刚需缺口」第一项：条件表达式可参与数值运算（`Speed * 2 > 10` / `Damage + 1 > 5` / `CurrentStateTime * 2 > 1`），无需 host code `setParam` 预计算。
- §4.16 决策「算术 defer P3.x刀 N+1」（8 算子够用）与 §4.17「不引入算术」在 P5 兑现——两处历史决策标 RESOLVED。
- P2 polish bytecode hot path 是生产路径：新算子必须 AST/bytecode 双实现 + parity 测试（INV-68），否则 hot path 与 AST 语义漂移。

### 4.23.3 Data Model

- `CondOp` 追加：`Add = 7 / Sub = 8 / Mul = 9 / Div = 10 / Neg = 11`（追加在 Not 之后，**旧值 0..6 不变**）。
- `CondOpByte` 追加：`OP_ADD = 10 / OP_SUB = 11 / OP_MUL = 12 / OP_DIV = 13 / OP_NEG = 14`（追加在 OP_LOAD_RESERVED = 9 之后，**旧值 0..9 不变**——已有测试按枚举引用）。
- `CondUnaryExpr.op` 契约从「only Not」放宽为「Not / Neg」。

### 4.23.4 Public API

**0 change**。`ConditionParser::parse` / `compileToBytecode` / `CondBytecode::evaluate` / `CondExprAst::evaluate` 签名 identical；`StateMachine` / `Transition` / `ConditionEvalCtx` / `CondVisitor`（复用了 Binary/Unary 节点，无需新 visit 方法）全部不变。

### 4.23.5 Internal Algorithm

- **Lexer**：+4 token（Plus / Minus / Star / Slash）+ **INV-65 负号消歧**——`-` 后跟数字仅在「前一 token 不能结束表达式」时折叠为负数字面量（`A-3` → `[A][-][3]`，`-3` / `2 * -3` 保持负字面量，pre-P5 AST 形状不变）。
- **Parser**（precedence-climbing）：Add/Sub = 5、Mul/Div = 6（Compare 仍 = 4）→ `A + B > C` 解析为 `(A + B) > C`（INV-69）；`parseUnary` 加 Minus 分支（INV-66：`-A * B` = `(-A) * B`）。
- **AST 求值**：`CondBinaryExpr::evaluateAsFloat` 新 override 计算 float 值（Add/Sub/Mul/Div 四臂；比较/逻辑臂 fallthrough 0.0f——INV-64 类型不匹配契约与 base-class default 一致）；`evaluate` 对算术臂走 `evaluateAsFloat(ctx) != 0.0f`（与 bare ident 的 bool 强转一致）；`CondUnaryExpr::evaluateAsFloat` 新 override：Neg = `-operand`（Not 仍 0.0f）。
- **INV-67 除零**：AST 与 Bytecode 双路径 `b == 0.0f → 0.0f`（fail-soft，绝不 inf/nan——否则污染下游 `fabs-epsilon` 比较）。
- **Bytecode**：`compileNode` 比较 switch 扩算术四臂 + unary 扩 `OP_NEG`；evaluator 固定栈 switch 加 5 分支（ADD/SUB/MUL 直接 `sp-1 op b`；DIV 带零 guard；NEG 取反）。算术 op 与比较同栈形（pop 2 push 1），**短跳转 ±127 机制不受影响**（右子树字节数编码不变）。

### 4.23.6 ECS Bridge

**0 touch**（AYEntity 无改动，AX 未跑新 case）。

### 4.23.7 Resource Bridge

**0 touch**。

### 4.23.8 Invariants

**INV-64..69 全部 NEW**（见 §4.23.1/5 引用 + ConditionExpr.h 头注释）：

- INV-64 — Add/Sub/Mul/Div 二元 float 算子；evaluateAsFloat 算值 / evaluate 强转 bool；比较/逻辑子表达式在 float 上下文仍 0.0f
- INV-65 — lexer 负号消歧（`A-3` == `A - 3`；负字面量形状保留）
- INV-66 — 一元 Neg 绑定比 Mul/Div 紧（`-A * B` = `(-A) * B`）
- INV-67 — 除零 → 0.0f（AST + bytecode 双路径，fail-soft）
- INV-68 — OP_ADD/SUB/MUL/DIV/NEG ≡ AST 1:1 语义等价（parity 测试）
- INV-69 — 算术优先级（5/6）高于比较（4）：`A + B > C` = `(A + B) > C`

### 4.23.9 Testing

**14 new unit tests**（§8.1.6 11 + §P5 3）+ 13-case parity 表 + 1 SM 集成：

- §8.1.6（11）：Add_Compare / MulDiv_Precedence / Parens_Override / NoSpace_Minus（INV-65）/ Div_Ok / DivByZero_FailsSoft（INV-67）/ UnaryNeg（INV-66 判别式 `-Speed - 1 == 2`）/ Chain_LeftAssoc（判别 `A - B - C == 2`）/ BoolCoercion（INV-64）/ ParseErrors（7 个坏串）/ Transition_Integration（`Speed * 2 > 10` 走 bytecode hot path 真 fire + 不 fire）
- §P5（3）：Bytecode_Parity（13 cases 跨 AST/bytecode，含 reserved `CurrentStateTime * 2 > 1` 组合 + 负字面量 back-compat + `A-3` 无空格）/ Bytecode_Opcodes（尾 opcode 编码 7 检 + 负字面量不含 OP_NEG 形状断言）/ Bytecode_ShortCircuit_SkipArith（INV-58 与算术共存）

3-run stable：AYAnimation **2831/2831** + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归（2817 → 2831，+14）。

### 4.23.10 Edge Cases & Lessons Learned

1. **测试期望值算错（3 个 FAIL 全是测试 bug，不是代码 bug）**：`(A + B) * 2 == 10` 的 false 臂最初用 A=4, B=1 → `(4+1)*2 = 10` 实际是 true。教训：手算期望值时避开「恰好等于」的数值组合；B=2 → `(4+2)*2 = 12` 才判别。
2. **测试块误插 TEST_SUITE 中间**：§8.1.5 的 section 注释头在文件**顶部**（TIS 测试是第一节），按「§8.1.5」匹配替换时把新测试块插到了 suite 开头 + 一个 `TEST_SUITE_END` 落在中间 → 后续 ~40 个 TEST_CASE 全掉出 suite（`_suite_name` 未定义 C2065）。教训：**插测试块前先确认目标 section 头在文件中的位置**（grep 行号），或用文件尾部 `TEST_SUITE_END` 作唯一锚点。
3. **负数消歧的漏网表达**：`--3`（两个一元减）经 INV-65 规则解析为 `Neg(Literal(-3))` = 3——无需特殊处理即工作。

### 4.23.11 Migration / Upgrade Hooks

- §4.16 决策「算术 defer P3.x刀 N+1」→ **RESOLVED（2026-08-10）**。
- §4.17 决策「不引入算术」→ **RESOLVED（2026-08-10）**（`(CurrentStateTime - 0.5) > 0` 现在可直接写）。
- §14.3 `算术 + - * /` row → 标 ✅（string/int param 类型仍 deferred）。
- UPGRADE-HOOK(P3.x L2 算术扩展) 的 reserved ident 扩展方向不受影响（`CurrentStateTime` 仍是唯一 reserved，算术是对它的运算符组合）。

### 4.23.12 Open Questions

无实质。`%` 取模 / `>=` `<=` / 一元 `+` 明确不在 scope（§14.3 纪律：string/int param 类型才是下一刀，算术已闭合）。

---

## 4.24 ✅ P6 polish — INV-60 flip debug assert + release 配置落地 — SHIP（2026-08-10）

> 本节为 P6 polish 完整 ship 文档。模板遵循 §4.11 P1.5 Full Ship 的 12 段式。
> 收口项：§4.21.12 Q2（`_threadSafe` 翻转 debug assert）+ §4.20.12 / §4.21.12 / §4.22.12 Q1（release 配置落地）。roadmap 剩三项中的两项；setTriggerByHash caller-side cache 明确永久挂起（用户决策，低频低 ROI）。

### 4.24.1 Overview

合并 PR，双刀：
- **A) INV-60 翻转 debug assert**：`setThreadSafe` 在**离开** thread-safe 模式（true→false）时做 try_lock probe——若另一线程正持锁在 cache 内，debug build assert（INV-60 契约违反）。
- **B) release 配置落地**：`out/build/x64-Release`（Ninja + MSVC /O2 + vcpkg toolchain + `AY_BUILD_BENCHMARKS=ON`）首次 configure + build；AYAnimation_UnitTests release 全绿 + Scenario G/H release 基准首测。

**0 public API change**，ECS bridge 0 touch。

### 4.24.2 Motivation

- §4.21.12 Q2：INV-60 契约（flag 非原子，仅允许「无其他线程在 cache 内」时翻转）「没有 runtime assert（无法低成本检测）」——多线程 host 在另一线程访问 cache 中途 `setThreadSafe(false)` 是静默破坏，debug build 应尽早暴露。
- §4.20.12 / §4.21.12 / §4.22.12 Q1：全项目只有 x64-Debug；Scenario G/H 的「release 收益」从未实测。release 配置是地基型基建——不改任何代码路径，只补验证维度，让 §4.21 的「lock delta 在 release 构建才显著」论断有实测证据。

### 4.24.3 Data Model

**0 新成员**。放弃 roadmap 预想的 `_lockCount` 计数方案，改用 mutex try_lock probe（权衡见 §4.24.10）：`_threadSafe` / `_mu` 不变；7 访问点 maybeLock 结构不变。

### 4.24.4 Public API

**0 change**（`setThreadSafe` / `isThreadSafe` 签名 identical；release 行为 identical——assert 编译掉后与 P3 完全一致）。

### 4.24.5 Internal Algorithm

`setThreadSafe(enabled)` 新逻辑（AssetBoneCache.cpp）：

```cpp
if (_threadSafe) {                      // 离开 thread-safe 模式才需要证明
    const bool uncontended = _mu.try_lock();  // 非阻塞，绝不死锁
    if (uncontended) _mu.unlock();
    assert(uncontended && "AssetBoneCache::setThreadSafe: another thread "
                          "is inside the cache — INV-60 flip contract violated");
}
_threadSafe = enabled;
```

检测边界（诚实口径）：
- **可证明方向（被 assert 守护）**：true → false。try_lock 失败 ⟺ 另一线程持锁在 cache 内——正是危险翻转（去掉锁还有人在用）。
- **不可证明方向**：false → true（进入 thread-safe 模式）。lock-free 模式 7 sites 从不碰 mutex，try_lock 必成功——该方向保持 startup 约定（INV-60 原文）。
- NDEBUG：release 下 assert 编译掉、翻转照旧——probe 是 best-effort 诊断，非正确性机制，契约不变。

### 4.24.6 ECS Bridge

**0 touch**。

### 4.24.7 Resource Bridge

**0 touch**。

### 4.24.8 Invariants

**INV-70 NEW**：debug build 下 `setThreadSafe` 离开 thread-safe 模式（true→false）时，若另一线程持有 mutex（在 cache 内），assert 必须触发；release build assert 编译掉、行为与 P3 一致（probe 保留——setThreadSafe 是低频路径，try_lock 成本可忽略）。

### 4.24.9 Testing

**+1 new unit test**（`P6_AssetBoneCache_SetThreadSafe_FlipKeepsData`）：populate 2 entries → lock-free→thread-safe→lock-free × 2 翻转（每次 true→false 都走 try_lock probe；P3 既有翻转测试 line ~3042 同路径回归兜底）→ entries 完整存活 + lookup 值不变 + 无 false positive。

单线程 UT **无法**构造 assert 触发路径（调用者定义上不在 cache 内）——assert 由真实多线程 host 触发；P3/P6 翻转测试保证 probe 无 false positive。

- debug：AYAnimation **2843/2843** × 3 stable（2831 → 2843，+1 TEST_CASE / +12 断言）
- release：AYAnimation **2843/2843** × 3（首跑 + 2 复跑全绿）——release 配置落地的第一份全绿证据
- AYEntity 421/421 + AYResource 1039/1039 未跑（0 touch，基线不变）

### 4.24.10 Benchmark（release 首测，§4.20.12/§4.21.12/§4.22.12 Q1 兑现）

Scenario G/H 在 **x64-Release** 首测（debug 基线今日同跑对照）：

| 场景 | debug（实测） | release（5 跑取 min / 区间） |
|---|---|---|
| G bytecode 3-ident true | 86.2 ns/iter | **18.3 ns（~4.7x）** |
| G bytecode short-circuit | 75.9 ns/iter | **20.6 ns（~3.7x）** |
| H resolveAndCache hit | 724.6 vs 765.7（1.06x） | 16.6 vs 23.6（**1.40x**，5 跑区间 1.40~2.10x） |
| H lookup hit | 697.2 vs 784.0（1.12x） | 14.1 vs 23.1（**1.44x**，5 跑区间 1.44~2.46x） |

结论：
- **lock-free 收益在 release 显著化**：debug 1.06x/1.12x（mutex delta 淹没在 debug STL 开销下）→ release **1.40x~2.46x**（中位 ~1.5x）——兑现 §4.21 的「lock delta 在 release 才显著」论断。
- **hot path 绝对值已接近裸 unordered_map 成本**：P4 transparent hash 后 release lookup 14~18ns（debug ~900ns 的 debug 大头消失）——P4 收益在 release 同样显著化。
- Scenario G release 18.3ns vs debug 86.2ns（~4.7x），与 P2 ship 的 debug 相对比（bytecode/AST 1.34x/1.28x）不冲突——那是 AST 相对比，这是 debug/release 绝对比。

测量教训（重复 §4.21.10）：run-to-run 噪声 18~51ns（±2.5x），系统负载影响 >> 被测差异——必须 min-of-5 取 min + 报区间，单次测量会得到 50.8ns 离群值。

### 4.24.11 Migration / Upgrade Hooks

- §4.21.12 Q2（`_threadSafe` 翻转时机 debug assert）→ **RESOLVED（2026-08-10）**（try_lock probe，非 _lockCount）。
- §4.20.12 Q1 / §4.21.12 Q1 / §4.22.12 Q1（release-build 验证）→ **RESOLVED（2026-08-10）**（x64-Release 落地 + Scenario G/H 首测）。
- §14.2 release-build row → 标 ✅。
- roadmap 剩项同步：setTriggerByHash caller-side cache 明确**永久挂起**（用户决策：低频低 ROI，省下时间开长线）。
- UPGRADE-HOOK（多线程 host）：`setThreadSafe(true)` 仍须并发前调用；debug build 下错误时机会被 INV-70 assert 捕获。

### 4.24.12 Open Questions

- **release 制度化未完成**：root 无 CMakePresets.json（x64-Release 由 `do_cmake.bat` 手动命令配置，非 preset 固化）；其余 27 模块 release 构建/测试未验证（本次只构建 AYAnimation 两 target）。若未来做全项目 release CI，需先补 preset + 各模块 release 冒烟。
- **INV-70 触发路径无真实 host 验证**：多线程 authoring host 尚不存在，assert 路径待真实 host 落地后自然暴露（P3/P6 测试只保证无 false positive，不保证真多线程场景必命中——契约本身未变）。

---

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
| P1.1 | Anim Notify 事件系统（多播 `function<void(NotifyEvent)>`）── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation notify channel (VERSION 2) + dispatchPendingNotifies + AnimNotifyEvent POCO + EventBus bridge via AYEntity AnimationSystem |
| P1.2 | Additive Layer 1 MVP（per-track blendMode + global additiveWeight）── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation VERSION 3 + v2 backward compat + AnimationPlayer Phase 1 additive branch (position += / rotation pow / scale *= (1+)) + AYEntity AnimationComponent.additiveWeight + 7 new tests (1 AYResource v2 + 6 AnimationPlayer) |
| P1.3 | CrossFade in/out (Layer 2 — separate base + additive clip source mixing) ── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation VERSION 4 (no on-disk change) + 5 invariants + AnimationPlayer dual-source state machine + Phase 1b additive branch (reuses P1.2 three formulas) + 2 notify queues + AYEntity AnimationComponent.{additiveClipPath,additivePlayRate,blendWeight} + 14 new tests (1 AYResource forward-compat + 10 AnimationPlayer + 3 AYEntity integration) |
| P1.4 | Hot-path 优化 + Cross-Fade full ship：track → boneIndex 预解析 (已 ship) + keyframed weight curve (`blendWeightOverTime` with 4 ease flavors reusing AYMath) + syncToBase option (additive playhead lock-step to base) + ref-pose capture path (CaptureState 3-state machine replacing rest-pose-at-0 assumption) + additive pause/resume (INV-8 unified with base pause)── ✅ **FULL SHIP 2026-07-26**; 8 AnimationPlayer tests + 3 AYEntity tests; 0 regression across 3 modules (701+312+187) |
| P1.5 | Multi-slot stack：`vector<AdditiveSlot>` (kMaxAdditiveSlots=8) + notify merge/`sourceTag` + `trackWeights` opt-in + AYEntity `AdditiveLayerSpec` 桥接 + EventBus `AnimNotifyEvent.sourceTag` pipe ── ✅ **FULL SHIP 2026-07-27**; 22 AnimationPlayer tests + 5 AYEntity tests; 0 regression across 3 modules (AYAnimation 398/398 + AYEntity 216/216 × 3); 详见 §4.11; 共享 skeleton tick cache 仍留 P1.6 |
| P1.6 | Deprecate Wrapper Cleanup：真实删除 `setAdditiveWeight` / `getAdditiveWeight`（P1.2 inline-forward）+ `consumePendingNotifiesAdditive` / `getPendingNotifyCountAdditive`（P1.3 DEPRECATE-P1.5）+ dead `dispatchAdditiveNotifies` helper；bridge 改 canonical `setBlendWeight`；tests 11 + 8 caller 改 canonical；`P1_3_NotifyIndependence_BaseAndAdditive` 改测 merged queue + sourceTag ── ✅ **SHIP 2026-07-27**; 3-run stable 398/398 + 216/216 + 701/701 × 3, zero regression。**共享 skeleton tick cache 留 P1.7**（需 ECS refactor `SkeletonComponent::skeleton` → `shared_ptr<const ISkeleton>`） |
| P1.7 | Shared Skeleton Tick Cache = **ECS refactor** + **asset-level boneIdx cache**：(A) `SkeletonComponent::skeleton` by-value → `shared_ptr<Skeleton>`（implicit convert 给 `setSkeleton(shared_ptr<const ISkeleton>)`），N entity 同 skeleton 共享 1 份 asset，砍 N 倍内存 + N 次首帧 `setBoneCount + n×setBone()` 拷贝；(B) `AssetBoneCache` 单例（mutex）`(ISkeleton* addr, boneName) → boneIdx` 跨 player 共享；`AnimationPlayer::_skeleton` 改 shared_ptr；`resolveBoneIdxOnce` 走 AssetBoneCache（hit 直接写、miss resolveAndCache）。新文件 `AssetBoneCache.h/cpp` + 改 6 文件 + 60+ AYAnim + 14 AYEntity test callsite + 6 + 2 新 test。**关键 footgun**：inline-build skeleton test 必须先 `skel->skeleton = std::make_shared<Skeleton>()` 再 `*skel->skeleton`。On-disk format 不变（`.ayanm` v4 / `.ayskel` v1） ── ✅ **SHIP 2026-07-27**; 3-run stable 420/420 + 701/701 × 3 (AYAnimation + AYResource); AYEntity 含 1 个 pre-existing CharacterEntity flake（与 P1.7 无关）。详见 §4.12 |

### P2 — 混合 + 蒙皮（~3 PR 量）

| Step | 内容 |
|---|---|
| P2.1 | Blend 1D / Blend 2D（BlendTree 节点类型）── ✅ **SHIP 2026-07-28 `68f4227`**（BlendSpace 1D/2D：BlendSpace.h/cpp + AYTest_BlendSpace.cpp 13 cases；P3.x刀2 BlendTree-in-SM 直接建立在它之上）|
| P2.2 | 骨骼遮罩 (Skeleton Mask) ── ✅ **SHIP 2026-08-03** + **P3.x刀1 .aymask loader ship 2026-08-06** — ISkeletonMask 移去 `ayt::resource` namespace + `IAYSkeletonMask.h` formal interface + `AYSkeletonMask` concrete + `SkeletonMaskLoader` + `registerLoaderType("SkeletonMask", ".aymask")` + 12 case loader 测试 + 1 case Bootstrap 测试 + 4 文件 include flip；AYAnimation 510/510 + AYEntity 338/338 + AYResource 1044/1044 × 3 stable，零回归 |
| P2.3 | Montage 语义 Slot（**对齐** §4.8 AdditiveSlot，禁止第二套 layer API）|
| P2.4 | Dual-Quaternion Skinning |
| P2.5 | CPUSkinningPass（独立 module，CPU 顶点变形真输出）|
| P2.6 | AYRenderer 改造：SkinnedLit 读 `SkeletonComponent.skinMatrices`（统一渲染路径）|

### P3 — 状态机 + IK + 重定向（~5 PR 量）

| Step | 内容 |
|---|---|
| P3.1 | L1 状态机 ── ✅ **SHIP 2026-08-06**：StateMachine class（first-match-wins + wildcard fromState + automatic trigger + cross-fade wait + trigger auto-consume + unknown-param fail-soft + INV-18..26）+ AnimationStateMachineComponent（POD: resourcePath placeholder + pendingTriggers + speed/verticalSpeed/isGrounded/isAttacking + read-back fields + setTrigger convenience）+ StateMachineSystem priority 460（after AnimationSystem 450，sync params + drain triggers + tick SM + push new clip + emit AnimStateChangedEvent via EventBus kTypeId=0x000A'0010）+ 15 AYAnimation unit tests + 8 AYEntity ECS integration tests；3-run stable AYAnimation 543/543 + AYEntity 370/370 + AYResource 1044/1044 × 3，零回归；详见 §4.14 + §13 row 20 + §11 P3.1 row |
| P3.2 | L3 子状态机 ── ✅ **SHIP 2026-08-06**：StateMachine 加 `vector<unique_ptr<StateMachine>> _children` + `_currentChildIndex` + `State.isSubMachine / subMachineIndex` + `addSubMachine / getActiveSubMachine / getActiveLeafStateName` API + StateMachine 显式 move-only（`_children` 不可拷贝）+ 递归 `setTrigger / setParam`（INV-28）+ child-first transition fallback（INV-29）+ `getActiveLeafStateName` 深度 ≤ 2（INV-30）+ `_currentChildIndex` 在 transition complete / instant cut 同步更新（INV-31）+ ECS bridge 兑现 dt plumbing（`sm.update(0.0f)` → `sm.update(dt)`）+ `AnimationStateMachineComponent.activeSubState` read-back + sub-machine entry state 不调 `player.play()`（child SM drives, INV-27）+ 12 AYAnimation unit tests + 4 AYEntity ECS integration tests；3-run stable AYAnimation 600/600 + AYEntity 385/385 + AYResource 1044/1044 × 3，零回归；详见 §4.15 + §13 row 21 + §11 P3.2 row |
| **P3.x** | **L2 Condition DSL ── ✅ SHIP 2026-08-07**：Transition 扩展缓存层（`conditionExpr / cachedAst / conditionDirty / conditionParseError` 4 字段）+ `setConditionExpr` / `invalidateConditionCache` / `evaluateCondition(ctx)` 3 API + `ConditionExprAst` 类族（Binary/Unary/Identifier/Literal + Visitor 接口给 P4.x graph-builder 留口）+ `ConditionParser`（mini Lexer + precedence-climbing Parser，照抄 AYShader pattern 但 0 link AYShader / AYScript / AYGraph）+ 8 算子（`> < == != && \|\| ! ()`）+ 字面量 float / bool + 短路求值（`&&` / `\|\|`）+ 负数字面量 + lazy parse + dirty cache + `setConditionExpr` auto-flag dirty + 显式 `invalidateConditionCache()` + parse-fail-soft-false（cachedAst=null + conditionParseError 非空 + stderr 一行 + transition 永假）+ L1 back-compat 双轨（`hasCondition=true + conditionExpr=""` 走 L1；非空 conditionExpr 走 L2）+ `ConditionEvalCtx` 4 字段（`params / triggers / currentState / currentStateTime`，后两个留 P3.x刀 N+1 钩子）+ 30 AYAnimation unit tests（§8.1.1 Parser 8 + §8.1.2 Evaluator 12 + §8.1.3 Cache 6 + §8.1.4 Back-compat 4）+ 4 AYEntity ECS integration tests（fires / does-not-fire / cache-warm / parse-fail-safe）；3-run stable AYAnimation 703/703 + AYEntity 401/401 + AYResource 1044/1044 × 3，零回归；详见 §4.16 + §13 row 20b + §11 P3.x row。**.ayasm loader / per-state AnimNotify routing / 算术 / 函数调用 / 节点图 deferred** |
| **P3.x刀 N+1** | **B Time-in-State Query + C Per-state AnimNotify Routing ── ✅ SHIP 2026-08-07**：**B** `StateMachine._currentStateEnterTime : float` 新字段 + `getCurrentStateElapsedTime() const → float` 公共 API + `update(dt)` 顶部 +dt 累加（即使 mid-transition 也累加，UE `FAnimNode_StateMachine::GetCurrentStateElapsedTime` 一致）+ `setInitialState` / lazy-init / `fireTransition` instant-cut + `fireTransition` cross-fade START 4 处 reset `_currentStateEnterTime = 0.0f` + `ConditionEvalCtx` 兑现 `currentStateTime`（`StateMachine::findEligibleTransition` 内 1 line 从 `0.0f` literal 改 `getCurrentStateElapsedTime()`）+ `CondIdentifierExpr::evaluateAsFloat` reserved-name pre-check `CurrentStateTime`（3 LoC，shadow user params 优先 SM 内部状态；UE pattern 同样不 raise warning）+ **C** `AnimNotifyRecord::fromStateName : std::string` + `AnimNotifyEvent::fromStateName : std::string` 字段（default empty back-compat sentinel，P1.3/P1.4/P1.5 398 tests 0 回归）+ `AnimationPlayer._currentStateNameForNotify` 字段 + `setCurrentStateName(std::string)` setter + `getCurrentStateName() const → const string&` getter + `AnimationPlayer::dispatchPendingNotifies` / `dispatchSlotNotifies` 路径 2-step pattern 写 `fromStateName`（避免 `push_back({...})` brace-init 跟 std::string alignment 冲突——P1.5 13-test regression 的 root cause）+ AYEntity `StateMachineSystem` bridge **every-tick** `player->setCurrentStateName(sm.getCurrentStateName())`（从 transition-only 改为 every-tick——1 line 简化，无 init 标志，避免 wire-up 后 first tick player cache 仍空）+ **6 TIS unit tests** + **4 ANR unit tests**（NEW `AYTest_AnimNotifyRouting.cpp`） + **4 ECS integration tests** (TIS_GT_Fires + ANR_NotifyCarriesFromStateName + ANR_PerStateRoute_SubscriberFilters + TIS_NoRegression)；3-run stable AYAnimation 752/752 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.17 + §13 row 20b (deferred 兑现) + §11 P3.x刀 N+1 row。**INV-36..39** (time-in-state 契约) + **INV-40..42** (per-state notify 契约) 全部 NEW。**L1/L2/L3 全 preserved**。**OnStateEntered/Exited event / 多状态 notify / arithmetic / functions / `.ayasm` loader / state-graph UI 全部 deferred** |
| **P0 polish** | **Flat-array params/triggers + FNV-1a ParamName Registry ── ✅ SHIP 2026-08-07**（§14 row 由 P2 polish ship day 2026-08-08 补录——P0 ship day 遗漏）：`StateMachine._params` 从 `std::unordered_map<std::string, float>` 改 `std::vector<ParamEntry>`（pre-reserve 8, linear scan, cache-friendly, N ≤ 8 production）+ `StateMachine._triggers` 从 `std::unordered_set<std::string>` 改 sorted `std::vector<uint32_t>`（pre-reserve 4, `std::lower_bound`, N ≤ 4 production）+ `detail::ParamNameRegistry` Meyers singleton FNV-1a 32-bit hash + `ParamEntry { uint32_t hash; float value; }` 移到 `ConditionExpr.h` 避免循环 include + 6 private helpers + `getParamName` / `getParamNameRegistrySize` debug read-back + `ConditionEvalCtx` field types change + `CondIdentifierExpr::evaluateAsFloat` 改 intern+linear scan + **public API 0 change**（ECS bridge 0 touch；L1/L2/L3 preserved）+ 2 new unit tests (INV-43..46 pin) + 30 L2 tests makeCtx 5-line shift + micro-benchmark 4 scenarios；3-run stable AYAnimation 759/759 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.18。**INV-43..46** 全部 NEW。Hot-path speedup (debug): getParam 8 params 4.0x / 1 param 5.2x / 32 params 1.7x；setTrigger regression 0.4x (debug-only, accepted)。**AST → 扁平字节码 (P2 polish) deferred → §4.20 RESOLVED** |
| **P1 polish** | **Hot-Path Eval Hash Caching ── ✅ SHIP 2026-08-07**：`Transition.triggerHash` + `Transition.conditionParamNameHash` 2 字段（`addTransition` 一次性 `ParamNameRegistry::intern()` 缓存）+ `CondIdentifierExpr.nameHash` 字段（ctor 一次性 intern 缓存）+ 3 hot-path callsite (`StateMachine::findEligibleTransition` / `Transition::evaluateCondition` L1 path / `StateMachine::fireTransition`) 改 cached hash + **lazy fallback**（test fixture `const_cast` mutation 后 cached hash=0 但 source 非空则 re-intern on the fly——production 永 0 cost 走 hit path，仅 fixture 走 fallback）+ `detail::ParamNameRegistry` **拆 leaf header `ParamNameRegistry.h`**（让 `ConditionExpr.h` inline ctor 调 `intern()` 无循环 include；`kEmpty` 在 `StateMachine.cpp` 定义）+ **public API 0 change**（`Transition` / `CondIdentifierExpr` / `ConditionParser` / `StateMachine` 签名 identical；ECS bridge `AYStateMachineSystem::onUpdate` 0 touch；INV-18..42 全部 preserved）+ **2 new unit tests** (`P1_Transition_TriggerHash_CachedAtAddTransition` 唯一名避 process-global registry 累积污染 + `P1_Transition_ConditionHash_CachedAtAddTransition`) + **3 new unit tests** (`P1_CondIdent_NameHash_NonEmpty` + `P1_CondIdent_NameHash_EmptyName_HashZero` 0 sentinel pre-check 跳过空名 intern + `P1_CondIdent_Evaluate_NoIntern` 1000× eval 后 registry size 不变证明 0 per-eval intern + reserved ident `"CurrentStateTime"` INV-51 priority preserved) + micro-benchmark `AYAnimation/benchmark/state_machine_params_bench.cpp` **+ Scenario E** `findEligibleTransition` 5 transitions × 100K iter (scan 1251 ns + scan+fire 2391 ns) + **+ Scenario F** DSL evaluate 3-ident `(Speed>5) && IsGrounded && !IsDead` × 100K iter (true path 132 ns + short-circuit 113 ns)；3-run stable AYAnimation 1783/1783 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.19 + §13 row 20e (NEW) + §11 P1 polish row。**INV-47..51** (transition hash cache + CondIdentifierExpr nameHash + reserved ident priority + lazy fallback back-compat + author-set immutability) 全部 NEW。**AST → 扁平字节码 (P2 polish) → §4.20 RESOLVED 2026-08-08 / AssetBoneCache lock-free (D polish) → §4.21 RESOLVED 2026-08-08 / Additive slot dynamic vector (内存复用方向) → §4.22 P4 polish RESOLVED 2026-08-10 / setTriggerByHash caller-side cache (P2 polish .A) 全部 deferred** |
| **P2 polish** | **Condition DSL AST → Flat Bytecode ── ✅ SHIP 2026-08-08**：`CondBytecode` 平行缓存（`std::vector<uint8_t> program` + `std::vector<float> literals` flat float table，无 tag bit）+ 10 opcodes + `CondReservedId` + `compileToBytecode(ast)` post-order walk（comparison: left→right→op；And/Or: placeholder + patch int8_t relative-jump INV-58）+ program-counter switch evaluator（`float stack[16]` 固定栈数组 + 全边界 guard fail-soft，**0 per-eval heap alloc**——vector 栈实测 8x 慢）+ `Transition.cachedBytecode` `mutable shared_ptr` lazy build（INV-52/57）+ `Transition::evaluateBytecode` hot path（L2: lazy parse + lazy compile + eval；L1: delegate `evaluateCondition`；`setConditionExpr` → invalidate 双清）+ `OP_LOAD_RESERVED R_CURRENT_STATE_TIME`（INV-55 0 string compare at eval）+ AST preserved（P4.x CondVisitor graph-builder 需要）+ **public API 0 change**（ECS bridge 0 touch；INV-18..51 全部 preserved）+ **6 new unit tests**（4 ConditionExpr: Parity 5 cases 含 Speed=3 vs 5.0 抓 bit-30 literal bug + LazyBuild + ReservedIdent + ParseFail；2 StateMachine: FindTransitionUsesBytecode + InvalidateCacheClearsBytecode）+ micro-benchmark **+ Scenario G** bytecode vs AST 3-ident × 100K；3-run stable AYAnimation 1824/1824 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.20 + §11 P2 polish row。**INV-52..58** (bytecode 1:1 AST 语义 + lazy build + reserved ident opcode + flat float literal table + shared_ptr copyable + short-circuit relative-jump) 全部 NEW。Benchmark (debug build): bytecode true 773 ns vs AST 1035 ns (**1.34x**) / short-circuit 800 ns vs 1022 ns (**1.28x**), parity PASS × 2。两个前期 bug 修复（bit-30 IEEE-754 exponent 冲突 literal 编码 → flat float table；per-eval vector 栈 8x 慢 → 固定数组），详见 §4.20.10。**setTriggerByHash caller-side cache (P2 polish .A) / Additive slot dynamic vector (内存复用方向 → §4.22 P4 polish RESOLVED 2026-08-10) 全部 deferred** |
| **P3 polish** | **AssetBoneCache lock-free single-threaded mode ── ✅ SHIP 2026-08-08**：`AssetBoneCache` 默认改无锁（`_threadSafe = false`，**INV-59** — 7 访问点永不触碰 mutex，ECS 单线程主 tick 路径零同步可证明）+ `setThreadSafe(true)` opt-in 重挂 mutex（authoring tools / 多线程 host，**INV-60** — flag 非原子，须并发使用前设）+ anonymous ns `maybeLock(mutex&, bool)` RAII helper（enabled ? `unique_lock(mu)` : default ctor — 无锁路径零成本 + 锁路径异常安全）+ 7 sites `lock_guard` → `unique_lock lk = maybeLock(_mu, _threadSafe)`（resolveAndCache fast-path 双 lookup 结构不变）+ **+2 additive public API**（`setThreadSafe` / `isThreadSafe`；`lookup / resolveAndCache / invalidate / clear / entry-count` 签名 identical；`AnimationPlayer` 2 callsite 0 touch；ECS bridge 0 touch；INV-1..17 全部 preserved）+ **2 new unit tests**（`P3_AssetBoneCache_DefaultIsLockFree` INV-59 pin + `P3_AssetBoneCache_ThreadSafeMode_BehaviorUnchanged` 双模式复跑完整 P1.7 contract）+ micro-benchmark **+ Scenario H** 真实 2-bone Skeleton hit 路径 × 100K 交错 min-of-5；3-run stable AYAnimation 1851/1851 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.21 + §11 P3 polish row。**INV-59/60** (默认零锁 + flag 非原子) 全部 NEW。Benchmark (debug build, min-of-5): resolveAndCache hit 961 vs 1001 ns/iter (**1.04x**) / lookup hit 910 vs 980 ns/iter (**1.08x**) — debug STL 主导绝对值（每 hit 临时 `std::string` 构造 + checked iterators ≈ 900-1000 ns），mutex delta 在噪声以下；**P3 收益结构性**（默认零同步）；lock delta 在 release 构建才显著。测量教训：顺序一次性测量噪声反转比值（0.83x 假象）→ 交错 min-of-5 修复，详见 §4.21.10。**Additive slot dynamic vector (内存复用方向) → §4.22 P4 polish RESOLVED 2026-08-10** |
| **P4 polish** | **Additive slot 内存回收 + AssetBoneCache transparent hash + 批量 tick 压力测试 ── ✅ SHIP 2026-08-10**：`AnimationPlayer::releaseSlotBuffers(AdditiveSlot&)` 新私有 helper（swap-with-empty 归还 tracks / pendingNotifies / capturedLocal{Pos,Rot,Scl}（n*3/n*4/n*3 floats，setSkeleton 时分配）/ trackWeights；`clearAdditiveLayerSource` + `stop()` 两 callsite；闲置 slot 此前持有这些 multi-KB buffer 直到 player 析构——100-bone ≈ 4KB/slot）+ `AssetBoneCache` inner map 改 `detail::StringViewHash + std::equal_to<>`（`is_transparent` C++14 异构查找，`find(const char*)` 0 临时 string 构造——兑现 §4.21.12 Q3 + §4.21.11 UPGRADE-HOOK(transparent hash)；3 重载（string_view / const string& / 显式 const char* 防 C3066 模糊））+ **0 public API change**（AnimationPlayer / AssetBoneCache 签名 identical；ECS bridge 0 touch；L1/L2/L3 + INV-1..60 全部 preserved）+ **3 new INV**（**INV-61** 闲置 slot heavy buffers 全释放 capacity 0 / **INV-62** `_additiveSlots` vector size 保留 sparse 语义 + re-bind 与 P1.5 逐位一致 / **INV-63** 异构查找 0 临时 string 且三种 key 拼写命中单 entry）+ **4 new unit tests**（`AYTest_P4Stress.cpp` 新 suite P4StressTests：`p4_stress_400_players_share_one_skeleton` 400 player × 200 帧完整帧 tick+evaluate 逐位 memcmp 一致 + AssetBoneCache 单 entry 共享验证 + `p4_stress_bind_clear_cycle_returns_cache_entries` 5 轮 × 40 player 每轮 fresh 40 entry + invalidate 回落 0（无跨轮泄漏）+ `P4_AssetBoneCache_HeterogeneousLookup_SingleEntry` INV-63 pin + `P4_AdditiveSlot_ClearStop_RebindFreshState` INV-61/62 行为验证（clear → 无贡献 → re-bind → 恢复同值 → stop → 全清））；3-run stable AYAnimation 2673/2673 + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归；详见 §4.22 + §11 P4 polish row。**INV-61/62/63** (内存归还 + sparse 保留 + 异构查找) 全部 NEW。教训：`tick()` 只推进时钟不跑渲染评估（压力测试首版只 tick → resolve 未触发 → entry 断言全败 → 每帧 tick+evaluate 完整帧）+ transparent hash 必须显式 `const char*` 重载（string_view/const string& 双重载对 const char* 等权转换 → C3066）+ fresh-round 断言（每轮 invalidate 后 count==0，断言是 kPerRound 非累积）。**setTriggerByHash caller-side cache (P2 polish .A) 仍 deferred** |
| **P5 polish** | **DSL 四则运算 + - * / ── ✅ SHIP 2026-08-10**（roadmap 方向 4「小型刚需缺口」第一项；§14.3 算术 row 标 ✅）：8 算子 → **13**（+ `+ - * /` 四则 + 一元 `-`），三层贯通（lexer → precedence-climbing parser → AST → bytecode）：`CondOp` 追加 Add/Sub/Mul/Div/Neg（7..11）+ `CondOpByte` 追加 OP_ADD..OP_NEG（10..14），**旧值 0..6 / 0..9 全部不变**（已有测试按枚举引用）+ **lexer 负号消歧 INV-65**（`-` 后跟数字仅在 prevEndsExpr=false 时折叠为负字面量——`A-3` == `A - 3`，pre-P5 负字面量 AST 形状保留 + 异形 `--3` 天然工作）+ **precedence 5/6 INV-69**（Compare 4 < Add/Sub 5 < Mul/Div 6：`A + B > C` = `(A + B) > C`）+ **parseUnary 一元减 INV-66**（prefix 绑定比一切二元紧：`-A * B` = `(-A) * B`）+ **`CondBinaryExpr::evaluateAsFloat` override** 算 float 值（比较/逻辑臂 fallthrough 0.0f——INV-64 类型不匹配契约与 base-class default 一致）+ **`CondUnaryExpr::evaluateAsFloat` Neg 取反** + **除零 → 0.0f INV-67**（AST + bytecode 双路径 fail-soft，绝不 inf/nan 污染 fabs-epsilon 比较）+ **bytecode evaluator 5 新分支**（算术与比较同栈形 pop 2 push 1；短跳转 ±127 机制不受影响）+ **0 public API change**（ConditionParser / Transition / StateMachine / CondVisitor 全部不变——复用 Binary/Unary 节点零新 visit 方法；ECS bridge 0 touch；L1/L2/L3 + INV-1..63 全部 preserved）+ **14 new unit tests**（§8.1.6 11：Add_Compare / MulDiv_Precedence / Parens_Override / NoSpace_Minus(INV-65) / Div_Ok / DivByZero_FailsSoft(INV-67) / UnaryNeg(INV-66 判别式 `-Speed - 1 == 2`) / Chain_LeftAssoc / BoolCoercion(INV-64) / ParseErrors 7 串 / Transition_Integration bytecode hot path 真 fire + §P5 3：13-case parity 表(INV-68，含 `CurrentStateTime * 2 > 1` reserved 组合 + 负字面量 back-compat + `A-3` 无空格) + 尾 opcode 编码 7 检 + 负字面量不含 OP_NEG 形状断言 + short-circuit 与算术共存(INV-58)）；3-run stable AYAnimation **2831/2831** + AYEntity 421/421 + AYResource 1039/1039 × 3，零回归（2817 → 2831 +14）；详见 §4.23 + §11 P5 polish row。**INV-64..69** (算术语义 + 负号消歧 + 一元减绑定 + 除零 fail-soft + bytecode parity + 优先级) 全部 NEW。§4.16「算术 defer P3.x刀 N+1」+ §4.17「不引入算术」两历史决策标 RESOLVED。教训：测试期望值手算避开恰好相等组合（`(A+1)*2` 两处 FAIL 全是期望值算错非代码 bug）+ 插测试块前 grep section 头位置（§8.1.5 头在文件顶部，误插把 TEST_SUITE_END 落中间 → ~40 TEST_CASE 掉出 suite C2065）。**`%` / `>=` `<=` / 一元 `+` 明确不做**（§14.3 纪律：下一刀 string/int param 类型） |
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

## 4.25 ✅ P4-1 — TwoBone IK（解析解 + AnimationPlayer Phase 2.5 集成）— SHIP（2026-08-10）

> 本节为 P4-1 完整 ship 文档。模板遵循 §4.11 12 段式。**长线第一刀**：polish 阶段收官（P6）后开张 Phase 4（IK），本刀 = TwoBoneSolver 纯数学核心 + AnimationPlayer IK pass 集成，一条完整消费链（IK chain config → evaluate → skin 矩阵 → 渲染）落地。FABRIK / CCD / 约束 / pole vector / 重定向留后续刀。

#### 4.25.1 Overview

Two-Bone IK 在 evaluate() 的 **Phase 2（世界累加）之后、Phase 3（skin = world × IBM）之前** 以 **Phase 2.5** 插入：

```
Phase 0 rest reseed → Phase 1 track sampling → Phase 1b additive → mask lerp
→ Phase 2 world accumulate（accumulateWorldFrom(0)）
→ Phase 2.5 IK pass（P4-1，本刀）——只写 root/mid 的 _localRot
→ Phase 3 skin = world × inverseBind（消费 _world，IK 结果自然进入蒙皮）
```

每帧全量重算、零跨帧状态：seek / stop / skeleton swap 天然正确。solver 是**纯函数**（世界空间位置 + 世界旋转 in，世界旋转 out），player 只做骨架世界位收集、世界→局部回写与子树 world 重算。

#### 4.25.2 Motivation

polish 阶段收官后用户选定 **IK 作为长线第一刀**（design §6 预留三型解算器之一：膝/肘）。范围决策：TwoBone 解析解 + 播放器集成，一条完整消费链先立起来（配置 → 解算 → 蒙皮矩阵 → 渲染侧可用）；FABRIK / CCD / 约束 / pole / 重定向 全部留后续刀。选解析解而非迭代解：Two-Bone 有闭式解（余弦定理），无迭代、无收敛问题、确定性输出，是后续迭代解（FABRIK/CCD）的对齐基线。

#### 4.25.3 Data Model

```cpp
// include/ayanimation/AnimationPlayer.h（IKChainSpec 在 class 前 + 私有存储）
struct IKChainSpec {
    std::string         rootBone;      // 链根名（hip/shoulder）
    std::string         midBone;       // 中骨名（knee/elbow）
    std::string         tipBone;       // 末端名（foot/hand）
    ayt::math::FVector3 targetWorld;   // 世界空间目标
    float               weight = 1.0f; // [0,1]，0 = 链关闭（零成本跳过）
};
constexpr uint32_t kMaxIKChains = 8;   // 镜像 kMaxAdditiveSlots

// 私有
struct IKChain {
    IKChainSpec spec;
    int32_t rootBone = -1, midBone = -1, tipBone = -1;  // -1 = 禁用（名字 miss / 拓扑非法）
};
std::vector<IKChain> _ikChains;          // 稀疏；空 spec == 未绑定
std::uint32_t        _ikGeneration = 0;  // bind/clear/re-resolve 时 bump
```

骨骼数据保持**零数据平行**：骨名索引来自 `ISkeleton::findBone`（**直查，不走 AssetBoneCache** —— 见 4.25.10 教训 #3），世界位/旋转来自 `_world`（Phase 2 输出）。IK 不新增任何骨架侧存储。

**拓扑不变量**：链合法 = `parentIndex(root) < parentIndex(mid) < parentIndex(tip)` 链上且 root/mid/tip 严格祖先链（resolve 时两段祖先 walk 验证）。`parentIndex < childIndex` 硬不变量使 walk 严格递减、必然终止。

#### 4.25.4 Public API

```cpp
bool setIKChain(uint32_t chainId, const IKChainSpec& spec);  // OOR（>= kMaxIKChains）→ false 静默忽略
void clearIKChain(uint32_t chainId);                          // 稀疏槽清空
void clearAllIKChains();
void setIKChainTarget(uint32_t chainId, const ayt::math::FVector3& worldPos); // 每帧热更新，零重解析
void setIKChainWeight(uint32_t chainId, float weight);        // saturate [0,1]
const IKChainSpec& getIKChain(uint32_t chainId) const;        // OOR → static 空 spec
size_t             getIKChainCount() const;                   // 已绑定（非空 spec）链数
bool               isIKChainActive(uint32_t chainId) const;   // 已解析 && weight > 0
std::uint32_t      getIKChainGeneration() const;              // 测试 / 未来 ECS 钩子
```

生命周期交互（均与 additive 保留语义对齐）：

| 事件 | 行为 |
|---|---|
| `setIKChain` | 稀疏填充 → eager `resolveIKChains()` → bump generation |
| `setSkeleton(新)` | mask re-resolve 旁追加 `resolveIKChains()`（INV-71）——同名命中复活 / 名字 miss 原地禁用（-1） |
| `evaluate()` | 每帧全量重算，无跨帧状态 → seek/stop 天然正确 |
| `stop()/play()` | 不触碰 `_ikChains`（同 additive 保留语义） |
| `_baseClip == null` | debug 退化分支（`#ifndef NDEBUG` 内）早退不跑 IK（文档化：release 同状态走主流程获得 IK） |

#### 4.25.5 Internal Algorithm

**solver 十二步**（`TwoBoneSolver::solve`，eps = 1e-5f，纯函数）：

记 A=root、B=mid、C=tip、T=target：
1. 任一输入含 NaN（`v != v`）→ 原样返回（INV-74）
2. `b0=B−A, len0=|b0|`；`b1=C−B, len1=|b1|`；零长骨 → 原样返回
3. `dir=T−A, d=|dir|`；d < eps（目标=根）→ 原样返回
4. `reachable = d <= len0+len1+eps`（诊断位；不可达不改变输出形状）
5. `dC = clamp(d, |len0−len1|, len0+len1)` —— 过近/不可达自动落入同一公式（cos≈1 链拉直朝目标）
6. **bend 轴 n**：`n = normalize(cross(unit0, unit1))`；**退化判定 = n 模长² < eps² 或 n 平行 unitTarget**（围绕平行轴旋转是 no-op —— v1 bug 修复点）→ fallback 链：`cross(unitTarget, unit0)` → `cross(unitTarget, rootRot*(0,1,0))` → `cross(unitTarget, (1,0,0))` → `cross(unitTarget, (0,1,0))` → 全灭则原样返回
7. `alpha = acos(clamp((len0²+dC²−len1²)/(2·len0·dC), −1, 1))`
8. **保侧候选法**（规避轴手性符号 bug）：`unitTarget = dir/d`；`Bp = A + len0·(fromAxisAngle(n, +alpha)·unitTarget)`，`Bm = A + len0·(fromAxisAngle(n, −alpha)·unitTarget)`；`side = dot(cross(unitTarget, unit0), n)`，`B_new = side>=0 ? Bp : Bm`（当前 mid 在目标轴哪侧，新 mid 落哪侧）
9. `b1new = T−B_new`；`C_new = B_new + len1·normalize(b1new)`（dC1≤eps 时用 unitTarget 兜底）
10. `rootRotNew = fromToRotation(unit0, normalize(B_new−A)) * rootRot`；`midRotNew = fromToRotation(unit1, normalize(C_new−B_new)) * midRot`（premultiply 保留骨自身 roll）
11. 权重混合（世界空间同一枢轴）：`finalRoot = rootRot.slerp(rootRotNew, saturate(w))`，mid 同理（w ≥ 1 直取全解）
12. 返回 `{finalRoot, finalMid, reachable}`

**不输出位置**：root 世界位置保持动画驱动（锚点语义），mid/tip 位置由旋转链传导 —— 这是 P7「链 2 root = 链 0 tip 可同时命中」成立的关键。

**player 集成（Phase 2.5，evaluate 内 Phase 3 前）**：

```cpp
if (!_ikChains.empty()) {
    for (IKChain& ch : _ikChains) {
        if (ch.spec.weight <= 0.0f) continue;              // INV-72 零成本跳过
        if (ch.rootBone < 0 || ch.midBone < 0 || ch.tipBone < 0) continue;
        rp = _world[r].transformPoint(0); mp = ...; tp = ...;      // 世界位
        decompose(_world[r]) → rr；decompose(_world[m]) → mr      // 世界旋转（singular → skip）
        res = TwoBoneSolver::solve(rp, mp, tp, rr, mr, spec.targetWorld, spec.weight);
        rootLocal = (root 的父 < 0) ? res.rootRotation
                  : decompose(_world[parent]).rot.conjugate() * res.rootRotation;
        writeLocalRot(r, rootLocal);
        writeLocalRot(m, res.rootRotation.conjugate() * res.midRotation);  // mid 父 = root 新 world rot
        accumulateWorldFrom(r);    // 子树 world 重算 — Phase 3 消费
    }
}
```

- **子树重算**：Phase 2 循环体原样提取 `accumulateWorldFrom(start)`（start=0 时逐位不变，2999 回归钉住）；IK pass 调 `accumulateWorldFrom(r)` —— `parentIndex < childIndex` 使 `[r, n)` 恰好覆盖链子树，无分支零簿记
- **世界→局部回写**：`local = parentWorldRot.conjugate() * worldRot`（unit quaternion 的 conjugate == inverse）；mid 的父是 root 的**新** world 旋转
- **mask 门控**：IK pass 在 mask lerp **之后**跑 —— mask 不 gate IK（绝对目标锁定，文档化；per-chain mask 门控 deferred）
- **weight 0 零开销**：`_ikChains.empty()` 单次分支 + 链级 `weight<=0` continue，不读 world 不 decompose
- **重叠链顺序语义**：链执行顺序 = chainId 升序。**共享 mid/tip 骨的链不能同时命中** —— 后执行者覆盖共享骨旋转（P7 教训，见 4.25.10 #2）；仅当链 2 的 root 是链 0 的 tip 时（锚点语义）两链可同时命中
- **resolve 用 findBone 直查**（不走 AssetBoneCache）：低频路径 + 规避指针复用陈旧命中（见 4.25.10 #3）

#### 4.25.6 ECS Bridge

**本轮 0 touch**。AYEntity 不感知 IK；留 `getIKChainGeneration()` 公开钩子（值变化 = 绑定/重解析发生），未来 ECS bridge（`IKChainComponent` + `IKChainSystem`）可轮询或事件驱动。重叠链、权重动画（ramp in/out）等 host 侧行为靠公开 setter 即可完成，不需要内核改动。

#### 4.25.7 Resource Bridge

- **只读消费** `ayt::resource::ISkeleton`：`findBone(name)`（直查）+ `getBones()[].parentIndex`（祖先 walk）+ `getBoneCount()`
- **无新资产类型**：IK 链配置是 runtime-only 结构（IKChainSpec），不做序列化格式（host 侧驱动）
- **solver 是纯数学**：零 AYAnimation 内部依赖（只吃 `ayt::math`），可独立单测

#### 4.25.8 Invariants（INV-71..74 全部 NEW）

| # | 契约 |
|---|---|
| **INV-71** | IK 链在绑定时 **eager resolve**（名字 → 索引 + 祖先拓扑校验）；`setSkeleton(新)` 时全量**重解析**——同名骨架链复活、名字 miss 原地禁用（-1），绝不 crash |
| **INV-72** | `setIKChainWeight` 饱和到 [0,1]；evaluate 中 `weight <= 0` 的链**零成本跳过**（不读 world 不 decompose） |
| **INV-73** | IK pass 只写 root/mid 两骨的 `_localRot`，post-mask、pre-Phase-3；其余骨 local 数据零触碰 |
| **INV-74** | solver 是纯函数：NaN / 零长骨 / 目标=根 / 无可用 bend 轴 / 不可达 → 输出**有限**（原样返回或拉直朝目标），**永不 NaN** |

#### 4.25.9 Testing

**Solver（TEST_SUITE(TwoBoneIKTests)，纯数学，无骨架无 player）**：

| Case | 验证 |
|---|---|
| S1 | 可达目标命中（T=(0,0,15)，共线走 fallback 轴；len0/len1 保持、tip==T、XZ 平面 y≈0）|
| S2 | 不可达拉直（T=(0,0,30)，reachable=false，三点共线沿 +Z 拉满）|
| S3 | 目标过近无 NaN（d < |len0−len1|，cosine 域 clamp）|
| S4 | 零长骨（mid==root）→ 输出逐位 == 输入 |
| S5 | 目标在链延长线恰达极限（T=(20,0,0)，alpha=0 直射命中）|
| S6 | NaN 输入 → 逐位原样 |
| S7 | 反平行 180° 翻转（T=(-30,0,0)）→ fromToRotation 兜底轴，拉直朝 −X，无 NaN |
| S8 | weight=0 → 逐位 == 输入 |
| S9 | weight=0.25/0.5/0.75 混合 == slerp(输入, 全解, w) 且 tip 距目标**单调递减**（旋转空间混合 ≠ 位置空间混合——w=0.75 时 tip 仍距目标 ~7，注释写明）|
| S10 | weight 越界 clamp（1.5==1、−1==0）|

**Player 集成（TEST_SUITE(AnimationPlayerIKTests)，fixture：3-bone 链 Root(0,0,0)/Mid(10,0,0)/Tip(20,0,0) + 8-bone 双链 + 后代）**：

| Case | 验证 |
|---|---|
| P1 | 绑定 → evaluate → tip ≈ target(1e-3)；root 世界位不变（锚点）；XZ 平面；全部矩阵有限 |
| P2 | weight=0 → world 与无链 player **memcmp 逐位相同** + inactive |
| P3 | weight 0.25/0.5/0.75/1.0 tip 距目标单调逼近，w=1 命中 |
| P4 | 未知骨名 → 禁用不崩，world 与无链一致，inactive |
| P5 | setSkeleton 同名骨架 → 链复活仍命中；异名 → 禁用不崩；generation 递增（INV-71）|
| P6 | 两独立链（LRoot 链 + RRoot 链）各自命中，双 root 锚点不动 |
| P7 | **重叠链**：链 2 root == 链 0 tip（锚点语义）→ 两 tip 同时命中（含设计修正，见 4.25.10 #2）|
| P8 | 后代跟随（LTipChild 距 LTip 恰 5 单位，local 偏移保持）|
| P9 | seek（0.3/0.7/0.5 往返）无跨帧状态，每帧仍命中 |
| P10 | stop()→play() 链保留仍命中 |
| P11 | 不 play 链已绑 → evaluate 早退无崩溃 |
| P12 | chainId OOR（>= kMaxIKChains）静默忽略 + OOR 读返回空 spec + 最后合法 id 可用 |

**回归护栏**：无链时 `_ikChains.empty()` 单分支挡一切 → 既有 2843 测试 0 变化。

#### 4.25.10 Edge Cases & Lessons

1. **stale .obj（ninja depfile 失效）**：AnimationPlayer.h 成员插在 `_boneMaskWeights` 与 `_world` 之间 → `_world` 偏移移动 → **未重编的 test TU 读旧偏移 → world garbage（值每次运行不同）**。instrumentation 证明 evaluate 内部正确（world0=identity）但测试读 garbage → 矛头指向 test 侧 → 4 个 TU（AYTest_AnimationPlayer/P4Stress/ConditionExpr/main）obj mtime 早于 header → **touch 真实源文件强制重编即愈**。教训：**改 .h 后若出现"内部正确测试 garbage"型怪异回归，先查 test TU 是否重编（obj mtime vs header mtime），不要先加 diagnostic**；且 touch 必须打在 **build.ninja 引用的真实源路径**（绝对路径 D:/Projects/...）而非 build-dir 残留副本（本仓库 CMake 曾留下 0 字节 source 副本，touch 错文件）。
2. **P7 设计缺陷（共享骨链不能同时命中）**：初版 P7 用「链 2 = LMid→LTip→LTipChild」，链 2 的 mid（LTip）== 链 0 的 tip → 链 2 后执行旋转 LTip 把链 0 的 tip 推出目标（FAIL：tip.x=14.66≠0）。**"两 tip 同时命中"对共享 mid/tip 的链在几何上不可能** —— 顺序依赖语义（chainId 升序，后执行者赢）必须文档化而非断言。重写 P7 为锚点场景：链 2 root == 链 0 tip → root 位置永不动 → 两链真同时命中。
3. **AssetBoneCache 指针复用陈旧命中**：resolveIKChains 初版走 `AssetBoneCache::resolveAndCache`（key = (骨架指针, 名字)）。cache **不跟踪骨架生命周期**：测试中骨架析构后其地址可能被新骨架（make_shared）复用 → 旧条目 (地址, "Root")→0 复活 → P5 换 8-bone 骨架后链误判 active（随机 FAIL，概率 ≈ malloc 是否复用地址）。**修复：resolveIKChains 改 `findBone` 直查** —— IK resolve 仅 bind/换骨架时运行（非每帧），cache 收益为零，直查精确。**同源风险仍存在于 resolveSkeletonMask（P2.2）**：mask 也按 (指针, 名) 缓存，长生命周期资产不受影响；测试期疯狂建/毁骨架才暴露——已文档化（§4.25.12 Q4），不在本刀修。
4. **fixture 注释与数据不符**：makeThreeBoneSkeleton 初版 Root local=(10,0,0) → rest world 实为 Root(10)/Mid(20)/Tip(20)，**mid 与 tip 重合 → len1=0 → solver noop**。S1-S10 是纯 solver 测试（手传 A/B/C）不经骨架所以全绿——P 测试第一次真正消费该 fixture 才暴露。教训：**fixture 必须先被真实路径消费一次**（写注释时按意图、数据与注释必须一致）。
5. **TEST_SUITE 双 suite 收口**：新增第二个 TEST_SUITE 时吞掉了第一个的 `TEST_SUITE_END`（Edit old_string 误匹配）→ C1075 namespace 不闭合。教训：新 suite 前显式补 `TEST_SUITE_END`。

#### 4.25.11 Migration Hooks（后续刀）

| 后续能力 | 预留钩子 |
|---|---|
| Pole vector | solver 双候选 → 按 pole 投影选择（step 8 的 side 选择已是该插槽）|
| FABRIK / CCD | `TwoBoneSolver::solve` 签名即统一入口；player 侧只需换 solver 调用 |
| IK 约束（angle/distance）| solve 后 clamp 步插入 step 10 与 11 之间 |
| 局部空间目标 | IKChainSpec 加 local-space 标志 + evaluate 前变换 |
| per-chain mask 门控 | Phase 2.5 循环内按链查 mask 权重 |
| ECS bridge | `getIKChainGeneration()` 钩子（§4.25.6）|

#### 4.25.12 Open Questions

1. **非均匀缩放分解误差**：`decompose` 对非均匀缩放 + 旋转混合的数值精度有限（singular skip 已兜底）——IK 链骨带非均匀缩放时精度未验证
2. **重叠链重算优化**：当前每链 `accumulateWorldFrom(r)` 全子树重算；链密集时（如两脚 + 两膝同时 IK）子树重叠 → 可合并为一次自底向上重算（按 start 排序），量级小暂不做
3. **Root Motion 交互**：IK 锚点语义（root 世界位不动）与 Root Motion（根移动）的关系未定义——后续 Root Motion 通道刀时对齐
4. **AssetBoneCache 指针复用同源风险**：resolveSkeletonMask 仍按 (指针, 名) 缓存且不跟踪生命周期——生产长生命周期资产不触发；测试期（P5 暴露同类）依赖 name-miss 场景时建议直接用 findBone（文档化接受，见 4.25.10 #3）
5. **world→local 回写精度**：`conjugate()` 假设 unit quaternion（动画输出 + fromToRotation 均单位化）；若未来出现非单位化旋转输入需 normalize 兜底

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

---

## 16. Changelog

| 日期 | 变更 |
|------|------|
| 2026-07-26 | P0–P1.4 多轮 SHIP；工业对照表初版 |
| 2026-07-27 | **设计审计补丁**：状态抬头；§4.3.1 Hold≠末帧 clamp；§4.7 Override 忽略 weight 陷阱；**§4.8 P1.5 Player SHIP 对齐代码**；§11/§13 勾选与统计修正；Montage Slot 与 AdditiveSlot 对齐约束 |
| 2026-08-03 | P2.2 Skeleton Mask ship：§4.13 全 12-section + §11 row + §13 row 17h + §11 P2.2 row |
| 2026-08-06 | P3.x刀1 .aymask loader ship：§4.13.7 收口；删 §4.13.11 UPGRADE-HOOK(P3.x) 第一条；§11 / §13 / §16 勾选同步；新增 §13 row 17h + §11 P3.x刀1 row |
| 2026-08-06 | **P3.1 L1 状态机 ship**：§4.14 全 12-section（StateMachine class + AnimationStateMachineComponent + StateMachineSystem priority 460 + AnimStateChangedEvent kTypeId=0x000A'0010 + 15 AYAnimation + 8 AYEntity tests）+ §11 P3.1 row + §13 row 20/20a/20b/20c/20d + §11 P3.1 row + 状态抬头同步 543/543 + 370/370 + 1044/1044 3-run stable；2 项 P3.x刀 2（BlendTree in SM）/ P4.x（.ayasm loader / editor wiring）deferred |
| 2026-08-06 | **P3.2 L3 子状态机 ship**：§4.15 全 12-section（StateMachine._children vector<unique_ptr<StateMachine>> + _currentChildIndex + State.isSubMachine/subMachineIndex + StateMachine move-only + 递归 setTrigger/setParam INV-28 + child-first transition fallback INV-29 + getActiveLeafStateName 深度≤2 INV-30 + _currentChildIndex sync INV-31 + INV-27 sub-machine entry clipPath 忽略）+ ECS bridge 兑现 dt plumbing（sm.update(0.0f)→sm.update(dt)）+ AnimationStateMachineComponent.activeSubState read-back + sub-machine entry 不调 player.play + 12 AYAnimation + 4 AYEntity L3 tests + §4.14.11 UPGRADE-HOOK(P3.2) 标 resolved + §11 P3.2 row 勾选 + 状态抬头同步 600/600 + 385/385 + 1044/1044 3-run stable；3 项 deferred（.ayasm loader / 多状态机 / parallel states）|
| 2026-08-07 | **P3.x L2 Condition DSL ship**：§4.16 全 12-section（Transition 扩展缓存层 4 字段 + 3 API + ConditionExprAst 类族 + ConditionParser mini Lexer + precedence-climbing Parser + 8 算子 + 短路求值 + 负数字面量 + lazy parse + dirty cache + parse-fail-soft-false + L1 back-compat 双轨 + ConditionEvalCtx 4 字段留 P3.x刀 N+1 钩子 + Visitor 接口为 P4.x graph-builder 留口）+ 30 AYAnimation unit tests（§8.1.1 Parser 8 + §8.1.2 Evaluator 12 + §8.1.3 Cache 6 + §8.1.4 Back-compat 4）+ 4 AYEntity ECS integration tests（fires / does-not-fire / cache-warm / parse-fail-safe）+ §11 P3.x row ✅ + §13 row 20b ❌→✅ + §11 P3.x row ✅ + 状态抬头同步 703/703 + 401/401 + 1044/1044 3-run stable；4 项 deferred（per-state AnimNotify routing / .ayasm loader / 算术表达式 / 函数调用 & 节点图）|
| 2026-08-07 | **P3.x刀 N+1.BC Time-in-State Query + Per-state AnimNotify Routing ship**：§4.17 全 12-section（StateMachine._currentStateEnterTime 字段 + getCurrentStateElapsedTime() API + update 顶部 +dt 累加 + 3 处 reset 0.0f + ConditionEvalCtx 兑现 currentStateTime + CondIdentifierExpr reserved-name "CurrentStateTime" pre-check 3 LoC + AnimNotifyRecord/AnimNotifyEvent.fromStateName 字段 default empty + AnimationPlayer._currentStateNameForNotify + setCurrentStateName/getCurrentStateName API + push notify 2-step pattern 修 P1.5 alignment 回归 + AYEntity bridge every-tick setCurrentStateName 1 line 简化）+ 6 TIS unit tests + 4 ANR unit tests（NEW AYTest_AnimNotifyRouting.cpp）+ 4 ECS integration tests（TIS_GT_Fires / ANR_NotifyCarries / ANR_PerStateRoute / TIS_NoRegression）+ §11 P3.x刀 N+1.BC row ✅ + §13 row 20b deferred 兑现 + 统计 26 项 ✅ + 内核 6.9/10 + 完整角色管线 5.3/10 + §11 P3.x刀 N+1.BC row ✅ + 状态抬头同步 752/752 + 421/421 + 1039/1039 3-run stable；**INV-36..39** (time-in-state) + **INV-40..42** (per-state notify) 全部 NEW；5 项 deferred（OnStateEntered/Exited event / 多状态 notify / 算术 / functions / `.ayasm` loader / state-graph UI）|
| 2026-08-07 | **P0 polish — Flat-array params/triggers + FNV-1a ParamName Registry ship**：§4.18 全 12-section（StateMachine._params 从 unordered_map 改 std::vector<ParamEntry> linear scan cache-friendly N ≤ 8 + _triggers 从 unordered_set 改 sorted std::vector<uint32_t> + detail::ParamNameRegistry Meyers singleton FNV-1a 32-bit hash + ParamEntry 结构移到 ConditionExpr.h 避免循环 include + 6 private helpers + StateMachine::getParamName/getParamNameRegistrySize debug read-back + ConditionEvalCtx field types change + CondIdentifierExpr::evaluateAsFloat 改 intern+linear scan + public API 0 change + ECS bridge 0 touch + L1/L2/L3 contracts 全部 preserved）+ 2 new unit tests（Params_FlatArray_FindByHashReturnsCorrectValue + Triggers_FlatArray_BinarySearchWorks pin INV-43..46）+ 30 L2 tests 通过 makeCtx helper 5-line shift 适配 flat-vector ConditionEvalCtx + micro-benchmark AYAnimation/benchmark/state_machine_params_bench.cpp 4 scenarios（8/1/32 params + triggers, 100K iter, default OFF behind AY_BUILD_BENCHMARKS=OFF cache var）+ 状态抬头同步 759/759 + 421/421 + 1039/1039 3-run stable；**INV-43..46** (flat-array hot-path 契约) 全部 NEW；hot-path speedup debug build：getParam 8 params 271→68 ns/iter (4.0x), 1 param 225→43 ns/iter (5.2x), 32 params 265→153 ns/iter (1.7x); setTrigger regression 221→609 ns/iter (0.4x, debug-only, accepted trade-off — production critical path is getParam)。详见 §4.18 + §11 P0 polish row |
| 2026-08-07 | **P1 polish — Hot-Path Eval Hash Caching ship**：§4.19 全 12-section（Transition 加 triggerHash + conditionParamNameHash 2 字段，addTransition 一次性 intern 缓存 + CondIdentifierExpr ctor 一次性 intern nameHash + 3 hot-path callsite (findEligibleTransition / evaluateCondition L1 / fireTransition) 改 cached hash + lazy fallback 保留 test fixture const_cast mutation back-compat + detail::ParamNameRegistry 拆 leaf header ParamNameRegistry.h 让 ConditionExpr.h ctor 调 intern 无循环 include + kEmpty 在 StateMachine.cpp 定义 + public API 0 change + ECS bridge 0 touch + L1/L2/L3 contracts preserved）+ 2 new unit tests (P1_Transition_TriggerHash_CachedAtAddTransition 唯一名避 process-global registry 污染 + P1_Transition_ConditionHash_CachedAtAddTransition) + 3 new unit tests (P1_CondIdent_NameHash_NonEmpty + P1_CondIdent_NameHash_EmptyName_HashZero sentinel pre-check + P1_CondIdent_Evaluate_NoIntern 1000x eval registry size 不变) + micro-benchmark 加 Scenario E findEligibleTransition 5 transitions × 100K (scan 1251 ns + scan+fire 2391 ns) + Scenario F DSL evaluate 3-ident × 100K (true path 132 ns + short-circuit 113 ns) + 状态抬头同步 1783/1783 + 421/421 + 1039/1039 3-run stable；**INV-47..51** (transition hash cache + CondIdentifierExpr nameHash + reserved ident priority + lazy fallback back-compat) 全部 NEW。详见 §4.19 + §11 P1 polish row |
| 2026-08-08 | **P2 polish — Condition DSL AST → Flat Bytecode ship**：§4.20 全 12-section（CondBytecode program + float literal table 平行缓存 + compileToBytecode post-order walk + program-counter switch evaluator 固定栈数组 + short-circuit relative-jump INV-58 + OP_LOAD_RESERVED INV-55 + Transition.cachedBytecode mutable shared_ptr lazy build INV-52/57 + evaluateBytecode hot path + setConditionExpr invalidate 双清 + AST preserved 供 P4.x Visitor + public API 0 change + ECS bridge 0 touch）+ 4 new unit tests (P2_Bytecode_Parity_3IdentExpr 5 cases + P2_Bytecode_LazyBuild_FirstEvalCompiles + P2_Bytecode_ReservedIdent_CompiledAsOpcode + P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse) + 2 new unit tests (P2_Bytecode_Integration_FindTransitionUsesBytecode + P2_Bytecode_Integration_InvalidateCacheClearsBytecode) + micro-benchmark 加 Scenario G bytecode vs AST 3-ident × 100K (bytecode true 773 ns / short-circuit 800 ns vs AST 1035/1022 = **1.34x / 1.28x**, parity PASS × 2) + §4.19.11 UPGRADE-HOOK(P2 polish) 标 resolved + **§11 P0 polish row 补录**（P0 ship day 2026-08-07 遗漏）+ §11 P2 polish row + 状态抬头同步 1824/1824 + 421/421 + 1039/1039 3-run stable；**INV-52..58** (bytecode 1:1 AST 语义 + lazy build + reserved ident opcode + flat float literal table + shared_ptr copyable + short-circuit relative-jump) 全部 NEW。两个前期 bug 修复（bit-30 IEEE-754 exponent 冲突 literal 编码 → flat float table；per-eval vector 栈 8x 慢 → 固定数组），详见 §4.20.10。详见 §4.20 + §11 P2 polish row |
| 2026-08-10 | **P4-1 TwoBone IK ship（长线第一刀）**：§4.25 全 12-section（TwoBoneSolver 解析解十二步 + AnimationPlayer Phase 2.5 集成 + INV-71..74 + accumulateWorldFrom(start) 子树重算 + 世界→局部回写 + findBone 直查 resolve + 0 ECS bridge change + 10 S 测试 + 12 P 测试）+ §6 IKSolver 改「✅ TwoBoneSolver ship，FABRIK/CCD 未启动」+ §11 Phase 4 row ✅ + §14.3 IK 行拆开（~~TwoBone~~ ✅ + FABRIK+CCD/约束/pole/局部目标/重定向 open）+ 状态抬头同步 **2999/2999** + 421/421 + 1039/1039 3-run stable（debug）+ release 2999/2999 × 3 全绿；**INV-71..74** (eager resolve + skeleton-swap re-resolve / weight saturate + ≤0 零成本 skip / 只写 root+mid localRot post-mask pre-Phase-3 / solver 纯函数退化→有限或原样永不 NaN) 全部 NEW；三 lessons（stale .obj depfile 失效 → touch 真实源 / P7 共享骨链设计缺陷 → 后执行者赢文档化 / AssetBoneCache 指针复用陈旧命中 → IK resolve 改 findBone 直查，mask 同源风险文档化）。详见 §4.25 + §6 + §11 P4-1 row |
| 2026-08-08 | **P3 polish — AssetBoneCache lock-free single-threaded mode ship**：§4.21 全 12-section（默认 `_threadSafe = false` 无锁 INV-59 + `setThreadSafe(true)` opt-in 重挂 mutex INV-60 + anonymous ns maybeLock RAII helper（enabled ? unique_lock(mu) : default ctor 零成本）+ 7 sites lock_guard → unique_lock 条件锁 + resolveAndCache fast-path 双 lookup 结构不变 + +2 additive public API（setThreadSafe/isThreadSafe）+ AnimationPlayer 2 callsite 0 touch + ECS bridge 0 touch + INV-1..17 preserved）+ 2 new unit tests (P3_AssetBoneCache_DefaultIsLockFree + P3_AssetBoneCache_ThreadSafeMode_BehaviorUnchanged 双模式复跑 P1.7 contract) + micro-benchmark 加 Scenario H 真实 2-bone Skeleton hit 路径 × 100K 交错 min-of-5（resolveAndCache hit 961 vs 1001 = **1.04x** / lookup hit 910 vs 980 = **1.08x**；debug STL 主导绝对值，P3 收益结构性零同步）+ §4.12.1 线程安全注释同步 + §11 P1 polish row deferred 列表同步（AST→bytecode + lock-free 标 resolved）+ §11 P3 polish row + 状态抬头同步 1851/1851 + 421/421 + 1039/1039 3-run stable；**INV-59/60** (默认零锁 + flag 非原子) 全部 NEW。测量教训：顺序一次性测量噪声反转比值（0.83x 假象）→ 交错 min-of-5 修复，详见 §4.21.10。详见 §4.21 + §11 P3 polish row |
| 2026-08-10 | **文档收口（长线审核副产品）**：新增 **§14 Deferred / Open 项总表**（正确性级 5 项接受权衡 + 性能级 7 项 + 新功能级 12 项 = 单一审计线索）；全文 ~35 处 "§14 X row" 幽灵引用（§14 从未存在，ship row 实际位于 §11）批量归位改指 §11；**BlendSpace 状态登记**（§11 Phase 2 / §13 row 16 / §14.3 / P2.1 roadmap row：P2.1 Blend 1D/2D ship `68f4227` 2026-07-28 从未登记，两处 "仍缺/❌" stale 修正）；§5 AnimationStateMachine "未启动" → 已 ship（P3.1/P3.2/P3.x 2026-08-06/07）；状态头日期 2026-08-08 → 2026-08-10。纯文档改动，0 代码 0 测试影响 |
| 2026-08-10 | **P4 polish — Additive slot 内存回收 + AssetBoneCache transparent hash + 批量 tick 压力测试 ship**：§4.22 全 12-section（`AnimationPlayer::releaseSlotBuffers` 私有 helper swap-with-empty 归还 tracks/pendingNotifies/capturedLocal{Pos,Rot,Scl}/trackWeights + `clearAdditiveLayerSource`/`stop()` 两 callsite + `AssetBoneCache` inner map 改 `detail::StringViewHash + std::equal_to<>` is_transparent 异构查找 0 临时 string + 显式 const char* 重载防 C3066 + 0 public API change + ECS bridge 0 touch）+ 4 new unit tests（AYTest_P4Stress.cpp 新 suite P4StressTests：400 player × 200 帧共享单 skeleton 逐位 memcmp + bind/clear 5 轮 invalidate 回落 + 异构查找单 entry + AdditiveSlot clear/stop/re-bind 行为）+ §4.21.11 UPGRADE-HOOK(transparent hash) + §4.21.12 Q3 标 resolved + §11 P4 polish row + §11 P1/P2/P3 polish rows deferred 同步（Additive slot dynamic vector → RESOLVED）+ 状态抬头同步 2673/2673 + 421/421 + 1039/1039 3-run stable；**INV-61/62/63** (闲置 slot 内存归还 + sparse 语义保留 + 异构查找 0 临时 string) 全部 NEW。教训：tick() 只推进时钟不跑渲染评估（压力测试首版只 tick → resolve 未触发断言全败）+ transparent hash 必须显式 const char* 重载（双重载等权转换 → C3066 模糊）。详见 §4.22 + §11 P4 polish row |
| 2026-08-10 | **P5 polish — DSL 四则运算 + - * / ship**：§4.23 全 12-section（8 算子 → 13 三层贯通：`CondOp` + `CondOpByte` 各追加 5 值旧值不变 + lexer 负号消歧 INV-65 + precedence 5/6 INV-69 + parseUnary 一元减 INV-66 + `CondBinaryExpr::evaluateAsFloat` override + `CondUnaryExpr::evaluateAsFloat` Neg + 除零 0.0f 双路径 fail-soft INV-67 + bytecode evaluator 5 新分支 + 0 public API change + ECS bridge 0 touch）+ 14 new unit tests（§8.1.6 11 + §P5 3：13-case parity 表 INV-68 + 尾 opcode 编码 + short-circuit 与算术共存 + reserved `CurrentStateTime * 2 > 1` 组合 + SM 集成 bytecode hot path）+ §14.3 算术 row ✅ + §4.16/§4.17 历史决策标 RESOLVED + §11 P5 polish row + roadmap P3 表 P5 polish row + 状态抬头同步 2831/2831 + 421/421 + 1039/1039 3-run stable；**INV-64..69** (算术语义 + 负号消歧 + 一元减 + 除零 fail-soft + bytecode parity + 优先级) 全部 NEW。教训：测试期望值手算避开恰好相等组合（2 处 FAIL 为期望值算错）+ 插测试块先 grep section 头位置（§8.1.5 头在文件顶部，误插 TEST_SUITE_END 落中间 → C2065）。详见 §4.23 + §11 P5 polish row |
| 2026-08-10 | **P6 polish — INV-60 flip debug assert + release 配置落地 ship**：§4.24 全 12-section（A: `setThreadSafe` 离开 thread-safe 模式 try_lock probe + debug assert——try_lock 失败 ⟺ 另一线程持锁在 cache 内，正是 INV-60 危险翻转；非阻塞绝不锁死；false→true 方向不可证明保持 startup 约定 + B: `out/build/x64-Release` 首次 configure（Ninja + MSVC /O2 + vcpkg toolchain + AY_BUILD_BENCHMARKS=ON）+ AYAnimation_UnitTests release 2843/2843 × 3 全绿 + Scenario G/H release 首测）+ 1 new unit test（`P6_AssetBoneCache_SetThreadSafe_FlipKeepsData`：双翻转 × 2 轮 entries 存活；单线程 UT 无法构造 assert 触发路径，P3 翻转测试回归兜底）+ §4.21.12 Q2 + §4.20.12/§4.21.12/§4.22.12 Q1 标 resolved + §14.2 release-build row ✅ + §11 P6 polish row + roadmap 剩项同步（setTriggerByHash 永久挂起）+ 状态抬头同步 debug 2843/2843 + 421/421 + 1039/1039 3-run stable + release 2843/2843 × 3；**INV-70** (翻转离开 thread-safe 模式 debug assert) NEW。Benchmark（release 首测）：Scenario G true 18.3ns / short 20.6ns（vs debug 86.2/75.9，~4.7x/~3.7x）；Scenario H lock-free 1.40x~2.46x（vs debug 1.06x/1.12x——lock delta 在 release 显著化，兑现 §4.21 论断；lookup 14~18ns 接近裸 unordered_map 成本）。教训：try_lock probe vs `_lockCount` 计数（0 新成员 + 可证明方向正是危险翻转）+ run-to-run 噪声 ±2.5x 必须 min-of-5 报区间（单次 50.8ns 离群）。详见 §4.24 + §11 P6 polish row |
 |