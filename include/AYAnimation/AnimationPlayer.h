#pragma once
// AYAnimation/AnimationPlayer.h — P0 (2026-07-26) rewrite + P1.5 AnimNotify extension (2026-07-26).
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
// Additive Layer 1 (Phase 1.2 — P1.2 MVP, deprecated P1.6):
//   setBlendWeight(w) below controls the global additive blend intensity for
//   any AnimTrack flagged AnimBlendMode::Additive in the bound clip.
//   Per-track Override behavior is byte-identical to pre-P1.2. Per-track
//   Additive behavior applies the sampled TRS value AS A DELTA on top of
//   the bone's base local TRS, weighted by _blendWeight (saturated to
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
//   UPGRADE-HOOK(P1.4 → resolved): discrete _blendWeight setter →
//     keyframed FloatCurve sampler via blendWeightOverTime(from, to, dur, easing).
//   UPGRADE-HOOK(P1.4 → resolved): independent axis → syncToBase option,
//     tick() branches (additivePaused / syncToBase / default-independent).
//   UPGRADE-HOOK(P1.4 → resolved): ref-pose capture from current base pose
//     via setAdditiveRefPoseCapture(true) (CaptureState 3-state machine).
//   UPGRADE-HOOK(P1.4 → resolved): additive layer obeys pause() —
//     INV-8 enforced in tick(); separate setAdditivePaused for additive-only.
//   UPGRADE-HOOK(P1.5): dual notify queue → merged + source-tagged
//   UPGRADE-HOOK(P1.5): single additive layer → vector<AdditiveSlot> stack
//   UPGRADE-HOOK(P1.5): uniform _blendWeight → per-track weights (mask)
//
//   See design.md §4.7 + §4.10 + §4.11 for full contract and resolved items.
//   P1.6 cleanup (2026-07-27): REMOVE-MARKER for setAdditiveWeight wrapper
//   has been resolved — the inline-forward setAdditiveWeight/getAdditiveWeight
//   wrappers were deleted; callers (AYAnimationSystem + tests) now use the
//   canonical setBlendWeight/getBlendWeight. Same for the P1.3 dual-queue
//   consumePendingNotifiesAdditive/getPendingNotifyCountAdditive — replaced
//   by the P1.5 merged consumePendingNotifiesMerged/getPendingNotifyCountMerged.

#include <AYMath/MathTypes.h>
#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/assetsDefs/ISkeleton.h>

#include <AYAnimation/AnimNotifyEvent.h>   // P1.5 — AnimNotifySourceTag definition
#include <AYResource/assetsDefs/ISkeletonMask.h>   // P3.x刀1 — formal interface in AYResource

#include <cstdint>
#include <functional>
#include <memory>
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

// ---------------------------------------------------------------------------
// P1.4 cross-fade — runtime-only config (NOT serialized into .ayanm).
//
// All three types below live on AnimationPlayer and configure runtime
// cross-fade behavior. They are NOT in IAnimation (no on-disk change —
// P1.3 v4 stays as-is) and NOT persisted across serialization. Their
// half-life is a single playback session: the host wires them via
// setAdditiveSyncToBase / setAdditiveRefPoseCapture /
// blendWeightOverTime / setAdditivePaused and reads them back via the
// is*() getters.
// ---------------------------------------------------------------------------

// P1.4 — easing function selector for blendWeightOverTime. Five entries
// cover the common Unreal / Unity / Godot cross-fade shapes; advanced
// hosts can extend by wrapping the player or shipping their own easing
// table (out of scope here — see deferred list at the end of design.md
// §4.10).
enum class BlendEasing : ayt::math::UInt8 {
    Linear     = 0,   // default — manual lerp(from, to, t)
    EaseIn     = 1,   // aymath::easeIn(t, 2)    — slow start
    EaseOut    = 2,   // aymath::easeOut(t, 2)   — slow end
    EaseInOut  = 3,   // aymath::easeInOut(t, 2) — S-curve
    Smoothstep = 4,   // aymath::smoothstep(t)   — Hermite 3t² − 2t³
};

// P1.4 — keyframed weight driver for cross-fade. curve.active = true
// makes sampleBlendCurve() return lerp(from, to, easing(t)) where
// t ∈ [0, 1] over the duration window; on the first frame past the
// end the curve auto-disarms (active → false) and Phase 1b reverts to
// the static _blendWeight value. duration == 0 with active == false
// (set by cancelBlendCurve) is the explicit "use static weight" path.
struct BlendCurve {
    float        from        = 0.0f;   // start weight (saturated to [0,1] on input)
    float        to          = 1.0f;   // end   weight (saturated to [0,1] on input)
    float        duration    = 0.0f;   // seconds; must be > 0 for the curve to run
    BlendEasing  easing      = BlendEasing::Linear;
    float        startTime   = 0.0f;   // evaluate-clock anchor = _time at the
                                       // moment blendWeightOverTime() was called
                                       // (or setTime t if the host seeks over the
                                       // active range)
    bool         active      = false;  // false → sampleBlendCurve() returns
                                       // `from` (static per-slot weight;
                                       // setBlendWeight mirrors onto slot[0])
};

