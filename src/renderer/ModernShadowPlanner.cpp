// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernShadowPlanner.h"
#include "RendererBenchmarks.h"
#include "RendererMetrics.h"
#include "ShadowMapClassification.h"
#include "ShadowMapProjected.h"
#include "ShadowMapArb2Parity.h"

const int MODERN_SHADOW_PLAN_MAX_LIGHTS = 1024;
const int MODERN_SHADOW_PLAN_MIN_SIZE = 128;
const int MODERN_SHADOW_PLAN_MAX_SIZE = 4096;
const int MODERN_SHADOW_FAIRNESS_HISTORY_SLOTS = 2048;
const int MODERN_SHADOW_FAIRNESS_MAX_AGE = 16;
const int MODERN_SHADOW_FAIRNESS_BOOST_PER_MISS = 8192;
const int MODERN_SHADOW_FAIRNESS_STALE_EPOCHS = 240;

enum modernShadowBudgetClass_t {
	MODERN_SHADOW_BUDGET_SINGLE_PROJECTED = 0,
	MODERN_SHADOW_BUDGET_CASCADE,
	MODERN_SHADOW_BUDGET_POINT,
	MODERN_SHADOW_BUDGET_COUNT
};

enum modernShadowBudgetThrottleReason_t {
	MODERN_SHADOW_THROTTLE_NONE = 0,
	MODERN_SHADOW_THROTTLE_CLASS_LIGHT = 1u << 0,
	MODERN_SHADOW_THROTTLE_CLASS_TILE = 1u << 1,
	MODERN_SHADOW_THROTTLE_CLASS_PIXEL = 1u << 2,
	MODERN_SHADOW_THROTTLE_GLOBAL_LIGHT = 1u << 3,
	MODERN_SHADOW_THROTTLE_GLOBAL_TILE = 1u << 4,
	MODERN_SHADOW_THROTTLE_GLOBAL_PIXEL = 1u << 5
};

typedef struct modernShadowPlannerBudget_s {
	int					maxMappedLights;
	int					maxAtlasTiles;
	int					budgetedPixels;
	int					lightQuota[MODERN_SHADOW_BUDGET_COUNT];
	int					tileQuota[MODERN_SHADOW_BUDGET_COUNT];
} modernShadowPlannerBudget_t;

typedef struct modernShadowPlannerBudgetUse_s {
	int					mappedLights[MODERN_SHADOW_BUDGET_COUNT];
	int					atlasTiles[MODERN_SHADOW_BUDGET_COUNT];
	int					pixels[MODERN_SHADOW_BUDGET_COUNT];
} modernShadowPlannerBudgetUse_t;

typedef struct modernShadowPlannerFairnessHistory_s {
	bool				valid;
	const idRenderLightLocal *lightDef;
	int					lightDefIndex;
	int					budgetMissAge;
	int					totalBudgetMisses;
	int					totalMappedFrames;
	unsigned int		throttleReasonMask;
	unsigned int		lastThrottleReasonMask;
	int					lastBudgetClass;
	int					lastPolicy;
	int					lastFallbackReason;
	int					lastSeenEpoch;
	int					lastMappedEpoch;
	int					lastBudgetMissEpoch;
} modernShadowPlannerFairnessHistory_t;

static renderBackendCaps_t rg_modernShadowPlannerCaps;
static renderFeatureSet_t rg_modernShadowPlannerFeatures;
static modernShadowPlannerStats_t rg_modernShadowPlannerStats;
static idList<modernShadowLightDescriptor_t> rg_modernShadowPlannerDescriptors;
// Index of descriptor slots by viewLight pointer, rebuilt in lockstep with the
// list each PrepareFrame so per-light lookups from the hot draw paths are O(1)
// instead of a linear scan.
static idHashIndex rg_modernShadowPlannerDescriptorHash;

static ID_INLINE int R_ModernShadowPlanner_DescriptorKey( const viewLight_t *viewLight ) {
	return ( int )( ( ( uintptr_t )viewLight ) >> 4 );
}
static modernShadowPlannerFairnessHistory_t rg_modernShadowPlannerFairnessHistory[MODERN_SHADOW_FAIRNESS_HISTORY_SLOTS];
static int rg_modernShadowPlannerFairnessEpoch = 0;
static int rg_modernShadowPlannerFairnessHighWater = 0;	// count of ever-allocated (valid) fairness slots; valid slots always form the prefix [0, highWater)
static bool rg_modernShadowPlannerInitialized = false;

static void R_ModernShadowPlanner_SetStatus( modernShadowPlannerStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status != NULL ? status : "unknown", sizeof( stats.status ) );
}

static void R_ModernShadowPlanner_ResetFairnessHistory( void ) {
	memset( rg_modernShadowPlannerFairnessHistory, 0, sizeof( rg_modernShadowPlannerFairnessHistory ) );
	rg_modernShadowPlannerFairnessEpoch = 0;
	rg_modernShadowPlannerFairnessHighWater = 0;
}

static const idRenderLightLocal *R_ModernShadowPlanner_FairnessLightDef( const modernShadowLightDescriptor_t &descriptor ) {
	return descriptor.viewLight != NULL ? descriptor.viewLight->lightDef : NULL;
}

static bool R_ModernShadowPlanner_FairnessHistoryMatches( const modernShadowPlannerFairnessHistory_t &history, const modernShadowLightDescriptor_t &descriptor ) {
	if ( !history.valid ) {
		return false;
	}
	const idRenderLightLocal *lightDef = R_ModernShadowPlanner_FairnessLightDef( descriptor );
	if ( lightDef != NULL && history.lightDef != NULL ) {
		return history.lightDef == lightDef;
	}
	return descriptor.lightDefIndex >= 0 && history.lightDefIndex == descriptor.lightDefIndex;
}

static modernShadowPlannerFairnessHistory_t *R_ModernShadowPlanner_FindFairnessHistory( const modernShadowLightDescriptor_t &descriptor, const bool allocate ) {
	if ( descriptor.lightDefIndex < 0 && R_ModernShadowPlanner_FairnessLightDef( descriptor ) == NULL ) {
		return NULL;
	}

	// Valid slots always form the prefix [0, highWater): slots become valid only
	// via allocation, which reuses a stale slot inside the prefix or claims the
	// slot at the prefix end, and only ResetFairnessHistory clears slots. The
	// scan is therefore bounded by the high-water mark and stays byte-for-byte
	// equivalent to a full scan over all MODERN_SHADOW_FAIRNESS_HISTORY_SLOTS.
	const int scanLimit = Min( rg_modernShadowPlannerFairnessHighWater, MODERN_SHADOW_FAIRNESS_HISTORY_SLOTS );
	modernShadowPlannerFairnessHistory_t *oldest = &rg_modernShadowPlannerFairnessHistory[0];
	bool oldestFromFreeSlot = false;
	for ( int historyIndex = 0; historyIndex < scanLimit; ++historyIndex ) {
		modernShadowPlannerFairnessHistory_t &history = rg_modernShadowPlannerFairnessHistory[historyIndex];
		if ( R_ModernShadowPlanner_FairnessHistoryMatches( history, descriptor ) ) {
			return &history;
		}
		if ( allocate && ( !history.valid || rg_modernShadowPlannerFairnessEpoch - history.lastSeenEpoch > MODERN_SHADOW_FAIRNESS_STALE_EPOCHS ) ) {
			oldest = &history;
			oldestFromFreeSlot = true;
			break;
		}
		if ( history.lastSeenEpoch < oldest->lastSeenEpoch ) {
			oldest = &history;
		}
	}

	if ( !allocate ) {
		return NULL;
	}

	if ( !oldestFromFreeSlot && scanLimit < MODERN_SHADOW_FAIRNESS_HISTORY_SLOTS ) {
		// the first invalid slot sits exactly at the prefix end; this is where the
		// original full scan's first-free-slot break would have landed
		oldest = &rg_modernShadowPlannerFairnessHistory[scanLimit];
	}
	memset( oldest, 0, sizeof( *oldest ) );
	oldest->valid = true;
	oldest->lightDef = R_ModernShadowPlanner_FairnessLightDef( descriptor );
	oldest->lightDefIndex = descriptor.lightDefIndex;
	oldest->lastBudgetClass = -1;
	oldest->lastPolicy = -1;
	oldest->lastFallbackReason = -1;
	oldest->lastSeenEpoch = rg_modernShadowPlannerFairnessEpoch;
	const int oldestIndex = static_cast<int>( oldest - rg_modernShadowPlannerFairnessHistory );
	if ( oldestIndex >= rg_modernShadowPlannerFairnessHighWater ) {
		rg_modernShadowPlannerFairnessHighWater = oldestIndex + 1;
	}
	return oldest;
}

static const modernShadowPlannerFairnessHistory_t *R_ModernShadowPlanner_FindFairnessHistoryConst( const modernShadowLightDescriptor_t &descriptor ) {
	return R_ModernShadowPlanner_FindFairnessHistory( descriptor, false );
}

static void R_ModernShadowPlanner_ClearDescriptorThrottleHistory( modernShadowLightDescriptor_t &descriptor ) {
	descriptor.throttleHistoryMissStreak = 0;
	descriptor.throttleHistoryTotalMisses = 0;
	descriptor.throttleHistoryLastMissAge = -1;
	descriptor.throttleHistoryLastMappedAge = -1;
	descriptor.throttleHistoryReasonMask = MODERN_SHADOW_THROTTLE_NONE;
	descriptor.throttleHistoryLastReasonMask = MODERN_SHADOW_THROTTLE_NONE;
	descriptor.throttleHistoryLastBudgetClass = -1;
	descriptor.throttleHistoryLastPolicy = -1;
	descriptor.throttleHistoryLastFallbackReason = -1;
}

static void R_ModernShadowPlanner_CopyThrottleHistoryToDescriptor( modernShadowLightDescriptor_t &descriptor, const modernShadowPlannerFairnessHistory_t *history ) {
	R_ModernShadowPlanner_ClearDescriptorThrottleHistory( descriptor );
	if ( history == NULL ) {
		return;
	}

	descriptor.throttleHistoryMissStreak = history->budgetMissAge;
	descriptor.throttleHistoryTotalMisses = history->totalBudgetMisses;
	descriptor.throttleHistoryReasonMask = history->throttleReasonMask;
	descriptor.throttleHistoryLastReasonMask = history->lastThrottleReasonMask;
	descriptor.throttleHistoryLastBudgetClass = history->lastBudgetClass;
	descriptor.throttleHistoryLastPolicy = history->lastPolicy;
	descriptor.throttleHistoryLastFallbackReason = history->lastFallbackReason;
	if ( history->lastBudgetMissEpoch > 0 ) {
		descriptor.throttleHistoryLastMissAge = Max( 0, rg_modernShadowPlannerFairnessEpoch - history->lastBudgetMissEpoch );
	}
	if ( history->lastMappedEpoch > 0 ) {
		descriptor.throttleHistoryLastMappedAge = Max( 0, rg_modernShadowPlannerFairnessEpoch - history->lastMappedEpoch );
	}
}

const char *ModernShadowMapType_Name( modernShadowMapType_t type ) {
	switch ( type ) {
	case MODERN_SHADOW_MAP_PROJECTED:
		return "projected";
	case MODERN_SHADOW_MAP_POINT:
		return "point";
	case MODERN_SHADOW_MAP_CASCADE:
		return "cascade";
	case MODERN_SHADOW_MAP_NONE:
	default:
		return "none";
	}
}

const char *ModernShadowPolicy_Name( modernShadowPolicy_t policy ) {
	switch ( policy ) {
	case MODERN_SHADOW_POLICY_MAPPED:
		return "mapped";
	case MODERN_SHADOW_POLICY_CACHE_REUSE:
		return "cache-reuse";
	case MODERN_SHADOW_POLICY_STENCIL_FALLBACK:
		return "stencil-fallback";
	case MODERN_SHADOW_POLICY_SKIPPED:
		return "skipped";
	case MODERN_SHADOW_POLICY_NONE:
	default:
		return "none";
	}
}

const char *ModernShadowFallbackReason_Name( modernShadowFallbackReason_t reason ) {
	switch ( reason ) {
	case MODERN_SHADOW_FALLBACK_NONE:
		return "none";
	case MODERN_SHADOW_FALLBACK_SHADOW_MAP_DISABLED:
		return "shadow-map-disabled";
	case MODERN_SHADOW_FALLBACK_SHADOWS_DISABLED:
		return "shadows-disabled";
	case MODERN_SHADOW_FALLBACK_NULL_LIGHT:
		return "null-light";
	case MODERN_SHADOW_FALLBACK_NO_SHADOWS_FLAG:
		return "noShadows-flag";
	case MODERN_SHADOW_FALLBACK_NO_DYNAMIC_SHADOWS_FLAG:
		return "noDynamicShadows-flag";
	case MODERN_SHADOW_FALLBACK_AMBIENT_LIGHT:
		return "ambient-light";
	case MODERN_SHADOW_FALLBACK_LIGHT_SHADER_NO_SHADOWS:
		return "lightShader-noShadows";
	case MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT:
		return "texture-limit";
	case MODERN_SHADOW_FALLBACK_NO_RECEIVERS:
		return "no-receivers";
	case MODERN_SHADOW_FALLBACK_CUBEMAP_UNAVAILABLE:
		return "cubemap-unavailable";
	case MODERN_SHADOW_FALLBACK_BUDGET:
		return "budget";
	case MODERN_SHADOW_FALLBACK_RESOURCE_UNAVAILABLE:
		return "resource-unavailable";
	case MODERN_SHADOW_FALLBACK_RECEIVER_SAMPLING_UNAVAILABLE:
		return "receiver-sampling-unavailable";
	case MODERN_SHADOW_FALLBACK_DESCRIPTOR_INVARIANT:
		return "descriptor-invariant";
	default:
		return "unknown";
	}
}

static int R_ModernShadowPlanner_CountDrawSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( const drawSurf_t *cursor = surf; cursor != NULL; cursor = cursor->nextOnLight ) {
		count++;
	}
	return count;
}

static bool R_ModernShadowPlanner_LightHasReceivers( const viewLight_t *vLight ) {
	return vLight != NULL
		&& ( vLight->localInteractions != NULL
			|| vLight->globalInteractions != NULL
			|| vLight->translucentInteractions != NULL );
}

static bool R_ModernShadowPlanner_LightHasStencilFallback( const modernShadowLightDescriptor_t &descriptor ) {
	return descriptor.localCasterCount > 0 || descriptor.globalCasterCount > 0;
}

static bool R_ModernShadowPlanner_LightMayReuseCachedMap( const modernShadowLightDescriptor_t &descriptor ) {
	if ( descriptor.arb2CacheReuseAvailable ) {
		return true;
	}
	const viewLight_t *vLight = descriptor.viewLight;
	if ( !r_shadowMapStaticCache.GetBool() || vLight == NULL || descriptor.lightDefIndex < 0 ) {
		return false;
	}
	if ( descriptor.translucentCasterCount > 0 || vLight->shadowMapTranslucentCasterCount > 0 || vLight->shadowMapDynamicCasterCount > 0 || vLight->shadowMapCasterCount <= 0 ) {
		return false;
	}
	if ( !descriptor.pointLight && descriptor.cascadeCount > 1 && !r_shadowMapCacheCSM.GetBool() ) {
		return false;
	}
	if ( descriptor.pointLight ) {
		return r_shadowMapPointCacheSize.GetInteger() > 0;
	}
	return r_shadowMapProjectedCacheSize.GetInteger() > 0;
}

static void R_ModernShadowPlanner_ApplyArb2CacheEstimate( modernShadowLightDescriptor_t &descriptor, const viewLight_t *vLight, const viewDef_t *viewDef ) {
	shadowMapArb2CacheEstimate_t estimate;
	if ( !RB_ShadowMapEstimateArb2CacheOwnership( vLight, viewDef, estimate ) ) {
		return;
	}

	descriptor.arb2CacheEstimateValid = estimate.valid;
	descriptor.arb2ShadowPasses = estimate.shadowPasses;
	descriptor.arb2CacheablePasses = estimate.cacheablePasses;
	descriptor.arb2CacheHitPasses = estimate.cacheHitPasses;
	descriptor.arb2CacheMissPasses = estimate.cacheMissPasses;
	descriptor.arb2FreshUpdatePasses = estimate.freshUpdatePasses;
	descriptor.arb2BudgetFallbackPasses = estimate.budgetFallbackPasses;
	descriptor.arb2StencilOnlyPasses = estimate.stencilOnlyPasses;
	descriptor.arb2ReceiverFallbackPasses = estimate.receiverFallbackPasses;
	descriptor.arb2UnshadowedPasses = estimate.unshadowedPasses;
	descriptor.arb2CacheablePassMask = estimate.cacheablePassMask;
	descriptor.arb2CacheHitPassMask = estimate.cacheHitPassMask;
	descriptor.arb2CacheKeyHitMask = estimate.cacheKeyHitMask;
	descriptor.arb2CacheMissPassMask = estimate.cacheMissPassMask;
	descriptor.arb2FreshUpdatePassMask = estimate.freshUpdatePassMask;
	descriptor.arb2BudgetFallbackPassMask = estimate.budgetFallbackPassMask;
	descriptor.arb2CacheReuseAvailable = estimate.cacheHitPasses > 0;
	descriptor.arb2CacheFullyReusable =
		estimate.cacheHitPasses > 0 &&
		estimate.freshUpdatePasses == 0 &&
		estimate.budgetFallbackPasses == 0 &&
		estimate.cacheMissPasses == 0;
}

// Modern clustered lighting owns one shadow resource per light, not separate
// LOCAL/GLOBAL receiver passes and not a stencil-composited hybrid. A cached
// GLOBAL map is therefore consumable only when it represents every active
// receiver exactly and contains every parity-relevant caster by itself.
static bool R_ModernShadowPlanner_Arb2SingleResourceComplete(
		const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return false;
	}
	unsigned int activeReceiverMask = 0;
	if ( vLight->localInteractions != NULL ) {
		activeReceiverMask |= SHADOWMAP_RECEIVER_MASK_LOCAL;
	}
	if ( vLight->globalInteractions != NULL
			|| vLight->translucentInteractions != NULL ) {
		activeReceiverMask |= SHADOWMAP_RECEIVER_MASK_GLOBAL;
	}
	const unsigned int incompleteMapMask = static_cast<unsigned int>(
		vLight->shadowMapIncompleteMapMask
		| vLight->shadowMapHybridIncompleteMask
		| vLight->shadowMapPrelightMapMissingMask );
	if ( ( incompleteMapMask & activeReceiverMask ) != 0 ) {
		return false;
	}
	if ( ( activeReceiverMask & SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0
			&& ( vLight->localShadowMapCasters != NULL
				|| vLight->localShadowMapDynamicCasters != NULL
				|| vLight->localTranslucentShadowMapCasters != NULL ) ) {
		return false;
	}
	// Supplement chains should imply an incomplete-map bit. Keep this
	// independent check fail-closed if frontend bookkeeping ever diverges.
	if ( ( activeReceiverMask & SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0
			&& vLight->globalShadowMapStencilSupplements != NULL ) {
		return false;
	}
	if ( ( activeReceiverMask & SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0
			&& ( vLight->globalShadowMapStencilSupplements != NULL
				|| vLight->localShadowMapStencilSupplements != NULL ) ) {
		return false;
	}
	return activeReceiverMask != 0;
}

// Resolves the physical ARB2 resource which a modern receiver can sample.
// Projected lights reference a persistent-atlas cell; point lights reference
// the one exact cache cube currently selected by classic GL.  Both require a
// current GLOBAL-pass signature and complete static content.
static void R_ModernShadowPlanner_ResolveArb2AtlasSlot( modernShadowLightDescriptor_t &descriptor, const viewLight_t *vLight, const viewDef_t *viewDef ) {
	descriptor.arb2AtlasSlotReady = false;
	descriptor.arb2AtlasCellX = -1;
	descriptor.arb2AtlasCellY = -1;
	descriptor.arb2AtlasCellSpan = 0;
	descriptor.arb2AtlasSignature = 0;
	descriptor.arb2AtlasStorageGeneration = 0;
	descriptor.arb2AtlasContentFrame = -1;
	descriptor.arb2PointCubeReady = false;
	descriptor.arb2PointCubeSignature = 0;
	descriptor.arb2PointCubeContentFrame = -1;
	memset( descriptor.arb2AtlasCascadeRect, 0, sizeof( descriptor.arb2AtlasCascadeRect ) );
	if ( vLight == NULL || descriptor.lightDefIndex < 0 ) {
		return;
	}
	const bool contentCurrent = descriptor.arb2CacheEstimateValid
		&& ( descriptor.arb2CacheKeyHitMask & SHADOWMAP_ARB2_CACHE_PASS_GLOBAL ) != 0;
	const bool staticContentComplete = vLight->shadowMapDynamicCasterCount == 0;
	if ( !contentCurrent || !staticContentComplete
			|| !R_ModernShadowPlanner_Arb2SingleResourceComplete( vLight ) ) {
		return;
	}
	if ( descriptor.pointLight ) {
		shadowMapArb2PointCube_t cube;
		if ( !RB_ShadowMapPointCubeForLight( vLight, viewDef, cube )
				|| !cube.valid || cube.size != descriptor.resolution
				|| cube.lastUpdatedFrame < 0 || cube.lastUpdatedFrame > tr.frameCount ) {
			return;
		}
		descriptor.arb2PointCubeSignature = cube.signature;
		descriptor.arb2PointCubeContentFrame = cube.lastUpdatedFrame;
		descriptor.arb2PointCubeReady = true;
		return;
	}
	shadowMapArb2AtlasSlot_t slot;
	if ( !RB_ShadowMapProjectedAtlasSlotForLight( vLight, viewDef, slot ) ) {
		return;
	}
	if ( slot.cascadeCount != Max( 1, descriptor.cascadeCount ) ) {
		return;
	}
	descriptor.arb2AtlasCellX = slot.cellX;
	descriptor.arb2AtlasCellY = slot.cellY;
	descriptor.arb2AtlasCellSpan = slot.cellSpan;
	descriptor.arb2AtlasSignature = slot.signature;
	descriptor.arb2AtlasStorageGeneration = slot.storageGeneration;
	descriptor.arb2AtlasContentFrame = slot.lastUpdatedFrame;
	const int cascadeLimit = Min( MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, SHADOWMAP_PROJECTED_MAX_CASCADES );
	for ( int i = 0; i < cascadeLimit; ++i ) {
		descriptor.arb2AtlasCascadeRect[i][0] = slot.cascadeAtlasRect[i][0];
		descriptor.arb2AtlasCascadeRect[i][1] = slot.cascadeAtlasRect[i][1];
		descriptor.arb2AtlasCascadeRect[i][2] = slot.cascadeAtlasRect[i][2];
		descriptor.arb2AtlasCascadeRect[i][3] = slot.cascadeAtlasRect[i][3];
	}
	descriptor.arb2AtlasSlotReady = true;
	// LRU pinning happens at the consumption point (clustered-lighting light
	// record build), not here: at init time the light's final policy is
	// unknown, and pinning entries for budget-missed or invariant-demoted
	// lights would distort ARB2's cache behavior.
}

static bool R_ModernShadowPlanner_CanIsolateArb2CacheOwnership( const modernShadowLightDescriptor_t &descriptor ) {
	// With modern receiver sampling live, cache reuse stays budget-exempt
	// only when the receivers can actually consume the cached tiles - the
	// persistent projected-atlas slot or exact point cube makes that possible;
	// without it the light must compete for an update instead of silently
	// sampling another light's resource.
	const bool physicalResourceReady = descriptor.pointLight
		? descriptor.arb2PointCubeReady : descriptor.arb2AtlasSlotReady;
	if ( descriptor.modernReceiverSamplingReady && !physicalResourceReady ) {
		return false;
	}
	return descriptor.arb2CacheEstimateValid
		&& descriptor.arb2CacheFullyReusable
		&& descriptor.arb2CacheHitPasses > 0
		&& descriptor.arb2ShadowPasses > 0;
}

static int R_ModernShadowPlanner_ScissorArea( const idScreenRect &rect ) {
	if ( rect.IsEmpty() ) {
		return 0;
	}
	return Max( 0, rect.x2 + 1 - rect.x1 ) * Max( 0, rect.y2 + 1 - rect.y1 );
}

static int R_ModernShadowPlanner_BudgetedShadowMapSize( void ) {
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	int size = idMath::ClampInt( MODERN_SHADOW_PLAN_MIN_SIZE, MODERN_SHADOW_PLAN_MAX_SIZE, r_shadowMapSize.GetInteger() );
	size = Min( size, idMath::ClampInt( MODERN_SHADOW_PLAN_MIN_SIZE, MODERN_SHADOW_PLAN_MAX_SIZE, budget.shadowMapSize ) );
	if ( rg_modernShadowPlannerCaps.maxTextureSize > 0 ) {
		size = Min( size, rg_modernShadowPlannerCaps.maxTextureSize );
	}
	return Max( 1, size );
}

static int R_ModernShadowPlanner_ShadowTileBudget( const rendererBenchmarkBudget_t &budget ) {
	int tileBudget = Max( 8, budget.lightBatchTarget / 2 );
	if ( rg_modernShadowPlannerFeatures.lowOverhead ) {
		tileBudget = Max( tileBudget, 16 );
	} else if ( rg_modernShadowPlannerFeatures.gpuDriven ) {
		tileBudget = Max( tileBudget, 12 );
	} else if ( rg_modernShadowPlannerFeatures.modernBaseline ) {
		tileBudget = Max( tileBudget, 10 );
	}
	if ( budget.shadowMapSize >= 2048 ) {
		tileBudget = Min( tileBudget, 48 );
	}
	return idMath::ClampInt( 8, 64, tileBudget );
}

static void R_ModernShadowPlanner_StoreBudgetInStats( const modernShadowPlannerBudget_t &budget, modernShadowPlannerStats_t &stats ) {
	stats.maxMappedLights = budget.maxMappedLights;
	stats.maxAtlasTiles = budget.maxAtlasTiles;
	stats.budgetedPixels = budget.budgetedPixels;
	stats.singleProjectedLightQuota = budget.lightQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED];
	stats.cascadeLightQuota = budget.lightQuota[MODERN_SHADOW_BUDGET_CASCADE];
	stats.pointLightQuota = budget.lightQuota[MODERN_SHADOW_BUDGET_POINT];
	stats.singleProjectedTileQuota = budget.tileQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED];
	stats.cascadeTileQuota = budget.tileQuota[MODERN_SHADOW_BUDGET_CASCADE];
	stats.pointTileQuota = budget.tileQuota[MODERN_SHADOW_BUDGET_POINT];
}

static modernShadowPlannerBudget_t R_ModernShadowPlanner_BuildBudget( const rendererBenchmarkBudget_t &rendererBudget, const int shadowMapSize ) {
	modernShadowPlannerBudget_t budget;
	memset( &budget, 0, sizeof( budget ) );

	const int tileBudget = R_ModernShadowPlanner_ShadowTileBudget( rendererBudget );
	const int tilePixels = Max( 1, shadowMapSize ) * Max( 1, shadowMapSize );
	budget.maxAtlasTiles = tileBudget;
	budget.budgetedPixels = tilePixels * tileBudget;

	budget.tileQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED] = idMath::ClampInt( 2, tileBudget, Max( 2, tileBudget / 2 ) );
	budget.tileQuota[MODERN_SHADOW_BUDGET_CASCADE] = idMath::ClampInt( 3, tileBudget, Max( 3, ( tileBudget * 3 ) / 4 ) );
	budget.tileQuota[MODERN_SHADOW_BUDGET_POINT] = idMath::ClampInt( 6, tileBudget, Max( 6, tileBudget / 2 ) );

	budget.lightQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED] = Max( 1, budget.tileQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED] );
	budget.lightQuota[MODERN_SHADOW_BUDGET_CASCADE] = Max( 1, budget.tileQuota[MODERN_SHADOW_BUDGET_CASCADE] / Max( 1, MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES - 1 ) );
	budget.lightQuota[MODERN_SHADOW_BUDGET_POINT] = Max( 1, budget.tileQuota[MODERN_SHADOW_BUDGET_POINT] / 6 );
	budget.maxMappedLights =
		budget.lightQuota[MODERN_SHADOW_BUDGET_SINGLE_PROJECTED] +
		budget.lightQuota[MODERN_SHADOW_BUDGET_CASCADE] +
		budget.lightQuota[MODERN_SHADOW_BUDGET_POINT];

	return budget;
}

