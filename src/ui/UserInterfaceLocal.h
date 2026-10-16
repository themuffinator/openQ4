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

#include <thread>
#include "UserInterfaceManaged.h"

class idWindow;
class idWinVec4;
class idChatWindow;
class idRetainedHud;
class idRemasteredHud;

struct LegacyLevelLoadCache;

class idUserInterfaceLocal : public idUserInterfaceManaged {
	friend class idUserInterfaceManagerLocal;
public:
								idUserInterfaceLocal( bool managed = true );
	virtual						~idUserInterfaceLocal();

	virtual const char *		Name() const;
	virtual const char *		Comment() const;
	virtual bool				IsInteractive() const;
	virtual void				SetInteractive(bool interactive);
	virtual bool				InitFromFile( const char *qpath, bool rebuild = true, bool cache = true );
	virtual bool KeepForLevelLoad() override;
	virtual const char *		HandleEvent( const sysEvent_t *event, int time, bool *updateVisuals );
	virtual void				HandleNamedEvent( const char* namedEvent );
	virtual void				Redraw( int time, bool useAspectCorrection = true );
	virtual void				DrawCursor();
	virtual const idDict &		State() const;
	virtual void				DeleteStateVar( const char *varName );
	virtual void				SetStateString( const char *varName, const char *value );
	virtual void				SetStateBool( const char *varName, const bool value );
	virtual void				SetStateInt( const char *varName, const int value );
	virtual void				SetStateFloat( const char *varName, const float value );
	virtual void				SetStateVec4( const char *varName, const idVec4& vector );

	// Gets a gui state variable
	virtual const char*			GetStateString( const char *varName, const char* defaultString = "" ) const;
	virtual bool				GetStateBool( const char *varName, const char* defaultString = "0" ) const;
	virtual int					GetStateInt( const char *varName, const char* defaultString = "0" ) const;
	virtual float				GetStateFloat( const char *varName, const char* defaultString = "0" ) const;
	virtual bool                GetPresentationValue( const char *name, idStr &value ) const override;
	virtual bool                SetPresentationValue( const char *name, const char *value, bool overrideExpression = true ) override;
	virtual bool                GetTextInputState( idRectangle &area, float &cursorOffset ) const override;

	virtual void				StateChanged( int time, bool redraw );
	virtual const char *		Activate( bool activate, int time );
	virtual void				Trigger( int time );
	virtual void				ReadFromDemoFile( class idDemoFile *f );
	virtual void				WriteToDemoFile( class idDemoFile *f );
	virtual bool				WriteToSaveGame( idFile *savefile ) const;
	virtual bool				ReadFromSaveGame( idFile *savefile );
	virtual bool				ReadFromSaveGameWithLayout( idFile *savefile, const char *layoutPath, int &restoredWindowValues );
	virtual void				SetKeyBindingNames( void );
	virtual bool				IsUniqued() const { return uniqued; };
	virtual void				SetUniqued( bool b ) { uniqued = b; };
	virtual void				SetCursor( float x, float y );

	virtual float				CursorX() { return cursorX; }
	virtual float				CursorY() { return cursorY; }
	virtual idVec4				GetLightColor(void) override;
	virtual bool				GetMaxTextIndex( const char *windowName, const char *text, wrapInfo_t& wrapInfo ) const override;

	virtual size_t				Size() override;
	virtual bool				IsMenuGui() const override;
	virtual bool				AlwaysThink() const override;
	virtual void				RunTimeEvents( int time ) override;
	virtual int					NumTransitions() override;

	idDict *					GetStateDict() { return &state; }

	virtual const char *		GetSourceFile( void ) const override { return source; }
	virtual ID_TIME_T			GetTimeStamp( void ) const override { return timeStamp; }

	// Legacy implementation/editor access only; not part of the game interface.
	idWindow *					GetDesktop() const { return desktop; }
	void						SetBindHandler( idWindow *win ) { bindHandler = win; }
	bool						Active() const override { return active; }
	bool						ControllerNavigation() const { return controllerNavigation; }
	void						SetControllerNavigation( bool enabled ) { controllerNavigation = enabled; }
	int							GetTime() const { return time; }
	void						SetTime( int _time ) { time = _time; }

