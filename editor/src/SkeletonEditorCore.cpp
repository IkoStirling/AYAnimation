#include <AYAnimationEditor/SkeletonEditorCore.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <system_error>

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

} // namespace

SkeletonEditorCore::SkeletonEditorCore()
{
    _history.emplace_back();
}

SkeletonEditorCore::~SkeletonEditorCore() = default;

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
    path.replace_extension(kSkeletonMappingExtension);
    return normalizedPath(path);
}

bool SkeletonEditorCore::open(const std::string& inputPath, std::string* error)
{
    if (inputPath.empty()) {
        setError(error, "Skeleton editor path is empty.");
        return false;
    }

    std::filesystem::path requested(inputPath);
    std::string extension = requested.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    const bool mappingFirst = extension == kSkeletonMappingExtension;
    Snapshot loaded;
    std::string skeletonReference;
    std::string sourceFingerprint;
    if (mappingFirst) {
        if (!loadMappingFile(inputPath, skeletonReference, loaded,
                             sourceFingerprint, error)) {
            return false;
        }
        std::filesystem::path skeletonPath(skeletonReference);
        if (skeletonPath.is_relative()) {
            skeletonPath = requested.parent_path() / skeletonPath;
        }
        _mappingPath = normalizedPath(std::filesystem::absolute(requested));
        if (!loadSkeleton(normalizedPath(skeletonPath), error)) return false;
    } else {
        if (!loadSkeleton(inputPath, error)) return false;
        _mappingPath = defaultMappingPath(_skeletonPath);
        std::error_code existsError;
        if (std::filesystem::exists(_mappingPath, existsError)) {
            if (!loadMappingFile(_mappingPath, skeletonReference, loaded,
                                 sourceFingerprint, error)) {
                return false;
            }
        }
    }

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
    std::string* error) const
{
    const std::string text = ayt::io::File::readAllText(path);
    if (text.empty()) {
        setError(error, "Skeleton mapping is empty or unreadable: " + path);
        return false;
    }
    try {
        const Json root = Json::parse(text);
        if (root.value("type", std::string{}) != "SkeletonMapping"
            || root.value("version", 0u) != kSkeletonMappingSchemaVersion) {
            setError(error, "Unsupported skeleton mapping schema.");
            return false;
        }
        skeletonReference = root.value("skeleton", std::string{});
        if (skeletonReference.empty()) {
            setError(error, "Skeleton mapping does not reference a skeleton.");
            return false;
        }
        sourceFingerprint = root.value("sourceFingerprint", std::string{});
        snapshot.nativeSkeleton = root.value("native", false);
        const std::string bake = root.value("bakeState", std::string{"notBaked"});
        snapshot.bake = bake == "ready" ? SkeletonBakeState::Ready
            : bake == "stale" ? SkeletonBakeState::Stale
            : bake == "failed" ? SkeletonBakeState::Failed
            : SkeletonBakeState::NotBaked;
        snapshot.bakedFingerprint = root.value("bakedFingerprint", std::string{});
        if (const auto roles = root.find("roles"); roles != root.end()
            && roles->is_object()) {
            for (const HumanoidBoneSpec& spec : getHumanoidBoneSpecs()) {
                const auto value = roles->find(std::string(spec.canonicalName));
                if (value != roles->end() && value->is_number_integer()) {
                    const int index = value->get<int>();
                    if (index >= 0) (void)snapshot.mapping.bind(spec.role, index);
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
        if (index >= 0) roles[std::string(spec.canonicalName)] = index;
    }
    Json root = {
        {"type", "SkeletonMapping"},
        {"version", kSkeletonMappingSchemaVersion},
        {"skeleton", normalizedPath(reference)},
        {"sourceFingerprint", skeletonFingerprint()},
        {"native", snapshot.nativeSkeleton},
        {"bakeState", bakeStateName(snapshot.bake)},
        {"bakedFingerprint", snapshot.bakedFingerprint},
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
    const std::string absolutePath = normalizedPath(std::filesystem::absolute(path));
    if (!writeMappingFile(absolutePath, current(), error)) return false;
    _mappingPath = absolutePath;
    _loadedSourceFingerprint = skeletonFingerprint();
    _savedCursor = _historyCursor;
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

void SkeletonEditorCore::commit(Snapshot next)
{
    if (_historyCursor + 1u < _history.size()) {
        if (_savedCursor > _historyCursor) _savedCursor = kNoSavedCursor;
        _history.erase(_history.begin()
            + static_cast<std::ptrdiff_t>(_historyCursor + 1u), _history.end());
    }
    if (next.bake == SkeletonBakeState::Ready) next.bake = SkeletonBakeState::Stale;
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

bool SkeletonEditorCore::isDirty() const noexcept
{
    return _savedCursor == kNoSavedCursor || _historyCursor != _savedCursor;
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
    ++_revision;
}

void SkeletonEditorCore::tick(float dt)
{
    if (!_playing || _animation == nullptr || dt <= 0.0f) return;
    _player.tick(dt);
    _player.evaluate();
    rebuildPoseFromPlayer();
    ++_revision;
}

bool SkeletonEditorCore::setTime(float seconds)
{
    if (_animation == nullptr) return false;
    _player.setTime(std::clamp(seconds, 0.0f, duration()));
    _player.evaluate();
    rebuildPoseFromPlayer();
    ++_revision;
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
}

void SkeletonEditorCore::rebuildPoseFromPlayer()
{
    const ayt::math::Float4x4* world = _player.getBoneWorldMatrices();
    const std::size_t count = _player.getBoneCount();
    if (world == nullptr || count != _bones.size()) {
        _poseWorld = _bindWorld;
        return;
    }
    _poseWorld.assign(world, world + count);
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

} // namespace ayt::anim::editor
