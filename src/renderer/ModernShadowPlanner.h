// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_SHADOW_PLANNER_H__
#define __MODERN_SHADOW_PLANNER_H__

#include <cstdint>

#include "RendererCaps.h"
#include "ScenePackets.h"

enum modernShadowMapType_t {
	MODERN_SHADOW_MAP_NONE = 0,
	MODERN_SHADOW_MAP_PROJECTED,
	MODERN_SHADOW_MAP_POINT,
	MODERN_SHADOW_MAP_CASCADE
};

enum modernShadowPolicy_t {
	MODERN_SHADOW_POLICY_NONE = 0,
	MODERN_SHADOW_POLICY_MAPPED,
	MODERN_SHADOW_POLICY_CACHE_REUSE,
	MODERN_SHADOW_POLICY_STENCIL_FALLBACK,
	MODERN_SHADOW_POLICY_SKIPPED
};

enum modernShadowFallbackReason_t {
	MODERN_SHADOW_FALLBACK_NONE = 0,
	MODERN_SHADOW_FALLBACK_SHADOW_MAP_DISABLED,
	MODERN_SHADOW_FALLBACK_SHADOWS_DISABLED,
	MODERN_SHADOW_FALLBACK_NULL_LIGHT,
	MODERN_SHADOW_FALLBACK_NO_SHADOWS_FLAG,
	MODERN_SHADOW_FALLBACK_NO_DYNAMIC_SHADOWS_FLAG,
	MODERN_SHADOW_FALLBACK_AMBIENT_LIGHT,
	MODERN_SHADOW_FALLBACK_LIGHT_SHADER_NO_SHADOWS,
	MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT,
	MODERN_SHADOW_FALLBACK_NO_RECEIVERS,
	MODERN_SHADOW_FALLBACK_CUBEMAP_UNAVAILABLE,
	MODERN_SHADOW_FALLBACK_BUDGET,
	MODERN_SHADOW_FALLBACK_RESOURCE_UNAVAILABLE,
	MODERN_SHADOW_FALLBACK_RECEIVER_SAMPLING_UNAVAILABLE,
	MODERN_SHADOW_FALLBACK_DESCRIPTOR_INVARIANT,
	MODERN_SHADOW_FALLBACK_COUNT
};

enum modernShadowDepthFormat_t {
	MODERN_SHADOW_DEPTH_FORMAT_NONE = 0,
	MODERN_SHADOW_DEPTH_FORMAT_D24,
	MODERN_SHADOW_DEPTH_FORMAT_D32F,
	MODERN_SHADOW_DEPTH_FORMAT_PACKED_RGBA8
};

enum modernShadowCompareMode_t {
	MODERN_SHADOW_COMPARE_NONE = 0,
	MODERN_SHADOW_COMPARE_MANUAL_DEPTH,
	MODERN_SHADOW_COMPARE_MANUAL_PACKED_DEPTH,
	MODERN_SHADOW_COMPARE_HARDWARE
};

enum modernShadowBiasModel_t {
	MODERN_SHADOW_BIAS_NONE = 0,
	MODERN_SHADOW_BIAS_CONSTANT_NORMAL,
	MODERN_SHADOW_BIAS_CASCADE_SCALED,
	MODERN_SHADOW_BIAS_POINT_VECTOR
};

static const int MODERN_SHADOW_DESCRIPTOR_MAX_TILES = 6;
static const int MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES = 4;

enum modernShadowDescriptorInvariant_t {
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_NONE = 0,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_LIGHT_CLASS = 1u << 0,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_MAP_COUNTS = 1u << 1,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_TILES = 1u << 2,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_ATLAS = 1u << 3,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_SPLIT_DEPTHS = 1u << 4,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_PROJECTED_STATE = 1u << 5,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_POLICY = 1u << 6,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_ATLAS_SLOT = 1u << 7,
	MODERN_SHADOW_DESCRIPTOR_INVARIANT_FRESHNESS = 1u << 8
};

