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




//#include "../renderer/Image.h"
#include "../bse/BSE_API.h"
#include "RenderDoc.h"
#include <SDL3/SDL.h>

#define	MAX_PRINT_MSG_SIZE	4096
#define MAX_WARNING_LIST	256

void OpenQ4_PrintFramePacingSnapshot( const char *reason );
void OpenQ4_RecordMultiplayerFramePacing( int frameStartMsec );

static const int OPENQ4_ENTITYDEF_MEDIA_CACHE_TOOL_MASK =
	EDITOR_RADIANT |
	EDITOR_MODVIEW |
	EDITOR_AAS |
	EDITOR_SPAWN_GUI |
	EDITOR_DECL_VALIDATING;

typedef enum {
	ERP_NONE,
	ERP_FATAL,						// exit the entire game with a popup window
	ERP_DROP,						// print to console and disconnect from game
	ERP_DISCONNECT					// don't kill server
} errorParm_t;

struct version_s {
			version_s( void ) { idStr::snPrintf( string, sizeof( string ), "%s", OPENQ4_PRODUCT_VERSION_FULL ); }
	char	string[256];
} version;

struct build_info_s {
#if defined( _DEBUG )
			build_info_s( void ) { idStr::snPrintf( string, sizeof( string ), "%s (debug, %s, %s %s)", OPENQ4_PRODUCT_VERSION_FULL, BUILD_STRING, __DATE__, __TIME__ ); }
#else
			build_info_s( void ) { idStr::snPrintf( string, sizeof( string ), "%s (%s, %s %s)", OPENQ4_PRODUCT_VERSION_FULL, BUILD_STRING, __DATE__, __TIME__ ); }
#endif
	char	string[256];
} buildInfo;

