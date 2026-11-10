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

#ifndef __SESSIONLOCAL_H__
#define __SESSIONLOCAL_H__
#include "NativeInputPublications.h"
#include "PresentationPacing.h"

/*

IsConnectedToServer();
IsGameLoaded();
IsGuiActive();
IsPlayingRenderDemo();

if connected to a server
	if handshaking
	if map loading
	if in game
else if a game loaded
	if in load game menu
	if main menu up
else if playing render demo
else
	if error dialog
	full console

*/

typedef struct {
	usercmd_t	cmd;
	int			consistencyHash;
} logCmd_t;

struct fileTIME_T {
	int				index;
	ID_TIME_T			timeStamp;

					operator int() const { return timeStamp; }
};

typedef struct {
	idDict			serverInfo;
	idDict			syncedCVars;
	idDict			userInfo[MAX_ASYNC_CLIENTS];
	idDict			persistentPlayerInfo[MAX_ASYNC_CLIENTS];
	usercmd_t		mapSpawnUsercmd[MAX_ASYNC_CLIENTS];		// needed for tracking delta angles
} mapSpawnData_t;

typedef enum {
	TD_NO,
	TD_YES,
	TD_YES_THEN_QUIT
} timeDemo_t;

enum demoLibraryType_t {
	DEMO_LIBRARY_UNKNOWN,
	DEMO_LIBRARY_MVD,
	DEMO_LIBRARY_RENDER,
	DEMO_LIBRARY_COMMAND,
	DEMO_LIBRARY_LEGACY_NET,
	DEMO_LIBRARY_LEGACY_RENDER,
	DEMO_LIBRARY_INCOMPLETE
};

enum demoLibraryCapability_t {
	DEMO_CAP_PLAY			= 1 << 0,
	DEMO_CAP_PAUSE			= 1 << 1,
	DEMO_CAP_RATE			= 1 << 2,
	DEMO_CAP_SEEK			= 1 << 3,
	DEMO_CAP_STEP			= 1 << 4,
	DEMO_CAP_FREE_ROAM		= 1 << 5,
	DEMO_CAP_FOLLOW			= 1 << 6,
	DEMO_CAP_DELETE			= 1 << 7,
	DEMO_CAP_FULL_WORLD		= 1 << 8
};

// A library entry's text for the retained Demos page: its row and its
// detail lines, capped as the legacy browser caps them.
struct sessionDemoText_t {
	idStr					name;
	idStr					map;
	idStr					date;
	idStr					length;
	idStr					kind;
	idStr					status;
	idStr					detail;
};

struct demoLibraryEntry_t {
							demoLibraryEntry_t() :
								type( DEMO_LIBRARY_UNKNOWN ),
								capabilities( 0 ),
								sizeBytes( 0 ),
								timestamp( 0 ),
								durationMS( -1 ),
								playable( false ),
								compatible( false ),
								cleanEnd( false ) {
							}

	demoLibraryType_t		type;
	int						capabilities;
	int						sizeBytes;
	ID_TIME_T				timestamp;
	int						durationMS;
	bool					playable;
	bool					compatible;
	bool					cleanEnd;
	idStr					path;
	idStr					displayName;
	idStr					mapName;
	idStr					gameType;
	idStr					format;
	idStr					status;
	idStr					details;
};

// A v11 render demo's time at a frame index: each recorded frame is
// USERCMD_PER_DEMO_FRAME tics at the recording's tic rate (Session_demo.cpp).
int Session_RenderDemoFramesToMS( int frames, int userCmdHz );

// A level's localized name from its map declaration, or the map path when it
// has none (Session.cpp).
idStr Session_LevelDisplayName( const char *map, const char *entityFilter );

// False, with the player-facing reason, when a campaign render demo was
// recorded in another game directory than the active one (Session_demo.cpp).
bool Session_RenderDemoContentMatches( const char *path, idStr &reason );

// Whether the legacy browser's filter (all, mvd, render, legacy or
// incomplete) shows an entry, and an entry's text for the retained page.
bool Session_DemoEntryMatchesFilter( const demoLibraryEntry_t &entry, const char *filter );
void Session_DemoEntryText( const demoLibraryEntry_t &entry, sessionDemoText_t &out );

typedef enum {
	OPENQ4_FRAME_BOUND_UNKNOWN,
	OPENQ4_FRAME_BOUND_SIMULATION,
	OPENQ4_FRAME_BOUND_VSYNC,
	OPENQ4_FRAME_BOUND_PRESENTATION_CAP,
	OPENQ4_FRAME_BOUND_UNCAPPED
} openq4FramePacingBound_t;

// Frame intervals run on the high-resolution clock from the start of one
// presentation frame to the next, binned at 50 us (PresentationPacing.h).
typedef struct {
	bool						valid;
	bool						multiplayer;
	int							frameSampleCount;
	float						lastFrameMsec;
	float						avgFrameMsec;
	float						avgFrameHz;
	float						minFrameMsec;
	float						maxFrameMsec;
	int							frameIntervalBins[openq4::FRAME_PACING_BINS];
	int							lastTicDelta;
	float						avgTicsPerFrame;
	int							lastGameTics;
	float						avgGameTicsPerFrame;
	int							lastRequestedWaitMsec;
	int							lastWaitMsec;
	int							lastWaitOvershootMsec;
	int							lastWakeJitterMsec;
	float						avgWaitOvershootMsec;
	float						avgWakeJitterMsec;
	int							swapInterval;
	int							presentationCap;
	float						refreshHz;			// the window's display, 0 when unknown
	openq4AsyncTimingStats_t	asyncStats;
	openq4FramePacingBound_t	boundMode;
} openq4FramePacingStats_t;

// A frame-interval percentile since framePacingReset, in milliseconds.
float Session_GetFramePacingPercentileMsec( const openq4FramePacingStats_t &stats, int percentile );

// The retained sub-page a change of menu level goes to or comes back from.
typedef enum {
	RETAINED_SUBPAGE_CAMPAIGN,		// Single Player's Campaign
	RETAINED_SUBPAGE_ARENA,			// Single Player's Arena
	RETAINED_SUBPAGE_CREATE_SERVER,	// Multiplayer's Create Server
	RETAINED_SUBPAGE_JOIN_GAME,		// Multiplayer's Join Game, the server browser
	RETAINED_SUBPAGE_DEMOS,			// Multiplayer's Demos, the demo library
	RETAINED_SUBPAGE_SETTINGS_CONTROLS,	// Settings' CONTROLS
	RETAINED_SUBPAGE_SETTINGS_GAME,	// Settings' GAME OPTIONS
	RETAINED_SUBPAGE_SETTINGS_AUDIO,	// Settings' AUDIO
	RETAINED_SUBPAGE_SETTINGS_SYSTEM	// Settings' DISPLAY, the SYSTEM page
} retainedSubpage_t;

