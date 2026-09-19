#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace ayt::anim::editor {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kNoSavedCursor = (std::numeric_limits<std::size_t>::max)();

void setError(std::string* error, std::string message)
{
    if (error != nullptr) *error = std::move(message);
}

bool roleInRange(HumanoidBone role) noexcept
{
    return static_cast<std::size_t>(role) < kHumanoidBoneCount;
}

const char* validationErrorName(HumanoidValidationError error) noexcept
{
    switch (error) {
    case HumanoidValidationError::None: return "validated";
    case HumanoidValidationError::SourceBoneIndexOutOfRange:
        return "mapped bone index is outside the source skeleton";
    case HumanoidValidationError::DuplicateSourceBone:
        return "one source bone is assigned to multiple roles";
    case HumanoidValidationError::SourceParentIndexOutOfRange:
        return "source skeleton contains an invalid parent index";
    case HumanoidValidationError::SourceHierarchyCycle:
        return "source skeleton contains a hierarchy cycle";
    case HumanoidValidationError::MissingRequiredBone:
        return "required humanoid roles are missing";
    case HumanoidValidationError::SemanticParentMismatch:
        return "mapped humanoid ancestry does not match the source hierarchy";
    }
    return "mapping validation failed";
}

std::string normalizedPath(const std::filesystem::path& path)
{
    return path.lexically_normal().generic_string();
}

std::string lowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

std::string fnvHex(const char* prefix, const std::string& value)
{
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= prime;
    }
    std::ostringstream text;
    text << prefix << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

std::string stableProfileId(const std::filesystem::path& path)
{
    return fnvHex("rig-", normalizedPath(std::filesystem::absolute(path)));
}

} // namespace

SkeletonEditorCore::SkeletonEditorCore()
{
    _history.emplace_back();
}

SkeletonEditorCore::~SkeletonEditorCore() = default;

std::size_t SkeletonPreflightReport::errorCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        issues.begin(), issues.end(), [](const SkeletonPreflightIssue& issue) {
            return issue.severity == SkeletonPreflightSeverity::Error;
        }));
}

std::size_t SkeletonPreflightReport::warningCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        issues.begin(), issues.end(), [](const SkeletonPreflightIssue& issue) {
            return issue.severity == SkeletonPreflightSeverity::Warning;
        }));
}

std::size_t SkeletonBakeDryRunPlan::boneActionCount(
    SkeletonBakeBoneAction action) const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        boneOperations.begin(), boneOperations.end(),
        [action](const SkeletonBakeBoneOperation& operation) {
            return operation.action == action;
        }));
}

const SkeletonEditorCore::Snapshot& SkeletonEditorCore::current() const noexcept
{
    return _history[_historyCursor];
}

SkeletonEditorCore::Snapshot& SkeletonEditorCore::current() noexcept
{
    return _history[_historyCursor];
}

std::string SkeletonEditorCore::defaultMappingPath(const std::string& skeletonPath)
{
    std::filesystem::path path(skeletonPath);
    path.replace_extension(kRigProfileExtension);
    return normalizedPath(path);
}

std::string SkeletonEditorCore::defaultLegacyMappingPath(
    const std::string& skeletonPath)
{
    std::filesystem::path path(skeletonPath);
    path.replace_extension(kLegacySkeletonMappingExtension);
    return normalizedPath(path);
}

std::string SkeletonEditorCore::defaultDryRunManifestPath(
    const std::string& mappingPath)
{
    return normalizedPath(std::filesystem::path(mappingPath).concat(
        ".bake-plan.json"));
}

