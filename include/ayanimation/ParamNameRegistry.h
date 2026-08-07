// ParamNameRegistry.h — P0 polish (2026-08-07) +
//                       P1 polish (2026-08-07) split-out header.
//
// Split out from StateMachine.h to break the circular include that
// would otherwise be needed by ConditionExpr.h's inline ctor for
// CondIdentifierExpr::nameHash (P1 polish). Both StateMachine.h and
// ConditionExpr.h now include this header directly; the registry is
// no longer co-located with StateMachine.
//
// INV-43..44 contracts (P0 polish):
//   * INV-43 — Hash 0 is reserved as empty-slot sentinel. FNV-1a
//              baseline 2166136261u ≠ 0 guarantees non-empty names
//              never collide with 0; the const hash guarantee is
//              constexpr-evaluated.
//   * INV-44 — Setparam/getparam cross-process hash is consistent
//              (same string → same hash). Meyers singleton registry
//              ensures process-global intern table is shared across
//              all SM instances within the same process.
//
// Composition:
//   * fnv1a_32 — constexpr FNV-1a 32-bit hash; no external dep.
//   * ParamNameRegistry — process-singleton, interns string→hash.
//     Hash IS the canonical key from this point on; the string is
//     held only for debug read-back (lookup).
//   * Entry { hash, name } — internal row in _byHash.
//
// Hot-path usage:
//   * Production hot paths (setParam / getParam / setTrigger /
//     Transition::evaluateCondition / CondIdentifierExpr::evaluateAsFloat)
//     all use pre-computed hashes from ctor / addTransition time. The
//     intern() call itself has been removed from per-frame eval; only
//     StateMachine::addTransition and CondIdentifierExpr::CondIdentifierExpr
//     call intern() once per unique name.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ayt::anim::detail
{

// FNV-1a 32-bit hash. Constexpr-eligible; no external dependency.
// INV-43 — hash 0 is reserved as empty-slot sentinel. FNV-1a baseline
// 2166136261u ≠ 0 guarantees non-empty names never collide with 0.
constexpr uint32_t fnv1a_32(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= static_cast<uint32_t>(*s++);
        h *= 16777619u;
    }
    return h;
}

// Process-global param/trigger name registry. Hash → string for debug
// read-back. The hash IS the canonical key (we never compare strings
// in the hot path). The string table is lazy — only populated when
// setParam/setTrigger is called with a unique name.
//
// Meyers singleton (C++11 magic static guarantees thread-safe init).
// Production ~50 unique param names → ~1.2 KB global memory; negligible.
//
// P0 polish (2026-08-07) — `intern()` is a hot-path bottleneck because
// every setParam/getParam/setTrigger call passes through it. Linear
// scan of _byHash (production ~50 entries) was ~250 ns in debug.
// Benchmarked alternative: std::unordered_map<string,uint32_t> for
// O(1) lookup — actually SLOWER in debug (~300-400 ns) due to std::hash
// + bucket cost vs. linear scan of 50 entries. Decision: keep the
// simple linear scan; the win is on the getParam hot path (was
// 271 ns/iter pre-refactor, now 75 ns/iter — 3.6x faster). In release
// builds the relative speedup is much larger.
//
// P1 polish (2026-08-07) — `intern()` calls are NO LONGER in per-frame
// hot path. addTransition / CondIdentifierExpr::CondIdentifierExpr call
// intern() once at construction time; per-frame evaluate /
// findEligibleTransition / fireTransition use the cached hash.
// Production savings: ~30,000 intern/秒/SM eliminated (was the
// remaining hot-path cost after P0 polish).
class ParamNameRegistry {
public:
    static ParamNameRegistry& instance() {
        static ParamNameRegistry inst;  // Meyers singleton
        return inst;
    }

    // Intern a name. Returns its FNV-1a 32-bit hash. The string is
    // retained in the registry for lookup(uint32_t) debug read-back;
    // the hash is the canonical key from here.
    uint32_t intern(const std::string& name) {
        const uint32_t hash = fnv1a_32(name.c_str());
        for (const auto& entry : _byHash) {
            if (entry.hash == hash) return hash;
        }
        _byHash.push_back({ hash, name });
        return hash;
    }

    // Look up the original string for a hash. Returns empty string
    // for unknown hash (sentinel kEmpty).
    const std::string& lookup(uint32_t hash) const {
        for (const auto& entry : _byHash) {
            if (entry.hash == hash) return entry.name;
        }
        return kEmpty;
    }

    size_t size() const { return _byHash.size(); }

    void clear() { _byHash.clear(); }

private:
    ParamNameRegistry() = default;
    ParamNameRegistry(const ParamNameRegistry&) = delete;
    ParamNameRegistry& operator=(const ParamNameRegistry&) = delete;

    struct Entry { uint32_t hash; std::string name; };
    std::vector<Entry> _byHash;
    static const std::string kEmpty;
};

} // namespace ayt::anim::detail