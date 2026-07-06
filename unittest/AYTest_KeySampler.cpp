// AYTest_KeySampler.cpp — exercises the AN-01 KeySampler layer.
//
// Each case builds a KeyframeTrack by hand (no AYResource dependency), then
// samples at a known time and checks the result with CHECK_FLOAT_EQ.

#include "AYAnimation.h"
#include <AYTest.h>
#include <AYMathTypes.h>

using namespace ayt::anim;

namespace
{

KeyframeTrack makeVec3Track(const char* nodeName, const char* property,
                             const std::vector<float>& times,
                             const std::vector<float>& values)
{
    KeyframeTrack tr;
    tr.nodeName = nodeName;
    tr.property = property;
    tr.type     = TrackType::Vector3;
    tr.times    = times;
    tr.values   = values;
    return tr;
}

KeyframeTrack makeQuatTrack(const char* nodeName, const char* property,
                            const std::vector<float>& times,
                            const std::vector<float>& values)
{
    KeyframeTrack tr;
    tr.nodeName = nodeName;
    tr.property = property;
    tr.type     = TrackType::Quaternion;
    tr.times    = times;
    tr.values   = values;
    return tr;
}

KeyframeTrack makeFloatTrack(const char* nodeName, const char* property,
                             const std::vector<float>& times,
                             const std::vector<float>& values)
{
    KeyframeTrack tr;
    tr.nodeName = nodeName;
    tr.property = property;
    tr.type     = TrackType::Float;
    tr.times    = times;
    tr.values   = values;
    return tr;
}

} // namespace

TEST_SUITE(KeySamplerTests)

    TEST_CASE(vector3_lerp_midpoint) {
        // times = [0, 1]; values = [0,0,0,  10,20,30]
        auto tr = makeVec3Track("Root", "position",
                                { 0.0f, 1.0f },
                                { 0.0f, 0.0f, 0.0f,   10.0f, 20.0f, 30.0f });
        ayt::math::FVector3 out;
        sampleTrackVector3(tr, 0.5f, out);
        CHECK_FLOAT_EQ(out.x,  5.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.y, 10.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.z, 15.0f, 1e-5f);
    }

    TEST_CASE(vector3_clamp_below_first) {
        auto tr = makeVec3Track("Root", "position",
                                { 1.0f, 2.0f },
                                { 3.0f, 4.0f, 5.0f,   6.0f, 7.0f, 8.0f });
        ayt::math::FVector3 out;
        sampleTrackVector3(tr, -10.0f, out);
        CHECK_FLOAT_EQ(out.x, 3.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.y, 4.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.z, 5.0f, 1e-5f);
    }

    TEST_CASE(vector3_clamp_above_last) {
        auto tr = makeVec3Track("Root", "position",
                                { 1.0f, 2.0f },
                                { 3.0f, 4.0f, 5.0f,   6.0f, 7.0f, 8.0f });
        ayt::math::FVector3 out;
        sampleTrackVector3(tr, 100.0f, out);
        CHECK_FLOAT_EQ(out.x, 6.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.y, 7.0f, 1e-5f);
        CHECK_FLOAT_EQ(out.z, 8.0f, 1e-5f);
    }

    TEST_CASE(quaternion_slerp_known_endpoints) {
        // Endpoint A = identity (0,0,0,1). Endpoint B = 90° around Y.
        // For a unit quaternion q = (sin(φ/2), cos(φ/2)) where φ is the full rotation
        // angle, dot(id, q) = cos(φ/2). So slerp's theta = acos(dot) equals φ/2 — the
        // quaternion half-angle. At t=0.5 the result is the half-arc midpoint, which
        // corresponds to a full rotation of φ/2 = 45° around Y. The returned quaternion
        // therefore has (y, w) = (sin(π/8), cos(π/8)) — π/8 here is the quaternion
        // half-angle for a 45° rotation, NOT the rotation itself.
        ayt::math::FQuaternion qy90 = ayt::math::FQuaternion::fromAxisAngle(
            ayt::math::FVector3(0.0f, 1.0f, 0.0f), static_cast<float>(MATH_PI * 0.5));
        auto tr = makeQuatTrack("Head", "rotation",
                                { 0.0f, 1.0f },
                                { 0.0f, 0.0f, 0.0f, 1.0f,    // identity
                                  qy90.x, qy90.y, qy90.z, qy90.w });
        ayt::math::FQuaternion out;
        sampleTrackQuaternion(tr, 0.5f, out);

        const float expectedY = std::sin(static_cast<float>(MATH_PI * 0.125));
        const float expectedW = std::cos(static_cast<float>(MATH_PI * 0.125));
        CHECK_FLOAT_EQ(out.x, 0.0f,        1e-5f);
        CHECK_FLOAT_EQ(out.y, expectedY,   1e-5f);
        CHECK_FLOAT_EQ(out.z, 0.0f,        1e-5f);
        CHECK_FLOAT_EQ(out.w, expectedW,   1e-5f);

        // Normalized check — length must be ~1.
        const float len2 = out.x*out.x + out.y*out.y + out.z*out.z + out.w*out.w;
        CHECK_FLOAT_EQ(len2, 1.0f, 1e-4f);
    }

    TEST_CASE(quaternion_slerp_endpoint_normalized) {
        // Force non-unit endpoint; the sampler must normalize the output.
        auto tr = makeQuatTrack("Head", "rotation",
                                { 0.0f, 1.0f },
                                { 0.0f, 0.0f, 0.0f, 1.0f,    // unit
                                  0.0f, 0.0f, 0.0f, 2.0f }); // non-unit
        ayt::math::FQuaternion out;
        sampleTrackQuaternion(tr, 0.5f, out);
        const float len2 = out.x*out.x + out.y*out.y + out.z*out.z + out.w*out.w;
        CHECK_FLOAT_EQ(len2, 1.0f, 1e-4f);
    }

    TEST_CASE(float_track_lerp) {
        auto tr = makeFloatTrack("Param", "weight",
                                 { 0.0f, 10.0f },
                                 { 0.0f, 100.0f });
        float out = -1.0f;
        sampleTrackFloat(tr, 5.0f, out);
        CHECK_FLOAT_EQ(out, 50.0f, 1e-4f);
    }

TEST_SUITE_END