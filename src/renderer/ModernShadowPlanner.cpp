// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernShadowPlanner.h"
#include "RendererBenchmarks.h"

const int MODERN_SHADOW_PLAN_MAX_LIGHTS = 1024;
const int MODERN_SHADOW_PLAN_MIN_SIZE = 128;
const int MODERN_SHADOW_PLAN_MAX_SIZE = 4096;

static renderBackendCaps_t rg_modernShadowPlannerCaps;
static renderFeatureSet_t rg_modernShadowPlannerFeatures;
static modernShadowPlannerStats_t rg_modernShadowPlannerStats;
static idList<modernShadowLightDescriptor_t> rg_modernShadowPlannerDescriptors;
static bool rg_modernShadowPlannerInitialized = false;

static void R_ModernShadowPlanner_SetStatus( modernShadowPlannerStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status != NULL ? status : "unknown", sizeof( stats.status ) );
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

static int R_ModernShadowPlanner_MaxMappedLights( const rendererBenchmarkBudget_t &budget ) {
	int maxLights = Max( 1, budget.lightBatchTarget / 16 );
	if ( rg_modernShadowPlannerFeatures.lowOverhead ) {
		maxLights = Max( maxLights, 8 );
	} else if ( rg_modernShadowPlannerFeatures.gpuDriven ) {
		maxLights = Max( maxLights, 6 );
	} else if ( rg_modernShadowPlannerFeatures.modernBaseline ) {
		maxLights = Max( maxLights, 4 );
	}
	return idMath::ClampInt( 1, 16, maxLights );
}

static int R_ModernShadowPlanner_MaxShadowPixels( int shadowMapSize, int maxMappedLights ) {
	const int tilePixels = shadowMapSize * shadowMapSize;
	return Max( tilePixels, tilePixels * maxMappedLights * 2 );
}

