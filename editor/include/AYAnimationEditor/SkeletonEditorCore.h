#pragma once

#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimation/HumanoidSkeleton.h>
#include <AYMath/MathTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ayt::resource {
class Animation;
class IAnimation;
class Skeleton;
}

namespace ayt::anim::editor {

inline constexpr std::uint32_t kRigProfileSchemaVersion = 1u;
inline constexpr std::uint32_t kLegacySkeletonMappingSchemaVersion = 1u;
inline constexpr std::uint32_t kSkeletonBakePlanSchemaVersion = 1u;
inline constexpr const char* kRigProfileExtension = ".ayrig";
inline constexpr const char* kLegacySkeletonMappingExtension = ".aysmap";

enum class SkeletonAdaptationState : std::uint8_t {
    Unmapped,
    Incomplete,
    Validated,
    Native,
};

enum class SkeletonBakeState : std::uint8_t {
    NotBaked,
    Stale,
    Ready,
    Failed,
};

enum class SkeletonPreflightSeverity : std::uint8_t {
    Warning,
    Error,
};

enum class SkeletonPreflightCode : std::uint8_t {
    NoSkeleton,
    MappingMissing,
    RequiredRoleMissing,
    MappedBoneOutOfRange,
    DuplicateMappedBone,
    SourceParentOutOfRange,
    SourceHierarchyCycle,
    SemanticParentMismatch,
    SourceSkeletonChanged,
    AnimationUnreadable,
    AnimationTrackBoneMissing,
};

struct SkeletonPreflightIssue {
    SkeletonPreflightSeverity severity = SkeletonPreflightSeverity::Error;
    SkeletonPreflightCode code = SkeletonPreflightCode::NoSkeleton;
    std::string message;
    std::string resourcePath;
    HumanoidBone role = HumanoidBone::Invalid;
    HumanoidBone relatedRole = HumanoidBone::Invalid;
    int boneIndex = -1;
    int trackIndex = -1;
};

struct SkeletonPreflightReport {
    std::vector<SkeletonPreflightIssue> issues;

    [[nodiscard]] std::size_t errorCount() const noexcept;
    [[nodiscard]] std::size_t warningCount() const noexcept;
    [[nodiscard]] bool canBake() const noexcept { return errorCount() == 0u; }
};

enum class SkeletonBakeBoneAction : std::uint8_t {
    Keep,
    Rename,
    Delete,
};

enum class SkeletonBakeDependencyKind : std::uint8_t {
    Animation,
    Mesh,
};

enum class SkeletonBakeDependencyImpact : std::uint8_t {
    Affected,
    RequiresVerification,
    Blocked,
};

struct SkeletonBakeBoneOperation {
    SkeletonBakeBoneAction action = SkeletonBakeBoneAction::Keep;
    int sourceBoneIndex = -1;
    int sourceParentIndex = -1;
    std::string sourceName;
    std::string targetName;
    HumanoidBone role = HumanoidBone::Invalid;
    std::string reason;
};

struct SkeletonBakeDependency {
    SkeletonBakeDependencyKind kind = SkeletonBakeDependencyKind::Animation;
    SkeletonBakeDependencyImpact impact = SkeletonBakeDependencyImpact::Affected;
    std::string path;
    std::string message;
};

struct SkeletonBakeDryRunPlan {
    std::uint32_t schemaVersion = kSkeletonBakePlanSchemaVersion;
    std::string skeletonPath;
    std::string mappingPath;
    std::string sourceFingerprint;
    std::string profileFingerprint;
    SkeletonPreflightReport preflight;
    std::vector<SkeletonBakeBoneOperation> boneOperations;
    std::vector<SkeletonBakeDependency> dependencies;

