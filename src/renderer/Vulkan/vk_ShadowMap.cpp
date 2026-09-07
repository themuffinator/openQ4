// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan shadow maps (Phase F2a projected + F2b point,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Port of the GL shadow-map backend (draw_arb2.cpp is excluded from the
	vk module build):

	- PROJECTED/PARALLEL lights render into one per-view depth atlas.
	  Per-view row-scan allocation gives ordinary lights one tile and CSM
	  lights one contiguous 2x2 block. The atlas's real layout is tracked
	  across views/frames so later attachment writes and transfer copies wait
	  for earlier receiver reads. Exact opaque/static single-cascade cache
	  updates copy the live tile into a persistent transfer image; exact hits
	  copy it back into the current atlas tile. Fresh ownership maps alone
	  consume r_shadowMapMaxUpdatesPerView.
	- POINT lights use a lazy scratch pool sized for both receiver ownerships
	  of every admitted light, plus separately identity-resident exact
	  static-cache cubes. Fresh maps render six depth-only face scopes; exact
	  hits sample the resident cube directly.
	  Stored depth is normalized radial distance. Receivers expose both
	  samplerCubeShadow LINEAR/LEQUAL compare and raw-depth sampling. Native
	  depth replaces the obsolete packed-color fallback.
	- Projected tiles use negative-height viewports for GL winding parity.
	  Point faces use positive-height viewports/CLOCKWISE front and a small
	  positive near plane. Depth clipping keeps light-plane-crossing triangles
	  away from the radial-depth interpolation singularity at the face origin.
	  The shared conservative bounds test skips faces a caster cannot touch.
	- RETAIL OWNERSHIP: each light prepares a LOCAL receiver map containing
	  global static + dynamic casters and a GLOBAL receiver map containing
	  global + local static + dynamic casters. noSelfShadow
	  (localInteractions) surfaces therefore cannot catch shadows from the
	  local caster chain; globalInteractions and translucent receivers use
	  the GLOBAL map. When no local casters exist, both receiver states alias
	  the same rendered tile/cube because their contents are identical.
	- Receiver filtering ports the stock ARB2 semantics: fixed/stable-rotated
	  1/5/9/13-tap Poisson kernels for projected and point lights, projected
	  receiver-plane derivative bias and PCSS-lite raw blocker search, plus
	  the source-aware distant-light radius policy.
	- Phase F3 (docs/dev/plans/2026-07-20-vulkan-phase-g.md): the module
	  tracks per-light-class resource generations, but keeps front-end
	  stencil volumes until per-view atlas/cube/material admission is
	  capacity-safe. A mapped resource miss therefore uses retained stencil
	  in the same frame when every required ownership caster has volume
	  coverage. A partial map can instead be sampled together with
	  ownership-specific stencil supplements for casters that the map cannot
	  represent; only a caster missing from both representations fails
	  closed. Hard map/render failures set the shared
	  shadowMapStencilFallbackSticky bit so later front-end frames retain
	  volumes too; budget/subview cache misses are transient and non-sticky.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderWorld_local.h"
#include "../ShadowMapProjected.h"
#include "../ClassicInteractionDomain.h"

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
#include <cmath>
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "VulkanShadowTiming.h"
#include "vk_ShadowMap.h"

// vk_GuiExecutor.cpp narrow accessors (vkExec stays file-static there)
VkCommandBuffer VK_Exec_ActiveCmd( void );
int VK_Exec_ActiveFrameSlot( void );
bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
bool VK_Exec_PrepareTriGeometry( VkCommandBuffer cmd, int slot,
		const srfTriangles_t *tri, int &vertexOffset, int &indexOffset );
void VK_Exec_BindPreparedTriGeometry( VkCommandBuffer cmd, int slot,
		int vertexOffset, int indexOffset );
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkPipeline VK_Exec_CasterPipeline( void );
VkPipeline VK_Exec_PointCasterPipeline( void );
VkPipelineLayout VK_Exec_BasePipelineLayout( void );
bool VK_Exec_BeginMainRendering( bool clearColorDepth );
void VK_Exec_EndMainRendering( void );
bool VK_Exec_UpdateShadowAtlasDescriptors( VkImageView view, VkSampler compareSampler, VkSampler rawSampler );
bool VK_Exec_CreateShadowCubeSets( VkImageView cubeView, VkSampler compareSampler, VkSampler rawSampler, VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] );
VkDescriptorSet VK_Exec_ShadowDescriptorSet( void );
VkPipeline VK_Exec_ShadowOverlayPanelPipeline( bool pointLight );
VkPipeline VK_Exec_ShadowOverlayTextPipeline( void );
VkPipelineLayout VK_Exec_ShadowOverlayPipelineLayout( void );
int VK_Exec_ActiveFramebufferWidth( void );
int VK_Exec_ActiveFramebufferHeight( void );
bool VK_Exec_MainRenderingScopeOpen( void );
void VK_Exec_SetViewScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, int fbHeight );
void VK_Exec_FreeShadowCubeSets( VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] );
void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] );

/*
====================
Module state
====================
*/
static const int VK_SHADOW_LIGHT_HISTORY_SLOTS = 256;

// mirror of the shared 128B caster push block; the alpha matrix rows' unused
// z components carry the two caster depth-offset scalars
typedef struct vkCasterPush_s {
	float			mvp[ 16 ];
	float			depthRow[ 4 ];
	float			alphaS[ 4 ];	// z = slope-scale depth factor
	float			alphaT[ 4 ];	// z = constant depth offset
	float			params[ 4 ];	// x: alpha mode (0 off, 1 greater, -1 less, 2 equal), y: ref, z: scale
} vkCasterPush_t;

// one point-light depth cube: 6 attachment layers + a cube compare view
// (Phase F2b); pool entries are created on demand and reused across views
typedef struct vkPointShadowCube_s {
	VkImage				image;
	VmaAllocation		allocation;
	VkImageView			cubeSampleView;					// cube view, depth aspect (compare + raw sampling)
	VkImageView			faceViews[ 6 ];					// per-face 2D layer views (depth attachment)
	VkDescriptorSet		sets[ VK_FRAMES_IN_FLIGHT ];	// set-7 sets (cube + shadow-block ring)
	VkImageLayout		layout;							// tracked for identity-resident cache reuse
} vkPointShadowCube_t;

typedef struct vkProjectedShadowCacheEntry_s {
	bool				valid;
	bool				reserved;
	int					generation;
	const idRenderWorldLocal *renderWorld;
	int					lightIndex;
	vkShadowReceiverPass_t passKind;
	int					signature;
	int					tileSize;
	int					blockSize;	// tileSize * atlasDiv: a CSM light's resident
									// content is the whole contiguous cascade block
	int					lastUsedFrame;
	int					lastUpdatedFrame;	// content write, not view touch
	shadowMapProjectedLightState_t projectedState;
	VkImage				image;
	VmaAllocation		allocation;
	VkImageLayout		layout;
} vkProjectedShadowCacheEntry_t;

typedef struct vkPointShadowCacheEntry_s {
	bool				valid;
	bool				reserved;
	int					generation;
	const idRenderWorldLocal *renderWorld;
	int					lightIndex;
	vkShadowReceiverPass_t passKind;
	int					signature;
	int					size;
	int					lastUsedFrame;
	int					lastUpdatedFrame;	// content write, not view touch
	float				pointFar;
	float				lightOrigin[ 3 ];
	vkPointShadowCube_t	cube;
} vkPointShadowCacheEntry_t;

typedef struct vkShadowLightHistory_s {
	bool				valid;
	const idRenderWorldLocal *renderWorld;
	int					lightIndex;
	int					lastDynamicFrame;
	int					lastTouchedFrame;
} vkShadowLightHistory_t;

typedef struct vkShadowMapState_s {
	VkImage				atlasImage;
	VmaAllocation		atlasAllocation;
	VkImageView			atlasAttachmentView;	// attachment-compatible depth aspects
	VkImageView			atlasSampleView;		// depth aspect only (compare + raw sampling)
	VkSampler			compareSampler;
	VkSampler			rawSampler;
	int					atlasSize;
	VkImageLayout		atlasLayout;

	// point cube pool (F2b); faceSize 0 = nothing built yet
	vkPointShadowCube_t	pointCubes[ VK_SHADOW_MAX_POINT_CUBES ];
	int					pointCubeFaceSize;

	// Exact-signature persistent static caches. Projected entries are
	// transfer-only tiles copied into the current per-view atlas; point
	// entries retain identity-owned sampled cubes.
	vkProjectedShadowCacheEntry_t projectedCache[ VK_SHADOW_MAX_CACHE_SLOTS ];
	vkPointShadowCacheEntry_t pointCache[ VK_SHADOW_MAX_CACHE_SLOTS ];
	vkShadowLightHistory_t lightHistory[ VK_SHADOW_LIGHT_HISTORY_SLOTS ];
	const idRenderWorldLocal *cacheRenderWorld;
	unsigned int		cacheMapFileCRC;
	int					cacheMapNameHash;
	int					projectedCacheTileSize;

	// per-view resource allocators + light table (reset every
	// PrepareViewLights). pointCubesUsed counts fresh uncached ownership
	// maps; freshUpdates is the r_shadowMapMaxUpdatesPerView admission count.
	int					nextTileX;
	int					nextTileY;
	int					nextTileRowHeight;
	int					pointCubesUsed;
	int					freshUpdates;
	int					projectedCacheHits;
	int					pointCacheHits;
	int					projectedFreshUpdates;
	int					pointFreshUpdates;
	int					composePasses;	// cached static tiles + this view's dynamics
	int					budgetFallbacks;
	int					admissionDenied;	// budget spent on higher-scoring lights
	int					subviewFallbacks;
	int					atlasTilesRendered;	// cascade tiles whose depth was written
	int					atlasTilesAllocated;	// atlasDiv^2 blocks claimed
	int					pointFacesRendered;
	int					pointCasterFaceTests;
	int					pointCasterFaceCulled;
	int					numLights;
	// The view whose PrepareViewLights produced the table below. Shared
	// ownership can reach a view that prepared nothing (shadowMapPassCount 0),
	// which would otherwise leave the previous view's lights readable.
	const viewDef_t *	preparedView;
	int					preparedFrame;
	vkShadowLightState_t lights[ VK_SHADOW_MAX_LIGHTS ];
} vkShadowMapState_t;

static vkShadowMapState_t vkShadow;

// View definitions and their lights live in the frame arena. An empty view
// can skip preparation and later reuse the old view's address. Pointer identity
// alone must not admit diagnostics or sampling of those recycled light records.
static bool VK_ShadowMap_ViewPrepared( const viewDef_t *viewDef ) {
	return viewDef != NULL && vkShadow.preparedView == viewDef
		&& vkShadow.preparedFrame == tr.frameCount;
}

// One sealed caster record may feed both LOCAL and GLOBAL ownership maps, so
// geometry/descriptors are retained once and referenced from both resource
// plans. The reference table is bounded at twice the domain caster arena.
static const int VK_CLASSIC_SHADOW_MAX_CASTER_REFS =
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS * 2;

typedef struct vkClassicShadowAlphaPlan_s {
	const classicInteractionDomainShadowAlphaStage_t *stage;
	VkDescriptorSet		imageSet;
} vkClassicShadowAlphaPlan_t;

typedef struct vkClassicShadowCasterPlan_s {
	const classicInteractionDomainShadowCaster_t *caster;
	int			vertexOffset;
	int			indexOffset;
	int			firstAlpha;
	int			alphaCount;
	VkCullModeFlags		cullMode;
} vkClassicShadowCasterPlan_t;

typedef struct vkClassicShadowPassPlan_s {
	const classicInteractionDomainLight_t *light;
	const classicInteractionDomainShadowMapPass_t *pass;
	const vkShadowLightState_t *physicalLight;
	const vkShadowPassState_t *physicalPass;
	vkShadowReceiverPass_t	receiver;
	int			firstCasterRef;
	int			casterRefCount;
	bool			resourceOwner;
} vkClassicShadowPassPlan_t;

typedef struct vkClassicShadowTransaction_s {
	const classicInteractionDomainView_t *view;
	VkCommandBuffer		cmd;
	int			frameSlot;
	VkPipelineLayout		layout;
	VkPipeline		projectedCasterPipeline;
	VkPipeline		pointCasterPipeline;
	VkDescriptorSet		whiteSet;
	int			projectedCount;
	int			projectedFreshCount;
	int			pointFreshCount;
	int			pointHitCount;
	int			passPlanCount;
	int			casterPlanCount;
	int			casterRefCount;
	int			alphaPlanCount;
	bool			active;
	bool			ready;
	bool			ownsPreparedLights;
	vkClassicShadowPassPlan_t passPlans[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES ];
	vkClassicShadowCasterPlan_t casterPlans[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ];
	int casterRefs[ VK_CLASSIC_SHADOW_MAX_CASTER_REFS ];
	vkClassicShadowAlphaPlan_t alphaPlans[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES ];
} vkClassicShadowTransaction_t;

static vkClassicShadowTransaction_t vkClassicShadowTransaction;

static VkImageAspectFlags VK_ShadowMap_DepthAspectMask( void ) {
	return VK_IMAGE_ASPECT_DEPTH_BIT |
			( vkCtx.shadowDepthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 );
}

static void VK_ShadowMap_ClearProjectedEntryMetadata(
		vkProjectedShadowCacheEntry_t &entry );
static void VK_ShadowMap_ClearPointEntryMetadata(
		vkPointShadowCacheEntry_t &entry );

/*
====================
Phase F3: per-light-class resource honesty + the sticky stencil fallback
====================
*/

// Published for the front-end stencil-volume elision policy
// (R_ShadowMapLightWillUseShadowMaps -> RB_ShadowMapResourcesKnownGood): the
// front end must keep generating stencil volumes until the backend has proven
// it can create each class's shadow-map resources in this video generation —
// the GL contract (draw_arb2.cpp g_shadowMap*ResourcesOkGeneration). Reset
// whenever the class's resources are destroyed.
static int vkShadowProjectedResourcesOkGeneration = -1;
static int vkShadowPointResourcesOkGeneration = -1;

bool VK_ShadowMap_ResourcesKnownGood( bool pointLight ) {
	// VK_ShadowMap_EnsureResources (atlas + compare/raw samplers + atlas
	// descriptor wiring) gates the whole view's shadow pass, point cubes
	// included, so the projected generation is a prerequisite for BOTH
	// classes
	if ( vkShadowProjectedResourcesOkGeneration != tr.videoRestartCount ) {
		return false;
	}
	if ( pointLight && vkShadowPointResourcesOkGeneration != tr.videoRestartCount ) {
		return false;
	}

	// Per-view atlas blocks, point-cube ownership slots, and caster
	// representability are still admitted after the front end has decided
	// whether to build volumes, so an admission miss can leave an elided
	// light with no volume to fall back to in the same frame. That is the
	// OpenGL contract too (draw_arb2.cpp RB_ShadowMapResourcesKnownGood
	// reports the same generation truth): the miss costs one unshadowed
	// frame, and VK_ShadowMap_MarkStencilFallbackSticky restores volume
	// generation from the next frame on.
	//
	// This stayed conservative only while an unresolved receiver was dropped
	// fail-closed. VK_Inter_DrawAllLights now draws every unresolved opaque
	// and translucent receiver unshadowed instead of discarding the light's
	// contribution, so a same-frame miss degrades exactly as it does on GL.
	// The shared front-end mirror (R_ShadowMapLightWillUseShadowMaps) also
	// refuses to elide under a non-zero r_shadowMapMaxUpdatesPerView or a
	// subview policy above 0, so neither discretionary policy can strand an
	// elided light.
	return true;
}

// A hard shadow-map failure marks the light sticky even though Vulkan already
// retains same-frame fallback volumes. The shared bit tells later front-end
// frames to retain volumes too; it does not suppress mapped retries.
void VK_ShadowMap_MarkStencilFallbackSticky( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL || vLight->lightDef->shadowMapStencilFallbackSticky ) {
		return;
	}
	vLight->lightDef->shadowMapStencilFallbackSticky = true;
	common->DPrintf( "shadow map pass failed for lightDef %d; restoring stencil volume generation\n", vLight->lightDef->index );
}

static void VK_ShadowMap_RefreshLightValidity( vkShadowLightState_t &light ) {
	light.valid = false;
	for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
		if ( light.passes[ passIndex ].valid ) {
			light.valid = true;
			return;
		}
	}
}

// Invalidate the owner and every receiver-pass state aliasing its resource.
// A failed LOCAL map must also invalidate an aliased GLOBAL state, while a
// distinct GLOBAL resource remains usable for the current frame.
static void VK_ShadowMap_InvalidatePassResource( vkShadowLightState_t &light,
		const vkShadowReceiverPass_t resourcePass ) {
	const vkShadowPassState_t &owner = light.passes[ resourcePass ];
	if ( owner.cacheEntry >= 0
			&& owner.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS ) {
		if ( light.pointLight ) {
			VK_ShadowMap_ClearPointEntryMetadata(
					vkShadow.pointCache[ owner.cacheEntry ] );
		} else {
			VK_ShadowMap_ClearProjectedEntryMetadata(
					vkShadow.projectedCache[
							owner.cacheEntry ] );
		}
	}
	for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
		vkShadowPassState_t &pass = light.passes[ passIndex ];
		if ( pass.valid && pass.resourcePass == resourcePass ) {
			pass.valid = false;
		}
	}
	VK_ShadowMap_RefreshLightValidity( light );
	VK_ShadowMap_MarkStencilFallbackSticky( light.vLight );
}

static void VK_ShadowMap_ReleasePreparedLights( const bool markSticky ) {
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		if ( markSticky && vkShadow.lights[ i ].valid ) {
			VK_ShadowMap_MarkStencilFallbackSticky(
					vkShadow.lights[ i ].vLight );
		}
		for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
			vkShadowPassState_t &pass =
					vkShadow.lights[ i ].passes[ passIndex ];
			if ( pass.cacheUpdate && pass.cacheEntry >= 0
					&& pass.cacheEntry
						< VK_SHADOW_MAX_CACHE_SLOTS ) {
				if ( vkShadow.lights[ i ].pointLight ) {
					VK_ShadowMap_ClearPointEntryMetadata(
							vkShadow.pointCache[
									pass.cacheEntry ] );
				} else {
					VK_ShadowMap_ClearProjectedEntryMetadata(
							vkShadow.projectedCache[
									pass.cacheEntry ] );
					}
			} else if ( pass.cacheHit && pass.cacheEntry >= 0
					&& pass.cacheEntry
						< VK_SHADOW_MAX_CACHE_SLOTS ) {
				if ( vkShadow.lights[ i ].pointLight ) {
					vkShadow.pointCache[
							pass.cacheEntry ].reserved = false;
				} else {
					vkShadow.projectedCache[
							pass.cacheEntry ].reserved = false;
				}
			}
			vkShadow.lights[ i ].passes[ passIndex ].valid = false;
		}
		vkShadow.lights[ i ].valid = false;
	}
	vkShadow.numLights = 0;
	vkShadow.preparedView = NULL;
	vkShadow.preparedFrame = -1;
	vkShadow.nextTileX = 0;
	vkShadow.nextTileY = 0;
	vkShadow.nextTileRowHeight = 0;
	vkShadow.pointCubesUsed = 0;
	vkShadow.freshUpdates = 0;
	vkShadow.projectedCacheHits = 0;
	vkShadow.pointCacheHits = 0;
	vkShadow.projectedFreshUpdates = 0;
	vkShadow.pointFreshUpdates = 0;
	vkShadow.composePasses = 0;
	vkShadow.admissionDenied = 0;
	vkShadow.atlasTilesRendered = 0;
	vkShadow.atlasTilesAllocated = 0;
	vkShadow.pointFacesRendered = 0;
	vkShadow.pointCasterFaceTests = 0;
	vkShadow.pointCasterFaceCulled = 0;
}

void VK_ShadowMap_AbandonPreparedLights( void ) {
	VK_ShadowMap_ReleasePreparedLights( true );
}

/*
====================
Resources
====================
*/
static void VK_ShadowMap_DestroyProjectedCaches( void ) {
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
			vkProjectedShadowCacheEntry_t &entry =
					vkShadow.projectedCache[ i ];
			if ( entry.image != VK_NULL_HANDLE ) {
				vmaDestroyImage( vkCtx.allocator, entry.image,
						entry.allocation );
			}
		}
	}
	memset( vkShadow.projectedCache, 0,
			sizeof( vkShadow.projectedCache ) );
	vkShadow.projectedCacheTileSize = 0;
}

static void VK_ShadowMap_DestroyAtlas( void ) {
	// Reset class resource truth when the atlas disappears: the front-end
	// elision gate must see false again until the resources prove out.
	vkShadowProjectedResourcesOkGeneration = -1;
	VK_ShadowMap_DestroyProjectedCaches();
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		return;
	}
	if ( vkShadow.atlasSampleView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, vkShadow.atlasSampleView, NULL );
		vkShadow.atlasSampleView = VK_NULL_HANDLE;
	}
	if ( vkShadow.atlasAttachmentView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, vkShadow.atlasAttachmentView, NULL );
		vkShadow.atlasAttachmentView = VK_NULL_HANDLE;
	}
	if ( vkShadow.atlasImage != VK_NULL_HANDLE ) {
		vmaDestroyImage( vkCtx.allocator, vkShadow.atlasImage, vkShadow.atlasAllocation );
		vkShadow.atlasImage = VK_NULL_HANDLE;
		vkShadow.atlasAllocation = NULL;
	}
	vkShadow.atlasSize = 0;
	vkShadow.atlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

static void VK_ShadowMap_DestroyPointCube( vkPointShadowCube_t &cube ) {
	VK_Exec_FreeShadowCubeSets( cube.sets );
	for ( int f = 0 ; f < 6 ; f++ ) {
		if ( cube.faceViews[ f ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, cube.faceViews[ f ], NULL );
		}
	}
	if ( cube.cubeSampleView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, cube.cubeSampleView, NULL );
	}
	if ( cube.image != VK_NULL_HANDLE ) {
		vmaDestroyImage( vkCtx.allocator, cube.image, cube.allocation );
	}
	memset( &cube, 0, sizeof( cube ) );
}

static void VK_ShadowMap_DestroyPointCaches( void ) {
	for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
		VK_ShadowMap_DestroyPointCube( vkShadow.pointCache[ i ].cube );
		memset( &vkShadow.pointCache[ i ], 0,
				sizeof( vkShadow.pointCache[ i ] ) );
	}
}

static void VK_ShadowMap_DestroyPointCubes( void ) {
	vkShadowPointResourcesOkGeneration = -1;
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		return;
	}
	for ( int i = 0 ; i < VK_SHADOW_MAX_POINT_CUBES ; i++ ) {
		VK_ShadowMap_DestroyPointCube( vkShadow.pointCubes[ i ] );
	}
	VK_ShadowMap_DestroyPointCaches();
	vkShadow.pointCubeFaceSize = 0;
}

void VK_ShadowMap_Shutdown( void ) {
	VK_ShadowGpuTiming_Shutdown();
	vkShadowProjectedResourcesOkGeneration = -1;
	vkShadowPointResourcesOkGeneration = -1;
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		memset( &vkShadow, 0, sizeof( vkShadow ) );
		return;
	}
	VK_ShadowMap_DestroyAtlas();
	VK_ShadowMap_DestroyPointCubes();
	if ( vkShadow.compareSampler != VK_NULL_HANDLE ) {
		vkDestroySampler( vkCtx.device, vkShadow.compareSampler, NULL );
	}
	if ( vkShadow.rawSampler != VK_NULL_HANDLE ) {
		vkDestroySampler( vkCtx.device, vkShadow.rawSampler, NULL );
	}
	memset( &vkShadow, 0, sizeof( vkShadow ) );
}

// creates (or resizes on r_shadowMapAtlasSize changes) the depth atlas plus
// the hardware-compare and raw-depth samplers, then points the executor's
// shadow descriptor sets at both. Failure leaves every light on retained
// stencil for the view; publishing both receiver families is atomic.
static bool VK_ShadowMap_EnsureResources( void ) {
	if ( !vkCtx.initialized || vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED ) {
		return false;
	}

	int wantedSize = idMath::ClampInt( 2048, 8192, r_shadowMapAtlasSize.GetInteger() );
	const int maxDim = (int)vkCtx.deviceProperties.limits.maxImageDimension2D;
	if ( maxDim > 0 && wantedSize > maxDim ) {
		wantedSize = maxDim;
	}

	if ( vkShadow.atlasImage != VK_NULL_HANDLE &&
			vkShadow.atlasSize == wantedSize &&
			vkShadow.compareSampler != VK_NULL_HANDLE &&
			vkShadow.rawSampler != VK_NULL_HANDLE ) {
		// live resources are proof for the current video generation too
		vkShadowProjectedResourcesOkGeneration = tr.videoRestartCount;
		return true;
	}

	if ( vkShadow.atlasImage != VK_NULL_HANDLE ) {
		// size change: frames in flight may still reference the old atlas and
		// its descriptor writes; this is a rare cvar path, wait it out
		vkDeviceWaitIdle( vkCtx.device );
		VK_Exec_UpdateShadowAtlasDescriptors( VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE );
		VK_ShadowMap_DestroyAtlas();
	}

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = vkCtx.shadowDepthFormat;
	ici.extent.width = (uint32_t)wantedSize;
	ici.extent.height = (uint32_t)wantedSize;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &vkShadow.atlasImage, &vkShadow.atlasAllocation, NULL ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas creation failed (%dx%d)", wantedSize, wantedSize );
		vkShadow.atlasImage = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = vkShadow.atlasImage;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = vkCtx.shadowDepthFormat;
	ivci.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 1;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkShadow.atlasAttachmentView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas attachment view creation failed" );
		VK_ShadowMap_DestroyAtlas();
		return false;
	}
	// Sampled shadow depth always selects exactly the depth aspect, including
	// when the attachment format also carries stencil.
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkShadow.atlasSampleView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas sample view creation failed" );
		VK_ShadowMap_DestroyAtlas();
		return false;
	}

	if ( vkShadow.compareSampler == VK_NULL_HANDLE ) {
		// Hardware LEQUAL compare with LINEAR filtering provides 2x2 PCF.
		// Some sampleable depth formats cannot filter linearly, in which case
		// NEAREST comparison remains valid and keeps shadows available.
		VkSamplerCreateInfo sci;
		memset( &sci, 0, sizeof( sci ) );
		sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		const VkFilter compareFilter =
				vkCtx.shadowDepthFilterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		sci.magFilter = compareFilter;
		sci.minFilter = compareFilter;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.compareEnable = VK_TRUE;
		sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		sci.maxLod = 0.25f;
		if ( vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.compareSampler ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: shadow compare sampler creation failed" );
			VK_ShadowMap_DestroyAtlas();
			return false;
		}
	}

	if ( vkShadow.rawSampler == VK_NULL_HANDLE ) {
		// Manual comparisons and PCSS blocker search must read native depth
		// without sampler filtering. The image/view is shared with the compare
		// path, but the sampler state is intentionally independent.
		VkSamplerCreateInfo sci;
		memset( &sci, 0, sizeof( sci ) );
		sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter = VK_FILTER_NEAREST;
		sci.minFilter = VK_FILTER_NEAREST;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.compareEnable = VK_FALSE;
		sci.maxLod = 0.25f;
		if ( vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.rawSampler ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: shadow raw-depth sampler creation failed" );
			VK_ShadowMap_DestroyAtlas();
			return false;
		}
	}

	if ( !VK_Exec_UpdateShadowAtlasDescriptors( vkShadow.atlasSampleView,
			vkShadow.compareSampler, vkShadow.rawSampler ) ) {
		VK_ShadowMap_DestroyAtlas();
		return false;
	}

	vkShadow.atlasSize = wantedSize;
	vkShadow.atlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// Record projected resource truth for this video generation; the public
	// gate remains conservative until pre-front-end admission exists.
	vkShadowProjectedResourcesOkGeneration = tr.videoRestartCount;
	return true;
}

static int VK_ShadowMap_PointSizeValue( void ) {
	int wantedSize = idMath::ClampInt( 128, 2048, r_shadowMapPointSize.GetInteger() );
	const int maxDim = (int)vkCtx.deviceProperties.limits.maxImageDimensionCube;
	if ( maxDim > 0 && wantedSize > maxDim ) {
		wantedSize = maxDim;
	}
	return wantedSize;
}

static bool VK_ShadowMap_EnsurePointConfiguration( void ) {
	const int wantedSize = VK_ShadowMap_PointSizeValue();
	if ( vkShadow.pointCubeFaceSize != 0 && vkShadow.pointCubeFaceSize != wantedSize ) {
		// Scratch and resident cubes share one size contract. A cvar change is
		// rare and may leave descriptors/images in flight, so retire them only
		// after the device becomes idle.
		vkDeviceWaitIdle( vkCtx.device );
		VK_ShadowMap_DestroyPointCubes();
	}
	vkShadow.pointCubeFaceSize = wantedSize;
	return wantedSize > 0;
}

