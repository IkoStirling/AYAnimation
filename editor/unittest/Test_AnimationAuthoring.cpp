#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYAnimationEditor/AnimationClipboard.h>
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

TEST_CASE(large_cross_track_selection_keeps_order_and_complete_remapping) {
    AuthoredAnimation clip;
    clip.duration = 20; clip.ticksPerSecond = 30;
    std::vector<AnimationKeyReference> keys;
    for (std::size_t row = 0; row < 100; ++row) {
        AnimTrack track;
        track.nodeName = "bone-" + std::to_string(row);
        track.property = "weight"; track.valueType = AnimTrackType::Float;
        for (std::size_t key = 0; key < 500; ++key) {
            track.times.push_back(static_cast<float>(key));
            track.values.push_back(static_cast<float>(key));
            keys.push_back({row, key, false});
        }
        clip.tracks.push_back(std::move(track));
    }
    const auto source = buildAuthoredAnimation(clip);
    const auto edit = translateAnimationKeys(*source, keys, .01);
    CHECK(edit);
    CHECK(edit.keys == keys);
    CHECK(edit.animation->getTrackCount() == 100);
    CHECK(edit.animation->getTrackKeyframeCount(99) == 500);
    CHECK(edit.animation->getTrackValues(99)[499] == 499);
    CHECK(std::fabs(edit.animation->getTrackTimes(99)[0] - .3f) < 1e-6f);
}

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
    auto malformed = copyAnimationForAuthoring(*source);
    malformed.tracks[0].times.back() = std::numeric_limits<float>::quiet_NaN();
    CHECK(!translateAnimationKeys(*buildAuthoredAnimation(malformed), {{0, 1, false}}, .5));
    malformed = copyAnimationForAuthoring(*source);
    malformed.notifies.back().time = std::numeric_limits<float>::quiet_NaN();
    CHECK(!translateAnimationKeys(*buildAuthoredAnimation(malformed), {{0, 1, true}}, .5));
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

TEST_SUITE(AnimationClipboardTests)
TEST_CASE(copy_paste_cross_rate_preserves_seconds_values_and_tangents) {
    const auto source = authoringFixture();
    const auto copied = copyAnimationKeys(*source, {{0, 1, false}, {1, 1, false}, {0, 1, true}});
    CHECK(copied);
    auto target = copyAnimationForAuthoring(*source);
    target.ticksPerSecond = 8;
    for (auto& track : target.tracks) for (auto& time : track.times) time *= 4;
    const auto edit = pasteAnimationKeys(*buildAuthoredAnimation(target), *copied, 2);
    CHECK(edit);
    CHECK(edit.keys.size() == 3);
    CHECK(edit.animation->getTrackTimes(0)[2] == 16);
    CHECK(edit.animation->getTrackValues(0)[2] == 10);
    CHECK(edit.animation->getTrackInTangents(0)[2] == 2);
    CHECK(edit.animation->getTrackOutTangents(0)[2] == 5);
    CHECK(edit.animation->getNotifyTime(2) == 2);
    CHECK(source->getTrackKeyframeCount(0) == 3);
}
TEST_CASE(relative_offsets_and_pasted_index_remapping_survive_reverse_order) {
    const auto source = authoringFixture();
    const auto copied = copyAnimationKeys(*source, {{0, 2, false}, {0, 1, false}});
    CHECK(copied);
    CHECK(copied->spanSeconds == 2);
    const auto edit = pasteAnimationKeys(*source, *copied, .5);
    CHECK(edit);
    CHECK(edit.keys[0].key == 3); // 2.5 seconds, first caller key
    CHECK(edit.keys[1].key == 1); // .5 seconds inserted afterwards
    CHECK(edit.animation->getTrackValues(0)[3] == 30);
}
TEST_CASE(paste_rejects_conflicts_missing_and_incompatible_tracks_atomically) {
    const auto source = authoringFixture();
    auto copied = *copyAnimationKeys(*source, {{0, 1, false}, {1, 1, false}});
    CHECK(!pasteAnimationKeys(*source, copied, 1));
    CHECK(!pasteAnimationKeys(*source, copied, -1));
    CHECK(!pasteAnimationKeys(*source, copied, 5));
    copied.tracks[1].nodeName = "absent";
    CHECK(!pasteAnimationKeys(*source, copied, 2));
    copied.tracks[1].nodeName = "hand";
    copied.tracks[1].interpolation = AnimInterpolation::Step;
    CHECK(!pasteAnimationKeys(*source, copied, 2));
    CHECK(source->getTrackKeyframeCount(0) == 3);
}
TEST_CASE(copy_and_paste_reject_malformed_payload_and_duplicate_ids) {
    const auto source = authoringFixture();
    CHECK(!copyAnimationKeys(*source, {{0, 1, false}, {0, 1, false}}));
    auto copied = *copyAnimationKeys(*source, {{0, 1, false}});
    copied.tracks[0].keys[0].inTangents = {1, 2};
    CHECK(!pasteAnimationKeys(*source, copied, 2));
    copied.tracks[0].keys[0].inTangents = {std::numeric_limits<float>::infinity()};
    CHECK(!pasteAnimationKeys(*source, copied, 2));
    copied.tracks[0].keys[0].inTangents = {};
    copied.tracks[0].keys[0].offsetSeconds = std::numeric_limits<double>::quiet_NaN();
    CHECK(!pasteAnimationKeys(*source, copied, 2));
}
TEST_CASE(notify_paste_conflict_retains_source_and_selection) {
    const auto source = authoringFixture();
    const auto copied = copyAnimationKeys(*source, {{0, 1, true}});
    CHECK(copied);
    CHECK(!pasteAnimationKeys(*source, *copied, 1));
    const auto edit = pasteAnimationKeys(*source, *copied, 2);
    CHECK(edit);
    CHECK(edit.keys[0].notify);
    CHECK(edit.keys[0].key == 2);
    CHECK(edit.animation->getNotifyPayload(2) == 2);
}
TEST_SUITE_END
