// AYTest_IterativeIK.cpp — P4-2 (2026-08-11) FABRIK + CCD acceptance cases.
//
// 三段：
//   F1-F11  FabrikSolverTests — pure math, INV-74 contract
//   C1-C11  CcdSolverTests    — pure math, INV-74 contract (+ documented
//                                collinear asymmetry vs FABRIK)
//   I 系列  AnimationPlayerIterativeIKTests — INV-75/76/77 player unified
//            pass integration (added with the player-side change)
//
// Geometry convention (mirrors AYTest_TwoBoneIK.cpp): bones rest along +X,
// targets in the XZ plane, y ≈ 0 asserts the bend plane.

#include <AYAnimation.h>
#include "AYTest.h"

#include <AYMath/MathTypes.h>
#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYResource/assetsImpl/Animation.h>
#include <cstring>
#include <limits>
#include <string>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace
{

constexpr float kEps = 1e-4f;

using ayt::math::FVector3;
using ayt::math::FQuaternion;

// Rebuild the chain geometry from the solved rotations:
//   pos[0] = jointPos[0]; pos[i+1] = pos[i] + outRot[i] * (jointPos[i+1]-jointPos[i])
// Returns the final (tip) position.
FVector3 chainTipAfter(const FVector3* jointPos, const FQuaternion* outRot, uint32_t count)
{
    FVector3 pos = jointPos[0];
    for (uint32_t i = 0; i + 1 < count; ++i) {
        pos = pos + outRot[i] * (jointPos[i + 1] - jointPos[i]);
    }
    return pos;
}

bool quatFinite(const FQuaternion& q)
{
    return (q.x == q.x) && (q.y == q.y) && (q.z == q.z) && (q.w == q.w);
}

bool rotArrayFinite(const FQuaternion* q, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (!quatFinite(q[i])) return false;
    }
    return true;
}

// Bit-exact identity check (NaN bits compare equal when copied verbatim).
bool quatBitSame(const FQuaternion& a, const FQuaternion& b)
{
    uint32_t ba[4], bb[4];
    std::memcpy(ba, &a.x, sizeof(ba));
    std::memcpy(bb, &b.x, sizeof(bb));
    return ba[0] == bb[0] && ba[1] == bb[1] && ba[2] == bb[2] && ba[3] == bb[3];
}

bool rotArrayBitSame(const FQuaternion* a, const FQuaternion* b, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (!quatBitSame(a[i], b[i])) return false;
    }
    return true;
}

// 4-joint collinear fixture: rest (0,0,0)/(10,0,0)/(20,0,0)/(30,0,0).
const FVector3 kFourJointPos[4] = { {0,0,0}, {10,0,0}, {20,0,0}, {30,0,0} };
const FQuaternion kFourJointRot[4] = { FQuaternion::identity(), FQuaternion::identity(),
                                       FQuaternion::identity(), FQuaternion::identity() };

// ---------------------------------------------------------------- I-series
// fixtures (AnimationPlayer integration; mirrors AYTest_TwoBoneIK.cpp).

// n-joint single chain — rest world joints at (10*i, 0, 0) with the ROOT
// at the origin (Root local (0,0,0), every other bone local (10,0,0) —
// mirror of the ThreeBone fixture's Root). Names: index 0 = "Root",
// index n-1 = "Tip", interior bones "Bone1".."Bone(n-2)" (for findBone).
Skeleton makeChainSkeleton(int n)
{
    Skeleton skel;
    skel.setBoneCount(static_cast<int>(n));
    for (int i = 0; i < n; ++i) {
        Bone b;
        b.name = (i == 0) ? "Root"
               : (i == n - 1) ? "Tip"
               : ("Bone" + std::to_string(i));
        b.parentIndex = (i == 0) ? -1 : (i - 1);
        b.localPosition  = (i == 0) ? FVector3(0, 0, 0) : FVector3(10, 0, 0);
        b.localRotation  = FQuaternion::identity();
        b.localScale     = FVector3(1,1,1);
        b.inverseBindMatrix = Float4x4::identity();
        skel.setBone(i, b);
    }
    return skel;
}

