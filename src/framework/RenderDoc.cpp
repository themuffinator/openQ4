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

#include "RenderDoc.h"

#if defined( _WIN32 )
#include <windows.h>
#elif defined( __linux__ ) || defined( __APPLE__ )
#include <dlfcn.h>
#endif

#if defined( _WIN32 )
#define OPENQ4_RENDERDOC_CC __cdecl
#else
#define OPENQ4_RENDERDOC_CC
#endif

typedef void *RENDERDOC_DevicePointer;
typedef void *RENDERDOC_WindowHandle;

typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_GetAPIVersion )( int *major, int *minor, int *patch );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_SetCaptureFilePathTemplate )( const char *pathTemplate );
typedef const char *( OPENQ4_RENDERDOC_CC *pRENDERDOC_GetCaptureFilePathTemplate )( void );
typedef uint32_t ( OPENQ4_RENDERDOC_CC *pRENDERDOC_GetNumCaptures )( void );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_TriggerCapture )( void );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_TriggerMultiFrameCapture )( uint32_t numFrames );
typedef uint32_t ( OPENQ4_RENDERDOC_CC *pRENDERDOC_IsTargetControlConnected )( void );
typedef uint32_t ( OPENQ4_RENDERDOC_CC *pRENDERDOC_LaunchReplayUI )( uint32_t connectTargetControl, const char *cmdLine );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_SetActiveWindow )( RENDERDOC_DevicePointer device, RENDERDOC_WindowHandle wndHandle );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_StartFrameCapture )( RENDERDOC_DevicePointer device, RENDERDOC_WindowHandle wndHandle );
typedef uint32_t ( OPENQ4_RENDERDOC_CC *pRENDERDOC_IsFrameCapturing )( void );
typedef uint32_t ( OPENQ4_RENDERDOC_CC *pRENDERDOC_EndFrameCapture )( RENDERDOC_DevicePointer device, RENDERDOC_WindowHandle wndHandle );
typedef int ( OPENQ4_RENDERDOC_CC *pRENDERDOC_GetAPI )( int version, void **outAPIPointers );
typedef void ( OPENQ4_RENDERDOC_CC *pRENDERDOC_VoidFn )( void );

enum {
	eRENDERDOC_API_Version_1_6_0 = 10600
};

typedef struct RENDERDOC_API_1_6_0 {
	pRENDERDOC_GetAPIVersion				GetAPIVersion;
	pRENDERDOC_VoidFn						SetCaptureOptionU32;
	pRENDERDOC_VoidFn						SetCaptureOptionF32;
	pRENDERDOC_VoidFn						GetCaptureOptionU32;
	pRENDERDOC_VoidFn						GetCaptureOptionF32;
	pRENDERDOC_VoidFn						SetFocusToggleKeys;
	pRENDERDOC_VoidFn						SetCaptureKeys;
	pRENDERDOC_VoidFn						GetOverlayBits;
	pRENDERDOC_VoidFn						MaskOverlayBits;
	pRENDERDOC_VoidFn						RemoveHooks;
	pRENDERDOC_VoidFn						UnloadCrashHandler;
	pRENDERDOC_SetCaptureFilePathTemplate	SetCaptureFilePathTemplate;
	pRENDERDOC_GetCaptureFilePathTemplate	GetCaptureFilePathTemplate;
	pRENDERDOC_GetNumCaptures				GetNumCaptures;
	pRENDERDOC_VoidFn						GetCapture;
	pRENDERDOC_TriggerCapture				TriggerCapture;
	pRENDERDOC_IsTargetControlConnected		IsTargetControlConnected;
	pRENDERDOC_LaunchReplayUI				LaunchReplayUI;
	pRENDERDOC_SetActiveWindow				SetActiveWindow;
	pRENDERDOC_StartFrameCapture			StartFrameCapture;
	pRENDERDOC_IsFrameCapturing				IsFrameCapturing;
	pRENDERDOC_EndFrameCapture				EndFrameCapture;
	pRENDERDOC_TriggerMultiFrameCapture		TriggerMultiFrameCapture;
	pRENDERDOC_VoidFn						SetCaptureFileComments;
	pRENDERDOC_VoidFn						DiscardFrameCapture;
	pRENDERDOC_VoidFn						ShowReplayUI;
	pRENDERDOC_VoidFn						SetCaptureTitle;
} RENDERDOC_API_1_6_0;

