#include <AYAnimationEditor/SkeletonEditorCore.h>
#include <AYAnimationEditor/SkeletonBakeJob.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYTest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <thread>

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

std::filesystem::path writeSkeleton(bool addHelper = false,
                                    bool renameHead = false,
                                    const char* fileName = "synthetic.ayskel")
{
    Skeleton skeleton;
    const struct BoneDef { const char* name; int parent; float x; float y; } defs[] = {
        {"sceneRoot", -1, 0, 0}, {"motionRoot", 0, 0, 0},
        {"hips", 1, 0, 1}, {"spine", 2, 0, 1},
        {renameHead ? "characterHead" : "head", 3, 0, 1},
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
    if (addHelper) {
        Bone helper;
        helper.name = "headAccessoryHelper";
        helper.parentIndex = 4;
        helper.localPosition = {0, 0.25f, 0};
        helper.localRotation = ayt::math::FQuaternion::identity();
        helper.localScale = {1, 1, 1};
        helper.inverseBindMatrix = ayt::math::Float4x4::identity();
        skeleton.addBone(helper);
    }
    std::vector<ayt::math::UInt8> bytes;
    CHECK(skeleton.saveToBinary(bytes));
    const auto path = fixtureRoot() / fileName;
    std::error_code ignored;
    auto rigPath = path;
    rigPath.replace_extension(".ayrig");
    std::filesystem::remove(rigPath, ignored);
    auto legacyPath = path;
    legacyPath.replace_extension(".aysmap");
    std::filesystem::remove(legacyPath, ignored);
    std::filesystem::remove_all(path.parent_path() / "Baked", ignored);
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
    const auto mappingPath = fixtureRoot() / "synthetic.ayrig";
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
    const auto encoded = nlohmann::json::parse(
        ayt::io::File::readAllText(mappingPath.string()));
    CHECK(encoded["type"] == "RigProfile");
    CHECK(encoded["version"] == kRigProfileSchemaVersion);
    CHECK(encoded["kind"] == "mapping");
    CHECK(encoded["strategy"] == "humanoid");
    CHECK(encoded.contains("id"));
    CHECK(encoded["roles"]["hips"]["bonePath"]
        == "sceneRoot/motionRoot/hips");
    CHECK_FALSE(encoded.contains("bakeState"));
    CHECK_FALSE(encoded.contains("bakedFingerprint"));

    auto reordered = encoded;
    reordered["roles"]["hips"]["sourceIndex"] = 999;
    CHECK(ayt::io::File::writeAllText(
        mappingPath.string(), reordered.dump(2) + "\n"));

    SkeletonEditorCore reopened;
    CHECK(reopened.open(mappingPath.string(), &error));
    CHECK(reopened.validation().isValid());
    CHECK(reopened.mapping().getBoundCount() == 17u);
    CHECK(reopened.mapping().getSourceBoneIndex(HumanoidBone::Hips) == 2);
    CHECK(reopened.status().adaptation == SkeletonAdaptationState::Validated);
}

TEST_CASE(skeleton_editor_core_migrates_legacy_mapping_non_destructively)
{
    const auto skeletonPath = writeSkeleton();
    const auto legacyPath = fixtureRoot() / "synthetic.aysmap";
    const nlohmann::json legacy = {
        {"type", "SkeletonMapping"},
        {"version", kLegacySkeletonMappingSchemaVersion},
        {"skeleton", skeletonPath.filename().generic_string()},
        {"sourceFingerprint", "legacy-fingerprint"},
        {"native", false},
        {"bakeState", "ready"},
        {"bakedFingerprint", "legacy-bake"},
        {"roles", {{"hips", 2}}},
    };
    CHECK(ayt::io::File::writeAllText(
        legacyPath.string(), legacy.dump(2) + "\n"));

    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(legacyPath.string(), &error));
    CHECK(core.openedLegacyMapping());
    CHECK(core.isDirty());
    CHECK(core.legacyMappingPath() == legacyPath.generic_string());
    CHECK(std::filesystem::path(core.mappingPath()).extension() == ".ayrig");
    CHECK(core.mapping().getSourceBoneIndex(HumanoidBone::Hips) == 2);
    CHECK(core.status().bake == SkeletonBakeState::NotBaked);
    CHECK(core.saveMapping(&error));
    CHECK_FALSE(core.openedLegacyMapping());
    CHECK_FALSE(core.isDirty());
    CHECK(std::filesystem::exists(legacyPath));
    CHECK(std::filesystem::exists(core.mappingPath()));
    const auto migrated = nlohmann::json::parse(
        ayt::io::File::readAllText(core.mappingPath()));
    CHECK(migrated["type"] == "RigProfile");
    CHECK(migrated["kind"] == "mapping");
    CHECK_FALSE(migrated.contains("bakeState"));
}

