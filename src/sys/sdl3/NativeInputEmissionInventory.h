// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeEventDisposition.h"
#include "../../framework/NativeInputRoute.h"

namespace openq4 {
struct NativeTranslatedEmission {
    NativeDispositionTicket ticket{};
    sysEventDispositionTag_t tag{};
    NativeInputValue value{};
};
struct NativeTranslatedKeyboard {
    NativeTranslatedEmission parent{}, deferred{};
};
// Actual translator ownership, one immutable batch/ledger. Typed operations
// record their branch's exact output BEFORE storage admission. None reconstructs
// a kind from a dequeued event, proves physical/native origin or inserts text.
// The driver keeps route, batch, ledger and this inventory alive through failed
// admission/retirement/disposal. Direct Issue calls outside this inventory are
// not silently reconstructed later. NativeEcho is deliberately unsupported.
class NativeInputEmissionInventory final {
public:
    NativeInputEmissionInventory();
    NativeInputEmissionInventory(const NativeInputEmissionInventory&) = delete;
    NativeInputEmissionInventory& operator=(const NativeInputEmissionInventory&) = delete;
    bool Bind(NativeInputRoute&, std::uint64_t route, const NativeInputBinding&,
        NativeEventDispositionLedger&, const NativeQueueBatch&, std::string& error) noexcept;
    bool SessionKey(NativeDispositionRecord, int key, bool down, int payloadLength,
        std::uintptr_t payload, NativeTranslatedEmission&, std::string&) noexcept;
    bool SessionCharacter(NativeDispositionRecord, int character, NativeTranslatedEmission&, std::string&) noexcept;
    bool SessionMouse(NativeDispositionRecord, int dx, int dy, NativeTranslatedEmission&, std::string&) noexcept;
    // Ctrl/Alt/RightAlt/PrintScreen reserve a deferred Session sibling before
    // exposing their parent. The actual Return path later transfers/admit it.
    bool Keyboard(NativeDispositionRecord, int key, bool down, int time,
        NativeTranslatedKeyboard&, std::string&) noexcept;
    bool Mouse(NativeDispositionRecord, int action, int value, int time,
        NativeTranslatedEmission&, std::string&) noexcept;
    // Call only after actual ownership transfer. Failure retains every issued
    // slot, including its payload address/child identity; never deletes a head.
    bool Admit(const NativeTranslatedEmission&, NativeDispositionAdmission&, std::string&) noexcept;
    bool Retire() noexcept;
    // Read-only exact join with the real RETIRED planned ledger. This does not
    // observe Source/provider/GUI or grant cancellation/ACK. Refusal preserves out.
    bool Inspect(std::uint64_t route,const NativeInputBinding&,const sysEventDispositionTag_t&,
        NativeInputIssued& out) const noexcept;
private:
    struct Entry {
        NativeTranslatedEmission emission;
        NativeEmissionPlan plan;
        NativeInputSink sink = NativeInputSink::Session;
        NativeInputKind kind = NativeInputKind::Unknown;
    };
    struct Guard;
    bool Enter(std::string&) noexcept;
    bool Fail(std::string&,const char*) noexcept;
    bool Current() noexcept;
    bool Issue(NativeDispositionRecord,NativeEmissionPlan,NativeInputSink,NativeInputKind,
        NativeInputValue,NativeTranslatedEmission&,std::string&) noexcept;
    const Entry* Find(const sysEventDispositionTag_t&) const noexcept;
    const std::thread::id thread;
    NativeInputRoute* route = nullptr;
    NativeEventDispositionLedger* ledger = nullptr;
    const NativeQueueBatch* batch = nullptr;
    std::unique_ptr<const NativeInputBinding> original;
    sysEventDispositionTag_t base{};
    std::vector<Entry> entries;
    bool calling = false, retired = false, bound = false;
};
} // namespace openq4