static bool R_ModernShadowPlanner_TranslucentMomentsAvailable( void ) {
	return r_shadowMapTranslucentMoments.GetBool()
		&& glConfig.GLSLProgramAvailable
		&& glConfig.maxTextureUnits >= 9
		&& glConfig.maxTextureImageUnits >= 9
		&& glConfig.maxDrawBuffers >= 3
		&& glConfig.maxColorAttachments >= 3;
}

static bool R_ModernShadowPlanner_ModernReceiverSamplingAvailable( void ) {
	const bool receiverPathRequested =
		r_rendererModernVisible.GetBool()
		|| r_rendererModernDeferred.GetBool()
		|| r_rendererForwardPlus.GetBool();
	return receiverPathRequested
		&& rg_modernShadowPlannerFeatures.modernBaseline
		&& rg_modernShadowPlannerCaps.hasUBO
		&& glConfig.GLSLProgramAvailable
		&& glConfig.maxTextureImageUnits >= 13;
}

static int R_ModernShadowPlanner_TotalCasterCount( const modernShadowLightDescriptor_t &descriptor ) {
	return descriptor.localCasterCount + descriptor.globalCasterCount + descriptor.translucentCasterCount;
}

static int R_ModernShadowPlanner_TotalReceiverCount( const modernShadowLightDescriptor_t &descriptor ) {
	return descriptor.localReceiverCount + descriptor.globalReceiverCount + descriptor.translucentReceiverCount;
}

static modernShadowBudgetClass_t R_ModernShadowPlanner_BudgetClassForDescriptor( const modernShadowLightDescriptor_t &descriptor ) {
	if ( descriptor.pointLight || descriptor.mapType == MODERN_SHADOW_MAP_POINT ) {
		return MODERN_SHADOW_BUDGET_POINT;
	}
	if ( descriptor.mapType == MODERN_SHADOW_MAP_CASCADE || descriptor.cascadeCount > 1 ) {
		return MODERN_SHADOW_BUDGET_CASCADE;
	}
	return MODERN_SHADOW_BUDGET_SINGLE_PROJECTED;
}

static int R_ModernShadowPlanner_ClassLightQuota( const modernShadowPlannerStats_t &stats, const modernShadowBudgetClass_t budgetClass ) {
	switch ( budgetClass ) {
	case MODERN_SHADOW_BUDGET_POINT:
		return stats.pointLightQuota;
	case MODERN_SHADOW_BUDGET_CASCADE:
		return stats.cascadeLightQuota;
	case MODERN_SHADOW_BUDGET_SINGLE_PROJECTED:
	default:
		return stats.singleProjectedLightQuota;
	}
}

static int R_ModernShadowPlanner_ClassTileQuota( const modernShadowPlannerStats_t &stats, const modernShadowBudgetClass_t budgetClass ) {
	switch ( budgetClass ) {
	case MODERN_SHADOW_BUDGET_POINT:
		return stats.pointTileQuota;
	case MODERN_SHADOW_BUDGET_CASCADE:
		return stats.cascadeTileQuota;
	case MODERN_SHADOW_BUDGET_SINGLE_PROJECTED:
	default:
		return stats.singleProjectedTileQuota;
	}
}

static void R_ModernShadowPlanner_RecordClassBudgetThrottle( modernShadowPlannerStats_t &stats, const modernShadowBudgetClass_t budgetClass ) {
	switch ( budgetClass ) {
	case MODERN_SHADOW_BUDGET_POINT:
		stats.pointBudgetThrottledLights++;
		break;
	case MODERN_SHADOW_BUDGET_CASCADE:
		stats.cascadeBudgetThrottledLights++;
		break;
	case MODERN_SHADOW_BUDGET_SINGLE_PROJECTED:
	default:
		stats.singleProjectedBudgetThrottledLights++;
		break;
	}
}

static void R_ModernShadowPlanner_RecordBudgetThrottleReason( modernShadowPlannerStats_t &stats, const unsigned int reasonMask ) {
	stats.budgetThrottleReasonMask |= reasonMask;
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_CLASS_LIGHT ) != 0 ) {
		stats.budgetThrottleClassLightLights++;
	}
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_CLASS_TILE ) != 0 ) {
		stats.budgetThrottleClassTileLights++;
	}
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_CLASS_PIXEL ) != 0 ) {
		stats.budgetThrottleClassPixelLights++;
	}
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_GLOBAL_LIGHT ) != 0 ) {
		stats.budgetThrottleGlobalLightLights++;
	}
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_GLOBAL_TILE ) != 0 ) {
		stats.budgetThrottleGlobalTileLights++;
	}
	if ( ( reasonMask & MODERN_SHADOW_THROTTLE_GLOBAL_PIXEL ) != 0 ) {
		stats.budgetThrottleGlobalPixelLights++;
	}
}

static void R_ModernShadowPlanner_RecordMappedClassStats( modernShadowPlannerStats_t &stats, const modernShadowLightDescriptor_t &descriptor ) {
	const int tiles = Max( 1, descriptor.tileCount );
	switch ( R_ModernShadowPlanner_BudgetClassForDescriptor( descriptor ) ) {
	case MODERN_SHADOW_BUDGET_POINT:
		stats.pointMappedLights++;
		stats.pointAtlasTiles += tiles;
		break;
	case MODERN_SHADOW_BUDGET_CASCADE:
		stats.cascadeMappedLights++;
		stats.cascadeAtlasTiles += tiles;
		break;
	case MODERN_SHADOW_BUDGET_SINGLE_PROJECTED:
	default:
		stats.singleProjectedMappedLights++;
		stats.singleProjectedAtlasTiles += tiles;
		break;
	}
}

static void R_ModernShadowPlanner_SetIdentityMatrix( float matrix[16] ) {
	for ( int i = 0; i < 16; ++i ) {
		matrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}
}

static void R_ModernShadowPlanner_CopyPlaneToFloats( float out[4], const idPlane &plane ) {
	out[0] = plane[0];
	out[1] = plane[1];
	out[2] = plane[2];
	out[3] = plane[3];
}

static void R_ModernShadowPlanner_CopyVec4ToFloats( float out[4], const idVec4 &vec ) {
	out[0] = vec[0];
	out[1] = vec[1];
	out[2] = vec[2];
	out[3] = vec[3];
}

static bool R_ModernShadowPlanner_FloatReady( const float value ) {
	return value == value && idMath::Fabs( value ) < 1.0e28f;
}

static bool R_ModernShadowPlanner_AtlasRectXYWHReady( const float rect[4] ) {
	const float epsilon = 0.0001f;
	return R_ModernShadowPlanner_FloatReady( rect[0] )
		&& R_ModernShadowPlanner_FloatReady( rect[1] )
		&& R_ModernShadowPlanner_FloatReady( rect[2] )
		&& R_ModernShadowPlanner_FloatReady( rect[3] )
		&& rect[0] >= -epsilon
		&& rect[1] >= -epsilon
		&& rect[2] > epsilon
		&& rect[3] > epsilon
		&& rect[0] + rect[2] <= 1.0f + epsilon
		&& rect[1] + rect[3] <= 1.0f + epsilon;
}

static bool R_ModernShadowPlanner_AtlasRectMinMaxReady( const float rect[4] ) {
	const float epsilon = 0.0001f;
	return R_ModernShadowPlanner_FloatReady( rect[0] )
		&& R_ModernShadowPlanner_FloatReady( rect[1] )
		&& R_ModernShadowPlanner_FloatReady( rect[2] )
		&& R_ModernShadowPlanner_FloatReady( rect[3] )
		&& rect[0] >= -epsilon
		&& rect[1] >= -epsilon
		&& rect[2] <= 1.0f + epsilon
		&& rect[3] <= 1.0f + epsilon
		&& rect[2] > rect[0] + epsilon
		&& rect[3] > rect[1] + epsilon;
}

static bool R_ModernShadowPlanner_ProjectedPlanesReady( const float planes[4][4] ) {
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			if ( !R_ModernShadowPlanner_FloatReady( planes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}
	return true;
}

static bool R_ModernShadowPlanner_FloatClose( const float lhs, const float rhs, const float epsilon = 0.001f ) {
	return R_ModernShadowPlanner_FloatReady( lhs ) && R_ModernShadowPlanner_FloatReady( rhs ) && idMath::Fabs( lhs - rhs ) <= epsilon;
}

static bool R_ModernShadowPlanner_ProjectBaseClipPlanesMatchLight( const modernShadowLightDescriptor_t &descriptor ) {
	if ( descriptor.viewLight == NULL ) {
		return false;
	}

	idPlane expectedBaseClipPlanes[4];
	R_ShadowMapBuildBaseClipPlanesForLight( descriptor.viewLight, expectedBaseClipPlanes );
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			if ( !R_ModernShadowPlanner_FloatClose( descriptor.projectedBaseClipPlanes[planeIndex][componentIndex], expectedBaseClipPlanes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}
	return true;
}

static bool R_ModernShadowPlanner_SingleCascadeClipPlanesMatchBase( const modernShadowLightDescriptor_t &descriptor ) {
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			if ( !R_ModernShadowPlanner_FloatClose( descriptor.projectedClipPlanes[0][planeIndex][componentIndex], descriptor.projectedBaseClipPlanes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}
	return true;
}

static bool R_ModernShadowPlanner_ProjectedSampleCountsReady( const modernShadowLightDescriptor_t &descriptor ) {
	return descriptor.projectedSampleCount > 0
		&& descriptor.projectedValidSampleCount >= 0
		&& descriptor.projectedSkippedSampleCount >= 0
		&& descriptor.projectedValidSampleCount + descriptor.projectedSkippedSampleCount == descriptor.projectedSampleCount
		&& descriptor.projectedPositiveWPoints + descriptor.projectedNegativeWPoints + descriptor.projectedNearZeroWPoints + descriptor.projectedNanWPoints == descriptor.projectedSampleCount;
}

static bool R_ModernShadowPlanner_ProjectedFallbackReasonReady( const modernShadowLightDescriptor_t &descriptor ) {
	if ( !descriptor.projectedCascadeFallback ) {
		return descriptor.projectedFallbackReason == SHADOWMAP_PROJECTED_FALLBACK_NONE
			&& descriptor.projectedMixedWSignCascades == 0
			&& descriptor.projectedNegativeWPoints == 0
			&& descriptor.projectedPositiveWPoints > 0;
	}

	if ( descriptor.projectedFallbackReason == SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS ) {
		return descriptor.projectedMixedWSignCascades > 0
			&& descriptor.projectedPositiveWPoints > 0
			&& descriptor.projectedNegativeWPoints > 0;
	}
	if ( descriptor.projectedFallbackReason == SHADOWMAP_PROJECTED_FALLBACK_INSUFFICIENT_VALID_SAMPLES ) {
		return descriptor.projectedValidSampleCount < 4;
	}
	if ( descriptor.projectedFallbackReason == SHADOWMAP_PROJECTED_FALLBACK_COLLAPSED_BOUNDS ) {
		return descriptor.projectedValidSampleCount >= 4;
	}
	return false;
}

static const char *R_ModernShadowPlanner_InvariantFlagName( const unsigned int flag ) {
	switch ( flag ) {
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS:
		return "light-class";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS:
		return "map-counts";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_TILES:
		return "atlas-tiles";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_ATLAS:
		return "projected-atlas";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS:
		return "split-depths";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE:
		return "projected-state";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_POLICY:
		return "policy";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_SLOT:
		return "atlas-slot";
	case MODERN_SHADOW_DESCRIPTOR_INVARIANT_FRESHNESS:
		return "freshness";
	default:
		return "unknown";
	}
}

static bool R_ModernShadowPlanner_CheckDescriptorInvariant( modernShadowLightDescriptor_t &descriptor, const unsigned int flag, const bool condition, const char *stage, const char *reason ) {
	if ( condition ) {
		return true;
	}

	const bool firstFailureForDescriptor = ( descriptor.invariantFailureMask & flag ) == 0;
	descriptor.invariantFailureMask |= flag;
	if ( common != NULL && firstFailureForDescriptor ) {
		common->Warning(
			"modernShadowPlan descriptor invariant failed: stage=%s light=%d descriptor=%d flag=%s reason=%s map=%s policy=%s cascades=%d requested=%d tiles=%d atlasDiv=%d",
			stage != NULL ? stage : "unknown",
			descriptor.lightDefIndex,
			descriptor.descriptorIndex,
			R_ModernShadowPlanner_InvariantFlagName( flag ),
			reason != NULL ? reason : "unknown",
			ModernShadowMapType_Name( descriptor.mapType ),
			ModernShadowPolicy_Name( descriptor.policy ),
			descriptor.cascadeCount,
			descriptor.requestedCascadeCount,
			descriptor.tileCount,
			descriptor.atlasDiv );
	}
	assert( condition );
	return false;
}

static void R_ModernShadowPlanner_ClearDescriptorContract( modernShadowLightDescriptor_t &descriptor ) {
	for ( int i = 0; i < MODERN_SHADOW_DESCRIPTOR_MAX_TILES; ++i ) {
		descriptor.tileAtlasRect[i][0] = 0.0f;
		descriptor.tileAtlasRect[i][1] = 0.0f;
		descriptor.tileAtlasRect[i][2] = 0.0f;
		descriptor.tileAtlasRect[i][3] = 0.0f;
	}
	R_ModernShadowPlanner_SetIdentityMatrix( descriptor.shadowMatrix );
	descriptor.projectionPad = 0.0f;
	descriptor.projectionScale = 1.0f;
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			descriptor.projectedBaseClipPlanes[planeIndex][componentIndex] = 0.0f;
		}
	}
	for ( int cascadeIndex = 0; cascadeIndex < MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES; ++cascadeIndex ) {
		for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
			for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
				descriptor.projectedClipPlanes[cascadeIndex][planeIndex][componentIndex] = 0.0f;
			}
		}
		descriptor.projectedAtlasRect[cascadeIndex][0] = 0.0f;
		descriptor.projectedAtlasRect[cascadeIndex][1] = 0.0f;
		descriptor.projectedAtlasRect[cascadeIndex][2] = 0.0f;
		descriptor.projectedAtlasRect[cascadeIndex][3] = 0.0f;
		descriptor.cascadeSplitDepths[cascadeIndex] = 0.0f;
		descriptor.cascadeBiasScale[cascadeIndex] = 1.0f;
		descriptor.texelDepthBias[cascadeIndex] = 0.0f;
		descriptor.worldTexelSize[cascadeIndex] = 0.0f;
		descriptor.sliceNear[cascadeIndex] = 0.0f;
		descriptor.sliceFar[cascadeIndex] = 0.0f;
		descriptor.depthRange[cascadeIndex] = 0.0f;
		descriptor.clipZExtent[cascadeIndex] = 0.0f;
	}
	descriptor.projectedFallbackCascade = -1;
	descriptor.projectedFallbackReason = SHADOWMAP_PROJECTED_FALLBACK_NONE;
	descriptor.projectedSampleCount = 0;
	descriptor.projectedValidSampleCount = 0;
	descriptor.projectedSkippedSampleCount = 0;
	descriptor.projectedPositiveWPoints = 0;
	descriptor.projectedNegativeWPoints = 0;
	descriptor.projectedNearZeroWPoints = 0;
	descriptor.projectedNanWPoints = 0;
	descriptor.projectedInvalidNdcPoints = 0;
	descriptor.projectedMixedWSignCascades = 0;
	descriptor.projectedStateReady = false;
	descriptor.projectedCascadeFallback = false;
}

static void R_ModernShadowPlanner_ApplyProjectedContract( modernShadowLightDescriptor_t &descriptor, const shadowMapProjectedLightState_t &projectedState ) {
	if ( !projectedState.valid ) {
		return;
	}

	descriptor.projectedStateReady = true;
	descriptor.projectedCascadeFallback = projectedState.cascadeFallback;
	descriptor.projectedFallbackCascade = projectedState.fallbackCascade;
	descriptor.projectedFallbackReason = projectedState.fallbackReason;
	descriptor.requestedCascadeCount = Max( 1, projectedState.requestedCascadeCount );
	descriptor.cascadeCount = idMath::ClampInt( 1, MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, projectedState.cascadeCount );
	descriptor.atlasDiv = Max( 1, projectedState.atlasDiv );
	descriptor.tileCount = Max( 1, descriptor.cascadeCount );
	descriptor.mapType = descriptor.cascadeCount > 1 ? MODERN_SHADOW_MAP_CASCADE : MODERN_SHADOW_MAP_PROJECTED;
	descriptor.projectionPad = projectedState.projectionPad;
	descriptor.projectionScale = projectedState.projectionScale;
	R_ShadowMapClipPlanesToGLMatrix( projectedState.clipPlanes[0], descriptor.shadowMatrix );
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		R_ModernShadowPlanner_CopyPlaneToFloats( descriptor.projectedBaseClipPlanes[planeIndex], projectedState.baseClipPlanes[planeIndex] );
	}

	for ( int cascadeIndex = 0; cascadeIndex < MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES; ++cascadeIndex ) {
		for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
			R_ModernShadowPlanner_CopyPlaneToFloats( descriptor.projectedClipPlanes[cascadeIndex][planeIndex], projectedState.clipPlanes[cascadeIndex][planeIndex] );
		}
		R_ModernShadowPlanner_CopyVec4ToFloats( descriptor.projectedAtlasRect[cascadeIndex], projectedState.atlasRect[cascadeIndex] );
		descriptor.cascadeSplitDepths[cascadeIndex] = projectedState.splitDepths[cascadeIndex];
		descriptor.cascadeBiasScale[cascadeIndex] = projectedState.biasScale[cascadeIndex];
		descriptor.texelDepthBias[cascadeIndex] = projectedState.texelDepthBias[cascadeIndex];
		descriptor.worldTexelSize[cascadeIndex] = projectedState.worldTexelSize[cascadeIndex];
		descriptor.sliceNear[cascadeIndex] = projectedState.sliceNear[cascadeIndex];
		descriptor.sliceFar[cascadeIndex] = projectedState.sliceFar[cascadeIndex];
		descriptor.depthRange[cascadeIndex] = projectedState.depthRange[cascadeIndex];
		descriptor.clipZExtent[cascadeIndex] = projectedState.clipZExtent[cascadeIndex];

		const shadowMapProjectedCascadeFit_t &fit = projectedState.cascadeFit[cascadeIndex];
		if ( fit.attempted ) {
			descriptor.projectedSampleCount += fit.sampleCount;
			descriptor.projectedValidSampleCount += fit.validPoints;
			descriptor.projectedSkippedSampleCount += fit.skippedPoints;
			descriptor.projectedPositiveWPoints += fit.positiveWPoints;
			descriptor.projectedNegativeWPoints += fit.negativeWPoints;
			descriptor.projectedNearZeroWPoints += fit.nearZeroWPoints;
			descriptor.projectedNanWPoints += fit.nanWPoints;
			descriptor.projectedInvalidNdcPoints += fit.invalidNdcPoints;
			descriptor.projectedMixedWSignCascades += fit.mixedWSigns ? 1 : 0;
		}
	}
}

static void R_ModernShadowPlanner_InitDescriptorContract( modernShadowLightDescriptor_t &descriptor, const viewLight_t *vLight, const viewDef_t *viewDef ) {
	R_ModernShadowPlanner_ClearDescriptorContract( descriptor );
	if ( vLight != NULL ) {
		descriptor.receiverScissor[0] = vLight->scissorRect.x1;
		descriptor.receiverScissor[1] = vLight->scissorRect.y1;
		descriptor.receiverScissor[2] = vLight->scissorRect.x2;
		descriptor.receiverScissor[3] = vLight->scissorRect.y2;
		if ( !descriptor.pointLight ) {
			shadowMapProjectedLightState_t projectedState;
			R_BuildShadowMapProjectedLightState( vLight, viewDef, descriptor.resolution, projectedState );
			R_ModernShadowPlanner_ApplyProjectedContract( descriptor, projectedState );
		}
	} else {
		descriptor.receiverScissor[0] = 0;
		descriptor.receiverScissor[1] = 0;
		descriptor.receiverScissor[2] = 0;
		descriptor.receiverScissor[3] = 0;
	}
	descriptor.faceIndex = descriptor.pointLight ? 0 : -1;
	descriptor.cascadeIndex = descriptor.mapType == MODERN_SHADOW_MAP_CASCADE ? 0 : -1;
	descriptor.depthFormat = descriptor.pointLight ? MODERN_SHADOW_DEPTH_FORMAT_PACKED_RGBA8 : MODERN_SHADOW_DEPTH_FORMAT_D24;
	descriptor.compareMode = descriptor.pointLight ? MODERN_SHADOW_COMPARE_MANUAL_PACKED_DEPTH : MODERN_SHADOW_COMPARE_MANUAL_DEPTH;
	descriptor.biasModel = descriptor.pointLight ? MODERN_SHADOW_BIAS_POINT_VECTOR : ( descriptor.mapType == MODERN_SHADOW_MAP_CASCADE ? MODERN_SHADOW_BIAS_CASCADE_SCALED : MODERN_SHADOW_BIAS_CONSTANT_NORMAL );
	const shadowMapProjectedFilterSettings_t projectedFilterSettings = R_ShadowMapProjectedFilterSettings( vLight );
	descriptor.pcfKernel = static_cast<int>( idMath::Ceil( descriptor.pointLight
		? r_shadowMapPointFilterRadius.GetFloat()
		: projectedFilterSettings.effectiveFilterRadius ) );
	descriptor.updateFrame = tr.frameCount;
	descriptor.casterCount = R_ModernShadowPlanner_TotalCasterCount( descriptor );
	descriptor.receiverCount = R_ModernShadowPlanner_TotalReceiverCount( descriptor );
	if ( descriptor.pointLight ) {
		const float pointFar = R_ShadowMapPointFarDistance( vLight );
		const shadowMapPointReceiverSettings_t basePointReceiverSettings =
			R_ClampShadowMapPointReceiverSettings(
				pointFar, descriptor.resolution,
				r_shadowMapPointBias.GetFloat(),
				r_shadowMapPointNormalBias.GetFloat(),
				r_shadowMapTexelBiasScale.GetFloat(),
				0.0f,
				r_shadowMapPointMaxWorldBias.GetFloat() );
		shadowMapPointReceiverSettings_t pointReceiverSettings =
			basePointReceiverSettings;
#ifndef OPENQ4_RENDERER_VK_MODULE
		// Modern GL always samples the manual color-depth cube, even when the
		// classic receiver requested and obtained a hardware comparison cube.
		// Preserve its storage-quantization floor as well as the shared
		// world-space cap so mixed classic/modern materials cannot disagree.
		pointReceiverSettings =
			R_ShadowMapPointStorageAdjustedReceiverSettings(
				basePointReceiverSettings, pointFar,
				descriptor.resolution,
				false,
				r_shadowMapPointHighPrecision.GetBool() );
#endif
		descriptor.bias[0] = pointReceiverSettings.constantBias;
		descriptor.bias[1] = pointReceiverSettings.normalBias;
		descriptor.texelDepthBias[0] =
			pointReceiverSettings.texelBiasScale
				/ static_cast<float>( Max( 1, descriptor.resolution ) );
		// The modern point receiver does not yet apply geometric normal offset;
		// its bounded depth-only contract above keeps that limitation explicit.
	} else {
		descriptor.bias[0] = r_shadowMapBias.GetFloat();
		descriptor.bias[1] = r_shadowMapNormalBias.GetFloat();
	}
	descriptor.bias[2] = r_shadowMapPolygonFactor.GetFloat();
	descriptor.bias[3] = r_shadowMapPolygonOffset.GetFloat();
	descriptor.casterPassReady = descriptor.casterCount > 0;
	descriptor.cutoutCasterReady = r_shadowMapHashedAlpha.GetBool();
	descriptor.receiverGuardReady = true;
	descriptor.modernReceiverSamplingReady = R_ModernShadowPlanner_ModernReceiverSamplingAvailable();
	descriptor.stableCascadeReady = descriptor.mapType != MODERN_SHADOW_MAP_CASCADE || r_shadowMapCascadeStabilize.GetBool();
}

static bool R_ModernShadowPlanner_ValidateProjectedDescriptor( modernShadowLightDescriptor_t &descriptor, const shadowMapLightClassification_t &classification, const char *stage ) {
	bool valid = true;
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, !descriptor.pointLight, stage, "projected descriptor is flagged as a point light" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, descriptor.projectedStateReady, stage, "projected descriptor has no projected-light state" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_FloatReady( descriptor.projectionPad ) && descriptor.projectionPad >= 0.0f, stage, "projected projection pad is invalid" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_FloatClose( descriptor.projectionScale, R_ShadowMapProjectionScale( descriptor.projectionPad ) ), stage, "projected projection scale does not match pad" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_ProjectedPlanesReady( descriptor.projectedBaseClipPlanes ), stage, "projected base clip planes contain invalid values" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_ProjectBaseClipPlanesMatchLight( descriptor ), stage, "projected base clip planes do not match padded light projection" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.requestedCascadeCount == classification.cascadeCount, stage, "requested cascade count does not match light classification" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.tileCount == descriptor.cascadeCount, stage, "projected tile count must match active cascade count" );
	if ( descriptor.requestedCascadeCount > 1 || descriptor.projectedSampleCount > 0 || descriptor.projectedCascadeFallback ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_ProjectedSampleCountsReady( descriptor ), stage, "projected cascade sample accounting is inconsistent" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_ProjectedFallbackReasonReady( descriptor ), stage, "projected fallback reason does not match sample validation" );
	}

	if ( descriptor.projectedCascadeFallback ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, descriptor.requestedCascadeCount > 1, stage, "projected cascade fallback requires a multi-cascade request" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, descriptor.projectedFallbackCascade >= 0 && descriptor.projectedFallbackCascade < descriptor.requestedCascadeCount, stage, "projected cascade fallback index is outside the requested range" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.mapType == MODERN_SHADOW_MAP_PROJECTED && descriptor.cascadeCount == 1 && descriptor.tileCount == 1 && descriptor.atlasDiv == 1, stage, "projected cascade fallback must collapse to one projected tile" );
	} else if ( classification.csmEnabled ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.mapType == MODERN_SHADOW_MAP_CASCADE, stage, "CSM-classified projected light is not a cascade descriptor" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.cascadeCount == classification.cascadeCount && descriptor.tileCount == classification.tileCount, stage, "CSM-classified projected light has mismatched cascade/tile counts" );
	} else {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.mapType == MODERN_SHADOW_MAP_PROJECTED, stage, "single projected light is not a projected descriptor" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.cascadeCount == 1 && descriptor.tileCount == 1 && descriptor.atlasDiv == 1, stage, "single projected light must use one tile" );
	}
	if ( descriptor.mapType == MODERN_SHADOW_MAP_PROJECTED ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_SingleCascadeClipPlanesMatchBase( descriptor ), stage, "single projected light does not use padded base clip planes" );
	}

	const int activeCascadeCount = idMath::ClampInt( 1, MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, descriptor.cascadeCount );
	for ( int cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_ATLAS, R_ModernShadowPlanner_AtlasRectMinMaxReady( descriptor.projectedAtlasRect[cascadeIndex] ), stage, "projected atlas rectangle is outside normalized bounds" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, R_ModernShadowPlanner_ProjectedPlanesReady( descriptor.projectedClipPlanes[cascadeIndex] ), stage, "projected clip planes contain invalid values" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS, R_ModernShadowPlanner_FloatReady( descriptor.sliceNear[cascadeIndex] ) && R_ModernShadowPlanner_FloatReady( descriptor.sliceFar[cascadeIndex] ) && descriptor.sliceFar[cascadeIndex] > descriptor.sliceNear[cascadeIndex], stage, "projected slice range is invalid" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS, R_ModernShadowPlanner_FloatReady( descriptor.depthRange[cascadeIndex] ) && descriptor.depthRange[cascadeIndex] > 0.0f, stage, "projected depth range is invalid" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS, R_ModernShadowPlanner_FloatReady( descriptor.cascadeBiasScale[cascadeIndex] ) && descriptor.cascadeBiasScale[cascadeIndex] > 0.0f, stage, "projected bias scale is invalid" );
		if ( cascadeIndex > 0 ) {
			valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS, descriptor.sliceNear[cascadeIndex] >= descriptor.sliceFar[cascadeIndex - 1] - 0.25f, stage, "projected slice ranges are not monotonic" );
		}
	}

	if ( activeCascadeCount > 1 ) {
		float previousSplit = 0.0f;
		for ( int splitIndex = 0; splitIndex < activeCascadeCount - 1; ++splitIndex ) {
			const float splitDepth = descriptor.cascadeSplitDepths[splitIndex];
			valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS, R_ModernShadowPlanner_FloatReady( splitDepth ) && splitDepth > previousSplit, stage, "cascade split depths are not strictly increasing" );
			previousSplit = splitDepth;
		}
	}

	if ( descriptor.arb2AtlasSlotReady ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_SLOT, descriptor.arb2AtlasCellSpan > 0 && descriptor.arb2AtlasCellX >= 0 && descriptor.arb2AtlasCellY >= 0 && descriptor.arb2AtlasStorageGeneration != 0, stage, "persistent-atlas slot provenance is invalid" );
		for ( int cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex ) {
			valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_SLOT, R_ModernShadowPlanner_AtlasRectMinMaxReady( descriptor.arb2AtlasCascadeRect[cascadeIndex] ), stage, "persistent-atlas slot rectangle is outside normalized bounds" );
		}
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_FRESHNESS, descriptor.arb2AtlasContentFrame >= 0 && descriptor.arb2AtlasContentFrame <= tr.frameCount, stage, "persistent-atlas slot content frame is invalid" );
	}
	return valid;
}

