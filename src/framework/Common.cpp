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
#include "../imagetools/ImageTools.h"
#include "../render_geo/RenderGeometry.h"
#include "../renderer/RendererModule.h"
#include "ArenaCampaign.h"
#include "GameModuleDiagnostics.h"
#include "RenderDoc.h"
#include "ParallelJobSystem.h"
#include "../sys/NetworkEndpoint.h"

#if defined( USE_SDL3 )
#include <SDL3/SDL_locale.h>
#endif

#define	MAX_PRINT_MSG_SIZE	4096
#define MAX_WARNING_LIST	256

void openQ4_PrintFramePacingSnapshot( const char *reason );
void openQ4_RecordMultiplayerFramePacing( int frameStartMsec );

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

static void Common_WriteLogIdentityBanner( idFile *file ) {
	if ( file == NULL ) {
		return;
	}

	char line[sizeof( version.string ) + 2];
	idStr::snPrintf( line, sizeof( line ), "%s\n", version.string );
	file->Write( line, idLib::SizeToInt( strlen( line ), "Common_WriteLogIdentityBanner" ) );
}

idCVar com_version( "si_version", version.string, CVAR_SYSTEM|CVAR_ROM|CVAR_SERVERINFO, "engine version" );
idCVar com_buildInfo( "com_buildInfo", buildInfo.string, CVAR_SYSTEM|CVAR_ROM, "detailed engine build information" );
idCVar com_skipRenderer( "com_skipRenderer", "0", CVAR_BOOL|CVAR_SYSTEM, "skip the renderer completely" );
idCVar com_machineSpec( "com_machineSpec", "-1", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM, "hardware classification, -1 = not detected, 0 = low quality, 1 = medium quality, 2 = high quality, 3 = ultra quality" );
const char *com_performancePresetArgs[] = { "minimum", "lowpower", "performance", "balanced", "quality", "ultra", NULL };
static const char *com_performancePresetValueStrings[] = { "balanced", "minimum", "lowpower", "performance", "quality", "ultra", NULL };
idCVar com_performancePreset( "com_performancePreset", "balanced", CVAR_ARCHIVE | CVAR_SYSTEM, "coherent system performance preset: minimum, lowpower, performance, balanced, quality, ultra", com_performancePresetValueStrings, idCmdSystem::ArgCompletion_String<com_performancePresetArgs> );
idCVar com_purgeAll( "com_purgeAll", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM, "purge everything between level loads" );
idCVar com_WriteSingleDeclFile( "com_WriteSingleDeclFile", "0", CVAR_SYSTEM | CVAR_BOOL, "write a packed decl file after startup or map loads; use com_singleDeclFileWriteMode for openQ4 or exact-retail game-type coverage" );
idCVar com_memoryMarker( "com_memoryMarker", "-1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_INIT, "used as a marker for memory stats" );
idCVar com_preciseTic( "com_preciseTic", "1", CVAR_BOOL|CVAR_SYSTEM, "run one game tick every async thread update" );
idCVar com_asyncInput( "com_asyncInput", "0", CVAR_BOOL|CVAR_SYSTEM, "sample input from the async thread" );
#if defined( ID_DEDICATED )
static const char *OPENQ4_JOBS_DETERMINISTIC_DEFAULT = "1";
#else
static const char *OPENQ4_JOBS_DETERMINISTIC_DEFAULT = "0";
#endif
idCVar jobs_enable( "jobs_enable", "1", CVAR_BOOL | CVAR_ARCHIVE | CVAR_INIT | CVAR_SYSTEM, "enable the portable job service; 0 preserves all work by executing job lists synchronously" );
idCVar jobs_deterministic( "jobs_deterministic", OPENQ4_JOBS_DETERMINISTIC_DEFAULT, CVAR_BOOL | CVAR_ARCHIVE | CVAR_INIT | CVAR_SYSTEM, "execute every job inline in deterministic insertion order (default on dedicated servers)" );
idCVar jobs_numThreads( "jobs_numThreads", "0", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_INIT | CVAR_SYSTEM, "portable job worker threads (0 = automatic, 1..32 = explicit)", 0, 32, idCmdSystem::ArgCompletion_Integer<0,32> );
idCVar jobs_queueCapacity( "jobs_queueCapacity", "64", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_INIT | CVAR_SYSTEM, "maximum admitted job lists; saturated submissions fail explicitly and remain retryable", 1, 1024, idCmdSystem::ArgCompletion_Integer<1,1024> );
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
idCVar com_showFPS( "com_showFPS", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "show frames rendered per second: 1 = in-game only, 2 = legacy always on", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
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
idCVar com_activeGameModule( "com_activeGameModule", "", CVAR_SYSTEM | CVAR_NOCHEAT, "active game module (game_sp/game_mp)" );
idCVar com_nextGameModule( "com_nextGameModule", "", CVAR_SYSTEM | CVAR_NOCHEAT, "internal one-shot game module override for reloadEngine" );
idCVar com_platformProfile( "com_platformProfile", "default", CVAR_SYSTEM | CVAR_INIT, "startup platform profile (default or steamdeck)" );

static bool openQ4_IsValidGameModuleName( const char *moduleName );

idCVar com_product_lang_ext( "com_product_lang_ext", "1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE, "Extension to use when creating language files." );
idCVar r_skipGlowOverlay( "r_skipGlowOverlay", "0", CVAR_ARCHIVE | CVAR_RENDERER, "skip glow overlays when non-zero" );
static idCVar r_borderlessDefaultMigrated( "r_borderlessDefaultMigrated", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "one-time migration flag for legacy bordered window defaults" );
static idCVar net_rateDefaultMigrated( "net_rateDefaultMigrated", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "one-time migration flag for the legacy 25600 B/s network rate caps" );

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

static void Common_MigrateLinuxLegacyLowVRamTexturePreset( void ) {
#if defined( __linux__ )
	const int archivedVideoRam = com_videoRam.GetInteger();
	if ( archivedVideoRam >= 128 ) {
		return;
	}

	const bool legacyTextureCap =
		cvarSystem->GetCVarBool( "image_ignoreHighQuality" ) &&
		cvarSystem->GetCVarInteger( "image_downSize" ) != 0 &&
		cvarSystem->GetCVarInteger( "image_downSizeLimit" ) == 256;
	if ( !legacyTextureCap ) {
		return;
	}

	const int detectedVideoRam = Sys_GetVideoRam();
	if ( detectedVideoRam < 128 ) {
		return;
	}

	common->Printf(
		"Migrating legacy Linux VRAM config: replacing archived %dMB texture downsize preset with detected %dMB VRAM\n",
		archivedVideoRam,
		detectedVideoRam );
	com_videoRam.SetInteger( detectedVideoRam );
	cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 0, CVAR_ARCHIVE );
	cvarSystem->SetCVarInteger( "image_downSize", 0, CVAR_ARCHIVE );
	cvarSystem->SetCVarInteger( "image_downSizeLimit", 0, CVAR_ARCHIVE );
	cvarSystem->SetCVarInteger( "image_downSizeSpecular", 0, CVAR_ARCHIVE );
	cvarSystem->SetCVarInteger( "image_downSizeBump", 0, CVAR_ARCHIVE );
#endif
}

static void Common_MigrateLegacyBorderlessWindowDefault( void ) {
#if defined( _WIN32 )
	if ( r_borderlessDefaultMigrated.GetBool() ) {
		return;
	}

	if ( !cvarSystem->GetCVarBool( "r_borderless" ) ) {
		common->Printf( "Migrating legacy display config: enabling borderless windowed presentation\n" );
		cvarSystem->SetCVarBool( "r_borderless", true, CVAR_ARCHIVE );
	}

	r_borderlessDefaultMigrated.SetBool( true );
#endif
}

/*
==================
Common_MigrateLegacyNetworkRateCaps

net_serverMaxClientRate / net_clientMaxRate are archived, so raising their
defaults reaches new installs only.  Everyone who has ever run openQ4 keeps the
25600 B/s these shipped with, and that cap does not shed bytes - the channel's
token bucket refuses whole snapshots until the previous packet has been paid
for, so any snapshot over ~1280 bytes drags the update rate below the one the
server was asked for.  Measured on a listen server, a client being served ~2.9KB
snapshots got 2-3 a second instead of 20.

Only the exact legacy default is rewritten: anyone who deliberately chose a rate
did not type 25600, and their choice is left alone.  Once.
==================
*/
static void Common_MigrateLegacyNetworkRateCaps( void ) {
	if ( net_rateDefaultMigrated.GetBool() ) {
		return;
	}

	// Kept in step with the defaults in idAsyncNetwork; tools/tests/network_security.py
	// pins the pair so they cannot drift apart silently.
	static const int legacyNetworkRate = 25600;
	static const int modernNetworkRate = 128000;
	static const char * const rateCVars[] = { "net_serverMaxClientRate", "net_clientMaxRate" };
	for ( int i = 0; i < 2; i++ ) {
		if ( cvarSystem->GetCVarInteger( rateCVars[i] ) != legacyNetworkRate ) {
			continue;
		}
		common->Printf( "Migrating legacy network config: %s %d -> %d B/s\n",
						rateCVars[i], legacyNetworkRate, modernNetworkRate );
		cvarSystem->SetCVarInteger( rateCVars[i], modernNetworkRate, CVAR_ARCHIVE );
	}

	net_rateDefaultMigrated.SetBool( true );
}

static int commonLastPresentationCap = -1;
static double commonNextPresentationFrameClock = 0.0;
static bool commonNextPresentationFrameValid = false;

static void Common_ResetPresentationThrottle( void ) {
	commonNextPresentationFrameClock = 0.0;
	commonNextPresentationFrameValid = false;
}

static double Common_GetPresentationClockNow( void ) {
	return Sys_GetClockTicks();
}

static double Common_GetPresentationClockUnitsPerSecond( void ) {
	return Sys_ClockTicksPerSecond();
}

static int Common_GetRequestedPresentationCap( void ) {
	int cap = Max( 0, com_maxfps.GetInteger() );

#if defined( _WIN32 )
	// Hidden windows should not spin uncapped when presentation is decoupled. Note
	// Win32 IsWindowVisible() reports TRUE for minimized windows, so in practice this
	// only fires for genuinely hidden ones (r_hiddenWindow, the light-grid bake).
	if ( !Sys_IsWindowVisible() && ( cap == 0 || cap > 60 ) ) {
		cap = 60;
	}
#endif

	return cap;
}

int openQ4_GetRequestedPresentationCap( void ) {
	return Common_GetRequestedPresentationCap();
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

	// Never wait longer than a few frame periods here however the clock behaves. The
	// tail of this loop is a deliberate busy-spin for sub-millisecond precision, so a
	// counter that stalls or runs backwards would otherwise wedge the process with no
	// way out.
	const double waitDeadlineClock = Common_GetPresentationClockNow() + frameClockUnits * 4.0;
	while ( true ) {
		const double spinNowClock = Common_GetPresentationClockNow();
		const double remainingClockUnits = commonNextPresentationFrameClock - spinNowClock;
		if ( remainingClockUnits <= 0.0 ) {
			break;
		}

		if ( spinNowClock >= waitDeadlineClock ) {
			// The clock is not behaving; drop the backlog and resynchronise next frame
			// rather than spinning against a target we can never reach.
			Common_ResetPresentationThrottle();
			commonLastPresentationCap = cap;
			return;
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

void openQ4_BeginPresentationFrame( void ) {
	if ( idAsyncNetwork::serverDedicated.GetInteger() == 1 ) {
		Common_ResetPresentationThrottle();
	} else {
		Common_ThrottlePresentationFrame();
	}

	com_frameRealTime = Sys_Milliseconds();
}

// While the single-player loading-screen continue gate is waiting for input, the
// mouse is neither captured nor routed to a menu GUI, so the platform backends
// would normally discard button events. This flag tells them to queue buttons
// as key events so a click can dismiss the gate.
static bool commonLoadingContinueInputActive = false;

void openQ4_SetLoadingContinueInputActive( bool active ) {
	commonLoadingContinueInputActive = active;
}

bool openQ4_AcceptingLoadingContinueInput( void ) {
	return commonLoadingContinueInputActive;
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
openQ4_GetActiveToolFlags
==================
*/
int openQ4_GetActiveToolFlags( int flags ) {
	if ( flags == EDITOR_ALL ) {
		return com_editors;
	}

	return com_editors & flags;
}

/*
==================
openQ4_IsAnyToolActive
==================
*/
bool openQ4_IsAnyToolActive( void ) {
	return com_editorActive || openQ4_GetActiveToolFlags( EDITOR_ALL ) != 0;
}

/*
==================
openQ4_ToolPrint
==================
*/
void openQ4_ToolPrint( const char *text ) {
	bool toolPrinted = false;

	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

#ifdef ID_ALLOW_TOOLS
	if ( openQ4_GetActiveToolFlags( EDITOR_DECL ) != 0 ) {
		toolPrinted = DeclBrowserPrint( text ) || toolPrinted;
	}

	if ( openQ4_GetActiveToolFlags( EDITOR_DEBUGGER ) != 0 ) {
		DebuggerServerPrint( text );
		toolPrinted = true;
	}

	if ( openQ4_GetActiveToolFlags( EDITOR_RADIANT ) != 0 ) {
		RadiantPrint( text );
		toolPrinted = true;
	}

	if ( openQ4_GetActiveToolFlags( EDITOR_MATERIAL ) != 0 ) {
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
openQ4_ShouldCacheEntityDefMedia
==================
*/
bool openQ4_ShouldCacheEntityDefMedia( bool noCaching ) {
	return !noCaching && openQ4_GetActiveToolFlags( OPENQ4_ENTITYDEF_MEDIA_CACHE_TOOL_MASK ) == 0;
}

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

static const char *Common_GetNonEmptyEnv( const char *name ) {
	const char *value = getenv( name );
	return ( value != NULL && value[0] != '\0' ) ? value : NULL;
}

static bool Common_IsEnvFlagFalse( const char *value ) {
	return value != NULL &&
		( idStr::Icmp( value, "0" ) == 0 ||
		  idStr::Icmp( value, "false" ) == 0 ||
		  idStr::Icmp( value, "no" ) == 0 ||
		  idStr::Icmp( value, "off" ) == 0 );
}

static bool Common_IsEnvFlagTrue( const char *value ) {
	return value != NULL &&
		( idStr::Icmp( value, "1" ) == 0 ||
		  idStr::Icmp( value, "true" ) == 0 ||
		  idStr::Icmp( value, "yes" ) == 0 ||
		  idStr::Icmp( value, "on" ) == 0 ||
		  idStr::Icmp( value, "steamdeck" ) == 0 );
}

#if defined( __linux__ )
static bool Common_FileContainsAnyToken( const char *path, const char **tokens, int numTokens ) {
	FILE *file = fopen( path, "r" );
	if ( file == NULL ) {
		return false;
	}

	char buffer[512];
	while ( fgets( buffer, sizeof( buffer ), file ) != NULL ) {
		idStr line = buffer;
		for ( int i = 0; i < numTokens; ++i ) {
			if ( tokens[i] != NULL && line.Find( tokens[i], false ) >= 0 ) {
				fclose( file );
				return true;
			}
		}
	}

	fclose( file );
	return false;
}
#endif

static bool Common_HasSteamDeckHostSignal( void ) {
	const char *explicitSignals[] = {
		"OPENQ4_STEAMDECK",
		"OPENQ4_AUTODETECT_STEAMDECK",
		"SteamDeck",
		"STEAM_DECK",
		"STEAMDECK",
		"steamdeck",
		"SteamOS",
		"STEAMOS",
		"steamos"
	};
	for ( int i = 0; i < static_cast<int>( sizeof( explicitSignals ) / sizeof( explicitSignals[0] ) ); ++i ) {
		const char *value = Common_GetNonEmptyEnv( explicitSignals[i] );
		if ( Common_IsEnvFlagFalse( value ) ) {
			return false;
		}
		if ( Common_IsEnvFlagTrue( value ) ) {
			return true;
		}
	}

#if defined( __linux__ )
	const char *dmiTokens[] = {
		"steam deck",
		"jupiter",
		"galileo"
	};
	if ( Common_FileContainsAnyToken( "/sys/devices/virtual/dmi/id/product_name", dmiTokens, static_cast<int>( sizeof( dmiTokens ) / sizeof( dmiTokens[0] ) ) ) ||
		 Common_FileContainsAnyToken( "/sys/devices/virtual/dmi/id/board_name", dmiTokens, static_cast<int>( sizeof( dmiTokens ) / sizeof( dmiTokens[0] ) ) ) ) {
		return true;
	}

	const char *steamOsTokens[] = {
		"ID=steamos",
		"ID_LIKE=steamos",
		"VARIANT_ID=steamdeck",
		"SteamOS"
	};
	if ( Common_FileContainsAnyToken( "/etc/os-release", steamOsTokens, static_cast<int>( sizeof( steamOsTokens ) / sizeof( steamOsTokens[0] ) ) ) ||
		 Common_FileContainsAnyToken( "/run/host/os-release", steamOsTokens, static_cast<int>( sizeof( steamOsTokens ) / sizeof( steamOsTokens[0] ) ) ) ) {
		return true;
	}
#endif

	return false;
}

static bool Common_HasRaspberryPiHostSignal( void ) {
	const char *explicitSignals[] = {
		"OPENQ4_RASPBERRYPI",
		"OPENQ4_RPI",
		"OPENQ4_AUTODETECT_RASPBERRYPI",
		"RaspberryPi",
		"RASPBERRY_PI",
		"raspberrypi"
	};
	for ( int i = 0; i < static_cast<int>( sizeof( explicitSignals ) / sizeof( explicitSignals[0] ) ); ++i ) {
		const char *value = Common_GetNonEmptyEnv( explicitSignals[i] );
		if ( Common_IsEnvFlagFalse( value ) ) {
			return false;
		}
		if ( Common_IsEnvFlagTrue( value ) ) {
			return true;
		}
	}

#if defined( __linux__ )
	const char *piTokens[] = {
		"raspberry pi"
	};
	if ( Common_FileContainsAnyToken( "/proc/device-tree/model", piTokens, static_cast<int>( sizeof( piTokens ) / sizeof( piTokens[0] ) ) ) ||
		 Common_FileContainsAnyToken( "/sys/firmware/devicetree/base/model", piTokens, static_cast<int>( sizeof( piTokens ) / sizeof( piTokens[0] ) ) ) ||
		 Common_FileContainsAnyToken( "/proc/cpuinfo", piTokens, static_cast<int>( sizeof( piTokens ) / sizeof( piTokens[0] ) ) ) ) {
		return true;
	}
#endif

	return false;
}

static bool Common_HasExplicitLowPowerHostSignal( void ) {
	const char *explicitSignals[] = {
		"OPENQ4_LOWPOWER",
		"OPENQ4_LOW_POWER",
		"OPENQ4_AUTODETECT_LOWPOWER"
	};
	for ( int i = 0; i < static_cast<int>( sizeof( explicitSignals ) / sizeof( explicitSignals[0] ) ); ++i ) {
		const char *value = Common_GetNonEmptyEnv( explicitSignals[i] );
		if ( Common_IsEnvFlagFalse( value ) ) {
			return false;
		}
		if ( Common_IsEnvFlagTrue( value ) ) {
			return true;
		}
	}
	return false;
}

static bool Common_HostCpuIsArm64( void ) {
#if defined( __aarch64__ ) || defined( _M_ARM64 ) || defined( __arm64__ )
	return true;
#else
	return false;
#endif
}

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
	virtual void				SetGameTimeScale( float scale );
	virtual float				GetGameTimeScale( void ) const;
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
	virtual const char *		KeysFromBindingForPrompt( const char *bind );
	virtual const char *		BindingFromKey( const char *key );

	virtual int					ButtonState( int key );
	virtual int					KeyState( int key );

	void						InitGame( void );
	void						ShutdownGame( bool reloading );
	void						PrintLoadingMessage( const char *msg );

	// localization
	void						InitLanguageDict( bool applyStartupSysLang, bool allowAutoLanguageSelect );
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
	void						ApplyAutomaticPlatformProfile( void );

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
	bool						gameShutdownCalled;
	bool						gameShutdownAfterDeclsCalled;

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

	errorMessage[0] = '\0';

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;

	gameDLL = 0;
	gameShutdownCalled = false;
	gameShutdownAfterDeclsCalled = false;

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
	if ( idStr::Icmpn( buff, EDITOR_WINDOWTEXT, idStr::Length( EDITOR_WINDOWTEXT ) ) == 0 ) {
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
		timeLength = idLib::SizeToInt( strlen( msg ), "idCommonLocal::VPrintf timestamp" );
	} else {
		timeLength = 0;
	}

	// don't overflow
	if ( idStr::vsnPrintf( msg+timeLength, MAX_PRINT_MSG_SIZE-timeLength-1, fmt, args ) < 0 ) {
		msg[sizeof(msg)-2] = '\n'; msg[sizeof(msg)-1] = '\0'; // avoid output garbling
		Sys_Printf( "idCommon::VPrintf: truncated to %zu characters\n", strlen(msg)-1 );
	}

	if ( rd_buffer ) {
		const char *text = msg;
		while ( *text ) {
			int used = idLib::SizeToInt( strlen( rd_buffer ), "idCommonLocal::VPrintf redirect buffer" );
			int available = rd_buffersize - used - 1;
			if ( available <= 0 ) {
				rd_flush( rd_buffer );
				*rd_buffer = 0;
				used = 0;
				available = rd_buffersize - 1;
			}
			if ( available <= 0 ) {
				break;
			}

			const int remaining = idLib::SizeToInt( strlen( text ), "idCommonLocal::VPrintf redirect text" );
			const int copyLength = Min( available, remaining );
			memcpy( rd_buffer + used, text, copyLength );
			rd_buffer[used + copyLength] = '\0';
			text += copyLength;

			if ( *text ) {
				rd_flush( rd_buffer );
				*rd_buffer = 0;
			}
		}
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

			Common_WriteLogIdentityBanner( logFile );
			Printf( "log file '%s' opened on %s\n", fileNameToOpen, asctime( newtime ) );
		}
		if ( logFile ) {
			logFile->Write( msg, idLib::SizeToInt( strlen( msg ), "idCommonLocal::VPrintf log message" ) );
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
	openQ4_PrintFramePacingSnapshot( reason );
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
Com_FlushBufferedOutput

Sys_Printf reaches the terminal through stdio, which buffers. The error paths
below finish in Sys_Error, and a fault raised by the teardown they run first
kills the process through _exit(), which discards every buffered stream: the
terminal ends mid-sentence several KB behind the message that explains the exit,
pointing at whichever subsystem happened to be printing (issue #96). Push the
buffers out before running teardown that could fault.
==================
*/
static void Com_FlushBufferedOutput( void ) {
	fflush( NULL );
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
	// (module-only builds have a NULL renderSystem until the renderer module
	// publishes its interfaces, which counts as "not running")
	if ( !renderSystem || !renderSystem->IsOpenGLRunning() ) {
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
			// A second error raised while unwinding a fatal error used to call
			// Sys_Quit() here, which exits with EXIT_SUCCESS and prints nothing:
			// the original fatal message never reached the log, the shell saw a
			// clean exit, and the user was left with a truncated log ending at
			// whichever teardown step tripped over uninitialized state.
			// Report both messages and exit through the fatal path instead.
			char secondary[ sizeof( errorMessage ) ];
			va_list argptr2;

			va_start( argptr2, fmt );
			idStr::vsnPrintf( secondary, sizeof( secondary ), fmt, argptr2 );
			va_end( argptr2 );
			secondary[ sizeof( secondary ) - 1 ] = '\0';

			Sys_Printf( "FATAL: %s\n", errorMessage );
			Sys_Printf( "FATAL: secondary error while shutting down: %s\n", secondary );
			Com_FlushBufferedOutput();

			Sys_Error( "%s (secondary error while shutting down: %s)", errorMessage, secondary );
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
#ifndef ID_DEDICATED
		// A recoverable drop can bypass the ordinary disconnect command. Restore
		// Arena's temporary server rules before the session tears its server down.
		if ( arenaCampaign.NeedsCleanup() ) {
			arenaCampaign.AbortMatch();
		}
#endif
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
	// renderSystem is NULL on module-only builds when erroring out before the
	// renderer module has published its interfaces (e.g. filesystem startup).
	if ( renderSystem && renderSystem->IsOpenGLRunning() && cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart partial windowed\n" );
	}

	Com_FlushBufferedOutput();

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
		Com_FlushBufferedOutput();

		// exit through the fatal path rather than Sys_Quit(): Sys_Quit() reports
		// EXIT_SUCCESS and writes no durable record, so a recursed fatal used to
		// look like a clean shutdown to the shell and to support tooling
		Sys_Error( "%s", errorMessage );
	}
	com_errorEntered = ERP_FATAL;

	va_start( argptr, fmt );
	idStr::vsnPrintf( errorMessage, sizeof(errorMessage), fmt, argptr );
	va_end( argptr );
	errorMessage[sizeof(errorMessage)-1] = '\0';

	// log the message before Shutdown closes the log file; Sys_Error output
	// is invisible on windowed builds without an attached console
	Printf( "********************\nFATAL: %s\n********************\n", errorMessage );

	// Only attempt a renderer restart fallback when GL is actually live.
	// renderSystem is NULL on module-only builds when a fatal error fires
	// before the renderer module has published its interfaces (e.g. failing
	// to open the log file during filesystem startup).
	if ( renderSystem && renderSystem->IsOpenGLRunning() && cvarSystem->GetCVarBool( "r_fullscreen" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart partial windowed\n" );
	}

	Sys_SetFatalError( errorMessage );

	// Shutdown() below runs the whole engine teardown, and FatalError can fire
	// from anywhere in startup -- including before the session has allocated the
	// state that teardown walks. Get the message out of the stdio buffers first
	// so a fault in teardown cannot swallow it (issue #96).
	Com_FlushBufferedOutput();

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
#ifndef ID_DEDICATED
		// Arena temporarily overrides archived multiplayer and user settings.
		// Restore and flush them while the session and filesystem are still
		// alive; session shutdown is too late to repair a config written during
		// the active match.
		if ( arenaCampaign.NeedsCleanup() ) {
			arenaCampaign.AbortMatch();
			WriteConfiguration();
		}
#endif
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

#define		MAX_CONSOLE_LINES	64
int			com_numConsoleLines;
idCmdArgs	com_consoleLines[MAX_CONSOLE_LINES];
static bool	com_droppedStartupCommands = false;

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
		// the log file is not open yet, so AddStartupCommands repeats this warning once it is
		com_droppedStartupCommands = true;
		Printf( "^3WARNING: command line contains more than %d startup commands; extra commands were ignored\n", MAX_CONSOLE_LINES );
	}
}

/*
==================
idCommonLocal::ClearCommandLine
==================
*/
void idCommonLocal::ClearCommandLine( void ) {
	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		com_consoleLines[ i ].ClearSensitive();
	}
	idCmdArgs::ClearArgsScratch();
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
static bool Common_HasStartupVariable( const char *match ) {
	if ( match == NULL || match[ 0 ] == '\0' ) {
		return false;
	}

	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		if ( com_consoleLines[ i ].Argc() < 2 ) {
			continue;
		}
		if ( idStr::Cmp( com_consoleLines[ i ].Argv( 0 ), "set" ) ) {
			continue;
		}
		if ( !idStr::Icmp( com_consoleLines[ i ].Argv( 1 ), match ) ) {
			return true;
		}
	}

	return false;
}

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

	if ( com_droppedStartupCommands ) {
		Warning( "command line contains more than %d startup commands; extra commands were ignored", MAX_CONSOLE_LINES );
	}

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

static std::atomic<float> openQ4_gameTimeScale( 1.0f );

void idCommonLocal::SetGameTimeScale( float scale ) {
	if ( FLOAT_IS_NAN( scale ) || FLOAT_IS_INF( scale ) ) {
		scale = 1.0f;
	}
	scale = idMath::ClampFloat( 0.01f, 1.0f, scale );
	openQ4_gameTimeScale.store( scale, std::memory_order_release );

	if ( soundSystem != NULL ) {
		idSoundWorld *soundWorld = soundSystem->GetPlayingSoundWorld();
		if ( soundWorld != NULL ) {
			soundWorld->SetSlowmoSpeed( scale );
		}
	}
}

float idCommonLocal::GetGameTimeScale( void ) const {
	return openQ4_gameTimeScale.load( std::memory_order_acquire );
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
	return openQ4_GetActiveToolFlags( validationMask ) == validationMask;
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
	runtag = cvarSystem->GetCVarString( "si_version" );
	runtag += " - ";
	runtag += curtime;
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
#ifndef ID_DEDICATED
	// Arena owns a temporary transaction of archived multiplayer rules. Do not
	// serialize that transaction; once it restores the player's values, the
	// ordinary modified-flag path writes the durable configuration.
	if ( arenaCampaign.NeedsCleanup() ) {
		return;
	}
#endif

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
KeysFromBindingForPrompt()
Returns the key bound to the command using the emphasized center-prompt form
===============
*/
const char* idCommonLocal::KeysFromBindingForPrompt( const char *bind ) {
	return idKeyInput::KeysFromBindingForPrompt( bind );
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
		// keep DDS replacements enabled at the top spec; in openQ4 this cvar also
		// gates user-supplied replacement packs, not just the stock progimg tree
		cvarSystem->SetCVarInteger( "image_usePrecompressedTextures", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_downSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarString( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 16, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 5, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 8, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 1, CVAR_ARCHIVE );
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
		cvarSystem->SetCVarInteger( "image_downSize", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_anisotropy", 8, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useCompression", 1, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_ignoreHighQuality", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "s_maxSoundsPerShader", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "image_useNormalCompression", 0, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_mode", 4, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_multiSamples", 4, CVAR_ARCHIVE );
		cvarSystem->SetCVarInteger( "r_postAA", 1, CVAR_ARCHIVE );
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

#if defined( MACOS_X )
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

typedef struct openQ4PerformancePreset_s {
	const char *name;
	int machineSpec;
	const char *rendererBenchmarkPreset;
	int screenFraction;
	int multiSamples;
	int postAA;
	int maxFps;
	int anisotropy;
	int downSizeLimit;
	int downSize;
	int ignoreHighQuality;
	int usePrecompressedTextures;
	int maxSoundsPerShader;
	int useShadowMap;
	int shadowMapSize;
	int shadowMapMaxUpdates;
	int bloom;
	int ssao;
	int hdrToneMap;
	int motionBlur;
	int crt;
	int useLightGrid;
	int uploadMegs;
	int uploadFrameBuffers;
	int numberOfSpeakers;
	int useEAXReverb;
	int maxEmitterChannels;
} openQ4PerformancePreset_t;

static const openQ4PerformancePreset_t OPENQ4_PERFORMANCE_PRESETS[] = {
	{ "minimum", 0, "low",
		50, 0, 0, 30,
		1, 512, 1, 1, 1, 1,
		0, 512, 1, 0, 0, 0, 0, 0, 1, 8, 3,
		2, 0, 24 },
	{ "lowpower", 0, "low",
		75, 0, 0, 30,
		1, 1024, 1, 1, 1, 1,
		0, 512, 1, 0, 0, 0, 0, 0, 1, 8, 3,
		2, 0, 32 },
	{ "performance", 1, "baseline",
		85, 0, 1, 60,
		2, 0, 0, 0, 1, 0,
		0, 1024, 2, 0, 0, 0, 0, 0, 1, 16, 4,
		2, 0, 40 },
	{ "balanced", 2, "baseline",
		100, 2, 1, 120,
		4, 0, 0, 0, 1, 0,
		0, 1024, 0, 0, 0, 0, 0, 0, 1, 16, 4,
		6, 1, 48 },
	{ "quality", 3, "modern",
		100, 4, 1, 144,
		8, 0, 0, 0, 1, 0,
		0, 1024, 0, 0, 0, 0, 0, 0, 1, 32, 4,
		6, 1, 48 },
	// image_usePrecompressedTextures stays at 1 here even though retail's top
	// machine spec used 0. In openQ4 that cvar also gates user-supplied DDS
	// replacement packs, so 0 silently discarded a player's high-resolution BC7
	// art the moment they touched the settings menu - the reverse of what the
	// highest preset should do.
	{ "ultra", 3, "high-end",
		100, 8, 1, 240,
		16, 0, 0, 0, 1, 0,
		0, 2048, 0, 0, 0, 0, 0, 0, 1, 32, 4,
		6, 1, 48 }
};

static const int OPENQ4_PERFORMANCE_PRESET_COUNT = static_cast<int>( sizeof( OPENQ4_PERFORMANCE_PRESETS ) / sizeof( OPENQ4_PERFORMANCE_PRESETS[0] ) );
static const char *OPENQ4_DEFAULT_PERFORMANCE_PRESET = "balanced";
static const int OPENQ4_PERFORMANCE_PRESET_MAX_SHADOW_UPDATES = 1024;
static const int OPENQ4_PERFORMANCE_PRESET_MIN_UPLOAD_FRAME_BUFFERS = 3;
static const int OPENQ4_PERFORMANCE_PRESET_MAX_UPLOAD_FRAME_BUFFERS = 8;
static const int OPENQ4_PERFORMANCE_PRESET_MAX_EMITTER_CHANNELS = 48;
static const int OPENQ4_PERFORMANCE_PRESET_UNLIMITED_BUDGET_RANK = 0x7fffffff;
static const int OPENQ4_PERFORMANCE_PRESET_UNKNOWN_SYSTEM_RAM_MB = 8192;
static const int OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB = 2048;
static const int OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_SYSTEM_RAM_MB = 4 * 1024 * 1024;
static const int OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB = 256 * 1024;

static const char *OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[] = {
	"com_performancePreset",
	"com_machineSpec",
	"r_rendererBenchmarkPreset",
	"r_screenFraction",
	"r_multiSamples",
	"r_postAA",
	"com_maxfps",
	"image_anisotropy",
	"image_usePrecompressedTextures",
	"image_downSize",
	"image_downSizeLimit",
	"image_downSizeSpecular",
	"image_downSizeBump",
	"image_downSizeSpecularLimit",
	"image_downSizeBumpLimit",
	"image_ignoreHighQuality",
	"image_writeGeneratedImages",
	"s_maxSoundsPerShader",
	"r_useShadowMap",
	"r_shadowMapSize",
	"r_shadowMapMaxUpdatesPerView",
	"r_bloom",
	"r_ssao",
	"r_hdrToneMap",
	"r_motionBlur",
	"r_crt",
	"r_useLightGrid",
	"r_rendererUploadMegs",
	"r_rendererUploadFrameBuffers",
	"s_numberOfSpeakers",
	"s_useEAXReverb",
	"s_maxEmitterChannels"
};

static const int OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT = static_cast<int>( sizeof( OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS ) / sizeof( OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[0] ) );

static bool Common_PerformancePresetTargetIsDeclared( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		if ( idStr::Icmp( OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[i], name ) == 0 ) {
			return true;
		}
	}
	return false;
}

static const openQ4PerformancePreset_t *Common_FindPerformancePreset( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return NULL;
	}

	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
		if ( idStr::Icmp( OPENQ4_PERFORMANCE_PRESETS[i].name, name ) == 0 ) {
			return &OPENQ4_PERFORMANCE_PRESETS[i];
		}
	}
	return NULL;
}

static const openQ4PerformancePreset_t *Common_DefaultPerformancePreset( void ) {
	return Common_FindPerformancePreset( OPENQ4_DEFAULT_PERFORMANCE_PRESET );
}

static void Common_BuildPerformancePresetNameList( idStr &names, const char *separator ) {
	names.Clear();
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
		if ( i > 0 ) {
			names += separator;
		}
		names += OPENQ4_PERFORMANCE_PRESETS[i].name;
	}
}

static void Common_PrintApplyPerformancePresetUsage( void ) {
	idStr names;
	Common_BuildPerformancePresetNameList( names, "|" );
	common->Printf( "Usage: applyPerformancePreset [%s]\n", names.c_str() );
}

static void Common_PrintAutoDetectPerformancePresetUsage( void ) {
	common->Printf( "Usage: autoDetectPerformancePreset\n" );
}

static bool Common_PerformancePresetTargetIsKnown( const char *name ) {
	return name != NULL && name[0] != '\0' && cvarSystem->Find( name ) != NULL;
}

static bool Common_PerformancePresetAllTargetsKnown( const char *presetName, bool quiet ) {
	bool allKnown = true;
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		const char *cvarName = OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[i];
		if ( !Common_PerformancePresetTargetIsKnown( cvarName ) ) {
			if ( !quiet ) {
				common->Warning( "Performance preset '%s' references unknown cvar '%s'", presetName, cvarName );
			}
			allKnown = false;
		}
	}
	return allKnown;
}

static bool Common_SetPerformancePresetString( const char *presetName, const char *cvarName, const char *value, bool quiet ) {
	if ( !Common_PerformancePresetTargetIsDeclared( cvarName ) ) {
		if ( !quiet ) {
			common->Warning( "Performance preset '%s' writes undeclared cvar '%s'", presetName, cvarName != NULL ? cvarName : "" );
		}
		return false;
	}
	if ( !Common_PerformancePresetTargetIsKnown( cvarName ) ) {
		if ( !quiet ) {
			common->Warning( "Performance preset '%s' references unknown cvar '%s'", presetName, cvarName );
		}
		return false;
	}
	cvarSystem->SetCVarString( cvarName, value != NULL ? value : "", CVAR_ARCHIVE );
	const idCVar *cvar = cvarSystem->Find( cvarName );
	if ( cvar != NULL && idStr::Icmp( cvar->GetString(), value != NULL ? value : "" ) == 0 ) {
		return true;
	}
	if ( !quiet ) {
		common->Warning(
			"Performance preset '%s' could not set cvar '%s' to '%s' (actual '%s')",
			presetName,
			cvarName,
			value != NULL ? value : "",
			cvar != NULL ? cvar->GetString() : "<missing>" );
	}
	return false;
}

static bool Common_SetPerformancePresetInt( const char *presetName, const char *cvarName, int value, bool quiet ) {
	if ( !Common_PerformancePresetTargetIsDeclared( cvarName ) ) {
		if ( !quiet ) {
			common->Warning( "Performance preset '%s' writes undeclared cvar '%s'", presetName, cvarName != NULL ? cvarName : "" );
		}
		return false;
	}
	if ( !Common_PerformancePresetTargetIsKnown( cvarName ) ) {
		if ( !quiet ) {
			common->Warning( "Performance preset '%s' references unknown cvar '%s'", presetName, cvarName );
		}
		return false;
	}
	cvarSystem->SetCVarInteger( cvarName, value, CVAR_ARCHIVE );
	const idCVar *cvar = cvarSystem->Find( cvarName );
	if ( cvar != NULL && cvar->GetInteger() == value ) {
		return true;
	}
	if ( !quiet ) {
		common->Warning(
			"Performance preset '%s' could not set cvar '%s' to %d (actual '%s')",
			presetName,
			cvarName,
			value,
			cvar != NULL ? cvar->GetString() : "<missing>" );
	}
	return false;
}

struct openQ4PerformancePresetCVarBackup_t {
	const char *name;
	bool valid;
	int flags;
	idStr value;
};

static void Common_BackupPerformancePresetCVars( openQ4PerformancePresetCVarBackup_t backups[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT] ) {
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		backups[i].name = OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[i];
		idCVar *cvar = cvarSystem->Find( backups[i].name );
		backups[i].valid = ( cvar != NULL );
		backups[i].flags = backups[i].valid ? cvar->GetFlags() : 0;
		backups[i].value = backups[i].valid ? cvar->GetString() : "";
	}
}

static void Common_RestorePerformancePresetCVarFlags( idCVar *cvar, int flags ) {
	if ( cvar == NULL ) {
		return;
	}

	cvar->RemoveFlag( CVAR_ALL );
	cvar->SetFlag( static_cast<cvarFlags_t>( flags ) );
}

static void Common_RestorePerformancePresetCVars( const openQ4PerformancePresetCVarBackup_t backups[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT] ) {
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		idCVar *cvar = cvarSystem->Find( backups[i].name );
		if ( backups[i].valid ) {
			cvarSystem->SetCVarString( backups[i].name, backups[i].value.c_str(), CVAR_ARCHIVE );
			cvar = cvarSystem->Find( backups[i].name );
			Common_RestorePerformancePresetCVarFlags( cvar, backups[i].flags );
		} else if ( cvar != NULL ) {
			cvar->RemoveFlag( CVAR_ARCHIVE );
			cvar->ClearModified();
		}
	}
}

static void Common_RestorePerformancePresetModifiedFlags( int modifiedFlags ) {
	cvarSystem->ClearModifiedFlags( CVAR_ALL );
	if ( modifiedFlags != 0 ) {
		cvarSystem->SetModifiedFlags( modifiedFlags );
	}
}

static bool Common_ApplyPerformancePreset( const openQ4PerformancePreset_t &preset, bool quiet = false ) {
	if ( !Common_PerformancePresetAllTargetsKnown( preset.name, quiet ) ) {
		if ( !quiet ) {
			common->Printf( "Performance preset '%s' did not apply because one or more target cvars are unavailable.\n", preset.name );
		}
		return false;
	}

	openQ4PerformancePresetCVarBackup_t backups[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT];
	const int savedModifiedFlags = cvarSystem->GetModifiedFlags();
	Common_BackupPerformancePresetCVars( backups );

	bool applied = true;
	applied &= Common_SetPerformancePresetInt( preset.name, "com_machineSpec", preset.machineSpec, quiet );

	applied &= Common_SetPerformancePresetString( preset.name, "r_rendererBenchmarkPreset", preset.rendererBenchmarkPreset, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_screenFraction", preset.screenFraction, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_multiSamples", preset.multiSamples, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_postAA", preset.postAA, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "com_maxfps", preset.maxFps, quiet );

	applied &= Common_SetPerformancePresetInt( preset.name, "image_anisotropy", preset.anisotropy, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_usePrecompressedTextures", preset.usePrecompressedTextures, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSize", preset.downSize, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSizeLimit", preset.downSizeLimit, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSizeSpecular", preset.downSize, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSizeBump", preset.downSize, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSizeSpecularLimit", 64, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_downSizeBumpLimit", preset.downSize != 0 ? 256 : 0, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "image_ignoreHighQuality", preset.ignoreHighQuality, quiet );
	// 1 at every preset: the generated image cache is a load-time win at any
	// quality level, and the downsize cvars above are part of its cache key, so
	// a preset change is exactly when the new variants need to be written out
	applied &= Common_SetPerformancePresetInt( preset.name, "image_writeGeneratedImages", 1, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "s_maxSoundsPerShader", preset.maxSoundsPerShader, quiet );

	applied &= Common_SetPerformancePresetInt( preset.name, "r_useShadowMap", preset.useShadowMap, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_shadowMapSize", preset.shadowMapSize, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_shadowMapMaxUpdatesPerView", preset.shadowMapMaxUpdates, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_bloom", preset.bloom, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_ssao", preset.ssao, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_hdrToneMap", preset.hdrToneMap, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_motionBlur", preset.motionBlur, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_crt", preset.crt, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_useLightGrid", preset.useLightGrid, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_rendererUploadMegs", preset.uploadMegs, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "r_rendererUploadFrameBuffers", preset.uploadFrameBuffers, quiet );

	applied &= Common_SetPerformancePresetInt( preset.name, "s_numberOfSpeakers", preset.numberOfSpeakers, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "s_useEAXReverb", preset.useEAXReverb, quiet );
	applied &= Common_SetPerformancePresetInt( preset.name, "s_maxEmitterChannels", preset.maxEmitterChannels, quiet );

	// Commit the public selection marker only after every profile target accepted
	// its value. Any unexpected normalization failure rolls the whole apply back.
	if ( applied ) {
		applied = Common_SetPerformancePresetString( preset.name, "com_performancePreset", preset.name, quiet );
	}
	if ( !applied ) {
		Common_RestorePerformancePresetCVars( backups );
		Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );
	}

	if ( !quiet && applied ) {
		common->Printf( "Applied performance preset '%s'. Run vid_restart for video/texture changes and s_restart for audio backend changes.\n", preset.name );
	} else if ( !quiet ) {
		common->Printf( "Performance preset '%s' did not apply cleanly and was rolled back; see warnings above.\n", preset.name );
	}

	return applied;
}

static int Common_SanitizePerformancePresetMemoryMB( int rawMegabytes, int fallbackMegabytes, int maxReasonableMegabytes ) {
	if ( rawMegabytes <= 0 || rawMegabytes > maxReasonableMegabytes ) {
		return fallbackMegabytes;
	}
	return rawMegabytes;
}

static idStr Common_FormatPerformancePresetMemorySignal( int rawMegabytes, int effectiveMegabytes ) {
	idStr text = Sys_FormatMemoryMB( effectiveMegabytes );
	if ( rawMegabytes != effectiveMegabytes ) {
		text += " effective, ";
		text += Sys_FormatMemoryMB( rawMegabytes );
		text += " reported";
	}
	return text;
}

static const char *Common_DetectPerformancePresetName( idStr &reason ) {
	if ( Common_HasExplicitLowPowerHostSignal() ) {
		reason = "explicit low-power environment signal";
		return "lowpower";
	}

	if ( Common_HasRaspberryPiHostSignal() ) {
		reason = "Raspberry Pi host signal";
		return "lowpower";
	}

	if ( idStr::Icmp( com_platformProfile.GetString(), "steamdeck" ) == 0 || Common_HasSteamDeckHostSignal() ) {
		reason = "Steam Deck platform profile";
		return "performance";
	}

	const int rawSysRam = Sys_GetSystemRam();
	const int rawVidRam = Sys_GetVideoRam();
	const int sysRam = Common_SanitizePerformancePresetMemoryMB(
		rawSysRam,
		OPENQ4_PERFORMANCE_PRESET_UNKNOWN_SYSTEM_RAM_MB,
		OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_SYSTEM_RAM_MB );
	const int vidRam = Common_SanitizePerformancePresetMemoryMB(
		rawVidRam,
		OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB,
		OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB );
	const idStr sysRamReason = Common_FormatPerformancePresetMemorySignal( rawSysRam, sysRam );
	const idStr vidRamReason = Common_FormatPerformancePresetMemorySignal( rawVidRam, vidRam );
	bool oldCard = false;
	bool nv10or20 = false;
	renderSystem->GetCardCaps( oldCard, nv10or20 );

	if ( oldCard ) {
		reason = "legacy renderer architecture";
		return "minimum";
	}

	if ( Common_HostCpuIsArm64() ) {
		if ( sysRam <= 4096 || vidRam <= 1024 ) {
			reason = va( "ARM64 with constrained memory (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
			return "lowpower";
		}
		reason = va( "ARM64 host (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
		return "performance";
	}

	if ( sysRam <= 4096 || vidRam <= 1024 ) {
		reason = va( "constrained memory (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
		return "lowpower";
	}

	if ( sysRam <= 8192 || vidRam <= 2048 ) {
		reason = va( "modest memory/GPU budget (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
		return "performance";
	}

	if ( sysRam >= 16384 && vidRam >= 6144 ) {
		reason = va( "high memory/GPU budget (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
		return "quality";
	}

	reason = va( "standard desktop budget (%s RAM, %s VRAM)", sysRamReason.c_str(), vidRamReason.c_str() );
	return "balanced";
}

static bool Common_ApplyPerformancePresetCommand( const idCmdArgs &args, bool quiet = false ) {
	if ( args.Argc() > 2 ) {
		if ( !quiet ) {
			Common_PrintApplyPerformancePresetUsage();
			common->Printf( "Too many arguments for applyPerformancePreset.\n" );
		}
		return false;
	}

	const bool explicitPreset = args.Argc() > 1;
	const char *presetName = explicitPreset ? args.Argv( 1 ) : com_performancePreset.GetString();
	const openQ4PerformancePreset_t *preset = Common_FindPerformancePreset( presetName );
	if ( preset == NULL ) {
		if ( explicitPreset ) {
			if ( !quiet ) {
				Common_PrintApplyPerformancePresetUsage();
				common->Printf( "Unknown performance preset '%s'.\n", presetName != NULL ? presetName : "" );
			}
			return false;
		}

		const openQ4PerformancePreset_t *defaultPreset = Common_DefaultPerformancePreset();
		if ( defaultPreset == NULL ) {
			if ( !quiet ) {
				common->Printf( "Unknown selected performance preset '%s' and default preset '%s' is unavailable.\n", presetName != NULL ? presetName : "", OPENQ4_DEFAULT_PERFORMANCE_PRESET );
			}
			return false;
		}
		if ( !quiet ) {
			common->Printf( "Unknown selected performance preset '%s'; applying default preset '%s'.\n", presetName != NULL ? presetName : "", defaultPreset->name );
		}
		preset = defaultPreset;
	}

	return Common_ApplyPerformancePreset( *preset, quiet );
}

static void Com_ApplyPerformancePreset_f( const idCmdArgs &args ) {
	Common_ApplyPerformancePresetCommand( args );
}

static bool Common_AutoDetectPerformancePresetCommand( const idCmdArgs &args, bool quiet = false ) {
	if ( args.Argc() > 1 ) {
		if ( !quiet ) {
			Common_PrintAutoDetectPerformancePresetUsage();
			common->Printf( "Too many arguments for autoDetectPerformancePreset.\n" );
		}
		return false;
	}

	idStr reason;
	const char *presetName = Common_DetectPerformancePresetName( reason );
	const openQ4PerformancePreset_t *preset = Common_FindPerformancePreset( presetName );
	if ( preset == NULL ) {
		preset = Common_DefaultPerformancePreset();
		reason = "internal fallback";
	}
	if ( preset == NULL ) {
		if ( !quiet ) {
			common->Printf( "Auto-detect default preset '%s' is unavailable.\n", OPENQ4_DEFAULT_PERFORMANCE_PRESET );
		}
		return false;
	}

	if ( !quiet ) {
		common->Printf( "Auto-detected performance preset '%s' (%s).\n", preset->name, reason.c_str() );
	}
	return Common_ApplyPerformancePreset( *preset, quiet );
}

static void Com_AutoDetectPerformancePreset_f( const idCmdArgs &args ) {
	Common_AutoDetectPerformancePresetCommand( args );
}

static const char *commonCommandCompletionMatch = NULL;
static bool commonCommandCompletionFound = false;

static void Common_CommandCompletionFindCallback( const char *name ) {
	if ( commonCommandCompletionMatch != NULL && idStr::Icmp( name, commonCommandCompletionMatch ) == 0 ) {
		commonCommandCompletionFound = true;
	}
}

static bool Common_CommandIsRegistered( const char *name ) {
	commonCommandCompletionMatch = name;
	commonCommandCompletionFound = false;
	cmdSystem->CommandCompletion( Common_CommandCompletionFindCallback );
	commonCommandCompletionMatch = NULL;
	return commonCommandCompletionFound;
}

static bool Common_ValidatePerformancePresetTargetCVars( void ) {
	bool passed = true;
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		const char *cvarName = OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[i];
		if ( !Common_PerformancePresetTargetIsKnown( cvarName ) ) {
			common->Printf( "PerformancePreset self-test failed: target cvar '%s' is not registered\n", cvarName );
			passed = false;
		}
		for ( int j = i + 1; j < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++j ) {
			const char *otherCvarName = OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[j];
			if ( idStr::Icmp( cvarName, otherCvarName ) == 0 ) {
				common->Printf( "PerformancePreset self-test failed: duplicate target cvar '%s'\n", cvarName );
				passed = false;
			}
		}
	}
	return passed;
}

static bool Common_ValidatePerformancePresetCVarsRestored( const openQ4PerformancePresetCVarBackup_t backups[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT] ) {
	bool passed = true;
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		idCVar *cvar = cvarSystem->Find( backups[i].name );
		if ( backups[i].valid ) {
			if ( cvar == NULL ) {
				common->Printf( "PerformancePreset self-test failed: restore lost cvar '%s'\n", backups[i].name );
				passed = false;
				continue;
			}
			if ( idStr::Icmp( cvar->GetString(), backups[i].value.c_str() ) != 0 ) {
				common->Printf(
					"PerformancePreset self-test failed: restore left %s as '%s', expected '%s'\n",
					backups[i].name,
					cvar->GetString(),
					backups[i].value.c_str() );
				passed = false;
			}
			if ( cvar->GetFlags() != backups[i].flags ) {
				common->Printf(
					"PerformancePreset self-test failed: restore left %s flags 0x%x, expected 0x%x\n",
					backups[i].name,
					cvar->GetFlags(),
					backups[i].flags );
				passed = false;
			}
		} else if ( cvar != NULL && ( cvar->GetFlags() & CVAR_ARCHIVE ) != 0 ) {
			common->Printf( "PerformancePreset self-test failed: restore left newly-created cvar '%s' archived\n", backups[i].name );
			passed = false;
		}
	}
	return passed;
}

static bool Common_CheckPerformancePresetString( const char *presetName, const char *cvarName, const char *expected ) {
	idCVar *cvar = cvarSystem->Find( cvarName );
	if ( cvar == NULL ) {
		common->Printf( "PerformancePreset self-test failed: %s did not create/find cvar %s\n", presetName, cvarName );
		return false;
	}
	if ( idStr::Icmp( cvar->GetString(), expected ) != 0 ) {
		common->Printf(
			"PerformancePreset self-test failed: %s set %s to '%s', expected '%s'\n",
			presetName,
			cvarName,
			cvar->GetString(),
			expected );
		return false;
	}
	return true;
}

static bool Common_CheckPerformancePresetInt( const char *presetName, const char *cvarName, int expected ) {
	idCVar *cvar = cvarSystem->Find( cvarName );
	if ( cvar == NULL ) {
		common->Printf( "PerformancePreset self-test failed: %s did not create/find cvar %s\n", presetName, cvarName );
		return false;
	}
	if ( cvar->GetInteger() != expected ) {
		common->Printf(
			"PerformancePreset self-test failed: %s set %s to %d, expected %d\n",
			presetName,
			cvarName,
			cvar->GetInteger(),
			expected );
		return false;
	}
	return true;
}

static bool Common_CheckPerformancePresetTargetsArchived( const char *presetName ) {
	bool passed = true;
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT; ++i ) {
		const char *cvarName = OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[i];
		const idCVar *cvar = cvarSystem->Find( cvarName );
		if ( cvar == NULL || ( cvar->GetFlags() & CVAR_ARCHIVE ) == 0 ) {
			common->Printf( "PerformancePreset self-test failed: %s did not archive cvar %s\n", presetName, cvarName );
			passed = false;
		}
	}
	return passed;
}

static bool Common_PerformancePresetMultiSamplesAreValid( int samples ) {
	return samples == 0 || samples == 2 || samples == 4 || samples == 8 || samples == 16;
}

static bool Common_PerformancePresetBenchmarkNameIsValid( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}
	return idStr::Icmp( name, "low" ) == 0 ||
		idStr::Icmp( name, "baseline" ) == 0 ||
		idStr::Icmp( name, "modern" ) == 0 ||
		idStr::Icmp( name, "high-end" ) == 0;
}

static bool Common_PerformancePresetIsNamed( const openQ4PerformancePreset_t &preset, const char *name ) {
	return preset.name != NULL && name != NULL && idStr::Icmp( preset.name, name ) == 0;
}

static int Common_PerformancePresetBudgetRank( int value ) {
	return value == 0 ? OPENQ4_PERFORMANCE_PRESET_UNLIMITED_BUDGET_RANK : value;
}

static bool Common_PerformancePresetCheckExpectedInt( const openQ4PerformancePreset_t &preset, const char *fieldName, int value, int expected ) {
	if ( value == expected ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s is %d, expected %d\n", preset.name, fieldName, value, expected );
	return false;
}

static bool Common_PerformancePresetCheckIntAtLeast( const openQ4PerformancePreset_t &preset, const char *fieldName, int value, int minimum ) {
	if ( value >= minimum ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s is %d, expected at least %d\n", preset.name, fieldName, value, minimum );
	return false;
}

static bool Common_PerformancePresetCheckIntAtMost( const openQ4PerformancePreset_t &preset, const char *fieldName, int value, int maximum ) {
	if ( value <= maximum ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s is %d, expected at most %d\n", preset.name, fieldName, value, maximum );
	return false;
}

static bool Common_PerformancePresetCheckBenchmarkPreset( const openQ4PerformancePreset_t &preset, const char *expected ) {
	if ( preset.rendererBenchmarkPreset != NULL && expected != NULL && idStr::Icmp( preset.rendererBenchmarkPreset, expected ) == 0 ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s renderer benchmark preset is '%s', expected '%s'\n", preset.name, preset.rendererBenchmarkPreset != NULL ? preset.rendererBenchmarkPreset : "", expected != NULL ? expected : "" );
	return false;
}

static bool Common_PerformancePresetCheckRestoredAudio( const openQ4PerformancePreset_t &preset ) {
	bool passed = true;
	passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_numberOfSpeakers", preset.numberOfSpeakers, 6 );
	passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_useEAXReverb", preset.useEAXReverb, 1 );
	passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_maxEmitterChannels", preset.maxEmitterChannels, OPENQ4_PERFORMANCE_PRESET_MAX_EMITTER_CHANNELS );
	passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_maxSoundsPerShader", preset.maxSoundsPerShader, 0 );
	return passed;
}

static bool Common_ValidatePerformancePresetIntent( const openQ4PerformancePreset_t &preset ) {
	bool passed = true;

	if ( preset.useShadowMap != 0 ) {
		common->Printf( "PerformancePreset self-test failed: %s enables shadow maps; presets should keep shadow maps opt-in\n", preset.name );
		passed = false;
	}
	if ( preset.bloom != 0 || preset.ssao != 0 || preset.hdrToneMap != 0 || preset.motionBlur != 0 || preset.crt != 0 ) {
		common->Printf( "PerformancePreset self-test failed: %s enables optional post effects; presets should keep authored rendering as the baseline\n", preset.name );
		passed = false;
	}
	passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_useLightGrid", preset.useLightGrid, 1 );

	if ( Common_PerformancePresetIsNamed( preset, "minimum" ) || Common_PerformancePresetIsNamed( preset, "lowpower" ) ) {
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "com_machineSpec", preset.machineSpec, 0 );
		passed &= Common_PerformancePresetCheckBenchmarkPreset( preset, "low" );
		passed &= Common_PerformancePresetCheckIntAtMost( preset, "r_screenFraction", preset.screenFraction, 75 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_multiSamples", preset.multiSamples, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_postAA", preset.postAA, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_downSize", preset.downSize, 1 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_ignoreHighQuality", preset.ignoreHighQuality, 1 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "s_maxSoundsPerShader", preset.maxSoundsPerShader, 1 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_numberOfSpeakers", preset.numberOfSpeakers, 2 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_useEAXReverb", preset.useEAXReverb, 0 );
		passed &= Common_PerformancePresetCheckIntAtMost( preset, "s_maxEmitterChannels", preset.maxEmitterChannels, 32 );
		passed &= Common_PerformancePresetCheckIntAtMost( preset, "r_rendererUploadMegs", preset.uploadMegs, 8 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_rendererUploadFrameBuffers", preset.uploadFrameBuffers, OPENQ4_PERFORMANCE_PRESET_MIN_UPLOAD_FRAME_BUFFERS );
	} else if ( Common_PerformancePresetIsNamed( preset, "performance" ) ) {
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "com_machineSpec", preset.machineSpec, 1 );
		passed &= Common_PerformancePresetCheckBenchmarkPreset( preset, "baseline" );
		passed &= Common_PerformancePresetCheckIntAtMost( preset, "r_screenFraction", preset.screenFraction, 85 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_multiSamples", preset.multiSamples, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_postAA", preset.postAA, 1 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_downSize", preset.downSize, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_ignoreHighQuality", preset.ignoreHighQuality, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_numberOfSpeakers", preset.numberOfSpeakers, 2 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "s_useEAXReverb", preset.useEAXReverb, 0 );
		passed &= Common_PerformancePresetCheckIntAtMost( preset, "s_maxEmitterChannels", preset.maxEmitterChannels, 40 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_rendererUploadMegs", preset.uploadMegs, 16 );
	} else if ( Common_PerformancePresetIsNamed( preset, "balanced" ) ) {
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "com_machineSpec", preset.machineSpec, 2 );
		passed &= Common_PerformancePresetCheckBenchmarkPreset( preset, "baseline" );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_screenFraction", preset.screenFraction, 100 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_multiSamples", preset.multiSamples, 2 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_postAA", preset.postAA, 1 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_downSize", preset.downSize, 0 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_ignoreHighQuality", preset.ignoreHighQuality, 0 );
		passed &= Common_PerformancePresetCheckRestoredAudio( preset );
	} else if ( Common_PerformancePresetIsNamed( preset, "quality" ) ) {
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "com_machineSpec", preset.machineSpec, 3 );
		passed &= Common_PerformancePresetCheckBenchmarkPreset( preset, "modern" );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_screenFraction", preset.screenFraction, 100 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_multiSamples", preset.multiSamples, 4 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "image_anisotropy", preset.anisotropy, 8 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_usePrecompressedTextures", preset.usePrecompressedTextures, 1 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_rendererUploadMegs", preset.uploadMegs, 32 );
		passed &= Common_PerformancePresetCheckRestoredAudio( preset );
	} else if ( Common_PerformancePresetIsNamed( preset, "ultra" ) ) {
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "com_machineSpec", preset.machineSpec, 3 );
		passed &= Common_PerformancePresetCheckBenchmarkPreset( preset, "high-end" );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "r_screenFraction", preset.screenFraction, 100 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_multiSamples", preset.multiSamples, 8 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_anisotropy", preset.anisotropy, 16 );
		passed &= Common_PerformancePresetCheckExpectedInt( preset, "image_usePrecompressedTextures", preset.usePrecompressedTextures, 1 );
		passed &= Common_PerformancePresetCheckIntAtLeast( preset, "r_rendererUploadMegs", preset.uploadMegs, 32 );
		passed &= Common_PerformancePresetCheckRestoredAudio( preset );
	} else {
		common->Printf( "PerformancePreset self-test failed: %s has no intent validation branch\n", preset.name );
		passed = false;
	}

	return passed;
}

static bool Common_PerformancePresetCheckNonDecreasingInt( const openQ4PerformancePreset_t &previousPreset, const openQ4PerformancePreset_t &preset, const char *fieldName, int previousValue, int value ) {
	if ( value >= previousValue ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s %d is below %s %d\n", preset.name, fieldName, value, previousPreset.name, previousValue );
	return false;
}

static bool Common_PerformancePresetCheckNonDecreasingBudget( const openQ4PerformancePreset_t &previousPreset, const openQ4PerformancePreset_t &preset, const char *fieldName, int previousValue, int value ) {
	if ( Common_PerformancePresetBudgetRank( value ) >= Common_PerformancePresetBudgetRank( previousValue ) ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s budget %d is below %s %d (0 means unlimited)\n", preset.name, fieldName, value, previousPreset.name, previousValue );
	return false;
}

static bool Common_PerformancePresetCheckNonIncreasingInt( const openQ4PerformancePreset_t &previousPreset, const openQ4PerformancePreset_t &preset, const char *fieldName, int previousValue, int value ) {
	if ( value <= previousValue ) {
		return true;
	}
	common->Printf( "PerformancePreset self-test failed: %s %s %d is above %s %d\n", preset.name, fieldName, value, previousPreset.name, previousValue );
	return false;
}

static bool Common_ValidatePerformancePresetProgression( void ) {
	bool passed = true;
	for ( int i = 1; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
		const openQ4PerformancePreset_t &previousPreset = OPENQ4_PERFORMANCE_PRESETS[i - 1];
		const openQ4PerformancePreset_t &preset = OPENQ4_PERFORMANCE_PRESETS[i];

		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "com_machineSpec", previousPreset.machineSpec, preset.machineSpec );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_screenFraction", previousPreset.screenFraction, preset.screenFraction );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_multiSamples", previousPreset.multiSamples, preset.multiSamples );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_postAA", previousPreset.postAA, preset.postAA );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "com_maxfps", previousPreset.maxFps, preset.maxFps );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "image_anisotropy", previousPreset.anisotropy, preset.anisotropy );
		passed &= Common_PerformancePresetCheckNonDecreasingBudget( previousPreset, preset, "image_downSizeLimit", previousPreset.downSizeLimit, preset.downSizeLimit );
		passed &= Common_PerformancePresetCheckNonDecreasingBudget( previousPreset, preset, "s_maxSoundsPerShader", previousPreset.maxSoundsPerShader, preset.maxSoundsPerShader );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_shadowMapSize", previousPreset.shadowMapSize, preset.shadowMapSize );
		passed &= Common_PerformancePresetCheckNonDecreasingBudget( previousPreset, preset, "r_shadowMapMaxUpdatesPerView", previousPreset.shadowMapMaxUpdates, preset.shadowMapMaxUpdates );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_rendererUploadMegs", previousPreset.uploadMegs, preset.uploadMegs );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "r_rendererUploadFrameBuffers", previousPreset.uploadFrameBuffers, preset.uploadFrameBuffers );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "s_numberOfSpeakers", previousPreset.numberOfSpeakers, preset.numberOfSpeakers );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "s_useEAXReverb", previousPreset.useEAXReverb, preset.useEAXReverb );
		passed &= Common_PerformancePresetCheckNonDecreasingInt( previousPreset, preset, "s_maxEmitterChannels", previousPreset.maxEmitterChannels, preset.maxEmitterChannels );

		passed &= Common_PerformancePresetCheckNonIncreasingInt( previousPreset, preset, "image_downSize", previousPreset.downSize, preset.downSize );
		passed &= Common_PerformancePresetCheckNonIncreasingInt( previousPreset, preset, "image_ignoreHighQuality", previousPreset.ignoreHighQuality, preset.ignoreHighQuality );
		passed &= Common_PerformancePresetCheckNonIncreasingInt( previousPreset, preset, "image_usePrecompressedTextures", previousPreset.usePrecompressedTextures, preset.usePrecompressedTextures );
	}
	return passed;
}

static bool Common_ValidatePerformancePresetDefinition( const openQ4PerformancePreset_t &preset ) {
	bool passed = true;
	if ( preset.name == NULL || preset.name[0] == '\0' || Common_FindPerformancePreset( preset.name ) != &preset ) {
		common->Printf( "PerformancePreset self-test failed: invalid preset name\n" );
		passed = false;
	}
	if ( !Common_PerformancePresetBenchmarkNameIsValid( preset.rendererBenchmarkPreset ) ) {
		common->Printf( "PerformancePreset self-test failed: %s uses invalid renderer benchmark preset '%s'\n", preset.name, preset.rendererBenchmarkPreset );
		passed = false;
	}
	if ( preset.machineSpec < 0 || preset.machineSpec > 3 ||
		 preset.screenFraction < 10 || preset.screenFraction > 200 ||
		 !Common_PerformancePresetMultiSamplesAreValid( preset.multiSamples ) ||
		 preset.postAA < 0 || preset.postAA > 4 ||
		 preset.maxFps < 0 || preset.maxFps > 1000 ||
		 preset.anisotropy < 1 || preset.anisotropy > 16 ||
		 preset.downSizeLimit < 0 ||
		 ( preset.downSize != 0 && preset.downSize != 1 ) ||
		 ( preset.ignoreHighQuality != 0 && preset.ignoreHighQuality != 1 ) ||
		 preset.usePrecompressedTextures < 0 || preset.usePrecompressedTextures > 2 ||
		 preset.maxSoundsPerShader < 0 ||
		 ( preset.useShadowMap != 0 && preset.useShadowMap != 1 ) ||
		 preset.shadowMapSize < 128 || preset.shadowMapSize > 4096 ||
		 preset.shadowMapMaxUpdates < 0 || preset.shadowMapMaxUpdates > OPENQ4_PERFORMANCE_PRESET_MAX_SHADOW_UPDATES ||
		 ( preset.bloom != 0 && preset.bloom != 1 ) ||
		 ( preset.ssao != 0 && preset.ssao != 1 ) ||
		 ( preset.hdrToneMap != 0 && preset.hdrToneMap != 1 ) ||
		 ( preset.motionBlur != 0 && preset.motionBlur != 1 ) ||
		 ( preset.crt != 0 && preset.crt != 1 ) ||
		 ( preset.useLightGrid != 0 && preset.useLightGrid != 1 ) ||
		 preset.uploadMegs < 1 || preset.uploadMegs > 128 ||
		 preset.uploadFrameBuffers < OPENQ4_PERFORMANCE_PRESET_MIN_UPLOAD_FRAME_BUFFERS || preset.uploadFrameBuffers > OPENQ4_PERFORMANCE_PRESET_MAX_UPLOAD_FRAME_BUFFERS ||
		 ( preset.numberOfSpeakers != 2 && preset.numberOfSpeakers != 6 ) ||
		 ( preset.useEAXReverb != 0 && preset.useEAXReverb != 1 ) ||
		 preset.maxEmitterChannels <= 0 || preset.maxEmitterChannels > OPENQ4_PERFORMANCE_PRESET_MAX_EMITTER_CHANNELS ) {
		common->Printf( "PerformancePreset self-test failed: %s has out-of-range values\n", preset.name );
		passed = false;
	}
	passed &= Common_ValidatePerformancePresetIntent( preset );
	return passed;
}

static bool Common_ValidatePerformancePresetNameList( const char *listName, const char **names, bool defaultMustBeFirst ) {
	bool passed = true;
	int argCount = 0;
	for ( ; names[argCount] != NULL; ++argCount ) {
		if ( argCount >= OPENQ4_PERFORMANCE_PRESET_COUNT ) {
			common->Printf( "PerformancePreset self-test failed: %s has more entries than preset definitions\n", listName );
			passed = false;
			continue;
		}
		if ( Common_FindPerformancePreset( names[argCount] ) == NULL ) {
			common->Printf( "PerformancePreset self-test failed: %s entry %d is unknown preset '%s'\n", listName, argCount, names[argCount] );
			passed = false;
		}
		for ( int j = 0; j < argCount; ++j ) {
			if ( idStr::Icmp( names[j], names[argCount] ) == 0 ) {
				common->Printf( "PerformancePreset self-test failed: duplicate %s entry '%s'\n", listName, names[argCount] );
				passed = false;
			}
		}
	}
	if ( argCount != OPENQ4_PERFORMANCE_PRESET_COUNT ) {
		common->Printf( "PerformancePreset self-test failed: %s has %d entries for %d preset definitions\n", listName, argCount, OPENQ4_PERFORMANCE_PRESET_COUNT );
		passed = false;
	}
	if ( defaultMustBeFirst && ( names[0] == NULL || idStr::Icmp( names[0], OPENQ4_DEFAULT_PERFORMANCE_PRESET ) != 0 ) ) {
		common->Printf( "PerformancePreset self-test failed: %s must start with default preset '%s'\n", listName, OPENQ4_DEFAULT_PERFORMANCE_PRESET );
		passed = false;
	}
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
		bool found = false;
		for ( int j = 0; names[j] != NULL; ++j ) {
			if ( idStr::Icmp( OPENQ4_PERFORMANCE_PRESETS[i].name, names[j] ) == 0 ) {
				found = true;
				break;
			}
		}
		if ( !found ) {
			common->Printf( "PerformancePreset self-test failed: preset '%s' is missing from %s\n", OPENQ4_PERFORMANCE_PRESETS[i].name, listName );
			passed = false;
		}
	}
	return passed;
}

static bool Common_ValidatePerformancePresetDefinitions( void ) {
	bool passed = true;
	passed &= Common_ValidatePerformancePresetNameList( "command completion choices", com_performancePresetArgs, false );
	passed &= Common_ValidatePerformancePresetNameList( "cvar value strings", com_performancePresetValueStrings, true );
	if ( Common_DefaultPerformancePreset() == NULL ) {
		common->Printf( "PerformancePreset self-test failed: default preset '%s' is missing\n", OPENQ4_DEFAULT_PERFORMANCE_PRESET );
		passed = false;
	}
	for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
		if ( !Common_ValidatePerformancePresetDefinition( OPENQ4_PERFORMANCE_PRESETS[i] ) ) {
			passed = false;
		}
		for ( int j = i + 1; j < OPENQ4_PERFORMANCE_PRESET_COUNT; ++j ) {
			if ( idStr::Icmp( OPENQ4_PERFORMANCE_PRESETS[i].name, OPENQ4_PERFORMANCE_PRESETS[j].name ) == 0 ) {
				common->Printf( "PerformancePreset self-test failed: duplicate preset '%s'\n", OPENQ4_PERFORMANCE_PRESETS[i].name );
				passed = false;
			}
		}
	}
	passed &= Common_ValidatePerformancePresetProgression();
	return passed;
}

static bool Common_ValidatePerformancePresetApplied( const openQ4PerformancePreset_t &preset ) {
	bool passed = true;
	passed &= Common_CheckPerformancePresetString( preset.name, "com_performancePreset", preset.name );
	passed &= Common_CheckPerformancePresetInt( preset.name, "com_machineSpec", preset.machineSpec );
	passed &= Common_CheckPerformancePresetString( preset.name, "r_rendererBenchmarkPreset", preset.rendererBenchmarkPreset );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_screenFraction", preset.screenFraction );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_multiSamples", preset.multiSamples );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_postAA", preset.postAA );
	passed &= Common_CheckPerformancePresetInt( preset.name, "com_maxfps", preset.maxFps );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_anisotropy", preset.anisotropy );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_usePrecompressedTextures", preset.usePrecompressedTextures );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSize", preset.downSize );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSizeLimit", preset.downSizeLimit );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSizeSpecular", preset.downSize );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSizeBump", preset.downSize );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSizeSpecularLimit", 64 );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_downSizeBumpLimit", preset.downSize != 0 ? 256 : 0 );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_ignoreHighQuality", preset.ignoreHighQuality );
	passed &= Common_CheckPerformancePresetInt( preset.name, "image_writeGeneratedImages", 1 );
	passed &= Common_CheckPerformancePresetInt( preset.name, "s_maxSoundsPerShader", preset.maxSoundsPerShader );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_useShadowMap", preset.useShadowMap );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_shadowMapSize", preset.shadowMapSize );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_shadowMapMaxUpdatesPerView", preset.shadowMapMaxUpdates );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_bloom", preset.bloom );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_ssao", preset.ssao );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_hdrToneMap", preset.hdrToneMap );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_motionBlur", preset.motionBlur );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_crt", preset.crt );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_useLightGrid", preset.useLightGrid );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_rendererUploadMegs", preset.uploadMegs );
	passed &= Common_CheckPerformancePresetInt( preset.name, "r_rendererUploadFrameBuffers", preset.uploadFrameBuffers );
	passed &= Common_CheckPerformancePresetInt( preset.name, "s_numberOfSpeakers", preset.numberOfSpeakers );
	passed &= Common_CheckPerformancePresetInt( preset.name, "s_useEAXReverb", preset.useEAXReverb );
	passed &= Common_CheckPerformancePresetInt( preset.name, "s_maxEmitterChannels", preset.maxEmitterChannels );
	passed &= Common_CheckPerformancePresetTargetsArchived( preset.name );
	return passed;
}

