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




#include "Session_local.h"
#include "ArenaCampaign.h"
#include "BuildVersion.h"
#include "../sys/NetworkEndpoint.h"
#if defined( __has_include )
#if __has_include( "openq4_savegame_compat_generated.h" )
#include "openq4_savegame_compat_generated.h"
#endif
#endif
#ifndef OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH
#define OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH ""
#endif
#ifndef OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT
#define OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT -1
#endif
#define private public
#define protected public
#include "Game_local.h"
#undef protected
#undef private
#include "../imagetools/ImageTools.h"
#include "../ui/Window.h"
#include "../ui/RetainedUI.h"

idCVar	idSessionLocal::com_showAngles( "com_showAngles", "0", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar	idSessionLocal::com_minTics( "com_minTics", "1", CVAR_SYSTEM, "" );
idCVar	idSessionLocal::com_showTics( "com_showTics", "0", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar	idSessionLocal::com_fixedTic( "com_fixedTic", "0", CVAR_SYSTEM | CVAR_INTEGER, "", 0, 10 );
idCVar	idSessionLocal::com_showDemo( "com_showDemo", "0", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar	idSessionLocal::com_skipGameDraw( "com_skipGameDraw", "0", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar	idSessionLocal::com_aviDemoSamples( "com_aviDemoSamples", "16", CVAR_SYSTEM, "" );
idCVar	idSessionLocal::com_aviDemoWidth( "com_aviDemoWidth", "256", CVAR_SYSTEM, "" );
idCVar	idSessionLocal::com_aviDemoHeight( "com_aviDemoHeight", "256", CVAR_SYSTEM, "" );
idCVar	idSessionLocal::com_aviDemoTics( "com_aviDemoTics", "2", CVAR_SYSTEM | CVAR_INTEGER, "", 1, 60 );
idCVar	idSessionLocal::com_wipeSeconds( "com_wipeSeconds", "1", CVAR_SYSTEM, "" );
idCVar	idSessionLocal::com_guid( "com_guid", "", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_ROM, "" );
idCVar	idSessionLocal::com_lastQuicksave( "com_lastQuicksave", "Quicksave0", CVAR_SYSTEM | CVAR_ARCHIVE, "last quicksave slot" );
idCVar	com_loadingContinueAutoAdvance( "com_loadingContinueAutoAdvance", "0", CVAR_SYSTEM | CVAR_INTEGER, "auto-accept the single-player loading-screen continue gate after N msec (testing), 0 = off", 0, 60000, idCmdSystem::ArgCompletion_Integer<0,60000> );
idCVar	com_skipLoadingContinue( "com_skipLoadingContinue", "0", CVAR_SYSTEM | CVAR_BOOL, "skip the single-player loading-screen continue gate (testing)" );
idCVar	com_minLoadingGuiMsec( "com_minLoadingGuiMsec", "250", CVAR_SYSTEM | CVAR_INTEGER, "minimum time to show the loading GUI before blocking map work starts", 0, 2000, idCmdSystem::ArgCompletion_Integer<0,2000> );
idCVar	com_showLevelLoadTimes( "com_showLevelLoadTimes", "1", CVAR_SYSTEM | CVAR_BOOL, "print detailed phase timings for level loads" );
idCVar	com_skipLogoVideos( "com_skipLogoVideos", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "skip startup logo videos and go straight to the main menu" );
idCVar	com_showLevelshotBounds( "com_showLevelshotBounds", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "draw a centered 4:3 frame guide for levelshot composition" );
idCVar	s_muteUnfocused( "s_muteUnfocused", "1", CVAR_ARCHIVE | CVAR_BOOL, "mute all audio when the application is out of focus" );

#if defined( USE_SDL3 )
bool Sys_SDL_IsGameWindowFocused( void );
#elif defined( _WIN32 )
bool Sys_IsGameWindowFocused( void );
#endif

idSessionLocal		sessLocal;
idSession			*session = &sessLocal;

// Implemented by FileSystem.cpp as an engine-only coordinator hook; this is
// intentionally not part of the public module-facing idFileSystem ABI.
void FS_ReleaseLevelLoadCache( void );

static float Session_UpdateMetricAverage( float currentAverage, float sample, int sampleCount ) {
	const int averagingWindow = idMath::ClampInt( 1, 120, sampleCount );
	if ( averagingWindow <= 1 ) {
		return sample;
	}
	return currentAverage + ( sample - currentAverage ) / static_cast<float>( averagingWindow );
}

static int Session_FindPresentationCap( void ) {
	if ( cvarSystem == NULL ) {
		return 0;
	}

	// Must match the cap Common_ThrottlePresentationFrame actually paces to, not raw
	// com_maxfps: when the two disagree the pacifier admits frames faster than the
	// throttle will release them and each one sleeps away the difference.
	return openQ4_GetRequestedPresentationCap();
}

// A blocking-load progress animation gains nothing from redrawing at the gameplay
// presentation cap, and every redraw is wall clock stolen from the load itself.
// Never redraw the loading screen faster than this, however high com_maxfps goes.
static const float SESSION_MAX_BLOCKING_LOAD_REDRAW_HZ = 60.0f;

// Back the pacifier interval off to at least this multiple of the last redraw's
// measured cost, bounding loading-screen presentation at 1/(1+ratio) of load wall
// clock on any present path.
static const float SESSION_PACIFIER_DRAW_BUDGET_RATIO = 4.0f;

static float Session_GetBlockingLoadFrameIntervalMsec( void ) {
	// Honour a *lower* gameplay cap - the user asked for fewer frames - but never let
	// a high cap drive the loading screen faster than the ceiling above. Tying this
	// directly to com_maxfps made load wall clock scale as W / (1 - R/P), which
	// diverges as the cap rises and stops converging entirely once one redraw costs
	// more than the interval it is throttled by.
	const float ceilingMsec = 1000.0f / SESSION_MAX_BLOCKING_LOAD_REDRAW_HZ;
	const int presentationCap = Session_FindPresentationCap();
	if ( presentationCap > 0 ) {
		return Max( ceilingMsec, 1000.0f / static_cast<float>( presentationCap ) );
	}

	// Uncapped blocking load hooks can fire extremely often during asset I/O.
	// Keep the legacy one-tic pacing as the conservative fallback in that mode.
	return Max( ceilingMsec, common->GetUserCmdMsecFloat() );
}

static void Session_BeginBlockingLoadPresentationFrame( void ) {
	if ( Session_FindPresentationCap() > 0 ) {
		openQ4_BeginPresentationFrame();
	} else {
		com_frameRealTime = Sys_Milliseconds();
	}
}

static const char *Session_GetFramePacingBoundName( openq4FramePacingBound_t boundMode ) {
	switch ( boundMode ) {
		case OPENQ4_FRAME_BOUND_SIMULATION:
			return "simulation";
		case OPENQ4_FRAME_BOUND_VSYNC:
			return "vsync";
		case OPENQ4_FRAME_BOUND_PRESENTATION_CAP:
			return "presentation-cap";
		case OPENQ4_FRAME_BOUND_UNCAPPED:
			return "uncapped";
		default:
			return "collecting";
	}
}

static int Session_GetFramePacingPercentileMsec( const openq4FramePacingStats_t &stats, int percentile ) {
	if ( stats.frameSampleCount <= 0 ) {
		return 0;
	}

	const int target = Max( 1, ( stats.frameSampleCount * percentile + 99 ) / 100 );
	int accumulated = 0;
	for ( int i = 0; i < OPENQ4_FRAME_PACING_HISTOGRAM_BINS; ++i ) {
		accumulated += stats.frameMsecHistogram[i];
		if ( accumulated >= target ) {
			return i == OPENQ4_FRAME_PACING_HISTOGRAM_OVERFLOW ? stats.maxFrameMsec : i;
		}
	}

	return stats.maxFrameMsec;
}

static void Session_PrintFramePacingSummary( const openq4FramePacingStats_t &stats, const char *reason ) {
	common->Printf(
		"Frame pacing%s (%s): bound=%s, samples=%d, present=%.2f ms (%.1f Hz), p50=%d ms, p95=%d ms, p99=%d ms, max=%d ms, async=%.2f ms (%.1f Hz), ticDelta/frame=%.2f, gameTics/frame=%.2f, wait overshoot=%.2f ms, wake jitter=%.2f ms\n",
		reason != NULL ? reason : "",
		stats.multiplayer ? "MP" : "SP",
		Session_GetFramePacingBoundName( stats.boundMode ),
		stats.frameSampleCount,
		stats.avgFrameMsec,
		stats.avgFrameHz,
		Session_GetFramePacingPercentileMsec( stats, 50 ),
		Session_GetFramePacingPercentileMsec( stats, 95 ),
		Session_GetFramePacingPercentileMsec( stats, 99 ),
		stats.maxFrameMsec,
		stats.asyncStats.avgDeltaMsec,
		stats.asyncStats.avgHz,
		stats.avgTicsPerFrame,
		stats.avgGameTicsPerFrame,
		stats.avgWaitOvershootMsec,
		stats.avgWakeJitterMsec );
}

#ifndef ID_DEDICATED
static void Session_FramePacingSnapshot_f( const idCmdArgs &args ) {
	const char *reason = NULL;
	idStr reasonText;
	if ( args.Argc() > 1 ) {
		reasonText = args.Args( 1, -1 );
		reason = reasonText.c_str();
	}

	sessLocal.PrintFramePacingSnapshot( reason );
}

static void Session_FramePacingReset_f( const idCmdArgs &args ) {
	sessLocal.ResetFramePacingStats();
	common->Printf( "Frame pacing stats reset\n" );
}

static void Session_TestWaitBox_f( const idCmdArgs &args ) {
	int durationMsec = 2000;
	bool network = false;
	const char *reason = NULL;
	idStr reasonText;

	if ( args.Argc() > 1 ) {
		durationMsec = atoi( args.Argv( 1 ) );
	}
	if ( args.Argc() > 2 ) {
		network = ( atoi( args.Argv( 2 ) ) != 0 );
	}
	if ( args.Argc() > 3 ) {
		reasonText = args.Args( 3, -1 );
		reason = reasonText.c_str();
	}

	sessLocal.RunTimedWaitBoxPacingTest( durationMsec, network, reason );
}

static void Session_TestMessageBox_f( const idCmdArgs &args ) {
	int durationMsec = 2000;
	bool network = false;
	const char *reason = NULL;
	idStr reasonText;

	if ( args.Argc() > 1 ) {
		durationMsec = atoi( args.Argv( 1 ) );
	}
	if ( args.Argc() > 2 ) {
		network = ( atoi( args.Argv( 2 ) ) != 0 );
	}
	if ( args.Argc() > 3 ) {
		reasonText = args.Args( 3, -1 );
		reason = reasonText.c_str();
	}

	sessLocal.RunTimedMessageBoxPacingTest( durationMsec, network, reason );
}
#endif

static openq4FramePacingBound_t Session_ClassifyFramePacing( const openq4FramePacingStats_t &stats ) {
	if ( !stats.valid || stats.avgFrameMsec <= 0.0f ) {
		return OPENQ4_FRAME_BOUND_UNKNOWN;
	}

	const float ticMsec = 1000.0f / static_cast<float>( USERCMD_HZ );
	if ( stats.presentationCap > 0 ) {
		const float capMsec = 1000.0f / static_cast<float>( stats.presentationCap );
		if ( capMsec > ticMsec * 1.10f && stats.avgFrameMsec >= capMsec * 0.95f ) {
			return OPENQ4_FRAME_BOUND_PRESENTATION_CAP;
		}
	}

	if ( stats.swapInterval > 0 && stats.avgFrameMsec > ticMsec * 1.10f ) {
		return OPENQ4_FRAME_BOUND_VSYNC;
	}

	if ( stats.avgTicsPerFrame >= 0.90f || stats.avgGameTicsPerFrame >= 0.90f || stats.lastRequestedWaitMsec > 0 ) {
		return OPENQ4_FRAME_BOUND_SIMULATION;
	}

	return OPENQ4_FRAME_BOUND_UNCAPPED;
}

// these must be kept up to date with window Levelshot in guis/mainmenu.gui
const int PREVIEW_X = 211;
const int PREVIEW_Y = 31;
const int PREVIEW_WIDTH = 398;
const int PREVIEW_HEIGHT = 298;

#ifndef ID_DEDICATED
static bool Session_IsRetailSaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0;
}

static bool Session_IsLegacyopenQ4SaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_LEGACY_OPENQ4 ) == 0;
}

static bool Session_IsSupportedSaveGameName( const idStr &gameName ) {
	return Session_IsRetailSaveGameName( gameName ) || Session_IsLegacyopenQ4SaveGameName( gameName );
}

static bool Session_SaveGameHeaderUsesEntityFilter( const idStr &gameName ) {
	return Session_IsRetailSaveGameName( gameName );
}

static bool Session_IsCompatibleSaveGameVersion( const int version );
#endif
static const int SESSION_MAX_SAVEGAME_DICT_KV = 16384;
static const int SESSION_MAX_CMD_DEMO_DICT_KV = 16384;
static const int SESSION_MAX_CMD_DEMO_TOTAL_KV = 65536;
static const int SESSION_MAX_CMD_DEMO_HEADER_BYTES = 16 * 1024 * 1024;
static const int SESSION_MAX_SAVEGAME_BYTES = 512 * 1024 * 1024;
static const int SESSION_MAX_SAVE_DESCRIPTION_BYTES = 8192;
static const int SESSION_MAX_SAVE_PREVIEW_BYTES = 64 * 1024 * 1024;
static const int SESSION_SAVE_PREVIEW_DELETE_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'D' << 24 );
static const int SESSION_SAVE_PREVIEW_DELETE_VERSION = 1;
static const char *SESSION_SAVEGAME_NO_OVERWRITE_TOKEN = "nooverwrite";
static const int SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'S' << 24 );
static const int SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION = 3;
static const int SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION = 2;
// Oldest v3 payload this build has a verified decoder for. Saves at or above it
// restore cleanly; the released v0.10 payload (build 1) still desyncs part way
// through, and it did so only after the running map had been torn down. Refuse
// anything below the verified floor during preflight, while the session is intact.
static const int SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD = 661;
static const int SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'F' << 24 );
static const int SESSION_OPENQ4_SAVEGAME_FOOTER_VERSION = 1;
static const int SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES = 5 * static_cast<int>( sizeof( int ) );
static const int SESSION_OPENQ4_SAVEGAME_INTEGRITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'I' << 24 );
static const int SESSION_OPENQ4_SAVEGAME_INTEGRITY_VERSION = 1;
static const int SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES = 4 * static_cast<int>( sizeof( int ) );
static const int SESSION_OPENQ4_SAVEGAME_SOURCE_STAMP_MAX_BYTES = 128;
static const int SESSION_OPENQ4_SAVEGAME_ABI_STAMP_MAX_BYTES = 96;
static const char *SESSION_LEGACY_SAVEGAME_WIRE_ABI = "windows-msvcabi-x64-le-raw1";

#ifndef ID_DEDICATED
static const char *Session_GetSaveGameWireABI( void );

static const char *Session_GetSaveGameWireABI( void ) {
#if defined( _WIN32 )
	#define SESSION_SAVEGAME_ABI_OS "windows"
	#define SESSION_SAVEGAME_ABI_COMPILER "msvcabi"
#elif defined( __APPLE__ )
	#define SESSION_SAVEGAME_ABI_OS "macos"
	#define SESSION_SAVEGAME_ABI_COMPILER "itaniumabi"
#elif defined( __linux__ )
	#define SESSION_SAVEGAME_ABI_OS "linux"
	#define SESSION_SAVEGAME_ABI_COMPILER "itaniumabi"
#else
	#define SESSION_SAVEGAME_ABI_OS "unknownos"
	#define SESSION_SAVEGAME_ABI_COMPILER "unknownabi"
#endif

#if defined( _M_X64 ) || defined( __x86_64__ )
	#define SESSION_SAVEGAME_ABI_ARCH "x64"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define SESSION_SAVEGAME_ABI_ARCH "arm64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define SESSION_SAVEGAME_ABI_ARCH "x86"
#elif defined( _M_ARM ) || defined( __arm__ )
	#define SESSION_SAVEGAME_ABI_ARCH "arm32"
#else
	#define SESSION_SAVEGAME_ABI_ARCH "unknownarch"
#endif

#if defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	#define SESSION_SAVEGAME_ABI_ENDIAN "be"
#else
	#define SESSION_SAVEGAME_ABI_ENDIAN "le"
#endif

	return SESSION_SAVEGAME_ABI_OS "-" SESSION_SAVEGAME_ABI_COMPILER "-" SESSION_SAVEGAME_ABI_ARCH "-" SESSION_SAVEGAME_ABI_ENDIAN "-raw1";

#undef SESSION_SAVEGAME_ABI_ENDIAN
#undef SESSION_SAVEGAME_ABI_ARCH
#undef SESSION_SAVEGAME_ABI_COMPILER
#undef SESSION_SAVEGAME_ABI_OS
}
#endif

#ifndef ID_DEDICATED
static bool Session_IsSafeSaveMaterialPath( const idStr &path ) {
	if ( path.IsEmpty() ) {
		return true;
	}
	if ( path.Length() >= MAX_STRING_CHARS || path[0] == '/' || path[0] == '\\' ) {
		return false;
	}
	if ( idStr::FindText( path.c_str(), ".." ) >= 0 ) {
		return false;
	}

	for ( int i = 0; i < path.Length(); i++ ) {
		const unsigned char ch = static_cast<unsigned char>( path[i] );
		if ( ch <= ' ' || ch >= 128 || ch == '\\' || ch == ':' || ch == '"' || ch == '<' || ch == '>' || ch == '|' ) {
			return false;
		}
	}

	return true;
}

static bool Session_SaveDescriptionMatchesSlot( const idStr &expectedSlotName, const idStr &descriptionSaveName ) {
	if ( expectedSlotName.IsEmpty() || descriptionSaveName.IsEmpty() ) {
		return false;
	}

	idStr scrubbedName = descriptionSaveName;
	sessLocal.ScrubSaveGameFileName( scrubbedName );
	return scrubbedName.Icmp( expectedSlotName ) == 0;
}

