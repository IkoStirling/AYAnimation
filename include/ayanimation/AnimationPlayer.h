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
//   REMOVE-MARKER(P1.6): deprecated setAdditiveWeight wrapper drop
//
//   See design.md §4.7 + §4.10 for full contract and resolved items.

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

// P1.5 — source tag attached to every AnimNotifyRecord in the merged
// queue. Lets AYEntity's AnimationSystem distinguish base markers
// (AnimNotifyEvent with kTypeId 0x000A'0001 — same as Phase 1.5) from
// per-slot additive markers so subscribers can route them differently
// (e.g. UI sfx vs. gameplay logic).
//
// Numeric encoding chosen so the Base tag is 0 (default-constructed
// record remains Base — backward-compat with all P1.3/P1.4 callers
// that never read sourceTag). Additive_N = 1..8 maps to slot index 0..7.
//
// Forward-declared ahead of AnimNotifyRecord so the record struct can
// reference the enum type by value (P1.5 default-constructs sourceTag
// to Base — backward-compat default).
enum class AnimNotifySourceTag : ayt::math::UInt8 {
    Base        = 0,
    Additive_0  = 1,
    Additive_1  = 2,
    Additive_2  = 3,
    Additive_3  = 4,
    Additive_4  = 5,
    Additive_5  = 6,
    Additive_6  = 7,
    Additive_7  = 8,
};

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

// P1.5 — AnimNotifySourceTag declared above (before AnimNotifyRecord)
// so the record can default-init its `sourceTag` field at compile time.
// Tag values are stable; do not reorder.

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

    // Merged notify queue (P1.5 NEW). Replaces the dual
    // consumePendingNotifies() + consumePendingNotifiesAdditive() pattern
    // with a single sorted-by-time vector carrying source tags. Dedup
    // rule: a (time, name) collision between base and slot K keeps the
    // BASE record and drops the slot K record (base wins — gameplay
    // subscribers want base reasoning, additive is enrichment).
    //
    // The old consumePendingNotifiesAdditive() is kept as a
    // slot[0]-only wrapper for backward compat with P1.3/P1.4 hosts;
    // it is DEPRECATE-P1.5 and may be removed in P1.6 alongside the
    // deprecated `setAdditiveWeight` wrapper.
    const std::vector<AnimNotifyRecord>& consumePendingNotifiesMerged();
    size_t getPendingNotifyCountMerged() const { return _pendingNotifiesMerged.size(); }

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
        // P1.5 — DEPRECATE-P1.5 wrapper. Returns slot[0]'s queue size.
        // The canonical path is getPendingNotifyCountMerged() which
        // counts records across every slot.
        const AdditiveSlot* s = getSlot(0);
        return (s != nullptr) ? s->pendingNotifies.size() : 0u;
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
    // P1.5: still kept for backward compat (consumePendingNotifiesAdditive
    // uses it indirectly via the slot[0] path); the per-slot version is
    // dispatchSlotNotifies(slotRef, ...).
    void dispatchAdditiveNotifies(float prev, float next, bool wrapped);

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

    // Per-slot P1.4 helpers.
    float sampleLayerBlendCurve(const AdditiveSlot& slot) const;
    void  captureRefPoseFromSlot(AdditiveSlot& slot);
    void  applyCapturedRefPoseFromSlot(AdditiveSlot& slot);

    // Per-slot dispatch (P1.5). Mirrors dispatchAdditiveNotifies but
    // operates on a slot reference.
    void dispatchSlotNotifies(AdditiveSlot& slot, float prev, float next, bool wrapped);

    // Merged notify rebuild + consume.
    void rebuildMergedNotifies();
    // consumePendingNotifies() is declared in the public API above; the
    // implementation in the .cpp swaps the thread-local return slot and
    // is the single canonical consume path (consumePendingNotifiesAdditive
    // and consumePendingNotifiesMerged layer on top of it).

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
};

} // namespace ayt::anim