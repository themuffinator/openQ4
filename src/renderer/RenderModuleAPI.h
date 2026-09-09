// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __RENDERMODULEAPI_H__
#define __RENDERMODULEAPI_H__

/*
===============================================================================

	Renderer module ABI.

	Renderer backends (renderer-gl_<arch>, renderer-vk_<arch>) are dynamic
	modules loaded from the executable/package root and selected with the
	r_renderApi cvar. The handshake mirrors the game-module GetGameAPI
	convention: a single versioned extern "C" entry point exchanging import
	and export structs.

	This header must compile both inside engine translation units and inside
	standalone renderer-module translation units that do not link idlib, so
	everything a bring-up module needs is expressed through the plain C
	services table. The full C++ engine interfaces are also carried (as
	opaque pointers to a standalone module) for full renderer modules that
	compile against the engine headers.

	See docs/dev/plans/2026-07-16-vulkan-renderer.md for the roadmap.

===============================================================================
*/

// version history:
//  1 - Phase A bring-up handshake (services table, diagnostics surface)
//  2 - Phase B4 import/export completion: session/uiManager/
//      collisionModelManager/bse/eventLoop imports, renderModelManager
//      export, sleep/critical-section/RenderDoc services
//  3 - Phase B5b window services: the engine owns the window/display layer
//      and the renderer drives context negotiation through
//      renderWindowServices_t
//  4 - Phase B8 module completion: console import, input window services
//      (InitInput/ShutdownInput/GrabMouseCursor) for the renderer's init and
//      vid_restart sequencing
//  5 - Phase B closure: idRenderSystem vtable changes (POD
//      GetCurrentLightGridBakeInfo, GetGLConfig) and idMaterial
//      cross-module virtuals; a module built against a different layout
//      must be rejected
//  6 - Phase C: Vulkan surface services — renderFramebufferDesc_t carries a
//      surface kind so the engine creates the window for the right API, and
//      the window services gain SDL-mediated Vulkan instance-extension and
//      surface creation (the module never links SDL)
//  7 - Extended renderEntity_t presentation contract (flat diffuse colour
//      and sweep flags)
//  8 - Append-only idRenderSystem slot for coherent full-frame capture with
//      CPU center-crop/resampling to caller-selected output dimensions
//  9 - Append-only idRenderSystem slots for backend-neutral delayed whole-frame
//      GPU timing and session-discontinuity resets (OpenGL and Vulkan)
// 10 - Shared idFile/idFileSystem ABI adds engine-owned memory staging,
//      semantic preload records, and source-authoritative generated caches
// 11 - Append-only idRenderSystem slots publish frame-latched scene/native
//      output state and enqueue the backend-neutral temporal presentation
//      resolve requested by game modules
// 12 - renderFramebufferDesc_t::glESProfile lets a module ask for an OpenGL ES
//      context; a stale module would leave that byte uninitialised
// 13 - ClearRenderTarget carries alpha for transparent retained UI layers.
// 14 - Strict window requests/readback and private recoverable device services.
#define RENDER_API_VERSION			14
#define RENDER_API_ENTRY_POINT		"GetRenderAPI"

class idSys;
class idCommon;
class idCVarSystem;
class idCmdSystem;
class idFileSystem;
class idDeclManager;
class idSoundSystem;
class idRenderSystem;
class idRenderModelManager;
class idSession;
class idUserInterfaceManager;
class idCollisionModelManager;
class idEventLoop;
class rvBSEManager;
class idConsole;

// bring-up-safe engine services; every callback is bound engine-side and
// remains valid for the lifetime of the loaded module
typedef struct renderModuleServices_s {
	void			( *Printf )( const char *fmt, ... );
	void			( *Warning )( const char *fmt, ... );
	void			( *Error )( const char *fmt, ... );		// does not return
	int				( *Milliseconds )( void );
	const char *	( *CVar_GetString )( const char *name );
	int				( *CVar_GetInteger )( const char *name );
	bool			( *CVar_GetBool )( const char *name );
	void			( *CVar_SetString )( const char *name, const char *value );

	// --- version 2 ---
	void			( *Sleep )( int msec );
	// engine critical-section slots (CRITICAL_SECTION_* indices)
	void			( *EnterCriticalSection )( int index );
	void			( *LeaveCriticalSection )( int index );
	bool			( *IsRenderDocInjected )( void );

	// --- version 5: the loader owns request/active/disposition status; the
	// module's gfxInfo routes here so the report stays truthful ---
	void			( *PrintRendererApiStatus )( void );
	// worker-thread creation intentionally not carried yet: the light-grid
	// bake pool's Sys_CreateThread signature (thread registry, priority,
	// threadInfo_t) is resolved by the module-side forwarder design in
	// Phase B8 (docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md)
} renderModuleServices_t;