static bool Session_WriteSaveGameBytes( idFile *file, const void *buffer, int len, const char *fieldName, const char *savePath ) {
	if ( len < 0 ) {
		common->Warning( "Savegame '%s' has invalid negative %s write length %d",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "data", len );
		return false;
	}
	if ( len == 0 ) {
		return true;
	}
	if ( buffer == NULL ) {
		common->Warning( "Savegame '%s' has null source while writing %s",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "data" );
		return false;
	}

	const int offset = file->Tell();
	const int bytesWritten = file->Write( buffer, len );
	if ( bytesWritten != len ) {
		common->Warning( "Savegame '%s' failed while writing %s at offset %d (wrote %d of %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "data", offset, bytesWritten, len );
		return false;
	}

	return true;
}

static bool Session_WriteSaveGameInt( idFile *file, int value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesWritten = file->WriteInt( value );
	if ( bytesWritten != sizeof( value ) ) {
		common->Warning( "Savegame '%s' failed while writing %s at offset %d (wrote %d of %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "integer",
			offset,
			bytesWritten,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_WriteSaveGameUnsignedInt( idFile *file, unsigned int value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesWritten = file->WriteUnsignedInt( value );
	if ( bytesWritten != sizeof( value ) ) {
		common->Warning( "Savegame '%s' failed while writing %s at offset %d (wrote %d of %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "unsigned integer",
			offset,
			bytesWritten,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_WriteSaveGameString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath ) {
	if ( string == NULL ) {
		string = "";
	}

	const int len = idStr::Length( string );
	if ( len < 0 || len > maxLength ) {
		common->Warning( "Savegame '%s' has invalid %s length %d (max %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", len, maxLength );
		return false;
	}

	return Session_WriteSaveGameInt( file, len, fieldName, savePath ) &&
		Session_WriteSaveGameBytes( file, string, len, fieldName, savePath );
}

static bool Session_WriteSaveGameCString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath ) {
	if ( string == NULL ) {
		string = "";
	}

	const int len = idStr::Length( string );
	if ( len < 0 || len >= maxLength ) {
		common->Warning( "Savegame '%s' has invalid %s length %d (max %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", len, maxLength - 1 );
		return false;
	}

	return Session_WriteSaveGameBytes( file, string, len + 1, fieldName, savePath );
}

static bool Session_WriteSaveGameDict( idFile *file, const idDict &dict, const char *fieldName, const char *savePath ) {
	const int count = dict.GetNumKeyVals();
	if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV ) {
		common->Warning( "Savegame '%s' has invalid %s key/value count %d (max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "dictionary",
			count,
			SESSION_MAX_SAVEGAME_DICT_KV );
		return false;
	}

	if ( !Session_WriteSaveGameInt( file, count, fieldName, savePath ) ) {
		return false;
	}

	for ( int i = 0; i < count; i++ ) {
		const idKeyValue *kv = dict.GetKeyVal( i );
		if ( kv == NULL ) {
			common->Warning( "Savegame '%s' has NULL %s key/value pair %d",
				savePath ? savePath : "<unknown>", fieldName ? fieldName : "dictionary", i );
			return false;
		}
		if ( !Session_WriteSaveGameCString( file, kv->GetKey(), MAX_STRING_CHARS, va( "%s key %d", fieldName ? fieldName : "dictionary", i ), savePath ) ||
			 !Session_WriteSaveGameCString( file, kv->GetValue(), MAX_STRING_CHARS, va( "%s value %d", fieldName ? fieldName : "dictionary", i ), savePath ) ) {
			return false;
		}
	}

	return true;
}

static bool Session_WriteSaveTextLine( idFile *file, const char *line, bool quoted, const char *fieldName, const char *savePath ) {
	idStr output;
	if ( quoted ) {
		output = "\"";
		output += line ? line : "";
		output += "\"\n";
	} else {
		output = line ? line : "";
		output += "\n";
	}

	return Session_WriteSaveGameBytes( file, output.c_str(), output.Length(), fieldName, savePath );
}

static bool Session_ValidateStagedSaveDescription( const idStr &descriptionPath, const idStr &expectedSlotName, saveType_t saveType ) {
	idStr gameDir = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( descriptionPath.c_str(), true, gameDir.Length() ? gameDir.c_str() : NULL );
	if ( file == NULL ) {
		common->Warning( "Failed to reopen staged save description '%s' for validation", descriptionPath.c_str() );
		return false;
	}

	const int len = file->Length();
	if ( len <= 0 || len > SESSION_MAX_SAVE_DESCRIPTION_BYTES ) {
		common->Warning( "Staged save description '%s' has invalid size %d (max %d)",
			descriptionPath.c_str(), len, SESSION_MAX_SAVE_DESCRIPTION_BYTES );
		fileSystem->CloseFile( file );
		return false;
	}

	char *buffer = new char[ len + 1 ];
	const int bytesRead = file->Read( buffer, len );
	fileSystem->CloseFile( file );
	if ( bytesRead != len ) {
		common->Warning( "Staged save description '%s' is truncated (read %d of %d)",
			descriptionPath.c_str(), bytesRead, len );
		delete[] buffer;
		return false;
	}
	buffer[ len ] = '\0';

	idStr sidecarSaveName;
	idStr sidecarDescription;
	idStr sidecarScreenshot;
	bool sidecarNoOverwrite = false;
	bool valid = true;
	{
		idLexer src( buffer, len, descriptionPath.c_str(), LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );
		idToken tok;
		if ( src.ReadToken( &tok ) ) {
			sidecarSaveName = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			sidecarDescription = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			sidecarScreenshot = tok;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			if ( !idStr::Icmp( tok.c_str(), SESSION_SAVEGAME_NO_OVERWRITE_TOKEN ) ) {
				sidecarNoOverwrite = true;
			} else {
				common->Warning( "Staged save description '%s' has unexpected token '%s'",
					descriptionPath.c_str(), tok.c_str() );
				valid = false;
			}
		}
		if ( valid && src.ReadToken( &tok ) ) {
			common->Warning( "Staged save description '%s' has trailing token '%s'",
				descriptionPath.c_str(), tok.c_str() );
			valid = false;
		}
	}

	delete[] buffer;

	if ( valid && !Session_SaveDescriptionMatchesSlot( expectedSlotName, sidecarSaveName ) ) {
		common->Warning( "Staged save description '%s' names slot '%s', expected '%s'",
			descriptionPath.c_str(), sidecarSaveName.c_str(), expectedSlotName.c_str() );
		valid = false;
	}
	if ( valid && ( sidecarSaveName.Length() >= MAX_STRING_CHARS || sidecarDescription.Length() >= MAX_STRING_CHARS ) ) {
		common->Warning( "Staged save description '%s' has an oversized display token", descriptionPath.c_str() );
		valid = false;
	}
	if ( valid && !Session_IsSafeSaveMaterialPath( sidecarScreenshot ) ) {
		common->Warning( "Staged save description '%s' has unsafe screenshot material '%s'",
			descriptionPath.c_str(), sidecarScreenshot.c_str() );
		valid = false;
	}

	const bool expectNoOverwrite = ( saveType == ST_AUTO || saveType == ST_CHECKPOINT );
	if ( valid && sidecarNoOverwrite != expectNoOverwrite ) {
		common->Warning( "Staged save description '%s' no-overwrite marker is %s but expected %s",
			descriptionPath.c_str(), sidecarNoOverwrite ? "present" : "absent", expectNoOverwrite ? "present" : "absent" );
		valid = false;
	}
	if ( valid && saveType == ST_AUTO && sidecarScreenshot.IsEmpty() ) {
		common->Warning( "Staged autosave description '%s' is missing its loadscreen material", descriptionPath.c_str() );
		valid = false;
	}
	if ( valid && saveType != ST_AUTO && !sidecarScreenshot.IsEmpty() ) {
		common->Warning( "Staged save description '%s' unexpectedly names screenshot material '%s'",
			descriptionPath.c_str(), sidecarScreenshot.c_str() );
		valid = false;
	}

	return valid;
}

static bool Session_ReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadInt( value );
	if ( bytesRead != sizeof( value ) ) {
		common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_ReadSaveGameUnsignedInt( idFile *file, unsigned int &value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadUnsignedInt( value );
	if ( bytesRead != sizeof( value ) ) {
		common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "unsigned integer",
			offset,
			bytesRead,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_ReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
	int len = 0;
	const int lengthOffset = file->Tell();
	if ( !Session_ReadSaveGameInt( file, len, fieldName, savePath ) ) {
		string.Clear();
		return false;
	}

	const int remainingBytes = Max( 0, file->Length() - file->Tell() );
	if ( len < 0 || len > maxLength || len > remainingBytes ) {
		common->Warning( "Savegame '%s' has invalid %s length %d at offset %d (remaining %d, max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "string",
			len,
			lengthOffset,
			remainingBytes,
			maxLength );
		string.Clear();
		return false;
	}

	string.Fill( ' ', len );
	if ( len > 0 ) {
		const int dataOffset = file->Tell();
		const int bytesRead = file->Read( &string[0], len );
		if ( bytesRead != len ) {
			common->Warning( "Savegame '%s' is truncated while reading %s data at offset %d (read %d of %d)",
				savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", dataOffset, bytesRead, len );
			string.Clear();
			return false;
		}
	}

	return true;
}

static bool Session_ReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
	string.Clear();

	const int offset = file->Tell();
	for ( int len = 0; len < maxLength; len++ ) {
		char ch = '\0';
		const int bytesRead = file->Read( &ch, 1 );
		if ( bytesRead != 1 ) {
			common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of 1)",
				savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", file->Tell(), bytesRead );
			string.Clear();
			return false;
		}
		if ( ch == '\0' ) {
			return true;
		}
		string += ch;
	}

	common->Warning( "Savegame '%s' has unterminated %s at offset %d (max %d)",
		savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", offset, maxLength );
	string.Clear();
	return false;
}

static bool Session_ReadSaveGameDict( idFile *file, idDict &dict, const char *fieldName, const char *savePath ) {
	int count = 0;
	if ( !Session_ReadSaveGameInt( file, count, fieldName, savePath ) ) {
		dict.Clear();
		return false;
	}
	if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV ) {
		common->Warning( "Savegame '%s' has invalid %s key/value count %d (max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "dictionary",
			count,
			SESSION_MAX_SAVEGAME_DICT_KV );
		dict.Clear();
		return false;
	}

	dict.Clear();
	for ( int i = 0; i < count; i++ ) {
		idStr key;
		idStr value;
		if ( !Session_ReadSaveGameCString( file, key, MAX_STRING_CHARS, va( "%s key %d", fieldName ? fieldName : "dictionary", i ), savePath ) ||
			 !Session_ReadSaveGameCString( file, value, MAX_STRING_CHARS, va( "%s value %d", fieldName ? fieldName : "dictionary", i ), savePath ) ) {
			dict.Clear();
			return false;
		}
		dict.Set( key, value );
	}

	return true;
}

static bool Session_GetBoundedSaveGameLength( idFile *file, const idStr &savePath, const char *operation, int &fileLength ) {
	fileLength = file != NULL ? file->Length() : -1;
	if ( fileLength <= 0 || fileLength > SESSION_MAX_SAVEGAME_BYTES ) {
		common->Warning( "Savegame '%s' has invalid %s size %d bytes (maximum total .save size %d bytes)",
			savePath.c_str(), operation != NULL ? operation : "file", fileLength, SESSION_MAX_SAVEGAME_BYTES );
		return false;
	}
	return true;
}

static bool Session_CalculateSaveGameChecksum( idFile *file, int protectedLength, unsigned int &checksum, const idStr &savePath ) {
	int fileLength = -1;
	if ( !Session_GetBoundedSaveGameLength( file, savePath, "checksum input", fileLength ) ) {
		return false;
	}
	if ( protectedLength < 0 || protectedLength > fileLength ) {
		common->Warning( "Savegame '%s' has invalid checksum length %d (file length %d)",
			savePath.c_str(), protectedLength, fileLength );
		return false;
	}
	if ( file->Seek( 0, FS_SEEK_SET ) != 0 ) {
		common->Warning( "Savegame '%s' failed to seek to the checksum start", savePath.c_str() );
		return false;
	}

	static const int CHECKSUM_CHUNK_BYTES = 64 * 1024;
	byte *buffer = new byte[ CHECKSUM_CHUNK_BYTES ];
	uint32_t crc = 0;
	CRC32_InitChecksum( crc );
	int remaining = protectedLength;
	while ( remaining > 0 ) {
		const int chunkBytes = Min( remaining, CHECKSUM_CHUNK_BYTES );
		const int bytesRead = file->Read( buffer, chunkBytes );
		if ( bytesRead != chunkBytes ) {
			common->Warning( "Savegame '%s' is truncated while checksumming offset %d (read %d of %d)",
				savePath.c_str(), protectedLength - remaining, bytesRead, chunkBytes );
			delete[] buffer;
			return false;
		}
		CRC32_UpdateChecksum( crc, buffer, chunkBytes );
		remaining -= chunkBytes;
	}
	delete[] buffer;
	CRC32_FinishChecksum( crc );
	checksum = static_cast<unsigned int>( crc );
	return true;
}

static bool Session_ValidateSaveGamePayload( idFile *file, const idStr &savePath, bool allowLegacyPayload ) {
	int fileLength = -1;
	if ( !Session_GetBoundedSaveGameLength( file, savePath, "payload preflight", fileLength ) ) {
		return false;
	}
	const int payloadOffset = file->Tell();
	if ( payloadOffset < 0 || payloadOffset > fileLength ||
		 fileLength - payloadOffset < static_cast<int>( sizeof( int ) ) ) {
		common->Warning( "Savegame '%s' is too short for a game payload (payload offset %d, length %d)",
			savePath.c_str(), payloadOffset, fileLength );
		return false;
	}

	int marker = 0;
	if ( !Session_ReadSaveGameInt( file, marker, "payload marker", savePath.c_str() ) ) {
		file->Seek( payloadOffset, FS_SEEK_SET );
		return false;
	}

	if ( marker != SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC ) {
		const bool legacyCompatible = allowLegacyPayload && marker == BUILD_NUMBER &&
			idStr::Icmp( Session_GetSaveGameWireABI(), SESSION_LEGACY_SAVEGAME_WIRE_ABI ) == 0;
		if ( !legacyCompatible ) {
			common->Warning( "Savegame '%s' has unsupported legacy payload build/marker %d (expected build %d or marker 0x%08x)",
				savePath.c_str(), marker, BUILD_NUMBER, SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC );
		}
		if ( file->Seek( payloadOffset, FS_SEEK_SET ) != 0 ) {
			common->Warning( "Savegame '%s' failed to rewind to payload offset %d", savePath.c_str(), payloadOffset );
			return false;
		}
		return legacyCompatible;
	}

	const int minimumStampedBytes = 5 * static_cast<int>( sizeof( int ) ) + SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES;
	if ( fileLength - payloadOffset < minimumStampedBytes ) {
		common->Warning( "Savegame '%s' is too short for a stamped openQ4 payload/footer (payload offset %d, length %d)",
			savePath.c_str(), payloadOffset, fileLength );
		file->Seek( payloadOffset, FS_SEEK_SET );
		return false;
	}

	int payloadVersion = 0;
	int payloadBuild = 0;
	idStr payloadSourceStamp;
	int payloadSourceFileCount = 0;
	idStr payloadWireABI;
	bool valid =
		Session_ReadSaveGameInt( file, payloadVersion, "payload version", savePath.c_str() ) &&
		Session_ReadSaveGameInt( file, payloadBuild, "payload build", savePath.c_str() ) &&
		Session_ReadSaveGameString( file, payloadSourceStamp, SESSION_OPENQ4_SAVEGAME_SOURCE_STAMP_MAX_BYTES, "payload source stamp", savePath.c_str() ) &&
		Session_ReadSaveGameInt( file, payloadSourceFileCount, "payload source file count", savePath.c_str() );

	if ( valid && payloadVersion != SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION &&
		 payloadVersion != SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION ) {
		common->Warning( "Savegame '%s' has unsupported payload version %d (supported %d and %d)",
			savePath.c_str(), payloadVersion,
			SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION,
			SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION );
		valid = false;
	}
	if ( valid && payloadVersion == SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION ) {
		valid = Session_ReadSaveGameString( file, payloadWireABI, SESSION_OPENQ4_SAVEGAME_ABI_STAMP_MAX_BYTES,
			"payload wire ABI", savePath.c_str() );
	}
	if ( valid && ( payloadSourceStamp.IsEmpty() || payloadSourceFileCount < 0 ) ) {
		common->Warning( "Savegame '%s' has invalid payload source file count %d",
			savePath.c_str(), payloadSourceFileCount );
		valid = false;
	}

	if ( valid && payloadVersion == SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION ) {
		// Every v2 snapshot that was tested against a real save desynced part way
		// through the restore, so none of them are claimed any more.
		common->Warning( "Savegame '%s' was written by an older openQ4 (v%d payload, build %d) and cannot be restored by this build",
			savePath.c_str(), payloadVersion, payloadBuild );
		valid = false;
	} else if ( valid ) {
		if ( payloadWireABI.Icmp( Session_GetSaveGameWireABI() ) != 0 ) {
			common->Warning( "Savegame '%s' wire ABI '%s' is incompatible with this '%s' build",
				savePath.c_str(), payloadWireABI.c_str(), Session_GetSaveGameWireABI() );
			valid = false;
		} else if ( payloadBuild < SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD ) {
			common->Warning( "Savegame '%s' was written by an older openQ4 (build %d, below build %d, the oldest this build can restore)",
				savePath.c_str(), payloadBuild, SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD );
			valid = false;
		} else if ( payloadBuild != BUILD_NUMBER ||
				OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT < 0 ||
				payloadSourceStamp.Icmp( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH ) != 0 ||
				payloadSourceFileCount != OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT ) {
			common->DPrintf( "Savegame '%s' was produced by build/source %d/%s (%d files); current schema-compatible build/source is %d/%s (%d files)\n",
				savePath.c_str(), payloadBuild, payloadSourceStamp.c_str(), payloadSourceFileCount,
				BUILD_NUMBER, OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH, OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT );
		}
	}

	const int compatibilityHeaderEnd = file->Tell();
	int protectedLength = fileLength;
	if ( valid && payloadVersion == SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION ) {
		const int integrityOffset = fileLength - SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES;
		if ( integrityOffset <= compatibilityHeaderEnd + SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES ||
			 file->Seek( integrityOffset, FS_SEEK_SET ) != 0 ) {
			common->Warning( "Savegame '%s' has invalid integrity trailer offset %d", savePath.c_str(), integrityOffset );
			valid = false;
		}

		int integrityMarker = 0;
		int integrityVersion = 0;
		int savedProtectedLength = 0;
		unsigned int savedChecksum = 0;
		if ( valid && ( !Session_ReadSaveGameInt( file, integrityMarker, "integrity marker", savePath.c_str() ) ||
			 !Session_ReadSaveGameInt( file, integrityVersion, "integrity version", savePath.c_str() ) ||
			 !Session_ReadSaveGameInt( file, savedProtectedLength, "integrity protected length", savePath.c_str() ) ||
			 !Session_ReadSaveGameUnsignedInt( file, savedChecksum, "integrity checksum", savePath.c_str() ) ) ) {
			valid = false;
		}
		if ( valid && integrityMarker != SESSION_OPENQ4_SAVEGAME_INTEGRITY_MAGIC ) {
			common->Warning( "Savegame '%s' has invalid integrity marker 0x%08x", savePath.c_str(), integrityMarker );
			valid = false;
		}
		if ( valid && integrityVersion != SESSION_OPENQ4_SAVEGAME_INTEGRITY_VERSION ) {
			common->Warning( "Savegame '%s' has integrity version %d, expected %d",
				savePath.c_str(), integrityVersion, SESSION_OPENQ4_SAVEGAME_INTEGRITY_VERSION );
			valid = false;
		}
		if ( valid && savedProtectedLength != integrityOffset ) {
			common->Warning( "Savegame '%s' integrity length %d does not match trailer offset %d",
				savePath.c_str(), savedProtectedLength, integrityOffset );
			valid = false;
		}

		unsigned int calculatedChecksum = 0;
		if ( valid && !Session_CalculateSaveGameChecksum( file, savedProtectedLength, calculatedChecksum, savePath ) ) {
			valid = false;
		}
		if ( valid && calculatedChecksum != savedChecksum ) {
			common->Warning( "Savegame '%s' failed its integrity check (stored 0x%08x, calculated 0x%08x)",
				savePath.c_str(), savedChecksum, calculatedChecksum );
			valid = false;
		}
		protectedLength = savedProtectedLength;
	}

	const int footerOffset = protectedLength - SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES;
	if ( valid && ( footerOffset <= compatibilityHeaderEnd || file->Seek( footerOffset, FS_SEEK_SET ) != 0 ) ) {
		common->Warning( "Savegame '%s' has invalid payload footer offset %d (payload offset %d, length %d)",
			savePath.c_str(), footerOffset, payloadOffset, protectedLength );
		valid = false;
	}

	int footerMarker = 0;
	int footerVersion = 0;
	int savedFooterOffset = 0;
	int savedObjectCount = 0;
	int savedSyncCount = 0;
	if ( valid && ( !Session_ReadSaveGameInt( file, footerMarker, "payload footer marker", savePath.c_str() ) ||
		 !Session_ReadSaveGameInt( file, footerVersion, "payload footer version", savePath.c_str() ) ||
		 !Session_ReadSaveGameInt( file, savedFooterOffset, "payload footer offset", savePath.c_str() ) ||
		 !Session_ReadSaveGameInt( file, savedObjectCount, "payload footer object count", savePath.c_str() ) ||
		 !Session_ReadSaveGameInt( file, savedSyncCount, "payload footer sync count", savePath.c_str() ) ) ) {
		valid = false;
	}

	if ( valid && footerMarker != SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC ) {
		common->Warning( "Savegame '%s' has invalid payload footer marker 0x%08x",
			savePath.c_str(), footerMarker );
		valid = false;
	}
	if ( valid && footerVersion != SESSION_OPENQ4_SAVEGAME_FOOTER_VERSION ) {
		common->Warning( "Savegame '%s' has footer version %d, expected %d",
			savePath.c_str(), footerVersion, SESSION_OPENQ4_SAVEGAME_FOOTER_VERSION );
		valid = false;
	}
	if ( valid && savedFooterOffset != footerOffset ) {
		common->Warning( "Savegame '%s' footer offset %d does not match EOF footer offset %d",
			savePath.c_str(), savedFooterOffset, footerOffset );
		valid = false;
	}
	if ( valid && ( savedObjectCount < 0 || savedSyncCount < 0 ) ) {
		common->Warning( "Savegame '%s' has invalid footer counts (objects %d, sync markers %d)",
			savePath.c_str(), savedObjectCount, savedSyncCount );
		valid = false;
	}

	if ( file->Seek( payloadOffset, FS_SEEK_SET ) != 0 ) {
		common->Warning( "Savegame '%s' failed to rewind to payload offset %d", savePath.c_str(), payloadOffset );
		return false;
	}
	return valid;
}

static bool Session_RelativeSaveFileExists( const idStr &relativePath ) {
	idStr osPath = fileSystem->RelativePathToOSPath( relativePath.c_str(), "fs_savepath" );
	idFile *file = fileSystem->OpenExplicitFileRead( osPath.c_str() );
	if ( file == NULL ) {
		return false;
	}
	fileSystem->CloseFile( file );
	return true;
}

static bool Session_ValidateStagedSavePreview( const idStr &previewPath ) {
	idStr gameDir = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( previewPath.c_str(), true, gameDir.Length() ? gameDir.c_str() : NULL );
	if ( file == NULL ) {
		return false;
	}

	const int len = file->Length();
	if ( len < 18 || len > SESSION_MAX_SAVE_PREVIEW_BYTES ) {
		common->Warning( "Staged save preview '%s' has invalid size %d (max %d)",
			previewPath.c_str(), len, SESSION_MAX_SAVE_PREVIEW_BYTES );
		fileSystem->CloseFile( file );
		return false;
	}

	unsigned char header[18];
	const int bytesRead = file->Read( header, static_cast<int>( sizeof( header ) ) );
	fileSystem->CloseFile( file );
	if ( bytesRead != static_cast<int>( sizeof( header ) ) ) {
		common->Warning( "Staged save preview '%s' is truncated while reading TGA header", previewPath.c_str() );
		return false;
	}

	const int imageType = header[2];
	const int width = header[12] | ( header[13] << 8 );
	const int height = header[14] | ( header[15] << 8 );
	const int bitsPerPixel = header[16];
	if ( imageType != 2 && imageType != 10 ) {
		common->Warning( "Staged save preview '%s' has unsupported TGA image type %d", previewPath.c_str(), imageType );
		return false;
	}
	if ( width <= 0 || height <= 0 || width > 8192 || height > 8192 ) {
		common->Warning( "Staged save preview '%s' has invalid dimensions %dx%d", previewPath.c_str(), width, height );
		return false;
	}
	if ( bitsPerPixel != 24 && bitsPerPixel != 32 ) {
		common->Warning( "Staged save preview '%s' has unsupported TGA bit depth %d", previewPath.c_str(), bitsPerPixel );
		return false;
	}

	return true;
}

static bool Session_IsSavePreviewDeletionMarker( const idStr &relativePath ) {
	idStr osPath = fileSystem->RelativePathToOSPath( relativePath.c_str(), "fs_savepath" );
	idFile *file = fileSystem->OpenExplicitFileRead( osPath.c_str() );
	if ( file == NULL || file->Length() != 2 * static_cast<int>( sizeof( int ) ) ) {
		if ( file != NULL ) {
			fileSystem->CloseFile( file );
		}
		return false;
	}

	int marker = 0;
	int version = 0;
	const bool matches = file->ReadInt( marker ) == sizeof( marker ) &&
		file->ReadInt( version ) == sizeof( version ) &&
		marker == SESSION_SAVE_PREVIEW_DELETE_MAGIC && version == SESSION_SAVE_PREVIEW_DELETE_VERSION;
	fileSystem->CloseFile( file );
	return matches;
}

static bool Session_CreateSavePreviewDeletionMarker( const idStr &relativePath ) {
	idFile *file = fileSystem->OpenFileWrite( relativePath.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		return false;
	}

	const bool written =
		Session_WriteSaveGameInt( file, SESSION_SAVE_PREVIEW_DELETE_MAGIC, "preview deletion marker", relativePath.c_str() ) &&
		Session_WriteSaveGameInt( file, SESSION_SAVE_PREVIEW_DELETE_VERSION, "preview deletion version", relativePath.c_str() );
	const bool synced = written && file->Sync();
	fileSystem->CloseFile( file );
	return written && synced;
}

static void Session_RemoveRelativeSaveFile( const idStr &relativePath ) {
	idStr osPath = fileSystem->RelativePathToOSPath( relativePath.c_str(), "fs_savepath" );
	fileSystem->RemoveExplicitFile( osPath.c_str() );
}

static bool Session_RenameRelativeSaveFile( const idStr &from, const idStr &to ) {
	idStr fromOSPath = fileSystem->RelativePathToOSPath( from.c_str(), "fs_savepath" );
	idStr toOSPath = fileSystem->RelativePathToOSPath( to.c_str(), "fs_savepath" );
	return rename( fromOSPath.c_str(), toOSPath.c_str() ) == 0;
}

static bool Session_AppendSaveGameIntegrityTrailer( const idStr &relativePath ) {
	idStr osPath = fileSystem->RelativePathToOSPath( relativePath.c_str(), "fs_savepath" );
	idFile *readFile = fileSystem->OpenExplicitFileRead( osPath.c_str() );
	if ( readFile == NULL ) {
		common->Warning( "Failed to reopen savegame '%s' for integrity protection", relativePath.c_str() );
		return false;
	}

	int protectedLength = -1;
	if ( !Session_GetBoundedSaveGameLength( readFile, relativePath, "staged write", protectedLength ) ) {
		fileSystem->CloseFile( readFile );
		return false;
	}
	if ( protectedLength > SESSION_MAX_SAVEGAME_BYTES - SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES ) {
		common->Warning( "Staged savegame '%s' is %d bytes before its %d-byte integrity trailer (maximum total .save size %d bytes)",
			relativePath.c_str(), protectedLength, SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES, SESSION_MAX_SAVEGAME_BYTES );
		fileSystem->CloseFile( readFile );
		return false;
	}
	unsigned int checksum = 0;
	const bool checksumReady = Session_CalculateSaveGameChecksum( readFile, protectedLength, checksum, relativePath );
	fileSystem->CloseFile( readFile );
	if ( !checksumReady ) {
		return false;
	}

	idFile *appendFile = fileSystem->OpenFileAppend( relativePath.c_str(), true, "fs_savepath" );
	if ( appendFile == NULL ) {
		common->Warning( "Failed to open savegame '%s' for its integrity trailer", relativePath.c_str() );
		return false;
	}

	const bool written =
		Session_WriteSaveGameInt( appendFile, SESSION_OPENQ4_SAVEGAME_INTEGRITY_MAGIC, "integrity marker", relativePath.c_str() ) &&
		Session_WriteSaveGameInt( appendFile, SESSION_OPENQ4_SAVEGAME_INTEGRITY_VERSION, "integrity version", relativePath.c_str() ) &&
		Session_WriteSaveGameInt( appendFile, protectedLength, "integrity protected length", relativePath.c_str() ) &&
		Session_WriteSaveGameUnsignedInt( appendFile, checksum, "integrity checksum", relativePath.c_str() );
	const bool synced = written && appendFile->Sync();
	fileSystem->CloseFile( appendFile );
	if ( written && !synced ) {
		common->Warning( "Failed to persist savegame integrity trailer '%s'", relativePath.c_str() );
	}
	return written && synced;
}

static bool Session_SyncRelativeSaveFile( const idStr &relativePath ) {
	idFile *file = fileSystem->OpenFileAppend( relativePath.c_str(), true, "fs_savepath" );
	if ( file == NULL ) {
		return false;
	}
	const bool synced = file->Sync();
	fileSystem->CloseFile( file );
	return synced;
}

struct sessionStagedSaveFile_t {
	idStr	tempName;
	idStr	finalName;
	idStr	backupName;
	bool	required;
	bool	backedUp;
	bool	committed;
};

static void Session_InitStagedSaveFile( sessionStagedSaveFile_t &file, const idStr &tempName, const idStr &finalName, bool required ) {
	file.tempName = tempName;
	file.finalName = finalName;
	file.backupName = finalName;
	file.backupName += ".prev";
	file.required = required;
	file.backedUp = false;
	file.committed = false;
}

static void Session_RollbackStagedSaveFiles( sessionStagedSaveFile_t *files, int numFiles ) {
	for ( int i = numFiles - 1; i >= 0; i-- ) {
		if ( files[i].committed ) {
			Session_RemoveRelativeSaveFile( files[i].finalName );
			files[i].committed = false;
		}
		if ( files[i].backedUp ) {
			Session_RenameRelativeSaveFile( files[i].backupName, files[i].finalName );
			files[i].backedUp = false;
		}
		Session_RemoveRelativeSaveFile( files[i].tempName );
	}
}

static void Session_CleanupCommittedSaveFiles( sessionStagedSaveFile_t *files, int numFiles ) {
	for ( int i = 0; i < numFiles; i++ ) {
		if ( files[i].backedUp ) {
			Session_RemoveRelativeSaveFile( files[i].backupName );
			files[i].backedUp = false;
		}
	}
}

static bool Session_CommitStagedSaveFiles( sessionStagedSaveFile_t *files, int numFiles ) {
	for ( int i = 0; i < numFiles; i++ ) {
		Session_RemoveRelativeSaveFile( files[i].backupName );
		if ( Session_RelativeSaveFileExists( files[i].finalName ) ) {
			if ( !Session_RenameRelativeSaveFile( files[i].finalName, files[i].backupName ) ) {
				common->Warning( "Failed to back up save file '%s' before committing '%s'",
					files[i].finalName.c_str(), files[i].tempName.c_str() );
				Session_RollbackStagedSaveFiles( files, numFiles );
				return false;
			}
			files[i].backedUp = true;
		}
	}

	for ( int i = 0; i < numFiles; i++ ) {
		if ( Session_RenameRelativeSaveFile( files[i].tempName, files[i].finalName ) ) {
			files[i].committed = true;
			continue;
		}

		if ( files[i].required ) {
			common->Warning( "Failed to commit save file '%s' to '%s'",
				files[i].tempName.c_str(), files[i].finalName.c_str() );
			Session_RollbackStagedSaveFiles( files, numFiles );
			return false;
		}

		if ( files[i].backedUp ) {
			Session_RenameRelativeSaveFile( files[i].backupName, files[i].finalName );
			files[i].backedUp = false;
		}
	}

	Session_CleanupCommittedSaveFiles( files, numFiles );
	return true;
}

static bool Session_ValidateStagedSaveGameHeader( const idStr &savePath ) {
	idStr gamename;
	idStr saveMap;
	idStr entityFilter;
	int version = 0;

	idStr gameDir = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( savePath.c_str(), true, gameDir.Length() ? gameDir.c_str() : NULL );
	if ( file == NULL ) {
		common->Warning( "Failed to reopen staged savegame '%s' for validation", savePath.c_str() );
		return false;
	}
	int stagedFileLength = -1;
	if ( !Session_GetBoundedSaveGameLength( file, savePath, "staged validation", stagedFileLength ) ) {
		fileSystem->CloseFile( file );
		return false;
	}

	bool valid = true;
	valid = valid && Session_ReadSaveGameString( file, gamename, MAX_STRING_CHARS, "game name", savePath.c_str() );
	valid = valid && Session_IsSupportedSaveGameName( gamename );
	valid = valid && Session_ReadSaveGameInt( file, version, "version", savePath.c_str() );
	valid = valid && Session_IsCompatibleSaveGameVersion( version );
	valid = valid && Session_ReadSaveGameString( file, saveMap, MAX_STRING_CHARS, "map name", savePath.c_str() );
	if ( valid && Session_SaveGameHeaderUsesEntityFilter( gamename ) ) {
		valid = Session_ReadSaveGameString( file, entityFilter, MAX_STRING_CHARS, "entity filter", savePath.c_str() );
	}
	for ( int i = 0; valid && i < MAX_ASYNC_CLIENTS; i++ ) {
		idDict persistentPlayerInfo;
		valid = Session_ReadSaveGameDict( file, persistentPlayerInfo, va( "persistent player info %d", i ), savePath.c_str() );
	}
	if ( valid && saveMap.IsEmpty() ) {
		common->Warning( "Staged savegame '%s' has an empty map name", savePath.c_str() );
		valid = false;
	}
	if ( valid ) {
		valid = Session_ValidateSaveGamePayload( file, savePath, false );
	}

	fileSystem->CloseFile( file );
	return valid;
}

struct sessionRecoverSaveFile_t {
	idStr finalName;
	idStr tempName;
	idStr backupName;
};

static void Session_InitRecoverSaveFile( sessionRecoverSaveFile_t &file, const idStr &finalName ) {
	file.finalName = finalName;
	file.tempName = finalName;
	file.tempName += ".tmp";
	file.backupName = finalName;
	file.backupName += ".prev";
}

static bool Session_RecoverInterruptedSaveFiles( const idStr &gameFile, const idStr &descriptionFile, const idStr &previewFile ) {
	sessionRecoverSaveFile_t files[3];
	Session_InitRecoverSaveFile( files[0], gameFile );
	Session_InitRecoverSaveFile( files[1], descriptionFile );
	Session_InitRecoverSaveFile( files[2], previewFile );

	const bool tempGameExists = Session_RelativeSaveFileExists( files[0].tempName );
	bool anyBackupExists = false;
	bool anyTempExists = tempGameExists;
	for ( int i = 0; i < 3; i++ ) {
		anyBackupExists |= Session_RelativeSaveFileExists( files[i].backupName );
		anyTempExists |= Session_RelativeSaveFileExists( files[i].tempName );
	}

	if ( !tempGameExists && !anyBackupExists ) {
		// Description/preview temporaries alone cannot represent a commit. They
		// may remain after a failed capture or description write.
		if ( anyTempExists ) {
			Session_RemoveRelativeSaveFile( files[1].tempName );
			Session_RemoveRelativeSaveFile( files[2].tempName );
		}
		if ( Session_IsSavePreviewDeletionMarker( previewFile ) ) {
			Session_RemoveRelativeSaveFile( previewFile );
		}
		return true;
	}

	// The payload is committed last. A valid final payload with no payload
	// temporary therefore proves that every required sidecar rename completed.
	if ( !tempGameExists && Session_RelativeSaveFileExists( gameFile ) &&
		 Session_ValidateStagedSaveGameHeader( gameFile ) ) {
		for ( int i = 0; i < 3; i++ ) {
			Session_RemoveRelativeSaveFile( files[i].tempName );
			Session_RemoveRelativeSaveFile( files[i].backupName );
		}
		if ( Session_IsSavePreviewDeletionMarker( previewFile ) ) {
			Session_RemoveRelativeSaveFile( previewFile );
		}
		common->Printf( "Recovered committed savegame '%s' after interrupted cleanup\n", gameFile.c_str() );
		return true;
	}

	bool recovered = true;
	for ( int i = 0; i < 3; i++ ) {
		const bool tempExists = Session_RelativeSaveFileExists( files[i].tempName );
		const bool backupExists = Session_RelativeSaveFileExists( files[i].backupName );
		if ( backupExists ) {
			Session_RemoveRelativeSaveFile( files[i].finalName );
			if ( !Session_RenameRelativeSaveFile( files[i].backupName, files[i].finalName ) ) {
				common->Warning( "Failed to restore interrupted save backup '%s' to '%s'",
					files[i].backupName.c_str(), files[i].finalName.c_str() );
				recovered = false;
			}
		} else if ( ( tempGameExists && !tempExists ) || ( !tempGameExists && anyBackupExists ) ) {
			// This component was committed without replacing an older file.
			Session_RemoveRelativeSaveFile( files[i].finalName );
		}
		Session_RemoveRelativeSaveFile( files[i].tempName );
	}

	if ( recovered ) {
		common->Printf( "Rolled back interrupted savegame '%s'\n", gameFile.c_str() );
	}
	return recovered;
}

static void Session_RemoveStagedSaveFiles( const idStr &gameFile, const idStr &descriptionFile, const idStr &previewFile ) {
	Session_RemoveRelativeSaveFile( gameFile );
	Session_RemoveRelativeSaveFile( descriptionFile );
	if ( !previewFile.IsEmpty() ) {
		Session_RemoveRelativeSaveFile( previewFile );
	}
}

static void Session_RestoreGeneratedSaveState( idSessionLocal &session, bool generatedSaveName, saveType_t saveType, const idStr &previousLastQuicksave, int previousLastCheckPoint ) {
	if ( !generatedSaveName ) {
		return;
	}

	if ( saveType == ST_QUICK ) {
		idSessionLocal::com_lastQuicksave.SetString( previousLastQuicksave );
	} else if ( saveType == ST_CHECKPOINT ) {
		session.lastCheckPoint = previousLastCheckPoint;
	}
}

class sessionSaveOperationGuard_t {
public:
	sessionSaveOperationGuard_t( idSessionLocal &session, idFile *&openFile,
		const idStr &tempGameFile, const idStr &tempDescriptionFile, const idStr &tempPreviewFile,
		bool generatedSaveName, saveType_t saveType, const idStr &previousLastQuicksave, int previousLastCheckPoint ) :
		session( session ), openFile( openFile ), tempGameFile( tempGameFile ),
		tempDescriptionFile( tempDescriptionFile ), tempPreviewFile( tempPreviewFile ),
		generatedSaveName( generatedSaveName ), saveType( saveType ),
		previousLastQuicksave( previousLastQuicksave ), previousLastCheckPoint( previousLastCheckPoint ), complete( false ) {
	}

	~sessionSaveOperationGuard_t() {
		if ( openFile != NULL ) {
			fileSystem->CloseFile( openFile );
			openFile = NULL;
		}
		if ( !complete ) {
			Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
			Session_RestoreGeneratedSaveState( session, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
			session.syncNextGameFrame = true;
			if ( soundSystem != NULL ) {
				soundSystem->SetMute( session.insideExecuteMapChange );
			}
		}
	}

	void Complete( void ) {
		complete = true;
	}

private:
	idSessionLocal &session;
	idFile *&openFile;
	idStr tempGameFile;
	idStr tempDescriptionFile;
	idStr tempPreviewFile;
	bool generatedSaveName;
	saveType_t saveType;
	idStr previousLastQuicksave;
	int previousLastCheckPoint;
	bool complete;
};

class sessionRenderCropGuard_t {
public:
	sessionRenderCropGuard_t( int width, int height ) : active( false ) {
		if ( renderSystem != NULL && renderSystem->IsOpenGLRunning() && width > 0 && height > 0 ) {
			renderSystem->CropRenderSize( width, height, false, true );
			active = true;
		}
	}

	~sessionRenderCropGuard_t() {
		if ( active && renderSystem != NULL ) {
			renderSystem->UnCrop();
		}
	}

private:
	bool active;
};
#endif

void idSessionLocal::ResetFramePacingStats( void ) {
	memset( &framePacingStats, 0, sizeof( framePacingStats ) );
	framePacingStats.boundMode = OPENQ4_FRAME_BOUND_UNKNOWN;
	framePacingLastFrameMsec = 0;
	framePacingLastLatchedTic = -1;
	framePacingLastLoggedBound = OPENQ4_FRAME_BOUND_UNKNOWN;
	framePacingLastLoggedSampleCount = 0;
}

void idSessionLocal::PrintFramePacingSnapshot( const char *reason ) const {
	idStr reasonText = " snapshot";
	if ( reason != NULL && reason[ 0 ] != '\0' ) {
		reasonText += " ";
		reasonText += reason;
	}

	if ( !framePacingStats.valid ) {
		common->Printf( "Frame pacing%s unavailable: no samples collected yet\n", reasonText.c_str() );
		return;
	}

	Session_PrintFramePacingSummary( framePacingStats, reasonText.c_str() );
}

void idSessionLocal::SampleMultiplayerFramePacing( int frameStartMsec ) {
	latchedTicNumber = com_ticNumber;
	UpdateFramePacingStats( frameStartMsec, 0, 0, 0 );
}

void openQ4_PrintFramePacingSnapshot( const char *reason ) {
	sessLocal.PrintFramePacingSnapshot( reason );
}

void openQ4_RecordMultiplayerFramePacing( int frameStartMsec ) {
	sessLocal.SampleMultiplayerFramePacing( frameStartMsec );
}

void idSessionLocal::UpdateFramePacingStats( int frameStartMsec, int requestedWaitMsec, int actualWaitMsec, int gameTicsToRun ) {
	openq4AsyncTimingStats_t asyncStats;
	openQ4_GetAsyncTimingStats( asyncStats, 120 );

	const int sampleCount = Max( framePacingStats.frameSampleCount, 1 );

	if ( framePacingLastFrameMsec > 0 ) {
		const int frameDeltaMsec = Max( 0, frameStartMsec - framePacingLastFrameMsec );
		framePacingStats.lastFrameMsec = frameDeltaMsec;
		++framePacingStats.frameSampleCount;
		framePacingStats.avgFrameMsec = Session_UpdateMetricAverage( framePacingStats.avgFrameMsec, static_cast<float>( frameDeltaMsec ), framePacingStats.frameSampleCount );
		framePacingStats.avgFrameHz = framePacingStats.avgFrameMsec > 0.0f ? 1000.0f / framePacingStats.avgFrameMsec : 0.0f;
		if ( framePacingStats.frameSampleCount == 1 || frameDeltaMsec < framePacingStats.minFrameMsec ) {
			framePacingStats.minFrameMsec = frameDeltaMsec;
		}
		framePacingStats.maxFrameMsec = Max( framePacingStats.maxFrameMsec, frameDeltaMsec );
		++framePacingStats.frameMsecHistogram[Min( frameDeltaMsec, static_cast<int>( OPENQ4_FRAME_PACING_HISTOGRAM_OVERFLOW ) )];
	}
	framePacingLastFrameMsec = frameStartMsec;

	if ( framePacingLastLatchedTic >= 0 ) {
		const int ticDelta = Max( 0, latchedTicNumber - framePacingLastLatchedTic );
		framePacingStats.lastTicDelta = ticDelta;
		framePacingStats.avgTicsPerFrame = Session_UpdateMetricAverage( framePacingStats.avgTicsPerFrame, static_cast<float>( ticDelta ), sampleCount );
	}
	framePacingLastLatchedTic = latchedTicNumber;

	framePacingStats.multiplayer = idAsyncNetwork::IsActive();
	framePacingStats.lastGameTics = Max( 0, gameTicsToRun );
	framePacingStats.avgGameTicsPerFrame = Session_UpdateMetricAverage( framePacingStats.avgGameTicsPerFrame, static_cast<float>( framePacingStats.lastGameTics ), sampleCount );

	framePacingStats.lastRequestedWaitMsec = Max( 0, requestedWaitMsec );
	framePacingStats.lastWaitMsec = Max( 0, actualWaitMsec );
	framePacingStats.lastWaitOvershootMsec = Max( 0, framePacingStats.lastWaitMsec - framePacingStats.lastRequestedWaitMsec );
	framePacingStats.lastWakeJitterMsec = abs( framePacingStats.lastWaitMsec - framePacingStats.lastRequestedWaitMsec );
	framePacingStats.avgWaitOvershootMsec = Session_UpdateMetricAverage( framePacingStats.avgWaitOvershootMsec, static_cast<float>( framePacingStats.lastWaitOvershootMsec ), sampleCount );
	framePacingStats.avgWakeJitterMsec = Session_UpdateMetricAverage( framePacingStats.avgWakeJitterMsec, static_cast<float>( framePacingStats.lastWakeJitterMsec ), sampleCount );

	framePacingStats.swapInterval = cvarSystem != NULL ? cvarSystem->GetCVarInteger( "r_swapInterval" ) : 0;
	framePacingStats.presentationCap = Session_FindPresentationCap();
	framePacingStats.asyncStats = asyncStats;
	framePacingStats.valid = ( framePacingStats.frameSampleCount > 0 ) || asyncStats.valid;
	framePacingStats.boundMode = Session_ClassifyFramePacing( framePacingStats );

	const bool shouldPeriodicLog = framePacingStats.frameSampleCount >= ( framePacingLastLoggedSampleCount + 120 );
	if ( com_showFramePacing.GetInteger() >= 2
		&& framePacingStats.valid
		&& framePacingStats.frameSampleCount >= 8
		&& ( framePacingStats.boundMode != framePacingLastLoggedBound || shouldPeriodicLog ) ) {
		Session_PrintFramePacingSummary( framePacingStats, "" );
		framePacingLastLoggedBound = framePacingStats.boundMode;
		framePacingLastLoggedSampleCount = framePacingStats.frameSampleCount;
	}
}

#ifndef ID_DEDICATED
static bool Session_IsCompatibleSaveGameVersion( const int version ) {
	return version == SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION_ALT;
}
#endif

static int Session_CountVisibleSmallChars( const char *string ) {
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

static void Session_DrawScaledSmallString( float x, float y, float charWidth, float charHeight,
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

	renderSystem->SetColor( colorWhite );
}

static void Session_DrawOutlinedScaledString( float x, float y, float charWidth, float charHeight,
	const char *string, const idVec4 &mainColor, const idVec4 &outlineColor, const idMaterial *material ) {
	if ( !( string && *string ) ) {
		return;
	}

	const float outlineOffsetX = idMath::ClampFloat( 2.0f, 8.0f, charWidth * 0.10f );
	const float outlineOffsetY = idMath::ClampFloat( 2.0f, 10.0f, charHeight * 0.035f );

	Session_DrawScaledSmallString( x - outlineOffsetX, y, charWidth, charHeight, string, outlineColor, true, material );
	Session_DrawScaledSmallString( x + outlineOffsetX, y, charWidth, charHeight, string, outlineColor, true, material );
	Session_DrawScaledSmallString( x, y - outlineOffsetY, charWidth, charHeight, string, outlineColor, true, material );
	Session_DrawScaledSmallString( x, y + outlineOffsetY, charWidth, charHeight, string, outlineColor, true, material );
	Session_DrawScaledSmallString( x, y, charWidth, charHeight, string, mainColor, true, material );
}

static bool Session_FileExistsInSearchPaths( const char *path ) {
	if ( path == NULL || path[0] == '\0' ) {
		return false;
	}

	return ( fileSystem->ReadFile( path, NULL, NULL ) != -1 );
}

static void Session_NormalizeMapDeclPath( const char *mapPath, idStr &normalizedPath ) {
	normalizedPath = ( mapPath != NULL ) ? mapPath : "";
	normalizedPath.BackSlashesToSlashes();
	normalizedPath.Strip( ' ' );
	normalizedPath.Strip( '\t' );
	normalizedPath.StripTrailingWhitespace();
	normalizedPath.StripQuotes();
	normalizedPath.StripFileExtension();

	if ( !idStr::Icmpn( normalizedPath.c_str(), "maps/", 5 ) ) {
		normalizedPath = normalizedPath.c_str() + 5;
	}
}

#ifndef ID_DEDICATED
static bool Session_IsSafeNormalizedMapPath( const idStr &path ) {
	if ( path.IsEmpty() || path.Length() >= MAX_STRING_CHARS || path[0] == '/' || path[0] == '\\' ) {
		return false;
	}
	if ( idStr::FindText( path.c_str(), ".." ) >= 0 ) {
		return false;
	}

	for ( int i = 0; i < path.Length(); i++ ) {
		const unsigned char ch = static_cast<unsigned char>( path[i] );
		if ( ch <= ' ' || ch >= 128 || ch == '\\' || ch == ':' || ch == '"' || ch == '<' || ch == '>' || ch == '|' ) {
			return false;
		}
	}
	return true;
}
#endif

static bool Session_IsMapFilterWhitespace( const char ch ) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void Session_NormalizeEntityFilterToken( const char *entityFilter, idStr &normalizedFilter ) {
	normalizedFilter = ( entityFilter != NULL ) ? entityFilter : "";
	normalizedFilter.Strip( ' ' );
	normalizedFilter.Strip( '\t' );
	normalizedFilter.StripTrailingWhitespace();
	normalizedFilter.StripQuotes();
}

static bool Session_ShouldDefaultFirstEntityFilter( const idStr &normalizedPath );

static void Session_NormalizeMapPathAndEntityFilter( const char *mapPath, const char *entityFilter,
	idStr &normalizedMapPath, idStr &normalizedEntityFilter, const bool defaultFirstEntityFilter = true ) {
	idStr mapToken = ( mapPath != NULL ) ? mapPath : "";
	mapToken.BackSlashesToSlashes();
	mapToken.Strip( ' ' );
	mapToken.Strip( '\t' );
	mapToken.StripTrailingWhitespace();
	mapToken.StripQuotes();

	Session_NormalizeEntityFilterToken( entityFilter, normalizedEntityFilter );

	int split = -1;
	for ( int i = mapToken.Length() - 1; i >= 0; --i ) {
		if ( Session_IsMapFilterWhitespace( mapToken[ i ] ) ) {
			split = i;
			break;
		}
	}

	if ( split > 0 ) {
		idStr mapPart = mapToken.Left( split );
		idStr filterPart = mapToken.Right( mapToken.Length() - split - 1 );
		idStr normalizedFilterPart;
		mapPart.Strip( ' ' );
		mapPart.Strip( '\t' );
		mapPart.StripTrailingWhitespace();
		mapPart.StripQuotes();
		Session_NormalizeEntityFilterToken( filterPart.c_str(), normalizedFilterPart );

		if ( mapPart.Length() > 0 && normalizedFilterPart.Length() > 0 ) {
			mapToken = mapPart;
			if ( normalizedEntityFilter.Length() == 0 ) {
				normalizedEntityFilter = normalizedFilterPart;
			}
		}
	}

	Session_NormalizeMapDeclPath( mapToken.c_str(), normalizedMapPath );
	if ( defaultFirstEntityFilter && normalizedEntityFilter.Length() == 0 && Session_ShouldDefaultFirstEntityFilter( normalizedMapPath ) ) {
		normalizedEntityFilter = "first";
	}
}

static bool Session_GetMapDeclDictForNormalizedPath( const idStr &normalizedPath, idDict &outMapDecl ) {
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	const idDecl *mapDecl = declManager->FindType( DECL_MAPDEF, normalizedPath.c_str(), false );
	const idDeclEntityDef *mapDef = static_cast<const idDeclEntityDef *>( mapDecl );
	if ( mapDef != NULL ) {
		outMapDecl = mapDef->dict;
		outMapDecl.Set( "path", mapDef->GetName() );
		return true;
	}

	const int numMaps = fileSystem->GetNumMaps();
	for ( int i = 0; i < numMaps; ++i ) {
		const idDict *candidate = fileSystem->GetMapDecl( i );
		if ( candidate == NULL ) {
			continue;
		}

		idStr candidatePath;
		Session_NormalizeMapDeclPath( candidate->GetString( "path" ), candidatePath );
		if ( candidatePath.IsEmpty() ) {
			continue;
		}

		if ( !fileSystem->FilenameCompare( normalizedPath.c_str(), candidatePath.c_str() ) ) {
			outMapDecl = *candidate;
			return true;
		}
	}

	return false;
}

static bool Session_MapDefDeclExistsWithoutParsing( const idStr &normalizedPath ) {
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	const int numMapDefs = declManager->GetNumDecls( DECL_MAPDEF );
	for ( int i = 0; i < numMapDefs; ++i ) {
		const idDecl *mapDecl = declManager->DeclByIndex( DECL_MAPDEF, i, false );
		if ( mapDecl == NULL ) {
			continue;
		}

		idStr mapDeclPath;
		Session_NormalizeMapDeclPath( mapDecl->GetName(), mapDeclPath );
		if ( mapDeclPath.IsEmpty() ) {
			continue;
		}

		if ( !fileSystem->FilenameCompare( normalizedPath.c_str(), mapDeclPath.c_str() ) ) {
			return true;
		}
	}

	return false;
}

static bool Session_ShouldDefaultFirstEntityFilter( const idStr &normalizedPath ) {
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	if ( Session_MapDefDeclExistsWithoutParsing( normalizedPath ) ) {
		return false;
	}

	idStr firstSegmentPath = normalizedPath;
	firstSegmentPath += "_first";
	return Session_MapDefDeclExistsWithoutParsing( firstSegmentPath );
}

static void Session_ApplyEntityFilterToServerInfo( idDict &serverInfo, const char *entityFilter ) {
	idStr normalizedFilter;
	Session_NormalizeEntityFilterToken( entityFilter, normalizedFilter );
	cvarSystem->SetCVarString( "si_entityFilter", normalizedFilter.c_str() );

	if ( normalizedFilter.Length() > 0 ) {
		serverInfo.Set( "si_entityFilter", normalizedFilter.c_str() );
	} else {
		serverInfo.Delete( "si_entityFilter" );
	}
}

static const char *Session_GetEntityFilterArg( const idCmdArgs &args ) {
	return ( args.Argc() > 2 ) ? args.Argv( 2 ) : "";
}

static bool Session_GetMapDeclDict( const char *mapPath, const char *entityFilter, idDict &outMapDecl ) {
	outMapDecl.Clear();

	idStr normalizedPath;
	idStr normalizedEntityFilter;
	Session_NormalizeMapPathAndEntityFilter( mapPath, entityFilter, normalizedPath, normalizedEntityFilter );
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	if ( normalizedEntityFilter.Length() > 0 ) {
		idStr filteredPath = normalizedPath;
		filteredPath += "_";
		filteredPath += normalizedEntityFilter;
		filteredPath.Strip( ' ' );
		filteredPath.Strip( '\t' );
		filteredPath.StripTrailingWhitespace();
		filteredPath.StripQuotes();
		filteredPath.BackSlashesToSlashes();
		filteredPath.Replace( " ", "_" );
		if ( Session_GetMapDeclDictForNormalizedPath( filteredPath, outMapDecl ) ) {
			return true;
		}
	}

	if ( Session_GetMapDeclDictForNormalizedPath( normalizedPath, outMapDecl ) ) {
		return true;
	}

	if ( entityFilter == NULL || entityFilter[0] == '\0' ) {
		idStr firstSegmentPath = normalizedPath;
		firstSegmentPath += "_first";
		if ( Session_GetMapDeclDictForNormalizedPath( firstSegmentPath, outMapDecl ) ) {
			return true;
		}
	}

	return false;
}

#ifndef ID_DEDICATED
static bool Session_SaveGameMapExists( const idStr &normalizedPath, const idStr &normalizedEntityFilter ) {
	if ( !Session_IsSafeNormalizedMapPath( normalizedPath ) ) {
		return false;
	}

	idStr mapFile = "maps/";
	mapFile += normalizedPath;
	mapFile.SetFileExtension( ".map" );
	if ( Session_FileExistsInSearchPaths( mapFile.c_str() ) ) {
		return true;
	}

	// Segmented maps may resolve through an entity-filter-specific mapDef.
	idDict mapDecl;
	if ( Session_GetMapDeclDict( normalizedPath.c_str(), normalizedEntityFilter.c_str(), mapDecl ) ) {
		idStr declaredPath;
		Session_NormalizeMapDeclPath( mapDecl.GetString( "path", normalizedPath.c_str() ), declaredPath );
		if ( Session_IsSafeNormalizedMapPath( declaredPath ) ) {
			mapFile = "maps/";
			mapFile += declaredPath;
			mapFile.SetFileExtension( ".map" );
			if ( Session_FileExistsInSearchPaths( mapFile.c_str() ) ) {
				return true;
			}
		}
	}

	return false;
}
#endif

static const char *SESSION_SAVE_DESC_AUTOSAVE_LABEL = "#str_107240";
static const char *SESSION_SAVE_DESC_QUICKSAVE_LABEL = "#str_107241";
static const char *SESSION_SAVE_DESC_CHECKPOINT_LABEL = "#str_107656";

static bool Session_GetTrailingDigit( const idStr &value, int &digit ) {
	if ( value.Length() <= 0 ) {
		return false;
	}

	const char ch = value[ value.Length() - 1 ];
	if ( ch < '0' || ch > '9' ) {
		return false;
	}

	digit = ch - '0';
	return true;
}

#ifndef ID_DEDICATED
static void Session_GenerateSaveFileName( idSessionLocal &session, idStr &saveName, saveType_t saveType ) {
	switch ( saveType ) {
		case ST_AUTO: {
			const char *mapName = session.mapSpawnData.serverInfo.GetString( "si_map" );
			const char *entityFilter = session.mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
			if ( entityFilter[0] != '\0' ) {
				saveName = va( "Autosave %s %s", mapName, entityFilter );
			} else {
				saveName = va( "Autosave %s", mapName );
			}
			break;
		}

		case ST_CHECKPOINT: {
			const int checkpointSlot = ( static_cast<unsigned int>( static_cast<unsigned char>( session.lastCheckPoint ) ) - 1 ) & 1;
			session.lastCheckPoint = checkpointSlot;
			saveName = va( "Checkpoint%d", checkpointSlot );
			break;
		}

		case ST_QUICK: {
			idStr currentQuickSave = idSessionLocal::com_lastQuicksave.GetString();
			if ( currentQuickSave.IsEmpty() ) {
				currentQuickSave = "Quicksave0";
			}

			int currentSlot = 0;
			const bool hasSlotSuffix = Session_GetTrailingDigit( currentQuickSave, currentSlot );
			const int quicksaveSlot = ( currentSlot + 1 ) & 3;

			if ( hasSlotSuffix ) {
				saveName = currentQuickSave.Left( currentQuickSave.Length() - 1 );
			} else {
				saveName = currentQuickSave;
			}
			if ( saveName.IsEmpty() ) {
				saveName = "Quicksave";
			}
			saveName += va( "%d", quicksaveSlot );
			idSessionLocal::com_lastQuicksave.SetString( saveName );
			break;
		}

		case ST_REGULAR:
		default:
			saveName = "SaveGame";
			break;
	}
}

static const char *Session_GetGeneratedSaveLabel( saveType_t saveType ) {
	switch ( saveType ) {
		case ST_AUTO:
			return SESSION_SAVE_DESC_AUTOSAVE_LABEL;
		case ST_CHECKPOINT:
			return SESSION_SAVE_DESC_CHECKPOINT_LABEL;
		case ST_QUICK:
		default:
			return SESSION_SAVE_DESC_QUICKSAVE_LABEL;
	}
}

static void Session_EscapeSaveDescription( idStr &description ) {
	description.Replace( "\\", "\\\\" );
	description.Replace( "\"", "\\\"" );
}
#endif

static bool Session_ResolveImageFilePath( const idStr &imagePath, idStr &resolvedPath ) {
	if ( imagePath.Length() <= 0 ) {
		return false;
	}

	idStr canonicalPath = imagePath;
	canonicalPath.BackSlashesToSlashes();

	idStr basePath = canonicalPath;
	idStr requestedExtension;
	basePath.ExtractFileExtension( requestedExtension );
	requestedExtension.ToLower();

	if ( requestedExtension.Length() > 0 ) {
		if ( Session_FileExistsInSearchPaths( canonicalPath.c_str() ) ) {
			resolvedPath = canonicalPath;
			return true;
		}
		basePath.StripFileExtension();
	}

	static const char *supportedExtensions[] = { "tga", "dds", "jpg", NULL };

	if ( requestedExtension.Length() > 0 ) {
		idStr requestedCandidate = basePath;
		requestedCandidate += ".";
		requestedCandidate += requestedExtension;
		if ( Session_FileExistsInSearchPaths( requestedCandidate.c_str() ) ) {
			resolvedPath = requestedCandidate;
			return true;
		}
	}

	for ( int i = 0; supportedExtensions[ i ] != NULL; i++ ) {
		if ( requestedExtension.Length() > 0 && requestedExtension == supportedExtensions[ i ] ) {
			continue;
		}

		idStr candidate = basePath;
		candidate += ".";
		candidate += supportedExtensions[ i ];
		if ( Session_FileExistsInSearchPaths( candidate.c_str() ) ) {
			resolvedPath = candidate;
			return true;
		}
	}

	return false;
}

static bool Session_ResolveSiblingImageFilePath( const idStr &resolvedBasePath, const char *suffix, idStr &resolvedPath ) {
	if ( resolvedBasePath.Length() <= 0 || suffix == NULL || suffix[ 0 ] == '\0' ) {
		return false;
	}

	idStr baseNoExt = resolvedBasePath;
	idStr preferredExtension;
	baseNoExt.ExtractFileExtension( preferredExtension );
	preferredExtension.ToLower();
	baseNoExt.StripFileExtension();
	baseNoExt += suffix;

	if ( preferredExtension.Length() > 0 ) {
		idStr preferredCandidate = baseNoExt;
		preferredCandidate += ".";
		preferredCandidate += preferredExtension;
		if ( Session_FileExistsInSearchPaths( preferredCandidate.c_str() ) ) {
			resolvedPath = preferredCandidate;
			return true;
		}
	}

	static const char *supportedExtensions[] = { "tga", "dds", "jpg", NULL };
	for ( int i = 0; supportedExtensions[ i ] != NULL; i++ ) {
		if ( preferredExtension.Length() > 0 && preferredExtension == supportedExtensions[ i ] ) {
			continue;
		}

		idStr candidate = baseNoExt;
		candidate += ".";
		candidate += supportedExtensions[ i ];
		if ( Session_FileExistsInSearchPaths( candidate.c_str() ) ) {
			resolvedPath = candidate;
			return true;
		}
	}

	return false;
}

static bool Session_LoadResolvedImageRGBA( const idStr &resolvedPath, byte *&pic, int &width, int &height ) {
	pic = NULL;
	width = 0;
	height = 0;

	R_LoadImage( resolvedPath.c_str(), &pic, &width, &height, NULL, false );
	return ( pic != NULL && width > 0 && height > 0 );
}

static byte *Session_CreateResampledImageRegion( const byte *src, int srcWidth, int srcHeight,
	int srcX, int srcY, int regionWidth, int regionHeight, int destWidth, int destHeight ) {
	if ( src == NULL || srcWidth <= 0 || srcHeight <= 0 || destWidth <= 0 || destHeight <= 0 ) {
		return NULL;
	}

	srcX = idMath::ClampInt( 0, srcWidth - 1, srcX );
	srcY = idMath::ClampInt( 0, srcHeight - 1, srcY );
	regionWidth = idMath::ClampInt( 1, srcWidth - srcX, regionWidth );
	regionHeight = idMath::ClampInt( 1, srcHeight - srcY, regionHeight );

	byte *cropped = (byte *)R_StaticAlloc( regionWidth * regionHeight * 4 );
	for ( int y = 0; y < regionHeight; y++ ) {
		const byte *srcRow = src + ( ( srcY + y ) * srcWidth + srcX ) * 4;
		byte *dstRow = cropped + y * regionWidth * 4;
		memcpy( dstRow, srcRow, regionWidth * 4 );
	}

	if ( regionWidth == destWidth && regionHeight == destHeight ) {
		return cropped;
	}

	byte *resampled = R_ResampleTexture( cropped, regionWidth, regionHeight, destWidth, destHeight );
	R_StaticFree( cropped );
	return resampled;
}

static void Session_BlitRGBA( byte *dest, int destWidth, int destHeight, int destX, int destY,
	const byte *src, int srcWidth, int srcHeight ) {
	if ( dest == NULL || src == NULL || destWidth <= 0 || destHeight <= 0 || srcWidth <= 0 || srcHeight <= 0 ) {
		return;
	}

	if ( destX < 0 || destY < 0 || destX + srcWidth > destWidth || destY + srcHeight > destHeight ) {
		return;
	}

	for ( int y = 0; y < srcHeight; y++ ) {
		byte *destRow = dest + ( ( destY + y ) * destWidth + destX ) * 4;
		const byte *srcRow = src + y * srcWidth * 4;
		memcpy( destRow, srcRow, srcWidth * 4 );
	}
}

static bool openQ4_IsSingleplayerGameType( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	return !( gameType && gameType[ 0 ] && idStr::Icmp( gameType, "singleplayer" ) != 0 );
}

static idEntity *openQ4_FindSpawnedEntityByBaseClass( const char *className ) {
	if ( !gameEdit || !className || !className[ 0 ] ) {
		return NULL;
	}

	for ( idEntity *ent = gameEdit->GetFirstSpawnedEntity(); ent; ent = gameEdit->GetNextSpawnedEntity( ent ) ) {
		if ( gameEdit->EntityIsDerivedFrom( ent, className ) ) {
			return ent;
		}
	}

	return NULL;
}

#ifndef ID_DEDICATED
static void Session_IAmTheDuke_f( const idCmdArgs &args ) {
	(void)args;
	sessLocal.ToggleIAmTheDuke();
}
#endif

static void Session_ReplicateColumnIntoRect( byte *dest, int destWidth, int destHeight,
	int sourceX, int rectX, int rectY, int rectWidth, int rectHeight ) {
	if ( dest == NULL || destWidth <= 0 || destHeight <= 0 || rectWidth <= 0 || rectHeight <= 0 ) {
		return;
	}

	sourceX = idMath::ClampInt( 0, destWidth - 1, sourceX );
	rectX = idMath::ClampInt( 0, destWidth, rectX );
	rectY = idMath::ClampInt( 0, destHeight, rectY );
	rectWidth = idMath::ClampInt( 0, destWidth - rectX, rectWidth );
	rectHeight = idMath::ClampInt( 0, destHeight - rectY, rectHeight );

	for ( int y = 0; y < rectHeight; y++ ) {
		const byte *sourcePixel = dest + ( ( rectY + y ) * destWidth + sourceX ) * 4;
		byte *destPixel = dest + ( ( rectY + y ) * destWidth + rectX ) * 4;
		for ( int x = 0; x < rectWidth; x++, destPixel += 4 ) {
			destPixel[ 0 ] = sourcePixel[ 0 ];
			destPixel[ 1 ] = sourcePixel[ 1 ];
			destPixel[ 2 ] = sourcePixel[ 2 ];
			destPixel[ 3 ] = sourcePixel[ 3 ];
		}
	}
}

static void Session_ReplicateRowIntoRect( byte *dest, int destWidth, int destHeight,
	int sourceY, int rectX, int rectY, int rectWidth, int rectHeight ) {
	if ( dest == NULL || destWidth <= 0 || destHeight <= 0 || rectWidth <= 0 || rectHeight <= 0 ) {
		return;
	}

	sourceY = idMath::ClampInt( 0, destHeight - 1, sourceY );
	rectX = idMath::ClampInt( 0, destWidth, rectX );
	rectY = idMath::ClampInt( 0, destHeight, rectY );
	rectWidth = idMath::ClampInt( 0, destWidth - rectX, rectWidth );
	rectHeight = idMath::ClampInt( 0, destHeight - rectY, rectHeight );

	for ( int y = 0; y < rectHeight; y++ ) {
		byte *destRow = dest + ( ( rectY + y ) * destWidth + rectX ) * 4;
		const byte *sourceRow = dest + ( sourceY * destWidth + rectX ) * 4;
		memcpy( destRow, sourceRow, rectWidth * 4 );
	}
}

static bool Session_GetLoadingCanvasExpansion( float &windowAspect, bool &expandHorizontally, bool &expandVertically ) {
	windowAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	expandHorizontally = false;
	expandVertically = false;

	if ( !cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		return false;
	}

	float viewportWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	float viewportHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		viewportWidth = static_cast<float>( engineWindowState.vidWidth );
		viewportHeight = static_cast<float>( engineWindowState.vidHeight );
	}

	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		return false;
	}

	const float targetAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	const float aspectEpsilon = 0.0001f;

	windowAspect = viewportWidth / viewportHeight;
	if ( windowAspect > targetAspect + aspectEpsilon ) {
		expandHorizontally = true;
	} else if ( windowAspect + aspectEpsilon < targetAspect ) {
		expandVertically = true;
	}

	return expandHorizontally || expandVertically;
}

static idStr Session_MakeExpandedLoadingBackgroundPath( const char *mapName, int width, int height ) {
	idStr safeName = mapName;
	safeName.BackSlashesToSlashes();
	safeName.StripPath();
	safeName.StripFileExtension();
	if ( safeName.Length() <= 0 ) {
		safeName = "generated";
	}

	return va( "guis/assets/generated/loadscreens/%s_%dx%d.tga", safeName.c_str(), width, height );
}

static bool Session_PrepareExpandedLoadingBackground( const idStr &backgroundPath, const char *mapName, idStr &generatedPath ) {
	float windowAspect = 0.0f;
	bool expandHorizontally = false;
	bool expandVertically = false;
	if ( !Session_GetLoadingCanvasExpansion( windowAspect, expandHorizontally, expandVertically ) ) {
		return false;
	}

	// Never use a shared, predictable staging name. If the platform cannot
	// provide a secure nonce, keep the stock source levelshot instead of risking
	// another client truncating this client's in-progress publication.
	uint64 stagingNonce[2] = { 0, 0 };
	if ( !Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) ) ) {
		common->Warning( "Could not create a secure expanded-loadscreen staging path for '%s'; using the source levelshot",
			backgroundPath.c_str() );
		return false;
	}

	idStr resolvedCenterPath;
	if ( !Session_ResolveImageFilePath( backgroundPath, resolvedCenterPath ) ) {
		return false;
	}

	idStr resolvedLeftPath;
	idStr resolvedRightPath;
	idStr resolvedTopPath;
	idStr resolvedBottomPath;

	bool hasLeft = false;
	bool hasRight = false;
	bool hasTop = false;
	bool hasBottom = false;
	if ( expandHorizontally ) {
		hasLeft = Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_left", resolvedLeftPath );
		hasRight = Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_right", resolvedRightPath );
	}
	if ( expandVertically ) {
		hasTop = Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_top", resolvedTopPath );
		hasBottom = Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_bottom", resolvedBottomPath );
	}

	byte *centerPic = NULL;
	int centerWidth = 0;
	int centerHeight = 0;
	if ( !Session_LoadResolvedImageRGBA( resolvedCenterPath, centerPic, centerWidth, centerHeight ) ) {
		return false;
	}

	byte *leftPic = NULL;
	byte *rightPic = NULL;
	byte *topPic = NULL;
	byte *bottomPic = NULL;
	int leftWidth = 0, leftHeight = 0;
	int rightWidth = 0, rightHeight = 0;
	int topWidth = 0, topHeight = 0;
	int bottomWidth = 0, bottomHeight = 0;

	if ( hasLeft && !Session_LoadResolvedImageRGBA( resolvedLeftPath, leftPic, leftWidth, leftHeight ) ) {
		hasLeft = false;
	}
	if ( hasRight && !Session_LoadResolvedImageRGBA( resolvedRightPath, rightPic, rightWidth, rightHeight ) ) {
		hasRight = false;
	}
	if ( hasTop && !Session_LoadResolvedImageRGBA( resolvedTopPath, topPic, topWidth, topHeight ) ) {
		hasTop = false;
	}
	if ( hasBottom && !Session_LoadResolvedImageRGBA( resolvedBottomPath, bottomPic, bottomWidth, bottomHeight ) ) {
		hasBottom = false;
	}

	// Levelshots are authored as square textures, but the stock loading GUI first stretches
	// them into the 4:3 virtual canvas and only then uniformly scales that canvas to the
	// current screen. Compose the expanded background in that same display space.
	const int centerDisplayHeight = centerHeight;
	const int centerDisplayWidth = Max( 1, idMath::Ftoi( centerDisplayHeight * ( static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT ) ) + 0.5f ) );

	int outputWidth = centerDisplayWidth;
	int outputHeight = centerDisplayHeight;
	int centerX = 0;
	int centerY = 0;

	if ( expandHorizontally ) {
		outputWidth = Max( centerDisplayWidth, idMath::Ftoi( windowAspect * centerDisplayHeight + 0.5f ) );
		centerX = ( outputWidth - centerDisplayWidth ) / 2;
	} else {
		outputHeight = Max( centerDisplayHeight, idMath::Ftoi( centerDisplayWidth / windowAspect + 0.5f ) );
		centerY = ( outputHeight - centerDisplayHeight ) / 2;
	}

	idTempArray<byte> composite( outputWidth * outputHeight * 4 );
	memset( composite.Ptr(), 0, outputWidth * outputHeight * 4 );

	byte *centerResampled = Session_CreateResampledImageRegion( centerPic, centerWidth, centerHeight, 0, 0, centerWidth, centerHeight, centerDisplayWidth, centerDisplayHeight );
	if ( centerResampled == NULL ) {
		R_StaticFree( centerPic );
		if ( leftPic ) {
			R_StaticFree( leftPic );
		}
		if ( rightPic ) {
			R_StaticFree( rightPic );
		}
		if ( topPic ) {
			R_StaticFree( topPic );
		}
		if ( bottomPic ) {
			R_StaticFree( bottomPic );
		}
		return false;
	}

	Session_BlitRGBA( composite.Ptr(), outputWidth, outputHeight, centerX, centerY, centerResampled, centerDisplayWidth, centerDisplayHeight );
	R_StaticFree( centerResampled );

	if ( expandHorizontally ) {
		const int leftWidthPixels = centerX;
		const int rightWidthPixels = outputWidth - centerX - centerDisplayWidth;

		if ( leftWidthPixels > 0 ) {
			Session_ReplicateColumnIntoRect( composite.Ptr(), outputWidth, outputHeight, centerX, 0, 0, leftWidthPixels, outputHeight );
			if ( hasLeft && leftPic != NULL ) {
				const int cropWidth = Max( 1, idMath::Ftoi( leftWidth * ( static_cast<float>( leftWidthPixels ) / static_cast<float>( centerDisplayWidth ) ) + 0.5f ) );
				byte *leftResampled = Session_CreateResampledImageRegion( leftPic, leftWidth, leftHeight,
					Max( 0, leftWidth - cropWidth ), 0, cropWidth, leftHeight, leftWidthPixels, outputHeight );
				if ( leftResampled != NULL ) {
					Session_BlitRGBA( composite.Ptr(), outputWidth, outputHeight, 0, 0, leftResampled, leftWidthPixels, outputHeight );
					R_StaticFree( leftResampled );
				}
			}
		}

		if ( rightWidthPixels > 0 ) {
			Session_ReplicateColumnIntoRect( composite.Ptr(), outputWidth, outputHeight, centerX + centerDisplayWidth - 1,
				centerX + centerDisplayWidth, 0, rightWidthPixels, outputHeight );
			if ( hasRight && rightPic != NULL ) {
				const int cropWidth = Max( 1, idMath::Ftoi( rightWidth * ( static_cast<float>( rightWidthPixels ) / static_cast<float>( centerDisplayWidth ) ) + 0.5f ) );
				byte *rightResampled = Session_CreateResampledImageRegion( rightPic, rightWidth, rightHeight,
					0, 0, cropWidth, rightHeight, rightWidthPixels, outputHeight );
				if ( rightResampled != NULL ) {
					Session_BlitRGBA( composite.Ptr(), outputWidth, outputHeight, centerX + centerDisplayWidth, 0,
						rightResampled, rightWidthPixels, outputHeight );
					R_StaticFree( rightResampled );
				}
			}
		}
	} else {
		const int topHeightPixels = centerY;
		const int bottomHeightPixels = outputHeight - centerY - centerDisplayHeight;

		if ( topHeightPixels > 0 ) {
			Session_ReplicateRowIntoRect( composite.Ptr(), outputWidth, outputHeight, centerY, 0, 0, outputWidth, topHeightPixels );
			if ( hasTop && topPic != NULL ) {
				const int cropHeight = Max( 1, idMath::Ftoi( topHeight * ( static_cast<float>( topHeightPixels ) / static_cast<float>( centerDisplayHeight ) ) + 0.5f ) );
				byte *topResampled = Session_CreateResampledImageRegion( topPic, topWidth, topHeight,
					0, Max( 0, topHeight - cropHeight ), topWidth, cropHeight, outputWidth, topHeightPixels );
				if ( topResampled != NULL ) {
					Session_BlitRGBA( composite.Ptr(), outputWidth, outputHeight, 0, 0, topResampled, outputWidth, topHeightPixels );
					R_StaticFree( topResampled );
				}
			}
		}

		if ( bottomHeightPixels > 0 ) {
			Session_ReplicateRowIntoRect( composite.Ptr(), outputWidth, outputHeight, centerY + centerDisplayHeight - 1,
				0, centerY + centerDisplayHeight, outputWidth, bottomHeightPixels );
			if ( hasBottom && bottomPic != NULL ) {
				const int cropHeight = Max( 1, idMath::Ftoi( bottomHeight * ( static_cast<float>( bottomHeightPixels ) / static_cast<float>( centerDisplayHeight ) ) + 0.5f ) );
				byte *bottomResampled = Session_CreateResampledImageRegion( bottomPic, bottomWidth, bottomHeight,
					0, 0, bottomWidth, cropHeight, outputWidth, bottomHeightPixels );
				if ( bottomResampled != NULL ) {
					Session_BlitRGBA( composite.Ptr(), outputWidth, outputHeight, 0, centerY + centerDisplayHeight,
						bottomResampled, outputWidth, bottomHeightPixels );
					R_StaticFree( bottomResampled );
				}
			}
		}
	}

	generatedPath = Session_MakeExpandedLoadingBackgroundPath( mapName, outputWidth, outputHeight );
	// Multiple isolated clients may generate the same resolution at once. Give
	// every publication a CSPRNG-backed staging qpath so one client cannot
	// truncate another client's in-progress TGA before either atomic rename.
	// Stage below a short root-level namespace rather than beside the final
	// loadscreen. The source and destination still share fs_savepath (and thus a
	// filesystem), preserving atomic cross-directory rename while leaving enough
	// path budget for legacy Windows stdio under deeply nested save roots. The
	// 128-bit nonce is the complete collision identity; no predictable shared
	// staging name or process-local sequence is needed.
	const idStr stagingPath = va( "_oq4/%016llx%016llx.tmp",
		static_cast<unsigned long long>( stagingNonce[0] ),
		static_cast<unsigned long long>( stagingNonce[1] ) );
	bool published = R_WriteTGA( stagingPath.c_str(), composite.Ptr(), outputWidth, outputHeight, false, "fs_savepath" );
	if ( published ) {
		published = fileSystem->PromoteFile( stagingPath.c_str(), generatedPath.c_str(), "fs_savepath" );
	}
	if ( !published ) {
		// A short or interrupted direct write used to leave a truncated final TGA.
		// Loading that generated file raises a fatal "incomplete file" error on
		// the next material lookup. Keep the old final intact and fall back to the
		// source levelshot whenever publication cannot complete (GitHub issue #102).
		fileSystem->RemoveFileChecked( stagingPath.c_str(), "fs_savepath" );
		common->Warning( "Could not publish expanded loading background '%s'; using the source levelshot",
			generatedPath.c_str() );
	} else if ( !Session_FileExistsInSearchPaths( generatedPath.c_str() ) ) {
		// A pure server deliberately excludes loose image files even when this
		// client just authored them under fs_savepath.  Publication alone is not
		// permission to consume the file: require the active VFS policy to expose
		// it before handing its name to the loading GUI/material system.  Keeping
		// published=false leaves the caller's stock levelshot selected.
		common->DPrintf( "Expanded loading background '%s' is unavailable through the active VFS; using the source levelshot\n",
			generatedPath.c_str() );
		published = false;
	} else {
		const idMaterial *generatedMaterial = declManager->FindMaterial( generatedPath.c_str() );
		if ( generatedMaterial == NULL || generatedMaterial->TestMaterialFlag( MF_DEFAULTED ) ) {
			common->DPrintf( "Expanded loading background '%s' has no usable material; using the source levelshot\n",
				generatedPath.c_str() );
			published = false;
		}
	}

	R_StaticFree( centerPic );
	if ( leftPic ) {
		R_StaticFree( leftPic );
	}
	if ( rightPic ) {
		R_StaticFree( rightPic );
	}
	if ( topPic ) {
		R_StaticFree( topPic );
	}
	if ( bottomPic ) {
		R_StaticFree( bottomPic );
	}

	return published;
}

void idSessionLocal::SetMainMenuBackgroundMontageGuiVars( void ) {
	static const char *menuBackgrounds[] = {
		"gfx/guis/mainmenu/level_mcc.dds",
		"gfx/guis/mainmenu/level_convoy1.dds",
		"gfx/guis/mainmenu/level_core1.dds",
		"gfx/guis/mainmenu/level_process1_first.dds",
		"gfx/guis/mainmenu/level_tram1.dds"
	};
	static const int numMenuBackgrounds = sizeof( menuBackgrounds ) / sizeof( menuBackgrounds[ 0 ] );
	static const char *menuBackgroundMask = "gfx/guis/mainmenu/level_edge_fade";
	static const char *fallbackBackground = "gfx/guis/loadscreens/generic";

	if ( guiMainMenu == NULL ) {
		return;
	}

	for ( int i = 0; i < numMenuBackgrounds; i++ ) {
		idStr resolvedPath;
		if ( !Session_ResolveImageFilePath( menuBackgrounds[ i ], resolvedPath ) ) {
			if ( !Session_ResolveImageFilePath( fallbackBackground, resolvedPath ) ) {
				resolvedPath = fallbackBackground;
			}
		}

		guiMainMenu->SetStateString( va( "menu_bg_%d", i + 1 ), resolvedPath.c_str() );

		idMaterial *material = const_cast<idMaterial *>( declManager->FindMaterial( resolvedPath.c_str() ) );
		if ( material != NULL ) {
			material->EnsureNotPurged();
			material->SetSort( SS_GUI );
		}
		renderSystem->PreloadImage( resolvedPath.c_str() );
	}

	idMaterial *maskMaterial = const_cast<idMaterial *>( declManager->FindMaterial( menuBackgroundMask ) );
	if ( maskMaterial != NULL ) {
		maskMaterial->EnsureNotPurged();
		maskMaterial->SetSort( SS_GUI );
	}

	guiMainMenu->SetStateInt( "menu_bg_count", numMenuBackgrounds );
}

void idSessionLocal::PrimeMainMenuGuiResources( void ) {
	static const char *fallbackLevelshot = "gfx/guis/loadscreens/generic";

	if ( guiMainMenu == NULL ) {
		return;
	}

	SetMainMenuBackgroundMontageGuiVars();
	guiMainMenu->SetStateString( "browser_levelshot", fallbackLevelshot );

	idMaterial *fallbackMaterial = const_cast<idMaterial *>( declManager->FindMaterial( fallbackLevelshot ) );
	if ( fallbackMaterial != NULL ) {
		fallbackMaterial->EnsureNotPurged();
		fallbackMaterial->SetSort( SS_GUI );
	}
	renderSystem->PreloadImage( fallbackLevelshot );

	// The GUI buffers this command when it activates. Resolve the shader during
	// startup/level load so the first in-game menu never parses it on demand.
	declManager->FindSound( "main_menu_gameplay" );
}

static void Session_DrawFallbackLoadingScreen() {
	static const idVec4 loadingTextColor( 0.94f, 0.62f, 0.05f, 1.0f );

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
		const float scaleX = viewportWidth / virtualWidth;
		const float scaleY = viewportHeight / virtualHeight;
		const float uniformPhysicalScale = ( scaleX < scaleY ) ? scaleX : scaleY;
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

	const idMaterial *splashMaterial = declManager->FindMaterial( "gfx/splashScreen", false );
	if ( splashMaterial ) {
		renderSystem->SetColor( colorWhite );
		renderSystem->DrawStretchPic( splashX, splashY, splashW, splashH, 0, 0, 1, 1, splashMaterial );
	}

	renderSystem->SetColor( colorWhite );
}

static void Session_DrawLevelshotBounds() {
	if ( !com_showLevelshotBounds.GetBool() ) {
		return;
	}

	const float screenW = static_cast<float>( renderSystem->GetScreenWidth() );
	const float screenH = static_cast<float>( renderSystem->GetScreenHeight() );
	if ( screenW <= 0.0f || screenH <= 0.0f ) {
		return;
	}

	float guideX = 0.0f;
	float guideY = 0.0f;
	float guideW = screenW;
	float guideH = screenH;

	const float targetAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	const float screenAspect = screenW / screenH;
	if ( screenAspect > targetAspect ) {
		guideW = screenH * targetAspect;
		guideX = ( screenW - guideW ) * 0.5f;
	} else if ( screenAspect < targetAspect ) {
		guideH = screenW / targetAspect;
		guideY = ( screenH - guideH ) * 0.5f;
	}

	const float minDimension = ( screenW < screenH ) ? screenW : screenH;
	const float lineThickness = Max( 1.0f, minDimension / 540.0f );
	const idMaterial *white = declManager->FindMaterial( "_white" );
	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();

	renderSystem->SetUseUIViewportFor2D( false );
	renderSystem->SetColor4( 1.0f, 0.65f, 0.0f, 0.9f );
	renderSystem->DrawStretchPic( guideX, guideY, guideW, lineThickness, 0.0f, 0.0f, 1.0f, 1.0f, white );
	renderSystem->DrawStretchPic( guideX, guideY + guideH - lineThickness, guideW, lineThickness, 0.0f, 0.0f, 1.0f, 1.0f, white );
	renderSystem->DrawStretchPic( guideX, guideY, lineThickness, guideH, 0.0f, 0.0f, 1.0f, 1.0f, white );
	renderSystem->DrawStretchPic( guideX + guideW - lineThickness, guideY, lineThickness, guideH, 0.0f, 0.0f, 1.0f, 1.0f, white );
	renderSystem->SetColor( colorWhite );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

static const char *Session_GetLongMPGameTypeName( const char *gametype ) {
	if ( !gametype || !gametype[ 0 ] ) {
		return "";
	}

	if ( !idStr::Icmp( gametype, "Tourney" ) ) {
		return common->GetLocalizedString( "#str_107676" );
	}
	if ( !idStr::Icmp( gametype, "Team DM" ) ) {
		return common->GetLocalizedString( "#str_107677" );
	}
	if ( !idStr::Icmp( gametype, "CTF" ) ) {
		return common->GetLocalizedString( "#str_107678" );
	}
	if ( !idStr::Icmp( gametype, "DM" ) ) {
		return common->GetLocalizedString( "#str_107679" );
	}
	if ( !idStr::Icmp( gametype, "One Flag CTF" ) ) {
		return common->GetLocalizedString( "#str_107680" );
	}
	if ( !idStr::Icmp( gametype, "Arena CTF" ) ) {
		return common->GetLocalizedString( "#str_107681" );
	}
	if ( !idStr::Icmp( gametype, "Arena One Flag CTF" ) ) {
		return common->GetLocalizedString( "#str_107682" );
	}
	if ( !idStr::Icmp( gametype, "DeadZone" ) ) {
		return common->GetLocalizedString( "#str_122001" );
	}

	return gametype;
}

static idStr Session_GetMPLoadLimitString( const idDict &serverInfo ) {
	const char *gameType = serverInfo.GetString( "si_gameType" );
	const bool useCaptureLimit =
		( !idStr::Icmp( gameType, "CTF" ) ) ||
		( !idStr::Icmp( gameType, "Arena CTF" ) ) ||
		( !idStr::Icmp( gameType, "One Flag CTF" ) ) ||
		( !idStr::Icmp( gameType, "Arena One Flag CTF" ) );

	const int limit = useCaptureLimit ? serverInfo.GetInt( "si_captureLimit" ) : serverInfo.GetInt( "si_fragLimit" );
	if ( limit <= 0 ) {
		return "";
	}

	const char *label = common->GetLocalizedString( useCaptureLimit ? "#str_107661" : "#str_107660" );
	return va( "%s %d", label, limit );
}

static bool Session_IsLoadingContinueKey( int key ) {
	if ( key <= 0 || key >= K_LAST_KEY ) {
		return false;
	}

	switch ( key ) {
		case K_COMMAND:
		case K_CAPSLOCK:
		case K_SCROLL:
		case K_POWER:
		case K_PRINT_SCR:
		case K_RIGHT_ALT:
			return false;
		default:
			return true;
	}
}

static bool Session_IsLoadingContinueChar( int ch ) {
	return ch >= K_SPACE;
}

static bool Session_ShouldSilenceAudioWhenUnfocused() {
#if defined( USE_SDL3 )
	return s_muteUnfocused.GetBool() && !Sys_SDL_IsGameWindowFocused();
#elif defined( _WIN32 )
	return s_muteUnfocused.GetBool() && !Sys_IsGameWindowFocused();
#else
	return false;
#endif
}

void RandomizeStack( void ) {
	// attempt to force uninitialized stack memory bugs
	int		bytes = 4000000;
	byte	*buf = (byte *)_alloca( bytes );

	int	fill = rand()&255;
	for ( int i = 0 ; i < bytes ; i++ ) {
		buf[i] = fill;
	}
}

/*
=================
Session_RescanSI_f
=================
*/
void Session_RescanSI_f( const idCmdArgs &args ) {
	sessLocal.mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	if ( game && idAsyncNetwork::server.IsActive() ) {
		game->SetServerInfo( sessLocal.mapSpawnData.serverInfo );
	}
}

#ifndef ID_DEDICATED
static bool Session_IsLightGridBakeMultiplayerMap( const idStr &mapName ) {
	return idStr::Icmpn( mapName.c_str(), "mp/", 3 ) == 0;
}

static void Session_NormalizeLightGridMapName( idStr &mapName ) {
	mapName.BackSlashesToSlashes();
	mapName.StripLeading( "maps/" );
	mapName.StripFileExtension();
}

static bool Session_LightGridMapListContains( const idList<idStr> &mapTargets, const idStr &mapName ) {
	for ( int i = 0; i < mapTargets.Num(); i++ ) {
		if ( mapTargets[ i ].Icmp( mapName ) == 0 ) {
			return true;
		}
	}

	return false;
}

static bool Session_LightGridMapExists( const idStr &mapName ) {
	idStr mapFile = "maps/";
	mapFile += mapName;
	mapFile.SetFileExtension( ".map" );
	return fileSystem->FindFile( mapFile, true ) != FIND_NO;
}

static void Session_AppendAllLightGridMaps( idList<idStr> &mapTargets ) {
	idList<idStr> singleplayerMaps;
	idList<idStr> multiplayerMaps;

	const int numMaps = fileSystem->GetNumMaps();
	for ( int i = 0; i < numMaps; i++ ) {
		const idDict *mapDecl = fileSystem->GetMapDecl( i );
		if ( mapDecl == NULL ) {
			continue;
		}

		idStr mapName = mapDecl->GetString( "path" );
		Session_NormalizeLightGridMapName( mapName );
		if ( mapName.Length() <= 0 ) {
			continue;
		}

		idList<idStr> &targetList = Session_IsLightGridBakeMultiplayerMap( mapName ) ? multiplayerMaps : singleplayerMaps;
		if ( !Session_LightGridMapListContains( targetList, mapName ) ) {
			targetList.Append( mapName );
		}
	}

	for ( int i = 0; i < singleplayerMaps.Num(); i++ ) {
		mapTargets.Append( singleplayerMaps[ i ] );
	}
	for ( int i = 0; i < multiplayerMaps.Num(); i++ ) {
		mapTargets.Append( multiplayerMaps[ i ] );
	}
}

static void Session_AppendAllMultiplayerLightGridMaps( idList<idStr> &mapTargets ) {
	idList<idStr> multiplayerMaps;

	const int numMaps = fileSystem->GetNumMaps();
	for ( int i = 0; i < numMaps; i++ ) {
		const idDict *mapDecl = fileSystem->GetMapDecl( i );
		if ( mapDecl == NULL ) {
			continue;
		}

		idStr mapName = mapDecl->GetString( "path" );
		Session_NormalizeLightGridMapName( mapName );
		if ( mapName.Length() <= 0 || !Session_IsLightGridBakeMultiplayerMap( mapName ) ) {
			continue;
		}

		if ( !Session_LightGridMapListContains( multiplayerMaps, mapName ) ) {
			multiplayerMaps.Append( mapName );
		}
	}

	for ( int i = 0; i < multiplayerMaps.Num(); i++ ) {
		mapTargets.Append( multiplayerMaps[ i ] );
	}
}

static void Session_BuildLightGridOutputPaths( const idStr &mapName, idStr &lightGridPath, idStr &atlasDir, idStr *packPath = NULL ) {
	lightGridPath = "maps/";
	lightGridPath += mapName;
	lightGridPath.SetFileExtension( ".lightgrid" );

	atlasDir = "env/maps/";
	atlasDir += mapName;

	if ( packPath != NULL ) {
		*packPath = "maps/";
		*packPath += mapName;
		packPath->SetFileExtension( ".lightgridpack" );
	}
}

static bool Session_IsLightGridAtlasArtifact( const idStr &relativePath ) {
	idStr fileName = relativePath;
	fileName.StripPath();
	if ( fileName.Icmpn( "area", 4 ) != 0 ) {
		return false;
	}
	return idStr::FindText( fileName.c_str(), "_lightgrid_amb" ) >= 0 ||
		idStr::FindText( fileName.c_str(), "_lightgrid_vis" ) >= 0 ||
		idStr::FindText( fileName.c_str(), "_lightgrid_pos" ) >= 0;
}

static void Session_RemoveLightGridAtlasArtifacts( const idStr &atlasDir, const char *extension ) {
	idFileList *fileList = fileSystem->ListFiles( atlasDir.c_str(), extension, true, true );
	if ( fileList == NULL ) {
		return;
	}

	for ( int i = 0; i < fileList->GetNumFiles(); i++ ) {
		idStr relativePath = fileList->GetFile( i );
		if ( !Session_IsLightGridAtlasArtifact( relativePath ) ) {
			continue;
		}

		fileSystem->RemoveFile( relativePath.c_str() );
	}

	fileSystem->FreeFileList( fileList );
}

static void Session_RemoveLightGridBakeOutputsForMap( const idStr &mapName ) {
	idStr lightGridPath;
	idStr atlasDir;
	idStr packPath;
	Session_BuildLightGridOutputPaths( mapName, lightGridPath, atlasDir, &packPath );

	fileSystem->RemoveFile( lightGridPath.c_str() );
	fileSystem->RemoveFile( va( "%s.prev", lightGridPath.c_str() ) );
	fileSystem->RemoveFile( va( "%s.baking", lightGridPath.c_str() ) );
	fileSystem->RemoveFile( packPath.c_str() );
	fileSystem->RemoveFile( va( "%s.prev", packPath.c_str() ) );
	fileSystem->RemoveFile( va( "%s.baking", packPath.c_str() ) );
	Session_RemoveLightGridAtlasArtifacts( atlasDir, ".tga" );
	Session_RemoveLightGridAtlasArtifacts( atlasDir, ".prev" );
}

static bool Session_CurrentLightGridOutputsComplete( const lightGridBakeOptions_t &options, idStr &mapName, int &requiredAtlasCount ) {
	requiredAtlasCount = 0;

	// caller-buffer out-params: idStr/idList must not resize across the
	// renderer-module ABI
	char bakeMapName[ MAX_OSPATH ];
	static const int MAX_BAKE_AREAS = 4096;
	int validAreaIndices[ MAX_BAKE_AREAS ];
	int numValidAreaIndices = 0;
	if ( !renderSystem->GetCurrentLightGridBakeInfo( options, bakeMapName, sizeof( bakeMapName ), validAreaIndices, MAX_BAKE_AREAS, numValidAreaIndices ) ) {
		return false;
	}
	if ( numValidAreaIndices > MAX_BAKE_AREAS ) {
		// more areas than the buffer holds: report incomplete so the bake
		// re-runs rather than trusting a truncated completeness check
		common->Warning( "Session_CurrentLightGridOutputsComplete: %d bake areas exceed the %d-entry buffer", numValidAreaIndices, MAX_BAKE_AREAS );
		return false;
	}
	mapName = bakeMapName;
	Session_NormalizeLightGridMapName( mapName );
	if ( mapName.Length() <= 0 ) {
		return false;
	}
	requiredAtlasCount = numValidAreaIndices;

	idStr lightGridPath;
	idStr atlasDir;
	idStr packPath;
	Session_BuildLightGridOutputPaths( mapName, lightGridPath, atlasDir, &packPath );
	if ( fileSystem->FindFile( packPath.c_str(), true ) != FIND_NO && renderSystem->LightGridPackFileMatchesBakeOptions( packPath.c_str(), options ) ) {
		return true;
	}
	if ( fileSystem->FindFile( lightGridPath.c_str(), true ) == FIND_NO ) {
		return false;
	}
	if ( !renderSystem->LightGridFileMatchesBakeOptions( lightGridPath.c_str(), options ) ) {
		return false;
	}

	for ( int i = 0; i < numValidAreaIndices; i++ ) {
		const int areaIndex = validAreaIndices[ i ];

		idStr atlasPath = va( "%s/area%i_lightgrid_amb.tga", atlasDir.c_str(), areaIndex );
		if ( fileSystem->FindFile( atlasPath.c_str(), true ) == FIND_NO ) {
			return false;
		}
		idStr visibilityPath = va( "%s/area%i_lightgrid_vis.tga", atlasDir.c_str(), areaIndex );
		if ( fileSystem->FindFile( visibilityPath.c_str(), true ) == FIND_NO ) {
			return false;
		}
		idStr probePath = va( "%s/area%i_lightgrid_pos.tga", atlasDir.c_str(), areaIndex );
		if ( fileSystem->FindFile( probePath.c_str(), true ) == FIND_NO ) {
			return false;
		}
	}

	return true;
}

static void Session_PrintLightGridBakeUsage() {
	common->Printf( "usage: bakeLightGrids [all | all-mp | <map> ...] [force] [-quit] [limit<num>] [bounce<num>] [size<num>] [blends<num>] [samples<num>] [separateAreas] [grid ( x y z )]\n" );
	common->Printf( "If no map names are given, the currently loaded map is baked.\n" );
	common->Printf( "Without 'force', maps whose .lightgridpack output or required .lightgrid metadata plus area atlas files already exist are skipped.\n" );
	common->Printf( "When map names, 'all', or 'all-mp' are given, openQ4 loads each map automatically, prints live progress to the console/log, and writes .lightgridpack plus loose .lightgrid/TGA fallback outputs to fs_savepath.\n" );
	common->Printf( "'separateAreas' rebuilds one portal-area probe layout at a time and streams .lightgrid metadata during the bake to reduce peak CPU memory usage.\n" );
	common->Printf( "Multiplayer targets are cheat-protected; enable cheats first with 'sv_cheats 1' or 'net_allowCheats 1'.\n" );
	common->Printf( "This bake is diffuse-only and LDR. It does not output the BFG EXR/PBR light-grid data path.\n" );
}

static bool Session_ParseLightGridBakeArgs( const idCmdArgs &args, lightGridBakeOptions_t &options,
	idList<idStr> &mapTargets, bool &bakeAll, bool &bakeAllMultiplayer, bool &forceBake, bool &autoQuit, int &resumeIndex, bool &helpRequested, bool &requestedTargets ) {
	renderSystem->GetDefaultLightGridBakeOptions( options );
	bakeAll = false;
	bakeAllMultiplayer = false;
	forceBake = false;
	autoQuit = false;
	resumeIndex = 0;
	helpRequested = false;
	requestedTargets = false;
	mapTargets.Clear();

	for ( int i = 1; i < args.Argc(); i++ ) {
		idStr rawToken = args.Argv( i );
		idStr option = rawToken;
		option.StripLeading( "-" );

		if ( option.Icmp( "h" ) == 0 || option.Icmp( "help" ) == 0 ) {
			helpRequested = true;
			return true;
		}

		if ( option.Icmp( "all" ) == 0 ) {
			bakeAll = true;
			requestedTargets = true;
			continue;
		}
		if ( option.Icmp( "all-mp" ) == 0 || option.Icmp( "allmp" ) == 0 ) {
			bakeAllMultiplayer = true;
			requestedTargets = true;
			continue;
		}
		if ( option.Icmp( "force" ) == 0 ) {
			forceBake = true;
			continue;
		}

		if ( option.Icmp( "quit" ) == 0 || option.Icmp( "autoquit" ) == 0 ) {
			autoQuit = true;
			continue;
		}
		if ( option.Icmp( "separateAreas" ) == 0 || option.Icmp( "separate" ) == 0 || option.Icmp( "streamAreas" ) == 0 ) {
			options.separateAreas = true;
			continue;
		}

		if ( option.IcmpPrefix( "resumeIndex" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "resumeIndex" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			resumeIndex = Max( 0, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "limit" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "limit" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			options.maxProbes = idMath::ClampInt( 1, 16384, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "bounce" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "bounce" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			options.bounces = Max( 1, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "size" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "size" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			options.captureSize = idMath::ClampInt( 16, 1024, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "blends" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "blends" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			options.blends = idMath::ClampInt( 1, 32, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "samples" ) == 0 ) {
			idStr value = option;
			value.StripLeading( "samples" );
			if ( value.Length() <= 0 ) {
				if ( i + 1 >= args.Argc() ) {
					common->Printf( "bakeLightGrids: missing value for %s\n", rawToken.c_str() );
					return false;
				}
				value = args.Argv( ++i );
			}
			options.samples = idMath::ClampInt( 1, 4096, atoi( value.c_str() ) );
			continue;
		}

		if ( option.IcmpPrefix( "grid" ) == 0 ) {
			if ( i + 5 < args.Argc() && idStr::Icmp( args.Argv( i + 1 ), "(" ) == 0 && idStr::Icmp( args.Argv( i + 5 ), ")" ) == 0 ) {
				options.gridSize.x = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 2 ) ) ) );
				options.gridSize.y = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 3 ) ) ) );
				options.gridSize.z = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 4 ) ) ) );
				i += 5;
				continue;
			}
			if ( i + 3 < args.Argc() ) {
				options.gridSize.x = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 1 ) ) ) );
				options.gridSize.y = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 2 ) ) ) );
				options.gridSize.z = Max( 1.0f, static_cast<float>( atof( args.Argv( i + 3 ) ) ) );
				i += 3;
				continue;
			}

			common->Printf( "bakeLightGrids: expected 'grid x y z' or 'grid ( x y z )'\n" );
			return false;
		}

		if ( rawToken.Length() > 0 && rawToken[ 0 ] == '-' ) {
			common->Printf( "bakeLightGrids: unknown option '%s'\n", rawToken.c_str() );
			return false;
		}

		idStr mapName = rawToken;
		Session_NormalizeLightGridMapName( mapName );
		if ( mapName.Length() > 0 && !Session_LightGridMapListContains( mapTargets, mapName ) ) {
			requestedTargets = true;
			mapTargets.Append( mapName );
		}
	}

	if ( bakeAll && bakeAllMultiplayer ) {
		common->Printf( "bakeLightGrids: ignoring 'all-mp' because 'all' was requested\n" );
		bakeAllMultiplayer = false;
	}

	if ( bakeAll || bakeAllMultiplayer ) {
		if ( mapTargets.Num() > 0 ) {
			common->Printf( "bakeLightGrids: ignoring explicit map names because '%s' was requested\n", bakeAll ? "all" : "all-mp" );
		}
		mapTargets.Clear();
		if ( bakeAll ) {
			Session_AppendAllLightGridMaps( mapTargets );
		} else {
			Session_AppendAllMultiplayerLightGridMaps( mapTargets );
		}
	} else {
		idList<idStr> validatedTargets;
		for ( int i = 0; i < mapTargets.Num(); i++ ) {
			if ( !Session_LightGridMapExists( mapTargets[ i ] ) ) {
				common->Printf( "bakeLightGrids: skipping missing map '%s'\n", mapTargets[ i ].c_str() );
				continue;
			}
			validatedTargets.Append( mapTargets[ i ] );
		}
		mapTargets = validatedTargets;
	}

	return true;
}

