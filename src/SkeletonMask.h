// SkeletonMask.h — P2.2 (2026-08-03) in-memory test fixture.
//
// Concrete ISkeletonMask implementation used by:
//   (a) the AYAnimation unit tests (AYTest_SkeletonMask.cpp)
//   (b) the AYEntity bridge tests (AYTest_SkeletonMaskBridge.cpp)
//
// Both test suites use this fixture rather than going through
// ResourceManager::load<ISkeletonMask>() because the .aymask loader
// is deferred per §4.2.1 (cross-module PR to AYResource). The
// bridge's load-failure path is tested separately with a real
// nonexistent path string to exercise the canonical ResourceManager
// nullptr return.
//
// Lifetime: header-only impl, std::shared_ptr via create() matches
// the P1.7 Skeleton makeFourBoneSkeletonShared style.
//
// IResource member overrides: load / unload / sizeInBytes are stubs
// (this fixture is in-memory only — no .aymask file backing). getPath
// / getType / isLoaded inherit IResource defaults (return _path /
// _type / _loaded which we initialize in the ctor). addTag / removeTag
// / hasTag inherit; tests don't use them.

#pragma once

#include <ayanimation/ISkeletonMask.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ayt { namespace anim {

class SkeletonMask final : public ISkeletonMask {
public:
    SkeletonMask() {
        // IResource base members — set type so ResourceManager-side
        // diagnostics / future loader can read it back. The path
        // remains empty for in-memory fixtures; the AYEntity bridge
        // fail-soft path is what surfaces "no path -> no mask".
        _type = TYPE_TAG;
        _loaded = true;     // in-memory fixture is always "loaded"
    }

    static std::shared_ptr<SkeletonMask> create() {
        return std::shared_ptr<SkeletonMask>(new SkeletonMask());
    }

    // === Authoring ===

    // Add (boneName, weight). Weights clamped to [0, 1]. Empty
    // boneName marks a WILDCARD entry whose weight applies to every
    // bone not named by another entry (named wins over wildcard).
    // Duplicate names overwrite the prior entry (last wins).
    void addEntry(const char* boneName, float weight) {
        if (boneName == nullptr) return;
        const float w = (weight < 0.0f) ? 0.0f
                     : (weight > 1.0f) ? 1.0f
                     :                    weight;
        const std::size_t nameLen = std::strlen(boneName);
        if (nameLen == 0) {
            _hasWildcard = true;
            _wildcardWeight = w;
            return;
        }
        for (auto& e : _entries) {
            if (std::strcmp(e.name, boneName) == 0) {
                e.weight = w;
                return;
            }
        }
        SkeletonMaskBone b{};
        const std::size_t copyLen = (nameLen >= sizeof(b.name))
                                    ? sizeof(b.name) - 1
                                    : nameLen;
        std::memcpy(b.name, boneName, copyLen);
        b.name[copyLen] = '\0';
        b.weight = w;
        b.resolvedIndex = -1;
        b.isWildcard = false;
        _entries.push_back(b);
    }

    void setDebugName(const char* n) {
        _debugName = (n != nullptr) ? n : "";
    }

    // === ISkeletonMask overrides ===

    std::size_t getEntryCount() const override {
        return _entries.size() + (_hasWildcard ? 1u : 0u);
    }

    const SkeletonMaskBone* getEntries() const override {
        return _entries.data();
    }

    std::size_t getAuthoredBoneCount() const override {
        return _entries.size();
    }

    bool  hasWildcard()    const override { return _hasWildcard; }
    float wildcardWeight() const override { return _wildcardWeight; }

    const char* getDebugName() const override {
        return _debugName.c_str();
    }

    // === IResource overrides ===

    // In-memory fixture: load is a no-op that records the path.
    bool load(const std::string& path) override {
        _path = path;
        _loaded = true;
        return true;
    }

    // In-memory fixture: unload clears the entries but keeps the
    // type string so re-load via load(path) restores nothing — the
    // test fixture is single-use. The production .aymask loader
    // (deferred) will override this with a proper unload.
    bool unload() override {
        _entries.clear();
        _hasWildcard = false;
        _wildcardWeight = 0.0f;
        _loaded = false;
        return true;
    }

    // In-memory fixture: report a tiny in-memory footprint.
    std::size_t sizeInBytes() const override {
        std::size_t s = sizeof(SkeletonMask);
        s += _entries.capacity() * sizeof(SkeletonMaskBone);
        s += _debugName.capacity();
        return s;
    }

private:
    std::vector<SkeletonMaskBone> _entries;
    bool         _hasWildcard    = false;
    float        _wildcardWeight = 0.0f;
    std::string  _debugName      = "SkeletonMask";
};

}} // namespace ayt::anim