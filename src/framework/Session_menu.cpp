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
#include "../ui/ListGUILocal.h"
#include "../ui/Window.h"
#include "../sound/snd_local.h"

#if defined( USE_SDL3 )
#include <SDL3/SDL.h>
#endif

extern idCVar com_skipLogoVideos;
extern glconfig_t glConfig;

idCVar	idSessionLocal::gui_configServerRate( "gui_configServerRate", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_ROM | CVAR_INTEGER, "" );
idCVar gui_set_sys_scroll( "gui_set_sys_scroll", "0", CVAR_GUI | CVAR_INTEGER, "display menu scroll step", 0, 28 );
idCVar gui_set_audio_scroll( "gui_set_audio_scroll", "0", CVAR_GUI | CVAR_INTEGER, "audio menu scroll step", 0.0f, 0.0f );
idCVar gui_set_game_scroll( "gui_set_game_scroll", "0", CVAR_GUI | CVAR_INTEGER, "game menu scroll step", 0, 46 );

static const int MENU_CONTROLLER_AXIS_THRESHOLD = 50;
static const int MENU_CONTROLLER_REPEAT_INITIAL_MSEC = 320;
static const int MENU_CONTROLLER_REPEAT_MSEC = 110;

typedef struct menuControllerRepeat_s {
	int		key;
	bool	fromAnalog;
	int		nextTime;
} menuControllerRepeat_t;

typedef struct mainMenuGunPositionPreset_s {
	float	x;
	float	y;
	float	z;
	bool	weaponFovEffect;
} mainMenuGunPositionPreset_t;

static menuControllerRepeat_t menuControllerRepeat = { 0, false, 0 };

static const int MENU_SETTINGS_PAGE_SCROLL_STEP = 6;
static const mainMenuGunPositionPreset_t MAINMENU_GUN_POSITION_PRESETS[] = {
	{ 1.0f, 2.0f, -1.0f, true },
	{ 1.0f, -5.0f, -1.0f, true },
	{ 1.0f, 0.0f, -1.0f, true }
};
static const char *MAINMENU_FORCE_MODEL_MARINE = "model_player_marine_helmeted_bright";
static const char *MAINMENU_FORCE_MODEL_STROGG = "model_player_tactical_transfer_bright";
static const float MAINMENU_CORPSE_TIME_PRESET_VALUES[] = {
	0.0f,
	-1.0f,
	5.0f,
	10.0f,
	15.0f,
	30.0f,
	60.0f
};

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const char *pageName, bool requireVisiblePage );
static void SyncMainMenuSettingsScrollPages( idUserInterface *gui );

static int MenuControllerAbs( int value ) {
	return value < 0 ? -value : value;
}

static void ClearMenuControllerRepeatState( void ) {
	menuControllerRepeat.key = 0;
	menuControllerRepeat.fromAnalog = false;
	menuControllerRepeat.nextTime = 0;
}

static int GetHeldControllerMenuKey( void ) {
	if ( idKeyInput::IsDown( K_JOY9 ) ) {
		return K_JOY9;
	}
	if ( idKeyInput::IsDown( K_JOY10 ) ) {
		return K_JOY10;
	}
	if ( idKeyInput::IsDown( K_JOY12 ) ) {
		return K_JOY12;
	}
	if ( idKeyInput::IsDown( K_JOY11 ) ) {
		return K_JOY11;
	}
	if ( idKeyInput::IsDown( K_JOY1 ) ) {
		return K_JOY1;
	}
	if ( idKeyInput::IsDown( K_JOY2 ) ) {
		return K_JOY2;
	}
	return 0;
}

static int GetAnalogControllerMenuKey( void ) {
	int moveX = 0;
	int moveY = 0;
	const bool haveX = Sys_GetJoystickAxisState( AXIS_YAW, moveX );
	const bool haveY = Sys_GetJoystickAxisState( AXIS_PITCH, moveY );
	if ( !haveX && !haveY ) {
		return 0;
	}

	const int absX = MenuControllerAbs( moveX );
	const int absY = MenuControllerAbs( moveY );
	if ( absX < MENU_CONTROLLER_AXIS_THRESHOLD && absY < MENU_CONTROLLER_AXIS_THRESHOLD ) {
		return 0;
	}

	if ( absX > absY ) {
		return moveX > 0 ? K_JOY11 : K_JOY12;
	}
	return moveY > 0 ? K_JOY9 : K_JOY10;
}

static int GetControllerMenuNavigationKey( bool &fromAnalog ) {
	fromAnalog = false;

	const int heldKey = GetHeldControllerMenuKey();
	if ( heldKey != 0 ) {
		return heldKey;
	}

	const int analogKey = GetAnalogControllerMenuKey();
	if ( analogKey != 0 ) {
		fromAnalog = true;
		return analogKey;
	}

	return 0;
}

static void PumpControllerMenuNavigation( idSessionLocal *session ) {
	if ( session == NULL ) {
		ClearMenuControllerRepeatState();
		return;
	}

	bool fromAnalog = false;
	const int key = GetControllerMenuNavigationKey( fromAnalog );
	if ( key == 0 ) {
		ClearMenuControllerRepeatState();
		return;
	}

	const int now = common->GetPresentationTime();
	if ( menuControllerRepeat.key != key || menuControllerRepeat.fromAnalog != fromAnalog ) {
		menuControllerRepeat.key = key;
		menuControllerRepeat.fromAnalog = fromAnalog;
		menuControllerRepeat.nextTime = now + ( fromAnalog ? 0 : MENU_CONTROLLER_REPEAT_INITIAL_MSEC );
	}

	if ( now < menuControllerRepeat.nextTime ) {
		return;
	}

	sysEvent_t event;
	memset( &event, 0, sizeof( event ) );
	event.evType = SE_KEY;
	event.evValue = key;
	event.evValue2 = 1;

	session->MenuEvent( &event );
	menuControllerRepeat.nextTime = now + MENU_CONTROLLER_REPEAT_MSEC;
}

static void ApplyMainMenuGunPositionChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const int choice = gui->State().GetInt( "g_gunXYZ" );
	const int numChoices = sizeof( MAINMENU_GUN_POSITION_PRESETS ) / sizeof( MAINMENU_GUN_POSITION_PRESETS[0] );
	if ( choice < 0 || choice >= numChoices ) {
		return;
	}

	const mainMenuGunPositionPreset_t &preset = MAINMENU_GUN_POSITION_PRESETS[ choice ];
	cvarSystem->SetCVarFloat( "g_gunX", preset.x );
	cvarSystem->SetCVarFloat( "g_gunY", preset.y );
	cvarSystem->SetCVarFloat( "g_gunZ", preset.z );
	if ( cvarSystem->Find( "g_weaponFovEffect" ) != NULL ) {
		cvarSystem->SetCVarBool( "g_weaponFovEffect", preset.weaponFovEffect );
	}
}

static void ApplyMainMenuForceModelChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const bool enabled = gui->State().GetBool( "ui_proskins" );
	cvarSystem->SetCVarString( "g_forceModel", enabled ? MAINMENU_FORCE_MODEL_MARINE : "" );
	cvarSystem->SetCVarString( "g_forceMarineModel", enabled ? MAINMENU_FORCE_MODEL_MARINE : "" );
	cvarSystem->SetCVarString( "g_forceStroggModel", enabled ? MAINMENU_FORCE_MODEL_STROGG : "" );
}

static void ApplyMainMenuCorpseTimeChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const int choice = gui->State().GetInt( "corpse_time_choice" );
	const int numChoices = sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES ) / sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES[0] );
	if ( choice < 0 || choice >= numChoices ) {
		return;
	}

	const char *const targetCvar = gui->State().GetInt( "ingame" ) == 2 ? "g_corpseRemoveDelayMP" : "g_corpseRemoveDelaySP";
	cvarSystem->SetCVarFloat( targetCvar, MAINMENU_CORPSE_TIME_PRESET_VALUES[ choice ] );
}

/*
=================
BuildMainMenuAudioDeviceChoices
=================
*/
static void BuildMainMenuAudioDeviceChoices( idStr &choiceNames, idStr &choiceValues ) {
#if defined( USE_OPENAL )
	idSoundHardware_OpenAL::BuildDeviceChoiceStrings( cvarSystem->GetCVarString( "s_deviceName" ), choiceNames, choiceValues );
#else
	choiceNames = common->GetLanguageDict()->GetString( "#str_229913" );
	choiceValues = "";
#endif
}

/*
=================
BuildMainMenuDisplayChoices
=================
*/
static void BuildMainMenuDisplayChoices( idStr &choiceNames, idStr &choiceValues, int &displayCountOut ) {
	choiceNames = common->GetLanguageDict()->GetString( "#str_229914" );
	choiceValues = "-1";
	displayCountOut = 0;

#if defined( USE_SDL3 )
	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &displayCount );
	if ( displays == NULL || displayCount <= 0 ) {
		if ( displays != NULL ) {
			SDL_free( displays );
		}
		return;
	}
	displayCountOut = displayCount;

	for ( int i = 0; i < displayCount; ++i ) {
		const char *displayName = SDL_GetDisplayName( displays[i] );
		if ( displayName == NULL || displayName[0] == '\0' ) {
			displayName = "Display";
		}

		idStr label = va( "%d: %s", i + 1, displayName );
		label.Replace( ";", "," );
		choiceNames += ";";
		choiceNames += label;
		choiceValues += ";";
		choiceValues += va( "%d", i );
	}

	SDL_free( displays );
#endif
}

typedef struct mainMenuVidMode_s {
	int	mode;
	int	width;
	int	height;
} mainMenuVidMode_t;

static const mainMenuVidMode_t mainMenuVidModes[] = {
	{ 11, 640, 480 },
	{ 12, 720, 480 },
	{ 13, 720, 576 },
	{ 14, 800, 480 },
	{ 15, 800, 600 },
	{ 16, 854, 480 },
	{ 17, 960, 540 },
	{ 18, 960, 600 },
	{ 19, 1024, 576 },
	{ 20, 1024, 600 },
	{ 21, 1024, 768 },
	{ 22, 1152, 648 },
	{ 23, 1152, 720 },
	{ 24, 1152, 768 },
	{ 25, 1152, 864 },
	{ 0, 1280, 720 },
	{ 26, 1280, 768 },
	{ 27, 1280, 800 },
	{ 28, 1280, 854 },
	{ 29, 1280, 960 },
	{ 30, 1280, 1024 },
	{ 31, 1360, 768 },
	{ 1, 1366, 768 },
	{ 32, 1400, 900 },
	{ 33, 1400, 1050 },
	{ 34, 1440, 900 },
	{ 35, 1440, 960 },
	{ 36, 1536, 864 },
	{ 2, 1600, 900 },
	{ 37, 1600, 1200 },
	{ 38, 1680, 1050 },
	{ 3, 1920, 1080 },
	{ 4, 1920, 1200 },
	{ 39, 1920, 1280 },
	{ 40, 2048, 1080 },
	{ 41, 2048, 1152 },
	{ 42, 2048, 1536 },
	{ 43, 2160, 1440 },
	{ 44, 2240, 1260 },
	{ 45, 2240, 1400 },
	{ 46, 2256, 1504 },
	{ 47, 2304, 1440 },
	{ 48, 2400, 1350 },
	{ 49, 2400, 1600 },
	{ 50, 2520, 1680 },
	{ 5, 2560, 1080 },
	{ 6, 2560, 1440 },
	{ 51, 2560, 1600 },
	{ 52, 2560, 1664 },
	{ 53, 2736, 1824 },
	{ 54, 2880, 1620 },
	{ 55, 2880, 1800 },
	{ 56, 2880, 1864 },
	{ 57, 2880, 1920 },
	{ 58, 3000, 2000 },
	{ 59, 3024, 1964 },
	{ 60, 3200, 1800 },
	{ 61, 3200, 2000 },
	{ 62, 3240, 2160 },
	{ 7, 3440, 1440 },
	{ 63, 3456, 2160 },
	{ 64, 3456, 2234 },
	{ 65, 3840, 1080 },
	{ 66, 3840, 1200 },
	{ 67, 3840, 1600 },
	{ 8, 3840, 2160 },
	{ 68, 3840, 2400 },
	{ 69, 3840, 2560 },
	{ 70, 4096, 2160 },
	{ 9, 5120, 1440 },
	{ 71, 5120, 2160 },
	{ 10, 5120, 2880 },
	{ 72, 6016, 3384 },
	{ 73, 7680, 2160 },
	{ 74, 7680, 4320 }
};

typedef struct mainMenuDisplayModeOption_s {
	int		width;
	int		height;
	int		legacyMode;
	bool	desktopNative;
	bool	custom;
	idStr	label;
} mainMenuDisplayModeOption_t;

static void AppendMainMenuChoice( idStr &choiceNames, idStr &choiceValues, const char *label, int value ) {
	if ( choiceNames.Length() > 0 ) {
		choiceNames += ";";
		choiceValues += ";";
	}

	choiceNames += label;
	choiceValues += va( "%d", value );
}

static const mainMenuVidMode_t *FindMainMenuVidModeByMode( int mode ) {
	for ( int i = 0; i < static_cast<int>( sizeof( mainMenuVidModes ) / sizeof( mainMenuVidModes[0] ) ); ++i ) {
		if ( mainMenuVidModes[i].mode == mode ) {
			return &mainMenuVidModes[i];
		}
	}
	return NULL;
}

static const mainMenuVidMode_t *FindMainMenuVidModeByResolution( int width, int height ) {
	for ( int i = 0; i < static_cast<int>( sizeof( mainMenuVidModes ) / sizeof( mainMenuVidModes[0] ) ); ++i ) {
		if ( mainMenuVidModes[i].width == width && mainMenuVidModes[i].height == height ) {
			return &mainMenuVidModes[i];
		}
	}
	return NULL;
}

static bool MainMenuDisplayModeResolutionExists( const idList<mainMenuDisplayModeOption_t> &options, int width, int height ) {
	for ( int i = 0; i < options.Num(); ++i ) {
		if ( options[i].desktopNative || options[i].custom ) {
			continue;
		}
		if ( options[i].width == width && options[i].height == height ) {
			return true;
		}
	}
	return false;
}

static int MainMenuGreatestCommonDivisor( int a, int b ) {
	a = idMath::Abs( a );
	b = idMath::Abs( b );
	while ( b != 0 ) {
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a > 0 ? a : 1;
}

static idStr MainMenuDisplayAspectRatioLabel( int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return "";
	}

	typedef struct commonAspectRatio_s {
		int width;
		int height;
	} commonAspectRatio_t;
	static const commonAspectRatio_t commonAspectRatios[] = {
		{ 5, 4 },
		{ 4, 3 },
		{ 3, 2 },
		{ 5, 3 },
		{ 16, 10 },
		{ 16, 9 },
		{ 21, 9 },
		{ 32, 10 },
		{ 32, 9 }
	};

	const float displayAspect = static_cast<float>( width ) / static_cast<float>( height );
	int bestIndex = -1;
	float bestDelta = idMath::INFINITY;
	for ( int i = 0; i < static_cast<int>( sizeof( commonAspectRatios ) / sizeof( commonAspectRatios[0] ) ); ++i ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[i].width ) / static_cast<float>( commonAspectRatios[i].height );
		const float delta = idMath::Fabs( displayAspect - targetAspect );
		if ( delta < bestDelta ) {
			bestDelta = delta;
			bestIndex = i;
		}
	}

	if ( bestIndex >= 0 ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[bestIndex].width ) / static_cast<float>( commonAspectRatios[bestIndex].height );
		if ( bestDelta <= targetAspect * 0.04f ) {
			return va( "%d:%d", commonAspectRatios[bestIndex].width, commonAspectRatios[bestIndex].height );
		}
	}

	const int gcd = MainMenuGreatestCommonDivisor( width, height );
	const int reducedWidth = width / gcd;
	const int reducedHeight = height / gcd;
	if ( reducedWidth <= 64 && reducedHeight <= 64 ) {
		return va( "%d:%d", reducedWidth, reducedHeight );
	}

	return "";
}

static idStr MainMenuDisplayResolutionLabel( int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return "";
	}

	const idStr aspectLabel = MainMenuDisplayAspectRatioLabel( width, height );
	if ( aspectLabel.Length() > 0 ) {
		return va( "%dx%d (%s)", width, height, aspectLabel.c_str() );
	}
	return va( "%dx%d", width, height );
}

static bool MainMenuDisplayModeOptionLess( const mainMenuDisplayModeOption_t &a, const mainMenuDisplayModeOption_t &b ) {
	if ( a.width != b.width ) {
		return a.width < b.width;
	}
	return a.height < b.height;
}