static bool R_ModernShadowPlanner_TranslucentMomentsAvailable( void ) {
	return r_shadowMapTranslucentMoments.GetBool()
		&& glConfig.GLSLProgramAvailable
		&& glConfig.maxTextureUnits >= 9
		&& glConfig.maxTextureImageUnits >= 9
		&& glConfig.maxDrawBuffers >= 3
		&& glConfig.maxColorAttachments >= 3;
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
	if ( !r_useShadowMap.GetBool() ) {
		return MODERN_SHADOW_FALLBACK_SHADOW_MAP_DISABLED;
	}
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return MODERN_SHADOW_FALLBACK_TEXTURE_LIMIT;
	}
	if ( vLight->pointLight && !glConfig.cubeMapAvailable ) {
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

static void R_ModernShadowPlanner_InitDescriptor( modernShadowLightDescriptor_t &descriptor, const viewLight_t *vLight, int sceneIndex, int descriptorIndex, int shadowMapSize, bool translucentAvailable ) {
	memset( &descriptor, 0, sizeof( descriptor ) );
	descriptor.viewLight = vLight;
	descriptor.descriptorIndex = descriptorIndex;
	descriptor.sceneIndex = sceneIndex;
	descriptor.lightDefIndex = vLight != NULL && vLight->lightDef != NULL ? vLight->lightDef->index : -1;
	descriptor.pointLight = vLight != NULL && vLight->pointLight;
	descriptor.parallel = vLight != NULL && vLight->parallel;
	descriptor.resolution = shadowMapSize;
	descriptor.updateModulo = Max( 1, RendererBenchmarks_CurrentBudget().shadowUpdateRate );
	descriptor.localCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->localShadows ) : 0;
	descriptor.globalCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalShadows ) : 0;
	descriptor.translucentCasterCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localTranslucentShadowMapCasters ) + R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalTranslucentShadowMapCasters ) : 0;
	descriptor.localReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->localInteractions ) : 0;
	descriptor.globalReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->globalInteractions ) : 0;
	descriptor.translucentReceiverCount = vLight != NULL ? R_ModernShadowPlanner_CountDrawSurfChain( vLight->translucentInteractions ) : 0;
	descriptor.translucentMoments = translucentAvailable && descriptor.translucentCasterCount > 0;
	descriptor.cascadeCount = 1;
	descriptor.tileCount = 1;
	descriptor.mapType = MODERN_SHADOW_MAP_PROJECTED;
	if ( descriptor.pointLight ) {
		descriptor.mapType = MODERN_SHADOW_MAP_POINT;
		descriptor.tileCount = 6;
	} else if ( r_shadowMapCSM.GetBool() ) {
		descriptor.mapType = MODERN_SHADOW_MAP_CASCADE;
		descriptor.cascadeCount = idMath::ClampInt( 1, 4, r_shadowMapCascadeCount.GetInteger() );
		descriptor.tileCount = descriptor.cascadeCount;
	}
	descriptor.estimatedPixels = descriptor.resolution * descriptor.resolution * Max( 1, descriptor.tileCount );
	const int scissorArea = vLight != NULL ? R_ModernShadowPlanner_ScissorArea( vLight->scissorRect ) : 0;
	descriptor.priority =
		scissorArea / 32
		+ ( descriptor.globalReceiverCount + descriptor.localReceiverCount ) * 512
		+ ( descriptor.globalCasterCount + descriptor.localCasterCount ) * 128
		+ ( descriptor.viewLight != NULL && descriptor.viewLight->viewInsideLight ? 1024 : 0 )
		+ ( descriptor.pointLight ? 256 : 0 )
		+ ( descriptor.parallel ? 128 : 0 );
	descriptor.fallbackReason = R_ModernShadowPlanner_SupportReason( vLight );
	if ( descriptor.fallbackReason == MODERN_SHADOW_FALLBACK_NONE ) {
		descriptor.policy = MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
	} else if ( R_ModernShadowPlanner_ReasonIsIntentionalSkip( descriptor.fallbackReason ) ) {
		descriptor.policy = MODERN_SHADOW_POLICY_SKIPPED;
	} else {
		descriptor.policy = R_ModernShadowPlanner_LightHasStencilFallback( descriptor ) ? MODERN_SHADOW_POLICY_STENCIL_FALLBACK : MODERN_SHADOW_POLICY_SKIPPED;
		descriptor.stencilFallback = descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
	}
}

static int R_ModernShadowPlanner_FindBestCandidate( const bool *selected ) {
	int best = -1;
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		if ( selected[i] ) {
			continue;
		}
		const modernShadowLightDescriptor_t &candidate = rg_modernShadowPlannerDescriptors[i];
		if ( candidate.fallbackReason != MODERN_SHADOW_FALLBACK_NONE ) {
			continue;
		}
		if ( best < 0 || candidate.priority > rg_modernShadowPlannerDescriptors[best].priority ) {
			best = i;
		}
	}
	return best;
}

static void R_ModernShadowPlanner_SelectMappedLights( modernShadowPlannerStats_t &stats ) {
	bool selected[MODERN_SHADOW_PLAN_MAX_LIGHTS];
	memset( selected, 0, sizeof( selected ) );
	int mappedLights = 0;
	int usedPixels = 0;
	for ( ;; ) {
		const int candidateIndex = R_ModernShadowPlanner_FindBestCandidate( selected );
		if ( candidateIndex < 0 ) {
			break;
		}
		selected[candidateIndex] = true;
		modernShadowLightDescriptor_t &candidate = rg_modernShadowPlannerDescriptors[candidateIndex];
		if ( mappedLights < stats.maxMappedLights && usedPixels + candidate.estimatedPixels <= stats.budgetedPixels ) {
			candidate.policy = MODERN_SHADOW_POLICY_MAPPED;
			candidate.fallbackReason = MODERN_SHADOW_FALLBACK_NONE;
			candidate.stencilFallback = false;
			const int atlasSlotsPerRow = Max( 1, static_cast<int>( idMath::Ceil( idMath::Sqrt( static_cast<float>( stats.maxMappedLights ) ) ) ) );
			const float slotScale = 1.0f / static_cast<float>( atlasSlotsPerRow );
			const int atlasSlot = mappedLights % Max( 1, atlasSlotsPerRow * atlasSlotsPerRow );
			candidate.atlasRect[0] = static_cast<float>( atlasSlot % atlasSlotsPerRow ) * slotScale;
			candidate.atlasRect[1] = static_cast<float>( atlasSlot / atlasSlotsPerRow ) * slotScale;
			candidate.atlasRect[2] = slotScale;
			candidate.atlasRect[3] = slotScale;
			mappedLights++;
			usedPixels += candidate.estimatedPixels;
			continue;
		}
		candidate.policy = R_ModernShadowPlanner_LightHasStencilFallback( candidate ) ? MODERN_SHADOW_POLICY_STENCIL_FALLBACK : MODERN_SHADOW_POLICY_SKIPPED;
		candidate.fallbackReason = MODERN_SHADOW_FALLBACK_BUDGET;
		candidate.stencilFallback = candidate.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK;
		stats.budgetThrottledLights++;
	}
}

