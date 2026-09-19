#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYTest.h>

#include <algorithm>
#include <filesystem>

using namespace ayt::anim;
using namespace ayt::anim::editor;
using namespace ayt::resource;

namespace {

std::filesystem::path fixtureRoot()
{
    const auto path = std::filesystem::temp_directory_path()
        / "ay_animation_skeleton_editor_core";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

std::filesystem::path writeSkeleton()
{
    Skeleton skeleton;
    const struct BoneDef { const char* name; int parent; float x; float y; } defs[] = {
        {"sceneRoot", -1, 0, 0}, {"motionRoot", 0, 0, 0},
        {"hips", 1, 0, 1}, {"spine", 2, 0, 1}, {"head", 3, 0, 1},
        {"leftUpperArm", 3, -1, 0}, {"leftLowerArm", 5, -1, 0},
        {"leftHand", 6, -1, 0}, {"rightUpperArm", 3, 1, 0},
        {"rightLowerArm", 8, 1, 0}, {"rightHand", 9, 1, 0},
        {"leftUpperLeg", 2, -0.4f, -1}, {"leftLowerLeg", 11, 0, -1},
        {"leftFoot", 12, 0, -1}, {"rightUpperLeg", 2, 0.4f, -1},
        {"rightLowerLeg", 14, 0, -1}, {"rightFoot", 15, 0, -1},
    };
    for (const BoneDef& def : defs) {
        Bone bone;
        bone.name = def.name;
        bone.parentIndex = def.parent;
        bone.localPosition = {def.x, def.y, 0.0f};
        bone.localRotation = ayt::math::FQuaternion::identity();
        bone.localScale = {1, 1, 1};
        bone.inverseBindMatrix = ayt::math::Float4x4::identity();
        skeleton.addBone(bone);
    }
    std::vector<ayt::math::UInt8> bytes;
    CHECK(skeleton.saveToBinary(bytes));
    const auto path = fixtureRoot() / "synthetic.ayskel";
    auto mappingPath = path;
    mappingPath.replace_extension(".aysmap");
    std::error_code ignored;
    std::filesystem::remove(mappingPath, ignored);
    CHECK(ayt::io::File::writeAllBytes(path.string(), bytes));
    return path;
}

std::filesystem::path writeAnimationForNode(
    const char* nodeName = "hips", const char* fileName = "synthetic.ayanm")
{
    Animation animation;
    animation.setName("synthetic");
    animation.setDuration(1.0f);
    animation.setTicksPerSecond(1.0f);
    AnimTrack track;
    track.nodeName = nodeName;
    track.property = "position";
    track.valueType = AnimTrackType::Vector3;
    track.times = {0.0f, 1.0f};
    track.values = {0, 1, 0, 0, 2, 0};
    animation.addTrack(track);
    std::vector<ayt::math::UInt8> bytes;
    CHECK(animation.saveToBinary(bytes));
    const auto path = fixtureRoot() / fileName;
    CHECK(ayt::io::File::writeAllBytes(path.string(), bytes));
    return path;
}

} // namespace

TEST_SUITE(SkeletonEditorCoreTests)

TEST_CASE(skeleton_editor_core_opens_synthetic_hierarchy)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(error.empty());
    CHECK(core.bones().size() == 17u);
    CHECK(core.bones()[4].name == "head");
    CHECK(core.bones()[4].depth == 4);
    CHECK(core.poseWorldMatrices().size() == core.bones().size());
    CHECK(core.status().adaptation == SkeletonAdaptationState::Unmapped);
    CHECK(core.status().bake == SkeletonBakeState::NotBaked);
}

