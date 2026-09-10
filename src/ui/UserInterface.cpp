/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#ifndef ID_DEDICATED
#include "../sys/sdl3/TextClipboard.h"
#endif
#include "ListGUILocal.h"
#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "UserInterfaceDeferred.h"
#ifndef ID_DEDICATED
#include "UserInterfaceRetained.h"
#endif
#include "ChatWindow.h"
#include "EditWindow.h"
#include "SimpleWindow.h"
#include "../framework/Session.h"
#include "RetainedUI.h"
#include <limits>

extern idCVar r_skipGuiShaders;		// 1 = don't render any gui elements on surfaces
extern idCVar gui_debugScript;
idCVar ui_aspectCorrection( "ui_aspectCorrection", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_BOOL,
	"preserve classic 4:3 layout for 2D UI (menu, HUD, console, loading/init); 0 = stretch to full 2D viewport" );

idUserInterfaceManagerLocal	uiManagerLocal;
idUserInterfaceManager *	uiManager = &uiManagerLocal;

idUserInterfaceManaged::idUserInterfaceManaged( bool managed ) : refs( 1 ), allocationId( 0 ), managed( managed ) {
	if ( managed ) {
		uiManagerLocal.RegisterAllocation( this );
	}
}

idUserInterfaceManaged::~idUserInterfaceManaged() {
	if ( managed ) {
		uiManagerLocal.UnregisterGui( this );
	}
}

void idUserInterfaceManaged::RegisterLoaded() {
	if ( managed ) {
		uiManagerLocal.RegisterGui( this );
	}
}

void idUserInterfaceManaged::RegisterDemo() {
	if ( managed ) {
		uiManagerLocal.RegisterDemoGui( this );
	}
}

void idUserInterfaceManaged::RefreshThinking() {
	if ( managed ) {
		uiManagerLocal.UpdateAlwaysThinkGui( this );
	}
}

bool UI_IsRetainedPath( const char *qpath ) {
	if ( qpath == NULL ) { return false; }
	const int length = idStr::Length( qpath );
	return length >= 5 && idStr::Icmp( qpath + length - 5, ".q4ui" ) == 0;
}

idUserInterfaceManaged *UI_CreateForPath( const char *qpath, bool managed ) {
	if ( UI_IsRetainedPath( qpath ) ) {
#ifndef ID_DEDICATED
		return new idUserInterfaceRetained( managed );
#else
		return NULL;
#endif
	}
	return new idUserInterfaceLocal( managed );
}

bool UI_DispatchApplicationActions( idUserInterface *gui, const char *command, bool &closeRequested ) {
	return uiManagerLocal.DispatchApplicationActions( gui, command, closeRequested );
}

std::uint64_t UI_NextTextLifetime() {
	// Engine-thread confined; never reset on manager or renderer shutdown.
	static std::uint64_t next = 0;
	if (next == (std::numeric_limits<std::uint64_t>::max)()) return 0;
	return ++next;
}

openq4::ui::TextBrokerContext UI_QueryTextContext(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession) {
	return uiManagerLocal.QueryTextContext(current,nativeWindow,nativeSession);
}

uiTextDeliveryResult_t UI_DeliverTextInput(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession,
	const openq4::ui::TextBrokerContext& authorizedContext,
	const openq4::ui::TextBrokerDelivery& delivery) {
	return uiManagerLocal.DeliverTextInput(current,nativeWindow,nativeSession,authorizedContext,delivery);
}

openq4::ui::TextBrokerContext idUserInterfaceManagerLocal::QueryTextContext(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession) {
#ifdef ID_DEDICATED
	(void)current; (void)nativeWindow; (void)nativeSession;
	return {};
#else
	if (textBoundaryActive || nativeBoundaryActive) { textBoundaryFailed = true; if(nativeBoundaryActive)nativeBoundaryFailed=true; return {}; }
	if (!current || !nativeWindow || !nativeSession) return {};
	textBoundaryActive = true; textBoundaryFailed = false;
	struct Guard { bool& active; ~Guard() { active = false; } } guard{textBoundaryActive};
	try {
		for (int i = 0; i < allocations.Num(); ++i) {
			if (allocations[i] != current || !allocations[i]->allocationId) continue;
			const auto allocation = allocations[i]->allocationId;
			const auto result = allocations[i]->QueryTextContext(allocation,nativeWindow,nativeSession);
			if (textBoundaryFailed) return {};
			// Refresh may invalidate membership. Do not dereference a saved pointer.
			for (int j = 0; j < allocations.Num(); ++j)
				if (allocations[j] == current && allocations[j]->allocationId == allocation) return result;
			return {};
		}
	} catch (...) { return {}; }
	return {};
#endif
}

uiTextDeliveryResult_t idUserInterfaceManagerLocal::DeliverTextInput(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession,
	const openq4::ui::TextBrokerContext& authorizedContext,
	const openq4::ui::TextBrokerDelivery& delivery) {
#ifdef ID_DEDICATED
	(void)current; (void)nativeWindow; (void)nativeSession; (void)authorizedContext; (void)delivery;
	return {openq4::ui::TextDeliveryOutcome::Rejected,{},"GUI text delivery is unavailable in a dedicated server"};
#else
	using namespace openq4::ui;
	uiTextDeliveryResult_t result;
	if (textBoundaryActive || nativeBoundaryActive) {
		textBoundaryFailed = true; if(nativeBoundaryActive)nativeBoundaryFailed=true; result.diagnostic = "Reentrant GUI text delivery"; return result;
	}
	if (!current || !nativeWindow || !nativeSession || !delivery.token || !delivery.sequence || !delivery.target.allocation) {
		result.diagnostic = "Invalid GUI text delivery identity"; return result;
	}
	textBoundaryActive = true; textBoundaryFailed = false;
	struct Guard { bool& active; ~Guard() { active = false; } } guard{textBoundaryActive};
	try {
		// Freeze the authorized context and payload before callbacks can refresh
		// resources. Native session and edit session are different identities.
		const TextBrokerContext expected = authorizedContext;
		if (expected.route != TextBrokerRoute::Retained || !expected.editor || *expected.editor != delivery.target) {
			result.diagnostic = "GUI text authorization does not match its delivery"; return result;
		}
		if (!ValidateTextInputEvent(delivery.input,result.diagnostic)) return result;
		const TextBrokerDelivery request = delivery;
		const auto resolve = [&]() -> idUserInterfaceManaged* {
			for (int i = 0; i < allocations.Num(); ++i)
				if (allocations[i] == current && allocations[i]->allocationId == request.target.allocation) return allocations[i];
			return NULL;
		};
		auto* gui = resolve();
		if (!gui) { result.diagnostic = "GUI text allocation is no longer current"; return result; }
		result.context = gui->QueryTextContext(request.target.allocation,nativeWindow,nativeSession);
		gui = resolve();
		if (textBoundaryFailed || !gui) {
			result.context = {}; result.diagnostic = "GUI text query invalidated its owner"; return result;
		}
		if (result.context != expected) { result.diagnostic = "GUI text editor identity changed"; return result; }
		const bool applied = gui->ApplyTextInput(expected,request.input,result.diagnostic);
		gui = resolve();
		if (textBoundaryFailed || !gui) {
			result.context = {}; result.diagnostic = "GUI text delivery invalidated its owner"; return result;
		}
		result.context = gui->QueryTextContext(request.target.allocation,nativeWindow,nativeSession);
		if (textBoundaryFailed || !resolve()) {
			result.context = {}; result.diagnostic = "GUI text receipt invalidated its owner"; return result;
		}
		if (applied) result.outcome = result.context.editor && result.context.editor->revision == request.target.revision ?
			TextDeliveryOutcome::AppliedNoChange : TextDeliveryOutcome::AppliedChanged;
		// Complete() checks this actual receipt; never relabel a new editor as old.
	} catch (...) { result.context = {}; result.diagnostic = "GUI text boundary failed"; }
	return result;
#endif
}


