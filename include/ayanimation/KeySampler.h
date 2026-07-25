#pragma once
// KeySampler.h — P0 (2026-07-26) signature change.
//
// Sample a track at time t. Inputs are now raw resource-side arrays
// (FVector3* / FQuaternion* / float* + keyCount + times[]).
// Empty / single-key short-circuits as before.
//
// P0 changes:
//   - Inputs are ayt::resource::AnimTrack-aligned (typed arrays).
//   - sampleTrackQuaternion now performs shortest-arc selection when
//     dot(a, b) < 0 to defend against visual twitching.
//   - Sample functions are responsible for normalizing quaternions on
//     return.
#include <aymath/MathTypes.h>
#include <assetsDefs/IAYAnimation.h>
#include <cstddef>
#include <vector>

namespace ayt::anim
{

void sampleTrackVector3(const ayt::math::FVector3* values,
                        size_t                     keyCount,
                        const std::vector<float>&  times,
                        float                      t,
                        ayt::math::FVector3&       out);

void sampleTrackQuaternion(const ayt::math::FQuaternion* values,
                           size_t                       keyCount,
                           const std::vector<float>&    times,
                           float                        t,
                           ayt::math::FQuaternion&      out);

void sampleTrackFloat(const float*             values,
                      size_t                   keyCount,
                      const std::vector<float>& times,
                      float                    t,
                      float&                   out);

} // namespace ayt::anim