bool SkeletonEditorCore::open(const std::string& inputPath, std::string* error)
{
    if (inputPath.empty()) {
        setError(error, "Skeleton editor path is empty.");
        return false;
    }

    const std::filesystem::path requested(inputPath);
    const std::string extension = lowerExtension(requested);
    const bool mappingFirst = extension == kRigProfileExtension
        || extension == kLegacySkeletonMappingExtension;
    Snapshot loaded;
    std::string skeletonReference;
    std::string sourceFingerprint;
    std::string profileId;
    std::array<std::string, kHumanoidBoneCount> bonePaths{};
    std::filesystem::path loadedMappingPath;
    _legacyMappingPath.clear();
    if (mappingFirst) {
        loadedMappingPath = requested;
        if (!loadMappingFile(inputPath, skeletonReference, loaded,
                             sourceFingerprint, profileId, bonePaths, error)) {
            return false;
        }
        std::filesystem::path skeletonPath(skeletonReference);
        if (skeletonPath.is_relative()) {
            skeletonPath = requested.parent_path() / skeletonPath;
        }
        const std::filesystem::path absoluteRequested =
            std::filesystem::absolute(requested);
        if (extension == kLegacySkeletonMappingExtension) {
            _legacyMappingPath = normalizedPath(absoluteRequested);
            _mappingPath = defaultMappingPath(_legacyMappingPath);
        } else {
            _mappingPath = normalizedPath(absoluteRequested);
        }
        if (!loadSkeleton(normalizedPath(skeletonPath), error)) return false;
    } else {
        if (!loadSkeleton(inputPath, error)) return false;
        _mappingPath = defaultMappingPath(_skeletonPath);
        std::error_code existsError;
        if (std::filesystem::exists(_mappingPath, existsError)) {
            loadedMappingPath = _mappingPath;
            if (!loadMappingFile(_mappingPath, skeletonReference, loaded,
                                 sourceFingerprint, profileId, bonePaths, error)) {
                return false;
            }
        } else {
            const std::string legacyPath = defaultLegacyMappingPath(_skeletonPath);
            existsError.clear();
            if (std::filesystem::exists(legacyPath, existsError)) {
                loadedMappingPath = legacyPath;
                if (!loadMappingFile(legacyPath, skeletonReference, loaded,
                                     sourceFingerprint, profileId, bonePaths,
                                     error)) {
                    return false;
                }
                _legacyMappingPath = legacyPath;
            }
        }
    }

    if (!mappingFirst && !skeletonReference.empty()) {
        std::filesystem::path referencedSkeleton(skeletonReference);
        if (referencedSkeleton.is_relative()) {
            referencedSkeleton = loadedMappingPath.parent_path()
                / referencedSkeleton;
        }
        const std::string boundPath = normalizedPath(
            std::filesystem::absolute(referencedSkeleton));
        std::error_code equivalentError;
        const bool sameSkeleton = std::filesystem::equivalent(
            std::filesystem::path(boundPath),
            std::filesystem::path(_skeletonPath), equivalentError);
        if (equivalentError || !sameSkeleton) {
            setError(error,
                "RigProfile is bound to a different source skeleton: "
                    + boundPath);
            return false;
        }
    }

    resolveBonePaths(loaded, bonePaths);
    _profileId = profileId.empty() ? stableProfileId(_mappingPath) : profileId;
    loadBakeReceipt(loaded);

    _history.assign(1u, std::move(loaded));
    _historyCursor = 0u;
    _savedCursor = 0u;
    _loadedSourceFingerprint = std::move(sourceFingerprint);
    _selectedBone = _bones.empty() ? -1 : 0;
    _selectedRole = HumanoidBone::Hips;
    detachAnimation();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

bool SkeletonEditorCore::reload(std::string* error)
{
    std::error_code existsError;
    const bool mappingExists = !_mappingPath.empty()
        && std::filesystem::exists(_mappingPath, existsError);
    const std::string path = mappingExists ? _mappingPath : _skeletonPath;
    return open(path, error);
}

bool SkeletonEditorCore::loadSkeleton(const std::string& path, std::string* error)
{
    auto skeleton = std::make_shared<ayt::resource::Skeleton>();
    if (!skeleton->load(path)) {
        setError(error, "Unable to load skeleton resource: " + path);
        return false;
    }
    _skeleton = std::move(skeleton);
    _skeletonPath = normalizedPath(std::filesystem::absolute(path));
    rebuildBoneViews();
    rebuildBindPose();
    _player.setSkeleton(_skeleton);
    return true;
}

bool SkeletonEditorCore::loadMappingFile(
    const std::string& path, std::string& skeletonReference,
    Snapshot& snapshot, std::string& sourceFingerprint,
    std::string& profileId,
    std::array<std::string, kHumanoidBoneCount>& bonePaths,
    std::string* error) const
{
    const std::string text = ayt::io::File::readAllText(path);
    if (text.empty()) {
        setError(error, "Skeleton mapping is empty or unreadable: " + path);
        return false;
    }
    try {
        const Json root = Json::parse(text);
        const std::string type = root.value("type", std::string{});
        const std::uint32_t version = root.value("version", 0u);
        const bool legacy = type == "SkeletonMapping"
            && version == kLegacySkeletonMappingSchemaVersion;
        const bool rigProfile = type == "RigProfile"
            && version == kRigProfileSchemaVersion;
        if (!legacy && !rigProfile) {
            setError(error, "Unsupported skeleton mapping schema.");
            return false;
        }
        if (rigProfile && root.value("kind", std::string{}) != "mapping") {
            setError(error, "This skeleton editor only supports mapping RigProfiles.");
            return false;
        }
        if (rigProfile) {
            profileId = root.value("id", std::string{});
            const auto source = root.find("source");
            if (source == root.end() || !source->is_object()) {
                setError(error, "RigProfile does not contain a source object.");
                return false;
            }
            skeletonReference = source->value("skeleton", std::string{});
            sourceFingerprint = source->value("fingerprint", std::string{});
        } else {
            skeletonReference = root.value("skeleton", std::string{});
            sourceFingerprint = root.value("sourceFingerprint", std::string{});
        }
        if (skeletonReference.empty()) {
            setError(error, "Skeleton mapping does not reference a skeleton.");
            return false;
        }
        snapshot.nativeSkeleton = root.value("native", false);
        if (const auto roles = root.find("roles"); roles != root.end()
            && roles->is_object()) {
            for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                const auto value = roles->find(std::string(spec.canonicalName));
                if (value == roles->end()) continue;
                if (legacy && value->is_number_integer()) {
                    const int index = value->get<int>();
                    if (index >= 0) (void)snapshot.mapping.bind(spec.role, index);
                } else if (rigProfile && value->is_object()) {
                    const int index = value->value("sourceIndex", -1);
                    if (index >= 0) (void)snapshot.mapping.bind(spec.role, index);
                    bonePaths[static_cast<std::size_t>(spec.role)] =
                        value->value("bonePath", std::string{});
                    if (bonePaths[static_cast<std::size_t>(spec.role)].empty()) {
                        setError(error, "RigProfile role is missing bonePath: "
                            + std::string(spec.canonicalName));
                        return false;
                    }
                } else {
                    setError(error, "RigProfile role has an invalid value: "
                        + std::string(spec.canonicalName));
                    return false;
                }
            }
        }
    } catch (const std::exception& exception) {
        setError(error, std::string("Skeleton mapping parse failed: ")
            + exception.what());
        return false;
    }
    return true;
}

bool SkeletonEditorCore::writeMappingFile(
    const std::string& path, const Snapshot& snapshot, std::string* error) const
{
    if (_skeleton == nullptr || _skeletonPath.empty()) {
        setError(error, "No skeleton is open.");
        return false;
    }
    const std::filesystem::path destination(path);
    std::error_code relativeError;
    std::filesystem::path reference = std::filesystem::relative(
        _skeletonPath, destination.parent_path(), relativeError);
    if (relativeError) reference = _skeletonPath;

    Json roles = Json::object();
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int index = snapshot.mapping.getSourceBoneIndex(spec.role);
        if (index >= 0) {
            roles[std::string(spec.canonicalName)] = {
                {"bonePath", bonePath(index)},
                {"sourceIndex", index},
            };
        }
    }
    Json root = {
        {"type", "RigProfile"},
        {"version", kRigProfileSchemaVersion},
        {"id", _profileId.empty() ? stableProfileId(destination) : _profileId},
        {"kind", "mapping"},
        {"source", {
            {"skeleton", normalizedPath(reference)},
            {"fingerprint", skeletonFingerprint()},
        }},
        {"native", snapshot.nativeSkeleton},
        {"roles", std::move(roles)},
    };

    std::error_code directoryError;
    std::filesystem::create_directories(destination.parent_path(), directoryError);
    if (directoryError) {
        setError(error, "Unable to create skeleton mapping directory.");
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".tmp";
    if (!ayt::io::File::writeAllText(temporary.string(), root.dump(2) + "\n")) {
        setError(error, "Unable to write skeleton mapping temporary file.");
        return false;
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(destination, removeError);
        renameError.clear();
        std::filesystem::rename(temporary, destination, renameError);
    }
    if (renameError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        setError(error, "Unable to commit skeleton mapping resource.");
        return false;
    }
    return true;
}

