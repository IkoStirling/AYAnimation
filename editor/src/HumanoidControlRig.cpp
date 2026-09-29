#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYAnimationEditor/HumanoidControlRig.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>

namespace ayt::anim::editor {
using namespace ayt::math;
using namespace ayt::resource;
using Json = nlohmann::json;
namespace {
bool fail(std::string *error, const char *message) {
  if (error)
    *error = message;
  return false;
}
bool finite(FVector3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         v.lengthSq() < 1e20f;
}
bool rotation(FQuaternion q) {
  const double n = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z +
                   double(q.w) * q.w;
  return std::isfinite(n) && n > 1e-12 && n < 1e20;
}
bool scale(FVector3 s) {
  return finite(s) && s.x > 1e-5f && std::fabs(s.x - s.y) < 1e-5f &&
         std::fabs(s.x - s.z) < 1e-5f;
}
FVector3 mix(FVector3 a, FVector3 b, float t) { return a * (1 - t) + b * t; }
Json vec(FVector3 v) { return {v.x, v.y, v.z}; }
Json quat(FQuaternion q) { return {q.x, q.y, q.z, q.w}; }
FVector3 readVec(const Json &v) {
  if (!v.is_array() || v.size() != 3)
    throw std::runtime_error("Invalid vector.");
  return {v.at(0), v.at(1), v.at(2)};
}
FQuaternion readQuat(const Json &v) {
  if (!v.is_array() || v.size() != 4)
    throw std::runtime_error("Invalid rotation.");
  return {v.at(0), v.at(1), v.at(2), v.at(3)};
}
Json poseJson(const RigControlPose &p) {
  Json out;
  out["overrides"] = p.overrides;
  for (std::size_t i = 0; i < p.fk.positions.size(); ++i)
    out["fk"].push_back(
        {vec(p.fk.positions[i]), quat(p.fk.rotations[i]), vec(p.fk.scales[i])});
  for (const auto &limb : p.limbs)
    out["limbs"].push_back({limb.ik, limb.weight, vec(limb.target),
                            vec(limb.pole), quat(limb.tipRotation)});
  return out;
}
RigControlPose readPose(const Json &value, std::size_t count) {
  if (value.at("fk").size() != count || value.at("overrides").size() != count ||
      value.at("limbs").size() != 4)
    throw std::runtime_error("Rig pose shape mismatch.");
  RigControlPose p;
  p.overrides = value.at("overrides").get<std::vector<bool>>();
  for (const auto &joint : value.at("fk")) {
    p.fk.positions.push_back(readVec(joint.at(0)));
    p.fk.rotations.push_back(readQuat(joint.at(1)));
    p.fk.scales.push_back(readVec(joint.at(2)));
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto &v = value.at("limbs").at(i);
    p.limbs[i] = {v.at(0), v.at(1), readVec(v.at(2)), readVec(v.at(3)),
                  readQuat(v.at(4))};
  }
  return p;
}
} // namespace
int HumanoidControlRig::limbBone(RigLimb limb, unsigned joint) const {
  return unsigned(limb) < 4 && joint < 3 && bound()
             ? _limbs[unsigned(limb)][joint]
             : -1;
}
bool HumanoidControlRig::bind(const ISkeleton &skeleton,
                              const HumanoidBoneMap &mapping,
                              std::string *error) {
  const auto count = skeleton.getBoneCount();
  if (!count || count > 4096 || !skeleton.getBones())
    return fail(error, "Invalid rig skeleton.");
  HumanoidControlRig next;
  next._bones.assign(skeleton.getBones(), skeleton.getBones() + count);
  std::vector<unsigned char> visited(count);
  std::function<bool(std::size_t)> visit = [&](std::size_t i) {
    if (visited[i] == 2)
      return true;
    if (visited[i] == 1)
      return false;
    visited[i] = 1;
    const auto &b = next._bones[i];
    if (b.name.empty() || !finite(b.localPosition) ||
        !rotation(b.localRotation) || !scale(b.localScale) ||
        b.parentIndex < -1 || b.parentIndex >= int(count))
      return false;
    if (b.parentIndex >= 0 && !visit(b.parentIndex))
      return false;
    visited[i] = 2;
    next._order.push_back(i);
    return true;
  };
  std::set<std::string> names;
  for (std::size_t i = 0; i < count; ++i)
    if (!visit(i) || !names.insert(next._bones[i].name).second)
      return fail(error,
                  "Invalid hierarchy, duplicate names or non-uniform scale.");
  std::set<int> mapped;
  for (const auto &spec : getHumanoidBoneSpecs()) {
    const int bone = mapping.getSourceBoneIndex(spec.role);
    if (bone < 0)
      continue;
    if (bone >= int(count) || !mapped.insert(bone).second)
      return fail(error, "Invalid/duplicate semantic mapping.");
    // Body/roots and limb FK handles; fingers/eyes are an explicit later
    // extension.
    if (spec.role <= HumanoidBone::Head ||
        (spec.role >= HumanoidBone::LeftShoulder &&
         spec.role <= HumanoidBone::RightToes))
      next._handles.push_back(
          {spec.role, bone, spec.role <= HumanoidBone::Hips});
  }
  if (next._handles.empty())
    return fail(error, "No mapped body controls.");
  const HumanoidBone chains[4][3] = {
      {HumanoidBone::LeftUpperArm, HumanoidBone::LeftLowerArm,
       HumanoidBone::LeftHand},
      {HumanoidBone::RightUpperArm, HumanoidBone::RightLowerArm,
       HumanoidBone::RightHand},
      {HumanoidBone::LeftUpperLeg, HumanoidBone::LeftLowerLeg,
       HumanoidBone::LeftFoot},
      {HumanoidBone::RightUpperLeg, HumanoidBone::RightLowerLeg,
       HumanoidBone::RightFoot}};
  auto ancestor = [&](int a, int b) {
    for (int p = next._bones[b].parentIndex; p >= 0;
         p = next._bones[p].parentIndex)
      if (p == a)
        return true;
    return false;
  };
  for (unsigned c = 0; c < 4; ++c) {
    auto &chain = next._limbs[c];
    for (unsigned j = 0; j < 3; ++j)
      chain[j] = mapping.getSourceBoneIndex(chains[c][j]);
    if (std::any_of(chain.begin(), chain.end(), [](int i) { return i < 0; }))
      chain = {-1, -1, -1};
    else if (!ancestor(chain[0], chain[1]) || !ancestor(chain[1], chain[2]))
      return fail(error, "Invalid limb hierarchy.");
  }
  for (const auto &b : next._bones) {
    next._pose.fk.positions.push_back(b.localPosition);
    next._pose.fk.rotations.push_back(b.localRotation.normalize());
    next._pose.fk.scales.push_back(b.localScale);
  }
  next._pose.overrides.resize(count, false);
  std::vector<Float4x4> world;
  if (!next.worldFromLocal(next._pose.fk, world))
    return fail(error, "Invalid bind pose.");
  for (unsigned c = 0; c < 4; ++c)
    if (next._limbs[c][0] >= 0) {
      const auto &chain = next._limbs[c];
      auto &ctl = next._pose.limbs[c];
      ctl.target = world[chain[2]].transformPoint({});
      ctl.pole = world[chain[1]].transformPoint({}) + FVector3{0, 0, 1};
      FVector3 p, s;
      world[chain[2]].decompose(p, ctl.tipRotation, s);
    }
  *this = std::move(next);
  if (error)
    error->clear();
  return true;
}
bool HumanoidControlRig::validPose(const RigControlPose &p) const {
  const auto n = _bones.size();
  if (!n || p.fk.positions.size() != n || p.fk.rotations.size() != n ||
      p.fk.scales.size() != n || p.overrides.size() != n)
    return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!finite(p.fk.positions[i]) || !rotation(p.fk.rotations[i]) ||
        !scale(p.fk.scales[i]))
      return false;
  for (unsigned i = 0; i < 4; ++i) {
    const auto &c = p.limbs[i];
    if (!finite(c.target) || !finite(c.pole) || !rotation(c.tipRotation) ||
        !std::isfinite(c.weight) || c.weight < 0 || c.weight > 1 ||
        (c.ik && _limbs[i][0] < 0))
      return false;
  }
  return true;
}
bool HumanoidControlRig::setPose(RigControlPose pose, std::string *error) {
  if (!validPose(pose))
    return fail(error, "Invalid rig control pose.");
  for (auto &q : pose.fk.rotations)
    q = q.normalize();
  for (auto &c : pose.limbs)
    c.tipRotation = c.tipRotation.normalize();
  _pose = std::move(pose);
  if (error)
    error->clear();
  return true;
}
bool HumanoidControlRig::worldFromLocal(const HumanoidLocalPose &local,
                                        std::vector<Float4x4> &world) const {
  if (local.positions.size() != _bones.size() ||
      local.rotations.size() != _bones.size() ||
      local.scales.size() != _bones.size())
    return false;
  world.resize(_bones.size());
  for (auto i : _order) {
    if (!finite(local.positions[i]) || !rotation(local.rotations[i]) ||
        !scale(local.scales[i]))
      return false;
    const auto m = Float4x4::fromTRS(
        local.positions[i], local.rotations[i].normalize(), local.scales[i]);
    const int parent = _bones[i].parentIndex;
    world[i] = parent < 0 ? m : world[parent] * m;
    if (!finite(world[i].transformPoint({})))
      return false;
  }
  return true;
}
bool HumanoidControlRig::localFromWorld(const std::vector<Float4x4> &world,
                                        HumanoidLocalPose &local) const {
  if (world.size() != _bones.size())
    return false;
  const auto n = world.size();
  local.positions.resize(n);
  local.rotations.resize(n);
  local.scales.resize(n);
  for (auto i : _order) {
    const int p = _bones[i].parentIndex;
    const auto m = p < 0 ? world[i] : world[p].inverse() * world[i];
    if (!m.decompose(local.positions[i], local.rotations[i], local.scales[i]) ||
        !finite(local.positions[i]) || !rotation(local.rotations[i]) ||
        !scale(local.scales[i]))
      return false;
  }
  return true;
}
bool HumanoidControlRig::evaluate(const std::vector<Float4x4> &baseWorld,
                                  HumanoidLocalPose &local,
                                  std::vector<Float4x4> &world,
                                  std::string *error) const {
  HumanoidLocalPose result;
  std::vector<Float4x4> solved;
  if (!validPose(_pose) || !localFromWorld(baseWorld, result))
    return fail(error, "Rig base pose is invalid or has unsupported scale.");
  if (enabled) {
    for (const auto &handle : _handles)
      if (_pose.overrides[handle.bone]) {
        result.rotations[handle.bone] = _pose.fk.rotations[handle.bone];
        if (handle.translation)
          result.positions[handle.bone] = _pose.fk.positions[handle.bone];
      }
    if (!worldFromLocal(result, solved))
      return fail(error, "Invalid FK result.");
    const auto worldRotation = [&](int i) {
      FVector3 p, s;
      FQuaternion q;
      solved[i].decompose(p, q, s);
      return q.normalize();
    };
    const auto setWorldRotation = [&](int i, FQuaternion q) {
      const int parent = _bones[i].parentIndex;
      result.rotations[i] = ((parent < 0 ? FQuaternion::identity()
                                         : worldRotation(parent).inverse()) *
                             q)
                                .normalize();
      return worldFromLocal(result, solved);
    };
    for (unsigned c = 0; c < 4; ++c) {
      const auto &ctl = _pose.limbs[c];
      const auto &chain = _limbs[c];
      if (!ctl.ik || ctl.weight == 0 || chain[0] < 0)
        continue;
      const int a = chain[0], b = chain[1], tip = chain[2];
      const auto A = solved[a].transformPoint({}),
                 B = solved[b].transformPoint({}),
                 C = solved[tip].transformPoint({});
      const float l0 = (B - A).length(), l1 = (C - B).length();
      auto direction = ctl.target - A;
      const float d = direction.length();
      if (l0 < 1e-5f || l1 < 1e-5f || d < 1e-5f)
        continue; // Deterministic zero-length/root-target fallback.
      direction = direction / d;
      auto bend = ctl.pole - A;
      bend -= direction * bend.dot(direction);
      if (bend.lengthSq() < 1e-10f) {
        bend = B - A;
        bend -= direction * bend.dot(direction);
      }
      if (bend.lengthSq() < 1e-10f) {
        bend = direction.cross({0, 1, 0});
        if (bend.lengthSq() < 1e-10f)
          bend = direction.cross({1, 0, 0});
      }
      bend = bend.normalize();
      const float distance =
          std::clamp(d, std::max(std::fabs(l0 - l1), 1e-5f), l0 + l1);
      const float x =
          (l0 * l0 - l1 * l1 + distance * distance) / (2 * distance);
      const float height = std::sqrt(std::max(0.f, l0 * l0 - x * x));
      const auto desiredMid = A + direction * x + bend * height;
      const auto desiredTip = A + direction * distance;
      const auto rootQ =
          FQuaternion::fromToRotation(B - A, desiredMid - A) * worldRotation(a);
      const auto midQ =
          FQuaternion::fromToRotation(C - B, desiredTip - desiredMid) *
          worldRotation(b);
      const auto oldA = result.rotations[a], oldB = result.rotations[b],
                 oldTip = result.rotations[tip];
      if (!setWorldRotation(a, rootQ) || !setWorldRotation(b, midQ) ||
          !setWorldRotation(tip, ctl.tipRotation))
        return fail(error, "Invalid IK result.");
      result.rotations[a] =
          oldA.slerp(result.rotations[a], ctl.weight).normalize();
      result.rotations[b] =
          oldB.slerp(result.rotations[b], ctl.weight).normalize();
      result.rotations[tip] =
          oldTip.slerp(result.rotations[tip], ctl.weight).normalize();
      if (!worldFromLocal(result, solved))
        return fail(error, "Invalid blended IK result.");
    }
  }
  if (!worldFromLocal(result, solved))
    return fail(error, "Invalid solved rig pose.");
  local = std::move(result);
  world = std::move(solved);
  if (error)
    error->clear();
  return true;
}
bool HumanoidControlRig::switchLimb(RigLimb limb, bool ik,
                                    const std::vector<Float4x4> &displayed,
                                    std::string *error) {
  if (unsigned(limb) >= 4 || limbBone(limb, 0) < 0)
    return fail(error, "Limb controls are unavailable.");
  HumanoidLocalPose local;
  if (!localFromWorld(displayed, local))
    return fail(error, "Cannot snap this pose.");
  auto next = _pose;
  for (const auto &h : _handles) {
    next.fk.rotations[h.bone] = local.rotations[h.bone];
    next.fk.positions[h.bone] = local.positions[h.bone];
    next.overrides[h.bone] = true;
  }
  const auto &chain = _limbs[unsigned(limb)];
  auto &ctl = next.limbs[unsigned(limb)];
  const auto A = displayed[chain[0]].transformPoint({}),
             B = displayed[chain[1]].transformPoint({}),
             C = displayed[chain[2]].transformPoint({});
  auto axis = C - A;
  if (axis.lengthSq() > 1e-10f)
    axis = axis.normalize();
  auto bend = B - A - axis * (B - A).dot(axis);
  if (bend.lengthSq() < 1e-10f) {
    bend = axis.cross({0, 1, 0});
    if (bend.lengthSq() < 1e-10f)
      bend = {0, 0, 1};
  }
  ctl.target = C;
  ctl.pole = B + bend.normalize() * std::max((C - A).length(), .1f);
  FVector3 p, s;
  displayed[chain[2]].decompose(p, ctl.tipRotation, s);
  ctl.ik = ik;
  ctl.weight = 1;
  return setPose(std::move(next), error);
}
bool HumanoidControlRig::captureFK(const std::vector<Float4x4> &displayed) {
  HumanoidLocalPose local;
  if (!localFromWorld(displayed, local))
    return false;
  auto next = _pose;
  for (const auto &h : _handles) {
    next.fk.positions[h.bone] = local.positions[h.bone];
    next.fk.rotations[h.bone] = local.rotations[h.bone];
    next.overrides[h.bone] = true;
  }
  return setPose(std::move(next));
}
RigControlPose HumanoidControlRig::sample(double seconds) const {
  if (_keys.empty() || !std::isfinite(seconds))
    return _pose;
  const auto right = std::upper_bound(
      _keys.begin(), _keys.end(), seconds,
      [](double t, const auto &key) { return t < key.seconds; });
  if (right == _keys.begin())
    return right->pose;
  if (right == _keys.end())
    return _keys.back().pose;
  const auto &a = *(right - 1);
  const auto &b = *right;
  const float t = float((seconds - a.seconds) / (b.seconds - a.seconds));
  auto p = a.pose;
  for (std::size_t i = 0; i < p.fk.positions.size(); ++i) {
    p.fk.positions[i] = mix(a.pose.fk.positions[i], b.pose.fk.positions[i], t);
    p.fk.rotations[i] =
        a.pose.fk.rotations[i].slerp(b.pose.fk.rotations[i], t).normalize();
  }
  for (unsigned i = 0; i < 4; ++i) {
    auto &c = p.limbs[i];
    const auto &x = a.pose.limbs[i];
    const auto &y = b.pose.limbs[i];
    c.target = mix(x.target, y.target, t);
    c.pole = mix(x.pole, y.pole, t);
    c.weight = x.weight * (1 - t) + y.weight * t;
    c.tipRotation = x.tipRotation.slerp(y.tipRotation, t).normalize();
  }
  return p; // Mode and override switches are stepped, not scalar interpolated.
}
bool HumanoidControlRig::seek(double seconds) {
  return std::isfinite(seconds) && setPose(sample(seconds));
}
bool HumanoidControlRig::record(double seconds, double duration) {
  if (!std::isfinite(seconds) || !std::isfinite(duration) || seconds < 0 ||
      seconds > duration || !validPose(_pose) || _keys.size() >= 10000)
    return false;
  auto i =
      std::lower_bound(_keys.begin(), _keys.end(), seconds,
                       [](const auto &k, double t) { return k.seconds < t; });
  if (i != _keys.end() && std::fabs(i->seconds - seconds) < 1e-6)
    i->pose = _pose;
  else if (i != _keys.begin() && std::fabs((i - 1)->seconds - seconds) < 1e-6)
    (i - 1)->pose = _pose;
  else
    _keys.insert(i, {seconds, _pose});
  return true;
}
bool HumanoidControlRig::removeKey(std::size_t i) {
  if (i >= _keys.size())
    return false;
  _keys.erase(_keys.begin() + i);
  return true;
}
bool HumanoidControlRig::moveKeys(const std::vector<std::size_t> &ids,
                                  double delta, double duration) {
  if (ids.empty() || !std::isfinite(delta) || !std::isfinite(duration))
    return false;
  std::set<std::size_t> unique(ids.begin(), ids.end());
  if (unique.size() != ids.size() || *unique.rbegin() >= _keys.size())
    return false;
  auto keys = _keys;
  for (auto i : ids) {
    keys[i].seconds += delta;
    if (keys[i].seconds < 0 || keys[i].seconds > duration)
      return false;
  }
  std::sort(keys.begin(), keys.end(),
            [](auto &a, auto &b) { return a.seconds < b.seconds; });
  for (std::size_t i = 1; i < keys.size(); ++i)
    if (keys[i].seconds - keys[i - 1].seconds < 1e-6)
      return false;
  _keys = std::move(keys);
  return true;
}
std::string HumanoidControlRig::encode() const {
  if (!bound())
    return "null";
  Json root = {{"version", 1}, {"enabled", enabled}, {"pose", poseJson(_pose)}};
  for (const auto &b : _bones)
    root["binding"].push_back({b.name, b.parentIndex, vec(b.localPosition),
                               quat(b.localRotation), vec(b.localScale)});
  for (const auto &h : _handles)
    root["handles"].push_back({unsigned(h.role), h.bone, h.translation});
  root["limbs"] = _limbs;
  root["keys"] = Json::array();
  for (const auto &k : _keys)
    root["keys"].push_back({k.seconds, poseJson(k.pose)});
  return root.dump();
}
bool HumanoidControlRig::decode(const std::string &text, std::string *error) {
  try {
    if (text.size() > 32 * 1024 * 1024)
      return fail(error, "Rig metadata too large.");
    const auto root = Json::parse(text);
    const auto binding = Json::parse(encode());
    if (root.at("version") != 1 ||
        root.at("binding") != binding.at("binding") ||
        root.at("handles") != binding.at("handles") ||
        root.at("limbs") != binding.at("limbs") ||
        root.at("keys").size() > 10000)
      return fail(error, "Rig binding/schema mismatch.");
    auto next = *this;
    next.enabled = root.at("enabled");
    if (!next.setPose(readPose(root.at("pose"), _bones.size()), error))
      return false;
    next._keys.clear();
    double last = -1;
    for (const auto &key : root.at("keys")) {
      const double t = key.at(0);
      auto p = readPose(key.at(1), _bones.size());
      if (!std::isfinite(t) || t < 0 || (last >= 0 && t - last < 1e-6) ||
          !next.validPose(p))
        return fail(error, "Invalid rig keys.");
      next._keys.push_back({t, std::move(p)});
      last = t;
    }
    *this = std::move(next);
    if (error)
      error->clear();
    return true;
  } catch (...) {
    return fail(error, "Invalid rig metadata.");
  }
}
std::shared_ptr<Animation> HumanoidControlRig::bake(const IAnimation &source,
                                                    double rate,
                                                    std::string *error) const {
  const double duration = source.getDuration();
  if (!enabled || !bound() || !std::isfinite(rate) || rate < 1 || rate > 240 ||
      !std::isfinite(duration) || duration < 0 || duration * rate > 10000 ||
      !std::isfinite(source.getTicksPerSecond()) ||
      source.getTicksPerSecond() <= 0) {
    fail(error, "Invalid rig bake rate/duration.");
    return {};
  }
  if (!_keys.empty() && _keys.back().seconds > duration) {
    fail(error, "Rig keys exceed the clip duration.");
    return {};
  }
  auto output = copyAnimationForAuthoring(source);
  const double timeTolerance = 1e-6 * std::max(1.0, duration);
  std::set<std::pair<std::string, std::string>> tracks;
  for (const auto &t : output.tracks) {
    const unsigned width = t.valueType == AnimTrackType::Quaternion ? 4
                           : t.valueType == AnimTrackType::Vector3  ? 3
                           : t.valueType == AnimTrackType::Float    ? 1
                                                                    : 0;
    const auto count = t.times.size() * width;
    const auto finiteValues = [](const auto &values) {
      return std::all_of(values.begin(), values.end(),
                         [](float v) { return std::isfinite(v); });
    };
    if (!width || count != t.values.size() ||
        (!t.inTangents.empty() && t.inTangents.size() != count) ||
        (!t.outTangents.empty() && t.outTangents.size() != count) ||
        !finiteValues(t.times) || !finiteValues(t.values) ||
        !finiteValues(t.inTangents) || !finiteValues(t.outTangents)) {
      fail(error, "Invalid rig bake track data.");
      return {};
    }
    for (std::size_t i = 0; i < t.times.size(); ++i)
      if (t.times[i] < 0 || t.times[i] / output.ticksPerSecond > duration + timeTolerance ||
          (i && t.times[i] <= t.times[i - 1])) {
        fail(error, "Invalid rig bake track times.");
        return {};
      }
    const auto bone =
        std::find_if(_bones.begin(), _bones.end(),
                     [&](const auto &b) { return b.name == t.nodeName; });
    if (t.blendMode != AnimBlendMode::Override ||
        (bone != _bones.end() &&
         (!tracks.emplace(t.nodeName, t.property).second ||
          (t.property != "position" && t.property != "rotation" &&
           t.property != "scale") ||
          (t.property == "rotation" ? width != 4 : width != 3)))) {
      fail(error, "Rig bake rejects additive, duplicate or unsupported "
                  "skeletal tracks.");
      return {};
    }
  }
  auto skeleton = std::make_shared<Skeleton>();
  for (const auto &b : _bones)
    skeleton->addBone(b);
  AnimationPlayer player;
  player.setSkeleton(skeleton);
  player.play(&source);
  player.setLoop(false); // Bake the endpoint, not the wrapped first frame.
  player.pause();
  // Replace only controlled TRS tracks; unrelated tracks/Notify/metadata
  // survive.
  std::set<int> affected;
  for (const auto &h : _handles)
    affected.insert(h.bone);
  output.tracks.erase(std::remove_if(output.tracks.begin(), output.tracks.end(),
                                     [&](const auto &t) {
                                       for (auto i : affected)
                                         if (_bones[i].name == t.nodeName &&
                                             (t.property == "position" ||
                                              t.property == "rotation" ||
                                              t.property == "scale"))
                                           return true;
                                       return false;
                                     }),
                      output.tracks.end());
  std::vector<AnimTrack> baked;
  for (int i : affected)
    for (const auto *property : {"position", "rotation", "scale"}) {
      AnimTrack t;
      t.nodeName = _bones[i].name;
      t.property = property;
      t.valueType = std::string(property) == "rotation"
                        ? AnimTrackType::Quaternion
                        : AnimTrackType::Vector3;
      t.interpolation = AnimInterpolation::Linear;
      baked.push_back(std::move(t));
    }
  const unsigned frames = unsigned(std::ceil(duration * rate));
  auto rig = *this;
  for (unsigned frame = 0; frame <= frames; ++frame) {
    const double time = std::min(duration, frame / rate);
    player.setTime(float(time));
    player.evaluate();
    const auto *matrices = player.getBoneWorldMatrices();
    if (!matrices || player.getBoneCount() != _bones.size()) {
      fail(error, "Rig bake base sampling failed.");
      return {};
    }
    std::vector<Float4x4> base(matrices, matrices + _bones.size()), world;
    HumanoidLocalPose local;
    if (!rig.setPose(sample(time), error) ||
        !rig.evaluate(base, local, world, error))
      return {};
    std::size_t index = 0;
    for (int bone : affected)
      for (unsigned channel = 0; channel < 3; ++channel) {
        auto &track = baked[index++];
        track.times.push_back(float(time * output.ticksPerSecond));
        if (channel == 1) {
          const auto q = local.rotations[bone];
          track.values.insert(track.values.end(), {q.x, q.y, q.z, q.w});
        } else {
          const auto v =
              channel == 0 ? local.positions[bone] : local.scales[bone];
          track.values.insert(track.values.end(), {v.x, v.y, v.z});
        }
      }
  }
  output.tracks.insert(output.tracks.end(), baked.begin(), baked.end());
  if (error)
    error->clear();
  return buildAuthoredAnimation(output);
}
bool HumanoidControlRig::restore(const ISkeleton &skeleton,
                                 const std::string &text, std::string *error) {
  try {
    if (text.size() > 32 * 1024 * 1024)
      return fail(error, "Rig metadata too large.");
    const auto root = Json::parse(text);
    HumanoidBoneMap mapping;
    if (!root.at("handles").is_array() ||
        root.at("handles").size() > kHumanoidBoneCount)
      return fail(error, "Invalid rig handles.");
    for (const auto &h : root.at("handles")) {
      const auto role = h.at(0).get<unsigned>();
      const int index = h.at(1);
      if (role >= kHumanoidBoneCount ||
          !mapping.bind(HumanoidBone(role), index))
        return fail(error, "Invalid persisted role.");
    }
    HumanoidControlRig next;
    if (!next.bind(skeleton, mapping, error) || !next.decode(text, error))
      return false;
    *this = std::move(next);
    return true;
  } catch (...) {
    return fail(error, "Invalid persisted rig.");
  }
}
} // namespace ayt::anim::editor
