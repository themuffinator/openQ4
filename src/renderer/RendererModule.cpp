// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "RenderModuleAPI.h"
#include "RendererModule.h"
#include "../framework/RenderDoc.h"
#include "../bse/BSEInterface.h"

/*
===============================================================================

	Renderer-module selection, loading, and fail-closed fallback.

	See docs/dev/plans/2026-07-16-vulkan-renderer.md.

===============================================================================
*/

// loader-owned cvars: defined here (not in a renderer TU) so they exist in
// every build shape, including module-only clients that shed the static
// renderer sources
static const char *r_renderApiArgs[] = { "best", "gl", "vulkan", "gl-module", "gles", NULL };
#ifdef __ANDROID__
// Android has no desktop GL at all; the ES module is the only renderer built.
#define OPENQ4_DEFAULT_RENDER_API	"gles"
static const rendererModuleApi_t rm_platformDefaultApi = RENDER_MODULE_API_GLES;
#else
#define OPENQ4_DEFAULT_RENDER_API	"gl"
static const rendererModuleApi_t rm_platformDefaultApi = RENDER_MODULE_API_GL;
#endif
idCVar r_renderApi( "r_renderApi", OPENQ4_DEFAULT_RENDER_API, CVAR_RENDERER | CVAR_ARCHIVE, "rendering API: best = platform default (Android: gles, desktop: gl), gles = OpenGL ES 3.0 renderer module, gl = OpenGL renderer (loaded as the renderer-gl module on module-only builds, statically linked elsewhere), vulkan = native Vulkan renderer module (bring-up; falls back to gl), gl-module = alias that always selects the OpenGL module. Module selections take effect on engine restart.", r_renderApiArgs, idCmdSystem::ArgCompletion_String<r_renderApiArgs> );
idCVar r_actualRenderApi( "r_actualRenderApi", "UNINITIALIZED", CVAR_RENDERER | CVAR_ROM, "rendering API actually active after request/fallback selection" );