static void Com_PerformancePresetSelfTest_f( const idCmdArgs &args ) {
	bool passed = true;

	if ( !Common_CommandIsRegistered( "applyPerformancePreset" ) ) {
		common->Printf( "PerformancePreset self-test failed: applyPerformancePreset command is not registered\n" );
		passed = false;
	}
	if ( !Common_CommandIsRegistered( "autoDetectPerformancePreset" ) ) {
		common->Printf( "PerformancePreset self-test failed: autoDetectPerformancePreset command is not registered\n" );
		passed = false;
	}
	if ( !Common_CommandIsRegistered( "performancePresetSelfTest" ) ) {
		common->Printf( "PerformancePreset self-test failed: performancePresetSelfTest command is not registered\n" );
		passed = false;
	}

	const bool definitionsPassed = Common_ValidatePerformancePresetDefinitions();
	passed &= definitionsPassed;
	const bool targetsPassed = Common_ValidatePerformancePresetTargetCVars();
	passed &= targetsPassed;

	idStr reason;
	const char *detectedPresetName = Common_DetectPerformancePresetName( reason );
	if ( Common_FindPerformancePreset( detectedPresetName ) == NULL ) {
		common->Printf( "PerformancePreset self-test failed: auto-detect returned unknown preset '%s'\n", detectedPresetName != NULL ? detectedPresetName : "" );
		passed = false;
	}
	if ( idStr::Icmp( detectedPresetName, "ultra" ) == 0 ) {
		common->Printf( "PerformancePreset self-test failed: auto-detect selected ultra\n" );
		passed = false;
	}
	if ( Common_SanitizePerformancePresetMemoryMB( 0, OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB, OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB ) != OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB ||
		 Common_SanitizePerformancePresetMemoryMB( OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB + 1, OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB, OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB ) != OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB ||
		 Common_SanitizePerformancePresetMemoryMB( 6144, OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB, OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB ) != 6144 ) {
		common->Printf( "PerformancePreset self-test failed: memory telemetry sanitizer returned an unexpected value\n" );
		passed = false;
	}
	const int rawDetectedVideoRam = Sys_GetVideoRam();
	const int effectiveDetectedVideoRam = Common_SanitizePerformancePresetMemoryMB(
		rawDetectedVideoRam,
		OPENQ4_PERFORMANCE_PRESET_UNKNOWN_VIDEO_RAM_MB,
		OPENQ4_PERFORMANCE_PRESET_MAX_REASONABLE_VIDEO_RAM_MB );
	if ( rawDetectedVideoRam != effectiveDetectedVideoRam && idStr::Icmp( detectedPresetName, "quality" ) == 0 ) {
		common->Printf(
			"PerformancePreset self-test failed: auto-detect selected quality from sanitized VRAM telemetry (%s reported, %s effective)\n",
			Sys_FormatMemoryMB( rawDetectedVideoRam ).c_str(),
			Sys_FormatMemoryMB( effectiveDetectedVideoRam ).c_str() );
		passed = false;
	}

	openQ4PerformancePresetCVarBackup_t backups[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT];
	const int savedModifiedFlags = cvarSystem->GetModifiedFlags();
	Common_BackupPerformancePresetCVars( backups );

	if ( definitionsPassed && targetsPassed ) {
		for ( int i = 0; i < OPENQ4_PERFORMANCE_PRESET_COUNT; ++i ) {
			if ( !Common_ApplyPerformancePreset( OPENQ4_PERFORMANCE_PRESETS[i], true ) ) {
				common->Printf( "PerformancePreset self-test failed: %s did not apply cleanly\n", OPENQ4_PERFORMANCE_PRESETS[i].name );
				passed = false;
			}
			if ( !Common_ValidatePerformancePresetApplied( OPENQ4_PERFORMANCE_PRESETS[i] ) ) {
				passed = false;
			}
		}

		idCmdArgs explicitArgs( "applyPerformancePreset quality", false );
		if ( !Common_ApplyPerformancePresetCommand( explicitArgs, true ) ||
			 !Common_ValidatePerformancePresetApplied( *Common_FindPerformancePreset( "quality" ) ) ) {
			common->Printf( "PerformancePreset self-test failed: explicit command argument did not apply quality\n" );
			passed = false;
		}

		com_performancePreset.SetString( "performance" );
		idCmdArgs noArgArgs( "applyPerformancePreset", false );
		if ( !Common_ApplyPerformancePresetCommand( noArgArgs, true ) ||
			 !Common_ValidatePerformancePresetApplied( *Common_FindPerformancePreset( "performance" ) ) ) {
			common->Printf( "PerformancePreset self-test failed: no-argument command did not apply selected cvar value\n" );
			passed = false;
		}

		if ( Common_ApplyPerformancePresetCommand( idCmdArgs( "applyPerformancePreset not-a-preset", false ), true ) ||
			 !Common_ValidatePerformancePresetApplied( *Common_FindPerformancePreset( "performance" ) ) ) {
			common->Printf( "PerformancePreset self-test failed: invalid explicit command argument mutated preset state\n" );
			passed = false;
		}

		if ( Common_ApplyPerformancePresetCommand( idCmdArgs( "applyPerformancePreset quality extra", false ), true ) ||
			 !Common_ValidatePerformancePresetApplied( *Common_FindPerformancePreset( "performance" ) ) ) {
			common->Printf( "PerformancePreset self-test failed: extra explicit command argument mutated preset state\n" );
			passed = false;
		}

		com_performancePreset.SetString( "not-a-preset" );
		if ( idStr::Icmp( com_performancePreset.GetString(), OPENQ4_DEFAULT_PERFORMANCE_PRESET ) != 0 ) {
			common->Printf( "PerformancePreset self-test failed: invalid stored cvar did not normalize to '%s'\n", OPENQ4_DEFAULT_PERFORMANCE_PRESET );
			passed = false;
		}
		if ( !Common_ApplyPerformancePresetCommand( noArgArgs, true ) ||
			 !Common_ValidatePerformancePresetApplied( *Common_DefaultPerformancePreset() ) ) {
			common->Printf( "PerformancePreset self-test failed: invalid stored cvar did not normalize to or fall back to the default preset\n" );
			passed = false;
		}

		const openQ4PerformancePreset_t *detectedPreset = Common_FindPerformancePreset( detectedPresetName );
		if ( detectedPreset != NULL ) {
			idCmdArgs autoDetectArgs( "autoDetectPerformancePreset", false );
			if ( !Common_AutoDetectPerformancePresetCommand( autoDetectArgs, true ) ||
				 !Common_ValidatePerformancePresetApplied( *detectedPreset ) ) {
				common->Printf( "PerformancePreset self-test failed: auto-detect command did not apply detected preset '%s'\n", detectedPreset->name );
				passed = false;
			}
		}

		const openQ4PerformancePreset_t *performancePreset = Common_FindPerformancePreset( "performance" );
		if ( performancePreset != NULL ) {
			Common_ApplyPerformancePreset( *performancePreset, true );
			openQ4PerformancePreset_t invalidPreset = *performancePreset;
			invalidPreset.anisotropy = 17;
			if ( Common_ApplyPerformancePreset( invalidPreset, true ) ||
				 !Common_ValidatePerformancePresetApplied( *performancePreset ) ) {
				common->Printf( "PerformancePreset self-test failed: rejected target normalization did not roll back atomically\n" );
				passed = false;
			}
			if ( Common_AutoDetectPerformancePresetCommand( idCmdArgs( "autoDetectPerformancePreset unexpected", false ), true ) ||
				 !Common_ValidatePerformancePresetApplied( *performancePreset ) ) {
				common->Printf( "PerformancePreset self-test failed: invalid auto-detect command argument mutated preset state\n" );
				passed = false;
			}
		}
	}

	Common_RestorePerformancePresetCVars( backups );
	passed &= Common_ValidatePerformancePresetCVarsRestored( backups );
	Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );

	if ( passed ) {
		common->Printf( "PerformancePreset self-test passed (presets=%d autodetect=%s reason='%s')\n", OPENQ4_PERFORMANCE_PRESET_COUNT, detectedPresetName, reason.c_str() );
	} else {
		common->Printf( "PerformancePreset self-test failed\n" );
	}
}