static void R_ModernShadowPlanner_CountDescriptor( const modernShadowLightDescriptor_t &descriptor, modernShadowPlannerStats_t &stats ) {
	stats.descriptorCount++;
	stats.shadowRelevantLights++;
	stats.localCasterCount += descriptor.localCasterCount;
	stats.globalCasterCount += descriptor.globalCasterCount;
	stats.translucentCasterCount += descriptor.translucentCasterCount;
	stats.localReceiverCount += descriptor.localReceiverCount;
	stats.globalReceiverCount += descriptor.globalReceiverCount;
	stats.translucentReceiverCount += descriptor.translucentReceiverCount;
	if ( descriptor.pointLight ) {
		stats.pointLights++;
	} else {
		stats.projectedLights++;
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
		stats.mappedPasses += Max( 1, descriptor.tileCount );
		stats.estimatedPixels += descriptor.estimatedPixels;
	} else if ( descriptor.policy == MODERN_SHADOW_POLICY_STENCIL_FALLBACK ) {
		stats.fallbackLights++;
		stats.fallbackPasses += Max( 1, descriptor.tileCount );
		stats.stencilFallbackPasses += Max( 1, descriptor.tileCount );
	} else {
		stats.skippedLights++;
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
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		R_ModernShadowPlanner_CountDescriptor( rg_modernShadowPlannerDescriptors[i], stats );
	}
	stats.frameValid = true;
	if ( stats.overflow ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-overflow" );
	} else if ( stats.mappedLights > 0 && stats.fallbackLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-mixed" );
	} else if ( stats.mappedLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-mapped" );
	} else if ( stats.fallbackLights > 0 ) {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-fallback" );
	} else {
		R_ModernShadowPlanner_SetStatus( stats, "prepared-empty" );
	}
}

void R_ModernShadowPlanner_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	rg_modernShadowPlannerCaps = caps;
	rg_modernShadowPlannerFeatures = features;
	rg_modernShadowPlannerInitialized = true;
	rg_modernShadowPlannerDescriptors.Clear();
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	rg_modernShadowPlannerStats.initialized = true;
	rg_modernShadowPlannerStats.available = features.scenePackets;
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, features.scenePackets ? "initialized" : "unavailable" );
}

void R_ModernShadowPlanner_Shutdown( void ) {
	rg_modernShadowPlannerDescriptors.Clear();
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	memset( &rg_modernShadowPlannerCaps, 0, sizeof( rg_modernShadowPlannerCaps ) );
	memset( &rg_modernShadowPlannerFeatures, 0, sizeof( rg_modernShadowPlannerFeatures ) );
	rg_modernShadowPlannerInitialized = false;
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, "off" );
}

