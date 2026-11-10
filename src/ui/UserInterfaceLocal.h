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
class idDeviceContext;

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
	// Steps the focus as a menu's Down (1) or Up (-1) arrow does, or leaves
	// it (0), and reports the focused window: its name, its screen rectangle
	// and whether it and every parent are shown.
	bool						StepFocus( int direction, idStr &report );
	// The focused window when it stands inside the window `ancestorName`:
	// its name, its rectangle in that window's coordinates, and the height
	// of the view that window scrolls in (its parent).
	bool						FocusWithin( const char *ancestorName, idStr &focusName, idRectangle &rect, float &viewHeight );
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
	idRemasteredHud*            essentialHud = nullptr; // transient Classic air overlay; never saved
	void                        DrawEssentialHud( int drawTime );
	bool                        DrawRemasteredHud( int drawTime );
	// The match summary's exit prompt key last published (-1 none yet); -2
	// for every other GUI.
	int							summaryExitKey = -2;
public:
	// The Remastered layout draws this stock HUD in place of its own windows.
	bool                        RemasteredActive() const;
	// The stock interface's Remastered layout of this HUD (idLegacyHudLayout):
	// its edge anchors this frame (left, top, right, bottom), when it moves them.
	bool                        LegacyEdges( float edges[4] ) const;
	// The retained interface's Classic draws this HUD on the centered 4:3
	// canvas, without edge expansion (as of its last Redraw).
	bool                        ClassicCanvas() const { return classicCanvas; }
	// This HUD's stock windows follow the Remastered layout this frame.
	bool                        LegacyRemastered() const { return legacyEdges; }
	// The stock interface's HUD scale for this HUD this frame (1: none).
	float                       LegacyScale() const { return legacyScale; }
	// The same for the centre column on the bottom edge, which narrow views limit.
	float                       LegacyCenterScale() const { return legacyCenterScale; }
private:
	friend class idLegacyHudGroup;
	// What this GUI is to the HUD presentations, resolved once per source: its
	// Remastered document and Classic air overlay, whether the coverage bridge
	// draws it, whether a mod owns the file, and whether the stock interface's
	// HUD opacity and scale apply to it.
	struct HudRoute {
		bool resolved = false, coverage = false, modOwned = false, fades = false, scales = false;
		const char *document = nullptr, *air = nullptr;
	};
	HudRoute                    hudRoute;
	const HudRoute &            ResolveHudRoute();
	// The stock interface's Remastered layout (idLegacyHudLayout); transient
	// presentation, never saved.
	bool                        legacyEdges = false;
	bool                        classicCanvas = false;
	float                       legacyEdge[4] = {};
	float                       legacyScale = 1.0f, legacyCenterScale = 1.0f, legacyOpacity = 1.0f;
	// The scale group being drawn, per axis: 0 the centre, 1 the leading or
	// top edge, 2 the trailing or bottom edge (idLegacyHudGroup).
	int                         legacyGroupX = 0, legacyGroupY = 0;
	void                        ApplyLegacyGroup( idDeviceContext &dc ) const;
	// The settings and edges last written to the dictionary, which a restore
	// or a reload replaces.
	float                       legacyPublished[11] = {};
	bool                        legacyPublishedValid = false;
	// The frame's engine-owned layout state last written (Redraw).
	float                       publishedFrame[24] = {};
	bool                        publishedFrameValid = false;
	// Forget everything derived from the dictionary or the source.
	void                        ResetHudPresentation( bool sourceChanged );
	struct LegacyPickupLine { idStr text; int count = 0, start = 0; bool used = false; };
	LegacyPickupLine            legacyPickups[2];
	int                         legacyPickupNewest = 0;
	void                        PublishLegacyHud( float xExpand, float yExpand );
	void                        PublishLegacyPickup();
	float                       MeasureWindowText( const char *windowName, const char *text ) const;
	idWindow *					bindHandler;

	idStr						source;
	// openQ4: the file the windows were parsed from (idLegacyHudLayout::DefinitionFor),
	// and whether to parse the named file itself: a save's earlier definition.
	idStr						definition;
	bool						literalDefinition = false;
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

// The stock interface's HUD scale (idLegacyHudLayout): a window that anchors to
// a screen edge, by its alignment or by naming the anchor, starts a group that
// scales about that edge; an axis it leaves alone keeps its ancestors' anchor,
// the centre by default. anchorX/anchorY: 0 the centre, 1 the leading or top
// edge, 2 the trailing or bottom edge, -1 inherit.
class idLegacyHudGroup {
public:
	idLegacyHudGroup( idUserInterfaceLocal *gui, idDeviceContext *dc, int anchorX, int anchorY );
	~idLegacyHudGroup();
	idLegacyHudGroup( const idLegacyHudGroup & ) = delete;
	idLegacyHudGroup &operator=( const idLegacyHudGroup & ) = delete;
private:
	idUserInterfaceLocal *gui;
	idDeviceContext *dc;
	bool active;
	int oldX = 0, oldY = 0;
	float oldAnchorX = 0.0f, oldAnchorY = 0.0f, oldScale = 1.0f;
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
	// Init rebinds the global manager when the engine starts on another thread.
	std::thread::id nativePresenceThread = std::this_thread::get_id();
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
	void BuildHudText(const char* text, float scale, float width, float height,
		idList<uiHudPrimitive_t>& output) override;
	void ClearHudPrimitives();
	idList<idRetainedHud*> generatedHuds;
	idStrList generatedHudNames;
};