// Private native collection owner boundary. All manager re-resolution happens
// outside backend method stacks; pure publication and teardown allocate nothing.
namespace {
struct NativeOwnerBoundaryScope {
    bool& active;
    ~NativeOwnerBoundaryScope() { active=false; }
};
void NativeOwnerDiagnostic(std::string& error,const char* message) noexcept {
    try {error=message;} catch (...) {error.clear();}
}
}
openq4::ui::NativeTextPresence idUserInterfaceManagerLocal::NativeTextPresence(openq4::ui::NativeTextIdentity native,
    const openq4::ui::TextEditorIdentity& owner) const noexcept {
    using Presence=openq4::ui::NativeTextPresence;
    if (std::this_thread::get_id()!=nativePresenceThread || nativeBoundaryActive || textBoundaryActive ||
        clipboardBoundaryActive || applicationPumpDepth || !native.document || !native.editorLease ||
        !owner.allocation || owner.allocation>nextAllocationId || !owner.backend || !owner.document ||
        !owner.modal || !owner.window || !owner.session || !owner.revision || owner.control.empty() ||
        owner.control.size()>128 || owner.control.find('\0')!=std::string::npos) return Presence::BusyOrUnknown;
#ifdef ID_DEDICATED
    return Presence::BusyOrUnknown;
#else
    for (int i=0;i<allocations.Num();++i)
        if (allocations[i]->allocationId==owner.allocation) return allocations[i]->QueryNativeTextPresence(native,owner);
    return Presence::AbsentOriginal;
#endif
}
bool idUserInterfaceManagerLocal::NativeTextEnter() noexcept {
    if(nativeBoundaryActive || textBoundaryActive || clipboardBoundaryActive) {
        nativeBoundaryFailed=true;
        if(textBoundaryActive)textBoundaryFailed=true;
        if(clipboardBoundaryActive)clipboardBoundaryFailed=true;
        return false;
    }
    nativeBoundaryActive=true;nativeBoundaryFailed=false;return true;
}
idUserInterfaceManaged* idUserInterfaceManagerLocal::NativeTextResolve(uiNativeTextRouteProbe_t probe,void* context,
    const openq4::ui::TextEditorIdentity& owner) const noexcept {
#ifdef ID_DEDICATED
    (void)probe;(void)context;(void)owner;return nullptr;
#else
    if(!probe || !owner.allocation || !owner.window)return nullptr;
    const auto route=probe(context);
    if(!route.current || !route.inputAllowed || route.window!=owner.window)return nullptr;
    for(int i=0;i<allocations.Num();++i)
        if(allocations[i]==route.current && allocations[i]->allocationId==owner.allocation)return allocations[i];
    return nullptr;
#endif
}
bool idUserInterfaceManagerLocal::NativeTextCheck(uiNativeTextRouteProbe_t probe,void* context,
    const openq4::ui::NativeTextEditorBarrier& expected) const noexcept {
    if(nativeBoundaryFailed)return false;
    auto* owner=NativeTextResolve(probe,context,expected.editor);
    return owner && owner->CurrentNativeText(expected);
}
bool idUserInterfaceManagerLocal::NativeTextCurrent(uiNativeTextRouteProbe_t probe,void* context,
    const openq4::ui::NativeTextEditorBarrier& expected) noexcept {
    if(!NativeTextEnter())return false;
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    return NativeTextCheck(probe,context,expected);
}
bool idUserInterfaceManagerLocal::NativeTextPublishSettlement(uiNativeTextRouteProbe_t probe,void* context,
    openq4::ui::Interaction::NativeSettlement& prepared,openq4::ui::NativeTextEditorReceipt& out) noexcept {
#ifdef ID_DEDICATED
    (void)probe;(void)context;(void)prepared;(void)out;return false;
#else
    if(!NativeTextEnter())return false;
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    const auto& expected=prepared.Receipt().before;
    if(!NativeTextCheck(probe,context,expected))return false;
    auto* owner=NativeTextResolve(probe,context,expected.editor);
    return owner && owner->PublishNativeTextSettlement(prepared,out);
#endif
}
bool idUserInterfaceManagerLocal::NativeTextRetireExact(openq4::ui::NativeTextIdentity native,
    const openq4::ui::TextEditorIdentity& owner) noexcept {
    // Retirement is permitted inside a failed boundary. Poison its in-flight
    // receipt first, then touch only the original registered lease without any
    // resource preparation, host observation or current-route requirement.
    if(nativeBoundaryActive)nativeBoundaryFailed=true;
    if(textBoundaryActive)textBoundaryFailed=true;
    if(clipboardBoundaryActive)clipboardBoundaryFailed=true;
#ifdef ID_DEDICATED
    (void)native;(void)owner;return false;
#else
    if(!owner.allocation || !native.document || !native.editorLease)return false;
    for(int i=0;i<allocations.Num();++i)
        if(allocations[i]->allocationId==owner.allocation)return allocations[i]->RetireNativeTextExact(native,owner);
    return false;
#endif
}

bool idUserInterfaceManagerLocal::NativeTextAttach(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::TextEditorIdentity& owner, openq4::ui::NativeTextIdentity native, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return false;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=owner;const auto nativeCopy=native;
        openq4::ui::NativeTextEditorBarrier candidate;
        const bool ready=[&]{auto* backend=NativeTextResolve(probe,context,frozen);return backend && backend->PrepareNativeText(frozen);}();
        if(!ready || nativeBoundaryFailed)return false;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen);return backend && backend->AttachNativeText(frozen,nativeCopy,candidate,error);}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate))return false;
        out=std::move(candidate);return true;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return false;}
}

bool idUserInterfaceManagerLocal::NativeTextRefresh(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, openq4::ui::NativeTextEditorView& out, std::string& error) {
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return false;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=expected;
        openq4::ui::NativeTextEditorView candidate;
        const bool ready=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && backend->PrepareNativeText(frozen.editor);}();
        if(!ready || nativeBoundaryFailed)return false;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && backend->RefreshNativeText(frozen,candidate,error);}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate.barrier))return false;
        if(candidate.barrier!=frozen)return false;out=std::move(candidate);return true;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return false;}
}

bool idUserInterfaceManagerLocal::NativeTextBegin(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return false;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=expected;const auto collectionCopy=collection;
        openq4::ui::NativeTextEditorBarrier candidate;
        if(!NativeTextCheck(probe,context,frozen))return false;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && backend->BeginNativeText(frozen,collectionCopy,candidate,error);}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate))return false;
        out=std::move(candidate);return true;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return false;}
}

bool idUserInterfaceManagerLocal::NativeTextApply(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextOffer& offer, openq4::ui::NativeTextEditorReceipt& out, std::string& error) {
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return false;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=expected;const auto offerCopy=offer;
        openq4::ui::NativeTextEditorReceipt candidate;
        if(!NativeTextCheck(probe,context,frozen))return false;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && backend->ApplyNativeText(frozen,offerCopy,candidate,error);}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate.after))return false;
        out=std::move(candidate);return true;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return false;}
}

bool idUserInterfaceManagerLocal::NativeTextComplete(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return false;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=expected;const auto collectionCopy=collection;
        openq4::ui::NativeTextEditorBarrier candidate;
        if(!NativeTextCheck(probe,context,frozen))return false;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && backend->CompleteNativeText(frozen,collectionCopy,candidate,error);}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate))return false;
        out=std::move(candidate);return true;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return false;}
}

std::unique_ptr<openq4::ui::Interaction::NativeSettlement> idUserInterfaceManagerLocal::NativeTextPrepareSettlement(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, std::string& error) {
#ifdef ID_DEDICATED
    (void)probe;(void)context;(void)expected;(void)error;return nullptr;
#else
    if(!NativeTextEnter()){NativeOwnerDiagnostic(error,"Reentrant native GUI owner boundary");return nullptr;}
    NativeOwnerBoundaryScope scope{nativeBoundaryActive};
    try {
        const auto frozen=expected;
        std::unique_ptr<openq4::ui::Interaction::NativeSettlement> candidate;
        if(!NativeTextCheck(probe,context,frozen))return nullptr;
        const bool accepted=[&]{auto* backend=NativeTextResolve(probe,context,frozen.editor);return backend && (candidate=backend->PrepareNativeTextSettlement(frozen,error)) != nullptr;}();
        if(!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,frozen))return nullptr;
        return candidate;
    } catch (...) {NativeOwnerDiagnostic(error,"Native GUI owner allocation or callback failed");return nullptr;}
#endif
}

bool UI_NativeTextCurrent(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected) noexcept {
    return uiManagerLocal.NativeTextCurrent(probe,context,expected);
}

bool UI_NativeTextPublishSettlement(uiNativeTextRouteProbe_t probe,void* context, openq4::ui::Interaction::NativeSettlement& prepared,openq4::ui::NativeTextEditorReceipt& out) noexcept {
    return uiManagerLocal.NativeTextPublishSettlement(probe,context,prepared,out);
}

bool UI_NativeTextAttach(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::TextEditorIdentity& owner, openq4::ui::NativeTextIdentity native, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    return uiManagerLocal.NativeTextAttach(probe,context,owner,native,out,error);
}

