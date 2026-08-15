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

// The OpenTouch touch layer (Clibs_OpenTouch/touch_interface_base.cpp) reaches
// SDL_StartTextInput through this global, which every SDL3 engine in the app is
// expected to export. Sys_SDL_PumpEvents keeps it pointed at the game window.
extern "C" SDL_Window *window = NULL;

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

extern "C" void Quake4_PostKey( int sdlScancode, int down ) {
	const int key = SDL3_MapScancode( (SDL_Scancode)sdlScancode );
	if ( key <= 0 || key >= K_LAST_KEY ) {
		return;
	}
	const int eventTime = Sys_Milliseconds();
	Sys_QueEvent( eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL );
	SDL3_QueueKeyboardInput( key, down, eventTime );
}

extern "C" void Quake4_PostChar( int codepoint ) {
	if ( codepoint <= 0 ) {
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
	SDL3_QueueMouseButtonEvent( K_MOUSE1 + ( button - 1 ), down != 0, Sys_Milliseconds(), true );
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
	s_joystickAxisState[ axis ] = idMath::ClampChar( value );
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
	usercmdGen->TriggerImpulse( impulse );
}

/*
====================
Quake4_PostCommand

The rebinding-proof path for gameplay actions: idTech4's impulse commands
("_forward", "_attack", ...) are what a bound key ultimately runs, so calling
them directly does the right thing no matter what the player rebound.
====================
*/
extern "C" void Quake4_PostCommand( const char *cmd ) {
	if ( cmd == NULL || cmd[0] == '\0' || cmdSystem == NULL ) {
		return;
	}
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, cmd );
}

/*
====================
Quake4_GetScreenMode

Mirrors touchscreemode_t in Clibs_OpenTouch/game_interface.h.
====================
*/
extern "C" int Quake4_GetScreenMode( void ) {
	enum { TS_BLANK = 0, TS_MENU = 1, TS_GAME = 2, TS_MAP = 3, TS_CONSOLE = 4 };

	if ( console != NULL && console->Active() ) {
		return TS_CONSOLE;
	}
	// IsGUIActive() covers both the front-end menus and in-game GUIs, which is
	// the same distinction the overlay wants.
	if ( session == NULL || session->IsGUIActive() ) {
		return TS_MENU;
	}
	return TS_GAME;
}
