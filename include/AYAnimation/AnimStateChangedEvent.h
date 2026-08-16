// AYAnimation/AnimStateChangedEvent.h — P3.1 (2026-08-06) state-changed event.
//
// Mirrors UE FAnimStateChangedEvent shape (subset). Emitted on the
// EventBus whenever a StateMachine transitions fires. Gameplay code
// subscribes to react (UI debug overlay, audio cue, particle spawn, etc).
//
// kTypeId is a fresh AYEventSystem type id (P3.1). Channel independence
// from AnimNotifyEvent (kTypeId 0x000A'0001) keeps subscription lists
// focused — subscribers wanting "notify on state change" don't receive
// per-frame marker events.

#pragma once

#include <cstdint>
#include <string>

namespace ayt { namespace entity { class Entity; } }

namespace ayt::anim
{

struct AnimStateChangedEvent {
    const ayt::entity::Entity* entity;
    std::string                previousState;
    std::string                currentState;
    static constexpr std::uint32_t kTypeId = 0x000A'0010u;
};

} // namespace ayt::anim