#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/KeySampler.h>

#include <aymath/MathTypes.h>
#include <aymath/MathUtils.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace ayt::anim
{

namespace
{

// Validate the topology invariant AnimationPlayer::evaluate Phase 2 relies on:
// parentIndex must precede the child index, recursively, so a single forward
// pass can accumulate world matrices. Loaders (FBX converter, SkeletonConverter)
// guarantee this via DFS collection order; assert in debug builds so a
// regression in either loader or a hand-rolled test Skeleton is caught loudly.
bool isTopologicallySorted(const ayt::resource::ISkeleton* skel)
{
    if (skel == nullptr) return true;
    const size_t n = skel->getBoneCount();
    const ayt::resource::Bone* bones = skel->getBones();
    for (size_t i = 0; i < n; ++i) {
        const int p = bones[i].parentIndex;
        if (p >= 0 && static_cast<size_t>(p) >= i) {
            return false;
        }
    }
    return true;
}

} // namespace

void AnimationPlayer::setSkeleton(const ayt::resource::ISkeleton* skel)
{
    _skeleton = skel;

    const size_t n = (skel != nullptr) ? skel->getBoneCount() : 0;
    _localPos.assign(n * 3, 0.0f);
    _localRot.assign(n * 4, 0.0f);
    _localScl.assign(n * 3, 1.0f);
    _world.assign(n, ayt::math::Float4x4::identity());
    _skin.assign(n,  ayt::math::Float4x4::identity());

    // Seed local TRS buffers from the skeleton's rest pose so missing
    // tracks leave a bone at bind pose.
    if (skel != nullptr && n > 0) {
        const ayt::math::FVector3*    pos = skel->getLocalPositions();
        const ayt::math::FQuaternion* rot = skel->getLocalRotations();
        const ayt::math::FVector3*    scl = skel->getLocalScales();
        if (pos != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localPos[i * 3 + 0] = pos[i].x;
                _localPos[i * 3 + 1] = pos[i].y;
                _localPos[i * 3 + 2] = pos[i].z;
            }
        }
        if (rot != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localRot[i * 4 + 0] = rot[i].x;
                _localRot[i * 4 + 1] = rot[i].y;
                _localRot[i * 4 + 2] = rot[i].z;
                _localRot[i * 4 + 3] = rot[i].w;
            }
        }
        if (scl != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localScl[i * 3 + 0] = scl[i].x;
                _localScl[i * 3 + 1] = scl[i].y;
                _localScl[i * 3 + 2] = scl[i].z;
            }
        }
    }

    // P0: assert topology. AYResource loaders guarantee parent-before-child;
    // a hand-rolled skeleton with bad parentIndex would NaN-out the world
    // matrices silently otherwise.
#ifndef NDEBUG
    assert(isTopologicallySorted(_skeleton) &&
           "AnimationPlayer: skeleton bone order must satisfy parentIndex < childIndex");
#endif

    // P1.4 — bone-index cache invalidation. The previous skeleton's
    // bone name table is no longer the right lookup target — every
    // TrackSlice.boneIdx entry must be re-resolved against the new
    // skeleton. We do NOT proactively re-resolve here because the
    // skeleton might be transiently null between calls; the first
    // evaluate() resolves each slice lazily via resolveBoneIdxOnce.
    invalidateBoneIndexCache();

    // P1.4 — ref-pose capture buffer resize + invalidation. The capture
    // path (setAdditiveRefPoseCapture(true)) snapshots the per-bone local
    // TRS after Phase 1a into _capturedLocal*. When the skeleton swaps,
    // the buffers must be re-sized and the CaptureState flipped to Stale
    // so the next evaluate() recaptures against the new bone layout.
    // Note: we preserve _refPoseCapture — turning the option off and
    // back on for a fresh skeleton should NOT lose the user's intent.
    const size_t captN = n;
    _capturedLocalPos.assign(captN * 3, 0.0f);
    _capturedLocalRot.assign(captN * 4, 0.0f);
    _capturedLocalScl.assign(captN * 3, 1.0f);
    if (_refPoseCapture) {
        _captureState = CaptureState::Stale;
    } else {
        _captureState = CaptureState::Fresh;
    }
}

// P1.4 — bone-index cache helpers. See TrackSlice header for the
// sentinel semantics (INT32_MIN == "not yet resolved"; -1 == "name
// not found in current skeleton"). After the first evaluate() the
// cache is hot and every subsequent frame reads the cached index
// directly — no more findBone() per track per frame per source.
//
// Both base (_tracks) and additive (_additiveTracks) slices share
// the same skeleton so they all need to be invalidated together.
void AnimationPlayer::invalidateBoneIndexCache()
{
    for (TrackSlice& tr : _tracks) {
        tr.boneIdx = kBoneUnresolved;
    }
    for (TrackSlice& tr : _additiveTracks) {
        tr.boneIdx = kBoneUnresolved;
    }
}

void AnimationPlayer::resolveBoneIdxOnce(TrackSlice& slice)
{
    // Sentinel means "not yet resolved" — caller (evaluate()) checks
    // before invoking us so we can assume it. If a slice has no
    // skeleton bound we leave the sentinel in place; the evaluate()
    // gate `if (boneIdx < 0)` handles the three cases:
    //   -1    = name not found in skeleton (cached negative)
    //   other = negative sentinel / null skeleton (skip the track)
    //   >=0   = valid bone index
    if (slice.boneIdx != kBoneUnresolved) return;
    if (_skeleton == nullptr) {
        // No skeleton to query against — leave sentinel so a later
        // setSkeleton() + evaluate() can still resolve.
        return;
    }
    const int found = _skeleton->findBone(slice.nodeName.c_str());
    // Cache BOTH outcomes:
    //   found >= 0  → bone index (hit)
    //   found <  0  → -1 (miss; we still don't want to re-query next frame)
    // We use -1 (not kBoneUnresolved) for the "looked up, not found"
    // result so evaluate() can short-circuit without re-entering here.
    slice.boneIdx = (found >= 0) ? static_cast<int32_t>(found) : -1;
}

