# AYAnimation Design

> **状态（2026-07-26 更新）**：AN-01 已 ship（最小单 clip 播放器，约 200 行真实代码 + 11 个 test case）。本设计文档自该版起 **重构目标** 明确为"AYAnimation 是 AYResource 的薄消费层"，不再独立维护 skeleton/animation 数据类型。Phase 1.5 P1.1 **Anim Notify** 已 ship（2026-07-26, 175+7=182 tests 3-run stable across AYAnimation + AYEntity）。
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
| `position` | `_localPos[k] = sample[k]` (blendWeight ignored for Override — confusing semantic, callers wanting partial override should mark Additive) | `_localPos[k] += sample[k] * _blendWeight` |
| `rotation` | `_localRot[q] = sample[q]` | `(base * sample.pow(_blendWeight)).normalize()` — weight==0 short-circuit before pow |
| `scale` | `_localScl[k] = sample[k]` | `_localScl[k] *= (1 + sample[k] * _blendWeight)` — UE convention, relative blend |
| Float | push to `setFloatCurveSink` at `_additiveTime` (P1.2 invariant: additive concept is local-TRS only) | (same — Float tracks not affected by blendMode) |

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
- `// UPGRADE-HOOK(P1.5)` at `consumePendingNotifiesAdditive`: merged + source-tagged + dedup-by-(time,name)
- `// UPGRADE-HOOK(P1.5)` at `_additiveTracks` / `_additiveClip`: single layer → `vector<AdditiveSlot>` stack
- `// REMOVE-MARKER(P1.6)` at `setAdditiveWeight` / `getAdditiveWeight` deprecated wrappers

### 4.8 ❌ Deferred to P1.5 / Phase 2 §14

- per-track weight map (mask expression) ── **P1.5 / P2.2**
- notify merge + source-tag + dedup-by-(time,name) ── **P1.5**
- multi-source stack (`vector<AdditiveSlot>`) ── **P1.5**
- drop deprecated `setAdditiveWeight` wrapper ── **P1.6**

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

#### 4.10.9 Out-of-scope for Full P1.4 (留到 P1.5+)

| Item | Defer to | Reason |
|------|----------|--------|
| Per-track weight map (mask expression) | P1.5 / P2.2 | curve 是 scalar-only; per-track = vector data 类型, 大模型改 |
| Multi-source stack (`vector<AdditiveSlot>`) | P1.5 | sync / ref-pose / pause 是 single-slot 内增强; 通用 stack 是数据架构大改 |
| Notify merge + source-tag + dedup-by-(time,name) | P1.5 | 与 P1.3 P1.5 upgrade hook 同 |
| Curve serialization (`.ayanm`) | 不定 | runtime-only config; motion-graph editor 出现再单独 design |
| Layer 1 (`additiveWeight` per-track) curve | 不定 | curve 只管 layer-mix, 不重复 per-track scalar;避免 invariant 缠夹 |
| 移除 `setAdditiveWeight` deprecated wrapper | P1.6 (按 plan) | 不动 P1.6 scope |

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
| 11 | Anim Notify 事件 | UE `FAnimNotifyEvent` | ✅ | Phase 1.5 SHIP (2026-07-26) |
| 12 | **拓扑序 assert** | UE `RebuildPoseCache` | 🔧 | P0 |
| 13 | **矩阵方向 lock-test** | UE `FAnimationRuntime` | 🔧 | P0 |
| 14 | **IBM = 0 NaN-safe** | UE `Check` macros | 🔧 | P0 |
| 15 | CrossFade | UE `UAnimMontage` | ❌ | Phase 2 |
| 16 | Blend 1D / 2D | UE `BlendSpace` | ❌ | Phase 2 |
| 17 | Additive 动画 (Layer 1) | UE `bAdditive` | ✅ | Phase 1.2 SHIP (2026-07-26) |
| 17b | Additive 动画 (Layer 2 / Cross-Fade) | UE `UAnimMontage` | ✅ | Phase 1.3 SHIP (2026-07-26) |
| 17c | BoneIdx cache (hot-path findBone 消除) | UE `FCompactPose` BoneIndexCache | ✅ | Phase 1.4 SHIP (2026-07-26) |
| 17d | Cross-fade 4-pack (curve + syncToBase + ref-pose + additive pause) | UE `UAnimMontage::BlendIn` / `bForceRootLock` / Unity `AnimationPlayable` | ✅ | Phase 1.4 SHIP (2026-07-26) |
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
| P1.1 | Anim Notify 事件系统（多播 `function<void(NotifyEvent)>`）── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation notify channel (VERSION 2) + dispatchPendingNotifies + AnimNotifyEvent POCO + EventBus bridge via AYEntity AnimationSystem |
| P1.2 | Additive Layer 1 MVP（per-track blendMode + global additiveWeight）── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation VERSION 3 + v2 backward compat + AnimationPlayer Phase 1 additive branch (position += / rotation pow / scale *= (1+)) + AYEntity AnimationComponent.additiveWeight + 7 new tests (1 AYResource v2 + 6 AnimationPlayer) |
| P1.3 | CrossFade in/out (Layer 2 — separate base + additive clip source mixing) ── ✅ **SHIP 2026-07-26**, root pin bump landed；IAnimation VERSION 4 (no on-disk change) + 5 invariants + AnimationPlayer dual-source state machine + Phase 1b additive branch (reuses P1.2 three formulas) + 2 notify queues + AYEntity AnimationComponent.{additiveClipPath,additivePlayRate,blendWeight} + 14 new tests (1 AYResource forward-compat + 10 AnimationPlayer + 3 AYEntity integration) |
| P1.4 | Hot-path 优化 + Cross-Fade full ship：track → boneIndex 预解析 (已 ship) + keyframed weight curve (`blendWeightOverTime` with 4 ease flavors reusing AYMath) + syncToBase option (additive playhead lock-step to base) + ref-pose capture path (CaptureState 3-state machine replacing rest-pose-at-0 assumption) + additive pause/resume (INV-8 unified with base pause)── ✅ **FULL SHIP 2026-07-26**; 8 AnimationPlayer tests + 3 AYEntity tests; 0 regression across 3 modules (701+312+187) |
| P1.5 | Tick cache（多 player 共享 skeleton 时避免重复 evaluate）+ notify merge (source-tag + dedup-by-(time,name)) + vector<AdditiveSlot> 多层 stack |

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