/*
===============================================================================
	Vulkan scene overlay passes.

	The Vulkan versions of the passes RB_STD_DrawView (draw_common.cpp) runs
	between the pre-fog material passes and the fog, which the renderer-vk
	module does not build:

	- RB_STD_LightGridIndirect: baked light-grid indirect diffuse, added over
	  every receiver the depth prepass left visible;
	- RB_STD_DrawPlayerVisibilityEffects: the multiplayer brightskin wash,
	  rimlight and silhouette outline;
	- RB_STD_DrawCelOutlines: ink shells around cel-shaded models.

	VK_GuiExecutor_Draw3DView calls VK_SceneEffects_DrawPreFog at that point.
	Each pass draws the surfaces again with the view's negative-height
	viewport, against the depth and stencil the scene already filled, through
	the interaction pipeline layout. The shaders are ports of the glprogs
	files named in each one.
===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../CelShading.h"

#undef snprintf
#undef vsnprintf
#include <cstdio>
#include <cstring>
#include "volk.h"

#include "VulkanDevice.h"
#include "vk_ExecutorHooks.h"
#include "vk_HDRScene.h"
#include "shaders/scene_shaders_spv.h"

void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] );

extern idCVar r_useScissor;

// pipeline cache keys for VK_Exec_ExtraPipeline
enum vkScenePassKind_t {
	VK_SCENE_LIGHT_GRID = VK_EXTRA_KIND_SCENE_BASE,
	VK_SCENE_OUTLINE_MASK,
	VK_SCENE_OUTLINE_SHELL,
	VK_SCENE_BRIGHTSKIN,
	VK_SCENE_RIMLIGHT
};

enum vkSceneModule_t {
	VK_SCENE_MODULE_LIGHT_GRID_VERT,
	VK_SCENE_MODULE_LIGHT_GRID_FRAG,
	VK_SCENE_MODULE_OUTLINE_VERT,
	VK_SCENE_MODULE_OUTLINE_FRAG,
	VK_SCENE_MODULE_RIMLIGHT_VERT,
	VK_SCENE_MODULE_RIMLIGHT_FRAG,
	VK_SCENE_MODULE_COUNT
};

typedef struct vkSceneModuleSource_s {
	const unsigned char *	code;
	unsigned int			size;
	const char *			name;
} vkSceneModuleSource_t;

static const vkSceneModuleSource_t vkSceneModuleSources[ VK_SCENE_MODULE_COUNT ] = {
	{ vk_lightgrid_indirect_vert_spv, vk_lightgrid_indirect_vert_spv_size, "light grid vertex" },
	{ vk_lightgrid_indirect_frag_spv, vk_lightgrid_indirect_frag_spv_size, "light grid fragment" },
	{ vk_player_outline_vert_spv, vk_player_outline_vert_spv_size, "player outline vertex" },
	{ vk_player_outline_frag_spv, vk_player_outline_frag_spv_size, "player outline fragment" },
	{ vk_player_rimlight_vert_spv, vk_player_rimlight_vert_spv_size, "player rimlight vertex" },
	{ vk_player_rimlight_frag_spv, vk_player_rimlight_frag_spv_size, "player rimlight fragment" }
};

static VkShaderModule vkSceneModules[ VK_SCENE_MODULE_COUNT ];
static bool vkSceneModuleFailed[ VK_SCENE_MODULE_COUNT ];

static VkShaderModule VK_Scene_Module( int which ) {
	if ( vkSceneModules[ which ] != VK_NULL_HANDLE ) {
		return vkSceneModules[ which ];
	}
	if ( vkSceneModuleFailed[ which ] || vkCtx.device == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	VkShaderModuleCreateInfo smci;
	memset( &smci, 0, sizeof( smci ) );
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = vkSceneModuleSources[ which ].size;
	smci.pCode = (const uint32_t *)vkSceneModuleSources[ which ].code;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkSceneModules[ which ] ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: %s shader module creation failed", vkSceneModuleSources[ which ].name );
		vkSceneModules[ which ] = VK_NULL_HANDLE;
		vkSceneModuleFailed[ which ] = true;
	}
	return vkSceneModules[ which ];
}

static VkPipeline VK_Scene_Pipeline( int kind, int vertModule, int fragModule, int stateBits, int flags ) {
	const VkShaderModule vert = VK_Scene_Module( vertModule );
	const VkShaderModule frag = VK_Scene_Module( fragModule );
	if ( vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	return VK_Exec_ExtraPipeline( kind, vert, frag, stateBits, VK_EXTRA_VERTEX_DRAWVERT, flags );
}

// the GL_Cull contract with the mirror swap, on the view's negative-height
// viewport (VK_GuiExecutor_Draw3DView's mapping)
static void VK_Scene_SetCull( VkCommandBuffer cmd, const viewDef_t *viewDef, cullType_t cullType ) {
	switch ( cullType ) {
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
}

static void VK_Scene_SetDepth( VkCommandBuffer cmd, bool test, VkCompareOp compareOp ) {
	vkCmdSetDepthTestEnable( cmd, test ? VK_TRUE : VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, compareOp );
}

// The overlays draw with the plain view projection, as OpenGL loads it once
// for the whole pass: no weapon or model depth hack.
static void VK_Scene_PlainMVP( const viewDef_t *viewDef, const drawSurf_t *surf, float mvp[ 16 ] ) {
	float mvpGL[ 16 ];
	myGlMultMatrix( surf->space->modelViewMatrix, viewDef->projectionMatrix, mvpGL );
	VK_FixupClipSpaceZ( mvp, mvpGL );
}

// OpenGL's Bind() loads an image the first time it is drawn; so does this
static VkDescriptorSet VK_Scene_ImageSet( idImage *image ) {
	if ( image == NULL ) {
		return VK_NULL_HANDLE;
	}
	if ( !image->IsLoaded() ) {
		image->ActuallyLoadImage( true );
	}
	return VK_Exec_ImageDescriptor( image->GetDeviceHandle(), true );
}

static void VK_Scene_BindUniform( VkCommandBuffer cmd, int uniformOffset ) {
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_Exec_InteractionPipelineLayout(),
			6, 1, &uniformSet, 1, &dynamicOffset );
}

/*
===============================================================================

	Light grid (RB_STD_LightGridIndirect)

===============================================================================
*/

typedef struct vkLightGridPush_s {
	float	mvp[ 16 ];
	float	bumpMatrixS[ 4 ];
	float	bumpMatrixT[ 4 ];
	float	diffuseMatrixS[ 4 ];
	float	diffuseMatrixT[ 4 ];
} vkLightGridPush_t;

// std140 layout of LightGridBlock in lightgrid_indirect.vert/.frag
typedef struct vkLightGridBlock_s {
	float	modelRow0[ 4 ];
	float	modelRow1[ 4 ];
	float	modelRow2[ 4 ];
	float	gridOrigin[ 4 ];		// w: r_lightGridDebug
	float	gridSize[ 4 ];			// w: probe relocation distance
	float	gridBounds[ 4 ];		// w: probe atlas bound
	float	atlasInfo[ 4 ];
	float	visibilityInfo[ 4 ];
	float	blendInfo[ 4 ];			// w: irradiance gamma
	float	portalPlane[ 4 ];
	float	portalBoundsMin[ 4 ];	// w: max contribution
	float	portalBoundsMax[ 4 ];	// w: vertex colour scale
	float	depthInfo[ 4 ];
	float	depthViewport[ 4 ];		// z: vertex colour bias, w: framebuffer height
	float	diffuseColor[ 4 ];
	float	flatDiffuseParams[ 4 ];
} vkLightGridBlock_t;

typedef struct vkLightGridPortalBlend_s {
	const LightGrid *	neighborLightGrid;
	idPlane				portalPlane;
	idBounds			portalBounds;
	float				blendDistance;
} vkLightGridPortalBlend_t;

typedef struct vkLightGridAlbedo_s {
	const shaderStage_t *	stage;
	int						stageIndex;
	idImage *				image;
	idVec4					matrix[ 2 ];
	float					color[ 4 ];
	float					vertexColorParams[ 2 ];
} vkLightGridAlbedo_t;

typedef struct vkLightGridDrawStats_s {
	int		nullInput;
	int		noAlbedo;
	int		cacheFail;
	int		emptyGeometry;
	int		noIrradiance;
	int		ensureFail;
	int		defaultIrradiance;
	int		badAtlas;
	int		stageReject;
	int		stageSubmit;
} vkLightGridDrawStats_t;

// per-view pass state shared by the surface draws
typedef struct vkLightGridPassState_s {
	const viewDef_t *	viewDef;
	VkCommandBuffer		cmd;
	int					slot;
	int					fbHeight;
	int					debugMode;
	bool				depthTextureCompare;
	int					depthWidth;
	int					depthHeight;
	VkPipeline			pipeline;
	bool				weaponRange;
} vkLightGridPassState_t;

static void VK_LightGrid_ModelRows( const float modelMatrix[ 16 ], float row0[ 4 ], float row1[ 4 ], float row2[ 4 ] ) {
	row0[ 0 ] = modelMatrix[ 0 ]; row0[ 1 ] = modelMatrix[ 4 ]; row0[ 2 ] = modelMatrix[ 8 ]; row0[ 3 ] = modelMatrix[ 12 ];
	row1[ 0 ] = modelMatrix[ 1 ]; row1[ 1 ] = modelMatrix[ 5 ]; row1[ 2 ] = modelMatrix[ 9 ]; row1[ 3 ] = modelMatrix[ 13 ];
	row2[ 0 ] = modelMatrix[ 2 ]; row2[ 1 ] = modelMatrix[ 6 ]; row2[ 2 ] = modelMatrix[ 10 ]; row2[ 3 ] = modelMatrix[ 14 ];
}

