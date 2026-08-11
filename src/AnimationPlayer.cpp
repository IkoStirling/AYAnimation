#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/AssetBoneCache.h>   // P1.7 — asset-level boneIdx cache
#include <ayanimation/KeySampler.h>
#include <ayanimation/TwoBoneSolver.h>   // P4-1 — two-bone IK analytic core
#include <ayanimation/FabrikSolver.h>    // P4-2 — FABRIK iterative IK core (multi-joint)
#include <ayanimation/CcdSolver.h>       // P4-2 — CCD iterative IK core (multi-joint)

#include <aymath/MathTypes.h>
#include <aymath/MathUtils.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace ayt::anim
{

namespace
{

// Validate the topology invariant AnimationPlayer::evaluate Phase 2 relies on:
// parentIndex must precede the child index, recursively, so a single forward
// pass can accumulate world matrices. Loaders (FBX converter, SkeletonConverter)
// guarantee this via DFS collection order; assert in debug builds so a
// regression in either loader or a hand-rolled test Skeleton is caught loudly.
bool isTopologicallySorted(const ayt::resource::ISkeleton* skel)
{
    if (skel == nullptr) return true;
    const size_t n = skel->getBoneCount();
    const ayt::resource::Bone* bones = skel->getBones();
    for (size_t i = 0; i < n; ++i) {
        const int p = bones[i].parentIndex;
        // parentIndex must be a prior bone; OOB / forward refs fail.
        if (p < -1 || (p >= 0 && static_cast<size_t>(p) >= n)) {
            return false;
        }
        if (p >= 0 && static_cast<size_t>(p) >= i) {
            return false;
        }
    }
    return true;
}

// P1.5 — slot index → AnimNotifySourceTag. Tag enum is dense (Base=0,
// Additive_0=1..Additive_7=8) so this is a +1 / 0 static map; kept
// inline rather than as a switch because most call sites are inside
// hot loops. Bounds-checking happens at the call site (for-loop over
// _additiveSlots.size() is the natural bound).
inline AnimNotifySourceTag tagForSlotIndex(uint32_t slotIdx)
{
    // Caller (Phase 0 of rebuildMergedNotifies) passes only slotIdx in
    // [0, _additiveSlots.size() <= kMaxAdditiveSlots). Cast is safe.
    return static_cast<AnimNotifySourceTag>(static_cast<uint8_t>(AnimNotifySourceTag::Additive_0) + slotIdx);
}

} // namespace

// ===========================================================================
//  P1.5 — Multi-source stack helpers
// ===========================================================================
//
// ensureSlot / getSlot are the three operations that touch the
// _additiveSlots vector. They are the ONLY callers that grow the
// vector; every per-slot API path goes through them so the bounds
// and resize policy live in exactly one place.
//
// Bounds policy: slotId >= kMaxAdditiveSlots → silently ignored (debug
// assert, release no-op). This is defensive — a host hitting the cap
// should be told via getAdditiveLayerCount / the bind return value,
// not via a crash. Vector growth is padded with empty (clip==null)
// AdditiveSlots up to slotId+1, keeping the structural invariant
// "_additiveSlots[i] exists iff i <= slotId-of-last-bind".
AdditiveSlot& AnimationPlayer::ensureSlot(uint32_t slotId)
{
    assert(slotId < kMaxAdditiveSlots &&
           "AnimationPlayer::ensureSlot: slotId >= kMaxAdditiveSlots (8)");
    if (slotId >= kMaxAdditiveSlots) {
        // Release fall-back. Returning a static dummy keeps the
        // reference alive for one statement (the binder immediately
        // bails out on the OOR check). The dummy is mutated and
        // discarded — bounds-checking at the caller is the real gate.
        static AdditiveSlot s_dummy;
        return s_dummy;
    }
    if (_additiveSlots.size() <= slotId) {
        _additiveSlots.resize(slotId + 1);
    }
    return _additiveSlots[slotId];
}

AdditiveSlot* AnimationPlayer::getSlot(uint32_t slotId)
{
    if (slotId >= kMaxAdditiveSlots) return nullptr;
    if (slotId >= _additiveSlots.size()) return nullptr;
    return &_additiveSlots[slotId];
}

const AdditiveSlot* AnimationPlayer::getSlot(uint32_t slotId) const
{
    if (slotId >= kMaxAdditiveSlots) return nullptr;
    if (slotId >= _additiveSlots.size()) return nullptr;
    return &_additiveSlots[slotId];
}

// P1.5 — TrackSlice rebuild for a slot. Mirror of the inline TrackSlice-
// build block in P1.3 setAdditiveSource body; extracted to a single helper
// so the single-slot wrapper (setAdditiveSource → setAdditiveLayerSource(0))
// and the multi-slot setAdditiveLayerSource(K) call the same code path.
//
// INV preserved: same flat-float → aligned FVector3 / FQuaternion copy
// (P0 alignment workaround for the AYResource reinterpret_cast path),
// same blendMode caching at bind time, same timesSec pre-normalization.
void AnimationPlayer::rebuildSlotTracks(AdditiveSlot& slot,
                                       const ayt::resource::IAnimation* src)
{
    slot.tracks.clear();
    if (src == nullptr) return;
    const float tps = src->getTicksPerSecond();
    const float invTps = (tps > 0.0f) ? (1.0f / tps) : 1.0f;
    const uint32_t trackCount = src->getTrackCount();
    slot.tracks.reserve(trackCount);
    for (uint32_t ti = 0; ti < trackCount; ++ti) {
        TrackSlice slice;
        slice.nodeName = (src->getTrackNodeName(ti) != nullptr)
                             ? std::string(src->getTrackNodeName(ti))
                             : std::string();
        slice.property = (src->getTrackProperty(ti) != nullptr)
                             ? std::string(src->getTrackProperty(ti))
                             : std::string();
        slice.type     = src->getTrackType(ti);
        slice.blendMode = src->getTrackBlendMode(ti);
        const uint32_t keyCount = src->getTrackKeyframeCount(ti);
        const float* rawTimes = src->getTrackTimes(ti);
        slice.timesSec.assign(keyCount, 0.0f);
        for (uint32_t k = 0; k < keyCount; ++k) {
            slice.timesSec[k] = rawTimes[k] * invTps;
        }
        const float* flat = src->getTrackFloatValues(ti);
        switch (slice.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                if (flat != nullptr && keyCount > 0) {
                    slice.vec3Values.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.vec3Values[k] = ayt::math::FVector3(
                            flat[k * 3 + 0],
                            flat[k * 3 + 1],
                            flat[k * 3 + 2]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                if (flat != nullptr && keyCount > 0) {
                    slice.quatValues.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.quatValues[k] = ayt::math::FQuaternion(
                            flat[k * 4 + 0],
                            flat[k * 4 + 1],
                            flat[k * 4 + 2],
                            flat[k * 4 + 3]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                if (flat != nullptr && keyCount > 0) {
                    slice.scalarValues.assign(flat, flat + keyCount);
                }
                break;
            }
        }
        slot.tracks.push_back(std::move(slice));
    }
}

// ===========================================================================
//  Factory / lifetime
// ===========================================================================

void AnimationPlayerDeleter::operator()(AnimationPlayer* p) const noexcept
{
    delete p;
}

std::unique_ptr<AnimationPlayer, AnimationPlayerDeleter> AnimationPlayer::create()
{
    return std::unique_ptr<AnimationPlayer, AnimationPlayerDeleter>(
        new AnimationPlayer());
}

// ===========================================================================
//  Resource binding
// ===========================================================================

void AnimationPlayer::setSkeleton(
    std::shared_ptr<const ayt::resource::ISkeleton> skel)
{
    // Near-null `this` means the caller held a garbage AnimationPlayer*
    // (typically SkeletonComponent ABI mismatch: unique_ptr field read
    // from an object still laid out with an embedded-by-value player).
    if (reinterpret_cast<std::uintptr_t>(this) < 0x10000u) {
        std::fprintf(stderr,
                     "[AnimationPlayer] setSkeleton refused: this=%p (near-null)\n",
                     (void*)this);
        std::fflush(stderr);
        return;
    }

    // P1.7 — retain a strong reference so the player's lifetime is
    // bounded by the source of truth (SkeletonComponent in ECS, test
    // fixture in unit tests). Empty shared_ptr unbinds the skeleton.
    _skeleton = std::move(skel);
    const ayt::resource::ISkeleton* skelRaw = _skeleton.get();

    const size_t n = (skelRaw != nullptr) ? skelRaw->getBoneCount() : 0;
    // Soft cap: a single AnimationPlayer palette / rest-pose buffer.
    // Sour Miku FBX can report ~500 bones (body + dummy/shadow/physics);
    // above this we still bind but refuse to allocate (avoids OOM / AV).
    constexpr size_t kMaxBonesPerPlayer = 4096;
    if (n > kMaxBonesPerPlayer) {
        std::fprintf(stderr,
                     "[AnimationPlayer] setSkeleton refused: boneCount=%zu > %zu\n",
                     n, kMaxBonesPerPlayer);
        _skeleton.reset();
        _localPos.clear();
        _localRot.clear();
        _localScl.clear();
        _world.clear();
        _skin.clear();
        return;
    }
    // Build into temporaries then swap so fill/memset never runs against
    // a stomped member `_Myfirst` (observed as write @ 0x20).
    {
        std::vector<float> pos(n * 3, 0.0f);
        std::vector<float> rot(n * 4, 0.0f);
        std::vector<float> scl(n * 3, 1.0f);
        std::vector<ayt::math::Float4x4> world(n);
        std::vector<ayt::math::Float4x4> skin(n);
        const ayt::math::Float4x4 id = ayt::math::Float4x4::identity();
        for (size_t i = 0; i < n; ++i) {
            std::memcpy(&world[i], &id, sizeof(id));
            std::memcpy(&skin[i], &id, sizeof(id));
        }
        _localPos.swap(pos);
        _localRot.swap(rot);
        _localScl.swap(scl);
        _world.swap(world);
        _skin.swap(skin);
    }

    // Seed local TRS buffers from the skeleton's rest pose so missing
    // tracks leave a bone at bind pose.
    if (skelRaw != nullptr && n > 0) {
        const ayt::math::FVector3*    pos = skelRaw->getLocalPositions();
        const ayt::math::FQuaternion* rot = skelRaw->getLocalRotations();
        const ayt::math::FVector3*    scl = skelRaw->getLocalScales();
        if (pos != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localPos[i * 3 + 0] = pos[i].x;
                _localPos[i * 3 + 1] = pos[i].y;
                _localPos[i * 3 + 2] = pos[i].z;
            }
        }
        if (rot != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localRot[i * 4 + 0] = rot[i].x;
                _localRot[i * 4 + 1] = rot[i].y;
                _localRot[i * 4 + 2] = rot[i].z;
                _localRot[i * 4 + 3] = rot[i].w;
            }
        }
        if (scl != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localScl[i * 3 + 0] = scl[i].x;
                _localScl[i * 3 + 1] = scl[i].y;
                _localScl[i * 3 + 2] = scl[i].z;
            }
        }
    }

    // P0: assert topology. AYResource loaders guarantee parent-before-child;
    // a hand-rolled skeleton with bad parentIndex would NaN-out the world
    // matrices silently otherwise.
#ifndef NDEBUG
    assert(isTopologicallySorted(_skeleton.get()) &&
           "AnimationPlayer: skeleton bone order must satisfy parentIndex < childIndex");
#endif

    // P1.4 — bone-index cache invalidation. The previous skeleton's
    // bone name table is no longer the right lookup target — every
    // TrackSlice.boneIdx entry must be re-resolved against the new
    // skeleton. P1.5: iterates every slot's tracks in addition to
    // the base _tracks.
    invalidateBoneIndexCache();

    // P1.4 / P1.5 — ref-pose capture buffer resize + state flip per slot.
    // Each slot that has refPoseCapture enabled owns its own captured
    // buffers; the resize goes through every slot (cheap empty slots
    // get sized to n*3/n*4/n*3 immediately, so a future enable() call
    // doesn't need a re-allocation). The CaptureState flip is opt-in:
    // only slots that already have refPoseCapture on get flipped to
    // Stale — others stay Fresh so the first time they enable they
    // capture from the current _local*.
    for (AdditiveSlot& s : _additiveSlots) {
        s.capturedLocalPos.assign(n * 3, 0.0f);
        s.capturedLocalRot.assign(n * 4, 0.0f);
        s.capturedLocalScl.assign(n * 3, 1.0f);
        if (s.refPoseCapture) {
            s.captureState = CaptureState::Stale;
        } else {
            s.captureState = CaptureState::Fresh;
        }
    }

    // P2.2 — re-resolve the skeleton mask against the new skeleton
    // pointer. The mask's bone-name → idx mapping is keyed by the
    // current _skeleton pointer (via AssetBoneCache); a new skeleton
    // pointer means old resolved indices are stale. Eager re-resolve
    // here keeps INV-13 honest (mask rebinds on skeleton swap) and
    // pays zero per-frame cost.
    if (_skeletonMask != nullptr) {
        resolveSkeletonMask();
    }

    // P4-1 — re-resolve every IK chain against the new skeleton pointer
    // (INV-71). Chains whose three names all hit revive; chains with a
    // miss are disabled in place (indices -1) and skipped by evaluate()
    // — never a crash. Same eager-rebind rationale as the mask above.
    if (!_ikChains.empty()) {
        resolveIKChains();
    }
}

// P1.4 — bone-index cache helpers. See TrackSlice header for the
// sentinel semantics (INT32_MIN == "not yet resolved"; -1 == "name
// not found in current skeleton"). After the first evaluate() the
// cache is hot and every subsequent frame reads the cached index
// directly — no more findBone() per track per frame per source.
//
// P1.5: walks ALL slot tracks in addition to the base _tracks. Slots
// share the skeleton's bone name table so a single skeleton swap
// invalidates everything.
void AnimationPlayer::invalidateBoneIndexCache()
{
    for (TrackSlice& tr : _tracks) {
        tr.boneIdx = kBoneUnresolved;
    }
    for (AdditiveSlot& s : _additiveSlots) {
        for (TrackSlice& tr : s.tracks) {
            tr.boneIdx = kBoneUnresolved;
        }
    }
}

void AnimationPlayer::resolveBoneIdxOnce(TrackSlice& slice)
{
    // P1.4 sentinel semantics (unchanged):
    //   -1    = name not found in skeleton (cached negative)
    //   INT32_MIN = not yet resolved (kBoneUnresolved)
    //   >=0   = valid bone index
    //
    // P1.7 — route the (skeleton, name) lookup through AssetBoneCache
    // so two players bound to the same ISkeleton* share the result.
    // Per-player TrackSlice.boneIdx remains the P1.4 hot-path cache;
    // the asset cache is the cross-player cold-miss path.
    if (slice.boneIdx != kBoneUnresolved) return;
    if (_skeleton == nullptr) {
        return;
    }
    AssetBoneCache& cache = AssetBoneCache::instance();
    const int32_t cached =
        cache.resolveAndCache(_skeleton.get(), slice.nodeName.c_str());
    slice.boneIdx = (cached >= 0) ? cached : -1;
}

void AnimationPlayer::play(const ayt::resource::IAnimation* anim)
{
    _baseClip = anim;
    _time = 0.0f;
    _paused = false;
    // Phase 1.5: starting a new clip clears the per-frame notify queue
    // and resets the prev-tick cursor so the first tick fires anything
    // in [0, tick_dt).
    _pendingNotifies.clear();
    _prevTickTime = 0.0f;

    _tracks.clear();
    if (anim == nullptr) return;

    // Pre-normalize track times ticks → seconds once. Per-frame sampling
    // then works in seconds without per-key division.
    const float tps = anim->getTicksPerSecond();
    const float invTps = (tps > 0.0f) ? (1.0f / tps) : 1.0f;

    const uint32_t trackCount = anim->getTrackCount();
    _tracks.reserve(trackCount);
    for (uint32_t ti = 0; ti < trackCount; ++ti) {
        TrackSlice slice;
        slice.nodeName = (anim->getTrackNodeName(ti) != nullptr)
                             ? std::string(anim->getTrackNodeName(ti))
                             : std::string();
        slice.property = (anim->getTrackProperty(ti) != nullptr)
                             ? std::string(anim->getTrackProperty(ti))
                             : std::string();
        slice.type     = anim->getTrackType(ti);
        slice.blendMode = anim->getTrackBlendMode(ti);

        const uint32_t keyCount = anim->getTrackKeyframeCount(ti);
        const float* rawTimes = anim->getTrackTimes(ti);
        slice.timesSec.assign(keyCount, 0.0f);
        for (uint32_t k = 0; k < keyCount; ++k) {
            slice.timesSec[k] = rawTimes[k] * invTps;
        }

        // P0 alignment workaround — same as setAdditiveSource body.
        const float* flat = anim->getTrackFloatValues(ti);
        switch (slice.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                if (flat != nullptr && keyCount > 0) {
                    slice.vec3Values.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.vec3Values[k] = ayt::math::FVector3(
                            flat[k * 3 + 0],
                            flat[k * 3 + 1],
                            flat[k * 3 + 2]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                if (flat != nullptr && keyCount > 0) {
                    slice.quatValues.resize(keyCount);
                    for (uint32_t k = 0; k < keyCount; ++k) {
                        slice.quatValues[k] = ayt::math::FQuaternion(
                            flat[k * 4 + 0],
                            flat[k * 4 + 1],
                            flat[k * 4 + 2],
                            flat[k * 4 + 3]);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                if (flat != nullptr && keyCount > 0) {
                    slice.scalarValues.assign(flat, flat + keyCount);
                }
                break;
            }
        }
        _tracks.push_back(std::move(slice));
    }

    // P1.3 / P1.5 — state machine contract: play() keeps the additive
    // layers persistent. Swapping the base clip does NOT touch any
    // slot's clip / time / tracks / pendingNotifies. UPGRADE-HOOK(P1.5)
    // resolved: multi-slot preserved across base re-bind (per-slot time
    // independence is a host responsibility — they re-anchor via
    // setAdditiveLayerSyncToBase(true) if they want lock-step).
}

void AnimationPlayer::stop()
{
    _time   = 0.0f;
    _paused = false;
    _prevTickTime = 0.0f;
    _pendingNotifies.clear();

    // P1.5 — stop() disposes ALL slots. State machine contract: stop
    // is "playback halted + sources unbound". Each slot's clip is nulled
    // and its heavy buffers released (P4 polish, INV-61); the AdditiveSlot
    // vector itself is left at its current size (sparse slots remain
    // sparse — a host that re-binds slot 4 still finds the slot[0..3]
    // tail empty, INV-62).
    for (AdditiveSlot& s : _additiveSlots) {
        s.clip = nullptr;
        s.time = 0.0f;
        s.prevTickTime = 0.0f;
        releaseSlotBuffers(s);
        // P1.4 cross-fade config also resets on unbound so a subsequent
        // re-bind via setAdditiveLayerSource() starts in fresh state.
        s.syncToBase     = false;
        s.refPoseCapture = false;
        s.captureState   = CaptureState::Fresh;
        s.paused         = false;
        s.curve.active   = false;
    }
    // Merged queue is also flushed — nothing fired this frame survives.
    _pendingNotifiesMerged.clear();
    _blendWeight = 1.0f;   // P1.3: layer intensity preserved on source swap,
                           // but stop() is source-clear, so reset to default.
}

// ===========================================================================
//  P1.3 — Additive Layer 2 (Cross-Fade) — backward-compat single-slot wrappers
// ===========================================================================
//
// P1.5: every old single-slot API is a thin redirect to slot[0]. The
// P1.3 / P1.4 contract is preserved bit-for-bit when callers haven't
// adopted the per-slot API yet. Per-slot state stays in the AdditiveSlot
// struct — no parallel cache fields, so wrapper + per-slot calls
// always see consistent state.

void AnimationPlayer::setAdditiveSource(const ayt::resource::IAnimation* src,
                                        float playRate,
                                        bool  loop)
{
    if (src == nullptr) {
        clearAdditiveSource();
        return;
    }
    setAdditiveLayerSource(0, src, playRate, loop);
}

void AnimationPlayer::clearAdditiveSource()
{
    clearAdditiveLayerSource(0);
}

void AnimationPlayer::pause()  { _paused = true; }
void AnimationPlayer::resume() { _paused = false; }

// ===========================================================================
//  Time control
// ===========================================================================

void AnimationPlayer::setTime(float t)
{
    _time = t;
    if (_baseClip && _loop) {
        const float d = _baseClip->getDuration();
        if (d > 0.0f) {
            _time = t - std::floor(t / d) * d;   // wrap [0, d)
        }
    }
    // Phase 1.5: setTime is a seek. No notify fires on the seek itself;
    // mark prev = current so the next tick() fires anything in
    // [current, current + dt).
    _prevTickTime = _time;
    _pendingNotifies.clear();

    // P1.5 — multi-slot seek loop. Each slot jumps independently per
    // its loop flag, clears its pending notify queue, and resets its
    // prev-tick cursor. INV-6 syncToBase locks to _time post-seek (so
    // both axes land on the same frame). The curve re-anchor guard
    // (P1.4) generalizes per-slot.
    for (uint32_t slotIdx = 0; slotIdx < _additiveSlots.size(); ++slotIdx) {
        AdditiveSlot& s = _additiveSlots[slotIdx];
        if (s.clip == nullptr) continue;
        s.time = t;
        if (s.loop) {
            const float dAdd = s.clip->getDuration();
            if (dAdd > 0.0f) {
                s.time = t - std::floor(t / dAdd) * dAdd;
            }
        }
        s.prevTickTime = s.time;
        s.pendingNotifies.clear();
        if (s.syncToBase) {
            s.time = _time;
            s.prevTickTime = s.time;
        }
        // P1.4: curve clock is base `_time`, not the (possibly looping)
        // slot playhead — seeking outside the window re-anchors "now".
        if (s.curve.active) {
            const float curveStart = s.curve.startTime;
            const float curveEnd   = curveStart + s.curve.duration;
            if (_time < curveStart || _time > curveEnd) {
                s.curve.startTime = _time;
            }
        }
    }
    // Flush the merged queue too — a seek invalidates any unsent records.
    _pendingNotifiesMerged.clear();
}

// P1.3 — canonical weight setter (was setAdditiveWeight in P1.2). In
// P1.5 the static blend weight lives on the base track path (_blendWeight)
// AND on slot[0].curve.from (sampleLayerBlendCurve inactive path).
// Phase 1a per-track Additive still reads _blendWeight (P1.2 invariant);
// Phase 1b slot[0] reads sampleLayerBlendCurve → curve.from when inactive.
// Keep both in sync so setBlendWeight remains the single-slot P1.3 API.
void AnimationPlayer::setBlendWeight(float w)
{
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    _blendWeight = w;
    if (AdditiveSlot* s = getSlot(0)) {
        s->curve.from = w;
    }
}

// ===========================================================================
//  tick() — P1.5 multi-slot additive branches
// ===========================================================================

void AnimationPlayer::tick(float dt)
{
    // Phase 1.5: when paused or no clip, keep prev in sync with _time so
    // the next un-paused tick won't fire markers across [prev, _time)
    // for the time spent paused. Per-slot cursors stay synced too (INV-8
    // generalization: pause halts every layer's clock).
    if (_paused || _baseClip == nullptr) {
        _prevTickTime = _time;
        for (AdditiveSlot& s : _additiveSlots) {
            if (s.clip != nullptr) {
                s.prevTickTime = s.time;
            }
        }
        return;
    }

    const float prev = _time;
    _time += dt * _playRate;
    const float d = _baseClip->getDuration();
    bool wrapped = false;
    if (d > 0.0f) {
        if (_loop) {
            const float raw = _time;
            _time = _time - std::floor(_time / d) * d;   // wrap to [0, d)
            wrapped = (raw >= d) || (raw < 0.0f);
        } else if (_time > d) {
            _time = d;
            _paused = true;   // clamp to end-of-clip when not looping
        }
    }

    dispatchPendingNotifies(prev, _time, wrapped);
    _prevTickTime = _time;

    // P1.5 — per-slot additive branches. Order: slot 0 first, slot 7
    // last (deterministic paint order — UE "stack order = apply order").
    // Each slot branches independently on its own paused / syncToBase /
    // independent flags (INV-8 / INV-6 / INV-10 per-slot generalization).
    for (AdditiveSlot& s : _additiveSlots) {
        if (s.clip == nullptr) continue;

        if (s.paused) {
            // INV-8 — additive-only pause (or _paused top-of-tick gate).
            s.prevTickTime = s.time;
        } else if (s.syncToBase) {
            // INV-6 — lock-step. additive time mirrors _time; wrap
            // semantics carry from the base clip. Notify window shares
            // the same prev/next as the base.
            const float prevAdd = s.prevTickTime;
            s.time = _time;
            dispatchSlotNotifies(s, prevAdd, s.time, wrapped);
            s.prevTickTime = s.time;
        } else {
            // P1.3 — independent axis (the per-slot default).
            const float prevAdd = s.time;
            s.time += dt * s.playRate;
            const float dAdd = s.clip->getDuration();
            bool wrappedAdd = false;
            if (dAdd > 0.0f) {
                if (s.loop) {
                    const float rawAdd = s.time;
                    s.time = s.time - std::floor(s.time / dAdd) * dAdd;
                    wrappedAdd = (rawAdd >= dAdd) || (rawAdd < 0.0f);
                } else if (s.time > dAdd) {
                    s.time = dAdd;
                }
            }
            dispatchSlotNotifies(s, prevAdd, s.time, wrappedAdd);
            s.prevTickTime = s.time;
        }

        // P1.4 — curve auto-disarm per-slot on the BASE clock. Using
        // s.time here wrongly restarts the window whenever an independent
        // looping additive playhead wraps (tick past duration → t≈0 →
        // elapsed tiny → active stays true forever).
        if (s.curve.active) {
            const float elapsed = _time - s.curve.startTime;
            if (elapsed >= s.curve.duration) {
                s.curve.active = false;
                // Latch end weight so inactive sampleLayerBlendCurve
                // returns `to` (static weight takeover after the fade).
                s.curve.from = s.curve.to;
                if (&s == &_additiveSlots[0]) {
                    _blendWeight = s.curve.to;
                }
            }
        }
    }

    // P1.5 — rebuild the merged queue AFTER every per-slot dispatch
    // (so consumers of consumePendingNotifiesMerged see a snapshot
    // consistent with this tick's _pendingNotifies + every slot's
    // pendingNotifies). Cheap O(N log N) on typical N (10–100).
    rebuildMergedNotifies();
}

// ===========================================================================
//  P1.5 / P1.1 — AnimNotify dispatch
// ===========================================================================

void AnimationPlayer::dispatchPendingNotifies(float prev,
                                              float next,
                                              bool  wrapped)
{
    if (_baseClip == nullptr) return;
    const uint32_t n = _baseClip->getNotifyCount();
    if (n == 0u) return;

    const bool wantSink = static_cast<bool>(_animNotifySink);

    auto fireOne = [&](uint32_t i) {
        const char* nm = _baseClip->getNotifyName(i);
        const float  tm = _baseClip->getNotifyTime(i);
        const float  pl = _baseClip->getNotifyPayload(i);
        if (wantSink) {
            _animNotifySink(nm, tm, pl);
        }
        // P3.x刀 N+1.C — Per-state AnimNotify routing. Pushed record
        // carries the active state name (set by AYEntity SM bridge via
        // setCurrentStateName). fromStateName defaults to empty when
        // not driven by an SM (legacy / direct clip playback path).
        // 2-step: brace-init the 4 trivial fields, then patch the
        // std::string. Avoids potential issues with std::move on a
        // struct whose std::string was just default-constructed.
        AnimNotifyRecord rec{nm, tm, pl, AnimNotifySourceTag::Base};
        rec.fromStateName = _currentStateNameForNotify;
        _pendingNotifies.push_back(std::move(rec));
    };

    const float dur = _baseClip->getDuration();
    if (!wrapped) {
        const float lo = std::min(prev, next);
        const float hi = std::max(prev, next);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _baseClip->getNotifyTime(i);
            if (t < lo) continue;
            if (t > hi) break;
            fireOne(i);
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            const float t = _baseClip->getNotifyTime(i);
            const bool inA = (t >= prev) && (t <  dur);
            const bool inB = (t >= 0.0f) && (t <= next);
            if (inA || inB) fireOne(i);
        }
    }
}

const std::vector<AnimationPlayer::AnimNotifyRecord>&
AnimationPlayer::consumePendingNotifies()
{
    static thread_local std::vector<AnimNotifyRecord> s_returnSlot;
    s_returnSlot.clear();
    s_returnSlot.swap(_pendingNotifies);
    return s_returnSlot;
}

// ---------------------------------------------------------------------------
//  P1.5 — per-slot notify dispatch.
//
// Mirrors dispatchPendingNotifies but reads from a slot's per-slot
// pendingNotifies queue. The AnimNotifyRecord produced here carries
// AnimNotifySourceTag::Additive_N set in rebuildMergedNotifies (not
// here) — this dispatch only knows "a marker fired on slot K", the
// queue merger tags them.
//
// Host sink fires unconditionally (we don't filter source — a caller
// that wants per-source routing can subscribe to the merged bus path
// instead, which carries sourceTag).
// ---------------------------------------------------------------------------
void AnimationPlayer::dispatchSlotNotifies(AdditiveSlot& slot,
                                           float prev,
                                           float next,
                                           bool  wrapped)
{
    if (slot.clip == nullptr) return;
    const uint32_t n = slot.clip->getNotifyCount();
    if (n == 0u) return;

    const bool wantSink = static_cast<bool>(_animNotifySink);

    auto fireOne = [&](uint32_t i) {
        const char* nm = slot.clip->getNotifyName(i);
        const float  tm = slot.clip->getNotifyTime(i);
        const float  pl = slot.clip->getNotifyPayload(i);
        if (wantSink) {
            _animNotifySink(nm, tm, pl);
        }
        // sourceTag is set in rebuildMergedNotifies (we don't know our
        // slot index here — the per-slot queue is consumed by the merger
        // which owns the slot → tag mapping).
        // P3.x刀 N+1.C — Per-state AnimNotify routing. Per-slot records
        // also carry fromStateName from the player-level cached value
        // (set by AYEntity SM bridge on transition). Default empty
        // when not driven by an SM.
        AnimNotifyRecord rec{nm, tm, pl, AnimNotifySourceTag::Base};
        rec.fromStateName = _currentStateNameForNotify;
        slot.pendingNotifies.push_back(std::move(rec));
    };

    const float dur = slot.clip->getDuration();
    if (!wrapped) {
        const float lo = std::min(prev, next);
        const float hi = std::max(prev, next);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = slot.clip->getNotifyTime(i);
            if (t < lo) continue;
            if (t > hi) break;
            fireOne(i);
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            const float t = slot.clip->getNotifyTime(i);
            const bool inA = (t >= prev) && (t <  dur);
            const bool inB = (t >= 0.0f) && (t <= next);
            if (inA || inB) fireOne(i);
        }
    }
}

// ---------------------------------------------------------------------------
//  P1.5 — Merged notify rebuild + consume.
//
// INV-11: every record in _pendingNotifiesMerged has a sourceTag in
// {Base, Additive_0..7}; collision (time, name) drops the additive
// record (base wins).
//
// Algorithm:
//   (1) copy base records (tag = Base) + every slot's records
//       (tag = Additive_N) into the merged buffer;
//   (2) sort by (time, sourceTag) — stable secondary key ensures
//       dedup keeps the lower-tagged (base or earlier-slot) record;
//   (3) dedup-by-(time, name): for each additive record, drop it if a
//       base record with the same (time, name) exists; ties between
//       two additive slots keep the lower slot index (slot 0 wins).
// ---------------------------------------------------------------------------
void AnimationPlayer::rebuildMergedNotifies()
{
    _pendingNotifiesMerged.clear();
    _pendingNotifiesMerged.reserve(
        _pendingNotifies.size() +
        [&]() {
            size_t sum = 0;
            for (const AdditiveSlot& s : _additiveSlots) sum += s.pendingNotifies.size();
            return sum;
        }());

    // (1) base records — sourceTag = Base.
    for (const auto& r : _pendingNotifies) {
        AnimNotifyRecord copy = r;
        copy.sourceTag = AnimNotifySourceTag::Base;
        _pendingNotifiesMerged.push_back(copy);
    }
    // (1b) per-slot records — sourceTag = Additive_0..7 by slot index.
    for (uint32_t slotIdx = 0; slotIdx < _additiveSlots.size(); ++slotIdx) {
        const AnimNotifySourceTag tag = tagForSlotIndex(slotIdx);
        for (const auto& r : _additiveSlots[slotIdx].pendingNotifies) {
            AnimNotifyRecord copy = r;
            copy.sourceTag = tag;
            _pendingNotifiesMerged.push_back(copy);
        }
    }

    // (2) sort by (time, sourceTag). Stable + secondary tag sort gives
    // us "base before additive" within the same time, which the dedup
    // pass below relies on.
    std::sort(_pendingNotifiesMerged.begin(), _pendingNotifiesMerged.end(),
              [](const AnimNotifyRecord& a, const AnimNotifyRecord& b) {
                  if (a.time != b.time) return a.time < b.time;
                  return static_cast<uint8_t>(a.sourceTag) <
                         static_cast<uint8_t>(b.sourceTag);
              });

    // (3) dedup-by-(time, name): the FIRST record at (time, name) wins
    // (sorted primary time + secondary sourceTag means "first" =
    // base on collision, or lowest slot index on additive vs additive).
    // Sort marks the winners first; we walk the sorted vector and drop
    // any record whose (time, name) matches the previous winner.
    _pendingNotifiesMerged.erase(
        std::unique(_pendingNotifiesMerged.begin(), _pendingNotifiesMerged.end(),
            [](const AnimNotifyRecord& a, const AnimNotifyRecord& b) {
                if (a.time != b.time) return false;
                if (a.name == b.name) return true;   // pointer-equal fast path
                return (a.name != nullptr && b.name != nullptr
                        && std::strcmp(a.name, b.name) == 0);
            }),
        _pendingNotifiesMerged.end());

#ifndef NDEBUG
    // INV-11 sanity — every record's sourceTag ∈ {Base} ∪ {Additive_0..7}.
    for (const AnimNotifyRecord& r : _pendingNotifiesMerged) {
        const uint8_t t = static_cast<uint8_t>(r.sourceTag);
        assert(t <= static_cast<uint8_t>(AnimNotifySourceTag::Additive_7)
               && "INV-11: merged queue sourceTag out of range");
    }
#endif
}

const std::vector<AnimationPlayer::AnimNotifyRecord>&
AnimationPlayer::consumePendingNotifiesMerged()
{
    // (a) Make sure the merged buffer reflects the most recent tick.
    // Tick already rebuilt it; this is a no-op when called from
    // AnimationSystem right after tick(). Defensive rebuild in case a
    // caller mutated _pendingNotifies or a slot's queue between ticks.
    rebuildMergedNotifies();

    static thread_local std::vector<AnimNotifyRecord> s_returnMerged;
    s_returnMerged.clear();
    s_returnMerged.swap(_pendingNotifiesMerged);

    // (b) After draining the merged queue, also drain the per-source
    // queues it was built from. Without this the next tick's records
    // would append to non-empty per-source queues and the merger
    // would re-produce the same records.
    _pendingNotifies.clear();
    for (AdditiveSlot& s : _additiveSlots) {
        s.pendingNotifies.clear();
    }
    return s_returnMerged;
}

// ===========================================================================
//  evaluate() — P1.5 multi-slot additive Phase 1b
// ===========================================================================

void AnimationPlayer::evaluate()
{
    if (!isValid()) {
        return;
    }
    const size_t n = _skeleton->getBoneCount();
    if (n == 0) return;

    // P1.3 + P1.4 + P1.5 — INV-2 / INV-3 / INV-7 / INV-8 / INV-9 invariants.
    // Debug-only — Release builds skip the checks. These pin the contract
    // that downstream code (Phase 1a / 1b / capture / ref-pose) relies on:
    //   INV-2: _local{Pos,Rot,Scl}.size() matches skeleton bone count.
    //   INV-3: _blendWeight ∈ [0, 1].
    //   INV-7: per-slot refPoseCapture + captureState must be coherent.
    //   INV-8: per-slot paused ⇒ per-slot prevTickTime == slot.time.
    //   INV-9: per-slot refPoseCapture Valid ⇒ captured buffers sized n*3/n*4/n*3.
#ifndef NDEBUG
    assert(_localPos.size() == n * 3 && "INV-2: _localPos size must match n*3");
    assert(_localRot.size() == n * 4 && "INV-2: _localRot size must match n*4");
    assert(_localScl.size() == n * 3 && "INV-2: _localScl size must match n*3");
    assert(_blendWeight >= 0.0f && _blendWeight <= 1.0f
           && "INV-3: _blendWeight must be in [0, 1] post-setter saturate");
    for (const AdditiveSlot& s : _additiveSlots) {
        if (s.clip == nullptr) continue;
        if (s.paused) {
            assert(s.prevTickTime == s.time
                   && "INV-8: paused slot must have prev cursor == current time");
        }
        // INV-7 + INV-9 — only when ref-pose is actually active on this slot.
        if (s.refPoseCapture) {
            assert((s.captureState == CaptureState::Fresh
                    || s.captureState == CaptureState::Valid
                    || s.captureState == CaptureState::Stale)
                   && "INV-7: per-slot captureState must be a known value");
            if (s.captureState == CaptureState::Valid) {
                assert(s.capturedLocalPos.size() == n * 3
                       && "INV-9: Valid captureState must have buffers sized to n*3");
                assert(s.capturedLocalRot.size() == n * 4
                       && "INV-9: Valid captureState must have buffers sized to n*4");
                assert(s.capturedLocalScl.size() == n * 3
                       && "INV-9: Valid captureState must have buffers sized to n*3");
            }
        }
    }
    // Degenerate state — base=null + ANY slot bound. Rest-pose seed +
    // early-return so the world/skin matrices are well-defined.
    const bool hasAnySlot = [&]() {
        for (const AdditiveSlot& s : _additiveSlots) if (s.clip != nullptr) return true;
        return false;
    }();
    if (_baseClip == nullptr && hasAnySlot) {
        const ayt::math::FVector3*    restPos = _skeleton->getLocalPositions();
        const ayt::math::FQuaternion* restRot = _skeleton->getLocalRotations();
        const ayt::math::FVector3*    restScl = _skeleton->getLocalScales();
        if (restPos != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localPos[i * 3 + 0] = restPos[i].x;
                _localPos[i * 3 + 1] = restPos[i].y;
                _localPos[i * 3 + 2] = restPos[i].z;
            }
        }
        if (restRot != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localRot[i * 4 + 0] = restRot[i].x;
                _localRot[i * 4 + 1] = restRot[i].y;
                _localRot[i * 4 + 2] = restRot[i].z;
                _localRot[i * 4 + 3] = restRot[i].w;
            }
        }
        if (restScl != nullptr) {
            for (size_t i = 0; i < n; ++i) {
                _localScl[i * 3 + 0] = restScl[i].x;
                _localScl[i * 3 + 1] = restScl[i].y;
                _localScl[i * 3 + 2] = restScl[i].z;
            }
        }
        // P4-1 — same world loop as the main path, via the shared helper
        // (the IK pass does NOT run here — debug-only early-return path).
        accumulateWorldFrom(0);
        const ayt::math::Float4x4* ibm = _skeleton->getInverseBindMatrices();
        for (size_t i = 0; i < n; ++i) {
            _skin[i] = (ibm != nullptr) ? (_world[i] * ibm[i]) : _world[i];
        }
        return;
    }
#endif

    // Re-seed local TRS from rest pose every frame (cheaper than tracking
    // which bones got a track written into last frame).
    const ayt::math::FVector3*    restPos = _skeleton->getLocalPositions();
    const ayt::math::FQuaternion* restRot = _skeleton->getLocalRotations();
    const ayt::math::FVector3*    restScl = _skeleton->getLocalScales();
    if (restPos != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localPos[i * 3 + 0] = restPos[i].x;
            _localPos[i * 3 + 1] = restPos[i].y;
            _localPos[i * 3 + 2] = restPos[i].z;
        }
    }
    if (restRot != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localRot[i * 4 + 0] = restRot[i].x;
            _localRot[i * 4 + 1] = restRot[i].y;
            _localRot[i * 4 + 2] = restRot[i].z;
            _localRot[i * 4 + 3] = restRot[i].w;
        }
    }
    if (restScl != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            _localScl[i * 3 + 0] = restScl[i].x;
            _localScl[i * 3 + 1] = restScl[i].y;
            _localScl[i * 3 + 2] = restScl[i].z;
        }
    }

    // INV-7 Phase 0 — Valid slots re-seed `_local*` from the previous
    // post-Phase-1a capture so bones the base clip does not write keep
    // their captured base (not rest). Fresh/Stale skip apply; they
    // capture after Phase 1a below.
    for (AdditiveSlot& s : _additiveSlots) {
        if (s.clip == nullptr || !s.refPoseCapture) continue;
        if (s.captureState == CaptureState::Valid) {
            applyCapturedRefPoseFromSlot(s);
        }
    }

    // Phase 1 — sample every track at _time, write into the matched bone's
    // local slot. Tracks whose nodeName doesn't resolve are silently skipped
    // (plan §5.4 — missing optional animation must not crash).
    const bool hasSink = static_cast<bool>(_floatSink);
    for (TrackSlice& tr : _tracks) {
        resolveBoneIdxOnce(tr);
        const int boneIdx = tr.boneIdx;
        if (boneIdx < 0) {
            if (hasSink && tr.type == ayt::resource::AnimTrackType::Float) {
                float v = 0.0f;
                sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                 tr.timesSec, _time, v);
                _floatSink(tr.nodeName.c_str(),
                           tr.property.empty() ? nullptr : tr.property.c_str(),
                           v);
            }
            continue;
        }
        const size_t idx = static_cast<size_t>(boneIdx);

        // P2.2 — bone mask is applied once in the Phase 2 pre-lerp
        // (INV-12 / Q3). Phase 1a writes full Override / Additive so
        // the post-write lerp(rest, _local, mask) scales both uniformly.
        // Multiplying mask here would square it against Phase 2.

        switch (tr.type) {
            case ayt::resource::AnimTrackType::Vector3: {
                ayt::math::FVector3 v;
                sampleTrackVector3(tr.vec3Values.data(), tr.timesSec.size(),
                                   tr.timesSec, _time, v);
                if (tr.property == "position") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        _localPos[idx * 3 + 0] = v.x;
                        _localPos[idx * 3 + 1] = v.y;
                        _localPos[idx * 3 + 2] = v.z;
                    } else {
                        const float w = _blendWeight;
                        _localPos[idx * 3 + 0] += v.x * w;
                        _localPos[idx * 3 + 1] += v.y * w;
                        _localPos[idx * 3 + 2] += v.z * w;
                    }
                } else if (tr.property == "scale") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        _localScl[idx * 3 + 0] = v.x;
                        _localScl[idx * 3 + 1] = v.y;
                        _localScl[idx * 3 + 2] = v.z;
                    } else {
                        const float w = _blendWeight;
                        _localScl[idx * 3 + 0] *= (1.0f + v.x * w);
                        _localScl[idx * 3 + 1] *= (1.0f + v.y * w);
                        _localScl[idx * 3 + 2] *= (1.0f + v.z * w);
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Quaternion: {
                ayt::math::FQuaternion q;
                sampleTrackQuaternion(tr.quatValues.data(), tr.timesSec.size(),
                                      tr.timesSec, _time, q);
                if (tr.property == "rotation") {
                    if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                        _localRot[idx * 4 + 0] = q.x;
                        _localRot[idx * 4 + 1] = q.y;
                        _localRot[idx * 4 + 2] = q.z;
                        _localRot[idx * 4 + 3] = q.w;
                    } else {
                        const float trackW = _blendWeight;
                        if (trackW <= 0.0f) {
                            // weight=0 → identity, leave _localRot untouched
                        } else {
                            ayt::math::FQuaternion base(
                                _localRot[idx * 4 + 0],
                                _localRot[idx * 4 + 1],
                                _localRot[idx * 4 + 2],
                                _localRot[idx * 4 + 3]);
                            ayt::math::FQuaternion scaled = q.pow(trackW);
                            ayt::math::FQuaternion blended = (base * scaled).normalize();
                            _localRot[idx * 4 + 0] = blended.x;
                            _localRot[idx * 4 + 1] = blended.y;
                            _localRot[idx * 4 + 2] = blended.z;
                            _localRot[idx * 4 + 3] = blended.w;
                        }
                    }
                }
                break;
            }
            case ayt::resource::AnimTrackType::Float: {
                float v = 0.0f;
                sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                 tr.timesSec, _time, v);
                if (hasSink) {
                    _floatSink(tr.nodeName.c_str(),
                               tr.property.empty() ? nullptr : tr.property.c_str(),
                               v);
                }
                break;
            }
        }
    }

    // INV-7 — capture post-Phase-1a base for every ref-pose slot.
    // Phase 1b adds deltas on top of this `_local*`; it must NOT
    // re-capture the post-additive final pose (that double-counts).
    for (AdditiveSlot& s : _additiveSlots) {
        if (s.clip == nullptr || !s.refPoseCapture) continue;
        captureRefPoseFromSlot(s);
        s.captureState = CaptureState::Valid;
    }

    // Phase 1b — P1.5 ADDITIVE LAYER STACK.
    //
    // Per-slot loop over _additiveSlots (ordered: slot 0 first, slot 7
    // last). Each slot independently:
    //   - Samples every track at slot.time (per-slot independent axis).
    //   - Applies the per-track blendMode math (Override → write,
    //     Additive → += / *= / q.pow()).
    //   - Honors the per-track mask (slot K's trackWeights[] indexed by
    //     track index; empty → uniform 1.0f).
    // Ref-pose apply/capture already ran above (Phase 0 / post-1a).
    for (uint32_t slotIdx = 0; slotIdx < _additiveSlots.size(); ++slotIdx) {
        AdditiveSlot& s = _additiveSlots[slotIdx];
        if (s.clip == nullptr) continue;
        const float effectiveWeight = sampleLayerBlendCurve(s);
        if (effectiveWeight <= 0.0f) continue;

        // Sample time = s.time (per-slot independent from base _time).
        for (TrackSlice& tr : s.tracks) {
            resolveBoneIdxOnce(tr);
            const int boneIdx = tr.boneIdx;
            if (boneIdx < 0) {
                // Per-track orphan: Float tracks still surface to sink.
                if (hasSink && tr.type == ayt::resource::AnimTrackType::Float) {
                    float v = 0.0f;
                    sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                     tr.timesSec, s.time, v);
                    _floatSink(tr.nodeName.c_str(),
                               tr.property.empty() ? nullptr : tr.property.c_str(),
                               v);
                }
                continue;
            }
            const size_t idx = static_cast<size_t>(boneIdx);

            // P1.5 per-track mask — empty vector = uniform, otherwise
            // multiply by trackWeights[k] indexed by this track's index
            // within the slot's tracks array.
            // Track index in slot.tracks: we use &tr - &s.tracks[0] which
            // gives a stable offset since we never reorder tracks.
            //
            // INV-16: P2.2 bone mask is orthogonal and applied later via
            // Phase 2 post-write lerp — do NOT multiply boneMaskW here
            // or mask weight is squared against Phase 2.
            const size_t trackIdx = static_cast<size_t>(&tr - &s.tracks[0]);
            const float trackW =
                (s.trackWeights.empty() || trackIdx >= s.trackWeights.size())
                  ? effectiveWeight
                  : (effectiveWeight * s.trackWeights[trackIdx]);

            switch (tr.type) {
                case ayt::resource::AnimTrackType::Vector3: {
                    ayt::math::FVector3 v;
                    sampleTrackVector3(tr.vec3Values.data(), tr.timesSec.size(),
                                       tr.timesSec, s.time, v);
                    if (tr.property == "position") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            _localPos[idx * 3 + 0] = v.x;
                            _localPos[idx * 3 + 1] = v.y;
                            _localPos[idx * 3 + 2] = v.z;
                        } else {
                            _localPos[idx * 3 + 0] += v.x * trackW;
                            _localPos[idx * 3 + 1] += v.y * trackW;
                            _localPos[idx * 3 + 2] += v.z * trackW;
                        }
                    } else if (tr.property == "scale") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            _localScl[idx * 3 + 0] = v.x;
                            _localScl[idx * 3 + 1] = v.y;
                            _localScl[idx * 3 + 2] = v.z;
                        } else {
                            _localScl[idx * 3 + 0] *= (1.0f + v.x * trackW);
                            _localScl[idx * 3 + 1] *= (1.0f + v.y * trackW);
                            _localScl[idx * 3 + 2] *= (1.0f + v.z * trackW);
                        }
                    }
                    break;
                }
                case ayt::resource::AnimTrackType::Quaternion: {
                    ayt::math::FQuaternion q;
                    sampleTrackQuaternion(tr.quatValues.data(), tr.timesSec.size(),
                                          tr.timesSec, s.time, q);
                    if (tr.property == "rotation") {
                        if (tr.blendMode == ayt::resource::AnimBlendMode::Override) {
                            _localRot[idx * 4 + 0] = q.x;
                            _localRot[idx * 4 + 1] = q.y;
                            _localRot[idx * 4 + 2] = q.z;
                            _localRot[idx * 4 + 3] = q.w;
                        } else {
                            if (trackW <= 0.0f) {
                                // leave _localRot untouched
                            } else {
                                ayt::math::FQuaternion base(
                                    _localRot[idx * 4 + 0],
                                    _localRot[idx * 4 + 1],
                                    _localRot[idx * 4 + 2],
                                    _localRot[idx * 4 + 3]);
                                ayt::math::FQuaternion scaled = q.pow(trackW);
                                ayt::math::FQuaternion blended = (base * scaled).normalize();
                                _localRot[idx * 4 + 0] = blended.x;
                                _localRot[idx * 4 + 1] = blended.y;
                                _localRot[idx * 4 + 2] = blended.z;
                                _localRot[idx * 4 + 3] = blended.w;
                            }
                        }
                    }
                    break;
                }
                case ayt::resource::AnimTrackType::Float: {
                    // P1.2 invariant: Float tracks go to the host sink
                    // regardless of blendMode. Per-slot Float surfaces at
                    // slot.time so a host can wire gameplay parameters
                    // that ride along with the additive layer.
                    float v = 0.0f;
                    sampleTrackFloat(tr.scalarValues.data(), tr.timesSec.size(),
                                     tr.timesSec, s.time, v);
                    if (hasSink) {
                        _floatSink(tr.nodeName.c_str(),
                                   tr.property.empty() ? nullptr : tr.property.c_str(),
                                   v);
                    }
                    break;
                }
            }
        }
    }

    // P2.2 — Phase 2 pre-step: sole bone-mask gate (INV-12 / Q3).
    // Lerp every bone whose _boneMaskWeights[i] < 1.0f from rest toward
    // the just-written _local* by mask weight. Runs AFTER all Phase 1a
    // / 1b writes so Override and Additive are gated uniformly. Skipped
    // entirely when no mask is bound (INV-15 — zero cost).
    //
    // INV-16: P1.5 trackWeights already scaled additive writes in Phase
    // 1b; this lerp then applies boneMask → final = trackW × boneMask.
    //
    // Quaternion interpolation: FQuaternion has no nlerp() in MathTypes;
    // we use slerp(b, t) which is the canonical rotation interpolator
    // and handles the partial-lerp use case correctly. When mask == 0
    // we snap directly to rest (avoids a slerp call when the answer is
    // trivially known).
    if (!_boneMaskWeights.empty()) {
        const ayt::math::FVector3*    restPos = _skeleton->getLocalPositions();
        const ayt::math::FQuaternion* restRot = _skeleton->getLocalRotations();
        const ayt::math::FVector3*    restScl = _skeleton->getLocalScales();
        for (size_t i = 0; i < n; ++i) {
            const float w = _boneMaskWeights[i];
            if (w >= 1.0f) continue;     // no-op — common case (no mask bound)
            if (w <= 0.0f) {
                // Snap to rest.
                if (restPos) {
                    _localPos[i * 3 + 0] = restPos[i].x;
                    _localPos[i * 3 + 1] = restPos[i].y;
                    _localPos[i * 3 + 2] = restPos[i].z;
                }
                if (restRot) {
                    _localRot[i * 4 + 0] = restRot[i].x;
                    _localRot[i * 4 + 1] = restRot[i].y;
                    _localRot[i * 4 + 2] = restRot[i].z;
                    _localRot[i * 4 + 3] = restRot[i].w;
                }
                if (restScl) {
                    _localScl[i * 3 + 0] = restScl[i].x;
                    _localScl[i * 3 + 1] = restScl[i].y;
                    _localScl[i * 3 + 2] = restScl[i].z;
                }
            } else {
                if (restPos) {
                    _localPos[i * 3 + 0] = restPos[i].x + (_localPos[i * 3 + 0] - restPos[i].x) * w;
                    _localPos[i * 3 + 1] = restPos[i].y + (_localPos[i * 3 + 1] - restPos[i].y) * w;
                    _localPos[i * 3 + 2] = restPos[i].z + (_localPos[i * 3 + 2] - restPos[i].z) * w;
                }
                if (restRot) {
                    const ayt::math::FQuaternion restQR(
                        restRot[i].x, restRot[i].y,
                        restRot[i].z, restRot[i].w);
                    const ayt::math::FQuaternion liveQ(
                        _localRot[i * 4 + 0], _localRot[i * 4 + 1],
                        _localRot[i * 4 + 2], _localRot[i * 4 + 3]);
                    const ayt::math::FQuaternion blended = restQR.slerp(liveQ, w);
                    _localRot[i * 4 + 0] = blended.x;
                    _localRot[i * 4 + 1] = blended.y;
                    _localRot[i * 4 + 2] = blended.z;
                    _localRot[i * 4 + 3] = blended.w;
                }
                if (restScl) {
                    _localScl[i * 3 + 0] = restScl[i].x + (_localScl[i * 3 + 0] - restScl[i].x) * w;
                    _localScl[i * 3 + 1] = restScl[i].y + (_localScl[i * 3 + 1] - restScl[i].y) * w;
                    _localScl[i * 3 + 2] = restScl[i].z + (_localScl[i * 3 + 2] - restScl[i].z) * w;
                }
            }
        }
    }

    // Phase 2 — accumulate world = parent.world * localTRS. The loop
    // body lives in accumulateWorldFrom() so the P4-1 IK pass can
    // rebuild only the affected subtree; a full pass is start = 0.
    accumulateWorldFrom(0);

    // Phase 2.5 — P4-1/P4-2 IK pass. Runs AFTER the bone-mask gate (P2.2)
    // so IK is an absolute post-process lock — the mask does NOT gate IK
    // (documented; future slices may add per-chain mask gating, design
    // §4.25.11). applyIKChain() writes only the _localRot of the chain's
    // non-tip bones; localPos/localScl untouched. The debug-only
    // degenerate branch (base=null + slots, above) early-returns BEFORE
    // this pass — documented asymmetry: release builds reach IK via the
    // main path for the same transient state (design §4.25.5).
    if (!_ikChains.empty()) {
        for (IKChain& ch : _ikChains) {
            applyIKChain(ch);   // zero-cost skip on inactive / degenerate (INV-72/76)
        }
    }

    // Phase 3 — skin matrix = world * inverseBindMatrix.
    const ayt::math::Float4x4* ibm = _skeleton->getInverseBindMatrices();
    for (size_t i = 0; i < n; ++i) {
        if (ibm != nullptr) {
            _skin[i] = _world[i] * ibm[i];
        } else {
            _skin[i] = _world[i];
        }
    }
}