// Player- or server-supplied text for a retained screen (Session.cpp): escapes,
// controls and malformed UTF-8 dropped, at most maxBytes bytes on maxLines
// lines, never read as a #str_ key.
idStr Session_RetainedPlayerText( const char *text, int maxBytes, int maxLines );

// A difficulty's localized name (g_skill 0-4) and a play time in seconds as
// the retained screens show them (Session.cpp).
const char *Session_SkillName( int skill );
idStr Session_RetainedPlayTime( int seconds );
idStr Session_RetainedList( const char *first, const char *second );

const int USERCMD_PER_DEMO_FRAME	= 2;
const int CONNECT_TRANSMIT_TIME		= 1000;
const int MAX_LOGGED_USERCMDS		= 60*60*60;	// one hour of single player, 15 minutes of four player

// A notice's status symbol (idSessionLocal::retainedNotice_t::kind), as the
// notice layer draws it (tools/ui/retained_notices.py), and what its action
// undoes (::undo).
enum {
	RETAINED_NOTICE_FAVORITE = 1,		// the favorite's star
	RETAINED_NOTICE_CONTROLLER = 2		// a controller crossed out
};
enum {
	RETAINED_NOTICE_UNDO_NONE,
	RETAINED_NOTICE_UNDO_FAVORITE		// puts a server back among the favorites
};

class idSessionLocal : public idSession {
public:

						idSessionLocal();
	virtual				~idSessionLocal();

	virtual void		Init();

	virtual void		Shutdown();

	virtual void		Stop();
	// Arena's result handoff keeps the completed fade held while Stop tears
	// down the live world and the result GUI is prepared.
	void				StopPreservingWipe();

	virtual void		UpdateScreen( bool outOfSequence = true );

	virtual void		PacifierUpdate();
	virtual void		BeginLoadingAssetQueue( int totalAssets );
	virtual void		AdvanceLoadingAssetQueue( int loadedAssets = 1 );
	virtual void		EndLoadingAssetQueue();

	virtual void		Frame();
	void				HandleAppResume( bool afterMapLoad );
	void				HandleVRFocusLoss();

	virtual bool		IsMultiplayer();
	virtual bool		IsLoadingSaveGame() const { return loadingSaveGame; }
	virtual bool		IsGUIActive() const;
	bool				IsMapSpawned() const { return mapSpawned; }
	// A dedicated server presents nothing: openQ4-ded, and this client while it
	// runs as one without a renderer device (launched with net_serverDedicated
	// 1, or after turning into one). Its session has no menus and skips the
	// wipes, loading screens and screen updates.
	bool				IsDedicatedServer() const;
	virtual idUserInterface *GetActiveGUI() const { return ( guiTest != NULL ) ? guiTest : guiActive; }
	bool QueryNativeInputPublication(openq4::NativeSessionPublication&) const noexcept;
	virtual bool		IsMainMenuIntroPlaying() const;

	virtual bool		ProcessEvent( const sysEvent_t *event );

	virtual void		StartMenu( bool playIntro = false );
	virtual void		ExitMenu();
	virtual void		GuiFrameEvents();
	virtual void		SetGUI( idUserInterface *gui, HandleGuiCommand_t handle );
	void OpenCampaignSelector( bool campaigns, bool fromArena = false, bool fromChapters = false );
	// A campaign entered: its game started, its levels (chapters), or else its
	// difficulty page, after any engine reload into its game directory.
	void SelectCampaign( const char *campaign, bool start = false, bool chapters = false );
	bool OpenRetainedCampaignSkill();
	bool OpenRetainedCampaignChapters();
	// The active campaign's chapters: every level in campaign order, each
	// unlocked once the player reaches it (Session.cpp).
	void RecordCampaignChapter();
	void PublishCampaignChapters( idUserInterface *gui, int select = -1 );
	// Its first level's levelshot and the furthest reached one's.
	void CampaignSummaryShots( idStr &first, idStr &furthest );
	bool StartCampaignChapter( int chapter );
	bool HandleStockCampaignCommand( idUserInterface *gui, const char *menuCommand );
	// The change between Campaign and a fourth-level child: its difficulty
	// page, or its levels with `chapters`; `back` climbs to Campaign.
	void BeginRetainedCampaignSkill( idUserInterface *from, const char *campaign, bool back = false, bool chapters = false );
	// Single Player's retained Arena sub-page (guis/menu/arena.q4ui): the
	// Arena campaign's browser where it can present (NULL: the stock one
	// presents), and the page while it is loaded, which the campaign
	// publishes into.
	idUserInterface *RetainedArenaPage();
	idUserInterface *RetainedArenaGui() const { return guiRetainedArena; }
	bool				OpenSystemSettings();
	bool				ReturnSystemSettings();
	void				ReportSystemSettings();
	void				CloseSystemSettings();

