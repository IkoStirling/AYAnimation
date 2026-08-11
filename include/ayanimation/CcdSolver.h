#pragma once
// CcdSolver.h — CCD (Cyclic Coordinate Descent) iterative IK solver
// (P4-2, 2026-08-11).
//
// Pure math core, same contract as FabrikSolver: count world joint
// positions + world rotations in, new world rotations out (chain root
// included, anchor semantics — the root's world POSITION is never moved).
// IterativeIKResult + kMaxIKChainBones are shared — defined in
// FabrikSolver.h and reused here (identical solve signature; the include
// keeps the type defined exactly once across TUs).
//
// Degeneracy contract (INV-74): identical guard set to FabrikSolver. CCD
// addition: a chain exactly collinear with the target ON the chain line
// has a zero rotation axis at every joint → no joint can bend → the
// inputs are returned unchanged, even when the target is geometrically
// reachable (reachable only reports |target-root| <= chain length —
// necessary, NOT sufficient). Documented asymmetry vs FABRIK, which CAN
// stretch such chains (design §4.26.5).
//
// v0 scope (P4-2): no constraints / joint limits / pole vector
// (design §4.26.11 — third slice).

#include "FabrikSolver.h"

namespace ayt::anim
{

class CcdSolver
{
public:
    // Iterations used for `iterations == 0`. CCD converges linearly; 10
    // passes over ≤5-segment chains are sufficient in practice. Explicitly
    // larger iteration counts (tests) pin near-exact hits.
    static constexpr uint32_t kDefaultCcdIterations = 10;

    // One pass = one sweep from the penultimate joint to the chain root,
    // each joint rotating its sub-chain to pull the tip toward the blended
    // target. `count` in [2, kMaxIKChainBones]; `weight` in [0,1] (target-
    // space interpolation, clamped); `iterations` == 0 → default, any
    // value clamped to ≤ 100.
    static IterativeIKResult solve(
        const ayt::math::FVector3*    jointPos,  // count world joint positions; [0] = chain root
        const ayt::math::FQuaternion* jointRot,  // count world rotations (one per joint);
                                                 // the tip's entry is never read
        uint32_t                     count,
        const ayt::math::FVector3&   targetPos,
        float                        weight     = 1.0f,
        uint32_t                     iterations = 0);
};

} // namespace ayt::anim
