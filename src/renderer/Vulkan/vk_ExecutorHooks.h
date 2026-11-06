#ifndef __VK_EXECUTOR_HOOKS_H__
#define __VK_EXECUTOR_HOOKS_H__

// Opt-in executor-only operation counts and current bounded descriptor usage.
void VK_Exec_PrintResourceInfo( bool reset = false );

/*
===============================================================================

	Narrow executor hooks for the passes that live outside vk_GuiExecutor.cpp:
	the full-screen post passes (vk_PostProcess.cpp), the scene overlays
	(vk_SceneEffects.cpp) and the debug tools (vk_DebugTools.cpp). vkExec
	stays file-static in vk_GuiExecutor.cpp; these are the only ways in.

	Every extra pass shares the interaction pipeline layout: single-sampler
	sets 0-5, the dynamic uniform slice on set 6 (at most 256 bytes) and the
	128-byte push range for both stages.

===============================================================================
*/

// vertex layouts for VK_Exec_ExtraPipeline
enum vkExtraVertexLayout_t {
	VK_EXTRA_VERTEX_NONE = 0,		// the vertex shader makes its own vertices
	VK_EXTRA_VERTEX_POSITION,		// idDrawVert xyz only
	VK_EXTRA_VERTEX_DRAWVERT,		// the interaction layout: xyz 0, colour 1, normal 2, tangents 3-4, st 5
	VK_EXTRA_VERTEX_DEBUG,			// vkDebugVert_t: xyzw 0, RGBA8 colour 1, st 2 (28-byte stride)
	VK_EXTRA_VERTEX_GUI,			// gui.vert's idDrawVert xyz 0, colour 1 and st 2
	VK_EXTRA_VERTEX_POSITION_PREVIOUS	// idDrawVert xyz 0 (binding 0) + idVec3 previous position 1 (binding 1)
};

// VK_Exec_ExtraPipeline flags
static const int VK_EXTRA_PIPELINE_LINES = 1 << 0;			// line-list topology
static const int VK_EXTRA_PIPELINE_COLOR_OFF = 1 << 1;		// every colour channel masked
static const int VK_EXTRA_PIPELINE_ALPHA_TO_COVERAGE = 1 << 2;
static const int VK_EXTRA_PIPELINE_POINTS = 1 << 3;			// point-list topology

// VK_Exec_ExtraPipeline kind ranges: vk_PostProcess.cpp 1-31,
// vk_SceneEffects.cpp 32-63, vk_DebugTools.cpp 64-95, vk_GuiExecutor.cpp 96+
static const int VK_EXTRA_KIND_SCENE_BASE = 32;
static const int VK_EXTRA_KIND_DEBUG_BASE = 64;
static const int VK_EXTRA_KIND_EXECUTOR_BASE = 96;

// OpenXR (vk_VRPresentation.cpp): an eye or the virtual screen stands in for
// the window while a VR frame renders into it, so everything that draws to,
// copies from or reads back the back buffer uses its images instead. The
// caller owns them and keeps them in COLOR_ATTACHMENT_OPTIMAL and
// DEPTH_STENCIL_ATTACHMENT_OPTIMAL while they are the output; depth has
// vkCtx.depthFormat, like the window's. NULL returns to the swapchain image.
// The frame must be open; the default target is bound afterwards.
static const int VK_EXEC_VR_TARGETS = 3;	// virtual screen, left eye, right eye
typedef struct vkExecDefaultOutput_s {
	VkImage				color;
	VkImageView			colorView;
	VkFormat			colorFormat;
	VkImage				depth;
	VkImageView			depthView;
	VkExtent2D			extent;
	bool				premultipliedAlpha;	// composited as a premultiplied-alpha layer
	int					vrTarget;			// 0 virtual screen, 1/2 eyes (< VK_EXEC_VR_TARGETS)
} vkExecDefaultOutput_t;
bool				VK_Exec_SetDefaultOutput( const vkExecDefaultOutput_t *output );
// r_brightness/r_gamma on the default output, once per frame for each VR
// target; the window's mapping at present is then skipped for its mirror
bool				VK_Exec_MapDefaultOutputColor( void );
void				VK_Exec_MarkWindowColorMapped( void );
// the frame's acquired swapchain image (the VR mirror draws into it)
VkImage				VK_Exec_SwapchainImage( void );
// Ends the rendering scope so transfers and barriers can be recorded, and
// returns the frame's command buffer (VK_NULL_HANDLE without an open frame).
// The next target bind or clear resumes rendering.
VkCommandBuffer		VK_Exec_SuspendRendering( void );
// vk_VRPresentation.cpp: a VR frame renders into its own targets, so it may
// open without the window's image (minimized, occluded, out of date);
// *forced reports the r_vkVRWindowlessTest drill
bool				VK_VR_FrameMayOpenWithoutWindow( bool *forced );
// ...and the virtual screen stands in for the window such a frame lacks
bool				VK_VR_BindWindowStandIn( void );