// engine-side homes for window/gui cvars referenced by both the platform
// backend (engine) and the renderer sources (module mirrors them in its
// glue TU); defined here for the same every-build-shape reason
idCVar r_fullscreenDesktop( "r_fullscreenDesktop", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = native desktop fullscreen, 0 = exclusive mode using r_mode/r_customWidth/r_customHeight" );
idCVar r_borderless( "r_borderless", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = borderless window mode when r_fullscreen is 0" );
idCVar r_windowWidth( "r_windowWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode width" );
idCVar r_windowHeight( "r_windowHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode height" );
idCVar r_skipGuiShaders( "r_skipGuiShaders", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );

#ifdef OPENQ4_RENDERER_MODULE_ONLY
// module-only clients have no renderer TUs; the engine-side interface
// globals live here, NULL until RM_PublishActiveModuleInterfaces fills them
// at first boot (before declManager->Init, so nothing dereferences earlier)
idRenderSystem *		renderSystem = NULL;
idRenderModelManager *	renderModelManager = NULL;
#endif

#if defined( _M_X64 ) || defined( __x86_64__ )
	#define RENDERER_MODULE_ARCH_TAG "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define RENDERER_MODULE_ARCH_TAG "x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define RENDERER_MODULE_ARCH_TAG "arm64"
#else
	#define RENDERER_MODULE_ARCH_TAG "unknown"
#endif

typedef struct rendererModuleState_s {
	rendererModuleStatus_t	status;
	intptr_t				moduleHandle;
	renderExport_t			moduleExport;
	bool					moduleExportValid;
	// engine-side interface pointers saved across a module activation so
	// unload can restore them
	idRenderSystem *		savedRenderSystem;
	idRenderModelManager *	savedRenderModelManager;
	bool					interfacesPublished;
	// module activation (publishing a module renderSystem) is only allowed
	// during the first boot, before renderSystem->Init() binds engine state
	// to the active renderer instance
	bool					activationAllowed;
	bool					everBooted;
} rendererModuleState_t;

static rendererModuleState_t rm_state;

// Deliberately outside rm_state and outside renderer modules: never reset when
// their interface tables or local presentation counters are discarded.
static uint64_t rm_displayModuleEpoch = 1;
static const renderWindowServices_t *rm_displayVideoPin = NULL;
static void RM_ReleaseDisplayVideoPin( void ) {
	if ( rm_displayVideoPin != NULL ) {
		rm_displayVideoPin->ReleaseVideoSystem();
		rm_displayVideoPin = NULL;
	}
}
static void RM_AdvanceDisplayEpoch( void ) {
	// Exhaustion permanently disables identity-dependent observations.
	if ( rm_displayModuleEpoch != 0 ) {
		rm_displayModuleEpoch = rm_displayModuleEpoch == UINT64_MAX ? 0 : rm_displayModuleEpoch + 1;
	}
}

// module binary short tags; indexed by rendererModuleApi_t
static const char *rm_moduleBinaryTags[ RENDER_MODULE_API_COUNT ] = { "gl", "vk", "gl", "gles" };
static const char *rm_apiNames[ RENDER_MODULE_API_COUNT ] = { "gl", "vulkan", "gl-module", "gles" };

/*
====================
Engine-side bindings for the module services table
====================
*/
static void RM_Services_Printf( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Printf( "%s", text );
}

static void RM_Services_Warning( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Warning( "%s", text );
}

static void RM_Services_Error( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Error( "%s", text );
}

static int RM_Services_Milliseconds( void ) {
	return Sys_Milliseconds();
}

static const char *RM_Services_CVarGetString( const char *name ) {
	return cvarSystem->GetCVarString( name );
}

static int RM_Services_CVarGetInteger( const char *name ) {
	return cvarSystem->GetCVarInteger( name );
}

static bool RM_Services_CVarGetBool( const char *name ) {
	return cvarSystem->GetCVarBool( name );
}

static void RM_Services_CVarSetString( const char *name, const char *value ) {
	cvarSystem->SetCVarString( name, value );
}

static void RM_Services_Sleep( int msec ) {
	Sys_Sleep( msec );
}

static void RM_Services_EnterCriticalSection( int index ) {
	Sys_EnterCriticalSection( index );
}

static void RM_Services_LeaveCriticalSection( int index ) {
	Sys_LeaveCriticalSection( index );
}

static bool RM_Services_IsRenderDocInjected( void ) {
	return RenderDoc_IsInjected();
}

static void RM_Services_PrintRendererApiStatus( void ) {
	RendererModule_PrintGfxInfo();
}

static const renderModuleServices_t rm_services = {
	RM_Services_Printf,
	RM_Services_Warning,
	RM_Services_Error,
	RM_Services_Milliseconds,
	RM_Services_CVarGetString,
	RM_Services_CVarGetInteger,
	RM_Services_CVarGetBool,
	RM_Services_CVarSetString,
	RM_Services_Sleep,
	RM_Services_EnterCriticalSection,
	RM_Services_LeaveCriticalSection,
	RM_Services_IsRenderDocInjected,
	RM_Services_PrintRendererApiStatus,
};

/*
====================
RM_BuildImport

Single import builder shared by the boot loader and the on-demand probe so
the field set cannot drift between them.
====================
*/
static void RM_BuildImport( renderImport_t &moduleImport ) {
	memset( &moduleImport, 0, sizeof( moduleImport ) );
	moduleImport.version = RENDER_API_VERSION;
	moduleImport.services = &rm_services;
	moduleImport.sys = ::sys;
	moduleImport.common = ::common;
	moduleImport.cvarSystem = ::cvarSystem;
	moduleImport.cmdSystem = ::cmdSystem;
	moduleImport.fileSystem = ::fileSystem;
	moduleImport.declManager = ::declManager;
	moduleImport.soundSystem = ::soundSystem;
	moduleImport.session = ::session;
	moduleImport.uiManager = ::uiManager;
	moduleImport.collisionModelManager = ::collisionModelManager;
	moduleImport.eventLoop = ::eventLoop;
	moduleImport.bse = ::bse;
	moduleImport.windowServices = Sys_GetRenderWindowServices();
	moduleImport.console = ::console;
}

/*
====================
RM_PublishActiveModuleInterfaces

Points the engine's renderer-interface globals at a module's exports. Dormant
until the Phase B8 seam lets RM_ExportCanRender activate a full module; kept
wired into the activation branch so the flip stays a one-line policy change.
====================
*/
static void RM_PublishActiveModuleInterfaces( const renderExport_t &moduleExport ) {
	RM_AdvanceDisplayEpoch();
	rm_state.savedRenderSystem = ::renderSystem;
	rm_state.savedRenderModelManager = ::renderModelManager;
	rm_state.interfacesPublished = true;
	if ( moduleExport.renderSystem != NULL ) {
		::renderSystem = moduleExport.renderSystem;
	}
	if ( moduleExport.renderModelManager != NULL ) {
		::renderModelManager = moduleExport.renderModelManager;
	}
}

/*
====================
RM_RestorePublishedInterfaces
====================
*/
static void RM_RestorePublishedInterfaces( void ) {
	if ( !rm_state.interfacesPublished ) {
		return;
	}
	RM_AdvanceDisplayEpoch();
	::renderSystem = rm_state.savedRenderSystem;
	::renderModelManager = rm_state.savedRenderModelManager;
	rm_state.savedRenderSystem = NULL;
	rm_state.savedRenderModelManager = NULL;
	rm_state.interfacesPublished = false;
}

/*
====================
R_RendererModule_ParseApi
====================
*/
bool R_RendererModule_ParseApi( const char *value, rendererModuleApi_t &api ) {
	if ( value == NULL || value[ 0 ] == '\0' ) {
		api = rm_platformDefaultApi;
		return false;
	}
	if ( idStr::Icmp( value, "gl" ) == 0 || idStr::Icmp( value, "opengl" ) == 0 ) {
		api = RENDER_MODULE_API_GL;
		return true;
	}
	if ( idStr::Icmp( value, "vulkan" ) == 0 || idStr::Icmp( value, "vk" ) == 0 ) {
		api = RENDER_MODULE_API_VULKAN;
		return true;
	}
	if ( idStr::Icmp( value, "gl-module" ) == 0 ) {
		api = RENDER_MODULE_API_GL_MODULE;
		return true;
	}
	if ( idStr::Icmp( value, "gles" ) == 0 || idStr::Icmp( value, "es" ) == 0 ) {
		api = RENDER_MODULE_API_GLES;
		return true;
	}
	if ( idStr::Icmp( value, "best" ) == 0 ) {
		api = rm_platformDefaultApi;
		return true;
	}
	api = rm_platformDefaultApi;
	return false;
}

/*
====================
R_RendererModule_ApiName
====================
*/
const char *R_RendererModule_ApiName( rendererModuleApi_t api ) {
	if ( api < 0 || api >= RENDER_MODULE_API_COUNT ) {
		return "invalid";
	}
	return rm_apiNames[ api ];
}

/*
====================
R_RendererModule_BuildBinaryName
====================
*/
void R_RendererModule_BuildBinaryNameForArch( rendererModuleApi_t api, const char *archTag, char *outName, int maxLength ) {
	const char *tag = ( api >= 0 && api < RENDER_MODULE_API_COUNT ) ? rm_moduleBinaryTags[ api ] : "invalid";
	idStr::snPrintf( outName, maxLength, "renderer-%s_%s", tag, archTag );
}

void R_RendererModule_BuildBinaryName( rendererModuleApi_t api, char *outName, int maxLength ) {
	R_RendererModule_BuildBinaryNameForArch( api, RENDERER_MODULE_ARCH_TAG, outName, maxLength );
}

/*
====================
R_RendererModule_BuildFallbackLadder

Every ladder fails closed onto GL, which retains its own safe-mode retry.
====================
*/
int R_RendererModule_BuildFallbackLadder( rendererModuleApi_t requested, rendererModuleApi_t *outLadder, int maxEntries ) {
	int numEntries = 0;

	if ( maxEntries <= 0 ) {
		return 0;
	}
#ifdef __ANDROID__
	// No desktop GL module is built, so falling back onto it would only trade a
	// clear "gles module failed" for a misleading "renderer-gl not found".
	outLadder[ numEntries++ ] = RENDER_MODULE_API_GLES;
	return numEntries;
#else
	if ( requested != RENDER_MODULE_API_GL ) {
		outLadder[ numEntries++ ] = requested;
	}
	if ( numEntries < maxEntries ) {
		outLadder[ numEntries++ ] = RENDER_MODULE_API_GL;
	}
	return numEntries;
#endif
}

/*
====================
RM_ResolveModulePath

Renderer modules load only from the executable directory, the platform package
root, or the platform's trusted module root, mirroring the trusted-root policy
in idFileSystem::FindDLL; they are never loaded from pak files, fs_savepath, or
mod content. The extra roots matter on macOS, where the executable lives in
openQ4.app/Contents/MacOS while signed Mach-O modules are staged in the flat
Contents/Frameworks code directory.
====================
*/
static bool RM_ModuleFileExists( const char *path ) {
	FILE *file = fopen( path, "rb" );
	if ( file == NULL ) {
		return false;
	}
	fclose( file );
	return true;
}

static bool RM_ResolveModulePath( rendererModuleApi_t api, char *outPath, int maxLength ) {
	char	binaryName[ MAX_OSPATH ];
	char	fileName[ MAX_OSPATH ];

	// Thin packages carry architecture-specific module names. A universal2
	// macOS package instead carries ONE two-slice module, so both executable
	// slices must converge on the same trusted name -- mirrors the game-module
	// fallback in openQ4_BuildGameModuleBinaryNameForArch / Common.cpp.
	const char *archTags[ 2 ];
	int numArchTags = 0;
	archTags[ numArchTags++ ] = RENDERER_MODULE_ARCH_TAG;
#if defined( MACOS_X ) || defined( __APPLE__ )
	archTags[ numArchTags++ ] = "universal2";
#endif

	R_RendererModule_BuildBinaryName( api, binaryName, sizeof( binaryName ) );
	sys->DLL_GetFileName( binaryName, fileName, sizeof( fileName ) );

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	idStr searchRoots[ 3 ];
	int numSearchRoots = 0;
	searchRoots[ numSearchRoots++ ] = exeDir;

	char moduleRoot[ MAX_OSPATH ];
	if ( Sys_GetGameModuleRootDirectory( moduleRoot, sizeof( moduleRoot ) )
			&& exeDir.Icmp( moduleRoot ) != 0 ) {
		searchRoots[ numSearchRoots++ ] = moduleRoot;
	}
	char packageRoot[ MAX_OSPATH ];
	if ( Sys_GetPackageRootDirectory( packageRoot, sizeof( packageRoot ) ) ) {
		bool duplicateRoot = false;
		for ( int i = 0; i < numSearchRoots; i++ ) {
			if ( searchRoots[ i ].Icmp( packageRoot ) == 0 ) {
				duplicateRoot = true;
				break;
			}
		}
		if ( !duplicateRoot ) {
			searchRoots[ numSearchRoots++ ] = packageRoot;
		}
	}

	// first existing candidate wins; fall back to the executable directory and
	// this slice's own arch name so a missing module still reports the path the
	// operator most likely expected
	idStr fallback;
	for ( int archIndex = 0; archIndex < numArchTags; archIndex++ ) {
		R_RendererModule_BuildBinaryNameForArch( api, archTags[ archIndex ], binaryName, sizeof( binaryName ) );
		sys->DLL_GetFileName( binaryName, fileName, sizeof( fileName ) );

		for ( int i = 0; i < numSearchRoots; i++ ) {
			idStr candidate = searchRoots[ i ] + PATHSEPERATOR_STR + fileName;
			if ( candidate.Length() >= maxLength ) {
				continue;
			}
			if ( archIndex == 0 && i == 0 ) {
				fallback = candidate;
			}
			if ( RM_ModuleFileExists( candidate.c_str() ) ) {
				idStr::Copynz( outPath, candidate.c_str(), maxLength );
				return true;
			}
		}
	}

	if ( fallback.Length() == 0 ) {
		outPath[ 0 ] = '\0';
		return false;
	}
	idStr::Copynz( outPath, fallback.c_str(), maxLength );
	return true;
}

/*
====================
RM_ValidateExport

Shared by the live loader and the self-test so rejection rules cannot drift.
Returns false when the export must be rejected; a valid bring-up export
(NULL renderSystem) returns true with diagnosticsOnly set.
====================
*/
static bool RM_ValidateExport( const renderExport_t *moduleExport, bool &diagnosticsOnly, const char **reason ) {
	diagnosticsOnly = false;
	*reason = "";

	if ( moduleExport == NULL ) {
		*reason = "module returned no export table";
		return false;
	}
	if ( moduleExport->version != RENDER_API_VERSION ) {
		*reason = "module render API version mismatch";
		return false;
	}
	if ( moduleExport->backendName == NULL || moduleExport->backendName[ 0 ] == '\0' ) {
		*reason = "module reported no backend name";
		return false;
	}
	if ( moduleExport->renderSystem == NULL ) {
		diagnosticsOnly = true;
	}
	return true;
}

/*
====================
RM_ExportCanRender

Activation policy on top of shape validation: with the Phase B8 seam landed
the engine can host a full module-provided idRenderSystem, so any export that
carries one is activatable. Bring-up/diagnostics exports still refuse. Shared
with the self-test so the policy cannot silently drift.
====================
*/
static bool RM_ExportCanRender( const renderExport_t *moduleExport, const char **reason ) {
	bool diagnosticsOnly = false;

	if ( !RM_ValidateExport( moduleExport, diagnosticsOnly, reason ) ) {
		return false;
	}
	if ( diagnosticsOnly ) {
		*reason = "module is bring-up/diagnostics only";
		return false;
	}
	if ( moduleExport->TryDeviceRestart == NULL || moduleExport->GetDisplayPresentation == NULL || moduleExport->TryInitializeDisplay == NULL ) {
		*reason = "module lacks version 15 device services";
		return false;
	}
	return true;
}

/*
====================
RM_UnloadModule
====================
*/
static void RM_UnloadModule( void ) {
	RM_RestorePublishedInterfaces();
	if ( rm_state.moduleExportValid && rm_state.moduleExport.Shutdown != NULL ) {
		rm_state.moduleExport.Shutdown();
	}
	rm_state.moduleExportValid = false;
	memset( &rm_state.moduleExport, 0, sizeof( rm_state.moduleExport ) );
	if ( rm_state.moduleHandle != 0 ) {
		// drop the counters before the code they live in goes away
		Mem_UnregisterModuleStats( ( memModuleStats_t )Sys_DLL_GetProcAddress( rm_state.moduleHandle, MEM_MODULE_STATS_ENTRY_POINT ) );
		Sys_DLL_Unload( rm_state.moduleHandle );
		rm_state.moduleHandle = 0;
	}
}

/*
====================
RM_AppendFallbackReason
====================
*/
static void RM_AppendFallbackReason( rendererModuleStatus_t &status, const char *reason ) {
	if ( status.fallbackReason[ 0 ] != '\0' ) {
		idStr::Append( status.fallbackReason, sizeof( status.fallbackReason ), "; " );
	}
	idStr::Append( status.fallbackReason, sizeof( status.fallbackReason ), reason );
}

/*
====================
RM_TryLoadModuleApi

Attempts to activate one candidate API from the ladder. Returns true when the
candidate is now the active renderer path.
====================
*/
static bool RM_TryLoadModuleApi( rendererModuleApi_t api, rendererModuleStatus_t &status ) {
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	if ( api == RENDER_MODULE_API_GL ) {
		// this build shape statically links the OpenGL renderer; activating
		// it is always possible
		status.activeApi = RENDER_MODULE_API_GL;
		status.disposition = ( status.requestedApi == RENDER_MODULE_API_GL ) ?
				RENDER_MODULE_DISPOSITION_BUILTIN : RENDER_MODULE_DISPOSITION_FALLBACK;
		return true;
	}
#endif

	if ( !rm_state.activationAllowed ) {
		// outside the first-boot activation window the engine has already
		// bound decl/material/font state to the active renderer instance;
		// don't even load the candidate — its GetRenderAPI would register
		// module-side static cvars whose completion callbacks dangle after
		// the unload
		common->Warning( "r_renderApi '%s': renderer modules can only activate at engine startup; restart to apply",
				R_RendererModule_ApiName( api ) );
		RM_AppendFallbackReason( status, "renderer module activation requires an engine restart" );
		return false;
	}

	char modulePath[ sizeof( status.modulePath ) ];
	if ( !RM_ResolveModulePath( api, modulePath, sizeof( modulePath ) ) ) {
		RM_AppendFallbackReason( status, "module path resolution failed" );
		return false;
	}
	idStr::Copynz( status.modulePath, modulePath, sizeof( status.modulePath ) );

	common->Printf( "Loading renderer module: api='%s' path='%s'\n", R_RendererModule_ApiName( api ), modulePath );
	intptr_t handle = Sys_DLL_Load( modulePath );
	if ( handle == 0 ) {
		common->Warning( "renderer module '%s' failed to load", modulePath );
		RM_AppendFallbackReason( status, "module load failed" );
		return false;
	}

	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Warning( "renderer module '%s' has no %s entry point", modulePath, RENDER_API_ENTRY_POINT );
		Sys_DLL_Unload( handle );
		RM_AppendFallbackReason( status, "missing GetRenderAPI entry point" );
		return false;
	}

	renderImport_t moduleImport;
	RM_BuildImport( moduleImport );

	renderExport_t *moduleExport = GetRenderAPI( &moduleImport );

	const char *reason = "";
	if ( !RM_ExportCanRender( moduleExport, &reason ) ) {
		common->Warning( "renderer module '%s'%s%s cannot become the active renderer: %s; falling back to OpenGL. Use rendererVkProbe to inspect it.",
				modulePath,
				( moduleExport != NULL && moduleExport->moduleDescription != NULL ) ? " " : "",
				( moduleExport != NULL && moduleExport->moduleDescription != NULL ) ? moduleExport->moduleDescription : "",
				reason );
		if ( moduleExport != NULL && moduleExport->version == RENDER_API_VERSION && moduleExport->Shutdown != NULL ) {
			moduleExport->Shutdown();
		}
		Sys_DLL_Unload( handle );
		RM_AppendFallbackReason( status, reason );
		return false;
	}

	// The module links its own idlib archive, so its allocations live in a
	// separate set of counters. Optional symbol, looked up by name: a module
	// built before this existed just does not contribute to the total.
	Mem_RegisterModuleStats( ( memModuleStats_t )Sys_DLL_GetProcAddress( handle, MEM_MODULE_STATS_ENTRY_POINT ) );

	rm_state.moduleHandle = handle;
	rm_state.moduleExport = *moduleExport;
	rm_state.moduleExportValid = true;
	RM_PublishActiveModuleInterfaces( rm_state.moduleExport );
	status.activeApi = api;
	status.disposition = ( status.requestedApi == api ) ?
			RENDER_MODULE_DISPOSITION_MODULE : RENDER_MODULE_DISPOSITION_FALLBACK;
	return true;
}

/*
====================
R_RendererModule_Boot
====================
*/
void R_RendererModule_Boot( void ) {
	rendererModuleStatus_t &status = rm_state.status;

	if ( rm_state.interfacesPublished ) {
		// a module renderSystem is live; re-boots (vid_restart) must not
		// unload the code the engine is executing through
		return;
	}

	rm_state.activationAllowed = !rm_state.everBooted;
	rm_state.everBooted = true;

	RM_UnloadModule();
	memset( &status, 0, sizeof( status ) );

	const char *requestedValue = r_renderApi.GetString();
	idStr::Copynz( status.requestedValue, requestedValue, sizeof( status.requestedValue ) );

	rendererModuleApi_t requestedApi;
	if ( !R_RendererModule_ParseApi( requestedValue, requestedApi ) ) {
		common->Warning( "r_renderApi '%s' is not a valid rendering API; using 'gl'", requestedValue );
	}
#ifdef ID_DEDICATED
	// the dedicated server keeps the statically linked front-end until the
	// Phase B7 source unification; never activate a renderer module there
	if ( requestedApi != RENDER_MODULE_API_GL ) {
		common->Printf( "r_renderApi '%s' ignored on the dedicated server; using 'gl'\n", requestedValue );
		requestedApi = RENDER_MODULE_API_GL;
	}
#endif
	status.requestedApi = requestedApi;

	rendererModuleApi_t ladder[ RENDER_MODULE_API_COUNT ];
	const int numCandidates = R_RendererModule_BuildFallbackLadder( requestedApi, ladder, RENDER_MODULE_API_COUNT );

	bool activated = false;
	for ( int i = 0; i < numCandidates; i++ ) {
		if ( RM_TryLoadModuleApi( ladder[ i ], status ) ) {
			activated = true;
			break;
		}
	}
	if ( !activated ) {
		// on module-only builds the GL tail is the renderer-gl module, which
		// can genuinely fail (missing/mis-staged binary); on static builds
		// the tail cannot fail and this stays a guard
		common->FatalError( "no renderer path could be activated (requested '%s'): %s", requestedValue,
				status.fallbackReason[ 0 ] != '\0' ? status.fallbackReason : "no failure detail recorded" );
		return;
	}

	r_actualRenderApi.SetString( R_RendererModule_ApiName( status.activeApi ) );

	if ( status.disposition == RENDER_MODULE_DISPOSITION_FALLBACK ) {
		common->Warning( "renderer API fallback: requested '%s', active '%s' (%s)",
				R_RendererModule_ApiName( status.requestedApi ),
				R_RendererModule_ApiName( status.activeApi ),
				status.fallbackReason );
	} else {
		common->Printf( "Renderer API: %s (%s)\n", R_RendererModule_ApiName( status.activeApi ),
				status.disposition == RENDER_MODULE_DISPOSITION_MODULE ? "module" : "builtin" );
	}
}

/*
====================
RM_PeekConfigRenderApi

The archived r_renderApi value lives in the config file, but config execution
happens after renderSystem->Init() — too late for the module swap. Peek the
last r_renderApi set/seta out of the config text; command-line +set overrides
are applied on top by the caller.
====================
*/
static bool RM_PeekConfigRenderApi( char *outValue, int maxLength ) {
	char *buffer = NULL;
	if ( fileSystem->ReadFile( CONFIG_FILE, ( void ** )&buffer, NULL ) < 0 || buffer == NULL ) {
		return false;
	}

	bool found = false;
	idLexer src( buffer, idStr::Length( buffer ), CONFIG_FILE,
			LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS );
	idToken token, name, value;
	while ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "set" ) != 0 && token.Icmp( "seta" ) != 0 ) {
			src.SkipRestOfLine();
			continue;
		}
		if ( !src.ReadTokenOnLine( &name ) || name.Icmp( "r_renderApi" ) != 0 ) {
			src.SkipRestOfLine();
			continue;
		}
		if ( src.ReadTokenOnLine( &value ) ) {
			idStr::Copynz( outValue, value.c_str(), maxLength );
			found = true;		// last occurrence wins, like config execution
		}
		src.SkipRestOfLine();
	}

	fileSystem->FreeFile( buffer );
	return found;
}

