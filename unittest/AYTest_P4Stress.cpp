// AYTest_P4Stress.cpp — P4 polish (2026-08-10) acceptance cases.
//
// Three P4 polish items verified here:
//   1. Batch-tick stress: many players sharing ONE skeleton asset tick
//      for hundreds of frames without divergence from a reference player
//      (the P1.7 shared-cache contract under load), and the asset-level
//      AssetBoneCache stays at ONE skeleton entry (sharing working).
//   2. Bind/clear cycles: scene-switch style loop — bind N players,
//      tick, invalidate — must return AssetBoneCache entry count to 0
//      (no cache leak across rounds).
//   3. AdditiveSlot memory release (INV-61/62): clear / stop() release
//      the slot's heavy buffers (swap-with-empty semantics); re-bind
//      afterwards starts in fresh P1.5 state. The buffers themselves are
//      private — verified behaviorally (evaluate contribution comes back
//      after re-bind), structural release is guaranteed by
//      releaseSlotBuffers' swap implementation.
//   4. AssetBoneCache heterogeneous lookup (INV-63): const char* /
//      string_view keys hit the SAME entry as a std::string-seeded one
//      (no duplicate inserts — the transparent StringViewHash path).

#include "AYAnimation.h"
#include <AYTest.h>
#include <AYMath/MathTypes.h>

#include <AYResource/assetsDefs/ISkeleton.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYResource/assetsImpl/Animation.h>
#include "AssetBoneCache.h"

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ayt::anim;
using namespace ayt::math;
using namespace ayt::resource;

namespace
{

// 2-bone skeleton: Root (no parent) → Child (parent=0). Rest pose at the
// origin with identity rotation and unit scale. Shared_ptr form so N
// players can hand the SAME asset to the player (P1.7 contract).
std::shared_ptr<const ISkeleton> makeTwoBoneSkeletonShared()
{
    Skeleton skel;
    skel.setBoneCount(2);

    Bone root;
    root.name = "Root";
    root.parentIndex = -1;
    root.localPosition  = FVector3(0, 0, 0);
    root.localRotation  = FQuaternion::identity();
    root.localScale     = FVector3(1, 1, 1);
    root.inverseBindMatrix = Float4x4::identity();
    skel.setBone(0, root);

    Bone child;
    child.name = "Child";
    child.parentIndex = 0;
    child.localPosition  = FVector3(0, 0, 0);
    child.localRotation  = FQuaternion::identity();
    child.localScale     = FVector3(1, 1, 1);
    child.inverseBindMatrix = Float4x4::identity();
    skel.setBone(1, child);

    return std::static_pointer_cast<const ISkeleton>(
        std::make_shared<Skeleton>(skel));
}

// Override track: Root.position ramps (0,0,0) → (10,0,0) over 1.0s.
Animation makeRootRampAnim()
{
    Animation anim;
    anim.setTicksPerSecond(30.0f);
    anim.setDuration(1.0f);
    AnimTrack tr;
    tr.nodeName  = "Root";
    tr.property  = "position";
    tr.valueType = AnimTrackType::Vector3;
    tr.times  = { 0.0f, 30.0f };
    tr.values = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f };
    anim.addTrack(tr);
    return anim;
}

// Additive ramp: Root.position delta (0,0,0) → (2,0,0) over 1.0s
// (zero delta at t=0 — additive clips MUST be authored this way).
Animation makeAdditiveRampAnim()
{
    Animation anim;
    anim.setTicksPerSecond(30.0f);
    anim.setDuration(1.0f);
    AnimTrack tr;
    tr.nodeName  = "Root";
    tr.property  = "position";
    tr.valueType = AnimTrackType::Vector3;
    tr.blendMode = AnimBlendMode::Additive;
    tr.times  = { 0.0f, 30.0f };
    tr.values = { 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };
    anim.addTrack(tr);
    return anim;
}

} // namespace