static void VK_LightGrid_VertexColorParams( stageVertexColor_t vertexColor, float params[ 2 ] ) {
	params[ 0 ] = 0.0f;
	params[ 1 ] = 1.0f;
	if ( vertexColor == SVC_MODULATE ) {
		params[ 0 ] = 1.0f;
		params[ 1 ] = 0.0f;
	} else if ( vertexColor == SVC_INVERSE_MODULATE ) {
		params[ 0 ] = -1.0f;
		params[ 1 ] = 1.0f;
	}
}

static bool VK_LightGrid_ReceiverOnlySubmission( int debugMode ) {
	return debugMode != 4;
}

static bool VK_LightGrid_MaterialHasActiveColorMaskStage( const idMaterial *shader, const float *regs ) {
	for ( int stageIndex = 0; stageIndex < shader->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( stage != NULL && ( regs == NULL || regs[ stage->conditionRegister ] != 0.0f )
				&& ( stage->drawStateBits & GLS_COLORMASK ) != 0 ) {
			return true;
		}
	}
	return false;
}

// RB_SurfaceCanReceiveLightGrid
static bool VK_LightGrid_SurfaceCanReceive( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->space == NULL || surf->geo == NULL ) {
		return false;
	}
	const idMaterial *shader = surf->material;
	if ( !shader->IsDrawn() || !shader->ReceivesLighting() || shader->GetSort() != SS_OPAQUE ) {
		return false;
	}
	if ( shader->IsPortalSky() || shader->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( surf->decalColorCache != NULL || shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		return false;
	}
	// scope and glass stages that author colour masks keep their own look
	return !( surf->space->weaponDepthHack
		&& VK_LightGrid_MaterialHasActiveColorMaskStage( shader, surf->shaderRegisters ) );
}

// RB_SurfaceHasLightGrid: world and entity receivers sample their own area
static bool VK_LightGrid_SurfaceHasGrid( const drawSurf_t *surf, const LightGrid *&lightGrid ) {
	lightGrid = NULL;
	if ( !VK_LightGrid_SurfaceCanReceive( surf ) ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f || surf->area == NULL ) {
		return false;
	}
	if ( !surf->area->lightGrid.IsUsable() ) {
		return false;
	}
	lightGrid = &surf->area->lightGrid;
	return true;
}

// RB_CurrentViewLightGridArea
static int VK_LightGrid_ViewArea( const viewDef_t *viewDef, idRenderWorldLocal *world ) {
	if ( world == NULL ) {
		return -1;
	}
	int areaNum = viewDef->areaNum;
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( viewDef->initialViewAreaOrigin );
	}
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( viewDef->renderView.vieworg );
	}
	return areaNum >= 0 && areaNum < world->numPortalAreas ? areaNum : -1;
}

// RB_SurfaceHasViewWeaponLightGrid: the view weapon samples the view's area
static bool VK_LightGrid_SurfaceHasViewWeaponGrid( const viewDef_t *viewDef, const drawSurf_t *surf,
		const LightGrid *&lightGrid ) {
	lightGrid = NULL;
	if ( !VK_LightGrid_SurfaceCanReceive( surf ) || !surf->space->weaponDepthHack ) {
		return false;
	}
	idRenderWorldLocal *world = viewDef->renderWorld;
	const int areaNum = VK_LightGrid_ViewArea( viewDef, world );
	if ( areaNum < 0 || !world->portalAreas[ areaNum ].lightGrid.IsUsable() ) {
		return false;
	}
	lightGrid = &world->portalAreas[ areaNum ].lightGrid;
	return true;
}

static bool VK_LightGrid_SurfaceWorldBounds( const drawSurf_t *surf, idBounds &worldBounds ) {
	if ( surf->space == NULL || surf->geo == NULL || surf->geo->bounds.IsCleared() ) {
		return false;
	}
	idVec3 localPoints[ 8 ];
	surf->geo->bounds.ToPoints( localPoints );
	worldBounds.Clear();
	for ( int i = 0; i < 8; i++ ) {
		idVec3 worldPoint;
		R_LocalPointToGlobal( surf->space->modelMatrix, localPoints[ i ], worldPoint );
		worldBounds.AddPoint( worldPoint );
	}
	return !worldBounds.IsCleared();
}

// RB_SurfaceHasLightGridPortalBlend
static bool VK_LightGrid_PortalBlend( const viewDef_t *viewDef, const drawSurf_t *surf,
		vkLightGridPortalBlend_t &blend ) {
	blend.neighborLightGrid = NULL;
	blend.blendDistance = 0.0f;
	blend.portalPlane.Zero();
	blend.portalBounds.Clear();
	if ( surf == NULL || surf->area == NULL || viewDef->renderWorld == NULL ) {
		return false;
	}
	const float blendDistance = r_lightGridPortalBlend.GetFloat();
	idBounds surfaceBounds;
	if ( blendDistance <= 0.0f || !VK_LightGrid_SurfaceWorldBounds( surf, surfaceBounds ) ) {
		return false;
	}

	idRenderWorldLocal *world = viewDef->renderWorld;
	float bestWeight = 0.0f;
	for ( const portal_t *portal = surf->area->portals; portal != NULL; portal = portal->next ) {
		if ( portal->w == NULL || portal->w->GetNumPoints() < 3 ) {
			continue;
		}
		if ( portal->doublePortal != NULL && ( portal->doublePortal->blockingBits & PS_BLOCK_VIEW ) ) {
			continue;
		}
		if ( portal->intoArea < 0 || portal->intoArea >= world->numPortalAreas ) {
			continue;
		}
		portalArea_t &neighborArea = world->portalAreas[ portal->intoArea ];
		if ( neighborArea.viewCount != tr.viewCount ) {
			continue;
		}
		if ( viewDef->connectedAreas != NULL && !viewDef->connectedAreas[ portal->intoArea ] ) {
			continue;
		}
		if ( !neighborArea.lightGrid.IsUsable() ) {
			continue;
		}
		idBounds portalBounds;
		portal->w->GetBounds( portalBounds );
		portalBounds.ExpandSelf( blendDistance );
		if ( !surfaceBounds.IntersectsBounds( portalBounds ) ) {
			continue;
		}
		const float planeDistance = idMath::Fabs( surfaceBounds.PlaneDistance( portal->plane ) );
		if ( planeDistance > blendDistance ) {
			continue;
		}
		const float weight = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, planeDistance / blendDistance );
		if ( weight <= bestWeight ) {
			continue;
		}
		bestWeight = weight;
		blend.neighborLightGrid = &neighborArea.lightGrid;
		blend.portalPlane = portal->plane;
		blend.portalBounds = portalBounds;
		blend.blendDistance = blendDistance;
	}
	return blend.neighborLightGrid != NULL;
}

// ---- atlas residency (RB_UpdateLightGridImageResidency) ----

static const int VK_LIGHTGRID_RESIDENCY_UNTOUCHED = -0x40000000;
static idRenderWorldLocal *vkLightGridResidencyWorld = NULL;
static idList<int> vkLightGridResidencyLastTouched;
static int vkLightGridResidencyFrame = 0;
static const viewDef_t *vkLightGridPreparedResidencyView;
static int vkLightGridPreparedResidencyFrame = -1;

static void VK_LightGrid_LoadImage( idImage *image ) {
	if ( image != NULL && !image->IsLoaded() ) {
		image->ActuallyLoadImage( true );
	}
}

static void VK_LightGrid_PurgeImage( idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		image->PurgeImage();
	}
}

static void VK_LightGrid_TouchArea( idRenderWorldLocal *world, int areaIndex, int frameIndex ) {
	if ( areaIndex < 0 || areaIndex >= world->numPortalAreas
			|| vkLightGridResidencyLastTouched[ areaIndex ] == frameIndex ) {
		return;
	}
	LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
	if ( !lightGrid.IsUsable() ) {
		return;
	}
	vkLightGridResidencyLastTouched[ areaIndex ] = frameIndex;
	if ( !world->EnsureLightGridAreaImages( areaIndex ) ) {
		return;
	}
	VK_LightGrid_LoadImage( lightGrid.irradianceImage );
	VK_LightGrid_LoadImage( lightGrid.visibilityImage );
	VK_LightGrid_LoadImage( lightGrid.probeImage );
}

static void VK_LightGrid_TouchAreaAndNeighbors( const viewDef_t *viewDef, idRenderWorldLocal *world,
		int areaIndex, int frameIndex ) {
	if ( areaIndex < 0 || areaIndex >= world->numPortalAreas ) {
		return;
	}
	VK_LightGrid_TouchArea( world, areaIndex, frameIndex );
	for ( const portal_t *portal = world->portalAreas[ areaIndex ].portals; portal != NULL; portal = portal->next ) {
		if ( portal->doublePortal != NULL && ( portal->doublePortal->blockingBits & PS_BLOCK_VIEW ) ) {
			continue;
		}
		if ( portal->intoArea < 0 || portal->intoArea >= world->numPortalAreas ) {
			continue;
		}
		if ( viewDef->connectedAreas != NULL && !viewDef->connectedAreas[ portal->intoArea ] ) {
			continue;
		}
		VK_LightGrid_TouchArea( world, portal->intoArea, frameIndex );
	}
}

