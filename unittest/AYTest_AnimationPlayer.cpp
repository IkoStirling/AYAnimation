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

// Phase 1.5 — Anim Notify EventBus integration test (test #8) lives at the
// bottom of this file and constructs an EventBus instance + subscribes to
// AnimNotifyEvent; pull in the EventBus header here so the source builds.
#include <ayevent/EventBus.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;
using namespace ayt::event;
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

    // ─────────────────────────────────────────────────────────────────────
    // Phase 1.5 — Anim Notify
    // ─────────────────────────────────────────────────────────────────────

    // Helper: build a 2-second looping clip with three evenly-spaced notify
    // markers at t=0.5, 1.0, 1.5 (name="M0.5/1.0/1.5", payload=10/20/30).
    ayt::resource::Animation makeNotifyClip()
    {
        ayt::resource::Animation clip;
        clip.setName("NotifyClip");
        clip.setTicksPerSecond(30.0f);
        clip.setDuration(2.0f);

        // No tracks — the helper below only cares about the notify block.
        // (Pure-notify clips are valid: ticksPerSecond/duration are still
        // meaningful for sink time stamping.)
        clip.addNotify(AnimNotifyMarker{"M0.5", 0.5f, 10.0f});
        clip.addNotify(AnimNotifyMarker{"M1.0", 1.0f, 20.0f});
        clip.addNotify(AnimNotifyMarker{"M1.5", 1.5f, 30.0f});
        return clip;
    }

    // Helper: build a 2-second looping clip with markers near 0 and near d
    // — used to verify loop-wrap re-fires the marker just past 0.
    ayt::resource::Animation makeWrapNotifyClip()
    {
        ayt::resource::Animation clip;
        clip.setName("WrapNotifyClip");
        clip.setTicksPerSecond(30.0f);
        clip.setDuration(2.0f);
        clip.addNotify(AnimNotifyMarker{"NearEnd", 1.9f, 0.0f});
        clip.addNotify(AnimNotifyMarker{"NearStart", 0.1f, 0.0f});
        return clip;
    }

    TEST_CASE(notify_marks_fire_on_cross_within_frame) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation clip = makeNotifyClip();

        std::vector<std::string> fired;
        std::vector<float>        firedTimes;

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setAnimNotifySink([&](const char* name, float t, float) {
            fired.emplace_back(name ? name : "");
            firedTimes.push_back(t);
        });
        player.setLoop(true);
        player.setPlayRate(1.0f);

        // Frame 1: dt=0.2 from t=0.4 → crosses M0.5 (at t=0.5).
        player.setTime(0.4f);
        player.tick(0.2f);
        // One marker crossed.
        CHECK(fired.size() == 1u);
        CHECK(fired[0]      == "M0.5");
        CHECK(firedTimes[0] == 0.5f);
        // Queue also populated.
        CHECK(player.getPendingNotifyCount() == 1u);
    }

    TEST_CASE(notify_does_not_re_fire_same_frame) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation clip = makeNotifyClip();
        int fireCount = 0;

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setAnimNotifySink([&](const char*, float, float) {
            ++fireCount;
        });
        player.setLoop(true);
        // tick crosses ONLY M1.0 (the [0.95,1.15] window does not include M0.5 at 0.5).
        player.setTime(0.95f);
        player.tick(0.2f);   // next=1.15 → crosses M1.0 once
        CHECK(fireCount == 1);
        // A zero-dt second tick should NOT re-fire.
        player.tick(0.0f);
        CHECK(fireCount == 1);
    }

    TEST_CASE(notify_re_fires_on_loop_wrap) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation clip = makeWrapNotifyClip();
        std::vector<std::string> fired;

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setAnimNotifySink([&](const char* name, float, float) {
            fired.emplace_back(name ? name : "");
        });
        player.setLoop(true);
        // Start at 1.85, tick 0.5 → raw 2.35 wraps to 0.35 → crosses 1.9 then 0.1.
        player.setTime(1.85f);
        player.tick(0.5f);
        CHECK(fired.size() == 2u);
        if (fired.size() >= 2u) {
            CHECK(fired[0] == "NearEnd");
            CHECK(fired[1] == "NearStart");
        }
    }

    TEST_CASE(notify_fires_only_when_sink_set) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation clip = makeNotifyClip();

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        // NO setAnimNotifySink — but the queue is still populated
        // (dual-exit design: queue is for the AYEntity → EventBus bridge).
        player.setLoop(true);

        player.setTime(0.4f);
        player.tick(0.2f);   // crosses M0.5
        CHECK(player.getPendingNotifyCount() == 1u);

        // Reset the sink and confirm a subsequent tick still records.
        player.setAnimNotifySink(nullptr);
        player.setTime(0.4f);
        player.tick(0.2f);
        CHECK(player.getPendingNotifyCount() == 1u);
    }

    TEST_CASE(notify_sink_receives_correct_payload) {
        Skeleton skel = makeTwoBoneSkeleton();
        ayt::resource::Animation clip;
        clip.setName("HitClip");
        clip.setTicksPerSecond(30.0f);
        clip.setDuration(2.0f);
        clip.addNotify(AnimNotifyMarker{"OnHit", 1.0f, 42.5f});

        std::string gotName;
        float gotTime = -1.0f;
        float gotPayload = -1.0f;
        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setAnimNotifySink([&](const char* name, float t, float p) {
            gotName    = name ? name : std::string();
            gotTime    = t;
            gotPayload = p;
        });
        player.setLoop(true);
        player.setTime(0.9f);
        player.tick(0.2f);   // crosses 1.0

        CHECK(gotName == "OnHit");
        CHECK(gotTime == 1.0f);
        CHECK(gotPayload == 42.5f);
    }

    TEST_CASE(notify_with_no_matching_name_silently_no_op) {
        // A clip with zero markers must dispatch nothing; the player must
        // tolerate callers that tick forever.
        Skeleton skel = makeTwoBoneSkeleton();
        ayt::resource::Animation clip;        // empty: 0 tracks, 0 notifies
        clip.setName("Empty");
        clip.setTicksPerSecond(30.0f);
        clip.setDuration(1.0f);

        int fireCount = 0;
        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setAnimNotifySink([&](const char*, float, float) {
            ++fireCount;
        });
        player.setLoop(true);
        for (int i = 0; i < 10; ++i) {
            player.tick(0.05f);
        }
        CHECK(fireCount == 0);
        CHECK(player.getPendingNotifyCount() == 0u);
    }

    TEST_CASE(consume_pending_notifies_drains_after_tick) {
        Skeleton skel = makeTwoBoneSkeleton();
        Animation clip = makeNotifyClip();

        AnimationPlayer player;
        player.setSkeleton(&skel);
        player.play(&clip);
        player.setLoop(true);
        player.setTime(0.4f);
        player.tick(0.2f);   // crosses M0.5

        const std::vector<AnimationPlayer::AnimNotifyRecord>& first = player.consumePendingNotifies();
        CHECK(first.size() == 1u);
        if (first.size() == 1u) {
            CHECK(std::string(first[0].name ? first[0].name : "") == "M0.5");
            CHECK(first[0].time    == 0.5f);
            CHECK(first[0].payload == 10.0f);
        }

        // Second consume on the same frame must yield nothing (drained).
        const std::vector<AnimationPlayer::AnimNotifyRecord>& second = player.consumePendingNotifies();
        CHECK(second.size() == 0u);
    }

    TEST_CASE(eventbus_animnotify_event_pod_carries_entity_id) {
        // Phase 1.5 cross-module bridge sanity: the POD type round-trips
        // through the AYEventSystem EventBus. We construct an instance and
        // subscribe to it the same way AYEntity will in PR3.
        //
        // This test lives in AYAnimation because the type is defined here
        // and we want the AYAnimation_unit_test target to own the basic
        // golden test. PR3 will add a fully wired AYEntity-side integration
        // test alongside.
        using namespace ayt::anim;
        ayt::event::EventBus bus;
        bool                  hit = false;
        AnimNotifyEvent       evt;
        evt.entity     = 0xDEADBEEFu;
        evt.clipName   = "test_clip";
        evt.notifyName = "OnFootstep";
        evt.notifyTime = 1.234f;
        evt.payload    = 3.14159f;

        bus.subscribe<AnimNotifyEvent>([&](const AnimNotifyEvent& e) {
            hit = (e.entity     == evt.entity) &&
                  (e.notifyTime == evt.notifyTime) &&
                  (e.payload    == evt.payload) &&
                  (std::strcmp(e.clipName,   "test_clip")   == 0) &&
                  (std::strcmp(e.notifyName, "OnFootstep") == 0);
        });
        bus.emit<AnimNotifyEvent>(evt);
        CHECK(hit == true);
        // kTypeId pinned so cross-module subscribers get the same id.
        CHECK(AnimNotifyEvent::kTypeId   == 0x000A'0001u);
        CHECK(AnimNotifyEvent::kPriority == ayt::event::EventPriority::High);
    }

    TEST_SUITE_END