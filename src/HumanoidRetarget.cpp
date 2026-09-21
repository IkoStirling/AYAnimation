#include <AYAnimation/HumanoidRetarget.h>

#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsImpl/Animation.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace ayt::anim {
namespace {

using ayt::math::FQuaternion;
using ayt::math::FVector3;

constexpr float kEpsilon = 1.0e-6f;

HumanoidRetargetResult errorResult(HumanoidRetargetError error,
    std::string message, HumanoidBone role = HumanoidBone::Invalid,
    int sourceBone = -1, int targetBone = -1, int track = -1)
{
    return {error, role, sourceBone, targetBone, track, std::move(message)};
}

bool finiteQuaternion(const FQuaternion& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w)
        && value.length() >= 1.0e-4f;
}

bool roleTransfersTranslation(HumanoidBone role,
    HumanoidRetargetTranslationPolicy policy) noexcept
{
    if (policy == HumanoidRetargetTranslationPolicy::AllMappedScaled) {
        return true;
    }
    if (policy == HumanoidRetargetTranslationPolicy::None) return false;
    return role == HumanoidBone::SceneRoot
        || role == HumanoidBone::MotionRoot
        || role == HumanoidBone::Hips;
}

FQuaternion retargetRotation(const FQuaternion& sourceRest,
    const FQuaternion& targetRest,
    const HumanoidRetargetBoneCorrection& correction,
    const FQuaternion& sourceAnimated)
{
    const FQuaternion sourceOffset =
        correction.sourceReferenceOffset.normalize();
    const FQuaternion targetOffset =
        correction.targetReferenceOffset.normalize();
    const FQuaternion axis = correction.axisCorrection.normalize();
    const FQuaternion sourceReference =
        (sourceRest.normalize() * sourceOffset).normalize();
    const FQuaternion sourceCorrected =
        (sourceAnimated.normalize() * sourceOffset).normalize();
    const FQuaternion sourceDelta =
        (sourceReference.inverse() * sourceCorrected).normalize();
    const FQuaternion convertedDelta =
        (axis * sourceDelta * axis.inverse()).normalize();
    const FQuaternion targetReference =
        (targetRest.normalize() * targetOffset).normalize();
    return (targetReference * convertedDelta * targetOffset.inverse())
        .normalize();
}

FVector3 retargetScale(const FVector3& sourceRest,
    const FVector3& targetRest, const FVector3& sourceAnimated) noexcept
{
    const auto ratio = [](float animated, float rest) {
        return std::abs(rest) > kEpsilon ? animated / rest : 1.0f;
    };
    return {targetRest.x * ratio(sourceAnimated.x, sourceRest.x),
            targetRest.y * ratio(sourceAnimated.y, sourceRest.y),
            targetRest.z * ratio(sourceAnimated.z, sourceRest.z)};
}

float referenceHeight(const ayt::resource::ISkeleton& skeleton,
    const HumanoidBoneMap& mapping) noexcept
{
    const int hips = mapping.getSourceBoneIndex(HumanoidBone::Hips);
    const int head = mapping.getSourceBoneIndex(HumanoidBone::Head);
    const std::size_t count = skeleton.getBoneCount();
    if (hips < 0 || head < 0 || static_cast<std::size_t>(hips) >= count
        || static_cast<std::size_t>(head) >= count) return 0.0f;
    const FVector3* positions = skeleton.getLocalPositions();
    const FQuaternion* rotations = skeleton.getLocalRotations();
    const FVector3* scales = skeleton.getLocalScales();
    if (positions == nullptr || rotations == nullptr || scales == nullptr) {
        return 0.0f;
    }
    std::vector<ayt::math::Float4x4> world(count);
    std::vector<std::uint8_t> state(count, 0u);
    std::function<bool(int)> build = [&](int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= count) return false;
        const std::size_t i = static_cast<std::size_t>(index);
        if (state[i] == 2u) return true;
        if (state[i] == 1u) return false;
        state[i] = 1u;
        const auto local = ayt::math::Float4x4::fromTRS(
            positions[i], rotations[i], scales[i]);
        const int parent = skeleton.getParentBoneIndex(i);
        if (parent >= 0) {
            if (!build(parent)) return false;
            world[i] = world[static_cast<std::size_t>(parent)] * local;
        } else {
            world[i] = local;
        }
        state[i] = 2u;
        return true;
    };
    if (!build(hips) || !build(head)) return 0.0f;
    const FVector3 hipsPosition = world[static_cast<std::size_t>(hips)]
        .transformPoint({0, 0, 0});
    const FVector3 headPosition = world[static_cast<std::size_t>(head)]
        .transformPoint({0, 0, 0});
    return (headPosition - hipsPosition).length();
}

