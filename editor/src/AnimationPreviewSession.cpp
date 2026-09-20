#include <AYAnimationEditor/AnimationPreviewSession.h>

#include <AYIO/File.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Mesh.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace ayt::anim::editor {
namespace {

using Json = nlohmann::json;

void setError(std::string* error, std::string message)
{
    if (error != nullptr) *error = std::move(message);
}

std::string normalizedAbsolute(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal().generic_string();
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizedRelative(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0u) == 0u) value.erase(0u, 2u);
    return lower(std::move(value));
}

std::filesystem::path resolveDependencyPath(
    const std::filesystem::path& root, const std::string& relative)
{
    std::filesystem::path path(relative);
    return path.is_absolute() ? path.lexically_normal()
                              : (root / path).lexically_normal();
}

bool regularFile(const std::string& path)
{
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

} // namespace

AnimationPreviewSession::AnimationPreviewSession() = default;
AnimationPreviewSession::~AnimationPreviewSession() = default;

bool AnimationPreviewSession::openAnimation(const std::string& path,
                                            std::string* error)
{
    auto animation = std::make_shared<ayt::resource::Animation>();
    const auto bytes = path.empty()
        ? std::vector<ayt::math::UInt8>{}
        : ayt::io::File::readAllBytes(path);
    if (bytes.empty()
        || !animation->loadFromBinary(bytes.data(), bytes.size())) {
        setError(error, "Unable to load animation resource: " + path);
        return false;
    }
    _animation = std::move(animation);
    _animationPath = normalizedAbsolute(path);
    _playing = false;
    bindPlayer();
    rebuildDiagnostics();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

bool AnimationPreviewSession::reloadAnimation(std::string* error)
{
    if (_animationPath.empty()) {
        setError(error, "No animation is open.");
        return false;
    }
    const float previousTime = time();
    const bool wasPlaying = _playing;
    const std::string path = _animationPath;
    if (!openAnimation(path, error)) return false;
    (void)setTime(previousTime);
    if (wasPlaying) play();
    return true;
}

bool AnimationPreviewSession::bindSkeleton(const std::string& path,
                                           std::string* error)
{
    auto skeleton = std::make_shared<ayt::resource::Skeleton>();
    if (path.empty() || !skeleton->load(path)) {
        setError(error, "Unable to load skeleton resource: " + path);
        return false;
    }
    _skeleton = std::move(skeleton);
    _bindings.skeletonPath = normalizedAbsolute(path);
    rebuildBones();
    rebuildBindPose();
    bindPlayer();
    rebuildDiagnostics();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

bool AnimationPreviewSession::bindMesh(const std::string& path,
                                       std::string* error)
{
    auto mesh = std::make_shared<ayt::resource::Mesh>();
    if (path.empty() || !mesh->load(path)) {
        setError(error, "Unable to load mesh resource: " + path);
        return false;
    }
    _mesh = std::move(mesh);
    _bindings.meshPath = normalizedAbsolute(path);
    inferMaterialFromMesh();
    rebuildDiagnostics();
    ++_revision;
    if (error != nullptr) error->clear();
    return true;
}

void AnimationPreviewSession::unbindSkeleton()
{
    _playing = false;
    _player.stop();
    _player.setSkeleton({});
    _skeleton.reset();
    _bindings.skeletonPath.clear();
    _bones.clear();
    _bindWorld.clear();
    _poseWorld.clear();
    _skin.clear();
    rebuildDiagnostics();
    ++_poseRevision;
    ++_revision;
}

void AnimationPreviewSession::unbindMesh()
{
    _mesh.reset();
    _bindings.meshPath.clear();
    _bindings.materialPath.clear();
    rebuildDiagnostics();
    ++_revision;
}

void AnimationPreviewSession::setMaterialPath(std::string path)
{
    if (!path.empty()) path = normalizedAbsolute(path);
    if (_bindings.materialPath == path) return;
    _bindings.materialPath = std::move(path);
    ++_revision;
}

AnimationPreviewBindings AnimationPreviewSession::inferCompanionAssets() const
{
    AnimationPreviewBindings result;
    if (_animationPath.empty()) return result;

    const std::filesystem::path animationPath(_animationPath);
    const std::filesystem::path assetRoot = animationPath.parent_path().parent_path();
    std::error_code error;
    if (assetRoot.empty() || !std::filesystem::is_directory(assetRoot, error)) {
        return result;
    }
    const std::string animationRelative = normalizedRelative(
        std::filesystem::relative(animationPath, assetRoot, error).generic_string());
    if (error) return result;

    std::string skeletonRelative;
    std::string meshRelative;
    for (std::filesystem::directory_iterator it(assetRoot, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)
            || lower(it->path().filename().string()).find(".aydep.json")
                == std::string::npos) {
            continue;
        }
        try {
            const std::string text = ayt::io::File::readAllText(it->path().string());
            if (text.empty()) continue;
            const Json root = Json::parse(text);
            const auto dependencies = root.find("dependencies");
            if (dependencies == root.end() || !dependencies->is_array()) continue;
            for (const auto& dependency : *dependencies) {
                const std::string from = dependency.value("from", std::string{});
                const std::string to = dependency.value("to", std::string{});
                if (normalizedRelative(to) == animationRelative) {
                    skeletonRelative = from;
                }
            }
            if (!skeletonRelative.empty()) {
                for (const auto& dependency : *dependencies) {
                    const std::string from = dependency.value("from", std::string{});
                    const std::string to = dependency.value("to", std::string{});
                    if (normalizedRelative(to)
                        == normalizedRelative(skeletonRelative)) {
                        meshRelative = from;
                        break;
                    }
                }
            }
        } catch (...) {
            continue;
        }
        if (!skeletonRelative.empty()) break;
    }

    if (!skeletonRelative.empty()) {
        const auto path = resolveDependencyPath(assetRoot, skeletonRelative);
        if (std::filesystem::is_regular_file(path, error)) {
            result.skeletonPath = normalizedAbsolute(path);
        }
    }
    if (!meshRelative.empty()) {
        const auto path = resolveDependencyPath(assetRoot, meshRelative);
        if (std::filesystem::is_regular_file(path, error)) {
            result.meshPath = normalizedAbsolute(path);
        }
    }

    // Loose assets and hand-authored fixtures may not have a dependency
    // sidecar. Match the import stem conservatively inside canonical folders.
    std::string stem = animationPath.stem().string();
    const auto separator = stem.find_last_of('_');
    if (separator != std::string::npos) stem.resize(separator);
    if (result.skeletonPath.empty()) {
        const auto skeletonDir = assetRoot / "skeletons";
        for (std::filesystem::directory_iterator it(skeletonDir, error), end;
             !error && it != end; it.increment(error)) {
            if (lower(it->path().extension().string()) == ".ayskel"
                && lower(it->path().stem().string()).rfind(lower(stem), 0u) == 0u) {
                result.skeletonPath = normalizedAbsolute(it->path());
                break;
            }
        }
        error.clear();
    }
    if (result.meshPath.empty()) {
        const auto meshDir = assetRoot / "meshes";
        for (std::filesystem::directory_iterator it(meshDir, error), end;
             !error && it != end; it.increment(error)) {
            if (lower(it->path().extension().string()) == ".aymesh"
                && lower(it->path().stem().string()).rfind(lower(stem), 0u) == 0u) {
                result.meshPath = normalizedAbsolute(it->path());
                break;
            }
        }
    }
    return result;
}

bool AnimationPreviewSession::applyBindings(
    const AnimationPreviewBindings& bindings, std::string* error)
{
    std::string localError;
    if (!bindings.skeletonPath.empty()
        && !bindSkeleton(bindings.skeletonPath, &localError)) {
        setError(error, localError);
        return false;
    }
    if (!bindings.meshPath.empty() && !bindMesh(bindings.meshPath, &localError)) {
        setError(error, localError);
        return false;
    }
    if (!bindings.materialPath.empty()) setMaterialPath(bindings.materialPath);
    if (error != nullptr) error->clear();
    return true;
}

const ayt::resource::IAnimation* AnimationPreviewSession::animation() const noexcept
{
    return _animation.get();
}

void AnimationPreviewSession::setPreviewMode(AnimationPreviewMode mode) noexcept
{
    if (_requestedMode == mode) return;
    _requestedMode = mode;
    ++_revision;
}

AnimationPreviewMode AnimationPreviewSession::effectivePreviewMode() const noexcept
{
    if (_requestedMode == AnimationPreviewMode::SkeletonOnly) {
        return AnimationPreviewMode::SkeletonOnly;
    }
    if (!canPreviewModel()) return AnimationPreviewMode::SkeletonOnly;
    return _requestedMode;
}

bool AnimationPreviewSession::canPreviewSkeleton() const noexcept
{
    return _skeleton != nullptr && !_poseWorld.empty();
}

bool AnimationPreviewSession::canPreviewModel() const noexcept
{
    return _mesh != nullptr && _mesh->hasSkinWeights() && _skeleton != nullptr
        && !_skin.empty();
}

void AnimationPreviewSession::play()
{
    if (_animation == nullptr || _skeleton == nullptr) return;
    if (time() >= duration() && !_looping) (void)setTime(0.0f);
    _player.resume();
    _playing = true;
}

void AnimationPreviewSession::pause()
{
    _player.pause();
    _playing = false;
}

void AnimationPreviewSession::stop()
{
    _playing = false;
    if (_animation == nullptr || _skeleton == nullptr) return;
    _player.stop();
    bindPlayer();
}

void AnimationPreviewSession::tick(float dt)
{
    if (!_playing || dt <= 0.0f || _animation == nullptr
        || _skeleton == nullptr) return;
    _player.tick(dt);
    _player.evaluate();
    rebuildPoseFromPlayer();
    if (!_looping && time() >= duration()) {
        _playing = false;
        _player.pause();
    }
}

bool AnimationPreviewSession::setTime(float seconds)
{
    if (_animation == nullptr || _skeleton == nullptr) return false;
    _player.setTime(std::clamp(seconds, 0.0f, duration()));
    _player.evaluate();
    rebuildPoseFromPlayer();
    return true;
}

void AnimationPreviewSession::setLooping(bool looping) noexcept
{
    if (_looping == looping) return;
    _looping = looping;
    _player.setLoop(looping);
    ++_revision;
}

void AnimationPreviewSession::setPlayRate(float rate) noexcept
{
    const float clamped = std::clamp(rate, 0.05f, 4.0f);
    if (std::fabs(_playRate - clamped) < 1.0e-6f) return;
    _playRate = clamped;
    _player.setPlayRate(clamped);
    ++_revision;
}

float AnimationPreviewSession::time() const noexcept
{
    return _player.getTime();
}

float AnimationPreviewSession::duration() const noexcept
{
    return _animation != nullptr ? _animation->getDuration() : 0.0f;
}

std::size_t AnimationPreviewSession::missingTrackCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        _diagnostics.begin(), _diagnostics.end(), [](const auto& value) {
            return value.code
                == AnimationPreviewDiagnosticCode::AnimationTrackBoneMissing;
        }));
}

