#pragma once
// AnimationPlayer.h — P0 (2026-07-26) rewrite.
//
// Consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly;
// no parallel skeleton/animation types in ayt::anim.
//
// Lifecycle:
//   setSkeleton(ISkeleton*)   — bind the rest pose + IBM data source
//   play(IAnimation*)         — bind the clip; pre-normalizes track times
//                                from ticks to seconds in one pass
//   tick(dt)                  — advances _time (loop wrap / end-clamp)
//   evaluate()                — runs 3 phases (see AnimationPlayer.cpp)
//   getBoneSkinMatrices()     — renderer reads this each frame
//
// Float track sink:
//   evaluate() samples every Float track at _time and pushes
//   (nodeName, property, value) to the sink. Default sink is a no-op
//   so existing callers don't need to register one.

#include <aymath/MathTypes.h>
#include <assetsDefs/IAYAnimation.h>
#include <assetsDefs/IAYSkeleton.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ayt::anim
{

class AnimationPlayer {
public:
    // Sink signature: (boneName, property, value-at-_time).
    // Empty property means "node-only" parameter (not T/R/S bound).
    using FloatCurveSink = std::function<void(const char* nodeName,
                                              const char* property,
                                              float value)>;

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

    float getTime() const             { return _time; }
    float getPlayRate() const         { return _playRate; }
    float getDuration() const         { return _anim ? _anim->getDuration() : 0.0f; }
    bool  isPaused() const            { return _paused; }
    bool  isValid() const             { return _skeleton != nullptr && _anim != nullptr; }

    void tick(float dt);
    void evaluate();

    // === Matrix results (renderer reads) ===
    const ayt::math::Float4x4* getBoneWorldMatrices() const { return _world.data(); }
    const ayt::math::Float4x4* getBoneSkinMatrices()  const { return _skin.data();  }
    size_t                     getBoneCount()         const { return _world.size(); }

    // === Float curve sink ===
    // Replace the sink. Pass an empty std::function to disable.
    void setFloatCurveSink(FloatCurveSink sink) { _floatSink = std::move(sink); }

private:
    const ayt::resource::ISkeleton* _skeleton = nullptr;
    const ayt::resource::IAnimation* _anim     = nullptr;

    float _time     = 0.0f;
    float _playRate = 1.0f;
    bool  _paused   = false;
    bool  _loop     = true;

    // Per-bone local TRS working buffers (float-array form to avoid
    // per-frame allocate / align to P0 hot-path goal).
    std::vector<float> _localPos;     // n * 3
    std::vector<float> _localRot;     // n * 4
    std::vector<float> _localScl;     // n * 3
    std::vector<ayt::math::Float4x4> _world;
    std::vector<ayt::math::Float4x4> _skin;

    // Pre-normalized track data (built once in play()).
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
        std::vector<float>                  timesSec;
    };
    std::vector<TrackSlice> _tracks;

    FloatCurveSink _floatSink;
};

} // namespace ayt::anim