// P1.4 — 3-state machine for the ref-pose capture path. Mirrors the
// kBoneUnresolved sentinel pattern: explicit "I don't know yet" state
// (Fresh), "good" state (Valid), and "stale since skeleton changed"
// state (Stale). INV-7 in evaluate() reads _captureState at the top
// of Phase 0 to decide whether to (re)capture or apply-captured.
enum class CaptureState : ayt::math::UInt8 {
    Fresh = 0,   // not yet captured against the current skeleton
    Valid = 1,   // captured; _capturedLocal* is in sync with last Phase 1a write
    Stale = 2,   // skeleton has been swapped since last capture; needs re-fill
};

// ---------------------------------------------------------------------------
// P1.5 — Multi-source stack (vector<AdditiveSlot> + Notify Merge)
//
// One AdditiveSlot bundles EVERY per-slot state that P1.3 + P1.4 spread
// across 8 single-slot fields (_additiveClip / _additiveTime / ... / _curve).
// A vector of slots means N additive layers can stack simultaneously (UE
// `LayerObjects[]`, Unity `AnimationLayerMixerPlayable.SetLayerCount`).
//
// Hard cap of 8 mirrors UE's MaxLayerCount = 8; 8 × 100-bone × 40-byte
// captured buffers = 32 KB worst case — small enough to live in the player.
// ---------------------------------------------------------------------------

// P1.5 — hard cap on simultaneous additive slots. UE / Unity production
// convention. 8 covers upper-body + lower-body + hit-react + weapon +
// knockback + breathing + breathing-variation + emergency-flash without
// requiring dynamic allocation growth. Bumped later only if a real
// host needs more.
constexpr uint32_t kMaxAdditiveSlots = 8;

// P1.5 — AnimNotifySourceTag is defined in AYAnimation/AnimNotifyEvent.h (the
// header-light bus-facing POD type). This file includes that header
// so the AnimNotifyRecord struct below can use the enum. The enum
// values (Base=0, Additive_0..7=1..8) are duplicated in source — the
// canonical declaration is AYAnimation/AnimNotifyEvent.h; we re-use the type by
// reference, no duplicate definition.

// P1.5 — Anim Notify record, hoisted from AnimationPlayer class scope
// so AdditiveSlot (declared above) can hold a std::vector of them.
// One record per AnimNotifyMarker crossed by a single tick() call;
// emitted via the dual sink + queue model (see class header for full
// description).
//
// P1.5 added `sourceTag` (default Base). P1.3 + P1.4 callers that never
// read it keep working — the merged queue still produces the same
// chronological order, just with an extra byte per record.
//
// The `name` pointer points into the IAnimation asset (lifetime =
// ResourceManager cache); valid only for the duration of the consume
// callback. Do NOT retain past the call.
struct AnimNotifyRecord {
    const char*           name      = nullptr;
    float                 time      = 0.0f;
    float                 payload   = 0.0f;
    AnimNotifySourceTag   sourceTag = AnimNotifySourceTag::Base;

    // P3.x刀 N+1.C NEW — Per-state AnimNotify routing. The state
    // name active when this notify fired. Empty string when the
    // player is not driven by a state machine (legacy / direct
    // clip playback). Default-empty keeps P1.3/P1.4/P1.5 records
    // source-compatible (subscriber code that does not know about
    // the new field continues to work — it just never sees a
    // non-empty fromStateName).
    std::string           fromStateName;
};

// P1.5 — per-layer state container. Default-constructed represents
// "slot is empty / not bound" (clip == nullptr; evaluate() skips it).
// Once a host binds a clip via setAdditiveLayerSource(slotId, src),
// the slot grows into a full state machine identical to the P1.3
// single-slot design, with P1.4 cross-fade flags per-slot.
struct AdditiveSlot {
    // Source ── mirror P1.3 single-slot fields.
    const ayt::resource::IAnimation* clip              = nullptr;
    float                            time              = 0.0f;
    float                            playRate          = 1.0f;
    bool                             loop              = true;
    float                            prevTickTime      = 0.0f;
    std::vector<TrackSlice>          tracks;
    std::vector<AnimNotifyRecord>    pendingNotifies;

    // P1.4 fields ── each slot owns its own flag set, independent of
    // every other slot. A host can enable syncToBase on slot 0 while
    // leaving slot 1's time axis independent (INV-10, structural).
    bool          syncToBase       = false;
    bool          refPoseCapture   = false;
    bool          paused           = false;
    BlendCurve    curve;
    CaptureState  captureState     = CaptureState::Fresh;
    std::vector<float> capturedLocalPos;   // n*3, sized by setSkeleton
    std::vector<float> capturedLocalRot;   // n*4
    std::vector<float> capturedLocalScl;   // n*3

    // P1.5 — per-track mask expression (opt-in). Empty = uniform 1.0f
    // (P1.3 behavior); non-empty = per-track scalar multiplier keyed
    // by track index within this slot's `tracks` array. Lets a host
    // suppress individual bones (e.g. only apply the additive rotation
    // to spine + chest, not the lower body).
    std::vector<float> trackWeights;
};

