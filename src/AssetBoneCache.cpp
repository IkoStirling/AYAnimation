// AssetBoneCache.cpp — P1.7 implementation. See AssetBoneCache.h for
// the contract; this file is the single definition site for the
// singleton and its three public methods.

#include <ayanimation/AssetBoneCache.h>

#include <cstring>

namespace ayt::anim
{

AssetBoneCache& AssetBoneCache::instance()
{
    // Magic-static: thread-safe init in C++11, destroyed at program
    // exit. No destructor ordering risk with std library (we don't
    // allocate from other static singletons during cache teardown).
    static AssetBoneCache s_inst;
    return s_inst;
}

int32_t AssetBoneCache::lookup(const ayt::resource::ISkeleton* skel,
                               const char* name) const
{
    if (skel == nullptr || name == nullptr) return kCacheKeyAbsent;
    std::lock_guard<std::mutex> lk(_mu);
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
    // Fast path: re-lookup under lock so concurrent resolves don't
    // race-fill. The common case is single-threaded main-tick, so
    // the second lookup is hot in L1.
    {
        std::lock_guard<std::mutex> lk(_mu);
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
    std::lock_guard<std::mutex> lk(_mu);
    _map[skel][name] = cached;
    return cached;
}

void AssetBoneCache::invalidate(const ayt::resource::ISkeleton* skel)
{
    if (skel == nullptr) return;
    std::lock_guard<std::mutex> lk(_mu);
    _map.erase(skel);
}

void AssetBoneCache::clear()
{
    std::lock_guard<std::mutex> lk(_mu);
    _map.clear();
}

size_t AssetBoneCache::skeletonEntryCount() const
{
    std::lock_guard<std::mutex> lk(_mu);
    return _map.size();
}

size_t AssetBoneCache::boneNameEntryCount(
    const ayt::resource::ISkeleton* skel) const
{
    if (skel == nullptr) return 0;
    std::lock_guard<std::mutex> lk(_mu);
    auto skIt = _map.find(skel);
    if (skIt == _map.end()) return 0;
    return skIt->second.size();
}

} // namespace ayt::anim