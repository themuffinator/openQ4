// Android/SigmaTouch support by emileb: https://github.com/emileb/openQ4/tree/android
// Integrated and adapted for current openQ4; licensed under GPL-3.0-or-later.

/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

===========================================================================
*/

// Android's flavour of the shared SDL3 backend. Structurally the same trick as
// sys/linux/linux_sdl3.cpp and sys/osx/macosx_sdl3.cpp: define the host tag,
// include the backend translation unit, then supply the handful of
// platform-specific entry points it leaves to the host.

#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"
#include "android_public.h"

#include <SDL3/SDL.h>
#include <atomic>

// The OpenTouch touch layer (Clibs_OpenTouch/touch_interface_base.cpp) reaches
// SDL_StartTextInput through this global, which every SDL3 engine in the app is
// expected to export. Sys_SDL_PumpEvents keeps it pointed at the game window.
#if defined( OPENQ4_SIGMATOUCH )
extern "C" { SDL_Window *window = NULL; }
#endif

#define OPENQ4_SDL3_ANDROID_HOST 1
#include "../sdl3/sdl3_backend.cpp"

idCVar sys_videoRam(
	"sys_videoRam",
	"0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"Texture memory on the video card (in megabytes) - 0: autodetect",
	0,
	65536
);

// The GLES renderer module resolves its entry points from the Khronos headers
// against the system driver; there is no GLEW-style loader to bring up.
bool QGL_Init( const char *dllname ) {
	(void)dllname;
	return true;
}

void QGL_Shutdown( void ) {
}

bool Sys_GetDesktopResolution( int *width, int *height ) {
	return SDL3_QueryDesktopResolution( width, height, "SDL3 Android" );
}

/*
================
Sys_GetVideoRam

Mobile GPUs share system memory and expose no VRAM total. Report a quarter of
system RAM, which is roughly the budget a game can actually use for textures,
unless the user pinned a value.
================
*/
int Sys_GetVideoRam( void ) {
	static int cachedVideoRam = 0;
	if ( cachedVideoRam != 0 ) {
		return cachedVideoRam;
	}

	if ( sys_videoRam.GetInteger() > 0 ) {
		cachedVideoRam = sys_videoRam.GetInteger();
		return cachedVideoRam;
	}

	cachedVideoRam = Sys_GetSystemRam() / 4;
	if ( cachedVideoRam < 256 ) {
		cachedVideoRam = 256;
	}
	common->Printf( "shared-memory GPU, assuming %d MB of texture budget ( use +set sys_videoRam to force )\n", cachedVideoRam );
	return cachedVideoRam;
}

/*
===============================================================================

	Touch input injection (the engine half of mobile/quake4_bridge.h).

	These reach the same two sinks a real SDL event would: the framework event
	queue that drives the console and menus, and the polled input queues the
	async input path drains. Engine thread only.

===============================================================================
*/

#if defined( OPENQ4_SIGMATOUCH )
// Mirrors touchscreemode_t in Clibs_OpenTouch/game_interface.h.
enum { TS_BLANK = 0, TS_MENU = 1, TS_GAME = 2, TS_MAP = 3, TS_CONSOLE = 4 };

static bool touchKeysDown[K_LAST_KEY] = { false };
static bool touchMouseDown[3] = { false };
static bool touchButtonsDown[QUAKE4_BTN_COUNT] = { false };
static std::atomic<int> touchScreenMode(TS_MENU);

extern "C" void Quake4_PostKey( int sdlScancode, int down ) {
	const int key = SDL3_MapScancode( (SDL_Scancode)sdlScancode );
	if ( key <= 0 || key >= K_LAST_KEY ) {
		return;
	}
	touchKeysDown[key] = down != 0;
	const int eventTime = Sys_Milliseconds();
	Sys_QueEvent( eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL );
	SDL3_QueueKeyboardInput( key, down, eventTime );
}

extern "C" void Quake4_PostChar( int codepoint ) {
	if ( codepoint <= 0 || codepoint > 0xff || !idStr::CharIsPrintable( static_cast<byte>(codepoint) ) ) {
		return;
	}
	Sys_QueEvent( Sys_Milliseconds(), SE_CHAR, codepoint, 0, 0, NULL );
}

extern "C" void Quake4_PostMouseDelta( int dx, int dy ) {
	if ( dx == 0 && dy == 0 ) {
		return;
	}
	SDL3_QueueMouseDelta( dx, dy, Sys_Milliseconds() );
}

extern "C" void Quake4_PostMouseButton( int button, int down ) {
	if ( button < 1 || button > 3 ) {
		return;
	}
	// OpenTouch's button ordinals are primary, secondary, middle; SDL's native
	// constants have a different order, so map to engine keys explicitly.
	static const int keys[] = { K_MOUSE1, K_MOUSE2, K_MOUSE3 };
	touchMouseDown[button - 1] = down != 0;
	SDL3_QueueMouseButtonEvent( keys[button - 1], down != 0, Sys_Milliseconds(), true );
}

/*
====================
Quake4_PostJoystickAxis

Sets the level the joystick poll queue reports for one axis. This is the analog
path idUsercmdGen::JoystickMove() reads, so touch sticks drive movement and
look through the engine's own sensitivity handling rather than through
synthetic key presses.
====================
*/
extern "C" void Quake4_PostJoystickAxis( int axis, int value ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		return;
	}
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	s_touchAxisState[ axis ] = idMath::ClampChar( value );
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
}