    [[nodiscard]] std::size_t boneActionCount(
        SkeletonBakeBoneAction action) const noexcept;
    [[nodiscard]] bool canBake() const noexcept { return preflight.canBake(); }
};

struct SkeletonAuthoringStatus {
    SkeletonAdaptationState adaptation = SkeletonAdaptationState::Unmapped;
    SkeletonBakeState bake = SkeletonBakeState::NotBaked;
    std::string message;
};

struct SkeletonBoneView {
    int index = -1;
    int parentIndex = -1;
    int depth = 0;
    std::string name;
    ayt::math::FVector3 localPosition{};
    ayt::math::FQuaternion localRotation{};
    ayt::math::FVector3 localScale{1.0f, 1.0f, 1.0f};
    ayt::math::Float4x4 inverseBindMatrix = ayt::math::Float4x4::identity();
};

// UI-free authoring model shared by AYEditor and future command-line tools.
// The source .ayskel is never modified: authored mapping data is stored in a
// .ayrig RigProfile. Legacy .aysmap files are read only and migrate to .ayrig
// on save without deleting the source file.
class SkeletonEditorCore final {
public:
    SkeletonEditorCore();
    ~SkeletonEditorCore();
    SkeletonEditorCore(const SkeletonEditorCore&) = delete;
    SkeletonEditorCore& operator=(const SkeletonEditorCore&) = delete;

    bool open(const std::string& skeletonOrMappingPath,
              std::string* error = nullptr);
    bool reload(std::string* error = nullptr);
    bool saveMapping(std::string* error = nullptr);
    bool saveMappingAs(const std::string& path,
                       std::string* error = nullptr);

    [[nodiscard]] const std::string& skeletonPath() const noexcept {
        return _skeletonPath;
    }
    [[nodiscard]] const std::string& mappingPath() const noexcept {
        return _mappingPath;
    }
    [[nodiscard]] const std::string& legacyMappingPath() const noexcept {
        return _legacyMappingPath;
    }
    [[nodiscard]] bool openedLegacyMapping() const noexcept {
        return !_legacyMappingPath.empty();
    }
    [[nodiscard]] const std::string& profileId() const noexcept {
        return _profileId;
    }
    [[nodiscard]] const std::vector<SkeletonBoneView>& bones() const noexcept {
        return _bones;
    }
    [[nodiscard]] std::shared_ptr<const ayt::resource::Skeleton>
        skeleton() const noexcept { return _skeleton; }

    [[nodiscard]] int selectedBone() const noexcept { return _selectedBone; }
    bool selectBone(int boneIndex) noexcept;

    [[nodiscard]] HumanoidBone selectedRole() const noexcept {
        return _selectedRole;
    }
    bool selectRole(HumanoidBone role) noexcept;

    [[nodiscard]] const HumanoidBoneMap& mapping() const noexcept {
        return current().mapping;
    }
    bool bind(HumanoidBone role, int sourceBoneIndex);
    bool unbind(HumanoidBone role);
    bool clearMapping();
    bool applyCanonicalNameTemplate();
    bool setNative(bool nativeSkeleton);
    [[nodiscard]] bool isNative() const noexcept { return current().nativeSkeleton; }

    [[nodiscard]] HumanoidValidationResult validation() const noexcept;
    [[nodiscard]] SkeletonAuthoringStatus status() const;
    [[nodiscard]] SkeletonPreflightReport preflight(
        const std::vector<std::string>& animationPaths = {}) const;
    [[nodiscard]] SkeletonBakeDryRunPlan dryRunBake(
        const std::vector<std::string>& animationPaths = {},
        const std::vector<std::string>& meshPaths = {}) const;
    bool writeDryRunManifest(const SkeletonBakeDryRunPlan& plan,
                             const std::string& path,
                             std::string* error = nullptr) const;
    bool recordBakeResult(bool succeeded,
                          const std::string& sourceFingerprint,
                          const std::string& profileFingerprint,
                          std::string* error = nullptr);
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    bool undo();
    bool redo();

    bool attachAnimation(const std::string& path,
                         std::string* error = nullptr);
    void detachAnimation();
    [[nodiscard]] const std::string& animationPath() const noexcept {
        return _animationPath;
    }
    [[nodiscard]] const ayt::resource::IAnimation* animation() const noexcept;
    [[nodiscard]] bool hasAnimation() const noexcept;
    void play();
    void pause();
    void stop();
    [[nodiscard]] bool isPlaying() const noexcept { return _playing; }
    void tick(float dt);
    bool setTime(float seconds);
    [[nodiscard]] float time() const noexcept;
    [[nodiscard]] float duration() const noexcept;