VkCommandBuffer		VK_Exec_ActiveCmd( void );
int					VK_Exec_ActiveFrameSlot( void );
bool				VK_GuiExecutor_FrameIsOpen( void );
bool				VK_GuiExecutor_BeginFrame( void );
bool				VK_GuiExecutor_EndFrameAndPresent( void );
bool				VK_Exec_MainRenderingScopeOpen( void );
bool				VK_Exec_ActiveTargetHasStencil( void );
bool				VK_Exec_ActiveTargetMultisampled( void );
int					VK_Exec_ActiveFramebufferWidth( void );
int					VK_Exec_ActiveFramebufferHeight( void );
idRenderTexture *	VK_Exec_ActiveRenderTexture( void );
bool VK_Exec_ActiveLowerOrigin();
void VK_Exec_MarkColorOrigin( bool flipY );
void VK_Exec_MarkCanonicalWrites();
VkFrontFace VK_Exec_CanonicalFrontFace();
int					VK_Exec_ActiveCubeFace( void );
// samplePreviousDepth=false is for color-only intermediate passes which do not
// sample the previous depth attachment; captures still manage their own layouts.
bool				VK_Exec_SetRenderTarget( idRenderTexture *renderTexture, int cubeFace = 0, bool samplePreviousDepth = true );
void				VK_Exec_ClearRenderTarget( bool clearColor, bool clearDepth, float depthValue,
							const float colorValue[ 4 ] );
bool				VK_Exec_CopyRender( idImage *image, int x, int y, int width, int height,
							int cubeFace, bool copyDepth );
bool				VK_Exec_ResolveRenderTargets( idRenderTexture *sourceRenderTexture,
							idRenderTexture *destinationRenderTexture, bool resolveDepth );
// _currentDepth for this view, captured once (backEnd.currentDepthCopied)
bool				VK_Exec_CaptureViewDepth( const viewDef_t *viewDef );

VkPipelineLayout	VK_Exec_InteractionPipelineLayout( void );
VkPipeline VK_Exec_ProbeEnvironmentPipeline( bool transparent );
VkPipelineLayout VK_Exec_ProbeEnvironmentPipelineLayout();
// Source-alpha variants of the interaction pipelines; shadowMode is
// 0 unshadowed, 1 projected, 2 point, matching vkInterPass_t::shadowMode.
VkPipeline			VK_Exec_TransparentInteractionPipeline( int shadowMode, bool composite );
VkDescriptorSet		VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkDescriptorSet		VK_Exec_InteractionUniformSet( void );
int					VK_Exec_InteractionUniformAlloc( const void *data, int bytes );
// Update a reserved, not-yet-consumed slice in the current frame slot. Submission
// flushes the mapped ring; callers must keep the slice private until its draw.
bool VK_Exec_UpdateInteractionUniform( int offset, const void *data, int bytes );
int VK_Exec_InteractionUniformCheckpoint();
void VK_Exec_InteractionUniformRestore( int checkpoint );
// Authored programs have their own descriptor layout and larger uniform block.
bool VK_Exec_AuthoredUniformAlloc( const void *data, int bytes, VkDescriptorBufferInfo &buffer );
VkPipeline VK_Exec_AuthoredMaterialPipeline( int identity, VkShaderModule vertex,
    VkShaderModule fragment, VkPipelineLayout layout, int stateBits,
    bool separateColor, unsigned int vertexInputMask );