HumanoidBone sourceRoleForBone(const HumanoidRetargetDefinition& definition,
    int sourceBone) noexcept
{
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        if (definition.sourceMapping.getSourceBoneIndex(spec.role)
            == sourceBone) return spec.role;
    }
    return HumanoidBone::Invalid;
}

void copyTrackMetadata(const ayt::resource::IAnimation& source,
    std::uint32_t index, ayt::resource::AnimTrack& track)
{
    const char* node = source.getTrackNodeName(index);
    const char* property = source.getTrackProperty(index);
    track.nodeName = node != nullptr ? node : "";
    track.property = property != nullptr ? property : "";
    track.valueType = source.getTrackType(index);
    track.blendMode = source.getTrackBlendMode(index);
    const std::uint32_t count = source.getTrackKeyframeCount(index);
    const float* times = source.getTrackTimes(index);
    if (count > 0u && times != nullptr) track.times.assign(times, times + count);
}

} // namespace

HumanoidRetargetResult validateHumanoidRetarget(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition) noexcept
{
    const HumanoidValidationResult sourceValidation =
        validateHumanoidSkeleton(source, definition.sourceMapping);
    if (!sourceValidation) {
        return errorResult(HumanoidRetargetError::InvalidSourceMapping,
            "Source humanoid mapping is invalid.", sourceValidation.role,
            sourceValidation.sourceBoneIndex);
    }
    const HumanoidValidationResult targetValidation =
        validateHumanoidSkeleton(target, definition.targetMapping);
    if (!targetValidation) {
        return errorResult(HumanoidRetargetError::InvalidTargetMapping,
            "Target humanoid mapping is invalid.", targetValidation.role,
            -1, targetValidation.sourceBoneIndex);
    }
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const auto& correction = definition.corrections[
            static_cast<std::size_t>(spec.role)];
        if (!finiteQuaternion(correction.sourceReferenceOffset)
            || !finiteQuaternion(correction.targetReferenceOffset)
            || !finiteQuaternion(correction.axisCorrection)) {
            return errorResult(HumanoidRetargetError::InvalidCorrection,
                "Retarget correction contains a non-finite or zero quaternion.",
                spec.role);
        }
    }
    return {};
}

float humanoidRetargetTranslationScale(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition) noexcept
{
    const float sourceHeight = referenceHeight(source, definition.sourceMapping);
    const float targetHeight = referenceHeight(target, definition.targetMapping);
    if (!std::isfinite(sourceHeight) || !std::isfinite(targetHeight)
        || sourceHeight <= kEpsilon || targetHeight <= kEpsilon) return 1.0f;
    return targetHeight / sourceHeight;
}

HumanoidRetargetResult retargetHumanoidLocalPose(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition,
    std::span<const FVector3> sourcePositions,
    std::span<const FQuaternion> sourceRotations,
    std::span<const FVector3> sourceScales,
    HumanoidLocalPose& output)
{
    const HumanoidRetargetResult validated =
        validateHumanoidRetarget(source, target, definition);
    if (!validated) return validated;
    const std::size_t sourceCount = source.getBoneCount();
    const std::size_t targetCount = target.getBoneCount();
    if (sourcePositions.size() != sourceCount
        || sourceRotations.size() != sourceCount
        || sourceScales.size() != sourceCount) {
        return errorResult(HumanoidRetargetError::PoseSizeMismatch,
            "Source local pose size does not match the source skeleton.");
    }
    const FVector3* sourceRestPositions = source.getLocalPositions();
    const FQuaternion* sourceRestRotations = source.getLocalRotations();
    const FVector3* sourceRestScales = source.getLocalScales();
    const FVector3* targetRestPositions = target.getLocalPositions();
    const FQuaternion* targetRestRotations = target.getLocalRotations();
    const FVector3* targetRestScales = target.getLocalScales();
    if (sourceRestPositions == nullptr || sourceRestRotations == nullptr
        || sourceRestScales == nullptr || targetRestPositions == nullptr
        || targetRestRotations == nullptr || targetRestScales == nullptr) {
        return errorResult(HumanoidRetargetError::PoseSizeMismatch,
            "A skeleton does not expose complete local rest transforms.");
    }
    output.positions.assign(targetRestPositions,
        targetRestPositions + targetCount);
    output.rotations.assign(targetRestRotations,
        targetRestRotations + targetCount);
    output.scales.assign(targetRestScales, targetRestScales + targetCount);
    const float translationScale = humanoidRetargetTranslationScale(
        source, target, definition);
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int sourceIndex = definition.sourceMapping.getSourceBoneIndex(
            spec.role);
        const int targetIndex = definition.targetMapping.getSourceBoneIndex(
            spec.role);
        if (sourceIndex < 0 || targetIndex < 0) continue;
        const std::size_t sourceI = static_cast<std::size_t>(sourceIndex);
        const std::size_t targetI = static_cast<std::size_t>(targetIndex);
        const auto& correction = definition.corrections[
            static_cast<std::size_t>(spec.role)];
        output.rotations[targetI] = retargetRotation(
            sourceRestRotations[sourceI], targetRestRotations[targetI],
            correction, sourceRotations[sourceI]);
        output.scales[targetI] = retargetScale(sourceRestScales[sourceI],
            targetRestScales[targetI], sourceScales[sourceI]);
        if (roleTransfersTranslation(spec.role,
                definition.translationPolicy)) {
            output.positions[targetI] = targetRestPositions[targetI]
                + (sourcePositions[sourceI] - sourceRestPositions[sourceI])
                    * translationScale;
        }
    }
    return {};
}

