// BlendSpace.cpp — P2.1 (2026-07-27). Linear-simplex Blend Tree impl.
//
// See AYAnimation/BlendSpace.h for the architecture rationale (owned-player mode +
// library-mode helper, 2D simplex algorithm).
//
// Implementation strategy:
//   - Both classes own `unique_ptr<AnimationPlayer>` per sample point.
//     Each player ticks independently; we read `getBoneWorldMatrices()`
//     after evaluate() and decompose back to per-bone TRS.
//   - Internal _localPos/_localRot/_localScl buffers on the BlendSpace
//     hold the COMPOSITE per-bone TRS output. evaluate() writes these
//     buffers; the caller (BlendSpaceSystem or render code) uses them.
//   - Library-mode `computeWeightedBoneTRS` is a pure helper — callers
//     hand it pre-evaluated per-clip TRS buffers and get back the
//     composite. Output is bit-identical to owned-player mode for
//     identical inputs (verified by test #11).

#include <AYAnimation/BlendSpace.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimation/KeySampler.h>

#include <AYMath/MathTypes.h>

#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/assetsDefs/ISkeleton.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ayt::anim
{

namespace
{

// Smallest meaningful distance between two sample parameters. Below
// this the simplex math goes singular (weight is 1/eps → huge). 1e-6f
// matches AnimationPlayer's INV-2 epsilon convention.
constexpr float kParamEps = 1.0e-6f;

// ---------------------------------------------------------------------
// Tangent-space quaternion blend for one bone.
//
// Algorithm mirrors AnimationPlayer.cpp:1031-1048 (per-track Additive)
// generalized to N weighted samples:
//   bindRot  = skel.getLocalRotations()[k]
//   for each active i:
//       if w[i] <= 0      → skip (early-return guard against NaN)
//       delta[i] = srcRot[i][k] * bindRot.inverse()
//       accum  *= delta[i].pow(w[i])
//   resultRot[k] = (accum * bindRot).normalize()
//
// Single-active-vertex case (Σw == 1, exactly one weight > 0) reduces
// to resultRot == srcRot[activeIdx] — byte-identical to a regular
// AnimationPlayer binding that single clip.
//
// NOTE: this is NOT slerp between two quaternions. It is the
// Unity / UE tangent-space blend — chosen so N-way blending falls out
// for free. test #4 explicitly asserts this formulation and labels the
// difference from slerp in its name.
// ---------------------------------------------------------------------
ayt::math::FQuaternion blendQuatNWay(
    const ayt::math::FQuaternion& bindRot,
    const std::vector<ayt::math::FQuaternion>& srcRots,
    const std::vector<float>& weights)
{
    // Find the single-active fast path.
    size_t activeCount = 0;
    size_t activeIdx = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0.0f) {
            ++activeCount;
            activeIdx = i;
        }
    }
    if (activeCount == 1) {
        // Single-vertex fast path — byte-identical to binding that
        // clip alone. No decompose + recompose round-trip; preserve
        // numerical precision.
        return srcRots[activeIdx];
    }
    if (activeCount == 0) {
        // No active sample contributes — degenerate. Return bind pose
        // (caller seeded from bind already, but be defensive).
        return bindRot;
    }

    // Multi-vertex: tangent-space accumulator.
    ayt::math::FQuaternion accum = ayt::math::FQuaternion::identity();
    for (size_t i = 0; i < weights.size(); ++i) {
        const float w = weights[i];
        if (w <= 0.0f) continue;
        const ayt::math::FQuaternion delta = srcRots[i] * bindRot.inverse();
        // pow(w) with w==0 → identity (delta.inverse() when w=-1) — but
        // we already skipped w<=0 above. For w==1 → identity too.
        accum = accum * delta.pow(w);
    }
    return (accum * bindRot).normalize();
}

// ---------------------------------------------------------------------
// Convert AnimationPlayer::getBoneWorldMatrices()[k] into world-local
// TRS via Float4x4::decompose. Returns the WORLD-LOCAL transform of
// bone k (i.e. TRS(k) relative to the skeleton root, not relative to
// parent[k]).
//
// BlendSpace's "composite pose" is conceptually per-bone TRS in
// PARENT-LOCAL space (the same space AnimationPlayer populates its
// _localPos/_localRot/_localScl in). To get parent-local from world-
// local we multiply by `inverse(world-of-parent[k])`. This routine
// also returns parent world matrices so the caller can compute the
// inverse.
// ---------------------------------------------------------------------
void decomposeWorldMatrix(
    const ayt::math::Float4x4& worldMat,
    ayt::math::FVector3&       outPos,
    ayt::math::FQuaternion&    outRot,
    ayt::math::FVector3&       outScale)
{
    // decompose returns true on success; false on singular scale → fallback to identity rot.
    const bool ok = worldMat.decompose(outPos, outRot, outScale);
    if (!ok) {
        // Singular scale — preserve the translation, fall back to identity rotation.
        outRot = ayt::math::FQuaternion::identity();
    }
}