static void VK_LightGrid_UpdateResidency( const viewDef_t *viewDef ) {
	idRenderWorldLocal *world = viewDef->renderWorld;
	if ( world == NULL || world->portalAreas == NULL ) {
		return;
	}
	if ( vkLightGridResidencyWorld != world || vkLightGridResidencyLastTouched.Num() != world->numPortalAreas ) {
		vkLightGridResidencyWorld = world;
		vkLightGridResidencyLastTouched.SetNum( world->numPortalAreas );
		for ( int i = 0; i < vkLightGridResidencyLastTouched.Num(); i++ ) {
			vkLightGridResidencyLastTouched[ i ] = VK_LIGHTGRID_RESIDENCY_UNTOUCHED;
		}
	}

	const int frameIndex = ++vkLightGridResidencyFrame;
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		if ( world->portalAreas[ areaIndex ].viewCount == tr.viewCount ) {
			VK_LightGrid_TouchAreaAndNeighbors( viewDef, world, areaIndex, frameIndex );
		}
	}
	VK_LightGrid_TouchAreaAndNeighbors( viewDef, world, VK_LightGrid_ViewArea( viewDef, world ), frameIndex );

	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[ i ];
		if ( surf == NULL || surf->material == NULL
				|| surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			continue;
		}
		const LightGrid *lightGrid = NULL;
		if ( VK_LightGrid_SurfaceHasGrid( surf, lightGrid ) ) {
			VK_LightGrid_TouchArea( world, lightGrid->area, frameIndex );
		}
		if ( VK_LightGrid_SurfaceHasViewWeaponGrid( viewDef, surf, lightGrid ) ) {
			VK_LightGrid_TouchArea( world, lightGrid->area, frameIndex );
			vkLightGridPortalBlend_t portalBlend;
			if ( VK_LightGrid_PortalBlend( viewDef, surf, portalBlend ) ) {
				VK_LightGrid_TouchArea( world, portalBlend.neighborLightGrid->area, frameIndex );
			}
		}
	}

	const int residencyFrames = r_lightGridResidencyFrames.GetInteger();
	if ( residencyFrames <= 0 ) {
		return;
	}
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		const int lastTouchedFrame = vkLightGridResidencyLastTouched[ areaIndex ];
		if ( lastTouchedFrame != VK_LIGHTGRID_RESIDENCY_UNTOUCHED && frameIndex - lastTouchedFrame <= residencyFrames ) {
			continue;
		}
		LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
		VK_LightGrid_PurgeImage( lightGrid.irradianceImage );
		VK_LightGrid_PurgeImage( lightGrid.visibilityImage );
		VK_LightGrid_PurgeImage( lightGrid.probeImage );
	}
}

// ---- albedo (RB_LightGridFindRepresentativeAlbedo) ----

static bool VK_LightGrid_StageActive( const shaderStage_t *stage, const float *regs ) {
	return stage != NULL && ( regs == NULL || regs[ stage->conditionRegister ] != 0.0f );
}

static bool VK_LightGrid_StageCanProvideAlbedo( const idMaterial *shader, const shaderStage_t *stage, const float *regs ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( stage->lighting == SL_DIFFUSE && VK_LightGrid_StageActive( stage, regs ) ) {
		return true;
	}
	if ( stage->lighting != SL_AMBIENT || !VK_LightGrid_StageActive( stage, regs ) ) {
		return false;
	}
	if ( shader->IsPortalSky() || shader->TestMaterialFlag( MF_SKY ) || shader->GetSort() >= SS_FAR ) {
		return false;
	}
	if ( stage->texture.image == NULL || stage->newStage != NULL ) {
		return false;
	}
	if ( stage->texture.texgen != TG_EXPLICIT && stage->texture.texgen != TG_POT_CORRECTION ) {
		return false;
	}
	// alpha-only maskcolor stages carry no albedo (RB_LightGridAmbientStageCanProvideAlbedo)
	if ( ( stage->drawStateBits & GLS_COLORMASK ) != 0 ) {
		return false;
	}
	return ( stage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == 0;
}

static bool VK_LightGrid_HasActiveAlbedoStage( const idMaterial *shader, const float *regs ) {
	for ( int stageIndex = 0; stageIndex < shader->GetNumStages(); stageIndex++ ) {
		if ( VK_LightGrid_StageCanProvideAlbedo( shader, shader->GetStage( stageIndex ), regs ) ) {
			return true;
		}
	}
	return false;
}

bool VK_LightGrid_SurfaceRequestsBakedDiffuse( const viewDef_t *viewDef, const drawSurf_t *surf ) {
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() || viewDef == NULL
			|| viewDef->viewEntitys == NULL || viewDef->renderWorld == NULL
			|| !viewDef->renderWorld->AnyLightGridAvailable() ) {
		return false;
	}
	const LightGrid *grid = NULL;
	if ( !VK_LightGrid_SurfaceHasGrid( surf, grid )
			&& !VK_LightGrid_SurfaceHasViewWeaponGrid( viewDef, surf, grid ) ) {
		return false;
	}
	return VK_LightGrid_ReceiverOnlySubmission( r_lightGridDebug.GetInteger() )
		|| VK_LightGrid_HasActiveAlbedoStage( surf->material, surf->shaderRegisters );
}

// Match the bounded modern GL receiver contract. Portal blending and view
// weapons need multiple grids/depth ranges and remain in the classic walker.
void VK_LightGrid_PrepareModernView( const viewDef_t *view ) {
	VK_LightGrid_UpdateResidency( view );
	vkLightGridPreparedResidencyView = view;
	vkLightGridPreparedResidencyFrame = backEnd.frameCount;
}

bool VK_LightGrid_PrepareModern( const viewDef_t *view, const drawSurf_t *surf,
		idImage *images[3], float params[7][4] ) {
	const LightGrid *grid = NULL;
	vkLightGridPortalBlend_t blend;
	if ( view == NULL || view->renderWorld == NULL || r_lightGridDebug.GetInteger() != 0
			|| !VK_LightGrid_SurfaceHasGrid( surf, grid ) || VK_LightGrid_PortalBlend( view, surf, blend )
			|| !view->renderWorld->EnsureLightGridAreaImages( grid->area ) ) { return false; }
	images[0] = grid->irradianceImage;
	images[1] = grid->visibilityImage;
	images[2] = grid->probeImage;
	for ( int i = 0; i < 3; ++i ) {
		if ( images[i] == NULL ) { return false; }
		VK_LightGrid_LoadImage( images[i] );
		if ( !images[i]->IsLoaded() || images[i]->IsDefaulted() || images[i]->GetOpts().textureType != TT_2D
				|| images[i]->GetOpts().width <= 0 || images[i]->GetOpts().height <= 0 ) { return false; }
		images[i]->SetSamplerState( TF_LINEAR, TR_CLAMP );
	}
	const int64_t cellsX = int64_t( grid->lightGridBounds[0] ) * grid->lightGridBounds[2];
	const int64_t cellsY = grid->lightGridBounds[1];
	if ( grid->lightGridBounds[0] <= 0 || grid->lightGridBounds[2] <= 0 || cellsY <= 0
			|| grid->imageSingleProbeSize <= grid->imageBorderSize || grid->imageBorderSize < 0
			|| cellsX > images[0]->GetOpts().width
			|| cellsX * grid->imageSingleProbeSize != images[0]->GetOpts().width
			|| cellsY * grid->imageSingleProbeSize != images[0]->GetOpts().height
			|| images[1]->GetOpts().width != images[0]->GetOpts().width
			|| images[1]->GetOpts().height != images[0]->GetOpts().height
			|| images[2]->GetOpts().width != cellsX || images[2]->GetOpts().height != cellsY ) { return false; }
	memset( params, 0, sizeof( float ) * 28 );
	for ( int axis = 0; axis < 3; ++axis ) {
		params[0][axis] = grid->lightGridOrigin[axis];
		params[1][axis] = grid->lightGridSize[axis];
		params[2][axis] = float( grid->lightGridBounds[axis] );
	}
	params[0][3] = 1.0f;
	params[1][3] = idMath::ClampFloat( 0.25f, 4.0f, r_lightGridIrradianceGamma.GetFloat() );
	params[2][3] = idMath::ClampFloat( 0.0f, 16.0f, r_lightGridIntensity.GetFloat() );
	params[3][0] = 1.0f / images[0]->GetOpts().width;
	params[3][1] = 1.0f / images[0]->GetOpts().height;
	params[3][2] = float( grid->imageSingleProbeSize );
	params[3][3] = float( grid->imageBorderSize );
	params[4][0] = grid->visibilityMaxDistance > 0.0f ? grid->visibilityMaxDistance : 4096.0f;
	params[4][1] = 3.0f;
	params[4][2] = idMath::ClampFloat( 0.0f, 1.0f, r_lightGridVisibilityFloor.GetFloat() );
	params[4][3] = 2.0f;
	params[5][0] = grid->relocationMaxDistance > 0.0f ? grid->relocationMaxDistance : 48.0f;
	params[6][3] = idMath::ClampFloat( 0.0f, 16.0f, r_lightGridMaxContribution.GetFloat() );
	return true;
}

static void VK_LightGrid_SetIdentityMatrix( idVec4 matrix[ 2 ] ) {
	matrix[ 0 ].Set( 1.0f, 0.0f, 0.0f, 0.0f );
	matrix[ 1 ].Set( 0.0f, 1.0f, 0.0f, 0.0f );
}

// The stage's image, texture matrix and clamped colour, with a black colour
// promoted to white, as the two albedo paths in draw_common.cpp do.
static void VK_LightGrid_StageAlbedo( const drawSurf_t *surf, const shaderStage_t *stage, idImage **image,
		idVec4 matrix[ 2 ], float color[ 4 ], idVec4 &flatDiffuseParams ) {
	*image = globalImages->whiteImage;
	VK_Interactions_SetDrawInteraction( stage, surf->shaderRegisters, image, matrix, color );
	if ( *image == NULL ) {
		*image = globalImages->whiteImage;
	}
	if ( color[ 0 ] <= 0.0f && color[ 1 ] <= 0.0f && color[ 2 ] <= 0.0f ) {
		color[ 0 ] = color[ 1 ] = color[ 2 ] = 1.0f;
	}
	flatDiffuseParams.Zero();
	if ( stage->lighting == SL_DIFFUSE ) {
		RB_ApplyFlatDiffuseStage( surf, image, color, flatDiffuseParams );
	}
}

