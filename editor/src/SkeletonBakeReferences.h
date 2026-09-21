#pragma once

#include <AYAnimation/HumanoidRetarget.h>
#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <AYMath/MathTypes.h>

#include <string>
#include <vector>

namespace ayt::resource {
class Skeleton;
}

namespace ayt::anim::editor {

struct SkeletonBakeReferenceContext {
    const ayt::resource::Skeleton& sourceSkeleton;
    const ayt::resource::Skeleton* targetSkeleton = nullptr;
    const std::vector<SkeletonBakeBoneOperation>& boneOperations;
    const HumanoidRetargetDefinition* retargetDefinition = nullptr;
    bool retarget = false;
};

bool rewriteMeshDependency(
    const std::string& sourcePath,
    const SkeletonBakeReferenceContext& context,
    std::vector<ayt::math::UInt8>& output,
    std::string& error);

bool rewriteSkeletonMaskDependency(
    const std::string& sourcePath,
    const SkeletonBakeReferenceContext& context,
    std::vector<ayt::math::UInt8>& output,
    std::string& error);

} // namespace ayt::anim::editor