	void						RecurseSetKeyBindingNames( idWindow *window );
	idStr						&GetPendingCmd() { return pendingCmd; };
	idStr						&GetReturnCmd() { return returnCmd; };
	void						SetLightColorVar( idWinVec4 *var ) { lightColorVar = var; }
	// Engine-scripted validation: runs a window's onAction script, or its
	// onBackAction when back is set, as a click on the window would.
	bool						RunWindowAction( const char *windowName, bool back, idStr &command );
	bool						HasWindow( const char *windowName ) const;
	bool						HasNamedEvent( const char *eventName ) const;

private:
	std::unique_ptr<LegacyLevelLoadCache> levelLoadCache;
	bool						active;
	bool						loading;
	bool						interactive;
	bool						uniqued;
	bool						initialized;
	bool						controllerNavigation; // transient menu ownership; never serialized

	idDict						state;
	// Retain pooled strings so equality cannot mistake a recycled value for
	// the previous one. This cache is transient and only used by HUD peers.
	idDict                      evaluatedHudState;
	int                         evaluatedHudTime = 0;
	bool                        evaluatedHudValid = false;
	idWindow *					desktop;
	idChatWindow *				chatWindow; // owned by desktop; transient MP presentation
	idRetainedHud*				retainedHud; // transient presentation, never part of saved GUI framing
	bool                        DrawRetainedHud();
	idRemasteredHud*			remastered; // transient Remastered layout peer, never saved
	bool                        DrawRemasteredHud( int drawTime );
	// The match summary's exit prompt key last published (-1 none yet); -2
	// for every other GUI.
	int							summaryExitKey = -2;
public:
	// The Remastered layout draws this stock HUD in place of its own windows.
	bool                        RemasteredActive() const;
private:
	idWindow *					bindHandler;

	idStr						source;
	idStr						activateStr;
	idStr						pendingCmd;
	idStr						returnCmd;
	ID_TIME_T						timeStamp;

	float						cursorX;
	float						cursorY;
	void						ClampCursor( void );

	int							time;

	idWinVec4 *					lightColorVar;
};

class idUserInterfaceManagerLocal : public idUserInterfaceManager {
	friend class idUserInterfaceLocal;
	friend class idUserInterfaceManaged;

public:
	void CaptureLegacyText(const char* text, const char* family, float scale, float width, float height,
		int align, bool clip, std::vector<openq4::ui::HudPrimitive>& output) {
		dc.CaptureLegacyText(text,family,scale,width,height,align,clip,output);
	}