typedef struct openQ4JobSelfTestStage_s {
	std::atomic<int> *phase;
	std::atomic<bool> *failed;
	int expectedPhase;
	int replacementPhase;
} openQ4JobSelfTestStage_t;

static void Common_JobSelfTestStage( const idJobContext &context ) {
	openQ4JobSelfTestStage_t *stage = static_cast<openQ4JobSelfTestStage_t *>( context.data );
	if ( stage == NULL || stage->phase == NULL || stage->failed == NULL ) {
		return;
	}
	if ( stage->phase->load( std::memory_order_acquire ) != stage->expectedPhase ) {
		stage->failed->store( true, std::memory_order_release );
	}
	stage->phase->store( stage->replacementPhase, std::memory_order_release );
}

static void Common_JobSelfTestIncrement( const idJobContext &context ) {
	std::atomic<int> *counter = static_cast<std::atomic<int> *>( context.data );
	if ( counter != NULL ) {
		counter->fetch_add( 1, std::memory_order_relaxed );
	}
}

static bool Common_JobSubmitSucceeded( const idJobSubmitResult result ) {
	return result == idJobSubmitResult::ACCEPTED ||
		result == idJobSubmitResult::EXECUTED_SYNCHRONOUSLY;
}

static void Com_JobsStats_f( const idCmdArgs &args ) {
	( void )args;
	const idJobSystemMetrics metrics = jobSystem.GetMetrics();
	common->Printf(
		"Job system: initialized=%s mode=%s workers=%u accepting=%s queued=%llu/%d highWater=%llu running=%llu sleeping=%llu\n",
		jobSystem.IsInitialized() ? "yes" : "no",
		metrics.synchronous ? "synchronous" : "threaded",
		metrics.workerThreads,
		metrics.accepting ? "yes" : "no",
		static_cast<unsigned long long>( metrics.queuedLists ),
		jobs_queueCapacity.GetInteger(),
		static_cast<unsigned long long>( metrics.queueHighWatermark ),
		static_cast<unsigned long long>( metrics.runningJobs ),
		static_cast<unsigned long long>( metrics.sleepingWorkers ) );
	common->Printf(
		"Job totals: lists submitted=%llu completed=%llu cancelled=%llu failed=%llu rejected=%llu; jobs submitted=%llu executed=%llu cancelled=%llu failed=%llu; wakes=%llu priorityPromotions=%llu execution=%lluus wait=%lluus\n",
		static_cast<unsigned long long>( metrics.submittedLists ),
		static_cast<unsigned long long>( metrics.completedLists ),
		static_cast<unsigned long long>( metrics.cancelledLists ),
		static_cast<unsigned long long>( metrics.failedLists ),
		static_cast<unsigned long long>( metrics.rejectedSubmissions ),
		static_cast<unsigned long long>( metrics.submittedJobs ),
		static_cast<unsigned long long>( metrics.executedJobs ),
		static_cast<unsigned long long>( metrics.cancelledJobs ),
		static_cast<unsigned long long>( metrics.failedJobs ),
		static_cast<unsigned long long>( metrics.workerWakeups ),
		static_cast<unsigned long long>( metrics.priorityPromotions ),
		static_cast<unsigned long long>( metrics.totalExecutionMicroseconds ),
		static_cast<unsigned long long>( metrics.totalWaitMicroseconds ) );
}