TEST_CASE(skeleton_editor_core_animation_updates_wire_pose)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.attachAnimation(writeAnimationForNode().string(), &error));
    CHECK(core.duration() == 1.0f);
    const std::uint64_t contentRevision = core.revision();
    const std::uint64_t initialPoseRevision = core.poseRevision();
    CHECK(core.setTime(0.5f));
    CHECK(core.revision() == contentRevision);
    CHECK(core.poseRevision() > initialPoseRevision);
    const auto hips = core.poseWorldMatrices()[2].transformPoint({0, 0, 0});
    CHECK(hips.y > 1.0f);
    core.play();
    CHECK(core.isPlaying());
    const std::uint64_t poseBeforeTick = core.poseRevision();
    core.tick(0.1f);
    CHECK(core.revision() == contentRevision);
    CHECK(core.poseRevision() > poseBeforeTick);
    core.pause();
    CHECK_FALSE(core.isPlaying());
    core.stop();
    CHECK(core.revision() == contentRevision);
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

TEST_CASE(skeleton_editor_core_applies_ayrig_template_without_overwriting_manual_roles)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.bind(HumanoidBone::Hips, 2));

    const auto templatePath = fixtureRoot() / "canonical_template.ayrig";
    const nlohmann::json rigTemplate = {
        {"type", "RigProfile"},
        {"version", kRigProfileSchemaVersion},
        {"id", "template-canonical-test"},
        {"kind", "template"},
        {"name", "Canonical Test"},
        {"roles", {
            {"hips", {{"candidates", {"pelvis", "hips"}}}},
            {"spine", "spine"},
            {"head", {{"sourceName", "head"}}},
            {"leftEye", {{"candidates", {"eye_l", "leftEye"}}}},
        }},
    };
    CHECK(ayt::io::File::writeAllText(
        templatePath.string(), rigTemplate.dump(2) + "\n"));

    SkeletonTemplateApplyReport report;
    CHECK(core.previewRigTemplate(templatePath.string(), &report, &error));
    CHECK(report.appliedCount == 2u);
    CHECK(core.mapping().getBoundCount() == 1u);
    CHECK(core.applyRigTemplate(templatePath.string(), &report, &error));
    CHECK(error.empty());
    CHECK(report.templateName == "Canonical Test");
    CHECK(report.preservedCount == 1u);
    CHECK(report.appliedCount == 2u);
    CHECK(report.missingCount == 1u);
    CHECK(core.mapping().getSourceBoneIndex(HumanoidBone::Hips) == 2);
    CHECK(core.mapping().getSourceBoneIndex(HumanoidBone::Spine) == 3);
    CHECK(core.mapping().getSourceBoneIndex(HumanoidBone::Head) == 4);

    RigProfileInfo info;
    CHECK(SkeletonEditorCore::inspectRigProfile(
        templatePath.string(), info, &error));
    CHECK(info.kind == RigProfileKind::Template);
    CHECK(info.name == "Canonical Test");
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