static bool R_ModernShadowPlanner_ValidateDescriptor( modernShadowLightDescriptor_t &descriptor, const char *stage ) {
	bool valid = true;
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( descriptor.viewLight );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_POLICY, descriptor.policy >= MODERN_SHADOW_POLICY_NONE && descriptor.policy <= MODERN_SHADOW_POLICY_SKIPPED, stage, "policy enum is outside the supported range" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.mapType >= MODERN_SHADOW_MAP_PROJECTED && descriptor.mapType <= MODERN_SHADOW_MAP_CASCADE, stage, "map type is outside the supported range" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.resolution > 0, stage, "shadow-map resolution must be positive" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.cascadeCount >= 1 && descriptor.cascadeCount <= MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, stage, "cascade count is outside descriptor limits" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.requestedCascadeCount >= 1 && descriptor.requestedCascadeCount <= MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, stage, "requested cascade count is outside descriptor limits" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.tileCount >= 1 && descriptor.tileCount <= MODERN_SHADOW_DESCRIPTOR_MAX_TILES, stage, "tile count is outside descriptor limits" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.atlasDiv >= 1, stage, "atlas division must be positive" );
	valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.estimatedPixels == descriptor.resolution * descriptor.resolution * Max( 1, descriptor.tileCount ), stage, "estimated pixel count does not match resolution and tile count" );
	if ( descriptor.viewLight != NULL ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.pointLight == classification.pointLight, stage, "point-light flag does not match shared classification" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.parallel == classification.parallelLight, stage, "parallel-light flag does not match shared classification" );
	}

	if ( descriptor.pointLight ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS, descriptor.mapType == MODERN_SHADOW_MAP_POINT, stage, "point light is not a point shadow descriptor" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.cascadeCount == 1 && descriptor.requestedCascadeCount == 1 && descriptor.tileCount == 6 && descriptor.atlasDiv == 3, stage, "point light must use one logical cascade and a 3-wide six-face atlas layout" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE, !descriptor.projectedStateReady, stage, "point light must not carry projected-light state" );
	} else {
		valid &= R_ModernShadowPlanner_ValidateProjectedDescriptor( descriptor, classification, stage );
	}
	if ( descriptor.arb2PointCubeReady ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_SLOT, descriptor.pointLight, stage, "point-cube provenance attached to a projected light" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_FRESHNESS, descriptor.arb2PointCubeContentFrame >= 0 && descriptor.arb2PointCubeContentFrame <= tr.frameCount, stage, "point-cube content frame is invalid" );
	}

	if ( descriptor.mapType == MODERN_SHADOW_MAP_CASCADE ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, !descriptor.pointLight && descriptor.cascadeCount >= 2 && descriptor.tileCount == descriptor.cascadeCount, stage, "cascade descriptors must use one tile per cascade" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.atlasDiv * descriptor.atlasDiv >= descriptor.cascadeCount, stage, "cascade atlas grid cannot contain all cascades" );
	} else if ( descriptor.mapType == MODERN_SHADOW_MAP_PROJECTED ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, !descriptor.pointLight && descriptor.cascadeCount == 1 && descriptor.tileCount == 1, stage, "projected descriptors must use one tile" );
	} else if ( descriptor.mapType == MODERN_SHADOW_MAP_POINT ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS, descriptor.pointLight && descriptor.tileCount == 6 && descriptor.atlasDiv == 3 && descriptor.atlasDiv * descriptor.atlasDiv >= descriptor.tileCount, stage, "point descriptors must use the ARB2 six-face atlas layout" );
	}

	if ( descriptor.policy == MODERN_SHADOW_POLICY_MAPPED || descriptor.atlasTileReady ) {
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_TILES, descriptor.atlasTileReady, stage, "mapped descriptor has no assigned atlas tiles" );
		valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_TILES, R_ModernShadowPlanner_AtlasRectXYWHReady( descriptor.atlasRect ), stage, "descriptor atlas rectangle is outside normalized bounds" );
		for ( int tileIndex = 0; tileIndex < descriptor.tileCount && tileIndex < MODERN_SHADOW_DESCRIPTOR_MAX_TILES; ++tileIndex ) {
			valid &= R_ModernShadowPlanner_CheckDescriptorInvariant( descriptor, MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_TILES, R_ModernShadowPlanner_AtlasRectXYWHReady( descriptor.tileAtlasRect[tileIndex] ), stage, "descriptor tile atlas rectangle is outside normalized bounds" );
		}
	}

	return valid;
}

static void R_ModernShadowPlanner_SetDescriptorInvariantPolicy( modernShadowLightDescriptor_t &descriptor ) {
	descriptor.fallbackReason = MODERN_SHADOW_FALLBACK_DESCRIPTOR_INVARIANT;
	descriptor.policy = R_ModernShadowPlanner_LightHasStencilFallback( descriptor ) ? MODERN_SHADOW_POLICY_STENCIL_FALLBACK : MODERN_SHADOW_POLICY_SKIPPED;
	descriptor.stencilFallback = descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
	descriptor.cacheReuseCandidate = false;
	descriptor.atlasTileReady = false;
	descriptor.arb2AtlasSlotReady = false;
	descriptor.arb2PointCubeReady = false;
	descriptor.arb2AtlasSignature = 0;
	descriptor.arb2AtlasStorageGeneration = 0;
	descriptor.arb2PointCubeSignature = 0;
	descriptor.arb2AtlasContentFrame = -1;
	descriptor.arb2PointCubeContentFrame = -1;
}

static modernShadowFallbackReason_t R_ModernShadowPlanner_SupportReason( const viewLight_t *vLight ) {
	if ( !r_shadows.GetBool() ) {
		return MODERN_SHADOW_FALLBACK_SHADOWS_DISABLED;
	}
	if ( vLight == NULL ) {
		return MODERN_SHADOW_FALLBACK_NULL_LIGHT;
	}
	if ( vLight->lightDef != NULL ) {
		if ( vLight->lightDef->parms.noShadows ) {
			return MODERN_SHADOW_FALLBACK_NO_SHADOWS_FLAG;
		}
		if ( vLight->lightDef->parms.noDynamicShadows ) {
			return MODERN_SHADOW_FALLBACK_NO_DYNAMIC_SHADOWS_FLAG;
		}
	}
	if ( vLight->lightShader == NULL || vLight->lightShader->IsAmbientLight() ) {
		return MODERN_SHADOW_FALLBACK_AMBIENT_LIGHT;
	}
	if ( !vLight->lightShader->LightCastsShadows() ) {
		return MODERN_SHADOW_FALLBACK_LIGHT_SHADER_NO_SHADOWS;
	}
	if ( !R_ModernShadowPlanner_LightHasReceivers( vLight ) ) {
		return MODERN_SHADOW_FALLBACK_NO_RECEIVERS;
	}
	if ( r_rendererOcclusion.GetBool() && R_ModernShadowPlanner_ScissorArea( vLight->scissorRect ) <= 0 ) {
		return MODERN_SHADOW_FALLBACK_NO_RECEIVERS;
	}
	if ( !r_useShadowMap.GetBool() ) {
		return MODERN_SHADOW_FALLBACK_SHADOW_MAP_DISABLED;
	}
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT;
	}
	if ( R_ClassifyShadowMapLight( vLight ).pointLight && !glConfig.cubeMapAvailable ) {
		return MODERN_SHADOW_FALLBACK_CUBEMAP_UNAVAILABLE;
	}
	return MODERN_SHADOW_FALLBACK_NONE;
}

static bool R_ModernShadowPlanner_ReasonIsIntentionalSkip( modernShadowFallbackReason_t reason ) {
	return reason == MODERN_SHADOW_FALLBACK_SHADOWS_DISABLED
		|| reason == MODERN_SHADOW_FALLBACK_NULL_LIGHT
		|| reason == MODERN_SHADOW_FALLBACK_NO_SHADOWS_FLAG
		|| reason == MODERN_SHADOW_FALLBACK_NO_DYNAMIC_SHADOWS_FLAG
		|| reason == MODERN_SHADOW_FALLBACK_AMBIENT_LIGHT
		|| reason == MODERN_SHADOW_FALLBACK_LIGHT_SHADER_NO_SHADOWS
		|| reason == MODERN_SHADOW_FALLBACK_NO_RECEIVERS;
}

static void R_ModernShadowPlanner_ApplyFairnessPriority( modernShadowLightDescriptor_t &descriptor ) {
	descriptor.fairnessAge = 0;
	descriptor.fairnessBoost = 0;
	descriptor.fairnessPriority = descriptor.priority;

	const modernShadowPlannerFairnessHistory_t *history = R_ModernShadowPlanner_FindFairnessHistoryConst( descriptor );
	R_ModernShadowPlanner_CopyThrottleHistoryToDescriptor( descriptor, history );
	if ( history == NULL || history->budgetMissAge <= 0 ) {
		return;
	}

	descriptor.fairnessAge = idMath::ClampInt( 0, MODERN_SHADOW_FAIRNESS_MAX_AGE, history->budgetMissAge );
	descriptor.fairnessBoost = descriptor.fairnessAge * MODERN_SHADOW_FAIRNESS_BOOST_PER_MISS;
	descriptor.fairnessPriority = Min( 0x3fffffff, descriptor.priority + descriptor.fairnessBoost );
}

static void R_ModernShadowPlanner_InitDescriptor( modernShadowLightDescriptor_t &descriptor, const viewLight_t *vLight, const viewDef_t *viewDef, int sceneIndex, int descriptorIndex, int shadowMapSize, bool translucentAvailable ) {
	memset( &descriptor, 0, sizeof( descriptor ) );
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	descriptor.viewLight = vLight;
	descriptor.descriptorIndex = descriptorIndex;
	descriptor.sceneIndex = sceneIndex;
	descriptor.lightDefIndex = vLight != NULL && vLight->lightDef != NULL ? vLight->lightDef->index : -1;
	descriptor.pointLight = classification.pointLight;
	descriptor.parallel = classification.parallelLight;
	descriptor.resolution = shadowMapSize;
	descriptor.requestedCascadeCount = classification.cascadeCount;
	descriptor.atlasDiv = classification.atlasDiv;
	descriptor.updateModulo = Max( 1, RendererBenchmarks_CurrentBudget().shadowUpdateRate );
	descriptor.localCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->localShadows ) : 0;
	descriptor.globalCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalShadows ) : 0;
	descriptor.translucentCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localTranslucentShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalTranslucentShadowMapCasters ) : 0;
	descriptor.localReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localInteractions ) : 0;
	descriptor.globalReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalInteractions ) : 0;
	descriptor.translucentReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->translucentInteractions ) : 0;
	descriptor.lodCasterTestCount = vLight != NULL ? vLight->shadowMapLODTestCount : 0;
	descriptor.lodCasterRejectedCount = vLight != NULL ? vLight->shadowMapLODRejectedCount : 0;
	descriptor.lodAlphaCasterRejectedCount = vLight != NULL ? vLight->shadowMapLODAlphaRejectedCount : 0;
	descriptor.lodTranslucentCasterRejectedCount = vLight != NULL ? vLight->shadowMapLODTranslucentRejectedCount : 0;
	descriptor.translucentMoments = translucentAvailable && descriptor.translucentCasterCount > 0;
	descriptor.cascadeCount = classification.cascadeCount;
	descriptor.tileCount = classification.tileCount;
	descriptor.mapType = MODERN_SHADOW_MAP_PROJECTED;
	if ( classification.pointLight ) {
		descriptor.mapType = MODERN_SHADOW_MAP_POINT;
		// point cubes have their own resolution knob; must match the ARB2
		// clamp in RB_ShadowMapPointSizeValue for descriptor parity
		descriptor.resolution = idMath::ClampInt( 128, 2048, r_shadowMapPointSize.GetInteger() );
	} else if ( classification.csmEnabled ) {
		descriptor.mapType = MODERN_SHADOW_MAP_CASCADE;
	}
	descriptor.budgetClass = R_ModernShadowPlanner_BudgetClassForDescriptor( descriptor );
	R_ModernShadowPlanner_InitDescriptorContract( descriptor, vLight, viewDef );
	R_ModernShadowPlanner_ApplyArb2CacheEstimate( descriptor, vLight, viewDef );
	R_ModernShadowPlanner_ResolveArb2AtlasSlot( descriptor, vLight, viewDef );
	descriptor.estimatedPixels = descriptor.resolution * descriptor.resolution * Max( 1, descriptor.tileCount );
	const int scissorArea = vLight != NULL ? R_ModernShadowPlanner_ScissorArea( vLight->scissorRect ) : 0;
	descriptor.priority =
		scissorArea / 32
		+ ( descriptor.globalReceiverCount + descriptor.localReceiverCount ) * 512
		+ ( descriptor.globalCasterCount + descriptor.localCasterCount ) * 128
		+ ( descriptor.viewLight != NULL && descriptor.viewLight->viewInsideLight ? 1024 : 0 )
		+ ( descriptor.pointLight ? 256 : 0 )
		+ ( descriptor.parallel ? 128 : 0 );
	R_ModernShadowPlanner_ApplyFairnessPriority( descriptor );
	descriptor.fallbackReason = R_ModernShadowPlanner_SupportReason( vLight );
	if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_NONE ) {
		descriptor.policy = MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
	} else if ( R_ModernShadowPlanner_ReasonIsIntentionalSkip( descriptor.fallbackReason ) ) {
		descriptor.policy = MODERN_SHADOW_POLICY_SKIPPED;
	} else {
		descriptor.policy = R_ModernShadowPlanner_LightHasStencilFallback( descriptor ) ? MODERN_SHADOW_POLICY_STENCIL_FALLBACK : MODERN_SHADOW_POLICY_SKIPPED;
		descriptor.stencilFallback = descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
	}
	if ( !R_ModernShadowPlanner_ValidateDescriptor( descriptor, "init" ) ) {
		R_ModernShadowPlanner_SetDescriptorInvariantPolicy( descriptor );
	}
}

static bool R_ModernShadowPlanner_CandidateBetter( const modernShadowLightDescriptor_t &candidate, const modernShadowLightDescriptor_t &best ) {
	if ( candidate.fairnessPriority != best.fairnessPriority ) {
		return candidate.fairnessPriority > best.fairnessPriority;
	}
	if ( candidate.fairnessAge != best.fairnessAge ) {
		return candidate.fairnessAge > best.fairnessAge;
	}
	if ( candidate.priority != best.priority ) {
		return candidate.priority > best.priority;
	}
	return candidate.lightDefIndex < best.lightDefIndex;
}

// Total order over descriptor indices matching the repeated best-candidate
// rescan: strictly-better wins, and a full tie keeps the lower descriptor index
// (what the linear scan's "only replace on strictly better" produces). The
// explicit index tie-break makes the result independent of the unstable sort.
static int R_ModernShadowPlanner_CompareCandidateIndices( const int *a, const int *b ) {
	const modernShadowLightDescriptor_t &A = rg_modernShadowPlannerDescriptors[*a];
	const modernShadowLightDescriptor_t &B = rg_modernShadowPlannerDescriptors[*b];
	if ( R_ModernShadowPlanner_CandidateBetter( A, B ) ) {
		return -1;
	}
	if ( R_ModernShadowPlanner_CandidateBetter( B, A ) ) {
		return 1;
	}
	return *a - *b;
}

static void R_ModernShadowPlanner_AssignAtlasTiles( modernShadowLightDescriptor_t &descriptor, int firstTile, int tilesPerRow ) {
	tilesPerRow = Max( 1, tilesPerRow );
	const float slotScale = 1.0f / static_cast<float>( tilesPerRow );
	float minX = 1.0f;
	float minY = 1.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
	for ( int tileIndex = 0; tileIndex < Max( 1, descriptor.tileCount ) && tileIndex < MODERN_SHADOW_DESCRIPTOR_MAX_TILES; ++tileIndex ) {
		const int atlasSlot = firstTile + tileIndex;
		const float x = static_cast<float>( atlasSlot % tilesPerRow ) * slotScale;
		const float y = static_cast<float>( atlasSlot / tilesPerRow ) * slotScale;
		descriptor.tileAtlasRect[tileIndex][0] = x;
		descriptor.tileAtlasRect[tileIndex][1] = y;
		descriptor.tileAtlasRect[tileIndex][2] = slotScale;
		descriptor.tileAtlasRect[tileIndex][3] = slotScale;
		minX = Min( minX, x );
		minY = Min( minY, y );
		maxX = Max( maxX, x + slotScale );
		maxY = Max( maxY, y + slotScale );
	}
	descriptor.atlasRect[0] = minX;
	descriptor.atlasRect[1] = minY;
	descriptor.atlasRect[2] = Max( 0.0f, maxX - minX );
	descriptor.atlasRect[3] = Max( 0.0f, maxY - minY );
	descriptor.atlasTileReady = true;
}

static void R_ModernShadowPlanner_SetBudgetMissPolicy( modernShadowLightDescriptor_t &descriptor ) {
	descriptor.fallbackReason = MODERN_SHADOW_FALLBACK_BUDGET;
	descriptor.atlasTileReady = false;
	descriptor.cacheReuseCandidate = R_ModernShadowPlanner_LightMayReuseCachedMap( descriptor );
	if ( descriptor.cacheReuseCandidate || R_ModernShadowPlanner_LightHasStencilFallback( descriptor ) ) {
		descriptor.policy = MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
		descriptor.stencilFallback = true;
		return;
	}

	descriptor.policy = MODERN_SHADOW_POLICY_SKIPPED;
	descriptor.stencilFallback = false;
	descriptor.cacheReuseCandidate = false;
}

static void R_ModernShadowPlanner_SelectMappedLights( modernShadowPlannerStats_t &stats ) {
	modernShadowPlannerBudgetUse_t budgetUse;
	memset( &budgetUse, 0, sizeof( budgetUse ) );
	int mappedLights = 0;
	int mappedTiles = 0;
	int usedPixels = 0;
	const int tilePixels = Max( 1, stats.shadowMapSize ) * Max( 1, stats.shadowMapSize );
	const int atlasSlotsPerRow = Max( 1, static_cast<int>( idMath::Ceil( idMath::Sqrt( static_cast<float>( stats.maxAtlasTiles ) ) ) ) );

	// Collect the selectable candidates once and order them exactly as the
	// repeated best-candidate rescan would, then process in that fixed order.
	// Processing a candidate only mutates that candidate's own keys, so a
	// pre-sorted order is identical to the O(selectable * total) rescan.
	static idList<int> selectionOrder;
	selectionOrder.SetNum( 0, false );
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		if ( rg_modernShadowPlannerDescriptors[i].fallbackReason == MODERN_SHADOW_FALLBACK_NONE ) {
			selectionOrder.Append( i );
		}
	}
	selectionOrder.Sort( R_ModernShadowPlanner_CompareCandidateIndices );

	for ( int orderIndex = 0; orderIndex < selectionOrder.Num(); ++orderIndex ) {
		const int candidateIndex = selectionOrder[orderIndex];
		modernShadowLightDescriptor_t &candidate = rg_modernShadowPlannerDescriptors[candidateIndex];
		const modernShadowBudgetClass_t budgetClass = R_ModernShadowPlanner_BudgetClassForDescriptor( candidate );
		candidate.budgetClass = budgetClass;
		if ( R_ModernShadowPlanner_CanIsolateArb2CacheOwnership( candidate ) ) {
			candidate.policy = MODERN_SHADOW_POLICY_CACHE_REUSE;
			candidate.fallbackReason = MODERN_SHADOW_FALLBACK_NONE;
			candidate.stencilFallback = false;
			candidate.cacheReuseCandidate = true;
			candidate.arb2CacheBudgetIsolated = true;
			candidate.budgetThrottleReasonMask = MODERN_SHADOW_THROTTLE_NONE;
			continue;
		}
		const int candidateTiles = Max( 1, candidate.tileCount );
		const int classLightQuota = R_ModernShadowPlanner_ClassLightQuota( stats, budgetClass );
		const int classTileQuota = R_ModernShadowPlanner_ClassTileQuota( stats, budgetClass );
		const int classPixelQuota = Max( 1, classTileQuota ) * tilePixels;
		const bool classLightAvailable = budgetUse.mappedLights[budgetClass] < classLightQuota;
		const bool classTileAvailable = budgetUse.atlasTiles[budgetClass] + candidateTiles <= classTileQuota;
		const bool classPixelAvailable = budgetUse.pixels[budgetClass] + candidate.estimatedPixels <= classPixelQuota;
		const bool globalLightAvailable = mappedLights < stats.maxMappedLights;
		const bool globalTileAvailable = mappedTiles + candidateTiles <= stats.maxAtlasTiles;
		const bool globalPixelAvailable = usedPixels + candidate.estimatedPixels <= stats.budgetedPixels;
		const bool classBudgetAvailable = classLightAvailable && classTileAvailable && classPixelAvailable;
		const bool globalBudgetAvailable = globalLightAvailable && globalTileAvailable && globalPixelAvailable;
		if ( classBudgetAvailable && globalBudgetAvailable ) {
			R_ModernShadowPlanner_AssignAtlasTiles( candidate, mappedTiles, atlasSlotsPerRow );
			candidate.policy = MODERN_SHADOW_POLICY_MAPPED;
			candidate.fallbackReason = MODERN_SHADOW_FALLBACK_NONE;
			candidate.stencilFallback = false;
			candidate.cacheReuseCandidate = false;
			candidate.budgetThrottleReasonMask = MODERN_SHADOW_THROTTLE_NONE;
			if ( !R_ModernShadowPlanner_ValidateDescriptor( candidate, "atlas" ) ) {
				R_ModernShadowPlanner_SetDescriptorInvariantPolicy( candidate );
				continue;
			}
			mappedLights++;
			mappedTiles += candidateTiles;
			usedPixels += candidate.estimatedPixels;
			budgetUse.mappedLights[budgetClass]++;
			budgetUse.atlasTiles[budgetClass] += candidateTiles;
			budgetUse.pixels[budgetClass] += candidate.estimatedPixels;
			continue;
		}
		unsigned int throttleReasonMask = MODERN_SHADOW_THROTTLE_NONE;
		if ( !classLightAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_CLASS_LIGHT;
		}
		if ( !classTileAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_CLASS_TILE;
		}
		if ( !classPixelAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_CLASS_PIXEL;
		}
		if ( !globalLightAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_GLOBAL_LIGHT;
		}
		if ( !globalTileAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_GLOBAL_TILE;
		}
		if ( !globalPixelAvailable ) {
			throttleReasonMask |= MODERN_SHADOW_THROTTLE_GLOBAL_PIXEL;
		}
		candidate.budgetThrottleReasonMask = throttleReasonMask;
		R_ModernShadowPlanner_SetBudgetMissPolicy( candidate );
		stats.budgetThrottledLights++;
		R_ModernShadowPlanner_RecordClassBudgetThrottle( stats, budgetClass );
		R_ModernShadowPlanner_RecordBudgetThrottleReason( stats, throttleReasonMask );
	}
	stats.atlasTiles = mappedTiles;
}

static void R_ModernShadowPlanner_UpdateFairnessHistory( modernShadowPlannerStats_t &stats ) {
	for ( int descriptorIndex = 0; descriptorIndex < rg_modernShadowPlannerDescriptors.Num(); ++descriptorIndex ) {
		modernShadowLightDescriptor_t &descriptor = rg_modernShadowPlannerDescriptors[descriptorIndex];
		const bool relevantForFairness =
			descriptor.policy == MODERN_SHADOW_POLICY_MAPPED ||
			descriptor.policy == MODERN_SHADOW_POLICY_CACHE_REUSE ||
			descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ||
			descriptor.fairnessAge > 0 ||
			descriptor.throttleHistoryMissStreak > 0 ||
			descriptor.throttleHistoryTotalMisses > 0;
		if ( !relevantForFairness ) {
			continue;
		}

		modernShadowPlannerFairnessHistory_t *history = R_ModernShadowPlanner_FindFairnessHistory( descriptor, true );
		if ( history == NULL ) {
			continue;
		}

		const int previousBudgetMissAge = history->budgetMissAge;
		history->lastSeenEpoch = rg_modernShadowPlannerFairnessEpoch;
		stats.fairnessTrackedLights++;
		stats.throttleHistoryTrackedLights++;
		if ( descriptor.fairnessAge > 0 ) {
			stats.fairnessAgedLights++;
			stats.fairnessMaxAge = Max( stats.fairnessMaxAge, descriptor.fairnessAge );
		}
		if ( descriptor.fairnessBoost > 0 ) {
			stats.fairnessBoostedLights++;
			stats.fairnessMaxBoost = Max( stats.fairnessMaxBoost, descriptor.fairnessBoost );
		}

		if ( descriptor.policy == MODERN_SHADOW_POLICY_MAPPED || descriptor.policy == MODERN_SHADOW_POLICY_CACHE_REUSE ) {
			if ( descriptor.fairnessAge > 0 ) {
				stats.fairnessAgedMappedLights++;
			}
			if ( previousBudgetMissAge > 0 ) {
				stats.throttleHistoryRecoveredLights++;
			}
			history->budgetMissAge = 0;
			history->totalMappedFrames++;
			history->lastMappedEpoch = rg_modernShadowPlannerFairnessEpoch;
		} else if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ) {
			if ( previousBudgetMissAge > 0 ) {
				stats.throttleHistoryRepeatedBudgetMissLights++;
			}
			history->budgetMissAge = idMath::ClampInt( 1, MODERN_SHADOW_FAIRNESS_MAX_AGE, history->budgetMissAge + 1 );
			history->totalBudgetMisses++;
			history->throttleReasonMask |= descriptor.budgetThrottleReasonMask;
			history->lastThrottleReasonMask = descriptor.budgetThrottleReasonMask;
			history->lastBudgetClass = descriptor.budgetClass;
			history->lastBudgetMissEpoch = rg_modernShadowPlannerFairnessEpoch;
			stats.throttleHistoryLastMissLightDefIndex = descriptor.lightDefIndex;
			stats.throttleHistoryLastMissReasonMask = descriptor.budgetThrottleReasonMask;
			stats.throttleHistoryLastMissBudgetClass = descriptor.budgetClass;
		} else {
			history->budgetMissAge = 0;
		}
		history->lastPolicy = descriptor.policy;
		history->lastFallbackReason = descriptor.fallbackReason;
		if ( history->budgetMissAge > stats.throttleHistoryMaxMissStreak ) {
			stats.throttleHistoryMaxMissStreak = history->budgetMissAge;
			stats.throttleHistoryMaxMissLightDefIndex = descriptor.lightDefIndex;
		}
		stats.throttleHistoryMaxTotalMisses = Max( stats.throttleHistoryMaxTotalMisses, history->totalBudgetMisses );
		R_ModernShadowPlanner_CopyThrottleHistoryToDescriptor( descriptor, history );
	}
}