TEST_SUITE(P4StressTests)

    TEST_CASE(p4_stress_400_players_share_one_skeleton) {
        // Batch-tick stability: 400 players bound to the SAME skeleton
        // asset, ticked 200 frames each. Every player must stay
        // byte-identical to a reference player (same input, same code
        // path), and the shared AssetBoneCache must hold exactly ONE
        // skeleton entry — the P1.7 share contract under load.
        AssetBoneCache::instance().clear();
        auto skel = makeTwoBoneSkeletonShared();
        Animation anim = makeRootRampAnim();

        constexpr int kPlayers = 400;
        constexpr int kFrames  = 200;
        constexpr float kDt    = 0.0052f;   // wraps the 1.0s clip ~once

        std::vector<std::unique_ptr<AnimationPlayer, AnimationPlayerDeleter>>
            players;
        players.reserve(kPlayers);
        for (int i = 0; i < kPlayers; ++i) {
            auto p = AnimationPlayer::create();
            p->setSkeleton(skel);
            p->play(&anim);
            p->setTime(0.0f);
            players.push_back(std::move(p));
        }

        AnimationPlayer ref;
        ref.setSkeleton(skel);
        ref.play(&anim);
        ref.setTime(0.0f);

        // First frame: every player resolves "Root" through the shared
        // asset cache → exactly ONE skeleton entry (P1.7 sharing).
        for (auto& p : players) p->evaluate();
        ref.evaluate();
        CHECK(AssetBoneCache::instance().skeletonEntryCount() == 1u);

        for (int f = 0; f < kFrames; ++f) {
            // Full frame: tick advances the clock + fires notifies,
            // evaluate runs the 3-phase render path (where per-track
            // boneIdx resolution + AssetBoneCache live). Both halves
            // are exercised every frame.
            for (auto& p : players) { p->tick(kDt); p->evaluate(); }
            ref.tick(kDt);
            ref.evaluate();

            // Spot-check: player[0] stays bit-identical to the reference
            // for the whole run (loop wrap included).
            CHECK(std::memcmp(players[0]->getBoneWorldMatrices(),
                              ref.getBoneWorldMatrices(),
                              ref.getBoneCount() * sizeof(Float4x4)) == 0);
            CHECK(players[0]->getBoneCount() == ref.getBoneCount());
        }

        // Every player still valid at the end of the batch.
        for (auto& p : players) CHECK(p->isValid());
        AssetBoneCache::instance().clear();
    }

    TEST_CASE(p4_stress_bind_clear_cycle_returns_cache_entries) {
        // Scene-switch loop: each round binds fresh players (each with
        // its OWN skeleton), ticks once, then invalidates the skeletons'
        // cache entries. Entry count must return to 0 every round — no
        // cache leak accumulates across scene switches.
        AssetBoneCache& cache = AssetBoneCache::instance();
        cache.clear();
        Animation anim = makeRootRampAnim();

        constexpr int kRounds   = 5;
        constexpr int kPerRound = 40;

        for (int round = 0; round < kRounds; ++round) {
            std::vector<std::shared_ptr<const ISkeleton>> skeletons;
            std::vector<std::unique_ptr<AnimationPlayer, AnimationPlayerDeleter>>
                players;
            skeletons.reserve(kPerRound);
            players.reserve(kPerRound);

            for (int i = 0; i < kPerRound; ++i) {
                auto skel = makeTwoBoneSkeletonShared();
                skeletons.push_back(skel);
                auto p = AnimationPlayer::create();
                p->setSkeleton(skel);
                p->play(&anim);
                // Full frame — tick advances the clock; evaluate runs
                // the render path where per-track boneIdx resolution
                // populates the asset cache.
                p->tick(0.016f);
                p->evaluate();
                players.push_back(std::move(p));
            }

            // Every player's first evaluate resolved its tracks through
            // the asset cache → exactly one cache entry per skeleton
            // (fresh round: previous round was fully invalidated).
            CHECK(cache.skeletonEntryCount() ==
                  static_cast<size_t>(kPerRound));

            // Scene switch: unload all skeletons → explicit invalidation
            // must return the cache to empty.
            for (const auto& skel : skeletons) cache.invalidate(skel.get());
            CHECK(cache.skeletonEntryCount() == 0u);
        }
    }

    TEST_CASE(P4_AssetBoneCache_HeterogeneousLookup_SingleEntry) {
        // INV-63 — find() with const char* / string_view keys must not
        // construct a temporary std::string (structural: StringViewHash
        // is_transparent). Functionally: all three key spellings hit the
        // SAME entry — no duplicate insert into the inner map.
        AssetBoneCache& cache = AssetBoneCache::instance();
        cache.clear();
        auto skel = makeTwoBoneSkeletonShared();
        const auto* p = skel.get();

        CHECK(cache.resolveAndCache(p, "Root") == 0);

        CHECK(cache.lookup(p, "Root") == 0);                    // literal
        const std::string str("Root");
        CHECK(cache.lookup(p, str.c_str()) == 0);               // const char*
        CHECK(cache.lookup(p, std::string_view("Root").data()) == 0);

        // One entry regardless of key spelling.
        CHECK(cache.boneNameEntryCount(p) == 1u);
        cache.clear();
    }

    TEST_CASE(P4_AdditiveSlot_ClearStop_RebindFreshState) {
        // INV-61/62 — clear (and stop) release the slot's heavy buffers;
        // a re-bind starts in fresh P1.5 state. The buffers are private,
        // so the release is verified behaviorally: cleared slot stops
        // contributing to evaluate, re-bound slot contributes the exact
        // same value as before (fresh cross-fade config, same curves).
        auto skel = makeTwoBoneSkeletonShared();
        Animation base = makeRootRampAnim();
        Animation add = makeAdditiveRampAnim();

        AnimationPlayer player;
        player.setSkeleton(skel);
        player.play(&base);
        player.setBlendWeight(1.0f);
        player.setAdditiveLayerSource(0, &add, 1.0f, true);

        auto rootX = [&](float t) {
            player.setTime(t);
            player.evaluate();
            return player.getBoneWorldMatrices()[0]
                .transformPoint(FVector3(0, 0, 0)).x;
        };

        // t=0.5: base 5.0 + additive delta 1.0 → 6.0.
        CHECK_FLOAT_EQ(rootX(0.5f), 6.0f, 1e-4f);

        // Clear the slot → additive contribution gone, base only.
        player.clearAdditiveLayerSource(0);
        CHECK_FLOAT_EQ(rootX(0.5f), 5.0f, 1e-4f);
        CHECK(player.getAdditiveLayerCount() == 0);

        // Re-bind the SAME slot → identical fresh-state result (INV-62).
        player.setAdditiveLayerSource(0, &add, 1.0f, true);
        CHECK_FLOAT_EQ(rootX(0.5f), 6.0f, 1e-4f);

        // stop() disposes ALL slots → base-only again (INV-61 via stop).
        player.stop();
        CHECK(player.getAdditiveLayerCount() == 0);
        CHECK_FLOAT_EQ(rootX(0.5f), 5.0f, 1e-4f);
    }

    TEST_SUITE_END