idCVar com_version( "si_version", version.string, CVAR_SYSTEM|CVAR_ROM|CVAR_SERVERINFO, "engine version" );
idCVar com_buildInfo( "com_buildInfo", buildInfo.string, CVAR_SYSTEM|CVAR_ROM, "detailed engine build information" );
idCVar com_skipRenderer( "com_skipRenderer", "0", CVAR_BOOL|CVAR_SYSTEM, "skip the renderer completely" );
idCVar com_machineSpec( "com_machineSpec", "-1", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM, "hardware classification, -1 = not detected, 0 = low quality, 1 = medium quality, 2 = high quality, 3 = ultra quality" );
idCVar com_purgeAll( "com_purgeAll", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM, "purge everything between level loads" );
idCVar com_WriteSingleDeclFile( "com_WriteSingleDeclFile", "0", CVAR_SYSTEM | CVAR_BOOL, "write a packed decl file after startup or map loads; use com_singleDeclFileWriteMode for OpenQ4 or exact-retail game-type coverage" );
idCVar com_memoryMarker( "com_memoryMarker", "-1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_INIT, "used as a marker for memory stats" );
idCVar com_preciseTic( "com_preciseTic", "1", CVAR_BOOL|CVAR_SYSTEM, "run one game tick every async thread update" );
idCVar com_asyncInput( "com_asyncInput", "0", CVAR_BOOL|CVAR_SYSTEM, "sample input from the async thread" );
#define ASYNCSOUND_INFO "0: mix sound inline, 1: memory mapped async mix, 2: callback mixing, 3: write async mix"
#if defined( MACOS_X )
idCVar com_asyncSound( "com_asyncSound", "2", CVAR_INTEGER|CVAR_SYSTEM|CVAR_ROM, ASYNCSOUND_INFO );
#elif defined( __linux__ )
idCVar com_asyncSound( "com_asyncSound", "3", CVAR_INTEGER|CVAR_SYSTEM|CVAR_ROM, ASYNCSOUND_INFO );
#else
idCVar com_asyncSound( "com_asyncSound", "1", CVAR_INTEGER|CVAR_SYSTEM, ASYNCSOUND_INFO, 0, 1 );
#endif
idCVar com_productionMode("com_productionMode", "0", CVAR_SYSTEM | CVAR_BOOL, "0 - no special behavior, 1 - building a production build, 2 - running a production build");

idCVar com_forceGenericSIMD( "com_forceGenericSIMD", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "force generic platform independent SIMD" );
idCVar com_developer( "developer", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "developer mode" );
idCVar con_allowConsole( "con_allowConsole", "1", CVAR_BOOL | CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "allow toggling the console with the tilde key; set to 0 to require Ctrl+Alt+Tilde" );
idCVar com_speeds( "com_speeds", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "show engine timings" );
idCVar com_showFPS( "com_showFPS", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_ARCHIVE|CVAR_NOCHEAT, "show frames rendered per second" );
idCVar com_maxfps( "com_maxfps", "240", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "presentation frame cap, 0 = uncapped", 0, 1000, idCmdSystem::ArgCompletion_Integer<0,1000> );
idCVar com_showFramePacing( "com_showFramePacing", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_NOCHEAT, "show frame pacing diagnostics: 1 = HUD overlay, 2 = overlay plus console logging", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar com_showMemoryUsage( "com_showMemoryUsage", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "show total and per frame memory usage" );
idCVar com_showAsyncStats( "com_showAsyncStats", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "show async network stats" );
idCVar com_showSoundDecoders( "com_showSoundDecoders", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "show sound decoders" );
idCVar com_timestampPrints( "com_timestampPrints", "0", CVAR_SYSTEM, "print time with each console print, 1 = msec, 2 = sec", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar com_timescale( "timescale", "1", CVAR_SYSTEM | CVAR_FLOAT, "scales the time", 0.1f, 10.0f );
idCVar com_logFile( "logFile", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "1 = buffer log, 2 = flush after each print", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar com_logFileName( "logFileName", "qconsole.log", CVAR_SYSTEM | CVAR_NOCHEAT, "name of log file, if empty, qconsole.log will be used" );
idCVar com_autoScreenshot( "com_autoScreenshot", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "take a one-shot screenshot after map load (diagnostic)" );
idCVar com_makingBuild( "com_makingBuild", "0", CVAR_BOOL | CVAR_SYSTEM, "1 when making a build" );
idCVar com_updateLoadSize( "com_updateLoadSize", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "update the load size after loading a map" );
idCVar com_videoRam( "com_videoRam", "64", CVAR_INTEGER | CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ARCHIVE, "holds the last amount of detected video ram" );
idCVar com_activeGameModule( "com_activeGameModule", "", CVAR_SYSTEM, "active game module (game_sp/game_mp)" );
idCVar com_nextGameModule( "com_nextGameModule", "", CVAR_SYSTEM, "internal one-shot game module override for reloadEngine" );
idCVar com_platformProfile( "com_platformProfile", "default", CVAR_SYSTEM | CVAR_INIT, "startup platform profile (default or steamdeck)" );

idCVar com_product_lang_ext( "com_product_lang_ext", "1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE, "Extension to use when creating language files." );
idCVar r_skipGlowOverlay( "r_skipGlowOverlay", "0", CVAR_ARCHIVE | CVAR_RENDERER, "skip glow overlays when non-zero" );

static void Common_MigrateLegacyConsoleAllowCVar( void ) {
	idCVar *legacyAllowConsole = cvarSystem->Find( "com_allowConsole" );
	if ( legacyAllowConsole == NULL || ( legacyAllowConsole->GetFlags() & CVAR_STATIC ) != 0 ) {
		return;
	}

	if ( idStr::Cmp( con_allowConsole.GetString(), "1" ) != 0 ) {
		return;
	}

	if ( idStr::Cmp( legacyAllowConsole->GetString(), con_allowConsole.GetString() ) == 0 ) {
		return;
	}

	common->Printf( "Migrating legacy console config: copying com_allowConsole to con_allowConsole (%s)\n", legacyAllowConsole->GetString() );
	con_allowConsole.SetString( legacyAllowConsole->GetString() );
}

static int commonLastPresentationCap = -1;
static double commonNextPresentationFrameClock = 0.0;
static bool commonNextPresentationFrameValid = false;

static void Common_ResetPresentationThrottle( void ) {
	commonNextPresentationFrameClock = 0.0;
	commonNextPresentationFrameValid = false;
}

static double Common_GetPresentationClockNow( void ) {
	return static_cast<double>( SDL_GetPerformanceCounter() );
}

static double Common_GetPresentationClockUnitsPerSecond( void ) {
	return static_cast<double>( SDL_GetPerformanceFrequency() );
}

static int Common_GetRequestedPresentationCap( void ) {
	int cap = Max( 0, com_maxfps.GetInteger() );

#if defined( _WIN32 )
	// Hidden/minimized windows should not spin uncapped when presentation is decoupled.
	if ( !Sys_IsWindowVisible() && ( cap == 0 || cap > 60 ) ) {
		cap = 60;
	}
#endif

	return cap;
}

static void Common_ThrottlePresentationFrame( void ) {
	const int cap = Common_GetRequestedPresentationCap();

	if ( cap <= 0 ) {
		commonLastPresentationCap = cap;
		Common_ResetPresentationThrottle();
		return;
	}

	const double clockUnitsPerSecond = Common_GetPresentationClockUnitsPerSecond();
	if ( clockUnitsPerSecond <= 0.0 ) {
		commonLastPresentationCap = cap;
		Common_ResetPresentationThrottle();
		return;
	}

	const double nowClock = Common_GetPresentationClockNow();
	const double frameClockUnits = clockUnitsPerSecond / static_cast<double>( cap );
	const double frameMsec = 1000.0 / static_cast<double>( cap );
	if ( cap != commonLastPresentationCap || !commonNextPresentationFrameValid ) {
		commonLastPresentationCap = cap;
		commonNextPresentationFrameClock = nowClock + frameClockUnits;
		commonNextPresentationFrameValid = true;
		return;
	}

	// A long hitch or debugger break should restart pacing from "now" instead of sleeping forever.
	if ( nowClock > commonNextPresentationFrameClock + frameClockUnits * 4.0
		|| nowClock + frameClockUnits * 4.0 < commonNextPresentationFrameClock ) {
		commonNextPresentationFrameClock = nowClock;
	}

	// Sleep away the bulk of the frame period, then use the high-resolution counter
	// for the last slice so low frame caps don't pick up an extra scheduler quantum.
	const double sleepSlackMsec = idMath::ClampFloat( 0.5f, 1.5f, static_cast<float>( frameMsec * 0.125f ) );
	while ( true ) {
		const double remainingClockUnits = commonNextPresentationFrameClock - Common_GetPresentationClockNow();
		if ( remainingClockUnits <= 0.0 ) {
			break;
		}

		const double remainingMsec = ( remainingClockUnits * 1000.0 ) / clockUnitsPerSecond;
		if ( remainingMsec <= sleepSlackMsec ) {
			continue;
		}

		const int sleepMsec = Max( 1, static_cast<int>( idMath::Floor( remainingMsec - sleepSlackMsec ) ) );
		Sys_Sleep( sleepMsec );
	}

	commonNextPresentationFrameClock += frameClockUnits;
}

void OpenQ4_BeginPresentationFrame( void ) {
	if ( idAsyncNetwork::serverDedicated.GetInteger() == 1 ) {
		Common_ResetPresentationThrottle();
	} else {
		Common_ThrottlePresentationFrame();
	}

	com_frameRealTime = Sys_Milliseconds();
}

// com_speeds times
int				time_gameFrame;
int				time_gameDraw;
int				time_frontend;			// renderSystem frontend time
int				time_backend;			// renderSystem backend time

int				com_frameTime;			// simulation time for the current frame in milliseconds
int				com_frameRealTime;		// presentation time for the current frame in milliseconds
int				com_frameNumber;		// variable frame number
volatile int	com_ticNumber;			// 60 hz tics
int				com_editors;			// currently opened editor(s)
bool			com_editorActive;		//  true if an editor has focus

/*
==================
OpenQ4_GetActiveToolFlags
==================
*/
int OpenQ4_GetActiveToolFlags( int flags ) {
	if ( flags == EDITOR_ALL ) {
		return com_editors;
	}

	return com_editors & flags;
}

/*
==================
OpenQ4_IsAnyToolActive
==================
*/
bool OpenQ4_IsAnyToolActive( void ) {
	return com_editorActive || OpenQ4_GetActiveToolFlags( EDITOR_ALL ) != 0;
}

/*
==================
OpenQ4_ToolPrint
==================
*/
void OpenQ4_ToolPrint( const char *text ) {
	bool toolPrinted = false;

	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

#ifdef ID_ALLOW_TOOLS
	if ( OpenQ4_GetActiveToolFlags( EDITOR_DECL ) != 0 ) {
		toolPrinted = DeclBrowserPrint( text ) || toolPrinted;
	}

	if ( OpenQ4_GetActiveToolFlags( EDITOR_DEBUGGER ) != 0 ) {
		DebuggerServerPrint( text );
		toolPrinted = true;
	}

	if ( OpenQ4_GetActiveToolFlags( EDITOR_RADIANT ) != 0 ) {
		RadiantPrint( text );
		toolPrinted = true;
	}

	if ( OpenQ4_GetActiveToolFlags( EDITOR_MATERIAL ) != 0 ) {
		MaterialEditorPrintConsole( text );
		toolPrinted = true;
	}
#endif

	if ( !toolPrinted ) {
		common->Printf( "%s", text );
	}
}

/*
==================
OpenQ4_ShouldCacheEntityDefMedia
==================
*/
bool OpenQ4_ShouldCacheEntityDefMedia( bool noCaching ) {
	return !noCaching && OpenQ4_GetActiveToolFlags( OPENQ4_ENTITYDEF_MEDIA_CACHE_TOOL_MASK ) == 0;
}

extern glconfig_t	glConfig;

#ifdef _WIN32
HWND			com_hwndMsg = NULL;
bool			com_outputMsg = false;
unsigned int	com_msgID = -1;
#endif

#ifdef __DOOM_DLL__
idGame *		game = NULL;
idGameEdit *	gameEdit = NULL;
#endif

class rvBSEManagerDisabled : public rvBSEManager {
public:
	virtual bool				Init( void ) { return true; }
	virtual bool				Shutdown( void ) {
		for ( int i = 0; i < traceModels.Num(); ++i ) {
			delete traceModels[i];
		}
		traceModels.Clear();
		return true;
	}

	virtual bool				PlayEffect( class rvRenderEffectLocal *def, float time ) { return false; }
	virtual bool				ServiceEffect( class rvRenderEffectLocal *def, float ownerTime, float presentationTime ) { return false; }
	virtual idRenderModel *		RenderEffect( class rvRenderEffectLocal *def, const struct viewDef_s *view ) { return NULL; }
	virtual void				StopEffect( rvRenderEffectLocal *def ) { }
	virtual bool				IsEffectStopped( const rvRenderEffectLocal *def ) const { return false; }
	virtual void				SetEffectStopped( rvRenderEffectLocal *def, bool stopped ) { }
	virtual void				FreeEffect( rvRenderEffectLocal *def ) { }
	virtual float				EffectDuration( const rvRenderEffectLocal *def ) { return 0.0f; }

	virtual bool				CheckDefForSound( const renderEffect_t *def ) { return false; }

	virtual void				BeginLevelLoad( void ) { }
	virtual void				EndLevelLoad( void ) { }

	virtual void				StartFrame( void ) { }
	virtual void				EndFrame( void ) { }
	virtual bool				Filtered( const char *name, effectCategory_t category ) { return true; }

	virtual void				UpdateRateTimes( void ) { }
	virtual bool				CanPlayRateLimited( effectCategory_t category ) { return false; }

	virtual int					AddTraceModel( idTraceModel *model ) {
		traceModels.Append( model );
		return traceModels.Num() - 1;
	}

	virtual idTraceModel *		GetTraceModel( int index ) {
		if ( index < 0 || index >= traceModels.Num() ) {
			return NULL;
		}
		return traceModels[index];
	}

	virtual void				FreeTraceModel( int index ) {
		if ( index < 0 || index >= traceModels.Num() ) {
			return;
		}
		delete traceModels[index];
		traceModels[index] = NULL;
	}

private:
	idList<idTraceModel *>		traceModels;
};

static rvBSEManagerDisabled	bseDisabledLocal;
rvBSEManager *	bse = &bseDisabledLocal;
rvDeclEffectEdit *declEffectEdit = NULL;
BSE_AllocDeclEffect_t bseAllocDeclEffect = NULL;

// writes si_version to the config file - in a kinda obfuscated way
//#define ID_WRITE_VERSION

class idCommonLocal : public idCommon {
public:
								idCommonLocal( void );

	virtual void				Init( int argc, const char **argv, const char *cmdline );
	virtual void				Shutdown( void );
	virtual void				Quit( void );
	virtual bool				IsInitialized( void ) const;
	virtual void				Frame( void );
	virtual void				GUIFrame( bool execCmd, bool network );
	virtual void				Async( void );
	virtual void				StartupVariable( const char *match, bool once );
	virtual int					GetUserCmdHz( void ) const;
	virtual int					GetUserCmdMSec( void ) const;
	virtual int					GetUserCmdTime( int ticNumber ) const;
	virtual int					GetUserCmdDeltaMsec( int ticNumber ) const;
	virtual int					GetFrameTime( void ) const;
	virtual bool				IsRenderableGameFrame( void ) const;
	virtual void				SetRenderableGameFrame( bool in );
	virtual const char *		GetErrorMessage( void ) const;
	virtual void				InitTool( const int tool, const idDict *dict );
	virtual bool				IsToolActive( void ) const;
	virtual rvISourceControl *	GetSourceControl( void );
	virtual void				ActivateTool( bool active );
	virtual void				WriteConfigToFile( const char *filename );
	virtual void				WriteFlaggedCVarsToFile( const char *filename, int flags, const char *setCmd );
	virtual void				ModViewThink( void );
	virtual void				RunAlwaysThinkGUIs( int time );
	virtual void				DebuggerCheckBreakpoint( idInterpreter *interpreter, idProgram *program, int instructionPointer );
	virtual bool				DoingDeclValidation( void );
	virtual void				SetCrashReportAutoSendString( const char *psString );
	virtual void				LoadToolsDLL( void );
	virtual void				UnloadToolsDLL( void );
	virtual void				BeginRedirect( char *buffer, int buffersize, void (*flush)( const char * ) );
	virtual void				EndRedirect( void );
	virtual void				SetRefreshOnPrint( bool set );
	virtual void				Printf( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual void				VPrintf( const char *fmt, va_list arg );
	virtual void				PrintFramePacingSnapshot( const char *reason );
	virtual void				DPrintf( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual void				Warning( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual void				DWarning( const char *fmt, ...) id_attribute((format(printf,2,3)));
	virtual void				PrintWarnings( void );
	virtual void				ClearWarnings( const char *reason );
	virtual void				Error( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual void				FatalError( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual const idLangDict *	GetLanguageDict( void );

	virtual const char *		KeysFromBinding( const char *bind );
	virtual const char *		BindingFromKey( const char *key );

	virtual int					ButtonState( int key );
	virtual int					KeyState( int key );

	void						InitGame( void );
	void						ShutdownGame( bool reloading );

	// localization
	void						InitLanguageDict( void );
	void						LocalizeGui( const char *fileName, idLangDict &langDict );
	void						LocalizeMapData( const char *fileName, idLangDict &langDict );
	void						LocalizeSpecificMapData( const char *fileName, idLangDict &langDict, const idLangDict &replaceArgs );

	void						SetMachineSpec( void );

private:
	void						InitCommands( void );
	void						InitRenderSystem( void );
	void						InitSIMD( void );
	bool						AddStartupCommands( void );
	void						ParseCommandLine( int argc, const char **argv );
	void						ClearCommandLine( void );
	bool						SafeMode( void );

static idStr Common_BuildPlatformProfileConfigName( const char *profileName ) {
	idStr profile = profileName;
	idStr sanitized;

	for ( int i = 0; i < profile.Length(); ++i ) {
		const char c = profile[i];
		if ( ( c >= 'a' && c <= 'z' ) ||
			( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' ) ||
			c == '_' || c == '-' ) {
			sanitized.Append( c );
		}
	}
	sanitized.ToLower();

	if ( sanitized.Length() == 0 || sanitized.Icmp( "default" ) == 0 ) {
		return "";
	}
	if ( sanitized.Icmp( "steamdeck" ) != 0 ) {
		return "";
	}

	return va( "openq4_profile_%s.cfg", sanitized.c_str() );
}
	void						CheckToolMode( void );
	void						CloseLogFile( void );
	void						WriteConfiguration( void );
	void						DumpWarnings( void );
	void						SingleAsyncTic( void );
	void						AttachBSE( void );
	void						DetachBSE( void );
	void						LoadGameDLL( void );
	void						UnloadGameDLL( void );
	void						PrintLoadingMessage( const char *msg );
	void						FilterLangList( idStrList* list, idStr lang );

	bool						com_fullyInitialized;
	bool						com_renderableGameFrame;
	bool						com_refreshOnPrint;		// update the screen every print for dmap
	int							com_errorEntered;		// 0, ERP_DROP, etc
	bool						com_shuttingDown;

	idFile *					logFile;

	char						errorMessage[MAX_PRINT_MSG_SIZE];

	char *						rd_buffer;
	int							rd_buffersize;
	void						(*rd_flush)( const char *buffer );

	idStr						warningCaption;
	idStrList					warningList;
	idStrList					errorList;

	intptr_t					gameDLL;

	idLangDict					languageDict;

#ifdef ID_WRITE_VERSION
	idCompressor *				config_compressor;
#endif
};

idCommonLocal	commonLocal;
idCommon *		common = &commonLocal;


/*
==================
idCommonLocal::idCommonLocal
==================
*/
idCommonLocal::idCommonLocal( void ) {
	com_fullyInitialized = false;
	com_renderableGameFrame = true;
	com_refreshOnPrint = false;
	com_errorEntered = 0;
	com_shuttingDown = false;

	logFile = NULL;

	strcpy( errorMessage, "" );

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;

	gameDLL = 0;

#ifdef ID_WRITE_VERSION
	config_compressor = NULL;
#endif
}

/*
==================
idCommonLocal::BeginRedirect
==================
*/
void idCommonLocal::BeginRedirect( char *buffer, int buffersize, void (*flush)( const char *) ) {
	if ( !buffer || !buffersize || !flush ) {
		return;
	}
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

/*
==================
idCommonLocal::EndRedirect
==================
*/
void idCommonLocal::EndRedirect( void ) {
	if ( rd_flush && rd_buffer[ 0 ] ) {
		rd_flush( rd_buffer );
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

#ifdef _WIN32

/*
==================
EnumWindowsProc
==================
*/
BOOL CALLBACK EnumWindowsProc( HWND hwnd, LPARAM lParam ) {
	char buff[1024];

	::GetWindowText( hwnd, buff, sizeof( buff ) );
	if ( idStr::Icmpn( buff, EDITOR_WINDOWTEXT, strlen( EDITOR_WINDOWTEXT ) ) == 0 ) {
		com_hwndMsg = hwnd;
		return FALSE;
	}
	return TRUE;
}

/*
==================
FindEditor
==================
*/
bool FindEditor( void ) {
	com_hwndMsg = NULL;
	EnumWindows( EnumWindowsProc, 0 );
	return !( com_hwndMsg == NULL );
}

#endif

/*
==================
idCommonLocal::CloseLogFile
==================
*/
void idCommonLocal::CloseLogFile( void ) {
	if ( logFile ) {
		com_logFile.SetBool( false ); // make sure no further VPrintf attempts to open the log file again
		fileSystem->CloseFile( logFile );
		logFile = NULL;
	}
}

/*
==================
idCommonLocal::SetRefreshOnPrint
==================
*/
void idCommonLocal::SetRefreshOnPrint( bool set ) {
	com_refreshOnPrint = set;
}

/*
==================
idCommonLocal::VPrintf

A raw string should NEVER be passed as fmt, because of "%f" type crashes.
==================
*/
void idCommonLocal::VPrintf( const char *fmt, va_list args ) {
	char		msg[MAX_PRINT_MSG_SIZE];
	int			timeLength;
	static bool	logFileFailed = false;

	// if the cvar system is not initialized
	if ( !cvarSystem->IsInitialized() ) {
		return;
	}

	// optionally put a timestamp at the beginning of each print,
	// so we can see how long different init sections are taking
	if ( com_timestampPrints.GetInteger() ) {
		int	t = Sys_Milliseconds();
		if ( com_timestampPrints.GetInteger() == 1 ) {
			t /= 1000;
		}
		sprintf( msg, "[%i]", t );
		timeLength = strlen( msg );
	} else {
		timeLength = 0;
	}

	// don't overflow
	if ( idStr::vsnPrintf( msg+timeLength, MAX_PRINT_MSG_SIZE-timeLength-1, fmt, args ) < 0 ) {
		msg[sizeof(msg)-2] = '\n'; msg[sizeof(msg)-1] = '\0'; // avoid output garbling
		Sys_Printf( "idCommon::VPrintf: truncated to %d characters\n", strlen(msg)-1 );
	}

	if ( rd_buffer ) {
		if ( (int)( strlen( msg ) + strlen( rd_buffer ) ) > ( rd_buffersize - 1 ) ) {
			rd_flush( rd_buffer );
			*rd_buffer = 0;
		}
		strcat( rd_buffer, msg );
		return;
	}

	// echo to console buffer
	console->Print( msg );

	// remove any color codes
	//idStr::RemoveColors( msg );

	// echo to dedicated console and early console
	Sys_Printf( "%s", msg );

	// print to script debugger server
	// DebuggerServerPrint( msg );

#if 0	// !@#
#if defined(_DEBUG) && defined(WIN32)
	if ( strlen( msg ) < 512 ) {
		TRACE( msg );
	}
#endif
#endif

	// logFile
	if ( com_logFile.GetInteger() && !logFileFailed && fileSystem->IsInitialized() ) {
		static bool recursing;

		if ( !logFile && !recursing ) {
			struct tm *newtime;
			ID_TIME_T aclock;
			idStr fileName = com_logFileName.GetString()[0] ? com_logFileName.GetString() : "qconsole.log";
			if ( fileName.Icmp( "auto" ) == 0 ) {
				fileName = "logs/openq4_%Y%m%d_%H%M%S.log";
			}

			char resolvedFileName[MAX_OSPATH];
			const char *fileNameToOpen = fileName.c_str();

			time( &aclock );
			newtime = localtime( &aclock );
			if ( newtime && fileName.Find( '%' ) != -1 ) {
				if ( strftime( resolvedFileName, sizeof( resolvedFileName ), fileName.c_str(), newtime ) > 0 ) {
					fileNameToOpen = resolvedFileName;
				}
			}

			// fileSystem->OpenFileWrite can cause recursive prints into here
			recursing = true;

			logFile = fileSystem->OpenFileWrite( fileNameToOpen );
			if ( !logFile ) {
				logFileFailed = true;
				FatalError( "failed to open log file '%s'\n", fileNameToOpen );
			}

			recursing = false;

			if ( com_logFile.GetInteger() > 1 ) {
				// force it to not buffer so we get valid
				// data even if we are crashing
				logFile->ForceFlush();
			}

			Printf( "log file '%s' opened on %s\n", fileNameToOpen, asctime( newtime ) );
		}
		if ( logFile ) {
			logFile->Write( msg, strlen( msg ) );
			logFile->Flush();	// ForceFlush doesn't help a whole lot
		}
	}

	// don't trigger any updates if we are in the process of doing a fatal error
	if ( com_errorEntered != ERP_FATAL ) {
		// update the console if we are in a long-running command, like dmap
		if ( com_refreshOnPrint ) {
			session->UpdateScreen();
		}

		// let session redraw the animated loading screen if necessary
		session->PacifierUpdate();
	}

#ifdef _WIN32

	if ( com_outputMsg ) {
		if ( com_msgID == -1 ) {
			com_msgID = ::RegisterWindowMessage( DMAP_MSGID );
			if ( !FindEditor() ) {
				com_outputMsg = false;
			} else {
				Sys_ShowWindow( false );
			}
		}
		if ( com_hwndMsg ) {
			ATOM atom = ::GlobalAddAtom( msg );
			::PostMessage( com_hwndMsg, com_msgID, 0, static_cast<LPARAM>(atom) );
		}
	}

#endif
}

/*
==================
idCommonLocal::Printf

Both client and server can use this, and it will output to the appropriate place.

A raw string should NEVER be passed as fmt, because of "%f" type crashers.
==================
*/
void idCommonLocal::Printf( const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	VPrintf( fmt, argptr );
	va_end( argptr );
}

/*
==================
idCommonLocal::PrintFramePacingSnapshot
==================
*/
void idCommonLocal::PrintFramePacingSnapshot( const char *reason ) {
	OpenQ4_PrintFramePacingSnapshot( reason );
}

/*
==================
idCommonLocal::DPrintf

prints message that only shows up if the "developer" cvar is set
==================
*/
void idCommonLocal::DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAX_PRINT_MSG_SIZE];
		
	if ( !cvarSystem->IsInitialized() || !com_developer.GetBool() ) {
		return;			// don't confuse non-developers with techie stuff...
	}

	va_start( argptr, fmt );
	idStr::vsnPrintf( msg, sizeof(msg), fmt, argptr );
	va_end( argptr );
	msg[sizeof(msg)-1] = '\0';
	
	// never refresh the screen, which could cause reentrency problems
	bool temp = com_refreshOnPrint;
	com_refreshOnPrint = false;

	Printf( S_COLOR_RED"%s", msg );

	com_refreshOnPrint = temp;
}

/*
==================
idCommonLocal::DWarning

prints warning message in yellow that only shows up if the "developer" cvar is set
==================
*/
void idCommonLocal::DWarning( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAX_PRINT_MSG_SIZE];
		
	if ( !com_developer.GetBool() ) {
		return;			// don't confuse non-developers with techie stuff...
	}

	va_start( argptr, fmt );
	idStr::vsnPrintf( msg, sizeof(msg), fmt, argptr );
	va_end( argptr );
	msg[sizeof(msg)-1] = '\0';

	Printf( S_COLOR_YELLOW"WARNING: %s\n", msg );
}

/*
==================
idCommonLocal::Warning

prints WARNING %s and adds the warning message to a queue to be printed later on
==================
*/
void idCommonLocal::Warning( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAX_PRINT_MSG_SIZE];
		
	va_start( argptr, fmt );
	idStr::vsnPrintf( msg, sizeof(msg), fmt, argptr );
	va_end( argptr );
	msg[sizeof(msg)-1] = 0;

	Printf( S_COLOR_YELLOW "WARNING: " S_COLOR_RED "%s\n", msg );

	if ( warningList.Num() < MAX_WARNING_LIST ) {
		warningList.AddUnique( msg );
	}
}

/*
==================
idCommonLocal::PrintWarnings
==================
*/
void idCommonLocal::PrintWarnings( void ) {
	int i;

	if ( !warningList.Num() ) {
		return;
	}

	warningList.Sort();

	Printf( "------------- Warnings ---------------\n" );
	Printf( "during %s...\n", warningCaption.c_str() );

	for ( i = 0; i < warningList.Num(); i++ ) {
		Printf( S_COLOR_YELLOW "WARNING: " S_COLOR_RED "%s\n", warningList[i].c_str() );
	}
	if ( warningList.Num() ) {
		if ( warningList.Num() >= MAX_WARNING_LIST ) {
			Printf( "more than %d warnings\n", MAX_WARNING_LIST );
		} else {
			Printf( "%d warnings\n", warningList.Num() );
		}
	}
}

/*
==================
idCommonLocal::ClearWarnings
==================
*/
void idCommonLocal::ClearWarnings( const char *reason ) {
	warningCaption = reason;
	warningList.Clear();
}

/*
==================
idCommonLocal::DumpWarnings
==================
*/
void idCommonLocal::DumpWarnings( void ) {
	int			i;
	idFile		*warningFile;

	if ( !warningList.Num() ) {
		return;
	}

	warningFile = fileSystem->OpenFileWrite( "warnings.txt", "fs_savepath" );
	if ( warningFile ) {

		warningFile->Printf( "------------- Warnings ---------------\n\n" );
		warningFile->Printf( "during %s...\n", warningCaption.c_str() );
		warningList.Sort();
		for ( i = 0; i < warningList.Num(); i++ ) {
		//	warningList[i].RemoveColors();
			warningFile->Printf( "WARNING: %s\n", warningList[i].c_str() );
		}
		if ( warningList.Num() >= MAX_WARNING_LIST ) {
			warningFile->Printf( "\nmore than %d warnings!\n", MAX_WARNING_LIST );
		} else {
			warningFile->Printf( "\n%d warnings.\n", warningList.Num() );
		}

		warningFile->Printf( "\n\n-------------- Errors ---------------\n\n" );
		errorList.Sort();
		for ( i = 0; i < errorList.Num(); i++ ) {
			//errorList[i].RemoveColors();
			warningFile->Printf( "ERROR: %s", errorList[i].c_str() );
		}

		warningFile->ForceFlush();

		fileSystem->CloseFile( warningFile );

#if defined(_WIN32) && !defined(_DEBUG)
		idStr	osPath;
		osPath = fileSystem->RelativePathToOSPath( "warnings.txt", "fs_savepath" );
		WinExec( va( "Notepad.exe %s", osPath.c_str() ), SW_SHOW );
#endif
	}
}

/*
==================
idCommonLocal::Error
==================
*/
void idCommonLocal::Error( const char *fmt, ... ) {
	va_list		argptr;
	static int	lastErrorTime;
	static int	errorCount;
	int			currentTime;

	int code = ERP_DROP;

	// always turn this off after an error
	com_refreshOnPrint = false;

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( cvarSystem->GetCVarInteger( "fs_copyfiles" ) ) {
		code = ERP_FATAL;
	}

	// if we don't have GL running, make it a fatal error
	if ( !renderSystem->IsOpenGLRunning() ) {
		code = ERP_FATAL;
	}

	// if we got a recursive error, make it fatal
	if ( com_errorEntered ) {
		// if we are recursively erroring while exiting
		// from a fatal error, just kill the entire
		// process immediately, which will prevent a
		// full screen rendering window covering the
		// error dialog
		if ( com_errorEntered == ERP_FATAL ) {
			Sys_Quit();
		}
		code = ERP_FATAL;
	}

	// if we are getting a solid stream of ERP_DROP, do an ERP_FATAL
	currentTime = Sys_Milliseconds();
	if ( currentTime - lastErrorTime < 100 ) {
		if ( ++errorCount > 3 ) {
			code = ERP_FATAL;
		}
	} else {
		errorCount = 0;
	}
	lastErrorTime = currentTime;

	com_errorEntered = code;

	va_start (argptr,fmt);
	idStr::vsnPrintf( errorMessage, sizeof(errorMessage), fmt, argptr );
	va_end (argptr);
	errorMessage[sizeof(errorMessage)-1] = '\0';

	// copy the error message to the clip board
	Sys_SetClipboardData( errorMessage );

	// add the message to the error list
	errorList.AddUnique( errorMessage );

	// Dont shut down the session for gui editor or debugger
	if ( !( com_editors & ( EDITOR_GUI | EDITOR_DEBUGGER ) ) ) {
		session->Stop();
	}

	if ( code == ERP_DISCONNECT ) {
		com_errorEntered = 0;
		throw idException( errorMessage );
	// The gui editor doesnt want thing to com_error so it handles exceptions instead
	} else if( com_editors & ( EDITOR_GUI | EDITOR_DEBUGGER ) ) {
		com_errorEntered = 0;
		throw idException( errorMessage );
	} else if ( code == ERP_DROP ) {
		Printf( "********************\nERROR: %s\n********************\n", errorMessage );
		com_errorEntered = 0;
		throw idException( errorMessage );
	} else {
		Printf( "********************\nERROR: %s\n********************\n", errorMessage );
	}

	// Only attempt a renderer restart fallback when GL is actually live.
	if ( renderSystem->IsOpenGLRunning() && cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart partial windowed\n" );
	}

	Shutdown();

	Sys_Error( "%s", errorMessage );
}

/*
==================
idCommonLocal::FatalError

Dump out of the game to a system dialog
==================
*/
void idCommonLocal::FatalError( const char *fmt, ... ) {
	va_list		argptr;

	// if we got a recursive error, make it fatal
	if ( com_errorEntered ) {
		// if we are recursively erroring while exiting
		// from a fatal error, just kill the entire
		// process immediately, which will prevent a
		// full screen rendering window covering the
		// error dialog

		Sys_Printf( "FATAL: recursed fatal error:\n%s\n", errorMessage );

		va_start( argptr, fmt );
		idStr::vsnPrintf( errorMessage, sizeof(errorMessage), fmt, argptr );
		va_end( argptr );
		errorMessage[sizeof(errorMessage)-1] = '\0';

		Sys_Printf( "%s\n", errorMessage );

		// write the console to a log file?
		Sys_Quit();
	}
	com_errorEntered = ERP_FATAL;

	va_start( argptr, fmt );
	idStr::vsnPrintf( errorMessage, sizeof(errorMessage), fmt, argptr );
	va_end( argptr );
	errorMessage[sizeof(errorMessage)-1] = '\0';

	// Only attempt a renderer restart fallback when GL is actually live.
	if ( renderSystem->IsOpenGLRunning() && cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart partial windowed\n" );
	}

	Sys_SetFatalError( errorMessage );

	Shutdown();

	Sys_Error( "%s", errorMessage );
}

/*
==================
idCommonLocal::Quit
==================
*/
void idCommonLocal::Quit( void ) {

#ifdef ID_ALLOW_TOOLS
	if ( com_editors & EDITOR_RADIANT ) {
		RadiantInit();
		return;
	}
#endif

	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		Shutdown();
	}

	Sys_Quit();
}


/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters separate the commandLine string into multiple console
command lines.

All of these are valid:

doom +set test blah +map test
doom set test blah+map test
doom set test blah + map test

============================================================================
*/

#define		MAX_CONSOLE_LINES	32
int			com_numConsoleLines;
idCmdArgs	com_consoleLines[MAX_CONSOLE_LINES];

int Com_GetNumStartupCommandLines( void ) {
	return com_numConsoleLines;
}

const idCmdArgs *Com_GetStartupCommandLine( int index ) {
	if ( index < 0 || index >= com_numConsoleLines ) {
		return NULL;
	}
	return &com_consoleLines[ index ];
}

/*
==================
idCommonLocal::ParseCommandLine
==================
*/
void idCommonLocal::ParseCommandLine( int argc, const char **argv ) {
	int i;
	bool droppedCommands;
	bool droppingCurrentCommand;

	com_numConsoleLines = 0;
	droppedCommands = false;
	droppingCurrentCommand = false;
	// API says no program path
	for ( i = 0; i < argc; i++ ) {
		if ( argv[ i ][ 0 ] == '+' ) {
			droppingCurrentCommand = false;
			if ( com_numConsoleLines >= MAX_CONSOLE_LINES ) {
				droppedCommands = true;
				droppingCurrentCommand = true;
				continue;
			}
			com_consoleLines[ com_numConsoleLines ].Clear();
			com_consoleLines[ com_numConsoleLines ].AppendArg( argv[ i ] + 1 );
			com_numConsoleLines++;
		} else {
			if ( droppingCurrentCommand ) {
				continue;
			}
			if ( !com_numConsoleLines ) {
				if ( com_numConsoleLines >= MAX_CONSOLE_LINES ) {
					droppedCommands = true;
					continue;
				}
				com_numConsoleLines++;
				com_consoleLines[ com_numConsoleLines - 1 ].Clear();
			}
			com_consoleLines[ com_numConsoleLines-1 ].AppendArg( argv[ i ] );
		}
	}

	if ( droppedCommands ) {
		Printf( "^3WARNING: command line contains more than %d startup commands; extra commands were ignored\n", MAX_CONSOLE_LINES );
	}
}

/*
==================
idCommonLocal::ClearCommandLine
==================
*/
void idCommonLocal::ClearCommandLine( void ) {
	com_numConsoleLines = 0;
}

/*
==================
idCommonLocal::SafeMode

Check for "safe" on the command line, which will
skip loading of config file (DoomConfig.cfg)
==================
*/
bool idCommonLocal::SafeMode( void ) {
	int			i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "safe" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "cvar_restart" ) ) {
			com_consoleLines[ i ].Clear();
			return true;
		}
	}
	return false;
}

/*
==================
idCommonLocal::CheckToolMode

Check for "renderbump", "dmap", or "editor" on the command line,
and force fullscreen off in those cases
==================
*/
void idCommonLocal::CheckToolMode( void ) {
	int			i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "guieditor" ) ) {
			com_editors |= EDITOR_GUI;
		}
		else if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "debugger" ) ) {
			com_editors |= EDITOR_DEBUGGER;
		}
		else if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "editor" ) ) {
			com_editors |= EDITOR_RADIANT;
		}
		// Nerve: Add support for the material editor
		else if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "materialEditor" ) ) {
			com_editors |= EDITOR_MATERIAL;
		}
		
		if ( !idStr::Icmp( com_consoleLines[ i ].Argv(0), "renderbump" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "editor" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "guieditor" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "debugger" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "dmap" )
			|| !idStr::Icmp( com_consoleLines[ i ].Argv(0), "materialEditor" )
			) {
			cvarSystem->SetCVarBool( "r_fullscreen", false );
			return;
		}
	}
}

