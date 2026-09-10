// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <thread>
#include <vector>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// One full production translation unit, not rewritten method doubles. Keeping
// it in this TU permits a terminal global identity-exhaustion fixture only.
#include "../../../src/framework/NativeInputRoute.cpp"

static std::atomic<long> failAfter{-1};
void* operator new(std::size_t size) {
    auto left = failAfter.load();
    if (left >= 0 && failAfter.fetch_sub(1) == 0) throw std::bad_alloc();
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

using namespace openq4;
using Phase = NativeInputRoute::Phase;
static int checks = 0, failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::fprintf(stderr,"line %d: %s\n",__LINE__,#value); } } while (false)
struct Source final : NativeInputRouteSource {
    NativeInputObservation observation;
    NativeInputRetirement retirement;
    NativeInputIssued issued;
    NativeInputBinding original;
    mutable unsigned observations = 0, retirements = 0, inspections = 0;
    mutable bool bindingMismatch = false;
    bool observationOK = true, retirementOK = true, issuedOK = true;
    std::function<void()> onObserve, onRetirement, onIssued;
    bool Observe(NativeInputObservation& out) const noexcept override {
        ++observations;
        if (onObserve) onObserve();
        if (!observationOK) return false;
        out = observation; return true;
    }
    bool Original(std::uint64_t route, const NativeInputBinding& b) const noexcept {
        const bool equal = route == retirement.route && b.outer == original.outer &&
            b.sessionTransition == original.sessionTransition && b.dispatchEpoch == original.dispatchEpoch &&
            b.streamToken == original.streamToken && b.editor == original.editor &&
            b.native == original.native && b.window == original.window;
        if (!equal) bindingMismatch = true;
        return equal;
    }
    bool Retirement(std::uint64_t route, const NativeInputBinding& b, NativeInputRetirement& out) const noexcept override {
        ++retirements;
        if (!Original(route,b)) return false;
        if (onRetirement) onRetirement();
        if (!retirementOK) return false;
        out = retirement; return true;
    }
    bool InspectIssued(std::uint64_t route, const NativeInputBinding& b,
        const sysEventDispositionTag_t&, NativeInputIssued& out) const noexcept override {
        ++inspections;
        if (!Original(route,b)) return false;
        if (onIssued) onIssued();
        if (!issuedOK) return false;
        out = issued; return true;
    }
    unsigned Calls() const { return observations + retirements + inspections; }
};
struct Fixture {
    Source source;
    NativeInputRoute route{source};
    NativeInputBinding binding;
    std::uint64_t id = 0;
    std::string error;
    Fixture() {
        binding.outer=0x1000; binding.sessionTransition=2; binding.dispatchEpoch=3; binding.streamToken=4;
        binding.window={0x2000,5,6,7,8,9};
        binding.editor={10,11,12,13,5,14,15,"system.gamma_precise"}; binding.native={16,17};
        source.original=binding;
        source.observation={binding.outer,binding.editor.allocation,binding.sessionTransition,
            binding.dispatchEpoch,binding.streamToken,binding.window,true,true,true,true};
        source.retirement.native=binding.native; source.retirement.window=binding.window;
        source.retirement.ui=NativeInputUiRetirement::RetiredExact;
        source.retirement.store=source.retirement.provider=NativeInputNativeRetirement::RetiredExact;
    }
    void Bind() { CHECK(route.Prepare(binding,id,error)); CHECK(id!=0); source.retirement.route=id; }
    void Drain() { Bind(); CHECK(route.Revoke(id)); CHECK(route.MarkDrainOnly(id)); }
    NativeInputHead Head(NativeInputLane lane=NativeInputLane::Platform) {
        NativeInputHead h;
        h.lane=lane; h.sequence=101; h.slot=2;
        h.tag={binding.dispatchEpoch,binding.streamToken,binding.window.module,id,binding.window.window,
            binding.window.lifetime,20,21,22,23,24,0,25};
        h.value={1,30,1,31,0,0,0};
        source.issued={h.tag,NativeInputSink::Session,NativeInputKind::RouteKey,h.value,true,false};
        if(lane==NativeInputLane::Keyboard) source.issued.sink=NativeInputSink::Keyboard;
        if(lane==NativeInputLane::Mouse) { source.issued.sink=NativeInputSink::Mouse; source.issued.kind=NativeInputKind::RouteMouse; }
        return h;
    }
    void ReleasedFacts() { source.retirement.hooksRemoved=source.retirement.controllerReleased=source.retirement.backlogDisposed=true; }
};
static auto Bytes(const NativeInputRoute::CancellationPermit& p) {
    std::array<unsigned char,sizeof(p)> result{};
    std::memcpy(result.data(),&p,sizeof(p));return result;
}

static void BindingAndProbes() {
    for (int field=0;field<22;++field) {
        Fixture f;
        switch(field) {
            case 0:f.binding.outer=0;break; case 1:f.binding.sessionTransition=0;break;
            case 2:f.binding.dispatchEpoch=0;break;case 3:f.binding.streamToken=0;break;
            case 4:f.binding.window.handle=0;break;case 5:f.binding.window.window=0;break;
            case 6:f.binding.window.lifetime=0;break;case 7:f.binding.window.module=0;break;
            case 8:f.binding.window.association=0;break;case 9:f.binding.window.registration=0;break;
            case 10:f.binding.editor.allocation=0;break;case 11:f.binding.editor.backend=0;break;
            case 12:f.binding.editor.document=0;break;case 13:f.binding.editor.modal=0;break;
            case 14:f.binding.editor.window++;break;case 15:f.binding.editor.session=0;break;
            case 16:f.binding.editor.revision=0;break;case 17:f.binding.editor.control.clear();break;
            case 18:f.binding.editor.control=std::string(129,'x');break;
            case 19:f.binding.editor.control=std::string("x\0y",3);break;
            case 20:f.binding.native.document=0;break;case 21:f.binding.native.editorLease=0;break;
        }
        std::uint64_t out=990;
        CHECK(!f.route.Prepare(f.binding,out,f.error)); CHECK(out==990); CHECK(f.source.Calls()==0); CHECK(f.route.State()==Phase::Empty);
    }
    for(int at=0;at<2;++at) for(int field=0;field<16;++field) {
        Fixture f; if(at)f.Bind();
        auto& o=f.source.observation;
        switch(field) {
            case 0:o.current++;break;case 1:o.allocation++;break;case 2:o.sessionTransition++;break;
            case 3:o.dispatchEpoch++;break;case 4:o.streamToken++;break;case 5:o.window.handle++;break;
            case 6:o.window.window++;break;case 7:o.window.lifetime++;break;case 8:o.window.module++;break;
            case 9:o.window.association++;break;case 10:o.window.registration++;break;
            case 11:o.boundThread=false;break;case 12:o.inputAllowed=false;break;
            case 13:o.windowAllowed=false;break;case 14:o.providerOwned=false;break;case 15:f.source.observationOK=false;break;
        }
        if(!at) {
            std::uint64_t token=60;CHECK(!f.route.Prepare(f.binding,token,f.error));
            CHECK(token==60);CHECK(f.route.State()==Phase::Empty);continue;
        }
        NativeInputSelection out{99,98,97,96}, saved=out;
        CHECK(!f.route.Probe(out)); CHECK(out==saved); CHECK(f.route.State()==Phase::Revoked);
        o={f.binding.outer,f.binding.editor.allocation,f.binding.sessionTransition,f.binding.dispatchEpoch,f.binding.streamToken,f.binding.window,true,true,true,true};
        f.source.observationOK=true;
        CHECK(!f.route.Probe(out)); CHECK(out==saved); // Sticky even after address/flags return.
    }
    Fixture f;
    f.source.onObserve=[&] { f.binding.editor.control="replacement"; f.binding.outer++; };
    f.Bind(); f.source.onObserve={};
    NativeInputSelection out;
    failAfter=0; CHECK(f.route.Probe(out)); CHECK(f.route.ProviderCurrent(f.source.original.window)); CHECK(f.route.WindowCurrent(f.source.original.window)); CHECK(failAfter==0); failAfter=-1;
    CHECK(out.current==f.source.original.outer); CHECK(out.allocation==f.source.original.editor.allocation);
    CHECK(f.route.Revoke(f.id)); CHECK(f.route.MarkDrainOnly(f.id)); CHECK(!f.source.bindingMismatch);
    std::uint64_t unchanged=78; CHECK(!f.route.Prepare(f.binding,unchanged,f.error)); CHECK(unchanged==78);
}
static void OriginalProvider() {
    Fixture f;f.Bind();
    f.source.observation.current++; f.source.observation.allocation++;f.source.observation.inputAllowed=false;
    NativeInputSelection out; CHECK(!f.route.Probe(out));
    CHECK(f.route.ProviderCurrent(f.binding.window)); CHECK(f.route.WindowCurrent(f.binding.window));
    f.source.observation.window.handle++;f.source.observation.window.lifetime++;f.source.observation.windowAllowed=false;
    CHECK(f.route.ProviderCurrent(f.binding.window)); CHECK(!f.route.WindowCurrent(f.binding.window));
    auto replacement=f.binding.window;replacement.registration++;
    CHECK(!f.route.ProviderCurrent(replacement));
    f.source.observation.window.registration++; CHECK(!f.route.ProviderCurrent(f.binding.window));
    f.source.observation.window=f.binding.window;f.source.observation.window.module++;
    CHECK(!f.route.ProviderCurrent(f.binding.window));
    f.source.observation.window=f.binding.window;f.source.observation.providerOwned=false;
    CHECK(!f.route.ProviderCurrent(f.binding.window));
}
static void Retirement() {
    for(int field=0;field<16;++field) {
        Fixture f;f.Bind();CHECK(f.route.Revoke(f.id));
        switch(field) {
            case 0:f.source.retirement.ui=NativeInputUiRetirement::Unknown;break;
            case 1:f.source.retirement.ui=NativeInputUiRetirement::Busy;break;
            case 2:f.source.retirement.store=NativeInputNativeRetirement::Unknown;break;
            case 3:f.source.retirement.store=NativeInputNativeRetirement::Busy;break;
            case 4:f.source.retirement.store=NativeInputNativeRetirement::ClaimLost;break;
            case 5:f.source.retirement.provider=NativeInputNativeRetirement::Unknown;break;
            case 6:f.source.retirement.provider=NativeInputNativeRetirement::Busy;break;
            case 7:f.source.retirement.provider=NativeInputNativeRetirement::ClaimLost;break;
            case 8:f.source.retirement.native.document++;break;
            case 9:f.source.retirement.native.editorLease++;break;
            case 10:f.source.retirement.window.registration++;break;
            case 11:f.source.retirement.route++;break;
            case 12:f.source.retirementOK=false;break;
            case 13:f.source.observationOK=false;break;
            case 14:f.source.observation.boundThread=false;break;
            case 15:f.source.retirement.ui=static_cast<NativeInputUiRetirement>(100);break;
        }
        CHECK(!f.route.MarkDrainOnly(f.id));CHECK(f.route.State()==Phase::Revoked);
        f.ReleasedFacts(); CHECK(!f.route.Release(f.id));
    }
    Fixture f;f.Bind();CHECK(!f.route.MarkDrainOnly(f.id)); CHECK(!f.route.Revoke(f.id+1));
    CHECK(f.route.Revoke(f.id));f.source.retirement.ui=NativeInputUiRetirement::AbsentOriginal;
    // The original event thread stays bound even when active storage epochs are gone.
    f.source.observation.dispatchEpoch=0;f.source.observation.streamToken=0;
    CHECK(f.route.MarkDrainOnly(f.id));
    auto h=f.Head();NativeInputRoute::CancellationPermit p;
    CHECK(f.route.PrepareCancellation(h,p)); CHECK(f.route.AllowsCancellation(p,h));
    CHECK(!f.route.Release(f.id));
    for(int field=0;field<3;++field) {
        f.ReleasedFacts();
        if(field==0)f.source.retirement.hooksRemoved=false;
        if(field==1)f.source.retirement.controllerReleased=false;
        if(field==2)f.source.retirement.backlogDisposed=false;
        CHECK(!f.route.Release(f.id));CHECK(f.route.State()==Phase::DrainOnly);
    }
    f.ReleasedFacts();CHECK(f.route.Release(f.id)); CHECK(f.route.State()==Phase::Empty);CHECK(!f.route.AllowsCancellation(p,h));
    f.source.observation.dispatchEpoch=f.binding.dispatchEpoch;f.source.observation.streamToken=f.binding.streamToken;
    const auto old=f.id;f.Bind();CHECK(f.id!=old);CHECK(!f.route.Revoke(old));CHECK(!f.route.AllowsCancellation(p,h));
}
static void Permits() {
    for(auto lane:{NativeInputLane::Platform,NativeInputLane::Pushed,NativeInputLane::Keyboard,NativeInputLane::Mouse}) {
        Fixture f;f.Drain();auto h=f.Head(lane);NativeInputRoute::CancellationPermit p;
        CHECK(!f.route.AllowsCancellation(p,h));CHECK(f.route.PrepareCancellation(h,p));
        const auto calls=f.source.Calls();
        failAfter=0;CHECK(f.route.AllowsCancellation(p,h));CHECK(f.route.AllowsCancellation(p,h));CHECK(failAfter==0);failAfter=-1;
        CHECK(f.source.Calls()==calls); // Predicate only, not a fake storage take.
        auto changed=h;changed.sequence++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.slot++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.lane=static_cast<NativeInputLane>(100);CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.type++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.value++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.value2++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.time++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.payloadLength++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.payload++;CHECK(!f.route.AllowsCancellation(p,changed));
        changed=h;changed.value.deferredEmission++;CHECK(!f.route.AllowsCancellation(p,changed));
        using Member=std::uint64_t sysEventDispositionTag_t::*;
        const Member members[]={&sysEventDispositionTag_t::dispatchEpoch,&sysEventDispositionTag_t::streamToken,&sysEventDispositionTag_t::providerEpoch,
            &sysEventDispositionTag_t::route,&sysEventDispositionTag_t::window,&sysEventDispositionTag_t::windowLifetime,&sysEventDispositionTag_t::ingress,
            &sysEventDispositionTag_t::batchSerial,&sysEventDispositionTag_t::ledger,&sysEventDispositionTag_t::ledgerSerial,&sysEventDispositionTag_t::queueSequence,
            &sysEventDispositionTag_t::recordIndex,&sysEventDispositionTag_t::emission};
        for(auto member:members) {
            changed=h;changed.tag.*member+=1; CHECK(!f.route.AllowsCancellation(p,changed));
            const auto saved=Bytes(p); CHECK(!f.route.PrepareCancellation(changed,p)); CHECK(Bytes(p)==saved);
            CHECK(!f.route.AllowsCancellation(p,h));CHECK(f.route.PrepareCancellation(h,p));
        }
        const auto old=p;CHECK(f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(old,h));
        for(int i=0;i<128;++i) {
            const auto previous=p;CHECK(f.route.Revoke(f.id));CHECK(!f.route.AllowsCancellation(previous,h));
            CHECK(f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(previous,h));CHECK(f.route.AllowsCancellation(p,h));
        }
        Fixture other;other.Drain();CHECK(!other.route.AllowsCancellation(p,h));
    }
    for(int field=0;field<19;++field) {
        Fixture f;f.Drain();auto h=f.Head();NativeInputRoute::CancellationPermit p;
        switch(field) {
            case 0:h.sequence=0;break;case 1:h.slot=8192;break;case 2:h.tag.recordIndex=8192;break;
            case 3:h.value.payloadLength=-1;break;case 4:h.value.payloadLength=1024*1024+1;h.value.payload=20;break;
            case 5:h.value.payloadLength=1;break;case 6:h.value.payload=20;break;
            case 7:h.value.deferredEmission=2;break;case 8:h.lane=static_cast<NativeInputLane>(99);break;
            case 9:f.source.issued.issued=false;break;case 10:f.source.issued.terminal=true;break;
            case 11:f.source.issued.value.value++;break;case 12:f.source.issued.sink=NativeInputSink::Mouse;break;
            case 13:f.source.issued.kind=NativeInputKind::WindowIntent;break;case 14:f.source.issued.kind=NativeInputKind::ProcessIntent;break;
            case 15:f.source.issued.kind=NativeInputKind::Unknown;break;case 16:f.source.issued.kind=static_cast<NativeInputKind>(99);break;
            case 17:f.source.issuedOK=false;break;
            case 18:f.source.issued.inFlight=true;break;
        }
        if(field<9) {f.source.issued.tag=h.tag;f.source.issued.value=h.value;}
        CHECK(!f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(p,h));
    }
    for(auto lane:{NativeInputLane::Platform,NativeInputLane::Pushed,NativeInputLane::Keyboard,NativeInputLane::Mouse}) {
        for(auto sink:{NativeInputSink::Session,NativeInputSink::Keyboard,NativeInputSink::Mouse}) {
            for(auto kind:{NativeInputKind::Unknown,NativeInputKind::RouteKey,NativeInputKind::RouteMouse,
                NativeInputKind::RouteText,NativeInputKind::NativeEcho,NativeInputKind::HeldRelease,
                NativeInputKind::WindowIntent,NativeInputKind::ProcessIntent}) {
                Fixture f;f.Drain();auto h=f.Head(lane);NativeInputRoute::CancellationPermit p;
                f.source.issued.sink=sink;f.source.issued.kind=kind;
                const bool session=(lane==NativeInputLane::Platform||lane==NativeInputLane::Pushed)&&sink==NativeInputSink::Session;
                const bool keyboard=lane==NativeInputLane::Keyboard&&sink==NativeInputSink::Keyboard;
                const bool mouse=lane==NativeInputLane::Mouse&&sink==NativeInputSink::Mouse;
                const bool expected=(session&&(kind==NativeInputKind::RouteKey||kind==NativeInputKind::RouteMouse||kind==NativeInputKind::RouteText||kind==NativeInputKind::NativeEcho||kind==NativeInputKind::HeldRelease))||
                    (keyboard&&(kind==NativeInputKind::RouteKey||kind==NativeInputKind::HeldRelease))||
                    (mouse&&(kind==NativeInputKind::RouteMouse||kind==NativeInputKind::HeldRelease));
                CHECK(f.route.PrepareCancellation(h,p)==expected);CHECK(f.route.AllowsCancellation(p,h)==expected);
            }
        }
    }
    Fixture f;f.Drain();auto h=f.Head();h.value.payloadLength=1024*1024;h.value.payload=0x4000;
    f.source.issued.value=h.value;f.source.issued.kind=NativeInputKind::NativeEcho;
    NativeInputRoute::CancellationPermit p;CHECK(f.route.PrepareCancellation(h,p));CHECK(f.route.AllowsCancellation(p,h));
    h=f.Head(NativeInputLane::Keyboard);h.value.deferredEmission=h.tag.emission+1;f.source.issued.value=h.value;
    CHECK(f.route.PrepareCancellation(h,p));CHECK(f.route.AllowsCancellation(p,h));
    h.value.deferredEmission=h.tag.emission;f.source.issued.value=h.value;CHECK(!f.route.PrepareCancellation(h,p));
    h=f.Head(NativeInputLane::Keyboard);h.slot=512;CHECK(!f.route.PrepareCancellation(h,p));
    h=f.Head(NativeInputLane::Mouse);h.value.payloadLength=1;h.value.payload=20;f.source.issued.value=h.value;
    CHECK(!f.route.PrepareCancellation(h,p));
}
static void ReentryAndCopies() {
    for(int boundary=0;boundary<3;++boundary) {
        Fixture f;f.Drain();auto h=f.Head();NativeInputRoute::CancellationPermit p;
        const auto attack=[&] { NativeInputSelection out{1,2,3,4};const auto saved=out;CHECK(!f.route.Probe(out));CHECK(out==saved); };
        if(boundary==0)f.source.onObserve=attack;
        if(boundary==1)f.source.onRetirement=attack;
        if(boundary==2)f.source.onIssued=attack;
        CHECK(!f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(p,h));
        f.source.onObserve={};f.source.onRetirement={};f.source.onIssued={};f.ReleasedFacts();
        // Already poisoned is not permission to ignore a *new* cleanup reentry.
        f.source.onRetirement=attack;CHECK(!f.route.Release(f.id));CHECK(f.route.State()==Phase::DrainOnly);
        f.source.onRetirement={};CHECK(f.route.Release(f.id));
    }
    for(int boundary=0;boundary<3;++boundary) {
        Fixture f;f.Drain();auto h=f.Head();NativeInputRoute::CancellationPermit p;
        const auto revoke=[&] { CHECK(f.route.Revoke(f.id)); };
        if(boundary==0)f.source.onObserve=revoke;
        if(boundary==1)f.source.onRetirement=revoke;
        if(boundary==2)f.source.onIssued=revoke;
        CHECK(!f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(p,h));
    }
    Fixture f;f.Drain();auto h=f.Head(), original=h;NativeInputRoute::CancellationPermit p;
    f.source.onIssued=[&]{ h.sequence++;h.value.value++; };
    CHECK(f.route.PrepareCancellation(h,p));CHECK(f.route.AllowsCancellation(p,original));CHECK(!f.route.AllowsCancellation(p,h));
    f.source.onIssued=[&]{f.source.retirement.store=NativeInputNativeRetirement::Busy;};
    const auto saved=Bytes(p);CHECK(!f.route.PrepareCancellation(original,p));CHECK(Bytes(p)==saved);CHECK(!f.route.AllowsCancellation(p,original));
    Fixture g;g.source.onObserve=[&] { std::uint64_t out=7;CHECK(!g.route.Prepare(g.binding,out,g.error));CHECK(out==7); };
    std::uint64_t out=9;CHECK(!g.route.Prepare(g.binding,out,g.error));CHECK(out==9);CHECK(g.route.State()==Phase::Empty);
    g.source.onObserve={};CHECK(!g.route.Prepare(g.binding,out,g.error)); // Faulted unbound object cannot silently retry.
}
static void ThreadsAndAllocation() {
    Fixture f;f.Drain();auto h=f.Head();NativeInputRoute::CancellationPermit p;CHECK(f.route.PrepareCancellation(h,p));
    const auto calls=f.source.Calls();
    std::thread wrong([&] {
        NativeInputSelection selected{1,2,3,4};const auto saved=selected;std::uint64_t id=89;std::string error;
        CHECK(!f.route.Prepare(f.binding,id,error));CHECK(id==89);
        CHECK(!f.route.Probe(selected));CHECK(selected==saved);CHECK(f.route.State()==Phase::Unavailable);
        CHECK(!f.route.Revoke(f.id));CHECK(!f.route.MarkDrainOnly(f.id));CHECK(!f.route.Release(f.id));
        CHECK(!f.route.ProviderCurrent(f.binding.window));CHECK(!f.route.WindowCurrent(f.binding.window));
        CHECK(!f.route.PrepareCancellation(h,p));CHECK(!f.route.AllowsCancellation(p,h));
    });wrong.join();
    CHECK(f.source.Calls()==calls);CHECK(f.route.AllowsCancellation(p,h));CHECK(f.route.State()==Phase::DrainOnly);
    f.ReleasedFacts();failAfter=0;CHECK(f.route.Release(f.id));CHECK(failAfter==0);failAfter=-1;
    Fixture bad;std::uint64_t out=67;
    failAfter=0;CHECK(!bad.route.Prepare(bad.binding,out,bad.error));failAfter=-1;
    CHECK(out==67);CHECK(bad.route.State()==Phase::Empty);CHECK(bad.source.Calls()==0);bad.Bind();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    // Release-iterator string copies can be exhaustively injected. MSVC debug
    // default iterator-proxy constructors are noexcept and deliberately terminate
    // on injected failure; its actual no-allocation probes/permit/Release run above.
    for(int at=0;at<5;++at) {
        Fixture candidate;candidate.binding.editor.control=std::string(120,'c');candidate.source.original=candidate.binding;
        std::uint64_t value=88;failAfter=at;
        const bool ok=candidate.route.Prepare(candidate.binding,value,candidate.error);failAfter=-1;
        if(!ok) {CHECK(value==88);CHECK(candidate.route.State()==Phase::Empty);}
        else {CHECK(value!=88);CHECK(candidate.route.State()==Phase::Bound);}
    }
#else
    std::puts("debug iterator broad allocation sweep unsupported; actual no-allocation endpoints tested");
#endif
}
static void FaultedBacklog() {
    Fixture f;f.Bind();NativeInputSelection selected{4,3,2,1};const auto saved=selected;
    f.source.onObserve=[&] {CHECK(!f.route.Probe(selected));};
    CHECK(!f.route.Probe(selected));CHECK(selected==saved);CHECK(f.route.State()==Phase::Revoked);
    f.source.onObserve={};
    CHECK(!f.route.Probe(selected));CHECK(selected==saved);
    CHECK(f.route.ProviderCurrent(f.binding.window));CHECK(f.route.WindowCurrent(f.binding.window));
    const auto original=f.source.observation.window;
    f.source.observation.window.registration++;CHECK(!f.route.ProviderCurrent(f.binding.window));
    f.source.observation.window=original;f.source.observation.window.module++;CHECK(!f.route.ProviderCurrent(f.binding.window));
    f.source.observation.window=original;f.source.observation.window.lifetime++;CHECK(!f.route.WindowCurrent(f.binding.window));
    CHECK(f.route.ProviderCurrent(f.binding.window));
    f.source.observation.window=original;f.source.observation.window.handle++;CHECK(!f.route.WindowCurrent(f.binding.window));
    f.source.observation.window=original;
    f.source.onObserve=[&] {CHECK(!f.route.ProviderCurrent(f.binding.window));};
    CHECK(!f.route.ProviderCurrent(f.binding.window)); // New reentry still refuses cleanup ownership.
    f.source.onObserve={};CHECK(f.route.ProviderCurrent(f.binding.window));
    f.source.retirement.store=NativeInputNativeRetirement::Busy;CHECK(!f.route.MarkDrainOnly(f.id));
    f.source.retirement.store=NativeInputNativeRetirement::RetiredExact;
    f.source.onRetirement=[&] {CHECK(!f.route.MarkDrainOnly(f.id));};
    CHECK(!f.route.MarkDrainOnly(f.id));CHECK(f.route.State()==Phase::Revoked);
    f.source.onRetirement={};CHECK(f.route.MarkDrainOnly(f.id));
    // Simulated storage owns these two original heads; the helper never takes
    // them itself. Driver-held translator facts and actual head are joined.
    auto first=f.Head(), second=first;second.sequence++;second.tag.emission++;
    std::vector<NativeInputHead> backlog{first,second};NativeInputRoute::CancellationPermit permit;
    f.ReleasedFacts();f.source.retirement.backlogDisposed=false;CHECK(!f.route.Release(f.id));
    CHECK(f.route.PrepareCancellation(first,permit));const auto previous=permit;
    f.source.onIssued=[&] {CHECK(!f.route.AllowsCancellation(previous,first));};
    CHECK(!f.route.PrepareCancellation(first,permit));CHECK(!f.route.AllowsCancellation(previous,first));
    f.source.onIssued={};
    for(const auto& head:{first,second}) {
        f.source.issued.tag=head.tag;f.source.issued.value=head.value;
        CHECK(f.route.PrepareCancellation(head,permit));
        const auto calls=f.source.Calls();failAfter=0;
        CHECK(f.route.AllowsCancellation(permit,backlog.front()));CHECK(failAfter==0);failAfter=-1;
        CHECK(f.source.Calls()==calls);backlog.erase(backlog.begin());
        if(!backlog.empty()) CHECK(!f.route.AllowsCancellation(permit,backlog.front()));
        CHECK(!f.route.Probe(selected));
    }
    CHECK(backlog.empty());f.source.retirement.backlogDisposed=true;CHECK(f.route.Release(f.id));
    const auto old=f.id;f.Bind();CHECK(f.id!=old);CHECK(f.route.Probe(selected));CHECK(!f.route.AllowsCancellation(permit,second));
}
static void Saturation() {
    nativeInputRouteHighwater=(std::numeric_limits<std::uint64_t>::max)()-1;
    Fixture f;f.Bind();CHECK(f.id==(std::numeric_limits<std::uint64_t>::max)());
    CHECK(f.route.Revoke(f.id));f.ReleasedFacts();CHECK(f.route.Release(f.id));
    std::uint64_t id=73;CHECK(!f.route.Prepare(f.binding,id,f.error));CHECK(id==73);CHECK(f.route.State()==Phase::Empty);
    Fixture second;CHECK(!second.route.Prepare(second.binding,id,second.error));CHECK(id==73);
}
static void PermitSaturation() {
    // Runner changes only the private counter's initial value to MAX-1. Every
    // method below remains actual production code, including failed rebind.
    Fixture f;f.Drain();auto h=f.Head();NativeInputRoute::CancellationPermit p;
    CHECK(f.route.PrepareCancellation(h,p));CHECK(f.route.AllowsCancellation(p,h));
    const auto saved=Bytes(p);CHECK(!f.route.PrepareCancellation(h,p));CHECK(Bytes(p)==saved);CHECK(!f.route.AllowsCancellation(p,h));
    f.ReleasedFacts();CHECK(f.route.Release(f.id));std::uint64_t id=99;
    CHECK(!f.route.Prepare(f.binding,id,f.error));CHECK(id==99);CHECK(f.route.State()==Phase::Empty);
}
int main(int argc,char** argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_WARN,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    if(argc==2&&std::strcmp(argv[1],"--permit-exhaustion")==0) PermitSaturation();
    else { BindingAndProbes();OriginalProvider();Retirement();Permits();ReentryAndCopies();ThreadsAndAllocation();FaultedBacklog();Saturation(); }
    std::printf("NativeInputRouteTest checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
