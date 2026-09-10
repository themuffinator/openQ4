// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "../framework/NativeInputPublications.h"
#include "UserInterfaceDeferred.h"

#include <cmath>
#include <memory>

idUserInterfaceDeferred::idUserInterfaceDeferred() : backend( NULL ),
	interactive( false ), interactiveSet( false ), uniqued( false ), active( false ),
	time( 0 ), cursorX( 0 ), cursorY( 0 ) {}

idUserInterfaceDeferred::~idUserInterfaceDeferred() { MarkNativeInputClosing(); delete backend; }

const char *idUserInterfaceDeferred::Name() const { return backend != NULL ? backend->Name() : ""; }
const char *idUserInterfaceDeferred::Comment() const { return backend != NULL ? backend->Comment() : ""; }
const char *idUserInterfaceDeferred::GetSourceFile() const { return backend != NULL ? backend->GetSourceFile() : ""; }
ID_TIME_T idUserInterfaceDeferred::GetTimeStamp() const { return backend != NULL ? backend->GetTimeStamp() : 0; }
bool idUserInterfaceDeferred::Active() const { return backend != NULL ? backend->Active() : active; }
bool idUserInterfaceDeferred::HasInteractiveOverride() const { return backend != NULL ? backend->HasInteractiveOverride() : interactiveSet; }
bool idUserInterfaceDeferred::IsMenuGui() const { return backend != NULL && backend->IsMenuGui(); }
bool idUserInterfaceDeferred::AlwaysThink() const { return backend != NULL && backend->AlwaysThink(); }
size_t idUserInterfaceDeferred::Size() { return sizeof( *this ) + pendingState.Allocated() + ( backend != NULL ? backend->Size() : 0 ); }
int idUserInterfaceDeferred::NumTransitions() { return backend != NULL ? backend->NumTransitions() : 0; }
bool idUserInterfaceDeferred::IsInteractive() const { return backend != NULL ? backend->IsInteractive() : interactive; }
bool idUserInterfaceDeferred::IsUniqued() const { return backend != NULL ? backend->IsUniqued() : uniqued; }

void idUserInterfaceDeferred::SetInteractive( bool value ) {
	interactive = value;
	interactiveSet = true;
	if ( backend != NULL ) { backend->SetInteractive( value ); }
}

void idUserInterfaceDeferred::SetUniqued( bool value ) {
	uniqued = value;
	if ( backend != NULL ) { backend->SetUniqued( value ); }
}

static void UI_CopyDeferredState( idUserInterfaceManaged &target, const idDict &state ) {
	for ( int i = 0; i < state.GetNumKeyVals(); ++i ) {
		const idKeyValue *entry = state.GetKeyVal( i );
		// The parsed source owns its name, including during cross-file loads.
		if ( idStr::Icmp( entry->GetKey(), "name" ) != 0 ) {
			target.SetStateString( entry->GetKey(), entry->GetValue() );
		}
	}
}

