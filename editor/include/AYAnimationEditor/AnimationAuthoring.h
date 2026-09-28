#pragma once

#include <AYResource/assetsImpl/Animation.h>
#include <memory>
#include <string>
#include <vector>

namespace ayt::anim::editor {

// Detached authoring data: no widgets, history, resource manager, or file writes.
struct AuthoredAnimation {
    std::string name;
    float duration = 0;
    float ticksPerSecond = 30;
    ayt::math::FGuid guid;
    std::vector<ayt::resource::AnimTrack> tracks;
    std::vector<ayt::resource::AnimNotifyMarker> notifies;
};

struct AnimationKeyReference {
    std::size_t track = 0;
    std::size_t key = 0;
    bool notify = false;
    bool operator==(const AnimationKeyReference&) const = default;
};

struct AnimationKeyEdit {
    std::shared_ptr<ayt::resource::Animation> animation;
    // Positional correspondence with caller selection, after sort/remap.
    std::vector<AnimationKeyReference> keys;
    std::string error;
    explicit operator bool() const noexcept { return animation != nullptr; }
};

AuthoredAnimation copyAnimationForAuthoring(const ayt::resource::IAnimation& source);
std::shared_ptr<ayt::resource::Animation> buildAuthoredAnimation(const AuthoredAnimation& source);
/// Cross-track/Notify translation is atomic. Group clamping preserves spacing.
/// Values/tangents stay attached to keys; collisions or invalid IDs reject all.
/// Nonzero component edits cannot include Notify. Times use seconds throughout.
AnimationKeyEdit translateAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, double deltaSeconds,
    std::size_t component = 0, float deltaValue = 0);
AnimationKeyEdit removeAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys);

} // namespace ayt::anim::editor
