// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../framework/NativeInputRoute.h"
#include <cstring>

// Pure footprint, no payload read or authority. sysEvent_t has no timestamp;
// its time field is consequently zero. The exact original payload stays owned
// by storage until a successful Take transfers it to the caller.
template<class Event>
inline openq4::NativeInputHead Sys_EventRetirementHead(openq4::NativeInputLane lane,
    std::uint64_t serial, unsigned slot, const Event& event, const sysEventDispositionTag_t& tag) noexcept {
    int type = 0;
    static_assert(sizeof(event.evType) == sizeof(type), "Event type footprint changed");
    std::memcpy(&type, &event.evType, sizeof(type));
    return {lane, serial, slot, tag, {type, event.evValue, event.evValue2, 0,
        event.evPtrLength, reinterpret_cast<std::uintptr_t>(event.evPtr), 0}};
}

// Original event thread only, including after its active epoch retires. These
// serialized platform operations never pump or invoke Source/GUI/native code.
// SDL variants additionally hold their existing owning storage lock.
// Peek only copies a tagged exact head. An untagged prefix refuses; never skip,
// reclassify or remove it. PrepareCancellation must run OUTSIDE storage locks.
// Take tests the current head against the opaque permit, then transfers event,
// payload and tag once. Only Ready changes outputs; no alias with storage/inputs.
// This is cancellation storage ownership, not terminal disposition or any ACK.
sysEventTransfer_t Sys_PeekEventForRetirement(openq4::NativeInputHead&) noexcept;
sysEventTransfer_t Sys_TakeEventForRetirement(openq4::NativeInputRoute&,
    const openq4::NativeInputRoute::CancellationPermit&, sysEvent_t&, sysEventDispositionTag_t&) noexcept;
