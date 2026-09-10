// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/sys/sdl3/NativeEventDisposition.h"
#include "src/sys/EventQueueContinuity.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <utility>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static bool denyAllocation = false;
void* operator new(std::size_t size) {
    if (denyAllocation) throw std::bad_alloc();
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif

using namespace openq4;
static unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#__VA_ARGS__); std::exit(1); } } while (false)

struct Source final : NativeQueueSource, NativeDispositionProbe {
    EventQueueContinuity continuity;
    NativeQueueStatus status;
    std::vector<std::pair<SDL_Event, OQ4_NativeQueueRecord>> events;
    std::function<void()> observe;
    std::function<void()> afterPoll;
    unsigned failPoll = 0, denyAfterPoll = 0;
    bool copyFailure = false;
    std::size_t head = 0;
    unsigned observations = 0, polls = 0, copies = 0;
    bool unavailable = false, claim = true;
    Source() {
        status.mainThread = status.healthy = true;
        status.providerEpoch = 7; status.generation = 13;
        status.engineToken = continuity.Token(); status.fenceEventType = SDL_EVENT_USER + 12;
    }
    bool Current(const NativeDispositionContext& context) const noexcept override {
        return claim && status.mainThread && status.healthy && context.providerEpoch == status.providerEpoch &&
            context.generation == status.generation && continuity.Matches(context.engineToken);
    }
    bool Observe(NativeQueueStatus& out, std::string&) override {
        ++observations;
        auto callback = std::move(observe);
        if (callback) callback();
        if (unavailable) return false;
        status.engineToken = continuity.Token(); out = status; return true;
    }
    int Poll(SDL_Event& event, OQ4_NativeQueueRecord& record) override {
        ++polls;
        if (failPoll == polls) return -1;
        if (head == events.size()) return 0;
        event = events[head].first; record = events[head++].second;
        auto callback = std::move(afterPoll); if (callback) callback();
        if (denyAfterPoll == polls) denyAllocation = true;
        return 1;
    }
    bool CopyFence(const SDL_Event& event, OQ4_NativeFence& out) override {
        ++copies;
        if (copyFailure || !status.pending || event.type != status.fenceEventType) return false;
        out = *status.pending; return true;
    }
    void Add(Uint32 kind, Uint32 type, Uint64 sequence, Uint32 ordinal = 0) {
        SDL_Event event{}; event.type = type;
        OQ4_NativeQueueRecord tag{1, kind, ordinal, 0, status.generation, sequence, 0, 0};
        if (kind == OQ4_QUEUE_COLLECTION || kind == OQ4_QUEUE_FENCE) tag.dispatch = status.pending->dispatch;
        if (kind == OQ4_QUEUE_FENCE) { tag.fence_sequence = status.pending->sequence; event.user.code = 1; }
        events.push_back({event, tag});
    }
};
struct Fixture {
    Source source;
    NativeQueueIngress ingress;
    std::unique_ptr<const NativeQueueBatch> batch;
    NativeEventDispositionLedger ledger{source};
    std::string error;
    explicit Fixture(bool mixed = false, bool fenceOnly = false) {
        if (mixed || fenceOnly) source.status.pending = OQ4_NativeFence{1, mixed ? 2u : 0u, 111, 333};
        if (mixed) {
            source.Add(OQ4_QUEUE_OUTSIDE, SDL_EVENT_KEY_DOWN, 1);
            source.Add(OQ4_QUEUE_SENTINEL, SDL_EVENT_POLL_SENTINEL, 3); // legal sentinel maintenance gap
            source.Add(OQ4_QUEUE_COLLECTION, SDL_EVENT_KEY_UP, 4, 1);
            source.Add(OQ4_QUEUE_OUTSIDE, SDL_EVENT_CLIPBOARD_UPDATE, 5);
            source.Add(OQ4_QUEUE_COLLECTION, SDL_EVENT_MOUSE_WHEEL, 6, 2);
            source.Add(OQ4_QUEUE_FENCE, source.status.fenceEventType, 7, 2);
        } else if (fenceOnly) source.Add(OQ4_QUEUE_FENCE, source.status.fenceEventType, 1);
        else source.Add(OQ4_QUEUE_OUTSIDE, SDL_EVENT_KEY_DOWN, 1);
        CHECK(ingress.Read(source, batch, error) == NativeQueueRead::Ready);
    }
    void Begin() { CHECK(ledger.Begin(ingress, source, *batch, error)); }
    NativeDispositionRecord Open(std::size_t index = 0) {
        NativeDispositionRecord value; CHECK(ledger.OpenRecord(index, value, error)); return value;
    }
    NativeDispositionTicket Issue(NativeDispositionRecord record) {
        NativeDispositionTicket value; CHECK(ledger.Issue(record, value, error)); return value;
    }
    void Deliver(NativeDispositionTicket ticket, NativeDeliveryDisposition how = NativeDeliveryDisposition::Delivered) {
        CHECK(ledger.BeginDelivery(ticket, error)); CHECK(ledger.CompleteDelivery(ticket, how, error));
    }
    NativeDispositionReceipt Finish() {
        NativeDispositionReceipt value; CHECK(ledger.Seal(value, error)); CHECK(ledger.Current(value)); return value;
    }
};
static void OrderedBatch() {
    Fixture f(true); f.Begin();
    const auto polls = f.source.polls;
    auto first = f.Open();
    auto a = f.Issue(first); auto b = f.Issue(first);
    CHECK(!f.ledger.Current(first.receipt));
    CHECK(f.ledger.SealRecord(first, NativeRecordDisposition::Emitted, f.error));
    f.Deliver(a); f.Deliver(b, NativeDeliveryDisposition::AuditedDiscard);
    auto sentinel = f.Open(1);
    CHECK(f.ledger.SealRecord(sentinel, NativeRecordDisposition::Sentinel, f.error));
    auto key = f.Open(2); CHECK(f.ledger.SealRecord(key, NativeRecordDisposition::Immediate, f.error));
    auto ignored = f.Open(3); CHECK(f.ledger.SealRecord(ignored, NativeRecordDisposition::Ignored, f.error));
    auto wheel = f.Open(4);
    std::vector<NativeDispositionTicket> emitted;
    for (unsigned i = 0; i < 12; ++i) emitted.push_back(f.Issue(wheel));
    CHECK(f.ledger.SealRecord(wheel, NativeRecordDisposition::Emitted, f.error));
    for (auto ticket : emitted) f.Deliver(ticket);
    auto fence = f.Open(5); CHECK(f.ledger.SealRecord(fence, NativeRecordDisposition::Fence, f.error));
    auto receipt = f.Finish();
    CHECK(receipt.batch == f.batch->Receipt());
    CHECK(f.source.polls == polls && f.source.copies == 1); // ledger never consumes or copies a fence
    // Receipt stays current across separately authorized ACK and ingress Finish.
    f.source.status.pending.reset(); CHECK(f.ingress.Finish(f.source, f.batch->Receipt(), f.error));
    CHECK(f.ledger.Current(receipt)); CHECK(f.ledger.Retire()); CHECK(!f.ledger.Current(receipt));
}
static void EmptyAndIgnored() {
    Fixture fence(false, true); fence.Begin(); auto root = fence.Open();
    CHECK(fence.ledger.SealRecord(root, NativeRecordDisposition::Fence, fence.error)); fence.Finish();
    Fixture outside; outside.Begin(); auto record = outside.Open();
    CHECK(outside.ledger.SealRecord(record, NativeRecordDisposition::Ignored, outside.error)); outside.Finish();
    Fixture immediate; immediate.Begin(); record = immediate.Open();
    CHECK(immediate.ledger.SealRecord(record, NativeRecordDisposition::Immediate, immediate.error)); immediate.Finish();
}
static void RecordFailures() {
    for (int which = 0; which < 11; ++which) {
        Fixture f(true); f.Begin(); auto record = f.Open();
        NativeDispositionRecord out{{{987,654},321,9},88,33}; const auto old = out;
        NativeDispositionTicket ticket{{{{11,12},13,14},15,16},17}; const auto oldTicket = ticket;
        switch (which) {
        case 0: CHECK(!f.ledger.OpenRecord(1, out, f.error)); CHECK(out == old); break;
        case 1: ++record.index; CHECK(!f.ledger.Issue(record, ticket, f.error)); CHECK(ticket == oldTicket); break;
        case 2: ++record.queueSequence; CHECK(!f.ledger.SealRecord(record, NativeRecordDisposition::Immediate, f.error)); break;
        case 3: ++record.receipt.batch.serial; CHECK(!f.ledger.Issue(record, ticket, f.error)); break;
        case 4: ++record.receipt.ledger; CHECK(!f.ledger.Issue(record, ticket, f.error)); break;
        case 5: ++record.receipt.serial; CHECK(!f.ledger.Issue(record, ticket, f.error)); break;
        case 6: CHECK(!f.ledger.SealRecord(record, NativeRecordDisposition::Fence, f.error)); break;
        case 7: CHECK(!f.ledger.SealRecord(record, NativeRecordDisposition::Emitted, f.error)); break;
        case 8: f.Issue(record); CHECK(!f.ledger.SealRecord(record, NativeRecordDisposition::Ignored, f.error)); break;
        case 9: CHECK(!f.ledger.SealRecord(record, static_cast<NativeRecordDisposition>(90), f.error)); break;
        case 10: CHECK(!f.ledger.Begin(f.ingress, f.source, *f.batch, f.error)); break;
        }
        CHECK(f.ledger.NeedsRetirement());
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        Fixture f(true); f.Begin();
        for (std::size_t index = 0; index < (kind == 0 ? 1u : kind == 1 ? 3u : 5u); ++index) {
            auto record = f.Open(index);
            const auto type = f.batch->Events()[index].record.kind;
            const auto disposition = type == OQ4_QUEUE_SENTINEL ? NativeRecordDisposition::Sentinel : NativeRecordDisposition::Ignored;
            CHECK(f.ledger.SealRecord(record, disposition, f.error));
        }
        auto special = f.Open(kind == 0 ? 1 : kind == 1 ? 3 : 5);
        NativeDispositionTicket output;
        CHECK(!f.ledger.Issue(special, output, f.error)); CHECK(f.ledger.NeedsRetirement());
    }
    Fixture reordered(true); reordered.Begin(); NativeDispositionRecord record;
    CHECK(!reordered.ledger.OpenRecord(2, record, reordered.error));
    Fixture special(false, true); special.Begin(); auto fence = special.Open();
    CHECK(!special.ledger.SealRecord(fence, NativeRecordDisposition::Ignored, special.error));
}
static void DeliveryFailures() {
    for (int which = 0; which < 13; ++which) {
        Fixture f; f.Begin(); auto record = f.Open(); auto a = f.Issue(record); auto b = f.Issue(record);
        if (which == 0) { CHECK(!f.ledger.BeginDelivery(a, f.error)); CHECK(f.ledger.NeedsRetirement()); continue; }
        CHECK(f.ledger.SealRecord(record, NativeRecordDisposition::Emitted, f.error));
        NativeDispositionReceipt out{{6,7},8,9}; const auto before = out;
        switch (which) {
        case 1: CHECK(!f.ledger.CompleteDelivery(a, NativeDeliveryDisposition::Delivered, f.error)); break;
        case 2: CHECK(!f.ledger.BeginDelivery(b, f.error)); break;
        case 3: a.emission = 0; CHECK(!f.ledger.BeginDelivery(a, f.error)); break;
        case 4: a.emission = 3; CHECK(!f.ledger.BeginDelivery(a, f.error)); break;
        case 5: ++a.record.queueSequence; CHECK(!f.ledger.BeginDelivery(a, f.error)); break;
        case 6: CHECK(f.ledger.BeginDelivery(a, f.error)); CHECK(!f.ledger.BeginDelivery(a, f.error)); break;
        case 7: CHECK(f.ledger.BeginDelivery(a, f.error)); CHECK(!f.ledger.CompleteDelivery(b, NativeDeliveryDisposition::Delivered, f.error)); break;
        case 8: f.Deliver(a); CHECK(!f.ledger.BeginDelivery(a, f.error)); break;
        case 9: f.Deliver(a); CHECK(!f.ledger.Seal(out, f.error)); CHECK(out == before); break;
        case 10: CHECK(f.ledger.BeginDelivery(a, f.error)); CHECK(!f.ledger.CompleteDelivery(a, static_cast<NativeDeliveryDisposition>(99), f.error)); break;
        case 11: CHECK(f.ledger.BeginDelivery(a, f.error)); CHECK(f.ledger.Retire()); CHECK(!f.ledger.CompleteDelivery(a, NativeDeliveryDisposition::Delivered, f.error)); break;
        case 12: f.Deliver(a); { NativeDispositionRecord next; CHECK(!f.ledger.OpenRecord(1, next, f.error)); } break;
        }
        CHECK(f.ledger.NeedsRetirement());
    }
}
static void ContinuityFailures() {
    for (int which = 0; which < 9; ++which) {
        Fixture f; f.Begin(); auto record = f.Open(); auto ticket = f.Issue(record);
        CHECK(f.ledger.SealRecord(record, NativeRecordDisposition::Emitted, f.error));
        CHECK(f.ledger.BeginDelivery(ticket, f.error));
        switch (which) {
        case 0: f.source.continuity.Invalidate(); break; // exact platform/pushed queue overflow or clear primitive
        case 1: ++f.source.status.generation; break;
        case 2: ++f.source.status.providerEpoch; break;
        case 3: f.source.status.healthy = false; break;
        case 4: f.source.claim = false; break;
        case 5: f.source.unavailable = true; break;
        case 6: f.source.observe = [] { throw 3; }; break;
        case 7: f.source.observe = [&] { f.source.claim = false; }; break;
        case 8: f.source.observe = [&] { f.source.continuity.Invalidate(); }; break;
        }
        CHECK(!f.ledger.CompleteDelivery(ticket, NativeDeliveryDisposition::Delivered, f.error));
        CHECK(f.ledger.NeedsRetirement()); CHECK(!f.ledger.Current(record.receipt));
    }
    Fixture sealed; sealed.Begin(); auto record = sealed.Open();
    CHECK(sealed.ledger.SealRecord(record, NativeRecordDisposition::Immediate, sealed.error)); auto receipt = sealed.Finish();
    const auto calls = sealed.source.observations;
    sealed.source.continuity.Invalidate(); CHECK(!sealed.ledger.Current(receipt)); CHECK(sealed.source.observations == calls);
    Fixture reentrant; reentrant.Begin(); NativeDispositionRecord output{{{6,7},8,9},10,11}; const auto old = output;
    reentrant.source.observe = [&] { NativeDispositionReceipt unused; CHECK(!reentrant.ledger.Seal(unused, reentrant.error)); };
    CHECK(!reentrant.ledger.OpenRecord(0, output, reentrant.error)); CHECK(output == old); CHECK(reentrant.ledger.NeedsRetirement());
    Fixture retire; retire.Begin(); retire.source.observe = [&] { CHECK(retire.ledger.Retire()); };
    CHECK(!retire.ledger.OpenRecord(0, output, retire.error)); CHECK(output == old);
}
static void ReplacementAndBudget() {
    Fixture f; f.Begin(); auto record = f.Open();
    Fixture other; other.Begin(); auto otherRecord = other.Open();
    NativeDispositionTicket ticket;
    CHECK(!other.ledger.Issue(record, ticket, other.error));
    CHECK(!f.ledger.Issue(otherRecord, ticket, f.error));
    Fixture stale; const auto receipt = stale.batch->Receipt();
    CHECK(stale.ingress.Finish(stale.source, receipt, stale.error));
    stale.source.Add(OQ4_QUEUE_OUTSIDE, SDL_EVENT_KEY_UP, 2);
    std::unique_ptr<const NativeQueueBatch> newer;
    CHECK(stale.ingress.Read(stale.source, newer, stale.error) == NativeQueueRead::Ready);
    CHECK(!stale.ledger.Begin(stale.ingress, stale.source, *stale.batch, stale.error));
    Fixture maximum; maximum.Begin(); record = maximum.Open();
    for (std::size_t n = 0; n < NativeEventDispositionLedger::MaxEmissions; ++n) {
        auto emitted = maximum.Issue(record); CHECK(emitted.emission == n + 1);
    }
    NativeDispositionTicket sentinel{{{{11,12},13,14},15,16},17}; const auto before = sentinel;
    CHECK(!maximum.ledger.Issue(record, sentinel, maximum.error)); CHECK(sentinel == before);
    CHECK(maximum.ledger.NeedsRetirement());
    Fixture immutable; immutable.Begin();
    immutable.source.events[0].second.queue_sequence = 900; // source storage no longer borrowed
    record = immutable.Open(); CHECK(record.queueSequence == 1);
    CHECK(immutable.ledger.SealRecord(record, NativeRecordDisposition::Immediate, immutable.error)); immutable.Finish();
}
static void ThreadsAndAllocation() {
    Fixture f; f.Begin(); auto record = f.Open(); auto ticket = f.Issue(record);
    CHECK(f.ledger.SealRecord(record, NativeRecordDisposition::Emitted, f.error)); f.Deliver(ticket); auto receipt = f.Finish();
    bool current = true, retired = true, seal = true;
    std::thread worker([&] { std::string error; NativeDispositionReceipt output; current = f.ledger.Current(receipt);
        retired = f.ledger.Retire(); seal = f.ledger.Seal(output, error); }); worker.join();
    CHECK(!current && !retired && !seal); CHECK(f.ledger.Current(receipt));
    // Prepare all STL values before denial, including MSVC debug string proxies.
    bool live = false, stopped = false;
    denyAllocation = true; live = f.ledger.Current(receipt); stopped = f.ledger.Retire(); denyAllocation = false;
    CHECK(live && stopped); CHECK(!f.ledger.Current(receipt));
    Fixture noAllocation; NativeDispositionRecord output; NativeDispositionTicket emission; NativeDispositionReceipt done;
    noAllocation.Begin();
    denyAllocation = true;
    const bool open = noAllocation.ledger.OpenRecord(0, output, noAllocation.error);
    const bool issue = noAllocation.ledger.Issue(output, emission, noAllocation.error);
    const bool sealed = noAllocation.ledger.SealRecord(output, NativeRecordDisposition::Emitted, noAllocation.error);
    const bool begin = noAllocation.ledger.BeginDelivery(emission, noAllocation.error);
    const bool end = noAllocation.ledger.CompleteDelivery(emission, NativeDeliveryDisposition::Delivered, noAllocation.error);
    const bool finish = noAllocation.ledger.Seal(done, noAllocation.error);
    denyAllocation = false;
    CHECK(open && issue && sealed && begin && end && finish);
    Fixture oom;
    denyAllocation = true; const bool bound = oom.ledger.Begin(oom.ingress, oom.source, *oom.batch, oom.error); denyAllocation = false;
    CHECK(!bound && oom.ledger.NeedsRetirement());
    bool constructed = false, allocationFailed = false;
    const auto observed = oom.source.observations;
    try { denyAllocation = true; NativeEventDispositionLedger empty(oom.source); constructed = true; }
    catch (const std::bad_alloc&) { allocationFailed = true; }
    denyAllocation = false;
    // Release STL needs no empty proxy; debug STL may throw. Neither may
    // terminate in an inherited noexcept container constructor or touch source.
    CHECK(constructed != allocationFailed); CHECK(oom.source.observations == observed);
}
static void FinalBoundaryFailures() {
    for (int stage = 0; stage < 4; ++stage) {
        Fixture f;
        if (stage == 0) {
            f.source.observe = [&] { CHECK(f.ledger.Retire()); };
            CHECK(!f.ledger.Begin(f.ingress, f.source, *f.batch, f.error));
        } else {
            f.Begin(); auto record = f.Open(); auto ticket = f.Issue(record);
            CHECK(f.ledger.SealRecord(record, NativeRecordDisposition::Emitted, f.error));
            CHECK(f.ledger.BeginDelivery(ticket, f.error));
            if (stage == 1) {
                f.source.observe = [&] { CHECK(f.ledger.Retire()); };
                CHECK(!f.ledger.CompleteDelivery(ticket, NativeDeliveryDisposition::Delivered, f.error));
            } else {
                CHECK(f.ledger.CompleteDelivery(ticket, NativeDeliveryDisposition::Delivered, f.error));
                NativeDispositionReceipt output{{6,7},8,9}; const auto before = output;
                if (stage == 2) f.source.observe = [&] { CHECK(f.ledger.Retire()); };
                else f.source.observe = [&] { NativeDispositionRecord unused; CHECK(!f.ledger.OpenRecord(0, unused, f.error)); };
                CHECK(!f.ledger.Seal(output, f.error)); CHECK(output == before);
            }
        }
        CHECK(f.ledger.NeedsRetirement());
    }
    Fixture a; a.Begin(); auto record = a.Open();
    CHECK(a.ledger.SealRecord(record, NativeRecordDisposition::Immediate, a.error)); auto receipt = a.Finish();
    for (int which = 0; which < 4; ++which) {
        auto forged = receipt;
        if (which == 0) ++forged.batch.ingress;
        if (which == 1) ++forged.batch.serial;
        if (which == 2) ++forged.ledger;
        if (which == 3) ++forged.serial;
        CHECK(!a.ledger.Current(forged)); CHECK(a.ledger.Current(receipt));
    }
    Fixture b; b.Begin(); record = b.Open();
    CHECK(b.ledger.SealRecord(record, NativeRecordDisposition::Ignored, b.error)); auto other = b.Finish();
    CHECK(!a.ledger.Current(other)); CHECK(!b.ledger.Current(receipt));
    Fixture released;
    const auto original = released.batch->Receipt();
    released.source.observe = [&] { released.batch.reset(); };
    CHECK(released.ledger.Begin(released.ingress, released.source, *released.batch, released.error));
    CHECK(!released.batch);
    record = released.Open(); CHECK(record.receipt.batch == original && record.queueSequence == 1);
    CHECK(released.ledger.SealRecord(record, NativeRecordDisposition::Immediate, released.error)); released.Finish();
}
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    for (int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
    OrderedBatch(); EmptyAndIgnored(); RecordFailures(); DeliveryFailures(); ContinuityFailures();
    ReplacementAndBudget(); ThreadsAndAllocation(); FinalBoundaryFailures();
    std::printf("PASS %u checks\n", checks);
    return 0;
}
