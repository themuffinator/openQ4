// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "WindowsNativeInputRouteSource.h"
#if defined(_WIN32)
#include "../../ui/UserInterfaceNativeText.h"
#include "../EventQueueContinuity.h"

namespace openq4::sys {
namespace {
NativeInputWindow Window(const NativeWindowPublication& w) noexcept {
    return {w.handle,w.window,w.lifetime,w.module,w.association,w.registration};
}
WindowsTextSessionWindow NativeWindow(const NativeInputWindow& w) noexcept {
    return {reinterpret_cast<HWND>(w.handle),w.window,w.lifetime,w.module,w.association,w.registration};
}
bool Same(const NativeInputBinding& a,const NativeInputBinding& b) noexcept {
    return a.outer==b.outer && a.sessionTransition==b.sessionTransition && a.dispatchEpoch==b.dispatchEpoch &&
        a.streamToken==b.streamToken && a.editor==b.editor && a.native==b.native && a.window==b.window;
}
bool Error(std::string& error,const char* text) noexcept {try{error=text;}catch(...){}return false;}
}
bool WindowsNativeInputWindowProbe::Current(const WindowsTextSessionWindow& expected) const noexcept {
    NativeWindowPublication current;
    return Sys_QueryNativeInputWindow(current) && current.windowAllowed && current.providerOwned &&
        NativeWindow(Window(current))==expected;
}
bool WindowsNativeInputWindowProbe::ProviderCurrent(const WindowsTextSessionWindow& expected) const noexcept {
    NativeWindowPublication current;
    return expected.moduleEpoch && expected.registration && Sys_QueryNativeInputWindow(current) && current.providerOwned &&
        current.module==expected.moduleEpoch && current.registration==expected.registration;
}
WindowsNativeInputRouteSource::WindowsNativeInputRouteSource(WindowsTextSession& value) noexcept
    :thread(std::this_thread::get_id()),controller(value){}
bool WindowsNativeInputRouteSource::SetOwner(const NativeInputBinding& requested,std::string& error) noexcept {
    if(std::this_thread::get_id()!=thread)return Error(error,"Native route facts require their original thread");
    if(preparing){poisoned=true;return Error(error,"Reentrant native route fact preparation");}
    if(binding || route || poisoned || finished)return Error(error,"Native route facts already own a lease");
    preparing=true;
    struct Guard{bool& flag;~Guard(){flag=false;}} guard{preparing};
    try{
        auto candidate=std::make_unique<const NativeInputBinding>(requested);
        WindowsTextSessionRetirement actual;
        NativeInputObservation observed;
        if(!controller.QueryRetirement(candidate->native,candidate->editor,NativeWindow(candidate->window),actual) ||
            !actual.session || actual.storeRetired || actual.nativeReleased ||
            UI_NativeTextPresence(candidate->native,candidate->editor)!=ui::NativeTextPresence::PresentExact ||
            !Observe(observed) || !observed.inputAllowed || !observed.windowAllowed || !observed.providerOwned ||
            observed.current!=candidate->outer || observed.allocation!=candidate->editor.allocation ||
            observed.sessionTransition!=candidate->sessionTransition || observed.dispatchEpoch!=candidate->dispatchEpoch ||
            observed.streamToken!=candidate->streamToken || observed.window!=candidate->window || poisoned)
            return Error(error,"Native route facts do not match the actual original owner/controller");
        controllerId=actual.session;binding=std::move(candidate);error.clear();return true;
    }catch(...){return Error(error,"Native route fact allocation failed");}
}
bool WindowsNativeInputRouteSource::BindRoute(NativeInputRoute& r,std::uint64_t id) noexcept {
    if(std::this_thread::get_id()!=thread || preparing || poisoned || !binding || route || !id)return false;
    if(!NativeInputBindPublications(r,id,*binding))return false;
    route=&r;routeId=id;return true;
}
bool WindowsNativeInputRouteSource::BindInventory(NativeInputEmissionInventory& value) noexcept {
    if(std::this_thread::get_id()!=thread || preparing || poisoned || !route || inventory ||
        route->State()!=NativeInputRoute::Phase::Bound || !NativeInputPublicationsCurrent(*route,routeId) ||
        !binding || !value.MatchesBinding(*route,routeId,*binding))return false;
    inventory=&value;return true;
}
bool WindowsNativeInputRouteSource::ReleaseRoute(std::uint64_t exactRoute) noexcept {
    if(std::this_thread::get_id()!=thread || preparing || finished || !route || !binding ||
        exactRoute!=routeId || !NativeInputPublicationsCurrent(*route,routeId))return false;
    if(releasing){(void)route->Revoke(routeId);return false;}
    releasing=true;struct Guard {bool& flag;~Guard(){flag=false;}} guard{releasing};
    // If successful Release was followed by an unbind refusal, retain and retry
    // only this exact empty original slot. Never observe/adopt a replacement.
    if(route->State()!=NativeInputRoute::Phase::Empty && !route->Release(routeId))return false;
    if(!NativeInputUnbindPublications(*route,routeId))return false;
    route=nullptr;inventory=nullptr;routeId=0;binding.reset();finished=true;return true;
}
bool WindowsNativeInputRouteSource::Observe(NativeInputObservation& out) const noexcept {
    if(std::this_thread::get_id()!=thread || poisoned || !Sys_EventDispositionBoundThread())return false;
    NativeSessionPublication session;
    NativeWindowPublication window;
    NativeInputObservation candidate;
    if(!Session_QueryNativeInputPublication(session) || !Sys_QueryNativeInputWindow(window))return false;
    const bool managed=session.current && UI_QueryNativeInputAllocation(session.current,candidate.allocation);
    candidate.current=session.current;candidate.sessionTransition=session.transition;
    candidate.dispatchEpoch=Sys_EventDispositionEpoch();candidate.streamToken=Sys_EventQueueToken();
    candidate.window=Window(window);candidate.boundThread=true;candidate.inputAllowed=managed && session.inputAllowed && candidate.dispatchEpoch;
    candidate.windowAllowed=window.windowAllowed;candidate.providerOwned=window.providerOwned;
    out=candidate;return true;
}
bool WindowsNativeInputRouteSource::Original(std::uint64_t id,const NativeInputBinding& expected) const noexcept {
    return std::this_thread::get_id()==thread && !preparing && !poisoned && binding && route &&
        id==routeId && Same(expected,*binding) && NativeInputPublicationsCurrent(*route,routeId);
}
bool WindowsNativeInputRouteSource::Retirement(std::uint64_t id,const NativeInputBinding& expected,NativeInputRetirement& out) const noexcept {
    if(!Original(id,expected))return false;
    NativeInputRetirement candidate;candidate.route=id;candidate.native=binding->native;candidate.window=binding->window;
    const auto present=UI_NativeTextPresence(binding->native,binding->editor);
    candidate.ui=present==ui::NativeTextPresence::AbsentOriginal ? NativeInputUiRetirement::AbsentOriginal :
        present==ui::NativeTextPresence::BusyOrUnknown ? NativeInputUiRetirement::Busy : NativeInputUiRetirement::Unknown;
    WindowsTextSessionRetirement native;
    if(!controller.QueryRetirement(binding->native,binding->editor,NativeWindow(binding->window),native) || native.session!=controllerId){
        candidate.store=NativeInputNativeRetirement::Busy;candidate.provider=NativeInputNativeRetirement::Busy;
    }else{
        candidate.store=native.storeRetired ? NativeInputNativeRetirement::RetiredExact : NativeInputNativeRetirement::Unknown;
        candidate.provider=native.providerRetired ? NativeInputNativeRetirement::RetiredExact : NativeInputNativeRetirement::Unknown;
        candidate.hooksRemoved=native.hooksRemoved;candidate.controllerReleased=native.nativeReleased;
        if(!native.providerRetired){
            NativeWindowPublication current;
            if(!Sys_QueryNativeInputWindow(current) || !current.providerOwned || current.module!=binding->window.module ||
                current.registration!=binding->window.registration)candidate.provider=NativeInputNativeRetirement::ClaimLost;
        }
    }
    candidate.backlogDisposed=inventory && inventory->MatchesBinding(*route,routeId,*binding) && inventory->BacklogDisposed();
    if(!Original(id,expected))return false;
    out=candidate;return true;
}
bool WindowsNativeInputRouteSource::InspectIssued(std::uint64_t id,const NativeInputBinding& expected,
    const sysEventDispositionTag_t& tag,NativeInputIssued& out) const noexcept {
    if(!Original(id,expected) || !inventory)return false;
    NativeInputIssued candidate;
    if(!inventory->Inspect(id,*binding,tag,candidate) || !Original(id,expected))return false;
    out=candidate;return true;
}
} // namespace openq4::sys
#endif
