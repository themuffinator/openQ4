// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "WindowsTextSession.h"
#if defined(_WIN32)
#include <SDL3/SDL_init.h>
#include <atomic>
#include <utility>

namespace openq4::sys {
using namespace ui;
namespace {
bool Error(std::string& out,const char* message) noexcept {try{out=message;}catch(...){}return false;}
struct Busy {bool& flag; explicit Busy(bool& f):flag(f){flag=true;} ~Busy(){flag=false;}};
template<class T> void Release(T*& value) noexcept {auto* owned=std::exchange(value,nullptr);if(owned)owned->Release();}
bool SameOwner(const TextEditorIdentity& a,const TextEditorIdentity& b) noexcept {
    return a.allocation==b.allocation && a.backend==b.backend && a.document==b.document && a.modal==b.modal &&
        a.window==b.window && a.session==b.session && a.control==b.control;
}
bool SameProvider(const NativeQueueStatus& a,const NativeQueueStatus& b) noexcept {
    return a.mainThread && a.healthy && a.providerEpoch==b.providerEpoch && a.generation==b.generation &&
        a.engineToken==b.engineToken && a.fenceEventType==b.fenceEventType;
}
std::uint64_t Identity() noexcept {
    static std::atomic<std::uint64_t> next{1};auto n=next.load();
    while(n!=UINT64_MAX){if(next.compare_exchange_weak(n,n+1))return n;}return 0;
}
}
HRESULT WindowsTextSessionSystemPlatform::InitializeSta() noexcept {return CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);}
HRESULT WindowsTextSessionSystemPlatform::CreateThreadManager(ITfThreadMgr** out) noexcept {
    return CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ITfThreadMgr),reinterpret_cast<void**>(out));
}
void WindowsTextSessionSystemPlatform::UninitializeSta() noexcept {CoUninitialize();}

