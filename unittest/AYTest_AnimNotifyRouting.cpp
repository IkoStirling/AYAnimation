// AYTest_AnimNotifyRouting.cpp — P3.x刀 N+1.C (2026-08-07) per-state
// AnimNotify routing unit tests.
//
// 4 cases pinning INV-40..42 contracts from design.md §4.17:
//   INV-40 — AnimNotifyRecord::fromStateName is set in push notify path
//            from the player's _currentStateNameForNotify cached value.
//            SM bridge calls setCurrentStateName to push new state.
//   INV-41 — AnimNotifyEvent::fromStateName mirror of AnimNotifyRecord
//            (default-empty keeps P1.5 subscribers source-compatible).
//   INV-42 — AnimNotifyRecord::fromStateName default "" when not driven
//            by an SM (legacy / direct clip playback back-compat).
//
// Standalone — no AYEntity, no AYAnimation/AnimationPlayer.h facade rebuild. Direct
// API calls on a hand-built ISkeleton + IAnimation.

#include "AYResource/assetsImpl/Animation.h"
#include <AYTest.h>
#include <AYMath/MathTypes.h>
#include <AYMath/MathUtils.h>

#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYResource/assetsImpl/Animation.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace
{

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

std::shared_ptr<const ISkeleton> sharedFromLocal(const Skeleton& skel)
{
    return std::static_pointer_cast<const ISkeleton>(
        std::make_shared<Skeleton>(skel));
}

// 2-second clip with 3 markers at t=0.5/1.0/1.5 (pure-notify, no tracks).
// Note: use small string interning (string literals) to keep notify name
// pointers stable across the test (see `std::string(...) == "..."` wrappers
// below — pointer equality is unreliable for non-interned std::string).
Animation makeNotifyClip()
{
    Animation clip;
    clip.setName("NotifyClip");
    clip.setTicksPerSecond(30.0f);
    clip.setDuration(2.0f);
    clip.addNotify(AnimNotifyMarker{"Footstep",  0.5f, 0.0f});
    clip.addNotify(AnimNotifyMarker{"SwordHit",  1.0f, 42.0f});
    clip.addNotify(AnimNotifyMarker{"Land",      1.5f, 0.0f});
    return clip;
}

// Helper: compare AnimNotifyRecord::name (const char*) to a string literal
// via std::string construction. `name` is owned by the IAnimation asset
// (a std::string inside), so direct pointer equality against a literal
// is unreliable; string compare is the safe path.
inline bool notifyNameEquals(const char* n, const char* expected)
{
    return n != nullptr && std::strcmp(n, expected) == 0;
}

} // namespace

TEST_SUITE(AnimNotifyRoutingTests)

// =====================================================================
// §8.1.6 Per-state AnimNotify routing (4 cases, NEW P3.x刀 N+1.C)
// =====================================================================

TEST_CASE(ANR_NotifyFromState_HasCorrectName) {
    // INV-40 — when the bridge pushes a state name via
    // setCurrentStateName, every subsequently-fired notify record
    // carries that name in fromStateName.
    Skeleton skel = makeTwoBoneSkeleton();
    Animation clip = makeNotifyClip();
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    player.play(&clip);
    player.setLoop(true);

    // No state name set → back-compat default empty (INV-42 baseline).
    // Tick from 0 in one shot to capture the marker; consume before any
    // subsequent setTime() would clear the queue.
    player.setTime(0.0f);
    player.tick(0.6f);   // crosses t=0.5 → "Footstep"
    const auto& recordsA = player.consumePendingNotifies();
    CHECK(recordsA.size() == 1u);
    if (recordsA.size() == 1u) {
        CHECK(notifyNameEquals(recordsA[0].name, "Footstep"));
        CHECK(recordsA[0].fromStateName.empty());   // INV-42 default-empty
    }

    // Bridge sets the active state — every notify from here on carries it.
    player.setCurrentStateName("Locomotion");
    CHECK(player.getCurrentStateName() == "Locomotion");

    // Continuous tick that crosses t=1.0 without a setTime() in between
    // (which would otherwise clear _pendingNotifies and drop the marker).
    player.tick(0.5f);   // crosses t=1.0 → "SwordHit"
    const auto& recordsB = player.consumePendingNotifies();
    CHECK(recordsB.size() == 1u);
    if (recordsB.size() == 1u) {
        CHECK(notifyNameEquals(recordsB[0].name, "SwordHit"));
        CHECK(recordsB[0].fromStateName == "Locomotion");
    }
}