static void AppendMainMenuDisplayModeOption(
	idList<mainMenuDisplayModeOption_t> &options,
	const char *label,
	int width,
	int height,
	int legacyMode,
	bool desktopNative,
	bool custom ) {
	mainMenuDisplayModeOption_t option;
	option.width = width;
	option.height = height;
	option.legacyMode = legacyMode;
	option.desktopNative = desktopNative;
	option.custom = custom;
	option.label = label;
	option.label.Replace( ";", "," );

	options.Append( option );
}

static void InsertMainMenuDisplayModeOptionSorted(
	idList<mainMenuDisplayModeOption_t> &options,
	const char *label,
	int width,
	int height,
	int legacyMode ) {
	mainMenuDisplayModeOption_t option;
	option.width = width;
	option.height = height;
	option.legacyMode = legacyMode;
	option.desktopNative = false;
	option.custom = false;
	option.label = label;
	option.label.Replace( ";", "," );

	int insertIndex = 0;
	while ( insertIndex < options.Num() && MainMenuDisplayModeOptionLess( options[insertIndex], option ) ) {
		insertIndex++;
	}
	options.Insert( option, insertIndex );
}

static void BuildMainMenuDisplayModeChoiceStrings(
	const idList<mainMenuDisplayModeOption_t> &options,
	idStr *choiceNames,
	idStr *choiceValues ) {
	if ( choiceNames == NULL || choiceValues == NULL ) {
		return;
	}

	choiceNames->Clear();
	choiceValues->Clear();
	for ( int i = 0; i < options.Num(); ++i ) {
		AppendMainMenuChoice( *choiceNames, *choiceValues, options[i].label.c_str(), i );
	}
}

static int MainMenuRoundRefreshRate( float refreshRate ) {
	if ( refreshRate <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( refreshRate + 0.5f );
}

#if defined( USE_SDL3 )
static SDL_DisplayID GetMainMenuSelectedSDLDisplay( void ) {
	SDL_DisplayID display = 0;
	const int requestedScreen = cvarSystem->GetCVarInteger( "r_screen" );

	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &displayCount );
	if ( displays != NULL && displayCount > 0 ) {
		if ( requestedScreen >= 0 && requestedScreen < displayCount ) {
			display = displays[requestedScreen];
		}
		if ( display == 0 ) {
			display = SDL_GetPrimaryDisplay();
		}
		if ( display == 0 ) {
			display = displays[0];
		}
	}
	if ( displays != NULL ) {
		SDL_free( displays );
	}

	if ( display == 0 ) {
		display = SDL_GetPrimaryDisplay();
	}
	return display;
}
#endif

static bool MainMenuGetSelectedDisplayDesktopResolution( int *width, int *height ) {
	if ( width == NULL || height == NULL ) {
		return false;
	}

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
	if ( display != 0 ) {
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( display );
		if ( desktopMode != NULL && desktopMode->w > 0 && desktopMode->h > 0 ) {
			*width = desktopMode->w;
			*height = desktopMode->h;
			return true;
		}
	}
#endif

	return Sys_GetDesktopResolution( width, height );
}

static void BuildMainMenuDisplayModeChoices(
	idList<mainMenuDisplayModeOption_t> &options,
	idStr *choiceNames,
	idStr *choiceValues ) {
	options.Clear();
	if ( choiceNames != NULL ) {
		choiceNames->Clear();
	}
	if ( choiceValues != NULL ) {
		choiceValues->Clear();
	}

	int desktopWidth = 0;
	int desktopHeight = 0;
	idStr desktopLabel = common->GetLanguageDict()->GetString( "#str_229973" );

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
#endif

	if ( MainMenuGetSelectedDisplayDesktopResolution( &desktopWidth, &desktopHeight ) && desktopWidth > 0 && desktopHeight > 0 ) {
		desktopLabel = va( "%s %dx%d", desktopLabel.c_str(), desktopWidth, desktopHeight );
	}
	AppendMainMenuDisplayModeOption( options, desktopLabel.c_str(), desktopWidth, desktopHeight, -2, true, false );

	idList<mainMenuDisplayModeOption_t> displayModes;

#if defined( USE_SDL3 )
	if ( display != 0 ) {
		int modeCount = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &modeCount );
		if ( modes != NULL && modeCount > 0 ) {
			for ( int i = 0; i < modeCount; ++i ) {
				const SDL_DisplayMode *mode = modes[i];
				if ( mode == NULL || mode->w < 320 || mode->h < 240 ) {
					continue;
				}
				if ( MainMenuDisplayModeResolutionExists( displayModes, mode->w, mode->h ) ) {
					continue;
				}

				const mainMenuVidMode_t *legacyMode = FindMainMenuVidModeByResolution( mode->w, mode->h );
				InsertMainMenuDisplayModeOptionSorted(
					displayModes,
					MainMenuDisplayResolutionLabel( mode->w, mode->h ).c_str(),
					mode->w,
					mode->h,
					legacyMode != NULL ? legacyMode->mode : -1 );
			}
		}
		if ( modes != NULL ) {
			SDL_free( modes );
		}
	}
#endif

	for ( int i = 0; i < displayModes.Num(); ++i ) {
		options.Append( displayModes[i] );
	}

	const int customWidth = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
	const int customHeight = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
	idStr customLabel = va( "%s %dx%d", common->GetLanguageDict()->GetString( "#str_229974" ), customWidth, customHeight );
	AppendMainMenuDisplayModeOption( options, customLabel.c_str(), customWidth, customHeight, -1, false, true );

	BuildMainMenuDisplayModeChoiceStrings( options, choiceNames, choiceValues );
}

static int FindMainMenuDisplayModeChoice( const idList<mainMenuDisplayModeOption_t> &options ) {
	const int currentMode = cvarSystem->GetCVarInteger( "r_mode" );
	int targetWidth = 0;
	int targetHeight = 0;
	bool preferCustom = false;

	if ( currentMode == -2 ) {
		for ( int i = 0; i < options.Num(); ++i ) {
			if ( options[i].desktopNative ) {
				return i;
			}
		}
		return 0;
	}

	if ( currentMode == -1 ) {
		targetWidth = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
		targetHeight = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
		preferCustom = true;
	} else {
		const mainMenuVidMode_t *mode = FindMainMenuVidModeByMode( currentMode );
		if ( mode != NULL ) {
			targetWidth = mode->width;
			targetHeight = mode->height;
		}
	}

	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return 0;
	}

	for ( int i = 0; i < options.Num(); ++i ) {
		if ( !options[i].desktopNative && !options[i].custom && options[i].width == targetWidth && options[i].height == targetHeight ) {
			return i;
		}
	}

	if ( preferCustom ) {
		for ( int i = 0; i < options.Num(); ++i ) {
			if ( options[i].custom ) {
				return i;
			}
		}
	}

	return 0;
}

static bool MainMenuRefreshRateExists( const idList<int> &refreshRates, int refreshRate ) {
	for ( int i = 0; i < refreshRates.Num(); ++i ) {
		if ( refreshRates[i] == refreshRate ) {
			return true;
		}
	}
	return false;
}

static void AddMainMenuRefreshRate( idList<int> &refreshRates, int refreshRate ) {
	if ( refreshRate <= 0 || MainMenuRefreshRateExists( refreshRates, refreshRate ) ) {
		return;
	}

	int insertIndex = 1; // keep Auto at index 0
	while ( insertIndex < refreshRates.Num() && refreshRates[insertIndex] < refreshRate ) {
		insertIndex++;
	}
	refreshRates.Insert( refreshRate, insertIndex );
}

static bool MainMenuGetRequestedFullscreenResolution( int &width, int &height ) {
	width = 0;
	height = 0;

	const int currentMode = cvarSystem->GetCVarInteger( "r_mode" );
	if ( currentMode == -2 ) {
		return MainMenuGetSelectedDisplayDesktopResolution( &width, &height ) && width > 0 && height > 0;
	}

	if ( currentMode == -1 ) {
		width = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
		height = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
		return true;
	}

	const mainMenuVidMode_t *mode = FindMainMenuVidModeByMode( currentMode );
	if ( mode == NULL ) {
		return false;
	}

	width = mode->width;
	height = mode->height;
	return true;
}

static void BuildMainMenuRefreshChoices( idStr &choiceNames, idStr &choiceValues ) {
	idList<int> refreshRates;
	refreshRates.Append( 0 );

	int targetWidth = 0;
	int targetHeight = 0;
	const bool haveTargetResolution = MainMenuGetRequestedFullscreenResolution( targetWidth, targetHeight );

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
	if ( display != 0 ) {
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( display );
		if ( haveTargetResolution && desktopMode != NULL && desktopMode->w == targetWidth && desktopMode->h == targetHeight ) {
			AddMainMenuRefreshRate( refreshRates, MainMenuRoundRefreshRate( desktopMode->refresh_rate ) );
		}

		int modeCount = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &modeCount );
		if ( modes != NULL && modeCount > 0 ) {
			for ( int i = 0; i < modeCount; ++i ) {
				const SDL_DisplayMode *mode = modes[i];
				if ( mode == NULL ) {
					continue;
				}
				if ( !haveTargetResolution || mode->w != targetWidth || mode->h != targetHeight ) {
					continue;
				}
				AddMainMenuRefreshRate( refreshRates, MainMenuRoundRefreshRate( mode->refresh_rate ) );
			}
		}
		if ( modes != NULL ) {
			SDL_free( modes );
		}
	}
#endif

	choiceNames.Clear();
	choiceValues.Clear();
	for ( int i = 0; i < refreshRates.Num(); ++i ) {
		const int refreshRate = refreshRates[i];
		const char *label = refreshRate == 0
			? common->GetLanguageDict()->GetString( "#str_229914" )
			: va( "%d Hz", refreshRate );
		AppendMainMenuChoice( choiceNames, choiceValues, label, refreshRate );
	}
}

static void SetMainMenuVideoGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	idList<mainMenuDisplayModeOption_t> displayModeOptions;
	BuildMainMenuDisplayModeChoices( displayModeOptions, &choiceNames, &choiceValues );

	gui->SetStateString( "display_mode_choices", choiceNames.c_str() );
	gui->SetStateString( "display_mode_values", choiceValues.c_str() );
	gui->SetStateInt( "display_mode_choice", FindMainMenuDisplayModeChoice( displayModeOptions ) );

	// Compatibility aliases for the original aspect-bucketed GUI widgets.
	gui->SetStateString( "4_3_choices", choiceNames.c_str() );
	gui->SetStateString( "4_3_values", choiceValues.c_str() );
	gui->SetStateString( "16_9_choices", choiceNames.c_str() );
	gui->SetStateString( "16_9_values", choiceValues.c_str() );
	gui->SetStateString( "16_10_choices", choiceNames.c_str() );
	gui->SetStateString( "16_10_values", choiceValues.c_str() );

	BuildMainMenuRefreshChoices( choiceNames, choiceValues );
	gui->SetStateString( "display_refresh_choices", choiceNames.c_str() );
	gui->SetStateString( "display_refresh_values", choiceValues.c_str() );
}

static void SetMainMenuQualityGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	gui->SetStateInt( "r_specularEnabled", cvarSystem->GetCVarBool( "r_skipSpecular" ) ? 0 : 1 );
	gui->SetStateInt( "r_bumpEnabled", cvarSystem->GetCVarBool( "r_skipBump" ) ? 0 : 1 );
	gui->SetStateInt( "r_skyEnabled", cvarSystem->GetCVarBool( "r_skipSky" ) ? 0 : 1 );
	// Apple GL 2.1 compatibility state: while ARB2 light interactions are
	// bypassed the shadow/specular/bump toggles have no visible effect, and
	// SMAA post-AA needs GLSL 1.30; the settings GUI greys those rows.
	const glconfig_t &rendererConfig = renderSystem->GetGLConfig();
	gui->SetStateInt( "r_interactionsBypassed", rendererConfig.disableARB2Interactions ? 1 : 0 );
	gui->SetStateInt( "r_postAAAvailable", ( rendererConfig.GLSLProgramAvailable && rendererConfig.GLSL130Available ) ? 1 : 0 );
}

static void SyncMainMenuAspectVisibility( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	gui->HandleNamedEvent( "forceAspect0" );
}

static void PopulateMainMenuAudioDeviceChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	BuildMainMenuAudioDeviceChoices( choiceNames, choiceValues );
	gui->SetStateString( "device_name", choiceNames.c_str() );
	gui->SetStateString( "device_value", choiceValues.c_str() );
}

static void PopulateMainMenuDisplayChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	int displayCount = 0;
	BuildMainMenuDisplayChoices( choiceNames, choiceValues, displayCount );
	gui->SetStateString( "display_names", choiceNames.c_str() );
	gui->SetStateString( "display_values", choiceValues.c_str() );
	gui->SetStateInt( "display_count", displayCount );
	SetMainMenuVideoGuiVars( gui );
}

static void RefreshMainMenuDisplayChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	SetMainMenuVideoGuiVars( gui );
	SyncMainMenuAspectVisibility( gui );
	gui->StateChanged( common->GetPresentationTime() );
}

static void RefreshMainMenuPerformancePresetGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	SetMainMenuVideoGuiVars( gui );
	SetMainMenuQualityGuiVars( gui );
	SyncMainMenuAspectVisibility( gui );
	gui->StateChanged( common->GetPresentationTime() );
}

static void AppendMainMenuCommandArgsUntilSeparator( const idCmdArgs &sourceArgs, int &argIndex, idCmdArgs &commandArgs ) {
	while ( argIndex < sourceArgs.Argc() && idStr::Cmp( sourceArgs.Argv( argIndex ), ";" ) != 0 ) {
		commandArgs.AppendArg( sourceArgs.Argv( argIndex++ ) );
	}
}

static void ApplyMainMenuDisplayModeChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idList<mainMenuDisplayModeOption_t> displayModeOptions;
	BuildMainMenuDisplayModeChoices( displayModeOptions, NULL, NULL );
	if ( displayModeOptions.Num() <= 0 ) {
		return;
	}

	int choice = gui->State().GetInt( "display_mode_choice" );
	choice = idMath::ClampInt( 0, displayModeOptions.Num() - 1, choice );
	const mainMenuDisplayModeOption_t &option = displayModeOptions[choice];

	if ( option.desktopNative ) {
		cvarSystem->SetCVarInteger( "r_mode", -2 );
	} else {
		cvarSystem->SetCVarInteger( "r_customWidth", idMath::ClampInt( 320, 16384, option.width ) );
		cvarSystem->SetCVarInteger( "r_customHeight", idMath::ClampInt( 240, 16384, option.height ) );
		cvarSystem->SetCVarInteger( "r_mode", ( !option.custom && option.legacyMode >= 0 ) ? option.legacyMode : -1 );
	}

	RefreshMainMenuDisplayChoices( gui );
}

static void ApplyMainMenuCustomDisplaySize( idUserInterface *gui ) {
	cvarSystem->SetCVarInteger( "r_mode", -1 );
	RefreshMainMenuDisplayChoices( gui );
}

/*
=================
MapSupportsStartServerGameType
=================
*/
static bool MapSupportsStartServerGameType( const idDict *dict, const char *gameType ) {
	if ( !dict || !gameType || !gameType[0] ) {
		return false;
	}

	// Match the multiplayer vote/admin behavior so DM and Team DM can use any standard MP map type.
	if ( !idStr::Icmp( gameType, "DM" ) || !idStr::Icmp( gameType, "Team DM" ) ) {
		return dict->GetBool( "DM" ) ||
			dict->GetBool( "Team DM" ) ||
			dict->GetBool( "CTF" ) ||
			dict->GetBool( "Tourney" ) ||
			dict->GetBool( "Arena CTF" );
	}
	if ( !idStr::Icmp( gameType, "Duel" ) ) {
		return dict->GetBool( "DM" );
	}
	if ( !idStr::Icmp( gameType, "Clan Arena" ) ||
		!idStr::Icmp( gameType, "Freeze Tag" ) ||
		!idStr::Icmp( gameType, "Red Rover" ) ) {
		return dict->GetBool( "Team DM" );
	}
	if ( !idStr::Icmp( gameType, "One Flag CTF" ) ) {
		return dict->GetBool( "CTF" );
	}
	if ( !idStr::Icmp( gameType, "Arena One Flag CTF" ) ) {
		return dict->GetBool( "Arena CTF" );
	}

	return dict->GetBool( gameType );
}

/*
=================
MapSupportsAnyMPGameType
=================
*/
static bool MapSupportsAnyMPGameType( const idDict *dict ) {
	if ( !dict ) {
		return false;
	}

	return dict->GetBool( "DM" ) ||
		dict->GetBool( "Team DM" ) ||
		dict->GetBool( "CTF" ) ||
		dict->GetBool( "Tourney" ) ||
		dict->GetBool( "Arena CTF" ) ||
		dict->GetBool( "DeadZone" );
}