static void Com_JobsSelfTest_f( const idCmdArgs &args ) {
	( void )args;
	bool passed = jobSystem.IsInitialized();
	std::atomic<int> phase( 0 );
	std::atomic<bool> dependencyFailed( false );
	std::atomic<int> parallelCount( 0 );
	openQ4JobSelfTestStage_t rootData = { &phase, &dependencyFailed, 0, 1 };
	openQ4JobSelfTestStage_t dependentData = { &phase, &dependencyFailed, 1, 2 };
	const idJobSystemMetrics before = jobSystem.GetMetrics();

	std::unique_ptr<idJobList> root = jobSystem.CreateJobList( "engine-self-test-root", 1, 0, idJobPriority::HIGH );
	std::unique_ptr<idJobList> dependent = jobSystem.CreateJobList( "engine-self-test-dependent", 1, 1, idJobPriority::NORMAL );
	std::unique_ptr<idJobList> batch = jobSystem.CreateJobList( "engine-self-test-batch", 8, 0, idJobPriority::LOW );
	passed &= root != NULL && dependent != NULL && batch != NULL;
	if ( passed ) {
		passed &= root->AddJob( Common_JobSelfTestStage, &rootData );
		passed &= Common_JobSubmitSucceeded( root->Submit() );
		passed &= root->Wait();
		passed &= dependent->AddDependency( *root );
		passed &= dependent->AddJob( Common_JobSelfTestStage, &dependentData );
		passed &= Common_JobSubmitSucceeded( dependent->Submit() );
		passed &= dependent->Wait();
		for ( int jobIndex = 0; jobIndex < 8; ++jobIndex ) {
			passed &= batch->AddJob( Common_JobSelfTestIncrement, &parallelCount );
		}
		passed &= Common_JobSubmitSucceeded( batch->Submit() );
		passed &= batch->Wait();
	}

	const idJobSystemMetrics after = jobSystem.GetMetrics();
	passed &= !dependencyFailed.load( std::memory_order_acquire );
	passed &= phase.load( std::memory_order_acquire ) == 2;
	passed &= parallelCount.load( std::memory_order_acquire ) == 8;
	passed &= after.submittedLists >= before.submittedLists + 3;
	passed &= after.executedJobs >= before.executedJobs + 10;
	passed &= after.failedLists == before.failedLists;

	if ( passed ) {
		common->Printf( "jobsSelfTest PASS v1 mode=%s workers=%u phase=%d batch=%d\n",
			after.synchronous ? "synchronous" : "threaded",
			after.workerThreads,
			phase.load(),
			parallelCount.load() );
	} else {
		common->Warning( "jobsSelfTest FAILED v1 root=%s dependent=%s batch=%s phase=%d batchCount=%d",
			root != NULL ? idJobListStatusName( root->GetStatus() ) : "unavailable",
			dependent != NULL ? idJobListStatusName( dependent->GetStatus() ) : "unavailable",
			batch != NULL ? idJobListStatusName( batch->GetStatus() ) : "unavailable",
			phase.load(),
			parallelCount.load() );
	}
}

