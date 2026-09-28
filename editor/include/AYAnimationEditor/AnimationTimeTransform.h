#pragma once
#include <AYAnimationEditor/AnimationAuthoring.h>

namespace ayt::anim::editor {
/// t' = anchor + (t - anchor) * scale, in seconds, across tracks and Notify.
/// Slopes divide by signed scale; negative scale swaps incoming/outgoing.
/// Bounds/collisions/overflow reject the complete operation. Step reversal is
/// rejected: the existing left-hold format cannot express exact right-hold.
AnimationKeyEdit retimeAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, double anchorSeconds, double scale);
}