/*
=================
GetListenServerPlayerWarningLimit
=================
*/
static int GetListenServerPlayerWarningLimit( const int serverRatePreset, const int maxClientRate ) {
	switch ( serverRatePreset ) {
		case 1:
			// Low Upload
			return 3;
		case 2:
			// Balanced Upload
			return 4;
		case 3:
			// High Upload
			return 5;
		case 4:
		case 5:
			// Fiber/Modern and LAN presets should not trigger the legacy 4-player listen warning.
			return 16;
		default:
			break;
	}

	// Manual mode: infer a safe listen target from net_serverMaxClientRate.
	if ( maxClientRate <= 8000 ) {
		return 3;
	}
	if ( maxClientRate <= 9500 ) {
		return 4;
	}
	if ( maxClientRate <= 10500 ) {
		return 5;
	}
	return 16;
}

/*
=================
CommitStartServerMapSelection
=================
*/
static void CommitStartServerMapSelection( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	if ( sessLocal.guiMainMenu_MapList == NULL ) {
		return;
	}

	int mapNum = sessLocal.guiMainMenu_MapList->GetSelection( NULL, 0 );
	if ( mapNum < 0 && sessLocal.guiMainMenu_MapList->Num() > 0 ) {
		sessLocal.guiMainMenu_MapList->SetSelection( 0 );
		mapNum = sessLocal.guiMainMenu_MapList->GetSelection( NULL, 0 );
	}
	if ( mapNum < 0 ) {
		return;
	}

	const idDict *dict = fileSystem->GetMapDecl( mapNum );
	if ( dict ) {
		cvarSystem->SetCVarString( "si_map", dict->GetString( "path" ) );
	}
}

/*
=================
Main-menu MP model helpers
=================
*/
static const char *mainMenuMPTeamNames[] = {
	"marine",
	"strogg"
};

static bool MainMenuIsTeamGame( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	return !idStr::Icmp( gameType, "Team DM" ) ||
		!idStr::Icmp( gameType, "CTF" ) ||
		!idStr::Icmp( gameType, "One Flag CTF" ) ||
		!idStr::Icmp( gameType, "Arena CTF" ) ||
		!idStr::Icmp( gameType, "Arena One Flag CTF" ) ||
		!idStr::Icmp( gameType, "Clan Arena" ) ||
		!idStr::Icmp( gameType, "Freeze Tag" ) ||
		!idStr::Icmp( gameType, "Red Rover" ) ||
		!idStr::Icmp( gameType, "DeadZone" );
}

static int MainMenuResolveModelTeam( void ) {
	if ( !MainMenuIsTeamGame() ) {
		return -1;
	}

	const char *uiTeam = cvarSystem->GetCVarString( "ui_team" );
	if ( !idStr::Icmp( uiTeam, "Strogg" ) ) {
		return 1;
	}

	// The retail MP settings flow always has a valid active team in team games.
	return 0;
}

static idStr MainMenuModelCVarName( const int menuModelTeam ) {
	if ( menuModelTeam >= 0 && menuModelTeam < 2 ) {
		return va( "ui_model_%s", mainMenuMPTeamNames[ menuModelTeam ] );
	}
	return "ui_model";
}

static int MainMenuMPSettingsGameTypeState( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	if ( gameType == NULL || gameType[0] == '\0' || !idStr::Icmp( gameType, "singleplayer" ) || !idStr::Icmp( gameType, "DM" ) ) {
		return 1;
	}
	if ( !idStr::Icmp( gameType, "Tourney" ) ) {
		return 2;
	}
	if ( !idStr::Icmp( gameType, "Team DM" ) ) {
		return 3;
	}
	if ( !idStr::Icmp( gameType, "CTF" ) ) {
		return 4;
	}
	if ( !idStr::Icmp( gameType, "One Flag CTF" ) ) {
		return 5;
	}
	if ( !idStr::Icmp( gameType, "Arena CTF" ) ) {
		return 6;
	}
	if ( !idStr::Icmp( gameType, "Arena One Flag CTF" ) ) {
		return 7;
	}
	if ( !idStr::Icmp( gameType, "DeadZone" ) ) {
		return 8;
	}
	if ( !idStr::Icmp( gameType, "Duel" ) ) {
		return 9;
	}
	if ( !idStr::Icmp( gameType, "Clan Arena" ) ) {
		return 10;
	}
	if ( !idStr::Icmp( gameType, "Freeze Tag" ) ) {
		return 11;
	}
	if ( !idStr::Icmp( gameType, "Red Rover" ) ) {
		return 12;
	}
	return 1;
}

static void MainMenuSyncMPSettingsGuiState( idUserInterface *gui, const int menuModelTeam ) {
	if ( !gui ) {
		return;
	}

	gui->SetStateInt( "gametype", MainMenuMPSettingsGameTypeState() );
	gui->SetStateInt( "player_team", menuModelTeam >= 0 ? menuModelTeam : 0 );
}

static bool MainMenuExtractPlayerModelInfo( const char *declName, idStr &modelOut, idStr &uiHeadOut, idStr &skinOut, idStr &descriptionOut, idStr &teamOut ) {
	modelOut.Clear();
	uiHeadOut.Clear();
	skinOut.Clear();
	descriptionOut.Clear();
	teamOut.Clear();

	if ( !declName || !declName[0] ) {
		return false;
	}

	const rvDeclPlayerModel *playerModel = static_cast<const rvDeclPlayerModel *>( declManager->FindType( DECL_PLAYER_MODEL, declName, false ) );
	if ( playerModel ) {
		modelOut = playerModel->model;
		uiHeadOut = playerModel->uiHead;
		skinOut = playerModel->skin;
		descriptionOut = playerModel->description;
		teamOut = playerModel->team;
		return true;
	}

	// Fallback for content variants that still define these as entityDef blocks.
	const idDeclEntityDef *entityModel = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, declName, false ) );
	if ( !entityModel ) {
		return false;
	}

	modelOut = entityModel->dict.GetString( "model" );
	uiHeadOut = entityModel->dict.GetString( "def_head_ui" );
	if ( !uiHeadOut.Length() ) {
		uiHeadOut = entityModel->dict.GetString( "def_head" );
	}
	skinOut = entityModel->dict.GetString( "skin" );
	descriptionOut = entityModel->dict.GetString( "description" );
	teamOut = entityModel->dict.GetString( "team" );
	return modelOut.Length() > 0 || uiHeadOut.Length() > 0 || skinOut.Length() > 0;
}

static bool MainMenuModelAllowedForTeam( const idStr &modelTeam, const bool isTeamGame, const int menuModelTeam ) {
	if ( !isTeamGame || menuModelTeam < 0 || menuModelTeam >= 2 ) {
		return true;
	}
	if ( !modelTeam.Length() ) {
		return true;
	}

	return idStr::Icmp( modelTeam.c_str(), mainMenuMPTeamNames[ menuModelTeam ] ) == 0;
}

static void MainMenuApplyPlayerModelPreview( idUserInterface *gui, const idStr &modelName, const idStr &headName, const idStr &skinName ) {
	if ( !gui ) {
		return;
	}

	gui->SetStateString( "player_model_name", modelName.c_str() );
	gui->SetStateString( "player_head_model_name", headName.c_str() );
	gui->SetStateString( "player_skin_name", skinName.c_str() );
	gui->SetStateString( "player_head_skin_name", "" );

	if ( headName.Length() ) {
		const idDeclEntityDef *head = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, headName.c_str(), false ) );
		if ( head && head->dict.GetString( "skin" ) ) {
			gui->SetStateString( "player_head_skin_name", head->dict.GetString( "skin" ) );
		}
	}

	gui->SetStateBool( "need_update", true );
}

static bool MainMenuFirstModelFromList( const idStr &buildValues, const bool isTeamGame, const int menuModelTeam, idStr &modelDeclOut, idStr &modelOut, idStr &headOut, idStr &skinOut ) {
	idStr remaining = buildValues;

	while ( remaining.Length() ) {
		idStr token = remaining;
		const int split = remaining.Find( ";" );
		if ( split >= 0 ) {
			token = remaining.Left( split );
			remaining = remaining.Right( remaining.Length() - split - 1 );
		} else {
			remaining.Clear();
		}

		token.StripLeading( ' ' );
		token.StripTrailing( ' ' );
		if ( !token.Length() ) {
			continue;
		}

		idStr description;
		idStr team;
		if ( !MainMenuExtractPlayerModelInfo( token.c_str(), modelOut, headOut, skinOut, description, team ) ) {
			continue;
		}
		if ( !MainMenuModelAllowedForTeam( team, isTeamGame, menuModelTeam ) ) {
			continue;
		}

		modelDeclOut = token;
		return true;
	}

	return false;
}

static void MainMenuAppendModelChoice( idStr &buildValues, idStr &buildNames, const char *declName, const char *displayName ) {
	if ( buildValues.Length() ) {
		buildValues += ";";
		buildNames += ";";
	}

	buildValues += declName;
	buildNames += ( displayName && displayName[0] ) ? displayName : declName;
}

static void MainMenuBuildModelList( const bool isTeamGame, const int menuModelTeam, idStr &buildValues, idStr &buildNames ) {
	buildValues.Clear();
	buildNames.Clear();

	const int numModels = declManager->GetNumDecls( DECL_PLAYER_MODEL );
	for ( int i = 0; i < numModels; ++i ) {
		const rvDeclPlayerModel *playerModel = static_cast<const rvDeclPlayerModel *>( declManager->DeclByIndex( DECL_PLAYER_MODEL, i, true ) );
		if ( !playerModel ) {
			continue;
		}

		const char *declName = playerModel->GetName();
		if ( !declName || !declName[0] ) {
			continue;
		}

		if ( !MainMenuModelAllowedForTeam( playerModel->team, isTeamGame, menuModelTeam ) ) {
			continue;
		}

		const char *localizedName = playerModel->description.Length() ? common->GetLocalizedString( playerModel->description.c_str() ) : "";
		MainMenuAppendModelChoice( buildValues, buildNames, declName, localizedName );
	}
}

static void SetMainMenuMPModelVars( idUserInterface *gui ) {
	if ( !gui ) {
		return;
	}

	const bool isTeamGame = MainMenuIsTeamGame();
	const int menuModelTeam = MainMenuResolveModelTeam();
	MainMenuSyncMPSettingsGuiState( gui, menuModelTeam );

	const idDeclEntityDef *menuDef = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, "player_marine_mp_ui", false ) );
	const idDeclEntityDef *fallbackDef = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, "player_marine_mp", false ) );
	const idDeclEntityDef *def = menuDef ? menuDef : fallbackDef;

	idStr buildValues;
	idStr buildNames;
	MainMenuBuildModelList( isTeamGame, menuModelTeam, buildValues, buildNames );

	gui->SetStateString( "model_values", buildValues.c_str() );
	gui->SetStateString( "model_names", buildNames.c_str() );
	gui->SetStateBool( "player_model_updated", true );

	const idStr modelCVar = MainMenuModelCVarName( menuModelTeam );
	const char *selectedModelDecl = cvarSystem->GetCVarString( modelCVar.c_str() );
	if ( ( !selectedModelDecl || !selectedModelDecl[0] ) && def ) {
		if ( menuModelTeam >= 0 && menuModelTeam < 2 ) {
			selectedModelDecl = def->dict.GetString( va( "def_default_model_%s", mainMenuMPTeamNames[ menuModelTeam ] ) );
		} else {
			selectedModelDecl = def->dict.GetString( "def_default_model" );
		}
	}

	idStr selectedDecl = selectedModelDecl ? selectedModelDecl : "";
	idStr selectedModelName;
	idStr selectedHeadName;
	idStr selectedSkinName;
	idStr selectedDescription;
	idStr selectedTeam;
	const bool selectedValid = selectedDecl.Length() &&
		MainMenuExtractPlayerModelInfo( selectedDecl.c_str(), selectedModelName, selectedHeadName, selectedSkinName, selectedDescription, selectedTeam ) &&
		MainMenuModelAllowedForTeam( selectedTeam, isTeamGame, menuModelTeam );

	if ( !selectedValid ) {
		if ( MainMenuFirstModelFromList( buildValues, isTeamGame, menuModelTeam, selectedDecl, selectedModelName, selectedHeadName, selectedSkinName ) ) {
			cvarSystem->SetCVarString( modelCVar.c_str(), selectedDecl.c_str() );
		}
	}

	if ( !selectedModelName.Length() && def ) {
		selectedModelName = def->dict.GetString( "model" );
		selectedHeadName = def->dict.GetString( "def_head_ui" );
		if ( !selectedHeadName.Length() ) {
			selectedHeadName = def->dict.GetString( "def_head" );
		}
		selectedSkinName = def->dict.GetString( "skin" );
	}

	if ( selectedModelName.Length() || selectedHeadName.Length() || selectedSkinName.Length() ) {
		MainMenuApplyPlayerModelPreview( gui, selectedModelName, selectedHeadName, selectedSkinName );
	}

	// Stage editable user fields while this menu is active.
	cvarSystem->SetCVarString( "gui_ui_name", cvarSystem->GetCVarString( "ui_name" ) );
	cvarSystem->SetCVarString( "gui_ui_clan", cvarSystem->GetCVarString( "ui_clan" ) );
}

static void CommitMainMenuMPSettings( void ) {
	if ( idStr::Cmp( cvarSystem->GetCVarString( "gui_ui_name" ), cvarSystem->GetCVarString( "ui_name" ) ) ) {
		cvarSystem->SetCVarString( "ui_name", cvarSystem->GetCVarString( "gui_ui_name" ) );
	}
	if ( idStr::Cmp( cvarSystem->GetCVarString( "gui_ui_clan" ), cvarSystem->GetCVarString( "ui_clan" ) ) ) {
		cvarSystem->SetCVarString( "ui_clan", cvarSystem->GetCVarString( "gui_ui_clan" ) );
	}
}

// implements the setup for, and commands from, the main menu

/*
==============
idSessionLocal::GetActiveMenu
==============
*/
idUserInterface *idSessionLocal::GetActiveMenu( void ) {
	return guiActive;
}

/*
==============
idSessionLocal::StartMainMenu
==============
*/
void idSessionLocal::StartMenu( bool playIntro ) {
	const bool shouldPlayIntro = playIntro && !com_skipLogoVideos.GetBool();

	if ( guiActive == guiMainMenu ) {
		return;
	}

	if ( readDemo ) {
		// if we're playing a demo, esc kills it
		UnloadMap();
	}

	// pause the game sound world
	if ( sw != NULL && !sw->IsPaused() ) {
		sw->Pause();
	}

	// start playing the menu sounds
	SetPlayingSoundWorld( menuSoundWorld );

	SetGUI( guiMainMenu, NULL );
	guiMainMenu->HandleNamedEvent( shouldPlayIntro ? "playIntro" : "noIntro" );
	menuIntroBlackoutActive = true;
	menuIntroBlackoutAwaitMenuMusic = shouldPlayIntro;
	menuIntroBlackoutFadeStart = -1;
	fallbackMenuStartTime = -1;
	if ( !shouldPlayIntro ) {
		// Ensure menu music always restarts when returning from gameplay.
		guiMainMenu->HandleNamedEvent( "MusicRestart" );
	}


	if(fileSystem->HasD3XP()) {
		guiMainMenu->SetStateString("game_list", common->GetLanguageDict()->GetString( "#str_07202" ));
	} else {
		guiMainMenu->SetStateString("game_list", common->GetLanguageDict()->GetString( "#str_107212" ));
	}

	console->Close();

}

bool idSessionLocal::IsMainMenuIntroPlaying() const {
	return guiActive == guiMainMenu &&
		guiMainMenu != NULL &&
		guiMainMenu->GetStateInt( "desktop::video_check", "0" ) != 0;
}

/*
=================
idSessionLocal::SetGUI
=================
*/
void idSessionLocal::SetGUI( idUserInterface *gui, HandleGuiCommand_t handle ) {
	const char	*cmd;

	guiActive = gui;
	guiHandle = handle;
	if ( guiMsgRestore ) {
		common->DPrintf( "idSessionLocal::SetGUI: cleared an active message box\n" );
		guiMsgRestore = NULL;
	}
	if ( !guiActive ) {
		return;
	}

	if ( guiActive == guiMainMenu ) {
		// Opening ESC must never wait for unbounded filesystem, device, display,
		// or declaration enumeration. Those catalogs are refreshed when their
		// page is opened; the title-screen path still primes them for stock GUIs.
		const bool refreshCatalogs = !mapSpawned;
		if ( refreshCatalogs ) {
			SetSaveGameGuiVars();
		}
		SetMainMenuGuiVars( refreshCatalogs );
	} else if ( guiActive == guiRestartMenu ) {
		SetSaveGameGuiVars();
	}

	sysEvent_t  ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evType = SE_NONE;

	cmd = guiActive->HandleEvent( &ev, common->GetPresentationTime() );
	guiActive->Activate( true, common->GetPresentationTime() );
}

