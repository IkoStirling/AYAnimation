// AYAnimation.h — umbrella header for the AYAnimation module.
//
// P0 (2026-07-26): AYResource/assetsImpl/Animation.h deleted. AnimationPlayer
// consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly.
// KeySampler remains a free-function layer on raw float arrays
// (Vec3/Quat/Float interpolation with dot<0 slerp shortest-arc selection).

#pragma once

#include "AYAnimation/AnimationPlayer.h"
#include "AYAnimation/AnimNotifyEvent.h"
#include "AYAnimation/KeySampler.h"
#include "AYAnimation/TwoBoneSolver.h"   // P4-1 (2026-08-10) — two-bone IK analytic core
#include "AYAnimation/FabrikSolver.h"    // P4-2 (2026-08-11) — FABRIK iterative IK core (multi-joint)
#include "AYAnimation/CcdSolver.h"       // P4-2 (2026-08-11) — CCD iterative IK core (multi-joint)

namespace ayt::anim
{

constexpr int MAJOR_VERSION = 0;
constexpr int MINOR_VERSION = 2;
constexpr int PATCH_VERSION = 2;  // Phase 1.5: Anim Notify dispatch + EventBus bridge

} // namespace ayt::anim