// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan backend for the shared renderer front-end (Phase C,
	docs/dev/plans/2026-07-17-vulkan-phase-c.md).

	Phase C scope: the real game loop runs against the module's
	idRenderSystem (the shared front-end), and every frame ends in an
	animated clear presented through the swapchain. Draw-bearing commands
	are consumed and discarded; later phases replace the discard with the
	real Vulkan draw path. This TU also carries the backend globals and the
	entry points the front-end links against in place of the excluded GL
	backend TUs.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderModuleAPI.h"
#include "../GLStateCache.h"
#include "../GLDebugScope.h"
#include "../ShadowMapArb2Parity.h"
#include "../ModernGLShaderLibrary.h"
#include "../RendererUpload.h"
#include "../ModernGLExecutor.h"
#include "../ModernLightImageAtlas.h"
#include "../RenderGraphResources.h"
#include "../RendererMetrics.h"
#include "../MaterialResourceTable.h"
#include "../ClassicGuiDomain.h"
#include "../ClassicCinematicPostDomain.h"
#include "../ClassicSpecialFrameDomain.h"
#include "../ClassicWorldAmbientDomain.h"
#include "../ClassicInteractionDomain.h"
#include "../ClassicFogBlendDomain.h"
#include "../ClassicSubviewDomain.h"
#include "VulkanDevice.h"
#include "vk_Image.h"

// the back-end state object normally defined by tr_backend.cpp
backEndState_t	backEnd;

// defined in RenderSystem_init.cpp (shared front-end)
bool R_GetInitialWindowSize( bool fullScreen, int *width, int *height );

static const renderWindowServices_t *vkBackendServices = NULL;
static float vkClearColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
// The Vulkan backend does not otherwise build the GL render graph.  Keep one
// backend-owned packet frame for the shared classic-GUI domain when front-end
// capture was not requested for metrics.
static idScenePacketFrame vkClassicDomainPacketFrame;