// ===========================================================================
//  P4-1 — Two-Bone IK pass helpers
// ===========================================================================

// P4-1 — rebuild _world[i] = parent.world * localTRS for i in [start, n).
// Phase 2 calls this with start = 0 (full pass); the IK pass calls it per
// chain with start = chain root index. The hard parentIndex < childIndex
// invariant (asserted in setSkeleton) means [start, n) covers exactly the
// chain's subtree; rows whose parents live before `start` reuse the
// already-correct Phase 2 parent values and recompute to identical
// results — zero branches, no bookkeeping (design §4.25.5).
void AnimationPlayer::accumulateWorldFrom(size_t start)
{
    const size_t n = _skeleton->getBoneCount();
    const ayt::resource::Bone* bones = _skeleton->getBones();
    for (size_t i = start; i < n; ++i) {
        const int p = bones[i].parentIndex;
        const ayt::math::FVector3    lp(_localPos[i * 3 + 0],
                                        _localPos[i * 3 + 1],
                                        _localPos[i * 3 + 2]);
        const ayt::math::FQuaternion lr(_localRot[i * 4 + 0],
                                        _localRot[i * 4 + 1],
                                        _localRot[i * 4 + 2],
                                        _localRot[i * 4 + 3]);
        const ayt::math::FVector3    ls(_localScl[i * 3 + 0],
                                        _localScl[i * 3 + 1],
                                        _localScl[i * 3 + 2]);
        const ayt::math::Float4x4 local = ayt::math::Float4x4::fromTRS(lp, lr, ls);
        if (p < 0 || static_cast<size_t>(p) >= n) {
            _world[i] = local;
        } else {
            _world[i] = _world[static_cast<size_t>(p)] * local;
        }
    }
}

