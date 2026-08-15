// AYTest_TwoBoneIK.cpp — P4-1 (2026-08-10) Two-Bone IK acceptance cases.
//
// Two parts:
//   S1-S10  — TwoBoneSolver pure-math core (standalone, INV-74). No
//             skeleton / player involvement; world-space numbers in,
//             world-space rotations out.
//   P1-P12  — AnimationPlayer Phase 2.5 integration (INV-71/72/73):
//             IKChainSpec binding, eager resolve + skeleton-swap
//             re-resolve, weight gating, subtree world re-accumulation.
//
// Geometry convention: bones lie along +X at rest; targets live in the XZ
// plane so assertions on `y ≈ 0` verify the bend plane.

#include "AYAnimation.h"
#include <AYTest.h>
#include <AYMath/MathTypes.h>
#include <ayanimation/TwoBoneSolver.h>

#include <assetsDefs/IAYSkeleton.h>
#include <assetsDefs/IAYAnimation.h>
#include <assetsImpl/AYSkeleton.h>
#include <assetsImpl/AYAnimation.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace
{

constexpr float kEps = 1e-4f;

// 3-bone chain: Root(parent=-1, local (0,0,0)) → Mid(parent=0, local
// (10,0,0)) → Tip(parent=1, local (10,0,0)). Rest world: Root(0,0,0) /
// Mid(10,0,0) / Tip(20,0,0) — a straight chain along +X (collinear → solver
// bend-axis fallback is exercised). NOTE: rest world = world-position OF THE
// JOINTS (the bone's own translation), so the chain geometry reads
// A=(0,0,0), B=(10,0,0), C=(20,0,0) with len0 = len1 = 10.
Skeleton makeThreeBoneSkeleton()
{
    Skeleton skel;
    skel.setBoneCount(3);

    Bone root;
    root.name = "Root";
    root.parentIndex = -1;
    root.localPosition  = FVector3(0,0,0);
    root.localRotation  = FQuaternion::identity();
    root.localScale     = FVector3(1,1,1);
    root.inverseBindMatrix = Float4x4::identity();
    skel.setBone(0, root);

    Bone mid;
    mid.name = "Mid";
    mid.parentIndex = 0;
    mid.localPosition  = FVector3(10,0,0);
    mid.localRotation  = FQuaternion::identity();
    mid.localScale     = FVector3(1,1,1);
    mid.inverseBindMatrix = Float4x4::identity();
    skel.setBone(1, mid);

    Bone tip;
    tip.name = "Tip";
    tip.parentIndex = 1;
    tip.localPosition  = FVector3(10,0,0);
    tip.localRotation  = FQuaternion::identity();
    tip.localScale     = FVector3(1,1,1);
    tip.inverseBindMatrix = Float4x4::identity();
    skel.setBone(2, tip);

    return skel;
}

// 8-bone skeleton for multi-chain / overlap / descendant tests:
//   LRoot(-1, (0,0,0)) → LMid(0, (10,0,0)) → LTip(1, (10,0,0))
//     → LTipChild(2, (5,0,0)) → LTipChild2(3, (10,0,0))
//   RRoot(-1, (30,0,0)) → RMid(4, (10,0,0)) → RTip(5, (10,0,0))
// Rest world: LRoot(0,0,0) LMid(10,0,0) LTip(20,0,0) LTipChild(25,0,0)
//              LTipChild2(35,0,0)  RRoot(30,0,0) RMid(40,0,0) RTip(50,0,0)
Skeleton makeEightBoneSkeleton()
{
    Skeleton skel;
    skel.setBoneCount(8);

    auto bone = [](const char* name, int parent, const FVector3& local) {
        Bone b;
        b.name = name;
        b.parentIndex = parent;
        b.localPosition  = local;
        b.localRotation  = FQuaternion::identity();
        b.localScale     = FVector3(1,1,1);
        b.inverseBindMatrix = Float4x4::identity();
        return b;
    };
    skel.setBone(0, bone("LRoot",     -1, FVector3( 0,0,0)));
    skel.setBone(1, bone("LMid",       0, FVector3(10,0,0)));
    skel.setBone(2, bone("LTip",       1, FVector3(10,0,0)));
    skel.setBone(3, bone("LTipChild",  2, FVector3( 5,0,0)));
    skel.setBone(4, bone("LTipChild2", 3, FVector3(10,0,0)));
    skel.setBone(5, bone("RRoot",     -1, FVector3(30,0,0)));
    skel.setBone(6, bone("RMid",       5, FVector3(10,0,0)));
    skel.setBone(7, bone("RTip",       6, FVector3(10,0,0)));
    return skel;
}

IKChainSpec makeSpec(const char* r, const char* m, const char* t,
                     const FVector3& target, float weight = 1.0f)
{
    IKChainSpec spec;
    spec.rootBone   = r;
    spec.midBone    = m;
    spec.tipBone    = t;
    spec.targetWorld = target;
    spec.weight     = weight;
    return spec;
}

std::shared_ptr<const ISkeleton> sharedFromLocal(const Skeleton& skel)
{
    return std::static_pointer_cast<const ISkeleton>(
        std::make_shared<Skeleton>(skel));
}

// Reconstruct the chain geometry from solved WORLD rotations. The solver
// premultiplies fromToRotation(originalAxis, newAxis) onto the input
// rotation, so rotating the ORIGINAL bone axis reproduces the new bone.
// Returns the world position of the tip (used by S1/S2/S3/S9 assertions).
FVector3 chainTipAfter(const FVector3& A, const FVector3& B, const FVector3& C,
                       const FQuaternion& rootRotOut, const FQuaternion& midRotOut)
{
    const FVector3 unit0 = (B - A).normalize();
    const FVector3 unit1 = (C - B).normalize();
    const float    len0  = (B - A).length();
    const float    len1  = (C - B).length();
    const FVector3 Bnew  = A + len0 * (rootRotOut * unit0);
    const FVector3 Cnew  = Bnew + len1 * (midRotOut * unit1);
    return Cnew;
}

bool quatFinite(const FQuaternion& q)
{
    return q.x == q.x && q.y == q.y && q.z == q.z && q.w == q.w;
}

bool vecFinite(const FVector3& v)
{
    return v.x == v.x && v.y == v.y && v.z == v.z;
}

} // namespace