/*
==================
idCommonLocal::StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets should
be after execing the config and default.
==================
*/
void idCommonLocal::StartupVariable( const char *match, bool once ) {
	int			i;
	const char *s;

	i = 0;
	while (	i < com_numConsoleLines ) {
		const int lineArgc = com_consoleLines[ i ].Argc();
		if ( lineArgc < 2 ) {
			i++;
			continue;
		}

		if ( idStr::Cmp( com_consoleLines[ i ].Argv( 0 ), "set" ) ) {
			i++;
			continue;
		}

		s = com_consoleLines[ i ].Argv(1);
		if ( !s || !s[0] ) {
			i++;
			continue;
		}

		if ( !match || !idStr::Icmp( s, match ) ) {
			// Match the regular "set" console command by preserving the full tail
			// of the startup line as the cvar value. This keeps multi-word launch
			// arguments intact even when a launcher/debugger doesn't quote them.
			const char *value = ( lineArgc >= 3 ) ? com_consoleLines[ i ].Args( 2, lineArgc - 1 ) : "";
			cvarSystem->SetCVarString( s, value );
			if ( once ) {
				// kill the line
				int j = i + 1;
				while ( j < com_numConsoleLines ) {
					com_consoleLines[ j - 1 ] = com_consoleLines[ j ];
					j++;
				}
				com_numConsoleLines--;
				continue;
			}
		}
		i++;
	}
}

/*
==================
idCommonLocal::AddStartupCommands

Adds command line parameters as script statements
Commands are separated by + signs

Returns true if any late commands were added, which
will keep the demoloop from immediately starting
==================
*/
bool idCommonLocal::AddStartupCommands( void ) {
	int		i;
	bool	added;

	added = false;
	// quote every token, so args with semicolons can work
	for ( i = 0; i < com_numConsoleLines; i++ ) {
		if ( !com_consoleLines[i].Argc() ) {
			continue;
		}

		// set commands won't override menu startup
		if ( idStr::Icmpn( com_consoleLines[i].Argv(0), "set", 3 ) ) {
			added = true;
		}
		// directly as tokenized so nothing gets screwed
		cmdSystem->BufferCommandArgs( CMD_EXEC_APPEND, com_consoleLines[i] );
	}

	return added;
}

/*
=================
idCommonLocal::InitTool
=================
*/
void idCommonLocal::InitTool( const int tool, const idDict *dict ) {
	if ( cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		cvarSystem->SetCVarBool( "r_fullscreen", false );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart\n" );
	}

	LoadToolsDLL();
	idKeyInput::ClearStates();

#ifdef ID_ALLOW_TOOLS
	if ( tool & EDITOR_SOUND ) {
		SoundEditorInit( dict );
	} else if ( tool & EDITOR_LIGHT ) {
		LightEditorInit( dict );
	} else if ( tool & EDITOR_PARTICLE ) {
		ParticleEditorInit( dict );
	} else if ( tool & EDITOR_AF ) {
		AFEditorInit( dict );
	} else if ( tool & EDITOR_DECL ) {
		DeclBrowserInit( dict );
	} else if ( tool & EDITOR_PDA ) {
		PDAEditorInit( dict );
	} else if ( tool & EDITOR_SCRIPT ) {
		ScriptEditorInit( dict );
	} else if ( tool & EDITOR_GUI ) {
		GUIEditorInit();
	} else if ( tool & EDITOR_RADIANT ) {
		RadiantInit();
	} else if ( tool & EDITOR_MATERIAL ) {
		MaterialEditorInit();
	}
#else
	(void)tool;
	(void)dict;
#endif
}

/*
==================
idCommonLocal::ActivateTool

Activates or Deactivates a tool
==================
*/
void idCommonLocal::ActivateTool( bool active ) {
	com_editorActive = active;
	Sys_GrabMouseCursor( !active );
}

/*
==================
idCommonLocal::GetUserCmdHz
==================
*/
int idCommonLocal::GetUserCmdHz( void ) const {
	return USERCMD_HZ;
}

/*
==================
idCommonLocal::GetUserCmdMSec
==================
*/
int idCommonLocal::GetUserCmdMSec( void ) const {
	return USERCMD_MSEC;
}

/*
==================
idCommonLocal::GetUserCmdTime
==================
*/
int idCommonLocal::GetUserCmdTime( int ticNumber ) const {
	return idCommon::GetUserCmdTime( ticNumber );
}

/*
==================
idCommonLocal::GetUserCmdDeltaMsec
==================
*/
int idCommonLocal::GetUserCmdDeltaMsec( int ticNumber ) const {
	return idCommon::GetUserCmdDeltaMsec( ticNumber );
}

/*
==================
idCommonLocal::GetFrameTime
==================
*/
int idCommonLocal::GetFrameTime( void ) const {
	return com_frameTime;
}

/*
==================
idCommonLocal::IsRenderableGameFrame
==================
*/
bool idCommonLocal::IsRenderableGameFrame( void ) const {
	return com_renderableGameFrame;
}

/*
==================
idCommonLocal::SetRenderableGameFrame
==================
*/
void idCommonLocal::SetRenderableGameFrame( bool in ) {
	com_renderableGameFrame = in;
}

/*
==================
idCommonLocal::GetErrorMessage
==================
*/
const char *idCommonLocal::GetErrorMessage( void ) const {
	return errorMessage;
}

/*
==================
idCommonLocal::IsToolActive
==================
*/
bool idCommonLocal::IsToolActive( void ) const {
	return com_editorActive;
}