// P4-2 (2026-08-11) — solver selection for IK chains. TwoBone is the
// P4-1 analytic two-segment solve (default — back-compat, P1-P12 tests
// pass unchanged); FABRIK / CCD are the P4-2 iterative multi-joint
// solvers, whose bone path is AUTO-DERIVED along the parent chain from
// rootBone to tipBone (midBone is ignored for them, design §4.26.3).
enum class IKSolverType : std::uint8_t {
    TwoBone = 0,   // P4-1 analytic solve, exactly 2 segments
    FABRIK  = 1,   // P4-2 forward-and-backward reaching (iterative)
    CCD     = 2,   // P4-2 cyclic coordinate descent (iterative)
};

// P4-1 (2026-08-10) — Two-Bone IK chain spec (host-authored). The three
// bone names are resolved against the bound skeleton at bind time —
// EAGERLY, via AssetBoneCache, mirroring resolveSkeletonMask (design
// §4.25.4). setSkeleton() re-resolves (INV-71): chains whose names hit
// the new skeleton revive, chains that miss are disabled in place
// (resolved indices -1) and skipped by evaluate() — never a crash.
// P4-2: `type` / `iterations` appended at the END so every existing
// aggregate-initialization site stays source-compatible.
struct IKChainSpec {
    std::string               rootBone;    // chain root name (hip / shoulder)
    std::string               midBone;     // mid bone name (knee / elbow); IGNORED by FABRIK/CCD
    std::string               tipBone;     // tip name (foot / hand)
    ayt::math::FVector3       targetWorld; // world-space goal for the tip
    float                     weight = 1.0f; // [0,1]; 0 = chain off (zero-cost skip, INV-72)
    IKSolverType              type = IKSolverType::TwoBone; // P4-2: solver selection
    uint32_t                  iterations = 0;               // P4-2: 0 = solver default (FABRIK 4 / CCD 10)
};

// Hard cap on simultaneous IK chains. Mirrors kMaxAdditiveSlots = 8;
// 2 arms + 2 legs + 2 head/aux covers production use without growth.
constexpr uint32_t kMaxIKChains = 8;

// P1.5 — AnimNotifySourceTag declared above (before AnimNotifyRecord)
// so the record can default-init its `sourceTag` field at compile time.
// Tag values are stable; do not reorder.

class AnimationPlayer;