// Creates one scratch or identity-resident point cube. Descriptor sets never
// change identity after creation, so exact cache hits can sample the cube
// directly without touching view-order scratch slots.
static bool VK_ShadowMap_CreatePointCube( vkPointShadowCube_t &cube ) {
	if ( vkShadow.compareSampler == VK_NULL_HANDLE ||
			vkShadow.rawSampler == VK_NULL_HANDLE ||
			!VK_ShadowMap_EnsurePointConfiguration() ) {
		return false;
	}
	if ( cube.image != VK_NULL_HANDLE ) {
		vkShadowPointResourcesOkGeneration = tr.videoRestartCount;
		return true;
	}
	const int wantedSize = vkShadow.pointCubeFaceSize;

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = vkCtx.shadowDepthFormat;
	ici.extent.width = (uint32_t)wantedSize;
	ici.extent.height = (uint32_t)wantedSize;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 6;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &cube.image, &cube.allocation, NULL ) != VK_SUCCESS ) {
		static bool warnedCube = false;
		if ( !warnedCube ) {
			warnedCube = true;
			common->Warning( "Vulkan: point shadow cube creation failed (%dx%d x6)", wantedSize, wantedSize );
		}
		cube.image = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = cube.image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	ivci.format = vkCtx.shadowDepthFormat;
	// Sampled shadow depth always selects exactly the depth aspect, including
	// when the attachment format also carries stencil.
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 6;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &cube.cubeSampleView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow cube view creation failed" );
		VK_ShadowMap_DestroyPointCube( cube );
		return false;
	}

	for ( int f = 0 ; f < 6 ; f++ ) {
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();
		ivci.subresourceRange.baseArrayLayer = (uint32_t)f;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &cube.faceViews[ f ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: point shadow cube face view creation failed" );
			VK_ShadowMap_DestroyPointCube( cube );
			return false;
		}
	}

	if ( !VK_Exec_CreateShadowCubeSets( cube.cubeSampleView,
			vkShadow.compareSampler, vkShadow.rawSampler, cube.sets ) ) {
		VK_ShadowMap_DestroyPointCube( cube );
		return false;
	}

	// Record point resource truth; ResourcesKnownGood remains conservative.
	vkShadowPointResourcesOkGeneration = tr.videoRestartCount;
	return true;
}

static bool VK_ShadowMap_EnsurePointCube( int index ) {
	if ( index < 0 || index >= VK_SHADOW_MAX_POINT_CUBES ) {
		return false;
	}
	return VK_ShadowMap_CreatePointCube( vkShadow.pointCubes[ index ] );
}

static bool VK_ShadowMap_EnsurePointCacheCube( int index ) {
	if ( index < 0 || index >= VK_SHADOW_MAX_CACHE_SLOTS ) {
		return false;
	}
	return VK_ShadowMap_CreatePointCube(
			vkShadow.pointCache[ index ].cube );
}

/*
====================
Point-light math (ports of the excluded draw_arb2.cpp helpers)
====================
*/

// port of RB_PointShadowMapBuildViewAxis (draw_arb2.cpp:7303): the GL cube
// face view axes. Combined with the GL viewport row mapping (positive-height
// viewport: NDC y=-1 -> texel row 0) these produce the exact GL/Vulkan cube
// face (s,t) layout — the specs share the face selection table.
static void VK_ShadowMap_PointFaceViewAxis( const int cubeFace, idMat3 &axis ) {
	memset( &axis, 0, sizeof( axis ) );

	switch ( cubeFace ) {
	case 0:
		axis[0][0] = 1.0f;
		axis[1][2] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	case 1:
		axis[0][0] = -1.0f;
		axis[1][2] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	case 2:
		axis[0][1] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = 1.0f;
		break;
	case 3:
		axis[0][1] = -1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = -1.0f;
		break;
	case 4:
		axis[0][2] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	default:
		axis[0][2] = -1.0f;
		axis[1][0] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	}
}

// port of RB_PointShadowMapBuildModelViewMatrix (draw_arb2.cpp:7340): the
// world -> face-view matrix through the shared R_SetViewMatrix, a rigid
// transform centered on the light origin (so view-space length == world
// radial distance)
static void VK_ShadowMap_PointFaceViewMatrix( const idVec3 &origin, const int cubeFace, float matrix[ 16 ] ) {
	viewDef_t shadowView;
	memset( &shadowView, 0, sizeof( shadowView ) );
	shadowView.renderView.vieworg = origin;
	VK_ShadowMap_PointFaceViewAxis( cubeFace, shadowView.renderView.viewaxis );
	R_SetViewMatrix( &shadowView );
	memcpy( matrix, shadowView.worldSpace.modelViewMatrix, sizeof( shadowView.worldSpace.modelViewMatrix ) );
}

/*
====================
Exact static cache + per-view update admission

Only an exact opaque/static signature may satisfy a receiver pass. A stale
entry is never sampled to work around the update budget or subview policy:
those misses use the same-frame stencil volumes when that ownership has a
complete fallback. If any admitted caster is map-only, correctness overrides
the discretionary budget/subview fallback and renders the ownership map.
====================
*/
typedef enum vkShadowScheduleAction_e {
	VK_SHADOW_SCHEDULE_UPDATE = 0,
	VK_SHADOW_SCHEDULE_REUSE,
	VK_SHADOW_SCHEDULE_FALLBACK
} vkShadowScheduleAction_t;

typedef struct vkShadowSchedule_s {
	vkShadowScheduleAction_t	action;
	bool					cacheable;
	int						signature;
	int						cacheEntry;
	vkShadowReceiverPass_t	cachePassKind;
} vkShadowSchedule_t;

static int VK_ShadowMap_LightIndex( const viewLight_t *vLight ) {
	return ( vLight != NULL && vLight->lightDef != NULL )
			? vLight->lightDef->index : -1;
}

static int VK_ShadowMap_HashInt( int hash, const int value ) {
	const unsigned int h = static_cast<unsigned int>( hash );
	const unsigned int v = static_cast<unsigned int>( value );
	return static_cast<int>( ( h ^ v ) * 16777619u );
}

static int VK_ShadowMap_HashFloat( int hash, const float value ) {
	return R_ShadowMapHashFloat( hash, value );
}

static int VK_ShadowMap_HashPointer( int hash, const void *pointer ) {
	const unsigned long long value =
			static_cast<unsigned long long>(
					reinterpret_cast<size_t>( pointer ) );
	hash = VK_ShadowMap_HashInt( hash,
			static_cast<int>( value & 0xffffffffull ) );
	hash = VK_ShadowMap_HashInt( hash,
			static_cast<int>( value >> 32 ) );
	return hash;
}

static int VK_ShadowMap_HashString( int hash, const char *text ) {
	if ( text == NULL ) {
		return VK_ShadowMap_HashInt( hash, 0 );
	}
	for ( const unsigned char *p =
			reinterpret_cast<const unsigned char *>( text ) ; *p ; p++ ) {
		hash = VK_ShadowMap_HashInt( hash,
				static_cast<int>( *p ) );
	}
	return hash;
}

static int VK_ShadowMap_MapNameHash( const viewDef_t *viewDef ) {
	int hash = static_cast<int>( 2166136261u );
	if ( viewDef == NULL || viewDef->renderWorld == NULL ) {
		return VK_ShadowMap_HashInt( hash, 0 );
	}
	return VK_ShadowMap_HashString( hash,
			viewDef->renderWorld->mapName.c_str() );
}

static vkShadowReceiverPass_t VK_ShadowMap_CachePassKind(
		const viewLight_t *vLight,
		const vkShadowReceiverPass_t requestedPass ) {
	// With no local caster of any coverage class, LOCAL (global casters) and
	// GLOBAL (global + local casters) own identical content. Canonicalize both
	// identities to GLOBAL so a LOCAL-filled entry also hits a later
	// GLOBAL-only view.
	if ( vLight != NULL
			&& vLight->localShadowMapCasters == NULL
			&& vLight->localShadowMapDynamicCasters == NULL
			&& vLight->localTranslucentShadowMapCasters == NULL ) {
		return VK_SHADOW_RECEIVER_GLOBAL;
	}
	return requestedPass;
}

static int VK_ShadowMap_BuildPassSignatureForView(
		const viewLight_t *vLight, const viewDef_t *viewDef,
		const vkShadowReceiverPass_t passKind, const bool pointLight,
		const int resourceSize, const int atlasDiv,
		const int cascadeCount ) {
	int hash = static_cast<int>( 2166136261u );
	hash = VK_ShadowMap_HashPointer( hash,
			viewDef != NULL ? viewDef->renderWorld : NULL );
	hash = VK_ShadowMap_HashInt( hash,
			( viewDef != NULL && viewDef->renderWorld != NULL )
				? static_cast<int>(
						viewDef->renderWorld->mapFileCRC ) : 0 );
	hash = VK_ShadowMap_HashInt( hash,
			VK_ShadowMap_MapNameHash( viewDef ) );
	hash = VK_ShadowMap_HashInt( hash,
			VK_ShadowMap_LightIndex( vLight ) );
	hash = VK_ShadowMap_HashInt( hash,
			static_cast<int>( passKind ) );
	const shadowMapLightClassification_t classification =
			R_ClassifyShadowMapLight( vLight );
	hash = VK_ShadowMap_HashInt( hash,
			static_cast<int>( classification.lightClass ) );
	hash = VK_ShadowMap_HashInt( hash, pointLight ? 1 : 0 );
	// Live caster counts do not describe the resident opaque static depth.
	hash = VK_ShadowMap_HashInt( hash,
			vLight != NULL ? vLight->shadowMapTranslucentCasterCount : 0 );
	hash = VK_ShadowMap_HashInt( hash,
			vLight != NULL ? vLight->shadowMapStaticCasterCount : 0 );
	hash = VK_ShadowMap_HashInt( hash,
			vLight != NULL ? vLight->shadowMapCasterSignature : 0 );
	hash = VK_ShadowMap_HashInt( hash, cascadeCount );
	hash = VK_ShadowMap_HashInt( hash,
			r_shadowMapStableAlphaHash.GetBool() ? 1 : 0 );
	hash = VK_ShadowMap_HashFloat( hash,
			r_shadowMapPolygonFactor.GetFloat() );
	hash = VK_ShadowMap_HashFloat( hash,
			r_shadowMapPolygonOffset.GetFloat() );
	hash = VK_ShadowMap_HashInt( hash,
			idMath::ClampInt( 0, 2,
					r_shadowMapCasterCulling.GetInteger() ) );

	if ( pointLight ) {
		hash = VK_ShadowMap_HashInt( hash, resourceSize );
		// r_shadowMapPointHighPrecision selects between OpenGL's packed
		// RGBA8 and fp16 colour cubes. Vulkan always stores native depth,
		// which quantizes finer than either, so the cvar cannot change this
		// cube's contents and must not partition the cache -- hashing it
		// would discard every resident point map on an inert toggle.
		hash = VK_ShadowMap_HashInt( hash,
				r_shadowMapPointDepthCompare.GetBool() ? 1 : 0 );
		hash = VK_ShadowMap_HashFloat( hash,
				r_shadowMapPointFarScale.GetFloat() );
		hash = VK_ShadowMap_HashFloat( hash,
				R_ShadowMapPointFarDistance( vLight ) );
		if ( vLight != NULL && vLight->lightDef != NULL ) {
			for ( int i = 0 ; i < 3 ; i++ ) {
				hash = VK_ShadowMap_HashFloat( hash,
						vLight->lightDef->parms.lightCenter[ i ] );
			}
		}
	} else {
		hash = VK_ShadowMap_HashInt( hash, resourceSize );
		hash = VK_ShadowMap_HashInt( hash, atlasDiv );
		hash = VK_ShadowMap_HashFloat( hash,
				r_shadowMapProjectionPad.GetFloat() );
		hash = VK_ShadowMap_HashFloat( hash,
				r_shadowMapTexelBiasScale.GetFloat() );
		shadowMapProjectedLightState_t projectedState;
		R_BuildShadowMapProjectedLightState( vLight, viewDef,
				resourceSize, projectedState );
		hash = R_ShadowMapProjectedStateHash( hash, projectedState );
	}

	if ( vLight != NULL ) {
		for ( int i = 0 ; i < 3 ; i++ ) {
			hash = VK_ShadowMap_HashFloat( hash,
					vLight->globalLightOrigin[ i ] );
			hash = VK_ShadowMap_HashFloat( hash,
					vLight->lightRadius[ i ] );
		}
		for ( int planeIndex = 0 ; planeIndex < 4 ; planeIndex++ ) {
			for ( int component = 0 ; component < 4 ; component++ ) {
				hash = VK_ShadowMap_HashFloat( hash,
						vLight->lightProject[ planeIndex ][ component ] );
			}
		}
	}
	return hash;
}

static void VK_ShadowMap_ClearProjectedEntryMetadata(
		vkProjectedShadowCacheEntry_t &entry ) {
	entry.valid = false;
	entry.reserved = false;
	entry.generation = -1;
	entry.renderWorld = NULL;
	entry.lightIndex = -1;
	entry.signature = 0;
	entry.tileSize = 0;
	entry.lastUsedFrame = 0;
	entry.lastUpdatedFrame = 0;
}

static void VK_ShadowMap_ClearPointEntryMetadata(
		vkPointShadowCacheEntry_t &entry ) {
	entry.valid = false;
	entry.reserved = false;
	entry.generation = -1;
	entry.renderWorld = NULL;
	entry.lightIndex = -1;
	entry.signature = 0;
	entry.size = 0;
	entry.lastUsedFrame = 0;
	entry.lastUpdatedFrame = 0;
}

static void VK_ShadowMap_InvalidateLightCaches(
		const idRenderWorldLocal *renderWorld, const int lightIndex ) {
	if ( lightIndex < 0 ) {
		return;
	}
	for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
		vkProjectedShadowCacheEntry_t &projected =
				vkShadow.projectedCache[ i ];
		if ( projected.renderWorld == renderWorld
				&& projected.lightIndex == lightIndex ) {
			VK_ShadowMap_ClearProjectedEntryMetadata( projected );
		}
		vkPointShadowCacheEntry_t &point = vkShadow.pointCache[ i ];
		if ( point.renderWorld == renderWorld
				&& point.lightIndex == lightIndex ) {
			VK_ShadowMap_ClearPointEntryMetadata( point );
		}
	}
}

// Pure lookup for the read-only admission estimate: unlike
// VK_ShadowMap_FindLightHistory it never claims or evicts a slot.
static const vkShadowLightHistory_t *VK_ShadowMap_FindLightHistoryConst(
		const idRenderWorldLocal *renderWorld, const int lightIndex ) {
	if ( renderWorld == NULL || lightIndex < 0 ) {
		return NULL;
	}
	for ( int i = 0 ; i < VK_SHADOW_LIGHT_HISTORY_SLOTS ; i++ ) {
		const vkShadowLightHistory_t &history = vkShadow.lightHistory[ i ];
		if ( history.valid && history.renderWorld == renderWorld
				&& history.lightIndex == lightIndex ) {
			return &history;
		}
	}
	return NULL;
}

static vkShadowLightHistory_t *VK_ShadowMap_FindLightHistory(
		const idRenderWorldLocal *renderWorld, const int lightIndex ) {
	if ( renderWorld == NULL || lightIndex < 0 ) {
		return NULL;
	}
	vkShadowLightHistory_t *oldest = &vkShadow.lightHistory[ 0 ];
	for ( int i = 0 ; i < VK_SHADOW_LIGHT_HISTORY_SLOTS ; i++ ) {
		vkShadowLightHistory_t &history = vkShadow.lightHistory[ i ];
		if ( history.valid && history.renderWorld == renderWorld
				&& history.lightIndex == lightIndex ) {
			return &history;
		}
		if ( !history.valid ) {
			oldest = &history;
			break;
		}
		if ( history.lastTouchedFrame < oldest->lastTouchedFrame ) {
			oldest = &history;
		}
	}
	memset( oldest, 0, sizeof( *oldest ) );
	oldest->valid = true;
	oldest->renderWorld = renderWorld;
	oldest->lightIndex = lightIndex;
	oldest->lastDynamicFrame = -0x3fffffff;
	return oldest;
}

static bool VK_ShadowMap_StaticCacheable(
		const viewLight_t *vLight, const viewDef_t *viewDef,
		const vkShadowReceiverPass_t passKind, const bool pointLight,
		const int cascadeCount, const int atlasDiv ) {
	const int lightIndex = VK_ShadowMap_LightIndex( vLight );
	const idRenderWorldLocal *renderWorld =
			viewDef != NULL ? viewDef->renderWorld : NULL;
	vkShadowLightHistory_t *history =
			VK_ShadowMap_FindLightHistory( renderWorld, lightIndex );
	const bool haveDynamicCasters = vLight != NULL
			&& ( vLight->shadowMapDynamicCasterCount > 0
				|| vLight->globalShadowMapDynamicCasters != NULL
				|| vLight->localShadowMapDynamicCasters != NULL );
	// GL parity (RB_ShadowMapStaticCacheable): dynamic casters no longer
	// defeat a PROJECTED light's cache. The front end already keeps them out
	// of shadowMapCasterSignature, so the resident entry holds static depth
	// only and this view composes the dynamics over the restored tile
	// (vkShadowPassState_t::composeDynamic). The point-cube path has no
	// composition, so there dynamics still defeat the cache outright.
	//
	// Composition is a legacy-walker feature on both backends. The sealed
	// domain sets allowCacheReuse false for any pass with dynamic casters
	// (ClassicInteractionDomain.cpp), and OpenGL likewise composes only in
	// RB_ARB2_DrawInteractions, never in its shared-stream executor. Keep the
	// old conservative rule while that opt-in stream can own the view so a
	// composed hit can never fail the domain's physical reconciliation.
	const bool dynamicsDefeatCache = haveDynamicCasters
			&& ( pointLight
				|| r_rendererSharedWorldInteraction.GetBool() );
	if ( !r_shadowMapStaticCache.GetBool() || vLight == NULL
			|| renderWorld == NULL || lightIndex < 0
			|| dynamicsDefeatCache
			|| vLight->shadowMapCasterCount <= 0
			|| vLight->shadowMapStaticCasterCount <= 0
			|| vLight->shadowMapTranslucentCasterCount > 0
			|| vLight->globalTranslucentShadowMapCasters != NULL
			|| vLight->localTranslucentShadowMapCasters != NULL ) {
		if ( history != NULL && haveDynamicCasters ) {
			history->lastDynamicFrame = tr.frameCount;
			history->lastTouchedFrame = tr.frameCount;
			if ( dynamicsDefeatCache ) {
				VK_ShadowMap_InvalidateLightCaches(
						renderWorld, lightIndex );
			}
		}
		return false;
	}

	// Perforated surfaces are in the live dynamic chains, never resident depth.
	// Translucent moment maps still bypass this opaque cache. View-fitted CSM
	// reuse follows the GL gate (RB_ShadowMapStaticCacheable): a resident
	// cascade block must match the current fitted projection. The signature
	// includes that fit, and AllocateProjectedPass restores it with the tiles.
	if ( !pointLight && cascadeCount > 1
			&& !r_shadowMapCacheCSM.GetBool() ) {
		return false;
	}
	(void)atlasDiv;
	if ( passKind == VK_SHADOW_RECEIVER_LOCAL
			&& vLight->globalShadowMapCasters == NULL ) {
		return false;
	}
	if ( passKind == VK_SHADOW_RECEIVER_GLOBAL
			&& vLight->globalShadowMapCasters == NULL
			&& vLight->localShadowMapCasters == NULL ) {
		return false;
	}

	if ( history != NULL ) {
		history->lastTouchedFrame = tr.frameCount;
		// Only the point path bakes dynamics into the cached content, so only
		// it must wait out the hysteresis after they disappear.
		if ( pointLight
				&& tr.frameCount - history->lastDynamicFrame
					< Max( 0,
							r_shadowMapStaticHysteresisFrames.GetInteger() ) ) {
			return false;
		}
	}
	return true;
}

static int VK_ShadowMap_ProjectedCacheSlotLimit( void ) {
	return idMath::ClampInt( 0, VK_SHADOW_MAX_CACHE_SLOTS,
			r_shadowMapProjectedCacheSize.GetInteger() );
}

static int VK_ShadowMap_PointCacheSlotLimit( void ) {
	return idMath::ClampInt( 0, VK_SHADOW_MAX_CACHE_SLOTS,
			r_shadowMapPointCacheSize.GetInteger() );
}

static void VK_ShadowMap_BeginCacheView( const viewDef_t *viewDef ) {
	const idRenderWorldLocal *renderWorld =
			viewDef != NULL ? viewDef->renderWorld : NULL;
	const unsigned int mapFileCRC =
			renderWorld != NULL ? renderWorld->mapFileCRC : 0;
	const int mapNameHash = VK_ShadowMap_MapNameHash( viewDef );
	const int projectedLimit = VK_ShadowMap_ProjectedCacheSlotLimit();
	const int pointLimit = VK_ShadowMap_PointCacheSlotLimit();
	const int residentFrames =
			Max( 1, r_shadowMapResidentFrames.GetInteger() );

	if ( vkShadow.cacheRenderWorld != renderWorld
			|| vkShadow.cacheMapFileCRC != mapFileCRC
			|| vkShadow.cacheMapNameHash != mapNameHash ) {
		for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
			VK_ShadowMap_ClearProjectedEntryMetadata(
					vkShadow.projectedCache[ i ] );
			VK_ShadowMap_ClearPointEntryMetadata(
					vkShadow.pointCache[ i ] );
		}
		memset( vkShadow.lightHistory, 0,
				sizeof( vkShadow.lightHistory ) );
		vkShadow.cacheRenderWorld = renderWorld;
		vkShadow.cacheMapFileCRC = mapFileCRC;
		vkShadow.cacheMapNameHash = mapNameHash;
	}

	for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
		vkProjectedShadowCacheEntry_t &projected =
				vkShadow.projectedCache[ i ];
		projected.reserved = false;
		if ( i >= projectedLimit
				|| projected.generation != tr.videoRestartCount
				|| ( projected.valid
					&& tr.frameCount - projected.lastUsedFrame
						> residentFrames ) ) {
			VK_ShadowMap_ClearProjectedEntryMetadata( projected );
		}

		vkPointShadowCacheEntry_t &point = vkShadow.pointCache[ i ];
		point.reserved = false;
		if ( i >= pointLimit
				|| point.generation != tr.videoRestartCount
				|| ( point.valid
					&& tr.frameCount - point.lastUsedFrame
						> residentFrames ) ) {
			VK_ShadowMap_ClearPointEntryMetadata( point );
		}
	}
}

static bool VK_ShadowMap_EnsureProjectedCacheConfiguration(
		const int tileSize ) {
	if ( tileSize <= 0 || vkCtx.device == VK_NULL_HANDLE ) {
		return false;
	}
	if ( vkShadow.projectedCacheTileSize != 0
			&& vkShadow.projectedCacheTileSize != tileSize ) {
		vkDeviceWaitIdle( vkCtx.device );
		VK_ShadowMap_DestroyProjectedCaches();
	}
	vkShadow.projectedCacheTileSize = tileSize;
	return true;
}

// blockSize is the resident content's edge: one tile for an ordinary
// projected light, tileSize * atlasDiv for a cached CSM block. A slot reused
// at a different block edge retires its image first; the device is idled
// because frames in flight may still reference it.
static bool VK_ShadowMap_EnsureProjectedCacheImage( const int index,
		const int blockSize ) {
	if ( index < 0 || index >= VK_SHADOW_MAX_CACHE_SLOTS
			|| vkShadow.projectedCacheTileSize <= 0 || blockSize <= 0 ) {
		return false;
	}
	vkProjectedShadowCacheEntry_t &entry =
			vkShadow.projectedCache[ index ];
	if ( entry.image != VK_NULL_HANDLE
			&& entry.blockSize != blockSize ) {
		vkDeviceWaitIdle( vkCtx.device );
		vmaDestroyImage( vkCtx.allocator, entry.image,
				entry.allocation );
		entry.image = VK_NULL_HANDLE;
		entry.allocation = NULL;
		entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_ShadowMap_ClearProjectedEntryMetadata( entry );
	}
	if ( entry.image != VK_NULL_HANDLE ) {
		return true;
	}

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = vkCtx.shadowDepthFormat;
	ici.extent.width = (uint32_t)blockSize;
	ici.extent.height = (uint32_t)blockSize;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci,
			&entry.image, &entry.allocation, NULL ) != VK_SUCCESS ) {
		entry.image = VK_NULL_HANDLE;
		entry.allocation = NULL;
		return false;
	}
	entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	entry.blockSize = blockSize;
	return true;
}

static int VK_ShadowMap_FindProjectedCacheEntry(
		const idRenderWorldLocal *renderWorld, const int lightIndex,
		const vkShadowReceiverPass_t passKind, const int signature,
		const int tileSize, const int blockSize ) {
	const int limit = VK_ShadowMap_ProjectedCacheSlotLimit();
	for ( int i = 0 ; i < limit ; i++ ) {
		vkProjectedShadowCacheEntry_t &entry =
				vkShadow.projectedCache[ i ];
		if ( entry.valid && !entry.reserved
				&& entry.generation == tr.videoRestartCount
				&& entry.renderWorld == renderWorld
				&& entry.lightIndex == lightIndex
				&& entry.passKind == passKind
				&& entry.signature == signature
				&& entry.tileSize == tileSize
				&& entry.blockSize == blockSize
				&& entry.image != VK_NULL_HANDLE
				&& entry.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
			entry.lastUsedFrame = tr.frameCount;
			entry.reserved = true;
			return i;
		}
	}
	return -1;
}

static int VK_ShadowMap_FindPointCacheEntry(
		const idRenderWorldLocal *renderWorld, const int lightIndex,
		const vkShadowReceiverPass_t passKind, const int signature,
		const int size ) {
	const int limit = VK_ShadowMap_PointCacheSlotLimit();
	for ( int i = 0 ; i < limit ; i++ ) {
		vkPointShadowCacheEntry_t &entry = vkShadow.pointCache[ i ];
		if ( entry.valid && !entry.reserved
				&& entry.generation == tr.videoRestartCount
				&& entry.renderWorld == renderWorld
				&& entry.lightIndex == lightIndex
				&& entry.passKind == passKind
				&& entry.signature == signature
				&& entry.size == size
				&& entry.cube.image != VK_NULL_HANDLE
				&& entry.cube.layout
					== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ) {
			entry.lastUsedFrame = tr.frameCount;
			entry.reserved = true;
			return i;
		}
	}
	return -1;
}

static int VK_ShadowMap_AllocProjectedCacheEntry(
		const int tileSize, const int blockSize ) {
	const int limit = VK_ShadowMap_ProjectedCacheSlotLimit();
	if ( limit <= 0
			|| !VK_ShadowMap_EnsureProjectedCacheConfiguration(
					tileSize ) ) {
		return -1;
	}
	int selected = -1;
	for ( int i = 0 ; i < limit ; i++ ) {
		const vkProjectedShadowCacheEntry_t &entry =
				vkShadow.projectedCache[ i ];
		if ( entry.reserved ) {
			continue;
		}
		if ( !entry.valid ) {
			selected = i;
			break;
		}
		if ( selected < 0 || entry.lastUsedFrame
				< vkShadow.projectedCache[ selected ].lastUsedFrame ) {
			selected = i;
		}
	}
	if ( selected < 0
			|| !VK_ShadowMap_EnsureProjectedCacheImage( selected,
					blockSize ) ) {
		return -1;
	}
	VK_ShadowMap_ClearProjectedEntryMetadata(
			vkShadow.projectedCache[ selected ] );
	vkShadow.projectedCache[ selected ].blockSize = blockSize;
	vkShadow.projectedCache[ selected ].reserved = true;
	return selected;
}

static int VK_ShadowMap_AllocPointCacheEntry( void ) {
	const int limit = VK_ShadowMap_PointCacheSlotLimit();
	if ( limit <= 0 ) {
		return -1;
	}
	int selected = -1;
	for ( int i = 0 ; i < limit ; i++ ) {
		const vkPointShadowCacheEntry_t &entry =
				vkShadow.pointCache[ i ];
		if ( entry.reserved ) {
			continue;
		}
		if ( !entry.valid ) {
			selected = i;
			break;
		}
		if ( selected < 0 || entry.lastUsedFrame
				< vkShadow.pointCache[ selected ].lastUsedFrame ) {
			selected = i;
		}
	}
	if ( selected < 0
			|| !VK_ShadowMap_EnsurePointCacheCube( selected ) ) {
		return -1;
	}
	VK_ShadowMap_ClearPointEntryMetadata(
			vkShadow.pointCache[ selected ] );
	vkShadow.pointCache[ selected ].reserved = true;
	return selected;
}

