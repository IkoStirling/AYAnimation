#pragma once

#include <AYAnimation/HumanoidSkeleton.h>
#include <AYMath/MathTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ayt::resource {
class Animation;
class IAnimation;
class ISkeleton;
}

namespace ayt::anim {

// A correction is a change of local reference coordinates, not an authored
// pose. Therefore an unanimated source rest pose always resolves to the target
// rest pose, including when any of these quaternions is non-identity.
struct HumanoidRetargetBoneCorrection {
    ayt::math::FQuaternion sourceReferenceOffset =
        ayt::math::FQuaternion::identity();
    ayt::math::FQuaternion targetReferenceOffset =
        ayt::math::FQuaternion::identity();
    ayt::math::FQuaternion axisCorrection =
        ayt::math::FQuaternion::identity();
};

enum class HumanoidRetargetTranslationPolicy : std::uint8_t {
    None,
    RootsAndHipsScaled,
    AllMappedScaled,
};

struct HumanoidRetargetDefinition {
    HumanoidBoneMap sourceMapping;
    HumanoidBoneMap targetMapping;
    std::array<HumanoidRetargetBoneCorrection, kHumanoidBoneCount>
        corrections{};
    HumanoidRetargetTranslationPolicy translationPolicy =
        HumanoidRetargetTranslationPolicy::RootsAndHipsScaled;
};

enum class HumanoidRetargetError : std::uint8_t {
    None,
    InvalidSourceMapping,
    InvalidTargetMapping,
    InvalidCorrection,
    PoseSizeMismatch,
    AnimationTrackDataMissing,
    AnimationTrackBoneMissing,
    UnmappedAnimatedBone,
    UnsupportedTrack,
    UnsupportedAdditiveTrack,
};

struct HumanoidRetargetResult {
    HumanoidRetargetError error = HumanoidRetargetError::None;
    HumanoidBone role = HumanoidBone::Invalid;
    int sourceBoneIndex = -1;
    int targetBoneIndex = -1;
    int trackIndex = -1;
    std::string message;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == HumanoidRetargetError::None;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return succeeded();
    }
};

struct HumanoidLocalPose {
    std::vector<ayt::math::FVector3> positions;
    std::vector<ayt::math::FQuaternion> rotations;
    std::vector<ayt::math::FVector3> scales;
};

[[nodiscard]] HumanoidRetargetResult validateHumanoidRetarget(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition) noexcept;

// Returns a target/source reference-height ratio. Degenerate rigs fall back to
// 1 so translation conversion remains finite and deterministic.
[[nodiscard]] float humanoidRetargetTranslationScale(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition) noexcept;

// Converts a complete source local pose into a complete target local pose.
// Target-unmapped bones stay at their target rest transform.
[[nodiscard]] HumanoidRetargetResult retargetHumanoidLocalPose(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition,
    std::span<const ayt::math::FVector3> sourcePositions,
    std::span<const ayt::math::FQuaternion> sourceRotations,
    std::span<const ayt::math::FVector3> sourceScales,
    HumanoidLocalPose& output);

// Offline conversion used by both baking and editor preview. The first
// implementation intentionally accepts only override TRS tracks. Unsupported
// source data fails explicitly instead of being silently discarded.
[[nodiscard]] HumanoidRetargetResult retargetHumanoidAnimation(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition,
    const ayt::resource::IAnimation& animation,
    ayt::resource::Animation& output);

[[nodiscard]] const char* humanoidRetargetErrorName(
    HumanoidRetargetError error) noexcept;

} // namespace ayt::anim
