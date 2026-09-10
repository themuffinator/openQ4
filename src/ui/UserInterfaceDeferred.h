// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "UserInterfaceManaged.h"

// Alloc() predates document paths. Keep its public identity and application
// state stable while selecting an unregistered backend at InitFromFile().
class idUserInterfaceDeferred final : public idUserInterfaceManaged {
public:
	idUserInterfaceDeferred();
	~idUserInterfaceDeferred() override;
	const char *Name() const override;
	const char *Comment() const override;
	bool IsInteractive() const override;
	void SetInteractive( bool value ) override;
	bool IsUniqued() const override;
	void SetUniqued( bool value ) override;
	bool InitFromFile( const char *qpath, bool rebuild = true, bool cache = true ) override;
	const char *HandleEvent( const sysEvent_t *event, int time, bool *updateVisuals = NULL ) override;
	void HandleNamedEvent( const char *eventName ) override;
	void Redraw( int time, bool useAspectCorrection = true ) override;
	void DrawCursor() override;
	const idDict &State() const override;
	void DeleteStateVar( const char *name ) override;
	void SetStateString( const char *name, const char *value ) override;
	void SetStateBool( const char *name, bool value ) override;
	void SetStateInt( const char *name, int value ) override;
	void SetStateFloat( const char *name, float value ) override;
	void SetStateVec4( const char *name, const idVec4 &value ) override;
	const char *GetStateString( const char *name, const char *fallback = "" ) const override;
	bool GetStateBool( const char *name, const char *fallback = "0" ) const override;
	int GetStateInt( const char *name, const char *fallback = "0" ) const override;
	float GetStateFloat( const char *name, const char *fallback = "0" ) const override;
	bool GetPresentationValue( const char *name, idStr &value ) const override;
	bool SetPresentationValue( const char *name, const char *value, bool overrideExpression = true ) override;
	bool GetTextInputState( idRectangle &area, float &cursorOffset ) const override;
	void StateChanged( int time, bool redraw = false ) override;
	const char *Activate( bool active, int time ) override;
	void Trigger( int time ) override;
	bool WriteToSaveGame( idFile *file ) const override;
	bool ReadFromSaveGame( idFile *file ) override;
	void SetKeyBindingNames() override;
	void SetCursor( float x, float y ) override;
	float CursorX() override;
	float CursorY() override;
	idVec4 GetLightColor() override;
	bool GetMaxTextIndex( const char *windowName, const char *text, wrapInfo_t &wrapInfo ) const override;
	const char *GetSourceFile() const override;
	ID_TIME_T GetTimeStamp() const override;
	bool Active() const override;
	bool HasInteractiveOverride() const override;
	bool IsMenuGui() const override;
	bool AlwaysThink() const override;
	void RunTimeEvents( int time ) override;
	size_t Size() override;
	int NumTransitions() override;
	bool DispatchApplicationActions( const char *command, bool &closeRequested ) override;
	const char *PendingApplicationCommand() const override;
	openq4::ui::TextBrokerContext QueryTextContext(std::uint64_t allocation,
		std::uint64_t window, std::uint64_t session) override;
	bool ApplyTextInput(const openq4::ui::TextBrokerContext& expected,
		const openq4::ui::TextInputEvent& input, std::string& error) override;

	bool TakeClipboardRequest(const char* command, uiClipboardRequest_t& out) override;
	bool QueryClipboardEditor(uiNumberEditorSnapshot_t& out, std::string& error) override;
	bool ReplaceClipboardSelection(const uiNumberEditorTarget_t& expected,
		std::string_view text, std::string& error) override;
	bool SetClipboardNotice(const uiNumberEditorTarget_t& expected,
		openq4::ui::NumberEditNotice notice, std::string& error) override;

	bool PrepareNativeText(const openq4::ui::TextEditorIdentity& owner) override;
	bool AttachNativeText(const openq4::ui::TextEditorIdentity& owner, openq4::ui::NativeTextIdentity native, openq4::ui::NativeTextEditorBarrier& out, std::string& error) override;
	bool RefreshNativeText(const openq4::ui::NativeTextEditorBarrier& expected, openq4::ui::NativeTextEditorView& out, std::string& error) override;
	bool CurrentNativeText(const openq4::ui::NativeTextEditorBarrier& expected) const noexcept override;
	bool BeginNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) override;
	bool ApplyNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextOffer& offer, openq4::ui::NativeTextEditorReceipt& out, std::string& error) override;
	bool CompleteNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) override;
	std::unique_ptr<openq4::ui::Interaction::NativeSettlement> PrepareNativeTextSettlement(const openq4::ui::NativeTextEditorBarrier& expected, std::string& error) override;
	bool PublishNativeTextSettlement(openq4::ui::Interaction::NativeSettlement& prepared, openq4::ui::NativeTextEditorReceipt& out) noexcept override;
	bool RetireNativeTextExact(openq4::ui::NativeTextIdentity native, const openq4::ui::TextEditorIdentity& owner) noexcept override;

private:
	idUserInterfaceManaged *backend;
	idDict pendingState;
	bool interactive;
	bool interactiveSet;
	bool uniqued;
	bool active;
	int time;
	float cursorX;
	float cursorY;
};
