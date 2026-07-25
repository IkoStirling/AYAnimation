// AnimNotifyEvent.h — Phase 1.5 cross-module bridge event.
//
// AnimationSystem::onUpdate (in AYEntity) drains each AnimationPlayer's
// per-frame fired-notifies queue via consumePendingNotifies() and posts one
// AnimNotifyEvent per crossed marker into AYEventSystem::EventBus. Game
// code subscribes via bus.subscribe<AnimNotifyEvent>(fn) regardless of
// which subsystem they live in (audio, VFX, UI feedback, gameplay rules,
// ...).
//
// Lifetime contract:
//   - `clipName` and `notifyName` are stable pointers into the IAnimation
//     asset; the asset lives in ResourceManager and survives until it is
//     unloaded. We use `emit` (synchronous, main-thread only) so callers
//     receive a valid pointer for the duration of the callback.
//   - Subscribers MUST NOT retain these pointers past the callback return.
//   - Subscribers MAY retain `entity` (cheap integer) and `notifyTime`
//     + `payload` (plain floats).
//
// Module id range:
//   AYAnimation = 0x000A'xxxx
//   AnimNotifyEvent = 0x000A'0001
//
// We intentionally keep this header **header-light**: only EventPriority
// from AYEventSystem. `entity` is typed as `uint32_t` because AYEntity's
// handle type isn't a single integer (it's `{id, version}` — see
// AYEntityHandle.h), and the unique identity we want to broadcast is
// just the id. A future PR can upgrade to EntityHandle if we need
// version-aware stale-pointer detection.

#pragma once

#include <ayevent/EventPriority.h>

#include <cstdint>

namespace ayt::anim
{

struct AnimNotifyEvent
{
    static constexpr ayt::event::EventTypeId   kTypeId   = 0x000A'0001u;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    std::uint32_t entity     = 0u;             // ayt::entity::Entity::getId()
    const char*   clipName   = nullptr;        // stable ptr, IAnimation-owned
    const char*   notifyName = nullptr;        // stable ptr, IAnimation-owned
    float         notifyTime = 0.0f;           // seconds on AYAnimation timeline
    float         payload    = 0.0f;           // optional marker payload
};

} // namespace ayt::anim
