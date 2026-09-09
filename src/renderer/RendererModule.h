// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __RENDERERMODULE_H__
#define __RENDERERMODULE_H__

#include "RenderModuleAPI.h"
#include "DisplayPresentation.h"

/*
===============================================================================

	Engine-side renderer-module selection and loading.

	Consumes r_renderApi, resolves renderer module binaries next to the
	executable (never through the game/mod search path), performs the
	GetRenderAPI handshake, and falls back closed to the OpenGL renderer on
	every failure class so a bad selection can never leave the user without
	a picture.

===============================================================================
*/

typedef enum {
	RENDER_MODULE_API_GL,		// OpenGL renderer, statically linked (default)
	RENDER_MODULE_API_VULKAN,	// native Vulkan renderer module
	RENDER_MODULE_API_GL_MODULE,// OpenGL renderer as a dynamic module (opt-in Phase B8 soak path)
	RENDER_MODULE_API_GLES,		// OpenGL ES renderer module (Android backend bring-up; macOS needs ANGLE)
	RENDER_MODULE_API_COUNT
} rendererModuleApi_t;

typedef enum {
	RENDER_MODULE_DISPOSITION_NONE,				// boot not run yet
	RENDER_MODULE_DISPOSITION_BUILTIN,			// statically linked renderer active
	RENDER_MODULE_DISPOSITION_MODULE,			// dynamic module renderer active
	RENDER_MODULE_DISPOSITION_FALLBACK,			// requested API failed; GL fallback active
} rendererModuleDisposition_t;

typedef struct rendererModuleStatus_s {
	rendererModuleApi_t				requestedApi;
	rendererModuleApi_t				activeApi;
	rendererModuleDisposition_t		disposition;
	char							requestedValue[ 32 ];	// raw r_renderApi string
	char							modulePath[ 1024 ];		// resolved module path when one was attempted
	char							fallbackReason[ 256 ];	// human-readable failure chain
} rendererModuleStatus_t;

// maps an r_renderApi value to an API; "best" resolves to the platform
// default (GL until Vulkan promotion evidence lands); returns false and
// selects GL for unrecognized values
bool	R_RendererModule_ParseApi( const char *value, rendererModuleApi_t &api );

const char *R_RendererModule_ApiName( rendererModuleApi_t api );

// builds the arch-tagged module base name, e.g. "renderer-vk_x64"
void	R_RendererModule_BuildBinaryName( rendererModuleApi_t api, char *outName, int maxLength );
// same, for an explicit architecture token; macOS universal2 packages carry a
// single two-slice module whose name cannot be this slice's own arch tag
void	R_RendererModule_BuildBinaryNameForArch( rendererModuleApi_t api, const char *archTag, char *outName, int maxLength );

// composes the fail-closed candidate ladder for a requested API; returns the
// number of entries written (GL is always the final entry)
int		R_RendererModule_BuildFallbackLadder( rendererModuleApi_t requested, rendererModuleApi_t *outLadder, int maxEntries );

// consumes r_renderApi and activates the selected renderer path; called at
// the head of R_InitOpenGL, before platform window creation. Module
// activation (publishing a module's idRenderSystem) is only permitted on the
// first boot of the process; vid_restart re-boots can shuffle gl/vulkan
// fallbacks but a module swap needs an engine restart.
void	R_RendererModule_Boot( void );

// first-boot entry called from idCommonLocal::InitGame BEFORE
// renderSystem->Init(), so decl/material/font state binds to the renderer
// instance that will actually draw. Peeks the archived r_renderApi value out
// of the config file (config execution happens later in InitGame) and applies
// command-line overrides before booting.
void	R_RendererModule_BootEarly( void );

// unloads any loaded renderer module; safe to call when none is loaded
void	R_RendererModule_Shutdown( void );

const rendererModuleStatus_t &R_RendererModule_GetStatus( void );

// Engine-owned identity survives module unload/reload. A renderer-local counter
// alone cannot identify a device across module lifetimes. These operations run
// on the main/video thread, serialized with module loading and frame submission.
struct rendererDisplayState_t {
	uint64_t moduleEpoch;
	renderDisplayPresentation_t presentation;
	renderWindowState_t window;
	bool windowValid;
	bool rendererReady;
	int videoRestartCount;
};

// Query failure leaves output unchanged. A supported but failed/uninitialized
// device is a successful observation with rendererReady/windowValid false.
bool R_RendererModule_QueryDisplay( rendererDisplayState_t *outState );
// Does not switch renderer modules, execute console commands or select fallback
// modes. Caller must drain frame work, retain recovery state and perform rollback.
bool R_RendererModule_TryDeviceRestart( const renderWindowRequest_t *request, char *error, int errorSize );
// Strict first device only, after Init and before any world/UI frame. The caller
// prepares SDL video, resolves portable monitor identity and journals before
// calling. Failure keeps the video identity pin until explicit retry or unload;
// success means device resources ready, never proof of a submitted/presented frame.
bool R_RendererModule_TryInitializeDisplay( const renderWindowRequest_t *request, char *error, int errorSize );

void	RendererModule_PrintGfxInfo( void );

bool	RendererModule_RunSelfTest( void );

// loads the Vulkan renderer module on demand, runs its bring-up probe, and
// unloads it again; used by the rendererVkProbe console command
bool	R_RendererModule_RunVulkanProbe( bool verbose );

#endif /* !__RENDERERMODULE_H__ */
