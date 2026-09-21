#include <AYAnimationEditor/SkeletonEditorCore.h>

#include "SkeletonBakeReferences.h"

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
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

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

RigProfileKind parseRigProfileKind(const std::string& value) noexcept
{
    if (value == "mapping") return RigProfileKind::Mapping;
    if (value == "retarget") return RigProfileKind::Retarget;
    if (value == "template") return RigProfileKind::Template;
    return RigProfileKind::Unknown;
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

Json quaternionJson(const ayt::math::FQuaternion& value)
{
    return Json::array({value.x, value.y, value.z, value.w});
}

bool parseQuaternion(const Json& value, ayt::math::FQuaternion& result)
{
    if (!value.is_array() || value.size() != 4u) return false;
    for (const Json& component : value) {
        if (!component.is_number()) return false;
    }
    result = {value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.y)
        || !std::isfinite(result.z) || !std::isfinite(result.w)
        || result.length() < 1.0e-4f) {
        return false;
    }
    result = result.normalize();
    return true;
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
    std::array<std::string, kHumanoidBoneCount> targetBonePaths{};
    bool targetRolesPresent = false;
    std::filesystem::path loadedMappingPath;
    _legacyMappingPath.clear();
    if (mappingFirst) {
        loadedMappingPath = requested;
        if (!loadMappingFile(inputPath, skeletonReference, loaded,
                             sourceFingerprint, profileId, bonePaths,
                             targetBonePaths, targetRolesPresent, error)) {
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
                                 sourceFingerprint, profileId, bonePaths,
                                 targetBonePaths, targetRolesPresent, error)) {
                return false;
            }
        } else {
            const std::string legacyPath = defaultLegacyMappingPath(_skeletonPath);
            existsError.clear();
            if (std::filesystem::exists(legacyPath, existsError)) {
                loadedMappingPath = legacyPath;
                if (!loadMappingFile(legacyPath, skeletonReference, loaded,
                                     sourceFingerprint, profileId, bonePaths,
                                     targetBonePaths, targetRolesPresent, error)) {
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
    if (loaded.profileKind == RigProfileKind::Retarget) {
        auto target = std::make_shared<ayt::resource::Skeleton>();
        if (!target->load(loaded.targetSkeletonPath)) {
            setError(error, "Unable to load retarget target skeleton: "
                + loaded.targetSkeletonPath);
            return false;
        }
        _targetSkeleton = std::move(target);
        _targetBones.clear();
        const ayt::resource::Bone* targetBones = _targetSkeleton->getBones();
        for (std::size_t index = 0; index < _targetSkeleton->getBoneCount(); ++index) {
            SkeletonBoneView view;
            view.index = static_cast<int>(index);
            view.parentIndex = targetBones[index].parentIndex;
            view.name = targetBones[index].name;
            view.localPosition = targetBones[index].localPosition;
            view.localRotation = targetBones[index].localRotation;
            view.localScale = targetBones[index].localScale;
            view.inverseBindMatrix = targetBones[index].inverseBindMatrix;
            int parent = view.parentIndex;
            std::size_t guard = 0u;
            while (parent >= 0
                && parent < static_cast<int>(_targetSkeleton->getBoneCount())
                && guard++ < _targetSkeleton->getBoneCount()) {
                ++view.depth;
                parent = targetBones[parent].parentIndex;
            }
            _targetBones.push_back(std::move(view));
        }
        if (!targetRolesPresent) {
            for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                const int index = _targetSkeleton->findBone(
                    spec.canonicalName.data());
                if (index >= 0) {
                    (void)loaded.targetMapping.bind(spec.role, index);
                }
            }
        }
        resolveTargetBonePaths(loaded, targetBonePaths);
    } else {
        _targetSkeleton.reset();
        _targetBones.clear();
    }
    _profileId = profileId.empty() ? stableProfileId(_mappingPath) : profileId;
    loadBakeReceipt(loaded);

    _history.assign(1u, std::move(loaded));
    _historyCursor = 0u;
    _savedCursor = 0u;
    syncTargetSkeleton();
    _loadedSourceFingerprint = std::move(sourceFingerprint);
    _selectedBone = _bones.empty() ? -1 : 0;
    _selectedRole = HumanoidBone::Hips;
    _bakeInProgress = false;
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
    std::array<std::string, kHumanoidBoneCount>& targetBonePaths,
    bool& targetRolesPresent,
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
        const RigProfileKind profileKind = rigProfile
            ? parseRigProfileKind(root.value("kind", std::string{}))
            : RigProfileKind::Mapping;
        if (profileKind != RigProfileKind::Mapping
            && profileKind != RigProfileKind::Retarget) {
            setError(error,
                "This skeleton editor supports mapping and retarget RigProfiles.");
            return false;
        }
        snapshot.profileKind = profileKind;
        if (rigProfile) {
            profileId = root.value("id", std::string{});
            const auto source = root.find("source");
            if (source == root.end() || !source->is_object()) {
                setError(error, "RigProfile does not contain a source object.");
                return false;
            }
            skeletonReference = source->value("skeleton", std::string{});
            sourceFingerprint = source->value("fingerprint", std::string{});
            if (profileKind == RigProfileKind::Retarget) {
                const auto target = root.find("target");
                if (target == root.end() || !target->is_object()) {
                    setError(error,
                        "Retarget RigProfile does not contain a target object.");
                    return false;
                }
                std::filesystem::path targetPath(
                    target->value("skeleton", std::string{}));
                if (targetPath.empty()) {
                    setError(error,
                        "Retarget RigProfile does not reference a target skeleton.");
                    return false;
                }
                if (targetPath.is_relative()) {
                    targetPath = std::filesystem::path(path).parent_path()
                        / targetPath;
                }
                snapshot.targetSkeletonPath = normalizedPath(
                    std::filesystem::absolute(targetPath));
                snapshot.targetFingerprint = target->value(
                    "fingerprint", std::string{});
                const auto output = root.find("output");
                if (output == root.end() || !output->is_object()) {
                    setError(error,
                        "Retarget RigProfile does not contain an output object.");
                    return false;
                }
                snapshot.outputMode = output->value(
                    "mode", std::string{"BakeToTarget"});
                snapshot.platform = output->value(
                    "platform", std::string{"default"});
                if (snapshot.outputMode != "BakeToTarget") {
                    setError(error,
                        "Retarget RigProfile output mode is unsupported: "
                            + snapshot.outputMode);
                    return false;
                }
                if (const auto targetRoles = target->find("roles");
                    targetRoles != target->end()) {
                    if (!targetRoles->is_object()) {
                        setError(error,
                            "Retarget target roles must be an object.");
                        return false;
                    }
                    targetRolesPresent = true;
                    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                        const auto value = targetRoles->find(
                            std::string(spec.canonicalName));
                        if (value == targetRoles->end()) continue;
                        if (!value->is_object()) {
                            setError(error,
                                "Retarget target role has an invalid value: "
                                    + std::string(spec.canonicalName));
                            return false;
                        }
                        const int index = value->value("targetIndex", -1);
                        if (index >= 0) {
                            (void)snapshot.targetMapping.bind(spec.role, index);
                        }
                        targetBonePaths[static_cast<std::size_t>(spec.role)] =
                            value->value("bonePath", std::string{});
                        if (targetBonePaths[static_cast<std::size_t>(spec.role)]
                            .empty()) {
                            setError(error,
                                "Retarget target role is missing bonePath: "
                                    + std::string(spec.canonicalName));
                            return false;
                        }
                    }
                }
                if (const auto corrections = root.find("corrections");
                    corrections != root.end() && corrections->is_object()) {
                    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                        const auto value = corrections->find(
                            std::string(spec.canonicalName));
                        if (value == corrections->end()) continue;
                        if (!value->is_object()) {
                            setError(error,
                                "Retarget correction has an invalid value: "
                                    + std::string(spec.canonicalName));
                            return false;
                        }
                        auto& correction = snapshot.corrections[
                            static_cast<std::size_t>(spec.role)];
                        const auto sourceOffset = value->find(
                            "sourceReferenceOffset");
                        const auto targetOffset = value->find(
                            "targetReferenceOffset");
                        const auto axis = value->find("axisCorrection");
                        if ((sourceOffset != value->end()
                                && !parseQuaternion(*sourceOffset,
                                    correction.sourceReferenceOffset))
                            || (targetOffset != value->end()
                                && !parseQuaternion(*targetOffset,
                                    correction.targetReferenceOffset))
                            || (axis != value->end()
                                && !parseQuaternion(*axis,
                                    correction.axisCorrection))) {
                            setError(error,
                                "Retarget correction contains an invalid quaternion: "
                                    + std::string(spec.canonicalName));
                            return false;
                        }
                    }
                }
            }
        } else {
            skeletonReference = root.value("skeleton", std::string{});
            sourceFingerprint = root.value("sourceFingerprint", std::string{});
        }
        if (skeletonReference.empty()) {
            setError(error, "Skeleton mapping does not reference a skeleton.");
            return false;
        }
        snapshot.nativeSkeleton = root.value("native", false)
            || root.value("strategy", std::string{}) == "native";
        snapshot.notApplicable = root.value("strategy", std::string{}) == "custom";
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
        {"kind", rigProfileKindName(snapshot.profileKind)},
        {"source", {
            {"skeleton", normalizedPath(reference)},
            {"fingerprint", skeletonFingerprint()},
        }},
        {"native", snapshot.nativeSkeleton},
        {"strategy", snapshot.notApplicable ? "custom"
            : snapshot.nativeSkeleton ? "native" : "humanoid"},
        {"roles", std::move(roles)},
    };
    if (snapshot.profileKind == RigProfileKind::Retarget) {
        std::error_code targetRelativeError;
        std::filesystem::path targetReference = std::filesystem::relative(
            snapshot.targetSkeletonPath, destination.parent_path(),
            targetRelativeError);
        if (targetRelativeError) targetReference = snapshot.targetSkeletonPath;
        Json targetRoles = Json::object();
        Json corrections = Json::object();
        for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
            const std::size_t roleIndex = static_cast<std::size_t>(spec.role);
            const int targetIndex = snapshot.targetMapping.getSourceBoneIndex(
                spec.role);
            if (targetIndex >= 0) {
                targetRoles[std::string(spec.canonicalName)] = {
                    {"bonePath", targetBonePath(targetIndex)},
                    {"targetIndex", targetIndex},
                };
            }
            const RetargetBoneCorrection& correction =
                snapshot.corrections[roleIndex];
            corrections[std::string(spec.canonicalName)] = {
                {"sourceReferenceOffset", quaternionJson(
                    correction.sourceReferenceOffset)},
                {"targetReferenceOffset", quaternionJson(
                    correction.targetReferenceOffset)},
                {"axisCorrection", quaternionJson(
                    correction.axisCorrection)},
            };
        }
        root["target"] = {
            {"skeleton", normalizedPath(targetReference)},
            {"fingerprint", fileFingerprint(snapshot.targetSkeletonPath)},
            {"roles", std::move(targetRoles)},
        };
        root["output"] = {
            {"mode", snapshot.outputMode.empty()
                ? "BakeToTarget" : snapshot.outputMode},
            {"platform", snapshot.platform.empty()
                ? "default" : snapshot.platform},
        };
        root["corrections"] = std::move(corrections);
    }

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
    if (invalidateReady && next.bake == SkeletonBakeState::Current) {
        next.bake = SkeletonBakeState::Stale;
    }
    _history.push_back(std::move(next));
    _historyCursor = _history.size() - 1u;
    syncTargetSkeleton();
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

