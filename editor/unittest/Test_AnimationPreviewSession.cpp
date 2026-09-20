#include <AYAnimationEditor/AnimationPreviewSession.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Mesh.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYTest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

using namespace ayt::anim::editor;
using namespace ayt::resource;

namespace {

std::filesystem::path previewFixtureRoot()
{
    const auto root = std::filesystem::temp_directory_path()
        / "ay_animation_preview_session";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "animations", error);
    std::filesystem::create_directories(root / "skeletons", error);
    std::filesystem::create_directories(root / "meshes", error);
    return root;
}

std::filesystem::path writePreviewSkeleton(const std::filesystem::path& root)
{
    Skeleton skeleton;
    Bone sceneRoot;
    sceneRoot.name = "root";
    sceneRoot.parentIndex = -1;
    sceneRoot.localRotation = ayt::math::FQuaternion::identity();
    sceneRoot.localScale = {1, 1, 1};
    sceneRoot.inverseBindMatrix = ayt::math::Float4x4::identity();
    skeleton.addBone(sceneRoot);
    Bone hips = sceneRoot;
    hips.name = "hips";
    hips.parentIndex = 0;
    hips.localPosition = {0, 1, 0};
    skeleton.addBone(hips);
    std::vector<ayt::math::UInt8> bytes;
    CHECK(skeleton.saveToBinary(bytes));
    const auto path = root / "skeletons" / "hero_Skeleton.ayskel";
    CHECK(ayt::io::File::writeAllBytes(path.string(), bytes));
    return path;
}

std::filesystem::path writePreviewAnimation(const std::filesystem::path& root,
                                            const char* node = "hips")
{
    Animation animation;
    animation.setName("hero_idle");
    animation.setDuration(1.0f);
    animation.setTicksPerSecond(1.0f);
    AnimTrack track;
    track.nodeName = node;
    track.property = "position";
    track.valueType = AnimTrackType::Vector3;
    track.times = {0.0f, 1.0f};
    track.values = {0, 1, 0, 0, 2, 0};
    animation.addTrack(track);
    std::vector<ayt::math::UInt8> bytes;
    CHECK(animation.saveToBinary(bytes));
    const auto path = root / "animations" / "hero_idle.ayanm";
    CHECK(ayt::io::File::writeAllBytes(path.string(), bytes));
    return path;
}

std::filesystem::path writePreviewMesh(const std::filesystem::path& root)
{
    Mesh mesh;
    mesh.createCube(1.0f);
    std::vector<VertexSkinWeight> weights(mesh.getVertexCount());
    for (auto& value : weights) {
        value.boneIndex[0] = 0u;
        value.boneWeight[0] = 1.0f;
    }
    mesh.debugSetSkinWeights(weights);
    std::vector<ayt::math::UInt8> bytes;
    CHECK(mesh.saveToBinary(bytes));
    const auto path = root / "meshes" / "hero_Body.aymesh";
    CHECK(ayt::io::File::writeAllBytes(path.string(), bytes));
    return path;
}

} // namespace

TEST_SUITE(AnimationPreviewSessionTests)

TEST_CASE(animation_preview_infers_import_batch_and_previews_model_and_skeleton)
{
    const auto root = previewFixtureRoot();
    const auto animationPath = writePreviewAnimation(root);
    const auto skeletonPath = writePreviewSkeleton(root);
    const auto meshPath = writePreviewMesh(root);
    const nlohmann::json dependency = {
        {"resources", nlohmann::json::array()},
        {"dependencies", {
            {{"from", "meshes/hero_Body.aymesh"},
             {"to", "skeletons/hero_Skeleton.ayskel"}},
            {{"from", "skeletons/hero_Skeleton.ayskel"},
             {"to", "animations/hero_idle.ayanm"}},
        }},
    };
    CHECK(ayt::io::File::writeAllText(
        (root / "hero.aydep.json").string(), dependency.dump(2)));

    AnimationPreviewSession session;
    std::string error;
    CHECK(session.openAnimation(animationPath.string(), &error));
    const AnimationPreviewBindings inferred = session.inferCompanionAssets();
    CHECK(inferred.skeletonPath == skeletonPath.generic_string());
    CHECK(inferred.meshPath == meshPath.generic_string());
    CHECK(session.applyBindings(inferred, &error));
    CHECK(session.canPreviewSkeleton());
    CHECK(session.canPreviewModel());
    CHECK(session.effectivePreviewMode() == AnimationPreviewMode::ModelAndSkeleton);
    CHECK(session.bones().size() == 2u);
    CHECK(session.skinMatrices().size() == 2u);

    const auto poseRevision = session.poseRevision();
    CHECK(session.setTime(0.5f));
    CHECK(session.poseRevision() > poseRevision);
    CHECK(session.poseWorldMatrices()[1].transformPoint({0, 0, 0}).y > 1.0f);
    session.setPreviewMode(AnimationPreviewMode::ModelOnly);
    CHECK(session.effectivePreviewMode() == AnimationPreviewMode::ModelOnly);
    session.play();
    session.tick(0.1f);
    CHECK(session.time() > 0.5f);
    session.pause();
}

TEST_CASE(animation_preview_falls_back_to_skeleton_and_reports_missing_tracks)
{
    const auto root = previewFixtureRoot();
    AnimationPreviewSession session;
    std::string error;
    CHECK(session.openAnimation(
        writePreviewAnimation(root, "missingBone").string(), &error));
    CHECK(session.bindSkeleton(writePreviewSkeleton(root).string(), &error));
    session.setPreviewMode(AnimationPreviewMode::ModelOnly);
    CHECK(session.effectivePreviewMode() == AnimationPreviewMode::SkeletonOnly);
    CHECK(session.missingTrackCount() == 1u);
    CHECK(std::any_of(session.diagnostics().begin(), session.diagnostics().end(),
        [](const AnimationPreviewDiagnostic& diagnostic) {
            return diagnostic.code
                == AnimationPreviewDiagnosticCode::AnimationTrackBoneMissing;
        }));
}

TEST_SUITE_END