static void VK_LightGrid_FindRepresentativeAlbedo( const drawSurf_t *surf, vkLightGridAlbedo_t &albedo ) {
	memset( &albedo, 0, sizeof( albedo ) );
	albedo.stageIndex = -1;
	albedo.image = globalImages->whiteImage;
	VK_LightGrid_SetIdentityMatrix( albedo.matrix );
	albedo.color[ 0 ] = albedo.color[ 1 ] = albedo.color[ 2 ] = 0.55f;
	albedo.color[ 3 ] = 1.0f;
	albedo.vertexColorParams[ 0 ] = 0.0f;
	albedo.vertexColorParams[ 1 ] = 1.0f;

	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	for ( int stageIndex = 0; stageIndex < shader->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( !VK_LightGrid_StageCanProvideAlbedo( shader, stage, regs ) ) {
			continue;
		}
		idVec4 flatDiffuseParams;
		VK_LightGrid_StageAlbedo( surf, stage, &albedo.image, albedo.matrix, albedo.color, flatDiffuseParams );
		albedo.stage = stage;
		albedo.stageIndex = stageIndex;
		VK_LightGrid_VertexColorParams( stage->vertexColor, albedo.vertexColorParams );
		return;
	}
}

static bool VK_LightGrid_Draw( const vkLightGridPassState_t &pass, const drawSurf_t *surf, const float mvp[ 16 ],
		idImage *bumpImage, const idVec4 bumpMatrix[ 2 ], idImage *diffuseImage, const idVec4 diffuseMatrix[ 2 ],
		vkLightGridBlock_t &block, const VkDescriptorSet gridSets[ 4 ] ) {
	VkDescriptorSet sets[ 6 ];
	sets[ 0 ] = VK_Scene_ImageSet( bumpImage );
	sets[ 1 ] = VK_Scene_ImageSet( diffuseImage );
	for ( int i = 0; i < 4; i++ ) {
		sets[ 2 + i ] = gridSets[ i ];
	}
	for ( int i = 0; i < 6; i++ ) {
		if ( sets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
	}
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	if ( uniformOffset < 0 ) {
		return false;
	}

	vkLightGridPush_t push;
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	memcpy( push.bumpMatrixS, bumpMatrix[ 0 ].ToFloatPtr(), sizeof( push.bumpMatrixS ) );
	memcpy( push.bumpMatrixT, bumpMatrix[ 1 ].ToFloatPtr(), sizeof( push.bumpMatrixT ) );
	memcpy( push.diffuseMatrixS, diffuseMatrix[ 0 ].ToFloatPtr(), sizeof( push.diffuseMatrixS ) );
	memcpy( push.diffuseMatrixT, diffuseMatrix[ 1 ].ToFloatPtr(), sizeof( push.diffuseMatrixT ) );

	const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();
	vkCmdBindDescriptorSets( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 6, sets, 0, NULL );
	VK_Scene_BindUniform( pass.cmd, uniformOffset );
	vkCmdPushConstants( pass.cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	vkCmdDrawIndexed( pass.cmd, VK_Device_DrawIndexCount( surf->geo->numIndexes ), 1, 0, 0, 0 );
	return true;
}

// RB_STD_DrawLightGridSurface
static bool VK_LightGrid_DrawSurface( vkLightGridPassState_t &pass, const drawSurf_t *surf, const LightGrid &lightGrid,
		const vkLightGridPortalBlend_t *portalBlend, bool invertPortalBlend, vkLightGridDrawStats_t *stats ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const bool receiverOnly = VK_LightGrid_ReceiverOnlySubmission( pass.debugMode );
	if ( tri == NULL || shader == NULL || regs == NULL ) {
		if ( stats != NULL ) {
			stats->nullInput++;
		}
		return false;
	}
	if ( !receiverOnly && !VK_LightGrid_HasActiveAlbedoStage( shader, regs ) ) {
		if ( stats != NULL ) {
			stats->noAlbedo++;
		}
		return false;
	}
	// A native PBR receiver takes the grid in its environment pass, PBR
	// weighted, as on OpenGL; the classic pass would light its legacy stage.
	if ( VK_PBR_GridOwned( pass.viewDef, surf ) ) {
		return false;
	}
	if ( tri->numIndexes <= 0 || tri->ambientCache == NULL ) {
		if ( stats != NULL ) {
			stats->emptyGeometry++;
		}
		return false;
	}
	idImage *irradianceImage = lightGrid.irradianceImage;
	if ( irradianceImage == NULL ) {
		if ( stats != NULL ) {
			stats->noIrradiance++;
		}
		return false;
	}
	if ( !pass.viewDef->renderWorld->EnsureLightGridAreaImages( lightGrid.area ) ) {
		if ( stats != NULL ) {
			stats->ensureFail++;
		}
		return false;
	}
	VK_LightGrid_LoadImage( irradianceImage );
	if ( irradianceImage->IsDefaulted() ) {
		if ( stats != NULL ) {
			stats->defaultIrradiance++;
		}
		return false;
	}
	const int atlasWidth = irradianceImage->GetOpts().width;
	const int atlasHeight = irradianceImage->GetOpts().height;
	if ( atlasWidth <= 0 || atlasHeight <= 0 ) {
		if ( stats != NULL ) {
			stats->badAtlas++;
		}
		return false;
	}

	idImage *visibilityImage = lightGrid.visibilityImage;
	if ( visibilityImage != NULL ) {
		VK_LightGrid_LoadImage( visibilityImage );
		if ( visibilityImage->IsDefaulted() || visibilityImage->GetOpts().width != atlasWidth
				|| visibilityImage->GetOpts().height != atlasHeight ) {
			visibilityImage = NULL;
		}
	}
	idImage *probeImage = lightGrid.probeImage;
	const int probeImageWidth = Max( lightGrid.lightGridBounds[ 0 ] * lightGrid.lightGridBounds[ 2 ], 1 );
	const int probeImageHeight = Max( lightGrid.lightGridBounds[ 1 ], 1 );
	if ( probeImage != NULL ) {
		VK_LightGrid_LoadImage( probeImage );
		if ( probeImage->IsDefaulted() || probeImage->GetOpts().width != probeImageWidth
				|| probeImage->GetOpts().height != probeImageHeight ) {
			probeImage = NULL;
		}
	}
	if ( !VK_Exec_BindTriGeometry( pass.cmd, pass.slot, tri ) ) {
		if ( stats != NULL ) {
			stats->cacheFail++;
		}
		return false;
	}

	vkLightGridBlock_t block;
	memset( &block, 0, sizeof( block ) );
	VK_LightGrid_ModelRows( surf->space->modelMatrix, block.modelRow0, block.modelRow1, block.modelRow2 );
	for ( int i = 0; i < 3; i++ ) {
		block.gridOrigin[ i ] = lightGrid.lightGridOrigin[ i ];
		block.gridSize[ i ] = lightGrid.lightGridSize[ i ];
		block.gridBounds[ i ] = (float)lightGrid.lightGridBounds[ i ];
	}
	block.gridOrigin[ 3 ] = (float)pass.debugMode;
	block.gridSize[ 3 ] = lightGrid.relocationMaxDistance > 0.0f ? lightGrid.relocationMaxDistance : 48.0f;
	block.gridBounds[ 3 ] = probeImage != NULL ? 1.0f : 0.0f;
	block.atlasInfo[ 0 ] = 1.0f / (float)atlasWidth;
	block.atlasInfo[ 1 ] = 1.0f / (float)atlasHeight;
	block.atlasInfo[ 2 ] = (float)lightGrid.imageSingleProbeSize;
	block.atlasInfo[ 3 ] = (float)lightGrid.imageBorderSize;
	block.visibilityInfo[ 0 ] = lightGrid.visibilityMaxDistance > 0.0f ? lightGrid.visibilityMaxDistance : 4096.0f;
	block.visibilityInfo[ 1 ] = 3.0f;
	block.visibilityInfo[ 2 ] = idMath::ClampFloat( 0.0f, 1.0f, r_lightGridVisibilityFloor.GetFloat() );
	block.visibilityInfo[ 3 ] = 2.0f;
	const bool usePortalBlend = portalBlend != NULL && portalBlend->blendDistance > 0.0f
		&& !portalBlend->portalBounds.IsCleared();
	block.blendInfo[ 0 ] = idMath::ClampFloat( 0.0f, 16.0f, r_lightGridIntensity.GetFloat() );
	block.blendInfo[ 1 ] = usePortalBlend ? ( invertPortalBlend ? -1.0f : 1.0f ) : 0.0f;
	block.blendInfo[ 2 ] = usePortalBlend ? portalBlend->blendDistance : 0.0f;
	block.blendInfo[ 3 ] = idMath::ClampFloat( 0.25f, 4.0f, r_lightGridIrradianceGamma.GetFloat() );
	if ( usePortalBlend ) {
		for ( int i = 0; i < 4; i++ ) {
			block.portalPlane[ i ] = portalBlend->portalPlane[ i ];
		}
		for ( int i = 0; i < 3; i++ ) {
			block.portalBoundsMin[ i ] = portalBlend->portalBounds[ 0 ][ i ];
			block.portalBoundsMax[ i ] = portalBlend->portalBounds[ 1 ][ i ];
		}
	}
	block.portalBoundsMin[ 3 ] = idMath::ClampFloat( 0.0f, 16.0f, r_lightGridMaxContribution.GetFloat() );
	const bool depthCompare = pass.depthTextureCompare && pass.debugMode != 3;
	block.depthInfo[ 0 ] = depthCompare ? 1.0f / (float)pass.depthWidth : 1.0f;
	block.depthInfo[ 1 ] = depthCompare ? 1.0f / (float)pass.depthHeight : 1.0f;
	block.depthInfo[ 2 ] = idMath::ClampFloat( 0.0f, 0.1f, r_lightGridDepthTolerance.GetFloat() );
	block.depthInfo[ 3 ] = depthCompare ? 1.0f : 0.0f;
	block.depthViewport[ 0 ] = (float)pass.viewDef->viewport.x1;
	block.depthViewport[ 1 ] = (float)pass.viewDef->viewport.y1;
	block.depthViewport[ 3 ] = (float)pass.fbHeight;

	VkDescriptorSet gridSets[ 4 ] = {
		VK_Scene_ImageSet( irradianceImage ),
		VK_Scene_ImageSet( visibilityImage != NULL ? visibilityImage : globalImages->whiteImage ),
		VK_Scene_ImageSet( probeImage != NULL ? probeImage : globalImages->blackImage ),
		VK_Scene_ImageSet( depthCompare ? globalImages->currentDepthImage : globalImages->whiteImage )
	};

	// weapon receivers keep the weapon depth hack (RB_EnterWeaponDepthHack)
	float mvp[ 16 ];
	VK_BuildSurfMVP( pass.viewDef, surf, mvp );
	const bool weaponRange = surf->space->weaponDepthHack;
	if ( weaponRange != pass.weaponRange ) {
		pass.weaponRange = weaponRange;
		VK_Exec_SetViewViewport( pass.cmd, pass.viewDef, weaponRange ? 0.5f : 1.0f );
	}
	VK_Exec_SetSurfScissor( pass.cmd, pass.viewDef, surf, pass.fbHeight );
	VK_Scene_SetCull( pass.cmd, pass.viewDef, shader->GetCullType() );

	idVec4 identity[ 2 ];
	VK_LightGrid_SetIdentityMatrix( identity );
	if ( receiverOnly ) {
		vkLightGridAlbedo_t albedo;
		VK_LightGrid_FindRepresentativeAlbedo( surf, albedo );
		idVec4 flatDiffuseParams;
		flatDiffuseParams.Zero();
		if ( albedo.stage != NULL && albedo.stage->lighting == SL_DIFFUSE ) {
			RB_GetFlatDiffuseParams( surf, flatDiffuseParams );
		}
		memcpy( block.diffuseColor, albedo.color, sizeof( block.diffuseColor ) );
		memcpy( block.flatDiffuseParams, flatDiffuseParams.ToFloatPtr(), sizeof( block.flatDiffuseParams ) );
		block.portalBoundsMax[ 3 ] = albedo.vertexColorParams[ 0 ];
		block.depthViewport[ 2 ] = albedo.vertexColorParams[ 1 ];
		if ( !VK_LightGrid_Draw( pass, surf, mvp, globalImages->flatNormalMap, identity,
				albedo.image, albedo.matrix, block, gridSets ) ) {
			return false;
		}
		if ( stats != NULL ) {
			stats->stageSubmit++;
		}
		return true;
	}

	// r_lightGridDebug 4: every albedo stage, with the bump stage before it
	idImage *currentBumpImage = globalImages->flatNormalMap;
	idVec4 currentBumpMatrix[ 2 ];
	VK_LightGrid_SetIdentityMatrix( currentBumpMatrix );
	bool submitted = false;
	for ( int stageIndex = 0; stageIndex < shader->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( stage->lighting == SL_BUMP ) {
			if ( !r_skipBump.GetBool() && VK_LightGrid_StageActive( stage, regs ) ) {
				VK_Interactions_SetDrawInteraction( stage, regs, &currentBumpImage, currentBumpMatrix, NULL );
				if ( currentBumpImage == NULL ) {
					currentBumpImage = globalImages->flatNormalMap;
				}
			}
			continue;
		}
		if ( !VK_LightGrid_StageCanProvideAlbedo( shader, stage, regs ) ) {
			if ( stats != NULL ) {
				stats->stageReject++;
			}
			continue;
		}
		idImage *diffuseImage = globalImages->whiteImage;
		idVec4 diffuseMatrix[ 2 ];
		float diffuseColor[ 4 ];
		idVec4 flatDiffuseParams;
		VK_LightGrid_StageAlbedo( surf, stage, &diffuseImage, diffuseMatrix, diffuseColor, flatDiffuseParams );
		float vertexColorParams[ 2 ];
		VK_LightGrid_VertexColorParams( stage->vertexColor, vertexColorParams );
		memcpy( block.diffuseColor, diffuseColor, sizeof( block.diffuseColor ) );
		memcpy( block.flatDiffuseParams, flatDiffuseParams.ToFloatPtr(), sizeof( block.flatDiffuseParams ) );
		block.portalBoundsMax[ 3 ] = vertexColorParams[ 0 ];
		block.depthViewport[ 2 ] = vertexColorParams[ 1 ];
		idImage *bumpImage = r_skipBump.GetBool() ? globalImages->flatNormalMap : currentBumpImage;
		if ( VK_LightGrid_Draw( pass, surf, mvp, bumpImage, currentBumpMatrix, diffuseImage, diffuseMatrix,
				block, gridSets ) ) {
			submitted = true;
			if ( stats != NULL ) {
				stats->stageSubmit++;
			}
		}
	}
	return submitted;
}

// RB_STD_SetLightGridDrawState, as a pipeline plus dynamic state
static VkPipeline VK_LightGrid_SetDrawState( vkLightGridPassState_t &pass ) {
	const int debugMode = pass.debugMode;
	const bool replace = debugMode == 2 || debugMode == 4 || debugMode == 5 || debugMode == 6 || debugMode == 7;
	const bool coverageNoDepth = debugMode == 3;
	const bool depthTextureDebug = debugMode == 6 || debugMode == 7;
	const bool depthTextureCompare = !coverageNoDepth && pass.depthTextureCompare;
	const bool disableHardwareDepth = coverageNoDepth || ( depthTextureCompare && depthTextureDebug );
	const float biasFactor = r_lightGridDepthBiasFactor.GetFloat();
	const float biasUnits = r_lightGridDepthBiasUnits.GetFloat();
	const bool depthBias = !coverageNoDepth && !depthTextureCompare && ( biasFactor != 0.0f || biasUnits != 0.0f );

	VK_Scene_SetDepth( pass.cmd, !disableHardwareDepth,
			depthBias ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_EQUAL );
	vkCmdSetDepthBiasEnable( pass.cmd, depthBias ? VK_TRUE : VK_FALSE );
	if ( depthBias ) {
		// glPolygonOffset( factor, units ); Vulkan takes units first
		vkCmdSetDepthBias( pass.cmd, biasUnits, 0.0f, biasFactor );
	}
	vkCmdSetStencilTestEnable( pass.cmd, VK_FALSE );
	vkCmdSetFrontFace( pass.cmd, VK_Exec_CanonicalFrontFace() );

	const int blendBits = replace ? ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO ) : ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	return VK_Scene_Pipeline( VK_SCENE_LIGHT_GRID, VK_SCENE_MODULE_LIGHT_GRID_VERT,
			VK_SCENE_MODULE_LIGHT_GRID_FRAG, blendBits, 0 );
}

static bool VK_SceneEffects_DrawLightGrid( const viewDef_t *viewDef ) {
	if ( VK_PBR_BakedViewReady( viewDef ) && VK_HDRScene_Accumulating() ) { return false; }
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() || viewDef->viewEntitys == NULL ) {
		return false;
	}
	// stock content ships no baked grids; without one the pass draws nothing
	idRenderWorldLocal *world = viewDef->renderWorld;
	if ( world == NULL || !world->AnyLightGridAvailable() ) {
		return false;
	}

	vkLightGridPassState_t pass;
	memset( &pass, 0, sizeof( pass ) );
	pass.viewDef = viewDef;
	pass.cmd = VK_Exec_ActiveCmd();
	pass.slot = VK_Exec_ActiveFrameSlot();
	pass.fbHeight = VK_Exec_ActiveFramebufferHeight();
	pass.debugMode = r_lightGridDebug.GetInteger();
	if ( pass.cmd == VK_NULL_HANDLE ) {
		return false;
	}

	if ( vkLightGridPreparedResidencyView != viewDef || vkLightGridPreparedResidencyFrame != backEnd.frameCount ) {
		VK_LightGrid_UpdateResidency( viewDef );
	}

	// RB_PrepareLightGridDepthTexture: the shader's own depth test reads the
	// prepass depth, which ambient stages have not changed since
	if ( pass.debugMode != 3 && globalImages->currentDepthImage != NULL && VK_Exec_CaptureViewDepth( viewDef ) ) {
		pass.depthWidth = globalImages->currentDepthImage->GetOpts().width;
		pass.depthHeight = globalImages->currentDepthImage->GetOpts().height;
		pass.depthTextureCompare = pass.depthWidth > 0 && pass.depthHeight > 0;
	}
	if ( !VK_Exec_MainRenderingScopeOpen() ) {
		return false;
	}

	pass.pipeline = VK_LightGrid_SetDrawState( pass );
	if ( pass.pipeline == VK_NULL_HANDLE ) {
		return false;
	}
	vkCmdBindPipeline( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline );
	VK_Exec_SetViewViewport( pass.cmd, viewDef, 1.0f );

	const int reportFrames = r_lightGridReport.GetInteger();
	const bool reportStats = reportFrames > 0 && ( backEnd.frameCount % reportFrames ) == 0;
	vkLightGridDrawStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	int worldConsidered = 0, worldPostSkipped = 0, worldNoGrid = 0, worldNoAlbedo = 0;
	int worldGrid = 0, worldSubmitted = 0, weaponGrid = 0, weaponSubmitted = 0;
	const bool receiverOnly = VK_LightGrid_ReceiverOnlySubmission( pass.debugMode );

	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[ i ];
		worldConsidered++;
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			worldPostSkipped++;
			continue;
		}
		const LightGrid *lightGrid = NULL;
		if ( !VK_LightGrid_SurfaceHasGrid( surf, lightGrid ) ) {
			worldNoGrid++;
			continue;
		}
		if ( !receiverOnly && !VK_LightGrid_HasActiveAlbedoStage( surf->material, surf->shaderRegisters ) ) {
			worldNoAlbedo++;
			continue;
		}
		worldGrid++;
		if ( VK_LightGrid_DrawSurface( pass, surf, *lightGrid, NULL, false, reportStats ? &stats : NULL ) ) {
			worldSubmitted++;
		}
	}

	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[ i ];
		if ( surf == NULL || surf->material == NULL
				|| surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			continue;
		}
		const LightGrid *lightGrid = NULL;
		if ( !VK_LightGrid_SurfaceHasViewWeaponGrid( viewDef, surf, lightGrid ) ) {
			continue;
		}
		weaponGrid++;
		bool submitted = false;
		vkLightGridPortalBlend_t portalBlend;
		if ( VK_LightGrid_PortalBlend( viewDef, surf, portalBlend ) ) {
			submitted |= VK_LightGrid_DrawSurface( pass, surf, *lightGrid, &portalBlend, true, reportStats ? &stats : NULL );
			submitted |= VK_LightGrid_DrawSurface( pass, surf, *portalBlend.neighborLightGrid, &portalBlend, false,
					reportStats ? &stats : NULL );
		} else {
			submitted |= VK_LightGrid_DrawSurface( pass, surf, *lightGrid, NULL, false, reportStats ? &stats : NULL );
		}
		if ( submitted ) {
			weaponSubmitted++;
		}
	}
	if ( pass.weaponRange ) {
		VK_Exec_SetViewViewport( pass.cmd, viewDef, 1.0f );
	}
	vkCmdSetDepthBiasEnable( pass.cmd, VK_FALSE );

	if ( reportStats ) {
		common->Printf(
			"LightGrid receiver stats: frame %i debug=%i world considered=%i postSkip=%i noGrid=%i noAlbedo=%i grid=%i submitted=%i weaponGrid=%i weaponSubmitted=%i draw null=%i noAlb=%i cache=%i empty=%i noIrr=%i ensure=%i default=%i badAtlas=%i stageReject=%i stageSubmit=%i\n",
			backEnd.frameCount, pass.debugMode, worldConsidered, worldPostSkipped, worldNoGrid, worldNoAlbedo,
			worldGrid, worldSubmitted, weaponGrid, weaponSubmitted, stats.nullInput, stats.noAlbedo,
			stats.cacheFail, stats.emptyGeometry, stats.noIrradiance, stats.ensureFail, stats.defaultIrradiance,
			stats.badAtlas, stats.stageReject, stats.stageSubmit );
	}
	const bool drew = worldSubmitted > 0 || weaponSubmitted > 0;
	if ( drew ) {
		static bool logged = false;
		if ( !logged ) {
			logged = true;
			common->Printf( "Vulkan: first light-grid pass drew %d world and %d weapon receivers\n",
					worldSubmitted, weaponSubmitted );
		}
	}
	return drew;
}