/*
====================
Importance-ordered update admission (RB_ShadowMapBuildUpdateAdmissions)

r_shadowMapMaxUpdatesPerView caps fresh renders per backend view. Spending
that cap in view-light-list order lets an off-screen light behind the camera
consume the budget a large, stale, on-screen one needed, and the loser keeps
a visibly wrong map. Score every candidate first, then admit greedily.

Everything here is read-only: the estimate must not reserve a cache slot, age
a light's history, or invalidate an entry, because the real scheduling walk
runs afterwards and would then see state it did not create.
====================
*/

static const int VK_SHADOW_MAX_ADMITTED_LIGHTS = VK_SHADOW_MAX_LIGHTS;
static int vkShadowAdmittedLightIndexes[ VK_SHADOW_MAX_ADMITTED_LIGHTS ];
static int vkShadowAdmittedLightCount = 0;
static bool vkShadowAdmissionsActive = false;

static bool VK_ShadowMap_UpdateAdmitted( const int lightIndex ) {
	for ( int i = 0 ; i < vkShadowAdmittedLightCount ; i++ ) {
		if ( vkShadowAdmittedLightIndexes[ i ] == lightIndex ) {
			return true;
		}
	}
	return false;
}

// VK_ShadowMap_StaticCacheable without its history and invalidation side
// effects (RB_ShadowMapStaticCacheableReadOnly parity).
static bool VK_ShadowMap_StaticCacheableReadOnly(
		const viewLight_t *vLight, const viewDef_t *viewDef,
		const bool pointLight, const int cascadeCount ) {
	const idRenderWorldLocal *renderWorld =
			viewDef != NULL ? viewDef->renderWorld : NULL;
	const int lightIndex = VK_ShadowMap_LightIndex( vLight );
	if ( !r_shadowMapStaticCache.GetBool() || vLight == NULL
			|| renderWorld == NULL || lightIndex < 0
			|| vLight->shadowMapCasterCount <= 0
			|| vLight->shadowMapStaticCasterCount <= 0
			|| vLight->shadowMapTranslucentCasterCount > 0
			|| vLight->globalTranslucentShadowMapCasters != NULL
			|| vLight->localTranslucentShadowMapCasters != NULL ) {
		return false;
	}
	const bool haveDynamicCasters =
			vLight->shadowMapDynamicCasterCount > 0
			|| vLight->globalShadowMapDynamicCasters != NULL
			|| vLight->localShadowMapDynamicCasters != NULL;
	if ( haveDynamicCasters
			&& ( pointLight
				|| r_rendererSharedWorldInteraction.GetBool() ) ) {
		return false;
	}
	if ( !pointLight && cascadeCount > 1
			&& !r_shadowMapCacheCSM.GetBool() ) {
		return false;
	}
	const vkShadowLightHistory_t *history =
			VK_ShadowMap_FindLightHistoryConst( renderWorld, lightIndex );
	if ( history != NULL && pointLight
			&& tr.frameCount - history->lastDynamicFrame
				< Max( 0,
						r_shadowMapStaticHysteresisFrames.GetInteger() ) ) {
		return false;
	}
	return true;
}

static vkShadowSchedule_t VK_ShadowMap_SchedulePass(
		const viewLight_t *vLight, const viewDef_t *viewDef,
		const vkShadowReceiverPass_t requestedPass,
		const bool stencilFallbackAvailable,
		const bool pointLight, const int resourceSize,
		const int atlasDiv, const int cascadeCount ) {
	vkShadowSchedule_t schedule;
	memset( &schedule, 0, sizeof( schedule ) );
	schedule.action = VK_SHADOW_SCHEDULE_UPDATE;
	schedule.cacheEntry = -1;
	schedule.cachePassKind =
			VK_ShadowMap_CachePassKind( vLight, requestedPass );
	schedule.signature = VK_ShadowMap_BuildPassSignatureForView(
			vLight, viewDef, schedule.cachePassKind, pointLight,
			resourceSize, atlasDiv, cascadeCount );
	const int receiverMask =
			requestedPass == VK_SHADOW_RECEIVER_LOCAL
				? SHADOWMAP_RECEIVER_MASK_LOCAL
				: SHADOWMAP_RECEIVER_MASK_GLOBAL;
	const int incompleteStencilMask =
			vLight->shadowMapIncompleteStencilMask |
			( vLight->shadowMapPrelightStencilRequiredMask
				& ~vLight->shadowMapPrelightStencilReadyMask );
	const bool mapRequiredForCorrectness =
			!stencilFallbackAvailable ||
			( incompleteStencilMask & receiverMask ) != 0;
	const int subviewPolicy = idMath::ClampInt( 0, 2,
			r_shadowMapSubviewPolicy.GetInteger() );
	if ( viewDef != NULL && viewDef->isSubview
			&& subviewPolicy >= 2
			&& !mapRequiredForCorrectness ) {
		schedule.action = VK_SHADOW_SCHEDULE_FALLBACK;
		vkShadow.subviewFallbacks++;
		return schedule;
	}
	schedule.cacheable = VK_ShadowMap_StaticCacheable(
			vLight, viewDef, schedule.cachePassKind, pointLight,
			cascadeCount, atlasDiv );
	// A CSM light's resident content is the whole contiguous cascade block,
	// not one tile; ordinary projected lights keep atlasDiv 1 and are
	// unaffected.
	const int projectedBlockSize = resourceSize
			* idMath::ClampInt( 1, 2, atlasDiv );

	const idRenderWorldLocal *renderWorld =
			viewDef != NULL ? viewDef->renderWorld : NULL;
	const int lightIndex = VK_ShadowMap_LightIndex( vLight );
	if ( schedule.cacheable ) {
		schedule.cacheEntry = pointLight
				? VK_ShadowMap_FindPointCacheEntry(
						renderWorld, lightIndex,
						schedule.cachePassKind,
						schedule.signature, resourceSize )
				: VK_ShadowMap_FindProjectedCacheEntry(
						renderWorld, lightIndex,
						schedule.cachePassKind,
						schedule.signature, resourceSize,
						projectedBlockSize );
		if ( schedule.cacheEntry >= 0 ) {
			schedule.action = VK_SHADOW_SCHEDULE_REUSE;
			if ( pointLight ) {
				vkShadow.pointCacheHits++;
			} else {
				vkShadow.projectedCacheHits++;
			}
			return schedule;
		}
	}

	if ( viewDef != NULL && viewDef->isSubview
			&& subviewPolicy >= 1
			&& !mapRequiredForCorrectness ) {
		schedule.action = VK_SHADOW_SCHEDULE_FALLBACK;
		schedule.cacheEntry = -1;
		vkShadow.subviewFallbacks++;
		return schedule;
	}
	// Importance ordering decides WHICH lights spend a limited budget; the
	// running count still decides when it is gone.
	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	const bool admissionDenied = updateBudget > 0
			&& vkShadowAdmissionsActive
			&& !VK_ShadowMap_UpdateAdmitted( lightIndex );
	if ( updateBudget > 0
			&& ( vkShadow.freshUpdates >= updateBudget || admissionDenied )
			&& !mapRequiredForCorrectness ) {
		schedule.action = VK_SHADOW_SCHEDULE_FALLBACK;
		schedule.cacheEntry = -1;
		vkShadow.budgetFallbacks++;
		if ( admissionDenied ) {
			// separable from an exhausted budget: this light lost the
			// ordering, it did not merely arrive late
			vkShadow.admissionDenied++;
		}
		return schedule;
	}

	// The budget counts ownership maps that will actually be rendered, not
	// lights and not exact resident hits.
	vkShadow.freshUpdates++;
	if ( pointLight ) {
		vkShadow.pointFreshUpdates++;
	} else {
		vkShadow.projectedFreshUpdates++;
	}
	if ( schedule.cacheable ) {
		schedule.cacheEntry = pointLight
				? VK_ShadowMap_AllocPointCacheEntry()
				: VK_ShadowMap_AllocProjectedCacheEntry(
						resourceSize, projectedBlockSize );
		// Cache allocation is optional. Failure remains a normal fresh
		// scratch/atlas update and never bypasses shadowing.
	}
	return schedule;
}

static void VK_ShadowMap_CancelScheduledPass(
		const vkShadowSchedule_t &schedule, const bool pointLight ) {
	if ( schedule.cacheEntry >= 0
			&& schedule.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS ) {
		if ( pointLight ) {
			vkShadow.pointCache[ schedule.cacheEntry ].reserved = false;
		} else {
			vkShadow.projectedCache[ schedule.cacheEntry ].reserved = false;
		}
	}

	if ( schedule.action == VK_SHADOW_SCHEDULE_UPDATE ) {
		// Scheduling reserves update admission before allocating the concrete
		// atlas/cube resource. Failed allocation must not consume admission
		// that a later ownership map can still use.
		vkShadow.freshUpdates = Max( 0, vkShadow.freshUpdates - 1 );
		if ( pointLight ) {
			vkShadow.pointFreshUpdates =
					Max( 0, vkShadow.pointFreshUpdates - 1 );
		} else {
			vkShadow.projectedFreshUpdates =
					Max( 0, vkShadow.projectedFreshUpdates - 1 );
		}
	} else if ( schedule.action == VK_SHADOW_SCHEDULE_REUSE ) {
		// A reserved exact hit is only a realized hit after its live
		// descriptor/tile allocation succeeds.
		if ( pointLight ) {
			vkShadow.pointCacheHits =
					Max( 0, vkShadow.pointCacheHits - 1 );
		} else {
			vkShadow.projectedCacheHits =
					Max( 0, vkShadow.projectedCacheHits - 1 );
		}
	}
}

/*
====================
Tile allocator (row scan, reset per view)
====================
*/
static bool VK_ShadowMap_AllocTileBlock( const int blockSize,
		int &tileX, int &tileY ) {
	if ( blockSize <= 0 || blockSize > vkShadow.atlasSize ) {
		return false;
	}
	if ( vkShadow.nextTileX + blockSize > vkShadow.atlasSize ) {
		vkShadow.nextTileX = 0;
		vkShadow.nextTileY += vkShadow.nextTileRowHeight;
		vkShadow.nextTileRowHeight = 0;
	}
	if ( vkShadow.nextTileY + blockSize > vkShadow.atlasSize ) {
		return false;
	}
	tileX = vkShadow.nextTileX;
	tileY = vkShadow.nextTileY;
	vkShadow.nextTileX += blockSize;
	vkShadow.nextTileRowHeight = Max( vkShadow.nextTileRowHeight, blockSize );
	return true;
}

static bool VK_ShadowMap_PassHasCasters( const viewLight_t *vLight,
		const vkShadowReceiverPass_t receiverPass ) {
	if ( vLight->globalShadowMapCasters != NULL
			|| vLight->globalShadowMapDynamicCasters != NULL ) {
		return true;
	}
	return receiverPass == VK_SHADOW_RECEIVER_GLOBAL
		&& ( vLight->localShadowMapCasters != NULL
			|| vLight->localShadowMapDynamicCasters != NULL );
}

// A receiver ownership's dynamic caster chains, in the same LOCAL/GLOBAL
// split VK_ShadowMap_PassHasCasters uses: LOCAL maps contain global casters,
// GLOBAL maps contain global + local casters.
// The physical edge of a projected light's resident/atlas content: one tile
// for an ordinary projector, the contiguous atlasDiv^2 cascade block for CSM.
static int VK_ShadowMap_ProjectedBlockSize(
		const vkShadowLightState_t &light ) {
	return light.tileSize
			* idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );
}

static bool VK_ShadowMap_PassHasDynamicCasters( const viewLight_t *vLight,
		const vkShadowReceiverPass_t receiverPass ) {
	if ( vLight == NULL ) {
		return false;
	}
	if ( vLight->globalShadowMapDynamicCasters != NULL ) {
		return true;
	}
	return receiverPass == VK_SHADOW_RECEIVER_GLOBAL
		&& vLight->localShadowMapDynamicCasters != NULL;
}

static int VK_ShadowMap_ReceiverMask(
		const vkShadowReceiverPass_t receiverPass ) {
	return receiverPass == VK_SHADOW_RECEIVER_LOCAL
		? SHADOWMAP_RECEIVER_MASK_LOCAL
		: SHADOWMAP_RECEIVER_MASK_GLOBAL;
}

static bool VK_ShadowMap_MapOrHybridOwnershipComplete(
		const viewLight_t *vLight,
		const vkShadowReceiverPass_t receiverPass ) {
	if ( vLight == NULL ) {
		return false;
	}
	const int receiverMask = VK_ShadowMap_ReceiverMask( receiverPass );
	const int incompleteMapMask =
		vLight->shadowMapIncompleteMapMask |
		vLight->shadowMapPrelightMapMissingMask;
	if ( ( incompleteMapMask & receiverMask ) == 0 ) {
		// A complete map needs no stencil supplement, so an independently
		// unavailable prelight volume cannot invalidate it.
		return true;
	}

	// The optimized prelight is one combined volume containing both mapped
	// and map-missing world casters. It cannot be used as a supplement without
	// re-applying mapped casters as hard stencil silhouettes, so force the
	// ownership to the complete-stencil fallback whenever a prelight caster
	// is absent from the map.
	const int hybridIncompleteMask =
		vLight->shadowMapHybridIncompleteMask |
		vLight->shadowMapPrelightMapMissingMask;
	if ( ( hybridIncompleteMask & receiverMask ) != 0 ) {
		if ( vLight->lightDef == NULL ||
				!vLight->lightDef->shadowMapStencilFallbackSticky ) {
			common->DPrintf(
				"shadow map ownership incomplete for lightDef %d pass %s "
				"(map=%d stencil=%d hybrid=%d prelightMissing=%d "
				"globalSupplement=%d localSupplement=%d)\n",
				vLight->lightDef != NULL ? vLight->lightDef->index : -1,
				receiverPass == VK_SHADOW_RECEIVER_LOCAL ? "LOCAL" : "GLOBAL",
				vLight->shadowMapIncompleteMapMask,
				vLight->shadowMapIncompleteStencilMask,
				vLight->shadowMapHybridIncompleteMask,
				vLight->shadowMapPrelightMapMissingMask,
				vLight->globalShadowMapStencilSupplements != NULL ? 1 : 0,
				vLight->localShadowMapStencilSupplements != NULL ? 1 : 0 );
		}
		return false;
	}

	// A partial map is only valid when the frontend actually linked the
	// ownership-specific stencil volumes. LOCAL receivers see global casters;
	// GLOBAL receivers additionally see noSelfShadow/local casters.
	const bool hasSupplement =
		vLight->globalShadowMapStencilSupplements != NULL ||
		( receiverPass == VK_SHADOW_RECEIVER_GLOBAL &&
			vLight->localShadowMapStencilSupplements != NULL );
	if ( !hasSupplement &&
			( vLight->lightDef == NULL ||
				!vLight->lightDef->shadowMapStencilFallbackSticky ) ) {
		common->DPrintf(
			"shadow map ownership has no stencil supplement for lightDef %d "
			"pass %s (map=%d)\n",
			vLight->lightDef != NULL ? vLight->lightDef->index : -1,
			receiverPass == VK_SHADOW_RECEIVER_LOCAL ? "LOCAL" : "GLOBAL",
			incompleteMapMask );
	}
	return hasSupplement;
}

static bool VK_ShadowMap_HasLocalCasters( const viewLight_t *vLight ) {
	return vLight->localShadowMapCasters != NULL
		|| vLight->localShadowMapDynamicCasters != NULL
		|| vLight->localTranslucentShadowMapCasters != NULL;
}

static bool VK_ShadowMap_AllocateProjectedPass( vkShadowLightState_t &light,
		const vkShadowReceiverPass_t receiverPass,
		const vkShadowSchedule_t &schedule ) {
	if ( schedule.action == VK_SHADOW_SCHEDULE_REUSE ) {
		if ( schedule.cacheEntry < 0
				|| schedule.cacheEntry
					>= VK_SHADOW_MAX_CACHE_SLOTS ) {
			VK_ShadowMap_CancelScheduledPass( schedule, false );
			return false;
		}
		const vkProjectedShadowCacheEntry_t &cache =
				vkShadow.projectedCache[ schedule.cacheEntry ];
		if ( !cache.valid ) {
			VK_ShadowMap_CancelScheduledPass( schedule, false );
			return false;
		}
		light.projectedState = cache.projectedState;
		light.tileSize = cache.tileSize;
	}

	int tileX = 0;
	int tileY = 0;
	const int atlasDiv = idMath::ClampInt( 1, 2,
			light.projectedState.atlasDiv );
	const int blockSize = light.tileSize * atlasDiv;
	if ( !VK_ShadowMap_AllocTileBlock( blockSize, tileX, tileY ) ) {
		VK_ShadowMap_CancelScheduledPass( schedule, false );
		return false;
	}

	vkShadowPassState_t &pass = light.passes[ receiverPass ];
	memset( &pass, 0, sizeof( pass ) );
	pass.valid = true;
	pass.resourcePass = receiverPass;
	pass.cacheHit =
			schedule.action == VK_SHADOW_SCHEDULE_REUSE;
	pass.cacheUpdate =
			schedule.action == VK_SHADOW_SCHEDULE_UPDATE
			&& schedule.cacheEntry >= 0;
	pass.cacheEntry = schedule.cacheEntry;
	pass.cacheSignature = schedule.signature;
	// Cached projected content is static-only. A pass that publishes or
	// restores such an entry must draw this view's dynamic casters over the
	// tile afterwards; an uncached scratch pass still draws every chain in
	// one go.
	//
	// This can only become true when VK_ShadowMap_StaticCacheable admitted a
	// light that has dynamic casters, which its dynamicsDefeatCache term
	// permits for projected lights only while the sealed shared stream is
	// off. That coupling is load-bearing: the sealed stream draws each pass's
	// complete caster plan and never composes, so a composed pass reaching it
	// would publish dynamics into a tile later reused as static content.
	pass.composeDynamic = ( pass.cacheHit || pass.cacheUpdate )
			&& VK_ShadowMap_PassHasDynamicCasters( light.vLight,
					receiverPass );
	pass.cubeIndex = -1;
	pass.tileX = tileX;
	pass.tileY = tileY;

	// Every cascade tile uses a negative-height viewport (GL winding
	// parity), so tile-local v=0 lands on the tile's bottom image row.
	const float invAtlas = light.invAtlasSize[ 0 ];
	const int cascadeCount = idMath::ClampInt( 1,
			SHADOWMAP_PROJECTED_MAX_CASCADES,
			light.projectedState.cascadeCount );
	for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ ) {
		const int cascadeX = cascadeIndex % atlasDiv;
		const int cascadeY = cascadeIndex / atlasDiv;
		const int cascadeTileX = tileX + cascadeX * light.tileSize;
		const int cascadeTileY = tileY + cascadeY * light.tileSize;
		pass.atlasRects[ cascadeIndex ][ 0 ] =
				(float)cascadeTileX * invAtlas;
		pass.atlasRects[ cascadeIndex ][ 1 ] =
				(float)( cascadeTileY + light.tileSize ) * invAtlas;
		pass.atlasRects[ cascadeIndex ][ 2 ] =
				(float)( cascadeTileX + light.tileSize ) * invAtlas;
		pass.atlasRects[ cascadeIndex ][ 3 ] =
				(float)cascadeTileY * invAtlas;
	}
	return true;
}

static bool VK_ShadowMap_AllocatePointPass( vkShadowLightState_t &light,
		const vkShadowReceiverPass_t receiverPass,
		const vkShadowSchedule_t &schedule ) {
	vkShadowPassState_t &pass = light.passes[ receiverPass ];
	memset( &pass, 0, sizeof( pass ) );
	pass.cacheEntry = -1;
	pass.cubeIndex = -1;
	const int frameSlot = VK_Exec_ActiveFrameSlot();
	if ( frameSlot < 0 || frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		VK_ShadowMap_CancelScheduledPass( schedule, true );
		return false;
	}

	if ( schedule.action == VK_SHADOW_SCHEDULE_REUSE ) {
		if ( schedule.cacheEntry < 0
				|| schedule.cacheEntry >= VK_SHADOW_MAX_CACHE_SLOTS ) {
			VK_ShadowMap_CancelScheduledPass( schedule, true );
			return false;
		}
		const vkPointShadowCacheEntry_t &cache =
				vkShadow.pointCache[ schedule.cacheEntry ];
		const VkDescriptorSet pointSet = cache.cube.sets[ frameSlot ];
		if ( !cache.valid || pointSet == VK_NULL_HANDLE ) {
			VK_ShadowMap_CancelScheduledPass( schedule, true );
			return false;
		}
		pass.valid = true;
		pass.resourcePass = receiverPass;
		pass.cacheHit = true;
		pass.cacheEntry = schedule.cacheEntry;
		pass.cacheSignature = schedule.signature;
		pass.pointSet = pointSet;
		light.tileSize = cache.size;
		light.pointFar = cache.pointFar;
		for ( int i = 0 ; i < 3 ; i++ ) {
			light.pointLightOrigin[ i ] = cache.lightOrigin[ i ];
		}
		return true;
	}

	if ( schedule.cacheEntry >= 0 ) {
		vkPointShadowCacheEntry_t &cache =
				vkShadow.pointCache[ schedule.cacheEntry ];
		const VkDescriptorSet pointSet = cache.cube.sets[ frameSlot ];
		if ( pointSet != VK_NULL_HANDLE ) {
			pass.valid = true;
			pass.resourcePass = receiverPass;
			pass.cacheUpdate = true;
			pass.cacheEntry = schedule.cacheEntry;
			pass.cacheSignature = schedule.signature;
			pass.pointSet = pointSet;
			light.tileSize = vkShadow.pointCubeFaceSize;
			return true;
		}
		cache.reserved = false;
	}

	if ( vkShadow.pointCubesUsed >= VK_SHADOW_MAX_POINT_CUBES
			|| !VK_ShadowMap_EnsurePointCube(
					vkShadow.pointCubesUsed ) ) {
		VK_ShadowMap_CancelScheduledPass( schedule, true );
		return false;
	}
	const int cubeIndex = vkShadow.pointCubesUsed;
	const VkDescriptorSet pointSet =
			vkShadow.pointCubes[ cubeIndex ].sets[ frameSlot ];
	if ( pointSet == VK_NULL_HANDLE ) {
		VK_ShadowMap_CancelScheduledPass( schedule, true );
		return false;
	}
	pass.valid = true;
	pass.resourcePass = receiverPass;
	pass.cacheSignature = schedule.signature;
	pass.cubeIndex = cubeIndex;
	pass.pointSet = pointSet;
	vkShadow.pointCubesUsed++;
	light.tileSize = vkShadow.pointCubeFaceSize;
	return true;
}

static void VK_ShadowMap_AliasPass( vkShadowLightState_t &light,
		const vkShadowReceiverPass_t receiverPass,
		const vkShadowReceiverPass_t resourcePass ) {
	light.passes[ receiverPass ] = light.passes[ resourcePass ];
	light.passes[ receiverPass ].resourcePass = resourcePass;
}

// Approximate resident shadow GPU memory (RB_ShadowMapResidentBytes
// parity): the atlas, every created resident projected image, and every
// created cube. The cost was otherwise invisible, and only a vid_restart
// ever reclaimed it.
static double VK_ShadowMap_DepthBytesPerPixel( void ) {
	switch ( vkCtx.shadowDepthFormat ) {
	case VK_FORMAT_D16_UNORM:
		return 2.0;
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return 4.0;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return 8.0;
	default:
		return 4.0;
	}
}

static double VK_ShadowMap_ResidentBytes( void ) {
	const double bytesPerPixel = VK_ShadowMap_DepthBytesPerPixel();
	double bytes = 0.0;
	if ( vkShadow.atlasImage != VK_NULL_HANDLE ) {
		bytes += (double)vkShadow.atlasSize * (double)vkShadow.atlasSize
				* bytesPerPixel;
	}
	for ( int i = 0 ; i < VK_SHADOW_MAX_CACHE_SLOTS ; i++ ) {
		const vkProjectedShadowCacheEntry_t &projected =
				vkShadow.projectedCache[ i ];
		if ( projected.image != VK_NULL_HANDLE ) {
			bytes += (double)projected.blockSize
					* (double)projected.blockSize * bytesPerPixel;
		}
		if ( vkShadow.pointCache[ i ].cube.image != VK_NULL_HANDLE ) {
			bytes += (double)vkShadow.pointCubeFaceSize
					* (double)vkShadow.pointCubeFaceSize * bytesPerPixel
					* 6.0;
		}
	}
	for ( int i = 0 ; i < VK_SHADOW_MAX_POINT_CUBES ; i++ ) {
		if ( vkShadow.pointCubes[ i ].image != VK_NULL_HANDLE ) {
			bytes += (double)vkShadow.pointCubeFaceSize
					* (double)vkShadow.pointCubeFaceSize * bytesPerPixel
					* 6.0;
		}
	}
	return bytes;
}

// r_shadowMapReport 2: one line per admitted light, naming the decision each
// receiver ownership reached. The view-level counters say how much happened;
// this says which light it happened to.
static void VK_ShadowMap_ReportViewLights( void ) {
	if ( r_shadowMapReport.GetInteger() < 2 ) {
		return;
	}
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		const vkShadowLightState_t &light = vkShadow.lights[ i ];
		const viewLight_t *vLight = light.vLight;
		if ( vLight == NULL ) {
			continue;
		}
		const shadowMapLightClassification_t classification =
				R_ClassifyShadowMapLight( vLight );
		const char *outcome[ VK_SHADOW_RECEIVER_PASS_COUNT ];
		for ( int passIndex = 0 ;
				passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
			const vkShadowPassState_t &pass = light.passes[ passIndex ];
			if ( !pass.valid ) {
				const bool receivers = passIndex == VK_SHADOW_RECEIVER_LOCAL
					? vLight->localInteractions != NULL
					: ( vLight->globalInteractions != NULL
						|| ( r_shadowMapTranslucentReceivers.GetBool()
							&& vLight->translucentInteractions != NULL ) );
				// This is map preparation, before the receiver decides whether
				// stencil is usable. Do not report a stencil draw for an unused
				// ownership or for an unresolved resource failure.
				outcome[ passIndex ] = receivers ? "unmapped" : "unused";
			} else if ( pass.resourcePass
					!= (vkShadowReceiverPass_t)passIndex ) {
				outcome[ passIndex ] = "alias";
			} else if ( pass.cacheHit ) {
				outcome[ passIndex ] = pass.composeDynamic
						? "reuse+compose" : "reuse";
			} else if ( pass.cacheUpdate ) {
				outcome[ passIndex ] = pass.composeDynamic
						? "publish+compose" : "publish";
			} else {
				outcome[ passIndex ] = "scratch";
			}
		}
		common->Printf(
				"SM pass light[%d] '%s' class=%s type=%s csm=%d cascades=%d atlasDiv=%d tile=%d LOCAL=%s GLOBAL=%s casters(static=%d dynamic=%d alpha=%d) receivers(local=%d global=%d translucent=%d)\n",
				vLight->lightDef != NULL ? vLight->lightDef->index : -1,
				vLight->lightShader != NULL
						? vLight->lightShader->GetName() : "<null>",
				R_ShadowMapLightClassName( classification.lightClass ),
				light.pointLight ? "point" : "projected",
				classification.csmEnabled ? 1 : 0,
				light.pointLight ? 1 : light.projectedState.cascadeCount,
				light.pointLight ? 1 : light.projectedState.atlasDiv,
				light.tileSize,
				outcome[ VK_SHADOW_RECEIVER_LOCAL ],
				outcome[ VK_SHADOW_RECEIVER_GLOBAL ],
				vLight->shadowMapStaticCasterCount,
				vLight->shadowMapDynamicCasterCount,
				vLight->shadowMapAlphaCasterCount,
				vLight->localInteractions != NULL ? 1 : 0,
				vLight->globalInteractions != NULL ? 1 : 0,
				vLight->translucentInteractions != NULL ? 1 : 0 );
	}
}