static void Common_NetIPv4SelfTestFailure( bool &passed, const char *reason ) {
	common->Printf( "IPv4 network self-test failure: %s\n", reason );
	passed = false;
}

static void Com_NetIPv4SelfTest_f( const idCmdArgs &args ) {
	(void)args;
	bool passed = true;

	netadr_t address;
	if ( !Sys_StringToNetAdr( "127.0.0.1:65535", &address, false ) ||
			( address.type != NA_LOOPBACK && address.type != NA_IP ) ||
			address.ip[0] != 127 || address.ip[1] != 0 || address.ip[2] != 0 || address.ip[3] != 1 || address.port != 65535 ) {
		Common_NetIPv4SelfTestFailure( passed, "numeric IPv4 endpoint did not round-trip" );
	}
	if ( !Sys_StringToNetAdr( "255.255.255.255:65535", &address, false ) ||
			address.type != NA_IP || address.ip[0] != 255 || address.ip[1] != 255 ||
			address.ip[2] != 255 || address.ip[3] != 255 || address.port != 65535 ) {
		Common_NetIPv4SelfTestFailure( passed, "IPv4 broadcast literal did not parse" );
	}
	if ( Sys_StringToNetAdr( "localhost", &address, false ) || address.type != NA_BAD ) {
		Common_NetIPv4SelfTestFailure( passed, "numeric-only resolution accepted a hostname" );
	}
	if ( !Sys_StringToNetAdr( "localhost:65535", &address, true ) ||
			( address.type != NA_LOOPBACK && address.type != NA_IP ) ||
			address.port != 65535 ) {
		Common_NetIPv4SelfTestFailure( passed, "DNS-enabled localhost did not resolve to IPv4" );
	}
	if ( Sys_StringToNetAdr( "127.0.0.1:65536", &address, false ) || address.type != NA_BAD ) {
		Common_NetIPv4SelfTestFailure( passed, "out-of-range port was accepted" );
	}

	static const char * const testCVarNames[] = { "net_ip", "net_forceLatency", "net_forceDrop" };
	static const char * const testCVarValues[] = { "127.0.0.1", "0", "0" };
	idStr savedCVarValues[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ];
	int savedCVarFlags[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ] = {};
	bool savedCVars[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ] = {};
	const int savedModifiedFlags = cvarSystem->GetModifiedFlags();
	for ( int i = 0; i < static_cast<int>( sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ); ++i ) {
		idCVar *cvar = cvarSystem->Find( testCVarNames[i] );
		if ( cvar == NULL ) {
			continue;
		}
		savedCVars[i] = true;
		savedCVarValues[i] = cvar->GetString();
		savedCVarFlags[i] = cvar->GetFlags();
		cvarSystem->SetCVarString( testCVarNames[i], testCVarValues[i] );
	}

	idPort invalidPort;
	if ( invalidPort.InitForPort( -2 ) || invalidPort.GetPort() != 0 ) {
		Common_NetIPv4SelfTestFailure( passed, "idPort accepted port -2" );
	}
	if ( invalidPort.InitForPort( 65536 ) || invalidPort.GetPort() != 0 ) {
		Common_NetIPv4SelfTestFailure( passed, "idPort accepted port 65536" );
	}

	idPort sender;
	idPort receiver;
	int receiverPort = 0;
	for ( int candidatePort = 65535; candidatePort >= 65472; --candidatePort ) {
		if ( receiver.InitForPort( candidatePort ) ) {
			receiverPort = candidatePort;
			break;
		}
	}
	if ( receiverPort == 0 || receiver.GetPort() != receiverPort || !sender.InitForPort( PORT_ANY ) ) {
		Common_NetIPv4SelfTestFailure( passed, "could not open IPv4 loopback sockets" );
	} else {
		if ( sender.packetsWritten != 0 || sender.bytesWritten != 0 || receiver.packetsRead != 0 || receiver.bytesRead != 0 ) {
			Common_NetIPv4SelfTestFailure( passed, "socket counters were not initialized" );
		}

		netadr_t destination;
		if ( !Sys_StringToNetAdr( va( "127.0.0.1:%d", receiverPort ), &destination, false ) ) {
			Common_NetIPv4SelfTestFailure( passed, "could not create the loopback destination" );
		} else {
			static const char payload[] = "openQ4 IPv4 loopback";
			sender.SendPacket( destination, payload, static_cast<int>( sizeof( payload ) ) );

			char received[sizeof( payload )];
			int receivedSize = 0;
			netadr_t source;
			if ( !receiver.GetPacketBlocking( source, received, receivedSize, sizeof( received ), 1000 ) ||
					receivedSize != sizeof( payload ) || memcmp( received, payload, sizeof( payload ) ) != 0 ||
					!Sys_CompareNetAdrBase( source, destination ) ) {
				Common_NetIPv4SelfTestFailure( passed, "UDP loopback payload did not round-trip" );
			}

			static const char reply[] = "openQ4 IPv4 reply";
			receiver.SendPacket( source, reply, static_cast<int>( sizeof( reply ) ) );
			netadr_t replySource;
			receivedSize = 0;
			if ( !sender.GetPacketBlocking( replySource, received, receivedSize, sizeof( received ), 1000 ) ||
					receivedSize != sizeof( reply ) || memcmp( received, reply, sizeof( reply ) ) != 0 ||
					!Sys_CompareNetAdrBase( replySource, destination ) || replySource.port != destination.port ) {
				Common_NetIPv4SelfTestFailure( passed, "UDP loopback reply did not round-trip" );
			}

			byte exactPacket[64];
			for ( int i = 0; i < static_cast<int>( sizeof( exactPacket ) ); ++i ) {
				exactPacket[i] = static_cast<byte>( i );
			}
			sender.SendPacket( destination, exactPacket, sizeof( exactPacket ) );
			byte exactReceived[sizeof( exactPacket )];
			receivedSize = 0;
			if ( !receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) ||
					receivedSize != sizeof( exactPacket ) || memcmp( exactReceived, exactPacket, sizeof( exactPacket ) ) != 0 ) {
				Common_NetIPv4SelfTestFailure( passed, "exact-capacity UDP datagram was rejected" );
			}

			byte oversizePacket[sizeof( exactPacket ) + 1];
			memset( oversizePacket, 0x5a, sizeof( oversizePacket ) );
			static const byte markerPacket[] = { 0x49, 0x50, 0x76, 0x34 };
			sender.SendPacket( destination, oversizePacket, sizeof( oversizePacket ) );
			sender.SendPacket( destination, markerPacket, sizeof( markerPacket ) );
			receivedSize = -1;
			if ( receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) || receivedSize != 0 ) {
				Common_NetIPv4SelfTestFailure( passed, "oversized UDP datagram was not rejected" );
			}
			receivedSize = 0;
			if ( !receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) ||
					receivedSize != sizeof( markerPacket ) || memcmp( exactReceived, markerPacket, sizeof( markerPacket ) ) != 0 ) {
				Common_NetIPv4SelfTestFailure( passed, "packet after oversized UDP datagram was not preserved" );
			}
		}

		const int previousPort = receiver.GetPort();
		receiver.Close();
		if ( receiver.GetPort() != 0 || !receiver.InitForPort( PORT_ANY ) || receiver.GetPort() == 0 || previousPort == 0 ) {
			Common_NetIPv4SelfTestFailure( passed, "socket close/reopen lifecycle failed" );
		}
	}

	sender.Close();
	receiver.Close();
	for ( int i = 0; i < static_cast<int>( sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ); ++i ) {
		if ( !savedCVars[i] ) {
			continue;
		}
		cvarSystem->SetCVarString( testCVarNames[i], savedCVarValues[i].c_str() );
		Common_RestorePerformancePresetCVarFlags( cvarSystem->Find( testCVarNames[i] ), savedCVarFlags[i] );
	}
	Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );
	if ( passed ) {
		common->Printf( "IPv4 network self-test: passed\n" );
	} else {
		common->Printf( "IPv4 network self-test: FAILED\n" );
	}
}

static void Common_NetIPv6SelfTestFailure( bool &passed, const char *reason ) {
	common->Printf( "IPv6 network self-test failure: %s\n", reason );
	passed = false;
}