HumanoidRetargetResult retargetHumanoidAnimation(
    const ayt::resource::ISkeleton& source,
    const ayt::resource::ISkeleton& target,
    const HumanoidRetargetDefinition& definition,
    const ayt::resource::IAnimation& animation,
    ayt::resource::Animation& output)
{
    const HumanoidRetargetResult validated =
        validateHumanoidRetarget(source, target, definition);
    if (!validated) return validated;
    ayt::resource::Animation converted;
    converted.setName(animation.getName() != nullptr ? animation.getName() : "");
    converted.setDuration(animation.getDuration());
    converted.setTicksPerSecond(animation.getTicksPerSecond());
    for (std::uint32_t notify = 0; notify < animation.getNotifyCount(); ++notify) {
        converted.addNotify({animation.getNotifyName(notify) != nullptr
                ? animation.getNotifyName(notify) : "",
            animation.getNotifyTime(notify), animation.getNotifyPayload(notify)});
    }
    const auto* targetBones = target.getBones();
    const FVector3* sourceRestPositions = source.getLocalPositions();
    const FQuaternion* sourceRestRotations = source.getLocalRotations();
    const FVector3* sourceRestScales = source.getLocalScales();
    const FVector3* targetRestPositions = target.getLocalPositions();
    const FQuaternion* targetRestRotations = target.getLocalRotations();
    const FVector3* targetRestScales = target.getLocalScales();
    const float translationScale = humanoidRetargetTranslationScale(
        source, target, definition);

    for (std::uint32_t index = 0; index < animation.getTrackCount(); ++index) {
        ayt::resource::AnimTrack track;
        copyTrackMetadata(animation, index, track);
        const std::uint32_t count = animation.getTrackKeyframeCount(index);
        if (count > 0u && animation.getTrackTimes(index) == nullptr) {
            return errorResult(HumanoidRetargetError::AnimationTrackDataMissing,
                "Animation track has no key times.", HumanoidBone::Invalid,
                -1, -1, static_cast<int>(index));
        }
        if (track.valueType == ayt::resource::AnimTrackType::Float) {
            const float* values = animation.getTrackFloatValues(index);
            if (count > 0u && values == nullptr) {
                return errorResult(
                    HumanoidRetargetError::AnimationTrackDataMissing,
                    "Float animation track has no key values.",
                    HumanoidBone::Invalid, -1, -1,
                    static_cast<int>(index));
            }
            if (count > 0u) track.values.assign(values, values + count);
            converted.addTrack(track);
            continue;
        }
        if (track.blendMode != ayt::resource::AnimBlendMode::Override) {
            return errorResult(HumanoidRetargetError::UnsupportedAdditiveTrack,
                "Additive skeletal tracks require an explicit reference-pose bake.",
                HumanoidBone::Invalid, -1, -1, static_cast<int>(index));
        }
        const int sourceIndex = source.findBone(track.nodeName.c_str());
        if (sourceIndex < 0) {
            return errorResult(
                HumanoidRetargetError::AnimationTrackBoneMissing,
                "Animation track targets a bone missing from the source skeleton.",
                HumanoidBone::Invalid, -1, -1, static_cast<int>(index));
        }
        const HumanoidBone role = sourceRoleForBone(definition, sourceIndex);
        if (role == HumanoidBone::Invalid) {
            return errorResult(HumanoidRetargetError::UnmappedAnimatedBone,
                "Animation track targets a source bone without a target role.",
                role, sourceIndex, -1, static_cast<int>(index));
        }
        const int targetIndex = definition.targetMapping.getSourceBoneIndex(role);
        if (targetIndex < 0) {
            return errorResult(HumanoidRetargetError::UnmappedAnimatedBone,
                "Animation track's humanoid role is not mapped on the target.",
                role, sourceIndex, -1, static_cast<int>(index));
        }
        track.nodeName = targetBones[targetIndex].name;
        const auto& correction = definition.corrections[
            static_cast<std::size_t>(role)];
        if (track.valueType == ayt::resource::AnimTrackType::Quaternion
            && track.property == "rotation") {
            const float* values = animation.getTrackValues(index);
            if (count > 0u && values == nullptr) {
                return errorResult(
                    HumanoidRetargetError::AnimationTrackDataMissing,
                    "Rotation animation track has no key values.", role,
                    sourceIndex, targetIndex, static_cast<int>(index));
            }
            track.values.reserve(static_cast<std::size_t>(count) * 4u);
            for (std::uint32_t key = 0; key < count; ++key) {
                const FQuaternion sourceValue(
                    values[static_cast<std::size_t>(key) * 4u],
                    values[static_cast<std::size_t>(key) * 4u + 1u],
                    values[static_cast<std::size_t>(key) * 4u + 2u],
                    values[static_cast<std::size_t>(key) * 4u + 3u]);
                if (!finiteQuaternion(sourceValue)) {
                    return errorResult(
                        HumanoidRetargetError::AnimationTrackDataMissing,
                        "Rotation animation track contains an invalid quaternion.",
                        role, sourceIndex, targetIndex,
                        static_cast<int>(index));
                }
                const FQuaternion value = retargetRotation(
                    sourceRestRotations[sourceIndex],
                    targetRestRotations[targetIndex], correction, sourceValue);
                track.values.insert(track.values.end(),
                    {value.x, value.y, value.z, value.w});
            }
        } else if (track.valueType == ayt::resource::AnimTrackType::Vector3
            && track.property == "position") {
            const float* values = animation.getTrackValues(index);
            if (count > 0u && values == nullptr) {
                return errorResult(
                    HumanoidRetargetError::AnimationTrackDataMissing,
                    "Position animation track has no key values.", role,
                    sourceIndex, targetIndex, static_cast<int>(index));
            }
            track.values.reserve(static_cast<std::size_t>(count) * 3u);
            for (std::uint32_t key = 0; key < count; ++key) {
                const FVector3 sourceValue(
                    values[static_cast<std::size_t>(key) * 3u],
                    values[static_cast<std::size_t>(key) * 3u + 1u],
                    values[static_cast<std::size_t>(key) * 3u + 2u]);
                FVector3 value = targetRestPositions[targetIndex];
                if (roleTransfersTranslation(role,
                        definition.translationPolicy)) {
                    value += (sourceValue - sourceRestPositions[sourceIndex])
                        * translationScale;
                }
                track.values.insert(track.values.end(),
                    {value.x, value.y, value.z});
            }
        } else if (track.valueType == ayt::resource::AnimTrackType::Vector3
            && track.property == "scale") {
            const float* values = animation.getTrackValues(index);
            if (count > 0u && values == nullptr) {
                return errorResult(
                    HumanoidRetargetError::AnimationTrackDataMissing,
                    "Scale animation track has no key values.", role,
                    sourceIndex, targetIndex, static_cast<int>(index));
            }
            track.values.reserve(static_cast<std::size_t>(count) * 3u);
            for (std::uint32_t key = 0; key < count; ++key) {
                const FVector3 sourceValue(
                    values[static_cast<std::size_t>(key) * 3u],
                    values[static_cast<std::size_t>(key) * 3u + 1u],
                    values[static_cast<std::size_t>(key) * 3u + 2u]);
                const FVector3 value = retargetScale(
                    sourceRestScales[sourceIndex],
                    targetRestScales[targetIndex], sourceValue);
                track.values.insert(track.values.end(),
                    {value.x, value.y, value.z});
            }
        } else {
            return errorResult(HumanoidRetargetError::UnsupportedTrack,
                "Skeletal animation track has an unsupported property/type pair.",
                role, sourceIndex, targetIndex, static_cast<int>(index));
        }
        converted.addTrack(track);
    }
    output = std::move(converted);
    return {};
}

const char* humanoidRetargetErrorName(HumanoidRetargetError error) noexcept
{
    switch (error) {
    case HumanoidRetargetError::None: return "none";
    case HumanoidRetargetError::InvalidSourceMapping: return "invalidSourceMapping";
    case HumanoidRetargetError::InvalidTargetMapping: return "invalidTargetMapping";
    case HumanoidRetargetError::InvalidCorrection: return "invalidCorrection";
    case HumanoidRetargetError::PoseSizeMismatch: return "poseSizeMismatch";
    case HumanoidRetargetError::AnimationTrackDataMissing: return "animationTrackDataMissing";
    case HumanoidRetargetError::AnimationTrackBoneMissing: return "animationTrackBoneMissing";
    case HumanoidRetargetError::UnmappedAnimatedBone: return "unmappedAnimatedBone";
    case HumanoidRetargetError::UnsupportedTrack: return "unsupportedTrack";
    case HumanoidRetargetError::UnsupportedAdditiveTrack: return "unsupportedAdditiveTrack";
    }
    return "unknown";
}

} // namespace ayt::anim