static void VK_ShadowMap_ReportViewCache( const viewDef_t *viewDef ) {
	if ( r_shadowMapReport.GetInteger() < 1 ) {
		return;
	}
	const int interval =
			Max( 1, r_shadowMapReportInterval.GetInteger() );
	if ( tr.frameCount % interval != 0 ) {
		return;
	}
	int projectedSlots = 0;
	int pointSlots = 0;
	const int projectedLimit =
			VK_ShadowMap_ProjectedCacheSlotLimit();
	const int pointLimit = VK_ShadowMap_PointCacheSlotLimit();
	for ( int i = 0 ; i < projectedLimit ; i++ ) {
		projectedSlots +=
				( vkShadow.projectedCache[ i ].valid
					|| vkShadow.projectedCache[ i ].reserved )
				? 1 : 0;
	}
	for ( int i = 0 ; i < pointLimit ; i++ ) {
		pointSlots += ( vkShadow.pointCache[ i ].valid
				|| vkShadow.pointCache[ i ].reserved ) ? 1 : 0;
	}
	common->Printf(
			"Vulkan shadow cache: view=%s exact=%d/%d projected/point fresh=%d/%d composed=%d fallback=%d budget (%d admission)/%d subview tiles=%d/%d pointFaces=%d resident=%.1fMB slots=%d/%d projected %d/%d point casterFaces=%d/%d culled/tested\n",
			( viewDef != NULL && viewDef->isSubview )
				? "subview" : "main",
			vkShadow.projectedCacheHits,
			vkShadow.pointCacheHits,
			vkShadow.projectedFreshUpdates,
			vkShadow.pointFreshUpdates,
			vkShadow.composePasses,
			vkShadow.budgetFallbacks,
			vkShadow.admissionDenied,
			vkShadow.subviewFallbacks,
			vkShadow.atlasTilesRendered,
			vkShadow.atlasTilesAllocated,
			vkShadow.pointFacesRendered,
			VK_ShadowMap_ResidentBytes() / ( 1024.0 * 1024.0 ),
			projectedSlots, projectedLimit,
			pointSlots, pointLimit,
			vkShadow.pointCasterFaceCulled, vkShadow.pointCasterFaceTests );
	// GPU shadow-pass timings. The numbers are what resolved during this
	// view, so they lag the work by a frame or more -- the same lagged
	// attribution the OpenGL shadow stats carry.
	const vkShadowGpuTimingReport_t &timing = VK_ShadowGpuTiming_Report();
	if ( r_shadowMapGpuTimerQueries.GetBool()
			|| r_shadowMapGpuSyncTimings.GetBool() ) {
		if ( !timing.available ) {
			common->Printf(
					"Vulkan shadow timings: unavailable"
					" (device reports no usable graphics timestamps)\n" );
		} else {
			common->Printf(
					"Vulkan shadow timings: gpu%s=%.2f/%d pending=%d dropped=%d"
					" map=%.2f/%d reuse=%.2f/%d\n",
					timing.synchronized ? "Sync" : "Query",
					timing.totalMilliseconds,
					timing.samples,
					timing.pending,
					timing.dropped,
					timing.phaseMilliseconds[ VK_SHADOW_TIMING_MAP_RENDER ],
					timing.phaseSamples[ VK_SHADOW_TIMING_MAP_RENDER ],
					timing.phaseMilliseconds[ VK_SHADOW_TIMING_CACHE_REUSE ],
					timing.phaseSamples[ VK_SHADOW_TIMING_CACHE_REUSE ] );
		}
	}
	if ( r_shadowMapPointHighPrecision.GetBool() ) {
		common->Printf(
				"Vulkan shadow storage: r_shadowMapPointHighPrecision is"
				" OpenGL-only; point cubes always store native depth\n" );
	}
	VK_ShadowMap_ReportViewLights();
}

// A resident entry matching this signature exists, without reserving it.
static const vkProjectedShadowCacheEntry_t *VK_ShadowMap_PeekProjectedCacheEntry(
		const idRenderWorldLocal *renderWorld, const int lightIndex,
		const vkShadowReceiverPass_t passKind, const int signature,
		const int tileSize, const int blockSize ) {
	const int limit = VK_ShadowMap_ProjectedCacheSlotLimit();
	for ( int i = 0 ; i < limit ; i++ ) {
		const vkProjectedShadowCacheEntry_t &entry =
				vkShadow.projectedCache[ i ];
		if ( entry.valid && !entry.reserved
				&& entry.generation == tr.videoRestartCount
				&& entry.renderWorld == renderWorld
				&& entry.lightIndex == lightIndex
				&& entry.passKind == passKind
				&& entry.signature == signature
				&& entry.tileSize == tileSize
				&& entry.blockSize == blockSize
				&& entry.image != VK_NULL_HANDLE ) {
			return &entry;
		}
	}
	return NULL;
}

static const vkPointShadowCacheEntry_t *VK_ShadowMap_PeekPointCacheEntry(
		const idRenderWorldLocal *renderWorld, const int lightIndex,
		const vkShadowReceiverPass_t passKind, const int signature,
		const int size ) {
	const int limit = VK_ShadowMap_PointCacheSlotLimit();
	for ( int i = 0 ; i < limit ; i++ ) {
		const vkPointShadowCacheEntry_t &entry =
				vkShadow.pointCache[ i ];
		if ( entry.valid && !entry.reserved
				&& entry.generation == tr.videoRestartCount
				&& entry.renderWorld == renderWorld
				&& entry.lightIndex == lightIndex
				&& entry.passKind == passKind
				&& entry.signature == signature
				&& entry.size == size
				&& entry.cube.image != VK_NULL_HANDLE ) {
			return &entry;
		}
	}
	return NULL;
}

// Fresh ownership renders this light would need, and how stale its newest
// resident content is. Returns false when the light needs no fresh update.
static bool VK_ShadowMap_EstimateUpdateCost( const viewLight_t *vLight,
		const viewDef_t *viewDef, int &cost, int &staleness ) {
	cost = 0;
	staleness = 64;
	if ( vLight == NULL || vLight->lightShader == NULL
			|| vLight->lightShader->IsFogLight()
			|| vLight->lightShader->IsBlendLight()
			|| vLight->lightShader->IsAmbientLight()
			|| !vLight->lightShader->LightCastsShadows() ) {
		return false;
	}
	if ( vLight->lightDef != NULL
			&& ( vLight->lightDef->parms.noShadows
				|| vLight->lightDef->parms.noDynamicShadows ) ) {
		return false;
	}
	const int lightIndex = VK_ShadowMap_LightIndex( vLight );
	const idRenderWorldLocal *renderWorld =
			viewDef != NULL ? viewDef->renderWorld : NULL;
	if ( lightIndex < 0 || renderWorld == NULL ) {
		return false;
	}

	const shadowMapLightClassification_t classification =
			R_ClassifyShadowMapLight( vLight );
	const bool pointLight = classification.pointLight;
	if ( pointLight && !r_shadowMapPointLights.GetBool() ) {
		return false;
	}
	const bool hasTranslucentReceivers =
			r_shadowMapTranslucentReceivers.GetBool()
			&& vLight->translucentInteractions != NULL;
	const bool passNeeded[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
		vLight->localInteractions != NULL,
		vLight->globalInteractions != NULL || hasTranslucentReceivers
	};

	int resourceSize = 0;
	int atlasDiv = 1;
	int cascadeCount = 1;
	if ( pointLight ) {
		resourceSize = VK_ShadowMap_PointSizeValue();
	} else {
		atlasDiv = idMath::ClampInt( 1, 2, classification.atlasDiv );
		// The atlas is created lazily by the first light that needs it, so
		// on the first view of a generation there is nothing to measure
		// against yet. Fall back to the configured edge; the scheduling
		// walk clamps against the real one either way.
		const int atlasEdge = vkShadow.atlasSize > 0 ? vkShadow.atlasSize
				: idMath::ClampInt( 2048, 8192,
						r_shadowMapAtlasSize.GetInteger() );
		const int maxTileSize = atlasEdge / atlasDiv;
		if ( maxTileSize < 128 ) {
			return false;
		}
		resourceSize = idMath::ClampInt( 128, maxTileSize,
				r_shadowMapSize.GetInteger() );
		cascadeCount = idMath::ClampInt( 1,
				SHADOWMAP_PROJECTED_MAX_CASCADES,
				classification.cascadeCount );
	}
	const bool cacheable = VK_ShadowMap_StaticCacheableReadOnly(
			vLight, viewDef, pointLight, cascadeCount );
	const int blockSize = resourceSize * atlasDiv;

	int newestUpdatedFrame = -1;
	for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
			passIndex++ ) {
		const vkShadowReceiverPass_t receiverPass =
				(vkShadowReceiverPass_t)passIndex;
		if ( !passNeeded[ passIndex ]
				|| !VK_ShadowMap_PassHasCasters( vLight, receiverPass ) ) {
			continue;
		}
		// A GLOBAL map with no local casters aliases the LOCAL one and costs
		// nothing extra, exactly as the scheduling walk decides.
		if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL
				&& !VK_ShadowMap_HasLocalCasters( vLight )
				&& passNeeded[ VK_SHADOW_RECEIVER_LOCAL ]
				&& VK_ShadowMap_PassHasCasters( vLight,
						VK_SHADOW_RECEIVER_LOCAL ) ) {
			continue;
		}
		const vkShadowReceiverPass_t cachePass =
				VK_ShadowMap_CachePassKind( vLight, receiverPass );
		const int signature =
				VK_ShadowMap_BuildPassSignatureForView( vLight, viewDef,
						cachePass, pointLight, resourceSize, atlasDiv,
						cascadeCount );
		if ( cacheable ) {
			if ( pointLight ) {
				const vkPointShadowCacheEntry_t *entry =
						VK_ShadowMap_PeekPointCacheEntry( renderWorld,
								lightIndex, cachePass, signature,
								resourceSize );
				if ( entry != NULL ) {
					newestUpdatedFrame = Max( newestUpdatedFrame,
							entry->lastUpdatedFrame );
					continue;
				}
			} else {
				const vkProjectedShadowCacheEntry_t *entry =
						VK_ShadowMap_PeekProjectedCacheEntry( renderWorld,
								lightIndex, cachePass, signature,
								resourceSize, blockSize );
				if ( entry != NULL ) {
					newestUpdatedFrame = Max( newestUpdatedFrame,
							entry->lastUpdatedFrame );
					continue;
				}
			}
		}
		cost++;
	}

	staleness = newestUpdatedFrame < 0 ? 64
			: idMath::ClampInt( 0, 64, tr.frameCount - newestUpdatedFrame );
	return cost > 0;
}

static void VK_ShadowMap_BuildUpdateAdmissions( const viewDef_t *viewDef ) {
	vkShadowAdmissionsActive = false;
	vkShadowAdmittedLightCount = 0;
	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	if ( updateBudget <= 0 || viewDef == NULL
			|| viewDef->viewLights == NULL ) {
		return;
	}
	if ( viewDef->isSubview
			&& idMath::ClampInt( 0, 2,
					r_shadowMapSubviewPolicy.GetInteger() ) > 0 ) {
		// the subview policy already forbids fresh renders
		return;
	}

	struct vkShadowAdmissionCandidate_t {
		int	lightIndex;
		int	cost;
		int	score;
	};
	vkShadowAdmissionCandidate_t candidates[ VK_SHADOW_MAX_ADMITTED_LIGHTS ];
	int candidateCount = 0;
	int totalCost = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights ;
			vLight != NULL
				&& candidateCount < VK_SHADOW_MAX_ADMITTED_LIGHTS ;
			vLight = vLight->next ) {
		int cost = 0;
		int staleness = 0;
		if ( !VK_ShadowMap_EstimateUpdateCost( vLight, viewDef, cost,
				staleness ) ) {
			continue;
		}
		const idScreenRect &rect = vLight->scissorRect;
		const int scissorArea = rect.IsEmpty() ? 0
				: ( rect.x2 + 1 - rect.x1 ) * ( rect.y2 + 1 - rect.y1 );
		// Screen coverage, how wrong the resident content already is, and
		// whether the camera stands inside the light. The GL score adds the
		// modern planner's fairness boost; that planner stays dormant under
		// Vulkan, so the remaining terms carry the ordering.
		candidates[ candidateCount ].lightIndex =
				VK_ShadowMap_LightIndex( vLight );
		candidates[ candidateCount ].cost = cost;
		candidates[ candidateCount ].score = scissorArea / 32
				+ staleness * 512
				+ ( vLight->viewInsideLight ? 8192 : 0 );
		candidateCount++;
		totalCost += cost;
	}
	if ( candidateCount == 0 || totalCost <= updateBudget ) {
		// everything fits; first-come order is already correct
		return;
	}

	// insertion sort by (score desc, lightIndex asc); counts are small
	for ( int i = 1 ; i < candidateCount ; i++ ) {
		const vkShadowAdmissionCandidate_t key = candidates[ i ];
		int j = i - 1;
		while ( j >= 0
				&& ( candidates[ j ].score < key.score
					|| ( candidates[ j ].score == key.score
						&& candidates[ j ].lightIndex
							> key.lightIndex ) ) ) {
			candidates[ j + 1 ] = candidates[ j ];
			j--;
		}
		candidates[ j + 1 ] = key;
	}

	int remaining = updateBudget;
	for ( int i = 0 ; i < candidateCount && remaining > 0 ; i++ ) {
		// A two-pass light must be able to fill its cache with a budget of one.
		// Admit the first candidate for partial progress; SchedulePass enforces
		// the per-pass limit and can finish the other ownership on a later view.
		if ( candidates[ i ].cost > remaining && vkShadowAdmittedLightCount > 0 ) {
			continue;
		}
		vkShadowAdmittedLightIndexes[ vkShadowAdmittedLightCount++ ] =
				candidates[ i ].lightIndex;
		remaining = Max( 0, remaining - candidates[ i ].cost );
	}
	vkShadowAdmissionsActive = true;
}

/*
====================
VK_ShadowMap_PrepareViewLights

CPU phase: the RB_ShadowMapLightSupportReason-equivalent gate (r_useShadowMap
&& r_shadows, not fog/blend/ambient/noShadows, casts shadows, has receivers
and casters), then per class: POINT lights (classification.pointLight, gated
on r_shadowMapPointLights like the GL support reason) claim a cube from the
per-view pool; PROJECTED/PARALLEL lights build the projected state via the
shared front-end helpers and take a contiguous cascade block.
====================
*/
int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef,
		const bool stencilFallbackAvailable ) {
	vkShadow.numLights = 0;
	vkShadow.preparedView = viewDef;
	vkShadow.preparedFrame = tr.frameCount;
	vkShadow.nextTileX = 0;
	vkShadow.nextTileY = 0;
	vkShadow.nextTileRowHeight = 0;
	vkShadow.pointCubesUsed = 0;
	vkShadow.freshUpdates = 0;
	vkShadow.projectedCacheHits = 0;
	vkShadow.pointCacheHits = 0;
	vkShadow.projectedFreshUpdates = 0;
	vkShadow.pointFreshUpdates = 0;
	vkShadow.composePasses = 0;
	vkShadow.admissionDenied = 0;
	vkShadow.atlasTilesRendered = 0;
	vkShadow.atlasTilesAllocated = 0;
	vkShadow.pointFacesRendered = 0;
	vkShadow.pointCasterFaceTests = 0;
	vkShadow.pointCasterFaceCulled = 0;
	vkShadow.budgetFallbacks = 0;
	vkShadow.subviewFallbacks = 0;

	if ( viewDef == NULL || !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return 0;
	}
	if ( !vkCtx.initialized ) {
		return 0;
	}
	VK_ShadowMap_BeginCacheView( viewDef );
	// Decide which lights a limited r_shadowMapMaxUpdatesPerView should
	// spend on before any of them can claim it in list order.
	VK_ShadowMap_BuildUpdateAdmissions( viewDef );

	int prepared = 0;
	bool resourcesChecked = false;
	bool resourcesOk = false;

	// Correctness-required ownership maps go first. An optional map always has
	// a complete stencil result and may fall back to it; it must never consume
	// the bounded light table, atlas, or cube pool before a later ownership
	// whose stencil representation is incomplete or unavailable. A light with
	// a mixture of required and optional ownerships maps only the required
	// ownership this view; its optional ownership deliberately uses stencil.
	for ( int correctnessPhase = 0 ; correctnessPhase < 2 ;
			correctnessPhase++ ) {
		const bool requiredPhase = correctnessPhase == 0;
		for ( const viewLight_t *vLight = viewDef->viewLights ; vLight ;
				vLight = vLight->next ) {
		if ( vLight->lightShader == NULL || vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		// policy gate (RB_ShadowMapLightPolicySupportReason)
		if ( vLight->lightDef != NULL
				&& ( vLight->lightDef->parms.noShadows || vLight->lightDef->parms.noDynamicShadows ) ) {
			continue;
		}
		if ( vLight->lightShader->IsAmbientLight() || !vLight->lightShader->LightCastsShadows() ) {
			continue;
		}
		const bool hasTranslucentReceivers =
			r_shadowMapTranslucentReceivers.GetBool()
			&& vLight->translucentInteractions != NULL;
		if ( vLight->globalInteractions == NULL && vLight->localInteractions == NULL
				&& !hasTranslucentReceivers ) {
			continue;
		}

		const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
		const bool passNeeded[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
			vLight->localInteractions != NULL,
			vLight->globalInteractions != NULL || hasTranslucentReceivers
		};
		const bool passHasCasters[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
			VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_LOCAL ),
			VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_GLOBAL )
		};
		const int incompleteStencilMask =
				vLight->shadowMapIncompleteStencilMask |
				( vLight->shadowMapPrelightStencilRequiredMask
					& ~vLight->shadowMapPrelightStencilReadyMask );
		const bool passRequiresMap[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
			!stencilFallbackAvailable ||
				( incompleteStencilMask &
					SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0,
			!stencilFallbackAvailable ||
				( incompleteStencilMask &
					SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0
		};
		const bool lightHasRequiredMap =
				( passNeeded[ VK_SHADOW_RECEIVER_LOCAL ] &&
					passHasCasters[ VK_SHADOW_RECEIVER_LOCAL ] &&
					passRequiresMap[ VK_SHADOW_RECEIVER_LOCAL ] ) ||
				( passNeeded[ VK_SHADOW_RECEIVER_GLOBAL ] &&
					passHasCasters[ VK_SHADOW_RECEIVER_GLOBAL ] &&
					passRequiresMap[ VK_SHADOW_RECEIVER_GLOBAL ] );
		if ( lightHasRequiredMap != requiredPhase ) {
			continue;
		}
		if ( ( !passNeeded[ VK_SHADOW_RECEIVER_LOCAL ] || !passHasCasters[ VK_SHADOW_RECEIVER_LOCAL ] )
				&& ( !passNeeded[ VK_SHADOW_RECEIVER_GLOBAL ] || !passHasCasters[ VK_SHADOW_RECEIVER_GLOBAL ] ) ) {
			// No required receiver pass has map-compatible casters. Use the
			// retained stencil path now; sticky preserves later-frame volumes
			// while mapped support may be retried.
			VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			continue;
		}
		if ( vkShadow.numLights >= VK_SHADOW_MAX_LIGHTS ) {
			// This otherwise mappable light cannot enter the bounded backend
			// table. Complete stencil coverage can recover this view; map-only
			// geometry must report a resource failure until its volumes rebuild.
			VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			continue;
		}

		if ( !resourcesChecked ) {
			resourcesChecked = true;
			resourcesOk = VK_ShadowMap_EnsureResources();
		}
		if ( !resourcesOk ) {
			// The destroy path reset class resource truth; this hard failure
			// also records the shared per-light volume-retention bit.
			VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			return 0;
		}

		if ( classification.pointLight ) {
			// F2b: point lights render into the cube pool; the classification
			// already excludes parallel (sun) lights (pointLight && !parallel).
			// r_shadowMapPointLights is the GL support-reason gate. Capacity is
			// governed by ownership resources; the lazy pool covers both
			// receiver ownerships of every admitted light, while exact resident
			// hits claim no scratch cube.
			if ( !r_shadowMapPointLights.GetBool() ) {
				// policy gate the front-end mirror checks identically
				// (tr_light.cpp) — never elided, no sticky needed
				continue;
			}
			vkShadowLightState_t &entry = vkShadow.lights[ vkShadow.numLights ];
			memset( &entry, 0, sizeof( entry ) );
			entry.vLight = vLight;
			entry.pointLight = true;
			entry.pointFar = R_ShadowMapPointFarDistance( vLight );
			for ( int i = 0 ; i < 3 ; i++ ) {
				entry.pointLightOrigin[ i ] =
						vLight->globalLightOrigin[ i ];
			}
			const int pointSize = VK_ShadowMap_PointSizeValue();

			for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
				const vkShadowReceiverPass_t receiverPass = (vkShadowReceiverPass_t)passIndex;
				if ( !passNeeded[ passIndex ] ) {
					continue;
				}
				if ( lightHasRequiredMap &&
						!passRequiresMap[ passIndex ] ) {
					continue;
				}
				if ( !VK_ShadowMap_MapOrHybridOwnershipComplete(
						vLight, receiverPass ) ) {
					VK_ShadowMap_MarkStencilFallbackSticky( vLight );
					continue;
				}
				if ( !passHasCasters[ passIndex ] ) {
					VK_ShadowMap_MarkStencilFallbackSticky( vLight );
					continue;
				}

				// Without local casters both ownership maps are identical.
				// Alias GLOBAL to the already-prepared LOCAL cube.
				if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL
						&& !VK_ShadowMap_HasLocalCasters( vLight )
						&& entry.passes[ VK_SHADOW_RECEIVER_LOCAL ].valid ) {
					VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );
					continue;
				}

				const vkShadowSchedule_t schedule =
						VK_ShadowMap_SchedulePass(
								vLight, viewDef, receiverPass,
								stencilFallbackAvailable,
								true, pointSize, 1, 1 );
				if ( schedule.action
						== VK_SHADOW_SCHEDULE_FALLBACK ) {
					// No prepared pass means the retained ownership stencil
					// path wins this frame. Admission misses are not sticky.
					continue;
				}
				if ( !VK_ShadowMap_AllocatePointPass(
						entry, receiverPass, schedule ) ) {
					if ( vLight->lightDef == NULL ||
							!vLight->lightDef->shadowMapStencilFallbackSticky ) {
						common->DPrintf(
							"point shadow ownership allocation failed for lightDef %d "
							"pass %s (scratch=%d/%d cache=%d action=%d)\n",
							vLight->lightDef != NULL
								? vLight->lightDef->index : -1,
							receiverPass == VK_SHADOW_RECEIVER_LOCAL
								? "LOCAL" : "GLOBAL",
							vkShadow.pointCubesUsed,
							VK_SHADOW_MAX_POINT_CUBES,
							schedule.cacheEntry,
							(int)schedule.action );
					}
					VK_ShadowMap_MarkStencilFallbackSticky( vLight );
				}
			}

			VK_ShadowMap_RefreshLightValidity( entry );
			if ( entry.valid ) {
				// Seal the same world-bounded receiver coefficients as GL after
				// cache reuse has restored the physical cube's final far envelope
				// and face size.
				const int faceSize = entry.tileSize;
				const shadowMapPointReceiverSettings_t receiverSettings =
					R_ShadowMapPointReceiverSettings(
						entry.pointFar, faceSize );
				entry.constantBias = receiverSettings.constantBias;
				entry.normalBias = receiverSettings.normalBias;
				entry.texelDepthBias = receiverSettings.texelBiasScale
					/ static_cast<float>( Max( 1, faceSize ) );
				entry.normalOffsetWorld =
					2.0f * receiverSettings.normalOffsetScale
						/ static_cast<float>( Max( 1, faceSize ) );
				vkShadow.numLights++;
				prepared++;
			}
			continue;
		}

		// Match RB_ShadowMapTileSizeForLight: r_shadowMapSize is the edge of
		// one cascade tile, while a CSM light needs a contiguous atlasDiv^2
		// block. Clamp against the backend atlas so admitting CSM can never
		// produce an allocation that is structurally impossible.
		const int requestedAtlasDiv = idMath::ClampInt( 1, 2,
				classification.atlasDiv );
		const int maxTileSize = vkShadow.atlasSize / requestedAtlasDiv;
		if ( maxTileSize < 128 ) {
			VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			continue;
		}
		const int tileSize = idMath::ClampInt( 128, maxTileSize,
				r_shadowMapSize.GetInteger() );

		shadowMapProjectedLightState_t projectedState;
		R_BuildShadowMapProjectedLightState( vLight, viewDef, tileSize, projectedState );
		if ( !projectedState.valid || projectedState.cascadeCount < 1
				|| projectedState.cascadeCount > SHADOWMAP_PROJECTED_MAX_CASCADES
				|| projectedState.atlasDiv < 1 || projectedState.atlasDiv > 2
				|| projectedState.tileSize != tileSize
				|| projectedState.tileSize * projectedState.atlasDiv
					> vkShadow.atlasSize ) {
			VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			continue;
		}

		vkShadowLightState_t &entry = vkShadow.lights[ vkShadow.numLights ];
		memset( &entry, 0, sizeof( entry ) );
		entry.vLight = vLight;
		entry.tileSize = projectedState.tileSize;
		entry.projectedState = projectedState;

		const float invAtlas = 1.0f / (float)vkShadow.atlasSize;
		entry.invAtlasSize[ 0 ] = invAtlas;
		entry.invAtlasSize[ 1 ] = invAtlas;

		for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
			const vkShadowReceiverPass_t receiverPass = (vkShadowReceiverPass_t)passIndex;
			if ( !passNeeded[ passIndex ] ) {
				continue;
			}
			if ( lightHasRequiredMap &&
					!passRequiresMap[ passIndex ] ) {
				continue;
			}
			if ( !VK_ShadowMap_MapOrHybridOwnershipComplete(
					vLight, receiverPass ) ) {
				VK_ShadowMap_MarkStencilFallbackSticky( vLight );
				continue;
			}
			if ( !passHasCasters[ passIndex ] ) {
				VK_ShadowMap_MarkStencilFallbackSticky( vLight );
				continue;
			}

			if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL
					&& !VK_ShadowMap_HasLocalCasters( vLight )
					&& entry.passes[ VK_SHADOW_RECEIVER_LOCAL ].valid ) {
				VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );
				continue;
			}

			const vkShadowSchedule_t schedule =
					VK_ShadowMap_SchedulePass(
							vLight, viewDef, receiverPass,
							stencilFallbackAvailable, false,
							entry.tileSize,
							entry.projectedState.atlasDiv,
							entry.projectedState.cascadeCount );
			if ( schedule.action == VK_SHADOW_SCHEDULE_FALLBACK ) {
				// Exact misses denied by budget/subview policy remain stencil
				// this frame and deliberately do not set sticky fallback.
				continue;
			}
			if ( !VK_ShadowMap_AllocateProjectedPass(
					entry, receiverPass, schedule ) ) {
				// Atlas exhausted for this ownership pass. Preserve any other
				// valid pass for the current frame and restore stencil volume
				// generation for this light from the next frame on.
				VK_ShadowMap_MarkStencilFallbackSticky( vLight );
			}
		}

		VK_ShadowMap_RefreshLightValidity( entry );
		if ( entry.valid ) {
			vkShadow.numLights++;
			prepared++;
		}
	}
	}

	return prepared;
}

const vkShadowLightState_t *VK_ShadowMap_LightState( const viewLight_t *vLight ) {
	if ( vkShadow.preparedFrame != tr.frameCount ) {
		return NULL;
	}
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		if ( vkShadow.lights[ i ].vLight == vLight ) {
			return vkShadow.lights[ i ].valid ? &vkShadow.lights[ i ] : NULL;
		}
	}
	return NULL;
}

const vkShadowPassState_t *VK_ShadowMap_PassState(
		const vkShadowLightState_t *lightState,
		const vkShadowReceiverPass_t receiverPass ) {
	if ( lightState == NULL || receiverPass < VK_SHADOW_RECEIVER_LOCAL
			|| receiverPass >= VK_SHADOW_RECEIVER_PASS_COUNT ) {
		return NULL;
	}
	const vkShadowPassState_t &pass = lightState->passes[ receiverPass ];
	return pass.valid ? &pass : NULL;
}

/*
====================
Caster drawing
====================
*/

