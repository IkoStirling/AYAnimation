#pragma once
#include <AYMathTypes.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::anim
{

// ===== Bone — single skeleton bone =====
struct Bone {
    std::string            name;
    int                    parentIndex = -1;          // -1 = root
    ayt::math::Float4x4    inverseBindMatrix;         // bone → mesh-local space
};

// ===== Skeleton — bone hierarchy with parallel local TRS arrays =====
//
// Parallel array layout matches AYResource's ISkeleton contract (see
// AYRuntime/AYResource/include/assetsDefs/IAYSkeleton.h), so a future loader PR
// can wrap a resource::ISkeleton without re-layout.
class Skeleton {
public:
    void addBone(const Bone& bone);

    size_t getBoneCount() const { return _bones.size(); }
    const Bone* getBones() const { return _bones.data(); }

    int findBone(const char* name) const;

    // Copy in rest pose from external arrays (parallel to _bones index range).
    // Pointers must remain valid for exactly nBones() entries at call time.
    void setRestPoses(const ayt::math::FVector3*    positions,
                      const ayt::math::FQuaternion* rotations,
                      const ayt::math::FVector3*    scales);

    const ayt::math::FVector3*    getLocalPositions() const { return _localPos.data(); }
    const ayt::math::FQuaternion* getLocalRotations() const { return _localRot.data(); }
    const ayt::math::FVector3*    getLocalScales()    const { return _localScl.data(); }

private:
    std::vector<Bone>                _bones;
    std::vector<ayt::math::FVector3>    _localPos;
    std::vector<ayt::math::FQuaternion> _localRot;
    std::vector<ayt::math::FVector3>    _localScl;
    std::unordered_map<std::string, size_t> _nameIndex;
};

} // namespace ayt::anim