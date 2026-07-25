// AYTest_AnimationPlayer.cpp — AN-01 / P0 (2026-07-26) acceptance cases.
//
// P0 changes:
//   - Helpers build ayt::resource::Skeleton / ayt::resource::Animation
//     (instead of the now-deleted parallel types).
//   - AnimationPlayer is bound via setSkeleton(ISkeleton*) / play(IAnimation*).
//   - Added cases for: Float track sink, IBM=0 safe, topology invariant.
//
// Drives the AnimationPlayer with hand-built ISkeleton + IAnimation, then
// verifies the 3-phase evaluate output and the time-management contract.

#include "AYAnimation.h"
#include <AYTest.h>
#include <aymath/MathTypes.h>
#include <aymath/MathUtils.h>

#include <assetsDefs/IAYAnimation.h>
#include <assetsDefs/IAYSkeleton.h>
#include <assetsImpl/AYSkeleton.h>
#include <assetsImpl/AYAnimation.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using ayt::resource::Bone;
using ayt::resource::Skeleton;
using ayt::resource::Animation;
using ayt::resource::AnimTrack;
using ayt::resource::AnimTrackType;

namespace
{

// Build a 2-bone skeleton: Root (no parent) → Child (parent=0).
// Both bones have rest pose at the origin with identity rotation and unit scale.
Skeleton makeTwoBoneSkeleton()
{
    Skeleton skel;
    skel.setBoneCount(2);

    Bone root;
    root.name = "Root";
    root.parentIndex = -1;
    root.localPosition  = FVector3(0,0,0);
    root.localRotation  = FQuaternion::identity();
    root.localScale     = FVector3(1,1,1);
    root.inverseBindMatrix = Float4x4::identity();
    skel.setBone(0, root);

    Bone child;
    child.name = "Child";
    child.parentIndex = 0;
    child.localPosition  = FVector3(0,0,0);
    child.localRotation  = FQuaternion::identity();
    child.localScale     = FVector3(1,1,1);
    child.inverseBindMatrix = Float4x4::identity();
    skel.setBone(1, child);

    return skel;
}

// Build a position track on "Root": (0,0,0) at t=0, (10,0,0) at t=1.
AnimTrack makeRootPosTrack()
{
    AnimTrack tr;
    tr.nodeName = "Root";
    tr.property = "position";
    tr.valueType = AnimTrackType::Vector3;
    tr.times  = { 0.0f, 30.0f };   // ticks (tps=30 → 0s, 1s)
    tr.values = {
        0.0f, 0.0f, 0.0f,
        10.0f, 0.0f, 0.0f,
    };
    return tr;
}

// Build a Float track on "Spine" with property "weight" — exercises the
// Float track sink exit path.
AnimTrack makeFloatWeightTrack()
{
    AnimTrack tr;
    tr.nodeName = "Spine";
    tr.property = "weight";
    tr.valueType = AnimTrackType::Float;
    tr.times  = { 0.0f, 60.0f };          // ticks (tps=30 → 0s, 2s)
    tr.values = { 0.0f, 100.0f };
    return tr;
}

} // namespace

