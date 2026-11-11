// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __RENDERERMODULE_H__
#define __RENDERERMODULE_H__

#include "RenderModuleAPI.h"
#include "DisplayPresentation.h"
#include "RendererResourceSettings.h"

/*
===============================================================================

	Engine-side renderer-module selection and loading.

	Consumes r_renderApi, resolves renderer module binaries next to the
	executable (never through the game/mod search path), performs the
	GetRenderAPI handshake, runs a module's device probe before activating
	it, and falls back closed to the OpenGL renderer on every failure it can
	see so a bad selection can never leave the user without a picture. A
	device that still fails after activation points the next launch at
	OpenGL instead.

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

// Startup-only two-phase recovery: preparation returns through the module;
// after ShutdownGame has released every owner, retire the failed module and
// permit one OpenGL activation during the next InitGame/BootEarly sequence.
bool	R_RendererModule_PrepareStartupDevice( void );
bool	R_RendererModule_RetryFailedStartup( void );

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
// The request applied to the running device: the window changes in place and
// the device, its images and its buffers stay. False without any change when
// the request needs a new device or the renderer has no such service. False
// after the window started to change leaves the device running; the caller
// then restarts the device on the request or restores its captured display.
bool R_RendererModule_TryDisplayTransition( const renderWindowRequest_t *request, char *error, int errorSize );
struct rendererImagePolicyResult_t {
    uint64_t moduleEpoch;
    renderImagePolicyResult_t resources;
};
// Copies request; false leaves output unchanged. Holds the same video identity
// lease as display restart until success/live context or explicit restoration.
bool R_RendererModule_TryImagePolicyRestart(const renderImagePolicyRequest_t* request,
    rendererImagePolicyResult_t* output, char* error, int errorSize);
struct rendererImageRecoveryLease_t {uint64_t moduleEpoch=0;renderImageRecoveryLease_t resources{};};
bool R_RendererModule_PrepareImageRecovery(uint64_t owner,uint64_t request,const char* attempt,const renderImagePolicy_t* target,
    rendererImageRecoveryLease_t* output,char* error,int size);
bool R_RendererModule_PrepareColdImageRecovery(uint64_t owner,uint64_t request,const char* attempt,uint32_t direction,
    const char* raw,uint32_t bytes,rendererImageRecoveryLease_t* output,char* error,int size);
bool R_RendererModule_CaptureImageRecovery(const rendererImageRecoveryLease_t*,uint32_t direction,char* output,uint32_t capacity,
    uint32_t* bytes,char* error,int size);
bool R_RendererModule_CancelImageRecovery(const rendererImageRecoveryLease_t*,char* error,int size);
bool R_RendererModule_ReleaseImageRecovery(const rendererImageRecoveryLease_t*,uint32_t direction,
    const rendererImagePolicyResult_t*,char* error,int size);
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

// the active module could not start its device, too late for the in-process
// ladder: selects gl for the next launch, appending it to the saved config
// when that config selects vulkan, and returns true when it did. Module builds
// forward this to the engine's loader through the services table.
bool	R_RendererModule_ResetApiAfterDeviceFailure( void );

// Settings reports (render API 20). The renderer calls the first three; module
// builds forward them to the engine through the services table. The engine
// keeps the latest report of each kind with a nonreused serial and the module
// epoch it came from, and forgets them when the module changes.
void	R_RendererModule_PublishRendererSelection( const renderRendererSelection_t *selection );
bool	R_RendererModule_GetLightGridLoadPolicy( bool *preload, uint64_t *token );
void	R_RendererModule_PublishLightGridLoadReceipt( const renderLightGridLoadReceipt_t *receipt );
bool	R_RendererModule_QueryRendererSelection( renderRendererSelection_t &selection, uint64_t &serial, uint64_t &epoch );
struct renderDeviceCatalog_t;
bool R_RendererModule_QueryRenderingDevices(renderDeviceCatalog_t& output);
void R_RendererModule_SyncConfiguration();
bool R_RendererModule_RenderSelectionPending(const char* api, const char* device);
// After checked menu persistence, supersede the original launch preferences
// for this process's explicit renderer reload. Other launch options survive.
void R_RendererModule_UseSavedSelectionOnReload();
// Engine command boundary only, after ShutdownGame has retired every owner.
bool R_RendererModule_FinishSelectionReload();
bool	R_RendererModule_QueryLightGridLoad( renderLightGridLoadReceipt_t &receipt, uint64_t &serial, uint64_t &epoch );

// OpenXR (render API 21): the renderer reports RENDER_DEVICE_READY after every
// device start and RENDER_DEVICE_STOPPING before every device teardown. The
// engine hands them to its VR system; module builds forward them through the
// services table.
void	R_RendererModule_RendererDeviceEvent( int event );

// OpenXR on Vulkan (render API 32): the Vulkan renderer offers the creation of
// its instance, physical device choice and device to the engine's VR system,
// which makes them through the XR runtime when VR wants this renderer. False
// leaves the object to the renderer. Module builds forward these through the
// services table.
bool	R_RendererModule_CreateVulkanInstanceForVR( void *getInstanceProcAddr, const void *createInfo,
												  void **outInstance, int *outResult );
bool	R_RendererModule_GetVulkanPhysicalDeviceForVR( void *instance, void **outPhysicalDevice );
bool	R_RendererModule_CreateVulkanDeviceForVR( void *getInstanceProcAddr, void *physicalDevice, const void *createInfo,
												void **outDevice, int *outResult );

#endif /* !__RENDERERMODULE_H__ */
