// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "UserInterface.h"
#include "UserInterfaceText.h"
#include "UserInterfaceClipboard.h"
#include "UserInterfaceNativeText.h"

// Engine-private ownership and scheduling contract. Game modules continue to
// use idUserInterface; neither legacy windows nor these manager operations
// cross that interface. Managed instances are dynamically allocated and become
// manager-owned immediately, including before a source has been loaded.
class idUserInterfaceManaged : public idUserInterface {
	friend class idUserInterfaceManagerLocal;
public:
	explicit idUserInterfaceManaged( bool managed = true );
	virtual ~idUserInterfaceManaged();

	virtual const char *GetSourceFile() const = 0;
	virtual ID_TIME_T GetTimeStamp() const = 0;
	virtual bool Active() const = 0;
	// Legacy interactivity is re-derived by InitFromFile/StateChanged, and its
	// save payload has no persistent override provenance. Retained overrides
	// report that policy separately from their current effective value.
	virtual bool HasInteractiveOverride() const { return false; }
	virtual bool IsMenuGui() const = 0;
	virtual bool AlwaysThink() const = 0;
	virtual void RunTimeEvents( int time ) = 0;
	virtual size_t Size() = 0;
	virtual int NumTransitions() = 0;
	// False leaves legacy command dispatch in charge. Retained events carry
	// typed invocations inside the engine instead of console command strings.
	virtual bool DispatchApplicationActions( const char *command, bool &closeRequested ) { return false; }
	virtual const char *PendingApplicationCommand() const { return ""; }
	// Front accepted session.menu verb of this exact dispatch marker, FIFO.
	// Verbs name session operations; they are never console command text.
	virtual bool TakeSessionRequest( const char *command, idStr &out ) { return false; }
	// A map change re-initializes every GUI so that its media resolve again
	// inside the level load, whose purge defaulted them (UI_ReloadForLevelLoad).
	// True when this GUI has done what the load needs without re-initializing,
	// keeping what it has loaded; false re-initializes it from its source.
	virtual bool KeepForLevelLoad() { return false; }
	// Only the manager calls these with an already checked outer allocation.
	// Deferred wrappers forward that identity to their unregistered backend.
	virtual openq4::ui::TextBrokerContext QueryTextContext(std::uint64_t allocation,
		std::uint64_t window, std::uint64_t session) {
		return {Active() ? openq4::ui::TextBrokerRoute::Legacy : openq4::ui::TextBrokerRoute::Unavailable,
			window,session,{}};
	}
	virtual bool ApplyTextInput(const openq4::ui::TextBrokerContext& expected,
		const openq4::ui::TextInputEvent& input, std::string& error) { return false; }

	// Clipboard work leaves every backend method stack before the manager calls
	// the native service. Take only the front FIFO request for this exact marker.
	virtual bool TakeClipboardRequest(const char* command, uiClipboardRequest_t& out) { return false; }
	virtual bool QueryClipboardEditor(uiNumberEditorSnapshot_t& out, std::string& error) { return false; }
	virtual bool ReplaceClipboardSelection(const uiNumberEditorTarget_t& expected,
		std::string_view text, std::string& error) { return false; }
	virtual bool SetClipboardNotice(const uiNumberEditorTarget_t& expected,
		openq4::ui::NumberEditNotice notice, std::string& error) { return false; }

	// Engine-private native owner endpoints. Legacy and unloaded backends refuse.
	// Prepare and Refresh may call host code; manager re-resolves afterward.
	// Current, Publish and Retire must not allocate or enter foreign callbacks.
	virtual bool PrepareNativeText(const openq4::ui::TextEditorIdentity& owner) { return false; }
	virtual bool AttachNativeText(const openq4::ui::TextEditorIdentity& owner, openq4::ui::NativeTextIdentity native, openq4::ui::NativeTextEditorBarrier& out, std::string& error) { return false; }
	virtual bool RefreshNativeText(const openq4::ui::NativeTextEditorBarrier& expected, openq4::ui::NativeTextEditorView& out, std::string& error) { return false; }
	virtual bool CurrentNativeText(const openq4::ui::NativeTextEditorBarrier& expected) const noexcept { return false; }
	virtual bool BeginNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) { return false; }
	virtual bool ApplyNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextOffer& offer, openq4::ui::NativeTextEditorReceipt& out, std::string& error) { return false; }
	virtual bool CompleteNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) { return false; }
#ifndef ID_DEDICATED
	// Client-only: a settlement owns retained models whose destructor the
	// dedicated server does not link. Optimized MSVC emits that destructor for
	// any by-value return, even unused, so the dedicated build omits Prepare.
	virtual std::unique_ptr<openq4::ui::Interaction::NativeSettlement> PrepareNativeTextSettlement(const openq4::ui::NativeTextEditorBarrier& expected, std::string& error) { return nullptr; }