	// Retained (RmlUi) screens, enabled by default and gated by ui_retained.
	// The title and single-player pause screens cover the legacy main menu
	// while it rests at its home state; remaining legacy pages use hand-off.
	void				UpdateRetainedHome();
	bool				RetainedHomePresenting() const { return guiRetainedHome != NULL; }
	// Whether menu input reaches a retained view: the home screen over the
	// legacy menu, or an active retained document such as the SYSTEM page.
	bool				RetainedInputTarget() const;
	// The retained view that takes the menu input now, as MenuEvent routes it,
	// or NULL while none does or a change between menu levels holds it.
	idUserInterface *	RetainedStickTarget() const;
	bool				RetainedHomeInputBlocked() const;
	void				RetainedHomeFrameEvent();
	void				DrawRetainedHome( int presentationTime );
	void				HandleRetainedSessionRequest( idUserInterface *gui, const char *request );
	void				PublishRetainedPauseState( idUserInterface *gui );
	idStr				RetainedPauseShot( const char *mapPath, const char *entityFilter = NULL ) const;
	void				ReportRetainedScreens();
	idUserInterface *	SelectRetainedLoadingGui( idUserInterface *legacy, bool multiplayer );
	// A screen's retained document, or NULL while its stock GUI presents
	// instead; a home screen also needs every legacy page it hands off to.
	idUserInterface *	FindRetainedGui( const char *path, bool shared, bool home );
	// A home screen's document, loaded on first use; NULL once it falls back.
	idUserInterface *	RetainedHomeDocument( idUserInterface *&home, const char *path );
	// Whether the local player is Strogg, Kane after his stroggification.
	bool				RetainedPauseIsStrogg();
	bool				RetainedSystemAvailable() const;
	// The SYSTEM button and the multiplayer menu's SYSTEM route: the retained
	// page, or the stock page when it cannot open.
	void				OpenSystemSettingsRoute( bool fromMultiplayer );
	// Resolve the screens and their pictures where the stock menus resolve
	// theirs: at startup the screens the title reaches, and inside each
	// single-player level load those a level reaches.
	void				PreloadRetainedScreens();
	// Starts parsing PreloadRetainedScreens' documents on the job system's
	// workers, while the stock menus load.
	void				PrefetchRetainedScreens();
	void				PrepareRetainedLevel( const char *mapPath, bool multiplayer );
	// As a map change begins, starts parsing the level's retained documents on
	// the job system's workers: the pages PrepareRetainedLevel loads, and the
	// HUD, weapon wheel and quick chat documents the game loads as it spawns.
	void				PrefetchRetainedLevel();
	// Once the game has spawned: drops the parses the single-player level did
	// not take, the other family's HUD and weapon wheel.
	void				SettleRetainedLevelPrefetch();
	// Every multiplayer map's levelshot for Create Server, and the Arena
	// campaign's for its page: a few on each idle front-end frame, or when
	// `all` is set every one still waiting of a page's kind, the maps' or not.
	void				PrecacheRetainedLevelshots( bool all, bool maps = true );
	// A levelshot for those frames; `material` precaches its stock material too.
	void				QueueRetainedLevelshot( const char *picture, bool material );
	// After the game spawns the map, still inside the load: the objective
	// screenshots the pause screen's Objectives page may show.
	void				PrecacheRetainedLevelImages();
	// The loader's phase and its place, or the asset queue's count, under the
	// retained loading bar.
	void				SetRetainedLoadingPhase( int phase, const char *count = NULL );
	void				PublishRetainedLoadingCount();
	// Single player loading tips: the first when the screen presents, then the
	// next every 6 s.
	void				PublishRetainedLoadingTip( bool first );
	// The multiplayer arsenal: the index'th kind of item to spawn, by its icon.
	void				PublishRetainedLoadingIcon( int index, const char *icon );
	// The loading server card's players (section 14.17): those known as a
	// multiplayer load starts, noted by the async server and client before
	// the map loads and published by team with the card.
	void				ClearRetainedLoadingRoster();
	void				NoteRetainedLoadingPlayer( const idDict &userInfo );
	void				NoteRetainedLoadingConnecting( int count );
	void				PublishRetainedLoadingServer( const idDict &serverInfo );
	// A multiplayer join that ends at the Welcome card keeps its loading
	// screen up, reading JOINING, until the card presents or 5 s pass, then
	// fades it out over the card (section 14.17); user commands wait
	// meanwhile.
	bool				BeginRetainedLoadingHold();
	void				FadeRetainedLoadingHold( const char *reason );
	void				UpdateRetainedLoadingHold();
	void				ClearRetainedLoadingHold();
	static const int	RETAINED_ROSTER_SLOTS = 32;
	idStr				retainedRosterName[ RETAINED_ROSTER_SLOTS ];
	int					retainedRosterTeam[ RETAINED_ROSTER_SLOTS ];	// 0 Marine, 1 Strogg, -1 spectating
	int					retainedRosterCount;
	int					retainedRosterConnecting;
	bool				retainedRosterKnown;
	// A page and its sub-page change (spec 1.10): Single Player and its
	// Campaign and Arena, Multiplayer and its Create Server and Join Game.
	void				BeginRetainedSubpage( idUserInterface *gui, bool deeper, retainedSubpage_t subpage = RETAINED_SUBPAGE_CAMPAIGN );
	void				PresentRetainedSubpage( bool deeper, retainedSubpage_t subpage );
	void				UpdateRetainedSubpage();
	bool				RetainedSubpageEvent( const sysEvent_t *event );
	// The retained Load Game page (guis/menu/loadgame.q4ui): LOAD GAME on the
	// title and the Marine pause hands off to it as to a stock page.
	bool				BeginRetainedPage( const char *request );
	void				UpdateRetainedPage();
	void				ReturnRetainedPage();
	void				ReturnRetainedSinglePlayerHome();
	void				DrawRetainedPageHome( int presentationTime );
	void				PublishRetainedLoadPage( idUserInterface *gui, int select, const char *error );
	void				BuildRetainedSaveList( idUserInterface *gui, int select );
	bool				RefreshRetainedSaveList( idUserInterface *gui, const char *request );
	void				PublishRetainedLoadRows( idUserInterface *gui );
	void				PublishRetainedLoadSave( idUserInterface *gui, const char *error );
	void				HandleRetainedLoadRequest( idUserInterface *gui, const char *request );
	// The retained Save Game page (guis/menu/savegame.q4ui): SAVE GAME on the
	// Marine pause hands off to it the same way. RetainedPageActive is true
	// while either page presents.
	bool				RetainedPageActive() const;
	void				PublishRetainedSavePage( idUserInterface *gui, int select );
	void				PublishRetainedSaveSelection( idUserInterface *gui, bool name );
	void				HandleRetainedSaveRequest( idUserInterface *gui, const char *request );
	bool				RetainedSaveGame( idUserInterface *gui, const char *name );
	// The retained Restart Level page (guis/menu/restartlevel.q4ui), which
	// RESTART LEVEL on the Marine pause hands off to the same way.
	void				HandleRetainedRestartRequest( idUserInterface *gui, const char *request );
	// The retained Demos page (guis/menu/demos.q4ui), which the title's DEMOS
	// hands off to the same way: the library, filtered, a page at a time.
	void				PublishRetainedDemosPage( idUserInterface *gui, const char *selectPath, int selectRow, const char *error );
	void				PublishRetainedDemoRows( idUserInterface *gui );
	void				PublishRetainedDemoSelection( idUserInterface *gui, const char *error );
	void				HandleRetainedDemosRequest( idUserInterface *gui, const char *request );
	idUserInterface *	RetainedDemosPage();
	void				OpenRetainedDemos();
	// The Arena page's verbs, which run the Arena campaign's own commands.
	void				HandleRetainedArenaRequest( idUserInterface *gui, const char *request );
	// The retained Multiplayer page (guis/menu/multiplayer.q4ui), which the
	// title's MULTIPLAYER hands off to: its sections open the stock pages
	// after its departure, and their Back presents it again (multiplayerOpen);
	// back from a sub-page it returns onto that sub-page's section.
	idUserInterface *	RetainedMultiplayerPage();
	bool				OpenRetainedMultiplayerPage( const char *section = NULL );
	void				HandleRetainedMultiplayerPageRequest( idUserInterface *gui, const char *request );
	void				UpdateRetainedMultiplayerPage();
	bool				OpenRetainedPlayerSetup( idUserInterface *gui );
	bool				PublishRetainedPlayerSetup( idUserInterface *gui, bool rebuild = false );
	void				HandleRetainedPlayerSetupRequest( idUserInterface *gui, const char *payload );
	// The right stick (section 13.5), its deflection -1..1 held for `seconds`:
	// y scrolls the view's detail area and x turns Player Setup's preview, as
	// its Rotation row does. Whether anything moved.
	bool				RetainedDetailStick( idUserInterface *gui, float x, float y, float seconds );
	bool				TurnRetainedPlayerSetup( idUserInterface *gui, float x, float seconds );
	idUserInterface *	RetainedSettingsPage();
	bool				OpenRetainedSettingsPage( const char *section = NULL );
	void				HandleRetainedSettingsPageRequest( idUserInterface *gui, const char *request );
	void				UpdateRetainedSettingsPage();
	bool				RetainedSettingsParent() const;
	// The retained Create Server sub-page (guis/menu/createserver.q4ui), which
	// CREATE SERVER on the Multiplayer page goes deeper to: the session owns
	// its settings and publishes them (create.*), and CREATE SERVER starts the
	// server as the stock page's does.
	idUserInterface *	RetainedCreateServerPage();
	void				OpenRetainedCreateServer();
	void				PublishRetainedCreateServer( idUserInterface *gui );
	void				PublishRetainedCreateMaps( idUserInterface *gui, bool rescan );
	void				PublishRetainedCreateBans( idUserInterface *gui );
	void				HandleRetainedCreateServerRequest( idUserInterface *gui, const char *request );
	bool				StartMenuServer();
	// The retained Join Game sub-page (guis/menu/joingame.q4ui), which JOIN
	// GAME on the Multiplayer page goes deeper to: the session lists the
	// server scan's servers (join.*) as the scan finds them, and JOIN SERVER
	// connects to the chosen one.
	idUserInterface *	RetainedJoinGamePage();
	void				OpenRetainedJoinGame();
	void				PublishRetainedJoinGame( idUserInterface *gui );
	void				PublishRetainedJoinDetail( idUserInterface *gui );
	void				UpdateRetainedJoinGame();
	bool				RetainedAudioAvailable() const;
	bool				OpenRetainedAudio();
	void				OpenRetainedAudioRoute();
	void				ReturnRetainedAudio();
	void				PublishRetainedAudio( idUserInterface *gui );
	void				HandleRetainedAudioRequest( idUserInterface *gui, const char *request );
	bool				RetainedGameAvailable() const;
	bool				OpenRetainedGame();
	void				OpenRetainedGameRoute( bool fromMultiplayer = false );
	void				ReturnRetainedGame();
	void				PublishRetainedGame( idUserInterface *gui );
	void				HandleRetainedGameRequest( idUserInterface *gui, const char *request );
	bool				RetainedControlsAvailable() const;
	bool				OpenRetainedControls();
	void				OpenRetainedControlsRoute( bool fromMultiplayer = false );
	void				ReturnRetainedControls();
	void				PublishRetainedControls( idUserInterface *gui );
	void				HandleRetainedControlsRequest( idUserInterface *gui, const char *request );
	bool				SaveForSuspension( void );
	bool				RetainedControlsKey( const sysEvent_t *event );
	void				PushRetainedNotice( const char *text, int kind, const char *action, const char *subject, int undo,
							bool persistent );
	void				ResolveRetainedNotices( int kind );
	void				UpdateRetainedNotices();
	void				HandleRetainedNoticeAction( idUserInterface *gui, int slot );
	// The stock browser's server tooltip, kept for the hovered row while the
	// classic main menu runs (idServerScan::GUIUpdateHover).
	void				UpdateBrowserTooltip();
	void				HandleRetainedJoinGameRequest( idUserInterface *gui, const char *request );
	// The demo library's operations, which the legacy browser and the
	// retained page share.
	void				ScanDemoLibrary();
	bool				PlayDemoLibraryEntry( int index );
	bool				DeleteDemoLibraryFile( const demoLibraryEntry_t &entry );
	// Why LoadGame last refused a save, including a failed restore after teardown.
	enum loadGameFailure_t {
		LOADGAME_FAILURE_NONE,
		LOADGAME_FAILURE_UNREADABLE,	// missing, or unreadable
		LOADGAME_FAILURE_DAMAGED,		// truncated, damaged or from an incompatible version
		LOADGAME_FAILURE_MAP,		// its level is not installed
		LOADGAME_FAILURE_PLATFORM,	// another platform's build wrote it (wire ABI)
		LOADGAME_FAILURE_RESTORE		// semantic restore failed after map teardown
	};
	loadGameFailure_t	loadGameFailure;
	bool				ExecuteSaveGameMapChange( const char *saveName );
	void				ShowSaveRestoreFailure( const char *saveName );
	// The multiplayer menu card (section 14.18) covers the game's own menu
	// while it is open over a match; the game keeps the menu, its state and
	// its commands, and publishes the card's mp.* keys.
	bool				RetainedMultiplayerCovers() const;
	// The menu GUI that ProcessEvent actually delivers input to (a retained
	// screen covering guiActive receives it instead of guiActive).
	idUserInterface *	MenuInputGUI() const;
	void				UpdateRetainedMultiplayer();
	void				RetainedMultiplayerFrameEvent();
	void				RetireRetainedMultiplayer( bool release );
	void				HandleRetainedMultiplayerRequest( idUserInterface *gui, const char *request );
	// The match summary card (section 14.14) covers the game's summary the
	// same way; the session owns its chat field's typing, into the stock
	// summary's chattext, at most the chat input's 128 bytes.
	static const int	RETAINED_SUMMARY_CHAT_LIMIT = 128;
	bool				RetainedSummaryCovers() const;
	void				BeginRetainedSummaryChat( bool team );
	void				EndRetainedSummaryChat( bool clear );
	void				PublishRetainedSummaryChat();
	bool				RetainedSummaryChatEvent( const sysEvent_t *event );
	// The game's answer to a menu command: close the menu (NULL), or open the
	// main menu, with an event for it after "main ".
	void				HandleGameMenuReturn( const char *cmd );

