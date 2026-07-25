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
}

void AnimationPlayer::tick(float dt)
{
    if (_paused || _anim == nullptr) {
        return;
    }
    _time += dt * _playRate;
    const float d = _anim->getDuration();
    if (d > 0.0f) {
        if (_loop) {
            _time = _time - std::floor(_time / d) * d;   // wrap to [0, d)
        } else if (_time > d) {
            _time = d;
            _paused = true;   // clamp to end-of-clip when not looping
        }
    }
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
                    _localPos[idx * 3 + 0] = v.x;
                    _localPos[idx * 3 + 1] = v.y;
                    _localPos[idx * 3 + 2] = v.z;
                } else if (tr.property == "scale") {
                    _localScl[idx * 3 + 0] = v.x;
                    _localScl[idx * 3 + 1] = v.y;
                    _localScl[idx * 3 + 2] = v.z;
                }
                // Unknown Vector3 property: silently ignored.
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                ayt::math::FQuaternion q;
                sampleTrackQuaternion(tr.quatValues.data(), tr.timesSec.size(),
                                      tr.timesSec, _time, q);
                if (tr.property == "rotation") {
                    _localRot[idx * 4 + 0] = q.x;
                    _localRot[idx * 4 + 1] = q.y;
                    _localRot[idx * 4 + 2] = q.z;
                    _localRot[idx * 4 + 3] = q.w;
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
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