bool idUserInterfaceDeferred::InitFromFile( const char *qpath, bool rebuild, bool cache ) {
	if ( qpath == NULL || qpath[0] == '\0' ) { return false; }
	// qpath may point into the live backend's source string.
	const idStr path( qpath );
	if ( backend != NULL && idStr::Icmp( backend->GetSourceFile(), path ) == 0 ) {
		// Each backend owns its same-document reload semantics. In particular,
		// a legacy rebuild=false must keep the existing desktop for editor loads.
		const bool loaded = backend->InitFromFile( path, rebuild, cache );
		if ( loaded ) { RegisterLoaded(); }
		RefreshThinking();
		return loaded;
	}

	const idDict state( State() );
	const float x = CursorX(), y = CursorY();
	const bool unique = IsUniqued();
	// Once loaded, the backend is authoritative: save restoration can change
	// these flags without going through the wrapper's pre-init setters.
	const bool activate = Active();
	const bool overrideInteractive = HasInteractiveOverride();
	const bool interactiveValue = IsInteractive();
	std::unique_ptr<idUserInterfaceManaged> candidate( UI_CreateForPath( path, false ) );
	if ( !candidate ) { return false; }
	UI_CopyDeferredState( *candidate, state );
	candidate->SetUniqued( unique );
	if ( !candidate->InitFromFile( path, rebuild, cache ) ) { return false; }
	// Some legacy defaults are applied while parsing; restore caller state
	// afterwards and resolve bindings once the new document exists.
	UI_CopyDeferredState( *candidate, state );
	candidate->StateChanged( time, false );
	candidate->SetCursor( x, y );
	candidate->SetUniqued( unique );
	if ( overrideInteractive ) { candidate->SetInteractive( interactiveValue ); }
	if ( activate ) { candidate->Activate( true, time ); }
	SetNativeInputChanging(true);
	struct NativeChangeGuard {
		idUserInterfaceDeferred& owner;
		~NativeChangeGuard() { owner.SetNativeInputChanging(false); }
	} nativeChangeGuard{*this};
	delete backend;
	backend = candidate.release();
	pendingState.Clear();
	RegisterLoaded();
	return true;
}

