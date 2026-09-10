// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <type_traits>

typedef struct sysEvent_s sysEvent_t;

// Private process-local queue sidecar, NEVER part of sysEvent_t or a journal.
// route is an immutable engine-owned registration for the original GUI/editor
// route (not a pointer). Window/module lifetimes and every identity are non-reused.
// The eventual native/Session adapter owns their resolution and eligibility.
// Record membership does not prove physical input or native text provenance.
struct sysEventDispositionTag_t {
    std::uint64_t dispatchEpoch = 0, streamToken = 0, providerEpoch = 0;
    std::uint64_t route = 0, window = 0, windowLifetime = 0;
    std::uint64_t ingress = 0, batchSerial = 0, ledger = 0, ledgerSerial = 0;
    std::uint64_t queueSequence = 0, recordIndex = 0, emission = 0;
    bool Empty() const noexcept {
        return !(dispatchEpoch | streamToken | providerEpoch | route | window | windowLifetime |
            ingress | batchSerial | ledger | ledgerSerial | queueSequence | recordIndex | emission);
    }
    bool ShapeValid() const noexcept {
        return dispatchEpoch && streamToken && providerEpoch && route && window && windowLifetime &&
            ingress && batchSerial && ledger && ledgerSerial && queueSequence && emission && recordIndex < 8192;
    }
    bool operator==(const sysEventDispositionTag_t& o) const noexcept {
        return dispatchEpoch == o.dispatchEpoch && streamToken == o.streamToken && providerEpoch == o.providerEpoch &&
            route == o.route && window == o.window && windowLifetime == o.windowLifetime && ingress == o.ingress &&
            batchSerial == o.batchSerial && ledger == o.ledger && ledgerSerial == o.ledgerSerial &&
            queueSequence == o.queueSequence && recordIndex == o.recordIndex && emission == o.emission;
    }
};
static_assert(std::is_trivially_copyable<sysEventDispositionTag_t>::value, "Queue sidecars must remain POD values");
static_assert(sizeof(sysEventDispositionTag_t) == 13 * sizeof(std::uint64_t), "Bounded queue tag layout changed");

// Storage epoch only. Init binds the engine event-loop thread; Shutdown retires
// it. These calls enable no native input/provider. Once bound, another thread
// cannot rebind, retire, admit or take tracked events. Epochs never repeat.
std::uint64_t Sys_BindEventDispositionThread() noexcept;
bool Sys_RetireEventDispositionThread() noexcept;
std::uint64_t Sys_EventDispositionEpoch() noexcept;
// Callback-free original-thread identity, retained even after epoch retirement.
// This grants no delivery/retirement authority and does not reactivate an epoch.
// The mutex is never held across an SDL critical-section acquisition or callback.
bool Sys_EventDispositionBoundThread() noexcept;
bool Sys_EventDispositionTagCurrent(const sysEventDispositionTag_t&) noexcept;

enum class sysEventTransfer_t { Ready, Empty, Refused };
// Tracked admission consumes both inputs ONLY on success (resets to zero).
// Failure preserves both inputs and payload ownership. No eviction on full.
// Checked take transfers the complete event+tag only on Ready; otherwise outputs
// remain unchanged. Stale tagged heads stay queued for explicit retirement/clear.
// Legacy getters refuse tagged heads without dequeue, invalidate continuity and
// return SE_NONE. There is no generic conversion/replay as legacy input.
// These are storage transfers, NEVER terminal delivery/native-ACK receipts.
// Inputs/outputs must not alias engine queue storage. Callers must serialize
// ordinary queue access; asyncInput=1 native activation remains unsupported.
bool Sys_QueTrackedEvent(sysEvent_t&, sysEventDispositionTag_t&) noexcept;
sysEventTransfer_t Sys_TakeEventWithDisposition(sysEvent_t&, sysEventDispositionTag_t&) noexcept;
