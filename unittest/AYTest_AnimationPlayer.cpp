// AYTest_AnimationPlayer.cpp — AN-01 / AN-02 acceptance cases.
//
// Drives the AnimationPlayer with hand-built Skeleton + Animation, then verifies:
//   - Rest pose at t=0 with no animation tracks active.
//   - Position track linearly interpolates bone local translation.
//   - Parent → child world matrix composition.
//   - Missing track leaves bone at rest pose (no crash).
//   - tick(dt) wraps time into [0, duration).
//   - Skin matrix = world * inverseBindMatrix.

#include "AYAnimation.h"
#include <AYTest.h>
#include <AYMathTypes.h>
#include <AYMathUtils.h>

using namespace ayt::anim;
using namespace ayt::math;

namespace
{

// Build a 2-bone skeleton: Root (no parent) → Child (parent=0).
// Both bones have rest pose at the origin with identity rotation and unit scale.
Skeleton makeTwoBoneSkeleton()
{
    Skeleton skel;
    Bone root;
    root.name = "Root";
    root.parentIndex = -1;
    root.inverseBindMatrix = Float4x4::identity();
    skel.addBone(root);

    Bone child;
    child.name = "Child";
    child.parentIndex = 0;
    child.inverseBindMatrix = Float4x4::identity();
    skel.addBone(child);
    return skel;
}

// Build a position track on "Root": (0,0,0) at t=0, (10,0,0) at t=1.
KeyframeTrack makeRootPosTrack()
{
    KeyframeTrack tr;
    tr.nodeName = "Root";
    tr.property = "position";
    tr.type     = TrackType::Vector3;
    tr.times    = { 0.0f, 1.0f };
    tr.values   = { 0.0f, 0.0f, 0.0f,    10.0f, 0.0f, 0.0f };
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
        for (size_t i = 0; i < 2; ++i) {
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
        anim.setDuration(1.0f);
        anim.addTrack(makeRootPosTrack());

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.setTime(0.5f);
        player.evaluate();

        // Root's local position at t=0.5 should be (5,0,0).
        const FVector3* lp = player.getBoneLocalPositions();
        CHECK_FLOAT_EQ(lp[0].x, 5.0f, 1e-4f);
        CHECK_FLOAT_EQ(lp[0].y, 0.0f, 1e-4f);
        CHECK_FLOAT_EQ(lp[0].z, 0.0f, 1e-4f);
        // Child has no track — must remain at rest (0,0,0).
        CHECK_FLOAT_EQ(lp[1].x, 0.0f, 1e-4f);
        CHECK_FLOAT_EQ(lp[1].y, 0.0f, 1e-4f);
        CHECK_FLOAT_EQ(lp[1].z, 0.0f, 1e-4f);

        // Root world translation should also be (5,0,0). We can probe via
        // transformPoint of the root world matrix.
        const Float4x4 rootWorld = player.getBoneWorldMatrices()[0];
        const FVector3 worldT = rootWorld.transformPoint(FVector3(0.0f, 0.0f, 0.0f));
        CHECK_FLOAT_EQ(worldT.x, 5.0f, 1e-4f);
    }

    TEST_CASE(parent_child_world_composition) {
        // Root track moves Root to (10, 0, 0). Child has no track, so its local
        // TRS is identity and its world matrix = Root.world * identity = Root.world.
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
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
        // Animation has no track for "Spine" — bone must keep (1,2,3) after evaluate.
        Skeleton skel;
        Bone spine;
        spine.name = "Spine";
        spine.parentIndex = -1;
        spine.inverseBindMatrix = Float4x4::identity();
        skel.addBone(spine);

        // Manually inject non-default rest pose via setRestPoses.
        const FVector3 restPos[1]    = { FVector3(1.0f, 2.0f, 3.0f) };
        const FQuaternion restRot[1] = { FQuaternion(0.0f, 0.0f, 0.0f, 1.0f) };
        const FVector3 restScl[1]    = { FVector3(1.0f, 1.0f, 1.0f) };
        skel.setRestPoses(restPos, restRot, restScl);

        Animation anim;
        anim.setDuration(1.0f);
        // No tracks.

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&anim);
        player.evaluate();

        const FVector3* lp = player.getBoneLocalPositions();
        CHECK_FLOAT_EQ(lp[0].x, 1.0f, 1e-5f);
        CHECK_FLOAT_EQ(lp[0].y, 2.0f, 1e-5f);
        CHECK_FLOAT_EQ(lp[0].z, 3.0f, 1e-5f);
    }

    TEST_CASE(loop_wraps_time) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation anim;
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

TEST_SUITE_END