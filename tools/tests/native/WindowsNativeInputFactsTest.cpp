#define main OriginalControllerMain
#include "WindowsTextSessionTest.cpp"
#undef main
#include "src/sys/sdl3/WindowsNativeInputRouteSource.h"

// Production source queries join actual controller/store/Interaction facts.
// Only the outer registry/Session/window copied publication boundary is counted
// here; its actual owning methods are exercised by native_input_lifecycle.py.
static Editor* fields=nullptr;
static openq4::NativeSessionPublication sessionFacts;
static openq4::NativeWindowPublication windowFacts;
static std::uint64_t allocationFact=0,epochFact=2,streamFact=5;
static bool registryAvailable=true,windowAvailable=true,slotAvailable=true;
static openq4::NativeInputRoute* registeredRoute=nullptr;
static std::uint64_t registeredId=0;
static unsigned sessionQueries=0,windowQueries=0,allocationQueries=0,presenceQueries=0;
static bool refuseUnbind=false;
static std::function<void()> onPresence;
bool Sys_EventDispositionBoundThread()noexcept{return std::this_thread::get_id()==engineThread;}
std::uint64_t Sys_EventDispositionEpoch()noexcept{return Sys_EventDispositionBoundThread()?epochFact:0;}
std::uint64_t Sys_EventQueueToken()noexcept{return streamFact;}
namespace openq4 {
bool Session_QueryNativeInputPublication(NativeSessionPublication& out)noexcept{++sessionQueries;if(!Sys_EventDispositionBoundThread())return false;out=sessionFacts;return true;}
bool Sys_QueryNativeInputWindow(NativeWindowPublication& out)noexcept{++windowQueries;if(!windowAvailable||!Sys_EventDispositionBoundThread())return false;out=windowFacts;return true;}
bool UI_QueryNativeInputAllocation(std::uintptr_t current,std::uint64_t& out)noexcept{++allocationQueries;if(!registryAvailable||current!=sessionFacts.current)return false;out=allocationFact;return true;}
bool NativeInputBindPublications(NativeInputRoute& r,std::uint64_t id,const NativeInputBinding&)noexcept{
    if(!slotAvailable||registeredRoute)return false;registeredRoute=&r;registeredId=id;return true;
}
bool NativeInputPublicationsCurrent(NativeInputRoute& r,std::uint64_t id)noexcept{return registeredRoute==&r && id==registeredId;}
bool NativeInputUnbindPublications(NativeInputRoute& r,std::uint64_t id)noexcept{
    if(refuseUnbind||!NativeInputPublicationsCurrent(r,id)||r.State()!=NativeInputRoute::Phase::Empty)return false;
    registeredRoute=nullptr;registeredId=0;return true;
}
}
openq4::ui::NativeTextPresence UI_NativeTextPresence(openq4::ui::NativeTextIdentity native,const openq4::ui::TextEditorIdentity& owner)noexcept {
    auto callback=std::move(onPresence);if(callback)callback();
    ++presenceQueries;return fields?fields->input.QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;
}
static NativeInputBinding Setup(Session& f) {
    fields=&f.editor;registryAvailable=windowAvailable=slotAvailable=true;registeredRoute=nullptr;registeredId=0;refuseUnbind=false;onPresence={};
    NativeInputBinding b;b.outer=0x1000;b.sessionTransition=3;b.dispatchEpoch=epochFact;b.streamToken=streamFact;
    b.editor=f.editor.barrier.editor;b.native=f.editor.barrier.native;
    b.window={reinterpret_cast<std::uintptr_t>(f.probe.original.hwnd),5,6,9,11,13};
    sessionFacts={b.outer,b.sessionTransition,true};allocationFact=b.editor.allocation;
    windowFacts={b.window.handle,5,6,9,11,13,true,true};return b;
}
static void RetirementQueries() {
    for(unsigned mode=0;mode<3;++mode) {
        Session f;const auto b=Setup(f);WindowsTextSessionRetirement out;
        CHECK(f.value->QueryRetirement(b.native,b.editor,f.probe.original,out));
        CHECK(out.session&&!out.storeRetired&&!out.providerRetired&&!out.nativeReleased&&out.hooksRemoved);
        if(mode==0)f.Start();else if(mode==1)f.Register();
        if(mode==0){f.Quiesce();CHECK(f.value->QueryRetirement(b.native,b.editor,f.probe.original,out));CHECK(!out.providerRetired&&!out.nativeReleased);f.Finish();}
        else CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);
        const auto previousCalls=providerCalls;
        denyAllocations=true;const bool checked=f.value->QueryRetirement(b.native,b.editor,f.probe.original,out);denyAllocations=false;
        CHECK(checked && out.storeRetired&&out.hooksRemoved&&out.nativeReleased && out.providerRetired==(mode!=2));
        CHECK(providerCalls==previousCalls);const auto old=out;
        for(unsigned which=0;which<6;++which) {
            auto n=b.native;auto e=b.editor;auto w=f.probe.original;
            if(which==0)++n.editorLease;if(which==1)++e.allocation;if(which==2)++e.document;if(which==3)++w.lifetime;
            if(which==4)++w.registration;if(which==5)e.revision=0;
            CHECK(!f.value->QueryRetirement(n,e,w,out));CHECK(out.session==old.session&&out.providerRetired==old.providerRetired);
        }
        auto advanced=b.editor;++advanced.revision;CHECK(f.value->QueryRetirement(b.native,advanced,f.probe.original,out));
        std::thread worker([&]{CHECK(!f.value->QueryRetirement(b.native,b.editor,f.probe.original,out));});worker.join();
        fields=nullptr;
    }
    Session busy;auto b=Setup(busy);busy.Register();
    onStage=[&](Stage s){if(s==Push){WindowsTextSessionRetirement out;out.session=999;CHECK(!busy.value->QueryRetirement(b.native,b.editor,busy.probe.original,out));CHECK(out.session==999);}};
    CHECK(busy.value->Activate(busy.source,busy.closed,busy.error));onStage={};fields=nullptr;
}
static void SourceFacts() {
    Session f;auto b=Setup(f);WindowsNativeInputRouteSource source(*f.value);std::string error;
    CHECK(source.SetOwner(b,error));CHECK(!source.SetOwner(b,error));
    NativeInputRoute route(source);std::uint64_t id=0;CHECK(route.Prepare(b,id,error));CHECK(source.BindRoute(route,id));
    NativeInputObservation out;const auto callsBefore=providerCalls;
    CHECK(source.Observe(out)&&out.inputAllowed&&out.allocation==b.editor.allocation);
    WindowsNativeInputWindowProbe probe;CHECK(probe.Current(f.probe.original)&&probe.ProviderCurrent(f.probe.original));
    // Current owner/GUI loss never loses the original independent provider fact.
    registryAvailable=false;CHECK(source.Observe(out)&&!out.inputAllowed&&!out.allocation);
    ++windowFacts.lifetime;CHECK(!probe.Current(f.probe.original)&&probe.ProviderCurrent(f.probe.original));
    ++windowFacts.registration;CHECK(!probe.ProviderCurrent(f.probe.original));windowFacts.registration=b.window.registration;
    CHECK(providerCalls==callsBefore);
    CHECK(route.Revoke(id));NativeInputRetirement retirement;
    CHECK(source.Retirement(id,b,retirement)&&retirement.ui==NativeInputUiRetirement::Unknown);
    f.Register();CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);
    CHECK(source.Retirement(id,b,retirement));CHECK(retirement.ui==NativeInputUiRetirement::AbsentOriginal&&retirement.store==NativeInputNativeRetirement::RetiredExact&&retirement.provider==NativeInputNativeRetirement::RetiredExact);
    CHECK(retirement.hooksRemoved&&retirement.controllerReleased&&!retirement.backlogDisposed);
    CHECK(route.MarkDrainOnly(id));CHECK(!route.Release(id)); // no invented driver backlog proof
    auto wrong=b;++wrong.editor.session;CHECK(!source.Retirement(id,wrong,retirement));
    std::thread worker([&]{CHECK(!source.Observe(out));CHECK(!source.Retirement(id,b,retirement));});worker.join();
    // Test registry removal only. A production driver cannot release this route
    // until it implements exact terminal backlog inventory; activation stays off.
    registeredRoute=nullptr;registeredId=0;CHECK(!source.Retirement(id,b,retirement));fields=nullptr;
}
struct LedgerProbe final:NativeDispositionProbe {
    bool Current(const NativeDispositionContext& c)const noexcept override{return c.providerEpoch==9&&c.generation==generation&&c.engineToken==streamFact;}
};
static void SourceRelease() {
    for(unsigned mode=0;mode<5;++mode){
        Session f;const auto b=Setup(f);f.Register();WindowsNativeInputRouteSource source(*f.value);std::string error;
        CHECK(source.SetOwner(b,error));NativeInputRoute route(source);std::uint64_t id=0;CHECK(route.Prepare(b,id,error));CHECK(source.BindRoute(route,id));
        f.source.Add(SDL_EVENT_KEY_DOWN);CHECK(f.ingress.Read(f.source,f.batch,error)==NativeQueueRead::Ready);
        LedgerProbe probe;NativeEventDispositionLedger ledger(probe);CHECK(ledger.BeginPlanned(f.ingress,f.source,*f.batch,error));
        NativeInputEmissionInventory inventory,empty;CHECK(!source.BindInventory(empty));
        CHECK(inventory.Bind(route,id,b,ledger,*f.batch,error));CHECK(source.BindInventory(inventory));CHECK(!source.BindInventory(inventory));
        CHECK(!source.ReleaseRoute(id));CHECK(!source.ReleaseRoute(id+1));CHECK(registeredRoute==&route);
        CHECK(inventory.Retire());CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);
        NativeInputDisposalReceipt receipt;CHECK(inventory.SealDisposal(receipt,error));NativeInputRetirement facts;
        CHECK(source.Retirement(id,b,facts)&&facts.backlogDisposed&&facts.hooksRemoved&&facts.controllerReleased);
        const auto nativeCalls=providerCalls;
        if(mode==0){std::thread worker([&]{CHECK(!source.ReleaseRoute(id));});worker.join();}
        if(mode==1){refuseUnbind=true;CHECK(!source.ReleaseRoute(id));CHECK(route.State()==NativeInputRoute::Phase::Empty&&registeredRoute==&route);refuseUnbind=false;}
        if(mode==2){onPresence=[&]{CHECK(!source.ReleaseRoute(id));};CHECK(!source.ReleaseRoute(id));CHECK(registeredRoute==&route);}
        if(mode==3){fields=nullptr;CHECK(!source.ReleaseRoute(id));CHECK(registeredRoute==&route);fields=&f.editor;}
        if(mode==4){const auto correct=registeredId;++registeredId;CHECK(!source.ReleaseRoute(id));registeredId=correct;}
        denyAllocations=true;CHECK(source.ReleaseRoute(id));denyAllocations=false;
        CHECK(providerCalls==nativeCalls&&!registeredRoute&&route.State()==NativeInputRoute::Phase::Empty);
        CHECK(!source.ReleaseRoute(id));CHECK(!source.Retirement(id,b,facts));CHECK(!source.BindInventory(inventory));CHECK(!source.SetOwner(b,error));fields=nullptr;
    }
}
int main(){RetirementQueries();SourceFacts();SourceRelease();std::printf("PASS %u checks\n",checks);return 0;}