/*
====================
Loader-owned console commands

Registered by the loader (not the renderer's R_InitCommands) so they exist
and reach the real loader diagnostics in every build shape, including
module-only clients where R_InitCommands runs inside the renderer module.
====================
*/
static void R_RendererModuleSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModule_RunSelfTest() ) {
		common->Warning( "Renderer module self-test failed" );
	}
}

static void R_RendererVkProbe_f( const idCmdArgs &args ) {
	const bool verbose = ( args.Argc() < 2 ) || ( idStr::Icmp( args.Argv( 1 ), "quiet" ) != 0 );
	R_RendererModule_RunVulkanProbe( verbose );
}

// implemented in ui/DeviceContext.cpp; declared here rather than through
// ui/DeviceContext.h so this TU keeps no include edge into src/ui
bool UI_FontParity_RunSelfTest( void );

static void R_UIFontParitySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !UI_FontParity_RunSelfTest() ) {
		common->Warning( "UI font parity self-test failed" );
	}
}

// Bounded engine-command diagnostic: mutate only an already hidden, windowed
// device. Save actual state rather than the unapplied archived CVar request.
static bool rm_displayProbeSaved = false;
static renderWindowRequest_t rm_displayProbeRestore = {};
static uint64_t rm_displayProbeEpoch = 0;
static bool RM_ParseProbeDimension( const char *text, int minimum, int maximum, int &value ) {
	if ( text == NULL || *text == '\0' ) return false;
	int parsed = 0;
	for ( const char *digit = text; *digit != '\0'; ++digit ) {
		if ( *digit < '0' || *digit > '9' || parsed > ( maximum - ( *digit - '0' ) ) / 10 ) return false;
		parsed = parsed * 10 + ( *digit - '0' );
	}
	if ( parsed < minimum || parsed > maximum ) return false;
	value = parsed;
	return true;
}
static void R_RendererDisplayProbe_f( const idCmdArgs &args ) {
	const char *operation = args.Argc() > 1 ? args.Argv( 1 ) : "report";
	rendererDisplayState_t state = {};
	const bool observed = R_RendererModule_QueryDisplay( &state );
	bool result = observed;
	char error[ 256 ] = {};
	if ( idStr::Icmp( operation, "save" ) == 0 ) {
		const uint32_t required = RDP_PARAMETER_SAMPLES | RDP_PARAMETER_SWAP_INTERVAL;
		result = args.Argc() == 2 && observed && state.rendererReady && state.windowValid
			&& state.window.hidden && !state.window.fullscreen && !state.window.minimized
			&& ( state.presentation.parametersValid & required ) == required;
		if ( result ) {
			rm_displayProbeRestore = {};
			rm_displayProbeRestore.parms.width = state.window.logicalWidth;
			rm_displayProbeRestore.parms.height = state.window.logicalHeight;
			rm_displayProbeRestore.parms.borderless = state.window.borderless;
			rm_displayProbeRestore.parms.hiddenWindow = true;
			rm_displayProbeRestore.parms.multiSamples = state.presentation.samples;
			rm_displayProbeRestore.displayId = state.window.displayId;
			rm_displayProbeRestore.displayIndex = state.window.displayIndex;
			rm_displayProbeRestore.swapInterval = state.presentation.swapInterval;
			rm_displayProbeRestore.restorePlacement = state.window.positionValid;
			rm_displayProbeRestore.windowX = state.window.windowX;
			rm_displayProbeRestore.windowY = state.window.windowY;
			rm_displayProbeRestore.maximized = state.window.maximized;
			rm_displayProbeEpoch = state.moduleEpoch;
			rm_displayProbeSaved = true;
		}
	} else if ( idStr::Icmp( operation, "apply" ) == 0 || idStr::Icmp( operation, "restore" ) == 0
		|| idStr::Icmp( operation, "missing-display" ) == 0 ) {
		const bool applying = idStr::Icmp( operation, "apply" ) == 0;
		const bool missingDisplay = idStr::Icmp( operation, "missing-display" ) == 0;
		const bool restoring = !applying && !missingDisplay;
		result = rm_displayProbeSaved && rm_displayProbeEpoch == rm_displayModuleEpoch
			&& ( applying ? ( args.Argc() >= 4 && args.Argc() <= 6 ) : args.Argc() == 2 )
			&& observed && ( state.windowValid ? state.window.hidden && !state.window.fullscreen && !state.window.minimized
				: restoring && !state.rendererReady );
		if ( result ) {
			renderWindowRequest_t request = rm_displayProbeRestore;
			if ( applying ) {
				request.maximized = false;
				result = RM_ParseProbeDimension( args.Argv( 2 ), 320, 16384, request.parms.width )
					&& RM_ParseProbeDimension( args.Argv( 3 ), 240, 16384, request.parms.height );
				if ( result && args.Argc() >= 5 ) {
					result = RM_ParseProbeDimension( args.Argv( 4 ), 0, 1, request.swapInterval );
				}
				if ( result && args.Argc() == 6 ) {
					result = RM_ParseProbeDimension( args.Argv( 5 ), 0, 16, request.parms.multiSamples )
						&& ( request.parms.multiSamples == 0 || request.parms.multiSamples == 2
							|| request.parms.multiSamples == 4 || request.parms.multiSamples == 8 || request.parms.multiSamples == 16 );
				}
			}
			if ( missingDisplay ) request.displayId = UINT32_MAX;
			if ( result ) result = R_RendererModule_TryDeviceRestart( &request, error, sizeof( error ) );
		}
	} else if ( idStr::Icmp( operation, "report" ) != 0 || args.Argc() > 2 ) result = false;
	const bool after = R_RendererModule_QueryDisplay( &state );
	common->Printf( "DISPLAY_PROBE operation=%s result=%d observed=%d epoch=%llu generation=%llu ready=%d window=%d "
		"available=%u outcome=%u submitted=%llu presented=%llu failures=%llu native=%d restart=%d "
		"logical=%dx%d pixel=%dx%d display=%u position=%d,%d hidden=%d fullscreen=%d maximized=%d samples=%d interval=%d valid=%u\n",
		operation, result ? 1 : 0, after ? 1 : 0,
		static_cast<unsigned long long>( state.moduleEpoch ), static_cast<unsigned long long>( state.presentation.generation ),
		state.rendererReady ? 1 : 0, state.windowValid ? 1 : 0, state.presentation.available, state.presentation.outcome,
		static_cast<unsigned long long>( state.presentation.submittedSequence ), static_cast<unsigned long long>( state.presentation.presentedSequence ),
		static_cast<unsigned long long>( state.presentation.failureSequence ), state.presentation.nativeError, state.videoRestartCount,
		state.window.logicalWidth, state.window.logicalHeight, state.window.pixelWidth, state.window.pixelHeight,
		state.window.displayId, state.window.windowX, state.window.windowY, state.window.hidden ? 1 : 0,
		state.window.fullscreen ? 1 : 0, state.window.maximized ? 1 : 0,
		state.presentation.samples, state.presentation.swapInterval, state.presentation.parametersValid );
	if ( error[ 0 ] ) common->Printf( "DISPLAY_PROBE_DETAIL %s\n", error );
}