static void R_ModernShadowPlanner_ValidatePreparedDescriptors( void ) {
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		modernShadowLightDescriptor_t &descriptor = rg_modernShadowPlannerDescriptors[i];
		if ( !R_ModernShadowPlanner_ValidateDescriptor( descriptor, "final" ) ) {
			R_ModernShadowPlanner_SetDescriptorInvariantPolicy( descriptor );
		}
	}
}

static void R_ModernShadowPlanner_CountDescriptor( const modernShadowLightDescriptor_t &descriptor, modernShadowPlannerStats_t &stats ) {
	const int casterCount = descriptor.localCasterCount + descriptor.globalCasterCount + descriptor.translucentCasterCount;
	stats.visibilityCasterTests += casterCount;
	if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_NO_RECEIVERS || descriptor.policy == MODERN_SHADOW_POLICY_SKIPPED ) {
		if ( casterCount > 0 ) {
			stats.visibilityCasterRejected += casterCount;
			stats.visibilityCasterSavedDraws += casterCount;
		}
		if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_NO_RECEIVERS ) {
			stats.visibilityReceiverScissorCulledLights++;
		}
	}
	stats.descriptorCount++;
	stats.shadowRelevantLights++;
	stats.localCasterCount += descriptor.localCasterCount;
	stats.globalCasterCount += descriptor.globalCasterCount;
	stats.translucentCasterCount += descriptor.translucentCasterCount;
	stats.localReceiverCount += descriptor.localReceiverCount;
	stats.globalReceiverCount += descriptor.globalReceiverCount;
	stats.translucentReceiverCount += descriptor.translucentReceiverCount;
	stats.lodCasterTestCount += descriptor.lodCasterTestCount;
	stats.lodCasterRejectedCount += descriptor.lodCasterRejectedCount;
	stats.lodAlphaCasterRejectedCount += descriptor.lodAlphaCasterRejectedCount;
	stats.lodTranslucentCasterRejectedCount += descriptor.lodTranslucentCasterRejectedCount;
	if ( descriptor.lodCasterRejectedCount > 0 ) {
		stats.lodRejectedLights++;
		if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ) {
			stats.lodRejectedBudgetThrottledLights++;
		}
		if ( descriptor.policy != MODERN_SHADOW_POLICY_MAPPED && descriptor.policy != MODERN_SHADOW_POLICY_CACHE_REUSE ) {
			stats.lodRejectedUnmappedLights++;
		}
	}
	if ( descriptor.invariantFailureMask != MODERN_SHADOW_DESCRIPTOR_INVARIANT_NONE ) {
		stats.descriptorInvariantFailures++;
		stats.descriptorInvariantFailureMask |= descriptor.invariantFailureMask;
	}
	if ( descriptor.cutoutCasterReady && R_ModernShadowPlanner_TotalCasterCount( descriptor ) > 0 ) {
		stats.cutoutCasterLights++;
	}
	if ( descriptor.receiverGuardReady && R_ModernShadowPlanner_TotalReceiverCount( descriptor ) > 0 ) {
		stats.receiverGuardedLights++;
	}
	if ( descriptor.arb2CacheEstimateValid ) {
		stats.arb2CacheAwareLights++;
		stats.arb2ShadowPasses += descriptor.arb2ShadowPasses;
		stats.arb2CacheablePasses += descriptor.arb2CacheablePasses;
		stats.arb2CacheHitPasses += descriptor.arb2CacheHitPasses;
		stats.arb2CacheMissPasses += descriptor.arb2CacheMissPasses;
		stats.arb2FreshUpdatePasses += descriptor.arb2FreshUpdatePasses;
		stats.arb2BudgetFallbackPasses += descriptor.arb2BudgetFallbackPasses;
		stats.arb2StencilOnlyPasses += descriptor.arb2StencilOnlyPasses;
		stats.arb2ReceiverFallbackPasses += descriptor.arb2ReceiverFallbackPasses;
		stats.arb2UnshadowedPasses += descriptor.arb2UnshadowedPasses;
		if ( descriptor.arb2CacheablePasses > 0 ) {
			stats.arb2CacheableLights++;
		}
		if ( descriptor.arb2CacheReuseAvailable ) {
			stats.arb2CacheReuseLights++;
		}
		if ( descriptor.arb2CacheFullyReusable ) {
			stats.arb2CacheFullyReusableLights++;
		}
		if ( descriptor.arb2AtlasSlotReady ) {
			stats.arb2AtlasSlotReadyLights++;
		}
		if ( descriptor.arb2FreshUpdatePasses > 0 ) {
			stats.arb2FreshUpdateLights++;
		}
		if ( descriptor.arb2BudgetFallbackPasses > 0 ) {
			stats.arb2BudgetFallbackLights++;
		}
	}
	if ( descriptor.arb2CacheBudgetIsolated ) {
		stats.arb2CacheBudgetIsolatedLights++;
	}
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( descriptor.viewLight );
	const bool projectedCsmEligible = classification.ordinaryProjectedLight
		&& stats.csmRequested
		&& idMath::ClampInt( 1, SHADOWMAP_CLASSIFICATION_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() ) > 1;
	if ( descriptor.pointLight ) {
		stats.pointLights++;
	} else {
		stats.projectedLights++;
	}
	if ( classification.ordinaryProjectedLight ) {
		stats.ordinaryProjectedLights++;
		if ( projectedCsmEligible ) {
			stats.projectedCsmEligibleLights++;
			if ( classification.csmEnabled ) {
				stats.projectedCsmCascadeLights++;
			} else {
				stats.projectedCsmSuppressedLights++;
			}
		}
	} else if ( classification.csmEnabled ) {
		stats.nonProjectedCsmLights++;
	}
	if ( descriptor.parallel ) {
		stats.parallelLights++;
	}
	if ( descriptor.mapType == MODERN_SHADOW_MAP_CASCADE ) {
		stats.cascadeLights++;
		stats.cascadeCount += descriptor.cascadeCount;
	}
	if ( descriptor.policy == MODERN_SHADOW_POLICY_MAPPED ) {
		stats.mappedLights++;
		R_ModernShadowPlanner_RecordMappedClassStats( stats, descriptor );
		stats.mappedPasses += Max( 1, descriptor.tileCount );
		stats.atlasTiles += Max( 1, descriptor.tileCount );
		stats.estimatedPixels += descriptor.estimatedPixels;
		if ( !descriptor.modernReceiverSamplingReady ) {
			stats.receiverSamplingBlockedLights++;
		}
	} else if ( descriptor.policy == MODERN_SHADOW_POLICY_CACHE_REUSE ) {
		if ( !descriptor.modernReceiverSamplingReady ) {
			stats.receiverSamplingBlockedLights++;
		}
	} else if ( descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK ) {
		stats.fallbackLights++;
		stats.fallbackPasses += Max( 1, descriptor.tileCount );
		stats.stencilFallbackPasses += Max( 1, descriptor.tileCount );
	} else {
		stats.skippedLights++;
	}
	if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ) {
		if ( descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK ) {
			stats.budgetFallbackLights++;
			stats.budgetStencilFallbackLights++;
			if ( descriptor.cacheReuseCandidate ) {
				stats.budgetCacheReuseCandidateLights++;
			}
		} else if ( descriptor.policy == MODERN_SHADOW_POLICY_SKIPPED ) {
			stats.budgetSkippedLights++;
		}
	}
	if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT ) {
		stats.textureLimitFallbacks++;
	} else if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_CUBEMAP_UNAVAILABLE ) {
		stats.cubemapFallbacks++;
	} else if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_NO_RECEIVERS ) {
		stats.noReceiverLights++;
	}
}

static void R_ModernShadowPlanner_FinalizeStats( modernShadowPlannerStats_t &stats ) {
	stats.descriptorCount = 0;
	stats.shadowRelevantLights = 0;
	stats.lodCasterTestCount = 0;
	stats.lodCasterRejectedCount = 0;
	stats.lodAlphaCasterRejectedCount = 0;
	stats.lodTranslucentCasterRejectedCount = 0;
	stats.lodRejectedLights = 0;
	stats.lodRejectedBudgetThrottledLights = 0;
	stats.lodRejectedUnmappedLights = 0;
	stats.atlasTiles = 0;
	stats.singleProjectedMappedLights = 0;
	stats.cascadeMappedLights = 0;
	stats.pointMappedLights = 0;
	stats.singleProjectedAtlasTiles = 0;
	stats.cascadeAtlasTiles = 0;
	stats.pointAtlasTiles = 0;
	stats.budgetFallbackLights = 0;
	stats.budgetStencilFallbackLights = 0;
	stats.budgetCacheReuseCandidateLights = 0;
	stats.budgetSkippedLights = 0;
	stats.arb2CacheAwareLights = 0;
	stats.arb2CacheableLights = 0;
	stats.arb2CacheReuseLights = 0;
	stats.arb2AtlasSlotReadyLights = 0;
	stats.arb2CacheFullyReusableLights = 0;
	stats.arb2CacheBudgetIsolatedLights = 0;
	stats.arb2FreshUpdateLights = 0;
	stats.arb2BudgetFallbackLights = 0;
	stats.arb2ShadowPasses = 0;
	stats.arb2CacheablePasses = 0;
	stats.arb2CacheHitPasses = 0;
	stats.arb2CacheMissPasses = 0;
	stats.arb2FreshUpdatePasses = 0;
	stats.arb2BudgetFallbackPasses = 0;
	stats.arb2StencilOnlyPasses = 0;
	stats.arb2ReceiverFallbackPasses = 0;
	stats.arb2UnshadowedPasses = 0;
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		R_ModernShadowPlanner_CountDescriptor( rg_modernShadowPlannerDescriptors[i], stats );
	}
	stats.frameValid = true;
	if ( stats.descriptorInvariantFailures > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-invariant-failed" );
	} else if ( stats.overflow ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-overflow" );
	} else if ( stats.mappedLights > 0 && stats.fallbackLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-mixed" );
	} else if ( stats.mappedLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-mapped" );
	} else if ( stats.arb2CacheBudgetIsolatedLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-cache-reuse" );
	} else if ( stats.fallbackLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-fallback" );
	} else {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-empty" );
	}
}

static void R_ModernShadowPlanner_RecordRendererMetrics( const modernShadowPlannerStats_t &stats ) {
	R_RendererMetrics_RecordModernShadowPlanner(
		stats.descriptorCount,
		stats.descriptorInvariantFailures,
		stats.descriptorInvariantFailureMask,
		stats.projectedCsmEligibleLights,
		stats.projectedCsmCascadeLights,
		stats.projectedCsmSuppressedLights,
		stats.nonProjectedCsmLights,
		stats.budgetThrottledLights,
		stats.budgetThrottleReasonMask,
		stats.throttleHistoryTrackedLights,
		stats.throttleHistoryRepeatedBudgetMissLights,
		stats.throttleHistoryRecoveredLights,
		stats.throttleHistoryMaxMissStreak,
		stats.throttleHistoryMaxMissLightDefIndex,
		stats.lodRejectedLights,
		stats.lodRejectedBudgetThrottledLights,
		stats.lodRejectedUnmappedLights,
		stats.arb2CacheReuseLights,
		stats.arb2CacheBudgetIsolatedLights,
		stats.arb2CacheHitPasses,
		stats.arb2FreshUpdatePasses );
}

static bool R_ModernShadowPlanner_HasArb2CacheStats( const modernShadowPlannerStats_t &stats ) {
	return stats.arb2CacheAwareLights > 0
		|| stats.arb2CacheReuseLights > 0
		|| stats.arb2CacheBudgetIsolatedLights > 0
		|| stats.arb2FreshUpdatePasses > 0
		|| stats.arb2BudgetFallbackPasses > 0;
}

static void R_ModernShadowPlanner_PrintArb2CacheStats( const modernShadowPlannerStats_t &stats, const char *prefix ) {
	if ( !R_ModernShadowPlanner_HasArb2CacheStats( stats ) ) {
		return;
	}
	common->Printf(
		"%s arb2Cache(lights aware=%d cacheable=%d reuse=%d atlasSlot=%d full=%d isolated=%d fresh=%d budgetFallback=%d passes total=%d cacheable=%d hit=%d miss=%d fresh=%d budgetFallback=%d stencilOnly=%d receiverFallback=%d unshadowed=%d)\n",
		prefix != NULL ? prefix : "modernShadowPlan",
		stats.arb2CacheAwareLights,
		stats.arb2CacheableLights,
		stats.arb2CacheReuseLights,
		stats.arb2AtlasSlotReadyLights,
		stats.arb2CacheFullyReusableLights,
		stats.arb2CacheBudgetIsolatedLights,
		stats.arb2FreshUpdateLights,
		stats.arb2BudgetFallbackLights,
		stats.arb2ShadowPasses,
		stats.arb2CacheablePasses,
		stats.arb2CacheHitPasses,
		stats.arb2CacheMissPasses,
		stats.arb2FreshUpdatePasses,
		stats.arb2BudgetFallbackPasses,
		stats.arb2StencilOnlyPasses,
		stats.arb2ReceiverFallbackPasses,
		stats.arb2UnshadowedPasses );
}

void R_ModernShadowPlanner_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	rg_modernShadowPlannerCaps = caps;
	rg_modernShadowPlannerFeatures = features;
	rg_modernShadowPlannerInitialized = true;
	rg_modernShadowPlannerDescriptors.Clear();
	R_ModernShadowPlanner_ResetFairnessHistory();
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	rg_modernShadowPlannerStats.initialized = true;
	rg_modernShadowPlannerStats.available = features.scenePackets;
	rg_modernShadowPlannerStats.throttleHistoryMaxMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissBudgetClass = -1;
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, features.scenePackets ? "initialized" : "unavailable" );
}

void R_ModernShadowPlanner_Shutdown( void ) {
	rg_modernShadowPlannerDescriptors.Clear();
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	memset( &rg_modernShadowPlannerCaps, 0, sizeof( rg_modernShadowPlannerCaps ) );
	memset( &rg_modernShadowPlannerFeatures, 0, sizeof( rg_modernShadowPlannerFeatures ) );
	R_ModernShadowPlanner_ResetFairnessHistory();
	rg_modernShadowPlannerInitialized = false;
	rg_modernShadowPlannerStats.throttleHistoryMaxMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissBudgetClass = -1;
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, "off" );
}

void R_ModernShadowPlanner_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested ) {
	const bool initialized = rg_modernShadowPlannerInitialized;
	const bool available = initialized && rg_modernShadowPlannerFeatures.scenePackets;
	rg_modernShadowPlannerDescriptors.SetNum( 0, false );
	rg_modernShadowPlannerDescriptorHash.Clear();
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	rg_modernShadowPlannerStats.initialized = initialized;
	rg_modernShadowPlannerStats.available = available;
	rg_modernShadowPlannerStats.requested = requested;
	rg_modernShadowPlannerStats.throttleHistoryMaxMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissLightDefIndex = -1;
	rg_modernShadowPlannerStats.throttleHistoryLastMissBudgetClass = -1;
	rg_modernShadowPlannerStats.shadowsEnabled = r_shadows.GetBool();
	rg_modernShadowPlannerStats.shadowMapsEnabled = r_useShadowMap.GetBool();
	rg_modernShadowPlannerStats.csmRequested = r_shadowMapCSM.GetBool();
	rg_modernShadowPlannerStats.projectedCsmRequested = r_shadowMapProjectedCSM.GetBool();
	rg_modernShadowPlannerStats.translucentRequested = r_shadowMapTranslucentMoments.GetBool();
	rg_modernShadowPlannerStats.translucentEnabled = R_ModernShadowPlanner_TranslucentMomentsAvailable();
	rg_modernShadowPlannerStats.debugOverlayRequested = r_shadowMapDebugOverlay.GetInteger() > 0;
	rg_modernShadowPlannerStats.reportRequested = r_shadowMapReport.GetInteger() > 0;
	rg_modernShadowPlannerStats.visibilityCasterCullingReady = r_rendererOcclusion.GetBool();
	rg_modernShadowPlannerStats.visibilityNoQueryStall = true;
	rg_modernShadowPlannerStats.modernReceiverSamplingReady = R_ModernShadowPlanner_ModernReceiverSamplingAvailable();
	const int sceneCount = packetFrame.NumScenes();
	rg_modernShadowPlannerStats.sceneCount = sceneCount;
	rg_modernShadowPlannerStats.shadowMapSize = R_ModernShadowPlanner_BudgetedShadowMapSize();
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	const modernShadowPlannerBudget_t shadowBudget = R_ModernShadowPlanner_BuildBudget( budget, rg_modernShadowPlannerStats.shadowMapSize );
	R_ModernShadowPlanner_StoreBudgetInStats( shadowBudget, rg_modernShadowPlannerStats );
	rg_modernShadowPlannerStats.updateModulo = Max( 1, budget.shadowUpdateRate );
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, requested ? "unavailable" : "off" );
	if ( !requested ) {
		R_ModernShadowPlanner_RecordRendererMetrics( rg_modernShadowPlannerStats );
		return;
	}
	if ( !available ) {
		R_ModernShadowPlanner_RecordRendererMetrics( rg_modernShadowPlannerStats );
		return;
	}
	rg_modernShadowPlannerFairnessEpoch++;
	if ( rg_modernShadowPlannerFairnessEpoch <= 0 ) {
		R_ModernShadowPlanner_ResetFairnessHistory();
		rg_modernShadowPlannerFairnessEpoch = 1;
	}

	const int startMsec = Sys_Milliseconds();
	for ( int sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( scene.viewDef == NULL ) {
			continue;
		}
		for ( const viewLight_t *vLight = scene.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
			rg_modernShadowPlannerStats.viewLightCount++;
			if ( rg_modernShadowPlannerDescriptors.Num() >= MODERN_SHADOW_PLAN_MAX_LIGHTS ) {
				rg_modernShadowPlannerStats.overflow = true;
				break;
			}
			// Build the descriptor in place. idList::Alloc grows by granularity
			// and returns a reference to the new slot, avoiding the ~1.4KB stack
			// temporary and the whole-struct Append copy (InitDescriptor memsets
			// the slot first, so a recycled slot cannot leak stale bytes).
			const int descriptorIndex = rg_modernShadowPlannerDescriptors.Num();
			modernShadowLightDescriptor_t &descriptor = rg_modernShadowPlannerDescriptors.Alloc();
			R_ModernShadowPlanner_InitDescriptor(
				descriptor,
				vLight,
				scene.viewDef,
				sceneIndex,
				descriptorIndex,
				rg_modernShadowPlannerStats.shadowMapSize,
				rg_modernShadowPlannerStats.translucentEnabled );
		}
		if ( rg_modernShadowPlannerStats.overflow ) {
			break;
		}
	}

	// Index the finalized descriptor list by viewLight pointer for the O(1)
	// DescriptorForLight lookups the hot draw paths make; the slots are appended
	// once above and never reordered afterward.
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		rg_modernShadowPlannerDescriptorHash.Add(
			R_ModernShadowPlanner_DescriptorKey( rg_modernShadowPlannerDescriptors[i].viewLight ), i );
	}

	R_ModernShadowPlanner_SelectMappedLights( rg_modernShadowPlannerStats );
	R_ModernShadowPlanner_ValidatePreparedDescriptors();
	R_ModernShadowPlanner_UpdateFairnessHistory( rg_modernShadowPlannerStats );
	rg_modernShadowPlannerStats.buildMsec = Sys_Milliseconds() - startMsec;
	R_ModernShadowPlanner_FinalizeStats( rg_modernShadowPlannerStats );
	R_ModernShadowPlanner_RecordRendererMetrics( rg_modernShadowPlannerStats );

	if ( ( r_rendererMetrics.GetInteger() >= 2 || rg_modernShadowPlannerStats.reportRequested ) && requested ) {
		common->Printf(
			"modernShadowPlan status=%s requested=%d valid=%d scenes=%d lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d types(projected=%d ordinary=%d point=%d parallel=%d csm=%d cascades=%d projectedCSM(eligible=%d cascaded=%d suppressed=%d nonProjected=%d)) casters(local=%d global=%d translucent=%d visibility=%d/%d saved=%d receiverCull=%d noQueryStall=%d lod(tests=%d rejected=%d alpha=%d translucent=%d lights=%d budget=%d unmapped=%d)) receivers(local=%d global=%d translucent=%d guarded=%d sampling=%d blocked=%d cutout=%d) budget(lights=%d class=%d/%d/%d atlasTiles=%d/%d classTiles=%d/%d/%d quotas=%d/%d/%d pixels=%d size=%d update=%d used=%d throttled=%d classThrottle=%d/%d/%d reasonMask=0x%02x reasons(class=%d/%d/%d global=%d/%d/%d) miss(fallback=%d stencil=%d cache=%d skipped=%d)) fairness(tracked=%d aged=%d boosted=%d mapped=%d maxAge=%d maxBoost=%d) throttleHistory(tracked=%d repeated=%d recovered=%d maxStreak=%d maxTotal=%d maxLight=%d lastMiss=%d lastReason=0x%02x lastClass=%d) cvars(shadows=%d shadowMap=%d csm=%d projectedCSM=%d translucent=%d/%d debug=%d report=%d) fallbacks(texture=%d cubemap=%d noReceivers=%d invariants=%d mask=0x%08x) build=%dms\n",
			rg_modernShadowPlannerStats.status,
			rg_modernShadowPlannerStats.requested ? 1 : 0,
			rg_modernShadowPlannerStats.frameValid ? 1 : 0,
			rg_modernShadowPlannerStats.sceneCount,
			rg_modernShadowPlannerStats.viewLightCount,
			rg_modernShadowPlannerStats.descriptorCount,
			rg_modernShadowPlannerStats.mappedLights,
			rg_modernShadowPlannerStats.fallbackLights,
			rg_modernShadowPlannerStats.skippedLights,
			rg_modernShadowPlannerStats.projectedLights,
			rg_modernShadowPlannerStats.ordinaryProjectedLights,
			rg_modernShadowPlannerStats.pointLights,
			rg_modernShadowPlannerStats.parallelLights,
			rg_modernShadowPlannerStats.cascadeLights,
			rg_modernShadowPlannerStats.cascadeCount,
			rg_modernShadowPlannerStats.projectedCsmEligibleLights,
			rg_modernShadowPlannerStats.projectedCsmCascadeLights,
			rg_modernShadowPlannerStats.projectedCsmSuppressedLights,
			rg_modernShadowPlannerStats.nonProjectedCsmLights,
			rg_modernShadowPlannerStats.localCasterCount,
			rg_modernShadowPlannerStats.globalCasterCount,
			rg_modernShadowPlannerStats.translucentCasterCount,
			rg_modernShadowPlannerStats.visibilityCasterTests,
			rg_modernShadowPlannerStats.visibilityCasterRejected,
			rg_modernShadowPlannerStats.visibilityCasterSavedDraws,
			rg_modernShadowPlannerStats.visibilityReceiverScissorCulledLights,
			rg_modernShadowPlannerStats.visibilityNoQueryStall ? 1 : 0,
			rg_modernShadowPlannerStats.lodCasterTestCount,
			rg_modernShadowPlannerStats.lodCasterRejectedCount,
			rg_modernShadowPlannerStats.lodAlphaCasterRejectedCount,
			rg_modernShadowPlannerStats.lodTranslucentCasterRejectedCount,
			rg_modernShadowPlannerStats.lodRejectedLights,
			rg_modernShadowPlannerStats.lodRejectedBudgetThrottledLights,
			rg_modernShadowPlannerStats.lodRejectedUnmappedLights,
			rg_modernShadowPlannerStats.localReceiverCount,
			rg_modernShadowPlannerStats.globalReceiverCount,
			rg_modernShadowPlannerStats.translucentReceiverCount,
			rg_modernShadowPlannerStats.receiverGuardedLights,
			rg_modernShadowPlannerStats.modernReceiverSamplingReady ? 1 : 0,
			rg_modernShadowPlannerStats.receiverSamplingBlockedLights,
			rg_modernShadowPlannerStats.cutoutCasterLights,
			rg_modernShadowPlannerStats.maxMappedLights,
			rg_modernShadowPlannerStats.singleProjectedLightQuota,
			rg_modernShadowPlannerStats.cascadeLightQuota,
			rg_modernShadowPlannerStats.pointLightQuota,
			rg_modernShadowPlannerStats.atlasTiles,
			rg_modernShadowPlannerStats.maxAtlasTiles,
			rg_modernShadowPlannerStats.singleProjectedAtlasTiles,
			rg_modernShadowPlannerStats.cascadeAtlasTiles,
			rg_modernShadowPlannerStats.pointAtlasTiles,
			rg_modernShadowPlannerStats.singleProjectedTileQuota,
			rg_modernShadowPlannerStats.cascadeTileQuota,
			rg_modernShadowPlannerStats.pointTileQuota,
			rg_modernShadowPlannerStats.budgetedPixels,
			rg_modernShadowPlannerStats.shadowMapSize,
			rg_modernShadowPlannerStats.updateModulo,
			rg_modernShadowPlannerStats.estimatedPixels,
			rg_modernShadowPlannerStats.budgetThrottledLights,
			rg_modernShadowPlannerStats.singleProjectedBudgetThrottledLights,
			rg_modernShadowPlannerStats.cascadeBudgetThrottledLights,
			rg_modernShadowPlannerStats.pointBudgetThrottledLights,
			rg_modernShadowPlannerStats.budgetThrottleReasonMask,
			rg_modernShadowPlannerStats.budgetThrottleClassLightLights,
			rg_modernShadowPlannerStats.budgetThrottleClassTileLights,
			rg_modernShadowPlannerStats.budgetThrottleClassPixelLights,
			rg_modernShadowPlannerStats.budgetThrottleGlobalLightLights,
			rg_modernShadowPlannerStats.budgetThrottleGlobalTileLights,
			rg_modernShadowPlannerStats.budgetThrottleGlobalPixelLights,
			rg_modernShadowPlannerStats.budgetFallbackLights,
			rg_modernShadowPlannerStats.budgetStencilFallbackLights,
			rg_modernShadowPlannerStats.budgetCacheReuseCandidateLights,
			rg_modernShadowPlannerStats.budgetSkippedLights,
			rg_modernShadowPlannerStats.fairnessTrackedLights,
			rg_modernShadowPlannerStats.fairnessAgedLights,
			rg_modernShadowPlannerStats.fairnessBoostedLights,
			rg_modernShadowPlannerStats.fairnessAgedMappedLights,
			rg_modernShadowPlannerStats.fairnessMaxAge,
			rg_modernShadowPlannerStats.fairnessMaxBoost,
			rg_modernShadowPlannerStats.throttleHistoryTrackedLights,
			rg_modernShadowPlannerStats.throttleHistoryRepeatedBudgetMissLights,
			rg_modernShadowPlannerStats.throttleHistoryRecoveredLights,
			rg_modernShadowPlannerStats.throttleHistoryMaxMissStreak,
			rg_modernShadowPlannerStats.throttleHistoryMaxTotalMisses,
			rg_modernShadowPlannerStats.throttleHistoryMaxMissLightDefIndex,
			rg_modernShadowPlannerStats.throttleHistoryLastMissLightDefIndex,
			rg_modernShadowPlannerStats.throttleHistoryLastMissReasonMask,
			rg_modernShadowPlannerStats.throttleHistoryLastMissBudgetClass,
			rg_modernShadowPlannerStats.shadowsEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.shadowMapsEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.csmRequested ? 1 : 0,
			rg_modernShadowPlannerStats.projectedCsmRequested ? 1 : 0,
			rg_modernShadowPlannerStats.translucentRequested ? 1 : 0,
			rg_modernShadowPlannerStats.translucentEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.debugOverlayRequested ? 1 : 0,
			rg_modernShadowPlannerStats.reportRequested ? 1 : 0,
			rg_modernShadowPlannerStats.textureLimitFallbacks,
			rg_modernShadowPlannerStats.cubemapFallbacks,
			rg_modernShadowPlannerStats.noReceiverLights,
			rg_modernShadowPlannerStats.descriptorInvariantFailures,
			rg_modernShadowPlannerStats.descriptorInvariantFailureMask,
			rg_modernShadowPlannerStats.buildMsec );
		R_ModernShadowPlanner_PrintArb2CacheStats( rg_modernShadowPlannerStats, "modernShadowPlan" );
	}
}