struct WindowsTextSession::Impl {
    NativeTextEditorBarrier original,current;
    WindowsTextSessionWindow window;
    WindowsTextSessionPlatform& platform;
    WindowsTextSessionWindowProbe& probe;
    NativeTextCollectionOwner& owner;
    const std::thread::id thread=std::this_thread::get_id();
    const std::uint64_t identity=Identity();
    WindowsTextStore* store=nullptr;
    std::unique_ptr<WindowsTextCollectionBridge> bridge;
    ITfThreadMgr* manager=nullptr;
    ITfDocumentMgr *document=nullptr,*previous=nullptr;
    ITfContext* context=nullptr;
    ITfSource* editSource=nullptr;
    ITfContextOwnerCompositionServices* composition=nullptr;
    TfClientId client=TF_CLIENTID_NULL;
    TfEditCookie editCookie=0;
    DWORD sinkCookie=TF_INVALID_COOKIE;
    NativeQueueStatus baseline;
    NativeClosedTextCollection closed;
    Phase phase=Phase::Created;
    bool busy=false,fault=false,hooks=false,apartment=false,activated=false,pushAttempted=false,associated=false;
    bool ownerRetired=false,associationLost=false,associationRestored=true,cleanupWork=false,terminated=false;
    Impl(const NativeTextEditorBarrier& b,const WindowsTextSessionWindow& w,WindowsTextSessionPlatform& p,
        WindowsTextSessionWindowProbe& v,NativeTextCollectionOwner& o):original(b),current(b),window(w),platform(p),probe(v),owner(o){}
    ~Impl(){bridge.reset();if(store)store->Release();}
    bool Thread() const noexcept {return std::this_thread::get_id()==thread;}
    bool Fail(std::string& error,const char* text) noexcept {fault=true;phase=Phase::Fault;return Error(error,text);}
    bool Enter(std::string& error) noexcept {
        if(!Thread())return Error(error,"Windows text session requires its creating thread");
        if(busy)return Fail(error,"Reentrant Windows text session operation");
        return true;
    }
    bool Window() const noexcept {return probe.Current(window);}
    bool ProviderClaim() const noexcept {return probe.ProviderCurrent(window);}
    bool Provider() const noexcept {
        return !fault && ProviderClaim() && SDL_IsMainThread() && OQ4_WindowsNativeFenceHealthy() &&
            OQ4_WindowsNativeFenceQueueGeneration()==baseline.generation && Window() && !fault;
    }
    bool Native() const noexcept {return Provider() && bridge->Healthy() && !fault;}
    bool After(HRESULT hr) noexcept {return SUCCEEDED(hr) && Native();}
    bool Observe(NativeQueueSource& source,bool pending,std::string& error) {
        if(!Provider())return false;
        NativeQueueStatus status;
        if(!source.Observe(status,error) || fault || !SameProvider(status,baseline) ||
            bool(status.pending)!=pending || !Provider())return false;
        return !pending || (status.pending->version==1 && status.pending->dispatch==closed.dispatch &&
            status.pending->sequence && bridge->StillClosed(closed));
    }
    bool Exact(const NativeTextEditorBarrier& b) const noexcept {
        return b.native==original.native && SameOwner(b.editor,original.editor) && !b.collectionOpen;
    }
    void RetireOwner() noexcept {
        if(!ownerRetired){ownerRetired=true;owner.RetireExact(original.native,original.editor);}
    }
    // Called within the live lifecycle for graceful cleanup. In terminal fault
    // cleanup the retired store refuses writes and no live scope is fabricated.
    bool Cleanup(bool graceful) noexcept {
        bool okay=true;
        if(graceful && composition && pushAttempted){
            // TSF obtains its own synchronous lock; do not RequestEditSession.
            const HRESULT hr=composition->TerminateComposition(nullptr);
            terminated=SUCCEEDED(hr);
            if(!After(hr))return false;
        }
        if(associated){
            if(!Window()){
                associationLost=true;associationRestored=false;associated=false;okay=false;
            } else {
                ITfDocumentMgr* replaced=nullptr;
                const HRESULT hr=manager->AssociateFocus(window.hwnd,previous,&replaced);
                if(SUCCEEDED(hr)){associated=false;associationRestored=true;}
                Release(replaced);
                if(FAILED(hr) || !Window()){associationRestored=false;okay=false;}
                if(graceful && (!okay || !Native()))return false;
            }
        }
        if(editSource && sinkCookie!=TF_INVALID_COOKIE){
            const HRESULT hr=editSource->UnadviseSink(sinkCookie);
            if(SUCCEEDED(hr))sinkCookie=TF_INVALID_COOKIE;else okay=false;
            if(graceful && !After(hr))return false;
        }
        if(document && pushAttempted){
            const HRESULT hr=document->Pop(TF_POPF_ALL);
            if(SUCCEEDED(hr))pushAttempted=false;else okay=false;
            if(graceful && !After(hr))return false;
        }
        if(manager && activated){
            const HRESULT hr=manager->Deactivate();
            if(SUCCEEDED(hr))activated=false;else okay=false;
            if(graceful && !After(hr))return false;
        }
        return okay;
    }
    bool Construct() noexcept {
        HRESULT hr=platform.InitializeSta();
        if(SUCCEEDED(hr))apartment=true; // S_FALSE also owns a CoUninitialize.
        if(!After(hr))return false;
        hr=platform.CreateThreadManager(&manager);
        if(!After(hr) || !manager)return false;
        hr=manager->Activate(&client);
        if(SUCCEEDED(hr))activated=true;
        if(!After(hr) || client==TF_CLIENTID_NULL)return false;
        hr=manager->CreateDocumentMgr(&document);
        if(!After(hr) || !document)return false;
        hr=document->CreateContext(client,0,static_cast<ITextStoreACP*>(store),&context,&editCookie);
        if(!After(hr) || !context)return false;
        hr=store->BindContext(original.native,context);
        if(!After(hr))return false;
        hr=context->QueryInterface(__uuidof(ITfSource),reinterpret_cast<void**>(&editSource));
        if(!After(hr) || !editSource)return false;
        hr=editSource->AdviseSink(__uuidof(ITfTextEditSink),static_cast<ITfTextEditSink*>(store),&sinkCookie);
        if(!After(hr) || sinkCookie==TF_INVALID_COOKIE)return false;
        hr=context->QueryInterface(__uuidof(ITfContextOwnerCompositionServices),reinterpret_cast<void**>(&composition));
        if(!After(hr) || !composition)return false;
        pushAttempted=true;hr=document->Push(context);
        if(!After(hr))return false;
        if(!Window())return false;
        hr=manager->AssociateFocus(window.hwnd,document,&previous);
        if(SUCCEEDED(hr)){associated=true;associationRestored=false;}
        if(!After(hr))return false;
        hr=manager->SetFocus(document);
        return After(hr);
    }
    bool Batch(const NativeQueueBatch& batch,const WindowsTextEventDisposition& token,
        WindowsTextSessionDisposition& disposition) const noexcept {
        const auto& status=batch.Status();
        return token.ledger && token.serial && token.batch==batch.Receipt() && disposition.Current(token) &&
            SameProvider(status,baseline) && status.pending && status.pending->dispatch==closed.dispatch &&
            status.pending->sequence && status.pending->version==1 && !fault;
    }
    struct CheckedFence final:NativeTextCollectionFence {
        Impl& session;WindowsTextSessionDisposition& ledger;
        const WindowsTextEventDisposition token;NativeTextCollectionFence& target;
        CheckedFence(Impl& s,WindowsTextSessionDisposition& d,WindowsTextEventDisposition t,NativeTextCollectionFence& f)
            :session(s),ledger(d),token(t),target(f){}
        bool Acknowledge(const NativeQueueStatus& status,std::string& error) override {
            if(!session.Provider() || !ledger.Current(token) || session.fault)return false;
            return target.Acknowledge(status,error) && session.Provider() && ledger.Current(token) && !session.fault;
        }
    };
    WindowsTextSessionRelease ReleaseNative(bool retiredProvider,bool graceful) noexcept {
        WindowsTextSessionRelease result{retiredProvider,false,false,associationRestored,graceful};
        if(hooks){
            if(!retiredProvider || !ProviderClaim() || !OQ4_WindowsNativeFenceRegisterHooks(nullptr))return result;
            hooks=false;
        }
        result.hooksRemoved=true;
        if(!graceful)(void)Cleanup(false);
        result.associationRestored=associationRestored && !associationLost;
        if(associated || sinkCookie!=TF_INVALID_COOKIE || pushAttempted || activated)return result;
        // Store holds context/sink/composition references. All of them leave on
        // this apartment before balancing our own successful initialization.
        bridge.reset();if(store){auto* s=std::exchange(store,nullptr);s->Release();}
        Release(composition);Release(editSource);Release(context);Release(previous);Release(document);Release(manager);
        if(apartment){apartment=false;platform.UninitializeSta();}
        result.nativeReleased=true;phase=Phase::Released;
        result.graceful=graceful && !fault && result.associationRestored;
        return result;
    }
};
WindowsTextSession::WindowsTextSession(std::unique_ptr<Impl> p):impl(std::move(p)){}
WindowsTextSession::~WindowsTextSession()=default;
bool WindowsTextSession::Create(const NativeTextEditorView& view,const WindowsTextSessionWindow& window,
    WindowsTextSessionPlatform& platform,WindowsTextSessionWindowProbe& probe,NativeTextCollectionOwner& owner,
    WindowsTextSession*& out,std::string& error) {
    try{
        if(!probe.ProviderCurrent(window) || !SDL_IsMainThread() || !view.active || !window.hwnd || !window.window || !window.lifetime ||
            !window.moduleEpoch || !window.association || !window.registration || view.barrier.editor.window!=window.window ||
            !probe.Current(window) || !owner.Current(view.barrier) || !view.presentation.compositions.empty() ||
            view.presentation.text!=view.draft.text || view.presentation.anchor!=view.draft.anchor || view.presentation.caret!=view.draft.caret)
            return Error(error,"Windows text session requires an exact fresh attached editor/window lease");
        auto data=std::make_unique<Impl>(view.barrier,window,platform,probe,owner);
        if(!data->identity || WindowsTextStore::Create(view.barrier.native,view.barrier.editor.revision,
            view.draft.text,view.draft.anchor,view.draft.caret,window.hwnd,&data->store)!=S_OK ||
            !WindowsTextCollectionBridge::Create(*data->store,view.barrier,window.moduleEpoch,data->bridge,error))return false;
        auto* value=new WindowsTextSession(std::move(data));out=value;error.clear();return true;
    }catch(...){return Error(error,"Windows text session construction failed");}
}
bool WindowsTextSession::Destroy(WindowsTextSession*& session) noexcept {
    if(!session)return true;auto& p=*session->impl;
    if(!p.Thread())return false;
    if(p.busy){p.fault=true;p.phase=Phase::Fault;return false;}
    if(p.phase!=Phase::Released || p.hooks || p.apartment || p.manager || p.document || p.context || p.editSource || p.composition)return false;
    auto* old=std::exchange(session,nullptr);delete old;return true;
}
WindowsTextSession::Phase WindowsTextSession::State() const noexcept {return impl->Thread()?impl->phase:Phase::Fault;}
NativeTextCollectionStore& WindowsTextSession::Collections() noexcept {return *impl->bridge;}
bool WindowsTextSession::Register(std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    if(p.phase!=Phase::Created || p.fault || !p.ProviderClaim() || !p.Window() || !p.owner.Current(p.original))return p.Fail(error,"Invalid Windows text hook registration");
    const auto hooks=p.bridge->Hooks();
    if(!OQ4_WindowsNativeFenceRegisterHooks(&hooks))return p.Fail(error,"Windows text hooks could not be registered while disabled");
    p.hooks=true;
    if(p.fault)return p.Fail(error,"Windows text registration was reentered");
    p.phase=Phase::Registered;error.clear();return true;
}
bool WindowsTextSession::Bind(NativeQueueSource& source,std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    try{
        NativeQueueStatus status;
        if(p.phase!=Phase::Registered || p.fault || !p.ProviderClaim() || !source.Observe(status,error) || p.fault ||
            !status.mainThread || !status.healthy || !status.generation || !status.engineToken || status.pending ||
            status.providerEpoch!=p.window.moduleEpoch || !status.fenceEventType || !p.Window() || !p.owner.Current(p.original) ||
            !p.bridge->BindProviderGeneration(status.providerEpoch,status.generation,error))return p.Fail(error,"Windows text provider binding is unavailable");
        p.baseline=status;
        if(!p.Observe(source,false,error) || p.fault)return p.Fail(error,"Windows text provider changed during binding");
        p.phase=Phase::Bound;error.clear();return true;
    }catch(...){return p.Fail(error,"Windows text provider binding callback failed");}
}
bool SDLCALL WindowsTextSession::Work(void* userdata,const OQ4_NativeCollectionContext* context) noexcept {
    auto& p=*static_cast<WindowsTextSession*>(userdata)->impl;
    if(!p.Thread() || !p.busy || !context || context->version!=1 || context->kind!=OQ4_COLLECTION_LIFECYCLE ||
        context->generation!=p.baseline.generation || !context->dispatch || !p.Native())return false;
    try{return p.cleanupWork?p.Cleanup(true):p.Construct();}
    catch(...){p.fault=true;return false;}
}
bool WindowsTextSession::Activate(NativeQueueSource& source,NativeClosedTextCollection& out,std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    try{
        if(p.phase!=Phase::Bound || !p.Observe(source,false,error) || !p.owner.Current(p.original))return p.Fail(error,"Windows text activation lost its owner/provider");
        p.cleanupWork=false;
        if(!OQ4_WindowsNativeFenceRunLifecycle(&Work,this) || p.fault)return p.Fail(error,"Windows text native activation failed");
        WindowsTextCollectionReceipt receipt;
        if(!p.bridge->CopyClosed(receipt) || !p.bridge->Closed(p.current,receipt.collection.dispatch,p.closed,error) ||
            !p.Observe(source,true,error) || !p.owner.Current(p.original) || p.fault)return p.Fail(error,"Windows text activation lost its closed lifecycle");
        p.phase=Phase::ActivationHeld;out=p.closed;error.clear();return true;
    }catch(...){return p.Fail(error,"Windows text activation callback failed");}
}
bool WindowsTextSession::FinishActivation(NativeTextCollectionCoordinator& coordinator,const NativeTextCollectionCompletion& requestedCompletion,
    NativeQueueIngress& ingress,NativeQueueSource& source,const NativeQueueBatch& batch,const WindowsTextEventDisposition& requestedDisposition,
    WindowsTextSessionDisposition& disposition,NativeTextCollectionFence& fence,std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    const auto completion=requestedCompletion;
    const auto token=requestedDisposition;
    try{
        NativeTextEditorBarrier current;
        if(p.phase!=Phase::ActivationHeld || !p.Batch(batch,token,disposition) || completion.batch!=batch.Receipt() ||
            !ingress.Validate(source,batch.Receipt(),error) || !p.Native() || !p.bridge->StillClosed(p.closed) ||
            !coordinator.QueryBarrier(current,error) || !p.Exact(current) || !p.owner.Current(current) || !disposition.Current(token) || p.fault)
            return p.Fail(error,"Windows text activation completion is stale");
        Impl::CheckedFence checked(p,disposition,token,fence);
        if(!coordinator.CompleteFence(ingress,source,completion,p.owner,*p.bridge,checked,error) || p.fault ||
            !p.Observe(source,false,error) || !disposition.Current(token) || !p.owner.Current(current))
            return p.Fail(error,"Windows text activation fence could not complete");
        p.current=std::move(current);p.phase=Phase::Active;error.clear();return true;
    }catch(...){return p.Fail(error,"Windows text activation completion failed");}
}
bool WindowsTextSession::Quiesce(NativeTextCollectionCoordinator& coordinator,NativeQueueSource& source,
    WindowsTextSessionCleanup& out,std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    try{
        NativeTextEditorBarrier current;
        if(p.phase!=Phase::Active || !coordinator.QueryBarrier(current,error) || !p.Exact(current) ||
            !p.Observe(source,false,error))return p.Fail(error,"Windows text cleanup requires an idle exact lifecycle");
        p.current=std::move(current);p.RetireOwner();
        if(p.fault || !p.Native())return p.Fail(error,"Windows text cleanup lost its original window");
        p.cleanupWork=true;
        if(!OQ4_WindowsNativeFenceRunLifecycle(&Work,this) || p.fault)return p.Fail(error,"Windows text composition cleanup failed");
        WindowsTextCollectionReceipt receipt;
        if(!p.bridge->CopyClosed(receipt) || !p.bridge->Closed(p.current,receipt.collection.dispatch,p.closed,error) ||
            !p.Observe(source,true,error) || p.fault)return p.Fail(error,"Windows text cleanup lost its closed scope");
        p.phase=Phase::CleanupHeld;out={p.identity,p.closed};error.clear();return true;
    }catch(...){return p.Fail(error,"Windows text cleanup callback failed");}
}
bool WindowsTextSession::FinishQuiesce(const WindowsTextSessionCleanup& requestedCleanup,NativeQueueIngress& ingress,NativeQueueSource& source,
    const NativeQueueBatch& batch,const WindowsTextEventDisposition& requestedDisposition,WindowsTextSessionDisposition& disposition,
    NativeTextCollectionFence& fence,WindowsTextSessionRelease& out,std::string& error) {
    auto& p=*impl;if(!p.Enter(error))return false;Busy guard(p.busy);
    const auto expected=requestedCleanup;
    const auto token=requestedDisposition;
    try{
        if(p.phase!=Phase::CleanupHeld || expected!=WindowsTextSessionCleanup{p.identity,p.closed} ||
            !p.Batch(batch,token,disposition) || !ingress.Validate(source,batch.Receipt(),error) ||
            !p.Native() || !p.bridge->StillClosed(p.closed) || !disposition.Current(token) || p.fault)return p.Fail(error,"Windows text cleanup disposition is stale");
        // The original engine lease is retired. Discard native cleanup offers
        // only after their whole exact ordinary event batch has been accounted.
        Impl::CheckedFence checked(p,disposition,token,fence);
        p.bridge->RetireExact(p.original.native);
        if(p.fault || !p.Provider() || !ingress.Validate(source,batch.Receipt(),error) || !disposition.Current(token) ||
            !checked.Acknowledge(batch.Status(),error) || p.fault || !p.Provider() || !disposition.Current(token) ||
            !ingress.Finish(source,batch.Receipt(),error) || p.fault || !p.Observe(source,false,error))
            return p.Fail(error,"Windows text cleanup fence lost continuity");
        if(!OQ4_WindowsNativeFenceEnable(false) || p.fault)return p.Fail(error,"Windows text provider could not disable after cleanup");
        const auto result=p.ReleaseNative(true,true);
        if(!result.hooksRemoved || !result.nativeReleased || !result.graceful)return p.Fail(error,"Windows text cleanup release is incomplete");
        out=result;error.clear();return true;
    }catch(...){return p.Fail(error,"Windows text cleanup completion failed");}
}
WindowsTextSessionRelease WindowsTextSession::FaultRetirePreservingEvents() noexcept {
    auto& p=*impl;
    if(!p.Thread())return {};
    if(p.busy){p.fault=true;p.phase=Phase::Fault;return {};}
    Busy guard(p.busy);p.fault=true;
    if(p.phase==Phase::Released)return {true,true,true,p.associationRestored,false};
    p.phase=Phase::Fault;p.RetireOwner();
    if(p.bridge)p.bridge->RetireExact(p.original.native);
    // Retire does not Poll/flush/ClearEvents/ACK. Already queued ordinary SDL
    // events remain the engine's explicit legacy/failure-disposition work.
    bool retired=!p.hooks;
    if(p.hooks && p.ProviderClaim()){
        const auto active=OQ4_WindowsNativeFenceQueueGeneration();
        if((!p.baseline.generation || !active || active==p.baseline.generation) && p.ProviderClaim())retired=OQ4_WindowsNativeFenceRetire();
    }
    return p.ReleaseNative(retired,false);
}
} // namespace openq4::sys
#endif