static const classicSubviewDomainView_t *VK_ClassicSubview_Preflight(
		const viewDef_t *viewDef ) {
	if ( !r_rendererSharedSubview.GetBool() || viewDef == NULL
			|| !viewDef->isSubview ) {
		return NULL;
	}
	const classicSubviewDomainView_t *view =
		R_ClassicSubviewDomain_FindView( viewDef );
	if ( view == NULL ) {
		R_ClassicSubviewDomain_RecordBackendFallback( viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
			CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NOT_READY, 0 );
		return NULL;
	}
	if ( view->backendOutcome[CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
			!= CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
		// A rejected nested member returns its entire sealed transaction to the
		// established walker, including every later parent command.
		return NULL;
	}
	const bool nestedDynamicReady =
		R_ClassicCinematicPostDomain_SubviewTransactionReady( viewDef,
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN );
	if ( !view->ready || !R_ClassicSubviewDomain_ViewSemanticsMatch( *view )
			|| ( R_ClassicSubviewDomain_IsCaptureBacked( *view )
				&& ( view->captureImage == NULL || !view->captureImage->IsLoaded() ) )
			|| !R_ClassicSubviewDomain_ReadyForBackend( *view,
				CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ) ) {
		R_ClassicSubviewDomain_RecordBackendFallback( viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
			!view->ready ? view->failure
				: ( !R_ClassicSubviewDomain_ViewSemanticsMatch( *view )
					? CLASSIC_SUBVIEW_DOMAIN_FAILURE_VIEW_SEMANTICS_MISMATCH
					: ( !nestedDynamicReady
						? CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTED_CINEMATIC_POST_INCOMPLETE
						: ( !R_ClassicSubviewDomain_ReadyForBackend( *view,
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN )
						? CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTING_INCOMPLETE
						: CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_REJECTED ) ) ),
			!view->ready ? view->failureDetail : 1 );
		return NULL;
	}
	return view;
}

// vk_GuiExecutor.cpp
void VK_GuiExecutor_SetClearColor( const float color[ 4 ] );
void VK_GuiExecutor_Draw2DView( const viewDef_t *viewDef );
void VK_GuiExecutor_Draw3DView( const viewDef_t *viewDef );
void VK_GuiExecutor_PrepareSpecialEffects( const viewDef_t *viewDef );
bool VK_GuiExecutor_SpecialEffectsAwaitResolve(
		idRenderTexture *sourceRenderTexture );
void VK_GuiExecutor_DrawResolvedSpecialEffects(
		idRenderTexture *sourceRenderTexture,
		idRenderTexture *destinationRenderTexture );
bool VK_GuiExecutor_EnsureFrameOpen( void );
bool VK_GuiExecutor_EndFrameAndPresent( void );
bool VK_GuiExecutor_FrameIsOpen( void );
bool VK_Exec_SetRenderTarget( idRenderTexture *renderTexture );
void VK_Exec_ClearRenderTarget( bool clearColor, bool clearDepth, float depthValue,
		const float colorValue[ 4 ] );
bool VK_Exec_CopyRender( idImage *image, int x, int y, int width, int height,
		int cubeFace, bool copyDepth );
bool VK_Exec_ResolveRenderTargets( idRenderTexture *sourceRenderTexture,
		idRenderTexture *destinationRenderTexture, bool resolveDepth );
bool VK_GuiExecutor_ResolveTemporalPresentation(
		const resolveTemporalPresentationCommand_t &command );

static void VK_DrawSharedDirectSubview( const classicSubviewDomainView_t &view ) {
	// Direct SS_SUBVIEW mirrors compose into the parent target and therefore
	// have no capture command. Keep the mature 3D executor, but publish only
	// after its complete sealed child-view coverage has returned.
	VK_GuiExecutor_Draw3DView( view.viewDef );
	if ( view.backendOutcome[CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
			!= CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
		// A nested cinematic/post range rejected while the mature child-view
		// executor was active. Its rollback already covered this complete tree.
		return;
	}
	if ( !R_ClassicSubviewDomain_RecordDirectOwned( view.viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN ) ) {
		common->Warning( "Vulkan: shared direct subview coverage rejected after committed view" );
	}
}

/*
====================
VK_FillGLConfigFromDevice

The front-end keys nearly all behavior off glConfig; describe the Vulkan
device conservatively so CPU-side logic stays on portable paths.
====================
*/
static void VK_FillGLConfigFromDevice( void ) {
	static char vendorString[ 128 ];
	static char rendererString[ 256 ];
	static char versionString[ 128 ];

	idStr::snPrintf( vendorString, sizeof( vendorString ), "vendorID 0x%04x", vkCtx.deviceProperties.vendorID );
	idStr::Copynz( rendererString, vkCtx.deviceProperties.deviceName, sizeof( rendererString ) );
	idStr::snPrintf( versionString, sizeof( versionString ), "Vulkan %u.%u.%u",
			VK_API_VERSION_MAJOR( vkCtx.deviceProperties.apiVersion ),
			VK_API_VERSION_MINOR( vkCtx.deviceProperties.apiVersion ),
			VK_API_VERSION_PATCH( vkCtx.deviceProperties.apiVersion ) );

	glConfig.renderer_string = rendererString;
	glConfig.vendor_string = vendorString;
	glConfig.version_string = versionString;
	glConfig.extensions_string = "";
	glConfig.wgl_extensions_string = "";

	glConfig.maxTextureSize = (int)vkCtx.deviceProperties.limits.maxImageDimension2D;
	glConfig.maxTextureUnits = 8;
	glConfig.maxTextureCoords = 8;
	glConfig.maxTextureImageUnits = 16;

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;

	glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
	glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;

	glConfig.isFullscreen = r_fullscreen.GetBool();

	// the front-end's standard (ARB2-shaped) path is what the Vulkan backend
	// consumes; without this SetBackEndRenderer() finds no usable back end
	// and FatalErrors on the first BeginFrame after a config marks
	// r_renderer modified
	glConfig.allowARB2Path = true;

	// capabilities Vulkan carries unconditionally that shared front-end code
	// checks: NPOT (kills TG_POT_CORRECTION texgens), S3TC + BC7 (without
	// these R_BinaryImageHeaderSupportedByRenderer REJECTS generated
	// compressed .bimage files and lightgrid chunks — retail DDS loads
	// bypass the check, which masked this at menu scope), cube maps
	// (vk_Image allocates 6-layer CUBE_COMPATIBLE images since Phase D)
	glConfig.textureNonPowerOfTwoAvailable = true;
	// BC availability is reported from the device feature the logical device
	// actually enabled rather than asserted, so a portability implementation
	// without block compression degrades to uncompressed art instead of failing
	// every BC image allocation one warning at a time.
	glConfig.textureCompressionAvailable = vkCtx.textureCompressionBCSupported;
	glConfig.bptcTextureCompressionAvailable = vkCtx.textureCompressionBCSupported;
	glConfig.cubeMapAvailable = true;

	// imagetools keeps a private copy of these per linked binary and gates all
	// precompressed-DDS selection on it; without this the Vulkan backend would
	// report BC7 support in gfxInfo while every DDS replacement (and every
	// retail progimg/ block upload) was silently rejected and re-decoded
	R_PublishCompressionCapsToImageTools();
}

/*
====================
VK_InitRenderDevice

The Vulkan equivalent of R_InitOpenGL's window/context bring-up: create the
engine window for a Vulkan surface, bring up the device + swapchain, fill
glConfig, and hand input to the engine.
====================
*/
static bool VK_ApplyRequestedScreenParms( const renderWindowParms_t& parms ) {
	if ( !R_IsRecoverableRendererRestart() ) return vkBackendServices->ApplyScreenParms( &parms );
	const auto* request = R_GetRecoverableWindowRequest();
	renderWindowState_t observed; char error[512] = {};
	if ( !request || !vkBackendServices->ApplyScreenParmsStrict ||
			!vkBackendServices->ApplyScreenParmsStrict( request, &observed, error, sizeof(error) ) ) {
		common->Warning( "Vulkan: strict screen parameter application failed: %s", error );
		R_DisplayPresentationFailed( RDP_SCREEN_FAILED ); return false;
	}
	return true;
}

bool VK_InitRenderDevice( void ) {
	common->Printf( "----- VK_InitRenderDevice -----\n" );

	vkBackendServices = Sys_GetRenderWindowServices();
	if ( vkBackendServices == NULL ) {
		R_DisplayPresentationFailed( RDP_INIT_FAILED );
		common->Warning( "Vulkan: no window services on this platform backend" );
		return false;
	}
	if ( !vkBackendServices->PrepareWindowSystem() ) {
		R_DisplayPresentationFailed( RDP_INIT_FAILED );
		common->Warning( "Vulkan: window system preparation failed" );
		return false;
	}

	glimpParms_t parms;
	memset( &parms, 0, sizeof( parms ) );
	parms.fullScreen = r_fullscreen.GetBool() && !R_ForceWindowForRendererRestart();
	if ( !R_GetInitialWindowSize( parms.fullScreen, &parms.width, &parms.height ) ) {
		R_DisplayPresentationFailed( RDP_INIT_FAILED ); return false;
	}
	parms.borderless = !parms.fullScreen && r_borderless.GetBool();
	parms.displayHz = r_displayRefresh.GetInteger();
	parms.multiSamples = 0;
	parms.stereo = false;

	renderWindowParms_t windowParms;
	memset( &windowParms, 0, sizeof( windowParms ) );
	windowParms.width = parms.width;
	windowParms.height = parms.height;
	windowParms.fullScreen = parms.fullScreen;
	windowParms.borderless = parms.borderless;
	windowParms.hiddenWindow = r_hiddenWindow.GetBool();
	windowParms.displayHz = parms.displayHz;
	if ( R_IsRecoverableRendererRestart() ) {
		const auto* request = R_GetRecoverableWindowRequest();
		// Vulkan's current render targets are single-sample. A strict request
		// must not silently advertise unsupported multisampling as applied.
		if ( !request || request->parms.multiSamples != 0 || request->parms.stereo ) {
			common->Warning( "Vulkan: strict framebuffer request is unsupported" );
			R_DisplayPresentationFailed( RDP_INIT_FAILED ); return false;
		}
		windowParms = request->parms;
	}

	renderFramebufferDesc_t desc;
	memset( &desc, 0, sizeof( desc ) );
	desc.surfaceKind = RENDER_SURFACE_VULKAN;
	desc.redBits = 8;
	desc.greenBits = 8;
	desc.blueBits = 8;
	desc.alphaBits = 8;
	desc.depthBits = 24;
	desc.stencilBits = 8;
	desc.doubleBuffer = true;

	renderModuleWindowInfo_t windowInfo;
	memset( &windowInfo, 0, sizeof( windowInfo ) );
	bool reusedPreserved = false;
	if ( !vkBackendServices->CreateWindowForFramebuffer( &desc, &windowParms, &windowInfo, &reusedPreserved ) ) {
		R_DisplayPresentationFailed( RDP_INIT_FAILED );
		common->Warning( "Vulkan: window creation failed" );
		return false;
	}

	if ( !VK_Device_Init( vkBackendServices ) ) {
		vkBackendServices->DestroyAttemptWindow();
		return false;
	}

	if ( !VK_ApplyRequestedScreenParms( windowParms ) ) {
		R_DisplayPresentationFailed( RDP_SCREEN_FAILED );
		common->Warning( "Vulkan: initial screen parameter application failed" );
		// The surface still belongs to this window. Release every device /
		// surface resource before destroying the failed attempt's window.
		VK_Device_Shutdown();
		vkBackendServices->DestroyAttemptWindow();
		glConfig.isInitialized = false;
		return false;
	}
	vkBackendServices->RefreshNativeWindowHandles( &windowInfo );
	if ( windowInfo.pixelWidth > 0 && windowInfo.pixelHeight > 0
			&& ( (uint32_t)windowInfo.pixelWidth != vkCtx.swapchainExtent.width
				|| (uint32_t)windowInfo.pixelHeight != vkCtx.swapchainExtent.height ) ) {
		// fullscreen/mode application changed the drawable size
		if ( !VK_Device_RecreateSwapchain() ) {
			common->Warning( "Vulkan: initial screen resize swapchain recreation failed" );
			// Recreation may already have retired the original swapchain.
			// Do not publish a ready renderer with incomplete replacement data.
			VK_Device_Shutdown();
			vkBackendServices->DestroyAttemptWindow();
			glConfig.isInitialized = false;
			return false;
		}
	}

	VK_FillGLConfigFromDevice();
	glConfig.uiViewportX = windowInfo.uiViewportX;
	glConfig.uiViewportY = windowInfo.uiViewportY;
	glConfig.uiViewportWidth = windowInfo.uiViewportWidth;
	glConfig.uiViewportHeight = windowInfo.uiViewportHeight;
	glConfig.isInitialized = true;

	vkBackendServices->NotifyWindowReady();
	Sys_InitInput();

	common->Printf( "Vulkan renderer initialized: %s (%s)\n", glConfig.renderer_string, glConfig.version_string );
	return true;
}

/*
====================
VK_ShutdownRenderDevice
====================
*/
void VK_ShutdownRenderDevice( void ) {
	R_RendererMetrics_ShutdownGpuTimers();
	VK_Device_Shutdown();
	if ( vkBackendServices != NULL ) {
		vkBackendServices->BeginWindowTeardown();
		vkBackendServices->FinishWindowTeardown();
	}
	glConfig.isInitialized = false;
}

/*
====================
GLimp_* context entry points

The front-end keeps its existing calls; under the Vulkan backend they map
onto the device context (or are inert where GL semantics have no analog).
====================
*/
void GLimp_Shutdown( void ) {
	VK_ShutdownRenderDevice();
}

bool GLimp_SetScreenParms( glimpParms_t parms ) {
	if ( vkBackendServices == NULL ) {
		R_DisplayPresentationFailed( RDP_SCREEN_FAILED );
		return false;
	}
	renderWindowParms_t windowParms;
	memset( &windowParms, 0, sizeof( windowParms ) );
	windowParms.width = parms.width;
	windowParms.height = parms.height;
	windowParms.fullScreen = parms.fullScreen;
	windowParms.borderless = parms.borderless;
	windowParms.hiddenWindow = parms.hiddenWindow;
	windowParms.displayHz = parms.displayHz;
	if ( R_IsRecoverableRendererRestart() ) {
		const auto* request = R_GetRecoverableWindowRequest();
		if ( !request || request->parms.multiSamples != 0 || request->parms.stereo ) {
			R_DisplayPresentationFailed( RDP_SCREEN_FAILED ); return false;
		}
		windowParms = request->parms;
	}
	if ( !VK_ApplyRequestedScreenParms( windowParms ) ) {
		R_DisplayPresentationFailed( RDP_SCREEN_FAILED );
		return false;
	}
	if ( !VK_Device_RecreateSwapchain() ) {
		return false;
	}
	glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
	glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;
	glConfig.isFullscreen = windowParms.fullScreen;
	return true;
}

void GLimp_SwapBuffers( void ) {
	// live window-state poll, mirroring the GL seam
	if ( vkBackendServices != NULL && vkBackendServices->RefreshNativeWindowHandles != NULL ) {
		renderModuleWindowInfo_t info;
		memset( &info, 0, sizeof( info ) );
		vkBackendServices->RefreshNativeWindowHandles( &info );
		glConfig.uiViewportX = info.uiViewportX;
		glConfig.uiViewportY = info.uiViewportY;
		glConfig.uiViewportWidth = info.uiViewportWidth;
		glConfig.uiViewportHeight = info.uiViewportHeight;
		if ( info.pixelWidth > 0 && info.pixelHeight > 0
				&& ( (uint32_t)info.pixelWidth != vkCtx.swapchainExtent.width
					|| (uint32_t)info.pixelHeight != vkCtx.swapchainExtent.height ) ) {
			// only recreate between frames; an open frame presents into the
			// old swapchain and OUT_OF_DATE handling catches the rest
			if ( !VK_GuiExecutor_FrameIsOpen() ) {
				if ( VK_Device_RecreateSwapchain() ) {
					glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
					glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;
				} else return;
			}
		}
	}

	// present whatever the frame holds; a frame with no draws still clears
	VK_GuiExecutor_SetClearColor( vkClearColor );
	if ( VK_GuiExecutor_EnsureFrameOpen() ) {
		// Screenshot/capture readback consumes the completed swapchain image
		// before presentation.  The GL backend observes the same
		// tr.takingScreenshot contract in RB_SwapBuffers.
		if ( !tr.takingScreenshot ) {
			VK_GuiExecutor_EndFrameAndPresent();
		}
	}
}

void GLimp_ActivateContext( void ) {
}

void GLimp_DeactivateContext( void ) {
}

bool GLimp_EnsureActiveContext( const char *operation ) {
	(void)operation;
	return vkCtx.initialized;
}

void *GLimp_ExtensionPointer( const char *name ) {
	(void)name;
	return NULL;
}

void GLimp_SetGamma( unsigned short red[256], unsigned short green[256], unsigned short blue[256] ) {
	(void)red;
	(void)green;
	(void)blue;
}

bool GLimp_UseNativeGammaRamps( void ) {
	return false;
}

void GLimp_EnableLogging( bool enable ) {
	(void)enable;
}

void GLimp_PreserveWindowOnShutdown( void ) {
}

// legacy SMP entry points referenced by dormant paths
void GLimp_WakeBackEnd( void *data ) {
	(void)data;
}

void GLimp_FrontEndSleep( void ) {
}

void *GLimp_BackEndSleep( void ) {
	return NULL;
}

bool GLimp_SpawnRenderThread( void ( *function )( void ) ) {
	(void)function;
	return false;
}

/*
====================
RB_ExecuteBackEndCommands

Phase D command consumption: 2D views draw through the GUI executor; 3D
views arrive with Phase E; RC_SWAP_BUFFERS closes and presents the frame.
====================
*/
void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	R_ClassicGuiDomain_ResetFrame();
	R_ClassicCinematicPostDomain_ResetFrame();
	R_ClassicSpecialFrameDomain_ResetFrame();
	R_ClassicWorldAmbientDomain_ResetFrame();
	R_ClassicInteractionDomain_ResetFrame();
	R_ClassicFogBlendDomain_ResetFrame();
	R_ClassicSubviewDomain_ResetFrame();
	if ( ( r_rendererModernQuality.GetBool() && r_pbrMaterials.GetBool() )
			|| r_rendererSharedGui.GetBool()
			|| r_rendererSharedInWorldGui.GetBool()
			|| r_rendererSharedCinematicPost.GetBool()
			|| r_rendererSharedSpecialFrame.GetBool()
			|| r_rendererSharedWorldAmbient.GetBool()
			|| r_rendererSharedWorldInteraction.GetBool()
			|| r_rendererSharedWorldFogBlend.GetBool()
			|| r_rendererSharedSubview.GetBool() ) {
		const idScenePacketFrame *scenePackets = NULL;
		if ( R_ScenePackets_FrontEndFrameAvailable() ) {
			scenePackets = &R_ScenePackets_FrontEndFrame();
		} else {
			R_ScenePackets_BuildLegacyCommandStream( cmds, vkClassicDomainPacketFrame );
			scenePackets = &vkClassicDomainPacketFrame;
		}
		R_MaterialResourceTable_PrepareFrame( *scenePackets );
		if ( r_rendererSharedGui.GetBool()
				|| r_rendererSharedInWorldGui.GetBool() ) {
			R_ClassicGuiDomain_PrepareFrame( *scenePackets );
		}
		// Special-view topology must be sealed before cinematic/post ranges can
		// join one of its atomic root transactions.
		if ( r_rendererSharedSubview.GetBool() ) {
			R_ClassicSubviewDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedCinematicPost.GetBool() ) {
			R_ClassicCinematicPostDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedSpecialFrame.GetBool() ) {
			R_ClassicSpecialFrameDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldAmbient.GetBool() ) {
			R_ClassicWorldAmbientDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldInteraction.GetBool() ) {
			R_ClassicInteractionDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldFogBlend.GetBool() ) {
			R_ClassicFogBlendDomain_PrepareFrame( *scenePackets );
		}
	}

	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;
	backEnd.postProcessTexelSize = tr.postProcessTexelSize;
	backEnd.postProcessSourceColorSpace = tr.postProcessSourceColorSpace;
	backEnd.postProcessSMAAQuality = tr.postProcessSMAAQuality;
	const classicSubviewDomainView_t *pendingSharedSubview = NULL;

	for ( ; cmds != NULL; cmds = (const emptyCommand_t *)cmds->next ) {
		if ( pendingSharedSubview != NULL
				&& cmds->commandId != RC_COPY_RENDER ) {
			if ( pendingSharedSubview->backendOutcome[
					CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
					== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
				R_ClassicSubviewDomain_RecordBackendFallback(
					pendingSharedSubview->viewDef,
					CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
					CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH,
					static_cast<int>( cmds->commandId ) );
			}
			pendingSharedSubview = NULL;
		}
		switch ( cmds->commandId ) {
			case RC_NOP:
				break;
			case RC_SET_BUFFER: {
				const setBufferCommand_t *cmd = (const setBufferCommand_t *)cmds;
				backEnd.frameCount = cmd->frameCount;
				// clear-color policy mirrors the GL RB_SetBuffer; black
				// otherwise (the swapchain load op always clears)
				float c[ 3 ];
				if ( sscanf( r_clear.GetString(), "%f %f %f", &c[ 0 ], &c[ 1 ], &c[ 2 ] ) == 3 ) {
					vkClearColor[ 0 ] = c[ 0 ];
					vkClearColor[ 1 ] = c[ 1 ];
					vkClearColor[ 2 ] = c[ 2 ];
				} else if ( r_clear.GetInteger() == 1 ) {
					vkClearColor[ 0 ] = 0.4f;
					vkClearColor[ 1 ] = 0.0f;
					vkClearColor[ 2 ] = 0.25f;
				} else {
					vkClearColor[ 0 ] = vkClearColor[ 1 ] = vkClearColor[ 2 ] = 0.0f;
				}
				vkClearColor[ 3 ] = 1.0f;
				VK_GuiExecutor_SetClearColor( vkClearColor );
				break;
			}
			case RC_DRAW_VIEW: {
				const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)cmds;
				if ( cmd->viewDef != NULL ) {
					// RB_DrawView accounts this for OpenGL, and the Vulkan backend does
					// not route through it, so the renderer metrics would otherwise read
					// zero surfaces on this backend.
					backEnd.pc.c_surfaces += cmd->viewDef->numDrawSurfs;
					const classicSubviewDomainView_t *sharedSubview =
						VK_ClassicSubview_Preflight( cmd->viewDef );
					pendingSharedSubview = sharedSubview != NULL
						&& R_ClassicSubviewDomain_IsCaptureBacked( *sharedSubview )
						? sharedSubview : NULL;
					if ( cmd->viewDef->viewEntitys == NULL ) {
						// 2D view (GUI/console/cinematics)
						VK_GuiExecutor_Draw2DView( cmd->viewDef );
					} else if ( sharedSubview != NULL
							&& R_ClassicSubviewDomain_IsDirect( *sharedSubview ) ) {
						VK_DrawSharedDirectSubview( *sharedSubview );
					} else {
						// world view: depth prepass + ambient walks (Phase E)
						VK_GuiExecutor_Draw3DView( cmd->viewDef );
					}
				}
				break;
			}
			case RC_SET_RENDERTEXTURE: {
				const setRenderTargetCommand_t *cmd = (const setRenderTargetCommand_t *)cmds;
				if ( cmd->renderTexture != NULL ) {
					if ( cmd->renderTexture->MakeCurrent() ) {
						backEnd.renderTexture = cmd->renderTexture;
						backEnd.feedbackRenderTexture = cmd->feedbackRenderTexture;
					}
				} else if ( VK_Exec_SetRenderTarget( NULL ) ) {
					backEnd.renderTexture = NULL;
					backEnd.feedbackRenderTexture = NULL;
				}
				break;
			}
			case RC_DRAW_SPECIAL_EFFECTS: {
				const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)cmds;
				VK_GuiExecutor_PrepareSpecialEffects( cmd->viewDef );
				break;
			}
			case RC_CLEAR_RENDERTARGET: {
				const renderClearBufferCommand_t *cmd = (const renderClearBufferCommand_t *)cmds;
				VK_Exec_ClearRenderTarget( cmd->clearColor, cmd->clearDepth,
						cmd->clearDepthValue, cmd->clearColorValue.ToFloatPtr() );
				break;
			}
			case RC_RESOLVE_MSAA: {
				const resolveRenderTargetCommand_t *cmd = (const resolveRenderTargetCommand_t *)cmds;
				const bool specialEffectsAwaitResolve =
						VK_GuiExecutor_SpecialEffectsAwaitResolve(
								cmd->msaaRenderTexture );
				const bool resolved = VK_Exec_ResolveRenderTargets(
						cmd->msaaRenderTexture, cmd->destRenderTexture,
						cmd->resolveDepth || specialEffectsAwaitResolve );
				if ( resolved && specialEffectsAwaitResolve ) {
					VK_GuiExecutor_DrawResolvedSpecialEffects(
							cmd->msaaRenderTexture, cmd->destRenderTexture );
				}
				break;
			}
			case RC_RESOLVE_TEMPORAL_PRESENTATION: {
				resolveTemporalPresentationCommand_t executionCommand =
					*reinterpret_cast<const resolveTemporalPresentationCommand_t *>( cmds );
				// Save previews can enter their capture scope after the front end
				// queued this command. Never let that late flush read or advance
				// gameplay history.
				if ( tr.takingScreenshot ) {
					executionCommand.captureFrame = true;
					executionCommand.historyValid = false;
				}
				(void)VK_GuiExecutor_ResolveTemporalPresentation(
					executionCommand );
				break;
			}
			case RC_SET_POSTPROCESS_SOURCE_SIZE:
				backEnd.postProcessTexelSize =
						((const setPostProcessSourceSizeCommand_t *)cmds)->texelSize;
				break;
			case RC_SET_POSTPROCESS_SOURCE_COLOR_SPACE:
				backEnd.postProcessSourceColorSpace =
						((const setPostProcessSourceColorSpaceCommand_t *)cmds)->colorSpace;
				break;
			case RC_SET_POSTPROCESS_SMAA_QUALITY:
				backEnd.postProcessSMAAQuality =
						((const setPostProcessSMAAQualityCommand_t *)cmds)->quality;
				break;
			case RC_COPY_RENDER: {
				const copyRenderCommand_t *cmd = (const copyRenderCommand_t *)cmds;
				const bool sharedCaptureMatches = pendingSharedSubview != NULL
					&& pendingSharedSubview->backendOutcome[
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
						== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED
					&& !r_skipCopyTexture.GetBool()
					&& R_ClassicSubviewDomain_CaptureMatches( *pendingSharedSubview,
						cmd->image, cmd->x, cmd->y, cmd->imageWidth,
						cmd->imageHeight, cmd->cubeFace, cmd->copyDepth );
				bool copied = false;
				if ( !r_skipCopyTexture.GetBool() ) {
					if ( sharedCaptureMatches ) {
						copied = VK_Exec_CopyRender(
							pendingSharedSubview->captureImage,
							pendingSharedSubview->captureX,
							pendingSharedSubview->captureY,
							pendingSharedSubview->captureWidth,
							pendingSharedSubview->captureHeight,
							pendingSharedSubview->captureCubeFace,
							pendingSharedSubview->captureCopyDepth );
					} else {
						copied = VK_Exec_CopyRender( cmd->image, cmd->x, cmd->y,
							cmd->imageWidth, cmd->imageHeight, cmd->cubeFace, cmd->copyDepth );
					}
				}
				if ( pendingSharedSubview != NULL
						&& pendingSharedSubview->backendOutcome[
							CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
							== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
					if ( sharedCaptureMatches && copied ) {
						R_ClassicSubviewDomain_RecordOwned(
							pendingSharedSubview->viewDef,
							CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
							pendingSharedSubview->captureImage,
							pendingSharedSubview->captureX,
							pendingSharedSubview->captureY,
							pendingSharedSubview->captureWidth,
							pendingSharedSubview->captureHeight,
							pendingSharedSubview->captureCubeFace,
							pendingSharedSubview->captureCopyDepth );
					} else {
						R_ClassicSubviewDomain_RecordBackendFallback(
							pendingSharedSubview->viewDef,
							CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
							r_skipCopyTexture.GetBool() || !copied
								? CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_REJECTED
								: CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH,
							r_skipCopyTexture.GetBool() ? 2 : 3 );
					}
				}
				pendingSharedSubview = NULL;
				break;
			}
			case RC_SWAP_BUFFERS:
				// CaptureRenderToFile flushes a cropped save-preview frame with
				// tr.takingScreenshot set. Match RB_SwapBuffers: retain that
				// back-buffer work for readback so it can be replaced by the real
				// frame instead of presenting the crop by itself.
				if ( !tr.takingScreenshot ) {
					GLimp_SwapBuffers();
				}
				break;
			default:
				// Debug-only command families remain part of the later
				// long-tail parity phase.
				break;
		}
	}
	if ( pendingSharedSubview != NULL ) {
		if ( pendingSharedSubview->backendOutcome[
				CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN]
				== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
			R_ClassicSubviewDomain_RecordBackendFallback(
				pendingSharedSubview->viewDef,
				CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN,
				CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH, 4 );
		}
	}
	R_ClassicSpecialFrameDomain_FinalizeBackendFrame(
		CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN );
}


/*
===============================================================================
	Phase C link surface for the excluded GL-backend TUs.

	The shared front-end references these symbols on paths that stay cold
	under the clear-only Vulkan backend (image upload halves, GL state/debug
	caches, ARB2 shadow parity, the GL shader library, upload-ring stats).
	Each becomes a real Vulkan implementation in its roadmap phase (D:
	images/uploads, E/F: draw + shadow paths); until then they are inert.
===============================================================================
*/

// tr_backend/tr_main frame allocator anchor (the functions live in the
// shared tr_main.cpp; only the global's home was the excluded tr_backend)
frameData_t *frameData = NULL;

// idImage GPU half: real Vulkan implementation in vk_Image.cpp (Phase D)

// --- idRenderTexture (Vulkan dynamic-rendering attachment set) ---
idRenderTexture::idRenderTexture( idImage *colorImage, idImage *depthImage ) {
	if ( colorImage != NULL ) {
		colorImages.Append( colorImage );
	}
	this->depthImage = depthImage;
	deviceHandle = 0;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	cachedDepthHandle = 0;
	cachedDepthGeneration = 0;
}

idRenderTexture::~idRenderTexture() {
	ReleaseDeviceHandle();
}

bool idRenderTexture::Resize( int width, int height ) {
	if ( width <= 0 || height <= 0 || colorImages.Num() <= 0 ) {
		return false;
	}
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( colorImages[ i ] == NULL ) {
			return false;
		}
		colorImages[ i ]->Resize( width, height );
	}
	if ( depthImage != NULL ) {
		depthImage->Resize( width, height );
	}
	ReleaseDeviceHandle();
	return EnsureDeviceHandle();
}

bool idRenderTexture::EnsureDeviceHandle( void ) {
	if ( HasCurrentDeviceHandle() && !NeedsAttachmentRefresh() ) {
		return true;
	}
	ReleaseDeviceHandle();
	return InitRenderTexture();
}

bool idRenderTexture::MakeCurrent( void ) {
	if ( !EnsureDeviceHandle() ) {
		return false;
	}
	return VK_Exec_SetRenderTarget( this );
}

bool idRenderTexture::MakeCurrent( int cubeFace ) {
	if ( cubeFace == 0 ) {
		return MakeCurrent();
	}
	static bool warnedCubeTargets = false;
	if ( !warnedCubeTargets ) {
		warnedCubeTargets = true;
		common->Warning( "Vulkan: cubemap render-target faces are not yet supported" );
	}
	return false;
}

void idRenderTexture::BindNull( void ) {
	if ( VK_GuiExecutor_FrameIsOpen() ) {
		(void)VK_Exec_SetRenderTarget( NULL );
	}
}

unsigned int idRenderTexture::GetDeviceHandle( void ) {
	return EnsureDeviceHandle() ? deviceHandle : 0;
}

void idRenderTexture::SetDebugLabel( const char *label ) {
	debugLabel = label != NULL ? label : "";
	ApplyDebugLabel();
}

void idRenderTexture::AddRenderImage( idImage *image ) {
	if ( image != NULL ) {
		colorImages.Append( image );
		ReleaseDeviceHandle();
	}
}

bool idRenderTexture::InitRenderTexture( void ) {
	if ( !vkCtx.initialized || colorImages.Num() != 1 || colorImages[ 0 ] == NULL ) {
		knownIncomplete = true;
		incompleteGeneration = tr.glContextGeneration;
		return false;
	}

	vkImageEntry_t *colorEntry = VK_Image_GetEntry( colorImages[ 0 ]->GetDeviceHandle() );
	vkImageEntry_t *depthEntry = depthImage != NULL
			? VK_Image_GetEntry( depthImage->GetDeviceHandle() ) : NULL;
	if ( colorEntry == NULL || colorEntry->attachmentView == VK_NULL_HANDLE
			|| ( colorEntry->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) == 0
			|| ( depthImage != NULL && ( depthEntry == NULL
				|| depthEntry->attachmentView == VK_NULL_HANDLE
				|| ( depthEntry->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0
				|| depthEntry->width != colorEntry->width
				|| depthEntry->height != colorEntry->height
				|| depthEntry->samples != colorEntry->samples ) ) ) {
		knownIncomplete = true;
		incompleteGeneration = tr.glContextGeneration;
		return false;
	}

	// The public handle remains an opaque non-zero token. Vulkan attachment
	// objects live on idImage entries, so no framebuffer object is required.
	deviceHandle = colorImages[ 0 ]->GetDeviceHandle() + 1;
	deviceHandleGeneration = tr.glContextGeneration;
	knownIncomplete = false;
	incompleteGeneration = -1;
	CaptureAttachmentHandles();
	ApplyDebugLabel();
	return true;
}

bool idRenderTexture::HasCurrentDeviceHandle( void ) const {
	return deviceHandle != 0 && deviceHandleGeneration == tr.glContextGeneration;
}

void idRenderTexture::ReleaseDeviceHandle( void ) {
	deviceHandle = 0;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	cachedColorHandles.Clear();
	cachedColorGenerations.Clear();
	cachedDepthHandle = 0;
	cachedDepthGeneration = 0;
}

bool idRenderTexture::FailFramebuffer( unsigned int status, const char *operation ) {
	ReportFramebufferFailure( status, operation );
	knownIncomplete = true;
	incompleteGeneration = tr.glContextGeneration;
	ReleaseDeviceHandle();
	return false;
}

void idRenderTexture::ReportFramebufferFailure( unsigned int status, const char *operation ) const {
	common->Warning( "Vulkan render target '%s' failed during %s (status %u)",
			debugLabel.Length() > 0 ? debugLabel.c_str() : "<unnamed>",
			operation != NULL ? operation : "validation", status );
}

bool idRenderTexture::NeedsAttachmentRefresh( void ) const {
	if ( cachedColorHandles.Num() != colorImages.Num()
			|| cachedColorGenerations.Num() != colorImages.Num() ) {
		return true;
	}
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( colorImages[ i ] == NULL
				|| cachedColorHandles[ i ] != colorImages[ i ]->GetDeviceHandle()
				|| cachedColorGenerations[ i ] != colorImages[ i ]->GetStorageGeneration() ) {
			return true;
		}
	}
	const unsigned int depthHandle = depthImage != NULL ? depthImage->GetDeviceHandle() : 0;
	const uint64_t depthGeneration = depthImage != NULL ? depthImage->GetStorageGeneration() : 0;
	return cachedDepthHandle != depthHandle || cachedDepthGeneration != depthGeneration;
}

void idRenderTexture::CaptureAttachmentHandles( void ) {
	cachedColorHandles.SetNum( colorImages.Num() );
	cachedColorGenerations.SetNum( colorImages.Num() );
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		cachedColorHandles[ i ] = colorImages[ i ] != NULL
				? colorImages[ i ]->GetDeviceHandle() : 0;
		cachedColorGenerations[ i ] = colorImages[ i ] != NULL
				? colorImages[ i ]->GetStorageGeneration() : 0;
	}
	cachedDepthHandle = depthImage != NULL ? depthImage->GetDeviceHandle() : 0;
	cachedDepthGeneration = depthImage != NULL ? depthImage->GetStorageGeneration() : 0;
}

