#pragma once

#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ayt::task { class ITaskScheduler; }

namespace ayt::anim::editor {

enum class SkeletonBakeJobState : std::uint8_t {
    Idle,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

struct SkeletonBakeJobSnapshot {
    std::uint64_t generation = 0u;
    SkeletonBakeJobState state = SkeletonBakeJobState::Idle;
    float progress = 0.0f;
    std::string message;
    std::string sourceFingerprint;
    std::string profileFingerprint;
    std::vector<std::string> outputPaths;
    std::size_t pendingWorkers = 0;
    bool cancellable = false;

    [[nodiscard]] bool finished() const noexcept {
        return state == SkeletonBakeJobState::Succeeded
            || state == SkeletonBakeJobState::Failed
            || state == SkeletonBakeJobState::Cancelled;
    }
};

// UI-free, generation-isolated async bake executor. Starting a newer job
// cancels building work; committing transactions finish before a newer writer.
// Canonically equivalent output roots share a process-local writer lane.
// poll() exposes only the newest generation and reaps completed owned workers.
// Cancellation is not drain: call drain before conflicting writes/scheduler
// shutdown. Destruction explicitly drains; no worker borrows this/page state.
class SkeletonBakeJob final {
public:
    SkeletonBakeJob();
    explicit SkeletonBakeJob(ayt::task::ITaskScheduler& scheduler);
    ~SkeletonBakeJob();
    SkeletonBakeJob(const SkeletonBakeJob&) = delete;
    SkeletonBakeJob& operator=(const SkeletonBakeJob&) = delete;

    std::uint64_t start(SkeletonBakeDryRunPlan plan,
                        std::string outputDirectory);
    void cancel();
    void drain();
    [[nodiscard]] SkeletonBakeJobSnapshot poll() const;

    static const char* stateName(SkeletonBakeJobState state) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace ayt::anim::editor