const char* AnimationPreviewSession::previewModeName(
    AnimationPreviewMode mode) noexcept
{
    switch (mode) {
    case AnimationPreviewMode::ModelAndSkeleton: return "model+skeleton";
    case AnimationPreviewMode::ModelOnly: return "model";
    case AnimationPreviewMode::SkeletonOnly: return "skeleton";
    }
    return "skeleton";
}

const char* AnimationPreviewSession::diagnosticCodeName(
    AnimationPreviewDiagnosticCode code) noexcept
{
    switch (code) {
    case AnimationPreviewDiagnosticCode::SkeletonNotBound:
        return "skeleton-not-bound";
    case AnimationPreviewDiagnosticCode::MeshNotBound: return "mesh-not-bound";
    case AnimationPreviewDiagnosticCode::MeshHasNoSkinWeights:
        return "mesh-has-no-skin-weights";
    case AnimationPreviewDiagnosticCode::AnimationTrackBoneMissing:
        return "animation-track-bone-missing";
    case AnimationPreviewDiagnosticCode::MeshPaletteBoneOutOfRange:
        return "mesh-palette-bone-out-of-range";
    }
    return "unknown";
}

void AnimationPreviewSession::bindPlayer()
{
    _player.stop();
    _player.setSkeleton(_skeleton);
    _player.setLoop(_looping);
    _player.setPlayRate(_playRate);
    if (_animation != nullptr && _skeleton != nullptr) {
        _player.play(_animation.get());
        _player.setLoop(_looping);
        _player.setPlayRate(_playRate);
        _player.setTime(0.0f);
        _player.evaluate();
        _player.pause();
        rebuildPoseFromPlayer();
    } else {
        _poseWorld = _bindWorld;
        _skin.clear();
        ++_poseRevision;
    }
}