/*
==================
idCommonLocal::DoingDeclValidation
==================
*/
bool idCommonLocal::DoingDeclValidation( void ) {
	const int validationMask = EDITOR_DECL | EDITOR_DECL_VALIDATING;
	return OpenQ4_GetActiveToolFlags( validationMask ) == validationMask;
}

/*
==================
idCommonLocal::GetSourceControl
==================
*/
rvISourceControl *idCommonLocal::GetSourceControl( void ) {
	return NULL;
}

/*
==================
idCommonLocal::ModViewThink
==================
*/
void idCommonLocal::ModViewThink( void ) {
}

/*
==================
idCommonLocal::RunAlwaysThinkGUIs
==================
*/
void idCommonLocal::RunAlwaysThinkGUIs( int time ) {
	uiManager->RunAlwaysThinkGUIs( time );
}

/*
==================
idCommonLocal::DebuggerCheckBreakpoint
==================
*/
void idCommonLocal::DebuggerCheckBreakpoint( idInterpreter *interpreter, idProgram *program, int instructionPointer ) {
#ifdef ID_ALLOW_TOOLS
	DebuggerServerCheckBreakpoint( interpreter, program, instructionPointer );
#else
	(void)interpreter;
	(void)program;
	(void)instructionPointer;
#endif
}

/*
==================
idCommonLocal::SetCrashReportAutoSendString
==================
*/
void idCommonLocal::SetCrashReportAutoSendString( const char *psString ) {
	(void)psString;
}

/*
==================
idCommonLocal::LoadToolsDLL
==================
*/
void idCommonLocal::LoadToolsDLL( void ) {
}

/*
==================
idCommonLocal::UnloadToolsDLL
==================
*/
void idCommonLocal::UnloadToolsDLL( void ) {
}

/*
==================
idCommonLocal::WriteFlaggedCVarsToFile
==================
*/
void idCommonLocal::WriteFlaggedCVarsToFile( const char *filename, int flags, const char *setCmd ) {
	idFile *f;

	f = fileSystem->OpenFileWrite( filename );
	if ( !f ) {
		Printf( "Couldn't write %s.\n", filename );
		return;
	}
	cvarSystem->WriteFlaggedVariables( flags, setCmd, f );
	fileSystem->CloseFile( f );
}

/*
==================
idCommonLocal::WriteConfigToFile
==================
*/
void idCommonLocal::WriteConfigToFile( const char *filename ) {
	idFile *f;
#ifdef ID_WRITE_VERSION
	ID_TIME_T t;
	char *curtime;
	idStr runtag;
	idFile_Memory compressed( "compressed" );
	idBase64 out;
#endif

	f = fileSystem->OpenFileWrite( filename );
	if ( !f ) {
		Printf ("Couldn't write %s.\n", filename );
		return;
	}

#ifdef ID_WRITE_VERSION
	assert( config_compressor );
	t = time( NULL );
	curtime = ctime( &t );
	sprintf( runtag, "%s - %s", cvarSystem->GetCVarString( "si_version" ), curtime );
	config_compressor->Init( &compressed, true, 8 );
	config_compressor->Write( runtag.c_str(), runtag.Length() );
	config_compressor->FinishCompress( );
	out.Encode( (const byte *)compressed.GetDataPtr(), compressed.Length() );
	f->Printf( "// %s\n", out.c_str() );
#endif

	idKeyInput::WriteBindings( f );
	cvarSystem->WriteFlaggedVariables( CVAR_ARCHIVE, "seta", f );
	fileSystem->CloseFile( f );
}

/*
===============
idCommonLocal::WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void idCommonLocal::WriteConfiguration( void ) {
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized ) {
		return;
	}

	if ( !( cvarSystem->GetModifiedFlags() & CVAR_ARCHIVE ) ) {
		return;
	}
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );

	// disable printing out the "Writing to:" message
	bool developer = com_developer.GetBool();
	com_developer.SetBool( false );

	WriteConfigToFile( CONFIG_FILE );
	session->WriteCDKey( );

	// restore the developer cvar
	com_developer.SetBool( developer );
}

/*
===============
KeysFromBinding()
Returns the key bound to the command
===============
*/
const char* idCommonLocal::KeysFromBinding( const char *bind ) {
	return idKeyInput::KeysFromBinding( bind );
}

/*
===============
BindingFromKey()
Returns the binding bound to key
===============
*/
const char* idCommonLocal::BindingFromKey( const char *key ) {
	return idKeyInput::BindingFromKey( key );
}

/*
===============
ButtonState()
Returns the state of the button
===============
*/
int	idCommonLocal::ButtonState( int key ) {
	return usercmdGen->ButtonState(key);
}

/*
===============
ButtonState()
Returns the state of the key
===============
*/
int	idCommonLocal::KeyState( int key ) {
	return usercmdGen->KeyState(key);
}

//============================================================================

#ifdef ID_ALLOW_TOOLS
/*
==================
Com_Editor_f

  we can start the editor dynamically, but we won't ever get back
==================
*/
static void Com_Editor_f( const idCmdArgs &args ) {
	RadiantInit();
}

/*
=============
Com_ScriptDebugger_f
=============
*/
static void Com_ScriptDebugger_f( const idCmdArgs &args ) {
	// Make sure it wasnt on the command line
	if ( !( com_editors & EDITOR_DEBUGGER ) ) {
		common->Printf( "Script debugger is currently disabled\n" );
		// DebuggerClientLaunch();
	}
}

/*
=============
Com_EditGUIs_f
=============
*/
static void Com_EditGUIs_f( const idCmdArgs &args ) {
	GUIEditorInit();
}

/*
=============
Com_MaterialEditor_f
=============
*/
static void Com_MaterialEditor_f( const idCmdArgs &args ) {
	// Turn off sounds
	soundSystem->SetMute( true );
	MaterialEditorInit();
}
#endif // ID_ALLOW_TOOLS

/*
=================
Com_Export_MD5R_f
=================
*/
static void Com_Export_MD5R_f( const idCmdArgs &args ) {
	(void)args;
	renderSystem->ExportMD5R( false );
}

/*
=====================
Com_Export_Cmp_MD5R_f
=====================
*/
static void Com_Export_Cmp_MD5R_f( const idCmdArgs &args ) {
	(void)args;
	renderSystem->ExportMD5R( true );
}

/*
============
idCmdSystemLocal::PrintMemInfo_f

This prints out memory debugging data
============
*/
static void PrintMemInfo_f( const idCmdArgs &args ) {

}

#ifdef ID_ALLOW_TOOLS
/*
==================
Com_EditLights_f
==================
*/
static void Com_EditLights_f( const idCmdArgs &args ) {
	LightEditorInit( NULL );
	cvarSystem->SetCVarInteger( "g_editEntityMode", 1 );
}

/*
==================
Com_EditSounds_f
==================
*/
static void Com_EditSounds_f( const idCmdArgs &args ) {
	SoundEditorInit( NULL );
	cvarSystem->SetCVarInteger( "g_editEntityMode", 2 );
}

/*
==================
Com_EditDecls_f
==================
*/
static void Com_EditDecls_f( const idCmdArgs &args ) {
	idDict dict;

	if ( args.Argc() > 1 ) {
		dict.Set( args.Argv( 1 ), "1" );
	}

	commonLocal.InitTool( EDITOR_DECL, &dict );
}

/*
==================
Com_EditAFs_f
==================
*/
static void Com_EditAFs_f( const idCmdArgs &args ) {
	AFEditorInit( NULL );
}

/*
==================
Com_EditParticles_f
==================
*/
static void Com_EditParticles_f( const idCmdArgs &args ) {
	ParticleEditorInit( NULL );
}

/*
==================
Com_EditScripts_f
==================
*/
static void Com_EditScripts_f( const idCmdArgs &args ) {
	ScriptEditorInit( NULL );
}

/*
==================
Com_EditPDAs_f
==================
*/
static void Com_EditPDAs_f( const idCmdArgs &args ) {
	PDAEditorInit( NULL );
}
#endif // ID_ALLOW_TOOLS

/*
==================
Com_Error_f

Just throw a fatal error to test error shutdown procedures.
==================
*/
static void Com_Error_f( const idCmdArgs &args ) {
	if ( !com_developer.GetBool() ) {
		commonLocal.Printf( "error may only be used in developer mode\n" );
		return;
	}

	if ( args.Argc() > 1 ) {
		commonLocal.FatalError( "Testing fatal error" );
	} else {
		commonLocal.Error( "Testing drop error" );
	}
}

/*
==================
Com_Freeze_f

Just freeze in place for a given number of seconds to test error recovery.
==================
*/
static void Com_Freeze_f( const idCmdArgs &args ) {
	float	s;
	int		start, now;

	if ( args.Argc() != 2 ) {
		commonLocal.Printf( "freeze <seconds>\n" );
		return;
	}

	if ( !com_developer.GetBool() ) {
		commonLocal.Printf( "freeze may only be used in developer mode\n" );
		return;
	}

	s = atof( args.Argv(1) );

	start = eventLoop->Milliseconds();

	while ( 1 ) {
		now = eventLoop->Milliseconds();
		if ( ( now - start ) * 0.001f > s ) {
			break;
		}
	}
}

/*
=================
Com_Crash_f

A way to force a bus error for development reasons
=================
*/
static void Com_Crash_f( const idCmdArgs &args ) {
	if ( !com_developer.GetBool() ) {
		commonLocal.Printf( "crash may only be used in developer mode\n" );
		return;
	}

	* ( int * ) 0 = 0x12345678;
}

/*
=================
Com_Quit_f
=================
*/
static void Com_Quit_f( const idCmdArgs &args ) {
	commonLocal.Quit();
}