// Read bone k's world-local TRS from an AnimationPlayer after
// tick+evaluate. Caller passes the skeleton's bones array so we can
// build the parent-cumulative chain implicitly via getBoneWorldMatrices.
void readPlayerBoneTRS(
    const AnimationPlayer&                          player,
    size_t                                          boneIndex,
    const ayt::resource::Bone*                      bones,
    ayt::math::FVector3&                            outWorldPos,
    ayt::math::FQuaternion&                         outWorldRot,
    ayt::math::FVector3&                            outWorldScale)
{
    const ayt::math::Float4x4* worlds = player.getBoneWorldMatrices();
    assert(worlds != nullptr);
    assert(boneIndex < player.getBoneCount());
    decomposeWorldMatrix(worlds[boneIndex], outWorldPos, outWorldRot, outWorldScale);
    (void)bones;  // bones[] read in higher-level code; this helper only uses world.
}

// Convert world-local TRS of bone k to parent-local TRS given
// parent[k]'s world-local TRS.
void worldLocalToParentLocal(
    const ayt::math::FVector3&    worldPos,
    const ayt::math::FQuaternion& worldRot,
    const ayt::math::FVector3&    worldScale,
    const ayt::math::FVector3&    parentWorldPos,
    const ayt::math::FQuaternion& parentWorldRot,
    const ayt::math::FVector3&    parentWorldScale,
    ayt::math::FVector3&          outParentLocalPos,
    ayt::math::FQuaternion&       outParentLocalRot,
    ayt::math::FVector3&          outParentLocalScale)
{
    // Parent-local position = (worldPos - parentWorldPos) rotated by
    // parentWorldRot.inverse(), with parent scale factored out.
    // Simplified for the affine-uniform-scale-typical case: position
    // = inverse(parentWorldTRS) * worldPos (as point).
    const ayt::math::Float4x4 parentWorld = ayt::math::Float4x4::fromTRS(
        parentWorldPos, parentWorldRot, parentWorldScale);
    const ayt::math::Float4x4 invParent = parentWorld.inverse();
    const ayt::math::Float4x4 worldM = ayt::math::Float4x4::fromTRS(
        worldPos, worldRot, worldScale);
    const ayt::math::Float4x4 localM = invParent * worldM;
    const bool ok = localM.decompose(outParentLocalPos, outParentLocalRot, outParentLocalScale);
    if (!ok) {
        outParentLocalRot = ayt::math::FQuaternion::identity();
    }
}

} // namespace

// ===========================================================================
// BlendSpace1D — implementation
// ===========================================================================

void BlendSpace1D::addSamplePoint(
    float parameter,
    std::shared_ptr<const ayt::resource::IAnimation> clip)
{
    SamplePoint sp;
    sp.parameter = parameter;
    sp.clip      = std::move(clip);
    _samples.push_back(std::move(sp));
    // ensurePlayer() runs lazily on first tick+evaluate.
}

void BlendSpace1D::removeAllSamplePoints()
{
    _samples.clear();
    _players.clear();
    _activeIndices.clear();
    _activeWeights.clear();
}

void BlendSpace1D::setSkeleton(
    std::shared_ptr<const ayt::resource::ISkeleton> skel)
{
    _skeleton = std::move(skel);
    if (_skeleton) {
        resizeTRS(_skeleton->getBoneCount());
        // Re-bind existing players to the new skeleton. Lazy: an
        // empty _skeleton means each player binds separately.
        for (auto& p : _players) {
            if (p) p->setSkeleton(_skeleton);
        }
    } else {
        resizeTRS(0);
    }
}

void BlendSpace1D::setParameter(float p)
{
    _parameter = p;
    rebuildWeights();
}

void BlendSpace1D::ensurePlayer(size_t sampleIndex)
{
    assert(sampleIndex < _samples.size());
    if (sampleIndex < _players.size() && _players[sampleIndex]) return;

    // Grow the _players vector to fit (fill gaps with empty unique_ptr).
    if (sampleIndex >= _players.size()) {
        _players.resize(_samples.size());
    }
    auto player = std::make_unique<AnimationPlayer>();
    if (_skeleton) {
        player->setSkeleton(_skeleton);
    }
    if (_samples[sampleIndex].clip) {
        player->play(_samples[sampleIndex].clip.get());
    }
    _players[sampleIndex] = std::move(player);
}

void BlendSpace1D::tick(float dt)
{
    for (size_t i = 0; i < _samples.size(); ++i) {
        ensurePlayer(i);
        if (_players[i]) {
            _players[i]->setLoop(_loop);
            _players[i]->setPlayRate(_playRate);
            _players[i]->tick(dt);
        }
    }
}

