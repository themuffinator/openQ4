// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "UserInterface.h"
#include "UserInterfaceText.h"
#include "UserInterfaceClipboard.h"

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

	void ClearRefs() { refs = 0; }
	void AddRef() { refs++; }
	int GetRefs() const { return refs; }

protected:
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
};

// Explicit retained documents use the retained backend; stock GUI/editor
// sources stay legacy. Unmanaged results belong to a deferred wrapper only.
idUserInterfaceManaged *UI_CreateForPath( const char *qpath, bool managed = true );
bool UI_IsRetainedPath( const char *qpath );
bool UI_DispatchApplicationActions( idUserInterface *gui, const char *command, bool &closeRequested );
typedef void (*UI_ApplicationCommandCallback)( idUserInterface *gui, const char *command, void *context );
// Pump only private typed requests. A null owner snapshots all pending managed
// allocations; a specific owner drains a lifecycle queue before its release.
void UI_PumpApplicationActions( UI_ApplicationCommandCallback callback, void *context, idUserInterface *only = NULL );