void AnimationPlayer::play(const ayt::resource::IAnimation* anim)
{
    _baseClip = anim;
    _time = 0.0f;
    _paused = false;
    // Phase 1.5: starting a new clip clears the per-frame notify queue
    // and resets the prev-tick cursor so the first tick fires anything
    // in [0, tick_dt).
    _pendingNotifies.clear();
    _prevTickTime = 0.0f;

    _tracks.clear();
    if (anim == nullptr) return;

    // Pre-normalize track times ticks → seconds once. Per-frame sampling
    // then works in seconds without per-key division.
    const float tps = anim->getTicksPerSecond();
    const float invTps = (tps > 0.0f) ? (1.0f / tps) : 1.0f;

    const uint32_t trackCount = anim->getTrackCount();
    _tracks.reserve(trackCount);
    for (uint32_t ti = 0; ti < trackCount; ++ti) {
        TrackSlice slice;
        slice.nodeName = (anim->getTrackNodeName(ti) != nullptr)
                             ? std::string(anim->getTrackNodeName(ti))
                             : std::string();
        slice.property = (anim->getTrackProperty(ti) != nullptr)
                             ? std::string(anim->getTrackProperty(ti))
                             : std::string();
        slice.type     = anim->getTrackType(ti);
        // Phase 1.2 — cache the per-track blend mode at play() time so the
        // hot path (evaluate Phase 1) reads from TrackSlice and never calls
        // back into the IAnimation interface.
        slice.blendMode = anim->getTrackBlendMode(ti);

        const uint32_t keyCount = anim->getTrackKeyframeCount(ti);
        const float* rawTimes = anim->getTrackTimes(ti);
        slice.timesSec.assign(keyCount, 0.0f);
        for (uint32_t k = 0; k < keyCount; ++k) {
            slice.timesSec[k] = rawTimes[k] * invTps;
        }

        // P0 alignment workaround: copy into our own aligned containers
        // rather than rely on the AYResource reinterpret_cast path
        // (which produces unaligned FVector3 / FQuaternion pointers that
        // alias into a std::vector<float>'s internal storage — UB on
        // MSVC because both types contain an __m128 union member that
        // requires 16-byte alignment). We go through the flat float
        // buffer to avoid the reinterpret-cast entirely.
        const float* flat = anim->getTrackFloatValues(ti);
        switch (slice.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                if (flat != nullptr && keyCount > 0) {
                    slice.vec3Values.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.vec3Values[k] = ayt::math::FVector3(
                            flat[k * 3 + 0],
                            flat[k * 3 + 1],
                            flat[k * 3 + 2]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                if (flat != nullptr && keyCount > 0) {
                    slice.quatValues.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.quatValues[k] = ayt::math::FQuaternion(
                            flat[k * 4 + 0],
                            flat[k * 4 + 1],
                            flat[k * 4 + 2],
                            flat[k * 4 + 3]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                if (flat != nullptr && keyCount > 0) {
                    slice.scalarValues.assign(flat, flat + keyCount);
                }
                break;
            }
        }
        _tracks.push_back(std::move(slice));
    }

    // P1.3 — state machine contract: play() keeps the additive layer
    // persistent. Swapping the base clip does NOT touch _additiveClip /
    // _additiveTime / _additiveTracks / _additivePendingNotifies.
    // UPGRADE-HOOK(P1.5): if a future stack-based layer model needs to
    // also rebind on base swap, document the trigger here.
}

void AnimationPlayer::stop()
{
    _time   = 0.0f;
    _paused = false;
    // Phase 1.5: reset the dispatch cursor so a later play() doesn't
    // re-fire markers across [0, prevSavedTime) spuriously.
    _prevTickTime = 0.0f;
    _pendingNotifies.clear();

    // P1.3 — stop() disposes BOTH layers. State machine contract: stop
    // is "playback halted + sources unbound". clearAdditiveSource drains
    // the additive queue and nulls _additiveClip; _blendWeight is kept
    // so the host can re-bind a new source without re-setting the weight.
    //
    // UPGRADE-HOOK(P1.5): when stack-based layers ship, iterate the
    // stack and dispose every slot here.
    clearAdditiveSource();
}

// ---------------------------------------------------------------------------
// P1.3 — Additive Layer 2 (Cross-Fade)
// ---------------------------------------------------------------------------
//
// setAdditiveSource / clearAdditiveSource are state-machine entry point 3.
// They own the six additive-layer fields listed in the header:
//   _additiveClip, _additiveTime, _additivePlayRate, _additiveLoop,
//   _additivePrevTickTime, _additiveTracks, _additivePendingNotifies
// The BASE source (_baseClip / _tracks / _time) is intentionally untouched
// — UE UAnimMontage / Unity additive layer semantics.
//
// UPGRADE-HOOK(P1.5): when stack-based layers ship, this becomes a
// single-slot bind; the implementation pattern (TrackSlice rebuild +
// notify queue reset + prev-tick reset) generalizes 1:1.
void AnimationPlayer::setAdditiveSource(const ayt::resource::IAnimation* src,
                                        float playRate,
                                        bool  loop)
{
    if (src == nullptr) {
        clearAdditiveSource();
        return;
    }

    _additiveClip         = src;
    _additiveTime         = 0.0f;
    _additivePrevTickTime = 0.0f;
    _additivePlayRate     = playRate;
    _additiveLoop         = loop;
    _additivePendingNotifies.clear();

    // Rebuild _additiveTracks — same TrackSlice pattern as play() above,
    // including the P0 alignment workaround (flat float unflatten into
    // our own FVector3/FQuaternion containers).
    _additiveTracks.clear();
    const float tps = src->getTicksPerSecond();
    const float invTps = (tps > 0.0f) ? (1.0f / tps) : 1.0f;

    const uint32_t trackCount = src->getTrackCount();
    _additiveTracks.reserve(trackCount);
    for (uint32_t ti = 0; ti < trackCount; ++ti) {
        TrackSlice slice;
        slice.nodeName = (src->getTrackNodeName(ti) != nullptr)
                             ? std::string(src->getTrackNodeName(ti))
                             : std::string();
        slice.property = (src->getTrackProperty(ti) != nullptr)
                             ? std::string(src->getTrackProperty(ti))
                             : std::string();
        slice.type     = src->getTrackType(ti);
        // Per-track blendMode honored from the additive source's own
        // AnimBlendMode byte (P1.2 contract).
        slice.blendMode = src->getTrackBlendMode(ti);

        const uint32_t keyCount = src->getTrackKeyframeCount(ti);
        const float* rawTimes = src->getTrackTimes(ti);
        slice.timesSec.assign(keyCount, 0.0f);
        for (uint32_t k = 0; k < keyCount; ++k) {
            slice.timesSec[k] = rawTimes[k] * invTps;
        }

        // P0 alignment workaround — mirror play() body. The flat
        // buffer read goes through our own aligned std::vector<FVector3>
        // / std::vector<FQuaternion> rather than the AYResource
        // reinterpret_cast that aliases unaligned memory.
        const float* flat = src->getTrackFloatValues(ti);
        switch (slice.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                if (flat != nullptr && keyCount > 0) {
                    slice.vec3Values.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.vec3Values[k] = ayt::math::FVector3(
                            flat[k * 3 + 0],
                            flat[k * 3 + 1],
                            flat[k * 3 + 2]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                if (flat != nullptr && keyCount > 0) {
                    slice.quatValues.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.quatValues[k] = ayt::math::FQuaternion(
                            flat[k * 4 + 0],
                            flat[k * 4 + 1],
                            flat[k * 4 + 2],
                            flat[k * 4 + 3]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                if (flat != nullptr && keyCount > 0) {
                    slice.scalarValues.assign(flat, flat + keyCount);
                }
                break;
            }
        }
        _additiveTracks.push_back(std::move(slice));
    }

    // P1.4 — rebind resets the cross-fade config. A fresh additive
    // source means: no sync (independent axis is the default), no
    // ref-pose capture (rest-pose base is the default), no curve
    // (static _blendWeight wins), no additive-only pause. The host
    // can re-enable each option AFTER binding if desired — calling
    // setAdditiveSyncToBase(true) inside the same frame is fine.
    _syncToBase     = false;
    _refPoseCapture = false;
    _captureState   = CaptureState::Fresh;
    _additivePaused = false;
    _curve.active   = false;
}

void AnimationPlayer::clearAdditiveSource()
{
    _additiveClip         = nullptr;
    _additiveTime         = 0.0f;
    _additivePrevTickTime = 0.0f;
    _additivePendingNotifies.clear();
    _additiveTracks.clear();
    // NOTE: _additivePlayRate / _additiveLoop are NOT reset here — they
    // describe the additive slot's defaults, not its current state. A
    // rebind to a new source inherits the most recent setter values.
    // _blendWeight is intentionally preserved so the host can swap the
    // source without resetting the layer intensity.

    // P1.4 — cross-fade config also resets on unbound so a subsequent
    // re-bind via setAdditiveSource() starts in P1.3-vanilla state.
    _syncToBase     = false;
    _refPoseCapture = false;
    _captureState   = CaptureState::Fresh;
    _additivePaused = false;
    _curve.active   = false;
}

void AnimationPlayer::pause()  { _paused = true; }
void AnimationPlayer::resume() { _paused = false; }

void AnimationPlayer::setTime(float t)
{
    _time = t;
    if (_baseClip && _loop) {
        const float d = _baseClip->getDuration();
        if (d > 0.0f) {
            _time = t - std::floor(t / d) * d;   // wrap [0, d)
        }
    }
    // Phase 1.5: setTime is a seek. No notify fires on the seek itself;
    // mark prev = current so the next tick() fires anything in
    // [current, current + dt).
    _prevTickTime = _time;
    _pendingNotifies.clear();

    // P1.3 — entry B contract. setTime also jumps the additive playhead
    // independently. Mirrors the base behavior: wrap if additive loops,
    // set prev = current so the next tick fires [current, current + dt)
    // on the additive side, clear the additive notify queue (no marker
    // fires on the seek itself). Phase 1b of evaluate then samples the
    // additive tracks at the new _additiveTime.
    //
    // UPGRADE-HOOK(P1.4): when the cross-fade curve hooks land, the
    // additive setTime would also restart the weight curve.
    if (_additiveClip != nullptr) {
        _additiveTime = t;
        if (_additiveLoop) {
            const float dAdd = _additiveClip->getDuration();
            if (dAdd > 0.0f) {
                _additiveTime = t - std::floor(t / dAdd) * dAdd;
            }
        }
        _additivePrevTickTime = _additiveTime;
        _additivePendingNotifies.clear();
    }

    // P1.4 — sync-to-base lock-step. After the seek we force the
    // additive playhead to mirror _time so hosts can land both axes
    // on the same frame. Without sync-to-base the jump uses the
    // additive clip's own loop (P1.3 behavior).
    if (_syncToBase && _additiveClip != nullptr) {
        _additiveTime = _time;
        _additivePrevTickTime = _additiveTime;
    }

    // P1.4 — re-anchor the curve to the new playhead ONLY when the
    // seek jumped past the curve's active window. If _time now lies
    // INSIDE the window ([startTime, startTime+duration]) we leave
    // startTime alone so the sample-time value at the new _time
    // produces the right curve value; if it's OUTSIDE the curve
    // re-anchors so the host's next tick resumes a fresh curve run.
    //
    // Without this guard, a test that does blendWeightOverTime() at
    // t=0 then setTime(0.5) — i.e. "jump to the curve's midpoint" —
    // would re-anchor to 0.5 and the sample weight at 0.5 would be
    // `from`, not the eased midpoint. Curve would visibly "reset to
    // start" instead of "tracking the curve mid-sample".
    if (_curve.active) {
        const float curveStart = _curve.startTime;
        const float curveEnd   = curveStart + _curve.duration;
        if (_time < curveStart || _time > curveEnd) {
            _curve.startTime = _time;
        }
    }
}

// P1.3 — canonical weight setter (was setAdditiveWeight in P1.2; that
// name is preserved as an inline-forward in the header). Saturates to
// [0, 1]: negative input silently clamps to 0 (avoids q.pow(negative)
// NaN); > 1 clamps to 1. Hot-path caller (AnimationSystem) just forwards
// the component field, so a typo is silently corrected.
//
// UPGRADE-HOOK(P1.4): replace this discrete assign with a keyframed
// FloatCurve sampler (re-uses FloatCurveSink machinery).
// UPGRADE-HOOK(P1.4): replace uniform _blendWeight with per-track
// weights (mask expression) when per-bone masking ships.
void AnimationPlayer::setBlendWeight(float w)
{
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    _blendWeight = w;
}

void AnimationPlayer::tick(float dt)
{
    // Phase 1.5: when paused or no clip, keep prev in sync with _time so
    // the next un-paused tick won't fire markers across [prev, _time)
    // for the time spent paused.
    if (_paused || _baseClip == nullptr) {
        _prevTickTime = _time;
        // P1.3 — additive axis ticks onward even when base is paused in
        // MVP. Pause semantics for the additive source are P1.4.
        // UPGRADE-HOOK(P1.4): when additive axis also obeys pause,
        // sync _additivePrevTickTime = _additiveTime here.
        return;
    }

    const float prev = _time;
    _time += dt * _playRate;
    const float d = _baseClip->getDuration();
    bool wrapped = false;
    if (d > 0.0f) {
        if (_loop) {
            const float raw = _time;
            _time = _time - std::floor(_time / d) * d;   // wrap to [0, d)
            // wrapped == true if the raw (un-wrapped) value lay outside
            // [0, d) — i.e. we crossed at least one endpoint forward or
            // backward. Capture BEFORE _time assignment comparison by
            // checking `raw` vs [0, d).
            wrapped = (raw >= d) || (raw < 0.0f);
        } else if (_time > d) {
            _time = d;
            _paused = true;   // clamp to end-of-clip when not looping
        }
    }

    // Phase 1.5: dispatch any AnimNotifyMarker the new _time has crossed.
    // Both the host sink and the queue get the same record list, in
    // chronological order. Either path can be skipped (no sink → callback
    // no-op; consumePendingNotifies() empty → bus emit skipped by
    // AnimationSystem).
    dispatchPendingNotifies(prev, _time, wrapped);
    _prevTickTime = _time;

    // P1.3 + P1.4 — additive axis tick branches. Three mutually-exclusive
    // modes (priority top-to-bottom):
    //
    //   (a) _additivePaused (or _paused via INV-8): the additive time
    //       does NOT advance. Just sync the prev cursor to the current
    //       time so a later un-paused tick won't fire stale notifies.
    //
    //   (b) _syncToBase: the additive time is FORCED to equal _time
    //       after the base advance. The additive clip's own playRate
    //       and loop are overridden by the base clip. INV-6.
    //
    //   (c) P1.3 default: independent axis advance at _additivePlayRate,
    //       loop wrap per _additiveLoop, independent notify dispatch.
    //
    // Note: _paused path (top of tick, P1.3) already short-circuits ALL
    // of these with the early `return`. The branches below only matter
    // for additive-only pause and sync-to-base.
    if (_additiveClip != nullptr) {
        if (_additivePaused) {
            // INV-8 partial — additive axis halted but base keeps
            // ticking. Reset the prev cursor so a resume won't fire
            // a backlog of accumulated notifies.
            _additivePrevTickTime = _additiveTime;
        } else if (_syncToBase) {
            // INV-6 — lock-step. additive time mirrors _time; the base
            // clip's wrap semantics carry over (a looping base wraps
            // both axes in lock-step). We dispatch additive notifies
            // across the same window the base just traversed so the
            // two notify queues stay aligned frame-by-frame.
            const float prevAdd = _additivePrevTickTime;
            _additiveTime = _time;
            // Use the base's wrapped flag — both axes cross endpoints
            // at the same moment because they share _time.
            dispatchAdditiveNotifies(prevAdd, _additiveTime, wrapped);
            _additivePrevTickTime = _additiveTime;
        } else {
            // P1.3 — independent axis (unchanged).
            const float prevAdd = _additiveTime;
            _additiveTime += dt * _additivePlayRate;
            const float dAdd = _additiveClip->getDuration();
            bool wrappedAdd = false;
            if (dAdd > 0.0f) {
                if (_additiveLoop) {
                    const float rawAdd = _additiveTime;
                    _additiveTime = _additiveTime - std::floor(_additiveTime / dAdd) * dAdd;
                    wrappedAdd = (rawAdd >= dAdd) || (rawAdd < 0.0f);
                } else if (_additiveTime > dAdd) {
                    _additiveTime = dAdd;
                }
            }
            dispatchAdditiveNotifies(prevAdd, _additiveTime, wrappedAdd);
            _additivePrevTickTime = _additiveTime;
        }
    }

    // P1.4 — curve auto-disarm. After the tick finishes we re-evaluate
    // the curve's elapsed time and flip active → false if the window
    // is past. Disarming here (rather than inside evaluate) means the
    // curve doesn't visibly drag the layer for one extra frame past
    // its end. A host that wants a fresh curve simply calls
    // blendWeightOverTime() again — no need for cancelBlendCurve().
    if (_curve.active) {
        const float elapsed = _time - _curve.startTime;
        if (elapsed >= _curve.duration) {
            _curve.active = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 1.5 — AnimNotify dispatch
//
// dispatchPendingNotifies scans the [prev, next) interval (or wrap-aware
// [prev, d) ∪ [0, next) for looping clips crossing an endpoint) and emits
// one record per marker whose `time` lies in that interval. Backward
// seeking (`new < prev`) fires everything in [new, prev] in the same
// order-on-arrival regardless of direction — callers needing strict
// forward order across seeks should re-bind the clip.
//
// The implementation assumes markers are sorted ascending by time (the
// Converter enforces this). Linear scan with early-exit is correct for
// unsorted input up to one full iteration; for notify counts typical of
// production clips (10–100), linear is cheaper than binary search.
void AnimationPlayer::dispatchPendingNotifies(float prev,
                                              float next,
                                              bool  wrapped)
{
    if (_baseClip == nullptr) return;
    const uint32_t n = _baseClip->getNotifyCount();
    if (n == 0u) return;

    // Dual-exit dispatch (Phase 1.5 decision): every crossed marker is
    // both pushed to the per-frame _pendingNotifies queue (for AYEntity's
    // AnimationSystem → EventBus bridge) AND forwarded to the optional
    // host sink (for callers that prefer an inline callback). When no
    // host sink is registered we still build the queue so consumePending
    // Notifies() returns it.
    const bool wantSink = static_cast<bool>(_animNotifySink);

    auto fireOne = [&](uint32_t i) {
        const char* nm = _baseClip->getNotifyName(i);
        const float  tm = _baseClip->getNotifyTime(i);
        const float  pl = _baseClip->getNotifyPayload(i);
        if (wantSink) {
            _animNotifySink(nm, tm, pl);
        }
        _pendingNotifies.push_back({nm, tm, pl});
    };

    // Three cases share the same sorted-iterator machinery:
    //   (1) forward, no wrap       → scan [min(prev,next), max(prev,next)]
    //                                 — covers [prev, next] when prev <= next,
    //                                 and [next, prev] when next < prev
    //                                 (negative dt or seek backward).
    //   (2) forward+wrap (looping)  → scan [prev, d) ∪ [0, next]
    //                                 where prev > next (the jump back IS the
    //                                 wrap; the post-wrap time next is < prev).
    //
    // For (1) when next < prev, the semantics become "fire everything that
    // crossed in the backward direction"; we use closed-[prev, next]
    // behaviour because both seek and reverse-play cross markers in
    // reverse-time order — the records delivered in arrival order match
    // the order they would have fired under forward play.
    const float dur = _baseClip->getDuration();
    if (!wrapped) {
        const float lo = std::min(prev, next);
        const float hi = std::max(prev, next);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _baseClip->getNotifyTime(i);
            if (t < lo) continue;
            if (t > hi) break;          // sorted; nothing further to fire
            fireOne(i);
        }
    } else {
        // Forward loop wrap. Two contiguous sub-ranges:
        //   A = [prev, dur)
        //   B = [0, next]
        // Both endpoints inclusive for `prev` (the start of the wrap) and
        // exclusive for `next` (the new position is past the marker).
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _baseClip->getNotifyTime(i);
            const bool inA = (t >= prev) && (t <  dur);
            const bool inB = (t >= 0.0f) && (t <= next);
            if (inA || inB) fireOne(i);
        }
    }
}

const std::vector<AnimationPlayer::AnimNotifyRecord>&
AnimationPlayer::consumePendingNotifies()
{
    // Strict contract: caller consumes by reference, then we eagerly clear
    // so the next tick starts fresh. We use a static-thread-local return
    // slot and swap-into-it to avoid invalidating iterators the caller
    // holds during iteration. Cheap (one swap) and safe across frames.
    static thread_local std::vector<AnimNotifyRecord> s_returnSlot;
    s_returnSlot.clear();
    s_returnSlot.swap(_pendingNotifies);   // move our data out; queue now empty
    return s_returnSlot;
}

// ---------------------------------------------------------------------------
// P1.3 — Additive source notify dispatch (mirror of P1.2 dispatchPendingNotifies)
//
// Same scan-and-fire semantics as the base dispatch but reads from the
// additive source's notify list, prev-tick cursor, and queue. The host
// sink is shared between both sources in MVP — a P1.5 upgrade hook will
// add per-source tag + dedup. Until then, a host that wants to
// distinguish base vs additive markers needs to inspect its own callback
// context (e.g. a thread-local "current source" flag the host sets
// before each tick).
//
// The dual-exit sink + queue model from P1.2 is preserved 1:1.
// ---------------------------------------------------------------------------
void AnimationPlayer::dispatchAdditiveNotifies(float prev,
                                               float next,
                                               bool  wrapped)
{
    if (_additiveClip == nullptr) return;
    const uint32_t n = _additiveClip->getNotifyCount();
    if (n == 0u) return;

    const bool wantSink = static_cast<bool>(_animNotifySink);

    auto fireOne = [&](uint32_t i) {
        const char* nm = _additiveClip->getNotifyName(i);
        const float  tm = _additiveClip->getNotifyTime(i);
        const float  pl = _additiveClip->getNotifyPayload(i);
        if (wantSink) {
            _animNotifySink(nm, tm, pl);
        }
        _additivePendingNotifies.push_back({nm, tm, pl});
    };

    const float dur = _additiveClip->getDuration();
    if (!wrapped) {
        const float lo = std::min(prev, next);
        const float hi = std::max(prev, next);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _additiveClip->getNotifyTime(i);
            if (t < lo) continue;
            if (t > hi) break;
            fireOne(i);
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _additiveClip->getNotifyTime(i);
            const bool inA = (t >= prev) && (t <  dur);
            const bool inB = (t >= 0.0f) && (t <= next);
            if (inA || inB) fireOne(i);
        }
    }
}

const std::vector<AnimationPlayer::AnimNotifyRecord>&
AnimationPlayer::consumePendingNotifiesAdditive()
{
    // P1.3 — mirror of consumePendingNotifies() for the additive source's
    // queue. Same thread_local swap pattern so a host that drains both
    // queues per frame sees them independently. Returns by const ref;
    // the queue is cleared after the swap (same contract as base).
    //
    // UPGRADE-HOOK(P1.5): replace with consumeNotifiesMerged() that
    // returns a single sorted vector with a `source` enum tag per
    // record, with optional dedup-by-(time,name).
    static thread_local std::vector<AnimNotifyRecord> s_returnSlotAdd;
    s_returnSlotAdd.clear();
    s_returnSlotAdd.swap(_additivePendingNotifies);
    return s_returnSlotAdd;
}

void AnimationPlayer::evaluate()
{
    if (!isValid()) {
        return;
    }
    const size_t n = _skeleton->getBoneCount();
    if (n == 0) return;

    // P1.3 + P1.4 — INV-2 / INV-3 / INV-4 / INV-6 / INV-7 / INV-8 invariants.
    // Debug-only — Release builds skip the checks. These pin the contract
    // that downstream code (Phase 1a / 1b / capture / ref-pose) relies on:
    //   INV-2: _local{Pos,Rot,Scl}.size() matches skeleton bone count.
    //   INV-3: _blendWeight ∈ [0, 1].
    //   INV-4: degenerate base-null + additive-non-null early-noops
    //          AFTER the rest-pose seed (so callers see a stable pose
    //          even mid-rebind).
    //   INV-6: syncToBase → _additiveTime == _time (post-tick guard).
    //          Note: this assertion reflects the post-tick invariant;
    //          mid-evaluate the two may legitimately differ.
    //   INV-7: refPoseCapture + captureState must be coherent
    //          (captureState == Valid implies the buffers are sized).
    //   INV-8: _additivePaused ⇒ _additivePrevTickTime == _additiveTime
    //          (no backlog during pause).
#ifndef NDEBUG
    assert(_localPos.size() == n * 3 && "INV-2: _localPos size must match n*3");
    assert(_localRot.size() == n * 4 && "INV-2: _localRot size must match n*4");
    assert(_localScl.size() == n * 3 && "INV-2: _localScl size must match n*3");
    assert(_blendWeight >= 0.0f && _blendWeight <= 1.0f
           && "INV-3: _blendWeight must be in [0, 1] post-setter saturate");
    assert((_additivePaused == false) || (_additivePrevTickTime == _additiveTime)
           && "INV-8: additive pause must reset prev cursor");
    if (_refPoseCapture) {
        assert((_captureState == CaptureState::Fresh
                || _captureState == CaptureState::Valid
                || _captureState == CaptureState::Stale)
               && "INV-7: captureState must be a known value when ref-pose is on");
        if (_captureState == CaptureState::Valid) {
            assert(_capturedLocalPos.size() == n * 3
                   && "INV-7: Valid captureState must have buffers sized to n*3");
            assert(_capturedLocalRot.size() == n * 4
                   && "INV-7: Valid captureState must have buffers sized to n*4");
            assert(_capturedLocalScl.size() == n * 3
                   && "INV-7: Valid captureState must have buffers sized to n*3");
        }
    }
    if (_baseClip == nullptr && _additiveClip != nullptr) {
        // INV-4 — degenerate state. Still seed rest pose so the world/skin
        // matrices are well-defined, then return without running either
        // source's tracks. Caller can swap to a valid base and re-evaluate.
        const ayt::math::FVector3*    restPos = _skeleton->getLocalPositions();
        const ayt::math::FQuaternion* restRot = _skeleton->getLocalRotations();
        const ayt::math::FVector3*    restScl = _skeleton->getLocalScales();
        if (restPos != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localPos[i * 3 + 0] = restPos[i].x;
                _localPos[i * 3 + 1] = restPos[i].y;
                _localPos[i * 3 + 2] = restPos[i].z;
            }
        }
        if (restRot != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localRot[i * 4 + 0] = restRot[i].x;
                _localRot[i * 4 + 1] = restRot[i].y;
                _localRot[i * 4 + 2] = restRot[i].z;
                _localRot[i * 4 + 3] = restRot[i].w;
            }
        }
        if (restScl != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localScl[i * 3 + 0] = restScl[i].x;
                _localScl[i * 3 + 1] = restScl[i].y;
                _localScl[i * 3 + 2] = restScl[i].z;
            }
        }
        // Compute world/skin from rest pose (Phase 2 + 3 essentially),
        // then return — skip Phase 1a/1b entirely.
        const ayt::resource::Bone* bones = _skeleton->getBones();
        for (size_t i = 0; i < n; ++i) {
            const int p = bones[i].parentIndex;
            const ayt::math::FVector3    lp(_localPos[i * 3 + 0],
                                            _localPos[i * 3 + 1],
                                            _localPos[i * 3 + 2]);
            const ayt::math::FQuaternion lr(_localRot[i * 4 + 0],
                                            _localRot[i * 4 + 1],
                                            _localRot[i * 4 + 2],
                                            _localRot[i * 4 + 3]);
            const ayt::math::FVector3    ls(_localScl[i * 3 + 0],
                                            _localScl[i * 3 + 1],
                                            _localScl[i * 3 + 2]);
            const ayt::math::Float4x4 local = ayt::math::Float4x4::fromTRS(lp, lr, ls);
            if (p < 0) {
                _world[i] = local;
            } else {
                _world[i] = _world[static_cast<size_t>(p)] * local;
            }
        }
        const ayt::math::Float4x4* ibm = _skeleton->getInverseBindMatrices();
        for (size_t i = 0; i < n; ++i) {
            _skin[i] = (ibm != nullptr) ? (_world[i] * ibm[i]) : _world[i];
        }
        return;
    }
#endif

    // Re-seed local TRS from rest pose every frame (cheaper than tracking
    // which bones got a track written into last frame).
    const ayt::math::FVector3*    restPos = _skeleton->getLocalPositions();
    const ayt::math::FQuaternion* restRot = _skeleton->getLocalRotations();
    const ayt::math::FVector3*    restScl = _skeleton->getLocalScales();
    if (restPos != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localPos[i * 3 + 0] = restPos[i].x;
            _localPos[i * 3 + 1] = restPos[i].y;
            _localPos[i * 3 + 2] = restPos[i].z;
        }
    }
    if (restRot != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localRot[i * 4 + 0] = restRot[i].x;
            _localRot[i * 4 + 1] = restRot[i].y;
            _localRot[i * 4 + 2] = restRot[i].z;
            _localRot[i * 4 + 3] = restRot[i].w;
        }
    }
    if (restScl != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localScl[i * 3 + 0] = restScl[i].x;
            _localScl[i * 3 + 1] = restScl[i].y;
            _localScl[i * 3 + 2] = restScl[i].z;
        }
    }

    // P1.4 — ref-pose capture Phase 0 dispatch. The block above just
    // seeded _local* from the skeleton's rest pose; that seeded buffer
    // is exactly the "ref" the Phase 1a additive layer would want if
    // setAdditiveRefPoseCapture(false). When the host has enabled
    // ref-pose capture (state = Fresh or Stale), we snapshot the
    // just-seeded _local* into _capturedLocal* and mark Valid so
    // subsequent evaluates Phase 1a runs first (writing fresh _local*
    // from base tracks) and re-snapshots below.
    //
    // When captureState == Valid (the steady state during a session),
    // we re-seed _local* from the captured buffer instead of from
    // rest pose — so Phase 1b's additive reads from a buffer that
    // represents the base pose the host has been playing, not the
    // initial bind pose.
    //
    // INV-7 contract enforcement point.
    if (_refPoseCapture) {
        if (_captureState == CaptureState::Fresh
            || _captureState == CaptureState::Stale) {
            captureRefPoseFromLocal();
            _captureState = CaptureState::Valid;
        } else {
            // Valid: re-seed _local* from captured for Phase 1b's
            // additive to layer against.
            applyCapturedRefPoseToLocal();
        }
    }

    // Phase 1 — sample every track at _time, write into the matched bone's
    // local slot. Tracks whose nodeName doesn't resolve are silently skipped
    // (plan §5.4 — missing optional animation must not crash).
    const bool hasSink = static_cast<bool>(_floatSink);
    for (TrackSlice& tr : _tracks) {
        // P1.4 — boneIdx cache lookup. tr.boneIdx is either a valid
        // bone index (>= 0), the cached "not found" sentinel (-1), or
        // kBoneUnresolved (no skeleton yet OR first-frame lazy resolve).
        // resolveBoneIdxOnce promotes the third case to one of the first
        // two on first invocation; subsequent frames hit the cache.
        //
        // Cache is invalidated by setSkeleton() (via
        // invalidateBoneIndexCache); play() / setAdditiveSource() leave
        // it alone because they don't change the skeleton's name table —
        // newly pushed slices start at kBoneUnresolved and are resolved
        // on first evaluate().
        resolveBoneIdxOnce(tr);
        const int boneIdx = tr.boneIdx;
        if (boneIdx < 0) {
            // Even if no matching bone exists, float tracks still need to
            // surface their value to the sink so the host can wire it
            // to arbitrary gameplay parameters (attack damage, blend
            // weights, etc.). Only when a bone IS found does the track
            // also drive local TRS.
            if (hasSink && tr.type == ayt::resource::AnimTrackType::Float) {
                float v = 0.0f;
                sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                 tr.timesSec, _time, v);
                _floatSink(tr.nodeName.c_str(),
                           tr.property.empty() ? nullptr : tr.property.c_str(),
                           v);
            }
            continue;
        }
        const size_t idx = static_cast<size_t>(boneIdx);

        switch (tr.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                ayt::math::FVector3 v;
                sampleTrackVector3(tr.vec3Values.data(), tr.timesSec.size(),
                                   tr.timesSec, _time, v);
                if (tr.property == "position") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        // Pre-P1.2 path — byte-identical.
                        _localPos[idx * 3 + 0] = v.x;
                        _localPos[idx * 3 + 1] = v.y;
                        _localPos[idx * 3 + 2] = v.z;
                    } else {
                        // Phase 1.2 Additive Layer 1: position delta = sample * weight
                        // added on top of whatever the rest pose or a prior
                        // Override track seeded into _localPos. The rest-pose
                        // seed at the top of evaluate() is the "base" — additive
                        // only makes sense layered above an Override track that
                        // supplied the same bone's pose, but the math is
                        // well-defined either way (delta accumulates).
                        const float w = _blendWeight;
                        _localPos[idx * 3 + 0] += v.x * w;
                        _localPos[idx * 3 + 1] += v.y * w;
                        _localPos[idx * 3 + 2] += v.z * w;
                    }
                } else if (tr.property == "scale") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        _localScl[idx * 3 + 0] = v.x;
                        _localScl[idx * 3 + 1] = v.y;
                        _localScl[idx * 3 + 2] = v.z;
                    } else {
                        // Phase 1.2 Additive Layer 1: scale delta is RELATIVE
                        // (UE convention). base *= (1 + sample * weight) keeps
                        // scale from collapsing to zero when an additive clip
                        // pushes a negative delta — additive-on-negative
                        // shrinks, but never flips the mesh inside-out.
                        const float w = _blendWeight;
                        _localScl[idx * 3 + 0] *= (1.0f + v.x * w);
                        _localScl[idx * 3 + 1] *= (1.0f + v.y * w);
                        _localScl[idx * 3 + 2] *= (1.0f + v.z * w);
                    }
                }
                // Unknown Vector3 property: silently ignored.
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                ayt::math::FQuaternion q;
                sampleTrackQuaternion(tr.quatValues.data(), tr.timesSec.size(),
                                      tr.timesSec, _time, q);
                if (tr.property == "rotation") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        // Pre-P1.2 path — byte-identical.
                        _localRot[idx * 4 + 0] = q.x;
                        _localRot[idx * 4 + 1] = q.y;
                        _localRot[idx * 4 + 2] = q.z;
                        _localRot[idx * 4 + 3] = q.w;
                    } else {
                        // Phase 1.2 Additive Layer 1: rotation delta.
                        //   result = (base * sample.pow(_blendWeight)).normalize()
                        //
                        // Why pow and not literal `*`: a quaternion has only
                        // a single rotation amount (the angle); `*` composes
                        // two rotations fully (angle 90° + angle 90° = 180°),
                        // which is NOT what "additive at half weight" should
                        // give us (angle 45°). pow(weight) scales the angle
                        // proportionally so weight=0.5 means "half the delta",
                        // weight=1.0 means the full delta, weight=0 means
                        // "no rotation contribution" → identity.
                        //
                        // Safety: when _blendWeight is exactly 0 we skip
                        // the pow() call entirely. MathTypes::FQuaternion::pow
                        // (MathTypes.cpp:1777-1780) returns the *original*
                        // quaternion in its `sinHalfAngle < 1e-6f` branch,
                        // NOT identity — so without this early-return,
                        // weight=0 would yield `base * sample` instead of
                        // `base`. The early-return keeps the no-op guarantee.
                        if (_blendWeight <= 0.0f) {
                            // leave _localRot untouched — the rest-pose seed
                            // (or prior Override track's write) stays in place.
                        } else {
                            ayt::math::FQuaternion base(
                                _localRot[idx * 4 + 0],
                                _localRot[idx * 4 + 1],
                                _localRot[idx * 4 + 2],
                                _localRot[idx * 4 + 3]);
                            ayt::math::FQuaternion scaled = q.pow(_blendWeight);
                            ayt::math::FQuaternion blended = (base * scaled).normalize();
                            _localRot[idx * 4 + 0] = blended.x;
                            _localRot[idx * 4 + 1] = blended.y;
                            _localRot[idx * 4 + 2] = blended.z;
                            _localRot[idx * 4 + 3] = blended.w;
                        }
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                // Phase 1.2: Float tracks are unaffected by blendMode —
                // additive is a local-TRS concept. Float curves go through
                // the sink the same way regardless of mode.
                float v = 0.0f;
                sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                 tr.timesSec, _time, v);
                if (hasSink) {
                    _floatSink(tr.nodeName.c_str(),
                               tr.property.empty() ? nullptr : tr.property.c_str(),
                               v);
                }
                break;
            }
        }
    }

    // P1.4 — ref-pose capture AFTER Phase 1a has written all base
    // tracks. The buffer is now holding whatever the base pose chose
    // to express at the current _time (Override tracks either
    // overwritten or seeded from rest pose in the no-track case).
    // Phase 1b will read from this captured base, so the additive
    // deltas layer on top of the actual base pose, not the rest pose.
    // Without this call, the next evaluate() would see the captured
    // buffer as one-frame stale relative to the live Phase 1a writes.
    if (_refPoseCapture && _captureState == CaptureState::Valid) {
        captureRefPoseFromLocal();
    }

    // P1.4 — effective weight for the gate and the per-track math comes
    // from sampleBlendCurve() (which returns _blendWeight when the
    // curve is inactive). Computing it once per evaluate() avoids the
    // curve walk firing on every track and lets isAdditiveLayerActive()
    // see the same value the Phase 1b body uses.
    const float effectiveWeight = sampleBlendCurve();

    // Phase 1b — P1.3 + P1.4 ADDITIVE LAYER 2 (Cross-Fade + curve).
    //
    // Gated by INV-1: `_additiveClip != nullptr && effectiveWeight > 0.f`.
    // null and zero-weight are equivalent "off" states; the layer-off
    // contract is bit-identical to P1.2 (Phase 1a output is the final
    // pose). UPGRADE-HOOK(P1.5): when a stack of layers ships, this
    // gate generalizes to "for each slot with non-zero weight".
    //
    // Sample-time axis is the additive source's own _additiveTime
    // (independent of _time per UE UAnimMontage semantics — see tick()
    // header for the rationale).
    //
    // Math contract (P1.2 three formulas reused, `_additiveWeight`
    // renamed to `_blendWeight`; P1.4 swaps static _blendWeight for
    // effectiveWeight = sampleBlendCurve() so a keyframed blend curve
    // drives the layer-mix amplitude through the same math):
    //   position: _localPos[idx*3+k] += sample[k] * effectiveWeight
    //   rotation: weight==0 early-return; else base * q.pow(w).normalize()
    //   scale:    _localScl[idx*3+k] *= (1 + sample[k] * effectiveWeight)
    //   Float:    additive source Float tracks sink via _floatSink
    //             (P1.2 invariant: additive concept is local-TRS only)
    //
    // Per-track blendMode is honored from the additive source's own
    // AnimBlendMode byte — an additive source track flagged Override
    // will simply write to _local* like a base Override track (rare
    // case; the typical pattern is "additive clip authored with all
    // Additive tracks").
    if (_additiveClip != nullptr && effectiveWeight > 0.0f) {
        for (TrackSlice& tr : _additiveTracks) {
            // P1.4 — same boneIdx cache as Phase 1a. Lazy-resolved on
            // first evaluate(); invalidated by setSkeleton(). Same
            // sentinel semantics (-1 = cached miss, kBoneUnresolved =
            // lazy resolve).
            resolveBoneIdxOnce(tr);
            const int boneIdx = tr.boneIdx;
            if (boneIdx < 0) {
                // Same orphan-Track policy as Phase 1a: Float tracks
                // surface to the sink regardless of bone match.
                if (hasSink && tr.type == ayt::resource::AnimTrackType::Float) {
                    float v = 0.0f;
                    sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                     tr.timesSec, _additiveTime, v);
                    _floatSink(tr.nodeName.c_str(),
                               tr.property.empty() ? nullptr : tr.property.c_str(),
                               v);
                }
                continue;
            }
            const size_t idx = static_cast<size_t>(boneIdx);

            switch (tr.type) {
                case ayt::resource::AnimTrackType::Vector3: {
                    ayt::math::FVector3 v;
                    sampleTrackVector3(tr.vec3Values.data(), tr.timesSec.size(),
                                       tr.timesSec, _additiveTime, v);
                    if (tr.property == "position") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            // Additive-source Override track — write
                            // directly on top of base. The blendWeight
                            // is intentionally NOT applied here (it would
                            // be a confusing semantic for an Override
                            // track — callers wanting partial override
                            // should mark the track Additive and ship
                            // the delta they want).
                            _localPos[idx * 3 + 0] = v.x;
                            _localPos[idx * 3 + 1] = v.y;
                            _localPos[idx * 3 + 2] = v.z;
                        } else {
                            const float w = effectiveWeight;
                            _localPos[idx * 3 + 0] += v.x * w;
                            _localPos[idx * 3 + 1] += v.y * w;
                            _localPos[idx * 3 + 2] += v.z * w;
                        }
                    } else if (tr.property == "scale") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            _localScl[idx * 3 + 0] = v.x;
                            _localScl[idx * 3 + 1] = v.y;
                            _localScl[idx * 3 + 2] = v.z;
                        } else {
                            const float w = effectiveWeight;
                            _localScl[idx * 3 + 0] *= (1.0f + v.x * w);
                            _localScl[idx * 3 + 1] *= (1.0f + v.y * w);
                            _localScl[idx * 3 + 2] *= (1.0f + v.z * w);
                        }
                    }
                    break;
                }
                case ayt::resource::AnimTrackType::Quaternion: {
                    ayt::math::FQuaternion q;
                    sampleTrackQuaternion(tr.quatValues.data(), tr.timesSec.size(),
                                          tr.timesSec, _additiveTime, q);
                    if (tr.property == "rotation") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            _localRot[idx * 4 + 0] = q.x;
                            _localRot[idx * 4 + 1] = q.y;
                            _localRot[idx * 4 + 2] = q.z;
                            _localRot[idx * 4 + 3] = q.w;
                        } else {
                            // P1.2 rotation safety: weight==0 early-return
                            // prevents q.pow(0) returning the original q
                            // (instead of identity) in MathTypes' degenerate
                            // sinHalfAngle<1e-6f branch. Result is that
                            // _localRot is unchanged from Phase 1a's seed.
                            if (effectiveWeight <= 0.0f) {
                                // skip — rest pose or prior Override wins
                            } else {
                                ayt::math::FQuaternion base(
                                    _localRot[idx * 4 + 0],
                                    _localRot[idx * 4 + 1],
                                    _localRot[idx * 4 + 2],
                                    _localRot[idx * 4 + 3]);
                                ayt::math::FQuaternion scaled = q.pow(effectiveWeight);
                                ayt::math::FQuaternion blended = (base * scaled).normalize();
                                _localRot[idx * 4 + 0] = blended.x;
                                _localRot[idx * 4 + 1] = blended.y;
                                _localRot[idx * 4 + 2] = blended.z;
                                _localRot[idx * 4 + 3] = blended.w;
                            }
                        }
                    }
                    break;
                }
                case ayt::resource::AnimTrackType::Float: {
                    // P1.2 invariant: Float tracks go to the host sink
                    // regardless of blendMode. Additive source Float
                    // surfaces at _additiveTime so the host can wire
                    // gameplay parameters that ride along with the
                    // additive layer (e.g. hit-react damage).
                    float v = 0.0f;
                    sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                     tr.timesSec, _additiveTime, v);
                    if (hasSink) {
                        _floatSink(tr.nodeName.c_str(),
                                   tr.property.empty() ? nullptr : tr.property.c_str(),
                                   v);
                    }
                    break;
                }
            }
        }
    }

    // Phase 2 — accumulate world = parent.world * localTRS.
    // AYResource loaders guarantee parent-before-child; assert holds.
    const ayt::resource::Bone* bones = _skeleton->getBones();
    for (size_t i = 0; i < n; ++i) {
        const int p = bones[i].parentIndex;
        const ayt::math::FVector3    lp(_localPos[i * 3 + 0],
                                        _localPos[i * 3 + 1],
                                        _localPos[i * 3 + 2]);
        const ayt::math::FQuaternion lr(_localRot[i * 4 + 0],
                                        _localRot[i * 4 + 1],
                                        _localRot[i * 4 + 2],
                                        _localRot[i * 4 + 3]);
        const ayt::math::FVector3    ls(_localScl[i * 3 + 0],
                                        _localScl[i * 3 + 1],
                                        _localScl[i * 3 + 2]);
        const ayt::math::Float4x4 local = ayt::math::Float4x4::fromTRS(lp, lr, ls);
        if (p < 0) {
            _world[i] = local;
        } else {
            _world[i] = _world[static_cast<size_t>(p)] * local;
        }
    }

    // Phase 3 — skin matrix = world * inverseBindMatrix. AYResource's
    // Bone.inverseBindMatrix = bindWorld.inverse() (see SuzanneSkinnedDemo
    // convention); this multiplication places each vertex in mesh-local
    // space relative to the animated bone's world transform.
    const ayt::math::Float4x4* ibm = _skeleton->getInverseBindMatrices();
    for (size_t i = 0; i < n; ++i) {
        if (ibm != nullptr) {
            _skin[i] = _world[i] * ibm[i];
        } else {
            _skin[i] = _world[i];
        }
    }
}