void idRenderTexture::ApplyDebugLabel( void ) const {
	// VK_EXT_debug_utils object labels can be added once image ownership
	// moves behind a dedicated render-target registry. The label is retained
	// now so diagnostics and future labeling preserve the public contract.
}

// --- tr_backend helpers ---
void RB_LogComment( const char *comment, ... ) {
	(void)comment;
}

void GL_SelectTextureNoClient( int unit ) {
	(void)unit;
}

void GL_ClearStateDelta( void ) {
}

// faithful port of the excluded tr_render.cpp implementation: the Phase F1
// interaction pass bakes light-stage texture matrices with it
void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) {
	matrix[0] = shaderRegisters[ texture->matrix[0][0] ];
	matrix[4] = shaderRegisters[ texture->matrix[0][1] ];
	matrix[8] = 0;
	matrix[12] = shaderRegisters[ texture->matrix[0][2] ];

	// we attempt to keep scrolls from generating incredibly large texture
	// values, but center rotations and center scales can still generate
	// offsets that need to be > 1
	if ( matrix[12] < -40 || matrix[12] > 40 ) {
		matrix[12] -= (int)matrix[12];
	}

	matrix[1] = shaderRegisters[ texture->matrix[1][0] ];
	matrix[5] = shaderRegisters[ texture->matrix[1][1] ];
	matrix[9] = 0;
	matrix[13] = shaderRegisters[ texture->matrix[1][2] ];
	if ( matrix[13] < -40 || matrix[13] > 40 ) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[2] = 0;
	matrix[6] = 0;
	matrix[10] = 1;
	matrix[14] = 0;

	matrix[3] = 0;
	matrix[7] = 0;
	matrix[11] = 0;
	matrix[15] = 1;
}