bool SkeletonEditorCore::saveMapping(std::string* error)
{
    return saveMappingAs(_mappingPath.empty()
        ? defaultMappingPath(_skeletonPath) : _mappingPath, error);
}

bool SkeletonEditorCore::saveMappingAs(const std::string& path,
                                       std::string* error)
{
    if (path.empty()) {
        setError(error, "Skeleton mapping path is empty.");
        return false;
    }
    std::filesystem::path destination(path);
    const std::string extension = lowerExtension(destination);
    if (extension == kLegacySkeletonMappingExtension) {
        destination.replace_extension(kRigProfileExtension);
    } else if (extension != kRigProfileExtension) {
        setError(error, "RigProfile files must use the .ayrig extension.");
        return false;
    } else if (destination.extension().string() != kRigProfileExtension) {
        destination.replace_extension(kRigProfileExtension);
    }
    const std::string absolutePath = normalizedPath(
        std::filesystem::absolute(destination));
    if (!writeMappingFile(absolutePath, current(), error)) return false;
    _mappingPath = absolutePath;
    _legacyMappingPath.clear();
    if (_profileId.empty()) _profileId = stableProfileId(_mappingPath);
    _loadedSourceFingerprint = skeletonFingerprint();
    _savedCursor = _historyCursor;
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

void SkeletonEditorCore::commit(Snapshot next, bool invalidateReady)
{
    if (_historyCursor + 1u < _history.size()) {
        if (_savedCursor > _historyCursor) _savedCursor = kNoSavedCursor;
        _history.erase(_history.begin()
            + static_cast<std::ptrdiff_t>(_historyCursor + 1u), _history.end());
    }
    if (invalidateReady && next.bake == SkeletonBakeState::Ready) {
        next.bake = SkeletonBakeState::Stale;
    }
    _history.push_back(std::move(next));
    _historyCursor = _history.size() - 1u;
    ++_revision;
}

bool SkeletonEditorCore::selectBone(int boneIndex) noexcept
{
    if (boneIndex < -1 || boneIndex >= static_cast<int>(_bones.size())) return false;
    if (_selectedBone == boneIndex) return true;
    _selectedBone = boneIndex;
    ++_revision;
    return true;
}

bool SkeletonEditorCore::selectRole(HumanoidBone role) noexcept
{
    if (!roleInRange(role)) return false;
    if (_selectedRole == role) return true;
    _selectedRole = role;
    ++_revision;
    return true;
}

bool SkeletonEditorCore::bind(HumanoidBone role, int sourceBoneIndex)
{
    if (!roleInRange(role) || sourceBoneIndex < 0
        || sourceBoneIndex >= static_cast<int>(_bones.size())) return false;
    if (current().mapping.getSourceBoneIndex(role) == sourceBoneIndex) return true;
    Snapshot next = current();
    (void)next.mapping.bind(role, sourceBoneIndex);
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::unbind(HumanoidBone role)
{
    if (!roleInRange(role)) return false;
    if (!current().mapping.isBound(role)) return true;
    Snapshot next = current();
    (void)next.mapping.unbind(role);
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::clearMapping()
{
    if (current().mapping.empty()) return true;
    Snapshot next = current();
    next.mapping.clear();
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::applyCanonicalNameTemplate()
{
    if (_skeleton == nullptr) return false;
    Snapshot next = current();
    bool changed = false;
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const std::string name(spec.canonicalName);
        const int index = _skeleton->findBone(name.c_str());
        if (index >= 0 && next.mapping.getSourceBoneIndex(spec.role) != index) {
            (void)next.mapping.bind(spec.role, index);
            changed = true;
        }
    }
    if (changed) commit(std::move(next));
    return changed;
}

bool SkeletonEditorCore::setNative(bool nativeSkeleton)
{
    if (current().nativeSkeleton == nativeSkeleton) return true;
    Snapshot next = current();
    next.nativeSkeleton = nativeSkeleton;
    commit(std::move(next));
    return true;
}

HumanoidValidationResult SkeletonEditorCore::validation() const noexcept
{
    return _skeleton != nullptr
        ? validateHumanoidSkeleton(*_skeleton, current().mapping)
        : HumanoidValidationResult{HumanoidValidationError::MissingRequiredBone};
}

SkeletonAuthoringStatus SkeletonEditorCore::status() const
{
    SkeletonAuthoringStatus result;
    if (current().mapping.empty()) {
        result.adaptation = SkeletonAdaptationState::Unmapped;
        result.message = "No humanoid mapping is configured.";
    } else if (sourceMappingIsStale()) {
        result.adaptation = SkeletonAdaptationState::Incomplete;
        result.message = "The source skeleton changed after this mapping was saved.";
    } else {
        const HumanoidValidationResult checked = validation();
        if (!checked) {
            result.adaptation = SkeletonAdaptationState::Incomplete;
            result.message = validationErrorName(checked.error);
        } else {
            result.adaptation = current().nativeSkeleton
                ? SkeletonAdaptationState::Native
                : SkeletonAdaptationState::Validated;
            result.message = current().nativeSkeleton
                ? "Native AYHumanoid mapping is valid."
                : "Humanoid mapping is valid.";
        }
    }
    result.bake = current().bake;
    if (result.bake == SkeletonBakeState::Ready
        && current().bakedFingerprint != skeletonFingerprint()) {
        result.bake = SkeletonBakeState::Stale;
    }
    return result;
}

SkeletonPreflightReport SkeletonEditorCore::preflight(
    const std::vector<std::string>& animationPaths) const
{
    SkeletonPreflightReport report;
    const auto add = [&report](SkeletonPreflightCode code, std::string message,
                               std::string resourcePath = {},
                               HumanoidBone role = HumanoidBone::Invalid,
                               HumanoidBone related = HumanoidBone::Invalid,
                               int boneIndex = -1, int trackIndex = -1) {
        report.issues.push_back({SkeletonPreflightSeverity::Error, code,
            std::move(message), std::move(resourcePath), role, related,
            boneIndex, trackIndex});
    };

    if (_skeleton == nullptr) {
        add(SkeletonPreflightCode::NoSkeleton,
            "No source skeleton is open.");
        return report;
    }

    if (current().mapping.empty()) {
        add(SkeletonPreflightCode::MappingMissing,
            "No humanoid mapping is configured.", _mappingPath);
    }

    std::vector<int> mappedOwner(_bones.size(), -1);
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int mapped = current().mapping.getSourceBoneIndex(spec.role);
        if (mapped < 0) {
            if (spec.requirement == HumanoidBoneRequirement::Required) {
                add(SkeletonPreflightCode::RequiredRoleMissing,
                    "Required role is not mapped: "
                        + std::string(spec.canonicalName),
                    _mappingPath, spec.role);
            }
            continue;
        }
        if (mapped >= static_cast<int>(_bones.size())) {
            add(SkeletonPreflightCode::MappedBoneOutOfRange,
                "Mapped bone index is outside the source skeleton for role: "
                    + std::string(spec.canonicalName),
                _mappingPath, spec.role, HumanoidBone::Invalid, mapped);
            continue;
        }
        if (mappedOwner[static_cast<std::size_t>(mapped)] >= 0) {
            const HumanoidBone other = static_cast<HumanoidBone>(
                mappedOwner[static_cast<std::size_t>(mapped)]);
            add(SkeletonPreflightCode::DuplicateMappedBone,
                "Source bone is assigned to more than one humanoid role: "
                    + _bones[static_cast<std::size_t>(mapped)].name,
                _mappingPath, spec.role, other, mapped);
        } else {
            mappedOwner[static_cast<std::size_t>(mapped)] =
                static_cast<int>(spec.role);
        }
    }

    bool cycleReported = false;
    for (std::size_t index = 0; index < _bones.size(); ++index) {
        const int parent = _bones[index].parentIndex;
        if (parent < -1 || parent >= static_cast<int>(_bones.size())) {
            add(SkeletonPreflightCode::SourceParentOutOfRange,
                "Source bone has an invalid parent index: " + _bones[index].name,
                _skeletonPath, HumanoidBone::Invalid, HumanoidBone::Invalid,
                static_cast<int>(index));
            continue;
        }
        if (cycleReported) continue;
        std::vector<std::uint8_t> visited(_bones.size(), 0u);
        int cursor = static_cast<int>(index);
        while (cursor >= 0 && cursor < static_cast<int>(_bones.size())) {
            if (visited[static_cast<std::size_t>(cursor)] != 0u) {
                add(SkeletonPreflightCode::SourceHierarchyCycle,
                    "Source skeleton hierarchy contains a cycle at bone: "
                        + _bones[static_cast<std::size_t>(cursor)].name,
                    _skeletonPath, HumanoidBone::Invalid,
                    HumanoidBone::Invalid, cursor);
                cycleReported = true;
                break;
            }
            visited[static_cast<std::size_t>(cursor)] = 1u;
            cursor = _bones[static_cast<std::size_t>(cursor)].parentIndex;
        }
    }

    const HumanoidValidationResult semantic = validation();
    if (semantic.error == HumanoidValidationError::SemanticParentMismatch) {
        add(SkeletonPreflightCode::SemanticParentMismatch,
            "Mapped humanoid ancestry does not match the source hierarchy.",
            _mappingPath, semantic.role, semantic.relatedRole,
            semantic.sourceBoneIndex);
    }
    if (sourceMappingIsStale()) {
        add(SkeletonPreflightCode::SourceSkeletonChanged,
            "The source skeleton changed after the mapping was saved.",
            _skeletonPath);
    }

    const auto inspectAnimation = [this, &add](
        const ayt::resource::IAnimation& animation, const std::string& path) {
        for (std::uint32_t track = 0; track < animation.getTrackCount(); ++track) {
            if (animation.getTrackType(track) == ayt::resource::AnimTrackType::Float) {
                continue;
            }
            const char* rawName = animation.getTrackNodeName(track);
            const std::string name = rawName != nullptr ? rawName : "";
            if (name.empty() || _skeleton->findBone(name.c_str()) < 0) {
                add(SkeletonPreflightCode::AnimationTrackBoneMissing,
                    "Animation track targets a bone missing from the source skeleton: "
                        + (name.empty() ? std::string("<empty>") : name),
                    path, HumanoidBone::Invalid, HumanoidBone::Invalid,
                    -1, static_cast<int>(track));
            }
        }
    };

    std::unordered_set<std::string> visitedAnimations;
    if (_animation != nullptr) {
        visitedAnimations.insert(_animationPath);
        inspectAnimation(*_animation, _animationPath);
    }
    for (const std::string& path : animationPaths) {
        std::error_code absoluteError;
        const std::filesystem::path absolute = std::filesystem::absolute(
            path, absoluteError);
        const std::string normalized = normalizedPath(
            absoluteError ? std::filesystem::path(path) : absolute);
        if (!visitedAnimations.insert(normalized).second) continue;
        const std::vector<std::uint8_t> bytes = ayt::io::File::readAllBytes(path);
        ayt::resource::Animation animation;
        if (bytes.empty()
            || !animation.loadFromBinary(bytes.data(), bytes.size())) {
            add(SkeletonPreflightCode::AnimationUnreadable,
                "Animation is empty, unreadable, or invalid.", normalized);
            continue;
        }
        inspectAnimation(animation, normalized);
    }
    return report;
}

SkeletonBakeDryRunPlan SkeletonEditorCore::dryRunBake(
    const std::vector<std::string>& animationPaths,
    const std::vector<std::string>& meshPaths) const
{
    SkeletonBakeDryRunPlan plan;
    plan.skeletonPath = _skeletonPath;
    plan.mappingPath = _mappingPath;
    plan.sourceFingerprint = skeletonFingerprint();
    plan.profileFingerprint = rigProfileFingerprint();

    std::vector<std::string> animations = animationPaths;
    if (!_animationPath.empty()) animations.push_back(_animationPath);
    const auto normalizeUnique = [](std::vector<std::string>& paths) {
        for (std::string& path : paths) {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(path, error);
            path = normalizedPath(error ? std::filesystem::path(path) : absolute);
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    };
    normalizeUnique(animations);
    std::vector<std::string> meshes = meshPaths;
    normalizeUnique(meshes);
    plan.preflight = preflight(animations);

    std::vector<HumanoidBone> roleByBone(_bones.size(), HumanoidBone::Invalid);
    std::vector<std::uint8_t> retained(_bones.size(), 0u);
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int mapped = current().mapping.getSourceBoneIndex(spec.role);
        if (mapped < 0 || mapped >= static_cast<int>(_bones.size())) continue;
        if (roleByBone[static_cast<std::size_t>(mapped)] == HumanoidBone::Invalid) {
            roleByBone[static_cast<std::size_t>(mapped)] = spec.role;
        }
        int cursor = mapped;
        std::size_t guard = 0u;
        while (cursor >= 0 && cursor < static_cast<int>(_bones.size())
               && guard++ < _bones.size()) {
            retained[static_cast<std::size_t>(cursor)] = 1u;
            cursor = _bones[static_cast<std::size_t>(cursor)].parentIndex;
        }
    }

    plan.boneOperations.reserve(_bones.size());
    for (const SkeletonBoneView& bone : _bones) {
        SkeletonBakeBoneOperation operation;
        operation.sourceBoneIndex = bone.index;
        operation.sourceParentIndex = bone.parentIndex;
        operation.sourceName = bone.name;
        operation.targetName = bone.name;
        operation.role = roleByBone[static_cast<std::size_t>(bone.index)];
        if (operation.role != HumanoidBone::Invalid) {
            operation.targetName = std::string(getHumanoidBoneName(operation.role));
            if (operation.targetName.empty()) operation.targetName = bone.name;
            operation.action = operation.targetName == operation.sourceName
                ? SkeletonBakeBoneAction::Keep
                : SkeletonBakeBoneAction::Rename;
            operation.reason = operation.action == SkeletonBakeBoneAction::Rename
                ? "Mapped bone will use its canonical AYHumanoid name."
                : "Mapped AYHumanoid bone is already canonical.";
        } else if (retained[static_cast<std::size_t>(bone.index)] != 0u) {
            operation.action = SkeletonBakeBoneAction::Keep;
            operation.reason = "Unmapped ancestor is required to preserve mapped hierarchy.";
        } else {
            operation.action = SkeletonBakeBoneAction::Delete;
            operation.targetName.clear();
            operation.reason = "Unmapped bone is not required by the mapped hierarchy.";
        }
        plan.boneOperations.push_back(std::move(operation));
    }

    const auto dependencyBlocked = [&plan](const std::string& path) {
        return std::any_of(plan.preflight.issues.begin(), plan.preflight.issues.end(),
            [&path](const SkeletonPreflightIssue& issue) {
                return issue.severity == SkeletonPreflightSeverity::Error
                    && issue.resourcePath == path;
            });
    };
    for (const std::string& path : animations) {
        const bool blocked = dependencyBlocked(path);
        plan.dependencies.push_back({SkeletonBakeDependencyKind::Animation,
            blocked ? SkeletonBakeDependencyImpact::Blocked
                    : SkeletonBakeDependencyImpact::Affected,
            path, blocked ? "Animation failed preflight validation."
                          : "Animation tracks are affected by skeleton cleanup and renaming."});
    }
    for (const std::string& path : meshes) {
        plan.dependencies.push_back({SkeletonBakeDependencyKind::Mesh,
            SkeletonBakeDependencyImpact::RequiresVerification, path,
            "Mesh skin bindings must be rewritten and verified during bake."});
    }
    return plan;
}

std::string SkeletonEditorCore::dryRunManifestJson(
    const SkeletonBakeDryRunPlan& plan)
{
    Json operations = Json::array();
    for (const SkeletonBakeBoneOperation& operation : plan.boneOperations) {
        Json item = {
            {"action", bakeBoneActionName(operation.action)},
            {"sourceIndex", operation.sourceBoneIndex},
            {"sourceParentIndex", operation.sourceParentIndex},
            {"sourceName", operation.sourceName},
            {"targetName", operation.targetName},
            {"reason", operation.reason},
        };
        if (operation.role != HumanoidBone::Invalid) {
            item["role"] = getHumanoidBoneName(operation.role);
        }
        operations.push_back(std::move(item));
    }
    Json dependencies = Json::array();
    for (const SkeletonBakeDependency& dependency : plan.dependencies) {
        dependencies.push_back({
            {"kind", bakeDependencyKindName(dependency.kind)},
            {"impact", bakeDependencyImpactName(dependency.impact)},
            {"path", dependency.path},
            {"message", dependency.message},
        });
    }
    Json issues = Json::array();
    for (const SkeletonPreflightIssue& issue : plan.preflight.issues) {
        issues.push_back({
            {"severity", issue.severity == SkeletonPreflightSeverity::Error
                ? "error" : "warning"},
            {"code", preflightCodeName(issue.code)},
            {"message", issue.message},
            {"resource", issue.resourcePath},
            {"boneIndex", issue.boneIndex},
            {"trackIndex", issue.trackIndex},
        });
    }
    const Json root = {
        {"type", "SkeletonBakeDryRun"},
        {"version", plan.schemaVersion},
        {"source", {{"skeleton", plan.skeletonPath},
                    {"mapping", plan.mappingPath},
                    {"fingerprint", plan.sourceFingerprint},
                    {"profileFingerprint", plan.profileFingerprint}}},
        {"canBake", plan.canBake()},
        {"summary", {
            {"keep", plan.boneActionCount(SkeletonBakeBoneAction::Keep)},
            {"rename", plan.boneActionCount(SkeletonBakeBoneAction::Rename)},
            {"delete", plan.boneActionCount(SkeletonBakeBoneAction::Delete)},
            {"dependencies", plan.dependencies.size()},
            {"errors", plan.preflight.errorCount()},
            {"warnings", plan.preflight.warningCount()},
        }},
        {"bones", std::move(operations)},
        {"dependencies", std::move(dependencies)},
        {"preflight", std::move(issues)},
    };
    return root.dump(2) + "\n";
}

bool SkeletonEditorCore::writeDryRunManifest(
    const SkeletonBakeDryRunPlan& plan, const std::string& path,
    std::string* error) const
{
    if (path.empty()) {
        setError(error, "Bake dry-run manifest path is empty.");
        return false;
    }
    const std::filesystem::path destination(path);
    std::error_code directoryError;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), directoryError);
    }
    if (directoryError) {
        setError(error, "Unable to create bake dry-run manifest directory.");
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".tmp";
    if (!ayt::io::File::writeAllText(temporary.string(), dryRunManifestJson(plan))) {
        setError(error, "Unable to write bake dry-run manifest temporary file.");
        return false;
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(destination, removeError);
        renameError.clear();
        std::filesystem::rename(temporary, destination, renameError);
    }
    if (renameError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        setError(error, "Unable to commit bake dry-run manifest.");
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

bool SkeletonEditorCore::recordBakeResult(
    bool succeeded, const std::string& sourceFingerprint,
    const std::string& profileFingerprint, std::string* error)
{
    if (_skeleton == nullptr || sourceFingerprint.empty()
        || sourceFingerprint != skeletonFingerprint()) {
        setError(error,
            "Bake result was produced from a different source skeleton revision.");
        return false;
    }
    if (profileFingerprint.empty()
        || profileFingerprint != rigProfileFingerprint()) {
        setError(error,
            "Bake result was produced from a different RigProfile revision.");
        return false;
    }
    Snapshot next = current();
    next.bake = succeeded ? SkeletonBakeState::Ready : SkeletonBakeState::Failed;
    next.bakedFingerprint = succeeded ? sourceFingerprint : std::string{};
    commit(std::move(next), false);
    if (error != nullptr) error->clear();
    return true;
}

bool SkeletonEditorCore::isDirty() const noexcept
{
    return !_legacyMappingPath.empty()
        || _savedCursor == kNoSavedCursor || _historyCursor != _savedCursor;
}

bool SkeletonEditorCore::canUndo() const noexcept { return _historyCursor > 0u; }
bool SkeletonEditorCore::canRedo() const noexcept {
    return _historyCursor + 1u < _history.size();
}

bool SkeletonEditorCore::undo()
{
    if (!canUndo()) return false;
    --_historyCursor;
    ++_revision;
    return true;
}

bool SkeletonEditorCore::redo()
{
    if (!canRedo()) return false;
    ++_historyCursor;
    ++_revision;
    return true;
}

bool SkeletonEditorCore::attachAnimation(const std::string& path,
                                         std::string* error)
{
    if (_skeleton == nullptr) {
        setError(error, "Open a skeleton before attaching an animation.");
        return false;
    }
    const std::vector<std::uint8_t> bytes = ayt::io::File::readAllBytes(path);
    if (bytes.empty()) {
        setError(error, "Animation is empty or unreadable: " + path);
        return false;
    }
    auto animation = std::make_shared<ayt::resource::Animation>();
    if (!animation->loadFromBinary(bytes.data(), bytes.size())) {
        setError(error, "Unable to decode animation resource: " + path);
        return false;
    }
    _animation = std::move(animation);
    _animationPath = normalizedPath(std::filesystem::absolute(path));
    _player.setSkeleton(_skeleton);
    _player.play(_animation.get());
    _player.setLoop(true);
    _player.setTime(0.0f);
    _player.evaluate();
    _playing = false;
    rebuildPoseFromPlayer();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

void SkeletonEditorCore::detachAnimation()
{
    _player.stop();
    _animation.reset();
    _animationPath.clear();
    _playing = false;
    _poseWorld = _bindWorld;
    ++_poseRevision;
    ++_revision;
}

const ayt::resource::IAnimation* SkeletonEditorCore::animation() const noexcept
{
    return _animation.get();
}

bool SkeletonEditorCore::hasAnimation() const noexcept { return _animation != nullptr; }

void SkeletonEditorCore::play()
{
    if (_animation == nullptr) return;
    const float resumeTime = time();
    _player.play(_animation.get());
    _player.setTime(std::min(resumeTime, duration()));
    _playing = true;
}

void SkeletonEditorCore::pause()
{
    if (_animation == nullptr) return;
    _player.pause();
    _playing = false;
}

void SkeletonEditorCore::stop()
{
    if (_animation == nullptr) return;
    _player.stop();
    _player.play(_animation.get());
    _player.setTime(0.0f);
    _player.evaluate();
    _playing = false;
    rebuildPoseFromPlayer();
}

void SkeletonEditorCore::tick(float dt)
{
    if (!_playing || _animation == nullptr || dt <= 0.0f) return;
    _player.tick(dt);
    _player.evaluate();
    rebuildPoseFromPlayer();
}

bool SkeletonEditorCore::setTime(float seconds)
{
    if (_animation == nullptr) return false;
    _player.setTime(std::clamp(seconds, 0.0f, duration()));
    _player.evaluate();
    rebuildPoseFromPlayer();
    return true;
}

float SkeletonEditorCore::time() const noexcept { return _player.getTime(); }
float SkeletonEditorCore::duration() const noexcept {
    return _animation != nullptr ? _animation->getDuration() : 0.0f;
}

void SkeletonEditorCore::rebuildBoneViews()
{
    _bones.clear();
    if (_skeleton == nullptr) return;
    const std::size_t count = _skeleton->getBoneCount();
    const ayt::resource::Bone* bones = _skeleton->getBones();
    _bones.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        SkeletonBoneView view;
        view.index = static_cast<int>(index);
        view.parentIndex = bones[index].parentIndex;
        view.name = bones[index].name;
        view.localPosition = bones[index].localPosition;
        view.localRotation = bones[index].localRotation;
        view.localScale = bones[index].localScale;
        view.inverseBindMatrix = bones[index].inverseBindMatrix;
        int parent = view.parentIndex;
        std::size_t guard = 0u;
        while (parent >= 0 && static_cast<std::size_t>(parent) < count
               && guard++ < count) {
            ++view.depth;
            parent = bones[parent].parentIndex;
        }
        _bones.push_back(std::move(view));
    }
}

void SkeletonEditorCore::rebuildBindPose()
{
    _bindWorld.assign(_bones.size(), ayt::math::Float4x4::identity());
    std::vector<std::uint8_t> state(_bones.size(), 0u);
    const auto build = [&](auto&& self, std::size_t index) -> void {
        if (state[index] == 2u) return;
        if (state[index] == 1u) {
            _bindWorld[index] = ayt::math::Float4x4::identity();
            state[index] = 2u;
            return;
        }
        state[index] = 1u;
        const SkeletonBoneView& bone = _bones[index];
        const ayt::math::Float4x4 local = ayt::math::Float4x4::fromTRS(
            bone.localPosition, bone.localRotation, bone.localScale);
        if (bone.parentIndex >= 0
            && bone.parentIndex < static_cast<int>(_bones.size())) {
            self(self, static_cast<std::size_t>(bone.parentIndex));
            _bindWorld[index] = _bindWorld[bone.parentIndex] * local;
        } else {
            _bindWorld[index] = local;
        }
        state[index] = 2u;
    };
    for (std::size_t index = 0; index < _bones.size(); ++index) {
        build(build, index);
    }
    _poseWorld = _bindWorld;
    ++_poseRevision;
}

void SkeletonEditorCore::rebuildPoseFromPlayer()
{
    const ayt::math::Float4x4* world = _player.getBoneWorldMatrices();
    const std::size_t count = _player.getBoneCount();
    if (world == nullptr || count != _bones.size()) {
        _poseWorld = _bindWorld;
        ++_poseRevision;
        return;
    }
    _poseWorld.assign(world, world + count);
    ++_poseRevision;
}

std::string SkeletonEditorCore::bonePath(int boneIndex) const
{
    if (boneIndex < 0 || boneIndex >= static_cast<int>(_bones.size())) return {};
    std::vector<std::string> segments;
    std::vector<std::uint8_t> visited(_bones.size(), 0u);
    int cursor = boneIndex;
    while (cursor >= 0 && cursor < static_cast<int>(_bones.size())) {
        if (visited[static_cast<std::size_t>(cursor)] != 0u) return {};
        visited[static_cast<std::size_t>(cursor)] = 1u;
        segments.push_back(_bones[static_cast<std::size_t>(cursor)].name);
        cursor = _bones[static_cast<std::size_t>(cursor)].parentIndex;
    }
    std::reverse(segments.begin(), segments.end());
    std::string result;
    for (const std::string& segment : segments) {
        if (!result.empty()) result.push_back('/');
        result += segment;
    }
    return result;
}

void SkeletonEditorCore::resolveBonePaths(
    Snapshot& snapshot,
    const std::array<std::string, kHumanoidBoneCount>& bonePaths) const
{
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const std::size_t roleIndex = static_cast<std::size_t>(spec.role);
        const std::string& expected = bonePaths[roleIndex];
        if (expected.empty()) continue;
        const int storedIndex = snapshot.mapping.getSourceBoneIndex(spec.role);
        if (storedIndex >= 0 && bonePath(storedIndex) == expected) continue;

        int resolved = -1;
        for (std::size_t index = 0; index < _bones.size(); ++index) {
            if (bonePath(static_cast<int>(index)) != expected) continue;
            if (resolved >= 0) {
                resolved = -1;
                break;
            }
            resolved = static_cast<int>(index);
        }
        (void)snapshot.mapping.unbind(spec.role);
        if (resolved >= 0) (void)snapshot.mapping.bind(spec.role, resolved);
    }
}

void SkeletonEditorCore::loadBakeReceipt(Snapshot& snapshot) const
{
    snapshot.bake = SkeletonBakeState::NotBaked;
    snapshot.bakedFingerprint.clear();
    if (_skeletonPath.empty() || snapshot.mapping.empty()) return;

    const std::filesystem::path skeleton(_skeletonPath);
    const std::filesystem::path receipt = skeleton.parent_path() / "Baked"
        / (skeleton.stem().string() + ".bake-result.json");
    std::error_code existsError;
    if (!std::filesystem::exists(receipt, existsError) || existsError) return;

    try {
        const std::string text = ayt::io::File::readAllText(receipt.string());
        const Json root = Json::parse(text);
        if (root.value("type", std::string{}) != "SkeletonBakeResult"
            || root.value("version", 0u) != 1u) {
            snapshot.bake = SkeletonBakeState::Failed;
            return;
        }
        snapshot.bakedFingerprint = root.value(
            "sourceFingerprint", std::string{});
        if (snapshot.bakedFingerprint.empty()
            || snapshot.bakedFingerprint != skeletonFingerprint()) {
            snapshot.bake = SkeletonBakeState::Stale;
            return;
        }
        if (root.value("profileFingerprint", std::string{})
            != rigProfileFingerprint(snapshot)) {
            snapshot.bake = SkeletonBakeState::Stale;
            return;
        }
        const auto outputs = root.find("outputs");
        if (outputs == root.end() || !outputs->is_array()
            || outputs->empty()) {
            snapshot.bake = SkeletonBakeState::Failed;
            return;
        }
        for (const Json& output : *outputs) {
            if (!output.is_string()) {
                snapshot.bake = SkeletonBakeState::Failed;
                return;
            }
            std::filesystem::path outputPath(output.get<std::string>());
            if (outputPath.is_relative()) outputPath = receipt.parent_path() / outputPath;
            existsError.clear();
            if (!std::filesystem::exists(outputPath, existsError) || existsError) {
                snapshot.bake = SkeletonBakeState::Stale;
                return;
            }
        }
        snapshot.bake = SkeletonBakeState::Ready;
    } catch (...) {
        snapshot.bake = SkeletonBakeState::Failed;
    }
}

std::string SkeletonEditorCore::skeletonFingerprint() const
{
    if (_skeletonPath.empty()) return {};
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(_skeletonPath, error);
    if (error) return {};
    const auto modified = std::filesystem::last_write_time(_skeletonPath, error);
    if (error) return std::to_string(size);
    return std::to_string(size) + ":"
        + std::to_string(modified.time_since_epoch().count());
}

std::string SkeletonEditorCore::rigProfileFingerprint() const
{
    return rigProfileFingerprint(current());
}

std::string SkeletonEditorCore::rigProfileFingerprint(
    const Snapshot& snapshot) const
{
    std::string canonical = "v1|" + skeletonFingerprint()
        + "|native=" + (snapshot.nativeSkeleton ? "1" : "0");
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int index = snapshot.mapping.getSourceBoneIndex(spec.role);
        canonical += "|";
        canonical += spec.canonicalName;
        canonical += "=";
        canonical += index >= 0 ? bonePath(index) : "-";
    }
    return fnvHex("rig-input-", canonical);
}