bool SkeletonEditorCore::previewRigTemplate(
    const std::string& path, SkeletonTemplateApplyReport* report,
    std::string* error)
{
    return evaluateRigTemplate(path, report, false, error);
}

bool SkeletonEditorCore::applyRigTemplate(
    const std::string& path, SkeletonTemplateApplyReport* report,
    std::string* error)
{
    return evaluateRigTemplate(path, report, true, error);
}

bool SkeletonEditorCore::evaluateRigTemplate(
    const std::string& path, SkeletonTemplateApplyReport* report,
    bool apply, std::string* error)
{
    SkeletonTemplateApplyReport result;
    result.templatePath = normalizedPath(std::filesystem::absolute(path));
    if (_skeleton == nullptr) {
        setError(error, "No skeleton is open.");
        if (report != nullptr) *report = std::move(result);
        return false;
    }

    try {
        const std::string text = ayt::io::File::readAllText(path);
        if (text.empty()) {
            setError(error, "RigProfile template is empty or unreadable: " + path);
            if (report != nullptr) *report = std::move(result);
            return false;
        }
        const Json root = Json::parse(text);
        if (root.value("type", std::string{}) != "RigProfile"
            || root.value("version", 0u) != kRigProfileSchemaVersion
            || root.value("kind", std::string{}) != "template") {
            setError(error, "Selected .ayrig is not a supported template profile.");
            if (report != nullptr) *report = std::move(result);
            return false;
        }
        result.templateName = root.value("name",
            std::filesystem::path(path).stem().string());
        const auto roles = root.find("roles");
        if (roles == root.end() || !roles->is_object()) {
            setError(error, "RigProfile template does not contain a roles object.");
            if (report != nullptr) *report = std::move(result);
            return false;
        }

        Snapshot next = current();
        for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
            const auto value = roles->find(std::string(spec.canonicalName));
            if (value == roles->end()) continue;
            if (next.mapping.isBound(spec.role)) {
                ++result.preservedCount;
                continue;
            }

            std::vector<std::string> candidates;
            std::string requestedPath;
            if (value->is_string()) {
                candidates.push_back(value->get<std::string>());
            } else if (value->is_object()) {
                requestedPath = value->value("bonePath", std::string{});
                const std::string sourceName = value->value(
                    "sourceName", std::string{});
                if (!sourceName.empty()) candidates.push_back(sourceName);
                const auto aliases = value->find("candidates");
                if (aliases != value->end() && aliases->is_array()) {
                    for (const auto& candidate : *aliases) {
                        if (candidate.is_string()) {
                            candidates.push_back(candidate.get<std::string>());
                        }
                    }
                }
            } else {
                ++result.missingCount;
                continue;
            }

            std::vector<int> matches;
            const std::string foldedPath = lowerAscii(requestedPath);
            for (const SkeletonBoneView& bone : _bones) {
                bool matched = !foldedPath.empty()
                    && lowerAscii(bonePath(bone.index)) == foldedPath;
                if (!matched) {
                    const std::string foldedName = lowerAscii(bone.name);
                    matched = std::any_of(candidates.begin(), candidates.end(),
                        [&foldedName](const std::string& candidate) {
                            return lowerAscii(candidate) == foldedName;
                        });
                }
                if (matched) matches.push_back(bone.index);
            }
            std::sort(matches.begin(), matches.end());
            matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
            if (matches.empty()) {
                ++result.missingCount;
                continue;
            }
            if (matches.size() != 1u) {
                ++result.ambiguousCount;
                continue;
            }
            const int matchedIndex = matches.front();
            bool alreadyOwned = false;
            for (const HumanoidBoneSpec& other : getHumanoidBoneSpecs()) {
                if (other.role != spec.role
                    && next.mapping.getSourceBoneIndex(other.role) == matchedIndex) {
                    alreadyOwned = true;
                    break;
                }
            }
            if (alreadyOwned) {
                ++result.ambiguousCount;
                continue;
            }
            (void)next.mapping.bind(spec.role, matchedIndex);
            ++result.appliedCount;
        }
        if (apply && result.changed()) commit(std::move(next));
        if (report != nullptr) *report = result;
        if (error != nullptr) error->clear();
        return true;
    } catch (const std::exception& exception) {
        setError(error, std::string("RigProfile template parse failed: ")
            + exception.what());
        if (report != nullptr) *report = std::move(result);
        return false;
    }
}