TEST_SUITE(AnimationPlayerTests)

    TEST_CASE(rest_pose_at_t_zero_with_no_animation) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setDuration(1.0f);
        // No tracks attached.

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.evaluate();

        // Both bones: world = identity (rest pose has identity TRS).
        const Float4x4* world = player.getBoneWorldMatrices();
        CHECK(world != nullptr);
        const size_t n = player.getBoneCount();
        CHECK(n == 2);
        for (size_t i = 0; i < n; ++i) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    const float expected = (r == c) ? 1.0f : 0.0f;
                    CHECK_FLOAT_EQ(world[i](r, c), expected, 1e-5f);
                }
            }
        }
    }

    TEST_CASE(position_track_lerps_bone_translation) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);
        anim.addTrack(makeRootPosTrack());

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setTime(0.5f);
        player.evaluate();

        // Root's local position at t=0.5 should be (5,0,0).
        const Float4x4 rootWorld = player.getBoneWorldMatrices()[0];
        const FVector3 worldT = rootWorld.transformPoint(FVector3(0,0,0));
        CHECK_FLOAT_EQ(worldT.x, 5.0f, 1e-4f);

        // Child has no track — local TRS stays at identity, so child
        // world = root world (child follows root). Verify it follows.
        const Float4x4 childWorld = player.getBoneWorldMatrices()[1];
        const FVector3 childT = childWorld.transformPoint(FVector3(0,0,0));
        CHECK_FLOAT_EQ(childT.x, 5.0f, 1e-4f);
        CHECK_FLOAT_EQ(childT.y, 0.0f, 1e-4f);
        CHECK_FLOAT_EQ(childT.z, 0.0f, 1e-4f);
    }

    TEST_CASE(parent_child_world_composition) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);
        anim.addTrack(makeRootPosTrack());

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setTime(1.0f);   // End of clip → Root at (10,0,0)
        player.evaluate();

        const Float4x4 rootWorld = player.getBoneWorldMatrices()[0];
        const Float4x4 childWorld = player.getBoneWorldMatrices()[1];

        // Child world == Root world (child's local is identity).
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                CHECK_FLOAT_EQ(childWorld(r, c), rootWorld(r, c), 1e-4f);
            }
        }
    }

    TEST_CASE(missing_track_uses_rest_pose) {
        // Skeleton has bone "Spine" with rest translation (1, 2, 3).
        Skeleton skel;
        skel.setBoneCount(1);
        Bone spine;
        spine.name = "Spine";
        spine.parentIndex = -1;
        spine.localPosition  = FVector3(1, 2, 3);
        spine.localRotation  = FQuaternion::identity();
        spine.localScale     = FVector3(1, 1, 1);
        spine.inverseBindMatrix = Float4x4::identity();
        skel.setBone(0, spine);

        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);
        // No tracks.

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.evaluate();

        const Float4x4 spineWorld = player.getBoneWorldMatrices()[0];
        const FVector3 t = spineWorld.transformPoint(FVector3(0,0,0));
        CHECK_FLOAT_EQ(t.x, 1.0f, 1e-5f);
        CHECK_FLOAT_EQ(t.y, 2.0f, 1e-5f);
        CHECK_FLOAT_EQ(t.z, 3.0f, 1e-5f);
    }

    TEST_CASE(loop_wraps_time) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(2.0f);

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.tick(2.5f);   // Should wrap to 0.5
        CHECK_FLOAT_EQ(player.getTime(), 0.5f, 1e-5f);
    }

    TEST_CASE(skin_matrix_equals_world_times_inverse_bind) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);
        anim.addTrack(makeRootPosTrack());

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setTime(0.5f);
        player.evaluate();

        const Float4x4* world = player.getBoneWorldMatrices();
        const Float4x4* skin  = player.getBoneSkinMatrices();
        const Bone*     bones = skel.getBones();
        for (size_t i = 0; i < skel.getBoneCount(); ++i) {
            const Float4x4 expected = world[i] * bones[i].inverseBindMatrix;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    CHECK_FLOAT_EQ(skin[i](r, c), expected(r, c), 1e-4f);
                }
            }
        }
    }

    // P0 (2026-07-26): Float track drives host sink. The test registers
    // a sink that captures the last-pushed (nodeName, property, value)
    // triple, advances the player into the middle of the Float track,
    // and verifies the sink was called with the expected interpolated value.
    TEST_CASE(float_track_drives_host_sink) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(2.0f);
        anim.addTrack(makeFloatWeightTrack());

        struct SinkCapture {
            std::string nodeName;
            std::string property;
            float       value     = 0.0f;
            int         callCount = 0;
        } cap;

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setFloatCurveSink([&](const char* nodeName,
                                    const char* property,
                                    float value) {
            cap.nodeName  = nodeName  ? nodeName  : "";
            cap.property  = property  ? property  : "";
            cap.value     = value;
            cap.callCount += 1;
        });
        player.setTime(1.0f);   // halfway through the [0, 2s] clip → value = 50
        player.evaluate();

        CHECK(cap.callCount == 1);
        CHECK(cap.nodeName == "Spine");
        CHECK(cap.property == "weight");
        CHECK_FLOAT_EQ(cap.value, 50.0f, 1e-4f);
    }

    // P0 (2026-07-26): Float track sink works even when the bone name
    // does NOT resolve to a skeleton bone (e.g. a gameplay parameter
    // like "attack_damage" that lives outside the skeleton hierarchy).
    TEST_CASE(float_track_sink_fires_for_orphan_track) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(2.0f);

        AnimTrack tr;
        tr.nodeName  = "attack_damage";   // not a bone name
        tr.property  = "value";
        tr.valueType = AnimTrackType::Float;
        tr.times     = { 0.0f, 60.0f };
        tr.values    = { 0.0f, 100.0f };
        anim.addTrack(tr);

        struct SinkCapture {
            int   callCount = 0;
            float value     = 0.0f;
        } cap;

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setFloatCurveSink([&](const char*, const char*, float v) {
            cap.callCount += 1;
            cap.value = v;
        });
        player.setTime(1.0f);
        player.evaluate();

        CHECK(cap.callCount == 1);
        CHECK_FLOAT_EQ(cap.value, 50.0f, 1e-4f);
    }

    // P0 (2026-07-26): Float track without a registered sink must NOT
    // crash. (Pre-P0 the value was silently discarded; post-P0 the player
    // simply does nothing if no sink is registered.)
    TEST_CASE(float_track_without_sink_does_not_crash) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(2.0f);
        anim.addTrack(makeFloatWeightTrack());

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        // Note: no sink registered.
        player.setTime(1.0f);
        player.evaluate();
        CHECK(true);  // reaching here = no crash
    }

    // P0 (2026-07-26): matrix convention lock. Build a 1-bone skeleton
    // whose IBM is exactly bindWorld.inverse() (i.e. the demo convention).
    // At rest pose the skin matrix MUST be identity — this is the test
    // that locks the row/col convention used everywhere.
    TEST_CASE(skin_matrix_is_identity_at_rest_pose) {
        Skeleton skel;
        skel.setBoneCount(1);

        // Bind pose: bone at local (1, 2, 3), parent = root, IBM = bindWorld.inverse().
        const FVector3    bindLocal(1, 2, 3);
        const FQuaternion bindRot   = FQuaternion::identity();
        const FVector3    bindScale (1, 1, 1);
        const Float4x4 bindWorld = Float4x4::fromTRS(bindLocal, bindRot, bindScale);
        const Float4x4 ibm       = bindWorld.inverse();

        Bone root;
        root.name = "root";
        root.parentIndex = -1;
        root.localPosition  = bindLocal;
        root.localRotation  = bindRot;
        root.localScale     = bindScale;
        root.inverseBindMatrix = ibm;
        skel.setBone(0, root);

        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.evaluate();

        const Float4x4 skin = player.getBoneSkinMatrices()[0];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const float expected = (r == c) ? 1.0f : 0.0f;
                CHECK_FLOAT_EQ(skin(r, c), expected, 1e-4f);
            }
        }
    }

    // P0 (2026-07-26): IBM = 0 must not propagate NaN — the eval must
    // either skip the bone or clamp; either way the resulting matrix
    // entries must be finite.
    TEST_CASE(inverse_bind_zero_does_not_propagate_nan) {
        Skeleton skel;
        skel.setBoneCount(1);

        Bone root;
        root.name = "root";
        root.parentIndex = -1;
        root.localPosition  = FVector3(0,0,0);
        root.localRotation  = FQuaternion::identity();
        root.localScale     = FVector3(1,1,1);
        // Zero IBM — pathological input. Evaluate must not NaN-out.
        root.inverseBindMatrix = Float4x4::zero();
        skel.setBone(0, root);

        Animation anim;
        anim.setTicksPerSecond(30.0f);
        anim.setDuration(1.0f);

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.evaluate();

        const Float4x4 skin = player.getBoneSkinMatrices()[0];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                CHECK(std::isfinite(skin(r, c)));
            }
        }
    }

    TEST_SUITE_END