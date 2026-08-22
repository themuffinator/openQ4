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

// OpenQ4_AndroidMain plus the whole touch-input bridge live here; the header
// is engine-type-free on purpose so mobile/ can include it too.
#include "../../../mobile/quake4_bridge.h"

// framework/UsercmdGen.cpp. Presses or releases one usercmd action, bypassing
// the key bindings, for the touch controls - which have no key to be rebound.
// Takes a usercmdGen->CommandStringUsercmdData() result.
void Sys_SetUsercmdButton( int action, bool down );

#endif