	virtual const char *MessageBox( msgBoxType_t type, const char *message, const char *title = NULL, bool wait = false, const char *fire_yes = NULL, const char *fire_no = NULL, bool network = false  );
	virtual void		StopBox( void );
	virtual void		DownloadProgressBox( backgroundDownload_t *bgl, const char *title, int progress_start = 0, int progress_end = 100 );
	virtual void		SetPlayingSoundWorld();
	void				SetPlayingSoundWorld( idSoundWorld *soundWorld );
	void				UpdateSoundWorldFocus();

	virtual void		TimeHitch( int msec );

	virtual void		ReadCDKey( void );
	virtual void		WriteCDKey( void );
	virtual const char *GetCDKey( bool xp );
	virtual bool		CheckKey( const char *key, bool netConnect, bool offline_valid[ 2 ] );
	virtual bool		CDKeysAreValid( bool strict );
	virtual void		ClearCDKey( bool valid[ 2 ] );
	virtual bool		WaitingForGameAuth( void );
	virtual void		CDKeysAuthReply( bool valid, const char *auth_msg );

	virtual int			GetSaveGameVersion( void );

	virtual const char *GetCurrentMapName();
	const openq4FramePacingStats_t &GetFramePacingStats( void ) const { return framePacingStats; }
	void				ResetFramePacingStats( void );
	void				PrintFramePacingSnapshot( const char *reason = NULL ) const;
	void				SampleMultiplayerFramePacing( void );
	void				RunTimedWaitBoxPacingTest( int durationMsec, bool network, const char *reason = NULL );
	void				RunTimedMessageBoxPacingTest( int durationMsec, bool network, const char *reason = NULL );