static void Session_BuildLightGridBakeResumeArgs( const lightGridBakeOptions_t &options, const idList<idStr> &mapTargets,
	bool bakeAll, bool bakeAllMultiplayer, bool forceBake, bool autoQuit, int resumeIndex, idCmdArgs &reloadArgs ) {
	reloadArgs.AppendArg( "openq4_resumeBakeLightGrids" );

	if ( bakeAll ) {
		reloadArgs.AppendArg( "all" );
	} else if ( bakeAllMultiplayer ) {
		reloadArgs.AppendArg( "all-mp" );
	} else {
		for ( int i = 0; i < mapTargets.Num(); i++ ) {
			reloadArgs.AppendArg( mapTargets[ i ].c_str() );
		}
	}

	if ( forceBake ) {
		reloadArgs.AppendArg( "force" );
	}

	if ( autoQuit ) {
		reloadArgs.AppendArg( "-quit" );
	}

	reloadArgs.AppendArg( "-limit" );
	reloadArgs.AppendArg( va( "%i", options.maxProbes ) );
	reloadArgs.AppendArg( "-bounce" );
	reloadArgs.AppendArg( va( "%i", options.bounces ) );
	reloadArgs.AppendArg( "-size" );
	reloadArgs.AppendArg( va( "%i", options.captureSize ) );
	reloadArgs.AppendArg( "-blends" );
	reloadArgs.AppendArg( va( "%i", options.blends ) );
	reloadArgs.AppendArg( "-samples" );
	reloadArgs.AppendArg( va( "%i", options.samples ) );
	reloadArgs.AppendArg( "-grid" );
	reloadArgs.AppendArg( va( "%.3f", options.gridSize.x ) );
	reloadArgs.AppendArg( va( "%.3f", options.gridSize.y ) );
	reloadArgs.AppendArg( va( "%.3f", options.gridSize.z ) );
	reloadArgs.AppendArg( "-resumeIndex" );
	reloadArgs.AppendArg( va( "%i", resumeIndex ) );
}