/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
void Com_WriteConfig_f( const idCmdArgs &args ) {
	idStr	filename;

	if ( args.Argc() != 2 ) {
		commonLocal.Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	filename = args.Argv(1);
	filename.DefaultFileExtension( ".cfg" );
	commonLocal.Printf( "Writing %s.\n", filename.c_str() );
	commonLocal.WriteConfigToFile( filename );
}

/*
=================
Com_SetMachineSpecs_f
=================
*/
void Com_SetMachineSpec_f( const idCmdArgs &args ) {
	commonLocal.SetMachineSpec();
}

/*
=================
Com_ExecMachineSpecs_f
=================
*/
#ifdef MACOS_X
void OSX_GetVideoCard( int& outVendorId, int& outDeviceId );
bool OSX_GetCPUIdentification( int& cpuId, bool& oldArchitecture );
#endif
void Com_ExecMachineSpec_f( const idCmdArgs &args ) {
	if ( com_machineSpec.GetInteger() == 3 ) {
		cvarSystem->SetCVarInteger( "image_anisotropy", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_lodbias", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_forceDownSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_roundDown", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_preload", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useAllFormats", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBump", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBumpLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_usePrecompressedTextures", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downsize", 0			, CVAR_ARCHIVE );
		cvarSystem->SetCVarString( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 16, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 5, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 8, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_lensFlare", 2, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_screenFraction", 100, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_swapInterval", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarFloat( "r_forceAmbient", 0.0f, CVAR_ARCHIVE );
	} else if ( com_machineSpec.GetInteger() == 2 ) {
		cvarSystem->SetCVarString( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_lodbias", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_forceDownSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_roundDown", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_preload", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useAllFormats", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBump", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBumpLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_usePrecompressedTextures", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downsize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 8, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 4, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 4, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_lensFlare", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_screenFraction", 100, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_swapInterval", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarFloat( "r_forceAmbient", 0.0f, CVAR_ARCHIVE );
	} else if ( com_machineSpec.GetInteger() == 1 ) {
		cvarSystem->SetCVarString( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 4, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_lodbias", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_forceDownSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_roundDown", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_preload", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useAllFormats", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_usePrecompressedTextures", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBump", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBumpLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 2, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 3, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 2, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_lensFlare", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_screenFraction", 100, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_swapInterval", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarFloat( "r_forceAmbient", 0.0f, CVAR_ARCHIVE );
	} else {
		cvarSystem->SetCVarString( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_lodbias", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_roundDown", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_preload", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useAllFormats", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_usePrecompressedTextures", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSize", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 2, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBump", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBumpLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 3	, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 2, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_lensFlare", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_screenFraction", 85, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_swapInterval", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarFloat( "r_forceAmbient", 0.0f, CVAR_ARCHIVE );
	}

	if ( Sys_GetVideoRam() < 128 ) {
		cvarSystem->SetCVarBool( "image_ignoreHighQuality", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSize", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBump", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeBumpLimit", 256, CVAR_ARCHIVE );
	}

	if ( Sys_GetSystemRam() < 512 ) {
		cvarSystem->SetCVarBool( "image_ignoreHighQuality", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSize", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeLimit", 256, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecular", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSizeSpecularLimit", 64, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "com_purgeAll", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "r_forceLoadImages", true, CVAR_ARCHIVE );
	} else {
		cvarSystem->SetCVarBool( "com_purgeAll", false, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "r_forceLoadImages", false, CVAR_ARCHIVE );
	}

	bool oldCard = false;
	bool nv10or20 = false;
	renderSystem->GetCardCaps( oldCard, nv10or20 );
	if ( oldCard ) {
		cvarSystem->SetCVarBool( "g_decals", false, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_projectileLights", false, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_doubleVision", false, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_muzzleFlash", false, CVAR_ARCHIVE );
	} else {
		cvarSystem->SetCVarBool( "g_decals", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_projectileLights", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_doubleVision", true, CVAR_ARCHIVE );
		cvarSystem->SetCVarBool( "g_muzzleFlash", true, CVAR_ARCHIVE );
	}
	if ( nv10or20 ) {
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 1, CVAR_ARCHIVE );
	}

#if MACOS_X
	// On low settings, G4 systems & 64MB FX5200/NV34 Systems should default shadows off
	bool oldArch;
	int vendorId, deviceId, cpuId;
	OSX_GetVideoCard( vendorId, deviceId );
	OSX_GetCPUIdentification( cpuId, oldArch );
	bool isFX5200 = vendorId == 0x10DE && ( deviceId & 0x0FF0 ) == 0x0320;
	if ( ( oldArch || ( isFX5200 && Sys_GetVideoRam() < 128 ) ) && com_machineSpec.GetInteger() == 0 ) {
		cvarSystem->SetCVarBool( "r_shadows", false, CVAR_ARCHIVE );
	} else {
		cvarSystem->SetCVarBool( "r_shadows", true, CVAR_ARCHIVE );
	}
#endif
}

/*
=================
Com_ReloadEngine_f
=================
*/
void Com_ReloadEngine_f( const idCmdArgs &args ) {
	bool menu = false;

	if ( !commonLocal.IsInitialized() ) {
		return;
	}

	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "menu" ) == 0 ) {
		menu = true;
	}

	common->Printf( "============= ReloadEngine start =============\n" );
	fileSystem->SetIsFileLoadingAllowed( true );
	if ( !menu ) {
		Sys_ShowConsole( 1, false );
	}
	commonLocal.ShutdownGame( true );
	commonLocal.InitGame();
	if ( !menu && !idAsyncNetwork::serverDedicated.GetBool() ) {
		Sys_ShowConsole( 0, false );
	}
	common->Printf( "============= ReloadEngine end ===============\n" );

	if ( !cmdSystem->PostReloadEngine() ) {
		if ( menu ) {
			session->StartMenu( );
		}
	}
	fileSystem->SetIsFileLoadingAllowed( false );
}

/*
===============
idCommonLocal::GetLanguageDict
===============
*/
const idLangDict *idCommonLocal::GetLanguageDict( void ) {
	return &languageDict;
}

/*
===============
idCommonLocal::FilterLangList
===============
*/
void idCommonLocal::FilterLangList( idStrList* list, idStr lang ) {
	
	idStr temp;
	for( int i = 0; i < list->Num(); i++ ) {
		temp = (*list)[i];
		temp = temp.Right(temp.Length()-strlen("strings/"));
		temp = temp.Left(lang.Length());
		if(idStr::Icmp(temp, lang) != 0) {
			list->RemoveIndex(i);
			i--;
		}
	}
}

/*
===============
idCommonLocal::InitLanguageDict
===============
*/
void idCommonLocal::InitLanguageDict( void ) {
	idStr fileName;
	languageDict.Clear();

	//D3XP: Instead of just loading a single lang file for each language
	//we are going to load all files that begin with the language name
	//similar to the way pak files work. So you can place english001.lang
	//to add new strings to the english language dictionary
	idFileList*	langFiles;
	langFiles =  fileSystem->ListFilesTree( "strings", ".lang", true );
	
	idStrList langList = langFiles->GetList();

	StartupVariable( "sys_lang", false );	// let it be set on the command line - this is needed because this init happens very early
	idStr langName = cvarSystem->GetCVarString( "sys_lang" );

	//Loop through the list and filter
	idStrList currentLangList = langList;
	FilterLangList(&currentLangList, langName);
	
	if ( currentLangList.Num() == 0 ) {
		// reset cvar to default and try to load again
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reset sys_lang" );
		langName = cvarSystem->GetCVarString( "sys_lang" );
		currentLangList = langList;
		FilterLangList(&currentLangList, langName);
	}

	for( int i = 0; i < currentLangList.Num(); i++ ) {
		//common->Printf("%s\n", currentLangList[i].c_str());
		languageDict.Load( currentLangList[i], false );
	}

	fileSystem->FreeFileList(langFiles);

	Sys_InitScanTable();
}

/*
===============
idCommonLocal::LocalizeSpecificMapData
===============
*/
void idCommonLocal::LocalizeSpecificMapData( const char *fileName, idLangDict &langDict, const idLangDict &replaceArgs ) {
	idStr out, ws, work;

	idMapFile map;
	if ( map.Parse( fileName, false, false ) ) {
		int count = map.GetNumEntities();
		for ( int i = 0; i < count; i++ ) {
			idMapEntity *ent = map.GetEntity( i );
			if ( ent ) {
				for ( int j = 0; j < replaceArgs.GetNumKeyVals(); j++ ) {
					const idLangKeyValue *kv = replaceArgs.GetKeyVal( j );
					const char *temp = ent->epairs.GetString( kv->key );
					if ( temp && *temp ) {
						idStr val = kv->value;
						if ( val == temp ) {
							ent->epairs.Set( kv->key, langDict.AddString( temp ) );
						}
					}
				}
			}
		}
	map.Write( fileName, ".map" );
	}
}

/*
===============
idCommonLocal::LocalizeMapData
===============
*/
void idCommonLocal::LocalizeMapData( const char *fileName, idLangDict &langDict ) {
	const char *buffer = NULL;
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	common->SetRefreshOnPrint( true );

	if ( fileSystem->ReadFile( fileName, (void**)&buffer ) > 0 ) {
		src.LoadMemory( buffer, strlen(buffer), fileName );
		if ( src.IsLoaded() ) {
			common->Printf( "Processing %s\n", fileName );
			idStr mapFileName;
			idToken token, token2;
			idLangDict replaceArgs;
			while ( src.ReadToken( &token ) ) {
				mapFileName = token;
				replaceArgs.Clear();
				src.ExpectTokenString( "{" );
				while ( src.ReadToken( &token) ) {
					if ( token == "}" ) {
						break;
					}
					if ( src.ReadToken( &token2 ) ) {
						if ( token2 == "}" ) {
							break;
						}
						replaceArgs.AddKeyVal( token, token2 );
					}
				}
				common->Printf( "  localizing map %s...\n", mapFileName.c_str() );
				LocalizeSpecificMapData( mapFileName, langDict, replaceArgs );
			}
		}
		fileSystem->FreeFile( (void*)buffer );
	}

	common->SetRefreshOnPrint( false );
}

/*
===============
idCommonLocal::LocalizeGui
===============
*/
void idCommonLocal::LocalizeGui( const char *fileName, idLangDict &langDict ) {
	idStr out, ws, work;
	const char *buffer = NULL;
	out.Empty();
	int k;
	char ch;
	char slash = '\\';
	char tab = 't';
	char nl = 'n';
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );
	if ( fileSystem->ReadFile( fileName, (void**)&buffer ) > 0 ) {
		src.LoadMemory( buffer, strlen(buffer), fileName );
		if ( src.IsLoaded() ) {
			idFile *outFile = fileSystem->OpenFileWrite( fileName ); 
			common->Printf( "Processing %s\n", fileName );
			session->UpdateScreen();
			idToken token;
			while( src.ReadToken( &token ) ) {
				src.GetLastWhiteSpace( ws );
				out += ws;
				if ( token.type == TT_STRING ) {
					out += va( "\"%s\"", token.c_str() );
				} else {
					out += token;
				}
				if ( out.Length() > 200000 ) {
					outFile->Write( out.c_str(), out.Length() );
					out = "";
				}
				work = token.Right( 6 );
				if ( token.Icmp( "text" ) == 0 || work.Icmp( "::text" ) == 0 || token.Icmp( "choices" ) == 0 ) {
					if ( src.ReadToken( &token ) ) {
						// see if already exists, if so save that id to this position in this file
						// otherwise add this to the list and save the id to this position in this file
						src.GetLastWhiteSpace( ws );
						out += ws;
						token = langDict.AddString( token );
						out += "\"";
						for ( k = 0; k < token.Length(); k++ ) {
							ch = token[k];
							if ( ch == '\t' ) {
								out += slash;
								out += tab;
							} else if ( ch == '\n' || ch == '\r' ) {
								out += slash;
								out += nl;
							} else {
								out += ch;
							}
						}
						out += "\"";
					}
				} else if ( token.Icmp( "comment" ) == 0 ) {
					if ( src.ReadToken( &token ) ) {
						// need to write these out by hand to preserve any \n's
						// see if already exists, if so save that id to this position in this file
						// otherwise add this to the list and save the id to this position in this file
						src.GetLastWhiteSpace( ws );
						out += ws;
						out += "\"";
						for ( k = 0; k < token.Length(); k++ ) {
							ch = token[k];
							if ( ch == '\t' ) {
								out += slash;
								out += tab;
							} else if ( ch == '\n' || ch == '\r' ) {
								out += slash;
								out += nl;
							} else {
								out += ch;
							}
						}
						out += "\"";
					}
				}
			}
			outFile->Write( out.c_str(), out.Length() );
			fileSystem->CloseFile( outFile );
		}
		fileSystem->FreeFile( (void*)buffer );
	}
}

/*
=================
ReloadLanguage_f
=================
*/
void Com_ReloadLanguage_f( const idCmdArgs &args ) {
	(void)args;
	fileSystem->SetIsFileLoadingAllowed( true );
	commonLocal.InitLanguageDict();
	fileSystem->SetIsFileLoadingAllowed( false );
}

/*
=================
Com_WriteAssetLog_f
=================
*/
void Com_WriteAssetLog_f( const idCmdArgs &args ) {
	(void)args;
	fileSystem->WriteAssetLog();
}

/*
=================
Com_ClearAssetLog_f
=================
*/
void Com_ClearAssetLog_f( const idCmdArgs &args ) {
	(void)args;
	fileSystem->ClearAssetLog();
}

typedef idHashTable<idStrList> ListHash;
void LoadMapLocalizeData(ListHash& listHash) {

	idStr fileName = "map_localize.cfg";
	const char *buffer = NULL;
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	if ( fileSystem->ReadFile( fileName, (void**)&buffer ) > 0 ) {
		src.LoadMemory( buffer, strlen(buffer), fileName );
		if ( src.IsLoaded() ) {
			idStr classname;
			idToken token;



			while ( src.ReadToken( &token ) ) {
				classname = token;
				src.ExpectTokenString( "{" );

				idStrList list;
				while ( src.ReadToken( &token) ) {
					if ( token == "}" ) {
						break;
					}
					list.Append(token);
				}

				listHash.Set(classname, list);
			}
		}
		fileSystem->FreeFile( (void*)buffer );
	}

}

void LoadGuiParmExcludeList(idStrList& list) {

	idStr fileName = "guiparm_exclude.cfg";
	const char *buffer = NULL;
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	if ( fileSystem->ReadFile( fileName, (void**)&buffer ) > 0 ) {
		src.LoadMemory( buffer, strlen(buffer), fileName );
		if ( src.IsLoaded() ) {
			idStr classname;
			idToken token;



			while ( src.ReadToken( &token ) ) {
				list.Append(token);
			}
		}
		fileSystem->FreeFile( (void*)buffer );
	}
}

bool TestMapVal(idStr& str) {
	//Already Localized?
	if(str.Find("#str_") != -1) {
		return false;
	}

	return true;
}

bool TestGuiParm(const char* parm, const char* value, idStrList& excludeList) {

	idStr testVal = value;

	//Already Localized?
	if(testVal.Find("#str_") != -1) {
		return false;
	}

	//Numeric
	if(testVal.IsNumeric()) {
		return false;
	}

	//Contains ::
	if(testVal.Find("::") != -1) {
		return false;
	}

	//Contains /
	if(testVal.Find("/") != -1) {
		return false;
	}

	if(excludeList.Find(testVal)) {
		return false;
	}

	return true;
}

void GetFileList(const char* dir, const char* ext, idStrList& list) {

	//Recurse Subdirectories
	idStrList dirList;
	Sys_ListFiles(dir, "/", dirList);
	for(int i = 0; i < dirList.Num(); i++) {
		if(dirList[i] == "." || dirList[i] == "..") {
			continue;
		}
		idStr fullName = va("%s/%s", dir, dirList[i].c_str());
		GetFileList(fullName, ext, list);
	}

	idStrList fileList;
	Sys_ListFiles(dir, ext, fileList);
	for(int i = 0; i < fileList.Num(); i++) {
		idStr fullName = va("%s/%s", dir, fileList[i].c_str());
		list.Append(fullName);
	}
}

int LocalizeMap(const char* mapName, idLangDict &langDict, ListHash& listHash, idStrList& excludeList, bool writeFile) {

	common->Printf("Localizing Map '%s'\n", mapName);

	int strCount = 0;
	
	idMapFile map;
	if ( map.Parse(mapName, false, false ) ) {
		int count = map.GetNumEntities();
		for ( int j = 0; j < count; j++ ) {
			idMapEntity *ent = map.GetEntity( j );
			if ( ent ) {

				idStr classname = ent->epairs.GetString("classname");

				//Hack: for info_location
				bool hasLocation = false;

				idStrList* list;
				listHash.Get(classname, &list);
				if(list) {

					for(int k = 0; k < list->Num(); k++) {

						idStr val = ent->epairs.GetString((*list)[k], "");
						
						if(val.Length() && classname == "info_location" && (*list)[k] == "location") {
							hasLocation = true;
						}

						if(val.Length() && TestMapVal(val)) {
							
							if(!hasLocation || (*list)[k] == "location") {
								//Localize it!!!
								strCount++;
								ent->epairs.Set( (*list)[k], langDict.AddString( val ) );
							}
						}
					}
				}

				listHash.Get("all", &list);
				if(list) {
					for(int k = 0; k < list->Num(); k++) {
						idStr val = ent->epairs.GetString((*list)[k], "");
						if(val.Length() && TestMapVal(val)) {
							//Localize it!!!
							strCount++;
							ent->epairs.Set( (*list)[k], langDict.AddString( val ) );
						}
					}
				}

				//Localize the gui_parms
				const idKeyValue* kv = ent->epairs.MatchPrefix("gui_parm");
				while( kv ) {
					if(TestGuiParm(kv->GetKey(), kv->GetValue(), excludeList)) {
						//Localize It!
						strCount++;
						ent->epairs.Set( kv->GetKey(), langDict.AddString( kv->GetValue() ) );
					}
					kv = ent->epairs.MatchPrefix( "gui_parm", kv );
				}
			}
		}
		if(writeFile && strCount > 0)  {
			//Before we write the map file lets make a backup of the original
			idStr file =  fileSystem->RelativePathToOSPath(mapName);
			idStr bak = file.Left(file.Length() - 4);
			bak.Append(".bak_loc");
			fileSystem->CopyFile( file, bak );
			
			map.Write( mapName, ".map" );
		}
	}

	common->Printf("Count: %d\n", strCount);
	return strCount;
}

/*
=================
LocalizeMaps_f
=================
*/
void Com_LocalizeMaps_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "Usage: localizeMaps <count | dictupdate | all> <map>\n" );
		return;
	}

	int strCount = 0;
	
	bool count = false;
	bool dictUpdate = false;
	bool write = false;

	if ( idStr::Icmp( args.Argv(1), "count" ) == 0 ) {
		count = true;
	} else if ( idStr::Icmp( args.Argv(1), "dictupdate" ) == 0 ) {
		count = true;
		dictUpdate = true;
	} else if ( idStr::Icmp( args.Argv(1), "all" ) == 0 ) {
		count = true;
		dictUpdate = true;
		write = true;
	} else {
		common->Printf( "Invalid Command\n" );
		common->Printf( "Usage: localizeMaps <count | dictupdate | all>\n" );
		return;

	}

	idLangDict strTable;
	idStr filename = va("strings/english%.3i.lang", com_product_lang_ext.GetInteger());
	if(strTable.Load( filename ) == false) {
		//This is a new file so set the base index
		strTable.SetBaseID(com_product_lang_ext.GetInteger()*100000);
	}

	common->SetRefreshOnPrint( true );
	
	ListHash listHash;
	LoadMapLocalizeData(listHash);

	idStrList excludeList;
	LoadGuiParmExcludeList(excludeList);

	if(args.Argc() == 3) {
		strCount += LocalizeMap(args.Argv(2), strTable, listHash, excludeList, write);
	} else {
		idStrList files;
		GetFileList("z:/d3xp/d3xp/maps/game", "*.map", files);
		for ( int i = 0; i < files.Num(); i++ ) {
			idStr file =  fileSystem->OSPathToRelativePath(files[i]);
			strCount += LocalizeMap(file, strTable, listHash, excludeList, write);		
		}
	}

	if(count) {
		common->Printf("Localize String Count: %d\n", strCount);
	}

	common->SetRefreshOnPrint( false );

	if(dictUpdate) {
		strTable.Save( filename );
	}
}

/*
=================
LocalizeGuis_f
=================
*/
void Com_LocalizeGuis_f( const idCmdArgs &args ) {

	if ( args.Argc() != 2 ) {
		common->Printf( "Usage: localizeGuis <all | gui>\n" );
		return;
	}

	idLangDict strTable;

	idStr filename = va("strings/english%.3i.lang", com_product_lang_ext.GetInteger());
	if(strTable.Load( filename ) == false) {
		//This is a new file so set the base index
		strTable.SetBaseID(com_product_lang_ext.GetInteger()*100000);
	}

	idFileList *files;
	if ( idStr::Icmp( args.Argv(1), "all" ) == 0 ) {
		idStr game = cvarSystem->GetCVarString( "fs_game" );
		if(game.Length()) {
			files = fileSystem->ListFilesTree( "guis", "*.gui", true, game );
		} else {
			files = fileSystem->ListFilesTree( "guis", "*.gui", true );
		}
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			commonLocal.LocalizeGui( files->GetFile( i ), strTable );
		}
		fileSystem->FreeFileList( files );

		if(game.Length()) {
			files = fileSystem->ListFilesTree( "guis", "*.pd", true, game );
		} else {
			files = fileSystem->ListFilesTree( "guis", "*.pd", true, "d3xp" );
		}
		
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			commonLocal.LocalizeGui( files->GetFile( i ), strTable );
		}
		fileSystem->FreeFileList( files );

	} else {
		commonLocal.LocalizeGui( args.Argv(1), strTable );
	}
	strTable.Save( filename );
}

