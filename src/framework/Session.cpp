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
#define private public
#define protected public
#include "Game_local.h"
#undef protected
#undef private
#include "../renderer/tr_local.h"

void *R_StaticAlloc( int bytes );
void R_StaticFree( void *data );

extern glconfig_t	glConfig;

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
idCVar	com_loadingContinueAutoAdvance( "com_loadingContinueAutoAdvance", "0", CVAR_SYSTEM | CVAR_INTEGER, "auto-accept the single-player loading-screen continue gate after N msec (testing), 0 = off", 0, 60000, idCmdSystem::ArgCompletion_Integer<0,60000> );
idCVar	com_skipLoadingContinue( "com_skipLoadingContinue", "0", CVAR_SYSTEM | CVAR_BOOL, "skip the single-player loading-screen continue gate (testing)" );
idCVar	com_skipLogoVideos( "com_skipLogoVideos", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "skip startup logo videos and go straight to the main menu" );
idCVar	com_showLevelshotBounds( "com_showLevelshotBounds", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "draw a centered 4:3 frame guide for levelshot composition" );
idCVar	s_muteUnfocused( "s_muteUnfocused", "1", CVAR_ARCHIVE | CVAR_BOOL, "mute all audio when the application is out of focus" );

#if defined( _WIN32 )
bool Sys_IsGameWindowFocused( void );
#endif

idSessionLocal		sessLocal;
idSession			*session = &sessLocal;

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

	return Max( 0, cvarSystem->GetCVarInteger( "com_maxfps" ) );
}

static float Session_GetBlockingLoadFrameIntervalMsec( void ) {
	const int presentationCap = Session_FindPresentationCap();
	if ( presentationCap > 0 ) {
		return 1000.0f / static_cast<float>( presentationCap );
	}

	// Uncapped blocking load hooks can fire extremely often during asset I/O.
	// Keep the legacy one-tic pacing as the conservative fallback in that mode.
	return common->GetUserCmdMsecFloat();
}

static void Session_BeginBlockingLoadPresentationFrame( void ) {
	if ( Session_FindPresentationCap() > 0 ) {
		OpenQ4_BeginPresentationFrame();
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

static void Session_PrintFramePacingSummary( const openq4FramePacingStats_t &stats, const char *reason ) {
	common->Printf(
		"Frame pacing%s (%s): bound=%s, samples=%d, present=%.2f ms (%.1f Hz), async=%.2f ms (%.1f Hz), ticDelta/frame=%.2f, gameTics/frame=%.2f, wait overshoot=%.2f ms, wake jitter=%.2f ms\n",
		reason != NULL ? reason : "",
		stats.multiplayer ? "MP" : "SP",
		Session_GetFramePacingBoundName( stats.boundMode ),
		stats.frameSampleCount,
		stats.avgFrameMsec,
		stats.avgFrameHz,
		stats.asyncStats.avgDeltaMsec,
		stats.asyncStats.avgHz,
		stats.avgTicsPerFrame,
		stats.avgGameTicsPerFrame,
		stats.avgWaitOvershootMsec,
		stats.avgWakeJitterMsec );
}

static void Session_FramePacingSnapshot_f( const idCmdArgs &args ) {
	const char *reason = NULL;
	idStr reasonText;
	if ( args.Argc() > 1 ) {
		reasonText = args.Args( 1, -1 );
		reason = reasonText.c_str();
	}

	sessLocal.PrintFramePacingSnapshot( reason );
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

static bool Session_IsRetailSaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0;
}

static bool Session_IsLegacyOpenQ4SaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_LEGACY_OPENQ4 ) == 0;
}

static bool Session_IsSupportedSaveGameName( const idStr &gameName ) {
	return Session_IsRetailSaveGameName( gameName ) || Session_IsLegacyOpenQ4SaveGameName( gameName );
}

static bool Session_SaveGameHeaderUsesEntityFilter( const idStr &gameName ) {
	return Session_IsRetailSaveGameName( gameName );
}

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

void OpenQ4_PrintFramePacingSnapshot( const char *reason ) {
	sessLocal.PrintFramePacingSnapshot( reason );
}

void OpenQ4_RecordMultiplayerFramePacing( int frameStartMsec ) {
	sessLocal.SampleMultiplayerFramePacing( frameStartMsec );
}

