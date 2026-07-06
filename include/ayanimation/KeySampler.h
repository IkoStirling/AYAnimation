#pragma once
#include "Animation.h"
#include <AYMathTypes.h>

namespace ayt::anim
{

// Sample a track at time t.
// - Empty times: writes zero/identity and returns.
// - Single key: writes that key's value (no interpolation).
// - Multiple keys: binary-searches the segment [times[k], times[k+1]] containing
//   t, then linearly interpolates between values[k] and values[k+1]. Outside the
//   time range the result is clamped to the nearest endpoint.
//
// Quaternion samples are normalized before returning to defend against numerical
// drift across many slerp calls.
void sampleTrackVector3   (const KeyframeTrack& tr, float t, ayt::math::FVector3&    out);
void sampleTrackQuaternion(const KeyframeTrack& tr, float t, ayt::math::FQuaternion& out);
void sampleTrackFloat     (const KeyframeTrack& tr, float t, float&                  out);

} // namespace ayt::anim