static void RM_RegisterCommands( void ) {
	cmdSystem->AddCommand( "rendererDisplayProbe", R_RendererDisplayProbe_f, CMD_FL_RENDERER, "observe or exercise a hidden windowed display: report/save/apply width height [interval 0 or 1] [samples 0/2/4/8/16]/restore/missing-display" );
	cmdSystem->AddCommand( "rendererModuleSelfTest", R_RendererModuleSelfTest_f, CMD_FL_RENDERER, "run renderer module selection/loading self tests" );
	cmdSystem->AddCommand( "rendererVkProbe", R_RendererVkProbe_f, CMD_FL_RENDERER, "load the Vulkan renderer module, run its device bring-up probe, and unload it" );
	cmdSystem->AddCommand( "uiFontParitySelfTest", R_UIFontParitySelfTest_f, CMD_FL_RENDERER, "run GUI font retail parity self tests" );
}

/*
====================
R_RendererModule_BootEarly
====================
*/
void R_RendererModule_BootEarly( void ) {
	RM_RegisterCommands();

	char configValue[ 64 ];
	if ( RM_PeekConfigRenderApi( configValue, sizeof( configValue ) ) ) {
		r_renderApi.SetString( configValue );
	}
	// command-line +set r_renderApi overrides the archived value
	common->StartupVariable( "r_renderApi", false );

	R_RendererModule_Boot();
}

