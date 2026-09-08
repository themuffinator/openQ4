// Android/SigmaTouch support by emileb: https://github.com/emileb/openQ4/tree/android
// Integrated and adapted for current openQ4; licensed under GPL-3.0-or-later.

#ifndef QUAKE4_BRIDGE_H
#define QUAKE4_BRIDGE_H

/*
===============================================================================

	The seam between the OpenTouch host glue and the engine.

	The touch overlay's callbacks arrive on Android's UI thread while the
	engine owns its own thread, and neither the framework event queue
	(Posix_QueEvent) nor engine state is safe to touch from outside. So the
	Portable* functions in mobile/game_interface.cpp use a mutex-protected
	queue, and Quake4_DrainTouchInput() - called once per event pump from the
	engine thread - is the only thing that turns those entries into real input.

	Deliberately free of engine types, so mobile/ needs no engine headers and
	the shared Clibs glue keeps compiling without the precompiled prefix.

===============================================================================
*/

#ifdef __cplusplus
extern "C" {
#endif

// sys/android/android_main.cpp. Runs common->Init() and the frame loop; never
// returns.
int OpenQ4_AndroidMain( int argc, const char **argv );

// mobile/game_interface.cpp; called from Sys_SDL_PumpEvents.
void Quake4_DrainTouchInput( void );
// Drop pending host input on focus/background transitions; thread-safe.
void Quake4_ClearTouchInput( void );
// Engine-thread-only release of already delivered touch inputs.
void Quake4_ResetTouchState( void );
// Publish the engine's current screen mode for host/UI-thread readers.
void Quake4_UpdateTouchScreenMode( void );
// Engine-thread lookup for touch editor labels; NULL until engine initialization.
const char *Quake4_LocalizeString( const char *stringId );

/*
	Everything below is implemented in sys/android/android_sdl3.cpp and posts input on the engine thread only. Quake4_GetScreenMode returns an
	atomic snapshot for UI-thread readers.
*/

void Quake4_PostKey( int sdlScancode, int down );
void Quake4_PostChar( int codepoint );
void Quake4_PostMouseDelta( int dx, int dy );
void Quake4_PostMouseButton( int button, int down );	// 1 = left, 2 = right, 3 = middle

// Analog move and look. Axis is one of QUAKE4_AXIS_*, value is -127..127 and
// is a level, not a delta: it stays applied until it is set again.
void Quake4_PostJoystickAxis( int axis, int value );

#define QUAKE4_AXIS_SIDE	0
#define QUAKE4_AXIS_FORWARD	1
#define QUAKE4_AXIS_UP		2
#define QUAKE4_AXIS_ROLL	3
#define QUAKE4_AXIS_YAW		4
#define QUAKE4_AXIS_PITCH	5

// Runs a console command ("togglemenu", "savegame quick", ...).
void Quake4_PostCommand( const char *cmd );

// Fires an _impulseNN action directly, so it works no matter what the player
// has that impulse bound to. Quake 4's weapon slots are impulses 0-10, reload
// is 13, next/prev weapon 14/15, flashlight 50, last weapon 51. Objectives is
// not an impulse at all despite the bind name - see QUAKE4_BTN_SCORES.
void Quake4_TriggerImpulse( int impulse );

// Presses or releases a held gameplay action on the usercmd itself, so it works
// no matter which key - if any - the player has that action bound to. Button is
// one of QUAKE4_BTN_*.
void Quake4_PostButton( int button, int down );

#define QUAKE4_BTN_ATTACK		0
#define QUAKE4_BTN_ZOOM			1
#define QUAKE4_BTN_MOVE_UP		2	// jump
#define QUAKE4_BTN_MOVE_DOWN	3	// crouch
#define QUAKE4_BTN_SPEED		4	// run / walk
#define QUAKE4_BTN_STRAFE		5
#define QUAKE4_BTN_WEAPON_WHEEL	6
#define QUAKE4_BTN_SCORES		7	// objectives (SP) / scoreboard (MP)
#define QUAKE4_BTN_COUNT		8

// Menu/console vs in-game, so the overlay can pick its control screen. Values
// match touchscreemode_t in Clibs_OpenTouch/game_interface.h.
int Quake4_GetScreenMode( void );

#ifdef __cplusplus
}
#endif

#endif