bool SkeletonEditorCore::setNative(bool nativeSkeleton)
{
    if (current().nativeSkeleton == nativeSkeleton
        && (!nativeSkeleton || !current().notApplicable)) return true;
    Snapshot next = current();
    next.nativeSkeleton = nativeSkeleton;
    if (nativeSkeleton) next.notApplicable = false;
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::setNotApplicable(bool notApplicable)
{
    if (current().notApplicable == notApplicable
        && (!notApplicable || !current().nativeSkeleton)) return true;
    Snapshot next = current();
    next.notApplicable = notApplicable;
    if (notApplicable) next.nativeSkeleton = false;
    commit(std::move(next));
    return true;
}

void SkeletonEditorCore::setBakeInProgress(bool baking) noexcept
{
    if (_bakeInProgress == baking) return;
    _bakeInProgress = baking;
    ++_revision;
}

bool SkeletonEditorCore::configureRetarget(
    const std::string& targetSkeletonPath, const std::string& platform,
    std::string* error)
{
    if (targetSkeletonPath.empty()) {
        setError(error, "Retarget target skeleton path is empty.");
        return false;
    }
    std::filesystem::path target(targetSkeletonPath);
    if (target.is_relative() && !_mappingPath.empty()) {
        target = std::filesystem::path(_mappingPath).parent_path() / target;
    }
    const std::string absolute = normalizedPath(
        std::filesystem::absolute(target));
    ayt::resource::Skeleton targetSkeleton;
    if (!targetSkeleton.load(absolute)) {
        setError(error, "Unable to load retarget target skeleton: " + absolute);
        return false;
    }
    Snapshot next = current();
    next.profileKind = RigProfileKind::Retarget;
    next.targetSkeletonPath = absolute;
    next.targetFingerprint = fileFingerprint(absolute);
    next.outputMode = "BakeToTarget";
    next.platform = platform.empty() ? "default" : platform;
    next.targetMapping.clear();
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int index = targetSkeleton.findBone(spec.canonicalName.data());
        if (index >= 0) (void)next.targetMapping.bind(spec.role, index);
    }
    commit(std::move(next));
    if (error != nullptr) error->clear();
    return true;
}