static void Com_NetIPv6SelfTest_f( const idCmdArgs &args ) {
	(void)args;
	bool passed = true;

	netadr_t address;
	if ( !Sys_StringToNetAdr( "[::1]:65535", &address, false ) || address.type != NA_IP6 ||
			!idNetworkEndpoint::IsIPv6Loopback( address.ip6 ) || address.port != 65535 ) {
		Common_NetIPv6SelfTestFailure( passed, "bracketed IPv6 endpoint did not round-trip" );
	}
	if ( !Sys_StringToNetAdr( "::1", &address, false ) || address.type != NA_IP6 ||
			!idNetworkEndpoint::IsIPv6Loopback( address.ip6 ) || address.port != 0 ) {
		Common_NetIPv6SelfTestFailure( passed, "unbracketed IPv6 literal did not parse" );
	}
	if ( Sys_StringToNetAdr( "[::1]:65536", &address, false ) || address.type != NA_BAD ) {
		Common_NetIPv6SelfTestFailure( passed, "out-of-range IPv6 port was accepted" );
	}
	if ( Sys_StringToNetAdr( "[::1", &address, false ) || address.type != NA_BAD ) {
		Common_NetIPv6SelfTestFailure( passed, "unterminated IPv6 bracket was accepted" );
	}
	// An IPv4 peer arriving through the mapped range must resolve to one
	// identity, not a second IPv6 shaped one.
	if ( !Sys_StringToNetAdr( "[::ffff:127.0.0.1]:65535", &address, false ) || address.type != NA_LOOPBACK ||
			address.ip[0] != 127 || address.ip[1] != 0 || address.ip[2] != 0 || address.ip[3] != 1 || address.port != 65535 ) {
		Common_NetIPv6SelfTestFailure( passed, "IPv4-mapped address was not normalized" );
	}

	// The formatted text is the identity key for the server browser and for the
	// game module's ban list, so it has to be canonical and re-parseable.
	if ( !Sys_StringToNetAdr( "[2001:0db8:0000:0000:0000:0000:0000:0001]:27650", &address, false ) ||
			idStr::Cmp( Sys_NetAdrToString( address ), "[2001:db8::1]:27650" ) != 0 ) {
		Common_NetIPv6SelfTestFailure( passed, "IPv6 text was not canonicalized" );
	}
	netadr_t reparsed;
	if ( !Sys_StringToNetAdr( "[2001:db8::1]:27650", &address, false ) ||
			!Sys_StringToNetAdr( Sys_NetAdrToString( address ), &reparsed, false ) ||
			!Sys_CompareNetAdrBase( address, reparsed ) || reparsed.port != address.port ) {
		Common_NetIPv6SelfTestFailure( passed, "IPv6 address text did not survive a round-trip" );
	}
	netadr_t other;
	if ( !Sys_StringToNetAdr( "[2001:db8::2]:27650", &other, false ) || Sys_CompareNetAdrBase( address, other ) ) {
		Common_NetIPv6SelfTestFailure( passed, "distinct IPv6 addresses compared equal" );
	}
	if ( !Sys_StringToNetAdr( "[::1]:27650", &address, false ) || !Sys_IsLANAddress( address ) ) {
		Common_NetIPv6SelfTestFailure( passed, "IPv6 loopback was not treated as a LAN address" );
	}
	if ( !Sys_StringToNetAdr( "[fe80::1]:27650", &address, false ) || !Sys_IsLANAddress( address ) ) {
		Common_NetIPv6SelfTestFailure( passed, "IPv6 link-local was not treated as a LAN address" );
	}

	static const char * const testCVarNames[] = { "net_ip", "net_ip6", "net_enableIPv4", "net_enableIPv6", "net_forceLatency", "net_forceDrop" };
	static const char * const testCVarValues[] = { "127.0.0.1", "::1", "1", "1", "0", "0" };
	idStr savedCVarValues[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ];
	int savedCVarFlags[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ] = {};
	bool savedCVars[ sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ] = {};
	const int savedModifiedFlags = cvarSystem->GetModifiedFlags();
	for ( int i = 0; i < static_cast<int>( sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ); ++i ) {
		idCVar *cvar = cvarSystem->Find( testCVarNames[i] );
		if ( cvar == NULL ) {
			continue;
		}
		savedCVars[i] = true;
		savedCVarValues[i] = cvar->GetString();
		savedCVarFlags[i] = cvar->GetFlags();
		cvarSystem->SetCVarString( testCVarNames[i], testCVarValues[i] );
	}

	idPort invalidPort;
	if ( invalidPort.InitForPort( -2 ) || invalidPort.GetPort() != 0 ) {
		Common_NetIPv6SelfTestFailure( passed, "idPort accepted port -2" );
	}
	if ( invalidPort.InitForPort( 65536 ) || invalidPort.GetPort() != 0 ) {
		Common_NetIPv6SelfTestFailure( passed, "idPort accepted port 65536" );
	}

	// Bind IPv6 alone for the transport checks so every datagram below provably
	// travels over the IPv6 socket rather than falling back to IPv4.
	cvarSystem->SetCVarString( "net_enableIPv4", "0" );

	idPort sender;
	idPort receiver;
	int receiverPort = 0;
	for ( int candidatePort = 65471; candidatePort >= 65408; --candidatePort ) {
		if ( receiver.InitForPort( candidatePort ) ) {
			receiverPort = candidatePort;
			break;
		}
	}
	if ( receiverPort == 0 || receiver.GetPort() != receiverPort || !sender.InitForPort( PORT_ANY ) ) {
		// A host with IPv6 switched off is a supported configuration, so this
		// reports an unavailable transport instead of a failed contract.
		common->Printf( "IPv6 network self-test: skipped transport checks - the host bound no IPv6 socket\n" );
	} else {
		if ( sender.packetsWritten != 0 || sender.bytesWritten != 0 || receiver.packetsRead != 0 || receiver.bytesRead != 0 ) {
			Common_NetIPv6SelfTestFailure( passed, "socket counters were not initialized" );
		}

		netadr_t destination;
		if ( !Sys_StringToNetAdr( va( "[::1]:%d", receiverPort ), &destination, false ) || destination.type != NA_IP6 ) {
			Common_NetIPv6SelfTestFailure( passed, "could not create the IPv6 loopback destination" );
		} else {
			static const char payload[] = "openQ4 IPv6 loopback";
			sender.SendPacket( destination, payload, static_cast<int>( sizeof( payload ) ) );

			char received[sizeof( payload )];
			int receivedSize = 0;
			netadr_t source;
			if ( !receiver.GetPacketBlocking( source, received, receivedSize, sizeof( received ), 1000 ) ||
					receivedSize != sizeof( payload ) || memcmp( received, payload, sizeof( payload ) ) != 0 ||
					source.type != NA_IP6 || !Sys_CompareNetAdrBase( source, destination ) ) {
				Common_NetIPv6SelfTestFailure( passed, "IPv6 UDP loopback payload did not round-trip" );
			}

			static const char reply[] = "openQ4 IPv6 reply";
			receiver.SendPacket( source, reply, static_cast<int>( sizeof( reply ) ) );
			netadr_t replySource;
			receivedSize = 0;
			if ( !sender.GetPacketBlocking( replySource, received, receivedSize, sizeof( received ), 1000 ) ||
					receivedSize != sizeof( reply ) || memcmp( received, reply, sizeof( reply ) ) != 0 ||
					!Sys_CompareNetAdrBase( replySource, destination ) || replySource.port != destination.port ) {
				Common_NetIPv6SelfTestFailure( passed, "IPv6 UDP loopback reply did not round-trip" );
			}

			byte exactPacket[64];
			for ( int i = 0; i < static_cast<int>( sizeof( exactPacket ) ); ++i ) {
				exactPacket[i] = static_cast<byte>( i );
			}
			sender.SendPacket( destination, exactPacket, sizeof( exactPacket ) );
			byte exactReceived[sizeof( exactPacket )];
			receivedSize = 0;
			if ( !receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) ||
					receivedSize != sizeof( exactPacket ) || memcmp( exactReceived, exactPacket, sizeof( exactPacket ) ) != 0 ) {
				Common_NetIPv6SelfTestFailure( passed, "exact-capacity IPv6 UDP datagram was rejected" );
			}

			byte oversizePacket[sizeof( exactPacket ) + 1];
			memset( oversizePacket, 0x5a, sizeof( oversizePacket ) );
			static const byte markerPacket[] = { 0x49, 0x50, 0x76, 0x36 };
			sender.SendPacket( destination, oversizePacket, sizeof( oversizePacket ) );
			sender.SendPacket( destination, markerPacket, sizeof( markerPacket ) );
			receivedSize = -1;
			if ( receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) || receivedSize != 0 ) {
				Common_NetIPv6SelfTestFailure( passed, "oversized IPv6 UDP datagram was not rejected" );
			}
			receivedSize = 0;
			if ( !receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) ||
					receivedSize != sizeof( markerPacket ) || memcmp( exactReceived, markerPacket, sizeof( markerPacket ) ) != 0 ) {
				Common_NetIPv6SelfTestFailure( passed, "packet after oversized IPv6 UDP datagram was not preserved" );
			}
		}

		const int previousPort = receiver.GetPort();
		receiver.Close();
		if ( receiver.GetPort() != 0 || !receiver.InitForPort( PORT_ANY ) || receiver.GetPort() == 0 || previousPort == 0 ) {
			Common_NetIPv6SelfTestFailure( passed, "IPv6 socket close/reopen lifecycle failed" );
		}

		// One endpoint must serve both families at the same port, which is what
		// lets a dual-stack server publish a single address to every client.
		cvarSystem->SetCVarString( "net_enableIPv4", "1" );
		idPort dualStack;
		if ( !dualStack.InitForPort( PORT_ANY ) || dualStack.GetPort() == 0 ) {
			Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not bind" );
		} else {
			netadr_t dualDestination;
			static const char dualPayload[] = "openQ4 dual stack";
			char dualReceived[sizeof( dualPayload )];
			int dualSize = 0;
			netadr_t dualSource;
			if ( !Sys_StringToNetAdr( va( "[::1]:%d", dualStack.GetPort() ), &dualDestination, false ) ) {
				Common_NetIPv6SelfTestFailure( passed, "could not address the dual-stack port over IPv6" );
			} else {
				sender.SendPacket( dualDestination, dualPayload, static_cast<int>( sizeof( dualPayload ) ) );
				if ( !dualStack.GetPacketBlocking( dualSource, dualReceived, dualSize, sizeof( dualReceived ), 1000 ) ||
						dualSize != sizeof( dualPayload ) || dualSource.type != NA_IP6 ) {
					Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not receive over IPv6" );
				}
			}
			if ( !Sys_StringToNetAdr( va( "127.0.0.1:%d", dualStack.GetPort() ), &dualDestination, false ) ) {
				Common_NetIPv6SelfTestFailure( passed, "could not address the dual-stack port over IPv4" );
			} else {
				idPort ipv4Sender;
				if ( !ipv4Sender.InitForPort( PORT_ANY ) ) {
					Common_NetIPv6SelfTestFailure( passed, "could not open an IPv4 sender for the dual-stack port" );
				} else {
					dualSize = 0;
					ipv4Sender.SendPacket( dualDestination, dualPayload, static_cast<int>( sizeof( dualPayload ) ) );
					if ( !dualStack.GetPacketBlocking( dualSource, dualReceived, dualSize, sizeof( dualReceived ), 1000 ) ||
							dualSize != sizeof( dualPayload ) || dualSource.type == NA_IP6 ) {
						Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not receive over IPv4" );
					}
				}
				ipv4Sender.Close();
			}
		}
		dualStack.Close();
	}

	sender.Close();
	receiver.Close();
	for ( int i = 0; i < static_cast<int>( sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ); ++i ) {
		if ( !savedCVars[i] ) {
			continue;
		}
		cvarSystem->SetCVarString( testCVarNames[i], savedCVarValues[i].c_str() );
		Common_RestorePerformancePresetCVarFlags( cvarSystem->Find( testCVarNames[i] ), savedCVarFlags[i] );
	}
	Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );
	if ( passed ) {
		common->Printf( "IPv6 network self-test: passed\n" );
	} else {
		common->Printf( "IPv6 network self-test: FAILED\n" );
	}
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
=================
Com_ReloadGameModule_f
=================
*/
void Com_ReloadGameModule_f( const idCmdArgs &args ) {
	if ( !commonLocal.IsInitialized() ) {
		return;
	}

	// Copy CVar-backed strings before teardown. Config replay and module unload
	// can replace their backing storage while this command is still running.
	const idStr requestedModule = cvarSystem->GetCVarString( "com_nextGameModule" );
	if ( !openQ4_IsValidGameModuleName( requestedModule.c_str() ) ) {
		common->Printf( "reloadGameModule requested without a valid com_nextGameModule; falling back to reloadEngine\n" );
		Com_ReloadEngine_f( args );
		return;
	}
	const idStr previousModule = cvarSystem->GetCVarString( "com_activeGameModule" );

	common->Printf( "============= ReloadGameModule start =============\n" );
	fileSystem->SetIsFileLoadingAllowed( true );

#ifndef ID_DEDICATED
	if ( !com_skipRenderer.GetBool() && renderSystem->IsOpenGLRunning() ) {
		commonLocal.PrintLoadingMessage( common->GetLanguageDict()->GetString( "#str_104350" ) );
	}
#endif

	// A module swap tears down and rebuilds the renderer, the sound system and
	// the whole decl set in the middle of a menu click. Without a guard an
	// ERP_DROP anywhere in there unwinds all the way to idCommonLocal::Frame,
	// which swallows it silently and leaves a half-built engine with no clue in
	// the log about which half failed.
	idStr swapError;
	idStr cleanupError;
	idStr rollbackError;
	gameModuleLoadPhase_t failedPhase = GAME_MODULE_PHASE_IDLE;
	bool swapFailed = false;
	bool oldShutdownComplete = false;
	bool newInitStarted = false;
	try {
		GLimp_PreserveWindowOnShutdown( true );
		commonLocal.ShutdownGame( true );
		oldShutdownComplete = true;
		GLimp_PreserveWindowOnShutdown( false );
		newInitStarted = true;
		commonLocal.InitGame();
	}
	catch( idException &ex ) {
		GLimp_PreserveWindowOnShutdown( false );
		swapFailed = true;
		swapError = ex.error;
		failedPhase = Com_GetGameModuleLoadPhase();

		// If the replacement InitGame started, its decls, renderer, UI or game
		// object may be only partly initialized. Tear that attempt back down
		// before trying to restore the previously active module.
		if ( newInitStarted ) {
			try {
				commonLocal.ShutdownGame( true );
			}
			catch( idException &cleanupException ) {
				cleanupError = cleanupException.error;
			}
		}
	}

	if ( swapFailed ) {
		cvarSystem->SetCVarString( "com_nextGameModule", "" );
		common->Warning(
			"game module swap to '%s' failed at phase '%s': %s",
			requestedModule.c_str(),
			Com_GameModuleLoadPhaseName( failedPhase ),
			swapError.c_str() );

		bool rollbackSucceeded = false;
		if ( oldShutdownComplete && cleanupError.IsEmpty() && openQ4_IsValidGameModuleName( previousModule.c_str() ) ) {
			// The requested si_gameType already names the new module by this point.
			// Restore a safe canonical mode for the old module before declarations
			// are rebuilt; the user's archived settings replay normally afterwards.
			cvarSystem->SetCVarString(
				"si_gameType",
				idStr::Icmp( previousModule.c_str(), "game_mp" ) == 0 ? "DM" : "singleplayer" );
			cvarSystem->SetCVarString( "com_nextGameModule", previousModule.c_str() );
			try {
				commonLocal.InitGame();
				rollbackSucceeded = true;
			}
			catch( idException &rollbackException ) {
				rollbackError = rollbackException.error;
			}
		} else if ( !cleanupError.IsEmpty() ) {
			rollbackError = va( "partial replacement cleanup failed: %s", cleanupError.c_str() );
		} else if ( !oldShutdownComplete ) {
			rollbackError = "the previous module did not finish shutting down";
		} else {
			rollbackError = "the previously active module name was unavailable";
		}

		if ( !rollbackSucceeded ) {
			common->FatalError(
				"game module swap to '%s' failed (%s), and rollback to '%s' failed (%s)",
				requestedModule.c_str(),
				swapError.c_str(),
				previousModule.c_str(),
				rollbackError.c_str() );
			return;
		}

		common->Printf( "============= ReloadGameModule failed ============\n" );
		// Whatever was queued to run after the swap assumed the new module
		// came up. Do not run it; the next successful swap overwrites it.
#ifndef ID_DEDICATED
		// ArenaCampaign::Shutdown deliberately preserves its transaction across
		// the expected game_sp -> game_mp swap. If InitGame failed, that handoff
		// will never complete, so release the token and restore the saved server
		// settings before returning to a usable menu.
		if ( arenaCampaign.NeedsCleanup() ) {
			arenaCampaign.AbortMatch();
		}
#endif
		session->StartMenu();
		fileSystem->SetIsFileLoadingAllowed( false );
		return;
	}

	common->Printf( "============= ReloadGameModule end ===============\n" );

	if ( !cmdSystem->PostReloadEngine() ) {
#ifndef ID_DEDICATED
		// A successful module load is not enough for Arena: its replayed
		// spawnServer command is the second half of the transaction. If that
		// command disappeared, fail closed instead of leaving an active token
		// and archived multiplayer overrides stranded behind the main menu.
		const bool abandonedArenaLaunch = arenaCampaign.NeedsCleanup();
		if ( abandonedArenaLaunch ) {
			arenaCampaign.AbortMatch();
		}
#endif
		session->StartMenu();
#ifndef ID_DEDICATED
		if ( abandonedArenaLaunch ) {
			arenaCampaign.OpenBrowser();
		}
#endif
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
	
	lang.Strip( ' ' );
	lang.Strip( '\t' );
	lang.Strip( '\r' );
	lang.Strip( '\n' );
	lang.ToLower();

	idStr temp;
	for( int i = 0; i < list->Num(); i++ ) {
		temp = (*list)[i];
		temp = temp.Right( temp.Length() - idStr::Length( "strings/" ) );
		temp = temp.Left(lang.Length());
		if(idStr::Icmp(temp, lang) != 0) {
			list->RemoveIndex(i);
			i--;
		}
	}
}

static void Common_NormalizeLanguageName( idStr &language ) {
	language.Strip( ' ' );
	language.Strip( '\t' );
	language.Strip( '\r' );
	language.Strip( '\n' );
	language.ToLower();
	if ( language.IsEmpty() ) {
		language = "english";
	}
}

static bool Common_TrySelectLanguage( const idStrList &languages, const char *candidate, idStr &selected ) {
	if ( candidate == NULL || candidate[ 0 ] == '\0' ) {
		return false;
	}

	for ( int i = 0; i < languages.Num(); ++i ) {
		if ( !languages[ i ].Icmp( candidate ) ) {
			selected = languages[ i ];
			return true;
		}
	}

	return false;
}

static void Common_AppendUniqueLanguage( idStrList &languages, const char *language ) {
	idStr selected;
	if ( language == NULL || language[ 0 ] == '\0' || Common_TrySelectLanguage( languages, language, selected ) ) {
		return;
	}
	languages.Append( language );
}

/*
===============
Common_AppendLanguagesWithStringTables

ListAvailableLanguagePacks only sees the retail zpak_<language> media archives,
because those are what carry a language's recorded dialogue. That is the right
test for a retail localisation, but it is the wrong one for a language whose
text ships in the base paks with no media pack of its own - openQ4 translates
its own strings for languages id never recorded VO for, and gating on the media
pack rejected them before the tables were ever consulted.

Take the union instead: a language is selectable if it has a media pack or a
string table. The dictionary loads English first and overlays the selected
language, so a text-only language still renders, just with English audio.
===============
*/
static void Common_AppendLanguagesWithStringTables( const idStrList &langFiles, idStrList &languages ) {
	const int prefixLength = idStr::Length( "strings/" );

	for ( int i = 0; i < langFiles.Num(); ++i ) {
		idStr fileName = langFiles[ i ];
		if ( fileName.Length() <= prefixLength ) {
			continue;
		}
		fileName = fileName.Right( fileName.Length() - prefixLength );

		// "<language>_<category>.lang"
		const int separator = fileName.Find( '_' );
		if ( separator <= 0 ) {
			continue;
		}
		fileName.CapLength( separator );
		fileName.ToLower();

		// Only names the engine actually knows, so a stray file cannot invent a
		// language the rest of the system has no font or codepage policy for.
		for ( int name = 0; sysLanguageNames[ name ] != NULL; ++name ) {
			if ( !fileName.Icmp( sysLanguageNames[ name ] ) ) {
				Common_AppendUniqueLanguage( languages, sysLanguageNames[ name ] );
				break;
			}
		}
	}
}

static const char *Common_MapLocaleLanguageCode( const char *languageCode ) {
	struct languageMap_t {
		const char *code;
		const char *sysLang;
	};
	static const languageMap_t languageMap[] = {
		{ "en", "english" },
		{ "es", "spanish" },
		{ "fr", "french" },
		{ "it", "italian" },
		{ "de", "german" },
		{ "ru", "russian" },
		{ "pl", "polish" },
		{ "ko", "korean" },
		{ "kr", "korean" },
		{ "ja", "japanese" },
		{ "jp", "japanese" },
		{ "zh", "chinese" },
		{ NULL, NULL }
	};

	if ( languageCode == NULL || languageCode[ 0 ] == '\0' ) {
		return NULL;
	}

	for ( int i = 0; languageMap[ i ].code != NULL; ++i ) {
		if ( !idStr::Icmp( languageCode, languageMap[ i ].code ) ) {
			return languageMap[ i ].sysLang;
		}
	}

	return NULL;
}

static void Common_AppendPreferredLanguageFromLocaleToken( const char *localeToken, idStrList &preferredLanguages ) {
	idStr languageCode;
	int cutLength;

	if ( localeToken == NULL || localeToken[ 0 ] == '\0' ) {
		return;
	}

	languageCode = localeToken;
	languageCode.Strip( ' ' );
	languageCode.Strip( '\t' );
	languageCode.Strip( '\r' );
	languageCode.Strip( '\n' );
	if ( languageCode.IsEmpty() || !languageCode.Icmp( "C" ) || !languageCode.Icmp( "POSIX" ) ) {
		return;
	}

	languageCode.ToLower();
	cutLength = languageCode.Length();
	const char delimiters[] = { '_', '-', '.', '@' };
	const int numDelimiters = sizeof( delimiters ) / sizeof( delimiters[ 0 ] );
	for ( int i = 0; i < numDelimiters; ++i ) {
		const int delimiterIndex = languageCode.Find( delimiters[ i ] );
		if ( delimiterIndex >= 0 && delimiterIndex < cutLength ) {
			cutLength = delimiterIndex;
		}
	}
	languageCode.CapLength( cutLength );

	Common_AppendUniqueLanguage( preferredLanguages, Common_MapLocaleLanguageCode( languageCode.c_str() ) );
}

static void Common_AppendPreferredLanguagesFromLocaleList( const char *localeList, idStrList &preferredLanguages ) {
	idStr token;

	if ( localeList == NULL || localeList[ 0 ] == '\0' ) {
		return;
	}

	for ( int i = 0; ; ++i ) {
		const char ch = localeList[ i ];
		if ( ch == ':' || ch == ';' || ch == '\0' ) {
			Common_AppendPreferredLanguageFromLocaleToken( token.c_str(), preferredLanguages );
			token.Clear();
			if ( ch == '\0' ) {
				break;
			}
			continue;
		}
		token.Append( ch );
	}
}

#if defined( _WIN32 )
static const char *Common_MapWindowsPrimaryLanguage( WORD primaryLanguage ) {
	switch ( primaryLanguage ) {
		case LANG_ENGLISH: return "english";
		case LANG_SPANISH: return "spanish";
		case LANG_FRENCH: return "french";
		case LANG_ITALIAN: return "italian";
		case LANG_GERMAN: return "german";
		case LANG_RUSSIAN: return "russian";
		case LANG_POLISH: return "polish";
		case LANG_KOREAN: return "korean";
		case LANG_JAPANESE: return "japanese";
		case LANG_CHINESE: return "chinese";
		default: return NULL;
	}
}
#endif

static void Common_GetOSPreferredLanguages( idStrList &preferredLanguages ) {
	preferredLanguages.Clear();

#if defined( USE_SDL3 )
	int localeCount = 0;
	SDL_Locale **locales = SDL_GetPreferredLocales( &localeCount );
	if ( locales != NULL ) {
		for ( int i = 0; i < localeCount && locales[ i ] != NULL; ++i ) {
			Common_AppendPreferredLanguageFromLocaleToken( locales[ i ]->language, preferredLanguages );
		}
		SDL_free( locales );
	}
#endif

#if defined( _WIN32 )
	Common_AppendUniqueLanguage(
		preferredLanguages,
		Common_MapWindowsPrimaryLanguage( PRIMARYLANGID( GetUserDefaultUILanguage() ) ) );
#endif

	const char *envNames[] = {
		"LANGUAGE",
		"LC_ALL",
		"LC_MESSAGES",
		"LANG",
		NULL
	};
	for ( int i = 0; envNames[ i ] != NULL; ++i ) {
		Common_AppendPreferredLanguagesFromLocaleList( Common_GetNonEmptyEnv( envNames[ i ] ), preferredLanguages );
	}
}

static idStr Common_FormatLanguageList( const idStrList &languages ) {
	idStr formatted;

	for ( int i = 0; i < languages.Num(); ++i ) {
		if ( i > 0 ) {
			formatted += ", ";
		}
		formatted += languages[ i ];
	}

	return formatted;
}

static bool Common_ResolveLanguageSelection( const idStr &requestedLanguage, const idStrList &availableLanguagePacks, bool preferOSLanguage, idStr &resolvedLanguage, bool &usedOSPreference ) {
	idStrList preferredLanguages;

	resolvedLanguage = requestedLanguage;
	usedOSPreference = false;

	if ( availableLanguagePacks.Num() == 0 ) {
		return false;
	}

	if ( preferOSLanguage || !Common_TrySelectLanguage( availableLanguagePacks, requestedLanguage.c_str(), resolvedLanguage ) ) {
		Common_GetOSPreferredLanguages( preferredLanguages );
		for ( int i = 0; i < preferredLanguages.Num(); ++i ) {
			if ( Common_TrySelectLanguage( availableLanguagePacks, preferredLanguages[ i ].c_str(), resolvedLanguage ) ) {
				usedOSPreference = true;
				return idStr::Icmp( resolvedLanguage.c_str(), requestedLanguage.c_str() ) != 0;
			}
		}
	}

	if ( Common_TrySelectLanguage( availableLanguagePacks, requestedLanguage.c_str(), resolvedLanguage ) ) {
		return idStr::Icmp( resolvedLanguage.c_str(), requestedLanguage.c_str() ) != 0;
	}

	if ( Common_TrySelectLanguage( availableLanguagePacks, "english", resolvedLanguage ) ) {
		return idStr::Icmp( resolvedLanguage.c_str(), requestedLanguage.c_str() ) != 0;
	}

	resolvedLanguage = availableLanguagePacks[ 0 ];
	return idStr::Icmp( resolvedLanguage.c_str(), requestedLanguage.c_str() ) != 0;
}

/*
===============
idCommonLocal::InitLanguageDict
===============
*/
void idCommonLocal::InitLanguageDict( bool applyStartupSysLang, bool allowAutoLanguageSelect ) {
	idStr fileName;
	languageDict.Clear();

	//D3XP: Instead of just loading a single lang file for each language
	//we are going to load all files that begin with the language name
	//similar to the way pak files work. So you can place english001.lang
	//to add new strings to the english language dictionary
	idFileList*	langFiles;
	langFiles =  fileSystem->ListFilesTree( "strings", ".lang", true );
	
	idStrList langList = langFiles->GetList();
	idStrList availableLanguagePacks;
	fileSystem->ListAvailableLanguagePacks( availableLanguagePacks );
	Common_AppendLanguagesWithStringTables( langList, availableLanguagePacks );
	const bool hasStartupSysLang = Common_HasStartupVariable( "sys_lang" );

	if ( applyStartupSysLang ) {
		// Let command-line sys_lang apply for the early startup dictionary load.
		StartupVariable( "sys_lang", false );
	}
	idStr langName = cvarSystem->GetCVarString( "sys_lang" );
	Common_NormalizeLanguageName( langName );
	const idStr requestedLangName = langName;
	idStr resolvedLangName;
	bool usedOSPreference = false;
	const bool languagePackChangedSelection = Common_ResolveLanguageSelection(
		requestedLangName,
		availableLanguagePacks,
		allowAutoLanguageSelect && !hasStartupSysLang,
		resolvedLangName,
		usedOSPreference );
	if ( languagePackChangedSelection ) {
		if ( usedOSPreference && allowAutoLanguageSelect && !hasStartupSysLang ) {
			common->Printf(
				"Selecting startup language '%s' from OS preference and available language packs (%s)\n",
				resolvedLangName.c_str(),
				Common_FormatLanguageList( availableLanguagePacks ).c_str() );
		} else if ( usedOSPreference ) {
			common->Printf(
				"Language pack for sys_lang '%s' is not available; using OS-preferred '%s' from available language packs (%s)\n",
				requestedLangName.c_str(),
				resolvedLangName.c_str(),
				Common_FormatLanguageList( availableLanguagePacks ).c_str() );
		} else {
			common->Printf(
				"Language pack for sys_lang '%s' is not available; using '%s' from available language packs (%s)\n",
				requestedLangName.c_str(),
				resolvedLangName.c_str(),
				Common_FormatLanguageList( availableLanguagePacks ).c_str() );
		}
		langName = resolvedLangName;
	}
	if ( idStr::Cmp( langName.c_str(), cvarSystem->GetCVarString( "sys_lang" ) ) != 0 ) {
		cvarSystem->SetCVarString( "sys_lang", langName.c_str() );
	}

	//Loop through the list and filter
	idStrList currentLangList = langList;
	FilterLangList(&currentLangList, langName);
	
	if ( currentLangList.Num() == 0 ) {
		// reset cvar to default and try to load again
		common->Printf( "No language files found for sys_lang '%s'; falling back to English\n", langName.c_str() );
		langName = "english";
		cvarSystem->SetCVarString( "sys_lang", langName.c_str() );
		currentLangList = langList;
		FilterLangList( &currentLangList, langName );
	}

	// Both the string tables and the byte-indexed fonts are 8-bit, so the
	// codepage has to be chosen before anything is loaded and it applies to the
	// English fallback tables as well - one dictionary cannot hold two
	// codepages. Polish selects Windows-1250; everything else Windows-1252.
	LangDict_SetActiveCodePage( LangDict_CodePageForLanguage( langName.c_str() ) );

	if ( idStr::Icmp( langName.c_str(), "english" ) != 0 ) {
		idStrList englishLangList = langList;
		FilterLangList( &englishLangList, "english" );
		for ( int i = 0; i < englishLangList.Num(); i++ ) {
			languageDict.Load( englishLangList[i], false );
		}
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
		src.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idCommonLocal::LocalizeMapData" ), fileName );
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
		src.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idCommonLocal::LocalizeGui" ), fileName );
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
	const bool wasFileLoadingAllowed = fileSystem->GetIsFileLoadingAllowed();
	fileSystem->SetIsFileLoadingAllowed( true );
	commonLocal.InitLanguageDict( false, false );
	fileSystem->SetIsFileLoadingAllowed( wasFileLoadingAllowed );
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
		src.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "LoadMapLocalizeData" ), fileName );
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
		src.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "LoadGuiParmExcludeList" ), fileName );
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
	cmdSystem->AddCommand( "reloadGameModule", Com_ReloadGameModule_f, CMD_FL_SYSTEM, "reloads the active game module while preserving the render window" );
	cmdSystem->AddCommand( "setMachineSpec", Com_SetMachineSpec_f, CMD_FL_SYSTEM, "detects system capabilities and sets com_machineSpec to appropriate value" );
	cmdSystem->AddCommand( "execMachineSpec", Com_ExecMachineSpec_f, CMD_FL_SYSTEM, "execs the appropriate config files and sets cvars based on com_machineSpec" );
	cmdSystem->AddCommand( "applyPerformancePreset", Com_ApplyPerformancePreset_f, CMD_FL_SYSTEM, "applies the selected openQ4 performance preset", idCmdSystem::ArgCompletion_String<com_performancePresetArgs> );
	cmdSystem->AddCommand( "autoDetectPerformancePreset", Com_AutoDetectPerformancePreset_f, CMD_FL_SYSTEM, "detects and applies a conservative openQ4 performance preset" );
	cmdSystem->AddCommand( "performancePresetSelfTest", Com_PerformancePresetSelfTest_f, CMD_FL_SYSTEM, "validates openQ4 performance preset commands and cvar mappings" );
	cmdSystem->AddCommand( "jobsStats", Com_JobsStats_f, CMD_FL_SYSTEM, "prints bounded job-service scheduling and timing counters" );
	cmdSystem->AddCommand( "jobsSelfTest", Com_JobsSelfTest_f, CMD_FL_SYSTEM, "validates job execution, dependencies, deterministic fallback, and metrics" );
	cmdSystem->AddCommand( "netIPv4SelfTest", Com_NetIPv4SelfTest_f, CMD_FL_SYSTEM, "validates IPv4 parsing and UDP loopback transport" );
	cmdSystem->AddCommand( "netIPv6SelfTest", Com_NetIPv6SelfTest_f, CMD_FL_SYSTEM, "validates IPv6 parsing and UDP loopback transport" );

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

	// imagetools is a static library, so the executable and the renderer module
	// each hold a private copy of the texture-compression capability block that
	// gates precompressed-DDS selection. The renderer publishes into its own
	// copy during context bring-up; mirror it into ours or engine-side image
	// loads (material types, the session, GUI widgets) silently reject every DDS
	// replacement and CPU-decode compressed data the GPU could take directly.
	if ( renderSystem->IsOpenGLRunning() ) {
		const glconfig_t &rendererConfig = renderSystem->GetGLConfig();
		imageToolsCompressionCaps_t compressionCaps;
		compressionCaps.textureCompressionAvailable = rendererConfig.textureCompressionAvailable;
		compressionCaps.bptcTextureCompressionAvailable = rendererConfig.bptcTextureCompressionAvailable;
		compressionCaps.etc2TextureCompressionAvailable = rendererConfig.etc2TextureCompressionAvailable;
		ImageTools_SetCompressionCaps( compressionCaps );
	}

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

	float viewportWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	float viewportHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		viewportWidth = static_cast<float>( engineWindowState.vidWidth );
		viewportHeight = static_cast<float>( engineWindowState.vidHeight );
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
	renderSystem->FlushGui();
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
		openQ4_BeginPresentationFrame();
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