static void Session_ReloadLightGridBakeBatch( const lightGridBakeOptions_t &options, const idList<idStr> &mapTargets,
	bool bakeAll, bool bakeAllMultiplayer, bool forceBake, bool autoQuit, int resumeIndex, bool useMultiplayerModule ) {
	cvarSystem->SetCVarString( "si_gameType", useMultiplayerModule ? "DM" : "singleplayer" );
	cvarSystem->SetCVarString( "com_nextGameModule", useMultiplayerModule ? "game_mp" : "game_sp" );

	idCmdArgs reloadArgs;
	Session_BuildLightGridBakeResumeArgs( options, mapTargets, bakeAll, bakeAllMultiplayer, forceBake, autoQuit, resumeIndex, reloadArgs );

	common->Printf(
		"bakeLightGrids: reloading engine into %s to continue at map %i of %i\n",
		useMultiplayerModule ? "game_mp" : "game_sp",
		resumeIndex + 1,
		mapTargets.Num() );
	cmdSystem->SetupReloadGameModule( reloadArgs );
}

static bool Session_WaitForLightGridBakeView( const idStr &mapName ) {
	const int startMsec = Sys_Milliseconds();
	const int maxWaitMsec = 10000;
	const int maxFrames = 240;

	for ( int frame = 0; frame < maxFrames && Sys_Milliseconds() - startMsec < maxWaitMsec; frame++ ) {
		if ( renderSystem->HasPrimaryRenderView() ) {
			if ( frame > 0 ) {
				common->Printf( "bakeLightGrids: render view ready for %s after %i warm-up frame(s)\n", mapName.c_str(), frame );
			}
			return true;
		}

		common->GUIFrame( false, true );
	}

	common->Printf( "bakeLightGrids: timed out waiting for render view after loading '%s'\n", mapName.c_str() );
	return renderSystem->HasPrimaryRenderView();
}

static bool Session_LoadLightGridBakeMap( const idStr &mapName ) {
	sessLocal.Stop();

	if ( Session_IsLightGridBakeMultiplayerMap( mapName ) ) {
		if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ) {
			common->Printf( "bakeLightGrids: forcing net_serverDedicated 0 so multiplayer maps can render during baking\n" );
			cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );
		}

		cvarSystem->SetCVarString( "si_gameType", "DM" );
		cvarSystem->SetCVarString( "si_map", mapName.c_str() );

		idCmdArgs spawnArgs;
		spawnArgs.AppendArg( "spawnServer" );
		spawnArgs.AppendArg( mapName.c_str() );
		cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, spawnArgs );
	} else {
		cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
		cvarSystem->SetCVarString( "si_map", mapName.c_str() );
		sessLocal.StartNewGame( mapName.c_str(), true );
	}

	if ( !sessLocal.mapSpawned ) {
		common->Printf( "bakeLightGrids: failed to load map '%s'\n", mapName.c_str() );
		return false;
	}

	return Session_WaitForLightGridBakeView( mapName );
}

static bool Session_CanBakeLightGridMap( const idStr &mapName ) {
	if ( !Session_IsLightGridBakeMultiplayerMap( mapName ) || idAsyncNetwork::AreCheatsEnabled() ) {
		return true;
	}

	common->Printf(
		"bakeLightGrids: multiplayer target '%s' is cheat-protected. Set sv_cheats 1 or net_allowCheats 1 before baking.\n",
		mapName.c_str() );
	return false;
}

static bool Session_BakeLightGridCurrentMap( const lightGridBakeOptions_t &options, bool forceBake, bool *wasSkipped = NULL ) {
	if ( wasSkipped != NULL ) {
		*wasSkipped = false;
	}

	if ( sessLocal.IsMultiplayer() && !idAsyncNetwork::AreCheatsEnabled() ) {
		common->Printf( "bakeLightGrids: the current multiplayer map is cheat-protected. Set sv_cheats 1 or net_allowCheats 1 before baking.\n" );
		return false;
	}

	if ( sessLocal.mapSpawned && !renderSystem->HasPrimaryRenderView() ) {
		idStr waitMapName = cvarSystem->GetCVarString( "si_map" );
		if ( !Session_WaitForLightGridBakeView( waitMapName ) ) {
			return false;
		}
	}

	idStr mapName;
	int requiredAtlasCount = 0;
	const bool outputsComplete = Session_CurrentLightGridOutputsComplete( options, mapName, requiredAtlasCount );
	if ( outputsComplete ) {
		if ( forceBake ) {
			common->Printf( "bakeLightGrids: force requested; cleaning existing outputs for %s\n", mapName.c_str() );
			Session_RemoveLightGridBakeOutputsForMap( mapName );
		} else {
			common->Printf( "bakeLightGrids: skipping %s because packed or loose light-grid outputs already exist for %i baked area(s)\n", mapName.c_str(), requiredAtlasCount );
			if ( wasSkipped != NULL ) {
				*wasSkipped = true;
			}
			return true;
		}
	} else if ( forceBake && mapName.Length() > 0 ) {
		common->Printf( "bakeLightGrids: force requested; removing any stale outputs for %s before baking\n", mapName.c_str() );
		Session_RemoveLightGridBakeOutputsForMap( mapName );
	}

	const char *jobName = cvarSystem->GetCVarString( "si_map" );
	return renderSystem->BakeCurrentLightGrids( options, ( jobName != NULL && jobName[ 0 ] != '\0' ) ? jobName : NULL );
}

static void Session_RunLightGridBake( const idCmdArgs &args ) {
	lightGridBakeOptions_t options;
	idList<idStr> mapTargets;
	bool bakeAll = false;
	bool bakeAllMultiplayer = false;
	bool forceBake = false;
	bool autoQuit = false;
	int resumeIndex = 0;
	bool helpRequested = false;
	bool requestedTargets = false;

	if ( !Session_ParseLightGridBakeArgs( args, options, mapTargets, bakeAll, bakeAllMultiplayer, forceBake, autoQuit, resumeIndex, helpRequested, requestedTargets ) ) {
		Session_PrintLightGridBakeUsage();
		return;
	}

	if ( helpRequested ) {
		Session_PrintLightGridBakeUsage();
		return;
	}

	Sys_ShowConsole( 1, false );

	if ( mapTargets.Num() <= 0 ) {
		if ( requestedTargets ) {
			common->Printf( "bakeLightGrids: no valid map targets were found.\n" );
			return;
		}

		if ( !Session_BakeLightGridCurrentMap( options, forceBake ) ) {
			if ( !sessLocal.mapSpawned ) {
				common->Printf( "bakeLightGrids: no map target was provided and no current map is loaded.\n" );
				Session_PrintLightGridBakeUsage();
			}
			return;
		}

		if ( autoQuit ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		}
		return;
	}

	if ( resumeIndex >= mapTargets.Num() ) {
		common->Printf( "bakeLightGrids: all requested maps are already complete.\n" );
		if ( autoQuit ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		}
		return;
	}

	common->Printf( "bakeLightGrids: batch mode with %i map(s), starting at %i\n", mapTargets.Num(), resumeIndex + 1 );

	for ( int mapIndex = resumeIndex; mapIndex < mapTargets.Num(); mapIndex++ ) {
		const idStr &mapName = mapTargets[ mapIndex ];
		const bool needsMultiplayerModule = Session_IsLightGridBakeMultiplayerMap( mapName );
		const char *requiredModule = needsMultiplayerModule ? "game_mp" : "game_sp";
		const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );

		if ( !Session_CanBakeLightGridMap( mapName ) ) {
			return;
		}

		if ( idStr::Icmp( activeModule, requiredModule ) != 0 ) {
			Session_ReloadLightGridBakeBatch( options, mapTargets, bakeAll, bakeAllMultiplayer, forceBake, autoQuit, mapIndex, needsMultiplayerModule );
			return;
		}

		common->Printf( "bakeLightGrids: [%i/%i] loading %s\n", mapIndex + 1, mapTargets.Num(), mapName.c_str() );
		if ( !Session_LoadLightGridBakeMap( mapName ) ) {
			return;
		}

		if ( !Session_BakeLightGridCurrentMap( options, forceBake ) ) {
			return;
		}
	}

	common->Printf( "bakeLightGrids: batch completed for %i map(s)\n", mapTargets.Num() );
	if ( autoQuit ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

static void Session_BakeLightGrids_f( const idCmdArgs &args ) {
	Session_RunLightGridBake( args );
}

static void Session_openQ4ResumeBakeLightGrids_f( const idCmdArgs &args ) {
	Session_RunLightGridBake( args );
}

/*
==================
Session_Map_f

Restart the server on a different map
==================
*/
static void Session_Map_f( const idCmdArgs &args ) {
	idStr		map, string;
	idStr		entityFilter;
	findFile_t	ff;
	idCmdArgs	rl_args;

	Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), map, entityFilter );
	if ( !map.Length() ) {
		return;
	}

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	// handle addon packs through reloadEngine
	string = "maps/";
	string += map;
	string.SetFileExtension( ".map" );
	ff = fileSystem->FindFile( string, true );
	switch ( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", string.c_str() );
		return;
	case FIND_ADDON:
		common->Printf( "map %s is in an addon pak - reloading\n", string.c_str() );
		rl_args.AppendArg( "map" );
		rl_args.AppendArg( map );
		if ( entityFilter.Length() > 0 ) {
			rl_args.AppendArg( entityFilter.c_str() );
		}
		cmdSystem->SetupReloadEngine( rl_args );
		return;
	default:
		break;
	}

	const bool developerMapStart = cvarSystem->GetCVarBool( "developer" );
	sessLocal.StartNewGame( map, developerMapStart, entityFilter.c_str() );
}

/*
==================
Session_DevMap_f

Restart the server on a different map in developer mode
==================
*/
static void Session_DevMap_f( const idCmdArgs &args ) {
	idStr map, string, entityFilter;
	findFile_t	ff;
	idCmdArgs	rl_args;	

	Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), map, entityFilter );
	if ( !map.Length() ) {
		return;
	}

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	// handle addon packs through reloadEngine
	string = "maps/";
	string += map;
	string.SetFileExtension( ".map" );
	ff = fileSystem->FindFile( string, true );
	switch ( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", string.c_str() );
		return;
	case FIND_ADDON:
		common->Printf( "map %s is in an addon pak - reloading\n", string.c_str() );
		rl_args.AppendArg( "devmap" );
		rl_args.AppendArg( map );
		if ( entityFilter.Length() > 0 ) {
			rl_args.AppendArg( entityFilter.c_str() );
		}
		cmdSystem->SetupReloadEngine( rl_args );
		return;
	default:
		break;
	}

	const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );
	if ( idStr::Icmp( activeModule, "game_mp" ) == 0 ) {
		idAsyncNetwork::SetCheatsEnabled( true );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "spawnServer %s", map.c_str() ) );
		return;
	}

	cvarSystem->SetCVarBool( "developer", true );
	sessLocal.StartNewGame( map, true, entityFilter.c_str() );
}

/*
==================
Session_TestMap_f
==================
*/
static void Session_TestMap_f( const idCmdArgs &args ) {
	idStr map, string, entityFilter;

	Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), map, entityFilter );
	if ( !map.Length() ) {
		return;
	}

	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );

	string = "dmap maps/";
	string += map;
	string.SetFileExtension( ".map" );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, string );

	if ( entityFilter.Length() > 0 ) {
		string = "devmap ";
		string += map;
		string += " ";
		string += entityFilter;
	} else {
		string = "devmap ";
		string += map;
	}
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, string );
}

/*
==================
Session_openQ4StartSingleplayer_f
==================
*/
static void Session_openQ4StartSingleplayer_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "USAGE: openq4_startSingleplayer <map> [devmap] [entityFilter]\n" );
		return;
	}

	const bool devmap = args.Argc() > 2 && atoi( args.Argv( 2 ) ) != 0;
	const char *entityFilter = ( args.Argc() > 3 ) ? args.Argv( 3 ) : "";
	sessLocal.StartNewGame( args.Argv( 1 ), devmap, entityFilter );
}

/*
==================
Session_openQ4AssertMapState_f
==================
*/
static void Session_openQ4AssertMapState_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "USAGE: openq4_assertMapState <map> [entityFilter]\n" );
		return;
	}

	idStr expectedMap;
	idStr expectedEntityFilter;
	Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), expectedMap, expectedEntityFilter );
	if ( expectedMap.Length() == 0 ) {
		common->Error( "openQ4 map state assertion needs a non-empty expected map" );
		return;
	}

	idStr actualMap;
	idStr actualEntityFilter;
	Session_NormalizeMapDeclPath( sessLocal.mapSpawnData.serverInfo.GetString( "si_map", "" ), actualMap );
	Session_NormalizeEntityFilterToken( sessLocal.mapSpawnData.serverInfo.GetString( "si_entityFilter", "" ), actualEntityFilter );

	common->Printf( "openQ4 map state: map=%s entityFilter=%s expectedMap=%s expectedEntityFilter=%s\n",
		actualMap.c_str(),
		actualEntityFilter.c_str(),
		expectedMap.c_str(),
		expectedEntityFilter.c_str() );

	const bool mapMatches = fileSystem->FilenameCompare( actualMap.c_str(), expectedMap.c_str() ) == 0;
	const bool filterMatches = idStr::Icmp( actualEntityFilter.c_str(), expectedEntityFilter.c_str() ) == 0;
	if ( !mapMatches || !filterMatches ) {
		common->Error( "openQ4 map state mismatch: expected map=%s entityFilter=%s, got map=%s entityFilter=%s",
			expectedMap.c_str(),
			expectedEntityFilter.c_str(),
			actualMap.c_str(),
			actualEntityFilter.c_str() );
	}
}

/*
==================
Session_openQ4AssertMPGameplayView_f
==================
*/
static void Session_openQ4AssertMPGameplayView_f( const idCmdArgs &args ) {
	if ( !sessLocal.IsMapSpawned() || !sessLocal.IsMultiplayer() ||
			sessLocal.GetActiveGUI() != NULL ) {
		common->Error( "openq4_assertMPGameplayView failed: map=%d multiplayer=%d gui=%d",
			sessLocal.IsMapSpawned() ? 1 : 0,
			sessLocal.IsMultiplayer() ? 1 : 0,
			sessLocal.GetActiveGUI() != NULL ? 1 : 0 );
		return;
	}

	common->Printf( "OPENQ4_STOCK_BASELINE_MP_CLIENT_VIEW gui=0\n" );
}

/*
==================
Session_openQ4AssertMenuActivation_f
==================
*/
static void Session_openQ4AssertMenuActivation_f( const idCmdArgs &args ) {
	const int maxMsec = args.Argc() > 1 ? atoi( args.Argv( 1 ) ) : 50;
	const bool gameMenuRequested = args.Argc() > 2 && idStr::Icmp( args.Argv( 2 ), "game" ) == 0;
	if ( gameMenuRequested && args.Argc() == 4 && idStr::Icmp( args.Argv( 3 ), "reopen" ) == 0 &&
		maxMsec > 0 && sessLocal.IsMapSpawned() && game != NULL && sessLocal.GetActiveGUI() != NULL ) {
		// A review screen can remain open into the next warmup. Exercise the
		// game's ordinary close handler before measuring a fresh menu opening;
		// this is a semantic GUI action, with no injected keyboard/mouse event.
		game->HandleGuiCommands( "close" );
		sessLocal.SetGUI( NULL, NULL );
	}
	if ( maxMsec < 1 || !sessLocal.IsMapSpawned() || sessLocal.GetActiveGUI() != NULL ) {
		common->Error( "openq4_assertMenuActivation requires active gameplay with no GUI and a positive millisecond limit" );
		return;
	}

	const int startMsec = Sys_Milliseconds();
	if ( gameMenuRequested ) {
		idUserInterface *gameMenu = NULL;
		if ( game == NULL || game->HandleESC( &gameMenu ) != ESC_GUI || gameMenu == NULL ) {
			common->Error( "openq4_assertMenuActivation: game did not provide its in-game menu" );
			return;
		}
		sessLocal.SetGUI( gameMenu, NULL );
	} else {
		sessLocal.StartMenu( false );
	}
	const int elapsedMsec = Max( 0, Sys_Milliseconds() - startMsec );
	if ( sessLocal.GetActiveMenu() == NULL || elapsedMsec > maxMsec ) {
		common->Error( "openq4_assertMenuActivation failed: active=%d elapsed=%dms limit=%dms",
			sessLocal.GetActiveMenu() != NULL ? 1 : 0, elapsedMsec, maxMsec );
		return;
	}

	common->Printf( "OPENQ4_MENU_ACTIVATION PASS elapsed=%dms limit=%dms\n", elapsedMsec, maxMsec );
}
#endif

/*
==================
Sess_WritePrecache_f
==================
*/
static void Sess_WritePrecache_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "USAGE: writePrecache <execFile>\n" );
		return;
	}
	idStr	str = args.Argv(1);
	str.DefaultFileExtension( ".cfg" );
	idFile *f = fileSystem->OpenFileWrite( str );
	declManager->WritePrecacheCommands( f );
	renderModelManager->WritePrecacheCommands( f );
	uiManager->WritePrecacheCommands( f );

	fileSystem->CloseFile( f );
}

/*
===============
idSessionLocal::MaybeWaitOnCDKey
===============
*/
bool idSessionLocal::MaybeWaitOnCDKey( void ) {
	return false;
}

/*
===============================================================================

SESSION LOCAL
  
===============================================================================
*/

/*
===============
idSessionLocal::Clear
===============
*/
void idSessionLocal::Clear() {
	
	insideUpdateScreen = false;
	insidePacifierUpdate = false;
	insideExecuteMapChange = false;
	stopDepth = 0;
	preserveWipeDuringStop = false;

	loadingSaveGame = false;
	savegameFile = NULL;
	savegameVersion = 0;

	currentMapName.Clear();
	currentFilterString.Clear();
	aviDemoShortName.Clear();
	msgFireBack[ 0 ].Clear();
	msgFireBack[ 1 ].Clear();

	timeHitch = 0;
	lastCheckPoint = -1;
	objectiveFailed = false;
	ResetFramePacingStats();

	rw = NULL;
	sw = NULL;
	menuSoundWorld = NULL;
	requestedSoundWorld = NULL;
	readDemo = NULL;
	writeDemo = NULL;
	renderdemoVersion = 0;
	cmdDemoFile = NULL;
	activeRenderDemoName.Clear();
	renderDemoDurationName.Clear();
	renderDemoPaused = false;
	renderDemoSeeking = false;
	renderDemoSeekRestartPending = false;
	renderDemoSeekRestartPresentationPending = false;
	renderDemoSeekWorkDeferred = false;
	renderDemoStepPending = false;
	renderDemoFramePayloadPending = false;
	renderDemoSeekMuteOwned = false;
	renderDemoSeekRestoreMute = false;
	renderDemoPlaybackRate = 1.0f;
	renderDemoFrameAccumulator = 0.0;
	renderDemoStartViewTime = 0;
	renderDemoKnownDurationMS = -1;
	renderDemoSeekTargetMS = 0;
	renderDemoSeekDeadlineMS = 0;

	syncNextGameFrame = false;
	syncNextGameFrameAwaitingAsyncTicLog = false;
	cinematicStateValid = false;
	cinematicActive = false;
	mapSpawned = false;
	guiActive = NULL;
	demoReturnGui = NULL;
	demoOverlayVisible = false;
	demoBrowserMode = true;
	demoMenuOpenedOverPlayback = false;
	demoResumeAfterMenu = false;
	demoActiveType = DEMO_LIBRARY_UNKNOWN;
	aviCaptureMode = false;
	timeDemo = TD_NO;
	waitingOnBind = false;
	lastPacifierTime = 0;
	lastPacifierDrawMsec = 0;
	loadingAssetQueueActive = false;
	loadingAssetQueueTotal = 0;
	loadingAssetQueueLoaded = 0;
	loadingAssetQueueStartPct = 0.0f;
	iamTheDukeActive = false;
	
	msgRunning = false;
	guiMsgRestore = NULL;
	msgIgnoreButtons = false;
	menuIntroBlackoutActive = false;
	menuIntroBlackoutAwaitMenuMusic = false;
	menuIntroBlackoutFadeStart = -1;
	fallbackMenuStartTime = -1;

	bytesNeededForMapLoad = 0;

#if ID_CONSOLE_LOCK
	emptyDrawCount = 0;
#endif
	ClearWipe();

	loadGameList.Clear();
	modsList.Clear();
	demoLibrary.Clear();
	demoLibraryFilter = "all";
	demoSelectedPath.Clear();

	authEmitTimeout = 0;
	authWaitBox = false;
	memset( cdkey, 0, sizeof( cdkey ) );
	memset( xpkey, 0, sizeof( xpkey ) );
	cdkey_state = CDKEY_NA;
	xpkey_state = CDKEY_NA;

	authMsg.Clear();
}

/*
===============
idSessionLocal::idSessionLocal
===============
*/
idSessionLocal::idSessionLocal() {
	guiInGame = guiMainMenu = guiIntro \
		= guiRestartMenu = guiLoading = guiGameOver = guiActive \
		= guiTest = guiMsg = guiMsgRestore = guiTakeNotes = guiDemoMenu = NULL;
	guiDemoList = NULL;
	
	menuSoundWorld = NULL;
	
	Clear();
}

/*
===============
idSessionLocal::~idSessionLocal
===============
*/
idSessionLocal::~idSessionLocal() {
}

/*
===============
idSessionLocal::Stop

called on errors and game exits
===============
*/
void idSessionLocal::Stop() {
	StopInternal( false );
}

/*
===============
idSessionLocal::StopPreservingWipe

Used for a completed scene-to-menu fade whose held black frame must survive
world teardown until the destination GUI has received its reveal event.
===============
*/
void idSessionLocal::StopPreservingWipe() {
	StopInternal( true );
}

/*
===============
idSessionLocal::StopInternal

idAsyncServer::Kill re-enters Stop after marking the server inactive. Preserve
the outer stop's wipe policy across that nested call.
===============
*/
void idSessionLocal::StopInternal( bool preserveWipe ) {
	const bool outermostStop = stopDepth == 0;
	if ( outermostStop ) {
		preserveWipeDuringStop = preserveWipe;
	}
	stopDepth++;
	const auto finishStopPolicy = [this, outermostStop]() {
		stopDepth--;
		if ( outermostStop ) {
			preserveWipeDuringStop = false;
		}
	};

	try {
		if ( !preserveWipeDuringStop ) {
			ClearWipe();
		}
		if ( outermostStop && savegameFile != NULL ) {
			fileSystem->CloseFile( savegameFile );
			savegameFile = NULL;
			loadingSaveGame = false;
		}

		idAsyncNetwork::multiViewDemo.SessionStop();

		// clear mapSpawned and demo playing flags
		UnloadMap();

		// disconnect async client
		idAsyncNetwork::client.DisconnectFromServer();

		// kill async server
		idAsyncNetwork::server.Kill();

		if ( sw ) {
			sw->StopAllSounds();
		}

		insideUpdateScreen = false;
		insideExecuteMapChange = false;

		// drop all guis
		SetGUI( NULL, NULL );
	}
	catch( ... ) {
		// A recoverable drop during MapShutdown must not poison the policy for
		// later Stop calls after the outer exception is handled.
		finishStopPolicy();
		throw;
	}

	finishStopPolicy();
}

/*
===============
idSessionLocal::Shutdown
===============
*/
void idSessionLocal::Shutdown() {
	int i;

	if ( aviCaptureMode ) {
		EndAVICapture();
	}

	Stop();

	// Sound worlds must be released through soundSystem so the internal
	// soundWorlds registry is updated before soundSystem shutdown walks it.
	SetPlayingSoundWorld( NULL );

	if ( sw ) {
		soundSystem->FreeSoundWorld( sw );
		sw = NULL;
	}

	if ( menuSoundWorld ) {
		soundSystem->FreeSoundWorld( menuSoundWorld );
		menuSoundWorld = NULL;
	}

	if ( rw ) {
		delete rw;
		rw = NULL;
	}
		
	mapSpawnData.serverInfo.Clear();
	mapSpawnData.syncedCVars.Clear();
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.userInfo[i].Clear();
		mapSpawnData.persistentPlayerInfo[i].Clear();
	}

	if ( guiMainMenu_MapList != NULL ) {
		guiMainMenu_MapList->Shutdown();
		uiManager->FreeListGUI( guiMainMenu_MapList );
		guiMainMenu_MapList = NULL;
	}
#ifndef ID_DEDICATED
	arenaCampaign.Shutdown();
#endif
	ShutdownDemoSystem();

	Clear();
}

/*
===============
idSessionLocal::IsMultiplayer
===============
*/
bool	idSessionLocal::IsMultiplayer() {
	return idAsyncNetwork::IsActive();
}

bool idSessionLocal::IsIAmTheDukeActive( void ) const {
	return iamTheDukeActive && mapSpawned && !idAsyncNetwork::IsActive();
}

void idSessionLocal::ToggleIAmTheDuke( void ) {
	if ( idAsyncNetwork::IsActive() || !openQ4_IsSingleplayerGameType() ) {
		common->Printf( "iamtheduke is single-player only.\n" );
		return;
	}

	if ( !mapSpawned || !gameEdit || !gameEdit->PlayerIsValid() ) {
		common->Printf( "You must be in a single-player game to use this command.\n" );
		return;
	}

	idPlayer *player = static_cast<idPlayer *>( openQ4_FindSpawnedEntityByBaseClass( "idPlayer" ) );
	if ( player != NULL && player->health <= 0 ) {
		common->Printf( "You must be alive to use this command.\n" );
		return;
	}

	iamTheDukeActive = !iamTheDukeActive;
	common->Printf( "iamtheduke %s\n", iamTheDukeActive ? "ON" : "OFF" );
}

void idSessionLocal::DrawIAmTheDukeOverlay( void ) const {
	if ( !IsIAmTheDukeActive() ) {
		return;
	}

	static const char *lines[] = {
		"Mustin Jarshall",
		"",
		"NiceColdDuke"
	};

	const idMaterial *charSetMaterial = declManager->FindMaterial( "fonts/english/bigchars" );
	if ( charSetMaterial == NULL ) {
		return;
	}

	const float margin = 16.0f;
	const float maxLineChars = static_cast<float>( Session_CountVisibleSmallChars( lines[0] ) );
	if ( maxLineChars <= 0.0f ) {
		return;
	}

	const float charWidth = idMath::Floor( ( SCREEN_WIDTH - margin * 2.0f ) / maxLineChars );
	const float charHeight = idMath::Floor( Min( ( SCREEN_HEIGHT - margin * 2.0f ) / 3.0f, charWidth * 3.5f ) );
	const float lineAdvance = charHeight;
	const float totalHeight = lineAdvance * 3.0f;
	const float startY = idMath::Floor( ( SCREEN_HEIGHT - totalHeight ) * 0.5f );
	const idVec4 outlineColor( 0.0f, 0.0f, 0.0f, 0.95f );
	const idVec4 mainColor( 0.96f, 0.99f, 1.0f, 1.0f );

	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );

	for ( int i = 0; i < 3; i++ ) {
		const char *line = lines[ i ];
		if ( line[ 0 ] == '\0' ) {
			continue;
		}

		const float visibleChars = static_cast<float>( Session_CountVisibleSmallChars( line ) );
		const float lineWidth = visibleChars * charWidth;
		const float x = idMath::Floor( ( SCREEN_WIDTH - lineWidth ) * 0.5f );
		const float y = startY + lineAdvance * static_cast<float>( i );
		Session_DrawOutlinedScaledString( x, y, charWidth, charHeight, line, mainColor, outlineColor, charSetMaterial );
	}

	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

/*
================
idSessionLocal::StartWipe

Draws and captures the current state, then starts a wipe with that image
================
*/
void idSessionLocal::StartWipe( const char *_wipeMaterial, bool hold ) {
#ifdef ID_DEDICATED
	// Dedicated servers never own a presentation surface to capture for wipes.
	return;
#endif
	console->Close();

	// render the current screen into a texture for the wipe model
	renderSystem->CropRenderSize( 640, 480, true );

	Draw();

	renderSystem->CaptureRenderToImage( "_scratch");
	renderSystem->UnCrop();

	wipeMaterial = declManager->FindMaterial( _wipeMaterial, false );

	wipeStartTic = com_ticNumber;
	const int wipeDurationMsec = idMath::Ftoi( com_wipeSeconds.GetFloat() * 1000.0f + 0.5f );
	wipeStopTic = wipeStartTic + common->GetUserCmdTicsForMsecCeil( wipeDurationMsec );
	wipeHold = hold;
}

/*
================
idSessionLocal::CompleteWipe
================
*/
void idSessionLocal::CompleteWipe() {
#ifdef ID_DEDICATED
	return;
#endif
	if ( com_ticNumber == 0 ) {
		// if the async thread hasn't started, we would hang here
		wipeStopTic = 0;
		UpdateScreen( true );
		return;
	}
	while ( com_ticNumber < wipeStopTic ) {
#if ID_CONSOLE_LOCK
		emptyDrawCount = 0;
#endif
		Session_BeginBlockingLoadPresentationFrame();
		UpdateScreen( true );
	}
}

/*
================
idSessionLocal::ShowLoadingGui
================
*/
void idSessionLocal::ShowLoadingGui() {
#ifdef ID_DEDICATED
	// Dedicated servers have no loading GUI or initialized renderer. Map loads
	// must continue directly instead of entering the client presentation loop.
	return;
#endif
	if ( com_ticNumber == 0 ) {
		return;
	}
	console->Close();

	// introduced in D3XP code. don't think it actually fixes anything, but doesn't hurt either
#if 1
	// Try and prevent the while loop from being skipped over (long hitch on the main thread?)
	const int minShowMsec = idMath::ClampInt( 0, 2000, com_minLoadingGuiMsec.GetInteger() );
	int stop = Sys_Milliseconds() + minShowMsec;
	int force = minShowMsec > 0 ? 2 : 1;
	while ( Sys_Milliseconds() < stop || force-- > 0 ) {
		openQ4_BeginPresentationFrame();
		com_frameTime = common->GetUserCmdTime( com_ticNumber );
		session->Frame();
		session->UpdateScreen( false );
	}
#else
	int stop = com_ticNumber + common->GetUserCmdTicsForMsecCeil( 1000 );
	while ( com_ticNumber < stop ) {
		openQ4_BeginPresentationFrame();
		com_frameTime = common->GetUserCmdTime( com_ticNumber );
		session->Frame();
		session->UpdateScreen( false );
	}
#endif
}



/*
================
idSessionLocal::ClearWipe
================
*/
void idSessionLocal::ClearWipe( void ) {
	wipeHold = false;
	wipeStopTic = 0;
	wipeStartTic = wipeStopTic + 1;
}

