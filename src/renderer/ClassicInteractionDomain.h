// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __CLASSIC_INTERACTION_DOMAIN_H__
#define __CLASSIC_INTERACTION_DOMAIN_H__

#include "ClassicDeformDomain.h"
#include "MaterialResourceTable.h"
#include "ShadowMapProjected.h"

/*
===============================================================================

	Shared whole-view ownership contract for classic fixed interaction lighting.

	Preparation is transactional per ordinary root 3D view.  A ready view owns
	the complete fixed-classic light -> shadow caster -> local/global/translucent
	receiver -> primitive stream in source order.  Every value needed by a
	backend is sealed here; retained legacy pointers are geometry/identity
	bridges, not permission to reread material stages or shader registers.

	Unsupported shadow-map caster variants (currently translucent moments),
	custom lighting, deforms, GPU-palette skinning, depth hacks, mutable resources, and
	renderer-specific interaction variants are explicit whole-view rollback
	boundaries. A failed view publishes no light, shadow,
	surface, primitive, alpha-stage, or texture range, so the unchanged classic
	renderer can execute it exactly once.

===============================================================================
*/

const int CLASSIC_INTERACTION_DOMAIN_MAX_VIEWS = SCENE_PACKET_MAX_SCENES;
const int CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS = SCENE_PACKET_MAX_DRAWS;
const int CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES = SCENE_PACKET_MAX_DRAWS;
const int CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES = SCENE_PACKET_MAX_DRAWS * 4;
const int CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES = SCENE_PACKET_MAX_DRAWS * 2;
const int CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS = SCENE_PACKET_MAX_DRAWS;
const int CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES =
	SCENE_PACKET_MAX_DRAWS * 2;
const int CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES =
	CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS * 2;

enum classicInteractionDomainReceiver_t {
	CLASSIC_INTERACTION_RECEIVER_LOCAL = 0,
	CLASSIC_INTERACTION_RECEIVER_GLOBAL,
	CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT,
	CLASSIC_INTERACTION_RECEIVER_COUNT
};

enum classicInteractionDomainPrimitiveDisposition_t {
	CLASSIC_INTERACTION_PRIMITIVE_DRAW = 0,
	CLASSIC_INTERACTION_PRIMITIVE_NOOP_INACTIVE_LIGHT,
	CLASSIC_INTERACTION_PRIMITIVE_NOOP_INACTIVE_SURFACE,
	CLASSIC_INTERACTION_PRIMITIVE_NOOP_MISSING_BUMP,
	CLASSIC_INTERACTION_PRIMITIVE_NOOP_BLACK,
	CLASSIC_INTERACTION_PRIMITIVE_COUNT
};

enum classicInteractionDomainDepth_t {
	CLASSIC_INTERACTION_DEPTH_EQUAL = 0,
	CLASSIC_INTERACTION_DEPTH_LESS_OR_EQUAL,
	CLASSIC_INTERACTION_DEPTH_COUNT
};

enum classicInteractionDomainShadowMode_t {
	CLASSIC_INTERACTION_SHADOW_NONE = 0,
	CLASSIC_INTERACTION_SHADOW_STENCIL,
	CLASSIC_INTERACTION_SHADOW_PROJECTED,
	CLASSIC_INTERACTION_SHADOW_POINT,
	CLASSIC_INTERACTION_SHADOW_HYBRID,
	CLASSIC_INTERACTION_SHADOW_MODE_COUNT
};

enum classicInteractionDomainShadowChain_t {
	CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL = 0,
	CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_TRANSLUCENT,
	CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT,
	CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL,
	CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL,
	CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT
};

enum classicInteractionDomainShadowDisposition_t {
	CLASSIC_INTERACTION_SHADOW_CASTER_DRAW = 0,
	CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY,
	CLASSIC_INTERACTION_SHADOW_CASTER_DISPOSITION_COUNT
};

