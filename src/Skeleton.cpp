#include <ayanimation/Skeleton.h>

namespace ayt::anim
{

void Skeleton::addBone(const Bone& bone)
{
    const size_t idx = _bones.size();
    _bones.push_back(bone);

    // Default rest pose: identity translation, identity rotation, unit scale.
    _localPos.push_back(ayt::math::FVector3(0.0f, 0.0f, 0.0f));
    _localRot.push_back(ayt::math::FQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
    _localScl.push_back(ayt::math::FVector3(1.0f, 1.0f, 1.0f));

    _nameIndex[bone.name] = idx;
}

int Skeleton::findBone(const char* name) const
{
    if (name == nullptr) {
        return -1;
    }
    const auto it = _nameIndex.find(name);
    if (it == _nameIndex.end()) {
        return -1;
    }
    return static_cast<int>(it->second);
}

void Skeleton::setRestPoses(const ayt::math::FVector3*    positions,
                            const ayt::math::FQuaternion* rotations,
                            const ayt::math::FVector3*    scales)
{
    const size_t n = _bones.size();
    _localPos.assign(positions,    positions    + n);
    _localRot.assign(rotations,    rotations    + n);
    _localScl.assign(scales,       scales       + n);
}

} // namespace ayt::anim