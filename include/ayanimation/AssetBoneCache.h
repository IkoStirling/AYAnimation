#pragma once
// AssetBoneCache.h — P1.7 (2026-07-27) Shared Skeleton Tick Cache.
//
// Asset-level (ISkeleton* address → boneName → boneIdx) cache that
// sits across every AnimationPlayer. Two players bound to the SAME
// ISkeleton* share the (skeleton, name) → boneIdx lookup result;
// the FIRST resolveBoneIdxOnce call writes the entry, every
// subsequent call hits the unordered_map and skips the vtable
// dispatch into ISkeleton::findBone.
//
// Per-player state (TrackSlice.boneIdx in P1.4) is unchanged — this
// cache is additive. resolveBoneIdxOnce still lazy-resolves per-track
// once, but on miss it consults AssetBoneCache before calling
// ISkeleton::findBone, and on AssetBoneCache hit it skips the vtable.
//
// Lifetime contract: keys are RAW `const ISkeleton*` addresses (not
// shared_ptr) because the cache is a side-table — the source of truth
// for skeleton lifecycle is the holders (SkeletonComponent or the
// test-side Skeleton). If a skeleton is unloaded while its address
// is still cached, the entry becomes stale and lookup() returns
// `kNotInCache` (INT32_MIN); the caller treats this the same as a
// cold start. invalidate() is the explicit "drop this entry" path
// (called from setSkeleton() on the new-pointer side, so the OLD
// skeleton's entries survive for any other player still bound).
//
// Thread-safety: touches happen on the main-thread AnimationSystem
// tick path (single-threaded by ECS convention; see ay-dev-rules.md);
// the mutex exists to keep the API future-proof for authoring tools
// and to satisfy thread-sanitizer.

#include <assetsDefs/IAYSkeleton.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ayt::anim
{

class AssetBoneCache {
public:
    // Sentinel returned by lookup() when the (skeleton, name) pair has
    // no cached entry yet. Resolved bone indices are >= 0; cached
    // misses return -1. Use `kCachedMiss` / `kCacheKeyAbsent` to
    // disambiguate "we know, name not found" vs "we don't know yet".
    static constexpr int32_t kCacheKeyAbsent = INT32_MIN;   // = kBoneUnresolved
    static constexpr int32_t kCachedMiss     = -1;

    static AssetBoneCache& instance();

    // Returns kCacheKeyAbsent on cold cache, kCachedMiss on a cached
    // "name not in skeleton", or the bone index >= 0 on hit. Does NOT
    // mutate the cache. Caller uses this to decide whether to call
    // resolveAndCache.
    int32_t lookup(const ayt::resource::ISkeleton* skel,
                   const char* name) const;

    // Resolve name against `skel->findBone(name)`, store into cache,
    // return the resolved index (>= 0) or kCachedMiss (-1) if not
    // found. Safe to call when skel == nullptr (returns kCachedMiss).
    // Inserts a new entry into the per-skeleton map on first call.
    int32_t resolveAndCache(const ayt::resource::ISkeleton* skel,
                            const char* name);

    // Drop the cache entry for one skeleton pointer. Other players
    // still bound to that skeleton can keep their cached boneIdx
    // (their TrackSlice state is independent); only the asset-level
    // shared cache is purged. Calling invalidate(nullptr) is a no-op.
    void invalidate(const ayt::resource::ISkeleton* skel);

    // Drop every entry. Test / diagnostic only.
    void clear();

    // Test / diagnostic only.
    size_t skeletonEntryCount() const;
    size_t boneNameEntryCount(const ayt::resource::ISkeleton* skel) const;

private:
    AssetBoneCache() = default;
    AssetBoneCache(const AssetBoneCache&) = delete;
    AssetBoneCache& operator=(const AssetBoneCache&) = delete;

    mutable std::mutex _mu;
    std::unordered_map<const ayt::resource::ISkeleton*,
                       std::unordered_map<std::string, int32_t>> _map;
};

} // namespace ayt::anim