TEST_SUITE(TwoBoneIKTests)

// S1 — reachable target: analytic hit. Straight chain (collinear → axis
// fallback), target at (0,0,15): d=15 <= len0+len1=20. The chain must bend
// in the XZ plane (fallback axis normal to it) and the tip must land
// exactly on the target; bone lengths preserved.
TEST_CASE(solve_reachable_tip_hits_target)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(0,0,15);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_TRUE(res.reachable);
    CHECK_TRUE(quatFinite(res.rootRotation));
    CHECK_TRUE(quatFinite(res.midRotation));

    const FVector3 Bnew = A + 10.0f * (res.rootRotation * FVector3(1,0,0));
    const FVector3 Cnew = chainTipAfter(A, B, C, res.rootRotation, res.midRotation);
    CHECK_FLOAT_EQ(10.0f, (Bnew - A).length(), kEps);        // len0 preserved
    CHECK_FLOAT_EQ(10.0f, (Cnew - Bnew).length(), kEps);     // len1 preserved
    CHECK_FLOAT_EQ(T.x, Cnew.x, kEps);                       // tip == target
    CHECK_FLOAT_EQ(T.y, Cnew.y, kEps);
    CHECK_FLOAT_EQ(T.z, Cnew.z, kEps);
    CHECK_FLOAT_EQ(0.0f, Bnew.y, kEps);                      // XZ plane kept
    CHECK_FLOAT_EQ(0.0f, Cnew.y, kEps);
}