/*
===============
idSessionLocal::ExitMenu
===============
*/
void idSessionLocal::ExitMenu( void ) {
	guiActive = NULL;

	// go back to the game sounds
	SetPlayingSoundWorld( sw );

	// unpause the game sound world
	if ( sw != NULL && sw->IsPaused() ) {
		sw->UnPause();
	}
}

/*
===============
idListSaveGameCompare
===============
*/
ID_INLINE int idListSaveGameCompare( const fileTIME_T *a, const fileTIME_T *b ) {
	return b->timeStamp - a->timeStamp;
}

static const int SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES = 8192;
static const int SESSION_MENU_MAX_SAVEGAME_DICT_KV = 16384;
static const int SESSION_MENU_MAX_SAVEGAME_BASENAME = 96;
static const float SESSION_MENU_SAVE_PREVIEW_X = 25.0f;
static const float SESSION_MENU_SAVE_PREVIEW_Y = 78.0f;
static const float SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH = 183.0f;
static const float SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT = 137.0f;
static const float SESSION_MENU_GUI_ASPECT = 640.0f / 480.0f;

typedef struct sessionMenuSaveDescription_s {
	idStr	saveName;
	idStr	description;
	idStr	screenshot;
	bool	noOverwrite;
} sessionMenuSaveDescription_t;

static idRectangle Session_MenuFitSaveGamePreviewRect( int imageWidth, int imageHeight ) {
	float imageAspect = SESSION_MENU_GUI_ASPECT;
	if ( imageWidth > 0 && imageHeight > 0 ) {
		imageAspect = static_cast<float>( imageWidth ) / static_cast<float>( imageHeight );
	}

	// With aspect correction enabled, authored GUI units have a uniform physical
	// scale.  In retail stretch mode, compensate for the viewport's non-uniform
	// 640x480 mapping so the final on-screen rectangle still matches the image.
	float logicalAspect = imageAspect;
	if ( !cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		float viewportWidth = static_cast<float>( engineWindowState.uiViewportWidth );
		float viewportHeight = static_cast<float>( engineWindowState.uiViewportHeight );
		if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
			viewportWidth = static_cast<float>( engineWindowState.vidWidth );
			viewportHeight = static_cast<float>( engineWindowState.vidHeight );
		}
		if ( viewportWidth > 0.0f && viewportHeight > 0.0f ) {
			const float viewportStretch = ( viewportWidth / viewportHeight ) / SESSION_MENU_GUI_ASPECT;
			if ( viewportStretch > 0.0f ) {
				logicalAspect /= viewportStretch;
			}
		}
	}

	float width = SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH;
	float height = SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT;
	const float availableAspect = width / height;
	if ( logicalAspect > availableAspect ) {
		height = width / logicalAspect;
	} else {
		width = height * logicalAspect;
	}

	return idRectangle(
		SESSION_MENU_SAVE_PREVIEW_X + ( SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH - width ) * 0.5f,
		SESSION_MENU_SAVE_PREVIEW_Y + ( SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT - height ) * 0.5f,
		width,
		height );
}

static void Session_MenuSetSaveGamePreviewRect( idUserInterface *gui, const idMaterial *material ) {
	if ( gui == NULL ) {
		return;
	}

	const int imageWidth = material != NULL ? material->GetImageWidth() : 0;
	const int imageHeight = material != NULL ? material->GetImageHeight() : 0;
	const idRectangle previewRect = Session_MenuFitSaveGamePreviewRect( imageWidth, imageHeight );
	gui->SetStateFloat( "loadgame_preview_x", previewRect.x );
	gui->SetStateFloat( "loadgame_preview_y", previewRect.y );
	gui->SetStateFloat( "loadgame_preview_w", previewRect.w );
	gui->SetStateFloat( "loadgame_preview_h", previewRect.h );
}

static bool Session_MenuIsSafeSaveSlotName( const idStr &slotName ) {
	if ( slotName.IsEmpty() || slotName.Length() > SESSION_MENU_MAX_SAVEGAME_BASENAME ) {
		return false;
	}

	idStr scrubbedName = slotName;
	sessLocal.ScrubSaveGameFileName( scrubbedName );
	return scrubbedName.Icmp( slotName ) == 0;
}

static bool Session_MenuSaveDescriptionMatchesSlot( const idStr &slotName, const idStr &descriptionSaveName ) {
	if ( descriptionSaveName.IsEmpty() ) {
		return false;
	}

	idStr scrubbedName = descriptionSaveName;
	sessLocal.ScrubSaveGameFileName( scrubbedName );
	return scrubbedName.Icmp( slotName ) == 0;
}

static bool Session_MenuIsSafeSaveMaterialPath( const idStr &path ) {
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

static bool Session_MenuReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadInt( value );
	if ( bytesRead != sizeof( value ) ) {
		common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_MenuReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
	int len = 0;
	const int lengthOffset = file->Tell();
	if ( !Session_MenuReadSaveGameInt( file, len, fieldName, savePath ) ) {
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

static bool Session_MenuReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
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

static bool Session_MenuSkipSaveGameDict( idFile *file, const char *fieldName, const char *savePath ) {
	int count = 0;
	if ( !Session_MenuReadSaveGameInt( file, count, fieldName, savePath ) ) {
		return false;
	}
	if ( count < 0 || count > SESSION_MENU_MAX_SAVEGAME_DICT_KV ) {
		common->Warning( "Savegame '%s' has invalid %s key/value count %d (max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "dictionary",
			count,
			SESSION_MENU_MAX_SAVEGAME_DICT_KV );
		return false;
	}

	for ( int i = 0; i < count; i++ ) {
		idStr key;
		idStr value;
		if ( !Session_MenuReadSaveGameCString( file, key, MAX_STRING_CHARS, va( "%s key %d", fieldName ? fieldName : "dictionary", i ), savePath ) ||
			 !Session_MenuReadSaveGameCString( file, value, MAX_STRING_CHARS, va( "%s value %d", fieldName ? fieldName : "dictionary", i ), savePath ) ) {
			return false;
		}
	}

	return true;
}

static bool Session_MenuIsSupportedSaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0 ||
		gameName.Icmp( SAVEGAME_GAME_NAME_LEGACY_OPENQ4 ) == 0;
}

static bool Session_MenuSaveGameHeaderUsesEntityFilter( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0;
}

static bool Session_MenuIsCompatibleSaveGameVersion( const int version ) {
	return version == SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION_ALT;
}

static bool Session_MenuIsLoadableSaveGameSlot( const idStr &slotName ) {
	if ( !Session_MenuIsSafeSaveSlotName( slotName ) ) {
		common->Warning( "Ignoring savegame slot with unsafe filename '%s'", slotName.c_str() );
		return false;
	}

	idStr savePath = va( "savegames/%s.save", slotName.c_str() );
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( savePath.c_str(), true, game.Length() ? game.c_str() : NULL );
	if ( file == NULL ) {
		return false;
	}

	idStr gamename;
	idStr saveMap;
	idStr entityFilter;
	int version = 0;
	bool valid = true;
	valid = valid && Session_MenuReadSaveGameString( file, gamename, MAX_STRING_CHARS, "game name", savePath.c_str() );
	valid = valid && Session_MenuIsSupportedSaveGameName( gamename );
	valid = valid && Session_MenuReadSaveGameInt( file, version, "version", savePath.c_str() );
	valid = valid && Session_MenuIsCompatibleSaveGameVersion( version );
	valid = valid && Session_MenuReadSaveGameString( file, saveMap, MAX_STRING_CHARS, "map name", savePath.c_str() );
	if ( valid && Session_MenuSaveGameHeaderUsesEntityFilter( gamename ) ) {
		valid = Session_MenuReadSaveGameString( file, entityFilter, MAX_STRING_CHARS, "entity filter", savePath.c_str() );
	}
	for ( int i = 0; valid && i < MAX_ASYNC_CLIENTS; i++ ) {
		valid = Session_MenuSkipSaveGameDict( file, va( "persistent player info %d", i ), savePath.c_str() );
	}
	if ( valid && saveMap.IsEmpty() ) {
		common->Warning( "Savegame '%s' has an empty map name", savePath.c_str() );
		valid = false;
	}

	fileSystem->CloseFile( file );
	return valid;
}

static bool Session_MenuReadSaveDescription( const idStr &slotName, sessionMenuSaveDescription_t &description ) {
	description.saveName.Clear();
	description.description.Clear();
	description.screenshot.Clear();
	description.noOverwrite = false;

	if ( !Session_MenuIsSafeSaveSlotName( slotName ) ) {
		common->Warning( "Ignoring save description for unsafe slot '%s'", slotName.c_str() );
		return false;
	}

	idStr descriptionPath = va( "savegames/%s.txt", slotName.c_str() );
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( descriptionPath.c_str(), true, game.Length() ? game.c_str() : NULL );
	if ( file == NULL ) {
		return false;
	}

	const int len = file->Length();
	if ( len < 0 || len > SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES ) {
		common->Warning( "Save description '%s' has invalid size %d (max %d)",
			descriptionPath.c_str(), len, SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES );
		fileSystem->CloseFile( file );
		return false;
	}

	char *buffer = new char[ len + 1 ];
	const int bytesRead = len > 0 ? file->Read( buffer, len ) : 0;
	fileSystem->CloseFile( file );
	if ( bytesRead != len ) {
		common->Warning( "Save description '%s' is truncated (read %d of %d)",
			descriptionPath.c_str(), bytesRead, len );
		delete[] buffer;
		return false;
	}
	buffer[ len ] = '\0';

	bool valid = true;
	{
		idLexer src( buffer, len, descriptionPath.c_str(), LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );
		idToken tok;
		if ( src.ReadToken( &tok ) ) {
			description.saveName = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.description = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.screenshot = tok;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.noOverwrite = !idStr::Icmp( tok.c_str(), "nooverwrite" );
		}
	}

	delete[] buffer;
	if ( valid && !Session_MenuSaveDescriptionMatchesSlot( slotName, description.saveName ) ) {
		common->Warning( "Save description '%s' names slot '%s', expected '%s'",
			descriptionPath.c_str(), description.saveName.c_str(), slotName.c_str() );
		valid = false;
	}
	if ( valid && ( description.saveName.Length() >= MAX_STRING_CHARS || description.description.Length() >= MAX_STRING_CHARS ) ) {
		common->Warning( "Save description '%s' has an oversized display token", descriptionPath.c_str() );
		valid = false;
	}
	if ( valid && !Session_MenuIsSafeSaveMaterialPath( description.screenshot ) ) {
		common->Warning( "Save description '%s' has unsafe screenshot material '%s'",
			descriptionPath.c_str(), description.screenshot.c_str() );
		valid = false;
	}
	return valid;
}

/*
===============
idSessionLocal::GetSaveGameList
===============
*/
void idSessionLocal::GetSaveGameList( idStrList &fileList, idList<fileTIME_T> &fileTimes ) {
	int i;
	idFileList *files;
	idStrList rawFileList;

	// NOTE: no fs_game_base for savegames
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	if( game.Length() ) {
		files = fileSystem->ListFiles( "savegames", ".save", false, false, game );
	} else {
		files = fileSystem->ListFiles( "savegames", ".save" );
	}
	
	rawFileList = files->GetList();
	fileSystem->FreeFileList( files );

	fileList.Clear();
	fileTimes.Clear();

	for ( i = 0; i < rawFileList.Num(); i++ ) {
		ID_TIME_T timeStamp;
		idStr slotName = rawFileList[i];
		slotName.StripLeading( '/' );
		slotName.StripFileExtension();
		if ( slotName.IsEmpty() || !Session_MenuIsLoadableSaveGameSlot( slotName ) ) {
			continue;
		}

		fileSystem->ReadFile( va( "savegames/%s.save", slotName.c_str() ), NULL, &timeStamp );

		fileTIME_T ft;
		ft.index = fileList.Num();
		ft.timeStamp = timeStamp;
		fileList.Append( slotName );
		fileTimes.Append( ft );
	}

	fileTimes.Sort( idListSaveGameCompare );
}

/*
===============
idSessionLocal::SetSaveGameGuiVars
===============
*/
void idSessionLocal::SetSaveGameGuiVars( void ) {
	int i;
	idStr name;
	idStrList fileList;
	idList<fileTIME_T> fileTimes;

	loadGameList.Clear();
	fileList.Clear();
	fileTimes.Clear();

	if ( !guiActive ) {
		return;
	}

	guiActive->SetStateString( "saveGameName", "" );
	guiActive->SetStateString( "saveGameDescription", "" );
	guiActive->SetStateString( "saveGameDate", "" );
	guiActive->SetStateString( "saveGameTime", "" );

	GetSaveGameList( fileList, fileTimes );

	loadGameList.SetNum( fileList.Num() );
	for ( i = 0; i < fileList.Num(); i++ ) {
		loadGameList[i] = fileList[fileTimes[i].index];

		sessionMenuSaveDescription_t description;
		if ( Session_MenuReadSaveDescription( loadGameList[i], description ) && description.description.Length() > 0 ) {
			name = description.description;
		} else {
			name = loadGameList[i];
		}

		guiActive->SetStateString( va("loadgame_item_%i", i), name);
	}
	guiActive->DeleteStateVar( va("loadgame_item_%i", fileList.Num()) );
	guiActive->SetStateInt( "loadgame_listdef_count", fileList.Num() );

	guiActive->SetStateString( "loadgame_sel_0", "-1" );
	guiActive->SetStateString( "loadgame_shot", "gfx/guis/loadscreens/generic" );
	const idMaterial *defaultPreview = declManager->FindMaterial( "gfx/guis/loadscreens/generic" );
	if ( defaultPreview != NULL ) {
		defaultPreview->SetSort( SS_GUI );
	}
	Session_MenuSetSaveGamePreviewRect( guiActive, defaultPreview );

}

/*
===============
idSessionLocal::SetModsMenuGuiVars
===============
*/
void idSessionLocal::SetModsMenuGuiVars( void ) {
	if ( !guiActive ) {
		return;
	}

	idModList *list = fileSystem->ListMods();
	const idStr currentGameDir = cvarSystem->GetCVarString( "fs_game" );
	int selectedIndex = -1;

	modsList.SetNum( list->GetNumMods() );

	// Build the gui list
	for ( int i = 0; i < list->GetNumMods(); i++ ) {
		modsList[ i ] = list->GetInfo( i );
		guiActive->SetStateString( va("modsList_item_%i", i), list->GetDescription( i ) );
		if ( !currentGameDir.Icmp( modsList[ i ].directory ) ) {
			selectedIndex = i;
		}
	}
	guiActive->DeleteStateVar( va("modsList_item_%i", list->GetNumMods()) );
	if ( selectedIndex < 0 && modsList.Num() > 0 ) {
		selectedIndex = 0;
	}
	guiActive->SetStateInt( "modsList_sel_0", selectedIndex );
	UpdateModsMenuGuiVars();

	fileSystem->FreeModList( list );
}

/*
===============
idSessionLocal::UpdateModsMenuGuiVars
===============
*/
void idSessionLocal::UpdateModsMenuGuiVars( void ) {
	if ( !guiActive ) {
		return;
	}

	const int choice = guiActive->State().GetInt( "modsList_sel_0" );
	if ( choice < 0 || choice >= modsList.Num() ) {
		guiActive->SetStateString( "mod_name", "" );
		guiActive->SetStateString( "mod_version", "" );
		guiActive->SetStateString( "mod_release_date", "" );
		guiActive->SetStateString( "mod_author", "" );
		guiActive->SetStateString( "mod_website", "" );
		guiActive->SetStateString( "mod_required_engine_version", "" );
		guiActive->SetStateString( "mod_directory", "" );
		return;
	}

	const idModInfo &modInfo = modsList[ choice ];
	guiActive->SetStateString( "mod_name", modInfo.displayName.c_str() );
	guiActive->SetStateString( "mod_version", modInfo.version.c_str() );
	guiActive->SetStateString( "mod_release_date", modInfo.releaseDate.c_str() );
	guiActive->SetStateString( "mod_author", modInfo.author.c_str() );
	guiActive->SetStateString( "mod_website", modInfo.website.c_str() );
	guiActive->SetStateString( "mod_required_engine_version", modInfo.requiredopenQ4Version.c_str() );
	guiActive->SetStateString( "mod_directory", modInfo.directory.c_str() );
}


/*
===============
idSessionLocal::SetMainMenuSkin
===============
*/
void idSessionLocal::SetMainMenuSkin( void ) {
	// skins
	idStr str = cvarSystem->GetCVarString( "mod_validSkins" );
	idStr uiSkin = cvarSystem->GetCVarString( "ui_skin" );
	idStr skin;
	int skinId = 1;
	int count = 1;
	while ( str.Length() ) {
		int n = str.Find( ";" );
		if ( n >= 0 ) {
			skin = str.Left( n );
			str = str.Right( str.Length() - n - 1 );
		} else {
			skin = str;
			str = "";
		}
		if ( skin.Icmp( uiSkin ) == 0 ) {
			skinId = count;
		}
		count++;
	}

	for ( int i = 0; i < count; i++ ) {
		guiMainMenu->SetStateInt( va( "skin%i", i+1 ), 0 );
	}
	guiMainMenu->SetStateInt( va( "skin%i", skinId ), 1 );
}

/*
===============
idSessionLocal::SetMainMenuGuiVars
===============
*/
void idSessionLocal::SetMainMenuGuiVars( bool refreshCatalogs ) {

	guiMainMenu->SetStateString( "serverlist_sel_0", "-1" );
	guiMainMenu->SetStateString( "serverlist_selid_0", "-1" ); 

	guiMainMenu->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );

	// "inetGame" will hold a hand-typed inet address, which is not archived to a cvar
	guiMainMenu->SetStateString( "inetGame", "" );

	if ( refreshCatalogs ) {
		// These operations may consult drivers, the filesystem, or large decl
		// catalogs. Keep them off the synchronous in-game activation path.
		guiMainMenu->SetKeyBindingNames();
		PopulateMainMenuAudioDeviceChoices( guiMainMenu );
		PopulateMainMenuDisplayChoices( guiMainMenu );
	}

	// flag for in-game menu
	const char *inGameState = mapSpawned ? ( IsMultiplayer() ? "2" : "1" ) : "0";
	guiMainMenu->SetStateString( "inGame", inGameState );
	guiMainMenu->SetStateString( "ingame", inGameState );

#ifdef ID_DEMO_BUILD
	guiMainMenu->SetStateString( "nightmare", "0" );
#else
	guiMainMenu->SetStateString( "nightmare", cvarSystem->GetCVarBool( "g_nightmare" ) ? "1" : "0" );
#endif
	guiMainMenu->SetStateString( "browser_levelshot", "gfx/guis/loadscreens/generic" );
	SetMainMenuQualityGuiVars( guiMainMenu );
	SyncMainMenuAspectVisibility( guiMainMenu );
	guiMainMenu->SetStateInt( "gui_set_sys_scroll", 0 );
	guiMainMenu->SetStateInt( "gui_set_audio_scroll", 0 );
	guiMainMenu->SetStateInt( "gui_set_game_scroll", 0 );
	gui_set_sys_scroll.SetInteger( 0 );
	gui_set_audio_scroll.SetInteger( 0 );
	gui_set_game_scroll.SetInteger( 0 );

	SetMainMenuSkin();
	if ( refreshCatalogs ) {
		SetModsMenuGuiVars();
	}

	guiMsg->SetStateString( "visible_hasxp", fileSystem->HasD3XP() ? "1" : "0" );

#if defined( __linux__ )
	guiMainMenu->SetStateString( "driver_prompt", "1" );
#else
	guiMainMenu->SetStateString( "driver_prompt", "0" );
#endif

	if ( refreshCatalogs ) {
		SetMainMenuMPModelVars( guiMainMenu );
	}
	arenaCampaign.UpdateMainMenuGui( guiMainMenu );
}

/*
==============
idSessionLocal::HandleSaveGameMenuCommands
==============
*/
bool idSessionLocal::HandleSaveGameMenuCommand( idCmdArgs &args, int &icmd ) {

	const char *cmd = args.Argv(icmd-1);

	if ( !idStr::Icmp( cmd, "refreshSaveGameList" ) ) {
		SetSaveGameGuiVars();
		if ( guiActive != NULL ) {
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "loadGame" ) ) {
		int choice = guiActive->State().GetInt("loadgame_sel_0");
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			sessLocal.LoadGame( loadGameList[choice] );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "saveGame" ) ) {
		const char *saveGameName = guiActive->State().GetString("saveGameName");
		if ( saveGameName && saveGameName[0] ) {

			// First see if the file already exists unless they pass '1' to authorize the overwrite
			if ( icmd == args.Argc() || atoi(args.Argv( icmd++ )) == 0 ) {
				idStr saveFileName = saveGameName;
				sessLocal.ScrubSaveGameFileName( saveFileName );
				saveFileName = "savegames/" + saveFileName;
				saveFileName.SetFileExtension(".save");

				idStr game = cvarSystem->GetCVarString( "fs_game" );
				idFile *file;
				if(game.Length()) {
					file = fileSystem->OpenFileRead( saveFileName, true, game );
				} else {
					file = fileSystem->OpenFileRead( saveFileName );
				}
				
				if ( file != NULL ) {
					fileSystem->CloseFile( file );

					// The file exists, see if it's an autosave
					idStr saveSlotName = saveGameName;
					sessLocal.ScrubSaveGameFileName( saveSlotName );
					sessionMenuSaveDescription_t description;
					if ( Session_MenuReadSaveDescription( saveSlotName, description ) ) {
						if ( description.screenshot.Length() > 0 || description.noOverwrite ) {
							// NOTE: base/ gui doesn't handle that one
							guiActive->HandleNamedEvent( "autosaveOverwriteError" );
							return true;
						}
					}
					guiActive->HandleNamedEvent( "saveGameOverwrite" );
					return true;
				}
			}

			sessLocal.SaveGame( saveGameName, ST_REGULAR );
			SetSaveGameGuiVars( );
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "deleteGame" ) ) {
		int choice = guiActive->State().GetInt( "loadgame_sel_0" );
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			DeleteGame( loadGameList[choice] );
			SetSaveGameGuiVars( );
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "updateSaveGameInfo" ) ) {
		int choice = guiActive->State().GetInt( "loadgame_sel_0" );
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			const idMaterial *material;

			idStr saveName, description, screenshot;
			sessionMenuSaveDescription_t saveDescription;
			if ( Session_MenuReadSaveDescription( loadGameList[choice], saveDescription ) ) {
				saveName = saveDescription.saveName;
				description = saveDescription.description;
				screenshot = saveDescription.screenshot;
			} else {
				saveName = loadGameList[choice];
				description = loadGameList[choice];
				screenshot = "";
			}
			if ( screenshot.Length() == 0 ) {
				screenshot = va("savegames/%s.tga", loadGameList[choice].c_str());
			}
			material = declManager->FindMaterial( screenshot );
			if ( material ) {
				material->ReloadImages( false );
				material->SetSort( SS_GUI );
			}
			guiActive->SetStateString( "loadgame_shot",  screenshot );
			Session_MenuSetSaveGamePreviewRect( guiActive, material );

			saveName.RemoveColors();
			guiActive->SetStateString( "saveGameName", saveName );
			guiActive->SetStateString( "saveGameDescription", description );

			ID_TIME_T timeStamp;
			fileSystem->ReadFile( va("savegames/%s.save", loadGameList[choice].c_str()), NULL, &timeStamp );
			idStr date = Sys_TimeStampToStr(timeStamp);
			int tab = date.Find( '\t' );
			idStr time = date.Right( date.Length() - tab - 1);
			guiActive->SetStateString( "saveGameDate", date.Left( tab ) );
			guiActive->SetStateString( "saveGameTime", time );
		}
		return true;
	}

	return false;
}