void AnimationPlayer::writeLocalRot(size_t i, const ayt::math::FQuaternion& q)
{
    _localRot[i * 4 + 0] = q.x;
    _localRot[i * 4 + 1] = q.y;
    _localRot[i * 4 + 2] = q.z;
    _localRot[i * 4 + 3] = q.w;
}

// P4-1/P4-2 — eager resolver (mirror of resolveSkeletonMask:1779): look up
// the bone names via findBone, then validate chain topology.
//
// TwoBone (P4-1 path, kept bit-identical): root must be an ancestor of mid
// and mid of tip (parentIndex < childIndex walk from the higher index) —
// resolves to the 3-entry path {r, m, t}.
//
// FABRIK/CCD (P4-2): the full root→tip path is AUTO-DERIVED by a single
// tip→root parent walk; midBone is IGNORED for these (design §4.26.3).
// Disabled when the walk fails (passed root without seeing it, or walked
// off the top), when root == tip (path < 2 joints), or when the path
// exceeds kMaxIKChainBones (fixed-size solver arrays, zero per-frame
// alloc — design §4.26.5).
//
// A miss or invalid topology disables the chain in place (empty path) —
// evaluate() skips it, never crashes. Idempotent; no-op when the skeleton
// is null; bumps _ikGeneration.
void AnimationPlayer::resolveIKChains()
{
    if (_skeleton == nullptr) {
        ++_ikGeneration;
        return;
    }
    const size_t n = _skeleton->getBoneCount();

    for (IKChain& ch : _ikChains) {
        if (ch.spec.rootBone.empty() && ch.spec.midBone.empty() && ch.spec.tipBone.empty()) {
            continue;   // unbound slot — untouched
        }
        // NOTE: direct findBone, NOT AssetBoneCache (unchanged from P4-1).
        // IK resolve runs only on bind / skeleton swap (not per-frame), so
        // the cache buys nothing — and the cache keys on the skeleton
        // POINTER without tracking its lifetime: a freed skeleton's address
        // can be reused by a new skeleton, reviving a stale (addr, name)
        // entry and falsely re-resolving a chain whose names miss on the
        // NEW skeleton (surfaced by P5, design §4.25.9). findBone is exact.
        const int32_t r = _skeleton->findBone(ch.spec.rootBone.c_str());
        const int32_t t = _skeleton->findBone(ch.spec.tipBone.c_str());
        ch.path.clear();
        if (r < 0 || t < 0) continue;                   // name miss → disabled
        if (static_cast<size_t>(r) >= n || static_cast<size_t>(t) >= n) continue;

        if (ch.spec.type != IKSolverType::TwoBone) {
            // FABRIK / CCD — auto-derive the full path: single walk from
            // tip up the parent chain, collecting on the way (reversed at
            // the end). The parentIndex < childIndex invariant makes the
            // walk strictly decreasing, so it terminates.
            if (r == t) continue;                       // path < 2 joints
            std::vector<int32_t> rev;
            int32_t cursor = t;
            for (;;) {
                rev.push_back(cursor);
                if (cursor == r) break;
                if (cursor < r) { rev.clear(); break; } // passed root — not a descendant
                cursor = _skeleton->getBones()[static_cast<size_t>(cursor)].parentIndex;
                if (cursor < 0) { rev.clear(); break; } // walked off the top
            }
            if (rev.size() < 2) continue;
            if (rev.size() > kMaxIKChainBones) continue; // over-cap → disabled
            ch.path.assign(rev.rbegin(), rev.rend());
            continue;
        }

        // P4-1 TwoBone — exact legacy logic: find mid, validate the two
        // ancestor steps, resolve {r, m, t}.
        const int32_t m = _skeleton->findBone(ch.spec.midBone.c_str());
        if (m < 0 || static_cast<size_t>(m) >= n) continue;
        int32_t cursor = t;
        bool midIsAncestorOfTip = false;
        while (cursor >= 0) {
            if (cursor == m) { midIsAncestorOfTip = true; break; }
            if (cursor < m) break;                      // passed mid — impossible
            cursor = _skeleton->getBones()[static_cast<size_t>(cursor)].parentIndex;
        }
        if (!midIsAncestorOfTip) continue;
        cursor = m;
        bool rootIsAncestorOfMid = false;
        while (cursor >= 0) {
            if (cursor == r) { rootIsAncestorOfMid = true; break; }
            if (cursor < r) break;
            cursor = _skeleton->getBones()[static_cast<size_t>(cursor)].parentIndex;
        }
        if (!rootIsAncestorOfMid) continue;
        ch.path = { r, m, t };
    }
    ++_ikGeneration;
}