void Com_LocalizeGuiParmsTest_f( const idCmdArgs &args ) {

	common->SetRefreshOnPrint( true );

	idFile *localizeFile = fileSystem->OpenFileWrite( "gui_parm_localize.csv" ); 
	idFile *noLocalizeFile = fileSystem->OpenFileWrite( "gui_parm_nolocalize.csv" ); 

	idStrList excludeList;
	LoadGuiParmExcludeList(excludeList);

	idStrList files;
	GetFileList("z:/d3xp/d3xp/maps/game", "*.map", files);

	for ( int i = 0; i < files.Num(); i++ ) {
		
		common->Printf("Testing Map '%s'\n", files[i].c_str());
		idMapFile map;

		idStr file =  fileSystem->OSPathToRelativePath(files[i]);
		if ( map.Parse(file, false, false ) ) {
			int count = map.GetNumEntities();
			for ( int j = 0; j < count; j++ ) {
				idMapEntity *ent = map.GetEntity( j );
				if ( ent ) {
					const idKeyValue* kv = ent->epairs.MatchPrefix("gui_parm");
					while( kv ) {
						if(TestGuiParm(kv->GetKey(), kv->GetValue(), excludeList)) {
							idStr out = va("%s,%s,%s\r\n", kv->GetValue().c_str(), kv->GetKey().c_str(), file.c_str());
							localizeFile->Write( out.c_str(), out.Length() );
						} else {
							idStr out = va("%s,%s,%s\r\n", kv->GetValue().c_str(), kv->GetKey().c_str(), file.c_str());
							noLocalizeFile->Write( out.c_str(), out.Length() );
						}
						kv = ent->epairs.MatchPrefix( "gui_parm", kv );
					}
				}
			}
		}
	}
	
	fileSystem->CloseFile( localizeFile );
	fileSystem->CloseFile( noLocalizeFile );

	common->SetRefreshOnPrint( false );
}


void Com_LocalizeMapsTest_f( const idCmdArgs &args ) {

	ListHash listHash;
	LoadMapLocalizeData(listHash);


	common->SetRefreshOnPrint( true );

	idFile *localizeFile = fileSystem->OpenFileWrite( "map_localize.csv" ); 
	
	idStrList files;
	GetFileList("z:/d3xp/d3xp/maps/game", "*.map", files);

	for ( int i = 0; i < files.Num(); i++ ) {

		common->Printf("Testing Map '%s'\n", files[i].c_str());
		idMapFile map;

		idStr file =  fileSystem->OSPathToRelativePath(files[i]);
		if ( map.Parse(file, false, false ) ) {
			int count = map.GetNumEntities();
			for ( int j = 0; j < count; j++ ) {
				idMapEntity *ent = map.GetEntity( j );
				if ( ent ) {
					
					//Temp code to get a list of all entity key value pairs
					/*idStr classname = ent->epairs.GetString("classname");
					if(classname == "worldspawn" || classname == "func_static" || classname == "light" || classname == "speaker" || classname.Left(8) == "trigger_") {
						continue;
					}
					for( int i = 0; i < ent->epairs.GetNumKeyVals(); i++) {
						const idKeyValue* kv = ent->epairs.GetKeyVal(i);
						idStr out = va("%s,%s,%s,%s\r\n", classname.c_str(), kv->GetKey().c_str(), kv->GetValue().c_str(), file.c_str());
						localizeFile->Write( out.c_str(), out.Length() );
					}*/

					idStr classname = ent->epairs.GetString("classname");
					
					//Hack: for info_location
					bool hasLocation = false;

					idStrList* list;
					listHash.Get(classname, &list);
					if(list) {

						for(int k = 0; k < list->Num(); k++) {

							idStr val = ent->epairs.GetString((*list)[k], "");
							
							if(classname == "info_location" && (*list)[k] == "location") {
								hasLocation = true;
							}

							if(val.Length() && TestMapVal(val)) {
								
								if(!hasLocation || (*list)[k] == "location") {
									idStr out = va("%s,%s,%s\r\n", val.c_str(), (*list)[k].c_str(), file.c_str());
									localizeFile->Write( out.c_str(), out.Length() );
								}
							}
						}
					}

					listHash.Get("all", &list);
					if(list) {
						for(int k = 0; k < list->Num(); k++) {
							idStr val = ent->epairs.GetString((*list)[k], "");
							if(val.Length() && TestMapVal(val)) {
								idStr out = va("%s,%s,%s\r\n", val.c_str(), (*list)[k].c_str(), file.c_str());
								localizeFile->Write( out.c_str(), out.Length() );
							}
						}
					}
				}
			}
		}
	}

	fileSystem->CloseFile( localizeFile );

	common->SetRefreshOnPrint( false );
}

/*
=================
Com_StartBuild_f
=================
*/
void Com_StartBuild_f( const idCmdArgs &args ) {
	//globalImages->StartBuild();
}

/*
=================
Com_FinishBuild_f
=================
*/
void Com_FinishBuild_f( const idCmdArgs &args ) {
	
}

/*
==============
Com_Help_f
==============
*/
void Com_Help_f( const idCmdArgs &args ) {
	common->Printf( "\nCommonly used commands:\n" );
	common->Printf( "  spawnServer      - start the server.\n" );
	common->Printf( "  disconnect       - shut down the server.\n" );
	common->Printf( "  listCmds         - list all console commands.\n" );
	common->Printf( "  listCVars        - list all console variables.\n" );
	common->Printf( "  kick             - kick a client by number.\n" );
	common->Printf( "  gameKick         - kick a client by name.\n" );
	common->Printf( "  serverNextMap    - immediately load next map.\n" );
	common->Printf( "  serverMapRestart - restart the current map.\n" );
	common->Printf( "  serverForceReady - force all players to ready status.\n" );
	common->Printf( "\nCommonly used variables:\n" );
	common->Printf( "  si_name          - server name (change requires a restart to see)\n" );
	common->Printf( "  si_gametype      - type of game.\n" );
	common->Printf( "  si_fragLimit     - max kills to win (or lives in Last Man Standing).\n" );
	common->Printf( "  si_timeLimit     - maximum time a game will last.\n" );
	common->Printf( "  si_warmup        - do pre-game warmup.\n" );
	common->Printf( "  si_pure          - pure server.\n" );
	common->Printf( "  g_mapCycle       - name of .scriptcfg file for cycling maps.\n" );
	common->Printf( "See mapcycle.scriptcfg for an example of a mapcyle script.\n\n" );
}

/*
=================
idCommonLocal::InitCommands
=================
*/
void idCommonLocal::InitCommands( void ) {
	cmdSystem->AddCommand( "error", Com_Error_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "causes an error" );
	cmdSystem->AddCommand( "crash", Com_Crash_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "causes a crash" );
	cmdSystem->AddCommand( "freeze", Com_Freeze_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "freezes the game for a number of seconds" );
	cmdSystem->AddCommand( "quit", Com_Quit_f, CMD_FL_SYSTEM, "quits the game" );
	cmdSystem->AddCommand( "exit", Com_Quit_f, CMD_FL_SYSTEM, "exits the game" );
	cmdSystem->AddCommand( "writeConfig", Com_WriteConfig_f, CMD_FL_SYSTEM, "writes a config file" );
	cmdSystem->AddCommand( "reloadEngine", Com_ReloadEngine_f, CMD_FL_SYSTEM, "reloads the engine down to including the file system" );
	cmdSystem->AddCommand( "setMachineSpec", Com_SetMachineSpec_f, CMD_FL_SYSTEM, "detects system capabilities and sets com_machineSpec to appropriate value" );
	cmdSystem->AddCommand( "execMachineSpec", Com_ExecMachineSpec_f, CMD_FL_SYSTEM, "execs the appropriate config files and sets cvars based on com_machineSpec" );

	cmdSystem->AddCommand("dmap", Dmap_f, CMD_FL_TOOL, "compiles a map", idCmdSystem::ArgCompletion_MapName);
	//cmdSystem->AddCommand("runAAS", RunAAS_f, CMD_FL_TOOL, "compiles an AAS file for a map", idCmdSystem::ArgCompletion_MapName);

#ifdef ID_ALLOW_TOOLS
	// compilers	
	cmdSystem->AddCommand( "renderbump", RenderBump_f, CMD_FL_TOOL, "renders a bump map", idCmdSystem::ArgCompletion_ModelName );
	cmdSystem->AddCommand( "renderbumpFlat", RenderBumpFlat_f, CMD_FL_TOOL, "renders a flat bump map", idCmdSystem::ArgCompletion_ModelName );	
	cmdSystem->AddCommand( "runAASDir", RunAASDir_f, CMD_FL_TOOL, "compiles AAS files for all maps in a folder", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "runReach", RunReach_f, CMD_FL_TOOL, "calculates reachability for an AAS file", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "roq", RoQFileEncode_f, CMD_FL_TOOL, "encodes a roq file" );
#endif

#ifdef ID_ALLOW_TOOLS
	// editors
	cmdSystem->AddCommand( "editor", Com_Editor_f, CMD_FL_TOOL, "launches the level editor Radiant" );
	cmdSystem->AddCommand( "editLights", Com_EditLights_f, CMD_FL_TOOL, "launches the in-game Light Editor" );
	cmdSystem->AddCommand( "editSounds", Com_EditSounds_f, CMD_FL_TOOL, "launches the in-game Sound Editor" );
	cmdSystem->AddCommand( "editDecls", Com_EditDecls_f, CMD_FL_TOOL, "launches the in-game Declaration Editor" );
	cmdSystem->AddCommand( "editAFs", Com_EditAFs_f, CMD_FL_TOOL, "launches the in-game Articulated Figure Editor" );
	cmdSystem->AddCommand( "editParticles", Com_EditParticles_f, CMD_FL_TOOL, "launches the in-game Particle Editor" );
	cmdSystem->AddCommand( "editScripts", Com_EditScripts_f, CMD_FL_TOOL, "launches the in-game Script Editor" );
	cmdSystem->AddCommand( "editGUIs", Com_EditGUIs_f, CMD_FL_TOOL, "launches the GUI Editor" );
	cmdSystem->AddCommand( "editPDAs", Com_EditPDAs_f, CMD_FL_TOOL, "launches the in-game PDA Editor" );
	cmdSystem->AddCommand( "debugger", Com_ScriptDebugger_f, CMD_FL_TOOL, "launches the Script Debugger" );

	//BSM Nerve: Add support for the material editor
	cmdSystem->AddCommand( "materialEditor", Com_MaterialEditor_f, CMD_FL_TOOL, "launches the Material Editor" );
#endif

	cmdSystem->AddCommand( "printMemInfo", PrintMemInfo_f, CMD_FL_SYSTEM, "prints memory debugging data" );
	cmdSystem->AddCommand( "exportMD5R", Com_Export_MD5R_f, CMD_FL_SYSTEM, "exports the active world MD5RProc companion and all supported loaded MD5R models when support is available" );
	cmdSystem->AddCommand( "exportCmpMD5R", Com_Export_Cmp_MD5R_f, CMD_FL_SYSTEM, "exports the active world MD5RProc companion and all supported loaded MD5R models in compressed form when support is available" );

	// idLib commands
	cmdSystem->AddCommand( "memoryDump", Mem_Dump_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "creates a memory dump" );
	cmdSystem->AddCommand( "memoryDumpCompressed", Mem_DumpCompressed_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "creates a compressed memory dump" );
	cmdSystem->AddCommand( "showStringMemory", idStr::ShowMemoryUsage_f, CMD_FL_SYSTEM, "shows memory used by strings" );
	cmdSystem->AddCommand( "showDictMemory", idDict::ShowMemoryUsage_f, CMD_FL_SYSTEM, "shows memory used by dictionaries" );
	cmdSystem->AddCommand( "listDictKeys", idDict::ListKeys_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "lists all keys used by dictionaries" );
	cmdSystem->AddCommand( "listDictValues", idDict::ListValues_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "lists all values used by dictionaries" );
	cmdSystem->AddCommand( "testSIMD", idSIMD::Test_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "test SIMD code" );

	// localization
	cmdSystem->AddCommand( "localizeGuis", Com_LocalizeGuis_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "localize guis" );
	cmdSystem->AddCommand( "localizeMaps", Com_LocalizeMaps_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "localize maps" );
	cmdSystem->AddCommand( "reloadLanguage", Com_ReloadLanguage_f, CMD_FL_SYSTEM, "reload language dict" );
	cmdSystem->AddCommand( "writeAssetLog", Com_WriteAssetLog_f, CMD_FL_SYSTEM, "generates log file of all the assets loaded" );
	cmdSystem->AddCommand( "clearAssetLog", Com_ClearAssetLog_f, CMD_FL_SYSTEM, "clears log of all the assets loaded" );

	//D3XP Localization
	cmdSystem->AddCommand( "localizeGuiParmsTest", Com_LocalizeGuiParmsTest_f, CMD_FL_SYSTEM, "Create test files that show gui parms localized and ignored." );
	cmdSystem->AddCommand( "localizeMapsTest", Com_LocalizeMapsTest_f, CMD_FL_SYSTEM, "Create test files that shows which strings will be localized." );

	// build helpers
	cmdSystem->AddCommand( "startBuild", Com_StartBuild_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "prepares to make a build" );
	cmdSystem->AddCommand( "finishBuild", Com_FinishBuild_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "finishes the build process" );
	RenderDoc_RegisterCommands();

#ifdef ID_DEDICATED
	cmdSystem->AddCommand( "help", Com_Help_f, CMD_FL_SYSTEM, "shows help" );
#endif
}

/*
=================
idCommonLocal::InitRenderSystem
=================
*/
void idCommonLocal::InitRenderSystem( void ) {
	if ( com_skipRenderer.GetBool() ) {
		return;
	}

	renderSystem->InitOpenGL();
	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104343" ) );
}

/*
=================
idCommonLocal::PrintLoadingMessage
=================
*/
static int Common_CountVisibleSmallChars( const char *string ) {
	if ( !( string && *string ) ) {
		return 0;
	}

	int count = 0;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( string );
	while ( *s ) {
		const int colorEscapeLength = idStr::ColorEscapeLength( reinterpret_cast<const char *>( s ) );
		if ( colorEscapeLength > 0 ) {
			s += colorEscapeLength;
			continue;
		}
		count++;
		s++;
	}
	return count;
}

