#include <AYAnimation/HumanoidRetarget.h>
#include <AYTest.h>

#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>

#include <cmath>
#include <iterator>
#include <string>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace {

constexpr float kEpsilon = 1.0e-4f;

struct BoneDef { const char* name; int parent; float x; float y; };

constexpr BoneDef kBones[] = {
    {"sceneRoot", -1, 0, 0}, {"motionRoot", 0, 0, 0},
    {"hips", 1, 0, 1}, {"spine", 2, 0, 1}, {"head", 3, 0, 1},
    {"leftUpperArm", 3, -1, 0}, {"leftLowerArm", 5, -1, 0},
    {"leftHand", 6, -1, 0}, {"rightUpperArm", 3, 1, 0},
    {"rightLowerArm", 8, 1, 0}, {"rightHand", 9, 1, 0},
    {"leftUpperLeg", 2, -0.4f, -1}, {"leftLowerLeg", 11, 0, -1},
    {"leftFoot", 12, 0, -1}, {"rightUpperLeg", 2, 0.4f, -1},
    {"rightLowerLeg", 14, 0, -1}, {"rightFoot", 15, 0, -1},
};

Skeleton makeSkeleton(float size, bool targetNames = false,
    bool helper = false)
{
    Skeleton skeleton;
    for (const BoneDef& def : kBones) {
        Bone bone;
        bone.name = targetNames ? std::string("T_") + def.name : def.name;
        bone.parentIndex = def.parent;
        bone.localPosition = {def.x * size, def.y * size, 0.0f};
        bone.localRotation = FQuaternion::identity();
        bone.localScale = {1, 1, 1};
        bone.inverseBindMatrix = Float4x4::identity();
        skeleton.addBone(bone);
    }
    if (helper) {
        Bone bone;
        bone.name = "helper";
        bone.parentIndex = 4;
        bone.localPosition = {0, 0.25f, 0};
        bone.localRotation = FQuaternion::identity();
        bone.localScale = {1, 1, 1};
        bone.inverseBindMatrix = Float4x4::identity();
        skeleton.addBone(bone);
    }
    return skeleton;
}

HumanoidBoneMap canonicalMapping()
{
    HumanoidBoneMap mapping;
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        for (std::size_t index = 0; index < std::size(kBones); ++index) {
            if (spec.canonicalName == kBones[index].name) {
                (void)mapping.bind(spec.role, static_cast<int>(index));
                break;
            }
        }
    }
    return mapping;
}

HumanoidRetargetDefinition definition()
{
    HumanoidRetargetDefinition result;
    result.sourceMapping = canonicalMapping();
    result.targetMapping = canonicalMapping();
    return result;
}

void copyRestPose(const Skeleton& skeleton, HumanoidLocalPose& pose)
{
    pose.positions.assign(skeleton.getLocalPositions(),
        skeleton.getLocalPositions() + skeleton.getBoneCount());
    pose.rotations.assign(skeleton.getLocalRotations(),
        skeleton.getLocalRotations() + skeleton.getBoneCount());
    pose.scales.assign(skeleton.getLocalScales(),
        skeleton.getLocalScales() + skeleton.getBoneCount());
}

} // namespace

TEST_SUITE(HumanoidRetargetTests)

TEST_CASE(retarget_rest_pose_is_target_rest_and_root_translation_is_scaled)
{
    const Skeleton source = makeSkeleton(1.0f);
    const Skeleton target = makeSkeleton(2.0f, true);
    HumanoidRetargetDefinition rig = definition();
    auto& hipsCorrection = rig.corrections[
        static_cast<std::size_t>(HumanoidBone::Hips)];
    hipsCorrection.sourceReferenceOffset = FQuaternion::fromAxisAngle(
        {1, 0, 0}, 0.2f);
    hipsCorrection.targetReferenceOffset = FQuaternion::fromAxisAngle(
        {0, 1, 0}, -0.3f);
    hipsCorrection.axisCorrection = FQuaternion::fromAxisAngle(
        {0, 0, 1}, 0.4f);

    HumanoidLocalPose sourcePose;
    copyRestPose(source, sourcePose);
    HumanoidLocalPose targetPose;
    CHECK_TRUE(retargetHumanoidLocalPose(source, target, rig,
        sourcePose.positions, sourcePose.rotations, sourcePose.scales,
        targetPose).succeeded());
    CHECK_FLOAT_EQ(target.getLocalPositions()[2].y,
        targetPose.positions[2].y, kEpsilon);
    CHECK(targetPose.rotations[2].dot(target.getLocalRotations()[2])
        > 0.9999f);

    sourcePose.positions[2].y += 1.0f;
    CHECK_TRUE(retargetHumanoidLocalPose(source, target, rig,
        sourcePose.positions, sourcePose.rotations, sourcePose.scales,
        targetPose).succeeded());
    CHECK_FLOAT_EQ(target.getLocalPositions()[2].y + 2.0f,
        targetPose.positions[2].y, kEpsilon);
}