struct openq4RenderDocState_t {
	void					*moduleHandle;
	bool					moduleHandleNeedsRelease;
	bool					apiChecked;
	RENDERDOC_API_1_6_0		*api;
	int						apiMajor;
	int						apiMinor;
	int						apiPatch;
};

static openq4RenderDocState_t renderDocState;

static bool RenderDoc_ModuleLoaded( void ) {
#if defined( _WIN32 )
	return GetModuleHandleA( "renderdoc.dll" ) != NULL;
#elif defined( __linux__ )
#if defined( RTLD_NOLOAD )
	void *handle = dlopen( "librenderdoc.so", RTLD_NOW | RTLD_NOLOAD );
	if ( handle == NULL ) {
		handle = dlopen( "librenderdoc.so.1", RTLD_NOW | RTLD_NOLOAD );
	}
	if ( handle != NULL ) {
		dlclose( handle );
		return true;
	}
	return false;
#else
	return false;
#endif
#elif defined( __APPLE__ )
#if defined( RTLD_NOLOAD )
	void *handle = dlopen( "librenderdoc.dylib", RTLD_NOW | RTLD_NOLOAD );
	if ( handle != NULL ) {
		dlclose( handle );
		return true;
	}
	return false;
#else
	return false;
#endif
#else
	return false;
#endif
}

static void *RenderDoc_FindModule( void ) {
#if defined( _WIN32 )
	return (void *)GetModuleHandleA( "renderdoc.dll" );
#elif defined( __linux__ )
#if defined( RTLD_NOLOAD )
	void *handle = dlopen( "librenderdoc.so", RTLD_NOW | RTLD_NOLOAD );
	if ( handle == NULL ) {
		handle = dlopen( "librenderdoc.so.1", RTLD_NOW | RTLD_NOLOAD );
	}
	return handle;
#else
	return NULL;
#endif
#elif defined( __APPLE__ )
#if defined( RTLD_NOLOAD )
	return dlopen( "librenderdoc.dylib", RTLD_NOW | RTLD_NOLOAD );
#else
	return NULL;
#endif
#else
	return NULL;
#endif
}

static void *RenderDoc_GetProcAddress( void *moduleHandle, const char *name ) {
	if ( moduleHandle == NULL || name == NULL ) {
		return NULL;
	}

#if defined( _WIN32 )
	return (void *)GetProcAddress( (HMODULE)moduleHandle, name );
#elif defined( __linux__ ) || defined( __APPLE__ )
	return dlsym( moduleHandle, name );
#else
	return NULL;
#endif
}

bool RenderDoc_IsInjected( void ) {
	return RenderDoc_ModuleLoaded();
}

static bool RenderDoc_InitAPI( void ) {
	if ( renderDocState.apiChecked ) {
		return renderDocState.api != NULL;
	}

	renderDocState.apiChecked = true;
	renderDocState.moduleHandle = RenderDoc_FindModule();
	if ( renderDocState.moduleHandle == NULL ) {
		return false;
	}

#if defined( __linux__ ) || defined( __APPLE__ )
	renderDocState.moduleHandleNeedsRelease = true;
#else
	renderDocState.moduleHandleNeedsRelease = false;
#endif

	pRENDERDOC_GetAPI getApi =
		(pRENDERDOC_GetAPI)RenderDoc_GetProcAddress( renderDocState.moduleHandle, "RENDERDOC_GetAPI" );
	if ( getApi == NULL ) {
		return false;
	}

	void *apiPointer = NULL;
	if ( getApi( eRENDERDOC_API_Version_1_6_0, &apiPointer ) != 1 || apiPointer == NULL ) {
		return false;
	}

	renderDocState.api = (RENDERDOC_API_1_6_0 *)apiPointer;
	if ( renderDocState.api->GetAPIVersion != NULL ) {
		renderDocState.api->GetAPIVersion(
			&renderDocState.apiMajor,
			&renderDocState.apiMinor,
			&renderDocState.apiPatch );
	}

	return true;
}