/*
================
Session_TestGUI_f
================
*/
static void Session_TestGUI_f( const idCmdArgs &args ) {
	sessLocal.TestGUI( args.Argv(1) );
}

#ifndef ID_DEDICATED
// This diagnostic exercises the same semantic browser actions as the menu.
// It neither synthesizes input events nor accepts arbitrary console commands.
static idStr Session_BrowserDiagnosticText( const char *source ) {
	idStr text = source != NULL ? source : "";
	for ( int i = 0; i < text.Length(); ++i ) {
		const unsigned char value = static_cast<unsigned char>( text[ i ] );
		if ( ( value < 32 && value != '\t' ) || value == 127 ) {
			text[ i ] = ' ';
		}
	}
	return text;
}

static void Session_OpenQ4Browser_f( const idCmdArgs &args ) {
	idUserInterface *gui = session->GetActiveGUI();
	if ( gui == NULL || gui != sessLocal.guiMainMenu || gui != sessLocal.guiActive ) {
		common->Printf( "openq4_browser requires the active main menu\n" );
		return;
	}
	const char *verb = args.Argv( 1 );
	if ( args.Argc() == 2 && !idStr::Icmp( verb, "open" ) ) {
		gui->HandleNamedEvent( "main_to_browser" );
		return;
	}
	if ( args.Argc() == 2 && !idStr::Icmp( verb, "report" ) ) {
		int visible = 0;
		const int maximumRows = Min( idAsyncNetwork::client.serverList.Num(), 4096 );
		while ( visible < maximumRows && gui->State().GetString(
			va( "serverList_item_%d", visible ) )[ 0 ] != '\0' ) {
			++visible;
		}
		idStr selectedAddress;
		const bool selected = idAsyncNetwork::client.serverList.GetSelectedAddress( selectedAddress );
		const int reportRows = Min( visible, 256 );
		common->Printf( "BROWSER_STATE discovered=%d visible=%d selected=%d selected_row=%d lan=%d scan=%d truncated=%d\n",
			idAsyncNetwork::client.serverList.Num(), visible, selected,
			gui->State().GetInt( "serverList_sel_0", "-1" ),
			gui->State().GetBool( "lanSet" ),
			static_cast<int>( idAsyncNetwork::client.serverList.GetState() ), visible > reportRows );
		for ( int i = 0; i < reportRows; ++i ) {
			const idStr row = Session_BrowserDiagnosticText(
				gui->State().GetString( va( "serverList_item_%d", i ) ) );
			common->Printf( "BROWSER_ROW index=%d text=%s\n", i, row.c_str() );
		}
		static const char *const details[] = {
			"browser_selected", "server_IP", "server_name", "server_map",
			"server_gameType", "server_passworded", "browser_levelshot",
			"loadNotice", "sortNotice", "filterMod", "browser_count"
		};
		for ( int i = 0; i < static_cast<int>( sizeof( details ) / sizeof( details[ 0 ] ) ); ++i ) {
			const idStr value = Session_BrowserDiagnosticText( gui->State().GetString( details[ i ] ) );
			common->Printf( "BROWSER_DETAIL key=%s value=%s\n", details[ i ], value.c_str() );
		}
		static const char *const windows[] = {
			"desktop::curr", "desktop::dest", "desktop::active", "desktop::blockBrowser",
			"p_main::visible", "p_mp_browse::visible", "p_mp_browse::rect",
			"anim_mainOut::notime", "anim_mpBrowseIn::notime"
		};
		for ( int i = 0; i < static_cast<int>( sizeof( windows ) / sizeof( windows[ 0 ] ) ); ++i ) {
			const idWinVar *variable = gui->GetDesktop() != NULL ?
				gui->GetDesktop()->GetWinVarByName( windows[ i ], true ) : NULL;
			const idStr value = Session_BrowserDiagnosticText( variable != NULL ? variable->c_str() : "" );
			common->Printf( "BROWSER_WINDOW key=%s found=%d value=%s\n",
				windows[ i ], variable != NULL, value.c_str() );
		}
		return;
	}
	if ( args.Argc() == 3 && !idStr::Icmp( verb, "source" ) ) {
		if ( idStr::Icmp( args.Argv( 2 ), "lan" ) && idStr::Icmp( args.Argv( 2 ), "internet" ) ) {
			common->Printf( "openq4_browser source requires lan or internet\n" );
			return;
		}
		gui->SetStateBool( "lanSet", !idStr::Icmp( args.Argv( 2 ), "lan" ) );
		gui->HandleNamedEvent( "showMpBrowse" );
		gui->StateChanged( common->GetPresentationTime() );
		return;
	}
	if ( args.Argc() == 3 && !idStr::Icmp( verb, "select" ) ) {
		const char *number = args.Argv( 2 );
		int row = -1;
		bool valid = !idStr::Cmp( number, "-1" );
		if ( !valid && number[ 0 ] != '\0' ) {
			row = 0;
			valid = true;
			for ( int i = 0; number[ i ] != '\0'; ++i ) {
				if ( number[ i ] < '0' || number[ i ] > '9' || row > 409 ) {
					valid = false;
					break;
				}
				row = row * 10 + number[ i ] - '0';
			}
		}
		if ( !valid || row >= 4096 || ( row >= 0 &&
			( row >= idAsyncNetwork::client.serverList.Num() ||
			  gui->State().GetString( va( "serverList_item_%d", row ) )[ 0 ] == '\0' ) ) ) {
			common->Printf( "openq4_browser select requires an existing visible row or -1\n" );
			return;
		}
		gui->SetStateInt( "serverList_sel_0", row );
		idAsyncNetwork::client.serverList.GUIUpdateSelected();
		gui->StateChanged( common->GetPresentationTime() );
		return;
	}
	if ( args.Argc() == 3 && !idStr::Icmp( verb, "action" ) ) {
		static const char *const actions[] = {
			"InitServerBrowser", "updateServers", "refreshServers", "FilterServers",
			"click_serverList", "connect", "toggleFavorite", "sortFavorite",
			"sortServerName", "sortPlayers", "sortPing", "sortGameType", "sortMap",
			"sortGame", "sortLocked", "sortDed", "sortPB", "sortRepeater",
			"server_clearSort", "filterByNextMod", "filterByPrevMod", "updateFilterByMod"
		};
		for ( int i = 0; i < static_cast<int>( sizeof( actions ) / sizeof( actions[ 0 ] ) ); ++i ) {
			if ( !idStr::Icmp( args.Argv( 2 ), actions[ i ] ) ) {
				sessLocal.HandleMainMenuCommands( actions[ i ] );
				return;
			}
		}
		common->Printf( "openq4_browser action is not a supported browser operation\n" );
		return;
	}
	common->Printf( "usage: openq4_browser open | report | source <lan|internet> | select <visible-row|-1> | action <browser-operation>\n" );
}

static void Session_OpenQ4GuiGet_f( const idCmdArgs &args ) {
	idUserInterface *gui = session->GetActiveGUI();
	if ( args.Argc() != 2 || gui == NULL || gui->GetDesktop() == NULL ) {
		common->Printf( "usage: openq4_guiGet <window::variable> with an active GUI\n" );
		return;
	}
	idWinVar *variable = gui->GetDesktop()->GetWinVarByName( args.Argv( 1 ), true );
	if ( variable == NULL ) {
		common->Printf( "openq4_guiGet: unknown GUI variable\n" );
		return;
	}
	common->Printf( "GUI_VALUE %s=%s\n", args.Argv( 1 ), variable->c_str() );
}

static void Session_OpenQ4GuiSet_f( const idCmdArgs &args ) {
	idUserInterface *gui = session->GetActiveGUI();
	if ( args.Argc() != 3 || gui == NULL || gui->GetDesktop() == NULL ) {
		common->Printf( "usage: openq4_guiSet <window::variable> <value> with an active GUI\n" );
		return;
	}
	idWinVar *variable = gui->GetDesktop()->GetWinVarByName( args.Argv( 1 ), true );
	if ( variable == NULL ) {
		common->Printf( "openq4_guiSet: unknown GUI variable\n" );
		return;
	}
	variable->Set( args.Argv( 2 ) );
	gui->StateChanged( common->GetPresentationTime() );
}

static void Session_GuiEvent_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		return;
	}

	idUserInterface *activeGui = session->GetActiveGUI();
	if ( activeGui == NULL ) {
		return;
	}

	const char *eventName = args.Args( 1, -1 );
	if ( eventName == NULL || eventName[0] == '\0' ) {
		return;
	}

	activeGui->HandleNamedEvent( eventName );
}
#endif

/*
================
idSessionLocal::TestGUI
================
*/
void idSessionLocal::TestGUI( const char *guiName ) {
	if ( guiName && *guiName ) {
		guiTest = uiManager->FindGui( guiName, true, false, true );
	} else {
		guiTest = NULL;
	}
}

/*
================
FindUnusedFileName
================
*/
static idStr FindUnusedFileName( const char *format ) {
	int i;
	char	filename[1024];

	for ( i = 0 ; i < 999 ; i++ ) {
		idStr::snPrintf( filename, sizeof( filename ), format, i );
		int len = fileSystem->ReadFile( filename, NULL, NULL );
		if ( len <= 0 ) {
			return filename;	// file doesn't exist
		}
	}

	return filename;
}

/*
================
Session_DemoShot_f
================
*/
static void Session_DemoShot_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		idStr filename = FindUnusedFileName( "demos/shot%03i.demo" );
		sessLocal.DemoShot( filename );
	} else {
		sessLocal.DemoShot( va( "demos/shot_%s.demo", args.Argv(1) ) );
	}
}

#ifndef ID_DEDICATED
/*
================
Session_RecordDemo_f
================
*/
static void Session_RecordDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		idStr filename = FindUnusedFileName( "demos/demo%03i.demo" );
		sessLocal.StartRecordingRenderDemo( filename );
	} else {
		sessLocal.StartRecordingRenderDemo( va( "demos/%s.demo", args.Argv(1) ) );
	}
}

/*
================
Session_CompressDemo_f
================
*/
static void Session_CompressDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() == 2 ) {
		sessLocal.CompressDemoFile( "2", args.Argv(1) );
	} else if ( args.Argc() == 3 ) {
		sessLocal.CompressDemoFile( args.Argv(2), args.Argv(1) );
	} else {
		common->Printf("use: CompressDemo <file> [scheme]\nscheme is the same as com_compressDemo, defaults to 2" );
	}
}

/*
================
Session_StopRecordingDemo_f
================
*/
static void Session_StopRecordingDemo_f( const idCmdArgs &args ) {
	sessLocal.StopRecordingRenderDemo();
}

/*
================
Session_PlayDemo_f
================
*/
static void Session_PlayDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() >= 2 ) {
		sessLocal.StartPlayingRenderDemo( va( "demos/%s", args.Argv(1) ) );
	}
}

/*
================
Session_TimeDemo_f
================
*/
static void Session_TimeDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() >= 2 ) {
		sessLocal.TimeRenderDemo( va( "demos/%s", args.Argv(1) ), ( args.Argc() > 2 ) );
	}
}

/*
================
Session_TimeDemoQuit_f
================
*/
static void Session_TimeDemoQuit_f( const idCmdArgs &args ) {
	sessLocal.TimeRenderDemo( va( "demos/%s", args.Argv(1) ) );
	if ( sessLocal.timeDemo == TD_YES ) {
		// this allows hardware vendors to automate some testing
		sessLocal.timeDemo = TD_YES_THEN_QUIT;
	}
}

/*
================
Session_AVIDemo_f
================
*/
static void Session_AVIDemo_f( const idCmdArgs &args ) {
	sessLocal.AVIRenderDemo( va( "demos/%s", args.Argv(1) ) );
}

/*
================
Session_AVIGame_f
================
*/
static void Session_AVIGame_f( const idCmdArgs &args ) {
	sessLocal.AVIGame( args.Argv(1) );
}

/*
================
Session_AVICmdDemo_f
================
*/
static void Session_AVICmdDemo_f( const idCmdArgs &args ) {
	sessLocal.AVICmdDemo( args.Argv(1) );
}

/*
================
Session_WriteCmdDemo_f
================
*/
static void Session_WriteCmdDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() == 1 ) {
		idStr	filename = FindUnusedFileName( "demos/cmdDemo%03i.cdemo" );
		sessLocal.WriteCmdDemo( filename );
	} else if ( args.Argc() == 2 ) {
		sessLocal.WriteCmdDemo( va( "demos/%s.cdemo", args.Argv( 1 ) ) );
	} else {
		common->Printf( "usage: writeCmdDemo [demoName]\n" );
	}
}

/*
================
Session_PlayCmdDemo_f
================
*/
static void Session_PlayCmdDemo_f( const idCmdArgs &args ) {
	sessLocal.StartPlayingCmdDemo( args.Argv(1) );
}

/*
================
Session_TimeCmdDemo_f
================
*/
static void Session_TimeCmdDemo_f( const idCmdArgs &args ) {
	sessLocal.TimeCmdDemo( args.Argv(1) );
}
#endif

/*
================
Session_Disconnect_f
================
*/
static void Session_Disconnect_f( const idCmdArgs &args ) {
	arenaCampaign.AbortMatch();
	sessLocal.Stop();
	sessLocal.StartMenu();
	if ( soundSystem ) {
		soundSystem->SetMute( false );
	}
}

#ifdef ID_DEMO_BUILD
/*
================
Session_EndOfDemo_f
================
*/
static void Session_EndOfDemo_f( const idCmdArgs &args ) {
	sessLocal.Stop();
	sessLocal.StartMenu();
	if ( soundSystem ) {
		soundSystem->SetMute( false );
	}
	if ( sessLocal.guiActive ) {
		sessLocal.guiActive->HandleNamedEvent( "endOfDemo" );
	}
}
#endif

#ifndef ID_DEDICATED
/*
================
Session_ExitCmdDemo_f
================
*/
static void Session_ExitCmdDemo_f( const idCmdArgs &args ) {
	if ( !sessLocal.cmdDemoFile ) {
		common->Printf( "not reading from a cmdDemo\n" );
		return;
	}
	fileSystem->CloseFile( sessLocal.cmdDemoFile );
	common->Printf( "Command demo exited at logIndex %i\n", sessLocal.logIndex );
	sessLocal.cmdDemoFile = NULL;
}
#endif

/*
================
idSessionLocal::StartRecordingRenderDemo
================
*/
void idSessionLocal::StartRecordingRenderDemo( const char *demoName ) {
	if ( writeDemo ) {
		// allow it to act like a toggle
		StopRecordingRenderDemo();
		return;
	}

	if ( !demoName[0] ) {
		common->Printf( "idSessionLocal::StartRecordingRenderDemo: no name specified\n" );
		return;
	}

	console->Close();

	writeDemo = new idDemoFile;
	if ( !writeDemo->OpenForWriting( demoName ) ) {
		common->Printf( "error opening %s\n", demoName );
		delete writeDemo;
		writeDemo = NULL;
		return;
	}

	common->Printf( "recording to %s\n", writeDemo->GetName() );

	writeDemo->WriteInt( DS_VERSION );
	writeDemo->WriteInt( OPENQ4_RENDERDEMO_CURRENT_VERSION );

	// if we are in a map already, dump the current state
	sw->StartWritingDemo( writeDemo );
	rw->StartWritingDemo( writeDemo );
}

/*
================
idSessionLocal::StopRecordingRenderDemo
================
*/
void idSessionLocal::StopRecordingRenderDemo() {
	if ( !writeDemo ) {
		common->Printf( "idSessionLocal::StopRecordingRenderDemo: not recording\n" );
		return;
	}
	sw->StopWritingDemo();
	rw->StopWritingDemo();

	writeDemo->Close();
	common->Printf( "stopped recording %s.\n", writeDemo->GetName() );
	delete writeDemo;
	writeDemo = NULL;
}

/*
================
idSessionLocal::StopPlayingRenderDemo

Reports timeDemo numbers and finishes any avi recording
================
*/
void idSessionLocal::StopPlayingRenderDemo() {
	FinishRenderDemoSeek();
	if ( !readDemo ) {
		timeDemo = TD_NO;
		return;
	}

	// Record the stop time before doing anything that could be time consuming 
	int timeDemoStopTime = Sys_Milliseconds();

	EndAVICapture();

	readDemo->Close();

	sw->StopAllSounds();
	SetPlayingSoundWorld( menuSoundWorld );

	common->Printf( "stopped playing %s.\n", readDemo->GetName() );
	delete readDemo;
	readDemo = NULL;
	activeRenderDemoName.Clear();
	renderDemoPaused = false;
	renderDemoSeeking = false;
	renderDemoStepPending = false;
	renderDemoFramePayloadPending = false;
	renderDemoPlaybackRate = 1.0f;
	renderDemoFrameAccumulator = 0.0;
	if ( sw != NULL ) {
		sw->SetSlowmoSpeed( 1.0f );
		if ( sw->IsPaused() ) {
			sw->UnPause();
		}
	}

	if ( timeDemo ) {
		// report the stats
		float	demoSeconds = ( timeDemoStopTime - timeDemoStartTime ) * 0.001f;
		float	demoFPS = numDemoFrames / demoSeconds;
		idStr	message = va( "%i frames rendered in %3.1f seconds = %3.1f fps\n", numDemoFrames, demoSeconds, demoFPS );

		common->Printf( "%s", message.c_str() );
		if ( timeDemo == TD_YES_THEN_QUIT ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		} else {
			soundSystem->SetMute( true );
			MessageBox( MSG_OK, message, "Time Demo Results", true );
			soundSystem->SetMute( false );
		}
		timeDemo = TD_NO;
	}
}

/*
================
idSessionLocal::DemoShot

A demoShot is a single frame demo
================
*/
void idSessionLocal::DemoShot( const char *demoName ) {
	StartRecordingRenderDemo( demoName );

	// force draw one frame
	UpdateScreen();

	StopRecordingRenderDemo();
}

/*
================
idSessionLocal::StartPlayingRenderDemo
================
*/
void idSessionLocal::StartPlayingRenderDemo( idStr demoName ) {
	if ( !demoName[0] ) {
		common->Printf( "idSessionLocal::StartPlayingRenderDemo: no name specified\n" );
		return;
	}
	demoName.DefaultFileExtension( ".demo" );
	const int cachedDuration =
		!renderDemoDurationName.IsEmpty() && !renderDemoDurationName.Icmp( demoName )
			? renderDemoKnownDurationMS : -1;
	const bool restoreDemoMenu = guiDemoMenu != NULL && guiActive == guiDemoMenu;
	const bool restoreMuteOnFailure = soundSystem->IsMuted();

	// make sure localSound / GUI intro music shuts up
	sw->StopAllSounds();
	sw->PlayShaderDirectly( "", 0 );	
	menuSoundWorld->StopAllSounds();
	menuSoundWorld->PlayShaderDirectly( "", 0 );

	// exit any current game
	Stop();

	// automatically put the console away
	console->Close();

	// bring up the loading screen manually, since demos won't
	// call ExecuteMapChange()
// jmarshall - quake 4 loading gui
	guiLoading = uiManager->FindGui( "guis/loading/generic.gui", true, false, true );
// jmarshall end
	guiLoading->SetStateString( "demo", common->GetLanguageDict()->GetString( "#str_02087" ) );
	readDemo = new idDemoFile;
	if ( !readDemo->OpenForReading( demoName ) ) {
		common->Printf( "couldn't open %s\n", demoName.c_str() );
		delete readDemo;
		readDemo = NULL;
		Stop();
		StartMenu();
		soundSystem->SetMute( restoreMuteOnFailure );
		return;
	}
	activeRenderDemoName = demoName;
	renderDemoPaused = false;
	renderDemoSeekRestartPending = false;
	renderDemoSeekRestartPresentationPending = false;
	renderDemoSeekWorkDeferred = false;
	renderDemoStepPending = false;
	renderDemoFramePayloadPending = false;
	renderDemoPlaybackRate = 1.0f;
	renderDemoFrameAccumulator = 0.0;
	renderDemoStartViewTime = 0;
	renderDemoKnownDurationMS = cachedDuration;
	renderDemoSeekDeadlineMS = 0;
	SetPlayingSoundWorld( sw );
	if ( sw != NULL ) {
		sw->SetSlowmoSpeed( 1.0f );
		if ( sw->IsPaused() ) {
			sw->UnPause();
		}
	}

	insideExecuteMapChange = true;
	UpdateScreen();
	insideExecuteMapChange = false;
	guiLoading->SetStateString( "demo", "" );

	// setup default render demo settings
	// that's default for <= legacy engine v1.1
	renderdemoVersion = 1;
	savegameVersion = 16;

	numDemoFrames = 0;
	memset( &currentDemoRenderView, 0, sizeof( currentDemoRenderView ) );
	// The first successfully decoded render view replaces this sentinel.
	// It lets the EOF compatibility path distinguish a complete legacy
	// demoShot from a truncated stream that never supplied a usable view.
	demoTimeOffset = idMath::INT_MIN;
	AdvanceRenderDemo( true );
	if ( readDemo == NULL || numDemoFrames != 1 ||
		 demoTimeOffset == idMath::INT_MIN ) {
		if ( readDemo != NULL ) {
			common->Warning( "Render demo '%s' did not contain a complete first frame",
				demoName.c_str() );
			Stop();
			StartMenu( false );
		}
		return;
	}
	renderDemoStartViewTime = currentDemoRenderView.time;

	lastDemoTic = -1;
	timeDemoStartTime = Sys_Milliseconds();
	if ( restoreDemoMenu && guiDemoMenu != NULL ) {
		// The menu chooses its visual mode in onActivate, so publish the current
		// playback state before the synchronous SetGUI activation.
		UpdateDemoMenuGui();
		SetGUI( guiDemoMenu, NULL );
		UpdateDemoMenuGui();
	}
}

/*
================
idSessionLocal::TimeRenderDemo
================
*/
void idSessionLocal::TimeRenderDemo( const char *demoName, bool twice ) {
	idStr demo = demoName;
	const bool restoreMuteOnFailure = soundSystem->IsMuted();
	
	// no sound in time demos
	soundSystem->SetMute( true );

	StartPlayingRenderDemo( demo );
	
	if ( twice && readDemo ) {
		// cycle through once to precache everything
		guiLoading->SetStateString( "demo", common->GetLanguageDict()->GetString( "#str_04852" ) );
		guiLoading->StateChanged( common->GetPresentationTime() );
		while ( readDemo ) {
			insideExecuteMapChange = true;
			UpdateScreen();
			insideExecuteMapChange = false;
			AdvanceRenderDemo( true );
		}
		guiLoading->SetStateString( "demo", "" );
		StartPlayingRenderDemo( demo );
	}
	

	if ( !readDemo ) {
		soundSystem->SetMute( restoreMuteOnFailure );
		return;
	}

	timeDemo = TD_YES;
}


/*
================
idSessionLocal::BeginAVICapture
================
*/
void idSessionLocal::BeginAVICapture( const char *demoName ) {
	idStr name = demoName;
	name.ExtractFileBase( aviDemoShortName );
	aviCaptureMode = true;
	aviDemoFrameCount = 0;
	aviTicStart = 0;
	sw->AVIOpen( va( "demos/%s/", aviDemoShortName.c_str() ), aviDemoShortName.c_str() );
}

/*
================
idSessionLocal::EndAVICapture
================
*/
void idSessionLocal::EndAVICapture() {
	if ( !aviCaptureMode ) {
		return;
	}

	sw->AVIClose();

	// write a .roqParam file so the demo can be converted to a roq file
	idFile *f = fileSystem->OpenFileWrite( va( "demos/%s/%s.roqParam", 
		aviDemoShortName.c_str(), aviDemoShortName.c_str() ) );
	f->Printf( "INPUT_DIR demos/%s\n", aviDemoShortName.c_str() );
	f->Printf( "FILENAME demos/%s/%s.RoQ\n", aviDemoShortName.c_str(), aviDemoShortName.c_str() );
	f->Printf( "\nINPUT\n" );
	f->Printf( "%s_*.tga [00000-%05i]\n", aviDemoShortName.c_str(), (int)( aviDemoFrameCount-1 ) );
	f->Printf( "END_INPUT\n" );
	delete f;

	common->Printf( "captured %i frames for %s.\n", ( int )aviDemoFrameCount, aviDemoShortName.c_str() );

	aviCaptureMode = false;
}


/*
================
idSessionLocal::AVIRenderDemo
================
*/
void idSessionLocal::AVIRenderDemo( const char *_demoName ) {
	idStr	demoName = _demoName;	// copy off from va() buffer

	StartPlayingRenderDemo( demoName );
	if ( !readDemo ) {
		return;
	}

	BeginAVICapture( demoName.c_str() ) ;

	// I don't understand why I need to do this twice, something
	// strange with the nvidia swapbuffers?
	UpdateScreen();
}

/*
================
idSessionLocal::AVICmdDemo
================
*/
void idSessionLocal::AVICmdDemo( const char *demoName ) {
	StartPlayingCmdDemo( demoName );

	BeginAVICapture( demoName ) ;
}

/*
================
idSessionLocal::AVIGame

Start AVI recording the current game session
================
*/
void idSessionLocal::AVIGame( const char *demoName ) {
	if ( aviCaptureMode ) {
		EndAVICapture();
		return;
	}

	if ( !mapSpawned ) {
		common->Printf( "No map spawned.\n" );
	}

	if ( !demoName || !demoName[0] ) {
		idStr filename = FindUnusedFileName( "demos/game%03i.game" );
		demoName = filename.c_str();

		// write a one byte stub .game file just so the FindUnusedFileName works,
		fileSystem->WriteFile( demoName, demoName, 1 );
	}

	BeginAVICapture( demoName ) ;
}

/*
================
idSessionLocal::CompressDemoFile
================
*/
void idSessionLocal::CompressDemoFile( const char *scheme, const char *demoName ) {
	idStr	fullDemoName = "demos/";
	fullDemoName += demoName;
	fullDemoName.DefaultFileExtension( ".demo" );
	idStr compressedName = fullDemoName;
	compressedName.StripFileExtension();
	compressedName.Append( "_compressed.demo" );

	int savedCompression = cvarSystem->GetCVarInteger("com_compressDemos");
	bool savedPreload = cvarSystem->GetCVarBool("com_preloadDemos");
	cvarSystem->SetCVarBool( "com_preloadDemos", false );
	cvarSystem->SetCVarInteger("com_compressDemos", atoi(scheme) );

	idDemoFile demoread, demowrite;
	if ( !demoread.OpenForReading( fullDemoName ) ) {
		common->Printf( "Could not open %s for reading\n", fullDemoName.c_str() );
		return;
	}
	if ( !demowrite.OpenForWriting( compressedName ) ) {
		common->Printf( "Could not open %s for writing\n", compressedName.c_str() );
		demoread.Close();
		cvarSystem->SetCVarBool( "com_preloadDemos", savedPreload );
		cvarSystem->SetCVarInteger("com_compressDemos", savedCompression);
		return;
	}
	common->SetRefreshOnPrint( true );
	common->Printf( "Compressing %s to %s...\n", fullDemoName.c_str(), compressedName.c_str() );

	static const int bufferSize = 65535;
	char buffer[bufferSize];
	int bytesRead;
	while ( 0 != (bytesRead = demoread.Read( buffer, bufferSize ) ) ) {
		demowrite.Write( buffer, bytesRead );
		common->Printf( "." );
	}

	demoread.Close();
	demowrite.Close();

	cvarSystem->SetCVarBool( "com_preloadDemos", savedPreload );
	cvarSystem->SetCVarInteger("com_compressDemos", savedCompression);

	common->Printf( "Done\n" );
	common->SetRefreshOnPrint( false );

}


/*
===============
idSessionLocal::StartNewGame
===============
*/
void idSessionLocal::StartNewGame( const char *mapName, bool devmap, const char *entityFilter ) {
#ifdef	ID_DEDICATED
	common->Printf( "Dedicated servers cannot start singleplayer games.\n" );
	return;
#else
	idStr normalizedMapName;
	idStr normalizedEntityFilter;
	Session_NormalizeMapPathAndEntityFilter( mapName, entityFilter, normalizedMapName, normalizedEntityFilter );
	if ( normalizedMapName.Length() == 0 ) {
		return;
	}

	if ( idAsyncNetwork::server.IsActive() ) {
		common->Printf("Server running, use si_map / serverMapRestart\n");
		return;
	}
	if ( idAsyncNetwork::client.IsActive() ) {
		common->Printf("Client running, disconnect from server first\n");
		return;
	}

	const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );
	if ( idStr::Icmp( activeModule, "game_sp" ) != 0 ) {
		cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
		cvarSystem->SetCVarString( "com_nextGameModule", "game_sp" );
		idCmdArgs reloadArgs;
		reloadArgs.AppendArg( "openq4_startSingleplayer" );
		reloadArgs.AppendArg( normalizedMapName.c_str() );
		reloadArgs.AppendArg( devmap ? "1" : "0" );
		if ( normalizedEntityFilter.Length() > 0 ) {
			reloadArgs.AppendArg( normalizedEntityFilter.c_str() );
		}
		cmdSystem->SetupReloadGameModule( reloadArgs );
		return;
	}

	// clear the userInfo so the player starts out with the defaults
	mapSpawnData.userInfo[0].Clear();
	mapSpawnData.persistentPlayerInfo[0].Clear();
	mapSpawnData.userInfo[0] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );

	mapSpawnData.serverInfo.Clear();
	mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	mapSpawnData.serverInfo.Set( "si_gameType", "singleplayer" );
	Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, normalizedEntityFilter.c_str() );

	// set the devmap key so any play testing items will be given at
	// spawn time to set approximately the right weapons and ammo
	if(devmap) {
		mapSpawnData.serverInfo.Set( "devmap", "1" );
	}

	mapSpawnData.syncedCVars.Clear();
	mapSpawnData.syncedCVars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );

	MoveToNewMap( normalizedMapName.c_str() );
	if ( com_WriteSingleDeclFile.GetBool() ) {
		Frame();
		UpdateScreen( true );
		declManager->WriteDeclFile();
	}
#endif
}

/*
===============
idSessionLocal::GetAutoSaveName
===============
*/
idStr idSessionLocal::GetAutoSaveName( const char *mapName ) const {
	const char *entityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	if ( entityFilter[0] != '\0' ) {
		return va( "Autosave %s %s", mapName, entityFilter );
	}
	return va( "Autosave %s", mapName );
}

/*
===============
idSessionLocal::MoveToNewMap

Leaves the existing userinfo and serverinfo
===============
*/
void idSessionLocal::MoveToNewMap( const char *mapName ) {
	idStr normalizedMapName;
	idStr embeddedEntityFilter;
	// Preserve filters already chosen by map/devmap/session-command callers.
	Session_NormalizeMapPathAndEntityFilter( mapName, "", normalizedMapName, embeddedEntityFilter, false );
	if ( normalizedMapName.Length() == 0 ) {
		return;
	}
	if ( embeddedEntityFilter.Length() > 0 ) {
		Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, embeddedEntityFilter.c_str() );
	}
	mapSpawnData.serverInfo.Set( "si_map", normalizedMapName.c_str() );

	ExecuteMapChange();

	if ( !mapSpawnData.serverInfo.GetBool("devmap") ) {
		// Autosave at the beginning of the level
		SaveGame( NULL, ST_AUTO );
	}

	SetGUI( NULL, NULL );
}

/*
==============
SaveCmdDemoFromFile
==============
*/
void idSessionLocal::SaveCmdDemoToFile( idFile *file ) {

	mapSpawnData.serverInfo.WriteToFileHandle( file );

	for ( int i = 0 ; i < MAX_ASYNC_CLIENTS ; i++ ) {
		mapSpawnData.userInfo[i].WriteToFileHandle( file );
		mapSpawnData.persistentPlayerInfo[i].WriteToFileHandle( file );
	}

	file->Write( &mapSpawnData.mapSpawnUsercmd, sizeof( mapSpawnData.mapSpawnUsercmd ) );

	if ( numClients < 1 ) {
		numClients = 1;
	}
	file->Write( loggedUsercmds, numClients * logIndex * sizeof( loggedUsercmds[0] ) );
}

/*
==============
idSessionLocal::LoadCmdDemoFromFile
==============
*/
static bool Session_ReadCmdDemoCString(
	idFile *file,
	idStr &string,
	const int headerStart,
	idStr &error ) {
	string.Clear();
	for ( int len = 0; len < MAX_STRING_CHARS; len++ ) {
		if ( file->Tell() < headerStart ||
			 file->Tell() - headerStart >= SESSION_MAX_CMD_DEMO_HEADER_BYTES ) {
			error = "command-demo header exceeds the supported size";
			string.Clear();
			return false;
		}
		char ch = '\0';
		if ( file->Read( &ch, 1 ) != 1 ) {
			error = "truncated command-demo dictionary string";
			string.Clear();
			return false;
		}
		if ( ch == '\0' ) {
			return true;
		}
		string += ch;
	}
	error = "unterminated command-demo dictionary string";
	string.Clear();
	return false;
}

static bool Session_ReadCmdDemoDict(
	idFile *file,
	idDict &dict,
	const int headerStart,
	int &totalKeyValues,
	idStr &error ) {
	dict.Clear();
	int count = 0;
	if ( file->ReadInt( count ) != sizeof( count ) ) {
		error = "truncated command-demo dictionary count";
		return false;
	}
	if ( count < 0 || count > SESSION_MAX_CMD_DEMO_DICT_KV ||
		 totalKeyValues > SESSION_MAX_CMD_DEMO_TOTAL_KV - count ) {
		error = va( "invalid command-demo dictionary count %d", count );
		return false;
	}
	totalKeyValues += count;

	for ( int i = 0; i < count; i++ ) {
		idStr key;
		idStr value;
		if ( !Session_ReadCmdDemoCString( file, key, headerStart, error ) ||
			 !Session_ReadCmdDemoCString( file, value, headerStart, error ) ) {
			dict.Clear();
			return false;
		}
		dict.Set( key, value );
	}
	return true;
}

static bool Session_ParseCmdDemoHeader(
	idFile *file,
	mapSpawnData_t &parsed,
	idStr &error ) {
	error.Clear();
	if ( file == NULL || file->Length() <= 0 ) {
		error = "empty command-demo file";
		return false;
	}

	const int headerStart = file->Tell();
	if ( headerStart < 0 ) {
		error = "invalid command-demo file position";
		return false;
	}

	int totalKeyValues = 0;
	if ( !Session_ReadCmdDemoDict(
			file, parsed.serverInfo, headerStart, totalKeyValues, error ) ) {
		return false;
	}
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( !Session_ReadCmdDemoDict(
				file, parsed.userInfo[i], headerStart, totalKeyValues, error ) ||
			 !Session_ReadCmdDemoDict(
				file, parsed.persistentPlayerInfo[i],
				headerStart, totalKeyValues, error ) ) {
			return false;
		}
	}

	const int spawnCommandBytes =
		static_cast<int>( sizeof( parsed.mapSpawnUsercmd ) );
	if ( file->Tell() < headerStart ||
		 file->Tell() - headerStart >
			SESSION_MAX_CMD_DEMO_HEADER_BYTES - spawnCommandBytes ||
		 file->Length() - file->Tell() < spawnCommandBytes ||
		 file->Read( parsed.mapSpawnUsercmd, spawnCommandBytes ) != spawnCommandBytes ) {
		error = "truncated command-demo map user commands";
		return false;
	}

	const int commandBytes = file->Length() - file->Tell();
	if ( commandBytes < static_cast<int>( sizeof( logCmd_t ) ) ) {
		error = "command demo contains no complete command frames";
		return false;
	}
	if ( commandBytes % static_cast<int>( sizeof( logCmd_t ) ) != 0 ) {
		error = "command-demo command stream is truncated or ABI-incompatible";
		return false;
	}
	return true;
}

bool idSessionLocal::LoadCmdDemoFromFile( idFile *file, idStr &error ) {
	mapSpawnData_t parsed;
	if ( !Session_ParseCmdDemoHeader( file, parsed, error ) ) {
		return false;
	}

	mapSpawnData.serverInfo = parsed.serverInfo;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.userInfo[i] = parsed.userInfo[i];
		mapSpawnData.persistentPlayerInfo[i] = parsed.persistentPlayerInfo[i];
	}
	memcpy( mapSpawnData.mapSpawnUsercmd,
		parsed.mapSpawnUsercmd, sizeof( mapSpawnData.mapSpawnUsercmd ) );
	return true;
}

bool idSessionLocal::ValidateCmdDemoFile( const char *path, idStr &error ) {
	error.Clear();
	idFile *file = fileSystem->OpenFileRead( path, false );
	if ( file == NULL ) {
		error = "command-demo file not found";
		return false;
	}
	mapSpawnData_t parsed;
	const bool valid = Session_ParseCmdDemoHeader( file, parsed, error );
	fileSystem->CloseFile( file );
	return valid;
}

/*
==============
idSessionLocal::WriteCmdDemo

Dumps the accumulated commands for the current level.
This should still work after disconnecting from a level
==============
*/
void idSessionLocal::WriteCmdDemo( const char *demoName, bool save ) {
	
	if ( !demoName[0] ) {
		common->Printf( "idSessionLocal::WriteCmdDemo: no name specified\n" );
		return;
	}

	idStr statsName;
	if (save) {
		statsName = demoName;
		statsName.StripFileExtension();
		statsName.DefaultFileExtension(".stats");
	}

	common->Printf( "writing save data to %s\n", demoName );

	idFile *cmdDemoFile = fileSystem->OpenFileWrite( demoName );
	if ( !cmdDemoFile ) {
		common->Printf( "Couldn't open for writing %s\n", demoName );
		return;
	}

	if ( save ) {
		cmdDemoFile->Write( &logIndex, sizeof( logIndex ) );
	}
	
	SaveCmdDemoToFile( cmdDemoFile );

	if ( save ) {
		idFile *statsFile = fileSystem->OpenFileWrite( statsName );
		if ( statsFile ) {
			statsFile->Write( &statIndex, sizeof( statIndex ) );
			statsFile->Write( loggedStats, numClients * statIndex * sizeof( loggedStats[0] ) );
			fileSystem->CloseFile( statsFile );
		}
	}

	fileSystem->CloseFile( cmdDemoFile );
}

/*
===============
idSessionLocal::FinishCmdLoad
===============
*/
void idSessionLocal::FinishCmdLoad() {
}