typedef struct modernShadowLightDescriptor_s {
	const viewLight_t *	viewLight;
	int					descriptorIndex;
	int					sceneIndex;
	int					lightDefIndex;
	modernShadowMapType_t mapType;
	modernShadowPolicy_t policy;
	modernShadowFallbackReason_t fallbackReason;
	int					priority;
	int					fairnessPriority;
	int					fairnessAge;
	int					fairnessBoost;
	int					budgetClass;
	unsigned int		budgetThrottleReasonMask;
	int					throttleHistoryMissStreak;
	int					throttleHistoryTotalMisses;
	int					throttleHistoryLastMissAge;
	int					throttleHistoryLastMappedAge;
	unsigned int		throttleHistoryReasonMask;
	unsigned int		throttleHistoryLastReasonMask;
	int					throttleHistoryLastBudgetClass;
	int					throttleHistoryLastPolicy;
	int					throttleHistoryLastFallbackReason;
	int					updateModulo;
	int					resolution;
	int					tileCount;
	int					cascadeCount;
	int					requestedCascadeCount;
	int					atlasDiv;
	int					estimatedPixels;
	int					localCasterCount;
	int					globalCasterCount;
	int					localReceiverCount;
	int					globalReceiverCount;
	int					translucentCasterCount;
	int					translucentReceiverCount;
	int					lodCasterTestCount;
	int					lodCasterRejectedCount;
	int					lodAlphaCasterRejectedCount;
	int					lodTranslucentCasterRejectedCount;
	float				atlasRect[4];
	float				tileAtlasRect[MODERN_SHADOW_DESCRIPTOR_MAX_TILES][4];
	float				shadowMatrix[16];
	float				projectionPad;
	float				projectionScale;
	float				projectedBaseClipPlanes[4][4];
	float				projectedClipPlanes[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][4][4];
	float				projectedAtlasRect[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][4];
	float				cascadeSplitDepths[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				cascadeBiasScale[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				texelDepthBias[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				worldTexelSize[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				sliceNear[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				sliceFar[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				depthRange[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				clipZExtent[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES];
	float				bias[4];
	float				normalOffsetScale;
	int					faceIndex;
	int					cascadeIndex;
	int					projectedFallbackCascade;
	int					projectedFallbackReason;
	// index + 1 of the cascade holding the light's whole map, 0 = none
	int					wholeLightCascadeSlot;
	int					projectedSampleCount;
	int					projectedValidSampleCount;
	int					projectedSkippedSampleCount;
	int					projectedPositiveWPoints;
	int					projectedNegativeWPoints;
	int					projectedNearZeroWPoints;
	int					projectedNanWPoints;
	int					projectedInvalidNdcPoints;
	int					projectedMixedWSignCascades;
	int					depthFormat;
	int					compareMode;
	int					biasModel;
	int					pcfKernel;
	int					updateFrame;
	int					casterCount;
	int					receiverCount;
	unsigned int		invariantFailureMask;
	int					receiverScissor[4];
	bool				pointLight;
	bool				parallel;
	bool				translucentMoments;
	bool				stencilFallback;
	bool				cacheReuseCandidate;
	bool				arb2CacheEstimateValid;
	bool				arb2CacheReuseAvailable;
	bool				arb2CacheFullyReusable;
	bool				arb2CacheBudgetIsolated;
	// resolved persistent-atlas placement (5c): true when the light's GLOBAL
	// cache entry holds live cells whose signature matches this frame's
	// content and no dynamic casters are missing from the static tiles -
	// the state in which modern receivers may sample the cell directly. The
	// storage generation closes the export-to-consumption recreation window.
	bool				arb2AtlasSlotReady;
	int					arb2AtlasCellX;
	int					arb2AtlasCellY;
	int					arb2AtlasCellSpan;
	int					arb2AtlasSignature;
	std::uint64_t		arb2AtlasStorageGeneration;
	int					arb2AtlasContentFrame;
	float				arb2AtlasCascadeRect[MODERN_SHADOW_DESCRIPTOR_MAX_CASCADES][4];
	// Exact provenance for the singular point cube bound by modern GL.  This
	// is separate from projected atlas placement because each cached point
	// light owns a distinct cube and only the currently selected one is bound.
	bool				arb2PointCubeReady;
	// A complete map rendered/copied before this frame's clustered lighting.
	// Point faces occupy consecutive tiles in a separate 2D atlas; projected
	// maps use arb2AtlasCascadeRect with the current-frame depth atlas binding.
	bool				currentFrameMapReady;
	int					currentPointFirstTile;
	int					currentPointAtlasColumns;
	int					arb2PointCubeSignature;
	int					arb2PointCubeContentFrame;
	bool				atlasTileReady;
	bool				casterPassReady;
	bool				cutoutCasterReady;
	bool				receiverGuardReady;
	bool				modernReceiverSamplingReady;
	bool				stableCascadeReady;
	bool				projectedStateReady;
	bool				projectedCascadeFallback;
	int					arb2ShadowPasses;
	int					arb2CacheablePasses;
	int					arb2CacheHitPasses;
	int					arb2CacheMissPasses;
	int					arb2FreshUpdatePasses;
	int					arb2BudgetFallbackPasses;
	int					arb2StencilOnlyPasses;
	int					arb2ReceiverFallbackPasses;
	int					arb2UnshadowedPasses;
	unsigned int		arb2CacheablePassMask;
	unsigned int		arb2CacheHitPassMask;
	unsigned int		arb2CacheKeyHitMask;
	unsigned int		arb2CacheMissPassMask;
	unsigned int		arb2FreshUpdatePassMask;
	unsigned int		arb2BudgetFallbackPassMask;
} modernShadowLightDescriptor_t;

typedef struct modernShadowPlannerStats_s {
	bool				initialized;
	bool				available;
	bool				requested;
	bool				frameValid;
	bool				shadowsEnabled;
	bool				shadowMapsEnabled;
	bool				csmRequested;
	bool				projectedCsmRequested;
	bool				translucentRequested;
	bool				translucentEnabled;
	bool				debugOverlayRequested;
	bool				reportRequested;
	bool				visibilityCasterCullingReady;
	bool				visibilityNoQueryStall;
	bool				modernReceiverSamplingReady;
	bool				overflow;
	int					sceneCount;
	int					viewLightCount;
	int					shadowRelevantLights;
	int					descriptorCount;
	int					mappedLights;
	int					fallbackLights;
	int					skippedLights;
	int					projectedLights;
	int					pointLights;
	int					parallelLights;
	int					ordinaryProjectedLights;
	int					cascadeLights;
	int					cascadeCount;
	int					projectedCsmEligibleLights;
	int					projectedCsmCascadeLights;
	int					projectedCsmSuppressedLights;
	int					nonProjectedCsmLights;
	int					mappedPasses;
	int					fallbackPasses;
	int					stencilFallbackPasses;
	int					localCasterCount;
	int					globalCasterCount;
	int					translucentCasterCount;
	int					localReceiverCount;
	int					globalReceiverCount;
	int					translucentReceiverCount;
	int					lodCasterTestCount;
	int					lodCasterRejectedCount;
	int					lodAlphaCasterRejectedCount;
	int					lodTranslucentCasterRejectedCount;
	int					lodRejectedLights;
	int					lodRejectedBudgetThrottledLights;
	int					lodRejectedUnmappedLights;
	int					estimatedPixels;
	int					budgetedPixels;
	int					maxMappedLights;
	int					shadowMapSize;
	int					updateModulo;
	int					atlasTiles;
	int					maxAtlasTiles;
	int					singleProjectedLightQuota;
	int					cascadeLightQuota;
	int					pointLightQuota;
	int					singleProjectedTileQuota;
	int					cascadeTileQuota;
	int					pointTileQuota;
	int					singleProjectedMappedLights;
	int					cascadeMappedLights;
	int					pointMappedLights;
	int					singleProjectedAtlasTiles;
	int					cascadeAtlasTiles;
	int					pointAtlasTiles;
	int					cutoutCasterLights;
	int					receiverGuardedLights;
	int					receiverSamplingBlockedLights;
	int					budgetThrottledLights;
	int					budgetFallbackLights;
	int					budgetStencilFallbackLights;
	int					budgetCacheReuseCandidateLights;
	int					budgetSkippedLights;
	unsigned int		budgetThrottleReasonMask;
	int					budgetThrottleClassLightLights;
	int					budgetThrottleClassTileLights;
	int					budgetThrottleClassPixelLights;
	int					budgetThrottleGlobalLightLights;
	int					budgetThrottleGlobalTileLights;
	int					budgetThrottleGlobalPixelLights;
	int					arb2CacheAwareLights;
	int					arb2CacheableLights;
	int					arb2CacheReuseLights;
	int					arb2AtlasSlotReadyLights;
	int					arb2CacheFullyReusableLights;
	int					arb2CacheBudgetIsolatedLights;
	int					arb2FreshUpdateLights;
	int					arb2BudgetFallbackLights;
	int					arb2ShadowPasses;
	int					arb2CacheablePasses;
	int					arb2CacheHitPasses;
	int					arb2CacheMissPasses;
	int					arb2FreshUpdatePasses;
	int					arb2BudgetFallbackPasses;
	int					arb2StencilOnlyPasses;
	int					arb2ReceiverFallbackPasses;
	int					arb2UnshadowedPasses;
	int					fairnessTrackedLights;
	int					fairnessAgedLights;
	int					fairnessBoostedLights;
	int					fairnessAgedMappedLights;
	int					fairnessMaxAge;
	int					fairnessMaxBoost;
	int					throttleHistoryTrackedLights;
	int					throttleHistoryRepeatedBudgetMissLights;
	int					throttleHistoryRecoveredLights;
	int					throttleHistoryMaxMissStreak;
	int					throttleHistoryMaxTotalMisses;
	int					throttleHistoryMaxMissLightDefIndex;
	int					throttleHistoryLastMissLightDefIndex;
	unsigned int		throttleHistoryLastMissReasonMask;
	int					throttleHistoryLastMissBudgetClass;
	int					singleProjectedBudgetThrottledLights;
	int					cascadeBudgetThrottledLights;
	int					pointBudgetThrottledLights;
	int					textureLimitFallbacks;
	int					cubemapFallbacks;
	int					noReceiverLights;
	int					descriptorInvariantFailures;
	unsigned int		descriptorInvariantFailureMask;
	int					visibilityReceiverScissorCulledLights;
	int					visibilityCasterTests;
	int					visibilityCasterRejected;
	int					visibilityCasterSavedDraws;
	int					buildMsec;
	char				status[96];
} modernShadowPlannerStats_t;

void R_ModernShadowPlanner_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernShadowPlanner_Shutdown( void );
void R_ModernShadowPlanner_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested );
void R_ModernShadowPlanner_PrepareReceiverMaps( const viewDef_t *viewDef );
const modernShadowPlannerStats_t &R_ModernShadowPlanner_Stats( void );
const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorForLight( const viewLight_t *viewLight );
const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorByIndex( int index );
int R_ModernShadowPlanner_NumDescriptors( void );
const char *ModernShadowMapType_Name( modernShadowMapType_t type );
const char *ModernShadowPolicy_Name( modernShadowPolicy_t policy );
const char *ModernShadowFallbackReason_Name( modernShadowFallbackReason_t reason );
void R_ModernShadowPlanner_PrintGfxInfo( void );
bool RendererShadowPlanner_RunSelfTest( void );
bool RendererShadowProjectedDiagnostic_RunSelfTest( void );
// shared by shadow self-tests: a light material guaranteed to cast shadows
// even in assetless runs (M5: no trivial-pass escape)
const idMaterial *R_ModernShadowPlanner_SelfTestLightShader( const char *materialName );

#endif /* !__MODERN_SHADOW_PLANNER_H__ */
