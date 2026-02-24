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

// -*- mode: objc -*-
#import "../../idlib/precompiled.h"

#include "../posix/posix_public.h"

#import <AppKit/AppKit.h>
#import <OpenGL/OpenGL.h>
#import <mach/mach_time.h>
#import <pthread.h>
#import <sys/sysctl.h>
#import <unistd.h>

#import "macosx_local.h"

static idStr	basepath;
static idStr	savepath;

/*
=================
Sys_AsyncThread
=================
*/
void Sys_AsyncThread( void ) {
	int now;
	int next;
	int want_sleep;
	int ticked;
	int to_ticked;

	now = Sys_Milliseconds();
	ticked = now >> 4;
	while ( 1 ) {
		now = Sys_Milliseconds();
		next = ( now & 0xFFFFFFF0 ) + 0x10;
		want_sleep = ( next - now - 1 ) * 1000;
		if ( want_sleep > 0 ) {
			usleep( want_sleep );
		}

		now = Sys_Milliseconds();
		to_ticked = now >> 4;
		while ( ticked < to_ticked ) {
			common->Async();
			ticked++;
			Sys_TriggerEvent( TRIGGER_EVENT_ONE );
		}

		pthread_testcancel();
	}
}

/*
==============
Sys_EXEPath
==============
*/
const char *Sys_EXEPath( void ) {
	static char exePath[1024];
	const char *bundlePath = [[[NSBundle mainBundle] bundlePath] fileSystemRepresentation];
	idStr::Copynz( exePath, bundlePath != NULL ? bundlePath : Posix_Cwd(), sizeof( exePath ) );
	return exePath;
}

/*
==============
Sys_DefaultSavePath
==============
*/
const char *Sys_DefaultSavePath( void ) {
	const char *home = [NSHomeDirectory() fileSystemRepresentation];
	if ( home != NULL && home[0] != '\0' ) {
#if defined( ID_DEMO_BUILD )
		savepath = va( "%s/Library/Application Support/OpenQ4 Demo", home );
#else
		savepath = va( "%s/Library/Application Support/OpenQ4", home );
#endif
	} else {
		savepath = Posix_Cwd();
	}
	return savepath.c_str();
}

/*
==============
Sys_DefaultBasePath
==============
*/
const char *Sys_DefaultBasePath( void ) {
	basepath = Sys_EXEPath();
	if ( basepath.Length() > 0 ) {
		basepath.StripFilename();
	}
	if ( basepath.Length() == 0 ) {
		basepath = Posix_Cwd();
	}
	return basepath.c_str();
}

/*
===============
Sys_GetProcessorId
===============
*/
cpuid_t Sys_GetProcessorId( void ) {
	cpuid_t cpuid = CPUID_GENERIC;
#if defined( __i386__ ) || defined( __x86_64__ )
	cpuid = (cpuid_t)( cpuid | CPUID_INTEL | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | CPUID_SSE3 | CPUID_HTT | CPUID_CMOV | CPUID_FTZ | CPUID_DAZ );
#elif defined( __ppc__ ) || defined( __ppc64__ )
	cpuid = (cpuid_t)( cpuid | CPUID_ALTIVEC );
#endif
	return cpuid;
}

/*
===============
Sys_GetProcessorString
===============
*/
const char *Sys_GetProcessorString( void ) {
#if defined( __aarch64__ ) || defined( __arm64__ )
	return "arm64 CPU";
#elif defined( __i386__ ) || defined( __x86_64__ )
	return "x86 CPU with MMX/SSE/SSE2/SSE3 extensions";
#elif defined( __ppc__ ) || defined( __ppc64__ )
	return "ppc CPU with AltiVec extensions";
#else
	return "generic CPU";
#endif
}

/*
===============
Sys_FPU_EnableExceptions
===============
*/
void Sys_FPU_EnableExceptions( int exceptions ) {
	(void)exceptions;
}

/*
===============
Sys_FPE_handler
===============
*/
void Sys_FPE_handler( int signum, siginfo_t *info, void *context ) {
	(void)signum;
	(void)info;
	(void)context;
}

/*
===============
Sys_GetClockTicks
===============
*/
double Sys_GetClockTicks( void ) {
	return (double)mach_absolute_time();
}