static void Common_DrawScaledSmallString( float x, float y, float charWidth, float charHeight,
	const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) {
	if ( !( string && *string ) || !material || charWidth <= 0.0f || charHeight <= 0.0f ) {
		return;
	}

	idVec4 color;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( string );
	float xx = x;
	renderSystem->SetColor( setColor );

	while ( *s ) {
		idVec4 parsedColor;
		bool resetToDefault = false;
		const int colorEscapeLength = idStr::ColorEscapeLength( reinterpret_cast<const char *>( s ), &parsedColor, &resetToDefault );
		if ( colorEscapeLength > 0 ) {
			if ( !forceColor ) {
				if ( resetToDefault ) {
					renderSystem->SetColor( setColor );
				} else {
					color = parsedColor;
					color[3] = setColor[3];
					renderSystem->SetColor( color );
				}
			}
			s += colorEscapeLength;
			continue;
		}

		const int ch = *s & 255;
		if ( ch != ' ' ) {
			const int row = ch >> 4;
			const int col = ch & 15;
			const float frow = row * 0.0625f;
			const float fcol = col * 0.0625f;
			const float size = 0.0625f;
			renderSystem->DrawStretchPic( xx, y, charWidth, charHeight,
				fcol, frow, fcol + size, frow + size, material );
		}

		xx += charWidth;
		s++;
	}

	renderSystem->SetColor( idVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
}

void idCommonLocal::PrintLoadingMessage( const char *msg ) {
	if ( !( msg && *msg ) ) {
		return;
	}

	renderSystem->BeginFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );

	const float virtualWidth = static_cast<float>( SCREEN_WIDTH );
	const float virtualHeight = static_cast<float>( SCREEN_HEIGHT );
	float splashX = 0.0f;
	float splashY = 0.0f;
	float splashW = virtualWidth;
	float splashH = virtualHeight;
	float correctedX = 0.0f;
	float correctedY = 0.0f;
	float correctedW = virtualWidth;
	float correctedH = virtualHeight;
	float textScaleX = 1.0f;
	float textScaleY = 1.0f;

	float viewportWidth = static_cast<float>( glConfig.uiViewportWidth );
	float viewportHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		viewportWidth = static_cast<float>( glConfig.vidWidth );
		viewportHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( viewportWidth > 0.0f && viewportHeight > 0.0f ) {
		const float uniformPhysicalScale = Min( viewportWidth / virtualWidth, viewportHeight / virtualHeight );
		const float drawWidth = virtualWidth * uniformPhysicalScale;
		const float drawHeight = virtualHeight * uniformPhysicalScale;
		const float virtualPerPhysicalX = virtualWidth / viewportWidth;
		const float virtualPerPhysicalY = virtualHeight / viewportHeight;

		textScaleX = uniformPhysicalScale * virtualPerPhysicalX;
		textScaleY = uniformPhysicalScale * virtualPerPhysicalY;
		correctedX = ( viewportWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
		correctedY = ( viewportHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;
		correctedW = virtualWidth * textScaleX;
		correctedH = virtualHeight * textScaleY;
	}

	if ( cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		splashX = correctedX;
		splashY = correctedY;
		splashW = correctedW;
		splashH = correctedH;
	}

	renderSystem->SetColor( idVec4( 24.0f / 255.0f, 26.0f / 255.0f, 8.0f / 255.0f, 1.0f ) );
	renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
	renderSystem->SetColor( idVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	renderSystem->DrawStretchPic( splashX, splashY, splashW, splashH, 0, 0, 1, 1, declManager->FindMaterial( "gfx/splashScreen" ) );

	const int charCount = Common_CountVisibleSmallChars( msg );
	const float charWidth = SMALLCHAR_WIDTH * textScaleX;
	const float charHeight = SMALLCHAR_HEIGHT * textScaleY;
	const float textWidth = charCount * charWidth;
	const float textX = correctedX + ( correctedW - textWidth ) * 0.5f;
	const float textY = correctedY + 410.0f * textScaleY;
	Common_DrawScaledSmallString( textX, textY, charWidth, charHeight, msg,
		idVec4( 0.94f, 0.62f, 0.05f, 1.0f ), true, declManager->FindMaterial( "fonts/english/bigchars", false ) );
	renderSystem->SetColor( idVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	renderSystem->EndFrame( NULL, NULL );
}

/*
=================
idCommonLocal::InitSIMD
=================
*/
void idCommonLocal::InitSIMD( void ) {
	idSIMD::InitProcessor( "doom", com_forceGenericSIMD.GetBool() );
	com_forceGenericSIMD.ClearModified();
}

/*
=================
idCommonLocal::Frame
=================
*/
void idCommonLocal::Frame( void ) {
	try {
		OpenQ4_BeginPresentationFrame();
		const int frameStartMsec = com_frameRealTime;

		// pump all the events
		Sys_GenerateEvents();

		// write config file if anything changed
		WriteConfiguration(); 

		// change SIMD implementation if required
		if ( com_forceGenericSIMD.IsModified() ) {
			InitSIMD();
		}

		eventLoop->RunEventLoop();

		com_frameTime = GetUserCmdTime( com_ticNumber );

		idAsyncNetwork::RunFrame();

	if ( idAsyncNetwork::IsActive() ) {
		if ( idAsyncNetwork::serverDedicated.GetInteger() != 1 ) {
			// Keep netplay flow the same as stock, but ensure audio mixes each frame.
			soundSystem->Render();
			session->GuiFrameEvents();
			OpenQ4_RecordMultiplayerFramePacing( frameStartMsec );
			session->UpdateScreen( false );
		}
	} else {
			session->Frame();

			// normal, in-sequence screen update
			session->UpdateScreen( false );
		}

		// report timing information
		if ( com_speeds.GetBool() ) {
			static int	lastTime;
			int		nowTime = Sys_Milliseconds();
			int		com_frameMsec = nowTime - lastTime;
			lastTime = nowTime;
			Printf( "frame:%i all:%3i gfr:%3i rf:%3i bk:%3i\n", com_frameNumber, com_frameMsec, time_gameFrame, time_frontend, time_backend );
			time_gameFrame = 0;
			time_gameDraw = 0;
		}	

		com_frameNumber++;

		// set idLib frame number for frame based memory dumps
		idLib::frameNumber = com_frameNumber;

		// the FPU stack better be empty at this point or some bad code or compiler bug left values on the stack
		//if ( !Sys_FPU_StackIsEmpty() ) {
		//	Printf( Sys_FPU_GetState() );
		//	FatalError( "idCommon::Frame: the FPU stack is not empty at the end of the frame\n" );
		//}
	}

	catch( idException & ) {
		return;			// an ERP_DROP was thrown
	}
}

/*
=================
idCommonLocal::GUIFrame
=================
*/
void idCommonLocal::GUIFrame( bool execCmd, bool network ) {
	OpenQ4_BeginPresentationFrame();
	Sys_GenerateEvents();
	eventLoop->RunEventLoop( execCmd );	// and execute any commands
	com_frameTime = GetUserCmdTime( com_ticNumber );
	if ( network ) {
		idAsyncNetwork::RunFrame();
	}
	session->Frame();
	session->UpdateScreen( false );	
}

/*
=================
idCommonLocal::SingleAsyncTic

The system will asyncronously call this function 60 times a second to
handle the time-critical functions that we don't want limited to
the frame rate:

sound mixing
user input generation (conditioned by com_asyncInput)
packet server operation
packet client operation

We are not using thread safe libraries, so any functionality put here must
be VERY VERY careful about what it calls.
=================
*/

typedef struct {
	int				milliseconds;			// should always be incremeting by 60hz
	int				deltaMsec;				// should always be one exact 60 Hz tic
	int				timeConsumed;			// msec spent in Com_AsyncThread()
	int				clientPacketsReceived;
	int				serverPacketsReceived;
	int				mostRecentServerPacketSequence;
} asyncStats_t;

static const int MAX_ASYNC_STATS = 1024;
asyncStats_t	com_asyncStats[MAX_ASYNC_STATS];		// indexed by com_ticNumber
int prevAsyncMsec;
double	lastTicMsec;
bool	lastTicMsecValid;

void OpenQ4_GetAsyncTimingStats( openq4AsyncTimingStats_t &stats, int maxSamples ) {
	memset( &stats, 0, sizeof( stats ) );

	if ( maxSamples <= 0 ) {
		return;
	}

	Sys_EnterCriticalSection();

	const int latestTic = com_ticNumber - 1;
	if ( latestTic < 0 ) {
		Sys_LeaveCriticalSection();
		return;
	}

	const int maxAvailableSamples = Min( latestTic + 1, MAX_ASYNC_STATS );
	const int requestedSamples = Min( maxSamples, maxAvailableSamples );
	if ( requestedSamples <= 0 ) {
		Sys_LeaveCriticalSection();
		return;
	}

	const float targetDeltaMsec = 1000.0f / static_cast<float>( USERCMD_HZ );
	float deltaSum = 0.0f;
	float timeConsumedSum = 0.0f;
	float jitterSum = 0.0f;

	for ( int i = 0; i < requestedSamples; ++i ) {
		const asyncStats_t &sample = com_asyncStats[( latestTic - i ) & ( MAX_ASYNC_STATS - 1 )];
		if ( sample.milliseconds <= 0 ) {
			break;
		}

		if ( stats.sampleCount == 0 ) {
			stats.lastDeltaMsec = sample.deltaMsec;
			stats.minDeltaMsec = sample.deltaMsec;
			stats.maxDeltaMsec = sample.deltaMsec;
		} else {
			stats.minDeltaMsec = Min( stats.minDeltaMsec, sample.deltaMsec );
			stats.maxDeltaMsec = Max( stats.maxDeltaMsec, sample.deltaMsec );
		}

		++stats.sampleCount;
		deltaSum += static_cast<float>( sample.deltaMsec );
		timeConsumedSum += static_cast<float>( sample.timeConsumed );
		jitterSum += idMath::Fabs( static_cast<float>( sample.deltaMsec ) - targetDeltaMsec );
	}

	Sys_LeaveCriticalSection();

	if ( stats.sampleCount <= 0 ) {
		return;
	}

	stats.valid = true;
	stats.avgDeltaMsec = deltaSum / static_cast<float>( stats.sampleCount );
	stats.avgHz = stats.avgDeltaMsec > 0.0f ? 1000.0f / stats.avgDeltaMsec : 0.0f;
	stats.avgTimeConsumedMsec = timeConsumedSum / static_cast<float>( stats.sampleCount );
	stats.avgJitterMsec = jitterSum / static_cast<float>( stats.sampleCount );
}

static bool OpenQ4_ShouldUseSmoothSingleplayerSlowTime( void ) {
	if ( cvarSystem == NULL || idAsyncNetwork::IsActive() ) {
		return false;
	}

	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	if ( gameType == NULL || idStr::Icmp( gameType, "singleplayer" ) != 0 ) {
		return false;
	}

	return com_timescale.GetFloat() < 0.999f;
}

void idCommonLocal::SingleAsyncTic( void ) {
	// main thread code can prevent this from happening while modifying
	// critical data structures
	Sys_EnterCriticalSection();

	asyncStats_t *stat = &com_asyncStats[com_ticNumber & (MAX_ASYNC_STATS-1)];
	memset( stat, 0, sizeof( *stat ) );
	stat->milliseconds = Sys_Milliseconds();
	stat->deltaMsec = stat->milliseconds - com_asyncStats[(com_ticNumber - 1) & (MAX_ASYNC_STATS-1)].milliseconds;

	if ( usercmdGen && com_asyncInput.GetBool() ) {
		usercmdGen->UsercmdInterrupt();
	}
// jmarshall
	//switch ( com_asyncSound.GetInteger() ) {
	//	case 1:
	//		soundSystem->AsyncUpdate( stat->milliseconds );
	//		break;
	//	case 3:
	//		soundSystem->AsyncUpdateWrite( stat->milliseconds );
	//		break;
	//}
// jmarshall end

	// we update com_ticNumber after all the background tasks
	// have completed their work for this tic
	com_ticNumber++;

	stat->timeConsumed = Sys_Milliseconds() - stat->milliseconds;

	Sys_LeaveCriticalSection();
}

/*
=================
idCommonLocal::Async
=================
*/
void idCommonLocal::Async( void ) {
	if ( com_shuttingDown ) {
		return;
	}

	int	msec = Sys_Milliseconds();
	if ( !lastTicMsecValid ) {
		lastTicMsec = static_cast<double>( msec ) - GetUserCmdMsecFloat();
		lastTicMsecValid = true;
	}

	if ( !com_preciseTic.GetBool() ) {
		// just run a single tic, even if the exact msec isn't precise
		SingleAsyncTic();
		return;
	}

	double ticMsec = GetUserCmdMsecFloat();

	// the number of msec per tic can be varies with the timescale cvar
	float timescale = com_timescale.GetFloat();
	const bool smoothSlowTime = OpenQ4_ShouldUseSmoothSingleplayerSlowTime();
	if ( !smoothSlowTime && timescale != 1.0f ) {
		ticMsec /= timescale;
		if ( ticMsec < 1.0 ) {
			ticMsec = 1.0;
		}
	}

	// don't skip too many
	if ( smoothSlowTime || timescale == 1.0f ) {
		const double clampedCatchupMsec = 10.0 * GetUserCmdMsecFloat();
		if ( lastTicMsec + clampedCatchupMsec < static_cast<double>( msec ) ) {
			lastTicMsec = static_cast<double>( msec ) - clampedCatchupMsec;
		}
	}

	while ( lastTicMsec + ticMsec <= static_cast<double>( msec ) ) {
		SingleAsyncTic();
		lastTicMsec += ticMsec;
	}
}

static bool OpenQ4_IsMultiplayerGameType( const char *gameType ) {
	return gameType && gameType[0] && idStr::Icmp( gameType, "singleplayer" ) != 0;
}

static bool OpenQ4_IsValidGameModuleName( const char *moduleName ) {
	return moduleName
		&& ( idStr::Icmp( moduleName, "game_sp" ) == 0 || idStr::Icmp( moduleName, "game_mp" ) == 0 );
}

static const char *OpenQ4_SelectGameModuleBaseName( void ) {
	const char *nextModule = cvarSystem->GetCVarString( "com_nextGameModule" );
	if ( OpenQ4_IsValidGameModuleName( nextModule ) ) {
		return idStr::Icmp( nextModule, "game_mp" ) == 0 ? "game_mp" : "game_sp";
	}

	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	return OpenQ4_IsMultiplayerGameType( gameType ) ? "game_mp" : "game_sp";
}

#if defined( _M_X64 ) || defined( __x86_64__ )
	#define OPENQ4_MODULE_ARCH_TAG "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define OPENQ4_MODULE_ARCH_TAG "x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define OPENQ4_MODULE_ARCH_TAG "arm64"
#else
	#define OPENQ4_MODULE_ARCH_TAG "unknown"
#endif
static void OpenQ4_BuildGameModuleBinaryName( const char *moduleName, char outName[ MAX_OSPATH ] ) {
	const char *variant = ( moduleName && idStr::Icmp( moduleName, "game_mp" ) == 0 ) ? "mp" : "sp";
	idStr::snPrintf( outName, MAX_OSPATH, "game-%s_%s", variant, OPENQ4_MODULE_ARCH_TAG );
}

static void OpenQ4_DisableBSEWithWarning( const char *reason, bool showDialog = true ) {
	static bool warnedConsole = false;
	if ( !warnedConsole ) {
		warnedConsole = true;
		common->Warning( "BSE unavailable (%s). Effects will be disabled.", reason ? reason : "unknown reason" );
	}

#if defined( _WIN32 ) && !defined( ID_DEDICATED )
	static bool warnedDialog = false;
	if ( showDialog && !warnedDialog ) {
		warnedDialog = true;

		char message[1024];
		idStr::snPrintf(
			message,
			sizeof( message ),
			"BSE initialization failed.\n\nReason: %s\n\nEffects will be disabled.",
			reason ? reason : "unknown reason"
		);
		::MessageBoxA( NULL, message, "OpenQ4 Warning", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL );
	}
#endif

	::declEffectEdit = NULL;
	::bseAllocDeclEffect = OpenQ4_AllocIntegratedBSEDeclEffect;
	::bse = &bseDisabledLocal;
}

/*
=================
idCommonLocal::AttachBSE
=================
*/
void idCommonLocal::AttachBSE( void ) {
#ifdef __DOOM_DLL__
	::bse = &bseDisabledLocal;
	::declEffectEdit = NULL;
	::bseAllocDeclEffect = OpenQ4_AllocIntegratedBSEDeclEffect;

#if !defined( ID_DEDICATED )
	common->DPrintf( "Attaching integrated BSE.\n" );
	::bse = OpenQ4_GetIntegratedBSEManager();
	::declEffectEdit = OpenQ4_GetIntegratedBSEDeclEffectEdit();
#else
	common->DPrintf( "Attaching integrated BSE decl allocator with disabled runtime manager.\n" );
#endif
#endif
}

/*
=================
idCommonLocal::DetachBSE
=================
*/
void idCommonLocal::DetachBSE( void ) {
#ifdef __DOOM_DLL__
	::bse = &bseDisabledLocal;
	::declEffectEdit = NULL;
	::bseAllocDeclEffect = NULL;
#endif
}

/*
=================
idCommonLocal::LoadGameDLL
=================
*/
void idCommonLocal::LoadGameDLL( void ) {
#ifdef __DOOM_DLL__
	char			dllPath[ MAX_OSPATH ];
	char			preferredGameModuleBinary[ MAX_OSPATH ];
	const char *	selectedModuleBinary;

	gameImport_t	gameImport;
	gameExport_t	gameExport;
	GetGameAPI_t	GetGameAPI;

	const char *gameModuleBaseName = OpenQ4_SelectGameModuleBaseName();
	OpenQ4_BuildGameModuleBinaryName( gameModuleBaseName, preferredGameModuleBinary );
	selectedModuleBinary = preferredGameModuleBinary;
	fileSystem->FindDLL( selectedModuleBinary, dllPath, true );

	if ( !dllPath[ 0 ] ) {
		// Legacy fallback for existing installs that still ship game_sp/game_mp.
		selectedModuleBinary = gameModuleBaseName;
		fileSystem->FindDLL( selectedModuleBinary, dllPath, true );
	}

	if ( !dllPath[ 0 ] ) {
		common->FatalError(
			"couldn't find game dynamic library '%s' (or legacy '%s')",
			preferredGameModuleBinary,
			gameModuleBaseName
		);
		return;
	}
	common->DPrintf( "Loading game DLL: '%s'\n", dllPath );
	gameDLL = sys->DLL_Load( dllPath );
	if ( !gameDLL ) {
		common->FatalError( "couldn't load game dynamic library '%s'", selectedModuleBinary );
		return;
	}

	GetGameAPI = (GetGameAPI_t) Sys_DLL_GetProcAddress( gameDLL, "GetGameAPI" );
	if ( !GetGameAPI ) {
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError( "couldn't find game DLL API" );
		return;
	}

	gameImport.version					= GAME_API_VERSION;
	gameImport.sys						= ::sys;
	gameImport.common					= ::common;
	gameImport.cmdSystem				= ::cmdSystem;
	gameImport.cvarSystem				= ::cvarSystem;
	gameImport.fileSystem				= ::fileSystem;
	gameImport.networkSystem			= ::networkSystem;
	gameImport.renderSystem				= ::renderSystem;
	gameImport.soundSystem				= ::soundSystem;
	gameImport.renderModelManager		= ::renderModelManager;
	gameImport.uiManager				= ::uiManager;
	gameImport.declManager				= ::declManager;
	gameImport.AASFileManager			= ::AASFileManager;
	gameImport.collisionModelManager	= ::collisionModelManager;
	gameImport.bse						= ::bse;

	gameExport							= *GetGameAPI( &gameImport );

	if ( gameExport.version != GAME_API_VERSION ) {
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError( "wrong game DLL API version" );
		return;
	}

	game								= gameExport.game;
	gameEdit							= gameExport.gameEdit;
	com_activeGameModule.SetString( gameModuleBaseName );
	com_nextGameModule.SetString( "" );

#endif

	// initialize the game object
	if ( game != NULL ) {
		game->Init();
	}
}

/*
=================
idCommonLocal::UnloadGameDLL
=================
*/
void idCommonLocal::UnloadGameDLL( void ) {

	// shut down the game object
	if ( game != NULL ) {
		game->Shutdown();
	}

#ifdef __DOOM_DLL__

	if ( gameDLL ) {
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
	}
	game = NULL;
	gameEdit = NULL;
	com_activeGameModule.SetString( "" );

#endif
}

/*
=================
idCommonLocal::IsInitialized
=================
*/
bool idCommonLocal::IsInitialized( void ) const {
	return com_fullyInitialized;
}

/*
=================
idCommonLocal::SetMachineSpec
=================
*/
void idCommonLocal::SetMachineSpec( void ) {
	const double cpuHz = Sys_ClockTicksPerSecond();
	double ghz = cpuHz * 0.000000001f;
	int vidRam = Sys_GetVideoRam();
	int sysRam = Sys_GetSystemRam();
	bool oldCard = false;
	bool nv10or20 = false;

	renderSystem->GetCardCaps( oldCard, nv10or20 );

	Printf( "Detected\n\tCPU: %s\n\tSystem memory: %s\n\tVideo memory: %s on %s\n\n",
		Sys_GetProcessorString(),
		Sys_FormatMemoryMB( sysRam ).c_str(),
		Sys_FormatMemoryMB( vidRam ).c_str(),
		( oldCard ) ? "a less than optimal video architecture" : "an optimal video architecture" );

	if ( ghz >= 3.0f && vidRam >= 6144 && sysRam >= 16384 && !oldCard ) {
		Printf( "This system qualifies for Ultra quality!\n" );
		com_machineSpec.SetInteger( 3 );
	} else if ( ghz >= 2.5f && vidRam >= 4096 && sysRam >= 8192 && !oldCard ) {
		Printf( "This system qualifies for High quality!\n" );
		com_machineSpec.SetInteger( 2 );
	} else if ( ghz >= 2.0f && vidRam >= 2048 && sysRam >= 6144 ) {
		Printf( "This system qualifies for Medium quality.\n" );
		com_machineSpec.SetInteger( 1 );
	} else {
		Printf( "This system qualifies for Low quality.\n" );
		com_machineSpec.SetInteger( 0 );
	}
	com_videoRam.SetInteger( vidRam );
}

/*
=================
idCommonLocal::Init
=================
*/
void idCommonLocal::Init( int argc, const char **argv, const char *cmdline ) {
	try {

		// set interface pointers used by idLib
		idLib::sys			= sys;
		idLib::common		= common;
		idLib::cvarSystem	= cvarSystem;
		idLib::fileSystem	= fileSystem;

		// initialize idLib
		idLib::Init();

		// clear warning buffer
		ClearWarnings( GAME_NAME " initialization" );
		
		// parse command line options
		idCmdArgs args;
		if ( cmdline ) {
			// tokenize if the OS doesn't do it for us
			args.TokenizeString( cmdline, true );
			argv = args.GetArgs( &argc );
		}
		ParseCommandLine( argc, argv );

		// init console command system
		cmdSystem->Init();

		// init CVar system
		cvarSystem->Init();

		// start file logging right away, before early console or whatever
		StartupVariable( "win_outputDebugString", false );

		// register all static CVars
		idCVar::RegisterStaticVars();

		// print engine version
		Printf( "%s\n", buildInfo.string );

		// initialize key input/binding, done early so bind command exists
		idKeyInput::Init();

		// init the console so we can take prints
		console->Init();

		// get architecture info
		Sys_Init();

		// initialize networking
		Sys_InitNetworking();

		// override cvars from command line
		StartupVariable( NULL, false );

		if ( !idAsyncNetwork::serverDedicated.GetInteger() && Sys_AlreadyRunning() ) {
			Sys_Quit();
		}

		// initialize processor specific SIMD implementation
		InitSIMD();

		// init commands
		InitCommands();

#ifdef ID_WRITE_VERSION
		config_compressor = idCompressor::AllocArithmetic();
#endif

		// game specific initialization
		InitGame();

		// dump parsed command line for diagnostics (only when developer + logFile are enabled)
		if ( com_developer.GetBool() && com_logFile.GetInteger() ) {
			Printf( "Command line (parsed):\n" );
			for ( int i = 0; i < com_numConsoleLines; ++i ) {
				if ( !com_consoleLines[ i ].Argc() ) {
					continue;
				}
				Printf( "  %d:", i );
				for ( int j = 0; j < com_consoleLines[ i ].Argc(); ++j ) {
					Printf( " %s", com_consoleLines[ i ].Argv( j ) );
				}
				Printf( "\n" );
			}
			Printf( "CVar snapshot: g_autoScreenshot=%s com_autoScreenshot=%s\n",
				cvarSystem->GetCVarString( "g_autoScreenshot" ),
				cvarSystem->GetCVarString( "com_autoScreenshot" ) );
		}

		if ( !AddStartupCommands() ) {
			// if the user didn't give any commands, run default action
			session->StartMenu( true );
		}

		if ( com_WriteSingleDeclFile.GetBool() ) {
			declManager->WriteDeclFile();
		}

		Printf( "--- Common Initialization Complete ---\n" );

		// print all warnings queued during initialization
		PrintWarnings();

#ifdef	ID_DEDICATED
		Printf( "\nType 'help' for dedicated server info.\n\n" );
#endif

		// remove any prints from the notify lines
		console->ClearNotifyLines();
		
		ClearCommandLine();

		com_fullyInitialized = true;
	}

	catch( idException & ) {
		Sys_Error( "Error during initialization" );
	}
}


/*
=================
idCommonLocal::Shutdown
=================
*/
void idCommonLocal::Shutdown( void ) {

	com_shuttingDown = true;

	idAsyncNetwork::server.Kill();
	idAsyncNetwork::client.Shutdown();

	// game specific shut down
	ShutdownGame( false );

	// shut down non-portable system services
	Sys_Shutdown();
	RenderDoc_Shutdown();

	// shut down the console
	console->Shutdown();

	// shut down the key system
	idKeyInput::Shutdown();

	// shut down the cvar system
	cvarSystem->Shutdown();

	// shut down the console command system
	cmdSystem->Shutdown();

#ifdef ID_WRITE_VERSION
	delete config_compressor;
	config_compressor = NULL;
#endif

	// free any buffered warning messages
	ClearWarnings( GAME_NAME " shutdown" );
	warningCaption.Clear();
	errorList.Clear();

	// free language dictionary
	languageDict.Clear();

	// enable leak test
	Mem_EnableLeakTest( "doom" );

	// shutdown idLib
	idLib::ShutDown();
}

/*
=================
idCommonLocal::InitGame
=================
*/
void idCommonLocal::InitGame( void ) {
	// initialize the file system
	fileSystem->Init();

	// attach the integrated BSE manager before decl initialization so DECL_EFFECT
	// allocation is available when effect declarations are parsed.
	AttachBSE();

	// initialize the declaration manager
	declManager->Init();

	// force r_fullscreen 0 if running a tool
	CheckToolMode();

	idFile *file = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( CONFIG_SPEC, "fs_savepath" ) );
	bool sysDetect = ( file == NULL );
	if ( file ) {
		fileSystem->CloseFile( file );
	} else {
		file = fileSystem->OpenFileWrite( CONFIG_SPEC );
		fileSystem->CloseFile( file );
	}
	
	idCmdArgs args;
	if ( sysDetect ) {
		SetMachineSpec();
		Com_ExecMachineSpec_f( args );
	}

	// initialize the renderSystem data structures, but don't start OpenGL yet
	renderSystem->Init();

	// initialize string database right off so we can use it for loading messages
	InitLanguageDict();

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104343" ) );

	// load the font, etc
	console->LoadGraphics();

	// init journalling, etc
	eventLoop->Init();

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104343" ) );

	// exec the startup scripts
	if ( fileSystem->ReadFile( "editor.cfg", NULL ) >= 0 ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec editor.cfg\n" );
	}
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec default.cfg\n" );
	if ( fileSystem->ReadFile( "openq4_defaults.cfg", NULL ) >= 0 ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec openq4_defaults.cfg\n" );
	}
	const idStr platformProfileConfig = Common_BuildPlatformProfileConfigName( com_platformProfile.GetString() );
	if ( platformProfileConfig.Length() > 0 && fileSystem->ReadFile( platformProfileConfig, NULL ) >= 0 ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "exec %s\n", platformProfileConfig.c_str() ) );
	}

	// skip the config file if "safe" is on the command line
	if ( !SafeMode() ) {
		if ( fileSystem->ReadFile( CONFIG_FILE, NULL ) >= 0 ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec " CONFIG_FILE "\n" );
		}
	}
	if ( fileSystem->ReadFile( "autoexec.cfg", NULL ) >= 0 ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec autoexec.cfg\n" );
	}

	// reload the language dictionary now that we've loaded config files
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reloadLanguage\n" );

	// run cfg execution
	cmdSystem->ExecuteCommandBuffer();

	// re-override anything from the config files with command line args
	StartupVariable( NULL, false );

	Common_MigrateLegacyConsoleAllowCVar();

	// if any archived cvars are modified after this, we will trigger a writing of the config file
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );

	// cvars are initialized, but not the rendering system. Allow preference startup dialog
	Sys_DoPreferences();

	// init the user command input code
	usercmdGen->Init();

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104346" ) );

	// start the sound system, but don't do any hardware operations yet
	soundSystem->Init();

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104347" ) );

	// init async network
	idAsyncNetwork::Init();

#ifdef	ID_DEDICATED
	idAsyncNetwork::server.InitPort();
	cvarSystem->SetCVarBool( "s_noSound", true );
#else
	if ( idAsyncNetwork::serverDedicated.GetInteger() == 1 ) {
		idAsyncNetwork::server.InitPort();
		cvarSystem->SetCVarBool( "s_noSound", true );
	} else {
		// init OpenGL, which will open a window and connect sound and input hardware
		PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104348" ) );
		InitRenderSystem();
	}
#endif

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104349" ) );

	// initialize the user interfaces
	uiManager->Init();

	// initialize the BSE system before the game DLL starts creating effects
	if ( bse && !bse->Init() ) {
		OpenQ4_DisableBSEWithWarning( "BSE initialization failed" );
	}

	// startup the script debugger
	// DebuggerServerInit();

	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104350" ) );

	// load the game dll
	LoadGameDLL();
	
	PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104351" ) );

	// init the session
	session->Init();

	// have to do this twice.. first one sets the correct r_mode for the renderer init
	// this time around the backend is all setup correct.. a bit fugly but do not want
	// to mess with all the gl init at this point.. an old vid card will never qualify for 
	if ( sysDetect ) {
		SetMachineSpec();
		Com_ExecMachineSpec_f( args );
		cvarSystem->SetCVarInteger( "s_numberOfSpeakers", 6 );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );
		cmdSystem->ExecuteCommandBuffer();
	}
}