bool UI_NativeTextRefresh(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, openq4::ui::NativeTextEditorView& out, std::string& error) {
    return uiManagerLocal.NativeTextRefresh(probe,context,expected,out,error);
}

bool UI_NativeTextBegin(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    return uiManagerLocal.NativeTextBegin(probe,context,expected,collection,out,error);
}

bool UI_NativeTextApply(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextOffer& offer, openq4::ui::NativeTextEditorReceipt& out, std::string& error) {
    return uiManagerLocal.NativeTextApply(probe,context,expected,offer,out,error);
}

bool UI_NativeTextComplete(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
    return uiManagerLocal.NativeTextComplete(probe,context,expected,collection,out,error);
}

std::unique_ptr<openq4::ui::Interaction::NativeSettlement> UI_NativeTextPrepareSettlement(uiNativeTextRouteProbe_t probe,void* context, const openq4::ui::NativeTextEditorBarrier& expected, std::string& error) {
    return uiManagerLocal.NativeTextPrepareSettlement(probe,context,expected,error);
}

openq4::ui::NativeTextPresence UI_NativeTextPresence(openq4::ui::NativeTextIdentity native,
    const openq4::ui::TextEditorIdentity& owner) noexcept {
    return uiManagerLocal.NativeTextPresence(native,owner);
}
bool UI_NativeTextRetireExact(openq4::ui::NativeTextIdentity native,const openq4::ui::TextEditorIdentity& owner) noexcept {
    return uiManagerLocal.NativeTextRetireExact(native,owner);
}

bool idUserInterfaceManagerLocal::DispatchApplicationActions( idUserInterface *gui, const char *command, bool &closeRequested ) {
	closeRequested = false;
#ifdef ID_DEDICATED
	for (int i = 0; i < allocations.Num(); ++i) {
		if (allocations[i] == gui) return allocations[i]->DispatchApplicationActions(command,closeRequested);
	}
	return false;
#else
	using namespace openq4::ui;
	if (clipboardBoundaryActive || nativeBoundaryActive) { clipboardBoundaryFailed = true; if(nativeBoundaryActive)nativeBoundaryFailed=true; return true; }
	unsigned long long allocation = 0;
	for (int i = 0; i < allocations.Num(); ++i) if (allocations[i] == gui) { allocation = allocations[i]->allocationId; break; }
	if (!allocation) return false;
	// command may point into the backend being replaced by the native callback.
	const std::string marker = command ? command : "";
	auto resolve = [&]() -> idUserInterfaceManaged* {
		for (int i = 0; i < allocations.Num(); ++i)
			if (allocations[i] == gui && allocations[i]->allocationId == allocation) return allocations[i];
		return nullptr;
	};
	clipboardBoundaryActive = true; clipboardBoundaryFailed = false;
	struct Guard { bool& active; ~Guard() { active = false; } } guard{clipboardBoundaryActive};
	bool handled = false;
	try {
		for (unsigned count = 0; count <= 256; ++count) {
			auto* owner = resolve(); if (!owner || clipboardBoundaryFailed) { closeRequested = false; return true; }
			handled = owner->DispatchApplicationActions(marker.c_str(),closeRequested);
			owner = resolve();
			if (!owner || clipboardBoundaryFailed) { closeRequested = false; return true; }
			if (!handled || closeRequested) return handled;
			uiClipboardRequest_t request;
			if (count == 256 || !owner->TakeClipboardRequest(marker.c_str(),request)) return handled;
			// All following work uses copied values. No backend method is on the
			// stack when clipboard access can pump/re-enter or replace that backend.
			uiNumberEditorSnapshot_t before; std::string error;
			auto query = [&](uiNumberEditorSnapshot_t& out) {
				auto* live = resolve();
				if (!live || clipboardBoundaryFailed || !live->QueryClipboardEditor(out,error)) return false;
				return !clipboardBoundaryFailed && resolve() && out.target == request.target &&
					out.target.backend && out.target.document && out.target.modal && !out.target.control.empty() &&
					out.target.edit.session && out.target.edit.revision && out.editor.identity == out.target.edit &&
					out.editor.active && !out.editor.conflict && !out.editor.composition;
			};
			if (!query(before)) continue;
			const auto& state = before.editor.state;
			TextInputEvent checked;
			if (!MakeTextInputCommit(state.text,checked,error) || state.anchor > state.text.size() || state.caret > state.text.size()) continue;
			const auto boundary = [&](std::size_t offset) {
				return offset == state.text.size() || (static_cast<unsigned char>(state.text[offset]) & 0xc0) != 0x80;
			};
			if (!boundary(state.anchor) || !boundary(state.caret)) continue;
			const auto first = (std::min)(state.anchor,state.caret), last = (std::max)(state.anchor,state.caret);
			const std::string selection = state.text.substr(first,last-first);
			if (!MakeTextInputCommit(selection,checked,error)) continue;
			if (request.operation != uiClipboardOperation_t::Copy && request.operation != uiClipboardOperation_t::Cut &&
				request.operation != uiClipboardOperation_t::Paste) continue;
			std::string paste; bool succeeded = true;
			NumberEditNotice notice = NumberEditNotice::None;
			try {
				if (request.operation == uiClipboardOperation_t::Paste) {
					succeeded = openq4::SDL3_ReadTextClipboard(paste,error);
					if (!succeeded) notice = NumberEditNotice::ClipboardReadFailed;
				} else if (!selection.empty()) {
					succeeded = openq4::SDL3_WriteTextClipboard(selection,error);
					if (!succeeded) notice = NumberEditNotice::ClipboardWriteFailed;
				}
			} catch (...) {
				succeeded = false;
				notice = request.operation == uiClipboardOperation_t::Paste ? NumberEditNotice::ClipboardReadFailed : NumberEditNotice::ClipboardWriteFailed;
			}
			uiNumberEditorSnapshot_t after;
			if (!query(after) || after.editor.state.text != state.text || after.editor.state.anchor != state.anchor ||
				after.editor.state.caret != state.caret) continue;
			// Empty Paste is a successful no-op, never deletion of the selection.
			if (succeeded && ((request.operation == uiClipboardOperation_t::Cut && !selection.empty()) ||
				(request.operation == uiClipboardOperation_t::Paste && !paste.empty()))) {
				if (!MakeTextInputCommit(paste,checked,error)) notice = NumberEditNotice::ClipboardRejected;
				else if (auto* live = resolve()) {
					if (live->ReplaceClipboardSelection(request.target,paste,error)) continue;
					notice = NumberEditNotice::ClipboardRejected;
				}
			}
			// A refusal reports only a fixed localized category to the same live
			// editor. Native diagnostics never become authored field contents.
			if (!clipboardBoundaryFailed) if (auto* live = resolve()) live->SetClipboardNotice(request.target,notice,error);
		}
	} catch (...) { closeRequested = false; return true; }
	return handled;
#endif
}

void UI_PumpApplicationActions( UI_ApplicationCommandCallback callback, void *context, idUserInterface *only ) {
	uiManagerLocal.PumpApplicationActions( callback, context, only );
}

void idUserInterfaceManagerLocal::PumpApplicationActions( UI_ApplicationCommandCallback callback, void *context, idUserInterface *only ) {
	// Nested global pumps cannot replay the current batch. A targeted lifecycle
	// drain may run inside a close callback, before an outgoing test GUI is freed.
	if ( callback == NULL || ( applicationPumpDepth != 0 && only == NULL ) || applicationPumpDepth >= 8 ) return;
	if ( applicationPumpDepth == 0 ) applicationPumpBudget = 256;
	struct DepthScope {
		int &depth;
		explicit DepthScope( int &value ) : depth( value ) { ++depth; }
		~DepthScope() { --depth; }
	} scope( applicationPumpDepth );
	idList<idUserInterfaceManaged*> ready;
	idList<unsigned long long> identities;
	idList<idStr> commands;
	for ( int i = 0; i < allocations.Num() && ready.Num() < applicationPumpBudget; ++i ) {
		idUserInterfaceManaged *gui = allocations[ i ];
		if ( only != NULL && only != gui ) continue;
		const char *command = gui->PendingApplicationCommand();
		if ( command == NULL || command[ 0 ] == '\0' ) continue;
		ready.Append( gui );
		identities.Append( gui->allocationId );
		commands.Append( idStr( command ) );
	}
	for ( int i = 0; i < ready.Num() && applicationPumpBudget > 0; ++i ) {
		idUserInterfaceManaged *gui = ready[ i ];
		if ( allocations.Find( gui ) == NULL || gui->allocationId != identities[ i ] ) continue;
		--applicationPumpBudget;
		callback( gui, commands[ i ].c_str(), context );
		// The callback may delete this or any peer, including reusing its address.
		// Never retain a registry iterator or dereference the owner afterward.
	}
}