bool SkeletonEditorCore::clearRetarget()
{
    if (current().profileKind == RigProfileKind::Mapping
        && current().targetSkeletonPath.empty()) return true;
    Snapshot next = current();
    next.profileKind = RigProfileKind::Mapping;
    next.targetSkeletonPath.clear();
    next.targetFingerprint.clear();
    next.targetMapping.clear();
    next.corrections = {};
    next.outputMode = "SemanticNormalize";
    next.platform.clear();
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::bindTarget(HumanoidBone role, int targetBoneIndex)
{
    if (current().profileKind != RigProfileKind::Retarget
        || !roleInRange(role) || targetBoneIndex < 0
        || targetBoneIndex >= static_cast<int>(_targetBones.size())) {
        return false;
    }
    if (current().targetMapping.getSourceBoneIndex(role) == targetBoneIndex) {
        return true;
    }
    Snapshot next = current();
    (void)next.targetMapping.bind(role, targetBoneIndex);
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::unbindTarget(HumanoidBone role)
{
    if (current().profileKind != RigProfileKind::Retarget
        || !roleInRange(role)) return false;
    if (!current().targetMapping.isBound(role)) return true;
    Snapshot next = current();
    (void)next.targetMapping.unbind(role);
    commit(std::move(next));
    return true;
}

const RetargetBoneCorrection& SkeletonEditorCore::retargetCorrection(
    HumanoidBone role) const noexcept
{
    static const RetargetBoneCorrection identity{};
    return roleInRange(role)
        ? current().corrections[static_cast<std::size_t>(role)] : identity;
}

bool SkeletonEditorCore::setRetargetCorrection(
    HumanoidBone role, const RetargetBoneCorrection& correction)
{
    if (current().profileKind != RigProfileKind::Retarget
        || !roleInRange(role)) return false;
    const auto valid = [](const ayt::math::FQuaternion& value) {
        return std::isfinite(value.x) && std::isfinite(value.y)
            && std::isfinite(value.z) && std::isfinite(value.w)
            && value.length() >= 1.0e-4f;
    };
    if (!valid(correction.sourceReferenceOffset)
        || !valid(correction.targetReferenceOffset)
        || !valid(correction.axisCorrection)) return false;
    Snapshot next = current();
    auto& stored = next.corrections[static_cast<std::size_t>(role)];
    stored.sourceReferenceOffset =
        correction.sourceReferenceOffset.normalize();
    stored.targetReferenceOffset =
        correction.targetReferenceOffset.normalize();
    stored.axisCorrection = correction.axisCorrection.normalize();
    commit(std::move(next));
    return true;
}

bool SkeletonEditorCore::resetRetargetCorrections()
{
    if (current().profileKind != RigProfileKind::Retarget) return false;
    Snapshot next = current();
    next.corrections = {};
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
    if (current().notApplicable) {
        result.adaptation = SkeletonAdaptationState::NotApplicable;
        result.message = "Custom/non-humanoid skeleton; humanoid mapping is not applicable.";
    } else if (current().mapping.empty()) {
        result.adaptation = SkeletonAdaptationState::Unmapped;
        result.message = "No humanoid mapping is configured.";
    } else if (sourceMappingIsStale()) {
        result.adaptation = SkeletonAdaptationState::Invalid;
        result.message = "The source skeleton changed after this mapping was saved.";
    } else {
        const HumanoidValidationResult checked = validation();
        if (!checked) {
            result.adaptation = checked.error
                    == HumanoidValidationError::MissingRequiredBone
                ? SkeletonAdaptationState::Incomplete
                : SkeletonAdaptationState::Invalid;
            result.message = validationErrorName(checked.error);
        } else if (current().profileKind == RigProfileKind::Retarget
            && (current().targetSkeletonPath.empty()
                || fileFingerprint(current().targetSkeletonPath).empty())) {
            result.adaptation = SkeletonAdaptationState::Invalid;
            result.message = "The retarget target skeleton is missing or unreadable.";
        } else if (current().profileKind == RigProfileKind::Retarget
            && !current().targetFingerprint.empty()
            && current().targetFingerprint
                != fileFingerprint(current().targetSkeletonPath)) {
            result.adaptation = SkeletonAdaptationState::Invalid;
            result.message = "The retarget target skeleton changed after this profile was saved.";
        } else if (current().profileKind == RigProfileKind::Retarget) {
            const HumanoidValidationResult targetChecked =
                _targetSkeleton != nullptr
                ? validateHumanoidSkeleton(*_targetSkeleton,
                    current().targetMapping)
                : HumanoidValidationResult{
                    HumanoidValidationError::MissingRequiredBone};
            if (!targetChecked) {
                result.adaptation = targetChecked.error
                        == HumanoidValidationError::MissingRequiredBone
                    ? SkeletonAdaptationState::Incomplete
                    : SkeletonAdaptationState::Invalid;
                result.message = "Target mapping: ";
                result.message += validationErrorName(targetChecked.error);
            } else {
                result.adaptation = SkeletonAdaptationState::Validated;
                result.message = "Source and target humanoid mappings are valid.";
            }
        } else {
            result.adaptation = current().nativeSkeleton
                ? SkeletonAdaptationState::Native
                : SkeletonAdaptationState::Validated;
            result.message = current().profileKind == RigProfileKind::Retarget
                ? "Humanoid mapping and retarget target are valid."
                : current().nativeSkeleton
                ? "Native AYHumanoid mapping is valid."
                : "Humanoid mapping is valid.";
        }
    }
    result.bake = _bakeInProgress ? SkeletonBakeState::Baking : current().bake;
    if (result.bake == SkeletonBakeState::Current
        && (current().bakedFingerprint != skeletonFingerprint()
            || current().bakedProfileFingerprint
                != rigProfileFingerprint())) {
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
    if (current().profileKind == RigProfileKind::Retarget) {
        const std::string targetFingerprint = fileFingerprint(
            current().targetSkeletonPath);
        if (current().targetSkeletonPath.empty() || targetFingerprint.empty()) {
            add(SkeletonPreflightCode::TargetSkeletonMissing,
                "The retarget target skeleton is missing or unreadable.",
                current().targetSkeletonPath);
        } else if (!current().targetFingerprint.empty()
            && current().targetFingerprint != targetFingerprint) {
            add(SkeletonPreflightCode::TargetSkeletonChanged,
                "The retarget target skeleton changed after the profile was saved.",
                current().targetSkeletonPath);
        }
        std::vector<int> targetOwner(_targetBones.size(), -1);
        for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
            const int mapped = current().targetMapping.getSourceBoneIndex(
                spec.role);
            if (mapped < 0) {
                if (spec.requirement == HumanoidBoneRequirement::Required) {
                    add(SkeletonPreflightCode::TargetRequiredRoleMissing,
                        "Required target role is not mapped: "
                            + std::string(spec.canonicalName),
                        current().targetSkeletonPath, spec.role);
                }
                continue;
            }
            if (mapped >= static_cast<int>(_targetBones.size())) {
                add(SkeletonPreflightCode::TargetMappedBoneOutOfRange,
                    "Target mapped bone index is outside the target skeleton for role: "
                        + std::string(spec.canonicalName),
                    current().targetSkeletonPath, spec.role,
                    HumanoidBone::Invalid, mapped);
                continue;
            }
            if (targetOwner[static_cast<std::size_t>(mapped)] >= 0) {
                add(SkeletonPreflightCode::DuplicateTargetMappedBone,
                    "Target bone is assigned to more than one humanoid role: "
                        + _targetBones[static_cast<std::size_t>(mapped)].name,
                    current().targetSkeletonPath, spec.role,
                    static_cast<HumanoidBone>(
                        targetOwner[static_cast<std::size_t>(mapped)]), mapped);
            } else {
                targetOwner[static_cast<std::size_t>(mapped)] =
                    static_cast<int>(spec.role);
            }
        }
        if (_targetSkeleton != nullptr) {
            const HumanoidValidationResult targetValidation =
                validateHumanoidSkeleton(*_targetSkeleton,
                    current().targetMapping);
            if (!targetValidation
                && targetValidation.error
                    != HumanoidValidationError::MissingRequiredBone
                && targetValidation.error
                    != HumanoidValidationError::SourceBoneIndexOutOfRange
                && targetValidation.error
                    != HumanoidValidationError::DuplicateSourceBone) {
                add(SkeletonPreflightCode::TargetMappingInvalid,
                    std::string(
                        "Target humanoid mapping or hierarchy is invalid: ")
                        + validationErrorName(targetValidation.error),
                    current().targetSkeletonPath, targetValidation.role,
                    targetValidation.relatedRole,
                    targetValidation.sourceBoneIndex);
            }
        }
    }

    const auto inspectAnimation = [this, &add](
        const ayt::resource::IAnimation& animation, const std::string& path) {
        for (std::uint32_t track = 0; track < animation.getTrackCount(); ++track) {
            if (animation.getTrackType(track) == ayt::resource::AnimTrackType::Float) {
                continue;
            }
            const char* rawName = animation.getTrackNodeName(track);
            const std::string name = rawName != nullptr ? rawName : "";
            const int sourceBone = name.empty()
                ? -1 : _skeleton->findBone(name.c_str());
            if (sourceBone < 0) {
                add(SkeletonPreflightCode::AnimationTrackBoneMissing,
                    "Animation track targets a bone missing from the source skeleton: "
                        + (name.empty() ? std::string("<empty>") : name),
                    path, HumanoidBone::Invalid, HumanoidBone::Invalid,
                    -1, static_cast<int>(track));
                continue;
            }
            if (current().profileKind != RigProfileKind::Retarget) continue;
            if (animation.getTrackBlendMode(track)
                != ayt::resource::AnimBlendMode::Override) {
                add(SkeletonPreflightCode::RetargetAdditiveTrackUnsupported,
                    "Additive skeletal tracks require an explicit reference-pose bake.",
                    path, HumanoidBone::Invalid, HumanoidBone::Invalid,
                    sourceBone, static_cast<int>(track));
                continue;
            }
            HumanoidBone role = HumanoidBone::Invalid;
            for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                if (current().mapping.getSourceBoneIndex(spec.role)
                    == sourceBone) {
                    role = spec.role;
                    break;
                }
            }
            if (role == HumanoidBone::Invalid
                || !current().targetMapping.isBound(role)) {
                add(SkeletonPreflightCode::RetargetAnimationBoneUnmapped,
                    "Animated source bone has no mapped target role: " + name,
                    path, role, HumanoidBone::Invalid, sourceBone,
                    static_cast<int>(track));
                continue;
            }
            const auto type = animation.getTrackType(track);
            const char* rawProperty = animation.getTrackProperty(track);
            const std::string property = rawProperty != nullptr
                ? rawProperty : "";
            const bool supported =
                (type == ayt::resource::AnimTrackType::Quaternion
                    && property == "rotation")
                || (type == ayt::resource::AnimTrackType::Vector3
                    && (property == "position" || property == "scale"));
            if (!supported) {
                add(SkeletonPreflightCode::RetargetTrackUnsupported,
                    "Skeletal track has an unsupported property/type pair: "
                        + property,
                    path, role, HumanoidBone::Invalid, sourceBone,
                    static_cast<int>(track));
            } else if (type == ayt::resource::AnimTrackType::Quaternion
                && animation.getTrackInterpolation(track)
                    == ayt::resource::AnimInterpolation::CubicHermite
                && (animation.getTrackInTangents(track) != nullptr
                    || animation.getTrackOutTangents(track) != nullptr)) {
                add(SkeletonPreflightCode::RetargetTrackUnsupported,
                    "Cubic quaternion retarget requires tangent-space conversion.",
                    path, role, HumanoidBone::Invalid, sourceBone,
                    static_cast<int>(track));
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
    const std::vector<std::string>& meshPaths,
    const std::vector<std::string>& skeletonMaskPaths) const
{
    SkeletonBakeDryRunPlan plan;
    plan.skeletonPath = _skeletonPath;
    plan.mappingPath = _mappingPath;
    plan.sourceFingerprint = skeletonFingerprint();
    plan.profileFingerprint = rigProfileFingerprint();
    plan.targetSkeletonPath = current().targetSkeletonPath;
    plan.outputMode = current().outputMode;
    plan.platform = current().platform;
    plan.scopeTag = bakeScopeTag(current());
    plan.receiptPath = bakeReceiptPath(current());
    plan.retargetDefinition.sourceMapping = current().mapping;
    plan.retargetDefinition.targetMapping = current().targetMapping;
    plan.retargetDefinition.corrections = current().corrections;

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
    std::vector<std::string> masks = skeletonMaskPaths;
    normalizeUnique(masks);
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
    if (_skeleton == nullptr) {
        for (const std::string& path : meshes) {
            plan.dependencies.push_back({SkeletonBakeDependencyKind::Mesh,
                SkeletonBakeDependencyImpact::Blocked, path,
                "Open a source skeleton before validating mesh references."});
        }
        for (const std::string& path : masks) {
            plan.dependencies.push_back({SkeletonBakeDependencyKind::SkeletonMask,
                SkeletonBakeDependencyImpact::Blocked, path,
                "Open a source skeleton before validating mask references."});
        }
        return plan;
    }
    const SkeletonBakeReferenceContext referenceContext{
        *_skeleton,
        current().profileKind == RigProfileKind::Retarget
            ? _targetSkeleton.get() : nullptr,
        plan.boneOperations,
        current().profileKind == RigProfileKind::Retarget
            ? &plan.retargetDefinition : nullptr,
        current().profileKind == RigProfileKind::Retarget,
    };
    for (const std::string& path : meshes) {
        std::vector<ayt::math::UInt8> ignored;
        std::string validationError;
        const bool valid = rewriteMeshDependency(
            path, referenceContext, ignored, validationError);
        if (!valid) {
            SkeletonPreflightIssue issue;
            issue.severity = SkeletonPreflightSeverity::Error;
            issue.code = current().profileKind == RigProfileKind::Retarget
                    && validationError.find("different target skeleton")
                        != std::string::npos
                    ? SkeletonPreflightCode::RetargetMeshUnsupported
                    : validationError.find("Unable to load") != std::string::npos
                        ? SkeletonPreflightCode::MeshUnreadable
                        : SkeletonPreflightCode::MeshSkinBindingInvalid;
            issue.message = validationError;
            issue.resourcePath = path;
            plan.preflight.issues.push_back(std::move(issue));
        }
        plan.dependencies.push_back({SkeletonBakeDependencyKind::Mesh,
            valid ? SkeletonBakeDependencyImpact::Affected
                  : SkeletonBakeDependencyImpact::Blocked,
            path, valid
                ? "Mesh skin palettes and active joint indices will be rewritten."
                : validationError});
    }
    for (const std::string& path : masks) {
        std::vector<ayt::math::UInt8> ignored;
        std::string validationError;
        const bool valid = rewriteSkeletonMaskDependency(
            path, referenceContext, ignored, validationError);
        if (!valid) {
            SkeletonPreflightIssue issue;
            issue.severity = SkeletonPreflightSeverity::Error;
            issue.code = validationError.find("Unable to load")
                != std::string::npos
                ? SkeletonPreflightCode::SkeletonMaskUnreadable
                : SkeletonPreflightCode::SkeletonMaskBoneMissing;
            issue.message = validationError;
            issue.resourcePath = path;
            plan.preflight.issues.push_back(std::move(issue));
        }
        plan.dependencies.push_back({SkeletonBakeDependencyKind::SkeletonMask,
            valid ? SkeletonBakeDependencyImpact::Affected
                  : SkeletonBakeDependencyImpact::Blocked,
            path, valid
                ? "Skeleton-mask bone references will be rewritten."
                : validationError});
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
    Json retargetRoles = Json::array();
    if (plan.outputMode == "BakeToTarget") {
        for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
            const std::size_t roleIndex = static_cast<std::size_t>(spec.role);
            const int sourceIndex = plan.retargetDefinition.sourceMapping
                .getSourceBoneIndex(spec.role);
            const int targetIndex = plan.retargetDefinition.targetMapping
                .getSourceBoneIndex(spec.role);
            if (sourceIndex < 0 && targetIndex < 0) continue;
            const auto& correction =
                plan.retargetDefinition.corrections[roleIndex];
            retargetRoles.push_back({
                {"role", spec.canonicalName},
                {"sourceIndex", sourceIndex},
                {"targetIndex", targetIndex},
                {"sourceReferenceOffset", quaternionJson(
                    correction.sourceReferenceOffset)},
                {"targetReferenceOffset", quaternionJson(
                    correction.targetReferenceOffset)},
                {"axisCorrection", quaternionJson(
                    correction.axisCorrection)},
            });
        }
    }
    const Json root = {
        {"type", "SkeletonBakeDryRun"},
        {"version", plan.schemaVersion},
        {"source", {{"skeleton", plan.skeletonPath},
                    {"mapping", plan.mappingPath},
                    {"fingerprint", plan.sourceFingerprint},
                    {"profileFingerprint", plan.profileFingerprint}}},
        {"target", {{"skeleton", plan.targetSkeletonPath},
                    {"outputMode", plan.outputMode},
                    {"platform", plan.platform},
                    {"scope", plan.scopeTag},
                    {"receipt", plan.receiptPath}}},
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
        {"retargetRoles", std::move(retargetRoles)},
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
    next.bake = succeeded ? SkeletonBakeState::Current : SkeletonBakeState::Failed;
    next.bakedFingerprint = succeeded ? sourceFingerprint : std::string{};
    next.bakedProfileFingerprint = succeeded
        ? profileFingerprint : std::string{};
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
    syncTargetSkeleton();
    ++_revision;
    return true;
}

bool SkeletonEditorCore::redo()
{
    if (!canRedo()) return false;
    ++_historyCursor;
    syncTargetSkeleton();
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
    rebuildTargetPreviewAnimation();
    rebuildPoseFromPlayer();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

void SkeletonEditorCore::detachAnimation()
{
    _player.stop();
    _targetPlayer.stop();
    _animation.reset();
    _targetAnimation.reset();
    _animationPath.clear();
    _targetPreviewError.clear();
    _playing = false;
    _poseWorld = _bindWorld;
    _targetPoseWorld = _targetBindWorld;
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
    if (_targetAnimation != nullptr) {
        _targetPlayer.play(_targetAnimation.get());
        _targetPlayer.setTime(std::min(resumeTime, duration()));
    }
    _playing = true;
}

void SkeletonEditorCore::pause()
{
    if (_animation == nullptr) return;
    _player.pause();
    _targetPlayer.pause();
    _playing = false;
}

void SkeletonEditorCore::stop()
{
    if (_animation == nullptr) return;
    _player.stop();
    _player.play(_animation.get());
    _player.setTime(0.0f);
    _player.evaluate();
    if (_targetAnimation != nullptr) {
        _targetPlayer.stop();
        _targetPlayer.play(_targetAnimation.get());
        _targetPlayer.setTime(0.0f);
        _targetPlayer.evaluate();
    }
    _playing = false;
    rebuildPoseFromPlayer();
}

void SkeletonEditorCore::tick(float dt)
{
    if (!_playing || _animation == nullptr || dt <= 0.0f) return;
    _player.tick(dt);
    _player.evaluate();
    if (_targetAnimation != nullptr) {
        _targetPlayer.setTime(_player.getTime());
        _targetPlayer.evaluate();
    }
    rebuildPoseFromPlayer();
}

bool SkeletonEditorCore::setTime(float seconds)
{
    if (_animation == nullptr) return false;
    _player.setTime(std::clamp(seconds, 0.0f, duration()));
    _player.evaluate();
    if (_targetAnimation != nullptr) {
        _targetPlayer.setTime(_player.getTime());
        _targetPlayer.evaluate();
    }
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

void SkeletonEditorCore::rebuildTargetBindPose()
{
    _targetBindWorld.assign(
        _targetBones.size(), ayt::math::Float4x4::identity());
    std::vector<std::uint8_t> state(_targetBones.size(), 0u);
    const auto build = [&](auto&& self, std::size_t index) -> void {
        if (state[index] == 2u) return;
        if (state[index] == 1u) {
            _targetBindWorld[index] = ayt::math::Float4x4::identity();
            state[index] = 2u;
            return;
        }
        state[index] = 1u;
        const SkeletonBoneView& bone = _targetBones[index];
        const ayt::math::Float4x4 local = ayt::math::Float4x4::fromTRS(
            bone.localPosition, bone.localRotation, bone.localScale);
        if (bone.parentIndex >= 0
            && bone.parentIndex < static_cast<int>(_targetBones.size())) {
            self(self, static_cast<std::size_t>(bone.parentIndex));
            _targetBindWorld[index] =
                _targetBindWorld[static_cast<std::size_t>(bone.parentIndex)]
                * local;
        } else {
            _targetBindWorld[index] = local;
        }
        state[index] = 2u;
    };
    for (std::size_t index = 0; index < _targetBones.size(); ++index) {
        build(build, index);
    }
    _targetPoseWorld = _targetBindWorld;
}

void SkeletonEditorCore::rebuildTargetPreviewAnimation()
{
    _targetPlayer.stop();
    _targetAnimation.reset();
    _targetPreviewError.clear();
    _targetPoseWorld = _targetBindWorld;
    if (_animation == nullptr || _skeleton == nullptr
        || _targetSkeleton == nullptr
        || current().profileKind != RigProfileKind::Retarget) return;

    HumanoidRetargetDefinition definition;
    definition.sourceMapping = current().mapping;
    definition.targetMapping = current().targetMapping;
    definition.corrections = current().corrections;
    auto converted = std::make_shared<ayt::resource::Animation>();
    const HumanoidRetargetResult result = retargetHumanoidAnimation(
        *_skeleton, *_targetSkeleton, definition, *_animation, *converted);
    if (!result) {
        _targetPreviewError = humanoidRetargetErrorName(result.error);
        if (!result.message.empty()) {
            _targetPreviewError += ": " + result.message;
        }
        return;
    }
    _targetAnimation = std::move(converted);
    _targetPlayer.setSkeleton(_targetSkeleton);
    _targetPlayer.play(_targetAnimation.get());
    _targetPlayer.setLoop(true);
    _targetPlayer.setTime(_player.getTime());
    _targetPlayer.evaluate();
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
    if (_targetAnimation != nullptr) {
        const ayt::math::Float4x4* targetWorld =
            _targetPlayer.getBoneWorldMatrices();
        const std::size_t targetCount = _targetPlayer.getBoneCount();
        if (targetWorld != nullptr && targetCount == _targetBones.size()) {
            _targetPoseWorld.assign(targetWorld, targetWorld + targetCount);
        } else {
            _targetPoseWorld = _targetBindWorld;
        }
    } else {
        _targetPoseWorld = _targetBindWorld;
    }
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

std::string SkeletonEditorCore::targetBonePath(int boneIndex) const
{
    if (boneIndex < 0
        || boneIndex >= static_cast<int>(_targetBones.size())) return {};
    std::vector<std::string> segments;
    std::vector<std::uint8_t> visited(_targetBones.size(), 0u);
    int cursor = boneIndex;
    while (cursor >= 0 && cursor < static_cast<int>(_targetBones.size())) {
        if (visited[static_cast<std::size_t>(cursor)] != 0u) return {};
        visited[static_cast<std::size_t>(cursor)] = 1u;
        segments.push_back(_targetBones[static_cast<std::size_t>(cursor)].name);
        cursor = _targetBones[static_cast<std::size_t>(cursor)].parentIndex;
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

void SkeletonEditorCore::resolveTargetBonePaths(
    Snapshot& snapshot,
    const std::array<std::string, kHumanoidBoneCount>& bonePaths) const
{
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const std::string& expected = bonePaths[
            static_cast<std::size_t>(spec.role)];
        if (expected.empty()) continue;
        const int storedIndex = snapshot.targetMapping.getSourceBoneIndex(
            spec.role);
        if (storedIndex >= 0 && targetBonePath(storedIndex) == expected) continue;
        int resolved = -1;
        for (std::size_t index = 0; index < _targetBones.size(); ++index) {
            if (targetBonePath(static_cast<int>(index)) != expected) continue;
            if (resolved >= 0) {
                resolved = -1;
                break;
            }
            resolved = static_cast<int>(index);
        }
        (void)snapshot.targetMapping.unbind(spec.role);
        if (resolved >= 0) {
            (void)snapshot.targetMapping.bind(spec.role, resolved);
        }
    }
}

void SkeletonEditorCore::syncTargetSkeleton()
{
    if (current().profileKind != RigProfileKind::Retarget
        || current().targetSkeletonPath.empty()) {
        _targetPlayer.stop();
        _targetAnimation.reset();
        _targetSkeleton.reset();
        _targetBones.clear();
        _targetBindWorld.clear();
        _targetPoseWorld.clear();
        _targetPreviewError.clear();
        return;
    }
    const bool alreadyLoaded = _targetSkeleton != nullptr
        && normalizedPath(_targetSkeleton->getPath())
            == normalizedPath(current().targetSkeletonPath);
    if (!alreadyLoaded) {
        auto target = std::make_shared<ayt::resource::Skeleton>();
        if (!target->load(current().targetSkeletonPath)) {
            _targetPlayer.stop();
            _targetAnimation.reset();
            _targetSkeleton.reset();
            _targetBones.clear();
            _targetBindWorld.clear();
            _targetPoseWorld.clear();
            _targetPreviewError = "Unable to load target skeleton.";
            return;
        }
        _targetSkeleton = std::move(target);
        _targetBones.clear();
        const ayt::resource::Bone* bones = _targetSkeleton->getBones();
        _targetBones.reserve(_targetSkeleton->getBoneCount());
        for (std::size_t index = 0;
             index < _targetSkeleton->getBoneCount(); ++index) {
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
            while (parent >= 0
                && parent < static_cast<int>(_targetSkeleton->getBoneCount())
                && guard++ < _targetSkeleton->getBoneCount()) {
                ++view.depth;
                parent = bones[parent].parentIndex;
            }
            _targetBones.push_back(std::move(view));
        }
    }
    _targetPlayer.setSkeleton(_targetSkeleton);
    rebuildTargetBindPose();
    rebuildTargetPreviewAnimation();
    rebuildPoseFromPlayer();
}

void SkeletonEditorCore::loadBakeReceipt(Snapshot& snapshot) const
{
    snapshot.bake = SkeletonBakeState::NotBaked;
    snapshot.bakedFingerprint.clear();
    snapshot.bakedProfileFingerprint.clear();
    if (_skeletonPath.empty() || snapshot.mapping.empty()) return;

    const std::filesystem::path receipt(bakeReceiptPath(snapshot));
    std::error_code existsError;
    if (!std::filesystem::exists(receipt, existsError) || existsError) return;

    try {
        const std::string text = ayt::io::File::readAllText(receipt.string());
        const Json root = Json::parse(text);
        const std::uint32_t receiptVersion = root.value("version", 0u);
        if (root.value("type", std::string{}) != "SkeletonBakeResult"
            || (receiptVersion != 1u && receiptVersion != 2u)) {
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
        snapshot.bakedProfileFingerprint = root.value(
            "profileFingerprint", std::string{});
        if (snapshot.bakedProfileFingerprint
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
        snapshot.bake = SkeletonBakeState::Current;
    } catch (...) {
        snapshot.bake = SkeletonBakeState::Failed;
    }
}

std::string SkeletonEditorCore::skeletonFingerprint() const
{
    return fileFingerprint(_skeletonPath);
}

std::string SkeletonEditorCore::fileFingerprint(const std::string& path)
{
    if (path.empty()) return {};
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return {};
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return std::to_string(size);
    return std::to_string(size) + ":"
        + std::to_string(modified.time_since_epoch().count());
}

std::string SkeletonEditorCore::bakeScopeTag(const Snapshot& snapshot) const
{
    if (snapshot.profileKind != RigProfileKind::Retarget) return {};
    return fnvHex("rt-", normalizedPath(snapshot.targetSkeletonPath)
        + "|" + snapshot.outputMode + "|" + snapshot.platform);
}

std::string SkeletonEditorCore::bakeReceiptPath(const Snapshot& snapshot) const
{
    if (_skeletonPath.empty()) return {};
    const std::filesystem::path skeleton(_skeletonPath);
    const std::string scope = bakeScopeTag(snapshot);
    return normalizedPath(skeleton.parent_path() / "Baked"
        / (skeleton.stem().string()
            + (scope.empty() ? std::string{} : "." + scope)
            + ".bake-result.json"));
}

std::string SkeletonEditorCore::rigProfileFingerprint() const
{
    return rigProfileFingerprint(current());
}

std::string SkeletonEditorCore::rigProfileFingerprint(
    const Snapshot& snapshot) const
{
    std::string canonical = "v1|" + skeletonFingerprint()
        + "|native=" + (snapshot.nativeSkeleton ? "1" : "0")
        + "|custom=" + (snapshot.notApplicable ? "1" : "0")
        + "|kind=" + rigProfileKindName(snapshot.profileKind)
        + "|target=" + normalizedPath(snapshot.targetSkeletonPath)
        + "|targetFingerprint=" + fileFingerprint(snapshot.targetSkeletonPath)
        + "|outputMode=" + snapshot.outputMode
        + "|platform=" + snapshot.platform;
    for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
        const int index = snapshot.mapping.getSourceBoneIndex(spec.role);
        canonical += "|";
        canonical += spec.canonicalName;
        canonical += "=";
        canonical += index >= 0 ? bonePath(index) : "-";
        const int targetIndex = snapshot.targetMapping.getSourceBoneIndex(
            spec.role);
        canonical += "->";
        canonical += targetIndex >= 0 ? targetBonePath(targetIndex) : "-";
        const RetargetBoneCorrection& correction = snapshot.corrections[
            static_cast<std::size_t>(spec.role)];
        const auto appendQuaternion = [&canonical](
            const ayt::math::FQuaternion& value) {
            std::ostringstream encoded;
            encoded << std::setprecision(9) << value.x << "," << value.y
                << "," << value.z << "," << value.w;
            canonical += "|" + encoded.str();
        };
        appendQuaternion(correction.sourceReferenceOffset);
        appendQuaternion(correction.targetReferenceOffset);
        appendQuaternion(correction.axisCorrection);
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
            return {SkeletonAdaptationState::Invalid,
                    SkeletonBakeState::Failed, std::move(error)};
        }
        return core.status();
    } catch (...) {
        return {SkeletonAdaptationState::Invalid,
                SkeletonBakeState::Failed,
                "Skeleton authoring status could not be inspected."};
    }
}

bool SkeletonEditorCore::inspectRigProfile(
    const std::string& path, RigProfileInfo& info, std::string* error) noexcept
{
    info = {};
    try {
        const std::string text = ayt::io::File::readAllText(path);
        if (text.empty()) {
            setError(error, "RigProfile is empty or unreadable: " + path);
            return false;
        }
        const Json root = Json::parse(text);
        if (root.value("type", std::string{}) != "RigProfile"
            || root.value("version", 0u) != kRigProfileSchemaVersion) {
            setError(error, "Unsupported RigProfile schema.");
            return false;
        }
        info.path = normalizedPath(std::filesystem::absolute(path));
        info.id = root.value("id", std::string{});
        info.name = root.value("name",
            std::filesystem::path(path).stem().string());
        info.kind = parseRigProfileKind(root.value("kind", std::string{}));
        if (info.kind == RigProfileKind::Unknown) {
            setError(error, "RigProfile has an unsupported kind.");
            return false;
        }
        if (const auto source = root.find("source"); source != root.end()
            && source->is_object()) {
            std::filesystem::path skeleton(
                source->value("skeleton", std::string{}));
            if (!skeleton.empty() && skeleton.is_relative()) {
                skeleton = std::filesystem::path(path).parent_path() / skeleton;
            }
            if (!skeleton.empty()) {
                info.sourceSkeletonPath = normalizedPath(
                    std::filesystem::absolute(skeleton));
            }
        }
        if (const auto target = root.find("target"); target != root.end()
            && target->is_object()) {
            std::filesystem::path skeleton(
                target->value("skeleton", std::string{}));
            if (!skeleton.empty() && skeleton.is_relative()) {
                skeleton = std::filesystem::path(path).parent_path() / skeleton;
            }
            if (!skeleton.empty()) {
                info.targetSkeletonPath = normalizedPath(
                    std::filesystem::absolute(skeleton));
            }
        }
        if (const auto output = root.find("output"); output != root.end()
            && output->is_object()) {
            info.outputMode = output->value("mode", std::string{});
            info.platform = output->value("platform", std::string{});
        }
        if (error != nullptr) error->clear();
        return true;
    } catch (const std::exception& exception) {
        setError(error, std::string("RigProfile inspection failed: ")
            + exception.what());
        return false;
    } catch (...) {
        setError(error, "RigProfile inspection failed.");
        return false;
    }
}

const char* SkeletonEditorCore::rigProfileKindName(RigProfileKind kind) noexcept
{
    switch (kind) {
    case RigProfileKind::Mapping: return "mapping";
    case RigProfileKind::Retarget: return "retarget";
    case RigProfileKind::Template: return "template";
    case RigProfileKind::Unknown: break;
    }
    return "unknown";
}

const char* SkeletonEditorCore::adaptationStateName(
    SkeletonAdaptationState state) noexcept
{
    switch (state) {
    case SkeletonAdaptationState::Unmapped: return "unmapped";
    case SkeletonAdaptationState::Incomplete: return "incomplete";
    case SkeletonAdaptationState::Invalid: return "invalid";
    case SkeletonAdaptationState::Validated: return "validated";
    case SkeletonAdaptationState::Native: return "native";
    case SkeletonAdaptationState::NotApplicable: return "notApplicable";
    }
    return "unknown";
}

const char* SkeletonEditorCore::bakeStateName(SkeletonBakeState state) noexcept
{
    switch (state) {
    case SkeletonBakeState::NotBaked: return "notBaked";
    case SkeletonBakeState::Baking: return "baking";
    case SkeletonBakeState::Stale: return "stale";
    case SkeletonBakeState::Current: return "current";
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
    case SkeletonPreflightCode::TargetSkeletonMissing: return "targetSkeletonMissing";
    case SkeletonPreflightCode::TargetSkeletonChanged: return "targetSkeletonChanged";
    case SkeletonPreflightCode::TargetRequiredRoleMissing: return "targetRequiredRoleMissing";
    case SkeletonPreflightCode::TargetMappedBoneOutOfRange: return "targetMappedBoneOutOfRange";
    case SkeletonPreflightCode::DuplicateTargetMappedBone: return "duplicateTargetMappedBone";
    case SkeletonPreflightCode::TargetMappingInvalid: return "targetMappingInvalid";
    case SkeletonPreflightCode::RetargetSolverUnavailable: return "retargetSolverUnavailable";
    case SkeletonPreflightCode::RetargetAnimationBoneUnmapped: return "retargetAnimationBoneUnmapped";
    case SkeletonPreflightCode::RetargetTrackUnsupported: return "retargetTrackUnsupported";
    case SkeletonPreflightCode::RetargetAdditiveTrackUnsupported: return "retargetAdditiveTrackUnsupported";
    case SkeletonPreflightCode::AnimationUnreadable: return "animationUnreadable";
    case SkeletonPreflightCode::AnimationTrackBoneMissing: return "animationTrackBoneMissing";
    case SkeletonPreflightCode::MeshUnreadable: return "meshUnreadable";
    case SkeletonPreflightCode::MeshSkinBindingInvalid: return "meshSkinBindingInvalid";
    case SkeletonPreflightCode::RetargetMeshUnsupported: return "retargetMeshUnsupported";
    case SkeletonPreflightCode::SkeletonMaskUnreadable: return "skeletonMaskUnreadable";
    case SkeletonPreflightCode::SkeletonMaskBoneMissing: return "skeletonMaskBoneMissing";
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
    case SkeletonBakeDependencyKind::SkeletonMask: return "skeletonMask";
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