// ---------------------------------------------------------------------------
// P1.4 — Cross-fade (curve / syncToBase / ref-pose / additive pause)
// ---------------------------------------------------------------------------
//
// Four runtime-config entry points layered on top of the P1.3 dual-source
// state machine. Defaults preserve P1.3 behavior bit-for-bit:
//
//   _syncToBase     = false   → additive axis independent (P1.3 default)
//   _refPoseCapture = false   → Phase 1b uses rest pose (P1.4 forced-on
//                                replaces this with capture-and-reuse)
//   _curve.active   = false   → sampleBlendCurve() returns _blendWeight
//   _additivePaused = false   → additive axis ticks onward
//
// The four entry points + cancelBlendCurve() are below, followed by the
// three private helpers (sampleBlendCurve + capture/applied ref-pose +
// the easing dispatch).

// P1.4 entry 6 — setAdditiveSyncToBase.
//
// Locks the additive playhead to _time after every tick. The Additive
// clip's own playRate / loop are overridden while this is on — a host
// that wants partial fade without lock-step should leave this at false
// and use a keyframed curve instead. We snap the additive time to _time
// here (rather than waiting for the next tick) so a host that toggles
// the flag mid-frame sees consistent state immediately.
//
// UPGRADE-HOOK(P1.5): once the vector<AdditiveSlot> stack lands, this
// becomes a per-slot bool.
void AnimationPlayer::setAdditiveSyncToBase(bool enabled)
{
    _syncToBase = enabled;
    if (enabled && _additiveClip != nullptr) {
        // INV-6 — snap immediately. Without this a host that toggled
        // the flag mid-frame could see _additiveTime stale-relative-
        // to _time until the next tick; that manifests as a visual
        // hitch on the additive layer's first frame after enable.
        _additiveTime = _time;
        _additivePrevTickTime = _additiveTime;
    }
}