/*
===============
idSessionLocal::StartPlayingCmdDemo
===============
*/
void idSessionLocal::StartPlayingCmdDemo(const char *demoName) {
	// exit any current game
	Stop();

	idStr fullDemoName = "demos/";
	fullDemoName += demoName;
	if ( fullDemoName.Length() < 6 ||
		 idStr::Icmp( fullDemoName.c_str() + fullDemoName.Length() - 6, ".cdemo" ) != 0 ) {
		fullDemoName += ".cdemo";
	}
	cmdDemoFile = fileSystem->OpenFileRead(fullDemoName);

	if ( cmdDemoFile == NULL ) {
		common->Printf( "Couldn't open %s\n", fullDemoName.c_str() );
		StartMenu( false );
		return;
	}

// jmarshall - quake 4 loading gui
	guiLoading = uiManager->FindGui("guis/loading/generic.gui", true, false, true);
// jmarshall end
	//cmdDemoFile->Read(&loadGameTime, sizeof(loadGameTime));

	idStr error;
	if ( !LoadCmdDemoFromFile( cmdDemoFile, error ) ) {
		common->Warning( "Rejected command demo '%s': %s",
			fullDemoName.c_str(), error.c_str() );
		fileSystem->CloseFile( cmdDemoFile );
		cmdDemoFile = NULL;
		StartMenu( false );
		return;
	}

	// start the map
	ExecuteMapChange();

	cmdDemoFile = fileSystem->OpenFileRead(fullDemoName);

	// have to do this twice as the execmapchange clears the cmddemofile
	if ( cmdDemoFile == NULL || !LoadCmdDemoFromFile( cmdDemoFile, error ) ) {
		common->Warning( "Could not restart command demo '%s' after map load: %s",
			fullDemoName.c_str(),
			cmdDemoFile == NULL ? "file not found" : error.c_str() );
		Stop();
		StartMenu( false );
		return;
	}

	// run one frame to get the view angles correct
	RunGameTic();
}

/*
===============
idSessionLocal::TimeCmdDemo
===============
*/
void idSessionLocal::TimeCmdDemo( const char *demoName ) {
	StartPlayingCmdDemo( demoName );
	ClearWipe();
	UpdateScreen();

	int		startTime = Sys_Milliseconds();
	int		count = 0;
	int		minuteStart, minuteEnd;
	float	sec;

	// run all the frames in sequence
	minuteStart = startTime;

	while( cmdDemoFile ) {
		RunGameTic();
		count++;

		if ( count / 3600 != ( count - 1 ) / 3600 ) {
			minuteEnd = Sys_Milliseconds();
			sec = ( minuteEnd - minuteStart ) / 1000.0;
			minuteStart = minuteEnd;
			common->Printf( "minute %i took %3.1f seconds\n", count / 3600, sec );
			UpdateScreen();
		}
	}

	int		endTime = Sys_Milliseconds();
	sec = ( endTime - startTime ) / 1000.0;
	common->Printf( "%i seconds of game, replayed in %5.1f seconds\n", count / 60, sec );
}

/*
===============
idSessionLocal::UnloadMap

Performs cleanup that needs to happen between maps, or when a
game is exited.
Exits with mapSpawned = false
===============
*/
void idSessionLocal::UnloadMap() {
	// A level-load generation owns worker-visible file handles and immutable
	// staging buffers. Join it before any game, render-world, renderer-module,
	// or filesystem state used by the outgoing map can be destroyed.
	fileSystem->CancelLevelLoadCache();
	if ( renderSystem != NULL ) {
		renderSystem->ResetGpuFrameTiming( "session unload" );
	}
	StopPlayingRenderDemo();

	if ( com_showFramePacing.GetInteger() >= 2 && framePacingStats.valid ) {
		Session_PrintFramePacingSummary( framePacingStats, " final" );
	}

	// Stop the game sound world before the entities that own its emitters are
	// destroyed. ~idEntity frees its emitter without stopping it, and a freed
	// emitter is only recycled once its channel list is empty, so every looping
	// sound from the outgoing session otherwise survives into the next one.
	// idSessionLocal::Stop() already does this, which is why quitting to the
	// menu first was clean while loading a savegame over a live map was not.
	if ( soundSystem ) {
		soundSystem->StopAllSounds( SOUNDWORLD_GAME );
	}

	// end the current map in the game
	if ( game ) {
		game->MapShutdown();
	}

	if ( cmdDemoFile ) {
		fileSystem->CloseFile( cmdDemoFile );
		cmdDemoFile = NULL;
	}

	if ( writeDemo ) {
		StopRecordingRenderDemo();
	}

	// drop every render entity/light the outgoing session registered, matching
	// the original engine's teardown order; InitFromMap's same-map retain path
	// frees them too, but only after the next map has already begun loading
	if ( rw ) {
		rw->FreeDefs();
	}

	iamTheDukeActive = false;
	objectiveFailed = false;
	mapSpawned = false;
}

/*
===============
Session_BuildLevelLoadCacheSettings

Only settings that can alter source selection, decoded CPU data, image/sample
policy, or renderer-owned cache payloads belong here.  Display-only settings
must not cause manifest churn.
===============
*/
static void Session_BuildLevelLoadCacheSettings( idStr &settings ) {
	static const char *const settingNames[] = {
		"com_binaryRead",
		"r_renderer",
		"r_actualRenderer",
		"r_mergeModelSurfaces",
		"r_slopVertex",
		"r_slopTexCoord",
		"r_slopNormal",
		"r_useNewSkinning",
		"r_useFastSkinning",
		"r_forceConvertMD5R",
		"r_convertMD5toMD5R",
		"r_convertStaticToMD5R",
		"r_convertProcToMD5R",
		"r_pbrMaterials",
		"image_downSize",
		"image_downSizeLimit",
		"image_downSizeSpecular",
		"image_downSizeSpecularLimit",
		"image_downSizeBump",
		"image_downSizeBumpLimit",
		"image_ignoreHighQuality",
		"image_picmip",
		"image_picmipFilter",
		"image_picmipMinSize",
		"image_usePrecompressedTextures",
		"s_useCompression",
		"sys_lang"
	};
	settings = "level-load-settings-v2;";
	for ( unsigned int i = 0; i < sizeof( settingNames ) / sizeof( settingNames[ 0 ] ); i++ ) {
		settings += settingNames[ i ];
		settings += '=';
		settings += cvarSystem->GetCVarString( settingNames[ i ] );
		settings += ';';
	}
}

/*
===============
idSessionLocal::LoadLoadingGui
===============
*/
void idSessionLocal::LoadLoadingGui( const char *mapName ) {
#ifdef ID_DEDICATED
	guiLoading = NULL;
	return;
#endif
	// load / program a gui to stay up on the screen while loading
	idStr stripped = mapName;
	stripped.StripFileExtension();
	stripped.StripPath();

	const char *loadingLevelName = mapName;
	const char *loadingObjectives = "";
	const char *loadingAuthor = "";
	idStr loadingBackground = "gfx/guis/loadscreens/generic";
	bool loadingBackgroundCanvasFill = false;
	const char *loadGuiOverride = "";
	const char *spawnGameType = mapSpawnData.serverInfo.GetString( "si_gameType", cvarSystem->GetCVarString( "si_gameType" ) );
	const char *spawnMapPath = mapSpawnData.serverInfo.GetString( "si_map", mapName );
	const char *spawnEntityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	const bool mapLooksMultiplayer = !idStr::Icmpn( spawnMapPath, "mp/", 3 );
	const bool isMultiplayerLoad = mapLooksMultiplayer || ( spawnGameType[ 0 ] != '\0' && idStr::Icmp( spawnGameType, "singleplayer" ) != 0 );

	idDict mapDeclDict;
	const idDict *mapDef = Session_GetMapDeclDict( spawnMapPath, spawnEntityFilter, mapDeclDict ) ? &mapDeclDict : NULL;
	if ( mapDef ) {
		loadingLevelName = common->GetLanguageDict()->GetString( mapDef->GetString( "name", spawnMapPath ) );
		loadingObjectives = common->GetLanguageDict()->GetString( mapDef->GetString( "objectives", "" ) );

		const char *loadingAuthorKey = mapDef->GetString( "author", "" );
		if ( loadingAuthorKey[ 0 ] == '\0' ) {
			// Add-on packs commonly store the loadscreen subtitle under `loading_message`.
			loadingAuthorKey = mapDef->GetString( "loading_message", "" );
		}
		loadingAuthor = common->GetLanguageDict()->GetString( loadingAuthorKey );

		loadGuiOverride = mapDef->GetString( "loadgui", "" );
		const char *loadImage = mapDef->GetString( "loadimage", "" );
		if ( loadImage[0] ) {
			loadingBackground = loadImage;
		} else if ( idStr::Icmp( loadGuiOverride, "guis/loading/intro.gui" ) == 0 &&
				Session_FileExistsInSearchPaths( "gfx/guis/loadscreens/e3_load.tga" ) ) {
			loadingBackground = "gfx/guis/loadscreens/e3_load";
		} else {
			// Match MP levelshot fallback behavior (including addon extraction).
			char screenshot[ MAX_STRING_CHARS ];
			fileSystem->FindMapScreenshot( spawnMapPath, screenshot, MAX_STRING_CHARS );
			loadingBackground = screenshot;
		}
	} else {
		// Keep non-mapDef paths consistent with MP levelshot handling.
		char screenshot[ MAX_STRING_CHARS ];
		fileSystem->FindMapScreenshot( spawnMapPath, screenshot, MAX_STRING_CHARS );
		loadingBackground = screenshot;
	}

	idStr expandedLoadingBackground;
	if ( Session_PrepareExpandedLoadingBackground( loadingBackground, stripped.c_str(), expandedLoadingBackground ) ) {
		loadingBackground = expandedLoadingBackground;
		loadingBackgroundCanvasFill = true;
	}

	char guiMap[ MAX_STRING_CHARS ];
	idStr::Copynz( guiMap, va( "guis/map/%s.gui", stripped.c_str() ), sizeof( guiMap ) );
	// give the gamecode a chance to override
	//game->GetMapLoadingGUI( guiMap );

	if ( loadGuiOverride[0] && uiManager->CheckGui( loadGuiOverride ) ) {
		guiLoading = uiManager->FindGui( loadGuiOverride, true, false, true );
	} else if ( uiManager->CheckGui( guiMap ) ) {
		guiLoading = uiManager->FindGui( guiMap, true, false, true );
	} else if ( isMultiplayerLoad && uiManager->CheckGui( "guis/loading/mplevel.gui" ) ) {
		guiLoading = uiManager->FindGui( "guis/loading/mplevel.gui", true, false, true );
	} else if ( loadingObjectives[0] && uiManager->CheckGui( "guis/loading/splevel.gui" ) ) {
		guiLoading = uiManager->FindGui( "guis/loading/splevel.gui", true, false, true );
	} else {
		guiLoading = uiManager->FindGui("guis/loading/generic.gui", true, false, true);
	}

	if ( guiLoading ) {
		guiLoading->SetStateFloat( "map_loading", 0.0f );
		guiLoading->SetStateString( "loading_bkgnd", loadingBackground.c_str() );
		guiLoading->SetStateInt( "loading_bkgnd_canvasfill", loadingBackgroundCanvasFill ? 1 : 0 );
		// Preserve compatibility with GUIs that still key off the old "wide" state to select
		// the full-canvas branch used by dynamically expanded levelshots.
		guiLoading->SetStateInt( "loading_bkgnd_wide", loadingBackgroundCanvasFill ? 1 : 0 );
		guiLoading->SetStateString( "loading_levelname", loadingLevelName );
		guiLoading->SetStateString( "loading_objectives", loadingObjectives );
		guiLoading->SetStateString( "loading_author", loadingAuthor );
		guiLoading->SetStateInt( "loading_author_visible", loadingAuthor[ 0 ] ? 1 : 0 );
		guiLoading->SetStateString( "loading_message", "" );
		guiLoading->SetStateString( "server_loadinfo", "" );
		guiLoading->SetStateString( "server_name", "" );
		guiLoading->SetStateString( "server_ip", "" );
		guiLoading->SetStateString( "server_gametype", "" );
		guiLoading->SetStateString( "server_limit", "" );

		if ( isMultiplayerLoad ) {
			const char *serverName = mapSpawnData.serverInfo.GetString( "si_name", cvarSystem->GetCVarString( "si_name" ) );
			idStr serverAddress = networkSystem->GetServerAddress();
			if ( !serverAddress.Length() ) {
				char localEndpoint[idNetworkEndpoint::LOCAL_ENDPOINT_TEXT_SIZE];
				if ( idNetworkEndpoint::FormatLocalServerEndpoint( cvarSystem->GetCVarString( "net_ip" ),
						cvarSystem->GetCVarInteger( "net_port" ), localEndpoint, sizeof( localEndpoint ) ) ) {
					serverAddress = localEndpoint;
				}
			}
			const char *gameType = mapSpawnData.serverInfo.GetString( "si_gameType", cvarSystem->GetCVarString( "si_gameType" ) );
			const char *localizedGameType = Session_GetLongMPGameTypeName( gameType );
			const idStr limitText = Session_GetMPLoadLimitString( mapSpawnData.serverInfo );

			if ( serverName && serverName[ 0 ] ) {
				guiLoading->SetStateString( "server_name", serverName );
			}
			if ( serverAddress.Length() ) {
				guiLoading->SetStateString( "server_ip", serverAddress.c_str() );
			}
			if ( localizedGameType && localizedGameType[ 0 ] ) {
				guiLoading->SetStateString( "server_gametype", localizedGameType );
			}
			if ( limitText.Length() ) {
				guiLoading->SetStateString( "server_limit", limitText.c_str() );
			}
		}

		guiLoading->SetStateInt( "load_icons", 0 );
		for ( int i = 1; i <= 20; i++ ) {
			guiLoading->SetStateInt( va( "load_icon_%d", i ), 0 );
			guiLoading->SetStateString( va( "load_icon_img_%d", i ), "" );
		}
		guiLoading->StateChanged( common->GetPresentationTime() );

		const char *fallbackLoadingBackground = "gfx/guis/loadscreens/generic";
		const idMaterial *mat = declManager->FindMaterial( loadingBackground.c_str() );
		if ( mat == NULL || mat->TestMaterialFlag( MF_DEFAULTED ) ) {
			// Keep loading-screen redraw on a known-good GUI material if a map-specific
			// levelshot or generated expansion could not be resolved at runtime.
			if ( idStr::Icmp( loadingBackground, fallbackLoadingBackground ) != 0 &&
				 idStr::Icmp( loadingBackground, "gfx/guis/loadscreens/generic.tga" ) != 0 ) {
				common->Warning( "Loading GUI background '%s' could not be resolved, falling back to '%s'",
					loadingBackground.c_str(), fallbackLoadingBackground );
				loadingBackground = fallbackLoadingBackground;
				loadingBackgroundCanvasFill = false;
				guiLoading->SetStateString( "loading_bkgnd", loadingBackground.c_str() );
				guiLoading->SetStateInt( "loading_bkgnd_canvasfill", 0 );
				guiLoading->SetStateInt( "loading_bkgnd_wide", 0 );
				guiLoading->StateChanged( common->GetPresentationTime() );
				mat = declManager->FindMaterial( loadingBackground.c_str() );
			}
		}
		if ( mat != NULL && !mat->TestMaterialFlag( MF_DEFAULTED ) ) {
			mat->SetSort( SS_GUI );
		}
	}
}

/*
===============
idSessionLocal::GetBytesNeededForMapLoad
===============
*/
int idSessionLocal::GetBytesNeededForMapLoad( const char *mapName ) {
	idDict mapDeclDict;
	const char *entityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	const idDict *mapDef = Session_GetMapDeclDict( mapName, entityFilter, mapDeclDict ) ? &mapDeclDict : NULL;
	const int machineSpec = idMath::ClampInt( 0, 3, com_machineSpec.GetInteger() );
	const int fallbackBytes = ( machineSpec < 2 ) ? ( 200 * 1024 * 1024 ) : ( 400 * 1024 * 1024 );

	if ( mapDef ) {
		// Stock map defs commonly provide size0..size2 only, so for ultra-spec
		// systems (or missing entries) walk down to the closest available key.
		for ( int spec = machineSpec; spec >= 0; --spec ) {
			const int bytesNeeded = mapDef->GetInt( va( "size%d", spec ), "0" );
			if ( bytesNeeded > 0 ) {
				return bytesNeeded;
			}
		}

		return fallbackBytes;
	} else {
		return fallbackBytes;
	}
}

/*
===============
idSessionLocal::SetBytesNeededForMapLoad
===============
*/
void idSessionLocal::SetBytesNeededForMapLoad( const char *mapName, int bytesNeeded ) {
	idDecl *mapDecl = const_cast<idDecl *>(declManager->FindType( DECL_MAPDEF, mapName, false ));
	idDeclEntityDef *mapDef = static_cast<idDeclEntityDef *>( mapDecl );

	if ( com_updateLoadSize.GetBool() && mapDef ) {
		// we assume that if com_updateLoadSize is true then the file is writable

		mapDef->dict.SetInt( va("size%d", com_machineSpec.GetInteger()), bytesNeeded );

		idStr declText = "\nmapDef ";
		declText += mapDef->GetName();
		declText += " {\n";
		for (int i=0; i<mapDef->dict.GetNumKeyVals(); i++) {
			const idKeyValue *kv = mapDef->dict.GetKeyVal( i );
			if ( kv && (kv->GetKey().Cmp("classname") != 0 ) ) {
				declText += "\t\"" + kv->GetKey() + "\"\t\t\"" + kv->GetValue() + "\"\n";
			}
		}
		declText += "}";
		mapDef->SetText( declText );
		mapDef->ReplaceSourceFileText();
	}
}

/*
===============
idSessionLocal::ExecuteMapChange

Performs the initialization of a game based on mapSpawnData, used for both single
player and multiplayer, but not for renderDemos, which don't
create a game at all.
Exits with mapSpawned = true
===============
*/
void idSessionLocal::ExecuteMapChange( bool noFadeWipe ) {
	int		i;
	bool	reloadingSameMap;

	loadingAssetQueueActive = false;
	loadingAssetQueueTotal = 0;
	loadingAssetQueueLoaded = 0;
	loadingAssetQueueStartPct = 0.0f;

	// Start each load from a clean pacifier budget so a hitch recorded during the
	// previous map change cannot keep throttling this one.
	lastPacifierDrawMsec = 0;

	// close console and remove any prints from the notify lines
	console->Close();

	if ( IsMultiplayer() ) {
		// make sure the mp GUI isn't up, or when players get back in the
		// map, mpGame's menu and the gui will be out of sync.
		SetGUI( NULL, NULL );
	}

	// mute sound
	soundSystem->SetMute( true );

	// clear all menu sounds
	menuSoundWorld->ClearAllSoundEmitters();

	// unpause the game sound world
	// NOTE: we UnPause again later down. not sure this is needed
	if ( sw->IsPaused() ) {
		sw->UnPause();
	}

	if ( !noFadeWipe ) {
		// capture the current screen and start a wipe
		StartWipe( "gfx/wipes/fade", true );

		// immediately complete the wipe to fade out the level transition
		// run the wipe to completion
		CompleteWipe();
	}

	// extract the map name from serverinfo
	idStr mapString = mapSpawnData.serverInfo.GetString( "si_map" );
	idStr filterString = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );

	idStr fullMapName = "maps/";
	fullMapName += mapString;
	fullMapName.StripFileExtension();

	// shut down the existing game if it is running
	UnloadMap();
	console->SetProcFileOutOfDate( false );

	// don't do the deferred caching if we are reloading the same map
	if ( fullMapName == currentMapName && filterString == currentFilterString ) {
		reloadingSameMap = true;
	} else {
		reloadingSameMap = false;
		currentMapName = fullMapName;
		currentFilterString = filterString;
		lastCheckPoint = -1;
	}
	fileSystem->SetAssetLogName( fullMapName.c_str() );
	// Reset before BeginLevelLoadCache can publish work.  Preload workers add
	// their reads to this counter, so resetting after the generation starts can
	// nondeterministically discard bytes already read by a worker.
	fileSystem->ResetReadCount();
	idStr levelLoadCacheSettings;
	Session_BuildLevelLoadCacheSettings( levelLoadCacheSettings );
#ifdef ID_DEDICATED
	const char *levelLoadGameMode = "dedicated";
#else
	const char *levelLoadGameMode = IsMultiplayer() ? "multiplayer" : "singleplayer";
#endif
	fileSystem->BeginLevelLoadCache( fullMapName.c_str(), levelLoadGameMode,
		filterString.c_str(), levelLoadCacheSettings.c_str() );

	if ( !reloadingSameMap && com_SingleDeclFile.GetBool() ) {
		declManager->FlushDecls();
		declManager->StartLoadingDecls();
		declManager->LoadDeclsFromFile();
		declManager->LoadDeclsFromFile();
		declManager->FinishLoadingDecls();
	}

	// note which media we are going to need to load
	if ( !reloadingSameMap ) {
		declManager->BeginLevelLoad();
		renderSystem->BeginLevelLoad();
		soundSystem->BeginLevelLoad();
		if ( bse ) {
			bse->BeginLevelLoad();
		}
	}

	uiManager->BeginLevelLoad();
	uiManager->Reload( true );
#ifndef ID_DEDICATED
	// Level-load purging clears media-use marks established at session startup.
	// Re-reference every in-game menu resource while the normal precache window
	// is active, keeping the first Escape press free of filesystem work.
	PrimeMainMenuGuiResources();
#endif

	// set the loading gui that we will wipe to
	LoadLoadingGui( mapString );

	// cause prints to force screen updates as a pacifier,
	// and draw the loading gui instead of game draws
	insideExecuteMapChange = true;

	// if this works out we will probably want all the sizes in a def file although this solution will 
	// work for new maps etc. after the first load. we can also drop the sizes into the default.cfg
	if ( !reloadingSameMap  ) {
		bytesNeededForMapLoad = GetBytesNeededForMapLoad( mapString.c_str() );
	} else {
		bytesNeededForMapLoad = 30 * 1024 * 1024;
	}

	ClearWipe();

	// let the loading gui spin for 1 second to animate out
	ShowLoadingGui();

	// note any warning prints that happen during the load process
	common->ClearWarnings( mapString );

	// release the mouse cursor
	// before we do this potentially long operation
	Sys_GrabMouseCursor( false );

	// if net play, we get the number of clients during mapSpawnInfo processing
	if ( !idAsyncNetwork::IsActive() ) {
		numClients = 1;
	} 
	
	int start = Sys_Milliseconds();
	int phaseStart = start;
	int renderWorldMsec = 0;
	int gameInitMsec = 0;
	int playerSpawnMsec = 0;
	int cacheJoinMsec = 0;
	int mediaFinishMsec = 0;
	int mediaRenderMsec = 0;
	int mediaSoundMsec = 0;
	int mediaDeclMsec = 0;
	int mediaBseMsec = 0;
	int mediaLoadSizeMsec = 0;
	int mediaUiMsec = 0;
	int settleMsec = 0;
	int settleFrameMsec[10] = { 0 };

	common->Printf( "--------- Map Initialization ---------\n" );
	common->Printf( "Map: %s\n", mapString.c_str() );

	// let the renderSystem load all the geometry
	if ( !rw->InitFromMap( fullMapName ) ) {
		common->Error( "couldn't load %s", fullMapName.c_str() );
	}
	renderWorldMsec = Sys_Milliseconds() - phaseStart;
	phaseStart = Sys_Milliseconds();

	// for the synchronous networking we needed to roll the angles over from
	// level to level, but now we can just clear everything
	usercmdGen->InitForNewMap();
	memset( &mapSpawnData.mapSpawnUsercmd, 0, sizeof( mapSpawnData.mapSpawnUsercmd ) );

	// set the user info
	for ( i = 0; i < numClients; i++ ) {
		game->SetUserInfo( i, mapSpawnData.userInfo[i], false );
		game->SetPersistentPlayerInfo( i, mapSpawnData.persistentPlayerInfo[i] );
	}

	// load and spawn all other entities ( from a savegame possibly )
	if ( loadingSaveGame && savegameFile ) {
		if ( game->InitFromSaveGame( fullMapName, rw, savegameFile ) == false ) {
			// If the loadgame failed, restart the map with the player persistent data
			loadingSaveGame = false;
			fileSystem->CloseFile( savegameFile );
			savegameFile = NULL;

			game->SetServerInfo( mapSpawnData.serverInfo );
			game->InitFromNewMap( fullMapName, rw, idAsyncNetwork::server.IsActive(), idAsyncNetwork::client.IsActive(), Sys_Milliseconds() );
		}
	} else {
		game->SetServerInfo( mapSpawnData.serverInfo );
		game->InitFromNewMap( fullMapName, rw, idAsyncNetwork::server.IsActive(), idAsyncNetwork::client.IsActive(), Sys_Milliseconds() );
	}
	gameInitMsec = Sys_Milliseconds() - phaseStart;
	phaseStart = Sys_Milliseconds();

	if ( !idAsyncNetwork::IsActive() && !loadingSaveGame ) {
		// spawn players
		for ( i = 0; i < numClients; i++ ) {
			game->SpawnPlayer( i, false, NULL );
		}
	}
	playerSpawnMsec = Sys_Milliseconds() - phaseStart;
	phaseStart = Sys_Milliseconds();
	fileSystem->FinishLevelLoadCache( true );
	cacheJoinMsec = Sys_Milliseconds() - phaseStart;
	phaseStart = Sys_Milliseconds();

	// actually purge/load the media
	int mediaPhaseStart = phaseStart;
	if ( !reloadingSameMap ) {
		renderSystem->EndLevelLoad();
		mediaRenderMsec = Sys_Milliseconds() - mediaPhaseStart;
		mediaPhaseStart = Sys_Milliseconds();
		soundSystem->EndLevelLoad();
		mediaSoundMsec = Sys_Milliseconds() - mediaPhaseStart;
		mediaPhaseStart = Sys_Milliseconds();
		declManager->EndLevelLoad();
		mediaDeclMsec = Sys_Milliseconds() - mediaPhaseStart;
		mediaPhaseStart = Sys_Milliseconds();
		if ( bse ) {
			bse->EndLevelLoad();
		}
		mediaBseMsec = Sys_Milliseconds() - mediaPhaseStart;
		mediaPhaseStart = Sys_Milliseconds();
		SetBytesNeededForMapLoad( mapString.c_str(), fileSystem->GetReadCount() );
		mediaLoadSizeMsec = Sys_Milliseconds() - mediaPhaseStart;
		mediaPhaseStart = Sys_Milliseconds();
	}
	uiManager->EndLevelLoad();
	mediaUiMsec = Sys_Milliseconds() - mediaPhaseStart;
	FS_ReleaseLevelLoadCache();
	mediaFinishMsec = Sys_Milliseconds() - phaseStart;
	phaseStart = Sys_Milliseconds();

	if ( !idAsyncNetwork::IsActive() && !loadingSaveGame ) {
		// run a few frames to allow everything to settle
		for ( i = 0; i < 10; i++ ) {
			const int settleFrameStart = Sys_Milliseconds();
			game->RunFrame( mapSpawnData.mapSpawnUsercmd, 0, true, 0 ); // serverGameFrame isn't used
			settleFrameMsec[i] = Sys_Milliseconds() - settleFrameStart;
		}
	}
	settleMsec = Sys_Milliseconds() - phaseStart;

	common->Printf ("-----------------------------------\n");

	int	msec = Sys_Milliseconds() - start;
	common->Printf( "%6d msec to load %s\n", msec, mapString.c_str() );
	if ( com_showLevelLoadTimes.GetBool() ) {
		common->Printf(
			"Map load phases: renderWorld=%d gameInit=%d playerSpawn=%d cacheJoin=%d mediaFinish=%d settle=%d total=%d msec\n",
			renderWorldMsec,
			gameInitMsec,
			playerSpawnMsec,
			cacheJoinMsec,
			mediaFinishMsec,
			settleMsec,
			msec );
		common->Printf(
			"Map media phases: render=%d sound=%d decl=%d bse=%d loadSize=%d ui=%d total=%d msec\n",
			mediaRenderMsec,
			mediaSoundMsec,
			mediaDeclMsec,
			mediaBseMsec,
			mediaLoadSizeMsec,
			mediaUiMsec,
			mediaFinishMsec );
		if ( settleMsec > 0 ) {
			common->Printf(
				"Map settle frames: %d %d %d %d %d %d %d %d %d %d msec\n",
				settleFrameMsec[0],
				settleFrameMsec[1],
				settleFrameMsec[2],
				settleFrameMsec[3],
				settleFrameMsec[4],
				settleFrameMsec[5],
				settleFrameMsec[6],
				settleFrameMsec[7],
				settleFrameMsec[8],
				settleFrameMsec[9] );
		}
	}

	// let the game trigger interaction generation after the first game frame
	// so lights and entities have presented to the render world.

	common->PrintWarnings();

	if ( guiLoading ) {
		float pct = idMath::ClampFloat( 0.0f, 1.0f, guiLoading->State().GetFloat( "map_loading" ) );
		while ( pct < 1.0f ) {
			Session_BeginBlockingLoadPresentationFrame();

			// Ease out quickly to full once loading has completed while keeping the motion smooth.
			const float remaining = 1.0f - pct;
			const float step = Max( 0.01f, remaining * 0.25f );
			pct = Min( 1.0f, pct + step );

			guiLoading->SetStateFloat( "map_loading", pct );
			guiLoading->StateChanged( common->GetPresentationTime() );
			Sys_GenerateEvents();
			UpdateScreen();
		}
	}

	const int loadingContinueAutoAdvanceMsec = idMath::ClampInt( 0, 60000, com_loadingContinueAutoAdvance.GetInteger() );
	const bool logLoadingContinueGate = ( loadingContinueAutoAdvanceMsec > 0 ) || ( com_showFramePacing.GetInteger() >= 2 );
	const bool waitForSPContinue =
		guiLoading &&
		!IsMultiplayer() &&
		!idAsyncNetwork::IsActive() &&
		!loadingSaveGame &&
		!com_skipLoadingContinue.GetBool();
	if ( !waitForSPContinue && logLoadingContinueGate ) {
		common->Printf( "Loading continue gate skipped (guiLoading=%d, isMultiplayer=%d, async=%d, loadingSaveGame=%d, skipCvar=%d)\n",
			guiLoading != NULL ? 1 : 0,
			IsMultiplayer() ? 1 : 0,
			idAsyncNetwork::IsActive() ? 1 : 0,
			loadingSaveGame ? 1 : 0,
			com_skipLoadingContinue.GetBool() ? 1 : 0 );
	}
	if ( waitForSPContinue ) {
		Session_BeginBlockingLoadPresentationFrame();
		guiLoading->HandleNamedEvent( "FinishedLoading" );
		guiLoading->StateChanged( common->GetPresentationTime() );
		UpdateScreen();

		if ( logLoadingContinueGate ) {
			common->Printf( "Loading continue gate entered%s\n",
				loadingContinueAutoAdvanceMsec > 0 ? va( " (auto-advance %d ms)", loadingContinueAutoAdvanceMsec ) : "" );
		}

		bool waitingForContinue = true;
		bool acceptContinueInput = false;
		bool loadingContinueAutoAdvanced = false;
		const int loadingContinueStartTime = common->GetPresentationTime();
		openQ4_SetLoadingContinueInputActive( true );
		while ( waitingForContinue ) {
			Sys_GenerateEvents();

			while ( waitingForContinue ) {
				sysEvent_t ev = eventLoop->GetEvent();
				if ( ev.evType == SE_NONE ) {
					break;
				}

				if ( ev.evType == SE_KEY ) {
					idKeyInput::PreliminaryKeyEvent( ev.evValue, ( ev.evValue2 != 0 ) );
				} else if ( ev.evType == SE_MOUSE ) {
					idKeyInput::PreliminaryMouseEvent( ev.evValue, ev.evValue2 );
				} else if ( ev.evType == SE_JOYSTICK_AXIS ) {
					idKeyInput::PreliminaryJoystickEvent( ev.evValue2 );
				}

				if ( ev.evType == SE_CONSOLE ) {
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, (char *)ev.evPtr );
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
				} else if ( waitingForContinue && acceptContinueInput &&
						( ( ev.evType == SE_KEY && ev.evValue2 && Session_IsLoadingContinueKey( ev.evValue ) ) ||
						  ( ev.evType == SE_CHAR && Session_IsLoadingContinueChar( ev.evValue ) ) ) ) {
					waitingForContinue = false;
				}

				if ( ev.evPtr ) {
					Mem_Free( ev.evPtr );
				}
			}

			if ( waitingForContinue && loadingContinueAutoAdvanceMsec > 0 ) {
				const int elapsedMsec = Max( 0, common->GetPresentationTime() - loadingContinueStartTime );
				if ( elapsedMsec >= loadingContinueAutoAdvanceMsec ) {
					loadingContinueAutoAdvanced = true;
					waitingForContinue = false;
				}
			}

			if ( waitingForContinue ) {
				if ( Session_FindPresentationCap() > 0 ) {
					Session_BeginBlockingLoadPresentationFrame();
				}
				UpdateScreen();
				if ( Session_FindPresentationCap() <= 0 ) {
					Sys_Sleep( static_cast<int>( idMath::Ceil( common->GetUserCmdMsecFloat() ) ) );
					com_frameRealTime = Sys_Milliseconds();
				}
				acceptContinueInput = true;
			}
		}
		openQ4_SetLoadingContinueInputActive( false );

		if ( logLoadingContinueGate ) {
			const int loadingContinueElapsedMsec = Max( 0, common->GetPresentationTime() - loadingContinueStartTime );
			common->Printf( "Loading continue gate completed via %s after %d ms\n",
				loadingContinueAutoAdvanced ? "auto-advance" : "input", loadingContinueElapsedMsec );
		}

		// The same key/button used to continue would otherwise remain latched into
		// the first gameplay frame and can immediately trigger gameplay input.
		idKeyInput::ClearStates();
		Sys_ClearInputEvents();
	}

	// capture the current screen and start a wipe
	StartWipe( "gfx/wipes/fade_blend" );

	usercmdGen->Clear();

	// start saving commands for possible writeCmdDemo usage
	logIndex = 0;
	statIndex = 0;
	lastSaveIndex = 0;

	// don't bother spinning over all the tics we spent loading
	lastGameTic = latchedTicNumber = com_ticNumber;

	// remove any prints from the notify lines
	console->ClearNotifyLines();

	// stop drawing the laoding screen
	insideExecuteMapChange = false;

	Sys_SetPhysicalWorkMemory( -1, -1 );

	// set the game sound world for playback
	SetPlayingSoundWorld( sw );

	// when loading a save game the sound is paused
	if ( sw->IsPaused() ) {
		// unpause the game sound world
		sw->UnPause();
	}

	// restart entity sound playback
	soundSystem->SetMute( false );

	// we are valid for game draws now
	mapSpawned = true;
#ifdef ID_DEDICATED
	common->Printf( "Dedicated map ready: %s\n", mapString.c_str() );
#endif
	ResetFramePacingStats();
	Sys_ClearEvents();
}

/*
===============
LoadGame_f
===============
*/
static bool Session_CommandRequestsQuickSave( const idCmdArgs &args ) {
	return args.Argc() < 2 || !idStr::Icmp( args.Argv(1), "quick" );
}

static const char *Session_CommandLoadGameName( const idCmdArgs &args ) {
	return Session_CommandRequestsQuickSave( args ) ? NULL : args.Argv(1);
}

static saveType_t Session_CommandSaveGameType( const idCmdArgs &args ) {
	if ( Session_CommandRequestsQuickSave( args ) ) {
		return ST_QUICK;
	}
	if ( !idStr::Icmp( args.Argv(1), "checkPoint" ) ) {
		return ST_CHECKPOINT;
	}
	return ST_REGULAR;
}

static const char *Session_CommandSaveGameName( const idCmdArgs &args ) {
	return ( Session_CommandSaveGameType( args ) == ST_REGULAR ) ? args.Argv(1) : NULL;
}

void LoadGame_f( const idCmdArgs &args ) {
	console->Close();
	sessLocal.LoadGame( Session_CommandLoadGameName( args ) );
}

/*
===============
SaveGame_f
===============
*/
void SaveGame_f( const idCmdArgs &args ) {
	sessLocal.SaveGame( Session_CommandSaveGameName( args ), Session_CommandSaveGameType( args ) );
}

/*
===============
TakeViewNotes_f
===============
*/
void TakeViewNotes_f( const idCmdArgs &args ) {
	const char *p = ( args.Argc() > 1 ) ? args.Argv( 1 ) : "";
	sessLocal.TakeNotes( p );
}

/*
===============
TakeViewNotes2_f
===============
*/
void TakeViewNotes2_f( const idCmdArgs &args ) {
	const char *p = ( args.Argc() > 1 ) ? args.Argv( 1 ) : "";
	sessLocal.TakeNotes( p, true );
}

/*
===============
idSessionLocal::TakeNotes
===============
*/
void idSessionLocal::TakeNotes( const char *p, bool extended ) {
// jmarshall - notes
/*
	if ( !mapSpawned ) {
		common->Printf( "No map loaded!\n" );
		return;
	}

	if ( extended ) {
		guiTakeNotes = uiManager->FindGui( "guis/takeNotes2.gui", true, false, true );

#if 0
		const char *people[] = {
			"Nobody", "Adam", "Brandon", "David", "PHook", "Jay", "Jake",
				"PatJ", "Brett", "Ted", "Darin", "Brian", "Sean"
		};
#else
		const char *people[] = {
			"Tim", "Kenneth", "Robert", 
			"Matt", "Mal", "Jerry", "Steve", "Pat",
			"Xian", "Ed", "Fred", "James", "Eric", "Andy", "Seneca", "Patrick", "Kevin",
			"MrElusive", "Jim", "Brian", "John", "Adrian", "Nobody"
		};
#endif
		const int numPeople = sizeof( people ) / sizeof( people[0] );

		idListGUI * guiList_people = uiManager->AllocListGUI();
		guiList_people->Config( guiTakeNotes, "person" );
		for ( int i = 0; i < numPeople; i++ ) {
			guiList_people->Push( people[i] );
		}
		uiManager->FreeListGUI( guiList_people );

	} else {
		guiTakeNotes = uiManager->FindGui( "guis/takeNotes.gui", true, false, true );
	}

	SetGUI( guiTakeNotes, NULL );
	guiActive->SetStateString( "note", "" );
	guiActive->SetStateString( "notefile", p );
	guiActive->SetStateBool( "extended", extended );
	guiActive->Activate( true, com_frameTime );
*/
}

/*
===============
Session_Hitch_f
===============
*/
void Session_Hitch_f( const idCmdArgs &args ) {
	idSoundWorld *sw = soundSystem->GetPlayingSoundWorld();
	if ( sw ) {
		soundSystem->SetMute(true);
		sw->Pause();
		Sys_EnterCriticalSection();
	}
	if ( args.Argc() == 2 ) {
		Sys_Sleep( atoi(args.Argv(1)) );
	} else {
		Sys_Sleep( 100 );
	}
	if ( sw ) {
		Sys_LeaveCriticalSection();
		sw->UnPause();
		soundSystem->SetMute(false);
	}
}