/*
==============
idSessionLocal::HandleRestartMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleRestartMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int icmd;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );

		if ( HandleSaveGameMenuCommand( args, icmd ) ) {
			continue;
		}

		if ( !idStr::Icmp( cmd, "restart" ) ) {
			idStr loadName;
			if ( lastCheckPoint != -1 ) {
				loadName = va( "Checkpoint%d", lastCheckPoint );
			} else {
				loadName = GetAutoSaveName( mapSpawnData.serverInfo.GetString("si_map") );
			}

			if ( !LoadGame( loadName ) ) {
				// If we can't load the retail restart slot then just restart the map
				MoveToNewMap( mapSpawnData.serverInfo.GetString("si_map") );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "disconnect" ) ) {
			const char *target = args.Argc() - icmd >= 1 ? args.Argv( icmd ) : "";
			if ( target[ 0 ] != '\0' && idStr::Icmp( target, "mainmenu" ) != 0 ) {
				continue;
			}

			if ( !idStr::Icmp( target, "mainmenu" ) ) {
				icmd++;
			}

			Stop();
			StartMenu();

			if ( guiMainMenu && args.Argc() - icmd >= 1 ) {
				idStr mainMenuEvent = args.Argv( icmd++ );
				if ( mainMenuEvent.Length() > 0 ) {
					guiMainMenu->HandleNamedEvent( mainMenuEvent.c_str() );
				}
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "quit" ) ) {
			ExitMenu();
			common->Quit();
			return;
		}

		if ( !idStr::Icmp ( cmd, "exec" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, args.Argv( icmd++ ) );
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv(icmd++);
				sw->PlayShaderDirectly(snd);
			}
			continue;
		}
	}
}

/*
==============
idSessionLocal::HandleIntroMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleIntroMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int i;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( i = 0; i < args.Argc(); ) {
		const char *cmd = args.Argv( i++ );

		if ( !idStr::Icmp( cmd, "startGame" ) ) {
			menuSoundWorld->ClearAllSoundEmitters();
			ExitMenu();
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - i >= 1 ) {
				idStr snd = args.Argv(i++);
				menuSoundWorld->PlayShaderDirectly(snd);
			}
			continue;
		}
	}
}

/*
==============
idSessionLocal::UpdateMPLevelShot
==============
*/
void idSessionLocal::UpdateMPLevelShot( void ) {
	char screenshot[ MAX_STRING_CHARS ];
	fileSystem->FindMapScreenshot( cvarSystem->GetCVarString( "si_map" ), screenshot, MAX_STRING_CHARS );
	guiMainMenu->SetStateString( "current_levelshot", screenshot );
	declManager->FindMaterial( screenshot )->SetSort( SS_GUI );
}

static int MainMenuGetNewGameOption( idUserInterface *gui, const char *desktopStateName, const char *stateName, int defaultValue ) {
	if ( gui == NULL ) {
		return defaultValue;
	}

	idWindow *desktop = gui->GetDesktop();
	if ( desktop != NULL ) {
		idWinVar *winVar = desktopStateName != NULL ? desktop->GetWinVarByName( desktopStateName, true ) : NULL;
		if ( winVar == NULL && stateName != NULL ) {
			winVar = desktop->GetWinVarByName( stateName, false );
		}
		if ( winVar != NULL ) {
			return atoi( winVar->c_str() );
		}
	}

	int value = defaultValue;
	if ( desktopStateName != NULL && gui->State().GetInt( desktopStateName, va( "%d", defaultValue ), value ) ) {
		return value;
	}
	if ( stateName != NULL && gui->State().GetInt( stateName, va( "%d", defaultValue ), value ) ) {
		return value;
	}

	return defaultValue;
}

static void MainMenuApplyNewGameOptions( idUserInterface *gui ) {
	const int skill = idMath::ClampInt( 0, 4, MainMenuGetNewGameOption( gui, "desktop::skill", "skill", cvarSystem->GetCVarInteger( "g_skill" ) ) );
	const int turboMode = MainMenuGetNewGameOption( gui, "desktop::turboMode", "turboMode", cvarSystem->GetCVarInteger( "g_turboMode" ) ) != 0 ? 1 : 0;

	cvarSystem->SetCVarInteger( "g_skill", skill );
	cvarSystem->SetCVarInteger( "g_turboMode", turboMode );
	common->DPrintf( "Main menu new game options: g_skill=%d g_turboMode=%d\n", skill, turboMode );
}