// P1.4 entry 7 — setAdditiveRefPoseCapture.
//
// Toggles the ref-pose capture path. When transitioning from OFF → ON,
// the next evaluate() runs the Phase 0 capture-once path
// (captureRefPoseFromLocal + state → Valid). When transitioning ON → OFF
// the state goes back to Fresh so a future re-enable doesn't see a
// stale buffer from a prior skeleton.
//
// Note: turning this ON does NOT recapture immediately — the
// deterministic "snapshot now" call would require evaluating the base
// pose right here, which Phase 0 of evaluate() does better (it has the
// rest-pose-seeded _local* in hand). The lazy path is intentional.
void AnimationPlayer::setAdditiveRefPoseCapture(bool enabled)
{
    _refPoseCapture = enabled;
    if (!enabled) {
        // Reset for a clean re-enable later. _capturedLocal* stays
        // sized so phase 0 doesn't need to re-allocate, but the
        // state-machine flags them as "not currently in use".
        _captureState = CaptureState::Fresh;
    } else if (_captureState == CaptureState::Fresh && _skeleton != nullptr) {
        // First-time-on path: capture now against the seeded _local*.
        // Safe because setSkeleton() already rest-pose-seeded _local*
        // and INV-2 guards against size mismatches.
        captureRefPoseFromLocal();
        _captureState = CaptureState::Valid;
    }
}

