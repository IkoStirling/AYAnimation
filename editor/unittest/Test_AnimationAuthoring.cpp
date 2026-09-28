#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYTest.h>
#include <limits>

using namespace ayt::anim::editor;
using namespace ayt::resource;
namespace {
std::shared_ptr<Animation> authoringFixture() {
    AuthoredAnimation clip;
    clip.duration = 4;
    clip.ticksPerSecond = 2;
    for (int i = 0; i < 2; ++i) {
        AnimTrack track;
        track.nodeName = i ? "hand" : "hips";
        track.property = "weight";
        track.valueType = AnimTrackType::Float;
        track.interpolation = AnimInterpolation::CubicHermite;
        track.times = {0, 2, 6};
        track.values = {0, 10, 30};
        track.inTangents = {1, 2, 3};
        track.outTangents = {4, 5, 6};
        clip.tracks.push_back(track);
    }
    clip.notifies = {{"start", 0, 1}, {"step", 1, 2}, {"end", 3, 3}};
    return buildAuthoredAnimation(clip);
}
}
TEST_SUITE(AnimationAuthoringTests)

TEST_CASE(cross_track_notify_translation_preserves_spacing_and_source) {
    const auto source = authoringFixture();
    const auto edit = translateAnimationKeys(*source,
        {{0, 1, false}, {1, 1, false}, {0, 1, true}}, .5);
    CHECK(edit);
    CHECK(edit.animation->getTrackTimes(0)[1] == 3);
    CHECK(edit.animation->getTrackTimes(1)[1] == 3);
    CHECK(edit.animation->getNotifyTime(1) == 1.5f);
    CHECK(source->getTrackTimes(0)[1] == 2);
    CHECK(edit.animation->getTrackInTangents(1)[1] == 2);
}

TEST_CASE(group_clamp_collision_and_invalid_selection_are_atomic) {
    const auto source = authoringFixture();
    const auto clamped = translateAnimationKeys(*source,
        {{0, 1, false}, {1, 2, false}}, 99);
    CHECK(clamped);
    CHECK(clamped.animation->getTrackTimes(0)[1] == 4);
    CHECK(clamped.animation->getTrackTimes(1)[2] == 8);
    CHECK(!translateAnimationKeys(*source, {{0, 1, false}}, 2));
    CHECK(!translateAnimationKeys(*source, {{0, 1, false}, {0, 1, false}}, .5));
    CHECK(!translateAnimationKeys(*source, {{0, 1, true}}, .5, 0, 1));
    CHECK(!translateAnimationKeys(*source, {{0, 1, false}}, std::numeric_limits<double>::infinity()));
    CHECK(source->getTrackTimes(0)[1] == 2);
}

TEST_CASE(reordering_remaps_keys_with_values_and_tangents) {
    const auto source = authoringFixture();
    const auto edit = translateAnimationKeys(*source, {{0, 1, false}}, 2.5);
    CHECK(edit);
    CHECK(edit.keys[0].key == 2);
    CHECK(edit.animation->getTrackValues(0)[2] == 10);
    CHECK(edit.animation->getTrackInTangents(0)[2] == 2);
    CHECK(edit.animation->getTrackOutTangents(0)[2] == 5);
}

TEST_CASE(delete_cross_track_and_notify_uses_original_indices) {
    const auto source = authoringFixture();
    const auto edit = removeAnimationKeys(*source,
        {{0, 0, false}, {1, 1, false}, {999, 0, true}, {0, 2, true}});
    CHECK(edit);
    CHECK(edit.animation->getTrackKeyframeCount(0) == 2);
    CHECK(edit.animation->getTrackValues(0)[0] == 10);
    CHECK(edit.animation->getTrackValues(1)[1] == 30);
    CHECK(edit.animation->getNotifyCount() == 1);
    CHECK(std::string(edit.animation->getNotifyName(0)) == "step");
    CHECK(source->getNotifyCount() == 3);
}
TEST_SUITE_END