/*
=================
idCommonLocal::ShutdownGame
=================
*/
void idCommonLocal::ShutdownGame( bool reloading ) {

	// kill sound first
	//idSoundWorld *sw = soundSystem->GetPlayingSoundWorld();
	//if ( sw ) {
	//	sw->StopAllSounds();
	//}
	//soundSystem->ClearBuffer();

	// shutdown the script debugger
	// DebuggerServerShutdown();

	idAsyncNetwork::client.Shutdown();

	// shut down the session
	session->Shutdown();

	// shut down the user interfaces
	uiManager->Shutdown();

	// shut down the sound system
	soundSystem->Shutdown();

	// shut down async networking
	idAsyncNetwork::Shutdown();

	// shut down the user command input code
	usercmdGen->Shutdown();

	// shut down the event loop
	eventLoop->Shutdown();

	// shut down the renderSystem
	renderSystem->Shutdown();

	// shutdown the decl manager while the game DLL is still loaded, because
	// game-owned decl types (e.g. entity/fx decls) can have module-local vtables.
	declManager->Shutdown();

	// unload the game dll after game-owned decl instances are released
	UnloadGameDLL();

	// shut down the BSE manager after game/decl data has been released
	if ( bse ) {
		bse->Shutdown();
	}
	DetachBSE();

	// dump warnings to "warnings.txt"
#ifdef DEBUG
	DumpWarnings();
#endif
	// only shut down the log file after all output is done
	CloseLogFile();

	// shut down the file system
	fileSystem->Shutdown( reloading );
}