// native window/surface handoff; zeroed until the engine has created a window
typedef struct renderModuleWindowInfo_s {
	bool			hasWindow;
	void *			nativeWindowHandle;		// HWND / X11 Window / wl_surface* / NSWindow*
	void *			nativeDisplayHandle;	// HDC / X11 Display* / wl_display*
	void *			sdlWindow;				// SDL_Window* when the SDL3 backend is active
	int				pixelWidth;
	int				pixelHeight;
	// --- version 5: live engine window state the module polls each present
	// (the engine no longer mirrors sizes into a renderer-owned glConfig) ---
	int				uiViewportX;
	int				uiViewportY;
	int				uiViewportWidth;
	int				uiViewportHeight;
} renderModuleWindowInfo_t;

/*
===============================================================================
	Window services (version 3)

	The engine owns the platform window, display enumeration, mode and
	fullscreen policy, and input; the renderer module owns the rendering
	context and drives the candidate negotiation loop through these
	callbacks. Window recreation on context-attribute changes is expressed
	by CreateWindowForFramebuffer/DestroyAttemptWindow so a failed context
	attempt can retry with different attributes exactly like the previous
	single-owner loop.
===============================================================================
*/

// which rendering API the window must be created for; zero-initialized
// descs request a GL window, matching every pre-v6 caller
typedef enum {
	RENDER_SURFACE_GL = 0,
	RENDER_SURFACE_VULKAN,
} renderSurfaceKind_t;

// requested framebuffer/context attributes for one negotiation attempt
typedef struct renderFramebufferDesc_s {
	int				redBits, greenBits, blueBits, alphaBits, depthBits, stencilBits;
	bool			doubleBuffer;
	bool			stereo;
	int				multiSamples;		// >1 enables MSAA buffers
	bool			explicitGLVersion;	// else an unversioned request
	int				glMajor, glMinor;
	bool			glCoreProfile;		// else compatibility profile
	bool			glDebugContext;
	// --- version 6 ---
	int				surfaceKind;		// renderSurfaceKind_t; GL attributes above are ignored for Vulkan
	// --- version 12 ---
	bool			glESProfile;		// request an OpenGL ES context; overrides glCoreProfile
} renderFramebufferDesc_t;

// ABI-neutral mirror of the renderer's glimpParms_t
typedef struct renderWindowParms_s {
	int				width, height;
	bool			fullScreen;
	bool			borderless;
	bool			hiddenWindow;
	bool			stereo;
	int				displayHz;
	int				multiSamples;
} renderWindowParms_t;

// Immutable display request. A nonzero displayId is authoritative across display
// enumeration changes; displayIndex is used only when displayId is zero (-1 Auto).
// Windowed dimensions are logical window units; exclusive dimensions are pixels.
// swapInterval and multiSamples are validated by the renderer, not SDL windowing.
typedef struct renderWindowRequest_s {
	renderWindowParms_t parms;
	unsigned int displayId;
	int displayIndex;
	bool fullscreenDesktop;
	bool spanDisplays;
	int swapInterval;
	// Explicit baseline restoration. Position is authoritative only when true;
	// maximized is applied independently, including on positionless compositors.
	bool restorePlacement;
	int windowX, windowY;
	bool maximized;
} renderWindowRequest_t;

// Observations, never echoes of requested CVars. False query/apply results leave
// output unchanged. A failed strict apply may have partially changed the window;
// the caller must explicitly restore its captured state before persisting it.
typedef struct renderWindowState_s {
	unsigned int displayId;
	int displayIndex;
	unsigned long long windowFlags;
	bool fullscreen, fullscreenDesktop, borderless, hidden, minimized, maximized, focused;
	int windowX, windowY, logicalWidth, logicalHeight, pixelWidth, pixelHeight;
	bool positionValid, currentModeValid;
	int modeWidth, modeHeight, modePixelWidth, modePixelHeight;
	float refreshRate, modePixelDensity, displayScale, pixelDensityX, pixelDensityY;
	int uiViewportX, uiViewportY, uiViewportWidth, uiViewportHeight;
} renderWindowState_t;

