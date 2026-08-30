#include "AYAnimation/HumanoidSkeleton.h"

#include <AYResource/assetsDefs/ISkeleton.h>

namespace ayt::anim
{
namespace
{

using R = HumanoidBoneRequirement;
using B = HumanoidBone;

constexpr std::array<HumanoidBoneSpec, kHumanoidBoneCount> kBoneSpecs = {{
    { B::SceneRoot,              B::Invalid,                 "sceneRoot",              R::EngineExtension },
    { B::MotionRoot,             B::SceneRoot,               "motionRoot",             R::EngineExtension },
    { B::Hips,                   B::MotionRoot,              "hips",                   R::Required },
    { B::Spine,                  B::Hips,                    "spine",                  R::Required },
    { B::Chest,                  B::Spine,                   "chest",                  R::Optional },
    { B::UpperChest,             B::Chest,                   "upperChest",             R::Optional },
    { B::Neck,                   B::UpperChest,              "neck",                   R::Optional },
    { B::Head,                   B::Neck,                    "head",                   R::Required },
    { B::LeftEye,                B::Head,                    "leftEye",                R::Optional },
    { B::RightEye,               B::Head,                    "rightEye",               R::Optional },
    { B::Jaw,                    B::Head,                    "jaw",                    R::Optional },

    { B::LeftShoulder,           B::UpperChest,              "leftShoulder",           R::Optional },
    { B::LeftUpperArm,           B::LeftShoulder,            "leftUpperArm",           R::Required },
    { B::LeftLowerArm,           B::LeftUpperArm,            "leftLowerArm",           R::Required },
    { B::LeftHand,               B::LeftLowerArm,            "leftHand",               R::Required },
    { B::RightShoulder,          B::UpperChest,              "rightShoulder",          R::Optional },
    { B::RightUpperArm,          B::RightShoulder,           "rightUpperArm",          R::Required },
    { B::RightLowerArm,          B::RightUpperArm,           "rightLowerArm",          R::Required },
    { B::RightHand,              B::RightLowerArm,           "rightHand",              R::Required },

    { B::LeftUpperLeg,           B::Hips,                    "leftUpperLeg",           R::Required },
    { B::LeftLowerLeg,           B::LeftUpperLeg,            "leftLowerLeg",           R::Required },
    { B::LeftFoot,               B::LeftLowerLeg,            "leftFoot",               R::Required },
    { B::LeftToes,               B::LeftFoot,                "leftToes",               R::Optional },
    { B::RightUpperLeg,          B::Hips,                    "rightUpperLeg",          R::Required },
    { B::RightLowerLeg,          B::RightUpperLeg,           "rightLowerLeg",          R::Required },
    { B::RightFoot,              B::RightLowerLeg,           "rightFoot",              R::Required },
    { B::RightToes,              B::RightFoot,               "rightToes",              R::Optional },

    { B::LeftThumbMetacarpal,    B::LeftHand,                "leftThumbMetacarpal",    R::Optional },
    { B::LeftThumbProximal,      B::LeftThumbMetacarpal,     "leftThumbProximal",      R::Optional },
    { B::LeftThumbDistal,        B::LeftThumbProximal,       "leftThumbDistal",        R::Optional },
    { B::LeftIndexProximal,      B::LeftHand,                "leftIndexProximal",      R::Optional },
    { B::LeftIndexIntermediate,  B::LeftIndexProximal,       "leftIndexIntermediate",  R::Optional },
    { B::LeftIndexDistal,        B::LeftIndexIntermediate,   "leftIndexDistal",        R::Optional },
    { B::LeftMiddleProximal,     B::LeftHand,                "leftMiddleProximal",     R::Optional },
    { B::LeftMiddleIntermediate, B::LeftMiddleProximal,      "leftMiddleIntermediate", R::Optional },
    { B::LeftMiddleDistal,       B::LeftMiddleIntermediate,  "leftMiddleDistal",       R::Optional },
    { B::LeftRingProximal,       B::LeftHand,                "leftRingProximal",       R::Optional },
    { B::LeftRingIntermediate,   B::LeftRingProximal,        "leftRingIntermediate",   R::Optional },
    { B::LeftRingDistal,         B::LeftRingIntermediate,    "leftRingDistal",         R::Optional },
    { B::LeftLittleProximal,     B::LeftHand,                "leftLittleProximal",     R::Optional },
    { B::LeftLittleIntermediate, B::LeftLittleProximal,      "leftLittleIntermediate", R::Optional },
    { B::LeftLittleDistal,       B::LeftLittleIntermediate,  "leftLittleDistal",       R::Optional },

    { B::RightThumbMetacarpal,    B::RightHand,               "rightThumbMetacarpal",    R::Optional },
    { B::RightThumbProximal,      B::RightThumbMetacarpal,    "rightThumbProximal",      R::Optional },
    { B::RightThumbDistal,        B::RightThumbProximal,      "rightThumbDistal",        R::Optional },
    { B::RightIndexProximal,      B::RightHand,               "rightIndexProximal",      R::Optional },
    { B::RightIndexIntermediate,  B::RightIndexProximal,      "rightIndexIntermediate",  R::Optional },
    { B::RightIndexDistal,        B::RightIndexIntermediate,  "rightIndexDistal",        R::Optional },
    { B::RightMiddleProximal,     B::RightHand,               "rightMiddleProximal",     R::Optional },
    { B::RightMiddleIntermediate, B::RightMiddleProximal,     "rightMiddleIntermediate", R::Optional },
    { B::RightMiddleDistal,       B::RightMiddleIntermediate, "rightMiddleDistal",       R::Optional },
    { B::RightRingProximal,       B::RightHand,               "rightRingProximal",       R::Optional },
    { B::RightRingIntermediate,   B::RightRingProximal,       "rightRingIntermediate",   R::Optional },
    { B::RightRingDistal,         B::RightRingIntermediate,   "rightRingDistal",         R::Optional },
    { B::RightLittleProximal,     B::RightHand,               "rightLittleProximal",     R::Optional },
    { B::RightLittleIntermediate, B::RightLittleProximal,     "rightLittleIntermediate", R::Optional },
    { B::RightLittleDistal,       B::RightLittleIntermediate, "rightLittleDistal",       R::Optional }
}};

consteval bool hasValidSemanticTable()
{
    for (std::size_t i = 0; i < kBoneSpecs.size(); ++i) {
        if (static_cast<std::size_t>(kBoneSpecs[i].role) != i) {
            return false;
        }
        const B parent = kBoneSpecs[i].semanticParent;
        if (parent != B::Invalid && static_cast<std::size_t>(parent) >= i) {
            return false;
        }
    }
    return true;
}

static_assert(hasValidSemanticTable(),
              "AYHumanoid specs must match enum order and form an acyclic parent tree");

constexpr bool isRoleInRange(B role) noexcept
{
    return static_cast<std::size_t>(role) < kHumanoidBoneCount;
}

HumanoidValidationResult makeError(
    HumanoidValidationError error,
    B role,
    int sourceBoneIndex,
    B relatedRole = B::Invalid) noexcept
{
    HumanoidValidationResult result;
    result.error = error;
    result.role = role;
    result.relatedRole = relatedRole;
    result.sourceBoneIndex = sourceBoneIndex;
    return result;
}

} // namespace

std::span<const HumanoidBoneSpec> getHumanoidBoneSpecs() noexcept
{
    return kBoneSpecs;
}

const HumanoidBoneSpec* getHumanoidBoneSpec(HumanoidBone role) noexcept
{
    if (!isRoleInRange(role)) {
        return nullptr;
    }
    return &kBoneSpecs[static_cast<std::size_t>(role)];
}

std::string_view getHumanoidBoneName(HumanoidBone role) noexcept
{
    const HumanoidBoneSpec* spec = getHumanoidBoneSpec(role);
    return spec ? spec->canonicalName : std::string_view{};
}

bool isRequiredHumanoidBone(HumanoidBone role) noexcept
{
    const HumanoidBoneSpec* spec = getHumanoidBoneSpec(role);
    return spec && spec->requirement == HumanoidBoneRequirement::Required;
}

HumanoidBoneMap::HumanoidBoneMap() noexcept
{
    clear();
}

bool HumanoidBoneMap::bind(HumanoidBone role, int sourceBoneIndex) noexcept
{
    if (!isRoleInRange(role) || sourceBoneIndex < 0) {
        return false;
    }
    _sourceBoneIndices[static_cast<std::size_t>(role)] = sourceBoneIndex;
    return true;
}

bool HumanoidBoneMap::unbind(HumanoidBone role) noexcept
{
    if (!isRoleInRange(role)) {
        return false;
    }
    _sourceBoneIndices[static_cast<std::size_t>(role)] = kUnmappedHumanoidBoneIndex;
    return true;
}

void HumanoidBoneMap::clear() noexcept
{
    _sourceBoneIndices.fill(kUnmappedHumanoidBoneIndex);
}

int HumanoidBoneMap::getSourceBoneIndex(HumanoidBone role) const noexcept
{
    if (!isRoleInRange(role)) {
        return kUnmappedHumanoidBoneIndex;
    }
    return _sourceBoneIndices[static_cast<std::size_t>(role)];
}

bool HumanoidBoneMap::isBound(HumanoidBone role) const noexcept
{
    return getSourceBoneIndex(role) != kUnmappedHumanoidBoneIndex;
}

bool HumanoidBoneMap::empty() const noexcept
{
    return getBoundCount() == 0;
}

std::size_t HumanoidBoneMap::getBoundCount() const noexcept
{
    std::size_t count = 0;
    for (int index : _sourceBoneIndices) {
        count += index != kUnmappedHumanoidBoneIndex ? 1u : 0u;
    }
    return count;
}

HumanoidValidationResult validateHumanoidSkeleton(
    const ayt::resource::ISkeleton& skeleton,
    const HumanoidBoneMap& mapping) noexcept
{
    const std::size_t sourceBoneCount = skeleton.getBoneCount();

    // Validate mapping bounds and one-to-one ownership first. This also
    // catches a mapping that became stale after swapping skeleton assets.
    for (std::size_t i = 0; i < kHumanoidBoneCount; ++i) {
        const B role = static_cast<B>(i);
        const int sourceIndex = mapping.getSourceBoneIndex(role);
        if (sourceIndex == kUnmappedHumanoidBoneIndex) {
            continue;
        }
        if (static_cast<std::size_t>(sourceIndex) >= sourceBoneCount) {
            return makeError(HumanoidValidationError::SourceBoneIndexOutOfRange,
                             role, sourceIndex);
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            const B previousRole = static_cast<B>(previous);
            if (mapping.getSourceBoneIndex(previousRole) == sourceIndex) {
                return makeError(HumanoidValidationError::DuplicateSourceBone,
                                 role, sourceIndex, previousRole);
            }
        }
    }

    // Every mapped path must terminate at parent=-1 without leaving the
    // source array or cycling. Intermediate, unmapped bones are included.
    for (std::size_t i = 0; i < kHumanoidBoneCount; ++i) {
        const B role = static_cast<B>(i);
        int current = mapping.getSourceBoneIndex(role);
        if (current == kUnmappedHumanoidBoneIndex) {
            continue;
        }

        bool terminated = false;
        for (std::size_t step = 0; step < sourceBoneCount; ++step) {
            const int parent = skeleton.getParentBoneIndex(
                static_cast<std::size_t>(current));
            if (parent == -1) {
                terminated = true;
                break;
            }
            if (parent < -1 || static_cast<std::size_t>(parent) >= sourceBoneCount) {
                return makeError(HumanoidValidationError::SourceParentIndexOutOfRange,
                                 role, parent);
            }
            current = parent;
        }
        if (!terminated) {
            return makeError(HumanoidValidationError::SourceHierarchyCycle,
                             role, mapping.getSourceBoneIndex(role));
        }
    }

    for (const HumanoidBoneSpec& spec : kBoneSpecs) {
        if (spec.requirement == R::Required && !mapping.isBound(spec.role)) {
            return makeError(HumanoidValidationError::MissingRequiredBone,
                             spec.role, kUnmappedHumanoidBoneIndex);
        }
    }

    // Resolve the nearest mapped semantic ancestor. This intentionally does
    // not require it to be the direct source parent (INV-80).
    for (const HumanoidBoneSpec& spec : kBoneSpecs) {
        const int childIndex = mapping.getSourceBoneIndex(spec.role);
        if (childIndex == kUnmappedHumanoidBoneIndex) {
            continue;
        }

        B ancestorRole = spec.semanticParent;
        while (ancestorRole != B::Invalid && !mapping.isBound(ancestorRole)) {
            const HumanoidBoneSpec* ancestorSpec = getHumanoidBoneSpec(ancestorRole);
            ancestorRole = ancestorSpec ? ancestorSpec->semanticParent : B::Invalid;
        }
        if (ancestorRole == B::Invalid) {
            continue;
        }

        const int expectedAncestorIndex = mapping.getSourceBoneIndex(ancestorRole);
        int current = skeleton.getParentBoneIndex(static_cast<std::size_t>(childIndex));
        bool found = false;
        for (std::size_t step = 0; step < sourceBoneCount && current >= 0; ++step) {
            if (current == expectedAncestorIndex) {
                found = true;
                break;
            }
            current = skeleton.getParentBoneIndex(static_cast<std::size_t>(current));
        }
        if (!found) {
            return makeError(HumanoidValidationError::SemanticParentMismatch,
                             spec.role, childIndex, ancestorRole);
        }
    }

    return {};
}

} // namespace ayt::anim
