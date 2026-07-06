#pragma once
#include "Animation.h"
#include "Skeleton.h"
#include <AYMathTypes.h>
#include <vector>

namespace ayt::anim
{

// ===== AnimationPlayer — single-clip playback + per-frame pose evaluation =====
//
// evaluate() runs three phases:
//   1. Sample every animation track at the current _time, writing into the bone's
//      local position / rotation / scale slot. Tracks whose nodeName doesn't
//      match any bone are silently skipped (plan §5.4 AN-01).
//   2. For each bone, build the local TRS matrix and accumulate world =
//      parent.world * local. Requires parent indices to precede child indices
//      in the skeleton order — the same invariant AYResource's FBX parser
//      guarantees via DFS.
//   3. Compute skin matrix = world * inverseBindMatrix, which is what the
//      AYRenderer SkinnedForwardPass (RD-04/05) will upload as bone matrices.
//
// tick(dt) advances _time by dt * playRate, wrapping to [0, duration) when loop
// is enabled (default true). pause() / resume() gate the dt accumulation.
class AnimationPlayer {
public:
    AnimationPlayer() = default;

    void setSkeleton(const Skeleton* skel) { _skeleton = skel; }
    void play(const Animation* anim);
    void stop();
    void pause();
    void resume();

    void  setTime(float t);
    void  setPlayRate(float r)        { _playRate = r; }
    void  setLoop(bool enabled)       { _loop = enabled; }

    float getTime() const             { return _time; }
    float getPlayRate() const         { return _playRate; }
    float getDuration() const         { return _anim ? _anim->getDuration() : 0.0f; }
    bool  isPaused() const            { return _paused; }

    bool isValid() const              { return _skeleton != nullptr && _anim != nullptr; }

    void tick(float dt);
    void evaluate();

    const ayt::math::FVector3*    getBoneLocalPositions() const { return _bonePos.data(); }
    const ayt::math::FQuaternion* getBoneLocalRotations() const { return _boneRot.data(); }
    const ayt::math::FVector3*    getBoneLocalScales()    const { return _boneScl.data(); }
    const ayt::math::Float4x4*    getBoneWorldMatrices()  const { return _world.data(); }
    const ayt::math::Float4x4*    getBoneSkinMatrices()   const { return _skin.data(); }

private:
    const Skeleton* _skeleton = nullptr;
    const Animation* _anim    = nullptr;

    float _time     = 0.0f;
    float _playRate = 1.0f;
    bool  _paused   = false;
    bool  _loop     = true;

    std::vector<ayt::math::FVector3>    _bonePos;
    std::vector<ayt::math::FQuaternion> _boneRot;
    std::vector<ayt::math::FVector3>    _boneScl;
    std::vector<ayt::math::Float4x4>    _world;
    std::vector<ayt::math::Float4x4>    _skin;
};

} // namespace ayt::anim