/*
====================
R_RendererModule_Shutdown
====================
*/
void R_RendererModule_Shutdown( void ) {
	RM_UnloadModule();
	RM_ReleaseDisplayVideoPin();
	RM_AdvanceDisplayEpoch();
	rm_state.status.disposition = RENDER_MODULE_DISPOSITION_NONE;
}

bool R_RendererModule_QueryDisplay( rendererDisplayState_t *outState ) {
	if ( outState == NULL || renderSystem == NULL || rm_displayModuleEpoch == 0 ) {
		return false;
	}
	void ( *query )( renderDisplayPresentation_t * ) = NULL;
	if ( rm_state.interfacesPublished && rm_state.moduleExportValid ) {
		query = rm_state.moduleExport.GetDisplayPresentation;
	}
#if !defined( OPENQ4_RENDERER_MODULE_ONLY ) && !defined( ID_DEDICATED )
	else if ( rm_state.status.disposition != RENDER_MODULE_DISPOSITION_NONE ) {
		query = R_GetDisplayPresentation;
	}
#endif
	if ( query == NULL ) {
		return false;
	}
	rendererDisplayState_t state = {};
	state.moduleEpoch = rm_displayModuleEpoch;
	renderDisplayPresentation_t before = {};
	query( &before );
	const renderWindowServices_t *windowServices = Sys_GetRenderWindowServices();
	state.windowValid = windowServices != NULL && windowServices->QueryWindowState != NULL
		&& windowServices->QueryWindowState( &state.window );
	state.rendererReady = renderSystem->IsOpenGLRunning();
	state.videoRestartCount = renderSystem->GetVideoRestartCount();
	query( &state.presentation );
	// A backend frame may finish during the window query. Its latest result is
	// useful, but never pair a window observation with a different device epoch.
	if ( state.moduleEpoch != rm_displayModuleEpoch || before.generation != state.presentation.generation ) {
		return false;
	}
	*outState = state;
	return true;
}

