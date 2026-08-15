#include <AYAnimation/KeySampler.h>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace ayt::anim
{

namespace
{

// Locate the segment [k, k+1] such that times[k] <= t < times[k+1].
// Returns the lower key index k and the local fraction in [0, 1] for the segment.
// For t outside the time range the segment is clamped to the nearest endpoint —
// the caller must handle "frac == 0 at the last key" specially (do NOT read
// values[(k+1)*stride]) and the same applies symmetrically at the lower end.
// For kcount <= 1 the caller should treat the output as "single endpoint".
//
// P0 (2026-07-26): assert times[] is non-decreasing. Unsorted tracks are a
// loader bug; assert in debug, and in release we fall through to the
// degenerate "clamp to front" branch so evaluate() never reads OOB.
struct SegmentLoc {
    size_t k;        // lower key index, always in [0, kcount-1]
    float  frac;     // in-segment fraction, in [0, 1]
    bool   clamped;  // true when t was outside the time range — caller should
                     // NOT access values[(k+1)*stride] in this case
};

SegmentLoc locateSegment(const std::vector<float>& times, float t)
{
    const size_t n = times.size();
    if (n == 0) {
        return { 0, 0.0f, true };
    }
    if (n == 1) {
        return { 0, 0.0f, true };
    }

    // P0: monotonicity assert. Loaders (FBX converter) produce strictly
    // increasing times; defend against regression by asserting in debug.
#ifndef NDEBUG
    for (size_t i = 1; i < n; ++i) {
        assert(times[i] >= times[i - 1] && "KeySampler: times[] must be non-decreasing");
    }
#endif

    if (t <= times.front()) {
        return { 0, 0.0f, true };
    }
    if (t >= times.back()) {
        // Clamp to last endpoint — don't expose k+1.
        return { n - 1, 0.0f, true };
    }
    // Binary search for the upper bound.
    size_t lo = 0;
    size_t hi = n - 1;
    while (hi - lo > 1) {
        const size_t mid = lo + (hi - lo) / 2;
        if (times[mid] <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const float span = times[hi] - times[lo];
    const float frac = span > 0.0f ? (t - times[lo]) / span : 0.0f;
    return { lo, frac, false };
}

inline size_t valueFloatsPerKey(ayt::resource::AnimTrackType type)
{
    switch (type) {
        case ayt::resource::AnimTrackType::Vector3:    return 3;
        case ayt::resource::AnimTrackType::Quaternion: return 4;
        case ayt::resource::AnimTrackType::Float:      return 1;
    }
    return 1;
}

} // namespace

void sampleTrackVector3(const ayt::math::FVector3* values,
                        size_t keyCount,
                        const std::vector<float>& times,
                        float t,
                        ayt::math::FVector3& out)
{
    if (keyCount == 0 || values == nullptr) {
        out = ayt::math::FVector3(0.0f, 0.0f, 0.0f);
        return;
    }
    if (keyCount == 1) {
        out = values[0];
        return;
    }
    const SegmentLoc loc = locateSegment(times, t);
    const ayt::math::FVector3 a = values[loc.k];
    if (loc.clamped) {
        out = a;
        return;
    }
    const ayt::math::FVector3 b = values[loc.k + 1];
    out = a.lerp(b, loc.frac);
}

void sampleTrackQuaternion(const ayt::math::FQuaternion* values,
                           size_t keyCount,
                           const std::vector<float>& times,
                           float t,
                           ayt::math::FQuaternion& out)
{
    const size_t stride = 4;
    if (keyCount == 0 || values == nullptr) {
        out = ayt::math::FQuaternion::identity();
        return;
    }
    if (keyCount == 1) {
        out = values[0];
        out = out.normalize();
        return;
    }
    const SegmentLoc loc = locateSegment(times, t);
    ayt::math::FQuaternion a = values[loc.k];
    if (loc.clamped) {
        out = a.normalize();
        return;
    }
    ayt::math::FQuaternion b = values[loc.k + 1];

    // P0 (2026-07-26): shortest-arc selection. When the dot product is
    // negative the two quaternions represent the same rotation via the
    // antipodal point on the 4D sphere; flipping b to -b forces slerp
    // to interpolate along the shorter great-circle arc. Without this
    // the interpolated quaternion can flip 180° mid-blend, causing
    // visible visual twitching in rotation tracks.
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b = ayt::math::FQuaternion(-b.x, -b.y, -b.z, -b.w);
    }

    out = a.slerp(b, loc.frac).normalize();
}

void sampleTrackFloat(const float* values,
                      size_t keyCount,
                      const std::vector<float>& times,
                      float t,
                      float& out)
{
    if (keyCount == 0 || values == nullptr) {
        out = 0.0f;
        return;
    }
    if (keyCount == 1) {
        out = values[0];
        return;
    }
    const SegmentLoc loc = locateSegment(times, t);
    const float a = values[loc.k];
    if (loc.clamped) {
        out = a;
        return;
    }
    const float b = values[loc.k + 1];
    out = a + (b - a) * loc.frac;
}

} // namespace ayt::anim