// P4-2 — unified Phase 2.5 pass for ONE bound chain (design §4.26.4).
// Returns false (no state touched) when the chain is inactive or
// degenerate — the per-frame loop relies on this as the zero-cost gate.
bool AnimationPlayer::applyIKChain(IKChain& ch)
{
    if (ch.spec.weight <= 0.0f) return false;   // INV-72 — zero-cost skip
    const size_t cnt = ch.path.size();
    if (cnt < 2) return false;                  // disabled / degenerate (INV-76)

    // 1. Snapshot world joint positions + non-tip world rotations. The tip
    // joint's rotation is never written back by any solver — only its
    // position enters the solve (localRot stays the Phase 1 sample).
    ayt::math::FVector3    jointPos[kMaxIKChainBones];
    ayt::math::FQuaternion jointRot[kMaxIKChainBones];
    for (size_t i = 0; i < cnt; ++i) {
        jointPos[i] = _world[static_cast<size_t>(ch.path[i])].transformPoint(
            ayt::math::FVector3(0, 0, 0));
    }
    for (size_t i = 0; i + 1 < cnt; ++i) {
        ayt::math::FVector3    tr; ayt::math::FQuaternion rr; ayt::math::FVector3 scl;
        if (!_world[static_cast<size_t>(ch.path[i])].decompose(tr, rr, scl)) {
            return false;   // singular — leave the chain untouched
        }
        jointRot[i] = rr;
    }

    // 2. Dispatch by solver type. newWorld[i] = the chain bone's NEW world
    // rotation; the tip entry is copied through (never written back).
    ayt::math::FQuaternion newWorld[kMaxIKChainBones];
    if (ch.spec.type != IKSolverType::TwoBone) {
        const IterativeIKResult res = (ch.spec.type == IKSolverType::FABRIK)
            ? FabrikSolver::solve(jointPos, jointRot, static_cast<uint32_t>(cnt),
                                  ch.spec.targetWorld, ch.spec.weight, ch.spec.iterations)
            : CcdSolver::solve(jointPos, jointRot, static_cast<uint32_t>(cnt),
                               ch.spec.targetWorld, ch.spec.weight, ch.spec.iterations);
        for (size_t i = 0; i + 1 < cnt; ++i) newWorld[i] = res.worldRot[i];
        // newWorld[cnt-1] (tip) intentionally unused — never written back.
    } else {
        // P4-1 TwoBone — analytic two-segment solve; the resolved path is
        // exactly {r, m, t} (3 entries). Equivalence with the P4-1 inline
        // code is bit-exact: same inputs, same solve, same write-back
        // (the two-bone branch below reduces to the P4-1 root/mid writes).
        const TwoBoneIKResult res = TwoBoneSolver::solve(
            jointPos[0], jointPos[1], jointPos[2], jointRot[0], jointRot[1],
            ch.spec.targetWorld, ch.spec.weight);
        newWorld[0] = res.rootRotation;
        newWorld[1] = res.midRotation;
        // newWorld[2] (tip) intentionally unused — never written back.
    }

    // 3. World → local write-back for the N-1 non-tip bones. For i == 0 the
    // parent world rotation comes from OUTSIDE the chain (identity when
    // the chain root is a skeleton root — the P4-1 pr < 0 branch); for
    // i > 0 it is newWorld[i-1] — the parent was just solved.
    // conjugate() == inverse for unit quaternions (decompose guarantees
    // unit rotation parts).
    for (size_t i = 0; i + 1 < cnt; ++i) {
        ayt::math::FQuaternion parentWorld;
        if (i == 0) {
            const int pr = _skeleton->getBones()[static_cast<size_t>(ch.path[0])].parentIndex;
            if (pr < 0) {
                parentWorld = ayt::math::FQuaternion();   // identity
            } else {
                ayt::math::FVector3    tr; ayt::math::FQuaternion rr; ayt::math::FVector3 scl;
                if (!_world[static_cast<size_t>(pr)].decompose(tr, rr, scl)) {
                    return false;   // singular parent — leave the chain untouched
                }
                parentWorld = rr;
            }
        } else {
            parentWorld = newWorld[i - 1];
        }
        writeLocalRot(static_cast<size_t>(ch.path[i]),
                      parentWorld.conjugate() * newWorld[i]);
    }

    // 4. Rebuild the chain subtree's world rows (Phase 3 reads _world).
    accumulateWorldFrom(static_cast<size_t>(ch.path[0]));
    return true;
}

