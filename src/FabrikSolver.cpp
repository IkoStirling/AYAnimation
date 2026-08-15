// FabrikSolver.cpp — FABRIK iterative IK solve (P4-2, design §4.26.5).
//
// Forward-and-backward reaching: the BACKWARD pass anchors the tip at the
// (weight-blended) target and lays each segment back onto its original
// length, root-ward; the FORWARD pass re-anchors the root at its input
// position and pushes the chain back out. Segment lengths are preserved
// exactly (each pass scales the direction to the stored length), so the
// chain converges to a valid pose automatically. Rotations are derived
// afterwards: each bone's new world rotation maps its ORIGINAL axis onto
// its new axis (fromToRotation premultiplied, preserving roll).
//
// Weight is a TARGET-POINT blend (targetEff = lerp(tip, target, w)), NOT a
// rotation-space slerp — per-joint slerp breaks iterative convergence
// (header + design §4.26.5). At w < 1 the tip lands exactly on the
// interpolated point (when reachable). Bit-exact identity at w <= 0 is
// guaranteed by the guard below (the player-side weight <= 0 skip is
// INV-72 — the first, zero-cost gate).

#include "AYAnimation/FabrikSolver.h"

#include <cmath>

namespace ayt::anim
{

namespace
{

constexpr float      kEps = 1e-5f;          // geometric guards (mirrors TwoBone)
constexpr float      kConvergeEps = 1e-3f;  // iteration early-out
constexpr uint32_t   kMaxIterations = 100;  // hard clamp on iterations

bool isFiniteVec(const ayt::math::FVector3& v)
{
    // IEEE-754 self-inequality — NaN is the only value unequal to itself.
    return (v.x == v.x) && (v.y == v.y) && (v.z == v.z);
}

float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

} // namespace

IterativeIKResult FabrikSolver::solve(
    const ayt::math::FVector3*    jointPos,
    const ayt::math::FQuaternion* jointRot,
    uint32_t                     count,
    const ayt::math::FVector3&   targetPos,
    float                        weight,
    uint32_t                     iterations)
{
    using ayt::math::FVector3;
    using ayt::math::FQuaternion;

    IterativeIKResult out;
    for (uint32_t i = 0; i < count; ++i) {
        out.worldRot[i] = jointRot[i];   // copy-through default
    }
    out.reachable = true;

    // --- Guard set (INV-74): every degenerate input returns the inputs
    // bit-exact. ---
    if (count < 2)       return out;
    if (weight <= 0.0f)  return out;
    if (!isFiniteVec(targetPos)) return out;
    for (uint32_t i = 0; i < count; ++i) {
        if (!isFiniteVec(jointPos[i])) return out;
    }

    const float w = clampf(weight, 0.0f, 1.0f);

    // Weight blend in GOAL space: interpolate the target point itself.
    const FVector3 tip = jointPos[count - 1];
    const FVector3 targetEff = tip + (targetPos - tip) * w;

    // Static reachability diagnostic (does not change the solve — the
    // backward pass anchors on targetEff regardless; an unreachable target
    // just lays the chain out straight toward it).
    float totalLen = 0.0f;
    for (uint32_t i = 0; i + 1 < count; ++i) {
        totalLen += (jointPos[i + 1] - jointPos[i]).length();
    }
    const float dRoot = (targetEff - jointPos[0]).length();
    if (dRoot < kEps) return out;        // blended target at the root — no direction
    out.reachable = (dRoot <= totalLen + kEps);

    // Working copy + original segment lengths (preserved exactly by the
    // iteration; the root anchor is locked).
    FVector3 p[kMaxIKChainBones];
    float    segLen[kMaxIKChainBones];
    for (uint32_t i = 0; i < count; ++i) p[i] = jointPos[i];
    for (uint32_t i = 0; i + 1 < count; ++i) {
        segLen[i] = (jointPos[i + 1] - jointPos[i]).length();
    }

    if (iterations == 0) iterations = kDefaultFabrikIterations;
    iterations = (iterations > kMaxIterations) ? kMaxIterations : iterations;

    const FVector3 rootAnchor = p[0];    // locked — the root never moves

    // --- Iteration ---
    for (uint32_t it = 0; it < iterations; ++it) {
        // BACKWARD pass: anchor the tip at the target, lay the chain back
        // root-ward at original segment lengths. (This pass DOES move the
        // root — the forward pass locks it back.)
        p[count - 1] = targetEff;
        for (int32_t i = static_cast<int32_t>(count) - 2; i >= 0; --i) {
            const FVector3 dir = p[i] - p[i + 1];
            const float    len = dir.length();
            if (len < kEps) continue;    // zero-length segment — leave joint
            p[i] = p[i + 1] + (dir / len) * segLen[i];
        }
        // FORWARD pass: re-anchor the root at its input position (the
        // backward pass moved it), then push the chain back out.
        p[0] = rootAnchor;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            const FVector3 dir = p[i + 1] - p[i];
            const float    len = dir.length();
            if (len < kEps) continue;
            p[i + 1] = p[i] + (dir / len) * segLen[i];
        }
        // Convergence: tip within kConvergeEps of the blended target.
        if ((p[count - 1] - targetEff).length() < kConvergeEps) break;
    }

    // --- Rotations: map each ORIGINAL segment axis onto its new axis.
    // Premultiplying preserves the bone's own roll (same as TwoBone step 10).
    for (uint32_t i = 0; i + 1 < count; ++i) {
        const FVector3 oldDir = jointPos[i + 1] - jointPos[i];
        const FVector3 newDir = p[i + 1] - p[i];
        const float    oldLen = oldDir.length();
        const float    newLen = newDir.length();
        if (oldLen < kEps || newLen < kEps) continue;   // unchanged
        out.worldRot[i] = FQuaternion::fromToRotation(oldDir / oldLen, newDir / newLen)
                        * jointRot[i];
    }
    // out.worldRot[count-1] stays the input tip rotation (copied above).
    return out;
}

} // namespace ayt::anim