/*
==============
idSessionLocal::HandleMainMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleMainMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int icmd;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );

		if ( HandleSaveGameMenuCommand( args, icmd ) ) {
			continue;
		}

		if ( !idStr::Cmp( cmd, ";" ) ) {
			continue;
		}

		if ( !idStr::Icmp( cmd, "demoOpen" ) ) {
			OpenDemoMenu( true );
			return;
		}

		if ( !idStr::Icmp( cmd, "singlePlayerOpen" ) ) {
			arenaCampaign.OpenSelector();
			return;
		}

		if ( !idStr::Icmp( cmd, "reloadLanguage" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadLanguage\n" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "reloadGuis" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadGuis\n" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "CVarStrcmp" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			if ( args.Argc() - icmd >= 2 && gui != NULL ) {
				const char *cvarName = args.Argv( icmd++ );
				const char *stateName = args.Argv( icmd++ );
				const char *value = cvarSystem->GetCVarString( cvarName );
				int matchIndex = -1;
				int optionIndex = 0;
				while ( icmd < args.Argc() ) {
					const char *candidate = args.Argv( icmd++ );
					if ( strstr( candidate, ";" ) != NULL ) {
						break;
					}
					if ( !idStr::Icmp( candidate, value ) ) {
						matchIndex = optionIndex;
					}
					optionIndex++;
				}
				gui->SetStateInt( stateName, matchIndex );
			}
			continue;
		}

		// always let the game know the command is being run
		if ( game ) {
			game->HandleMainMenuCommands( cmd, guiActive );
		}

		if ( !idStr::Icmp( cmd, "startMap" ) ) {
			MainMenuApplyNewGameOptions( guiMainMenu );
			if ( icmd < args.Argc() ) {
				StartNewGame( args.Argv( icmd++ ) );
			} else {
#ifndef ID_DEMO_BUILD
				StartNewGame( "game/mars_city1" );
#else
				StartNewGame( "game/demo_mars_city1" );
#endif
			}
			if ( guiActive == guiMainMenu ) {
				ExitMenu();
			}
			continue;
		}
		
		if ( !idStr::Icmp( cmd, "startGame" ) ) {
			MainMenuApplyNewGameOptions( guiMainMenu );
			if ( icmd < args.Argc() ) {
				StartNewGame( args.Argv( icmd++ ) );
			} else {
#ifndef ID_DEMO_BUILD
				StartNewGame( "game/mars_city1" );
#else
				StartNewGame( "game/demo_mars_city1" );
#endif
			}
			// need to do this here to make sure presentation time is current or the gui activates with a time that 
			// is "however long map load took" time in the past
			common->GUIFrame( false, false );
			SetGUI( guiIntro, NULL );
			guiIntro->StateChanged( common->GetPresentationTime(), true );
			// stop playing the game sounds
			SetPlayingSoundWorld( menuSoundWorld );

			continue;
		}

		if ( !idStr::Icmp( cmd, "quit" ) ) {
			ExitMenu();
			common->Quit();
			return;
		}

		if ( !idStr::Icmp( cmd, "loadMod" ) ) {
			int choice = guiActive->State().GetInt( "modsList_sel_0" );
			if ( choice >= 0 && choice < modsList.Num() ) {
				cvarSystem->SetCVarString( "fs_game", modsList[ choice ].directory.c_str() );
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reloadEngine menu\n" );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "updateModInfo" ) ) {
			UpdateModsMenuGuiVars();
			guiActive->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "InitServerBrowser" ) ) {
			idAsyncNetwork::client.serverList.GUIInit();
			continue;
		}

		if ( !idStr::Icmp( cmd, "updateFilterByMod" ) ||
			!idStr::Icmp( cmd, "filterByPrevMod" ) || !idStr::Icmp( cmd, "filterByNextMod" ) ) {
			const int direction = !idStr::Icmp( cmd, "filterByPrevMod" ) ? -1 : !idStr::Icmp( cmd, "filterByNextMod" ) ? 1 : 0;
			idAsyncNetwork::client.serverList.UpdateFilterByMod( direction );
			continue;
		}

		if ( !idStr::Icmp( cmd, "toggleFavorite" ) ) {
			idAsyncNetwork::client.serverList.ToggleFavorite();
			continue;
		}

		if ( !idStr::Icmp( cmd, "server_clearSort" ) ) {
			idAsyncNetwork::client.serverList.ResetSorting();
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortFavorite" ) || !idStr::Icmp( cmd, "sortLocked" ) ||
			!idStr::Icmp( cmd, "sortDed" ) || !idStr::Icmp( cmd, "sortPB" ) || !idStr::Icmp( cmd, "sortRepeater" ) ) {
			const serverSort_t sort = !idStr::Icmp( cmd, "sortFavorite" ) ? SORT_FAVORITE :
				!idStr::Icmp( cmd, "sortLocked" ) ? SORT_PASSWORD : !idStr::Icmp( cmd, "sortDed" ) ? SORT_DEDICATED :
				!idStr::Icmp( cmd, "sortPB" ) ? SORT_PUNKBUSTER : SORT_REPEATER;
			idAsyncNetwork::client.serverList.SetSorting( sort );
			continue;
		}

		if ( !idStr::Icmp( cmd, "UpdateServers" ) ) {
			idAsyncNetwork::client.serverList.GUIInit();
			if ( guiActive->State().GetBool( "lanSet" ) ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "LANScan" );
			} else {
				idAsyncNetwork::GetNETServers();
				idAsyncNetwork::client.serverList.AddFavoriteServers();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "RefreshServers" ) ) {
			if ( guiActive->State().GetBool( "lanSet" ) ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "LANScan" );
			} else {
				idAsyncNetwork::client.serverList.NetScan( );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "FilterServers" ) ) {
			idAsyncNetwork::client.serverList.ApplyFilter( );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortServerName" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_SERVERNAME );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortGame" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_GAME );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortPlayers" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_PLAYERS );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortPing" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_PING );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortGameType" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_GAMETYPE );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortMap" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_MAP );
			continue;
		}

		if ( !idStr::Icmp( cmd, "serverList" ) || !idStr::Icmp( cmd, "click_serverList" ) ) {
			idAsyncNetwork::client.serverList.GUIUpdateSelected();
			continue;
		}

		if ( !idStr::Icmp( cmd, "LANConnect" ) || !idStr::Icmp( cmd, "connect" ) ) {
			idStr endpoint;
			if ( !idAsyncNetwork::client.serverList.GetSelectedAddress( endpoint ) ) {
				continue;
			}
			idCmdArgs connectArgs;
			connectArgs.AppendArg( "connect" );
			connectArgs.AppendArg( endpoint.c_str() );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );
			return;
		}

		if ( !idStr::Icmp( cmd, "MAPScan" ) || !idStr::Icmp( cmd, "initCreateServerSettings" ) ) {
			const char *gametype = cvarSystem->GetCVarString( "si_gameType" );
			if ( gametype == NULL || *gametype == 0 || idStr::Icmp( gametype, "singleplayer" ) == 0 ) {
				gametype = "DM";
			}

			int i, num;
			idListGUILocal *const mainMenuMapList = static_cast<idListGUILocal *>( guiMainMenu_MapList );
			int numMapsAdded = 0;
			int selectedIndex = -1;
			idStr si_map = cvarSystem->GetCVarString( "si_map" );
			const idDict *dict = NULL;

			mainMenuMapList->SetStateChanges( false );
			mainMenuMapList->Clear();

			num = fileSystem->GetNumMaps();
			for ( i = 0; i < num; i++ ) {
				dict = fileSystem->GetMapDecl( i );
				if ( !MapSupportsStartServerGameType( dict, gametype ) ) {
					continue;
				}

				const char *mapName = dict->GetString( "name" );
				if ( mapName[ 0 ] == '\0' ) {
					mapName = dict->GetString( "path" );
				}
				mapName = common->GetLanguageDict()->GetString( mapName );
				mainMenuMapList->Add( i, mapName );
				if ( !si_map.Icmp( dict->GetString( "path" ) ) ) {
					selectedIndex = numMapsAdded;
				}
				numMapsAdded++;
			}

			// If the requested gametype has no explicit flags in current content, keep the list usable.
			if ( numMapsAdded == 0 ) {
				for ( i = 0; i < num; i++ ) {
					dict = fileSystem->GetMapDecl( i );
					if ( !MapSupportsAnyMPGameType( dict ) ) {
						continue;
					}

					const char *mapName = dict->GetString( "name" );
					if ( mapName[ 0 ] == '\0' ) {
						mapName = dict->GetString( "path" );
					}
					mapName = common->GetLanguageDict()->GetString( mapName );
					mainMenuMapList->Add( i, mapName );
					if ( !si_map.Icmp( dict->GetString( "path" ) ) ) {
						selectedIndex = numMapsAdded;
					}
					numMapsAdded++;
				}
			}

			if ( numMapsAdded > 0 ) {
				if ( selectedIndex < 0 ) {
					selectedIndex = 0;
				}
				guiMainMenu->SetStateInt( "mapList_top", -1 );
				mainMenuMapList->SetSelection( selectedIndex );
				mainMenuMapList->SetStateChanges( true );
				int mapNum = mainMenuMapList->GetSelection( NULL, 0 );
				dict = fileSystem->GetMapDecl( mapNum );
			} else {
				guiMainMenu->SetStateInt( "mapList_sel_0", -1 );
				guiMainMenu->SetStateInt( "mapList_top", 0 );
				mainMenuMapList->SetStateChanges( true );
				dict = NULL;
			}
			cvarSystem->SetCVarString( "si_map", ( dict ? dict->GetString( "path" ) : "" ) );
			guiMainMenu->SetStateInt( "mapList_num", numMapsAdded );

			// set the current level shot
			UpdateMPLevelShot();
			guiMainMenu->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "click_mapList" ) ) {
			CommitStartServerMapSelection( guiMainMenu );
			UpdateMPLevelShot();
			guiMainMenu->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "inetConnect" ) ) {
			const char	*s = guiMainMenu->State().GetString( "inetGame" );

			if ( !s || s[0] == 0 ) {
				// don't put the menu away if there isn't a valid selection
				continue;
			}

			// Validate before handing a typed argument to the command system.
			// A hostname or IPv6 literal must never become console command text.
			netadr_t address;
			bool valid = strlen( s ) < 256;
			for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( s ); valid && *p; ++p ) {
				valid = *p > 32 && *p != 127 && *p != '"' && *p != ';' && *p != '\\';
			}
			if ( !valid || !Sys_StringToNetAdr( s, &address, true ) ||
				( address.type != NA_IP && address.type != NA_IP6 && address.type != NA_LOOPBACK ) ) {
				MessageBox( MSG_OK, va( common->GetLanguageDict()->GetString( "#str_06734" ), s ),
					common->GetLanguageDict()->GetString( "#str_06735" ), true );
				continue;
			}
			if ( !address.port ) {
				address.port = PORT_SERVER;
			}
			idCmdArgs connectArgs;
			connectArgs.AppendArg( "connect" );
			connectArgs.AppendArg( Sys_NetAdrToString( address ) );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );
			return;
		}

		if ( !idStr::Icmp( cmd, "startMultiplayer" ) ) {
			CommitStartServerMapSelection( guiMainMenu );
			int dedicated = guiActive->State().GetInt( "dedicated" );
			cvarSystem->SetCVarBool( "net_LANServer", guiActive->State().GetBool( "server_type" ) );
			if ( gui_configServerRate.GetInteger() > 0 ) {
				// guess the best rate for upstream, number of internet clients
				if ( gui_configServerRate.GetInteger() == 5 || cvarSystem->GetCVarBool( "net_LANServer" ) ) {
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 25600 );
				} else {
					// internet players
					int n_clients = cvarSystem->GetCVarInteger( "si_maxPlayers" );
					if ( !dedicated ) {
						n_clients--;
					}
					int maxclients = 0;
					switch ( gui_configServerRate.GetInteger() ) {
						case 1:
							// 128 kbits
							cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 8000 );
							maxclients = 2;
							break;
						case 2:
							// 256 kbits
							cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 9500 );
							maxclients = 3;
							break;
						case 3:
							// 384 kbits
							cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 10500 );
							maxclients = 4;
							break;
						case 4:
							// highest internet preset: treat as modern high-bandwidth connection
							cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 25600 );
							maxclients = 16;
							break;
						default:
							// unknown preset: fall back to modern defaults
							cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 16000 );
							maxclients = 16;
							break;
					}
					if ( n_clients > maxclients ) {
						const int adjustedMaxClients = dedicated ? maxclients : Min( 16, maxclients + 1 );
						if ( MessageBox( MSG_OKCANCEL, va( common->GetLanguageDict()->GetString( "#str_04315" ), adjustedMaxClients ), common->GetLanguageDict()->GetString( "#str_04316" ), true, "OK" )[ 0 ] == '\0' ) {
							continue;
						}
						cvarSystem->SetCVarInteger( "si_maxPlayers", adjustedMaxClients );
					}
				}
			}

			const int listenWarningLimit = GetListenServerPlayerWarningLimit(
				gui_configServerRate.GetInteger(),
				cvarSystem->GetCVarInteger( "net_serverMaxClientRate" ) );

			if ( !dedicated &&
				!cvarSystem->GetCVarBool( "net_LANServer" ) &&
				listenWarningLimit < 16 &&
				cvarSystem->GetCVarInteger( "si_maxPlayers" ) > listenWarningLimit ) {
				// "Dedicated server mode is recommended for internet servers with more than 4 players. Continue in listen mode?"
				if ( !MessageBox( MSG_YESNO, va( common->GetLanguageDict()->GetString( "#str_100625" ), listenWarningLimit ), common->GetLanguageDict()->GetString ( "#str_100626" ), true, "yes" )[ 0 ] ) {
					continue;
				}
			}

			if ( dedicated ) {
				cvarSystem->SetCVarInteger( "net_serverDedicated", 1 );
			} else {
				cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );
			}



			ExitMenu();
			// may trigger a reloadEngine - APPEND
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "SpawnServer\n" );
			return;
		}

		if ( !idStr::Icmp( cmd, "mpSkin")) {
			idStr skin;
			if ( args.Argc() - icmd >= 1 ) {
				skin = args.Argv( icmd++ );
				cvarSystem->SetCVarString( "ui_skin", skin );
				SetMainMenuSkin();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "close" ) ) {
			// if we aren't in a game, the menu can't be closed
			if ( mapSpawned ) {
				ExitMenu();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "resetdefaults" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "exec default.cfg" );
			guiMainMenu->SetKeyBindingNames();
			SetMainMenuVideoGuiVars( guiMainMenu );
			SetMainMenuQualityGuiVars( guiMainMenu );
			SyncMainMenuAspectVisibility( guiMainMenu );
			continue;
		}


		if ( !idStr::Icmp( cmd, "bind" ) ) {
			if ( args.Argc() - icmd >= 2 ) {
				int key = atoi( args.Argv( icmd++ ) );
				idStr bind = args.Argv( icmd++ );
				if ( idKeyInput::NumBinds( bind ) >= 2 && !idKeyInput::KeyIsBoundTo( key, bind ) ) {
					idKeyInput::UnbindBinding( bind );
				}
				idKeyInput::SetBinding( key, bind );
				guiMainMenu->SetKeyBindingNames();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv( icmd++ );
				int channel = 1;
				if ( snd.Length() == 1 ) {
					channel = atoi( snd );
					snd = args.Argv( icmd++ );
				}
				menuSoundWorld->PlayShaderDirectly( snd, channel );

			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "music" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv( icmd++ );
				menuSoundWorld->PlayShaderDirectly( snd, 2 );
				if ( menuIntroBlackoutActive && menuIntroBlackoutAwaitMenuMusic &&
					( snd.Icmp( "main_menu" ) == 0 || snd.Icmp( "main_menu_gameplay" ) == 0 ) ) {
					menuIntroBlackoutAwaitMenuMusic = false;
					menuIntroBlackoutFadeStart = -1;
				}
			}
			continue;
		}

		// triggered from mainmenu or mpmain
		if ( !idStr::Icmp( cmd, "sound" ) ) {
			idStr vcmd;
			if ( args.Argc() - icmd >= 1 ) {
				vcmd = args.Argv( icmd++ );
			}
			if ( !vcmd.Length() || !vcmd.Icmp( "speakers" ) ) {
				int old = cvarSystem->GetCVarInteger( "s_numberOfSpeakers" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );
				if ( old != cvarSystem->GetCVarInteger( "s_numberOfSpeakers" ) ) {
#ifdef _WIN32
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04142" ), common->GetLanguageDict()->GetString( "#str_04141" ), true );
#else
					// a message that doesn't mention the windows control panel
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07230" ), common->GetLanguageDict()->GetString( "#str_04141" ), true );
#endif
				}
			}
			if ( !vcmd.Icmp( "eax" ) ) {
				if ( cvarSystem->GetCVarBool( "s_useEAXReverb" ) ) {
					// EAX requires the OpenAL backend; force it on when requested from the menu.
					cvarSystem->SetCVarBool( "s_useOpenAL", true );
					cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );

					const int eax = soundSystem->IsEAXAvailable();
					switch ( eax ) {
					case 2:
						// OpenAL subsystem load failed.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07238" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case 1:
						// Enabled and active on current device.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04137" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case -1:
						cvarSystem->SetCVarBool( "s_useEAXReverb", false );
						// Explicitly disabled.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07233" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case 0:
					default:
						cvarSystem->SetCVarBool( "s_useEAXReverb", false );
						// Unsupported by OpenAL version/device or EFX unavailable.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07232" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					}
				} else {
					// openQ4 has no legacy (non-OpenAL) mixer; leave
					// s_useOpenAL untouched so IsEAXAvailable() keeps
					// reporting real capability after re-enabling EAX.
					cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );
					// when you restart
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04137" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
				}
			}
			if ( !vcmd.Icmp( "drivar" ) ) {
				if ( idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( cvarSystem->GetCVarString( "s_deviceName" ) ) ) {
					cvarSystem->SetCVarString( "s_deviceName", "" );
				}
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );				
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyCorpseTimeChoice" ) ) {
			ApplyMainMenuCorpseTimeChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyGunPositionChoice" ) ) {
			ApplyMainMenuGunPositionChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyForceModelChoice" ) ) {
			ApplyMainMenuForceModelChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshKeyBindings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			if ( targetGui != NULL ) {
				targetGui->SetKeyBindingNames();
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshSystemSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			PopulateMainMenuDisplayChoices( targetGui );
			SetMainMenuQualityGuiVars( targetGui );
			SyncMainMenuAspectVisibility( targetGui );
			if ( targetGui != NULL ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshAudioSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			PopulateMainMenuAudioDeviceChoices( targetGui );
			if ( targetGui != NULL ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshMods" ) ) {
			SetModsMenuGuiVars();
			if ( guiActive != NULL ) {
				guiActive->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyDisplayModeChoice" ) ) {
			ApplyMainMenuDisplayModeChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyCustomDisplaySize" ) ) {
			ApplyMainMenuCustomDisplaySize( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshDisplayChoices" ) ) {
			RefreshMainMenuDisplayChoices( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyPerformancePreset" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			idCmdArgs presetArgs;
			presetArgs.AppendArg( "applyPerformancePreset" );
			AppendMainMenuCommandArgsUntilSeparator( args, icmd, presetArgs );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, presetArgs );
			RefreshMainMenuPerformancePresetGuiVars( gui );
			continue;
		}

		if ( !idStr::Icmp( cmd, "autoDetectPerformancePreset" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			idCmdArgs presetArgs;
			presetArgs.AppendArg( "autoDetectPerformancePreset" );
			AppendMainMenuCommandArgsUntilSeparator( args, icmd, presetArgs );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, presetArgs );
			RefreshMainMenuPerformancePresetGuiVars( gui );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applySettingsScroll" ) ) {
			idStr pageName;
			if ( args.Argc() - icmd >= 1 ) {
				pageName = args.Argv( icmd++ );
			}
			ApplyMainMenuSettingsScrollPage( guiActive ? guiActive : guiMainMenu, pageName.c_str(), false );
			continue;
		}

		if ( !idStr::Icmp( cmd, "video" ) ) {
			idStr vcmd;
			if ( args.Argc() - icmd >= 1 ) {
				vcmd = args.Argv( icmd++ );
			}

			int oldSpec = com_machineSpec.GetInteger();

			if ( idStr::Icmp( vcmd, "low" ) == 0 ) {
				com_machineSpec.SetInteger( 0 );
			} else if ( idStr::Icmp( vcmd, "medium" ) == 0 ) {
				com_machineSpec.SetInteger( 1 );
			} else  if ( idStr::Icmp( vcmd, "high" ) == 0 ) {
				com_machineSpec.SetInteger( 2 );
			} else  if ( idStr::Icmp( vcmd, "ultra" ) == 0 ) {
				com_machineSpec.SetInteger( 3 );
			} else if ( idStr::Icmp( vcmd, "recommended" ) == 0 ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "setMachineSpec\n" );
			}

			if ( oldSpec != com_machineSpec.GetInteger() ) {
				guiActive->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "execMachineSpec\n" );
				SetMainMenuVideoGuiVars( guiActive );
				SyncMainMenuAspectVisibility( guiActive );
				guiActive->StateChanged( common->GetPresentationTime() );
			}

			if ( idStr::Icmp( vcmd, "restart" )  == 0) {
				guiActive->HandleNamedEvent( "cvar write render" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart\n" );
			}

			continue;
		}

		if ( !idStr::Icmp( cmd, "clearBind" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idKeyInput::UnbindBinding( args.Argv( icmd++ ) );
				guiMainMenu->SetKeyBindingNames();
			}
			continue;
		}

		// FIXME: obsolete
		if ( !idStr::Icmp( cmd, "chatdone" ) ) {
			idStr temp = guiActive->State().GetString( "chattext" );
			temp += "\r";
			guiActive->SetStateString( "chattext", "" );
			continue;
		}

		if ( !idStr::Icmp ( cmd, "exec" ) ) {

			//Backup the language so we can restore it after defaults.
			idStr lang = cvarSystem->GetCVarString("sys_lang");

			cmdSystem->BufferCommandText( CMD_EXEC_NOW, args.Argv( icmd++ ) );
			if ( idStr::Icmp( "cvar_restart", args.Argv( icmd - 1 ) ) == 0 ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "exec default.cfg" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "setMachineSpec\n" );

				//Make sure that any r_brightness changes take effect
				float bright = cvarSystem->GetCVarFloat("r_brightness");
				cvarSystem->SetCVarFloat("r_brightness", 0.0f);
				cvarSystem->SetCVarFloat("r_brightness", bright);

				//Force user info modified after a reset to defaults
				cvarSystem->SetModifiedFlags(CVAR_USERINFO);

				guiActive->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );

				//Restore the language
				cvarSystem->SetCVarString("sys_lang", lang);

			}
			continue;
		}

		if ( !idStr::Icmp ( cmd, "loadBinds" ) ) {
			guiMainMenu->SetKeyBindingNames();
			continue;
		}
		
		if ( !idStr::Icmp( cmd, "systemCvars" ) ) {
			guiActive->HandleNamedEvent( "cvar read render" );
			guiActive->HandleNamedEvent( "cvar read sound" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "CheckUpdate" ) ) {
			idAsyncNetwork::client.SendVersionCheck();
			continue;
		}

		if ( !idStr::Icmp( cmd, "CheckUpdate2" ) ) {
			idAsyncNetwork::client.SendVersionCheck( true );
			continue;
		}

		if ( !idStr::Icmp( cmd, "update_model" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			SetMainMenuMPModelVars( targetGui );
			if ( targetGui ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "initMPSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			SetMainMenuMPModelVars( targetGui );
			if ( targetGui ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "exitMPSettings" ) ) {
			CommitMainMenuMPSettings();
			continue;
		}
	}
}

/*
==============
idSessionLocal::HandleChatMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleChatMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int i;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for ( i = 0; i < args.Argc(); ) {
		const char *cmd = args.Argv( i++ );

		if ( idStr::Icmp( cmd, "chatactive" ) == 0 ) {
			//chat.chatMode = CHAT_GLOBAL;
			continue;
		}
		if ( idStr::Icmp( cmd, "chatabort" ) == 0 ) {
			//chat.chatMode = CHAT_NONE;
			continue;
		}
		if ( idStr::Icmp( cmd, "netready" ) == 0 ) {
			bool b = cvarSystem->GetCVarBool( "ui_ready" );
			cvarSystem->SetCVarBool( "ui_ready", !b );
			continue;
		}
		if ( idStr::Icmp( cmd, "netstart" ) == 0 ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "netcommand start\n" );
			continue;
		}
	}
}

/*
==============
idSessionLocal::HandleInGameCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleInGameCommands( const char *menuCommand ) {
	// execute the command from the menu
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	const char *cmd = args.Argv( 0 );
	if ( !idStr::Icmp( cmd, "close" ) ) {
		if ( guiActive ) {
			sysEvent_t  ev;
			ev.evType = SE_NONE;
			const char	*cmd;
			cmd = guiActive->HandleEvent( &ev, common->GetPresentationTime() );
			guiActive->Activate( false, common->GetPresentationTime() );
			guiActive = NULL;
		}
	}
}

/*
==============
idSessionLocal::DispatchCommand
==============
*/
void idSessionLocal::DispatchCommand( idUserInterface *gui, const char *menuCommand, bool doIngame ) {

	if ( !gui ) {
		gui = guiActive;
	}

	if ( gui == guiMainMenu ) {
		HandleMainMenuCommands( menuCommand );
		return;
	} else if ( arenaCampaign.IsGui( gui ) ) {
		arenaCampaign.HandleGuiCommand( menuCommand );
		return;
	} else if ( gui == guiDemoMenu ) {
		HandleDemoMenuCommand( menuCommand );
		return;
	} else if ( gui == guiIntro) {
		HandleIntroMenuCommands( menuCommand );
	} else if ( gui == guiMsg ) {
		HandleMsgCommands( menuCommand );
	} else if ( gui == guiTakeNotes ) {
		HandleNoteCommands( menuCommand );
	} else if ( gui == guiRestartMenu ) {
		HandleRestartMenuCommands( menuCommand );
	} else if ( game && guiActive && guiActive->State().GetBool( "gameDraw" ) ) {
		const char *cmd = game->HandleGuiCommands( menuCommand );
		if ( !cmd ) {
			guiActive = NULL;
		} else if ( idStr::Icmp( cmd, "main" ) == 0 ) {
			StartMenu();
		} else if ( idStr::Icmpn( cmd, "main ", 5 ) == 0 ) {
			StartMenu();
			idStr mainMenuEvent = cmd + 5;
			mainMenuEvent.StripLeading( ' ' );
			if ( mainMenuEvent.Length() > 0 ) {
				guiMainMenu->HandleNamedEvent( mainMenuEvent.c_str() );
			}
		} else if ( strstr( cmd, "sound " ) == cmd ) {
			// pipe the GUI sound commands not handled by the game to the main menu code
			HandleMainMenuCommands( cmd );
		}
	} else if ( guiHandle ) {
		if ( (*guiHandle)( menuCommand ) ) {
			return;
		}
	} else if ( !doIngame ) {
		common->DPrintf( "idSessionLocal::DispatchCommand: no dispatch found for command '%s'\n", menuCommand );
	}

	if ( doIngame ) {
		HandleInGameCommands( menuCommand );
	}
}