// ===========================================================================
//  P4-1 — Two-Bone IK public API
// ===========================================================================

bool AnimationPlayer::setIKChain(uint32_t chainId, const IKChainSpec& spec)
{
    if (chainId >= kMaxIKChains) {
        return false;   // cap hit — silently ignore (defensive)
    }
    if (chainId >= _ikChains.size()) {
        _ikChains.resize(static_cast<size_t>(chainId) + 1);
    }
    IKChain& ch = _ikChains[chainId];
    ch.spec = spec;
    ch.path.clear();
    resolveIKChains();   // eager resolve (INV-71) — bumps _ikGeneration
    return true;
}

void AnimationPlayer::clearIKChain(uint32_t chainId)
{
    if (chainId >= _ikChains.size()) return;
    IKChain& ch = _ikChains[chainId];
    ch.spec = IKChainSpec{};
    ch.path.clear();
    ++_ikGeneration;
}

void AnimationPlayer::clearAllIKChains()
{
    _ikChains.clear();
    ++_ikGeneration;
}

void AnimationPlayer::setIKChainTarget(uint32_t chainId, const ayt::math::FVector3& worldPos)
{
    if (chainId >= _ikChains.size()) return;
    _ikChains[chainId].spec.targetWorld = worldPos;
}

void AnimationPlayer::setIKChainWeight(uint32_t chainId, float weight)
{
    if (chainId >= _ikChains.size()) return;
    // Saturate [0, 1] (INV-72) — mirrors setBlendWeight semantics.
    _ikChains[chainId].spec.weight =
        (weight < 0.0f) ? 0.0f : ((weight > 1.0f) ? 1.0f : weight);
}

