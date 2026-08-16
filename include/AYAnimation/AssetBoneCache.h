#pragma once
// AYAnimation/AssetBoneCache.h — P1.7 (2026-07-27) Shared Skeleton Tick Cache.
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
// Thread-safety (P3 polish, 2026-08-08): touches happen on the
// main-thread AnimationSystem tick path (single-threaded by ECS
// convention; see ay-dev-rules.md). Since P3 polish the DEFAULT mode
// is lock-free (INV-59): no mutex is ever touched, the flag is a
// plain bool read by the 7 access sites. Authoring tools /
// multi-threaded hosts call setThreadSafe(true) BEFORE first
// concurrent use to re-engage the mutex (INV-60: the flag is NOT
// atomic — flip it only while no other thread is inside the cache).

#include <AYResource/assetsDefs/ISkeleton.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ayt::anim
{

namespace detail
{

// P4 polish (2026-08-10) — transparent hash for heterogeneous lookup.
// The inner map is keyed by std::string, but the public API takes
// `const char*` names; with the DEFAULT std::hash<std::string> every
// find(name) constructs a temporary std::string (~900ns in debug builds
// on the bind path — see design §4.21.12). `is_transparent` enables
// C++14 heterogeneous lookup: the key is hashed and compared in place,
// 0 temporary string, 0 allocation. std::equal_to<> matches the
// const char* key against the stored std::string via the C++20
// reverse operator== overloads.
struct StringViewHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }

    size_t operator()(const std::string& s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }

    // Explicit const char* overload: without it, a const char* key is an
    // equally-good user-defined conversion to BOTH string_view and
    // std::string → C3066 ambiguous call at the heterogeneous lookup
    // site (MSVC xhash _Uhash_compare).
    size_t operator()(const char* c) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view(c));
    }
};

} // namespace detail

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

    // P3 polish (2026-08-08) — lock-free single-threaded mode.
    // Default is false (INV-59): the cache NEVER touches the mutex —
    // the ECS main-tick path is single-threaded, so every access is a
    // plain unordered_map read/write. Call setThreadSafe(true) BEFORE
    // the cache is first used from more than one thread (authoring
    // tools / multi-threaded hosts); this re-engages the mutex on all
    // 7 access sites. Behavior is identical in both modes.
    //
    // INV-60 — the flag is a plain bool, NOT atomic. Flipping it is
    // only allowed from a single thread while no other thread is
    // inside the cache (typically: once at startup, before threads
    // spawn). Never flip it mid-flight in a multi-threaded host.
    // P6 polish (2026-08-10) — setThreadSafe debug-asserts the
    // dangerous half of INV-60 when LEAVING thread-safe mode: try_lock()
    // proves no other thread is mid-access (see AssetBoneCache.cpp).
    // Best-effort diagnostics, contract unchanged.
    void setThreadSafe(bool enabled) noexcept;
    bool isThreadSafe() const noexcept;

private:
    AssetBoneCache() = default;
    AssetBoneCache(const AssetBoneCache&) = delete;
    AssetBoneCache& operator=(const AssetBoneCache&) = delete;

    mutable std::mutex _mu;
    bool _threadSafe = false;   // INV-59/60 — plain bool, not atomic
    // P4 polish (2026-08-10) — StringViewHash + std::equal_to<> enable
    // heterogeneous lookup: find(name) with const char* / string_view
    // keys does NOT construct a temporary std::string (INV-63).
    std::unordered_map<const ayt::resource::ISkeleton*,
                       std::unordered_map<std::string, int32_t,
                                          detail::StringViewHash,
                                          std::equal_to<>>> _map;
};

} // namespace ayt::anim