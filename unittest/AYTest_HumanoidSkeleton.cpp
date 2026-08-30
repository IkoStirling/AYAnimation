// AYTest_HumanoidSkeleton.cpp — P4-3 AYHumanoid semantic skeleton tests.
//
// These fixtures use deliberately generic source names. There are no MMD or
// Mixamo aliases to test: the built-in mapping is intentionally empty until
// representative assets have been verified (design §7, INV-81).

#include "AYAnimation.h"
#include <AYTest.h>

#include <AYMath/MathTypes.h>
#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsImpl/Skeleton.h>

#include <string>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace
{

Bone makeBone(const char* name, int parent)
{
    Bone bone;
    bone.name = name;
    bone.parentIndex = parent;
    bone.localPosition = FVector3(0, 0, 0);
    bone.localRotation = FQuaternion::identity();
    bone.localScale = FVector3(1, 1, 1);
    bone.inverseBindMatrix = Float4x4::identity();
    return bone;
}

// Source hierarchy with two deliberately unmapped torso helpers:
// Hips -> Spine -> ChestAux -> NeckAux -> Head. The semantic map omits both
// helpers, proving that validation uses ancestry instead of direct parents.
Skeleton makeRequiredSkeleton()
{
    Skeleton skeleton;
    skeleton.setBoneCount(17);
    skeleton.setBone(0,  makeBone("Hips",          -1));
    skeleton.setBone(1,  makeBone("Spine",          0));
    skeleton.setBone(2,  makeBone("ChestAux",       1));
    skeleton.setBone(3,  makeBone("NeckAux",        2));
    skeleton.setBone(4,  makeBone("Head",           3));
    skeleton.setBone(5,  makeBone("LeftUpperLeg",   0));
    skeleton.setBone(6,  makeBone("LeftLowerLeg",   5));
    skeleton.setBone(7,  makeBone("LeftFoot",       6));
    skeleton.setBone(8,  makeBone("RightUpperLeg",  0));
    skeleton.setBone(9,  makeBone("RightLowerLeg",  8));
    skeleton.setBone(10, makeBone("RightFoot",      9));
    skeleton.setBone(11, makeBone("LeftUpperArm",   2));
    skeleton.setBone(12, makeBone("LeftLowerArm",  11));
    skeleton.setBone(13, makeBone("LeftHand",      12));
    skeleton.setBone(14, makeBone("RightUpperArm",  2));
    skeleton.setBone(15, makeBone("RightLowerArm", 14));
    skeleton.setBone(16, makeBone("RightHand",     15));
    return skeleton;
}

HumanoidBoneMap makeRequiredMapping()
{
    HumanoidBoneMap mapping;
    (void)mapping.bind(HumanoidBone::Hips,          0);
    (void)mapping.bind(HumanoidBone::Spine,         1);
    (void)mapping.bind(HumanoidBone::Head,          4);
    (void)mapping.bind(HumanoidBone::LeftUpperLeg,  5);
    (void)mapping.bind(HumanoidBone::LeftLowerLeg,  6);
    (void)mapping.bind(HumanoidBone::LeftFoot,      7);
    (void)mapping.bind(HumanoidBone::RightUpperLeg, 8);
    (void)mapping.bind(HumanoidBone::RightLowerLeg, 9);
    (void)mapping.bind(HumanoidBone::RightFoot,    10);
    (void)mapping.bind(HumanoidBone::LeftUpperArm, 11);
    (void)mapping.bind(HumanoidBone::LeftLowerArm, 12);
    (void)mapping.bind(HumanoidBone::LeftHand,     13);
    (void)mapping.bind(HumanoidBone::RightUpperArm,14);
    (void)mapping.bind(HumanoidBone::RightLowerArm,15);
    (void)mapping.bind(HumanoidBone::RightHand,    16);
    return mapping;
}

} // namespace