// r_shadowMapCasterCulling in the executor's GL-parity winding convention
// (negative-height viewport, CCW front): mode 0 is always two-sided, mode 1
// stores light-facing near-shell faces (cull FRONT), and mode 2 (default) is
// topology-aware: open/unknown hulls and hulls enclosing the light are
// two-sided, as are mirrored/invalid model transforms, while other perfect
// hulls use mode 1. The authored material orientation is honored whenever
// one-sided culling is active.
static VkCullModeFlags VK_ShadowMap_CasterCullMode(
		const viewLight_t *vLight, const drawSurf_t *surf,
		const srfTriangles_t *casterGeo ) {
	const int mode = idMath::ClampInt( 0, 2, r_shadowMapCasterCulling.GetInteger() );
	const idMaterial *shader = surf != NULL ? surf->material : NULL;
	const int materialCull = ( shader != NULL ) ? shader->GetCullType() : CT_FRONT_SIDED;
	if ( mode == 0 || materialCull == CT_TWO_SIDED
			|| ( mode == 2
				&& ( casterGeo == NULL || !casterGeo->perfectHull
					|| surf == NULL || surf->space == NULL
					|| R_ShadowMapCasterTransformNeedsTwoSided(
						surf->space->modelMatrix )
					|| R_ShadowMapLightOriginInsideCasterBounds( vLight,
						surf->space->modelMatrix,
						casterGeo->bounds[0].ToFloatPtr(),
						casterGeo->bounds[1].ToFloatPtr() ) ) ) ) {
		return VK_CULL_MODE_NONE;
	}
	bool cullFront = true;
	if ( materialCull == CT_BACK_SIDED ) {
		cullFront = !cullFront;
	}
	return cullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

// Vulkan can currently represent the ordinary explicit-ST static-image
// cutout path. Cinematics and generated/dynamic images require their own
// update/ownership path and non-explicit texgen requires extra vertex data;
// fail those surfaces to sticky stencil fallback instead of turning their
// holes into solid depth.
static bool VK_ShadowMap_PerforatedCasterSupported( const drawSurf_t *surf,
		bool requireDescriptors, bool &haveActiveStage,
		bool &haveDrawableStage, const char *&failureReason ) {
	haveActiveStage = false;
	haveDrawableStage = false;
	failureReason = NULL;
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		failureReason = "missing-material-registers";
		return false;
	}

	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0 ; stage < stageCount ; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}
		haveActiveStage = true;

		// A conditioned-on stage whose alpha scale is zero intentionally
		// emits no depth and needs no texture representation.
		const float alphaScale = regs[ pStage->color.registers[ 3 ] ];
		if ( alphaScale <= 0.0f ) {
			continue;
		}
		haveDrawableStage = true;

		if ( pStage->texture.cinematic != NULL ) {
			failureReason = "cinematic-alpha";
			return false;
		}
		if ( pStage->texture.dynamic != DI_STATIC ) {
			failureReason = "dynamic-alpha";
			return false;
		}
		if ( pStage->texture.image == NULL ) {
			failureReason = "missing-alpha-image";
			return false;
		}
		if ( pStage->texture.texgen != TG_EXPLICIT ) {
			failureReason = "unsupported-alpha-texgen";
			return false;
		}
		if ( pStage->alphaTestMode != GL_LESS
				&& pStage->alphaTestMode != GL_EQUAL
				&& pStage->alphaTestMode != GL_GREATER ) {
			failureReason = "unsupported-alpha-mode";
			return false;
		}
		if ( requireDescriptors
				&& VK_Exec_ImageDescriptor(
						pStage->texture.image->GetDeviceHandle(), true )
					== VK_NULL_HANDLE ) {
			failureReason = "alpha-descriptor";
			return false;
		}
	}

	return true;
}

static void VK_ShadowMap_SetPushAlphaIdentity( vkCasterPush_t &push ) {
	push.alphaS[ 0 ] = 1.0f;
	push.alphaS[ 1 ] = 0.0f;
	push.alphaS[ 3 ] = 0.0f;
	push.alphaT[ 0 ] = 0.0f;
	push.alphaT[ 1 ] = 1.0f;
	push.alphaT[ 3 ] = 0.0f;
	push.params[ 0 ] = 0.0f;
	push.params[ 1 ] = 0.0f;
	push.params[ 2 ] = 1.0f;
	push.params[ 3 ] = 0.0f;
}

static bool VK_ShadowMap_AlphaTestModeValue( const int alphaTestMode,
		float &modeValue ) {
	if ( alphaTestMode == GL_LESS ) {
		modeValue = -1.0f;
		return true;
	}
	if ( alphaTestMode == GL_EQUAL ) {
		modeValue = 2.0f;
		return true;
	}
	if ( alphaTestMode == GL_GREATER ) {
		modeValue = 1.0f;
		return true;
	}
	modeValue = 0.0f;
	return false;
}

// The Vulkan caster push block is intentionally capped at the portable
// 128-byte push-constant minimum, so it cannot carry three complete model
// rows alongside the clip/depth and alpha state. Stable hashing therefore
// uses model-local coordinates in the shader, plus the world translation's
// GL hash phase packed into params.w. This matches the GL world-space seed
// for identity/world geometry and unrotated two-unit-aligned translations,
// while remaining camera/atlas stable for every caster. Arbitrarily rotated,
// scaled, or sub-two-unit-translated entities keep a stable approximation
// rather than a bit-exact world-space hash.
static float VK_ShadowMap_AlphaHashMode( const drawSurf_t *surf ) {
	if ( !r_shadowMapHashedAlpha.GetBool() ) {
		return 0.0f;
	}
	if ( !r_shadowMapStableAlphaHash.GetBool() || surf == NULL
			|| surf->space == NULL ) {
		return 1.0f;	// screen-space gl_FragCoord hash
	}

	const float *modelMatrix = surf->space->modelMatrix;
	const float worldSeedBase =
			idMath::Floor( modelMatrix[ 12 ] * 0.5f ) * 0.06711056f
			+ idMath::Floor( modelMatrix[ 13 ] * 0.5f ) * 0.00583715f
			+ idMath::Floor( modelMatrix[ 14 ] * 0.5f ) * 0.01327111f;
	const float worldSeed = worldSeedBase - idMath::Floor( worldSeedBase );
	return 2.0f + worldSeed;	// stable model/world-seeded hash
}

typedef struct vkCasterPassCtx_s {
	VkCommandBuffer		cmd;
	int					slot;
	VkPipelineLayout	layout;
	VkDescriptorSet		whiteSet;
	VkDescriptorSet		boundImageSet;
	VkCullModeFlags		boundCullMode;
	float				slopeFactor;
	float				constOffset;
	bool				unsupportedCaster;
} vkCasterPassCtx_t;

static void VK_ShadowMap_CasterDraw( vkCasterPassCtx_t &ctx, const vkCasterPush_t &push,
		VkDescriptorSet imageSet, const srfTriangles_t *casterGeo ) {
	if ( imageSet != ctx.boundImageSet ) {
		ctx.boundImageSet = imageSet;
		vkCmdBindDescriptorSets( ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout, 0, 1, &imageSet, 0, NULL );
	}
	vkCmdPushConstants( ctx.cmd, ctx.layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	VK_Device_CountDrawIndexed( (int)( casterGeo->numIndexes ), (int)( casterGeo->numVerts ) );
	vkCmdDrawIndexed( ctx.cmd, (uint32_t)casterGeo->numIndexes, 1, 0, 0, 0 );
}

// casters draw the AMBIENT geometry (full tri->indexes), never the
// light-tris subset (RB_ShadowMapResolveCasterDrawData); returns NULL when
// the surface cannot produce a bound-able caster
static srfTriangles_t *VK_ShadowMap_ResolveCasterGeo( const drawSurf_t *surf ) {
	if ( surf->geo == NULL || surf->space == NULL ) {
		return NULL;
	}
	srfTriangles_t *casterGeo = surf->geo->ambientSurface != NULL
			? surf->geo->ambientSurface : const_cast<srfTriangles_t *>( surf->geo );
	if ( casterGeo == NULL || casterGeo->numVerts <= 0 || casterGeo->numIndexes <= 0 ) {
		return NULL;
	}
	if ( casterGeo->ambientCache == NULL ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( R_TriHasPrimBatchMesh( casterGeo ) ) {
			if ( !R_CreatePackedSurfaceFrameCaches( casterGeo, false, true ) ) {
				return NULL;
			}
		} else
#endif
		if ( casterGeo->verts == NULL || !R_CreateAmbientCache( casterGeo, false ) ) {
			return NULL;
		}
	}
	// Vulkan always streams an indexed draw into its own ring. CPU-null
	// surfaces remain valid when their CPU-backed index cache is resident;
	// packed ambient surfaces can additionally reconstruct the canonical
	// draw-index stream in VK_Exec_BindTriGeometry.
	if ( casterGeo->indexes == NULL && casterGeo->indexCache == NULL
			&& !R_TriHasPrimBatchMesh( casterGeo ) ) {
		return NULL;
	}
	return casterGeo;
}

static void VK_ShadowMap_ReportUnsupportedCaster( vkCasterPassCtx_t &ctx,
		const drawSurf_t *surf, const char *reason ) {
	if ( !ctx.unsupportedCaster ) {
		const char *materialName = surf != NULL && surf->material != NULL
				? surf->material->GetName() : "<null>";
		common->DPrintf(
				"Vulkan: shadow-map caster '%s' needs stencil fallback (%s)\n",
				materialName, reason != NULL ? reason : "unsupported-alpha" );
	}
	ctx.unsupportedCaster = true;
}

// Per-surface tail shared by projected and point chains: material cull mode,
// then the perforated alpha-stage walk or solid draw. Returns true only when
// at least one indexed draw was actually emitted. Unsupported cutout stages
// set ctx.unsupportedCaster and emit NO conservative solid substitute.
static bool VK_ShadowMap_DrawResolvedCaster( vkCasterPassCtx_t &ctx,
		vkCasterPush_t &push,
		const viewLight_t *vLight, const drawSurf_t *surf,
		const srfTriangles_t *casterGeo ) {
	const VkCullModeFlags cullMode =
		VK_ShadowMap_CasterCullMode( vLight, surf, casterGeo );
	if ( cullMode != ctx.boundCullMode ) {
		ctx.boundCullMode = cullMode;
		vkCmdSetCullMode( ctx.cmd, cullMode );
	}

	const idMaterial *shader = surf->material;
	if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
		bool haveActiveStage = false;
		bool haveDrawableStage = false;
		const char *failureReason = NULL;
		if ( !VK_ShadowMap_PerforatedCasterSupported( surf, true,
				haveActiveStage, haveDrawableStage, failureReason ) ) {
			VK_ShadowMap_ReportUnsupportedCaster( ctx, surf, failureReason );
			return false;
		}

		// Matches the depth prepass: a perforated material with every
		// alpha-test stage conditioned off is currently opaque.
		if ( !haveActiveStage ) {
			VK_ShadowMap_CasterDraw( ctx, push, ctx.whiteSet, casterGeo );
			return true;
		}
		if ( !haveDrawableStage ) {
			return false;
		}

		const float *regs = surf->shaderRegisters;
		bool emittedDraw = false;
		const int stageCount = shader->GetNumStages();
		for ( int stage = 0 ; stage < stageCount ; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}
			const float alphaScale = regs[ pStage->color.registers[ 3 ] ];
			if ( alphaScale <= 0.0f ) {
				continue;
			}
			VkDescriptorSet stageSet = VK_Exec_ImageDescriptor( pStage->texture.image->GetDeviceHandle(), true );
			if ( stageSet == VK_NULL_HANDLE ) {
				VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
						"alpha-descriptor-lost" );
				return false;
			}
			if ( pStage->texture.hasMatrix ) {
				push.alphaS[ 0 ] = regs[ pStage->texture.matrix[ 0 ][ 0 ] ];
				push.alphaS[ 1 ] = regs[ pStage->texture.matrix[ 0 ][ 1 ] ];
				push.alphaS[ 3 ] = regs[ pStage->texture.matrix[ 0 ][ 2 ] ];
				push.alphaT[ 0 ] = regs[ pStage->texture.matrix[ 1 ][ 0 ] ];
				push.alphaT[ 1 ] = regs[ pStage->texture.matrix[ 1 ][ 1 ] ];
				push.alphaT[ 3 ] = regs[ pStage->texture.matrix[ 1 ][ 2 ] ];
			}
			if ( !VK_ShadowMap_AlphaTestModeValue(
					pStage->alphaTestMode, push.params[ 0 ] ) ) {
				VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
						"unsupported-alpha-mode" );
				return false;
			}
			push.params[ 1 ] = regs[ pStage->alphaTestRegister ];
			push.params[ 2 ] = alphaScale;
			push.params[ 3 ] = VK_ShadowMap_AlphaHashMode( surf );
			VK_ShadowMap_CasterDraw( ctx, push, stageSet, casterGeo );
			emittedDraw = true;
			// restore the solid defaults for the next stage/surface
			VK_ShadowMap_SetPushAlphaIdentity( push );
		}
		return emittedDraw;
	}

	VK_ShadowMap_CasterDraw( ctx, push, ctx.whiteSet, casterGeo );
	return true;
}

// Conservative CSM rejection ported from RB_ShadowMapCasterOutsideCascade:
// reject only when all eight world-space bounds corners lie beyond the same
// x/y side of the cropped cascade. Apex-side corners keep the caster.
static bool VK_ShadowMap_CasterOutsideCascade(
		const vkShadowLightState_t &light, const drawSurf_t *surf,
		const srfTriangles_t *casterGeo, const int cascadeIndex ) {
	if ( light.projectedState.cascadeCount <= 1 || surf == NULL
			|| surf->space == NULL || casterGeo == NULL
			|| casterGeo->bounds.IsCleared() ) {
		return false;
	}

	const int safeCascadeIndex = idMath::ClampInt( 0,
			SHADOWMAP_PROJECTED_MAX_CASCADES - 1, cascadeIndex );
	const idPlane *planes =
			light.projectedState.clipPlanes[ safeCascadeIndex ];
	const idBounds &bounds = casterGeo->bounds;
	int outsideMask = 0x0F;
	for ( int cornerIndex = 0 ; cornerIndex < 8 ; cornerIndex++ ) {
		const idVec3 corner(
				bounds[ ( cornerIndex >> 0 ) & 1 ][ 0 ],
				bounds[ ( cornerIndex >> 1 ) & 1 ][ 1 ],
				bounds[ ( cornerIndex >> 2 ) & 1 ][ 2 ] );
		idVec3 world;
		R_LocalPointToGlobal( surf->space->modelMatrix, corner, world );
		const float w = planes[ 3 ].Distance( world );
		if ( w <= 1.0e-5f ) {
			return false;
		}

		int mask = 0;
		if ( planes[ 0 ].Distance( world ) < -w ) {
			mask |= 1;
		} else if ( planes[ 0 ].Distance( world ) > w ) {
			mask |= 2;
		}
		if ( planes[ 1 ].Distance( world ) < -w ) {
			mask |= 4;
		} else if ( planes[ 1 ].Distance( world ) > w ) {
			mask |= 8;
		}
		outsideMask &= mask;
		if ( outsideMask == 0 ) {
			return false;
		}
	}
	return outsideMask != 0;
}

// One caster chain into the bound cascade tile. Casters conservatively culled
// from a cascade still count as represented so an empty crop cannot
// invalidate an otherwise complete ownership map.
static int VK_ShadowMap_DrawCasterChain( vkCasterPassCtx_t &ctx,
		const vkShadowLightState_t &light, const int cascadeIndex,
		const drawSurf_t *surf ) {
	int drawnCasters = 0;
	const int safeCascadeIndex = idMath::ClampInt( 0,
			SHADOWMAP_PROJECTED_MAX_CASCADES - 1, cascadeIndex );
	const idPlane *clipPlanes =
			light.projectedState.clipPlanes[ safeCascadeIndex ];
	float clipMatrix[ 16 ];
	R_ShadowMapClipPlanesToGLMatrix( clipPlanes, clipMatrix );

	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = VK_ShadowMap_ResolveCasterGeo( surf );
		if ( casterGeo == NULL ) {
			VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
					"caster-geometry" );
			continue;
		}
		if ( VK_ShadowMap_CasterOutsideCascade(
				light, surf, casterGeo, safeCascadeIndex ) ) {
			drawnCasters++;
			continue;
		}
		if ( !VK_Exec_BindTriGeometry( ctx.cmd, ctx.slot, casterGeo ) ) {
			VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
					"caster-geometry-bind" );
			continue;
		}

		// light-space MVP: the light's clip matrix over the model matrix, with
		// the shared clip-z fixup; the stored depth comes from the localized
		// depth plane instead (shader-written, GL caster parity)
		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		float mvpGL[ 16 ];
		myGlMultMatrix( surf->space->modelMatrix, clipMatrix, mvpGL );
		VK_FixupClipSpaceZ( push.mvp, mvpGL );

		idPlane localDepthPlane;
		R_GlobalPlaneToLocal( surf->space->modelMatrix,
				clipPlanes[ 2 ], localDepthPlane );
		memcpy( push.depthRow, localDepthPlane.ToFloatPtr(), sizeof( push.depthRow ) );

		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = ctx.slopeFactor;
		push.alphaT[ 2 ] = ctx.constOffset;

		if ( VK_ShadowMap_DrawResolvedCaster( ctx, push,
				light.vLight, surf, casterGeo ) ) {
			drawnCasters++;
		}
	}

	return drawnCasters;
}

// GL RB_RenderShadowMap chain selection. SHADOWMAP_RENDER_STATIC_ONLY draws
// the two static chains into a tile that will be published as cached content;
// SHADOWMAP_RENDER_COMPOSE_DYNAMIC draws the two dynamic chains over depth
// that is already present; an uncached full render draws all four. LOCAL maps
// take only the global chains, GLOBAL maps add the local ones.
typedef enum vkShadowChainSelect_e {
	VK_SHADOW_CHAINS_ALL = 0,
	VK_SHADOW_CHAINS_STATIC_ONLY,
	VK_SHADOW_CHAINS_DYNAMIC_ONLY
} vkShadowChainSelect_t;

static int VK_ShadowMap_DrawPassCasters( vkCasterPassCtx_t &ctx,
		const vkShadowLightState_t &light,
		const vkShadowReceiverPass_t receiverPass,
		const int cascadeIndex,
		const vkShadowChainSelect_t select ) {
	const viewLight_t *vLight = light.vLight;
	if ( vLight == NULL ) {
		return 0;
	}
	const bool drawStatic = select != VK_SHADOW_CHAINS_DYNAMIC_ONLY;
	const bool drawDynamic = select != VK_SHADOW_CHAINS_STATIC_ONLY;
	const bool globalOwnership =
			receiverPass == VK_SHADOW_RECEIVER_GLOBAL;
	int drawnCasters = 0;
	if ( drawStatic ) {
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light,
				cascadeIndex, vLight->globalShadowMapCasters );
	}
	if ( drawDynamic ) {
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light,
				cascadeIndex, vLight->globalShadowMapDynamicCasters );
	}
	if ( globalOwnership && drawStatic ) {
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light,
				cascadeIndex, vLight->localShadowMapCasters );
	}
	if ( globalOwnership && drawDynamic ) {
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light,
				cascadeIndex, vLight->localShadowMapDynamicCasters );
	}
	return drawnCasters;
}

// One caster chain into the bound cube face. The push
// mvp is the model -> face-view matrix; the shader projects analytically
// through projRow and stores the radial view-space distance / far.
static int VK_ShadowMap_DrawPointCasterChain( vkCasterPassCtx_t &ctx,
		const viewLight_t *vLight, const float faceViewMatrix[ 16 ],
		const float projRow[ 2 ], const float farClip, const int cubeFace,
		const drawSurf_t *surf ) {
	int drawnCasters = 0;

	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = VK_ShadowMap_ResolveCasterGeo( surf );
		if ( casterGeo == NULL ) {
			VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
					"caster-geometry" );
			continue;
		}
		vkShadow.pointCasterFaceTests++;
		if ( R_ShadowMapCasterOutsidePointFace( vLight, surf->space->modelMatrix,
				casterGeo->bounds[0].ToFloatPtr(), casterGeo->bounds[1].ToFloatPtr(), cubeFace ) ) {
			vkShadow.pointCasterFaceCulled++;
			continue;
		}
		if ( !VK_Exec_BindTriGeometry( ctx.cmd, ctx.slot, casterGeo ) ) {
			VK_ShadowMap_ReportUnsupportedCaster( ctx, surf,
					"caster-geometry-bind" );
			continue;
		}

		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		myGlMultMatrix( surf->space->modelMatrix, faceViewMatrix, push.mvp );
		push.depthRow[ 0 ] = projRow[ 0 ];
		push.depthRow[ 1 ] = projRow[ 1 ];
		push.depthRow[ 2 ] = farClip;

		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = ctx.slopeFactor;
		push.alphaT[ 2 ] = ctx.constOffset;

		if ( VK_ShadowMap_DrawResolvedCaster( ctx, push,
				vLight, surf, casterGeo ) ) {
			drawnCasters++;
		}
	}

	return drawnCasters;
}

static bool VK_ShadowMap_CasterChainRepresentable( const drawSurf_t *surf ) {
	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		if ( surf->material == NULL
				|| surf->material->Coverage() != MC_PERFORATED ) {
			continue;
		}

		bool haveActiveStage = false;
		bool haveDrawableStage = false;
		const char *failureReason = NULL;
		if ( !VK_ShadowMap_PerforatedCasterSupported( surf, true,
				haveActiveStage, haveDrawableStage, failureReason ) ) {
			common->DPrintf(
					"Vulkan: shadow-map caster '%s' rejected (%s); enabling stencil fallback\n",
					surf->material->GetName(),
					failureReason != NULL ? failureReason : "unsupported-alpha" );
			return false;
		}
	}
	return true;
}

// Validate every chain contributing to an ownership resource BEFORE any
// depth is emitted. This prevents a late unsupported cutout from leaving a
// partially valid map that would shadow some receivers while leaking others.
static bool VK_ShadowMap_PassCastersRepresentable(
		const vkShadowLightState_t &light,
		const vkShadowReceiverPass_t receiverPass ) {
	const viewLight_t *vLight = light.vLight;
	if ( vLight == NULL
			|| !VK_ShadowMap_CasterChainRepresentable(
					vLight->globalShadowMapCasters )
			|| !VK_ShadowMap_CasterChainRepresentable(
					vLight->globalShadowMapDynamicCasters ) ) {
		return false;
	}

	if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL
			&& ( !VK_ShadowMap_CasterChainRepresentable(
						vLight->localShadowMapCasters )
				|| !VK_ShadowMap_CasterChainRepresentable(
						vLight->localShadowMapDynamicCasters ) ) ) {
		return false;
	}
	return true;
}

/*
====================
Shared fixed-classic transaction preflight

The legacy scheduler remains the physical atlas/cube/cache allocator. The
shared corridor accepts its result only after reconciling every allocation to
the sealed semantic pass and retaining every fresh caster upload and alpha
descriptor. No live material stage or register is followed by Commit.
====================
*/

static bool VK_ClassicShadow_FloatsFinite( const float *values,
		const int count ) {
	if ( values == NULL || count < 0 ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		if ( !std::isfinite( values[ i ] ) ) {
			return false;
		}
	}
	return true;
}

static bool VK_ClassicShadow_CullMode( const rendererCullMode_t cull,
		VkCullModeFlags &mode ) {
	switch ( cull ) {
	case RENDERER_CULL_NONE:
		mode = VK_CULL_MODE_NONE;
		return true;
	case RENDERER_CULL_FRONT:
		mode = VK_CULL_MODE_FRONT_BIT;
		return true;
	case RENDERER_CULL_BACK:
		mode = VK_CULL_MODE_BACK_BIT;
		return true;
	default:
		return false;
	}
}

static bool VK_ClassicShadow_ResolveDescriptor(
		const std::uint64_t resourceId, VkDescriptorSet &descriptor ) {
	descriptor = VK_NULL_HANDLE;
	const classicInteractionDomainTexture_t *texture =
		R_ClassicInteractionDomain_ResolveTexture( resourceId );
	if ( texture == NULL || texture->textureResourceId != resourceId
			|| texture->image == NULL || !texture->loaded
			|| texture->defaulted || texture->mutableImage
			|| texture->textureHandle == 0
			|| !texture->image->IsLoaded()
			|| texture->image->IsDefaulted()
			|| texture->textureHandle
				!= const_cast<idImage *>( texture->image )->GetDeviceHandle()
			|| texture->filter != texture->image->GetFilter()
			|| texture->repeat != texture->image->GetRepeat()
			|| texture->storageGeneration
				!= texture->image->GetStorageGeneration() ) {
		return false;
	}
	descriptor = VK_Exec_ImageDescriptor( texture->textureHandle, true );
	return descriptor != VK_NULL_HANDLE;
}

static bool VK_ClassicShadow_ProjectedStateMatches(
		const shadowMapProjectedLightState_t &sealed,
		const shadowMapProjectedLightState_t &physical ) {
	if ( sealed.valid != physical.valid
			|| sealed.cascadeCount != physical.cascadeCount
			|| sealed.atlasDiv != physical.atlasDiv
			|| sealed.tileSize != physical.tileSize
			|| sealed.requestedCascadeCount != physical.requestedCascadeCount
			|| sealed.fallbackCascade != physical.fallbackCascade
			|| sealed.fallbackReason != physical.fallbackReason
			|| sealed.cascadeFallback != physical.cascadeFallback ) {
		return false;
	}
	return memcmp( sealed.clipPlanes, physical.clipPlanes,
			sizeof( sealed.clipPlanes ) ) == 0
		&& memcmp( sealed.splitDepths, physical.splitDepths,
			sizeof( sealed.splitDepths ) ) == 0
		&& memcmp( sealed.biasScale, physical.biasScale,
			sizeof( sealed.biasScale ) ) == 0
		&& memcmp( sealed.texelDepthBias, physical.texelDepthBias,
			sizeof( sealed.texelDepthBias ) ) == 0
		&& memcmp( sealed.worldTexelSize, physical.worldTexelSize,
			sizeof( sealed.worldTexelSize ) ) == 0;
}

static int VK_ClassicShadow_FindCasterPlan(
		const classicInteractionDomainShadowCaster_t *caster ) {
	for ( int i = 0; i < vkClassicShadowTransaction.casterPlanCount; ++i ) {
		if ( vkClassicShadowTransaction.casterPlans[ i ].caster == caster ) {
			return i;
		}
	}
	return -1;
}

static bool VK_ClassicShadow_PrepareCaster(
		const classicInteractionDomainShadowCaster_t &caster,
		int &casterPlanIndex ) {
	vkClassicShadowTransaction_t &transaction =
		vkClassicShadowTransaction;
	casterPlanIndex = VK_ClassicShadow_FindCasterPlan( &caster );
	if ( casterPlanIndex >= 0 ) {
		return true;
	}
	if ( transaction.casterPlanCount
			>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS
			|| caster.disposition
				!= CLASSIC_INTERACTION_SHADOW_CASTER_DRAW
			|| caster.indexSelection
				!= CLASSIC_INTERACTION_SHADOW_INDEX_AMBIENT
			|| !caster.ambientGeometry || caster.preload || caster.external
			|| caster.legacyDrawSurf == NULL
			|| caster.legacyCasterGeometry == NULL
			|| caster.legacyDrawSurf->space == NULL
			|| R_TriHasPrimBatchMesh( caster.legacyCasterGeometry )
			|| caster.vertexCount <= 0 || caster.selectedIndexCount <= 0
			|| caster.totalIndexCount != caster.selectedIndexCount
			|| caster.legacyCasterGeometry->numVerts != caster.vertexCount
			|| caster.legacyCasterGeometry->numIndexes
				!= caster.selectedIndexCount
			|| caster.legacyCasterGeometry->ambientCache == NULL
			|| ( caster.legacyCasterGeometry->indexes == NULL
				&& caster.legacyCasterGeometry->indexCache == NULL )
			|| !VK_ClassicShadow_FloatsFinite( caster.modelMatrix, 16 )
			|| !VK_ClassicShadow_FloatsFinite( caster.boundsMin, 3 )
			|| !VK_ClassicShadow_FloatsFinite( caster.boundsMax, 3 ) ) {
		return false;
	}

	vkClassicShadowCasterPlan_t &plan =
		transaction.casterPlans[ transaction.casterPlanCount ];
	memset( &plan, 0, sizeof( plan ) );
	plan.caster = &caster;
	plan.vertexOffset = -1;
	plan.indexOffset = -1;
	plan.firstAlpha = transaction.alphaPlanCount;
	if ( !VK_ClassicShadow_CullMode( caster.cull, plan.cullMode )
			|| !VK_Exec_PrepareTriGeometry( transaction.cmd,
				transaction.frameSlot, caster.legacyCasterGeometry,
				plan.vertexOffset, plan.indexOffset ) ) {
		return false;
	}

	for ( int alphaIndex = 0; alphaIndex < caster.alphaStageCount;
			++alphaIndex ) {
		if ( transaction.alphaPlanCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES ) {
			return false;
		}
		const classicInteractionDomainShadowAlphaStage_t *stage =
			R_ClassicInteractionDomain_ShadowAlphaStage( caster,
				alphaIndex );
		float alphaMode = 0.0f;
		VkDescriptorSet imageSet = VK_NULL_HANDLE;
		if ( stage == NULL || stage->textureResourceId == 0
				|| stage->alphaScale <= 0.0f
				|| !VK_ShadowMap_AlphaTestModeValue(
					stage->alphaTestMode, alphaMode )
				|| !std::isfinite( stage->alphaTestValue )
				|| !std::isfinite( stage->alphaScale )
				|| !std::isfinite( stage->alphaHashMode )
				|| !VK_ClassicShadow_FloatsFinite(
					&stage->textureMatrix[ 0 ][ 0 ], 8 )
				|| !VK_ClassicShadow_ResolveDescriptor(
					stage->textureResourceId, imageSet ) ) {
			return false;
		}
		vkClassicShadowAlphaPlan_t &alphaPlan =
			transaction.alphaPlans[ transaction.alphaPlanCount++ ];
		alphaPlan.stage = stage;
		alphaPlan.imageSet = imageSet;
		plan.alphaCount++;
	}

	casterPlanIndex = transaction.casterPlanCount++;
	return true;
}

