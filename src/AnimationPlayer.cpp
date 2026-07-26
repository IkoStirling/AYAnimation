#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/KeySampler.h>

#include <aymath/MathTypes.h>

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
}

void AnimationPlayer::play(const ayt::resource::IAnimation* anim)
{
    _anim = anim;
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
}

void AnimationPlayer::stop()
{
    _time   = 0.0f;
    _paused = false;
    // Phase 1.5: reset the dispatch cursor so a later play() doesn't
    // re-fire markers across [0, prevSavedTime) spuriously.
    _prevTickTime = 0.0f;
    _pendingNotifies.clear();
}

void AnimationPlayer::pause()  { _paused = true; }
void AnimationPlayer::resume() { _paused = false; }

void AnimationPlayer::setTime(float t)
{
    _time = t;
    if (_anim && _loop) {
        const float d = _anim->getDuration();
        if (d > 0.0f) {
            _time = t - std::floor(t / d) * d;   // wrap [0, d)
        }
    }
    // Phase 1.5: setTime is a seek. No notify fires on the seek itself;
    // mark prev = current so the next tick() fires anything in
    // [current, current + dt).
    _prevTickTime = _time;
    _pendingNotifies.clear();
}

// Phase 1.2 — saturating setter for the additive layer weight. Negative
// values would silently produce base * q.pow(negative) which yields NaN
// (quaternion power is undefined for negative scalars), so we clamp to
// [0, 1]. The hot-path caller (AnimationSystem) just forwards the
// component field, so saturating here is safe — a typo on the engine side
// ("-0.5 meant 0.5") is silently corrected instead of corrupting the pose.
void AnimationPlayer::setAdditiveWeight(float w)
{
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    _additiveWeight = w;
}

void AnimationPlayer::tick(float dt)
{
    // Phase 1.5: when paused or no clip, keep prev in sync with _time so
    // the next un-paused tick won't fire markers across [prev, _time)
    // for the time spent paused.
    if (_paused || _anim == nullptr) {
        _prevTickTime = _time;
        return;
    }

    const float prev = _time;
    _time += dt * _playRate;
    const float d = _anim->getDuration();
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
    if (_anim == nullptr) return;
    const uint32_t n = _anim->getNotifyCount();
    if (n == 0u) return;

    // Dual-exit dispatch (Phase 1.5 decision): every crossed marker is
    // both pushed to the per-frame _pendingNotifies queue (for AYEntity's
    // AnimationSystem → EventBus bridge) AND forwarded to the optional
    // host sink (for callers that prefer an inline callback). When no
    // host sink is registered we still build the queue so consumePending
    // Notifies() returns it.
    const bool wantSink = static_cast<bool>(_animNotifySink);

    auto fireOne = [&](uint32_t i) {
        const char* nm = _anim->getNotifyName(i);
        const float  tm = _anim->getNotifyTime(i);
        const float  pl = _anim->getNotifyPayload(i);
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
    const float dur = _anim->getDuration();
    if (!wrapped) {
        const float lo = std::min(prev, next);
        const float hi = std::max(prev, next);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _anim->getNotifyTime(i);
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
            const float t = _anim->getNotifyTime(i);
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

void AnimationPlayer::evaluate()
{
    if (!isValid()) {
        return;
    }
    const size_t n = _skeleton->getBoneCount();
    if (n == 0) return;

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

    // Phase 1 — sample every track at _time, write into the matched bone's
    // local slot. Tracks whose nodeName doesn't resolve are silently skipped
    // (plan §5.4 — missing optional animation must not crash).
    const bool hasSink = static_cast<bool>(_floatSink);
    for (const TrackSlice& tr : _tracks) {
        const int boneIdx = _skeleton->findBone(tr.nodeName.c_str());
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
                        const float w = _additiveWeight;
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
                        const float w = _additiveWeight;
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
                        //   result = (base * sample.pow(_additiveWeight)).normalize()
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
                        // Safety: when _additiveWeight is exactly 0 we skip
                        // the pow() call entirely. MathTypes::FQuaternion::pow
                        // (MathTypes.cpp:1777-1780) returns the *original*
                        // quaternion in its `sinHalfAngle < 1e-6f` branch,
                        // NOT identity — so without this early-return,
                        // weight=0 would yield `base * sample` instead of
                        // `base`. The early-return keeps the no-op guarantee.
                        if (_additiveWeight <= 0.0f) {
                            // leave _localRot untouched — the rest-pose seed
                            // (or prior Override track's write) stays in place.
                        } else {
                            ayt::math::FQuaternion base(
                                _localRot[idx * 4 + 0],
                                _localRot[idx * 4 + 1],
                                _localRot[idx * 4 + 2],
                                _localRot[idx * 4 + 3]);
                            ayt::math::FQuaternion scaled = q.pow(_additiveWeight);
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

} // namespace ayt::anim