void BlendSpace1D::resizeTRS(size_t n)
{
    _skelBoneCount = n;
}

void BlendSpace1D::evaluate(
    std::vector<float>& outLocalPos,
    std::vector<float>& outLocalRot,
    std::vector<float>& outLocalScl)
{
    const size_t n = _skelBoneCount;
    // Output contract: caller provided arrays sized n*stride; we
    // overwrite every element. If arrays are smaller than n we resize
    // — defensive; tests sometimes forget.
    outLocalPos.resize(n * 3, 0.0f);
    outLocalRot.resize(n * 4, 0.0f);
    outLocalScl.resize(n * 3, 1.0f);

    if (n == 0 || _samples.empty()) return;
    if (_activeIndices.empty()) {
        // No active sample (degenerate — all weights 0). Fall back
        // to rest pose.
        if (_skeleton) {
            const ayt::math::FVector3*    rP = _skeleton->getLocalPositions();
            const ayt::math::FQuaternion* rR = _skeleton->getLocalRotations();
            const ayt::math::FVector3*    rS = _skeleton->getLocalScales();
            for (size_t k = 0; k < n; ++k) {
                if (rP) {
                    outLocalPos[k * 3 + 0] = rP[k].x;
                    outLocalPos[k * 3 + 1] = rP[k].y;
                    outLocalPos[k * 3 + 2] = rP[k].z;
                }
                if (rR) {
                    outLocalRot[k * 4 + 0] = rR[k].x;
                    outLocalRot[k * 4 + 1] = rR[k].y;
                    outLocalRot[k * 4 + 2] = rR[k].z;
                    outLocalRot[k * 4 + 3] = rR[k].w;
                }
                if (rS) {
                    outLocalScl[k * 3 + 0] = rS[k].x;
                    outLocalScl[k * 3 + 1] = rS[k].y;
                    outLocalScl[k * 3 + 2] = rS[k].z;
                }
            }
        }
        return;
    }

    // Ensure every active-index player has evaluated at least once.
    for (size_t idx : _activeIndices) {
        ensurePlayer(idx);
        if (_players[idx] && _players[idx]->isValid()) {
            _players[idx]->evaluate();
        }
    }

    // Per-bone composite TRS loop.
    const ayt::resource::Bone* bones = _skeleton ? _skeleton->getBones() : nullptr;
    const ayt::math::FVector3*    rP = _skeleton ? _skeleton->getLocalPositions()    : nullptr;
    const ayt::math::FQuaternion* rR = _skeleton ? _skeleton->getLocalRotations()    : nullptr;
    const ayt::math::FVector3*    rS = _skeleton ? _skeleton->getLocalScales()       : nullptr;

    for (size_t k = 0; k < n; ++k) {
        // 1. Read parent world TRS from the FIRST active player
        //    (all players share the same skeleton bone hierarchy; any
        //    active player works as the world-chain reference). If a
        //    bone has parent index p, we need parentWorld[k] from
        //    the active player's getBoneWorldMatrices()[p].
        std::vector<ayt::math::FVector3>    srcWorldPos(_activeIndices.size());
        std::vector<ayt::math::FQuaternion> srcWorldRot(_activeIndices.size());
        std::vector<ayt::math::FVector3>    srcWorldScl(_activeIndices.size());
        for (size_t j = 0; j < _activeIndices.size(); ++j) {
            const size_t idx = _activeIndices[j];
            if (_players[idx] && _players[idx]->isValid()) {
                readPlayerBoneTRS(*_players[idx], k, bones,
                                  srcWorldPos[j], srcWorldRot[j], srcWorldScl[j]);
            } else {
                // Missing/invalid sample → bind-pose fallback for that
                // bone in that sample. Matches Phase 1a semantics.
                srcWorldPos[j] = rP ? rP[k] : ayt::math::FVector3(0, 0, 0);
                srcWorldRot[j] = rR ? rR[k] : ayt::math::FQuaternion::identity();
                srcWorldScl[j] = rS ? rS[k] : ayt::math::FVector3(1, 1, 1);
            }
        }

        // 2. Convert each sample's world TRS back to parent-local
        //    using the FIRST active player's parent world TRS.
        std::vector<ayt::math::FVector3>    srcLocalPos(_activeIndices.size());
        std::vector<ayt::math::FQuaternion> srcLocalRot(_activeIndices.size());
        std::vector<ayt::math::FVector3>    srcLocalScl(_activeIndices.size());
        if (bones && bones[k].parentIndex >= 0 && _players[_activeIndices[0]]
            && _players[_activeIndices[0]]->isValid()) {
            const int p = bones[k].parentIndex;
            ayt::math::FVector3    parPos, parScl;
            ayt::math::FQuaternion parRot;
            readPlayerBoneTRS(*_players[_activeIndices[0]],
                              static_cast<size_t>(p), bones, parPos, parRot, parScl);
            for (size_t j = 0; j < _activeIndices.size(); ++j) {
                worldLocalToParentLocal(srcWorldPos[j], srcWorldRot[j], srcWorldScl[j],
                                        parPos, parRot, parScl,
                                        srcLocalPos[j], srcLocalRot[j], srcLocalScl[j]);
            }
        } else {
            // Root bone — world TRS == parent-local TRS.
            for (size_t j = 0; j < _activeIndices.size(); ++j) {
                srcLocalPos[j] = srcWorldPos[j];
                srcLocalRot[j] = srcWorldRot[j];
                srcLocalScl[j] = srcWorldScl[j];
            }
        }

        // 3. Weighted blend:
        //    pos[k] = Σ_i w[i] * pos[i]
        //    rot[k] = blendQuatNWay(bindRot, srcLocalRot, w)
        //    scl[k] = Σ_i w[i] * scl[i]  (linear; UE convention for
        //          scale in BlendSpace differs from the P1.2
        //          multiplicative scale because BlendSpace clips are
        //          Override-mode at sample point authoring — the
        //          linear sum is correct for the dominant case.)
        const ayt::math::FQuaternion bindRot = rR ? rR[k]
                                                  : ayt::math::FQuaternion::identity();
        float px = 0, py = 0, pz = 0;
        float sx = 0, sy = 0, sz = 0;
        for (size_t j = 0; j < _activeIndices.size(); ++j) {
            const float w = _activeWeights[j];
            px += w * srcLocalPos[j].x;
            py += w * srcLocalPos[j].y;
            pz += w * srcLocalPos[j].z;
            sx += w * srcLocalScl[j].x;
            sy += w * srcLocalScl[j].y;
            sz += w * srcLocalScl[j].z;
        }
        const ayt::math::FQuaternion blendedRot = blendQuatNWay(
            bindRot, srcLocalRot, _activeWeights);

        outLocalPos[k * 3 + 0] = px;
        outLocalPos[k * 3 + 1] = py;
        outLocalPos[k * 3 + 2] = pz;
        outLocalRot[k * 4 + 0] = blendedRot.x;
        outLocalRot[k * 4 + 1] = blendedRot.y;
        outLocalRot[k * 4 + 2] = blendedRot.z;
        outLocalRot[k * 4 + 3] = blendedRot.w;
        outLocalScl[k * 3 + 0] = sx;
        outLocalScl[k * 3 + 1] = sy;
        outLocalScl[k * 3 + 2] = sz;
    }
}