TEST_CASE(skeleton_editor_core_authors_retarget_profile_without_fake_bake)
{
    const auto sourcePath = writeSkeleton();
    const auto targetPath = writeSkeleton(false, false, "target.ayskel");
    const auto profilePath = fixtureRoot() / "synthetic-retarget.ayrig";
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(sourcePath.string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.configureRetarget(targetPath.string(), "windows-d3d12", &error));
    CHECK(core.profileKind() == RigProfileKind::Retarget);
    CHECK(core.targetSkeletonPath() == targetPath.generic_string());
    CHECK(core.bakePlatform() == "windows-d3d12");
    CHECK(core.outputMode() == "BakeToTarget");
    CHECK(core.saveMappingAs(profilePath.string(), &error));

    const auto encoded = nlohmann::json::parse(
        ayt::io::File::readAllText(profilePath.string()));
    CHECK(encoded["kind"] == "retarget");
    CHECK(encoded["target"]["skeleton"] == "target.ayskel");
    CHECK(encoded["output"]["mode"] == "BakeToTarget");
    CHECK(encoded["output"]["platform"] == "windows-d3d12");

    RigProfileInfo info;
    CHECK(SkeletonEditorCore::inspectRigProfile(
        profilePath.string(), info, &error));
    CHECK(info.kind == RigProfileKind::Retarget);
    CHECK(info.targetSkeletonPath == targetPath.generic_string());
    CHECK(info.platform == "windows-d3d12");

    auto unsupported = encoded;
    unsupported["output"]["mode"] = "UnknownRetargetMode";
    const auto unsupportedPath = fixtureRoot() / "unsupported-retarget.ayrig";
    CHECK(ayt::io::File::writeAllText(
        unsupportedPath.string(), unsupported.dump(2) + "\n"));
    SkeletonEditorCore rejected;
    CHECK_FALSE(rejected.open(unsupportedPath.string(), &error));
    CHECK(error.find("unsupported") != std::string::npos);

    SkeletonEditorCore reopened;
    CHECK(reopened.open(profilePath.string(), &error));
    CHECK(reopened.profileKind() == RigProfileKind::Retarget);
    const auto plan = reopened.dryRunBake();
    CHECK_FALSE(plan.canBake());
    CHECK_FALSE(plan.scopeTag.empty());
    CHECK(plan.receiptPath.find(plan.scopeTag) != std::string::npos);
    CHECK(std::any_of(plan.preflight.issues.begin(), plan.preflight.issues.end(),
        [](const SkeletonPreflightIssue& issue) {
            return issue.code
                == SkeletonPreflightCode::RetargetSolverUnavailable;
        }));
    CHECK(reopened.clearRetarget());
    CHECK(reopened.profileKind() == RigProfileKind::Mapping);
    CHECK(reopened.targetSkeletonPath().empty());
}

TEST_CASE(skeleton_editor_core_exposes_complete_authoring_statuses)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    CHECK(core.bind(HumanoidBone::Hips, 2));
    CHECK(core.status().adaptation == SkeletonAdaptationState::Incomplete);
    CHECK(core.bind(HumanoidBone::Spine, 2));
    CHECK(core.status().adaptation == SkeletonAdaptationState::Invalid);
    CHECK(core.setNotApplicable(true));
    CHECK(core.status().adaptation == SkeletonAdaptationState::NotApplicable);
    core.setBakeInProgress(true);
    CHECK(core.status().bake == SkeletonBakeState::Baking);
    core.setBakeInProgress(false);
    CHECK(core.status().bake == SkeletonBakeState::NotBaked);
}

TEST_CASE(skeleton_bake_dry_run_is_auditable_and_does_not_modify_sources)
{
    const auto skeletonPath = writeSkeleton(true, true);
    const auto animationPath = writeAnimationForNode();
    const auto sourceBefore = ayt::io::File::readAllBytes(skeletonPath.string());
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(skeletonPath.string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.bind(HumanoidBone::Head, 4));
    CHECK(core.validation().isValid());

    const auto plan = core.dryRunBake(
        {animationPath.string()}, {(fixtureRoot() / "character.aymesh").string()});
    CHECK(plan.canBake());
    CHECK(plan.boneActionCount(SkeletonBakeBoneAction::Rename) == 1u);
    CHECK(plan.boneActionCount(SkeletonBakeBoneAction::Delete) == 1u);
    CHECK(plan.boneActionCount(SkeletonBakeBoneAction::Keep) == 16u);
    CHECK(plan.dependencies.size() == 2u);
    CHECK(plan.dependencies[0].kind == SkeletonBakeDependencyKind::Animation);
    CHECK(plan.dependencies[1].kind == SkeletonBakeDependencyKind::Mesh);

    const auto manifestPath = fixtureRoot() / "synthetic.ayrig.bake-plan.json";
    CHECK(core.writeDryRunManifest(plan, manifestPath.string(), &error));
    const auto manifest = nlohmann::json::parse(
        ayt::io::File::readAllText(manifestPath.string()));
    CHECK(manifest["type"] == "SkeletonBakeDryRun");
    CHECK(manifest["version"] == kSkeletonBakePlanSchemaVersion);
    CHECK(manifest["summary"]["rename"] == 1u);
    CHECK(manifest["summary"]["delete"] == 1u);
    CHECK(manifest["bones"].size() == 18u);
    CHECK(ayt::io::File::readAllBytes(skeletonPath.string()) == sourceBefore);
}

