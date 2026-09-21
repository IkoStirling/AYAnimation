#include <AYAnimationEditor/SkeletonBakeJob.h>

#include "SkeletonBakeReferences.h"

#include <AYAnimation/HumanoidRetarget.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Mesh.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYResource/assetsImpl/SkeletonMask.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ayt::anim::editor {
namespace {

using Json = nlohmann::json;

std::string normalizedPath(const std::filesystem::path& path)
{
    return path.lexically_normal().generic_string();
}

struct RunState {
    enum class Phase : std::uint8_t {
        Building,
        Committing,
        Finished,
        Cancelled,
    };

    std::uint64_t generation = 0u;
    std::atomic<SkeletonBakeJobState> state{SkeletonBakeJobState::Running};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancelled{false};
    std::atomic<Phase> phase{Phase::Building};
    std::mutex detailsMutex;
    std::string message = "Preparing skeleton bake.";
    std::string sourceFingerprint;
    std::string profileFingerprint;
    std::vector<std::string> outputPaths;
};

void setDetails(const std::shared_ptr<RunState>& run, std::string message,
                std::vector<std::string> outputs = {})
{
    std::lock_guard lock(run->detailsMutex);
    run->message = std::move(message);
    if (!outputs.empty()) run->outputPaths = std::move(outputs);
}

void fail(const std::shared_ptr<RunState>& run, std::string message)
{
    if (run->cancelled.load()) return;
    setDetails(run, std::move(message));
    run->state.store(SkeletonBakeJobState::Failed);
    run->phase.store(RunState::Phase::Finished);
}

bool writeBytes(const std::filesystem::path& path,
                const std::vector<ayt::math::UInt8>& bytes)
{
    return ayt::io::File::writeAllBytes(path.string(), bytes);
}

void cleanupFiles(const std::vector<std::filesystem::path>& paths)
{
    for (const auto& path : paths) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

struct StagedFile {
    enum class Kind : std::uint8_t {
        Skeleton,
        Animation,
        Mesh,
        SkeletonMask,
        Receipt,
    };
    Kind kind = Kind::Animation;
    std::string sourcePath;
    std::filesystem::path temporary;
    std::filesystem::path destination;
};

bool commitFilesTransaction(const std::vector<StagedFile>& files,
                            std::uint64_t generation,
                            std::string& error)
{
    struct Entry {
        const StagedFile* file = nullptr;
        std::filesystem::path backup;
        bool hadOriginal = false;
        bool committed = false;
    };
    std::vector<Entry> entries;
    entries.reserve(files.size());
    for (const StagedFile& file : files) {
        Entry entry;
        entry.file = &file;
        entry.backup = file.destination.string() + ".g"
            + std::to_string(generation) + ".rollback";
        std::error_code filesystemError;
        entry.hadOriginal = std::filesystem::exists(
            file.destination, filesystemError) && !filesystemError;
        if (filesystemError) {
            error = "Unable to inspect existing bake output: "
                + normalizedPath(file.destination);
            return false;
        }
        std::filesystem::remove(entry.backup, filesystemError);
        if (filesystemError) {
            error = "Unable to clear stale bake rollback file: "
                + normalizedPath(entry.backup);
            return false;
        }
        entries.push_back(std::move(entry));
    }

    const auto rollback = [&entries]() {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            std::error_code ignored;
            if (it->committed) {
                std::filesystem::remove(it->file->destination, ignored);
            }
            if (it->hadOriginal && std::filesystem::exists(it->backup)) {
                ignored.clear();
                std::filesystem::rename(
                    it->backup, it->file->destination, ignored);
            }
        }
    };

    for (Entry& entry : entries) {
        std::error_code filesystemError;
        if (entry.hadOriginal) {
            std::filesystem::rename(
                entry.file->destination, entry.backup, filesystemError);
            if (filesystemError) {
                error = "Unable to preserve previous bake output: "
                    + normalizedPath(entry.file->destination);
                rollback();
                return false;
            }
        }
        filesystemError.clear();
        std::filesystem::rename(entry.file->temporary,
            entry.file->destination, filesystemError);
        if (filesystemError) {
            if (entry.hadOriginal) {
                std::error_code ignored;
                std::filesystem::rename(
                    entry.backup, entry.file->destination, ignored);
            }
            error = "Unable to commit bake output: "
                + normalizedPath(entry.file->destination);
            rollback();
            return false;
        }
        entry.committed = true;
    }
    for (const Entry& entry : entries) {
        if (!entry.hadOriginal) continue;
        std::error_code ignored;
        std::filesystem::remove(entry.backup, ignored);
    }
    return true;
}

bool validateStagedFile(const StagedFile& file, std::string& error)
{
    if (file.kind == StagedFile::Kind::Receipt) {
        try {
            const Json receipt = Json::parse(
                ayt::io::File::readAllText(file.temporary.string()));
            if (receipt.value("type", std::string{}) != "SkeletonBakeResult"
                || receipt.value("version", 0u) != 2u) {
                error = "Staged bake receipt failed schema validation.";
                return false;
            }
            return true;
        } catch (...) {
            error = "Staged bake receipt is unreadable.";
            return false;
        }
    }
    switch (file.kind) {
    case StagedFile::Kind::Skeleton: {
        ayt::resource::Skeleton skeleton;
        if (!skeleton.load(file.temporary.string())) {
            error = "Staged skeleton failed validation: "
                + normalizedPath(file.destination);
            return false;
        }
        return true;
    }
    case StagedFile::Kind::Animation: {
        ayt::resource::Animation animation;
        const auto bytes = ayt::io::File::readAllBytes(file.temporary.string());
        if (bytes.empty()
            || !animation.loadFromBinary(bytes.data(), bytes.size())) {
            error = "Staged animation failed validation: "
                + normalizedPath(file.destination);
            return false;
        }
        return true;
    }
    case StagedFile::Kind::Mesh: {
        ayt::resource::Mesh mesh;
        if (!mesh.load(file.temporary.string())) {
            error = "Staged mesh failed validation: "
                + normalizedPath(file.destination);
            return false;
        }
        return true;
    }
    case StagedFile::Kind::SkeletonMask: {
        ayt::resource::SkeletonMask mask;
        if (!mask.load(file.temporary.string())) {
            error = "Staged skeleton mask failed validation: "
                + normalizedPath(file.destination);
            return false;
        }
        return true;
    }
    case StagedFile::Kind::Receipt: return true;
    }
    return false;
}

bool rewriteAnimation(const std::string& sourcePath,
                      const std::unordered_map<std::string,
                          const SkeletonBakeBoneOperation*>& operations,
                      std::vector<ayt::math::UInt8>& output,
                      std::string& error)
{
    ayt::resource::Animation source;
    const std::vector<ayt::math::UInt8> sourceBytes =
        ayt::io::File::readAllBytes(sourcePath);
    if (sourceBytes.empty()
        || !source.loadFromBinary(sourceBytes.data(), sourceBytes.size())) {
        error = "Unable to load animation dependency: " + sourcePath;
        return false;
    }
    ayt::resource::Animation baked;
    baked.setName(source.getName() != nullptr ? source.getName() : "");
    baked.setDuration(source.getDuration());
    baked.setTicksPerSecond(source.getTicksPerSecond());
    baked.setGuid(source.getGuid());
    for (std::uint32_t notify = 0; notify < source.getNotifyCount(); ++notify) {
        baked.addNotify({source.getNotifyName(notify) != nullptr
                ? source.getNotifyName(notify) : "",
            source.getNotifyTime(notify), source.getNotifyPayload(notify)});
    }
    for (std::uint32_t index = 0; index < source.getTrackCount(); ++index) {
        const char* rawNode = source.getTrackNodeName(index);
        const std::string sourceNode = rawNode != nullptr ? rawNode : "";
        const auto found = operations.find(sourceNode);
        if (found != operations.end()
            && found->second->action == SkeletonBakeBoneAction::Delete) {
            continue;
        }
        ayt::resource::AnimTrack track;
        track.nodeName = found != operations.end()
            && !found->second->targetName.empty()
            ? found->second->targetName : sourceNode;
        const char* property = source.getTrackProperty(index);
        track.property = property != nullptr ? property : "";
        track.valueType = source.getTrackType(index);
        track.blendMode = source.getTrackBlendMode(index);
        const std::uint32_t count = source.getTrackKeyframeCount(index);
        const float* times = source.getTrackTimes(index);
        if (count > 0u && times == nullptr) {
            error = "Animation dependency has missing key times: " + sourcePath;
            return false;
        }
        if (count > 0u) track.times.assign(times, times + count);
        const std::size_t width = track.valueType == ayt::resource::AnimTrackType::Quaternion
            ? 4u : track.valueType == ayt::resource::AnimTrackType::Vector3 ? 3u : 1u;
        const float* values = source.getTrackValues(index);
        if (count > 0u && values == nullptr) {
            error = "Animation dependency has missing key values: " + sourcePath;
            return false;
        }
        if (count > 0u) {
            track.values.assign(values,
                values + static_cast<std::size_t>(count) * width);
        }
        baked.addTrack(track);
    }
    if (!baked.saveToBinary(output)) {
        error = "Unable to serialize baked animation: " + sourcePath;
        return false;
    }
    return true;
}

bool rewriteRetargetAnimation(const std::string& sourcePath,
                              const ayt::resource::Skeleton& sourceSkeleton,
                              const ayt::resource::Skeleton& targetSkeleton,
                              const HumanoidRetargetDefinition& definition,
                              std::vector<ayt::math::UInt8>& output,
                              std::string& error)
{
    ayt::resource::Animation source;
    const std::vector<ayt::math::UInt8> sourceBytes =
        ayt::io::File::readAllBytes(sourcePath);
    if (sourceBytes.empty()
        || !source.loadFromBinary(sourceBytes.data(), sourceBytes.size())) {
        error = "Unable to load animation dependency: " + sourcePath;
        return false;
    }
    ayt::resource::Animation baked;
    const HumanoidRetargetResult result = retargetHumanoidAnimation(
        sourceSkeleton, targetSkeleton, definition, source, baked);
    if (!result) {
        error = "Unable to retarget animation dependency (";
        error += humanoidRetargetErrorName(result.error);
        error += "): " + sourcePath;
        if (!result.message.empty()) error += " - " + result.message;
        return false;
    }
    baked.setGuid(source.getGuid());
    if (!baked.saveToBinary(output)) {
        error = "Unable to serialize retargeted animation: " + sourcePath;
        return false;
    }
    return true;
}

void executeBake(const std::shared_ptr<RunState>& run,
                 SkeletonBakeDryRunPlan plan,
                 std::string outputDirectory)
{
    run->sourceFingerprint = plan.sourceFingerprint;
    run->profileFingerprint = plan.profileFingerprint;
    if (!plan.canBake()) {
        fail(run, "Bake preflight contains blocking errors.");
        return;
    }
    if (outputDirectory.empty()) {
        fail(run, "Bake output directory is empty.");
        return;
    }
    for (const auto& dependency : plan.dependencies) {
        if (dependency.impact == SkeletonBakeDependencyImpact::Blocked) {
            fail(run, "A bake dependency is blocked: " + dependency.path);
            return;
        }
    }
    if (run->cancelled.load()) return;
    run->progress.store(0.1f);
    setDetails(run, "Loading source skeleton.");

    ayt::resource::Skeleton source;
    if (!source.load(plan.skeletonPath)) {
        fail(run, "Unable to load source skeleton: " + plan.skeletonPath);
        return;
    }
    const bool retarget = plan.outputMode == "BakeToTarget";
    ayt::resource::Skeleton target;
    std::unordered_map<std::string, const SkeletonBakeBoneOperation*> byName;
    std::vector<int> remap(source.getBoneCount(), -1);
    std::vector<ayt::math::UInt8> skeletonBytes;
    if (retarget) {
        if (!target.load(plan.targetSkeletonPath)) {
            fail(run, "Unable to load target skeleton: "
                + plan.targetSkeletonPath);
            return;
        }
        const HumanoidRetargetResult validation = validateHumanoidRetarget(
            source, target, plan.retargetDefinition);
        if (!validation) {
            fail(run, "Retarget definition is invalid: "
                + std::string(humanoidRetargetErrorName(validation.error)));
            return;
        }
        if (!target.saveToBinary(skeletonBytes)) {
            fail(run, "Unable to serialize target skeleton output.");
            return;
        }
    } else {
        if (plan.boneOperations.size() != source.getBoneCount()) {
            fail(run,
                "Bake plan no longer matches the source skeleton bone count.");
            return;
        }
        int nextIndex = 0;
        for (const auto& operation : plan.boneOperations) {
            if (operation.sourceBoneIndex < 0
                || operation.sourceBoneIndex
                    >= static_cast<int>(source.getBoneCount())) {
                fail(run, "Bake plan contains an invalid source bone index.");
                return;
            }
            if (operation.action != SkeletonBakeBoneAction::Delete) {
                remap[static_cast<std::size_t>(operation.sourceBoneIndex)] =
                    nextIndex++;
            }
        }
        ayt::resource::Skeleton baked;
        baked.setGuid(source.getGuid());
        const ayt::resource::Bone* sourceBones = source.getBones();
        for (const auto& operation : plan.boneOperations) {
            byName.emplace(operation.sourceName, &operation);
            if (operation.action == SkeletonBakeBoneAction::Delete) continue;
            ayt::resource::Bone bone = sourceBones[operation.sourceBoneIndex];
            if (!operation.targetName.empty()) bone.name = operation.targetName;
            int parent = bone.parentIndex;
            std::size_t guard = 0u;
            while (parent >= 0 && parent < static_cast<int>(remap.size())
                   && remap[static_cast<std::size_t>(parent)] < 0
                   && guard++ < remap.size()) {
                parent = sourceBones[parent].parentIndex;
            }
            bone.parentIndex = parent >= 0
                && parent < static_cast<int>(remap.size())
                ? remap[static_cast<std::size_t>(parent)] : -1;
            baked.addBone(bone);
        }
        if (!baked.saveToBinary(skeletonBytes)) {
            fail(run, "Unable to serialize baked skeleton.");
            return;
        }
    }
    if (run->cancelled.load()) return;
    run->progress.store(0.4f);
    setDetails(run, "Rewriting animation dependencies.");

    const std::filesystem::path outputRoot(outputDirectory);
    std::error_code directoryError;
    std::filesystem::create_directories(outputRoot, directoryError);
    if (directoryError) {
        fail(run, "Unable to create bake output directory.");
        return;
    }
    const std::string suffix = ".g" + std::to_string(run->generation) + ".tmp";
    std::vector<StagedFile> files;
    std::unordered_map<std::string, std::string> destinationOwners;
    const auto addStaged = [&](StagedFile::Kind kind,
                               const std::string& sourcePath,
                               const std::filesystem::path& destination,
                               const std::vector<ayt::math::UInt8>& bytes,
                               std::string& error) -> bool {
        const std::string key = normalizedPath(
            std::filesystem::absolute(destination));
        if (!destinationOwners.emplace(key, sourcePath).second) {
            error = "Multiple bake inputs resolve to the same output path: " + key;
            return false;
        }
        StagedFile file;
        file.kind = kind;
        file.sourcePath = sourcePath;
        file.destination = destination;
        file.temporary = destination.string() + suffix;
        if (!writeBytes(file.temporary, bytes)) {
            error = "Unable to write bake temporary file: "
                + normalizedPath(file.temporary);
            return false;
        }
        files.push_back(std::move(file));
        return true;
    };
    const auto cleanupTemps = [&files]() {
        std::vector<std::filesystem::path> paths;
        paths.reserve(files.size());
        for (const auto& file : files) paths.push_back(file.temporary);
        cleanupFiles(paths);
    };
    const std::string skeletonStem = std::filesystem::path(plan.skeletonPath).stem().string();
    const std::string scopeSuffix = plan.scopeTag.empty()
        ? std::string{} : "." + plan.scopeTag;
    const auto skeletonOutput = outputRoot
        / (skeletonStem + scopeSuffix + ".baked.ayskel");
    std::string stagingError;
    if (!addStaged(StagedFile::Kind::Skeleton, plan.skeletonPath,
                   skeletonOutput, skeletonBytes, stagingError)) {
        fail(run, std::move(stagingError));
        cleanupTemps();
        return;
    }

    const SkeletonBakeReferenceContext referenceContext{
        source,
        retarget ? &target : nullptr,
        plan.boneOperations,
        retarget ? &plan.retargetDefinition : nullptr,
        retarget,
    };
    Json artifacts = Json::array();
    artifacts.push_back({
        {"kind", "skeleton"},
        {"source", normalizedPath(std::filesystem::absolute(plan.skeletonPath))},
        {"output", normalizedPath(std::filesystem::absolute(skeletonOutput))},
    });
    for (const auto& dependency : plan.dependencies) {
        if (run->cancelled.load()) {
            cleanupTemps();
            return;
        }
        std::vector<ayt::math::UInt8> dependencyBytes;
        std::string error;
        bool rewritten = false;
        StagedFile::Kind stagedKind = StagedFile::Kind::Animation;
        std::string extension;
        switch (dependency.kind) {
        case SkeletonBakeDependencyKind::Animation:
            rewritten = retarget
                ? rewriteRetargetAnimation(dependency.path, source, target,
                    plan.retargetDefinition, dependencyBytes, error)
                : rewriteAnimation(
                    dependency.path, byName, dependencyBytes, error);
            stagedKind = StagedFile::Kind::Animation;
            extension = ".ayanm";
            break;
        case SkeletonBakeDependencyKind::Mesh:
            rewritten = rewriteMeshDependency(
                dependency.path, referenceContext, dependencyBytes, error);
            stagedKind = StagedFile::Kind::Mesh;
            extension = ".aymesh";
            break;
        case SkeletonBakeDependencyKind::SkeletonMask:
            rewritten = rewriteSkeletonMaskDependency(
                dependency.path, referenceContext, dependencyBytes, error);
            stagedKind = StagedFile::Kind::SkeletonMask;
            extension = ".aymask";
            break;
        }
        if (!rewritten) {
            cleanupTemps();
            fail(run, std::move(error));
            return;
        }
        const auto sourcePath = std::filesystem::path(dependency.path);
        const auto output = outputRoot
            / (sourcePath.stem().string() + scopeSuffix + ".baked" + extension);
        if (!addStaged(stagedKind, dependency.path, output,
                       dependencyBytes, error)) {
            cleanupTemps();
            fail(run, std::move(error));
            return;
        }
        artifacts.push_back({
            {"kind", SkeletonEditorCore::bakeDependencyKindName(
                dependency.kind)},
            {"source", normalizedPath(
                std::filesystem::absolute(dependency.path))},
            {"output", normalizedPath(std::filesystem::absolute(output))},
        });
    }
    run->progress.store(0.75f);
    setDetails(run, "Writing bake result manifest.");

    std::vector<std::string> outputs;
    for (const auto& file : files) {
        outputs.push_back(normalizedPath(
            std::filesystem::absolute(file.destination)));
    }
    const auto receiptOutput = plan.receiptPath.empty()
        ? outputRoot / (skeletonStem + scopeSuffix + ".bake-result.json")
        : std::filesystem::path(plan.receiptPath);
    Json receipt = {
        {"type", "SkeletonBakeResult"},
        {"version", 2u},
        {"generation", run->generation},
        {"sourceFingerprint", plan.sourceFingerprint},
        {"profileFingerprint", plan.profileFingerprint},
        {"targetSkeleton", plan.targetSkeletonPath},
        {"outputMode", plan.outputMode},
        {"platform", plan.platform},
        {"scope", plan.scopeTag},
        {"outputs", outputs},
        {"artifacts", std::move(artifacts)},
        {"dryRun", Json::parse(SkeletonEditorCore::dryRunManifestJson(plan))},
    };
    StagedFile receiptFile;
    receiptFile.kind = StagedFile::Kind::Receipt;
    receiptFile.destination = receiptOutput;
    receiptFile.temporary = receiptOutput.string() + suffix;
    const std::string receiptKey = normalizedPath(
        std::filesystem::absolute(receiptOutput));
    if (!destinationOwners.emplace(receiptKey, "receipt").second
        || !ayt::io::File::writeAllText(receiptFile.temporary.string(),
                                        receipt.dump(2) + "\n")) {
        cleanupTemps();
        fail(run, "Unable to write bake result manifest temporary file.");
        return;
    }
    files.push_back(std::move(receiptFile));
    outputs.push_back(normalizedPath(std::filesystem::absolute(receiptOutput)));
    for (const StagedFile& file : files) {
        std::string validationError;
        if (!validateStagedFile(file, validationError)) {
            cleanupTemps();
            fail(run, std::move(validationError));
            return;
        }
    }
    if (run->cancelled.load()) {
        cleanupTemps();
        return;
    }
    run->progress.store(0.9f);
    RunState::Phase expected = RunState::Phase::Building;
    if (!run->phase.compare_exchange_strong(
            expected, RunState::Phase::Committing)) {
        cleanupTemps();
        return;
    }
    std::string commitError;
    if (!commitFilesTransaction(files, run->generation, commitError)) {
        cleanupTemps();
        fail(run, std::move(commitError));
        return;
    }
    setDetails(run, "Skeleton bake completed.", std::move(outputs));
    run->progress.store(1.0f);
    run->state.store(SkeletonBakeJobState::Succeeded);
    run->phase.store(RunState::Phase::Finished);
}

} // namespace

