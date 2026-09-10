// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeQueueBatch.h"
#include <thread>
#include <array>

namespace openq4 {
struct NativeDispositionContext {
    std::uint64_t providerEpoch = 0, generation = 0, engineToken = 0;
    bool operator==(const NativeDispositionContext&) const = default;
};
// Engine-owned, callback-free, allocation-free observation of provider/module
// ownership and queue continuity. These non-reused claims must stay valid across
// ACK (the presence of a pending fence is NOT part of this probe). No GUI lookup,
// dispatch, SDL polling, user callback or external ownership adoption is allowed.
class NativeDispositionProbe {
public:
    virtual ~NativeDispositionProbe() = default;
    virtual bool Current(const NativeDispositionContext&) const noexcept = 0;
};
struct NativeDispositionReceipt {
    NativeQueueReceipt batch;
    std::uint64_t ledger = 0, serial = 0;
    bool operator==(const NativeDispositionReceipt&) const = default;
};
struct NativeDispositionRecord {
    NativeDispositionReceipt receipt;
    std::uint64_t queueSequence = 0;
    std::size_t index = 0;
    bool operator==(const NativeDispositionRecord&) const = default;
};
struct NativeDispositionTicket {
    NativeDispositionRecord record;
    std::uint64_t emission = 0;
    bool operator==(const NativeDispositionTicket&) const = default;
};
enum class NativeDispositionPass { SessionInitial, MousePoll, KeyboardPoll, SessionDeferred };
// Selected by the audited collection entry point, never inferred from SP/MP,
// GUI visibility or whichever sink first attempts delivery.
enum class NativeDispositionSchedule { BeforeSessionDrain, AtMousePollEntry };
struct NativeEmissionPlan {
    NativeDispositionPass pass = NativeDispositionPass::SessionInitial;
    // SessionDeferred alone requires an earlier same-record KeyboardPoll ticket.
    NativeDispositionTicket trigger{};
};
struct NativeDispositionAdmission {
    NativeDispositionTicket ticket{};
    std::uint64_t serial = 0;
    bool operator==(const NativeDispositionAdmission&) const = default;
};
enum class NativeRecordDisposition {
    // The matching engine branch handled the event without a queued emission.
    Immediate,
    // A specifically audited policy branch ignored the event; not queue loss.
    Ignored,
    Emitted,
    Sentinel,
    Fence
};
enum class NativeDeliveryDisposition { Delivered, AuditedDiscard };

// One owned immutable batch, on the constructing thread. Queue membership and
// this receipt prove ORDINARY disposition only, never text origin/insertion
// authority, physical input, GUI ownership or successful native reconciliation.
//
// Begin copies bounded record metadata from an actual ingress-owned batch, then
// binds that exact ingress/source for its entire lifetime (both and the probe
// must outlive this ledger). It does not retain event/payload pointers.
// OpenRecord visits exact FIFO indices, including Outside/Sentinel/Fence. Issue
// reserves a ticket BEFORE queue admission; all tickets must survive in private
// queue sidecars until actual delivery. A failed/partial enqueue requires Retire.
// SealRecord ends translation. Only then can BeginDelivery authorize the oldest
// ticket, immediately before downstream handling; CompleteDelivery follows the
// handler's successful return or an explicit audited discard. A later record
// cannot begin while any prior ticket is outstanding in legacy Begin mode.
// BeginPlanned instead requires every non-deferred sibling admitted before
// advancing translation, then completes the fixed sinks after SealTranslation.
// Dequeue/admission alone
// never completes delivery. Exceptions, overflow, clear, untracked consumption,
// reentry or lost sidecars require retirement, not an invented discard reason.
//
// Every effect gate and successful-return boundary rechecks the ingress and
// callback-free probe. The owner/route must be checked separately by the caller.
// Seal produces a receipt only after every record and emission is terminal.
// Current is allocation/callback-free and remains usable across a separately
// checked native ACK/ingress Finish until retirement or continuity/claim loss.
// The owner must retire this completed ledger before starting any subsequent
// batch/lifecycle operation; Current is a completed fact, not current ingress.
// No queue, sysEvent_t, journal, native pump, ACK or GUI is modified here.
// No live adapter currently supplies the required admission/delivery sidecars.
//
// All inputs are copied values; outputs/errors must not alias. Outputs are
// unchanged on failure. A protocol failure latches retirement. No reset/reuse
// of a ledger is supported; create another only for a fresh ingress receipt.
class NativeEventDispositionLedger final {
public:
    static constexpr std::size_t MaxEmissions = 16384;
    explicit NativeEventDispositionLedger(NativeDispositionProbe&);
    NativeEventDispositionLedger(const NativeEventDispositionLedger&) = delete;
    NativeEventDispositionLedger& operator=(const NativeEventDispositionLedger&) = delete;
    bool Begin(NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&, std::string& error) noexcept;
    // Opt-in fixed sink accounting. Begin retains the historical serial API.
    // Source records translate in order; admission is an explicit caller report
    // of actual successful storage transfer, never inferred from Issue.
    bool BeginPlanned(NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&, std::string& error) noexcept;
    // The existing overload selects BeforeSessionDrain. This overload binds one
    // immutable four-pass order before translation; unknown values retire.
    bool BeginPlanned(NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&,
        NativeDispositionSchedule, std::string& error) noexcept;
    bool OpenRecord(std::size_t index, NativeDispositionRecord& out, std::string& error) noexcept;
    bool Issue(NativeDispositionRecord, NativeDispositionTicket& out, std::string& error) noexcept;
    bool Issue(NativeDispositionRecord, NativeEmissionPlan, NativeDispositionTicket& out, std::string& error) noexcept;
    bool Admit(NativeDispositionTicket, NativeDispositionAdmission& out, std::string& error) noexcept;
    bool SealTranslation(std::string& error) noexcept;
    // Explicit checkpoint, including sealed zero-ticket passes (which do not
    // imply an uncalled Usercmd loop ran). No next pass before all its
    // admitted tickets complete. Deferred admission occurs only during its
    // exact Keyboard parent delivery; it cannot silently mint a new emission.
    bool CompletePass(NativeDispositionPass, std::string& error) noexcept;
    bool SealRecord(NativeDispositionRecord, NativeRecordDisposition, std::string& error) noexcept;
    bool BeginDelivery(NativeDispositionTicket, std::string& error) noexcept;
    bool CompleteDelivery(NativeDispositionTicket, NativeDeliveryDisposition, std::string& error) noexcept;
    bool Seal(NativeDispositionReceipt& out, std::string& error) noexcept;
    bool Current(NativeDispositionReceipt) const noexcept;
    bool Retire() noexcept;
    bool NeedsRetirement() const noexcept;
private:
    enum class Phase { Empty, Between, Translating, Delivering, Complete, Retired };
    struct Record { OQ4_NativeQueueRecord tag{}; bool ignored = false; };
    struct Emission {
        std::size_t record = 0;
        NativeDispositionPass pass = NativeDispositionPass::SessionInitial;
        std::uint64_t trigger = 0, child = 0, admission = 0;
        bool done = false;
    };
    struct Guard;
    bool Enter(std::string&) noexcept;
    bool Fail(std::string&, const char*) noexcept;
    bool Check(std::string&) noexcept;
    bool Matches(NativeDispositionRecord) const noexcept;
    bool TicketMatches(NativeDispositionTicket) const noexcept;
    bool BeginInternal(NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&, bool,
        NativeDispositionSchedule, std::string&) noexcept;
    bool PlannedTicketMatches(NativeDispositionTicket) const noexcept;
    std::size_t NextEmission(std::size_t cursor, NativeDispositionPass) const noexcept;
    void Advance() noexcept;
    const std::thread::id thread;
    NativeDispositionProbe& probe;
    const std::uint64_t identity;
    NativeQueueIngress* ingress = nullptr;
    NativeQueueSource* source = nullptr;
    NativeDispositionContext context;
    NativeDispositionReceipt receipt;
    std::vector<Record> records;
    std::vector<Emission> emissions;
    std::array<std::size_t, 4> admissionCursor{}, deliveryCursor{};
    bool planned = false, translationClosed = false, passesComplete = false;
    std::array<NativeDispositionPass, 4> passOrder{};
    std::size_t checkpoint = 0;
    NativeDispositionPass currentPass = NativeDispositionPass::SessionInitial;
    std::uint64_t admissionSerial = 0, plannedInFlight = 0;
    Phase phase = Phase::Empty;
    bool calling = false, inFlight = false;
    std::size_t next = 0;
    std::uint64_t issued = 0, completed = 0, first = 0;
};
} // namespace openq4