typedef struct renderWindowServices_s {
	// idempotent window-system bring-up: video subsystem, hints, lifecycle
	// watch, display enumeration/diagnostics
	bool			( *PrepareWindowSystem )( void );
	// sets the framebuffer/context attributes and creates (or reuses a
	// preserved) window with initial placement; *outReusedPreservedWindow
	// tells the caller whether a failed attempt may destroy the window
	bool			( *CreateWindowForFramebuffer )( const renderFramebufferDesc_t *desc, const renderWindowParms_t *parms,
													 renderModuleWindowInfo_t *outInfo, bool *outReusedPreservedWindow );
	// failed-candidate teardown; only destroys a window the current attempt
	// created
	void			( *DestroyAttemptWindow )( void );
	// fullscreen/mode/placement application after context creation
	bool			( *ApplyScreenParms )( const renderWindowParms_t *parms );
	// refreshes native handles (HWND/HDC etc.) into outInfo
	void			( *RefreshNativeWindowHandles )( renderModuleWindowInfo_t *outInfo );
	// focus/active-app bookkeeping once the context is live
	void			( *NotifyWindowReady )( void );
	// pre-context-destroy window teardown (input, fullscreen restore, text
	// input), preserve-aware via the engine-side preserve flag
	void			( *BeginWindowTeardown )( void );
	// post-context-destroy window teardown (window destroy, video unref,
	// handle clears, input queues), preserve-aware
	void			( *FinishWindowTeardown )( void );
	void			( *GetDesktopResolution )( int *width, int *height );
	// video-driver quirk: try the unversioned compatibility candidates first
	// (native Wayland, Cocoa); *outMessage receives the log line to print
	bool			( *PreferCompatibilityFallbackFirst )( const char **outMessage );
	// context-error accounting (legacy wglErrors counter)
	void			( *CountContextError )( void );

	// --- GL context primitives, executed on the engine's video instance ---
	// The engine links the windowing library (SDL) exactly once; the module
	// owns context lifetime and policy but performs the primitive operations
	// through these callbacks so no second windowing-library instance ever
	// touches the engine's window.
	void *			( *CreateGLContext )( void );				// on the current game window
	bool			( *MakeGLContextCurrent )( void *context );	// NULL detaches
	void			( *DestroyGLContext )( void *context );
	bool			( *IsGLContextCurrent )( void *context );
	bool			( *SwapGLWindow )( void );
	bool			( *SetGLSwapInterval )( int interval );
	bool			( *GetGLSwapInterval )( int *outInterval );
	bool			( *GetGLAttribute )( int attribute, int *outValue );	// renderGLAttribute_t
	void *			( *GetGLProcAddress )( const char *name );
	const char *	( *GetVideoErrorString )( void );

	// --- version 4: input bring-up/teardown, sequenced by the renderer's
	// init and vid_restart sequencing exactly like the static build ---
	void			( *InitInput )( void );
	void			( *ShutdownInput )( void );
	void			( *GrabMouseCursor )( bool grabIt );

	// --- version 6: Vulkan surface primitives, executed on the engine's
	// video instance (the module owns the VkInstance/device/swapchain but
	// never links the windowing library). Handles are opaque here so this
	// header stays Vulkan-free: vkInstance is the VkInstance handle,
	// outVkSurface receives a VkSurfaceKHR (64-bit) ---
	// copies up to maxNames required instance-extension name pointers
	// (static engine-lifetime strings) and reports the true count
	bool			( *GetVulkanInstanceExtensions )( const char **outNames, int maxNames, int *outCount );
	// creates a surface on the current game window; false when the window
	// was not created with RENDER_SURFACE_VULKAN or creation fails
	bool			( *CreateVulkanSurface )( void *vkInstance, unsigned long long *outVkSurface );

	// --- version 14: strict application has no closest-mode/desktop/placement
	// fallback. Query reads current SDL state without changing archived CVars.
	bool			( *QueryWindowState )( renderWindowState_t *outState );
	bool			( *ApplyScreenParmsStrict )( const renderWindowRequest_t *request,
								renderWindowState_t *outState, char *error, int errorSize );
	// A recoverable attempt pins the existing video subsystem so live display
	// identities survive context/window teardown, including failed attempts.
	bool			( *RetainVideoSystem )( void );
	void			( *ReleaseVideoSystem )( void );
} renderWindowServices_t;