// S2 — unreachable target: chain straightens toward the target. d=30 >
// len0+len1=20 → reachable=false, all three joints collinear along +Z.
TEST_CASE(solve_unreachable_stretches)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(0,0,30);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_FALSE(res.reachable);
    CHECK_TRUE(quatFinite(res.rootRotation));
    CHECK_TRUE(quatFinite(res.midRotation));

    const FVector3 Bnew = A + 10.0f * (res.rootRotation * FVector3(1,0,0));
    const FVector3 Cnew = chainTipAfter(A, B, C, res.rootRotation, res.midRotation);
    CHECK_FLOAT_EQ(0.0f, Bnew.x, kEps);  CHECK_FLOAT_EQ(0.0f, Bnew.y, kEps);
    CHECK_FLOAT_EQ(10.0f, Bnew.z, kEps); // straight toward target
    CHECK_FLOAT_EQ(0.0f, Cnew.x, kEps);  CHECK_FLOAT_EQ(0.0f, Cnew.y, kEps);
    CHECK_FLOAT_EQ(20.0f, Cnew.z, kEps); // stretched at reach limit
}

// S3 — target inside reach but past the straight-line domain (too close to
// the root): cosine law clamps, output stays finite, bone lengths intact.
TEST_CASE(solve_target_too_close_no_nan)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(0.001f,0,0);   // d < |len0-len1| = 0

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_TRUE(quatFinite(res.rootRotation));
    CHECK_TRUE(quatFinite(res.midRotation));
    const FVector3 Bnew = A + 10.0f * (res.rootRotation * FVector3(1,0,0));
    const FVector3 Cnew = chainTipAfter(A, B, C, res.rootRotation, res.midRotation);
    CHECK_TRUE(vecFinite(Bnew));
    CHECK_TRUE(vecFinite(Cnew));
    CHECK_FLOAT_EQ(10.0f, (Bnew - A).length(), kEps);
    CHECK_FLOAT_EQ(10.0f, (Cnew - Bnew).length(), kEps);
}

// S4 — zero-length bone (mid == root): no solution, inputs returned
// unchanged, bit-identical.
TEST_CASE(solve_zero_length_bone_noop)
{
    const FVector3    A(0,0,0), B(0,0,0), C(20,0,0);
    const FQuaternion rootRot(0.1f, 0.2f, 0.3f, 0.9f);
    const FQuaternion midRot(0.9f, -0.1f, 0.0f, 0.4f);
    const FVector3    T(5,5,5);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_FLOAT_EQ(rootRot.x, res.rootRotation.x, 0.0f);
    CHECK_FLOAT_EQ(rootRot.y, res.rootRotation.y, 0.0f);
    CHECK_FLOAT_EQ(rootRot.z, res.rootRotation.z, 0.0f);
    CHECK_FLOAT_EQ(rootRot.w, res.rootRotation.w, 0.0f);
    CHECK_FLOAT_EQ(midRot.x, res.midRotation.x, 0.0f);
    CHECK_FLOAT_EQ(midRot.y, res.midRotation.y, 0.0f);
    CHECK_FLOAT_EQ(midRot.z, res.midRotation.z, 0.0f);
    CHECK_FLOAT_EQ(midRot.w, res.midRotation.w, 0.0f);
}

// S5 — target ON the chain extension line, exactly at the reach limit
// (unitTarget ∥ unit0 → cross=0 → up-axis fallback). Straight pose is
// already correct: alpha=0, tip hits at d = len0+len1 = 20.
TEST_CASE(solve_collinear_target_along_chain)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(20,0,0);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_TRUE(res.reachable);   // d = 20 == len0+len1 exactly
    CHECK_TRUE(quatFinite(res.rootRotation));
    CHECK_TRUE(quatFinite(res.midRotation));
    const FVector3 Cnew = chainTipAfter(A, B, C, res.rootRotation, res.midRotation);
    CHECK_FLOAT_EQ(T.x, Cnew.x, kEps);
    CHECK_FLOAT_EQ(T.y, Cnew.y, kEps);
    CHECK_FLOAT_EQ(T.z, Cnew.z, kEps);
}