const modernShadowPlannerStats_t &R_ModernShadowPlanner_Stats( void ) {
	return rg_modernShadowPlannerStats;
}

const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorForLight( const viewLight_t *viewLight ) {
	if ( viewLight == NULL ) {
		return NULL;
	}
	// The hash is rebuilt in lockstep with the descriptor list each PrepareFrame,
	// so a lookup here is exact; the index guard is defensive only.
	const int key = R_ModernShadowPlanner_DescriptorKey( viewLight );
	for ( int i = rg_modernShadowPlannerDescriptorHash.First( key ); i != -1;
			i = rg_modernShadowPlannerDescriptorHash.Next( i ) ) {
		if ( i < rg_modernShadowPlannerDescriptors.Num()
				&& rg_modernShadowPlannerDescriptors[i].viewLight == viewLight ) {
			return &rg_modernShadowPlannerDescriptors[i];
		}
	}
	return NULL;
}

const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorByIndex( int index ) {
	if ( index < 0 || index >= rg_modernShadowPlannerDescriptors.Num() ) {
		return NULL;
	}
	return &rg_modernShadowPlannerDescriptors[index];
}

int R_ModernShadowPlanner_NumDescriptors( void ) {
	return rg_modernShadowPlannerDescriptors.Num();
}

void R_ModernShadowPlanner_PrintGfxInfo( void ) {
	common->Printf(
		"Modern shadow plan: %s, requested=%d valid=%d scenes=%d lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d projected=%d ordinary=%d point=%d csm=%d/%d projectedCSM=%d/%d/%d/%d casters(local=%d global=%d translucent=%d visibility=%d/%d saved=%d receiverCull=%d noQueryStall=%d lod(tests=%d rejected=%d alpha=%d translucent=%d lights=%d budget=%d unmapped=%d)) receivers(local=%d global=%d translucent=%d guarded=%d sampling=%d blocked=%d cutout=%d) budget(lights=%d class=%d/%d/%d atlasTiles=%d/%d classTiles=%d/%d/%d quotas=%d/%d/%d pixels=%d size=%d update=%d used=%d throttled=%d classThrottle=%d/%d/%d reasonMask=0x%02x reasons(class=%d/%d/%d global=%d/%d/%d) miss(fallback=%d stencil=%d cache=%d skipped=%d)) fairness(tracked=%d aged=%d boosted=%d mapped=%d maxAge=%d maxBoost=%d) throttleHistory(tracked=%d repeated=%d recovered=%d maxStreak=%d maxTotal=%d maxLight=%d lastMiss=%d lastReason=0x%02x lastClass=%d) cvars(shadows=%d shadowMap=%d csm=%d projectedCSM=%d translucent=%d/%d debug=%d report=%d) invariants=%d mask=0x%08x build=%dms\n",
		rg_modernShadowPlannerStats.available ? "available" : "unavailable",
		rg_modernShadowPlannerStats.requested ? 1 : 0,
		rg_modernShadowPlannerStats.frameValid ? 1 : 0,
		rg_modernShadowPlannerStats.sceneCount,
		rg_modernShadowPlannerStats.viewLightCount,
		rg_modernShadowPlannerStats.descriptorCount,
		rg_modernShadowPlannerStats.mappedLights,
		rg_modernShadowPlannerStats.fallbackLights,
		rg_modernShadowPlannerStats.skippedLights,
		rg_modernShadowPlannerStats.projectedLights,
		rg_modernShadowPlannerStats.ordinaryProjectedLights,
		rg_modernShadowPlannerStats.pointLights,
		rg_modernShadowPlannerStats.cascadeLights,
		rg_modernShadowPlannerStats.cascadeCount,
		rg_modernShadowPlannerStats.projectedCsmEligibleLights,
		rg_modernShadowPlannerStats.projectedCsmCascadeLights,
		rg_modernShadowPlannerStats.projectedCsmSuppressedLights,
		rg_modernShadowPlannerStats.nonProjectedCsmLights,
		rg_modernShadowPlannerStats.localCasterCount,
		rg_modernShadowPlannerStats.globalCasterCount,
		rg_modernShadowPlannerStats.translucentCasterCount,
		rg_modernShadowPlannerStats.visibilityCasterTests,
		rg_modernShadowPlannerStats.visibilityCasterRejected,
		rg_modernShadowPlannerStats.visibilityCasterSavedDraws,
		rg_modernShadowPlannerStats.visibilityReceiverScissorCulledLights,
		rg_modernShadowPlannerStats.visibilityNoQueryStall ? 1 : 0,
		rg_modernShadowPlannerStats.lodCasterTestCount,
		rg_modernShadowPlannerStats.lodCasterRejectedCount,
		rg_modernShadowPlannerStats.lodAlphaCasterRejectedCount,
		rg_modernShadowPlannerStats.lodTranslucentCasterRejectedCount,
		rg_modernShadowPlannerStats.lodRejectedLights,
		rg_modernShadowPlannerStats.lodRejectedBudgetThrottledLights,
		rg_modernShadowPlannerStats.lodRejectedUnmappedLights,
		rg_modernShadowPlannerStats.localReceiverCount,
		rg_modernShadowPlannerStats.globalReceiverCount,
		rg_modernShadowPlannerStats.translucentReceiverCount,
		rg_modernShadowPlannerStats.receiverGuardedLights,
		rg_modernShadowPlannerStats.modernReceiverSamplingReady ? 1 : 0,
		rg_modernShadowPlannerStats.receiverSamplingBlockedLights,
		rg_modernShadowPlannerStats.cutoutCasterLights,
		rg_modernShadowPlannerStats.maxMappedLights,
		rg_modernShadowPlannerStats.singleProjectedLightQuota,
		rg_modernShadowPlannerStats.cascadeLightQuota,
		rg_modernShadowPlannerStats.pointLightQuota,
		rg_modernShadowPlannerStats.atlasTiles,
		rg_modernShadowPlannerStats.maxAtlasTiles,
		rg_modernShadowPlannerStats.singleProjectedAtlasTiles,
		rg_modernShadowPlannerStats.cascadeAtlasTiles,
		rg_modernShadowPlannerStats.pointAtlasTiles,
		rg_modernShadowPlannerStats.singleProjectedTileQuota,
		rg_modernShadowPlannerStats.cascadeTileQuota,
		rg_modernShadowPlannerStats.pointTileQuota,
		rg_modernShadowPlannerStats.budgetedPixels,
		rg_modernShadowPlannerStats.shadowMapSize,
		rg_modernShadowPlannerStats.updateModulo,
		rg_modernShadowPlannerStats.estimatedPixels,
		rg_modernShadowPlannerStats.budgetThrottledLights,
		rg_modernShadowPlannerStats.singleProjectedBudgetThrottledLights,
		rg_modernShadowPlannerStats.cascadeBudgetThrottledLights,
		rg_modernShadowPlannerStats.pointBudgetThrottledLights,
		rg_modernShadowPlannerStats.budgetThrottleReasonMask,
		rg_modernShadowPlannerStats.budgetThrottleClassLightLights,
		rg_modernShadowPlannerStats.budgetThrottleClassTileLights,
		rg_modernShadowPlannerStats.budgetThrottleClassPixelLights,
		rg_modernShadowPlannerStats.budgetThrottleGlobalLightLights,
		rg_modernShadowPlannerStats.budgetThrottleGlobalTileLights,
		rg_modernShadowPlannerStats.budgetThrottleGlobalPixelLights,
		rg_modernShadowPlannerStats.budgetFallbackLights,
		rg_modernShadowPlannerStats.budgetStencilFallbackLights,
		rg_modernShadowPlannerStats.budgetCacheReuseCandidateLights,
		rg_modernShadowPlannerStats.budgetSkippedLights,
		rg_modernShadowPlannerStats.fairnessTrackedLights,
		rg_modernShadowPlannerStats.fairnessAgedLights,
		rg_modernShadowPlannerStats.fairnessBoostedLights,
		rg_modernShadowPlannerStats.fairnessAgedMappedLights,
		rg_modernShadowPlannerStats.fairnessMaxAge,
		rg_modernShadowPlannerStats.fairnessMaxBoost,
		rg_modernShadowPlannerStats.throttleHistoryTrackedLights,
		rg_modernShadowPlannerStats.throttleHistoryRepeatedBudgetMissLights,
		rg_modernShadowPlannerStats.throttleHistoryRecoveredLights,
		rg_modernShadowPlannerStats.throttleHistoryMaxMissStreak,
		rg_modernShadowPlannerStats.throttleHistoryMaxTotalMisses,
		rg_modernShadowPlannerStats.throttleHistoryMaxMissLightDefIndex,
		rg_modernShadowPlannerStats.throttleHistoryLastMissLightDefIndex,
		rg_modernShadowPlannerStats.throttleHistoryLastMissReasonMask,
		rg_modernShadowPlannerStats.throttleHistoryLastMissBudgetClass,
		rg_modernShadowPlannerStats.shadowsEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.shadowMapsEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.csmRequested ? 1 : 0,
		rg_modernShadowPlannerStats.projectedCsmRequested ? 1 : 0,
		rg_modernShadowPlannerStats.translucentRequested ? 1 : 0,
		rg_modernShadowPlannerStats.translucentEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.debugOverlayRequested ? 1 : 0,
		rg_modernShadowPlannerStats.reportRequested ? 1 : 0,
		rg_modernShadowPlannerStats.descriptorInvariantFailures,
		rg_modernShadowPlannerStats.descriptorInvariantFailureMask,
		rg_modernShadowPlannerStats.buildMsec );
	R_ModernShadowPlanner_PrintArb2CacheStats( rg_modernShadowPlannerStats, "Modern shadow plan" );
}

static modernShadowMapType_t R_ModernShadowPlanner_Arb2ParityMapType( const shadowMapArb2ParityState_t &arb2State ) {
	if ( arb2State.pointLight ) {
		return MODERN_SHADOW_MAP_POINT;
	}
	return arb2State.cascadeCount > 1 ? MODERN_SHADOW_MAP_CASCADE : MODERN_SHADOW_MAP_PROJECTED;
}

static bool R_ModernShadowPlanner_Arb2ParityFloatMatches( const char *label, const char *field, const int index, const float descriptorValue, const float arb2Value ) {
	const float epsilon = 0.001f;
	if ( idMath::Fabs( descriptorValue - arb2Value ) <= epsilon ) {
		return true;
	}
	common->Printf(
		"RendererShadowPlanner self-test failed: ARB2 parity %s mismatch %s[%d] descriptor=%.6f arb2=%.6f\n",
		label != NULL ? label : "<unknown>",
		field != NULL ? field : "<field>",
		index,
		descriptorValue,
		arb2Value );
	return false;
}

static bool R_ModernShadowPlanner_Arb2ParityIntMatches( const char *label, const char *field, const int descriptorValue, const int arb2Value ) {
	if ( descriptorValue == arb2Value ) {
		return true;
	}
	common->Printf(
		"RendererShadowPlanner self-test failed: ARB2 parity %s mismatch %s descriptor=%d arb2=%d\n",
		label != NULL ? label : "<unknown>",
		field != NULL ? field : "<field>",
		descriptorValue,
		arb2Value );
	return false;
}

static void R_ModernShadowPlanner_ProjectedStateSampleSummary( const shadowMapProjectedLightState_t &state, int &sampleCount, int &validCount, int &skippedCount, int &positiveWCount, int &negativeWCount, int &nearZeroWCount, int &nanWCount, int &invalidNdcCount, int &mixedWSignCascades ) {
	sampleCount = 0;
	validCount = 0;
	skippedCount = 0;
	positiveWCount = 0;
	negativeWCount = 0;
	nearZeroWCount = 0;
	nanWCount = 0;
	invalidNdcCount = 0;
	mixedWSignCascades = 0;

	for ( int cascadeIndex = 0; cascadeIndex < SHADOWMAP_PROJECTED_MAX_CASCADES; ++cascadeIndex ) {
		const shadowMapProjectedCascadeFit_t &fit = state.cascadeFit[cascadeIndex];
		if ( !fit.attempted ) {
			continue;
		}
		sampleCount += fit.sampleCount;
		validCount += fit.validPoints;
		skippedCount += fit.skippedPoints;
		positiveWCount += fit.positiveWPoints;
		negativeWCount += fit.negativeWPoints;
		nearZeroWCount += fit.nearZeroWPoints;
		nanWCount += fit.nanWPoints;
		invalidNdcCount += fit.invalidNdcPoints;
		mixedWSignCascades += fit.mixedWSigns ? 1 : 0;
	}
}

