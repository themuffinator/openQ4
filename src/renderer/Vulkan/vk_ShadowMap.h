// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VK_SHADOWMAP_H__
#define __VK_SHADOWMAP_H__

#include "../ShadowMapProjected.h"

typedef struct classicInteractionDomainView_s classicInteractionDomainView_t;

/*
===============================================================================

	Vulkan shadow maps (Phase F2a projected + F2b point,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	One module-owned per-view depth atlas serves projected/parallel lights,
	with a lazily populated scratch cube pool large enough for both receiver
	ownerships of every admitted point light. Exact-signature opaque
	static-only passes may reuse resident depth: projected entries copy
	between transfer-only cache images and the current atlas tile, while point
	entries retain identity-owned cubes. Projected maps compose live moving and
	cutout casters over static depth; point maps with live casters regenerate.
	Each prepared light carries
	the retail LOCAL/GLOBAL receiver ownership split: LOCAL maps contain global
	casters, while GLOBAL maps contain global + local casters. Include after
	tr_local.h (idPlane/viewLight_t) and volk.h (VkDescriptorSet).

===============================================================================
*/

typedef enum vkShadowReceiverPass_e {
	VK_SHADOW_RECEIVER_LOCAL = 0,
	VK_SHADOW_RECEIVER_GLOBAL,
	VK_SHADOW_RECEIVER_PASS_COUNT
} vkShadowReceiverPass_t;

// One admitted light can need two distinct LOCAL/GLOBAL resources. Keep the
// point pool large enough for every ownership in the bounded light table;
// cube images and descriptor sets are still created lazily as a view needs
// them, so ordinary maps do not pay the maximum image allocation. A stock
// Air Defense 2 door/portal view exceeds 64 shadowing lights; table overflow
// can lose a map-only caster before sticky stencil recovery on the next view.
// Keep descriptor capacity (vk_GuiExecutor) and admission capacity tied to this
// same bound so increasing one does not simply move the failure elsewhere.
static const int VK_SHADOW_MAX_LIGHTS = 256;
static const int VK_SHADOW_MAX_POINT_CUBES =
	VK_SHADOW_MAX_LIGHTS * VK_SHADOW_RECEIVER_PASS_COUNT;
static const int VK_SHADOW_MAX_CACHE_SLOTS = 16;

// Per-receiver-pass resource state. resourcePass identifies the pass that
// owns/rendered the contents; when no local casters exist the LOCAL and
// GLOBAL states can safely alias one tile/cube.
typedef struct vkShadowPassState_s {
	bool				valid;
	vkShadowReceiverPass_t resourcePass;
	bool				cacheHit;		// exact resident entry supplies this pass
	bool				cacheUpdate;	// fresh render will publish this entry
	bool				composeDynamic;	// projected only: the cached tile holds STATIC
									// depth, and this view's dynamic casters are drawn
									// over it after publication/restore (the GL
									// SHADOWMAP_RENDER_COMPOSE_DYNAMIC contract)
	int					cacheEntry;	// class-specific resident slot, -1 when uncached
	int					cacheSignature;
	int					tileX;			// atlas block origin, image (top-left) coordinates
	int					tileY;
	int					cubeIndex;		// point cube pool slot (pointLight only)
	VkDescriptorSet		pointSet;		// cube set-7 descriptor set for the active frame slot (pointLight only)
	float				atlasRects[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
										// composed per-cascade UV rects; v spans inverted
} vkShadowPassState_t;

// Per-view, per-light shared state plus the two ownership-specific maps the
// interaction pass consumes.
typedef struct vkShadowLightState_s {
	const viewLight_t *	vLight;
	bool				valid;			// at least one receiver pass is valid
	bool				pointLight;		// F2b: depth-cube path instead of an atlas tile
	int					tileSize;		// atlas tile edge, or the cube face size for point lights
	float				pointFar;		// padded radial far envelope (pointLight only)
	float				pointLightOrigin[ 3 ];	// receiver state matching the rendered/cache map
	shadowMapProjectedLightState_t projectedState;
										// full shared CSM state (projected lights only)
	float				invAtlasSize[ 2 ];
	float				constantBias;	// point-light effective normalized receiver bias
	float				normalBias;	// point-light effective normalized slope bias
	float				texelDepthBias;	// point-light receiver value
	float				normalOffsetWorld;	// point: per-distance texel factor
	vkShadowPassState_t	passes[ VK_SHADOW_RECEIVER_PASS_COUNT ];
} vkShadowLightState_t;

// CPU phase: classify + gate the view's lights, build projected states /
// claim point cubes, and allocate atlas tiles. stencilFallbackAvailable is
// the active target's real attachment + pipeline capability, so scheduling
// cannot select an unusable fallback. Returns the number of shadow-map-ready
// lights.
int		VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef,
			bool stencilFallbackAvailable );

// GPU phase: frame-scope interruption that renders every prepared light's
// casters into the atlas / its depth cube, then resumes main rendering with
// LOAD
bool	VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef );

// Shared fixed-classic ownership is a whole-view transaction. Preflight may
// allocate private images/descriptors and reserve cache entries, but it emits
// no attachment writes. Commit consumes only the sealed domain records and is
// structurally infallible; Abort releases every private reservation so the
// unchanged classic interaction walker can own the view instead.
bool	VK_ShadowMap_PreflightClassicInteractionView(
			const classicInteractionDomainView_t *view );
void	VK_ShadowMap_CommitClassicInteractionView(
			const classicInteractionDomainView_t *view );
void	VK_ShadowMap_AbortClassicInteractionView(
			const classicInteractionDomainView_t *view );

// prepared state for a light, or NULL when retained stencil handles it
const vkShadowLightState_t *VK_ShadowMap_LightState( const viewLight_t *vLight );

// ownership-specific prepared resource for a light, or NULL when that
// receiver chain must use its retained stencil fallback
const vkShadowPassState_t *VK_ShadowMap_PassState(
	const vkShadowLightState_t *lightState, vkShadowReceiverPass_t receiverPass );

// Phase F3: per-light-class resource truth behind the tr_local.h hook
// RB_ShadowMapResourcesKnownGood, so the front end may stop generating and
// linking stencil volumes for shadow-mapped lights. Per-view atlas/cube/
// material admission still happens after that decision; a miss draws the
// light unshadowed for one frame and marks it sticky, which is the OpenGL
// contract.
bool	VK_ShadowMap_ResourcesKnownGood( bool pointLight );

// Phase F3 sticky fallback contract (GL RB_ShadowMapMarkStencilFallbackSticky
// parity): a hard shadow-map failure records that later front-end frames must
// retain this light's stencil volume too
void	VK_ShadowMap_MarkStencilFallbackSticky( const viewLight_t *vLight );

// mark every still-valid prepared light sticky and drop it from the table:
// the caller could not run or consume the shadow pass this view
void	VK_ShadowMap_AbandonPreparedLights( void );

// r_shadowMapDebugOverlay: draw the selected shadow map as a top-left
// mini-map with a stats readout (GL RB_ShadowMapDebugOverlayDraw parity).
// Must be called at the end of the interaction pass, inside the open main
// rendering scope; it issues its own viewport and restores the view scissor.
void	VK_ShadowMap_DebugOverlayDraw( const viewDef_t *viewDef );

// device-shutdown hook (device idle by contract)
void	VK_ShadowMap_Shutdown( void );

#endif /* !__VK_SHADOWMAP_H__ */
