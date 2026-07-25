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

private:
    const ayt::resource::ISkeleton* _skeleton = nullptr;
    const ayt::resource::IAnimation* _anim     = nullptr;

    // Phase 1.5 — internal scan-fire-callback implementation. Declared
    // here, defined in AnimationPlayer.cpp. Public callers use the sink
    // setter + consumePendingNotifies() instead.
    void dispatchPendingNotifies(float prev, float next, bool wrapped);

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

    // Phase 1.5 — Anim Notify (see header comment above for the dual-exit
    // sink + queue model).
    AnimNotifySink                  _animNotifySink;
    std::vector<AnimNotifyRecord>  _pendingNotifies;
    float                           _prevTickTime = 0.0f;
};

} // namespace ayt::anim