struct SkeletonBakeJob::Impl {
    mutable std::mutex mutex;
    std::uint64_t nextGeneration = 1u;
    std::shared_ptr<RunState> current;
    std::vector<std::future<void>> workers;
};

SkeletonBakeJob::SkeletonBakeJob() : _impl(std::make_unique<Impl>()) {}

SkeletonBakeJob::~SkeletonBakeJob()
{
    cancel();
    for (auto& worker : _impl->workers) {
        if (worker.valid()) worker.wait();
    }
}

std::uint64_t SkeletonBakeJob::start(SkeletonBakeDryRunPlan plan,
                                     std::string outputDirectory)
{
    std::lock_guard lock(_impl->mutex);
    if (_impl->current != nullptr) {
        RunState::Phase expected = RunState::Phase::Building;
        if (_impl->current->phase.compare_exchange_strong(
                expected, RunState::Phase::Cancelled)) {
            _impl->current->cancelled.store(true);
            _impl->current->state.store(SkeletonBakeJobState::Cancelled);
            setDetails(_impl->current, "Superseded by a newer bake generation.");
        }
    }
    auto run = std::make_shared<RunState>();
    run->generation = _impl->nextGeneration++;
    run->sourceFingerprint = plan.sourceFingerprint;
    run->profileFingerprint = plan.profileFingerprint;
    _impl->current = run;
    _impl->workers.push_back(std::async(std::launch::async,
        [run, plan = std::move(plan), output = std::move(outputDirectory)]() mutable {
            executeBake(run, std::move(plan), std::move(output));
            if (run->cancelled.load()
                && run->state.load() == SkeletonBakeJobState::Running) {
                run->state.store(SkeletonBakeJobState::Cancelled);
                setDetails(run, "Skeleton bake cancelled.");
            }
        }));
    return run->generation;
}