void AnimationPreviewSession::rebuildBones()
{
    _bones.clear();
    if (_skeleton == nullptr) return;
    _bones.reserve(_skeleton->getBoneCount());
    const ayt::resource::Bone* sourceBones = _skeleton->getBones();
    for (std::uint32_t index = 0; index < _skeleton->getBoneCount(); ++index) {
        AnimationPreviewBone bone;
        bone.index = static_cast<int>(index);
        bone.parentIndex = _skeleton->getParentBoneIndex(index);
        bone.name = sourceBones != nullptr ? sourceBones[index].name : std::string{};
        int cursor = bone.parentIndex;
        std::size_t guard = 0u;
        while (cursor >= 0 && guard++ < _skeleton->getBoneCount()) {
            ++bone.depth;
            cursor = _skeleton->getParentBoneIndex(
                static_cast<std::size_t>(cursor));
        }
        _bones.push_back(std::move(bone));
    }
}

void AnimationPreviewSession::rebuildBindPose()
{
    _bindWorld.clear();
    if (_skeleton == nullptr) return;
    const std::size_t count = _skeleton->getBoneCount();
    _bindWorld.assign(count, ayt::math::Float4x4::identity());
    const auto* positions = _skeleton->getLocalPositions();
    const auto* rotations = _skeleton->getLocalRotations();
    const auto* scales = _skeleton->getLocalScales();
    for (std::size_t index = 0; index < count; ++index) {
        const auto local = ayt::math::Float4x4::fromTRS(
            positions[index], rotations[index], scales[index]);
        const int parent = _skeleton->getParentBoneIndex(index);
        _bindWorld[index] = parent >= 0
            ? _bindWorld[static_cast<std::size_t>(parent)] * local : local;
    }
    _poseWorld = _bindWorld;
    ++_poseRevision;
}