bool R_RendererModule_TryDeviceRestart( const renderWindowRequest_t *request, char *error, int errorSize ) {
	if ( error != NULL && errorSize > 0 ) {
		error[ 0 ] = '\0';
	}
	const renderWindowServices_t *windowServices = Sys_GetRenderWindowServices();
	if ( request == NULL || renderSystem == NULL || rm_displayModuleEpoch == 0 || windowServices == NULL
		|| windowServices->ApplyScreenParmsStrict == NULL || windowServices->QueryWindowState == NULL
		|| windowServices->RetainVideoSystem == NULL || windowServices->ReleaseVideoSystem == NULL ) {
		if ( error != NULL && errorSize > 0 ) {
			idStr::Copynz( error, "strict display services are unavailable", errorSize );
		}
		return false;
	}
	bool ( *restart )( const renderWindowRequest_t *, char *, int ) = NULL;
	if ( rm_state.interfacesPublished && rm_state.moduleExportValid ) restart = rm_state.moduleExport.TryDeviceRestart;
#if !defined( OPENQ4_RENDERER_MODULE_ONLY ) && !defined( ID_DEDICATED )
	else if ( rm_state.status.disposition != RENDER_MODULE_DISPOSITION_NONE ) {
		restart = R_TryFullVidRestart;
	}
#endif
	if ( restart != NULL ) {
		if ( rm_displayVideoPin == NULL ) {
			if ( !windowServices->RetainVideoSystem() ) {
				if ( error != NULL && errorSize > 0 ) idStr::Copynz( error, "cannot retain the active video subsystem", errorSize );
				return false;
			}
			rm_displayVideoPin = windowServices;
		}
		const bool result = restart( request, error, errorSize );
		// A failed attempt with no context needs this identity lease until an
		// explicit restore. Releasing the last SDL reference would invalidate
		// its saved display ID. A live device already holds its own reference.
		if ( result || renderSystem->IsOpenGLRunning() ) RM_ReleaseDisplayVideoPin();
		return result;
	}
	if ( error != NULL && errorSize > 0 ) {
		idStr::Copynz( error, "active renderer has no recoverable device service", errorSize );
	}
	return false;
}