enum classicInteractionDomainShadowIndexSelection_t {
	CLASSIC_INTERACTION_SHADOW_INDEX_FULL = 0,
	CLASSIC_INTERACTION_SHADOW_INDEX_NO_FRONT_CAPS,
	CLASSIC_INTERACTION_SHADOW_INDEX_NO_CAPS,
	CLASSIC_INTERACTION_SHADOW_INDEX_AMBIENT,
	CLASSIC_INTERACTION_SHADOW_INDEX_SELECTION_COUNT
};

enum classicInteractionDomainShadowMapPassDisposition_t {
	CLASSIC_INTERACTION_SHADOW_MAP_PASS_UNUSED = 0,
	CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED,
	CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID,
	CLASSIC_INTERACTION_SHADOW_MAP_PASS_DISPOSITION_COUNT
};

enum classicInteractionDomainFailure_t {
	CLASSIC_INTERACTION_FAILURE_NONE = 0,
	CLASSIC_INTERACTION_FAILURE_UNAVAILABLE,
	CLASSIC_INTERACTION_FAILURE_SCENE_PACKET_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_NOT_PREPARED,
	CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_VIEW_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_LIGHT_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_SURFACE_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_PRIMITIVE_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_SHADOW_ALPHA_STAGE_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_TEXTURE_POOL_OVERFLOW,
	CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_VIEW,
	CLASSIC_INTERACTION_FAILURE_INVALID_SCENE_RANGE,
	CLASSIC_INTERACTION_FAILURE_INVALID_INTERACTION_PASS,
	CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_RANGE,
	CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
	CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_PACKET,
	CLASSIC_INTERACTION_FAILURE_MISSING_GEOMETRY_RECORD,
	CLASSIC_INTERACTION_FAILURE_MISSING_INSTANCE_RECORD,
	CLASSIC_INTERACTION_FAILURE_MISSING_MATERIAL_RECORD,
	CLASSIC_INTERACTION_FAILURE_STALE_MATERIAL_RECORD,
	CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
	CLASSIC_INTERACTION_FAILURE_REGISTER_OUT_OF_RANGE,
	CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
	CLASSIC_INTERACTION_FAILURE_SHADOWS,
	CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
	CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
	CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY,
	CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING,
	CLASSIC_INTERACTION_FAILURE_DEFORM,
	CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT,
	CLASSIC_INTERACTION_FAILURE_SKINNING,
	CLASSIC_INTERACTION_FAILURE_SPECIAL_SURFACE,
	CLASSIC_INTERACTION_FAILURE_DEPTH_HACK,
	CLASSIC_INTERACTION_FAILURE_NEGATIVE_SCALE,
	CLASSIC_INTERACTION_FAILURE_FLAT_DIFFUSE,
	CLASSIC_INTERACTION_FAILURE_ENHANCED_MATERIAL,
	CLASSIC_INTERACTION_FAILURE_CEL_SHADING,
	CLASSIC_INTERACTION_FAILURE_ALTERNATE_INTERACTION_PROGRAM,
	CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
	CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
	CLASSIC_INTERACTION_FAILURE_DYNAMIC_RESOURCE,
	CLASSIC_INTERACTION_FAILURE_MISSING_RESOURCE,
	CLASSIC_INTERACTION_FAILURE_DEFAULTED_RESOURCE,
	CLASSIC_INTERACTION_FAILURE_UNLOADED_RESOURCE,
	CLASSIC_INTERACTION_FAILURE_BACKEND_NOT_READY,
	CLASSIC_INTERACTION_FAILURE_BACKEND_COVERAGE_MISMATCH,
	CLASSIC_INTERACTION_FAILURE_BACKEND_REJECTED,
	CLASSIC_INTERACTION_FAILURE_COUNT
};

enum classicInteractionDomainBackend_t {
	CLASSIC_INTERACTION_BACKEND_GL = 0,
	CLASSIC_INTERACTION_BACKEND_VULKAN,
	CLASSIC_INTERACTION_BACKEND_COUNT
};

enum classicInteractionDomainBackendOutcome_t {
	CLASSIC_INTERACTION_BACKEND_UNRECORDED = 0,
	CLASSIC_INTERACTION_BACKEND_OWNED,
	CLASSIC_INTERACTION_BACKEND_FALLBACK
};