const IKChainSpec& AnimationPlayer::getIKChain(uint32_t chainId) const
{
    static const IKChainSpec kEmptySpec;
    if (chainId >= _ikChains.size()) return kEmptySpec;
    return _ikChains[chainId].spec;
}

size_t AnimationPlayer::getIKChainCount() const
{
    size_t count = 0;
    for (const IKChain& ch : _ikChains) {
        if (!ch.spec.rootBone.empty() || !ch.spec.tipBone.empty()) {
            ++count;
        }
    }
    return count;
}

bool AnimationPlayer::isIKChainActive(uint32_t chainId) const
{
    if (chainId >= _ikChains.size()) return false;
    const IKChain& ch = _ikChains[chainId];
    return !ch.path.empty() && ch.spec.weight > 0.0f;
}

// ===========================================================================
//  P1.5 — Per-slot public API (11 methods + 2 read-back getters)
// ===========================================================================

bool AnimationPlayer::setAdditiveLayerSource(uint32_t slotId,
                                             const ayt::resource::IAnimation* src,
                                             float playRate,
                                             bool  loop)
{
    if (slotId >= kMaxAdditiveSlots) {
        // Cap hit — silently ignore (defensive). Caller should check
        // getAdditiveLayerCount() == slotId before binding to avoid
        // losing the bind silently.
        return false;
    }
    if (src == nullptr) {
        clearAdditiveLayerSource(slotId);
        return true;
    }
    AdditiveSlot& s = ensureSlot(slotId);
    s.clip              = src;
    s.time              = 0.0f;
    s.prevTickTime      = 0.0f;
    s.playRate          = playRate;
    s.loop              = loop;
    s.pendingNotifies.clear();
    rebuildSlotTracks(s, src);

    // P1.4 rebind: a fresh source means a fresh cross-fade config.
    // Host can re-enable each option AFTER binding if desired.
    s.syncToBase       = false;
    s.refPoseCapture   = false;
    s.captureState     = CaptureState::Fresh;
    s.paused           = false;
    s.curve.active     = false;
    // Inherit the current static weight. Default BlendCurve::from is 0,
    // which would make isAdditiveLayerActive / Phase 1b treat a freshly
    // bound layer as weight-0 (silent no-op) until setBlendWeight.
    s.curve.from       = (slotId == 0) ? _blendWeight : 1.0f;
    // P1.5 NEW — per-track mask resets to uniform (empty vector).
    s.trackWeights.clear();
    return true;
}

