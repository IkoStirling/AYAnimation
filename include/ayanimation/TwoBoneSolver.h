#pragma once
// TwoBoneSolver.h — Two-Bone IK analytic solver (P4-1, 2026-08-10).
//
// Pure math core: NO skeleton / animation-player dependencies. Consumes
// world-space positions + world rotations, produces new world rotations.
// The player converts world → local when writing back (see the Phase 2.5
// IK pass in AnimationPlayer::evaluate, design §4.25).
//
// Degeneracy contract (INV-74): every degenerate input — NaN, zero-length
// bone, coincident points, target at the root, unreachable target — yields
// a well-defined result: either the input rotations unchanged or the
// stretched pose. Never NaN, never a crash.
//
// v0 scope (P4-1): bend direction inherited from the CURRENT chain plane
// (no pole vector); no FABRIK / CCD / constraints (design §4.25.11).

#include <AYMath/MathTypes.h>

namespace ayt::anim
{

// Result of a two-bone solve. Both rotations are WORLD-SPACE and already
// blended by `weight`.
struct TwoBoneIKResult
{
    ayt::math::FQuaternion rootRotation;   // new world rotation of the chain root
    ayt::math::FQuaternion midRotation;    // new world rotation of the mid bone
    bool                   reachable = true; // false = target beyond len0+len1
                                             // (chain stretched straight at target)
};

class TwoBoneSolver
{
public:
    // Analytic solve — cosine law for the two joint angles + fromToRotation
    // for the rotations. Bend direction is inherited from the current chain
    // plane (cross of the two bone axes); collinear chains fall back to the
    // root's up axis, then X, then Y.
    //
    // rootPos/midPos/tipPos — current world positions (world matrix
    //                          translations; already include animation + scale).
    // rootRot/midRot        — current world rotations (decompose() of the
    //                          world matrices' rotation parts).
    // targetPos             — world-space goal for the tip.
    // weight                — [0,1]; result = slerp(current, solved, weight).
    //                          0 → inputs returned unchanged (caller skips
    //                          the call entirely at weight <= 0, INV-72);
    //                          >=1 → full solve. Out of range is clamped.
    static TwoBoneIKResult solve(
        const ayt::math::FVector3&    rootPos,
        const ayt::math::FVector3&    midPos,
        const ayt::math::FVector3&    tipPos,
        const ayt::math::FQuaternion& rootRot,
        const ayt::math::FQuaternion& midRot,
        const ayt::math::FVector3&    targetPos,
        float                         weight = 1.0f);
};

} // namespace ayt::anim
