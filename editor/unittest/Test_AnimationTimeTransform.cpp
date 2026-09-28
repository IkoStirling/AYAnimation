#include <AYAnimationEditor/AnimationTimeTransform.h>
#include <AYAnimation/KeySampler.h>
#include <AYTest.h>
#include <cmath>
#include <limits>

using namespace ayt::anim::editor;
using namespace ayt::resource;
namespace {
std::shared_ptr<Animation> timeFixture() {
    AuthoredAnimation clip;
    clip.duration = 5;
    clip.ticksPerSecond = 1;
    AnimTrack track;
    track.nodeName = "hips"; track.property = "weight";
    track.valueType = AnimTrackType::Float;
    track.interpolation = AnimInterpolation::CubicHermite;
    track.times = {1, 2}; track.values = {1, 3};
    track.inTangents = {2, 5}; track.outTangents = {4, 6};
    clip.tracks.push_back(track);
    clip.notifies = {{"step", 1, 2}};
    return buildAuthoredAnimation(clip);
}
float sample(const IAnimation& clip, float seconds) {
    std::vector<float> times, values, in, out;
    for (uint32_t i = 0; i < clip.getTrackKeyframeCount(0); ++i) {
        times.push_back(clip.getTrackTimes(0)[i] / clip.getTicksPerSecond());
        values.push_back(clip.getTrackValues(0)[i]);
        in.push_back(clip.getTrackInTangents(0)[i]);
        out.push_back(clip.getTrackOutTangents(0)[i]);
    }
    float output = 0;
    ayt::anim::sampleTrackFloat(values.data(), values.size(), times, seconds,
        output, clip.getTrackInterpolation(0), in.data(), out.data());
    return output;
}
}
TEST_SUITE(AnimationTimeTransformTests)
TEST_CASE(scale_preserves_hermite_shape_and_inverse_slopes) {
    const auto source = timeFixture();
    const auto edit = retimeAnimationKeys(*source,
        {{0, 0, false}, {0, 1, false}, {0, 0, true}}, 0, 2);
    CHECK(edit);
    CHECK(edit.animation->getTrackTimes(0)[0] == 2);
    CHECK(edit.animation->getTrackOutTangents(0)[0] == 2);
    CHECK(edit.animation->getNotifyTime(0) == 2);
    for (float t : {1.f, 1.25f, 1.5f, 1.75f, 2.f})
        CHECK(std::fabs(sample(*source, t) - sample(*edit.animation, t * 2)) < 1e-5f);
}
TEST_CASE(reverse_swaps_signed_tangents_and_remaps_selection) {
    const auto source = timeFixture();
    const auto edit = retimeAnimationKeys(*source,
        {{0, 0, false}, {0, 1, false}, {0, 0, true}}, 1.5, -1);
    CHECK(edit);
    CHECK(edit.keys[0].key == 1);
    CHECK(edit.keys[1].key == 0);
    CHECK(edit.animation->getTrackInTangents(0)[0] == -6);
    CHECK(edit.animation->getTrackOutTangents(0)[0] == -5);
    CHECK(edit.animation->getTrackInTangents(0)[1] == -4);
    CHECK(edit.animation->getTrackOutTangents(0)[1] == -2);
    CHECK(edit.animation->getNotifyTime(0) == 2);
    for (float t : {1.f, 1.25f, 1.5f, 1.75f, 2.f})
        CHECK(std::fabs(sample(*source, t) - sample(*edit.animation, 3 - t)) < 1e-5f);
}
TEST_CASE(bounds_collisions_step_reverse_and_nonfinite_reject_all) {
    const auto source = timeFixture();
    CHECK(!retimeAnimationKeys(*source, {{0, 0, false}}, 2, 0));
    CHECK(!retimeAnimationKeys(*source, {{0, 0, false}}, 2, std::numeric_limits<double>::infinity()));
    CHECK(!retimeAnimationKeys(*source, {{0, 0, false}}, std::numeric_limits<double>::quiet_NaN(), 2));
    CHECK(!retimeAnimationKeys(*source, {{0, 1, false}}, 0, 3));
    CHECK(!retimeAnimationKeys(*source, {{0, 0, false}}, 0, 2));
    auto clip = copyAnimationForAuthoring(*source);
    clip.tracks[0].interpolation = AnimInterpolation::Step;
    CHECK(!retimeAnimationKeys(*buildAuthoredAnimation(clip), {{0, 0, false}}, 1.5, -1));
    CHECK(source->getTrackTimes(0)[0] == 1);
}
TEST_SUITE_END

TEST_SUITE(AnimationTimeQuaternionTests)
TEST_CASE(reverse_cubic_quaternion_matches_formal_shortest_arc_sampling) {
    auto clip = copyAnimationForAuthoring(*timeFixture());
    auto& track = clip.tracks[0];
    track.valueType = AnimTrackType::Quaternion;
    track.property = "rotation";
    track.values = {0, 0, 0, 1, 0, 0, -.70710678f, -.70710678f};
    track.inTangents = {0, 0, .2f, 0, 0, 0, -.1f, .1f};
    track.outTangents = {0, 0, .3f, -.1f, 0, 0, -.2f, 0};
    const auto source = buildAuthoredAnimation(clip);
    const auto edit = retimeAnimationKeys(*source, {{0, 0, false}, {0, 1, false}}, 1.5, -1);
    CHECK(edit);
    const auto sampleQuat = [](const IAnimation& animation, float time) {
        std::vector<ayt::math::FQuaternion> values, in, out;
        std::vector<float> times;
        for (uint32_t i = 0; i < animation.getTrackKeyframeCount(0); ++i) {
            const auto v = animation.getTrackValues(0) + i * 4;
            const auto a = animation.getTrackInTangents(0) + i * 4;
            const auto b = animation.getTrackOutTangents(0) + i * 4;
            values.emplace_back(v[0], v[1], v[2], v[3]);
            in.emplace_back(a[0], a[1], a[2], a[3]);
            out.emplace_back(b[0], b[1], b[2], b[3]);
            times.push_back(animation.getTrackTimes(0)[i]);
        }
        ayt::math::FQuaternion output;
        ayt::anim::sampleTrackQuaternion(values.data(), values.size(), times, time,
            output, AnimInterpolation::CubicHermite, in.data(), out.data());
        return output;
    };
    for (float time : {1.f, 1.25f, 1.5f, 1.75f, 2.f}) {
        const auto a = sampleQuat(*source, time), b = sampleQuat(*edit.animation, 3 - time);
        CHECK(std::fabs(std::fabs(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w) - 1) < 1e-5f);
        CHECK(std::fabs(b.x*b.x + b.y*b.y + b.z*b.z + b.w*b.w - 1) < 1e-5f);
    }
}
TEST_SUITE_END