// P4 polish (2026-08-10) — INV-61. Release the slot's heavy buffers back
// to the allocator. tracks (per-track boneIdx cache), pendingNotifies,
// capturedLocal{Pos,Rot,Scl} (n*3 / n*4 / n*3 floats, sized at
// setSkeleton) and trackWeights are the multi-KB members of AdditiveSlot;
// swap-with-empty frees capacity while plain clear() keeps the
// allocation. A host that clears an idle slot (or stop()s the player)
// gets the memory back; a later re-bind reallocates once via
// rebuildSlotTracks / capture — acceptable, bind is a low-frequency op.
// The `_additiveSlots` vector itself is untouched (sparse semantics,
// INV-62), and the slot's scalar fields are preserved so the re-bind
// contract (fresh cross-fade state) is unchanged.
void AnimationPlayer::releaseSlotBuffers(AdditiveSlot& slot)
{
    std::vector<TrackSlice>().swap(slot.tracks);
    std::vector<AnimNotifyRecord>().swap(slot.pendingNotifies);
    std::vector<float>().swap(slot.capturedLocalPos);
    std::vector<float>().swap(slot.capturedLocalRot);
    std::vector<float>().swap(slot.capturedLocalScl);
    std::vector<float>().swap(slot.trackWeights);
}

void AnimationPlayer::clearAdditiveLayerSource(uint32_t slotId)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return;
    s->clip = nullptr;
    s->time = 0.0f;
    s->prevTickTime = 0.0f;
    releaseSlotBuffers(*s);
    // Note: playRate / loop are NOT reset — they describe the slot's
    // defaults, not its current state. _blendWeight is preserved for
    // re-bind without re-set.
    // P1.4 cross-fade config resets to fresh state.
    s->syncToBase     = false;
    s->refPoseCapture = false;
    s->captureState   = CaptureState::Fresh;
    s->paused         = false;
    s->curve.active   = false;
}

int32_t AnimationPlayer::getAdditiveLayerCount() const
{
    int32_t cnt = 0;
    for (const AdditiveSlot& s : _additiveSlots) {
        if (s.clip != nullptr) ++cnt;
    }
    return cnt;
}

void AnimationPlayer::setAdditiveLayerWeight(uint32_t slotId, float w)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    // Static weight — only effective when no curve is active. We write
    // it to the curve's `from` slot so when the curve is disarmed
    // (cancelLayerBlendCurve or auto-past-end) sampleLayerBlendCurve
    // returns this value. Magnitude-saturated to [0,1].
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    s->curve.from = w;
    // Slot 0 mirrors P1.3 _blendWeight (Phase 1a Additive + getBlendWeight).
    if (slotId == 0) {
        _blendWeight = w;
    }
    // to is left at its current value so a future blendWeightOverTime
    // call still has a meaningful end value. If a curve is active,
    // the static weight only takes over after the curve disarms.
}

float AnimationPlayer::getAdditiveLayerWeight(uint32_t slotId) const
{
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return 0.0f;
    return s->curve.from;
}