const idDict &idUserInterfaceDeferred::State() const { return backend != NULL ? backend->State() : pendingState; }
void idUserInterfaceDeferred::DeleteStateVar( const char *name ) {
	if ( backend != NULL ) { backend->DeleteStateVar( name ); } else { pendingState.Delete( name ); }
}
void idUserInterfaceDeferred::SetStateString( const char *name, const char *value ) {
	if ( backend != NULL ) { backend->SetStateString( name, value ); } else { pendingState.Set( name, value ); }
}
void idUserInterfaceDeferred::SetStateBool( const char *name, bool value ) {
	if ( backend != NULL ) { backend->SetStateBool( name, value ); } else { pendingState.SetBool( name, value ); }
}
void idUserInterfaceDeferred::SetStateInt( const char *name, int value ) {
	if ( backend != NULL ) { backend->SetStateInt( name, value ); } else { pendingState.SetInt( name, value ); }
}
void idUserInterfaceDeferred::SetStateFloat( const char *name, float value ) {
	if ( backend != NULL ) { backend->SetStateFloat( name, value ); } else { pendingState.SetFloat( name, value ); }
}
void idUserInterfaceDeferred::SetStateVec4( const char *name, const idVec4 &value ) {
	if ( backend != NULL ) { backend->SetStateVec4( name, value ); } else { pendingState.SetVec4( name, value ); }
}
const char *idUserInterfaceDeferred::GetStateString( const char *name, const char *fallback ) const { return State().GetString( name, fallback ); }
bool idUserInterfaceDeferred::GetStateBool( const char *name, const char *fallback ) const { return State().GetBool( name, fallback ); }
int idUserInterfaceDeferred::GetStateInt( const char *name, const char *fallback ) const { return State().GetInt( name, fallback ); }
float idUserInterfaceDeferred::GetStateFloat( const char *name, const char *fallback ) const { return State().GetFloat( name, fallback ); }
bool idUserInterfaceDeferred::GetPresentationValue( const char *name, idStr &value ) const {
	return backend != NULL && backend->GetPresentationValue( name, value );
}
bool idUserInterfaceDeferred::SetPresentationValue( const char *name, const char *value, bool overrideExpression ) {
	if ( backend == NULL ) { return false; }
	const bool result = backend->SetPresentationValue( name, value, overrideExpression );
	RefreshThinking();
	return result;
}
bool idUserInterfaceDeferred::GetTextInputState( idRectangle &area, float &cursorOffset ) const {
	return backend != NULL && backend->GetTextInputState( area, cursorOffset );
}
void idUserInterfaceDeferred::StateChanged( int newTime, bool redraw ) {
	time = newTime;
	if ( backend != NULL ) { backend->StateChanged( time, redraw ); }
	RefreshThinking();
}
const char *idUserInterfaceDeferred::Activate( bool value, int newTime ) {
	active = value; time = newTime;
	const char *result = backend != NULL ? backend->Activate( active, time ) : "";
	RefreshThinking();
	return result;
}
void idUserInterfaceDeferred::Trigger( int newTime ) {
	time = newTime;
	if ( backend != NULL ) { backend->Trigger( time ); }
	RefreshThinking();
}
const char *idUserInterfaceDeferred::HandleEvent( const sysEvent_t *event, int newTime, bool *updateVisuals ) {
	time = newTime;
	if ( backend == NULL ) { if ( updateVisuals != NULL ) { *updateVisuals = false; } return ""; }
	const char *result = backend->HandleEvent( event, time, updateVisuals );
	RefreshThinking();
	return result;
}
void idUserInterfaceDeferred::HandleNamedEvent( const char *eventName ) {
	if ( backend != NULL ) { backend->HandleNamedEvent( eventName ); }
	RefreshThinking();
}
void idUserInterfaceDeferred::Redraw( int newTime, bool useAspectCorrection ) {
	time = newTime;
	if ( backend != NULL ) { backend->Redraw( time, useAspectCorrection ); }
	RefreshThinking();
}
void idUserInterfaceDeferred::DrawCursor() { if ( backend != NULL ) { backend->DrawCursor(); } }
void idUserInterfaceDeferred::RunTimeEvents( int newTime ) {
	time = newTime;
	if ( backend != NULL ) { backend->RunTimeEvents( time ); }
}
bool idUserInterfaceDeferred::DispatchApplicationActions( const char *command, bool &closeRequested ) {
	return backend != NULL && backend->DispatchApplicationActions( command, closeRequested );
}
const char *idUserInterfaceDeferred::PendingApplicationCommand() const {
	return backend != NULL ? backend->PendingApplicationCommand() : "";
}
openq4::ui::TextBrokerContext idUserInterfaceDeferred::QueryTextContext(std::uint64_t allocation,
	std::uint64_t window, std::uint64_t session) {
	return backend != NULL ? backend->QueryTextContext(allocation,window,session) : openq4::ui::TextBrokerContext{};
}
bool idUserInterfaceDeferred::ApplyTextInput(const openq4::ui::TextBrokerContext& expected,
	const openq4::ui::TextInputEvent& input, std::string& error) {
	return backend != NULL && backend->ApplyTextInput(expected,input,error);
}
bool idUserInterfaceDeferred::WriteToSaveGame( idFile *file ) const { return backend != NULL && backend->WriteToSaveGame( file ); }
bool idUserInterfaceDeferred::ReadFromSaveGame( idFile *file ) {
	if ( backend == NULL ) { return false; }
	const bool result = backend->ReadFromSaveGame( file );
	RefreshThinking();
	return result;
}
void idUserInterfaceDeferred::SetKeyBindingNames() { if ( backend != NULL ) { backend->SetKeyBindingNames(); } }
void idUserInterfaceDeferred::SetCursor( float x, float y ) {
	if ( backend != NULL ) { backend->SetCursor( x, y ); return; }
	// Match the unloaded legacy cursor's 640x480 coordinate contract.
	cursorX = std::isfinite( x ) ? idMath::ClampFloat( 0.0f, 640.0f, x ) : 0.0f;
	cursorY = std::isfinite( y ) ? idMath::ClampFloat( 0.0f, 480.0f, y ) : 0.0f;
}
float idUserInterfaceDeferred::CursorX() { return backend != NULL ? backend->CursorX() : cursorX; }
float idUserInterfaceDeferred::CursorY() { return backend != NULL ? backend->CursorY() : cursorY; }
idVec4 idUserInterfaceDeferred::GetLightColor() { return backend != NULL ? backend->GetLightColor() : vec4_origin; }
bool idUserInterfaceDeferred::GetMaxTextIndex( const char *windowName, const char *text, wrapInfo_t &wrapInfo ) const {
	return backend != NULL && backend->GetMaxTextIndex( windowName, text, wrapInfo );
}