/*
===============
Sys_ClockTicksPerSecond
===============
*/
double Sys_ClockTicksPerSecond( void ) {
	static double ticksPerSecond = 0.0;
	if ( ticksPerSecond == 0.0 ) {
		mach_timebase_info_data_t timebase;
		timebase.numer = 0;
		timebase.denom = 0;
		if ( mach_timebase_info( &timebase ) == KERN_SUCCESS && timebase.numer != 0 && timebase.denom != 0 ) {
			const double nsPerTick = (double)timebase.numer / (double)timebase.denom;
			ticksPerSecond = 1000000000.0 / nsPerTick;
		} else {
			ticksPerSecond = 1000000000.0;
		}
	}
	return ticksPerSecond;
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	uint64_t memSizeBytes = 0;
	size_t len = sizeof( memSizeBytes );
	if ( sysctlbyname( "hw.memsize", &memSizeBytes, &len, NULL, 0 ) == 0 && memSizeBytes > 0 ) {
		return (int)( memSizeBytes / ( 1024ULL * 1024ULL ) );
	}
	return 1024;
}

/*
================
Sys_GetVideoRam
returns in megabytes
================
*/
int Sys_GetVideoRam( void ) {
	CGLRendererInfoObj rendererInfo = NULL;
	GLint rendererCount = 0;
	GLint maxVRAM = 0;

	CGLError err = CGLQueryRendererInfo(
		CGDisplayIDToOpenGLDisplayMask( Sys_DisplayToUse() ),
		&rendererInfo,
		&rendererCount );
	if ( err != kCGLNoError || rendererInfo == NULL ) {
		return 0;
	}

	for ( GLint rendererIndex = 0; rendererIndex < rendererCount; rendererIndex++ ) {
		GLint accelerated = 0;
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPAccelerated, &accelerated );
		if ( err != kCGLNoError || !accelerated ) {
			continue;
		}

		GLint vramMB = 0;
#if defined( kCGLRPVideoMemoryMegabytes )
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPVideoMemoryMegabytes, &vramMB );
		if ( err == kCGLNoError ) {
			if ( vramMB > maxVRAM ) {
				maxVRAM = vramMB;
			}
			continue;
		}
#endif
		GLint vramBytes = 0;
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPVideoMemory, &vramBytes );
		if ( err == kCGLNoError ) {
			vramMB = vramBytes / ( 1024 * 1024 );
			if ( vramMB > maxVRAM ) {
				maxVRAM = vramMB;
			}
		}
	}

	(void)CGLDestroyRendererInfo( rendererInfo );
	return maxVRAM;
}

/*
========================
OSX_GetCPUIdentification
========================
*/
bool OSX_GetCPUIdentification( int& cpuId, bool& oldArchitecture ) {
	cpuId = (int)Sys_GetProcessorId();
	oldArchitecture = false;
	return true;
}

/*
================
OSX_GetVideoCard
================
*/
void OSX_GetVideoCard( int& outVendorId, int& outDeviceId ) {
	outVendorId = -1;
	outDeviceId = -1;
}

/*
=================
Sys_DoPreferences
=================
*/
void Sys_DoPreferences( void ) {
}

/*
==================
Sys_ShutdownSymbols
==================
*/
void Sys_ShutdownSymbols( void ) {
}

/*
====================
Sys_SetClipboardData
====================
*/
void Sys_SetClipboardData( const char *string ) {
	NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
	[pasteboard clearContents];

	NSString *clipboardString = @"";
	if ( string != NULL ) {
		clipboardString = [NSString stringWithUTF8String:string];
		if ( clipboardString == nil ) {
			clipboardString = [NSString stringWithCString:string encoding:NSISOLatin1StringEncoding];
		}
		if ( clipboardString == nil ) {
			clipboardString = @"";
		}
	}

	[pasteboard setString:clipboardString forType:NSPasteboardTypeString];
}

/*
===============
Sys_FPU_SetDAZ
===============
*/
void Sys_FPU_SetDAZ( bool enable ) {
	(void)enable;
}

/*
===============
Sys_FPU_SetFTZ
===============
*/
void Sys_FPU_SetFTZ( bool enable ) {
	(void)enable;
}