static bool R_ModernShadowPlanner_Arb2ParityProjectedStateMatches( const char *label, const modernShadowLightDescriptor_t &descriptor, const shadowMapArb2ParityState_t &arb2State ) {
	if ( descriptor.projectedCascadeFallback != arb2State.projectedCascadeFallback || descriptor.projectedFallbackCascade != arb2State.projectedFallbackCascade ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: ARB2 parity %s projected fallback mismatch descriptor=%d/%d arb2=%d/%d\n",
			label != NULL ? label : "<unknown>",
			descriptor.projectedCascadeFallback ? 1 : 0,
			descriptor.projectedFallbackCascade,
			arb2State.projectedCascadeFallback ? 1 : 0,
			arb2State.projectedFallbackCascade );
		return false;
	}
	if ( !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedFallbackReason", descriptor.projectedFallbackReason, arb2State.projectedState.fallbackReason ) ) {
		return false;
	}

	int arb2SampleCount;
	int arb2ValidCount;
	int arb2SkippedCount;
	int arb2PositiveWCount;
	int arb2NegativeWCount;
	int arb2NearZeroWCount;
	int arb2NanWCount;
	int arb2InvalidNdcCount;
	int arb2MixedWSignCascades;
	R_ModernShadowPlanner_ProjectedStateSampleSummary(
		arb2State.projectedState,
		arb2SampleCount,
		arb2ValidCount,
		arb2SkippedCount,
		arb2PositiveWCount,
		arb2NegativeWCount,
		arb2NearZeroWCount,
		arb2NanWCount,
		arb2InvalidNdcCount,
		arb2MixedWSignCascades );
	if ( !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedSampleCount", descriptor.projectedSampleCount, arb2SampleCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedValidSampleCount", descriptor.projectedValidSampleCount, arb2ValidCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedSkippedSampleCount", descriptor.projectedSkippedSampleCount, arb2SkippedCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedPositiveWPoints", descriptor.projectedPositiveWPoints, arb2PositiveWCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedNegativeWPoints", descriptor.projectedNegativeWPoints, arb2NegativeWCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedNearZeroWPoints", descriptor.projectedNearZeroWPoints, arb2NearZeroWCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedNanWPoints", descriptor.projectedNanWPoints, arb2NanWCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedInvalidNdcPoints", descriptor.projectedInvalidNdcPoints, arb2InvalidNdcCount )
		|| !R_ModernShadowPlanner_Arb2ParityIntMatches( label, "projectedMixedWSignCascades", descriptor.projectedMixedWSignCascades, arb2MixedWSignCascades ) ) {
		return false;
	}

	float arb2ShadowMatrix[16];
	R_ShadowMapClipPlanesToGLMatrix( arb2State.projectedState.clipPlanes[0], arb2ShadowMatrix );
	if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "projectionPad", 0, descriptor.projectionPad, arb2State.projectedState.projectionPad ) ) {
		return false;
	}
	if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "projectionScale", 0, descriptor.projectionScale, arb2State.projectedState.projectionScale ) ) {
		return false;
	}
	for ( int matrixIndex = 0; matrixIndex < 16; ++matrixIndex ) {
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "shadowMatrix", matrixIndex, descriptor.shadowMatrix[matrixIndex], arb2ShadowMatrix[matrixIndex] ) ) {
			return false;
		}
	}
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "projectedBaseClipPlanes", ( planeIndex * 4 ) + componentIndex, descriptor.projectedBaseClipPlanes[planeIndex][componentIndex], arb2State.projectedState.baseClipPlanes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}

	const int cascadeCount = idMath::ClampInt( 1, MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES, descriptor.cascadeCount );
	for ( int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex ) {
		for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
			if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "projectedAtlasRect", cascadeIndex * 4 + componentIndex, descriptor.projectedAtlasRect[cascadeIndex][componentIndex], arb2State.projectedState.atlasRect[cascadeIndex][componentIndex] ) ) {
				return false;
			}
		}
		for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
			for ( int componentIndex = 0; componentIndex < 4; ++componentIndex ) {
				if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "projectedClipPlanes", ( cascadeIndex * 16 ) + ( planeIndex * 4 ) + componentIndex, descriptor.projectedClipPlanes[cascadeIndex][planeIndex][componentIndex], arb2State.projectedState.clipPlanes[cascadeIndex][planeIndex][componentIndex] ) ) {
					return false;
				}
			}
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "cascadeSplitDepths", cascadeIndex, descriptor.cascadeSplitDepths[cascadeIndex], arb2State.projectedState.splitDepths[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "cascadeBiasScale", cascadeIndex, descriptor.cascadeBiasScale[cascadeIndex], arb2State.projectedState.biasScale[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "texelDepthBias", cascadeIndex, descriptor.texelDepthBias[cascadeIndex], arb2State.projectedState.texelDepthBias[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "worldTexelSize", cascadeIndex, descriptor.worldTexelSize[cascadeIndex], arb2State.projectedState.worldTexelSize[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "sliceNear", cascadeIndex, descriptor.sliceNear[cascadeIndex], arb2State.projectedState.sliceNear[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "sliceFar", cascadeIndex, descriptor.sliceFar[cascadeIndex], arb2State.projectedState.sliceFar[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "depthRange", cascadeIndex, descriptor.depthRange[cascadeIndex], arb2State.projectedState.depthRange[cascadeIndex] ) ) {
			return false;
		}
		if ( !R_ModernShadowPlanner_Arb2ParityFloatMatches( label, "clipZExtent", cascadeIndex, descriptor.clipZExtent[cascadeIndex], arb2State.projectedState.clipZExtent[cascadeIndex] ) ) {
			return false;
		}
	}

	return true;
}

static bool R_ModernShadowPlanner_CompareDescriptorToArb2Parity( const char *label, const modernShadowLightDescriptor_t *descriptor, const viewLight_t *vLight, const viewDef_t *viewDef, int &parityChecks ) {
	if ( descriptor == NULL ) {
		common->Printf( "RendererShadowPlanner self-test failed: ARB2 parity %s has no descriptor\n", label != NULL ? label : "<unknown>" );
		return false;
	}

	shadowMapArb2ParityState_t arb2State;
	if ( !RB_ShadowMapBuildArb2ParityState( vLight, viewDef, descriptor->resolution, arb2State ) || !arb2State.valid ) {
		common->Printf( "RendererShadowPlanner self-test failed: ARB2 parity %s could not build reference state\n", label != NULL ? label : "<unknown>" );
		return false;
	}

	const modernShadowMapType_t expectedMapType = R_ModernShadowPlanner_Arb2ParityMapType( arb2State );
	if ( descriptor->mapType != expectedMapType || descriptor->pointLight != arb2State.pointLight || descriptor->projectedStateReady != arb2State.projectedStateReady || descriptor->requestedCascadeCount != arb2State.requestedCascadeCount || descriptor->cascadeCount != arb2State.cascadeCount || descriptor->tileCount != arb2State.tileCount || descriptor->atlasDiv != arb2State.atlasDiv || descriptor->resolution != arb2State.tileSize ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: ARB2 parity %s descriptor(map=%s point=%d ready=%d requested=%d cascades=%d tiles=%d atlasDiv=%d size=%d) arb2(map=%s point=%d ready=%d requested=%d cascades=%d tiles=%d atlasDiv=%d size=%d)\n",
			label != NULL ? label : "<unknown>",
			ModernShadowMapType_Name( descriptor->mapType ),
			descriptor->pointLight ? 1 : 0,
			descriptor->projectedStateReady ? 1 : 0,
			descriptor->requestedCascadeCount,
			descriptor->cascadeCount,
			descriptor->tileCount,
			descriptor->atlasDiv,
			descriptor->resolution,
			ModernShadowMapType_Name( expectedMapType ),
			arb2State.pointLight ? 1 : 0,
			arb2State.projectedStateReady ? 1 : 0,
			arb2State.requestedCascadeCount,
			arb2State.cascadeCount,
			arb2State.tileCount,
			arb2State.atlasDiv,
			arb2State.tileSize );
		return false;
	}

	if ( !descriptor->pointLight && !R_ModernShadowPlanner_Arb2ParityProjectedStateMatches( label, *descriptor, arb2State ) ) {
		return false;
	}

	parityChecks++;
	return true;
}

// Resolves a light material that is guaranteed to cast shadows. Assetless
// runs default light materials to a translucent (implicitly noShadows)
// stand-in, which used to collapse the entire regression net into a trivial
// "all lights skipped" pass (M5). When the on-disk material cannot cast
// shadows, a minimal forceShadows material is synthesized so the mapped
// path is always exercised. The synthetic decl is allocated OUTSIDE the
// decl manager's file registry (the idMaterial::Validate pattern): a
// registered phantom file would make reloadDecls FatalError on the missing
// file, and a registry decl would be purged back to the noShadows default
// text on every level load. The one-time allocation deliberately lives for
// the process lifetime.
const idMaterial *R_ModernShadowPlanner_SelfTestLightShader( const char *materialName ) {
	if ( declManager == NULL ) {
		return tr.defaultMaterial;
	}
	const idMaterial *material = declManager->FindMaterial( materialName );
	if ( material != NULL && material->LightCastsShadows() ) {
		return material;
	}
	static idDecl *syntheticDecl = NULL;
	if ( syntheticDecl == NULL ) {
		syntheticDecl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( syntheticDecl != NULL ) {
			const char *body = "{\n\tforceShadows\n\t{\n\t\tmap _white\n\t}\n}\n";
			syntheticDecl->Parse( body, static_cast<int>( strlen( body ) ), false );
		}
	}
	const idMaterial *synthetic = syntheticDecl != NULL ? static_cast<const idMaterial *>( syntheticDecl ) : NULL;
	if ( synthetic != NULL && synthetic->LightCastsShadows() ) {
		return synthetic;
	}
	return material != NULL ? material : tr.defaultMaterial;
}

bool RendererShadowPlanner_RunSelfTest( void ) {
	if ( !rg_modernShadowPlannerInitialized || !rg_modernShadowPlannerFeatures.scenePackets ) {
		common->Printf( "RendererShadowPlanner self-test passed (planner unavailable)\n" );
		return true;
	}

	// PrepareFrame below fills the global descriptor table with viewLight
	// pointers into this stack frame and synthetic stats; re-prepare against an
	// empty frame on every exit path so neither outlives the test. Constructed
	// before the cvar restores so it runs after them and records user values.
	struct rendererShadowPlannerSelfTestStateReset_t {
		~rendererShadowPlannerSelfTestStateReset_t() {
			idScenePacketFrame emptyFrame;
			emptyFrame.Clear();
			R_ModernShadowPlanner_PrepareFrame( emptyFrame, false );
			R_ModernShadowPlanner_ResetFairnessHistory();
			R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, "selftest-complete" );
		}
	} plannerStateReset;
	R_ModernShadowPlanner_ResetFairnessHistory();

	struct rendererShadowPlannerBoolCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererShadowPlannerBoolCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererShadowPlannerBoolCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	struct rendererShadowPlannerIntCVarRestore_t {
		idCVar &cvar;
		int oldValue;
		rendererShadowPlannerIntCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetInteger() ) {}
		~rendererShadowPlannerIntCVarRestore_t() { cvar.SetInteger( oldValue ); }
	};
	struct rendererShadowPlannerFloatCVarRestore_t {
		idCVar &cvar;
		float oldValue;
		rendererShadowPlannerFloatCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetFloat() ) {}
		~rendererShadowPlannerFloatCVarRestore_t() { cvar.SetFloat( oldValue ); }
	};
	struct rendererShadowPlannerStringCVarRestore_t {
		idCVar &cvar;
		idStr oldValue;
		rendererShadowPlannerStringCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetString() ) {}
		~rendererShadowPlannerStringCVarRestore_t() { cvar.SetString( oldValue.c_str() ); }
	};
	rendererShadowPlannerBoolCVarRestore_t restoreShadows( r_shadows );
	rendererShadowPlannerBoolCVarRestore_t restoreShadowMap( r_useShadowMap );
	rendererShadowPlannerBoolCVarRestore_t restoreCSM( r_shadowMapCSM );
	rendererShadowPlannerBoolCVarRestore_t restoreProjectedCSM( r_shadowMapProjectedCSM );
	rendererShadowPlannerBoolCVarRestore_t restoreTranslucent( r_shadowMapTranslucentMoments );
	rendererShadowPlannerBoolCVarRestore_t restoreStaticCache( r_shadowMapStaticCache );
	rendererShadowPlannerBoolCVarRestore_t restoreCacheCSM( r_shadowMapCacheCSM );
	rendererShadowPlannerIntCVarRestore_t restoreCascadeCount( r_shadowMapCascadeCount );
	rendererShadowPlannerIntCVarRestore_t restoreShadowMapSize( r_shadowMapSize );
	rendererShadowPlannerIntCVarRestore_t restoreProjectedCacheSize( r_shadowMapProjectedCacheSize );
	rendererShadowPlannerIntCVarRestore_t restorePointCacheSize( r_shadowMapPointCacheSize );
	rendererShadowPlannerFloatCVarRestore_t restoreProjectionPad( r_shadowMapProjectionPad );
	rendererShadowPlannerStringCVarRestore_t restoreBenchmarkPreset( r_rendererBenchmarkPreset );
	r_shadows.SetBool( true );
	r_useShadowMap.SetBool( true );
	r_shadowMapCSM.SetBool( true );
	r_shadowMapProjectedCSM.SetBool( true );
	r_shadowMapTranslucentMoments.SetBool( false );
	r_shadowMapStaticCache.SetBool( true );
	r_shadowMapCacheCSM.SetBool( false );
	r_shadowMapCascadeCount.SetInteger( 3 );
	r_shadowMapSize.SetInteger( 1024 );
	r_shadowMapProjectionPad.SetFloat( 0.20f );
	r_shadowMapProjectedCacheSize.SetInteger( 4 );
	r_shadowMapPointCacheSize.SetInteger( 4 );
	r_rendererBenchmarkPreset.SetString( "baseline" );
	if ( !R_ShadowMapCasterAdmissionSelfTest() ) {
		return false;
	}
	if ( !R_ShadowMapLODAdmissionSelfTest() ) {
		return false;
	}
	if ( !RB_ShadowMapArb2ReceiverFallbackSelfTest() ) {
		return false;
	}

	drawSurf_t casterSurfs[6];
	drawSurf_t receiverSurfs[6];
	memset( casterSurfs, 0, sizeof( casterSurfs ) );
	memset( receiverSurfs, 0, sizeof( receiverSurfs ) );
	for ( int i = 0; i < 5; ++i ) {
		casterSurfs[i].nextOnLight = &casterSurfs[i + 1];
		receiverSurfs[i].nextOnLight = &receiverSurfs[i + 1];
	}

	viewLight_t lights[6];
	idRenderLightLocal lightDefs[6];
	memset( lights, 0, sizeof( lights ) );
	// idRenderLightLocal is polymorphic; its constructor clears its fields while preserving its vtable.
	const idMaterial *projectedLightShader = R_ModernShadowPlanner_SelfTestLightShader( "lights/defaultProjectedLight" );
	const idMaterial *pointLightShader = R_ModernShadowPlanner_SelfTestLightShader( "lights/defaultPointLight" );
	if ( projectedLightShader == NULL ) {
		projectedLightShader = tr.defaultMaterial;
	}
	if ( pointLightShader == NULL ) {
		pointLightShader = projectedLightShader;
	}
	srfTriangles_t receiverGeo;
	viewEntity_t receiverSpace;
	float receiverRegisters[1] = { 1.0f };
	memset( &receiverGeo, 0, sizeof( receiverGeo ) );
	memset( &receiverSpace, 0, sizeof( receiverSpace ) );
	receiverGeo.numIndexes = 3;
	for ( int i = 0; i < 6; ++i ) {
		receiverSurfs[i].geo = &receiverGeo;
		receiverSurfs[i].space = &receiverSpace;
		receiverSurfs[i].material = tr.defaultMaterial;
		receiverSurfs[i].shaderRegisters = receiverRegisters;
	}
	for ( int i = 0; i < 6; ++i ) {
		lightDefs[i].index = i + 1;
		lights[i].lightDef = &lightDefs[i];
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 128 + i * 32;
		lights[i].scissorRect.y2 = 128 + i * 16;
		lights[i].viewInsideLight = i == 0;
		lights[i].pointLight = i == 1;
		lights[i].lightShader = lights[i].pointLight ? pointLightShader : projectedLightShader;
		lightDefs[i].lightShader = lights[i].lightShader;
		lights[i].lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.lightRadius = lights[i].lightRadius;
		lights[i].lightProject[0] = idPlane( 0.0f, 0.001f, 0.0f, 0.5f );
		lights[i].lightProject[1] = idPlane( 0.0f, 0.0f, 0.001f, 0.5f );
		lights[i].lightProject[2] = idPlane( 0.0f, 0.0f, 0.0f, 1.0f );
		lights[i].lightProject[3] = idPlane( 0.001f, 0.0f, 0.0f, 0.0f );
		lights[i].parallel = i == 2;
		lights[i].globalShadowMapCasters = &casterSurfs[0];
		lights[i].localShadowMapCasters = &casterSurfs[3];
		lights[i].globalShadows = &casterSurfs[1];
		lights[i].localShadows = &casterSurfs[4];
		lights[i].globalInteractions = &receiverSurfs[0];
		lights[i].localInteractions = &receiverSurfs[3];
		lights[i].shadowMapCasterCount = 6;
		lights[i].shadowMapStaticCasterCount = 6;
		lights[i].shadowMapDynamicCasterCount = 0;
		lights[i].shadowMapTranslucentCasterCount = 0;
		lights[i].shadowMapCasterSignature = 0x100 + i;
		lights[i].next = i + 1 < 6 ? &lights[i + 1] : NULL;
	}
	lights[0].shadowMapLODTestCount = lights[0].shadowMapCasterCount;
	lights[1].shadowMapLODTestCount = lights[1].shadowMapCasterCount + 2;
	lights[1].shadowMapLODRejectedCount = 2;
	lights[1].shadowMapLODAlphaRejectedCount = 1;
	lights[2].shadowMapLODTestCount = lights[2].shadowMapCasterCount + 1;
	lights[2].shadowMapLODRejectedCount = 1;
	lights[2].shadowMapLODTranslucentRejectedCount = 1;
	lightDefs[4].parms.noShadows = true;
	lights[5].globalInteractions = NULL;
	lights[5].localInteractions = NULL;
	lights[5].translucentInteractions = NULL;

	viewDef_t view;
	memset( &view, 0, sizeof( view ) );
	view.renderView.width = 1280;
	view.renderView.height = 720;
	view.renderView.fov_x = 90.0f;
	view.renderView.fov_y = 70.0f;
	view.renderView.vieworg.Zero();
	view.renderView.viewaxis = mat3_identity;
	view.viewport.x1 = 0;
	view.viewport.y1 = 0;
	view.viewport.x2 = 1279;
	view.viewport.y2 = 719;
	view.scissor.x1 = 0;
	view.scissor.y1 = 0;
	view.scissor.x2 = 1279;
	view.scissor.y2 = 719;
	view.viewLights = &lights[0];
	idScenePacketFrame packetFrame;
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererShadowPlanner self-test failed: could not add scene\n" );
		return false;
	}
	packetFrame.FinishScene();
	int arb2ParityChecks = 0;

	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t stats = R_ModernShadowPlanner_Stats();
	// No trivial-pass escape (M5): the synthetic light materials are
	// guaranteed to cast shadows, so an all-skipped frame here is a real
	// regression and must fail loudly, not self-report as coverage.
	if ( !stats.frameValid || stats.viewLightCount != 6 || stats.descriptorCount != 6 || stats.mappedLights <= 0 || stats.skippedLights < 2 || stats.cascadeLights <= 0 || stats.cascadeCount < 3 || stats.shadowMapSize <= 0 || stats.maxMappedLights <= 0 || stats.atlasTiles <= 0 || stats.receiverGuardedLights <= 0 || stats.receiverSamplingBlockedLights <= 0 || stats.descriptorInvariantFailures != 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: valid=%d lights=%d desc=%d mapped=%d fallback=%d skipped=%d csm=%d/%d size=%d budget=%d atlas=%d/%d guarded=%d blocked=%d invariants=%d mask=0x%08x status=%s\n",
			stats.frameValid ? 1 : 0,
			stats.viewLightCount,
			stats.descriptorCount,
			stats.mappedLights,
			stats.fallbackLights,
			stats.skippedLights,
			stats.cascadeLights,
			stats.cascadeCount,
			stats.shadowMapSize,
			stats.maxMappedLights,
			stats.atlasTiles,
			stats.maxAtlasTiles,
			stats.receiverGuardedLights,
			stats.receiverSamplingBlockedLights,
			stats.descriptorInvariantFailures,
			stats.descriptorInvariantFailureMask,
			stats.status );
		return false;
	}
	if ( !stats.projectedCsmRequested || stats.ordinaryProjectedLights <= 0 || stats.projectedCsmEligibleLights <= 0 || stats.projectedCsmCascadeLights <= 0 || stats.projectedCsmSuppressedLights != 0 || stats.nonProjectedCsmLights <= 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected CSM-on counters requested=%d ordinary=%d eligible=%d cascaded=%d suppressed=%d nonProjected=%d csm=%d/%d\n",
			stats.projectedCsmRequested ? 1 : 0,
			stats.ordinaryProjectedLights,
			stats.projectedCsmEligibleLights,
			stats.projectedCsmCascadeLights,
			stats.projectedCsmSuppressedLights,
			stats.nonProjectedCsmLights,
			stats.cascadeLights,
			stats.cascadeCount );
		return false;
	}
	if ( stats.lodCasterTestCount != 21 || stats.lodCasterRejectedCount != 3 || stats.lodAlphaCasterRejectedCount != 1 || stats.lodTranslucentCasterRejectedCount != 1 || stats.lodRejectedLights != 2 || stats.lodRejectedBudgetThrottledLights != 0 || stats.lodRejectedUnmappedLights != 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: LOD counters tests=%d rejected=%d alpha=%d translucent=%d lights=%d budget=%d unmapped=%d\n",
			stats.lodCasterTestCount,
			stats.lodCasterRejectedCount,
			stats.lodAlphaCasterRejectedCount,
			stats.lodTranslucentCasterRejectedCount,
			stats.lodRejectedLights,
			stats.lodRejectedBudgetThrottledLights,
			stats.lodRejectedUnmappedLights );
		return false;
	}
	const modernShadowLightDescriptor_t *firstDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[0] );
	if ( firstDescriptor == NULL || firstDescriptor->descriptorIndex < 0 || firstDescriptor->policy != MODERN_SHADOW_POLICY_MAPPED || !firstDescriptor->atlasTileReady || !firstDescriptor->receiverGuardReady || firstDescriptor->casterCount <= 0 || firstDescriptor->receiverCount <= 0 || firstDescriptor->pcfKernel < 0 ) {
		common->Printf( "RendererShadowPlanner self-test failed: descriptor lookup mismatch\n" );
		return false;
	}
	if ( firstDescriptor->mapType != MODERN_SHADOW_MAP_CASCADE || firstDescriptor->cascadeCount != 3 || firstDescriptor->tileCount != 3 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected CSM enabled classified as %s cascades=%d tiles=%d\n",
			ModernShadowMapType_Name( firstDescriptor->mapType ),
			firstDescriptor->cascadeCount,
			firstDescriptor->tileCount );
		return false;
	}
	const float expectedPlannerProjectionPad = 0.20f;
	const float expectedPlannerProjectionScale = R_ShadowMapProjectionScale( expectedPlannerProjectionPad );
	const float expectedPlannerBaseS1 = ( lights[0].lightProject[0][1] * 2.0f - lights[0].lightProject[2][1] ) * expectedPlannerProjectionScale;
	if ( !firstDescriptor->projectedStateReady || firstDescriptor->requestedCascadeCount != 3 || firstDescriptor->atlasDiv != 2 || firstDescriptor->projectedCascadeFallback || firstDescriptor->projectedFallbackReason != SHADOWMAP_PROJECTED_FALLBACK_NONE || firstDescriptor->projectedSampleCount != SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT * 3 || firstDescriptor->projectedValidSampleCount + firstDescriptor->projectedSkippedSampleCount != firstDescriptor->projectedSampleCount || firstDescriptor->projectedPositiveWPoints <= 0 || firstDescriptor->projectedNegativeWPoints != 0 || firstDescriptor->projectedMixedWSignCascades != 0 || firstDescriptor->projectedAtlasRect[0][2] < 0.49f || firstDescriptor->projectedAtlasRect[0][3] < 0.49f || firstDescriptor->projectedClipPlanes[0][3][3] < 0.5f || firstDescriptor->shadowMatrix[15] < 0.5f || firstDescriptor->cascadeSplitDepths[0] <= 0.0f || firstDescriptor->sliceFar[2] <= firstDescriptor->sliceNear[0] || !R_ModernShadowPlanner_FloatClose( firstDescriptor->projectionPad, expectedPlannerProjectionPad ) || !R_ModernShadowPlanner_FloatClose( firstDescriptor->projectionScale, expectedPlannerProjectionScale ) || !R_ModernShadowPlanner_FloatClose( firstDescriptor->projectedBaseClipPlanes[0][1], expectedPlannerBaseS1 ) || R_ModernShadowPlanner_FloatClose( firstDescriptor->projectedBaseClipPlanes[0][1], lights[0].lightProject[0][1], 0.00001f ) ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected contract ready=%d requested=%d atlasDiv=%d fallback=%d/%s samples=%d/%d/%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d rect=[%.2f %.2f %.2f %.2f] clipW=%.3f matrixW=%.3f split0=%.2f slice=%.2f-%.2f projectionPad=%.3f scale=%.6f baseS1=%.6f expected=%.6f raw=%.6f\n",
			firstDescriptor->projectedStateReady ? 1 : 0,
			firstDescriptor->requestedCascadeCount,
			firstDescriptor->atlasDiv,
			firstDescriptor->projectedCascadeFallback ? 1 : 0,
			R_ShadowMapProjectedFallbackReasonName( firstDescriptor->projectedFallbackReason ),
			firstDescriptor->projectedSampleCount,
			firstDescriptor->projectedValidSampleCount,
			firstDescriptor->projectedSkippedSampleCount,
			firstDescriptor->projectedPositiveWPoints,
			firstDescriptor->projectedNegativeWPoints,
			firstDescriptor->projectedNearZeroWPoints,
			firstDescriptor->projectedNanWPoints,
			firstDescriptor->projectedInvalidNdcPoints,
			firstDescriptor->projectedMixedWSignCascades,
			firstDescriptor->projectedAtlasRect[0][0],
			firstDescriptor->projectedAtlasRect[0][1],
			firstDescriptor->projectedAtlasRect[0][2],
			firstDescriptor->projectedAtlasRect[0][3],
			firstDescriptor->projectedClipPlanes[0][3][3],
			firstDescriptor->shadowMatrix[15],
			firstDescriptor->cascadeSplitDepths[0],
			firstDescriptor->sliceNear[0],
			firstDescriptor->sliceFar[2],
			firstDescriptor->projectionPad,
			firstDescriptor->projectionScale,
			firstDescriptor->projectedBaseClipPlanes[0][1],
			expectedPlannerBaseS1,
			lights[0].lightProject[0][1] );
		return false;
	}
	if ( stats.singleProjectedLightQuota < 2 || stats.cascadeLightQuota < 3 || stats.pointLightQuota < 1 || stats.singleProjectedTileQuota < 2 || stats.cascadeTileQuota < 9 || stats.pointTileQuota < 6 || stats.singleProjectedMappedLights != 0 || stats.cascadeMappedLights != 3 || stats.pointMappedLights != 1 || stats.singleProjectedAtlasTiles != 0 || stats.cascadeAtlasTiles != 9 || stats.pointAtlasTiles != 6 || stats.budgetThrottledLights != 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: CSM-on class budget quotas=%d/%d/%d tiles=%d/%d/%d mapped=%d/%d/%d usedTiles=%d/%d/%d throttled=%d/%d/%d total=%d\n",
			stats.singleProjectedLightQuota,
			stats.cascadeLightQuota,
			stats.pointLightQuota,
			stats.singleProjectedTileQuota,
			stats.cascadeTileQuota,
			stats.pointTileQuota,
			stats.singleProjectedMappedLights,
			stats.cascadeMappedLights,
			stats.pointMappedLights,
			stats.singleProjectedAtlasTiles,
			stats.cascadeAtlasTiles,
			stats.pointAtlasTiles,
			stats.singleProjectedBudgetThrottledLights,
			stats.cascadeBudgetThrottledLights,
			stats.pointBudgetThrottledLights,
			stats.budgetThrottledLights );
		return false;
	}
	if ( stats.arb2CacheAwareLights <= 0 || stats.arb2ShadowPasses <= 0 || stats.arb2FreshUpdatePasses <= 0 || stats.arb2CacheBudgetIsolatedLights != 0 || stats.arb2BudgetFallbackPasses != 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: ARB2 cache estimate CSM-on aware=%d passes=%d cacheable=%d hit=%d miss=%d fresh=%d isolated=%d budgetFallback=%d\n",
			stats.arb2CacheAwareLights,
			stats.arb2ShadowPasses,
			stats.arb2CacheablePasses,
			stats.arb2CacheHitPasses,
			stats.arb2CacheMissPasses,
			stats.arb2FreshUpdatePasses,
			stats.arb2CacheBudgetIsolatedLights,
			stats.arb2BudgetFallbackPasses );
		return false;
	}
	const int projectedCsmOnAtlasDiv = firstDescriptor->atlasDiv;
	if ( !R_ModernShadowPlanner_CompareDescriptorToArb2Parity( "projected-csm-on", firstDescriptor, &lights[0], &view, arb2ParityChecks ) ) {
		return false;
	}
	const modernShadowLightDescriptor_t *pointDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[1] );
	if ( !R_ModernShadowPlanner_CompareDescriptorToArb2Parity( "point", pointDescriptor, &lights[1], &view, arb2ParityChecks ) ) {
		return false;
	}

	r_shadowMapProjectedCSM.SetBool( false );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t projectedCsmOffStats = R_ModernShadowPlanner_Stats();
	const modernShadowLightDescriptor_t *projectedDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[0] );
	const modernShadowLightDescriptor_t *parallelDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[2] );
	if ( !projectedCsmOffStats.frameValid || projectedCsmOffStats.descriptorInvariantFailures != 0 || projectedDescriptor == NULL || projectedDescriptor->mapType != MODERN_SHADOW_MAP_PROJECTED || projectedDescriptor->cascadeCount != 1 || projectedDescriptor->tileCount != 1 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected CSM disabled classified as %s cascades=%d tiles=%d valid=%d invariants=%d mask=0x%08x\n",
			projectedDescriptor != NULL ? ModernShadowMapType_Name( projectedDescriptor->mapType ) : "<null>",
			projectedDescriptor != NULL ? projectedDescriptor->cascadeCount : -1,
			projectedDescriptor != NULL ? projectedDescriptor->tileCount : -1,
			projectedCsmOffStats.frameValid ? 1 : 0,
			projectedCsmOffStats.descriptorInvariantFailures,
			projectedCsmOffStats.descriptorInvariantFailureMask );
		return false;
	}
	if ( projectedCsmOffStats.projectedCsmRequested || projectedCsmOffStats.projectedCsmEligibleLights <= 0 || projectedCsmOffStats.projectedCsmCascadeLights != 0 || projectedCsmOffStats.projectedCsmSuppressedLights <= 0 || projectedCsmOffStats.nonProjectedCsmLights <= 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected CSM-off counters requested=%d eligible=%d cascaded=%d suppressed=%d nonProjected=%d csm=%d/%d\n",
			projectedCsmOffStats.projectedCsmRequested ? 1 : 0,
			projectedCsmOffStats.projectedCsmEligibleLights,
			projectedCsmOffStats.projectedCsmCascadeLights,
			projectedCsmOffStats.projectedCsmSuppressedLights,
			projectedCsmOffStats.nonProjectedCsmLights,
			projectedCsmOffStats.cascadeLights,
			projectedCsmOffStats.cascadeCount );
		return false;
	}
	if ( !projectedDescriptor->projectedStateReady || projectedDescriptor->requestedCascadeCount != 1 || projectedDescriptor->atlasDiv != 1 || projectedDescriptor->projectedAtlasRect[0][2] < 0.99f || projectedDescriptor->projectedAtlasRect[0][3] < 0.99f || projectedDescriptor->projectedClipPlanes[0][3][3] < 0.5f ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: projected CSM-off contract ready=%d requested=%d atlasDiv=%d rect=[%.2f %.2f %.2f %.2f] clipW=%.3f\n",
			projectedDescriptor->projectedStateReady ? 1 : 0,
			projectedDescriptor->requestedCascadeCount,
			projectedDescriptor->atlasDiv,
			projectedDescriptor->projectedAtlasRect[0][0],
			projectedDescriptor->projectedAtlasRect[0][1],
			projectedDescriptor->projectedAtlasRect[0][2],
			projectedDescriptor->projectedAtlasRect[0][3],
			projectedDescriptor->projectedClipPlanes[0][3][3] );
		return false;
	}
	if ( parallelDescriptor == NULL || parallelDescriptor->mapType != MODERN_SHADOW_MAP_CASCADE || parallelDescriptor->cascadeCount != 3 || parallelDescriptor->tileCount != 3 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: parallel CSM classified as %s cascades=%d tiles=%d while projected CSM was disabled\n",
			parallelDescriptor != NULL ? ModernShadowMapType_Name( parallelDescriptor->mapType ) : "<null>",
			parallelDescriptor != NULL ? parallelDescriptor->cascadeCount : -1,
			parallelDescriptor != NULL ? parallelDescriptor->tileCount : -1 );
		return false;
	}
	if ( !parallelDescriptor->projectedStateReady || parallelDescriptor->requestedCascadeCount != 3 || parallelDescriptor->atlasDiv != 2 || parallelDescriptor->projectedCascadeFallback ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: parallel CSM contract ready=%d requested=%d atlasDiv=%d fallback=%d\n",
			parallelDescriptor->projectedStateReady ? 1 : 0,
			parallelDescriptor->requestedCascadeCount,
			parallelDescriptor->atlasDiv,
			parallelDescriptor->projectedCascadeFallback ? 1 : 0 );
		return false;
	}
	if ( projectedCsmOffStats.singleProjectedMappedLights != 2 || projectedCsmOffStats.cascadeMappedLights != 1 || projectedCsmOffStats.pointMappedLights != 1 || projectedCsmOffStats.singleProjectedAtlasTiles != 2 || projectedCsmOffStats.cascadeAtlasTiles != 3 || projectedCsmOffStats.pointAtlasTiles != 6 || projectedCsmOffStats.budgetThrottledLights != 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: CSM-off class budget mapped=%d/%d/%d usedTiles=%d/%d/%d throttled=%d/%d/%d total=%d quotas=%d/%d/%d\n",
			projectedCsmOffStats.singleProjectedMappedLights,
			projectedCsmOffStats.cascadeMappedLights,
			projectedCsmOffStats.pointMappedLights,
			projectedCsmOffStats.singleProjectedAtlasTiles,
			projectedCsmOffStats.cascadeAtlasTiles,
			projectedCsmOffStats.pointAtlasTiles,
			projectedCsmOffStats.singleProjectedBudgetThrottledLights,
			projectedCsmOffStats.cascadeBudgetThrottledLights,
			projectedCsmOffStats.pointBudgetThrottledLights,
			projectedCsmOffStats.budgetThrottledLights,
			projectedCsmOffStats.singleProjectedLightQuota,
			projectedCsmOffStats.cascadeLightQuota,
			projectedCsmOffStats.pointLightQuota );
		return false;
	}
	if ( projectedCsmOffStats.arb2CacheAwareLights <= 0 || projectedCsmOffStats.arb2CacheablePasses <= 0 || projectedCsmOffStats.arb2FreshUpdatePasses <= 0 || projectedCsmOffStats.arb2CacheBudgetIsolatedLights != 0 || projectedDescriptor->arb2CacheEstimateValid == false || projectedDescriptor->arb2CacheablePasses <= 0 || projectedDescriptor->arb2FreshUpdatePasses <= 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: ARB2 cache estimate CSM-off aware=%d cacheable=%d hit=%d miss=%d fresh=%d isolated=%d desc(valid=%d cacheable=%d hit=%d miss=%d fresh=%d masks c=0x%02x h=0x%02x f=0x%02x)\n",
			projectedCsmOffStats.arb2CacheAwareLights,
			projectedCsmOffStats.arb2CacheablePasses,
			projectedCsmOffStats.arb2CacheHitPasses,
			projectedCsmOffStats.arb2CacheMissPasses,
			projectedCsmOffStats.arb2FreshUpdatePasses,
			projectedCsmOffStats.arb2CacheBudgetIsolatedLights,
			projectedDescriptor->arb2CacheEstimateValid ? 1 : 0,
			projectedDescriptor->arb2CacheablePasses,
			projectedDescriptor->arb2CacheHitPasses,
			projectedDescriptor->arb2CacheMissPasses,
			projectedDescriptor->arb2FreshUpdatePasses,
			projectedDescriptor->arb2CacheablePassMask,
			projectedDescriptor->arb2CacheHitPassMask,
			projectedDescriptor->arb2FreshUpdatePassMask );
		return false;
	}
	if ( !R_ModernShadowPlanner_CompareDescriptorToArb2Parity( "projected-csm-off", projectedDescriptor, &lights[0], &view, arb2ParityChecks ) ) {
		return false;
	}
	if ( !R_ModernShadowPlanner_CompareDescriptorToArb2Parity( "parallel-csm", parallelDescriptor, &lights[2], &view, arb2ParityChecks ) ) {
		return false;
	}

	r_shadowMapProjectedCSM.SetBool( true );
	r_shadowMapCacheCSM.SetBool( true );
	r_shadowMapCascadeCount.SetInteger( 4 );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t budgetStressStats = R_ModernShadowPlanner_Stats();
	const modernShadowLightDescriptor_t *budgetMissDescriptor = NULL;
	const int budgetDescriptorCount = R_ModernShadowPlanner_NumDescriptors();
	for ( int descriptorIndex = 0; descriptorIndex < budgetDescriptorCount; ++descriptorIndex ) {
		const modernShadowLightDescriptor_t *descriptor = R_ModernShadowPlanner_DescriptorByIndex( descriptorIndex );
		if ( descriptor != NULL && descriptor->fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ) {
			budgetMissDescriptor = descriptor;
			break;
		}
	}
	if ( !budgetStressStats.frameValid || budgetStressStats.descriptorInvariantFailures != 0 || budgetStressStats.budgetThrottledLights <= 0 || budgetStressStats.budgetFallbackLights != budgetStressStats.budgetThrottledLights || budgetStressStats.budgetStencilFallbackLights != budgetStressStats.budgetFallbackLights || budgetStressStats.budgetCacheReuseCandidateLights <= 0 || budgetStressStats.budgetSkippedLights != 0 || budgetStressStats.fallbackLights < budgetStressStats.budgetFallbackLights || budgetStressStats.budgetThrottleReasonMask == MODERN_SHADOW_THROTTLE_NONE || budgetStressStats.lodRejectedBudgetThrottledLights <= 0 || budgetStressStats.lodRejectedUnmappedLights <= 0 || budgetStressStats.throttleHistoryTrackedLights <= 0 || budgetStressStats.throttleHistoryLastMissLightDefIndex < 0 || budgetStressStats.throttleHistoryLastMissReasonMask == MODERN_SHADOW_THROTTLE_NONE || budgetMissDescriptor == NULL || budgetMissDescriptor->policy != MODERN_SHADOW_POLICY_STENCIL_FALLBACK || !budgetMissDescriptor->stencilFallback || !budgetMissDescriptor->cacheReuseCandidate || budgetMissDescriptor->atlasTileReady || !budgetMissDescriptor->pointLight || budgetMissDescriptor->lodCasterRejectedCount <= 0 || budgetMissDescriptor->budgetThrottleReasonMask == MODERN_SHADOW_THROTTLE_NONE || budgetMissDescriptor->throttleHistoryMissStreak <= 0 || budgetMissDescriptor->throttleHistoryTotalMisses <= 0 || budgetMissDescriptor->throttleHistoryLastReasonMask == MODERN_SHADOW_THROTTLE_NONE ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: budget fallback invalid valid=%d invariants=%d mask=0x%08x throttled=%d reasonMask=0x%02x reasons=%d/%d/%d/%d/%d/%d lod=%d/%d/%d/%d/%d/%d/%d history=%d last=%d/0x%02x/%d fallback=%d stencil=%d cache=%d budgetSkipped=%d totalFallback=%d miss(policy=%s stencil=%d cache=%d atlas=%d light=%d point=%d lodRejected=%d reason=0x%02x streak=%d total=%d last=0x%02x)\n",
			budgetStressStats.frameValid ? 1 : 0,
			budgetStressStats.descriptorInvariantFailures,
			budgetStressStats.descriptorInvariantFailureMask,
			budgetStressStats.budgetThrottledLights,
			budgetStressStats.budgetThrottleReasonMask,
			budgetStressStats.budgetThrottleClassLightLights,
			budgetStressStats.budgetThrottleClassTileLights,
			budgetStressStats.budgetThrottleClassPixelLights,
			budgetStressStats.budgetThrottleGlobalLightLights,
			budgetStressStats.budgetThrottleGlobalTileLights,
			budgetStressStats.budgetThrottleGlobalPixelLights,
			budgetStressStats.lodCasterTestCount,
			budgetStressStats.lodCasterRejectedCount,
			budgetStressStats.lodAlphaCasterRejectedCount,
			budgetStressStats.lodTranslucentCasterRejectedCount,
			budgetStressStats.lodRejectedLights,
			budgetStressStats.lodRejectedBudgetThrottledLights,
			budgetStressStats.lodRejectedUnmappedLights,
			budgetStressStats.throttleHistoryTrackedLights,
			budgetStressStats.throttleHistoryLastMissLightDefIndex,
			budgetStressStats.throttleHistoryLastMissReasonMask,
			budgetStressStats.throttleHistoryLastMissBudgetClass,
			budgetStressStats.budgetFallbackLights,
			budgetStressStats.budgetStencilFallbackLights,
			budgetStressStats.budgetCacheReuseCandidateLights,
			budgetStressStats.budgetSkippedLights,
			budgetStressStats.fallbackLights,
			budgetMissDescriptor != NULL ? ModernShadowPolicy_Name( budgetMissDescriptor->policy ) : "<null>",
			budgetMissDescriptor != NULL && budgetMissDescriptor->stencilFallback ? 1 : 0,
			budgetMissDescriptor != NULL && budgetMissDescriptor->cacheReuseCandidate ? 1 : 0,
			budgetMissDescriptor != NULL && budgetMissDescriptor->atlasTileReady ? 1 : 0,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->lightDefIndex : -1,
			budgetMissDescriptor != NULL && budgetMissDescriptor->pointLight ? 1 : 0,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->lodCasterRejectedCount : -1,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->budgetThrottleReasonMask : 0,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->throttleHistoryMissStreak : -1,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->throttleHistoryTotalMisses : -1,
			budgetMissDescriptor != NULL ? budgetMissDescriptor->throttleHistoryLastReasonMask : 0 );
		return false;
	}

	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t fairnessStats = R_ModernShadowPlanner_Stats();
	const modernShadowLightDescriptor_t *fairnessPointDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[1] );
	const modernShadowLightDescriptor_t *fairnessBudgetMissDescriptor = NULL;
	const int fairnessDescriptorCount = R_ModernShadowPlanner_NumDescriptors();
	for ( int descriptorIndex = 0; descriptorIndex < fairnessDescriptorCount; ++descriptorIndex ) {
		const modernShadowLightDescriptor_t *descriptor = R_ModernShadowPlanner_DescriptorByIndex( descriptorIndex );
		if ( descriptor != NULL && descriptor->fallbackReason == MODERN_SHADOW_FALLBACK_BUDGET ) {
			fairnessBudgetMissDescriptor = descriptor;
			break;
		}
	}
	if ( !fairnessStats.frameValid || fairnessStats.descriptorInvariantFailures != 0 || fairnessPointDescriptor == NULL || fairnessPointDescriptor->policy != MODERN_SHADOW_POLICY_MAPPED || fairnessPointDescriptor->fairnessAge <= 0 || fairnessPointDescriptor->fairnessBoost <= 0 || fairnessPointDescriptor->fairnessPriority <= fairnessPointDescriptor->priority || fairnessPointDescriptor->throttleHistoryTotalMisses <= 0 || fairnessPointDescriptor->throttleHistoryLastReasonMask == MODERN_SHADOW_THROTTLE_NONE || fairnessPointDescriptor->throttleHistoryLastMissAge < 0 || fairnessStats.fairnessAgedLights <= 0 || fairnessStats.fairnessBoostedLights <= 0 || fairnessStats.fairnessAgedMappedLights <= 0 || fairnessStats.fairnessMaxAge < fairnessPointDescriptor->fairnessAge || fairnessStats.budgetThrottledLights <= 0 || fairnessStats.budgetFallbackLights != fairnessStats.budgetThrottledLights || fairnessStats.budgetSkippedLights != 0 || fairnessStats.budgetThrottleReasonMask == MODERN_SHADOW_THROTTLE_NONE || fairnessStats.throttleHistoryTrackedLights <= 0 || fairnessStats.throttleHistoryRecoveredLights <= 0 || fairnessStats.throttleHistoryLastMissReasonMask == MODERN_SHADOW_THROTTLE_NONE || fairnessBudgetMissDescriptor == NULL || fairnessBudgetMissDescriptor->budgetThrottleReasonMask == MODERN_SHADOW_THROTTLE_NONE || fairnessBudgetMissDescriptor->throttleHistoryMissStreak <= 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: fairness invalid valid=%d invariants=%d mask=0x%08x point(policy=%s age=%d boost=%d priority=%d/%d atlas=%d hist=%d/%d last=0x%02x age=%d) fairness=%d/%d/%d/%d max=%d/%d budget=%d/%d/%d reason=0x%02x history=%d/%d/%d max=%d/%d last=%d/0x%02x/%d miss=%d reason=0x%02x streak=%d\n",
			fairnessStats.frameValid ? 1 : 0,
			fairnessStats.descriptorInvariantFailures,
			fairnessStats.descriptorInvariantFailureMask,
			fairnessPointDescriptor != NULL ? ModernShadowPolicy_Name( fairnessPointDescriptor->policy ) : "<null>",
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->fairnessAge : -1,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->fairnessBoost : -1,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->priority : -1,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->fairnessPriority : -1,
			fairnessPointDescriptor != NULL && fairnessPointDescriptor->atlasTileReady ? 1 : 0,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->throttleHistoryMissStreak : -1,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->throttleHistoryTotalMisses : -1,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->throttleHistoryLastReasonMask : 0,
			fairnessPointDescriptor != NULL ? fairnessPointDescriptor->throttleHistoryLastMissAge : -1,
			fairnessStats.fairnessTrackedLights,
			fairnessStats.fairnessAgedLights,
			fairnessStats.fairnessBoostedLights,
			fairnessStats.fairnessAgedMappedLights,
			fairnessStats.fairnessMaxAge,
			fairnessStats.fairnessMaxBoost,
			fairnessStats.budgetThrottledLights,
			fairnessStats.budgetFallbackLights,
			fairnessStats.budgetSkippedLights,
			fairnessStats.budgetThrottleReasonMask,
			fairnessStats.throttleHistoryTrackedLights,
			fairnessStats.throttleHistoryRepeatedBudgetMissLights,
			fairnessStats.throttleHistoryRecoveredLights,
			fairnessStats.throttleHistoryMaxMissStreak,
			fairnessStats.throttleHistoryMaxTotalMisses,
			fairnessStats.throttleHistoryLastMissLightDefIndex,
			fairnessStats.throttleHistoryLastMissReasonMask,
			fairnessStats.throttleHistoryLastMissBudgetClass,
			fairnessBudgetMissDescriptor != NULL ? fairnessBudgetMissDescriptor->lightDefIndex : -1,
			fairnessBudgetMissDescriptor != NULL ? fairnessBudgetMissDescriptor->budgetThrottleReasonMask : 0,
			fairnessBudgetMissDescriptor != NULL ? fairnessBudgetMissDescriptor->throttleHistoryMissStreak : -1 );
		return false;
	}

	common->Printf(
		"RendererShadowPlanner regression coverage: projected=1 point=1 csm=1 projectedCsmOff=1 budgetFallback=1 cacheReuse=1 fairness=1 throttleHistory=1 casterAdmission=1 receiverFallback=1 lod=1 arb2Parity=%d\n",
		arb2ParityChecks );
	common->Printf(
		"RendererShadowPlanner self-test passed (lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d csm=%d/%d projectedGate(on=%d/%d/%d off=%d/%d/%d nonProjected=%d) projectedTransform(pad=%.3f scale=%.6f baseS1=%.6f rawS1=%.6f) sampleValidation(samples=%d valid=%d skipped=%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d reason=%s) projectedCsmOff=%d/%d projectedAtlasDiv=%d/%d parity=%d classMapped=%d/%d/%d classTiles=%d/%d/%d quotas=%d/%d/%d arb2Cache=%d/%d/%d/%d/%d lod=%d/%d/%d/%d/%d/%d/%d budgetMiss=%d/%d/%d/%d reason=0x%02x fairness=%d/%d/%d/%d/%d throttleHistory=%d/%d/%d/%d/%d/%d size=%d budget=%d atlas=%d/%d pixels=%d guarded=%d blocked=%d invariants=%d/%d/%d/%d status=%s)\n",
		stats.viewLightCount,
		stats.descriptorCount,
		stats.mappedLights,
		stats.fallbackLights,
		stats.skippedLights,
		stats.cascadeLights,
		stats.cascadeCount,
		stats.projectedCsmEligibleLights,
		stats.projectedCsmCascadeLights,
		stats.projectedCsmSuppressedLights,
		projectedCsmOffStats.projectedCsmEligibleLights,
		projectedCsmOffStats.projectedCsmCascadeLights,
		projectedCsmOffStats.projectedCsmSuppressedLights,
		projectedCsmOffStats.nonProjectedCsmLights,
		firstDescriptor->projectionPad,
		firstDescriptor->projectionScale,
		firstDescriptor->projectedBaseClipPlanes[0][1],
		lights[0].lightProject[0][1],
		firstDescriptor->projectedSampleCount,
		firstDescriptor->projectedValidSampleCount,
		firstDescriptor->projectedSkippedSampleCount,
		firstDescriptor->projectedPositiveWPoints,
		firstDescriptor->projectedNegativeWPoints,
		firstDescriptor->projectedNearZeroWPoints,
		firstDescriptor->projectedNanWPoints,
		firstDescriptor->projectedInvalidNdcPoints,
		firstDescriptor->projectedMixedWSignCascades,
		R_ShadowMapProjectedFallbackReasonName( firstDescriptor->projectedFallbackReason ),
		projectedCsmOffStats.cascadeLights,
		projectedCsmOffStats.cascadeCount,
		projectedCsmOnAtlasDiv,
		projectedDescriptor->atlasDiv,
		arb2ParityChecks,
		projectedCsmOffStats.singleProjectedMappedLights,
		projectedCsmOffStats.cascadeMappedLights,
		projectedCsmOffStats.pointMappedLights,
		projectedCsmOffStats.singleProjectedAtlasTiles,
		projectedCsmOffStats.cascadeAtlasTiles,
		projectedCsmOffStats.pointAtlasTiles,
		stats.singleProjectedLightQuota,
		stats.cascadeLightQuota,
		stats.pointLightQuota,
		projectedCsmOffStats.arb2CacheAwareLights,
		projectedCsmOffStats.arb2CacheablePasses,
		projectedCsmOffStats.arb2CacheHitPasses,
		projectedCsmOffStats.arb2CacheMissPasses,
		projectedCsmOffStats.arb2FreshUpdatePasses,
		stats.lodCasterTestCount,
		stats.lodCasterRejectedCount,
		stats.lodAlphaCasterRejectedCount,
		stats.lodTranslucentCasterRejectedCount,
		stats.lodRejectedLights,
		budgetStressStats.lodRejectedBudgetThrottledLights,
		budgetStressStats.lodRejectedUnmappedLights,
		budgetStressStats.budgetThrottledLights,
		budgetStressStats.budgetFallbackLights,
		budgetStressStats.budgetCacheReuseCandidateLights,
		budgetStressStats.budgetSkippedLights,
		budgetStressStats.budgetThrottleReasonMask,
		fairnessStats.fairnessTrackedLights,
		fairnessStats.fairnessAgedLights,
		fairnessStats.fairnessBoostedLights,
		fairnessStats.fairnessAgedMappedLights,
		fairnessPointDescriptor != NULL ? fairnessPointDescriptor->lightDefIndex : -1,
		fairnessStats.throttleHistoryTrackedLights,
		fairnessStats.throttleHistoryRepeatedBudgetMissLights,
		fairnessStats.throttleHistoryRecoveredLights,
		fairnessStats.throttleHistoryMaxMissStreak,
		fairnessStats.throttleHistoryMaxTotalMisses,
		fairnessStats.throttleHistoryLastMissLightDefIndex,
		stats.shadowMapSize,
		stats.maxMappedLights,
		stats.atlasTiles,
		stats.maxAtlasTiles,
		stats.estimatedPixels,
		stats.receiverGuardedLights,
		stats.receiverSamplingBlockedLights,
		stats.descriptorInvariantFailures,
		projectedCsmOffStats.descriptorInvariantFailures,
		budgetStressStats.descriptorInvariantFailures,
		fairnessStats.descriptorInvariantFailures,
		stats.status );
	return true;
}