// P1.4 entry 8 — blendWeightOverTime.
//
// Keyframed weight driver. Saturates from/to to [0, 1] (mirrors
// setBlendWeight's defensive clamp). duration ≤ 0 is rejected —
// callers want a static weight should use setBlendWeight instead, or
// explicitly call cancelBlendCurve() afterwards.
//
// startTime is anchored to the CURRENT _time at the call site so the
// curve starts "now". A later setTime() seek will re-anchor if the
// curve is still active (see setTime body).
//
// Past the duration window the curve auto-disarms inside tick(); the
// host does NOT have to call cancelBlendCurve() between successive
// blend-ins.
void AnimationPlayer::blendWeightOverTime(float from,
                                          float to,
                                          float duration,
                                          BlendEasing easing)
{
    if (duration <= 0.0f) {
        // Defensive no-op; caller should have used setBlendWeight or
        // set the duration in advance. We DON'T cancel an existing
        // curve here because the caller might have intended a single
        // static rest, not "kill the in-flight fade".
        return;
    }
    if (from < 0.0f) from = 0.0f;
    if (from > 1.0f) from = 1.0f;
    if (to   < 0.0f) to   = 0.0f;
    if (to   > 1.0f) to   = 1.0f;

    _curve.from      = from;
    _curve.to        = to;
    _curve.duration  = duration;
    _curve.easing    = easing;
    _curve.startTime = _time;
    _curve.active    = true;
}