void BlendSpace1D::rebuildWeights()
{
    _activeIndices.clear();
    _activeWeights.clear();
    const size_t N = _samples.size();
    if (N == 0) return;

    // Sort sample indices by parameter. Don't physically reorder
    // _samples — we carry the original index in _activeIndices.
    std::vector<size_t> sorted(N);
    for (size_t i = 0; i < N; ++i) sorted[i] = i;
    std::sort(sorted.begin(), sorted.end(),
              [this](size_t a, size_t b) {
                  return _samples[a].parameter < _samples[b].parameter;
              });

    if (N == 1) {
        _activeIndices = { sorted[0] };
        _activeWeights = { 1.0f };
        return;
    }

    // Find the bracketing pair. parameter < first → clamp to first;
    // parameter > last → clamp to last.
    if (_parameter <= _samples[sorted[0]].parameter) {
        _activeIndices = { sorted[0] };
        _activeWeights = { 1.0f };
        return;
    }
    if (_parameter >= _samples[sorted.back()].parameter) {
        _activeIndices = { sorted.back() };
        _activeWeights = { 1.0f };
        return;
    }

    // Linear scan for the bracket. N is small (production ≤16) so
    // an O(N) walk is fine.
    for (size_t i = 0; i + 1 < N; ++i) {
        const float pA = _samples[sorted[i]].parameter;
        const float pB = _samples[sorted[i + 1]].parameter;
        if (pA <= _parameter && _parameter <= pB) {
            const float denom = (pB - pA);
            const float t = (denom > kParamEps) ? (_parameter - pA) / denom : 0.0f;
            _activeIndices = { sorted[i], sorted[i + 1] };
            _activeWeights = { 1.0f - t, t };
            return;
        }
    }

    // Should not reach here (parameter is bracketed), but be defensive.
    _activeIndices = { sorted[0] };
    _activeWeights = { 1.0f };
}

// ===========================================================================
// BlendSpace2D — implementation
// ===========================================================================

void BlendSpace2D::addSamplePoint(
    ayt::math::FVector2 position,
    std::shared_ptr<const ayt::resource::IAnimation> clip)
{
    SamplePoint sp;
    sp.position = position;
    sp.clip     = std::move(clip);
    _samples.push_back(std::move(sp));
    _triangles.clear();   // triangulation is stale; will be rebuilt lazily.
}