// Out-of-line delete so ~AnimationPlayer is only emitted from AYAnimation.cpp
// (see AnimationPlayer::create). Prevents MSVC LNK2005 when many TUs
// include SkeletonComponent's unique_ptr<AnimationPlayer>.
struct AnimationPlayerDeleter {
    void operator()(AnimationPlayer* p) const noexcept;
};

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
    //
    // P1.5 hoisted to namespace scope above (struct AnimNotifyRecord).
    // Use `ayt::anim::AnimNotifyRecord` or the class-qualified form
    // `AnimationPlayer::AnimNotifyRecord` interchangeably.
    using AnimNotifyRecord = ::ayt::anim::AnimNotifyRecord;

    AnimationPlayer() = default;
    ~AnimationPlayer() = default;

    AnimationPlayer(const AnimationPlayer&) = delete;
    AnimationPlayer& operator=(const AnimationPlayer&) = delete;
    AnimationPlayer(AnimationPlayer&&) noexcept = default;
    AnimationPlayer& operator=(AnimationPlayer&&) noexcept = default;

    // Factory + custom deleter: construction/deletion of the heap object
    // stay in AYAnimation.cpp. Other TUs only call Deleter::operator()
    // (avoids MSVC LNK2005 when default_delete instantiates ~AnimationPlayer
    // in every TU that destroys unique_ptr<AnimationPlayer>).
    static std::unique_ptr<AnimationPlayer, AnimationPlayerDeleter> create();

    // === Resource binding ===
    // P1.7 — setSkeleton now takes a shared_ptr<const ISkeleton>. The
    // player retains a copy so its lifetime is bounded by the holder
    // (SkeletonComponent in ECS, test fixture in unit tests). Calling
    // with an empty shared_ptr unbinds (same semantics as a null raw
    // pointer in P1.4/P1.5/P1.6).
    void setSkeleton(std::shared_ptr<const ayt::resource::ISkeleton> skel);
    void play(const ayt::resource::IAnimation* anim);

    void stop();
    void pause();
    void resume();

    // === Time control ===
    void  setTime(float t);
    void  setPlayRate(float r)        { _playRate = r; }
    void  setLoop(bool enabled)       { _loop = enabled; }

    // === Additive Layer 2 (Phase 1.3 — P1.3 MVP — Cross-Fade) ===
    //
    // Bind / rebind the additive layer source. Pass nullptr (or call
    // clearAdditiveSource) to turn the layer OFF. The base source bound
    // via play() is NOT touched. Independent time axis: the additive
    // source has its own _additiveTime / _additivePlayRate / _additiveLoop
    // that tick independently of the base. Per-track blendMode on the
    // additive source is honored from its own AnimBlendMode byte (P1.2).
    //
    // UPGRADE-HOOK(P1.4 → resolved): syncToBase option — bind the
    //   additive axis to _time so it stays in lock-step. Resolved
    //   in 2026-07-26 via setAdditiveSyncToBase(bool).
    // UPGRADE-HOOK(P1.4 → resolved): ref-pose capture — bind against
    //   the captured base pose rather than the skeleton's rest pose.
    //   Resolved in 2026-07-26 via setAdditiveRefPoseCapture(bool).
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
    // Uses sampleBlendCurve() so the curve path AND the static weight path
    // both flow through this single check — a curve that just finished
    // (active → false after the duration window) reverts to the static
    // weight. Hence isAdditiveLayerActive remains a stable read for hosts.
    bool isAdditiveLayerActive() const {
        // P1.5 — multi-aware. The layer is active iff slot[0] is bound
        // AND its effective weight is > 0. Higher-index slots don't
        // affect the layer-0 active flag (this is the legacy P1.3 /
        // P1.4 contract); callers wanting a true multi-slot-active
        // predicate should iterate via getAdditiveLayerCount() == 0
        // check themselves.
        const AdditiveSlot* s = getSlot(0);
        const float w = sampleBlendCurve();
        return (s != nullptr && s->clip != nullptr) && w > 0.0f;
    }

    // === P1.4 cross-fade — syncToBase (entry 6) ===
    //
    // When false (default, P1.3 behavior) the additive layer's playhead
    // advances independently of the base per `_additivePlayRate` /
    // `_additiveLoop`. When true, the additive playhead is FORCED to
    // equal `_time` after every tick — a lock-step mode that mirrors
    // UE `UAnimMontage::bForceRootLock`. The additive clip's own
    // playRate and loop are overridden by the base clip's settings
    // while this is on. The additive duration is still readable via
    // getDuration() (which now returns base) so callers can introspect
    // clip length regardless.
    //
    // INV-6 contract: post-tick, `_additiveTime == _time` if and only if
    // _syncToBase == true. Enforced inside tick().
    //
    // UPGRADE-HOOK(P1.5): once the multi-source vector<AdditiveSlot>
    // stack lands, syncToBase becomes per-slot; this single bool
    // generalises 1:1.
    void setAdditiveSyncToBase(bool enabled);

    bool isAdditiveSyncToBase() const {
        const AdditiveSlot* s = getSlot(0);
        return (s != nullptr) && s->syncToBase;
    }

    // === P1.4 cross-fade — ref-pose capture (entry 7) ===
    //
    // When false (default, P1.3 behavior), Phase 1b treats the
    // skeleton's bind-pose TRS as the additive base. This forced authors
    // to export additive clips with a "rest-pose sample at t=0" (a
    // long-standing authoring footgun).
    //
    // When true, evaluate() captures the post-Phase-1a base pose into
    // _capturedLocal* and reuses it as the additive base. Net effect:
    // additive tracks now layer their deltas on top of whatever base
    // was actually playing, so an additive clip with a non-rest delta
    // at t=0 still produces correct relative motion (the captured base
    // already encodes whatever the base pose chose to write).
    //
    // INV-7: while _refPoseCapture is true, Phase 1b reads from
    // _capturedLocal* (not the rest pose). The CaptureState 3-state
    // (Fresh / Valid / Stale) governs WHEN the (re)capture happens.
    // setSkeleton() flips to Stale so the next evaluate() recaptures.
    void setAdditiveRefPoseCapture(bool enabled);

    bool isAdditiveRefPoseCapture() const {
        const AdditiveSlot* s = getSlot(0);
        return (s != nullptr) && s->refPoseCapture;
    }

    // === P1.4 cross-fade — keyframed weight driver (entry 8) ===
    //
    // Switches the layer-mix weight from the static _blendWeight to a
    // time-driven curve. After this call, sampleBlendCurve() returns
    // lerp(from, to, easing((_time - startTime) / duration)) instead
    // of _blendWeight for as long as the curve is active. Past the end
    // of the curve window the implementation auto-disarms (active →
    // false) and the static _blendWeight takes over — a host that
    // wants a one-shot fade-out simply chooses from > to.
    //
    // from and to are saturated to [0, 1] on input (mirrors the
    // setBlendWeight semantics, defensive against caller typos).
    // duration ≤ 0 is rejected: the call becomes a no-op and the
    // static _blendWeight keeps its current value. Past-dur curve
    // anchor is latched to the current _time (so the curve ALWAYS
    // starts "now" rather than resuming from a previous window).
    //
    // UPGRADE-HOOK(P1.5): envelope-follower / impulse curve variants
    // (a curve whose start is decided dynamically based on gameplay,
    // e.g. damage impulse) — same data model, different fill path.
    void blendWeightOverTime(float from,
                            float to,
                            float duration,
                            BlendEasing easing = BlendEasing::Linear);

    // Aux D — explicitly disarm the curve without touching _blendWeight.
    // Useful for "cancel current blend in" before a new one starts.
    void cancelBlendCurve();

    bool isBlendCurveActive() const {
        const AdditiveSlot* s = getSlot(0);
        return (s != nullptr) && s->curve.active;
    }

    // === P1.4 cross-fade — additive-only pause (Aux E) ===
    //
    // Distinct from the base pause() — this stops ONLY the additive
    // axis while leaving the base ticking. Useful for hit-react that
    // should freeze without halting locomotion, or vice versa.
    // Default false so the additive axis ticks onward by default
    // (matches P1.3 behavior; the high-level pause() now ALSO pauses
    // the additive axis per INV-8, see the implementation for the
    // precedence rule).
    //
    // Resuming does NOT re-fire notifications that crossed during the
    // pause (cursor is reset to current time before the next tick).
    void  setAdditivePaused(bool paused);
    bool  isAdditivePaused() const {
        const AdditiveSlot* s = getSlot(0);
        return (s != nullptr) && s->paused;
    }

    // === P1.5 — Multi-source stack (vector<AdditiveSlot>) ===
    //
    // The 18 single-slot methods above all redirect to slot[0] — their
    // semantics are preserved bit-for-bit when callers haven't adopted
    // the per-slot API yet. Once a host wants N simultaneous additive
    // layers (UE `LayerObjects[]`-style), the 14 per-slot methods below
    // are the canonical path.
    //
    // Slot index range [0, kMaxAdditiveSlots). Bounds-checked; an
    // out-of-range slotId is silently ignored (defensive — never crash).
    //
    // Slot-count semantics: getAdditiveLayerCount() returns the number
    // of slots whose `clip != nullptr` (i.e. bound slots). Slot vector
    // itself is sparse: a host can bind slot 4 without binding 0..3
    // first; the implementation pads with empty slots as needed.

    // Source bind ── per-slot replacement for setAdditiveSource.
    // Returns true on successful bind (slotId in range AND src != null);
    // false if slotId >= kMaxAdditiveSlots (caller hit the cap).
    bool  setAdditiveLayerSource(uint32_t slotId,
                                 const ayt::resource::IAnimation* src,
                                 float playRate = 1.0f,
                                 bool  loop     = true);
    void  clearAdditiveLayerSource(uint32_t slotId);

    // Layer count = number of slots whose clip != nullptr. Bounded by
    // kMaxAdditiveSlots. 0 = "no additive layer active" (mirrors
    // P1.3 single-slot "off" state).
    int32_t getAdditiveLayerCount() const;

    // Weight + curve (P1.4 per-slot generalization).
    void    setAdditiveLayerWeight(uint32_t slotId, float w);
    float   getAdditiveLayerWeight(uint32_t slotId) const;
    void    blendLayerWeightOverTime(uint32_t slotId,
                                     float from, float to, float duration,
                                     BlendEasing easing = BlendEasing::Linear);
    void    cancelLayerBlendCurve(uint32_t slotId);
    bool    isLayerBlendCurveActive(uint32_t slotId) const;

    // Sync / pause / ref-pose (P1.4 per-slot generalization).
    void    setAdditiveLayerSyncToBase(uint32_t slotId, bool v);
    bool    isAdditiveLayerSyncToBase(uint32_t slotId) const;
    void    setAdditiveLayerPaused(uint32_t slotId, bool v);
    bool    isAdditiveLayerPaused(uint32_t slotId) const;
    void    setAdditiveLayerRefPoseCapture(uint32_t slotId, bool v);
    bool    isAdditiveLayerRefPoseCapture(uint32_t slotId) const;

    // Per-track mask expression (P1.5 NEW). Empty vector = uniform
    // 1.0f (P1.3 behavior); non-empty = per-track scalar multiplier
    // keyed by track index within the slot's `tracks` array. A track
    // with index >= trackWeights.size() reverts to uniform.
    void setAdditiveLayerTrackWeights(uint32_t slotId,
                                     const std::vector<float>& weights);
    const std::vector<float>& getAdditiveLayerTrackWeights(uint32_t slotId) const;

    // Merged notify queue (P1.5 NEW). Replaces the P1.3 dual-queue
    // consumePendingNotifies() pattern with a single sorted-by-time vector
    // carrying source tags. Dedup rule: a (time, name) collision between
    // base and slot K keeps the BASE record and drops the slot K record
    // (base wins — gameplay subscribers want base reasoning, additive is
    // enrichment).
    //
    const std::vector<AnimNotifyRecord>& consumePendingNotifiesMerged();
    size_t getPendingNotifyCountMerged() const { return _pendingNotifiesMerged.size(); }

    // === P3.x刀 N+1.C NEW — Per-state AnimNotify routing ===
    // Set the active state name (called by AYEntity StateMachineSystem
    // bridge after SM fires a transition). Recorded into every
    // AnimNotifyRecord::fromStateName (PUSH path) until the next call.
    // Default empty string = no state routing (legacy / direct clip).
    void setCurrentStateName(std::string name) { _currentStateNameForNotify = std::move(name); }
    const std::string& getCurrentStateName() const { return _currentStateNameForNotify; }

    // === P2.2 — Resource-level bone mask ===
    //
    // Bind a skeleton-bone-name → weight mask. Per-bone weight in [0, 1]
    // gates the contribution of every Phase 1a base write and every
    // Phase 1b additive write into that bone local TRS slot. Empty mask
    // (via clearSkeletonMask()) restores identity behavior. Weights are
    // read once (eagerly) and cached in _boneMaskWeights.
    //
    // Takes ownership of the mask via std::shared_ptr — passing a
    // shared_ptr<SkeletonMask> from SkeletonMask::create() is the
    // canonical authoring path. The shared_ptr guarantees lifetime
    // covers every evaluate() that reads it; a stack-local mask
    // reference would UAF on scope exit (INV-13/14 robustness).
    //
    // Orthogonal to P1.5 trackWeights (per-slot per-track). Either, both,
    // or neither can be active simultaneously. Order of apply within a
    // frame: trackWeights then boneMask so the final visible TRS =
    // trackMask × boneMask.
    //
    // Resolution rules (see AnimationPlayer.cpp §resolveSkeletonMask):
    //   named pass    — lookup each entry's boneName via AssetBoneCache
    //                   to a bone index; set _boneMaskWeights[idx] = weight
    //   wildcard pass — for every bone i that no named entry hit, set
    //                   _boneMaskWeights[i] = wildcard weight (if any)
    //   Default       — bones not touched by any entry default to 1.0f
    //                   (identity).
    //
    // setSkeleton() re-resolves the mask if one is bound (INV-13).
    // The mask's own weights are assumed pre-clamped to [0, 1] by the
    // authoring layer (INV-14); setSkeletonMask does not re-clamp.
    void setSkeletonMask(std::shared_ptr<const ayt::resource::ISkeletonMask> mask);
    void clearSkeletonMask();

    bool        hasSkeletonMask()             const { return static_cast<bool>(_skeletonMask); }
    std::size_t getSkeletonMaskBoneCount()     const { return _boneMaskWeights.size(); }
    const std::vector<float>& getResolvedBoneMaskWeights() const { return _boneMaskWeights; }
    // Bumped on every setSkeletonMask / clearSkeletonMask / setSkeleton
    // that triggers re-resolve. Tests use this to assert rebind-cache
    // stability across frames.
    std::uint32_t getSkeletonMaskGeneration()  const { return _skeletonMaskGeneration; }

    // === P4-1 — Two-Bone IK chains ===
    //
    // Chain identity = chainId index into a sparse vector (mirrors
    // AdditiveSlot slotId semantics). Out-of-range chainId is silently
    // ignored (defensive, never crash). Binding resolves the three bone
    // names via AssetBoneCache immediately (eager, like
    // resolveSkeletonMask); an unresolvable or topologically-invalid
    // chain is disabled in place (indices -1) and skipped by evaluate().
    //
    // The IK pass runs AFTER the bone-mask gate inside evaluate()
    // (Phase 2.5, between world accumulation and skin) — the mask does
    // NOT gate IK (absolute target lock, design §4.25.5).
    //
    // setSkeleton() re-resolves every bound chain (INV-71). stop() /
    // play() preserve chains (same semantics as additive layers).
    bool setIKChain(uint32_t chainId, const IKChainSpec& spec);
    void clearIKChain(uint32_t chainId);              // resets to empty spec
    void clearAllIKChains();
    // Per-frame hot update: the target moves every frame in gameplay
    // (e.g. hand follows a world point). No re-resolve, no re-alloc —
    // just a copy. Weight setter saturates to [0, 1] (INV-72).
    void setIKChainTarget(uint32_t chainId, const ayt::math::FVector3& worldPos);
    void setIKChainWeight(uint32_t chainId, float weight);

    const IKChainSpec& getIKChain(uint32_t chainId) const; // OOR → static empty spec
    size_t             getIKChainCount() const;           // bound (non-empty) chains
    bool               isIKChainActive(uint32_t chainId) const; // resolved && weight > 0
    // Bumped on every bind / clear / re-resolve. Tests + future ECS
    // bridge use it to detect rebinds.
    std::uint32_t getIKChainGeneration() const { return _ikGeneration; }

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

    // P1.6 cleanup: P1.3 dual-queue (consumePendingNotifies + consumePendingNotifiesAdditive)
    // has been removed. Hosts that need a per-frame drain of all notify markers
    // (base + every slot) should call consumePendingNotifiesMerged() above — it
    // returns one chronologically-sorted vector with sourceTag on every record.