namespace {

// Resolve presentation aliases without parser fixup. GetWinVarByName(..., true)
// can disable a root expression or allocate a gui:: variable as a side effect.
// External value queries must do neither, including when used by diagnostics.
static idWinVar *FindPresentationVariable( idWindow *desktop, const char *name ) {
	if ( desktop == NULL || name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}
	idStr key = name;
	const int separator = key.Find( "::" );
	if ( separator < 0 ) {
		return desktop->GetWinVarByName( key.c_str(), false );
	}
	if ( separator == 0 || separator + 2 == key.Length() ) {
		return NULL;
	}
	const idStr element = key.Left( separator );
	key = key.Right( key.Length() - separator - 2 );
	if ( key.Find( "::" ) >= 0 ) {
		return NULL;
	}
	drawWin_t *target = desktop->FindChildByName( element.c_str() );
	if ( target == NULL ) {
		return NULL;
	}
	if ( target->win != NULL ) {
		return target->win->GetWinVarByName( key.c_str(), false );
	}
	return target->simp != NULL ? target->simp->GetWinVarByName( key.c_str() ) : NULL;
}

static void SetStateRectangleComponents( idUserInterfaceLocal *gui, const char *prefix, const idRectangle &rect ) {
	if ( gui == NULL || prefix == NULL ) {
		return;
	}

	gui->SetStateFloat( va( "%s_x", prefix ), rect.x );
	gui->SetStateFloat( va( "%s_y", prefix ), rect.y );
	gui->SetStateFloat( va( "%s_w", prefix ), rect.w );
	gui->SetStateFloat( va( "%s_h", prefix ), rect.h );
}

}

bool idUserInterfaceLocal::GetPresentationValue( const char *name, idStr &value ) const {
	idWinVar *variable = FindPresentationVariable( desktop, name );
	if ( variable == NULL ) {
		return false;
	}
	value = variable->c_str();
	return true;
}

bool idUserInterfaceLocal::SetPresentationValue( const char *name, const char *value, bool overrideExpression ) {
	if ( value == NULL ) {
		return false;
	}
	idWinVar *variable = FindPresentationVariable( desktop, name );
	if ( variable == NULL ) {
		return false;
	}
	variable->Set( value );
	if ( overrideExpression ) {
		variable->SetEval( false );
	}
	return true;
}

bool idUserInterfaceLocal::GetTextInputState( idRectangle &area, float &cursorOffset ) const {
	if ( desktop == NULL ) {
		return false;
	}
	idEditWindow *edit = dynamic_cast<idEditWindow *>( desktop->GetFocusedChild() );
	idRectangle candidateArea;
	float candidateOffset = 0.0f;
	if ( edit == NULL || !edit->GetTextInputState( candidateArea, candidateOffset ) ) {
		return false;
	}
	area = candidateArea;
	cursorOffset = candidateOffset;
	return true;
}

/*
===============================================================================

	idUserInterfaceManagerLocal

===============================================================================
*/

void idUserInterfaceManagerLocal::Init() {
	RetainedUI_Init();
	cmdSystem->AddCommand("chatHistory", idChatWindow::History_f, CMD_FL_SYSTEM, "browse open chat: up, down, top, bottom, status");
	screenRect = idRectangle(0, 0, 640, 480);
	dc.Init();
}

void idUserInterfaceManagerLocal::Shutdown() {
	cmdSystem->RemoveCommand("chatHistory");
	idChatWindow::Reset();
	// Destruction unregisters from every list. Take one live allocation at a
	// time instead of iterating a container that its destructor will mutate.
	while ( allocations.Num() > 0 ) {
		delete allocations[ allocations.Num() - 1 ];
	}
	RetainedUI_Shutdown();
	dc.Shutdown();
}

void idUserInterfaceManagerLocal::Touch( const char *name ) {
	idUserInterface *gui = Alloc();
	if ( !gui->InitFromFile( name ) ) {
		delete gui;
	}
}

void idUserInterfaceManagerLocal::WritePrecacheCommands( idFile *f ) {

	int c = guis.Num();
	for( int i = 0; i < c; i++ ) {
		idStr command = "touchGui ";
		command += guis[i]->Name();
		command += "\n";
		common->Printf( "%s", command.c_str() );
		f->Printf( "%s", command.c_str() );
	}
}

void idUserInterfaceManagerLocal::SetSize( float width, float height ) {
	if ( width > 0.0f && height > 0.0f ) {
		screenRect = idRectangle( 0.0f, 0.0f, width, height );
	}
	dc.SetSize( width, height );
}

void idUserInterfaceManagerLocal::SetAspectCorrection( bool enabled ) {
	dc.SetAspectCorrection( enabled );
}

void idUserInterfaceManagerLocal::BeginLevelLoad() {
	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( !guis[ i ]->IsMenuGui() ) {
			guis[ i ]->ClearRefs();
		}
	}
}

void idUserInterfaceManagerLocal::EndLevelLoad() {
	for ( int i = 0; i < guis.Num(); ) {
		if ( guis[i]->GetRefs() == 0 ) {
			//common->Printf( "purging %s.\n", guis[i]->GetSourceFile() );

			// use this to make sure no materials still reference this gui
			bool remove = true;
			for ( int j = 0; j < declManager->GetNumDecls( DECL_MATERIAL ); j++ ) {
				const idMaterial *material = static_cast<const idMaterial *>(declManager->DeclByIndex( DECL_MATERIAL, j, false ));
				if ( material->GlobalGui() == guis[i] ) {
					remove = false;
					break;
				}
			}
			if ( remove ) {
				delete guis[ i ];
				continue;
			}
		}
		i++;
	}

	// icons registered before their image was resident can be sized now
	dc.SizeIcons();
}

// RAVEN BEGIN
// bdube: embedded icons
// The game registers the inline text icons it needs ( weapon and means-of-death
// obituary icons, team/ready/voice icons ) from "icon <code>" spawn args while
// caching entity def media, so this has to reach the device context or every
// ^i escape in game text silently draws nothing.
void idUserInterfaceManagerLocal::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	dc.RegisterIcon( code, shader, x, y, w, h );
}
// RAVEN END

void idUserInterfaceManagerLocal::Reload( bool all ) {
	ID_TIME_T ts;

	const idList<idUserInterfaceManaged*> reloadGuis = guis;
	idList<unsigned long long> identities;
	for ( int i = 0; i < reloadGuis.Num(); i++ ) {
		identities.Append( reloadGuis[ i ]->allocationId );
	}
	for ( int i = 0; i < reloadGuis.Num(); i++ ) {
		idUserInterfaceManaged *gui = reloadGuis[ i ];
		if ( guis.Find( gui ) == NULL || gui->allocationId != identities[ i ] ) {
			continue;
		}
		// InitFromFile may replace the owned source string while parsing.
		const idStr sourcePath = gui->GetSourceFile();
		if ( !all ) {
			fileSystem->ReadFile( sourcePath, NULL, &ts );
			if ( ts <= gui->GetTimeStamp() ) {
				continue;
			}
		}

		gui->InitFromFile( sourcePath );
		common->Printf( "reloading %s.\n", sourcePath.c_str() );
	}
}

void idUserInterfaceManagerLocal::ListGuis() const {
	int c = guis.Num();
	common->Printf( "\n   size   refs   name\n" );
	size_t total = 0;
	int copies = 0;
	int unique = 0;
	for ( int i = 0; i < c; i++ ) {
		idUserInterfaceManaged *gui = guis[i];
		size_t sz = gui->Size();
		bool isUnique = gui->IsInteractive();
		if ( isUnique ) {
			unique++;
		} else {
			copies++;
		}
		common->Printf( "%6.1fk %4i (%s) %s ( %i transitions )\n", sz / 1024.0f, gui->GetRefs(), isUnique ? "unique" : "copy", gui->GetSourceFile(), gui->NumTransitions() );
		total += sz;
	}
	common->Printf( "===========\n  %i total Guis ( %i copies, %i unique ), %.2f total Mbytes", c, copies, unique, total / ( 1024.0f * 1024.0f ) );
}

bool idUserInterfaceManagerLocal::CheckGui( const char *qpath ) const {
	idFile *file = fileSystem->OpenFileRead( qpath );
	if ( file ) {
		fileSystem->CloseFile( file );
		return true;
	}
	return false;
}

