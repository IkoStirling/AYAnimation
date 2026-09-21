#include "SkeletonBakeReferences.h"

#include <AYResource/assetsImpl/Mesh.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYResource/assetsImpl/SkeletonMask.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace ayt::anim::editor {
namespace {

bool buildIndexRemap(const SkeletonBakeReferenceContext& context,
                     std::vector<int>& remap,
                     std::string& error)
{
    const std::size_t boneCount = context.sourceSkeleton.getBoneCount();
    if (context.boneOperations.size() != boneCount) {
        error = "Bake plan no longer matches the source skeleton bone count.";
        return false;
    }
    remap.assign(boneCount, -1);
    int next = 0;
    for (const SkeletonBakeBoneOperation& operation : context.boneOperations) {
        if (operation.sourceBoneIndex < 0
            || operation.sourceBoneIndex >= static_cast<int>(boneCount)) {
            error = "Bake plan contains an invalid source bone index.";
            return false;
        }
        if (operation.action != SkeletonBakeBoneAction::Delete) {
            remap[static_cast<std::size_t>(operation.sourceBoneIndex)] = next++;
        }
    }
    return true;
}

bool cloneMeshBase(const ayt::resource::Mesh& source,
                   ayt::resource::Mesh& target,
                   std::string& error)
{
    target.setGuid(source.getGuid());
    target._setForTestVertexLayout(source.getAttributeMask(),
        source.getVertexCount(), source.getVertexStride());
    const std::size_t vertexBytes = static_cast<std::size_t>(
        source.getVertexCount()) * source.getVertexStride();
    if (vertexBytes > 0u && source.getVertexData() == nullptr) {
        error = "Mesh has missing vertex data.";
        return false;
    }
    target._setForTestVertexData(source.getVertexData(), vertexBytes);
    if (source.getIndexCount() > 0u && source.getIndexData() == nullptr) {
        error = "Mesh has missing index data.";
        return false;
    }
    target._setForTestIndices(source.getIndexData(), source.getIndexCount());
    if (source.getSubmeshCount() > 0u) {
        if (source.getSubmeshes() == nullptr) {
            error = "Mesh has missing submesh data.";
            return false;
        }
        target._setForTestSubmeshes(
            source.getSubmeshes(), source.getSubmeshCount());
    }
    for (ayt::math::UInt32 index = 0;
         index < source.getMaterialSlotCount(); ++index) {
        const char* slot = source.getMaterialSlot(index);
        target._addForTestMaterialSlot(slot != nullptr ? slot : "");
    }
    if (source.hasBounds()) {
        const auto bounds = source.getBounds();
        target._setForTestBounds(bounds.center, bounds.halfExtent);
    }
    for (ayt::math::UInt32 index = 0;
         index < source.getExtensionCount(); ++index) {
        const auto* extension = source.getExtension(index);
        if (extension == nullptr
            || (extension->size > 0u && extension->data == nullptr)) {
            error = "Mesh has an invalid extension payload.";
            return false;
        }
        target._setForTestExtension(
            extension->type, extension->data, extension->size);
    }
    return true;
}

bool rewritePaletteSkin(const ayt::resource::Mesh& source,
                        const std::vector<int>& boneRemap,
                        ayt::resource::Mesh& target,
                        std::string& error)
{
    using ayt::math::UInt8;
    using ayt::math::UInt32;
    using ayt::resource::SkinPalette;
    using ayt::resource::VertexSkinWeight;

    const UInt32 vertexCount = source.getVertexCount();
    const VertexSkinWeight* sourceWeights = source.getSkinWeights();
    if (sourceWeights == nullptr) {
        error = "Skinned mesh has no skin-weight records.";
        return false;
    }
    std::vector<VertexSkinWeight> weights(
        sourceWeights, sourceWeights + vertexCount);
    const UInt32 paletteCount = source.getSkinPaletteCount();
    const SkinPalette* sourcePalettes = source.getSkinPalettes();
    const UInt32 paletteJointCount = source.getSkinPaletteJointCount();
    const UInt32* sourcePaletteJoints = source.getSkinPaletteJoints();

    if (paletteCount == 0u) {
        for (UInt32 vertex = 0; vertex < vertexCount; ++vertex) {
            for (UInt32 slot = 0; slot < 4u; ++slot) {
                if (!std::isfinite(weights[vertex].boneWeight[slot])
                    || weights[vertex].boneWeight[slot] < 0.0f) {
                    error = "Mesh contains a non-finite or negative skin weight.";
                    return false;
                }
                if (weights[vertex].boneWeight[slot] <= 0.0f) {
                    weights[vertex].boneIndex[slot] = 0u;
                    continue;
                }
                const UInt32 sourceJoint = weights[vertex].boneIndex[slot];
                if (sourceJoint >= boneRemap.size()
                    || boneRemap[sourceJoint] < 0
                    || boneRemap[sourceJoint]
                        > static_cast<int>((std::numeric_limits<UInt8>::max)())) {
                    error = "Mesh has an active influence on a removed or invalid bone.";
                    return false;
                }
                weights[vertex].boneIndex[slot] =
                    static_cast<UInt8>(boneRemap[sourceJoint]);
            }
        }
        target._setForTestSkinWeights(weights);
        return true;
    }

    if (paletteCount != source.getSubmeshCount() || sourcePalettes == nullptr
        || (paletteJointCount > 0u && sourcePaletteJoints == nullptr)) {
        error = "Mesh skin palettes do not match its submeshes.";
        return false;
    }
    const auto* submeshes = source.getSubmeshes();
    const UInt32* indices = source.getIndexData();
    std::vector<SkinPalette> palettes;
    std::vector<UInt32> joints;
    std::vector<std::uint8_t> assigned(vertexCount, 0u);
    palettes.reserve(paletteCount);

    for (UInt32 section = 0; section < paletteCount; ++section) {
        const SkinPalette& oldPalette = sourcePalettes[section];
        const std::uint64_t paletteEnd =
            static_cast<std::uint64_t>(oldPalette.jointOffset)
            + oldPalette.jointCount;
        const auto& submesh = submeshes[section];
        const std::uint64_t indexEnd =
            static_cast<std::uint64_t>(submesh.indexOffset)
            + submesh.indexCount;
        if (paletteEnd > paletteJointCount
            || indexEnd > source.getIndexCount()) {
            error = "Mesh contains an out-of-range palette or submesh span.";
            return false;
        }
        std::vector<std::uint8_t> used(oldPalette.jointCount, 0u);
        for (std::uint64_t at = submesh.indexOffset; at < indexEnd; ++at) {
            const UInt32 vertex = indices[at];
            if (vertex >= vertexCount) {
                error = "Mesh submesh references a vertex outside the mesh.";
                return false;
            }
            for (UInt32 slot = 0; slot < 4u; ++slot) {
                const float weight = sourceWeights[vertex].boneWeight[slot];
                if (!std::isfinite(weight) || weight < 0.0f) {
                    error = "Mesh contains a non-finite or negative skin weight.";
                    return false;
                }
                if (weight <= 0.0f) continue;
                const UInt32 local = sourceWeights[vertex].boneIndex[slot];
                if (local >= oldPalette.jointCount) {
                    error = "Mesh vertex references a joint outside its palette.";
                    return false;
                }
                used[local] = 1u;
            }
        }

        std::vector<int> localRemap(oldPalette.jointCount, -1);
        SkinPalette newPalette;
        newPalette.jointOffset = static_cast<UInt32>(joints.size());
        for (UInt32 local = 0; local < oldPalette.jointCount; ++local) {
            if (used[local] == 0u) continue;
            const UInt32 sourceJoint = sourcePaletteJoints[
                oldPalette.jointOffset + local];
            if (sourceJoint >= boneRemap.size() || boneRemap[sourceJoint] < 0) {
                error = "Mesh has an active influence on a removed or invalid bone.";
                return false;
            }
            localRemap[local] = static_cast<int>(newPalette.jointCount++);
            joints.push_back(static_cast<UInt32>(boneRemap[sourceJoint]));
        }
        if (newPalette.jointCount > 256u) {
            error = "Rewritten mesh palette exceeds the UInt8 joint limit.";
            return false;
        }
        palettes.push_back(newPalette);

        for (std::uint64_t at = submesh.indexOffset; at < indexEnd; ++at) {
            const UInt32 vertex = indices[at];
            VertexSkinWeight candidate = sourceWeights[vertex];
            for (UInt32 slot = 0; slot < 4u; ++slot) {
                if (candidate.boneWeight[slot] <= 0.0f) {
                    candidate.boneIndex[slot] = 0u;
                    continue;
                }
                const int local = localRemap[candidate.boneIndex[slot]];
                if (local < 0 || local > 255) {
                    error = "Mesh palette compaction lost an active influence.";
                    return false;
                }
                candidate.boneIndex[slot] = static_cast<UInt8>(local);
            }
            if (assigned[vertex] != 0u
                && std::memcmp(&weights[vertex], &candidate,
                    sizeof(VertexSkinWeight)) != 0) {
                error = "Mesh shares one skinned vertex across incompatible palettes.";
                return false;
            }
            weights[vertex] = candidate;
            assigned[vertex] = 1u;
        }
    }
    target._setForTestSkinWeights(weights);
    target._setForTestSkinPalettes(palettes, joints);
    return true;
}

} // namespace

