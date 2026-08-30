#pragma once
// AYAnimation/HumanoidSkeleton.h — source-independent humanoid semantics.
//
// AYHumanoid uses the VRM 1.0 humanoid role set plus two optional engine
// roots. It deliberately contains no MMD/Mixamo name aliases: source
// adapters populate HumanoidBoneMap only after their conventions are
// verified against real assets (design §7, INV-78..81).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ayt::resource
{
class ISkeleton;
}

namespace ayt::anim
{

// The first two roles are AY engine extensions. The remaining 55 roles are
// the VRM 1.0 humanoid vocabulary. Numeric values are a stable public
// semantic contract; reordering requires an explicit versioning decision.
enum class HumanoidBone : std::uint8_t
{
    SceneRoot = 0,
    MotionRoot,
    Hips,
    Spine,
    Chest,
    UpperChest,
    Neck,
    Head,
    LeftEye,
    RightEye,
    Jaw,

    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,

    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,

    LeftThumbMetacarpal,
    LeftThumbProximal,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,

    RightThumbMetacarpal,
    RightThumbProximal,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,

    Count,
    Invalid = 0xFF
};

inline constexpr std::size_t kHumanoidBoneCount =
    static_cast<std::size_t>(HumanoidBone::Count);
inline constexpr int kUnmappedHumanoidBoneIndex = -1;

static_assert(kHumanoidBoneCount == 57,
              "AYHumanoid must remain 55 VRM roles + 2 AY roots");

enum class HumanoidBoneRequirement : std::uint8_t
{
    EngineExtension,
    Required,
    Optional
};

struct HumanoidBoneSpec
{
    HumanoidBone            role = HumanoidBone::Invalid;
    HumanoidBone            semanticParent = HumanoidBone::Invalid;
    std::string_view         canonicalName;
    HumanoidBoneRequirement requirement = HumanoidBoneRequirement::Optional;
};

[[nodiscard]] std::span<const HumanoidBoneSpec> getHumanoidBoneSpecs() noexcept;
[[nodiscard]] const HumanoidBoneSpec* getHumanoidBoneSpec(HumanoidBone role) noexcept;
[[nodiscard]] std::string_view getHumanoidBoneName(HumanoidBone role) noexcept;
[[nodiscard]] bool isRequiredHumanoidBone(HumanoidBone role) noexcept;

// A source skeleton mapping. The default is intentionally fully unmapped;
// this module does not guess bone names or ship source-specific aliases.
class HumanoidBoneMap
{
public:
    HumanoidBoneMap() noexcept;

    [[nodiscard]] bool bind(HumanoidBone role, int sourceBoneIndex) noexcept;
    [[nodiscard]] bool unbind(HumanoidBone role) noexcept;
    void clear() noexcept;

    [[nodiscard]] int getSourceBoneIndex(HumanoidBone role) const noexcept;
    [[nodiscard]] bool isBound(HumanoidBone role) const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t getBoundCount() const noexcept;

private:
    std::array<int, kHumanoidBoneCount> _sourceBoneIndices;
};

enum class HumanoidValidationError : std::uint8_t
{
    None,
    SourceBoneIndexOutOfRange,
    DuplicateSourceBone,
    SourceParentIndexOutOfRange,
    SourceHierarchyCycle,
    MissingRequiredBone,
    SemanticParentMismatch
};

struct HumanoidValidationResult
{
    HumanoidValidationError error = HumanoidValidationError::None;
    HumanoidBone role = HumanoidBone::Invalid;
    HumanoidBone relatedRole = HumanoidBone::Invalid;
    int sourceBoneIndex = kUnmappedHumanoidBoneIndex;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return error == HumanoidValidationError::None;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return isValid();
    }
};

// Validates semantic ancestry, not direct-parent equality. Unmapped source
// helper/twist/IK bones may therefore remain between two mapped roles.
[[nodiscard]] HumanoidValidationResult validateHumanoidSkeleton(
    const ayt::resource::ISkeleton& skeleton,
    const HumanoidBoneMap& mapping) noexcept;

} // namespace ayt::anim