/*
===============================================================================

	Player visibility effects (RB_STD_DrawPlayerVisibilityEffects) and cel
	outline shells (RB_STD_DrawCelOutlines)

	The outline is a shell pushed out along the screen-space normal and
	masked against the body in stencil bit 0 (RB_PLAYER_OUTLINE_STENCIL_BIT),
	so it rings the silhouette instead of covering the body. OpenGL clears
	just that bit with a masked glClear; vkCmdClearAttachments ignores the
	write mask, so Vulkan clears the whole stencil to 128, the value the
	scene starts from, which leaves bit 0 clear. Nothing after the
	interactions reads the shadow stencil.

===============================================================================
*/

static const uint32_t VK_OUTLINE_STENCIL_BIT = 1;
static const float VK_PLAYER_OUTLINE_MIN_WIDTH = 0.5f;
static const float VK_PLAYER_OUTLINE_MAX_WIDTH = 6.0f;

typedef struct vkOutlinePush_s {
	float	mvp[ 16 ];
	float	color[ 4 ];
	float	outlineParams[ 4 ];
} vkOutlinePush_t;

// std140 layout of RimlightBlock in player_rimlight.vert/.frag
typedef struct vkRimlightBlock_s {
	float	modelRow0[ 4 ];
	float	modelRow1[ 4 ];
	float	modelRow2[ 4 ];
	float	viewOrigin[ 4 ];
	float	color[ 4 ];
	float	rimParams[ 4 ];
} vkRimlightBlock_t;

