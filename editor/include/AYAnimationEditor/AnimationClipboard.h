#pragma once
#include <AYAnimationEditor/AnimationAuthoring.h>
#include <optional>

namespace ayt::anim::editor {
struct AnimationClipboardKey {
    double offsetSeconds = 0;
    std::vector<float> values, inTangents, outTangents;
};
struct AnimationClipboardTrack {
    std::string nodeName, property;
    ayt::resource::AnimTrackType valueType{};
    ayt::resource::AnimBlendMode blendMode{};
    ayt::resource::AnimInterpolation interpolation{};
    std::vector<AnimationClipboardKey> keys;
};
struct AnimationClipboardNotify {
    std::string name;
    double offsetSeconds = 0;
    float payload = 0;
};
// Owned, in-memory authoring payload, not a resource/file extension.
// Times are relative seconds, tangent slopes remain value/second.
struct AnimationClipboard {
    std::vector<AnimationClipboardTrack> tracks;
    std::vector<AnimationClipboardNotify> notifies;
    double spanSeconds = 0;
    bool empty() const noexcept { return tracks.empty() && notifies.empty(); }
};
std::optional<AnimationClipboard> copyAnimationKeys(
    const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, std::string* error = nullptr);
/// Requires one exact compatible existing track per signature; no implicit
/// track creation, overwriting, bone remapping, or duration extension.
AnimationKeyEdit pasteAnimationKeys(const ayt::resource::IAnimation& target,
    const AnimationClipboard& clipboard, double startSeconds);
}
