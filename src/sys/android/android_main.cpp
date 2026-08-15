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

// Android platform layer. The Linux peer of this file (sys/linux/main.cpp) is
// not reused: most of its bulk is process spawning, /proc/cpuinfo parsing and
// freedesktop URL helpers, none of which exist or apply here, and the paths
// come from the host app rather than from the environment.

#include "../../idlib/precompiled.h"
#include "../posix/posix_public.h"
#include "../sys_local.h"

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "android_public.h"

static idStr	basepath;
static idStr	savepath;
static idStr	exepath;

/*
=================
Sys_AsyncThread
=================
*/
void Sys_AsyncThread( void ) {
	while ( 1 ) {
		if ( Sys_IsCurrentThreadStopRequested() ) {
			return;
		}

		usleep( 1000 );

		const int previousTicNumber = com_ticNumber;
		common->Async();
		for ( int tic = previousTicNumber; tic < com_ticNumber; ++tic ) {
			Sys_TriggerEvent( TRIGGER_EVENT_ONE );
		}
	}
}

/*
==============
Sys_DefaultSavePath

Everything the engine writes goes to the app's user_files/quake4 folder. The
game data folder may be read-only (SAF-backed storage) and must never be mixed
with the original install anyway.
==============
*/
const char *Sys_DefaultSavePath( void ) {
	if ( savepath.Length() ) {
		return savepath.c_str();
	}

	const char *userFiles = getenv( "USER_FILES" );
	if ( userFiles != NULL && userFiles[0] == '/' ) {
		savepath = userFiles;
		savepath.StripTrailing( '/' );
		savepath += "/quake4";
	} else {
		savepath = Posix_Cwd();
	}
	return savepath.c_str();
}

/*
==============
Sys_DefaultBasePath

The host app chdir()s into the game data folder before main() runs and passes
the same folder as +set fs_basepath, so the cwd is always the right answer.
==============
*/
const char *Sys_DefaultBasePath( void ) {
	basepath = Posix_Cwd();
	return basepath.c_str();
}

/*
==============
Sys_EXEPath

There is no executable: the engine is a shared library loaded by the app. The
only thing the engine uses this for is locating its sibling modules
(renderer-gles, game-sp), which live in the APK's native library directory, so
report a fake binary inside that directory and every StripFilename() consumer
lands on the right place.
==============
*/
const char *Sys_EXEPath( void ) {
	if ( exepath.Length() ) {
		return exepath.c_str();
	}

	const char *nativeLibs = getenv( OPENQ4_ANDROID_NATIVE_LIBS_ENV );
	if ( nativeLibs != NULL && nativeLibs[0] == '/' ) {
		exepath = nativeLibs;
		exepath.StripTrailing( '/' );
		exepath += "/openq4";
	} else {
		exepath = Posix_Cwd();
		exepath += "/openq4";
	}
	return exepath.c_str();
}

/*
==============
Sys_GetPackageRootDirectory
==============
*/
bool Sys_GetPackageRootDirectory( char *packageRoot, int packageRootSize ) {
	if ( packageRoot != NULL && packageRootSize > 0 ) {
		packageRoot[0] = '\0';
	}
	return false;
}

/*
==============
Sys_GetGameModuleRootDirectory

The native library directory, which is where the loader must find
libgame-sp_arm64.so and librenderer-gles_arm64.so.
==============
*/
bool Sys_GetGameModuleRootDirectory( char *moduleRoot, int moduleRootSize ) {
	if ( moduleRoot == NULL || moduleRootSize <= 0 ) {
		return false;
	}
	moduleRoot[0] = '\0';

	const char *nativeLibs = getenv( OPENQ4_ANDROID_NATIVE_LIBS_ENV );
	if ( nativeLibs == NULL || nativeLibs[0] != '/' ) {
		return false;
	}

	idStr::Copynz( moduleRoot, nativeLibs, moduleRootSize );
	return true;
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	basepath.Clear();
	savepath.Clear();
	exepath.Clear();
	Posix_Shutdown();
}

/*
===============
Sys_GetProcessorId
===============
*/
cpuid_t Sys_GetProcessorId( void ) {
	return CPUID_GENERIC;
}

/*
===============
Sys_GetProcessorString
===============
*/
const char *Sys_GetProcessorString( void ) {
	static bool initialized = false;
	static idStr processorString;

	if ( !initialized ) {
		const long logicalCount = sysconf( _SC_NPROCESSORS_CONF );
		processorString = Sys_FormatProcessorSummary(
			"Android",
			CPUSTRING,
			0,
			logicalCount > 0 ? (int)logicalCount : 0,
			0,
			0.0 );
		initialized = true;
	}

	return processorString.c_str();
}