void BlendSpace2D::removeAllSamplePoints()
{
    _samples.clear();
    _players.clear();
    _activeIndices.clear();
    _activeWeights.clear();
    _triangles.clear();
}

void BlendSpace2D::setSkeleton(
    std::shared_ptr<const ayt::resource::ISkeleton> skel)
{
    _skeleton = std::move(skel);
    if (_skeleton) {
        resizeTRS(_skeleton->getBoneCount());
        for (auto& p : _players) {
            if (p) p->setSkeleton(_skeleton);
        }
    } else {
        resizeTRS(0);
    }
}

void BlendSpace2D::setParameter(ayt::math::FVector2 p)
{
    _parameter = p;
    rebuildWeights();
}

void BlendSpace2D::setTriangulation(const std::vector<Triangle>& triangles)
{
    _triangles = triangles;
}

void BlendSpace2D::ensurePlayer(size_t sampleIndex)
{
    assert(sampleIndex < _samples.size());
    if (sampleIndex < _players.size() && _players[sampleIndex]) return;
    if (sampleIndex >= _players.size()) {
        _players.resize(_samples.size());
    }
    auto player = std::make_unique<AnimationPlayer>();
    if (_skeleton) player->setSkeleton(_skeleton);
    if (_samples[sampleIndex].clip) {
        player->play(_samples[sampleIndex].clip.get());
    }
    _players[sampleIndex] = std::move(player);
}

void BlendSpace2D::tick(float dt)
{
    for (size_t i = 0; i < _samples.size(); ++i) {
        ensurePlayer(i);
        if (_players[i]) {
            _players[i]->setLoop(_loop);
            _players[i]->setPlayRate(_playRate);
            _players[i]->tick(dt);
        }
    }
}

void BlendSpace2D::resizeTRS(size_t n)
{
    _skelBoneCount = n;
}

bool BlendSpace2D::barycentric2D(
    const ayt::math::FVector2& p,
    const ayt::math::FVector2& a,
    const ayt::math::FVector2& b,
    const ayt::math::FVector2& c,
    float& u, float& v, float& w)
{
    // Sign-based test (same as MathGeometry.h::Triangle::barycentric,
    // reimplemented here in 2D since 2D simplex math doesn't exist
    // elsewhere in the codebase).
    auto sign = [](const ayt::math::FVector2& p1,
                   const ayt::math::FVector2& p2,
                   const ayt::math::FVector2& p3) -> float {
        return (p1.x - p3.x) * (p2.y - p3.y)
             - (p2.x - p3.x) * (p1.y - p3.y);
    };
    const float d1 = sign(p, a, b);
    const float d2 = sign(p, b, c);
    const float d3 = sign(p, c, a);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    if (hasNeg && hasPos) {
        // Strictly outside the triangle.
        return false;
    }

    // Inside (or on edge). Compute barycentric via cross-product
    // weights (matches standard 2D barycentric formula).
    const float denom = (b.y - c.y) * (a.x - c.x)
                      + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(denom) < kParamEps) {
        // Degenerate triangle — fall back to vertex A.
        u = 1.0f; v = 0.0f; w = 0.0f;
        return true;
    }
    u = ((b.y - c.y) * (p.x - c.x)
       + (c.x - b.x) * (p.y - c.y)) / denom;
    v = ((c.y - a.y) * (p.x - c.x)
       + (a.x - c.x) * (p.y - c.y)) / denom;
    w = 1.0f - u - v;
    return true;
}

