// AssetBoneCache.cpp — P1.7 implementation + P3 polish (2026-08-08)
// lock-free single-threaded mode. See AYAnimation/AssetBoneCache.h for the
// contract; this file is the single definition site for the
// singleton, its three public methods, and the P3 polish mode flag.

#include <AYAnimation/AssetBoneCache.h>

#include <cassert>
#include <cstring>
#include <mutex>

namespace ayt::anim
{

namespace {

// P3 polish — RAII conditional lock. Returns an UNLOCKED unique_lock
// when `enabled` is false, so the single-threaded path (INV-59 default)
// has zero lock overhead: the unique_lock default ctor is noexcept and
// costs nothing, and the enclosing block still releases it properly on
// the thread-safe path. `_threadSafe` is read here by all 7 sites.
std::unique_lock<std::mutex> maybeLock(std::mutex& mu, bool enabled)
{
    if (enabled) return std::unique_lock<std::mutex>(mu);
    return std::unique_lock<std::mutex>();
}

} // namespace

AssetBoneCache& AssetBoneCache::instance()
{
    // Magic-static: thread-safe init in C++11, destroyed at program
    // exit. No destructor ordering risk with std library (we don't
    // allocate from other static singletons during cache teardown).
    static AssetBoneCache s_inst;
    return s_inst;
}

void AssetBoneCache::setThreadSafe(bool enabled) noexcept
{
    // P6 polish (2026-08-10) — INV-60 debug assert (design §4.21.12 Q2).
    // The flip contract: only flip while no other thread is inside the
    // cache. Leaving thread-safe mode is PROVABLE — try_lock() fails
    // iff some other thread holds the mutex mid-access, and being
    // non-blocking it never deadlocks on an in-flight access. Entering
    // from lock-free mode is NOT provable (lock-free sites never touch
    // the mutex), so that direction stays a startup convention (INV-60);
    // the assert therefore guards exactly the dangerous flip (true→false).
    //
    // NDEBUG note: in release builds assert() compiles out and the flip
    // proceeds as before — the probe is best-effort diagnostics, not a
    // correctness mechanism (the contract itself is unchanged).
    if (_threadSafe) {
        const bool uncontended = _mu.try_lock();
        if (uncontended) _mu.unlock();
        assert(uncontended &&
               "AssetBoneCache::setThreadSafe: another thread is inside "
               "the cache — INV-60 flip contract violated");
    }
    _threadSafe = enabled;
}

bool AssetBoneCache::isThreadSafe() const noexcept
{
    return _threadSafe;
}

int32_t AssetBoneCache::lookup(const ayt::resource::ISkeleton* skel,
                               const char* name) const
{
    if (skel == nullptr || name == nullptr) return kCacheKeyAbsent;
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    auto skIt = _map.find(skel);
    if (skIt == _map.end()) return kCacheKeyAbsent;
    auto nameIt = skIt->second.find(name);
    if (nameIt == skIt->second.end()) return kCacheKeyAbsent;
    return nameIt->second;
}

int32_t AssetBoneCache::resolveAndCache(const ayt::resource::ISkeleton* skel,
                                        const char* name)
{
    if (skel == nullptr || name == nullptr) return kCachedMiss;
    // Fast path: re-lookup so concurrent resolves don't race-fill.
    // The common case is single-threaded main-tick (lock-free, INV-59);
    // under the mutex the second lookup is hot in L1.
    {
        std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
        auto skIt = _map.find(skel);
        if (skIt != _map.end()) {
            auto nameIt = skIt->second.find(name);
            if (nameIt != skIt->second.end()) {
                return nameIt->second;
            }
        }
    }
    // Slow path: ask the skeleton. The lookup may legitimately race
    // with another resolveAndCache call for the SAME (skel, name);
    // both will compute the same answer, and whichever writes last
    // wins — the value is idempotent.
    const int found = skel->findBone(name);
    const int32_t cached = (found >= 0) ? static_cast<int32_t>(found)
                                        : kCachedMiss;
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    _map[skel][name] = cached;
    return cached;
}

void AssetBoneCache::invalidate(const ayt::resource::ISkeleton* skel)
{
    if (skel == nullptr) return;
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    _map.erase(skel);
}

void AssetBoneCache::clear()
{
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    _map.clear();
}

size_t AssetBoneCache::skeletonEntryCount() const
{
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    return _map.size();
}

size_t AssetBoneCache::boneNameEntryCount(
    const ayt::resource::ISkeleton* skel) const
{
    if (skel == nullptr) return 0;
    std::unique_lock<std::mutex> lk = maybeLock(_mu, _threadSafe);
    auto skIt = _map.find(skel);
    if (skIt == _map.end()) return 0;
    return skIt->second.size();
}

} // namespace ayt::anim