void R_ModernShadowPlanner_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested ) {
	const bool initialized = rg_modernShadowPlannerInitialized;
	const bool available = initialized && rg_modernShadowPlannerFeatures.scenePackets;
	rg_modernShadowPlannerDescriptors.SetNum( 0, false );
	memset( &rg_modernShadowPlannerStats, 0, sizeof( rg_modernShadowPlannerStats ) );
	rg_modernShadowPlannerStats.initialized = initialized;
	rg_modernShadowPlannerStats.available = available;
	rg_modernShadowPlannerStats.requested = requested;
	rg_modernShadowPlannerStats.shadowsEnabled = r_shadows.GetBool();
	rg_modernShadowPlannerStats.shadowMapsEnabled = r_useShadowMap.GetBool();
	rg_modernShadowPlannerStats.csmRequested = r_shadowMapCSM.GetBool();
	rg_modernShadowPlannerStats.translucentRequested = r_shadowMapTranslucentMoments.GetBool();
	rg_modernShadowPlannerStats.translucentEnabled = R_ModernShadowPlanner_TranslucentMomentsAvailable();
	rg_modernShadowPlannerStats.debugOverlayRequested = r_shadowMapDebugOverlay.GetInteger() > 0;
	rg_modernShadowPlannerStats.reportRequested = r_shadowMapReport.GetInteger() > 0;
	rg_modernShadowPlannerStats.sceneCount = packetFrame.NumScenes();
	rg_modernShadowPlannerStats.shadowMapSize = R_ModernShadowPlanner_BudgetedShadowMapSize();
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	rg_modernShadowPlannerStats.maxMappedLights = R_ModernShadowPlanner_MaxMappedLights( budget );
	rg_modernShadowPlannerStats.budgetedPixels = R_ModernShadowPlanner_MaxShadowPixels( rg_modernShadowPlannerStats.shadowMapSize, rg_modernShadowPlannerStats.maxMappedLights );
	rg_modernShadowPlannerStats.updateModulo = Max( 1, budget.shadowUpdateRate );
	R_ModernShadowPlanner_SetStatus( rg_modernShadowPlannerStats, requested ? "unavailable" : "off" );
	if ( !requested ) {
		return;
	}
	if ( !available ) {
		return;
	}

	const int startMsec = Sys_Milliseconds();
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
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
			modernShadowLightDescriptor_t descriptor;
			R_ModernShadowPlanner_InitDescriptor(
				descriptor,
				vLight,
				sceneIndex,
				rg_modernShadowPlannerDescriptors.Num(),
				rg_modernShadowPlannerStats.shadowMapSize,
				rg_modernShadowPlannerStats.translucentEnabled );
			rg_modernShadowPlannerDescriptors.Append( descriptor );
		}
		if ( rg_modernShadowPlannerStats.overflow ) {
			break;
		}
	}

	R_ModernShadowPlanner_SelectMappedLights( rg_modernShadowPlannerStats );
	rg_modernShadowPlannerStats.buildMsec = Sys_Milliseconds() - startMsec;
	R_ModernShadowPlanner_FinalizeStats( rg_modernShadowPlannerStats );

	if ( ( r_rendererMetrics.GetInteger() >= 2 || rg_modernShadowPlannerStats.reportRequested ) && requested ) {
		common->Printf(
			"modernShadowPlan status=%s requested=%d valid=%d scenes=%d lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d types(projected=%d point=%d parallel=%d csm=%d cascades=%d) casters(local=%d global=%d translucent=%d) receivers(local=%d global=%d translucent=%d) budget(lights=%d pixels=%d size=%d update=%d used=%d throttled=%d) cvars(shadows=%d shadowMap=%d csm=%d translucent=%d/%d debug=%d report=%d) fallbacks(texture=%d cubemap=%d noReceivers=%d) build=%dms\n",
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
			rg_modernShadowPlannerStats.pointLights,
			rg_modernShadowPlannerStats.parallelLights,
			rg_modernShadowPlannerStats.cascadeLights,
			rg_modernShadowPlannerStats.cascadeCount,
			rg_modernShadowPlannerStats.localCasterCount,
			rg_modernShadowPlannerStats.globalCasterCount,
			rg_modernShadowPlannerStats.translucentCasterCount,
			rg_modernShadowPlannerStats.localReceiverCount,
			rg_modernShadowPlannerStats.globalReceiverCount,
			rg_modernShadowPlannerStats.translucentReceiverCount,
			rg_modernShadowPlannerStats.maxMappedLights,
			rg_modernShadowPlannerStats.budgetedPixels,
			rg_modernShadowPlannerStats.shadowMapSize,
			rg_modernShadowPlannerStats.updateModulo,
			rg_modernShadowPlannerStats.estimatedPixels,
			rg_modernShadowPlannerStats.budgetThrottledLights,
			rg_modernShadowPlannerStats.shadowsEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.shadowMapsEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.csmRequested ? 1 : 0,
			rg_modernShadowPlannerStats.translucentRequested ? 1 : 0,
			rg_modernShadowPlannerStats.translucentEnabled ? 1 : 0,
			rg_modernShadowPlannerStats.debugOverlayRequested ? 1 : 0,
			rg_modernShadowPlannerStats.reportRequested ? 1 : 0,
			rg_modernShadowPlannerStats.textureLimitFallbacks,
			rg_modernShadowPlannerStats.cubemapFallbacks,
			rg_modernShadowPlannerStats.noReceiverLights,
			rg_modernShadowPlannerStats.buildMsec );
	}
}