void BlendSpace2D::buildHeuristicTriangulation()
{
    _triangles.clear();
    const size_t N = _samples.size();
    if (N < 3) return;

    // Bounding-rect heuristic: take the axis-aligned bounding box,
    // subdivide into 2 triangles by the + diagonal. For ≤16 sample
    // points this is good enough; production authoring tools should
    // ship explicit Delaunay via setTriangulation().
    ayt::math::FVector2 lo( FLT_MAX,  FLT_MAX);
    ayt::math::FVector2 hi(-FLT_MAX, -FLT_MAX);
    for (const auto& s : _samples) {
        lo.x = std::min(lo.x, s.position.x);
        lo.y = std::min(lo.y, s.position.y);
        hi.x = std::max(hi.x, s.position.x);
        hi.y = std::max(hi.y, s.position.y);
    }
    // The bounding rectangle's corners are NOT in the sample set;
    // map them to the nearest real sample to build a coarse triangle
    // pair. This produces at most 4 candidate triangles that touch
    // every sample point with at least one vertex.
    auto nearestIdx = [&](ayt::math::FVector2 target) -> uint32_t {
        uint32_t best = 0;
        float bestDist = FLT_MAX;
        for (size_t i = 0; i < N; ++i) {
            const float dx = _samples[i].position.x - target.x;
            const float dy = _samples[i].position.y - target.y;
            const float d = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = static_cast<uint32_t>(i); }
        }
        return best;
    };
    const uint32_t iBL = nearestIdx(ayt::math::FVector2(lo.x, lo.y));
    const uint32_t iBR = nearestIdx(ayt::math::FVector2(hi.x, lo.y));
    const uint32_t iTL = nearestIdx(ayt::math::FVector2(lo.x, hi.y));
    const uint32_t iTR = nearestIdx(ayt::math::FVector2(hi.x, hi.y));

    // Two triangles covering the bounding rect.
    _triangles.push_back({ iBL, iBR, iTR });
    _triangles.push_back({ iBL, iTR, iTL });

    // If duplicates (e.g. N==3 and the four corners collapse to the
    // same 3 unique samples), prune duplicate triangle entries — same
    // vertex set regardless of order is treated as duplicate.
    auto triEq = [](const Triangle& a, const Triangle& b) {
        auto setOf = [](const Triangle& t) {
            std::array<uint32_t, 3> s = { t.a, t.b, t.c };
            std::sort(s.begin(), s.end());
            return s;
        };
        return setOf(a) == setOf(b);
    };
    std::vector<Triangle> dedup;
    for (const auto& t : _triangles) {
        bool found = false;
        for (const auto& u : dedup) {
            if (triEq(t, u)) { found = true; break; }
        }
        if (!found) dedup.push_back(t);
    }
    _triangles = dedup;
}

void BlendSpace2D::rebuildWeights()
{
    _activeIndices.clear();
    _activeWeights.clear();
    const size_t N = _samples.size();
    if (N == 0) return;

    if (N == 1) {
        _activeIndices = { 0 };
        _activeWeights = { 1.0f };
        return;
    }

    // Clamp parameter to bounding rect first — boundary queries always
    // land on some enclosing triangle.
    ayt::math::FVector2 lo( FLT_MAX,  FLT_MAX);
    ayt::math::FVector2 hi(-FLT_MAX, -FLT_MAX);
    for (const auto& s : _samples) {
        lo.x = std::min(lo.x, s.position.x);
        lo.y = std::min(lo.y, s.position.y);
        hi.x = std::max(hi.x, s.position.x);
        hi.y = std::max(hi.y, s.position.y);
    }
    ayt::math::FVector2 clamped(
        std::min(std::max(_parameter.x, lo.x), hi.x),
        std::min(std::max(_parameter.y, lo.y), hi.y));

    // Build triangulation lazily when needed.
    if (_triangles.empty()) {
        buildHeuristicTriangulation();
    }

    // Linear scan over triangles; first hit wins.
    for (const Triangle& tri : _triangles) {
        if (tri.a >= N || tri.b >= N || tri.c >= N) continue;
        float u = 0, v = 0, w = 0;
        if (barycentric2D(clamped,
                          _samples[tri.a].position,
                          _samples[tri.b].position,
                          _samples[tri.c].position,
                          u, v, w)) {
            _activeIndices = { tri.a, tri.b, tri.c };
            _activeWeights = { u, v, w };
            return;
        }
    }

    // No triangle contains the point — fall back to nearest vertex.
    uint32_t nearest = 0;
    float bestDist = FLT_MAX;
    for (size_t i = 0; i < N; ++i) {
        const float dx = _samples[i].position.x - clamped.x;
        const float dy = _samples[i].position.y - clamped.y;
        const float d = dx * dx + dy * dy;
        if (d < bestDist) { bestDist = d; nearest = static_cast<uint32_t>(i); }
    }
    _activeIndices = { nearest };
    _activeWeights = { 1.0f };
}