/*
===============
idSessionLocal::ScrubSaveGameFileName

Turns a bad file name into a good one or your money back
===============
*/
void idSessionLocal::ScrubSaveGameFileName( idStr &saveFileName ) const {
	int i;
	idStr inFileName;

	inFileName = saveFileName;
	//inFileName.RemoveColors();
	inFileName.StripFileExtension();

	saveFileName.Clear();

	int len = inFileName.Length();
	for ( i = 0; i < len; i++ ) {
		if ( strchr( "',.~!@#$%^&*()[]{}<>\\|/=?+;:-\'\"", inFileName[i] ) ) {
			// random junk
			saveFileName += '_';
		} else if ( (const unsigned char)inFileName[i] >= 128 ) {
			// high ascii chars
			saveFileName += '_';
		} else if ( inFileName[i] <= ' ' ) {
			// Avoid whitespace and control characters in save-slot filenames.
			saveFileName += '_';
		} else {
			saveFileName += inFileName[i];
		}
	}

	static const int MAX_SAVEGAME_BASENAME = 96;
	if ( saveFileName.Length() > MAX_SAVEGAME_BASENAME ) {
		saveFileName = saveFileName.Left( MAX_SAVEGAME_BASENAME );
	}

	idStr deviceName = saveFileName;
	deviceName.ToUpper();
	const bool reservedDeviceName =
		deviceName.Icmp( "CON" ) == 0 ||
		deviceName.Icmp( "PRN" ) == 0 ||
		deviceName.Icmp( "AUX" ) == 0 ||
		deviceName.Icmp( "NUL" ) == 0 ||
		deviceName.Icmp( "CLOCK_" ) == 0 ||
		( deviceName.Length() == 4 &&
			( deviceName.Left( 3 ).Icmp( "COM" ) == 0 || deviceName.Left( 3 ).Icmp( "LPT" ) == 0 ) &&
			deviceName[3] >= '1' && deviceName[3] <= '9' );
	if ( reservedDeviceName ) {
		saveFileName = "_" + saveFileName;
	}
}

/*
===============
idSessionLocal::SaveGame
===============
*/
bool idSessionLocal::SaveGame( const char *saveName, saveType_t saveType ) {
#ifdef	ID_DEDICATED
	common->Printf( "Dedicated servers cannot save games.\n" );
	return false;
#else
	int i;
	idStr gameFile, previewFile, descriptionFile, tempGameFile, tempPreviewFile, tempDescriptionFile, mapName, saveSlotName, saveSlotFileName;
	idFile *fileOut = NULL;
	const idStr previousLastQuicksave = com_lastQuicksave.GetString();
	const int previousLastCheckPoint = lastCheckPoint;
	bool generatedSaveName = false;

	if ( !mapSpawned ) {
		common->Printf( "Not playing a game.\n" );
		return false;
	}

	if ( IsMultiplayer() ) {
		common->Printf( "Can't save during net play.\n" );
		return false;
	}

	if ( game->GetPersistentPlayerInfo( 0 ).GetInt( "health" ) <= 0 ) {
		MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_104311" ), common->GetLanguageDict()->GetString ( "#str_104312" ), true );
		common->Printf( "You must be alive to save the game\n" );
		return false;
	}

	if ( Sys_GetDriveFreeSpace( cvarSystem->GetCVarString( "fs_savepath" ) ) < 25 ) {
		MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_104313" ), common->GetLanguageDict()->GetString ( "#str_104314" ), true );
		common->Printf( "Not enough drive space to save the game\n" );
		return false;
	}

	if ( objectiveFailed ) {
		MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_107654" ), common->GetLanguageDict()->GetString( "#str_104312" ), true );
		common->Printf( "Can't save after failed mission.\n" );
		return false;
	}

	if ( game->InCinematic() && saveType != ST_AUTO ) {
		common->Printf( "Can't save during a cinematic.\n" );
		return false;
	}

	const bool explicitSaveName = ( saveName != NULL && saveName[0] != '\0' );
	if ( explicitSaveName ) {
		saveSlotName = saveName;
	} else {
		generatedSaveName = true;
		Session_GenerateSaveFileName( *this, saveSlotName, saveType );
	}

	// setup up filenames and paths
	gameFile = saveSlotName;
	ScrubSaveGameFileName( gameFile );
	if ( gameFile.IsEmpty() ) {
		common->Warning( "Save name '%s' does not contain a valid file name\n", saveSlotName.c_str() );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		return false;
	}
	saveSlotFileName = gameFile;

	soundSystem->SetMute( true );

	gameFile = "savegames/" + gameFile;
	gameFile.SetFileExtension( ".save" );

	previewFile = gameFile;
	previewFile.SetFileExtension( ".tga" );

	descriptionFile = gameFile;
	descriptionFile.SetFileExtension( ".txt" );

	tempGameFile = gameFile;
	tempGameFile += ".tmp";
	tempPreviewFile = previewFile;
	tempPreviewFile += ".tmp";
	tempDescriptionFile = descriptionFile;
	tempDescriptionFile += ".tmp";
	sessionSaveOperationGuard_t operationGuard( *this, fileOut,
		tempGameFile, tempDescriptionFile, tempPreviewFile,
		generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );

	if ( !Session_RecoverInterruptedSaveFiles( gameFile, descriptionFile, previewFile ) ) {
		common->Warning( "Could not recover interrupted save files for '%s'", gameFile.c_str() );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}
	Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );

	// Open savegame file
	fileOut = fileSystem->OpenFileWrite( tempGameFile );
	if ( fileOut == NULL ) {
		common->Warning( "Failed to open save file '%s'\n", tempGameFile.c_str() );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	// Write screenshot
	if ( saveType != ST_AUTO ) {
		if ( rw ) {
			rw->PushMarkedDefs();
		}
		// The current crop can be smaller than the drawable in legacy
		// r_screenFraction mode.  Push a physical-size crop so all screen-space
		// feedback targets see one coherent frame, then restore the prior crop.
		sessionRenderCropGuard_t previewCrop( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );
		common->SetRenderableGameFrame( true );
		game->Draw( 0 );
		// The renderer reduces the coherent physical frame after readback.
		renderSystem->CaptureRenderToFile( tempPreviewFile, true, 320, 240 );
	}

	mapName = mapSpawnData.serverInfo.GetString( "si_map" );

	// Write the retail-style description sidecar:
	// slot name, menu display label, preview material, optional no-overwrite marker.
	idFile *fileDesc = fileSystem->OpenFileWrite( tempDescriptionFile );
	if ( fileDesc == NULL ) {
		common->Warning( "Failed to open description file '%s'\n", tempDescriptionFile.c_str() );
		fileSystem->CloseFile( fileOut );
		fileOut = NULL;
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	idStr escapedSlotName = saveSlotName;
	Session_EscapeSaveDescription( escapedSlotName );

	idStr mapDisplayName = mapName;
	idStr saveImage = "gfx/guis/loadscreens/generic";
	idDict mapDeclDict;
	const char *entityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	const idDict *mapDef = Session_GetMapDeclDict( mapName, entityFilter, mapDeclDict ) ? &mapDeclDict : NULL;
	if ( mapDef ) {
		mapDisplayName = common->GetLanguageDict()->GetString( mapDef->GetString( "name", mapName ) );
		const char *loadImage = mapDef->GetString( "loadimage", "" );
		if ( loadImage[0] != '\0' ) {
			saveImage = loadImage;
		}
	}

	idStr menuDescription = "^:";
	if ( explicitSaveName && saveType == ST_REGULAR ) {
		menuDescription += saveSlotName;
	} else {
		menuDescription += common->GetLanguageDict()->GetString( Session_GetGeneratedSaveLabel( saveType ) );
	}
	menuDescription += "\t^0";
	menuDescription += mapDisplayName;
	Session_EscapeSaveDescription( menuDescription );

	bool descriptionWritten = true;
	descriptionWritten = descriptionWritten && Session_WriteSaveTextLine( fileDesc, escapedSlotName.c_str(), true, "description slot name", tempDescriptionFile.c_str() );
	descriptionWritten = descriptionWritten && Session_WriteSaveTextLine( fileDesc, menuDescription.c_str(), true, "description menu label", tempDescriptionFile.c_str() );

	if ( saveType == ST_AUTO ) {
		descriptionWritten = descriptionWritten && Session_WriteSaveTextLine( fileDesc, saveImage.c_str(), true, "description preview image", tempDescriptionFile.c_str() );
	} else {
		descriptionWritten = descriptionWritten && Session_WriteSaveTextLine( fileDesc, "", true, "description preview image", tempDescriptionFile.c_str() );
	}

	if ( saveType == ST_AUTO || saveType == ST_CHECKPOINT ) {
		descriptionWritten = descriptionWritten && Session_WriteSaveTextLine( fileDesc, SESSION_SAVEGAME_NO_OVERWRITE_TOKEN, false, "description no-overwrite marker", tempDescriptionFile.c_str() );
	}
	if ( descriptionWritten && !fileDesc->Sync() ) {
		common->Warning( "Failed to persist description file '%s'", tempDescriptionFile.c_str() );
		descriptionWritten = false;
	}

	fileSystem->CloseFile( fileDesc );
	if ( !descriptionWritten ) {
		common->Warning( "Failed to write description file '%s'\n", tempDescriptionFile.c_str() );
		fileSystem->CloseFile( fileOut );
		fileOut = NULL;
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}
	if ( !Session_ValidateStagedSaveDescription( tempDescriptionFile, saveSlotFileName, saveType ) ) {
		common->Warning( "Staged save description '%s' failed validation", tempDescriptionFile.c_str() );
		fileSystem->CloseFile( fileOut );
		fileOut = NULL;
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	// Write SaveGame Header:
	// Game Name / Version / Map Name / Entity Filter / Persistent Player Info
	bool headerWritten = true;
	headerWritten = headerWritten && Session_WriteSaveGameString( fileOut, SAVEGAME_GAME_NAME_RETAIL, MAX_STRING_CHARS, "game name", tempGameFile.c_str() );
	headerWritten = headerWritten && Session_WriteSaveGameInt( fileOut, SAVEGAME_VERSION, "version", tempGameFile.c_str() );
	headerWritten = headerWritten && Session_WriteSaveGameString( fileOut, mapName.c_str(), MAX_STRING_CHARS, "map name", tempGameFile.c_str() );
	headerWritten = headerWritten && Session_WriteSaveGameString( fileOut, mapSpawnData.serverInfo.GetString( "si_entityFilter" ), MAX_STRING_CHARS, "entity filter", tempGameFile.c_str() );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.persistentPlayerInfo[i] = game->GetPersistentPlayerInfo( i );
		headerWritten = headerWritten &&
			Session_WriteSaveGameDict( fileOut, mapSpawnData.persistentPlayerInfo[i], va( "persistent player info %d", i ), tempGameFile.c_str() );
	}

	if ( !headerWritten ) {
		common->Warning( "Failed to write savegame header '%s'\n", tempGameFile.c_str() );
		fileSystem->CloseFile( fileOut );
		fileOut = NULL;
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	game->SaveGame( fileOut, saveType );
	int stagedProtectedLength = -1;
	const bool stagedSizeValid = Session_GetBoundedSaveGameLength( fileOut, tempGameFile, "staged write", stagedProtectedLength ) &&
		stagedProtectedLength <= SESSION_MAX_SAVEGAME_BYTES - SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES;
	if ( !stagedSizeValid ) {
		if ( stagedProtectedLength > SESSION_MAX_SAVEGAME_BYTES - SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES &&
			 stagedProtectedLength <= SESSION_MAX_SAVEGAME_BYTES ) {
			common->Warning( "Staged savegame '%s' is %d bytes before its %d-byte integrity trailer (maximum total .save size %d bytes)",
				tempGameFile.c_str(), stagedProtectedLength, SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES, SESSION_MAX_SAVEGAME_BYTES );
		}
		fileSystem->CloseFile( fileOut );
		fileOut = NULL;
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	// close the save game file
	const bool payloadSynced = fileOut->Sync();
	fileSystem->CloseFile( fileOut );
	fileOut = NULL;
	if ( !payloadSynced ) {
		common->Warning( "Failed to persist staged savegame '%s'", tempGameFile.c_str() );
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	if ( !Session_AppendSaveGameIntegrityTrailer( tempGameFile ) ) {
		common->Warning( "Failed to protect staged savegame '%s' with an integrity trailer", tempGameFile.c_str() );
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	if ( !Session_ValidateStagedSaveGameHeader( tempGameFile ) ) {
		common->Warning( "Staged savegame '%s' failed validation", tempGameFile.c_str() );
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	const bool previewReady = ( saveType != ST_AUTO && Session_ValidateStagedSavePreview( tempPreviewFile ) &&
		Session_SyncRelativeSaveFile( tempPreviewFile ) );
	if ( !previewReady ) {
		Session_RemoveRelativeSaveFile( tempPreviewFile );
		if ( !Session_CreateSavePreviewDeletionMarker( tempPreviewFile ) ) {
			common->Warning( "Failed to stage preview deletion marker '%s'", tempPreviewFile.c_str() );
			Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
			Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
			syncNextGameFrame = true;
			soundSystem->SetMute( insideExecuteMapChange );
			return false;
		}
	}
	sessionStagedSaveFile_t stagedFiles[3];
	int numStagedFiles = 0;
	Session_InitStagedSaveFile( stagedFiles[numStagedFiles++], tempDescriptionFile, descriptionFile, true );
	Session_InitStagedSaveFile( stagedFiles[numStagedFiles++], tempPreviewFile, previewFile, true );
	// The payload is the commit point. Sidecars must be in place before its
	// final rename so interrupted saves can be recovered deterministically.
	Session_InitStagedSaveFile( stagedFiles[numStagedFiles++], tempGameFile, gameFile, true );

	if ( !Session_CommitStagedSaveFiles( stagedFiles, numStagedFiles ) ) {
		Session_RemoveStagedSaveFiles( tempGameFile, tempDescriptionFile, tempPreviewFile );
		Session_RestoreGeneratedSaveState( *this, generatedSaveName, saveType, previousLastQuicksave, previousLastCheckPoint );
		syncNextGameFrame = true;
		soundSystem->SetMute( insideExecuteMapChange );
		return false;
	}

	if ( !previewReady ) {
		Session_RemoveRelativeSaveFile( previewFile );
	}

	syncNextGameFrame = true;
	soundSystem->SetMute( insideExecuteMapChange );

	common->Printf( "Saved '%s'\n", saveSlotName.c_str() );
	operationGuard.Complete();

	return true;
#endif
}

/*
===============
idSessionLocal::LoadGame
===============
*/
bool idSessionLocal::LoadGame( const char *saveName ) { 
#ifdef	ID_DEDICATED
	common->Printf( "Dedicated servers cannot load games.\n" );
	return false;
#else
	int i;
	int loadedSavegameVersion = 0;
	idStr in, loadFile, saveMap, gamename, entityFilter, requestedSaveName, normalizedSaveMap, normalizedEntityFilter;
	idDict loadedPersistentPlayerInfo[MAX_ASYNC_CLIENTS];

	if ( IsMultiplayer() ) {
		common->Printf( "Can't load during net play.\n" );
		return false;
	}

	requestedSaveName = ( saveName != NULL && saveName[0] != '\0' ) ? saveName : com_lastQuicksave.GetString();
	if ( requestedSaveName.IsEmpty() ) {
		requestedSaveName = "Quicksave0";
	}

	loadFile = requestedSaveName;
	ScrubSaveGameFileName( loadFile );
	if ( loadFile.IsEmpty() ) {
		common->Warning( "Save name '%s' does not contain a valid file name\n", requestedSaveName.c_str() );
		return false;
	}

	const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );
	if ( idStr::Icmp( activeModule, "game_sp" ) != 0 ) {
		cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
		cvarSystem->SetCVarString( "com_nextGameModule", "game_sp" );
		idCmdArgs reloadArgs;
		reloadArgs.AppendArg( "loadGame" );
		reloadArgs.AppendArg( requestedSaveName.c_str() );
		cmdSystem->SetupReloadGameModule( reloadArgs );
		return true;
	}

	//Hide the dialog box if it is up.
	StopBox();

	loadFile.SetFileExtension( ".save" );

	in = "savegames/";
	in += loadFile;
	idStr descriptionPath = in;
	descriptionPath.SetFileExtension( ".txt" );
	idStr previewPath = in;
	previewPath.SetFileExtension( ".tga" );
	if ( !Session_RecoverInterruptedSaveFiles( in, descriptionPath, previewPath ) ) {
		common->Warning( "Could not recover interrupted save files for '%s'", in.c_str() );
		return false;
	}

	// Open savegame file
	// only allow loads from the game directory because we don't want a base game to load
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *loadGameFile = fileSystem->OpenFileRead( in, true, game.Length() ? game : NULL );

	if ( loadGameFile == NULL ) {
		common->Warning( "Couldn't open savegame file %s", in.c_str() );
		return false;
	}
	int loadGameFileLength = -1;
	if ( !Session_GetBoundedSaveGameLength( loadGameFile, in, "load preflight", loadGameFileLength ) ) {
		fileSystem->CloseFile( loadGameFile );
		return false;
	}

	loadingSaveGame = false;
	savegameFile = NULL;

	// Read in save game header
	// Game Name / Version / Map Name / [Entity Filter] / Persistent Player Info

	// game
	bool headerValid = true;
	headerValid = headerValid && Session_ReadSaveGameString( loadGameFile, gamename, MAX_STRING_CHARS, "game name", in.c_str() );
	if ( headerValid && !Session_IsSupportedSaveGameName( gamename ) ) {
		common->Warning( "Attempted to load an invalid savegame header '%s' from %s", gamename.c_str(), in.c_str() );
		headerValid = false;
	}

	// version
	headerValid = headerValid && Session_ReadSaveGameInt( loadGameFile, loadedSavegameVersion, "version", in.c_str() );
	if ( headerValid && !Session_IsCompatibleSaveGameVersion( loadedSavegameVersion ) ) {
		common->Warning( "Savegame '%s' has unsupported outer version %d", in.c_str(), loadedSavegameVersion );
		headerValid = false;
	}

	// map
	headerValid = headerValid && Session_ReadSaveGameString( loadGameFile, saveMap, MAX_STRING_CHARS, "map name", in.c_str() );

	// retail Quake 4 stores an entity filter after the map name.
	// Older openQ4 saves omitted it, so only consume it from retail-style headers.
	if ( headerValid && Session_SaveGameHeaderUsesEntityFilter( gamename ) ) {
		headerValid = Session_ReadSaveGameString( loadGameFile, entityFilter, MAX_STRING_CHARS, "entity filter", in.c_str() );
	} else {
		entityFilter.Clear();
	}

	// persistent player info
	for ( i = 0; headerValid && i < MAX_ASYNC_CLIENTS; i++ ) {
		headerValid = Session_ReadSaveGameDict( loadGameFile, loadedPersistentPlayerInfo[i], va( "persistent player info %d", i ), in.c_str() );
	}
	if ( headerValid ) {
		// Reject truncated, stale, or source-incompatible payloads before
		// ExecuteMapChange tears down the currently running map.
		headerValid = Session_ValidateSaveGamePayload( loadGameFile, in, true );
	}
	if ( headerValid && saveMap.IsEmpty() ) {
		common->Warning( "Savegame '%s' has an empty map name", in.c_str() );
		headerValid = false;
	}
	if ( headerValid ) {
		Session_NormalizeMapPathAndEntityFilter( saveMap.c_str(), entityFilter.c_str(), normalizedSaveMap, normalizedEntityFilter );
		if ( !Session_IsSafeNormalizedMapPath( normalizedSaveMap ) ) {
			common->Warning( "Savegame '%s' has unsafe map path '%s'", in.c_str(), saveMap.c_str() );
			headerValid = false;
		} else if ( !Session_SaveGameMapExists( normalizedSaveMap, normalizedEntityFilter ) ) {
			common->Warning( "Savegame '%s' references unavailable map '%s'", in.c_str(), normalizedSaveMap.c_str() );
			headerValid = false;
		}
	}
	if ( !headerValid ) {
		fileSystem->CloseFile( loadGameFile );
		return false;
	}

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.persistentPlayerInfo[i] = loadedPersistentPlayerInfo[i];
	}
	savegameVersion = loadedSavegameVersion;

	loadingSaveGame = true;
	savegameFile = loadGameFile;
	loadGameFile = NULL;

	common->DPrintf( "loading a v%d savegame\n", savegameVersion );

	if ( saveMap.Length() > 0 ) {
		// Start loading map
		mapSpawnData.serverInfo.Clear();

		mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
		mapSpawnData.serverInfo.Set( "si_gameType", "singleplayer" );

		mapSpawnData.serverInfo.Set( "si_map", normalizedSaveMap );
		Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, normalizedEntityFilter.c_str() );

		mapSpawnData.syncedCVars.Clear();
		mapSpawnData.syncedCVars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );

		mapSpawnData.mapSpawnUsercmd[0] = usercmdGen->TicCmd( latchedTicNumber );
		// make sure no buttons are pressed
		mapSpawnData.mapSpawnUsercmd[0].buttons = 0;

		ExecuteMapChange();

		SetGUI( NULL, NULL );
	}

	if ( loadingSaveGame ) {
		fileSystem->CloseFile( savegameFile );
		loadingSaveGame = false;
		savegameFile = NULL;
	}

	return true;
#endif
}

/*
===============
idSessionLocal::DeleteGame
===============
*/
bool idSessionLocal::DeleteGame( const char *saveName ) {
	if ( saveName == NULL || saveName[0] == '\0' ) {
		return false;
	}

	idStr deleteFile = saveName;
	ScrubSaveGameFileName( deleteFile );
	if ( deleteFile.IsEmpty() ) {
		common->Warning( "Save name '%s' does not contain a valid file name\n", saveName );
		return false;
	}

	idStr quicksaveName = com_lastQuicksave.GetString();
	idStr quicksaveFile = quicksaveName;
	ScrubSaveGameFileName( quicksaveFile );
	if ( !idStr::Icmp( deleteFile, quicksaveFile ) ) {
		int currentSlot = 0;
		const bool hasSlotSuffix = Session_GetTrailingDigit( quicksaveName, currentSlot );
		const int previousSlot = ( currentSlot - 1 ) & 3;
		if ( hasSlotSuffix ) {
			quicksaveName = quicksaveName.Left( quicksaveName.Length() - 1 );
		}
		if ( quicksaveName.IsEmpty() ) {
			quicksaveName = "Quicksave";
		}
		quicksaveName += va( "%d", previousSlot );
		com_lastQuicksave.SetString( quicksaveName );
	}

	static const char *SAVE_EXTENSIONS[] = { ".save", ".tga", ".txt" };
	for ( int i = 0; i < static_cast<int>( sizeof( SAVE_EXTENSIONS ) / sizeof( SAVE_EXTENSIONS[0] ) ); i++ ) {
		idStr path = va( "savegames/%s%s", deleteFile.c_str(), SAVE_EXTENSIONS[i] );
		fileSystem->RemoveFile( path.c_str() );
		path += ".tmp";
		fileSystem->RemoveFile( path.c_str() );
		path = va( "savegames/%s%s.prev", deleteFile.c_str(), SAVE_EXTENSIONS[i] );
		fileSystem->RemoveFile( path.c_str() );
	}
	return true;
}

/*
===============
idSessionLocal::ProcessEvent
===============
*/
bool idSessionLocal::ProcessEvent( const sysEvent_t *event ) {
	// hitting escape anywhere brings up the menu
	if ( !guiActive && event->evType == SE_KEY && event->evValue2 == 1 &&
		( event->evValue == K_ESCAPE || event->evValue == K_JOY7 || event->evValue == K_JOY8 ) ) {
		console->Close();
		if ( IsDemoPlaybackActive() ) {
			OpenDemoMenu( false );
			return true;
		}
		if ( game ) {
			idUserInterface	*gui = NULL;
			escReply_t		op;
			op = game->HandleESC( &gui );
			if ( op == ESC_IGNORE ) {
				return true;
			} else if ( op == ESC_GUI ) {
				SetGUI( gui, NULL );
				return true;
			}
		}
		StartMenu();
		return true;
	}

	// let the pull-down console take it if desired
	if ( console->ProcessEvent( event, false ) ) {
		return true;
	}

	// if we are testing a GUI, send all events to it
	if ( guiTest ) {
		// hitting escape exits the testgui
		if ( event->evType == SE_KEY && event->evValue2 == 1 && event->evValue == K_ESCAPE ) {
			guiTest = NULL;
			return true;
		}
		
		static const char *cmd;
		cmd = guiTest->HandleEvent( event, common->GetPresentationTime() );
		if ( cmd && cmd[0] ) {
			common->Printf( "testGui event returned: '%s'\n", cmd );
		}
		return true;
	}

	// menus / etc
	if ( guiActive ) {
		MenuEvent( event );
		return true;
	}

	// if we aren't in a game, force the console to take it
	if ( !mapSpawned ) {
		console->ProcessEvent( event, true );
		return true;
	}

	// in game, exec bindings for all key downs
	if ( event->evType == SE_KEY && event->evValue2 == 1 ) {
		idKeyInput::ExecKeyBinding( event->evValue );
		return true;
	}

	return false;
}

/*
===============
idSessionLocal::DrawWipeModel

Draw the fade material over everything that has been drawn
===============
*/
void	idSessionLocal::DrawWipeModel() {
	int		latchedTic = com_ticNumber;

	if (  wipeStartTic >= wipeStopTic ) {
		return;
	}

	if ( !wipeHold && latchedTic >= wipeStopTic ) {
		return;
	}

	float fade = ( float )( latchedTic - wipeStartTic ) / ( wipeStopTic - wipeStartTic );
	renderSystem->SetColor4( 1, 1, 1, fade );
	renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, wipeMaterial );
}

/*
===============
idSessionLocal::AdvanceRenderDemo
===============
*/
void idSessionLocal::AdvanceRenderDemo( bool singleFrameOnly ) {
	if ( readDemo == NULL ) {
		return;
	}
	if ( renderDemoSeeking && !singleFrameOnly ) {
		ProcessRenderDemoSeekBudget();
		return;
	}
	if ( lastDemoTic == -1 ) {
		lastDemoTic = latchedTicNumber - 1;
	}

	int skipFrames = 0;

	if ( !aviCaptureMode && !timeDemo && !singleFrameOnly ) {
		const int scheduledDemoTic =
			latchedTicNumber - latchedTicNumber % USERCMD_PER_DEMO_FRAME;
		if ( renderDemoPaused && !renderDemoStepPending ) {
			lastDemoTic = scheduledDemoTic;
			return;
		}
		if ( renderDemoStepPending ) {
			renderDemoStepPending = false;
			skipFrames = 0;
		} else {
			const int elapsedDemoFrames = Max( 0,
				( scheduledDemoTic - lastDemoTic ) / USERCMD_PER_DEMO_FRAME );
			lastDemoTic = scheduledDemoTic;
			if ( elapsedDemoFrames <= 0 ) {
				return;
			}
			renderDemoFrameAccumulator +=
				elapsedDemoFrames *
				idMath::ClampFloat( 0.05f, 16.0f, renderDemoPlaybackRate );
			int framesToAdvance = static_cast<int>( renderDemoFrameAccumulator );
			if ( framesToAdvance <= 0 ) {
				return;
			}
			framesToAdvance = Min( framesToAdvance, 64 );
			renderDemoFrameAccumulator -= framesToAdvance;
			skipFrames = framesToAdvance - 1;
		}
		lastDemoTic = scheduledDemoTic;
	} else {
		// always advance a single frame with avidemo and timedemo
		lastDemoTic = latchedTicNumber; 
	}

	while( skipFrames > -1 ) {
		if ( renderDemoSeeking && singleFrameOnly &&
			 renderDemoSeekDeadlineMS > 0 &&
			 Sys_Milliseconds() > renderDemoSeekDeadlineMS ) {
			renderDemoSeekWorkDeferred = true;
			break;
		}
		int		ds = DS_FINISHED;
		const int tokenBytes = readDemo->ReadInt( ds );
		if ( tokenBytes != 0 && tokenBytes != sizeof( ds ) ) {
			common->Warning( "Render demo '%s' has a truncated top-level token; playback stopped safely",
				activeRenderDemoName.c_str() );
			Stop();
			StartMenu( false );
			break;
		}
		if ( ds == DS_FINISHED ) {
			if ( renderDemoFramePayloadPending ) {
				// Early demoShot writers could end directly after a complete
				// first render view without emitting DC_END_FRAME. Accept only
				// that exact EOF case. Any later unterminated frame is a
				// malformed multi-frame stream, not a one-frame demo.
				if ( tokenBytes == 0 && readDemo->IsOpen() &&
					 singleFrameOnly &&
					 numDemoFrames == 0 &&
					 demoTimeOffset != idMath::INT_MIN ) {
					renderDemoFramePayloadPending = false;
					numDemoFrames = 1;
					renderDemoKnownDurationMS = 0;
					renderDemoDurationName = activeRenderDemoName;
					common->DPrintf( "Accepted legacy render demoShot without DC_END_FRAME: %s\n",
						activeRenderDemoName.c_str() );
					break;
				}
				common->Warning( "Render demo '%s' ended before DC_END_FRAME; playback stopped safely",
					activeRenderDemoName.c_str() );
				Stop();
				StartMenu( false );
				break;
			}
			if ( numDemoFrames <= 0 ) {
				common->Warning( "Render demo '%s' contains no complete frames",
					activeRenderDemoName.c_str() );
				Stop();
				StartMenu( false );
				break;
			}
			renderDemoKnownDurationMS = Max( 0,
				currentDemoRenderView.time - renderDemoStartViewTime );
			renderDemoDurationName = activeRenderDemoName;
			if ( numDemoFrames != 1 ) {
				// if the demo has a single frame (a demoShot), continuously replay
				// the renderView that has already been read
				Stop();
				StartMenu();
			}
			break;
		}
		if ( ds == DS_RENDER ) {
			renderDemoFramePayloadPending = true;
			if ( rw->ProcessDemoCommand( readDemo, &currentDemoRenderView, &demoTimeOffset ) ) {
				// The render world returns true once the full frame payload has been consumed.
				renderDemoFramePayloadPending = false;
				skipFrames--;
				numDemoFrames++;
			}
			continue;
		}
		if ( ds == DS_SOUND ) {
			renderDemoFramePayloadPending = true;
			if ( !sw->ProcessDemoCommand( readDemo ) ) {
				common->Warning( "Render demo '%s' contains a malformed sound command; playback stopped safely",
					activeRenderDemoName.c_str() );
				Stop();
				StartMenu( false );
				break;
			}
			continue;
		}
		// appears in v1.2, with savegame format 17
		if ( ds == DS_VERSION ) {
			int streamVersion = 0;
			if ( readDemo->ReadInt( streamVersion ) != sizeof( streamVersion ) ) {
				common->Warning( "Render demo '%s' has a truncated version token; playback stopped safely",
					activeRenderDemoName.c_str() );
				Stop();
				StartMenu( false );
				break;
			}
			if ( streamVersion < 1 ||
				 streamVersion > OPENQ4_RENDERDEMO_CURRENT_VERSION ) {
				common->Warning( "Render demo '%s' uses unsupported stream version %d (supported: 1-%d)",
					activeRenderDemoName.c_str(), streamVersion,
					OPENQ4_RENDERDEMO_CURRENT_VERSION );
				Stop();
				StartMenu( false );
				break;
			}
			renderdemoVersion = streamVersion;
			common->Printf( "reading a v%d render demo\n", renderdemoVersion );
			// set the savegameVersion to current for render demo paths that share the savegame paths
			savegameVersion = SAVEGAME_VERSION;
			continue;
		}
		common->Warning( "Render demo '%s' contains invalid token %d; playback stopped safely",
			activeRenderDemoName.c_str(), ds );
		Stop();
		StartMenu( false );
		break;
	}

	if ( com_showDemo.GetBool() ) {
		common->Printf( "frame:%i DemoTic:%i latched:%i skip:%i\n", numDemoFrames, lastDemoTic, latchedTicNumber, skipFrames );
	}

}

/*
===============
idSessionLocal::DrawCmdGraph

Graphs yaw angle for testing smoothness
===============
*/
static const int	ANGLE_GRAPH_HEIGHT = 128;
static const int	ANGLE_GRAPH_STRETCH = 3;
void idSessionLocal::DrawCmdGraph() {
	if ( !com_showAngles.GetBool() ) {
		return;
	}
	renderSystem->SetColor4( 0.1f, 0.1f, 0.1f, 1.0f );
	renderSystem->DrawStretchPic( 0, 480-ANGLE_GRAPH_HEIGHT, MAX_BUFFERED_USERCMD*ANGLE_GRAPH_STRETCH, ANGLE_GRAPH_HEIGHT, 0, 0, 1, 1, whiteMaterial );
	renderSystem->SetColor4( 0.9f, 0.9f, 0.9f, 1.0f );
	for ( int i = 0 ; i < MAX_BUFFERED_USERCMD-4 ; i++ ) {
		usercmd_t	cmd = usercmdGen->TicCmd( latchedTicNumber - (MAX_BUFFERED_USERCMD-4) + i );
		int h = cmd.angles[1];
		h >>= 8;
		h &= (ANGLE_GRAPH_HEIGHT-1);
		renderSystem->DrawStretchPic( i* ANGLE_GRAPH_STRETCH, 480-h, 1, h, 0, 0, 1, 1, whiteMaterial );
	}
}

/*
===============
idSessionLocal::BeginLoadingAssetQueue
===============
*/
void idSessionLocal::BeginLoadingAssetQueue( int totalAssets ) {
	loadingAssetQueueActive = false;
	loadingAssetQueueTotal = 0;
	loadingAssetQueueLoaded = 0;

	if ( totalAssets <= 0 || !insideExecuteMapChange || guiLoading == NULL ) {
		return;
	}

	loadingAssetQueueActive = true;
	loadingAssetQueueTotal = totalAssets;
	loadingAssetQueueStartPct = idMath::ClampFloat( 0.0f, 1.0f, guiLoading->State().GetFloat( "map_loading" ) );
}

/*
===============
idSessionLocal::AdvanceLoadingAssetQueue
===============
*/
void idSessionLocal::AdvanceLoadingAssetQueue( int loadedAssets ) {
	if ( !loadingAssetQueueActive || loadedAssets <= 0 ) {
		return;
	}

	loadingAssetQueueLoaded = Min( loadingAssetQueueTotal, loadingAssetQueueLoaded + loadedAssets );
	PacifierUpdate();
}

/*
===============
idSessionLocal::EndLoadingAssetQueue
===============
*/
void idSessionLocal::EndLoadingAssetQueue() {
	if ( !loadingAssetQueueActive ) {
		return;
	}

	loadingAssetQueueLoaded = loadingAssetQueueTotal;
	PacifierUpdate();

	loadingAssetQueueActive = false;
	loadingAssetQueueTotal = 0;
	loadingAssetQueueLoaded = 0;
}

/*
===============
idSessionLocal::PacifierUpdate
===============
*/
void idSessionLocal::PacifierUpdate() {
	if ( !insideExecuteMapChange || insidePacifierUpdate ) {
		return;
	}
	// Printing from a loading redraw or network send offers another pacifier
	// update. The time gate alone cannot prevent recursion when that work takes
	// longer than the interval: a nested send can mutate an unfinished fragment.
	struct PacifierScope {
		bool &active;
		explicit PacifierScope( bool &flag ) : active( flag ) { active = true; }
		~PacifierScope() { active = false; }
	} scope( insidePacifierUpdate );

#ifdef ID_DEDICATED
	// Dedicated map loads have no client presentation path, but the server
	// pacifier must still run often enough to keep connected clients alive.
	const int dedicatedTime = Sys_Milliseconds();
	const float dedicatedMinIntervalMs = Session_GetBlockingLoadFrameIntervalMsec();
	if ( lastPacifierTime != 0 && static_cast<float>( dedicatedTime - lastPacifierTime ) < dedicatedMinIntervalMs ) {
		return;
	}
	lastPacifierTime = dedicatedTime;
	idAsyncNetwork::server.PacifierUpdate();
	return;
#endif

	// never do pacifier screen updates while inside the
	// drawing code, or we can have various recursive problems
	if ( insideUpdateScreen ) {
		return;
	}

	const int time = Sys_Milliseconds();
	const float minPacifierIntervalMs = Session_GetBlockingLoadFrameIntervalMsec();

	// Charge the previous redraw's own cost against the interval. lastPacifierTime is
	// stamped after the redraw completes, so this gate measures loading work only; a
	// slow present path therefore stretches the gap between redraws instead of
	// consuming the load. Without both halves the gate is already satisfied the
	// instant a redraw finishes whenever a redraw outlasts its interval, and every
	// asset read then presents a full frame.
	const float pacifierIntervalMs = Max( minPacifierIntervalMs,
		static_cast<float>( lastPacifierDrawMsec ) * SESSION_PACIFIER_DRAW_BUDGET_RATIO );

	int elapsedMs = time - lastPacifierTime;
	if ( lastPacifierTime != 0 && static_cast<float>( elapsedMs ) < pacifierIntervalMs ) {
		return;
	}

	Session_BeginBlockingLoadPresentationFrame();

	const int presentationTime = common->GetPresentationTime();
	elapsedMs = presentationTime - lastPacifierTime;
	if ( static_cast<float>( elapsedMs ) < pacifierIntervalMs ) {
		elapsedMs = static_cast<int>( idMath::Ceil( pacifierIntervalMs ) );
	}

	if ( guiLoading ) {
		float shownPct = idMath::ClampFloat( 0.0f, 1.0f, guiLoading->State().GetFloat( "map_loading" ) );
		float targetPct = shownPct;
		float byteTargetPct = shownPct;
		bool hasByteTarget = false;

		if ( bytesNeededForMapLoad > 0 ) {
			const float n = Max( 0.0f, static_cast<float>( fileSystem->GetReadCount() ) );
			byteTargetPct = idMath::ClampFloat( 0.0f, 1.0f, n / bytesNeededForMapLoad );
			targetPct = byteTargetPct;
			hasByteTarget = true;
		}

		if ( loadingAssetQueueActive && loadingAssetQueueTotal > 0 ) {
			const float queueFloorFraction = idMath::ClampFloat( 0.0f, 1.0f,
				static_cast<float>( loadingAssetQueueLoaded ) / static_cast<float>( loadingAssetQueueTotal ) );
			const int nextLoaded = Min( loadingAssetQueueTotal, loadingAssetQueueLoaded + 1 );
			const float queueCeilFraction = idMath::ClampFloat( 0.0f, 1.0f,
				static_cast<float>( nextLoaded ) / static_cast<float>( loadingAssetQueueTotal ) );
			const float queueFloor = loadingAssetQueueStartPct + ( 1.0f - loadingAssetQueueStartPct ) * queueFloorFraction;
			const float queueCeil = loadingAssetQueueStartPct + ( 1.0f - loadingAssetQueueStartPct ) * queueCeilFraction;

			// Keep queue accounting authoritative, but allow byte-read progress to animate
			// smoothly within the current queued-asset bucket.
			if ( hasByteTarget ) {
				targetPct = idMath::ClampFloat( queueFloor, queueCeil, byteTargetPct );
			} else {
				targetPct = queueFloor;
			}
		}

		// Loading bars should be monotonic.
		targetPct = Max( targetPct, shownPct );

		// Keep progress accurate, but smooth visual jumps when read-count deltas arrive in bursts.
		const float alpha = idMath::ClampFloat( 0.0f, 1.0f, ( elapsedMs * 0.001f ) * 20.0f );
		if ( targetPct >= shownPct ) {
			shownPct += ( targetPct - shownPct ) * alpha;
			if ( ( targetPct - shownPct ) < 0.002f ) {
				shownPct = targetPct;
			}
		}

		guiLoading->SetStateFloat( "map_loading", shownPct );
		guiLoading->StateChanged( presentationTime );
	}

	Sys_GenerateEvents();

	const int drawStartMsec = Sys_Milliseconds();
	UpdateScreen();
	lastPacifierDrawMsec = Max( 0, Sys_Milliseconds() - drawStartMsec );

	// Stamp after the redraw, on the same clock the gate above reads, so the redraw's
	// own cost is never counted as part of the interval it is throttled by.
	lastPacifierTime = Sys_Milliseconds();

	idAsyncNetwork::client.PacifierUpdate();
	idAsyncNetwork::server.PacifierUpdate();
}

/*
===============
idSessionLocal::Draw
===============
*/
void idSessionLocal::Draw() {
	static const int fallbackMenuDelayMs = 3000;
	const int presentationTime = common->GetPresentationTime();
	if ( guiDemoMenu != NULL && ( IsDemoPlaybackActive() || guiActive == guiDemoMenu ) ) {
		UpdateDemoMenuGui();
	}

	bool fullConsole = false;
	float menuIntroBlackoutAlpha = 0.0f;
	bool drawingFallbackLoadingScreen = false;

	if ( insideExecuteMapChange ) {
		if ( guiLoading ) {
			guiLoading->Redraw( presentationTime );
		}
		if ( guiActive == guiMsg ) {
			guiMsg->Redraw( presentationTime );
		} 
	} else if ( guiTest ) {
		// if testing a gui, clear the screen and draw it
		// clear the background, in case the tested gui is transparent
		// NOTE that you can't use this for aviGame recording, it will tick at real presentation time between screenshots..
		renderSystem->SetColor( colorBlack );
		renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
		guiTest->Redraw( presentationTime );
	} else if ( guiActive ) {
		
		// draw the frozen gui in the background
		if ( guiActive == guiMsg && guiMsgRestore ) {
			guiMsgRestore->Redraw( presentationTime );
		}
		
		// draw the menus full screen
		if ( guiActive == guiTakeNotes && !com_skipGameDraw.GetBool() ) {
			game->Draw( GetLocalClientNum() );
		}

		if ( guiActive == guiDemoMenu && readDemo != NULL ) {
			// Playback controls are translucent, so keep the current recorded
			// frame live behind them even though this GUI does not request the
			// ordinary in-game "gameDraw" path.
			rw->RenderScene( &currentDemoRenderView );
			renderSystem->DrawDemoPics();
		} else if ( guiActive->State().GetBool( "gameDraw" ) ) {
			if ( mapSpawned && !com_skipGameDraw.GetBool() && GetLocalClientNum() >= 0 ) {
				bool gameDraw = game->Draw( GetLocalClientNum() );
				if ( !gameDraw ) {
					renderSystem->SetColor( colorBlack );
					renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
				}
			} else {
				renderSystem->SetColor( colorBlack );
				renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
			}
		}

		guiActive->Redraw( presentationTime );

		if ( guiActive == guiMainMenu ) {
			if ( menuIntroBlackoutActive ) {
				if ( menuIntroBlackoutAwaitMenuMusic ) {
					menuIntroBlackoutAlpha = 1.0f;
				} else {
				if ( menuIntroBlackoutFadeStart < 0 ) {
					menuIntroBlackoutFadeStart = presentationTime;
				}

				const float fadeElapsed = static_cast<float>( presentationTime - menuIntroBlackoutFadeStart );
				const float fadeDurationMs = 350.0f;
				const float t = idMath::ClampFloat( 0.0f, 1.0f, fadeElapsed / fadeDurationMs );
				menuIntroBlackoutAlpha = 1.0f - t;

				if ( t >= 1.0f ) {
					menuIntroBlackoutActive = false;
					menuIntroBlackoutAwaitMenuMusic = false;
					menuIntroBlackoutFadeStart = -1;
					menuIntroBlackoutAlpha = 0.0f;
				}
				}
			}
		} else {
			menuIntroBlackoutActive = false;
			menuIntroBlackoutAwaitMenuMusic = false;
			menuIntroBlackoutFadeStart = -1;
		}
	} else if ( readDemo ) {
		rw->RenderScene( &currentDemoRenderView );
		renderSystem->DrawDemoPics();
	} else if ( mapSpawned ) {
		bool gameDraw = false;
		// normal drawing for both single and multi player
		if ( !com_skipGameDraw.GetBool() && GetLocalClientNum() >= 0 ) {
			// draw the game view
			int	start = Sys_Milliseconds();
			gameDraw = game->Draw( GetLocalClientNum() );
			int end = Sys_Milliseconds();
			time_gameDraw += ( end - start );	// note time used for com_speeds
		}
		if ( !gameDraw ) {
			renderSystem->SetColor( colorBlack );
			renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
		}

		// save off the 2D drawing from the game
		if ( writeDemo ) {
			renderSystem->WriteDemoPics();
		}
	} else {
#if ID_CONSOLE_LOCK
		if ( con_allowConsole.GetBool() ) {
			drawingFallbackLoadingScreen = true;
			if ( fallbackMenuStartTime < 0 ) {
				fallbackMenuStartTime = Sys_Milliseconds();
			}
			Session_DrawFallbackLoadingScreen();
			if ( guiMainMenu != NULL && ( Sys_Milliseconds() - fallbackMenuStartTime ) >= fallbackMenuDelayMs ) {
				StartMenu();
			}
		} else {
			emptyDrawCount++;
			if ( emptyDrawCount > 5 ) {
				// it's best if you can avoid triggering the watchgod by doing the right thing somewhere else
				assert( false );
				common->Warning( "idSession: triggering mainmenu watchdog" );
				emptyDrawCount = 0;
				StartMenu();
			}
			renderSystem->SetColor4( 0, 0, 0, 1 );
			renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
		}
#else
		// when this fallback branch is hit, show loading instead of a full-screen console
		drawingFallbackLoadingScreen = true;
		if ( fallbackMenuStartTime < 0 ) {
			fallbackMenuStartTime = Sys_Milliseconds();
		}
		Session_DrawFallbackLoadingScreen();
		if ( guiMainMenu != NULL && ( Sys_Milliseconds() - fallbackMenuStartTime ) >= fallbackMenuDelayMs ) {
			StartMenu();
		}
#endif
		fullConsole = true;
	}

	if ( !drawingFallbackLoadingScreen ) {
		fallbackMenuStartTime = -1;
	}

	if ( menuIntroBlackoutAlpha > 0.0f ) {
		const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
		const float screenW = static_cast<float>( renderSystem->GetScreenWidth() );
		const float screenH = static_cast<float>( renderSystem->GetScreenHeight() );
		renderSystem->SetUseUIViewportFor2D( false );
		renderSystem->SetColor4( 0.0f, 0.0f, 0.0f, menuIntroBlackoutAlpha );
		renderSystem->DrawStretchPic( 0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 1.0f, 1.0f, whiteMaterial );
		renderSystem->SetColor( colorWhite );
		renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
	}

#if ID_CONSOLE_LOCK
	if ( !fullConsole && emptyDrawCount ) {
		common->DPrintf( "idSession: %d empty frame draws\n", emptyDrawCount );
		emptyDrawCount = 0;
	}
	fullConsole = false;
#endif

	if ( mapSpawned && !insideExecuteMapChange && !guiActive && !readDemo ) {
		DrawIAmTheDukeOverlay();
		Session_DrawLevelshotBounds();
	}

	if ( guiActive == NULL && demoOverlayVisible && IsDemoPlaybackActive() &&
		 guiDemoMenu != NULL && !insideExecuteMapChange ) {
		demoBrowserMode = false;
		UpdateDemoMenuGui();
		guiDemoMenu->Redraw( presentationTime );
	}

	// draw the wipe material on top of this if it hasn't completed yet
	RetainedUI_Draw();
	DrawWipeModel();
	
	// draw debug graphs
	DrawCmdGraph();

	// draw the half console / notify console on top of everything
	if ( !fullConsole && !IsMainMenuIntroPlaying() ) {
		console->Draw( false );
	}
}

/*
===============
idSessionLocal::UpdateScreen
===============
*/
void idSessionLocal::UpdateScreen( bool outOfSequence ) {

#ifdef ID_DEDICATED
	return;
#endif

#ifdef _WIN32

	if ( com_editors ) {
		if ( !Sys_IsWindowVisible() ) {
			return;
		}
	}
#endif

	if ( insideUpdateScreen ) {
		return;
//		common->FatalError( "idSessionLocal::UpdateScreen: recursively called" );
	}

	insideUpdateScreen = true;

	// if this is a long-operation update and we are in windowed mode,
	// release the mouse capture back to the desktop
	if ( outOfSequence ) {
		Sys_GrabMouseCursor( false );
	}

	renderSystem->SetLoadingScreenSwapIntervalBypass( insideExecuteMapChange );

	renderSystem->BeginFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );

	// draw everything
	Draw();

	if ( com_speeds.GetBool() ) {
		renderSystem->EndFrame( &time_frontend, &time_backend );
	} else {
		renderSystem->EndFrame( NULL, NULL );
	}

	insideUpdateScreen = false;
}

