#pragma once
// FabrikSolver.h — FABRIK iterative IK solver (P4-2, 2026-08-11).
//
// Pure math core: NO skeleton / animation-player dependencies. Consumes
// count world-space joint positions + world rotations, produces new world
// rotations (chain root included — its rotation follows the first segment's
// new direction; the root's world POSITION is never moved: anchor semantics,
// same as TwoBoneSolver). The player converts world → local when writing
// back (see AnimationPlayer::applyIKChain, design §4.26).
//
// Degeneracy contract (INV-74): NaN input, count < 2, weight <= 0, blended
// target at the root → inputs returned unchanged (bit-exact); zero-length
// segment → that joint's rotation left unchanged while the rest of the
// chain still solves. Never NaN, never a crash.
//
// Weight semantics (documented difference vs TwoBoneSolver, design §4.26.5):
// weight interpolates the TARGET POINT (targetEff = lerp(currentTip,
// target, w)), NOT the output rotations — per-joint slerp breaks iterative
// convergence, so the blend happens in goal space (industry standard).
// At w < 1 the tip lands exactly on the interpolated point (when
// reachable), whereas TwoBone leaves it short (rotation-space blend,
// design §4.25.5). w <= 0 → inputs returned unchanged; the player's
// weight <= 0 skip (INV-72) is the first, zero-cost gate.
//
// v0 scope (P4-2): no constraints / joint limits / pole vector
// (design §4.26.11 — third slice).

#include <AYMath/MathTypes.h>
#include <cstdint>

namespace ayt::anim
{

// Hard cap on chain joint count — fixed arrays, zero per-frame allocation.
// A chain longer than this (root→tip parent walk) is disabled at resolve.
constexpr uint32_t kMaxIKChainBones = 32;

// Result of an iterative N-joint solve. worldRot[i] = NEW world rotation of
// chain joint i (index 0 = root); worldRot[count-1] is the input tip
// rotation copied through (the solver never rotates the tip bone itself).
// Entries [count, kMaxIKChainBones) are unspecified. All finite (INV-74).
struct IterativeIKResult
{
    ayt::math::FQuaternion worldRot[kMaxIKChainBones];
    bool                   reachable = true; // static geometric diagnostic:
                                             // |target-root| <= chain length
                                             // (necessary, NOT sufficient —
                                             // CCD cannot bend an exactly
                                             // collinear chain, see CcdSolver.h)
};

class FabrikSolver
{
public:
    // Iterations used for `iterations == 0`. FABRIK converges superlinearly
    // — 3 passes put the tip within ~1% of a reachable target; 4 leaves
    // margin (plus the kConvergeEps early-out).
    static constexpr uint32_t kDefaultFabrikIterations = 4;

    // Forward-and-backward reaching iteration (see FabrikSolver.cpp for the
    // exact pseudocode). `count` in [2, kMaxIKChainBones]; `weight` in [0,1]
    // (target-space interpolation, clamped); `iterations` == 0 → default,
    // any value clamped to ≤ 100.
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