TEST_SUITE(HumanoidSkeletonTests)

    TEST_CASE(role_table_matches_vrm_plus_engine_roots) {
        const auto specs = getHumanoidBoneSpecs();
        CHECK_INT_EQ(specs.size(), 57);
        CHECK_INT_EQ(static_cast<int>(HumanoidBone::Hips), 2);
        CHECK(getHumanoidBoneName(HumanoidBone::Head) == "head");
        CHECK(getHumanoidBoneSpec(HumanoidBone::Hips)->semanticParent ==
              HumanoidBone::MotionRoot);
        CHECK(getHumanoidBoneSpec(HumanoidBone::LeftIndexIntermediate)->semanticParent ==
              HumanoidBone::LeftIndexProximal);
        CHECK_NULL(getHumanoidBoneSpec(HumanoidBone::Invalid));

        int requiredCount = 0;
        for (const HumanoidBoneSpec& spec : specs) {
            requiredCount += spec.requirement == HumanoidBoneRequirement::Required ? 1 : 0;
        }
        CHECK_INT_EQ(requiredCount, 15);
    }

    TEST_CASE(mapping_is_empty_until_explicitly_populated) {
        HumanoidBoneMap mapping;
        CHECK_TRUE(mapping.empty());
        CHECK_INT_EQ(mapping.getBoundCount(), 0);
        CHECK_INT_EQ(mapping.getSourceBoneIndex(HumanoidBone::Head), -1);
        CHECK_FALSE(mapping.bind(HumanoidBone::Invalid, 0));
        CHECK_FALSE(mapping.bind(HumanoidBone::Head, -1));
        CHECK_TRUE(mapping.bind(HumanoidBone::Head, 7));
        CHECK_TRUE(mapping.isBound(HumanoidBone::Head));
        CHECK_INT_EQ(mapping.getBoundCount(), 1);
        CHECK_TRUE(mapping.unbind(HumanoidBone::Head));
        CHECK_TRUE(mapping.empty());
    }

    TEST_CASE(required_mapping_accepts_unmapped_intermediate_bones) {
        const Skeleton skeleton = makeRequiredSkeleton();
        const HumanoidBoneMap mapping = makeRequiredMapping();
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_TRUE(result.isValid());
        CHECK_INT_EQ(result.error, HumanoidValidationError::None);
    }

    TEST_CASE(optional_role_uses_nearest_mapped_semantic_ancestor) {
        const Skeleton skeleton = makeRequiredSkeleton();
        HumanoidBoneMap mapping = makeRequiredMapping();
        CHECK_TRUE(mapping.bind(HumanoidBone::Chest, 2));
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_TRUE(result.isValid());
    }

    TEST_CASE(missing_required_role_is_reported) {
        const Skeleton skeleton = makeRequiredSkeleton();
        HumanoidBoneMap mapping = makeRequiredMapping();
        CHECK_TRUE(mapping.unbind(HumanoidBone::Head));
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error, HumanoidValidationError::MissingRequiredBone);
        CHECK(result.role == HumanoidBone::Head);
    }

    TEST_CASE(duplicate_source_bone_is_rejected) {
        const Skeleton skeleton = makeRequiredSkeleton();
        HumanoidBoneMap mapping = makeRequiredMapping();
        CHECK_TRUE(mapping.bind(HumanoidBone::Chest, 1));
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error, HumanoidValidationError::DuplicateSourceBone);
        CHECK(result.role == HumanoidBone::Chest);
        CHECK(result.relatedRole == HumanoidBone::Spine);
    }

    TEST_CASE(out_of_range_source_index_is_rejected) {
        const Skeleton skeleton = makeRequiredSkeleton();
        HumanoidBoneMap mapping = makeRequiredMapping();
        CHECK_TRUE(mapping.bind(HumanoidBone::Chest, 99));
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error, HumanoidValidationError::SourceBoneIndexOutOfRange);
        CHECK(result.role == HumanoidBone::Chest);
        CHECK_INT_EQ(result.sourceBoneIndex, 99);
    }

    TEST_CASE(semantic_parent_mismatch_is_rejected) {
        Skeleton skeleton = makeRequiredSkeleton();
        skeleton.setBone(4, makeBone("Head", 7)); // under LeftFoot, not Spine
        const HumanoidBoneMap mapping = makeRequiredMapping();
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error, HumanoidValidationError::SemanticParentMismatch);
        CHECK(result.role == HumanoidBone::Head);
        CHECK(result.relatedRole == HumanoidBone::Spine);
    }

    TEST_CASE(invalid_source_parent_index_is_rejected) {
        Skeleton skeleton = makeRequiredSkeleton();
        skeleton.setBone(4, makeBone("Head", 99));
        const HumanoidBoneMap mapping = makeRequiredMapping();
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error,
                     HumanoidValidationError::SourceParentIndexOutOfRange);
        CHECK(result.role == HumanoidBone::Head);
        CHECK_INT_EQ(result.sourceBoneIndex, 99);
    }

    TEST_CASE(source_hierarchy_cycle_is_rejected) {
        Skeleton skeleton = makeRequiredSkeleton();
        skeleton.setBone(1, makeBone("Spine", 4)); // Spine -> ... -> Head -> Spine
        const HumanoidBoneMap mapping = makeRequiredMapping();
        const HumanoidValidationResult result =
            validateHumanoidSkeleton(skeleton, mapping);
        CHECK_INT_EQ(result.error, HumanoidValidationError::SourceHierarchyCycle);
        CHECK(result.role == HumanoidBone::Spine);
    }

TEST_SUITE_END
