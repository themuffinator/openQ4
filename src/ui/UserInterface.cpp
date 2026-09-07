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




#include "ListGUILocal.h"
#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "ChatWindow.h"
#include "SimpleWindow.h"
#include "../framework/Session.h"

extern idCVar r_skipGuiShaders;		// 1 = don't render any gui elements on surfaces
extern idCVar gui_debugScript;
idCVar ui_aspectCorrection( "ui_aspectCorrection", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_BOOL,
	"preserve classic 4:3 layout for 2D UI (menu, HUD, console, loading/init); 0 = stretch to full 2D viewport" );

idUserInterfaceManagerLocal	uiManagerLocal;
idUserInterfaceManager *	uiManager = &uiManagerLocal;

namespace {

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

/*
===============================================================================

	idUserInterfaceManagerLocal

===============================================================================
*/

void idUserInterfaceManagerLocal::Init() {
	cmdSystem->AddCommand("chatHistory", idChatWindow::History_f, CMD_FL_SYSTEM, "browse open chat: up, down, top, bottom, status");
	screenRect = idRectangle(0, 0, 640, 480);
	dc.Init();
}

void idUserInterfaceManagerLocal::Shutdown() {
	cmdSystem->RemoveCommand("chatHistory");
	idChatWindow::Reset();
	guis.DeleteContents( true );
	alwaysThinkGUIs.Clear();
	demoGuis.DeleteContents( true );
	dc.Shutdown();
}

void idUserInterfaceManagerLocal::Touch( const char *name ) {
	idUserInterface *gui = Alloc();
	gui->InitFromFile( name );
//	delete gui;
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
		if ( (guis[ i ]->GetDesktop()->GetFlags() & WIN_MENUGUI) == 0 ) {
			guis[ i ]->ClearRefs();
			/*
			delete guis[ i ];
			guis.RemoveIndex( i );
			i--; c--;
			*/
		}
	}
}

void idUserInterfaceManagerLocal::EndLevelLoad() {
	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
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
				RemoveAlwaysThinkGui( guis[i] );
				delete guis[ i ];
				guis.RemoveIndex( i );
				i--; c--;
			}
		}
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

	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( !all ) {
			fileSystem->ReadFile( guis[i]->GetSourceFile(), NULL, &ts );
			if ( ts <= guis[i]->GetTimeStamp() ) {
				continue;
			}
		}

		guis[i]->InitFromFile( guis[i]->GetSourceFile() );
		common->Printf( "reloading %s.\n", guis[i]->GetSourceFile() );
	}
}

void idUserInterfaceManagerLocal::ListGuis() const {
	int c = guis.Num();
	common->Printf( "\n   size   refs   name\n" );
	size_t total = 0;
	int copies = 0;
	int unique = 0;
	for ( int i = 0; i < c; i++ ) {
		idUserInterfaceLocal *gui = guis[i];
		size_t sz = gui->Size();
		bool isUnique = guis[i]->interactive;
		if ( isUnique ) {
			unique++;
		} else {
			copies++;
		}
		common->Printf( "%6.1fk %4i (%s) %s ( %i transitions )\n", sz / 1024.0f, guis[i]->GetRefs(), isUnique ? "unique" : "copy", guis[i]->GetSourceFile(), guis[i]->desktop->NumTransitions() );
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
	return new idUserInterfaceLocal();
}

void idUserInterfaceManagerLocal::DeAlloc( idUserInterface *gui ) {
	if ( gui ) {
		int c = guis.Num();
		for ( int i = 0; i < c; i++ ) {
			if ( guis[i] == gui ) {
				RemoveAlwaysThinkGui( guis[i] );
				delete guis[i];
				guis.RemoveIndex( i );
				return;
			}
		}
	}
}

idUserInterface *idUserInterfaceManagerLocal::FindGui( const char *qpath, bool autoLoad, bool needUnique, bool forceNOTUnique ) {
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
		idUserInterface *gui = Alloc();
		if ( gui->InitFromFile( qpath ) ) {
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

void idUserInterfaceManagerLocal::UpdateAlwaysThinkGui( idUserInterfaceLocal *gui ) {
	if ( gui == NULL || gui->desktop == NULL || !gui->desktop->AlwaysThink() ) {
		RemoveAlwaysThinkGui( gui );
		return;
	}

	alwaysThinkGUIs.AddUnique( gui );
}

void idUserInterfaceManagerLocal::RemoveAlwaysThinkGui( idUserInterfaceLocal *gui ) {
	if ( gui == NULL ) {
		return;
	}

	alwaysThinkGUIs.Remove( gui );
}

void idUserInterfaceManagerLocal::RunAlwaysThinkGUIs( int time ) {
	for ( int i = 0; i < alwaysThinkGUIs.Num(); i++ ) {
		idUserInterfaceLocal *gui = alwaysThinkGUIs[i];
		if ( gui == NULL || guis.Find( gui ) == NULL || gui->desktop == NULL || !gui->desktop->AlwaysThink() ) {
			alwaysThinkGUIs.RemoveIndex( i );
			i--;
			continue;
		}

		gui->time = time;
		gui->desktop->RunTimeEvents( time );
	}
}

/*
===============================================================================

	idUserInterfaceLocal

===============================================================================
*/

idUserInterfaceLocal::idUserInterfaceLocal() {
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
	refs = 1;
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

	if ( uiManagerLocal.guis.Find( this ) == NULL ) {
		uiManagerLocal.guis.Append( this );
	}
	uiManagerLocal.UpdateAlwaysThinkGui( this );

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

	bool add = true;
	int c = uiManagerLocal.demoGuis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( uiManagerLocal.demoGuis[i] == this ) {
			add = false;
			break;
		}
	}

	if (add) {
		uiManagerLocal.demoGuis.Append(this);
	}
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