/*
===============
Sys_FPU_EnableExceptions
===============
*/
void Sys_FPU_EnableExceptions( int exceptions ) {
}

/*
===============
Sys_FPE_handler
===============
*/
void Sys_FPE_handler( int signum, siginfo_t *info, void *context ) {
	assert( signum == SIGFPE );
	Sys_Printf( "FPE\n" );
}

/*
===============
Sys_GetClockTicks
===============
*/
double Sys_GetClockTicks( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double)ts.tv_sec * 1000000000.0 + (double)ts.tv_nsec;
}

/*
===============
Sys_ClockTicksPerSecond
===============
*/
double Sys_ClockTicksPerSecond( void ) {
	return 1000000000.0;
}

/*
===============
Sys_GetApproximateProcessorFrequencyHz

Display only. Android's /proc/cpuinfo carries no "cpu MHz" line and the
per-core scaling files are a moving target, so don't guess.
===============
*/
double Sys_GetApproximateProcessorFrequencyHz( void ) {
	return 0.0;
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	const long count = sysconf( _SC_PHYS_PAGES );
	const long pageSize = sysconf( _SC_PAGE_SIZE );

	if ( count <= 0 || pageSize <= 0 ) {
		common->Printf( "GetSystemRam: sysconf failed\n" );
		return 2048;
	}

	const unsigned long long bytes =
		(unsigned long long)count * (unsigned long long)pageSize;
	const unsigned long long megabytes = bytes / ( 1024ULL * 1024ULL );
	if ( megabytes == 0 ) {
		return 2048;
	}
	if ( megabytes > 65536ULL ) {
		return 65536;
	}
	return (int)megabytes;
}

/*
==================
Sys_DoStartProcess

Android apps cannot exec siblings, and nothing in the mobile build path asks
them to.
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {
	Sys_Printf( "Sys_DoStartProcess: not supported on Android ('%s')\n", exeName ? exeName : "" );
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	common->Printf( "OpenURL: not supported on Android (%s)\n", url ? url : "" );
}

/*
 ==================
 Sys_DoPreferences
 ==================
 */
void Sys_DoPreferences( void ) { }

/*
================
Sys_FPU_SetDAZ
================
*/
void Sys_FPU_SetDAZ( bool enable ) { }

/*
================
Sys_FPU_SetFTZ
================
*/
void Sys_FPU_SetFTZ( bool enable ) { }

/*
===============
Call stacks

bionic has no <execinfo.h>, so these are the same no-ops sys/linux/stack.cpp
compiles under ID_BT_STUB. Native crashes are far better served by the NDK's
own tombstones anyway.
===============
*/
void Sys_ShutdownSymbols( void ) { }

void Sys_GetCallStack( address_t *callStack, const int callStackSize ) {
	for ( int i = 0; i < callStackSize; i++ ) {
		callStack[i] = 0;
	}
}

const char *Sys_GetCallStackStr( const address_t *callStack, const int callStackSize ) {
	return "";
}

const char *Sys_GetCallStackCurStr( int depth ) {
	return "";
}

const char *Sys_GetCallStackCurAddressStr( int depth ) {
	return "";
}

/*
===============
Android entry point

Called once from the host app's JNI glue (mobile/game_interface.cpp) on the
thread the engine then owns for the rest of the process's life. Never returns,
exactly like the desktop main().
===============
*/
static void Sys_HandlePendingQuitSignal( void ) {
	const int quitSignal = Posix_ConsumeQuitSignal();
	if ( quitSignal == 0 ) {
		return;
	}

	Posix_SetExit( 128 + quitSignal );
	common->Printf( "Exiting on %s\n", Posix_SignalName( quitSignal ) );
	common->Quit();
}

extern "C" int OpenQ4_AndroidMain( int argc, const char **argv ) {
	Posix_EarlyInit();

	if ( argc > 1 && argv != NULL ) {
		common->Init( argc - 1, &argv[1], NULL );
	} else {
		common->Init( 0, NULL, NULL );
	}

	Sys_HandlePendingQuitSignal();
	Posix_LateInit();

	while ( 1 ) {
		Sys_HandlePendingQuitSignal();
		common->Frame();
	}

	return 0;
}