TEST_SUITE_END

TEST_SUITE(SkeletonBakeJobTests)

TEST_CASE(skeleton_bake_job_writes_cleaned_outputs_and_records_current_state)
{
    const auto skeletonPath = writeSkeleton(true, true);
    const auto animationPath = writeAnimationForNode(
        "characterHead", "head_motion.ayanm");
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(skeletonPath.string(), &error));
    CHECK(core.applyCanonicalNameTemplate());
    CHECK(core.bind(HumanoidBone::Head, 4));
    CHECK(core.saveMapping(&error));
    const auto plan = core.dryRunBake({animationPath.string()});
    CHECK(plan.canBake());

    SkeletonBakeJob job;
    const auto output = fixtureRoot() / "Baked";
    const std::uint64_t generation = job.start(plan, output.string());
    SkeletonBakeJobSnapshot snapshot;
    for (int attempt = 0; attempt < 400; ++attempt) {
        snapshot = job.poll();
        if (snapshot.finished()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(snapshot.generation == generation);
    CHECK(snapshot.state == SkeletonBakeJobState::Succeeded);
    CHECK(snapshot.progress == 1.0f);
    CHECK(snapshot.outputPaths.size() == 3u);

    Skeleton baked;
    CHECK(baked.load((output / "synthetic.baked.ayskel").string()));
    CHECK(baked.getBoneCount() == 17u);
    CHECK(baked.findBone("head") == 4);
    CHECK(baked.findBone("headAccessoryHelper") == -1);
    Animation animation;
    const auto bakedAnimationBytes = ayt::io::File::readAllBytes(
        (output / "head_motion.baked.ayanm").string());
    CHECK(animation.loadFromBinary(
        bakedAnimationBytes.data(), bakedAnimationBytes.size()));
    CHECK(animation.getTrackNodeName(0) != nullptr);
    if (animation.getTrackNodeName(0) != nullptr) {
        CHECK(std::string(animation.getTrackNodeName(0)) == "head");
    }
    CHECK(core.setNative(true));
    CHECK_FALSE(core.recordBakeResult(true, snapshot.sourceFingerprint,
        snapshot.profileFingerprint, &error));
    CHECK(error.find("RigProfile revision") != std::string::npos);
    CHECK(core.setNative(false));
    CHECK(core.recordBakeResult(true, snapshot.sourceFingerprint,
        snapshot.profileFingerprint, &error));
    CHECK(core.status().bake == SkeletonBakeState::Current);
    CHECK(core.saveMapping(&error));

    SkeletonEditorCore reopened;
    CHECK(reopened.open(skeletonPath.string(), &error));
    CHECK(reopened.status().bake == SkeletonBakeState::Current);
    CHECK(reopened.setNative(true));
    CHECK(reopened.saveMapping(&error));
    SkeletonEditorCore stale;
    CHECK(stale.open(skeletonPath.string(), &error));
    CHECK(stale.status().bake == SkeletonBakeState::Stale);
}

TEST_CASE(skeleton_bake_job_rejects_blocked_and_isolates_generations)
{
    SkeletonEditorCore core;
    std::string error;
    CHECK(core.open(writeSkeleton().string(), &error));
    const auto blocked = core.dryRunBake();
    SkeletonBakeJob job;
    const auto output = (fixtureRoot() / "blocked").string();
    const std::uint64_t first = job.start(blocked, output);
    const std::uint64_t second = job.start(blocked, output);
    CHECK(second > first);
    SkeletonBakeJobSnapshot snapshot;
    for (int attempt = 0; attempt < 200; ++attempt) {
        snapshot = job.poll();
        if (snapshot.finished()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(snapshot.generation == second);
    CHECK(snapshot.state == SkeletonBakeJobState::Failed);
    CHECK(snapshot.message.find("preflight") != std::string::npos);
}

TEST_SUITE_END

int main(int, char**)
{
    return ayt::test::runAllTests("AYAnimationEditorCore_UnitTests");
}