// Destroys every cached pipeline of an evicted authored version. The caller
// guarantees no frame in flight still references them.
void VK_Exec_ReleaseAuthoredMaterialPipelines( int identity );
bool VK_Exec_ProgramPipelineCapacityExhausted();
VkPipeline			VK_Exec_PostPipeline( int kind, VkShaderModule vertModule,
							VkShaderModule fragModule, int stateBits );
VkPipeline			VK_Exec_ExtraPipeline( int kind, VkShaderModule vertModule,
							VkShaderModule fragModule, int stateBits, int vertexLayout, int flags );
void				VK_Exec_TransitionImageForSampling( idImage *image );
// Queue one compact luminance sample per slot. Completion is delivered only
// after that slot's existing frame fence, without an extra GPU wait. The
// diagnostic synchronous mode submits and resumes this frame without presenting.
bool				VK_Exec_QueueHDRExposureReadback( idImage *image, unsigned int generation, int frame, bool synchronous );
bool VK_Exec_PrepareHDRExposureReadback();
// Explicit self-tests only: submits the diagnostic frame and waits for its fence.
bool VK_Exec_TestReadFloatImage( idImage *image, idList<float> &values );
// Explicit console capture: submit/wait/resume without presenting a partial frame.
bool VK_Exec_ReadLinearScreenshot( idImage *image, idList<float> &values );
// Raw RGBA8 mip zero, normalized to the image API's bottom-up row order.
bool VK_Exec_ReadImageScreenshot( idImage *image, idList<byte> &pixels );
void VK_PostProcess_ResetLinearCapture();
bool VK_PostProcess_LinearScreenshot( const char *fileName );
void				VK_PostProcess_ConsumeHDRSample( unsigned int generation, int frame, float logLuminance );
bool				VK_PostProcess_HDRSceneRequested( void );
// Called before any temporary scene scaling mutates the incoming viewport.
void VK_PostProcess_BeginView( const viewDef_t *viewDef );
// Portal-sky mask: pair each backdrop view with the main view it backs, once
// per executed command list; then, at the backdrop's depth clear, draw the
// main view's world depth and keep the backdrop to the pixels it leaves far.
void VK_PostProcess_PlanPortalSkyMask( const emptyCommand_t *cmds );
bool VK_PostProcess_PortalSkyMask( const viewDef_t *portalView );
// the main view the planned command list draws after this portal-sky backdrop, or NULL
const viewDef_t *VK_PostProcess_PortalSkyMainView( const viewDef_t *portalView );
bool VK_Exec_DrawWorldDepth( const viewDef_t *viewDef, bool ( *accept )( const drawSurf_t *surf ) );

// TAA transform history is independent of motion blur. Commit only after the
// matching color history is written; captures and failed writes discard it.
idImage *VK_PostProcess_TemporalMotionVectors( const viewDef_t *viewDef,
	idImage *depthImage, int width, int height, unsigned int generation,
	bool useHistory, bool &complete );
void VK_PostProcess_CommitTemporalMotion( const viewDef_t *viewDef,
	int width, int height, unsigned int generation );
void VK_PostProcess_ResetTemporalMotion( void );

bool				VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
// binds drawSurf->previousPositionCache at vertex binding 1 (temporal AA)
bool				VK_Exec_BindPreviousPositions( VkCommandBuffer cmd, int slot, const drawSurf_t *surf );
// streams vertices into the frame's vertex ring and binds them as binding 0
bool				VK_Exec_BindTransientVertices( VkCommandBuffer cmd, const void *data, int bytes );
void				VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] );
void				VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef,
							const drawSurf_t *drawSurf, int fbHeight );
void				VK_Exec_SetViewScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, int fbHeight );
// the 3D view's negative-height viewport, as VK_GuiExecutor_Draw3DView sets
// it; maxDepth 0.5 is the weapon depth range
bool				VK_Exec_SetViewViewport( VkCommandBuffer cmd, const viewDef_t *viewDef, float maxDepth );

// R_SetDrawInteraction (vk_Interactions.cpp)
void				VK_Interactions_SetDrawInteraction( const shaderStage_t *surfaceStage,
							const float *surfaceRegs, idImage **image, idVec4 matrix[ 2 ], float color[ 4 ] );