bool SkeletonEditorCore::sourceMappingIsStale() const
{
    return !_loadedSourceFingerprint.empty()
        && _loadedSourceFingerprint != skeletonFingerprint();
}

SkeletonAuthoringStatus SkeletonEditorCore::inspectStatus(
    const std::string& skeletonPath) noexcept
{
    try {
        SkeletonEditorCore core;
        std::string error;
        if (!core.open(skeletonPath, &error)) {
            return {SkeletonAdaptationState::Incomplete,
                    SkeletonBakeState::Failed, std::move(error)};
        }
        return core.status();
    } catch (...) {
        return {SkeletonAdaptationState::Incomplete,
                SkeletonBakeState::Failed,
                "Skeleton authoring status could not be inspected."};
    }
}

const char* SkeletonEditorCore::adaptationStateName(
    SkeletonAdaptationState state) noexcept
{
    switch (state) {
    case SkeletonAdaptationState::Unmapped: return "unmapped";
    case SkeletonAdaptationState::Incomplete: return "incomplete";
    case SkeletonAdaptationState::Validated: return "validated";
    case SkeletonAdaptationState::Native: return "native";
    }
    return "unknown";
}

const char* SkeletonEditorCore::bakeStateName(SkeletonBakeState state) noexcept
{
    switch (state) {
    case SkeletonBakeState::NotBaked: return "notBaked";
    case SkeletonBakeState::Stale: return "stale";
    case SkeletonBakeState::Ready: return "ready";
    case SkeletonBakeState::Failed: return "failed";
    }
    return "notBaked";
}