bool idUserInterfaceDeferred::TakeClipboardRequest(const char* command, uiClipboardRequest_t& out) {
	return backend != NULL && backend->TakeClipboardRequest(command,out);
}
bool idUserInterfaceDeferred::QueryClipboardEditor(uiNumberEditorSnapshot_t& out, std::string& error) {
	return backend != NULL && backend->QueryClipboardEditor(out,error);
}
bool idUserInterfaceDeferred::ReplaceClipboardSelection(const uiNumberEditorTarget_t& expected,
	std::string_view text, std::string& error) {
	return backend != NULL && backend->ReplaceClipboardSelection(expected,text,error);
}
bool idUserInterfaceDeferred::SetClipboardNotice(const uiNumberEditorTarget_t& expected,
	openq4::ui::NumberEditNotice notice, std::string& error) {
	return backend != NULL && backend->SetClipboardNotice(expected,notice,error);
}

// No deferred loading: each call uses only the currently installed backend.
bool idUserInterfaceDeferred::PrepareNativeText(const openq4::ui::TextEditorIdentity& owner) {
	return backend != NULL ? backend->PrepareNativeText(owner) : false;
}
bool idUserInterfaceDeferred::AttachNativeText(const openq4::ui::TextEditorIdentity& owner, openq4::ui::NativeTextIdentity native, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
	return backend != NULL ? backend->AttachNativeText(owner,native,out,error) : false;
}
bool idUserInterfaceDeferred::RefreshNativeText(const openq4::ui::NativeTextEditorBarrier& expected, openq4::ui::NativeTextEditorView& out, std::string& error) {
	return backend != NULL ? backend->RefreshNativeText(expected,out,error) : false;
}
bool idUserInterfaceDeferred::CurrentNativeText(const openq4::ui::NativeTextEditorBarrier& expected) const noexcept {
	return backend != NULL ? backend->CurrentNativeText(expected) : false;
}
bool idUserInterfaceDeferred::BeginNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
	return backend != NULL ? backend->BeginNativeText(expected,collection,out,error) : false;
}
bool idUserInterfaceDeferred::ApplyNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextOffer& offer, openq4::ui::NativeTextEditorReceipt& out, std::string& error) {
	return backend != NULL ? backend->ApplyNativeText(expected,offer,out,error) : false;
}
bool idUserInterfaceDeferred::CompleteNativeText(const openq4::ui::NativeTextEditorBarrier& expected, const openq4::ui::NativeTextCollection& collection, openq4::ui::NativeTextEditorBarrier& out, std::string& error) {
	return backend != NULL ? backend->CompleteNativeText(expected,collection,out,error) : false;
}
std::unique_ptr<openq4::ui::Interaction::NativeSettlement> idUserInterfaceDeferred::PrepareNativeTextSettlement(const openq4::ui::NativeTextEditorBarrier& expected, std::string& error) {
#ifdef ID_DEDICATED
	// Avoid even a temporary owning client-model value in debug STL builds.
	(void)expected; (void)error; return nullptr;
#else
	return backend != NULL ? backend->PrepareNativeTextSettlement(expected,error) : nullptr;
#endif
}
bool idUserInterfaceDeferred::PublishNativeTextSettlement(openq4::ui::Interaction::NativeSettlement& prepared, openq4::ui::NativeTextEditorReceipt& out) noexcept {
	return backend != NULL ? backend->PublishNativeTextSettlement(prepared,out) : false;
}
openq4::ui::NativeTextPresence idUserInterfaceDeferred::QueryNativeTextPresence(openq4::ui::NativeTextIdentity native,
    const openq4::ui::TextEditorIdentity& owner) const noexcept {
    return backend!=NULL?backend->QueryNativeTextPresence(native,owner):openq4::ui::NativeTextPresence::AbsentOriginal;
}
bool idUserInterfaceDeferred::RetireNativeTextExact(openq4::ui::NativeTextIdentity native, const openq4::ui::TextEditorIdentity& owner) noexcept {
	return backend != NULL ? backend->RetireNativeTextExact(native,owner) : false;
}