static bool VK_ClassicShadow_AddCasterChain(
		const classicInteractionDomainLight_t &light,
		const classicInteractionDomainShadowChain_t chain,
		vkClassicShadowPassPlan_t &passPlan,
		int &mappedCasters, int &drawableCasters, int &noopCasters ) {
	vkClassicShadowTransaction_t &transaction =
		vkClassicShadowTransaction;
	const int chainCount = light.shadowCasterCount[ chain ];
	if ( chainCount < 0 ) {
		return false;
	}
	for ( int i = 0; i < chainCount; ++i ) {
		const classicInteractionDomainShadowCaster_t *caster =
			R_ClassicInteractionDomain_LightShadowCaster( light, chain, i );
		if ( caster == NULL || caster->chain != chain
				|| caster->legacyViewLight != light.legacyViewLight ) {
			return false;
		}
		mappedCasters++;
		if ( caster->disposition
				== CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY ) {
			if ( !R_ClassicInteractionDomain_ShadowCasterNoopValid( *caster ) ) {
				return false;
			}
			noopCasters++;
			continue;
		}
		int planIndex = -1;
		if ( transaction.casterRefCount >= VK_CLASSIC_SHADOW_MAX_CASTER_REFS
				|| !VK_ClassicShadow_PrepareCaster( *caster, planIndex ) ) {
			return false;
		}
		transaction.casterRefs[ transaction.casterRefCount++ ] = planIndex;
		passPlan.casterRefCount++;
		drawableCasters++;
	}
	return true;
}