// Complete native baked-lighting admission, before HDR framebuffer ownership.
bool VK_LightGrid_PrepareModern( const viewDef_t *view, const drawSurf_t *surf,
    idImage *images[3], float params[7][4] );
void VK_PBR_PrepareBakedView( const viewDef_t *view );
bool VK_PBR_BakedViewReady( const viewDef_t *view );
bool VK_PBR_RetargetBakedView( const viewDef_t *view );
bool VK_PBR_BakedSurfaceOwned( const viewDef_t *view, const drawSurf_t *surf );
// A display-referred frame lights a natively owned PBR receiver's baked grid
// in its environment pass, never in the classic grid pass.
bool VK_PBR_GridOwned( const viewDef_t *view, const drawSurf_t *surf );
// The ambient walk visits a PBR surface without authored ambient stages when
// its environment pass has work: environment light, a diagnostic or a grid.
bool VK_PBR_EnvironmentVisit( const viewDef_t *view, const drawSurf_t *surf );
VkPipeline VK_Exec_BakedEnvironmentPipeline( bool probes );
VkPipelineLayout VK_Exec_BakedEnvironmentPipelineLayout();
VkDescriptorSet VK_Exec_BakedDescriptor( idImage * const images[3], VkDescriptorSet probes );
void VK_LightGrid_PrepareModernView( const viewDef_t *view );

// Native emission shares material admission with the direct-light owner and
// replaces only the proven matching additive ambient stage, once per surface.
bool VK_PBR_DepthCoverage( const drawSurf_t *surf );
// Change only the sealed transparent pipelines to match the current target.
// All required variants are obtained before any prepared record is modified.
bool VK_PBR_RetargetTransparentView( const viewDef_t *view );
// CPU receiver eligibility, not GPU resource admission. Used to avoid adding
// environment diffuse over a requested baked contribution.
bool VK_LightGrid_SurfaceRequestsBakedDiffuse( const viewDef_t *viewDef, const drawSurf_t *surf );
bool VK_PBR_EmissionForStage( const drawSurf_t *surf, int stageIndex,
		idImage *&image, float color[ 4 ] );

// Once per opaque/perforated surface, before its authored ambient stages.
// Translucent environment radiance joins the ordered composite below.
bool VK_PBR_DrawEnvironment( VkCommandBuffer cmd, const viewDef_t *viewDef,
	const drawSurf_t *surf, const srfTriangles_t *tri, const float mvp[16],
	float alphaScale = 0.0f, bool composite = false );

// Native ordered transparency. Returns true when the light pass recorded this
// surface's draws for the named source-alpha stage and has now composited
// them; the classic stage draw is then the replaced work, not extra work.
bool VK_PBR_BindTransparentGeometry( VkCommandBuffer cmd, const viewDef_t *viewDef,
		const drawSurf_t *surf );
bool VK_PBR_DrawTransparentStage( VkCommandBuffer cmd, const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri, const float mvp[ 16 ],
		int stageIndex, float alphaScale );
void VK_PBR_PrintGfxInfo();
// Prepare against the final single-color scene target, before an HDR MRT
// accumulation target is bound. The ordered replay keeps these sealed pipelines.
void VK_PBR_PrepareTransparentView( const viewDef_t *viewDef );
bool VK_PBR_TransparentSurfaceReady( const viewDef_t *viewDef, const drawSurf_t *surf );

// The debug tools' stencil prints (RB_CountStencilBuffer, RB_ScanStencilBuffer).
// The executor copies the active target's stencil out and hands it to
// VK_DebugTools_PrintStencilReadback once the frame's fence has signalled.
enum vkStencilReadbackMode_t {
	VK_STENCIL_READBACK_COUNT = 1,	// the average, "overdraw: %5.1f"
	VK_STENCIL_READBACK_SCAN		// the histogram, "stencil values:"
};
// failure, when given, receives why nothing was queued.
bool				VK_Exec_QueueStencilReadback( int mode, const char **failure = NULL );
void				VK_DebugTools_PrintStencilReadback( const byte *stencil, int width, int height, int mode );

#endif /* !__VK_EXECUTOR_HOOKS_H__ */