void idSessionLocal::UpdateFramePacingStats( int frameStartMsec, int requestedWaitMsec, int actualWaitMsec, int gameTicsToRun ) {
	openq4AsyncTimingStats_t asyncStats;
	OpenQ4_GetAsyncTimingStats( asyncStats, 120 );

	const int sampleCount = Max( framePacingStats.frameSampleCount, 1 );

	if ( framePacingLastFrameMsec > 0 ) {
		const int frameDeltaMsec = Max( 0, frameStartMsec - framePacingLastFrameMsec );
		framePacingStats.lastFrameMsec = frameDeltaMsec;
		++framePacingStats.frameSampleCount;
		framePacingStats.avgFrameMsec = Session_UpdateMetricAverage( framePacingStats.avgFrameMsec, static_cast<float>( frameDeltaMsec ), framePacingStats.frameSampleCount );
		framePacingStats.avgFrameHz = framePacingStats.avgFrameMsec > 0.0f ? 1000.0f / framePacingStats.avgFrameMsec : 0.0f;
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

static bool Session_IsCompatibleSaveGameVersion( const int version ) {
	return version == SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION_ALT;
}

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
	normalizedPath.StripFileExtension();

	if ( !idStr::Icmpn( normalizedPath.c_str(), "maps/", 5 ) ) {
		normalizedPath = normalizedPath.c_str() + 5;
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

static void Session_ApplyEntityFilterToServerInfo( idDict &serverInfo, const char *entityFilter ) {
	if ( entityFilter != NULL && entityFilter[0] != '\0' ) {
		serverInfo.Set( "si_entityFilter", entityFilter );
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
	Session_NormalizeMapDeclPath( mapPath, normalizedPath );
	if ( normalizedPath.IsEmpty() ) {
		return false;
	}

	if ( entityFilter != NULL && entityFilter[0] != '\0' ) {
		idStr filteredPath = normalizedPath;
		filteredPath += "_";
		filteredPath += entityFilter;
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

static bool OpenQ4_IsSingleplayerGameType( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	return !( gameType && gameType[ 0 ] && idStr::Icmp( gameType, "singleplayer" ) != 0 );
}

static idEntity *OpenQ4_FindSpawnedEntityByBaseClass( const char *className ) {
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

static void Session_IAmTheDuke_f( const idCmdArgs &args ) {
	(void)args;
	sessLocal.ToggleIAmTheDuke();
}

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

	float viewportWidth = static_cast<float>( glConfig.uiViewportWidth );
	float viewportHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		viewportWidth = static_cast<float>( glConfig.vidWidth );
		viewportHeight = static_cast<float>( glConfig.vidHeight );
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
	R_WriteTGA( generatedPath.c_str(), composite.Ptr(), outputWidth, outputHeight );

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

	return true;
}

static bool Session_EndsWithIgnoreCase( const idStr &value, const char *suffix ) {
	if ( suffix == NULL || suffix[ 0 ] == '\0' ) {
		return false;
	}

	const int valueLength = value.Length();
	const int suffixLength = idStr::Length( suffix );
	if ( valueLength < suffixLength ) {
		return false;
	}

	return idStr::Icmp( value.c_str() + valueLength - suffixLength, suffix ) == 0;
}

static bool Session_IsMainMenuMontageLevelshotExcluded( const idStr &baseName ) {
	return baseName.Icmp( "generic" ) == 0 ||
		baseName.Icmp( "e3_load" ) == 0 ||
		Session_EndsWithIgnoreCase( baseName, "_left" ) ||
		Session_EndsWithIgnoreCase( baseName, "_right" ) ||
		Session_EndsWithIgnoreCase( baseName, "_top" ) ||
		Session_EndsWithIgnoreCase( baseName, "_bottom" );
}

static bool Session_StringListContainsNoCase( const idStrList &list, const idStr &value ) {
	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[ i ].Icmp( value ) == 0 ) {
			return true;
		}
	}

	return false;
}

static void Session_AppendMainMenuMontageCandidatesForExtension( const char *extension, idStrList &candidates ) {
	idFileList *fileList = fileSystem->ListFiles( "gfx/guis/loadscreens", extension, true, true );
	if ( fileList == NULL ) {
		return;
	}

	for ( int i = 0; i < fileList->GetNumFiles(); i++ ) {
		idStr levelshotPath = fileList->GetFile( i );
		levelshotPath.BackSlashesToSlashes();
		levelshotPath.StripFileExtension();

		idStr baseName = levelshotPath;
		baseName.StripPath();
		if ( baseName.IsEmpty() || Session_IsMainMenuMontageLevelshotExcluded( baseName ) ) {
			continue;
		}
		if ( Session_StringListContainsNoCase( candidates, levelshotPath ) ) {
			continue;
		}

		idStr resolvedCenterPath;
		if ( !Session_ResolveImageFilePath( levelshotPath, resolvedCenterPath ) ) {
			continue;
		}

		idStr resolvedLeftPath;
		idStr resolvedRightPath;
		idStr resolvedTopPath;
		idStr resolvedBottomPath;
		if ( !Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_left", resolvedLeftPath ) ||
			!Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_right", resolvedRightPath ) ||
			!Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_top", resolvedTopPath ) ||
			!Session_ResolveSiblingImageFilePath( resolvedCenterPath, "_bottom", resolvedBottomPath ) ) {
			continue;
		}

		candidates.Append( levelshotPath );
	}

	fileSystem->FreeFileList( fileList );
}

static void Session_BuildMainMenuMontageCandidates( idStrList &candidates ) {
	static const char *supportedExtensions[] = { ".tga", ".dds", ".jpg", NULL };

	candidates.Clear();
	for ( int i = 0; supportedExtensions[ i ] != NULL; i++ ) {
		Session_AppendMainMenuMontageCandidatesForExtension( supportedExtensions[ i ], candidates );
	}
}

static bool Session_ResolveMainMenuMontageTileSet( const idStr &backgroundPath, idStr &centerPath, idStr &leftPath, idStr &rightPath, idStr &topPath, idStr &bottomPath ) {
	if ( !Session_ResolveImageFilePath( backgroundPath, centerPath ) ) {
		return false;
	}

	if ( !Session_ResolveSiblingImageFilePath( centerPath, "_left", leftPath ) ||
		!Session_ResolveSiblingImageFilePath( centerPath, "_right", rightPath ) ||
		!Session_ResolveSiblingImageFilePath( centerPath, "_top", topPath ) ||
		!Session_ResolveSiblingImageFilePath( centerPath, "_bottom", bottomPath ) ) {
		return false;
	}

	return true;
}

void idSessionLocal::SetMainMenuBackgroundMontageGuiVars( void ) {
	static const int maxMontageShots = 5;
	static const char *fallbackBackground = "gfx/guis/loadscreens/generic";

	if ( guiMainMenu == NULL ) {
		return;
	}

	idStrList candidates;
	Session_BuildMainMenuMontageCandidates( candidates );

	idStrList selected;
	selected = candidates;

	if ( selected.Num() > 1 ) {
		idRandom random( Sys_Milliseconds() ^ com_frameTime ^ selected.Num() );
		for ( int i = selected.Num() - 1; i > 0; --i ) {
			const int swapIndex = random.RandomInt( i + 1 );
			if ( swapIndex == i ) {
				continue;
			}

			idStr temp = selected[ i ];
			selected[ i ] = selected[ swapIndex ];
			selected[ swapIndex ] = temp;
		}
	}

	const int uniqueShotCount = Min( maxMontageShots, selected.Num() );
	for ( int i = 0; i < maxMontageShots; i++ ) {
		idStr backgroundPath = fallbackBackground;
		if ( uniqueShotCount > 0 ) {
			backgroundPath = selected[ i % uniqueShotCount ];
		}

		idStr centerPath;
		idStr leftPath;
		idStr rightPath;
		idStr topPath;
		idStr bottomPath;
		if ( !Session_ResolveMainMenuMontageTileSet( backgroundPath, centerPath, leftPath, rightPath, topPath, bottomPath ) ) {
			if ( !Session_ResolveMainMenuMontageTileSet( fallbackBackground, centerPath, leftPath, rightPath, topPath, bottomPath ) ) {
				continue;
			}
		}

		guiMainMenu->SetStateString( va( "menu_bg_%d", i + 1 ), centerPath.c_str() );
		guiMainMenu->SetStateString( va( "menu_bg_%d_left", i + 1 ), leftPath.c_str() );
		guiMainMenu->SetStateString( va( "menu_bg_%d_right", i + 1 ), rightPath.c_str() );
		guiMainMenu->SetStateString( va( "menu_bg_%d_top", i + 1 ), topPath.c_str() );
		guiMainMenu->SetStateString( va( "menu_bg_%d_bottom", i + 1 ), bottomPath.c_str() );
		guiMainMenu->SetStateInt( va( "menu_bg_%d_canvasfill", i + 1 ), 0 );
		declManager->FindMaterial( centerPath.c_str() )->SetSort( SS_GUI );
		declManager->FindMaterial( leftPath.c_str() )->SetSort( SS_GUI );
		declManager->FindMaterial( rightPath.c_str() )->SetSort( SS_GUI );
		declManager->FindMaterial( topPath.c_str() )->SetSort( SS_GUI );
		declManager->FindMaterial( bottomPath.c_str() )->SetSort( SS_GUI );
	}

	guiMainMenu->SetStateInt( "menu_bg_count", Max( uniqueShotCount, 1 ) );
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

	float viewportWidth = static_cast<float>( glConfig.uiViewportWidth );
	float viewportHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		viewportWidth = static_cast<float>( glConfig.vidWidth );
		viewportHeight = static_cast<float>( glConfig.vidHeight );
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
	if ( key == K_ESCAPE || key == K_ENTER || key == K_KP_ENTER || key == K_SPACE ) {
		return true;
	}
	if ( key >= K_MOUSE1 && key <= K_MOUSE8 ) {
		return true;
	}
	if ( key >= K_JOY1 && key <= K_JOY32 ) {
		return true;
	}
	if ( key >= K_AUX1 && key <= K_AUX16 ) {
		return true;
	}
	return false;
}

static bool Session_ShouldSilenceAudioWhenUnfocused() {
#if defined( _WIN32 )
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

static void Session_BuildLightGridOutputPaths( const idStr &mapName, idStr &lightGridPath, idStr &atlasDir ) {
	lightGridPath = "maps/";
	lightGridPath += mapName;
	lightGridPath.SetFileExtension( ".lightgrid" );

	atlasDir = "env/maps/";
	atlasDir += mapName;
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
	Session_BuildLightGridOutputPaths( mapName, lightGridPath, atlasDir );

	fileSystem->RemoveFile( lightGridPath.c_str() );
	fileSystem->RemoveFile( va( "%s.prev", lightGridPath.c_str() ) );
	fileSystem->RemoveFile( va( "%s.baking", lightGridPath.c_str() ) );
	Session_RemoveLightGridAtlasArtifacts( atlasDir, ".tga" );
	Session_RemoveLightGridAtlasArtifacts( atlasDir, ".prev" );
}

static bool Session_CurrentLightGridOutputsComplete( const lightGridBakeOptions_t &options, idStr &mapName, int &requiredAtlasCount ) {
	requiredAtlasCount = 0;

	if ( tr.primaryWorld == NULL ) {
		return false;
	}

	idRenderWorldLocal *world = tr.primaryWorld;
	mapName = world->mapName;
	Session_NormalizeLightGridMapName( mapName );
	if ( mapName.Length() <= 0 ) {
		return false;
	}

	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		world->portalAreas[ areaIndex ].lightGrid.SetupGrid(
			world->portalAreas[ areaIndex ].globalBounds,
			world,
			options.gridSize,
			areaIndex,
			world->numPortalAreas,
			options.maxProbes,
			true );
		if ( world->portalAreas[ areaIndex ].lightGrid.CountValidGridPoints() > 0 ) {
			requiredAtlasCount++;
		}
	}

	idStr lightGridPath;
	idStr atlasDir;
	Session_BuildLightGridOutputPaths( mapName, lightGridPath, atlasDir );
	if ( fileSystem->FindFile( lightGridPath.c_str(), true ) == FIND_NO ) {
		return false;
	}
	if ( !R_LightGridFileMatchesBakeOptions( lightGridPath.c_str(), options, world ) ) {
		return false;
	}

	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		const LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
		if ( lightGrid.CountValidGridPoints() <= 0 ) {
			continue;
		}

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
	common->Printf( "Without 'force', maps whose required .lightgrid metadata and area*_lightgrid_amb.tga atlases already exist are skipped.\n" );
	common->Printf( "When map names, 'all', or 'all-mp' are given, OpenQ4 loads each map automatically, prints live progress to the console/log, and writes .lightgrid metadata plus area*_lightgrid_amb.tga atlases to fs_savepath.\n" );
	common->Printf( "'separateAreas' rebuilds one portal-area probe layout at a time and streams .lightgrid metadata during the bake to reduce peak CPU memory usage.\n" );
	common->Printf( "Multiplayer targets are cheat-protected; enable cheats first with 'sv_cheats 1' or 'net_allowCheats 1'.\n" );
	common->Printf( "This bake is diffuse-only and LDR. It does not output the BFG EXR/PBR light-grid data path.\n" );
}

static bool Session_ParseLightGridBakeArgs( const idCmdArgs &args, lightGridBakeOptions_t &options,
	idList<idStr> &mapTargets, bool &bakeAll, bool &bakeAllMultiplayer, bool &forceBake, bool &autoQuit, int &resumeIndex, bool &helpRequested, bool &requestedTargets ) {
	R_SetDefaultLightGridBakeOptions( options );
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
	cvarSystem->SetCVarString( "si_gameType", useMultiplayerModule ? "dm" : "singleplayer" );
	cvarSystem->SetCVarString( "com_nextGameModule", useMultiplayerModule ? "game_mp" : "game_sp" );

	idCmdArgs reloadArgs;
	Session_BuildLightGridBakeResumeArgs( options, mapTargets, bakeAll, bakeAllMultiplayer, forceBake, autoQuit, resumeIndex, reloadArgs );

	common->Printf(
		"bakeLightGrids: reloading engine into %s to continue at map %i of %i\n",
		useMultiplayerModule ? "game_mp" : "game_sp",
		resumeIndex + 1,
		mapTargets.Num() );
	cmdSystem->SetupReloadEngine( reloadArgs );
}

static bool Session_LoadLightGridBakeMap( const idStr &mapName ) {
	sessLocal.Stop();

	if ( Session_IsLightGridBakeMultiplayerMap( mapName ) ) {
		if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ) {
			common->Printf( "bakeLightGrids: forcing net_serverDedicated 0 so multiplayer maps can render during baking\n" );
			cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );
		}

		cvarSystem->SetCVarString( "si_gameType", "dm" );
		cvarSystem->SetCVarString( "si_map", mapName.c_str() );

		idCmdArgs spawnArgs;
		spawnArgs.AppendArg( "spawnServer" );
		spawnArgs.AppendArg( mapName.c_str() );
		cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, spawnArgs );
	} else {
		sessLocal.StartNewGame( mapName.c_str(), true );
	}

	if ( !sessLocal.mapSpawned ) {
		common->Printf( "bakeLightGrids: failed to load map '%s'\n", mapName.c_str() );
		return false;
	}

	sessLocal.UpdateScreen();
	return true;
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

	if ( sessLocal.mapSpawned && ( !tr.primaryWorld || !tr.primaryView ) ) {
		sessLocal.UpdateScreen();
	}

	idStr mapName;
	int requiredAtlasCount = 0;
	const bool outputsComplete = Session_CurrentLightGridOutputsComplete( options, mapName, requiredAtlasCount );
	if ( outputsComplete ) {
		if ( forceBake ) {
			common->Printf( "bakeLightGrids: force requested; cleaning existing outputs for %s\n", mapName.c_str() );
			Session_RemoveLightGridBakeOutputsForMap( mapName );
		} else {
			common->Printf( "bakeLightGrids: skipping %s because %i required atlas file(s) and the .lightgrid metadata already exist\n", mapName.c_str(), requiredAtlasCount );
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
	return R_BakeCurrentLightGrids( options, ( jobName != NULL && jobName[ 0 ] != '\0' ) ? jobName : NULL );
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

static void Session_OpenQ4ResumeBakeLightGrids_f( const idCmdArgs &args ) {
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
	findFile_t	ff;
	idCmdArgs	rl_args;

	map = args.Argv(1);
	if ( !map.Length() ) {
		return;
	}
	map.StripFileExtension();

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	// handle addon packs through reloadEngine
	sprintf( string, "maps/%s.map", map.c_str() );
	ff = fileSystem->FindFile( string, true );
	switch ( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", string.c_str() );
		return;
	case FIND_ADDON:
		common->Printf( "map %s is in an addon pak - reloading\n", string.c_str() );
		rl_args.AppendArg( "map" );
		rl_args.AppendArg( map );
		if ( args.Argc() > 2 ) {
			rl_args.AppendArg( args.Argv( 2 ) );
		}
		cmdSystem->SetupReloadEngine( rl_args );
		return;
	default:
		break;
	}

	const bool developerMapStart = cvarSystem->GetCVarBool( "developer" );
	sessLocal.StartNewGame( map, developerMapStart, Session_GetEntityFilterArg( args ) );
}

/*
==================
Session_DevMap_f

Restart the server on a different map in developer mode
==================
*/
static void Session_DevMap_f( const idCmdArgs &args ) {
	idStr map, string;
	findFile_t	ff;
	idCmdArgs	rl_args;	

	map = args.Argv(1);
	if ( !map.Length() ) {
		return;
	}
	map.StripFileExtension();

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	// handle addon packs through reloadEngine
	sprintf( string, "maps/%s.map", map.c_str() );
	ff = fileSystem->FindFile( string, true );
	switch ( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", string.c_str() );
		return;
	case FIND_ADDON:
		common->Printf( "map %s is in an addon pak - reloading\n", string.c_str() );
		rl_args.AppendArg( "devmap" );
		rl_args.AppendArg( map );
		if ( args.Argc() > 2 ) {
			rl_args.AppendArg( args.Argv( 2 ) );
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
	sessLocal.StartNewGame( map, true, Session_GetEntityFilterArg( args ) );
}

/*
==================
Session_TestMap_f
==================
*/
static void Session_TestMap_f( const idCmdArgs &args ) {
	idStr map, string;

	map = args.Argv(1);
	if ( !map.Length() ) {
		return;
	}
	map.StripFileExtension();

	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );

	sprintf( string, "dmap maps/%s.map", map.c_str() );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, string );

	sprintf( string, "devmap %s", map.c_str() );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, string );
}

/*
==================
Session_OpenQ4StartSingleplayer_f
==================
*/
static void Session_OpenQ4StartSingleplayer_f( const idCmdArgs &args ) {
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
	insideExecuteMapChange = false;

	loadingSaveGame = false;
	savegameFile = NULL;
	savegameVersion = 0;

	currentMapName.Clear();
	aviDemoShortName.Clear();
	msgFireBack[ 0 ].Clear();
	msgFireBack[ 1 ].Clear();

	timeHitch = 0;
	ResetFramePacingStats();

	rw = NULL;
	sw = NULL;
	menuSoundWorld = NULL;
	readDemo = NULL;
	writeDemo = NULL;
	renderdemoVersion = 0;
	cmdDemoFile = NULL;

	syncNextGameFrame = false;
	syncNextGameFrameAwaitingAsyncTicLog = false;
	cinematicStateValid = false;
	cinematicActive = false;
	mapSpawned = false;
	guiActive = NULL;
	aviCaptureMode = false;
	timeDemo = TD_NO;
	waitingOnBind = false;
	lastPacifierTime = 0;
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
		= guiTest = guiMsg = guiMsgRestore = guiTakeNotes = NULL;	
	
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
	ClearWipe();

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
	if ( idAsyncNetwork::IsActive() || !OpenQ4_IsSingleplayerGameType() ) {
		common->Printf( "iamtheduke is single-player only.\n" );
		return;
	}

	if ( !mapSpawned || !gameEdit || !gameEdit->PlayerIsValid() ) {
		common->Printf( "You must be in a single-player game to use this command.\n" );
		return;
	}

	idPlayer *player = static_cast<idPlayer *>( OpenQ4_FindSpawnedEntityByBaseClass( "idPlayer" ) );
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
	if ( com_ticNumber == 0 ) {
		return;
	}
	console->Close();

	// introduced in D3XP code. don't think it actually fixes anything, but doesn't hurt either
#if 1
	// Try and prevent the while loop from being skipped over (long hitch on the main thread?)
	int stop = Sys_Milliseconds() + 1000;
	int force = 10;
	while ( Sys_Milliseconds() < stop || force-- > 0 ) {
		OpenQ4_BeginPresentationFrame();
		com_frameTime = common->GetUserCmdTime( com_ticNumber );
		session->Frame();
		session->UpdateScreen( false );
	}
#else
	int stop = com_ticNumber + common->GetUserCmdTicsForMsecCeil( 1000 );
	while ( com_ticNumber < stop ) {
		OpenQ4_BeginPresentationFrame();
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
		sprintf( filename, format, i );
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

/*
================
Session_Disconnect_f
================
*/
static void Session_Disconnect_f( const idCmdArgs &args ) {
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

	if ( timeDemo ) {
		// report the stats
		float	demoSeconds = ( timeDemoStopTime - timeDemoStartTime ) * 0.001f;
		float	demoFPS = numDemoFrames / demoSeconds;
		idStr	message = va( "%i frames rendered in %3.1f seconds = %3.1f fps\n", numDemoFrames, demoSeconds, demoFPS );

		common->Printf( message );
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
	demoName.DefaultFileExtension( ".demo" );
	if ( !readDemo->OpenForReading( demoName ) ) {
		common->Printf( "couldn't open %s\n", demoName.c_str() );
		delete readDemo;
		readDemo = NULL;
		Stop();
		StartMenu();
		soundSystem->SetMute( false );
		return;
	}

	insideExecuteMapChange = true;
	UpdateScreen();
	insideExecuteMapChange = false;
	guiLoading->SetStateString( "demo", "" );

	// setup default render demo settings
	// that's default for <= legacy engine v1.1
	renderdemoVersion = 1;
	savegameVersion = 16;

	AdvanceRenderDemo( true );

	numDemoFrames = 1;

	lastDemoTic = -1;
	timeDemoStartTime = Sys_Milliseconds();
}

/*
================
idSessionLocal::TimeRenderDemo
================
*/
void idSessionLocal::TimeRenderDemo( const char *demoName, bool twice ) {
	idStr demo = demoName;
	
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
		reloadArgs.AppendArg( mapName );
		reloadArgs.AppendArg( devmap ? "1" : "0" );
		if ( entityFilter != NULL && entityFilter[0] != '\0' ) {
			reloadArgs.AppendArg( entityFilter );
		}
		cmdSystem->SetupReloadEngine( reloadArgs );
		return;
	}

	// clear the userInfo so the player starts out with the defaults
	mapSpawnData.userInfo[0].Clear();
	mapSpawnData.persistentPlayerInfo[0].Clear();
	mapSpawnData.userInfo[0] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );

	mapSpawnData.serverInfo.Clear();
	mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	mapSpawnData.serverInfo.Set( "si_gameType", "singleplayer" );
	Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, entityFilter );

	// set the devmap key so any play testing items will be given at
	// spawn time to set approximately the right weapons and ammo
	if(devmap) {
		mapSpawnData.serverInfo.Set( "devmap", "1" );
	}

	mapSpawnData.syncedCVars.Clear();
	mapSpawnData.syncedCVars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );

	MoveToNewMap( mapName );
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
	idDict mapDeclDict;
	const char *entityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	const idDict *mapDef = Session_GetMapDeclDict( mapName, entityFilter, mapDeclDict ) ? &mapDeclDict : NULL;
	if ( mapDef ) {
		mapName = common->GetLanguageDict()->GetString( mapDef->GetString( "name", mapName ) );
	}
	// Fixme: Localization
	return va( "^3AutoSave:^0 %s", mapName );
}

/*
===============
idSessionLocal::MoveToNewMap

Leaves the existing userinfo and serverinfo
===============
*/
void idSessionLocal::MoveToNewMap( const char *mapName ) {
	mapSpawnData.serverInfo.Set( "si_map", mapName );

	ExecuteMapChange();

	if ( !mapSpawnData.serverInfo.GetBool("devmap") ) {
		// Autosave at the beginning of the level
		SaveGame( GetAutoSaveName( mapName ), true );
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
void idSessionLocal::LoadCmdDemoFromFile( idFile *file ) {

	mapSpawnData.serverInfo.ReadFromFileHandle( file );

	for ( int i = 0 ; i < MAX_ASYNC_CLIENTS ; i++ ) {
		mapSpawnData.userInfo[i].ReadFromFileHandle( file );
		mapSpawnData.persistentPlayerInfo[i].ReadFromFileHandle( file );
	}
	file->Read( &mapSpawnData.mapSpawnUsercmd, sizeof( mapSpawnData.mapSpawnUsercmd ) );
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
	fullDemoName.DefaultFileExtension( ".cdemo" );
	cmdDemoFile = fileSystem->OpenFileRead(fullDemoName);

	if ( cmdDemoFile == NULL ) {
		common->Printf( "Couldn't open %s\n", fullDemoName.c_str() );
		return;
	}

// jmarshall - quake 4 loading gui
	guiLoading = uiManager->FindGui("guis/loading/generic.gui", true, false, true);
// jmarshall end
	//cmdDemoFile->Read(&loadGameTime, sizeof(loadGameTime));

	LoadCmdDemoFromFile(cmdDemoFile);

	// start the map
	ExecuteMapChange();

	cmdDemoFile = fileSystem->OpenFileRead(fullDemoName);

	// have to do this twice as the execmapchange clears the cmddemofile
	LoadCmdDemoFromFile(cmdDemoFile);

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
	StopPlayingRenderDemo();

	if ( com_showFramePacing.GetInteger() >= 2 && framePacingStats.valid ) {
		Session_PrintFramePacingSummary( framePacingStats, " final" );
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

	iamTheDukeActive = false;
	mapSpawned = false;
}

/*
===============
idSessionLocal::LoadLoadingGui
===============
*/
void idSessionLocal::LoadLoadingGui( const char *mapName ) {
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

		const char *loadImage = mapDef->GetString( "loadimage", "" );
		if ( loadImage[0] ) {
			loadingBackground = loadImage;
		} else {
			// Match MP levelshot fallback behavior (including addon extraction).
			char screenshot[ MAX_STRING_CHARS ];
			fileSystem->FindMapScreenshot( spawnMapPath, screenshot, MAX_STRING_CHARS );
			loadingBackground = screenshot;
		}

		loadGuiOverride = mapDef->GetString( "loadgui", "" );
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
	strncpy( guiMap, va( "guis/map/%s.gui", stripped.c_str() ), MAX_STRING_CHARS );
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
				const char *netIP = cvarSystem->GetCVarString( "net_ip" );
				const int netPort = cvarSystem->GetCVarInteger( "net_port" );
				if ( netIP && netIP[ 0 ] ) {
					serverAddress = va( "%s:%d", idStr::Icmp( netIP, "0.0.0.0" ) ? netIP : "127.0.0.1", netPort );
				} else if ( netPort > 0 ) {
					serverAddress = va( "127.0.0.1:%d", netPort );
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

	idStr fullMapName = "maps/";
	fullMapName += mapString;
	fullMapName.StripFileExtension();

	// shut down the existing game if it is running
	UnloadMap();
	console->SetProcFileOutOfDate( false );

	// don't do the deferred caching if we are reloading the same map
	if ( fullMapName == currentMapName ) {
		reloadingSameMap = true;
	} else {
		reloadingSameMap = false;
		currentMapName = fullMapName;
	}
	fileSystem->SetAssetLogName( fullMapName.c_str() );

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

	// set the loading gui that we will wipe to
	LoadLoadingGui( mapString );

	// cause prints to force screen updates as a pacifier,
	// and draw the loading gui instead of game draws
	insideExecuteMapChange = true;

	// if this works out we will probably want all the sizes in a def file although this solution will 
	// work for new maps etc. after the first load. we can also drop the sizes into the default.cfg
	fileSystem->ResetReadCount();
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

	common->Printf( "--------- Map Initialization ---------\n" );
	common->Printf( "Map: %s\n", mapString.c_str() );

	// let the renderSystem load all the geometry
	if ( !rw->InitFromMap( fullMapName ) ) {
		common->Error( "couldn't load %s", fullMapName.c_str() );
	}

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

	if ( !idAsyncNetwork::IsActive() && !loadingSaveGame ) {
		// spawn players
		for ( i = 0; i < numClients; i++ ) {
			game->SpawnPlayer( i, false, NULL );
		}
	}

	// actually purge/load the media
	if ( !reloadingSameMap ) {
		renderSystem->EndLevelLoad();
		soundSystem->EndLevelLoad();
		declManager->EndLevelLoad();
		if ( bse ) {
			bse->EndLevelLoad();
		}
		SetBytesNeededForMapLoad( mapString.c_str(), fileSystem->GetReadCount() );
	}
	uiManager->EndLevelLoad();

	if ( !idAsyncNetwork::IsActive() && !loadingSaveGame ) {
		// run a few frames to allow everything to settle
		for ( i = 0; i < 10; i++ ) {
			game->RunFrame( mapSpawnData.mapSpawnUsercmd, 0, true, 0 ); // serverGameFrame isn't used
		}
	}

	common->Printf ("-----------------------------------\n");

	int	msec = Sys_Milliseconds() - start;
	common->Printf( "%6d msec to load %s\n", msec, mapString.c_str() );

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
		while ( waitingForContinue ) {
			Sys_GenerateEvents();

			while ( waitingForContinue ) {
				sysEvent_t ev = eventLoop->GetEvent();
				if ( ev.evType == SE_NONE ) {
					break;
				}

				if ( ev.evType == SE_KEY ) {
					idKeyInput::PreliminaryKeyEvent( ev.evValue, ( ev.evValue2 != 0 ) );
				}

				if ( ev.evType == SE_CONSOLE ) {
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, (char *)ev.evPtr );
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
				} else if ( waitingForContinue && acceptContinueInput && ev.evType == SE_KEY && ev.evValue2 && Session_IsLoadingContinueKey( ev.evValue ) ) {
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

		if ( logLoadingContinueGate ) {
			const int loadingContinueElapsedMsec = Max( 0, common->GetPresentationTime() - loadingContinueStartTime );
			common->Printf( "Loading continue gate completed via %s after %d ms\n",
				loadingContinueAutoAdvanced ? "auto-advance" : "input", loadingContinueElapsedMsec );
		}

		// The same key/button used to continue would otherwise remain latched into
		// the first gameplay frame and can immediately trigger cinematic skip.
		idKeyInput::ClearStates();
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
	ResetFramePacingStats();
	Sys_ClearEvents();
}

/*
===============
LoadGame_f
===============
*/
void LoadGame_f( const idCmdArgs &args ) {
	console->Close();
	if ( args.Argc() < 2 || idStr::Icmp(args.Argv(1), "quick" ) == 0 ) {
		idStr saveName = common->GetLanguageDict()->GetString( "#str_07178" );
		sessLocal.LoadGame( saveName );
	} else {
		sessLocal.LoadGame( args.Argv(1) );
	}
}

/*
===============
SaveGame_f
===============
*/
void SaveGame_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 || idStr::Icmp( args.Argv(1), "quick" ) == 0 ) {
		idStr saveName = common->GetLanguageDict()->GetString( "#str_07178" );
		if ( sessLocal.SaveGame( saveName ) ) {
			common->Printf( "%s\n", saveName.c_str() );
		}
	} else {
		if ( sessLocal.SaveGame( args.Argv(1) ) ) {
			common->Printf( "Saved %s\n", args.Argv(1) );
		}
	}
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
		} else if ( inFileName[i] == ' ' ) {
			saveFileName += '_';
		} else {
			saveFileName += inFileName[i];
		}
	}
}

/*
===============
idSessionLocal::SaveGame
===============
*/
bool idSessionLocal::SaveGame( const char *saveName, bool autosave ) {
#ifdef	ID_DEDICATED
	common->Printf( "Dedicated servers cannot save games.\n" );
	return false;
#else
	int i;
	idStr gameFile, previewFile, descriptionFile, mapName;

	if ( !mapSpawned ) {
		common->Printf( "Not playing a game.\n" );
		return false;
	}

	if ( IsMultiplayer() ) {
		common->Printf( "Can't save during net play.\n" );
		return false;
	}

	if ( game->GetPersistentPlayerInfo( 0 ).GetInt( "health" ) <= 0 ) {
		MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_04311" ), common->GetLanguageDict()->GetString ( "#str_04312" ), true );
		common->Printf( "You must be alive to save the game\n" );
		return false;
	}

	if ( Sys_GetDriveFreeSpace( cvarSystem->GetCVarString( "fs_savepath" ) ) < 25 ) {
		MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_04313" ), common->GetLanguageDict()->GetString ( "#str_04314" ), true );
		common->Printf( "Not enough drive space to save the game\n" );
		return false;
	}

	idSoundWorld *pauseWorld = soundSystem->GetPlayingSoundWorld();
	if ( pauseWorld ) {
		pauseWorld->Pause();
		SetPlayingSoundWorld( NULL );
	}

	// setup up filenames and paths
	gameFile = saveName;
	ScrubSaveGameFileName( gameFile );

	gameFile = "savegames/" + gameFile;
	gameFile.SetFileExtension( ".save" );

	previewFile = gameFile;
	previewFile.SetFileExtension( ".tga" );

	descriptionFile = gameFile;
	descriptionFile.SetFileExtension( ".txt" );

	// Open savegame file
	idFile *fileOut = fileSystem->OpenFileWrite( gameFile );
	if ( fileOut == NULL ) {
		common->Warning( "Failed to open save file '%s'\n", gameFile.c_str() );
		if ( pauseWorld ) {
			SetPlayingSoundWorld( pauseWorld );
			pauseWorld->UnPause();
		}
		return false;
	}

	// Write SaveGame Header:
	// Game Name / Version / Map Name / Entity Filter / Persistent Player Info

	// game
	const char *gamename = SAVEGAME_GAME_NAME_RETAIL;
	fileOut->WriteString( gamename );

	// version
	fileOut->WriteInt( SAVEGAME_VERSION );

	// map
	mapName = mapSpawnData.serverInfo.GetString( "si_map" );
	fileOut->WriteString( mapName );

	// retail Quake 4 also stores the active entity filter in the header.
	fileOut->WriteString( mapSpawnData.serverInfo.GetString( "si_entityFilter" ) );

	// persistent player info
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.persistentPlayerInfo[i] = game->GetPersistentPlayerInfo( i );
		mapSpawnData.persistentPlayerInfo[i].WriteToFileHandle( fileOut );
	}

	// let the game save its state
	game->SaveGame( fileOut );

	// close the sava game file
	fileSystem->CloseFile( fileOut );

	// Write screenshot
	if ( !autosave ) {
		renderSystem->CropRenderSize( 320, 240, false );
		game->Draw( 0 );
		renderSystem->CaptureRenderToFile( previewFile, true );
		renderSystem->UnCrop();
	}

	// Write description, which is just a text file with
	// the unclean save name on line 1, map name on line 2, screenshot on line 3
	idFile *fileDesc = fileSystem->OpenFileWrite( descriptionFile );
	if ( fileDesc == NULL ) {
		common->Warning( "Failed to open description file '%s'\n", descriptionFile.c_str() );
		if ( pauseWorld ) {
			SetPlayingSoundWorld( pauseWorld );
			pauseWorld->UnPause();
		}
		return false;
	}

	idStr description = saveName;
	description.Replace( "\\", "\\\\" );
	description.Replace( "\"", "\\\"" );

	const idDeclEntityDef *mapDef = static_cast<const idDeclEntityDef *>(declManager->FindType( DECL_MAPDEF, mapName, false ));
	if ( mapDef ) {
		mapName = common->GetLanguageDict()->GetString( mapDef->dict.GetString( "name", mapName ) );
	}

	fileDesc->Printf( "\"%s\"\n", description.c_str() );
	fileDesc->Printf( "\"%s\"\n", mapName.c_str());

	if ( autosave ) {
		idStr sshot = mapSpawnData.serverInfo.GetString( "si_map" );
		sshot.StripPath();
		sshot.StripFileExtension();
		fileDesc->Printf( "\"guis/assets/autosave/%s\"\n", sshot.c_str() );
	} else {
		fileDesc->Printf( "\"\"\n" );
	}

	fileSystem->CloseFile( fileDesc );

	if ( pauseWorld ) {
		SetPlayingSoundWorld( pauseWorld );
		pauseWorld->UnPause();
	}

	syncNextGameFrame = true;


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
	idStr in, loadFile, saveMap, gamename, entityFilter;

	if ( IsMultiplayer() ) {
		common->Printf( "Can't load during net play.\n" );
		return false;
	}

	const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );
	if ( idStr::Icmp( activeModule, "game_sp" ) != 0 ) {
		cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
		cvarSystem->SetCVarString( "com_nextGameModule", "game_sp" );
		idCmdArgs reloadArgs;
		reloadArgs.AppendArg( "loadGame" );
		reloadArgs.AppendArg( saveName );
		cmdSystem->SetupReloadEngine( reloadArgs );
		return true;
	}

	//Hide the dialog box if it is up.
	StopBox();

	loadFile = saveName;
	ScrubSaveGameFileName( loadFile );
	loadFile.SetFileExtension( ".save" );

	in = "savegames/";
	in += loadFile;

	// Open savegame file
	// only allow loads from the game directory because we don't want a base game to load
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	savegameFile = fileSystem->OpenFileRead( in, true, game.Length() ? game : NULL );

	if ( savegameFile == NULL ) {
		common->Warning( "Couldn't open savegame file %s", in.c_str() );
		return false;
	}

	loadingSaveGame = true;

	// Read in save game header
	// Game Name / Version / Map Name / [Entity Filter] / Persistent Player Info

	// game
	savegameFile->ReadString( gamename );

	// Accept both the retail Quake 4 save header and older OpenQ4-branded headers.
	if ( !Session_IsSupportedSaveGameName( gamename ) ) {
		common->Warning( "Attempted to load an invalid savegame header '%s' from %s",
			gamename.c_str(), in.c_str() );

		loadingSaveGame = false;
		fileSystem->CloseFile( savegameFile );
		savegameFile = NULL;
		return false;
	}

	// version
	savegameFile->ReadInt( savegameVersion );

	// map
	savegameFile->ReadString( saveMap );

	// retail Quake 4 stores an entity filter after the map name.
	// Older OpenQ4 saves omitted it, so only consume it from retail-style headers.
	if ( Session_SaveGameHeaderUsesEntityFilter( gamename ) ) {
		savegameFile->ReadString( entityFilter );
	} else {
		entityFilter.Clear();
	}

	// persistent player info
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		mapSpawnData.persistentPlayerInfo[i].ReadFromFileHandle( savegameFile );
	}

	// check the version, if it doesn't match, cancel the loadgame,
	// but still load the map with the persistant playerInfo from the header
	// so that the player doesn't lose too much progress.
	if ( !Session_IsCompatibleSaveGameVersion( savegameVersion ) ) {
		common->Warning( "Savegame Version mismatch: aborting loadgame and starting level with persistent data" );
		loadingSaveGame = false;
		fileSystem->CloseFile( savegameFile );
		savegameFile = NULL;
	}

	common->DPrintf( "loading a v%d savegame\n", savegameVersion );

	if ( saveMap.Length() > 0 ) {

		// Start loading map
		mapSpawnData.serverInfo.Clear();

		mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
		mapSpawnData.serverInfo.Set( "si_gameType", "singleplayer" );

		mapSpawnData.serverInfo.Set( "si_map", saveMap );
		mapSpawnData.serverInfo.Set( "si_entityFilter", entityFilter );

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
idSessionLocal::ProcessEvent
===============
*/
bool idSessionLocal::ProcessEvent( const sysEvent_t *event ) {
	// hitting escape anywhere brings up the menu
	if ( !guiActive && event->evType == SE_KEY && event->evValue2 == 1 &&
		( event->evValue == K_ESCAPE || event->evValue == K_JOY7 || event->evValue == K_JOY8 ) ) {
		console->Close();
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
	if ( lastDemoTic == -1 ) {
		lastDemoTic = latchedTicNumber - 1;
	}

	int skipFrames = 0;

	if ( !aviCaptureMode && !timeDemo && !singleFrameOnly ) {
		skipFrames = ( (latchedTicNumber - lastDemoTic) / USERCMD_PER_DEMO_FRAME ) - 1;
		// never skip too many frames, just let it go into slightly slow motion
		if ( skipFrames > 4 ) {
			skipFrames = 4;
		}
		lastDemoTic = latchedTicNumber - latchedTicNumber % USERCMD_PER_DEMO_FRAME;
	} else {
		// always advance a single frame with avidemo and timedemo
		lastDemoTic = latchedTicNumber; 
	}

	while( skipFrames > -1 ) {
		int		ds = DS_FINISHED;

		readDemo->ReadInt( ds );
		if ( ds == DS_FINISHED ) {
			if ( numDemoFrames != 1 ) {
				// if the demo has a single frame (a demoShot), continuously replay
				// the renderView that has already been read
				Stop();
				StartMenu();
			}
			break;
		}
		if ( ds == DS_RENDER ) {
			if ( rw->ProcessDemoCommand( readDemo, &currentDemoRenderView, &demoTimeOffset ) ) {
				// The render world returns true once the full frame payload has been consumed.
				skipFrames--;
				numDemoFrames++;
			}
			continue;
		}
		if ( ds == DS_SOUND ) {
			sw->ProcessDemoCommand( readDemo );
			continue;
		}
		// appears in v1.2, with savegame format 17
		if ( ds == DS_VERSION ) {
			readDemo->ReadInt( renderdemoVersion );
			common->Printf( "reading a v%d render demo\n", renderdemoVersion );
			// set the savegameVersion to current for render demo paths that share the savegame paths
			savegameVersion = SAVEGAME_VERSION;
			continue;
		}
		common->Error( "Bad render demo token" );
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
	if ( !insideExecuteMapChange ) {
		return;
	}

	// never do pacifier screen updates while inside the
	// drawing code, or we can have various recursive problems
	if ( insideUpdateScreen ) {
		return;
	}

	const int time = Sys_Milliseconds();
	const float minPacifierIntervalMs = Session_GetBlockingLoadFrameIntervalMsec();
	int elapsedMs = time - lastPacifierTime;
	if ( lastPacifierTime != 0 && static_cast<float>( elapsedMs ) < minPacifierIntervalMs ) {
		return;
	}

	Session_BeginBlockingLoadPresentationFrame();

	const int presentationTime = common->GetPresentationTime();
	elapsedMs = presentationTime - lastPacifierTime;
	if ( static_cast<float>( elapsedMs ) < minPacifierIntervalMs ) {
		elapsedMs = static_cast<int>( idMath::Ceil( minPacifierIntervalMs ) );
	}
	lastPacifierTime = presentationTime;

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

	UpdateScreen();

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

		if ( guiActive->State().GetBool( "gameDraw" ) ) {
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

	// draw the wipe material on top of this if it hasn't completed yet
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
		if ( !timeDemo && numDemoFrames != 1 ) {
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
		if ( !cmdDemoFile->Read( &logCmd, sizeof( logCmd ) ) ) {
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
			// get current player states
			for ( int i = 0 ; i < numClients ; i++ ) {
				mapSpawnData.persistentPlayerInfo[i] = game->GetPersistentPlayerInfo( i );
			}
			// clear the devmap key on serverinfo, so player spawns
			// won't get the map testing items
			mapSpawnData.serverInfo.Delete( "devmap" );
			Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, Session_GetEntityFilterArg( args ) );

			// go to the next map
			MoveToNewMap( args.Argv(1) );
		} else if ( !idStr::Icmp( args.Argv(0), "devmap" ) ) {
			mapSpawnData.serverInfo.Set( "devmap", "1" );
			Session_ApplyEntityFilterToServerInfo( mapSpawnData.serverInfo, Session_GetEntityFilterArg( args ) );
			MoveToNewMap( args.Argv(1) );
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
		}
	}
}

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
	cmdSystem->AddCommand( "openq4_startSingleplayer", Session_OpenQ4StartSingleplayer_f, CMD_FL_SYSTEM, "internal helper to start singleplayer after game-module switches" );
	cmdSystem->AddCommand( "openq4_resumeBakeLightGrids", Session_OpenQ4ResumeBakeLightGrids_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "internal helper to continue light-grid baking after game-module switches" );
	cmdSystem->AddCommand( "iamtheduke", Session_IAmTheDuke_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "toggles the SP-only iamtheduke cheat text overlay" );
	cmdSystem->AddCommand( "bakeLightGrids", Session_BakeLightGrids_f, CMD_FL_SYSTEM|CMD_FL_CHEAT, "bakes OpenQ4-compatible lightgrid metadata and irradiance atlases for the current map or a batch of maps" );
	cmdSystem->AddCommand( "map", Session_Map_f, CMD_FL_SYSTEM, "loads a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "devmap", Session_DevMap_f, CMD_FL_SYSTEM, "loads a map in developer mode", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "testmap", Session_TestMap_f, CMD_FL_SYSTEM, "tests a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "framePacingSnapshot", Session_FramePacingSnapshot_f, CMD_FL_SYSTEM, "prints the current frame-pacing diagnostics summary" );
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
#ifndef ID_DEMO_BUILD
	guiMainMenu = uiManager->FindGui( "guis/mainmenu.gui", true, false, true );
#else
	guiMainMenu = uiManager->FindGui( "guis/demo_mainmenu.gui", true, false, true );
#endif
	guiMainMenu_MapList = uiManager->AllocListGUI();
	guiMainMenu_MapList->Config( guiMainMenu, "mapList" );
	idAsyncNetwork::client.serverList.GUIConfig( guiMainMenu, "serverList" );
	guiRestartMenu = uiManager->FindGui( "guis/restart.gui", true, false, true );
	guiGameOver = uiManager->FindGui( "guis/gameover.gui", true, false, true );
	guiMsg = uiManager->FindGui( "guis/msg.gui", true, false, true );
	guiTakeNotes = uiManager->FindGui( "guis/takeNotes.gui", true, false, true );
	guiIntro = uiManager->FindGui( "guis/intro.gui", true, false, true );

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
	idSoundWorld *targetSoundWorld = soundWorld;

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