/*
==============
idSessionLocal::MenuEvent

Executes any commands returned by the gui
==============
*/
static bool MainMenuWindowStateIsNonZero( idUserInterface *gui, const char *stateName ) {
	if ( gui == NULL || gui->GetDesktop() == NULL ) {
		return false;
	}

	idWinVar *state = gui->GetDesktop()->GetWinVarByName( stateName, true );
	return state != NULL && atoi( state->c_str() ) != 0;
}

static bool MainMenuWindowStateEqualsInt( idUserInterface *gui, const char *stateName, int expectedValue ) {
	if ( gui == NULL || gui->GetDesktop() == NULL ) {
		return false;
	}

	idWinVar *state = gui->GetDesktop()->GetWinVarByName( stateName, true );
	return state != NULL && atoi( state->c_str() ) == expectedValue;
}

static bool MainMenuSettingsPopupIsVisible( idUserInterface *gui ) {
	static const char *settingsPopupStates[] = {
		"pop_p_defaults::visible",
		"pop_p_setAdv::visible",
		"pop_p_auto::visible",
		"pop_p_ultrawarn::visible",
		"pop_p_vidwarn::visible",
		"pop_p_set_sndadv::visible"
	};

	for ( int i = 0; i < static_cast<int>( sizeof( settingsPopupStates ) / sizeof( settingsPopupStates[ 0 ] ) ); ++i ) {
		if ( MainMenuWindowStateIsNonZero( gui, settingsPopupStates[ i ] ) ) {
			return true;
		}
	}

	return false;
}

typedef struct mainMenuSettingsScrollPage_s {
	const char *name;
	const char *pageVisibleState;
	const char *scrollStateName;
	idCVar *scrollCvar;
	const char *contentRectState;
	const char *thumbVisibleState;
	const char *thumbNoEventsState;
	const char *sectionChoiceState;
	int expectedPage;
	int minValue;
	int maxValue;
	int contentX;
	int baseY;
	int contentHeight;
	float stepY;
} mainMenuSettingsScrollPage_t;

static const mainMenuSettingsScrollPage_t MAINMENU_SETTINGS_SCROLL_PAGES[] = {
	{
		"system",
		"p_settings_sys::visible",
		"gui_set_sys_scroll",
		&gui_set_sys_scroll,
		"set_sys_content::rect",
		"set_sys_scroll_thumb::visible",
		"set_sys_scroll_thumb::noevents",
		"sys_section_choice",
		22,
		0,
		28,
		-24,
		-88,
		958,
		22.0f
	},
	{
		"audio",
		"p_settings_audio::visible",
		"gui_set_audio_scroll",
		&gui_set_audio_scroll,
		"set_audio_content::rect",
		"set_audio_scroll_thumb::visible",
		"set_audio_scroll_thumb::noevents",
		NULL,
		36,
		0,
		0,
		-24,
		-84,
		330,
		0.0f
	},
	{
		"game",
		"p_settings_game::visible",
		"gui_set_game_scroll",
		&gui_set_game_scroll,
		"set_game_content::rect",
		"set_game_scroll_thumb::visible",
		"set_game_scroll_thumb::noevents",
		"game_section_choice",
		21,
		0,
		46,
		-24,
		-41,
		1428,
		24.0f
	}
};

static const mainMenuSettingsScrollPage_t *FindMainMenuSettingsScrollPage( const char *name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		if ( idStr::Icmp( MAINMENU_SETTINGS_SCROLL_PAGES[ i ].name, name ) == 0 ) {
			return &MAINMENU_SETTINGS_SCROLL_PAGES[ i ];
		}
	}

	return NULL;
}

static bool MainMenuSetWindowVar( idUserInterface *gui, const char *stateName, const char *value ) {
	if ( gui == NULL || gui->GetDesktop() == NULL || stateName == NULL || value == NULL ) {
		return false;
	}

	idWinVar *state = gui->GetDesktop()->GetWinVarByName( stateName, true );
	if ( state == NULL ) {
		return false;
	}

	state->Set( value );
	state->SetEval( false );
	return true;
}

static int MainMenuSettingsSectionChoiceForScroll( const mainMenuSettingsScrollPage_t &page, int scrollValue ) {
	if ( idStr::Icmp( page.name, "game" ) == 0 ) {
		if ( scrollValue < 11 ) {
			return 0;
		}
		if ( scrollValue < 21 ) {
			return 1;
		}
		if ( scrollValue < 37 ) {
			return 2;
		}
		if ( scrollValue < 44 ) {
			return 3;
		}
		if ( scrollValue < 46 ) {
			return 4;
		}
		return 5;
	}

	if ( idStr::Icmp( page.name, "system" ) == 0 ) {
		if ( scrollValue < 5 ) {
			return 0;
		}
		if ( scrollValue < 9 ) {
			return 1;
		}
		if ( scrollValue < 14 ) {
			return 2;
		}
		if ( scrollValue < 21 ) {
			return 3;
		}
		if ( scrollValue < 28 ) {
			return 4;
		}
		return 5;
	}

	return 0;
}

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const mainMenuSettingsScrollPage_t &page, int requestedValue, bool requireVisiblePage ) {
	if ( gui == NULL || gui->GetDesktop() == NULL ) {
		return false;
	}

	if ( requireVisiblePage && !MainMenuWindowStateEqualsInt( gui, "desktop::curr", page.expectedPage ) ) {
		return false;
	}

	if ( requireVisiblePage && MainMenuSettingsPopupIsVisible( gui ) ) {
		return false;
	}

	if ( requireVisiblePage && !MainMenuWindowStateIsNonZero( gui, page.pageVisibleState ) ) {
		return false;
	}

	const int minValue = page.minValue;
	const int maxValue = page.maxValue;
	int scrollValue = requestedValue;
	if ( scrollValue < minValue || scrollValue > maxValue ) {
		scrollValue = gui->GetStateInt( page.scrollStateName, va( "%d", page.scrollCvar->GetInteger() ) );
	}
	scrollValue = idMath::ClampInt( minValue, maxValue, scrollValue );

	page.scrollCvar->SetInteger( scrollValue );
	gui->SetStateInt( page.scrollStateName, scrollValue );

	const bool canScroll = maxValue > minValue;
	MainMenuSetWindowVar( gui, page.thumbVisibleState, canScroll ? "1" : "0" );
	MainMenuSetWindowVar( gui, page.thumbNoEventsState, canScroll ? "0" : "1" );

	int contentHeight = page.contentHeight;
	float stepY = page.stepY;
	if ( idStr::Icmp( page.name, "system" ) == 0 && gui->GetStateInt( "display_count", "0" ) <= 1 ) {
		contentHeight = 902;
		stepY = 19.5f;
	}

	const int scrollOffset = idMath::Ftoi( stepY * static_cast<float>( scrollValue ) + 0.5f );
	const int contentY = page.baseY - scrollOffset;
	const idStr contentRect = va( "%d,%d,640,%d", page.contentX, contentY, contentHeight );
	const bool applied = MainMenuSetWindowVar( gui, page.contentRectState, contentRect.c_str() );

	if ( page.sectionChoiceState != NULL ) {
		gui->SetStateInt( page.sectionChoiceState, MainMenuSettingsSectionChoiceForScroll( page, scrollValue ) );
	}

	gui->StateChanged( common->GetPresentationTime(), true );
	return applied;
}

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const char *pageName, bool requireVisiblePage ) {
	const mainMenuSettingsScrollPage_t *page = FindMainMenuSettingsScrollPage( pageName );
	if ( page == NULL ) {
		return false;
	}

	const int minValue = page->minValue;
	const int maxValue = page->maxValue;
	const int requestedValue = idMath::ClampInt( minValue, maxValue, gui ? gui->GetStateInt( page->scrollStateName, va( "%d", page->scrollCvar->GetInteger() ) ) : page->scrollCvar->GetInteger() );
	return ApplyMainMenuSettingsScrollPage( gui, *page, requestedValue, requireVisiblePage );
}

static bool AdjustMainMenuPageScroll( idUserInterface *gui, const mainMenuSettingsScrollPage_t &page, int delta, bool toStart, bool toEnd ) {
	if ( gui == NULL || gui->GetDesktop() == NULL ) {
		return false;
	}

	if ( !MainMenuWindowStateEqualsInt( gui, "desktop::curr", page.expectedPage ) ) {
		return false;
	}

	if ( MainMenuSettingsPopupIsVisible( gui ) ) {
		return false;
	}

	if ( !MainMenuWindowStateIsNonZero( gui, page.pageVisibleState ) ) {
		return false;
	}

	const int minValue = page.minValue;
	const int maxValue = page.maxValue;
	if ( maxValue <= minValue ) {
		return false;
	}

	const int current = idMath::ClampInt( minValue, maxValue, gui->GetStateInt( page.scrollStateName, va( "%d", page.scrollCvar->GetInteger() ) ) );
	int next = current;
	if ( toStart ) {
		next = minValue;
	} else if ( toEnd ) {
		next = maxValue;
	} else {
		next = idMath::ClampInt( minValue, maxValue, current + delta );
	}

	if ( next != current || page.scrollCvar->GetInteger() != next ) {
		ApplyMainMenuSettingsScrollPage( gui, page, next, true );
	}

	return true;
}

static bool HandleMainMenuSettingsScrollInput( idUserInterface *gui, int key ) {
	int delta = 0;
	bool toStart = false;
	bool toEnd = false;
	switch ( key ) {
		case K_MWHEELUP:
			delta = -1;
			break;
		case K_MWHEELDOWN:
			delta = 1;
			break;
		case K_PGUP:
		case K_KP_PGUP:
		case K_JOY1:
			delta = -MENU_SETTINGS_PAGE_SCROLL_STEP;
			break;
		case K_PGDN:
		case K_KP_PGDN:
		case K_JOY2:
			delta = MENU_SETTINGS_PAGE_SCROLL_STEP;
			break;
		case K_HOME:
		case K_KP_HOME:
			toStart = true;
			break;
		case K_END:
		case K_KP_END:
			toEnd = true;
			break;
		default:
			return false;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		if ( AdjustMainMenuPageScroll( gui, MAINMENU_SETTINGS_SCROLL_PAGES[ i ], delta, toStart, toEnd ) ) {
			return true;
		}
	}

	return false;
}

static void SyncMainMenuSettingsScrollPages( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		ApplyMainMenuSettingsScrollPage( gui, MAINMENU_SETTINGS_SCROLL_PAGES[ i ], idMath::INT_MIN, true );
	}
}

void idSessionLocal::MenuEvent( const sysEvent_t *event ) {
	const char	*menuCommand;

	if ( guiActive == NULL ) {
		return;
	}

	if ( event->evType == SE_KEY && event->evValue2 == 1 ) {
		if ( HandleMainMenuSettingsScrollInput( guiActive, event->evValue ) ) {
			return;
		}
	}

	menuCommand = guiActive->HandleEvent( event, common->GetPresentationTime() );
	SyncMainMenuSettingsScrollPages( guiActive );

	if ( !menuCommand || !menuCommand[0] ) {
		// If the menu didn't handle the event, and it's a key down event for an F key, run the bind
		if ( event->evType == SE_KEY && event->evValue2 == 1 && event->evValue >= K_F1 && event->evValue <= K_F12 ) {
			idKeyInput::ExecKeyBinding( event->evValue );
		}
		return;
	}

	DispatchCommand( guiActive, menuCommand );
	SyncMainMenuSettingsScrollPages( guiActive );
}

