#pragma once

#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    [[nodiscard]] bool finished() const noexcept {
        return state == SkeletonBakeJobState::Succeeded
            || state == SkeletonBakeJobState::Failed
            || state == SkeletonBakeJobState::Cancelled;
    }
};

// UI-free, generation-isolated async bake executor. Starting a newer job
// cancels the previous generation; poll() only exposes the newest generation.
class SkeletonBakeJob final {
public:
    SkeletonBakeJob();
    ~SkeletonBakeJob();
    SkeletonBakeJob(const SkeletonBakeJob&) = delete;
    SkeletonBakeJob& operator=(const SkeletonBakeJob&) = delete;

    std::uint64_t start(SkeletonBakeDryRunPlan plan,
                        std::string outputDirectory);
    void cancel();
    [[nodiscard]] SkeletonBakeJobSnapshot poll() const;

    static const char* stateName(SkeletonBakeJobState state) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace ayt::anim::editor
