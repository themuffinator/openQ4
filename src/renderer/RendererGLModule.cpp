// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Shared renderer-module glue (Phase B8 / Phase C,
	docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md,
	docs/dev/plans/2026-07-17-vulkan-phase-c.md).

	Compiled ONLY into renderer dynamic modules (OPENQ4_RENDERER_MODULE,
	with OPENQ4_RENDERER_GL_MODULE / OPENQ4_RENDERER_VK_MODULE selecting the
	backend tail); the engine's renderer glob sees an empty translation
	unit. Mirrors the game-DLL convention: defines the engine interface
	globals the renderer sources bind against, initializes idlib, provides
	the module-local forwarders for the engine free functions the renderer
	calls, and exports GetRenderAPI.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_MODULE

#ifndef OPENQ4_RENDERER_BACKEND_NAME
#define OPENQ4_RENDERER_BACKEND_NAME		"gl"
#endif
#ifndef OPENQ4_RENDERER_BACKEND_DESC
#define OPENQ4_RENDERER_BACKEND_DESC		"openQ4 OpenGL renderer module"
#endif

#include "tr_local.h"
#include "RenderModuleAPI.h"
#include "RendererModule.h"
#include "../bse/BSEInterface.h"
#include "../framework/EventLoop.h"
#include "../framework/Console.h"

#if defined( _WIN32 )
#include <windows.h>
#else
#include <pthread.h>
#include <string.h>
#endif

#if defined( MACOS_X )
#include <limits.h>
#include <mach-o/dyld.h>
// same defensive fallback the macOS platform sources carry
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

/*
====================
Engine interface globals

The renderer sources reference these exactly like game code does; inside the
module they are module-local variables bound from renderImport_t.
====================
*/
idSys *						sys = NULL;
idCommon *					common = NULL;
idCVarSystem *				cvarSystem = NULL;
idCmdSystem *				cmdSystem = NULL;
idFileSystem *				fileSystem = NULL;
idDeclManager *				declManager = NULL;
idSoundSystem *				soundSystem = NULL;
idSession *					session = NULL;
idUserInterfaceManager *	uiManager = NULL;
idCollisionModelManager *	collisionModelManager = NULL;
idEventLoop *				eventLoop = NULL;
rvBSEManager *				bse = NULL;
idConsole *					console = NULL;

// the module's own static-cvar registration chain (game-DLL convention)
idCVar *					idCVar::staticVars = NULL;