// 8-bone skeleton (mirror of AYTest_TwoBoneIK.cpp) for multi-chain /
// cross-subtree tests:
//   LRoot(-1, (0,0,0)) → LMid(0, (10,0,0)) → LTip(1, (10,0,0))
//     → LTipChild(2, (5,0,0)) → LTipChild2(3, (10,0,0))
//   RRoot(-1, (30,0,0)) → RMid(4, (10,0,0)) → RTip(5, (10,0,0))
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

std::shared_ptr<const ISkeleton> sharedFromLocal(const Skeleton& skel)
{
    return std::static_pointer_cast<const ISkeleton>(
        std::make_shared<Skeleton>(skel));
}

// Iterative-chain spec: FABRIK / CCD take root + tip only (midBone empty —
// auto-derived path, design §4.26.3). `type` / `iterations` passed through
// so tests also exercise the new IKChainSpec fields.
IKChainSpec makeItSpec(IKSolverType type, uint32_t iterations,
                       const char* r, const char* t,
                       const FVector3& target, float weight = 1.0f)
{
    IKChainSpec spec;
    spec.rootBone    = r;
    spec.tipBone     = t;
    spec.targetWorld = target;
    spec.weight      = weight;
    spec.type        = type;
    spec.iterations  = iterations;
    return spec;
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

TEST_SUITE(FabrikSolverTests)
    // F1 — reachable 4-joint chain, tip hits the target in the XZ plane,
    // root stays anchored, chain bends (root rotation changed).
    TEST_CASE(solve_fabrik_4joint_reachable_tip_hits_target)
    {
        const IterativeIKResult res = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));

        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 20.0f, 1e-3f);

        // Bend stays in the XZ plane (y ≈ 0 at every joint).
        FVector3 cur = kFourJointPos[0];
        for (uint32_t i = 0; i < 3; ++i) {
            cur = cur + res.worldRot[i] * (kFourJointPos[i + 1] - kFourJointPos[i]);
            CHECK_FLOAT_EQ(cur.y, 0.0f, 1e-3f);
        }

        // The chain bent — the root rotation is no longer identity.
        CHECK(res.worldRot[0].w < 0.999f);
    }

    // F2 — unreachable target: chain stretches straight toward it, laid
    // out to its full length (tip at (0,0,30), reachable=false).
    TEST_CASE(solve_fabrik_unreachable_stretches)
    {
        const IterativeIKResult res = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,50));

        CHECK_FALSE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));

        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 5e-2f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 30.0f, 1e-2f);
    }

    // F3 — weight=0 → outputs bit-identical to inputs (identity guard).
    TEST_CASE(solve_fabrik_weight_zero_identity)
    {
        const IterativeIKResult res = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 0.0f);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // F4 — weight=0.5 → tip lands EXACTLY on the interpolated target
    // (15,0,10) — goal-space blend semantics (vs TwoBone's slerp shortfall).
    TEST_CASE(solve_fabrik_weight_half_target_interpolation)
    {
        const IterativeIKResult res = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 0.5f);

        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 15.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 10.0f, 1e-3f);
    }

    // F5 — NaN input → outputs bit-identical to inputs (INV-74).
    TEST_CASE(solve_fabrik_nan_input_noop)
    {
        FVector3 pos[4] = { {0,0,0}, {10,0,0}, {20,0,0}, {30,0,0} };
        const float qnan = std::numeric_limits<float>::quiet_NaN();
        pos[2].x = qnan;

        const IterativeIKResult res = FabrikSolver::solve(
            pos, kFourJointRot, 4, FVector3(0,0,20));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // F6 — zero-length segment: that joint is left unchanged, the rest of
    // the chain still solves (tip hits the target).
    TEST_CASE(solve_fabrik_zero_length_segment_skips)
    {
        const FVector3 pos[4] = { {0,0,0}, {10,0,0}, {10,0,0}, {30,0,0} };
        const FQuaternion rot[4] = { FQuaternion::identity(), FQuaternion::identity(),
                                     FQuaternion::identity(), FQuaternion::identity() };

        const IterativeIKResult res = FabrikSolver::solve(pos, rot, 4, FVector3(0,0,20));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));

        // Zero-length segment's joint rotation untouched.
        CHECK_TRUE(quatBitSame(res.worldRot[1], FQuaternion::identity()));

        const FVector3 tip = chainTipAfter(pos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 20.0f, 1e-3f);
    }

    // F7 — target at the root → outputs bit-identical to inputs.
    TEST_CASE(solve_fabrik_target_at_root_noop)
    {
        const IterativeIKResult res = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, kFourJointPos[0]);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // F8 — 2-joint chain = single segment point-at (documented: no TwoBone
    // fallback for iterative types). Tip hits, root rotation maps +X→+Z.
    TEST_CASE(solve_fabrik_two_joint_single_segment)
    {
        const FVector3 pos[2] = { {0,0,0}, {10,0,0} };
        const FQuaternion rot[2] = { FQuaternion::identity(), FQuaternion::identity() };

        const IterativeIKResult res = FabrikSolver::solve(pos, rot, 2, FVector3(0,0,10));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 2));

        const FVector3 tip = chainTipAfter(pos, res.worldRot, 2);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 10.0f, 1e-3f);

        // The segment direction now points +Z.
        const FVector3 newDir = res.worldRot[0] * FVector3(1,0,0);
        CHECK_FLOAT_EQ(newDir.x, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(newDir.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(newDir.z, 1.0f, 1e-3f);
    }

    // F9 — count=1 → outputs bit-identical to inputs.
    TEST_CASE(solve_fabrik_count_one_noop)
    {
        const FVector3 pos[1] = { {5,5,5} };
        const FQuaternion rot[1] = { FQuaternion::fromAxisAngle(FVector3(0,1,0), 0.3f) };

        const IterativeIKResult res = FabrikSolver::solve(pos, rot, 1, FVector3(1,2,3));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(quatBitSame(res.worldRot[0], rot[0]));
    }

    // F10 — collinear 5-joint chain: FABRIK bends it natively (no explicit
    // bend-axis needed). Tip hits the target, plane y ≈ 0 preserved.
    TEST_CASE(solve_fabrik_collinear_bend_fallback)
    {
        const FVector3 pos[5] = { {0,0,0}, {10,0,0}, {20,0,0}, {30,0,0}, {40,0,0} };
        const FQuaternion rot[5] = { FQuaternion::identity(), FQuaternion::identity(),
                                     FQuaternion::identity(), FQuaternion::identity(),
                                     FQuaternion::identity() };

        const IterativeIKResult res = FabrikSolver::solve(pos, rot, 5, FVector3(0,0,25));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 5));

        const FVector3 tip = chainTipAfter(pos, res.worldRot, 5);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.z, 25.0f, 1e-2f);
    }

    // F11 — iterations == 0 → default (kDefaultFabrikIterations), results
    // bit-identical to the explicit default; a huge value clamps to 100
    // and still solves.
    TEST_CASE(solve_fabrik_iterations_zero_uses_default)
    {
        const IterativeIKResult def = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 1.0f, 0);
        const IterativeIKResult exp = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 1.0f,
            FabrikSolver::kDefaultFabrikIterations);
        CHECK_TRUE(rotArrayBitSame(def.worldRot, exp.worldRot, 4));

        const IterativeIKResult huge = FabrikSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 1.0f, 100000);
        CHECK_TRUE(rotArrayFinite(huge.worldRot, 4));
        const FVector3 tip = chainTipAfter(kFourJointPos, huge.worldRot, 4);
        CHECK_FLOAT_EQ(tip.z, 20.0f, 1e-3f);
    }
