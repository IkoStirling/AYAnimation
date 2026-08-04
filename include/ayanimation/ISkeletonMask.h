// ISkeletonMask.h — P2.2 (2026-08-03) Skeleton Mask resource interface.
//
// Resource-level bone mask, orthogonal to P1.5 per-slot per-track
// `trackWeights` (which gates additive slot deltas keyed by clip track
// index). ISkeletonMask gates per-bone TRS writes keyed by SKELETON
// bone name, so the host can author a mask against the skeleton
// (e.g. "upper-body only") and have it apply across every clip bound
// to that skeleton without per-clip track-index knowledge.
//
// Layering note (P2.2 §4.2.1 defer): the formal `IAYSkeletonMask` will
// move to AYResource/interface/assetsDefs/ when the .aymask loader
// ships (deferred follow-up PR). For this ship the interface lives in
// AYAnimation/include/ayanimation/ to keep AYResource untouched; the
// bridge calls ResourceManager::load<ayt::anim::ISkeletonMask>() and
// on a nullptr return falls back to "no mask" via fail-soft in
// AnimationSystem. When the loader lands, the include path flips.
//
// Inheritance note: derives from ayt::resource::IResource so
// ResourceManager::load<T>() can dynamic_pointer_cast correctly.
// Concrete impls (SkeletonMask in src/) must still provide load /
// unload / sizeInBytes / getPath / getType — IResource declares
// these as concrete (non-pure) members with default implementations,
// but SkeletonMask must implement load / unload / sizeInBytes (and
// override getType) to be non-abstract.
//
// Authoring:
//   addEntry("Spine", 0.5f)        — bone "Spine" gets weight 0.5
//   addEntry("",      0.25f)       — wildcard: all UNNAMED bones get 0.25
//   Duplicate names overwrite (last wins).
//   Weights clamped to [0, 1] by SkeletonMask::addEntry.
//
// Resolution (in AnimationPlayer::resolveSkeletonMask):
//   named pass   — lookup each boneName via AssetBoneCache -> boneIdx
//                  set _boneMaskWeights[idx] = weight
//   wildcard pass — for every i where no named entry hit, set
//                  _boneMaskWeights[i] = wildcard weight (if any)
//   Default = 1.0f (identity) for bones not touched by any entry.

#pragma once

#include "IAYResource.h"

#include <cstddef>
#include <cstdint>

namespace ayt { namespace anim {

// Per-bone mask record. POD; fixed-size name buffer avoids heap
// allocations at resolve time. `resolvedIndex` is populated by the
// resolver (AssetBoneCache), -1 means "not yet resolved" or "name
// not found in skeleton". `isWildcard` is set by SkeletonMask::addEntry
// when the input boneName is empty; the resolver skips wildcard rows
// in the named pass.
struct SkeletonMaskBone {
    char         name[64];        // bone name; empty string = wildcard
    float        weight;          // [0, 1], pre-clamped by SkeletonMask::addEntry
    std::int32_t resolvedIndex;   // -1 = unresolved; >= 0 = bone index
    bool         isWildcard;      // true => apply to ALL bones not named elsewhere
};

class ISkeletonMask : public ayt::resource::IResource {
public:
    virtual ~ISkeletonMask() = default;

    // Total entries the resolver sees: named entries + 1 if hasWildcard.
    virtual std::size_t getEntryCount() const = 0;

    // Pointer to the contiguous named-entry array. Wildcard is NOT in
    // this array; use hasWildcard() / wildcardWeight() to read it.
    virtual const SkeletonMaskBone* getEntries() const = 0;

    // Number of named entries (excluding any wildcard).
    virtual std::size_t getAuthoredBoneCount() const = 0;

    virtual bool  hasWildcard()    const = 0;
    virtual float wildcardWeight() const = 0;

    // Human-readable name for logs / debug overlays. Not used by the
    // resolver.
    virtual const char* getDebugName() const = 0;

    // Type tag / version — mirrors IAYSkeleton / IAYAnimation.
    static constexpr char         TYPE_TAG[16] = "SkeletonMask";
    static constexpr std::uint32_t VERSION     = 1;
};

}} // namespace ayt::anim