void AnimationPlayer::blendLayerWeightOverTime(uint32_t slotId,
                                               float from, float to,
                                               float duration,
                                               BlendEasing easing)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    if (duration <= 0.0f) {
        // Defensive no-op (same as blendWeightOverTime policy).
        return;
    }
    if (from < 0.0f) from = 0.0f;
    if (from > 1.0f) from = 1.0f;
    if (to   < 0.0f) to   = 0.0f;
    if (to   > 1.0f) to   = 1.0f;

    s->curve.from      = from;
    s->curve.to        = to;
    s->curve.duration  = duration;
    s->curve.easing    = easing;
    // P1.4 contract: curve window is keyed off base `_time` so an
    // independent looping additive playhead cannot wrap the fade clock.
    s->curve.startTime = _time;
    s->curve.active    = true;
}

void AnimationPlayer::cancelLayerBlendCurve(uint32_t slotId)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return;
    s->curve.active = false;
}

bool AnimationPlayer::isLayerBlendCurveActive(uint32_t slotId) const
{
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return false;
    return s->curve.active;
}

void AnimationPlayer::setAdditiveLayerSyncToBase(uint32_t slotId, bool v)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    s->syncToBase = v;
    if (v) {
        // INV-6 per-slot — snap slot.time to base _time immediately.
        s->time = _time;
        s->prevTickTime = s->time;
    }
}

bool AnimationPlayer::isAdditiveLayerSyncToBase(uint32_t slotId) const
{
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return false;
    return s->syncToBase;
}

void AnimationPlayer::setAdditiveLayerPaused(uint32_t slotId, bool v)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    s->paused = v;
    if (!v) {
        // INV-8 per-slot — re-sync the cursor so we don't fire the
        // accumulated notify backlog.
        s->prevTickTime = s->time;
        s->pendingNotifies.clear();
    }
}

bool AnimationPlayer::isAdditiveLayerPaused(uint32_t slotId) const
{
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return false;
    return s->paused;
}

void AnimationPlayer::setAdditiveLayerRefPoseCapture(uint32_t slotId, bool v)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    s->refPoseCapture = v;
    // Do NOT eagerly capture here — `_local*` is still rest / stale until
    // the next evaluate()'s Phase 1a. Leave Fresh so evaluate captures
    // the post-Phase-1a base (INV-7 / design.md §4.10.6).
    if (!v) {
        s->captureState = CaptureState::Fresh;
    }
}

bool AnimationPlayer::isAdditiveLayerRefPoseCapture(uint32_t slotId) const
{
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return false;
    return s->refPoseCapture;
}

void AnimationPlayer::setAdditiveLayerTrackWeights(uint32_t slotId,
                                                   const std::vector<float>& weights)
{
    AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr || s->clip == nullptr) return;
    s->trackWeights = weights;
    // Empty vector = uniform 1.0f (Phase 1.5 default). Non-empty =
    // per-track multiplier; tracks beyond the vector's size revert to
    // uniform (per-track mask is allowed to be sparse).
}

const std::vector<float>&
AnimationPlayer::getAdditiveLayerTrackWeights(uint32_t slotId) const
{
    static const std::vector<float> s_empty;
    const AdditiveSlot* s = getSlot(slotId);
    if (s == nullptr) return s_empty;
    return s->trackWeights;
}

// ===========================================================================
//  P1.4 — Cross-fade single-slot wrappers (backward-compat)
// ===========================================================================

void AnimationPlayer::setAdditiveSyncToBase(bool enabled)
{
    // P1.5 wrapper → per-slot[0] setter. INV-6 snap happens inside.
    setAdditiveLayerSyncToBase(0, enabled);
}

void AnimationPlayer::setAdditiveRefPoseCapture(bool enabled)
{
    setAdditiveLayerRefPoseCapture(0, enabled);
}

void AnimationPlayer::blendWeightOverTime(float from, float to,
                                          float duration,
                                          BlendEasing easing)
{
    blendLayerWeightOverTime(0, from, to, duration, easing);
}

void AnimationPlayer::cancelBlendCurve()
{
    cancelLayerBlendCurve(0);
}

void AnimationPlayer::setAdditivePaused(bool paused)
{
    setAdditiveLayerPaused(0, paused);
}

// ===========================================================================
//  P1.5 / P1.4 — Per-slot internals
// ===========================================================================

// P1.5 — sampleLayerBlendCurve. Generalized form of P1.4 sampleBlendCurve;
// operates on an arbitrary slot rather than the single-slot _curve.
float AnimationPlayer::sampleLayerBlendCurve(const AdditiveSlot& slot) const
{
    if (!slot.curve.active) {
        // Static per-slot weight (setBlendWeight / setAdditiveLayerWeight /
        // post-disarm latch of `to`).
        return slot.curve.from;
    }
    // Curve progress rides the base clock — see blendLayerWeightOverTime.
    const float elapsed = _time - slot.curve.startTime;
    if (slot.curve.duration <= 0.0f) {
        return slot.curve.to;
    }
    const float t01 = std::min(1.0f, std::max(0.0f, elapsed / slot.curve.duration));
    const float eased = applyEasing(t01, slot.curve.easing);
    return slot.curve.from + (slot.curve.to - slot.curve.from) * eased;
}

// P1.5 — capture / apply-captured per-slot. Mirror of P1.4 single-slot
// captureRefPoseFromLocal + applyCapturedRefPoseToLocal but operates on
// a slot's captured buffers instead of the implicit _additiveSlots[0].
void AnimationPlayer::captureRefPoseFromSlot(AdditiveSlot& slot)
{
    slot.capturedLocalPos.assign(_localPos.begin(), _localPos.end());
    slot.capturedLocalRot.assign(_localRot.begin(), _localRot.end());
    slot.capturedLocalScl.assign(_localScl.begin(), _localScl.end());
}

void AnimationPlayer::applyCapturedRefPoseFromSlot(AdditiveSlot& slot)
{
    if (slot.capturedLocalPos.size() != _localPos.size()
        || slot.capturedLocalRot.size() != _localRot.size()
        || slot.capturedLocalScl.size() != _localScl.size()) {
        return;
    }
    std::copy(slot.capturedLocalPos.begin(), slot.capturedLocalPos.end(), _localPos.begin());
    std::copy(slot.capturedLocalRot.begin(), slot.capturedLocalRot.end(), _localRot.begin());
    std::copy(slot.capturedLocalScl.begin(), slot.capturedLocalScl.end(), _localScl.begin());
}

// P1.4 — single-slot backward-compat helpers. Thin wrappers around
// slot[0]. The 312 P1.4 tests still rely on this surface bit-for-bit.
float AnimationPlayer::sampleBlendCurve() const
{
    const AdditiveSlot* s = getSlot(0);
    if (s == nullptr) return _blendWeight;   // P1.3 pre-bind behavior
    return sampleLayerBlendCurve(*s);
}

void AnimationPlayer::captureRefPoseFromLocal()
{
    AdditiveSlot* s = getSlot(0);
    if (s != nullptr) captureRefPoseFromSlot(*s);
}

void AnimationPlayer::applyCapturedRefPoseToLocal()
{
    AdditiveSlot* s = getSlot(0);
    if (s != nullptr) applyCapturedRefPoseFromSlot(*s);
}

float AnimationPlayer::applyEasing(float t01, BlendEasing e)
{
    switch (e) {
        case BlendEasing::Linear:
            return t01;
        case BlendEasing::EaseIn:
            return ayt::math::easeIn(t01, 2.0f);
        case BlendEasing::EaseOut:
            return ayt::math::easeOut(t01, 2.0f);
        case BlendEasing::EaseInOut:
            return ayt::math::easeInOut(t01, 2.0f);
        case BlendEasing::Smoothstep:
            return ayt::math::smoothstep(t01);
    }
    return t01;
}

// ============================================================================
// P2.2 — Resource-level bone mask
// ============================================================================

// Bind a mask. Takes ownership of the shared_ptr (the caller must
// keep it alive for the duration of every evaluate() that reads it).
// The shared_ptr handles its own lifetime — no dangling pointer risk
// because the resource stays alive until the last shared_ptr drops.
// This replaces an earlier `shared_ptr(&mask, no-op deleter)` design
// that UAF'd once the caller's stack-local mask went out of scope.
void AnimationPlayer::setSkeletonMask(std::shared_ptr<const ayt::resource::ISkeletonMask> mask)
{
    _skeletonMask = std::move(mask);
    resolveSkeletonMask();
}

// Drop the mask reference. Resets _boneMaskWeights so the per-frame
// mask gate becomes a single bounds check (idx < empty.size() == false).
void AnimationPlayer::clearSkeletonMask()
{
    _skeletonMask.reset();
    _boneMaskWeights.clear();
    ++_skeletonMaskGeneration;
}

// Eager resolver — populate _boneMaskWeights from _skeletonMask. Two
// passes: named entries set per-bone weight via AssetBoneCache, then
// the wildcard (if any) fills every bone the named pass didn't touch.
// Default for untouched bones is 1.0f (identity). No-op when either
// mask or skeleton is null (just clears the vector + bumps generation).
void AnimationPlayer::resolveSkeletonMask()
{
    if (_skeletonMask == nullptr || _skeleton == nullptr) {
        _boneMaskWeights.clear();
        ++_skeletonMaskGeneration;
        return;
    }

    const size_t n = _skeleton->getBoneCount();
    _boneMaskWeights.assign(n, 1.0f);  // identity default

    auto& cache = ayt::anim::AssetBoneCache::instance();

    // First pass: named entries.
    std::vector<bool> namedHit(n, false);
    const size_t entryCount = _skeletonMask->getAuthoredBoneCount();
    const ayt::resource::SkeletonMaskBone* entries = _skeletonMask->getEntries();
    for (size_t i = 0; i < entryCount; ++i) {
        const ayt::resource::SkeletonMaskBone& e = entries[i];
        // Skip wildcard rows in the named pass.
        if (e.name.empty()) continue;
        const int32_t boneIdx = cache.resolveAndCache(_skeleton.get(), e.name.c_str());
        if (boneIdx >= 0 && static_cast<size_t>(boneIdx) < n) {
            _boneMaskWeights[static_cast<size_t>(boneIdx)] = e.weight;
            namedHit[static_cast<size_t>(boneIdx)] = true;
        }
        // Unresolved name — silently skipped. Same behavior as P1.7
        // track slice with no matching bone.
    }

    // Second pass: wildcard applies to every bone not explicitly named.
    if (_skeletonMask->hasWildcard()) {
        const float w = _skeletonMask->wildcardWeight();
        for (size_t i = 0; i < n; ++i) {
            if (!namedHit[i]) _boneMaskWeights[i] = w;
        }
    }

    ++_skeletonMaskGeneration;
}

} // namespace ayt::anim