	//=====================================

	int					GetLocalClientNum();

	void				MoveToNewMap( const char *mapName );

	// loads a map and starts a new game on it
	void				StartNewGame( const char *mapName, bool devmap = false, const char *entityFilter = NULL );
	void				PlayIntroGui();

	void				LoadSession( const char *name );
	void				SaveSession( const char *name );

	// called by Draw when the scene to scene wipe is still running
	void				DrawWipeModel();
	void				StartWipe( const char *materialName, bool hold = false);
	void				CompleteWipe();
	void				ClearWipe();

	void				ShowLoadingGui();

	void				ScrubSaveGameFileName( idStr &saveFileName ) const;
	idStr				GetAutoSaveName( const char *mapName ) const;

	bool				LoadGame(const char *saveName);
	// restarts the running single-player level from its beginning at a difficulty
	bool				RestartLevelAtSkill( int skill );
	bool				SaveGame(const char *saveName = NULL, saveType_t saveType = ST_REGULAR);
	bool FinishPendingSave( bool wait );
	bool				DeleteGame(const char *saveName);

	const char			*GetAuthMsg( void );

	//=====================================

	static idCVar		com_showAngles;
	static idCVar		com_showTics;
	static idCVar		com_minTics;
	static idCVar		com_fixedTic;
	static idCVar		com_showDemo;
	static idCVar		com_skipGameDraw;
	static idCVar		com_aviDemoWidth;
	static idCVar		com_aviDemoHeight;
	static idCVar		com_aviDemoSamples;
	static idCVar		com_aviDemoTics;
	static idCVar		com_wipeSeconds;
	static idCVar		com_guid;
	static idCVar		com_lastQuicksave;

	static idCVar		gui_configServerRate;

	int					timeHitch;

	bool				menuActive;

	bool				insideExecuteMapChange;	// draw loading screen and update
												// screen on prints

	// The sound world the session wants playing, before the unfocused-silence
	// filter is applied. Kept so focus changes can be re-derived every frame
	// instead of being latched into what the sound system was last told.
	idSoundWorld *		requestedSoundWorld;
	int					bytesNeededForMapLoad;	// 

	// we don't want to redraw the loading screen for every single
	// console print that happens
	int					lastPacifierTime;
	// Measured cost of the last blocking-load redraw. The pacifier interval backs
	// off to a multiple of this so loading-screen presentation can never consume an
	// unbounded share of the load's wall clock on a slow present path.
	int					lastPacifierDrawMsec;
	bool				loadingAssetQueueActive;
	int					loadingAssetQueueTotal;
	int					loadingAssetQueueLoaded;
	float				loadingAssetQueueStartPct;
	bool				iamTheDukeActive;

	// this is the information required to be set before ExecuteMapChange() is called,
	// which can be saved off at any time with the following commands so it can all be played back
	mapSpawnData_t		mapSpawnData;
	idStr				currentMapName;			// for checking reload on same level
	idStr				currentFilterString;		// for checking reload on same filtered level
	bool				mapSpawned;				// cleared on Stop()

	int					numClients;				// from serverInfo

	int					logIndex;
	logCmd_t			loggedUsercmds[MAX_LOGGED_USERCMDS];
	int					statIndex;
	logStats_t			loggedStats[MAX_LOGGED_STATS];
	int					lastSaveIndex;
	int					lastCheckPoint;
	bool				objectiveFailed;
	// each game tic, numClients usercmds will be added, until full

	bool				insideUpdateScreen;	// true while inside ::UpdateScreen() or a capture draw (sessionCaptureDrawGuard_t)
	bool				insidePacifierUpdate;	// prevents recursive loading/network updates

	bool				loadingSaveGame;	// currently loading map from a SaveGame
	idFile *			savegameFile;		// this is the savegame file to load from
	int					savegameVersion;

	idFile *			cmdDemoFile;		// if non-zero, we are reading commands from a file

	int					latchedTicNumber;	// set to com_ticNumber each frame
	int					lastGameTic;		// while latchedTicNumber > lastGameTic, run game frames
	int					lastDemoTic;
	bool				syncNextGameFrame;
	bool				syncNextGameFrameAwaitingAsyncTicLog;
	bool				cinematicStateValid;
	bool				cinematicActive;
	openq4FramePacingStats_t framePacingStats;
	double				framePacingWindowMsec;
	double				framePacingLastFrameClock;
	int					framePacingLastLatchedTic;
	openq4FramePacingBound_t framePacingLastLoggedBound;
	int					framePacingLastLoggedSampleCount;


	bool				aviCaptureMode;		// if true, screenshots will be taken and sound captured
	idStr				aviDemoShortName;	// 
	float				aviDemoFrameCount;
	int					aviTicStart;

	timeDemo_t			timeDemo;
	int					timeDemoStartTime;
	int					numDemoFrames;		// for timeDemo and demoShot
	int					demoTimeOffset;
	renderView_t		currentDemoRenderView;
	// the next one will be read when 
	// com_frameTime + demoTimeOffset > currentDemoRenderView.

	// TODO: make this private (after sync networking removal and idnet tweaks)
	idUserInterface *	guiActive;
	HandleGuiCommand_t	guiHandle;