static bool R_ShadowMapPointFaceBoundsSelfTest( void ) {
	viewLight_t light;
	memset( &light, 0, sizeof( light ) );
	const idVec3 minimum( -6.0f, -3.0f, -1.0f );
	const idVec3 maximum( 6.0f, 3.0f, 1.0f );
	int rejected = 0;
	for ( int sample = 0; sample < 160; ++sample ) {
		float matrix[16] = { 0 };
		matrix[15] = 1.0f;
		// Rotated, mirrored, scaled, and sheared bounds, including singular
		// transforms and casters that straddle cube seams or enclose the light.
		const float angle = sample * 0.37f;
		matrix[0] = idMath::Cos( angle ) * ( ( sample % 7 ) - 3 );
		matrix[1] = idMath::Sin( angle ) * ( ( sample % 7 ) - 3 );
		matrix[4] = -idMath::Sin( angle ) * 2.0f + 0.4f;
		matrix[5] = idMath::Cos( angle ) * 2.0f;
		matrix[8] = 0.75f;
		matrix[10] = ( sample % 5 ) - 2.0f;
		matrix[12] = ( ( sample * 17 ) % 101 ) - 50.0f;
		matrix[13] = ( ( sample * 29 ) % 103 ) - 51.0f;
		matrix[14] = ( ( sample * 37 ) % 107 ) - 53.0f;
		for ( int face = 0; face < 6; ++face ) {
			if ( !R_ShadowMapCasterOutsidePointFace( &light, matrix,
					minimum.ToFloatPtr(), maximum.ToFloatPtr(), face ) ) {
				continue;
			}
			++rejected;
			const int axis = face >> 1;
			const float sign = ( face & 1 ) ? -1.0f : 1.0f;
			bool separatingPlane = false;
			// Independent reference: transform all eight corners, then require
			// a single face plane to exclude every corner of the convex box.
			for ( int plane = 0; plane < 4; ++plane ) {
				const int other = ( axis + 1 + ( plane >> 1 ) ) % 3;
				const float side = ( plane & 1 ) ? -1.0f : 1.0f;
				bool allOutside = true;
				for ( int corner = 0; corner < 8; ++corner ) {
					idVec3 local, world;
					for ( int component = 0; component < 3; ++component ) {
						local[component] = ( corner & ( 1 << component ) ) ? maximum[component] : minimum[component];
					}
					R_LocalPointToGlobal( matrix, local, world );
					allOutside &= sign * world[axis] + side * world[other] < 0.0f;
				}
				separatingPlane |= allOutside;
			}
			if ( !separatingPlane ) {
				common->Printf( "Point-face bounds self-test FAILED sample=%d face=%d\n", sample, face );
				return false;
			}
		}
	}
	float identity[16] = { 0 };
	identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
	for ( int face = 0; face < 6; ++face ) {
		if ( R_ShadowMapCasterOutsidePointFace( &light, identity, minimum.ToFloatPtr(), maximum.ToFloatPtr(), face ) ||
				R_ShadowMapCasterOutsidePointFace( &light, identity, maximum.ToFloatPtr(), minimum.ToFloatPtr(), face ) ) {
			return false;
		}
	}
	common->Printf( "Point-face bounds self-test %s: 960 affine cases, %d conservative rejections\n",
		rejected > 0 ? "passed" : "FAILED", rejected );
	return rejected > 0;
}