static bool VK_ClassicShadow_ValidatePhysicalPass(
		const classicInteractionDomainView_t &view,
		const classicInteractionDomainShadowMapPass_t &sealed,
		const vkShadowLightState_t &light,
		const vkShadowPassState_t &pass,
		const vkShadowReceiverPass_t receiver ) {
	const bool point = sealed.lightClass == SHADOWMAP_LIGHT_POINT;
	if ( !pass.valid || light.pointLight != point
			|| pass.resourcePass < VK_SHADOW_RECEIVER_LOCAL
			|| pass.resourcePass >= VK_SHADOW_RECEIVER_PASS_COUNT
			|| sealed.resourceAlias
				!= ( pass.resourcePass != receiver )
			|| sealed.resourceOwner
				!= static_cast<classicInteractionDomainReceiver_t>(
					pass.resourcePass ) ) {
		return false;
	}
	if ( pass.cacheHit && !sealed.allowCacheReuse ) {
		return false;
	}
	if ( pass.cacheUpdate && !sealed.allowCacheUpdate ) {
		return false;
	}
	if ( !pass.cacheHit && !pass.cacheUpdate && !sealed.allowScratch ) {
		return false;
	}
	// The physical cache signature is built from the live caster state.  Reject
	// any drift after the front end sealed the pass so a cache hit cannot use
	// different culling/bias, and freshly rendered sealed contents cannot be
	// published under a mismatched live signature.
	if ( light.vLight == NULL
			|| sealed.casterSignature
				!= light.vLight->shadowMapCasterSignature
			|| sealed.hashedAlpha != r_shadowMapHashedAlpha.GetBool()
			|| sealed.stableAlphaHash
				!= r_shadowMapStableAlphaHash.GetBool()
			|| sealed.casterCullMode != idMath::ClampInt( 0, 2,
			r_shadowMapCasterCulling.GetInteger() )
			|| idMath::Fabs( sealed.polygonFactor
				- r_shadowMapPolygonFactor.GetFloat() ) > 0.00001f
			|| idMath::Fabs( sealed.polygonOffset
				- r_shadowMapPolygonOffset.GetFloat() ) > 0.00001f ) {
		return false;
	}

	if ( point ) {
		if ( !sealed.point.valid || sealed.point.faceCount != 6
				|| sealed.point.faceSize != light.tileSize
				|| sealed.point.farDistance != light.pointFar
				|| sealed.point.lightOrigin[ 0 ] != light.pointLightOrigin[ 0 ]
				|| sealed.point.lightOrigin[ 1 ] != light.pointLightOrigin[ 1 ]
				|| sealed.point.lightOrigin[ 2 ] != light.pointLightOrigin[ 2 ]
				|| pass.pointSet == VK_NULL_HANDLE ) {
			return false;
		}
		if ( pass.cacheHit ) {
			const vkPointShadowCacheEntry_t *cache =
				pass.cacheEntry >= 0
					&& pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS
				? &vkShadow.pointCache[ pass.cacheEntry ] : NULL;
			return cache != NULL && cache->valid && cache->reserved
				&& cache->generation == tr.videoRestartCount
				&& cache->renderWorld == view.viewDef->renderWorld
				&& cache->signature == pass.cacheSignature
				&& cache->size == light.tileSize
				&& cache->cube.image != VK_NULL_HANDLE
				&& cache->cube.layout
					== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		const vkPointShadowCube_t *cube = NULL;
		if ( pass.cacheUpdate && pass.cacheEntry >= 0
				&& pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS
				&& vkShadow.pointCache[ pass.cacheEntry ].reserved ) {
			cube = &vkShadow.pointCache[ pass.cacheEntry ].cube;
		} else if ( pass.cubeIndex >= 0
				&& pass.cubeIndex < VK_SHADOW_MAX_POINT_CUBES ) {
			cube = &vkShadow.pointCubes[ pass.cubeIndex ];
		}
		return cube != NULL && cube->image != VK_NULL_HANDLE;
	}

	if ( !sealed.projected.state.valid
			|| !VK_ClassicShadow_ProjectedStateMatches(
				sealed.projected.state, light.projectedState )
			|| light.tileSize != sealed.projected.state.tileSize
			|| vkShadow.atlasImage == VK_NULL_HANDLE ) {
		return false;
	}
	if ( pass.cacheHit ) {
		const vkProjectedShadowCacheEntry_t *cache =
			pass.cacheEntry >= 0
				&& pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS
			? &vkShadow.projectedCache[ pass.cacheEntry ] : NULL;
		return cache != NULL && cache->valid && cache->reserved
			&& cache->generation == tr.videoRestartCount
			&& cache->renderWorld == view.viewDef->renderWorld
			&& cache->signature == pass.cacheSignature
			&& cache->tileSize == light.tileSize
			&& cache->blockSize
				== VK_ShadowMap_ProjectedBlockSize( light )
			&& cache->image != VK_NULL_HANDLE
			&& cache->layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
	return !pass.cacheUpdate || ( pass.cacheEntry >= 0
		&& pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS
		&& vkShadow.projectedCache[ pass.cacheEntry ].reserved
		&& vkShadow.projectedCache[ pass.cacheEntry ].image
			!= VK_NULL_HANDLE );
}

void VK_ShadowMap_AbortClassicInteractionView(
		const classicInteractionDomainView_t *view ) {
	if ( !vkClassicShadowTransaction.active ) {
		return;
	}
	if ( view != NULL && vkClassicShadowTransaction.view != view ) {
		return;
	}
	if ( vkClassicShadowTransaction.ownsPreparedLights ) {
		VK_ShadowMap_ReleasePreparedLights( false );
	}
	memset( &vkClassicShadowTransaction, 0,
		sizeof( vkClassicShadowTransaction ) );
}

bool VK_ShadowMap_PreflightClassicInteractionView(
		const classicInteractionDomainView_t *view ) {
	VK_ShadowMap_AbortClassicInteractionView( NULL );
	memset( &vkClassicShadowTransaction, 0,
		sizeof( vkClassicShadowTransaction ) );
	vkClassicShadowTransaction_t &transaction =
		vkClassicShadowTransaction;
	transaction.active = true;
	transaction.view = view;
	if ( view == NULL || !view->ready || view->viewDef == NULL
			|| view->shadowMapPassCount < 0
			|| view->shadowMapPassCount
				> CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES ) {
		VK_ShadowMap_AbortClassicInteractionView( view );
		return false;
	}
	if ( view->shadowMapPassCount == 0 ) {
		transaction.ready = true;
		return true;
	}

	transaction.cmd = VK_Exec_ActiveCmd();
	transaction.frameSlot = VK_Exec_ActiveFrameSlot();
	transaction.layout = VK_Exec_BasePipelineLayout();
	transaction.projectedCasterPipeline = VK_Exec_CasterPipeline();
	transaction.pointCasterPipeline = VK_Exec_PointCasterPipeline();
	if ( transaction.cmd == VK_NULL_HANDLE || transaction.frameSlot < 0
			|| transaction.frameSlot >= VK_FRAMES_IN_FLIGHT
			|| transaction.layout == VK_NULL_HANDLE
			|| globalImages == NULL || globalImages->whiteImage == NULL
			|| !globalImages->whiteImage->IsLoaded()
			|| globalImages->whiteImage->IsDefaulted() ) {
		VK_ShadowMap_AbortClassicInteractionView( view );
		return false;
	}
	transaction.whiteSet = VK_Exec_ImageDescriptor(
		globalImages->whiteImage->GetDeviceHandle(), true );
	if ( transaction.whiteSet == VK_NULL_HANDLE ) {
		VK_ShadowMap_AbortClassicInteractionView( view );
		return false;
	}
	transaction.ownsPreparedLights = true;
	// Shared ownership may atomically hand the whole view back to the legacy
	// executor, whose own preparation will re-evaluate the active target.  Let
	// optional maps obey the ordinary budget here; if even one sealed pass is
	// denied, reconciliation below aborts every private reservation and no
	// shared attachment write has occurred. Correctness-required maps still
	// bypass the budget through their incomplete-stencil mask.
	if ( VK_ShadowMap_PrepareViewLights( view->viewDef, true ) <= 0 ) {
		VK_ShadowMap_AbortClassicInteractionView( view );
		return false;
	}

	int reconciledPasses = 0;
	for ( int lightIndex = 0; lightIndex < view->lightCount; ++lightIndex ) {
		const classicInteractionDomainLight_t *light =
			R_ClassicInteractionDomain_ViewLight( *view, lightIndex );
		if ( light == NULL || light->legacyViewLight == NULL ) {
			VK_ShadowMap_AbortClassicInteractionView( view );
			return false;
		}
		const vkShadowLightState_t *physicalLight =
			VK_ShadowMap_LightState( light->legacyViewLight );

		for ( int receiverIndex = CLASSIC_INTERACTION_RECEIVER_LOCAL;
				receiverIndex <= CLASSIC_INTERACTION_RECEIVER_GLOBAL;
				++receiverIndex ) {
			const classicInteractionDomainReceiver_t domainReceiver =
				static_cast<classicInteractionDomainReceiver_t>( receiverIndex );
			const classicInteractionDomainShadowMapPass_t *sealed =
				R_ClassicInteractionDomain_LightShadowMapPass(
					*light, domainReceiver );
			if ( sealed == NULL ) {
				continue;
			}
			if ( transaction.passPlanCount
					>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES
					|| physicalLight == NULL
					|| sealed->legacyViewLight != light->legacyViewLight
					|| sealed->lightIndex != view->firstLight + lightIndex
					|| sealed->receiver != domainReceiver
					|| ( sealed->disposition
						!= CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED
						&& sealed->disposition
							!= CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID )
					|| !sealed->mapRequired
					|| ( sealed->disposition
						== CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED
						&& !sealed->mapComplete )
					|| ( sealed->disposition
						== CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID
						&& !sealed->hybridComplete )
					|| sealed->hasTranslucentCasters ) {
				VK_ShadowMap_AbortClassicInteractionView( view );
				return false;
			}
			const vkShadowReceiverPass_t receiver =
				static_cast<vkShadowReceiverPass_t>( receiverIndex );
			const vkShadowPassState_t *physicalPass =
				VK_ShadowMap_PassState( physicalLight, receiver );
			if ( physicalPass == NULL
					|| !VK_ClassicShadow_ValidatePhysicalPass( *view,
						*sealed, *physicalLight, *physicalPass, receiver ) ) {
				VK_ShadowMap_AbortClassicInteractionView( view );
				return false;
			}

			vkClassicShadowPassPlan_t &passPlan =
				transaction.passPlans[ transaction.passPlanCount++ ];
			memset( &passPlan, 0, sizeof( passPlan ) );
			passPlan.light = light;
			passPlan.pass = sealed;
			passPlan.physicalLight = physicalLight;
			passPlan.physicalPass = physicalPass;
			passPlan.receiver = receiver;
			passPlan.resourceOwner = physicalPass->resourcePass == receiver;
			passPlan.firstCasterRef = transaction.casterRefCount;

			int mappedCasters = 0;
			int drawableCasters = 0;
			int noopCasters = 0;
			const classicInteractionDomainShadowChain_t chains[] = {
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC
			};
			const int chainCount = receiver == VK_SHADOW_RECEIVER_GLOBAL
				? 4 : 2;
			for ( int chainIndex = 0; chainIndex < chainCount;
					++chainIndex ) {
				if ( !VK_ClassicShadow_AddCasterChain( *light,
						chains[ chainIndex ], passPlan, mappedCasters,
						drawableCasters, noopCasters ) ) {
					VK_ShadowMap_AbortClassicInteractionView( view );
					return false;
				}
			}
			if ( mappedCasters != sealed->mappedCasterCount
					|| drawableCasters != sealed->drawableMappedCasters
					|| noopCasters != sealed->noopMappedCasters ) {
				VK_ShadowMap_AbortClassicInteractionView( view );
				return false;
			}

			if ( passPlan.resourceOwner ) {
				if ( physicalLight->pointLight ) {
					if ( physicalPass->cacheHit ) {
						transaction.pointHitCount++;
					} else {
						transaction.pointFreshCount++;
					}
				} else {
					transaction.projectedCount++;
					if ( !physicalPass->cacheHit ) {
						transaction.projectedFreshCount++;
					}
				}
			}
			reconciledPasses++;
		}
	}
	int physicalPassCount = 0;
	for ( int physicalLightIndex = 0;
			physicalLightIndex < vkShadow.numLights; ++physicalLightIndex ) {
		const vkShadowLightState_t &physical =
			vkShadow.lights[ physicalLightIndex ];
		for ( int receiverIndex = 0;
				receiverIndex < VK_SHADOW_RECEIVER_PASS_COUNT;
				++receiverIndex ) {
			if ( !physical.passes[ receiverIndex ].valid ) {
				continue;
			}
			bool found = false;
			for ( int planIndex = 0;
					planIndex < transaction.passPlanCount; ++planIndex ) {
				const vkClassicShadowPassPlan_t &plan =
					transaction.passPlans[ planIndex ];
				if ( plan.physicalLight == &physical
						&& plan.receiver == receiverIndex ) {
					found = true;
					break;
				}
			}
			if ( !found ) {
				VK_ShadowMap_AbortClassicInteractionView( view );
				return false;
			}
			physicalPassCount++;
		}
	}

	if ( reconciledPasses != view->shadowMapPassCount
			|| transaction.passPlanCount != view->shadowMapPassCount
			|| physicalPassCount != transaction.passPlanCount
			|| transaction.projectedCount + transaction.pointFreshCount
				+ transaction.pointHitCount <= 0
			|| ( transaction.projectedFreshCount > 0
				&& transaction.projectedCasterPipeline == VK_NULL_HANDLE )
			|| ( transaction.pointFreshCount > 0
				&& transaction.pointCasterPipeline == VK_NULL_HANDLE ) ) {
		VK_ShadowMap_AbortClassicInteractionView( view );
		return false;
	}

	transaction.ready = true;
	return true;
}

static const vkClassicShadowPassPlan_t *VK_ClassicShadow_FindPassPlan(
		const vkShadowLightState_t *light,
		const vkShadowReceiverPass_t receiver ) {
	for ( int i = 0; i < vkClassicShadowTransaction.passPlanCount; ++i ) {
		const vkClassicShadowPassPlan_t &plan =
			vkClassicShadowTransaction.passPlans[ i ];
		if ( plan.physicalLight == light && plan.receiver == receiver ) {
			return &plan;
		}
	}
	return NULL;
}

static VkCullModeFlags VK_ClassicShadow_EffectiveCull(
		const vkClassicShadowCasterPlan_t &casterPlan,
		const classicInteractionDomainShadowMapPass_t &pass ) {
	if ( pass.casterCullMode == 0
			|| casterPlan.cullMode == VK_CULL_MODE_NONE
			|| ( pass.casterCullMode == 2
				&& ( !casterPlan.caster->perfectHull
					|| R_ShadowMapLightOriginInsideCasterBounds(
						pass.legacyViewLight,
						casterPlan.caster->modelMatrix,
						casterPlan.caster->boundsMin,
						casterPlan.caster->boundsMax ) ) ) ) {
		return VK_CULL_MODE_NONE;
	}
	// Both explicit mode 1 and AUTO mode 2 on a sealed perfect hull store the
	// light-facing near shell. The material cull orientation may reverse it.
	bool cullFront = true;
	if ( casterPlan.cullMode == VK_CULL_MODE_BACK_BIT ) {
		cullFront = !cullFront;
	}
	return cullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

static void VK_ClassicShadow_DrawCaster(
		vkCasterPassCtx_t &ctx,
		const vkClassicShadowCasterPlan_t &casterPlan,
		const classicInteractionDomainShadowMapPass_t &pass,
		const vkCasterPush_t &basePush ) {
	const classicInteractionDomainShadowCaster_t &caster =
		*casterPlan.caster;
	const VkCullModeFlags cullMode =
		VK_ClassicShadow_EffectiveCull( casterPlan, pass );
	if ( cullMode != ctx.boundCullMode ) {
		ctx.boundCullMode = cullMode;
		vkCmdSetCullMode( ctx.cmd, cullMode );
	}
	VK_Exec_BindPreparedTriGeometry( ctx.cmd, ctx.slot,
		casterPlan.vertexOffset, casterPlan.indexOffset );

	const int drawCount = casterPlan.alphaCount > 0
		? casterPlan.alphaCount : 1;
	for ( int alphaIndex = 0; alphaIndex < drawCount; ++alphaIndex ) {
		vkCasterPush_t push = basePush;
		VkDescriptorSet imageSet = ctx.whiteSet;
		if ( casterPlan.alphaCount > 0 ) {
			const vkClassicShadowAlphaPlan_t &alphaPlan =
				vkClassicShadowTransaction.alphaPlans[
					casterPlan.firstAlpha + alphaIndex ];
			const classicInteractionDomainShadowAlphaStage_t &stage =
				*alphaPlan.stage;
			imageSet = alphaPlan.imageSet;
			push.alphaS[ 0 ] = stage.textureMatrix[ 0 ][ 0 ];
			push.alphaS[ 1 ] = stage.textureMatrix[ 0 ][ 1 ];
			push.alphaS[ 3 ] = stage.textureMatrix[ 0 ][ 3 ];
			push.alphaT[ 0 ] = stage.textureMatrix[ 1 ][ 0 ];
			push.alphaT[ 1 ] = stage.textureMatrix[ 1 ][ 1 ];
			push.alphaT[ 3 ] = stage.textureMatrix[ 1 ][ 3 ];
			VK_ShadowMap_AlphaTestModeValue( stage.alphaTestMode,
				push.params[ 0 ] );
			push.params[ 1 ] = stage.alphaTestValue;
			push.params[ 2 ] = stage.alphaScale;
			push.params[ 3 ] = stage.alphaHashMode;
		}
		if ( imageSet != ctx.boundImageSet ) {
			ctx.boundImageSet = imageSet;
			vkCmdBindDescriptorSets( ctx.cmd,
				VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout,
				0, 1, &imageSet, 0, NULL );
		}
		vkCmdPushConstants( ctx.cmd, ctx.layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
		vkCmdDrawIndexed( ctx.cmd,
			static_cast<uint32_t>( caster.selectedIndexCount ),
			1, 0, 0, 0 );
		backEnd.pc.c_shadowElements++;
		backEnd.pc.c_shadowIndexes += caster.selectedIndexCount;
		backEnd.pc.c_shadowVertexes += caster.vertexCount;
	}
}

static bool VK_ClassicShadow_CasterOutsideCascade(
		const vkClassicShadowCasterPlan_t &casterPlan,
		const classicInteractionDomainShadowMapPass_t &pass,
		const int cascadeIndex ) {
	const classicInteractionDomainShadowCaster_t &caster =
		*casterPlan.caster;
	const shadowMapProjectedLightState_t &projected = pass.projected.state;
	if ( projected.cascadeCount <= 1 ) {
		return false;
	}
	const int safeCascade = idMath::ClampInt( 0,
		SHADOWMAP_PROJECTED_MAX_CASCADES - 1, cascadeIndex );
	const idPlane *planes = projected.clipPlanes[ safeCascade ];
	int outsideMask = 0x0f;
	for ( int cornerIndex = 0; cornerIndex < 8; ++cornerIndex ) {
		const idVec3 local(
			caster.boundsMin[ 0 ] + ( ( cornerIndex & 1 ) != 0
				? caster.boundsMax[ 0 ] - caster.boundsMin[ 0 ] : 0.0f ),
			caster.boundsMin[ 1 ] + ( ( cornerIndex & 2 ) != 0
				? caster.boundsMax[ 1 ] - caster.boundsMin[ 1 ] : 0.0f ),
			caster.boundsMin[ 2 ] + ( ( cornerIndex & 4 ) != 0
				? caster.boundsMax[ 2 ] - caster.boundsMin[ 2 ] : 0.0f ) );
		idVec3 world;
		R_LocalPointToGlobal( caster.modelMatrix, local, world );
		const float w = planes[ 3 ].Distance( world );
		if ( w <= 1.0e-5f ) {
			return false;
		}
		int mask = 0;
		if ( planes[ 0 ].Distance( world ) < -w ) {
			mask |= 1;
		} else if ( planes[ 0 ].Distance( world ) > w ) {
			mask |= 2;
		}
		if ( planes[ 1 ].Distance( world ) < -w ) {
			mask |= 4;
		} else if ( planes[ 1 ].Distance( world ) > w ) {
			mask |= 8;
		}
		outsideMask &= mask;
		if ( outsideMask == 0 ) {
			return false;
		}
	}
	return outsideMask != 0;
}

static int VK_ClassicShadow_DrawProjectedPass(
		vkCasterPassCtx_t &ctx,
		const vkClassicShadowPassPlan_t &passPlan,
		const int cascadeIndex ) {
	const classicInteractionDomainShadowMapPass_t &pass = *passPlan.pass;
	const int safeCascade = idMath::ClampInt( 0,
		SHADOWMAP_PROJECTED_MAX_CASCADES - 1, cascadeIndex );
	const idPlane *clipPlanes = pass.projected.state.clipPlanes[
		safeCascade ];
	float clipMatrix[ 16 ];
	R_ShadowMapClipPlanesToGLMatrix( clipPlanes, clipMatrix );
	int draws = 0;
	for ( int i = 0; i < passPlan.casterRefCount; ++i ) {
		const vkClassicShadowCasterPlan_t &casterPlan =
			vkClassicShadowTransaction.casterPlans[
				vkClassicShadowTransaction.casterRefs[
					passPlan.firstCasterRef + i ] ];
		if ( VK_ClassicShadow_CasterOutsideCascade(
				casterPlan, pass, safeCascade ) ) {
			continue;
		}
		const classicInteractionDomainShadowCaster_t &caster =
			*casterPlan.caster;
		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		float mvpGL[ 16 ];
		myGlMultMatrix( caster.modelMatrix, clipMatrix, mvpGL );
		VK_FixupClipSpaceZ( push.mvp, mvpGL );
		idPlane localDepthPlane;
		R_GlobalPlaneToLocal( caster.modelMatrix, clipPlanes[ 2 ],
			localDepthPlane );
		memcpy( push.depthRow, localDepthPlane.ToFloatPtr(),
			sizeof( push.depthRow ) );
		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = pass.polygonFactor;
		push.alphaT[ 2 ] = pass.polygonOffset
			* ( 1.0f / 16777216.0f );
		VK_ClassicShadow_DrawCaster( ctx, casterPlan, pass, push );
		draws++;
	}
	return draws;
}

static int VK_ClassicShadow_DrawPointPass(
		vkCasterPassCtx_t &ctx,
		const vkClassicShadowPassPlan_t &passPlan,
		const float faceViewMatrix[ 16 ], const float projRow[ 2 ] ) {
	const classicInteractionDomainShadowMapPass_t &pass = *passPlan.pass;
	int draws = 0;
	for ( int i = 0; i < passPlan.casterRefCount; ++i ) {
		const vkClassicShadowCasterPlan_t &casterPlan =
			vkClassicShadowTransaction.casterPlans[
				vkClassicShadowTransaction.casterRefs[
					passPlan.firstCasterRef + i ] ];
		const classicInteractionDomainShadowCaster_t &caster =
			*casterPlan.caster;
		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		myGlMultMatrix( caster.modelMatrix, faceViewMatrix, push.mvp );
		push.depthRow[ 0 ] = projRow[ 0 ];
		push.depthRow[ 1 ] = projRow[ 1 ];
		push.depthRow[ 2 ] = pass.point.farDistance;
		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = pass.polygonFactor;
		push.alphaT[ 2 ] = pass.polygonOffset
			* ( 1.0f / 16777216.0f );
		VK_ClassicShadow_DrawCaster( ctx, casterPlan, pass, push );
		draws++;
	}
	return draws;
}

/*
====================
VK_ShadowMap_RenderAtlas

GPU phase: suspend the main rendering scope, render every prepared
projected ownership pass into its atlas cascade block inside one depth-only
dynamic-rendering scope, render every prepared point ownership pass into
its depth cube (six per-face scopes), transition everything for fragment
compare sampling, and resume the main scope with loadOp LOAD.
====================
*/

static void VK_ShadowMap_ImageBarrier( VkCommandBuffer cmd,
		VkImage image, const uint32_t layerCount,
		const VkImageLayout oldLayout,
		const VkImageLayout newLayout,
		const VkPipelineStageFlags2 srcStageMask,
		const VkAccessFlags2 srcAccessMask,
		const VkPipelineStageFlags2 dstStageMask,
		const VkAccessFlags2 dstAccessMask ) {
	VkImageMemoryBarrier2 barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStageMask;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.dstAccessMask = dstAccessMask;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.image = image;
	barrier.subresourceRange.aspectMask =
			VK_ShadowMap_DepthAspectMask();
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = layerCount;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( cmd, &dep );
}

static void VK_ShadowMap_CopyDepthTile( VkCommandBuffer cmd,
		VkImage srcImage, const int srcX, const int srcY,
		VkImage dstImage, const int dstX, const int dstY,
		const int size ) {
	VkImageCopy2 region;
	memset( &region, 0, sizeof( region ) );
	region.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.srcSubresource.layerCount = 1;
	region.srcOffset.x = srcX;
	region.srcOffset.y = srcY;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.dstSubresource.layerCount = 1;
	region.dstOffset.x = dstX;
	region.dstOffset.y = dstY;
	region.extent.width = (uint32_t)size;
	region.extent.height = (uint32_t)size;
	region.extent.depth = 1;

	VkCopyImageInfo2 copy;
	memset( &copy, 0, sizeof( copy ) );
	copy.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
	copy.srcImage = srcImage;
	copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	copy.dstImage = dstImage;
	copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	copy.regionCount = 1;
	copy.pRegions = &region;
	vkCmdCopyImage2( cmd, &copy );
}

static void VK_ShadowMap_FinalizeCachePasses(
		const viewDef_t *viewDef ) {
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		vkShadowLightState_t &light = vkShadow.lights[ i ];
		for ( int passIndex = 0 ;
				passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
				passIndex++ ) {
			const vkShadowReceiverPass_t receiverPass =
					(vkShadowReceiverPass_t)passIndex;
			const vkShadowPassState_t &pass =
					light.passes[ passIndex ];
			if ( !pass.valid || pass.resourcePass != receiverPass
					|| pass.cacheEntry < 0
					|| pass.cacheEntry
						>= VK_SHADOW_MAX_CACHE_SLOTS ) {
				continue;
			}
			if ( pass.cacheHit ) {
				if ( light.pointLight ) {
					vkShadow.pointCache[
							pass.cacheEntry ].reserved = false;
				} else {
					vkShadow.projectedCache[
							pass.cacheEntry ].reserved = false;
				}
				continue;
			}
			if ( !pass.cacheUpdate ) {
				continue;
			}

			if ( light.pointLight ) {
				vkPointShadowCacheEntry_t &cache =
						vkShadow.pointCache[ pass.cacheEntry ];
				if ( cache.cube.image == VK_NULL_HANDLE
						|| cache.cube.layout
							!= VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ) {
					VK_ShadowMap_ClearPointEntryMetadata(
							cache );
					continue;
				}
				cache.valid = true;
				cache.reserved = false;
				cache.generation = tr.videoRestartCount;
				cache.renderWorld = viewDef->renderWorld;
				cache.lightIndex =
						VK_ShadowMap_LightIndex(
								light.vLight );
				cache.passKind =
						VK_ShadowMap_CachePassKind(
								light.vLight, receiverPass );
				cache.signature = pass.cacheSignature;
				cache.size = light.tileSize;
				cache.lastUsedFrame = tr.frameCount;
				cache.lastUpdatedFrame = tr.frameCount;
				cache.pointFar = light.pointFar;
				for ( int originIndex = 0 ; originIndex < 3 ;
						originIndex++ ) {
					cache.lightOrigin[ originIndex ] =
							light.pointLightOrigin[
									originIndex ];
				}
			} else {
				vkProjectedShadowCacheEntry_t &cache =
						vkShadow.projectedCache[
								pass.cacheEntry ];
				if ( cache.image == VK_NULL_HANDLE
						|| cache.layout
							!= VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
					VK_ShadowMap_ClearProjectedEntryMetadata(
							cache );
					continue;
				}
				cache.valid = true;
				cache.reserved = false;
				cache.generation = tr.videoRestartCount;
				cache.renderWorld = viewDef->renderWorld;
				cache.lightIndex =
						VK_ShadowMap_LightIndex(
								light.vLight );
				cache.passKind =
						VK_ShadowMap_CachePassKind(
								light.vLight, receiverPass );
				cache.signature = pass.cacheSignature;
				cache.tileSize = light.tileSize;
				cache.blockSize =
						VK_ShadowMap_ProjectedBlockSize( light );
				cache.lastUsedFrame = tr.frameCount;
				cache.lastUpdatedFrame = tr.frameCount;
				cache.projectedState = light.projectedState;
			}
		}
	}
}

bool VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef ) {
	const bool classicCommit = vkClassicShadowTransaction.ready
		&& vkClassicShadowTransaction.view != NULL
		&& vkClassicShadowTransaction.view->viewDef == viewDef;
	if ( viewDef == NULL || vkShadow.numLights <= 0
			|| vkShadow.atlasImage == VK_NULL_HANDLE ) {
		return false;
	}

	// A missing per-class caster pipeline or pass resource invalidates only
	// the affected ownership resource (plus any alias of it). A distinct
	// pass for the same light remains usable this frame. Exact hits require
	// neither caster representability nor a caster pipeline: those properties
	// were proven by the successful update that published the signature.
	int projectedCount = classicCommit
		? vkClassicShadowTransaction.projectedCount : 0;
	int projectedFreshCount = classicCommit
		? vkClassicShadowTransaction.projectedFreshCount : 0;
	int pointFreshCount = classicCommit
		? vkClassicShadowTransaction.pointFreshCount : 0;
	int pointHitCount = classicCommit
		? vkClassicShadowTransaction.pointHitCount : 0;
	// Projected passes whose cached tile holds static depth only and still owe
	// this view's dynamic casters a LOAD-op scope after publication/restore.
	int projectedComposeCount = 0;
	VkPipeline casterPipeline = classicCommit
		? vkClassicShadowTransaction.projectedCasterPipeline
		: VK_Exec_CasterPipeline();
	VkPipeline pointCasterPipeline = classicCommit
		? vkClassicShadowTransaction.pointCasterPipeline
		: VK_Exec_PointCasterPipeline();
	if ( !classicCommit ) {
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		vkShadowLightState_t &light = vkShadow.lights[ i ];
		if ( !light.valid ) {
			continue;
		}
		for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
			vkShadowPassState_t &pass = light.passes[ passIndex ];
			if ( !pass.valid || pass.resourcePass != (vkShadowReceiverPass_t)passIndex ) {
				continue;
			}
			const vkShadowReceiverPass_t receiverPass =
					(vkShadowReceiverPass_t)passIndex;

			if ( light.pointLight ) {
				if ( pass.cacheHit ) {
					const vkShadowReceiverPass_t cachePass =
							VK_ShadowMap_CachePassKind(
									light.vLight, receiverPass );
					const vkPointShadowCacheEntry_t *cache =
							( pass.cacheEntry >= 0
								&& pass.cacheEntry
									< VK_SHADOW_MAX_CACHE_SLOTS )
							? &vkShadow.pointCache[
									pass.cacheEntry ] : NULL;
					if ( cache == NULL || !cache->valid
							|| !cache->reserved
							|| cache->generation
								!= tr.videoRestartCount
							|| cache->renderWorld
								!= viewDef->renderWorld
							|| cache->lightIndex
								!= VK_ShadowMap_LightIndex(
										light.vLight )
							|| cache->passKind != cachePass
							|| cache->signature
								!= pass.cacheSignature
							|| cache->size != light.tileSize
							|| cache->cube.image
								== VK_NULL_HANDLE
							|| cache->cube.layout
								!= VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ) {
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
						continue;
					}
					pointHitCount++;
					continue;
				}

				const vkPointShadowCube_t *cube = NULL;
				if ( pass.cacheUpdate && pass.cacheEntry >= 0
						&& pass.cacheEntry
							< VK_SHADOW_MAX_CACHE_SLOTS ) {
					if ( vkShadow.pointCache[
							pass.cacheEntry ].reserved ) {
						cube = &vkShadow.pointCache[
								pass.cacheEntry ].cube;
					}
				} else if ( pass.cubeIndex >= 0
						&& pass.cubeIndex
							< VK_SHADOW_MAX_POINT_CUBES ) {
					cube = &vkShadow.pointCubes[
							pass.cubeIndex ];
				}
				if ( pointCasterPipeline == VK_NULL_HANDLE
						|| cube == NULL
						|| cube->image == VK_NULL_HANDLE
						|| !VK_ShadowMap_PassCastersRepresentable(
								light, receiverPass ) ) {
					VK_ShadowMap_InvalidatePassResource( light, receiverPass );
					continue;
				}
				pointFreshCount++;
			} else {
				if ( pass.cacheHit ) {
					const vkShadowReceiverPass_t cachePass =
							VK_ShadowMap_CachePassKind(
									light.vLight, receiverPass );
					const vkProjectedShadowCacheEntry_t *cache =
							( pass.cacheEntry >= 0
								&& pass.cacheEntry
									< VK_SHADOW_MAX_CACHE_SLOTS )
							? &vkShadow.projectedCache[
									pass.cacheEntry ] : NULL;
					if ( cache == NULL || !cache->valid
							|| !cache->reserved
							|| cache->generation
								!= tr.videoRestartCount
							|| cache->renderWorld
								!= viewDef->renderWorld
							|| cache->lightIndex
								!= VK_ShadowMap_LightIndex(
										light.vLight )
							|| cache->passKind != cachePass
							|| cache->signature
								!= pass.cacheSignature
							|| cache->tileSize != light.tileSize
							|| cache->blockSize
								!= VK_ShadowMap_ProjectedBlockSize( light )
							|| cache->image == VK_NULL_HANDLE
							|| cache->layout
								!= VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
					} else if ( pass.composeDynamic
							&& ( casterPipeline == VK_NULL_HANDLE
								|| !VK_ShadowMap_PassCastersRepresentable(
										light, receiverPass ) ) ) {
						// An exact hit normally needs neither a caster
						// pipeline nor representability, because the update
						// that published the signature already proved both.
						// A composed hit still has live dynamic casters to
						// draw, so it needs both again.
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
					} else {
						projectedCount++;
						if ( pass.composeDynamic ) {
							projectedComposeCount++;
						}
					}
					continue;
				}
				if ( casterPipeline == VK_NULL_HANDLE
						|| !VK_ShadowMap_PassCastersRepresentable(
								light, receiverPass ) ) {
					VK_ShadowMap_InvalidatePassResource( light, receiverPass );
					continue;
				}
				if ( pass.cacheUpdate
						&& ( pass.cacheEntry < 0
							|| pass.cacheEntry
								>= VK_SHADOW_MAX_CACHE_SLOTS
							|| !vkShadow.projectedCache[
									pass.cacheEntry ].reserved
							|| vkShadow.projectedCache[
									pass.cacheEntry ].image
								== VK_NULL_HANDLE ) ) {
					VK_ShadowMap_InvalidatePassResource(
							light, receiverPass );
					continue;
				}
				projectedCount++;
				projectedFreshCount++;
				if ( pass.composeDynamic ) {
					projectedComposeCount++;
				}
			}
		}
	}
	}
	if ( projectedCount + pointFreshCount + pointHitCount == 0 ) {
		return false;
	}

	// A view made exclusively of resident point hits performs no GPU work:
	// their identity-owned descriptor cubes are already sampleable.
	if ( projectedCount == 0 && pointFreshCount == 0
			&& pointHitCount > 0 ) {
		VK_ShadowMap_FinalizeCachePasses( viewDef );
		return true;
	}

	VkCommandBuffer cmd = classicCommit
		? vkClassicShadowTransaction.cmd : VK_Exec_ActiveCmd();
	VkDescriptorSet whiteSet = classicCommit
		? vkClassicShadowTransaction.whiteSet : VK_NULL_HANDLE;
	if ( !classicCommit && projectedFreshCount + pointFreshCount > 0
			&& globalImages->whiteImage != NULL ) {
		whiteSet = VK_Exec_ImageDescriptor(
				globalImages->whiteImage->GetDeviceHandle(), true );
	}
	if ( cmd == VK_NULL_HANDLE
			|| ( projectedFreshCount + pointFreshCount > 0
				&& whiteSet == VK_NULL_HANDLE ) ) {
		VK_ShadowMap_AbandonPreparedLights();
		return false;
	}

	VK_Exec_EndMainRendering();

	vkCasterPassCtx_t ctx;
	memset( &ctx, 0, sizeof( ctx ) );
	ctx.cmd = cmd;
	ctx.slot = classicCommit ? vkClassicShadowTransaction.frameSlot
		: VK_Exec_ActiveFrameSlot();
	ctx.layout = classicCommit ? vkClassicShadowTransaction.layout
		: VK_Exec_BasePipelineLayout();
	ctx.whiteSet = whiteSet;
	ctx.boundCullMode = (VkCullModeFlags)~0u;
	// r_shadowMapDebugMode 10 isolates the caster-side offset from receiver
	// bias by zeroing it, exactly as RB_ShadowMapPolygonFactor/-Offset do.
	const bool casterOffsetOff = idMath::ClampInt( 0,
			SHADOWMAP_DEBUGMODE_COUNT - 1,
			r_shadowMapDebugMode.GetInteger() )
				== SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF;
	ctx.slopeFactor = casterOffsetOff
			? 0.0f : r_shadowMapPolygonFactor.GetFloat();
	// pre-scaled to one resolvable depth-buffer step (glPolygonOffset parity)
	ctx.constOffset = ( casterOffsetOff
			? 0.0f : r_shadowMapPolygonOffset.GetFloat() )
			* ( 1.0f / 16777216.0f );

	// Resolve whatever the GPU finished since the last view and start this
	// view's report. Every span below opens outside a dynamic-rendering
	// scope, because resetting its query pair inside one is illegal.
	VK_ShadowGpuTiming_BeginView( ctx.slot );

	if ( projectedCount > 0 ) {
		VkImageLayout atlasLayout = vkShadow.atlasLayout;
		if ( projectedFreshCount > 0 ) {
			VK_ShadowGpuTiming_BeginPhase( cmd,
					VK_SHADOW_TIMING_MAP_RENDER );
			VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,
					atlasLayout,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
							? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
							: VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
							? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
						| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
			atlasLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			// One depth-only scope over the whole atlas; the clear also
			// establishes transparent/zero-alpha ownership maps as valid
			// empty depth instead of spuriously falling back to stencil.
			VkRenderingAttachmentInfo depth;
			memset( &depth, 0, sizeof( depth ) );
			depth.sType =
					VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth.imageView = vkShadow.atlasAttachmentView;
			depth.imageLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depth.clearValue.depthStencil.depth = 1.0f;
			depth.clearValue.depthStencil.stencil = 0;

			VkRenderingInfo ri;
			memset( &ri, 0, sizeof( ri ) );
			ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			ri.renderArea.extent.width =
					(uint32_t)vkShadow.atlasSize;
			ri.renderArea.extent.height =
					(uint32_t)vkShadow.atlasSize;
			ri.layerCount = 1;
			ri.pDepthAttachment = &depth;
			ri.pStencilAttachment =
					vkCtx.shadowDepthHasStencil ? &depth : NULL;
			vkCmdBeginRendering( cmd, &ri );

			vkCmdBindPipeline( cmd,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					casterPipeline );
			vkCmdSetDepthTestEnable( cmd, VK_TRUE );
			vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
			vkCmdSetDepthCompareOp( cmd,
					VK_COMPARE_OP_LESS_OR_EQUAL );
			vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
			vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
			vkCmdSetStencilTestEnable( cmd, VK_FALSE );
			vkCmdSetFrontFace( cmd,
					VK_FRONT_FACE_COUNTER_CLOCKWISE );

			for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
				vkShadowLightState_t &light =
						vkShadow.lights[ i ];
				if ( !light.valid || light.pointLight ) {
					continue;
				}
				for ( int passIndex = 0 ;
						passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
						passIndex++ ) {
					const vkShadowReceiverPass_t receiverPass =
							(vkShadowReceiverPass_t)passIndex;
					vkShadowPassState_t &pass =
							light.passes[ passIndex ];
					if ( !pass.valid || pass.cacheHit
							|| pass.resourcePass
								!= receiverPass ) {
						continue;
					}
					const vkClassicShadowPassPlan_t *classicPass =
						classicCommit ? VK_ClassicShadow_FindPassPlan(
							&light, receiverPass ) : NULL;

					ctx.unsupportedCaster = false;
					int drawnCasters = 0;
					const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, light.projectedState.cascadeCount );
					const int atlasDiv = idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );
					vkShadow.atlasTilesRendered += cascadeCount;
					vkShadow.atlasTilesAllocated += atlasDiv * atlasDiv;
					for ( int cascadeIndex = 0 ;
							cascadeIndex < cascadeCount ;
							cascadeIndex++ ) {
						const int cascadeTileX = pass.tileX
								+ ( cascadeIndex % atlasDiv )
									* light.tileSize;
						const int cascadeTileY = pass.tileY
								+ ( cascadeIndex / atlasDiv )
									* light.tileSize;

						VkViewport viewport;
						viewport.x = (float)cascadeTileX;
						viewport.y =
								(float)( cascadeTileY
									+ light.tileSize );
						viewport.width =
								(float)light.tileSize;
						viewport.height =
								-(float)light.tileSize;
						viewport.minDepth = 0.0f;
						viewport.maxDepth = 1.0f;
						vkCmdSetViewport( cmd, 0, 1,
								&viewport );

						VkRect2D scissor;
						scissor.offset.x = cascadeTileX;
						scissor.offset.y = cascadeTileY;
						scissor.extent.width =
								(uint32_t)light.tileSize;
						scissor.extent.height =
								(uint32_t)light.tileSize;
						vkCmdSetScissor( cmd, 0, 1,
								&scissor );

						if ( classicCommit ) {
							drawnCasters +=
								VK_ClassicShadow_DrawProjectedPass(
									ctx, *classicPass, cascadeIndex );
						} else {
							// A pass that will publish this tile as a cache
							// entry renders static depth only; its dynamics
							// compose over the published copy below.
							drawnCasters +=
									VK_ShadowMap_DrawPassCasters(
											ctx, light, receiverPass,
											cascadeIndex,
											pass.composeDynamic
												? VK_SHADOW_CHAINS_STATIC_ONLY
												: VK_SHADOW_CHAINS_ALL );
						}
					}
					// A fully transparent represented perforated chain emits
					// zero draws by design; the clear map is still complete.
					if ( !classicCommit && ctx.unsupportedCaster ) {
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
					}

					static bool loggedFirstShadowPass = false;
					if ( !loggedFirstShadowPass && pass.valid ) {
						loggedFirstShadowPass = true;
						common->Printf(
								"Vulkan: first shadow-map pass: %d prepared lights, %s %d cascade(s), tile %d block at (%d,%d) of %d\n",
								vkShadow.numLights,
								receiverPass
									== VK_SHADOW_RECEIVER_LOCAL
										? "LOCAL" : "GLOBAL",
								cascadeCount, light.tileSize,
								pass.tileX, pass.tileY,
								vkShadow.atlasSize );
					}
					(void)drawnCasters;
				}
			}
			vkCmdEndRendering( cmd );
			VK_ShadowGpuTiming_EndPhase( cmd,
					VK_SHADOW_TIMING_MAP_RENDER );
		} else {
			VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,
					atlasLayout,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
							? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
							: VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
							? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					VK_ACCESS_2_TRANSFER_WRITE_BIT );
			atlasLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		bool haveCacheUpdates = false;
		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			const vkShadowLightState_t &light =
					vkShadow.lights[ i ];
			if ( !light.valid || light.pointLight ) {
				continue;
			}
			for ( int passIndex = 0 ;
					passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
					passIndex++ ) {
				const vkShadowPassState_t &pass =
						light.passes[ passIndex ];
				if ( pass.valid && pass.cacheUpdate
						&& pass.resourcePass
							== (vkShadowReceiverPass_t)passIndex ) {
					haveCacheUpdates = true;
				}
			}
		}
		bool haveCacheHits = false;
		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			const vkShadowLightState_t &light =
					vkShadow.lights[ i ];
			if ( !light.valid || light.pointLight ) {
				continue;
			}
			for ( int passIndex = 0 ;
					passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
					passIndex++ ) {
				const vkShadowPassState_t &pass =
						light.passes[ passIndex ];
				haveCacheHits = haveCacheHits
						|| ( pass.valid && pass.cacheHit
							&& pass.resourcePass
								== (vkShadowReceiverPass_t)passIndex );
			}
		}
		// Publication and restore are one phase: both are resident-cache
		// transfers, and OpenGL reports them together as reuse.
		if ( haveCacheUpdates || haveCacheHits ) {
			VK_ShadowGpuTiming_BeginPhase( cmd,
					VK_SHADOW_TIMING_CACHE_REUSE );
		}
		if ( haveCacheUpdates ) {
			const VkPipelineStageFlags2 srcStage =
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
					? ( VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT )
					: VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			const VkAccessFlags2 srcAccess =
					atlasLayout
						== VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
					? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
					: VK_ACCESS_2_TRANSFER_WRITE_BIT;
			VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,
					atlasLayout,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					srcStage, srcAccess,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					VK_ACCESS_2_TRANSFER_READ_BIT );
			atlasLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

			for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
				vkShadowLightState_t &light =
						vkShadow.lights[ i ];
				if ( !light.valid || light.pointLight ) {
					continue;
				}
				for ( int passIndex = 0 ;
						passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
						passIndex++ ) {
					const vkShadowReceiverPass_t receiverPass =
							(vkShadowReceiverPass_t)passIndex;
					vkShadowPassState_t &pass =
							light.passes[ passIndex ];
					if ( !pass.valid || !pass.cacheUpdate
							|| pass.resourcePass
								!= receiverPass
							|| pass.cacheEntry < 0
							|| pass.cacheEntry
								>= VK_SHADOW_MAX_CACHE_SLOTS ) {
						continue;
					}
					vkProjectedShadowCacheEntry_t &cache =
							vkShadow.projectedCache[
									pass.cacheEntry ];
					const VkImageLayout oldCacheLayout =
							cache.layout;
					VK_ShadowMap_ImageBarrier( cmd, cache.image, 1,
							oldCacheLayout,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							oldCacheLayout
								== VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
									? VK_PIPELINE_STAGE_2_TRANSFER_BIT
									: VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
							oldCacheLayout
								== VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
									? VK_ACCESS_2_TRANSFER_READ_BIT : 0,
							VK_PIPELINE_STAGE_2_TRANSFER_BIT,
							VK_ACCESS_2_TRANSFER_WRITE_BIT );
					VK_ShadowMap_CopyDepthTile( cmd,
							vkShadow.atlasImage,
							pass.tileX, pass.tileY,
							cache.image, 0, 0,
							VK_ShadowMap_ProjectedBlockSize(
									light ) );
					VK_ShadowMap_ImageBarrier( cmd, cache.image, 1,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							VK_PIPELINE_STAGE_2_TRANSFER_BIT,
							VK_ACCESS_2_TRANSFER_WRITE_BIT,
							VK_PIPELINE_STAGE_2_TRANSFER_BIT,
							VK_ACCESS_2_TRANSFER_READ_BIT );
					cache.layout =
							VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				}
			}
		}

		if ( haveCacheHits ) {
			if ( atlasLayout
					!= VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ) {
				const bool fromAttachment =
						atlasLayout
							== VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				VK_ShadowMap_ImageBarrier( cmd,
						vkShadow.atlasImage, 1,
						atlasLayout,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						fromAttachment
							? ( VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
								| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT )
							: VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						fromAttachment
							? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
							: VK_ACCESS_2_TRANSFER_READ_BIT,
						VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						VK_ACCESS_2_TRANSFER_WRITE_BIT );
				atlasLayout =
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			}
			for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
				vkShadowLightState_t &light =
						vkShadow.lights[ i ];
				if ( !light.valid || light.pointLight ) {
					continue;
				}
				for ( int passIndex = 0 ;
						passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
						passIndex++ ) {
					const vkShadowReceiverPass_t receiverPass =
							(vkShadowReceiverPass_t)passIndex;
					vkShadowPassState_t &pass =
							light.passes[ passIndex ];
					if ( !pass.valid || !pass.cacheHit
							|| pass.resourcePass
								!= receiverPass
							|| pass.cacheEntry < 0
							|| pass.cacheEntry
								>= VK_SHADOW_MAX_CACHE_SLOTS ) {
						continue;
					}
					vkProjectedShadowCacheEntry_t &cache =
							vkShadow.projectedCache[
									pass.cacheEntry ];
					VK_ShadowMap_CopyDepthTile( cmd,
							cache.image, 0, 0,
							vkShadow.atlasImage,
							pass.tileX, pass.tileY,
							VK_ShadowMap_ProjectedBlockSize(
									light ) );
				}
			}
		}

		if ( haveCacheUpdates || haveCacheHits ) {
			VK_ShadowGpuTiming_EndPhase( cmd,
					VK_SHADOW_TIMING_CACHE_REUSE );
		}

		// GL SHADOWMAP_RENDER_COMPOSE_DYNAMIC (draw_arb2.cpp): every cached
		// tile now holds this light's STATIC depth, whether it was just
		// published or restored from a resident entry. Re-enter the atlas with
		// loadOp LOAD and draw only the dynamic chains over it, so a moving
		// caster costs one copy plus its own draws instead of a full static
		// re-render. The cascade clip state is the light's own, so the dynamics
		// land in exactly the projection the static tiles were rendered with.
		if ( projectedComposeCount > 0
				&& casterPipeline != VK_NULL_HANDLE ) {
			// OpenGL times compose under MAP_RENDER too; the composed=
			// counter is what separates it in the report.
			VK_ShadowGpuTiming_BeginPhase( cmd,
					VK_SHADOW_TIMING_MAP_RENDER );
			// The tiles reaching this scope were produced by depth writes,
			// cache-publication transfer reads, or resident-restore transfer
			// writes. One layout cannot describe every producer, so take the
			// union like the closing transition does.
			VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,
					atlasLayout,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
						| VK_ACCESS_2_TRANSFER_READ_BIT
						| VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
						| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
			atlasLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkRenderingAttachmentInfo depth;
			memset( &depth, 0, sizeof( depth ) );
			depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth.imageView = vkShadow.atlasAttachmentView;
			depth.imageLayout =
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

			VkRenderingInfo ri;
			memset( &ri, 0, sizeof( ri ) );
			ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			ri.renderArea.extent.width = (uint32_t)vkShadow.atlasSize;
			ri.renderArea.extent.height = (uint32_t)vkShadow.atlasSize;
			ri.layerCount = 1;
			ri.pDepthAttachment = &depth;
			ri.pStencilAttachment =
					vkCtx.shadowDepthHasStencil ? &depth : NULL;
			vkCmdBeginRendering( cmd, &ri );

			// The fresh scope may not have run at all (a view of pure cache
			// hits), so re-establish the whole caster state here.
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					casterPipeline );
			vkCmdSetDepthTestEnable( cmd, VK_TRUE );
			vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
			vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
			vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
			vkCmdSetStencilTestEnable( cmd, VK_FALSE );
			vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
			ctx.boundCullMode = (VkCullModeFlags)~0u;

			for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
				vkShadowLightState_t &light = vkShadow.lights[ i ];
				if ( !light.valid || light.pointLight ) {
					continue;
				}
				for ( int passIndex = 0 ;
						passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ;
						passIndex++ ) {
					const vkShadowReceiverPass_t receiverPass =
							(vkShadowReceiverPass_t)passIndex;
					vkShadowPassState_t &pass =
							light.passes[ passIndex ];
					if ( !pass.valid || !pass.composeDynamic
							|| pass.resourcePass != receiverPass ) {
						continue;
					}
					const int cascadeCount = idMath::ClampInt( 1,
							SHADOWMAP_PROJECTED_MAX_CASCADES,
							light.projectedState.cascadeCount );
					const int atlasDiv = idMath::ClampInt( 1, 2,
							light.projectedState.atlasDiv );
					ctx.unsupportedCaster = false;
					for ( int cascadeIndex = 0 ;
							cascadeIndex < cascadeCount ;
							cascadeIndex++ ) {
						const int cascadeTileX = pass.tileX
								+ ( cascadeIndex % atlasDiv )
									* light.tileSize;
						const int cascadeTileY = pass.tileY
								+ ( cascadeIndex / atlasDiv )
									* light.tileSize;

						VkViewport viewport;
						viewport.x = (float)cascadeTileX;
						viewport.y = (float)( cascadeTileY
								+ light.tileSize );
						viewport.width = (float)light.tileSize;
						viewport.height = -(float)light.tileSize;
						viewport.minDepth = 0.0f;
						viewport.maxDepth = 1.0f;
						vkCmdSetViewport( cmd, 0, 1, &viewport );

						VkRect2D scissor;
						scissor.offset.x = cascadeTileX;
						scissor.offset.y = cascadeTileY;
						scissor.extent.width =
								(uint32_t)light.tileSize;
						scissor.extent.height =
								(uint32_t)light.tileSize;
						vkCmdSetScissor( cmd, 0, 1, &scissor );

						VK_ShadowMap_DrawPassCasters( ctx, light,
								receiverPass, cascadeIndex,
								VK_SHADOW_CHAINS_DYNAMIC_ONLY );
					}
					// The static half of this map is already published or
					// resident and correct; only the composed dynamics can
					// fail here, and a partial compose would shadow some
					// receivers while leaking others.
					if ( ctx.unsupportedCaster ) {
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
					}
				}
			}
			vkCmdEndRendering( cmd );
			VK_ShadowGpuTiming_EndPhase( cmd,
					VK_SHADOW_TIMING_MAP_RENDER );
			vkShadow.composePasses += projectedComposeCount;
		}

		// The atlas can contain tiles whose last meaningful access came from
		// different stages in this scope: fresh depth writes, resident-cache
		// transfer writes, and cache-publication transfer reads. The whole
		// image has one current layout, but that layout alone cannot describe
		// every tile's producer, so close the scope with their union.
		VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,
				atlasLayout,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
					| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
					| VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
					| VK_ACCESS_2_TRANSFER_READ_BIT
					| VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT );
		vkShadow.atlasLayout =
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	}

	if ( pointFreshCount > 0 ) {
		VK_ShadowGpuTiming_BeginPhase( cmd, VK_SHADOW_TIMING_MAP_RENDER );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pointCasterPipeline );
		vkCmdSetDepthTestEnable( cmd, VK_TRUE );
		vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
		vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
		vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
		vkCmdSetStencilTestEnable( cmd, VK_FALSE );
		// POSITIVE-height viewport keeps the GL cube-face row mapping (NDC
		// y=-1 lands on texel row 0, exactly the GL FBO viewport transform),
		// which makes the GL winding convention CLOCKWISE in Vulkan
		// framebuffer terms — the material cull mapping
		// (VK_ShadowMap_CasterCullMode) is unchanged under CW front
		vkCmdSetFrontFace( cmd, VK_FRONT_FACE_CLOCKWISE );

		const int faceSize = vkShadow.pointCubeFaceSize;
		VkViewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)faceSize;
		viewport.height = (float)faceSize;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );

		VkRect2D scissor;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		scissor.extent.width = (uint32_t)faceSize;
		scissor.extent.height = (uint32_t)faceSize;
		vkCmdSetScissor( cmd, 0, 1, &scissor );

		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			vkShadowLightState_t &light = vkShadow.lights[ i ];
			if ( !light.valid || !light.pointLight ) {
				continue;
			}
			const vkClassicShadowPassPlan_t *classicPointState = NULL;
			if ( classicCommit ) {
				classicPointState = VK_ClassicShadow_FindPassPlan( &light,
					VK_SHADOW_RECEIVER_LOCAL );
				if ( classicPointState == NULL ) {
					classicPointState = VK_ClassicShadow_FindPassPlan( &light,
						VK_SHADOW_RECEIVER_GLOBAL );
				}
				if ( classicPointState == NULL ) {
					common->FatalError(
						"Vulkan: sealed point shadow state was lost after preflight" );
				}
			}
			// near/far + the analytic face projection in the shared
			// VK_FixupClipSpaceZ convention: z_clip = zA*z_eye + zB*w_eye,
			// w_clip = -z_eye (RB_PointShadowMapBuildProjectionMatrix).
			// Shared commits derive far distance from the sealed point state.
			const float farClip = classicCommit
				? classicPointState->pass->point.farDistance
				: light.pointFar;
			// Radial depth is written by the fragment shader, so a tiny near
			// plane does not sacrifice stored-depth precision. Keep clipping
			// enabled: depth-clamped world triangles crossing the light plane
			// produced false near occluders in Air Defense 2 on native Vulkan.
			// A conventional 4/16-unit near plane would instead lose nearby
			// casters; this plane only excludes the immediate face apex.
			const float nearClip = 0.01f;
			const float projA = -( farClip + nearClip ) / ( farClip - nearClip );
			const float projB = -( 2.0f * farClip * nearClip ) / ( farClip - nearClip );
			float projRow[ 2 ];
			projRow[ 0 ] = 0.5f * ( projA - 1.0f );
			projRow[ 1 ] = 0.5f * projB;

			for ( int passIndex = 0 ; passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
				const vkShadowReceiverPass_t receiverPass = (vkShadowReceiverPass_t)passIndex;
				vkShadowPassState_t &pass = light.passes[ passIndex ];
				if ( !pass.valid || pass.cacheHit
						|| pass.resourcePass != receiverPass ) {
					continue;
				}
				const vkClassicShadowPassPlan_t *classicPass =
					classicCommit ? VK_ClassicShadow_FindPassPlan(
						&light, receiverPass ) : NULL;
				vkPointShadowCube_t *cube = NULL;
				if ( pass.cacheUpdate && pass.cacheEntry >= 0
						&& pass.cacheEntry
							< VK_SHADOW_MAX_CACHE_SLOTS ) {
					cube = &vkShadow.pointCache[
							pass.cacheEntry ].cube;
				} else if ( pass.cubeIndex >= 0
						&& pass.cubeIndex
							< VK_SHADOW_MAX_POINT_CUBES ) {
					cube = &vkShadow.pointCubes[
							pass.cubeIndex ];
				}
				if ( cube == NULL ) {
					if ( !classicCommit ) {
						VK_ShadowMap_InvalidatePassResource(
								light, receiverPass );
					}
					continue;
				}

				const VkImageLayout oldCubeLayout = cube->layout;
				VK_ShadowMap_ImageBarrier( cmd, cube->image, 6,
						oldCubeLayout,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
						oldCubeLayout
							== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
								? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
								: VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
						oldCubeLayout
							== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
								? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
						VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
							| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
							| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );

				ctx.unsupportedCaster = false;
				int drawnCasters = 0;
				const idVec3 pointOrigin(
						classicPass != NULL
							? classicPass->pass->point.lightOrigin[ 0 ]
							: light.pointLightOrigin[ 0 ],
						classicPass != NULL
							? classicPass->pass->point.lightOrigin[ 1 ]
							: light.pointLightOrigin[ 1 ],
						classicPass != NULL
							? classicPass->pass->point.lightOrigin[ 2 ]
							: light.pointLightOrigin[ 2 ] );
				vkShadow.pointFacesRendered += 6;
				for ( int cubeFace = 0 ; cubeFace < 6 ; cubeFace++ ) {
					float faceViewMatrix[ 16 ];
					VK_ShadowMap_PointFaceViewMatrix(
							pointOrigin, cubeFace,
							faceViewMatrix );

					VkRenderingAttachmentInfo depth;
					memset( &depth, 0, sizeof( depth ) );
					depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
					depth.imageView = cube->faceViews[ cubeFace ];
					depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
					depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					depth.clearValue.depthStencil.depth = 1.0f;
					depth.clearValue.depthStencil.stencil = 0;

					VkRenderingInfo ri;
					memset( &ri, 0, sizeof( ri ) );
					ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
					ri.renderArea.extent.width = (uint32_t)faceSize;
					ri.renderArea.extent.height = (uint32_t)faceSize;
					ri.layerCount = 1;
					ri.pDepthAttachment = &depth;
					ri.pStencilAttachment = vkCtx.shadowDepthHasStencil ? &depth : NULL;
					vkCmdBeginRendering( cmd, &ri );

					if ( classicCommit ) {
						drawnCasters += VK_ClassicShadow_DrawPointPass(
							ctx, *classicPass, faceViewMatrix, projRow );
					} else {
					const viewLight_t *vLight = light.vLight;
					drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, vLight, faceViewMatrix,
							projRow, farClip, cubeFace, vLight->globalShadowMapCasters );
					drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, vLight, faceViewMatrix,
							projRow, farClip, cubeFace, vLight->globalShadowMapDynamicCasters );
					if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL ) {
						drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, vLight, faceViewMatrix,
								projRow, farClip, cubeFace, vLight->localShadowMapCasters );
						drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, vLight, faceViewMatrix,
								projRow, farClip, cubeFace, vLight->localShadowMapDynamicCasters );
					}
					}

					vkCmdEndRendering( cmd );
				}

				// Zero emitted draws can be a represented fully transparent
				// perforated chain; its cleared six faces are valid.
				if ( !classicCommit && ctx.unsupportedCaster ) {
					VK_ShadowMap_InvalidatePassResource( light, receiverPass );
				}

				// Unconditional: the image must end in the layout its
				// ownership-specific descriptor sets declare.
				VK_ShadowMap_ImageBarrier( cmd, cube->image, 6,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
						VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
							| VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
						VK_ACCESS_2_SHADER_SAMPLED_READ_BIT );
				cube->layout =
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

				// One-shot bring-up evidence the split point pass emitted
				// real work.
				static bool loggedFirstPointShadow = false;
				if ( !loggedFirstPointShadow && pass.valid ) {
					loggedFirstPointShadow = true;
					common->Printf( "Vulkan: first point shadow pass: %d prepared maps, %s, %d cube faces, far %.1f, %d casters\n",
							pointFreshCount,
							receiverPass == VK_SHADOW_RECEIVER_LOCAL ? "LOCAL" : "GLOBAL",
							faceSize, farClip, drawnCasters );
				}
			}
		}
		VK_ShadowGpuTiming_EndPhase( cmd, VK_SHADOW_TIMING_MAP_RENDER );
	}

	// order the suspended main scope's color/depth attachment writes against
	// the resumed scope's loads (dynamic rendering scopes do not sync
	// themselves)
	{
		VkMemoryBarrier2 attachmentOrder;
		memset( &attachmentOrder, 0, sizeof( attachmentOrder ) );
		attachmentOrder.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		attachmentOrder.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		attachmentOrder.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		attachmentOrder.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		attachmentOrder.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
				| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

		VkDependencyInfo dep;
		memset( &dep, 0, sizeof( dep ) );
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.memoryBarrierCount = 1;
		dep.pMemoryBarriers = &attachmentOrder;
		vkCmdPipelineBarrier2( cmd, &dep );
	}

	const bool resumedMainRendering = VK_Exec_BeginMainRendering( false );
	if ( !resumedMainRendering ) {
		// This command buffer will not be submitted, so its timestamps will
		// never resolve; a synchronized read would wait on them forever.
		VK_ShadowGpuTiming_AbandonView();
		VK_ShadowMap_AbandonPreparedLights();
		common->Warning( "Vulkan: failed to resume the main rendering scope after shadow maps" );
	} else {
		// Publish resident metadata only after all copy/render commands are
		// recorded and the main interaction scope is live again.
		VK_ShadowMap_FinalizeCachePasses( viewDef );
	}
	(void)viewDef;
	return resumedMainRendering;
}