typedef struct classicInteractionDomainTexture_s {
	std::uint64_t		textureResourceId;
	const idImage		*image;
	std::uint64_t		storageGeneration;
	std::uint64_t		nameHash;
	unsigned int		textureHandle;
	textureFilter_t		filter;
	textureRepeat_t		repeat;
	bool			loaded;
	bool			defaulted;
	bool			mutableImage;
} classicInteractionDomainTexture_t;

typedef struct classicInteractionDomainShadowAlphaStage_s {
	int			casterIndex;
	int			stageIndex;
	int			alphaTestMode;
	float			alphaTestValue;
	float			alphaScale;
	float			alphaHashMode;
	float			textureMatrix[ 2 ][ 4 ];
	std::uint64_t	textureResourceId;
	std::uint64_t	hash;
} classicInteractionDomainShadowAlphaStage_t;

typedef struct classicInteractionDomainShadowCaster_s {
	// Identity/geometry bridges only after publication.
	const drawSurf_t		*legacyDrawSurf;
	const viewLight_t	*legacyViewLight;
	const srfTriangles_t	*legacyCasterGeometry;
	int			drawPacketIndex;
	int			lightIndex;
	int			sourceOrdinal;
	int			chainOrdinal;
	int			geometryRecordIndex;
	int			instanceRecordIndex;
	int			materialTableRecordIndex;
	int			materialId;
	std::uint32_t		tableGeneration;
	classicInteractionDomainShadowChain_t chain;
	classicInteractionDomainShadowDisposition_t disposition;
	classicInteractionDomainShadowIndexSelection_t indexSelection;
	rendererCullMode_t	cull;
	int			materialCoverage;
	int			firstAlphaStage;
	int			alphaStageCount;
	int			vertexCount;
	int			totalIndexCount;
	int			selectedIndexCount;
	int			scissorX1;
	int			scissorY1;
	int			scissorX2;
	int			scissorY2;
	float			depthMin;
	float			depthMax;
	float			localLightOrigin[ 4 ];
	float			modelMatrix[ 16 ];
	float			modelViewMatrix[ 16 ];
	float			boundsMin[ 3 ];
	float			boundsMax[ 3 ];
	bool			external;
	bool			preload;
	bool			ambientGeometry;
	// Topology classification sealed from the actual caster geometry.
	bool			perfectHull;
	bool			dynamicCaster;
	bool			translucentCaster;
	classicDeformRole_t	deformRole;
	classicDeformOutcome_t	deformOutcome;
	std::uint64_t		deformContractHash;
	std::uint64_t		hash;
} classicInteractionDomainShadowCaster_t;

// The shared classifier and fitter build projected state exactly once. Both
// backends consume these sealed clip, crop, split, filtering and bias values.
typedef struct classicInteractionDomainShadowProjectedState_s {
	shadowMapProjectedLightState_t state;
	shadowMapProjectedFilterSettings_t filter;
	float		constantBias;
	float		normalBias;
	float		normalOffsetScale;
	float		cascadeBlend;
	float		texelBiasScale;
	bool		depthCompare;
	bool		receiverPlaneBias;
} classicInteractionDomainShadowProjectedState_t;

typedef struct classicInteractionDomainShadowPointState_s {
	bool		valid;
	int		faceCount;
	int		faceSize;
	float		lightOrigin[ 4 ];
	float		farDistance;
	float		constantBias;
	float		normalBias;
	float		normalOffsetScale;
	float		texelBiasScale;
	float		filterRadius;
	int		filterTaps;
	int		filterMode;
	bool		depthCompare;
	bool		highPrecision;
} classicInteractionDomainShadowPointState_t;

