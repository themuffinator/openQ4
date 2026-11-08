// Android/SigmaTouch support by emileb: https://github.com/emileb/openQ4/tree/android
// Integrated and adapted for current openQ4; licensed under GPL-3.0-or-later.

/*
===========================================================================

openQ4 Android platform layer.

===========================================================================
*/

#ifndef __SYS_ANDROID_PUBLIC__
#define __SYS_ANDROID_PUBLIC__

// Absolute path of the APK's native library directory, exported by the host
// app's JNI glue before the engine starts. Sibling modules (game-sp,
// renderer-gles) are loaded from there.
#define OPENQ4_ANDROID_NATIVE_LIBS_ENV	"OPENQ4_NATIVE_LIBS"

// Match early native diagnostics to the host's +set fs_savepath, before the
// file system and command-line CVars are initialized.
#define OPENQ4_ANDROID_SAVE_ROOT_ENV	"OPENQ4_SAVE_ROOT"

// OpenQ4_AndroidMain plus the whole touch-input bridge live here; the header
// is engine-type-free on purpose so mobile/ can include it too.
#include "../../../mobile/quake4_bridge.h"

// Atomic lifecycle state; safe for the asynchronous tic thread to query.
bool Sys_SDL_IsAppBackgrounded( void );

// framework/UsercmdGen.cpp. Presses or releases one usercmd action, bypassing
// the key bindings, for the touch controls - which have no key to be rebound.
// Takes a usercmdGen->CommandStringUsercmdData() result.
void Sys_SetUsercmdButton( int action, bool down );
// Independent built-in touch channel. Callers run on the engine thread; the
// usercmd implementation synchronizes with asynchronous command generation.
void Sys_SetTouchMotion( float side, float forward, float yaw, float pitch, bool active, unsigned buttons, unsigned canceledButtons,
	int nextWeapon, int reload );
void Sys_ResetTouchMotion();

enum androidTouchPhase_t {
	ANDROID_TOUCH_DOWN, ANDROID_TOUCH_MOVE, ANDROID_TOUCH_UP, ANDROID_TOUCH_CANCEL
};
void AndroidTouchControls_Init();
void AndroidTouchControls_Frame( int width, int height, float density, bool appActive );
bool AndroidTouchControls_HandleFinger( androidTouchPhase_t phase, long long device, long long finger, float x, float y );
void AndroidTouchControls_OtherInput();
void AndroidTouchControls_Release();
void AndroidTouchControls_Draw();
// Lower edge of the visible top controls in SDL window coordinates; zero
// when inactive. HUD placement converts this into its own viewport.
float AndroidTouchControls_HudTop();
// Portrait: the thumb deck's top edge in SDL window coordinates, which the
// bottom readouts stay above; zero when inactive or in landscape.
float AndroidTouchControls_HudBottomEdge();

#endif