TEST_SUITE_END

TEST_SUITE(CcdSolverTests)
    // C1 — reachable 4-joint chain with explicit high iteration count: tip
    // hits the target, root anchored, bend in the XZ plane.
    TEST_CASE(solve_ccd_4joint_reachable_tip_hits_target)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 1.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));

        // CCD converges linearly — 100 passes leave ~2e-3 residual here
        // (FABRIK reaches 1e-3 in 4 passes; documented solver difference).
        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 5e-3f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 20.0f, 5e-3f);

        FVector3 cur = kFourJointPos[0];
        for (uint32_t i = 0; i < 3; ++i) {
            cur = cur + res.worldRot[i] * (kFourJointPos[i + 1] - kFourJointPos[i]);
            CHECK_FLOAT_EQ(cur.y, 0.0f, 1e-3f);
        }

        CHECK(res.worldRot[0].w < 0.999f);
    }

    // C2 — unreachable: chain stretches straight at the target, tip pulled
    // to the full chain length (0,0,30).
    TEST_CASE(solve_ccd_unreachable_stretches)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,50), 1.0f, 100);

        CHECK_FALSE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));

        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.z, 30.0f, 1e-2f);
    }

    // C3 — weight=0 → outputs bit-identical to inputs.
    TEST_CASE(solve_ccd_weight_zero_identity)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 0.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // C4 — weight=0.5 → tip lands on the interpolated point (15,0,10) —
    // goal-space blend aligned with FABRIK (F4).
    TEST_CASE(solve_ccd_weight_half_target_interpolation)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(0,0,20), 0.5f, 100);

        const FVector3 tip = chainTipAfter(kFourJointPos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 15.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.y, 0.0f, 1e-2f);
        CHECK_FLOAT_EQ(tip.z, 10.0f, 1e-2f);
    }

    // C5 — NaN input → outputs bit-identical to inputs (INV-74).
    TEST_CASE(solve_ccd_nan_input_noop)
    {
        FVector3 pos[4] = { {0,0,0}, {10,0,0}, {20,0,0}, {30,0,0} };
        const float qnan = std::numeric_limits<float>::quiet_NaN();
        pos[2].z = qnan;

        const IterativeIKResult res = CcdSolver::solve(
            pos, kFourJointRot, 4, FVector3(0,0,20), 1.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // C6 — zero-length segment: that joint is left unchanged, the rest of
    // the chain still solves.
    TEST_CASE(solve_ccd_zero_length_segment_skips)
    {
        const FVector3 pos[4] = { {0,0,0}, {10,0,0}, {10,0,0}, {30,0,0} };
        const FQuaternion rot[4] = { FQuaternion::identity(), FQuaternion::identity(),
                                     FQuaternion::identity(), FQuaternion::identity() };

        const IterativeIKResult res = CcdSolver::solve(pos, rot, 4, FVector3(0,0,20), 1.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayFinite(res.worldRot, 4));
        CHECK_TRUE(quatBitSame(res.worldRot[1], FQuaternion::identity()));

        // The skipped zero-length joint leaves one lever arm shorter —
        // residual ~1.7e-2 after 100 passes (measured; deterministic).
        const FVector3 tip = chainTipAfter(pos, res.worldRot, 4);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 3e-2f);
        CHECK_FLOAT_EQ(tip.z, 20.0f, 3e-2f);
    }

    // C7 — target at the root → outputs bit-identical to inputs.
    TEST_CASE(solve_ccd_target_at_root_noop)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, kFourJointPos[0], 1.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // C8 — 2-joint chain: single rotation aligns the segment with the
    // target; tip hits (0,0,10).
    TEST_CASE(solve_ccd_two_joint_single_segment)
    {
        const FVector3 pos[2] = { {0,0,0}, {10,0,0} };
        const FQuaternion rot[2] = { FQuaternion::identity(), FQuaternion::identity() };

        const IterativeIKResult res = CcdSolver::solve(pos, rot, 2, FVector3(0,0,10), 1.0f, 100);

        CHECK_TRUE(res.reachable);
        const FVector3 tip = chainTipAfter(pos, res.worldRot, 2);
        CHECK_FLOAT_EQ(tip.x, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(tip.z, 10.0f, 1e-3f);
    }

    // C9 — collinear chain + target on the chain line BEYOND it (unreach-
    // able): every joint has a zero rotation axis → outputs bit-identical
    // to inputs, reachable=false. Documented asymmetry: FABRIK CAN stretch
    // this chain (F2), CCD cannot (header + design §4.26.5).
    TEST_CASE(solve_ccd_collinear_unreachable_no_bend)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(40,0,0), 1.0f, 100);

        CHECK_FALSE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // C10 — collinear chain + target ON the chain line INSIDE reach:
    // geometrically reachable (reachable=true) but no joint has a rotation
    // axis → outputs bit-identical to inputs. Pins that `reachable` is a
    // necessary, NOT sufficient diagnostic.
    TEST_CASE(solve_ccd_collinear_reachable_but_no_bend)
    {
        const IterativeIKResult res = CcdSolver::solve(
            kFourJointPos, kFourJointRot, 4, FVector3(25,0,0), 1.0f, 100);

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(rotArrayBitSame(res.worldRot, kFourJointRot, 4));
    }

    // C11 — count=1 → outputs bit-identical to inputs.
    TEST_CASE(solve_ccd_count_one_noop)
    {
        const FVector3 pos[1] = { {5,5,5} };
        const FQuaternion rot[1] = { FQuaternion::fromAxisAngle(FVector3(0,1,0), 0.3f) };

        const IterativeIKResult res = CcdSolver::solve(pos, rot, 1, FVector3(1,2,3));

        CHECK_TRUE(res.reachable);
        CHECK_TRUE(quatBitSame(res.worldRot[0], rot[0]));
    }
TEST_SUITE_END

// ===========================================================================
// I 系列 — AnimationPlayer Phase 2.5 unified-pass integration (INV-75/76/77).
// FABRIK / CCD chains carry only rootBone + tipBone; the bone path is
// auto-derived at resolve time. All players run a no-track anim (rest pose
// every frame).
// ===========================================================================
TEST_SUITE(AnimationPlayerIterativeIKTests)
    // I1 — FABRIK 6-joint chain, auto-derived 5-segment path (midBone
    // EMPTY): tip lands on the target, root stays anchored, bend stays in
    // the XZ plane, every matrix finite. Tolerance 1e-2: FABRIK's default
    // 4 iterations leave a residual ~4e-3 on a 5-segment chain (measured,
    // deterministic — convergence slows with segment count; 3-segment
    // chains reach 1e-3, pinned by F1).
    TEST_CASE(ik_fabrik_auto_path_hits_target)
    {
        Skeleton skel = makeChainSkeleton(6);   // rest joints (0..50, 0, 0)
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,35))));
        CHECK_TRUE(player.isIKChainActive(0));
        player.play(&anim);
        player.evaluate();

        const FVector3 rp = worldPos(player, 0);
        CHECK_FLOAT_EQ(0.0f, rp.x, 1e-3f);   // anchor preserved
        CHECK_FLOAT_EQ(0.0f, rp.y, 1e-3f);
        CHECK_FLOAT_EQ(0.0f, rp.z, 1e-3f);
        const FVector3 tp = worldPos(player, 5);
        CHECK_FLOAT_EQ(0.0f,  tp.x, 1e-2f);
        CHECK_FLOAT_EQ(0.0f,  tp.y, 1e-2f);
        CHECK_FLOAT_EQ(35.0f, tp.z, 1e-2f);
        CHECK_TRUE(worldFinite(player));
    }

    // I2 — CCD auto-derived path with EXPLICIT iterations=40: the spec's
    // `iterations` field reaches the solver (CCD converges linearly, so the
    // tolerance is looser than FABRIK's — measured, deterministic).
    TEST_CASE(ik_ccd_auto_path_explicit_iterations)
    {
        Skeleton skel = makeChainSkeleton(4);   // rest joints (0..30, 0, 0)
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::CCD, 40, "Root", "Tip", FVector3(0,0,20))));
        CHECK_TRUE(player.isIKChainActive(0));
        player.play(&anim);
        player.evaluate();

        const FVector3 tp = worldPos(player, 3);
        CHECK_FLOAT_EQ(0.0f,  tp.x, 5e-2f);
        CHECK_FLOAT_EQ(0.0f,  tp.y, 5e-2f);
        CHECK_FLOAT_EQ(20.0f, tp.z, 5e-2f);
        CHECK_TRUE(worldFinite(player));
    }

    // I3 — spec round-trip: type + iterations survive bind and per-frame
    // hot updates (target / weight setters never touch them).
    TEST_CASE(ik_spec_roundtrip_type_and_iterations)
    {
        Skeleton skel = makeChainSkeleton(4);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::CCD, 40, "Root", "Tip", FVector3(0,0,20))));

        const IKChainSpec& s = player.getIKChain(0);
        CHECK_TRUE(s.type == IKSolverType::CCD);
        CHECK_TRUE(s.iterations == 40);

        player.setIKChainTarget(0, FVector3(0,0,18));
        player.setIKChainWeight(0, 0.7f);
        const IKChainSpec& s2 = player.getIKChain(0);
        CHECK_TRUE(s2.type == IKSolverType::CCD);       // untouched
        CHECK_TRUE(s2.iterations == 40);                // untouched
        CHECK_FLOAT_EQ(0.7f, s2.weight, 0.0f);
    }

    // I4 — player-level weight=0.5: FABRIK blends in GOAL space, so the tip
    // lands EXACTLY on lerp(restTip, target, 0.5) = (10, 0, 7.5) when
    // reachable (INV-75 — the solver-side contract, visible at player level).
    TEST_CASE(ik_weight_half_goal_space_blend)
    {
        Skeleton skel = makeChainSkeleton(3);   // 2 segments, rest tip (20,0,0)
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,15), 0.5f)));
        player.play(&anim);
        player.evaluate();

        const FVector3 tp = worldPos(player, 2);
        CHECK_FLOAT_EQ(10.0f, tp.x, 1e-3f);   // exact interpolation point
        CHECK_FLOAT_EQ(0.0f,  tp.y, 1e-3f);
        CHECK_FLOAT_EQ(7.5f,  tp.z, 1e-3f);
        CHECK_TRUE(worldFinite(player));
    }

    // I5 — three solver types coexist on ONE skeleton (re-pins the P7
    // anchor lesson: targets of later chains must account for earlier
    // chains having moved shared bones). TwoBone LRoot→LMid→LTip pulls
    // LTip to (0,0,15); the CCD chain rooted at LTip then targets
    // (10,0,20) — reachable FROM THE MOVED LTip, not from rest.
    TEST_CASE(ik_three_solver_types_coexist)
    {
        Skeleton skel = makeEightBoneSkeleton();
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        // chain 0: TwoBone (default type — legacy 3-name spec).
        IKChainSpec tb;
        tb.rootBone = "LRoot"; tb.midBone = "LMid"; tb.tipBone = "LTip";
        tb.targetWorld = FVector3(0,0,15);
        CHECK_TRUE(player.setIKChain(0, tb));
        // chain 1: FABRIK on the RIGHT subtree (independent of chain 0).
        CHECK_TRUE(player.setIKChain(1, makeItSpec(
            IKSolverType::FABRIK, 0, "RRoot", "RTip", FVector3(30,0,15))));
        // chain 2: CCD rooted at LTip (moved by chain 0), explicit 100 iters.
        CHECK_TRUE(player.setIKChain(2, makeItSpec(
            IKSolverType::CCD, 100, "LTip", "LTipChild2", FVector3(10,0,20))));
        player.play(&anim);
        player.evaluate();

        // chain 0 hit: LTip on (0,0,15) — later chains never move it back.
        const FVector3 tip = worldPos(player, 2);
        CHECK_FLOAT_EQ(0.0f,  tip.x, 1e-3f);
        CHECK_FLOAT_EQ(0.0f,  tip.y, 1e-3f);
        CHECK_FLOAT_EQ(15.0f, tip.z, 1e-3f);
        // chain 1 hit: RTip on (30,0,15).
        const FVector3 rt = worldPos(player, 7);
        CHECK_FLOAT_EQ(30.0f, rt.x, 1e-3f);
        CHECK_FLOAT_EQ(0.0f,  rt.y, 1e-3f);
        CHECK_FLOAT_EQ(15.0f, rt.z, 1e-3f);
        // chain 2 hit: LTipChild2 on (10,0,20) (measured residual, CCD).
        const FVector3 c2 = worldPos(player, 4);
        CHECK_FLOAT_EQ(10.0f, c2.x, 3e-2f);
        CHECK_FLOAT_EQ(0.0f,  c2.y, 3e-2f);
        CHECK_FLOAT_EQ(20.0f, c2.z, 3e-2f);
        CHECK_TRUE(worldFinite(player));
    }

    // I6 — target / weight hot updates never re-resolve: generation stays
    // pinned while the blended goal is hit next evaluate. weight=0.8 blends
    // in GOAL space (INV-75), so the tip lands EXACTLY on
    // lerp(restTip(30,0,0), target(0,0,15), 0.8) = (6, 0, 12).
    TEST_CASE(ik_hot_update_no_generation_bump)
    {
        Skeleton skel = makeChainSkeleton(4);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,20))));
        const uint32_t g0 = player.getIKChainGeneration();

        player.setIKChainTarget(0, FVector3(0,0,15));
        player.setIKChainWeight(0, 0.8f);
        CHECK_TRUE(player.getIKChainGeneration() == g0);   // no re-resolve
        player.play(&anim);
        player.evaluate();

        const FVector3 tp = worldPos(player, 3);
        CHECK_FLOAT_EQ(6.0f,  tp.x, 1e-3f);   // exact interpolation point
        CHECK_FLOAT_EQ(0.0f,  tp.y, 1e-3f);
        CHECK_FLOAT_EQ(12.0f, tp.z, 1e-3f);
        CHECK_TRUE(worldFinite(player));
    }

    // I7 — unknown bone names: path stays empty (disabled), world output
    // bit-identical to a chain-less player, no crash (memcmp guard).
    TEST_CASE(ik_unknown_bone_disabled_memcmp)
    {
        Skeleton skel = makeChainSkeleton(4);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer base;
        base.setSkeleton(sharedFromLocal(skel));
        base.play(&anim);
        base.evaluate();

        AnimationPlayer ik;
        ik.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(ik.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "NoSuchRoot", "NoSuchTip", FVector3(0,0,20))));
        CHECK_FALSE(ik.isIKChainActive(0));
        ik.play(&anim);
        ik.evaluate();

        CHECK(std::memcmp(ik.getBoneWorldMatrices(), base.getBoneWorldMatrices(),
                          base.getBoneCount() * sizeof(Float4x4)) == 0);
    }

    // I8 — setSkeleton re-resolve (INV-71): a same-named skeleton revives
    // the chain (still hits), a differently-named skeleton disables it in
    // place (no crash); generation bumps on every bind / re-resolve.
    TEST_CASE(ik_skeleton_swap_reresolves)
    {
        Skeleton skel = makeChainSkeleton(4);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,20))));
        const uint32_t g0 = player.getIKChainGeneration();
        CHECK_TRUE(g0 > 0);

        // Same-named skeleton → revives, still hits.
        player.setSkeleton(sharedFromLocal(makeChainSkeleton(4)));
        CHECK_TRUE(player.getIKChainGeneration() > g0);
        CHECK_TRUE(player.isIKChainActive(0));
        player.play(&anim);
        player.evaluate();
        const FVector3 tp = worldPos(player, 3);
        CHECK_FLOAT_EQ(0.0f, tp.x, 1e-3f);
        CHECK_FLOAT_EQ(20.0f, tp.z, 1e-3f);

        // Differently-named skeleton → disabled, no crash.
        player.setSkeleton(sharedFromLocal(makeEightBoneSkeleton()));
        CHECK_FALSE(player.isIKChainActive(0));
        player.evaluate();
        CHECK_TRUE(worldFinite(player));
    }

    // I9 — topology failures disable in place: (a) tip NOT a descendant of
    // root (different subtree — walk reaches the top without seeing root),
    // (b) root == tip (path < 2 joints). Both no-crash + world identical to
    // a chain-less player.
    TEST_CASE(ik_bad_topology_disabled)
    {
        Skeleton skel = makeEightBoneSkeleton();
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer base;
        base.setSkeleton(sharedFromLocal(skel));
        base.play(&anim);
        base.evaluate();

        AnimationPlayer ik;
        ik.setSkeleton(sharedFromLocal(skel));
        // (a) LRoot → RTip: different subtrees.
        CHECK_TRUE(ik.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "LRoot", "RTip", FVector3(0,0,15))));
        // (b) root == tip — same name resolves to one index.
        CHECK_TRUE(ik.setIKChain(1, makeItSpec(
            IKSolverType::CCD, 0, "LTip", "LTip", FVector3(10,0,20))));
        CHECK_FALSE(ik.isIKChainActive(0));
        CHECK_FALSE(ik.isIKChainActive(1));
        ik.play(&anim);
        ik.evaluate();

        CHECK(std::memcmp(ik.getBoneWorldMatrices(), base.getBoneWorldMatrices(),
                          base.getBoneCount() * sizeof(Float4x4)) == 0);
    }

    // I10 — 34-joint chain exceeds kMaxIKChainBones (32): resolve disables
    // it (over-cap), no crash, world identical to a chain-less player.
    TEST_CASE(ik_over_cap_chain_disabled)
    {
        Skeleton skel = makeChainSkeleton(34);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer base;
        base.setSkeleton(sharedFromLocal(skel));
        base.play(&anim);
        base.evaluate();

        AnimationPlayer ik;
        ik.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(ik.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,150))));
        CHECK_FALSE(ik.isIKChainActive(0));   // path > 32 joints → disabled
        ik.play(&anim);
        ik.evaluate();

        CHECK(std::memcmp(ik.getBoneWorldMatrices(), base.getBoneWorldMatrices(),
                          base.getBoneCount() * sizeof(Float4x4)) == 0);
    }

    // I11 — legacy spec WITHOUT the new fields defaults to TwoBone (enum
    // back-compat): a classic 3-name chain resolves and hits.
    TEST_CASE(ik_legacy_spec_defaults_to_twobone)
    {
        Skeleton skel = makeChainSkeleton(3);
        Animation anim; anim.setDuration(1.0f);

        IKChainSpec spec;   // aggregate-init style: no type / iterations
        spec.rootBone = "Root"; spec.midBone = "Bone1"; spec.tipBone = "Tip";
        spec.targetWorld = FVector3(0,0,10);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, spec));
        CHECK_TRUE(player.getIKChain(0).type == IKSolverType::TwoBone);
        player.play(&anim);
        player.evaluate();

        const FVector3 tp = worldPos(player, 2);
        CHECK_FLOAT_EQ(0.0f,  tp.x, 1e-3f);
        CHECK_FLOAT_EQ(0.0f,  tp.y, 1e-3f);
        CHECK_FLOAT_EQ(10.0f, tp.z, 1e-3f);
        CHECK_TRUE(worldFinite(player));
    }

    // I12 — no cross-frame state: seek back and forth, and stop() → play(),
    // every evaluate still hits (IK re-solves from scratch each frame).
    TEST_CASE(ik_seek_and_stop_keeps_hitting)
    {
        Skeleton skel = makeChainSkeleton(4);
        Animation anim; anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,20))));
        player.play(&anim);
        player.setTime(0.3f); player.evaluate();
        player.setTime(0.7f); player.evaluate();
        player.setTime(0.5f); player.evaluate();
        CHECK_FLOAT_EQ(20.0f, worldPos(player, 3).z, 1e-3f);

        player.stop();
        player.evaluate();
        CHECK_TRUE(worldFinite(player));
        player.play(&anim);           // rebind — chain survives
        player.evaluate();
        CHECK_FLOAT_EQ(20.0f, worldPos(player, 3).z, 1e-3f);
        CHECK_TRUE(worldFinite(player));
    }

    // I13 — midBone is IGNORED for iterative types: filling it with a REAL
    // bone name still yields the full auto-derived root→tip path (the
    // chain hits as in I1 — a mid-only solve would never reach (0,0,35)).
    TEST_CASE(ik_midbone_ignored_for_iterative)
    {
        Skeleton skel = makeChainSkeleton(6);
        Animation anim; anim.setDuration(1.0f);

        IKChainSpec spec = makeItSpec(
            IKSolverType::FABRIK, 0, "Root", "Tip", FVector3(0,0,35));
        spec.midBone = "Bone2";   // exists, but must be ignored

        AnimationPlayer player;
        player.setSkeleton(sharedFromLocal(skel));
        CHECK_TRUE(player.setIKChain(0, spec));
        player.play(&anim);
        player.evaluate();

        const FVector3 tp = worldPos(player, 5);
        CHECK_FLOAT_EQ(0.0f,  tp.x, 1e-2f);   // same FABRIK default-iteration
        CHECK_FLOAT_EQ(35.0f, tp.z, 1e-2f);   // residual as I1 (~4e-3)
        CHECK_TRUE(worldFinite(player));
    }
TEST_SUITE_END