// S6 — NaN in the target: inputs returned unchanged, nothing NaN escapes.
TEST_CASE(solve_nan_input_noop)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot(0.1f, 0.0f, 0.0f, 0.995f);
    const FQuaternion midRot(0.0f, 0.2f, 0.0f, 0.98f);
    const FVector3    T(0, std::nanf(""), 15);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_FLOAT_EQ(rootRot.x, res.rootRotation.x, 0.0f);
    CHECK_FLOAT_EQ(rootRot.y, res.rootRotation.y, 0.0f);
    CHECK_FLOAT_EQ(rootRot.z, res.rootRotation.z, 0.0f);
    CHECK_FLOAT_EQ(rootRot.w, res.rootRotation.w, 0.0f);
    CHECK_FLOAT_EQ(midRot.x, res.midRotation.x, 0.0f);
    CHECK_FLOAT_EQ(midRot.y, res.midRotation.y, 0.0f);
    CHECK_FLOAT_EQ(midRot.z, res.midRotation.z, 0.0f);
    CHECK_FLOAT_EQ(midRot.w, res.midRotation.w, 0.0f);
}

// S7 — antiparallel target (180° flip): MathTypes::fromToRotation falls
// back to a perpendicular axis (design F2); result finite, tip lands at
// the stretched position toward the target.
TEST_CASE(solve_antiparallel_flip_no_nan)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(-30,0,0);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);

    CHECK_FALSE(res.reachable);
    CHECK_TRUE(quatFinite(res.rootRotation));
    CHECK_TRUE(quatFinite(res.midRotation));
    const FVector3 Cnew = chainTipAfter(A, B, C, res.rootRotation, res.midRotation);
    CHECK_FLOAT_EQ(-20.0f, Cnew.x, kEps);   // stretched along -X
    CHECK_FLOAT_EQ(0.0f, Cnew.y, kEps);
    CHECK_FLOAT_EQ(0.0f, Cnew.z, kEps);
}

// S8 — weight = 0: full solve skipped, inputs returned unchanged.
TEST_CASE(solve_weight_zero_identity)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot(0.1f, 0.2f, 0.3f, 0.9f);
    const FQuaternion midRot(0.9f, -0.1f, 0.0f, 0.4f);
    const FVector3    T(0,0,15);

    const TwoBoneIKResult res = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 0.0f);

    CHECK_FLOAT_EQ(rootRot.x, res.rootRotation.x, 0.0f);
    CHECK_FLOAT_EQ(rootRot.y, res.rootRotation.y, 0.0f);
    CHECK_FLOAT_EQ(rootRot.z, res.rootRotation.z, 0.0f);
    CHECK_FLOAT_EQ(rootRot.w, res.rootRotation.w, 0.0f);
    CHECK_FLOAT_EQ(midRot.x, res.midRotation.x, 0.0f);
    CHECK_FLOAT_EQ(midRot.y, res.midRotation.y, 0.0f);
    CHECK_FLOAT_EQ(midRot.z, res.midRotation.z, 0.0f);
    CHECK_FLOAT_EQ(midRot.w, res.midRotation.w, 0.0f);
}

