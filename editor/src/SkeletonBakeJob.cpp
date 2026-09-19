#include <AYAnimationEditor/SkeletonBakeJob.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
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
    std::uint64_t generation = 0u;
    std::atomic<SkeletonBakeJobState> state{SkeletonBakeJobState::Running};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancelled{false};
    std::mutex detailsMutex;
    std::string message = "Preparing skeleton bake.";
    std::string sourceFingerprint;
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
}

bool writeBytes(const std::filesystem::path& path,
                const std::vector<ayt::math::UInt8>& bytes)
{
    return ayt::io::File::writeAllBytes(path.string(), bytes);
}

bool replaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) return true;
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    return !error;
}

void cleanupFiles(const std::vector<std::filesystem::path>& paths)
{
    for (const auto& path : paths) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
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

void executeBake(const std::shared_ptr<RunState>& run,
                 SkeletonBakeDryRunPlan plan,
                 std::string outputDirectory)
{
    run->sourceFingerprint = plan.sourceFingerprint;
    if (!plan.canBake()) {
        fail(run, "Bake preflight contains blocking errors.");
        return;
    }
    if (outputDirectory.empty()) {
        fail(run, "Bake output directory is empty.");
        return;
    }
    for (const auto& dependency : plan.dependencies) {
        if (dependency.kind == SkeletonBakeDependencyKind::Mesh) {
            fail(run, "Mesh skin-binding rewrite is not yet supported by the bake executor.");
            return;
        }
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
    if (plan.boneOperations.size() != source.getBoneCount()) {
        fail(run, "Bake plan no longer matches the source skeleton bone count.");
        return;
    }
    std::vector<int> remap(source.getBoneCount(), -1);
    int nextIndex = 0;
    for (const auto& operation : plan.boneOperations) {
        if (operation.sourceBoneIndex < 0
            || operation.sourceBoneIndex >= static_cast<int>(source.getBoneCount())) {
            fail(run, "Bake plan contains an invalid source bone index.");
            return;
        }
        if (operation.action != SkeletonBakeBoneAction::Delete) {
            remap[static_cast<std::size_t>(operation.sourceBoneIndex)] = nextIndex++;
        }
    }
    ayt::resource::Skeleton baked;
    baked.setGuid(source.getGuid());
    const ayt::resource::Bone* sourceBones = source.getBones();
    std::unordered_map<std::string, const SkeletonBakeBoneOperation*> byName;
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
        bone.parentIndex = parent >= 0 && parent < static_cast<int>(remap.size())
            ? remap[static_cast<std::size_t>(parent)] : -1;
        baked.addBone(bone);
    }
    std::vector<ayt::math::UInt8> skeletonBytes;
    if (!baked.saveToBinary(skeletonBytes)) {
        fail(run, "Unable to serialize baked skeleton.");
        return;
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
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
    const std::string skeletonStem = std::filesystem::path(plan.skeletonPath).stem().string();
    const auto skeletonOutput = outputRoot / (skeletonStem + ".baked.ayskel");
    files.emplace_back(skeletonOutput.string() + suffix, skeletonOutput);
    if (!writeBytes(files.back().first, skeletonBytes)) {
        fail(run, "Unable to write baked skeleton temporary file.");
        cleanupFiles({files.back().first});
        return;
    }

    for (const auto& dependency : plan.dependencies) {
        if (run->cancelled.load()) {
            std::vector<std::filesystem::path> temps;
            for (const auto& file : files) temps.push_back(file.first);
            cleanupFiles(temps);
            return;
        }
        std::vector<ayt::math::UInt8> animationBytes;
        std::string error;
        if (!rewriteAnimation(dependency.path, byName, animationBytes, error)) {
            std::vector<std::filesystem::path> temps;
            for (const auto& file : files) temps.push_back(file.first);
            cleanupFiles(temps);
            fail(run, std::move(error));
            return;
        }
        const auto sourcePath = std::filesystem::path(dependency.path);
        const auto output = outputRoot
            / (sourcePath.stem().string() + ".baked.ayanm");
        files.emplace_back(output.string() + suffix, output);
        if (!writeBytes(files.back().first, animationBytes)) {
            std::vector<std::filesystem::path> temps;
            for (const auto& file : files) temps.push_back(file.first);
            cleanupFiles(temps);
            fail(run, "Unable to write baked animation temporary file.");
            return;
        }
    }
    run->progress.store(0.75f);
    setDetails(run, "Writing bake result manifest.");

    std::vector<std::string> outputs;
    for (const auto& file : files) outputs.push_back(normalizedPath(file.second));
    const auto receiptOutput = outputRoot / (skeletonStem + ".bake-result.json");
    Json receipt = {
        {"type", "SkeletonBakeResult"},
        {"version", 1u},
        {"generation", run->generation},
        {"sourceFingerprint", plan.sourceFingerprint},
        {"outputs", outputs},
        {"dryRun", Json::parse(SkeletonEditorCore::dryRunManifestJson(plan))},
    };
    files.emplace_back(receiptOutput.string() + suffix, receiptOutput);
    if (!ayt::io::File::writeAllText(files.back().first.string(),
                                     receipt.dump(2) + "\n")) {
        std::vector<std::filesystem::path> temps;
        for (const auto& file : files) temps.push_back(file.first);
        cleanupFiles(temps);
        fail(run, "Unable to write bake result manifest temporary file.");
        return;
    }
    outputs.push_back(normalizedPath(receiptOutput));
    if (run->cancelled.load()) {
        std::vector<std::filesystem::path> temps;
        for (const auto& file : files) temps.push_back(file.first);
        cleanupFiles(temps);
        return;
    }
    run->progress.store(0.9f);
    for (const auto& file : files) {
        if (!replaceFile(file.first, file.second)) {
            fail(run, "Unable to atomically commit bake output: "
                + normalizedPath(file.second));
            return;
        }
    }
    if (run->cancelled.load()) return;
    setDetails(run, "Skeleton bake completed.", std::move(outputs));
    run->progress.store(1.0f);
    run->state.store(SkeletonBakeJobState::Succeeded);
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
        _impl->current->cancelled.store(true);
        if (_impl->current->state.load() == SkeletonBakeJobState::Running) {
            _impl->current->state.store(SkeletonBakeJobState::Cancelled);
            setDetails(_impl->current, "Superseded by a newer bake generation.");
        }
    }
    auto run = std::make_shared<RunState>();
    run->generation = _impl->nextGeneration++;
    run->sourceFingerprint = plan.sourceFingerprint;
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
