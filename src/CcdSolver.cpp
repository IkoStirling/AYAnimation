// CcdSolver.cpp — CCD iterative IK solve (P4-2, design §4.26.5).
//
// Cyclic Coordinate Descent: each pass sweeps from the penultimate joint
// down to the chain root; every joint rotates its whole sub-chain about
// ITS OWN pivot so the tip moves toward the blended target. Per joint:
// axis = normalize(cross(unit(joint→tip), unit(joint→target))) — rotating
// the tip-vector onto the target-vector by the angle between them
// (right-hand rule; no angle limit this slice). A collinear joint
// (|cross| ≈ 0) is skipped — a chain exactly collinear with the target
// ON the chain line can therefore NOT be straightened by CCD (FABRIK can;
// documented asymmetry, header + design §4.26.5).
//
// Rotations are derived AFTER the position iteration (same mapping as
// FABRIK: fromToRotation of the original segment axis onto the final one).
// Accumulating the per-joint deltas during the iteration would be wrong —
// each R_j pivots about the joint's CURRENT anchor, which itself moves as
// other joints rotate (dev-time bug, fixed in P4-2; see the note in
// solve()).

#include "AYAnimation/CcdSolver.h"

#include <cmath>

namespace ayt::anim
{

namespace
{

constexpr float      kEps = 1e-5f;          // geometric guards (mirrors FABRIK)
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

IterativeIKResult CcdSolver::solve(
    const ayt::math::FVector3*    jointPos,
    const ayt::math::FQuaternion* jointRot,
    uint32_t                     count,
    const ayt::math::FVector3&   targetPos,
    float                        weight,
    uint32_t                     iterations)
{
    using ayt::math::FVector3;
    using ayt::math::FQuaternion;

    // Guard set IDENTICAL to FabrikSolver (see FabrikSolver.cpp) — every
    // degenerate input returns the inputs bit-exact (INV-74).
    IterativeIKResult out;
    for (uint32_t i = 0; i < count; ++i) {
        out.worldRot[i] = jointRot[i];
    }
    out.reachable = true;

    if (count < 2)       return out;
    if (weight <= 0.0f)  return out;
    if (!isFiniteVec(targetPos)) return out;
    for (uint32_t i = 0; i < count; ++i) {
        if (!isFiniteVec(jointPos[i])) return out;
    }

    const float w = clampf(weight, 0.0f, 1.0f);

    // Weight blend in GOAL space (same semantics as FabrikSolver).
    const FVector3 tip = jointPos[count - 1];
    const FVector3 targetEff = tip + (targetPos - tip) * w;

    // Static reachability diagnostic (does not change the solve).
    float totalLen = 0.0f;
    for (uint32_t i = 0; i + 1 < count; ++i) {
        totalLen += (jointPos[i + 1] - jointPos[i]).length();
    }
    const float dRoot = (targetEff - jointPos[0]).length();
    if (dRoot < kEps) return out;        // blended target at the root — no direction
    out.reachable = (dRoot <= totalLen + kEps);

    FVector3 p[kMaxIKChainBones];
    for (uint32_t i = 0; i < count; ++i) p[i] = jointPos[i];

    if (iterations == 0) iterations = kDefaultCcdIterations;
    iterations = (iterations > kMaxIterations) ? kMaxIterations : iterations;

    // --- Iteration ---
    for (uint32_t it = 0; it < iterations; ++it) {
        // ONE PASS: sweep from the penultimate joint down to the root.
        for (int32_t j = static_cast<int32_t>(count) - 2; j >= 0; --j) {
            // Zero-length segment: no lever arm — leave the joint unchanged
            // (mirrors FABRIK; keeps the two solvers' semantics aligned).
            if ((p[j + 1] - p[j]).length() < kEps) continue;
            const FVector3 a = p[count - 1] - p[j];   // joint → current tip
            const FVector3 b = targetEff - p[j];      // joint → blended target
            const float    la = a.length();
            const float    lb = b.length();
            if (la < kEps || lb < kEps) continue;     // tip or target at the joint
            const FVector3 an = a / la;
            const FVector3 bn = b / lb;

            const FVector3 axis = an.cross(bn);       // rotate tip toward target
            const float    axisLen = axis.length();
            if (axisLen < kEps) continue;             // COLLINEAR — no bend possible
                                                      // (documented asymmetry vs FABRIK)
            const FVector3 n = axis / axisLen;
            const float    cosT = clampf(an.dot(bn), -1.0f, 1.0f);
            const float    theta = std::acos(cosT);   // no angle limit this slice
            const FQuaternion R = FQuaternion::fromAxisAngle(n, theta);

            for (uint32_t k = static_cast<uint32_t>(j) + 1; k < count; ++k) {
                p[k] = p[j] + R * (p[k] - p[j]);      // rotate sub-chain about joint j
            }
        }
        // Convergence: tip within kConvergeEps of the blended target.
        if ((p[count - 1] - targetEff).length() < kConvergeEps) break;
    }

    // Rotations: derived AFTER the position iteration. The per-joint deltas
    // are NOT accumulated during it — each R_j pivots about the joint's
    // CURRENT anchor, which itself moves as other joints rotate, so a
    // world-frame accumulation of those pivots would not reproduce the
    // final chain (that was a P4-2 dev-time bug). Same mapping as FABRIK:
    // each bone's new world rotation maps its ORIGINAL segment axis onto
    // its final axis.
    for (uint32_t j = 0; j + 1 < count; ++j) {
        const FVector3 oldDir = jointPos[j + 1] - jointPos[j];
        const FVector3 newDir = p[j + 1] - p[j];
        const float    oldLen = oldDir.length();
        const float    newLen = newDir.length();
        if (oldLen < kEps || newLen < kEps) continue;   // unchanged
        out.worldRot[j] = FQuaternion::fromToRotation(oldDir / oldLen, newDir / newLen)
                        * jointRot[j];
    }
    // out.worldRot[count-1] stays the input tip rotation (copied above).
    return out;
}

} // namespace ayt::anim