	idUserInterface *	guiInGame;
	idUserInterface *	guiMainMenu;
	// Explicit opt-in child; the legacy parent remains allocated and inactive.
	idUserInterface *	guiSystem;
	idUserInterface *	guiSystemParent;
	HandleGuiCommand_t	guiSystemParentHandle;
	bool				systemGuiTransition;
	bool				systemGuiBackEvent;
	// Retained screens (ui_retained): the home document presenting now, the
	// cached title/pause documents, and the hand-off window during which the
	// retained screen still covers a legacy page transition.
	idUserInterface *	guiRetainedHome;
	idUserInterface *	guiRetainedTitle;
	idUserInterface *	guiRetainedPause;
	idUserInterface *	guiRetainedPauseStrogg;	// the Strogg family's pause
	int					retainedPauseStrogg;	// this level's player is Strogg (-1: not asked yet)
	ID_TIME_T			retainedNewestSave;		// when the newest save was written (0: none)
	idUserInterface *	guiRetainedReleasing;	// a closed pause screen releasing the softened view
	int					retainedReleaseUntil;	// presentation time the release ends
	// A change between the Single Player page and its Campaign sub-page: the
	// leaving document plays its half, and the other presents at the hand-over.
	idUserInterface *	retainedSubpageFrom;	// the document playing its half (NULL: none)
	bool				retainedSubpageDeeper;	// going to the sub-page, or back to the page
	int					retainedSubpageBegan;	// presentation time the change began
	int					retainedSubpageUntil;	// presentation time of the hand-over
	int                     retainedDetailTarget = 0; // 0: normal level change, 1: skill, 2: campaign parent, 3: chapters, 4: campaign parent onto CHAPTERS
	idStr                   retainedDetailCampaign;
	bool                    wipeToBlack = false;
	int                     wipeFadeStarted = 0, wipeFadeDuration = 0;
	retainedSubpage_t	retainedSubpageKind;	// the sub-page the change goes to or comes back from
	bool				retainedHomeReturning;
	bool				retainedHomeUnderMessage;	// a stock message box covers the presented home
	bool				retainedJoinReturn;		// a join from Join Game is under way: a return finds its list
	// The Load Game page: its document (NULL until it first loads), the home
	// that handed off to it, a hand-off presenting it and when, whether the
	// home presents coming back from it, and its saves (newest first, with
	// when each was written), the first save its rows show and the selected
	// save (-1: none).
	idUserInterface *	guiRetainedLoad;
	idUserInterface *	retainedPageHome;		// the home drawing behind the page
	idUserInterface *	retainedPagePending;
	int					retainedPageAt;
	bool				retainedPageReturn;
	idStrList			retainedLoadSlots;
	idList<ID_TIME_T>	retainedLoadTimes;
	idStrList			retainedLoadNames;
	idStrList			retainedLoadLocations;
	int					retainedSaveGeneration;
	int					retainedLoadFirst;
	int					retainedLoadSelected;
	// The Save Game page (NULL until it first loads), which shares the list
	// above, and the save its overwrite dialog asks about.
	idUserInterface *	guiRetainedSave;
	idStr				retainedSaveTarget;
	// The Restart Level page (NULL until it first loads).
	idUserInterface *	guiRetainedRestart;
	// The Strogg pause's Load Game, Save Game and Restart Level pages, the
	// same pages in the Strogg family (NULL until they first load).
	idUserInterface *	guiRetainedLoadStrogg;
	idUserInterface *	guiRetainedSaveStrogg;
	idUserInterface *	guiRetainedRestartStrogg;
	bool				IsRetainedLoadPage( const idUserInterface *gui ) const {
		return gui != NULL && ( gui == guiRetainedLoad || gui == guiRetainedLoadStrogg );
	}
	bool				IsRetainedSavePage( const idUserInterface *gui ) const {
		return gui != NULL && ( gui == guiRetainedSave || gui == guiRetainedSaveStrogg );
	}
	bool				IsRetainedRestartPage( const idUserInterface *gui ) const {
		return gui != NULL && ( gui == guiRetainedRestart || gui == guiRetainedRestartStrogg );
	}
	// The Demos page (NULL until it first loads): the library entries its
	// filter shows (indices into demoLibrary, newest first), the first one
	// its rows show, the selected one (-1: none) and the filter (0: all).
	idUserInterface *	guiRetainedDemos;
	idList<int>			retainedDemoRows;
	int					retainedDemoFirst;
	int					retainedDemoSelected;
	int					retainedDemoFilter;
	// The multiplayer Escape and Welcome cards, loaded inside each
	// multiplayer level load; the card covering the game's menu now (NULL:
	// none) and whether it is Welcome; a stock page that took the menu over
	// until it closes; the game's last published revision; and a hand-off to
	// a stock page waiting for its menu column to show.
	idUserInterface *	guiRetainedEscape;
	idUserInterface *	guiRetainedWelcome;
	idUserInterface *	guiRetainedSummary;
	// The single-player Arena's pause card.
	idUserInterface *	guiRetainedArenaPause;
	idUserInterface *	guiRetainedMultiplayer;
	idUserInterface *	retainedMultiplayerTarget;		// the game GUI the cover was last decided for
	bool				retainedMultiplayerSummary;		// the card covers the summary
	bool				retainedSummaryTyping;
	int					retainedSummaryCaret;			// a byte offset into the stock summary's chattext
	bool				retainedMultiplayerWelcome;
	bool				retainedMultiplayerUncovered;
	int					retainedMultiplayerRevision;
	int					retainedMultiplayerHandoff;		// the stock page's index (-1: none)
	int					retainedMultiplayerHandoffUntil;
	// Retained documents that fell back to their stock screens this session.
	idStrList			retainedStock;
	int					retainedHandoffUntil;
	// The retained loading screen's phase line and prompt device, published
	// only while that screen presents the load (-1: not published yet).
	float				retainedPointerX;		// the home screen's last published pointer
	float				retainedPointerY;
	bool				retainedLoadingActive;
	int					retainedLoadingPhase;
	int					retainedLoadingLoaded;
	int					retainedLoadingTotal;
	int					retainedLoadingTip;		// index of the tip showing (-1: none)
	int					retainedLoadingTipSlot;	// the label slot showing it (0: a, 1: b)
	int					retainedLoadingTipAt;	// presentation time it was shown
	idUserInterface *	guiLoadingHold;				// the loading screen held over a join (NULL: none)
	int					retainedLoadingHoldBegan;	// presentation time the hold began (the load's, stale)
	int					retainedLoadingHoldLive;	// the first presentation time after it (0: not yet)
	int					retainedLoadingHoldUntil;	// when it gives up, or once fading, when the fade ends
	bool				retainedLoadingHoldFading;
	idListGUI *			guiMainMenu_MapList;		// easy map list handling
	idUserInterface *	guiDemoMenu;
	idListGUI *			guiDemoList;
	idUserInterface *	guiRestartMenu;
	idUserInterface *	guiRetainedRestartMenu;	// the retained restart screen (NULL until it loads)
	idUserInterface *	guiRetainedCredits;		// the retained Credits page (NULL until it loads)
	idUserInterface *	guiRetainedArena;		// the retained Arena sub-page (NULL until it loads)
	idUserInterface *	guiRetainedMpPage;		// the retained Multiplayer page (NULL until it loads)
	int					retainedMpPageDest;		// the stock Multiplayer page it is leaving for (0: none)
	int					retainedMpPageLeft;		// the stock Multiplayer page it last left for, which Back returns from
	int					retainedMpPageAt;		// presentation time that page takes over
	idStrList			retainedSetupModels, retainedSetupNames;
	idStr				retainedSetupCvar, retainedSetupValue, retainedSetupLanguage;
	unsigned int		retainedSetupCatalog;
	int					retainedSetupFirst, retainedSetupDeclCount;
	float				retainedSetupRotation;
	idUserInterface *	guiRetainedSettingsPage;	// the retained SETTINGS page (NULL until it loads)
	int					retainedSettingsDest;		// the stock settings panel it is leaving for (0: none)
	int					retainedSettingsLeft;		// the stock panel it last left for, which Back returns from
	int					retainedSettingsAt;			// presentation time that panel takes over
	idUserInterface *	guiRetainedCreate;		// the retained Create Server sub-page (NULL until it loads)
	struct retainedLevelshot_t {
		idStr			name;					// a multiplayer map's path, or a picture
		bool			map;					// a map, whose levelshot RetainedPauseShot chooses
		bool			material;				// a picture whose stock material precaches too
	};
	idList<retainedLevelshot_t> retainedLevelshots;	// levelshots still to precache
	int					retainedLevelshotsFrom;	// when idle frames start precaching them (0: not yet set)
	idList<int>			retainedCreateMaps;		// the map decls the chosen game type plays, in list order
	int					retainedCreateMapFirst;	// the first of them the list holds
	int					retainedCreateMapSelected;	// the chosen one among them (-1: none)
	idStrList			retainedCreateBanGuids;		// PLAYER BAN LIST's rows' GUIDs, in list order
	int					retainedCreateBanSelected;	// the chosen row (-1: none)
	idUserInterface *	guiRetainedJoin;		// the retained Join Game sub-page (NULL until it loads)
	idList<int>			retainedJoinServers;	// the scan's servers the list shows, in list order
	int					retainedJoinFirst;		// the first of them the list holds
	idStr				retainedJoinAddress;	// the chosen server's address (empty: none)
	idStrList			retainedJoinRowAddresses;	// each page row's server address as last published
	int					retainedJoinRevision;	// the scan's revision the page shows (-1: none yet)
	idStr				retainedJoinShotMap;	// the map whose levelshot the page shows
	idStr				retainedJoinShot;		// that levelshot
	int					retainedJoinDetailRow;	// the page row whose server's details the page holds (-1: none)
	// A notice of section 13.13 over the retained menus: its text and its
	// action's label (language-table keys; no action: empty), its status
	// symbol, what the action undoes (RETAINED_NOTICE_UNDO_*) and on what (a
	// server's address), whether it stays until resolved, and when it entered
	// and began to leave (-1: not yet).
	struct retainedNotice_t {
		idStr			text;
		idStr			action;
		int				kind;
		idStr			subject;
		int				undo;
		bool			persistent;
		bool			resolved;
		int				shownAt;
		int				leavingAt;
	};
	idUserInterface *	guiRetainedAudio;			// the retained AUDIO settings page (NULL until it loads)
	idUserInterface *	retainedAudioParent;		// the screen it returns to, and that screen's handler
	HandleGuiCommand_t	retainedAudioParentHandle;
	idUserInterface *	guiRetainedGame;			// the retained GAME OPTIONS settings page (NULL until it loads)
	idUserInterface *	retainedGameParent;			// the screen it returns to, and that screen's handler
	HandleGuiCommand_t	retainedGameParentHandle;
	idUserInterface *	guiRetainedControls;		// the retained CONTROLS settings page (NULL until it loads)
	idUserInterface *	retainedControlsParent;		// the screen it returns to, and that screen's handler
	HandleGuiCommand_t	retainedControlsParentHandle;
	int					retainedControlsCapture;	// the row waiting for a key, or -1
	int					retainedControlsCaptured;	// the key that ended a wait, until it is released, or -1
	idList<retainedNotice_t> retainedNotices;	// shown oldest first, then those waiting, as they came
	bool				retainedNoticesDirty;		// the stack changed since it was published
	idUserInterface *	retainedNoticesGui;			// the retained menu it was published to
	idStr				retainedNoticeGamepad;		// in_gamepadFamily as last seen (empty: not yet read)
	idUserInterface *	guiLoading;
	idUserInterface *	guiIntro;
	idUserInterface *	guiGameOver;
	idUserInterface *	guiTest;
	idUserInterface *	guiTakeNotes;
	