bool R_RendererModule_TryInitializeDisplay( const renderWindowRequest_t *request, char *error, int errorSize ) {
	if ( error != NULL && errorSize > 0 ) error[0] = '\0';
	const renderWindowServices_t *windowServices = Sys_GetRenderWindowServices();
	if ( request == NULL || renderSystem == NULL || rm_displayModuleEpoch == 0 || renderSystem->IsOpenGLRunning() || windowServices == NULL ||
		windowServices->ApplyScreenParmsStrict == NULL || windowServices->QueryWindowState == NULL ||
		windowServices->RetainVideoSystem == NULL || windowServices->ReleaseVideoSystem == NULL ) {
		if ( error != NULL && errorSize > 0 ) idStr::Copynz( error, "strict initial display services are unavailable or the renderer is already running", errorSize );
		return false;
	}
	bool ( *initialize )( const renderWindowRequest_t *, char *, int ) = NULL;
	if ( rm_state.interfacesPublished && rm_state.moduleExportValid ) initialize = rm_state.moduleExport.TryInitializeDisplay;
#if !defined( OPENQ4_RENDERER_MODULE_ONLY ) && !defined( ID_DEDICATED )
	else if ( rm_state.status.disposition != RENDER_MODULE_DISPOSITION_NONE ) initialize = R_TryInitializeDisplay;
#endif
	if ( initialize == NULL ) {
		if ( error != NULL && errorSize > 0 ) idStr::Copynz( error, "active renderer has no strict initial display service", errorSize );
		return false;
	}
	// Startup prepares video before resolving portable monitor identities. Pin
	// that same SDL lifetime across partial initialization cleanup and explicit
	// retries, just as a failed live-device restart pins its original identity.
	if ( rm_displayVideoPin == NULL ) {
		if ( !windowServices->RetainVideoSystem() ) {
			if ( error != NULL && errorSize > 0 ) idStr::Copynz( error, "cannot retain the prepared video subsystem", errorSize );
			return false;
		}
		rm_displayVideoPin = windowServices;
	}
	const bool result = initialize( request, error, errorSize );
	if ( result && renderSystem->IsOpenGLRunning() ) {
		RM_ReleaseDisplayVideoPin();
		return true;
	}
	if ( result && error != NULL && errorSize > 0 ) idStr::Copynz( error, "initial display service returned without a ready renderer", errorSize );
	return false;
}

/*
====================
R_RendererModule_GetStatus
====================
*/
const rendererModuleStatus_t &R_RendererModule_GetStatus( void ) {
	return rm_state.status;
}

/*
====================
RendererModule_PrintGfxInfo
====================
*/
void RendererModule_PrintGfxInfo( void ) {
	const rendererModuleStatus_t &status = rm_state.status;
	const char *dispositionName = "not-booted";

	switch ( status.disposition ) {
		case RENDER_MODULE_DISPOSITION_BUILTIN:	dispositionName = "builtin"; break;
		case RENDER_MODULE_DISPOSITION_MODULE:	dispositionName = "module"; break;
		case RENDER_MODULE_DISPOSITION_FALLBACK:	dispositionName = "fallback"; break;
		default: break;
	}

	common->Printf( "Renderer API: requested=%s active=%s disposition=%s\n",
			R_RendererModule_ApiName( status.requestedApi ),
			R_RendererModule_ApiName( status.activeApi ),
			dispositionName );
	if ( status.modulePath[ 0 ] != '\0' ) {
		common->Printf( "Renderer module path: %s\n", status.modulePath );
	}
	if ( status.fallbackReason[ 0 ] != '\0' ) {
		common->Printf( "Renderer API fallback reason: %s\n", status.fallbackReason );
	}
}

/*
====================
R_RendererModule_RunVulkanProbe

Loads the Vulkan module for a diagnostics pass and unloads it afterwards.
Never touches the live GL context or window; the probe is instance/device
scoped only.
====================
*/
bool R_RendererModule_RunVulkanProbe( bool verbose ) {
	char modulePath[ 1024 ];

	if ( !RM_ResolveModulePath( RENDER_MODULE_API_VULKAN, modulePath, sizeof( modulePath ) ) ) {
		common->Printf( "rendererVkProbe: module path resolution failed\n" );
		return false;
	}
	common->Printf( "rendererVkProbe: loading '%s'\n", modulePath );

	intptr_t handle = Sys_DLL_Load( modulePath );
	if ( handle == 0 ) {
		common->Printf( "rendererVkProbe: module not present or failed to load (build with -Dbuild_renderer_vk=true and stage it next to the executable)\n" );
		return false;
	}

	bool probePassed = false;
	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Printf( "rendererVkProbe: module has no %s entry point\n", RENDER_API_ENTRY_POINT );
	} else {
		renderImport_t moduleImport;
		RM_BuildImport( moduleImport );

		renderExport_t *moduleExport = GetRenderAPI( &moduleImport );

		bool diagnosticsOnly = false;
		const char *reason = "";
		if ( !RM_ValidateExport( moduleExport, diagnosticsOnly, &reason ) ) {
			common->Printf( "rendererVkProbe: module rejected: %s\n", reason );
		} else if ( moduleExport->diagnostics == NULL || moduleExport->diagnostics->RunProbe == NULL ) {
			common->Printf( "rendererVkProbe: module exposes no probe diagnostics\n" );
		} else {
			common->Printf( "rendererVkProbe: module '%s' (%s)\n",
					moduleExport->backendName,
					moduleExport->moduleDescription != NULL ? moduleExport->moduleDescription : "no description" );
			probePassed = moduleExport->diagnostics->RunProbe( verbose );
			common->Printf( "rendererVkProbe: %s\n", probePassed ? "PASS" : "FAIL" );
		}
		if ( moduleExport != NULL && moduleExport->version == RENDER_API_VERSION && moduleExport->Shutdown != NULL ) {
			moduleExport->Shutdown();
		}
	}

	Sys_DLL_Unload( handle );
	return probePassed;
}