#ifndef ID_DEDICATED
		// Drive Arena presentation transitions from the common frame path: while
		// its listen server is active, idSessionLocal::Frame is intentionally not
		// called by the ordinary multiplayer branch below.
		arenaCampaign.Frame();
#endif

	if ( idAsyncNetwork::IsActive() ) {
		if ( idAsyncNetwork::serverDedicated.GetInteger() != 1 ) {
			// Keep netplay flow the same as stock, but ensure audio mixes each frame.
			soundSystem->Render();
			session->GuiFrameEvents();
			openQ4_RecordMultiplayerFramePacing( frameStartMsec );
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
	openQ4_BeginPresentationFrame();
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

void openQ4_GetAsyncTimingStats( openq4AsyncTimingStats_t &stats, int maxSamples ) {
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

// The async thread must not search the global CVar registry while a game DLL is
// registering its static CVars. Module swaps grow that registry on the main
// thread and can invalidate an in-flight FindInternal traversal. Publish the
// only bit of module state needed by the timer after game->Init() completes.
static std::atomic<bool> openQ4_singleplayerGameModuleReady( false );

static bool openQ4_ShouldUseSmoothSingleplayerSlowTime( float effectiveTimeScale ) {
	if ( !openQ4_singleplayerGameModuleReady.load( std::memory_order_acquire ) ||
		 idAsyncNetwork::IsActive() ) {
		return false;
	}

	return effectiveTimeScale < 0.999f;
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

	// Accelerated time produces more tics. Slow time keeps the normal tic cadence
	// and lets the single-player game advance simulation by a smaller delta.
	float timescale = com_timescale.GetFloat() * openQ4_gameTimeScale.load( std::memory_order_acquire );
	const bool smoothSlowTime = openQ4_ShouldUseSmoothSingleplayerSlowTime( timescale );
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

// Mirror of the multiplayer rows in the GameLibs descriptor table
// (src/mpgame/mp/GameTypes.cpp), including whether each row is selectable. The
// engine has to choose a game module before any module is loaded, so it cannot
// ask the game for its own table; tools/tests/game_type_module_selection.py
// cross-checks the two tables.
//
// This is an allowlist on purpose. "anything that is not singleplayer is
// multiplayer" meant a typo, a stale config or a mod-specific value booted
// game_mp and then forced a full renderer-tearing module swap on the first
// New Game.
struct openQ4GameTypeRoute_t {
	const char *name;
	bool selectable;
};

static const openQ4GameTypeRoute_t openQ4_multiplayerGameTypes[] = {
	{ "DM", true },
	{ "Tourney", true },
	{ "Team DM", true },
	{ "CTF", true },
	{ "One Flag CTF", true },
	{ "Arena CTF", true },
	{ "Arena One Flag CTF", true },
	{ "DeadZone", true },
	{ "Duel", true },
	{ "Clan Arena", true },
	{ "Freeze Tag", true },
	{ "Red Rover", true },
	// Reserved multiplayer wire tokens still route to game_mp so its validated
	// descriptor table remains append-only. They are normalized to DM before
	// game_mp registers its public si_gameType value list.
	{ "Overload", false },
	{ "Harvester", false },
	{ "Domination", false },
	{ "Attack Defend", false },
	{ NULL, false }
};

static const char *openQ4_CanonicalMultiplayerGameType( const char *gameType, const bool selectableOnly ) {
	if ( gameType == NULL || gameType[0] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; openQ4_multiplayerGameTypes[i].name != NULL; i++ ) {
		const openQ4GameTypeRoute_t &route = openQ4_multiplayerGameTypes[i];
		if ( idStr::Icmp( gameType, route.name ) == 0 && ( !selectableOnly || route.selectable ) ) {
			return route.name;
		}
	}
	return NULL;
}

#ifndef ID_DEDICATED
static bool openQ4_IsMultiplayerGameType( const char *gameType ) {
	return openQ4_CanonicalMultiplayerGameType( gameType, false ) != NULL;
}
#endif

static void openQ4_NormalizeGameTypeForModule( const char *moduleName ) {
	if ( idStr::Icmp( moduleName, "game_mp" ) != 0 ) {
		return;
	}

	const char *currentGameType = cvarSystem->GetCVarString( "si_gameType" );
	const char *canonicalGameType = openQ4_CanonicalMultiplayerGameType( currentGameType, true );
	if ( canonicalGameType != NULL ) {
		if ( idStr::Cmp( currentGameType, canonicalGameType ) != 0 ) {
			// CVar values use a case-insensitive early-out, so a direct "dm" ->
			// "DM" write is ignored. Cross a different valid value to make the
			// canonical spelling observable before declarations are rebuilt.
			cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
			cvarSystem->SetCVarString( "si_gameType", canonicalGameType );
		}
		return;
	}

	// game_mp cannot initialize against an unsupported or single-player mode.
	// This covers joins, listen servers, and dedicated startup alike.
	common->Printf( "Selected game_mp with unsupported si_gameType '%s'; using DM for multiplayer module initialization.\n", currentGameType );
	cvarSystem->SetCVarString( "si_gameType", "DM" );
}

static bool openQ4_IsValidGameModuleName( const char *moduleName ) {
	return moduleName
		&& ( idStr::Icmp( moduleName, "game_sp" ) == 0 || idStr::Icmp( moduleName, "game_mp" ) == 0 );
}

static const char *openQ4_SelectGameModuleBaseName( void ) {
	const char *nextModule = cvarSystem->GetCVarString( "com_nextGameModule" );
	if ( openQ4_IsValidGameModuleName( nextModule ) ) {
		return idStr::Icmp( nextModule, "game_mp" ) == 0 ? "game_mp" : "game_sp";
	}

	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
#ifdef ID_DEDICATED
	// A dedicated server has no single-player mode at all (StartNewGame refuses
	// there), so keep the historical "anything but singleplayer" reading rather
	// than sending an unrecognised gametype to the single-player module.
	return ( gameType != NULL && idStr::Icmp( gameType, "singleplayer" ) == 0 ) ? "game_sp" : "game_mp";
#else
	return openQ4_IsMultiplayerGameType( gameType ) ? "game_mp" : "game_sp";
#endif
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
static void openQ4_BuildGameModuleBinaryNameForArch( const char *moduleName, const char *archTag, char outName[ MAX_OSPATH ] ) {
	const char *variant = ( moduleName && idStr::Icmp( moduleName, "game_mp" ) == 0 ) ? "mp" : "sp";
	idStr::snPrintf( outName, MAX_OSPATH, "game-%s_%s", variant, archTag );
}

static void openQ4_BuildGameModuleBinaryName( const char *moduleName, char outName[ MAX_OSPATH ] ) {
	openQ4_BuildGameModuleBinaryNameForArch( moduleName, OPENQ4_MODULE_ARCH_TAG, outName );
}

static void openQ4_DisableBSEWithWarning( const char *reason, bool showDialog = true ) {
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
		::MessageBoxA( NULL, message, "openQ4 Warning", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL );
	}
#endif

	::declEffectEdit = NULL;
	::bseAllocDeclEffect = openQ4_AllocIntegratedBSEDeclEffect;
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
	::bseAllocDeclEffect = openQ4_AllocIntegratedBSEDeclEffect;

#if !defined( ID_DEDICATED )
	common->DPrintf( "Attaching integrated BSE.\n" );
	::bse = openQ4_GetIntegratedBSEManager();
	::declEffectEdit = openQ4_GetIntegratedBSEDeclEffectEdit();
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

static volatile sig_atomic_t com_lastGameModuleLoadPhase = GAME_MODULE_PHASE_IDLE;

static gameModuleLoadPhase_t Com_NormalizeGameModuleLoadPhase( sig_atomic_t phase ) {
	if ( phase < GAME_MODULE_PHASE_IDLE || phase >= GAME_MODULE_PHASE_COUNT ) {
		return GAME_MODULE_PHASE_IDLE;
	}
	return static_cast<gameModuleLoadPhase_t>( phase );
}

const char *Com_GameModuleLoadPhaseName( gameModuleLoadPhase_t phase ) {
	switch ( phase ) {
	case GAME_MODULE_PHASE_IDLE:
		return "idle";
	case GAME_MODULE_PHASE_LOCATE:
		return "locating game module binary";
	case GAME_MODULE_PHASE_BINARY_LOAD:
		return "loading game module binary (static initializers)";
	case GAME_MODULE_PHASE_RESOLVE_ENTRY_POINT:
		return "resolving GetGameAPI";
	case GAME_MODULE_PHASE_CALL_GET_GAME_API:
		return "calling GetGameAPI";
	case GAME_MODULE_PHASE_VERIFY_API_VERSION:
		return "verifying game API version";
	case GAME_MODULE_PHASE_GAME_INIT:
		return "game->Init()";
	case GAME_MODULE_PHASE_READY:
		return "game module ready";
	case GAME_MODULE_PHASE_GAME_SHUTDOWN:
		return "game->Shutdown()";
	case GAME_MODULE_PHASE_GAME_FINALIZE:
		return "game->ShutdownAfterDecls()";
	case GAME_MODULE_PHASE_BINARY_UNLOAD:
		return "unloading game module binary";
	default:
		return "unknown game module phase";
	}
}

void Com_SetGameModuleLoadPhase( gameModuleLoadPhase_t phase ) {
	com_lastGameModuleLoadPhase = static_cast<sig_atomic_t>( Com_NormalizeGameModuleLoadPhase( phase ) );
}

void Com_RecordGameModuleLoadPhase( gameModuleLoadPhase_t phase ) {
	Com_SetGameModuleLoadPhase( phase );
	if ( common != NULL ) {
		common->Printf( "game module phase: %s\n", Com_GameModuleLoadPhaseName( phase ) );
	}
}

gameModuleLoadPhase_t Com_GetGameModuleLoadPhase( void ) {
	return Com_NormalizeGameModuleLoadPhase( com_lastGameModuleLoadPhase );
}

const char *Com_GameModuleLoadPhaseSignalName( void ) {
	return Com_GameModuleLoadPhaseName( Com_NormalizeGameModuleLoadPhase( com_lastGameModuleLoadPhase ) );
}

/*
=================
idCommonLocal::LoadGameDLL
=================
*/
void idCommonLocal::LoadGameDLL( void ) {
	openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );
	SetGameTimeScale( 1.0f );
	gameShutdownCalled = false;
	gameShutdownAfterDeclsCalled = false;
	const char *gameModuleBaseName = openQ4_SelectGameModuleBaseName();
	openQ4_NormalizeGameTypeForModule( gameModuleBaseName );

#ifdef __DOOM_DLL__
	char			dllPath[ MAX_OSPATH ];
	char			preferredGameModuleBinary[ MAX_OSPATH ];
#if defined( MACOS_X ) || defined( __APPLE__ )
	char			universalGameModuleBinary[ MAX_OSPATH ];
#endif
	const char *	selectedModuleBinary;

	gameImport_t	gameImport;
	gameExport_t	gameExport;
	GetGameAPI_t	GetGameAPI;

	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_LOCATE );

	openQ4_BuildGameModuleBinaryName( gameModuleBaseName, preferredGameModuleBinary );
	selectedModuleBinary = preferredGameModuleBinary;
	fileSystem->FindDLL( selectedModuleBinary, dllPath, true );

#if defined( MACOS_X ) || defined( __APPLE__ )
	// Thin packages retain architecture-specific module names. A universal2
	// package instead carries a single two-slice module per game variant, so
	// both executable slices must converge on the same trusted module name.
	if ( !dllPath[ 0 ] ) {
		openQ4_BuildGameModuleBinaryNameForArch( gameModuleBaseName, "universal2", universalGameModuleBinary );
		fileSystem->FindDLL( universalGameModuleBinary, dllPath, true );
		if ( dllPath[ 0 ] ) {
			selectedModuleBinary = universalGameModuleBinary;
		}
	}
#endif

	if ( !dllPath[ 0 ] ) {
		common->FatalError(
			"couldn't find game dynamic library '%s'",
			preferredGameModuleBinary
		);
		return;
	}
	common->Printf(
		"Selected game module: logical='%s' binary='%s' path='%s'\n",
		gameModuleBaseName,
		selectedModuleBinary,
		dllPath );
	common->DPrintf( "Loading game DLL: '%s'\n", dllPath );
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_BINARY_LOAD );
	gameDLL = sys->DLL_Load( dllPath );
	if ( !gameDLL ) {
		common->Printf(
			"Game module load failed: logical='%s' binary='%s' path='%s'; inspect the preceding platform loader error.\n",
			gameModuleBaseName,
			selectedModuleBinary,
			dllPath );
		common->FatalError( "couldn't load game dynamic library '%s'", selectedModuleBinary );
		return;
	}

	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_RESOLVE_ENTRY_POINT );
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

	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_CALL_GET_GAME_API );
	const gameExport_t *gameExportPtr = GetGameAPI( &gameImport );
	if ( gameExportPtr == NULL ) {
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError(
			"game module '%s' returned no export table from GetGameAPI",
			selectedModuleBinary );
		return;
	}
	gameExport							= *gameExportPtr;

	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_VERIFY_API_VERSION );
	if ( gameExport.version != GAME_API_VERSION ) {
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError( "wrong game DLL API version" );
		return;
	}

	// the game module links its own idlib archive; fold its allocation
	// counters into the engine's total. Optional symbol, so a module built
	// before this existed is simply left out rather than rejected.
	Mem_RegisterModuleStats( (memModuleStats_t) Sys_DLL_GetProcAddress( gameDLL, MEM_MODULE_STATS_ENTRY_POINT ) );

	game								= gameExport.game;
	gameEdit							= gameExport.gameEdit;
	com_activeGameModule.SetString( gameModuleBaseName );
	com_nextGameModule.SetString( "" );

