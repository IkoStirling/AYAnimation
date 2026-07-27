// AnimNotifyEvent.h — Phase 1.5 cross-module bridge event.
//
// AnimationSystem::onUpdate (in AYEntity) drains each AnimationPlayer's
// per-frame fired-notifies queue via consumePendingNotifiesMerged() and
// posts one AnimNotifyEvent per crossed marker into AYEventSystem::EventBus.
// Game code subscribes via bus.subscribe<AnimNotifyEvent>(fn) regardless of
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
// Header-light by design: NO dependency on AYEventSystem. AYAnimation
// itself does not link AYEventSystem, so this file stays out of the
// include graph that pulls in EventBus.h. Callers (AYEntity bridge)
// include both AYEventSystem and AnimNotifyEvent; subscribers can
// include AnimNotifyEvent.h alone if they only need the POD shape and
// don't subscribe via the bus.
//
// `entity` is typed as `uint32_t` because AYEntity's handle type isn't
// a single integer (it's `{id, version}` — see AYEntityHandle.h), and
// the unique identity we want to broadcast is just the id. A future
// PR can upgrade to EntityHandle if we need version-aware stale-pointer
// detection.
//
// P1.5 (2026-07-27): `sourceTag` field added so subscribers can route
// per-source (Base vs Additive_0..7). kTypeId unchanged (0x000A'0001) so
// existing P1.3 subscribers keep their subscriptions.

#pragma once

#include <cstdint>

namespace ayt::anim
{

// P1.5 — AnimNotifySourceTag. Dense enum: Base=0, Additive_0=1..7=8.
// Default-constructible to Base (so existing P1.3 records that never
// read sourceTag remain Base-tagged after the field is added).
enum class AnimNotifySourceTag : std::uint8_t {
    Base        = 0,
    Additive_0  = 1,
    Additive_1  = 2,
    Additive_2  = 3,
    Additive_3  = 4,
    Additive_4  = 5,
    Additive_5  = 6,
    Additive_6  = 7,
    Additive_7  = 8,
};

struct AnimNotifyEvent
{
    // AYAnimation module id range (0x000A'xxxx). Stable across P1.3 /
    // P1.4 / P1.5 — existing subscribers keep their subscription.
    static constexpr std::uint32_t kTypeId = 0x000A'0001u;

    std::uint32_t        entity     = 0u;       // ayt::entity::Entity::getId()
    const char*          clipName   = nullptr;  // stable ptr, IAnimation-owned
    const char*          notifyName = nullptr;  // stable ptr, IAnimation-owned
    float                notifyTime = 0.0f;     // seconds on AYAnimation timeline
    float                payload    = 0.0f;     // optional marker payload
    AnimNotifySourceTag  sourceTag  = AnimNotifySourceTag::Base;   // P1.5 NEW
};

} // namespace ayt::anim