/*
===============
idSessionLocal::Frame
===============
*/
void idSessionLocal::Frame() {
	// Re-derive the playing sound world from the current focus state before the
	// mix runs, so s_muteUnfocused can never leave audio switched off once the
	// window is back.
	UpdateSoundWorldFocus();

	soundSystem->Render();

	// Editors that completely take over the game
	if ( com_editorActive && ( com_editors & ( EDITOR_RADIANT | EDITOR_GUI ) ) ) {
		return;
	}

	const int frameStartMsec = Sys_Milliseconds();

	// if the console is down, we don't need to hold
	// the mouse cursor
	if ( console->Active() || com_editorActive ) {
		Sys_GrabMouseCursor( false );
	} else {
		Sys_GrabMouseCursor( true );
	}

	// save the screenshot and audio from the last draw if needed
	if ( aviCaptureMode ) {
		idStr	name;

		name = va("demos/%s/%s_%05i.tga", aviDemoShortName.c_str(), aviDemoShortName.c_str(), aviTicStart );

		const float demoFrameRate = static_cast<float>( common->GetUserCmdHz() ) / static_cast<float>( com_aviDemoTics.GetInteger() );
		float ratio = 30.0f / demoFrameRate;
		aviDemoFrameCount += ratio;
		if ( aviTicStart + 1 != ( int )aviDemoFrameCount ) {
			// skipped frames so write them out
			int c = aviDemoFrameCount - aviTicStart;
			while ( c-- ) {
				//renderSystem->TakeScreenshot( com_aviDemoWidth.GetInteger(), com_aviDemoHeight.GetInteger(), name, com_aviDemoSamples.GetInteger(), NULL );
				name = va("demos/%s/%s_%05i.tga", aviDemoShortName.c_str(), aviDemoShortName.c_str(), ++aviTicStart );
			}
		}
		aviTicStart = aviDemoFrameCount;

		// remove any printed lines at the top before taking the screenshot
		console->ClearNotifyLines();

		// this will call Draw, possibly multiple times if com_aviDemoSamples is > 1
	//	renderSystem->TakeScreenshot( com_aviDemoWidth.GetInteger(), com_aviDemoHeight.GetInteger(), name, com_aviDemoSamples.GetInteger(), NULL );
	}

	// at startup, we may be backwards
	if ( latchedTicNumber > com_ticNumber ) {
		latchedTicNumber = com_ticNumber;
	}
	const int previousLatchedTicNumber = latchedTicNumber;

	// Phase 2 decouples presentation from simulation: only demos / fixed-rate capture
	// still wait for exact async tics before the foreground frame proceeds.
	int	minTic = previousLatchedTicNumber + 1;
	if ( com_minTics.GetInteger() > 1 ) {
		minTic = lastGameTic + com_minTics.GetInteger();
	}
	
	if ( readDemo ) {
		if ( renderDemoSeeking ) {
			// Replay-seek work is main-thread budgeted and does not represent
			// real-time presentation, so do not throttle it to the 30 Hz demo
			// cadence.
			minTic = previousLatchedTicNumber;
		} else if ( !timeDemo && numDemoFrames != 1 ) {
			minTic = lastDemoTic + USERCMD_PER_DEMO_FRAME;
		} else {
			// timedemos and demoshots will run as fast as they can, other demos
			// will not run more than 30 hz
			minTic = latchedTicNumber;
		}
	} else if ( writeDemo ) {
		minTic = lastGameTic + USERCMD_PER_DEMO_FRAME;		// demos are recorded at 30 hz
	}
	
	// fixedTic lets us run a forced number of usercmd each frame without timing
	if ( com_fixedTic.GetInteger() ) {
		minTic = previousLatchedTicNumber;
	} else if ( aviCaptureMode ) {
		// Fixed-rate capture should only advance once the next capture-sized block of
		// async tics is ready; otherwise repeated-state presentation frames can
		// incorrectly rerun capture work faster than the requested cadence.
		minTic = lastGameTic + com_aviDemoTics.GetInteger();
	}

	int requestedWaitMsec = 0;
	int actualWaitMsec = 0;
	const bool shouldWaitForGameTic = readDemo || writeDemo || aviCaptureMode;
	const int latchedTicBeforeWait = previousLatchedTicNumber;

	if ( shouldWaitForGameTic && minTic > latchedTicBeforeWait ) {
		requestedWaitMsec = common->GetUserCmdTime( minTic ) - common->GetUserCmdTime( latchedTicBeforeWait );
	}

	if ( shouldWaitForGameTic ) {
		const int waitStartMsec = Sys_Milliseconds();
#if defined( _WIN32 )
		// Demo / capture playback still consumes exact tics instead of repeated-state presentation.
		while( 1 ) {
			latchedTicNumber = com_ticNumber;
			if ( latchedTicNumber >= minTic ) {
				break;
			}
			Sys_Sleep( 1 );
		}
#else
		while( 1 ) {
			latchedTicNumber = com_ticNumber;
			if ( latchedTicNumber >= minTic ) {
				break;
			}
			Sys_WaitForEvent( TRIGGER_EVENT_ONE );
		}
#endif
		actualWaitMsec = Max( 0, Sys_Milliseconds() - waitStartMsec );
	} else {
		latchedTicNumber = com_ticNumber;
	}

	// send frame and mouse events to active guis
	GuiFrameEvents();

	// advance demos
	if ( readDemo ) {
		UpdateFramePacingStats( frameStartMsec, requestedWaitMsec, actualWaitMsec, 0 );
		AdvanceRenderDemo( false );
		return;
	}

	//------------ single player game tics --------------

	if ( !mapSpawned || guiActive ) {
		if ( !com_asyncInput.GetBool() ) {
			// early exit, won't do RunGameTic .. but still need to update mouse position for GUIs
			usercmdGen->GetDirectUsercmd();
		}
	}

	if ( !mapSpawned ) {
		UpdateFramePacingStats( frameStartMsec, requestedWaitMsec, actualWaitMsec, 0 );
		return;
	}

	if ( guiActive ) {
		lastGameTic = latchedTicNumber;
		UpdateFramePacingStats( frameStartMsec, requestedWaitMsec, actualWaitMsec, 0 );
		return;
	}

	// in message box / GUIFrame, idSessionLocal::Frame is used for GUI interactivity
	// but we early exit to avoid running game frames
	if ( idAsyncNetwork::IsActive() ) {
		UpdateFramePacingStats( frameStartMsec, requestedWaitMsec, actualWaitMsec, 0 );
		return;
	}

	// check for user info changes
	if ( cvarSystem->GetModifiedFlags() & CVAR_USERINFO ) {
		mapSpawnData.userInfo[0] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
		game->SetUserInfo( 0, mapSpawnData.userInfo[0], false );
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO );
	}

	// see how many usercmds we are going to run
	int	numCmdsToRun = latchedTicNumber - lastGameTic;
	if ( !shouldWaitForGameTic && latchedTicNumber < minTic ) {
		numCmdsToRun = 0;
	}

	// don't let a long onDemand sound load unsync everything
	if ( timeHitch ) {
		int	skip = common->GetUserCmdTicsForMsecFloor( timeHitch );
		lastGameTic += skip;
		numCmdsToRun -= skip;
		timeHitch = 0;
	}

	// don't get too far behind after a hitch
	if ( numCmdsToRun > 10 ) {
		lastGameTic = latchedTicNumber - 10;
	}

	// never use more than USERCMD_PER_DEMO_FRAME,
	// which makes it go into slow motion when recording
	if ( writeDemo ) {
		int fixedTic = USERCMD_PER_DEMO_FRAME;
		// we should have waited long enough
		if ( numCmdsToRun < fixedTic ) {
			common->Error( "idSessionLocal::Frame: numCmdsToRun < fixedTic" );
		}
		// we may need to dump older commands
		lastGameTic = latchedTicNumber - fixedTic;
	} else if ( com_fixedTic.GetInteger() > 0 ) {
		// this may cause commands run in a previous frame to
		// be run again if we are going at above the real time rate
		lastGameTic = latchedTicNumber - com_fixedTic.GetInteger();
	} else if (	aviCaptureMode ) {
		lastGameTic = latchedTicNumber - com_aviDemoTics.GetInteger();
	}

	// force only one game frame update this frame.  the game code requests this after skipping cinematics
	// so we come back immediately after the cinematic is done instead of a few frames later which can
	// cause sounds played right after the cinematic to not play.
	if ( syncNextGameFrame ) {
		if ( latchedTicNumber > lastGameTic ) {
			if ( com_showFramePacing.GetInteger() >= 2 ) {
				common->Printf( "syncNextGameFrame consuming next async tic (latched=%d, lastGameTic=%d)\n",
					latchedTicNumber, lastGameTic );
			}
			lastGameTic = latchedTicNumber - 1;
			syncNextGameFrame = false;
			syncNextGameFrameAwaitingAsyncTicLog = false;
		} else if ( com_showFramePacing.GetInteger() >= 2 && !syncNextGameFrameAwaitingAsyncTicLog ) {
			common->Printf( "syncNextGameFrame waiting for next async tic (latched=%d, lastGameTic=%d)\n",
				latchedTicNumber, lastGameTic );
			syncNextGameFrameAwaitingAsyncTicLog = true;
		}
	} else {
		syncNextGameFrameAwaitingAsyncTicLog = false;
	}

	// create client commands, which will be sent directly
	// to the game
	if ( com_showTics.GetBool() ) {
		common->Printf( "%i ", latchedTicNumber - lastGameTic );
	}

	int	gameTicsToRun = latchedTicNumber - lastGameTic;
	int i;
	for ( i = 0 ; i < gameTicsToRun ; i++ ) {
		RunGameTic();
		if ( !mapSpawned ) {
			// exited game play
			break;
		}
		if ( syncNextGameFrame ) {
			// long game frame, so break out and continue executing as if there was no hitch
			break;
		}
	}

	UpdateFramePacingStats( frameStartMsec, requestedWaitMsec, actualWaitMsec, i );
}

/*
================
idSessionLocal::RunGameTic
================
*/
void idSessionLocal::RunGameTic() {
	logCmd_t	logCmd;
	usercmd_t	cmd;

	// if we are doing a command demo, read or write from the file
	if ( cmdDemoFile ) {
		if ( cmdDemoFile->Read( &logCmd, sizeof( logCmd ) ) != sizeof( logCmd ) ) {
			common->Printf( "Command demo completed at logIndex %i\n", logIndex );
			fileSystem->CloseFile( cmdDemoFile );
			cmdDemoFile = NULL;
			if ( aviCaptureMode ) {
				EndAVICapture();
				Shutdown();
			}
			// we fall out of the demo to normal commands
			// the impulse and chat character toggles may not be correct, and the view
			// angle will definitely be wrong
		} else {
			cmd = logCmd.cmd;
			cmd.ByteSwap();
			logCmd.consistencyHash = LittleLong( logCmd.consistencyHash );
		}
	}
	
	// if we didn't get one from the file, get it locally
	if ( !cmdDemoFile ) {
		// get a locally created command
		if ( com_asyncInput.GetBool() ) {
			cmd = usercmdGen->TicCmd( lastGameTic );
		} else {
			cmd = usercmdGen->GetDirectUsercmd();
		}
		lastGameTic++;
	}

	// run the game logic every player move
	int	start = Sys_Milliseconds();
	gameReturn_t	ret = game->RunFrame( &cmd, 0, true, 0 ); // jmarshall: serverGameFrame isn't used

	int end = Sys_Milliseconds();
	time_gameFrame += end - start;	// note time used for com_speeds

	// check for constency failure from a recorded command
	if ( cmdDemoFile ) {
		if ( ret.consistencyHash != logCmd.consistencyHash ) {
			common->Printf( "Consistency failure on logIndex %i\n", logIndex );
			Stop();
			return;
		}
	}

	// save the cmd for cmdDemo archiving
	if ( logIndex < MAX_LOGGED_USERCMDS ) {
		loggedUsercmds[logIndex].cmd = cmd;
		// save the consistencyHash for demo playback verification
		loggedUsercmds[logIndex].consistencyHash = ret.consistencyHash;
		if (logIndex % 30 == 0 && statIndex < MAX_LOGGED_STATS) {
			loggedStats[statIndex].health = ret.health;
			loggedStats[statIndex].heartRate = ret.heartRate;
			loggedStats[statIndex].stamina = ret.stamina;
			loggedStats[statIndex].combat = ret.combat;
			statIndex++;
		}
		logIndex++;
	}

	syncNextGameFrame = ret.syncNextGameFrame;
	const bool gameInCinematic = game->InCinematic();
	if ( com_showFramePacing.GetInteger() >= 2 && ( !cinematicStateValid || gameInCinematic != cinematicActive ) ) {
		common->Printf(
			"cinematic %s (latched=%d, lastGameTic=%d, syncNextGameFrame=%d)\n",
			gameInCinematic ? "entered" : "exited",
			latchedTicNumber,
			lastGameTic,
			syncNextGameFrame ? 1 : 0 );
	}
	cinematicStateValid = true;
	cinematicActive = gameInCinematic;
	if ( syncNextGameFrame && com_showFramePacing.GetInteger() >= 2 ) {
		common->Printf( "syncNextGameFrame requested by game code (latched=%d, lastGameTic=%d)\n",
			latchedTicNumber, lastGameTic );
	}

	if ( ret.sessionCommand[0] ) {
		idCmdArgs args;

		args.TokenizeString( ret.sessionCommand, false );

		if ( !idStr::Icmp( args.Argv(0), "map" ) ) {
			idStr commandMap;
			idStr commandEntityFilter;
			Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), commandMap, commandEntityFilter );
			if ( commandMap.Length() == 0 ) {
				return;
			}

			// get current player states
			for ( int i = 0 ; i < numClients ; i++ ) {
				mapSpawnData.persistentPlayerInfo[i] = game->GetPersistentPlayerInfo( i );
			}
			// clear the devmap key on serverinfo, so player spawns
			// won't get the map testing items
			mapSpawnData.serverInfo.Delete( "devmap" );
			Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, commandEntityFilter.c_str() );

			// go to the next map
			MoveToNewMap( commandMap.c_str() );
		} else if ( !idStr::Icmp( args.Argv(0), "devmap" ) ) {
			idStr commandMap;
			idStr commandEntityFilter;
			Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), commandMap, commandEntityFilter );
			if ( commandMap.Length() == 0 ) {
				return;
			}

			mapSpawnData.serverInfo.Set( "devmap", "1" );
			Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, commandEntityFilter.c_str() );
			MoveToNewMap( commandMap.c_str() );
		} else if ( !idStr::Icmp( args.Argv(0), "nextMap" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "nextMap" );
		} else if ( !idStr::Icmp( args.Argv(0), "died" ) ) {
			// restart on the same map
			UnloadMap();
			SetGUI(guiRestartMenu, NULL);
		} else if ( !idStr::Icmp( args.Argv(0), "disconnect" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "stoprecording ; disconnect" );
		} else if ( !idStr::Icmp( args.Argv(0), "endOfDemo" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "endOfDemo" );
		} else if ( !idStr::Icmp( args.Argv(0), "objectiveFailed" ) ) {
			objectiveFailed = true;
		} else if ( !idStr::Icmp( args.Argv(0), "endOfGame" ) ) {
			if ( guiGameOver ) {
				SetGUI( guiGameOver, NULL );
			} else {
				cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "stoprecording ; disconnect" );
			}
		}
	}
}

#ifndef ID_DEDICATED
/*
===============
Session_DeclareMultiplayerMenuGameCVars

The create-server menu edits multiplayer settings that only game_mp declares,
but it is reachable while game_sp is loaded - the module is not swapped until
spawnServer. A GUI widget resolves its cvar once, when the gui is parsed, and
idChoiceWindow::InitVars warns and then does nothing at all when the name is
absent, so those rows would be inert for anyone who reaches multiplayer from a
cold start or straight out of single player.

Declare them here instead, with the same defaults game_mp uses so its later
static registration matches. That registration keeps the current value and only
adopts the reset value and range, so whatever the player picked in the menu is
what the match starts with.
===============
*/
static void Session_DeclareMultiplayerMenuGameCVars( void ) {
	struct menuGameCVar_t {
		const char *name;
		const char *value;
	};
	static const menuGameCVar_t menuGameCVars[] = {
		{ "bot_minPlayers",	"0" },
		{ "bot_skill",		"3" }
	};

	for ( int i = 0; i < (int)( sizeof( menuGameCVars ) / sizeof( menuGameCVars[0] ) ); i++ ) {
		if ( cvarSystem->Find( menuGameCVars[i].name ) != NULL ) {
			continue;
		}
		cvarSystem->SetCVarString( menuGameCVars[i].name, menuGameCVars[i].value,
			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE );
	}
}
#endif

/*
===============
idSessionLocal::Init

Called in an orderly fashion at system startup,
so commands, cvars, files, etc are all available
===============
*/
void idSessionLocal::Init() {

	common->Printf( "-------- Initializing Session --------\n" );

	cmdSystem->AddCommand( "writePrecache", Sess_WritePrecache_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "writes precache commands" );

#ifndef	ID_DEDICATED
	cmdSystem->AddCommand( "openq4_startSingleplayer", Session_openQ4StartSingleplayer_f, CMD_FL_SYSTEM, "internal helper to start singleplayer after game-module switches" );
	cmdSystem->AddCommand( "openq4_assertMapState", Session_openQ4AssertMapState_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "asserts the active map and entity filter for validation harnesses" );
	cmdSystem->AddCommand( "openq4_assertMPGameplayView", Session_openQ4AssertMPGameplayView_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "asserts that multiplayer rendering is not covered by an active session GUI" );
	cmdSystem->AddCommand( "openq4_assertMenuActivation", Session_openQ4AssertMenuActivation_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "opens the in-game menu and asserts a bounded activation time for validation harnesses" );
	cmdSystem->AddCommand( "openq4_resumeBakeLightGrids", Session_openQ4ResumeBakeLightGrids_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "internal helper to continue light-grid baking after game-module switches" );
	cmdSystem->AddCommand( "iamtheduke", Session_IAmTheDuke_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "toggles the SP-only iamtheduke cheat text overlay" );
	cmdSystem->AddCommand( "bakeLightGrids", Session_BakeLightGrids_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "bakes openQ4-compatible lightgrid metadata and irradiance atlases for the current map or a batch of maps" );
	cmdSystem->AddCommand( "map", Session_Map_f, CMD_FL_SYSTEM, "loads a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "devmap", Session_DevMap_f, CMD_FL_SYSTEM, "loads a map in developer mode", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "testmap", Session_TestMap_f, CMD_FL_SYSTEM, "tests a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "framePacingSnapshot", Session_FramePacingSnapshot_f, CMD_FL_SYSTEM, "prints the current frame-pacing diagnostics summary" );
	cmdSystem->AddCommand( "framePacingReset", Session_FramePacingReset_f, CMD_FL_SYSTEM, "resets frame-pacing diagnostics before a measured window" );
	cmdSystem->AddCommand( "testWaitBox", Session_TestWaitBox_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "opens a timed wait box and prints frame-pacing stats: testWaitBox <msec> [network 0/1] [reason]" );
	cmdSystem->AddCommand( "testMessageBox", Session_TestMessageBox_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "opens a timed message box and prints frame-pacing stats: testMessageBox <msec> [network 0/1] [reason]" );

	cmdSystem->AddCommand( "writeCmdDemo", Session_WriteCmdDemo_f, CMD_FL_SYSTEM, "writes a command demo" );
	cmdSystem->AddCommand( "playCmdDemo", Session_PlayCmdDemo_f, CMD_FL_SYSTEM, "plays back a command demo" );
	cmdSystem->AddCommand( "timeCmdDemo", Session_TimeCmdDemo_f, CMD_FL_SYSTEM, "times a command demo" );
	cmdSystem->AddCommand( "exitCmdDemo", Session_ExitCmdDemo_f, CMD_FL_SYSTEM, "exits a command demo" );
	cmdSystem->AddCommand( "aviCmdDemo", Session_AVICmdDemo_f, CMD_FL_SYSTEM, "writes AVIs for a command demo" );
	cmdSystem->AddCommand( "aviGame", Session_AVIGame_f, CMD_FL_SYSTEM, "writes AVIs for the current game" );

	cmdSystem->AddCommand( "recordDemo", Session_RecordDemo_f, CMD_FL_SYSTEM, "records a demo" );
	cmdSystem->AddCommand( "stopRecording", Session_StopRecordingDemo_f, CMD_FL_SYSTEM, "stops demo recording" );
	cmdSystem->AddCommand( "playDemo", Session_PlayDemo_f, CMD_FL_SYSTEM, "plays back a demo", idCmdSystem::ArgCompletion_DemoName );
	cmdSystem->AddCommand( "timeDemo", Session_TimeDemo_f, CMD_FL_SYSTEM, "times a demo", idCmdSystem::ArgCompletion_DemoName );
	cmdSystem->AddCommand( "timeDemoQuit", Session_TimeDemoQuit_f, CMD_FL_SYSTEM, "times a demo and quits", idCmdSystem::ArgCompletion_DemoName );
	cmdSystem->AddCommand( "aviDemo", Session_AVIDemo_f, CMD_FL_SYSTEM, "writes AVIs for a demo", idCmdSystem::ArgCompletion_DemoName );
	cmdSystem->AddCommand( "compressDemo", Session_CompressDemo_f, CMD_FL_SYSTEM, "compresses a demo file", idCmdSystem::ArgCompletion_DemoName );
#endif

	cmdSystem->AddCommand( "disconnect", Session_Disconnect_f, CMD_FL_SYSTEM, "disconnects from a game" );

#ifdef ID_DEMO_BUILD
	cmdSystem->AddCommand( "endOfDemo", Session_EndOfDemo_f, CMD_FL_SYSTEM, "ends the demo version of the game" );
#endif

	cmdSystem->AddCommand( "demoShot", Session_DemoShot_f, CMD_FL_SYSTEM, "writes a screenshot for a demo" );
	cmdSystem->AddCommand( "testGUI", Session_TestGUI_f, CMD_FL_SYSTEM, "tests a gui" );

#ifndef	ID_DEDICATED
	cmdSystem->AddCommand( "GuiEvent", Session_GuiEvent_f, CMD_FL_SYSTEM, "sends a named event to the active gui" );
	cmdSystem->AddCommand( "openq4_guiSet", Session_OpenQ4GuiSet_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "sets an existing active GUI variable for engine-scripted layout validation" );
	cmdSystem->AddCommand( "openq4_guiGet", Session_OpenQ4GuiGet_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "reports an existing active GUI variable for engine-scripted layout validation" );
	cmdSystem->AddCommand( "openq4_browser", Session_OpenQ4Browser_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "reports and exercises main-menu server-browser operations for engine-scripted validation" );
	cmdSystem->AddCommand( "saveGame", SaveGame_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "saves a game" );
	cmdSystem->AddCommand( "loadGame", LoadGame_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "loads a game", idCmdSystem::ArgCompletion_SaveGame );
#endif

	cmdSystem->AddCommand( "takeViewNotes", TakeViewNotes_f, CMD_FL_SYSTEM, "take notes about the current map from the current view" );
	cmdSystem->AddCommand( "takeViewNotes2", TakeViewNotes2_f, CMD_FL_SYSTEM, "extended take view notes" );

	cmdSystem->AddCommand( "rescanSI", Session_RescanSI_f, CMD_FL_SYSTEM, "internal - rescan serverinfo cvars and tell game" );

	cmdSystem->AddCommand( "hitch", Session_Hitch_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "hitches the game" );

	// the same idRenderWorld will be used for all games
	// and demos, insuring that level specific models
	// will be freed
	rw = renderSystem->AllocRenderWorld();
	sw = soundSystem->AllocSoundWorld( rw );

	menuSoundWorld = soundSystem->AllocSoundWorld( rw );

	// we have a single instance of the main menu
#ifdef ID_DEDICATED
	common->Printf( "Dedicated server: skipping client GUI preload.\n" );
#else
	Session_DeclareMultiplayerMenuGameCVars();
#ifndef ID_DEMO_BUILD
	guiMainMenu = uiManager->FindGui( "guis/mainmenu.gui", true, false, true );
#else
	guiMainMenu = uiManager->FindGui( "guis/demo_mainmenu.gui", true, false, true );
#endif
	// Resolve and retain menu media while the session is initialized. Doing
	// this synchronously from StartMenu made the first ESC press wait on image,
	// material, and sound lookup before the GUI could be activated.
	PrimeMainMenuGuiResources();
	guiMainMenu_MapList = uiManager->AllocListGUI();
	guiMainMenu_MapList->Config( guiMainMenu, "mapList" );
	idAsyncNetwork::client.serverList.GUIConfig( guiMainMenu, "serverList" );
	guiRestartMenu = uiManager->FindGui( "guis/restart.gui", true, false, true );
	guiGameOver = uiManager->FindGui( "guis/gameover.gui", true, false, true );
	guiMsg = uiManager->FindGui( "guis/msg.gui", true, false, true );
	guiTakeNotes = uiManager->FindGui( "guis/takeNotes.gui", true, false, true );
	guiIntro = uiManager->FindGui( "guis/intro.gui", true, false, true );
	InitDemoSystem();
	arenaCampaign.Init();
#endif

	whiteMaterial = declManager->FindMaterial( "_white" );

	guiInGame = NULL;
	guiTest = NULL;

	guiActive = NULL;
	guiHandle = NULL;

	ReadCDKey();

	common->Printf( "session initialized\n" );
	common->Printf( "--------------------------------------\n" );
}

/*
===============
idSessionLocal::GetLocalClientNum
===============
*/
int idSessionLocal::GetLocalClientNum() {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetLocalClientNum();
	} else if ( idAsyncNetwork::server.IsActive() ) {
		if ( idAsyncNetwork::serverDedicated.GetInteger() == 0 ) {
			return 0;
		} else if ( idAsyncNetwork::server.IsClientInGame( idAsyncNetwork::serverDrawClient.GetInteger() ) ) {
			return idAsyncNetwork::serverDrawClient.GetInteger();
		} else {
			return -1;
		}
	} else {
		return 0;
	}
}

/*
===============
idSessionLocal::SetPlayingSoundWorld
===============
*/
void idSessionLocal::SetPlayingSoundWorld( idSoundWorld *soundWorld ) {
	// Record what the session actually wants playing and let
	// UpdateSoundWorldFocus() decide what reaches the sound system. Folding the
	// focus test straight into the stored world used to latch: a call made while
	// the window happened to be unfocused - most importantly the one that ends
	// ExecuteMapChange() - handed the sound system NULL, and nothing re-derived
	// it afterwards, so the level played out in silence.
	requestedSoundWorld = soundWorld;
	UpdateSoundWorldFocus();
}

/*
===============
idSessionLocal::UpdateSoundWorldFocus

Applies the unfocused-silence preference on top of the world the session asked
for. Called every frame so audio always comes back with the window, even when no
focus event is delivered (the window was never focused to begin with, the video
reference was being re-acquired when the world was selected, and so on).
===============
*/
void idSessionLocal::UpdateSoundWorldFocus() {
	if ( soundSystem == NULL ) {
		return;
	}

	idSoundWorld *targetSoundWorld = requestedSoundWorld;

	if ( targetSoundWorld != NULL && Session_ShouldSilenceAudioWhenUnfocused() ) {
		targetSoundWorld = NULL;
	}

	soundSystem->SetPlayingSoundWorld( targetSoundWorld );
}

void idSessionLocal::SetPlayingSoundWorld() {
	if ( guiActive && ( guiActive == guiMainMenu || guiActive == guiIntro || guiActive == guiLoading || ( guiActive == guiMsg && !mapSpawned ) ) ) {
		SetPlayingSoundWorld( menuSoundWorld );
	} else {
		SetPlayingSoundWorld( sw );
	}
}

/*
===============
idSessionLocal::TimeHitch

this is used by the sound system when an OnDemand sound is loaded, so the game action
doesn't advance and get things out of sync
===============
*/
void idSessionLocal::TimeHitch( int msec ) {
	timeHitch += msec;
}

/*
=================
idSessionLocal::ReadCDKey
=================
*/
void idSessionLocal::ReadCDKey( void ) {
	memset( cdkey, 0, sizeof( cdkey ) );
	memset( xpkey, 0, sizeof( xpkey ) );
	cdkey_state = CDKEY_NA;
	xpkey_state = CDKEY_NA;
	authEmitTimeout = 0;
	authWaitBox = false;
	authMsg.Clear();
}

/*
================
idSessionLocal::WriteCDKey
================
*/
void idSessionLocal::WriteCDKey( void ) {
	
}

/*
===============
idSessionLocal::ClearKey
===============
*/
void idSessionLocal::ClearCDKey( bool valid[ 2 ] ) {
	(void)valid;
}

/*
================
idSessionLocal::GetCDKey
================
*/
const char *idSessionLocal::GetCDKey( bool xp ) {
	(void)xp;
	return "";
}

/*
===============
idSessionLocal::EmitGameAuth
Legacy no-op (CD key auth removed).
===============
*/
void idSessionLocal::EmitGameAuth( void ) {
}

/*
================
idSessionLocal::CheckKey
Legacy compatibility shim: always accepts and reports valid.
================
*/
bool idSessionLocal::CheckKey( const char *key, bool netConnect, bool offline_valid[ 2 ] ) {
	(void)key;
	(void)netConnect;
	offline_valid[ 0 ] = true;
	offline_valid[ 1 ] = true;
	return true;
}

/*
===============
idSessionLocal::CDKeysAreValid
Legacy compatibility shim: always true.
===============
*/
bool idSessionLocal::CDKeysAreValid( bool strict ) {
	(void)strict;
	return true;
}

/*
===============
idSessionLocal::WaitingForGameAuth
===============
*/
bool idSessionLocal::WaitingForGameAuth( void ) {
	return false;
}

/*
===============
idSessionLocal::CDKeysAuthReply
===============
*/
void idSessionLocal::CDKeysAuthReply( bool valid, const char *auth_msg ) {
	(void)valid;
	(void)auth_msg;
}

/*
===============
idSessionLocal::GetCurrentMapName
===============
*/
const char *idSessionLocal::GetCurrentMapName() {
	return currentMapName.c_str();
}

/*
===============
idSessionLocal::GetSaveGameVersion
===============
*/
int idSessionLocal::GetSaveGameVersion( void ) {
	return savegameVersion;
}

/*
===============
idSessionLocal::GetAuthMsg
===============
*/
const char *idSessionLocal::GetAuthMsg( void ) {
	return authMsg.c_str();
}