// mirrors of engine-defined cvar objects the renderer sources reference
// directly; RegisterStaticVars links them to the engine's internal cvars, so
// state is shared. Definitions must match the engine's exactly (Common.cpp /
// DeclManager.cpp).
idCVar com_developer( "developer", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "developer mode" );
idCVar com_purgeAll( "com_purgeAll", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM, "purge everything between level loads" );
idCVar com_makingBuild( "com_makingBuild", "0", CVAR_BOOL | CVAR_SYSTEM, "1 when making a build" );
idCVar com_SingleDeclFile( "com_SingleDeclFile", "0", CVAR_SYSTEM | CVAR_BOOL, "load decls from a packed single .decls file instead of scanning loose decl folders" );
idCVar r_skipGlowOverlay( "r_skipGlowOverlay", "0", CVAR_ARCHIVE | CVAR_RENDERER, "skip glow overlays when non-zero" );
// window/gui cvars whose engine-side homes moved to the loader TU
// (RendererModule.cpp); renderer sources reference the objects directly
idCVar r_fullscreenDesktop( "r_fullscreenDesktop", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = native desktop fullscreen, 0 = exclusive mode using r_mode/r_customWidth/r_customHeight" );
idCVar r_borderless( "r_borderless", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = borderless window mode when r_fullscreen is 0" );
idCVar r_windowWidth( "r_windowWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode width" );
idCVar r_windowHeight( "r_windowHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode height" );
idCVar r_skipGuiShaders( "r_skipGuiShaders", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );

static const renderModuleServices_t *rgm_services = NULL;
static const renderWindowServices_t *rgm_windowServices = NULL;
static renderExport_t rgm_export;

// the engine-owned window state is mirrored through the import; the module's
// engineWindowState instance satisfies the renderer sources' references and
// is refreshed from the services on demand
engineWindowState_t engineWindowState;

/*
====================
Sys_GetRenderWindowServices

Inside the module the window services come from the import, not from a
platform backend.
====================
*/
const renderWindowServices_t *Sys_GetRenderWindowServices( void ) {
	return rgm_windowServices;
}

/*
====================
Engine free-function forwarders

The renderer sources keep their existing calls; these module-local
definitions route them over the services table.
====================
*/
int Sys_Milliseconds( void ) {
	return rgm_services->Milliseconds();
}

void Sys_Sleep( int msec ) {
	rgm_services->Sleep( msec );
}

void Sys_EnterCriticalSection( int index ) {
	rgm_services->EnterCriticalSection( index );
}

void Sys_LeaveCriticalSection( int index ) {
	rgm_services->LeaveCriticalSection( index );
}

bool RenderDoc_IsInjected( void ) {
	return rgm_services->IsRenderDocInjected();
}

bool Sys_GetDesktopResolution( int *width, int *height ) {
	if ( rgm_windowServices == NULL || rgm_windowServices->GetDesktopResolution == NULL ) {
		return false;
	}
	rgm_windowServices->GetDesktopResolution( width, height );
	return true;
}

const char *Sys_GetProcessorString( void ) {
	// the platform layers publish the CPU description as the sys_cpustring
	// cvar; read it through the services instead of the engine's sys globals
	return rgm_services->CVar_GetString( "sys_cpustring" );
}

#if defined( MACOS_X )
/*
====================
Sys_EXEPath

macOS only, and required rather than merely convenient: the ELF modules can
leave this undefined and let dlopen bind the engine's copy, but Mach-O resolves
every undefined symbol when the dylib is *linked*, so renderer-vk fails to link
outright without a module-local definition. The reference comes from the
MoltenVK loader search in VulkanDevice.cpp, which anchors its bundle-relative
lookup on the executable directory.

_NSGetExecutablePath is exactly what the engine's own macOS Sys_EXEPath is built
on (src/sys/osx/macosx_compat.mm), so both halves resolve the same MoltenVK
image - which is the whole point of that search, since two libraries behind one
VkInstance is undefined behavior. The engine's NSBundle fallback is dropped
here: it needs Objective-C, and it only ever runs when _NSGetExecutablePath
itself fails.
====================
*/
const char *Sys_EXEPath( void ) {
	static char exePath[ PATH_MAX ];

	uint32_t bufferSize = (uint32_t)sizeof( exePath );
	if ( _NSGetExecutablePath( exePath, &bufferSize ) != 0 ) {
		// the real path does not fit; callers treat empty as "unknown" and
		// fall through to the system Vulkan loader
		exePath[ 0 ] = '\0';
	}
	return exePath;
}
#endif

void Sys_InitInput( void ) {
	if ( rgm_windowServices != NULL && rgm_windowServices->InitInput != NULL ) {
		rgm_windowServices->InitInput();
	}
}

void Sys_ShutdownInput( void ) {
	if ( rgm_windowServices != NULL && rgm_windowServices->ShutdownInput != NULL ) {
		rgm_windowServices->ShutdownInput();
	}
}

void Sys_GrabMouseCursor( bool grabIt ) {
	if ( rgm_windowServices != NULL && rgm_windowServices->GrabMouseCursor != NULL ) {
		rgm_windowServices->GrabMouseCursor( grabIt );
	}
}

/*
====================
Worker threads (light-grid bake pool)

Module-local implementations of the sys thread primitives, faithful to the
platform backends (win_main.cpp / posix_threads.cpp) minus the engine-only
trigger-event pokes. The registry is module-local, so these workers do not
appear in the engine's Sys_ListThreads output.
====================
*/
int g_thread_count = 0;
xthreadInfo *g_threads[MAX_THREADS];

static void RGM_RemoveThreadInfo( xthreadInfo &info ) {
	Sys_EnterCriticalSection( CRITICAL_SECTION_ZERO );
	for ( int i = 0; i < g_thread_count; i++ ) {
		if ( &info == g_threads[i] ) {
			g_threads[i] = NULL;
			int j;
			for ( j = i + 1; j < g_thread_count; j++ ) {
				g_threads[j - 1] = g_threads[j];
			}
			g_threads[j - 1] = NULL;
			g_thread_count--;
			break;
		}
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
}

void Sys_RequestThreadStop( xthreadInfo &info ) {
	info.stopRequested = true;
}

bool Sys_IsThreadStopRequested( const xthreadInfo &info ) {
	return info.stopRequested;
}

#if defined( _WIN32 )

void Sys_CreateThread( xthread_t function, void *parms, xthreadPriority priority, xthreadInfo &info, const char *name, xthreadInfo *threads[MAX_THREADS], int *thread_count ) {
	DWORD threadId = 0;
	HANDLE temp = CreateThread( NULL, 0, ( LPTHREAD_START_ROUTINE )function, parms, 0, &threadId );
	info.threadHandle = reinterpret_cast<uintptr_t>( temp );
	info.threadId = static_cast<uint32_t>( threadId );
	info.stopRequested = false;
	if ( priority == THREAD_HIGHEST ) {
		SetThreadPriority( reinterpret_cast<HANDLE>( info.threadHandle ), THREAD_PRIORITY_HIGHEST );
	} else if ( priority == THREAD_ABOVE_NORMAL ) {
		SetThreadPriority( reinterpret_cast<HANDLE>( info.threadHandle ), THREAD_PRIORITY_ABOVE_NORMAL );
	}
	info.name = name;
	// Match RGM_RemoveThreadInfo and the POSIX branch below: the slot store and
	// the count bump have to be serialized against the readers, otherwise a
	// weakly ordered target can expose the bumped count with a stale slot.
	Sys_EnterCriticalSection( CRITICAL_SECTION_ZERO );
	if ( *thread_count < MAX_THREADS ) {
		threads[( *thread_count )++] = &info;
		Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
	} else {
		Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
		common->DPrintf( "WARNING: MAX_THREADS reached\n" );
	}
}

bool Sys_IsCurrentThreadStopRequested( void ) {
	const DWORD id = GetCurrentThreadId();
	bool stopRequested = false;
	Sys_EnterCriticalSection( CRITICAL_SECTION_ZERO );
	for ( int i = 0; i < g_thread_count; i++ ) {
		if ( g_threads[i] != NULL && id == g_threads[i]->threadId ) {
			stopRequested = g_threads[i]->stopRequested;
			break;
		}
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
	return stopRequested;
}

void Sys_DestroyThread( xthreadInfo &info ) {
	Sys_RequestThreadStop( info );
	WaitForSingleObject( reinterpret_cast<HANDLE>( info.threadHandle ), INFINITE );
	CloseHandle( reinterpret_cast<HANDLE>( info.threadHandle ) );
	info.threadHandle = 0;
	info.threadId = 0;
	info.stopRequested = false;
	RGM_RemoveThreadInfo( info );
}

#else

typedef void *( *rgmPthreadFunction_t )( void * );

static_assert( sizeof( pthread_t ) <= sizeof( uintptr_t ),
	"xthreadInfo::threadHandle cannot represent pthread_t on this platform" );

static uintptr_t RGM_PThreadToHandle( pthread_t thread ) {
	uintptr_t handle = 0;
	memcpy( &handle, &thread, sizeof( thread ) );
	return handle;
}

static pthread_t RGM_HandleToPThread( uintptr_t handle ) {
	pthread_t thread;
	memset( &thread, 0, sizeof( thread ) );
	memcpy( &thread, &handle, sizeof( thread ) );
	return thread;
}

void Sys_CreateThread( xthread_t function, void *parms, xthreadPriority priority, xthreadInfo &info, const char *name, xthreadInfo *threads[MAX_THREADS], int *thread_count ) {
	( void )priority;
	info.threadHandle = 0;
	info.threadId = 0;
	info.stopRequested = false;
	info.name = name != NULL && name[0] != '\0' ? name : "unnamed";

	Sys_EnterCriticalSection( CRITICAL_SECTION_ZERO );
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_JOINABLE );
	const size_t minWorkerStackSize = 4 * 1024 * 1024;
	size_t defaultStackSize = 0;
	if ( pthread_attr_getstacksize( &attr, &defaultStackSize ) == 0 && defaultStackSize < minWorkerStackSize ) {
		( void )pthread_attr_setstacksize( &attr, minWorkerStackSize );
	}
	pthread_t thread;
	const int result = pthread_create( &thread, &attr, ( rgmPthreadFunction_t )function, parms );
	pthread_attr_destroy( &attr );
	if ( result != 0 ) {
		Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
		common->Error( "renderer module: pthread_create %s failed: %s\n", info.name, strerror( result ) );
		return;
	}
	info.threadHandle = RGM_PThreadToHandle( thread );
	if ( *thread_count < MAX_THREADS ) {
		threads[( *thread_count )++] = &info;
	} else {
		common->DPrintf( "WARNING: MAX_THREADS reached\n" );
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
}

bool Sys_IsCurrentThreadStopRequested( void ) {
	const pthread_t thread = pthread_self();
	bool stopRequested = false;
	Sys_EnterCriticalSection( CRITICAL_SECTION_ZERO );
	for ( int i = 0; i < g_thread_count; i++ ) {
		if ( g_threads[i] != NULL && pthread_equal( thread, RGM_HandleToPThread( g_threads[i]->threadHandle ) ) ) {
			stopRequested = g_threads[i]->stopRequested;
			break;
		}
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ZERO );
	return stopRequested;
}

void Sys_DestroyThread( xthreadInfo &info ) {
	if ( info.threadHandle == 0 ) {
		return;
	}
	Sys_RequestThreadStop( info );
	( void )pthread_join( RGM_HandleToPThread( info.threadHandle ), NULL );
	info.threadHandle = 0;
	info.threadId = 0;
	info.stopRequested = false;
	RGM_RemoveThreadInfo( info );
}

#endif

/*
====================
Loader-owned diagnostics

The module loader (RendererModule.cpp) lives engine-side; inside the module
these entry points are inert so the shared renderer sources link unchanged.
====================
*/
void R_RendererModule_Boot( void ) {
}

void R_RendererModule_Shutdown( void ) {
}

void RendererModule_PrintGfxInfo( void ) {
	// the loader owns the request/active/disposition status
	if ( rgm_services != NULL && rgm_services->PrintRendererApiStatus != NULL ) {
		rgm_services->PrintRendererApiStatus();
	}
}

#if defined( OPENQ4_RENDERER_GL_MODULE ) || defined( OPENQ4_RENDERER_GLES_MODULE )
/*
====================
QGL loader (module-local, GL and GLES backends)

Under the SDL3 seam all GL procs resolve through the window services; QGL
only tracks the driver library handle on Windows for parity with the legacy
loader.
====================
*/
#if defined( _WIN32 )
static HMODULE rgm_glDriverModule = NULL;
PROC ( WINAPI *qwglGetProcAddress )( LPCSTR ) = NULL;

bool QGL_Init( const char *dllname ) {
	rgm_glDriverModule = LoadLibraryA( dllname );
	if ( rgm_glDriverModule == NULL ) {
		return false;
	}
	qwglGetProcAddress = ( PROC ( WINAPI * )( LPCSTR ) )GetProcAddress( rgm_glDriverModule, "wglGetProcAddress" );
	return true;
}

void QGL_Shutdown( void ) {
	qwglGetProcAddress = NULL;
	if ( rgm_glDriverModule != NULL ) {
		FreeLibrary( rgm_glDriverModule );
		rgm_glDriverModule = NULL;
	}
}
#else
bool QGL_Init( const char *dllname ) {
	(void)dllname;
	return true;
}

void QGL_Shutdown( void ) {
}
#endif

#endif /* OPENQ4_RENDERER_GL_MODULE || OPENQ4_RENDERER_GLES_MODULE */

/*
====================
GetRenderAPI
====================
*/
#ifdef OPENQ4_RENDERER_VK_MODULE
// Vulkan backend tail hooks (VulkanModule.cpp / VulkanBringup.cpp); declared
// at file scope so they keep C++ linkage even when called from inside the
// extern-C GetRenderAPI body (block-scope externs there would take C linkage)
void VK_ModuleBindServices( const renderModuleServices_t *services );
const renderModuleDiagnostics_t *VK_GetModuleDiagnostics( void );
void VK_Bringup_Shutdown( void );
#endif

static void RGM_Shutdown( void ) {
#ifdef OPENQ4_RENDERER_VK_MODULE
	VK_Bringup_Shutdown();
#endif
	idSIMD::Shutdown();
	idLib::ShutDown();
}

#if defined( _WIN32 )
	#define RENDERER_GL_MODULE_EXPORT __declspec( dllexport )
#else
	#define RENDERER_GL_MODULE_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

extern "C" RENDERER_GL_MODULE_EXPORT
renderExport_t *GetRenderAPI( renderImport_t *moduleImport ) {
	memset( &rgm_export, 0, sizeof( rgm_export ) );
	rgm_export.version = RENDER_API_VERSION;
	rgm_export.backendName = OPENQ4_RENDERER_BACKEND_NAME;
	rgm_export.moduleDescription = OPENQ4_RENDERER_BACKEND_DESC;
	rgm_export.Shutdown = RGM_Shutdown;

	if ( moduleImport == NULL || moduleImport->version != RENDER_API_VERSION || moduleImport->services == NULL ) {
		return &rgm_export;
	}

	rgm_services = moduleImport->services;
	rgm_windowServices = moduleImport->windowServices;

#ifdef OPENQ4_RENDERER_VK_MODULE
	VK_ModuleBindServices( rgm_services );
#endif

	sys = moduleImport->sys;
	common = moduleImport->common;
	cvarSystem = moduleImport->cvarSystem;
	cmdSystem = moduleImport->cmdSystem;
	fileSystem = moduleImport->fileSystem;
	declManager = moduleImport->declManager;
	soundSystem = moduleImport->soundSystem;
	session = moduleImport->session;
	uiManager = moduleImport->uiManager;
	collisionModelManager = moduleImport->collisionModelManager;
	eventLoop = moduleImport->eventLoop;
	bse = moduleImport->bse;
	console = moduleImport->console;

	// interface pointers used by the module's idlib copy
	idLib::sys = sys;
	idLib::common = common;
	idLib::cvarSystem = cvarSystem;
	idLib::fileSystem = fileSystem;

	// initialize the module's idlib copy (memory system, math tables),
	// exactly like the game modules do on load
	idLib::Init();

	// link the module's static cvars (r_*, image_*, ...) into the engine's
	// cvar system
	idCVar::RegisterStaticVars();

	// initialize processor specific SIMD for the module's idlib copy
	idSIMD::InitProcessor( "renderer-" OPENQ4_RENDERER_BACKEND_NAME, cvarSystem->GetCVarBool( "com_forceGenericSIMD" ) );

	rgm_export.renderSystem = renderSystem;			// the module's statically-initialized &tr
	rgm_export.renderModelManager = renderModelManager;
	rgm_export.TryDeviceRestart = R_TryFullVidRestart;
	rgm_export.GetDisplayPresentation = R_GetDisplayPresentation;
	rgm_export.TryInitializeDisplay = R_TryInitializeDisplay;
#ifdef OPENQ4_RENDERER_VK_MODULE
	// the Vulkan backend keeps its bring-up diagnostics surface for the
	// on-demand rendererVkProbe flow
	rgm_export.diagnostics = VK_GetModuleDiagnostics();
#endif
	return &rgm_export;
}

#endif /* OPENQ4_RENDERER_MODULE */
