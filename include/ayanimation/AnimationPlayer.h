#pragma once
// AnimationPlayer.h — P0 (2026-07-26) rewrite + P1.5 AnimNotify extension (2026-07-26).
//
// Consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly;
// no parallel skeleton/animation types in ayt::anim.
//
// Lifecycle:
//   setSkeleton(ISkeleton*)   — bind the rest pose + IBM data source
//   play(IAnimation*)         — bind the clip; pre-normalizes track times
//                                from ticks to seconds in one pass
//   tick(dt)                  — advances _time (loop wrap / end-clamp),
//                                dispatches any AnimNotify markers crossed
//                                during the tick
//   evaluate()                — runs 3 phases (see AnimationPlayer.cpp)
//   getBoneSkinMatrices()     — renderer reads this each frame
//
// Float track sink:
//   evaluate() samples every Float track at _time and pushes
//   (nodeName, property, value) to the sink. Default sink is a no-op
//   so existing callers don't need to register one.
//
// Anim Notify (Phase 1.5):
//   tick() detects when _time has crossed any AnimNotifyMarker on the
//   current clip and emits them via:
//     (a) the optional AnimNotifySink callback registered by the host, AND
//     (b) appending them to a per-frame _pendingNotifies queue that
//         AnimationSystem::onUpdate drains via consumePendingNotifies()
//         and posts into the AYEventSystem bus as AnimNotifyEvent.
//   Both paths fire for every crossed marker; callers may consume either
//   or both (the callback runs in tick() — synchronous; the queue is
//   drained after memcpy of skin matrices, on the same main-thread tick).
//
// Additive Layer 1 (Phase 1.2 — P1.2 MVP):
//   setAdditiveWeight(w) controls the global additive blend intensity for
//   any AnimTrack flagged AnimBlendMode::Additive in the bound clip.
//   Per-track Override behavior is byte-identical to pre-P1.2. Per-track
//   Additive behavior applies the sampled TRS value AS A DELTA on top of
//   the bone's base local TRS, weighted by _additiveWeight (saturated to
//   [0, 1] on write to keep quaternion math safe). Math:
//     position: _localPos[k] += sample[k] * weight
//     rotation: (base * sample.pow(weight)).normalize();  weight==0 → identity
//               early-return so the result equals base, defeating a
//               quaternion.pow(0) degenerate case in MathTypes.
//     scale:    _localScl[k] *= (1.0f + sample[k] * weight)    (UE convention)
//     Float:    additive concept is local-TRS only — Float tracks always
//               go through setFloatCurveSink unaffected.
//   See design.md §4.6 for the full Layer 1 contract, ref-pose-at-frame-0
//   assumption, and the explicit out-of-scope list (cross-fade, mask, etc.).
//
// Additive Layer 2 (Phase 1.3 — P1.3 MVP — Cross-Fade):
//   Two IAnimation sources stacked on a single player. `play(baseClip)`
//   binds the BASE source; `setAdditiveSource(src)` binds the ADDITIVE
//   LAYER. Phase 1 of evaluate() runs BASE tracks (Override or Additive
//   per the per-track AnimBlendMode byte), then Phase 1b runs ADDITIVE
//   tracks gated by INV-1 (`src != nullptr && blendWeight > 0`).
//
//   State machine (5 entry points, single owner per field):
//     setSkeleton  : resizes _local*; additive layer PRESERVED.
//     play         : rebuilds _tracks from base; additive layer PRESERVED.
//     setAdditiveSource / clearAdditiveSource : bind/unbind layer source;
//                                                base untouched.
//     setBlendWeight : saturate [0,1]; neither source touched.
//     tick         : base + additive time axes advanced INDEPENDENTLY.
//                    Each has its own loop wrap and notify dispatch.
//
//   Five invariants (asserted in evaluate() header, debug only):
//     INV-1: _additiveClip == nullptr || _blendWeight <= 0 ⇒ Phase 1b
//            loop skipped entirely. null and zero-weight are equivalent
//            "off" states.
//     INV-2: _local{Pos,Rot,Scl}.size() == n * stride maintained by
//            setSkeleton ONLY; Phase 1b writes elements 0..n-1, never
//            resizes.
//     INV-3: _blendWeight ∈ [0,1] post-every-setter (inline saturate).
//     INV-4: _baseClip == nullptr && _additiveClip != nullptr ⇒
//            evaluate early-return after rest-pose seed (degenerate
//            state during clip swap window).
//     INV-5: _additiveClip == nullptr ⇒ Phase 1a output identical to
//            P1.2 (zero-regression for the 197-test baseline).
//
//   UPGRADE-HOOK(P1.4): discrete _blendWeight setter → keyframed curve
//   UPGRADE-HOOK(P1.4): uniform _blendWeight → per-track weights (mask)
//   UPGRADE-HOOK(P1.4): independent axis → syncToBase option
//   UPGRADE-HOOK(P1.5): dual notify queue → merged + source-tagged
//   UPGRADE-HOOK(P1.5): single additive layer → vector<AdditiveSlot> stack
//   REMOVE-MARKER(P1.6): deprecated setAdditiveWeight wrapper drop
//
//   See design.md §4.7 for full contract and deferred items.

