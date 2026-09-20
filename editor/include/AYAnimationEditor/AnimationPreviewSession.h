#pragma once

#include <AYAnimation/AnimationPlayer.h>
#include <AYMath/MathTypes.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ayt::resource {
class Animation;
class IAnimation;
class Mesh;
class Skeleton;
}

namespace ayt::anim::editor {

enum class AnimationPreviewMode : std::uint8_t {
    ModelAndSkeleton,
    ModelOnly,
    SkeletonOnly,
};

enum class AnimationPreviewDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class AnimationPreviewDiagnosticCode : std::uint8_t {
    SkeletonNotBound,
    MeshNotBound,
    MeshHasNoSkinWeights,
    AnimationTrackBoneMissing,
    MeshPaletteBoneOutOfRange,
};

struct AnimationPreviewDiagnostic {
    AnimationPreviewDiagnosticSeverity severity =
        AnimationPreviewDiagnosticSeverity::Info;
    AnimationPreviewDiagnosticCode code =
        AnimationPreviewDiagnosticCode::SkeletonNotBound;
    std::string message;
    int trackIndex = -1;
    int boneIndex = -1;
};

struct AnimationPreviewBone {
    int index = -1;
    int parentIndex = -1;
    int depth = 0;
    std::string name;
};

struct AnimationPreviewBindings {
    std::string skeletonPath;
    std::string meshPath;
    std::string materialPath;

    [[nodiscard]] bool empty() const noexcept {
        return skeletonPath.empty() && meshPath.empty() && materialPath.empty();
    }
};

// UI-free animation preview state shared by AYEditor and future tools. Cooked
// animation, skeleton and mesh resources stay read-only. The session owns only
// transient playback state and exposes both world-pose and skinning matrices.
class AnimationPreviewSession final {
public:
    AnimationPreviewSession();
    ~AnimationPreviewSession();
    AnimationPreviewSession(const AnimationPreviewSession&) = delete;
    AnimationPreviewSession& operator=(const AnimationPreviewSession&) = delete;

    bool openAnimation(const std::string& path, std::string* error = nullptr);
    bool reloadAnimation(std::string* error = nullptr);
    bool bindSkeleton(const std::string& path, std::string* error = nullptr);
    bool bindMesh(const std::string& path, std::string* error = nullptr);
    void unbindSkeleton();
    void unbindMesh();
    void setMaterialPath(std::string path);

    // Uses the converter's .aydep.json dependency graph when available, then
    // falls back to sibling-name matching. It never writes resource files.
    [[nodiscard]] AnimationPreviewBindings inferCompanionAssets() const;
    bool applyBindings(const AnimationPreviewBindings& bindings,
                       std::string* error = nullptr);

    [[nodiscard]] const std::string& animationPath() const noexcept {
        return _animationPath;
    }
    [[nodiscard]] const std::string& skeletonPath() const noexcept {
        return _bindings.skeletonPath;
    }
    [[nodiscard]] const std::string& meshPath() const noexcept {
        return _bindings.meshPath;
    }
    [[nodiscard]] const std::string& materialPath() const noexcept {
        return _bindings.materialPath;
    }
    [[nodiscard]] const AnimationPreviewBindings& bindings() const noexcept {
        return _bindings;
    }

    [[nodiscard]] const ayt::resource::IAnimation* animation() const noexcept;
    [[nodiscard]] std::shared_ptr<const ayt::resource::Skeleton>
        skeleton() const noexcept { return _skeleton; }
    [[nodiscard]] std::shared_ptr<const ayt::resource::Mesh>
        mesh() const noexcept { return _mesh; }
    [[nodiscard]] const std::vector<AnimationPreviewBone>& bones() const noexcept {
        return _bones;
    }

    void setPreviewMode(AnimationPreviewMode mode) noexcept;
    [[nodiscard]] AnimationPreviewMode requestedPreviewMode() const noexcept {
        return _requestedMode;
    }
    [[nodiscard]] AnimationPreviewMode effectivePreviewMode() const noexcept;
    [[nodiscard]] bool canPreviewSkeleton() const noexcept;
    [[nodiscard]] bool canPreviewModel() const noexcept;

    void play();
    void pause();
    void stop();
    void tick(float dt);
    bool setTime(float seconds);
    void setLooping(bool looping) noexcept;
    void setPlayRate(float rate) noexcept;
    [[nodiscard]] bool isPlaying() const noexcept { return _playing; }
    [[nodiscard]] bool looping() const noexcept { return _looping; }
    [[nodiscard]] float playRate() const noexcept { return _playRate; }
    [[nodiscard]] float time() const noexcept;
    [[nodiscard]] float duration() const noexcept;

    [[nodiscard]] const std::vector<ayt::math::Float4x4>&
        poseWorldMatrices() const noexcept { return _poseWorld; }
    [[nodiscard]] const std::vector<ayt::math::Float4x4>&
        skinMatrices() const noexcept { return _skin; }
    [[nodiscard]] const std::vector<AnimationPreviewDiagnostic>&
        diagnostics() const noexcept { return _diagnostics; }
    [[nodiscard]] std::size_t missingTrackCount() const noexcept;

    [[nodiscard]] std::uint64_t revision() const noexcept { return _revision; }
    [[nodiscard]] std::uint64_t poseRevision() const noexcept {
        return _poseRevision;
    }

    static const char* previewModeName(AnimationPreviewMode mode) noexcept;
    static const char* diagnosticCodeName(
        AnimationPreviewDiagnosticCode code) noexcept;

private:
    void bindPlayer();
    void rebuildBones();
    void rebuildBindPose();
    void rebuildPoseFromPlayer();
    void rebuildDiagnostics();
    void inferMaterialFromMesh();

    std::string _animationPath;
    AnimationPreviewBindings _bindings;
    std::shared_ptr<ayt::resource::Animation> _animation;
    std::shared_ptr<ayt::resource::Skeleton> _skeleton;
    std::shared_ptr<ayt::resource::Mesh> _mesh;
    AnimationPlayer _player;
    std::vector<AnimationPreviewBone> _bones;
    std::vector<ayt::math::Float4x4> _bindWorld;
    std::vector<ayt::math::Float4x4> _poseWorld;
    std::vector<ayt::math::Float4x4> _skin;
    std::vector<AnimationPreviewDiagnostic> _diagnostics;
    AnimationPreviewMode _requestedMode = AnimationPreviewMode::ModelAndSkeleton;
    bool _playing = false;
    bool _looping = true;
    float _playRate = 1.0f;
    std::uint64_t _revision = 1u;
    std::uint64_t _poseRevision = 1u;
};

} // namespace ayt::anim::editor
