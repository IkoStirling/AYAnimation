// TwoBoneSolver.cpp — two-bone analytic IK solve (P4-1, design §4.25.5).
//
// Geometry: A = chain root, B = mid, C = tip, T = target. The chain is
// solved in WORLD space: joint angles via the cosine law, rotations via
// fromToRotation, bend direction inherited from the current chain plane.
// The root world POSITION is never moved (anchor semantics) — position
// changes propagate purely through the rotation chain.
//
// Step-by-step math (eps = 1e-5f):
//   1. NaN guard                    -> return inputs unchanged
//   2. bone lengths len0=|B-A|, len1=|C-B|; zero-length -> unchanged
//   3. d=|T-A|; d < eps (target at root) -> unchanged
//   4. reachable = d <= len0+len1+eps
//   5. dC = clamp(d, |len0-len1|, len0+len1) — too-close AND unreachable
//      targets both collapse into the same formula: cos ~ 1 when clamped
//      down, so the chain straightens toward the target automatically.
//   6. bend axis n = normalize(cross(unit0, unit1)); collinear (|n|^2 < eps)
//      falls back to root's up axis, then +X, then +Y.
//   7. alpha = acos(clamp((len0^2 + dC^2 - len1^2) / (2*len0*dC), -1, 1))
//   8. TWO candidate mid positions (rotate unitTarget by +/-alpha about n)
//      + side-preserving pick — immune to axis-handedness sign bugs.
//   9. new tip = B_new + len1 * normalize(T - B_new)
//  10. rootRotNew = fromToRotation(unit0, normalize(B_new-A)) * rootRot;
//      midRotNew  = fromToRotation(unit1, normalize(C_new-B_new)) * midRot
//  11. weight blend in world space: slerp(current, solved, saturate(w))
//  12. return {finalRoot, finalMid, reachable}

#include "AYAnimation/TwoBoneSolver.h"

#include <cmath>

namespace ayt::anim
{

namespace
{

constexpr float kEps = 1e-5f;

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

TwoBoneIKResult TwoBoneSolver::solve(
    const ayt::math::FVector3&    rootPos,
    const ayt::math::FVector3&    midPos,
    const ayt::math::FVector3&    tipPos,
    const ayt::math::FQuaternion& rootRot,
    const ayt::math::FQuaternion& midRot,
    const ayt::math::FVector3&    targetPos,
    float                         weight)
{
    using ayt::math::FVector3;
    using ayt::math::FQuaternion;

    const FVector3& A = rootPos;
    const FVector3& B = midPos;
    const FVector3& C = tipPos;
    const FVector3& T = targetPos;

    // Step 1 — NaN guard (INV-74).
    if (!isFiniteVec(A) || !isFiniteVec(B) || !isFiniteVec(C) || !isFiniteVec(T)) {
        return { rootRot, midRot, true };
    }

    // Step 2 — bone lengths; zero-length bone has no solution.
    const FVector3 b0 = B - A;
    const FVector3 b1 = C - B;
    const float    len0 = b0.length();
    const float    len1 = b1.length();
    if (len0 < kEps || len1 < kEps) {
        return { rootRot, midRot, true };
    }
    const FVector3 unit0 = b0 / len0;
    const FVector3 unit1 = b1 / len1;

    // Step 3 — target at the root: no direction to point.
    const FVector3 dir = T - A;
    const float    d   = dir.length();
    if (d < kEps) {
        return { rootRot, midRot, true };
    }

    // Step 4 — reachability (diagnostic only; clamped dC below makes the
    // unreachable case fold into the same formula with cos ~ 1).
    const bool reachable = (d <= len0 + len1 + kEps);

    // Step 5 — clamp target distance into the cosine-law domain.
    const float dC = clampf(d, std::fabs(len0 - len1), len0 + len1);

    // Step 6 — bend axis, PERPENDICULAR TO THE TARGET DIRECTION so the
    // rotation of unitTarget about it is non-degenerate (rotating a vector
    // about a parallel axis is a no-op — that was the v1 bug this check
    // kills). Preferred axis: the current chain plane normal. A collinear
    // chain OR a target parallel to that normal falls back to
    // cross(unitTarget, unit0), then the root's up axis, then +X, then +Y.
    const FVector3 unitTarget = dir / d;
    FVector3 n = unit0.cross(unit1);
    const bool planeDegenerate =
        n.lengthSq() < kEps * kEps ||
        std::fabs((n / n.length()).dot(unitTarget)) > 1.0f - kEps;
    if (planeDegenerate) {
        n = unitTarget.cross(unit0);
        if (n.lengthSq() < kEps * kEps) {
            n = unitTarget.cross(rootRot * FVector3(0.0f, 1.0f, 0.0f));
        }
        if (n.lengthSq() < kEps * kEps) {
            n = unitTarget.cross(FVector3(1.0f, 0.0f, 0.0f));
        }
        if (n.lengthSq() < kEps * kEps) {
            n = unitTarget.cross(FVector3(0.0f, 1.0f, 0.0f));
        }
    }
    if (n.lengthSq() < kEps * kEps) {
        // Defensive: no usable axis exists (target parallel to every
        // candidate) — nothing to bend toward, keep the pose.
        return { rootRot, midRot, true };
    }
    n = n.normalize();

    // Step 7 — root joint angle (cosine law).
    const float cosA = (len0 * len0 + dC * dC - len1 * len1) / (2.0f * len0 * dC);
    const float alpha = std::acos(clampf(cosA, -1.0f, 1.0f));

    // Step 8 — two candidate mid positions; pick the side the CURRENT mid
    // sits on (sign of cross(unitTarget, unit0) dot n). Candidate method is
    // immune to axis-handedness sign conventions.
    const FVector3 Bp = A + len0 * (FQuaternion::fromAxisAngle(n, +alpha) * unitTarget);
    const FVector3 Bm = A + len0 * (FQuaternion::fromAxisAngle(n, -alpha) * unitTarget);
    const float    side = unitTarget.cross(unit0).dot(n);
    const FVector3 Bnew = (side >= 0.0f) ? Bp : Bm;

    // Step 9 — new tip, re-normalized onto the len1 sphere (the alpha
    // construction guarantees |T - Bnew| ~ len1; normalize absorbs drift).
    const FVector3 b1new = T - Bnew;
    const float    dC1   = b1new.length();
    const FVector3 Cnew = (dC1 > kEps) ? (Bnew + len1 * (b1new / dC1)) : (Bnew + len1 * unitTarget);

    // Step 10 — world rotations. fromToRotation maps the CURRENT bone axis
    // onto the NEW bone axis; premultiplying preserves the bone's own roll.
    const FVector3 b0new  = Bnew - A;
    const FVector3 b1new2 = Cnew - Bnew;
    FQuaternion rootRotNew = FQuaternion::fromToRotation(unit0, b0new.normalize()) * rootRot;
    FQuaternion midRotNew  = FQuaternion::fromToRotation(unit1, b1new2.normalize()) * midRot;

    // Step 11 — weight blend (world space: same pivot for both bones).
    const float w = clampf(weight, 0.0f, 1.0f);
    if (w < 1.0f) {
        rootRotNew = rootRot.slerp(rootRotNew, w);
        midRotNew  = midRot.slerp(midRotNew, w);
    }

    // Step 12.
    return { rootRotNew, midRotNew, reachable };
}

} // namespace ayt::anim