void VK_ShadowMap_CommitClassicInteractionView(
		const classicInteractionDomainView_t *view ) {
	if ( view == NULL || !vkClassicShadowTransaction.active
			|| !vkClassicShadowTransaction.ready
			|| vkClassicShadowTransaction.view != view ) {
		return;
	}
	if ( view->shadowMapPassCount == 0 ) {
		vkClassicShadowTransaction.active = false;
		vkClassicShadowTransaction.ready = false;
		vkClassicShadowTransaction.view = NULL;
		return;
	}

	// Every command/resource/descriptor decision was retained by Preflight.
	// RenderAtlas recognizes this transaction and replaces every legacy caster
	// walk with the sealed caster/alpha plans above.
	const bool committed = VK_ShadowMap_RenderAtlas( view->viewDef );
	if ( !committed ) {
		common->FatalError(
			"Vulkan: preflighted classic shadow transaction could not commit" );
	}
	vkClassicShadowTransaction.active = false;
	vkClassicShadowTransaction.ready = false;
	vkClassicShadowTransaction.view = NULL;
}

/*
===============================================================================

	r_shadowMapDebugOverlay (OpenGL RB_ShadowMapDebugOverlayDraw parity)

	A top-left mini-map of one shadow map plus a stats readout, drawn at the
	end of the interaction pass into the live scene exactly where the OpenGL
	overlay draws it.

	Unlike the OpenGL driver, nothing is captured while the shadow pass runs:
	the per-view light table survives until the next PrepareViewLights, so the
	overlay selects from it afterwards and reads the same state the receivers
	sampled. The prepared view and frame identity make that safe — a view that
	prepared no lights (or handed the pass back) must not display the previous
	view's table.

	Geometry is generated in the vertex shader from gl_VertexIndex, so the
	overlay allocates nothing: every rectangle is one push-constant block and
	one six-vertex draw.

===============================================================================
*/

typedef struct vkShadowOverlayPush_s {
	float			rect[ 4 ];
	float			uvRect[ 4 ];
	float			color[ 4 ];
	float			params[ 4 ];
	float			screen[ 4 ];
} vkShadowOverlayPush_t;

// params.x sentinel: fill the rectangle with color instead of a font cell
static const float VK_SHADOW_OVERLAY_SOLID = -1.0f;

typedef struct vkShadowOverlaySelection_s {
	bool			valid;
	bool			pointLight;
	bool			globalPass;		// the shown resource belongs to GLOBAL receivers
	int				lightDefIndex;
	int				tileCount;		// cascades inside the block, or 6 cube faces
	int				atlasDiv;
	int				tileSize;
	int				staticCasters;
	int				dynamicCasters;
	int				alphaCasters;
	const char *	outcome;		// how this pass got its contents
	VkDescriptorSet	panelSet;
	float			uvRect[ 4 ];	// image-oriented block rect (identity for cubes)
} vkShadowOverlaySelection_t;

typedef struct vkShadowOverlayDraw_s {
	VkCommandBuffer		cmd;
	VkPipelineLayout	layout;
	VkPipeline			panelPipeline;
	VkPipeline			textPipeline;
	VkPipeline			boundPipeline;
	float				screen[ 2 ];
} vkShadowOverlayDraw_t;

// Compact font slot for shadow_debug_overlay_text.frag: space, 0-9, A-Z, and
// the punctuation the readout uses. Everything else renders blank rather than
// a wrong glyph.
static int VK_ShadowMap_OverlayGlyphSlot( const char c ) {
	if ( c >= '0' && c <= '9' ) {
		return 1 + ( c - '0' );
	}
	if ( c >= 'A' && c <= 'Z' ) {
		return 11 + ( c - 'A' );
	}
	if ( c >= 'a' && c <= 'z' ) {
		return 11 + ( c - 'a' );
	}
	switch ( c ) {
	case '/':	return 37;
	case '-':	return 38;
	case '.':	return 39;
	case ':':	return 40;
	case '?':	return 41;
	case '%':	return 42;
	case '+':	return 43;
	default:	return 0;
	}
}

static void VK_ShadowMap_OverlayQuad( vkShadowOverlayDraw_t &draw,
		VkPipeline pipeline, const float x, const float y,
		const float w, const float h, const idVec4 &color,
		const float glyphSlot, const float divisions,
		const float activeTiles, const float uvRect[ 4 ] ) {
	if ( pipeline == VK_NULL_HANDLE ) {
		return;
	}
	if ( pipeline != draw.boundPipeline ) {
		vkCmdBindPipeline( draw.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		draw.boundPipeline = pipeline;
	}

	vkShadowOverlayPush_t push;
	memset( &push, 0, sizeof( push ) );
	push.rect[ 0 ] = x;
	push.rect[ 1 ] = y;
	push.rect[ 2 ] = w;
	push.rect[ 3 ] = h;
	if ( uvRect != NULL ) {
		for ( int i = 0 ; i < 4 ; i++ ) {
			push.uvRect[ i ] = uvRect[ i ];
		}
	} else {
		push.uvRect[ 2 ] = 1.0f;
		push.uvRect[ 3 ] = 1.0f;
	}
	for ( int i = 0 ; i < 4 ; i++ ) {
		push.color[ i ] = color[ i ];
	}
	push.params[ 0 ] = glyphSlot;
	push.params[ 1 ] = divisions;
	push.params[ 2 ] = activeTiles;
	push.screen[ 0 ] = draw.screen[ 0 ];
	push.screen[ 1 ] = draw.screen[ 1 ];

	vkCmdPushConstants( draw.cmd, draw.layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	// deliberately unaccounted: the GL overlay's immediate-mode quads never
	// reach backEnd.pc either, and a diagnostic must not move the counters
	// the diagnostic is there to read
	vkCmdDraw( draw.cmd, 6, 1, 0, 0 );
}

static void VK_ShadowMap_OverlaySolid( vkShadowOverlayDraw_t &draw,
		const float x, const float y, const float w, const float h,
		const idVec4 &color ) {
	VK_ShadowMap_OverlayQuad( draw, draw.textPipeline, x, y, w, h, color,
			VK_SHADOW_OVERLAY_SOLID, 1.0f, 0.0f, NULL );
}

// Two passes for a dark drop shadow under the readout, as the OpenGL overlay
// does; the scene behind it is arbitrary, so unshadowed text is unreadable.
static void VK_ShadowMap_OverlayString( vkShadowOverlayDraw_t &draw,
		const float x, const float y, const float scale,
		const idVec4 &color, const char *text ) {
	if ( text == NULL || text[ 0 ] == '\0' ) {
		return;
	}

	const float glyphW = 6.0f * scale;
	const float glyphH = 9.0f * scale;
	const float advance = glyphW + scale;
	const idVec4 shadowColor( 0.0f, 0.0f, 0.0f, color[ 3 ] * 0.85f );

	for ( int pass = 0 ; pass < 2 ; pass++ ) {
		const idVec4 &passColor = ( pass == 0 ) ? shadowColor : color;
		const float offset = ( pass == 0 ) ? 1.0f : 0.0f;
		float drawX = x + offset;
		for ( const char *c = text ; *c != '\0' ; c++ ) {
			const int slot = VK_ShadowMap_OverlayGlyphSlot( *c );
			if ( slot != 0 ) {
				VK_ShadowMap_OverlayQuad( draw, draw.textPipeline, drawX,
						y + offset, glyphW, glyphH, passColor,
						(float)slot, 1.0f, 0.0f, NULL );
			}
			drawX += advance;
		}
	}
}

// How this receiver ownership got the contents the panel is showing.
static const char *VK_ShadowMap_OverlayOutcome( const vkShadowPassState_t &pass ) {
	if ( pass.cacheHit ) {
		return pass.composeDynamic ? "REUSE+DYN" : "REUSE";
	}
	if ( pass.cacheUpdate ) {
		return pass.composeDynamic ? "PUB+DYN" : "PUB";
	}
	return "SCRATCH";
}

// The point cube backing one prepared pass, or NULL. Mirrors the resolution
// the point render walk uses, so the overlay cannot sample a different image
// than the receivers did.
static const vkPointShadowCube_t *VK_ShadowMap_OverlayPassCube(
		const vkShadowPassState_t &pass ) {
	if ( pass.cacheEntry >= 0 && pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS ) {
		return &vkShadow.pointCache[ pass.cacheEntry ].cube;
	}
	if ( pass.cubeIndex >= 0 && pass.cubeIndex < VK_SHADOW_MAX_POINT_CUBES ) {
		return &vkShadow.pointCubes[ pass.cubeIndex ];
	}
	return NULL;
}

/*
====================
VK_ShadowMap_OverlaySelect

r_singleLight picks its light exactly; otherwise the first prepared light in
table order wins, which is stable across frames as long as the view is. Only
a pass that OWNS its resource is shown: an aliased pass points at the other
ownership's tile, which is already on screen under its own name.

Every candidate must also be samplable RIGHT NOW. Both descriptor set families
declare VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, so a light whose image
did not end the shadow pass in that layout is skipped rather than sampled
through a descriptor that disagrees with the image.
====================
*/
static bool VK_ShadowMap_OverlaySelect( const viewDef_t *viewDef,
		vkShadowOverlaySelection_t &selection ) {
	memset( &selection, 0, sizeof( selection ) );
	selection.lightDefIndex = -1;
	selection.outcome = "";
	selection.uvRect[ 2 ] = 1.0f;
	selection.uvRect[ 3 ] = 1.0f;

	if ( !VK_ShadowMap_ViewPrepared( viewDef ) ) {
		return false;
	}

	const int singleLight = r_singleLight.GetInteger();
	const bool atlasReadable = vkShadow.atlasImage != VK_NULL_HANDLE
			&& vkShadow.atlasLayout
				== VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	const VkDescriptorSet atlasSet = atlasReadable
			? VK_Exec_ShadowDescriptorSet() : VK_NULL_HANDLE;

	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		const vkShadowLightState_t &light = vkShadow.lights[ i ];
		if ( !light.valid || light.vLight == NULL ) {
			continue;
		}
		const int lightDefIndex = light.vLight->lightDef != NULL
				? light.vLight->lightDef->index : -1;
		if ( singleLight >= 0 && lightDefIndex != singleLight ) {
			continue;
		}

		for ( int passIndex = 0 ;
				passIndex < VK_SHADOW_RECEIVER_PASS_COUNT ; passIndex++ ) {
			const vkShadowPassState_t &pass = light.passes[ passIndex ];
			if ( !pass.valid
					|| pass.resourcePass != (vkShadowReceiverPass_t)passIndex ) {
				continue;
			}

			VkDescriptorSet panelSet = VK_NULL_HANDLE;
			if ( light.pointLight ) {
				const vkPointShadowCube_t *cube =
						VK_ShadowMap_OverlayPassCube( pass );
				if ( cube == NULL || cube->image == VK_NULL_HANDLE
						|| cube->layout
							!= VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ) {
					continue;
				}
				panelSet = pass.pointSet;
			} else {
				panelSet = atlasSet;
			}
			if ( panelSet == VK_NULL_HANDLE ) {
				continue;
			}

			selection.valid = true;
			selection.pointLight = light.pointLight;
			selection.globalPass = ( passIndex == VK_SHADOW_RECEIVER_GLOBAL );
			selection.lightDefIndex = lightDefIndex;
			selection.tileSize = light.tileSize;
			selection.outcome = VK_ShadowMap_OverlayOutcome( pass );
			selection.panelSet = panelSet;
			selection.staticCasters = light.vLight->shadowMapStaticCasterCount;
			selection.dynamicCasters = light.vLight->shadowMapDynamicCasterCount;
			selection.alphaCasters = light.vLight->shadowMapAlphaCasterCount;
			if ( light.pointLight ) {
				selection.tileCount = 6;
				selection.atlasDiv = 3;
			} else {
				selection.atlasDiv = Max( 1, light.projectedState.atlasDiv );
				selection.tileCount = idMath::ClampInt( 1,
						selection.atlasDiv * selection.atlasDiv,
						Max( 1, light.projectedState.cascadeCount ) );
				// The panel shows the light's whole atlasDiv^2 block, so the
				// shader's cascade grid lines up with the panel's own edges no
				// matter where in the atlas the block was allocated.
				const int blockSize = light.tileSize * selection.atlasDiv;
				const float invAtlas = light.invAtlasSize[ 0 ];
				selection.uvRect[ 0 ] = (float)pass.tileX * invAtlas;
				selection.uvRect[ 1 ] = (float)pass.tileY * invAtlas;
				selection.uvRect[ 2 ] =
						(float)( pass.tileX + blockSize ) * invAtlas;
				selection.uvRect[ 3 ] =
						(float)( pass.tileY + blockSize ) * invAtlas;
			}
			return true;
		}
	}
	return false;
}

/*
====================
VK_ShadowMap_DebugOverlayDraw

Called at the end of the interaction pass, inside the open main rendering
scope, by whichever walker owned the view. Issues its own full-framebuffer
positive-height viewport (the interaction pass leaves a negative-height one
that would flip the panel) and restores the view scissor behind itself,
because Vulkan cannot query the rect the shared walker last latched.
====================
*/
void VK_ShadowMap_DebugOverlayDraw( const viewDef_t *viewDef ) {
	// This hook runs after either interaction walker. Reporting at preparation
	// time always printed zero rendered faces/tiles and hid late map failures.
	const bool viewPrepared = VK_ShadowMap_ViewPrepared( viewDef );
	if ( viewPrepared ) {
		VK_ShadowMap_ReportViewCache( viewDef );
	}
	if ( !r_shadowMapDebugOverlay.GetBool() || viewDef == NULL ) {
		return;
	}

	// The shadow pass interrupts the main rendering scope and can fail to
	// resume it. Appending draws to a closed scope is invalid, so the overlay
	// asks rather than assuming the interaction pass left one open.
	if ( !VK_Exec_MainRenderingScopeOpen() ) {
		return;
	}

	vkShadowOverlayDraw_t draw;
	memset( &draw, 0, sizeof( draw ) );
	draw.cmd = VK_Exec_ActiveCmd();
	draw.layout = VK_Exec_ShadowOverlayPipelineLayout();
	draw.textPipeline = VK_Exec_ShadowOverlayTextPipeline();
	if ( draw.cmd == VK_NULL_HANDLE || draw.layout == VK_NULL_HANDLE
			|| draw.textPipeline == VK_NULL_HANDLE ) {
		return;
	}

	const int fbWidth = VK_Exec_ActiveFramebufferWidth();
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();
	if ( fbWidth <= 0 || fbHeight <= 0 ) {
		return;
	}
	draw.screen[ 0 ] = (float)fbWidth;
	draw.screen[ 1 ] = (float)fbHeight;

	vkShadowOverlaySelection_t selection;
	if ( VK_ShadowMap_OverlaySelect( viewDef, selection ) ) {
		draw.panelPipeline =
				VK_Exec_ShadowOverlayPanelPipeline( selection.pointLight );
	}
	const bool drawPanel = selection.valid
			&& draw.panelPipeline != VK_NULL_HANDLE;

	// The view counters describe the view that prepared the table; a view that
	// prepared nothing reports nothing rather than the previous view's work.

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)fbWidth;
	viewport.height = (float)fbHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( draw.cmd, 0, 1, &viewport );

	VkRect2D scissor;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = (uint32_t)fbWidth;
	scissor.extent.height = (uint32_t)fbHeight;
	vkCmdSetScissor( draw.cmd, 0, 1, &scissor );

	vkCmdSetDepthTestEnable( draw.cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( draw.cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( draw.cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetDepthBiasEnable( draw.cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( draw.cmd, VK_FALSE );
	vkCmdSetCullMode( draw.cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( draw.cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	if ( vkCtx.depthBoundsSupported ) {
		vkCmdSetDepthBoundsTestEnable( draw.cmd, VK_FALSE );
	}

	if ( drawPanel ) {
		// Both panel variants read set 0 binding 2 only, but binding 1 is the
		// receivers' shadow block, so the layout still wants its one dynamic
		// offset.
		const uint32_t dynamicOffset = 0;
		vkCmdBindDescriptorSets( draw.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				draw.layout, 0, 1, &selection.panelSet, 1, &dynamicOffset );
	}

	const float margin = 8.0f;
	const float panelW = 192.0f;
	const float panelH = selection.pointLight ? 128.0f : 192.0f;
	const float statsH = 52.0f;
	const float outerW = panelW + 12.0f;
	const float outerH = panelH + statsH + 18.0f;
	const float panelX = margin + 6.0f;
	const float panelY = margin + 6.0f;
	const float statsY = panelY + panelH + 8.0f;
	const idVec4 outerColor( 0.02f, 0.03f, 0.04f, 0.78f );
	const idVec4 panelFrameColor = selection.pointLight
			? idVec4( 0.92f, 0.70f, 0.18f, 0.95f )
			: idVec4( 0.15f, 0.78f, 0.95f, 0.95f );
	const idVec4 failColor( 0.85f, 0.24f, 0.20f, 0.95f );
	const idVec4 textColor( 0.96f, 0.96f, 0.92f, 0.98f );
	const idVec4 labelColor( 0.95f, 0.95f, 0.95f, 0.95f );
	const idVec4 &frameColor = drawPanel ? panelFrameColor : failColor;

	VK_ShadowMap_OverlaySolid( draw, margin, margin, outerW, outerH, outerColor );
	VK_ShadowMap_OverlaySolid( draw, panelX - 2.0f, panelY - 2.0f,
			panelW + 4.0f, panelH + 4.0f, frameColor );

	if ( drawPanel ) {
		VK_ShadowMap_OverlayQuad( draw, draw.panelPipeline, panelX, panelY,
				panelW, panelH, frameColor, VK_SHADOW_OVERLAY_SOLID,
				(float)selection.atlasDiv, (float)selection.tileCount,
				selection.uvRect );

		const int columns = Max( 1, selection.pointLight ? 3 : selection.atlasDiv );
		const int rows = Max( 1, selection.pointLight ? 2 : selection.atlasDiv );
		const float tileW = panelW / (float)columns;
		const float tileH = panelH / (float)rows;
		for ( int tileIndex = 0 ; tileIndex < selection.tileCount ; tileIndex++ ) {
			const int col = tileIndex % columns;
			const int row = tileIndex / columns;
			char label[ 8 ];
			idStr::snPrintf( label, sizeof( label ), "%d", tileIndex );
			VK_ShadowMap_OverlayString( draw,
					panelX + col * tileW + 4.0f, panelY + row * tileH + 4.0f,
					0.9f, labelColor, label );
		}
	} else {
		VK_ShadowMap_OverlaySolid( draw, panelX, panelY, panelW, panelH,
				idVec4( 0.02f, 0.02f, 0.03f, 0.92f ) );
	}

	char line1[ 64 ];
	char line2[ 64 ];
	char line3[ 64 ];
	char line4[ 64 ];
	if ( drawPanel ) {
		idStr::snPrintf( line1, sizeof( line1 ), "%s %c L%d %c%d MAP",
				selection.pointLight ? "POINT" : "PROJ",
				selection.globalPass ? 'G' : 'L',
				selection.lightDefIndex,
				selection.pointLight ? 'F' : 'C',
				selection.tileCount );
		idStr::snPrintf( line2, sizeof( line2 ), "%s T%d CAST %d/%d A%d",
				selection.outcome, selection.tileSize,
				selection.staticCasters, selection.dynamicCasters,
				selection.alphaCasters );
	} else {
		idStr::Copynz( line1, "NO MAP", sizeof( line1 ) );
		idStr::Copynz( line2, "NO CAST", sizeof( line2 ) );
	}
	idStr::snPrintf( line3, sizeof( line3 ), "HIT %d/%d NEW %d/%d CMP %d",
			viewPrepared ? vkShadow.projectedCacheHits : 0,
			viewPrepared ? vkShadow.pointCacheHits : 0,
			viewPrepared ? vkShadow.projectedFreshUpdates : 0,
			viewPrepared ? vkShadow.pointFreshUpdates : 0,
			viewPrepared ? vkShadow.composePasses : 0 );
	idStr::snPrintf( line4, sizeof( line4 ), "TILE %d/%d FACE %d FB %d/%d/%d",
			viewPrepared ? vkShadow.atlasTilesRendered : 0,
			viewPrepared ? vkShadow.atlasTilesAllocated : 0,
			viewPrepared ? vkShadow.pointFacesRendered : 0,
			viewPrepared ? vkShadow.budgetFallbacks : 0,
			viewPrepared ? vkShadow.admissionDenied : 0,
			viewPrepared ? vkShadow.subviewFallbacks : 0 );

	VK_ShadowMap_OverlayString( draw, panelX, statsY, 0.9f, textColor, line1 );
	VK_ShadowMap_OverlayString( draw, panelX, statsY + 10.0f, 0.8f, textColor, line2 );
	VK_ShadowMap_OverlayString( draw, panelX, statsY + 20.0f, 0.8f, textColor, line3 );
	VK_ShadowMap_OverlayString( draw, panelX, statsY + 30.0f, 0.8f, textColor, line4 );

	VK_Exec_SetViewScissor( draw.cmd, viewDef, fbHeight );
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