idUserInterface *idUserInterfaceManagerLocal::Alloc( void ) const {
	return new idUserInterfaceDeferred();
}

void idUserInterfaceManagerLocal::DeAlloc( idUserInterface *gui ) {
	if ( gui ) {
		int c = allocations.Num();
		for ( int i = 0; i < c; i++ ) {
			if ( allocations[i] == gui ) {
				delete allocations[i];
				return;
			}
		}
	}
}

idUserInterface *idUserInterfaceManagerLocal::FindGui( const char *qpath, bool autoLoad, bool needUnique, bool forceNOTUnique ) {
	if ( qpath == NULL || qpath[ 0 ] == '\0' ) {
		return NULL;
	}
	int c = guis.Num();

	for ( int i = 0; i < c; i++ ) {
		if ( !idStr::Icmp( guis[i]->GetSourceFile(), qpath ) ) {
			// Retail keeps unique GUI instances isolated even when state changes make them temporarily noninteractive.
			if ( !forceNOTUnique && ( needUnique || guis[i]->IsInteractive() || guis[i]->IsUniqued() ) ) {
				break;
			}
			guis[i]->AddRef();
			return guis[i];
		}
	}

	if ( autoLoad ) {
		// Editors use concrete legacy access for .guied documents. Only pathless
		// Alloc needs a deferred identity; named loads select their backend now.
		idUserInterface *gui = UI_CreateForPath( qpath );
		if ( gui != NULL && gui->InitFromFile( qpath ) ) {
			gui->SetUniqued( forceNOTUnique ? false : needUnique );
			return gui;
		} else {
			delete gui;
			if ( session != NULL && session->IsLoadingSaveGame() ) {
				common->Error( "Savegame restore could not load serialized GUI '%s'; aborting before its positional state payload can desynchronize the stream",
					qpath ? qpath : "<null>" );
			}
		}
	}
	return NULL;
}

idUserInterface *idUserInterfaceManagerLocal::FindDemoGui( const char *qpath ) {
	if ( qpath == NULL || qpath[ 0 ] == '\0' ) {
		return NULL;
	}
	int c = demoGuis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( !idStr::Icmp( demoGuis[i]->GetSourceFile(), qpath ) ) {
			return demoGuis[i];
		}
	}
	return NULL;
}

idListGUI *	idUserInterfaceManagerLocal::AllocListGUI( void ) const {
	return new idListGUILocal();
}

void idUserInterfaceManagerLocal::FreeListGUI( idListGUI *listgui ) {
	delete listgui;
}

void idUserInterfaceManagerLocal::RegisterAllocation( idUserInterfaceManaged *gui ) {
	if (nextAllocationId == (std::numeric_limits<unsigned long long>::max)()) {
		common->FatalError("GUI allocation identity exhausted"); return;
	}
	gui->allocationId = ++nextAllocationId;
	allocations.AddUnique( gui );
}

void idUserInterfaceManagerLocal::RegisterGui( idUserInterfaceManaged *gui ) {
	guis.AddUnique( gui );
	UpdateAlwaysThinkGui( gui );
}

void idUserInterfaceManagerLocal::RegisterDemoGui( idUserInterfaceManaged *gui ) {
	demoGuis.AddUnique( gui );
}

void idUserInterfaceManagerLocal::UnregisterGui( idUserInterfaceManaged *gui ) {
	RemoveAlwaysThinkGui( gui );
	guis.Remove( gui );
	demoGuis.Remove( gui );
	allocations.Remove( gui );
}

void idUserInterfaceManagerLocal::UpdateAlwaysThinkGui( idUserInterfaceManaged *gui ) {
	if ( gui == NULL || guis.Find( gui ) == NULL || !gui->AlwaysThink() ) {
		RemoveAlwaysThinkGui( gui );
		return;
	}

	alwaysThinkGUIs.AddUnique( gui );
}

void idUserInterfaceManagerLocal::RemoveAlwaysThinkGui( idUserInterfaceManaged *gui ) {
	if ( gui == NULL ) {
		return;
	}

	alwaysThinkGUIs.Remove( gui );
}

void idUserInterfaceManagerLocal::RunAlwaysThinkGUIs( int time ) {
	// A callback can remove a view. Visit the starting set once, checking
	// membership and allocation identity before dereferencing it; a new view
	// at a deleted object's address still waits until the next tick.
	const idList<idUserInterfaceManaged*> thinkers = alwaysThinkGUIs;
	idList<unsigned long long> identities;
	for ( int i = 0; i < thinkers.Num(); i++ ) {
		identities.Append( thinkers[ i ]->allocationId );
	}
	for ( int i = 0; i < thinkers.Num(); i++ ) {
		idUserInterfaceManaged *gui = thinkers[i];
		if ( gui == NULL || guis.Find( gui ) == NULL || gui->allocationId != identities[ i ] ) {
			continue;
		}
		if ( !gui->AlwaysThink() ) {
			RemoveAlwaysThinkGui( gui );
			continue;
		}

		gui->RunTimeEvents( time );
	}
}

/*
===============================================================================

	idUserInterfaceLocal

===============================================================================
*/

idUserInterfaceLocal::idUserInterfaceLocal( bool managed ) : idUserInterfaceManaged( managed ) {
	chatWindow = NULL;
	cursorX = cursorY = 0.0;
	desktop = NULL;
	loading = false;
	active = false;
	interactive = false;
	uniqued = false;
	initialized = false;
	controllerNavigation = false;
	bindHandler = NULL;
	lightColorVar = NULL;
	//so the reg eval in gui parsing doesn't get bogus values
	time = 0;
	timeStamp = 0;
}

idUserInterfaceLocal::~idUserInterfaceLocal() {
	delete desktop;
	desktop = NULL;
}

const char *idUserInterfaceLocal::Name() const {
	return source;
}

const char *idUserInterfaceLocal::Comment() const {
	if ( desktop ) {
		return desktop->GetComment();
	}
	return "";
}

bool idUserInterfaceLocal::IsInteractive() const {
	return interactive;
}

void idUserInterfaceLocal::SetInteractive(bool interactive) {
	this->interactive = interactive;
}

bool idUserInterfaceLocal::InitFromFile( const char *qpath, bool rebuild, bool cache ) { 

	if ( !( qpath && *qpath ) ) { 
		return false;
	}

	loading = true;

	if ( rebuild ) {
		delete desktop;
		chatWindow = NULL;
		desktop = new idWindow( this );
	} else if ( desktop == NULL ) {
		desktop = new idWindow( this );
	}

	source = qpath;
	state.Set( "text", "Test Text!" );

	idParser src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	//Load the timestamp so reload guis will work correctly
	fileSystem->ReadFile(qpath, NULL, &timeStamp);

	src.LoadFile( qpath );

	if ( src.IsLoaded() ) {
		idToken token;
		while( src.ReadToken( &token ) ) {
			if ( idStr::Icmp( token, "windowDef" ) == 0 ) {
				desktop->SetDC( &uiManagerLocal.dc );
				if ( desktop->Parse( &src, rebuild ) ) {
					desktop->SetFlag( WIN_DESKTOP );
					desktop->FixupParms();
				}
				continue;
			}
			else {
				common->Error("Parsing gui %s invalid token %s\n", qpath, token.c_str());
			}
		}

		state.Set( "name", qpath );
	} else {
		desktop->SetDC( &uiManagerLocal.dc );
		desktop->SetFlag( WIN_DESKTOP );
		desktop->name = "Desktop";
		desktop->text = va( "Invalid GUI: %s", qpath );
		desktop->rect = idRectangle( 0.0f, 0.0f, 640.0f, 480.0f );
		desktop->drawRect = desktop->rect;
		desktop->foreColor = idVec4( 1.0f, 1.0f, 1.0f, 1.0f );
		desktop->backColor = idVec4( 0.0f, 0.0f, 0.0f, 1.0f );
		desktop->SetupFromState();
		common->Warning( "Couldn't load gui: '%s'", qpath );
	}

	// Replace only the two stock chat surfaces. Installing the native control
	// here also supports retail PK4 GUIs without shipping replacement GUI art
	// or depending on openQ4's optional HUD scripts.
	const bool chatInput = !idStr::Icmp(qpath, "guis/mpmsgmode.gui");
	const bool chatHud = !idStr::Icmp(qpath, "guis/mphud.gui");
	if ((chatInput || chatHud) && chatWindow == NULL) {
		drawWin_t *legacy = desktop->FindChildByName(chatInput ? "MainWindow" : "p_history");
		if (legacy != NULL) {
			idWinVar *visibility = legacy->win != NULL ? legacy->win->GetWinVarByName("visible") : legacy->simp->GetWinVarByName("visible");
			if (visibility != NULL) { visibility->Set("0"); visibility->SetEval(false); }
		}
		chatWindow = new idChatWindow(&uiManagerLocal.dc, this, chatInput);
		desktop->InsertChild(chatWindow, NULL);
		chatWindow->SetParent(desktop);
		chatWindow->FixupParms();
		if (chatInput && active) {
			idStr ignored;
			chatWindow->Activate(true, ignored);
			desktop->SetFocus(chatWindow, false);
		}
	}
	interactive = desktop->Interactive();

	RegisterLoaded();

	loading = false;
	lightColorVar = NULL;
	initialized = false;

	return true; 
}