bool rewriteMeshDependency(
    const std::string& sourcePath,
    const SkeletonBakeReferenceContext& context,
    std::vector<ayt::math::UInt8>& output,
    std::string& error)
{
    ayt::resource::Mesh source;
    if (!source.load(sourcePath)) {
        error = "Unable to load mesh dependency: " + sourcePath;
        return false;
    }
    if (context.retarget && source.hasSkinWeights()) {
        error = "A skinned mesh cannot be rebound to a different target skeleton "
            "without geometry-space bind conversion: " + sourcePath;
        return false;
    }
    ayt::resource::Mesh baked;
    if (!cloneMeshBase(source, baked, error)) {
        error += " " + sourcePath;
        return false;
    }
    if (source.hasSkinWeights()) {
        std::vector<int> remap;
        if (!buildIndexRemap(context, remap, error)
            || !rewritePaletteSkin(source, remap, baked, error)) {
            error += " " + sourcePath;
            return false;
        }
    }
    if (!baked.saveToBinary(output)) {
        error = "Unable to serialize baked mesh: " + sourcePath;
        return false;
    }
    return true;
}

bool rewriteSkeletonMaskDependency(
    const std::string& sourcePath,
    const SkeletonBakeReferenceContext& context,
    std::vector<ayt::math::UInt8>& output,
    std::string& error)
{
    ayt::resource::SkeletonMask source;
    if (!source.load(sourcePath)) {
        error = "Unable to load skeleton-mask dependency: " + sourcePath;
        return false;
    }
    ayt::resource::SkeletonMask baked;
    baked.setGuid(source.getGuid());
    baked.setDebugName(source.getDebugName());
    std::unordered_map<std::string, std::string> semanticNames;
    for (const SkeletonBakeBoneOperation& operation : context.boneOperations) {
        semanticNames.emplace(operation.sourceName,
            operation.action == SkeletonBakeBoneAction::Delete
                ? std::string{} : operation.targetName);
    }
    std::unordered_map<std::string, float> emittedWeights;
    const auto* entries = source.getEntries();
    for (std::size_t index = 0; index < source.getAuthoredBoneCount(); ++index) {
        const auto& entry = entries[index];
        std::string targetName;
        if (!context.retarget) {
            const auto found = semanticNames.find(entry.name);
            if (found == semanticNames.end()) {
                error = "Skeleton mask references a bone absent from the source skeleton: "
                    + entry.name;
                return false;
            }
            targetName = found->second;
            if (targetName.empty()) continue;
        } else {
            if (context.targetSkeleton == nullptr
                || context.retargetDefinition == nullptr) {
                error = "Retarget mask rewrite is missing its target context.";
                return false;
            }
            const int sourceIndex = context.sourceSkeleton.findBone(
                entry.name.c_str());
            HumanoidBone role = HumanoidBone::Invalid;
            for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                if (context.retargetDefinition->sourceMapping
                        .getSourceBoneIndex(spec.role) == sourceIndex) {
                    role = spec.role;
                    break;
                }
            }
            const int targetIndex = role == HumanoidBone::Invalid ? -1
                : context.retargetDefinition->targetMapping
                    .getSourceBoneIndex(role);
            if (sourceIndex < 0 || targetIndex < 0
                || targetIndex >= static_cast<int>(
                    context.targetSkeleton->getBoneCount())) {
                error = "Skeleton mask bone has no safe target-role mapping: "
                    + entry.name;
                return false;
            }
            targetName = context.targetSkeleton->getBones()[targetIndex].name;
        }
        const auto duplicate = emittedWeights.find(targetName);
        if (duplicate != emittedWeights.end()
            && std::abs(duplicate->second - entry.weight) > 1.0e-6f) {
            error = "Skeleton mask entries collapse to one target bone with "
                "different weights: " + targetName;
            return false;
        }
        emittedWeights[targetName] = entry.weight;
        baked.addEntry(targetName.c_str(), entry.weight);
    }
    if (source.hasWildcard()) {
        baked.addEntry("", source.wildcardWeight());
    }
    if (!baked.saveToBinary(output)) {
        error = "Unable to serialize baked skeleton mask: " + sourcePath;
        return false;
    }
    return true;
}

} // namespace ayt::anim::editor