	idUserInterface *	guiMsg;
	idUserInterface *	guiMsgRestore;				// store the calling GUI for restore
	idStr				msgFireBack[ 2 ];
	bool				msgRunning;
	int					msgRetIndex;
	bool				msgIgnoreButtons;
	bool				menuIntroBlackoutActive;
	bool				menuIntroBlackoutAwaitMenuMusic;
	int					menuIntroBlackoutFadeStart;
	int					fallbackMenuStartTime;
	
	bool				waitingOnBind;

	const idMaterial *	whiteMaterial;

	const idMaterial *	wipeMaterial;
	int					wipeStartTic;
	int					wipeStopTic;
	bool				wipeHold;
	int					stopDepth;
	bool				preserveWipeDuringStop;

#if ID_CONSOLE_LOCK
	int					emptyDrawCount;				// watchdog to force the main menu to restart
#endif

	//=====================================
	void				Clear();
	void				StopInternal( bool preserveWipe );

	void				DrawCmdGraph();
	void				Draw();

	void				WriteCmdDemo( const char *name, bool save = false);
	void				StartPlayingCmdDemo( const char *demoName);
	void				TimeCmdDemo( const char *demoName);
	void				SaveCmdDemoToFile(idFile *file);
	bool				LoadCmdDemoFromFile( idFile *file, idStr &error );
	bool				ValidateCmdDemoFile( const char *path, idStr &error );
	void				StartRecordingRenderDemo( const char *name );
	void				StopRecordingRenderDemo();
	void				StartPlayingRenderDemo( idStr name );
	void				StopPlayingRenderDemo();
	void				CompressDemoFile( const char *scheme, const char *name );
	void				TimeRenderDemo( const char *name, bool twice = false );
	void				AVIRenderDemo( const char *name );
	void				AVICmdDemo( const char *name );
	void				AVIGame( const char *name );
	void				BeginAVICapture( const char *name );
	void				EndAVICapture();

	void				AdvanceRenderDemo( bool singleFrameOnly );
	void				ProcessRenderDemoSeekBudget();
	void				FinishRenderDemoSeek();
	void				RunGameTic();

	void				InitDemoSystem();
	void				ShutdownDemoSystem();
	void				OpenDemoMenu( bool browser = true );
	void				CloseDemoMenu();
	void				RefreshDemoLibrary();
	void				RefreshDemoLibraryList();
	void				UpdateDemoMenuGui();
	bool				HandleDemoMenuCommand( const char *menuCommand );
	bool				IsDemoPlaybackActive() const;
	void				SetDemoPaused( bool paused );
	void				ToggleDemoPaused();
	void				SetDemoSpeed( float speed );
	bool				SeekDemoMS( int timeMS );
	bool				SkipDemoMS( int deltaMS );
	void				StepDemo();
	void				StopDemoPlayback();
	
	void				FinishCmdLoad();
	void				LoadLoadingGui(const char *mapName);

	void				DemoShot( const char *name );

	void				TestGUI( const char *name );

	int					GetBytesNeededForMapLoad( const char *mapName );
	void				SetBytesNeededForMapLoad( const char *mapName, int bytesNeeded );

	void				ExecuteMapChange( bool noFadeWipe = false );
	void				UnloadMap();
	void				ToggleIAmTheDuke( void );

	// return true if we actually waiting on an auth reply
	bool				MaybeWaitOnCDKey( void );

	//------------------
	// Session_menu.cpp