const char *idUserInterfaceLocal::HandleEvent( const sysEvent_t *event, int _time, bool *updateVisuals ) {

	time = _time;

	const bool controllerKey = event->evType == SE_KEY && event->evValue >= K_JOY1 && event->evValue <= K_JOY32;
	if (chatWindow != NULL && chatWindow->IsInput() && active && !controllerKey) {
		return chatWindow->HandleEvent(event, updateVisuals);
	}

	if ( bindHandler && event->evType == SE_KEY && event->evValue2 == 1 ) {
		const char *ret = bindHandler->HandleEvent( event, updateVisuals );
		bindHandler = NULL;
		return ret;
	}

	if ( event->evType == SE_MOUSE ) {
		SetCursor( cursorX + static_cast<float>( event->evValue ),
			cursorY + static_cast<float>( event->evValue2 ) );
	}

	if ( desktop ) {
		return desktop->HandleEvent( event, updateVisuals );
	} 

	return "";
}

void idUserInterfaceLocal::HandleNamedEvent ( const char* eventName ) {
	desktop->RunNamedEvent( eventName );
}

void idUserInterfaceLocal::Redraw( int _time, bool useAspectCorrection ) {
	SetStateInt( "mousex", (int)CursorX() );
	SetStateInt( "mousey", (int)CursorY() );
	if ( r_skipGuiShaders.GetInteger() > 5 ) {
		return;
	}
	if ( !loading && desktop ) {
		time = _time;
		renderSystem->SetFrameShaderTime( _time );
		if ( !initialized ) {
			initialized = true;
			desktop->Init();
		}

		const bool aspectCorrect = useAspectCorrection && ui_aspectCorrection.GetBool();
		uiManagerLocal.SetAspectCorrection( aspectCorrect );
		uiManagerLocal.SetSize( desktop->forceAspectWidth, desktop->forceAspectHeight );
		float xExpand = 0.0f;
		float yExpand = 0.0f;
		uiManagerLocal.dc.GetVirtualScreenExpansion( desktop->forceAspectWidth, desktop->forceAspectHeight, xExpand, yExpand );
		SetStateFloat( "virtual_screen_x_expand", xExpand );
		SetStateFloat( "virtual_screen_y_expand", yExpand );
		// Physical aspect of the region the authored canvas covers.  The expanded
		// logical area always maps onto the whole 2D viewport, so this is the only
		// value a gui needs to size cinematic framing that has to look the same on
		// every display, and it stays correct when aspect correction is disabled
		// and the canvas is stretched instead of expanded.
		SetStateFloat( "virtual_screen_aspect", uiManagerLocal.dc.GetCanvasAspect() );

		idRectangle cinematicTopBar;
		idRectangle cinematicBottomBar;
		idRectangle cinematicLeftBar;
		idRectangle cinematicRightBar;
		idRectangle cinematicVisibleArea;
		uiManagerLocal.dc.GetCinematic16x9Bars( desktop->forceAspectWidth, desktop->forceAspectHeight, cinematicTopBar, cinematicBottomBar, cinematicLeftBar, cinematicRightBar, cinematicVisibleArea );
		SetStateRectangleComponents( this, "cinematic_bar_top", cinematicTopBar );
		SetStateRectangleComponents( this, "cinematic_bar_bottom", cinematicBottomBar );
		SetStateRectangleComponents( this, "cinematic_bar_left", cinematicLeftBar );
		SetStateRectangleComponents( this, "cinematic_bar_right", cinematicRightBar );
		SetStateRectangleComponents( this, "cinematic_visible_area", cinematicVisibleArea );

		if ( gui_debugScript.GetInteger() > 4 ) {
			static int lastDebugRedrawTime = -10000;
			if ( _time - lastDebugRedrawTime >= 250 ) {
				common->Printf( "GUI: redraw gui=%s time=%d active=%d interactive=%d visible=%d r_skipGuiShaders=%d cursor=%.1f,%.1f\n",
					source.c_str(),
					_time,
					active ? 1 : 0,
					interactive ? 1 : 0,
					desktop->visible ? 1 : 0,
					r_skipGuiShaders.GetInteger(),
					cursorX, cursorY );
				lastDebugRedrawTime = _time;
			}
		}
		uiManagerLocal.dc.PushClipRect( uiManagerLocal.screenRect );
		desktop->Redraw( 0, 0 );
		uiManagerLocal.dc.PopClipRect();
	}
}

void idUserInterfaceLocal::DrawCursor() {
	if ( controllerNavigation ) {
		return;
	}
	if ( !desktop || desktop->GetFlags() & WIN_MENUGUI ) {
		uiManagerLocal.dc.DrawCursor(&cursorX, &cursorY, 32.0f );
	} else {
		uiManagerLocal.dc.DrawCursor(&cursorX, &cursorY, 64.0f );
	}
}

const idDict &idUserInterfaceLocal::State() const {
	return state;
}

void idUserInterfaceLocal::DeleteStateVar( const char *varName ) {
	state.Delete( varName );
}

