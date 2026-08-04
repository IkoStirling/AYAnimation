// AYTest_SkeletonMask.cpp — P2.2 (2026-08-03) acceptance cases.
//
// 16 cases pin every contract documented in design.md §4.13:
//   INV-12..17 (mask = post-write pre-Phase-2 lerp; rebinds on skeleton
//   swap; clamp on input; empty = identity / zero allocs; orthogonality
//   with P1.5 trackWeights; wildcard expansion).
//
// Test fixture reuses the P2.1 makeTwoBoneSkeletonShared style — hand-
// built Skeleton + Animation, no AYResource disk I/O, no World/Entity
// wiring. AYEntity bridge tests are in
// AYEntity/unittest/AYTest_SkeletonMaskBridge.cpp.

#include "AYAnimation.h"
#include <AYTest.h>

#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/ISkeletonMask.h>

#include "../src/SkeletonMask.h"   // P2.2 fixture (in-memory concrete)

#include <aymath/MathTypes.h>

#include <assetsDefs/IAYAnimation.h>
#include <assetsDefs/IAYSkeleton.h>
#include <assetsImpl/AYAnimation.h>
#include <assetsImpl/AYSkeleton.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace ayt::anim;
using ayt::math::FVector2;
using ayt::math::FVector3;
using ayt::math::FQuaternion;
using ayt::math::Float4x4;
using ayt::resource::Bone;
using ayt::resource::Skeleton;
using ayt::resource::Animation;
using ayt::resource::AnimTrack;
using ayt::resource::AnimTrackType;
using ayt::resource::AnimBlendMode;

namespace
{

// === Fixture helpers ===

// Build a fresh heap-allocated SkeletonMask for the tests. Using
// shared_ptr ensures the player's setSkeletonMask(shared_ptr) holds
// the mask alive across evaluate(); a stack-local SkeletonMask
// variable would UAF on scope exit (see INV-13 / API fix).
static std::shared_ptr<SkeletonMask> makeMask()
{
    return SkeletonMask::create();
}

// 2-bone skeleton at rest: Root=(0,0,0), Child=(0,0,0), both identity
// rotation, identity scale. Used by every override-track test.
std::shared_ptr<const ayt::resource::ISkeleton> makeTwoBoneSkelShared()
{
    auto s = std::make_shared<Skeleton>();
    s->setBoneCount(2);
    Bone root; root.name = "Root"; root.parentIndex = -1;
    root.localPosition  = FVector3(0, 0, 0);
    root.localRotation  = FQuaternion::identity();
    root.localScale     = FVector3(1, 1, 1);
    root.inverseBindMatrix = Float4x4::identity();
    s->setBone(0, root);

    Bone child; child.name = "Child"; child.parentIndex = 0;
    child.localPosition  = FVector3(0, 0, 0);
    child.localRotation  = FQuaternion::identity();
    child.localScale     = FVector3(1, 1, 1);
    child.inverseBindMatrix = Float4x4::identity();
    s->setBone(1, child);

    return std::static_pointer_cast<const ayt::resource::ISkeleton>(s);
}

// Override position track that drives Root to (10, 0, 0) over the clip
// duration. t=0 → (0,0,0); t=duration → (10,0,0). Default blendMode
// = Override so it replaces Root's local position entirely.
Animation makeRootPosOverride(float duration = 1.0f, float endX = 10.0f)
{
    Animation anim;
    anim.setName("RootPosOverride");
    anim.setTicksPerSecond(30.0f);
    anim.setDuration(duration);
    AnimTrack tr;
    tr.nodeName  = "Root";
    tr.property  = "position";
    tr.valueType = AnimTrackType::Vector3;
    tr.blendMode = AnimBlendMode::Override;
    tr.times  = { 0.0f, duration * 30.0f };
    tr.values = {
        0.0f, 0.0f, 0.0f,
        endX,  0.0f, 0.0f,
    };
    anim.addTrack(tr);
    return anim;
}

// Same as makeRootPosOverride but blendMode = Additive (delta on top
// of the rest pose). Used by mask × additive interaction tests.
Animation makeRootPosAdditive(float duration = 1.0f, float deltaX = 10.0f)
{
    Animation anim;
    anim.setName("RootPosAdditive");
    anim.setTicksPerSecond(30.0f);
    anim.setDuration(duration);
    AnimTrack tr;
    tr.nodeName  = "Root";
    tr.property  = "position";
    tr.valueType = AnimTrackType::Vector3;
    tr.blendMode = AnimBlendMode::Additive;   // <-- the difference
    tr.times  = { 0.0f, duration * 30.0f };
    tr.values = {
        0.0f, 0.0f, 0.0f,
        deltaX, 0.0f, 0.0f,
    };
    anim.addTrack(tr);
    return anim;
}

} // namespace