// S9 — partial weight: result equals slerp(current, full-solve, w), and the
// tip distance to the target decreases monotonically with weight.
TEST_CASE(solve_weight_half_blends_and_monotone)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot = FQuaternion::identity();
    const FQuaternion midRot  = FQuaternion::identity();
    const FVector3    T(0,0,15);

    const TwoBoneIKResult full = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);
    const TwoBoneIKResult half = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 0.5f);
    const TwoBoneIKResult q25  = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 0.25f);
    const TwoBoneIKResult q75  = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 0.75f);

    // Blend == slerp(current, full, w).
    const FQuaternion expectRoot = rootRot.slerp(full.rootRotation, 0.5f);
    const FQuaternion expectMid  = midRot.slerp(full.midRotation, 0.5f);
    CHECK_FLOAT_EQ(expectRoot.x, half.rootRotation.x, kEps);
    CHECK_FLOAT_EQ(expectRoot.y, half.rootRotation.y, kEps);
    CHECK_FLOAT_EQ(expectRoot.z, half.rootRotation.z, kEps);
    CHECK_FLOAT_EQ(expectRoot.w, half.rootRotation.w, kEps);
    CHECK_FLOAT_EQ(expectMid.x, half.midRotation.x, kEps);
    CHECK_FLOAT_EQ(expectMid.y, half.midRotation.y, kEps);
    CHECK_FLOAT_EQ(expectMid.z, half.midRotation.z, kEps);
    CHECK_FLOAT_EQ(expectMid.w, half.midRotation.w, kEps);

    // Monotone approach: distance(0.25) > distance(0.5) > distance(0.75).
    // NOTE: rotation-space blending is NOT position-space blending — at
    // w=0.75 the tip lands ~7 units short of the target (each bone turns
    // 0.75× its solved angle), so no exact-hit assertion here; monotone
    // approach toward the target is the contract (design §4.25.5).
    const float d25 = (chainTipAfter(A, B, C, q25.rootRotation, q25.midRotation) - T).length();
    const float d50 = (chainTipAfter(A, B, C, half.rootRotation, half.midRotation) - T).length();
    const float d75 = (chainTipAfter(A, B, C, q75.rootRotation, q75.midRotation) - T).length();
    CHECK_TRUE(d25 > d50);
    CHECK_TRUE(d50 > d75);
    CHECK_TRUE(d75 < d25);
}

// S10 — out-of-range weight clamps: 1.5 == full solve, -1 == no-op.
TEST_CASE(solve_weight_out_of_range_clamped)
{
    const FVector3    A(0,0,0), B(10,0,0), C(20,0,0);
    const FQuaternion rootRot(0.1f, 0.2f, 0.3f, 0.9f);
    const FQuaternion midRot(0.9f, -0.1f, 0.0f, 0.4f);
    const FVector3    T(0,0,15);

    const TwoBoneIKResult over  = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.5f);
    const TwoBoneIKResult full  = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 1.0f);
    const TwoBoneIKResult under = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, -1.0f);
    const TwoBoneIKResult zero  = TwoBoneSolver::solve(A, B, C, rootRot, midRot, T, 0.0f);

    CHECK_FLOAT_EQ(full.rootRotation.x, over.rootRotation.x, 0.0f);
    CHECK_FLOAT_EQ(full.rootRotation.y, over.rootRotation.y, 0.0f);
    CHECK_FLOAT_EQ(full.rootRotation.z, over.rootRotation.z, 0.0f);
    CHECK_FLOAT_EQ(full.rootRotation.w, over.rootRotation.w, 0.0f);
    CHECK_FLOAT_EQ(full.midRotation.x, over.midRotation.x, 0.0f);
    CHECK_FLOAT_EQ(full.midRotation.y, over.midRotation.y, 0.0f);
    CHECK_FLOAT_EQ(full.midRotation.z, over.midRotation.z, 0.0f);
    CHECK_FLOAT_EQ(full.midRotation.w, over.midRotation.w, 0.0f);

    CHECK_FLOAT_EQ(zero.rootRotation.x, under.rootRotation.x, 0.0f);
    CHECK_FLOAT_EQ(zero.rootRotation.y, under.rootRotation.y, 0.0f);
    CHECK_FLOAT_EQ(zero.rootRotation.z, under.rootRotation.z, 0.0f);
    CHECK_FLOAT_EQ(zero.rootRotation.w, under.rootRotation.w, 0.0f);
    CHECK_FLOAT_EQ(zero.midRotation.x, under.midRotation.x, 0.0f);
    CHECK_FLOAT_EQ(zero.midRotation.y, under.midRotation.y, 0.0f);
    CHECK_FLOAT_EQ(zero.midRotation.z, under.midRotation.z, 0.0f);
    CHECK_FLOAT_EQ(zero.midRotation.w, under.midRotation.w, 0.0f);
}

