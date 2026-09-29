#pragma once
#include <AYAnimation/HumanoidRetarget.h>
#include <AYResource/assetsDefs/ISkeleton.h>
#include <array>
#include <memory>
#include <string>

namespace ayt::anim::editor {
enum class RigLimb : unsigned { LeftArm, RightArm, LeftLeg, RightLeg, Count };
struct RigLimbControl {
  bool ik = false;
  float weight = 1;
  ayt::math::FVector3 target{}, pole{};
  ayt::math::FQuaternion tipRotation = ayt::math::FQuaternion::identity();
};
struct RigControlPose {
  HumanoidLocalPose fk;
  std::vector<bool> overrides;
  std::array<RigLimbControl, 4> limbs;
};
struct RigControlKey {
  double seconds = 0;
  RigControlPose pose;
};
struct RigControlHandle {
  HumanoidBone role;
  int bone;
  bool translation;
};

/// UI-free, owned humanoid authoring rig. Never adds bones or changes source
/// assets. Explicit semantic mappings only; missing optional roles produce no
/// handles. Coordinates are skeleton/model space, not Entity world space.
/// Positive uniform scale only; IK preserves lengths and includes a pole and
/// end rotation.
class HumanoidControlRig {
public:
  /// Bind an owned definition using explicit humanoid semantics. Missing roles
  /// are optional; invalid hierarchy/scale/mapping leaves this rig unchanged.
  bool bind(const ayt::resource::ISkeleton &, const HumanoidBoneMap &,
            std::string *error = nullptr);
  bool bound() const noexcept { return !_bones.empty(); }
  bool enabled = false;
  const std::vector<RigControlHandle> &handles() const { return _handles; }
  const RigControlPose &pose() const { return _pose; }
  const std::vector<RigControlKey> &keys() const { return _keys; }
  int limbBone(RigLimb limb, unsigned joint) const;
  bool setPose(RigControlPose pose, std::string *error = nullptr);
  bool seek(double seconds);
  bool record(double seconds, double duration);
  bool removeKey(std::size_t key);
  bool moveKeys(const std::vector<std::size_t> &keys, double delta,
                double duration);
  RigControlPose sample(double seconds) const;
  // Snap all controlled FK joints and the selected IK goal to the solved pose.
  // Switching does not pop; a subsequent target/rotation edit changes the pose.
  bool switchLimb(RigLimb, bool ik,
                  const std::vector<ayt::math::Float4x4> &displayedWorld,
                  std::string *error = nullptr);
  bool captureFK(const std::vector<ayt::math::Float4x4> &displayedWorld);
  bool evaluate(const std::vector<ayt::math::Float4x4> &baseWorld,
                HumanoidLocalPose &local,
                std::vector<ayt::math::Float4x4> &world,
                std::string *error = nullptr) const;
  // Versioned JSON value stored in existing editor metadata; not a new asset
  // extension.
  std::string encode() const;
  bool decode(const std::string &, std::string *error = nullptr);
  bool restore(const ayt::resource::ISkeleton &, const std::string &,
               std::string *error = nullptr);
  std::shared_ptr<ayt::resource::Animation>
  bake(const ayt::resource::IAnimation &, double sampleRate,
       std::string *error = nullptr) const;

private:
  bool validPose(const RigControlPose &) const;
  bool localFromWorld(const std::vector<ayt::math::Float4x4> &,
                      HumanoidLocalPose &) const;
  bool worldFromLocal(const HumanoidLocalPose &,
                      std::vector<ayt::math::Float4x4> &) const;
  std::vector<ayt::resource::Bone> _bones;
  std::vector<std::size_t> _order;
  std::vector<RigControlHandle> _handles;
  std::array<std::array<int, 3>, 4> _limbs{};
  RigControlPose _pose;
  std::vector<RigControlKey> _keys;
};
} // namespace ayt::anim::editor