TEST_SUITE(SkeletonMaskTests)

    // ─── #1 — No mask = identity. Regression guard for INV-15. ────────
    TEST_CASE(mask_no_mask_means_identity_pose) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        CHECK(p.hasSkeletonMask() == false);
        CHECK(p.getSkeletonMaskBoneCount() == 0u);
        // Without a mask the Override track runs full strength on Root.
        // Read Root's world matrix translation: Root rest = (0,0,0);
        // anim at t=0.5 → (5,0,0); world[0].transformPoint((0,0,0)) = (5,0,0).
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);
        CHECK_FLOAT_EQ(worldRoot.y, 0.0f, 1e-3f);
        CHECK_FLOAT_EQ(worldRoot.z, 0.0f, 1e-3f);
    }

    // ─── #2 — Mask (Bone, 0.0f) snaps Override target bone to rest. ──
    // INV-12 path: Phase 2 pre-lerp uses w=0 → snap to rest.
    TEST_CASE(mask_single_bone_zero_weight_snaps_to_rest) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 0.0f);   // full suppression

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        CHECK(p.hasSkeletonMask());
        CHECK(p.getSkeletonMaskBoneCount() == 2u);
        // Root suppressed → worldRoot.x ≈ 0 (rest)
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 0.0f, 1e-3f);
    }

    // ─── #3 — Mask (Bone, 0.5f) halves the Override target. ───────────
    // INV-12 partial mask: lerp(rest=0, override=5, w=0.5) = 2.5.
    TEST_CASE(mask_single_bone_half_weight_halves_override) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 0.5f);

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 2.5f, 1e-3f);   // half of 5
    }

    // ─── #4 — Mask (Bone, 1.0f) is identity (no suppression). ─────────
    TEST_CASE(mask_single_bone_full_weight_one_is_identity) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 1.0f);

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);   // full Override
    }

    // ─── #5 — Wildcard applies to every bone NOT named explicitly. ───
    // INV-17: mask (Root=1.0, *=0.25). Root: full Override. Child:
    // still rest (no clip on it), but mask applies via wildcard.
    TEST_CASE(mask_wildcard_applies_to_unnamed_bones) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 1.0f);
        m->addEntry("",      0.25f);   // wildcard

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        // Root: mask=1.0 → full Override → (5,0,0).
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);

        // Child: mask=0.25, no clip track on it → rest=(0,0,0);
        // lerp(rest, rest, 0.25) = rest. worldChild = rest translation
        // because Child's parent (Root) is at world (5,0,0), so the
        // world position of Child's bind point is parent.world * rest
        // = (5,0,0). The wildcard-suppress mask doesn't affect the
        // bone identity (it only affects the lerp FROM rest), so
        // Child's world position still includes Root's animation.
        const FVector3 worldChild = w[1].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldChild.x, 5.0f, 1e-3f);   // inherits Root's x
    }

    // ─── #6 — Named entry wins over wildcard (explicit > implicit). ──
    // INV-17: wildcard=0.5, but Root=0.0 → Root snapped to rest.
    TEST_CASE(mask_wildcard_does_not_override_named_entry) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 0.0f);
        m->addEntry("",      0.5f);   // wildcard

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 0.0f, 1e-3f);   // named (0.0) wins
    }

    // ─── #7 — Mask × Additive: mask (Root=0) kills the additive delta. ──
    TEST_CASE(mask_with_additive_track_phase_1b) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation baseClip = makeRootPosOverride(1.0f, 0.0f);
        Animation clip = makeRootPosAdditive(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 0.0f);

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&baseClip);
        // loop=false: setTime seeks both axes; no need to tick (and
        // loop=true + tick(dt) that lands on duration wraps additive
        // playhead to 0 — silent rest sample).
        p.setAdditiveSource(&clip, 1.0f, false);
        p.setTime(0.5f);
        p.evaluate();

        // Additive delta at t=0.5 is +5; Phase 2 mask=0 snaps to rest.
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 0.0f, 1e-3f);
    }

    // ─── #8 — INV-16: mask multiplies with P1.5 trackWeights. ─────────
    // P1.5 slot[0] trackWeights = {0.5} (track 0 only);
    // P2.2 mask Root = 0.5.
    //
    // Without mask: base Override writes rest (0,0,0); additive at t=0.5
    // has delta +5; effective write weight 1.0 * 0.5 (trackWeight) = 0.5;
    // _localPos = (5*0.5, 0, 0) = (2.5, 0, 0).
    //
    // With mask Root=0.5: Phase 2 pre-lerp applies
    //   lerp(rest=0, _local=2.5, w=0.5) = 1.25.
    //
    // We compare against the no-mask expected (2.5) to assert the mask
    // half-suppressed the post-write TRS — the multiplicative
    // composition INV-16 is verifiable by the 0.5 ratio (1.25 == 2.5 * 0.5).
    //
    // The test deliberately uses a separate fresh `SkeletonMask m`
    // allocated via SkeletonMask::create() (a heap-allocated shared_ptr)
    // rather than a stack-local mask — the previous stack-local variant
    // confused debug-build stack-reuse detection. SkeletonMask::create
    // returns a shared_ptr that the test owns for its full lifetime.
    TEST_CASE(mask_multiplies_with_track_weights) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation baseClip = makeRootPosOverride(1.0f, 0.0f);   // base writes rest
        Animation clip = makeRootPosAdditive(1.0f, 10.0f);

        // Heap-allocated mask via SkeletonMask::create() — player holds
        // a shared_ptr copy for the evaluate lifetime.
        auto maskHandle = ayt::anim::SkeletonMask::create();
        maskHandle->addEntry("Root", 0.5f);
        maskHandle->setDebugName("P22_test8_multiply");

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(maskHandle);
        p.play(&baseClip);
        // Seek via setTime (covers base + additive). Do NOT tick past
        // duration with loop=true — independent additive wrap maps
        // t=duration → 0 and samples rest.
        p.setAdditiveSource(&clip, 1.0f, false);
        p.setAdditiveLayerTrackWeights(0, { 0.5f });
        p.setTime(0.5f);
        p.evaluate();

        const Float4x4* wm = p.getBoneWorldMatrices();
        const FVector3 worldRoot = wm[0].transformPoint(FVector3(0, 0, 0));
        // trackWeights=0.5 → additive delta 5 * 0.5 → _localPos=2.5;
        // Phase 2 lerp(rest=0, 2.5, mask=0.5) = 1.25.
        CHECK_FLOAT_EQ(worldRoot.x, 1.25f, 1e-3f);
    }

    // ─── #9 — INV-13: mask rebinds on skeleton swap. ─────────────────
    // Bind mask to "Root"; swap skeleton where Root still exists but
    // with a different IBM. Mask must still apply on the new bone.
    TEST_CASE(mask_resolves_again_after_skeleton_swap) {
        auto skelA = makeTwoBoneSkelShared();

        Skeleton skelB;
        skelB.setBoneCount(2);
        Bone root; root.name = "Root"; root.parentIndex = -1;
        root.localPosition  = FVector3(0, 0, 0);
        root.localRotation  = FQuaternion::identity();
        root.localScale     = FVector3(1, 1, 1);
        root.inverseBindMatrix = Float4x4::identity();
        skelB.setBone(0, root);
        Bone child; child.name = "OtherBone"; child.parentIndex = 0;   // NEW name
        child.localPosition  = FVector3(0, 0, 0);
        child.localRotation  = FQuaternion::identity();
        child.localScale     = FVector3(1, 1, 1);
        child.inverseBindMatrix = Float4x4::identity();
        skelB.setBone(1, child);
        auto skelBSP = std::static_pointer_cast<const ayt::resource::ISkeleton>(
            std::make_shared<Skeleton>(skelB));

        auto m = makeMask();
        m->addEntry("Root", 0.0f);   // suppresses Root on either skeleton

        AnimationPlayer p;
        p.setSkeleton(skelA);
        p.setSkeletonMask(m);
        const uint32_t genAfterFirstSet = p.getSkeletonMaskGeneration();

        // Swap skeleton. The mask entry "Root" still exists in skelB →
        // resolve must keep mask applied.
        p.setSkeleton(skelBSP);
        const uint32_t genAfterSwap = p.getSkeletonMaskGeneration();

        CHECK(genAfterSwap == genAfterFirstSet + 1u);
        CHECK(p.getSkeletonMaskBoneCount() == 2u);
        // Entry "Root" is still at idx 0 in the new skeleton → still suppressed.
        const auto& w = p.getResolvedBoneMaskWeights();
        CHECK_FLOAT_EQ(w[0], 0.0f, 1e-5f);   // Root: explicit 0.0
        CHECK_FLOAT_EQ(w[1], 1.0f, 1e-5f);   // OtherBone: default identity
    }

    // ─── #10 — clearSkeletonMask restores identity. ──────────────────
    TEST_CASE(clear_skeleton_mask_restores_identity) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("Root", 0.0f);

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        const uint32_t genBefore = p.getSkeletonMaskGeneration();
        p.clearSkeletonMask();
        const uint32_t genAfter = p.getSkeletonMaskGeneration();

        CHECK(genAfter == genBefore + 1u);
        CHECK(p.hasSkeletonMask() == false);
        CHECK(p.getSkeletonMaskBoneCount() == 0u);

        // No mask → Root full Override.
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);
    }

    // ─── #11 — Unresolved bone name is silently skipped. ─────────────
    TEST_CASE(mask_unresolved_bone_name_is_silently_skipped) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();
        m->addEntry("NoSuchBone", 0.0f);   // bogus entry
        // No entry on Root → Root defaults to 1.0 → full Override.

        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();

        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);
    }

    // ─── #12 — Defensive: setMask before skeleton is safe. ───────────
    TEST_CASE(mask_with_no_skeleton_bound_is_safe) {
        auto m = makeMask();
        m->addEntry("Root", 0.0f);

        AnimationPlayer p;
        // No setSkeleton yet.
        p.setSkeletonMask(m);
        CHECK(p.hasSkeletonMask());
        // No skeleton → resolveSkeletonMask leaves _boneMaskWeights empty.
        CHECK(p.getSkeletonMaskBoneCount() == 0u);

        // Now bind a skeleton — resolve fires in setSkeleton → mask applied.
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);
        p.setSkeleton(skelSP);
        CHECK(p.getSkeletonMaskBoneCount() == 2u);
        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();
        const Float4x4* w = p.getBoneWorldMatrices();
        const FVector3 worldRoot = w[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 0.0f, 1e-3f);   // mask 0.0 → snapped to rest
    }

    // ─── #13 — INV-14: addEntry clamps to [0, 1]. ────────────────────
    TEST_CASE(mask_clamp_in_addentry) {
        auto m = makeMask();
        m->addEntry("A", -0.5f);
        m->addEntry("B",  2.0f);
        const auto* entries = m->getEntries();
        // Entries are stored in insertion order.
        CHECK_FLOAT_EQ(entries[0].weight, 0.0f, 1e-6f);
        CHECK_FLOAT_EQ(entries[1].weight, 1.0f, 1e-6f);
    }

    // ─── #14 — Generation counter increments on rebind. ──────────────
    TEST_CASE(mask_generation_counter_increments) {
        auto skelSP = makeTwoBoneSkelShared();
        AnimationPlayer p;
        p.setSkeleton(skelSP);
        const uint32_t gen0 = p.getSkeletonMaskGeneration();

        auto m = makeMask();
        m->addEntry("Root", 0.5f);
        p.setSkeletonMask(m);
        const uint32_t gen1 = p.getSkeletonMaskGeneration();
        CHECK(gen1 == gen0 + 1u);

        p.setSkeletonMask(m);     // same mask → resolve again → +1
        const uint32_t gen2 = p.getSkeletonMaskGeneration();
        CHECK(gen2 == gen1 + 1u);

        p.clearSkeletonMask();
        const uint32_t gen3 = p.getSkeletonMaskGeneration();
        CHECK(gen3 == gen2 + 1u);
    }

    // ─── #15 — Empty mask (no entries, no wildcard) = identity. ──────
    TEST_CASE(empty_mask_entries_equal_identity) {
        auto skelSP = makeTwoBoneSkelShared();
        Animation clip = makeRootPosOverride(1.0f, 10.0f);

        auto m = makeMask();   // zero entries
        AnimationPlayer p;
        p.setSkeleton(skelSP);
        p.setSkeletonMask(m);
        CHECK(p.getSkeletonMaskBoneCount() == 2u);
        // Every bone defaults to 1.0 → full Override on Root.
        const auto& w = p.getResolvedBoneMaskWeights();
        CHECK_FLOAT_EQ(w[0], 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(w[1], 1.0f, 1e-6f);

        p.play(&clip);
        p.setTime(0.5f);
        p.evaluate();
        const Float4x4* wm = p.getBoneWorldMatrices();
        const FVector3 worldRoot = wm[0].transformPoint(FVector3(0, 0, 0));
        CHECK_FLOAT_EQ(worldRoot.x, 5.0f, 1e-3f);
    }

    // ─── #16 — Duplicate name: last wins. ────────────────────────────
    TEST_CASE(mask_duplicate_name_takes_last_weight) {
        auto m = makeMask();
        m->addEntry("Root", 0.0f);
        m->addEntry("Root", 0.7f);
        const auto* entries = m->getEntries();
        CHECK(m->getAuthoredBoneCount() == 1u);
        CHECK_FLOAT_EQ(entries[0].weight, 0.7f, 1e-6f);
    }

TEST_SUITE_END