const modernShadowPlannerStats_t &R_ModernShadowPlanner_Stats( void ) {
	return rg_modernShadowPlannerStats;
}

const modernShadowLightDescriptor_t *R_ModernShadowPlanner_DescriptorForLight( const viewLight_t *viewLight ) {
	if ( viewLight == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < rg_modernShadowPlannerDescriptors.Num(); ++i ) {
		if ( rg_modernShadowPlannerDescriptors[i].viewLight == viewLight ) {
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
		"Modern shadow plan: %s, requested=%d valid=%d scenes=%d lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d projected=%d point=%d csm=%d/%d casters(local=%d global=%d translucent=%d) receivers(local=%d global=%d translucent=%d) budget(lights=%d pixels=%d size=%d update=%d used=%d throttled=%d) cvars(shadows=%d shadowMap=%d csm=%d translucent=%d/%d debug=%d report=%d) build=%dms\n",
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
		rg_modernShadowPlannerStats.pointLights,
		rg_modernShadowPlannerStats.cascadeLights,
		rg_modernShadowPlannerStats.cascadeCount,
		rg_modernShadowPlannerStats.localCasterCount,
		rg_modernShadowPlannerStats.globalCasterCount,
		rg_modernShadowPlannerStats.translucentCasterCount,
		rg_modernShadowPlannerStats.localReceiverCount,
		rg_modernShadowPlannerStats.globalReceiverCount,
		rg_modernShadowPlannerStats.translucentReceiverCount,
		rg_modernShadowPlannerStats.maxMappedLights,
		rg_modernShadowPlannerStats.budgetedPixels,
		rg_modernShadowPlannerStats.shadowMapSize,
		rg_modernShadowPlannerStats.updateModulo,
		rg_modernShadowPlannerStats.estimatedPixels,
		rg_modernShadowPlannerStats.budgetThrottledLights,
		rg_modernShadowPlannerStats.shadowsEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.shadowMapsEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.csmRequested ? 1 : 0,
		rg_modernShadowPlannerStats.translucentRequested ? 1 : 0,
		rg_modernShadowPlannerStats.translucentEnabled ? 1 : 0,
		rg_modernShadowPlannerStats.debugOverlayRequested ? 1 : 0,
		rg_modernShadowPlannerStats.reportRequested ? 1 : 0,
		rg_modernShadowPlannerStats.buildMsec );
}

bool RendererShadowPlanner_RunSelfTest( void ) {
	if ( !rg_modernShadowPlannerInitialized || !rg_modernShadowPlannerFeatures.scenePackets ) {
		common->Printf( "RendererShadowPlanner self-test passed (planner unavailable)\n" );
		return true;
	}

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
	rendererShadowPlannerBoolCVarRestore_t restoreShadows( r_shadows );
	rendererShadowPlannerBoolCVarRestore_t restoreShadowMap( r_useShadowMap );
	rendererShadowPlannerBoolCVarRestore_t restoreCSM( r_shadowMapCSM );
	rendererShadowPlannerBoolCVarRestore_t restoreTranslucent( r_shadowMapTranslucentMoments );
	rendererShadowPlannerIntCVarRestore_t restoreCascadeCount( r_shadowMapCascadeCount );
	r_shadows.SetBool( true );
	r_useShadowMap.SetBool( true );
	r_shadowMapCSM.SetBool( true );
	r_shadowMapTranslucentMoments.SetBool( false );
	r_shadowMapCascadeCount.SetInteger( 3 );

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
	memset( lightDefs, 0, sizeof( lightDefs ) );
	const idMaterial *projectedLightShader = declManager != NULL ? declManager->FindMaterial( "lights/defaultProjectedLight" ) : tr.defaultMaterial;
	const idMaterial *pointLightShader = declManager != NULL ? declManager->FindMaterial( "lights/defaultPointLight" ) : tr.defaultMaterial;
	if ( projectedLightShader == NULL ) {
		projectedLightShader = tr.defaultMaterial;
	}
	if ( pointLightShader == NULL ) {
		pointLightShader = projectedLightShader;
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
		lights[i].parallel = i == 2;
		lights[i].globalShadowMapCasters = &casterSurfs[0];
		lights[i].localShadowMapCasters = &casterSurfs[3];
		lights[i].globalShadows = &casterSurfs[1];
		lights[i].localShadows = &casterSurfs[4];
		lights[i].globalInteractions = &receiverSurfs[0];
		lights[i].localInteractions = &receiverSurfs[3];
		lights[i].next = i + 1 < 6 ? &lights[i + 1] : NULL;
	}
	lightDefs[4].parms.noShadows = true;
	lights[5].globalInteractions = NULL;
	lights[5].localInteractions = NULL;
	lights[5].translucentInteractions = NULL;

	viewDef_t view;
	memset( &view, 0, sizeof( view ) );
	view.viewLights = &lights[0];
	idScenePacketFrame packetFrame;
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererShadowPlanner self-test failed: could not add scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	const modernShadowPlannerStats_t &stats = R_ModernShadowPlanner_Stats();
	if ( !stats.frameValid || stats.viewLightCount != 6 || stats.descriptorCount != 6 || stats.mappedLights <= 0 || stats.skippedLights < 2 || stats.cascadeLights <= 0 || stats.cascadeCount < 3 || stats.shadowMapSize <= 0 || stats.maxMappedLights <= 0 ) {
		common->Printf(
			"RendererShadowPlanner self-test failed: valid=%d lights=%d desc=%d mapped=%d fallback=%d skipped=%d csm=%d/%d size=%d budget=%d status=%s\n",
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
			stats.status );
		return false;
	}
	const modernShadowLightDescriptor_t *firstDescriptor = R_ModernShadowPlanner_DescriptorForLight( &lights[0] );
	if ( firstDescriptor == NULL || firstDescriptor->descriptorIndex < 0 || firstDescriptor->policy != MODERN_SHADOW_POLICY_MAPPED ) {
		common->Printf( "RendererShadowPlanner self-test failed: descriptor lookup mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererShadowPlanner self-test passed (lights=%d descriptors=%d mapped=%d fallback=%d skipped=%d csm=%d/%d size=%d budget=%d pixels=%d status=%s)\n",
		stats.viewLightCount,
		stats.descriptorCount,
		stats.mappedLights,
		stats.fallbackLights,
		stats.skippedLights,
		stats.cascadeLights,
		stats.cascadeCount,
		stats.shadowMapSize,
		stats.maxMappedLights,
		stats.estimatedPixels,
		stats.status );
	return true;
}