void BlendSpace2D::evaluate(
    std::vector<float>& outLocalPos,
    std::vector<float>& outLocalRot,
    std::vector<float>& outLocalScl)
{
    // Identical body to BlendSpace1D::evaluate — copy-paste is intentional
    // here (P2.x cleanup: extract a free helper in AYAnimation/AYAnimation.h).
    const size_t n = _skelBoneCount;
    outLocalPos.resize(n * 3, 0.0f);
    outLocalRot.resize(n * 4, 0.0f);
    outLocalScl.resize(n * 3, 1.0f);

    if (n == 0 || _samples.empty()) return;
    if (_activeIndices.empty()) {
        if (_skeleton) {
            const ayt::math::FVector3*    rP = _skeleton->getLocalPositions();
            const ayt::math::FQuaternion* rR = _skeleton->getLocalRotations();
            const ayt::math::FVector3*    rS = _skeleton->getLocalScales();
            for (size_t k = 0; k < n; ++k) {
                if (rP) {
                    outLocalPos[k * 3 + 0] = rP[k].x;
                    outLocalPos[k * 3 + 1] = rP[k].y;
                    outLocalPos[k * 3 + 2] = rP[k].z;
                }
                if (rR) {
                    outLocalRot[k * 4 + 0] = rR[k].x;
                    outLocalRot[k * 4 + 1] = rR[k].y;
                    outLocalRot[k * 4 + 2] = rR[k].z;
                    outLocalRot[k * 4 + 3] = rR[k].w;
                }
                if (rS) {
                    outLocalScl[k * 3 + 0] = rS[k].x;
                    outLocalScl[k * 3 + 1] = rS[k].y;
                    outLocalScl[k * 3 + 2] = rS[k].z;
                }
            }
        }
        return;
    }

    for (size_t idx : _activeIndices) {
        ensurePlayer(idx);
        if (_players[idx] && _players[idx]->isValid()) {
            _players[idx]->evaluate();
        }
    }

    const ayt::resource::Bone* bones = _skeleton ? _skeleton->getBones() : nullptr;
    const ayt::math::FVector3*    rP = _skeleton ? _skeleton->getLocalPositions()    : nullptr;
    const ayt::math::FQuaternion* rR = _skeleton ? _skeleton->getLocalRotations()    : nullptr;
    const ayt::math::FVector3*    rS = _skeleton ? _skeleton->getLocalScales()       : nullptr;

    for (size_t k = 0; k < n; ++k) {
        std::vector<ayt::math::FVector3>    srcWorldPos(_activeIndices.size());
        std::vector<ayt::math::FQuaternion> srcWorldRot(_activeIndices.size());
        std::vector<ayt::math::FVector3>    srcWorldScl(_activeIndices.size());
        for (size_t j = 0; j < _activeIndices.size(); ++j) {
            const size_t idx = _activeIndices[j];
            if (_players[idx] && _players[idx]->isValid()) {
                readPlayerBoneTRS(*_players[idx], k, bones,
                                  srcWorldPos[j], srcWorldRot[j], srcWorldScl[j]);
            } else {
                srcWorldPos[j] = rP ? rP[k] : ayt::math::FVector3(0, 0, 0);
                srcWorldRot[j] = rR ? rR[k] : ayt::math::FQuaternion::identity();
                srcWorldScl[j] = rS ? rS[k] : ayt::math::FVector3(1, 1, 1);
            }
        }

        std::vector<ayt::math::FVector3>    srcLocalPos(_activeIndices.size());
        std::vector<ayt::math::FQuaternion> srcLocalRot(_activeIndices.size());
        std::vector<ayt::math::FVector3>    srcLocalScl(_activeIndices.size());
        if (bones && bones[k].parentIndex >= 0 && _players[_activeIndices[0]]
            && _players[_activeIndices[0]]->isValid()) {
            const int p = bones[k].parentIndex;
            ayt::math::FVector3    parPos, parScl;
            ayt::math::FQuaternion parRot;
            readPlayerBoneTRS(*_players[_activeIndices[0]],
                              static_cast<size_t>(p), bones, parPos, parRot, parScl);
            for (size_t j = 0; j < _activeIndices.size(); ++j) {
                worldLocalToParentLocal(srcWorldPos[j], srcWorldRot[j], srcWorldScl[j],
                                        parPos, parRot, parScl,
                                        srcLocalPos[j], srcLocalRot[j], srcLocalScl[j]);
            }
        } else {
            for (size_t j = 0; j < _activeIndices.size(); ++j) {
                srcLocalPos[j] = srcWorldPos[j];
                srcLocalRot[j] = srcWorldRot[j];
                srcLocalScl[j] = srcWorldScl[j];
            }
        }

        const ayt::math::FQuaternion bindRot = rR ? rR[k]
                                                  : ayt::math::FQuaternion::identity();
        float px = 0, py = 0, pz = 0;
        float sx = 0, sy = 0, sz = 0;
        for (size_t j = 0; j < _activeIndices.size(); ++j) {
            const float w = _activeWeights[j];
            px += w * srcLocalPos[j].x;
            py += w * srcLocalPos[j].y;
            pz += w * srcLocalPos[j].z;
            sx += w * srcLocalScl[j].x;
            sy += w * srcLocalScl[j].y;
            sz += w * srcLocalScl[j].z;
        }
        const ayt::math::FQuaternion blendedRot = blendQuatNWay(
            bindRot, srcLocalRot, _activeWeights);

        outLocalPos[k * 3 + 0] = px;
        outLocalPos[k * 3 + 1] = py;
        outLocalPos[k * 3 + 2] = pz;
        outLocalRot[k * 4 + 0] = blendedRot.x;
        outLocalRot[k * 4 + 1] = blendedRot.y;
        outLocalRot[k * 4 + 2] = blendedRot.z;
        outLocalRot[k * 4 + 3] = blendedRot.w;
        outLocalScl[k * 3 + 0] = sx;
        outLocalScl[k * 3 + 1] = sy;
        outLocalScl[k * 3 + 2] = sz;
    }
}