/*
====================
Quake4_TriggerImpulse

Rebinding-proof: the engine resolves impulses by number, not by which key the
player happens to have bound to them.
====================
*/
extern "C" void Quake4_TriggerImpulse( int impulse ) {
	if ( usercmdGen == NULL || impulse < 0 ) {
		return;
	}
	Sys_EnterCriticalSection();
	usercmdGen->TriggerImpulse( impulse );
	Sys_LeaveCriticalSection();
}

/*
====================
Quake4_PostButton

The rebinding-proof path for held gameplay buttons. These action names are what
a bound key resolves to on its way into the usercmd, so naming the action does
the same press without needing a key that still has it bound.
====================
*/
extern "C" void Quake4_PostButton( int button, int down ) {
	static const char *actionNames[QUAKE4_BTN_COUNT] = {
		"_attack",			// QUAKE4_BTN_ATTACK
		"_zoom",			// QUAKE4_BTN_ZOOM
		"_moveUp",			// QUAKE4_BTN_MOVE_UP
		"_moveDown",		// QUAKE4_BTN_MOVE_DOWN
		"_speed",			// QUAKE4_BTN_SPEED
		"_strafe",			// QUAKE4_BTN_STRAFE
		"_weaponWheel",		// QUAKE4_BTN_WEAPON_WHEEL
		"_showScores"		// QUAKE4_BTN_SCORES
	};

	if ( usercmdGen == NULL || button < 0 || button >= QUAKE4_BTN_COUNT ) {
		return;
	}

	touchButtonsDown[button] = down != 0;
	Sys_SetUsercmdButton( usercmdGen->CommandStringUsercmdData( actionNames[ button ] ), down != 0 );
}

/*
====================
Quake4_PostCommand

Runs a real console command ("savegame quick", ...). Note this is not a route to
the _attack / _moveUp family: those are usercmd actions resolved from a key's
binding, not registered commands - Quake4_PostButton is their path.
====================
*/
extern "C" void Quake4_PostCommand( const char *cmd ) {
	if ( cmd == NULL || cmd[0] == '\0' || cmdSystem == NULL ) {
		return;
	}
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, cmd );
	// Terminate the command: APPEND inserts raw text, so without this two queued
	// commands run together into one garbled line.
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
}

/*
====================
Quake4_FatalConsoleActive / Quake4_FatalConsoleDismiss

The host input thread has no other way to reach the fatal-error wait: the touch
overlay queues into a ring buffer that only Sys_SDL_PumpEvents drains, and that
is not running once Sys_Error has taken over.
====================
*/
extern "C" int Quake4_FatalConsoleActive( void ) {
	return Posix_ConsoleFatalErrorActive() ? 1 : 0;
}

extern "C" void Quake4_FatalConsoleDismiss( void ) {
	Posix_ConsoleRequestFatalDismiss();
}

extern "C" void Quake4_UpdateTouchScreenMode( void ) {
	if ( console != NULL && console->Active() ) {
		touchScreenMode.store(TS_CONSOLE);
		return;
	}
	// IsGUIActive() covers both the front-end menus and in-game GUIs, which is
	// the same distinction the overlay wants.
	if ( session == NULL || session->IsGUIActive() ) {
		touchScreenMode.store(TS_MENU);
		return;
	}
	touchScreenMode.store(TS_GAME);
}

extern "C" int Quake4_GetScreenMode( void ) {
	// Read here rather than in the update above: the fatal path has taken over
	// from the event pump, so the published mode would never change again.
	//
	// The fatal-error console owns the screen and the process is on its way
	// out, so no game or menu pad means anything from here. TS_BLANK is the
	// overlay's own empty set -- one full-screen invisible button that sends
	// enter -- so the error text stays readable and a tap still dismisses it.
	//
	// Any other mode draws real controls, and they are drawn with GL state set
	// up for the game window the fatal path has just hidden: the pads come out
	// opaque because nothing clears behind them, and they fight the console's
	// own render loop for the surface, which is the flicker.
	if ( Posix_ConsoleFatalErrorActive() ) {
		return TS_BLANK;
	}
	return touchScreenMode.load();
}

extern "C" void Quake4_ResetTouchState( void ) {
	for (int key = 1; key < K_LAST_KEY; ++key) {
		if (touchKeysDown[key]) {
			const int eventTime = Sys_Milliseconds();
			Sys_QueEvent(eventTime, SE_KEY, key, 0, 0, NULL);
			SDL3_QueueKeyboardInput(key, false, eventTime);
			touchKeysDown[key] = false;
		}
	}
	for (int button = 0; button < 3; ++button) {
		if (touchMouseDown[button]) Quake4_PostMouseButton(button + 1, false);
	}
	for (int button = 0; button < QUAKE4_BTN_COUNT; ++button) {
		if (touchButtonsDown[button]) Quake4_PostButton(button, false);
	}
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	memset(s_touchAxisState, 0, sizeof(s_touchAxisState));
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
}

extern "C" const char *Quake4_LocalizeString( const char *stringId ) {
	// The external host builds its controls before PortableInit/common->Init.
	// In particular, a missing string lookup there would call idLib::common
	// before that interface pointer exists. Resolve labels from newFrame only
	// after engine startup, on the same thread that owns the language dictionary.
	if ( common == NULL || !common->IsInitialized() ) {
		return NULL;
	}
	return common->GetLanguageDict()->GetString(stringId);
}
#endif