void idUserInterfaceLocal::SetStateString( const char *varName, const char *value ) {
	const char *oldValue = state.GetString( varName, "" );
	state.Set( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		const char *newValue = value ? value : "";
		if ( idStr::Icmp( oldValue, newValue ) != 0 ) {
			common->Printf( "GUI: state %s = \"%s\" (was \"%s\") gui=%s\n", varName, newValue, oldValue, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateBool( const char *varName, const bool value ) {
	bool oldValue = state.GetBool( varName, "0" );
	state.SetBool( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %d (was %d) gui=%s\n", varName, value ? 1 : 0, oldValue ? 1 : 0, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateInt( const char *varName, const int value ) {
	int oldValue = state.GetInt( varName, "0" );
	state.SetInt( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %d (was %d) gui=%s\n", varName, value, oldValue, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateFloat( const char *varName, const float value ) {
	float oldValue = state.GetFloat( varName, "0" );
	state.SetFloat( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %.4f (was %.4f) gui=%s\n", varName, value, oldValue, source.c_str() );
		}
	}
}

const char* idUserInterfaceLocal::GetStateString( const char *varName, const char* defaultString ) const {
	return state.GetString(varName, defaultString);
}

bool idUserInterfaceLocal::GetStateBool( const char *varName, const char* defaultString ) const {
	return state.GetBool(varName, defaultString); 
}

int idUserInterfaceLocal::GetStateInt( const char *varName, const char* defaultString ) const {
	return state.GetInt(varName, defaultString);
}

float idUserInterfaceLocal::GetStateFloat( const char *varName, const char* defaultString ) const {
	return state.GetFloat(varName, defaultString);
}

idVec4 idUserInterfaceLocal::GetLightColor( void ) {
	if ( lightColorVar ) {
		return *lightColorVar;
	}
	return vec4_origin;
}

void idUserInterfaceLocal::StateChanged( int _time, bool redraw ) {
	time = _time;
	if (desktop) {
		desktop->StateChanged( redraw );
	}
	if ( state.GetBool( "noninteractive" ) ) {
		interactive = false;
	}
	else {
		if (desktop) {
			interactive = desktop->Interactive();
		} else {
			interactive = false;
		}
	}
}

const char *idUserInterfaceLocal::Activate(bool activate, int _time) {
	time = _time;
	active = activate;
	if ( desktop ) {
		activateStr = "";
		desktop->Activate( activate, activateStr );
		if (activate && chatWindow != NULL && chatWindow->IsInput()) {
			desktop->SetFocus(chatWindow, false);
		}
		return activateStr;
	}
	return "";
}

void idUserInterfaceLocal::Trigger(int _time) {
	time = _time;
	if ( desktop ) {
		desktop->Trigger();
	}
}

void idUserInterfaceLocal::ReadFromDemoFile( class idDemoFile *f ) {
	idStr work;
	f->ReadDict( state );
	source = state.GetString("name");

	if (desktop == NULL) {
		f->Log("creating new gui\n");
		desktop = new idWindow(this);
	   	desktop->SetFlag( WIN_DESKTOP );
	   	desktop->SetDC( &uiManagerLocal.dc );
		desktop->ReadFromDemoFile(f);
	} else {
		f->Log("re-using gui\n");
		desktop->ReadFromDemoFile(f, false);
	}

	float restoredCursorX = 0.0f;
	float restoredCursorY = 0.0f;
	f->ReadFloat( restoredCursorX );
	f->ReadFloat( restoredCursorY );
	SetCursor( restoredCursorX, restoredCursorY );

	RegisterDemo();
}

void idUserInterfaceLocal::WriteToDemoFile( class idDemoFile *f ) {
	idStr work;
	f->WriteDict( state );
	if (desktop) {
		desktop->WriteToDemoFile(f);
	}

	f->WriteFloat( cursorX );
	f->WriteFloat( cursorY );
}

static const int UI_MAX_SAVEGAME_STATE_ENTRIES = 16384;
static const int UI_MAX_SAVEGAME_STRING_LENGTH = 64 * 1024;
static const int UI_MAX_SAVEGAME_STATE_BYTES = 16 * 1024 * 1024;

static bool UI_SaveGameStringContainsNul( const idStr &string ) {
	for ( int i = 0; i < string.Length(); i++ ) {
		if ( string[i] == '\0' ) {
			return true;
		}
	}
	return false;
}

static bool UI_WriteSaveGameChecked( idFile *savefile, int bytesWritten, int expectedBytes, int offset, const char *detail ) {
	if ( bytesWritten == expectedBytes ) {
		return true;
	}
	common->Warning( "idUserInterfaceLocal::WriteToSaveGame: failed to write %s at offset %d (%d of %d bytes)",
		detail ? detail : "data", offset, bytesWritten, expectedBytes );
	return false;
}

static bool UI_WriteSaveGameInt( idFile *savefile, int value, const char *detail ) {
	const int offset = savefile->Tell();
	return UI_WriteSaveGameChecked( savefile, savefile->WriteInt( value ), static_cast<int>( sizeof( value ) ), offset, detail );
}

static bool UI_WriteSaveGameBool( idFile *savefile, bool value, const char *detail ) {
	const int offset = savefile->Tell();
	return UI_WriteSaveGameChecked( savefile, savefile->WriteUnsignedChar( value ? 1 : 0 ), 1, offset, detail );
}

static bool UI_WriteSaveGameFloat( idFile *savefile, float value, const char *detail ) {
	if ( !std::isfinite( value ) ) {
		common->Warning( "idUserInterfaceLocal::WriteToSaveGame: refusing non-finite %s",
			detail ? detail : "float" );
		return false;
	}
	const int offset = savefile->Tell();
	return UI_WriteSaveGameChecked( savefile, savefile->WriteFloat( value ), static_cast<int>( sizeof( value ) ), offset, detail );
}

static bool UI_WriteSaveGameString( idFile *savefile, const idStr &string, const char *detail ) {
	const int len = string.Length();
	if ( len < 0 || len > UI_MAX_SAVEGAME_STRING_LENGTH || UI_SaveGameStringContainsNul( string ) ) {
		common->Warning( "idUserInterfaceLocal::WriteToSaveGame: invalid %s length/content (%d bytes)",
			detail ? detail : "string", len );
		return false;
	}
	if ( !UI_WriteSaveGameInt( savefile, len, detail ) ) {
		return false;
	}
	if ( len == 0 ) {
		return true;
	}
	const int offset = savefile->Tell();
	return UI_WriteSaveGameChecked( savefile, savefile->Write( string.c_str(), len ), len, offset, detail );
}

bool idUserInterfaceLocal::WriteToSaveGame( idFile *savefile ) const {
	if ( savefile == NULL || desktop == NULL ) {
		common->Warning( "idUserInterfaceLocal::WriteToSaveGame: gui '%s' has no valid output file/desktop",
			source.c_str() );
		return false;
	}

	const int num = state.GetNumKeyVals();
	if ( num < 0 || num > UI_MAX_SAVEGAME_STATE_ENTRIES || !UI_WriteSaveGameInt( savefile, num, "state count" ) ) {
		common->Warning( "idUserInterfaceLocal::WriteToSaveGame: gui '%s' has invalid state count %d",
			source.c_str(), num );
		return false;
	}

	int64 totalStateBytes = 0;
	for ( int i = 0; i < num; i++ ) {
		const idKeyValue *kv = state.GetKeyVal( i );
		if ( kv == NULL || kv->GetKey().IsEmpty() ) {
			common->Warning( "idUserInterfaceLocal::WriteToSaveGame: gui '%s' has an invalid state entry at index %d",
				source.c_str(), i );
			return false;
		}
		totalStateBytes += kv->GetKey().Length();
		totalStateBytes += kv->GetValue().Length();
		if ( totalStateBytes > UI_MAX_SAVEGAME_STATE_BYTES ||
			 !UI_WriteSaveGameString( savefile, kv->GetKey(), "state key" ) ||
			 !UI_WriteSaveGameString( savefile, kv->GetValue(), "state value" ) ) {
			common->Warning( "idUserInterfaceLocal::WriteToSaveGame: gui '%s' state entry %d exceeds the savegame budget or could not be written",
				source.c_str(), i );
			return false;
		}
	}

	if ( !UI_WriteSaveGameBool( savefile, active, "active flag" ) ||
		 !UI_WriteSaveGameBool( savefile, interactive, "interactive flag" ) ||
		 !UI_WriteSaveGameBool( savefile, uniqued, "unique flag" ) ||
		 !UI_WriteSaveGameInt( savefile, time, "time" ) ||
		 !UI_WriteSaveGameString( savefile, activateStr, "activate command" ) ||
		 !UI_WriteSaveGameString( savefile, pendingCmd, "pending command" ) ||
		 !UI_WriteSaveGameString( savefile, returnCmd, "return command" ) ||
		 !UI_WriteSaveGameFloat( savefile, cursorX, "cursor x" ) ||
		 !UI_WriteSaveGameFloat( savefile, cursorY, "cursor y" ) ) {
		return false;
	}

	desktop->WriteToSaveGame( savefile );
	return true;
}

static bool UI_ReadSaveGameBytes( idFile *savefile, void *buffer, int len, const char *detail ) {
	const int offset = savefile->Tell();
	const int bytesRead = savefile->Read( buffer, len );
	if ( bytesRead != len ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			detail ? detail : "data", offset, bytesRead, len );
		return false;
	}
	return true;
}

static bool UI_ReadSaveGameInt( idFile *savefile, int &value, const char *detail ) {
	const int offset = savefile->Tell();
	const int bytesRead = savefile->ReadInt( value );
	if ( bytesRead != static_cast<int>( sizeof( value ) ) ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			detail ? detail : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool UI_ReadSaveGameBool( idFile *savefile, bool &value, const char *detail ) {
	unsigned char savedValue = 0;
	const int offset = savefile->Tell();
	const int bytesRead = savefile->ReadUnsignedChar( savedValue );
	if ( bytesRead != 1 || savedValue > 1 ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid %s at offset %d (read %d bytes, value %u)",
			detail ? detail : "boolean", offset, bytesRead, static_cast<unsigned int>( savedValue ) );
		return false;
	}
	value = savedValue != 0;
	return true;
}

static bool UI_ReadSaveGameFloat( idFile *savefile, float &value, const char *detail ) {
	const int offset = savefile->Tell();
	const int bytesRead = savefile->ReadFloat( value );
	if ( bytesRead != static_cast<int>( sizeof( value ) ) ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			detail ? detail : "float", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
		return false;
	}
	if ( !std::isfinite( value ) ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: non-finite %s at offset %d",
			detail ? detail : "float", offset );
		return false;
	}
	return true;
}

static bool UI_ReadSaveGameString( idFile *savefile, idStr &string, const char *detail ) {
	int len = 0;
	const int offset = savefile->Tell();
	if ( !UI_ReadSaveGameInt( savefile, len, detail ) ) {
		string.Clear();
		return false;
	}

	const int remainingBytes = Max( 0, savefile->Length() - savefile->Tell() );
	if ( len < 0 || len > UI_MAX_SAVEGAME_STRING_LENGTH || len > remainingBytes ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid %s length %d at offset %d (remaining %d)",
			detail ? detail : "string", len, offset, remainingBytes );
		string.Clear();
		return false;
	}

	string.Fill( ' ', len );
	if ( len > 0 && !UI_ReadSaveGameBytes( savefile, &string[0], len, detail ) ) {
		string.Clear();
		return false;
	}
	return true;
}

bool idUserInterfaceLocal::ReadFromSaveGame( idFile *savefile ) {
	if ( savefile == NULL || desktop == NULL ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: gui '%s' has no valid input file/parsed desktop",
			source.c_str() );
		return false;
	}

	int num = 0;
	idStr key;
	idStr value;

	if ( !UI_ReadSaveGameInt( savefile, num, "state count" ) ) {
		return false;
	}
	if ( num < 0 || num > UI_MAX_SAVEGAME_STATE_ENTRIES ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid state count %d", num );
		return false;
	}

	idDict restoredState;
	int64 totalStateBytes = 0;
	for ( int i = 0; i < num; i++ ) {
		if ( !UI_ReadSaveGameString( savefile, key, "state key" ) ) {
			return false;
		}
		if ( !UI_ReadSaveGameString( savefile, value, "state value" ) ) {
			return false;
		}
		totalStateBytes += key.Length();
		totalStateBytes += value.Length();
		if ( key.IsEmpty() || UI_SaveGameStringContainsNul( key ) || UI_SaveGameStringContainsNul( value ) ||
			 totalStateBytes > UI_MAX_SAVEGAME_STATE_BYTES ) {
			common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid state entry %d in gui '%s' (aggregate %lld bytes)",
				i, source.c_str(), static_cast<long long>( totalStateBytes ) );
			return false;
		}
		if ( restoredState.FindKey( key ) != NULL ) {
			common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: duplicate state key '%s' in gui '%s'",
				key.c_str(), source.c_str() );
			return false;
		}
		restoredState.Set( key, value );
	}

	bool restoredActive = false;
	bool restoredInteractive = false;
	bool restoredUniqued = false;
	int restoredTime = 0;
	if ( !UI_ReadSaveGameBool( savefile, restoredActive, "active flag" ) ||
		 !UI_ReadSaveGameBool( savefile, restoredInteractive, "interactive flag" ) ||
		 !UI_ReadSaveGameBool( savefile, restoredUniqued, "unique flag" ) ||
		 !UI_ReadSaveGameInt( savefile, restoredTime, "time" ) ) {
		return false;
	}

	idStr restoredActivateStr;
	idStr restoredPendingCmd;
	idStr restoredReturnCmd;
	if ( !UI_ReadSaveGameString( savefile, restoredActivateStr, "activate command" ) ||
		 !UI_ReadSaveGameString( savefile, restoredPendingCmd, "pending command" ) ||
		 !UI_ReadSaveGameString( savefile, restoredReturnCmd, "return command" ) ) {
		return false;
	}

	float restoredCursorX = 0.0f;
	float restoredCursorY = 0.0f;
	if ( !UI_ReadSaveGameFloat( savefile, restoredCursorX, "cursor x" ) ||
		 !UI_ReadSaveGameFloat( savefile, restoredCursorY, "cursor y" ) ) {
		return false;
	}

	state = restoredState;
	active = restoredActive;
	interactive = restoredInteractive;
	uniqued = restoredUniqued;
	time = restoredTime;
	activateStr = restoredActivateStr;
	pendingCmd = restoredPendingCmd;
	returnCmd = restoredReturnCmd;
	desktop->ReadFromSaveGame( savefile );
	SetCursor( restoredCursorX, restoredCursorY );

	return true;
}