private:
    // P1.7 — shared_ptr<const ISkeleton>. The player retains one
    // strong reference so lifetime is bounded by the source of truth
    // (SkeletonComponent in ECS, test fixture in unit tests). Lookup
    // via `->` keeps the call sites unchanged.
    std::shared_ptr<const ayt::resource::ISkeleton> _skeleton = nullptr;
    // P1.3 rename: _anim → _baseClip (internal-only, no API impact). The
    // public play(IAnimation*) API is unchanged; the field now signals
    // its role as the "base source" of a (potentially) two-source player.
    const ayt::resource::IAnimation* _baseClip = nullptr;

    // Phase 1.5 — internal scan-fire-callback implementation. Declared
    // here, defined in AnimationPlayer.cpp. Public callers use the sink
    // setter + consumePendingNotifies() instead.
    void dispatchPendingNotifies(float prev, float next, bool wrapped);

    // P1.4 — bone index cache management. See TrackSlice header for the
    // sentinel semantics (kBoneUnresolved = INT32_MIN, -1 = cached miss).
    //
    // invalidateBoneIndexCache resets every TrackSlice.boneIdx back to
    // kBoneUnresolved so the next evaluate() rebuilds the cache against
    // the new skeleton. Called from setSkeleton() once the skeleton
    // pointer (and therefore the bone name table) has changed. P1.5:
    // iterates every slot's tracks in addition to base _tracks.
    //
    // resolveBoneIdxOnce is the lazy single-slice resolver used inside
    // evaluate(). When boneIdx == kBoneUnresolved AND _skeleton != nullptr
    // it calls findBone once and writes the result back. After the first
    // frame the cache is hot and the branch falls through in O(1).
    void invalidateBoneIndexCache();
    void resolveBoneIdxOnce(TrackSlice& slice);

    // P1.5 — Multi-source stack helpers.
    //
    // ensureSlot(slotId) returns a reference to slot[slotId], resizing
    // the vector with empty slots if necessary. Bounds-checked; out-of-
    // range slotId triggers an assert in debug and a no-op in release.
    AdditiveSlot& ensureSlot(uint32_t slotId);
    AdditiveSlot* getSlot(uint32_t slotId);       // nullptr if OOR or vector too small
    const AdditiveSlot* getSlot(uint32_t slotId) const;

    // Rebuild TrackSlice for a slot (mirror of setAdditiveSource body
    // used by setAdditiveLayerSource). Hoisted so the single-slot and
    // multi-slot paths share the same TrackSlice-rebuild code.
    void rebuildSlotTracks(AdditiveSlot& slot, const ayt::resource::IAnimation* src);

    // P4 polish (2026-08-10) — release a slot's heavy buffers back to
    // the allocator (INV-61). Called from clearAdditiveLayerSource and
    // stop(). swap-with-empty frees capacity; plain clear() would keep
    // the allocation, and the per-slot buffers (tracks + capturedLocal*
    // + trackWeights) are the multi-KB members of AdditiveSlot. Small
    // scalar fields are untouched so a re-bind starts in fresh state.
    void releaseSlotBuffers(AdditiveSlot& slot);

    // Per-slot P1.4 helpers.
    float sampleLayerBlendCurve(const AdditiveSlot& slot) const;
    void  captureRefPoseFromSlot(AdditiveSlot& slot);
    void  applyCapturedRefPoseFromSlot(AdditiveSlot& slot);

    // Per-slot dispatch (P1.5). Called from the Phase 1b per-slot loop
    // inside tick() / evaluate().
    void dispatchSlotNotifies(AdditiveSlot& slot, float prev, float next, bool wrapped);

    // Merged notify rebuild + consume.
    void rebuildMergedNotifies();

    // P2.2 — resolve every entry of _skeletonMask into _boneMaskWeights.
    // Idempotent. Called from setSkeletonMask() AND from setSkeleton() if
    // a mask is still bound (skeleton swap invalidates resolved indices
    // — new ISkeleton pointer means old AssetBoneCache entries stale).
    // Eager (Q2 = X): no per-frame lazy fallback.
    void resolveSkeletonMask();
    // consumePendingNotifies() is declared in the public API above; the
    // implementation in the .cpp swaps the thread-local return slot and
    // is the canonical consume path. consumePendingNotifiesMerged()
    // rebuilds + swaps _pendingNotifiesMerged.

    // P1.4 cross-fade — single-slot helpers. These three are kept as
    // thin wrappers around `_additiveSlots[0]` so old P1.4 callers (the
    // 312-test AYAnimation + 187-test AYEntity baselines) keep working
    // bit-for-bit. The per-slot generalisations live in
    // sampleLayerBlendCurve / captureRefPoseFromSlot / applyCapturedRefPoseFromSlot.
    //
    // sampleBlendCurve() returns the currently-effective layer weight
    // for slot[0]. See AdditiveSlot::curve for the math; equivalent to
    // sampleLayerBlendCurve(_additiveSlots[0]).
    float sampleBlendCurve() const;
    void  captureRefPoseFromLocal();
    void  applyCapturedRefPoseToLocal();
    static float applyEasing(float t01, BlendEasing e);

    float _time     = 0.0f;
    float _playRate = 1.0f;
    bool  _paused   = false;
    bool  _loop     = true;
    // P1.3 rename: _additiveWeight → _blendWeight. Same field semantics
    // (saturate [0,1], default 1.0f). P1.5 still uses this for slot[0]
    // (the single-slot backward-compat path); per-slot weights live in
    // AdditiveSlot::curve. Phase 1a (per-track additive within base)
    // still uses this scalar.
    float _blendWeight = 1.0f;

    // P1.5 — Multi-source stack. _additiveSlots[i] holds slot i's full
    // state machine (P1.3 single-slot fields + P1.4 per-slot flags).
    // _additiveSlots.size() == 0 means no slot bound (P1.3 "off" state).
    // After P1.5 the old _additiveClip / _additiveTime / ... fields are
    // GONE — slot[0] is the single source of truth.
    std::vector<AdditiveSlot> _additiveSlots;

    // P2.2 — Resource-level bone mask. _skeletonMask is a weak type-only
    // pointer (we keep the strong ref so the mask survives across
    // evaluate() but is dropped on clearSkeletonMask()). _boneMaskWeights
    // is the resolved per-bone float vector of length
    // _skeleton->getBoneCount() filled by resolveSkeletonMask().
    // _skeletonMaskGeneration bumps on every resolve so tests / ECS bridge
    // can detect a rebind.
    std::shared_ptr<const ayt::resource::ISkeletonMask> _skeletonMask = nullptr;
    std::vector<float>         _boneMaskWeights;
    std::uint32_t              _skeletonMaskGeneration = 0;

    // P4-1/P4-2 — IK chains (sparse vector; empty spec == unbound).
    // `spec` retains the bone NAMES so setSkeleton() can re-resolve —
    // names outlive index validity (mirror of the TrackSlice pattern).
    // P4-2: the three resolved indices became `path` (index 0 = root,
    // last = tip). TwoBone holds exactly 3 entries; FABRIK/CCD hold the
    // full root→tip bone path, auto-derived by resolveIKChains. An empty
    // path = disabled (name miss / topology invalid / over-cap).
    struct IKChain {
        IKChainSpec spec;
        std::vector<int32_t> path;
    };
    std::vector<IKChain> _ikChains;
    std::uint32_t        _ikGeneration = 0;

    // P4-1/P4-2 — eager resolver (design §4.25.4 + §4.26.4): AssetBoneCache
    // lookup for the names + chain topology validation. TwoBone: the P4-1
    // three-name walk (root ancestor of mid, mid of tip). FABRIK/CCD: a
    // single tip→root parent walk auto-derives the full path (midBone
    // ignored); walk failure / root==tip / > kMaxIKChainBones → disabled
    // (empty path). No-op when _skeleton is null. Idempotent; bumps
    // _ikGeneration.
    void resolveIKChains();

    // P4-2 — unified Phase 2.5 pass for ONE bound chain (design §4.26.4):
    // snapshot world pos/rot along `ch.path`, dispatch by spec.type,
    // write back the N-1 bone local rotations (world→local conjugate),
    // then accumulateWorldFrom(path[0]) re-runs FK over the chain subtree.
    // Returns false when the chain is inactive (weight <= 0, INV-72) or
    // degenerate (path.size() < 2) — caller skips without touching state.
    bool applyIKChain(IKChain& ch);

    // P4-1 — rebuild _world[i] = parent.world * localTRS for i in
    // [start, n). Phase 2 becomes accumulateWorldFrom(0); the IK pass
    // calls it per chain with start = chain root index. The hard
    // parentIndex < childIndex invariant makes [start, n) cover exactly
    // the chain's subtree — ancestors reuse their untouched Phase 2
    // values, no branches needed (design §4.25.5).
    void accumulateWorldFrom(size_t start);

    // P4-1 — write a quaternion into the flat _localRot buffer (n*4).
    void writeLocalRot(size_t i, const ayt::math::FQuaternion& q);

    // Per-bone local TRS working buffers (float-array form to avoid
    // per-frame allocate / align to P0 hot-path goal).
    std::vector<float> _localPos;     // n * 3
    std::vector<float> _localRot;     // n * 4
    std::vector<float> _localScl;     // n * 3
    std::vector<ayt::math::Float4x4> _world;
    std::vector<ayt::math::Float4x4> _skin;

    // Pre-normalized track data (built once in play()). The TrackSlice
    // struct itself is defined at namespace scope above the class so the
    // P1.5 _additiveSlots member can refer to it without a forward-decl
    // dependency.
    std::vector<TrackSlice> _tracks;

    FloatCurveSink _floatSink;

    // Phase 1.5 — Anim Notify (see header comment above for the dual-exit
    // sink + queue model). P1.5: _pendingNotifies still holds BASE records;
    // per-slot records live in AdditiveSlot::pendingNotifies; the merged
    // vector (rebuilt by rebuildMergedNotifies) lives in
    // _pendingNotifiesMerged and is drained by consumePendingNotifiesMerged.
    AnimNotifySink                  _animNotifySink;
    std::vector<AnimNotifyRecord>  _pendingNotifies;
    std::vector<AnimNotifyRecord>  _pendingNotifiesMerged;   // P1.5 NEW
    float                           _prevTickTime = 0.0f;

    // P3.x刀 N+1.C — Per-state AnimNotify routing. Recorded into
    // AnimNotifyRecord::fromStateName on push. Setter is called by
    // AYEntity StateMachineSystem bridge on transition. Default empty
    // = no state routing (legacy / direct clip playback — back-compat
    // sentinel for P1.3/P1.4/P1.5 records).
    std::string                     _currentStateNameForNotify;
};

} // namespace ayt::anim