TEST_SUITE_END

TEST_SUITE(AnimationPlayerIKTests)

namespace
{
// Player bound to the 3-bone chain + a no-track anim (rest pose every
// frame). Chain geometry: A=(0,0,0) B=(10,0,0) C=(20,0,0), len0=len1=10.
AnimationPlayer makeBoundPlayer(const Skeleton& skel, const IKChainSpec& spec)
{
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    player.setIKChain(0, spec);
    return player;
}

FVector3 worldPos(const AnimationPlayer& p, size_t i)
{
    return p.getBoneWorldMatrices()[i].transformPoint(FVector3(0,0,0));
}

bool worldFinite(const AnimationPlayer& p)
{
    const Float4x4* w = p.getBoneWorldMatrices();
    for (size_t i = 0; i < p.getBoneCount(); ++i) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const float v = w[i](r, c);
                if (!(v == v)) return false;   // NaN check
            }
        }
    }
    return true;
}
} // namespace

// P1 — bind → evaluate → tip lands on the target (1e-3); the chain root
// world position is untouched (anchor semantics); bend stays in the XZ
// plane; every matrix element stays finite.
TEST_CASE(ik_binds_chain_and_hits_target)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    CHECK_TRUE(player.isIKChainActive(0));
    player.play(&anim);
    player.evaluate();

    const FVector3 rp = worldPos(player, 0);
    CHECK_FLOAT_EQ(0.0f, rp.x, 1e-3f);   // anchor preserved
    CHECK_FLOAT_EQ(0.0f, rp.y, 1e-3f);
    CHECK_FLOAT_EQ(0.0f, rp.z, 1e-3f);
    const FVector3 mp = worldPos(player, 1);
    CHECK_FLOAT_EQ(0.0f, mp.y, 1e-3f);   // XZ plane bend
    const FVector3 tp = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f, tp.x, 1e-3f);   // tip == target
    CHECK_FLOAT_EQ(0.0f, tp.y, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tp.z, 1e-3f);
    CHECK_TRUE(worldFinite(player));
}

// P2 — weight = 0: the chain costs zero work AND the world output is
// bit-identical to a player with no chain bound (memcmp guard).
TEST_CASE(ik_weight_zero_bit_identical_to_no_chain)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer base;
    base.setSkeleton(sharedFromLocal(skel));
    base.play(&anim);
    base.evaluate();

    AnimationPlayer ik;
    ik.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(ik.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15), 0.0f)));
    CHECK_FALSE(ik.isIKChainActive(0));   // weight 0 → inactive
    ik.play(&anim);
    ik.evaluate();

    CHECK(ik.getBoneCount() == base.getBoneCount());
    CHECK(std::memcmp(ik.getBoneWorldMatrices(), base.getBoneWorldMatrices(),
                      base.getBoneCount() * sizeof(Float4x4)) == 0);
}

// P3 — partial weight: the tip approaches the target monotonically as the
// weight rises (rotation-space blend, so no exact hit below w=1; the
// rest-distance > w0.25 > w0.5 > w0.75 > w1.0 chain is the contract).
TEST_CASE(ik_partial_weight_monotone_approach)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    const FVector3 target(0,0,15);
    const FVector3 restTip(20,0,0);
    float prev = (restTip - target).length();   // 25
    for (float w : {0.25f, 0.5f, 0.75f, 1.0f}) {
        AnimationPlayer player = makeBoundPlayer(
            skel, makeSpec("Root", "Mid", "Tip", target, w));
        player.play(&anim);
        player.evaluate();
        const float d = (worldPos(player, 2) - target).length();
        CHECK_TRUE(d < prev);      // strictly closer than the previous weight
        prev = d;
    }
    CHECK_FLOAT_EQ(0.0f, prev, 1e-3f);   // w=1 lands on the target
}

