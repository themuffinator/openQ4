// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan 2D/GUI executor (Phase D,
	docs/dev/plans/2026-07-18-vulkan-phase-d.md).

	Draws the engine's 2D views (main menu, console, HUD overlays, ROQ
	cinematics) on the swapchain. The contract mirrors the GL reference
	(RB_STD_DrawShaderPasses): 2D views arrive as RC_DRAW_VIEW commands
	whose viewDef->viewEntitys is NULL, with a front-end-built ortho
	projection, pre-evaluated shader registers, paint-order drawSurfs, and
	CPU-resident geometry (the vertex cache runs CPU-backed under Vulkan).

	Frame shape: the first clear/draw of a frame opens the swapchain
	rendering; RC_SWAP_BUFFERS closes and presents. Geometry streams
	through per-frame-in-flight host-visible rings; images bind through a
	per-image descriptor cache keyed on the image generation.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../Model_local.h"
#include "../RenderModuleAPI.h"

#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#ifndef INT_MIN
#define INT_MIN		( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX	0xffffffffu
#endif
#include <cstdio>
#include <cstring>
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "VulkanGpuFrameTiming.h"
#include "../ClassicGuiDomain.h"
#include "../ClassicCinematicPostDomain.h"
#include "../ClassicSpecialFrameDomain.h"
#include "../ClassicInteractionDomain.h"
#include "../ClassicWorldAmbientDomain.h"
#include "../ClassicFogBlendDomain.h"
#include "../GpuSkinning.h"
#include "../MaterialResourceTable.h"
#include "../RendererContracts.h"
#include "../RendererMetrics.h"
#include "../ScenePackets.h"
#include "vk_Image.h"
// vk_ShadowMap.h: VK_ShadowMap_Shutdown + the point cube pool size the
// descriptor pool budgets for (Phase F2a/F2b)
#include "vk_ShadowMap.h"
#include "shaders/gui_shaders_spv.h"
#include "shaders/temporal_resolve_spv.h"

extern idCVar r_skipDynamicTextures;

// vk_Interactions.cpp: the Phase F1 per-light interaction pass, inserted
// between the depth fill and the ambient walks
void VK_Interactions_DrawLights( const viewDef_t *viewDef );
bool VK_ClassicInteraction_Preflight( const viewDef_t *viewDef );
void VK_ClassicInteraction_DrawOwnedView( const viewDef_t *viewDef );
// vk_Interactions.cpp: the Phase G2 fog/blend light pass, inserted between
// the two ambient walks (RB_STD_DrawView's fog point)
void VK_Fog_DrawAllLights( const viewDef_t *viewDef );
bool VK_ClassicFogBlend_Preflight( const viewDef_t *viewDef );
void VK_ClassicFogBlend_DrawOwnedView( const viewDef_t *viewDef );

// frame-scope split (Phase F2a): the swapchain rendering scope can be
// suspended for the shadow atlas caster pass and resumed with loadOp LOAD
bool VK_Exec_BeginMainRendering( bool clearColorDepth );
void VK_Exec_EndMainRendering( void );
bool VK_GuiExecutor_EndFrameAndPresent( void );
static bool VK_GuiExecutor_SubmitFrame( bool present );
VkDescriptorSet VK_Exec_InteractionUniformSet( void );
int VK_Exec_InteractionUniformAlloc( const void *data, int bytes );
static bool VK_TemporalPresentation_DrawPendingSceneSpatial(
	const viewDef_t *viewDef, idImage *sceneImage, vkImageEntry_t *sceneEntry,
	idImage *depthImage, vkImageEntry_t *depthEntry );
static bool VK_TemporalPresentation_ResolveTargets(
	const resolveTemporalPresentationCommand_t &sourceCommand,
	bool trackExternalHistory, bool *historyAdvanced );

// no module-owned vertex-cache GPU state exists: the engine cache runs
// CPU-backed under Vulkan and the executor streams into its own rings
void VK_VertexCache_Shutdown( void ) {
}

/*
====================
Executor state
====================
*/
// world views stream all visible geometry through the rings each frame
// (the vertex cache is CPU-backed); sized for q4dm2-scale 3D views
static const int VK_VERTEX_RING_BYTES = 32 * 1024 * 1024;
static const int VK_INDEX_RING_BYTES = 8 * 1024 * 1024;
static const int VK_GPU_SKINNING_WORKGROUP_SIZE = 64;
static const int VK_GPU_SKINNING_MEMO_SIZE = 2048; // power of two
static const int VK_MAX_GUI_PIPELINES = 64;
static const int VK_MAX_SCREEN_PIPELINES = 64;
static const int VK_MAX_CUBE_PIPELINES = 32;
static const int VK_MAX_ENV_PIPELINES = 32;
static const int VK_MAX_PROGRAM_PIPELINES = 128;
static const int VK_MAX_BLEND_LIGHT_PIPELINES = 32;
static const int VK_MAX_SPECIAL_PIPELINES = 64;
static const int VK_MAX_TEMPORAL_RESOLVE_PIPELINES = 8;
static const int VK_MAX_DESCRIPTOR_SETS = 4096;
// rvspecial_depth.fs linearizes the Raven controller's depth texture with
// this historical near-plane convention.  The Vulkan path samples raw depth,
// so it must use the same value after translating normalized focus/range.
static const float VK_RVSPECIAL_DEPTH_ZNEAR = 0.25f;
// Descriptor sets displaced by a generation change during command recording
// cannot be rewritten until the slot fence completes.
static const int VK_MAX_RETIRED_SETS = 128;
// Per-draw blocks share one dynamic uniform ring. Ordinary interaction
// blocks retain their 256-byte range; the projected CSM block gets a
// shadow-only 512-byte range. Dynamic offsets use the device-reported
// alignment rather than assuming 256.
static const int VK_UNIFORM_RING_BYTES = 4 * 1024 * 1024;
static const int VK_UNIFORM_SLICE_BYTES = 256;
static const int VK_SHADOW_UNIFORM_SLICE_BYTES = 512;

typedef struct vkRing_s {
	VkBuffer		buffer;
	VmaAllocation	allocation;
	unsigned char *	mapped;
	int				capacity;
	int				cursor;
	bool			overflowWarned;	// latched per frame so one oversized scene cannot emit a warning per failed surface allocation
} vkRing_t;

typedef struct vkPipelineTarget_s {
	VkFormat			colorFormat;
	VkFormat			depthFormat;
	VkFormat			stencilFormat;
	VkSampleCountFlagBits samples;
} vkPipelineTarget_t;

typedef struct vkGuiPipeline_s {
	int				stateBits;		// GLS blend + per-channel write masks
	bool			separateColor;	// tightly-packed RGBA8 stream on binding 1
	vkPipelineTarget_t target;
	VkPipeline		pipeline;
} vkGuiPipeline_t;

typedef struct vkProgramPipeline_s {
	int				family;			// ARB family, or 0x100 + vkGLSLProgramFamily_t
	int				stateBits;		// GLS blend + per-channel write masks
	bool			separateColor;	// stage-specific decal color stream on binding 1
	vkPipelineTarget_t target;
	VkPipeline		pipeline;
} vkProgramPipeline_t;

typedef struct vkCubePipeline_s {
	int				stateBits;		// GLS blend + per-channel write masks
	bool			dirFromNormal;	// TG_DIFFUSE_CUBE: dir attribute reads the idDrawVert normal
	vkPipelineTarget_t target;
	VkPipeline		pipeline;
} vkCubePipeline_t;

enum vkSpecialPipelineKind_t {
	VK_SPECIAL_INTERACTION,
	VK_SPECIAL_SHADOW_INTERACTION,
	VK_SPECIAL_POINT_SHADOW_INTERACTION,
	VK_SPECIAL_STENCIL_SHADOW,
	VK_SPECIAL_FOG,
	VK_SPECIAL_SHADOW_OVERLAY_PANEL,
	VK_SPECIAL_SHADOW_OVERLAY_POINT_PANEL,
	VK_SPECIAL_SHADOW_OVERLAY_TEXT
};

typedef struct vkSpecialPipeline_s {
	vkSpecialPipelineKind_t kind;
	vkPipelineTarget_t target;
	VkPipeline		pipeline;
} vkSpecialPipeline_t;

typedef struct vkGuiPushConstants_s {
	float			mvp[ 16 ];
	float			stageColor[ 4 ];
	float			texMatrixS[ 4 ];
	float			texMatrixT[ 4 ];
	float			params[ 4 ];	// x: vertexColorMode, y: alphaTest, z: alphaTestRef, w: texMatrix enable
} vkGuiPushConstants_t;

// r_shadowMapDebugOverlay: one axis-aligned rectangle per draw. Deliberately
// smaller than vkGuiPushConstants_t so it stays inside the 128-byte push
// range every Vulkan implementation guarantees.
typedef struct vkShadowOverlayPush_s {
	float			rect[ 4 ];		// x, y, w, h in framebuffer pixels (top-left origin)
	float			uvRect[ 4 ];	// u0, v0, u1, v1 of the sampled region
	float			color[ 4 ];		// solid/glyph/border color
	float			params[ 4 ];	// x: glyph slot (< 0 = solid), y: grid divisions, z: active tiles
	float			screen[ 4 ];	// x: framebuffer width, y: framebuffer height
} vkShadowOverlayPush_t;

typedef struct vkBumpyEnvironmentBlock_s {
	float			localViewOrigin[ 4 ];
	float			modelRow0[ 4 ];
	float			modelRow1[ 4 ];
	float			modelRow2[ 4 ];
} vkBumpyEnvironmentBlock_t;

// std140-compatible block consumed by temporal_resolve.frag. Keep this under
// the established 256-byte dynamic-uniform slice so the temporal pass can
// reuse the interaction ring without adding another allocator or descriptor
// ownership surface.
typedef struct vkTemporalResolveBlock_s {
	float		sceneOutputExtent[ 4 ];
	float		currentReconstruct[ 4 ];
	float		previousProject[ 4 ];
	float		depthFeedback[ 4 ];
	float		currentViewOrigin[ 4 ];
	float		currentViewAxis0[ 4 ];
	float		currentViewAxis1[ 4 ];
	float		currentViewAxis2[ 4 ];
	float		previousViewOrigin[ 4 ];
	float		previousViewAxis0[ 4 ];
	float		previousViewAxis1[ 4 ];
	float		previousViewAxis2[ 4 ];
	float		temporalParams[ 4 ];
	float		motionParams[ 4 ];
	float		reactiveRect0[ 4 ];
	float		reactiveRect1[ 4 ];
} vkTemporalResolveBlock_t;

static_assert( sizeof( vkTemporalResolveBlock_t ) == 16 * 16,
	"Vulkan temporal resolve block must fill one 256-byte uniform slice" );

typedef struct vkDescriptorCacheEntry_s {
	VkDescriptorSet	set;
	unsigned int	generation;
} vkDescriptorCacheEntry_t;

// per-frame memo of surfaces already streamed into the rings: the depth
// fill and the two ambient walks visit the same tris, and re-uploading
// triples ring traffic. Direct-mapped on the geometry pointers; a
// collision just re-uploads.
static const int VK_TRI_MEMO_SIZE = 4096;	// power of two
typedef struct vkVertUpload_s {
	const void *	vertKey;
	int				vertexOffset;
} vkVertUpload_t;
typedef struct vkIdxUpload_s {
	const void *	idxKey;
	int				indexOffset;
} vkIdxUpload_t;

// GPU-skinned output is retained separately from the ordinary direct-mapped
// upload memo. Stencil volumes deliberately use the CPU shadow cache and must
// not evict a compute result that later ambient/interaction passes still need.
typedef struct vkGpuSkinningMemo_s {
	const void *	ambientKey;
	int				vertexOffset;
} vkGpuSkinningMemo_t;

typedef struct vkGpuSkinningPushConstants_s {
	uint32_t	sourceWordOffset;
	uint32_t	skinWordOffset;
	uint32_t	jointWordOffset;
	uint32_t	outputWordOffset;
	uint32_t	vertexCount;
	uint32_t	jointCount;
} vkGpuSkinningPushConstants_t;

typedef struct vkGpuSkinningDispatch_s {
	int				memoIndex;
	int				sourceOffset;
	int				skinOffset;
	int				jointOffset;
	int				outputOffset;
	int				sourceBytes;
	int				skinBytes;
	int				jointBytes;
	int				outputBytes;
	uint32_t	vertexCount;
	uint32_t	jointCount;
} vkGpuSkinningDispatch_t;

typedef struct vkGuiExecutor_s {
	bool				initialized;

	VkShaderModule		vertModule;
	VkShaderModule		fragModule;
	VkShaderModule		screenVertModule;
	VkShaderModule		screenFragModule;
	VkShaderModule		skyVertModule;
	VkShaderModule		skyFragModule;
	VkShaderModule		envVertModule;
	VkShaderModule		envFragModule;
	VkShaderModule		bumpyEnvVertModule;
	VkShaderModule		bumpyEnvFragModule;
	VkShaderModule		heatHazeVertModule;
	VkShaderModule		heatHazeFragModule;
	VkShaderModule		heatHazeMaskFragModule;
	VkShaderModule		heatHazeVertexVertModule;
	VkShaderModule		heatHazeMaskVertexFragModule;
	VkShaderModule		monochromeVertModule;
	VkShaderModule		monochromeFragModule;
	VkShaderModule		glassWarpVertModule;
	VkShaderModule		glassWarpFragModule;
	VkShaderModule		refractiveGlassVertModule;
	VkShaderModule		refractiveGlassFragModule;
	VkShaderModule		glslMaterialVertModules[ VK_GLSL_PROGRAM_FAMILY_COUNT ];
	VkShaderModule		glslMaterialFragModules[ VK_GLSL_PROGRAM_FAMILY_COUNT ];
	VkShaderModule		interactionVertModule;
	VkShaderModule		interactionFragModule;
	VkShaderModule		interactionShadowVertModule;
	VkShaderModule		interactionShadowFragModule;
	VkShaderModule		interactionShadowPointVertModule;
	VkShaderModule		interactionShadowPointFragModule;
	VkShaderModule		casterVertModule;
	VkShaderModule		casterFragModule;
	VkShaderModule		pointCasterVertModule;
	VkShaderModule		pointCasterFragModule;
	VkShaderModule		stencilShadowVertModule;
	VkShaderModule		stencilShadowFragModule;
	VkShaderModule		shadowOverlayVertModule;
	VkShaderModule		shadowOverlayFragModule;
	VkShaderModule		shadowOverlayPointFragModule;
	VkShaderModule		shadowOverlayTextFragModule;
	VkShaderModule		fogVertModule;
	VkShaderModule		fogFragModule;
	VkShaderModule		blendLightVertModule;
	VkShaderModule		blendLightFragModule;
	VkShaderModule		gpuSkinningModule;
	VkShaderModule		temporalResolveVertModule;
	VkShaderModule		temporalResolveFragModule;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSetLayout uboSetLayout;		// one dynamic uniform buffer (interaction block ring)
	// shadow receiver set: binding 0 = atlas + compare sampler (fragment),
	// binding 1 = dynamic uniform buffer (per-space shadow block ring slice)
	VkDescriptorSetLayout shadowSetLayout;
	VkDescriptorSetLayout gpuSkinningSetLayout;
	VkDescriptorPool	descriptorPool;
	VkPipelineLayout	pipelineLayout;
	VkPipelineLayout	gpuSkinningPipelineLayout;
	VkPipeline			gpuSkinningPipeline;
	// interactions: 6 single-combined-sampler sets (0=specTable, 1=bump,
	// 2=falloff, 3=lightProjection, 4=diffuse, 5=specular) + set 6 dynamic UBO
	VkPipelineLayout	interactionPipelineLayout;
	// shadow-receiving interactions add set 7 (atlas compare sampler + shadow UBO)
	VkPipelineLayout	shadowInteractionPipelineLayout;
	// r_shadowMapDebugOverlay: set 0 is the same shadow receiver layout, so the
	// atlas set and the point cube sets bind unchanged; the text variant
	// declares no sampler and binds no set at all
	VkPipelineLayout	shadowOverlayPipelineLayout;
	// fog/blend lights: 2 single-combined-sampler sets (0=fog/projection,
	// 1=fogEnter/falloff) + set 2 dynamic UBO (blend-light block ring)
	VkPipelineLayout	fogBlendPipelineLayout;
	VkPipeline			casterPipeline;			// lazily built; depth-only atlas caster
	VkPipeline			pointCasterPipeline;	// lazily built; depth-only cube-face caster
	vkSpecialPipeline_t	specialPipelines[ VK_MAX_SPECIAL_PIPELINES ];
	int					numSpecialPipelines;
	vkGuiPipeline_t		pipelines[ VK_MAX_GUI_PIPELINES ];
	int					numPipelines;
	int					lastPipelineMRU;	// index of the last exact-key VK_GuiExecutor_GetPipeline result
	vkGuiPipeline_t		screenPipelines[ VK_MAX_SCREEN_PIPELINES ];
	int					numScreenPipelines;
	vkCubePipeline_t	cubePipelines[ VK_MAX_CUBE_PIPELINES ];
	int					numCubePipelines;
	vkGuiPipeline_t		envPipelines[ VK_MAX_ENV_PIPELINES ];
	int					numEnvPipelines;
	vkProgramPipeline_t	programPipelines[ VK_MAX_PROGRAM_PIPELINES ];
	int					numProgramPipelines;
	vkGuiPipeline_t		blendLightPipelines[ VK_MAX_BLEND_LIGHT_PIPELINES ];	// per light-stage blend bits
	int					numBlendLightPipelines;
	vkGuiPipeline_t		temporalResolvePipelines[ VK_MAX_TEMPORAL_RESOLVE_PIPELINES ];
	int					numTemporalResolvePipelines;
	VkFormat			pipelineTargetFormat;	// swapchain format the pipelines were built for

	vkRing_t			vertexRings[ VK_FRAMES_IN_FLIGHT ];
	vkRing_t			indexRings[ VK_FRAMES_IN_FLIGHT ];
	vkRing_t			uniformRings[ VK_FRAMES_IN_FLIGHT ];
	VkDescriptorSet		uniformRingSets[ VK_FRAMES_IN_FLIGHT ];
	VkDescriptorSet		shadowSets[ VK_FRAMES_IN_FLIGHT ];
	VkDescriptorSet		gpuSkinningSets[ VK_FRAMES_IN_FLIGHT ];
	bool				shadowSetsHaveAtlas;	// compare + raw bindings written with a live atlas view
	bool				gpuSkinningAvailable;

	vkDescriptorCacheEntry_t descriptorCache[ 4096 ];	// parallel to the image table
	VkDescriptorSet		retiredSets[ VK_FRAMES_IN_FLIGHT ][ VK_MAX_RETIRED_SETS ];
	int					numRetiredSets[ VK_FRAMES_IN_FLIGHT ];

	// frame-in-progress state
	bool				frameOpen;
	// The acquire semaphore is consumed by the first submission that uses
	// this swapchain image. A synchronous screenshot readback can split one
	// logical engine frame into two submissions without reacquiring it.
	bool				acquireWaitPending;
	bool				mainScopeOpen;		// the swapchain dynamic-rendering scope is recording
	int					frameSlot;
	uint32_t			swapImageIndex;
	VkCommandBuffer		cmd;
	float				clearColor[ 4 ];
	int					boundVertexOffset;	// binding-0 ring offset of the last VK_Exec_BindTriGeometry
	idRenderTexture *	activeRenderTexture;
	vkImageEntry_t *	activeColorEntry;
	vkImageEntry_t *	activeDepthEntry;
	VkImageView			activeDepthAttachmentView;
	VkExtent2D			activeExtent;
	vkPipelineTarget_t	activePipelineTarget;
	// Direct 3D presentation is isolated from the native swapchain so dynamic
	// resolution never filters the HUD/menu views that follow it.  Game-owned
	// feedback render targets use the same coordinate mapper but keep their own
	// allocation and final-post ownership.
	idImage *			temporalSceneColorImage;
	idImage *			temporalSceneDepthImage;
	idRenderTexture *	temporalSceneRenderTexture;
	int				temporalSceneWidth;
	int				temporalSceneHeight;
	int				temporalNativeWidth;
	int				temporalNativeHeight;
	VkFormat			temporalSwapchainFormat;
	int				temporalSceneFrame;
	const viewDef_t *	temporalSceneRoot;
	bool				temporalScenePendingComposite;
	bool				temporalSceneAllocationWarned;
	idImage *			temporalDirectHistoryImages[ 2 ];
	idRenderTexture *	temporalDirectHistoryRenderTextures[ 2 ];
	unsigned long long temporalDirectHistoryStorageGenerations[ 2 ];
	int				temporalDirectHistoryWidth;
	int				temporalDirectHistoryHeight;
	int				temporalDirectHistoryReadIndex;
	int				temporalDirectHistoryFrame;
	unsigned int		temporalDirectHistoryGeneration;
	unsigned long long temporalDirectHistoryViewIdentity;
	bool				temporalDirectHistoryValid;
	bool				temporalDirectHistoryAllocationWarned;
	idRenderTexture *	temporalDepthStampTarget;
	unsigned long long temporalDepthStampStorageGeneration;
	int				temporalDepthStampFrame;
	unsigned int		temporalDepthStampHistoryGeneration;
	bool				temporalDepthStampValid;
	unsigned int		temporalHistoryGenerationSeen;
	int				temporalCaptureInvalidatedFrame;
	int				temporalSceneCompositeInvalidatedFrame;
	idRenderTexture *	temporalHistoryTargets[ 2 ];
	unsigned long long temporalHistoryStorageGenerations[ 2 ];
	const viewDef_t *	pendingSpecialEffectsView;
	int					pendingSpecialEffectsMask;
	idRenderTexture *	pendingSpecialEffectsSource;
	bool				pendingSpecialEffectsNeedsResolve;

	vkVertUpload_t		vertMemo[ VK_TRI_MEMO_SIZE ];
	vkIdxUpload_t		idxMemo[ VK_TRI_MEMO_SIZE ];
	vkGpuSkinningMemo_t gpuSkinningMemo[ VK_GPU_SKINNING_MEMO_SIZE ];
	uint64				gpuSkinningDispatches;
	uint64				gpuSkinningVertices;
	uint64				gpuSkinningRingFallbacks;
	uint64				gpuSkinningMemoFallbacks;
	uint64				gpuSkinningValidationFallbacks;
} vkGuiExecutor_t;

static vkGuiExecutor_t vkExec;
static bool vkGpuSkinningBackendRequested = true;

typedef struct vkSceneScaleState_s {
	bool			active;
	bool			backendOwned;
	bool			projectionRecentered;
	const viewDef_t *rootView;
	int			nativeWidth;
	int			nativeHeight;
	int			targetWidth;
	int			targetHeight;
	float			nativeProjectionOffsetX;
	float			nativeProjectionOffsetY;
	idScreenRect	nativeViewport;
	typedef struct rectRestore_s {
		idScreenRect *rect;
		idScreenRect original;
	} rectRestore_t;
	idList<rectRestore_t> rectRestores;
	idHashIndex rectRestoreHash;
} vkSceneScaleState_t;

// Shared-domain preflight is allowed to populate the ordinary geometry rings,
// but it has not taken framebuffer ownership yet. Snapshot the bounded upload
// state so any later rejection can return the established classic walker to
// the exact cursor/memo state it would have seen without the speculative walk.
typedef struct vkSharedGeometryCheckpoint_s {
	bool			active;
	int			frameSlot;
	int			vertexCursor;
	int			indexCursor;
	int			boundVertexOffset;
	vkVertUpload_t	vertMemo[ VK_TRI_MEMO_SIZE ];
	vkIdxUpload_t	idxMemo[ VK_TRI_MEMO_SIZE ];
	vkGpuSkinningMemo_t gpuSkinningMemo[ VK_GPU_SKINNING_MEMO_SIZE ];
} vkSharedGeometryCheckpoint_t;

static vkSharedGeometryCheckpoint_t vkSharedGeometryCheckpoint;

static vkPipelineTarget_t VK_Exec_SwapchainPipelineTarget( void ) {
	vkPipelineTarget_t target;
	target.colorFormat = vkCtx.swapchainFormat;
	target.depthFormat = vkCtx.depthFormat;
	target.stencilFormat = vkCtx.depthFormat;
	target.samples = VK_SAMPLE_COUNT_1_BIT;
	return target;
}

static const vkPipelineTarget_t &VK_Exec_CurrentPipelineTarget( void ) {
	if ( vkExec.frameOpen && vkExec.activePipelineTarget.colorFormat != VK_FORMAT_UNDEFINED ) {
		return vkExec.activePipelineTarget;
	}
	static vkPipelineTarget_t swapchainTarget;
	swapchainTarget = VK_Exec_SwapchainPipelineTarget();
	return swapchainTarget;
}

static bool VK_Exec_PipelineTargetsMatch( const vkPipelineTarget_t &a, const vkPipelineTarget_t &b ) {
	return a.colorFormat == b.colorFormat
			&& a.depthFormat == b.depthFormat
			&& a.stencilFormat == b.stencilFormat
			&& a.samples == b.samples;
}

/*
====================
VK_FixupClipSpaceZ

The front-end builds GL-convention projections (NDC z in [-1,1]; the 2D
ortho even lands gui verts at exactly -1). Vulkan clips to 0 <= z <= w, so
every MVP is remapped at assembly: row2 = (row2 + row3) / 2. Window depth
then matches GL's glDepthRange(0,1), keeping depth-compare parity for the
world passes. Column-major float[16]: row2 = elements 2,6,10,14.
====================
*/
void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] ) {
	RendererContracts_ConvertClipMatrix( dst, src,
		RendererContracts_GLClipSpace(), RendererContracts_VulkanClipSpace() );
}

static bool VK_BuildCanonicalViewport( VkViewport &destination, float x, float y,
		float width, float height, float framebufferHeight,
		float minDepth = 0.0f, float maxDepth = 1.0f ) {
	const rendererCanonicalViewport_t source = {
		x, y, width, height, minDepth, maxDepth
	};
	rendererBackendViewport_t backend;
	if ( !RendererContracts_BuildViewport( backend, source, framebufferHeight,
			RendererContracts_VulkanClipSpace() ) ) {
		return false;
	}
	destination.x = backend.x;
	destination.y = backend.y;
	destination.width = backend.width;
	destination.height = backend.height;
	destination.minDepth = backend.minDepth;
	destination.maxDepth = backend.maxDepth;
	return true;
}

/*
====================
Rings
====================
*/
static bool VK_Ring_Create( vkRing_t &ring, int capacity, VkBufferUsageFlags usage ) {
	VkBufferCreateInfo bci;
	memset( &bci, 0, sizeof( bci ) );
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = (VkDeviceSize)capacity;
	bci.usage = usage;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo info;
	if ( vmaCreateBuffer( vkCtx.allocator, &bci, &vaci, &ring.buffer, &ring.allocation, &info ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: ring buffer creation failed (%d bytes)", capacity );
		return false;
	}
	ring.mapped = (unsigned char *)info.pMappedData;
	ring.capacity = capacity;
	ring.cursor = 0;
	return true;
}

static int VK_Ring_Alloc( vkRing_t &ring, const void *data, size_t bytes, int alignment ) {
	if ( data == NULL || ring.mapped == NULL || bytes == 0 || ring.capacity <= 0 ||
			ring.cursor < 0 || ring.cursor > ring.capacity || alignment <= 0 ||
			( alignment & ( alignment - 1 ) ) != 0 ) {
		if ( !ring.overflowWarned ) {
			common->Warning( "Vulkan: invalid frame geometry ring allocation" );
			ring.overflowWarned = true;
		}
		return -1;
	}

	const size_t capacity = static_cast<size_t>( ring.capacity );
	const size_t alignmentMask = static_cast<size_t>( alignment - 1 );
	const size_t offset = ( static_cast<size_t>( ring.cursor ) + alignmentMask ) & ~alignmentMask;
	if ( offset > capacity || bytes > capacity - offset ) {
		if ( !ring.overflowWarned ) {
			common->Warning( "Vulkan: frame geometry ring overflow (%zu + %zu > %zu)", offset, bytes, capacity );
			ring.overflowWarned = true;
		}
		return -1;
	}
	memcpy( ring.mapped + offset, data, bytes );
	// Host writes are flushed once per ring in VK_GuiExecutor_SubmitFrame, before
	// the queue submit that consumes them, rather than once per allocation.
	ring.cursor = static_cast<int>( offset + bytes );
	return static_cast<int>( offset );
}

// Reserve device-written output without spending host bandwidth initializing
// it. The same bounds and power-of-two alignment contract as VK_Ring_Alloc is
// retained; the frame-slot fence protects the range until the next reuse.
static int VK_Ring_Reserve( vkRing_t &ring, size_t bytes, int alignment ) {
	if ( ring.mapped == NULL || bytes == 0 || ring.capacity <= 0
			|| ring.cursor < 0 || ring.cursor > ring.capacity || alignment <= 0
			|| ( alignment & ( alignment - 1 ) ) != 0 ) {
		if ( !ring.overflowWarned ) {
			common->Warning( "Vulkan: invalid frame geometry ring reservation" );
			ring.overflowWarned = true;
		}
		return -1;
	}

	const size_t capacity = static_cast<size_t>( ring.capacity );
	const size_t alignmentMask = static_cast<size_t>( alignment - 1 );
	const size_t offset = ( static_cast<size_t>( ring.cursor ) + alignmentMask ) & ~alignmentMask;
	if ( offset > capacity || bytes > capacity - offset ) {
		if ( !ring.overflowWarned ) {
			common->Warning( "Vulkan: frame geometry ring reservation overflow (%zu + %zu > %zu)",
				offset, bytes, capacity );
			ring.overflowWarned = true;
		}
		return -1;
	}
	ring.cursor = static_cast<int>( offset + bytes );
	return static_cast<int>( offset );
}

static int VK_Exec_UniformSliceAlignment( const int sliceBytes ) {
	VkDeviceSize alignment =
			vkCtx.deviceProperties.limits.minUniformBufferOffsetAlignment;
	if ( alignment < (VkDeviceSize)sliceBytes ) {
		alignment = (VkDeviceSize)sliceBytes;
	}
	if ( alignment == 0 || alignment > (VkDeviceSize)INT_MAX ) {
		return 0;
	}
	const int intAlignment = (int)alignment;
	return ( intAlignment & ( intAlignment - 1 ) ) == 0
			? intAlignment : 0;
}

/*
====================
Pipelines
====================
*/
static VkBlendFactor VK_BlendFactorFromGLSSrc( int bits ) {
	switch ( bits & GLS_SRCBLEND_BITS ) {
		case GLS_SRCBLEND_ZERO:					return VK_BLEND_FACTOR_ZERO;
		case GLS_SRCBLEND_DST_COLOR:			return VK_BLEND_FACTOR_DST_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case GLS_SRCBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_SRCBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case GLS_SRCBLEND_ALPHA_SATURATE:		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		case GLS_SRCBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		default:								return VK_BLEND_FACTOR_ONE;
	}
}

static VkBlendFactor VK_BlendFactorFromGLSDst( int bits ) {
	switch ( bits & GLS_DSTBLEND_BITS ) {
		case GLS_DSTBLEND_ONE:					return VK_BLEND_FACTOR_ONE;
		case GLS_DSTBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case GLS_DSTBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_DSTBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		default:								return VK_BLEND_FACTOR_ZERO;
	}
}

// shared graphics-pipeline assembly for the GUI, cube-texgen, interaction,
// shadow-caster, and stencil-shadow variants: everything except the shader
// modules, vertex input, and pipeline layout is identical (dynamic
// depth/cull/bias/stencil state, blend from the GLS bits, dynamic rendering
// against the swapchain + depth formats). depthOnly pipelines target the
// shadow atlas: zero color attachments and the separately probed sampled
// shadow-depth format. colorWriteOff keeps the swapchain color
// attachment but masks every channel (GLS_COLORMASK|GLS_ALPHAMASK for the
// stencil shadow volumes) — the write mask is not dynamic without EDS3, so
// it is a pipeline-level variant
static VkPipeline VK_Exec_CreatePipeline( VkShaderModule vertModule, VkShaderModule fragModule,
		const VkPipelineVertexInputStateCreateInfo *vertexInput, int blendBits, VkPipelineLayout layout,
		bool depthOnly, bool colorWriteOff, const vkPipelineTarget_t &target,
		bool enableDepthClamp = false ) {
	VkPipelineShaderStageCreateInfo stages[ 2 ];
	memset( stages, 0, sizeof( stages ) );
	stages[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[ 0 ].module = vertModule;
	stages[ 0 ].pName = "main";
	stages[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[ 1 ].module = fragModule;
	stages[ 1 ].pName = "main";

	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState;
	memset( &viewportState, 0, sizeof( viewportState ) );
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster;
	memset( &raster, 0, sizeof( raster ) );
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;	// 2D
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	raster.depthClampEnable = enableDepthClamp ? VK_TRUE : VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisample;
	memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = target.samples;

	VkPipelineColorBlendAttachmentState blendAttachment;
	memset( &blendAttachment, 0, sizeof( blendAttachment ) );
	if ( !colorWriteOff ) {
		if ( !( blendBits & GLS_REDMASK ) ) {
			blendAttachment.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
		}
		if ( !( blendBits & GLS_GREENMASK ) ) {
			blendAttachment.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
		}
		if ( !( blendBits & GLS_BLUEMASK ) ) {
			blendAttachment.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
		}
		if ( !( blendBits & GLS_ALPHAMASK ) ) {
			blendAttachment.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
		}
	}
	const VkBlendFactor srcFactor = VK_BlendFactorFromGLSSrc( blendBits );
	const VkBlendFactor dstFactor = VK_BlendFactorFromGLSDst( blendBits );
	if ( !( srcFactor == VK_BLEND_FACTOR_ONE && dstFactor == VK_BLEND_FACTOR_ZERO ) ) {
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = srcFactor;
		blendAttachment.dstColorBlendFactor = dstFactor;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = srcFactor;
		blendAttachment.dstAlphaBlendFactor = dstFactor;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	VkPipelineColorBlendStateCreateInfo blendState;
	memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = depthOnly ? 0 : 1;
	blendState.pAttachments = depthOnly ? NULL : &blendAttachment;

	// depth/cull/bias are core-1.3 dynamic state, so one pipeline per blend
	// combination serves 2D (depth off) and the world passes (per-stage
	// depth func/write, per-material cull, polygon-offset). All five
	// stencil states are dynamic on EVERY pipeline (Phase G1): test enable
	// and op are core-1.3 promotions from extended_dynamic_state — the same
	// mandatory family as CULL_MODE/FRONT_FACE above — and the masks +
	// reference are core 1.0; the interaction pipelines can then toggle
	// per-light stencil without pipeline-cache growth. The frame baseline
	// (VK_Exec_BeginMainRendering) latches all five before any draw.
	VkDynamicState dynamicStates[ 16 ] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
		VK_DYNAMIC_STATE_CULL_MODE,
		VK_DYNAMIC_STATE_FRONT_FACE,
		VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
		VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
		VK_DYNAMIC_STATE_STENCIL_OP,
		VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
		VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
	};
	uint32_t dynamicStateCount = 14;
	if ( vkCtx.depthBoundsSupported ) {
		dynamicStates[ dynamicStateCount++ ] = VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
		dynamicStates[ dynamicStateCount++ ] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
	}
	VkPipelineDynamicStateCreateInfo dynamicState;
	memset( &dynamicState, 0, sizeof( dynamicState ) );
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = dynamicStateCount;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineDepthStencilStateCreateInfo depthStencil;
	memset( &depthStencil, 0, sizeof( depthStencil ) );
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	// depth test/write/compare and the whole stencil block are dynamic

	VkPipelineRenderingCreateInfo rendering;
	memset( &rendering, 0, sizeof( rendering ) );
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = depthOnly ? 0 : 1;
	rendering.pColorAttachmentFormats = depthOnly ? NULL : &target.colorFormat;
	rendering.depthAttachmentFormat = target.depthFormat;
	rendering.stencilAttachmentFormat = target.stencilFormat;

	VkGraphicsPipelineCreateInfo gpci;
	memset( &gpci, 0, sizeof( gpci ) );
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.pNext = &rendering;
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = vertexInput;
	gpci.pInputAssemblyState = &inputAssembly;
	gpci.pViewportState = &viewportState;
	gpci.pRasterizationState = &raster;
	gpci.pMultisampleState = &multisample;
	gpci.pDepthStencilState = &depthStencil;
	gpci.pColorBlendState = &blendState;
	gpci.pDynamicState = &dynamicState;
	gpci.layout = layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	if ( vkCreateGraphicsPipelines( vkCtx.device, vkCtx.pipelineCache, 1, &gpci, NULL, &pipeline ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: pipeline creation failed (blend 0x%x)", blendBits );
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

static VkPipeline VK_GuiExecutor_GetPipeline( int stateBits, bool separateColor = false ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	if ( vkExec.lastPipelineMRU >= 0 && vkExec.lastPipelineMRU < vkExec.numPipelines ) {
		const vkGuiPipeline_t &mru = vkExec.pipelines[ vkExec.lastPipelineMRU ];
		if ( mru.stateBits == pipelineBits && mru.separateColor == separateColor
				&& VK_Exec_PipelineTargetsMatch( mru.target, target ) ) {
			return mru.pipeline;
		}
	}

	for ( int i = 0; i < vkExec.numPipelines; i++ ) {
		if ( vkExec.pipelines[ i ].stateBits == pipelineBits
				&& vkExec.pipelines[ i ].separateColor == separateColor
				&& VK_Exec_PipelineTargetsMatch( vkExec.pipelines[ i ].target, target ) ) {
			vkExec.lastPipelineMRU = i;
			return vkExec.pipelines[ i ].pipeline;
		}
	}
	if ( vkExec.numPipelines >= VK_MAX_GUI_PIPELINES ) {
		common->Warning( "Vulkan: GUI pipeline cache exhausted" );
		return vkExec.pipelines[ 0 ].pipeline;
	}

	// idDrawVert: xyz@0, color ubyte4@12, st@56 (64-byte stride)
	VkVertexInputBindingDescription bindings[ 2 ];
	memset( bindings, 0, sizeof( bindings ) );
	bindings[ 0 ].binding = 0;
	bindings[ 0 ].stride = sizeof( idDrawVert );
	bindings[ 0 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[ 1 ].binding = 1;
	bindings[ 1 ].stride = 4;
	bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 3 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = 0;
	attrs[ 1 ].location = 1;
	attrs[ 1 ].binding = separateColor ? 1 : 0;
	attrs[ 1 ].format = VK_FORMAT_R8G8B8A8_UNORM;
	attrs[ 1 ].offset = separateColor ? 0 : 12;
	attrs[ 2 ].location = 2;
	attrs[ 2 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 2 ].offset = 56;

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = separateColor ? 2 : 1;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 3;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.vertModule, vkExec.fragModule,
			&vertexInput, pipelineBits, vkExec.pipelineLayout, false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return vkExec.numPipelines > 0 ? vkExec.pipelines[ 0 ].pipeline : VK_NULL_HANDLE;
	}
	vkExec.pipelines[ vkExec.numPipelines ].stateBits = pipelineBits;
	vkExec.pipelines[ vkExec.numPipelines ].separateColor = separateColor;
	vkExec.pipelines[ vkExec.numPipelines ].target = target;
	vkExec.pipelines[ vkExec.numPipelines ].pipeline = pipeline;
	vkExec.numPipelines++;
	vkExec.lastPipelineMRU = vkExec.numPipelines - 1;
	return pipeline;
}

/*
====================
VK_GuiExecutor_GetPipelineStrict

The legacy material walker historically tolerates GUI-pipeline allocation or
cache exhaustion by returning the first cached pipeline.  Whole-view shared
ownership cannot: accepting a different blend/write-mask pipeline would make
the backend claim a view whose framebuffer result is known to be wrong.  Ask
the normal cache to populate the exact key, then prove that the returned
object is backed by that key before exposing it to the transactional path.
====================
*/
static VkPipeline VK_GuiExecutor_GetPipelineStrict( int stateBits ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	for ( int i = 0; i < vkExec.numPipelines; ++i ) {
		const vkGuiPipeline_t &entry = vkExec.pipelines[ i ];
		if ( entry.stateBits == pipelineBits && !entry.separateColor
				&& VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return entry.pipeline;
		}
	}
	if ( vkExec.numPipelines >= VK_MAX_GUI_PIPELINES ) {
		return VK_NULL_HANDLE;
	}

	const VkPipeline candidate = VK_GuiExecutor_GetPipeline( stateBits, false );
	if ( candidate == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	for ( int i = 0; i < vkExec.numPipelines; ++i ) {
		const vkGuiPipeline_t &entry = vkExec.pipelines[ i ];
		if ( entry.pipeline == candidate && entry.stateBits == pipelineBits
				&& !entry.separateColor
				&& VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return candidate;
		}
	}
	return VK_NULL_HANDLE;
}

static VkPipeline VK_GuiExecutor_GetScreenPipeline( int stateBits,
		bool separateColor = false ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numScreenPipelines; i++ ) {
		if ( vkExec.screenPipelines[ i ].stateBits == pipelineBits
				&& vkExec.screenPipelines[ i ].separateColor == separateColor
				&& VK_Exec_PipelineTargetsMatch( vkExec.screenPipelines[ i ].target, target ) ) {
			return vkExec.screenPipelines[ i ].pipeline;
		}
	}
	if ( vkExec.screenVertModule == VK_NULL_HANDLE
			|| vkExec.screenFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	if ( vkExec.numScreenPipelines >= VK_MAX_SCREEN_PIPELINES ) {
		common->Warning( "Vulkan: screen pipeline cache exhausted" );
		return vkExec.screenPipelines[ 0 ].pipeline;
	}

	VkVertexInputBindingDescription bindings[ 2 ];
	memset( bindings, 0, sizeof( bindings ) );
	bindings[ 0 ].binding = 0;
	bindings[ 0 ].stride = sizeof( idDrawVert );
	bindings[ 0 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[ 1 ].binding = 1;
	bindings[ 1 ].stride = 4;
	bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 2 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	attrs[ 1 ].location = 1;
	attrs[ 1 ].binding = separateColor ? 1 : 0;
	attrs[ 1 ].format = VK_FORMAT_R8G8B8A8_UNORM;
	attrs[ 1 ].offset = separateColor ? 0 : (uint32_t)offsetof( idDrawVert, color );

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = separateColor ? 2 : 1;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.screenVertModule,
			vkExec.screenFragModule, &vertexInput, pipelineBits,
			vkExec.pipelineLayout, false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return vkExec.numScreenPipelines > 0
				? vkExec.screenPipelines[ 0 ].pipeline : VK_NULL_HANDLE;
	}
	vkGuiPipeline_t &entry = vkExec.screenPipelines[ vkExec.numScreenPipelines++ ];
	entry.stateBits = pipelineBits;
	entry.separateColor = separateColor;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

static VkPipeline VK_TemporalPresentation_GetResolvePipeline( void ) {
	const int pipelineBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO;
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	for ( int i = 0; i < vkExec.numTemporalResolvePipelines; ++i ) {
		const vkGuiPipeline_t &entry = vkExec.temporalResolvePipelines[i];
		if ( VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return entry.pipeline;
		}
	}
	if ( vkExec.temporalResolveVertModule == VK_NULL_HANDLE
			|| vkExec.temporalResolveFragModule == VK_NULL_HANDLE
			|| vkExec.numTemporalResolvePipelines
				>= VK_MAX_TEMPORAL_RESOLVE_PIPELINES ) {
		return VK_NULL_HANDLE;
	}

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	const VkPipeline pipeline = VK_Exec_CreatePipeline(
		vkExec.temporalResolveVertModule, vkExec.temporalResolveFragModule,
		&vertexInput, pipelineBits, vkExec.interactionPipelineLayout,
		false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkGuiPipeline_t &entry = vkExec.temporalResolvePipelines[
		vkExec.numTemporalResolvePipelines++ ];
	entry.stateBits = pipelineBits;
	entry.separateColor = false;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

// cube-texgen pipelines (TG_SKYBOX_CUBE / TG_WOBBLESKY_CUBE / TG_DIFFUSE_CUBE):
// position from the idDrawVert stream plus a vec3 direction attribute — the
// front-end texgen's tightly packed stream on binding 1 for the skies, or
// the idDrawVert normal straight off binding 0 for diffuse cube maps
static VkPipeline VK_GuiExecutor_GetCubePipeline( int stateBits, bool dirFromNormal ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
		if ( vkExec.cubePipelines[ i ].stateBits == pipelineBits
				&& vkExec.cubePipelines[ i ].dirFromNormal == dirFromNormal
				&& VK_Exec_PipelineTargetsMatch( vkExec.cubePipelines[ i ].target, target ) ) {
			return vkExec.cubePipelines[ i ].pipeline;
		}
	}
	if ( vkExec.numCubePipelines >= VK_MAX_CUBE_PIPELINES ) {
		common->Warning( "Vulkan: cube pipeline cache exhausted" );
		return vkExec.cubePipelines[ 0 ].pipeline;
	}

	VkVertexInputBindingDescription bindings[ 2 ];
	memset( bindings, 0, sizeof( bindings ) );
	bindings[ 0 ].binding = 0;
	bindings[ 0 ].stride = sizeof( idDrawVert );
	bindings[ 0 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[ 1 ].binding = 1;
	bindings[ 1 ].stride = sizeof( idVec3 );
	bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 2 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].binding = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = 0;
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	if ( dirFromNormal ) {
		attrs[ 1 ].binding = 0;
		attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, normal );
	} else {
		attrs[ 1 ].binding = 1;
		attrs[ 1 ].offset = 0;
	}

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = dirFromNormal ? 1 : 2;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.skyVertModule, vkExec.skyFragModule,
			&vertexInput, pipelineBits, vkExec.pipelineLayout, false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkExec.cubePipelines[ vkExec.numCubePipelines ].stateBits = pipelineBits;
	vkExec.cubePipelines[ vkExec.numCubePipelines ].dirFromNormal = dirFromNormal;
	vkExec.cubePipelines[ vkExec.numCubePipelines ].target = target;
	vkExec.cubePipelines[ vkExec.numCubePipelines ].pipeline = pipeline;
	vkExec.numCubePipelines++;
	return pipeline;
}

static void VK_Exec_InteractionVertexInput( VkVertexInputBindingDescription &binding,
		VkVertexInputAttributeDescription attrs[ 6 ],
		VkPipelineVertexInputStateCreateInfo &vertexInput );

// TG_REFLECT_CUBE follows Quake 4's environment.vfp or, when the material
// owns a bump stage, bumpyEnvironment.vfp with a second sampler and the
// model-space transform block.
static VkPipeline VK_GuiExecutor_GetEnvironmentPipeline( int stateBits, bool bumpy ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numEnvPipelines; i++ ) {
		if ( vkExec.envPipelines[ i ].stateBits == pipelineBits
				&& vkExec.envPipelines[ i ].separateColor == bumpy
				&& VK_Exec_PipelineTargetsMatch( vkExec.envPipelines[ i ].target, target ) ) {
			return vkExec.envPipelines[ i ].pipeline;
		}
	}
	const VkShaderModule vertModule = bumpy ? vkExec.bumpyEnvVertModule : vkExec.envVertModule;
	const VkShaderModule fragModule = bumpy ? vkExec.bumpyEnvFragModule : vkExec.envFragModule;
	if ( vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	if ( vkExec.numEnvPipelines >= VK_MAX_ENV_PIPELINES ) {
		common->Warning( "Vulkan: environment pipeline cache exhausted" );
		return vkExec.envPipelines[ 0 ].pipeline;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	if ( bumpy ) {
		VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );
	} else {
		memset( &binding, 0, sizeof( binding ) );
		binding.stride = sizeof( idDrawVert );
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		memset( attrs, 0, sizeof( attrs ) );
		attrs[ 0 ].location = 0;
		attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
		attrs[ 1 ].location = 1;
		attrs[ 1 ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, normal );
		attrs[ 2 ].location = 2;
		attrs[ 2 ].format = VK_FORMAT_R8G8B8A8_UNORM;
		attrs[ 2 ].offset = (uint32_t)offsetof( idDrawVert, color );
		memset( &vertexInput, 0, sizeof( vertexInput ) );
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = 3;
		vertexInput.pVertexAttributeDescriptions = attrs;
	}

	const VkPipelineLayout layout = bumpy
			? vkExec.interactionPipelineLayout : vkExec.pipelineLayout;
	VkPipeline pipeline = VK_Exec_CreatePipeline( vertModule, fragModule,
			&vertexInput, pipelineBits, layout, false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkExec.envPipelines[ vkExec.numEnvPipelines ].stateBits = pipelineBits;
	vkExec.envPipelines[ vkExec.numEnvPipelines ].separateColor = bumpy;
	vkExec.envPipelines[ vkExec.numEnvPipelines ].target = target;
	vkExec.envPipelines[ vkExec.numEnvPipelines ].pipeline = pipeline;
	vkExec.numEnvPipelines++;
	return pipeline;
}

// full idDrawVert vertex input shared by the interaction pipelines:
// xyz@0, color ubyte4@12, normal@16, tangent0@32, tangent1@44, st@56
// (64-byte stride, one binding)
static bool VK_Exec_TranslateInteractionVertexAttribute(
		const rendererVertexAttributeDesc_t &source, std::uint32_t &location,
		VkFormat &format ) {
	switch ( source.semantic ) {
	case RENDERER_VERTEX_SEMANTIC_POSITION:
		location = 0;
		break;
	case RENDERER_VERTEX_SEMANTIC_COLOR0:
		location = 1;
		break;
	case RENDERER_VERTEX_SEMANTIC_NORMAL:
		location = 2;
		break;
	case RENDERER_VERTEX_SEMANTIC_TANGENT0:
		location = 3;
		break;
	case RENDERER_VERTEX_SEMANTIC_TANGENT1:
		location = 4;
		break;
	case RENDERER_VERTEX_SEMANTIC_TEXCOORD0:
		location = 5;
		break;
	default:
		return false;
	}
	switch ( source.format ) {
	case RENDERER_VERTEX_FORMAT_FLOAT32X2:
		format = VK_FORMAT_R32G32_SFLOAT;
		return true;
	case RENDERER_VERTEX_FORMAT_FLOAT32X3:
		format = VK_FORMAT_R32G32B32_SFLOAT;
		return true;
	case RENDERER_VERTEX_FORMAT_FLOAT32X4:
		format = VK_FORMAT_R32G32B32A32_SFLOAT;
		return true;
	case RENDERER_VERTEX_FORMAT_UNORM8X4:
		format = VK_FORMAT_R8G8B8A8_UNORM;
		return true;
	case RENDERER_VERTEX_FORMAT_UINT32X4:
		format = VK_FORMAT_R32G32B32A32_UINT;
		return true;
	default:
		return false;
	}
}

static void VK_Exec_InteractionVertexInput( VkVertexInputBindingDescription &binding,
		VkVertexInputAttributeDescription attrs[ 6 ], VkPipelineVertexInputStateCreateInfo &vertexInput ) {
	memset( &binding, 0, sizeof( binding ) );
	memset( attrs, 0, 6 * sizeof( attrs[ 0 ] ) );
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	const rendererVertexLayoutDesc_t &layout = RendererContracts_LegacyDrawVertLayout();
	if ( !RendererContracts_ValidateVertexLayout( layout )
			|| layout.bindingCount != 1 || layout.attributeCount != 6 ) {
		common->Warning( "Vulkan: shared idDrawVert layout contract is invalid" );
		return;
	}
	binding.binding = layout.bindings[ 0 ].binding;
	binding.stride = layout.bindings[ 0 ].stride;
	binding.inputRate = layout.bindings[ 0 ].inputRate == RENDERER_VERTEX_RATE_PER_INSTANCE
		? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
	for ( std::uint32_t i = 0; i < layout.attributeCount; ++i ) {
		const rendererVertexAttributeDesc_t &source = layout.attributes[ i ];
		std::uint32_t location = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		if ( !VK_Exec_TranslateInteractionVertexAttribute( source, location, format ) ) {
			common->Warning( "Vulkan: shared idDrawVert attribute cannot be translated" );
			return;
		}
		attrs[ i ].location = location;
		attrs[ i ].binding = source.binding;
		attrs[ i ].format = format;
		attrs[ i ].offset = source.offset;
	}
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = layout.attributeCount;
	vertexInput.pVertexAttributeDescriptions = attrs;
}

static VkPipeline VK_Exec_FindSpecialPipeline( vkSpecialPipelineKind_t kind,
		const vkPipelineTarget_t &target ) {
	for ( int i = 0; i < vkExec.numSpecialPipelines; i++ ) {
		if ( vkExec.specialPipelines[ i ].kind == kind
				&& VK_Exec_PipelineTargetsMatch( vkExec.specialPipelines[ i ].target, target ) ) {
			return vkExec.specialPipelines[ i ].pipeline;
		}
	}
	return VK_NULL_HANDLE;
}

static VkPipeline VK_Exec_StoreSpecialPipeline( vkSpecialPipelineKind_t kind,
		const vkPipelineTarget_t &target, VkPipeline pipeline ) {
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	if ( vkExec.numSpecialPipelines >= VK_MAX_SPECIAL_PIPELINES ) {
		common->Warning( "Vulkan: specialized pipeline cache exhausted" );
		vkDestroyPipeline( vkCtx.device, pipeline, NULL );
		return VK_NULL_HANDLE;
	}
	vkSpecialPipeline_t &entry = vkExec.specialPipelines[ vkExec.numSpecialPipelines++ ];
	entry.kind = kind;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

// interaction pipeline (Phase F1): full idDrawVert vertex input, fixed
// ONE/ONE additive blend (the only state the GL interaction batch ever
// uses), depth func/write and cull through the shared dynamic state.
// Lazily built and dropped with the other pipelines on format changes.
VkPipeline VK_Exec_InteractionPipeline( void ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( VK_SPECIAL_INTERACTION, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.interactionVertModule == VK_NULL_HANDLE || vkExec.interactionFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );

	return VK_Exec_StoreSpecialPipeline( VK_SPECIAL_INTERACTION, target,
			VK_Exec_CreatePipeline( vkExec.interactionVertModule, vkExec.interactionFragModule,
				&vertexInput, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE,
				vkExec.interactionPipelineLayout, false, false, target ) );
}

VkPipelineLayout VK_Exec_InteractionPipelineLayout( void ) {
	return vkExec.interactionPipelineLayout;
}

// shadow-receiving interaction variant (Phase F2a): same vertex input and
// additive blend, plus set 7 (atlas compare sampler + per-space shadow UBO)
VkPipeline VK_Exec_ShadowInteractionPipeline( void ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( VK_SPECIAL_SHADOW_INTERACTION, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.interactionShadowVertModule == VK_NULL_HANDLE || vkExec.interactionShadowFragModule == VK_NULL_HANDLE
			|| vkExec.shadowInteractionPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );

	return VK_Exec_StoreSpecialPipeline( VK_SPECIAL_SHADOW_INTERACTION, target,
			VK_Exec_CreatePipeline( vkExec.interactionShadowVertModule, vkExec.interactionShadowFragModule,
				&vertexInput, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE,
				vkExec.shadowInteractionPipelineLayout, false, false, target ) );
}

VkPipelineLayout VK_Exec_ShadowInteractionPipelineLayout( void ) {
	return vkExec.shadowInteractionPipelineLayout;
}

// point-shadow-receiving interaction variant (Phase F2b): identical to the
// projected variant except the shaders sample samplerCubeShadow + samplerCube
// through set 7's compare/raw bindings (the layout is view-type agnostic)
VkPipeline VK_Exec_PointShadowInteractionPipeline( void ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( VK_SPECIAL_POINT_SHADOW_INTERACTION, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.interactionShadowPointVertModule == VK_NULL_HANDLE || vkExec.interactionShadowPointFragModule == VK_NULL_HANDLE
			|| vkExec.shadowInteractionPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );

	return VK_Exec_StoreSpecialPipeline( VK_SPECIAL_POINT_SHADOW_INTERACTION, target,
			VK_Exec_CreatePipeline( vkExec.interactionShadowPointVertModule,
				vkExec.interactionShadowPointFragModule,
				&vertexInput, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE,
				vkExec.shadowInteractionPipelineLayout, false, false, target ) );
}

/*
====================
r_shadowMapDebugOverlay pipelines

Three variants over one vertex stage that builds its quad from
gl_VertexIndex, so the overlay needs no vertex ring, no index ring, and no
geometry state of its own. The two panel variants differ only in the view
type they declare for set 0 binding 2 (the raw shadow sampler), which the
shadow set layout leaves open; the text variant declares no sampler so the
"no map" readout can draw with nothing bound. All three are SRC_ALPHA /
ONE_MINUS_SRC_ALPHA over the live scene, matching the GL overlay's
GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA.
====================
*/
static VkPipeline VK_Exec_ShadowOverlayPipelineVariant( vkSpecialPipelineKind_t kind,
		VkShaderModule fragModule ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( kind, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.shadowOverlayVertModule == VK_NULL_HANDLE
			|| fragModule == VK_NULL_HANDLE
			|| vkExec.shadowOverlayPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	// no vertex input at all: the quad corners come from gl_VertexIndex
	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	return VK_Exec_StoreSpecialPipeline( kind, target,
			VK_Exec_CreatePipeline( vkExec.shadowOverlayVertModule, fragModule,
				&vertexInput, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA,
				vkExec.shadowOverlayPipelineLayout, false, false, target ) );
}

VkPipeline VK_Exec_ShadowOverlayPanelPipeline( bool pointLight ) {
	return pointLight
		? VK_Exec_ShadowOverlayPipelineVariant( VK_SPECIAL_SHADOW_OVERLAY_POINT_PANEL,
			vkExec.shadowOverlayPointFragModule )
		: VK_Exec_ShadowOverlayPipelineVariant( VK_SPECIAL_SHADOW_OVERLAY_PANEL,
			vkExec.shadowOverlayFragModule );
}

VkPipeline VK_Exec_ShadowOverlayTextPipeline( void ) {
	return VK_Exec_ShadowOverlayPipelineVariant( VK_SPECIAL_SHADOW_OVERLAY_TEXT,
			vkExec.shadowOverlayTextFragModule );
}

VkPipelineLayout VK_Exec_ShadowOverlayPipelineLayout( void ) {
	return vkExec.shadowOverlayPipelineLayout;
}

// position + st off the idDrawVert stream, shared by the caster pipelines
// (perforated casters alpha-test; opaque draws ignore the texcoord)
static void VK_Exec_CasterVertexInput( VkVertexInputBindingDescription &binding,
		VkVertexInputAttributeDescription attrs[ 2 ], VkPipelineVertexInputStateCreateInfo &vertexInput ) {
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	memset( attrs, 0, 2 * sizeof( attrs[ 0 ] ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, st );

	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;
}

// Native replacements for Quake 4's closed ARB newStage program set.  Heat
// haze uses the compact position/ST stream; the AndVertex variant also reads
// the packed primary color from idDrawVert.
static VkPipeline VK_Exec_GetProgramPipeline( vkMaterialProgramFamily_t family,
		int stateBits, bool separateColor ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numProgramPipelines; i++ ) {
		const vkProgramPipeline_t &entry = vkExec.programPipelines[ i ];
		if ( entry.family == family && entry.stateBits == pipelineBits
				&& entry.separateColor == separateColor
				&& VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return entry.pipeline;
		}
	}
	if ( vkExec.numProgramPipelines >= VK_MAX_PROGRAM_PIPELINES ) {
		common->Warning( "Vulkan: material-program pipeline cache exhausted" );
		return VK_NULL_HANDLE;
	}

	VkShaderModule vertModule = VK_NULL_HANDLE;
	VkShaderModule fragModule = VK_NULL_HANDLE;
	bool vertexColorVariant = false;
	bool fullVertexInput = false;
	switch ( family ) {
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE:
			vertModule = vkExec.heatHazeVertModule;
			fragModule = vkExec.heatHazeFragModule;
			break;
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK:
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_GRAY_WITH_MASK:
			vertModule = vkExec.heatHazeVertModule;
			fragModule = vkExec.heatHazeMaskFragModule;
			break;
		case VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX:
			vertModule = vkExec.heatHazeVertexVertModule;
			fragModule = vkExec.heatHazeMaskVertexFragModule;
			vertexColorVariant = true;
			break;
		case VK_MATERIAL_PROGRAM_FAMILY_MONOCHROME:
			vertModule = vkExec.monochromeVertModule;
			fragModule = vkExec.monochromeFragModule;
			break;
		case VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS:
			vertModule = vkExec.refractiveGlassVertModule;
			fragModule = vkExec.refractiveGlassFragModule;
			fullVertexInput = true;
			break;
		default:
			return VK_NULL_HANDLE;
	}
	if ( vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription bindings[ 2 ];
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( bindings, 0, sizeof( bindings ) );
	memset( attrs, 0, sizeof( attrs ) );
	if ( fullVertexInput ) {
		VK_Exec_InteractionVertexInput( bindings[ 0 ], attrs, vertexInput );
	} else {
		VK_Exec_CasterVertexInput( bindings[ 0 ], attrs, vertexInput );
	}
	if ( vertexColorVariant && !fullVertexInput ) {
		attrs[ 2 ].location = 2;
		attrs[ 2 ].binding = separateColor ? 1 : 0;
		attrs[ 2 ].format = VK_FORMAT_R8G8B8A8_UNORM;
		attrs[ 2 ].offset = separateColor ? 0 : (uint32_t)offsetof( idDrawVert, color );
		vertexInput.vertexAttributeDescriptionCount = 3;
		if ( separateColor ) {
			bindings[ 1 ].binding = 1;
			bindings[ 1 ].stride = 4;
			bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vertexInput.vertexBindingDescriptionCount = 2;
			vertexInput.pVertexBindingDescriptions = bindings;
		}
	}

	VkPipeline pipeline = VK_Exec_CreatePipeline( vertModule, fragModule,
			&vertexInput, pipelineBits, vkExec.interactionPipelineLayout,
			false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkProgramPipeline_t &entry =
			vkExec.programPipelines[ vkExec.numProgramPipelines++ ];
	entry.family = family;
	entry.stateBits = pipelineBits;
	entry.separateColor = separateColor;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

// Quake 4 GLSL material stages read family-specific subsets of idDrawVert.
// Their program-family key is kept disjoint from the legacy ARB family
// values in the shared cache.
static bool VK_Exec_GLSLFamilyUsesVertexColor(
		vkGLSLProgramFamily_t family ) {
	switch ( family ) {
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT:
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE:
		case VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING:
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2:
		case VK_GLSL_PROGRAM_FAMILY_MULTIPLY_BLEND:
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE:
			return true;
		default:
			return false;
	}
}

static VkPipeline VK_Exec_GetGLSLMaterialPipeline( vkGLSLProgramFamily_t family,
		int stateBits, bool separateColor ) {
	if ( family <= VK_GLSL_PROGRAM_FAMILY_UNKNOWN
			|| family >= VK_GLSL_PROGRAM_FAMILY_COUNT ) {
		return VK_NULL_HANDLE;
	}
	const bool usesVertexColor =
			VK_Exec_GLSLFamilyUsesVertexColor( family );
	separateColor = separateColor && usesVertexColor;
	const int programKey = 0x100 + (int)family;
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numProgramPipelines; i++ ) {
		const vkProgramPipeline_t &entry = vkExec.programPipelines[ i ];
		if ( entry.family == programKey && entry.stateBits == pipelineBits
				&& entry.separateColor == separateColor
				&& VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return entry.pipeline;
		}
	}
	if ( vkExec.numProgramPipelines >= VK_MAX_PROGRAM_PIPELINES ) {
		common->Warning( "Vulkan: material-program pipeline cache exhausted" );
		return VK_NULL_HANDLE;
	}

	const VkShaderModule vertModule = vkExec.glslMaterialVertModules[ family ];
	const VkShaderModule fragModule = vkExec.glslMaterialFragModules[ family ];
	if ( vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription bindings[ 2 ];
	memset( bindings, 0, sizeof( bindings ) );
	bindings[ 0 ].binding = 0;
	bindings[ 0 ].stride = sizeof( idDrawVert );
	bindings[ 0 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[ 1 ].binding = 1;
	bindings[ 1 ].stride = 4;
	bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 6 ];
	memset( attrs, 0, sizeof( attrs ) );
	int numAttributes = 0;
	attrs[ numAttributes ].location = 0;
	attrs[ numAttributes ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ numAttributes ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	numAttributes++;
	if ( usesVertexColor ) {
		attrs[ numAttributes ].location = 1;
		attrs[ numAttributes ].binding = separateColor ? 1 : 0;
		attrs[ numAttributes ].format = VK_FORMAT_R8G8B8A8_UNORM;
		attrs[ numAttributes ].offset =
				separateColor ? 0 : (uint32_t)offsetof( idDrawVert, color );
		numAttributes++;
	}
	if ( family == VK_GLSL_PROGRAM_FAMILY_WATER ) {
		attrs[ numAttributes ].location = 2;
		attrs[ numAttributes ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ numAttributes ].offset =
				(uint32_t)offsetof( idDrawVert, normal );
		numAttributes++;
		attrs[ numAttributes ].location = 3;
		attrs[ numAttributes ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ numAttributes ].offset =
				(uint32_t)offsetof( idDrawVert, tangents );
		numAttributes++;
		attrs[ numAttributes ].location = 4;
		attrs[ numAttributes ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ numAttributes ].offset =
				(uint32_t)( offsetof( idDrawVert, tangents ) + sizeof( idVec3 ) );
		numAttributes++;
	} else if ( family == VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE ) {
		attrs[ numAttributes ].location = 2;
		attrs[ numAttributes ].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[ numAttributes ].offset =
				(uint32_t)offsetof( idDrawVert, normal );
		numAttributes++;
	}
	attrs[ numAttributes ].location = 5;
	attrs[ numAttributes ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ numAttributes ].offset = (uint32_t)offsetof( idDrawVert, st );
	numAttributes++;

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = separateColor ? 2 : 1;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = (uint32_t)numAttributes;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipeline pipeline = VK_Exec_CreatePipeline( vertModule, fragModule,
			&vertexInput, pipelineBits, vkExec.interactionPipelineLayout,
			false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkProgramPipeline_t &entry =
			vkExec.programPipelines[ vkExec.numProgramPipelines++ ];
	entry.family = programKey;
	entry.stateBits = pipelineBits;
	entry.separateColor = separateColor;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

static VkPipeline VK_Exec_GetGlassWarpPipeline( int stateBits ) {
	static const int programKey = 0x80;
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	for ( int i = 0; i < vkExec.numProgramPipelines; i++ ) {
		const vkProgramPipeline_t &entry = vkExec.programPipelines[ i ];
		if ( entry.family == programKey && entry.stateBits == pipelineBits
				&& !entry.separateColor
				&& VK_Exec_PipelineTargetsMatch( entry.target, target ) ) {
			return entry.pipeline;
		}
	}
	if ( vkExec.glassWarpVertModule == VK_NULL_HANDLE
			|| vkExec.glassWarpFragModule == VK_NULL_HANDLE
			|| vkExec.numProgramPipelines >= VK_MAX_PROGRAM_PIPELINES ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	memset( &binding, 0, sizeof( binding ) );
	binding.binding = 0;
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attrs[ 2 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	attrs[ 1 ].location = 5;
	attrs[ 1 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, st );
	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;

	const VkPipeline pipeline = VK_Exec_CreatePipeline(
			vkExec.glassWarpVertModule, vkExec.glassWarpFragModule,
			&vertexInput, pipelineBits, vkExec.interactionPipelineLayout,
			false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkProgramPipeline_t &entry =
			vkExec.programPipelines[ vkExec.numProgramPipelines++ ];
	entry.family = programKey;
	entry.stateBits = pipelineBits;
	entry.separateColor = false;
	entry.target = target;
	entry.pipeline = pipeline;
	return pipeline;
}

// depth-only shadow-map caster (Phase F2a): zero color attachments,
// single-image layout (slot 0 = alpha map)
VkPipeline VK_Exec_CasterPipeline( void ) {
	if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
		return vkExec.casterPipeline;
	}
	if ( vkExec.casterVertModule == VK_NULL_HANDLE || vkExec.casterFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 2 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_CasterVertexInput( binding, attrs, vertexInput );

	vkPipelineTarget_t target;
	target.colorFormat = VK_FORMAT_UNDEFINED;
	target.depthFormat = vkCtx.shadowDepthFormat;
	target.stencilFormat = vkCtx.shadowDepthHasStencil ? vkCtx.shadowDepthFormat : VK_FORMAT_UNDEFINED;
	target.samples = VK_SAMPLE_COUNT_1_BIT;
	vkExec.casterPipeline = VK_Exec_CreatePipeline( vkExec.casterVertModule, vkExec.casterFragModule,
			&vertexInput, 0, vkExec.pipelineLayout, true, false, target );
	return vkExec.casterPipeline;
}

// depth-only point cube-face caster (Phase F2b): same shape as the atlas
// caster, radial-depth shaders. Retain near clipping to avoid false radial
// depths from triangles crossing a cube face's origin. The point projection
// uses a tiny positive near plane independent of the radial depth precision.
VkPipeline VK_Exec_PointCasterPipeline( void ) {
	if ( vkExec.pointCasterPipeline != VK_NULL_HANDLE ) {
		return vkExec.pointCasterPipeline;
	}
	if ( vkExec.pointCasterVertModule == VK_NULL_HANDLE || vkExec.pointCasterFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 2 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_CasterVertexInput( binding, attrs, vertexInput );

	vkPipelineTarget_t target;
	target.colorFormat = VK_FORMAT_UNDEFINED;
	target.depthFormat = vkCtx.shadowDepthFormat;
	target.stencilFormat = vkCtx.shadowDepthHasStencil ? vkCtx.shadowDepthFormat : VK_FORMAT_UNDEFINED;
	target.samples = VK_SAMPLE_COUNT_1_BIT;
	vkExec.pointCasterPipeline = VK_Exec_CreatePipeline( vkExec.pointCasterVertModule, vkExec.pointCasterFragModule,
			&vertexInput, 0, vkExec.pipelineLayout, true, false, target );
	return vkExec.pointCasterPipeline;
}

// stencil shadow volume pipeline (Phase G1): the shadowCache_t vec4 stream
// (one attribute, stride 16), color writes masked off (GLS_COLORMASK |
// GLS_ALPHAMASK — a pipeline-level variant), blend irrelevant. Depth
// LEQUAL/no-write, the two-sided wrap-op stencil sequencing, and the
// r_shadowPolygonFactor/-Offset bias all ride the shared dynamic state.
// Uses the base 128B-push layout; the shaders bind no descriptor sets.
VkPipeline VK_Exec_StencilShadowPipeline( void ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( VK_SPECIAL_STENCIL_SHADOW, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.stencilShadowVertModule == VK_NULL_HANDLE || vkExec.stencilShadowFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( shadowCache_t );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attr;
	memset( &attr, 0, sizeof( attr ) );
	attr.location = 0;
	attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attr.offset = 0;

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 1;
	vertexInput.pVertexAttributeDescriptions = &attr;

	return VK_Exec_StoreSpecialPipeline( VK_SPECIAL_STENCIL_SHADOW, target,
			VK_Exec_CreatePipeline( vkExec.stencilShadowVertModule, vkExec.stencilShadowFragModule,
				&vertexInput, 0, vkExec.pipelineLayout, false, true, target ) );
}

VkPipelineLayout VK_Exec_BasePipelineLayout( void ) {
	return vkExec.pipelineLayout;
}

// position-only idDrawVert stream shared by the fog/blend pipelines (the
// GL fog/blend draws are fixed-function glVertexPointer(xyz)-only —
// RB_T_RenderTriangleSurface / RB_T_BlendLight)
static void VK_Exec_PositionVertexInput( VkVertexInputBindingDescription &binding,
		VkVertexInputAttributeDescription &attr, VkPipelineVertexInputStateCreateInfo &vertexInput ) {
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	memset( &attr, 0, sizeof( attr ) );
	attr.location = 0;
	attr.format = VK_FORMAT_R32G32B32_SFLOAT;
	attr.offset = (uint32_t)offsetof( idDrawVert, xyz );

	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 1;
	vertexInput.pVertexAttributeDescriptions = &attr;
}

// fog light pipeline (Phase G2): fixed SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend
// (the only state RB_FogPass ever uses), position-only input; depth EQUAL
// for the surface chains and LEQUAL for the frustumTris cap ride the
// shared dynamic state. The fog shaders bind only sets 0/1 of the
// fog/blend layout.
VkPipeline VK_Exec_FogPipeline( void ) {
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();
	VkPipeline cached = VK_Exec_FindSpecialPipeline( VK_SPECIAL_FOG, target );
	if ( cached != VK_NULL_HANDLE ) {
		return cached;
	}
	if ( vkExec.fogVertModule == VK_NULL_HANDLE || vkExec.fogFragModule == VK_NULL_HANDLE
			|| vkExec.fogBlendPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attr;
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_PositionVertexInput( binding, attr, vertexInput );

	return VK_Exec_StoreSpecialPipeline( VK_SPECIAL_FOG, target,
			VK_Exec_CreatePipeline( vkExec.fogVertModule, vkExec.fogFragModule,
				&vertexInput, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA,
				vkExec.fogBlendPipelineLayout, false, false, target ) );
}

// blend-light pipelines (Phase G2): one per light-stage blend combination —
// GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL ),
// where only the blend factors are pipeline-level state
VkPipeline VK_Exec_BlendLightPipeline( int stateBits ) {
	const int pipelineBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_COLORMASK | GLS_ALPHAMASK );
	const vkPipelineTarget_t target = VK_Exec_CurrentPipelineTarget();

	for ( int i = 0; i < vkExec.numBlendLightPipelines; i++ ) {
		if ( vkExec.blendLightPipelines[ i ].stateBits == pipelineBits
				&& VK_Exec_PipelineTargetsMatch( vkExec.blendLightPipelines[ i ].target, target ) ) {
			return vkExec.blendLightPipelines[ i ].pipeline;
		}
	}
	if ( vkExec.blendLightVertModule == VK_NULL_HANDLE || vkExec.blendLightFragModule == VK_NULL_HANDLE
			|| vkExec.fogBlendPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	if ( vkExec.numBlendLightPipelines >= VK_MAX_BLEND_LIGHT_PIPELINES ) {
		common->Warning( "Vulkan: blend-light pipeline cache exhausted" );
		// The requested blend/color-mask key has no exact pipeline.  Returning
		// an arbitrary cached variant would corrupt the sealed light-stage
		// contract; shared preflight must reject the complete view instead.
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attr;
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_PositionVertexInput( binding, attr, vertexInput );

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.blendLightVertModule, vkExec.blendLightFragModule,
			&vertexInput, pipelineBits, vkExec.fogBlendPipelineLayout, false, false, target );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkExec.blendLightPipelines[ vkExec.numBlendLightPipelines ].stateBits = pipelineBits;
	vkExec.blendLightPipelines[ vkExec.numBlendLightPipelines ].target = target;
	vkExec.blendLightPipelines[ vkExec.numBlendLightPipelines ].pipeline = pipeline;
	vkExec.numBlendLightPipelines++;
	return pipeline;
}

VkPipelineLayout VK_Exec_FogBlendPipelineLayout( void ) {
	return vkExec.fogBlendPipelineLayout;
}

/*
====================
Descriptors
====================
*/
static VkDescriptorSet VK_GuiExecutor_GetImageDescriptor( unsigned int texnum ) {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL || entry->view == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	vkDescriptorCacheEntry_t &cached = vkExec.descriptorCache[ texnum ];
	if ( cached.set != VK_NULL_HANDLE && cached.generation == entry->generation ) {
		return cached.set;
	}

	// A generation bump can occur mid-frame when a feedback image is resized
	// or re-backed. Already-recorded draws may still reference the old set, so
	// allocate a fresh one and retire the old set with this recording slot.
	if ( cached.set != VK_NULL_HANDLE ) {
		if ( vkExec.numRetiredSets[ vkExec.frameSlot ] >= VK_MAX_RETIRED_SETS ) {
			static bool warnedRetiredSetOverflow = false;
			if ( !warnedRetiredSetOverflow ) {
				warnedRetiredSetOverflow = true;
				common->Warning( "Vulkan: descriptor retirement budget exhausted; image draw skipped" );
			}
			return VK_NULL_HANDLE;
		}
		vkExec.retiredSets[ vkExec.frameSlot ]
				[ vkExec.numRetiredSets[ vkExec.frameSlot ]++ ] = cached.set;
		cached.set = VK_NULL_HANDLE;
	}

	if ( cached.set == VK_NULL_HANDLE ) {
		VkDescriptorSetAllocateInfo dsai;
		memset( &dsai, 0, sizeof( dsai ) );
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = vkExec.descriptorPool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &vkExec.setLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &cached.set ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: descriptor set allocation failed" );
			cached.set = VK_NULL_HANDLE;
			return VK_NULL_HANDLE;
		}
	}

	VkDescriptorImageInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sampler = entry->sampler;
	imageInfo.imageView = entry->view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write;
	memset( &write, 0, sizeof( write ) );
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = cached.set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );

	cached.generation = entry->generation;
	return cached.set;
}

/*
====================
VK_GuiExecutor_GetResidentImageDescriptor

The OpenGL backend's idImage::Bind path loads purged images on demand. Vulkan
binds descriptors directly, so preserve that residency contract here before
looking up the image's device handle. This is required while a level load is
active: BeginLevelLoad deliberately purges non-persistent GUI images and the
loading GUI must be able to draw them before EndLevelLoad reloads map media.
====================
*/
static VkDescriptorSet VK_GuiExecutor_GetResidentImageDescriptor( idImage *image ) {
	if ( image == NULL ) {
		return VK_NULL_HANDLE;
	}
	if ( !image->IsLoaded() ) {
		image->ActuallyLoadImage( true );
	}
	if ( !image->IsLoaded() ) {
		return VK_NULL_HANDLE;
	}
	return VK_GuiExecutor_GetImageDescriptor( image->GetDeviceHandle() );
}

/*
====================
VK_GpuSkinning_DestroyResources
====================
*/
static void VK_GpuSkinning_DestroyResources( void ) {
	vkExec.gpuSkinningAvailable = false;
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		vkExec.gpuSkinningPipeline = VK_NULL_HANDLE;
		vkExec.gpuSkinningPipelineLayout = VK_NULL_HANDLE;
		vkExec.gpuSkinningSetLayout = VK_NULL_HANDLE;
		vkExec.gpuSkinningModule = VK_NULL_HANDLE;
		memset( vkExec.gpuSkinningSets, 0, sizeof( vkExec.gpuSkinningSets ) );
		return;
	}
	if ( vkExec.gpuSkinningPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.gpuSkinningPipeline, NULL );
		vkExec.gpuSkinningPipeline = VK_NULL_HANDLE;
	}
	if ( vkExec.gpuSkinningPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.gpuSkinningPipelineLayout, NULL );
		vkExec.gpuSkinningPipelineLayout = VK_NULL_HANDLE;
	}
	if ( vkExec.gpuSkinningSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.gpuSkinningSetLayout, NULL );
		vkExec.gpuSkinningSetLayout = VK_NULL_HANDLE;
	}
	if ( vkExec.gpuSkinningModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.gpuSkinningModule, NULL );
		vkExec.gpuSkinningModule = VK_NULL_HANDLE;
	}
	memset( vkExec.gpuSkinningSets, 0, sizeof( vkExec.gpuSkinningSets ) );
}

/*
====================
VK_GpuSkinning_InitResources

GPU skinning is optional. Any unsupported limit or object-creation failure
leaves the existing CPU deformation/cache path authoritative without taking
down the Vulkan renderer.
====================
*/
static bool VK_GpuSkinning_InitResources( void ) {
	VK_GpuSkinning_DestroyResources();

	const VkPhysicalDeviceLimits &limits = vkCtx.deviceProperties.limits;
	if ( sizeof( idDrawVert ) != 64
			|| offsetof( idDrawVert, xyz ) != 0
			|| offsetof( idDrawVert, color ) != 12
			|| offsetof( idDrawVert, normal ) != 16
			|| offsetof( idDrawVert, color2 ) != 28
			|| offsetof( idDrawVert, tangents ) != 32
			|| offsetof( idDrawVert, st ) != 56
			|| sizeof( gpuSkinningVertex_t ) != 32
			|| offsetof( gpuSkinningVertex_t, jointIndices ) != 0
			|| offsetof( gpuSkinningVertex_t, jointWeights ) != 16
			|| limits.maxStorageBufferRange < static_cast<uint32_t>( VK_VERTEX_RING_BYTES )
			|| limits.maxPushConstantsSize < sizeof( vkGpuSkinningPushConstants_t )
			|| limits.maxComputeWorkGroupInvocations < VK_GPU_SKINNING_WORKGROUP_SIZE
			|| limits.maxComputeWorkGroupSize[ 0 ] < VK_GPU_SKINNING_WORKGROUP_SIZE ) {
		common->Warning( "Vulkan: GPU skinning unavailable because device/ABI limits are insufficient" );
		return false;
	}

	VkShaderModuleCreateInfo smci;
	memset( &smci, 0, sizeof( smci ) );
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = vk_gpu_skinning_comp_spv_size;
	smci.pCode = reinterpret_cast<const uint32_t *>( vk_gpu_skinning_comp_spv );
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
			&vkExec.gpuSkinningModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GPU skinning compute shader module creation failed" );
		VK_GpuSkinning_DestroyResources();
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[ 4 ];
	memset( bindings, 0, sizeof( bindings ) );
	for ( uint32_t binding = 0; binding < 4; ++binding ) {
		bindings[ binding ].binding = binding;
		bindings[ binding ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[ binding ].descriptorCount = 1;
		bindings[ binding ].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	VkDescriptorSetLayoutCreateInfo dslci;
	memset( &dslci, 0, sizeof( dslci ) );
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 4;
	dslci.pBindings = bindings;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL,
			&vkExec.gpuSkinningSetLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GPU skinning descriptor layout creation failed" );
		VK_GpuSkinning_DestroyResources();
		return false;
	}

	VkPushConstantRange pushRange;
	memset( &pushRange, 0, sizeof( pushRange ) );
	pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushRange.size = sizeof( vkGpuSkinningPushConstants_t );
	VkPipelineLayoutCreateInfo plci;
	memset( &plci, 0, sizeof( plci ) );
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &vkExec.gpuSkinningSetLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pushRange;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL,
			&vkExec.gpuSkinningPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GPU skinning pipeline layout creation failed" );
		VK_GpuSkinning_DestroyResources();
		return false;
	}

	VkPipelineShaderStageCreateInfo stage;
	memset( &stage, 0, sizeof( stage ) );
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = vkExec.gpuSkinningModule;
	stage.pName = "main";
	VkComputePipelineCreateInfo cpci;
	memset( &cpci, 0, sizeof( cpci ) );
	cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	cpci.stage = stage;
	cpci.layout = vkExec.gpuSkinningPipelineLayout;
	if ( vkCreateComputePipelines( vkCtx.device, vkCtx.pipelineCache, 1, &cpci, NULL,
			&vkExec.gpuSkinningPipeline ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GPU skinning compute pipeline creation failed" );
		VK_GpuSkinning_DestroyResources();
		return false;
	}

	for ( int slot = 0; slot < VK_FRAMES_IN_FLIGHT; ++slot ) {
		VkDescriptorSetAllocateInfo dsai;
		memset( &dsai, 0, sizeof( dsai ) );
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = vkExec.descriptorPool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &vkExec.gpuSkinningSetLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai,
				&vkExec.gpuSkinningSets[ slot ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: GPU skinning frame descriptor allocation failed" );
			VK_GpuSkinning_DestroyResources();
			return false;
		}

		VkDescriptorBufferInfo bufferInfos[ 4 ];
		VkWriteDescriptorSet writes[ 4 ];
		memset( bufferInfos, 0, sizeof( bufferInfos ) );
		memset( writes, 0, sizeof( writes ) );
		for ( uint32_t binding = 0; binding < 4; ++binding ) {
			bufferInfos[ binding ].buffer = vkExec.vertexRings[ slot ].buffer;
			bufferInfos[ binding ].range = static_cast<VkDeviceSize>(
					vkExec.vertexRings[ slot ].capacity );
			writes[ binding ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[ binding ].dstSet = vkExec.gpuSkinningSets[ slot ];
			writes[ binding ].dstBinding = binding;
			writes[ binding ].descriptorCount = 1;
			writes[ binding ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[ binding ].pBufferInfo = &bufferInfos[ binding ];
		}
		vkUpdateDescriptorSets( vkCtx.device, 4, writes, 0, NULL );
	}

	vkExec.gpuSkinningAvailable = true;
	common->Printf( "Vulkan: GPU skinning compute initialized\n" );
	return true;
}

/*
====================
Init / Shutdown
====================
*/
void VK_GuiExecutor_Shutdown( void );

class vkGuiExecutorInitGuard_t {
public:
	vkGuiExecutorInitGuard_t() : committed( false ) {
	}

	~vkGuiExecutorInitGuard_t() {
		if ( !committed ) {
			VK_GuiExecutor_Shutdown();
		}
	}

	void Commit( void ) {
		committed = true;
	}

private:
	bool committed;
};

static bool VK_GuiExecutor_Init( void ) {
	if ( vkExec.initialized ) {
		return true;
	}
	memset( &vkExec, 0, sizeof( vkExec ) );
	vkGuiExecutorInitGuard_t initGuard;
	if ( vkCtx.deviceProperties.limits.maxUniformBufferRange
				< VK_SHADOW_UNIFORM_SLICE_BYTES
			|| VK_Exec_UniformSliceAlignment( VK_UNIFORM_SLICE_BYTES ) == 0
			|| VK_Exec_UniformSliceAlignment(
					VK_SHADOW_UNIFORM_SLICE_BYTES ) == 0 ) {
		common->Warning(
				"Vulkan: dynamic uniform limits cannot represent shadow receiver blocks" );
		return false;
	}

	VkShaderModuleCreateInfo smci;
	memset( &smci, 0, sizeof( smci ) );
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = vk_gui_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_gui_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.vertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GUI vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_gui_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_gui_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.fragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GUI fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_screen_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_screen_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.screenVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: screen vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_screen_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_screen_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.screenFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: screen fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_temporal_resolve_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_temporal_resolve_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
			&vkExec.temporalResolveVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: temporal resolve vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_temporal_resolve_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_temporal_resolve_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
			&vkExec.temporalResolveFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: temporal resolve fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_sky_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_sky_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.skyVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: sky vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_sky_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_sky_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.skyFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: sky fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_environment_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_environment_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.envVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: environment vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_environment_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_environment_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.envFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: environment fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_bumpy_environment_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_bumpy_environment_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.bumpyEnvVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: bumpy-environment vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_bumpy_environment_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_bumpy_environment_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.bumpyEnvFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: bumpy-environment fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_heathaze_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_heathaze_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.heatHazeVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: heat-haze vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_heathaze_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_heathaze_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.heatHazeFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: heat-haze fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_heathaze_mask_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_heathaze_mask_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.heatHazeMaskFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: masked heat-haze fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_heathaze_vertex_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_heathaze_vertex_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.heatHazeVertexVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: vertex-colored heat-haze vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_heathaze_mask_vertex_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_heathaze_mask_vertex_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.heatHazeMaskVertexFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: vertex-colored heat-haze fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_monochrome_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_monochrome_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.monochromeVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: monochrome compatibility vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_monochrome_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_monochrome_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.monochromeFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: monochrome compatibility fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_glasswarp_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_glasswarp_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.glassWarpVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: glass-warp vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_glasswarp_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_glasswarp_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.glassWarpFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: glass-warp fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_refractive_glass_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_refractive_glass_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
			&vkExec.refractiveGlassVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: refractive-glass vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_refractive_glass_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_refractive_glass_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
			&vkExec.refractiveGlassFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: refractive-glass fragment shader module creation failed" );
		return false;
	}

	struct embeddedMaterialProgram_t {
		vkGLSLProgramFamily_t family;
		const unsigned char *vertCode;
		unsigned int vertSize;
		const unsigned char *fragCode;
		unsigned int fragSize;
		const char *name;
	};
	static const embeddedMaterialProgram_t materialPrograms[] = {
		{ VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT,
			vk_material_displacement_vert_spv, vk_material_displacement_vert_spv_size,
			vk_material_displacement_frag_spv, vk_material_displacement_frag_spv_size,
			"Displacement" },
		{ VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE,
			vk_material_displacement_two_stage_vert_spv, vk_material_displacement_two_stage_vert_spv_size,
			vk_material_displacement_two_stage_frag_spv, vk_material_displacement_two_stage_frag_spv_size,
			"DisplacementTwoStage" },
		{ VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING,
			vk_material_ghost_pulling_vert_spv, vk_material_ghost_pulling_vert_spv_size,
			vk_material_ghost_pulling_frag_spv, vk_material_ghost_pulling_frag_spv_size,
			"GhostPulling" },
		{ VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2,
			vk_material_displacement2_vert_spv, vk_material_displacement2_vert_spv_size,
			vk_material_displacement2_frag_spv, vk_material_displacement2_frag_spv_size,
			"Displacement2" },
		{ VK_GLSL_PROGRAM_FAMILY_MULTIPLY_BLEND,
			vk_material_multiply_blend_vert_spv, vk_material_multiply_blend_vert_spv_size,
			vk_material_multiply_blend_frag_spv, vk_material_multiply_blend_frag_spv_size,
			"MultiplyBlend" },
		{ VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE,
			vk_material_displacement_cube_vert_spv, vk_material_displacement_cube_vert_spv_size,
			vk_material_displacement_cube_frag_spv, vk_material_displacement_cube_frag_spv_size,
			"DisplacementCube" },
		{ VK_GLSL_PROGRAM_FAMILY_SNIPER_STRETCH2,
			vk_material_sniper_stretch2_vert_spv, vk_material_sniper_stretch2_vert_spv_size,
			vk_material_sniper_stretch2_frag_spv, vk_material_sniper_stretch2_frag_spv_size,
			"SniperStretch2" },
		{ VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE,
			vk_material_depth_texture_vert_spv, vk_material_depth_texture_vert_spv_size,
			vk_material_depth_texture_frag_spv, vk_material_depth_texture_frag_spv_size,
			"DepthTexture" },
		{ VK_GLSL_PROGRAM_FAMILY_BLUR,
			vk_material_blur_vert_spv, vk_material_blur_vert_spv_size,
			vk_material_blur_frag_spv, vk_material_blur_frag_spv_size,
			"Blur" },
		{ VK_GLSL_PROGRAM_FAMILY_MEDLABS,
			vk_material_medlabs_vert_spv, vk_material_medlabs_vert_spv_size,
			vk_material_medlabs_frag_spv, vk_material_medlabs_frag_spv_size,
			"MedLabs" },
		{ VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE2,
			vk_material_depth_texture2_vert_spv, vk_material_depth_texture2_vert_spv_size,
			vk_material_depth_texture2_frag_spv, vk_material_depth_texture2_frag_spv_size,
			"DepthTexture2" },
		{ VK_GLSL_PROGRAM_FAMILY_AL,
			vk_material_al_vert_spv, vk_material_al_vert_spv_size,
			vk_material_al_frag_spv, vk_material_al_frag_spv_size,
			"AL" },
		{ VK_GLSL_PROGRAM_FAMILY_WATER,
			vk_material_water_vert_spv, vk_material_water_vert_spv_size,
			vk_material_water_frag_spv, vk_material_water_frag_spv_size,
			"Water" },
		{ VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR,
			vk_material_depth_aware_blur_vert_spv, vk_material_depth_aware_blur_vert_spv_size,
			vk_material_depth_aware_blur_frag_spv, vk_material_depth_aware_blur_frag_spv_size,
			"depth-aware blur" },
		{ VK_GLSL_PROGRAM_FAMILY_SMAA_EDGE,
			vk_material_smaa_edge_vert_spv, vk_material_smaa_edge_vert_spv_size,
			vk_material_smaa_edge_frag_spv, vk_material_smaa_edge_frag_spv_size,
			"SMAA edge" },
		{ VK_GLSL_PROGRAM_FAMILY_SMAA_WEIGHTS,
			vk_material_smaa_weights_vert_spv, vk_material_smaa_weights_vert_spv_size,
			vk_material_smaa_weights_frag_spv, vk_material_smaa_weights_frag_spv_size,
			"SMAA weights" },
		{ VK_GLSL_PROGRAM_FAMILY_SMAA_BLEND,
			vk_material_smaa_blend_vert_spv, vk_material_smaa_blend_vert_spv_size,
			vk_material_smaa_blend_frag_spv, vk_material_smaa_blend_frag_spv_size,
			"SMAA blend" },
	};
	const int numMaterialPrograms =
			(int)( sizeof( materialPrograms ) / sizeof( materialPrograms[ 0 ] ) );
	for ( int i = 0; i < numMaterialPrograms; i++ ) {
		const embeddedMaterialProgram_t &program = materialPrograms[ i ];
		smci.codeSize = program.vertSize;
		smci.pCode = (const uint32_t *)program.vertCode;
		if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
				&vkExec.glslMaterialVertModules[ program.family ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: %s material vertex shader module creation failed",
					program.name );
			return false;
		}
		smci.codeSize = program.fragSize;
		smci.pCode = (const uint32_t *)program.fragCode;
		if ( vkCreateShaderModule( vkCtx.device, &smci, NULL,
				&vkExec.glslMaterialFragModules[ program.family ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: %s material fragment shader module creation failed",
					program.name );
			return false;
		}
	}
	smci.codeSize = vk_interaction_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_point_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_point_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowPointVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow interaction vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_point_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_point_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowPointFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow interaction fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_caster_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_caster_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.casterVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow caster vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_caster_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_caster_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.casterFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow caster fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_point_caster_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_point_caster_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.pointCasterVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow caster vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_point_caster_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_point_caster_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.pointCasterFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow caster fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_volume_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_volume_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.stencilShadowVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: stencil shadow vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_volume_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_volume_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.stencilShadowFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: stencil shadow fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_debug_overlay_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_debug_overlay_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.shadowOverlayVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow overlay vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_debug_overlay_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_debug_overlay_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.shadowOverlayFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow overlay panel fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_debug_overlay_point_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_debug_overlay_point_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.shadowOverlayPointFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow overlay point panel fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_debug_overlay_text_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_debug_overlay_text_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.shadowOverlayTextFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow overlay text fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_fog_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_fog_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.fogVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: fog vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_fog_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_fog_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.fogFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: fog fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_blend_light_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_blend_light_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.blendLightVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: blend light vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_blend_light_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_blend_light_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.blendLightFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: blend light fragment shader module creation failed" );
		return false;
	}

	VkDescriptorSetLayoutBinding bindingInfo;
	memset( &bindingInfo, 0, sizeof( bindingInfo ) );
	bindingInfo.binding = 0;
	bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindingInfo.descriptorCount = 1;
	bindingInfo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci;
	memset( &dslci, 0, sizeof( dslci ) );
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings = &bindingInfo;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.setLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: descriptor set layout creation failed" );
		return false;
	}

	// interaction block ring: one dynamic uniform buffer, both stages
	VkDescriptorSetLayoutBinding uboBindingInfo;
	memset( &uboBindingInfo, 0, sizeof( uboBindingInfo ) );
	uboBindingInfo.binding = 0;
	uboBindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uboBindingInfo.descriptorCount = 1;
	uboBindingInfo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	dslci.pBindings = &uboBindingInfo;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.uboSetLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: uniform descriptor set layout creation failed" );
		return false;
	}

	// shadow receiver set: compare sampler, per-space shadow block, and raw
	// depth sampler. Both sampler families are always valid so a cvar can
	// choose hardware or manual comparisons without a pipeline variant.
	VkDescriptorSetLayoutBinding shadowBindings[ 3 ];
	memset( shadowBindings, 0, sizeof( shadowBindings ) );
	shadowBindings[ 0 ].binding = 0;
	shadowBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	shadowBindings[ 0 ].descriptorCount = 1;
	shadowBindings[ 0 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	shadowBindings[ 1 ].binding = 1;
	shadowBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	shadowBindings[ 1 ].descriptorCount = 1;
	shadowBindings[ 1 ].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	shadowBindings[ 2 ].binding = 2;
	shadowBindings[ 2 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	shadowBindings[ 2 ].descriptorCount = 1;
	shadowBindings[ 2 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	dslci.bindingCount = 3;
	dslci.pBindings = shadowBindings;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.shadowSetLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow descriptor set layout creation failed" );
		return false;
	}
	dslci.bindingCount = 1;

	// Shadow-set budget: the atlas set plus scratch and identity-resident
	// point-cache cubes per frame slot. Each set is two combined image
	// samplers plus one dynamic UBO.
	const int shadowSetBudget = ( 1 + VK_SHADOW_MAX_POINT_CUBES
			+ VK_SHADOW_MAX_CACHE_SLOTS ) * VK_FRAMES_IN_FLIGHT;
	const int retiredSetBudget = VK_MAX_RETIRED_SETS * VK_FRAMES_IN_FLIGHT;
	VkDescriptorPoolSize poolSizes[ 3 ];
	poolSizes[ 0 ].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[ 0 ].descriptorCount = VK_MAX_DESCRIPTOR_SETS + 2 * shadowSetBudget + retiredSetBudget;
	poolSizes[ 1 ].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[ 1 ].descriptorCount = VK_FRAMES_IN_FLIGHT + shadowSetBudget;
	poolSizes[ 2 ].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[ 2 ].descriptorCount = 4 * VK_FRAMES_IN_FLIGHT;
	VkDescriptorPoolCreateInfo dpci;
	memset( &dpci, 0, sizeof( dpci ) );
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dpci.maxSets = VK_MAX_DESCRIPTOR_SETS + 2 * VK_FRAMES_IN_FLIGHT + shadowSetBudget + retiredSetBudget;
	dpci.poolSizeCount = 3;
	dpci.pPoolSizes = poolSizes;
	if ( vkCreateDescriptorPool( vkCtx.device, &dpci, NULL, &vkExec.descriptorPool ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: descriptor pool creation failed" );
		return false;
	}

	VkPushConstantRange pushRange;
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof( vkGuiPushConstants_t );

	VkPipelineLayoutCreateInfo plci;
	memset( &plci, 0, sizeof( plci ) );
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &vkExec.setLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pushRange;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.pipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: pipeline layout creation failed" );
		return false;
	}

	// interaction layout: slots 0..5 reuse the per-image single-sampler
	// layout (cached per-image sets bind directly), slot 6 is the ring UBO;
	// the push range keeps the shared 128B block
	VkDescriptorSetLayout interactionSetLayouts[ 8 ];
	for ( int i = 0; i < 6; i++ ) {
		interactionSetLayouts[ i ] = vkExec.setLayout;
	}
	interactionSetLayouts[ 6 ] = vkExec.uboSetLayout;
	plci.setLayoutCount = 7;
	plci.pSetLayouts = interactionSetLayouts;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.interactionPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction pipeline layout creation failed" );
		return false;
	}

	// shadow-receiving interactions: the same seven slots plus set 7 (atlas
	// compare sampler + shadow block); dynamic offsets bind in set order,
	// so offset 0 = interaction slice, offset 1 = shadow slice
	interactionSetLayouts[ 7 ] = vkExec.shadowSetLayout;
	plci.setLayoutCount = 8;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.shadowInteractionPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction pipeline layout creation failed" );
		return false;
	}

	// r_shadowMapDebugOverlay: the shadow receiver set layout at slot 0, so
	// the published atlas set and any point cube set bind straight into the
	// overlay's panel pipelines. Its own smaller push range keeps every
	// overlay rectangle inside the guaranteed 128 bytes.
	VkPushConstantRange overlayPushRange;
	overlayPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	overlayPushRange.offset = 0;
	overlayPushRange.size = sizeof( vkShadowOverlayPush_t );
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &vkExec.shadowSetLayout;
	plci.pPushConstantRanges = &overlayPushRange;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.shadowOverlayPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow overlay pipeline layout creation failed" );
		return false;
	}
	plci.pPushConstantRanges = &pushRange;

	// fog/blend lights (Phase G2): sets 0/1 reuse the per-image
	// single-sampler layout (fog binds _fog + _fogEnter, blend binds the
	// stage projection + falloff), set 2 is the ring UBO for the
	// blend-light block (the fog shaders bind only sets 0/1); the push
	// range keeps the shared 128B block
	VkDescriptorSetLayout fogBlendSetLayouts[ 3 ];
	fogBlendSetLayouts[ 0 ] = vkExec.setLayout;
	fogBlendSetLayouts[ 1 ] = vkExec.setLayout;
	fogBlendSetLayouts[ 2 ] = vkExec.uboSetLayout;
	plci.setLayoutCount = 3;
	plci.pSetLayouts = fogBlendSetLayouts;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.fogBlendPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: fog/blend pipeline layout creation failed" );
		return false;
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( !VK_Ring_Create( vkExec.vertexRings[ i ], VK_VERTEX_RING_BYTES,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT )
				|| !VK_Ring_Create( vkExec.indexRings[ i ], VK_INDEX_RING_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT )
				|| !VK_Ring_Create( vkExec.uniformRings[ i ], VK_UNIFORM_RING_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ) ) {
			return false;
		}

		// one descriptor set per slot, written once: dynamic offsets select
		// the 256B slice at bind time
		VkDescriptorSetAllocateInfo dsai;
		memset( &dsai, 0, sizeof( dsai ) );
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = vkExec.descriptorPool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &vkExec.uboSetLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &vkExec.uniformRingSets[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: uniform ring descriptor set allocation failed" );
			return false;
		}
		VkDescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = vkExec.uniformRings[ i ].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_UNIFORM_SLICE_BYTES;
		VkWriteDescriptorSet write;
		memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vkExec.uniformRingSets[ i ];
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );

		// shadow set per slot: the ring binding is written once here; the
		// atlas image binding is (re)written when the atlas is created
		dsai.pSetLayouts = &vkExec.shadowSetLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &vkExec.shadowSets[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: shadow descriptor set allocation failed" );
			return false;
		}
		bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;
		write.dstSet = vkExec.shadowSets[ i ];
		write.dstBinding = 1;
		vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );
	}

	// Optional: failures leave the authoritative CPU deformation/cache path in
	// place and do not prevent the Vulkan renderer from starting.
	if ( !VK_GpuSkinning_InitResources() ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
	}

	vkExec.pipelineTargetFormat = vkCtx.swapchainFormat;
	vkExec.clearColor[ 0 ] = 0.0f;
	vkExec.clearColor[ 1 ] = 0.0f;
	vkExec.clearColor[ 2 ] = 0.0f;
	vkExec.clearColor[ 3 ] = 1.0f;
	vkExec.temporalSceneFrame = -1;
	vkExec.temporalDirectHistoryFrame = -1;
	vkExec.temporalDepthStampFrame = -1;
	vkExec.temporalDirectHistoryGeneration =
		R_TemporalPresentation_HistoryGeneration();
	vkExec.temporalCaptureInvalidatedFrame = -1;
	vkExec.temporalSceneCompositeInvalidatedFrame = -1;
	vkExec.temporalHistoryGenerationSeen =
		R_TemporalPresentation_HistoryGeneration();
	vkExec.initialized = true;
	initGuard.Commit();
	common->Printf( "Vulkan: GUI executor initialized\n" );
	return true;
}

void VK_GuiExecutor_Shutdown( void ) {
	for ( int i = 0; i < 2; ++i ) {
		if ( vkExec.temporalDirectHistoryRenderTextures[i] != NULL ) {
			delete vkExec.temporalDirectHistoryRenderTextures[i];
			vkExec.temporalDirectHistoryRenderTextures[i] = NULL;
		}
		vkExec.temporalDirectHistoryImages[i] = NULL;
	}
	if ( vkExec.temporalSceneRenderTexture != NULL ) {
		if ( backEnd.renderTexture == vkExec.temporalSceneRenderTexture ) {
			backEnd.renderTexture = NULL;
		}
		if ( backEnd.feedbackRenderTexture == vkExec.temporalSceneRenderTexture ) {
			backEnd.feedbackRenderTexture = NULL;
		}
		delete vkExec.temporalSceneRenderTexture;
		vkExec.temporalSceneRenderTexture = NULL;
	}
	vkExec.temporalSceneColorImage = NULL;
	vkExec.temporalSceneDepthImage = NULL;
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		memset( &vkExec, 0, sizeof( vkExec ) );
		return;
	}
	VK_ShadowMap_Shutdown();
	VK_GpuSkinning_DestroyResources();
	for ( int i = 0; i < vkExec.numPipelines; i++ ) {
		if ( vkExec.pipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numScreenPipelines; i++ ) {
		if ( vkExec.screenPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.screenPipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
		if ( vkExec.cubePipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.cubePipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numEnvPipelines; i++ ) {
		if ( vkExec.envPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.envPipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numProgramPipelines; i++ ) {
		if ( vkExec.programPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.programPipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numSpecialPipelines; i++ ) {
		if ( vkExec.specialPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.specialPipelines[ i ].pipeline, NULL );
		}
	}
	if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.casterPipeline, NULL );
	}
	if ( vkExec.pointCasterPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.pointCasterPipeline, NULL );
	}
	for ( int i = 0; i < vkExec.numBlendLightPipelines; i++ ) {
		if ( vkExec.blendLightPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.blendLightPipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numTemporalResolvePipelines; ++i ) {
		if ( vkExec.temporalResolvePipelines[i].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device,
				vkExec.temporalResolvePipelines[i].pipeline, NULL );
		}
	}
	if ( vkExec.pipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.pipelineLayout, NULL );
	}
	if ( vkExec.interactionPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.interactionPipelineLayout, NULL );
	}
	if ( vkExec.shadowInteractionPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.shadowInteractionPipelineLayout, NULL );
	}
	if ( vkExec.shadowOverlayPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.shadowOverlayPipelineLayout, NULL );
	}
	if ( vkExec.fogBlendPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.fogBlendPipelineLayout, NULL );
	}
	if ( vkExec.descriptorPool != VK_NULL_HANDLE ) {
		vkDestroyDescriptorPool( vkCtx.device, vkExec.descriptorPool, NULL );
	}
	if ( vkExec.setLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.setLayout, NULL );
	}
	if ( vkExec.uboSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.uboSetLayout, NULL );
	}
	if ( vkExec.shadowSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.shadowSetLayout, NULL );
	}
	if ( vkExec.vertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.vertModule, NULL );
	}
	if ( vkExec.fragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.fragModule, NULL );
	}
	if ( vkExec.screenVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.screenVertModule, NULL );
	}
	if ( vkExec.screenFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.screenFragModule, NULL );
	}
	if ( vkExec.temporalResolveVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device,
			vkExec.temporalResolveVertModule, NULL );
	}
	if ( vkExec.temporalResolveFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device,
			vkExec.temporalResolveFragModule, NULL );
	}
	if ( vkExec.skyVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.skyVertModule, NULL );
	}
	if ( vkExec.skyFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.skyFragModule, NULL );
	}
	if ( vkExec.envVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.envVertModule, NULL );
	}
	if ( vkExec.envFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.envFragModule, NULL );
	}
	if ( vkExec.bumpyEnvVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.bumpyEnvVertModule, NULL );
	}
	if ( vkExec.bumpyEnvFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.bumpyEnvFragModule, NULL );
	}
	if ( vkExec.heatHazeVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.heatHazeVertModule, NULL );
	}
	if ( vkExec.heatHazeFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.heatHazeFragModule, NULL );
	}
	if ( vkExec.heatHazeMaskFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.heatHazeMaskFragModule, NULL );
	}
	if ( vkExec.heatHazeVertexVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.heatHazeVertexVertModule, NULL );
	}
	if ( vkExec.heatHazeMaskVertexFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.heatHazeMaskVertexFragModule, NULL );
	}
	if ( vkExec.monochromeVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.monochromeVertModule, NULL );
	}
	if ( vkExec.monochromeFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.monochromeFragModule, NULL );
	}
	if ( vkExec.glassWarpVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.glassWarpVertModule, NULL );
	}
	if ( vkExec.glassWarpFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.glassWarpFragModule, NULL );
	}
	if ( vkExec.refractiveGlassVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.refractiveGlassVertModule, NULL );
	}
	if ( vkExec.refractiveGlassFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.refractiveGlassFragModule, NULL );
	}
	for ( int i = 0; i < VK_GLSL_PROGRAM_FAMILY_COUNT; i++ ) {
		if ( vkExec.glslMaterialVertModules[ i ] != VK_NULL_HANDLE ) {
			vkDestroyShaderModule( vkCtx.device,
					vkExec.glslMaterialVertModules[ i ], NULL );
		}
		if ( vkExec.glslMaterialFragModules[ i ] != VK_NULL_HANDLE ) {
			vkDestroyShaderModule( vkCtx.device,
					vkExec.glslMaterialFragModules[ i ], NULL );
		}
	}
	if ( vkExec.interactionVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionVertModule, NULL );
	}
	if ( vkExec.interactionFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionFragModule, NULL );
	}
	if ( vkExec.interactionShadowVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowVertModule, NULL );
	}
	if ( vkExec.interactionShadowFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowFragModule, NULL );
	}
	if ( vkExec.interactionShadowPointVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowPointVertModule, NULL );
	}
	if ( vkExec.interactionShadowPointFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowPointFragModule, NULL );
	}
	if ( vkExec.casterVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.casterVertModule, NULL );
	}
	if ( vkExec.casterFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.casterFragModule, NULL );
	}
	if ( vkExec.pointCasterVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.pointCasterVertModule, NULL );
	}
	if ( vkExec.pointCasterFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.pointCasterFragModule, NULL );
	}
	if ( vkExec.stencilShadowVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.stencilShadowVertModule, NULL );
	}
	if ( vkExec.stencilShadowFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.stencilShadowFragModule, NULL );
	}
	if ( vkExec.shadowOverlayVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.shadowOverlayVertModule, NULL );
	}
	if ( vkExec.shadowOverlayFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.shadowOverlayFragModule, NULL );
	}
	if ( vkExec.shadowOverlayPointFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.shadowOverlayPointFragModule, NULL );
	}
	if ( vkExec.shadowOverlayTextFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.shadowOverlayTextFragModule, NULL );
	}
	if ( vkExec.fogVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.fogVertModule, NULL );
	}
	if ( vkExec.fogFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.fogFragModule, NULL );
	}
	if ( vkExec.blendLightVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.blendLightVertModule, NULL );
	}
	if ( vkExec.blendLightFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.blendLightFragModule, NULL );
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkExec.vertexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.vertexRings[ i ].buffer, vkExec.vertexRings[ i ].allocation );
		}
		if ( vkExec.indexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.indexRings[ i ].buffer, vkExec.indexRings[ i ].allocation );
		}
		if ( vkExec.uniformRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.uniformRings[ i ].buffer, vkExec.uniformRings[ i ].allocation );
		}
	}
	memset( &vkExec, 0, sizeof( vkExec ) );
}

/*
====================
Frame lifecycle
====================
*/
void VK_GuiExecutor_SetClearColor( const float color[ 4 ] ) {
	vkExec.clearColor[ 0 ] = color[ 0 ];
	vkExec.clearColor[ 1 ] = color[ 1 ];
	vkExec.clearColor[ 2 ] = color[ 2 ];
	vkExec.clearColor[ 3 ] = color[ 3 ];
}

static bool VK_GuiExecutor_BeginFrame( void ) {
	static bool loggedNotInitialized = false;
	static bool loggedInitFailed = false;
	if ( vkExec.frameOpen ) {
		return true;
	}
	if ( !vkCtx.initialized ) {
		if ( !loggedNotInitialized ) {
			loggedNotInitialized = true;
			common->Printf( "Vulkan: GUI executor BeginFrame before device init\n" );
		}
		return false;
	}
	if ( !VK_GuiExecutor_Init() ) {
		if ( !loggedInitFailed ) {
			loggedInitFailed = true;
			common->Printf( "Vulkan: GUI executor init failed; frames skipped\n" );
		}
		return false;
	}
	// a failed mid-run recreate tears the swapchain down entirely; keep
	// retrying until a usable swapchain (with depth images) exists
	if ( vkCtx.swapchain == VK_NULL_HANDLE || vkCtx.depthImages[ vkCtx.frameSlot ] == VK_NULL_HANDLE ) {
		if ( !VK_Device_RecreateSwapchain()
				|| vkCtx.swapchain == VK_NULL_HANDLE || vkCtx.depthImages[ vkCtx.frameSlot ] == VK_NULL_HANDLE ) {
			return false;
		}
	}
	const bool temporalOutputKnown = vkExec.temporalNativeWidth > 0
		&& vkExec.temporalNativeHeight > 0
		&& vkExec.temporalSwapchainFormat != VK_FORMAT_UNDEFINED;
	if ( temporalOutputKnown
			&& ( vkExec.temporalNativeWidth != (int)vkCtx.swapchainExtent.width
				|| vkExec.temporalNativeHeight != (int)vkCtx.swapchainExtent.height
				|| vkExec.temporalSwapchainFormat != vkCtx.swapchainFormat ) ) {
		R_TemporalPresentation_InvalidateHistory(
			"Vulkan swapchain extent/format changed" );
		vkExec.temporalHistoryGenerationSeen =
			R_TemporalPresentation_HistoryGeneration();
	}
	vkExec.temporalNativeWidth = (int)vkCtx.swapchainExtent.width;
	vkExec.temporalNativeHeight = (int)vkCtx.swapchainExtent.height;
	vkExec.temporalSwapchainFormat = vkCtx.swapchainFormat;
	// swapchain format changes (rare) invalidate the pipeline set
	if ( vkExec.pipelineTargetFormat != vkCtx.swapchainFormat ) {
		vkDeviceWaitIdle( vkCtx.device );
		for ( int i = 0; i < vkExec.numPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
		vkExec.numPipelines = 0;
		for ( int i = 0; i < vkExec.numScreenPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.screenPipelines[ i ].pipeline, NULL );
		}
		vkExec.numScreenPipelines = 0;
		for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.cubePipelines[ i ].pipeline, NULL );
		}
		vkExec.numCubePipelines = 0;
		for ( int i = 0; i < vkExec.numEnvPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.envPipelines[ i ].pipeline, NULL );
		}
		vkExec.numEnvPipelines = 0;
		for ( int i = 0; i < vkExec.numProgramPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.programPipelines[ i ].pipeline, NULL );
		}
		vkExec.numProgramPipelines = 0;
		for ( int i = 0; i < vkExec.numSpecialPipelines; i++ ) {
			if ( vkExec.specialPipelines[ i ].pipeline != VK_NULL_HANDLE ) {
				vkDestroyPipeline( vkCtx.device, vkExec.specialPipelines[ i ].pipeline, NULL );
			}
		}
		vkExec.numSpecialPipelines = 0;
		if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.casterPipeline, NULL );
			vkExec.casterPipeline = VK_NULL_HANDLE;
		}
		if ( vkExec.pointCasterPipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pointCasterPipeline, NULL );
			vkExec.pointCasterPipeline = VK_NULL_HANDLE;
		}
		for ( int i = 0; i < vkExec.numBlendLightPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.blendLightPipelines[ i ].pipeline, NULL );
		}
		vkExec.numBlendLightPipelines = 0;
		for ( int i = 0; i < vkExec.numTemporalResolvePipelines; ++i ) {
			vkDestroyPipeline( vkCtx.device,
				vkExec.temporalResolvePipelines[i].pipeline, NULL );
		}
		vkExec.numTemporalResolvePipelines = 0;
		vkExec.pipelineTargetFormat = vkCtx.swapchainFormat;
	}

	const int slot = vkCtx.frameSlot;
	vkCtx.frameSlot = ( vkCtx.frameSlot + 1 ) % VK_FRAMES_IN_FLIGHT;
	vkCtx.recordingSlot = slot;

	const VkResult frameFenceResult = vkWaitForFences( vkCtx.device, 1,
		&vkCtx.frameFences[ slot ], VK_TRUE, UINT64_MAX );
	if ( frameFenceResult != VK_SUCCESS ) {
		common->Warning( "Vulkan: frame-slot %d fence wait failed (%d)", slot,
			static_cast<int>( frameFenceResult ) );
		return false;
	}
	// retire the last upload batch (and, per fence submission-order scope,
	// every earlier one) before deferred destroys can release images those
	// batches referenced; near-free, since the batch was submitted before the
	// previous frame's submit
	VK_Device_WaitUploadBatch();
	VK_Device_FlushDeferredDestroys( slot );
	if ( vkExec.numRetiredSets[ slot ] > 0 ) {
		vkFreeDescriptorSets( vkCtx.device, vkExec.descriptorPool,
				(uint32_t)vkExec.numRetiredSets[ slot ], vkExec.retiredSets[ slot ] );
		vkExec.numRetiredSets[ slot ] = 0;
	}

	// A present-mode change needs a new swapchain. GL re-applies its swap
	// interval on every present; the Vulkan equivalent only ever ran inside
	// VK_Device_PresentClearFrame, which nothing calls, so toggling vsync did
	// nothing here until something else forced a rebuild. Do it before the
	// acquire, where the old swapchain is not yet in use this frame.
	if ( R_GetEffectiveSwapInterval() != vkCtx.swapInterval ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return false;
		}
	}

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
			vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return false;
		}
		res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
				vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	}
	if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR ) {
		return false;
	}

	VkCommandBuffer cmd = vkCtx.commandBuffers[ slot ];
	VkCommandBufferBeginInfo cbbi;
	memset( &cbbi, 0, sizeof( cbbi ) );
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	// Reset/begin the command buffer BEFORE resetting the slot fence, and bail
	// on failure (device-lost between the fence wait and here): recording the
	// barriers below into a command buffer that was never begun is undefined
	// behavior, and leaving the fence signaled on the failure path keeps the
	// slot's "fence signaled == slot idle" invariant so the next frame does not
	// deadlock. The acquired swapchain image is abandoned only on this already
	// terminal device-lost path.
	if ( vkResetCommandBuffer( cmd, 0 ) != VK_SUCCESS
			|| vkBeginCommandBuffer( cmd, &cbbi ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: frame-slot %d command buffer begin failed", slot );
		return false;
	}
	if ( vkResetFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ] ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: frame-slot %d fence reset failed", slot );
		return false;
	}
	// The slot fence above is the sole retirement wait. Timestamp results are
	// polled here without VK_QUERY_RESULT_WAIT_BIT before this slot is reset.
	VK_GpuFrameTiming_BeginFrame( cmd, slot, tr.frameCount );

	VkImageMemoryBarrier2 toAttachment[ 2 ];
	memset( toAttachment, 0, sizeof( toAttachment ) );
	toAttachment[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toAttachment[ 0 ].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	toAttachment[ 0 ].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toAttachment[ 0 ].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toAttachment[ 0 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toAttachment[ 0 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toAttachment[ 0 ].image = vkCtx.swapchainImages[ imageIndex ];
	toAttachment[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toAttachment[ 0 ].subresourceRange.levelCount = 1;
	toAttachment[ 0 ].subresourceRange.layerCount = 1;
	toAttachment[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toAttachment[ 1 ].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment[ 1 ].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment[ 1 ].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	toAttachment[ 1 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toAttachment[ 1 ].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	toAttachment[ 1 ].image = vkCtx.depthImages[ slot ];
	toAttachment[ 1 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	toAttachment[ 1 ].subresourceRange.levelCount = 1;
	toAttachment[ 1 ].subresourceRange.layerCount = 1;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 2;
	dep.pImageMemoryBarriers = toAttachment;
	vkCmdPipelineBarrier2( cmd, &dep );

	vkExec.frameSlot = slot;
	vkExec.swapImageIndex = imageIndex;
	vkExec.cmd = cmd;
	vkExec.activeRenderTexture = NULL;
	vkExec.activeColorEntry = NULL;
	vkExec.activeDepthEntry = NULL;
	vkExec.activeDepthAttachmentView = vkCtx.depthViews[ slot ];
	vkExec.activeExtent = vkCtx.swapchainExtent;
	vkExec.activePipelineTarget = VK_Exec_SwapchainPipelineTarget();
	vkExec.frameOpen = true;
	vkExec.acquireWaitPending = true;
	VK_Exec_BeginMainRendering( true );

	vkExec.vertexRings[ slot ].cursor = 0;
	vkExec.indexRings[ slot ].cursor = 0;
	vkExec.uniformRings[ slot ].cursor = 0;
	vkExec.vertexRings[ slot ].overflowWarned = false;
	vkExec.indexRings[ slot ].overflowWarned = false;
	vkExec.uniformRings[ slot ].overflowWarned = false;
	memset( vkExec.vertMemo, 0, sizeof( vkExec.vertMemo ) );
	memset( vkExec.idxMemo, 0, sizeof( vkExec.idxMemo ) );
	memset( vkExec.gpuSkinningMemo, 0, sizeof( vkExec.gpuSkinningMemo ) );
	vkSharedGeometryCheckpoint.active = false;
	return true;
}

/*
====================
VK_Exec_BeginMainRendering / VK_Exec_EndMainRendering

The swapchain dynamic-rendering scope, factored so the Phase F2a shadow
pass can interrupt it mid-3D-view: end main rendering -> atlas caster scope
-> resume with loadOp LOAD for color AND depth. The clear path is the frame
open (byte-identical to the pre-split BeginFrame recording when no shadow
maps render); the resume path re-establishes the same baseline dynamic
state. Depth storeOp stays DONT_CARE on the r_useShadowMap 0 default and
switches to STORE only when a shadow interruption may need the depth-fill
contents to survive the scope break.
====================
*/
static void VK_Exec_BarrierActiveTargetForLoad( void ) {
	VkImageMemoryBarrier2 barriers[ 2 ];
	memset( barriers, 0, sizeof( barriers ) );

	barriers[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barriers[ 0 ].srcStageMask =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barriers[ 0 ].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[ 0 ].dstStageMask =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barriers[ 0 ].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
			| VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[ 0 ].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[ 0 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[ 0 ].image = vkExec.activeColorEntry != NULL
			? vkExec.activeColorEntry->image
			: vkCtx.swapchainImages[ vkExec.swapImageIndex ];
	barriers[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barriers[ 0 ].subresourceRange.levelCount = 1;
	barriers[ 0 ].subresourceRange.layerCount = 1;

	barriers[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barriers[ 1 ].srcStageMask =
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	barriers[ 1 ].srcAccessMask =
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barriers[ 1 ].dstStageMask =
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
	barriers[ 1 ].dstAccessMask =
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
			| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barriers[ 1 ].oldLayout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	barriers[ 1 ].newLayout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	barriers[ 1 ].image = vkExec.activeDepthEntry != NULL
			? vkExec.activeDepthEntry->image
			: vkCtx.depthImages[ vkExec.frameSlot ];
	barriers[ 1 ].subresourceRange.aspectMask =
			vkExec.activeDepthEntry != NULL
			? vkExec.activeDepthEntry->aspectMask
			: VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	barriers[ 1 ].subresourceRange.levelCount = 1;
	barriers[ 1 ].subresourceRange.layerCount = 1;

	VkDependencyInfo dependency;
	memset( &dependency, 0, sizeof( dependency ) );
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount =
			vkExec.activeDepthAttachmentView != VK_NULL_HANDLE ? 2 : 1;
	dependency.pImageMemoryBarriers = barriers;
	vkCmdPipelineBarrier2( vkExec.cmd, &dependency );
}

bool VK_Exec_BeginMainRendering( bool clearColorDepth ) {
	if ( vkExec.mainScopeOpen || vkExec.cmd == VK_NULL_HANDLE ) {
		return vkExec.mainScopeOpen;
	}
	VkCommandBuffer cmd = vkExec.cmd;
	if ( !clearColorDepth ) {
		// Dynamic-rendering scopes do not provide implicit external
		// dependencies. Make the previous stores visible to LOAD before a
		// scope resumes, including when the layout itself did not change.
		VK_Exec_BarrierActiveTargetForLoad();
	}

	VkRenderingAttachmentInfo color;
	memset( &color, 0, sizeof( color ) );
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = vkExec.activeColorEntry != NULL
			? vkExec.activeColorEntry->attachmentView
			: vkCtx.swapchainViews[ vkExec.swapImageIndex ];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = clearColorDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color.float32[ 0 ] = vkExec.clearColor[ 0 ];
	color.clearValue.color.float32[ 1 ] = vkExec.clearColor[ 1 ];
	color.clearValue.color.float32[ 2 ] = vkExec.clearColor[ 2 ];
	color.clearValue.color.float32[ 3 ] = vkExec.clearColor[ 3 ];

	// depth/stencil attach for the whole frame; contents are transient (the
	// world passes re-clear per 3D view via vkCmdClearAttachments)
	VkRenderingAttachmentInfo depth;
	memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = vkExec.activeDepthAttachmentView;
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depth.loadOp = clearColorDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	// Render-target switches, feedback captures, and depth readback can all
	// interrupt the scope. Keep depth/stencil contents live across those
	// breaks; default direct rendering still uses transient device-local
	// storage, so this changes bandwidth rather than allocation policy.
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth.clearValue.depthStencil.depth = 1.0f;
	depth.clearValue.depthStencil.stencil = 128;

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent = vkExec.activeExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;
	if ( depth.imageView != VK_NULL_HANDLE ) {
		ri.pDepthAttachment = &depth;
		if ( vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED ) {
			ri.pStencilAttachment = &depth;
		}
	}
	vkCmdBeginRendering( cmd, &ri );

	// baseline dynamic state: 2D semantics (depth/cull off); the world
	// passes override per surface and the next 2D view resets here
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
	if ( vkCtx.depthBoundsSupported ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );
		vkCmdSetDepthBounds( cmd, 0.0f, 1.0f );
	}

	// stencil baseline: off with benign values. Every pipeline declares the
	// five stencil dynamics (Phase G1), so all five must be latched before
	// any draw of the frame; the stencil shadow pass owns the live values
	// per light and restores test-off behind itself
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
	vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
	vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
	vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 128 );

	vkExec.mainScopeOpen = true;
	return true;
}

void VK_Exec_EndMainRendering( void ) {
	if ( !vkExec.mainScopeOpen ) {
		return;
	}
	vkCmdEndRendering( vkExec.cmd );
	vkExec.mainScopeOpen = false;
}

static void VK_Exec_LayoutAccess( VkImageLayout layout, VkPipelineStageFlags2 &stage,
		VkAccessFlags2 &access ) {
	switch ( layout ) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			access = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			access = 0;
			break;
		case VK_IMAGE_LAYOUT_UNDEFINED:
			stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			access = 0;
			break;
		default:
			stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			break;
	}
}

static VkImageAspectFlags VK_Exec_BarrierAspectMask( const vkImageEntry_t *entry ) {
	VkImageAspectFlags aspectMask = entry->aspectMask;
	// A depth-only sampled/attachment view can still be backed by a combined
	// depth-stencil image. Without separate depth/stencil layouts, image
	// barriers must cover both aspects even when an operation consumes depth.
	switch ( entry->format ) {
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			break;
		default:
			break;
	}
	return aspectMask;
}

static void VK_Exec_TransitionImage( vkImageEntry_t *entry, VkImageLayout newLayout ) {
	if ( entry == NULL || entry->image == VK_NULL_HANDLE || entry->layout == newLayout ) {
		return;
	}
	VkImageMemoryBarrier2 barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	VK_Exec_LayoutAccess( entry->layout, barrier.srcStageMask, barrier.srcAccessMask );
	VK_Exec_LayoutAccess( newLayout, barrier.dstStageMask, barrier.dstAccessMask );
	barrier.oldLayout = entry->layout;
	barrier.newLayout = newLayout;
	barrier.image = entry->image;
	barrier.subresourceRange.aspectMask = VK_Exec_BarrierAspectMask( entry );
	barrier.subresourceRange.levelCount = (uint32_t)entry->numMips;
	barrier.subresourceRange.layerCount = (uint32_t)entry->numLayers;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( vkExec.cmd, &dep );
	entry->layout = newLayout;
}

static void VK_Exec_TransitionSwapchain( VkImageLayout oldLayout, VkImageLayout newLayout ) {
	if ( oldLayout == newLayout ) {
		return;
	}
	VkImageMemoryBarrier2 barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	VK_Exec_LayoutAccess( oldLayout, barrier.srcStageMask, barrier.srcAccessMask );
	VK_Exec_LayoutAccess( newLayout, barrier.dstStageMask, barrier.dstAccessMask );
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.image = vkCtx.swapchainImages[ vkExec.swapImageIndex ];
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( vkExec.cmd, &dep );
}

static void VK_Exec_TransitionSwapchainDepth( VkImageLayout oldLayout, VkImageLayout newLayout ) {
	if ( oldLayout == newLayout ) {
		return;
	}
	VkImageMemoryBarrier2 barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	VK_Exec_LayoutAccess( oldLayout, barrier.srcStageMask, barrier.srcAccessMask );
	VK_Exec_LayoutAccess( newLayout, barrier.dstStageMask, barrier.dstAccessMask );
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.image = vkCtx.depthImages[ vkExec.frameSlot ];
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( vkExec.cmd, &dep );
}

static bool VK_Exec_RenderTextureEntries( idRenderTexture *renderTexture,
		vkImageEntry_t *&colorEntry, vkImageEntry_t *&depthEntry ) {
	colorEntry = NULL;
	depthEntry = NULL;
	if ( renderTexture == NULL || !renderTexture->EnsureDeviceHandle()
			|| renderTexture->GetNumColorImages() != 1 ) {
		return false;
	}

	idImage *colorImage = renderTexture->GetColorImage( 0 );
	if ( colorImage == NULL ) {
		return false;
	}
	colorEntry = VK_Image_GetEntry( colorImage->GetDeviceHandle() );
	if ( colorEntry == NULL || colorEntry->attachmentView == VK_NULL_HANDLE
			|| ( colorEntry->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) == 0 ) {
		return false;
	}

	idImage *depthImage = renderTexture->GetDepthImage();
	if ( depthImage != NULL ) {
		depthEntry = VK_Image_GetEntry( depthImage->GetDeviceHandle() );
		if ( depthEntry == NULL || depthEntry->attachmentView == VK_NULL_HANDLE
				|| ( depthEntry->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0
				|| depthEntry->width != colorEntry->width || depthEntry->height != colorEntry->height
				|| depthEntry->samples != colorEntry->samples ) {
			return false;
		}
	}
	return true;
}

static void VK_Exec_TransitionActiveTargetToSampled( void ) {
	if ( vkExec.activeRenderTexture == NULL ) {
		return;
	}
	VK_Exec_TransitionImage( vkExec.activeColorEntry, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_Exec_TransitionImage( vkExec.activeDepthEntry, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

static void VK_Exec_TransitionActiveTargetToAttachments( void ) {
	if ( vkExec.activeRenderTexture == NULL ) {
		return;
	}
	VK_Exec_TransitionImage( vkExec.activeColorEntry, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_Exec_TransitionImage( vkExec.activeDepthEntry, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
}

bool VK_Exec_SetRenderTarget( idRenderTexture *renderTexture ) {
	if ( !VK_GuiExecutor_BeginFrame() ) {
		return false;
	}
	if ( renderTexture == vkExec.activeRenderTexture ) {
		if ( !vkExec.mainScopeOpen ) {
			VK_Exec_TransitionActiveTargetToAttachments();
			return VK_Exec_BeginMainRendering( false );
		}
		return true;
	}

	vkImageEntry_t *colorEntry = NULL;
	vkImageEntry_t *depthEntry = NULL;
	if ( renderTexture != NULL && !VK_Exec_RenderTextureEntries( renderTexture, colorEntry, depthEntry ) ) {
		return false;
	}

	VK_Exec_EndMainRendering();
	VK_Exec_TransitionActiveTargetToSampled();

	vkExec.activeRenderTexture = renderTexture;
	vkExec.activeColorEntry = colorEntry;
	vkExec.activeDepthEntry = depthEntry;
	if ( renderTexture != NULL ) {
		VK_Exec_TransitionActiveTargetToAttachments();
		vkExec.activeDepthAttachmentView = depthEntry != NULL ? depthEntry->attachmentView : VK_NULL_HANDLE;
		vkExec.activeExtent.width = (uint32_t)colorEntry->width;
		vkExec.activeExtent.height = (uint32_t)colorEntry->height;
		vkExec.activePipelineTarget.colorFormat = colorEntry->format;
		vkExec.activePipelineTarget.depthFormat = depthEntry != NULL ? depthEntry->format : VK_FORMAT_UNDEFINED;
		vkExec.activePipelineTarget.stencilFormat = depthEntry != NULL
				&& ( depthEntry->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT ) != 0
				? depthEntry->format : VK_FORMAT_UNDEFINED;
		vkExec.activePipelineTarget.samples = colorEntry->samples;
	} else {
		vkExec.activeDepthAttachmentView = vkCtx.depthViews[ vkExec.frameSlot ];
		vkExec.activeExtent = vkCtx.swapchainExtent;
		vkExec.activePipelineTarget = VK_Exec_SwapchainPipelineTarget();
	}
	return VK_Exec_BeginMainRendering( false );
}

int VK_Exec_ActiveFramebufferWidth( void ) {
	return vkExec.frameOpen ? (int)vkExec.activeExtent.width : (int)vkCtx.swapchainExtent.width;
}

int VK_Exec_ActiveFramebufferHeight( void ) {
	return vkExec.frameOpen ? (int)vkExec.activeExtent.height : (int)vkCtx.swapchainExtent.height;
}

// Is the dynamic-rendering scope actually recording? VK_Exec_ActiveCmd only
// reports an open frame, and a pass that interrupts the scope can fail to
// resume it, so a consumer that appends draws of its own has to ask. Unlike
// VK_Exec_SharedInteractionTargetReady this accepts any active target: the
// scene can legitimately be rendering into an offscreen image.
bool VK_Exec_MainRenderingScopeOpen( void ) {
	return vkExec.frameOpen && vkExec.mainScopeOpen
		&& vkExec.cmd != VK_NULL_HANDLE;
}

bool VK_Exec_ActiveTargetHasStencil( void ) {
	return vkExec.frameOpen
		&& vkExec.activeDepthAttachmentView != VK_NULL_HANDLE
		&& vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED;
}

// Shared interaction ownership is intentionally limited to the primary
// swapchain scope.  Keep the executor's private attachment and pending-effect
// state behind one fail-closed query so the interaction consumer cannot
// mistake an offscreen render texture for the main framebuffer.
bool VK_Exec_SharedInteractionTargetReady( void ) {
	return vkExec.frameOpen && vkExec.mainScopeOpen
		&& vkExec.cmd != VK_NULL_HANDLE
		&& vkExec.activeRenderTexture == NULL
		&& vkExec.activeColorEntry == NULL
		&& vkExec.activeDepthEntry == NULL
		&& vkExec.pendingSpecialEffectsView == NULL
		&& vkExec.pendingSpecialEffectsMask == 0
		&& vkExec.pendingSpecialEffectsSource == NULL
		&& !vkExec.pendingSpecialEffectsNeedsResolve
		&& VK_Exec_PipelineTargetsMatch( vkExec.activePipelineTarget,
			VK_Exec_SwapchainPipelineTarget() );
}

void VK_Exec_ClearRenderTarget( bool clearColor, bool clearDepth, float depthValue,
		const float colorValue[ 4 ] ) {
	if ( !VK_GuiExecutor_BeginFrame() ) {
		return;
	}
	if ( !vkExec.mainScopeOpen && !VK_Exec_BeginMainRendering( false ) ) {
		return;
	}

	VkClearAttachment attachments[ 2 ];
	memset( attachments, 0, sizeof( attachments ) );
	int attachmentCount = 0;
	if ( clearColor ) {
		VkClearAttachment &attachment = attachments[ attachmentCount++ ];
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = 0;
		memcpy( attachment.clearValue.color.float32, colorValue, 4 * sizeof( float ) );
	}
	if ( clearDepth && vkExec.activeDepthAttachmentView != VK_NULL_HANDLE ) {
		VkClearAttachment &attachment = attachments[ attachmentCount++ ];
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if ( vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED ) {
			attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		attachment.clearValue.depthStencil.depth = depthValue;
		attachment.clearValue.depthStencil.stencil = 128;
	}
	if ( attachmentCount == 0 ) {
		return;
	}

	VkClearRect rect;
	memset( &rect, 0, sizeof( rect ) );
	rect.rect.extent = vkExec.activeExtent;
	rect.layerCount = 1;
	vkCmdClearAttachments( vkExec.cmd, (uint32_t)attachmentCount, attachments, 1, &rect );
}

static void VK_TemporalPresentation_ClearScaleState( vkSceneScaleState_t &state ) {
	state.active = false;
	state.backendOwned = false;
	state.projectionRecentered = false;
	state.rootView = NULL;
	state.nativeWidth = 0;
	state.nativeHeight = 0;
	state.targetWidth = 0;
	state.targetHeight = 0;
	state.nativeProjectionOffsetX = 0.0f;
	state.nativeProjectionOffsetY = 0.0f;
	state.nativeViewport.Clear();
	state.rectRestores.Clear();
	state.rectRestores.SetGranularity( 256 );
	state.rectRestoreHash.Clear( 4096, 4096 );
}

static bool VK_TemporalPresentation_IsMainSceneView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL
			|| ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0
			|| viewDef->isSubview || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->isXraySubview
			|| viewDef->renderView.viewID < 0 ) {
		return false;
	}
	return viewDef->renderWorld == NULL
		|| viewDef->renderWorld->mapName.Length() > 0;
}

static bool VK_TemporalPresentation_ViewMatchesRoot( const viewDef_t *viewDef,
		const viewDef_t *rootView ) {
	return viewDef != NULL && rootView != NULL
		&& viewDef->viewport.Equals( rootView->viewport )
		&& viewDef->renderView.width == rootView->renderView.width
		&& viewDef->renderView.height == rootView->renderView.height;
}

static const viewDef_t *VK_TemporalPresentation_RootForView(
		const viewDef_t *viewDef ) {
	if ( VK_TemporalPresentation_IsMainSceneView( viewDef ) ) {
		return viewDef;
	}
	if ( viewDef == NULL || viewDef->viewEntitys == NULL ) {
		return NULL;
	}

	if ( ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		const viewDef_t *rootView = tr.primaryView;
		return rootView != viewDef
			&& VK_TemporalPresentation_IsMainSceneView( rootView )
			&& VK_TemporalPresentation_ViewMatchesRoot( viewDef, rootView )
			? rootView : NULL;
	}

	// SS_SUBVIEW contributors render directly into their ancestor's viewport.
	// Capture-backed mirror/remote/refraction views use different footprints and
	// remain isolated on the target selected by their authored crop/copy stream.
	const viewDef_t *candidate = viewDef;
	while ( candidate != NULL && candidate->isSubview ) {
		const viewDef_t *superView = candidate->superView;
		if ( candidate->isXraySubview || superView == NULL
				|| candidate->subviewSurface == NULL
				|| candidate->subviewSurface->material == NULL
				|| candidate->subviewSurface->material->GetSort() != SS_SUBVIEW
				|| !VK_TemporalPresentation_ViewMatchesRoot( candidate, superView ) ) {
			return NULL;
		}
		candidate = superView;
	}
	return VK_TemporalPresentation_IsMainSceneView( candidate )
		&& VK_TemporalPresentation_ViewMatchesRoot( viewDef, candidate )
		? candidate : NULL;
}

static bool VK_TemporalPresentation_RootCoversNativeOutput(
		const viewDef_t *rootView ) {
	if ( rootView == NULL ) {
		return false;
	}
	const int width = rootView->viewport.x2 - rootView->viewport.x1 + 1;
	const int height = rootView->viewport.y2 - rootView->viewport.y1 + 1;
	return rootView->viewport.x1 == 0 && rootView->viewport.y1 == 0
		&& width == (int)vkCtx.swapchainExtent.width
		&& height == (int)vkCtx.swapchainExtent.height;
}

static bool VK_TemporalPresentation_BackendSceneRequested(
		const viewDef_t *rootView, int &targetWidth, int &targetHeight ) {
	targetWidth = 0;
	targetHeight = 0;
	if ( !VK_TemporalPresentation_RootCoversNativeOutput( rootView ) ) {
		return false;
	}
	const temporalPresentationFrameState_t &presentation =
		R_TemporalPresentation_GetFrameState();
	if ( presentation.frameNumber != backEnd.frameCount
			|| presentation.nativeWidth != (int)vkCtx.swapchainExtent.width
			|| presentation.nativeHeight != (int)vkCtx.swapchainExtent.height
			|| presentation.sceneWidth <= 0 || presentation.sceneHeight <= 0 ) {
		return false;
	}
	const bool sceneScaled = presentation.sceneWidth != presentation.nativeWidth
		|| presentation.sceneHeight != presentation.nativeHeight;
	if ( sceneScaled && !presentation.dynamicResolutionRequested
			&& presentation.effectiveScalePercent < 100
			&& r_resolutionScaleMode.GetInteger() == 0
			&& !R_TemporalPresentation_TemporalAARequested()
			&& !R_TemporalPresentation_ScreenSpaceEffectsRequested() ) {
		return false;
	}
	const bool knownCapture = presentation.captureFrozen
		|| presentation.captureForcedNative
		|| ( rootView != NULL && rootView->temporalCaptureFrame );
	const bool temporalScene = presentation.temporalAARequested && !knownCapture;
	const bool screenSpaceScene = AdvancedScreenSpaceCore_Requested(
		presentation.advancedScreenSpace );
	if ( !sceneScaled && !temporalScene && !screenSpaceScene ) {
		return false;
	}
	// The spatial presentation shader is also the projection-jitter safety
	// gate for a backend-owned scene target. If its swapchain pipeline cannot
	// be created, leave the view native so a later fixed-function scale cannot
	// expose subpixel jitter.
	if ( vkExec.temporalResolveVertModule == VK_NULL_HANDLE
			|| vkExec.temporalResolveFragModule == VK_NULL_HANDLE
			|| VK_TemporalPresentation_GetResolvePipeline() == VK_NULL_HANDLE ) {
		return false;
	}
	targetWidth = presentation.sceneWidth;
	targetHeight = presentation.sceneHeight;
	return true;
}

static int VK_TemporalPresentation_ScaleRectStart( int value,
		int sourceExtent, int targetExtent ) {
	return static_cast<int>( ( static_cast<int64>( value ) * targetExtent )
		/ sourceExtent );
}

static int VK_TemporalPresentation_ScaleRectEnd( int value,
		int sourceExtent, int targetExtent ) {
	return static_cast<int>( ( ( static_cast<int64>( value + 1 ) * targetExtent )
		+ sourceExtent - 1 ) / sourceExtent ) - 1;
}

static void VK_TemporalPresentation_ScaleLocalRect( idScreenRect &rect,
		int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( rect.IsEmpty() || sourceWidth <= 0 || sourceHeight <= 0
			|| targetWidth <= 0 || targetHeight <= 0 ) {
		return;
	}
	const int x1 = idMath::ClampInt( 0, targetWidth - 1,
		VK_TemporalPresentation_ScaleRectStart( rect.x1, sourceWidth, targetWidth ) );
	const int y1 = idMath::ClampInt( 0, targetHeight - 1,
		VK_TemporalPresentation_ScaleRectStart( rect.y1, sourceHeight, targetHeight ) );
	const int x2 = idMath::ClampInt( 0, targetWidth - 1,
		VK_TemporalPresentation_ScaleRectEnd( rect.x2, sourceWidth, targetWidth ) );
	const int y2 = idMath::ClampInt( 0, targetHeight - 1,
		VK_TemporalPresentation_ScaleRectEnd( rect.y2, sourceHeight, targetHeight ) );
	if ( x2 < x1 || y2 < y1 ) {
		rect.Clear();
		return;
	}
	rect.x1 = static_cast<short>( x1 );
	rect.y1 = static_cast<short>( y1 );
	rect.x2 = static_cast<short>( x2 );
	rect.y2 = static_cast<short>( y2 );
}

static bool VK_TemporalPresentation_RecordScaledRect(
		vkSceneScaleState_t &state, idScreenRect *rect ) {
	if ( rect == NULL ) {
		return false;
	}
	const int key = static_cast<int>( reinterpret_cast<uintptr_t>( rect ) >> 4 );
	for ( int i = state.rectRestoreHash.First( key ); i != -1;
			i = state.rectRestoreHash.Next( i ) ) {
		if ( state.rectRestores[i].rect == rect ) {
			return false;
		}
	}
	vkSceneScaleState_t::rectRestore_t &restore = state.rectRestores.Alloc();
	restore.rect = rect;
	restore.original = *rect;
	state.rectRestoreHash.Add( key, state.rectRestores.Num() - 1 );
	return true;
}

static void VK_TemporalPresentation_ScaleTrackedRect(
		vkSceneScaleState_t &state, idScreenRect *rect,
		int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( !VK_TemporalPresentation_RecordScaledRect( state, rect ) ) {
		return;
	}
	VK_TemporalPresentation_ScaleLocalRect( *rect, sourceWidth, sourceHeight,
		targetWidth, targetHeight );
}

static void VK_TemporalPresentation_ScaleDrawSurf(
		vkSceneScaleState_t &state, const drawSurf_t *surf,
		int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( surf != NULL ) {
		VK_TemporalPresentation_ScaleTrackedRect( state,
			&const_cast<drawSurf_t *>( surf )->scissorRect,
			sourceWidth, sourceHeight, targetWidth, targetHeight );
	}
}

static void VK_TemporalPresentation_ScaleDrawSurfChain(
		vkSceneScaleState_t &state, const drawSurf_t *surf,
		int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	for ( const drawSurf_t *chainSurf = surf; chainSurf != NULL;
			chainSurf = chainSurf->nextOnLight ) {
		VK_TemporalPresentation_ScaleDrawSurf( state, chainSurf, sourceWidth,
			sourceHeight, targetWidth, targetHeight );
	}
}

static void VK_TemporalPresentation_ScaleLight(
		vkSceneScaleState_t &state, const viewLight_t *vLight,
		int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( vLight == NULL ) {
		return;
	}
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->globalShadows,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->localInteractions,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->localShadows,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->globalInteractions,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->localShadowMapCasters,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->globalShadowMapCasters,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state,
		vLight->localTranslucentShadowMapCasters,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state,
		vLight->globalTranslucentShadowMapCasters,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	VK_TemporalPresentation_ScaleDrawSurfChain( state, vLight->translucentInteractions,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
}

static bool VK_TemporalPresentation_BeginScale( vkSceneScaleState_t &state,
		const viewDef_t *viewDef, const viewDef_t *rootView,
		int targetWidth, int targetHeight, bool backendOwned ) {
	VK_TemporalPresentation_ClearScaleState( state );
	if ( viewDef == NULL || rootView == NULL || targetWidth <= 0 || targetHeight <= 0
			|| targetWidth > 32767 || targetHeight > 32767 ) {
		return false;
	}
	viewDef_t *mutableView = const_cast<viewDef_t *>( viewDef );
	const int sourceWidth = mutableView->viewport.x2 - mutableView->viewport.x1 + 1;
	const int sourceHeight = mutableView->viewport.y2 - mutableView->viewport.y1 + 1;
	if ( sourceWidth <= 0 || sourceHeight <= 0 ) {
		return false;
	}

	state.active = true;
	state.backendOwned = backendOwned;
	state.rootView = rootView;
	state.nativeWidth = sourceWidth;
	state.nativeHeight = sourceHeight;
	state.targetWidth = targetWidth;
	state.targetHeight = targetHeight;
	state.nativeViewport = mutableView->viewport;
	// Native-scale TAA still needs backend ownership so the jittered 3D scene
	// reaches a history consumer before native UI begins. It needs no coordinate
	// mutation, but EndView must retain the same transaction/composite seam.
	if ( sourceWidth == targetWidth && sourceHeight == targetHeight ) {
		return true;
	}
	VK_TemporalPresentation_ScaleTrackedRect( state, &mutableView->scissor,
		sourceWidth, sourceHeight, targetWidth, targetHeight );
	mutableView->viewport.x1 = 0;
	mutableView->viewport.y1 = 0;
	mutableView->viewport.x2 = static_cast<short>( targetWidth - 1 );
	mutableView->viewport.y2 = static_cast<short>( targetHeight - 1 );

	for ( int i = 0; i < mutableView->numDrawSurfs; i++ ) {
		VK_TemporalPresentation_ScaleDrawSurf( state, mutableView->drawSurfs[i],
			sourceWidth, sourceHeight, targetWidth, targetHeight );
	}
	for ( viewLight_t *vLight = mutableView->viewLights; vLight != NULL;
			vLight = vLight->next ) {
		VK_TemporalPresentation_ScaleTrackedRect( state, &vLight->scissorRect,
			sourceWidth, sourceHeight, targetWidth, targetHeight );
		VK_TemporalPresentation_ScaleLight( state, vLight, sourceWidth,
			sourceHeight, targetWidth, targetHeight );
	}
	for ( viewEntity_t *vEntity = mutableView->viewEntitys; vEntity != NULL;
			vEntity = vEntity->next ) {
		VK_TemporalPresentation_ScaleTrackedRect( state, &vEntity->scissorRect,
			sourceWidth, sourceHeight, targetWidth, targetHeight );
	}
	return true;
}

static void VK_TemporalPresentation_RestoreScale( vkSceneScaleState_t &state,
		const viewDef_t *viewDef ) {
	if ( !state.active || viewDef == NULL ) {
		return;
	}
	viewDef_t *mutableView = const_cast<viewDef_t *>( viewDef );
	mutableView->viewport = state.nativeViewport;
	for ( int i = state.rectRestores.Num() - 1; i >= 0; i-- ) {
		if ( state.rectRestores[i].rect != NULL ) {
			*state.rectRestores[i].rect = state.rectRestores[i].original;
		}
	}
	state.rectRestores.Clear();
	state.rectRestoreHash.Clear();
	state.active = false;
}

static void VK_TemporalPresentation_RecenterDirectProjection(
		vkSceneScaleState_t &state, const viewDef_t *viewDef ) {
	if ( viewDef == NULL || !viewDef->temporalJitterEnabled
			|| state.projectionRecentered ) {
		return;
	}
	const int width = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int height = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( width <= 0 || height <= 0 ) {
		return;
	}
	viewDef_t *mutableView = const_cast<viewDef_t *>( viewDef );
	state.nativeProjectionOffsetX = mutableView->projectionMatrix[8];
	state.nativeProjectionOffsetY = mutableView->projectionMatrix[9];
	mutableView->projectionMatrix[8] -= 2.0f
		* mutableView->temporalJitterPixels.x / static_cast<float>( width );
	mutableView->projectionMatrix[9] -= 2.0f
		* mutableView->temporalJitterPixels.y / static_cast<float>( height );
	state.projectionRecentered = true;

	// No temporal consumer owns this direct view. Retire image/camera history so
	// a later successful offscreen frame is seeded instead of blending across
	// the spatial-only gap. A late capture already advanced this generation.
	if ( viewDef->temporalHistoryGeneration
			== R_TemporalPresentation_HistoryGeneration() ) {
		R_TemporalPresentation_InvalidateHistory(
			"Vulkan temporal scene ownership unavailable" );
	}
}

static void VK_TemporalPresentation_RestoreDirectProjection(
		vkSceneScaleState_t &state, const viewDef_t *viewDef ) {
	if ( !state.projectionRecentered || viewDef == NULL ) {
		return;
	}
	viewDef_t *mutableView = const_cast<viewDef_t *>( viewDef );
	mutableView->projectionMatrix[8] = state.nativeProjectionOffsetX;
	mutableView->projectionMatrix[9] = state.nativeProjectionOffsetY;
	state.projectionRecentered = false;
}

static void VK_TemporalPresentation_ObserveHistoryAndCapture( void ) {
	const unsigned int historyGeneration =
		R_TemporalPresentation_HistoryGeneration();
	if ( vkExec.temporalHistoryGenerationSeen != historyGeneration ) {
		// Both backend-owned and game-owned Vulkan histories retire at the shared
		// generation seam. Game ownership validates its immutable command later;
		// direct roots also drop their local ping-pong immediately.
		vkExec.temporalHistoryGenerationSeen = historyGeneration;
		vkExec.temporalDirectHistoryGeneration = historyGeneration;
		vkExec.temporalDirectHistoryValid = false;
		vkExec.temporalDirectHistoryFrame = -1;
		vkExec.temporalDepthStampValid = false;
	}
	if ( tr.takingScreenshot
			&& vkExec.temporalCaptureInvalidatedFrame != backEnd.frameCount ) {
		R_TemporalPresentation_MarkCurrentFrameCapture(
			"Vulkan execution-time capture" );
		vkExec.temporalHistoryGenerationSeen =
			R_TemporalPresentation_HistoryGeneration();
		vkExec.temporalDirectHistoryGeneration =
			vkExec.temporalHistoryGenerationSeen;
		vkExec.temporalDirectHistoryValid = false;
		vkExec.temporalDirectHistoryFrame = -1;
		vkExec.temporalDepthStampValid = false;
		vkExec.temporalCaptureInvalidatedFrame = backEnd.frameCount;
	}
}

static void VK_TemporalPresentation_StampDepth(
		idRenderTexture *target, bool success ) {
	vkExec.temporalDepthStampTarget = target;
	vkExec.temporalDepthStampFrame = backEnd.frameCount;
	vkExec.temporalDepthStampHistoryGeneration =
		R_TemporalPresentation_HistoryGeneration();
	vkExec.temporalDepthStampStorageGeneration = 0;
	vkExec.temporalDepthStampValid = false;
	if ( !success || target == NULL || target->GetDepthImage() == NULL ) {
		return;
	}
	vkExec.temporalDepthStampStorageGeneration =
		static_cast<unsigned long long>(
			target->GetDepthImage()->GetStorageGeneration() );
	vkExec.temporalDepthStampValid = true;
}

static bool VK_TemporalPresentation_DepthStampMatches(
		idRenderTexture *target, idImage *depthImage,
		unsigned int historyGeneration ) {
	return vkExec.temporalDepthStampValid && target != NULL
		&& depthImage != NULL && vkExec.temporalDepthStampTarget == target
		&& vkExec.temporalDepthStampFrame == backEnd.frameCount
		&& vkExec.temporalDepthStampHistoryGeneration == historyGeneration
		&& vkExec.temporalDepthStampStorageGeneration
			== static_cast<unsigned long long>(
				depthImage->GetStorageGeneration() );
}

static bool VK_TemporalPresentation_BlitSupported( VkFormat sourceFormat,
		VkFilter &filter ) {
	VkFormatProperties sourceProperties;
	VkFormatProperties destinationProperties;
	memset( &sourceProperties, 0, sizeof( sourceProperties ) );
	memset( &destinationProperties, 0, sizeof( destinationProperties ) );
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice,
		sourceFormat, &sourceProperties );
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice,
		vkCtx.swapchainFormat, &destinationProperties );
	if ( ( sourceProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) == 0
			|| ( destinationProperties.optimalTilingFeatures
				& VK_FORMAT_FEATURE_BLIT_DST_BIT ) == 0 ) {
		return false;
	}
	filter = ( sourceProperties.optimalTilingFeatures
		& VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) != 0
		? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	return true;
}

// Depth formats have no mandatory blit support, so probe before using a single
// flipped vkCmdBlitImage in place of the per-row depth copy. Source and
// destination share the depth format (the copyDepth path requires it).
static bool VK_Exec_DepthBlitSupported( VkFormat depthFormat ) {
	VkFormatProperties properties;
	memset( &properties, 0, sizeof( properties ) );
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, depthFormat, &properties );
	return ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) != 0
		&& ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) != 0;
}

static bool VK_TemporalPresentation_EnsureSceneTarget( int width, int height ) {
	const int maxDimension = Min( 32767,
		static_cast<int>( vkCtx.deviceProperties.limits.maxImageDimension2D ) );
	if ( width <= 0 || height <= 0 || width > maxDimension
			|| height > maxDimension ) {
		if ( !vkExec.temporalSceneAllocationWarned ) {
			vkExec.temporalSceneAllocationWarned = true;
			common->Warning(
				"Vulkan: temporal scene target %dx%d exceeds device/composite support; using native rendering",
				width, height );
		}
		return false;
	}

	const bool sizeChanged = vkExec.temporalSceneWidth != width
		|| vkExec.temporalSceneHeight != height;
	if ( vkExec.temporalSceneRenderTexture == NULL ) {
		idImageOpts colorOpts;
		colorOpts.format = FMT_RGBA8;
		colorOpts.colorFormat = CFM_DEFAULT;
		colorOpts.numLevels = 1;
		colorOpts.textureType = TT_2D;
		colorOpts.isPersistant = true;
		colorOpts.width = width;
		colorOpts.height = height;
		colorOpts.numMSAASamples = 0;
		vkExec.temporalSceneColorImage = globalImages->ScratchImage(
			"_vkTemporalSceneColor", &colorOpts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
		idImageOpts depthOpts = colorOpts;
		depthOpts.format = FMT_DEPTH_STENCIL;
		vkExec.temporalSceneDepthImage = globalImages->ScratchImage(
			"_vkTemporalSceneDepth", &depthOpts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
		if ( vkExec.temporalSceneColorImage != NULL
				&& vkExec.temporalSceneDepthImage != NULL ) {
			vkExec.temporalSceneRenderTexture = tr.CreateRenderTexture(
				vkExec.temporalSceneColorImage, vkExec.temporalSceneDepthImage );
		}
		if ( vkExec.temporalSceneRenderTexture != NULL ) {
			vkExec.temporalSceneRenderTexture->SetDebugLabel(
				"Vulkan temporal presentation scene" );
		}
	} else if ( sizeChanged ) {
		(void)tr.ResizeRenderTexture( vkExec.temporalSceneRenderTexture,
			width, height );
	}

	if ( vkExec.temporalSceneRenderTexture == NULL
			|| vkExec.temporalSceneRenderTexture->GetWidth() != width
			|| vkExec.temporalSceneRenderTexture->GetHeight() != height ) {
		if ( !vkExec.temporalSceneAllocationWarned ) {
			vkExec.temporalSceneAllocationWarned = true;
			common->Warning(
				"Vulkan: temporal scene target allocation failed; using native rendering" );
		}
		return false;
	}

	if ( sizeChanged ) {
		vkExec.temporalSceneWidth = width;
		vkExec.temporalSceneHeight = height;
		vkExec.temporalSceneFrame = -1;
		vkExec.temporalSceneRoot = NULL;
		vkExec.temporalScenePendingComposite = false;
		vkExec.temporalHistoryGenerationSeen =
			R_TemporalPresentation_HistoryGeneration();
	}
	vkExec.temporalSceneAllocationWarned = false;
	return true;
}

static void VK_TemporalPresentation_ResetDirectHistory(
		const char *reason, bool invalidateSharedGeneration ) {
	const unsigned int generation =
		R_TemporalPresentation_HistoryGeneration();
	if ( invalidateSharedGeneration && vkExec.temporalDirectHistoryValid
			&& vkExec.temporalDirectHistoryGeneration == generation ) {
		R_TemporalPresentation_InvalidateHistory( reason );
	}
	vkExec.temporalHistoryGenerationSeen =
		R_TemporalPresentation_HistoryGeneration();
	vkExec.temporalDirectHistoryGeneration =
		vkExec.temporalHistoryGenerationSeen;
	vkExec.temporalDirectHistoryReadIndex = 0;
	vkExec.temporalDirectHistoryFrame = -1;
	vkExec.temporalDirectHistoryViewIdentity = 0;
	vkExec.temporalDirectHistoryValid = false;
}

static bool VK_TemporalPresentation_EnsureDirectHistoryTargets(
		int width, int height ) {
	const int maxDimension = Min( 32767,
		static_cast<int>( vkCtx.deviceProperties.limits.maxImageDimension2D ) );
	if ( width <= 0 || height <= 0 || width > maxDimension
			|| height > maxDimension ) {
		return false;
	}

	const bool hadExtent = vkExec.temporalDirectHistoryWidth > 0
		&& vkExec.temporalDirectHistoryHeight > 0;
	const bool extentChanged = vkExec.temporalDirectHistoryWidth != width
		|| vkExec.temporalDirectHistoryHeight != height;
	bool resourceChanged = hadExtent && extentChanged;
	bool ready = true;

	idImageOpts opts;
	opts.format = FMT_RGBA16F;
	opts.colorFormat = CFM_DEFAULT;
	opts.numLevels = 1;
	opts.textureType = TT_2D;
	opts.isPersistant = true;
	opts.width = width;
	opts.height = height;
	opts.numMSAASamples = 0;

	for ( int i = 0; i < 2; ++i ) {
		idRenderTexture *&target =
			vkExec.temporalDirectHistoryRenderTextures[i];
		idImage *&image = vkExec.temporalDirectHistoryImages[i];
		if ( target != NULL && extentChanged ) {
			resourceChanged = true;
			(void)tr.ResizeRenderTexture( target, width, height );
		}
		if ( target == NULL ) {
			if ( image == NULL ) {
				image = globalImages->ScratchImage( i == 0
					? "_vkTemporalDirectHistory0"
					: "_vkTemporalDirectHistory1",
					&opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
			} else if ( image->GetUploadWidth() != width
					|| image->GetUploadHeight() != height ) {
				image->Resize( width, height );
			}
			if ( image != NULL ) {
				target = tr.CreateRenderTexture( image, NULL );
			}
			if ( target != NULL ) {
				target->SetDebugLabel( i == 0
					? "Vulkan direct temporal history 0 (native)"
					: "Vulkan direct temporal history 1 (native)" );
			}
		}

		if ( target == NULL || image == NULL
				|| target->GetWidth() != width
				|| target->GetHeight() != height ) {
			ready = false;
			continue;
		}
		const unsigned long long storageGeneration =
			static_cast<unsigned long long>( image->GetStorageGeneration() );
		if ( vkExec.temporalDirectHistoryStorageGenerations[i] != 0
				&& vkExec.temporalDirectHistoryStorageGenerations[i]
					!= storageGeneration ) {
			resourceChanged = true;
		}
		vkExec.temporalDirectHistoryStorageGenerations[i] = storageGeneration;
	}

	if ( !ready ) {
		VK_TemporalPresentation_ResetDirectHistory(
			"Vulkan direct temporal history resource loss", true );
		if ( !vkExec.temporalDirectHistoryAllocationWarned ) {
			vkExec.temporalDirectHistoryAllocationWarned = true;
			common->Warning(
				"Vulkan: native temporal history allocation failed; using spatial presentation" );
		}
		return false;
	}
	if ( resourceChanged ) {
		VK_TemporalPresentation_ResetDirectHistory(
			"Vulkan direct temporal history resources changed", true );
	}
	vkExec.temporalDirectHistoryWidth = width;
	vkExec.temporalDirectHistoryHeight = height;
	vkExec.temporalDirectHistoryAllocationWarned = false;
	return true;
}

static bool VK_TemporalPresentation_ResolvePendingSceneTemporal(
		const viewDef_t *viewDef ) {
	const temporalPresentationFrameState_t &presentation =
		R_TemporalPresentation_GetFrameState();
	const bool captureFrame = tr.takingScreenshot
		|| presentation.captureFrozen || presentation.captureForcedNative
		|| ( viewDef != NULL && viewDef->temporalCaptureFrame );
	if ( viewDef == NULL || !presentation.temporalAARequested || captureFrame ) {
		return false;
	}
	const int outputWidth = static_cast<int>( vkCtx.swapchainExtent.width );
	const int outputHeight = static_cast<int>( vkCtx.swapchainExtent.height );
	if ( outputWidth <= 0 || outputHeight <= 0
			|| !VK_TemporalPresentation_EnsureDirectHistoryTargets(
				outputWidth, outputHeight ) ) {
		return false;
	}

	const unsigned int generation =
		R_TemporalPresentation_HistoryGeneration();
	const bool continuity = vkExec.temporalDirectHistoryValid
		&& vkExec.temporalDirectHistoryGeneration == generation
		&& vkExec.temporalDirectHistoryViewIdentity
			== viewDef->temporalViewIdentity
		&& vkExec.temporalDirectHistoryFrame == backEnd.frameCount - 1
		&& viewDef->temporalPrepared && viewDef->temporalHistoryValid
		&& viewDef->temporalPreviousProjectionValid
		&& viewDef->temporalHistoryGeneration == generation;
	const int readIndex = vkExec.temporalDirectHistoryReadIndex;
	const int writeIndex = readIndex ^ 1;

	resolveTemporalPresentationCommand_t command;
	memset( &command, 0, sizeof( command ) );
	command.sceneColorTarget = vkExec.temporalSceneRenderTexture;
	command.sceneDepthTarget = vkExec.temporalSceneRenderTexture;
	command.historyReadTarget = continuity
		? vkExec.temporalDirectHistoryRenderTextures[readIndex] : NULL;
	command.historyWriteTarget =
		vkExec.temporalDirectHistoryRenderTextures[writeIndex];
	command.viewDef = viewDef;
	command.presentation.frameNumber = backEnd.frameCount;
	command.presentation.outputWidth = outputWidth;
	command.presentation.outputHeight = outputHeight;
	command.presentation.sceneWidth = vkExec.temporalSceneWidth;
	command.presentation.sceneHeight = vkExec.temporalSceneHeight;
	command.presentation.effectiveScalePercent =
		presentation.effectiveScalePercent;
	command.presentation.historyGeneration = generation;
	command.presentation.dynamicResolutionRequested =
		presentation.dynamicResolutionRequested;
	command.presentation.dynamicResolutionActive =
		presentation.dynamicResolutionActive;
	command.presentation.temporalAARequested = true;
	command.historyGeneration = generation;
	command.historyValid = continuity;
	command.feedback = presentation.temporalFeedback;
	command.reactiveScale = presentation.temporalReactiveScale;
	command.debugMode = presentation.temporalDebugMode;
	command.advancedScreenSpace = presentation.advancedScreenSpace;

	bool historyAdvanced = false;
	const bool presented = VK_TemporalPresentation_ResolveTargets(
		command, false, &historyAdvanced );
	if ( !presented ) {
		vkExec.temporalDirectHistoryValid = false;
		vkExec.temporalDirectHistoryFrame = -1;
		return false;
	}
	if ( historyAdvanced ) {
		vkExec.temporalDirectHistoryReadIndex = writeIndex;
		vkExec.temporalDirectHistoryGeneration =
			R_TemporalPresentation_HistoryGeneration();
		vkExec.temporalDirectHistoryViewIdentity =
			viewDef->temporalViewIdentity;
		vkExec.temporalDirectHistoryFrame = backEnd.frameCount;
		vkExec.temporalDirectHistoryValid =
			vkExec.temporalDirectHistoryGeneration == generation;
	} else if ( vkExec.temporalDirectHistoryGeneration
			!= R_TemporalPresentation_HistoryGeneration() ) {
		vkExec.temporalDirectHistoryGeneration =
			R_TemporalPresentation_HistoryGeneration();
		vkExec.temporalDirectHistoryValid = false;
		vkExec.temporalDirectHistoryFrame = -1;
	}
	return true;
}

static bool VK_TemporalPresentation_CompositePendingScene( void ) {
	if ( !vkExec.temporalScenePendingComposite ) {
		return true;
	}
	if ( !vkExec.frameOpen || vkExec.temporalSceneRenderTexture == NULL ) {
		vkExec.temporalScenePendingComposite = false;
		vkExec.temporalSceneRoot = NULL;
		R_TemporalPresentation_InvalidateHistory(
			"Vulkan temporal scene composite unavailable" );
		return false;
	}

	vkImageEntry_t *sceneColor = NULL;
	vkImageEntry_t *sceneDepth = NULL;
	if ( !VK_Exec_RenderTextureEntries( vkExec.temporalSceneRenderTexture,
			sceneColor, sceneDepth ) || sceneColor == NULL
			|| sceneColor->samples != VK_SAMPLE_COUNT_1_BIT
			|| ( sceneColor->usage & VK_IMAGE_USAGE_SAMPLED_BIT ) == 0 ) {
		return false;
	}
	if ( vkExec.activeRenderTexture != vkExec.temporalSceneRenderTexture
			&& !VK_Exec_SetRenderTarget( vkExec.temporalSceneRenderTexture ) ) {
		return false;
	}
	if ( !VK_Exec_SetRenderTarget( NULL ) ) {
		return false;
	}
	const viewDef_t *sceneRoot = vkExec.temporalSceneRoot;
	const bool depthReady = sceneDepth != NULL
		&& ( sceneDepth->usage & VK_IMAGE_USAGE_SAMPLED_BIT ) != 0;
	// Let the resolver validate the exact depth-success stamp. A missing or
	// stale depth image is a spatial/no-write result that must advance the
	// shared invalidation generation rather than silently bypassing ownership.
	const bool temporalPresented =
		VK_TemporalPresentation_ResolvePendingSceneTemporal( sceneRoot );
	const bool presented = temporalPresented
		|| VK_TemporalPresentation_DrawPendingSceneSpatial( sceneRoot,
			vkExec.temporalSceneColorImage, sceneColor,
			depthReady ? vkExec.temporalSceneDepthImage : NULL,
			depthReady ? sceneDepth : NULL );
	if ( presented ) {
		vkExec.temporalScenePendingComposite = false;
		vkExec.temporalSceneRoot = NULL;
		if ( backEnd.renderTexture == vkExec.temporalSceneRenderTexture ) {
			backEnd.renderTexture = NULL;
		}
		if ( backEnd.feedbackRenderTexture == vkExec.temporalSceneRenderTexture ) {
			backEnd.feedbackRenderTexture = NULL;
		}
		return true;
	}

	// Shader creation is capability-gated before the low-resolution target is
	// selected. A failure here is therefore an execution-time resource fault;
	// preserve a visible frame with the transfer fallback and retire history.
	if ( vkExec.temporalSceneCompositeInvalidatedFrame
			!= backEnd.frameCount ) {
		R_TemporalPresentation_InvalidateHistory(
			"Vulkan temporal spatial composite execution failed" );
		vkExec.temporalHistoryGenerationSeen =
			R_TemporalPresentation_HistoryGeneration();
		vkExec.temporalSceneCompositeInvalidatedFrame = backEnd.frameCount;
	}
	if ( sceneRoot != NULL && sceneRoot->temporalJitterEnabled ) {
		// VkImageBlit cannot express the fractional projection recenter required
		// by a jittered scene. Fail closed instead of exposing an unstable frame;
		// abandon this scene so a later frame cannot retry stale command-arena
		// view state after the source frame has already been submitted.
		vkExec.temporalScenePendingComposite = false;
		vkExec.temporalSceneRoot = NULL;
		if ( backEnd.renderTexture == vkExec.temporalSceneRenderTexture ) {
			backEnd.renderTexture = NULL;
		}
		if ( backEnd.feedbackRenderTexture
				== vkExec.temporalSceneRenderTexture ) {
			backEnd.feedbackRenderTexture = NULL;
		}
		return false;
	}
	if ( ( sceneColor->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0 ) {
		return false;
	}

	VkFilter filter = VK_FILTER_NEAREST;
	if ( !VK_TemporalPresentation_BlitSupported( sceneColor->format, filter ) ) {
		return false;
	}
	VK_Exec_EndMainRendering();
	VK_Exec_TransitionImage( sceneColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

	VkImageBlit region;
	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.srcOffsets[1].x = vkExec.temporalSceneWidth;
	region.srcOffsets[1].y = vkExec.temporalSceneHeight;
	region.srcOffsets[1].z = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.dstOffsets[1].x = static_cast<int32_t>( vkCtx.swapchainExtent.width );
	region.dstOffsets[1].y = static_cast<int32_t>( vkCtx.swapchainExtent.height );
	region.dstOffsets[1].z = 1;
	vkCmdBlitImage( vkExec.cmd, sceneColor->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vkCtx.swapchainImages[vkExec.swapImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter );

	VK_Exec_TransitionImage( sceneColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	const bool resumed = VK_Exec_BeginMainRendering( false );
	vkExec.temporalScenePendingComposite = false;
	vkExec.temporalSceneRoot = NULL;
	if ( backEnd.renderTexture == vkExec.temporalSceneRenderTexture ) {
		backEnd.renderTexture = NULL;
	}
	if ( backEnd.feedbackRenderTexture == vkExec.temporalSceneRenderTexture ) {
		backEnd.feedbackRenderTexture = NULL;
	}
	return resumed;
}

static bool VK_TemporalPresentation_BeginView( const viewDef_t *viewDef,
		vkSceneScaleState_t &scaleState ) {
	VK_TemporalPresentation_ClearScaleState( scaleState );
	VK_TemporalPresentation_ObserveHistoryAndCapture();
	const viewDef_t *rootView =
		VK_TemporalPresentation_RootForView( viewDef );
	if ( rootView == NULL ) {
		return false;
	}

	// feedbackRenderTexture is the explicit game-side opt-in: map its native
	// root view to the actual attachment extent, but do not take allocation or
	// final-post ownership away from the game render graph.
	if ( backEnd.renderTexture != NULL
			&& backEnd.renderTexture == backEnd.feedbackRenderTexture
			&& vkExec.activeRenderTexture == backEnd.renderTexture ) {
		const temporalPresentationFrameState_t &presentation =
			R_TemporalPresentation_GetFrameState();
		if ( presentation.frameNumber != backEnd.frameCount
				|| !VK_TemporalPresentation_RootCoversNativeOutput( rootView )
				|| presentation.nativeWidth != (int)vkCtx.swapchainExtent.width
				|| presentation.nativeHeight != (int)vkCtx.swapchainExtent.height
				|| presentation.sceneWidth != (int)vkExec.activeExtent.width
				|| presentation.sceneHeight != (int)vkExec.activeExtent.height ) {
			return false;
		}
		return VK_TemporalPresentation_BeginScale( scaleState, viewDef, rootView,
			(int)vkExec.activeExtent.width, (int)vkExec.activeExtent.height, false );
	}
	if ( backEnd.renderTexture != NULL || vkExec.activeRenderTexture != NULL ) {
		return false;
	}

	int targetWidth = 0;
	int targetHeight = 0;
	if ( !VK_TemporalPresentation_BackendSceneRequested( rootView,
			targetWidth, targetHeight )
			|| !VK_TemporalPresentation_EnsureSceneTarget(
				targetWidth, targetHeight ) ) {
		return false;
	}
	if ( vkExec.temporalScenePendingComposite
			&& ( vkExec.temporalSceneFrame != backEnd.frameCount
				|| vkExec.temporalSceneRoot != rootView )
			&& !VK_TemporalPresentation_CompositePendingScene() ) {
		return false;
	}
	if ( !VK_Exec_SetRenderTarget( vkExec.temporalSceneRenderTexture ) ) {
		return false;
	}
	if ( vkExec.temporalSceneFrame != backEnd.frameCount
			|| vkExec.temporalSceneRoot != rootView ) {
		VK_Exec_ClearRenderTarget( true, true, 1.0f, vkExec.clearColor );
		vkExec.temporalSceneFrame = backEnd.frameCount;
		vkExec.temporalSceneRoot = rootView;
	}
	vkExec.temporalScenePendingComposite = true;
	if ( !VK_TemporalPresentation_BeginScale( scaleState, viewDef, rootView,
			targetWidth, targetHeight, true ) ) {
		(void)VK_Exec_SetRenderTarget( NULL );
		vkExec.temporalScenePendingComposite = false;
		vkExec.temporalSceneRoot = NULL;
		return false;
	}
	return true;
}

static void VK_TemporalPresentation_EndView( const viewDef_t *viewDef,
		vkSceneScaleState_t &scaleState, bool depthComplete ) {
	VK_TemporalPresentation_RestoreDirectProjection( scaleState, viewDef );
	if ( !scaleState.active ) {
		return;
	}
	const bool backendOwned = scaleState.backendOwned;
	const bool rootView = viewDef == scaleState.rootView;
	VK_TemporalPresentation_RestoreScale( scaleState, viewDef );
	if ( !backendOwned ) {
		return;
	}
	if ( rootView ) {
		// Backend-owned scene depth is single-sample, so a completed root view is
		// the resolve seam. Stamp the exact frame, generation and storage owner;
		// an aborted view must retire any earlier resident stamp before composite.
		VK_TemporalPresentation_StampDepth(
			vkExec.temporalSceneRenderTexture, depthComplete );
		(void)VK_TemporalPresentation_CompositePendingScene();
	} else {
		(void)VK_Exec_SetRenderTarget( NULL );
	}
}

static bool VK_Exec_PrepareCopyDestination( idImage *image, int width, int height,
		vkImageEntry_t *&entry ) {
	entry = NULL;
	if ( image == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	if ( image->GetUploadWidth() != width || image->GetUploadHeight() != height ) {
		image->Resize( width, height );
	}
	entry = VK_Image_GetEntry( image->GetDeviceHandle() );
	return entry != NULL && ( entry->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) != 0;
}

bool VK_Exec_CopyRender( idImage *image, int x, int y, int width, int height,
		int cubeFace, bool copyDepth ) {
	if ( !VK_GuiExecutor_BeginFrame() || image == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	const idImageOpts &imageOpts = image->GetOpts();
	const bool targetIsCube = imageOpts.textureType == TT_CUBIC;
	if ( ( imageOpts.textureType != TT_2D && !targetIsCube )
			|| ( targetIsCube && ( cubeFace < 0 || cubeFace >= 6 ) )
			|| ( !targetIsCube && cubeFace != 0 ) ) {
		return false;
	}
	const bool targetIsDepth = imageOpts.format == FMT_DEPTH
		|| imageOpts.format == FMT_DEPTH_STENCIL;
	if ( copyDepth != targetIsDepth ) {
		return false;
	}

	VkImage sourceImage = VK_NULL_HANDLE;
	VkFormat sourceFormat = VK_FORMAT_UNDEFINED;
	VkImageAspectFlags copyAspect = copyDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	vkImageEntry_t *sourceEntry = NULL;
	if ( copyDepth ) {
		sourceEntry = vkExec.activeRenderTexture != NULL ? vkExec.activeDepthEntry : NULL;
		sourceImage = sourceEntry != NULL ? sourceEntry->image : vkCtx.depthImages[ vkExec.frameSlot ];
		sourceFormat = sourceEntry != NULL ? sourceEntry->format : vkCtx.depthFormat;
	} else {
		sourceEntry = vkExec.activeRenderTexture != NULL ? vkExec.activeColorEntry : NULL;
		sourceImage = sourceEntry != NULL ? sourceEntry->image : vkCtx.swapchainImages[ vkExec.swapImageIndex ];
		sourceFormat = sourceEntry != NULL ? sourceEntry->format : vkCtx.swapchainFormat;
	}
	if ( sourceImage == VK_NULL_HANDLE
			|| ( sourceEntry == NULL && !copyDepth && !vkCtx.swapchainTransferSrc )
			|| ( sourceEntry != NULL && ( sourceEntry->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0 )
			|| ( sourceEntry != NULL && sourceEntry->samples != VK_SAMPLE_COUNT_1_BIT ) ) {
		return false;
	}

	const int sourceWidth = VK_Exec_ActiveFramebufferWidth();
	const int sourceHeight = VK_Exec_ActiveFramebufferHeight();
	if ( x < 0 ) {
		width += x;
		x = 0;
	}
	if ( y < 0 ) {
		height += y;
		y = 0;
	}
	if ( x + width > sourceWidth ) {
		width = sourceWidth - x;
	}
	if ( y + height > sourceHeight ) {
		height = sourceHeight - y;
	}
	if ( width <= 0 || height <= 0 ) {
		return false;
	}

	// Validate aliasing before a resize/re-back can retire the destination.
	vkImageEntry_t *destination = VK_Image_GetEntry( image->GetDeviceHandle() );
	if ( destination != NULL && destination->image == sourceImage ) {
		return false;
	}
	if ( copyDepth ) {
		if ( !VK_Image_MakeDepthCopyTarget( image, width, height, sourceFormat ) ) {
			return false;
		}
		destination = VK_Image_GetEntry( image->GetDeviceHandle() );
	} else if ( !VK_Exec_PrepareCopyDestination( image, width, height, destination ) ) {
		return false;
	}
	if ( destination == NULL || destination->image == sourceImage
			|| destination->samples != VK_SAMPLE_COUNT_1_BIT
			|| ( destination->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) == 0
			|| ( copyDepth && destination->format != sourceFormat )
			|| destination->isCube != targetIsCube
			|| destination->numLayers != ( targetIsCube ? 6 : 1 ) ) {
		return false;
	}

	VK_Exec_EndMainRendering();
	if ( sourceEntry != NULL ) {
		VK_Exec_TransitionImage( sourceEntry, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	} else if ( !copyDepth ) {
		if ( !vkCtx.swapchainTransferSrc ) {
			VK_Exec_BeginMainRendering( false );
			return false;
		}
		VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	} else {
		VK_Exec_TransitionSwapchainDepth( VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	}
	VK_Exec_TransitionImage( destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

	if ( copyDepth ) {
		if ( VK_Exec_DepthBlitSupported( sourceFormat ) ) {
			// One flipped NEAREST blit reproduces the per-row copy's GL
			// bottom-left orientation texel-exactly (identical format + NEAREST),
			// without allocating a per-row region array. Same offset convention
			// as the colour blit path below.
			VkImageBlit depthBlit;
			memset( &depthBlit, 0, sizeof( depthBlit ) );
			depthBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depthBlit.srcSubresource.layerCount = 1;
			depthBlit.srcOffsets[ 0 ].x = x;
			depthBlit.srcOffsets[ 0 ].y = sourceHeight - y;
			depthBlit.srcOffsets[ 1 ].x = x + width;
			depthBlit.srcOffsets[ 1 ].y = sourceHeight - y - height;
			depthBlit.srcOffsets[ 1 ].z = 1;
			depthBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depthBlit.dstSubresource.baseArrayLayer = targetIsCube ? (uint32_t)cubeFace : 0;
			depthBlit.dstSubresource.layerCount = 1;
			depthBlit.dstOffsets[ 1 ].x = width;
			depthBlit.dstOffsets[ 1 ].y = height;
			depthBlit.dstOffsets[ 1 ].z = 1;
			vkCmdBlitImage( vkExec.cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &depthBlit, VK_FILTER_NEAREST );
		} else {
			// Depth blit unsupported on this device: emit one exact-format copy
			// per row to preserve the GL bottom-left capture orientation.
			VkImageCopy *rows = (VkImageCopy *)Mem_Alloc( height * sizeof( VkImageCopy ) );
			if ( rows == NULL ) {
				VK_Exec_TransitionImage( destination, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
				if ( sourceEntry != NULL ) {
					VK_Exec_TransitionImage( sourceEntry, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
				} else {
					VK_Exec_TransitionSwapchainDepth( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
				}
				VK_Exec_BeginMainRendering( false );
				return false;
			}
			memset( rows, 0, height * sizeof( VkImageCopy ) );
			for ( int row = 0; row < height; row++ ) {
				rows[ row ].srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				rows[ row ].srcSubresource.layerCount = 1;
				rows[ row ].srcOffset.x = x;
				rows[ row ].srcOffset.y = sourceHeight - 1 - ( y + row );
				rows[ row ].dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				rows[ row ].dstSubresource.baseArrayLayer = targetIsCube
					? (uint32_t)cubeFace : 0;
				rows[ row ].dstSubresource.layerCount = 1;
				rows[ row ].dstOffset.y = row;
				rows[ row ].extent.width = (uint32_t)width;
				rows[ row ].extent.height = 1;
				rows[ row ].extent.depth = 1;
			}
			vkCmdCopyImage( vkExec.cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					(uint32_t)height, rows );
			Mem_Free( rows );
		}

		destination->everUploaded = true;
		VK_Exec_TransitionImage( destination, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		if ( sourceEntry != NULL ) {
			VK_Exec_TransitionImage( sourceEntry, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
		} else {
			VK_Exec_TransitionSwapchainDepth( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
		}
		VK_Exec_BeginMainRendering( false );
		return true;
	}

	VkImageBlit region;
	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = copyAspect;
	region.srcSubresource.layerCount = 1;
	region.srcOffsets[ 0 ].x = x;
	region.srcOffsets[ 0 ].y = sourceHeight - y;
	region.srcOffsets[ 1 ].x = x + width;
	region.srcOffsets[ 1 ].y = sourceHeight - y - height;
	region.srcOffsets[ 1 ].z = 1;
	region.dstSubresource.aspectMask = copyAspect;
	region.dstSubresource.baseArrayLayer = targetIsCube ? (uint32_t)cubeFace : 0;
	region.dstSubresource.layerCount = 1;
	region.dstOffsets[ 1 ].x = width;
	region.dstOffsets[ 1 ].y = height;
	region.dstOffsets[ 1 ].z = 1;
	vkCmdBlitImage( vkExec.cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST );

	destination->everUploaded = true;
	VK_Exec_TransitionImage( destination, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	if ( sourceEntry != NULL ) {
		VK_Exec_TransitionImage( sourceEntry, copyDepth
				? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				: VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	} else if ( !copyDepth ) {
		VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	} else {
		VK_Exec_TransitionSwapchainDepth( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
	}
	VK_Exec_BeginMainRendering( false );
	return true;
}

static bool VK_Exec_AutomaticCaptureAllowed( void ) {
	return backEnd.renderTexture == NULL
			|| ( backEnd.feedbackRenderTexture != NULL
				&& backEnd.renderTexture == backEnd.feedbackRenderTexture );
}

static bool VK_Exec_ImageIsCurrentRender( const idImage *image ) {
	if ( image == NULL || globalImages == NULL ) {
		return false;
	}
	if ( image == globalImages->currentRenderImage
			|| image == globalImages->originalCurrentRenderImage ) {
		return true;
	}
	const char *name = image->GetName();
	return name != NULL && idStr::Icmpn( name, "_currentRender", 14 ) == 0;
}

static bool VK_Exec_ImageIsCurrentDepth( const idImage *image ) {
	if ( image == NULL || globalImages == NULL ) {
		return false;
	}
	if ( image == globalImages->currentDepthImage ) {
		return true;
	}
	const char *name = image->GetName();
	return name != NULL && idStr::Icmpn( name, "_currentDepth", 13 ) == 0;
}

static bool VK_Exec_StageUsesCurrentRender( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( VK_Exec_ImageIsCurrentRender( stage->texture.image ) ) {
		return true;
	}
	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}
	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( VK_Exec_ImageIsCurrentRender( newStage->fragmentProgramImages[ i ] ) ) {
			return true;
		}
	}
	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( VK_Exec_ImageIsCurrentRender( newStage->shaderTextureImages[ i ] ) ) {
			return true;
		}
	}
	return false;
}

static bool VK_Exec_StageUsesCurrentDepth( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( VK_Exec_ImageIsCurrentDepth( stage->texture.image ) ) {
		return true;
	}
	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}
	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( VK_Exec_ImageIsCurrentDepth( newStage->fragmentProgramImages[ i ] ) ) {
			return true;
		}
	}
	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( VK_Exec_ImageIsCurrentDepth( newStage->shaderTextureImages[ i ] ) ) {
			return true;
		}
	}
	return false;
}

static bool VK_Exec_MaterialUsesCurrentDepth( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	for ( int i = 0; i < material->GetNumStages(); i++ ) {
		if ( VK_Exec_StageUsesCurrentDepth( material->GetStage( i ) ) ) {
			return true;
		}
	}
	return false;
}

static bool VK_Exec_CaptureCurrentRender( const viewDef_t *viewDef ) {
	if ( globalImages == NULL || globalImages->currentRenderImage == NULL
			|| viewDef == NULL ) {
		return false;
	}
	const int width = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int height = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	idImage *sceneImage = globalImages->currentRenderImage;
	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->GetNumColorImages() > 0
			&& backEnd.renderTexture->GetColorImage( 0 ) == sceneImage ) {
		static bool warnedColorAlias = false;
		if ( !warnedColorAlias ) {
			warnedColorAlias = true;
			common->Warning( "Vulkan: refusing _currentRender feedback from the active color attachment" );
		}
		return false;
	}
	if ( !VK_Exec_CopyRender( sceneImage, viewDef->viewport.x1, viewDef->viewport.y1,
			width, height, 0, false ) ) {
		return false;
	}
	backEnd.currentRenderCopied = true;
	return true;
}

static bool VK_Exec_CaptureCurrentDepth( const viewDef_t *viewDef ) {
	if ( globalImages == NULL || globalImages->currentDepthImage == NULL
			|| viewDef == NULL ) {
		return false;
	}
	const int width = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int height = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	idImage *depthImage = globalImages->currentDepthImage;
	if ( backEnd.renderTexture != NULL
			&& backEnd.renderTexture->GetDepthImage() == depthImage ) {
		static bool warnedDepthAlias = false;
		if ( !warnedDepthAlias ) {
			warnedDepthAlias = true;
			common->Warning( "Vulkan: refusing _currentDepth feedback from the active depth attachment" );
		}
		return false;
	}
	if ( !VK_Exec_CopyRender( depthImage, viewDef->viewport.x1, viewDef->viewport.y1,
			width, height, 0, true ) ) {
		return false;
	}
	backEnd.currentDepthCopied = true;
	return true;
}

static VkResolveModeFlagBits VK_Exec_DepthResolveMode( void ) {
	VkPhysicalDeviceDepthStencilResolveProperties resolveProperties;
	memset( &resolveProperties, 0, sizeof( resolveProperties ) );
	resolveProperties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;

	VkPhysicalDeviceProperties2 properties;
	memset( &properties, 0, sizeof( properties ) );
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &resolveProperties;
	vkGetPhysicalDeviceProperties2( vkCtx.physicalDevice, &properties );

	// SAMPLE_ZERO most closely matches the implementation-selected sample
	// produced by the legacy framebuffer depth blit. Prefer MIN next so
	// silhouettes retain the nearest covered surface for depth-aware effects.
	const VkResolveModeFlags modes = resolveProperties.supportedDepthResolveModes;
	if ( ( modes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT ) != 0 ) {
		return VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
	}
	if ( ( modes & VK_RESOLVE_MODE_MIN_BIT ) != 0 ) {
		return VK_RESOLVE_MODE_MIN_BIT;
	}
	if ( ( modes & VK_RESOLVE_MODE_MAX_BIT ) != 0 ) {
		return VK_RESOLVE_MODE_MAX_BIT;
	}
	if ( ( modes & VK_RESOLVE_MODE_AVERAGE_BIT ) != 0 ) {
		return VK_RESOLVE_MODE_AVERAGE_BIT;
	}
	return VK_RESOLVE_MODE_NONE;
}

static void VK_Exec_DepthResolveBarrier( vkImageEntry_t *sourceDepth,
		vkImageEntry_t *destinationDepth, bool beforeResolve ) {
	VkImageMemoryBarrier2 barriers[ 2 ];
	memset( barriers, 0, sizeof( barriers ) );

	for ( int i = 0; i < 2; i++ ) {
		vkImageEntry_t *entry = i == 0 ? sourceDepth : destinationDepth;
		VkImageMemoryBarrier2 &barrier = barriers[ i ];
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.image = entry->image;
		barrier.subresourceRange.aspectMask =
				VK_Exec_BarrierAspectMask( entry );
		barrier.subresourceRange.levelCount = (uint32_t)entry->numMips;
		barrier.subresourceRange.layerCount = (uint32_t)entry->numLayers;

		if ( beforeResolve ) {
			VK_Exec_LayoutAccess( entry->layout,
					barrier.srcStageMask, barrier.srcAccessMask );
			barrier.dstStageMask = i == 0
					? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
							| VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
					: VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = i == 0
					? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
							| VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
					: VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
							| VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout = entry->layout;
			barrier.newLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		} else {
			barrier.srcStageMask = i == 0
					? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
							| VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
					: VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.srcAccessMask = i == 0
					? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
							| VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
					: VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
							| VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
					| VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			barrier.oldLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}

	VkDependencyInfo dependency;
	memset( &dependency, 0, sizeof( dependency ) );
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 2;
	dependency.pImageMemoryBarriers = barriers;
	vkCmdPipelineBarrier2( vkExec.cmd, &dependency );

	sourceDepth->layout = beforeResolve
			? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	destinationDepth->layout = sourceDepth->layout;
}

static bool VK_Exec_ResolveDepthImage( vkImageEntry_t *sourceDepth,
		vkImageEntry_t *destinationDepth, uint32_t width, uint32_t height ) {
	if ( sourceDepth == NULL || destinationDepth == NULL
			|| sourceDepth->format != destinationDepth->format
			|| sourceDepth->image == destinationDepth->image
			|| width == 0 || height == 0 ) {
		return false;
	}

	if ( sourceDepth->samples == VK_SAMPLE_COUNT_1_BIT
			&& destinationDepth->samples == VK_SAMPLE_COUNT_1_BIT ) {
		if ( ( sourceDepth->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0
				|| ( destinationDepth->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) == 0 ) {
			return false;
		}
		VK_Exec_TransitionImage( sourceDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
		VK_Exec_TransitionImage( destinationDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

		VkImageCopy region;
		memset( &region, 0, sizeof( region ) );
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;
		vkCmdCopyImage( vkExec.cmd,
				sourceDepth->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				destinationDepth->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &region );
	} else if ( sourceDepth->samples != VK_SAMPLE_COUNT_1_BIT
			&& destinationDepth->samples == VK_SAMPLE_COUNT_1_BIT ) {
		const VkResolveModeFlagBits resolveMode = VK_Exec_DepthResolveMode();
		if ( resolveMode == VK_RESOLVE_MODE_NONE ) {
			return false;
		}

		// The fixed-function depth resolve is synchronized in the color-output
		// domain, even though both images use depth/stencil attachment layouts.
		// Resolve-specific barriers avoid treating its destination write as a
		// late-fragment depth-test write.
		VK_Exec_DepthResolveBarrier(
				sourceDepth, destinationDepth, true );

		VkRenderingAttachmentInfo depthAttachment;
		memset( &depthAttachment, 0, sizeof( depthAttachment ) );
		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = sourceDepth->attachmentView;
		depthAttachment.imageLayout =
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.resolveMode = resolveMode;
		depthAttachment.resolveImageView = destinationDepth->attachmentView;
		depthAttachment.resolveImageLayout =
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderingInfo;
		memset( &renderingInfo, 0, sizeof( renderingInfo ) );
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.extent.width = width;
		renderingInfo.renderArea.extent.height = height;
		renderingInfo.layerCount = 1;
		renderingInfo.pDepthAttachment = &depthAttachment;
		vkCmdBeginRendering( vkExec.cmd, &renderingInfo );
		vkCmdEndRendering( vkExec.cmd );

		VK_Exec_DepthResolveBarrier(
				sourceDepth, destinationDepth, false );
		sourceDepth->everUploaded = true;
		destinationDepth->everUploaded = true;
		return true;
	} else {
		return false;
	}

	sourceDepth->everUploaded = true;
	destinationDepth->everUploaded = true;
	VK_Exec_TransitionImage( sourceDepth,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_Exec_TransitionImage( destinationDepth,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	return true;
}

bool VK_Exec_ResolveRenderTargets( idRenderTexture *sourceRenderTexture,
		idRenderTexture *destinationRenderTexture, bool resolveDepth ) {
	if ( resolveDepth ) {
		// Retire a resident success stamp before any validation or copy work. A
		// failed MSAA depth resolve must never make stale depth look current.
		VK_TemporalPresentation_StampDepth(
			destinationRenderTexture, false );
	}
	if ( !VK_GuiExecutor_BeginFrame() || sourceRenderTexture == NULL
			|| destinationRenderTexture == NULL ) {
		return false;
	}
	vkImageEntry_t *sourceColor = NULL;
	vkImageEntry_t *sourceDepth = NULL;
	vkImageEntry_t *destinationColor = NULL;
	vkImageEntry_t *destinationDepth = NULL;
	if ( !VK_Exec_RenderTextureEntries( sourceRenderTexture, sourceColor, sourceDepth )
			|| !VK_Exec_RenderTextureEntries( destinationRenderTexture, destinationColor, destinationDepth )
			|| sourceColor->format != destinationColor->format
			|| sourceColor->image == destinationColor->image
			|| ( sourceColor->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0
			|| ( destinationColor->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) == 0 ) {
		return false;
	}

	const uint32_t width = (uint32_t)Min( sourceColor->width, destinationColor->width );
	const uint32_t height = (uint32_t)Min( sourceColor->height, destinationColor->height );
	VK_Exec_EndMainRendering();
	VK_Exec_TransitionImage( sourceColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_Exec_TransitionImage( destinationColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

	if ( sourceColor->samples != VK_SAMPLE_COUNT_1_BIT
			&& destinationColor->samples == VK_SAMPLE_COUNT_1_BIT ) {
		VkImageResolve region;
		memset( &region, 0, sizeof( region ) );
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;
		vkCmdResolveImage( vkExec.cmd, sourceColor->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				destinationColor->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	} else if ( sourceColor->samples == VK_SAMPLE_COUNT_1_BIT
			&& destinationColor->samples == VK_SAMPLE_COUNT_1_BIT ) {
		VkImageCopy region;
		memset( &region, 0, sizeof( region ) );
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;
		vkCmdCopyImage( vkExec.cmd,
				sourceColor->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				destinationColor->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &region );
	} else {
		VK_Exec_TransitionImage( sourceColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		VK_Exec_TransitionImage( destinationColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		VK_Exec_TransitionActiveTargetToAttachments();
		VK_Exec_BeginMainRendering( false );
		return false;
	}

	sourceColor->everUploaded = true;
	destinationColor->everUploaded = true;
	VK_Exec_TransitionImage( sourceColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_Exec_TransitionImage( destinationColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

	bool depthResolved = true;
	if ( resolveDepth ) {
		const uint32_t depthWidth = sourceDepth != NULL && destinationDepth != NULL
				? (uint32_t)Min( sourceDepth->width, destinationDepth->width ) : 0;
		const uint32_t depthHeight = sourceDepth != NULL && destinationDepth != NULL
				? (uint32_t)Min( sourceDepth->height, destinationDepth->height ) : 0;
		depthResolved = VK_Exec_ResolveDepthImage( sourceDepth, destinationDepth,
				depthWidth, depthHeight );
		if ( !depthResolved ) {
			static bool warnedDepthResolve = false;
			if ( !warnedDepthResolve ) {
				warnedDepthResolve = true;
				common->Warning( "Vulkan: requested depth resolve failed; color resolve completed" );
			}
		}
		VK_TemporalPresentation_StampDepth(
			destinationRenderTexture, depthResolved );
	}

	VK_Exec_TransitionActiveTargetToAttachments();
	VK_Exec_BeginMainRendering( false );
	return depthResolved;
}

/*
====================
VK_GuiExecutor_ReadPixels

components is 3 (GL_RGB) or 4 (GL_RGBA). Both are needed: R_ReadTiledPixels
reads GL_RGBA everywhere since the ES screenshot fix, because OpenGL ES
guarantees only that pair for the default framebuffer. This entry point
accepted GL_RGB alone until 2026-08-10, so every Vulkan screenshot after that
change came back black with a warning -- the same silent-black failure the ES
fix was written to cure, moved to a different backend.
====================
*/
bool VK_GuiExecutor_ReadPixels( int x, int y, int width, int height, void *pixels, int components ) {
	if ( components != 3 && components != 4 ) {
		return false;
	}
	if ( pixels == NULL || width <= 0 || height <= 0 || !VK_GuiExecutor_BeginFrame()
			|| !vkCtx.swapchainTransferSrc ) {
		return false;
	}
	VK_TemporalPresentation_ObserveHistoryAndCapture();
	if ( vkExec.temporalScenePendingComposite
			&& !VK_TemporalPresentation_CompositePendingScene() ) {
		return false;
	}
	if ( vkExec.activeRenderTexture != NULL && !VK_Exec_SetRenderTarget( NULL ) ) {
		return false;
	}

	const int framebufferWidth = (int)vkCtx.swapchainExtent.width;
	const int framebufferHeight = (int)vkCtx.swapchainExtent.height;
	if ( x < 0 ) {
		width += x;
		x = 0;
	}
	if ( y < 0 ) {
		height += y;
		y = 0;
	}
	if ( x + width > framebufferWidth ) {
		width = framebufferWidth - x;
	}
	if ( y + height > framebufferHeight ) {
		height = framebufferHeight - y;
	}
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	const bool bgra = vkCtx.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM
			|| vkCtx.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
	const bool rgba = vkCtx.swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM
			|| vkCtx.swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB;
	if ( !bgra && !rgba ) {
		common->Warning( "Vulkan: screenshot readback does not support swapchain format %d",
				(int)vkCtx.swapchainFormat );
		return false;
	}

	const VkDeviceSize readbackBytes = (VkDeviceSize)width * (VkDeviceSize)height * 4;
	VkBufferCreateInfo bci;
	memset( &bci, 0, sizeof( bci ) );
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = readbackBytes;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer readbackBuffer = VK_NULL_HANDLE;
	VmaAllocation readbackAllocation = NULL;
	VmaAllocationInfo allocationInfo;
	memset( &allocationInfo, 0, sizeof( allocationInfo ) );
	if ( vmaCreateBuffer( vkCtx.allocator, &bci, &vaci, &readbackBuffer,
			&readbackAllocation, &allocationInfo ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: screenshot readback buffer allocation failed" );
		return false;
	}

	VK_Exec_EndMainRendering();
	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

	VkBufferImageCopy copy;
	memset( &copy, 0, sizeof( copy ) );
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset.x = x;
	copy.imageOffset.y = framebufferHeight - y - height;
	copy.imageExtent.width = (uint32_t)width;
	copy.imageExtent.height = (uint32_t)height;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer( vkExec.cmd, vkCtx.swapchainImages[ vkExec.swapImageIndex ],
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &copy );

	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	const int submittedSlot = vkExec.frameSlot;
	// CaptureRenderToFile deliberately calls R_IssueRenderCommands with
	// tr.takingScreenshot set so the GL backend leaves this crop in the back
	// buffer for the real frame to replace. Presenting here exposes the
	// save-preview crop (normally 320x240) for one frame. Submit it only for
	// the synchronous readback, then resume recording against this acquired
	// swapchain image for the actual frame.
	const bool resumeAfterReadback = tr.takingScreenshot;
	R_RendererMetrics_ResetGpuFrameTiming( "Vulkan synchronous screenshot readback" );
	if ( !VK_GuiExecutor_SubmitFrame( !resumeAfterReadback ) ) {
		vmaDestroyBuffer( vkCtx.allocator, readbackBuffer, readbackAllocation );
		return false;
	}
	const VkResult readbackFenceResult = vkWaitForFences( vkCtx.device, 1,
		&vkCtx.frameFences[ submittedSlot ], VK_TRUE, UINT64_MAX );
	if ( readbackFenceResult != VK_SUCCESS ) {
		VK_GpuFrameTiming_SubmitFailed( submittedSlot );
		common->Warning( "Vulkan: screenshot readback fence wait failed (%d)",
			static_cast<int>( readbackFenceResult ) );
		vmaDestroyBuffer( vkCtx.allocator, readbackBuffer, readbackAllocation );
		return false;
	}
	vmaInvalidateAllocation( vkCtx.allocator, readbackAllocation, 0, VK_WHOLE_SIZE );

	const byte *source = (const byte *)allocationInfo.pMappedData;
	byte *destination = (byte *)pixels;
	// GL pads rows to dword boundaries, and R_ReadTiledPixels indexes the
	// result with exactly this stride
	const int destinationStride = ( width * components + 3 ) & ~3;
	for ( int row = 0; row < height; row++ ) {
		const byte *sourceRow = source + (size_t)( height - 1 - row ) * (size_t)width * 4;
		byte *destinationRow = destination + (size_t)row * (size_t)destinationStride;
		for ( int column = 0; column < width; column++ ) {
			const byte *sourcePixel = sourceRow + column * 4;
			byte *destinationPixel = destinationRow + column * components;
			destinationPixel[ 0 ] = sourcePixel[ bgra ? 2 : 0 ];
			destinationPixel[ 1 ] = sourcePixel[ 1 ];
			destinationPixel[ 2 ] = sourcePixel[ bgra ? 0 : 2 ];
			if ( components == 4 ) {
				// the swapchain is opaque; a captured alpha of 0 would make the
				// whole shot transparent in any format that keeps the channel
				destinationPixel[ 3 ] = 255;
			}
		}
		if ( destinationStride > width * components ) {
			memset( destinationRow + width * components, 0,
					(size_t)( destinationStride - width * components ) );
		}
	}

	vmaDestroyBuffer( vkCtx.allocator, readbackBuffer, readbackAllocation );
	if ( resumeAfterReadback ) {
		VkCommandBufferBeginInfo cbbi;
		memset( &cbbi, 0, sizeof( cbbi ) );
		cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		// Reset the command buffer and begin recording BEFORE resetting the slot
		// fence: if the command-buffer reset/begin fails, the fence stays
		// signaled so the next BeginFrame's fence wait on this slot cannot
		// deadlock on a fence that will never be submitted.
		if ( vkResetCommandBuffer( vkExec.cmd, 0 ) != VK_SUCCESS
				|| vkBeginCommandBuffer( vkExec.cmd, &cbbi ) != VK_SUCCESS
				|| vkResetFences( vkCtx.device, 1, &vkCtx.frameFences[ submittedSlot ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: failed to resume rendering after screenshot readback" );
			return false;
		}
		vkExec.frameOpen = true;
		vkExec.mainScopeOpen = false;
		// The screenshot path explicitly retired this same slot; start a fresh
		// timing epoch for the real frame recorded after the crop.
		VK_GpuFrameTiming_BeginFrame( vkExec.cmd, submittedSlot, tr.frameCount );
		VK_Exec_BeginMainRendering( true );
	}
	return true;
}

static bool VK_GuiExecutor_SubmitFrame( bool present ) {
	if ( !vkExec.frameOpen ) {
		return false;
	}
	const int slot = vkExec.frameSlot;
	const uint32_t imageIndex = vkExec.swapImageIndex;
	VkCommandBuffer cmd = vkExec.cmd;

	// submit any batched image uploads first: same-queue submission order makes
	// them execute before this frame samples the images, and this frame's fence
	// then covers them for deferred-destroy retirement
	VK_Device_FlushUploadBatch();

	VK_Exec_EndMainRendering();
	VK_Exec_TransitionActiveTargetToSampled();

	if ( present ) {
		VkImageMemoryBarrier2 toPresent;
		memset( &toPresent, 0, sizeof( toPresent ) );
		toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		toPresent.image = vkCtx.swapchainImages[ imageIndex ];
		toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toPresent.subresourceRange.levelCount = 1;
		toPresent.subresourceRange.layerCount = 1;
		VkDependencyInfo dep;
		memset( &dep, 0, sizeof( dep ) );
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &toPresent;
		vkCmdPipelineBarrier2( cmd, &dep );
	}

	VK_GpuFrameTiming_EndFrame( cmd, slot );
	vkEndCommandBuffer( cmd );

	// Flush each ring's host-written prefix once, before the queue submit that
	// consumes it, instead of once per allocation. vmaFlushAllocation rounds to
	// nonCoherentAtomSize and clamps to the allocation, and is a no-op on the
	// HOST_COHERENT memory VMA hands us on desktop.
	{
		vkRing_t *frameRings[ 3 ] = {
			&vkExec.vertexRings[ slot ], &vkExec.indexRings[ slot ], &vkExec.uniformRings[ slot ] };
		for ( int r = 0; r < 3; r++ ) {
			if ( frameRings[ r ]->mapped != NULL && frameRings[ r ]->cursor > 0 ) {
				const VkResult flushResult = vmaFlushAllocation( vkCtx.allocator,
						frameRings[ r ]->allocation, 0, (VkDeviceSize)frameRings[ r ]->cursor );
				if ( flushResult != VK_SUCCESS ) {
					common->Warning( "Vulkan: frame ring flush failed (%d)", (int)flushResult );
				}
			}
		}
	}

	VkSemaphoreSubmitInfo waitInfo;
	memset( &waitInfo, 0, sizeof( waitInfo ) );
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = vkCtx.acquireSemaphores[ slot ];
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSemaphoreSubmitInfo signalInfo;
	memset( &signalInfo, 0, sizeof( signalInfo ) );
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = vkCtx.renderFinishedSemaphores[ imageIndex ];
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkCommandBufferSubmitInfo cmdInfo;
	memset( &cmdInfo, 0, sizeof( cmdInfo ) );
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = cmd;
	VkSubmitInfo2 si;
	memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	si.waitSemaphoreInfoCount = vkExec.acquireWaitPending ? 1 : 0;
	si.pWaitSemaphoreInfos = vkExec.acquireWaitPending ? &waitInfo : NULL;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &cmdInfo;
	si.signalSemaphoreInfoCount = present ? 1 : 0;
	si.pSignalSemaphoreInfos = present ? &signalInfo : NULL;
	if ( vkQueueSubmit2( vkCtx.graphicsQueue, 1, &si, vkCtx.frameFences[ slot ] ) != VK_SUCCESS ) {
		VK_GpuFrameTiming_SubmitFailed( slot );
		// The slot fence was reset in BeginFrame; a failed submit never signals
		// it, so a later vkWaitForFences on this slot (next BeginFrame, or a
		// screenshot readback) would block forever. The failed submit enqueued
		// nothing, so the reset fence is idle and can be replaced with a fresh
		// signaled one. Swap through a temporary so a create failure leaves the
		// old handle intact rather than a NULL handle. Also clear the frame-open
		// state: the command buffer is already ended, so the next BeginFrame must
		// start fresh rather than resume recording into it.
		vkExec.acquireWaitPending = false;
		vkExec.frameOpen = false;
		VkFenceCreateInfo fci;
		memset( &fci, 0, sizeof( fci ) );
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		VkFence recoveredFence = VK_NULL_HANDLE;
		if ( vkCreateFence( vkCtx.device, &fci, NULL, &recoveredFence ) == VK_SUCCESS ) {
			vkDestroyFence( vkCtx.device, vkCtx.frameFences[ slot ], NULL );
			vkCtx.frameFences[ slot ] = recoveredFence;
		}
		common->Warning( "Vulkan: frame submit failed on slot %d; recovered frame state", slot );
		return false;
	}
	vkExec.acquireWaitPending = false;
	vkExec.frameOpen = false;
	if ( !present ) {
		return true;
	}

	VkPresentInfoKHR pi;
	memset( &pi, 0, sizeof( pi ) );
	pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &vkCtx.renderFinishedSemaphores[ imageIndex ];
	pi.swapchainCount = 1;
	pi.pSwapchains = &vkCtx.swapchain;
	pi.pImageIndices = &imageIndex;
	const VkResult res = vkQueuePresentKHR( vkCtx.graphicsQueue, &pi );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ) {
		VK_Device_RecreateSwapchain();
	}
	return true;
}

bool VK_GuiExecutor_EndFrameAndPresent( void ) {
	return VK_GuiExecutor_SubmitFrame( true );
}

/*
====================
Shared surface helpers (2D + world views)
====================
*/

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
/*
====================
VK_Exec_PackedLightIndexesHeadered

Packed MD5R light tris store one emitted-index count per primitive batch
before the actual light-triangle index stream. tri->numIndexes deliberately
excludes those header words. Validate the complete header before treating the
array as headered so the classic flat fallback from R_CreateLightTris remains
usable when the packed builder declined a surface.
====================
*/
static bool VK_Exec_PackedLightIndexesHeadered( const srfTriangles_t *tri,
		const rvMD5RMesh *mesh ) {
	if ( tri == NULL || mesh == NULL || tri->ambientSurface == NULL
			|| tri->indexes == NULL || tri->numIndexes <= 0 ) {
		return false;
	}

	const int numBatches = mesh->primBatches.Num();
	if ( numBatches <= 0 || tri->numIndexes > INT_MAX - numBatches
			|| tri->numAllocedIndices < tri->numIndexes + numBatches ) {
		return false;
	}

	int headerIndexCount = 0;
	for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ ) {
		const unsigned int encodedCount = tri->indexes[ batchIndex ];
		if ( encodedCount > static_cast<unsigned int>( INT_MAX ) ) {
			return false;
		}
		const int batchIndexCount = static_cast<int>( encodedCount );
		if ( ( batchIndexCount % 3 ) != 0
				|| batchIndexCount > tri->numIndexes - headerIndexCount ) {
			return false;
		}
		headerIndexCount += batchIndexCount;
	}

	return headerIndexCount == tri->numIndexes;
}

/*
====================
VK_Exec_FlattenPackedLightIndexes

The packed light-triangle stream addresses vertices in the source MD5R draw
buffer. Vulkan's compatibility cache concatenates each batch into one
idDrawVert array, so remap the source ranges to that flattened cache.
====================
*/
static bool VK_Exec_FlattenPackedLightIndexes( const srfTriangles_t *tri,
		const rvMD5RMesh *mesh, glIndex_t *flattened ) {
	if ( tri == NULL || mesh == NULL || flattened == NULL
			|| !VK_Exec_PackedLightIndexesHeadered( tri, mesh ) ) {
		return false;
	}

	const int numBatches = mesh->primBatches.Num();
	const glIndex_t *batchHeader = tri->indexes;
	const glIndex_t *batchIndexes = tri->indexes + numBatches;
	int destVertexBase = 0;
	int destIndexBase = 0;

	for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ ) {
		const rvMD5RPrimBatch &batch = mesh->primBatches[ batchIndex ];
		const int batchIndexCount = static_cast<int>( batchHeader[ batchIndex ] );
		if ( !batch.hasDrawGeoSpec
				|| batch.drawGeoSpec.vertexStart < 0
				|| batch.drawGeoSpec.vertexCount < 0
				|| batch.drawGeoSpec.vertexCount > tri->numVerts - destVertexBase ) {
			return false;
		}

		const int sourceVertexStart = batch.drawGeoSpec.vertexStart;
		if ( batch.drawGeoSpec.vertexCount > INT_MAX - sourceVertexStart ) {
			return false;
		}
		const int sourceVertexEnd = sourceVertexStart + batch.drawGeoSpec.vertexCount;
		for ( int index = 0 ; index < batchIndexCount ; index++ ) {
			const unsigned int encodedIndex = batchIndexes[ index ];
			if ( encodedIndex > static_cast<unsigned int>( INT_MAX ) ) {
				return false;
			}
			const int sourceIndex = static_cast<int>( encodedIndex );
			if ( sourceIndex < sourceVertexStart || sourceIndex >= sourceVertexEnd ) {
				return false;
			}
			flattened[ destIndexBase + index ] = static_cast<glIndex_t>(
					destVertexBase + sourceIndex - sourceVertexStart );
		}

		batchIndexes += batchIndexCount;
		destIndexBase += batchIndexCount;
		destVertexBase += batch.drawGeoSpec.vertexCount;
	}

	return destIndexBase == tri->numIndexes && destVertexBase == tri->numVerts;
}

/*
====================
VK_Exec_FlattenPackedAmbientIndexes

Frame-temp packed caster caches can be vertex-only when the legacy
r_useIndexBuffers cvar is disabled. Reconstruct the full ambient index stream
directly from the decoded MD5R draw buffer in that case.
====================
*/
static bool VK_Exec_FlattenPackedAmbientIndexes( const srfTriangles_t *tri,
		const rvMD5RMesh *mesh, glIndex_t *flattened ) {
	if ( tri == NULL || mesh == NULL || flattened == NULL
			|| tri->ambientSurface != NULL ) {
		return false;
	}

	const rvMD5RIndexBufferDesc *drawIndexBuffer =
			R_MD5R_GetDrawIndexBufferForTri( tri );
	if ( drawIndexBuffer == NULL || drawIndexBuffer->numIndices <= 0
			|| drawIndexBuffer->indices.Num() != drawIndexBuffer->numIndices ) {
		return false;
	}

	int destVertexBase = 0;
	int destIndexBase = 0;
	for ( int batchIndex = 0 ; batchIndex < mesh->primBatches.Num() ; batchIndex++ ) {
		const rvMD5RPrimBatch &batch = mesh->primBatches[ batchIndex ];
		if ( !batch.hasDrawGeoSpec
				|| batch.drawGeoSpec.vertexStart < 0
				|| batch.drawGeoSpec.vertexCount < 0
				|| batch.drawGeoSpec.primitiveCount < 0
				|| batch.drawGeoSpec.primitiveCount > INT_MAX / 3 ) {
			return false;
		}

		const int batchIndexCount = batch.drawGeoSpec.primitiveCount * 3;
		if ( batch.drawGeoSpec.indexStart < 0
				|| batchIndexCount > drawIndexBuffer->numIndices - batch.drawGeoSpec.indexStart
				|| batchIndexCount > tri->numIndexes - destIndexBase
				|| batch.drawGeoSpec.vertexCount > tri->numVerts - destVertexBase ) {
			return false;
		}

		const int sourceVertexStart = batch.drawGeoSpec.vertexStart;
		if ( batch.drawGeoSpec.vertexCount > INT_MAX - sourceVertexStart ) {
			return false;
		}
		const int sourceVertexEnd = sourceVertexStart + batch.drawGeoSpec.vertexCount;
		const glIndex_t *source =
				drawIndexBuffer->indices.Ptr() + batch.drawGeoSpec.indexStart;
		for ( int index = 0 ; index < batchIndexCount ; index++ ) {
			const unsigned int encodedIndex = source[ index ];
			if ( encodedIndex > static_cast<unsigned int>( INT_MAX ) ) {
				return false;
			}
			const int sourceIndex = static_cast<int>( encodedIndex );
			if ( sourceIndex < sourceVertexStart || sourceIndex >= sourceVertexEnd ) {
				return false;
			}
			flattened[ destIndexBase + index ] = static_cast<glIndex_t>(
					destVertexBase + sourceIndex - sourceVertexStart );
		}

		destVertexBase += batch.drawGeoSpec.vertexCount;
		destIndexBase += batchIndexCount;
	}

	return destVertexBase == tri->numVerts && destIndexBase == tri->numIndexes;
}
#endif

static int VK_GpuSkinning_FindMemo( const void *ambientKey, bool reserve,
		bool &existing ) {
	existing = false;
	if ( ambientKey == NULL ) {
		return -1;
	}
	const unsigned int first = static_cast<unsigned int>(
			( reinterpret_cast<uintptr_t>( ambientKey ) >> 4 )
			& ( VK_GPU_SKINNING_MEMO_SIZE - 1 ) );
	for ( int probe = 0; probe < VK_GPU_SKINNING_MEMO_SIZE; ++probe ) {
		const int index = static_cast<int>( ( first + static_cast<unsigned int>( probe ) )
				& ( VK_GPU_SKINNING_MEMO_SIZE - 1 ) );
		vkGpuSkinningMemo_t &memo = vkExec.gpuSkinningMemo[ index ];
		if ( memo.ambientKey == ambientKey ) {
			existing = true;
			return index;
		}
		if ( memo.ambientKey == NULL ) {
			if ( reserve ) {
				memo.ambientKey = ambientKey;
				memo.vertexOffset = -1;
			}
			return index;
		}
	}
	return -1;
}

/*
====================
VK_GpuSkinning_PrepareView

The front end cannot dispatch while building dynamic models, so Vulkan keeps
its CPU ambient cache as a correctness fallback. Immediately before a 3D view
starts issuing caller state, gather each unique eligible surface, interrupt
the active dynamic-rendering scope once, and materialize ordinary idDrawVert
output in the frame vertex ring. The ambient-cache handle remains the draw
memo key, so depth, interactions, ambient passes, shadow maps, subviews, and
view models all consume the same result without learning a skinning ABI.
====================
*/
static bool VK_GpuSkinning_PrepareView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->drawSurfs == NULL || viewDef->numDrawSurfs <= 0
			|| !r_gpuSkinning.GetBool() || !vkGpuSkinningBackendRequested
			|| !vkExec.gpuSkinningAvailable || !vkExec.frameOpen
			|| !vkExec.mainScopeOpen || vkExec.cmd == VK_NULL_HANDLE
			|| vkExec.frameSlot < 0 || vkExec.frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return true;
	}

	const int slot = vkExec.frameSlot;
	vkRing_t &ring = vkExec.vertexRings[ slot ];
	const VkDeviceSize nonCoherentAtomSize =
			vkCtx.deviceProperties.limits.nonCoherentAtomSize;
	if ( nonCoherentAtomSize == 0 || nonCoherentAtomSize > static_cast<VkDeviceSize>( INT_MAX )
			|| ( nonCoherentAtomSize & ( nonCoherentAtomSize - 1 ) ) != 0 ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
		return true;
	}
	const int deviceWriteAlignment = Max( 64, static_cast<int>( nonCoherentAtomSize ) );
	idTempArray<vkGpuSkinningDispatch_t> dispatches(
			static_cast<unsigned int>( viewDef->numDrawSurfs ) );
	int dispatchCount = 0;
	int firstHostWrite = INT_MAX;
	int lastHostWrite = 0;
	int firstOutput = INT_MAX;
	int lastOutput = 0;

	drawSurf_t **drawSurfs = reinterpret_cast<drawSurf_t **>( viewDef->drawSurfs );
	for ( int surfNum = 0; surfNum < viewDef->numDrawSurfs; ++surfNum ) {
		const drawSurf_t *drawSurf = drawSurfs[ surfNum ];
		const srfTriangles_t *tri = drawSurf != NULL ? drawSurf->geo : NULL;
		if ( tri == NULL || tri->ambientCache == NULL || tri->numVerts <= 0
				|| !R_GpuSkinning_IsCandidate( tri ) ) {
			continue;
		}
		bool existing = false;
		if ( VK_GpuSkinning_FindMemo( tri->ambientCache, false, existing ) < 0 ) {
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
			vkExec.gpuSkinningMemoFallbacks++;
			continue;
		}
		if ( existing ) {
			continue;
		}

		gpuSkinningSurface_t surface;
		if ( !R_GpuSkinning_GetSurface( tri, surface ) ) {
			R_GpuSkinning_RecordFallback( surface.fallbackReason );
			vkExec.gpuSkinningValidationFallbacks++;
			continue;
		}
		const gpuSkinningFallbackReason_t validation =
				R_GpuSkinning_ValidateSurface( surface );
		if ( validation != GPU_SKINNING_FALLBACK_NONE
				|| surface.fallbackReason != GPU_SKINNING_FALLBACK_NONE
				|| surface.numVerts != tri->numVerts
				|| surface.palette.matrixStrideFloats != GPU_SKINNING_JOINT_FLOATS ) {
			const gpuSkinningFallbackReason_t reason =
					validation != GPU_SKINNING_FALLBACK_NONE ? validation
					: ( surface.fallbackReason != GPU_SKINNING_FALLBACK_NONE
						? surface.fallbackReason
						: ( surface.numVerts != tri->numVerts
							? GPU_SKINNING_FALLBACK_VERTEX_COUNT
							: GPU_SKINNING_FALLBACK_STALE_PALETTE ) );
			R_GpuSkinning_RecordFallback( reason );
			vkExec.gpuSkinningValidationFallbacks++;
			continue;
		}

		const size_t sourceBytes = static_cast<size_t>( surface.numVerts )
				* sizeof( idDrawVert );
		const size_t skinBytes = static_cast<size_t>( surface.numVerts )
				* sizeof( gpuSkinningVertex_t );
		const size_t jointBytes = static_cast<size_t>( surface.palette.numJoints )
				* GPU_SKINNING_JOINT_FLOATS * sizeof( float );
		if ( sourceBytes == 0 || sourceBytes > static_cast<size_t>( INT_MAX )
				|| skinBytes == 0 || skinBytes > static_cast<size_t>( INT_MAX )
				|| jointBytes == 0 || jointBytes > static_cast<size_t>( INT_MAX ) ) {
			R_GpuSkinning_RecordFallback( sourceBytes == 0
					|| sourceBytes > static_cast<size_t>( INT_MAX )
					|| skinBytes == 0 || skinBytes > static_cast<size_t>( INT_MAX )
						? GPU_SKINNING_FALLBACK_VERTEX_COUNT
						: GPU_SKINNING_FALLBACK_JOINT_COUNT );
			vkExec.gpuSkinningValidationFallbacks++;
			continue;
		}
		const size_t deviceWriteMask = static_cast<size_t>( deviceWriteAlignment - 1 );
		if ( sourceBytes > static_cast<size_t>( INT_MAX ) - deviceWriteMask ) {
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_VERTEX_COUNT );
			vkExec.gpuSkinningValidationFallbacks++;
			continue;
		}
		const size_t outputReserveBytes = ( sourceBytes + deviceWriteMask )
				& ~deviceWriteMask;
		const uint32_t groupCount = static_cast<uint32_t>(
				( surface.numVerts + VK_GPU_SKINNING_WORKGROUP_SIZE - 1 )
				/ VK_GPU_SKINNING_WORKGROUP_SIZE );
		if ( groupCount == 0
				|| groupCount > vkCtx.deviceProperties.limits.maxComputeWorkGroupCount[ 0 ] ) {
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_VERTEX_COUNT );
			vkExec.gpuSkinningValidationFallbacks++;
			continue;
		}

		const int savedCursor = ring.cursor;
		const int sourceOffset = VK_Ring_Alloc( ring, surface.bindPoseVerts,
				sourceBytes, 64 );
		const int skinOffset = sourceOffset >= 0
				? VK_Ring_Alloc( ring, surface.skinVerts, skinBytes, 16 ) : -1;
		const int jointOffset = skinOffset >= 0
				? VK_Ring_Alloc( ring, surface.palette.matrices, jointBytes, 16 ) : -1;
		const int outputOffset = jointOffset >= 0
				? VK_Ring_Reserve( ring, outputReserveBytes,
						deviceWriteAlignment ) : -1;
		if ( sourceOffset < 0 || skinOffset < 0 || jointOffset < 0 || outputOffset < 0 ) {
			ring.cursor = savedCursor;
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_PALETTE_ALLOCATION );
			vkExec.gpuSkinningRingFallbacks++;
			continue;
		}
		bool insertedExisting = false;
		const int memoIndex = VK_GpuSkinning_FindMemo(
				tri->ambientCache, true, insertedExisting );
		if ( memoIndex < 0 || insertedExisting ) {
			ring.cursor = savedCursor;
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
			vkExec.gpuSkinningMemoFallbacks++;
			continue;
		}

		vkGpuSkinningDispatch_t &dispatch = dispatches[ dispatchCount++ ];
		dispatch.memoIndex = memoIndex;
		dispatch.sourceOffset = sourceOffset;
		dispatch.skinOffset = skinOffset;
		dispatch.jointOffset = jointOffset;
		dispatch.outputOffset = outputOffset;
		dispatch.sourceBytes = static_cast<int>( sourceBytes );
		dispatch.skinBytes = static_cast<int>( skinBytes );
		dispatch.jointBytes = static_cast<int>( jointBytes );
		dispatch.outputBytes = static_cast<int>( sourceBytes );
		dispatch.vertexCount = static_cast<uint32_t>( surface.numVerts );
		dispatch.jointCount = static_cast<uint32_t>( surface.palette.numJoints );
		firstHostWrite = Min( firstHostWrite, sourceOffset );
		lastHostWrite = Max( lastHostWrite, jointOffset + dispatch.jointBytes );
		firstOutput = Min( firstOutput, outputOffset );
		lastOutput = Max( lastOutput, outputOffset + dispatch.outputBytes );
	}

	if ( dispatchCount == 0 ) {
		return true;
	}

	VK_Exec_EndMainRendering();

	VkBufferMemoryBarrier2 hostToCompute;
	memset( &hostToCompute, 0, sizeof( hostToCompute ) );
	hostToCompute.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	hostToCompute.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	hostToCompute.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
	hostToCompute.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	hostToCompute.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	hostToCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	hostToCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	hostToCompute.buffer = ring.buffer;
	hostToCompute.offset = static_cast<VkDeviceSize>( firstHostWrite );
	hostToCompute.size = static_cast<VkDeviceSize>( lastHostWrite - firstHostWrite );
	VkDependencyInfo dependency;
	memset( &dependency, 0, sizeof( dependency ) );
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &hostToCompute;
	vkCmdPipelineBarrier2( vkExec.cmd, &dependency );

	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			vkExec.gpuSkinningPipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			vkExec.gpuSkinningPipelineLayout, 0, 1,
			&vkExec.gpuSkinningSets[ slot ], 0, NULL );
	for ( int dispatchIndex = 0; dispatchIndex < dispatchCount; ++dispatchIndex ) {
		const vkGpuSkinningDispatch_t &dispatch = dispatches[ dispatchIndex ];
		vkGpuSkinningPushConstants_t push;
		push.sourceWordOffset = static_cast<uint32_t>( dispatch.sourceOffset / 4 );
		push.skinWordOffset = static_cast<uint32_t>( dispatch.skinOffset / 4 );
		push.jointWordOffset = static_cast<uint32_t>( dispatch.jointOffset / 4 );
		push.outputWordOffset = static_cast<uint32_t>( dispatch.outputOffset / 4 );
		push.vertexCount = dispatch.vertexCount;
		push.jointCount = dispatch.jointCount;
		vkCmdPushConstants( vkExec.cmd, vkExec.gpuSkinningPipelineLayout,
				VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
		vkCmdDispatch( vkExec.cmd,
				( dispatch.vertexCount + VK_GPU_SKINNING_WORKGROUP_SIZE - 1 )
				/ VK_GPU_SKINNING_WORKGROUP_SIZE, 1, 1 );
	}

	VkBufferMemoryBarrier2 computeToVertex;
	memset( &computeToVertex, 0, sizeof( computeToVertex ) );
	computeToVertex.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	computeToVertex.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	computeToVertex.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	computeToVertex.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
	computeToVertex.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
	computeToVertex.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	computeToVertex.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	computeToVertex.buffer = ring.buffer;
	computeToVertex.offset = static_cast<VkDeviceSize>( firstOutput );
	computeToVertex.size = static_cast<VkDeviceSize>( lastOutput - firstOutput );
	dependency.pBufferMemoryBarriers = &computeToVertex;
	vkCmdPipelineBarrier2( vkExec.cmd, &dependency );

	if ( !VK_Exec_BeginMainRendering( false ) ) {
		for ( int dispatchIndex = 0; dispatchIndex < dispatchCount; ++dispatchIndex ) {
			R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
		}
		common->Warning( "Vulkan: GPU skinning could not resume main rendering; view skipped" );
		return false;
	}

	for ( int dispatchIndex = 0; dispatchIndex < dispatchCount; ++dispatchIndex ) {
		const vkGpuSkinningDispatch_t &dispatch = dispatches[ dispatchIndex ];
		vkExec.gpuSkinningMemo[ dispatch.memoIndex ].vertexOffset = dispatch.outputOffset;
		vkExec.gpuSkinningDispatches++;
		vkExec.gpuSkinningVertices += dispatch.vertexCount;
		R_GpuSkinning_RecordBackendPrepared(
				static_cast<int>( dispatch.vertexCount ),
				static_cast<int>( dispatch.jointCount ),
				static_cast<uint64>( dispatch.jointBytes ) );
	}

	static bool loggedFirstDispatch = false;
	if ( !loggedFirstDispatch ) {
		loggedFirstDispatch = true;
		common->Printf( "Vulkan: GPU skinning dispatched %d surfaces (%llu vertices)\n",
			dispatchCount, static_cast<unsigned long long>( vkExec.gpuSkinningVertices ) );
	}
	return true;
}

// Memoized ring upload: the depth fill and both ambient walks visit the same
// tris, so a hit retains the existing offsets without re-copying. Binding is
// deliberately separate so speculative shared-domain preflight records no
// graphics state. The same upload path also serves the
// interaction pass, where the light-tris chains carry their own index
// subset over the shared ambient vertex cache (distinct idxKey, so memo
// entries never alias across the subsets).
static bool VK_Exec_CPUCacheValid( const vertCache_t *cache,
		const bool indexBuffer, const size_t requiredBytes ) {
	return cache != NULL && requiredBytes > 0
		&& requiredBytes <= static_cast<size_t>( INT_MAX )
		&& cache->tag != TAG_FREE
		&& cache->indexBuffer == indexBuffer && cache->offset >= 0
		&& cache->size >= static_cast<int>( requiredBytes )
		&& cache->vbo == 0 && cache->virtMem != NULL;
}

static bool VK_Exec_UploadTriGeometry( int slot, const srfTriangles_t *tri,
		int &vertexOffset, int &indexOffset ) {
	vertexOffset = -1;
	indexOffset = -1;
	if ( tri == NULL || tri->ambientCache == NULL
			|| tri->numVerts <= 0 || tri->numIndexes <= 0
			|| slot < 0 || slot >= VK_FRAMES_IN_FLIGHT ) {
		return false;
	}
	const size_t vertexBytes = static_cast<size_t>( tri->numVerts )
		* sizeof( idDrawVert );
	const size_t exactIndexBytes = static_cast<size_t>( tri->numIndexes ) * sizeof( glIndex_t );
	if ( !VK_Exec_CPUCacheValid( tri->ambientCache, false, vertexBytes )
			|| ( tri->indexes == NULL && tri->indexCache != NULL
				&& !VK_Exec_CPUCacheValid( tri->indexCache, true,
					exactIndexBytes ) ) ) {
		return false;
	}

	const void *vertKey = tri->ambientCache;
	const void *idxKey = tri->indexes != NULL
			? static_cast<const void *>( tri->indexes )
			: ( tri->indexCache != NULL
				? static_cast<const void *>( tri->indexCache )
				: static_cast<const void *>( tri ) );

	// independent vert/index memos: light-tris chains carry their own index
	// subset over the SHARED ambient vertex array, so a combined memo would
	// re-upload the full vertex payload once per light per surface
	{
		bool gpuMemoExisting = false;
		const int gpuMemoIndex = VK_GpuSkinning_FindMemo(
				vertKey, false, gpuMemoExisting );
		if ( gpuMemoExisting && gpuMemoIndex >= 0
				&& vkExec.gpuSkinningMemo[ gpuMemoIndex ].vertexOffset >= 0 ) {
			vertexOffset = vkExec.gpuSkinningMemo[ gpuMemoIndex ].vertexOffset;
		} else {
			const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)vertKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
			vkVertUpload_t &memo = vkExec.vertMemo[ memoIndex ];
			if ( memo.vertKey == vertKey && vertKey != NULL ) {
				vertexOffset = memo.vertexOffset;
			} else {
				const idDrawVert *verts = (const idDrawVert *)vertexCache.Position( tri->ambientCache );
				vertexOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,
					static_cast<size_t>( tri->numVerts ) * sizeof( idDrawVert ), 64 );
				if ( vertexOffset < 0 ) {
					return false;
				}
				if ( vertKey != NULL ) {
					memo.vertKey = vertKey;
					memo.vertexOffset = vertexOffset;
				}
			}
		}
	}
	{
		const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)idxKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
		vkIdxUpload_t &memo = vkExec.idxMemo[ memoIndex ];
		if ( memo.idxKey == idxKey && idxKey != NULL ) {
			indexOffset = memo.indexOffset;
		} else {
			const size_t indexBytes = exactIndexBytes;
			if ( indexBytes > static_cast<size_t>( vkExec.indexRings[ slot ].capacity ) ) {
				return false;
			}

			const glIndex_t *indexSource = tri->indexes;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
			const rvMD5RMesh *packedMesh = R_MD5R_GetMeshForTri( tri );
			const bool headeredLightIndexes =
					VK_Exec_PackedLightIndexesHeadered( tri, packedMesh );
			const bool rebuildPackedAmbientIndexes =
					indexSource == NULL && tri->indexCache == NULL
					&& packedMesh != NULL && tri->ambientSurface == NULL;
			if ( headeredLightIndexes || rebuildPackedAmbientIndexes ) {
				idTempArray<glIndex_t> flattened(
						static_cast<unsigned int>( tri->numIndexes ) );
				const bool flattenedOK = headeredLightIndexes
						? VK_Exec_FlattenPackedLightIndexes( tri, packedMesh,
								flattened.Ptr() )
						: VK_Exec_FlattenPackedAmbientIndexes( tri, packedMesh,
								flattened.Ptr() );
				if ( !flattenedOK ) {
					return false;
				}
				indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ],
						flattened.Ptr(), indexBytes, 4 );
			} else
#endif
			if ( indexSource != NULL ) {
				indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ],
						indexSource, indexBytes, 4 );
			} else if ( VK_Exec_CPUCacheValid( tri->indexCache, true,
					indexBytes ) ) {
				const glIndex_t *cachedIndexes =
						static_cast<const glIndex_t *>( vertexCache.Position( tri->indexCache ) );
				indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ],
						cachedIndexes, indexBytes, 4 );
			} else {
				return false;
			}
			if ( indexOffset < 0 ) {
				return false;
			}
			if ( idxKey != NULL ) {
				memo.idxKey = idxKey;
				memo.indexOffset = indexOffset;
			}
		}
	}

	return true;
}

bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot,
		const srfTriangles_t *tri ) {
	int vertexOffset;
	int indexOffset;
	if ( cmd == VK_NULL_HANDLE
			|| !VK_Exec_UploadTriGeometry( slot, tri,
				vertexOffset, indexOffset ) ) {
		return false;
	}

	const VkDeviceSize vertexBindOffset =
		static_cast<VkDeviceSize>( vertexOffset );
	vkCmdBindVertexBuffers( cmd, 0, 1,
		&vkExec.vertexRings[ slot ].buffer, &vertexBindOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer,
		static_cast<VkDeviceSize>( indexOffset ), VK_INDEX_TYPE_UINT32 );
	vkExec.boundVertexOffset = vertexOffset;	// the cube-texgen path re-binds binding 0 alongside its dir stream
	return true;
}

// stencil-shadow-volume variant of the memoized upload (Phase G1): streams
// the shadowCache_t vec4 stream (tri->shadowCache holds CPU pointers via
// the CPU-backed vertex cache; the shadow tri's numVerts IS the cache
// vertex count) plus the volume indexes. The memo keys share the ambient
// tables — cache handles and index arrays never alias across the two
// families, and a direct-mapped collision just re-uploads.
bool VK_Exec_BindShadowGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri ) {
	if ( tri == NULL || slot < 0 || slot >= VK_FRAMES_IN_FLIGHT ) {
		return false;
	}
	// Explicit GPU_SKINNING_FALLBACK_STENCIL_VOLUME: silhouette extrusion,
	// caps, and the trace-visible shadow topology remain CPU authoritative.
	// Never consult the compute-output memo here; only tri->shadowCache is admissible.

	const void *vertKey = tri->shadowCache;
	const void *idxKey = tri->indexes;
	const size_t vertexBytes = static_cast<size_t>( tri->numVerts ) * sizeof( shadowCache_t );
	const size_t indexBytes = static_cast<size_t>( tri->numIndexes ) * sizeof( glIndex_t );
	if ( vertKey == NULL || idxKey == NULL || tri->numVerts <= 0
			|| tri->numIndexes <= 0
			|| !VK_Exec_CPUCacheValid( tri->shadowCache, false,
				vertexBytes )
			|| indexBytes == 0 || indexBytes > static_cast<size_t>( INT_MAX ) ) {
		return false;
	}

	int vertexOffset;
	{
		const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)vertKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
		vkVertUpload_t &memo = vkExec.vertMemo[ memoIndex ];
		if ( memo.vertKey == vertKey ) {
			vertexOffset = memo.vertexOffset;
		} else {
			const shadowCache_t *verts = (const shadowCache_t *)vertexCache.Position( tri->shadowCache );
			vertexOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,
				vertexBytes, 16 );
			if ( vertexOffset < 0 ) {
				return false;
			}
			memo.vertKey = vertKey;
			memo.vertexOffset = vertexOffset;
		}
	}
	int indexOffset;
	{
		const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)idxKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
		vkIdxUpload_t &memo = vkExec.idxMemo[ memoIndex ];
		if ( memo.idxKey == idxKey ) {
			indexOffset = memo.indexOffset;
		} else {
			indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ], tri->indexes,
				indexBytes, 4 );
			if ( indexOffset < 0 ) {
				return false;
			}
			memo.idxKey = idxKey;
			memo.indexOffset = indexOffset;
		}
	}

	VkDeviceSize vertexBindOffset = (VkDeviceSize)vertexOffset;
	vkCmdBindVertexBuffers( cmd, 0, 1, &vkExec.vertexRings[ slot ].buffer, &vertexBindOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );
	vkExec.boundVertexOffset = vertexOffset;
	return true;
}

// Shared stencil-interaction preflight retains the exact ring offsets produced
// by the ordinary CPU-authored shadow-cache bridge.  The bind performed here is
// limited to ring upload/bind command recording; no framebuffer write occurs
// until the complete view has reconciled and selected shared ownership.
bool VK_Exec_PrepareShadowGeometry( VkCommandBuffer cmd, int slot,
		const srfTriangles_t *tri, int &vertexOffset, int &indexOffset ) {
	vertexOffset = -1;
	indexOffset = -1;
	if ( !VK_Exec_BindShadowGeometry( cmd, slot, tri ) || tri == NULL
			|| tri->indexes == NULL ) {
		return false;
	}

	vertexOffset = vkExec.boundVertexOffset;
	const void *indexKey = static_cast<const void *>( tri->indexes );
	const unsigned int memoIndex = static_cast<unsigned int>(
		( reinterpret_cast<uintptr_t>( indexKey ) >> 4 )
		& ( VK_TRI_MEMO_SIZE - 1 ) );
	const vkIdxUpload_t &memo = vkExec.idxMemo[ memoIndex ];
	if ( memo.idxKey != indexKey || vertexOffset < 0 || memo.indexOffset < 0 ) {
		vertexOffset = -1;
		return false;
	}
	indexOffset = memo.indexOffset;
	return true;
}

// Backend-generated geometry which has no idVertexCache handle cannot use the
// pointer-keyed upload memo above. Packed MD5R shadow volumes are decoded and
// skinned one primitive batch at a time, so stream those transient arrays
// directly into the active frame rings.
bool VK_Exec_BindRawShadowGeometry( VkCommandBuffer cmd, int slot,
		const shadowCache_t *verts, int numVerts,
		const glIndex_t *indexes, int numIndexes ) {
	if ( verts == NULL || indexes == NULL || numVerts <= 0 || numIndexes <= 0 ) {
		return false;
	}

	const int vertexOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,
			static_cast<size_t>( numVerts ) * sizeof( shadowCache_t ), 16 );
	if ( vertexOffset < 0 ) {
		return false;
	}
	const int indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ], indexes,
			static_cast<size_t>( numIndexes ) * sizeof( glIndex_t ), 4 );
	if ( indexOffset < 0 ) {
		return false;
	}

	VkDeviceSize vertexBindOffset = (VkDeviceSize)vertexOffset;
	vkCmdBindVertexBuffers( cmd, 0, 1, &vkExec.vertexRings[ slot ].buffer, &vertexBindOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );
	vkExec.boundVertexOffset = vertexOffset;
	return true;
}

// Viewport base + a GL bottom-left request rect, clipped to the viewport and
// flipped into Vulkan framebuffer coordinates.
static void VK_Exec_SetScissorRect( VkCommandBuffer cmd, const viewDef_t *viewDef,
		const idScreenRect &requested, int fbHeight ) {
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	VkRect2D scissor;
	if ( !requested.IsEmpty() ) {
		const int requestedX0 = vpX + requested.x1;
		const int requestedY0GL = vpYGL + requested.y1;
		const int requestedX1 = vpX + requested.x2 + 1;
		const int requestedY1GL = vpYGL + requested.y2 + 1;
		const int x0 = Max( 0, Max( vpX, requestedX0 ) );
		const int x1 = Max( x0, Min( vpX + vpW, requestedX1 ) );
		const int y0GL = Max( vpYGL, requestedY0GL );
		const int y1GL = Max( y0GL, Min( vpYGL + vpH, requestedY1GL ) );
		const int y0 = Max( 0, fbHeight - y1GL );
		const int y1 = Max( y0, Min( fbHeight, fbHeight - y0GL ) );
		scissor.offset.x = x0;
		scissor.offset.y = y0;
		scissor.extent.width = (uint32_t)( x1 - x0 );
		scissor.extent.height = (uint32_t)( y1 - y0 );
	} else {
		const int x0 = Max( 0, vpX );
		const int x1 = Max( x0, vpX + vpW );
		const int y0 = Max( 0, fbHeight - vpYGL - vpH );
		const int y1 = Max( y0, Min( fbHeight, fbHeight - vpYGL ) );
		scissor.offset.x = x0;
		scissor.offset.y = y0;
		scissor.extent.width = (uint32_t)( x1 - x0 );
		scissor.extent.height = (uint32_t)( y1 - y0 );
	}
	vkCmdSetScissor( cmd, 0, 1, &scissor );
}

// Per-surface scissor: viewport base + drawSurf scissor (GL bottom-left).
// When r_useScissor is disabled, retain RB_BeginDrawingView's view-level
// scissor instead of expanding subviews to the whole viewport.
void VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, const drawSurf_t *drawSurf, int fbHeight ) {
	VK_Exec_SetScissorRect( cmd, viewDef,
			( r_useScissor.GetBool() && !drawSurf->scissorRect.IsEmpty() )
				? drawSurf->scissorRect : viewDef->scissor,
			fbHeight );
}

// The view's own scissor, with no surface refinement. Vulkan has no scissor
// query, so a pass that needs a full-framebuffer rect of its own restores
// this rather than whatever per-surface rect happened to be latched.
void VK_Exec_SetViewScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, int fbHeight ) {
	VK_Exec_SetScissorRect( cmd, viewDef, viewDef->scissor, fbHeight );
}

// mvp for a surface's space: GL projection (with the id depth hacks) times
// the space's modelview, then the Vulkan clip-z fixup. cl_gunfov's weapon
// projection refit is not carried into the module (rare tuner cvar).
void VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] ) {
	const struct viewEntity_s *space = drawSurf->space;
	float proj[ 16 ];
	memcpy( proj, viewDef->projectionMatrix, sizeof( proj ) );
	if ( space->modelDepthHack != 0.0f ) {
		proj[ 14 ] -= space->modelDepthHack;
	} else if ( space->weaponDepthHack ) {
		proj[ 14 ] *= 0.25f;
	}
	float mvpGL[ 16 ];
	myGlMultMatrix( space->modelViewMatrix, proj, mvpGL );
	VK_FixupClipSpaceZ( outMvp, mvpGL );
}

/*
====================
Interaction-pass accessors (vk_Interactions.cpp)

vkExec stays file-static; the interaction TU reaches the frame state and
the Phase F1 resources through these narrow hooks.
====================
*/
VkCommandBuffer VK_Exec_ActiveCmd( void ) {
	return vkExec.frameOpen ? vkExec.cmd : VK_NULL_HANDLE;
}

int VK_Exec_ActiveFrameSlot( void ) {
	return vkExec.frameSlot;
}

// cached per-image descriptor; NULL when the image has no device backing or
// (require2D) when its view is a cube — the interaction pipeline samples
// every slot through sampler2D and a cube view would trip validation
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D ) {
	if ( texnum >= sizeof( vkExec.descriptorCache )
			/ sizeof( vkExec.descriptorCache[0] ) ) {
		return VK_NULL_HANDLE;
	}
	if ( require2D ) {
		const vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
		if ( entry == NULL || entry->isCube ) {
			return VK_NULL_HANDLE;
		}
	}
	return VK_GuiExecutor_GetImageDescriptor( texnum );
}

VkDescriptorSet VK_Exec_InteractionUniformSet( void ) {
	return vkExec.uniformRingSets[ vkExec.frameSlot ];
}

// Streams one ordinary interaction block into the frame's uniform ring.
// The descriptor range stays 256 bytes, while the dynamic offset honors the
// device's possibly stricter alignment.
int VK_Exec_InteractionUniformAlloc( const void *data, int bytes ) {
	if ( bytes > VK_UNIFORM_SLICE_BYTES ) {
		return -1;
	}
	const int alignment =
			VK_Exec_UniformSliceAlignment( VK_UNIFORM_SLICE_BYTES );
	return alignment > 0
			? VK_Ring_Alloc( vkExec.uniformRings[ vkExec.frameSlot ],
					data, bytes, alignment )
			: -1;
}

static bool VK_TemporalPresentation_GetColorTarget(
		idRenderTexture *renderTexture, idImage *&image,
		vkImageEntry_t *&entry ) {
	image = NULL;
	entry = NULL;
	vkImageEntry_t *depthEntry = NULL;
	if ( !VK_Exec_RenderTextureEntries( renderTexture, entry, depthEntry )
			|| entry == NULL || entry->samples != VK_SAMPLE_COUNT_1_BIT ) {
		return false;
	}
	image = renderTexture->GetColorImage( 0 );
	return image != NULL
		&& ( entry->usage & VK_IMAGE_USAGE_SAMPLED_BIT ) != 0;
}

static bool VK_TemporalPresentation_GetDepthTarget(
		idRenderTexture *renderTexture, idImage *&image,
		vkImageEntry_t *&entry ) {
	image = NULL;
	entry = NULL;
	vkImageEntry_t *colorEntry = NULL;
	if ( !VK_Exec_RenderTextureEntries( renderTexture, colorEntry, entry )
			|| entry == NULL || entry->samples != VK_SAMPLE_COUNT_1_BIT ) {
		return false;
	}
	image = renderTexture->GetDepthImage();
	return image != NULL
		&& ( entry->usage & VK_IMAGE_USAGE_SAMPLED_BIT ) != 0;
}

static bool VK_TemporalPresentation_TrackHistoryTarget(
		idRenderTexture *target ) {
	if ( target == NULL || target->GetNumColorImages() != 1
			|| target->GetColorImage( 0 ) == NULL ) {
		return false;
	}
	idImage *image = target->GetColorImage( 0 );
	const unsigned long long generation =
		static_cast<unsigned long long>( image->GetStorageGeneration() );
	for ( int i = 0; i < 2; ++i ) {
		if ( vkExec.temporalHistoryTargets[i] == target ) {
			if ( vkExec.temporalHistoryStorageGenerations[i] == generation ) {
				return false;
			}
			vkExec.temporalHistoryStorageGenerations[i] = generation;
			return true;
		}
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( vkExec.temporalHistoryTargets[i] == NULL ) {
			vkExec.temporalHistoryTargets[i] = target;
			vkExec.temporalHistoryStorageGenerations[i] = generation;
			return false;
		}
	}
	// The public contract is a two-image ping-pong. Seeing a third owner means
	// the native history allocation was replaced without a module restart.
	vkExec.temporalHistoryTargets[0] = target;
	vkExec.temporalHistoryStorageGenerations[0] = generation;
	vkExec.temporalHistoryTargets[1] = NULL;
	vkExec.temporalHistoryStorageGenerations[1] = 0;
	return true;
}

static void VK_TemporalPresentation_FillResolveBlock(
		const resolveTemporalPresentationCommand_t &command,
		int sceneWidth, int sceneHeight, bool useHistory, bool depthValid,
		bool captureRecenter, int debugMode,
		vkTemporalResolveBlock_t &block ) {
	memset( &block, 0, sizeof( block ) );
	const viewDef_t *viewDef = command.viewDef;
	const int outputWidth = Max( 1, (int)vkCtx.swapchainExtent.width );
	const int outputHeight = Max( 1, (int)vkCtx.swapchainExtent.height );
	block.sceneOutputExtent[0] = 1.0f / Max( 1, sceneWidth );
	block.sceneOutputExtent[1] = 1.0f / Max( 1, sceneHeight );
	block.sceneOutputExtent[2] = (float)outputWidth;
	block.sceneOutputExtent[3] = (float)outputHeight;
	block.depthFeedback[2] = idMath::ClampFloat( 0.0f, 0.98f,
		command.feedback );
	block.depthFeedback[3] = idMath::ClampFloat( 0.0f, 2.0f,
		command.reactiveScale );
	block.temporalParams[2] = useHistory ? 1.0f : 0.0f;
	block.temporalParams[3] = (float)idMath::ClampInt( 0, 3, debugMode );
	block.motionParams[2] = depthValid ? 1.0f : 0.0f;
	block.motionParams[3] = captureRecenter ? 1.0f : 0.0f;
	for ( int component = 0; component < 4; ++component ) {
		block.reactiveRect0[component] = -1.0f;
		block.reactiveRect1[component] = -1.0f;
	}
	if ( useHistory ) {
		// Packet ownership is a correctness input, not an optional quality hint.
		// Until a matching policy is proven available, reject history over the
		// complete output so a packet-frame miss cannot leave moving content
		// consuming unowned stale samples.
		block.reactiveRect0[0] = 0.0f;
		block.reactiveRect0[1] = 0.0f;
		block.reactiveRect0[2] = 1.0f;
		block.reactiveRect0[3] = 1.0f;
		temporalViewMotionPolicy_t motionPolicy;
		const bool motionPolicyAvailable = viewDef != NULL
			&& R_ScenePackets_BuildTemporalViewMotionPolicy(
				viewDef, 0u, motionPolicy );
		if ( motionPolicyAvailable ) {
			for ( int component = 0; component < 4; ++component ) {
				block.reactiveRect0[component] = -1.0f;
			}
			float *rects[TEMPORAL_MAX_REACTIVE_REGIONS] = {
				block.reactiveRect0, block.reactiveRect1
			};
			const int regionCount = Min( motionPolicy.reactiveRegionCount,
				TEMPORAL_MAX_REACTIVE_REGIONS );
			for ( int regionIndex = 0; regionIndex < regionCount;
					++regionIndex ) {
				const temporalReactiveRegion_t &region =
					motionPolicy.reactiveRegions[regionIndex];
				rects[regionIndex][0] = region.x1;
				rects[regionIndex][1] = region.y1;
				rects[regionIndex][2] = region.x2;
				rects[regionIndex][3] = region.y2;
			}
		}
	}
	if ( viewDef == NULL ) {
		return;
	}

	const float projectionX = viewDef->projectionMatrix[0];
	const float projectionY = viewDef->projectionMatrix[5];
	if ( idMath::Fabs( projectionX ) > 0.00001f
			&& idMath::Fabs( projectionY ) > 0.00001f ) {
		block.currentReconstruct[0] = 1.0f / projectionX;
		block.currentReconstruct[1] = 1.0f / projectionY;
	}
	block.currentReconstruct[2] = viewDef->projectionMatrix[8];
	block.currentReconstruct[3] = viewDef->projectionMatrix[9];
	memcpy( block.previousProject,
		viewDef->temporalPreviousProjectInfo.ToFloatPtr(),
		sizeof( block.previousProject ) );
	block.depthFeedback[0] = viewDef->projectionMatrix[10];
	block.depthFeedback[1] = viewDef->projectionMatrix[14];

	memcpy( block.currentViewOrigin,
		viewDef->renderView.vieworg.ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.currentViewAxis0,
		viewDef->renderView.viewaxis[0].ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.currentViewAxis1,
		viewDef->renderView.viewaxis[1].ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.currentViewAxis2,
		viewDef->renderView.viewaxis[2].ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.previousViewOrigin,
		viewDef->temporalPreviousViewOrigin.ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.previousViewAxis0,
		viewDef->temporalPreviousViewAxis[0].ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.previousViewAxis1,
		viewDef->temporalPreviousViewAxis[1].ToFloatPtr(), 3 * sizeof( float ) );
	memcpy( block.previousViewAxis2,
		viewDef->temporalPreviousViewAxis[2].ToFloatPtr(), 3 * sizeof( float ) );
	float screenEffects[8];
	AdvancedScreenSpaceCore_Pack( command.advancedScreenSpace, screenEffects );
	block.currentViewOrigin[3] = screenEffects[0];
	block.currentViewAxis0[3] = screenEffects[1];
	block.currentViewAxis1[3] = screenEffects[2];
	block.currentViewAxis2[3] = screenEffects[3];
	block.previousViewOrigin[3] = screenEffects[4];
	block.previousViewAxis0[3] = screenEffects[5];
	block.previousViewAxis1[3] = screenEffects[6];
	block.previousViewAxis2[3] = screenEffects[7];
	block.temporalParams[0] = viewDef->temporalJitterPixels.x / outputWidth;
	block.temporalParams[1] = viewDef->temporalJitterPixels.y / outputHeight;
	block.motionParams[1] = useHistory && depthValid
		&& viewDef->temporalPreviousProjectionValid
		&& idMath::Fabs( projectionX ) > 0.00001f
		&& idMath::Fabs( projectionY ) > 0.00001f ? 1.0f : 0.0f;
}

static bool VK_TemporalPresentation_DrawResolve(
		idRenderTexture *destination,
		idImage *sceneImage, vkImageEntry_t *sceneEntry,
		idImage *depthImage, vkImageEntry_t *depthEntry,
		idImage *historyImage, vkImageEntry_t *historyEntry,
		const vkTemporalResolveBlock_t &block ) {
	if ( sceneImage == NULL || sceneEntry == NULL
			|| !VK_Exec_SetRenderTarget( NULL ) ) {
		return false;
	}
	VK_Exec_TransitionImage( sceneEntry, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	if ( depthEntry != NULL ) {
		VK_Exec_TransitionImage( depthEntry,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}
	if ( historyEntry != NULL ) {
		VK_Exec_TransitionImage( historyEntry,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}

	VkDescriptorSet sceneSet = VK_GuiExecutor_GetImageDescriptor(
		sceneImage->GetDeviceHandle() );
	idImage *depthFallbackImage = globalImages->whiteImage;
	idImage *historyFallbackImage = globalImages->blackImage;
	VkDescriptorSet depthSet = depthImage != NULL && depthEntry != NULL
		? VK_GuiExecutor_GetImageDescriptor( depthImage->GetDeviceHandle() )
		: VK_GuiExecutor_GetResidentImageDescriptor( depthFallbackImage );
	VkDescriptorSet historySet = historyImage != NULL && historyEntry != NULL
		? VK_GuiExecutor_GetImageDescriptor( historyImage->GetDeviceHandle() )
		: VK_GuiExecutor_GetResidentImageDescriptor( historyFallbackImage );
	const int uniformOffset = VK_Exec_InteractionUniformAlloc(
		&block, sizeof( block ) );
	if ( sceneSet == VK_NULL_HANDLE || depthSet == VK_NULL_HANDLE
			|| historySet == VK_NULL_HANDLE || uniformOffset < 0
			|| !VK_Exec_SetRenderTarget( destination ) ) {
		return false;
	}

	const VkPipeline pipeline =
		VK_TemporalPresentation_GetResolvePipeline();
	if ( pipeline == VK_NULL_HANDLE ) {
		return false;
	}
	VkViewport viewport;
	memset( &viewport, 0, sizeof( viewport ) );
	viewport.width = (float)vkExec.activeExtent.width;
	viewport.height = (float)vkExec.activeExtent.height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor;
	memset( &scissor, 0, sizeof( scissor ) );
	scissor.extent = vkExec.activeExtent;
	vkCmdSetViewport( vkExec.cmd, 0, 1, &viewport );
	vkCmdSetScissor( vkExec.cmd, 0, 1, &scissor );
	vkCmdSetDepthTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( vkExec.cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( vkExec.cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( vkExec.cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	VkDescriptorSet imageSets[3] = { sceneSet, depthSet, historySet };
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vkExec.interactionPipelineLayout, 0, 3, imageSets, 0, NULL );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
		1, &dynamicOffset );
	vkCmdDraw( vkExec.cmd, 3, 1, 0, 0 );
	return true;
}

static bool VK_TemporalPresentation_DrawPendingSceneSpatial(
		const viewDef_t *viewDef, idImage *sceneImage,
		vkImageEntry_t *sceneEntry, idImage *depthImage,
		vkImageEntry_t *depthEntry ) {
	if ( viewDef == NULL || sceneImage == NULL || sceneEntry == NULL ) {
		return false;
	}
	resolveTemporalPresentationCommand_t command;
	memset( &command, 0, sizeof( command ) );
	command.viewDef = viewDef;
	command.reactiveScale = 1.0f;
	command.advancedScreenSpace =
		R_TemporalPresentation_GetFrameState().advancedScreenSpace;
	vkTemporalResolveBlock_t block;
	VK_TemporalPresentation_FillResolveBlock( command,
		sceneEntry->width, sceneEntry->height, false, depthEntry != NULL,
		true, 0, block );
	return VK_TemporalPresentation_DrawResolve( NULL, sceneImage, sceneEntry,
		depthImage, depthEntry, NULL, NULL, block );
}

static bool VK_TemporalPresentation_DrawResolvedColorToSwap(
		const resolveTemporalPresentationCommand_t &command,
		idImage *resolvedImage, vkImageEntry_t *resolvedEntry,
		int outputWidth, int outputHeight ) {
	if ( resolvedImage == NULL || resolvedEntry == NULL
			|| resolvedEntry->width != outputWidth
			|| resolvedEntry->height != outputHeight ) {
		return false;
	}
	resolveTemporalPresentationCommand_t presentCommand = command;
	memset( &presentCommand.advancedScreenSpace, 0,
		sizeof( presentCommand.advancedScreenSpace ) );
	vkTemporalResolveBlock_t block;
	VK_TemporalPresentation_FillResolveBlock( presentCommand,
		outputWidth, outputHeight, false, false, false, 0, block );
	return VK_TemporalPresentation_DrawResolve( NULL,
		resolvedImage, resolvedEntry, NULL, NULL, NULL, NULL, block );
}

static bool VK_TemporalPresentation_BlitColorToSwap(
		vkImageEntry_t *sourceEntry, int width, int height ) {
	if ( sourceEntry == NULL || sourceEntry->samples != VK_SAMPLE_COUNT_1_BIT
			|| ( sourceEntry->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0
			|| width != (int)vkCtx.swapchainExtent.width
			|| height != (int)vkCtx.swapchainExtent.height
			|| !VK_Exec_SetRenderTarget( NULL ) ) {
		return false;
	}
	VkFilter filter = VK_FILTER_NEAREST;
	if ( !VK_TemporalPresentation_BlitSupported( sourceEntry->format, filter ) ) {
		return false;
	}
	VK_Exec_EndMainRendering();
	VK_Exec_TransitionImage( sourceEntry, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	VkImageBlit region;
	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.srcOffsets[1].x = width;
	region.srcOffsets[1].y = height;
	region.srcOffsets[1].z = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.dstOffsets[1].x = width;
	region.dstOffsets[1].y = height;
	region.dstOffsets[1].z = 1;
	vkCmdBlitImage( vkExec.cmd, sourceEntry->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vkCtx.swapchainImages[vkExec.swapImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter );
	VK_Exec_TransitionImage( sourceEntry,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	VK_Exec_TransitionSwapchain( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	return VK_Exec_BeginMainRendering( false );
}

static void VK_TemporalPresentation_InvalidateAcceptedNoWrite(
		const resolveTemporalPresentationCommand_t &command,
		const char *reason ) {
	// Effect-only presentation deliberately has no history write target. That
	// is a successful current-frame resolve, not a temporal discontinuity; do
	// not churn the shared history generation once per frame while a leaf is on.
	if ( !command.presentation.temporalAARequested
			|| command.captureFrame || tr.takingScreenshot
			|| command.historyGeneration
				!= R_TemporalPresentation_HistoryGeneration() ) {
		return;
	}
	R_TemporalPresentation_InvalidateHistory( reason );
	vkExec.temporalHistoryGenerationSeen =
		R_TemporalPresentation_HistoryGeneration();
}

static bool VK_TemporalPresentation_ResolveTargets(
		const resolveTemporalPresentationCommand_t &sourceCommand,
		bool trackExternalHistory, bool *historyAdvanced ) {
	if ( historyAdvanced != NULL ) {
		*historyAdvanced = false;
	}
	resolveTemporalPresentationCommand_t command = sourceCommand;
	if ( tr.takingScreenshot ) {
		command.captureFrame = true;
		command.historyValid = false;
	}

	idImage *sceneImage = NULL;
	idImage *depthImage = NULL;
	idImage *historyReadImage = NULL;
	idImage *historyWriteImage = NULL;
	vkImageEntry_t *sceneEntry = NULL;
	vkImageEntry_t *depthEntry = NULL;
	vkImageEntry_t *historyReadEntry = NULL;
	vkImageEntry_t *historyWriteEntry = NULL;
	if ( !VK_TemporalPresentation_GetColorTarget( command.sceneColorTarget,
			sceneImage, sceneEntry ) ) {
		VK_TemporalPresentation_InvalidateAcceptedNoWrite( command,
			"Vulkan temporal scene color is unavailable" );
		return false;
	}
	const bool depthResident = VK_TemporalPresentation_GetDepthTarget(
		command.sceneDepthTarget, depthImage, depthEntry );
	const bool depthReady = depthResident
		&& VK_TemporalPresentation_DepthStampMatches(
			command.sceneDepthTarget, depthImage,
			command.historyGeneration );
	const bool writeReady = !command.captureFrame
		&& VK_TemporalPresentation_GetColorTarget( command.historyWriteTarget,
			historyWriteImage, historyWriteEntry );
	const bool readReady = !command.captureFrame
		&& command.historyReadTarget != NULL
		&& VK_TemporalPresentation_GetColorTarget( command.historyReadTarget,
			historyReadImage, historyReadEntry );

	bool historyResourceChanged = false;
	if ( trackExternalHistory && !command.captureFrame && writeReady ) {
		historyResourceChanged |=
			VK_TemporalPresentation_TrackHistoryTarget(
				command.historyWriteTarget );
	}
	if ( trackExternalHistory && !command.captureFrame && readReady ) {
		historyResourceChanged |=
			VK_TemporalPresentation_TrackHistoryTarget(
				command.historyReadTarget );
	}
	if ( historyResourceChanged ) {
		// A swapchain/output reset can retire these images before this queued
		// command executes. Do not advance the shared generation twice for the
		// same reset; a replacement under the still-current generation does own
		// a new invalidation.
		if ( command.historyGeneration
				== R_TemporalPresentation_HistoryGeneration() ) {
			R_TemporalPresentation_InvalidateHistory(
				"Vulkan temporal history resource changed" );
		}
		vkExec.temporalHistoryGenerationSeen =
			R_TemporalPresentation_HistoryGeneration();
		command.historyValid = false;
	}

	const int sceneWidth = sceneEntry->width;
	const int sceneHeight = sceneEntry->height;
	const int outputWidth = (int)vkCtx.swapchainExtent.width;
	const int outputHeight = (int)vkCtx.swapchainExtent.height;
	const bool presentationMatches = command.presentation.frameNumber
			== backEnd.frameCount
		&& command.presentation.outputWidth == outputWidth
		&& command.presentation.outputHeight == outputHeight
		&& command.presentation.sceneWidth == sceneWidth
		&& command.presentation.sceneHeight == sceneHeight;
	const bool writeMatches = writeReady
		&& historyWriteEntry->width == outputWidth
		&& historyWriteEntry->height == outputHeight;
	const bool readMatches = readReady
		&& historyReadEntry->width == outputWidth
		&& historyReadEntry->height == outputHeight;
	const bool generationMatches = command.historyGeneration
		== R_TemporalPresentation_HistoryGeneration();
	const bool viewHistoryReady = command.viewDef != NULL
		&& command.viewDef->temporalPrepared
		&& command.viewDef->temporalHistoryValid
		&& command.viewDef->temporalPreviousProjectionValid
		&& command.viewDef->temporalHistoryGeneration
			== command.historyGeneration;
	const bool historyValid = !command.captureFrame && command.historyValid
		&& presentationMatches && generationMatches
		&& command.presentation.historyGeneration == command.historyGeneration
		&& viewHistoryReady
		&& depthReady && readMatches
		&& command.historyReadTarget != command.historyWriteTarget
		&& historyReadEntry->image != sceneEntry->image
		&& ( !writeReady || historyReadEntry->image != historyWriteEntry->image );
	const bool writeHistory = !command.captureFrame && presentationMatches
		&& generationMatches
		&& command.presentation.historyGeneration == command.historyGeneration
		&& depthReady && writeMatches
		&& historyWriteEntry->image != sceneEntry->image;

	if ( !writeHistory ) {
		// Capture and every validated failure path go directly to the native
		// output. No game history target is bound, sampled, or mutated. Recenter
		// the current projection so a shader/resource fallback cannot expose its
		// per-frame jitter at native output.
		VK_TemporalPresentation_InvalidateAcceptedNoWrite( command, depthReady
			? "Vulkan temporal history write unavailable"
			: "Vulkan temporal depth is not current" );
		vkTemporalResolveBlock_t spatialBlock;
		VK_TemporalPresentation_FillResolveBlock( command, sceneWidth,
			sceneHeight, false, depthReady, true, 0, spatialBlock );
		return VK_TemporalPresentation_DrawResolve( NULL, sceneImage,
			sceneEntry, depthReady ? depthImage : NULL,
			depthReady ? depthEntry : NULL, NULL, NULL, spatialBlock );
	}

	vkTemporalResolveBlock_t normalBlock;
	VK_TemporalPresentation_FillResolveBlock( command, sceneWidth,
		sceneHeight, historyValid, depthReady, false, 0, normalBlock );
	if ( !VK_TemporalPresentation_DrawResolve(
			command.historyWriteTarget, sceneImage, sceneEntry,
			depthReady ? depthImage : NULL, depthReady ? depthEntry : NULL,
			historyValid ? historyReadImage : NULL,
			historyValid ? historyReadEntry : NULL, normalBlock )
			|| !VK_Exec_SetRenderTarget( NULL ) ) {
		// Fail closed to a spatial present without sampling or further mutating
		// history. The game will reject this generation on the next frame.
		VK_TemporalPresentation_InvalidateAcceptedNoWrite( command,
			"Vulkan temporal resolve execution failed" );
		vkTemporalResolveBlock_t spatialBlock;
		VK_TemporalPresentation_FillResolveBlock( command, sceneWidth,
			sceneHeight, false, depthReady, true, 0, spatialBlock );
		return VK_TemporalPresentation_DrawResolve( NULL, sceneImage,
			sceneEntry, depthReady ? depthImage : NULL,
			depthReady ? depthEntry : NULL, NULL, NULL, spatialBlock );
	}

	const int debugMode = idMath::ClampInt( 0, 3, command.debugMode );
	if ( debugMode != 0 ) {
		vkTemporalResolveBlock_t debugBlock;
		VK_TemporalPresentation_FillResolveBlock( command, sceneWidth,
			sceneHeight, historyValid, depthReady, false,
			debugMode, debugBlock );
		const bool presented = VK_TemporalPresentation_DrawResolve(
			NULL, sceneImage,
			sceneEntry, depthReady ? depthImage : NULL,
			depthReady ? depthEntry : NULL,
			historyValid ? historyReadImage : NULL,
			historyValid ? historyReadEntry : NULL, debugBlock );
		if ( presented && historyAdvanced != NULL ) {
			*historyAdvanced = true;
		}
		return presented;
	}
	const bool presented = VK_TemporalPresentation_DrawResolvedColorToSwap(
		command, historyWriteImage, historyWriteEntry,
		outputWidth, outputHeight )
		|| VK_TemporalPresentation_BlitColorToSwap(
			historyWriteEntry, outputWidth, outputHeight );
	if ( presented && historyAdvanced != NULL ) {
		*historyAdvanced = true;
	}
	return presented;
}

bool VK_GuiExecutor_ResolveTemporalPresentation(
		const resolveTemporalPresentationCommand_t &sourceCommand ) {
	if ( !VK_GuiExecutor_BeginFrame() ) {
		VK_TemporalPresentation_InvalidateAcceptedNoWrite( sourceCommand,
			"Vulkan temporal frame is unavailable" );
		return false;
	}
	if ( vkExec.temporalScenePendingComposite
			&& !VK_TemporalPresentation_CompositePendingScene() ) {
		VK_TemporalPresentation_InvalidateAcceptedNoWrite( sourceCommand,
			"Vulkan temporal scene composite blocked command execution" );
		return false;
	}
	VK_TemporalPresentation_ObserveHistoryAndCapture();
	return VK_TemporalPresentation_ResolveTargets(
		sourceCommand, true, NULL );
}

int VK_Exec_InteractionUniformCheckpoint( void ) {
	return vkExec.frameSlot >= 0 && vkExec.frameSlot < VK_FRAMES_IN_FLIGHT
		? vkExec.uniformRings[ vkExec.frameSlot ].cursor : -1;
}

void VK_Exec_InteractionUniformRestore( int checkpoint ) {
	if ( vkExec.frameSlot < 0 || vkExec.frameSlot >= VK_FRAMES_IN_FLIGHT
			|| checkpoint < 0
			|| checkpoint > vkExec.uniformRings[ vkExec.frameSlot ].capacity ) {
		return;
	}
	vkExec.uniformRings[ vkExec.frameSlot ].cursor = checkpoint;
}

// Shadow receiver blocks use a larger descriptor range for four localized
// projected cascades. Point receivers share set 7 and use this allocator too,
// keeping every set-7 dynamic descriptor valid through the full 512 bytes.
int VK_Exec_ShadowUniformAlloc( const void *data, int bytes ) {
	if ( bytes > VK_SHADOW_UNIFORM_SLICE_BYTES ) {
		return -1;
	}
	const int alignment =
			VK_Exec_UniformSliceAlignment( VK_SHADOW_UNIFORM_SLICE_BYTES );
	return alignment > 0
			? VK_Ring_Alloc( vkExec.uniformRings[ vkExec.frameSlot ],
					data, bytes, alignment )
			: -1;
}

// the frame slot's shadow set (atlas compare/raw samplers + shadow-block
// ring), or NULL until both atlas descriptors have been written
VkDescriptorSet VK_Exec_ShadowDescriptorSet( void ) {
	return vkExec.shadowSetsHaveAtlas ? vkExec.shadowSets[ vkExec.frameSlot ] : VK_NULL_HANDLE;
}

// (re)points every frame slot's shadow set at the atlas view + compare/raw
// samplers (vk_ShadowMap.cpp calls this at atlas creation, before the set is
// ever bound; recreation waits the device idle first). Any NULL clears.
bool VK_Exec_UpdateShadowAtlasDescriptors( VkImageView view,
		VkSampler compareSampler, VkSampler rawSampler ) {
	vkExec.shadowSetsHaveAtlas = false;
	if ( view == VK_NULL_HANDLE ||
			compareSampler == VK_NULL_HANDLE ||
			rawSampler == VK_NULL_HANDLE ) {
		return true;
	}
	if ( !vkExec.initialized ) {
		return false;
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkExec.shadowSets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
		VkDescriptorImageInfo imageInfos[ 2 ];
		memset( imageInfos, 0, sizeof( imageInfos ) );
		imageInfos[ 0 ].sampler = compareSampler;
		imageInfos[ 1 ].sampler = rawSampler;
		for ( int imageIndex = 0; imageIndex < 2; imageIndex++ ) {
			imageInfos[ imageIndex ].imageView = view;
			imageInfos[ imageIndex ].imageLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		VkWriteDescriptorSet writes[ 2 ];
		memset( writes, 0, sizeof( writes ) );
		for ( int writeIndex = 0; writeIndex < 2; writeIndex++ ) {
			writes[ writeIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[ writeIndex ].dstSet = vkExec.shadowSets[ i ];
			writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;
			writes[ writeIndex ].descriptorCount = 1;
			writes[ writeIndex ].descriptorType =
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[ writeIndex ].pImageInfo = &imageInfos[ writeIndex ];
		}
		vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );
	}
	vkExec.shadowSetsHaveAtlas = true;
	return true;
}

// allocates (first call per cube) and points one point-light cube's
// per-frame-slot shadow sets at the cube view + compare/raw samplers; binding
// 1 is that slot's uniform ring (the same shadow set layout as the atlas set,
// so the point pipelines stay set-7 compatible). Called at cube creation,
// before the sets are ever bound; recreation waits the device idle first.
bool VK_Exec_CreateShadowCubeSets( VkImageView cubeView,
		VkSampler compareSampler, VkSampler rawSampler,
		VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] ) {
	if ( !vkExec.initialized || cubeView == VK_NULL_HANDLE ||
			compareSampler == VK_NULL_HANDLE ||
			rawSampler == VK_NULL_HANDLE ) {
		return false;
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( sets[ i ] == VK_NULL_HANDLE ) {
			VkDescriptorSetAllocateInfo dsai;
			memset( &dsai, 0, sizeof( dsai ) );
			dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsai.descriptorPool = vkExec.descriptorPool;
			dsai.descriptorSetCount = 1;
			dsai.pSetLayouts = &vkExec.shadowSetLayout;
			if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &sets[ i ] ) != VK_SUCCESS ) {
				common->Warning( "Vulkan: point shadow descriptor set allocation failed" );
				sets[ i ] = VK_NULL_HANDLE;
				return false;
			}
			VkDescriptorBufferInfo bufferInfo;
			bufferInfo.buffer = vkExec.uniformRings[ i ].buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;
			VkWriteDescriptorSet ringWrite;
			memset( &ringWrite, 0, sizeof( ringWrite ) );
			ringWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ringWrite.dstSet = sets[ i ];
			ringWrite.dstBinding = 1;
			ringWrite.descriptorCount = 1;
			ringWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			ringWrite.pBufferInfo = &bufferInfo;
			vkUpdateDescriptorSets( vkCtx.device, 1, &ringWrite, 0, NULL );
		}

		VkDescriptorImageInfo imageInfos[ 2 ];
		memset( imageInfos, 0, sizeof( imageInfos ) );
		imageInfos[ 0 ].sampler = compareSampler;
		imageInfos[ 1 ].sampler = rawSampler;
		for ( int imageIndex = 0; imageIndex < 2; imageIndex++ ) {
			imageInfos[ imageIndex ].imageView = cubeView;
			imageInfos[ imageIndex ].imageLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		VkWriteDescriptorSet writes[ 2 ];
		memset( writes, 0, sizeof( writes ) );
		for ( int writeIndex = 0; writeIndex < 2; writeIndex++ ) {
			writes[ writeIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[ writeIndex ].dstSet = sets[ i ];
			writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;
			writes[ writeIndex ].descriptorCount = 1;
			writes[ writeIndex ].descriptorType =
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[ writeIndex ].pImageInfo = &imageInfos[ writeIndex ];
		}
		vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );
	}
	return true;
}

void VK_Exec_FreeShadowCubeSets( VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] ) {
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( sets[ i ] != VK_NULL_HANDLE && vkExec.initialized && vkExec.descriptorPool != VK_NULL_HANDLE ) {
			vkFreeDescriptorSets( vkCtx.device, vkExec.descriptorPool, 1, &sets[ i ] );
		}
		sets[ i ] = VK_NULL_HANDLE;
	}
}

// the RB_STD_T_RenderShaderPasses ambient-stage walk shared by 2D views and
// the world ambient passes. Geometry and scissor are already bound; mvp is
// clip-z fixed. worldDepthState additionally applies each stage's GLS depth
// bits. Cube texgens (skybox/wobblesky/diffuse) draw through the cube
// pipeline and non-bumpy reflection stages use the environment pipeline.
static float VK_Exec_AlphaTestModeValue( const shaderStage_t *stage ) {
	if ( stage == NULL || !stage->hasAlphaTest ) {
		return 0.0f;
	}
	if ( stage->alphaTestMode == GL_LESS ) {
		return -1.0f;
	}
	if ( stage->alphaTestMode == GL_EQUAL ) {
		return 2.0f;
	}
	return 1.0f;
}

static void VK_Exec_SetPushTextureMatrix( const shaderStage_t *stage,
		const float *registers, vkGuiPushConstants_t &push ) {
	push.texMatrixS[ 0 ] = 1.0f;
	push.texMatrixS[ 1 ] = 0.0f;
	push.texMatrixS[ 2 ] = 0.0f;
	push.texMatrixS[ 3 ] = 0.0f;
	push.texMatrixT[ 0 ] = 0.0f;
	push.texMatrixT[ 1 ] = 1.0f;
	push.texMatrixT[ 2 ] = 0.0f;
	push.texMatrixT[ 3 ] = 0.0f;
	push.params[ 3 ] = 0.0f;
	if ( stage == NULL || !stage->texture.hasMatrix || registers == NULL ) {
		return;
	}
	float matrix[ 16 ];
	RB_GetShaderTextureMatrix( registers, &stage->texture, matrix );
	push.texMatrixS[ 0 ] = matrix[ 0 ];
	push.texMatrixS[ 1 ] = matrix[ 4 ];
	push.texMatrixS[ 3 ] = matrix[ 12 ];
	push.texMatrixT[ 0 ] = matrix[ 1 ];
	push.texMatrixT[ 1 ] = matrix[ 5 ];
	push.texMatrixT[ 3 ] = matrix[ 13 ];
	push.params[ 3 ] = 1.0f;
}

static void VK_Exec_RestoreSurfaceState( VkCommandBuffer cmd, const viewDef_t *viewDef,
		const idMaterial *shader, bool worldDepthState ) {
	vkCmdSetDepthTestEnable( cmd, worldDepthState ? VK_TRUE : VK_FALSE );
	if ( !worldDepthState || viewDef == NULL || shader == NULL ) {
		return;
	}
	switch ( shader->GetCullType() ) {
		case CT_TWO_SIDED:
			vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
			break;
		case CT_BACK_SIDED:
			vkCmdSetCullMode( cmd,
					viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
			break;
		default:
			vkCmdSetCullMode( cmd,
					viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
			break;
	}
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
		vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(),
				0.0f, r_offsetFactor.GetFloat() );
	} else {
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	}
}

static void VK_Exec_SetProgramStageDepthState( VkCommandBuffer cmd,
		const shaderStage_t *stage, bool worldDepthState ) {
	if ( !worldDepthState ) {
		return;
	}
	const int bits = stage->drawStateBits;
	VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	if ( bits & GLS_DEPTHFUNC_EQUAL ) {
		compareOp = VK_COMPARE_OP_EQUAL;
	} else if ( bits & GLS_DEPTHFUNC_ALWAYS ) {
		compareOp = VK_COMPARE_OP_ALWAYS;
	}
	vkCmdSetDepthCompareOp( cmd, compareOp );
	vkCmdSetDepthWriteEnable( cmd, ( bits & GLS_DEPTHMASK ) ? VK_FALSE : VK_TRUE );
}

static bool VK_Exec_DrawBumpyProgramStage( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri, const float mvp[ 16 ],
		bool worldDepthState, const shaderStage_t *stage ) {
	const newShaderStage_t *newStage = stage->newStage;
	if ( drawSurf->space == NULL || newStage->numFragmentProgramImages < 2
			|| newStage->fragmentProgramImages[ 0 ] == NULL
			|| newStage->fragmentProgramImages[ 1 ] == NULL ) {
		return false;
	}

	idImage *cubeImage = newStage->fragmentProgramImages[ 0 ];
	idImage *bumpImage = newStage->fragmentProgramImages[ 1 ];
	const vkImageEntry_t *cubeEntry =
			VK_Image_GetEntry( cubeImage->GetDeviceHandle() );
	const vkImageEntry_t *bumpEntry =
			VK_Image_GetEntry( bumpImage->GetDeviceHandle() );
	if ( cubeEntry == NULL || !cubeEntry->isCube
			|| bumpEntry == NULL || bumpEntry->isCube ) {
		return false;
	}
	VkDescriptorSet cubeSet =
			VK_GuiExecutor_GetImageDescriptor( cubeImage->GetDeviceHandle() );
	VkDescriptorSet bumpSet =
			VK_Exec_ImageDescriptor( bumpImage->GetDeviceHandle(), true );
	if ( cubeSet == VK_NULL_HANDLE || bumpSet == VK_NULL_HANDLE ) {
		return false;
	}

	idVec3 localViewOrigin;
	R_GlobalPointToLocal( drawSurf->space->modelMatrix,
			viewDef->renderView.vieworg, localViewOrigin );
	const float *modelMatrix = drawSurf->space->modelMatrix;
	vkBumpyEnvironmentBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.localViewOrigin[ 0 ] = localViewOrigin[ 0 ];
	block.localViewOrigin[ 1 ] = localViewOrigin[ 1 ];
	block.localViewOrigin[ 2 ] = localViewOrigin[ 2 ];
	block.localViewOrigin[ 3 ] = 1.0f;
	block.modelRow0[ 0 ] = modelMatrix[ 0 ];
	block.modelRow0[ 1 ] = modelMatrix[ 4 ];
	block.modelRow0[ 2 ] = modelMatrix[ 8 ];
	block.modelRow0[ 3 ] = modelMatrix[ 12 ];
	block.modelRow1[ 0 ] = modelMatrix[ 1 ];
	block.modelRow1[ 1 ] = modelMatrix[ 5 ];
	block.modelRow1[ 2 ] = modelMatrix[ 9 ];
	block.modelRow1[ 3 ] = modelMatrix[ 13 ];
	block.modelRow2[ 0 ] = modelMatrix[ 2 ];
	block.modelRow2[ 1 ] = modelMatrix[ 6 ];
	block.modelRow2[ 2 ] = modelMatrix[ 10 ];
	block.modelRow2[ 3 ] = modelMatrix[ 14 ];
	const int uniformOffset =
			VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	if ( uniformOffset < 0 ) {
		return false;
	}

	VkPipeline pipeline =
			VK_GuiExecutor_GetEnvironmentPipeline( stage->drawStateBits, true );
	if ( pipeline == VK_NULL_HANDLE ) {
		return false;
	}

	vkGuiPushConstants_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	VK_Exec_SetProgramStageDepthState( vkExec.cmd, stage, worldDepthState );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 0, 1, &cubeSet, 0, NULL );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 1, 1, &bumpSet, 0, NULL );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
			1, &dynamicOffset );
	vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
	vkCmdDrawIndexed( vkExec.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
	return true;
}

static bool VK_Exec_DrawRefractiveGlassStage( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri,
		const float mvp[ 16 ], bool worldDepthState,
		const shaderStage_t *stage ) {
	const newShaderStage_t *newStage = stage->newStage;
	const float *regs = drawSurf->shaderRegisters;
	if ( drawSurf->space == NULL || newStage == NULL
			|| newStage->numFragmentProgramImages < 3 ) {
		return false;
	}

	VkDescriptorSet textureSets[ 3 ];
	for ( int i = 0; i < 3; i++ ) {
		idImage *image = newStage->fragmentProgramImages[ i ];
		if ( image == NULL ) {
			return false;
		}
		const vkImageEntry_t *entry =
				VK_Image_GetEntry( image->GetDeviceHandle() );
		const bool expectCube = i == 2;
		if ( entry == NULL || entry->isCube != expectCube ) {
			return false;
		}
		textureSets[ i ] =
				VK_GuiExecutor_GetImageDescriptor( image->GetDeviceHandle() );
		if ( textureSets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
	}

	float parms[ 16 ][ 4 ];
	memset( parms, 0, sizeof( parms ) );
	for ( int i = 0; i < newStage->numVertexParms && i < 2; i++ ) {
		for ( int j = 0; j < 4; j++ ) {
			parms[ i ][ j ] = regs != NULL
					? regs[ newStage->vertexParms[ i ][ j ] ] : 0.0f;
		}
	}

	idVec3 localViewOrigin;
	R_GlobalPointToLocal( drawSurf->space->modelMatrix,
			viewDef->renderView.vieworg, localViewOrigin );
	parms[ 2 ][ 0 ] = localViewOrigin[ 0 ];
	parms[ 2 ][ 1 ] = localViewOrigin[ 1 ];
	parms[ 2 ][ 2 ] = localViewOrigin[ 2 ];
	parms[ 2 ][ 3 ] = 1.0f;

	const float *modelMatrix = drawSurf->space->modelMatrix;
	parms[ 3 ][ 0 ] = modelMatrix[ 0 ];
	parms[ 3 ][ 1 ] = modelMatrix[ 4 ];
	parms[ 3 ][ 2 ] = modelMatrix[ 8 ];
	parms[ 4 ][ 0 ] = modelMatrix[ 1 ];
	parms[ 4 ][ 1 ] = modelMatrix[ 5 ];
	parms[ 4 ][ 2 ] = modelMatrix[ 9 ];
	parms[ 5 ][ 0 ] = modelMatrix[ 2 ];
	parms[ 5 ][ 1 ] = modelMatrix[ 6 ];
	parms[ 5 ][ 2 ] = modelMatrix[ 10 ];

	const int viewportWidth =
			Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const int viewportHeight =
			Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	int currentRenderWidth = viewportWidth;
	int currentRenderHeight = viewportHeight;
	if ( globalImages->currentRenderImage != NULL ) {
		currentRenderWidth =
				Max( 1, globalImages->currentRenderImage->GetUploadWidth() );
		currentRenderHeight =
				Max( 1, globalImages->currentRenderImage->GetUploadHeight() );
	}
	parms[ 12 ][ 0 ] = (float)VK_Exec_ActiveFramebufferHeight();
	parms[ 13 ][ 0 ] = (float)viewDef->viewport.x1;
	parms[ 13 ][ 1 ] = (float)viewDef->viewport.y1;
	parms[ 14 ][ 0 ] = (float)viewportWidth;
	parms[ 14 ][ 1 ] = (float)viewportHeight;
	parms[ 15 ][ 0 ] = (float)viewportWidth / (float)currentRenderWidth;
	parms[ 15 ][ 1 ] = (float)viewportHeight / (float)currentRenderHeight;

	const int uniformOffset =
			VK_Exec_InteractionUniformAlloc( parms, sizeof( parms ) );
	if ( uniformOffset < 0 ) {
		return false;
	}
	const VkPipeline pipeline = VK_Exec_GetProgramPipeline(
			VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS,
			stage->drawStateBits, false );
	if ( pipeline == VK_NULL_HANDLE ) {
		return false;
	}

	vkGuiPushConstants_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	if ( regs != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			push.stageColor[ i ] = regs[ stage->color.registers[ i ] ];
		}
	} else {
		push.stageColor[ 0 ] = push.stageColor[ 1 ] =
				push.stageColor[ 2 ] = push.stageColor[ 3 ] = 1.0f;
	}
	push.params[ 1 ] = VK_Exec_AlphaTestModeValue( stage );
	push.params[ 2 ] = stage->hasAlphaTest && regs != NULL
			? regs[ stage->alphaTestRegister ] : 0.0f;

	VK_Exec_SetProgramStageDepthState( vkExec.cmd, stage, worldDepthState );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 0, 3, textureSets, 0, NULL );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
			1, &dynamicOffset );
	vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
	vkCmdDrawIndexed( vkExec.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
	return true;
}

static int VK_Exec_GLSLCanonicalParmSlot( vkGLSLProgramFamily_t family,
		const char *name ) {
	if ( name == NULL ) {
		return -1;
	}
	static const char * const displacementParms[] = {
		"scrollX", "scrollY", "sizeX", "sizeY", "texCoordSize"
	};
	static const char * const twoStageParms[] = {
		"scrollX", "scrollY", "sizeX", "sizeY", "texCoordSize",
		"scrollX2", "scrollY2", "sizeX2", "sizeY2", "texCoordSize2"
	};
	static const char * const ghostParms[] = {
		"scrollX", "scrollY", "sizeX", "sizeY", "distortionLength"
	};
	static const char * const displacement2Parms[] = {
		"OffsetScrollSpeed", "EffectAScrollSpeed", "EffectBScrollSpeed",
		"OffsetScale", "EffectAScale", "EffectBScale", "BaseFlicker",
		"EffectAFlicker", "EffectBFlicker"
	};
	static const char * const sniperParms[] = {
		"textureScale", "textureHalfScale", "backgroundColor"
	};
	static const char * const blurParms[] = {
		"textureScale", "sampleDist"
	};
	static const char * const medLabsParms[] = {
		"range", "focus", "Scroll", "ApproachColor", "ApproachPercent"
	};
	static const char * const alParms[] = {
		"distanceScale", "textureScale", "LightLoc", "LightColor",
		"LightSize", "LightBehind", "LightMinDistance"
	};
	static const char * const waterParms[] = {
		"fRefractionIndexAndPower", "vColorLight", "vColorDark",
		"POTCorrection", "TextureTranslateScale",
		"TextureTranslateScale2", "vFogColor", "vDistortionScale",
		"localEyePos"
	};
	static const char * const depthAwareBlurParms[] = {
		"invTexSize", "range", "focus", "approachColor",
		"approachPercent", "distanceScale"
	};
	static const char * const smaaEdgeParms[] = {
		"invTexSize", "sourceColorSpace", "quality"
	};
	static const char * const smaaWeightsParms[] = {
		"invTexSize", "texSize", "quality"
	};

	const char * const *names = NULL;
	int count = 0;
	switch ( family ) {
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT:
			names = displacementParms;
			count = sizeof( displacementParms ) / sizeof( displacementParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE:
			names = twoStageParms;
			count = sizeof( twoStageParms ) / sizeof( twoStageParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING:
			names = ghostParms;
			count = sizeof( ghostParms ) / sizeof( ghostParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2:
			names = displacement2Parms;
			count = sizeof( displacement2Parms ) / sizeof( displacement2Parms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE:
			names = displacementParms;
			count = sizeof( displacementParms ) / sizeof( displacementParms[ 0 ] );
			if ( !idStr::Icmp( name, "EyeVector" ) ) {
				return 5;
			}
			break;
		case VK_GLSL_PROGRAM_FAMILY_SNIPER_STRETCH2:
			if ( !idStr::Icmp( name, "currentRenderViewportOrigin" ) ) {
				return 13;
			}
			if ( !idStr::Icmp( name, "currentRenderViewportSize" ) ) {
				return 14;
			}
			if ( !idStr::Icmp( name, "currentRenderTextureScale" ) ) {
				return 15;
			}
			names = sniperParms;
			count = sizeof( sniperParms ) / sizeof( sniperParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE:
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE2:
			if ( !idStr::Icmp( name, "distanceScale" ) ) {
				return 0;
			}
			break;
		case VK_GLSL_PROGRAM_FAMILY_BLUR:
			names = blurParms;
			count = sizeof( blurParms ) / sizeof( blurParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_MEDLABS:
			names = medLabsParms;
			count = sizeof( medLabsParms ) / sizeof( medLabsParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_AL:
			names = alParms;
			count = sizeof( alParms ) / sizeof( alParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_WATER:
			names = waterParms;
			count = sizeof( waterParms ) / sizeof( waterParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR:
			names = depthAwareBlurParms;
			count = sizeof( depthAwareBlurParms )
					/ sizeof( depthAwareBlurParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_SMAA_EDGE:
			names = smaaEdgeParms;
			count = sizeof( smaaEdgeParms ) / sizeof( smaaEdgeParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_SMAA_WEIGHTS:
			names = smaaWeightsParms;
			count = sizeof( smaaWeightsParms )
					/ sizeof( smaaWeightsParms[ 0 ] );
			break;
		case VK_GLSL_PROGRAM_FAMILY_SMAA_BLEND:
			if ( !idStr::Icmp( name, "invTexSize" ) ) {
				return 0;
			}
			break;
		default:
			break;
	}
	for ( int i = 0; i < count; i++ ) {
		if ( !idStr::Icmp( name, names[ i ] ) ) {
			return i;
		}
	}
	return -1;
}

static bool VK_Exec_EvaluateGLSLParm( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const newShaderStage_t *newStage,
		int authoredSlot, float value[ 4 ] ) {
	memset( value, 0, 4 * sizeof( value[ 0 ] ) );
	const glslShaderParmBinding_t binding =
			newStage->shaderParmBindings[ authoredSlot ];
	const float *regs = drawSurf->shaderRegisters;
	switch ( binding ) {
		case GLSL_SHADERPARM_REGISTERS: {
			const int count = newStage->shaderParmNumRegisters[ authoredSlot ];
			if ( regs == NULL || count <= 0 ) {
				return false;
			}
			for ( int i = 0; i < count && i < 4; i++ ) {
				value[ i ] =
						regs[ newStage->shaderParmRegisters[ authoredSlot ][ i ] ];
			}
			return true;
		}
		case GLSL_SHADERPARM_VIEW_ORIGIN:
			value[ 0 ] = viewDef->renderView.vieworg[ 0 ];
			value[ 1 ] = viewDef->renderView.vieworg[ 1 ];
			value[ 2 ] = viewDef->renderView.vieworg[ 2 ];
			value[ 3 ] = 1.0f;
			return true;
		case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_ORIGIN:
			value[ 0 ] = (float)viewDef->viewport.x1;
			value[ 1 ] = (float)viewDef->viewport.y1;
			return true;
		case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_SIZE:
			value[ 0 ] =
					(float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
			value[ 1 ] =
					(float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
			return true;
		case GLSL_SHADERPARM_CURRENT_RENDER_TEXTURE_SCALE: {
			const int viewportWidth =
					Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
			const int viewportHeight =
					Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
			int textureWidth = viewportWidth;
			int textureHeight = viewportHeight;
			if ( globalImages->currentRenderImage != NULL ) {
				textureWidth =
						Max( 1, globalImages->currentRenderImage->GetUploadWidth() );
				textureHeight =
						Max( 1, globalImages->currentRenderImage->GetUploadHeight() );
			}
			value[ 0 ] = (float)viewportWidth / (float)textureWidth;
			value[ 1 ] = (float)viewportHeight / (float)textureHeight;
			return true;
		}
		case GLSL_SHADERPARM_POSTPROCESS_INV_TEX_SIZE:
			value[ 0 ] = backEnd.postProcessTexelSize[ 0 ];
			value[ 1 ] = backEnd.postProcessTexelSize[ 1 ];
			return true;
		case GLSL_SHADERPARM_POSTPROCESS_TEX_SIZE:
			value[ 0 ] = backEnd.postProcessTexelSize[ 2 ];
			value[ 1 ] = backEnd.postProcessTexelSize[ 3 ];
			return true;
		case GLSL_SHADERPARM_POSTPROCESS_SOURCE_COLOR_SPACE:
			memcpy( value, backEnd.postProcessSourceColorSpace.ToFloatPtr(),
					4 * sizeof( value[ 0 ] ) );
			return true;
		case GLSL_SHADERPARM_POSTPROCESS_SMAA_QUALITY:
			memcpy( value, backEnd.postProcessSMAAQuality.ToFloatPtr(),
					4 * sizeof( value[ 0 ] ) );
			return true;
		default:
			return false;
	}
}

static int VK_Exec_GLSLTextureSet( vkGLSLProgramFamily_t family,
		const char *name, bool &cube ) {
	cube = false;
	if ( name == NULL ) {
		return -1;
	}
	if ( !idStr::Icmp( name, "Image" )
			|| !idStr::Icmp( name, "BackgroundImage" )
			|| !idStr::Icmp( name, "Depth" )
			|| !idStr::Icmp( name, "Scene" )
			|| !idStr::Icmp( name, "ColorTex" )
			|| !idStr::Icmp( name, "EdgesTex" )
			|| !idStr::Icmp( name, "RT" ) ) {
		return 0;
	}
	if ( !idStr::Icmp( name, "DisplacementMap" )
			|| !idStr::Icmp( name, "Scope" )
			|| !idStr::Icmp( name, "DepthTex" )
			|| !idStr::Icmp( name, "Blur1" )
			|| !idStr::Icmp( name, "AreaTex" )
			|| !idStr::Icmp( name, "BlendTex" )
			|| !idStr::Icmp( name, "LightImage" ) ) {
		return 1;
	}
	if ( !idStr::Icmp( name, "DisplacementMap2" )
			|| !idStr::Icmp( name, "EffectA" )
			|| !idStr::Icmp( name, "SearchTex" )
			|| !idStr::Icmp( name, "Variance" )
			|| !idStr::Icmp( name, "ReflectTexture" ) ) {
		return 2;
	}
	if ( !idStr::Icmp( name, "CubeImage" ) ) {
		cube = true;
		return family == VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE ? 2 : -1;
	}
	if ( !idStr::Icmp( name, "EffectB" )
			|| !idStr::Icmp( name, "RefractTexture" ) ) {
		return 3;
	}
	if ( !idStr::Icmp( name, "NoiseNormalTexture" ) ) {
		return family == VK_GLSL_PROGRAM_FAMILY_WATER ? 0 : -1;
	}
	if ( !idStr::Icmp( name, "NoiseNormalTexture2" ) ) {
		return family == VK_GLSL_PROGRAM_FAMILY_WATER ? 1 : -1;
	}
	if ( !idStr::Icmp( name, "Mask" ) ) {
		return 4;
	}
	return -1;
}

static unsigned int VK_Exec_GLSLRequiredTextureMask(
		vkGLSLProgramFamily_t family ) {
	switch ( family ) {
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT:
		case VK_GLSL_PROGRAM_FAMILY_GHOST_PULLING:
		case VK_GLSL_PROGRAM_FAMILY_SNIPER_STRETCH2:
		case VK_GLSL_PROGRAM_FAMILY_AL:
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR:
			return 0x03;
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_TWO_STAGE:
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT_CUBE:
		case VK_GLSL_PROGRAM_FAMILY_MEDLABS:
			return 0x07;
		case VK_GLSL_PROGRAM_FAMILY_DISPLACEMENT2:
			return 0x1f;
		case VK_GLSL_PROGRAM_FAMILY_WATER:
			return 0x0f;
		case VK_GLSL_PROGRAM_FAMILY_MULTIPLY_BLEND:
		case VK_GLSL_PROGRAM_FAMILY_BLUR:
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE:
		case VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE2:
		case VK_GLSL_PROGRAM_FAMILY_SMAA_EDGE:
			return 0x01;
		case VK_GLSL_PROGRAM_FAMILY_SMAA_BLEND:
			return 0x03;
		case VK_GLSL_PROGRAM_FAMILY_SMAA_WEIGHTS:
			return 0x07;
		default:
			return UINT_MAX;
	}
}

static bool VK_Exec_BindGLSLStageColor( const drawSurf_t *drawSurf,
		const srfTriangles_t *tri, const shaderStage_t *stage, int stageNum ) {
	if ( stage->vertexColor == SVC_IGNORE || drawSurf->decalColorCache == NULL
			|| stageNum < 0 || stageNum >= drawSurf->decalColorStageCount
			|| drawSurf->decalColorStride < tri->numVerts * 4 ) {
		return false;
	}
	// Position() returns a byte offset, not an address, when the cache is
	// VBO-backed, so offset zero is legal and must never be null-tested or
	// used as a pointer base. Form the attribute address in integer space
	// exactly like the GL back end does (VertexCache.h).
	const void *colorData = vertexCache.Position( drawSurf->decalColorCache );
	const int colorOffset = VK_Ring_Alloc(
			vkExec.vertexRings[ vkExec.frameSlot ],
			RB_DrawVertAttributePointer( colorData,
				drawSurf->decalColorOffset + stageNum * drawSurf->decalColorStride ),
			(size_t)tri->numVerts * 4, 4 );
	if ( colorOffset < 0 ) {
		return false;
	}
	const VkBuffer colorBuffer =
			vkExec.vertexRings[ vkExec.frameSlot ].buffer;
	const VkDeviceSize bindOffset = (VkDeviceSize)colorOffset;
	vkCmdBindVertexBuffers( vkExec.cmd, 1, 1, &colorBuffer, &bindOffset );
	return true;
}

static bool VK_Exec_DrawGLSLProgramStage( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri, const float mvp[ 16 ],
		bool worldDepthState, const shaderStage_t *stage, int stageNum ) {
	newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL || !newStage->glslProgram
			|| !R_ValidateGLSLProgram( newStage ) ) {
		return false;
	}
	const vkGLSLProgramFamily_t family =
			R_GetGLSLProgramFamily( newStage->glslProgramName );
	if ( family <= VK_GLSL_PROGRAM_FAMILY_UNKNOWN
			|| family >= VK_GLSL_PROGRAM_FAMILY_COUNT ) {
		return false;
	}

	float parms[ 16 ][ 4 ];
	memset( parms, 0, sizeof( parms ) );
	for ( int i = 0; i < newStage->numShaderParms; i++ ) {
		const int targetSlot = VK_Exec_GLSLCanonicalParmSlot(
				family, newStage->shaderParmNames[ i ] );
		if ( targetSlot < 0 || targetSlot >= 16 ) {
			continue;
		}
		float value[ 4 ];
		if ( VK_Exec_EvaluateGLSLParm( viewDef, drawSurf, newStage, i, value ) ) {
			if ( family == VK_GLSL_PROGRAM_FAMILY_WATER
					&& targetSlot == 8 && drawSurf->space != NULL ) {
				idVec3 localEye;
				R_GlobalPointToLocal( drawSurf->space->modelMatrix,
						viewDef->renderView.vieworg, localEye );
				value[ 0 ] = localEye[ 0 ];
				value[ 1 ] = localEye[ 1 ];
				value[ 2 ] = localEye[ 2 ];
				value[ 3 ] = 1.0f;
			}
			memcpy( parms[ targetSlot ], value, sizeof( value ) );
		}
	}

	const int viewportWidth =
			Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const int viewportHeight =
			Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	int currentRenderWidth = viewportWidth;
	int currentRenderHeight = viewportHeight;
	if ( globalImages->currentRenderImage != NULL ) {
		currentRenderWidth =
				Max( 1, globalImages->currentRenderImage->GetUploadWidth() );
		currentRenderHeight =
				Max( 1, globalImages->currentRenderImage->GetUploadHeight() );
	}
	// Slots 12..15 are reserved across all embedded material modules for
	// framebuffer/current-render facts. SniperStretch2 consumes them to
	// support both the retail and openQ4 override parameter contracts.
	parms[ 12 ][ 0 ] = (float)VK_Exec_ActiveFramebufferHeight();
	parms[ 13 ][ 0 ] = (float)viewDef->viewport.x1;
	parms[ 13 ][ 1 ] = (float)viewDef->viewport.y1;
	parms[ 14 ][ 0 ] = (float)viewportWidth;
	parms[ 14 ][ 1 ] = (float)viewportHeight;
	parms[ 15 ][ 0 ] = (float)viewportWidth / (float)currentRenderWidth;
	parms[ 15 ][ 1 ] = (float)viewportHeight / (float)currentRenderHeight;

	const int uniformOffset =
			VK_Exec_InteractionUniformAlloc( parms, sizeof( parms ) );
	if ( uniformOffset < 0 ) {
		return false;
	}

	unsigned int boundTextureMask = 0;
	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		bool cube = false;
		const int setIndex = VK_Exec_GLSLTextureSet(
				family, newStage->shaderTextureNames[ i ], cube );
		if ( setIndex < 0 || setIndex >= 6 ) {
			continue;
		}
		idImage *image = RB_ResolveGLSLShaderTextureImage( newStage, i, NULL );
		if ( image == NULL ) {
			return false;
		}
		image->SetSamplerState( newStage->shaderTextureFilters[ i ],
				newStage->shaderTextureRepeats[ i ] );
		const vkImageEntry_t *entry =
				VK_Image_GetEntry( image->GetDeviceHandle() );
		if ( entry == NULL || entry->isCube != cube ) {
			return false;
		}
		const VkDescriptorSet descriptor =
				VK_GuiExecutor_GetImageDescriptor( image->GetDeviceHandle() );
		if ( descriptor == VK_NULL_HANDLE ) {
			return false;
		}
		vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.interactionPipelineLayout, (uint32_t)setIndex, 1,
				&descriptor, 0, NULL );
		boundTextureMask |= 1u << setIndex;
	}
	// The shipped DepthTexture hardwareShader has no shaderTexture entry.
	// The original program consequently saw opaque coverage; bind the
	// engine-owned white image explicitly so the Vulkan descriptor contract
	// remains valid without requiring an asset override.
	if ( family == VK_GLSL_PROGRAM_FAMILY_DEPTH_TEXTURE
			&& ( boundTextureMask & 0x01 ) == 0 ) {
		idImage *image = globalImages->whiteImage;
		const vkImageEntry_t *entry = image != NULL
				? VK_Image_GetEntry( image->GetDeviceHandle() ) : NULL;
		if ( entry == NULL || entry->isCube ) {
			return false;
		}
		const VkDescriptorSet descriptor =
				VK_GuiExecutor_GetImageDescriptor( image->GetDeviceHandle() );
		if ( descriptor == VK_NULL_HANDLE ) {
			return false;
		}
		vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.interactionPipelineLayout, 0, 1, &descriptor, 0, NULL );
		boundTextureMask |= 0x01;
	}
	const unsigned int requiredMask =
			VK_Exec_GLSLRequiredTextureMask( family );
	if ( requiredMask == UINT_MAX
			|| ( boundTextureMask & requiredMask ) != requiredMask ) {
		return false;
	}

	const bool separateColor =
			VK_Exec_GLSLFamilyUsesVertexColor( family )
			&& VK_Exec_BindGLSLStageColor( drawSurf, tri, stage, stageNum );
	const VkPipeline pipeline = VK_Exec_GetGLSLMaterialPipeline(
			family, stage->drawStateBits, separateColor );
	if ( pipeline == VK_NULL_HANDLE ) {
		return false;
	}

	vkGuiPushConstants_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	if ( separateColor ) {
		push.stageColor[ 0 ] = 1.0f;
		push.stageColor[ 1 ] = 1.0f;
		push.stageColor[ 2 ] = 1.0f;
		push.stageColor[ 3 ] = 1.0f;
	} else if ( drawSurf->shaderRegisters != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			push.stageColor[ i ] =
					drawSurf->shaderRegisters[ stage->color.registers[ i ] ];
		}
	} else {
		push.stageColor[ 0 ] = 1.0f;
		push.stageColor[ 1 ] = 1.0f;
		push.stageColor[ 2 ] = 1.0f;
		push.stageColor[ 3 ] = 1.0f;
	}
	switch ( stage->vertexColor ) {
		case SVC_MODULATE:			push.params[ 0 ] = 1.0f; break;
		case SVC_INVERSE_MODULATE:	push.params[ 0 ] = 2.0f; break;
		default:					push.params[ 0 ] = 0.0f; break;
	}
	push.params[ 1 ] = VK_Exec_AlphaTestModeValue( stage );
	push.params[ 2 ] = stage->hasAlphaTest
			&& drawSurf->shaderRegisters != NULL
			? drawSurf->shaderRegisters[ stage->alphaTestRegister ] : 0.0f;

	VK_Exec_SetProgramStageDepthState( vkExec.cmd, stage, worldDepthState );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
			1, &dynamicOffset );
	vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
	vkCmdDrawIndexed( vkExec.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );

	static bool loggedFamily[ VK_GLSL_PROGRAM_FAMILY_COUNT ];
	if ( !loggedFamily[ family ] ) {
		loggedFamily[ family ] = true;
		common->Printf( "Vulkan: first native GLSL family %d stage drew (%s)\n",
				(int)family, drawSurf->material->GetName() );
	}
	return true;
}

/*
====================
VK_GuiExecutor_PrepareSpecialEffects

RC_DRAW_SPECIAL_EFFECTS is deliberately adjacent to its RC_DRAW_VIEW.  The
legacy GL backend performs the Raven controller's source capture immediately;
Vulkan keeps the view pointer and consumes it from the matching world walk,
after the scene color and depth are complete and before any later HUD view.
====================
*/
void VK_GuiExecutor_PrepareSpecialEffects( const viewDef_t *viewDef ) {
	int activeMask = tr.specialEffectsEnabled;
	if ( r_forceSpecialEffects.GetInteger() > 0 ) {
		activeMask = r_forceSpecialEffects.GetInteger();
	}
	activeMask &= SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL;

	vkExec.pendingSpecialEffectsView =
			activeMask != 0 ? viewDef : NULL;
	vkExec.pendingSpecialEffectsMask = activeMask;
	vkExec.pendingSpecialEffectsSource = NULL;
	vkExec.pendingSpecialEffectsNeedsResolve = false;
}

static void VK_Exec_SetSpecialEffectsViewport( const viewDef_t *viewDef ) {
	const int framebufferHeight = VK_Exec_ActiveFramebufferHeight();
	const int viewportWidth =
			viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewportHeight =
			viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	VkViewport viewport;
	if ( !VK_BuildCanonicalViewport( viewport,
			(float)viewDef->viewport.x1, (float)viewDef->viewport.y1,
			(float)viewportWidth, (float)viewportHeight, (float)framebufferHeight ) ) {
		return;
	}
	vkCmdSetViewport( vkExec.cmd, 0, 1, &viewport );

	VkRect2D scissor;
	scissor.offset.x =
			Max( 0, viewDef->viewport.x1 + viewDef->scissor.x1 );
	scissor.offset.y = framebufferHeight
			- viewDef->viewport.y1 - viewDef->scissor.y2 - 1;
	scissor.offset.y = Max( 0, scissor.offset.y );
	scissor.extent.width = (uint32_t)Max( 0,
			viewDef->scissor.x2 - viewDef->scissor.x1 + 1 );
	scissor.extent.height = (uint32_t)Max( 0,
			viewDef->scissor.y2 - viewDef->scissor.y1 + 1 );
	vkCmdSetScissor( vkExec.cmd, 0, 1, &scissor );
}

static void VK_Exec_InitSpecialEffectsPushConstants(
		vkGuiPushConstants_t &push ) {
	memset( &push, 0, sizeof( push ) );
	push.mvp[ 0 ] = 1.0f;
	push.mvp[ 5 ] = 1.0f;
	push.mvp[ 10 ] = 1.0f;
	push.mvp[ 15 ] = 1.0f;
	push.stageColor[ 0 ] = 1.0f;
	push.stageColor[ 1 ] = 1.0f;
	push.stageColor[ 2 ] = 1.0f;
	push.stageColor[ 3 ] = 1.0f;
}

static bool VK_Exec_BindSpecialEffectsQuad( float x1, float y1,
		float x2, float y2 ) {
	idDrawVert vertices[ 4 ];
	memset( vertices, 0, sizeof( vertices ) );
	vertices[ 0 ].xyz.Set( x1, y1, 0.0f );
	vertices[ 0 ].st.Set( 0.0f, 0.0f );
	vertices[ 1 ].xyz.Set( x2, y1, 0.0f );
	vertices[ 1 ].st.Set( 1.0f, 0.0f );
	vertices[ 2 ].xyz.Set( x2, y2, 0.0f );
	vertices[ 2 ].st.Set( 1.0f, 1.0f );
	vertices[ 3 ].xyz.Set( x1, y2, 0.0f );
	vertices[ 3 ].st.Set( 0.0f, 1.0f );
	for ( int i = 0; i < 4; i++ ) {
		vertices[ i ].color[ 0 ] = 255;
		vertices[ i ].color[ 1 ] = 255;
		vertices[ i ].color[ 2 ] = 255;
		vertices[ i ].color[ 3 ] = 255;
	}

	const glIndex_t indices[ 6 ] = { 0, 1, 2, 0, 2, 3 };
	const int vertexOffset = VK_Ring_Alloc(
			vkExec.vertexRings[ vkExec.frameSlot ],
			vertices, sizeof( vertices ), 64 );
	const int indexOffset = VK_Ring_Alloc(
			vkExec.indexRings[ vkExec.frameSlot ],
			indices, sizeof( indices ), 4 );
	if ( vertexOffset < 0 || indexOffset < 0 ) {
		return false;
	}

	const VkBuffer vertexBuffer =
			vkExec.vertexRings[ vkExec.frameSlot ].buffer;
	const VkDeviceSize vertexBindOffset = (VkDeviceSize)vertexOffset;
	vkCmdBindVertexBuffers( vkExec.cmd, 0, 1,
			&vertexBuffer, &vertexBindOffset );
	vkCmdBindIndexBuffer( vkExec.cmd,
			vkExec.indexRings[ vkExec.frameSlot ].buffer,
			(VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );
	return true;
}

static bool VK_Exec_DrawRVSpecialBlur( const viewDef_t *viewDef ) {
	if ( globalImages == NULL || globalImages->currentRenderImage == NULL
			|| globalImages->currentDepthImage == NULL ) {
		return false;
	}
	if ( !VK_Exec_CaptureCurrentRender( viewDef )
			|| !VK_Exec_CaptureCurrentDepth( viewDef ) ) {
		return false;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	idImage *depthImage = globalImages->currentDepthImage;
	sceneImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
	depthImage->SetSamplerState( TF_NEAREST, TR_CLAMP );
	const VkDescriptorSet sceneSet =
			VK_GuiExecutor_GetImageDescriptor( sceneImage->GetDeviceHandle() );
	const VkDescriptorSet depthSet =
			VK_GuiExecutor_GetImageDescriptor( depthImage->GetDeviceHandle() );
	if ( sceneSet == VK_NULL_HANDLE || depthSet == VK_NULL_HANDLE ) {
		return false;
	}

	const int sourceWidth = Max( 1, sceneImage->GetUploadWidth() );
	const int sourceHeight = Max( 1, sceneImage->GetUploadHeight() );
	const float distanceScale =
			Max( tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][ 7 ], 1.0f );
	float parms[ 16 ][ 4 ];
	memset( parms, 0, sizeof( parms ) );
	parms[ 0 ][ 0 ] = 1.0f / (float)sourceWidth;
	parms[ 0 ][ 1 ] = 1.0f / (float)sourceHeight;
	// The retail controller stores focus/range against its normalized
	// 256x256 depth texture. The native pass consumes view-space distance,
	// so translate the authored controller values at this boundary.
	parms[ 1 ][ 0 ] = Max(
			tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][ 4 ]
					* distanceScale * 0.125f,
			1.0f );
	parms[ 2 ][ 0 ] = idMath::ClampFloat( 0.0f, 1.0f,
			tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][ 5 ] )
					* distanceScale;
	for ( int i = 0; i < 4; i++ ) {
		parms[ 3 ][ i ] =
				tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][ i ];
	}
	parms[ 4 ][ 0 ] = idMath::ClampFloat( 0.0f, 1.0f,
			tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][ 6 ] );
	parms[ 5 ][ 0 ] = VK_RVSPECIAL_DEPTH_ZNEAR;

	const int uniformOffset =
			VK_Exec_InteractionUniformAlloc( parms, sizeof( parms ) );
	const VkPipeline pipeline = VK_Exec_GetGLSLMaterialPipeline(
			VK_GLSL_PROGRAM_FAMILY_DEPTH_AWARE_BLUR, 0, false );
	if ( uniformOffset < 0 || pipeline == VK_NULL_HANDLE
			|| !VK_Exec_BindSpecialEffectsQuad(
					-1.0f, -1.0f, 1.0f, 1.0f ) ) {
		return false;
	}

	VK_Exec_SetSpecialEffectsViewport( viewDef );
	vkCmdSetDepthTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( vkExec.cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( vkExec.cmd, VK_CULL_MODE_NONE );
	vkCmdSetStencilTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthBiasEnable( vkExec.cmd, VK_FALSE );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 0, 1, &sceneSet, 0, NULL );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 1, 1, &depthSet, 0, NULL );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
			1, &dynamicOffset );
	vkGuiPushConstants_t push;
	VK_Exec_InitSpecialEffectsPushConstants( push );
	vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( 6 ), (int)( 4 ) );
	vkCmdDrawIndexed( vkExec.cmd, 6, 1, 0, 0, 0 );
	return true;
}

static bool VK_Exec_ProjectRVSpecialLight( const viewDef_t *viewDef,
		const idVec3 &origin, float size, float &x1, float &y1,
		float &x2, float &y2, idVec3 &eyePoint, float &lightDepth ) {
	idPlane eye;
	idPlane clip;
	const idVec3 right = viewDef->renderView.viewaxis[ 1 ] * size;
	const idVec3 up = viewDef->renderView.viewaxis[ 2 ] * size;
	const idVec3 points[ 4 ] = {
		origin + right + up,
		origin - right + up,
		origin - right - up,
		origin + right - up
	};
	const int viewportWidth =
			viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewportHeight =
			viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	R_TransformModelToClip( origin,
			viewDef->worldSpace.modelViewMatrix,
			viewDef->projectionMatrix, eye, clip );
	if ( clip[ 3 ] <= 0.0f ) {
		return false;
	}
	lightDepth = -eye[ 2 ];
	if ( lightDepth <= 0.0f ) {
		return false;
	}

	x1 = idMath::INFINITY;
	y1 = idMath::INFINITY;
	x2 = -idMath::INFINITY;
	y2 = -idMath::INFINITY;
	for ( int i = 0; i < 4; i++ ) {
		idVec3 ndc;
		R_TransformModelToClip( points[ i ],
				viewDef->worldSpace.modelViewMatrix,
				viewDef->projectionMatrix, eye, clip );
		if ( clip[ 3 ] <= 0.0f ) {
			return false;
		}
		R_TransformClipToDevice( clip, viewDef, ndc );
		const float screenX =
				( ndc.x * 0.5f + 0.5f ) * viewportWidth;
		const float screenY =
				( 1.0f - ( ndc.y * 0.5f + 0.5f ) )
						* viewportHeight;
		x1 = Min( x1, screenX );
		y1 = Min( y1, screenY );
		x2 = Max( x2, screenX );
		y2 = Max( y2, screenY );
	}
	if ( x2 < 0.0f || y2 < 0.0f
			|| x1 > viewportWidth || y1 > viewportHeight ) {
		return false;
	}
	x1 = idMath::ClampFloat( 0.0f, (float)viewportWidth, x1 );
	y1 = idMath::ClampFloat( 0.0f, (float)viewportHeight, y1 );
	x2 = idMath::ClampFloat( 0.0f, (float)viewportWidth, x2 );
	y2 = idMath::ClampFloat( 0.0f, (float)viewportHeight, y2 );
	R_LocalPointToGlobal(
			viewDef->worldSpace.modelViewMatrix, origin, eyePoint );
	return x2 > x1 && y2 > y1;
}

static bool VK_Exec_DrawRVSpecialAL( const viewDef_t *viewDef ) {
	if ( globalImages == NULL || globalImages->currentDepthImage == NULL
			|| tr.primaryWorld == NULL ) {
		return false;
	}
	if ( !backEnd.currentDepthCopied
			&& !VK_Exec_CaptureCurrentDepth( viewDef ) ) {
		return false;
	}
	if ( tr.specialALLightImage == NULL ) {
		tr.specialALLightImage =
				globalImages->GetImage( "gfx/lights/round.tga" );
	}
	if ( tr.specialALLightImage == NULL ) {
		static bool warnedMissingLightImage = false;
		if ( !warnedMissingLightImage ) {
			warnedMissingLightImage = true;
			common->Warning(
					"Vulkan: Alpha Labs special effect is missing gfx/lights/round.tga" );
		}
		return false;
	}

	idImage *depthImage = globalImages->currentDepthImage;
	depthImage->SetSamplerState( TF_NEAREST, TR_CLAMP );
	tr.specialALLightImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
	const VkDescriptorSet depthSet =
			VK_GuiExecutor_GetImageDescriptor( depthImage->GetDeviceHandle() );
	const VkDescriptorSet lightSet = VK_GuiExecutor_GetImageDescriptor(
			tr.specialALLightImage->GetDeviceHandle() );
	const VkPipeline pipeline = VK_Exec_GetGLSLMaterialPipeline(
			VK_GLSL_PROGRAM_FAMILY_AL,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, false );
	if ( depthSet == VK_NULL_HANDLE || lightSet == VK_NULL_HANDLE
			|| pipeline == VK_NULL_HANDLE ) {
		return false;
	}

	VK_Exec_SetSpecialEffectsViewport( viewDef );
	vkCmdSetDepthTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( vkExec.cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( vkExec.cmd, VK_CULL_MODE_NONE );
	vkCmdSetStencilTestEnable( vkExec.cmd, VK_FALSE );
	vkCmdSetDepthBiasEnable( vkExec.cmd, VK_FALSE );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 0, 1, &depthSet, 0, NULL );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 1, 1, &lightSet, 0, NULL );

	const int viewportWidth =
			viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewportHeight =
			viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	const float distanceScale = 2000.0f;
	int drawnLights = 0;
	for ( int i = 0; i < tr.primaryWorld->lightDefs.Num(); i++ ) {
		idRenderLightLocal *light = tr.primaryWorld->lightDefs[ i ];
		if ( light == NULL ) {
			continue;
		}
		idVec3 lightColor( light->parms.shaderParms[ 0 ],
				light->parms.shaderParms[ 1 ],
				light->parms.shaderParms[ 2 ] );
		if ( lightColor.LengthSqr() <= idMath::FLOAT_EPSILON ) {
			continue;
		}
		lightColor.Normalize();

		float x1;
		float y1;
		float x2;
		float y2;
		float lightDepth;
		idVec3 eyePoint;
		const float lightSize = 300.0f;
		if ( !VK_Exec_ProjectRVSpecialLight( viewDef,
				light->globalLightOrigin, lightSize,
				x1, y1, x2, y2, eyePoint, lightDepth ) ) {
			continue;
		}

		float parms[ 16 ][ 4 ];
		memset( parms, 0, sizeof( parms ) );
		parms[ 0 ][ 0 ] = distanceScale;
		parms[ 1 ][ 0 ] = 1.0f;
		parms[ 1 ][ 1 ] = 1.0f;
		parms[ 2 ][ 0 ] = eyePoint[ 0 ];
		parms[ 2 ][ 1 ] = eyePoint[ 1 ];
		parms[ 2 ][ 2 ] = eyePoint[ 2 ];
		parms[ 3 ][ 0 ] = lightColor[ 0 ];
		parms[ 3 ][ 1 ] = lightColor[ 1 ];
		parms[ 3 ][ 2 ] = lightColor[ 2 ];
		parms[ 3 ][ 3 ] = 1.0f;
		parms[ 4 ][ 0 ] = lightSize;
		parms[ 6 ][ 0 ] = lightDepth;
		// Direct Vulkan controller capture samples the resolved hardware
		// depth image. Stock material draws leave this marker at zero and
		// continue sampling the authored encoded DepthTexture.
		parms[ 7 ][ 0 ] = 1.0f;
		parms[ 12 ][ 0 ] = (float)VK_Exec_ActiveFramebufferHeight();
		parms[ 13 ][ 0 ] = (float)viewDef->viewport.x1;
		parms[ 13 ][ 1 ] = (float)viewDef->viewport.y1;
		parms[ 14 ][ 0 ] = (float)viewportWidth;
		parms[ 14 ][ 1 ] = (float)viewportHeight;
		const int uniformOffset =
				VK_Exec_InteractionUniformAlloc( parms, sizeof( parms ) );
		if ( uniformOffset < 0 ) {
			break;
		}

		const float ndcX1 = x1 * 2.0f / viewportWidth - 1.0f;
		const float ndcX2 = x2 * 2.0f / viewportWidth - 1.0f;
		const float ndcY1 = 1.0f - y1 * 2.0f / viewportHeight;
		const float ndcY2 = 1.0f - y2 * 2.0f / viewportHeight;
		if ( !VK_Exec_BindSpecialEffectsQuad(
				ndcX1, ndcY2, ndcX2, ndcY1 ) ) {
			break;
		}
		const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
		const uint32_t dynamicOffset = (uint32_t)uniformOffset;
		vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
				1, &dynamicOffset );
		vkGuiPushConstants_t push;
		VK_Exec_InitSpecialEffectsPushConstants( push );
		vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( push ), &push );
		VK_Device_CountDrawIndexed( (int)( 6 ), (int)( 4 ) );
		vkCmdDrawIndexed( vkExec.cmd, 6, 1, 0, 0, 0 );
		drawnLights++;
	}
	return drawnLights > 0;
}

static void VK_Exec_DrawRVSpecialEffects( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || vkExec.pendingSpecialEffectsView != viewDef ) {
		if ( viewDef != NULL && vkExec.pendingSpecialEffectsMask != 0 ) {
			static bool warnedSpecialEffectsViewMismatch = false;
			if ( !warnedSpecialEffectsViewMismatch ) {
				warnedSpecialEffectsViewMismatch = true;
				common->Warning(
						"Vulkan: Raven special-effects view mismatch (pending=%p draw=%p)",
						vkExec.pendingSpecialEffectsView, viewDef );
			}
		}
		return;
	}
	if ( vkExec.activePipelineTarget.samples != VK_SAMPLE_COUNT_1_BIT ) {
		// The forward scene is resolved by the immediately following
		// RC_RESOLVE_MSAA command. Keep the matching viewDef alive and apply
		// the controller to that single-sample destination instead of trying
		// to feed vkCmdCopyImage from a multisampled attachment.
		vkExec.pendingSpecialEffectsSource = vkExec.activeRenderTexture;
		vkExec.pendingSpecialEffectsNeedsResolve = true;
		return;
	}
	const int activeMask = vkExec.pendingSpecialEffectsMask;
	const bool sharedSpecialFrame = r_rendererSharedSpecialFrame.GetBool()
		&& R_ClassicSpecialFrameDomain_FindRavenEffectsView( viewDef ) != NULL;
	vkExec.pendingSpecialEffectsView = NULL;
	vkExec.pendingSpecialEffectsMask = 0;
	vkExec.pendingSpecialEffectsSource = NULL;
	vkExec.pendingSpecialEffectsNeedsResolve = false;

	bool drewBlur = false;
	bool drewAL = false;
	if ( ( activeMask & SPECIAL_EFFECT_BLUR ) != 0 ) {
		drewBlur = VK_Exec_DrawRVSpecialBlur( viewDef );
	}
	if ( ( activeMask & SPECIAL_EFFECT_AL ) != 0 ) {
		drewAL = VK_Exec_DrawRVSpecialAL( viewDef );
	}
	if ( sharedSpecialFrame && drewBlur ) {
		(void)R_ClassicSpecialFrameDomain_RecordOwned( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
			CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, SPECIAL_EFFECT_BLUR );
	}
	if ( sharedSpecialFrame && drewAL ) {
		(void)R_ClassicSpecialFrameDomain_RecordOwned( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
			CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, SPECIAL_EFFECT_AL );
	}

	static bool loggedBlur = false;
	static bool loggedAL = false;
	if ( drewBlur && !loggedBlur ) {
		loggedBlur = true;
		common->Printf(
				"Vulkan: Raven MedLabs controller drew its native depth-aware blur pass\n" );
	}
	if ( drewAL && !loggedAL ) {
		loggedAL = true;
		common->Printf(
				"Vulkan: Raven Alpha Labs controller drew projected light overlays\n" );
	}
}

bool VK_GuiExecutor_SpecialEffectsAwaitResolve(
		idRenderTexture *sourceRenderTexture ) {
	return vkExec.pendingSpecialEffectsNeedsResolve
			&& vkExec.pendingSpecialEffectsView != NULL
			&& vkExec.pendingSpecialEffectsMask != 0
			&& vkExec.pendingSpecialEffectsSource == sourceRenderTexture;
}

void VK_GuiExecutor_DrawResolvedSpecialEffects(
		idRenderTexture *sourceRenderTexture,
		idRenderTexture *destinationRenderTexture ) {
	if ( !VK_GuiExecutor_SpecialEffectsAwaitResolve( sourceRenderTexture )
			|| destinationRenderTexture == NULL ) {
		return;
	}

	idRenderTexture *savedRenderTexture = vkExec.activeRenderTexture;
	idRenderTexture *savedBackEndRenderTexture = backEnd.renderTexture;
	idRenderTexture *savedFeedbackRenderTexture =
			backEnd.feedbackRenderTexture;
	if ( !VK_Exec_SetRenderTarget( destinationRenderTexture ) ) {
		return;
	}
	backEnd.renderTexture = destinationRenderTexture;
	backEnd.feedbackRenderTexture = NULL;
	vkExec.pendingSpecialEffectsNeedsResolve = false;
	const viewDef_t *effectsView = vkExec.pendingSpecialEffectsView;
	vkSceneScaleState_t sceneScaleState;
	VK_TemporalPresentation_ClearScaleState( sceneScaleState );
	const viewDef_t *rootView =
		VK_TemporalPresentation_RootForView( effectsView );
	if ( rootView != NULL ) {
		(void)VK_TemporalPresentation_BeginScale( sceneScaleState,
			effectsView, rootView,
			(int)vkExec.activeExtent.width, (int)vkExec.activeExtent.height, false );
	}
	VK_Exec_DrawRVSpecialEffects( effectsView );
	VK_TemporalPresentation_RestoreScale( sceneScaleState, effectsView );

	(void)VK_Exec_SetRenderTarget( savedRenderTexture );
	backEnd.renderTexture = savedBackEndRenderTexture;
	backEnd.feedbackRenderTexture = savedFeedbackRenderTexture;
}

// Draws the stock ARB newStage families for which Vulkan has native SPIR-V.
// The stable family query deliberately avoids depending on parser handle
// allocation order.
static void VK_Exec_DrawProgramStage( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri, const float mvp[ 16 ],
		bool worldDepthState, const shaderStage_t *stage, int stageNum ) {
	const newShaderStage_t *newStage = stage->newStage;
	const idMaterial *shader = drawSurf->material;
	const float *regs = drawSurf->shaderRegisters;
	if ( newStage == NULL || newStage->customLighting ) {
		return;
	}
	if ( r_skipNewAmbient.GetBool() && shader->GetSort() < SS_POST_PROCESS ) {
		return;
	}
	if ( newStage->glslProgram ) {
		(void)VK_Exec_DrawGLSLProgramStage( viewDef, drawSurf, tri, mvp,
				worldDepthState, stage, stageNum );
		return;
	}

	const vkMaterialProgramFamily_t fragmentFamily =
			R_GetARBProgramFamily( GL_FRAGMENT_PROGRAM_ARB,
				newStage->fragmentProgram );
	const vkMaterialProgramFamily_t vertexFamily =
			R_GetARBProgramFamily( GL_VERTEX_PROGRAM_ARB,
				newStage->vertexProgram );
	vkMaterialProgramFamily_t family = fragmentFamily;
	if ( family == VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN ) {
		family = vertexFamily;
	}
	if ( family == VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN
			|| ( fragmentFamily != VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN
				&& vertexFamily != VK_MATERIAL_PROGRAM_FAMILY_UNKNOWN
				&& fragmentFamily != vertexFamily ) ) {
		static bool loggedUnknownProgram = false;
		if ( !loggedUnknownProgram ) {
			loggedUnknownProgram = true;
			common->Printf( "Vulkan: unsupported ARB material program skipped (%s)\n",
					shader->GetName() );
		}
		return;
	}

	if ( family == VK_MATERIAL_PROGRAM_FAMILY_MONOCHROME ) {
		// The retail PK4 names monochrome.vfp but does not contain its source.
		// Preserve the authored test material with the documented grayscale
		// reconstruction embedded above: fragmentMap 0, sampled alpha, and
		// the ordinary material blend/depth/alpha-test contract.
		if ( newStage->numFragmentProgramImages < 1
				|| newStage->fragmentProgramImages[ 0 ] == NULL ) {
			return;
		}
		const VkDescriptorSet imageSet = VK_Exec_ImageDescriptor(
				newStage->fragmentProgramImages[ 0 ]->GetDeviceHandle(), true );
		const VkPipeline pipeline = VK_Exec_GetProgramPipeline(
				family, stage->drawStateBits, false );
		if ( imageSet == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE ) {
			return;
		}

		vkGuiPushConstants_t push;
		memset( &push, 0, sizeof( push ) );
		memcpy( push.mvp, mvp, sizeof( push.mvp ) );
		if ( regs != NULL ) {
			for ( int i = 0; i < 4; i++ ) {
				push.stageColor[ i ] =
						regs[ stage->color.registers[ i ] ];
			}
		} else {
			push.stageColor[ 0 ] = push.stageColor[ 1 ] =
					push.stageColor[ 2 ] = push.stageColor[ 3 ] = 1.0f;
		}
		push.params[ 1 ] = VK_Exec_AlphaTestModeValue( stage );
		push.params[ 2 ] = stage->hasAlphaTest && regs != NULL
				? regs[ stage->alphaTestRegister ] : 0.0f;

		VK_Exec_SetProgramStageDepthState( vkExec.cmd, stage, worldDepthState );
		vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.interactionPipelineLayout, 0, 1, &imageSet, 0, NULL );
		vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( push ), &push );
		VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
		vkCmdDrawIndexed( vkExec.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );

		static bool loggedMonochromeProgram = false;
		if ( !loggedMonochromeProgram ) {
			loggedMonochromeProgram = true;
			common->Printf( "Vulkan: first monochrome compatibility program stage drew (%s)\n",
					shader->GetName() );
		}
		return;
	}

	if ( family == VK_MATERIAL_PROGRAM_FAMILY_REFRACTIVE_GLASS ) {
		if ( VK_Exec_DrawRefractiveGlassStage( viewDef, drawSurf, tri, mvp,
				worldDepthState, stage ) ) {
			static bool loggedRefractiveGlass = false;
			if ( !loggedRefractiveGlass ) {
				loggedRefractiveGlass = true;
				common->Printf(
						"Vulkan: first refractive-glass compatibility stage drew (%s)\n",
						shader->GetName() );
			}
		}
		return;
	}

	if ( family == VK_MATERIAL_PROGRAM_FAMILY_BUMPY_ENVIRONMENT ) {
		if ( VK_Exec_DrawBumpyProgramStage( viewDef, drawSurf, tri, mvp,
				worldDepthState, stage ) ) {
			static bool loggedBumpyProgram = false;
			if ( !loggedBumpyProgram ) {
				loggedBumpyProgram = true;
				common->Printf( "Vulkan: first explicit bumpy-environment program stage drew (%s)\n",
						shader->GetName() );
			}
		}
		return;
	}

	const bool masked = family == VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK
			|| family == VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_GRAY_WITH_MASK
			|| family == VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX;
	const bool vertexColorVariant =
			family == VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE_WITH_MASK_AND_VERTEX;
	if ( family != VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE && !masked ) {
		return;
	}
	const int numTextures = masked ? 3 : 2;
	if ( newStage->numFragmentProgramImages < numTextures
			|| drawSurf->space == NULL ) {
		return;
	}
	VkDescriptorSet textureSets[ 3 ];
	for ( int i = 0; i < numTextures; i++ ) {
		idImage *image = newStage->fragmentProgramImages[ i ];
		if ( image == NULL ) {
			return;
		}
		textureSets[ i ] =
				VK_Exec_ImageDescriptor( image->GetDeviceHandle(), true );
		if ( textureSets[ i ] == VK_NULL_HANDLE ) {
			return;
		}
	}

	float parms[ 8 ][ 4 ];
	memset( parms, 0, sizeof( parms ) );
	for ( int i = 0; i < newStage->numVertexParms && i < 2; i++ ) {
		for ( int j = 0; j < 4; j++ ) {
			parms[ i ][ j ] = regs != NULL
					? regs[ newStage->vertexParms[ i ][ j ] ] : 0.0f;
		}
	}
	const float *modelView = drawSurf->space->modelViewMatrix;
	const float *projection = viewDef->projectionMatrix;
	parms[ 2 ][ 0 ] = modelView[ 2 ];
	parms[ 2 ][ 1 ] = modelView[ 6 ];
	parms[ 2 ][ 2 ] = modelView[ 10 ];
	parms[ 2 ][ 3 ] = modelView[ 14 ];
	parms[ 3 ][ 0 ] = projection[ 0 ];
	parms[ 3 ][ 1 ] = projection[ 4 ];
	parms[ 3 ][ 2 ] = projection[ 8 ];
	parms[ 3 ][ 3 ] = projection[ 12 ];
	parms[ 4 ][ 0 ] = projection[ 3 ];
	parms[ 4 ][ 1 ] = projection[ 7 ];
	parms[ 4 ][ 2 ] = projection[ 11 ];
	parms[ 4 ][ 3 ] = projection[ 15 ];

	const int viewportWidth =
			viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewportHeight =
			viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}
	int textureWidth = viewportWidth;
	int textureHeight = viewportHeight;
	if ( globalImages->currentRenderImage != NULL ) {
		if ( globalImages->currentRenderImage->GetUploadWidth() > 0 ) {
			textureWidth =
					globalImages->currentRenderImage->GetUploadWidth();
		}
		if ( globalImages->currentRenderImage->GetUploadHeight() > 0 ) {
			textureHeight =
					globalImages->currentRenderImage->GetUploadHeight();
		}
	}
	parms[ 5 ][ 0 ] = (float)viewportWidth / (float)textureWidth;
	parms[ 5 ][ 1 ] = (float)viewportHeight / (float)textureHeight;
	parms[ 5 ][ 3 ] = 1.0f;
	parms[ 6 ][ 0 ] = 1.0f / (float)viewportWidth;
	parms[ 6 ][ 1 ] = 1.0f / (float)viewportHeight;
	parms[ 6 ][ 2 ] = (float)VK_Exec_ActiveFramebufferHeight();
	parms[ 7 ][ 0 ] = (float)viewDef->viewport.x1;
	parms[ 7 ][ 1 ] = (float)viewDef->viewport.y1;

	const int uniformOffset =
			VK_Exec_InteractionUniformAlloc( parms, sizeof( parms ) );
	if ( uniformOffset < 0 ) {
		return;
	}

	bool separateColor = false;
	if ( vertexColorVariant && drawSurf->decalColorCache != NULL
			&& stageNum >= 0 && stageNum < drawSurf->decalColorStageCount
			&& drawSurf->decalColorStride >= tri->numVerts * 4 ) {
		// Offset zero is a legal VBO-backed Position() result; see
		// VK_Exec_BindGLSLStageColor.
		const void *colorData = vertexCache.Position( drawSurf->decalColorCache );
		const size_t colorBytes = (size_t)tri->numVerts * 4;
		const int colorOffset = VK_Ring_Alloc(
				vkExec.vertexRings[ vkExec.frameSlot ],
				RB_DrawVertAttributePointer( colorData,
					drawSurf->decalColorOffset + stageNum * drawSurf->decalColorStride ),
				colorBytes, 4 );
		if ( colorOffset >= 0 ) {
			const VkBuffer colorBuffer =
					vkExec.vertexRings[ vkExec.frameSlot ].buffer;
			const VkDeviceSize bindOffset = (VkDeviceSize)colorOffset;
			vkCmdBindVertexBuffers( vkExec.cmd, 1, 1,
					&colorBuffer, &bindOffset );
			separateColor = true;
		}
	}

	VkPipeline pipeline = VK_Exec_GetProgramPipeline( family,
			stage->drawStateBits, separateColor );
	if ( pipeline == VK_NULL_HANDLE ) {
		return;
	}
	vkGuiPushConstants_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	if ( regs != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			push.stageColor[ i ] =
					regs[ stage->color.registers[ i ] ];
		}
	} else {
		push.stageColor[ 0 ] = push.stageColor[ 1 ] =
				push.stageColor[ 2 ] = push.stageColor[ 3 ] = 1.0f;
	}
	push.params[ 1 ] = VK_Exec_AlphaTestModeValue( stage );
	push.params[ 2 ] = stage->hasAlphaTest && regs != NULL
			? regs[ stage->alphaTestRegister ] : 0.0f;

	VK_Exec_SetProgramStageDepthState( vkExec.cmd, stage, worldDepthState );
	vkCmdBindPipeline( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 0, (uint32_t)numTextures,
			textureSets, 0, NULL );
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( vkExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkExec.interactionPipelineLayout, 6, 1, &uniformSet,
			1, &dynamicOffset );
	vkCmdPushConstants( vkExec.cmd, vkExec.interactionPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
	vkCmdDrawIndexed( vkExec.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );

	static bool loggedHeatHazeFamily[ 4 ] = { false, false, false, false };
	const int logIndex = family - VK_MATERIAL_PROGRAM_FAMILY_HEAT_HAZE;
	if ( logIndex >= 0 && logIndex < 4 && !loggedHeatHazeFamily[ logIndex ] ) {
		loggedHeatHazeFamily[ logIndex ] = true;
		common->Printf( "Vulkan: first heat-haze program family %d stage drew (%s)\n",
				(int)family, shader->GetName() );
	}
}

// mirrors the GL backend's cross-frame cinematic upload dedupe (tr_render.cpp)
static const idCinematic *	vkGuiLastCinematic = NULL;
static int					vkGuiLastCinematicSerial = 0;
static unsigned long long	vkGuiLastCinematicStorageGeneration = 0;

static void VK_Exec_DrawAmbientStages( const viewDef_t *viewDef, const drawSurf_t *drawSurf,
		const srfTriangles_t *tri, const float mvp[ 16 ], bool worldDepthState ) {
	VkCommandBuffer cmd = vkExec.cmd;
	const idMaterial *shader = drawSurf->material;
	const float *regs = drawSurf->shaderRegisters;

	for ( int stageNum = 0; stageNum < shader->GetNumStages(); stageNum++ ) {
		const shaderStage_t *pStage = shader->GetStage( stageNum );

		if ( regs != NULL && regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;	// alpha-mask stage
		}
		if ( pStage->texture.dynamic == DI_REFLECTION_RENDER
				|| pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
			continue;	// capture producer; a later image stage consumes it
		}
		// Feedback is captured lazily at the first stage that samples it.
		// This must run before the program-stage dispatcher/skip so program
		// materials see the same scheduling contract as fixed stages.
		bool feedbackCaptureFailed = false;
		bool captureInterruptedScope = false;
		if ( !backEnd.currentRenderCopied && VK_Exec_StageUsesCurrentRender( pStage ) ) {
			if ( VK_Exec_AutomaticCaptureAllowed() ) {
				if ( VK_Exec_CaptureCurrentRender( viewDef ) ) {
					captureInterruptedScope = true;
				} else {
					feedbackCaptureFailed = true;
				}
			} else {
				backEnd.currentRenderCopied = true;
			}
		}
		if ( !backEnd.currentDepthCopied && VK_Exec_StageUsesCurrentDepth( pStage ) ) {
			if ( VK_Exec_AutomaticCaptureAllowed() ) {
				if ( VK_Exec_CaptureCurrentDepth( viewDef ) ) {
					captureInterruptedScope = true;
				} else {
					feedbackCaptureFailed = true;
				}
			} else {
				backEnd.currentDepthCopied = true;
			}
		}
		if ( captureInterruptedScope ) {
			VK_Exec_RestoreSurfaceState( cmd, viewDef, shader, worldDepthState );
		}
		if ( feedbackCaptureFailed ) {
			continue;
		}
		if ( pStage->newStage != NULL ) {
			// Program stages use the same per-stage depth-bias contract as
			// classic stages.  Decal materials can select either path, so
			// applying privatePolygonOffset only below would leave program
			// decals fighting the receiving surface.
			const bool stagePolygonOffset =
					worldDepthState && pStage->privatePolygonOffset != 0.0f;
			if ( stagePolygonOffset ) {
				vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
				vkCmdSetDepthBias( cmd,
						r_offsetUnits.GetFloat() * pStage->privatePolygonOffset,
						0.0f, r_offsetFactor.GetFloat() );
			}
			VK_Exec_DrawProgramStage( viewDef, drawSurf, tri, mvp,
					worldDepthState, pStage, stageNum );
			if ( stagePolygonOffset ) {
				if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					vkCmdSetDepthBias( cmd,
							r_offsetUnits.GetFloat() * shader->GetPolygonOffset(),
							0.0f, r_offsetFactor.GetFloat() );
				} else {
					vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
				}
			}
			continue;
		}
		// Cube texgens draw through the cube pipeline.  Reflection stages
		// use environment.vfp or bumpyEnvironment.vfp when a normal map exists.
		const int texgen = pStage->texture.texgen;
		const bool cubeStage = texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE || texgen == TG_DIFFUSE_CUBE;
		const bool screenStage = texgen == TG_SCREEN || texgen == TG_SCREEN2;
		const bool glassStage = texgen == TG_GLASSWARP;
		bool reflectStage = false;
		bool bumpyReflectStage = false;
		const shaderStage_t *bumpStage = NULL;
		if ( texgen == TG_REFLECT_CUBE ) {
			if ( drawSurf->space == NULL ) {
				continue;
			}
			reflectStage = true;
			bumpStage = shader->GetBumpStage();
			bumpyReflectStage = bumpStage != NULL;
			if ( bumpyReflectStage && bumpStage->texture.image == NULL ) {
				continue;
			}
		}
		if ( worldDepthState && texgen != TG_EXPLICIT && !cubeStage
				&& !reflectStage && !screenStage && !glassStage ) {
			continue;
		}
		if ( ( texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE ) && drawSurf->dynamicTexCoords == NULL ) {
			continue;	// the front-end texgen produced no direction stream
		}

		float color[ 4 ];
		if ( regs != NULL ) {
			color[ 0 ] = regs[ pStage->color.registers[ 0 ] ];
			color[ 1 ] = regs[ pStage->color.registers[ 1 ] ];
			color[ 2 ] = regs[ pStage->color.registers[ 2 ] ];
			color[ 3 ] = regs[ pStage->color.registers[ 3 ] ];
		} else {
			color[ 0 ] = color[ 1 ] = color[ 2 ] = color[ 3 ] = 1.0f;
		}

		// skip stages that can't change the framebuffer
		const int blendBits = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
		if ( color[ 0 ] <= 0 && color[ 1 ] <= 0 && color[ 2 ] <= 0
				&& blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
			continue;
		}
		if ( color[ 3 ] <= 0 && blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ) {
			continue;
		}

		// texture: cinematic or static image (RB_BindVariableStageImage contract)
		idImage *stageImage = NULL;
		if ( pStage->texture.cinematic != NULL ) {
			if ( r_skipDynamicTextures.GetBool() ) {
				stageImage = globalImages->defaultImage;
			} else {
				cinData_t cin = pStage->texture.cinematic->ImageForTime(
						(int)( 1000 * ( viewDef->floatTime + viewDef->renderView.shaderParms[ 11 ] ) ) );
				if ( cin.image != NULL ) {
					idImage *cinImage = globalImages->cinematicImage;
					const bool sameFrameResident =
						cin.frameSerial != 0
						&& pStage->texture.cinematic == vkGuiLastCinematic
						&& cin.frameSerial == vkGuiLastCinematicSerial
						&& cinImage->IsLoaded()
						&& cinImage->GetStorageGeneration() == vkGuiLastCinematicStorageGeneration
						&& cinImage->GetOpts().textureType == TT_2D
						&& cin.imageHeight != cin.imageWidth * 6;
					if ( !sameFrameResident ) {
						globalImages->cinematicImage->UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
						vkGuiLastCinematic = pStage->texture.cinematic;
						vkGuiLastCinematicSerial = cin.frameSerial;
						vkGuiLastCinematicStorageGeneration = cinImage->GetStorageGeneration();
					}
					stageImage = globalImages->cinematicImage;
				} else {
					stageImage = globalImages->blackImage;
				}
			}
		} else {
			stageImage = pStage->texture.image;
		}
		if ( stageImage == NULL ) {
			continue;
		}
		// cube/reflect stages sample through samplerCube; the descriptor's view must
		// really be a cube view or validation trips
		if ( cubeStage || reflectStage ) {
			const vkImageEntry_t *cubeEntry = VK_Image_GetEntry( stageImage->GetDeviceHandle() );
			if ( cubeEntry == NULL || !cubeEntry->isCube ) {
				continue;
			}
		}
		VkDescriptorSet descriptor = VK_GuiExecutor_GetResidentImageDescriptor( stageImage );
		if ( descriptor == VK_NULL_HANDLE ) {
			continue;
		}
		VkDescriptorSet glassDescriptors[ 2 ] = {
			VK_NULL_HANDLE, VK_NULL_HANDLE
		};
		if ( glassStage ) {
			const vkImageEntry_t *warpEntry =
					VK_Image_GetEntry( stageImage->GetDeviceHandle() );
			if ( warpEntry == NULL || warpEntry->isCube
					|| globalImages->scratchImage2 == NULL
					|| globalImages->scratchImage == NULL ) {
				continue;
			}
			glassDescriptors[ 0 ] = VK_Exec_ImageDescriptor(
					globalImages->scratchImage2->GetDeviceHandle(), true );
			glassDescriptors[ 1 ] = VK_Exec_ImageDescriptor(
					globalImages->scratchImage->GetDeviceHandle(), true );
			if ( glassDescriptors[ 0 ] == VK_NULL_HANDLE
					|| glassDescriptors[ 1 ] == VK_NULL_HANDLE ) {
				continue;
			}
		}
		VkDescriptorSet bumpDescriptor = VK_NULL_HANDLE;
		if ( bumpyReflectStage ) {
			const vkImageEntry_t *bumpEntry =
					VK_Image_GetEntry( bumpStage->texture.image->GetDeviceHandle() );
			if ( bumpEntry == NULL || bumpEntry->isCube ) {
				continue;
			}
			bumpDescriptor =
					VK_GuiExecutor_GetImageDescriptor( bumpStage->texture.image->GetDeviceHandle() );
			if ( bumpDescriptor == VK_NULL_HANDLE ) {
				continue;
			}
		}

		// skybox/wobblesky direction stream: the front-end texgen writes
		// tightly packed vec3s into the CPU-backed vertex cache; stream them
		// into the frame ring and bind them as binding 1 next to the re-bound
		// idDrawVert stream (diffuse cube reads the idDrawVert normal off
		// binding 0 instead, so no extra buffer is needed)
		if ( texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE ) {
			if ( drawSurf->dynamicTexCoords == NULL ) {
				continue;
			}
			const void *dirCoords = vertexCache.Position( drawSurf->dynamicTexCoords );
			const int slot = vkExec.frameSlot;
			const int dirOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], dirCoords,
					static_cast<size_t>( tri->numVerts ) * sizeof( idVec3 ), 16 );
			if ( dirOffset < 0 ) {
				continue;
			}
			VkBuffer dirBuffers[ 2 ] = { vkExec.vertexRings[ slot ].buffer, vkExec.vertexRings[ slot ].buffer };
			VkDeviceSize dirOffsets[ 2 ] = { (VkDeviceSize)vkExec.boundVertexOffset, (VkDeviceSize)dirOffset };
			vkCmdBindVertexBuffers( cmd, 0, 2, dirBuffers, dirOffsets );
		}

		vkGuiPushConstants_t push;
		memcpy( push.mvp, mvp, sizeof( push.mvp ) );
		// decal surfaces bake regs-color (x depth fade) into the uploaded
		// vertex colors; modulating by the regs color again double-applies
		// it (the skip culls above still use the regs color)
		bool bakedDecalStageColor = !cubeStage && !reflectStage && !glassStage
				&& drawSurf->decalColorCache != NULL
				&& stageNum < drawSurf->decalColorStageCount
				&& drawSurf->decalColorStride >= tri->numVerts * 4
				&& pStage->vertexColor != SVC_IGNORE;
		if ( bakedDecalStageColor ) {
			// Offset zero is a legal VBO-backed Position() result; see
			// VK_Exec_BindGLSLStageColor.
			const void *colorData = vertexCache.Position( drawSurf->decalColorCache );
			const size_t colorBytes = static_cast<size_t>( tri->numVerts ) * 4;
			const int colorOffset = VK_Ring_Alloc( vkExec.vertexRings[ vkExec.frameSlot ],
					RB_DrawVertAttributePointer( colorData,
						drawSurf->decalColorOffset + stageNum * drawSurf->decalColorStride ),
					colorBytes, 4 );
			if ( colorOffset >= 0 ) {
				const VkBuffer colorBuffer = vkExec.vertexRings[ vkExec.frameSlot ].buffer;
				const VkDeviceSize colorBindOffset = (VkDeviceSize)colorOffset;
				vkCmdBindVertexBuffers( cmd, 1, 1, &colorBuffer, &colorBindOffset );
			} else {
				bakedDecalStageColor = false;
			}
		}
		if ( bakedDecalStageColor ) {
			push.stageColor[ 0 ] = 1.0f;
			push.stageColor[ 1 ] = 1.0f;
			push.stageColor[ 2 ] = 1.0f;
			push.stageColor[ 3 ] = 1.0f;
		} else {
			push.stageColor[ 0 ] = color[ 0 ];
			push.stageColor[ 1 ] = color[ 1 ];
			push.stageColor[ 2 ] = color[ 2 ];
			push.stageColor[ 3 ] = color[ 3 ];
		}

		VK_Exec_SetPushTextureMatrix( pStage, regs, push );
		int bumpyUniformOffset = -1;
		if ( reflectStage ) {
			idVec3 localViewOrigin;
			R_GlobalPointToLocal( drawSurf->space->modelMatrix, viewDef->renderView.vieworg, localViewOrigin );
			push.texMatrixS[ 0 ] = localViewOrigin[ 0 ];
			push.texMatrixS[ 1 ] = localViewOrigin[ 1 ];
			push.texMatrixS[ 2 ] = localViewOrigin[ 2 ];
			push.texMatrixS[ 3 ] = 0.0f;
			push.params[ 3 ] = 0.0f;
			if ( bumpyReflectStage ) {
				const float *modelMatrix = drawSurf->space->modelMatrix;
				vkBumpyEnvironmentBlock_t block;
				memset( &block, 0, sizeof( block ) );
				block.localViewOrigin[ 0 ] = localViewOrigin[ 0 ];
				block.localViewOrigin[ 1 ] = localViewOrigin[ 1 ];
				block.localViewOrigin[ 2 ] = localViewOrigin[ 2 ];
				block.localViewOrigin[ 3 ] = 1.0f;
				block.modelRow0[ 0 ] = modelMatrix[ 0 ];
				block.modelRow0[ 1 ] = modelMatrix[ 4 ];
				block.modelRow0[ 2 ] = modelMatrix[ 8 ];
				block.modelRow0[ 3 ] = modelMatrix[ 12 ];
				block.modelRow1[ 0 ] = modelMatrix[ 1 ];
				block.modelRow1[ 1 ] = modelMatrix[ 5 ];
				block.modelRow1[ 2 ] = modelMatrix[ 9 ];
				block.modelRow1[ 3 ] = modelMatrix[ 13 ];
				block.modelRow2[ 0 ] = modelMatrix[ 2 ];
				block.modelRow2[ 1 ] = modelMatrix[ 6 ];
				block.modelRow2[ 2 ] = modelMatrix[ 10 ];
				block.modelRow2[ 3 ] = modelMatrix[ 14 ];
				bumpyUniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
				if ( bumpyUniformOffset < 0 ) {
					continue;
				}
			}
		}

		switch ( pStage->vertexColor ) {
			case SVC_MODULATE:			push.params[ 0 ] = 1.0f; break;
			case SVC_INVERSE_MODULATE:	push.params[ 0 ] = 2.0f; break;
			default:					push.params[ 0 ] = 0.0f; break;
		}

		// Material parsing keeps alpha-test state explicitly; GLS_ATEST_BITS
		// are retained only for legacy fixed-function call sites.
		push.params[ 1 ] = VK_Exec_AlphaTestModeValue( pStage );
		push.params[ 2 ] = pStage->hasAlphaTest && regs != NULL
				? regs[ pStage->alphaTestRegister ] : 0.0f;

		if ( worldDepthState ) {
			// stage depth semantics from the material parse: opaque and
			// perforated stages test EQUAL against the depth fill,
			// translucents test LEQUAL; GLS_DEPTHMASK set = writes OFF
			const int bits = pStage->drawStateBits;
			VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			if ( bits & GLS_DEPTHFUNC_EQUAL ) {
				compareOp = VK_COMPARE_OP_EQUAL;
			} else if ( bits & GLS_DEPTHFUNC_ALWAYS ) {
				compareOp = VK_COMPARE_OP_ALWAYS;
			}
			vkCmdSetDepthCompareOp( cmd, compareOp );
			vkCmdSetDepthWriteEnable( cmd, ( bits & GLS_DEPTHMASK ) ? VK_FALSE : VK_TRUE );
		}

		// per-stage polygon offset (RB_PrepareStageTexturing contract)
		const bool stagePolygonOffset = worldDepthState && pStage->privatePolygonOffset != 0.0f;
		if ( stagePolygonOffset ) {
			vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
			vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * pStage->privatePolygonOffset, 0.0f, r_offsetFactor.GetFloat() );
		}

		VkPipeline pipeline;
		if ( cubeStage ) {
			pipeline = VK_GuiExecutor_GetCubePipeline( pStage->drawStateBits, texgen == TG_DIFFUSE_CUBE );
		} else if ( reflectStage ) {
			pipeline = VK_GuiExecutor_GetEnvironmentPipeline( pStage->drawStateBits, bumpyReflectStage );
		} else if ( glassStage ) {
			pipeline = VK_Exec_GetGlassWarpPipeline( pStage->drawStateBits );
		} else if ( screenStage ) {
			pipeline = VK_GuiExecutor_GetScreenPipeline( pStage->drawStateBits, bakedDecalStageColor );
		} else {
			pipeline = VK_GuiExecutor_GetPipeline( pStage->drawStateBits, bakedDecalStageColor );
		}
		if ( pipeline == VK_NULL_HANDLE ) {
			continue;
		}

		// one-shot bring-up evidence that a cube texgen actually drew
		if ( cubeStage ) {
			static bool loggedFirstCubeStage = false;
			if ( !loggedFirstCubeStage ) {
				loggedFirstCubeStage = true;
				common->Printf( "Vulkan: first cube-texgen stage drew (texgen %d, %s)\n", texgen, shader->GetName() );
			}
		}
		if ( reflectStage ) {
			static bool loggedFirstReflectStage[ 2 ] = { false, false };
			const int variant = bumpyReflectStage ? 1 : 0;
			if ( !loggedFirstReflectStage[ variant ] ) {
				loggedFirstReflectStage[ variant ] = true;
				common->Printf( "Vulkan: first %sreflect-cube environment stage drew (%s)\n",
						bumpyReflectStage ? "bumpy " : "", shader->GetName() );
			}
		}
		if ( screenStage ) {
			static bool loggedFirstScreenStage = false;
			if ( !loggedFirstScreenStage ) {
				loggedFirstScreenStage = true;
				common->Printf( "Vulkan: first projective screen-texgen stage drew (%s)\n",
						shader->GetName() );
			}
		}
		if ( glassStage ) {
			static bool loggedFirstGlassWarpStage = false;
			if ( !loggedFirstGlassWarpStage ) {
				loggedFirstGlassWarpStage = true;
				common->Printf( "Vulkan: first glass-warp stage drew (%s)\n",
						shader->GetName() );
			}
		}
		const VkPipelineLayout stageLayout = bumpyReflectStage || glassStage
				? vkExec.interactionPipelineLayout : vkExec.pipelineLayout;
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				stageLayout, 0, 1, &descriptor, 0, NULL );
		if ( bumpyReflectStage ) {
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					stageLayout, 1, 1, &bumpDescriptor, 0, NULL );
			const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
			const uint32_t dynamicOffset = (uint32_t)bumpyUniformOffset;
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					stageLayout, 6, 1, &uniformSet, 1, &dynamicOffset );
		} else if ( glassStage ) {
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					stageLayout, 1, 2, glassDescriptors, 0, NULL );
		}
		vkCmdPushConstants( cmd, stageLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
		VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
		vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );

		if ( stagePolygonOffset ) {
			if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
				vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
			} else {
				vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
			}
		}
	}
}

/*
===============================================================================

	Backend-neutral classic GUI whole-view consumer

	The preparation contract in ClassicGuiDomain owns material interpretation.
	This adapter is deliberately limited to geometry upload from legacyDrawSurf;
	all material state, register results, texture identity, transforms, and
	scissors come from immutable domain records.  Every fallible resource lookup
	and upload completes before the first indexed draw, so a rejection can still
	return to the untouched classic walker for the complete view.

===============================================================================
*/

enum vkClassicGuiRejectDetail_t {
	VK_CLASSIC_GUI_REJECT_NONE = 0,
	VK_CLASSIC_GUI_REJECT_VIEW_MUTATION,
	VK_CLASSIC_GUI_REJECT_OFFSCREEN_TARGET,
	VK_CLASSIC_GUI_REJECT_RENDER_SCOPE,
	VK_CLASSIC_GUI_REJECT_VIEWPORT,
	VK_CLASSIC_GUI_REJECT_DOMAIN_COUNTS,
	VK_CLASSIC_GUI_REJECT_DRAW_RECORD,
	VK_CLASSIC_GUI_REJECT_GEOMETRY,
	VK_CLASSIC_GUI_REJECT_PASS_RECORD,
	VK_CLASSIC_GUI_REJECT_PASS_STATE,
	VK_CLASSIC_GUI_REJECT_TEXTURE_BINDING,
	VK_CLASSIC_GUI_REJECT_TEXTURE_RESIDENCY,
	VK_CLASSIC_GUI_REJECT_DESCRIPTOR,
	VK_CLASSIC_GUI_REJECT_PIPELINE,
	VK_CLASSIC_GUI_REJECT_COVERAGE
};

typedef struct vkClassicGuiDrawPlan_s {
	const classicGuiDomainDraw_t *draw;
	const srfTriangles_t *tri;
	int vertexOffset;
	int indexOffset;
	int firstPass;
	int passCount;
	VkRect2D scissor;
	float mvp[ 16 ];
} vkClassicGuiDrawPlan_t;

typedef struct vkClassicGuiPassPlan_s {
	const rendererEvaluatedMaterialPass_t *pass;
	VkPipeline pipeline;
	VkDescriptorSet descriptor;
	int stateBits;
	float alphaTestMode;
	VkCompareOp depthCompare;
	VkCullModeFlags cullMode;
} vkClassicGuiPassPlan_t;

static vkClassicGuiDrawPlan_t vkClassicGuiDrawPlans[ CLASSIC_GUI_DOMAIN_MAX_DRAWS ];
static vkClassicGuiPassPlan_t vkClassicGuiPassPlans[ CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES ];

static bool VK_ClassicGui_Fail( const viewDef_t *viewDef,
		classicGuiDomainFailure_t failure, int detail ) {
	R_ClassicGuiDomain_RecordBackendFallback( viewDef,
		CLASSIC_GUI_DOMAIN_BACKEND_VULKAN, failure, detail );
	return false;
}

static bool VK_ClassicGui_SourceNoopValid(
		const classicGuiDomainDraw_t &draw ) {
	switch ( draw.sourceSurface ) {
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NULL_SURFACE:
		if ( draw.legacyDrawSurf != NULL ) {
			return false;
		}
		break;
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_MISSING_MATERIAL:
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NO_AMBIENT:
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_PORTAL_SKY:
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY:
		if ( draw.legacyDrawSurf == NULL ) {
			return false;
		}
		break;
	default:
		return false;
	}
	return !draw.packetBacked && draw.drawPacketIndex == -1
		&& draw.firstEvaluatedPass == -1 && draw.evaluatedPassCount == 0
		&& draw.activePassCount == 0 && draw.drawablePassCount == 0
		&& draw.inactivePassCount == 0 && draw.activeNoopPassCount == 0
		&& draw.noopPassCount == 0;
}

static bool VK_ClassicGui_MapSourceBlend( rendererBlendFactor_t factor, int &bits ) {
	switch ( factor ) {
	case RENDERER_BLEND_ZERO: bits = GLS_SRCBLEND_ZERO; return true;
	case RENDERER_BLEND_ONE: bits = GLS_SRCBLEND_ONE; return true;
	case RENDERER_BLEND_SRC_COLOR: bits = GLS_SRCBLEND_SRC_COLOR; return true;
	case RENDERER_BLEND_ONE_MINUS_SRC_COLOR: bits = GLS_SRCBLEND_ONE_MINUS_SRC_COLOR; return true;
	case RENDERER_BLEND_DST_COLOR: bits = GLS_SRCBLEND_DST_COLOR; return true;
	case RENDERER_BLEND_ONE_MINUS_DST_COLOR: bits = GLS_SRCBLEND_ONE_MINUS_DST_COLOR; return true;
	case RENDERER_BLEND_SRC_ALPHA: bits = GLS_SRCBLEND_SRC_ALPHA; return true;
	case RENDERER_BLEND_ONE_MINUS_SRC_ALPHA: bits = GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA; return true;
	case RENDERER_BLEND_DST_ALPHA: bits = GLS_SRCBLEND_DST_ALPHA; return true;
	case RENDERER_BLEND_ONE_MINUS_DST_ALPHA: bits = GLS_SRCBLEND_ONE_MINUS_DST_ALPHA; return true;
	case RENDERER_BLEND_SRC_ALPHA_SATURATE: bits = GLS_SRCBLEND_ALPHA_SATURATE; return true;
	default: return false;
	}
}

static bool VK_ClassicGui_MapDestinationBlend( rendererBlendFactor_t factor, int &bits ) {
	switch ( factor ) {
	case RENDERER_BLEND_ZERO: bits = GLS_DSTBLEND_ZERO; return true;
	case RENDERER_BLEND_ONE: bits = GLS_DSTBLEND_ONE; return true;
	case RENDERER_BLEND_SRC_COLOR: bits = GLS_DSTBLEND_SRC_COLOR; return true;
	case RENDERER_BLEND_ONE_MINUS_SRC_COLOR: bits = GLS_DSTBLEND_ONE_MINUS_SRC_COLOR; return true;
	case RENDERER_BLEND_SRC_ALPHA: bits = GLS_DSTBLEND_SRC_ALPHA; return true;
	case RENDERER_BLEND_ONE_MINUS_SRC_ALPHA: bits = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA; return true;
	case RENDERER_BLEND_DST_ALPHA: bits = GLS_DSTBLEND_DST_ALPHA; return true;
	case RENDERER_BLEND_ONE_MINUS_DST_ALPHA: bits = GLS_DSTBLEND_ONE_MINUS_DST_ALPHA; return true;
	default: return false;
	}
}

static bool VK_ClassicGui_MapDepthCompare( rendererCompareOp_t operation,
		VkCompareOp &compare ) {
	switch ( operation ) {
	case RENDERER_COMPARE_NEVER: compare = VK_COMPARE_OP_NEVER; return true;
	case RENDERER_COMPARE_LESS: compare = VK_COMPARE_OP_LESS; return true;
	case RENDERER_COMPARE_EQUAL: compare = VK_COMPARE_OP_EQUAL; return true;
	case RENDERER_COMPARE_LESS_OR_EQUAL: compare = VK_COMPARE_OP_LESS_OR_EQUAL; return true;
	case RENDERER_COMPARE_GREATER: compare = VK_COMPARE_OP_GREATER; return true;
	case RENDERER_COMPARE_NOT_EQUAL: compare = VK_COMPARE_OP_NOT_EQUAL; return true;
	case RENDERER_COMPARE_GREATER_OR_EQUAL: compare = VK_COMPARE_OP_GREATER_OR_EQUAL; return true;
	case RENDERER_COMPARE_ALWAYS: compare = VK_COMPARE_OP_ALWAYS; return true;
	default: return false;
	}
}

static bool VK_ClassicGui_MapAlphaTest( const rendererEvaluatedMaterialPass_t &pass,
		float &mode ) {
	if ( !pass.alphaTestEnabled
			|| pass.alphaTestCompareOperation == RENDERER_COMPARE_ALWAYS ) {
		mode = 0.0f;
		return true;
	}
	switch ( pass.alphaTestCompareOperation ) {
	case RENDERER_COMPARE_LESS: mode = -1.0f; return true;
	case RENDERER_COMPARE_EQUAL: mode = 2.0f; return true;
	case RENDERER_COMPARE_GREATER: mode = 1.0f; return true;
	default:
		// gui.frag intentionally exposes only these three exact classic tests.
		// Other neutral compare operations keep whole-view ownership classic.
		return false;
	}
}

static bool VK_ClassicGui_MapCull( rendererCullMode_t cull,
		VkCullModeFlags &mode ) {
	switch ( cull ) {
	case RENDERER_CULL_NONE: mode = VK_CULL_MODE_NONE; return true;
	case RENDERER_CULL_FRONT: mode = VK_CULL_MODE_FRONT_BIT; return true;
	case RENDERER_CULL_BACK: mode = VK_CULL_MODE_BACK_BIT; return true;
	default: return false;
	}
}

static bool VK_ClassicGui_MapState( const rendererEvaluatedMaterialPass_t &pass,
		bool inWorld, vkClassicGuiPassPlan_t &plan ) {
	if ( pass.kind != ( inWorld ? RENDERER_MATERIAL_PASS_SURFACE
			: RENDERER_MATERIAL_PASS_GUI )
			|| ( inWorld
				? pass.programFamily != RENDERER_PROGRAM_FIXED
				: ( pass.programFamily != RENDERER_PROGRAM_GUI
					&& pass.programFamily != RENDERER_PROGRAM_FIXED ) )
			|| pass.programKey != 0
			|| pass.texgen != RENDERER_TEXGEN_EXPLICIT
			|| pass.textureSemantic != RENDERER_TEXTURE_DIFFUSE
			|| pass.textureResourceId == 0
			|| ( pass.colorWriteMask
				& ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_RGBA ) ) != 0
			|| pass.vertexColor < RENDERER_VERTEX_COLOR_IGNORE
			|| pass.vertexColor > RENDERER_VERTEX_COLOR_INVERSE_MODULATE
			|| pass.blend.colorOperation != RENDERER_BLEND_OP_ADD
			|| pass.blend.alphaOperation != RENDERER_BLEND_OP_ADD
			|| pass.blend.sourceAlpha != pass.blend.sourceColor
			|| pass.blend.destinationAlpha != pass.blend.destinationColor
			|| pass.depth.testEnabled != inWorld
			|| ( pass.depth.compareOperation != RENDERER_COMPARE_LESS_OR_EQUAL
				&& pass.depth.compareOperation != RENDERER_COMPARE_EQUAL
				&& pass.depth.compareOperation != RENDERER_COMPARE_ALWAYS ) ) {
		return false;
	}
	const bool replacementBlend = pass.blend.sourceColor == RENDERER_BLEND_ONE
		&& pass.blend.destinationColor == RENDERER_BLEND_ZERO;
	if ( pass.blend.enabled == replacementBlend ) {
		return false;
	}

	int sourceBits = 0;
	int destinationBits = 0;
	if ( !VK_ClassicGui_MapSourceBlend( pass.blend.sourceColor, sourceBits )
			|| !VK_ClassicGui_MapDestinationBlend(
				pass.blend.destinationColor, destinationBits )
			|| !VK_ClassicGui_MapDepthCompare(
				pass.depth.compareOperation, plan.depthCompare )
			|| !VK_ClassicGui_MapAlphaTest( pass, plan.alphaTestMode )
			|| !VK_ClassicGui_MapCull( pass.cull, plan.cullMode ) ) {
		return false;
	}

	plan.stateBits = sourceBits | destinationBits;
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_RED ) == 0 ) {
		plan.stateBits |= GLS_REDMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_GREEN ) == 0 ) {
		plan.stateBits |= GLS_GREENMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_BLUE ) == 0 ) {
		plan.stateBits |= GLS_BLUEMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_ALPHA ) == 0 ) {
		plan.stateBits |= GLS_ALPHAMASK;
	}

	if ( !std::isfinite( pass.condition ) || !std::isfinite( pass.alphaTest )
			|| !std::isfinite( pass.polygonOffsetFactor )
			|| !std::isfinite( pass.polygonOffsetUnits ) ) {
		return false;
	}
	for ( int i = 0; i < 4; ++i ) {
		if ( !std::isfinite( pass.color[ i ] ) ) {
			return false;
		}
	}
	for ( int i = 0; i < 6; ++i ) {
		if ( !std::isfinite( pass.textureMatrix[ i ] ) ) {
			return false;
		}
	}
	if ( static_cast<double>( pass.textureMatrix[ 2 ] )
			< static_cast<double>( INT_MIN )
			|| static_cast<double>( pass.textureMatrix[ 2 ] )
				> static_cast<double>( INT_MAX )
			|| static_cast<double>( pass.textureMatrix[ 5 ] )
				< static_cast<double>( INT_MIN )
			|| static_cast<double>( pass.textureMatrix[ 5 ] )
				> static_cast<double>( INT_MAX ) ) {
		return false;
	}
	return true;
}

static bool VK_ClassicGui_BuildScissor( const classicGuiDomainView_t &view,
		const classicGuiDomainDraw_t &draw, int framebufferHeight,
		VkRect2D &scissor ) {
	const int viewportWidth = view.viewportX2 - view.viewportX1 + 1;
	const int viewportHeight = view.viewportY2 - view.viewportY1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 || framebufferHeight <= 0 ) {
		return false;
	}

	const bool drawScissorEmpty = draw.scissorX2 < draw.scissorX1
			|| draw.scissorY2 < draw.scissorY1;
	const bool useDrawScissor = r_useScissor.GetBool() && !drawScissorEmpty;
	int requestedX1 = useDrawScissor ? draw.scissorX1 : view.scissorX1;
	int requestedY1 = useDrawScissor ? draw.scissorY1 : view.scissorY1;
	int requestedX2 = useDrawScissor ? draw.scissorX2 : view.scissorX2;
	int requestedY2 = useDrawScissor ? draw.scissorY2 : view.scissorY2;
	const bool requestedEmpty = requestedX2 < requestedX1
			|| requestedY2 < requestedY1;

	int x0 = view.viewportX1;
	int x1 = view.viewportX1 + viewportWidth;
	int y0GL = view.viewportY1;
	int y1GL = view.viewportY1 + viewportHeight;
	if ( !requestedEmpty ) {
		x0 = Max( x0, view.viewportX1 + requestedX1 );
		x1 = Min( x1, view.viewportX1 + requestedX2 + 1 );
		y0GL = Max( y0GL, view.viewportY1 + requestedY1 );
		y1GL = Min( y1GL, view.viewportY1 + requestedY2 + 1 );
	}
	x0 = Min( view.viewportX1 + viewportWidth, Max( 0, x0 ) );
	x1 = Max( x0, x1 );
	y0GL = Min( view.viewportY1 + viewportHeight,
		Max( view.viewportY1, y0GL ) );
	y1GL = Max( y0GL, y1GL );
	const int y0 = Max( 0, framebufferHeight - y1GL );
	const int y1 = Max( y0, Min( framebufferHeight,
		framebufferHeight - y0GL ) );

	scissor.offset.x = x0;
	scissor.offset.y = y0;
	scissor.extent.width = static_cast<std::uint32_t>( x1 - x0 );
	scissor.extent.height = static_cast<std::uint32_t>( y1 - y0 );
	return true;
}

static bool VK_Exec_PrepareTriGeometryOffsets( int slot,
		const srfTriangles_t *tri, int &vertexOffset, int &indexOffset ) {
	return VK_Exec_UploadTriGeometry( slot, tri,
		vertexOffset, indexOffset );
}

static bool VK_Exec_SharedGeometryCheckpoint( void ) {
	vkSharedGeometryCheckpoint_t &checkpoint = vkSharedGeometryCheckpoint;
	if ( checkpoint.active || !vkExec.frameOpen
			|| vkExec.frameSlot < 0
			|| vkExec.frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return false;
	}
	const vkRing_t &vertexRing = vkExec.vertexRings[ vkExec.frameSlot ];
	const vkRing_t &indexRing = vkExec.indexRings[ vkExec.frameSlot ];
	if ( vertexRing.cursor < 0 || vertexRing.cursor > vertexRing.capacity
			|| indexRing.cursor < 0 || indexRing.cursor > indexRing.capacity ) {
		return false;
	}

	checkpoint.frameSlot = vkExec.frameSlot;
	checkpoint.vertexCursor = vertexRing.cursor;
	checkpoint.indexCursor = indexRing.cursor;
	checkpoint.boundVertexOffset = vkExec.boundVertexOffset;
	memcpy( checkpoint.vertMemo, vkExec.vertMemo,
		sizeof( checkpoint.vertMemo ) );
	memcpy( checkpoint.idxMemo, vkExec.idxMemo,
		sizeof( checkpoint.idxMemo ) );
	memcpy( checkpoint.gpuSkinningMemo, vkExec.gpuSkinningMemo,
		sizeof( checkpoint.gpuSkinningMemo ) );
	checkpoint.active = true;
	return true;
}

static void VK_Exec_SharedGeometryRestore( void ) {
	vkSharedGeometryCheckpoint_t &checkpoint = vkSharedGeometryCheckpoint;
	if ( !checkpoint.active ) {
		return;
	}
	if ( checkpoint.frameSlot >= 0
			&& checkpoint.frameSlot < VK_FRAMES_IN_FLIGHT ) {
		vkExec.vertexRings[ checkpoint.frameSlot ].cursor =
			checkpoint.vertexCursor;
		vkExec.indexRings[ checkpoint.frameSlot ].cursor =
			checkpoint.indexCursor;
		vkExec.boundVertexOffset = checkpoint.boundVertexOffset;
		memcpy( vkExec.vertMemo, checkpoint.vertMemo,
			sizeof( checkpoint.vertMemo ) );
		memcpy( vkExec.idxMemo, checkpoint.idxMemo,
			sizeof( checkpoint.idxMemo ) );
		memcpy( vkExec.gpuSkinningMemo, checkpoint.gpuSkinningMemo,
			sizeof( checkpoint.gpuSkinningMemo ) );
	}
	checkpoint.active = false;
}

static void VK_Exec_SharedGeometryCommit( void ) {
	// The complete shared view is now ready.  Its retained offsets and memo
	// entries become ordinary frame allocations consumed by the committed draw.
	vkSharedGeometryCheckpoint.active = false;
}

// Preserve the interaction/fog ABI while all shared classic domains use the
// same single-owner geometry transaction internally.
bool VK_Exec_SharedInteractionGeometryCheckpoint( void ) {
	return VK_Exec_SharedGeometryCheckpoint();
}

void VK_Exec_SharedInteractionGeometryRestore( void ) {
	VK_Exec_SharedGeometryRestore();
}

void VK_Exec_SharedInteractionGeometryCommit( void ) {
	VK_Exec_SharedGeometryCommit();
}

// Shared classic domains preflight every geometry upload before they take
// framebuffer ownership.  Keep the ring/memo implementation private while
// exposing the exact retained offsets to the Vulkan interaction consumer.
bool VK_Exec_PrepareTriGeometry( VkCommandBuffer cmd, int slot,
		const srfTriangles_t *tri, int &vertexOffset, int &indexOffset ) {
	(void)cmd;
	return VK_Exec_PrepareTriGeometryOffsets( slot, tri,
		vertexOffset, indexOffset );
}

void VK_Exec_BindPreparedTriGeometry( VkCommandBuffer cmd, int slot,
		int vertexOffset, int indexOffset ) {
	if ( cmd == VK_NULL_HANDLE || slot < 0 || slot >= VK_FRAMES_IN_FLIGHT
			|| vertexOffset < 0 || indexOffset < 0 ) {
		return;
	}
	const VkBuffer vertexBuffer = vkExec.vertexRings[ slot ].buffer;
	const VkDeviceSize bindOffset = static_cast<VkDeviceSize>( vertexOffset );
	vkCmdBindVertexBuffers( cmd, 0, 1, &vertexBuffer, &bindOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer,
		static_cast<VkDeviceSize>( indexOffset ), VK_INDEX_TYPE_UINT32 );
}

static void VK_ClassicGui_BindPreparedGeometry( VkCommandBuffer cmd, int slot,
		const vkClassicGuiDrawPlan_t &plan ) {
	const VkBuffer vertexBuffer = vkExec.vertexRings[ slot ].buffer;
	const VkDeviceSize vertexOffset = static_cast<VkDeviceSize>( plan.vertexOffset );
	vkCmdBindVertexBuffers( cmd, 0, 1, &vertexBuffer, &vertexOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer,
		static_cast<VkDeviceSize>( plan.indexOffset ), VK_INDEX_TYPE_UINT32 );
}

static void VK_ClassicGui_SetPushTextureMatrix(
		const rendererEvaluatedMaterialPass_t &pass,
		vkGuiPushConstants_t &push ) {
	float offsetS = pass.textureMatrix[ 2 ];
	float offsetT = pass.textureMatrix[ 5 ];
	// Match RB_GetShaderTextureMatrix exactly: only large translations wrap,
	// retaining center-rotation/scale offsets in the ordinary range.
	if ( offsetS < -40.0f || offsetS > 40.0f ) {
		offsetS -= static_cast<int>( offsetS );
	}
	if ( offsetT < -40.0f || offsetT > 40.0f ) {
		offsetT -= static_cast<int>( offsetT );
	}
	push.texMatrixS[ 0 ] = pass.textureMatrix[ 0 ];
	push.texMatrixS[ 1 ] = pass.textureMatrix[ 1 ];
	push.texMatrixS[ 2 ] = 0.0f;
	push.texMatrixS[ 3 ] = offsetS;
	push.texMatrixT[ 0 ] = pass.textureMatrix[ 3 ];
	push.texMatrixT[ 1 ] = pass.textureMatrix[ 4 ];
	push.texMatrixT[ 2 ] = 0.0f;
	push.texMatrixT[ 3 ] = offsetT;
	push.params[ 3 ] =
		pass.textureMatrix[ 0 ] != 1.0f || pass.textureMatrix[ 1 ] != 0.0f
		|| offsetS != 0.0f || pass.textureMatrix[ 3 ] != 0.0f
		|| pass.textureMatrix[ 4 ] != 1.0f || offsetT != 0.0f
			? 1.0f : 0.0f;
}

static bool VK_ClassicGui_DrawOwnedViewForScope( const viewDef_t *viewDef,
		classicGuiDomainScope_t scope ) {
	const bool inWorld = scope == CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD;
	const classicGuiDomainView_t *view = inWorld
		? R_ClassicGuiDomain_FindInWorldView( viewDef )
		: R_ClassicGuiDomain_FindRootView( viewDef );
	if ( view == NULL ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY, -1 );
	}
	if ( view->scope != scope || !view->ready ) {
		return VK_ClassicGui_Fail( viewDef,
			view->failure != CLASSIC_GUI_DOMAIN_FAILURE_NONE
				? view->failure : CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
			view->failureDetail );
	}
	// The same immutable view pointer may legitimately appear in more than one
	// RC_DRAW_VIEW command.  An earlier owned outcome must not suppress that
	// later command's draw.  A recorded fallback remains classic for every
	// occurrence; duplicate ownership reporting after commit is diagnostic.
	if ( view->backendOutcome[ CLASSIC_GUI_DOMAIN_BACKEND_VULKAN ]
			== CLASSIC_GUI_DOMAIN_BACKEND_FALLBACK ) {
		return false;
	}

	if ( view->viewDef != viewDef
			|| ( !inWorld && view->sourceSurfaceCount != viewDef->numDrawSurfs )
			|| ( inWorld && view->sourceSurfaceCount > viewDef->numDrawSurfs )
			|| view->sourceSurfaceCount < 0
			|| view->sourceSurfaceCount > SCENE_PACKET_MAX_DRAWS
			|| view->drawableSurfaceCount < 0
			|| view->drawableSurfaceCount > view->sourceSurfaceCount
			|| view->noopSurfaceCount
				!= view->sourceSurfaceCount - view->drawableSurfaceCount
			|| ( viewDef->numDrawSurfs > 0 && viewDef->drawSurfs == NULL ) ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_GUI_REJECT_DOMAIN_COUNTS );
	}
	if ( ( !inWorld && ( viewDef->viewEntitys != NULL
				|| viewDef->viewLights != NULL || viewDef->renderWorld != NULL ) )
			|| ( inWorld && ( viewDef->viewEntitys == NULL
				|| viewDef->renderWorld == NULL ) )
			|| viewDef->isSubview
			|| viewDef->isMirror || viewDef->isXraySubview || viewDef->isEditor
			|| viewDef->superView != NULL || viewDef->subviewSurface != NULL
			|| viewDef->numClipPlanes != 0
			|| ( !inWorld && viewDef->renderFlags != 0 )
			|| viewDef->numOutlineDrawSurfs != 0
			|| viewDef->renderView.globalMaterial != NULL
			|| r_showOverDraw.GetInteger() != 0
			|| r_singleTriangle.GetBool() ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_GUI_REJECT_VIEW_MUTATION );
	}
	if ( vkExec.activeRenderTexture != NULL || vkExec.activeColorEntry != NULL
			|| vkExec.activeDepthEntry != NULL || backEnd.renderTexture != NULL
			|| backEnd.feedbackRenderTexture != NULL ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_GUI_REJECT_OFFSCREEN_TARGET );
	}
	if ( !vkExec.frameOpen || !vkExec.mainScopeOpen
			|| vkExec.cmd == VK_NULL_HANDLE
			|| vkExec.pendingSpecialEffectsView != NULL
			|| vkExec.pendingSpecialEffectsMask != 0
			|| vkExec.pendingSpecialEffectsSource != NULL
			|| vkExec.pendingSpecialEffectsNeedsResolve
			|| !VK_Exec_PipelineTargetsMatch( vkExec.activePipelineTarget,
				VK_Exec_SwapchainPipelineTarget() ) ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_GUI_REJECT_RENDER_SCOPE );
	}

	const int framebufferWidth = VK_Exec_ActiveFramebufferWidth();
	const int framebufferHeight = VK_Exec_ActiveFramebufferHeight();
	const int viewportWidth = view->viewportX2 - view->viewportX1 + 1;
	const int viewportHeight = view->viewportY2 - view->viewportY1 + 1;
	if ( view->viewportX1 != viewDef->viewport.x1
			|| view->viewportY1 != viewDef->viewport.y1
			|| view->viewportX2 != viewDef->viewport.x2
			|| view->viewportY2 != viewDef->viewport.y2
			|| view->scissorX1 != viewDef->scissor.x1
			|| view->scissorY1 != viewDef->scissor.y1
			|| view->scissorX2 != viewDef->scissor.x2
			|| view->scissorY2 != viewDef->scissor.y2
			|| memcmp( view->projectionMatrix, viewDef->projectionMatrix,
				sizeof( view->projectionMatrix ) ) != 0
			|| viewportWidth <= 0 || viewportHeight <= 0
			|| view->viewportX1 < 0 || view->viewportY1 < 0
			|| view->viewportX2 >= framebufferWidth
			|| view->viewportY2 >= framebufferHeight
			|| view->drawCount < 0
			|| view->drawCount > CLASSIC_GUI_DOMAIN_MAX_DRAWS
			|| view->evaluatedPassCount < 0
			|| view->evaluatedPassCount
				> CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_GUI_REJECT_VIEWPORT );
	}
	VkViewport viewport;
	if ( !VK_BuildCanonicalViewport( viewport,
			static_cast<float>( view->viewportX1 ),
			static_cast<float>( view->viewportY1 ),
			static_cast<float>( viewportWidth ),
			static_cast<float>( viewportHeight ),
			static_cast<float>( framebufferHeight ) ) ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_GUI_REJECT_VIEWPORT );
	}
	for ( int matrixIndex = 0; matrixIndex < 16; ++matrixIndex ) {
		if ( !std::isfinite( view->projectionMatrix[ matrixIndex ] ) ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_VIEW_MUTATION );
		}
	}

	int planDrawCount = 0;
	int planPassCount = 0;
	int drawablePasses = 0;
	int activePasses = 0;
	int noopPasses = 0;
	int inactivePasses = 0;
	int activeNoopPasses = 0;
	int sourceNoopSurfaces = 0;
	int previousSourceSurface = -1;
	for ( int drawIndex = 0; drawIndex < view->drawCount; ++drawIndex ) {
		const classicGuiDomainDraw_t *draw =
			R_ClassicGuiDomain_ViewDraw( *view, drawIndex );
		if ( draw == NULL || draw->sourceSurfaceIndex <= previousSourceSurface
				|| draw->sourceSurfaceIndex < 0
				|| draw->sourceSurfaceIndex >= viewDef->numDrawSurfs
				|| viewDef->drawSurfs[ draw->sourceSurfaceIndex ]
					!= draw->legacyDrawSurf
				) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_DRAW_RECORD );
		}
		if ( inWorld && ( draw->legacyDrawSurf == NULL
				|| ( draw->legacyDrawSurf->dsFlags & DSF_IN_WORLD_GUI ) == 0 ) ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_DRAW_RECORD );
		}
		previousSourceSurface = draw->sourceSurfaceIndex;
		if ( draw->sourceSurface != CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_DRAWABLE ) {
			if ( !VK_ClassicGui_SourceNoopValid( *draw ) ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_DRAW_RECORD );
			}
			sourceNoopSurfaces++;
			continue;
		}
		if ( draw->legacyDrawSurf == NULL || !draw->packetBacked
				|| draw->firstIndex != 0 || draw->vertexOffset != 0
				|| draw->evaluatedPassCount < 0
				|| planPassCount > CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES
					- draw->evaluatedPassCount ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_DRAW_RECORD );
		}

		vkClassicGuiDrawPlan_t &drawPlan = vkClassicGuiDrawPlans[ planDrawCount++ ];
		memset( &drawPlan, 0, sizeof( drawPlan ) );
		drawPlan.draw = draw;
		drawPlan.tri = draw->legacyDrawSurf->geo;
		drawPlan.vertexOffset = -1;
		drawPlan.indexOffset = -1;
		drawPlan.firstPass = planPassCount;
		drawPlan.passCount = draw->evaluatedPassCount;
		const srfTriangles_t *tri = drawPlan.tri;
		if ( tri == NULL || tri->ambientCache == NULL
				|| tri->numVerts <= 0 || tri->numIndexes <= 0
				|| tri->numIndexes % 3 != 0
				|| tri->numVerts != draw->vertexCount
				|| tri->numIndexes != draw->indexCount
				|| draw->vertexCount > INT_MAX
					/ static_cast<int>( sizeof( idDrawVert ) )
				|| draw->indexCount > INT_MAX
					/ static_cast<int>( sizeof( glIndex_t ) )
				|| !draw->hasAmbientCache
				|| draw->hasIndexCache != ( tri->indexCache != NULL )
				|| ( tri->indexes == NULL && tri->indexCache == NULL )
				|| draw->legacyDrawSurf->decalColorCache != NULL ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_GEOMETRY );
		}
		const long long scissorWidth = static_cast<long long>( draw->scissorX2 )
			- draw->scissorX1 + 1;
		const long long scissorHeight = static_cast<long long>( draw->scissorY2 )
			- draw->scissorY1 + 1;
		if ( scissorWidth <= 0 || scissorHeight <= 0
				|| scissorWidth > INT_MAX || scissorHeight > INT_MAX ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_GEOMETRY );
		}
		if ( !VK_ClassicGui_BuildScissor( *view, *draw,
				framebufferHeight, drawPlan.scissor ) ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_VIEWPORT );
		}
		float mvpGL[ 16 ];
		myGlMultMatrix( draw->modelViewMatrix, view->projectionMatrix, mvpGL );
		VK_FixupClipSpaceZ( drawPlan.mvp, mvpGL );
		for ( int matrixIndex = 0; matrixIndex < 16; ++matrixIndex ) {
			if ( !std::isfinite( drawPlan.mvp[ matrixIndex ] ) ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_DRAW_RECORD );
			}
		}

		int drawDrawablePasses = 0;
		int drawActivePasses = 0;
		int drawInactivePasses = 0;
		int drawActiveNoopPasses = 0;
		for ( int passIndex = 0; passIndex < draw->evaluatedPassCount;
				++passIndex ) {
			const rendererEvaluatedMaterialPass_t *pass =
				R_ClassicGuiDomain_DrawPass( *draw, passIndex );
			const materialResourceTextureBinding_t *binding =
				R_ClassicGuiDomain_DrawPassTexture( *draw, passIndex );
			if ( pass == NULL || binding == NULL
					|| pass->order != static_cast<std::uint32_t>( passIndex )
					|| pass->sourceStageIndex < 0
					|| pass->sourceStageIndex != binding->stageIndex
					|| pass->textureResourceId != binding->textureResourceId
					|| binding->image == NULL || binding->textureResourceId == 0
					|| pass->active != ( pass->condition != 0.0f ) ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_PASS_RECORD );
			}

			vkClassicGuiPassPlan_t &passPlan =
				vkClassicGuiPassPlans[ planPassCount++ ];
			memset( &passPlan, 0, sizeof( passPlan ) );
			passPlan.pass = pass;
			if ( !VK_ClassicGui_MapState( *pass, inWorld, passPlan ) ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_PASS_STATE );
			}

			switch ( pass->disposition ) {
			case RENDERER_MATERIAL_PASS_DRAW:
				if ( !pass->active ) {
					return VK_ClassicGui_Fail( viewDef,
						CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_GUI_REJECT_PASS_RECORD );
				}
				drawDrawablePasses++;
				drawActivePasses++;
				drawablePasses++;
				activePasses++;
				break;
			case RENDERER_MATERIAL_PASS_INACTIVE_CONDITION:
				if ( pass->active ) {
					return VK_ClassicGui_Fail( viewDef,
						CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_GUI_REJECT_PASS_RECORD );
				}
				drawInactivePasses++;
				inactivePasses++;
				noopPasses++;
				break;
			case RENDERER_MATERIAL_PASS_NOOP_ZERO_ONE_BLEND:
			case RENDERER_MATERIAL_PASS_NOOP_BLACK_ADDITIVE:
			case RENDERER_MATERIAL_PASS_NOOP_TRANSPARENT_ALPHA:
				if ( !pass->active ) {
					return VK_ClassicGui_Fail( viewDef,
						CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_GUI_REJECT_PASS_RECORD );
				}
				drawActiveNoopPasses++;
				drawActivePasses++;
				activeNoopPasses++;
				activePasses++;
				noopPasses++;
				break;
			default:
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_PASS_RECORD );
			}

			// The opaque resource must resolve for every ordered pass, including
			// inactive/no-op entries.  Only framebuffer-affecting passes spend
			// descriptor and pipeline capacity.
			const materialResourceTextureBinding_t *resolved =
				R_MaterialResourceTable_ResolveTextureResource(
					pass->textureResourceId );
			if ( resolved != binding || resolved->image != binding->image
					|| resolved->textureHandle != binding->textureHandle ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_TEXTURE_BINDING );
			}
			idImage *image = const_cast<idImage *>( binding->image );
			const idImageOpts &imageOptions = image->GetOpts();
			if ( !binding->loaded || binding->defaulted || binding->missing
					|| binding->filter < TF_LINEAR
					|| binding->filter > TF_DEFAULT
					|| binding->repeat < TR_REPEAT
					|| binding->repeat > TR_CLAMP_TO_ZERO_ALPHA
					|| !image->IsLoaded() || image->IsDefaulted()
					|| image->GetDeviceHandle() != binding->textureHandle
					|| binding->textureHandle == 0
					|| binding->textureHandle
						>= static_cast<unsigned int>(
							sizeof( vkExec.descriptorCache )
							/ sizeof( vkExec.descriptorCache[ 0 ] ) )
					|| imageOptions.textureType != TT_2D
					|| imageOptions.width <= 0 || imageOptions.height <= 0 ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_TEXTURE_RESIDENCY );
			}
			if ( pass->disposition != RENDERER_MATERIAL_PASS_DRAW ) {
				continue;
			}

			if ( image->GetFilter() != binding->filter
					|| image->GetRepeat() != binding->repeat ) {
				image->SetSamplerState( binding->filter, binding->repeat );
			}
			const vkImageEntry_t *imageEntry =
				VK_Image_GetEntry( binding->textureHandle );
			if ( imageEntry == NULL || imageEntry->isCube
					|| imageEntry->view == VK_NULL_HANDLE
					|| imageEntry->sampler == VK_NULL_HANDLE
					|| imageEntry->layout
						!= VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_TEXTURE_RESIDENCY );
			}
			passPlan.descriptor = VK_GuiExecutor_GetImageDescriptor(
				binding->textureHandle );
			if ( passPlan.descriptor == VK_NULL_HANDLE ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_DESCRIPTOR );
			}
			passPlan.pipeline =
				VK_GuiExecutor_GetPipelineStrict( passPlan.stateBits );
			if ( passPlan.pipeline == VK_NULL_HANDLE ) {
				return VK_ClassicGui_Fail( viewDef,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_GUI_REJECT_PIPELINE );
			}
		}
		if ( drawActivePasses != draw->activePassCount
				|| drawDrawablePasses != draw->drawablePassCount
				|| drawInactivePasses != draw->inactivePassCount
				|| drawActiveNoopPasses != draw->activeNoopPassCount
				|| drawInactivePasses + drawActiveNoopPasses
					!= draw->noopPassCount ) {
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_COVERAGE_MISMATCH,
				VK_CLASSIC_GUI_REJECT_COVERAGE );
		}
	}

	if ( planDrawCount != view->drawableSurfaceCount
			|| sourceNoopSurfaces != view->noopSurfaceCount
			|| planPassCount != view->evaluatedPassCount
			|| activePasses != view->activePassCount
			|| drawablePasses != view->drawablePassCount
			|| inactivePasses != view->inactivePassCount
			|| activeNoopPasses != view->activeNoopPassCount
			|| noopPasses != view->noopPassCount ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_COVERAGE_MISMATCH,
			VK_CLASSIC_GUI_REJECT_DOMAIN_COUNTS );
	}

	// Geometry upload is the final preflight phase. Retain every offset without
	// recording bind state, then commit the ring/memo transaction only after the
	// complete view fits. A mid-list failure leaves the classic walker untouched.
	VkCommandBuffer cmd = vkExec.cmd;
	const int slot = vkExec.frameSlot;
	if ( !VK_Exec_SharedGeometryCheckpoint() ) {
		return VK_ClassicGui_Fail( viewDef,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_GUI_REJECT_GEOMETRY );
	}
	for ( int drawIndex = 0; drawIndex < planDrawCount; ++drawIndex ) {
		vkClassicGuiDrawPlan_t &drawPlan = vkClassicGuiDrawPlans[ drawIndex ];
		if ( !VK_Exec_PrepareTriGeometryOffsets( slot, drawPlan.tri,
				drawPlan.vertexOffset, drawPlan.indexOffset ) ) {
			VK_Exec_SharedGeometryRestore();
			return VK_ClassicGui_Fail( viewDef,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_GUI_REJECT_GEOMETRY );
		}
	}
	VK_Exec_SharedGeometryCommit();

	// Commit: every command below is infallible command-buffer recording over
	// prevalidated handles and offsets.  Preserve source-surface/pass order.
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	int submittedPasses = 0;
	int submittedNoops = 0;
	for ( int drawIndex = 0; drawIndex < planDrawCount; ++drawIndex ) {
		const vkClassicGuiDrawPlan_t &drawPlan =
			vkClassicGuiDrawPlans[ drawIndex ];
		VK_ClassicGui_BindPreparedGeometry( cmd, slot, drawPlan );
		vkCmdSetScissor( cmd, 0, 1, &drawPlan.scissor );
		for ( int localPass = 0; localPass < drawPlan.passCount; ++localPass ) {
			const vkClassicGuiPassPlan_t &passPlan =
				vkClassicGuiPassPlans[ drawPlan.firstPass + localPass ];
			const rendererEvaluatedMaterialPass_t &pass = *passPlan.pass;
			if ( pass.disposition != RENDERER_MATERIAL_PASS_DRAW ) {
				submittedNoops++;
				continue;
			}

			vkCmdSetDepthTestEnable( cmd,
				pass.depth.testEnabled ? VK_TRUE : VK_FALSE );
			vkCmdSetDepthWriteEnable( cmd,
				pass.depth.writeEnabled ? VK_TRUE : VK_FALSE );
			vkCmdSetDepthCompareOp( cmd, passPlan.depthCompare );
			vkCmdSetCullMode( cmd, passPlan.cullMode );
			vkCmdSetDepthBiasEnable( cmd,
				pass.polygonOffsetEnabled ? VK_TRUE : VK_FALSE );
			if ( pass.polygonOffsetEnabled ) {
				vkCmdSetDepthBias( cmd, pass.polygonOffsetUnits, 0.0f,
					pass.polygonOffsetFactor );
			}

			vkGuiPushConstants_t push;
			memset( &push, 0, sizeof( push ) );
			memcpy( push.mvp, drawPlan.mvp, sizeof( push.mvp ) );
			memcpy( push.stageColor, pass.color, sizeof( push.stageColor ) );
			VK_ClassicGui_SetPushTextureMatrix( pass, push );
			switch ( pass.vertexColor ) {
			case RENDERER_VERTEX_COLOR_MODULATE: push.params[ 0 ] = 1.0f; break;
			case RENDERER_VERTEX_COLOR_INVERSE_MODULATE: push.params[ 0 ] = 2.0f; break;
			default: push.params[ 0 ] = 0.0f; break;
			}
			push.params[ 1 ] = passPlan.alphaTestMode;
			push.params[ 2 ] = pass.alphaTest;

			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				passPlan.pipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.pipelineLayout, 0, 1, &passPlan.descriptor, 0, NULL );
			vkCmdPushConstants( cmd, vkExec.pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( push ), &push );
			VK_Device_CountDrawIndexed( (int)( drawPlan.draw->indexCount ), (int)( drawPlan.draw->vertexCount ) );
			vkCmdDrawIndexed( cmd,
				static_cast<std::uint32_t>( drawPlan.draw->indexCount ),
				1, static_cast<std::uint32_t>( drawPlan.draw->firstIndex ),
				drawPlan.draw->vertexOffset, 0 );
			submittedPasses++;
		}
	}
	vkCmdSetDepthTestEnable( cmd, inWorld ? VK_TRUE : VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, inWorld ? VK_TRUE : VK_FALSE );
	vkCmdSetDepthCompareOp( cmd,
		inWorld ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, inWorld ? VK_CULL_MODE_FRONT_BIT
		: VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );

	const bool coverageRecorded = R_ClassicGuiDomain_RecordOwned( viewDef,
		CLASSIC_GUI_DOMAIN_BACKEND_VULKAN,
		submittedPasses, submittedNoops );
	if ( !coverageRecorded ) {
		// The framebuffer work is already committed.  Never compound a
		// bookkeeping fault by replaying the complete classic view over it.
		common->Warning( "Vulkan: shared classic GUI coverage record rejected after committed view" );
	}
	return true;
}

static bool VK_ClassicGui_DrawOwnedView( const viewDef_t *viewDef ) {
	return VK_ClassicGui_DrawOwnedViewForScope( viewDef,
		CLASSIC_GUI_DOMAIN_SCOPE_ROOT_2D );
}

static bool VK_ClassicGui_DrawOwnedInWorldView( const viewDef_t *viewDef ) {
	return VK_ClassicGui_DrawOwnedViewForScope( viewDef,
		CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD );
}

/*
===============================================================================

	Backend-neutral classic world ambient/material whole-view consumer

	Every descriptor, strict pipeline key, geometry upload, state mapping, and
	coverage count is validated before commit.  The two ordered authored ranges
	then execute at the established pre-fog and post-fog points without reading
	legacy material stages or shader registers.

===============================================================================
*/

enum vkClassicWorldAmbientRejectDetail_t {
	VK_CLASSIC_WORLD_AMBIENT_REJECT_NONE = 0,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEW_MUTATION,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_OFFSCREEN_TARGET,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_RENDER_SCOPE,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEWPORT,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_DOMAIN_COUNTS,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_DRAW_RECORD,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_GEOMETRY,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_STATE,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_BINDING,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_RESIDENCY,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_DESCRIPTOR,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_PIPELINE,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_COVERAGE,
	VK_CLASSIC_WORLD_AMBIENT_REJECT_PHASE
};

typedef struct vkClassicWorldAmbientDrawPlan_s {
	const classicWorldAmbientDomainDraw_t *draw;
	const srfTriangles_t *tri;
	int vertexOffset;
	int indexOffset;
	int firstPass;
	int passCount;
	VkRect2D scissor;
	float mvp[ 16 ];
} vkClassicWorldAmbientDrawPlan_t;

typedef struct vkClassicWorldAmbientPassPlan_s {
	const rendererEvaluatedMaterialPass_t *pass;
	VkPipeline pipeline;
	VkDescriptorSet descriptor;
	int stateBits;
	float alphaTestMode;
	VkCompareOp depthCompare;
	VkCullModeFlags cullMode;
} vkClassicWorldAmbientPassPlan_t;

typedef struct vkClassicWorldAmbientSamplerUndo_s {
	idImage *image;
	textureFilter_t filter;
	textureRepeat_t repeat;
} vkClassicWorldAmbientSamplerUndo_t;

typedef struct vkClassicWorldAmbientPreparedView_s {
	const classicWorldAmbientDomainView_t *view;
	const viewDef_t *viewDef;
	VkViewport viewport;
	int drawCount;
	int passCount;
	int drawablePasses;
	int noopPasses;
	int submittedPasses[ CLASSIC_WORLD_AMBIENT_PHASE_COUNT ];
	int submittedNoops;
	bool ready;
	bool committed;
	bool completedPhase[ CLASSIC_WORLD_AMBIENT_PHASE_COUNT ];
	int samplerUndoCount;
	vkClassicWorldAmbientSamplerUndo_t samplerUndo[
		CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES ];
	vkClassicWorldAmbientDrawPlan_t draws[ CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_DRAWS ];
	vkClassicWorldAmbientPassPlan_t passes[ CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES ];
} vkClassicWorldAmbientPreparedView_t;

static vkClassicWorldAmbientPreparedView_t vkClassicWorldAmbientPreparedView;

static void VK_ClassicWorldAmbient_RestoreSamplerState( void ) {
	vkClassicWorldAmbientPreparedView_t &prepared =
		vkClassicWorldAmbientPreparedView;
	for ( int undoIndex = prepared.samplerUndoCount - 1;
			undoIndex >= 0; --undoIndex ) {
		const vkClassicWorldAmbientSamplerUndo_t &undo =
			prepared.samplerUndo[ undoIndex ];
		if ( undo.image != NULL
				&& ( undo.image->GetFilter() != undo.filter
					|| undo.image->GetRepeat() != undo.repeat ) ) {
			undo.image->SetSamplerState( undo.filter, undo.repeat );
		}
	}
	prepared.samplerUndoCount = 0;
}

static bool VK_ClassicWorldAmbient_SetPreflightSamplerState( idImage *image,
		textureFilter_t filter, textureRepeat_t repeat ) {
	if ( image == NULL ) {
		return false;
	}
	vkClassicWorldAmbientPreparedView_t &prepared =
		vkClassicWorldAmbientPreparedView;
	if ( image->GetFilter() == filter && image->GetRepeat() == repeat ) {
		return true;
	}

	bool alreadyTracked = false;
	for ( int undoIndex = 0; undoIndex < prepared.samplerUndoCount;
			++undoIndex ) {
		if ( prepared.samplerUndo[ undoIndex ].image == image ) {
			alreadyTracked = true;
			break;
		}
	}
	if ( !alreadyTracked ) {
		if ( prepared.samplerUndoCount
				>= CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES ) {
			return false;
		}
		vkClassicWorldAmbientSamplerUndo_t &undo =
			prepared.samplerUndo[ prepared.samplerUndoCount++ ];
		undo.image = image;
		undo.filter = image->GetFilter();
		undo.repeat = image->GetRepeat();
	}

	image->SetSamplerState( filter, repeat );
	return image->GetFilter() == filter && image->GetRepeat() == repeat;
}

static bool VK_ClassicWorldAmbient_Fail( const viewDef_t *viewDef,
		classicWorldAmbientDomainFailure_t failure, int detail ) {
	VK_ClassicWorldAmbient_RestoreSamplerState();
	R_ClassicWorldAmbientDomain_RecordBackendFallback( viewDef,
		CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN, failure, detail );
	return false;
}

static bool VK_ClassicWorldAmbient_SourceNoopValid(
		const classicWorldAmbientDomainDraw_t &draw ) {
	switch ( draw.sourceSurface ) {
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NULL_SURFACE:
		if ( draw.legacyDrawSurf != NULL ) {
			return false;
		}
		break;
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_MISSING_MATERIAL:
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NO_AMBIENT:
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY:
		if ( draw.legacyDrawSurf == NULL ) {
			return false;
		}
		break;
	default:
		return false;
	}
	return !draw.packetBacked && draw.drawPacketIndex == -1
		&& draw.depthDrawPacketIndex == -1 && !draw.depthPrerequisite
		&& draw.firstEvaluatedPass == -1 && draw.evaluatedPassCount == 0
		&& draw.activePassCount == 0 && draw.drawablePassCount == 0
		&& draw.inactivePassCount == 0 && draw.activeNoopPassCount == 0
		&& draw.noopPassCount == 0;
}

static bool VK_ClassicWorldAmbient_MapState(
		const rendererEvaluatedMaterialPass_t &pass,
		vkClassicWorldAmbientPassPlan_t &plan ) {
	if ( pass.kind != RENDERER_MATERIAL_PASS_SURFACE
			|| pass.programFamily != RENDERER_PROGRAM_FIXED
			|| pass.programKey != 0
			|| pass.texgen != RENDERER_TEXGEN_EXPLICIT
			|| pass.textureSemantic != RENDERER_TEXTURE_DIFFUSE
			|| pass.textureResourceId == 0
			|| ( pass.colorWriteMask
				& ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_RGBA ) ) != 0
			|| pass.vertexColor < RENDERER_VERTEX_COLOR_IGNORE
			|| pass.vertexColor > RENDERER_VERTEX_COLOR_INVERSE_MODULATE
			|| pass.blend.colorOperation != RENDERER_BLEND_OP_ADD
			|| pass.blend.alphaOperation != RENDERER_BLEND_OP_ADD
			|| pass.blend.sourceAlpha != pass.blend.sourceColor
			|| pass.blend.destinationAlpha != pass.blend.destinationColor
			|| !pass.depth.testEnabled
			|| ( pass.depth.compareOperation != RENDERER_COMPARE_LESS_OR_EQUAL
				&& pass.depth.compareOperation != RENDERER_COMPARE_EQUAL
				&& pass.depth.compareOperation != RENDERER_COMPARE_ALWAYS ) ) {
		return false;
	}
	const bool replacementBlend = pass.blend.sourceColor == RENDERER_BLEND_ONE
		&& pass.blend.destinationColor == RENDERER_BLEND_ZERO;
	if ( pass.blend.enabled == replacementBlend ) {
		return false;
	}
	int sourceBits = 0;
	int destinationBits = 0;
	if ( !VK_ClassicGui_MapSourceBlend( pass.blend.sourceColor, sourceBits )
			|| !VK_ClassicGui_MapDestinationBlend(
				pass.blend.destinationColor, destinationBits )
			|| !VK_ClassicGui_MapDepthCompare(
				pass.depth.compareOperation, plan.depthCompare )
			|| !VK_ClassicGui_MapAlphaTest( pass, plan.alphaTestMode )
			|| !VK_ClassicGui_MapCull( pass.cull, plan.cullMode ) ) {
		return false;
	}
	plan.stateBits = sourceBits | destinationBits;
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_RED ) == 0 ) {
		plan.stateBits |= GLS_REDMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_GREEN ) == 0 ) {
		plan.stateBits |= GLS_GREENMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_BLUE ) == 0 ) {
		plan.stateBits |= GLS_BLUEMASK;
	}
	if ( ( pass.colorWriteMask & RENDERER_COLOR_WRITE_ALPHA ) == 0 ) {
		plan.stateBits |= GLS_ALPHAMASK;
	}
	if ( !std::isfinite( pass.condition ) || !std::isfinite( pass.alphaTest )
			|| !std::isfinite( pass.polygonOffsetFactor )
			|| !std::isfinite( pass.polygonOffsetUnits ) ) {
		return false;
	}
	for ( int i = 0; i < 4; ++i ) {
		if ( !std::isfinite( pass.color[i] ) ) {
			return false;
		}
	}
	for ( int i = 0; i < 6; ++i ) {
		if ( !std::isfinite( pass.textureMatrix[i] ) ) {
			return false;
		}
	}
	return static_cast<double>( pass.textureMatrix[2] ) >= INT_MIN
		&& static_cast<double>( pass.textureMatrix[2] ) <= INT_MAX
		&& static_cast<double>( pass.textureMatrix[5] ) >= INT_MIN
		&& static_cast<double>( pass.textureMatrix[5] ) <= INT_MAX;
}

static bool VK_ClassicWorldAmbient_BuildScissor(
		const classicWorldAmbientDomainView_t &view,
		const classicWorldAmbientDomainDraw_t &draw, int framebufferHeight,
		VkRect2D &scissor ) {
	const int viewportWidth = view.viewportX2 - view.viewportX1 + 1;
	const int viewportHeight = view.viewportY2 - view.viewportY1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 || framebufferHeight <= 0 ) {
		return false;
	}
	const bool useDrawScissor = r_useScissor.GetBool();
	const int requestedX1 = useDrawScissor ? draw.scissorX1 : view.scissorX1;
	const int requestedY1 = useDrawScissor ? draw.scissorY1 : view.scissorY1;
	const int requestedX2 = useDrawScissor ? draw.scissorX2 : view.scissorX2;
	const int requestedY2 = useDrawScissor ? draw.scissorY2 : view.scissorY2;
	if ( requestedX2 < requestedX1 || requestedY2 < requestedY1 ) {
		return false;
	}
	int x0 = Max( view.viewportX1,
		view.viewportX1 + requestedX1 );
	int x1 = Min( view.viewportX1 + viewportWidth,
		view.viewportX1 + requestedX2 + 1 );
	int y0GL = Max( view.viewportY1,
		view.viewportY1 + requestedY1 );
	int y1GL = Min( view.viewportY1 + viewportHeight,
		view.viewportY1 + requestedY2 + 1 );
	x0 = Max( 0, x0 );
	x1 = Min( VK_Exec_ActiveFramebufferWidth(), x1 );
	y0GL = Max( 0, y0GL );
	y1GL = Min( framebufferHeight, y1GL );
	if ( x1 <= x0 || y1GL <= y0GL ) {
		return false;
	}
	const int y0 = framebufferHeight - y1GL;
	const int y1 = framebufferHeight - y0GL;
	scissor.offset.x = x0;
	scissor.offset.y = y0;
	scissor.extent.width = static_cast<std::uint32_t>( x1 - x0 );
	scissor.extent.height = static_cast<std::uint32_t>( y1 - y0 );
	return true;
}

static bool VK_ClassicWorldAmbient_Preflight( const viewDef_t *viewDef ) {
	std::memset( &vkClassicWorldAmbientPreparedView, 0,
		sizeof( vkClassicWorldAmbientPreparedView ) );
	vkClassicWorldAmbientPreparedView_t &prepared =
		vkClassicWorldAmbientPreparedView;
	const classicWorldAmbientDomainView_t *view =
		R_ClassicWorldAmbientDomain_FindView( viewDef );
	prepared.view = view;
	prepared.viewDef = viewDef;
	if ( view == NULL ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEW_MUTATION );
	}
	if ( view->backendOutcome[ CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN ]
			== CLASSIC_WORLD_AMBIENT_BACKEND_FALLBACK ) {
		return false;
	}
	if ( !view->ready ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			view->failure != CLASSIC_WORLD_AMBIENT_FAILURE_NONE
				? view->failure : CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY,
			view->failureDetail );
	}
	if ( viewDef == NULL || view->viewDef != viewDef
			|| viewDef->viewEntitys == NULL
			|| view->sourceSurfaceCount != viewDef->numDrawSurfs
			|| view->sourceSurfaceCount < 0
			|| view->sourceSurfaceCount > SCENE_PACKET_MAX_DRAWS
			|| view->drawableSurfaceCount < 0
			|| view->drawableSurfaceCount > view->sourceSurfaceCount
			|| view->noopSurfaceCount
				!= view->sourceSurfaceCount - view->drawableSurfaceCount
			|| view->drawCount < 0
			|| view->drawCount > CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_DRAWS
			|| view->evaluatedPassCount < 0
			|| view->evaluatedPassCount
				> CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES
			|| ( viewDef->numDrawSurfs > 0 && viewDef->drawSurfs == NULL ) ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_DOMAIN_COUNTS );
	}
	const int allowedRenderFlags =
		RF_NO_GUI | RF_PENUMBRA_MAP | RF_PRIMARY_VIEW;
	if ( viewDef->isSubview || viewDef->isMirror || viewDef->isXraySubview
			|| viewDef->isEditor || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->numClipPlanes != 0
			|| viewDef->renderWorld == NULL || viewDef->viewLights != NULL
			|| viewDef->renderView.viewID < 0
			|| ( viewDef->renderFlags & ~allowedRenderFlags ) != 0
			|| viewDef->numOutlineDrawSurfs != 0
			|| viewDef->renderView.globalMaterial != NULL
			|| r_showOverDraw.GetInteger() != 0 || r_singleTriangle.GetBool()
			|| r_skipAmbient.GetBool() || r_skipRender.GetBool()
			|| r_skipRenderContext.GetBool()
			|| r_portalsDistanceCull.GetBool()
			|| r_celShading.GetBool() || r_celShadingWorld.GetBool()
			|| r_forceAmbient.GetFloat() > 0.0f ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEW_MUTATION );
	}
	if ( vkExec.activeRenderTexture != NULL || vkExec.activeColorEntry != NULL
			|| vkExec.activeDepthEntry != NULL || backEnd.renderTexture != NULL
			|| backEnd.feedbackRenderTexture != NULL ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_OFFSCREEN_TARGET );
	}
	if ( !vkExec.frameOpen || !vkExec.mainScopeOpen
			|| vkExec.cmd == VK_NULL_HANDLE
			|| vkExec.pendingSpecialEffectsView != NULL
			|| vkExec.pendingSpecialEffectsMask != 0
			|| vkExec.pendingSpecialEffectsSource != NULL
			|| vkExec.pendingSpecialEffectsNeedsResolve
			|| !VK_Exec_PipelineTargetsMatch( vkExec.activePipelineTarget,
				VK_Exec_SwapchainPipelineTarget() ) ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_RENDER_SCOPE );
	}

	const int framebufferWidth = VK_Exec_ActiveFramebufferWidth();
	const int framebufferHeight = VK_Exec_ActiveFramebufferHeight();
	const int viewportWidth = view->viewportX2 - view->viewportX1 + 1;
	const int viewportHeight = view->viewportY2 - view->viewportY1 + 1;
	if ( view->viewportX1 != viewDef->viewport.x1
			|| view->viewportY1 != viewDef->viewport.y1
			|| view->viewportX2 != viewDef->viewport.x2
			|| view->viewportY2 != viewDef->viewport.y2
			|| view->scissorX1 != viewDef->scissor.x1
			|| view->scissorY1 != viewDef->scissor.y1
			|| view->scissorX2 != viewDef->scissor.x2
			|| view->scissorY2 != viewDef->scissor.y2
			|| std::memcmp( view->projectionMatrix, viewDef->projectionMatrix,
				sizeof( view->projectionMatrix ) ) != 0
			|| viewportWidth <= 0 || viewportHeight <= 0
			|| view->viewportX1 < 0 || view->viewportY1 < 0
			|| view->viewportX2 >= framebufferWidth
			|| view->viewportY2 >= framebufferHeight
			|| !VK_BuildCanonicalViewport( prepared.viewport,
				static_cast<float>( view->viewportX1 ),
				static_cast<float>( view->viewportY1 ),
				static_cast<float>( viewportWidth ),
				static_cast<float>( viewportHeight ),
				static_cast<float>( framebufferHeight ) ) ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEWPORT );
	}
	for ( int matrixIndex = 0; matrixIndex < 16; ++matrixIndex ) {
		if ( !std::isfinite( view->projectionMatrix[matrixIndex] ) ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEW_MUTATION );
		}
	}

	int previousSourceSurface = -1;
	int sourceNoopSurfaces = 0;
	int drawablePasses = 0;
	int activePasses = 0;
	int inactivePasses = 0;
	int activeNoopPasses = 0;
	int noopPasses = 0;
	int phaseDrawablePasses[ CLASSIC_WORLD_AMBIENT_PHASE_COUNT ] = { 0, 0 };
	int phaseNoopPasses[ CLASSIC_WORLD_AMBIENT_PHASE_COUNT ] = { 0, 0 };
	for ( int drawIndex = 0; drawIndex < view->drawCount; ++drawIndex ) {
		const classicWorldAmbientDomainDraw_t *draw =
			R_ClassicWorldAmbientDomain_ViewDraw( *view, drawIndex );
		if ( draw == NULL || draw->sourceSurfaceIndex <= previousSourceSurface
				|| draw->sourceSurfaceIndex < 0
				|| draw->sourceSurfaceIndex >= view->sourceSurfaceCount
				|| viewDef->drawSurfs[ draw->sourceSurfaceIndex ]
					!= draw->legacyDrawSurf ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_DRAW_RECORD );
		}
		previousSourceSurface = draw->sourceSurfaceIndex;
		if ( draw->sourceSurface
				!= CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_DRAWABLE ) {
			if ( !VK_ClassicWorldAmbient_SourceNoopValid( *draw ) ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_DRAW_RECORD );
			}
			sourceNoopSurfaces++;
			continue;
		}
		if ( draw->legacyDrawSurf == NULL || !draw->packetBacked
				|| draw->firstIndex != 0 || draw->vertexOffset != 0
				|| draw->phase < CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG
				|| draw->phase >= CLASSIC_WORLD_AMBIENT_PHASE_COUNT
				|| draw->evaluatedPassCount < 0
				|| prepared.passCount
					> CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES
						- draw->evaluatedPassCount ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_DRAW_RECORD );
		}

		vkClassicWorldAmbientDrawPlan_t &drawPlan =
			prepared.draws[ prepared.drawCount++ ];
		std::memset( &drawPlan, 0, sizeof( drawPlan ) );
		drawPlan.draw = draw;
		drawPlan.tri = draw->legacyDrawSurf->geo;
		drawPlan.vertexOffset = -1;
		drawPlan.indexOffset = -1;
		drawPlan.firstPass = prepared.passCount;
		drawPlan.passCount = draw->evaluatedPassCount;
		const srfTriangles_t *tri = drawPlan.tri;
		if ( tri == NULL || tri->ambientCache == NULL
				|| draw->tableGeneration != view->tableGeneration
				|| tri->numVerts <= 0 || tri->numIndexes <= 0
				|| tri->numIndexes % 3 != 0
				|| tri->numVerts != draw->vertexCount
				|| tri->numIndexes != draw->indexCount
				|| !draw->hasAmbientCache
				|| draw->hasIndexCache != ( tri->indexCache != NULL )
				|| ( tri->indexes == NULL && tri->indexCache == NULL )
				|| draw->legacyDrawSurf->decalColorCache != NULL ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_GEOMETRY );
		}
		if ( !VK_ClassicWorldAmbient_BuildScissor(
				*view, *draw, framebufferHeight, drawPlan.scissor ) ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_VIEWPORT );
		}
		float mvpGL[ 16 ];
		myGlMultMatrix( draw->modelViewMatrix, view->projectionMatrix, mvpGL );
		VK_FixupClipSpaceZ( drawPlan.mvp, mvpGL );
		for ( int matrixIndex = 0; matrixIndex < 16; ++matrixIndex ) {
			if ( !std::isfinite( drawPlan.mvp[matrixIndex] ) ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_DRAW_RECORD );
			}
		}

		int drawDrawablePasses = 0;
		int drawActivePasses = 0;
		int drawInactivePasses = 0;
		int drawActiveNoopPasses = 0;
		for ( int passIndex = 0; passIndex < draw->evaluatedPassCount;
				++passIndex ) {
			const rendererEvaluatedMaterialPass_t *pass =
				R_ClassicWorldAmbientDomain_DrawPass( *draw, passIndex );
			const materialResourceTextureBinding_t *binding =
				R_ClassicWorldAmbientDomain_DrawPassTexture( *draw, passIndex );
			if ( pass == NULL || binding == NULL
					|| pass->order != static_cast<std::uint32_t>( passIndex )
					|| pass->sourceStageIndex < 0
					|| pass->sourceStageIndex != binding->stageIndex
					|| pass->textureResourceId != binding->textureResourceId
					|| binding->image == NULL || binding->textureResourceId == 0
					|| pass->active != ( pass->condition != 0.0f ) ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD );
			}
			vkClassicWorldAmbientPassPlan_t &passPlan =
				prepared.passes[ prepared.passCount++ ];
			std::memset( &passPlan, 0, sizeof( passPlan ) );
			passPlan.pass = pass;
			if ( !VK_ClassicWorldAmbient_MapState( *pass, passPlan ) ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_STATE );
			}
			switch ( pass->disposition ) {
			case RENDERER_MATERIAL_PASS_DRAW:
				if ( !pass->active ) {
					return VK_ClassicWorldAmbient_Fail( viewDef,
						CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD );
				}
				drawDrawablePasses++;
				drawActivePasses++;
				drawablePasses++;
				activePasses++;
				phaseDrawablePasses[ draw->phase ]++;
				break;
			case RENDERER_MATERIAL_PASS_INACTIVE_CONDITION:
				if ( pass->active ) {
					return VK_ClassicWorldAmbient_Fail( viewDef,
						CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD );
				}
				drawInactivePasses++;
				inactivePasses++;
				noopPasses++;
				phaseNoopPasses[ draw->phase ]++;
				break;
			case RENDERER_MATERIAL_PASS_NOOP_ZERO_ONE_BLEND:
			case RENDERER_MATERIAL_PASS_NOOP_BLACK_ADDITIVE:
			case RENDERER_MATERIAL_PASS_NOOP_TRANSPARENT_ALPHA:
				if ( !pass->active ) {
					return VK_ClassicWorldAmbient_Fail( viewDef,
						CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
						VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD );
				}
				drawActiveNoopPasses++;
				drawActivePasses++;
				activeNoopPasses++;
				activePasses++;
				noopPasses++;
				phaseNoopPasses[ draw->phase ]++;
				break;
			default:
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_PASS_RECORD );
			}

			const materialResourceTextureBinding_t *resolved =
				R_MaterialResourceTable_ResolveTextureResource(
					pass->textureResourceId );
			if ( resolved != binding || resolved->image != binding->image
					|| resolved->textureHandle != binding->textureHandle ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_BINDING );
			}
			idImage *image = const_cast<idImage *>( binding->image );
			const idImageOpts &imageOptions = image->GetOpts();
			if ( !binding->loaded || binding->defaulted || binding->missing
					|| binding->filter < TF_LINEAR
					|| binding->filter > TF_DEFAULT
					|| binding->repeat < TR_REPEAT
					|| binding->repeat > TR_CLAMP_TO_ZERO_ALPHA
					|| !image->IsLoaded() || image->IsDefaulted()
					|| image->GetDeviceHandle() != binding->textureHandle
					|| binding->textureHandle == 0
					|| imageOptions.textureType != TT_2D
					|| imageOptions.width <= 0 || imageOptions.height <= 0 ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_RESIDENCY );
			}
			if ( pass->disposition != RENDERER_MATERIAL_PASS_DRAW ) {
				continue;
			}
			if ( !VK_ClassicWorldAmbient_SetPreflightSamplerState(
					image, binding->filter, binding->repeat ) ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_RESIDENCY );
			}
			const vkImageEntry_t *imageEntry =
				VK_Image_GetEntry( binding->textureHandle );
			if ( imageEntry == NULL || imageEntry->isCube
					|| imageEntry->view == VK_NULL_HANDLE
					|| imageEntry->sampler == VK_NULL_HANDLE
					|| imageEntry->layout
						!= VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_TEXTURE_RESIDENCY );
			}
			passPlan.descriptor = VK_GuiExecutor_GetImageDescriptor(
				binding->textureHandle );
			if ( passPlan.descriptor == VK_NULL_HANDLE ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_DESCRIPTOR );
			}
			passPlan.pipeline =
				VK_GuiExecutor_GetPipelineStrict( passPlan.stateBits );
			if ( passPlan.pipeline == VK_NULL_HANDLE ) {
				return VK_ClassicWorldAmbient_Fail( viewDef,
					CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
					VK_CLASSIC_WORLD_AMBIENT_REJECT_PIPELINE );
			}
		}
		if ( drawActivePasses != draw->activePassCount
				|| drawDrawablePasses != draw->drawablePassCount
				|| drawInactivePasses != draw->inactivePassCount
				|| drawActiveNoopPasses != draw->activeNoopPassCount
				|| drawInactivePasses + drawActiveNoopPasses
					!= draw->noopPassCount ) {
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_COVERAGE_MISMATCH,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_COVERAGE );
		}
	}

	if ( prepared.drawCount != view->drawableSurfaceCount
			|| sourceNoopSurfaces != view->noopSurfaceCount
			|| prepared.passCount != view->evaluatedPassCount
			|| activePasses != view->activePassCount
			|| drawablePasses != view->drawablePassCount
			|| inactivePasses != view->inactivePassCount
			|| activeNoopPasses != view->activeNoopPassCount
			|| noopPasses != view->noopPassCount
			|| phaseDrawablePasses[ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ]
				!= view->phaseDrawablePassCount[ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ]
			|| phaseDrawablePasses[ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ]
				!= view->phaseDrawablePassCount[ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ]
			|| phaseNoopPasses[ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ]
				!= view->phaseNoopPassCount[ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ]
			|| phaseNoopPasses[ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ]
				!= view->phaseNoopPassCount[ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_COVERAGE_MISMATCH,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_DOMAIN_COUNTS );
	}

	// Final preflight stage: retain every exact geometry offset without recording
	// bind state. Commit only once the complete upload set fits; otherwise the
	// ring cursors, memos, bound offset, and sampler state all roll back.
	if ( !VK_Exec_SharedGeometryCheckpoint() ) {
		return VK_ClassicWorldAmbient_Fail( viewDef,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_WORLD_AMBIENT_REJECT_GEOMETRY );
	}
	for ( int drawIndex = 0; drawIndex < prepared.drawCount; ++drawIndex ) {
		vkClassicWorldAmbientDrawPlan_t &drawPlan = prepared.draws[drawIndex];
		if ( !VK_Exec_PrepareTriGeometryOffsets( vkExec.frameSlot, drawPlan.tri,
				drawPlan.vertexOffset, drawPlan.indexOffset ) ) {
			VK_Exec_SharedGeometryRestore();
			return VK_ClassicWorldAmbient_Fail( viewDef,
				CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_WORLD_AMBIENT_REJECT_GEOMETRY );
		}
	}
	VK_Exec_SharedGeometryCommit();
	// The desired per-stage descriptors are now sealed and geometry ownership
	// cannot fall back, so retain the final sampler state exactly as before.
	prepared.samplerUndoCount = 0;
	prepared.drawablePasses = drawablePasses;
	prepared.noopPasses = noopPasses;
	prepared.ready = true;
	return true;
}

static void VK_ClassicWorldAmbient_DrawPhase(
		classicWorldAmbientPhase_t phase ) {
	vkClassicWorldAmbientPreparedView_t &prepared =
		vkClassicWorldAmbientPreparedView;
	if ( !prepared.ready || prepared.view == NULL || prepared.viewDef == NULL
			|| phase < CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG
			|| phase >= CLASSIC_WORLD_AMBIENT_PHASE_COUNT
			|| prepared.completedPhase[phase] ) {
		return;
	}
	prepared.committed = true;
	prepared.completedPhase[phase] = true;
	VkCommandBuffer cmd = vkExec.cmd;
	const int slot = vkExec.frameSlot;
	vkCmdSetViewport( cmd, 0, 1, &prepared.viewport );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	for ( int drawIndex = 0; drawIndex < prepared.drawCount; ++drawIndex ) {
		const vkClassicWorldAmbientDrawPlan_t &drawPlan =
			prepared.draws[drawIndex];
		if ( drawPlan.draw->phase != phase ) {
			continue;
		}
		const VkBuffer vertexBuffer = vkExec.vertexRings[slot].buffer;
		const VkDeviceSize vertexOffset =
			static_cast<VkDeviceSize>( drawPlan.vertexOffset );
		vkCmdBindVertexBuffers( cmd, 0, 1, &vertexBuffer, &vertexOffset );
		vkCmdBindIndexBuffer( cmd, vkExec.indexRings[slot].buffer,
			static_cast<VkDeviceSize>( drawPlan.indexOffset ),
			VK_INDEX_TYPE_UINT32 );
		vkCmdSetScissor( cmd, 0, 1, &drawPlan.scissor );
		for ( int localPass = 0; localPass < drawPlan.passCount; ++localPass ) {
			const vkClassicWorldAmbientPassPlan_t &passPlan =
				prepared.passes[ drawPlan.firstPass + localPass ];
			const rendererEvaluatedMaterialPass_t &pass = *passPlan.pass;
			if ( pass.disposition != RENDERER_MATERIAL_PASS_DRAW ) {
				prepared.submittedNoops++;
				continue;
			}
			vkCmdSetDepthTestEnable( cmd, VK_TRUE );
			vkCmdSetDepthWriteEnable( cmd,
				pass.depth.writeEnabled ? VK_TRUE : VK_FALSE );
			vkCmdSetDepthCompareOp( cmd, passPlan.depthCompare );
			vkCmdSetCullMode( cmd, passPlan.cullMode );
			vkCmdSetDepthBiasEnable( cmd,
				pass.polygonOffsetEnabled ? VK_TRUE : VK_FALSE );
			if ( pass.polygonOffsetEnabled ) {
				vkCmdSetDepthBias( cmd, pass.polygonOffsetUnits, 0.0f,
					pass.polygonOffsetFactor );
			}
			vkGuiPushConstants_t push;
			std::memset( &push, 0, sizeof( push ) );
			std::memcpy( push.mvp, drawPlan.mvp, sizeof( push.mvp ) );
			std::memcpy( push.stageColor, pass.color,
				sizeof( push.stageColor ) );
			VK_ClassicGui_SetPushTextureMatrix( pass, push );
			switch ( pass.vertexColor ) {
			case RENDERER_VERTEX_COLOR_MODULATE: push.params[0] = 1.0f; break;
			case RENDERER_VERTEX_COLOR_INVERSE_MODULATE: push.params[0] = 2.0f; break;
			default: push.params[0] = 0.0f; break;
			}
			push.params[1] = passPlan.alphaTestMode;
			push.params[2] = pass.alphaTest;
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				passPlan.pipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vkExec.pipelineLayout, 0, 1, &passPlan.descriptor, 0, NULL );
			vkCmdPushConstants( cmd, vkExec.pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof( push ), &push );
			VK_Device_CountDrawIndexed( (int)( drawPlan.draw->indexCount ), (int)( drawPlan.draw->vertexCount ) );
			vkCmdDrawIndexed( cmd,
				static_cast<std::uint32_t>( drawPlan.draw->indexCount ), 1,
				static_cast<std::uint32_t>( drawPlan.draw->firstIndex ),
				drawPlan.draw->vertexOffset, 0 );
			prepared.submittedPasses[phase]++;
		}
	}
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_FRONT_BIT );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	if ( phase == CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ) {
		const bool coverageRecorded = R_ClassicWorldAmbientDomain_RecordOwned(
			prepared.viewDef, CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN,
			prepared.submittedPasses[ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ],
			prepared.submittedPasses[ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ],
			prepared.submittedNoops );
		if ( !coverageRecorded ) {
			common->Warning( "Vulkan: shared world ambient coverage rejected after committed view" );
		}
	}
}

enum vkClassicCinematicPostRejectDetail_t {
	VK_CLASSIC_CINEMATIC_POST_REJECT_NOT_READY = 0,
	VK_CLASSIC_CINEMATIC_POST_REJECT_SKIP_OR_EMPTY,
	VK_CLASSIC_CINEMATIC_POST_REJECT_FEEDBACK,
	VK_CLASSIC_CINEMATIC_POST_REJECT_BEGIN_FRAME,
	VK_CLASSIC_CINEMATIC_POST_REJECT_GPU_SKINNING,
	VK_CLASSIC_CINEMATIC_POST_REJECT_VIEWPORT,
	VK_CLASSIC_CINEMATIC_POST_REJECT_NO_POST_WALK
};

static void VK_RecordCinematicPostFallbackIfPending(
		const viewDef_t *viewDef, classicCinematicPostDomainScope_t scope,
		classicCinematicPostDomainFailure_t failure, int detail ) {
	if ( !r_rendererSharedCinematicPost.GetBool() ) {
		return;
	}
	const classicCinematicPostDomainView_t *view = NULL;
	if ( scope == CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC ) {
		view = R_ClassicCinematicPostDomain_FindRootCinematicView( viewDef );
	} else if ( scope == CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST ) {
		view = R_ClassicCinematicPostDomain_FindAuthoredPostView( viewDef );
	} else {
		return;
	}
	if ( view == NULL
			|| view->backendOutcome[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN]
				!= CLASSIC_CINEMATIC_POST_BACKEND_UNRECORDED
			|| view->backendCompleted[CLASSIC_CINEMATIC_POST_BACKEND_VULKAN] ) {
		return;
	}
	R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef, scope,
		CLASSIC_CINEMATIC_POST_BACKEND_VULKAN, failure, detail );
}

/*
====================
VK_GuiExecutor_Draw2DView

The RB_STD_DrawShaderPasses contract for 2D views, on the swapchain.
====================
*/
void VK_GuiExecutor_Draw2DView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return;
	}
	if ( vkExec.temporalScenePendingComposite ) {
		if ( !VK_GuiExecutor_BeginFrame()
				|| !VK_TemporalPresentation_CompositePendingScene() ) {
			return;
		}
	}
	const bool sharedCinematicRoot = r_rendererSharedCinematicPost.GetBool()
		&& R_ClassicCinematicPostDomain_FindRootCinematicView( viewDef ) != NULL;
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;
	if ( viewDef->numDrawSurfs <= 0 ) {
		if ( r_rendererSharedGui.GetBool() ) {
			if ( !VK_GuiExecutor_BeginFrame() ) {
				R_ClassicGuiDomain_RecordBackendFallback( viewDef,
					CLASSIC_GUI_DOMAIN_BACKEND_VULKAN,
					CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
					VK_CLASSIC_GUI_REJECT_RENDER_SCOPE );
				return;
			}
			backEnd.viewDef = const_cast<viewDef_t *>( viewDef );
			(void)VK_ClassicGui_DrawOwnedView( viewDef );
		}
		return;
	}
	if ( !VK_GuiExecutor_BeginFrame() ) {
		if ( r_rendererSharedGui.GetBool() ) {
			R_ClassicGuiDomain_RecordBackendFallback( viewDef,
				CLASSIC_GUI_DOMAIN_BACKEND_VULKAN,
				CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
				VK_CLASSIC_GUI_REJECT_RENDER_SCOPE );
		}
		if ( sharedCinematicRoot ) {
			R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef,
				CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
				CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_NOT_READY,
				VK_CLASSIC_CINEMATIC_POST_REJECT_NOT_READY );
		}
		return;
	}
	VK_TemporalPresentation_ObserveHistoryAndCapture();

	backEnd.viewDef = (viewDef_t *)viewDef;
	if ( r_rendererSharedGui.GetBool() && !sharedCinematicRoot
			&& VK_ClassicGui_DrawOwnedView( viewDef ) ) {
		return;
	}

	VkCommandBuffer cmd = vkExec.cmd;
	const int slot = vkExec.frameSlot;
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();

	// 2D semantics regardless of what an earlier 3D view left behind
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );

	// GL bottom-left viewport -> Vulkan negative-height viewport
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	VkViewport viewport;
	if ( !VK_BuildCanonicalViewport( viewport, (float)vpX, (float)vpYGL,
			(float)vpW, (float)vpH, (float)fbHeight ) ) {
		VK_RecordCinematicPostFallbackIfPending( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_CINEMATIC_POST_REJECT_VIEWPORT );
		return;
	}
	vkCmdSetViewport( cmd, 0, 1, &viewport );

	// the front-end ortho projection is the whole-view MVP
	float mvp[ 16 ];
	VK_FixupClipSpaceZ( mvp, viewDef->projectionMatrix );

	for ( int surfNum = 0; surfNum < viewDef->numDrawSurfs; surfNum++ ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ surfNum ];
		const idMaterial *shader = drawSurf->material;
		if ( shader == NULL || !shader->HasAmbient() || shader->IsPortalSky() ) {
			continue;
		}
		const srfTriangles_t *tri = drawSurf->geo;
		if ( tri == NULL || tri->numIndexes <= 0 || tri->indexes == NULL ) {
			continue;
		}
		if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
			continue;
		}
		VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );
		VK_Exec_DrawAmbientStages( viewDef, drawSurf, tri, mvp, false );
	}
	if ( sharedCinematicRoot ) {
		if ( !R_ClassicCinematicPostDomain_ReadyForBackend( viewDef,
				CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN ) ) {
			R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef,
				CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
				CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
				VK_CLASSIC_CINEMATIC_POST_REJECT_NOT_READY );
		} else if ( !R_ClassicCinematicPostDomain_RecordOwned( viewDef,
				CLASSIC_CINEMATIC_POST_SCOPE_ROOT_CINEMATIC,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
				viewDef->numDrawSurfs ) ) {
			common->Warning( "Vulkan: shared cinematic root coverage rejected after committed view" );
		}
	}
}

/*
====================
VK_GuiExecutor_Draw3DView

Phase E world consumer: RB_STD_DrawView's depth prepass + the two ambient
shader-pass walks (pre-fog: decals and sort < SS_MEDIUM; post-fog:
SS_MEDIUM..<SS_POST_PROCESS), with the interaction pass (Phase F1) between
the fill and the walks and the fog/blend light pass (Phase G2) between the
walks. Post-process surfaces belong to Phase H.
====================
*/
void VK_GuiExecutor_Draw3DView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return;
	}
	const bool sharedRenderDemo = r_rendererSharedSpecialFrame.GetBool()
		&& R_ClassicSpecialFrameDomain_FindRenderDemoView( viewDef ) != NULL;
	const bool sharedRenderDemoReady = sharedRenderDemo
		&& R_ClassicSpecialFrameDomain_ReadyForBackend( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
			CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN );
	if ( sharedRenderDemo && !sharedRenderDemoReady ) {
		R_ClassicSpecialFrameDomain_RecordBackendFallback( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
			CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN,
			CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_NOT_READY, 0 );
	}
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;
	if ( viewDef->numDrawSurfs <= 0 ) {
		VK_RecordCinematicPostFallbackIfPending( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_CINEMATIC_POST_REJECT_SKIP_OR_EMPTY );
		return;
	}
	if ( !VK_GuiExecutor_BeginFrame() ) {
		VK_RecordCinematicPostFallbackIfPending( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_NOT_READY,
			VK_CLASSIC_CINEMATIC_POST_REJECT_BEGIN_FRAME );
		return;
	}

	backEnd.viewDef = (viewDef_t *)viewDef;

	VkCommandBuffer cmd = vkExec.cmd;
	const int slot = vkExec.frameSlot;

	drawSurf_t **drawSurfs = (drawSurf_t **)viewDef->drawSurfs;
	const int numDrawSurfs = viewDef->numDrawSurfs;
	// This is deliberately before any viewport/depth/cull state owned by the
	// caller. Compute ends and resumes dynamic rendering exactly once, then the
	// ordinary 3D walk establishes all graphics state from scratch.
	if ( !VK_GpuSkinning_PrepareView( viewDef ) ) {
		VK_RecordCinematicPostFallbackIfPending( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_CINEMATIC_POST_REJECT_GPU_SKINNING );
		return;
	}
	vkSceneScaleState_t sceneScaleState;
	VK_TemporalPresentation_ClearScaleState( sceneScaleState );
	if ( !VK_TemporalPresentation_BeginView( viewDef, sceneScaleState ) ) {
		VK_TemporalPresentation_RecenterDirectProjection(
			sceneScaleState, viewDef );
	}
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();
	const int fbWidth = VK_Exec_ActiveFramebufferWidth();
	// Seal the complete shared ambient plan before the depth clear or any
	// other framebuffer-affecting command.  A failed preflight leaves the
	// established Vulkan world walker completely untouched.
	const bool sharedWorldAmbientOwned =
		!sceneScaleState.active && r_rendererSharedWorldAmbient.GetBool()
		&& VK_ClassicWorldAmbient_Preflight( viewDef );
	// GL bottom-left viewport -> Vulkan negative-height viewport
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	VkViewport viewport;
	if ( !VK_BuildCanonicalViewport( viewport, (float)vpX, (float)vpYGL,
			(float)vpW, (float)vpH, (float)fbHeight ) ) {
		VK_RecordCinematicPostFallbackIfPending( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_CINEMATIC_POST_REJECT_VIEWPORT );
		VK_TemporalPresentation_EndView( viewDef, sceneScaleState, false );
		return;
	}
	vkCmdSetViewport( cmd, 0, 1, &viewport );

	// every 3D view starts from clean depth/stencil, exactly like
	// RB_BeginDrawingView's glClear (subviews are separate earlier commands).
	// The rect must stay inside the render area (the viewDef can carry a
	// stale, larger size for one frame across an OUT_OF_DATE recreate)
	{
		int x0 = vpX > 0 ? vpX : 0;
		int y0 = fbHeight - vpYGL - vpH;
		if ( y0 < 0 ) {
			y0 = 0;
		}
		int x1 = vpX + vpW;
		if ( x1 > fbWidth ) {
			x1 = fbWidth;
		}
		int y1 = fbHeight - vpYGL;
		if ( y1 > fbHeight ) {
			y1 = fbHeight;
		}
		VkImageAspectFlags clearAspects = 0;
		if ( vkExec.activeDepthAttachmentView != VK_NULL_HANDLE
				&& vkExec.activePipelineTarget.depthFormat != VK_FORMAT_UNDEFINED ) {
			clearAspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if ( vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED ) {
			clearAspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		if ( x1 > x0 && y1 > y0 && clearAspects != 0 ) {
			VkClearAttachment clearAtt;
			memset( &clearAtt, 0, sizeof( clearAtt ) );
			clearAtt.aspectMask = clearAspects;
			clearAtt.clearValue.depthStencil.depth = 1.0f;
			clearAtt.clearValue.depthStencil.stencil = 128;
			VkClearRect clearRect;
			memset( &clearRect, 0, sizeof( clearRect ) );
			clearRect.rect.offset.x = x0;
			clearRect.rect.offset.y = y0;
			clearRect.rect.extent.width = (uint32_t)( x1 - x0 );
			clearRect.rect.extent.height = (uint32_t)( y1 - y0 );
			clearRect.layerCount = 1;
			vkCmdClearAttachments( cmd, 1, &clearAtt, 1, &clearRect );
		}
	}

	// per-space state tracking (matrix + weapon depth-range hack)
	const struct viewEntity_s *currentSpace = NULL;
	bool weaponDepthRange = false;
	float mvp[ 16 ];
	VK_FixupClipSpaceZ( mvp, viewDef->projectionMatrix );

	// same-value pipeline/descriptor re-binds are semantic no-ops; the fill
	// uses at most two pipelines and mostly the white-image descriptor, so
	// skip re-recording identical binds (no other bind sites exist in this loop)
	VkPipeline depthFillBoundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet depthFillBoundSet = VK_NULL_HANDLE;

	// ---- pass 1: depth fill (RB_STD_FillDepthBuffer contract) ----
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	// the GL fill runs under RB_BeginDrawingView's front-sided cull
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );

	for ( int surfNum = 0; surfNum < numDrawSurfs; surfNum++ ) {
		const drawSurf_t *drawSurf = drawSurfs[ surfNum ];
		const idMaterial *shader = drawSurf->material;
		const srfTriangles_t *tri = drawSurf->geo;
		if ( shader == NULL || tri == NULL || !shader->IsDrawn() ) {
			continue;
		}
		if ( tri->numIndexes <= 0 || tri->indexes == NULL ) {
			continue;
		}
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;	// translucents neither write nor test here
		}
		if ( tri->ambientCache == NULL ) {
			continue;
		}
		const float *regs = drawSurf->shaderRegisters;

		// if all stages are conditioned off, skip
		int stage;
		const int stageCount = shader->GetNumStages();
		for ( stage = 0; stage < stageCount; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( regs == NULL || regs[ pStage->conditionRegister ] != 0 ) {
				break;
			}
		}
		if ( stage == stageCount ) {
			continue;
		}

		if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
			continue;
		}
		VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );

		// space change: rebuild the MVP (depth hacks included) and the
		// weapon depth-range window
		if ( drawSurf->space != currentSpace ) {
			currentSpace = drawSurf->space;
			VK_BuildSurfMVP( viewDef, drawSurf, mvp );
			const bool wantWeaponRange = drawSurf->space->weaponDepthHack;
			if ( wantWeaponRange != weaponDepthRange ) {
				weaponDepthRange = wantWeaponRange;
				viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
				vkCmdSetViewport( cmd, 0, 1, &viewport );
			}
		}

		// polygon offset per material
		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
			vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
		}

		// subviews down-modulate instead of drawing black
		const bool isSubview = shader->GetSort() == SS_SUBVIEW;
		vkGuiPushConstants_t push;
		memcpy( push.mvp, mvp, sizeof( push.mvp ) );
		push.stageColor[ 0 ] = push.stageColor[ 1 ] = push.stageColor[ 2 ] = isSubview ? 1.0f : 0.0f;
		push.stageColor[ 3 ] = 1.0f;
		push.texMatrixS[ 0 ] = 1.0f; push.texMatrixS[ 1 ] = 0.0f; push.texMatrixS[ 2 ] = 0.0f; push.texMatrixS[ 3 ] = 0.0f;
		push.texMatrixT[ 0 ] = 0.0f; push.texMatrixT[ 1 ] = 1.0f; push.texMatrixT[ 2 ] = 0.0f; push.texMatrixT[ 3 ] = 0.0f;
		push.params[ 0 ] = 0.0f;	// SVC_IGNORE
		push.params[ 1 ] = 0.0f;	// alpha test off (per-stage below)
		push.params[ 2 ] = 0.0f;
		push.params[ 3 ] = 0.0f;	// no texmatrix

		const int fillBlendBits = isSubview ? ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO ) : 0;

		bool drawSolid = shader->Coverage() == MC_OPAQUE;

		if ( shader->Coverage() == MC_PERFORATED ) {
			// alpha-tested stages punch holes; if none draws, fall back solid
			bool didDraw = false;
			for ( stage = 0; stage < stageCount; stage++ ) {
				const shaderStage_t *pStage = shader->GetStage( stage );
				if ( !pStage->hasAlphaTest ) {
					continue;
				}
				if ( regs != NULL && regs[ pStage->conditionRegister ] == 0 ) {
					continue;
				}
				didDraw = true;
				const float stageAlpha = regs != NULL ? regs[ pStage->color.registers[ 3 ] ] : 1.0f;
				if ( stageAlpha <= 0.0f ) {
					continue;
				}
				if ( pStage->texture.image == NULL || pStage->texture.texgen != TG_EXPLICIT ) {
					continue;
				}
				VkDescriptorSet stageDescriptor = VK_GuiExecutor_GetImageDescriptor( pStage->texture.image->GetDeviceHandle() );
				if ( stageDescriptor == VK_NULL_HANDLE ) {
					continue;
				}
				// per-stage polygon offset (RB_PrepareStageTexturing contract)
				const bool stagePolygonOffset = pStage->privatePolygonOffset != 0.0f;
				if ( stagePolygonOffset ) {
					vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
					vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * pStage->privatePolygonOffset, 0.0f, r_offsetFactor.GetFloat() );
				}
				push.stageColor[ 3 ] = stageAlpha;
				push.params[ 1 ] = VK_Exec_AlphaTestModeValue( pStage );
				push.params[ 2 ] = regs != NULL ? regs[ pStage->alphaTestRegister ] : 0.5f;
				VK_Exec_SetPushTextureMatrix( pStage, regs, push );
				VkPipeline pipeline = VK_GuiExecutor_GetPipeline( fillBlendBits );
				if ( pipeline == VK_NULL_HANDLE ) {
					continue;
				}
				if ( pipeline != depthFillBoundPipeline ) {
					vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
					depthFillBoundPipeline = pipeline;
				}
				if ( stageDescriptor != depthFillBoundSet ) {
					vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &stageDescriptor, 0, NULL );
					depthFillBoundSet = stageDescriptor;
				}
				vkCmdPushConstants( cmd, vkExec.pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
				VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
				vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
				if ( stagePolygonOffset ) {
					if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
						vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
					} else {
						vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
					}
				}
				// restore solid-fill push defaults for the next stage
				push.stageColor[ 3 ] = 1.0f;
				push.params[ 1 ] = 0.0f;
				push.params[ 2 ] = 0.0f;
				push.params[ 3 ] = 0.0f;
			}
			if ( !didDraw ) {
				drawSolid = true;
			}
		}

		if ( drawSolid ) {
			VkDescriptorSet whiteDescriptor = VK_GuiExecutor_GetImageDescriptor( globalImages->whiteImage->GetDeviceHandle() );
			VkPipeline pipeline = VK_GuiExecutor_GetPipeline( fillBlendBits );
			if ( whiteDescriptor != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE ) {
				if ( pipeline != depthFillBoundPipeline ) {
					vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
					depthFillBoundPipeline = pipeline;
				}
				if ( whiteDescriptor != depthFillBoundSet ) {
					vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &whiteDescriptor, 0, NULL );
					depthFillBoundSet = whiteDescriptor;
				}
				vkCmdPushConstants( cmd, vkExec.pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
				VK_Device_CountDrawIndexed( (int)( tri->numIndexes ), (int)( tri->numVerts ) );
				vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
			}
		}

		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
		}
	}

	// ---- interactions: per-light bump/diffuse/specular (Phase F1) ----
	// RB_STD_DrawView order: depth fill, then RB_ARB2_DrawInteractions,
	// then the ambient walks. The pass tracks its own space/depth-range
	// state and exits at maxDepth 1.0 with depth bias off; restart the
	// walk baseline to match.
	// Seal all Vulkan resources now, after the established depth prerequisite
	// has consumed its transient allocations but before the first interaction
	// framebuffer write. A rejection leaves the complete classic light/shadow
	// walker untouched.
	const bool sharedWorldInteractionOwned =
		!sceneScaleState.active && r_rendererSharedWorldInteraction.GetBool()
		&& VK_ClassicInteraction_Preflight( viewDef );
	if ( sharedWorldInteractionOwned ) {
		VK_ClassicInteraction_DrawOwnedView( viewDef );
	} else {
		VK_Interactions_DrawLights( viewDef );
	}
	// r_shadowMapDebugOverlay draws where RB_ARB2_DrawInteractions draws it:
	// after the last receiver, while the shadow atlas and point cubes still
	// hold this view's contents. One call site covers both walkers and the
	// views where neither drew a light, exactly like the GL overlay's "NO
	// MAP" state. r_skipInteractions suppresses it on both backends.
	if ( !r_skipInteractions.GetBool() ) {
		VK_ShadowMap_DebugOverlayDraw( viewDef );
	}
	currentSpace = NULL;
	weaponDepthRange = false;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	// R_RenderGuiSurf output sorts before the regular world ambient ranges.
	// Commit its sealed subset at that exact boundary; on rejection the normal
	// walker below retains every source drawSurf unchanged.
	if ( !sceneScaleState.active && r_rendererSharedInWorldGui.GetBool() ) {
		(void)VK_ClassicGui_DrawOwnedInWorldView( viewDef );
	}

	// ---- passes 2-4: ambient walks split at fog, then post-process ----
	int processed = numDrawSurfs;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[ i ]->material != NULL && drawSurfs[ i ]->material->GetSort() >= SS_POST_PROCESS ) {
			processed = i;
			break;
		}
	}
	const classicCinematicPostDomainView_t *sharedAuthoredPostView =
		!sceneScaleState.active && r_rendererSharedCinematicPost.GetBool()
			? R_ClassicCinematicPostDomain_FindAuthoredPostView( viewDef )
			: NULL;
	// A prior member may already have rolled back this complete special-view
	// transaction. Preserve the classic walk for later members without
	// recording the same propagated fallback again.
	const bool sharedAuthoredPostCandidate = sharedAuthoredPostView != NULL
		&& sharedAuthoredPostView->backendOutcome[
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN]
			== CLASSIC_CINEMATIC_POST_BACKEND_UNRECORDED
		&& !sharedAuthoredPostView->backendCompleted[
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN];
	const bool sharedAuthoredPostReady = sharedAuthoredPostCandidate
		&& R_ClassicCinematicPostDomain_ReadyForBackend( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN );
	bool sharedAuthoredPostWalkExecuted = false;
	if ( sharedAuthoredPostCandidate && !sharedAuthoredPostReady ) {
		R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef,
			CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
			CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
			CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
			VK_CLASSIC_CINEMATIC_POST_REJECT_NOT_READY );
	}

	for ( int pass = 0; pass < 3; pass++ ) {
		// ---- fog and blend lights between the two walks (Phase G2) ----
		// RB_STD_DrawView order: pre-fog material passes (draw_common.cpp:
		// 9774), RB_STD_FogAllLights (:9806), post-fog passes (:9818) —
		// fog runs regardless of r_skipAmbient. The pass tracks its own
		// space/depth-range state and exits at maxDepth 1.0 with depth
		// bias off; restart the walk baseline like the interaction pass.
		if ( pass == 1 ) {
			const bool sharedFogBlendOwned =
				!sceneScaleState.active && r_rendererSharedWorldFogBlend.GetBool()
				&& VK_ClassicFogBlend_Preflight( viewDef );
			if ( sharedFogBlendOwned ) {
				VK_ClassicFogBlend_DrawOwnedView( viewDef );
			} else {
				VK_Fog_DrawAllLights( viewDef );
			}
			// Fog changes the framebuffer after any earlier lazy capture.
			backEnd.currentRenderCopied = false;
			backEnd.currentDepthCopied = false;
			currentSpace = NULL;
			weaponDepthRange = false;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport( cmd, 0, 1, &viewport );
		}
		if ( !r_skipAmbient.GetBool() ) {
			if ( sharedWorldAmbientOwned && pass < 2 ) {
				VK_ClassicWorldAmbient_DrawPhase(
					pass == 0
						? CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG
						: CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG );
				continue;
			}
			if ( pass == 2 ) {
				if ( processed >= numDrawSurfs || r_skipPostProcess.GetBool() ) {
					if ( sharedAuthoredPostCandidate ) {
						R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef,
							CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
							CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
							CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
							VK_CLASSIC_CINEMATIC_POST_REJECT_SKIP_OR_EMPTY );
					}
					break;
				}
				bool feedbackReady = true;
				if ( VK_Exec_AutomaticCaptureAllowed() ) {
					feedbackReady = backEnd.currentRenderCopied
							|| VK_Exec_CaptureCurrentRender( viewDef );
					bool needsCurrentDepth = false;
					for ( int surfNum = processed; surfNum < numDrawSurfs; surfNum++ ) {
						if ( VK_Exec_MaterialUsesCurrentDepth( drawSurfs[ surfNum ]->material ) ) {
							needsCurrentDepth = true;
							break;
						}
					}
					if ( needsCurrentDepth && !backEnd.currentDepthCopied ) {
						feedbackReady = VK_Exec_CaptureCurrentDepth( viewDef ) && feedbackReady;
					}
				} else {
					// Explicit offscreen post chains own their source capture.
					backEnd.currentRenderCopied = true;
				}
				if ( !feedbackReady ) {
					if ( sharedAuthoredPostCandidate ) {
						R_ClassicCinematicPostDomain_RecordBackendFallback( viewDef,
							CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
							CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
							CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
							VK_CLASSIC_CINEMATIC_POST_REJECT_FEEDBACK );
					}
					static bool warnedPostCapture = false;
					if ( !warnedPostCapture ) {
						warnedPostCapture = true;
						common->Warning( "Vulkan: post-process surfaces skipped because feedback capture failed" );
					}
					break;
				}
				static bool loggedFirstPostProcessPass = false;
				if ( !loggedFirstPostProcessPass ) {
					loggedFirstPostProcessPass = true;
					common->Printf( "Vulkan: first post-process surface pass: %d surfaces (_currentRender %dx%d)\n",
							numDrawSurfs - processed,
							globalImages->currentRenderImage != NULL
								? globalImages->currentRenderImage->GetUploadWidth() : 0,
							globalImages->currentRenderImage != NULL
								? globalImages->currentRenderImage->GetUploadHeight() : 0 );
				}
			}
			const int surfBegin = pass == 2 ? processed : 0;
			const int surfEnd = pass == 2 ? numDrawSurfs : processed;
			for ( int surfNum = surfBegin; surfNum < surfEnd; surfNum++ ) {
				const drawSurf_t *drawSurf = drawSurfs[ surfNum ];
				if ( R_ClassicGuiDomain_IsLegacyInWorldDrawOwned( viewDef,
						CLASSIC_GUI_DOMAIN_BACKEND_VULKAN, drawSurf ) ) {
					continue;
				}
				const idMaterial *shader = drawSurf->material;
				if ( shader == NULL || !shader->HasAmbient() || shader->IsPortalSky() ) {
					continue;
				}
				if ( shader->SuppressInSubview() ) {
					continue;
				}

				if ( pass < 2 ) {
					// pre-fog: decals + sort < SS_MEDIUM; post-fog:
					// SS_MEDIUM..<SS_POST_PROCESS.
					const bool isDecal = drawSurf->decalColorCache != NULL
							|| ( shader->GetSort() >= SS_DECAL && shader->GetSort() < SS_FAR );
					const bool inPass = pass == 0
							? ( isDecal ? !r_skipDecals.GetBool() : shader->GetSort() < SS_MEDIUM )
							: ( !isDecal && shader->GetSort() >= SS_MEDIUM
								&& shader->GetSort() < SS_POST_PROCESS );
					if ( !inPass ) {
						continue;
					}
					if ( shader->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
							&& shader->GetSort() < SS_POST_PROCESS
							&& !backEnd.currentRenderCopied ) {
						if ( VK_Exec_AutomaticCaptureAllowed() ) {
							if ( !VK_Exec_CaptureCurrentRender( viewDef ) ) {
								continue;
							}
						} else {
							backEnd.currentRenderCopied = true;
						}
					}
				}

				const srfTriangles_t *tri = drawSurf->geo;
				if ( tri == NULL || tri->numIndexes <= 0 || tri->indexes == NULL || tri->ambientCache == NULL ) {
					continue;
				}
				if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
					continue;
				}
				VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );

				if ( drawSurf->space != currentSpace ) {
					currentSpace = drawSurf->space;
					const bool wantWeaponRange = drawSurf->space->weaponDepthHack;
					if ( wantWeaponRange != weaponDepthRange ) {
						weaponDepthRange = wantWeaponRange;
						viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
						vkCmdSetViewport( cmd, 0, 1, &viewport );
					}
				}
				VK_BuildSurfMVP( viewDef, drawSurf, mvp );

				// material cull with the mirror swap (GL_Cull contract)
				switch ( shader->GetCullType() ) {
					case CT_TWO_SIDED:
						vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
						break;
					case CT_BACK_SIDED:
						vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
						break;
					default:
						vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
						break;
				}

				if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
					vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
				}

				vkCmdSetDepthTestEnable( cmd, VK_TRUE );
				VK_Exec_DrawAmbientStages( viewDef, drawSurf, tri, mvp, true );

				if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
				}
			}
			if ( pass == 2 ) {
				sharedAuthoredPostWalkExecuted = true;
			}
		}
	}
	if ( sharedAuthoredPostCandidate && sharedAuthoredPostReady
			&& sharedAuthoredPostWalkExecuted
			&& !R_ClassicCinematicPostDomain_RecordOwned( viewDef,
				CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
				CLASSIC_CINEMATIC_POST_BACKEND_VULKAN,
				numDrawSurfs - processed ) ) {
		common->Warning( "Vulkan: shared authored post coverage rejected after committed range" );
	}
	VK_RecordCinematicPostFallbackIfPending( viewDef,
		CLASSIC_CINEMATIC_POST_SCOPE_AUTHORED_POST,
		CLASSIC_CINEMATIC_POST_FAILURE_BACKEND_REJECTED,
		VK_CLASSIC_CINEMATIC_POST_REJECT_NO_POST_WALK );

	if ( sharedRenderDemoReady
			&& !R_ClassicSpecialFrameDomain_RecordOwned( viewDef,
				CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
				CLASSIC_SPECIAL_FRAME_BACKEND_VULKAN, numDrawSurfs ) ) {
		common->Warning( "Vulkan: shared render-demo coverage rejected after committed view" );
	}

	// RC_DRAW_SPECIAL_EFFECTS immediately precedes this view in the command
	// stream. Consume its Raven controller state only after the complete scene
	// is available, while the world depth attachment is still intact.
	VK_Exec_DrawRVSpecialEffects( viewDef );

	// leave 2D-friendly state for a following HUD view
	if ( weaponDepthRange ) {
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );
	}
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );

	// one-shot bring-up evidence that the world walk emitted real work
	static bool loggedFirstWorldView = false;
	if ( !loggedFirstWorldView ) {
		loggedFirstWorldView = true;
		common->Printf( "Vulkan: first world view drew %d surfaces (rings: %d KB verts, %d KB indexes)\n",
				numDrawSurfs, vkExec.vertexRings[ slot ].cursor / 1024, vkExec.indexRings[ slot ].cursor / 1024 );
	}
	VK_TemporalPresentation_EndView( viewDef, sceneScaleState, true );
}

/*
====================
VK_GuiExecutor_EnsureFrameOpen

Clear-only frames (no 2D view was drawn) still need a presentable image.
====================
*/
bool VK_GuiExecutor_EnsureFrameOpen( void ) {
	if ( !VK_GuiExecutor_BeginFrame() ) {
		return false;
	}
	return VK_TemporalPresentation_CompositePendingScene();
}

bool VK_GuiExecutor_FrameIsOpen( void ) {
	return vkExec.frameOpen;
}

/*
====================
Backend-neutral GPU skinning seam

Vulkan cannot record compute while dynamic models are being built on the
front end. Returning false from PrepareAmbientCache intentionally requests
the established CPU cache as the fail-closed fallback; Draw3D can later
replace its ring upload with compute output while a command buffer is active.
====================
*/
void R_BackendGpuSkinning_Init( const renderBackendCaps_t &caps ) {
	(void)caps;
	// Compute and storage buffers are Vulkan core capabilities. Exact device
	// limits and shader/pipeline creation are validated by the lazy executor.
	vkGpuSkinningBackendRequested = true;
}

void R_BackendGpuSkinning_Shutdown( void ) {
	vkGpuSkinningBackendRequested = false;
}

bool R_BackendGpuSkinning_PrepareAmbientCache( srfTriangles_t *tri,
		bool needsLighting ) {
	(void)tri;
	(void)needsLighting;
	if ( vkExec.initialized
			&& ( !vkGpuSkinningBackendRequested || !vkExec.gpuSkinningAvailable ) ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
	}
	return false;
}

void R_BackendGpuSkinning_PrintGfxInfo( void ) {
	const char *status = vkExec.gpuSkinningAvailable
			? "available"
			: ( vkExec.initialized ? "unavailable" : "pending executor initialization" );
	common->Printf(
			"GPU skinning compute (Vulkan): %s, dispatches=%llu vertices=%llu "
			"fallbacks(validation=%llu ring=%llu memo=%llu)\n",
			status,
			static_cast<unsigned long long>( vkExec.gpuSkinningDispatches ),
			static_cast<unsigned long long>( vkExec.gpuSkinningVertices ),
			static_cast<unsigned long long>( vkExec.gpuSkinningValidationFallbacks ),
			static_cast<unsigned long long>( vkExec.gpuSkinningRingFallbacks ),
			static_cast<unsigned long long>( vkExec.gpuSkinningMemoFallbacks ) );
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