#include <aymath/MathTypes.h>
#include <assetsDefs/IAYAnimation.h>
#include <assetsDefs/IAYSkeleton.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ayt::anim
{

// Pre-normalized track data (built once in play() / setAdditiveSource()).
//
// P0 alignment workaround: ayt::resource::IAnimation::getTrackVector3Values
// returns reinterpret_cast<FVector3*> over a flat float buffer; FVector3
// contains an __m128 union member requiring 16-byte alignment that the
// underlying vector<float> storage doesn't guarantee. We therefore
// copy values into our own std::vector<FVector3> (which IS 16-byte
// aligned because the element type requires it) at play() time, and
// hand those to the sampler.
struct TrackSlice {
    std::vector<ayt::math::FVector3>    vec3Values;   // packed, aligned
    std::vector<ayt::math::FQuaternion> quatValues;   // packed, aligned
    std::vector<float>                  scalarValues; // flat
    std::string                         nodeName;
    std::string                         property;
    ayt::resource::AnimTrackType        type     = ayt::resource::AnimTrackType::Vector3;
    // Phase 1.2 — per-track blend mode. Cached at play() so evaluate()
    // never calls back into the IAnimation interface (hot-path).
    // Default Override → byte-identical to pre-P1.2 behavior.
    ayt::resource::AnimBlendMode        blendMode = ayt::resource::AnimBlendMode::Override;
    std::vector<float>                  timesSec;
    // P1.4 — bone index cache. Sentinel INT32_MIN ("unresolved"); 0+
    // ("resolved to a real bone"); -1 ("looked up, name not found in
    // skeleton — cached so we don't re-query every frame"). See
    // AnimationPlayer::resolveBoneIdxOnce / invalidateBoneIndexCache.
    int32_t                             boneIdx  = INT32_MIN;
};

// P1.4 — sentinel for the TrackSlice.boneIdx cache. Negative so it
// never collides with a valid bone index (>= 0); distinct from -1 so
// "not yet resolved" is distinguishable from "name not found".
constexpr int32_t kBoneUnresolved = INT32_MIN;

class AnimationPlayer {
public:
    // Sink signature: (boneName, property, value-at-_time).
    // Empty property means "node-only" parameter (not T/R/S bound).
    using FloatCurveSink = std::function<void(const char* nodeName,
                                              const char* property,
                                              float value)>;

    // Phase 1.5 — Anim Notify sink: (notifyName, time, payload).
    // Fired synchronously inside tick() when the playhead has crossed
    // an AnimNotifyMarker on the bound IAnimation. Multiple crossings
    // during a single tick fire in chronological order. The pointer
    // arguments point into the IAnimation asset (lifetime = ResourceManager
    // cache) and are valid only for the duration of the callback — do NOT
    // retain past the call.
    using AnimNotifySink = std::function<void(const char* notifyName,
                                              float       time,
                                              float       payload)>;

    // Phase 1.5 — public record exposed via consumePendingNotifies().
    // The AYEntity AnimationSystem drains this queue each tick and posts
    // one AnimNotifyEvent per record into the AYEventSystem bus.
    struct AnimNotifyRecord {
        const char* name    = nullptr;
        float       time    = 0.0f;
        float       payload = 0.0f;
    };

    AnimationPlayer() = default;

    // === Resource binding ===
    void setSkeleton(const ayt::resource::ISkeleton* skel);
    void play(const ayt::resource::IAnimation* anim);

    void stop();
    void pause();
    void resume();

    // === Time control ===
    void  setTime(float t);
    void  setPlayRate(float r)        { _playRate = r; }
    void  setLoop(bool enabled)       { _loop = enabled; }

    // === Additive Layer 1 (Phase 1.2 — P1.2 MVP) ===
    //
    // Global scalar blend weight applied to any AnimTrack carrying the
    // Additive blendMode flag in the bound IAnimation. Override tracks are
    // not affected. Default 1.0f (no scaling). Negative input is saturated
    // to 0; values > 1 are saturated to 1 — keeps quaternion pow safe from
    // a caller typo (e.g. -0.5 meant 0.5) without leaking NaNs into the
    // skin matrices.
    //
    // P1.3 deprecation: kept as inline-forward to setBlendWeight so the
    // 197-test P1.2 baseline (and AYAnimationSystem's existing caller at
    // AYAnimationSystem.cpp:124) keeps compiling. The canonical P1.3 API
    // is setBlendWeight / getBlendWeight below. Remove in P1.6.
    void  setAdditiveWeight(float w)   { setBlendWeight(w); }
    float getAdditiveWeight() const    { return _blendWeight; }

    // === Additive Layer 2 (Phase 1.3 — P1.3 MVP — Cross-Fade) ===
    //
    // Bind / rebind the additive layer source. Pass nullptr (or call
    // clearAdditiveSource) to turn the layer OFF. The base source bound
    // via play() is NOT touched. Independent time axis: the additive
    // source has its own _additiveTime / _additivePlayRate / _additiveLoop
    // that tick independently of the base. Per-track blendMode on the
    // additive source is honored from its own AnimBlendMode byte (P1.2).
    //
    // UPGRADE-HOOK(P1.4): syncToBase option — bind the additive axis
    //   to _time so it stays in lock-step.
    // UPGRADE-HOOK(P1.4): ref-pose capture — bind against the captured
    //   base pose rather than the skeleton's rest pose.
    // UPGRADE-HOOK(P1.5): stack — vector<AdditiveSlot> for layered mixing.
    void setAdditiveSource(const ayt::resource::IAnimation* src,
                           float playRate = 1.0f,
                           bool  loop     = true);
    // Layer-off entry point. Drains the additive notify queue and resets
    // the additive playhead + prev-tick cursor; preserves _blendWeight.
    void clearAdditiveSource();

    // Canonical P1.3 weight setter (renamed from _additiveWeight / setAdditiveWeight
    // in P1.2; the old name is preserved as an inline-forward wrapper above).
    // Saturates to [0, 1]. Phase 1b is skipped when _blendWeight == 0
    // (equivalent to layer-off per INV-1).
    //
    // UPGRADE-HOOK(P1.4): discrete setter → keyframed FloatCurve sampler
    // UPGRADE-HOOK(P1.4): uniform weight → per-track weights (mask)
    void  setBlendWeight(float w);
    float getBlendWeight() const        { return _blendWeight; }

    // INV-1 mirror: true iff layer would actually contribute to evaluate().
    bool isAdditiveLayerActive() const {
        return _additiveClip != nullptr && _blendWeight > 0.0f;
    }

    float getTime() const             { return _time; }
    float getPlayRate() const         { return _playRate; }
    float getDuration() const         { return _baseClip ? _baseClip->getDuration() : 0.0f; }
    bool  isPaused() const            { return _paused; }
    bool  isValid() const             { return _skeleton != nullptr && _baseClip != nullptr; }

    void tick(float dt);
    void evaluate();

    // === Matrix results (renderer reads) ===
    const ayt::math::Float4x4* getBoneWorldMatrices() const { return _world.data(); }
    const ayt::math::Float4x4* getBoneSkinMatrices()  const { return _skin.data();  }
    size_t                     getBoneCount()         const { return _world.size(); }

    // === Float curve sink ===
    // Replace the sink. Pass an empty std::function to disable.
    void setFloatCurveSink(FloatCurveSink sink) { _floatSink = std::move(sink); }

    // === Anim Notify sink + queue (Phase 1.5) ===
    //
    // setAnimNotifySink installs a direct host callback fired inside
    // tick() when markers are crossed. Empty std::function disables it.
    //
    // consumePendingNotifies() returns the queue of records fired by the
    // most recent tick() (and any ticks since the last consume). The
    // returned reference is cleared after the call. AYEntity's
    // AnimationSystem is the intended consumer.
    void setAnimNotifySink(AnimNotifySink sink) { _animNotifySink = std::move(sink); }
    const std::vector<AnimNotifyRecord>& consumePendingNotifies();
    // Read-only peek (does not clear). Useful for tests / diagnostics.
    size_t getPendingNotifyCount() const { return _pendingNotifies.size(); }

    // P1.3 — second notify queue. Additive source markers fired by the
    // most recent tick() (and any since the last consume). Independent
    // of the base queue. Same dual-exit / thread_local swap semantics as
    // consumePendingNotifies() so a host that drains both per frame sees
    // them in their own chronological order.
    //
    // UPGRADE-HOOK(P1.5): merged + source-tagged + dedup-by-(time,name).
    const std::vector<AnimNotifyRecord>& consumePendingNotifiesAdditive();
    size_t getPendingNotifyCountAdditive() const {
        return _additivePendingNotifies.size();
    }

private:
    const ayt::resource::ISkeleton* _skeleton = nullptr;
    // P1.3 rename: _anim → _baseClip (internal-only, no API impact). The
    // public play(IAnimation*) API is unchanged; the field now signals
    // its role as the "base source" of a (potentially) two-source player.
    const ayt::resource::IAnimation* _baseClip = nullptr;

    // Phase 1.5 — internal scan-fire-callback implementation. Declared
    // here, defined in AnimationPlayer.cpp. Public callers use the sink
    // setter + consumePendingNotifies() instead.
    void dispatchPendingNotifies(float prev, float next, bool wrapped);
    // P1.3 — mirror dispatch for the additive source's notify markers.
    void dispatchAdditiveNotifies(float prev, float next, bool wrapped);

    // P1.4 — bone index cache management. See TrackSlice header for the
    // sentinel semantics (kBoneUnresolved = INT32_MIN, -1 = cached miss).
    //
    // invalidateBoneIndexCache resets every TrackSlice.boneIdx back to
    // kBoneUnresolved so the next evaluate() rebuilds the cache against
    // the new skeleton. Called from setSkeleton() once the skeleton
    // pointer (and therefore the bone name table) has changed.
    //
    // resolveBoneIdxOnce is the lazy single-slice resolver used inside
    // evaluate(). When boneIdx == kBoneUnresolved AND _skeleton != nullptr
    // it calls findBone once and writes the result back. After the first
    // frame the cache is hot and the branch falls through in O(1).
    void invalidateBoneIndexCache();
    void resolveBoneIdxOnce(TrackSlice& slice);

    float _time     = 0.0f;
    float _playRate = 1.0f;
    bool  _paused   = false;
    bool  _loop     = true;
    // P1.3 rename: _additiveWeight → _blendWeight. Same field semantics
    // (saturate [0,1], default 1.0f) — the new name reflects its broader
    // role as the global layer-mix weight (covers both Layer 1 per-track
    // additive within a clip AND Layer 2 cross-clip blending).
    //
    // P1.3 also extends its semantics: when setAdditiveSource is bound,
    // _blendWeight gates Phase 1b (per INV-1). Phase 1a (per-track
    // additive within base) still uses the same scalar.
    float _blendWeight = 1.0f;

    // P1.3 — Additive Layer 2 state. All six fields below are owned by
    // setAdditiveSource / clearAdditiveSource (entry point 3 in the state
    // machine). INV-1: when _additiveClip == nullptr OR _blendWeight == 0,
    // Phase 1b is skipped and the layer contributes nothing to evaluate().
    const ayt::resource::IAnimation* _additiveClip       = nullptr;
    float                            _additiveTime       = 0.0f;
    float                            _additivePlayRate   = 1.0f;
    bool                             _additiveLoop       = true;
    float                            _additivePrevTickTime = 0.0f;
    std::vector<TrackSlice>          _additiveTracks;
    std::vector<AnimNotifyRecord>    _additivePendingNotifies;

    // Per-bone local TRS working buffers (float-array form to avoid
    // per-frame allocate / align to P0 hot-path goal).
    std::vector<float> _localPos;     // n * 3
    std::vector<float> _localRot;     // n * 4
    std::vector<float> _localScl;     // n * 3
    std::vector<ayt::math::Float4x4> _world;
    std::vector<ayt::math::Float4x4> _skin;

    // Pre-normalized track data (built once in play()). The TrackSlice
    // struct itself is defined at namespace scope above the class so the
    // P1.3 _additiveTracks member (declared earlier in the private
    // section) can refer to it without a forward-decl dependency.
    std::vector<TrackSlice> _tracks;

    FloatCurveSink _floatSink;

    // Phase 1.5 — Anim Notify (see header comment above for the dual-exit
    // sink + queue model).
    AnimNotifySink                  _animNotifySink;
    std::vector<AnimNotifyRecord>  _pendingNotifies;
    float                           _prevTickTime = 0.0f;
};

} // namespace ayt::anim