	idStrList			loadGameList;
	idList<idModInfo>	modsList;
	idList<demoLibraryEntry_t> demoLibrary;
	idStr				demoLibraryFilter;
	idStr				demoSelectedPath;
	idUserInterface *	demoReturnGui;
	bool				demoOverlayVisible;
	bool				demoBrowserMode;
	bool				demoMenuOpenedOverPlayback;
	bool				demoResumeAfterMenu;
	int					demoActiveType;

	idStr				activeRenderDemoName;
	idStr				renderDemoDurationName;
	bool				renderDemoPaused;
	bool				renderDemoSeeking;
	bool				renderDemoSeekRestartPending;
	bool				renderDemoSeekRestartPresentationPending;
	bool				renderDemoSeekWorkDeferred;
	bool				renderDemoStepPending;
	bool				renderDemoFramePayloadPending;
	bool				renderDemoSeekMuteOwned;
	bool				renderDemoSeekRestoreMute;
	float				renderDemoPlaybackRate;
	double				renderDemoFrameAccumulator;
	int					renderDemoStartViewTime;
	int					renderDemoKnownDurationMS;
	int					renderDemoSeekTargetMS;
	int					renderDemoSeekDeadlineMS;

	// Campaign render demos (render demo v11, Session_demo.cpp). A recording
	// is one file of self-contained segments, one per level it spans: a
	// single-player map change, save load, death reload or restart suspends
	// it and the next level starts a new segment. writeDemo is set only while
	// recording, so nothing is written while a level loads, before the first
	// level or between levels.
	enum renderDemoRecordState_t {
		RENDER_DEMO_RECORD_IDLE,
		RENDER_DEMO_RECORD_ARMED,		// waiting for the next single-player level
		RENDER_DEMO_RECORD_RECORDING,
		RENDER_DEMO_RECORD_SUSPENDED	// between levels
	};
	struct renderDemoSegment_t {
		int				startFrame;		// frames recorded before the segment
		idStr			map;
		idStr			entityFilter;
		idStr			reason;			// start, level, load or restart
	};
	renderDemoRecordState_t	renderDemoRecordState;
	idDemoFile *		renderDemoRecordFile;
	idStr				renderDemoRecordPath;
	int					renderDemoRecordFrames;
	int					renderDemoRecordStartMsec;
	idList<renderDemoSegment_t> renderDemoRecordSegments;
	idStr				renderDemoNextSegmentReason;	// set by the map change that ends one

	// Playback of a v11 recording. Its timeline counts recorded frames, each
	// USERCMD_PER_DEMO_FRAME tics at the recording's tic rate, so it runs on
	// across levels whose game clocks restart.
	bool				renderDemoFrameClock;
	int					renderDemoUserCmdHz;
	idDict				renderDemoPlayHeader;
	idList<renderDemoSegment_t> renderDemoPlaySegments;	// from the trailer, then as met
	int					renderDemoPlaySegment;		// the segment playing, or -1
	idStr				renderDemoPlayMap;			// the playing segment's map
	bool				renderDemoLevelLoadOpen;	// a segment's media load is open

	bool				IsRenderDemoRecording() const { return renderDemoRecordState != RENDER_DEMO_RECORD_IDLE; }
	bool				OpenRenderDemoRecordFile();
	void				BeginRenderDemoSegment( const char *reason );
	void				SuspendRenderDemoRecording();
	void				ContinueRenderDemoAfterMapChange();
	void				PrintRenderDemoRecordStatus();
	void				ResetRenderDemoPlaybackSession();
	bool				ProcessRenderDemoSessionRecord( bool &finished );
	void				CloseRenderDemoLevelLoad();
	int					RenderDemoFramesToMS( int frames ) const;
	bool				SeekRenderDemoSegment( int direction );
	idStr				RenderDemoSegmentLabel() const;

	idUserInterface *	GetActiveMenu();

	void				DispatchCommand( idUserInterface *gui, const char *menuCommand, bool doIngame = true );
	void				PumpApplicationActions( idUserInterface *only = NULL );
	void				MenuEvent( const sysEvent_t *event );
	bool				HandleSaveGameMenuCommand( idCmdArgs &args, int &icmd );
	void				HandleInGameCommands( const char *menuCommand );
	void				HandleMainMenuCommands( const char *menuCommand );
	void				HandleChatMenuCommands( const char *menuCommand );
	void				HandleIntroMenuCommands( const char *menuCommand );
	void				HandleRestartMenuCommands( const char *menuCommand );
	void				HandleMsgCommands( const char *menuCommand );
	void				HandleNoteCommands( const char *menuCommand );
	void				GetSaveGameList( idStrList &fileList, idList<fileTIME_T> &fileTimes );
	void				TakeNotes( const char * p, bool extended = false );
	void				UpdateMPLevelShot( void );

	void				SetSaveGameGuiVars( void );
	void				SetMainMenuGuiVars( bool refreshCatalogs );
	void				SetMainMenuBackgroundMontageGuiVars( void );
	void				PrimeMainMenuGuiResources( void );
	void				SetModsMenuGuiVars( void );
	// The title's MODS dialog: the mods it lists (modsList) and the selected one.
	void				PublishRetainedMods( idUserInterface *gui );
	void				SelectRetainedMod( idUserInterface *gui, int row );
	// After the player dies: the retained restart screen, else the stock one.
	void				OpenRestartMenu();
	// The retained Credits page (guis/menu/credits.q4ui), which the title's
	// CREDITS hands off to: its lines read from the legacy credits windows.
	void				PublishRetainedCredits( idUserInterface *page );
	int					retainedModSelected;
	void				UpdateModsMenuGuiVars( void );
	void				SetMainMenuSkin( void );
	
private:
	bool				BoxDialogSanityCheck( void );
	void				EmitGameAuth( void );
	bool				IsIAmTheDukeActive( void ) const;
	void				DrawIAmTheDukeOverlay( void ) const;
	void				UpdateFramePacingStats( int requestedWaitMsec, int actualWaitMsec, int gameTicsToRun );
	
	typedef enum {
		CDKEY_UNKNOWN,	// need to perform checks on the key
		CDKEY_INVALID,	// that key is wrong
		CDKEY_OK,		// valid
		CDKEY_CHECKING, // legacy state retained for compatibility
		CDKEY_NA		// does not apply, xp key when xp is not present
	} cdKeyState_t;

	static const int	CDKEY_BUF_LEN = 17;
	static const int	CDKEY_AUTH_TIMEOUT = 5000;

	char				cdkey[ CDKEY_BUF_LEN ];
	cdKeyState_t		cdkey_state;
	char				xpkey[ CDKEY_BUF_LEN ];
	cdKeyState_t		xpkey_state;
	int					authEmitTimeout;
	bool				authWaitBox;

	idStr				authMsg;
};

extern idSessionLocal	sessLocal;

#endif /* !__SESSIONLOCAL_H__ */