void SkeletonBakeJob::cancel()
{
    std::shared_ptr<RunState> run;
    {
        std::lock_guard lock(_impl->mutex);
        run = _impl->current;
    }
    if (run == nullptr || run->state.load() != SkeletonBakeJobState::Running) return;
    RunState::Phase expected = RunState::Phase::Building;
    if (!run->phase.compare_exchange_strong(
            expected, RunState::Phase::Cancelled)) {
        if (expected == RunState::Phase::Committing) {
            setDetails(run,
                "Bake output transaction is committing and cannot be cancelled safely.");
        }
        return;
    }
    run->cancelled.store(true);
    run->state.store(SkeletonBakeJobState::Cancelled);
    setDetails(run, "Skeleton bake cancelled.");
}

SkeletonBakeJobSnapshot SkeletonBakeJob::poll() const
{
    std::shared_ptr<RunState> run;
    {
        std::lock_guard lock(_impl->mutex);
        run = _impl->current;
    }
    if (run == nullptr) return {};
    SkeletonBakeJobSnapshot snapshot;
    snapshot.generation = run->generation;
    snapshot.state = run->state.load();
    snapshot.progress = run->progress.load();
    snapshot.sourceFingerprint = run->sourceFingerprint;
    snapshot.profileFingerprint = run->profileFingerprint;
    {
        std::lock_guard lock(run->detailsMutex);
        snapshot.message = run->message;
        snapshot.outputPaths = run->outputPaths;
    }
    return snapshot;
}

const char* SkeletonBakeJob::stateName(SkeletonBakeJobState state) noexcept
{
    switch (state) {
    case SkeletonBakeJobState::Idle: return "idle";
    case SkeletonBakeJobState::Running: return "running";
    case SkeletonBakeJobState::Succeeded: return "succeeded";
    case SkeletonBakeJobState::Failed: return "failed";
    case SkeletonBakeJobState::Cancelled: return "cancelled";
    }
    return "failed";
}

} // namespace ayt::anim::editor