// RB_PlayerVisibilityEffectsSurfaceAllowed
static bool VK_Player_SurfaceAllowed( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->space->entityDef == NULL || surf->geo == NULL
			|| surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 || surf->geo->numIndexes <= 0 ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	const idMaterial *shader = surf->material;
	if ( !shader->IsDrawn() || shader->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	return !shader->IsPortalSky() && !shader->SuppressInSubview()
		&& shader->GetSort() < SS_POST_PROCESS && !shader->HasGui();
}

static bool VK_Player_HasOutline( const renderEntity_t &renderEntity ) {
	return renderEntity.outlineColor[ 3 ] > 0.0f && renderEntity.outlineWidth > 0.0f;
}

static bool VK_Player_IsSeeThrough( const renderEntity_t &renderEntity ) {
	return ( renderEntity.outlineFlags & ( REF_OUTLINE_NODEPTH | REF_OUTLINE_THROUGH_WORLD ) ) != 0;
}

typedef struct vkSceneOverlayPass_s {
	const viewDef_t *	viewDef;
	VkCommandBuffer		cmd;
	int					slot;
	int					fbHeight;
	bool				maskSilhouette;
} vkSceneOverlayPass_t;

static bool VK_Scene_BindSurface( const vkSceneOverlayPass_t &pass, const drawSurf_t *surf, float mvp[ 16 ] ) {
	const srfTriangles_t *tri = surf->geo;
	if ( tri == NULL || tri->ambientCache == NULL || tri->numIndexes <= 0
			|| !VK_Exec_BindTriGeometry( pass.cmd, pass.slot, tri ) ) {
		return false;
	}
	VK_Scene_PlainMVP( pass.viewDef, surf, mvp );
	return true;
}

static void VK_Scene_DrawOutlinePush( const vkSceneOverlayPass_t &pass, const drawSurf_t *surf,
		const float mvp[ 16 ], const float color[ 4 ], float width ) {
	const int viewportWidth = Max( 1, pass.viewDef->viewport.x2 - pass.viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, pass.viewDef->viewport.y2 - pass.viewDef->viewport.y1 + 1 );
	vkOutlinePush_t push;
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	memcpy( push.color, color, sizeof( push.color ) );
	push.outlineParams[ 0 ] = width;
	push.outlineParams[ 1 ] = 2.0f / (float)viewportWidth;
	push.outlineParams[ 2 ] = 2.0f / (float)viewportHeight;
	push.outlineParams[ 3 ] = 0.0f;
	vkCmdPushConstants( pass.cmd, VK_Exec_InteractionPipelineLayout(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	vkCmdDrawIndexed( pass.cmd, VK_Device_DrawIndexCount( surf->geo->numIndexes ), 1, 0, 0, 0 );
}

// Clears the stencil over the view scissor, leaving the outline bit clear.
static void VK_Scene_ClearOutlineStencil( const vkSceneOverlayPass_t &pass ) {
	const viewDef_t *viewDef = pass.viewDef;
	const int x0 = viewDef->viewport.x1 + viewDef->scissor.x1;
	const int y0GL = viewDef->viewport.y1 + viewDef->scissor.y1;
	const int width = viewDef->scissor.x2 - viewDef->scissor.x1 + 1;
	const int height = viewDef->scissor.y2 - viewDef->scissor.y1 + 1;
	int x1 = Max( 0, x0 );
	int y1 = Max( 0, VK_Exec_ActiveLowerOrigin() ? y0GL : pass.fbHeight - y0GL - height );
	const int x2 = Min( VK_Exec_ActiveFramebufferWidth(), x0 + width );
	const int y2 = Min( pass.fbHeight, VK_Exec_ActiveLowerOrigin() ? y0GL + height : pass.fbHeight - y0GL );
	if ( x2 <= x1 || y2 <= y1 ) {
		return;
	}
	VkClearAttachment clear;
	memset( &clear, 0, sizeof( clear ) );
	clear.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	clear.clearValue.depthStencil.stencil = 128;
	VkClearRect rect;
	memset( &rect, 0, sizeof( rect ) );
	rect.rect.offset.x = x1;
	rect.rect.offset.y = y1;
	rect.rect.extent.width = (uint32_t)( x2 - x1 );
	rect.rect.extent.height = (uint32_t)( y2 - y1 );
	rect.layerCount = 1;
	vkCmdClearAttachments( pass.cmd, 1, &clear, 1, &rect );
}

static void VK_Scene_SetOutlineStencil( VkCommandBuffer cmd, VkCompareOp compareOp ) {
	vkCmdSetStencilTestEnable( cmd, VK_TRUE );
	vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE,
			VK_STENCIL_OP_KEEP, compareOp );
	vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_OUTLINE_STENCIL_BIT );
	vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_OUTLINE_STENCIL_BIT );
	vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_OUTLINE_STENCIL_BIT );
}

static void VK_Scene_RestoreStencil( VkCommandBuffer cmd ) {
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
	vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
	vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
	vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 128 );
}