const char* SkeletonEditorCore::preflightCodeName(
    SkeletonPreflightCode code) noexcept
{
    switch (code) {
    case SkeletonPreflightCode::NoSkeleton: return "noSkeleton";
    case SkeletonPreflightCode::MappingMissing: return "mappingMissing";
    case SkeletonPreflightCode::RequiredRoleMissing: return "requiredRoleMissing";
    case SkeletonPreflightCode::MappedBoneOutOfRange: return "mappedBoneOutOfRange";
    case SkeletonPreflightCode::DuplicateMappedBone: return "duplicateMappedBone";
    case SkeletonPreflightCode::SourceParentOutOfRange: return "sourceParentOutOfRange";
    case SkeletonPreflightCode::SourceHierarchyCycle: return "sourceHierarchyCycle";
    case SkeletonPreflightCode::SemanticParentMismatch: return "semanticParentMismatch";
    case SkeletonPreflightCode::SourceSkeletonChanged: return "sourceSkeletonChanged";
    case SkeletonPreflightCode::AnimationUnreadable: return "animationUnreadable";
    case SkeletonPreflightCode::AnimationTrackBoneMissing: return "animationTrackBoneMissing";
    }
    return "unknown";
}

const char* SkeletonEditorCore::bakeBoneActionName(
    SkeletonBakeBoneAction action) noexcept
{
    switch (action) {
    case SkeletonBakeBoneAction::Keep: return "keep";
    case SkeletonBakeBoneAction::Rename: return "rename";
    case SkeletonBakeBoneAction::Delete: return "delete";
    }
    return "keep";
}

const char* SkeletonEditorCore::bakeDependencyKindName(
    SkeletonBakeDependencyKind kind) noexcept
{
    switch (kind) {
    case SkeletonBakeDependencyKind::Animation: return "animation";
    case SkeletonBakeDependencyKind::Mesh: return "mesh";
    }
    return "animation";
}

const char* SkeletonEditorCore::bakeDependencyImpactName(
    SkeletonBakeDependencyImpact impact) noexcept
{
    switch (impact) {
    case SkeletonBakeDependencyImpact::Affected: return "affected";
    case SkeletonBakeDependencyImpact::RequiresVerification:
        return "requiresVerification";
    case SkeletonBakeDependencyImpact::Blocked: return "blocked";
    }
    return "blocked";
}

} // namespace ayt::anim::editor