TEST_CASE(skeleton_editor_core_mapping_is_undoable_and_persistent)
{
    const auto skeletonPath = writeSkeleton();
    const auto mappingPath = fixtureRoot() / "synthetic.aysmap";
    std::error_code ignored;
    std::filesystem::remove(mappingPath, ignored);
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(skeletonPath.string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.validation().isValid());
    CHECK(core.isDirty());
    CHECK(core.canUndo());
    CHECK(core.undo());
    CHECK(core.mapping().empty());
    CHECK(core.redo());
    CHECK(core.validation().isValid());
    CHECK(core.saveMappingAs(mappingPath.string(), &error));
    CHECK_FALSE(core.isDirty());

    SkeletonEditorCore reopened;
    CHECK(reopened.open(mappingPath.string(), &error));
    CHECK(reopened.validation().isValid());
    CHECK(reopened.mapping().getBoundCount() == 17u);
    CHECK(reopened.status().adaptation == SkeletonAdaptationState::Validated);
}

TEST_CASE(skeleton_editor_core_animation_updates_wire_pose)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.attachAnimation(writeAnimationForNode().string(), &error));
    CHECK(core.duration() == 1.0f);
    CHECK(core.setTime(0.5f));
    const auto hips = core.poseWorldMatrices()[2].transformPoint({0, 0, 0});
    CHECK(hips.y > 1.0f);
    core.play();
    CHECK(core.isPlaying());
    core.tick(0.1f);
    core.pause();
    CHECK_FALSE(core.isPlaying());
}

TEST_CASE(skeleton_editor_core_reload_uses_source_before_sidecar_exists)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.mapping().empty());
    CHECK(core.reload(&error));
    CHECK(error.empty());
    CHECK(core.bones().size() == 17u);
}

TEST_CASE(skeleton_preflight_reports_mapping_completeness)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    const SkeletonPreflightReport report = core.preflight();
    CHECK_FALSE(report.canBake());
    CHECK(report.errorCount() >= 2u);
    CHECK(std::any_of(report.issues.begin(), report.issues.end(),
        [](const SkeletonPreflightIssue& issue) {
            return issue.code == SkeletonPreflightCode::MappingMissing;
        }));
    CHECK(std::any_of(report.issues.begin(), report.issues.end(),
        [](const SkeletonPreflightIssue& issue) {
            return issue.code == SkeletonPreflightCode::RequiredRoleMissing;
        }));
}

TEST_CASE(skeleton_preflight_accepts_valid_mapping_and_animation)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.attachAnimation(writeAnimationForNode().string(), &error));
    const SkeletonPreflightReport report = core.preflight();
    CHECK(report.canBake());
    CHECK(report.errorCount() == 0u);
}

TEST_CASE(skeleton_preflight_finds_orphan_animation_tracks)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.attachAnimation(writeAnimationForNode(
        "missingBone", "orphan.ayanm").string(), &error));
    const SkeletonPreflightReport report = core.preflight();
    CHECK_FALSE(report.canBake());
    CHECK(std::any_of(report.issues.begin(), report.issues.end(),
        [](const SkeletonPreflightIssue& issue) {
            return issue.code
                == SkeletonPreflightCode::AnimationTrackBoneMissing
                && issue.trackIndex == 0;
        }));
}

TEST_CASE(skeleton_preflight_detects_source_change_after_mapping_save)
{
    const auto skeletonPath = writeSkeleton();
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(skeletonPath.string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.saveMapping(&error));
    std::vector<ayt::math::UInt8> bytes =
        ayt::io::File::readAllBytes(skeletonPath.string());
    bytes.push_back(0u);
    CHECK(ayt::io::File::writeAllBytes(skeletonPath.string(), bytes));
    const SkeletonPreflightReport report = core.preflight();
    CHECK_FALSE(report.canBake());
    CHECK(std::any_of(report.issues.begin(), report.issues.end(),
        [](const SkeletonPreflightIssue& issue) {
            return issue.code == SkeletonPreflightCode::SourceSkeletonChanged;
        }));
}

TEST_SUITE_END

int main(int, char**)
{
    return ayt::test::runAllTests("AYAnimationEditorCore_UnitTests");
}