// attribute selectors for renderWindowServices_t::GetGLAttribute; the
// profile-mask result is normalized to renderGLProfileValue_t
typedef enum {
	RENDER_GLATTR_CONTEXT_MAJOR_VERSION,
	RENDER_GLATTR_CONTEXT_MINOR_VERSION,
	RENDER_GLATTR_CONTEXT_PROFILE_MASK,
	RENDER_GLATTR_CONTEXT_FLAGS,
	RENDER_GLATTR_MULTISAMPLE_BUFFERS,
	RENDER_GLATTR_MULTISAMPLE_SAMPLES,
	RENDER_GLATTR_DEPTH_SIZE,
	RENDER_GLATTR_STENCIL_SIZE,
} renderGLAttribute_t;

typedef enum {
	RENDER_GLPROFILE_DEFAULT,
	RENDER_GLPROFILE_CORE,
	RENDER_GLPROFILE_COMPATIBILITY,
	RENDER_GLPROFILE_ES,
} renderGLProfileValue_t;

// engine-side accessor for the active platform backend's window services;
// returns NULL on backends that do not implement the seam (legacy win32,
// native Linux/macOS diagnostics paths, dedicated stubs)
const renderWindowServices_t *Sys_GetRenderWindowServices( void );

typedef struct renderImport_s {
	int										version;		// RENDER_API_VERSION
	const renderModuleServices_t *			services;
	renderModuleWindowInfo_t				window;

	// full engine interfaces for full renderer modules; a standalone bring-up
	// module treats these as opaque
	idSys *									sys;
	idCommon *								common;
	idCVarSystem *							cvarSystem;
	idCmdSystem *							cmdSystem;
	idFileSystem *							fileSystem;
	idDeclManager *							declManager;
	idSoundSystem *							soundSystem;

	// --- version 2: the remaining engine interfaces the renderer sources
	// bind as globals inside the module (game-DLL convention) ---
	idSession *								session;		// demo IO, pacifier, rw/sw
	idUserInterfaceManager *				uiManager;
	idCollisionModelManager *				collisionModelManager;
	idEventLoop *							eventLoop;		// frame-time queries
	rvBSEManager *							bse;			// effects system

	// --- version 3: engine window layer for the context-owning module ---
	const renderWindowServices_t *			windowServices;	// NULL on non-seam backends

	// --- version 4: console handle for vid_restart sequencing ---
	idConsole *								console;
} renderImport_t;


// diagnostics surface, valid even while the module cannot yet provide a full
// idRenderSystem; drives rendererVkProbe and module self-tests
typedef struct renderModuleDiagnostics_s {
	// prints a full bring-up/probe report through services->Printf;
	// returns false when any required step fails
	bool			( *RunProbe )( bool verbose );
	// quiet pass/fail for self-test harnesses; writes a one-line summary
	bool			( *RunDeviceSelfTest )( char *outSummary, int summaryLength );
} renderModuleDiagnostics_t;

struct renderDisplayPresentation_t;

typedef struct renderExport_s {
	int										version;		// RENDER_API_VERSION
	const char *							backendName;	// "gl" / "vulkan" -> r_actualRenderApi
	const char *							moduleDescription;
	// full renderer interface; NULL while the module is bring-up/diagnostics
	// only, which the loader treats as an instruction to fall back
	idRenderSystem *						renderSystem;
	// version 2: published alongside renderSystem when a module activates
	// (the engine forwards it into gameImport for the game DLLs)
	idRenderModelManager *					renderModelManager;
	const renderModuleDiagnostics_t *		diagnostics;
	// releases module resources; must be safe to call before Sys_DLL_Unload
	// whenever GetRenderAPI has been called
	void			( *Shutdown )( void );
	// Version 14: frame-boundary device work and actual backend presentation.
	// A failed restart may leave no device; the caller must explicitly restore.
	bool			( *TryDeviceRestart )( const renderWindowRequest_t *request, char *error, int errorSize );
	void			( *GetDisplayPresentation )( renderDisplayPresentation_t *outState );
} renderExport_t;

extern "C" {
typedef renderExport_t * ( *GetRenderAPI_t )( renderImport_t *import );
}

#endif /* !__RENDERMODULEAPI_H__ */