#endif

	// initialize the game object
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_GAME_INIT );
	if ( game != NULL ) {
		game->Init();
	}
	openQ4_singleplayerGameModuleReady.store(
		game != NULL && idStr::Icmp( gameModuleBaseName, "game_sp" ) == 0,
		std::memory_order_release );
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_READY );
}

/*
=================
idCommonLocal::UnloadGameDLL
=================
*/
void idCommonLocal::UnloadGameDLL( void ) {
#ifdef __DOOM_DLL__

	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_BINARY_UNLOAD );
	if ( gameDLL ) {
		// drop the counters before the code they live in goes away
		Mem_UnregisterModuleStats( (memModuleStats_t) Sys_DLL_GetProcAddress( gameDLL, MEM_MODULE_STATS_ENTRY_POINT ) );
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
	}
	game = NULL;
	gameEdit = NULL;
	com_activeGameModule.SetString( "" );

#endif
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_IDLE );
}

/*
=================
idCommonLocal::IsInitialized
=================
*/
bool idCommonLocal::IsInitialized( void ) const {
	return com_fullyInitialized;
}

void idCommonLocal::ApplyAutomaticPlatformProfile( void ) {
	if ( idStr::Icmp( com_platformProfile.GetString(), "default" ) != 0 ) {
		return;
	}

	if ( Common_IsEnvFlagTrue( Common_GetNonEmptyEnv( "OPENQ4_DISABLE_STEAMDECK_AUTODETECT" ) ) ||
		 Common_IsEnvFlagTrue( Common_GetNonEmptyEnv( "OPENQ4_NO_STEAMDECK_AUTODETECT" ) ) ) {
		Printf( "Steam Deck platform profile auto-detection disabled by environment.\n" );
		return;
	}

	if ( !Common_HasSteamDeckHostSignal() ) {
		return;
	}

	Printf( "Auto-selecting steamdeck platform profile from host environment.\n" );
	com_platformProfile.SetString( "steamdeck" );
}

static double Common_AdjustMachineSpecGHz( double ghz, cpuid_t cpu ) {
	// Retail Quake 4 applies a CPU-vendor adjustment before classifying the
	// quality tier. openQ4 does not expose a separate AMD64 flag, so keep the
	// conservative legacy AMD adjustment without changing the public CPU enum.
	if ( ( cpu & CPUID_AMD ) != 0 ) {
		return ghz * 1.15f;
	}
	return ghz;
}

static int Common_DetectMachineSpec( double ghz, int vidRam, int sysRam, bool oldCard ) {
#if defined( __aarch64__ ) || defined( __arm64__ ) || defined( _M_ARM64 )
	// No arm64 host reports a usable CPU frequency: Apple Silicon has no
	// hw.cpufrequency, aarch64 Linux has no "cpu MHz" line in /proc/cpuinfo,
	// and Windows on ARM has no "~MHz" registry value. Every arm64 desktop
	// part comfortably exceeds the 2005-era retail CPU thresholds, so
	// classify by memory alone rather than dropping every arm64 machine to
	// the lowest tier. Unified or shared memory also means a missing VRAM
	// probe should fall back to a share of system memory.
	if ( ghz <= 0.0 && !oldCard ) {
		int effectiveVidRam = vidRam;
		const bool guessedVidRam = ( effectiveVidRam <= 0 );
		if ( guessedVidRam ) {
			effectiveVidRam = sysRam / 2;
		}

		// Thresholds for the upper tiers. A real VRAM probe is trusted and uses
		// the stock numbers. A *guessed* one is not: sysRam / 2 says nothing
		// about GPU capability, so a 8 GB single-board computer produces the
		// same number as a workstation.
		//
		// This matters because tier 0 is the only tier that downsizes textures
		// (image_downSize, image_ignoreHighQuality, image_downSizeSpecular and
		// image_downSizeBump - see Com_ExecMachineSpec_f), and openQ4 currently
		// uploads ordinary textures uncompressed, so promoting out of tier 0
		// multiplies the texture budget. Both arm64 hosts that have reported
		// memory failures were 8 GB boards killed by the OOM reaper during load
		// (issues #76 on a Pi 5 and #78 on aarch64 Linux). So when we are
		// guessing, require clearly abundant system memory before leaving the
		// downsizing tier.
		//
		// Apple Silicon keeps the stock thresholds regardless: it has genuine
		// unified memory on a GPU class known to run the game well, and no arm64
		// Mac has reported a memory-pressure failure.
#if defined( MACOS_X )
		const int ultraSysRam = 1000;
		const int highSysRam = 1000;
		const int mediumSysRam = 750;
#else
		const int ultraSysRam = guessedVidRam ? 24000 : 1000;
		const int highSysRam = guessedVidRam ? 16000 : 1000;
		const int mediumSysRam = guessedVidRam ? 12000 : 750;
#endif

		if ( effectiveVidRam >= 300 && sysRam >= ultraSysRam ) {
			return 3;
		}
		if ( effectiveVidRam >= 160 && sysRam >= highSysRam ) {
			return 2;
		}
		if ( effectiveVidRam >= 160 && sysRam >= mediumSysRam ) {
			return 1;
		}
		return 0;
	}
#endif
	if ( ghz >= 2.89f && vidRam >= 300 && sysRam >= 1000 && !oldCard ) {
		return 3;
	}
	if ( ghz >= 2.89f && vidRam >= 160 && sysRam >= 1000 && !oldCard ) {
		return 2;
	}
	if ( ghz >= 2.29f && vidRam >= 160 && sysRam >= 750 && !oldCard ) {
		return 1;
	}
	return 0;
}

static const char *Common_MachineSpecQualificationMessage( int machineSpec ) {
	switch ( machineSpec ) {
		case 3:
			return "This system qualifies for Ultra quality!\n";
		case 2:
			return "This system qualifies for High quality!\n";
		case 1:
			return "This system qualifies for Medium quality.\n";
		default:
			return "This system qualifies for Low quality.\n";
	}
}

/*
=================
idCommonLocal::SetMachineSpec
=================
*/
void idCommonLocal::SetMachineSpec( void ) {
	// Sys_ClockTicksPerSecond is the timer rate (QPC/monotonic), not CPU speed
	cpuid_t cpu = Sys_GetProcessorId();
	const double cpuHz = Sys_GetApproximateProcessorFrequencyHz();
	const double ghz = Common_AdjustMachineSpecGHz( cpuHz * 0.000000001f, cpu );
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

	const int machineSpec = Common_DetectMachineSpec( ghz, vidRam, sysRam, oldCard );
	Printf( "%s", Common_MachineSpecQualificationMessage( machineSpec ) );
	com_machineSpec.SetInteger( machineSpec );
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
		ApplyAutomaticPlatformProfile();

		if ( !idAsyncNetwork::serverDedicated.GetInteger() && Sys_AlreadyRunning() ) {
			Sys_Quit();
		}

		// initialize processor specific SIMD implementation
		InitSIMD();

		// The job service is engine-owned and renderer-independent, so the same
		// lifecycle is available to clients, tools, and headless dedicated builds.
		// Disabling jobs never drops work: it selects the deterministic inline path.
		idJobSystemConfig jobConfig;
		jobConfig.synchronous = !jobs_enable.GetBool() || jobs_deterministic.GetBool();
		jobConfig.workerThreads = jobConfig.synchronous ? 0 :
			idJobSystem::ResolveWorkerThreadCount( jobs_numThreads.GetInteger() );
		jobConfig.maxQueuedLists = static_cast<std::size_t>( jobs_queueCapacity.GetInteger() );
		if ( !jobSystem.Initialize( jobConfig ) ) {
			FatalError( "Failed to initialize the portable job system" );
		}
		Printf( "Job system initialized: mode=%s workers=%u queueCapacity=%d (jobs_enable=%d deterministic=%d)\n",
			jobSystem.IsSynchronous() ? "synchronous" : "threaded",
			jobSystem.GetWorkerThreadCount(),
			jobs_queueCapacity.GetInteger(),
			jobs_enable.GetBool() ? 1 : 0,
			jobs_deterministic.GetBool() ? 1 : 0 );

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
				const char *lineText = com_consoleLines[ i ].Args( 0, com_consoleLines[ i ].Argc() - 1 );
				if ( cvarSystem->CommandContainsPrivateCVar( lineText ) ) {
					idCmdArgs::ClearArgsScratch();
					Printf( "  %d: <private command redacted>\n", i );
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

	// Stop every engine job before any subsystem it may reference is torn down.
	// Running jobs receive cancellation and are joined; queued jobs never start.
	jobSystem.Shutdown( idJobShutdownMode::CANCEL_PENDING );
	const idJobSystemMetrics shutdownJobMetrics = jobSystem.GetMetrics();
	if ( jobSystem.IsInitialized() || shutdownJobMetrics.queuedLists != 0 || shutdownJobMetrics.runningJobs != 0 ) {
		Warning( "jobsShutdown FAILED v1 initialized=%d queued=%llu running=%llu",
			jobSystem.IsInitialized() ? 1 : 0,
			static_cast<unsigned long long>( shutdownJobMetrics.queuedLists ),
			static_cast<unsigned long long>( shutdownJobMetrics.runningJobs ) );
	} else {
		Printf( "jobsShutdown PASS v1 initialized=0 queued=0 running=0\n" );
	}

	idAsyncNetwork::server.Kill();
	idAsyncNetwork::client.Shutdown();

	// game specific shut down
	ShutdownGame( false );

	// Release the engine/tool-side render-geometry copy after renderer teardown.
	// This is a no-op on static-renderer builds whose shared copy already stopped.
	R_ShutdownTriSurfData();

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
	// A game-module reload rebuilds declarations before it replays archived cfgs.
	// Canonicalize the pending multiplayer mode at the start of that rebuild;
	// doing this only in LoadGameDLL is too late for the decl checksum.
	const idStr pendingGameModule = openQ4_SelectGameModuleBaseName();
	openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );

	// initialize the file system
	fileSystem->Init();

	// attach the integrated BSE manager before decl initialization so DECL_EFFECT
	// allocation is available when effect declarations are parsed.
	AttachBSE();

	// resolve the active renderer path (r_renderApi) before decl
	// initialization, so every decl object the renderer allocates through
	// AllocMaterialDecl carries the vtable of the renderer instance that
	// will actually draw it (Phase B8 module seam)
	R_RendererModule_BootEarly();

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

	// The render-geometry library is linked independently into module-only
	// renderers and the engine. Initialize the engine copy for dmap and the
	// other in-process tools after the renderer has installed its allocation
	// hooks, but before startup scripts can invoke a tool command. On static
	// renderer builds the shared copy is already initialized, so this is a no-op.
	R_InitTriSurfData();

	// initialize string database right off so we can use it for loading messages
	const idStr startupLanguageBeforeAutoSelect = cvarSystem->GetCVarString( "sys_lang" );
	const bool allowStartupLanguageAutoSelect = sysDetect && !Common_HasStartupVariable( "sys_lang" );
	InitLanguageDict( true, allowStartupLanguageAutoSelect );
	const bool startupLanguageAutoSelected =
		allowStartupLanguageAutoSelect &&
		idStr::Icmp( startupLanguageBeforeAutoSelect.c_str(), cvarSystem->GetCVarString( "sys_lang" ) ) != 0;

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

	// run cfg execution
	cmdSystem->ExecuteCommandBuffer();

	// re-override anything from the config files with command line args
	StartupVariable( NULL, false );

	// Reload the language dictionary after cfg/autoexec and command-line cvars
	// have settled, so +set sys_lang wins over archived startup scripts.
	const bool wasFileLoadingAllowed = fileSystem->GetIsFileLoadingAllowed();
	fileSystem->SetIsFileLoadingAllowed( true );
	InitLanguageDict( false, false );
	fileSystem->SetIsFileLoadingAllowed( wasFileLoadingAllowed );

	bool repairedUnsetMachineSpec = false;
	if ( com_machineSpec.GetInteger() < 0 ) {
		Printf( "com_machineSpec is unset; detecting system quality tier.\n" );
		SetMachineSpec();
		Com_ExecMachineSpec_f( args );
		repairedUnsetMachineSpec = true;
	}

	Common_MigrateLegacyConsoleAllowCVar();

	// Unconditional, because the "This system qualifies for ..." banner above
	// only prints on the run that detects the tier. Every later run -- which is
	// every run a reporter actually sends -- said nothing about which quality
	// tier, VRAM figure, preset or ambient floor produced the image they are
	// asking about (issue #73).
	Printf(
		"Quality profile: com_machineSpec=%d com_videoRam=%d com_performancePreset='%s' r_forceAmbient=%.3f\n",
		com_machineSpec.GetInteger(),
		com_videoRam.GetInteger(),
		com_performancePreset.GetString(),
		cvarSystem->GetCVarFloat( "r_forceAmbient" ) );

	// if any archived cvars are modified after this, we will trigger a writing of the config file
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );
	if ( repairedUnsetMachineSpec || startupLanguageAutoSelected ) {
		cvarSystem->SetModifiedFlags( CVAR_ARCHIVE );
	}
	Common_MigrateLinuxLegacyLowVRamTexturePreset();
	Common_MigrateLegacyBorderlessWindowDefault();
	Common_MigrateLegacyNetworkRateCaps();

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
		openQ4_DisableBSEWithWarning( "BSE initialization failed" );
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
	// Stop advertising a ready single-player module before any shutdown work can
	// race the async thread or mutate game-owned state.
	openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );
	SetGameTimeScale( 1.0f );

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

	// The game owns GUI list handlers and may clear GUI, HUD, renderer, sound and
	// network state during Shutdown.  Run it while those engine services are
	// still alive, but keep the DLL loaded until game-owned decl instances with
	// module-local vtables have been released below.
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_GAME_SHUTDOWN );
	if ( game != NULL && !gameShutdownCalled ) {
		// Set this before entering module code so a fatal error raised by game
		// teardown cannot recursively invoke the same half of the lifecycle.
		gameShutdownCalled = true;
		game->Shutdown();
	}

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

	// shut down the renderSystem; NULL on module-only builds when a fatal
	// error shuts down before the renderer module published its interfaces
	if ( renderSystem ) {
		renderSystem->Shutdown();
	}

	// shutdown the decl manager while the game DLL is still loaded, because
	// game-owned decl types (e.g. entity/fx decls) can have module-local vtables.
	declManager->Shutdown();

	// Model decl destructors release idAnim references into the module animation
	// manager, and all module decl destructors depend on the module idLib.  Those
	// support resources therefore have a separate late teardown phase.
	Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_GAME_FINALIZE );
	if ( game != NULL && !gameShutdownAfterDeclsCalled ) {
		// As above, publish the one-shot state before crossing the module boundary.
		gameShutdownAfterDeclsCalled = true;
		game->ShutdownAfterDecls();
	}

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
	const int savedLogFile = com_logFile.GetInteger();
	CloseLogFile();
	if ( reloading ) {
		com_logFile.SetInteger( savedLogFile );
	}

	// shut down the file system
	fileSystem->Shutdown( reloading );
}