// RB_PlayerVisibilityMaskOutlineSurface / RB_CelMaskOutlineSurface: mark the
// pixels the body covers (all of them for a see-through entity)
static void VK_Scene_MaskSurface( const vkSceneOverlayPass_t &pass, const drawSurf_t *surf, bool seeThrough,
		VkPipeline &bound ) {
	float mvp[ 16 ];
	if ( !VK_Scene_BindSurface( pass, surf, mvp ) ) {
		return;
	}
	const VkPipeline pipeline = VK_Scene_Pipeline( VK_SCENE_OUTLINE_MASK, VK_SCENE_MODULE_OUTLINE_VERT,
			VK_SCENE_MODULE_OUTLINE_FRAG, 0, VK_EXTRA_PIPELINE_COLOR_OFF );
	if ( pipeline == VK_NULL_HANDLE ) {
		return;
	}
	if ( pipeline != bound ) {
		vkCmdBindPipeline( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		bound = pipeline;
	}
	VK_Exec_SetViewScissor( pass.cmd, pass.viewDef, pass.fbHeight );
	VK_Scene_SetDepth( pass.cmd, true, seeThrough ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL );
	VK_Scene_SetCull( pass.cmd, pass.viewDef, surf->material->GetCullType() );
	const float noColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	VK_Scene_DrawOutlinePush( pass, surf, mvp, noColor, 0.0f );
}

// RB_PlayerVisibilityDrawOutlineSurface / RB_CelDrawOutlineSurface: the far
// side of the extruded shell, so the interior fails the depth test as well as
// the mask and only the band past the silhouette shows
static bool VK_Scene_DrawShell( const vkSceneOverlayPass_t &pass, const drawSurf_t *surf, const float color[ 4 ],
		float width, bool ignoreDepth, VkPipeline &bound ) {
	if ( surf->material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	float mvp[ 16 ];
	if ( !VK_Scene_BindSurface( pass, surf, mvp ) ) {
		return false;
	}
	const VkPipeline pipeline = VK_Scene_Pipeline( VK_SCENE_OUTLINE_SHELL, VK_SCENE_MODULE_OUTLINE_VERT,
			VK_SCENE_MODULE_OUTLINE_FRAG, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, 0 );
	if ( pipeline == VK_NULL_HANDLE ) {
		return false;
	}
	if ( pipeline != bound ) {
		vkCmdBindPipeline( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		bound = pipeline;
	}
	VK_Exec_SetViewScissor( pass.cmd, pass.viewDef, pass.fbHeight );
	VK_Scene_SetDepth( pass.cmd, true, ignoreDepth ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_LESS_OR_EQUAL );
	VK_Scene_SetCull( pass.cmd, pass.viewDef, CT_BACK_SIDED );
	VK_Scene_DrawOutlinePush( pass, surf, mvp, color, width );
	return true;
}

static int VK_Player_OutlineOnlySurfaces( const viewDef_t *viewDef, drawSurf_t **&surfs ) {
	if ( viewDef->outlineDrawSurfs == NULL || viewDef->numOutlineDrawSurfs <= 0 ) {
		surfs = NULL;
		return 0;
	}
	surfs = viewDef->outlineDrawSurfs;
	return viewDef->numOutlineDrawSurfs;
}

// RB_PlayerVisibilityDrawOutlineGroup
static bool VK_Player_DrawOutlineGroup( const vkSceneOverlayPass_t &pass, drawSurf_t **drawSurfs,
		int numDrawSurfs, bool seeThroughGroup ) {
	drawSurf_t **outlineOnly = NULL;
	int numOutlineOnly = 0;
	if ( seeThroughGroup && pass.maskSilhouette ) {
		numOutlineOnly = VK_Player_OutlineOnlySurfaces( pass.viewDef, outlineOnly );
	}
	VkPipeline bound = VK_NULL_HANDLE;
	if ( pass.maskSilhouette ) {
		VK_Scene_ClearOutlineStencil( pass );
		VK_Scene_SetOutlineStencil( pass.cmd, VK_COMPARE_OP_ALWAYS );
		for ( int list = 0; list < 2; list++ ) {
			drawSurf_t **surfs = list == 0 ? drawSurfs : outlineOnly;
			const int count = list == 0 ? numDrawSurfs : numOutlineOnly;
			for ( int i = 0; i < count; i++ ) {
				const drawSurf_t *surf = surfs[ i ];
				if ( !VK_Player_SurfaceAllowed( surf ) ) {
					continue;
				}
				const renderEntity_t &renderEntity = surf->space->entityDef->parms;
				if ( !VK_Player_HasOutline( renderEntity )
						|| ( !seeThroughGroup && VK_Player_IsSeeThrough( renderEntity ) ) ) {
					continue;
				}
				VK_Scene_MaskSurface( pass, surf, VK_Player_IsSeeThrough( renderEntity ), bound );
			}
		}
		VK_Scene_SetOutlineStencil( pass.cmd, VK_COMPARE_OP_NOT_EQUAL );
	}

	bool submitted = false;
	for ( int list = 0; list < 2; list++ ) {
		drawSurf_t **surfs = list == 0 ? drawSurfs : outlineOnly;
		const int count = list == 0 ? numDrawSurfs : numOutlineOnly;
		for ( int i = 0; i < count; i++ ) {
			const drawSurf_t *surf = surfs[ i ];
			if ( !VK_Player_SurfaceAllowed( surf ) ) {
				continue;
			}
			const renderEntity_t &renderEntity = surf->space->entityDef->parms;
			if ( !VK_Player_HasOutline( renderEntity ) || VK_Player_IsSeeThrough( renderEntity ) != seeThroughGroup ) {
				continue;
			}
			const float width = idMath::ClampFloat( VK_PLAYER_OUTLINE_MIN_WIDTH, VK_PLAYER_OUTLINE_MAX_WIDTH,
					renderEntity.outlineWidth );
			const bool ignoreDepth = pass.maskSilhouette && VK_Player_IsSeeThrough( renderEntity );
			if ( VK_Scene_DrawShell( pass, surf, renderEntity.outlineColor.ToFloatPtr(), width, ignoreDepth, bound ) ) {
				submitted = true;
			}
		}
	}
	return submitted;
}

static bool VK_SceneEffects_DrawPlayerVisibility( const viewDef_t *viewDef, drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( viewDef->viewEntitys == NULL || drawSurfs == NULL || numDrawSurfs <= 0
			|| r_skipPlayerVisibilityEffects.GetBool() ) {
		return false;
	}
	// RB_PlayerVisibilityGatherWork: one scan decides what has work
	bool brightSkin = false, rimlight = false, outlineDepthTested = false, outlineSeeThrough = false;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[ i ];
		if ( !VK_Player_SurfaceAllowed( surf ) ) {
			continue;
		}
		const renderEntity_t &renderEntity = surf->space->entityDef->parms;
		brightSkin = brightSkin || renderEntity.brightSkinColor[ 3 ] > 0.0f;
		rimlight = rimlight || renderEntity.rimlightColor[ 3 ] > 0.0f;
		if ( VK_Player_HasOutline( renderEntity ) ) {
			if ( VK_Player_IsSeeThrough( renderEntity ) ) {
				outlineSeeThrough = true;
			} else {
				outlineDepthTested = true;
			}
		}
	}
	drawSurf_t **outlineOnly = NULL;
	if ( VK_Player_OutlineOnlySurfaces( viewDef, outlineOnly ) > 0 ) {
		outlineSeeThrough = true;
	}
	if ( !brightSkin && !rimlight && !outlineDepthTested && !outlineSeeThrough ) {
		return false;
	}

	vkSceneOverlayPass_t pass;
	pass.viewDef = viewDef;
	pass.cmd = VK_Exec_ActiveCmd();
	pass.slot = VK_Exec_ActiveFrameSlot();
	pass.fbHeight = VK_Exec_ActiveFramebufferHeight();
	pass.maskSilhouette = VK_Exec_ActiveTargetHasStencil();
	if ( pass.cmd == VK_NULL_HANDLE || !VK_Exec_MainRenderingScopeOpen() ) {
		return false;
	}
	VK_Exec_SetViewViewport( pass.cmd, viewDef, 1.0f );
	vkCmdSetFrontFace( pass.cmd, VK_Exec_CanonicalFrontFace() );
	vkCmdSetDepthBiasEnable( pass.cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( pass.cmd, VK_FALSE );

	bool submitted = false;
	const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();

	// brightskin: OpenGL's white texture times glColor( rgb * a, a ) under
	// SRC_ALPHA/ONE, so the wash lands as rgb * a * a
	if ( brightSkin ) {
		const VkPipeline pipeline = VK_Scene_Pipeline( VK_SCENE_BRIGHTSKIN, VK_SCENE_MODULE_OUTLINE_VERT,
				VK_SCENE_MODULE_OUTLINE_FRAG, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE, 0 );
		if ( pipeline != VK_NULL_HANDLE ) {
			vkCmdBindPipeline( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
			VK_Scene_SetDepth( pass.cmd, true, VK_COMPARE_OP_EQUAL );
			for ( int i = 0; i < numDrawSurfs; i++ ) {
				const drawSurf_t *surf = drawSurfs[ i ];
				if ( !VK_Player_SurfaceAllowed( surf ) ) {
					continue;
				}
				const renderEntity_t &renderEntity = surf->space->entityDef->parms;
				float mvp[ 16 ];
				if ( renderEntity.brightSkinColor[ 3 ] <= 0.0f || !VK_Scene_BindSurface( pass, surf, mvp ) ) {
					continue;
				}
				const idVec4 &c = renderEntity.brightSkinColor;
				const float color[ 4 ] = { c[ 0 ] * c[ 3 ], c[ 1 ] * c[ 3 ], c[ 2 ] * c[ 3 ], c[ 3 ] };
				VK_Exec_SetSurfScissor( pass.cmd, viewDef, surf, pass.fbHeight );
				VK_Scene_SetCull( pass.cmd, viewDef, surf->material->GetCullType() );
				VK_Scene_DrawOutlinePush( pass, surf, mvp, color, 0.0f );
				submitted = true;
			}
		}
	}

	// rimlight: depth tested even for a see-through entity
	if ( rimlight ) {
		const VkPipeline pipeline = VK_Scene_Pipeline( VK_SCENE_RIMLIGHT, VK_SCENE_MODULE_RIMLIGHT_VERT,
				VK_SCENE_MODULE_RIMLIGHT_FRAG, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, 0 );
		if ( pipeline != VK_NULL_HANDLE ) {
			vkCmdBindPipeline( pass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
			VK_Scene_SetDepth( pass.cmd, true, VK_COMPARE_OP_EQUAL );
			const idVec3 &viewOrg = viewDef->renderView.vieworg;
			for ( int i = 0; i < numDrawSurfs; i++ ) {
				const drawSurf_t *surf = drawSurfs[ i ];
				if ( !VK_Player_SurfaceAllowed( surf ) ) {
					continue;
				}
				const renderEntity_t &renderEntity = surf->space->entityDef->parms;
				float mvp[ 16 ];
				if ( renderEntity.rimlightColor[ 3 ] <= 0.0f || !VK_Scene_BindSurface( pass, surf, mvp ) ) {
					continue;
				}
				vkRimlightBlock_t block;
				memset( &block, 0, sizeof( block ) );
				VK_LightGrid_ModelRows( surf->space->modelMatrix, block.modelRow0, block.modelRow1, block.modelRow2 );
				block.viewOrigin[ 0 ] = viewOrg.x;
				block.viewOrigin[ 1 ] = viewOrg.y;
				block.viewOrigin[ 2 ] = viewOrg.z;
				block.viewOrigin[ 3 ] = 1.0f;
				memcpy( block.color, renderEntity.rimlightColor.ToFloatPtr(), sizeof( block.color ) );
				block.rimParams[ 0 ] = idMath::ClampFloat( RB_PLAYER_RIMLIGHT_MIN_POWER, RB_PLAYER_RIMLIGHT_MAX_POWER,
						r_playerRimlightPower.GetFloat() );
				block.rimParams[ 1 ] = 1.0f;
				block.rimParams[ 2 ] = idMath::ClampFloat( RB_PLAYER_RIMLIGHT_MIN_FLOOR, RB_PLAYER_RIMLIGHT_MAX_FLOOR,
						r_playerRimlightFloor.GetFloat() );
				const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
				if ( uniformOffset < 0 ) {
					continue;
				}
				VK_Scene_BindUniform( pass.cmd, uniformOffset );
				VK_Exec_SetSurfScissor( pass.cmd, viewDef, surf, pass.fbHeight );
				VK_Scene_SetCull( pass.cmd, viewDef, surf->material->GetCullType() );
				vkCmdPushConstants( pass.cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof( float ) * 16, mvp );
				vkCmdDrawIndexed( pass.cmd, VK_Device_DrawIndexCount( surf->geo->numIndexes ), 1, 0, 0, 0 );
				submitted = true;
			}
		}
	}

	// outline: depth tested group first, so a see-through ring over it wins
	if ( outlineDepthTested && VK_Player_DrawOutlineGroup( pass, drawSurfs, numDrawSurfs, false ) ) {
		submitted = true;
	}
	if ( outlineSeeThrough && VK_Player_DrawOutlineGroup( pass, drawSurfs, numDrawSurfs, true ) ) {
		submitted = true;
	}
	VK_Scene_RestoreStencil( pass.cmd );

	if ( submitted ) {
		static bool logged = false;
		if ( !logged ) {
			logged = true;
			common->Printf( "Vulkan: first player visibility effects pass drew (brightskin %d rimlight %d outline %d)\n",
					brightSkin ? 1 : 0, rimlight ? 1 : 0, ( outlineDepthTested || outlineSeeThrough ) ? 1 : 0 );
		}
	}
	return submitted;
}

// RB_STD_DrawCelOutlines
static bool VK_SceneEffects_DrawCelOutlines( const viewDef_t *viewDef, drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( viewDef->viewEntitys == NULL || drawSurfs == NULL || numDrawSurfs <= 0 || !R_CelOutlineEnabled() ) {
		return false;
	}
	bool hasWork = false;
	for ( int i = 0; i < numDrawSurfs && !hasWork; i++ ) {
		hasWork = R_CelOutlineSurfaceActive( drawSurfs[ i ] );
	}
	if ( !hasWork ) {
		return false;
	}

	vkSceneOverlayPass_t pass;
	pass.viewDef = viewDef;
	pass.cmd = VK_Exec_ActiveCmd();
	pass.slot = VK_Exec_ActiveFrameSlot();
	pass.fbHeight = VK_Exec_ActiveFramebufferHeight();
	pass.maskSilhouette = VK_Exec_ActiveTargetHasStencil();
	if ( pass.cmd == VK_NULL_HANDLE || !VK_Exec_MainRenderingScopeOpen() ) {
		return false;
	}
	// OpenGL draws these with the plain projection and the full depth range,
	// view weapon included, and the weapon ring leans on the depth test alone
	VK_Exec_SetViewViewport( pass.cmd, viewDef, 1.0f );
	vkCmdSetFrontFace( pass.cmd, VK_Exec_CanonicalFrontFace() );
	vkCmdSetDepthBiasEnable( pass.cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( pass.cmd, VK_FALSE );

	VkPipeline bound = VK_NULL_HANDLE;
	if ( pass.maskSilhouette ) {
		VK_Scene_ClearOutlineStencil( pass );
		VK_Scene_SetOutlineStencil( pass.cmd, VK_COMPARE_OP_ALWAYS );
		for ( int i = 0; i < numDrawSurfs; i++ ) {
			if ( R_CelOutlineSurfaceActive( drawSurfs[ i ] ) ) {
				VK_Scene_MaskSurface( pass, drawSurfs[ i ], false, bound );
			}
		}
		VK_Scene_SetOutlineStencil( pass.cmd, VK_COMPARE_OP_NOT_EQUAL );
	}

	bool submitted = false;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[ i ];
		if ( !R_CelOutlineSurfaceActive( surf ) ) {
			continue;
		}
		idVec4 color;
		R_CelOutlineColorForSurface( surf, color );
		if ( color.w <= 0.0f ) {
			continue;
		}
		if ( VK_Scene_DrawShell( pass, surf, color.ToFloatPtr(), R_CelOutlineWidthForSurface( surf ), false, bound ) ) {
			submitted = true;
		}
	}
	VK_Scene_RestoreStencil( pass.cmd );

	if ( submitted ) {
		static bool logged = false;
		if ( !logged ) {
			logged = true;
			common->Printf( "Vulkan: first cel outline shell pass drew\n" );
		}
	}
	return submitted;
}

/*
====================
VK_SceneEffects_DrawPreFog

RB_STD_DrawView's steps between the pre-fog material passes and the fog, in
its order: light grid, the r_forceAmbient floor, player visibility effects,
cel outline shells, over
drawSurfs[0..processed), the surfaces sorted before SS_POST_PROCESS. Returns
true when anything drew; the caller then restores its viewport and space
tracking and treats _currentRender as stale.
====================
*/
const char *VK_SceneEffects_HDRRejection( const viewDef_t *viewDef ) {
	// The floor is a display-referred blend against the finished frame, like
	// the overlays below: a view that draws it keeps the display-referred scene.
	if ( r_forceAmbient.GetFloat() > 0.0f ) { return "ambient-floor"; }
	if ( r_useLightGrid.GetBool() && !r_skipDiffuse.GetBool() && viewDef->renderWorld != NULL
			&& viewDef->renderWorld->AnyLightGridAvailable()
			&& !VK_PBR_BakedViewReady( viewDef ) ) { return "baked-lighting"; }
	for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
		const drawSurf_t *surf = viewDef->drawSurfs[i];
		if ( R_CelOutlineEnabled() && R_CelOutlineSurfaceActive( surf ) ) { return "cel-shells"; }
		if ( !r_skipPlayerVisibilityEffects.GetBool() && VK_Player_SurfaceAllowed( surf ) ) {
			const renderEntity_t &entity = surf->space->entityDef->parms;
			if ( entity.brightSkinColor[3] > 0 || entity.rimlightColor[3] > 0 || VK_Player_HasOutline( entity ) ) {
				return "player-visibility";
			}
		}
	}
	drawSurf_t **outlineOnly = NULL;
	if ( !r_skipPlayerVisibilityEffects.GetBool() && VK_Player_OutlineOnlySurfaces( viewDef, outlineOnly ) > 0 ) {
		return "player-visibility";
	}
	return NULL;
}

bool VK_SceneEffects_DrawPreFog( const viewDef_t *viewDef, int processed ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL || !VK_GuiExecutor_FrameIsOpen() ) {
		return false;
	}
	drawSurf_t **drawSurfs = (drawSurf_t **)viewDef->drawSurfs;
	bool drew = false;
	if ( VK_SceneEffects_DrawLightGrid( viewDef ) ) {
		drew = true;
	}
	// The brightness floor follows the ambient and light-grid passes and comes
	// before the readability overlays, which it would otherwise wash out by
	// exactly the amount the player turned it up.
	if ( VK_Exec_DrawAmbientFloor( viewDef ) ) {
		drew = true;
	}
	if ( VK_SceneEffects_DrawPlayerVisibility( viewDef, drawSurfs, processed ) ) {
		drew = true;
	}
	if ( VK_SceneEffects_DrawCelOutlines( viewDef, drawSurfs, processed ) ) {
		drew = true;
	}
	return drew;
}

void VK_SceneEffects_Shutdown( void ) {
	for ( int i = 0; i < VK_SCENE_MODULE_COUNT; i++ ) {
		if ( vkSceneModules[ i ] != VK_NULL_HANDLE && vkCtx.device != VK_NULL_HANDLE ) {
			vkDestroyShaderModule( vkCtx.device, vkSceneModules[ i ], NULL );
		}
		vkSceneModules[ i ] = VK_NULL_HANDLE;
		vkSceneModuleFailed[ i ] = false;
	}
	vkLightGridResidencyWorld = NULL;
	vkLightGridResidencyLastTouched.Clear();
	vkLightGridResidencyFrame = 0;
	vkLightGridPreparedResidencyView = NULL;
	vkLightGridPreparedResidencyFrame = -1;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