static bool RenderDoc_UpdateCaptureTemplate( idStr &captureTemplate ) {
	captureTemplate.Clear();

	if ( !RenderDoc_InitAPI() || fileSystem == NULL ) {
		return false;
	}

	idStr captureDir = fileSystem->RelativePathToOSPath( "renderdoc", "fs_savepath" );
	if ( captureDir.Length() == 0 ) {
		return false;
	}

	fileSystem->CreateOSPath( captureDir.c_str() );

	captureTemplate = captureDir;
	captureTemplate.AppendPath( "openq4" );

	if ( renderDocState.api->SetCaptureFilePathTemplate != NULL ) {
		renderDocState.api->SetCaptureFilePathTemplate( captureTemplate.c_str() );
	}

	return true;
}

static void RenderDoc_PrintUnavailable( void ) {
	common->Printf(
		"RenderDoc API unavailable. Launch openQ4 through RenderDoc first "
		"(for example tools/debug/renderdoc_capture.ps1 or renderdoccmd capture).\n" );
}

static void RenderDoc_Status_f( const idCmdArgs &args ) {
	(void)args;

	if ( !RenderDoc_InitAPI() ) {
		RenderDoc_PrintUnavailable();
		return;
	}

	idStr captureTemplate;
	RenderDoc_UpdateCaptureTemplate( captureTemplate );

	const bool targetControlConnected =
		( renderDocState.api->IsTargetControlConnected != NULL ) &&
		( renderDocState.api->IsTargetControlConnected() != 0 );
	const uint32_t captureCount =
		( renderDocState.api->GetNumCaptures != NULL )
			? renderDocState.api->GetNumCaptures()
			: 0;
	const char *activeTemplate =
		( renderDocState.api->GetCaptureFilePathTemplate != NULL )
			? renderDocState.api->GetCaptureFilePathTemplate()
			: NULL;

	common->Printf(
		"RenderDoc API %d.%d.%d active. target control: %s. session captures: %u\n",
		renderDocState.apiMajor,
		renderDocState.apiMinor,
		renderDocState.apiPatch,
		targetControlConnected ? "connected" : "not connected",
		captureCount );

	if ( captureTemplate.Length() > 0 ) {
		common->Printf( "RenderDoc capture template: %s\n", captureTemplate.c_str() );
	} else if ( activeTemplate != NULL && activeTemplate[0] != '\0' ) {
		common->Printf( "RenderDoc capture template: %s\n", activeTemplate );
	}
}

static void RenderDoc_Capture_f( const idCmdArgs &args ) {
	if ( !RenderDoc_InitAPI() ) {
		RenderDoc_PrintUnavailable();
		return;
	}

	if ( renderDocState.api->TriggerCapture == NULL ) {
		common->Printf( "RenderDoc API is active but capture triggering is unavailable.\n" );
		return;
	}

	int numFrames = 1;
	if ( args.Argc() > 1 ) {
		numFrames = idMath::ClampInt( 1, 1024, atoi( args.Argv( 1 ) ) );
	}

	idStr captureTemplate;
	RenderDoc_UpdateCaptureTemplate( captureTemplate );

	if ( numFrames > 1 && renderDocState.api->TriggerMultiFrameCapture != NULL ) {
		renderDocState.api->TriggerMultiFrameCapture( numFrames );
		common->Printf(
			"RenderDoc armed a %d-frame capture%s%s\n",
			numFrames,
			captureTemplate.Length() > 0 ? " -> " : "",
			captureTemplate.Length() > 0 ? captureTemplate.c_str() : "" );
		return;
	}

	renderDocState.api->TriggerCapture();
	common->Printf(
		"RenderDoc armed the next-frame capture%s%s\n",
		captureTemplate.Length() > 0 ? " -> " : "",
		captureTemplate.Length() > 0 ? captureTemplate.c_str() : "" );
}

void RenderDoc_RegisterCommands( void ) {
	cmdSystem->AddCommand(
		"renderDocStatus",
		RenderDoc_Status_f,
		CMD_FL_SYSTEM,
		"shows RenderDoc availability, capture path, and target-control status" );
	cmdSystem->AddCommand(
		"renderDocCapture",
		RenderDoc_Capture_f,
		CMD_FL_SYSTEM,
		"arms a RenderDoc capture for the next frame or N frames" );
}

void RenderDoc_Shutdown( void ) {
#if defined( __linux__ ) || defined( __APPLE__ )
	if ( renderDocState.moduleHandleNeedsRelease && renderDocState.moduleHandle != NULL ) {
		dlclose( renderDocState.moduleHandle );
	}
#endif

	memset( &renderDocState, 0, sizeof( renderDocState ) );
}