size_t idUserInterfaceLocal::Size() {
	size_t sz = sizeof(*this) + state.Size() + source.Allocated();
	if ( desktop ) {
		sz += desktop->Size();
	}
	return sz;
}

bool idUserInterfaceLocal::IsMenuGui() const {
	return desktop != NULL && ( desktop->GetFlags() & WIN_MENUGUI ) != 0;
}

bool idUserInterfaceLocal::AlwaysThink() const {
	return desktop != NULL && desktop->AlwaysThink();
}

void idUserInterfaceLocal::RunTimeEvents( int _time ) {
	time = _time;
	if ( desktop != NULL ) {
		desktop->RunTimeEvents( _time );
	}
}

int idUserInterfaceLocal::NumTransitions() {
	return desktop != NULL ? desktop->NumTransitions() : 0;
}

void idUserInterfaceLocal::RecurseSetKeyBindingNames( idWindow *window ) {
	int i;
	idWinVar *v = window->GetWinVarByName( "bind" );
	if ( v ) {
		SetStateString( v->GetName(), idKeyInput::KeysFromBindingForMenu( v->GetName() ) );
	}
	i = 0;
	while ( i < window->GetChildCount() ) {
		idWindow *next = window->GetChild( i );
		if ( next ) {
			RecurseSetKeyBindingNames( next );
		}
		i++;
	}
}

/*
==============
idUserInterfaceLocal::SetKeyBindingNames
==============
*/
void idUserInterfaceLocal::SetKeyBindingNames( void ) {
	if ( !desktop ) {
		return;
	}
	// walk the windows
	RecurseSetKeyBindingNames( desktop );
}

/*
==============
idUserInterfaceLocal::SetCursor
==============
*/
void idUserInterfaceLocal::SetCursor( float x, float y ) {
	cursorX = x;
	cursorY = y;
	ClampCursor();
}

/*
==============
idUserInterfaceLocal::ClampCursor
==============
*/
void idUserInterfaceLocal::ClampCursor( void ) {
	if ( !std::isfinite( cursorX ) ) {
		cursorX = 0.0f;
	}
	if ( !std::isfinite( cursorY ) ) {
		cursorY = 0.0f;
	}

	// Retail clamps the cursor to the virtual screen, and the game's in-world
	// GUI interaction depends on it: idPlayer::UpdateFocus parks the cursor at
	// the corner before every absolute reposition. Menu GUIs may extend past
	// the 4:3 canvas, so retain their aspect-corrected bounds.
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = static_cast<float>( VIRTUAL_WIDTH );
	float maxY = static_cast<float>( VIRTUAL_HEIGHT );
	if ( desktop != NULL ) {
		if ( std::isfinite( desktop->forceAspectWidth ) && desktop->forceAspectWidth > 0.0f ) {
			maxX = desktop->forceAspectWidth;
		}
		if ( std::isfinite( desktop->forceAspectHeight ) && desktop->forceAspectHeight > 0.0f ) {
			maxY = desktop->forceAspectHeight;
		}
		if ( ( desktop->GetFlags() & WIN_MENUGUI ) && ui_aspectCorrection.GetBool() ) {
			float xExpand = 0.0f;
			float yExpand = 0.0f;
			uiManagerLocal.dc.GetVirtualScreenExpansion( maxX, maxY, xExpand, yExpand );
			if ( std::isfinite( xExpand ) && xExpand >= 0.0f ) {
				minX -= xExpand;
				maxX += xExpand;
			}
			if ( std::isfinite( yExpand ) && yExpand >= 0.0f ) {
				minY -= yExpand;
				maxY += yExpand;
			}
		}
	}
	cursorX = idMath::ClampFloat( minX, maxX, cursorX );
	cursorY = idMath::ClampFloat( minY, maxY, cursorY );
}

bool idUserInterfaceLocal::GetMaxTextIndex( const char *windowName, const char *text, wrapInfo_t& wrapInfo ) const {
	if ( desktop == NULL ) {
		return false;
	}

	drawWin_t *drawWindow = desktop->FindChildByName( windowName );
	if ( drawWindow == NULL ) {
		return false;
	}

	idDeviceContext *measureDc = NULL;
	int pixelLimit = 0;
	float textScale = 0.0f;

	if ( drawWindow->win != NULL ) {
		drawWindow->win->SetFont();
		measureDc = drawWindow->win->dc;
		pixelLimit = static_cast<int>( drawWindow->win->textRect.w );
		textScale = drawWindow->win->textScale;
	} else if ( drawWindow->simp != NULL ) {
		drawWindow->simp->dc->SetFont( drawWindow->simp->fontNum );
		measureDc = drawWindow->simp->dc;
		pixelLimit = static_cast<int>( drawWindow->simp->textRect.w );
		textScale = drawWindow->simp->textScale;
	}

	if ( measureDc == NULL ) {
		return false;
	}

	return measureDc->GetMaxTextIndex( text, pixelLimit, textScale, wrapInfo );
}