TEST_CASE(ANR_NotifyAfterTransition_NewStateName) {
    // INV-40 — when the SM bridge calls setCurrentStateName with a
    // new value mid-stream, the next batch of fired notifies carries
    // the new name (not the previous one).
    Skeleton skel = makeTwoBoneSkeleton();
    Animation clip = makeNotifyClip();
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    player.play(&clip);
    player.setLoop(true);

    // Tick 1 — bridge pushes "Locomotion", fire Footstep
    player.setCurrentStateName("Locomotion");
    player.setTime(0.0f);
    player.tick(0.6f);   // crosses t=0.5
    const auto& recs1 = player.consumePendingNotifies();
    CHECK(recs1.size() == 1u);
    if (recs1.size() == 1u) {
        CHECK(notifyNameEquals(recs1[0].name, "Footstep"));
        CHECK(recs1[0].fromStateName == "Locomotion");
    }

    // Transition — bridge pushes "Attack"; continuous tick to t=1.0.
    player.setCurrentStateName("Attack");
    player.tick(0.5f);   // crosses t=1.0 → "SwordHit"
    const auto& recs2 = player.consumePendingNotifies();
    CHECK(recs2.size() == 1u);
    if (recs2.size() == 1u) {
        CHECK(notifyNameEquals(recs2[0].name, "SwordHit"));
        CHECK(recs2[0].fromStateName == "Attack");
    }

    // Another transition — bridge pushes "Landing"; continuous tick to t=1.5.
    player.setCurrentStateName("Landing");
    player.tick(0.5f);   // crosses t=1.5 → "Land"
    const auto& recs3 = player.consumePendingNotifies();
    CHECK(recs3.size() == 1u);
    if (recs3.size() == 1u) {
        CHECK(notifyNameEquals(recs3[0].name, "Land"));
        CHECK(recs3[0].fromStateName == "Landing");
    }
}

TEST_CASE(ANR_NotifyWithoutStateMachine_EmptyName) {
    // INV-42 — when no state name was ever set (legacy / direct clip
    // playback), every notify record's fromStateName is empty. This
    // pins the back-compat sentinel: subscribers without fromStateName
    // awareness continue to receive and process events normally.
    Skeleton skel = makeTwoBoneSkeleton();
    Animation clip = makeNotifyClip();
    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    player.play(&clip);
    player.setLoop(true);
    // No setCurrentStateName call — pure direct-clip playback.

    // Single continuous tick that crosses all 3 markers. setTime() would
    // clear the per-frame queue, so we just tick from 0 across the full
    // 2-second timeline.
    player.setTime(0.0f);
    player.tick(1.6f);   // crosses t=0.5, t=1.0, t=1.5 → all 3 markers

    const auto& records = player.consumePendingNotifies();
    CHECK(records.size() == 3u);
    if (records.size() == 3u) {
        CHECK(notifyNameEquals(records[0].name, "Footstep"));
        CHECK(notifyNameEquals(records[1].name, "SwordHit"));
        CHECK(notifyNameEquals(records[2].name, "Land"));
        for (const auto& r : records) {
            CHECK(r.fromStateName.empty());        // INV-42
        }
    }
}

TEST_CASE(ANR_MergedQueue_PreservesFromStateName) {
    // INV-41 — the merged queue (consumePendingNotifiesMerged) preserves
    // fromStateName across the base + per-slot dedup rebuild. Both base
    // and additive records carry the same SM-cached state name; the
    // dedup pass does NOT drop it.
    Skeleton skel = makeTwoBoneSkeleton();
    Animation baseClip = makeNotifyClip();
    // Build an additive clip with its own marker at t=0.5 (same time
    // as base "Footstep") so the dedup pass runs.
    Animation addClip;
    addClip.setName("AddClip");
    addClip.setTicksPerSecond(30.0f);
    addClip.setDuration(2.0f);
    addClip.addNotify(AnimNotifyMarker{"Footstep",  0.5f, 0.0f});
    addClip.addNotify(AnimNotifyMarker{"WindWhoosh", 1.0f, 0.0f});

    AnimationPlayer player;
    player.setSkeleton(sharedFromLocal(skel));
    player.setCurrentStateName("Combat");      // bridge-style push
    player.play(&baseClip);
    player.setAdditiveLayerSource(0, &addClip, 1.0f, true);
    player.setLoop(true);

    player.setTime(0.4f);
    player.tick(0.2f);   // crosses t=0.5: base "Footstep" + additive "Footstep"

    const auto& merged = player.consumePendingNotifiesMerged();
    // dedup-by-(time, name) drops the additive duplicate → 1 record.
    CHECK(merged.size() == 1u);
    if (merged.size() == 1u) {
        CHECK(notifyNameEquals(merged[0].name, "Footstep"));
        CHECK(merged[0].fromStateName == "Combat");
        CHECK(merged[0].sourceTag == AnimNotifySourceTag::Base);
    }

    // Continue ticking — second marker at t=1.0 (base "SwordHit" + add "WindWhoosh")
    // are different names so both survive.
    player.setTime(0.9f);
    player.tick(0.2f);
    const auto& merged2 = player.consumePendingNotifiesMerged();
    CHECK(merged2.size() == 2u);
    if (merged2.size() == 2u) {
        // Sorted by (time, sourceTag) → Base (SwordHit @ t=1.0) before Additive_0 (WindWhoosh).
        CHECK(notifyNameEquals(merged2[0].name, "SwordHit"));
        CHECK(merged2[0].fromStateName == "Combat");
        CHECK(notifyNameEquals(merged2[1].name, "WindWhoosh"));
        CHECK(merged2[1].fromStateName == "Combat");
    }
}

TEST_SUITE_END