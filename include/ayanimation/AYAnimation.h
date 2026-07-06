// AYAnimation.h — umbrella header for the AYAnimation module.
//
// AN-01 covers the minimum playback surface:
//   - Skeleton:        bone hierarchy + rest pose + inverse-bind matrices.
//   - Animation:       keyframe tracks with Vector3 / Quaternion / Float channels.
//   - KeySampler:      lerp / slerp sampling at a given time.
//   - AnimationPlayer: time control + per-frame pose evaluation (local + world + skin).
//
// Deferred to later PRs: AYResource loader wiring, GPU skinning upload, state machine,
// blend trees, IK, retargeting.

#pragma once

#include "Skeleton.h"
#include "Animation.h"
#include "AnimationPlayer.h"
#include "KeySampler.h"

namespace ayt::anim
{

constexpr int MAJOR_VERSION = 0;
constexpr int MINOR_VERSION = 1;
constexpr int PATCH_VERSION = 0;

} // namespace ayt::anim