/*
====================
RendererModule_RunSelfTest

Pure-logic checks that need no window, device, or module binary: api-string
parsing, module naming, ladder composition, and export validation rules.
====================
*/
bool RendererModule_RunSelfTest( void ) {
	int numFailures = 0;

	// api parsing
	{
		rendererModuleApi_t api;
		if ( !R_RendererModule_ParseApi( "gl", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: parse 'gl' failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "VULKAN", api ) || api != RENDER_MODULE_API_VULKAN ) {
			common->Warning( "rendererModuleSelfTest: parse 'VULKAN' failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "vk", api ) || api != RENDER_MODULE_API_VULKAN ) {
			common->Warning( "rendererModuleSelfTest: parse 'vk' alias failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "best", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: 'best' must resolve to gl until promotion evidence lands" );
			numFailures++;
		}
		if ( R_RendererModule_ParseApi( "glide", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: invalid api must be rejected and select gl" );
			numFailures++;
		}
	}

	// module naming
	{
		char name[ MAX_OSPATH ];
		R_RendererModule_BuildBinaryName( RENDER_MODULE_API_VULKAN, name, sizeof( name ) );
		idStr expected = va( "renderer-vk_%s", RENDERER_MODULE_ARCH_TAG );
		if ( expected.Cmp( name ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: vulkan module name '%s', expected '%s'", name, expected.c_str() );
			numFailures++;
		}
		R_RendererModule_BuildBinaryName( RENDER_MODULE_API_GL, name, sizeof( name ) );
		expected = va( "renderer-gl_%s", RENDERER_MODULE_ARCH_TAG );
		if ( expected.Cmp( name ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: gl module name '%s', expected '%s'", name, expected.c_str() );
			numFailures++;
		}

		// a universal2 macOS package carries one two-slice module, so both
		// executable slices must be able to name it
		R_RendererModule_BuildBinaryNameForArch( RENDER_MODULE_API_VULKAN, "universal2", name, sizeof( name ) );
		if ( idStr::Cmp( name, "renderer-vk_universal2" ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: universal2 vulkan module name '%s', expected 'renderer-vk_universal2'", name );
			numFailures++;
		}
	}

	// fallback ladder composition
	{
		rendererModuleApi_t ladder[ RENDER_MODULE_API_COUNT ];
		int numEntries = R_RendererModule_BuildFallbackLadder( RENDER_MODULE_API_VULKAN, ladder, RENDER_MODULE_API_COUNT );
		if ( numEntries != 2 || ladder[ 0 ] != RENDER_MODULE_API_VULKAN || ladder[ 1 ] != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: vulkan ladder must be [vulkan, gl]" );
			numFailures++;
		}
		numEntries = R_RendererModule_BuildFallbackLadder( RENDER_MODULE_API_GL, ladder, RENDER_MODULE_API_COUNT );
		if ( numEntries != 1 || ladder[ 0 ] != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: gl ladder must be [gl]" );
			numFailures++;
		}
	}

	// export validation rules
	{
		bool diagnosticsOnly = false;
		const char *reason = "";

		if ( RM_ValidateExport( NULL, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: NULL export must be rejected" );
			numFailures++;
		}

		renderExport_t testExport;
		memset( &testExport, 0, sizeof( testExport ) );
		testExport.version = RENDER_API_VERSION + 1;
		testExport.backendName = "vulkan";
		if ( RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: version mismatch must be rejected" );
			numFailures++;
		}

		testExport.version = RENDER_API_VERSION;
		testExport.backendName = NULL;
		if ( RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: missing backend name must be rejected" );
			numFailures++;
		}

		testExport.backendName = "vulkan";
		testExport.renderSystem = NULL;
		if ( !RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) || !diagnosticsOnly ) {
			common->Warning( "rendererModuleSelfTest: bring-up export must validate as diagnostics-only" );
			numFailures++;
		}

		// activation policy: bring-up exports stay diagnostics-only; a full
		// export is activatable now that the Phase B8 seam hosts module
		// renderers (the first-boot activation window is enforced separately
		// in RM_TryLoadModuleApi)
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: diagnostics-only export must not be activatable" );
			numFailures++;
		}
		static int dummyRenderSystemStorage;
		testExport.renderSystem = reinterpret_cast<idRenderSystem *>( &dummyRenderSystemStorage );
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: full export without device services must be rejected" );
			numFailures++;
		}
		testExport.TryDeviceRestart = []( const renderWindowRequest_t *, char *, int ) { return false; };
		testExport.GetDisplayPresentation = []( renderDisplayPresentation_t * ) {};
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: full export without strict initial device service must be rejected" );
			numFailures++;
		}
		testExport.TryInitializeDisplay = []( const renderWindowRequest_t *, char *, int ) { return false; };
		if ( !RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: full exports must be activatable with the Phase B8 seam landed" );
			numFailures++;
		}
		testExport.renderSystem = NULL;
	}

	// import completeness: every v2 interface pointer and service binding
	// must be filled by the shared builder
	{
		renderImport_t testImport;
		RM_BuildImport( testImport );
		if ( testImport.version != RENDER_API_VERSION ) {
			common->Warning( "rendererModuleSelfTest: import version must be RENDER_API_VERSION" );
			numFailures++;
		}
		if ( testImport.services == NULL
				|| testImport.services->Sleep == NULL
				|| testImport.services->EnterCriticalSection == NULL
				|| testImport.services->LeaveCriticalSection == NULL
				|| testImport.services->IsRenderDocInjected == NULL ) {
			common->Warning( "rendererModuleSelfTest: v2 service bindings incomplete" );
			numFailures++;
		}
		if ( testImport.sys == NULL || testImport.common == NULL || testImport.cvarSystem == NULL
				|| testImport.cmdSystem == NULL || testImport.fileSystem == NULL
				|| testImport.declManager == NULL || testImport.soundSystem == NULL
				|| testImport.session == NULL || testImport.uiManager == NULL
				|| testImport.collisionModelManager == NULL || testImport.eventLoop == NULL
				|| testImport.bse == NULL ) {
			common->Warning( "rendererModuleSelfTest: v2 import interface pointers incomplete" );
			numFailures++;
		}
	}

	if ( numFailures > 0 ) {
		common->Warning( "RendererModule self-test failed: %d failure(s)", numFailures );
		return false;
	}
	common->Printf( "RendererModule self-test passed\n" );
	return true;
}
