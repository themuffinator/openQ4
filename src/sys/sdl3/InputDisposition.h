// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../EventDisposition.h"
#include "../EventRetirement.h"

// Private scalar SDL poll storage, not sysEvent_t/journal data or delivery proof.
// The parent tag identifies the exact KeyboardPoll emission. A nonzero deferred
// emission reserves its later Session child; storage never emits that child.
struct sysKeyboardDisposition_t {
    sysEventDispositionTag_t parent;
    std::uint64_t deferredEmission = 0;
    bool Empty() const noexcept { return parent.Empty() && !deferredEmission; }
};
struct sysKeyboardInputDisposition_t {
    int key = 0;
    bool down = false;
    int time = 0;
    sysKeyboardDisposition_t disposition;
};
struct sysMouseInputDisposition_t {
    int action = 0, value = 0, time = 0;
    sysEventDispositionTag_t disposition;
};
enum class sysInputDispositionLane_t { Keyboard, Mouse };
struct sysInputDispositionSlice_t {
    std::uint64_t epoch = 0, streamToken = 0, serial = 0;
    sysInputDispositionLane_t lane = sysInputDispositionLane_t::Keyboard;
    unsigned count = 0;
    bool operator==(const sysInputDispositionSlice_t& o) const noexcept {
        return epoch == o.epoch && streamToken == o.streamToken && serial == o.serial &&
            lane == o.lane && count == o.count;
    }
};
struct sysInputDispositionSlot_t {
    sysInputDispositionSlice_t slice;
    unsigned index = 0;
};
static_assert(std::is_trivially_copyable<sysKeyboardInputDisposition_t>::value &&
    std::is_trivially_copyable<sysMouseInputDisposition_t>::value &&
    std::is_trivially_copyable<sysInputDispositionSlot_t>::value, "Poll sidecars must remain POD");
static_assert(sizeof(sysKeyboardDisposition_t) == sizeof(sysEventDispositionTag_t) + sizeof(std::uint64_t),
    "Keyboard sidecar must remain bounded");
static_assert(sizeof(sysInputDispositionSlice_t) == 32 && sizeof(sysInputDispositionSlot_t) == 40,
    "Polled receipts must remain bounded");

// Constructing event thread only; no pumping, allocation, native callbacks or
// accepted input effects. SDL CRITICAL_SECTION_ONE precedes the disposition
// mutex. Disposition methods never acquire an SDL section; no reverse lock path.
// Inputs/outputs must not alias each other or engine storage. Continuity is an
// observation, not a cross-queue reservation: callers recheck it before effects.
// Admission consumes the entire scalar+tag input only on success, with no full
// ring eviction. A failed admission preserves caller and queued ownership.
bool Sys_QueKeyboardInputWithDisposition(sysKeyboardInputDisposition_t&) noexcept;
bool Sys_QueMouseInputWithDisposition(sysMouseInputDisposition_t&) noexcept;

// Poll moves the whole validated ring into a checked slice (including ordinary
// untagged entries), refusing prior polled ownership until End. Empty slices are
// successful and also require End. Serials never repeat, including after Clear.
// Legacy Poll may replace a legacy slice as before, but cannot touch a checked
// slice or pass a tagged ring head. End of a legacy slice releases its indices.
bool Sys_PollKeyboardInputWithDisposition(sysInputDispositionSlice_t&) noexcept;
bool Sys_PollMouseInputWithDisposition(sysInputDispositionSlice_t&) noexcept;

// Peek copies the exact next slot without removal. Take transfers that slot once
// and clears it. Ready alone changes outputs; Empty/Refused preserve them. End
// requires every slot taken. Stale ownership remains for explicit Clear/retirement;
// these methods do not authorize stale-head cancellation or legacy replay.
sysEventTransfer_t Sys_PeekKeyboardInputWithDisposition(const sysInputDispositionSlice_t&,
    sysInputDispositionSlot_t&, sysKeyboardInputDisposition_t&) noexcept;
sysEventTransfer_t Sys_PeekMouseInputWithDisposition(const sysInputDispositionSlice_t&,
    sysInputDispositionSlot_t&, sysMouseInputDisposition_t&) noexcept;
sysEventTransfer_t Sys_TakeKeyboardInputWithDisposition(const sysInputDispositionSlot_t&,
    sysKeyboardInputDisposition_t&) noexcept;
sysEventTransfer_t Sys_TakeMouseInputWithDisposition(const sysInputDispositionSlot_t&,
    sysMouseInputDisposition_t&) noexcept;
bool Sys_EndKeyboardInputWithDisposition(const sysInputDispositionSlice_t&) noexcept;
bool Sys_EndMouseInputWithDisposition(const sysInputDispositionSlice_t&) noexcept;

// Exact original-thread cleanup, never pumping or delivering. A retained checked
// slice precedes its ring; a legacy slice or untagged next entry obstructs. Peek
// copies NativeInputHead without permission lookup. Take calls only the route's
// callback-free permit predicate under the SDL lock, then transfers one value.
// Keyboard output retains its reserved deferred child but never queues that child.
// Footprints use type SE_KEY/SE_MOUSE, value key/action, value2 down/value and
// original time. That shape does not imply a Session emission or input authority.
sysEventTransfer_t Sys_PeekKeyboardInputForRetirement(openq4::NativeInputHead&) noexcept;
sysEventTransfer_t Sys_PeekMouseInputForRetirement(openq4::NativeInputHead&) noexcept;
sysEventTransfer_t Sys_TakeKeyboardInputForRetirement(openq4::NativeInputRoute&,
    const openq4::NativeInputRoute::CancellationPermit&, sysKeyboardInputDisposition_t&) noexcept;
sysEventTransfer_t Sys_TakeMouseInputForRetirement(openq4::NativeInputRoute&,
    const openq4::NativeInputRoute::CancellationPermit&, sysMouseInputDisposition_t&) noexcept;
// Empty metadata release only: exact original slice, next==count, original bound
// thread. Active epoch/continuity may be retired. No remaining entry is discarded,
// and success proves neither event disposition, provider retirement nor an ACK.
bool Sys_EndKeyboardInputForRetirement(const sysInputDispositionSlice_t&) noexcept;
bool Sys_EndMouseInputForRetirement(const sysInputDispositionSlice_t&) noexcept;