void AnimationPreviewSession::rebuildPoseFromPlayer()
{
    const auto* world = _player.getBoneWorldMatrices();
    const auto* skin = _player.getBoneSkinMatrices();
    const std::size_t count = _player.getBoneCount();
    if (world == nullptr || count != _bones.size()) {
        _poseWorld = _bindWorld;
        _skin.clear();
    } else {
        _poseWorld.assign(world, world + count);
        if (skin != nullptr) _skin.assign(skin, skin + count);
        else _skin.clear();
    }
    ++_poseRevision;
}

void AnimationPreviewSession::rebuildDiagnostics()
{
    _diagnostics.clear();
    if (_skeleton == nullptr) {
        _diagnostics.push_back({AnimationPreviewDiagnosticSeverity::Warning,
            AnimationPreviewDiagnosticCode::SkeletonNotBound,
            "Bind a skeleton to evaluate animation tracks."});
    }
    if (_mesh == nullptr) {
        _diagnostics.push_back({AnimationPreviewDiagnosticSeverity::Info,
            AnimationPreviewDiagnosticCode::MeshNotBound,
            "No preview mesh is bound; skeleton preview remains available."});
    } else if (!_mesh->hasSkinWeights()) {
        _diagnostics.push_back({AnimationPreviewDiagnosticSeverity::Warning,
            AnimationPreviewDiagnosticCode::MeshHasNoSkinWeights,
            "The bound mesh has no skin weights."});
    }
    if (_animation != nullptr && _skeleton != nullptr) {
        for (std::uint32_t index = 0; index < _animation->getTrackCount(); ++index) {
            const char* node = _animation->getTrackNodeName(index);
            if (node == nullptr || *node == '\0' || _skeleton->findBone(node) >= 0) {
                continue;
            }
            _diagnostics.push_back({AnimationPreviewDiagnosticSeverity::Error,
                AnimationPreviewDiagnosticCode::AnimationTrackBoneMissing,
                std::string("Track targets missing bone: ") + node,
                static_cast<int>(index), -1});
        }
    }
    if (_mesh != nullptr && _skeleton != nullptr) {
        const std::uint32_t* joints = _mesh->getSkinPaletteJoints();
        for (std::uint32_t index = 0; joints != nullptr
             && index < _mesh->getSkinPaletteJointCount(); ++index) {
            if (joints[index] < _skeleton->getBoneCount()) continue;
            _diagnostics.push_back({AnimationPreviewDiagnosticSeverity::Error,
                AnimationPreviewDiagnosticCode::MeshPaletteBoneOutOfRange,
                "Mesh skin palette references a bone outside the skeleton.",
                -1, static_cast<int>(joints[index])});
            break;
        }
    }
}

void AnimationPreviewSession::inferMaterialFromMesh()
{
    if (_mesh == nullptr || _mesh->getMaterialSlotCount() == 0u
        || _mesh->getMaterialSlot(0u) == nullptr) return;
    std::filesystem::path material(_mesh->getMaterialSlot(0u));
    if (material.empty()) return;
    if (material.is_relative()) {
        const std::filesystem::path meshPath(_bindings.meshPath);
        const std::filesystem::path root = meshPath.parent_path().parent_path();
        material = root / material;
    }
    if (regularFile(material.string())) {
        _bindings.materialPath = normalizedAbsolute(material);
    }
}

} // namespace ayt::anim::editor