// P1.4 Aux D — cancelBlendCurve. Disarm any in-flight curve WITHOUT
// touching _blendWeight. After this call the static _blendWeight takes
// over immediately (the curve no longer drives sampleBlendCurve()).
void AnimationPlayer::cancelBlendCurve()
{
    _curve.active = false;
}

// P1.4 Aux E — setAdditivePaused. Halts ONLY the additive axis. Base
// keeps ticking regardless. Resetting the prev-tick cursor on resume
// is critical — without it, every marker the playhead "would have
// crossed" during the pause fires in a backlog when ticks resume.
void AnimationPlayer::setAdditivePaused(bool paused)
{
    _additivePaused = paused;
    if (!paused && _additiveClip != nullptr) {
        // INV-8 partial — re-sync the cursor so we don't fire the
        // accumulated notify backlog. Mirrors what setTime does.
        _additivePrevTickTime = _additiveTime;
        _additivePendingNotifies.clear();
    }
}

// P1.4 helper — sampleBlendCurve. Returns the currently-effective
// layer-mix weight. Called from Phase 1b's gate and body, and from
// isAdditiveLayerActive(). Because the same value is used in two
// places per evaluate, we DO cache it in a local at the call site
// inside evaluate() — see the const float effectiveWeight above.
float AnimationPlayer::sampleBlendCurve() const
{
    if (!_curve.active) {
        return _blendWeight;
    }
    const float elapsed = _time - _curve.startTime;
    if (_curve.duration <= 0.0f) {
        // Defensive — shouldn't happen because blendWeightOverTime
        // rejects duration ≤ 0, but be safe.
        return _curve.to;
    }
    const float t01 = std::min(1.0f, std::max(0.0f, elapsed / _curve.duration));
    const float eased = applyEasing(t01, _curve.easing);
    // Manual lerp; we deliberately don't reuse sampleTrackFloat here
    // because we'd need to allocate a 2-key buffer per call and the
    // math is trivial.
    return _curve.from + (_curve.to - _curve.from) * eased;
}