// --- draw_arb2 surface ---
static const int VK_MAX_MATERIAL_PROGRAMS = 256;
typedef struct vkMaterialProgramRecord_s {
	unsigned int	target;
	char			name[ MAX_OSPATH ];
	vkMaterialProgramFamily_t family;
	bool			supported;
} vkMaterialProgramRecord_t;
static vkMaterialProgramRecord_t vkMaterialPrograms[ VK_MAX_MATERIAL_PROGRAMS ];
static int vkNumMaterialPrograms = 0;

static const char *VK_MaterialProgramBaseName( const char *name ) {
	if ( name == NULL ) {
		return "";
	}
	const char *base = name;
	for ( const char *cursor = name; *cursor != '\0'; cursor++ ) {
		if ( *cursor == '/' || *cursor == '\\' ) {
			base = cursor + 1;
		}
	}
	return base;
}

vkGLSLProgramFamily_t R_GetGLSLProgramFamily( const char *program ) {
	idStr base = VK_MaterialProgramBaseName( program );
	idStr extension;
	base.ExtractFileExtension( extension );
	base.StripFileExtension();

	// openQ4's live depth-aware postprocess uses the split-source token
	// `blur.fs`; all retail Quake 4 families use canonical `.glsl` tokens.
	// Reject other extensions before comparing basenames so an unrelated
	// split shader cannot silently inherit a stock-family ABI.
	if ( !base.Icmp( "Blur" ) && !extension.Icmp( "fs" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR;
	}
	if ( !extension.Icmp( "fs" ) ) {
		if ( !base.Icmp( "smaa_edge" ) ) {
			return VK_GLSL_PROGRAM_FAMILY_SMAA_EDGE;
		}
		if ( !base.Icmp( "smaa_weights" ) ) {
			return VK_GLSL_PROGRAM_FAMILY_SMAA_WEIGHTS;
		}
		if ( !base.Icmp( "smaa_blend" ) ) {
			return VK_GLSL_PROGRAM_FAMILY_SMAA_BLEND;
		}
	}
	if ( extension.Icmp( "glsl" ) != 0 ) {
		return VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
	}

	if ( !base.Icmp( "Displacement" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT;
	}
	if ( !base.Icmp( "DisplacementTwoStage" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE;
	}
	if ( !base.Icmp( "GhostPulling" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING;
	}
	if ( !base.Icmp( "Displacement2" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2;
	}
	if ( !base.Icmp( "MultiplyBlend" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_MULTIPLY_BLEND;
	}
	if ( !base.Icmp( "DisplacementCube" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE;
	}
	if ( !base.Icmp( "SniperStretch2" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_SNIPER_STRETCH2;
	}
	if ( !base.Icmp( "DepthTexture" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE;
	}
	if ( !base.Icmp( "Blur" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_BLUR;
	}
	if ( !base.Icmp( "MedLabs" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_MEDLABS;
	}
	if ( !base.Icmp( "DepthTexture2" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE2;
	}
	if ( !base.Icmp( "AL" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_AL;
	}
	if ( !base.Icmp( "Parallaxbump" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP;
	}
	if ( !base.Icmp( "Customlit" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT;
	}
	if ( !base.Icmp( "Water" ) ) {
		return VK_GLSL_PROGRAM_FAMILY_WATER;
	}
	return VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
}

static vkMaterialProgramFamily_t VK_MaterialProgramCanonicalFamily( const char *name ) {
	const char *base = VK_MaterialProgramBaseName( name );
	if ( idStr::Icmp( base, "bumpyEnvironment" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_BUMPY_ENVIRONMENT;
	}
	if ( idStr::Icmp( base, "heatHaze" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE;
	}
	if ( idStr::Icmp( base, "heatHazeWithMask" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK;
	}
	if ( idStr::Icmp( base, "heatHazeGrayWithMask" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_GRAY_WITH_MASK;
	}
	if ( idStr::Icmp( base, "heatHazeWithMaskAndVertex" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX;
	}
	if ( idStr::Icmp( base, "monochrome" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_MONOCHROME;
	}
	if ( idStr::Icmp( base, "arbFP1_Glass" ) == 0
			|| idStr::Icmp( base, "nvFP20_Glass" ) == 0 ) {
		return VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS;
	}
	return VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN;
}

static bool VK_MaterialProgramHasNativeImplementation( unsigned int target, const char *name ) {
	if ( target != GL_VERTEX_PROGRAM_ARB
			&& target != GL_FRAGMENT_PROGRAM_ARB ) {
		return false;
	}
	switch ( VK_MaterialProgramCanonicalFamily( name ) ) {
		case VK_MATERIAL_PROGRAM_FAMILY_BUMPY_ENVIRONMENT:
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE:
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK:
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_GRAY_WITH_MASK:
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX:
		case VK_MATERIAL_PROGRAM_FAMILY_MONOCHROME:
		case VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS:
			return true;
		default:
			return false;
	}
}

int R_FindARBProgram( unsigned int target, const char *program ) {
	if ( program == NULL || program[ 0 ] == '\0' ) {
		return 0;
	}
	idStr normalized = program;
	normalized.StripFileExtension();
	for ( int i = 0; i < vkNumMaterialPrograms; i++ ) {
		if ( vkMaterialPrograms[ i ].target == target
				&& idStr::Icmp( vkMaterialPrograms[ i ].name, normalized.c_str() ) == 0 ) {
			return i + 1;
		}
	}
	if ( vkNumMaterialPrograms >= VK_MAX_MATERIAL_PROGRAMS ) {
		common->Error( "Vulkan R_FindARBProgram: VK_MAX_MATERIAL_PROGRAMS" );
		return 0;
	}
	vkMaterialProgramRecord_t &record = vkMaterialPrograms[ vkNumMaterialPrograms ];
	memset( &record, 0, sizeof( record ) );
	record.target = target;
	idStr::Copynz( record.name, normalized.c_str(), sizeof( record.name ) );
	record.family = VK_MaterialProgramCanonicalFamily( record.name );
	record.supported = VK_MaterialProgramHasNativeImplementation( target, record.name );
	vkNumMaterialPrograms++;
	// A stable nonzero identity preserves newShaderStage_t in the material
	// parser even when the native implementation is still pending.
	return vkNumMaterialPrograms;
}

bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf ) {
	(void)surf;
	return false;
}

bool RB_ShadowMapBuildArb2ParityState( const viewLight_t *vLight, const viewDef_t *viewDef, int shadowMapSize, shadowMapArb2ParityState_t &state ) {
	(void)vLight; (void)viewDef; (void)shadowMapSize;
	memset( &state, 0, sizeof( state ) );
	return false;
}

bool RB_ShadowMapEstimateArb2CacheOwnership( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate ) {
	(void)vLight; (void)viewDef;
	memset( &estimate, 0, sizeof( estimate ) );
	return false;
}

bool RB_ShadowMapProjectedAtlasSlotForLight( const viewLight_t *vLight,
		const viewDef_t *viewDef, shadowMapArb2AtlasSlot_t &slot ) {
	(void)vLight; (void)viewDef;
	memset( &slot, 0, sizeof( slot ) );
	return false;
}

bool RB_ShadowMapProjectedAtlasSlotMarkUsed( int lightDefIndex,
		int signature, std::uint64_t storageGeneration,
		int cellX, int cellY, int cellSpan ) {
	(void)lightDefIndex; (void)signature; (void)cellX; (void)cellY;
	(void)storageGeneration; (void)cellSpan;
	return false;
}

bool RB_ShadowMapPointCubeForLight( const viewLight_t *vLight,
		const viewDef_t *viewDef, shadowMapArb2PointCube_t &cube ) {
	(void)vLight; (void)viewDef;
	memset( &cube, 0, sizeof( cube ) );
	return false;
}

void RB_ShadowMapPointCubeMarkUsed( int lightDefIndex ) {
	(void)lightDefIndex;
}

bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {
	// no ARB2 receiver path exists under the Vulkan backend yet
	return true;
}

// --- GL debug scope/labels (KHR_debug; no Vulkan analog wired yet) ---
idGLDebugScope::idGLDebugScope( const char *name, unsigned int id ) {
	(void)name; (void)id;
}

idGLDebugScope::~idGLDebugScope() {
}

void R_GLDebug_LabelBuffer( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelTexture( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelProgram( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelVertexArray( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

// --- GL state cache (cold under the clear backend) ---
idGLStateCache::idGLStateCache() {
}

bool idGLStateCache::UseProgram( GLuint program ) { (void)program; return false; }
bool idGLStateCache::BindVertexArray( GLuint vertexArray ) { (void)vertexArray; return false; }
bool idGLStateCache::BindBuffer( GLenum target, GLuint buffer ) { (void)target; (void)buffer; return false; }
bool idGLStateCache::BindBufferBase( GLenum target, GLuint index, GLuint buffer ) { (void)target; (void)index; (void)buffer; return false; }
bool idGLStateCache::BindBuffersBase( GLenum target, GLuint first, GLsizei count, const GLuint *buffers ) { (void)target; (void)first; (void)count; (void)buffers; return false; }
bool idGLStateCache::ActiveTextureUnit( int unit ) { (void)unit; return false; }
bool idGLStateCache::BindTexture( int unit, GLenum target, GLuint texture ) { (void)unit; (void)target; (void)texture; return false; }
bool idGLStateCache::BindFramebuffer( GLenum target, GLuint framebuffer ) { (void)target; (void)framebuffer; return false; }
bool idGLStateCache::SetBlendEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetDepthTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetDepthMask( GLboolean mask ) { (void)mask; return false; }
bool idGLStateCache::SetStencilTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetScissorTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetCullFaceEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetViewport( GLint x, GLint y, GLsizei width, GLsizei height ) { (void)x; (void)y; (void)width; (void)height; return false; }
bool idGLStateCache::SetScissor( GLint x, GLint y, GLsizei width, GLsizei height ) { (void)x; (void)y; (void)width; (void)height; return false; }
bool idGLStateCache::SetColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha ) { (void)red; (void)green; (void)blue; (void)alpha; return false; }

idGLStateCache &R_GLStateCache( void ) {
	static idGLStateCache cache;
	return cache;
}

void R_GLStateCache_InvalidateAll( const char *reason ) {
	(void)reason;
}

// --- GL shader library (Vulkan pipeline library replaces it in Phase E) ---
const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void ) {
	static modernGLShaderLibraryStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}

const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion ) {
	(void)kind; (void)preferredGLSLVersion;
	return NULL;
}

// --- upload ring stats (Vulkan staging ring replaces it in Phase D) ---
bool R_RendererUpload_QueryStorage(rendererUploadStorage_t&) { return false; }

const rendererUploadStats_t &R_RendererUpload_Stats( void ) {
	static rendererUploadStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}


/*
===============================================================================
	Residual link surface (complete enumeration, link cycle 6).

	R_InitOpenGL and the GL vid-restart flow stay compiled but unreachable
	under the Vulkan seam; their callees plus the per-frame upload hooks,
	GL self-test commands, and debug-tool entry points resolve here.
===============================================================================
*/

// GL bring-up flow (unreachable: the InitOpenGL seam routes to
// VK_InitRenderDevice)
bool GLimp_Init( glimpParms_t parms ) {
	(void)parms;
	common->Warning( "GLimp_Init reached under the Vulkan backend" );
	return false;
}

void R_ARB2_Init( void ) {
}

void RB_ResetARB2InteractionHandoffBreadcrumb( void ) {
}

void RB_ResetAppleGL21RouteCounters( void ) {
}

void RB_ReportAppleGL21RouteCounters( void ) {
}

void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	(void)args;
	common->Printf( "reloadARBprograms: not applicable under the Vulkan backend\n" );
}

void R_ReportShaderPrograms_f( const idCmdArgs &args ) {
	(void)args;
	common->Printf( "Vulkan material programs: %d registered\n", vkNumMaterialPrograms );
	for ( int i = 0; i < vkNumMaterialPrograms; i++ ) {
		const vkMaterialProgramRecord_t &record = vkMaterialPrograms[ i ];
		common->Printf( "  %3d  %s  %s  %s\n", i + 1,
				record.target == GL_VERTEX_PROGRAM_ARB ? "vertex  " : "fragment",
				record.supported ? "native     " : "unsupported",
				record.name );
	}
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	if ( stage == NULL || !stage->glslProgram ) {
		return false;
	}

	const vkGLSLProgramFamily_t family =
			R_GetGLSLProgramFamily( stage->glslProgramName );
	const bool supported = family > VK_GLSL_PROGRAM_FAMILY_UNKNOWN
			&& family < VK_GLSL_PROGRAM_FAMILY_COUNT;

	stage->glslProgramLoaded = true;
	stage->glslProgramValid = supported;
	stage->glslProgramGeneration = tr.glContextGeneration;
	// Vulkan pipelines are selected by the stable family enum. Keep all GL
	// object handles zero so material teardown cannot mistake native Vulkan
	// identities for live OpenGL resources.
	stage->glslProgramObject = 0;
	stage->glslVertexShaderObject = 0;
	stage->glslFragmentShaderObject = 0;
	return supported;
}

idImage *RB_ResolveGLSLShaderTextureImage( const newShaderStage_t *stage,
		int slot, const drawInteraction_t *din ) {
	if ( stage == NULL || slot < 0 || slot >= stage->numShaderTextures ) {
		return NULL;
	}

	switch ( stage->shaderTextureBindings[ slot ] ) {
		case GLSL_SHADERTEXTURE_LIGHT_FALLOFF:
			return din != NULL && din->lightFalloffImage != NULL
					? din->lightFalloffImage : globalImages->whiteImage;
		case GLSL_SHADERTEXTURE_LIGHT_IMAGE:
			return din != NULL && din->lightImage != NULL
					? din->lightImage : globalImages->whiteImage;
		case GLSL_SHADERTEXTURE_AMBIENT_NORMAL_MAP:
			return globalImages->ambientNormalMap != NULL
					? globalImages->ambientNormalMap : globalImages->defaultImage;
		case GLSL_SHADERTEXTURE_NORMAL_CUBE_MAP:
			return globalImages->normalCubeMapImage != NULL
					? globalImages->normalCubeMapImage : globalImages->defaultImage;
		case GLSL_SHADERTEXTURE_SPECULAR_TABLE:
			return globalImages->specularTableImage != NULL
					? globalImages->specularTableImage : globalImages->defaultImage;
		case GLSL_SHADERTEXTURE_IMAGE:
		default:
			return stage->shaderTextureImages[ slot ];
	}
}

bool R_IsARBProgramValid( unsigned int target, unsigned int handle ) {
	if ( handle == 0 || handle > (unsigned int)vkNumMaterialPrograms ) {
		return false;
	}
	const vkMaterialProgramRecord_t &record = vkMaterialPrograms[ handle - 1 ];
	return record.target == target && record.supported;
}

vkMaterialProgramFamily_t R_GetARBProgramFamily( unsigned int target, unsigned int handle ) {
	if ( handle == 0 || handle > (unsigned int)vkNumMaterialPrograms ) {
		return VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN;
	}
	const vkMaterialProgramRecord_t &record = vkMaterialPrograms[ handle - 1 ];
	if ( record.target != target ) {
		return VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN;
	}
	return record.family;
}

// GL state wrappers (cold)
void GL_State( int stateBits ) {
	(void)stateBits;
}

void GL_Cull( int cullType ) {
	(void)cullType;
}

void GL_SelectTexture( int unit ) {
	(void)unit;
}

void GL_TexEnv( int env ) {
	(void)env;
}

// KHR_debug output layer
bool R_GLDebugOutput_Available( void ) {
	return false;
}

bool R_GLDebugOutput_Registered( void ) {
	return false;
}

void R_GLDebugOutput_Init( void ) {
}

void R_GLDebugOutput_Shutdown( void ) {
}

void R_GLDebugOutput_FlushMessages( void ) {
}

// The shared clustered-lighting TU acquires per-light atlas cells; the atlas
// itself is a GL-backend TU, so the Vulkan module reports no residency.
modernLightAtlasReject_t R_ModernLightImageAtlas_Acquire( const idImage *image, float rect[4] ) {
	(void)image;
	if ( rect != NULL ) {
		rect[0] = rect[1] = rect[2] = rect[3] = 0.0f;
	}
	return MODERN_LIGHT_ATLAS_REJECT_UNAVAILABLE;
}

// modern GL executor (the Vulkan executor replaces it from Phase E)
void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	(void)caps; (void)features;
}

void R_ModernGLExecutor_Shutdown( void ) {
}

void R_ModernGLExecutor_InvalidatePlans( void ) {
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
}

bool R_ModernGLExecutor_ModernVisibleRequestedForPost( void ) {
	return false;
}

const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void ) {
	static modernGLExecutorStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}

bool R_ModernGLShaderLibrary_Reload( void ) {
	return false;
}

// render-graph transient resources (Vulkan graph resources from Phase E)
void R_RenderGraphResources_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	(void)caps; (void)features;
}

void R_RenderGraphResources_Shutdown( void ) {
}

void R_RenderGraphResources_PrintGfxInfo( void ) {
}

void R_RenderGraphResources_DumpLatest( void ) {
}

// upload ring (Vulkan staging ring from Phase D)
void R_RendererUpload_Init( const renderBackendCaps_t &caps ) {
	(void)caps;
}

void R_RendererUpload_Shutdown( void ) {
}

void R_RendererUpload_BeginFrame( int frameCount ) {
	(void)frameCount;
}

void R_RendererUpload_EndFrame( void ) {
}

bool R_RendererUpload_AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation ) {
	(void)data; (void)bytes; (void)alignment;
	memset( &allocation, 0, sizeof( allocation ) );
	return false;
}

bool R_RendererUpload_AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool dynamic, unsigned int &handle ) {
	(void)data; (void)bytes; (void)indexBuffer; (void)dynamic;
	handle = 0;
	return false;
}

void R_RendererUpload_FreeStaticBuffer( unsigned int &handle, int bytes, bool indexBuffer ) {
	(void)bytes; (void)indexBuffer;
	handle = 0;
}

bool R_RendererUpload_DynamicFrameBridgeAvailable( void ) {
	return false;
}

int R_RendererUpload_FrameCapacity( void ) {
	return 0;
}

void R_RendererUpload_RecordLegacyStall( void ) {
}

void R_RendererUpload_RecordLegacyUpload( int bytes ) {
	(void)bytes;
}

// vk_ShadowMap.cpp narrow accessor (the shadow module state stays file-static there)
bool VK_ShadowMap_ResourcesKnownGood( bool pointLight );

// Phase F3 hook for the front-end stencil-volume elision gate
// (R_ShadowMapLightWillUseShadowMaps, tr_light.cpp). Vulkan reports the same
// per-light-class generation truth OpenGL does; a later per-view admission
// miss degrades to one unshadowed frame plus a sticky volume restore rather
// than to a dropped receiver.
bool RB_ShadowMapResourcesKnownGood( bool pointLight ) {
	return VK_ShadowMap_ResourcesKnownGood( pointLight );
}

// shadow-map / post-process resource teardown hooks

void RB_ShutdownShadowMapResources( void ) {
}

void RB_ShutdownScenePostProcess( void ) {
}

// buffered debug-tool surface (Phase I implements as buffered-line draws)
void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
	(void)color; (void)start; (void)end; (void)lifeTime; (void)depthTest;
}

void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	(void)color; (void)winding; (void)lifeTime; (void)depthTest;
}

void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, bool depthTest ) {
	(void)text; (void)origin; (void)scale; (void)color; (void)viewAxis; (void)align; (void)lifetime; (void)depthTest;
}

void RB_ClearDebugLines( int time ) {
	(void)time;
}

void RB_ClearDebugPolygons( int time ) {
	(void)time;
}

void RB_ClearDebugText( int time ) {
	(void)time;
}

void RB_DrawBounds( const idBounds &bounds ) {
	(void)bounds;
}

float RB_DrawTextLength( const char *text, float scale, int len ) {
	(void)text; (void)scale; (void)len;
	return 0.0f;
}

void RB_DrawElementsImmediate( const srfTriangles_t *tri ) {
	(void)tri;
}

void RB_ShowImages( void ) {
}

void RB_ShutdownDebugTools( void ) {
}

// GL-subsystem self-test commands: honest skips under the Vulkan backend
#define VK_GL_SELFTEST_STUB( name ) \
	bool name( void ) { \
		common->Printf( #name ": skipped (GL subsystem; not applicable under the Vulkan backend)\n" ); \
		return true; \
	}

VK_GL_SELFTEST_STUB( RendererDeferredResolve_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererForwardPlus_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGBuffer_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererPBRVisible_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGLStateCache_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGpuDriven_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererLightImageAtlas_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererLowOverhead_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernCompatibility_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernGLExecutor_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernGLShaderLibrary_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernVisibility_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernVisible_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererPassOwnership_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererRenderGraphResource_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererUpload_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererVisiblePath_RunSelfTest )

#endif /* OPENQ4_RENDERER_VK_MODULE */


