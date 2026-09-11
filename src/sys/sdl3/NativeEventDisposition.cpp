// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeEventDisposition.h"
#include <atomic>
#include <limits>

namespace openq4 {
namespace {
constexpr std::array<NativeDispositionPass,4> SessionFirstOrder{
    NativeDispositionPass::SessionInitial, NativeDispositionPass::MousePoll,
    NativeDispositionPass::KeyboardPoll, NativeDispositionPass::SessionDeferred};
constexpr std::array<NativeDispositionPass,4> PollFirstOrder{
    NativeDispositionPass::MousePoll, NativeDispositionPass::KeyboardPoll,
    NativeDispositionPass::SessionInitial, NativeDispositionPass::SessionDeferred};
std::atomic<std::uint64_t> nextLedger{1};
std::uint64_t Identity() noexcept {
    auto value = nextLedger.load();
    while (value && !nextLedger.compare_exchange_weak(value,
        value == (std::numeric_limits<std::uint64_t>::max)() ? 0 : value + 1)) {}
    return value;
}
void Error(std::string& error, const char* message) noexcept {
    try { error = message; } catch (...) { error.clear(); }
}
}
struct NativeEventDispositionLedger::Guard {
    NativeEventDispositionLedger& ledger;
    ~Guard() { ledger.calling = false; }
};
NativeEventDispositionLedger::NativeEventDispositionLedger(NativeDispositionProbe& value)
    : thread(std::this_thread::get_id()), probe(value), identity(Identity()), records(0), emissions(0) {}
bool NativeEventDispositionLedger::Fail(std::string& error, const char* message) noexcept {
    phase = Phase::Retired;
    Error(error, message);
    return false;
}
bool NativeEventDispositionLedger::Enter(std::string& error) noexcept {
    if (thread != std::this_thread::get_id()) {
        Error(error, "Native disposition is owned by another thread"); return false;
    }
    if (calling) return Fail(error, "Reentrant native disposition requires retirement");
    if (phase == Phase::Retired) { Error(error, "Native disposition requires retirement"); return false; }
    calling = true; return true;
}
bool NativeEventDispositionLedger::Check(std::string& error) noexcept {
    try {
        if (phase == Phase::Retired || !probe.Current(context) || !ingress || !source ||
            !ingress->Validate(*source, receipt.batch, error) || phase == Phase::Retired ||
            !probe.Current(context)) return Fail(error, "Native disposition continuity or provider changed");
        return true;
    } catch (...) { return Fail(error, "Native disposition source failed"); }
}
bool NativeEventDispositionLedger::Begin(NativeQueueIngress& input, NativeQueueSource& provider,
    const NativeQueueBatch& batch, std::string& error) noexcept {
    return BeginInternal(input, provider, batch, false, NativeDispositionSchedule::BeforeSessionDrain, error);
}
bool NativeEventDispositionLedger::BeginPlanned(NativeQueueIngress& input, NativeQueueSource& provider,
    const NativeQueueBatch& batch, std::string& error) noexcept {
    return BeginPlanned(input, provider, batch, NativeDispositionSchedule::BeforeSessionDrain, error);
}
bool NativeEventDispositionLedger::BeginPlanned(NativeQueueIngress& input, NativeQueueSource& provider,
    const NativeQueueBatch& batch, NativeDispositionSchedule schedule, std::string& error) noexcept {
    return BeginInternal(input, provider, batch, true, schedule, error);
}
bool NativeEventDispositionLedger::BeginInternal(NativeQueueIngress& input, NativeQueueSource& provider,
    const NativeQueueBatch& batch, bool sinkPlan, NativeDispositionSchedule schedule, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (phase != Phase::Empty || !identity) return Fail(error, "Native disposition is not a fresh ledger");
    if (schedule != NativeDispositionSchedule::BeforeSessionDrain && schedule != NativeDispositionSchedule::AtMousePollEntry)
        return Fail(error, "Unsupported native disposition schedule");
    // Seal/input identity is supplied by actual NativeQueueIngress, not a caller
    // array of self-declared tags. All allocation occurs before the first effect.
    try {
        const auto& status = batch.Status();
        ingress = &input; source = &provider;
        context = {status.providerEpoch, status.generation, status.engineToken};
        receipt = {batch.Receipt(), identity, 1};
        if (batch.Events().empty() || batch.Events().size() > NativeQueueIngress::MaxEvents)
            return Fail(error, "Invalid or unavailable native disposition batch");
        // The sized constructor is allowed to throw even when MSVC debug
        // iterators allocate an empty container proxy. Its noexcept default
        // constructor would terminate before this operation can report OOM.
        std::vector<Record> candidate(0);
        candidate.reserve(batch.Events().size());
        for (const auto& event : batch.Events()) candidate.push_back({event.record, event.ignored});
        if (sinkPlan) emissions.reserve(MaxEmissions);
        // No reference into the caller's batch is used across Source::Observe.
        // Even releasing that owned object cannot replace the copied identity.
        if (!Check(error)) return false;
        passOrder = schedule == NativeDispositionSchedule::AtMousePollEntry ? PollFirstOrder : SessionFirstOrder;
        currentPass = passOrder[0];
        records.swap(candidate); planned = sinkPlan; phase = Phase::Between; error.clear(); return true;
    } catch (...) { return Fail(error, "Native disposition allocation failed"); }
}
bool NativeEventDispositionLedger::Matches(NativeDispositionRecord record) const noexcept {
    return record.receipt == receipt && next < records.size() && record.index == next &&
        record.queueSequence == records[next].tag.queue_sequence;
}
bool NativeEventDispositionLedger::TicketMatches(NativeDispositionTicket ticket) const noexcept {
    return Matches(ticket.record) && ticket.emission > first && ticket.emission <= issued &&
        ticket.emission == completed + 1;
}
void NativeEventDispositionLedger::Advance() noexcept {
    if (completed == issued && !inFlight) { ++next; phase = Phase::Between; }
}
bool NativeEventDispositionLedger::OpenRecord(std::size_t index, NativeDispositionRecord& out, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (phase != Phase::Between || index != next || next >= records.size())
        return Fail(error, "Native disposition record order mismatch");
    if (!Check(error)) return false;
    const NativeDispositionRecord result{receipt, records[next].tag.queue_sequence, next};
    first = issued; phase = Phase::Translating; out = result; error.clear(); return true;
}
bool NativeEventDispositionLedger::Issue(NativeDispositionRecord record, NativeDispositionTicket& out, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (planned || phase != Phase::Translating || !Matches(record) || records[next].ignored ||
        records[next].tag.kind == OQ4_QUEUE_SENTINEL || records[next].tag.kind == OQ4_QUEUE_FENCE || issued == MaxEmissions)
        return Fail(error, "Native disposition emission is invalid or exceeds budget");
    if (!Check(error)) return false;
    const NativeDispositionTicket result{record, issued + 1};
    ++issued; out = result; error.clear(); return true;
}
bool NativeEventDispositionLedger::PlannedTicketMatches(NativeDispositionTicket ticket) const noexcept {
    if (!planned || ticket.record.receipt != receipt || !ticket.emission || ticket.emission > emissions.size()) return false;
    const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
    return ticket.record.index == emission.record && emission.record < records.size() &&
        ticket.record.queueSequence == records[emission.record].tag.queue_sequence;
}
std::size_t NativeEventDispositionLedger::NextEmission(std::size_t cursor, NativeDispositionPass pass) const noexcept {
    while (cursor < emissions.size() && emissions[cursor].pass != pass) ++cursor;
    return cursor;
}
bool NativeEventDispositionLedger::Issue(NativeDispositionRecord record, NativeEmissionPlan plan,
    NativeDispositionTicket& out, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    const auto pass = static_cast<unsigned>(plan.pass);
    if (!planned || phase != Phase::Translating || !Matches(record) || records[next].ignored ||
        records[next].tag.kind == OQ4_QUEUE_SENTINEL || records[next].tag.kind == OQ4_QUEUE_FENCE ||
        pass > static_cast<unsigned>(NativeDispositionPass::SessionDeferred) || emissions.size() == MaxEmissions)
        return Fail(error, "Invalid fixed-sink emission plan");
    std::uint64_t trigger = 0;
    if (plan.pass == NativeDispositionPass::SessionDeferred) {
        if (!PlannedTicketMatches(plan.trigger) || plan.trigger.record != record)
            return Fail(error, "Deferred Session emission requires its exact Keyboard parent");
        const auto& parent = emissions[static_cast<std::size_t>(plan.trigger.emission - 1)];
        if (parent.pass != NativeDispositionPass::KeyboardPoll || parent.child)
            return Fail(error, "Deferred Session parent is not an unused Keyboard ticket");
        trigger = plan.trigger.emission;
    } else if (plan.trigger != NativeDispositionTicket{}) return Fail(error, "Unexpected deferred emission parent");
    if (!Check(error)) return false;
    const NativeDispositionTicket result{record, issued + 1};
    emissions.push_back({next, plan.pass, trigger, 0, 0, false}); // Pre-reserved, trivial values only.
    if (trigger) emissions[static_cast<std::size_t>(trigger - 1)].child = result.emission;
    ++issued; out = result; error.clear(); return true;
}
bool NativeEventDispositionLedger::AdmissionMatches(NativeDispositionTicket ticket) const noexcept {
    if (!PlannedTicketMatches(ticket)) return false;
    const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
    const auto pass = static_cast<std::size_t>(emission.pass);
    const bool deferred = emission.pass == NativeDispositionPass::SessionDeferred;
    return !emission.admission && ticket.emission - 1 == NextEmission(admissionCursor[pass], emission.pass) &&
        ((!deferred && phase == Phase::Translating && emission.record == next) ||
         (deferred && phase == Phase::Delivering && translationClosed && currentPass == NativeDispositionPass::KeyboardPoll &&
          inFlight && plannedInFlight == emission.trigger));
}
bool NativeEventDispositionLedger::CanAdmit(NativeDispositionTicket ticket) const noexcept {
    return thread == std::this_thread::get_id() && !calling && AdmissionMatches(ticket);
}
bool NativeEventDispositionLedger::Admit(NativeDispositionTicket ticket, NativeDispositionAdmission& out, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (!AdmissionMatches(ticket)) return Fail(error, "Admission is duplicate, reordered or outside its exact source/Keyboard delivery");
    auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
    const auto pass = static_cast<std::size_t>(emission.pass);
    if (!Check(error)) return false;
    const NativeDispositionAdmission result{ticket, admissionSerial + 1};
    emission.admission = ++admissionSerial;
    admissionCursor[pass] = static_cast<std::size_t>(ticket.emission);
    out = result; error.clear(); return true;
}
bool NativeEventDispositionLedger::SealTranslation(std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (!planned || phase != Phase::Between || next != records.size() || translationClosed)
        return Fail(error, "Fixed-sink translation is incomplete or already sealed");
    for (const auto& emission : emissions)
        if (emission.pass != NativeDispositionPass::SessionDeferred && !emission.admission)
            return Fail(error, "Translation has an unadmitted emission");
    if (!Check(error)) return false;
    translationClosed = true; phase = Phase::Delivering; error.clear(); return true;
}
bool NativeEventDispositionLedger::CompletePass(NativeDispositionPass pass, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (!planned || phase != Phase::Delivering || !translationClosed || inFlight || pass != currentPass ||
        NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass) != emissions.size())
        return Fail(error, "Fixed-sink pass has not reached its exact terminal checkpoint");
    if (!Check(error)) return false;
    if (++checkpoint == passOrder.size()) { passesComplete = true; phase = Phase::Between; }
    else currentPass = passOrder[checkpoint];
    error.clear(); return true;
}
bool NativeEventDispositionLedger::SealRecord(NativeDispositionRecord record, NativeRecordDisposition disposition, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (phase != Phase::Translating || !Matches(record)) return Fail(error, "Invalid native record seal");
    const auto kind = records[next].tag.kind;
    bool valid = false;
    if (kind == OQ4_QUEUE_SENTINEL) valid = disposition == NativeRecordDisposition::Sentinel;
    else if (kind == OQ4_QUEUE_FENCE) valid = disposition == NativeRecordDisposition::Fence;
    else if (records[next].ignored) valid = disposition == NativeRecordDisposition::Ignored;
    else if (issued != first) valid = disposition == NativeRecordDisposition::Emitted;
    else valid = disposition == NativeRecordDisposition::Immediate || disposition == NativeRecordDisposition::Ignored;
    if (!valid) return Fail(error, "Native record disposition does not match its effects");
    if (planned) {
        for (auto index = first; index < issued; ++index) {
            const auto& emission = emissions[static_cast<std::size_t>(index)];
            if (emission.pass != NativeDispositionPass::SessionDeferred && !emission.admission)
                return Fail(error, "Source record has an unadmitted sibling emission");
        }
    }
    if (!Check(error)) return false;
    if (planned) { ++next; phase = Phase::Between; }
    else { phase = Phase::Delivering; Advance(); }
    error.clear(); return true;
}
bool NativeEventDispositionLedger::CanBeginDelivery(NativeDispositionTicket ticket) const noexcept {
    if (thread != std::this_thread::get_id() || calling || !planned || phase != Phase::Delivering ||
        inFlight || !PlannedTicketMatches(ticket)) return false;
    const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
    return translationClosed && emission.pass == currentPass && emission.admission && !emission.done &&
        ticket.emission - 1 == NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass);
}
bool NativeEventDispositionLedger::BeginDelivery(NativeDispositionTicket ticket, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (planned) {
        if (phase != Phase::Delivering || inFlight || !PlannedTicketMatches(ticket))
            return Fail(error, "Invalid fixed-sink delivery ticket");
        const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
        if (!translationClosed || emission.pass != currentPass || !emission.admission || emission.done ||
            ticket.emission - 1 != NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass))
            return Fail(error, "Fixed-sink delivery is unadmitted, duplicate or reordered");
        if (!Check(error)) return false;
        inFlight = true; plannedInFlight = ticket.emission; error.clear(); return true;
    }
    if (phase != Phase::Delivering || inFlight || !TicketMatches(ticket))
        return Fail(error, "Unknown, duplicate or reordered native delivery ticket");
    if (!Check(error)) return false;
    inFlight = true; error.clear(); return true;
}
bool NativeEventDispositionLedger::CompleteDelivery(NativeDispositionTicket ticket, NativeDeliveryDisposition disposition, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (planned) {
        if (phase != Phase::Delivering || !inFlight || plannedInFlight != ticket.emission || !PlannedTicketMatches(ticket) ||
            (disposition != NativeDeliveryDisposition::Delivered && disposition != NativeDeliveryDisposition::AuditedDiscard))
            return Fail(error, "Fixed-sink completion does not match its active ticket");
        auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
        if (emission.child && !emissions[static_cast<std::size_t>(emission.child - 1)].admission)
            return Fail(error, "Keyboard delivery did not admit its reserved Session event");
        if (!Check(error)) return false;
        emission.done = true; deliveryCursor[static_cast<std::size_t>(currentPass)] = static_cast<std::size_t>(ticket.emission);
        ++completed; inFlight = false; plannedInFlight = 0; error.clear(); return true;
    }
    if (phase != Phase::Delivering || !inFlight || !TicketMatches(ticket) ||
        (disposition != NativeDeliveryDisposition::Delivered && disposition != NativeDeliveryDisposition::AuditedDiscard))
        return Fail(error, "Native delivery did not complete its exact active ticket");
    if (!Check(error)) return false;
    ++completed; inFlight = false; Advance(); error.clear(); return true;
}
bool NativeEventDispositionLedger::Seal(NativeDispositionReceipt& out, std::string& error) noexcept {
    if (!Enter(error)) return false;
    Guard guard{*this};
    if (phase != Phase::Between || next != records.size() || inFlight || completed != issued || (planned && !passesComplete))
        return Fail(error, "Native disposition batch is incomplete");
    if (!Check(error)) return false;
    phase = Phase::Complete; out = receipt; error.clear(); return true;
}
bool NativeEventDispositionLedger::Current(NativeDispositionReceipt expected) const noexcept {
    return thread == std::this_thread::get_id() && !calling && phase == Phase::Complete &&
        expected == receipt && probe.Current(context);
}
bool NativeEventDispositionLedger::Retire() noexcept {
    if (thread != std::this_thread::get_id()) return false;
    phase = Phase::Retired; return true;
}
bool NativeEventDispositionLedger::NeedsRetirement() const noexcept {
    return thread == std::this_thread::get_id() && phase == Phase::Retired;
}
bool NativeEventDispositionLedger::InspectIssuedForRetirement(NativeDispositionTicket ticket,
    NativeIssuedEmission& out) const noexcept {
    if (thread != std::this_thread::get_id() || calling || phase != Phase::Retired || !PlannedTicketMatches(ticket)) return false;
    const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];
    NativeIssuedEmission result;
    result.ticket = ticket;
    result.pass = emission.pass;
    result.admissionSerial = emission.admission;
    result.terminal = emission.done;
    result.inFlight = inFlight && plannedInFlight == ticket.emission;
    if (emission.trigger) result.trigger = {ticket.record, emission.trigger};
    out = result;
    return true;
}
bool NativeEventDispositionLedger::QueryRetirement(NativeDispositionRetirement& out) const noexcept {
    if (thread != std::this_thread::get_id() || calling || phase != Phase::Retired || !planned ||
        !receipt.ledger || !receipt.serial || issued != emissions.size()) return false;
    out = {receipt, issued, inFlight ? plannedInFlight : 0};
    return true;
}
} // namespace openq4