// A semantic resource plan is immutable and backend-neutral. resourcePlanId
// is a frame identity rather than an API texture handle: GL and Vulkan reserve
// their own physical image only after proving that it implements this record.
typedef struct classicInteractionDomainShadowMapPass_s {
	const viewLight_t	*legacyViewLight;
	int			lightIndex;
	classicInteractionDomainReceiver_t receiver;
	classicInteractionDomainReceiver_t resourceOwner;
	classicInteractionDomainShadowMapPassDisposition_t disposition;
	classicInteractionDomainShadowMode_t mode;
	shadowMapLightClass_t	lightClass;
	int			receiverMask;
	int			mappedCasterCount;
	int			supplementCasterCount;
	int			drawableMappedCasters;
	int			noopMappedCasters;
	int			drawableSupplementCasters;
	int			noopSupplementCasters;
	int			casterSignature;
	int			incompleteMapMask;
	int			incompleteStencilMask;
	int			hybridIncompleteMask;
	int			prelightMapMissingMask;
	int			prelightStencilRequiredMask;
	int			prelightStencilReadyMask;
	std::uint64_t		resourcePlanId;
	std::uint32_t		resourceGeneration;
	bool			resourceAlias;
	bool			mapRequired;
	bool			mapComplete;
	bool			stencilComplete;
	bool			hybridComplete;
	bool			hasStaticCasters;
	bool			hasDynamicCasters;
	bool			hasAlphaCasters;
	bool			hasTranslucentCasters;
	bool			allowCacheReuse;
	// The pass may take its static chains from a resident static-only entry
	// and draw its live chains over a copy, as the classic walker composes.
	// Only a backend that reproduces that composition consumes it (Vulkan);
	// OpenGL's sealed executor redraws the whole plan instead.
	bool			allowCacheCompose;
	bool			allowCacheUpdate;
	bool			allowScratch;
	bool			hashedAlpha;
	bool			stableAlphaHash;
	int			casterCullMode;
	bool			casterWindingMirrored;
	float			polygonFactor;
	float			polygonOffset;
	classicInteractionDomainShadowProjectedState_t projected;
	classicInteractionDomainShadowPointState_t point;
	std::uint64_t		hash;
} classicInteractionDomainShadowMapPass_t;

typedef struct classicInteractionDomainPrimitive_s {
	// Geometry/identity bridges only.  Backends must not follow these to stages
	// or register arrays.
	const drawSurf_t		*legacyDrawSurf;
	const viewLight_t	*legacyViewLight;
	int			lightIndex;
	int			surfaceIndex;
	int			lightStageIndex;
	int			bumpStageIndex;
	int			diffuseStageIndex;
	int			specularStageIndex;
	classicInteractionDomainReceiver_t receiver;
	classicInteractionDomainPrimitiveDisposition_t disposition;
	classicInteractionDomainDepth_t depth;
	rendererCullMode_t	cull;
	rendererVertexColorMode_t vertexColor;
	rendererBlendState_t	blend;
	int			vertexCount;
	int			firstIndex;
	int			indexCount;
	int			vertexOffset;
	int			scissorX1;
	int			scissorY1;
	int			scissorX2;
	int			scissorY2;
	bool			ambientLight;
	bool			polygonOffsetEnabled;
	float			polygonOffsetFactor;
	float			polygonOffsetUnits;
	std::uint64_t		lightImageResourceId;
	std::uint64_t		lightFalloffImageResourceId;
	std::uint64_t		bumpImageResourceId;
	std::uint64_t		diffuseImageResourceId;
	std::uint64_t		specularImageResourceId;
	float			diffuseColor[ 4 ];
	float			specularColor[ 4 ];
	float			flatDiffuseParams[ 4 ];
	float			localLightOrigin[ 4 ];
	float			localViewOrigin[ 4 ];
	float			lightProjection[ 4 ][ 4 ];
	float			bumpMatrix[ 2 ][ 4 ];
	float			diffuseMatrix[ 2 ][ 4 ];
	float			specularMatrix[ 2 ][ 4 ];
	float			modelMatrix[ 16 ];
	float			modelViewMatrix[ 16 ];
	std::uint64_t		hash;
} classicInteractionDomainPrimitive_t;