	virtual void				Init();
	virtual void				Shutdown();
	virtual void				Touch( const char *name );
	virtual void				WritePrecacheCommands( idFile *f );
	virtual void				SetSize( float width, float height );
	void						SetAspectCorrection( bool enabled );
	virtual void				BeginLevelLoad();
	virtual void				EndLevelLoad();
	virtual void				Reload( bool all );
	void						ReloadForLevelLoad();
	virtual void				ListGuis() const;
	virtual bool				CheckGui( const char *qpath ) const;
	virtual idUserInterface *	Alloc( void ) const;
	virtual void				DeAlloc( idUserInterface *gui );
	virtual idUserInterface *	FindGui( const char *qpath, bool autoLoad = false, bool needInteractive = false, bool forceUnique = false );
	virtual idUserInterface *	FindDemoGui( const char *qpath );
	virtual	idListGUI *			AllocListGUI( void ) const;
	virtual void				FreeListGUI( idListGUI *listgui );
	virtual void				RunAlwaysThinkGUIs( int time );
	virtual void				RegisterIcon( const char *code, const char *shader, int x = -1, int y = -1, int w = -1, int h = -1 );
	bool DispatchApplicationActions( idUserInterface *gui, const char *command, bool &closeRequested );
	bool TakeSessionRequest( idUserInterface *gui, const char *command, idStr &request );
	void RunTimeEvents( idUserInterface *gui, int time );
	void PumpApplicationActions( UI_ApplicationCommandCallback callback, void *context, idUserInterface *only );
	openq4::ui::TextBrokerContext QueryTextContext(idUserInterface* current,
		std::uint64_t nativeWindow, std::uint64_t nativeSession);
	uiTextDeliveryResult_t DeliverTextInput(idUserInterface* current,
		std::uint64_t nativeWindow, std::uint64_t nativeSession,
		const openq4::ui::TextBrokerContext& authorizedContext,
		const openq4::ui::TextBrokerDelivery& delivery);

bool NativeTextAttach(uiNativeTextRouteProbe_t,void*,const openq4::ui::TextEditorIdentity&,
	openq4::ui::NativeTextIdentity,openq4::ui::NativeTextEditorBarrier&,std::string&);
bool NativeTextRefresh(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&,
	openq4::ui::NativeTextEditorView&,std::string&);
bool NativeTextCurrent(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&) noexcept;
bool NativeTextBegin(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&,
	const openq4::ui::NativeTextCollection&,openq4::ui::NativeTextEditorBarrier&,std::string&);
bool NativeTextApply(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&,
	const openq4::ui::NativeTextOffer&,openq4::ui::NativeTextEditorReceipt&,std::string&);
bool NativeTextComplete(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&,
	const openq4::ui::NativeTextCollection&,openq4::ui::NativeTextEditorBarrier&,std::string&);
#ifndef ID_DEDICATED
std::unique_ptr<openq4::ui::Interaction::NativeSettlement> NativeTextPrepareSettlement(
	uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&,std::string&);
#endif
bool NativeTextPublishSettlement(uiNativeTextRouteProbe_t,void*,
	openq4::ui::Interaction::NativeSettlement&,openq4::ui::NativeTextEditorReceipt&) noexcept;
bool NativeTextRetireExact(openq4::ui::NativeTextIdentity,const openq4::ui::TextEditorIdentity&) noexcept;
openq4::ui::NativeTextPresence NativeTextPresence(openq4::ui::NativeTextIdentity,const openq4::ui::TextEditorIdentity&) const noexcept;

private:
	bool NativeTextEnter() noexcept;
public:
	bool QueryNativeInputAllocation(std::uintptr_t,std::uint64_t&) const noexcept;
private:
	idUserInterfaceManaged* NativeTextResolve(uiNativeTextRouteProbe_t,void*,const openq4::ui::TextEditorIdentity&) const noexcept;
	bool NativeTextCheck(uiNativeTextRouteProbe_t,void*,const openq4::ui::NativeTextEditorBarrier&) const noexcept;
	void						ReloadGuis( bool all, bool levelLoad );
	void						RegisterAllocation( idUserInterfaceManaged *gui );
	void						RegisterGui( idUserInterfaceManaged *gui );
	void						RegisterDemoGui( idUserInterfaceManaged *gui );
	void						UnregisterGui( idUserInterfaceManaged *gui );
	void						UpdateAlwaysThinkGui( idUserInterfaceManaged *gui );
	void						RemoveAlwaysThinkGui( idUserInterfaceManaged *gui );

	idRectangle					screenRect;
	idDeviceContext				dc;

	// Only allocations owns objects. Every other registry is a non-owning
	// subset, removed by the managed destructor even on direct editor deletes.
	idList<idUserInterfaceManaged*> allocations;
	unsigned long long nextAllocationId = 0;
	bool textBoundaryActive = false, textBoundaryFailed = false;
	const std::thread::id nativePresenceThread = std::this_thread::get_id();
	bool nativeBoundaryActive = false, nativeBoundaryFailed = false;
	bool clipboardBoundaryActive = false, clipboardBoundaryFailed = false;
	int applicationPumpDepth = 0;
	int applicationPumpBudget = 0;
	idList<idUserInterfaceManaged*> guis;
	idList<idUserInterfaceManaged*> alwaysThinkGUIs;
	idList<idUserInterfaceManaged*> demoGuis;

public:
	// HUD presentation (RetainedHud.cpp): the coverage bridge's generated
	// HUDs and the ui_hudTest probe.
	void HudTest(const idCmdArgs& args);
	bool DrawHudPrimitives(const char* source, const idList<uiHudPrimitive_t>& items) override;
	void ClearHudPrimitives();
	idList<idRetainedHud*> generatedHuds;
	idStrList generatedHudNames;
};