// ===========================================================================
// Library-mode static helper (shared by BlendSpace1D and BlendSpace2D)
// ===========================================================================

namespace
{
void computeWeightedBoneTRSImpl(
    const std::vector<std::vector<float>>& srcLocalPos,
    const std::vector<std::vector<float>>& srcLocalRot,
    const std::vector<std::vector<float>>& srcLocalScl,
    const std::vector<float>&              weights,
    const ayt::resource::ISkeleton*        skel,
    std::vector<float>&                    outLocalPos,
    std::vector<float>&                    outLocalRot,
    std::vector<float>&                    outLocalScl)
{
    assert(skel != nullptr);
    const size_t n = skel->getBoneCount();
    const size_t N = weights.size();
    assert(N == srcLocalPos.size());
    assert(N == srcLocalRot.size());
    assert(N == srcLocalScl.size());
    // Each per-clip buffer is sized n*3 / n*4 / n*3.
    for (const auto& v : srcLocalPos) assert(v.size() == n * 3);
    for (const auto& v : srcLocalRot) assert(v.size() == n * 4);
    for (const auto& v : srcLocalScl) assert(v.size() == n * 3);

    outLocalPos.resize(n * 3, 0.0f);
    outLocalRot.resize(n * 4, 0.0f);
    outLocalScl.resize(n * 3, 1.0f);

    const ayt::math::FVector3*    rP = skel->getLocalPositions();
    const ayt::math::FQuaternion* rR = skel->getLocalRotations();
    const ayt::math::FVector3*    rS = skel->getLocalScales();

    for (size_t k = 0; k < n; ++k) {
        // Compose per-sample rotations.
        std::vector<ayt::math::FQuaternion> srcRots(N);
        for (size_t i = 0; i < N; ++i) {
            srcRots[i] = ayt::math::FQuaternion(
                srcLocalRot[i][k * 4 + 0],
                srcLocalRot[i][k * 4 + 1],
                srcLocalRot[i][k * 4 + 2],
                srcLocalRot[i][k * 4 + 3]);
        }
        const ayt::math::FQuaternion bindRot = rR ? rR[k]
                                                  : ayt::math::FQuaternion::identity();
        const ayt::math::FQuaternion blendedRot = blendQuatNWay(bindRot, srcRots, weights);

        float px = 0, py = 0, pz = 0;
        float sx = 0, sy = 0, sz = 0;
        for (size_t i = 0; i < N; ++i) {
            const float w = weights[i];
            px += w * srcLocalPos[i][k * 3 + 0];
            py += w * srcLocalPos[i][k * 3 + 1];
            pz += w * srcLocalPos[i][k * 3 + 2];
            sx += w * srcLocalScl[i][k * 3 + 0];
            sy += w * srcLocalScl[i][k * 3 + 1];
            sz += w * srcLocalScl[i][k * 3 + 2];
        }
        outLocalPos[k * 3 + 0] = px;
        outLocalPos[k * 3 + 1] = py;
        outLocalPos[k * 3 + 2] = pz;
        outLocalRot[k * 4 + 0] = blendedRot.x;
        outLocalRot[k * 4 + 1] = blendedRot.y;
        outLocalRot[k * 4 + 2] = blendedRot.z;
        outLocalRot[k * 4 + 3] = blendedRot.w;
        outLocalScl[k * 3 + 0] = sx;
        outLocalScl[k * 3 + 1] = sy;
        outLocalScl[k * 3 + 2] = sz;
    }
}
} // namespace

void BlendSpace1D::computeWeightedBoneTRS(
    const std::vector<std::vector<float>>& srcLocalPos,
    const std::vector<std::vector<float>>& srcLocalRot,
    const std::vector<std::vector<float>>& srcLocalScl,
    const std::vector<float>&              weights,
    const ayt::resource::ISkeleton*        skel,
    std::vector<float>&                    outLocalPos,
    std::vector<float>&                    outLocalRot,
    std::vector<float>&                    outLocalScl)
{
    computeWeightedBoneTRSImpl(srcLocalPos, srcLocalRot, srcLocalScl,
                               weights, skel,
                               outLocalPos, outLocalRot, outLocalScl);
}

void BlendSpace2D::computeWeightedBoneTRS(
    const std::vector<std::vector<float>>& srcLocalPos,
    const std::vector<std::vector<float>>& srcLocalRot,
    const std::vector<std::vector<float>>& srcLocalScl,
    const std::vector<float>&              weights,
    const ayt::resource::ISkeleton*        skel,
    std::vector<float>&                    outLocalPos,
    std::vector<float>&                    outLocalRot,
    std::vector<float>&                    outLocalScl)
{
    computeWeightedBoneTRSImpl(srcLocalPos, srcLocalRot, srcLocalScl,
                               weights, skel,
                               outLocalPos, outLocalRot, outLocalScl);
}

} // namespace ayt::anim