typedef struct classicInteractionDomainSurface_s {
	const drawSurf_t		*legacyDrawSurf;
	const viewLight_t	*legacyViewLight;
	int			drawPacketIndex;
	int			lightIndex;
	int			sourceOrdinal;
	int			receiverOrdinal;
	classicInteractionDomainReceiver_t receiver;
	int			materialTableRecordIndex;
	int			materialId;
	std::uint32_t		tableGeneration;
	int			firstPrimitive;
	int			primitiveCount;
	int			drawablePrimitiveCount;
	int			noopPrimitiveCount;
	int			surfaceStageCount;
	int			activeSurfaceStageCount;
	int			inactiveSurfaceStageCount;
	int			vertexCount;
	int			firstIndex;
	int			indexCount;
	int			vertexOffset;
	int			scissorX1;
	int			scissorY1;
	int			scissorX2;
	int			scissorY2;
	classicDeformRole_t	deformRole;
	classicDeformOutcome_t	deformOutcome;
	std::uint64_t		deformContractHash;
	std::uint64_t		hash;
} classicInteractionDomainSurface_t;

typedef struct classicInteractionDomainLight_s {
	const viewLight_t	*legacyViewLight;
	int			sourceOrdinal;
	int			firstSurface;
	int			surfaceCount;
	int			firstPrimitive;
	int			primitiveCount;
	int			drawablePrimitiveCount;
	int			noopPrimitiveCount;
	int			lightStageCount;
	int			activeLightStageCount;
	int			inactiveLightStageCount;
	int			receiverSurfaceCount[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int			receiverPrimitiveCount[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int			firstShadowCaster[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	int			shadowCasterCount[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	// LOCAL and GLOBAL own physical map resources. TRANSLUCENT, when enabled,
	// samples the sealed GLOBAL pass and therefore has no third allocation.
	int			shadowMapPassIndex[ 2 ];
	classicInteractionDomainShadowMode_t receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int			shadowCasterTotal;
	int			drawableShadowCasters;
	int			noopShadowCasters;
	int			logicalVolumeDraws;
	int			preloadVolumeDraws;
	int			scissorX1;
	int			scissorY1;
	int			scissorX2;
	int			scissorY2;
	float			globalLightOrigin[ 4 ];
	float			lightRadius[ 4 ];
	float			lightProject[ 4 ][ 4 ];
	bool			pointLight;
	bool			parallel;
	bool			ambientLight;
	bool			shadowClassified;
	bool			clearStencil;
	std::uint64_t		hash;
} classicInteractionDomainLight_t;

typedef struct classicInteractionDomainBackendCoverage_s {
	std::uint64_t	ownedViewMask;
	std::uint64_t	fallbackViewMask;
	int		ownedViews;
	int		fallbackViews;
	int		ownedLights;
	int		fallbackLights;
	int		ownedSurfaces;
	int		fallbackSurfaces;
	int		ownedDrawablePrimitives;
	int		fallbackDrawablePrimitives;
	int		ownedNoopPrimitives;
	int		ownedShadowCasters;
	int		ownedNoopShadowCasters;
	int		ownedLogicalVolumeDraws;
	int		ownedPreloadVolumeDraws;
	int		ownedShadowMapPasses;
	int		ownedHybridPasses;
	int		fallbackNoopPrimitives;
	int		fallbackShadowCasters;
	int		fallbackNoopShadowCasters;
	int		fallbackLogicalVolumeDraws;
	int		fallbackPreloadVolumeDraws;
	int		fallbackShadowMapPasses;
	int		fallbackHybridPasses;
	int		coverageMismatches;
	int		duplicateReports;
	int		untrackedFallbacks;
} classicInteractionDomainBackendCoverage_t;

typedef struct classicInteractionDomainView_s {
	const viewDef_t		*viewDef;
	int			scenePacketIndex;
	int			interactionPassPacketIndex;
	int			stencilShadowPassPacketIndex;
	int			shadowMapPassPacketIndex;
	std::uint32_t		tableGeneration;
	int			firstLight;
	int			lightCount;
	int			firstSurface;
	int			surfaceCount;
	int			firstPrimitive;
	int			primitiveCount;
	int			drawablePrimitiveCount;
	int			noopPrimitiveCount;
	int			firstShadowCaster;
	int			shadowCasterCount;
	int			firstShadowMapPass;
	int			drawableShadowCasterCount;
	int			noopShadowCasterCount;
	int			logicalVolumeDrawCount;
	int			preloadVolumeDrawCount;
	int			shadowLightCount;
	int			shadowMapPassCount;
	int			hybridShadowPassCount;
	int			projectedShadowMapPassCount;
	int			csmShadowMapPassCount;
	int			pointShadowMapPassCount;
	int			projectedShadowLightCount;
	int			pointShadowLightCount;
	int			packetDrawCount;
	int			shadowPacketDrawCount;
	int			activeLightStageCount;
	int			inactiveLightStageCount;
	int			activeSurfaceStageCount;
	int			inactiveSurfaceStageCount;
	int			materialDeformReceiverCount;
	int			completedDeformCasterCount;
	int			emptyDeformCasterCount;
	int			receiverSurfaceCount[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int			receiverPrimitiveCount[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int			viewportX1;
	int			viewportY1;
	int			viewportX2;
	int			viewportY2;
	int			scissorX1;
	int			scissorY1;
	int			scissorX2;
	int			scissorY2;
	float			projectionMatrix[ 16 ];
	float			maxLightValue;
	float			lightScale;
	float			overBright;
	float			shadowPolygonFactor;
	float			shadowPolygonUnits;
	int			stencilReference;
	classicInteractionDomainShadowMode_t shadowMode;
	bool			useScissor;
	bool			useShadowVertexProgram;
	bool			preferTwoSidedStencil;
	bool			useDepthBounds;
	bool			stencilTranslucentShadows;
	bool			ready;
	classicInteractionDomainFailure_t failure;
	int			failureDetail;
	int			failurePassPacketIndex;
	int			failureDrawPacketIndex;
	int			failureLightOrdinal;
	int			failureReceiverOrdinal;
	int			failureStageIndex;
	std::uint64_t		hash;
	classicInteractionDomainBackendOutcome_t backendOutcome[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	classicInteractionDomainFailure_t backendFailure[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendFailureDetail[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendDrawnPrimitives[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendNoopPrimitives[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendShadowCasters[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendNoopShadowCasters[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendLogicalVolumeDraws[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendPreloadVolumeDraws[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendShadowMapPasses[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	int			backendHybridPasses[ CLASSIC_INTERACTION_BACKEND_COUNT ];
} classicInteractionDomainView_t;

typedef struct classicInteractionDomainStats_s {
	bool		prepared;
	bool		frameValid;
	bool		overflow;
	int		sourceScenes;
	int		interactionViews;
	int		readyViews;
	int		fallbackViews;
	int		lights;
	int		surfaces;
	int		primitives;
	int		drawablePrimitives;
	int		noopPrimitives;
	int		shadowCasters;
	int		drawableShadowCasters;
	int		noopShadowCasters;
	int		logicalVolumeDraws;
	int		preloadVolumeDraws;
	int		shadowAlphaStages;
	int		shadowLights;
	int		shadowMapPasses;
	int		hybridShadowPasses;
	int		projectedShadowMapPasses;
	int		csmShadowMapPasses;
	int		pointShadowMapPasses;
	int		projectedShadowLights;
	int		pointShadowLights;
	int		textures;
	int		activeLightStages;
	int		inactiveLightStages;
	int		activeSurfaceStages;
	int		inactiveSurfaceStages;
	int		materialDeformReceivers;
	int		completedDeformCasters;
	int		emptyDeformCasters;
	int		receiverSurfaces[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int		receiverPrimitives[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int		failureCounts[ CLASSIC_INTERACTION_FAILURE_COUNT ];
	std::uint64_t	hash;
	classicInteractionDomainBackendCoverage_t backend[ CLASSIC_INTERACTION_BACKEND_COUNT ];
	char		status[ 96 ];
} classicInteractionDomainStats_t;

void R_ClassicInteractionDomain_ResetFrame( void );
void R_ClassicInteractionDomain_PrepareFrame( const idScenePacketFrame &packetFrame );
const classicInteractionDomainStats_t &R_ClassicInteractionDomain_Stats( void );
int R_ClassicInteractionDomain_NumViews( void );
const classicInteractionDomainView_t *R_ClassicInteractionDomain_ViewByIndex( int index );
const classicInteractionDomainView_t *R_ClassicInteractionDomain_ViewForScenePacket( int scenePacketIndex );
const classicInteractionDomainView_t *R_ClassicInteractionDomain_FindView( const viewDef_t *viewDef );
const classicInteractionDomainLight_t *R_ClassicInteractionDomain_ViewLight(
	const classicInteractionDomainView_t &view, int lightIndex );
const classicInteractionDomainSurface_t *R_ClassicInteractionDomain_ViewSurface(
	const classicInteractionDomainView_t &view, int surfaceIndex );
const classicInteractionDomainPrimitive_t *R_ClassicInteractionDomain_ViewPrimitive(
	const classicInteractionDomainView_t &view, int primitiveIndex );
const classicInteractionDomainShadowCaster_t *R_ClassicInteractionDomain_ViewShadowCaster(
	const classicInteractionDomainView_t &view, int casterIndex );
const classicInteractionDomainShadowCaster_t *R_ClassicInteractionDomain_LightShadowCaster(
	const classicInteractionDomainLight_t &light,
	classicInteractionDomainShadowChain_t chain, int casterIndex );
bool R_ClassicInteractionDomain_ShadowCasterNoopValid(
	const classicInteractionDomainShadowCaster_t &caster );
const classicInteractionDomainShadowAlphaStage_t *R_ClassicInteractionDomain_ShadowAlphaStage(
	const classicInteractionDomainShadowCaster_t &caster, int stageIndex );
const classicInteractionDomainShadowMapPass_t *R_ClassicInteractionDomain_LightShadowMapPass(
	const classicInteractionDomainLight_t &light,
	classicInteractionDomainReceiver_t receiver );
const classicInteractionDomainSurface_t *R_ClassicInteractionDomain_LightSurface(
	const classicInteractionDomainLight_t &light, int surfaceIndex );
const classicInteractionDomainPrimitive_t *R_ClassicInteractionDomain_SurfacePrimitive(
	const classicInteractionDomainSurface_t &surface, int primitiveIndex );
const classicInteractionDomainTexture_t *R_ClassicInteractionDomain_ResolveTexture(
	std::uint64_t textureResourceId );
bool R_ClassicInteractionDomain_RecordOwned( const viewDef_t *viewDef,
	classicInteractionDomainBackend_t backend, int drawnPrimitives,
	int noopPrimitives, int submittedShadowCasters = 0,
	int noopShadowCasters = 0, int logicalVolumeDraws = 0,
	int preloadVolumeDraws = 0, int shadowMapPasses = 0,
	int hybridPasses = 0 );
void R_ClassicInteractionDomain_RecordBackendFallback( const viewDef_t *viewDef,
	classicInteractionDomainBackend_t backend,
	classicInteractionDomainFailure_t failure, int detail );
const classicInteractionDomainBackendCoverage_t &R_ClassicInteractionDomain_BackendCoverage(
	classicInteractionDomainBackend_t backend );
const char *ClassicInteractionDomainReceiver_Name( classicInteractionDomainReceiver_t receiver );
const char *ClassicInteractionDomainPrimitiveDisposition_Name(
	classicInteractionDomainPrimitiveDisposition_t disposition );
const char *ClassicInteractionDomainDepth_Name( classicInteractionDomainDepth_t depth );
const char *ClassicInteractionDomainShadowMode_Name(
	classicInteractionDomainShadowMode_t mode );
const char *ClassicInteractionDomainShadowChain_Name(
	classicInteractionDomainShadowChain_t chain );
const char *ClassicInteractionDomainFailure_Name( classicInteractionDomainFailure_t failure );
const char *ClassicInteractionDomainBackend_Name( classicInteractionDomainBackend_t backend );
bool RendererClassicInteractionDomain_RunSelfTest( void );

#endif /* !__CLASSIC_INTERACTION_DOMAIN_H__ */