    // World-space pose used by the editor wireframe. Falls back to bind pose
    // when no clip is attached.
    [[nodiscard]] const std::vector<ayt::math::Float4x4>&
        poseWorldMatrices() const noexcept { return _poseWorld; }

    [[nodiscard]] std::uint64_t revision() const noexcept { return _revision; }

    static std::string defaultMappingPath(const std::string& skeletonPath);
    static std::string defaultLegacyMappingPath(const std::string& skeletonPath);
    static std::string defaultDryRunManifestPath(const std::string& mappingPath);
    static std::string dryRunManifestJson(const SkeletonBakeDryRunPlan& plan);
    static SkeletonAuthoringStatus inspectStatus(
        const std::string& skeletonPath) noexcept;
    static const char* adaptationStateName(SkeletonAdaptationState state) noexcept;
    static const char* bakeStateName(SkeletonBakeState state) noexcept;
    static const char* preflightCodeName(SkeletonPreflightCode code) noexcept;
    static const char* bakeBoneActionName(
        SkeletonBakeBoneAction action) noexcept;
    static const char* bakeDependencyKindName(
        SkeletonBakeDependencyKind kind) noexcept;
    static const char* bakeDependencyImpactName(
        SkeletonBakeDependencyImpact impact) noexcept;

private:
    struct Snapshot {
        HumanoidBoneMap mapping;
        bool nativeSkeleton = false;
        SkeletonBakeState bake = SkeletonBakeState::NotBaked;
        std::string bakedFingerprint;
    };

    [[nodiscard]] const Snapshot& current() const noexcept;
    [[nodiscard]] Snapshot& current() noexcept;
    void commit(Snapshot next, bool invalidateReady = true);
    bool loadSkeleton(const std::string& path, std::string* error);
    bool loadMappingFile(const std::string& path,
                         std::string& skeletonReference,
                         Snapshot& snapshot,
                         std::string& sourceFingerprint,
                         std::string& profileId,
                         std::array<std::string, kHumanoidBoneCount>& bonePaths,
                         std::string* error) const;
    bool writeMappingFile(const std::string& path,
                          const Snapshot& snapshot,
                          std::string* error) const;
    void rebuildBoneViews();
    void rebuildBindPose();
    void rebuildPoseFromPlayer();
    void resolveBonePaths(Snapshot& snapshot,
                          const std::array<std::string,
                              kHumanoidBoneCount>& bonePaths) const;
    void loadBakeReceipt(Snapshot& snapshot) const;
    [[nodiscard]] std::string bonePath(int boneIndex) const;
    [[nodiscard]] std::string skeletonFingerprint() const;
    [[nodiscard]] std::string rigProfileFingerprint() const;
    [[nodiscard]] std::string rigProfileFingerprint(
        const Snapshot& snapshot) const;
    [[nodiscard]] bool sourceMappingIsStale() const;

    std::string _skeletonPath;
    std::string _mappingPath;
    std::string _legacyMappingPath;
    std::string _profileId;
    std::string _loadedSourceFingerprint;
    std::shared_ptr<ayt::resource::Skeleton> _skeleton;
    std::vector<SkeletonBoneView> _bones;
    std::vector<ayt::math::Float4x4> _bindWorld;
    std::vector<ayt::math::Float4x4> _poseWorld;
    std::vector<Snapshot> _history;
    std::size_t _historyCursor = 0u;
    std::size_t _savedCursor = 0u;
    int _selectedBone = -1;
    HumanoidBone _selectedRole = HumanoidBone::Hips;

    std::string _animationPath;
    std::shared_ptr<ayt::resource::Animation> _animation;
    AnimationPlayer _player;
    bool _playing = false;
    std::uint64_t _revision = 1u;
};

} // namespace ayt::anim::editor
