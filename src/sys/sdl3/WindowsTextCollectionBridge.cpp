// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "WindowsTextCollectionBridge.h"
#if defined(_WIN32)
#if defined(USE_SDL3) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
#include <SDL3/SDL_init.h>
#define OQ4_COLLECTION_PROVIDER 1
#else
#define OQ4_COLLECTION_PROVIDER 0
#endif
#include <optional>
#include <thread>
#include <type_traits>

namespace openq4::sys {
using namespace ui;
namespace {
bool Error(std::string& out,const char* message) noexcept {try {out=message;} catch (...) {out.clear();} return false;}
bool SameOwner(const TextEditorIdentity& a,const TextEditorIdentity& b) noexcept {
    return a.allocation==b.allocation && a.backend==b.backend && a.document==b.document && a.modal==b.modal &&
        a.window==b.window && a.session==b.session && a.control==b.control;
}
bool ValidOwner(const TextEditorIdentity& a) noexcept {
    return a.allocation && a.backend && a.document && a.modal && a.window && a.session && a.revision && !a.control.empty();
}
bool SameContext(const OQ4_NativeCollectionContext& a,const OQ4_NativeCollectionContext& b) noexcept {
    return a.version==b.version && a.kind==b.kind && a.generation==b.generation && a.dispatch==b.dispatch;
}
struct Busy {bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};
NativeClosedTextCollection Portable(const WindowsTextCollectionReceipt& source) noexcept {
    return {source.collection.identity,source.collection.serial,source.collection.dispatch,
        source.collection.kind==WindowsTextCollectionKind::Pump?NativeClosedCollectionKind::Pump:NativeClosedCollectionKind::Lifecycle,
        source.pending,source.admittedCallbacks};
}
bool SameSeal(const NativeClosedTextCollection& a,const NativeClosedTextCollection& b) noexcept {
    return a.identity==b.identity && a.serial==b.serial && a.dispatch==b.dispatch && a.kind==b.kind &&
        a.pending==b.pending && a.admittedCallbacks==b.admittedCallbacks;
}
}
struct WindowsTextCollectionBridge::Impl {
    WindowsTextStore* store;
    const NativeTextIdentity native;
    const TextEditorIdentity owner;
    const std::uint64_t epoch;
    const std::thread::id thread=std::this_thread::get_id();
    std::uint64_t generation=0,engine=0,sequence=0,shadow=0,lastDispatch=0;
    bool terminal=false,busy=false;
    std::optional<OQ4_NativeCollectionContext> attempt;
    std::optional<WindowsTextCollection> opened;
    std::optional<WindowsTextCollectionReceipt> closed;
    Impl(WindowsTextStore& s,const NativeTextEditorBarrier& b,std::uint64_t e)
        :store(&s),native(b.native),owner(b.editor),epoch(e),engine(b.editor.revision),sequence(b.sequence),shadow(b.shadowRevision){store->AddRef();}
    ~Impl(){store->Release();}
    bool Owner() const noexcept {return std::this_thread::get_id()==thread;}
    void Fault() noexcept {terminal=true;closed.reset();}
    void Retire() noexcept {Fault();(void)store->Retire(native);}
    bool Provider() const noexcept {
#if OQ4_COLLECTION_PROVIDER
        return Owner() && epoch && generation && SDL_IsMainThread() && OQ4_WindowsNativeFenceHealthy() &&
            OQ4_WindowsNativeFenceQueueGeneration()==generation;
#else
        return false;
#endif
    }
    bool Context(const OQ4_NativeCollectionContext& c) const noexcept {
        return c.version==1 && (c.kind==OQ4_COLLECTION_PUMP || c.kind==OQ4_COLLECTION_LIFECYCLE) &&
            c.generation==generation && c.dispatch && Provider();
    }
    bool Barrier(const NativeTextEditorBarrier& b) const noexcept {
        return b.native==native && SameOwner(owner,b.editor) && b.editor.revision==engine && b.sequence==sequence && b.shadowRevision==shadow;
    }
    bool Seal(bool inside=false) const noexcept {
        if(!Owner() || terminal || (!inside && busy) || attempt || opened || !closed || !Provider()) return false;
        WindowsTextLifecycle life;
        return store->QueryLifecycle(native,life)==S_OK && life.healthy && !life.collectionOpen &&
            life.lastScopeSerial==closed->collection.serial && life.lastDispatch==closed->collection.dispatch &&
            life.engineRevision==engine && life.acknowledgedSequence==sequence && life.acknowledgedShadowRevision==shadow &&
            sequence<=closed->pending.lastSequence && closed->pending.lastSequence-sequence==life.pending &&
            life.shadowRevision==closed->pending.shadowRevision && Provider() && !terminal;
    }
    template<class F> bool Call(std::string& error,F&& function) {
        if(!Owner()) return Error(error,"Native collection bridge requires its owning thread");
        if(busy){Fault();return Error(error,"Native collection bridge reentry");}
        if(terminal) return Error(error,"Native collection bridge is retired");
        Busy guard(busy);
        try{return function();}catch(...){Retire();return Error(error,"Native collection bridge allocation or callback failure");}
    }
    bool Prepare(const OQ4_NativeCollectionContext* input) noexcept {
        if(!Owner()) return false;
        // A failed/reentrant attempt cannot replace the original cleanup pair.
        if(busy || attempt || opened){Fault();return false;}
        if(!input){Fault();return false;}
        attempt=*input;closed.reset();
        Busy guard(busy);
        if(terminal || !Context(*input) || input->dispatch<=lastDispatch){Fault();return false;}
        WindowsTextCollection candidate;
        const auto kind=input->kind==OQ4_COLLECTION_PUMP?WindowsTextCollectionKind::Pump:WindowsTextCollectionKind::Lifecycle;
        if(store->OpenCollection(native,engine,sequence,shadow,input->dispatch,kind,candidate)!=S_OK){Fault();return false;}
        opened=candidate;
        if(terminal || !Context(*input) || candidate.identity!=native || candidate.dispatch!=input->dispatch || candidate.kind!=kind){Fault();return false;}
        lastDispatch=input->dispatch;return true;
    }
    bool Finish(const OQ4_NativeCollectionContext* input,bool aborted) noexcept {
        if(!Owner()) return false;
        if(busy){Fault();return false;}
        if(!input || !attempt || !SameContext(*input,*attempt)){Fault();return false;}
        Busy guard(busy);
        struct Cleanup {Impl& self;~Cleanup(){self.attempt.reset();self.opened.reset();}} cleanup{*this};
        // No Open from this exact attempt means NO authority to close/abort a
        // pre-existing scope, even if Open refused because another scope exists.
        if(!opened){Fault();return false;}
        if(aborted || terminal || !Context(*input)) {
            (void)store->AbortCollection(*opened);Fault();return false;
        }
        WindowsTextCollectionReceipt result;
        if(store->CloseCollection(*opened,result)!=S_OK){Retire();return false;}
        const auto& p=result.pending;
        if(terminal || result.collection!=*opened || p.identity!=native || p.dispatch!=input->dispatch ||
            p.acknowledgedSequence!=sequence || p.lastSequence<sequence || p.count>32 || p.lastSequence-sequence!=p.count ||
            p.shadowRevision<p.acknowledgedShadowRevision || p.shadowRevision-p.acknowledgedShadowRevision!=p.count ||
            p.engineRevision!=engine || p.acknowledgedShadowRevision!=shadow ||
            (p.count && !result.admittedCallbacks) || !Context(*input)){Retire();return false;}
#if OQ4_COLLECTION_PROVIDER
        if(result.admittedCallbacks && !OQ4_WindowsNativeFenceMarkActivity(input)){Retire();return false;}
#endif
        WindowsTextLifecycle life;
        if(terminal || !Context(*input) || store->QueryLifecycle(native,life)!=S_OK || !life.healthy || life.collectionOpen ||
            life.lastScopeSerial!=result.collection.serial || life.lastDispatch!=input->dispatch || life.engineRevision!=p.engineRevision ||
            life.acknowledgedSequence!=p.acknowledgedSequence || life.acknowledgedShadowRevision!=p.acknowledgedShadowRevision ||
            life.shadowRevision!=p.shadowRevision || life.pending!=p.count){Retire();return false;}
        engine=p.engineRevision;shadow=p.acknowledgedShadowRevision;closed=result;return true;
    }
};
WindowsTextCollectionBridge::WindowsTextCollectionBridge(std::unique_ptr<Impl> value):impl(std::move(value)){}
WindowsTextCollectionBridge::~WindowsTextCollectionBridge(){impl->Retire();}
bool WindowsTextCollectionBridge::Create(WindowsTextStore& store,const NativeTextEditorBarrier& initial,
    std::uint64_t epoch,std::unique_ptr<WindowsTextCollectionBridge>& out,std::string& error) {
    if(!OQ4_COLLECTION_PROVIDER) return Error(error,"Checked Windows native collection provider is unavailable");
    WindowsTextLifecycle life;
    if(out || !epoch || !initial.native.document || !initial.native.editorLease || !ValidOwner(initial.editor) ||
        initial.shadowRevision!=1 || initial.sequence || initial.group || initial.collection!=NativeTextCollection{} || initial.collectionOpen ||
        store.QueryLifecycle(initial.native,life)!=S_OK || !life.healthy || life.lastScopeSerial || life.retainedCompositions || life.liveCompositions ||
        life.collectionOpen || life.pending || life.engineRevision!=initial.editor.revision || life.acknowledgedSequence!=initial.sequence ||
        life.acknowledgedShadowRevision!=initial.shadowRevision || life.shadowRevision!=initial.shadowRevision)
        return Error(error,"Native collection bridge attachment is not exact and quiescent");
    try {
        auto state=std::make_unique<Impl>(store,initial,epoch);
        auto candidate=std::unique_ptr<WindowsTextCollectionBridge>(new WindowsTextCollectionBridge(std::move(state)));
        out=std::move(candidate);error.clear();return true;
    }catch(...){return Error(error,"Native collection bridge allocation failed");}
}
OQ4_NativeCollectionHooks WindowsTextCollectionBridge::Hooks() noexcept {return {1,this,Prepare,Finish};}
bool SDLCALL WindowsTextCollectionBridge::Prepare(void* user,const OQ4_NativeCollectionContext* context) noexcept {
    return user && static_cast<WindowsTextCollectionBridge*>(user)->impl->Prepare(context);
}
bool SDLCALL WindowsTextCollectionBridge::Finish(void* user,const OQ4_NativeCollectionContext* context,bool aborted) noexcept {
    return user && static_cast<WindowsTextCollectionBridge*>(user)->impl->Finish(context,aborted);
}
bool WindowsTextCollectionBridge::BindProviderGeneration(std::uint64_t epoch,std::uint64_t generation,std::string& error) {
    return impl->Call(error,[&]{
        if(epoch!=impl->epoch || !generation || impl->generation || impl->attempt || impl->opened || impl->closed)
            return Error(error,"Native collection provider binding is stale");
#if OQ4_COLLECTION_PROVIDER
        if(!SDL_IsMainThread() || !OQ4_WindowsNativeFenceHealthy() || OQ4_WindowsNativeFenceQueueGeneration()!=generation)
            return Error(error,"Native collection provider generation is unavailable");
        impl->generation=generation;error.clear();return true;
#else
        return Error(error,"Checked Windows native collection provider is unavailable");
#endif
    });
}
bool WindowsTextCollectionBridge::Healthy() const noexcept {return impl->Owner() && !impl->terminal && impl->Provider() && impl->store->Healthy();}
bool WindowsTextCollectionBridge::CopyClosed(WindowsTextCollectionReceipt& out) const noexcept {
    if(!impl->Seal()) return false;out=*impl->closed;return true;
}
bool WindowsTextCollectionBridge::Closed(const NativeTextEditorBarrier& expected,std::uint64_t dispatch,
    NativeClosedTextCollection& out,std::string& error) {
    return impl->Call(error,[&]{
        if(!impl->Barrier(expected) || !impl->Seal(true) || impl->closed->collection.dispatch!=dispatch)
            return Error(error,"Native collection has no matching closed receipt");
        out=Portable(*impl->closed);error.clear();return true;
    });
}
bool WindowsTextCollectionBridge::StillClosed(const NativeClosedTextCollection& expected) const noexcept {
    return impl->Seal() && SameSeal(Portable(*impl->closed),expected);
}
bool WindowsTextCollectionBridge::Pending(const NativeTextEditorBarrier& expected,std::uint64_t dispatch,
    NativeTextPendingSnapshot& out,std::string& error) {
    return impl->Call(error,[&]{
        if(!impl->Barrier(expected) || !impl->Seal(true) || impl->closed->collection.dispatch!=dispatch)
            return Error(error,"Native pending query has no matching closed owner");
        NativeTextPendingSnapshot candidate;
        if(impl->store->QueryPendingCollection(impl->native,impl->engine,impl->sequence,impl->shadow,dispatch,candidate)!=S_OK || !impl->Seal(true))
            return Error(error,"Native pending query was refused");
        out=candidate;error.clear();return true;
    });
}
bool WindowsTextCollectionBridge::Peek(const NativeTextIdentity& id,NativeTextOffer& out,std::string& error) {
    return impl->Call(error,[&]{
        if(id!=impl->native || !impl->Seal(true)) return Error(error,"Native offer has no matching closed owner");
        // Copy an already constructed output instead of default-constructing
        // debug-STL string/vector proxies under allocation failure. Copy failure
        // is catchable and leaves the caller's complete output untouched.
        NativeTextOffer candidate=out;
        if(impl->store->Peek(id,candidate)!=S_OK || !impl->Seal(true)) return Error(error,"Native offer is unavailable");
        const auto& tx=candidate.transaction;
        if(tx.identity!=id || tx.nativeDispatch!=impl->closed->collection.dispatch || impl->sequence==UINT64_MAX || tx.sequence!=impl->sequence+1 ||
            impl->shadow==UINT64_MAX || tx.shadowBefore!=impl->shadow || tx.shadowAfter!=impl->shadow+1 || candidate.expectedEngineRevision!=impl->engine)
            return Error(error,"Native offer does not match the closed queue");
        static_assert(std::is_nothrow_move_assignable_v<NativeTextOffer>);out=std::move(candidate);error.clear();return true;
    });
}
bool WindowsTextCollectionBridge::Acknowledge(const NativeTextEditorReceipt& receipt,std::string& error) {
    return impl->Call(error,[&]{
        const auto& after=receipt.after;
        if(receipt.effect!=NativeTextEditorEffect::Acknowledge || !impl->Barrier(receipt.before) || !impl->Seal(true) ||
            after.native!=impl->native || !SameOwner(impl->owner,after.editor) || after.editor.revision<impl->engine ||
            impl->sequence==UINT64_MAX || after.sequence!=impl->sequence+1 || impl->shadow==UINT64_MAX || after.shadowRevision!=impl->shadow+1)
            return Error(error,"Native acknowledgement does not match the owner barrier");
        if(impl->store->Acknowledge(impl->native,after.sequence,after.shadowRevision,impl->engine,after.editor.revision)!=S_OK)
            return Error(error,"Native acknowledgement was refused");
        impl->engine=after.editor.revision;impl->sequence=after.sequence;impl->shadow=after.shadowRevision;
        if(!impl->Seal(true)){impl->Retire();return Error(error,"Native acknowledgement lost its closed scope");}
        error.clear();return true;
    });
}
bool WindowsTextCollectionBridge::Sync(const NativeTextEditorReceipt& receipt,const NativeTextSnapshot& presentation,std::string& error) {
    return impl->Call(error,[&]{
        const auto& after=receipt.after;
        if(receipt.effect!=NativeTextEditorEffect::SyncEngine || !impl->Barrier(receipt.before) || !impl->Seal(true) ||
            after.native!=impl->native || !SameOwner(impl->owner,after.editor) || after.editor.revision<=impl->engine ||
            after.sequence!=impl->sequence || after.shadowRevision!=impl->shadow || after.group || after.collectionOpen ||
            !presentation.compositions.empty() || impl->closed->pending.lastSequence!=impl->sequence)
            return Error(error,"Native settlement synchronization is not exact");
        if(impl->store->SyncEngine(impl->native,impl->engine,impl->shadow,after.editor.revision,presentation.text,presentation.anchor,presentation.caret)!=S_OK) {
            impl->Retire();return Error(error,"Native settlement synchronization failed");
        }
        // No string construction, allocation or foreign COM callback after
        // successful Sync: the coordinator will publish its prepared candidate.
        impl->engine=after.editor.revision;
        if(!impl->Seal(true)){impl->Retire();return Error(error,"Native settlement lost authority after synchronization");}
        error.clear();return true;
    });
}
void WindowsTextCollectionBridge::RetireExact(const NativeTextIdentity& id) noexcept {
    if(impl->Owner() && id==impl->native) impl->Retire();
}
} // namespace openq4::sys
#endif