TEST_CASE(retarget_converts_rotation_delta_through_axis_basis)
{
    const Skeleton source = makeSkeleton(1.0f);
    const Skeleton target = makeSkeleton(1.0f, true);
    HumanoidRetargetDefinition rig = definition();
    const auto role = HumanoidBone::LeftLowerArm;
    const FQuaternion axis = FQuaternion::fromAxisAngle({0, 0, 1},
        1.57079632679f);
    rig.corrections[static_cast<std::size_t>(role)].axisCorrection = axis;
    HumanoidLocalPose sourcePose;
    copyRestPose(source, sourcePose);
    const FQuaternion sourceDelta = FQuaternion::fromAxisAngle(
        {1, 0, 0}, 0.5f);
    sourcePose.rotations[6] = sourceDelta;
    HumanoidLocalPose targetPose;
    CHECK_TRUE(retargetHumanoidLocalPose(source, target, rig,
        sourcePose.positions, sourcePose.rotations, sourcePose.scales,
        targetPose).succeeded());
    const FQuaternion expected =
        (axis * sourceDelta * axis.inverse()).normalize();
    CHECK(std::abs(targetPose.rotations[6].dot(expected)) > 0.9999f);
}

TEST_CASE(retarget_animation_targets_real_target_bones_and_preserves_metadata)
{
    const Skeleton source = makeSkeleton(1.0f);
    const Skeleton target = makeSkeleton(2.0f, true);
    HumanoidRetargetDefinition rig = definition();
    Animation input;
    input.setName("walk");
    input.setDuration(1.0f);
    input.setTicksPerSecond(30.0f);
    AnimTrack rotation;
    rotation.nodeName = "leftLowerArm";
    rotation.property = "rotation";
    rotation.valueType = AnimTrackType::Quaternion;
    rotation.times = {0.0f, 30.0f};
    const FQuaternion delta = FQuaternion::fromAxisAngle({1, 0, 0}, 0.5f);
    rotation.values = {0, 0, 0, 1, delta.x, delta.y, delta.z, delta.w};
    input.addTrack(rotation);
    AnimTrack position;
    position.nodeName = "hips";
    position.property = "position";
    position.valueType = AnimTrackType::Vector3;
    position.times = {0.0f, 30.0f};
    position.values = {0, 1, 0, 0, 2, 0};
    input.addTrack(position);
    AnimTrack parameter;
    parameter.nodeName = "character";
    parameter.property = "speed";
    parameter.valueType = AnimTrackType::Float;
    parameter.times = {0.0f};
    parameter.values = {1.0f};
    input.addTrack(parameter);
    input.addNotify({"step", 0.5f, 1.0f});

    Animation output;
    const HumanoidRetargetResult result = retargetHumanoidAnimation(
        source, target, rig, input, output);
    CHECK_TRUE(result.succeeded());
    CHECK_INT_EQ(output.getTrackCount(), 3);
    CHECK(std::string(output.getTrackNodeName(0)) == "T_leftLowerArm");
    CHECK(std::string(output.getTrackNodeName(1)) == "T_hips");
    CHECK(std::string(output.getTrackNodeName(2)) == "character");
    CHECK_FLOAT_EQ(4.0f, output.getTrackValues(1)[4], kEpsilon);
    CHECK_INT_EQ(output.getNotifyCount(), 1);
    CHECK(std::string(output.getNotifyName(0)) == "step");
}

TEST_CASE(retarget_animation_rejects_unmapped_skeletal_tracks)
{
    const Skeleton source = makeSkeleton(1.0f, false, true);
    const Skeleton target = makeSkeleton(1.0f, true, true);
    Animation input;
    AnimTrack track;
    track.nodeName = "helper";
    track.property = "rotation";
    track.valueType = AnimTrackType::Quaternion;
    track.times = {0.0f};
    track.values = {0, 0, 0, 1};
    input.addTrack(track);
    Animation output;
    const HumanoidRetargetResult result = retargetHumanoidAnimation(
        source, target, definition(), input, output);
    CHECK_FALSE(result.succeeded());
    CHECK(result.error == HumanoidRetargetError::UnmappedAnimatedBone);
    CHECK_INT_EQ(result.trackIndex, 0);
}

TEST_SUITE_END
