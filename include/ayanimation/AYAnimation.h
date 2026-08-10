// AYAnimation.h — umbrella header for the AYAnimation module.
//
// P0 (2026-07-26): Skeleton.h + Animation.h deleted. AnimationPlayer
// consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly.
// KeySampler remains a free-function layer on raw float arrays
// (Vec3/Quat/Float interpolation with dot<0 slerp shortest-arc selection).

#pragma once

#include "AnimationPlayer.h"
#include "AnimNotifyEvent.h"
#include "KeySampler.h"
#include "TwoBoneSolver.h"   // P4-1 (2026-08-10) — two-bone IK analytic core

namespace ayt::anim
{

constexpr int MAJOR_VERSION = 0;
constexpr int MINOR_VERSION = 2;
constexpr int PATCH_VERSION = 2;  // Phase 1.5: Anim Notify dispatch + EventBus bridge

} // namespace ayt::anim