#endif
	virtual bool PublishNativeTextSettlement(openq4::ui::Interaction::NativeSettlement& prepared, openq4::ui::NativeTextEditorReceipt& out) noexcept { return false; }
	virtual openq4::ui::NativeTextPresence QueryNativeTextPresence(openq4::ui::NativeTextIdentity,
        const openq4::ui::TextEditorIdentity&) const noexcept { return openq4::ui::NativeTextPresence::BusyOrUnknown; }
	virtual bool RetireNativeTextExact(openq4::ui::NativeTextIdentity native, const openq4::ui::TextEditorIdentity& owner) noexcept { return false; }

	void ClearRefs() { refs = 0; }
	void AddRef() { refs++; }
	int GetRefs() const { return refs; }

protected:
	void MarkNativeInputClosing() noexcept;
	void SetNativeInputChanging(bool changing) noexcept;
	// Publish only after initialization has established valid metadata. Loaded
	// and demo registries are non-owning subsets of the allocation registry.
	void RegisterLoaded();
	void RegisterDemo();
	void RefreshThinking();

private:
	idUserInterfaceManaged( const idUserInterfaceManaged & ) = delete;
	idUserInterfaceManaged &operator=( const idUserInterfaceManaged & ) = delete;
	int refs;
	unsigned long long allocationId;
	bool managed;
	bool nativeInputClosing = false, nativeInputChanging = false;
};

// Explicit retained documents use the retained backend; stock GUI/editor
// sources stay legacy. Unmanaged results belong to a deferred wrapper only.
idUserInterfaceManaged *UI_CreateForPath( const char *qpath, bool managed = true );
bool UI_IsRetainedPath( const char *qpath );
bool UI_DispatchApplicationActions( idUserInterface *gui, const char *command, bool &closeRequested );
// Take the next session verb a retained GUI accepted during the dispatch of
// `command`. The manager checks the allocation is live before delegating.
bool UI_TakeSessionRequest( idUserInterface *gui, const char *command, idStr &request );
// Validates a VFS image name for a retained image node (see ValidImageSource).
bool UI_RetainedImageSource( const char *source );
// Resolves a bound picture ahead of its first draw; call it at startup or
// inside a level load (see RetainedUI_PrecacheImage).
void UI_RetainedPrecacheImage( const char *source );
// Rereads a picture whose file a newer save may have replaced.
void UI_RetainedReloadImage( const char *source );
// Parses retained documents on the job system's workers ahead of their loads:
// a load of one whose source is unchanged takes its parse instead of parsing.
// Clear drops the parses no load took, after their workers finish.
void UI_RetainedPrefetchDocuments( const char *const *paths, int count );
void UI_RetainedPrefetchClear();
// The gameplay documents a level's game loads as it spawns (the Remastered
// HUDs, weapon wheels, quick chat and view effects), parsed on workers as the
// map change begins so those loads build only their views. Settle drops the
// parses a single-player spawn did not take: the other family's HUD and wheel.
void UI_RetainedPrefetchGameplayDocuments( bool multiplayer );
void UI_RetainedSettleGameplayDocuments( bool multiplayer );
// Whether a prefetch parsed this document for the level under way and no load
// has taken it yet: the documents a stock HUD's load may load with itself. A
// HUD the level only precaches, and never draws, keeps its load for its first draw.
bool UI_RetainedPrefetched( const char *path );
// Advance a live GUI's timelines and time events without drawing it.
void UI_RunTimeEvents( idUserInterface *gui, int time );
// A map change's reload, inside the level load (ExecuteMapChange): every GUI
// re-initializes, as uiManager->Reload( true ) does, except one that keeps
// what it has loaded (KeepForLevelLoad): a retained document whose source is
// unchanged keeps its view and resolves its pictures again.
void UI_ReloadForLevelLoad();
// Engine-scripted validation for classic GUIs: runs a window's onAction script,
// or its onBackAction when back is set, as a click on the window would, and
// returns the command it issued. No device input is read or synthesized.
bool UI_RunLegacyWindowAction( idUserInterface *gui, const char *windowName, bool back, idStr &command );
bool UI_StepLegacyFocus( idUserInterface *gui, int direction, idStr &report );
bool UI_LegacyFocusWithin( idUserInterface *gui, const char *ancestorName, idStr &focusName, idRectangle &rect, float &viewHeight );
// Whether a classic GUI defines the named window, visible or not.
bool UI_LegacyWindowExists( idUserInterface *gui, const char *windowName );
// Whether a classic GUI's desktop defines the named event.
bool UI_LegacyNamedEventExists( idUserInterface *gui, const char *eventName );
typedef void (*UI_ApplicationCommandCallback)( idUserInterface *gui, const char *command, void *context );
// Pump only private typed requests. A null owner snapshots all pending managed
// allocations; a specific owner drains a lifecycle queue before its release.
void UI_PumpApplicationActions( UI_ApplicationCommandCallback callback, void *context, idUserInterface *only = NULL );