// P4 — unknown bone names: chain resolved to -1 (disabled), no crash, world
// identical to a no-chain player, active flag false.
TEST_CASE(ik_unknown_bone_disabled_no_crash)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer base;
    base.setSkeleton(sharedFromLocal(skel));
    base.play(&anim);
    base.evaluate();

    AnimationPlayer ik;
    ik.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(ik.setIKChain(0, makeSpec("NoSuchRoot", "NoSuchMid", "NoSuchTip", FVector3(0,0,15))));
    CHECK_FALSE(ik.isIKChainActive(0));
    ik.play(&anim);
    ik.evaluate();

    CHECK(std::memcmp(ik.getBoneWorldMatrices(), base.getBoneWorldMatrices(),
                      base.getBoneCount() * sizeof(Float4x4)) == 0);
}

// P5 — setSkeleton re-resolve (INV-71): a same-named skeleton revives the
// chain (still hits), a differently-named skeleton disables it (no crash);
// generation bumps on every bind and every re-resolve.
TEST_CASE(ik_skeleton_swap_reresolves_and_bumps_generation)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    const uint32_t g0 = player.getIKChainGeneration();
    CHECK_TRUE(g0 > 0);

    // Same-named skeleton (fresh copy) → chain revives and still hits.
    player.setSkeleton(sharedFromLocal(makeThreeBoneSkeleton()));
    CHECK_TRUE(player.getIKChainGeneration() > g0);
    CHECK_TRUE(player.isIKChainActive(0));
    player.play(&anim);
    player.evaluate();
    const FVector3 tp = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f, tp.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tp.z, 1e-3f);

    // Differently-named skeleton → chain disabled, no crash.
    player.setSkeleton(sharedFromLocal(makeEightBoneSkeleton()));
    CHECK_FALSE(player.isIKChainActive(0));
    player.evaluate();
    CHECK_TRUE(worldFinite(player));
}

// P6 — two independent chains on one skeleton, both hit their own target.
TEST_CASE(ik_two_independent_chains_both_hit)
{
    Skeleton skel = makeEightBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("LRoot", "LMid", "LTip", FVector3(0,0,15))));
    CHECK_TRUE(player.setIKChain(1, makeSpec("RRoot", "RMid", "RTip", FVector3(30,0,15))));
    player.play(&anim);
    player.evaluate();

    const FVector3 lt = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f,  lt.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, lt.z, 1e-3f);
    const FVector3 rt = worldPos(player, 7);
    CHECK_FLOAT_EQ(30.0f, rt.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, rt.z, 1e-3f);
    // Roots untouched (anchors).
    const FVector3 lr = worldPos(player, 0);
    const FVector3 rr = worldPos(player, 5);
    CHECK_FLOAT_EQ(0.0f,  lr.x, 1e-3f);
    CHECK_FLOAT_EQ(30.0f, rr.x, 1e-3f);
    CHECK_TRUE(worldFinite(player));
}

// P7 — overlapping chains: chain 1's ROOT is chain 0's TIP. The solver
// anchors the chain root (never moves its world POSITION), so chain 1 can
// bend its own sub-chain without displacing chain 0's tip — both hit
// simultaneously. NOTE: chains that SHARE a mid/tip bone (e.g. chain 1's
// mid == chain 0's tip) cannot both hit — the later chain overwrites the
// shared bone's rotation (execution order = chainId ascending); that
// order-dependence is documented in design §4.25.5 Open Questions.
TEST_CASE(ik_overlapping_chains_both_hit)
{
    Skeleton skel = makeEightBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    // chain 0: LRoot→LMid→LTip target (0,0,15)  — LTip lands there.
    // chain 1: LTip→LTipChild→LTipChild2 target (10,0,20) — root = LTip,
    //          anchored; len0=5 len1=10, d=|(10,0,5)|≈11.2 <= 15 reachable.
    CHECK_TRUE(player.setIKChain(0, makeSpec("LRoot", "LMid", "LTip", FVector3(0,0,15))));
    CHECK_TRUE(player.setIKChain(1, makeSpec("LTip", "LTipChild", "LTipChild2", FVector3(10,0,20))));
    player.play(&anim);
    player.evaluate();

    // chain 0 hit: LTip anchored on its target (chain 1 never moves it).
    const FVector3 tip = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f,  tip.x, 1e-3f);
    CHECK_FLOAT_EQ(0.0f,  tip.y, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tip.z, 1e-3f);
    // chain 1 hit: LTipChild2 on (10,0,20).
    const FVector3 child2 = worldPos(player, 4);
    CHECK_FLOAT_EQ(10.0f, child2.x, 1e-3f);
    CHECK_FLOAT_EQ(0.0f,  child2.y, 1e-3f);
    CHECK_FLOAT_EQ(20.0f, child2.z, 1e-3f);
    CHECK_TRUE(worldFinite(player));
}

