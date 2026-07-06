#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/KeySampler.h>

namespace ayt::anim
{

void AnimationPlayer::play(const Animation* anim)
{
    _anim   = anim;
    _time   = 0.0f;
    _paused = false;
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

    // First-time (or size-changed) init: seed internal TRS buffers from skeleton
    // rest pose and reserve world/skin matrices.
    if (_bonePos.size() != n) {
        _bonePos.assign(_skeleton->getLocalPositions(), _skeleton->getLocalPositions() + n);
        _boneRot.assign(_skeleton->getLocalRotations(), _skeleton->getLocalRotations() + n);
        _boneScl.assign(_skeleton->getLocalScales(),    _skeleton->getLocalScales()    + n);
        _world.assign(n, ayt::math::Float4x4::identity());
        _skin.assign(n,  ayt::math::Float4x4::identity());
    }

    // Phase 1 — sample every track at _time, writing into the matched bone's local slot.
    // Tracks whose nodeName doesn't resolve to a bone, or whose property string is
    // unrecognised, are silently skipped — plan §5.4 AN-01 says missing optional
    // animation must not crash.
    for (size_t ti = 0; ti < _anim->getTrackCount(); ++ti) {
        const KeyframeTrack& tr = _anim->getTrack(ti);
        const int boneIdx = _skeleton->findBone(tr.nodeName.c_str());
        if (boneIdx < 0) {
            continue;
        }

        if (tr.type == TrackType::Vector3) {
            ayt::math::FVector3 v;
            sampleTrackVector3(tr, _time, v);
            if      (tr.property == "position") _bonePos[static_cast<size_t>(boneIdx)] = v;
            else if (tr.property == "scale")    _boneScl[static_cast<size_t>(boneIdx)] = v;
        } else if (tr.type == TrackType::Quaternion) {
            ayt::math::FQuaternion q;
            sampleTrackQuaternion(tr, _time, q);
            if (tr.property == "rotation") {
                _boneRot[static_cast<size_t>(boneIdx)] = q;
            }
        } else if (tr.type == TrackType::Float) {
            // Float property mapping is not part of AN-01 (plan §2.3 deferred to Phase 1.5).
            // We still consume the value to keep track traversal uniform — but ignore it.
            float discard = 0.0f;
            sampleTrackFloat(tr, _time, discard);
            (void)discard;
        }
    }

    // Phase 2 — accumulate world = parent.world * localTRS.
    // Requires parent indices to precede children in skeleton order. AYResource's
    // FBXParser / SkeletonConverter guarantee this via DFS collection order.
    const Bone* bones = _skeleton->getBones();
    for (size_t i = 0; i < n; ++i) {
        const int p = bones[i].parentIndex;
        const ayt::math::Float4x4 local =
            ayt::math::Float4x4::fromTRS(_bonePos[i], _boneRot[i], _boneScl[i]);
        if (p < 0) {
            _world[i] = local;
        } else {
            _world[i] = _world[static_cast<size_t>(p)] * local;
        }
    }

    // Phase 3 — skin matrix = world * inverseBindMatrix.
    for (size_t i = 0; i < n; ++i) {
        _skin[i] = _world[i] * bones[i].inverseBindMatrix;
    }
}

} // namespace ayt::anim