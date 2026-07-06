#include <ayanimation/KeySampler.h>
#include <cstddef>

namespace ayt::anim
{

namespace
{

// Locate the segment [k, k+1] such that times[k] <= t < times[k+1].
// Returns the lower key index k and the local fraction in [0, 1] for the segment.
// For t outside the time range, the segment is clamped to the nearest endpoint —
// the caller must handle "frac == 0 at the last key" specially (do NOT read
// values[(k+1)*stride]) and the same applies symmetrically at the lower end.
// For kcount <= 1 the caller should treat the output as "single endpoint".
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

inline size_t valueFloatsPerKey(TrackType type)
{
    switch (type) {
        case TrackType::Vector3:    return 3;
        case TrackType::Quaternion: return 4;
        case TrackType::Float:      return 1;
    }
    return 1;
}

} // namespace

void sampleTrackVector3(const KeyframeTrack& tr, float t, ayt::math::FVector3& out)
{
    const size_t kcount = tr.times.size();
    const size_t stride = valueFloatsPerKey(tr.type);
    if (kcount == 0 || tr.values.size() < stride) {
        out = ayt::math::FVector3(0.0f, 0.0f, 0.0f);
        return;
    }
    if (kcount == 1) {
        out = ayt::math::FVector3(tr.values[0], tr.values[1], tr.values[2]);
        return;
    }
    const SegmentLoc loc = locateSegment(tr.times, t);
    const size_t off0 = loc.k * stride;
    const ayt::math::FVector3 a(tr.values[off0 + 0], tr.values[off0 + 1], tr.values[off0 + 2]);
    if (loc.clamped) {
        // Outside the time range — return endpoint, no lerp.
        out = a;
        return;
    }
    const size_t off1 = (loc.k + 1) * stride;
    const ayt::math::FVector3 b(tr.values[off1 + 0], tr.values[off1 + 1], tr.values[off1 + 2]);
    out = a.lerp(b, loc.frac);
}

void sampleTrackQuaternion(const KeyframeTrack& tr, float t, ayt::math::FQuaternion& out)
{
    const size_t kcount = tr.times.size();
    const size_t stride = valueFloatsPerKey(tr.type);
    if (kcount == 0 || tr.values.size() < stride) {
        out = ayt::math::FQuaternion::identity();
        return;
    }
    if (kcount == 1) {
        out = ayt::math::FQuaternion(tr.values[0], tr.values[1], tr.values[2], tr.values[3]);
        out = out.normalize();
        return;
    }
    const SegmentLoc loc = locateSegment(tr.times, t);
    const size_t off0 = loc.k * stride;
    const ayt::math::FQuaternion a(tr.values[off0 + 0], tr.values[off0 + 1],
                                   tr.values[off0 + 2], tr.values[off0 + 3]);
    if (loc.clamped) {
        out = a.normalize();
        return;
    }
    const size_t off1 = (loc.k + 1) * stride;
    const ayt::math::FQuaternion b(tr.values[off1 + 0], tr.values[off1 + 1],
                                   tr.values[off1 + 2], tr.values[off1 + 3]);
    out = a.slerp(b, loc.frac).normalize();
}

void sampleTrackFloat(const KeyframeTrack& tr, float t, float& out)
{
    const size_t kcount = tr.times.size();
    if (kcount == 0 || tr.values.empty()) {
        out = 0.0f;
        return;
    }
    if (kcount == 1) {
        out = tr.values[0];
        return;
    }
    const SegmentLoc loc = locateSegment(tr.times, t);
    const float a = tr.values[loc.k];
    if (loc.clamped) {
        out = a;
        return;
    }
    const float b = tr.values[loc.k + 1];
    out = a + (b - a) * loc.frac;
}

} // namespace ayt::anim