// P8 — descendants follow the chain tip: LTipChild (local (5,0,0)) sits
// exactly 5 units from LTip after the solve, all finite.
TEST_CASE(ik_descendant_follows_tip)
{
    Skeleton skel = makeEightBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("LRoot", "LMid", "LTip", FVector3(0,0,15))));
    player.play(&anim);
    player.evaluate();

    const FVector3 tip   = worldPos(player, 2);
    const FVector3 child = worldPos(player, 3);
    CHECK_FLOAT_EQ(0.0f, tip.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tip.z, 1e-3f);
    CHECK_FLOAT_EQ(5.0f, (child - tip).length(), 1e-3f);   // local offset kept
    CHECK_TRUE(worldFinite(player));
}

// P9 — no cross-frame state: seek back and forth, every evaluate still
// lands on the target (IK re-solves from scratch each frame).
TEST_CASE(ik_seek_no_cross_frame_state)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    player.play(&anim);

    player.setTime(0.3f); player.evaluate();
    player.setTime(0.7f); player.evaluate();
    player.setTime(0.5f); player.evaluate();
    const FVector3 tp = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f,  tp.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tp.z, 1e-3f);
}

// P10 — stop() → play() keeps the chain bound and hitting.
TEST_CASE(ik_stop_play_keeps_chain)
{
    Skeleton skel = makeThreeBoneSkeleton();
    Animation anim; anim.setDuration(1.0f);

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    player.play(&anim);
    player.evaluate();
    player.stop();
    player.play(&anim);
    player.evaluate();
    const FVector3 tp = worldPos(player, 2);
    CHECK_FLOAT_EQ(0.0f,  tp.x, 1e-3f);
    CHECK_FLOAT_EQ(15.0f, tp.z, 1e-3f);
}

// P11 — chain bound but never played: evaluate() early-outs, no crash,
// skeleton buffers still queryable.
TEST_CASE(ik_bound_but_not_played_evaluate_early_out)
{
    Skeleton skel = makeThreeBoneSkeleton();
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    CHECK_TRUE(player.setIKChain(0, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    player.evaluate();          // no play() — must not crash
    CHECK(player.getBoneCount() == 3);
    CHECK_TRUE(worldFinite(player));
}

// P12 — chainId out of range: set silently ignored (false), read returns an
// empty spec, count only reflects bound chains; the last legal id works.
TEST_CASE(ik_chain_id_out_of_range_ignored)
{
    Skeleton skel = makeThreeBoneSkeleton();
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));

    CHECK_FALSE(player.setIKChain(kMaxIKChains, makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    CHECK_FALSE(player.setIKChain(999u,     makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    CHECK(player.getIKChainCount() == 0);
    const IKChainSpec& empty = player.getIKChain(kMaxIKChains);
    CHECK(empty.rootBone.empty());

    CHECK_TRUE(player.setIKChain(kMaxIKChains - 1u,
                                 makeSpec("Root", "Mid", "Tip", FVector3(0,0,15))));
    CHECK(player.getIKChainCount() == 1);
}

TEST_SUITE_END