/*
=================
idSessionLocal::GuiFrameEvents
=================
*/
void idSessionLocal::GuiFrameEvents() {
	const char	*cmd;
	sysEvent_t  ev;
	idUserInterface	*gui;

	// stop generating move and button commands when a local console or menu is active
	// running here so SP, async networking and no game all go through it
	if ( console->Active() || guiActive ) {
		usercmdGen->InhibitUsercmd( INHIBIT_SESSION, true );
	} else {
		usercmdGen->InhibitUsercmd( INHIBIT_SESSION, false );
	}

	if ( guiTest ) {
		gui = guiTest;
	} else if ( guiActive ) {
		gui = guiActive;
	} else {
		ClearMenuControllerRepeatState();
		return;
	}

	if ( guiActive ) {
		PumpControllerMenuNavigation( this );
		if ( guiTest ) {
			gui = guiTest;
		} else if ( guiActive ) {
			gui = guiActive;
		} else {
			ClearMenuControllerRepeatState();
			return;
		}
	} else {
		ClearMenuControllerRepeatState();
	}

	memset( &ev, 0, sizeof( ev ) );

	ev.evType = SE_NONE;
	cmd = gui->HandleEvent( &ev, common->GetPresentationTime() );
	if ( cmd && cmd[0] ) {
		DispatchCommand( guiActive, cmd );
	}
	SyncMainMenuSettingsScrollPages( gui );
}

/*
=================
idSessionLocal::BoxDialogSanityCheck
=================
*/
bool idSessionLocal::BoxDialogSanityCheck( void ) {
	if ( !common->IsInitialized() ) {
		common->DPrintf( "message box sanity check: !common->IsInitialized()\n" );
		return false;
	}
	if ( !guiMsg ) {
		return false;
	}
	if ( guiMsgRestore ) {
		common->DPrintf( "message box sanity check: recursed\n" );
		return false;
	}
	if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) ) {
		common->DPrintf( "message box sanity check: not compatible with dedicated server\n" );
		return false;
	}
	return true;
}

/*
=================
idSessionLocal::MessageBox
=================
*/
const char* idSessionLocal::MessageBox( msgBoxType_t type, const char *message, const char *title, bool wait, const char *fire_yes, const char *fire_no, bool network ) {
	
	common->DPrintf( "MessageBox: %s - %s\n", title ? title : "", message ? message : "" );
	
	if ( !BoxDialogSanityCheck() ) {
		return NULL;
	}

	guiMsg->SetStateString( "title", title ? title : "" );
	guiMsg->SetStateString( "message", message ? message : "" );
	if ( type == MSG_WAIT ) {
		guiMsg->SetStateString( "visible_msgbox", "0" );
		guiMsg->SetStateString( "visible_waitbox", "1" );
	} else {
		guiMsg->SetStateString( "visible_msgbox", "1" );
		guiMsg->SetStateString( "visible_waitbox", "0" );
	}

	guiMsg->SetStateString( "visible_entry", "0" );
	guiMsg->SetStateString( "visible_cdkey", "0" );
	switch ( type ) {
		case MSG_INFO:
			guiMsg->SetStateString( "mid", "" );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_OK:
			guiMsg->SetStateString( "mid", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "visible_mid", "1" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_ABORT:
			guiMsg->SetStateString( "mid", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "1" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_OKCANCEL:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			break;
		case MSG_YESNO:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104341" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104342" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			break;
		case MSG_PROMPT:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			guiMsg->SetStateString( "visible_entry", "1" );			
			guiMsg->HandleNamedEvent( "Prompt" );
			break;
		case MSG_WAIT:
			break;
		default:
			common->Printf( "idSessionLocal::MessageBox: unknown msg box type\n" );
	}
	msgFireBack[ 0 ] = fire_yes ? fire_yes : "";
	msgFireBack[ 1 ] = fire_no ? fire_no : "";
	guiMsgRestore = guiActive;
	guiActive = guiMsg;
	guiMsg->SetCursor( 325, 290 );
	guiActive->Activate( true, common->GetPresentationTime() );
	msgRunning = true;
	msgRetIndex = -1;
	
	if ( wait ) {
		// play one frame ignoring events so we don't get confused by parasite button releases
		msgIgnoreButtons = true;
		common->GUIFrame( true, network );
		msgIgnoreButtons = false;
		while ( msgRunning ) {
			common->GUIFrame( true, network );
		}
		if ( msgRetIndex < 0 ) {
			// MSG_WAIT and other StopBox calls
			return NULL;
		}
		if ( type == MSG_PROMPT ) {
			if ( msgRetIndex == 0 ) {
				guiMsg->State().GetString( "str_entry", "", msgFireBack[ 0 ] );
				return msgFireBack[ 0 ].c_str();
			} else {
				return NULL;
			}
		} else {
			return msgFireBack[ msgRetIndex ].c_str();
		}
	}
	return NULL;
}

void idSessionLocal::RunTimedWaitBoxPacingTest( int durationMsec, bool network, const char *reason ) {
	const int clampedDurationMsec = idMath::ClampInt( 1, 60000, durationMsec );
	const char *snapshotReason = ( reason != NULL && reason[ 0 ] != '\0' ) ? reason : "testWaitBox";

	if ( !BoxDialogSanityCheck() ) {
		common->Warning( "RunTimedWaitBoxPacingTest: message box path is unavailable in the current session state\n" );
		return;
	}

	ResetFramePacingStats();

	MessageBox( MSG_WAIT, "", "", false, NULL, NULL, network );
	if ( !msgRunning || guiActive != guiMsg ) {
		common->Warning( "RunTimedWaitBoxPacingTest: failed to activate wait box\n" );
		return;
	}

	// Keep pumping the modal loop on the normal GUI presentation path, but do
	// not execute queued console commands from inside the automated test itself.
	common->GUIFrame( false, network );
	const int endTime = common->GetPresentationTime() + clampedDurationMsec;

	while ( msgRunning && common->GetPresentationTime() < endTime ) {
		common->GUIFrame( false, network );
	}

	if ( msgRunning ) {
		StopBox();
	}
	while ( msgRunning ) {
		common->GUIFrame( false, network );
	}

	PrintFramePacingSnapshot( snapshotReason );
}

void idSessionLocal::RunTimedMessageBoxPacingTest( int durationMsec, bool network, const char *reason ) {
	const int clampedDurationMsec = idMath::ClampInt( 1, 60000, durationMsec );
	const char *snapshotReason = ( reason != NULL && reason[ 0 ] != '\0' ) ? reason : "testMessageBox";

	if ( !BoxDialogSanityCheck() ) {
		common->Warning( "RunTimedMessageBoxPacingTest: message box path is unavailable in the current session state\n" );
		return;
	}

	ResetFramePacingStats();

	MessageBox( MSG_OK, "", "", false, NULL, NULL, network );
	if ( !msgRunning || guiActive != guiMsg ) {
		common->Warning( "RunTimedMessageBoxPacingTest: failed to activate message box\n" );
		return;
	}

	// Keep pumping the modal loop on the normal GUI presentation path, but do
	// not execute queued console commands from inside the automated test itself.
	common->GUIFrame( false, network );
	const int endTime = common->GetPresentationTime() + clampedDurationMsec;

	while ( msgRunning && common->GetPresentationTime() < endTime ) {
		common->GUIFrame( false, network );
	}

	if ( msgRunning ) {
		HandleMsgCommands( "mid" );
	}
	while ( msgRunning ) {
		common->GUIFrame( false, network );
	}

	PrintFramePacingSnapshot( snapshotReason );
}

/*
=================
idSessionLocal::DownloadProgressBox
=================
*/
void idSessionLocal::DownloadProgressBox( backgroundDownload_t *bgl, const char *title, int progress_start, int progress_end ) {
	int dlnow = 0, dltotal = 0;
	int startTime = Sys_Milliseconds();
	int lapsed;
	idStr sNow, sTotal, sBW, sETA, sMsg;

	if ( !BoxDialogSanityCheck() ) {
		return;
	}

	guiMsg->SetStateString( "visible_msgbox", "1" );
	guiMsg->SetStateString( "visible_waitbox", "0" );

	guiMsg->SetStateString( "visible_entry", "0" );
	guiMsg->SetStateString( "visible_cdkey", "0" );

	guiMsg->SetStateString( "mid", "Cancel" );
	guiMsg->SetStateString( "visible_mid", "1" );
	guiMsg->SetStateString( "visible_left", "0" );
	guiMsg->SetStateString( "visible_right", "0" );

	guiMsg->SetStateString( "title", title );
	guiMsg->SetStateString( "message", "Connecting.." );

	guiMsgRestore = guiActive;
	guiActive = guiMsg;
	msgRunning = true;

	while ( 1 ) {
		while ( msgRunning ) {
			common->GUIFrame( true, false );
			if ( bgl->completed ) {
				guiActive = guiMsgRestore;
				guiMsgRestore = NULL;
				return;
			} else if ( bgl->url.dltotal != dltotal || bgl->url.dlnow != dlnow ) {
				dltotal = bgl->url.dltotal;
				dlnow = bgl->url.dlnow;
				lapsed = Sys_Milliseconds() - startTime;
				sNow.BestUnit( "%.2f", dlnow, MEASURE_SIZE );
				if ( lapsed > 2000 ) {
					sBW.BestUnit( "%.1f", ( 1000.0f * dlnow ) / lapsed, MEASURE_BANDWIDTH );
				} else {
					sBW = "-- KB/s";
				}
				if ( dltotal ) {
					sTotal.BestUnit( "%.2f", dltotal, MEASURE_SIZE );
					if ( lapsed < 2000 ) {
						sMsg = sNow;
						sMsg += " / ";
						sMsg += sTotal;
					} else {
						if ( dlnow > 0 ) {
							sETA = va( "%.0f sec", ( (float)dltotal / (float)dlnow - 1.0f ) * lapsed / 1000 );
						} else {
							sETA = "-- sec";
						}
						sMsg = sNow;
						sMsg += " / ";
						sMsg += sTotal;
						sMsg += " ( ";
						sMsg += sBW;
						sMsg += " - ";
						sMsg += sETA;
						sMsg += " )";
					}
				} else {
					if ( lapsed < 2000 ) {
						sMsg = sNow;
					} else {
						sMsg = sNow;
						sMsg += " - ";
						sMsg += sBW;
					}
				}
				if ( dltotal ) {
					const int64 progressNumerator = static_cast<int64>( dlnow ) * ( progress_end - progress_start );
					guiMsg->SetStateString( "progress", va( "%d", progress_start + static_cast<int>( progressNumerator / dltotal ) ) );
				} else {
					guiMsg->SetStateString( "progress", "0" );
				}
				guiMsg->SetStateString( "message", sMsg.c_str() );
			}
		}
		// abort was used - tell the downloader and wait till final stop
		bgl->url.status = DL_ABORTING;
		guiMsg->SetStateString( "title", "Aborting.." );
		guiMsg->SetStateString( "visible_mid", "0" );
		// continue looping
		guiMsgRestore = guiActive;
		guiActive = guiMsg;
		msgRunning = true;
	}
}

/*
=================
idSessionLocal::StopBox
=================
*/
void idSessionLocal::StopBox() {
	if ( guiActive == guiMsg ) {
		HandleMsgCommands( "stop" );
	}
}

/*
=================
idSessionLocal::HandleMsgCommands
=================
*/
void idSessionLocal::HandleMsgCommands( const char *menuCommand ) {
	assert( guiActive == guiMsg );
	idCmdArgs args;
	args.TokenizeString( menuCommand, false );
	if ( args.Argc() == 0 ) {
		return;
	}
	const char *cmd = args.Argv( 0 );
	// "stop" works even on first frame
	if ( idStr::Icmp( cmd, "stop" ) == 0 ) {
		// force hiding the current dialog
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = -1;
	}
	if ( msgIgnoreButtons ) {
		common->DPrintf( "MessageBox HandleMsgCommands 1st frame ignore\n" );
		return;
	}
	if ( idStr::Icmp( cmd, "mid" ) == 0 || idStr::Icmp( cmd, "left" ) == 0 ) {
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = 0;
		DispatchCommand( guiActive, msgFireBack[ 0 ].c_str() );
	} else if ( idStr::Icmp( cmd, "right" ) == 0 ) {
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = 1;
		DispatchCommand( guiActive, msgFireBack[ 1 ].c_str() );
	}
}

/*
=================
idSessionLocal::HandleNoteCommands
=================
*/
#define NOTEDATFILE "C:/notenumber.dat"

void idSessionLocal::HandleNoteCommands( const char *menuCommand ) {
	guiActive = NULL;

	if ( idStr::Icmp( menuCommand,  "note" ) == 0 && mapSpawned ) {

		idFile *file = NULL;
		for ( int tries = 0; tries < 10; tries++ ) {
			file = fileSystem->OpenExplicitFileRead( NOTEDATFILE );
			if ( file != NULL ) {
				break;
			}
			Sys_Sleep( 500 );
		}
		int noteNumber = 1000;
		if ( file ) {
			file->Read( &noteNumber, 4 );
			fileSystem->CloseFile( file );
		}

		int i;
		idStr str, noteNum, shotName, workName, fileName = "viewnotes/";
		idStrList fileList;

		const char *severity = NULL;
		const char *p = guiTakeNotes->State().GetString( "notefile" );
		if ( p == NULL || *p == '\0' ) {
			p = cvarSystem->GetCVarString( "ui_name" );
		}

		bool extended = guiTakeNotes->State().GetBool( "extended" );
		if ( extended ) {
			if ( guiTakeNotes->State().GetInt( "severity" ) == 1 ) {
				severity = "WishList_Viewnotes/";
			} else {
				severity = "MustFix_Viewnotes/";
			}
			fileName += severity;

			const idDecl *mapDecl = declManager->FindType(DECL_ENTITYDEF, mapSpawnData.serverInfo.GetString( "si_map" ), false );
			const idDeclEntityDef *mapInfo = static_cast<const idDeclEntityDef *>(mapDecl);

			if ( mapInfo ) {
				fileName += mapInfo->dict.GetString( "devname" );
			} else {
				fileName += mapSpawnData.serverInfo.GetString( "si_map" );
				fileName.StripFileExtension();
			}

			int count = guiTakeNotes->State().GetInt( "person_numsel" );
			if ( count == 0 ) {
				fileList.Append( fileName + "/Nobody" );
			} else {
				for ( i = 0; i < count; i++ ) {
					int person = guiTakeNotes->State().GetInt( va( "person_sel_%i", i ) );
					workName = fileName + "/";
					workName += guiTakeNotes->State().GetString( va( "person_item_%i", person ), "Nobody" );
					fileList.Append( workName );
				}
			}
		} else {
			fileName += "maps/";
			fileName += mapSpawnData.serverInfo.GetString( "si_map" );
			fileName.StripFileExtension();
			fileList.Append( fileName );
		}

		bool bCon = cvarSystem->GetCVarBool( "con_noPrint" );
		cvarSystem->SetCVarBool( "con_noPrint", true );
		for ( i = 0; i < fileList.Num(); i++ ) {
			workName = fileList[i];
			workName += "/";
			workName += p;
		//	R_ScreenshotFilename( workNote, workName, shotName );

			noteNum = shotName;
			noteNum.StripPath();
			noteNum.StripFileExtension();

			if ( severity && *severity ) {
				workName = severity;
				workName += "viewNotes";
			}

			idStr note = guiTakeNotes->State().GetString( "note" );
			workName.Replace( "\\", "\\\\" );
			workName.Replace( "\"", "\\\"" );
			noteNum.Replace( "\\", "\\\\" );
			noteNum.Replace( "\"", "\\\"" );
			note.Replace( "\\", "\\\\" );
			note.Replace( "\"", "\\\"" );

			str = "recordViewNotes \"";
			str += workName;
			str += "\" \"";
			str += noteNum;
			str += "\" \"";
			str += note;
			str += "\"\n";
			
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, str );
			cmdSystem->ExecuteCommandBuffer();

			UpdateScreen();
			//renderSystem->TakeScreenshot( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight(), shotName, 1, NULL );
		}
		noteNumber++;

		for ( int tries = 0; tries < 10; tries++ ) {
			file = fileSystem->OpenExplicitFileWrite( "p:/viewnotes/notenumber.dat" );
			if ( file != NULL ) {
				break;
			}
			Sys_Sleep( 500 );
		}
		if ( file ) {
			file->Write( &noteNumber, 4 );
			fileSystem->CloseFile( file );
		}

		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "closeViewNotes\n" );
		cvarSystem->SetCVarBool( "con_noPrint", bCon );
	}
}