bool RendererShadowProjectedDiagnostic_RunSelfTest( void ) {
	if ( !R_ShadowMapPointFaceBoundsSelfTest() ) {
		return false;
	}
	// pure-math stabilization pins run regardless of planner availability
	if ( !R_ShadowMapCascadeStabilitySelfTest() ) {
		common->Printf( "RendererShadowProjectedDiagnostic self-test failed (cascade stability)\n" );
		return false;
	}

	if ( !rg_modernShadowPlannerInitialized || !rg_modernShadowPlannerFeatures.scenePackets ) {
		common->Printf( "RendererShadowProjectedDiagnostic self-test passed (planner unavailable)\n" );
		return true;
	}

	struct rendererShadowProjectedDiagnosticStateReset_t {
		~rendererShadowProjectedDiagnosticStateReset_t() {
			idScenePacketFrame emptyFrame;
			emptyFrame.Clear();
			R_ModernShadowPlanner_PrepareFrame( emptyFrame, false );
			R_ModernShadowPlanner_ResetFairnessHistory();
			R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, "projected-diagnostic-complete" );
		}
	} plannerStateReset;
	R_ModernShadowPlanner_ResetFairnessHistory();

	struct rendererShadowProjectedDiagnosticBoolCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererShadowProjectedDiagnosticBoolCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererShadowProjectedDiagnosticBoolCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	struct rendererShadowProjectedDiagnosticIntCVarRestore_t {
		idCVar &cvar;
		int oldValue;
		rendererShadowProjectedDiagnosticIntCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetInteger() ) {}
		~rendererShadowProjectedDiagnosticIntCVarRestore_t() { cvar.SetInteger( oldValue ); }
	};
	struct rendererShadowProjectedDiagnosticFloatCVarRestore_t {
		idCVar &cvar;
		float oldValue;
		rendererShadowProjectedDiagnosticFloatCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetFloat() ) {}
		~rendererShadowProjectedDiagnosticFloatCVarRestore_t() { cvar.SetFloat( oldValue ); }
	};
	struct rendererShadowProjectedDiagnosticStringCVarRestore_t {
		idCVar &cvar;
		idStr oldValue;
		rendererShadowProjectedDiagnosticStringCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetString() ) {}
		~rendererShadowProjectedDiagnosticStringCVarRestore_t() { cvar.SetString( oldValue.c_str() ); }
	};

	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreShadows( r_shadows );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreShadowMap( r_useShadowMap );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreCSM( r_shadowMapCSM );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreProjectedCSM( r_shadowMapProjectedCSM );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreTranslucent( r_shadowMapTranslucentMoments );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreStaticCache( r_shadowMapStaticCache );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreCacheCSM( r_shadowMapCacheCSM );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreReceiverPlaneBias( r_shadowMapReceiverPlaneBias );
	rendererShadowProjectedDiagnosticBoolCVarRestore_t restoreCascadeStabilize( r_shadowMapCascadeStabilize );
	rendererShadowProjectedDiagnosticIntCVarRestore_t restoreCascadeCount( r_shadowMapCascadeCount );
	rendererShadowProjectedDiagnosticIntCVarRestore_t restoreShadowMapSize( r_shadowMapSize );
	rendererShadowProjectedDiagnosticIntCVarRestore_t restoreFilterTaps( r_shadowMapFilterTaps );
	rendererShadowProjectedDiagnosticIntCVarRestore_t restoreFilterMode( r_shadowMapFilterMode );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreZNear( r_znear );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreProjectionPad( r_shadowMapProjectionPad );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreCascadeDistance( r_shadowMapCascadeDistance );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreCascadeBlend( r_shadowMapCascadeBlend );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreBias( r_shadowMapBias );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreNormalBias( r_shadowMapNormalBias );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreTexelBiasScale( r_shadowMapTexelBiasScale );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreFilterRadius( r_shadowMapFilterRadius );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restoreDistantFilterScale( r_shadowMapDistantFilterScale );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restorePCSSLightRadius( r_shadowMapPCSSLightRadius );
	rendererShadowProjectedDiagnosticFloatCVarRestore_t restorePCSSMaxRadius( r_shadowMapPCSSMaxRadius );
	rendererShadowProjectedDiagnosticStringCVarRestore_t restoreBenchmarkPreset( r_rendererBenchmarkPreset );

	r_shadows.SetBool( true );
	r_useShadowMap.SetBool( true );
	r_shadowMapCSM.SetBool( true );
	r_shadowMapProjectedCSM.SetBool( true );
	r_shadowMapTranslucentMoments.SetBool( false );
	r_shadowMapStaticCache.SetBool( false );
	r_shadowMapCacheCSM.SetBool( false );
	r_shadowMapReceiverPlaneBias.SetBool( false );
	r_shadowMapCascadeStabilize.SetBool( true );
	r_shadowMapCascadeCount.SetInteger( 3 );
	r_shadowMapSize.SetInteger( 1024 );
	r_shadowMapFilterTaps.SetInteger( 13 );
	r_shadowMapFilterMode.SetInteger( 0 );
	r_znear.SetFloat( 3.0f );
	r_shadowMapProjectionPad.SetFloat( 0.15f );
	r_shadowMapCascadeDistance.SetFloat( 1536.0f );
	r_shadowMapCascadeBlend.SetFloat( 0.15f );
	r_shadowMapBias.SetFloat( 0.00016f );
	r_shadowMapNormalBias.SetFloat( 0.00075f );
	r_shadowMapTexelBiasScale.SetFloat( 0.45f );
	r_shadowMapFilterRadius.SetFloat( 2.0f );
	r_shadowMapDistantFilterScale.SetFloat( 0.35f );
	r_shadowMapPCSSLightRadius.SetFloat( 4.0f );
	r_shadowMapPCSSMaxRadius.SetFloat( 8.0f );
	r_rendererBenchmarkPreset.SetString( "baseline" );

	drawSurf_t casterSurfs[2];
	drawSurf_t receiverSurf;
	srfTriangles_t receiverGeo;
	viewEntity_t receiverSpace;
	float receiverRegisters[1] = { 1.0f };
	memset( casterSurfs, 0, sizeof( casterSurfs ) );
	memset( &receiverSurf, 0, sizeof( receiverSurf ) );
	memset( &receiverGeo, 0, sizeof( receiverGeo ) );
	memset( &receiverSpace, 0, sizeof( receiverSpace ) );
	receiverGeo.numIndexes = 3;
	R_ModernShadowPlanner_SetIdentityMatrix( receiverSpace.modelMatrix );
	R_ModernShadowPlanner_SetIdentityMatrix( receiverSpace.modelViewMatrix );
	receiverSurf.geo = &receiverGeo;
	receiverSurf.space = &receiverSpace;
	receiverSurf.material = tr.defaultMaterial;
	receiverSurf.shaderRegisters = receiverRegisters;

	viewLight_t light;
	idRenderLightLocal lightDef;
	memset( &light, 0, sizeof( light ) );
	// idRenderLightLocal is polymorphic; its constructor clears its fields while preserving its vtable.
	const idMaterial *projectedLightShader = R_ModernShadowPlanner_SelfTestLightShader( "lights/defaultProjectedLight" );
	if ( projectedLightShader == NULL ) {
		projectedLightShader = tr.defaultMaterial;
	}
	lightDef.index = 3101;
	lightDef.parms.origin.Set( 128.0f, -96.0f, 64.0f );
	lightDef.parms.lightRadius.Set( 384.0f, 384.0f, 384.0f );
	lightDef.lightShader = projectedLightShader;
	light.lightDef = &lightDef;
	light.lightShader = projectedLightShader;
	light.scissorRect.x1 = 96;
	light.scissorRect.y1 = 64;
	light.scissorRect.x2 = 1055;
	light.scissorRect.y2 = 655;
	light.viewInsideLight = true;
	light.pointLight = false;
	light.parallel = false;
	light.globalLightOrigin = lightDef.parms.origin;
	light.lightRadius = lightDef.parms.lightRadius;
	light.lightProject[0] = idPlane( 0.00135f, 0.00020f, 0.00000f, 0.50f );
	light.lightProject[1] = idPlane( -0.00015f, 0.00110f, 0.00010f, 0.50f );
	light.lightProject[2] = idPlane( 0.00000f, 0.00000f, 0.00000f, 1.00f );
	light.lightProject[3] = idPlane( 0.00090f, 0.00000f, 0.00025f, 0.00f );
	light.globalShadowMapCasters = &casterSurfs[0];
	light.localShadowMapCasters = NULL;
	light.globalShadows = &casterSurfs[1];
	light.localShadows = NULL;
	light.globalInteractions = &receiverSurf;
	light.localInteractions = NULL;
	light.translucentInteractions = NULL;
	light.shadowMapCasterCount = 2;
	light.shadowMapStaticCasterCount = 2;
	light.shadowMapDynamicCasterCount = 0;
	light.shadowMapTranslucentCasterCount = 0;
	light.shadowMapCasterSignature = 0x31c001;

	viewDef_t view;
	memset( &view, 0, sizeof( view ) );
	view.renderView.width = 1280;
	view.renderView.height = 720;
	view.renderView.fov_x = 90.0f;
	view.renderView.fov_y = 70.0f;
	view.renderView.vieworg.Zero();
	view.renderView.viewaxis = mat3_identity;
	view.viewport.x1 = 0;
	view.viewport.y1 = 0;
	view.viewport.x2 = 1279;
	view.viewport.y2 = 719;
	view.scissor.x1 = 0;
	view.scissor.y1 = 0;
	view.scissor.x2 = 1279;
	view.scissor.y2 = 719;
	view.viewLights = &light;

	idScenePacketFrame packetFrame;
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererShadowProjectedDiagnostic self-test failed: could not add diagnostic scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t stats = R_ModernShadowPlanner_Stats();
	const modernShadowLightDescriptor_t *descriptor = R_ModernShadowPlanner_DescriptorForLight( &light );
	// No trivial-pass escape (M5): the synthetic flashlight material is
	// guaranteed to cast shadows, so a skipped light here is a regression.
	if ( !stats.frameValid || stats.viewLightCount != 1 || stats.descriptorCount != 1 || stats.descriptorInvariantFailures != 0 || stats.cascadeLights != 1 || stats.cascadeCount != 3 || stats.mappedLights != 1 || descriptor == NULL ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: planner valid=%d lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d csm=%d/%d invariants=%d mask=0x%08x descriptor=%d status=%s\n",
			stats.frameValid ? 1 : 0,
			stats.viewLightCount,
			stats.descriptorCount,
			stats.mappedLights,
			stats.fallbackLights,
			stats.skippedLights,
			stats.cascadeLights,
			stats.cascadeCount,
			stats.descriptorInvariantFailures,
			stats.descriptorInvariantFailureMask,
			descriptor != NULL ? 1 : 0,
			stats.status );
		return false;
	}
	if ( !stats.projectedCsmRequested || stats.ordinaryProjectedLights != 1 || stats.projectedCsmEligibleLights != 1 || stats.projectedCsmCascadeLights != 1 || stats.projectedCsmSuppressedLights != 0 || stats.nonProjectedCsmLights != 0 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: projected CSM counters requested=%d ordinary=%d eligible=%d cascaded=%d suppressed=%d nonProjected=%d\n",
			stats.projectedCsmRequested ? 1 : 0,
			stats.ordinaryProjectedLights,
			stats.projectedCsmEligibleLights,
			stats.projectedCsmCascadeLights,
			stats.projectedCsmSuppressedLights,
			stats.nonProjectedCsmLights );
		return false;
	}
	if ( descriptor->policy != MODERN_SHADOW_POLICY_MAPPED || descriptor->mapType != MODERN_SHADOW_MAP_CASCADE || descriptor->fallbackReason != MODERN_SHADOW_FALLBACK_NONE || descriptor->cascadeCount != 3 || descriptor->tileCount != 3 || descriptor->atlasDiv != 2 || !descriptor->projectedStateReady || !descriptor->atlasTileReady || descriptor->projectedCascadeFallback ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: descriptor policy=%s map=%s fallback=%s cascades=%d tiles=%d atlasDiv=%d ready=%d tileReady=%d projectedFallback=%d\n",
			ModernShadowPolicy_Name( descriptor->policy ),
			ModernShadowMapType_Name( descriptor->mapType ),
			ModernShadowFallbackReason_Name( descriptor->fallbackReason ),
			descriptor->cascadeCount,
			descriptor->tileCount,
			descriptor->atlasDiv,
			descriptor->projectedStateReady ? 1 : 0,
			descriptor->atlasTileReady ? 1 : 0,
			descriptor->projectedCascadeFallback ? 1 : 0 );
		return false;
	}
	const float expectedDiagnosticProjectionPad = 0.15f;
	const float expectedDiagnosticProjectionScale = R_ShadowMapProjectionScale( expectedDiagnosticProjectionPad );
	const float expectedDiagnosticBaseS0 = ( light.lightProject[0][0] * 2.0f - light.lightProject[2][0] ) * expectedDiagnosticProjectionScale;
	if ( !R_ModernShadowPlanner_FloatClose( descriptor->projectionPad, expectedDiagnosticProjectionPad ) || !R_ModernShadowPlanner_FloatClose( descriptor->projectionScale, expectedDiagnosticProjectionScale ) || !R_ModernShadowPlanner_FloatClose( descriptor->projectedBaseClipPlanes[0][0], expectedDiagnosticBaseS0 ) || R_ModernShadowPlanner_FloatClose( descriptor->projectedBaseClipPlanes[0][0], light.lightProject[0][0], 0.00001f ) ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: projected transform pad=%.3f/%.3f scale=%.6f/%.6f baseS0=%.6f expected=%.6f raw=%.6f\n",
			descriptor->projectionPad,
			expectedDiagnosticProjectionPad,
			descriptor->projectionScale,
			expectedDiagnosticProjectionScale,
			descriptor->projectedBaseClipPlanes[0][0],
			expectedDiagnosticBaseS0,
			light.lightProject[0][0] );
		return false;
	}
	if ( descriptor->projectedFallbackReason != SHADOWMAP_PROJECTED_FALLBACK_NONE || descriptor->projectedSampleCount != SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT * descriptor->requestedCascadeCount || descriptor->projectedValidSampleCount + descriptor->projectedSkippedSampleCount != descriptor->projectedSampleCount || descriptor->projectedPositiveWPoints <= 0 || descriptor->projectedNegativeWPoints != 0 || descriptor->projectedMixedWSignCascades != 0 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: sample validation reason=%s samples=%d/%d/%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d requested=%d\n",
			R_ShadowMapProjectedFallbackReasonName( descriptor->projectedFallbackReason ),
			descriptor->projectedSampleCount,
			descriptor->projectedValidSampleCount,
			descriptor->projectedSkippedSampleCount,
			descriptor->projectedPositiveWPoints,
			descriptor->projectedNegativeWPoints,
			descriptor->projectedNearZeroWPoints,
			descriptor->projectedNanWPoints,
			descriptor->projectedInvalidNdcPoints,
			descriptor->projectedMixedWSignCascades,
			descriptor->requestedCascadeCount );
		return false;
	}

	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( &light );
	if ( !classification.ordinaryProjectedLight || !classification.projectedCSMEnabled || !classification.csmEnabled || classification.cascadeCount != 3 || classification.tileCount != 3 || classification.atlasDiv != 2 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: classification=%s ordinary=%d projectedCSM=%d csm=%d cascades=%d tiles=%d atlasDiv=%d\n",
			R_ShadowMapLightClassName( classification.lightClass ),
			classification.ordinaryProjectedLight ? 1 : 0,
			classification.projectedCSMEnabled ? 1 : 0,
			classification.csmEnabled ? 1 : 0,
			classification.cascadeCount,
			classification.tileCount,
			classification.atlasDiv );
		return false;
	}
	const shadowMapProjectedFilterSettings_t localFilterSettings = R_ShadowMapProjectedFilterSettings( &light );
	if ( localFilterSettings.distantSource || !R_ModernShadowPlanner_FloatClose( localFilterSettings.filterScale, 1.0f ) || !R_ModernShadowPlanner_FloatClose( localFilterSettings.filterRadius, 2.0f ) || !R_ModernShadowPlanner_FloatClose( localFilterSettings.effectiveFilterRadius, 2.0f ) ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: local filter distant=%d scale=%.3f radius=%.3f effective=%.3f\n",
			localFilterSettings.distantSource ? 1 : 0,
			localFilterSettings.filterScale,
			localFilterSettings.filterRadius,
			localFilterSettings.effectiveFilterRadius );
		return false;
	}

	viewLight_t distantFilterLight = light;
	idRenderLightLocal distantFilterLightDef = lightDef;
	distantFilterLightDef.parms.parallel = true;
	distantFilterLight.lightDef = &distantFilterLightDef;
	distantFilterLight.parallel = true;
	distantFilterLight.pointLight = true;
	r_shadowMapFilterMode.SetInteger( 2 );
	const shadowMapProjectedFilterSettings_t distantFilterSettings = R_ShadowMapProjectedFilterSettings( &distantFilterLight );
	r_shadowMapFilterMode.SetInteger( 0 );
	if ( !distantFilterSettings.distantSource || !R_ModernShadowPlanner_FloatClose( distantFilterSettings.filterScale, 0.35f ) || !R_ModernShadowPlanner_FloatClose( distantFilterSettings.filterRadius, 0.7f ) || !R_ModernShadowPlanner_FloatClose( distantFilterSettings.pcssLightRadius, 1.4f ) || !R_ModernShadowPlanner_FloatClose( distantFilterSettings.pcssMaxRadius, 2.8f ) || !R_ModernShadowPlanner_FloatClose( distantFilterSettings.effectiveFilterRadius, 2.8f ) ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: distant filter distant=%d scale=%.3f radius=%.3f pcss=%.3f/%.3f effective=%.3f\n",
			distantFilterSettings.distantSource ? 1 : 0,
			distantFilterSettings.filterScale,
			distantFilterSettings.filterRadius,
			distantFilterSettings.pcssLightRadius,
			distantFilterSettings.pcssMaxRadius,
			distantFilterSettings.effectiveFilterRadius );
		return false;
	}

	viewLight_t viewScopedLights[2];
	idRenderLightLocal viewScopedLightDefs[2];
	const char *viewScopedLabels[2] = { "allowLightInViewID", "suppressLightInViewID" };
	for ( int viewScopedIndex = 0; viewScopedIndex < 2; viewScopedIndex++ ) {
		viewScopedLights[viewScopedIndex] = light;
		viewScopedLightDefs[viewScopedIndex] = lightDef;
		viewScopedLightDefs[viewScopedIndex].index = 3103 + viewScopedIndex;
		viewScopedLightDefs[viewScopedIndex].parms.allowLightInViewID = viewScopedIndex == 0 ? 1 : 0;
		viewScopedLightDefs[viewScopedIndex].parms.suppressLightInViewID = viewScopedIndex == 1 ? 1 : 0;
		viewScopedLights[viewScopedIndex].lightDef = &viewScopedLightDefs[viewScopedIndex];

		const shadowMapLightClassification_t viewScopedClassification = R_ClassifyShadowMapLight( &viewScopedLights[viewScopedIndex] );
		if ( !viewScopedClassification.ordinaryProjectedLight || viewScopedClassification.projectedCSMEnabled || viewScopedClassification.csmEnabled || viewScopedClassification.cascadeCount != 1 || viewScopedClassification.tileCount != 1 || viewScopedClassification.atlasDiv != 1 ) {
			common->Printf(
				"RendererShadowProjectedDiagnostic self-test failed: view-scoped projected light %s classified as %s ordinary=%d projectedCSM=%d/%d csm=%d cascades=%d tiles=%d atlasDiv=%d\n",
				viewScopedLabels[viewScopedIndex],
				R_ShadowMapLightClassName( viewScopedClassification.lightClass ),
				viewScopedClassification.ordinaryProjectedLight ? 1 : 0,
				viewScopedClassification.projectedCSMEnabled ? 1 : 0,
				viewScopedClassification.projectedCSMGateApplies ? 1 : 0,
				viewScopedClassification.csmEnabled ? 1 : 0,
				viewScopedClassification.cascadeCount,
				viewScopedClassification.tileCount,
				viewScopedClassification.atlasDiv );
			return false;
		}

		shadowMapProjectedLightState_t viewScopedProjectedState;
		R_BuildShadowMapProjectedLightState( &viewScopedLights[viewScopedIndex], &view, descriptor->resolution, viewScopedProjectedState );
		if ( !viewScopedProjectedState.valid || viewScopedProjectedState.requestedCascadeCount != 1 || viewScopedProjectedState.cascadeCount != 1 || viewScopedProjectedState.atlasDiv != 1 || viewScopedProjectedState.cascadeFallback || viewScopedProjectedState.fallbackReason != SHADOWMAP_PROJECTED_FALLBACK_NONE || viewScopedProjectedState.cascadeFit[0].attempted ) {
			common->Printf(
				"RendererShadowProjectedDiagnostic self-test failed: view-scoped projected state %s valid=%d requested=%d cascades=%d atlasDiv=%d fallback=%d/%s fitAttempted=%d\n",
				viewScopedLabels[viewScopedIndex],
				viewScopedProjectedState.valid ? 1 : 0,
				viewScopedProjectedState.requestedCascadeCount,
				viewScopedProjectedState.cascadeCount,
				viewScopedProjectedState.atlasDiv,
				viewScopedProjectedState.cascadeFallback ? 1 : 0,
				R_ShadowMapProjectedFallbackReasonName( viewScopedProjectedState.fallbackReason ),
				viewScopedProjectedState.cascadeFit[0].attempted ? 1 : 0 );
			return false;
		}

		shadowMapArb2ParityState_t viewScopedArb2State;
		if ( !RB_ShadowMapBuildArb2ParityState( &viewScopedLights[viewScopedIndex], &view, descriptor->resolution, viewScopedArb2State ) || !viewScopedArb2State.valid || !viewScopedArb2State.projectedStateReady || viewScopedArb2State.csmEnabled || viewScopedArb2State.requestedCascadeCount != 1 || viewScopedArb2State.cascadeCount != 1 || viewScopedArb2State.tileCount != 1 || viewScopedArb2State.atlasDiv != 1 || viewScopedArb2State.projectedCascadeFallback ) {
			common->Printf(
				"RendererShadowProjectedDiagnostic self-test failed: view-scoped ARB2 state %s valid=%d projectedReady=%d csm=%d requested=%d cascades=%d tiles=%d atlasDiv=%d fallback=%d/%d\n",
				viewScopedLabels[viewScopedIndex],
				viewScopedArb2State.valid ? 1 : 0,
				viewScopedArb2State.projectedStateReady ? 1 : 0,
				viewScopedArb2State.csmEnabled ? 1 : 0,
				viewScopedArb2State.requestedCascadeCount,
				viewScopedArb2State.cascadeCount,
				viewScopedArb2State.tileCount,
				viewScopedArb2State.atlasDiv,
				viewScopedArb2State.projectedCascadeFallback ? 1 : 0,
				viewScopedArb2State.projectedFallbackCascade );
			return false;
		}
	}

	const idMaterial *flashlightLightShader = declManager != NULL ? declManager->FindMaterial( "gfx/lights/flashlight" ) : NULL;
	if ( flashlightLightShader == NULL ) {
		common->Printf( "RendererShadowProjectedDiagnostic self-test failed: could not resolve gfx/lights/flashlight material\n" );
		return false;
	}

	viewLight_t flashlightLight = light;
	idRenderLightLocal flashlightLightDef = lightDef;
	flashlightLightDef.index = 3105;
	flashlightLightDef.lightShader = flashlightLightShader;
	flashlightLightDef.parms.shader = flashlightLightShader;
	flashlightLight.lightDef = &flashlightLightDef;
	flashlightLight.lightShader = flashlightLightShader;

	const shadowMapLightClassification_t flashlightClassification = R_ClassifyShadowMapLight( &flashlightLight );
	if ( !flashlightClassification.ordinaryProjectedLight || flashlightClassification.projectedCSMEnabled || flashlightClassification.csmEnabled || flashlightClassification.cascadeCount != 1 || flashlightClassification.tileCount != 1 || flashlightClassification.atlasDiv != 1 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: stock flashlight classified as %s ordinary=%d projectedCSM=%d/%d csm=%d cascades=%d tiles=%d atlasDiv=%d\n",
			R_ShadowMapLightClassName( flashlightClassification.lightClass ),
			flashlightClassification.ordinaryProjectedLight ? 1 : 0,
			flashlightClassification.projectedCSMEnabled ? 1 : 0,
			flashlightClassification.projectedCSMGateApplies ? 1 : 0,
			flashlightClassification.csmEnabled ? 1 : 0,
			flashlightClassification.cascadeCount,
			flashlightClassification.tileCount,
			flashlightClassification.atlasDiv );
		return false;
	}

	shadowMapProjectedLightState_t flashlightProjectedState;
	R_BuildShadowMapProjectedLightState( &flashlightLight, &view, descriptor->resolution, flashlightProjectedState );
	if ( !flashlightProjectedState.valid || flashlightProjectedState.requestedCascadeCount != 1 || flashlightProjectedState.cascadeCount != 1 || flashlightProjectedState.atlasDiv != 1 || flashlightProjectedState.cascadeFallback || flashlightProjectedState.fallbackReason != SHADOWMAP_PROJECTED_FALLBACK_NONE || flashlightProjectedState.cascadeFit[0].attempted ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: stock flashlight projected state valid=%d requested=%d cascades=%d atlasDiv=%d fallback=%d/%s fitAttempted=%d\n",
			flashlightProjectedState.valid ? 1 : 0,
			flashlightProjectedState.requestedCascadeCount,
			flashlightProjectedState.cascadeCount,
			flashlightProjectedState.atlasDiv,
			flashlightProjectedState.cascadeFallback ? 1 : 0,
			R_ShadowMapProjectedFallbackReasonName( flashlightProjectedState.fallbackReason ),
			flashlightProjectedState.cascadeFit[0].attempted ? 1 : 0 );
		return false;
	}

	shadowMapArb2ParityState_t flashlightArb2State;
	if ( !RB_ShadowMapBuildArb2ParityState( &flashlightLight, &view, descriptor->resolution, flashlightArb2State ) || !flashlightArb2State.valid || !flashlightArb2State.projectedStateReady || flashlightArb2State.csmEnabled || flashlightArb2State.requestedCascadeCount != 1 || flashlightArb2State.cascadeCount != 1 || flashlightArb2State.tileCount != 1 || flashlightArb2State.atlasDiv != 1 || flashlightArb2State.projectedCascadeFallback ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: stock flashlight ARB2 state valid=%d projectedReady=%d csm=%d requested=%d cascades=%d tiles=%d atlasDiv=%d fallback=%d/%d\n",
			flashlightArb2State.valid ? 1 : 0,
			flashlightArb2State.projectedStateReady ? 1 : 0,
			flashlightArb2State.csmEnabled ? 1 : 0,
			flashlightArb2State.requestedCascadeCount,
			flashlightArb2State.cascadeCount,
			flashlightArb2State.tileCount,
			flashlightArb2State.atlasDiv,
			flashlightArb2State.projectedCascadeFallback ? 1 : 0,
			flashlightArb2State.projectedFallbackCascade );
		return false;
	}

	shadowMapArb2ParityState_t arb2State;
	if ( !RB_ShadowMapBuildArb2ParityState( &light, &view, descriptor->resolution, arb2State ) || !arb2State.valid || !arb2State.projectedStateReady || !arb2State.csmEnabled || arb2State.cascadeCount != 3 || arb2State.tileCount != 3 || arb2State.atlasDiv != 2 || arb2State.projectedCascadeFallback ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: arb2 valid=%d projectedReady=%d csm=%d cascades=%d tiles=%d atlasDiv=%d fallback=%d/%d\n",
			arb2State.valid ? 1 : 0,
			arb2State.projectedStateReady ? 1 : 0,
			arb2State.csmEnabled ? 1 : 0,
			arb2State.cascadeCount,
			arb2State.tileCount,
			arb2State.atlasDiv,
			arb2State.projectedCascadeFallback ? 1 : 0,
			arb2State.projectedFallbackCascade );
		return false;
	}
	int parityChecks = 0;
	if ( !R_ModernShadowPlanner_CompareDescriptorToArb2Parity( "projected-diagnostic", descriptor, &light, &view, parityChecks ) || parityChecks != 1 ) {
		common->Printf( "RendererShadowProjectedDiagnostic self-test failed: ARB2 parity checks=%d\n", parityChecks );
		return false;
	}

	viewLight_t mixedWLight = light;
	idRenderLightLocal mixedWLightDef = lightDef;
	mixedWLightDef.index = 3102;
	mixedWLight.lightDef = &mixedWLightDef;
	mixedWLight.lightProject[2] = idPlane( 0.0f, 0.0f, 0.010f, 0.0f );
	shadowMapProjectedLightState_t mixedWState;
	R_BuildShadowMapProjectedLightState( &mixedWLight, &view, descriptor->resolution, mixedWState );
	const int mixedWFallbackCascade = idMath::ClampInt( 0, SHADOWMAP_PROJECTED_MAX_CASCADES - 1, mixedWState.fallbackCascade );
	const shadowMapProjectedCascadeFit_t &mixedWFit = mixedWState.cascadeFit[mixedWFallbackCascade];
	if ( !mixedWState.valid || !mixedWState.cascadeFallback || mixedWState.fallbackReason != SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS || mixedWState.cascadeCount != 1 || mixedWState.atlasDiv != 1 || mixedWState.fallbackCascade < 0 || !mixedWFit.attempted || !mixedWFit.mixedWSigns || mixedWFit.sampleCount != SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT || mixedWFit.validPoints + mixedWFit.skippedPoints != mixedWFit.sampleCount || mixedWFit.positiveWPoints <= 0 || mixedWFit.negativeWPoints <= 0 || mixedWFit.skippedPoints <= 0 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: mixed-W fallback valid=%d fallback=%d/%d reason=%s cascades=%d atlasDiv=%d fit(attempted=%d samples=%d valid=%d skipped=%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d collapsed=%d)\n",
			mixedWState.valid ? 1 : 0,
			mixedWState.cascadeFallback ? 1 : 0,
			mixedWState.fallbackCascade,
			R_ShadowMapProjectedFallbackReasonName( mixedWState.fallbackReason ),
			mixedWState.cascadeCount,
			mixedWState.atlasDiv,
			mixedWFit.attempted ? 1 : 0,
			mixedWFit.sampleCount,
			mixedWFit.validPoints,
			mixedWFit.skippedPoints,
			mixedWFit.positiveWPoints,
			mixedWFit.negativeWPoints,
			mixedWFit.nearZeroWPoints,
			mixedWFit.nanWPoints,
			mixedWFit.invalidNdcPoints,
			mixedWFit.mixedWSigns ? 1 : 0,
			mixedWFit.collapsedBounds ? 1 : 0 );
		return false;
	}
	shadowMapArb2ParityState_t mixedWArb2State;
	if ( !RB_ShadowMapBuildArb2ParityState( &mixedWLight, &view, descriptor->resolution, mixedWArb2State ) || !mixedWArb2State.valid || !mixedWArb2State.projectedCascadeFallback || mixedWArb2State.projectedState.fallbackReason != SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS || mixedWArb2State.cascadeCount != 1 || mixedWArb2State.atlasDiv != 1 ) {
		common->Printf(
			"RendererShadowProjectedDiagnostic self-test failed: mixed-W ARB2 fallback valid=%d fallback=%d/%d reason=%s cascades=%d atlasDiv=%d\n",
			mixedWArb2State.valid ? 1 : 0,
			mixedWArb2State.projectedCascadeFallback ? 1 : 0,
			mixedWArb2State.projectedFallbackCascade,
			R_ShadowMapProjectedFallbackReasonName( mixedWArb2State.projectedState.fallbackReason ),
			mixedWArb2State.cascadeCount,
			mixedWArb2State.atlasDiv );
		return false;
	}
	common->Printf(
		"SM projected-diagnostic fallbackValidation(light=%d reason=%s cascade=%d samples=%d valid=%d skipped=%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d arb2=%d)\n",
		mixedWLightDef.index,
		R_ShadowMapProjectedFallbackReasonName( mixedWState.fallbackReason ),
		mixedWState.fallbackCascade,
		mixedWFit.sampleCount,
		mixedWFit.validPoints,
		mixedWFit.skippedPoints,
		mixedWFit.positiveWPoints,
		mixedWFit.negativeWPoints,
		mixedWFit.nearZeroWPoints,
		mixedWFit.nanWPoints,
		mixedWFit.invalidNdcPoints,
		mixedWFit.mixedWSigns ? 1 : 0,
		mixedWArb2State.valid ? 1 : 0 );

	idVec4 localLightOrigin;
	idVec4 localViewOrigin;
	idPlane localLightProject[4];
	idPlane localShadowClip[4];
	idVec3 localPoint;
	R_GlobalPointToLocal( receiverSpace.modelMatrix, light.globalLightOrigin, localPoint );
	localLightOrigin.Set( localPoint.x, localPoint.y, localPoint.z, 1.0f );
	R_GlobalPointToLocal( receiverSpace.modelMatrix, view.renderView.vieworg, localPoint );
	localViewOrigin.Set( localPoint.x, localPoint.y, localPoint.z, 1.0f );
	for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
		R_GlobalPlaneToLocal( receiverSpace.modelMatrix, light.lightProject[planeIndex], localLightProject[planeIndex] );
		R_GlobalPlaneToLocal( receiverSpace.modelMatrix, arb2State.projectedState.clipPlanes[0][planeIndex], localShadowClip[planeIndex] );
	}
	const int atlasTextureSize = Max( 1, descriptor->resolution * descriptor->atlasDiv );
	const float shadowTexelSizeX = 1.0f / static_cast<float>( atlasTextureSize );
	const float shadowTexelSizeY = shadowTexelSizeX;
	const float cascadeBlend = idMath::ClampFloat( 0.0f, 0.5f, r_shadowMapCascadeBlend.GetFloat() );
	const int filterTaps = idMath::ClampInt( 1, 13, r_shadowMapFilterTaps.GetInteger() );
	const int filterMode = idMath::ClampInt( 0, 2, r_shadowMapFilterMode.GetInteger() );

	common->Printf(
		"SM projected-diagnostic scene=synthetic-flashlight light=%d shader='%s' classification=%s ordinary=%d projectedCSM=%d csm=%d planner(map=%s policy=%s cascades=%d tiles=%d atlasDiv=%d size=%d tile0=%.4f,%.4f,%.4f,%.4f fallback=%s) arb2(cascades=%d tiles=%d atlasDiv=%d size=%d fallback=%d/%d) projectedTransform(pad=%.3f scale=%.6f baseS0=%.6f rawS0=%.6f) clipPlane0=%.6f,%.6f,%.6f,%.6f atlas0=%.4f,%.4f,%.4f,%.4f split0=%.3f split1=%.3f\n",
		descriptor->lightDefIndex,
		projectedLightShader != NULL ? projectedLightShader->GetName() : "<null>",
		R_ShadowMapLightClassName( classification.lightClass ),
		classification.ordinaryProjectedLight ? 1 : 0,
		classification.projectedCSMEnabled ? 1 : 0,
		classification.csmEnabled ? 1 : 0,
		ModernShadowMapType_Name( descriptor->mapType ),
		ModernShadowPolicy_Name( descriptor->policy ),
		descriptor->cascadeCount,
		descriptor->tileCount,
		descriptor->atlasDiv,
		descriptor->resolution,
		descriptor->tileAtlasRect[0][0],
		descriptor->tileAtlasRect[0][1],
		descriptor->tileAtlasRect[0][2],
		descriptor->tileAtlasRect[0][3],
		ModernShadowFallbackReason_Name( descriptor->fallbackReason ),
		arb2State.cascadeCount,
		arb2State.tileCount,
		arb2State.atlasDiv,
		arb2State.tileSize,
		arb2State.projectedCascadeFallback ? 1 : 0,
		arb2State.projectedFallbackCascade,
		descriptor->projectionPad,
		descriptor->projectionScale,
		descriptor->projectedBaseClipPlanes[0][0],
		light.lightProject[0][0],
		arb2State.projectedState.clipPlanes[0][0][0],
		arb2State.projectedState.clipPlanes[0][0][1],
		arb2State.projectedState.clipPlanes[0][0][2],
		arb2State.projectedState.clipPlanes[0][0][3],
		arb2State.projectedState.atlasRect[0][0],
		arb2State.projectedState.atlasRect[0][1],
		arb2State.projectedState.atlasRect[0][2],
		arb2State.projectedState.atlasRect[0][3],
		arb2State.projectedState.splitDepths[0],
		arb2State.projectedState.splitDepths[1] );
	common->Printf(
		"SM projected-diagnostic receiverInputs(localLight=%.2f,%.2f,%.2f,%.1f localView=%.2f,%.2f,%.2f,%.1f lightS=%.6f,%.6f,%.6f,%.6f lightT=%.6f,%.6f,%.6f,%.6f lightQ=%.6f,%.6f,%.6f,%.6f falloff=%.6f,%.6f,%.6f,%.6f shadowRow0=%.6f,%.6f,%.6f,%.6f shadowRow1=%.6f,%.6f,%.6f,%.6f shadowRow2=%.6f,%.6f,%.6f,%.6f shadowRow3=%.6f,%.6f,%.6f,%.6f texel=%.7f,%.7f bias=%.7f normalBias=%.7f texelBias0=%.7f receiverPlaneBias=%.1f cascadeBias0=%.4f filterRadius=%.2f taps=%d mode=%d pcss=%.2f/%.2f cascadeBlend=%.2f cascadeCount=%d)\n",
		localLightOrigin[0],
		localLightOrigin[1],
		localLightOrigin[2],
		localLightOrigin[3],
		localViewOrigin[0],
		localViewOrigin[1],
		localViewOrigin[2],
		localViewOrigin[3],
		localLightProject[0][0],
		localLightProject[0][1],
		localLightProject[0][2],
		localLightProject[0][3],
		localLightProject[1][0],
		localLightProject[1][1],
		localLightProject[1][2],
		localLightProject[1][3],
		localLightProject[2][0],
		localLightProject[2][1],
		localLightProject[2][2],
		localLightProject[2][3],
		localLightProject[3][0],
		localLightProject[3][1],
		localLightProject[3][2],
		localLightProject[3][3],
		localShadowClip[0][0],
		localShadowClip[0][1],
		localShadowClip[0][2],
		localShadowClip[0][3],
		localShadowClip[1][0],
		localShadowClip[1][1],
		localShadowClip[1][2],
		localShadowClip[1][3],
		localShadowClip[2][0],
		localShadowClip[2][1],
		localShadowClip[2][2],
		localShadowClip[2][3],
		localShadowClip[3][0],
		localShadowClip[3][1],
		localShadowClip[3][2],
		localShadowClip[3][3],
		shadowTexelSizeX,
		shadowTexelSizeY,
		r_shadowMapBias.GetFloat(),
		r_shadowMapNormalBias.GetFloat(),
		arb2State.projectedState.texelDepthBias[0],
		r_shadowMapReceiverPlaneBias.GetBool() ? 1.0f : 0.0f,
		arb2State.projectedState.biasScale[0],
		r_shadowMapFilterRadius.GetFloat(),
		filterTaps,
		filterMode,
		r_shadowMapPCSSLightRadius.GetFloat(),
		r_shadowMapPCSSMaxRadius.GetFloat(),
		cascadeBlend,
		arb2State.projectedState.cascadeCount );
	common->Printf(
		"RendererShadowProjectedDiagnostic self-test passed (light=%d classification=%s cascades=%d atlasDiv=%d projectedGate=%d/%d/%d/%d projectedTransform(pad=%.3f scale=%.6f baseS0=%.6f rawS0=%.6f) sampleValidation(samples=%d valid=%d skipped=%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d reason=%s) fallbackValidation(reason=%s cascade=%d samples=%d valid=%d skipped=%d mixed=%d) parity=%d split0=%.3f split1=%.3f texel=%.7f status=%s)\n",
		descriptor->lightDefIndex,
		R_ShadowMapLightClassName( classification.lightClass ),
		descriptor->cascadeCount,
		descriptor->atlasDiv,
		stats.projectedCsmEligibleLights,
		stats.projectedCsmCascadeLights,
		stats.projectedCsmSuppressedLights,
		stats.nonProjectedCsmLights,
		descriptor->projectionPad,
		descriptor->projectionScale,
		descriptor->projectedBaseClipPlanes[0][0],
		light.lightProject[0][0],
		descriptor->projectedSampleCount,
		descriptor->projectedValidSampleCount,
		descriptor->projectedSkippedSampleCount,
		descriptor->projectedPositiveWPoints,
		descriptor->projectedNegativeWPoints,
		descriptor->projectedNearZeroWPoints,
		descriptor->projectedNanWPoints,
		descriptor->projectedInvalidNdcPoints,
		descriptor->projectedMixedWSignCascades,
		R_ShadowMapProjectedFallbackReasonName( descriptor->projectedFallbackReason ),
		R_ShadowMapProjectedFallbackReasonName( mixedWState.fallbackReason ),
		mixedWState.fallbackCascade,
		mixedWFit.sampleCount,
		mixedWFit.validPoints,
		mixedWFit.skippedPoints,
		mixedWFit.mixedWSigns ? 1 : 0,
		parityChecks,
		descriptor->cascadeSplitDepths[0],
		descriptor->cascadeSplitDepths[1],
		shadowTexelSizeX,
		stats.status );
	return true;
}