// P1.4 helper — captureRefPoseFromLocal. Snapshots the per-bone local
// TRS into _capturedLocal*. Sized lazily; the first call after a
// setSkeleton() resize goes through the .assign path which both
// sizes AND copies (we pass _local* as the source).
//
// INV-7 — only callable when _capturedLocal* matches _local* in
// size; setSkeleton() always re-sizes the captured buffers in lockstep
// with the local ones.
void AnimationPlayer::captureRefPoseFromLocal()
{
    _capturedLocalPos.assign(_localPos.begin(), _localPos.end());
    _capturedLocalRot.assign(_localRot.begin(), _localRot.end());
    _capturedLocalScl.assign(_localScl.begin(), _localScl.end());
}

// P1.4 helper — applyCapturedRefPoseToLocal. Reverses the capture:
// _local* ← _capturedLocal*. Used in Phase 0 of evaluate() when
// ref-pose is on AND _captureState is Valid, so Phase 1a writes go
// from the captured base rather than the rest pose.
void AnimationPlayer::applyCapturedRefPoseToLocal()
{
    if (_capturedLocalPos.size() != _localPos.size()
        || _capturedLocalRot.size() != _localRot.size()
        || _capturedLocalScl.size() != _localScl.size()) {
        // Defensive — should never trip if INV-2 holds (setSkeleton
        // keeps the two buffer families synchronized). If it does trip,
        // we silently skip the apply rather than risk a buffer-overrun;
        // the rest-pose seed Phase 0 just ran is still in place.
        return;
    }
    std::copy(_capturedLocalPos.begin(), _capturedLocalPos.end(), _localPos.begin());
    std::copy(_capturedLocalRot.begin(), _capturedLocalRot.end(), _localRot.begin());
    std::copy(_capturedLocalScl.begin(), _capturedLocalScl.end(), _localScl.begin());
}

// P1.4 helper — applyEasing dispatch. Five cases: Linear is the
// identity (we let the lerp do its work), the four AYMath ones map
// 1:1 onto BlendEasing. Adding more easings (cubic-bezier, etc.)
// is a future PR.
float AnimationPlayer::applyEasing(float t01, BlendEasing e)
{
    switch (e) {
        case BlendEasing::Linear:
            return t01;
        case BlendEasing::EaseIn:
            // aymath::easeIn(Float32 t, Float32 power=2.0f) — MathUtils.h:1036
            return ayt::math::easeIn(t01, 2.0f);
        case BlendEasing::EaseOut:
            // aymath::easeOut(Float32 t, Float32 power=2.0f) — MathUtils.h:1098
            return ayt::math::easeOut(t01, 2.0f);
        case BlendEasing::EaseInOut:
            // aymath::easeInOut(Float32 t, Float32 power=2.0f) — MathUtils.h:1116
            return ayt::math::easeInOut(t01, 2.0f);
        case BlendEasing::Smoothstep:
            // aymath::smoothstep(Float32 t) — MathUtils.h:930
            return ayt::math::smoothstep(t01);
    }
    return t01;   // unreachable; satisfies -Wreturn-type on strict compilers
}

} // namespace ayt::anim