// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeInputEmissionInventory.h"
#include "InputDisposition.h"

class idEventLoop;
typedef struct sysEvent_s sysEvent_t;
namespace openq4 {
// The status refers to the original, frozen input. StoredRetired means storage
// owns that input even though post-transfer admission/owner validation failed.
enum class NativeInputTransferStatus { Unchanged, StoredAdmitted, StoredRetired };
enum class NativeInputSessionQueue { Platform, Pushed };
enum class NativeInputDeliveryStatus { Unchanged, OwnedDelivery, OwnedRetired };

// One engine-owned transport/disposal owner, not a pump or delivery scheduler.
// Claim before issuing any ticket. Actual queue functions, not caller booleans,
// establish ownership. Every referenced object outlives checked Release.
// No native enablement, Session/GUI callback, ordinary replay or fence ACK.
class NativeInputTransfers final {
public:
    NativeInputTransfers(NativeInputEmissionInventory&, NativeInputRoute&, idEventLoop&) noexcept;
    NativeInputTransfers(const NativeInputTransfers&)=delete;
    NativeInputTransfers& operator=(const NativeInputTransfers&)=delete;
    bool Claim(std::string& error) noexcept;
    NativeInputTransferStatus SessionKey(NativeDispositionRecord, sysEvent_t&, NativeInputSessionQueue,
        NativeTranslatedEmission& out, std::string&) noexcept;
    NativeInputTransferStatus SessionCharacter(NativeDispositionRecord, sysEvent_t&, NativeInputSessionQueue,
        NativeTranslatedEmission& out, std::string&) noexcept;
    NativeInputTransferStatus SessionMouse(NativeDispositionRecord, sysEvent_t&, NativeInputSessionQueue,
        NativeTranslatedEmission& out, std::string&) noexcept;
    NativeInputTransferStatus Keyboard(NativeDispositionRecord,int key,bool down,int time,
        NativeTranslatedKeyboard& out,std::string&) noexcept;
    NativeInputTransferStatus Mouse(NativeDispositionRecord,int action,int value,int time,
        NativeTranslatedEmission& out,std::string&) noexcept;
    // Only during the exact Keyboard parent's live delivery. The emitted key
    // is derived from the retained reservation, never reconstructed from input.
    NativeInputTransferStatus Deferred(const NativeTranslatedKeyboard&,
        NativeTranslatedEmission& out,std::string&) noexcept;
    // These exact Take boundaries transfer storage ownership to the caller,
    // then BeginDelivery rechecks the current ledger/route. Both Owned results
    // replace outputs and require caller payload disposal. OwnedRetired forbids
    // input effects and retains an unresolved obligation, never a terminal fact.
    // The sink calls the existing ledger CompleteDelivery only after its actual
    // delivery/audited discard and payload disposal. No sink callback is made here.
    NativeInputDeliveryStatus BeginSessionDelivery(sysEvent_t&,NativeTranslatedEmission&,std::string&) noexcept;
    NativeInputDeliveryStatus BeginKeyboardDelivery(const sysInputDispositionSlot_t&,
        sysKeyboardInputDisposition_t&,NativeTranslatedEmission&,std::string&) noexcept;
    NativeInputDeliveryStatus BeginMouseDelivery(const sysInputDispositionSlot_t&,
        sysMouseInputDisposition_t&,NativeTranslatedEmission&,std::string&) noexcept;
    // Caller must already retire native/UI/provider authority. These functions
    // never do so implicitly. Session uses actual pushed-before-platform FIFO.
    // Ready means one exact stored entry was removed and its payload freed;
    // Empty/Refused preserve out. Partial progress after reentry stays recorded
    // privately even when Refused is returned. No forgotten/synthesized input.
    sysEventTransfer_t CancelSession(NativeInputHead& out,std::string&) noexcept;
    sysEventTransfer_t CancelKeyboard(NativeInputHead& out,std::string&) noexcept;
    sysEventTransfer_t CancelMouse(NativeInputHead& out,std::string&) noexcept;
    // Must follow Source's checked route release/unbind. No foreign calls.
    bool Release() noexcept;
    bool Busy() const noexcept {return std::this_thread::get_id()!=thread || calling;}
private:
    struct Guard;
    struct Authorization;
    bool Enter(bool cleanup,std::string&) noexcept;
    bool Fail(std::string&,const char*) noexcept;
    NativeInputTransferStatus Session(NativeDispositionRecord,sysEvent_t&,NativeInputSessionQueue,
        NativeInputKind,NativeTranslatedEmission&,std::string&) noexcept;
    NativeInputTransferStatus AdmitStored(const NativeTranslatedEmission&,std::uint64_t,std::string&) noexcept;
    NativeInputDeliveryStatus Take(NativeInputSink,const sysInputDispositionSlot_t*,sysEvent_t&,
        sysKeyboardInputDisposition_t&,sysMouseInputDisposition_t&,NativeTranslatedEmission&,std::string&) noexcept;
    sysEventTransfer_t Cancel(NativeInputSink,NativeInputHead&,std::string&) noexcept;
    NativeInputEmissionInventory& inventory;
    NativeInputRoute& route;
    idEventLoop& loop;
    const std::thread::id thread;
    std::uint64_t mutation=0;
    bool calling=false,claimed=false,revoked=false,released=false,interrupted=false;
};
} // namespace openq4
