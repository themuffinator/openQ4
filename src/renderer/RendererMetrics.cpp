// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererBenchmarks.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"

typedef struct rendererMetricsFrame_s {
	int		frameCount;
	int		frontEndMsec;
	int		sceneExtractionMsec;
	int		visibilityMsec;
	int		packetBuildMsec;
	int		graphBuildMsec;
	int		submitMsec;
	int		backEndMsec;
	int		presentMsec;
	int		gpuMsec;
	int		draw3d;
	int		draw2d;
	int		setBuffers;
	int		swapBuffers;
	int		copyRenders;
	int		specialEffects;
	int		renderTargetOps;
	int		drawElements;
	int		surfaces;
	int		vertexes;
	int		indexes;
	int		visibleEntities;
	int		viewLights;
	int		views;
	int		uploadBytes;
	int		bufferStalls;
	int		scenePackets;
	int		passPackets;
	int		drawPackets;
	int		clippedDrawPackets;
	int		commandPackets;
	int		legacyDrawViews;
	bool	scenePacketsFrontEndDerived;
	bool	scenePacketsBackendDerived;
	bool	scenePacketOverflow;
	scenePacketOverflowCause_t scenePacketOverflowCause;
	int		materialRecords;
	int		geometryRecords;
	int		instanceRecords;
	int		drawPacketsWithMaterial;
	int		drawPacketsWithResourceRecord;
	int		drawPacketsWithGeometryRecord;
	int		drawPacketsWithInstanceRecord;
	int		drawPacketsWithGeometry;
	int		drawPacketsWithShaderRegisters;
	int		drawPacketsWithIndexCache;
	int		drawPacketsWithAmbientCache;
	int		worldPackets;
	int		subviewPackets;
	int		remoteCameraPackets;
	int		specialEffectPackets;
	int		viewmodelPackets;
	int		renderDemoPackets;
	int		guiPackets;
	int		postProcessPackets;
	int		presentPackets;
	int		commandOnlyPackets;
	int		sortKeyValidationFailures;
	int		renderGraphPasses;
	int		renderGraphPassPackets;
	int		renderGraphScenePackets;
	int		renderGraphDrawPackets;
	int		renderGraphCommandPackets;
	int		renderGraphResources;
	int		renderGraphImportedResources;
	int		renderGraphTransientResources;
	int		renderGraphAliasableTransientResources;
	int		renderGraphResourceAccesses;
	int		renderGraphReadAccesses;
	int		renderGraphWriteAccesses;
	int		renderGraphClearOps;
	int		renderGraphResolveOps;
	int		renderGraphInvalidateOps;
	int		renderGraphPresentOps;
	bool	renderGraphOverflow;
	renderGraphResourceManagerStats_t renderGraphResourceManager;
	materialResourceTableStats_t materialResourceTable;
	rendererModernExecutorMetricsMode_t modernExecutorMode;
	int		modernExecutorGraphPasses;
	int		modernExecutorPreparedPasses;
	int		modernExecutorFallbackPasses;
	int		modernExecutorPreparedDrawPackets;
	int		modernExecutorMaterialDrawPackets;
	int		modernExecutorResourceDrawPackets;
	int		modernExecutorGeometryDrawPackets;
	bool	modernExecutorVAOReady;
	bool	modernExecutorFrameUBOReady;
	bool	modernExecutorShaderLibraryReady;
	int		modernExecutorShaderProgramCount;
	int		modernExecutorShaderFailureCount;
	bool	modernExecutorDrawPlanReady;
	bool	modernExecutorDrawPlanOverflow;
	int		modernExecutorDrawPlanDraws;
	int		modernExecutorDrawPlanDepthDraws;
	int		modernExecutorDrawPlanMaterialDraws;
	int		modernExecutorDrawPlanFallbackDraws;
	int		modernExecutorDrawPlanStateBatches;
	int		modernExecutorDrawPlanProgramSwitches;
	int		modernExecutorDrawPlanMaterialSwitches;
	bool	modernExecutorSubmitPlanReady;
	bool	modernExecutorSubmitPlanOverflow;
	int		modernExecutorSubmitPlanDraws;
	int		modernExecutorSubmitPlanFallbackDraws;
	int		modernExecutorSubmitPlanDepthDraws;
	int		modernExecutorSubmitPlanMaterialDraws;
	int		modernExecutorSubmitPlanMissingAmbientDraws;
	int		modernExecutorSubmitPlanMissingIndexDraws;
	int		modernExecutorSubmitPlanIndexUploadDraws;
	bool	modernExecutorSubmitExecuted;
	int		modernExecutorSubmittedDraws;
	int		modernExecutorSubmittedFallbackDraws;
	int		modernExecutorSubmittedIndexUploadDraws;
	int		modernExecutorSubmitPlanProgramBatches;
	int		modernExecutorSubmitPlanVertexBufferBatches;
	int		modernExecutorSubmitPlanIndexBufferBatches;
	int		modernExecutorSubmitPlanScissorBatches;
	int		modernExecutorSubmitPlanMaterialBatches;
	int		modernExecutorSubmitPlanUniformUpdates;
	int		modernExecutorSubmitPlanFrameUBOBinds;
	bool	modernExecutorVisibleDepthRequested;
	bool	modernExecutorVisibleDepthExecuted;
	bool	modernExecutorVisibleDepthResourceReady;
	bool	modernExecutorVisibleShadowResourceReady;
	bool	modernExecutorVisibleDepthDebugOverlayReady;
	int		modernExecutorVisibleDepthDraws;
	int		modernExecutorVisibleDepthAlphaTestDraws;
	int		modernExecutorVisibleDepthSkinnedDraws;
	int		modernExecutorVisibleDepthFallbackDraws;
	int		modernExecutorVisibleShadowDepthDraws;
	int		modernExecutorVisibleShadowFallbackDraws;
	int		modernExecutorVisibleStencilShadowFallbackDraws;
	int		modernExecutorVisibleDepthMismatchDraws;
	int		modernExecutorVisibleDepthDebugOverlayDraws;
	bool	modernExecutorOpaqueGBufferRequested;
	bool	modernExecutorOpaqueGBufferExecuted;
	bool	modernExecutorOpaqueGBufferResourcesReady;
	bool	modernExecutorOpaqueGBufferMRTReady;
	bool	modernExecutorOpaqueGBufferDebugOverlayReady;
	int		modernExecutorOpaqueGBufferDraws;
	int		modernExecutorOpaqueGBufferFallbackDraws;
	int		modernExecutorOpaqueGBufferAttachmentCount;
	int		modernExecutorOpaqueGBufferBytesPerPixel;
	int		modernExecutorOpaqueGBufferBandwidthKB;
	int		modernExecutorOpaqueGBufferDebugOverlayDraws;
	bool	modernDeferredRequested;
	bool	modernDeferredExecuted;
	bool	modernDeferredResourcesReady;
	bool	modernDeferredOutputReady;
	bool	modernDeferredProgramReady;
	bool	modernDeferredClusterReady;
	bool	modernDeferredDebugOverlayReady;
	int		modernDeferredResolvedPixels;
	int		modernDeferredActiveLights;
	int		modernDeferredPointLights;
	int		modernDeferredProjectedLights;
	int		modernDeferredLightGridContributions;
	int		modernDeferredClusterReads;
	int		modernDeferredResourceFallbacks;
	int		modernDeferredUnsupportedLightFallbacks;
	int		modernDeferredFogFallbackLights;
	int		modernDeferredSpecialFallbackLights;
	int		modernDeferredOverflowClusters;
	int		modernDeferredClearOps;
	int		modernDeferredDebugMode;
	int		modernDeferredDebugOverlayDraws;
	bool	modernForwardRequested;
	bool	modernForwardExecuted;
	bool	modernForwardResourcesReady;
	bool	modernForwardSceneColorReady;
	bool	modernForwardSceneDepthReady;
	bool	modernForwardProgramReady;
	bool	modernForwardClusterReady;
	int		modernForwardDraws;
	int		modernForwardOpaqueDraws;
	int		modernForwardAlphaTestDraws;
	int		modernForwardTransparentDraws;
	int		modernForwardViewModelDraws;
	int		modernForwardFogBlendDraws;
	int		modernForwardSortedBatches;
	int		modernForwardFallbackDraws;
	int		modernForwardResourceFallbackDraws;
	int		modernForwardMaterialFallbackDraws;
	int		modernForwardGeometryFallbackDraws;
	int		modernForwardTextureFallbackDraws;
	int		modernForwardUnsupportedBlendFallbackDraws;
	int		modernForwardSpecialEffectFallbacks;
	int		modernForwardSortFallbackDraws;
	int		modernForwardOverdrawEstimate;
	int		modernForwardClusterReads;
	int		modernForwardActiveLights;
	int		modernForwardPointLights;
	int		modernForwardProjectedLights;
	int		modernForwardLightGridContributions;
	int		modernForwardClearOps;
	bool	modernVisibleRequested;
	bool	modernVisibleExecuted;
	bool	modernVisibleResourcesReady;
	bool	modernVisibleProgramReady;
	bool	modernVisibleSourceReady;
	bool	modernVisibleBackBufferReady;
	bool	modernVisibleHybridTargetReady;
	bool	modernVisibleShadowReady;
	bool	modernVisibleHDRTargetReady;
	bool	modernVisiblePostProcessHandoff;
	bool	modernVisibleBlockedByLegacy;
	int		modernVisibleCompositions;
	int		modernVisiblePixels;
	int		modernVisibleCompositeCopies;
	int		modernVisiblePostProcessCompositions;
	int		modernVisibleDepthCopies;
	int		modernVisibleModernPasses;
	int		modernVisibleLegacyPasses;
	int		modernVisibleDisabledPasses;
	int		modernVisibleFallbackPasses;
	int		modernVisibleOwnerFallbacks;
	int		modernVisibleResourceFallbacks;
	int		modernVisibleGuiLegacyPasses;
	int		modernVisiblePostLegacyPasses;
	int		modernVisibleSpecialLegacyPasses;
	int		modernVisibleSubviewLegacyPasses;
	int		modernVisiblePresentPasses;
	int		modernVisibleClearOps;
	bool	gpuDrivenRequested;
	bool	gpuDrivenExecuted;
	bool	gpuDrivenResourcesReady;
	bool	gpuDrivenValidationRequested;
	bool	gpuDrivenValidationReadbackReady;
	bool	gpuDrivenIndirectExecuted;
	bool	gpuDrivenMultiDrawReady;
	int		gpuDrivenSourceCommands;
	int		gpuDrivenEligibleCommands;
	int		gpuDrivenGeneratedCommands;
	int		gpuDrivenCulledCommands;
	int		gpuDrivenVisibleInstances;
	int		gpuDrivenCpuGeneratedCommands;
	int		gpuDrivenCpuCulledCommands;
	int		gpuDrivenCpuVisibleInstances;
	int		gpuDrivenGpuGeneratedCommands;
	int		gpuDrivenGpuCulledCommands;
	int		gpuDrivenGpuVisibleInstances;
	int		gpuDrivenCpuClusterBins;
	int		gpuDrivenGpuClusterBins;
	int		gpuDrivenValidationReadbacks;
	int		gpuDrivenValidationMismatches;
	int		gpuDrivenIndirectDrawCalls;
	int		gpuDrivenMultiDrawBatches;
	int		gpuDrivenIndirectFallbacks;
	int		gpuDrivenComputeDispatches;
	bool	lowOverheadRequested;
	bool	lowOverheadReady;
	bool	lowOverheadUsesDSA;
	bool	lowOverheadUsesMultiBind;
	bool	lowOverheadBindlessRequested;
	bool	lowOverheadBindlessAvailable;
	bool	lowOverheadSamplerReady;
	int		lowOverheadDSAUpdates;
	int		lowOverheadFramebufferDSAUpdates;
	int		lowOverheadSamplerDSACreations;
	int		lowOverheadSamplerDSAUpdates;
	int		lowOverheadBufferMultiBindBatches;
	int		lowOverheadTextureMultiBindBatches;
	int		lowOverheadSamplerMultiBindBatches;
	int		lowOverheadClassicTextureBinds;
	int		lowOverheadCompactedBatches;
	rendererClusteredLightingStats_t clusteredLighting;
	glStateCacheStats_t glStateCache;
	bool	gpuTimersValid;
	int		gpuTimerMsec[RENDERER_GPU_TIMER_COUNT];
	int		gpuTimerSamples[RENDERER_GPU_TIMER_COUNT];
	int		gpuTimerDroppedQueries;
} rendererMetricsFrame_t;

typedef struct rendererGpuTimerQuery_s {
	GLuint					id;
	rendererGpuTimerSlot_t	slot;
	bool					issued;
} rendererGpuTimerQuery_t;

typedef struct rendererGpuTimerFrame_s {
	rendererGpuTimerQuery_t	queries[128];
	int						numQueries;
	int						frameCount;
} rendererGpuTimerFrame_t;

typedef struct rendererGpuTimerLatest_s {
	bool	valid;
	int		msec[RENDERER_GPU_TIMER_COUNT];
	int		samples[RENDERER_GPU_TIMER_COUNT];
	int		droppedQueries;
} rendererGpuTimerLatest_t;

typedef struct rendererScenePacketLatest_s {
	int		scenePackets;
	int		passPackets;
	int		drawPackets;
	int		clippedDrawPackets;
	int		commandPackets;
	int		legacyDrawViews;
	int		materialRecords;
	int		drawPacketsWithMaterial;
	int		drawPacketsWithResourceRecord;
	int		drawPacketsWithGeometry;
	int		drawPacketsWithShaderRegisters;
	int		drawPacketsWithIndexCache;
	int		drawPacketsWithAmbientCache;
	int		geometryRecords;
	int		instanceRecords;
	int		drawPacketsWithGeometryRecord;
	int		drawPacketsWithInstanceRecord;
	int		worldPackets;
	int		subviewPackets;
	int		remoteCameraPackets;
	int		specialEffectPackets;
	int		viewmodelPackets;
	int		renderDemoPackets;
	int		guiPackets;
	int		postProcessPackets;
	int		presentPackets;
	int		commandOnlyPackets;
	int		sortKeyValidationFailures;
	bool	frontEndDerived;
	bool	backendDerived;
	bool	overflow;
	scenePacketOverflowCause_t overflowCause;
} rendererScenePacketLatest_t;

typedef struct rendererRenderGraphLatest_s {
	int		graphPasses;
	int		passPackets;
	int		scenePackets;
	int		drawPackets;
	int		commandPackets;
	int		resources;
	int		importedResources;
	int		transientResources;
	int		aliasableTransientResources;
	int		resourceAccesses;
	int		readAccesses;
	int		writeAccesses;
	int		clearOps;
	int		resolveOps;
	int		invalidateOps;
	int		presentOps;
	bool	overflow;
} rendererRenderGraphLatest_t;

typedef struct rendererModernExecutorLatest_s {
	rendererModernExecutorMetricsMode_t mode;
	int		graphPasses;
	int		preparedPasses;
	int		fallbackPasses;
	int		preparedDrawPackets;
	int		materialDrawPackets;
	int		resourceDrawPackets;
	int		geometryDrawPackets;
	bool	vaoReady;
	bool	frameUBOReady;
	bool	shaderLibraryReady;
	int		shaderProgramCount;
	int		shaderFailureCount;
	bool	drawPlanReady;
	bool	drawPlanOverflow;
	int		drawPlanDraws;
	int		drawPlanDepthDraws;
	int		drawPlanMaterialDraws;
	int		drawPlanFallbackDraws;
	int		drawPlanStateBatches;
	int		drawPlanProgramSwitches;
	int		drawPlanMaterialSwitches;
	bool	submitPlanReady;
	bool	submitPlanOverflow;
	int		submitPlanDraws;
	int		submitPlanFallbackDraws;
	int		submitPlanDepthDraws;
	int		submitPlanMaterialDraws;
	int		submitPlanMissingAmbientDraws;
	int		submitPlanMissingIndexDraws;
	int		submitPlanIndexUploadDraws;
	bool	submitExecuted;
	int		submittedDraws;
	int		submittedFallbackDraws;
	int		submittedIndexUploadDraws;
	int		submitPlanProgramBatches;
	int		submitPlanVertexBufferBatches;
	int		submitPlanIndexBufferBatches;
	int		submitPlanScissorBatches;
	int		submitPlanMaterialBatches;
	int		submitPlanUniformUpdates;
	int		submitPlanFrameUBOBinds;
	bool	visibleDepthRequested;
	bool	visibleDepthExecuted;
	bool	visibleDepthResourceReady;
	bool	visibleShadowResourceReady;
	bool	visibleDepthDebugOverlayReady;
	int		visibleDepthDraws;
	int		visibleDepthAlphaTestDraws;
	int		visibleDepthSkinnedDraws;
	int		visibleDepthFallbackDraws;
	int		visibleShadowDepthDraws;
	int		visibleShadowFallbackDraws;
	int		visibleStencilShadowFallbackDraws;
	int		visibleDepthMismatchDraws;
	int		visibleDepthDebugOverlayDraws;
	bool	opaqueGBufferRequested;
	bool	opaqueGBufferExecuted;
	bool	opaqueGBufferResourcesReady;
	bool	opaqueGBufferMRTReady;
	bool	opaqueGBufferDebugOverlayReady;
	int		opaqueGBufferDraws;
	int		opaqueGBufferFallbackDraws;
	int		opaqueGBufferAttachmentCount;
	int		opaqueGBufferBytesPerPixel;
	int		opaqueGBufferBandwidthKB;
	int		opaqueGBufferDebugOverlayDraws;
} rendererModernExecutorLatest_t;

typedef struct rendererDeferredResolveLatest_s {
	bool	requested;
	bool	executed;
	bool	resourcesReady;
	bool	outputReady;
	bool	programReady;
	bool	clusterReady;
	bool	debugOverlayReady;
	int		resolvedPixels;
	int		activeLights;
	int		pointLights;
	int		projectedLights;
	int		lightGridContributions;
	int		clusterReads;
	int		resourceFallbacks;
	int		unsupportedLightFallbacks;
	int		fogFallbackLights;
	int		specialFallbackLights;
	int		overflowClusters;
	int		clearOps;
	int		debugMode;
	int		debugOverlayDraws;
} rendererDeferredResolveLatest_t;

typedef struct rendererForwardPlusLatest_s {
	bool	requested;
	bool	executed;
	bool	resourcesReady;
	bool	sceneColorReady;
	bool	sceneDepthReady;
	bool	programReady;
	bool	clusterReady;
	int		draws;
	int		opaqueDraws;
	int		alphaTestDraws;
	int		transparentDraws;
	int		viewModelDraws;
	int		fogBlendDraws;
	int		sortedBatches;
	int		fallbackDraws;
	int		resourceFallbackDraws;
	int		materialFallbackDraws;
	int		geometryFallbackDraws;
	int		textureFallbackDraws;
	int		unsupportedBlendFallbackDraws;
	int		specialEffectFallbacks;
	int		sortFallbackDraws;
	int		overdrawEstimate;
	int		clusterReads;
	int		activeLights;
	int		pointLights;
	int		projectedLights;
	int		lightGridContributions;
	int		clearOps;
} rendererForwardPlusLatest_t;

typedef struct rendererModernVisibleLatest_s {
	bool	requested;
	bool	executed;
	bool	resourcesReady;
	bool	programReady;
	bool	sourceReady;
	bool	backBufferReady;
	bool	hybridTargetReady;
	bool	shadowReady;
	bool	hdrTargetReady;
	bool	postProcessHandoff;
	bool	blockedByLegacy;
	int		compositions;
	int		pixels;
	int		compositeCopies;
	int		postProcessCompositions;
	int		depthCopies;
	int		modernPasses;
	int		legacyPasses;
	int		disabledPasses;
	int		fallbackPasses;
	int		ownerFallbacks;
	int		resourceFallbacks;
	int		guiLegacyPasses;
	int		postLegacyPasses;
	int		specialLegacyPasses;
	int		subviewLegacyPasses;
	int		presentPasses;
	int		clearOps;
} rendererModernVisibleLatest_t;

typedef struct rendererGpuDrivenLatest_s {
	bool	requested;
	bool	executed;
	bool	resourcesReady;
	bool	validationRequested;
	bool	validationReadbackReady;
	bool	indirectExecuted;
	bool	multiDrawReady;
	int		sourceCommands;
	int		eligibleCommands;
	int		generatedCommands;
	int		culledCommands;
	int		visibleInstances;
	int		cpuGeneratedCommands;
	int		cpuCulledCommands;
	int		cpuVisibleInstances;
	int		gpuGeneratedCommands;
	int		gpuCulledCommands;
	int		gpuVisibleInstances;
	int		cpuClusterBins;
	int		gpuClusterBins;
	int		validationReadbacks;
	int		validationMismatches;
	int		indirectDrawCalls;
	int		multiDrawBatches;
	int		indirectFallbacks;
	int		computeDispatches;
} rendererGpuDrivenLatest_t;

typedef struct rendererLowOverheadLatest_s {
	bool	requested;
	bool	ready;
	bool	usesDSA;
	bool	usesMultiBind;
	bool	bindlessRequested;
	bool	bindlessAvailable;
	bool	samplerReady;
	int		dsaUpdates;
	int		framebufferDSAUpdates;
	int		samplerDSACreations;
	int		samplerDSAUpdates;
	int		bufferMultiBindBatches;
	int		textureMultiBindBatches;
	int		samplerMultiBindBatches;
	int		classicTextureBinds;
	int		compactedBatches;
} rendererLowOverheadLatest_t;

typedef struct rendererGLStateCacheLatest_s {
	glStateCacheStats_t stats;
} rendererGLStateCacheLatest_t;

static rendererMetricsFrame_t rg_rendererMetrics;
static int rg_rendererMetricsLastSummaryFrame = -1;
static rendererGpuTimerFrame_t rg_gpuTimerFrames[4];
static rendererGpuTimerLatest_t rg_gpuTimerLatest;
static rendererScenePacketLatest_t rg_scenePacketLatest;
static rendererRenderGraphLatest_t rg_renderGraphLatest;
static renderGraphResourceManagerStats_t rg_renderGraphResourceLatest;
static materialResourceTableStats_t rg_materialResourceTableLatest;
static rendererModernExecutorLatest_t rg_modernExecutorLatest;
static rendererDeferredResolveLatest_t rg_deferredResolveLatest;
static rendererForwardPlusLatest_t rg_forwardPlusLatest;
static rendererModernVisibleLatest_t rg_modernVisibleLatest;
static rendererGpuDrivenLatest_t rg_gpuDrivenLatest;
static rendererLowOverheadLatest_t rg_lowOverheadLatest;
static rendererClusteredLightingStats_t rg_clusteredLightingLatest;
static rendererGLStateCacheLatest_t rg_glStateCacheLatest;
static int rg_gpuTimerFrameCursor = 0;
static int rg_gpuTimerBackendFrameCount = 0;
static bool rg_gpuTimerQueryActive = false;
static rendererGpuTimerQuery_t *rg_gpuTimerActiveQuery = NULL;
static bool rg_gpuTimerOverflowThisFrame = false;

static const char *R_RendererMetrics_GpuTimerSlotName( rendererGpuTimerSlot_t slot ) {
	switch ( slot ) {
	case RENDERER_GPU_TIMER_SET_BUFFER:
		return "setbuf";
	case RENDERER_GPU_TIMER_DRAW3D:
		return "3d";
	case RENDERER_GPU_TIMER_DRAW2D:
		return "2d";
	case RENDERER_GPU_TIMER_SPECIAL_EFFECTS:
		return "special";
	case RENDERER_GPU_TIMER_RENDER_TARGET:
		return "rt";
	case RENDERER_GPU_TIMER_COPY_RENDER:
		return "copy";
	case RENDERER_GPU_TIMER_SWAP_BUFFERS:
		return "swap";
	case RENDERER_GPU_TIMER_MODERN_DEFERRED:
		return "modernDeferred";
	case RENDERER_GPU_TIMER_MODERN_FORWARD:
		return "modernForward";
	case RENDERER_GPU_TIMER_MODERN_COMPOSITE:
		return "modernComposite";
	case RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT:
		return "gpuDrivenIndirect";
	default:
		return "unknown";
	}
}

static const char *R_RendererMetrics_ModernExecutorModeName( rendererModernExecutorMetricsMode_t mode ) {
	switch ( mode ) {
	case RENDERER_MODERN_EXECUTOR_METRICS_OFF:
		return "off";
	case RENDERER_MODERN_EXECUTOR_METRICS_UNAVAILABLE:
		return "unavailable";
	case RENDERER_MODERN_EXECUTOR_METRICS_PREPARED:
		return "prepared";
	case RENDERER_MODERN_EXECUTOR_METRICS_LEGACY_FALLBACK:
		return "legacyFallback";
	default:
		return "unknown";
	}
}

static const char *R_RendererMetrics_ScenePacketSourceName( const rendererMetricsFrame_t &metrics ) {
	if ( metrics.scenePacketsFrontEndDerived ) {
		return "frontend";
	}
	if ( metrics.scenePacketsBackendDerived ) {
		return "backend";
	}
	return "none";
}

static bool R_RendererMetrics_GpuTimersEnabled( void ) {
	if ( r_rendererMetrics.GetInteger() <= 0 || !r_rendererGpuTimers.GetBool() ) {
		return false;
	}
	return R_RendererMetrics_GpuTimersAvailable();
}

static int R_RendererMetrics_TotalGpuMsec( const rendererMetricsFrame_t &metrics ) {
	if ( !metrics.gpuTimersValid ) {
		return -1;
	}
	int total = 0;
	for ( int i = 0; i < RENDERER_GPU_TIMER_COUNT; ++i ) {
		total += metrics.gpuTimerMsec[i];
	}
	return total;
}

static const char *R_RendererMetrics_FormatGpuMsec( const rendererMetricsFrame_t &metrics, char *buffer, int bufferSize ) {
	if ( buffer == NULL || bufferSize <= 0 ) {
		return "";
	}
	if ( !metrics.gpuTimersValid ) {
		idStr::snPrintf( buffer, bufferSize, "not-sampled" );
		return buffer;
	}
	idStr::snPrintf( buffer, bufferSize, "%dms", R_RendererMetrics_TotalGpuMsec( metrics ) );
	return buffer;
}

static void R_RendererMetrics_PollGpuTimerFrame( rendererGpuTimerFrame_t &frame ) {
	if ( frame.numQueries <= 0 ) {
		return;
	}

	rendererGpuTimerLatest_t latest;
	memset( &latest, 0, sizeof( latest ) );
	latest.valid = true;

	for ( int i = 0; i < frame.numQueries; ++i ) {
		const rendererGpuTimerQuery_t &query = frame.queries[i];
		if ( !query.issued || query.id == 0 ) {
			continue;
		}

		GLint available = 0;
		glGetQueryObjectiv( query.id, GL_QUERY_RESULT_AVAILABLE, &available );
		if ( !available ) {
			latest.valid = false;
			latest.droppedQueries = frame.numQueries;
			rg_gpuTimerLatest = latest;
			frame.numQueries = 0;
			return;
		}
	}

	for ( int i = 0; i < frame.numQueries; ++i ) {
		const rendererGpuTimerQuery_t &query = frame.queries[i];
		if ( !query.issued || query.id == 0 ) {
			continue;
		}

		GLuint64 elapsedNsec = 0;
		if ( glGetQueryObjectui64v != NULL ) {
			glGetQueryObjectui64v( query.id, GL_QUERY_RESULT, &elapsedNsec );
		} else {
			GLuint elapsedNsec32 = 0;
			glGetQueryObjectuiv( query.id, GL_QUERY_RESULT, &elapsedNsec32 );
			elapsedNsec = elapsedNsec32;
		}

		const int elapsedMsec = static_cast<int>( ( elapsedNsec + 500000ULL ) / 1000000ULL );
		latest.msec[query.slot] += elapsedMsec;
		latest.samples[query.slot]++;
	}

	rg_gpuTimerLatest = latest;
	frame.numQueries = 0;
}

void R_RendererMetrics_BeginFrame( int frameCount ) {
	memset( &rg_rendererMetrics, 0, sizeof( rg_rendererMetrics ) );
	rg_rendererMetrics.frameCount = frameCount;
	rg_rendererMetrics.gpuTimersValid = rg_gpuTimerLatest.valid;
	rg_rendererMetrics.gpuTimerDroppedQueries = rg_gpuTimerLatest.droppedQueries;
	for ( int i = 0; i < RENDERER_GPU_TIMER_COUNT; ++i ) {
		rg_rendererMetrics.gpuTimerMsec[i] = rg_gpuTimerLatest.msec[i];
		rg_rendererMetrics.gpuTimerSamples[i] = rg_gpuTimerLatest.samples[i];
	}
	rg_rendererMetrics.scenePackets = rg_scenePacketLatest.scenePackets;
	rg_rendererMetrics.passPackets = rg_scenePacketLatest.passPackets;
	rg_rendererMetrics.drawPackets = rg_scenePacketLatest.drawPackets;
	rg_rendererMetrics.clippedDrawPackets = rg_scenePacketLatest.clippedDrawPackets;
	rg_rendererMetrics.commandPackets = rg_scenePacketLatest.commandPackets;
	rg_rendererMetrics.legacyDrawViews = rg_scenePacketLatest.legacyDrawViews;
	rg_rendererMetrics.scenePacketsFrontEndDerived = rg_scenePacketLatest.frontEndDerived;
	rg_rendererMetrics.scenePacketsBackendDerived = rg_scenePacketLatest.backendDerived;
	rg_rendererMetrics.scenePacketOverflow = rg_scenePacketLatest.overflow;
	rg_rendererMetrics.scenePacketOverflowCause = rg_scenePacketLatest.overflowCause;
	rg_rendererMetrics.materialRecords = rg_scenePacketLatest.materialRecords;
	rg_rendererMetrics.geometryRecords = rg_scenePacketLatest.geometryRecords;
	rg_rendererMetrics.instanceRecords = rg_scenePacketLatest.instanceRecords;
	rg_rendererMetrics.drawPacketsWithMaterial = rg_scenePacketLatest.drawPacketsWithMaterial;
	rg_rendererMetrics.drawPacketsWithResourceRecord = rg_scenePacketLatest.drawPacketsWithResourceRecord;
	rg_rendererMetrics.drawPacketsWithGeometryRecord = rg_scenePacketLatest.drawPacketsWithGeometryRecord;
	rg_rendererMetrics.drawPacketsWithInstanceRecord = rg_scenePacketLatest.drawPacketsWithInstanceRecord;
	rg_rendererMetrics.drawPacketsWithGeometry = rg_scenePacketLatest.drawPacketsWithGeometry;
	rg_rendererMetrics.drawPacketsWithShaderRegisters = rg_scenePacketLatest.drawPacketsWithShaderRegisters;
	rg_rendererMetrics.drawPacketsWithIndexCache = rg_scenePacketLatest.drawPacketsWithIndexCache;
	rg_rendererMetrics.drawPacketsWithAmbientCache = rg_scenePacketLatest.drawPacketsWithAmbientCache;
	rg_rendererMetrics.worldPackets = rg_scenePacketLatest.worldPackets;
	rg_rendererMetrics.subviewPackets = rg_scenePacketLatest.subviewPackets;
	rg_rendererMetrics.remoteCameraPackets = rg_scenePacketLatest.remoteCameraPackets;
	rg_rendererMetrics.specialEffectPackets = rg_scenePacketLatest.specialEffectPackets;
	rg_rendererMetrics.viewmodelPackets = rg_scenePacketLatest.viewmodelPackets;
	rg_rendererMetrics.renderDemoPackets = rg_scenePacketLatest.renderDemoPackets;
	rg_rendererMetrics.guiPackets = rg_scenePacketLatest.guiPackets;
	rg_rendererMetrics.postProcessPackets = rg_scenePacketLatest.postProcessPackets;
	rg_rendererMetrics.presentPackets = rg_scenePacketLatest.presentPackets;
	rg_rendererMetrics.commandOnlyPackets = rg_scenePacketLatest.commandOnlyPackets;
	rg_rendererMetrics.sortKeyValidationFailures = rg_scenePacketLatest.sortKeyValidationFailures;
	rg_rendererMetrics.renderGraphPasses = rg_renderGraphLatest.graphPasses;
	rg_rendererMetrics.renderGraphPassPackets = rg_renderGraphLatest.passPackets;
	rg_rendererMetrics.renderGraphScenePackets = rg_renderGraphLatest.scenePackets;
	rg_rendererMetrics.renderGraphDrawPackets = rg_renderGraphLatest.drawPackets;
	rg_rendererMetrics.renderGraphCommandPackets = rg_renderGraphLatest.commandPackets;
	rg_rendererMetrics.renderGraphResources = rg_renderGraphLatest.resources;
	rg_rendererMetrics.renderGraphImportedResources = rg_renderGraphLatest.importedResources;
	rg_rendererMetrics.renderGraphTransientResources = rg_renderGraphLatest.transientResources;
	rg_rendererMetrics.renderGraphAliasableTransientResources = rg_renderGraphLatest.aliasableTransientResources;
	rg_rendererMetrics.renderGraphResourceAccesses = rg_renderGraphLatest.resourceAccesses;
	rg_rendererMetrics.renderGraphReadAccesses = rg_renderGraphLatest.readAccesses;
	rg_rendererMetrics.renderGraphWriteAccesses = rg_renderGraphLatest.writeAccesses;
	rg_rendererMetrics.renderGraphClearOps = rg_renderGraphLatest.clearOps;
	rg_rendererMetrics.renderGraphResolveOps = rg_renderGraphLatest.resolveOps;
	rg_rendererMetrics.renderGraphInvalidateOps = rg_renderGraphLatest.invalidateOps;
	rg_rendererMetrics.renderGraphPresentOps = rg_renderGraphLatest.presentOps;
	rg_rendererMetrics.renderGraphOverflow = rg_renderGraphLatest.overflow;
	rg_rendererMetrics.renderGraphResourceManager = rg_renderGraphResourceLatest;
	rg_rendererMetrics.materialResourceTable = rg_materialResourceTableLatest;
	rg_rendererMetrics.modernExecutorMode = rg_modernExecutorLatest.mode;
	rg_rendererMetrics.modernExecutorGraphPasses = rg_modernExecutorLatest.graphPasses;
	rg_rendererMetrics.modernExecutorPreparedPasses = rg_modernExecutorLatest.preparedPasses;
	rg_rendererMetrics.modernExecutorFallbackPasses = rg_modernExecutorLatest.fallbackPasses;
	rg_rendererMetrics.modernExecutorPreparedDrawPackets = rg_modernExecutorLatest.preparedDrawPackets;
	rg_rendererMetrics.modernExecutorMaterialDrawPackets = rg_modernExecutorLatest.materialDrawPackets;
	rg_rendererMetrics.modernExecutorResourceDrawPackets = rg_modernExecutorLatest.resourceDrawPackets;
	rg_rendererMetrics.modernExecutorGeometryDrawPackets = rg_modernExecutorLatest.geometryDrawPackets;
	rg_rendererMetrics.modernExecutorVAOReady = rg_modernExecutorLatest.vaoReady;
	rg_rendererMetrics.modernExecutorFrameUBOReady = rg_modernExecutorLatest.frameUBOReady;
	rg_rendererMetrics.modernExecutorShaderLibraryReady = rg_modernExecutorLatest.shaderLibraryReady;
	rg_rendererMetrics.modernExecutorShaderProgramCount = rg_modernExecutorLatest.shaderProgramCount;
	rg_rendererMetrics.modernExecutorShaderFailureCount = rg_modernExecutorLatest.shaderFailureCount;
	rg_rendererMetrics.modernExecutorDrawPlanReady = rg_modernExecutorLatest.drawPlanReady;
	rg_rendererMetrics.modernExecutorDrawPlanOverflow = rg_modernExecutorLatest.drawPlanOverflow;
	rg_rendererMetrics.modernExecutorDrawPlanDraws = rg_modernExecutorLatest.drawPlanDraws;
	rg_rendererMetrics.modernExecutorDrawPlanDepthDraws = rg_modernExecutorLatest.drawPlanDepthDraws;
	rg_rendererMetrics.modernExecutorDrawPlanMaterialDraws = rg_modernExecutorLatest.drawPlanMaterialDraws;
	rg_rendererMetrics.modernExecutorDrawPlanFallbackDraws = rg_modernExecutorLatest.drawPlanFallbackDraws;
	rg_rendererMetrics.modernExecutorDrawPlanStateBatches = rg_modernExecutorLatest.drawPlanStateBatches;
	rg_rendererMetrics.modernExecutorDrawPlanProgramSwitches = rg_modernExecutorLatest.drawPlanProgramSwitches;
	rg_rendererMetrics.modernExecutorDrawPlanMaterialSwitches = rg_modernExecutorLatest.drawPlanMaterialSwitches;
	rg_rendererMetrics.modernExecutorSubmitPlanReady = rg_modernExecutorLatest.submitPlanReady;
	rg_rendererMetrics.modernExecutorSubmitPlanOverflow = rg_modernExecutorLatest.submitPlanOverflow;
	rg_rendererMetrics.modernExecutorSubmitPlanDraws = rg_modernExecutorLatest.submitPlanDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanFallbackDraws = rg_modernExecutorLatest.submitPlanFallbackDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanDepthDraws = rg_modernExecutorLatest.submitPlanDepthDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanMaterialDraws = rg_modernExecutorLatest.submitPlanMaterialDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanMissingAmbientDraws = rg_modernExecutorLatest.submitPlanMissingAmbientDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanMissingIndexDraws = rg_modernExecutorLatest.submitPlanMissingIndexDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanIndexUploadDraws = rg_modernExecutorLatest.submitPlanIndexUploadDraws;
	rg_rendererMetrics.modernExecutorSubmitExecuted = rg_modernExecutorLatest.submitExecuted;
	rg_rendererMetrics.modernExecutorSubmittedDraws = rg_modernExecutorLatest.submittedDraws;
	rg_rendererMetrics.modernExecutorSubmittedFallbackDraws = rg_modernExecutorLatest.submittedFallbackDraws;
	rg_rendererMetrics.modernExecutorSubmittedIndexUploadDraws = rg_modernExecutorLatest.submittedIndexUploadDraws;
	rg_rendererMetrics.modernExecutorSubmitPlanProgramBatches = rg_modernExecutorLatest.submitPlanProgramBatches;
	rg_rendererMetrics.modernExecutorSubmitPlanVertexBufferBatches = rg_modernExecutorLatest.submitPlanVertexBufferBatches;
	rg_rendererMetrics.modernExecutorSubmitPlanIndexBufferBatches = rg_modernExecutorLatest.submitPlanIndexBufferBatches;
	rg_rendererMetrics.modernExecutorSubmitPlanScissorBatches = rg_modernExecutorLatest.submitPlanScissorBatches;
	rg_rendererMetrics.modernExecutorSubmitPlanMaterialBatches = rg_modernExecutorLatest.submitPlanMaterialBatches;
	rg_rendererMetrics.modernExecutorSubmitPlanUniformUpdates = rg_modernExecutorLatest.submitPlanUniformUpdates;
	rg_rendererMetrics.modernExecutorSubmitPlanFrameUBOBinds = rg_modernExecutorLatest.submitPlanFrameUBOBinds;
	rg_rendererMetrics.modernExecutorVisibleDepthRequested = rg_modernExecutorLatest.visibleDepthRequested;
	rg_rendererMetrics.modernExecutorVisibleDepthExecuted = rg_modernExecutorLatest.visibleDepthExecuted;
	rg_rendererMetrics.modernExecutorVisibleDepthResourceReady = rg_modernExecutorLatest.visibleDepthResourceReady;
	rg_rendererMetrics.modernExecutorVisibleShadowResourceReady = rg_modernExecutorLatest.visibleShadowResourceReady;
	rg_rendererMetrics.modernExecutorVisibleDepthDebugOverlayReady = rg_modernExecutorLatest.visibleDepthDebugOverlayReady;
	rg_rendererMetrics.modernExecutorVisibleDepthDraws = rg_modernExecutorLatest.visibleDepthDraws;
	rg_rendererMetrics.modernExecutorVisibleDepthAlphaTestDraws = rg_modernExecutorLatest.visibleDepthAlphaTestDraws;
	rg_rendererMetrics.modernExecutorVisibleDepthSkinnedDraws = rg_modernExecutorLatest.visibleDepthSkinnedDraws;
	rg_rendererMetrics.modernExecutorVisibleDepthFallbackDraws = rg_modernExecutorLatest.visibleDepthFallbackDraws;
	rg_rendererMetrics.modernExecutorVisibleShadowDepthDraws = rg_modernExecutorLatest.visibleShadowDepthDraws;
	rg_rendererMetrics.modernExecutorVisibleShadowFallbackDraws = rg_modernExecutorLatest.visibleShadowFallbackDraws;
	rg_rendererMetrics.modernExecutorVisibleStencilShadowFallbackDraws = rg_modernExecutorLatest.visibleStencilShadowFallbackDraws;
	rg_rendererMetrics.modernExecutorVisibleDepthMismatchDraws = rg_modernExecutorLatest.visibleDepthMismatchDraws;
	rg_rendererMetrics.modernExecutorVisibleDepthDebugOverlayDraws = rg_modernExecutorLatest.visibleDepthDebugOverlayDraws;
	rg_rendererMetrics.modernExecutorOpaqueGBufferRequested = rg_modernExecutorLatest.opaqueGBufferRequested;
	rg_rendererMetrics.modernExecutorOpaqueGBufferExecuted = rg_modernExecutorLatest.opaqueGBufferExecuted;
	rg_rendererMetrics.modernExecutorOpaqueGBufferResourcesReady = rg_modernExecutorLatest.opaqueGBufferResourcesReady;
	rg_rendererMetrics.modernExecutorOpaqueGBufferMRTReady = rg_modernExecutorLatest.opaqueGBufferMRTReady;
	rg_rendererMetrics.modernExecutorOpaqueGBufferDebugOverlayReady = rg_modernExecutorLatest.opaqueGBufferDebugOverlayReady;
	rg_rendererMetrics.modernExecutorOpaqueGBufferDraws = rg_modernExecutorLatest.opaqueGBufferDraws;
	rg_rendererMetrics.modernExecutorOpaqueGBufferFallbackDraws = rg_modernExecutorLatest.opaqueGBufferFallbackDraws;
	rg_rendererMetrics.modernExecutorOpaqueGBufferAttachmentCount = rg_modernExecutorLatest.opaqueGBufferAttachmentCount;
	rg_rendererMetrics.modernExecutorOpaqueGBufferBytesPerPixel = rg_modernExecutorLatest.opaqueGBufferBytesPerPixel;
	rg_rendererMetrics.modernExecutorOpaqueGBufferBandwidthKB = rg_modernExecutorLatest.opaqueGBufferBandwidthKB;
	rg_rendererMetrics.modernExecutorOpaqueGBufferDebugOverlayDraws = rg_modernExecutorLatest.opaqueGBufferDebugOverlayDraws;
	rg_rendererMetrics.modernDeferredRequested = rg_deferredResolveLatest.requested;
	rg_rendererMetrics.modernDeferredExecuted = rg_deferredResolveLatest.executed;
	rg_rendererMetrics.modernDeferredResourcesReady = rg_deferredResolveLatest.resourcesReady;
	rg_rendererMetrics.modernDeferredOutputReady = rg_deferredResolveLatest.outputReady;
	rg_rendererMetrics.modernDeferredProgramReady = rg_deferredResolveLatest.programReady;
	rg_rendererMetrics.modernDeferredClusterReady = rg_deferredResolveLatest.clusterReady;
	rg_rendererMetrics.modernDeferredDebugOverlayReady = rg_deferredResolveLatest.debugOverlayReady;
	rg_rendererMetrics.modernDeferredResolvedPixels = rg_deferredResolveLatest.resolvedPixels;
	rg_rendererMetrics.modernDeferredActiveLights = rg_deferredResolveLatest.activeLights;
	rg_rendererMetrics.modernDeferredPointLights = rg_deferredResolveLatest.pointLights;
	rg_rendererMetrics.modernDeferredProjectedLights = rg_deferredResolveLatest.projectedLights;
	rg_rendererMetrics.modernDeferredLightGridContributions = rg_deferredResolveLatest.lightGridContributions;
	rg_rendererMetrics.modernDeferredClusterReads = rg_deferredResolveLatest.clusterReads;
	rg_rendererMetrics.modernDeferredResourceFallbacks = rg_deferredResolveLatest.resourceFallbacks;
	rg_rendererMetrics.modernDeferredUnsupportedLightFallbacks = rg_deferredResolveLatest.unsupportedLightFallbacks;
	rg_rendererMetrics.modernDeferredFogFallbackLights = rg_deferredResolveLatest.fogFallbackLights;
	rg_rendererMetrics.modernDeferredSpecialFallbackLights = rg_deferredResolveLatest.specialFallbackLights;
	rg_rendererMetrics.modernDeferredOverflowClusters = rg_deferredResolveLatest.overflowClusters;
	rg_rendererMetrics.modernDeferredClearOps = rg_deferredResolveLatest.clearOps;
	rg_rendererMetrics.modernDeferredDebugMode = rg_deferredResolveLatest.debugMode;
	rg_rendererMetrics.modernDeferredDebugOverlayDraws = rg_deferredResolveLatest.debugOverlayDraws;
	rg_rendererMetrics.modernForwardRequested = rg_forwardPlusLatest.requested;
	rg_rendererMetrics.modernForwardExecuted = rg_forwardPlusLatest.executed;
	rg_rendererMetrics.modernForwardResourcesReady = rg_forwardPlusLatest.resourcesReady;
	rg_rendererMetrics.modernForwardSceneColorReady = rg_forwardPlusLatest.sceneColorReady;
	rg_rendererMetrics.modernForwardSceneDepthReady = rg_forwardPlusLatest.sceneDepthReady;
	rg_rendererMetrics.modernForwardProgramReady = rg_forwardPlusLatest.programReady;
	rg_rendererMetrics.modernForwardClusterReady = rg_forwardPlusLatest.clusterReady;
	rg_rendererMetrics.modernForwardDraws = rg_forwardPlusLatest.draws;
	rg_rendererMetrics.modernForwardOpaqueDraws = rg_forwardPlusLatest.opaqueDraws;
	rg_rendererMetrics.modernForwardAlphaTestDraws = rg_forwardPlusLatest.alphaTestDraws;
	rg_rendererMetrics.modernForwardTransparentDraws = rg_forwardPlusLatest.transparentDraws;
	rg_rendererMetrics.modernForwardViewModelDraws = rg_forwardPlusLatest.viewModelDraws;
	rg_rendererMetrics.modernForwardFogBlendDraws = rg_forwardPlusLatest.fogBlendDraws;
	rg_rendererMetrics.modernForwardSortedBatches = rg_forwardPlusLatest.sortedBatches;
	rg_rendererMetrics.modernForwardFallbackDraws = rg_forwardPlusLatest.fallbackDraws;
	rg_rendererMetrics.modernForwardResourceFallbackDraws = rg_forwardPlusLatest.resourceFallbackDraws;
	rg_rendererMetrics.modernForwardMaterialFallbackDraws = rg_forwardPlusLatest.materialFallbackDraws;
	rg_rendererMetrics.modernForwardGeometryFallbackDraws = rg_forwardPlusLatest.geometryFallbackDraws;
	rg_rendererMetrics.modernForwardTextureFallbackDraws = rg_forwardPlusLatest.textureFallbackDraws;
	rg_rendererMetrics.modernForwardUnsupportedBlendFallbackDraws = rg_forwardPlusLatest.unsupportedBlendFallbackDraws;
	rg_rendererMetrics.modernForwardSpecialEffectFallbacks = rg_forwardPlusLatest.specialEffectFallbacks;
	rg_rendererMetrics.modernForwardSortFallbackDraws = rg_forwardPlusLatest.sortFallbackDraws;
	rg_rendererMetrics.modernForwardOverdrawEstimate = rg_forwardPlusLatest.overdrawEstimate;
	rg_rendererMetrics.modernForwardClusterReads = rg_forwardPlusLatest.clusterReads;
	rg_rendererMetrics.modernForwardActiveLights = rg_forwardPlusLatest.activeLights;
	rg_rendererMetrics.modernForwardPointLights = rg_forwardPlusLatest.pointLights;
	rg_rendererMetrics.modernForwardProjectedLights = rg_forwardPlusLatest.projectedLights;
	rg_rendererMetrics.modernForwardLightGridContributions = rg_forwardPlusLatest.lightGridContributions;
	rg_rendererMetrics.modernForwardClearOps = rg_forwardPlusLatest.clearOps;
	rg_rendererMetrics.modernVisibleRequested = rg_modernVisibleLatest.requested;
	rg_rendererMetrics.modernVisibleExecuted = rg_modernVisibleLatest.executed;
	rg_rendererMetrics.modernVisibleResourcesReady = rg_modernVisibleLatest.resourcesReady;
	rg_rendererMetrics.modernVisibleProgramReady = rg_modernVisibleLatest.programReady;
	rg_rendererMetrics.modernVisibleSourceReady = rg_modernVisibleLatest.sourceReady;
	rg_rendererMetrics.modernVisibleBackBufferReady = rg_modernVisibleLatest.backBufferReady;
	rg_rendererMetrics.modernVisibleHybridTargetReady = rg_modernVisibleLatest.hybridTargetReady;
	rg_rendererMetrics.modernVisibleShadowReady = rg_modernVisibleLatest.shadowReady;
	rg_rendererMetrics.modernVisibleHDRTargetReady = rg_modernVisibleLatest.hdrTargetReady;
	rg_rendererMetrics.modernVisiblePostProcessHandoff = rg_modernVisibleLatest.postProcessHandoff;
	rg_rendererMetrics.modernVisibleBlockedByLegacy = rg_modernVisibleLatest.blockedByLegacy;
	rg_rendererMetrics.modernVisibleCompositions = rg_modernVisibleLatest.compositions;
	rg_rendererMetrics.modernVisiblePixels = rg_modernVisibleLatest.pixels;
	rg_rendererMetrics.modernVisibleCompositeCopies = rg_modernVisibleLatest.compositeCopies;
	rg_rendererMetrics.modernVisiblePostProcessCompositions = rg_modernVisibleLatest.postProcessCompositions;
	rg_rendererMetrics.modernVisibleDepthCopies = rg_modernVisibleLatest.depthCopies;
	rg_rendererMetrics.modernVisibleModernPasses = rg_modernVisibleLatest.modernPasses;
	rg_rendererMetrics.modernVisibleLegacyPasses = rg_modernVisibleLatest.legacyPasses;
	rg_rendererMetrics.modernVisibleDisabledPasses = rg_modernVisibleLatest.disabledPasses;
	rg_rendererMetrics.modernVisibleFallbackPasses = rg_modernVisibleLatest.fallbackPasses;
	rg_rendererMetrics.modernVisibleOwnerFallbacks = rg_modernVisibleLatest.ownerFallbacks;
	rg_rendererMetrics.modernVisibleResourceFallbacks = rg_modernVisibleLatest.resourceFallbacks;
	rg_rendererMetrics.modernVisibleGuiLegacyPasses = rg_modernVisibleLatest.guiLegacyPasses;
	rg_rendererMetrics.modernVisiblePostLegacyPasses = rg_modernVisibleLatest.postLegacyPasses;
	rg_rendererMetrics.modernVisibleSpecialLegacyPasses = rg_modernVisibleLatest.specialLegacyPasses;
	rg_rendererMetrics.modernVisibleSubviewLegacyPasses = rg_modernVisibleLatest.subviewLegacyPasses;
	rg_rendererMetrics.modernVisiblePresentPasses = rg_modernVisibleLatest.presentPasses;
	rg_rendererMetrics.modernVisibleClearOps = rg_modernVisibleLatest.clearOps;
	rg_rendererMetrics.gpuDrivenRequested = rg_gpuDrivenLatest.requested;
	rg_rendererMetrics.gpuDrivenExecuted = rg_gpuDrivenLatest.executed;
	rg_rendererMetrics.gpuDrivenResourcesReady = rg_gpuDrivenLatest.resourcesReady;
	rg_rendererMetrics.gpuDrivenValidationRequested = rg_gpuDrivenLatest.validationRequested;
	rg_rendererMetrics.gpuDrivenValidationReadbackReady = rg_gpuDrivenLatest.validationReadbackReady;
	rg_rendererMetrics.gpuDrivenIndirectExecuted = rg_gpuDrivenLatest.indirectExecuted;
	rg_rendererMetrics.gpuDrivenMultiDrawReady = rg_gpuDrivenLatest.multiDrawReady;
	rg_rendererMetrics.gpuDrivenSourceCommands = rg_gpuDrivenLatest.sourceCommands;
	rg_rendererMetrics.gpuDrivenEligibleCommands = rg_gpuDrivenLatest.eligibleCommands;
	rg_rendererMetrics.gpuDrivenGeneratedCommands = rg_gpuDrivenLatest.generatedCommands;
	rg_rendererMetrics.gpuDrivenCulledCommands = rg_gpuDrivenLatest.culledCommands;
	rg_rendererMetrics.gpuDrivenVisibleInstances = rg_gpuDrivenLatest.visibleInstances;
	rg_rendererMetrics.gpuDrivenCpuGeneratedCommands = rg_gpuDrivenLatest.cpuGeneratedCommands;
	rg_rendererMetrics.gpuDrivenCpuCulledCommands = rg_gpuDrivenLatest.cpuCulledCommands;
	rg_rendererMetrics.gpuDrivenCpuVisibleInstances = rg_gpuDrivenLatest.cpuVisibleInstances;
	rg_rendererMetrics.gpuDrivenGpuGeneratedCommands = rg_gpuDrivenLatest.gpuGeneratedCommands;
	rg_rendererMetrics.gpuDrivenGpuCulledCommands = rg_gpuDrivenLatest.gpuCulledCommands;
	rg_rendererMetrics.gpuDrivenGpuVisibleInstances = rg_gpuDrivenLatest.gpuVisibleInstances;
	rg_rendererMetrics.gpuDrivenCpuClusterBins = rg_gpuDrivenLatest.cpuClusterBins;
	rg_rendererMetrics.gpuDrivenGpuClusterBins = rg_gpuDrivenLatest.gpuClusterBins;
	rg_rendererMetrics.gpuDrivenValidationReadbacks = rg_gpuDrivenLatest.validationReadbacks;
	rg_rendererMetrics.gpuDrivenValidationMismatches = rg_gpuDrivenLatest.validationMismatches;
	rg_rendererMetrics.gpuDrivenIndirectDrawCalls = rg_gpuDrivenLatest.indirectDrawCalls;
	rg_rendererMetrics.gpuDrivenMultiDrawBatches = rg_gpuDrivenLatest.multiDrawBatches;
	rg_rendererMetrics.gpuDrivenIndirectFallbacks = rg_gpuDrivenLatest.indirectFallbacks;
	rg_rendererMetrics.gpuDrivenComputeDispatches = rg_gpuDrivenLatest.computeDispatches;
	rg_rendererMetrics.lowOverheadRequested = rg_lowOverheadLatest.requested;
	rg_rendererMetrics.lowOverheadReady = rg_lowOverheadLatest.ready;
	rg_rendererMetrics.lowOverheadUsesDSA = rg_lowOverheadLatest.usesDSA;
	rg_rendererMetrics.lowOverheadUsesMultiBind = rg_lowOverheadLatest.usesMultiBind;
	rg_rendererMetrics.lowOverheadBindlessRequested = rg_lowOverheadLatest.bindlessRequested;
	rg_rendererMetrics.lowOverheadBindlessAvailable = rg_lowOverheadLatest.bindlessAvailable;
	rg_rendererMetrics.lowOverheadSamplerReady = rg_lowOverheadLatest.samplerReady;
	rg_rendererMetrics.lowOverheadDSAUpdates = rg_lowOverheadLatest.dsaUpdates;
	rg_rendererMetrics.lowOverheadFramebufferDSAUpdates = rg_lowOverheadLatest.framebufferDSAUpdates;
	rg_rendererMetrics.lowOverheadSamplerDSACreations = rg_lowOverheadLatest.samplerDSACreations;
	rg_rendererMetrics.lowOverheadSamplerDSAUpdates = rg_lowOverheadLatest.samplerDSAUpdates;
	rg_rendererMetrics.lowOverheadBufferMultiBindBatches = rg_lowOverheadLatest.bufferMultiBindBatches;
	rg_rendererMetrics.lowOverheadTextureMultiBindBatches = rg_lowOverheadLatest.textureMultiBindBatches;
	rg_rendererMetrics.lowOverheadSamplerMultiBindBatches = rg_lowOverheadLatest.samplerMultiBindBatches;
	rg_rendererMetrics.lowOverheadClassicTextureBinds = rg_lowOverheadLatest.classicTextureBinds;
	rg_rendererMetrics.lowOverheadCompactedBatches = rg_lowOverheadLatest.compactedBatches;
	rg_rendererMetrics.clusteredLighting = rg_clusteredLightingLatest;
	rg_rendererMetrics.glStateCache = rg_glStateCacheLatest.stats;
}

void R_RendererMetrics_RecordSubmitMsec( int submitMsec ) {
	rg_rendererMetrics.submitMsec += submitMsec;
}

void R_RendererMetrics_AddVisibilityMsec( int msec ) {
	if ( msec > 0 ) {
		rg_rendererMetrics.visibilityMsec += msec;
	}
}

void R_RendererMetrics_AddPacketBuildMsec( int msec ) {
	if ( msec > 0 ) {
		rg_rendererMetrics.packetBuildMsec += msec;
	}
}

void R_RendererMetrics_AddGraphBuildMsec( int msec ) {
	if ( msec > 0 ) {
		rg_rendererMetrics.graphBuildMsec += msec;
	}
}

void R_RendererMetrics_AddPresentMsec( int msec ) {
	if ( msec > 0 ) {
		rg_rendererMetrics.presentMsec += msec;
	}
}

void R_RendererMetrics_RecordBackendCommands( int draw3d, int draw2d, int setBuffers, int swapBuffers, int copyRenders, int specialEffects, int renderTargetOps ) {
	rg_rendererMetrics.draw3d += draw3d;
	rg_rendererMetrics.draw2d += draw2d;
	rg_rendererMetrics.setBuffers += setBuffers;
	rg_rendererMetrics.swapBuffers += swapBuffers;
	rg_rendererMetrics.copyRenders += copyRenders;
	rg_rendererMetrics.specialEffects += specialEffects;
	rg_rendererMetrics.renderTargetOps += renderTargetOps;
}

void R_RendererMetrics_RecordScenePackets( const scenePacketFrameStats_t &stats ) {
	rg_scenePacketLatest.scenePackets = stats.scenePackets;
	rg_scenePacketLatest.passPackets = stats.passPackets;
	rg_scenePacketLatest.drawPackets = stats.drawPackets;
	rg_scenePacketLatest.clippedDrawPackets = stats.clippedDrawPackets;
	rg_scenePacketLatest.commandPackets = stats.commandPackets;
	rg_scenePacketLatest.legacyDrawViews = stats.legacyDrawViews;
	rg_scenePacketLatest.materialRecords = stats.materialRecords;
	rg_scenePacketLatest.geometryRecords = stats.geometryRecords;
	rg_scenePacketLatest.instanceRecords = stats.instanceRecords;
	rg_scenePacketLatest.drawPacketsWithMaterial = stats.drawPacketsWithMaterial;
	rg_scenePacketLatest.drawPacketsWithResourceRecord = stats.drawPacketsWithResourceRecord;
	rg_scenePacketLatest.drawPacketsWithGeometryRecord = stats.drawPacketsWithGeometryRecord;
	rg_scenePacketLatest.drawPacketsWithInstanceRecord = stats.drawPacketsWithInstanceRecord;
	rg_scenePacketLatest.drawPacketsWithGeometry = stats.drawPacketsWithGeometry;
	rg_scenePacketLatest.drawPacketsWithShaderRegisters = stats.drawPacketsWithShaderRegisters;
	rg_scenePacketLatest.drawPacketsWithIndexCache = stats.drawPacketsWithIndexCache;
	rg_scenePacketLatest.drawPacketsWithAmbientCache = stats.drawPacketsWithAmbientCache;
	rg_scenePacketLatest.worldPackets = stats.worldPackets;
	rg_scenePacketLatest.subviewPackets = stats.subviewPackets;
	rg_scenePacketLatest.remoteCameraPackets = stats.remoteCameraPackets;
	rg_scenePacketLatest.specialEffectPackets = stats.specialEffectPackets;
	rg_scenePacketLatest.viewmodelPackets = stats.viewmodelPackets;
	rg_scenePacketLatest.renderDemoPackets = stats.renderDemoPackets;
	rg_scenePacketLatest.guiPackets = stats.guiPackets;
	rg_scenePacketLatest.postProcessPackets = stats.postProcessPackets;
	rg_scenePacketLatest.presentPackets = stats.presentPackets;
	rg_scenePacketLatest.commandOnlyPackets = stats.commandOnlyPackets;
	rg_scenePacketLatest.sortKeyValidationFailures = stats.sortKeyValidationFailures;
	rg_scenePacketLatest.frontEndDerived = stats.frontEndDerived;
	rg_scenePacketLatest.backendDerived = stats.backendDerived;
	rg_scenePacketLatest.overflow = stats.overflow;
	rg_scenePacketLatest.overflowCause = stats.overflowCause;
}

void R_RendererMetrics_RecordRenderGraph( int graphPasses, int passPackets, int scenePackets, int drawPackets, int commandPackets, int resources, int importedResources, int transientResources, int aliasableTransientResources, int resourceAccesses, int readAccesses, int writeAccesses, int clearOps, int resolveOps, int invalidateOps, int presentOps, bool overflow ) {
	rg_renderGraphLatest.graphPasses = graphPasses;
	rg_renderGraphLatest.passPackets = passPackets;
	rg_renderGraphLatest.scenePackets = scenePackets;
	rg_renderGraphLatest.drawPackets = drawPackets;
	rg_renderGraphLatest.commandPackets = commandPackets;
	rg_renderGraphLatest.resources = resources;
	rg_renderGraphLatest.importedResources = importedResources;
	rg_renderGraphLatest.transientResources = transientResources;
	rg_renderGraphLatest.aliasableTransientResources = aliasableTransientResources;
	rg_renderGraphLatest.resourceAccesses = resourceAccesses;
	rg_renderGraphLatest.readAccesses = readAccesses;
	rg_renderGraphLatest.writeAccesses = writeAccesses;
	rg_renderGraphLatest.clearOps = clearOps;
	rg_renderGraphLatest.resolveOps = resolveOps;
	rg_renderGraphLatest.invalidateOps = invalidateOps;
	rg_renderGraphLatest.presentOps = presentOps;
	rg_renderGraphLatest.overflow = overflow;
}

void R_RendererMetrics_RecordRenderGraphResources( const renderGraphResourceManagerStats_t &stats ) {
	rg_renderGraphResourceLatest = stats;
	rg_rendererMetrics.renderGraphResourceManager = stats;
}

void R_RendererMetrics_RecordMaterialResourceTable( const materialResourceTableStats_t &stats ) {
	rg_materialResourceTableLatest = stats;
	rg_rendererMetrics.materialResourceTable = stats;
}

void R_RendererMetrics_RecordModernExecutor( rendererModernExecutorMetricsMode_t mode, int graphPasses, int preparedPasses, int fallbackPasses, int preparedDrawPackets, int materialDrawPackets, int resourceDrawPackets, int geometryDrawPackets, bool vaoReady, bool frameUBOReady, bool shaderLibraryReady, int shaderProgramCount, int shaderFailureCount, bool drawPlanReady, bool drawPlanOverflow, int drawPlanDraws, int drawPlanDepthDraws, int drawPlanMaterialDraws, int drawPlanFallbackDraws, int drawPlanStateBatches, int drawPlanProgramSwitches, int drawPlanMaterialSwitches, bool submitPlanReady, bool submitPlanOverflow, int submitPlanDraws, int submitPlanFallbackDraws, int submitPlanDepthDraws, int submitPlanMaterialDraws, int submitPlanMissingAmbientDraws, int submitPlanMissingIndexDraws, int submitPlanIndexUploadDraws, bool submitExecuted, int submittedDraws, int submittedFallbackDraws, int submittedIndexUploadDraws, int submitPlanProgramBatches, int submitPlanVertexBufferBatches, int submitPlanIndexBufferBatches, int submitPlanScissorBatches, int submitPlanMaterialBatches, int submitPlanUniformUpdates, int submitPlanFrameUBOBinds, bool visibleDepthRequested, bool visibleDepthExecuted, bool visibleDepthResourceReady, bool visibleShadowResourceReady, bool visibleDepthDebugOverlayReady, int visibleDepthDraws, int visibleDepthAlphaTestDraws, int visibleDepthSkinnedDraws, int visibleDepthFallbackDraws, int visibleShadowDepthDraws, int visibleShadowFallbackDraws, int visibleStencilShadowFallbackDraws, int visibleDepthMismatchDraws, int visibleDepthDebugOverlayDraws, bool opaqueGBufferRequested, bool opaqueGBufferExecuted, bool opaqueGBufferResourcesReady, bool opaqueGBufferMRTReady, bool opaqueGBufferDebugOverlayReady, int opaqueGBufferDraws, int opaqueGBufferFallbackDraws, int opaqueGBufferAttachmentCount, int opaqueGBufferBytesPerPixel, int opaqueGBufferBandwidthKB, int opaqueGBufferDebugOverlayDraws ) {
	rg_modernExecutorLatest.mode = mode;
	rg_modernExecutorLatest.graphPasses = graphPasses;
	rg_modernExecutorLatest.preparedPasses = preparedPasses;
	rg_modernExecutorLatest.fallbackPasses = fallbackPasses;
	rg_modernExecutorLatest.preparedDrawPackets = preparedDrawPackets;
	rg_modernExecutorLatest.materialDrawPackets = materialDrawPackets;
	rg_modernExecutorLatest.resourceDrawPackets = resourceDrawPackets;
	rg_modernExecutorLatest.geometryDrawPackets = geometryDrawPackets;
	rg_modernExecutorLatest.vaoReady = vaoReady;
	rg_modernExecutorLatest.frameUBOReady = frameUBOReady;
	rg_modernExecutorLatest.shaderLibraryReady = shaderLibraryReady;
	rg_modernExecutorLatest.shaderProgramCount = shaderProgramCount;
	rg_modernExecutorLatest.shaderFailureCount = shaderFailureCount;
	rg_modernExecutorLatest.drawPlanReady = drawPlanReady;
	rg_modernExecutorLatest.drawPlanOverflow = drawPlanOverflow;
	rg_modernExecutorLatest.drawPlanDraws = drawPlanDraws;
	rg_modernExecutorLatest.drawPlanDepthDraws = drawPlanDepthDraws;
	rg_modernExecutorLatest.drawPlanMaterialDraws = drawPlanMaterialDraws;
	rg_modernExecutorLatest.drawPlanFallbackDraws = drawPlanFallbackDraws;
	rg_modernExecutorLatest.drawPlanStateBatches = drawPlanStateBatches;
	rg_modernExecutorLatest.drawPlanProgramSwitches = drawPlanProgramSwitches;
	rg_modernExecutorLatest.drawPlanMaterialSwitches = drawPlanMaterialSwitches;
	rg_modernExecutorLatest.submitPlanReady = submitPlanReady;
	rg_modernExecutorLatest.submitPlanOverflow = submitPlanOverflow;
	rg_modernExecutorLatest.submitPlanDraws = submitPlanDraws;
	rg_modernExecutorLatest.submitPlanFallbackDraws = submitPlanFallbackDraws;
	rg_modernExecutorLatest.submitPlanDepthDraws = submitPlanDepthDraws;
	rg_modernExecutorLatest.submitPlanMaterialDraws = submitPlanMaterialDraws;
	rg_modernExecutorLatest.submitPlanMissingAmbientDraws = submitPlanMissingAmbientDraws;
	rg_modernExecutorLatest.submitPlanMissingIndexDraws = submitPlanMissingIndexDraws;
	rg_modernExecutorLatest.submitPlanIndexUploadDraws = submitPlanIndexUploadDraws;
	rg_modernExecutorLatest.submitExecuted = submitExecuted;
	rg_modernExecutorLatest.submittedDraws = submittedDraws;
	rg_modernExecutorLatest.submittedFallbackDraws = submittedFallbackDraws;
	rg_modernExecutorLatest.submittedIndexUploadDraws = submittedIndexUploadDraws;
	rg_modernExecutorLatest.submitPlanProgramBatches = submitPlanProgramBatches;
	rg_modernExecutorLatest.submitPlanVertexBufferBatches = submitPlanVertexBufferBatches;
	rg_modernExecutorLatest.submitPlanIndexBufferBatches = submitPlanIndexBufferBatches;
	rg_modernExecutorLatest.submitPlanScissorBatches = submitPlanScissorBatches;
	rg_modernExecutorLatest.submitPlanMaterialBatches = submitPlanMaterialBatches;
	rg_modernExecutorLatest.submitPlanUniformUpdates = submitPlanUniformUpdates;
	rg_modernExecutorLatest.submitPlanFrameUBOBinds = submitPlanFrameUBOBinds;
	rg_modernExecutorLatest.visibleDepthRequested = visibleDepthRequested;
	rg_modernExecutorLatest.visibleDepthExecuted = visibleDepthExecuted;
	rg_modernExecutorLatest.visibleDepthResourceReady = visibleDepthResourceReady;
	rg_modernExecutorLatest.visibleShadowResourceReady = visibleShadowResourceReady;
	rg_modernExecutorLatest.visibleDepthDebugOverlayReady = visibleDepthDebugOverlayReady;
	rg_modernExecutorLatest.visibleDepthDraws = visibleDepthDraws;
	rg_modernExecutorLatest.visibleDepthAlphaTestDraws = visibleDepthAlphaTestDraws;
	rg_modernExecutorLatest.visibleDepthSkinnedDraws = visibleDepthSkinnedDraws;
	rg_modernExecutorLatest.visibleDepthFallbackDraws = visibleDepthFallbackDraws;
	rg_modernExecutorLatest.visibleShadowDepthDraws = visibleShadowDepthDraws;
	rg_modernExecutorLatest.visibleShadowFallbackDraws = visibleShadowFallbackDraws;
	rg_modernExecutorLatest.visibleStencilShadowFallbackDraws = visibleStencilShadowFallbackDraws;
	rg_modernExecutorLatest.visibleDepthMismatchDraws = visibleDepthMismatchDraws;
	rg_modernExecutorLatest.visibleDepthDebugOverlayDraws = visibleDepthDebugOverlayDraws;
	rg_modernExecutorLatest.opaqueGBufferRequested = opaqueGBufferRequested;
	rg_modernExecutorLatest.opaqueGBufferExecuted = opaqueGBufferExecuted;
	rg_modernExecutorLatest.opaqueGBufferResourcesReady = opaqueGBufferResourcesReady;
	rg_modernExecutorLatest.opaqueGBufferMRTReady = opaqueGBufferMRTReady;
	rg_modernExecutorLatest.opaqueGBufferDebugOverlayReady = opaqueGBufferDebugOverlayReady;
	rg_modernExecutorLatest.opaqueGBufferDraws = opaqueGBufferDraws;
	rg_modernExecutorLatest.opaqueGBufferFallbackDraws = opaqueGBufferFallbackDraws;
	rg_modernExecutorLatest.opaqueGBufferAttachmentCount = opaqueGBufferAttachmentCount;
	rg_modernExecutorLatest.opaqueGBufferBytesPerPixel = opaqueGBufferBytesPerPixel;
	rg_modernExecutorLatest.opaqueGBufferBandwidthKB = opaqueGBufferBandwidthKB;
	rg_modernExecutorLatest.opaqueGBufferDebugOverlayDraws = opaqueGBufferDebugOverlayDraws;
}

void R_RendererMetrics_RecordDeferredResolve( bool requested, bool executed, bool resourcesReady, bool outputReady, bool programReady, bool clusterReady, bool debugOverlayReady, int resolvedPixels, int activeLights, int pointLights, int projectedLights, int lightGridContributions, int clusterReads, int resourceFallbacks, int unsupportedLightFallbacks, int fogFallbackLights, int specialFallbackLights, int overflowClusters, int clearOps, int debugMode, int debugOverlayDraws ) {
	rg_deferredResolveLatest.requested = requested;
	rg_deferredResolveLatest.executed = executed;
	rg_deferredResolveLatest.resourcesReady = resourcesReady;
	rg_deferredResolveLatest.outputReady = outputReady;
	rg_deferredResolveLatest.programReady = programReady;
	rg_deferredResolveLatest.clusterReady = clusterReady;
	rg_deferredResolveLatest.debugOverlayReady = debugOverlayReady;
	rg_deferredResolveLatest.resolvedPixels = resolvedPixels;
	rg_deferredResolveLatest.activeLights = activeLights;
	rg_deferredResolveLatest.pointLights = pointLights;
	rg_deferredResolveLatest.projectedLights = projectedLights;
	rg_deferredResolveLatest.lightGridContributions = lightGridContributions;
	rg_deferredResolveLatest.clusterReads = clusterReads;
	rg_deferredResolveLatest.resourceFallbacks = resourceFallbacks;
	rg_deferredResolveLatest.unsupportedLightFallbacks = unsupportedLightFallbacks;
	rg_deferredResolveLatest.fogFallbackLights = fogFallbackLights;
	rg_deferredResolveLatest.specialFallbackLights = specialFallbackLights;
	rg_deferredResolveLatest.overflowClusters = overflowClusters;
	rg_deferredResolveLatest.clearOps = clearOps;
	rg_deferredResolveLatest.debugMode = debugMode;
	rg_deferredResolveLatest.debugOverlayDraws = debugOverlayDraws;
	rg_rendererMetrics.modernDeferredRequested = requested;
	rg_rendererMetrics.modernDeferredExecuted = executed;
	rg_rendererMetrics.modernDeferredResourcesReady = resourcesReady;
	rg_rendererMetrics.modernDeferredOutputReady = outputReady;
	rg_rendererMetrics.modernDeferredProgramReady = programReady;
	rg_rendererMetrics.modernDeferredClusterReady = clusterReady;
	rg_rendererMetrics.modernDeferredDebugOverlayReady = debugOverlayReady;
	rg_rendererMetrics.modernDeferredResolvedPixels = resolvedPixels;
	rg_rendererMetrics.modernDeferredActiveLights = activeLights;
	rg_rendererMetrics.modernDeferredPointLights = pointLights;
	rg_rendererMetrics.modernDeferredProjectedLights = projectedLights;
	rg_rendererMetrics.modernDeferredLightGridContributions = lightGridContributions;
	rg_rendererMetrics.modernDeferredClusterReads = clusterReads;
	rg_rendererMetrics.modernDeferredResourceFallbacks = resourceFallbacks;
	rg_rendererMetrics.modernDeferredUnsupportedLightFallbacks = unsupportedLightFallbacks;
	rg_rendererMetrics.modernDeferredFogFallbackLights = fogFallbackLights;
	rg_rendererMetrics.modernDeferredSpecialFallbackLights = specialFallbackLights;
	rg_rendererMetrics.modernDeferredOverflowClusters = overflowClusters;
	rg_rendererMetrics.modernDeferredClearOps = clearOps;
	rg_rendererMetrics.modernDeferredDebugMode = debugMode;
	rg_rendererMetrics.modernDeferredDebugOverlayDraws = debugOverlayDraws;
}

void R_RendererMetrics_RecordForwardPlus( bool requested, bool executed, bool resourcesReady, bool sceneColorReady, bool sceneDepthReady, bool programReady, bool clusterReady, int draws, int opaqueDraws, int alphaTestDraws, int transparentDraws, int viewModelDraws, int fogBlendDraws, int sortedBatches, int fallbackDraws, int resourceFallbackDraws, int materialFallbackDraws, int geometryFallbackDraws, int textureFallbackDraws, int unsupportedBlendFallbackDraws, int specialEffectFallbacks, int sortFallbackDraws, int overdrawEstimate, int clusterReads, int activeLights, int pointLights, int projectedLights, int lightGridContributions, int clearOps ) {
	rg_forwardPlusLatest.requested = requested;
	rg_forwardPlusLatest.executed = executed;
	rg_forwardPlusLatest.resourcesReady = resourcesReady;
	rg_forwardPlusLatest.sceneColorReady = sceneColorReady;
	rg_forwardPlusLatest.sceneDepthReady = sceneDepthReady;
	rg_forwardPlusLatest.programReady = programReady;
	rg_forwardPlusLatest.clusterReady = clusterReady;
	rg_forwardPlusLatest.draws = draws;
	rg_forwardPlusLatest.opaqueDraws = opaqueDraws;
	rg_forwardPlusLatest.alphaTestDraws = alphaTestDraws;
	rg_forwardPlusLatest.transparentDraws = transparentDraws;
	rg_forwardPlusLatest.viewModelDraws = viewModelDraws;
	rg_forwardPlusLatest.fogBlendDraws = fogBlendDraws;
	rg_forwardPlusLatest.sortedBatches = sortedBatches;
	rg_forwardPlusLatest.fallbackDraws = fallbackDraws;
	rg_forwardPlusLatest.resourceFallbackDraws = resourceFallbackDraws;
	rg_forwardPlusLatest.materialFallbackDraws = materialFallbackDraws;
	rg_forwardPlusLatest.geometryFallbackDraws = geometryFallbackDraws;
	rg_forwardPlusLatest.textureFallbackDraws = textureFallbackDraws;
	rg_forwardPlusLatest.unsupportedBlendFallbackDraws = unsupportedBlendFallbackDraws;
	rg_forwardPlusLatest.specialEffectFallbacks = specialEffectFallbacks;
	rg_forwardPlusLatest.sortFallbackDraws = sortFallbackDraws;
	rg_forwardPlusLatest.overdrawEstimate = overdrawEstimate;
	rg_forwardPlusLatest.clusterReads = clusterReads;
	rg_forwardPlusLatest.activeLights = activeLights;
	rg_forwardPlusLatest.pointLights = pointLights;
	rg_forwardPlusLatest.projectedLights = projectedLights;
	rg_forwardPlusLatest.lightGridContributions = lightGridContributions;
	rg_forwardPlusLatest.clearOps = clearOps;
	rg_rendererMetrics.modernForwardRequested = requested;
	rg_rendererMetrics.modernForwardExecuted = executed;
	rg_rendererMetrics.modernForwardResourcesReady = resourcesReady;
	rg_rendererMetrics.modernForwardSceneColorReady = sceneColorReady;
	rg_rendererMetrics.modernForwardSceneDepthReady = sceneDepthReady;
	rg_rendererMetrics.modernForwardProgramReady = programReady;
	rg_rendererMetrics.modernForwardClusterReady = clusterReady;
	rg_rendererMetrics.modernForwardDraws = draws;
	rg_rendererMetrics.modernForwardOpaqueDraws = opaqueDraws;
	rg_rendererMetrics.modernForwardAlphaTestDraws = alphaTestDraws;
	rg_rendererMetrics.modernForwardTransparentDraws = transparentDraws;
	rg_rendererMetrics.modernForwardViewModelDraws = viewModelDraws;
	rg_rendererMetrics.modernForwardFogBlendDraws = fogBlendDraws;
	rg_rendererMetrics.modernForwardSortedBatches = sortedBatches;
	rg_rendererMetrics.modernForwardFallbackDraws = fallbackDraws;
	rg_rendererMetrics.modernForwardResourceFallbackDraws = resourceFallbackDraws;
	rg_rendererMetrics.modernForwardMaterialFallbackDraws = materialFallbackDraws;
	rg_rendererMetrics.modernForwardGeometryFallbackDraws = geometryFallbackDraws;
	rg_rendererMetrics.modernForwardTextureFallbackDraws = textureFallbackDraws;
	rg_rendererMetrics.modernForwardUnsupportedBlendFallbackDraws = unsupportedBlendFallbackDraws;
	rg_rendererMetrics.modernForwardSpecialEffectFallbacks = specialEffectFallbacks;
	rg_rendererMetrics.modernForwardSortFallbackDraws = sortFallbackDraws;
	rg_rendererMetrics.modernForwardOverdrawEstimate = overdrawEstimate;
	rg_rendererMetrics.modernForwardClusterReads = clusterReads;
	rg_rendererMetrics.modernForwardActiveLights = activeLights;
	rg_rendererMetrics.modernForwardPointLights = pointLights;
	rg_rendererMetrics.modernForwardProjectedLights = projectedLights;
	rg_rendererMetrics.modernForwardLightGridContributions = lightGridContributions;
	rg_rendererMetrics.modernForwardClearOps = clearOps;
}

void R_RendererMetrics_RecordModernVisible( bool requested, bool executed, bool resourcesReady, bool programReady, bool sourceReady, bool backBufferReady, bool hybridTargetReady, bool shadowReady, bool hdrTargetReady, bool postProcessHandoff, bool blockedByLegacy, int compositions, int pixels, int compositeCopies, int postProcessCompositions, int depthCopies, int modernPasses, int legacyPasses, int disabledPasses, int fallbackPasses, int ownerFallbacks, int resourceFallbacks, int guiLegacyPasses, int postLegacyPasses, int specialLegacyPasses, int subviewLegacyPasses, int presentPasses, int clearOps ) {
	rg_modernVisibleLatest.requested = requested;
	rg_modernVisibleLatest.executed = executed;
	rg_modernVisibleLatest.resourcesReady = resourcesReady;
	rg_modernVisibleLatest.programReady = programReady;
	rg_modernVisibleLatest.sourceReady = sourceReady;
	rg_modernVisibleLatest.backBufferReady = backBufferReady;
	rg_modernVisibleLatest.hybridTargetReady = hybridTargetReady;
	rg_modernVisibleLatest.shadowReady = shadowReady;
	rg_modernVisibleLatest.hdrTargetReady = hdrTargetReady;
	rg_modernVisibleLatest.postProcessHandoff = postProcessHandoff;
	rg_modernVisibleLatest.blockedByLegacy = blockedByLegacy;
	rg_modernVisibleLatest.compositions = compositions;
	rg_modernVisibleLatest.pixels = pixels;
	rg_modernVisibleLatest.compositeCopies = compositeCopies;
	rg_modernVisibleLatest.postProcessCompositions = postProcessCompositions;
	rg_modernVisibleLatest.depthCopies = depthCopies;
	rg_modernVisibleLatest.modernPasses = modernPasses;
	rg_modernVisibleLatest.legacyPasses = legacyPasses;
	rg_modernVisibleLatest.disabledPasses = disabledPasses;
	rg_modernVisibleLatest.fallbackPasses = fallbackPasses;
	rg_modernVisibleLatest.ownerFallbacks = ownerFallbacks;
	rg_modernVisibleLatest.resourceFallbacks = resourceFallbacks;
	rg_modernVisibleLatest.guiLegacyPasses = guiLegacyPasses;
	rg_modernVisibleLatest.postLegacyPasses = postLegacyPasses;
	rg_modernVisibleLatest.specialLegacyPasses = specialLegacyPasses;
	rg_modernVisibleLatest.subviewLegacyPasses = subviewLegacyPasses;
	rg_modernVisibleLatest.presentPasses = presentPasses;
	rg_modernVisibleLatest.clearOps = clearOps;
	rg_rendererMetrics.modernVisibleRequested = requested;
	rg_rendererMetrics.modernVisibleExecuted = executed;
	rg_rendererMetrics.modernVisibleResourcesReady = resourcesReady;
	rg_rendererMetrics.modernVisibleProgramReady = programReady;
	rg_rendererMetrics.modernVisibleSourceReady = sourceReady;
	rg_rendererMetrics.modernVisibleBackBufferReady = backBufferReady;
	rg_rendererMetrics.modernVisibleHybridTargetReady = hybridTargetReady;
	rg_rendererMetrics.modernVisibleShadowReady = shadowReady;
	rg_rendererMetrics.modernVisibleHDRTargetReady = hdrTargetReady;
	rg_rendererMetrics.modernVisiblePostProcessHandoff = postProcessHandoff;
	rg_rendererMetrics.modernVisibleBlockedByLegacy = blockedByLegacy;
	rg_rendererMetrics.modernVisibleCompositions = compositions;
	rg_rendererMetrics.modernVisiblePixels = pixels;
	rg_rendererMetrics.modernVisibleCompositeCopies = compositeCopies;
	rg_rendererMetrics.modernVisiblePostProcessCompositions = postProcessCompositions;
	rg_rendererMetrics.modernVisibleDepthCopies = depthCopies;
	rg_rendererMetrics.modernVisibleModernPasses = modernPasses;
	rg_rendererMetrics.modernVisibleLegacyPasses = legacyPasses;
	rg_rendererMetrics.modernVisibleDisabledPasses = disabledPasses;
	rg_rendererMetrics.modernVisibleFallbackPasses = fallbackPasses;
	rg_rendererMetrics.modernVisibleOwnerFallbacks = ownerFallbacks;
	rg_rendererMetrics.modernVisibleResourceFallbacks = resourceFallbacks;
	rg_rendererMetrics.modernVisibleGuiLegacyPasses = guiLegacyPasses;
	rg_rendererMetrics.modernVisiblePostLegacyPasses = postLegacyPasses;
	rg_rendererMetrics.modernVisibleSpecialLegacyPasses = specialLegacyPasses;
	rg_rendererMetrics.modernVisibleSubviewLegacyPasses = subviewLegacyPasses;
	rg_rendererMetrics.modernVisiblePresentPasses = presentPasses;
	rg_rendererMetrics.modernVisibleClearOps = clearOps;
}

void R_RendererMetrics_RecordGpuDriven( bool requested, bool executed, bool resourcesReady, bool validationRequested, bool validationReadbackReady, bool indirectExecuted, bool multiDrawReady, int sourceCommands, int eligibleCommands, int generatedCommands, int culledCommands, int visibleInstances, int cpuGeneratedCommands, int cpuCulledCommands, int cpuVisibleInstances, int gpuGeneratedCommands, int gpuCulledCommands, int gpuVisibleInstances, int cpuClusterBins, int gpuClusterBins, int validationReadbacks, int validationMismatches, int indirectDrawCalls, int multiDrawBatches, int indirectFallbacks, int computeDispatches ) {
	rg_gpuDrivenLatest.requested = requested;
	rg_gpuDrivenLatest.executed = executed;
	rg_gpuDrivenLatest.resourcesReady = resourcesReady;
	rg_gpuDrivenLatest.validationRequested = validationRequested;
	rg_gpuDrivenLatest.validationReadbackReady = validationReadbackReady;
	rg_gpuDrivenLatest.indirectExecuted = indirectExecuted;
	rg_gpuDrivenLatest.multiDrawReady = multiDrawReady;
	rg_gpuDrivenLatest.sourceCommands = sourceCommands;
	rg_gpuDrivenLatest.eligibleCommands = eligibleCommands;
	rg_gpuDrivenLatest.generatedCommands = generatedCommands;
	rg_gpuDrivenLatest.culledCommands = culledCommands;
	rg_gpuDrivenLatest.visibleInstances = visibleInstances;
	rg_gpuDrivenLatest.cpuGeneratedCommands = cpuGeneratedCommands;
	rg_gpuDrivenLatest.cpuCulledCommands = cpuCulledCommands;
	rg_gpuDrivenLatest.cpuVisibleInstances = cpuVisibleInstances;
	rg_gpuDrivenLatest.gpuGeneratedCommands = gpuGeneratedCommands;
	rg_gpuDrivenLatest.gpuCulledCommands = gpuCulledCommands;
	rg_gpuDrivenLatest.gpuVisibleInstances = gpuVisibleInstances;
	rg_gpuDrivenLatest.cpuClusterBins = cpuClusterBins;
	rg_gpuDrivenLatest.gpuClusterBins = gpuClusterBins;
	rg_gpuDrivenLatest.validationReadbacks = validationReadbacks;
	rg_gpuDrivenLatest.validationMismatches = validationMismatches;
	rg_gpuDrivenLatest.indirectDrawCalls = indirectDrawCalls;
	rg_gpuDrivenLatest.multiDrawBatches = multiDrawBatches;
	rg_gpuDrivenLatest.indirectFallbacks = indirectFallbacks;
	rg_gpuDrivenLatest.computeDispatches = computeDispatches;
	rg_rendererMetrics.gpuDrivenRequested = requested;
	rg_rendererMetrics.gpuDrivenExecuted = executed;
	rg_rendererMetrics.gpuDrivenResourcesReady = resourcesReady;
	rg_rendererMetrics.gpuDrivenValidationRequested = validationRequested;
	rg_rendererMetrics.gpuDrivenValidationReadbackReady = validationReadbackReady;
	rg_rendererMetrics.gpuDrivenIndirectExecuted = indirectExecuted;
	rg_rendererMetrics.gpuDrivenMultiDrawReady = multiDrawReady;
	rg_rendererMetrics.gpuDrivenSourceCommands = sourceCommands;
	rg_rendererMetrics.gpuDrivenEligibleCommands = eligibleCommands;
	rg_rendererMetrics.gpuDrivenGeneratedCommands = generatedCommands;
	rg_rendererMetrics.gpuDrivenCulledCommands = culledCommands;
	rg_rendererMetrics.gpuDrivenVisibleInstances = visibleInstances;
	rg_rendererMetrics.gpuDrivenCpuGeneratedCommands = cpuGeneratedCommands;
	rg_rendererMetrics.gpuDrivenCpuCulledCommands = cpuCulledCommands;
	rg_rendererMetrics.gpuDrivenCpuVisibleInstances = cpuVisibleInstances;
	rg_rendererMetrics.gpuDrivenGpuGeneratedCommands = gpuGeneratedCommands;
	rg_rendererMetrics.gpuDrivenGpuCulledCommands = gpuCulledCommands;
	rg_rendererMetrics.gpuDrivenGpuVisibleInstances = gpuVisibleInstances;
	rg_rendererMetrics.gpuDrivenCpuClusterBins = cpuClusterBins;
	rg_rendererMetrics.gpuDrivenGpuClusterBins = gpuClusterBins;
	rg_rendererMetrics.gpuDrivenValidationReadbacks = validationReadbacks;
	rg_rendererMetrics.gpuDrivenValidationMismatches = validationMismatches;
	rg_rendererMetrics.gpuDrivenIndirectDrawCalls = indirectDrawCalls;
	rg_rendererMetrics.gpuDrivenMultiDrawBatches = multiDrawBatches;
	rg_rendererMetrics.gpuDrivenIndirectFallbacks = indirectFallbacks;
	rg_rendererMetrics.gpuDrivenComputeDispatches = computeDispatches;
}

void R_RendererMetrics_RecordLowOverhead( bool requested, bool ready, bool usesDSA, bool usesMultiBind, bool bindlessRequested, bool bindlessAvailable, bool samplerReady, int dsaUpdates, int framebufferDSAUpdates, int samplerDSACreations, int samplerDSAUpdates, int bufferMultiBindBatches, int textureMultiBindBatches, int samplerMultiBindBatches, int classicTextureBinds, int compactedBatches ) {
	rg_lowOverheadLatest.requested = requested;
	rg_lowOverheadLatest.ready = ready;
	rg_lowOverheadLatest.usesDSA = usesDSA;
	rg_lowOverheadLatest.usesMultiBind = usesMultiBind;
	rg_lowOverheadLatest.bindlessRequested = bindlessRequested;
	rg_lowOverheadLatest.bindlessAvailable = bindlessAvailable;
	rg_lowOverheadLatest.samplerReady = samplerReady;
	rg_lowOverheadLatest.dsaUpdates = dsaUpdates;
	rg_lowOverheadLatest.framebufferDSAUpdates = framebufferDSAUpdates;
	rg_lowOverheadLatest.samplerDSACreations = samplerDSACreations;
	rg_lowOverheadLatest.samplerDSAUpdates = samplerDSAUpdates;
	rg_lowOverheadLatest.bufferMultiBindBatches = bufferMultiBindBatches;
	rg_lowOverheadLatest.textureMultiBindBatches = textureMultiBindBatches;
	rg_lowOverheadLatest.samplerMultiBindBatches = samplerMultiBindBatches;
	rg_lowOverheadLatest.classicTextureBinds = classicTextureBinds;
	rg_lowOverheadLatest.compactedBatches = compactedBatches;
	rg_rendererMetrics.lowOverheadRequested = requested;
	rg_rendererMetrics.lowOverheadReady = ready;
	rg_rendererMetrics.lowOverheadUsesDSA = usesDSA;
	rg_rendererMetrics.lowOverheadUsesMultiBind = usesMultiBind;
	rg_rendererMetrics.lowOverheadBindlessRequested = bindlessRequested;
	rg_rendererMetrics.lowOverheadBindlessAvailable = bindlessAvailable;
	rg_rendererMetrics.lowOverheadSamplerReady = samplerReady;
	rg_rendererMetrics.lowOverheadDSAUpdates = dsaUpdates;
	rg_rendererMetrics.lowOverheadFramebufferDSAUpdates = framebufferDSAUpdates;
	rg_rendererMetrics.lowOverheadSamplerDSACreations = samplerDSACreations;
	rg_rendererMetrics.lowOverheadSamplerDSAUpdates = samplerDSAUpdates;
	rg_rendererMetrics.lowOverheadBufferMultiBindBatches = bufferMultiBindBatches;
	rg_rendererMetrics.lowOverheadTextureMultiBindBatches = textureMultiBindBatches;
	rg_rendererMetrics.lowOverheadSamplerMultiBindBatches = samplerMultiBindBatches;
	rg_rendererMetrics.lowOverheadClassicTextureBinds = classicTextureBinds;
	rg_rendererMetrics.lowOverheadCompactedBatches = compactedBatches;
}

void R_RendererMetrics_RecordGLStateCache( const glStateCacheStats_t &stats ) {
	rg_glStateCacheLatest.stats = stats;
	rg_rendererMetrics.glStateCache = stats;
}

void R_RendererMetrics_RecordClusteredLighting( const rendererClusteredLightingStats_t &stats ) {
	rg_clusteredLightingLatest = stats;
	rg_rendererMetrics.clusteredLighting = stats;
}

void R_RendererMetrics_AddUploadBytes( int bytes ) {
	if ( bytes > 0 ) {
		rg_rendererMetrics.uploadBytes += bytes;
	}
}

void R_RendererMetrics_AddBufferStall( void ) {
	rg_rendererMetrics.bufferStalls++;
}

static void R_RendererMetrics_RecordBenchmarkCapture( void ) {
	rendererBenchmarkFrameSample_t sample;
	memset( &sample, 0, sizeof( sample ) );

	sample.frameMsec = rg_rendererMetrics.frontEndMsec + rg_rendererMetrics.submitMsec;
	if ( sample.frameMsec <= 0 ) {
		sample.frameMsec = rg_rendererMetrics.frontEndMsec + rg_rendererMetrics.backEndMsec;
	}
	sample.frontEndMsec = rg_rendererMetrics.frontEndMsec;
	sample.visibilityMsec = rg_rendererMetrics.visibilityMsec;
	sample.packetBuildMsec = rg_rendererMetrics.packetBuildMsec;
	sample.graphBuildMsec = rg_rendererMetrics.graphBuildMsec;
	sample.submitMsec = rg_rendererMetrics.submitMsec;
	sample.backEndMsec = rg_rendererMetrics.backEndMsec;
	sample.presentMsec = rg_rendererMetrics.presentMsec;
	sample.gpuMsec = Max( 0, R_RendererMetrics_TotalGpuMsec( rg_rendererMetrics ) );
	for ( int i = 0; i < RENDERER_GPU_TIMER_COUNT; ++i ) {
		sample.gpuPassMsec[i] = rg_rendererMetrics.gpuTimerMsec[i];
		sample.gpuPassSamples[i] = rg_rendererMetrics.gpuTimerSamples[i];
	}
	sample.uploadBytes = rg_rendererMetrics.uploadBytes;
	sample.drawElements = rg_rendererMetrics.drawElements;
	sample.surfaces = rg_rendererMetrics.surfaces;
	sample.vertexes = rg_rendererMetrics.vertexes;
	sample.indexes = rg_rendererMetrics.indexes;
	sample.visibleEntities = rg_rendererMetrics.visibleEntities;
	sample.viewLights = rg_rendererMetrics.viewLights;
	sample.scenePackets = rg_rendererMetrics.scenePackets;
	sample.passPackets = rg_rendererMetrics.passPackets;
	sample.drawPackets = rg_rendererMetrics.drawPackets;
	sample.renderGraphPasses = rg_rendererMetrics.renderGraphPasses;
	sample.renderGraphResources = rg_rendererMetrics.renderGraphResources;
	sample.clusterCount = rg_rendererMetrics.clusteredLighting.clusterCount;
	sample.clusterActiveCount = rg_rendererMetrics.clusteredLighting.activeClusters;
	sample.clusterLightCount = rg_rendererMetrics.clusteredLighting.lightCount;
	sample.clusterReferenceCount = rg_rendererMetrics.clusteredLighting.lightReferences;
	sample.clusterOverflowCount = rg_rendererMetrics.clusteredLighting.overflowClusters;
	sample.drawPlanFallbacks = rg_rendererMetrics.modernExecutorDrawPlanFallbackDraws;
	sample.submitPlanFallbacks = rg_rendererMetrics.modernExecutorSubmitPlanFallbackDraws;
	sample.opaqueGBufferFallbacks = rg_rendererMetrics.modernExecutorOpaqueGBufferFallbackDraws;
	sample.deferredFallbacks = rg_rendererMetrics.modernDeferredResourceFallbacks + rg_rendererMetrics.modernDeferredUnsupportedLightFallbacks + rg_rendererMetrics.modernDeferredFogFallbackLights + rg_rendererMetrics.modernDeferredSpecialFallbackLights;
	sample.forwardFallbacks = rg_rendererMetrics.modernForwardFallbackDraws;
	sample.visibleFallbacks = rg_rendererMetrics.modernVisibleFallbackPasses;
	sample.visibleOwnerFallbacks = rg_rendererMetrics.modernVisibleOwnerFallbacks;
	sample.visibleResourceFallbacks = rg_rendererMetrics.modernVisibleResourceFallbacks;

	RendererBenchmarks_RecordFrame( sample );
}

void R_RendererMetrics_EndFrame( int frontEndMsec, int backEndMsec, int viewCount, int visibleEntities, int viewLights, int drawElements, int surfaces, int vertexes, int indexes ) {
	if ( r_rendererMetrics.GetInteger() <= 0 ) {
		return;
	}

	rg_rendererMetrics.frontEndMsec = frontEndMsec;
	rg_rendererMetrics.backEndMsec = backEndMsec;
	rg_rendererMetrics.views = viewCount;
	rg_rendererMetrics.visibleEntities = visibleEntities;
	rg_rendererMetrics.viewLights = viewLights;
	rg_rendererMetrics.drawElements = drawElements;
	rg_rendererMetrics.surfaces = surfaces;
	rg_rendererMetrics.vertexes = vertexes;
	rg_rendererMetrics.indexes = indexes;
	R_RendererMetrics_RecordBenchmarkCapture();

	const int detail = r_rendererMetrics.GetInteger();
	const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
	char gpuText[32];
	R_RendererMetrics_FormatGpuMsec( rg_rendererMetrics, gpuText, sizeof( gpuText ) );
	if ( detail >= 2 ) {
		common->Printf(
			"rendererMetrics frame=%d tier=%s fe=%dms visibility=%dms packet=%dms graph=%dms submit=%dms be=%dms present=%dms gpu=%s views=%d ents=%d lights=%d draws=%d surf=%d verts=%d idx=%d uploads=%d stalls=%d ring=%d/%dKB allocs=%d overflow=%d static=%dKB/%d live=%d/%dKB writes(p=%d map=%d sub=%d) packets(source=%s scene=%d pass=%d draw=%d clipped=%d cmd=%d views=%d overflow=%d cause=%s sortFailures=%d categories(world=%d subview=%d remote=%d fx=%d viewmodel=%d demo=%d gui=%d post=%d present=%d command=%d)) resources(materials=%d geometryRecords=%d instances=%d withMaterial=%d materialRefs=%d geometryRefs=%d instanceRefs=%d geometry=%d regs=%d ibo=%d vbo=%d) graph(pass=%d packets=%d scenes=%d draw=%d cmd=%d res=%d imported=%d transient=%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d overflow=%d) graphGL(prepared=%d available=%d handles=%d imported=%d transient=%d textures=%d buffers=%d physical=%d new=%d reuse=%d aliasReuse=%d fbo=%d/%d skipped(imported=%d buffer=%d) lifetimeFailures=%d overflow=%d status='%s') materialTable(prepared=%d available=%d records=%d source=%d draws=%d textures=%d classic=%d arrays=%d views=%d bindless=%d/%d classes(o=%d p=%d t=%d gui=%d post=%d) fallback=%d missing=%d unsupported=%d reasons(matl=%d nodraw=%d image=%d custom=%d dynamic=%d texgen=%d current=%d slots=%d) status='%s') modernExec(mode=%s vao=%d ubo=%d shaderLib=%d shaders=%d shaderFails=%d passes=%d/%d fallback=%d draws=%d material=%d resources=%d geometry=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d switches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitDepth=%d submitMaterial=%d submitFallback=%d missing(vbo=%d ibo=%d) indexUpload=%d submitted=%d/%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d visibleDepth(req=%d exec=%d res=%d/%d draws=%d alpha=%d skinned=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d overlay=%d/%d) gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d att=%d bpp=%d bw=%dKB overlay=%d/%d) deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d p=%d proj=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clear=%d debug=%d overlay=%d/%d) cluster(req=%d valid=%d grids=%d lights=%d p=%d proj=%d fog=%d ambient=%d special=%d clusters=%d active=%d refs=%d overflow=%d/%d overflowRefs=%d max=%d grid=%dx%dx%d build=%dms ubo=%d bytes=%dKB overlay=%d/%d)) stateCache(hits=%d misses=%d invalidations=%d legacyResets=%d labels=%d groups=%d prog=%d vao=%d buf=%d tex=%d sampler=%d fbo=%d blend=%d depth=%d stencil=%d raster=%d viewport=%d scissor=%d color=%d last='%s') gpuPass(3d=%d/%d 2d=%d/%d rt=%d/%d copy=%d/%d special=%d/%d setbuf=%d/%d swap=%d/%d deferred=%d/%d dropped=%d) cmds(3d=%d 2d=%d rt=%d copy=%d swap=%d)\n",
			rg_rendererMetrics.frameCount,
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.visibilityMsec,
			rg_rendererMetrics.packetBuildMsec,
			rg_rendererMetrics.graphBuildMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
			rg_rendererMetrics.presentMsec,
			gpuText,
			rg_rendererMetrics.views,
			rg_rendererMetrics.visibleEntities,
			rg_rendererMetrics.viewLights,
			rg_rendererMetrics.drawElements,
			rg_rendererMetrics.surfaces,
			rg_rendererMetrics.vertexes,
			rg_rendererMetrics.indexes,
			rg_rendererMetrics.uploadBytes,
			rg_rendererMetrics.bufferStalls,
			uploadStats.frameRingHighWaterBytes / 1024,
			uploadStats.ringSizeBytes / 1024,
			uploadStats.frameAllocations,
			uploadStats.frameOverflowBytes / 1024,
			uploadStats.frameStaticUploadBytes / 1024,
			uploadStats.frameStaticAllocations,
			uploadStats.staticBuffersLive,
			uploadStats.staticBytesLive / 1024,
			uploadStats.framePersistentWrites,
			uploadStats.frameMapRangeWrites,
			uploadStats.frameSubDataWrites,
			R_RendererMetrics_ScenePacketSourceName( rg_rendererMetrics ),
			rg_rendererMetrics.scenePackets,
			rg_rendererMetrics.passPackets,
			rg_rendererMetrics.drawPackets,
			rg_rendererMetrics.clippedDrawPackets,
			rg_rendererMetrics.commandPackets,
			rg_rendererMetrics.legacyDrawViews,
			rg_rendererMetrics.scenePacketOverflow ? 1 : 0,
			ScenePacketOverflowCause_Name( rg_rendererMetrics.scenePacketOverflowCause ),
			rg_rendererMetrics.sortKeyValidationFailures,
			rg_rendererMetrics.worldPackets,
			rg_rendererMetrics.subviewPackets,
			rg_rendererMetrics.remoteCameraPackets,
			rg_rendererMetrics.specialEffectPackets,
			rg_rendererMetrics.viewmodelPackets,
			rg_rendererMetrics.renderDemoPackets,
			rg_rendererMetrics.guiPackets,
			rg_rendererMetrics.postProcessPackets,
			rg_rendererMetrics.presentPackets,
			rg_rendererMetrics.commandOnlyPackets,
			rg_rendererMetrics.materialRecords,
			rg_rendererMetrics.geometryRecords,
			rg_rendererMetrics.instanceRecords,
			rg_rendererMetrics.drawPacketsWithMaterial,
			rg_rendererMetrics.drawPacketsWithResourceRecord,
			rg_rendererMetrics.drawPacketsWithGeometryRecord,
			rg_rendererMetrics.drawPacketsWithInstanceRecord,
			rg_rendererMetrics.drawPacketsWithGeometry,
			rg_rendererMetrics.drawPacketsWithShaderRegisters,
			rg_rendererMetrics.drawPacketsWithIndexCache,
			rg_rendererMetrics.drawPacketsWithAmbientCache,
			rg_rendererMetrics.renderGraphPasses,
			rg_rendererMetrics.renderGraphPassPackets,
			rg_rendererMetrics.renderGraphScenePackets,
			rg_rendererMetrics.renderGraphDrawPackets,
			rg_rendererMetrics.renderGraphCommandPackets,
			rg_rendererMetrics.renderGraphResources,
			rg_rendererMetrics.renderGraphImportedResources,
			rg_rendererMetrics.renderGraphTransientResources,
			rg_rendererMetrics.renderGraphAliasableTransientResources,
			rg_rendererMetrics.renderGraphResourceAccesses,
			rg_rendererMetrics.renderGraphReadAccesses,
			rg_rendererMetrics.renderGraphWriteAccesses,
			rg_rendererMetrics.renderGraphClearOps,
			rg_rendererMetrics.renderGraphResolveOps,
			rg_rendererMetrics.renderGraphInvalidateOps,
			rg_rendererMetrics.renderGraphPresentOps,
			rg_rendererMetrics.renderGraphOverflow ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.prepared ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.available ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.handles,
			rg_rendererMetrics.renderGraphResourceManager.importedHandles,
			rg_rendererMetrics.renderGraphResourceManager.transientHandles,
			rg_rendererMetrics.renderGraphResourceManager.textureHandles,
			rg_rendererMetrics.renderGraphResourceManager.bufferHandles,
			rg_rendererMetrics.renderGraphResourceManager.physicalAllocations,
			rg_rendererMetrics.renderGraphResourceManager.newPhysicalAllocations,
			rg_rendererMetrics.renderGraphResourceManager.reusedPhysicalAllocations,
			rg_rendererMetrics.renderGraphResourceManager.aliasReusedPhysicalAllocations,
			rg_rendererMetrics.renderGraphResourceManager.completeFramebuffers,
			rg_rendererMetrics.renderGraphResourceManager.framebufferCount,
			rg_rendererMetrics.renderGraphResourceManager.skippedImported,
			rg_rendererMetrics.renderGraphResourceManager.skippedBuffers,
			rg_rendererMetrics.renderGraphResourceManager.lifetimeValidationFailures,
			rg_rendererMetrics.renderGraphResourceManager.overflow ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.lastFailure,
			rg_rendererMetrics.materialResourceTable.prepared ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.available ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.records,
			rg_rendererMetrics.materialResourceTable.sourceMaterialRecords,
			rg_rendererMetrics.materialResourceTable.drawPacketReferences,
			rg_rendererMetrics.materialResourceTable.textureBindings,
			rg_rendererMetrics.materialResourceTable.classicTextureBindings,
			rg_rendererMetrics.materialResourceTable.textureArrayDescriptors,
			rg_rendererMetrics.materialResourceTable.textureViewDescriptors,
			rg_rendererMetrics.materialResourceTable.bindlessEnabled ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.bindlessSupported ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.opaqueRecords,
			rg_rendererMetrics.materialResourceTable.perforatedRecords,
			rg_rendererMetrics.materialResourceTable.translucentRecords,
			rg_rendererMetrics.materialResourceTable.guiRecords,
			rg_rendererMetrics.materialResourceTable.postProcessRecords,
			rg_rendererMetrics.materialResourceTable.fallbackRecords,
			rg_rendererMetrics.materialResourceTable.missingImages,
			rg_rendererMetrics.materialResourceTable.unsupportedFeatures,
			rg_rendererMetrics.materialResourceTable.fallbackMissingMaterial,
			rg_rendererMetrics.materialResourceTable.fallbackNoDrawStages,
			rg_rendererMetrics.materialResourceTable.fallbackMissingImage,
			rg_rendererMetrics.materialResourceTable.fallbackCustomProgram,
			rg_rendererMetrics.materialResourceTable.fallbackDynamicImage,
			rg_rendererMetrics.materialResourceTable.fallbackUnsupportedTexgen,
			rg_rendererMetrics.materialResourceTable.fallbackNeedsCurrentRender,
			rg_rendererMetrics.materialResourceTable.fallbackTooManyTextures,
			rg_rendererMetrics.materialResourceTable.lastFailure,
			R_RendererMetrics_ModernExecutorModeName( rg_rendererMetrics.modernExecutorMode ),
			rg_rendererMetrics.modernExecutorVAOReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorFrameUBOReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorShaderLibraryReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorShaderProgramCount,
			rg_rendererMetrics.modernExecutorShaderFailureCount,
			rg_rendererMetrics.modernExecutorPreparedPasses,
			rg_rendererMetrics.modernExecutorGraphPasses,
			rg_rendererMetrics.modernExecutorFallbackPasses,
			rg_rendererMetrics.modernExecutorPreparedDrawPackets,
			rg_rendererMetrics.modernExecutorMaterialDrawPackets,
			rg_rendererMetrics.modernExecutorResourceDrawPackets,
			rg_rendererMetrics.modernExecutorGeometryDrawPackets,
			rg_rendererMetrics.modernExecutorDrawPlanReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorDrawPlanDraws,
			rg_rendererMetrics.modernExecutorDrawPlanDepthDraws,
			rg_rendererMetrics.modernExecutorDrawPlanMaterialDraws,
			rg_rendererMetrics.modernExecutorDrawPlanFallbackDraws,
			rg_rendererMetrics.modernExecutorDrawPlanStateBatches,
			rg_rendererMetrics.modernExecutorDrawPlanProgramSwitches,
			rg_rendererMetrics.modernExecutorDrawPlanMaterialSwitches,
			rg_rendererMetrics.modernExecutorDrawPlanOverflow ? 1 : 0,
			rg_rendererMetrics.modernExecutorSubmitPlanReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorSubmitPlanDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanDepthDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanMaterialDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanFallbackDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanMissingAmbientDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanMissingIndexDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanIndexUploadDraws,
			rg_rendererMetrics.modernExecutorSubmitExecuted ? 1 : 0,
			rg_rendererMetrics.modernExecutorSubmittedDraws,
			rg_rendererMetrics.modernExecutorSubmittedFallbackDraws,
			rg_rendererMetrics.modernExecutorSubmittedIndexUploadDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanProgramBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanVertexBufferBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanIndexBufferBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanScissorBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanMaterialBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanUniformUpdates,
			rg_rendererMetrics.modernExecutorSubmitPlanFrameUBOBinds,
			rg_rendererMetrics.modernExecutorSubmitPlanOverflow ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleDepthRequested ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleDepthExecuted ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleDepthResourceReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleShadowResourceReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleDepthDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthAlphaTestDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthSkinnedDraws,
			rg_rendererMetrics.modernExecutorVisibleShadowDepthDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthFallbackDraws,
			rg_rendererMetrics.modernExecutorVisibleShadowFallbackDraws,
			rg_rendererMetrics.modernExecutorVisibleStencilShadowFallbackDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthMismatchDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthDebugOverlayReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorVisibleDepthDebugOverlayDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferRequested ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferExecuted ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferResourcesReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferMRTReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferFallbackDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferAttachmentCount,
			rg_rendererMetrics.modernExecutorOpaqueGBufferBytesPerPixel,
			rg_rendererMetrics.modernExecutorOpaqueGBufferBandwidthKB,
			rg_rendererMetrics.modernExecutorOpaqueGBufferDebugOverlayReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferDebugOverlayDraws,
			rg_rendererMetrics.modernDeferredRequested ? 1 : 0,
			rg_rendererMetrics.modernDeferredExecuted ? 1 : 0,
			rg_rendererMetrics.modernDeferredResourcesReady ? 1 : 0,
			rg_rendererMetrics.modernDeferredOutputReady ? 1 : 0,
			rg_rendererMetrics.modernDeferredProgramReady ? 1 : 0,
			rg_rendererMetrics.modernDeferredClusterReady ? 1 : 0,
			rg_rendererMetrics.modernDeferredResolvedPixels,
			rg_rendererMetrics.modernDeferredActiveLights,
			rg_rendererMetrics.modernDeferredPointLights,
			rg_rendererMetrics.modernDeferredProjectedLights,
			rg_rendererMetrics.modernDeferredLightGridContributions,
			rg_rendererMetrics.modernDeferredClusterReads,
			rg_rendererMetrics.modernDeferredResourceFallbacks,
			rg_rendererMetrics.modernDeferredUnsupportedLightFallbacks,
			rg_rendererMetrics.modernDeferredFogFallbackLights,
			rg_rendererMetrics.modernDeferredSpecialFallbackLights,
			rg_rendererMetrics.modernDeferredOverflowClusters,
			rg_rendererMetrics.modernDeferredClearOps,
			rg_rendererMetrics.modernDeferredDebugMode,
			rg_rendererMetrics.modernDeferredDebugOverlayReady ? 1 : 0,
			rg_rendererMetrics.modernDeferredDebugOverlayDraws,
			rg_rendererMetrics.clusteredLighting.requested ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.frameValid ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.gridCount,
			rg_rendererMetrics.clusteredLighting.lightCount,
			rg_rendererMetrics.clusteredLighting.pointLights,
			rg_rendererMetrics.clusteredLighting.projectedLights,
			rg_rendererMetrics.clusteredLighting.fogLights,
			rg_rendererMetrics.clusteredLighting.ambientLights,
			rg_rendererMetrics.clusteredLighting.specialLights,
			rg_rendererMetrics.clusteredLighting.clusterCount,
			rg_rendererMetrics.clusteredLighting.activeClusters,
			rg_rendererMetrics.clusteredLighting.lightReferences,
			rg_rendererMetrics.clusteredLighting.overflow ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.overflowClusters,
			rg_rendererMetrics.clusteredLighting.overflowReferences,
			rg_rendererMetrics.clusteredLighting.maxLightsInCluster,
			rg_rendererMetrics.clusteredLighting.tileCountX,
			rg_rendererMetrics.clusteredLighting.tileCountY,
			rg_rendererMetrics.clusteredLighting.sliceCountZ,
			rg_rendererMetrics.clusteredLighting.buildMsec,
			rg_rendererMetrics.clusteredLighting.uboFallbackReady ? 1 : 0,
			( rg_rendererMetrics.clusteredLighting.paramsUBOBytes + rg_rendererMetrics.clusteredLighting.lightsUBOBytes + rg_rendererMetrics.clusteredLighting.indicesUBOBytes ) / 1024,
			rg_rendererMetrics.clusteredLighting.debugOverlayReady ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.debugOverlayDraws,
			rg_rendererMetrics.glStateCache.hits,
			rg_rendererMetrics.glStateCache.misses,
			rg_rendererMetrics.glStateCache.forcedInvalidations,
			rg_rendererMetrics.glStateCache.legacyHandoffResets,
			rg_rendererMetrics.glStateCache.objectLabelsAvailable ? 1 : 0,
			rg_rendererMetrics.glStateCache.debugGroupsAvailable ? 1 : 0,
			rg_rendererMetrics.glStateCache.programMisses,
			rg_rendererMetrics.glStateCache.vertexArrayMisses,
			rg_rendererMetrics.glStateCache.bufferMisses,
			rg_rendererMetrics.glStateCache.textureMisses,
			rg_rendererMetrics.glStateCache.samplerMisses,
			rg_rendererMetrics.glStateCache.framebufferMisses,
			rg_rendererMetrics.glStateCache.blendMisses,
			rg_rendererMetrics.glStateCache.depthMisses,
			rg_rendererMetrics.glStateCache.stencilMisses,
			rg_rendererMetrics.glStateCache.rasterMisses,
			rg_rendererMetrics.glStateCache.viewportMisses,
			rg_rendererMetrics.glStateCache.scissorMisses,
			rg_rendererMetrics.glStateCache.colorMaskMisses,
			rg_rendererMetrics.glStateCache.lastInvalidationReason,
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_DRAW3D],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_DRAW3D],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_DRAW2D],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_DRAW2D],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_RENDER_TARGET],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_RENDER_TARGET],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_COPY_RENDER],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_COPY_RENDER],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_SPECIAL_EFFECTS],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_SPECIAL_EFFECTS],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_SET_BUFFER],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_SET_BUFFER],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_SWAP_BUFFERS],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_SWAP_BUFFERS],
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_MODERN_DEFERRED],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_MODERN_DEFERRED],
			rg_rendererMetrics.gpuTimerDroppedQueries,
			rg_rendererMetrics.draw3d,
			rg_rendererMetrics.draw2d,
			rg_rendererMetrics.renderTargetOps,
			rg_rendererMetrics.copyRenders,
			rg_rendererMetrics.swapBuffers );
		common->Printf(
			"rendererMetrics forwardPlus(req=%d exec=%d res=%d scene=%d depth=%d program=%d cluster=%d draws=%d opaque=%d alpha=%d transparent=%d viewmodel=%d fog=%d batches=%d fallback=%d resource=%d material=%d geometry=%d texture=%d blend=%d effects=%d sort=%d overdraw=%d reads=%d lights=%d point=%d projected=%d lightGrid=%d clear=%d gpu=%d/%d)\n",
			rg_rendererMetrics.modernForwardRequested ? 1 : 0,
			rg_rendererMetrics.modernForwardExecuted ? 1 : 0,
			rg_rendererMetrics.modernForwardResourcesReady ? 1 : 0,
			rg_rendererMetrics.modernForwardSceneColorReady ? 1 : 0,
			rg_rendererMetrics.modernForwardSceneDepthReady ? 1 : 0,
			rg_rendererMetrics.modernForwardProgramReady ? 1 : 0,
			rg_rendererMetrics.modernForwardClusterReady ? 1 : 0,
			rg_rendererMetrics.modernForwardDraws,
			rg_rendererMetrics.modernForwardOpaqueDraws,
			rg_rendererMetrics.modernForwardAlphaTestDraws,
			rg_rendererMetrics.modernForwardTransparentDraws,
			rg_rendererMetrics.modernForwardViewModelDraws,
			rg_rendererMetrics.modernForwardFogBlendDraws,
			rg_rendererMetrics.modernForwardSortedBatches,
			rg_rendererMetrics.modernForwardFallbackDraws,
			rg_rendererMetrics.modernForwardResourceFallbackDraws,
			rg_rendererMetrics.modernForwardMaterialFallbackDraws,
			rg_rendererMetrics.modernForwardGeometryFallbackDraws,
			rg_rendererMetrics.modernForwardTextureFallbackDraws,
			rg_rendererMetrics.modernForwardUnsupportedBlendFallbackDraws,
			rg_rendererMetrics.modernForwardSpecialEffectFallbacks,
			rg_rendererMetrics.modernForwardSortFallbackDraws,
			rg_rendererMetrics.modernForwardOverdrawEstimate,
			rg_rendererMetrics.modernForwardClusterReads,
			rg_rendererMetrics.modernForwardActiveLights,
			rg_rendererMetrics.modernForwardPointLights,
			rg_rendererMetrics.modernForwardProjectedLights,
			rg_rendererMetrics.modernForwardLightGridContributions,
			rg_rendererMetrics.modernForwardClearOps,
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_MODERN_FORWARD],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_MODERN_FORWARD] );
		common->Printf(
			"rendererMetrics modernVisible(req=%d exec=%d res=%d program=%d source=%d hybrid=%d backBuffer=%d shadow=%d hdr=%d postHandoff=%d blocked=%d composed=%d copies=%d postComposed=%d depthCopies=%d pixels=%d modern=%d legacy=%d disabled=%d fallback=%d ownerFallback=%d resourceFallback=%d gui=%d post=%d special=%d subview=%d present=%d clear=%d gpu=%d/%d)\n",
			rg_rendererMetrics.modernVisibleRequested ? 1 : 0,
			rg_rendererMetrics.modernVisibleExecuted ? 1 : 0,
			rg_rendererMetrics.modernVisibleResourcesReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleProgramReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleSourceReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleHybridTargetReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleBackBufferReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleShadowReady ? 1 : 0,
			rg_rendererMetrics.modernVisibleHDRTargetReady ? 1 : 0,
			rg_rendererMetrics.modernVisiblePostProcessHandoff ? 1 : 0,
			rg_rendererMetrics.modernVisibleBlockedByLegacy ? 1 : 0,
			rg_rendererMetrics.modernVisibleCompositions,
			rg_rendererMetrics.modernVisibleCompositeCopies,
			rg_rendererMetrics.modernVisiblePostProcessCompositions,
			rg_rendererMetrics.modernVisibleDepthCopies,
			rg_rendererMetrics.modernVisiblePixels,
			rg_rendererMetrics.modernVisibleModernPasses,
			rg_rendererMetrics.modernVisibleLegacyPasses,
			rg_rendererMetrics.modernVisibleDisabledPasses,
			rg_rendererMetrics.modernVisibleFallbackPasses,
			rg_rendererMetrics.modernVisibleOwnerFallbacks,
			rg_rendererMetrics.modernVisibleResourceFallbacks,
			rg_rendererMetrics.modernVisibleGuiLegacyPasses,
			rg_rendererMetrics.modernVisiblePostLegacyPasses,
			rg_rendererMetrics.modernVisibleSpecialLegacyPasses,
			rg_rendererMetrics.modernVisibleSubviewLegacyPasses,
			rg_rendererMetrics.modernVisiblePresentPasses,
			rg_rendererMetrics.modernVisibleClearOps,
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_MODERN_COMPOSITE],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_MODERN_COMPOSITE] );
		common->Printf(
			"rendererMetrics gpuDriven(req=%d exec=%d res=%d validation=%d readback=%d indirect=%d multiDraw=%d source=%d eligible=%d generated=%d culled=%d visible=%d cpu=%d/%d/%d gpu=%d/%d/%d clusters=%d/%d readbacks=%d mismatches=%d indirectCalls=%d batches=%d fallback=%d dispatches=%d gpu=%d/%d)\n",
			rg_rendererMetrics.gpuDrivenRequested ? 1 : 0,
			rg_rendererMetrics.gpuDrivenExecuted ? 1 : 0,
			rg_rendererMetrics.gpuDrivenResourcesReady ? 1 : 0,
			rg_rendererMetrics.gpuDrivenValidationRequested ? 1 : 0,
			rg_rendererMetrics.gpuDrivenValidationReadbackReady ? 1 : 0,
			rg_rendererMetrics.gpuDrivenIndirectExecuted ? 1 : 0,
			rg_rendererMetrics.gpuDrivenMultiDrawReady ? 1 : 0,
			rg_rendererMetrics.gpuDrivenSourceCommands,
			rg_rendererMetrics.gpuDrivenEligibleCommands,
			rg_rendererMetrics.gpuDrivenGeneratedCommands,
			rg_rendererMetrics.gpuDrivenCulledCommands,
			rg_rendererMetrics.gpuDrivenVisibleInstances,
			rg_rendererMetrics.gpuDrivenCpuGeneratedCommands,
			rg_rendererMetrics.gpuDrivenCpuCulledCommands,
			rg_rendererMetrics.gpuDrivenCpuVisibleInstances,
			rg_rendererMetrics.gpuDrivenGpuGeneratedCommands,
			rg_rendererMetrics.gpuDrivenGpuCulledCommands,
			rg_rendererMetrics.gpuDrivenGpuVisibleInstances,
			rg_rendererMetrics.gpuDrivenCpuClusterBins,
			rg_rendererMetrics.gpuDrivenGpuClusterBins,
			rg_rendererMetrics.gpuDrivenValidationReadbacks,
			rg_rendererMetrics.gpuDrivenValidationMismatches,
			rg_rendererMetrics.gpuDrivenIndirectDrawCalls,
			rg_rendererMetrics.gpuDrivenMultiDrawBatches,
			rg_rendererMetrics.gpuDrivenIndirectFallbacks,
			rg_rendererMetrics.gpuDrivenComputeDispatches,
			rg_rendererMetrics.gpuTimerMsec[RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT],
			rg_rendererMetrics.gpuTimerSamples[RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT] );
		common->Printf(
			"rendererMetrics lowOverhead(req=%d ready=%d dsa=%d multiBind=%d bindless=%d/%d sampler=%d dsaUpdates=%d framebufferDSA=%d samplerDSA=%d/%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d compactedBatches=%d graphDSA(tex=%d params=%d fbo=%d) graphClassic(tex=%d fbo=%d) upload(persistent=%d default=%d fences=%d/%d waits=%d sync=%d))\n",
			rg_rendererMetrics.lowOverheadRequested ? 1 : 0,
			rg_rendererMetrics.lowOverheadReady ? 1 : 0,
			rg_rendererMetrics.lowOverheadUsesDSA ? 1 : 0,
			rg_rendererMetrics.lowOverheadUsesMultiBind ? 1 : 0,
			rg_rendererMetrics.lowOverheadBindlessRequested ? 1 : 0,
			rg_rendererMetrics.lowOverheadBindlessAvailable ? 1 : 0,
			rg_rendererMetrics.lowOverheadSamplerReady ? 1 : 0,
			rg_rendererMetrics.lowOverheadDSAUpdates,
			rg_rendererMetrics.lowOverheadFramebufferDSAUpdates,
			rg_rendererMetrics.lowOverheadSamplerDSACreations,
			rg_rendererMetrics.lowOverheadSamplerDSAUpdates,
			rg_rendererMetrics.lowOverheadBufferMultiBindBatches,
			rg_rendererMetrics.lowOverheadTextureMultiBindBatches,
			rg_rendererMetrics.lowOverheadSamplerMultiBindBatches,
			rg_rendererMetrics.lowOverheadClassicTextureBinds,
			rg_rendererMetrics.lowOverheadCompactedBatches,
			rg_rendererMetrics.renderGraphResourceManager.dsaTextureAllocations,
			rg_rendererMetrics.renderGraphResourceManager.dsaTextureParameterUpdates,
			rg_rendererMetrics.renderGraphResourceManager.dsaFramebufferAllocations,
			rg_rendererMetrics.renderGraphResourceManager.classicTextureAllocations,
			rg_rendererMetrics.renderGraphResourceManager.classicFramebufferAllocations,
			uploadStats.persistentMapped ? 1 : 0,
			uploadStats.lowOverheadPersistentDefault ? 1 : 0,
			uploadStats.frameFencesSubmitted,
			uploadStats.frameFencesRetired,
			uploadStats.frameFenceWaits,
			uploadStats.fenceSyncAvailable ? 1 : 0 );
		RendererBenchmarks_PrintLatestCapture();
		return;
	}

	if ( rg_rendererMetricsLastSummaryFrame < 0 || rg_rendererMetrics.frameCount - rg_rendererMetricsLastSummaryFrame >= 60 ) {
		rg_rendererMetricsLastSummaryFrame = rg_rendererMetrics.frameCount;
		common->Printf(
			"rendererMetrics summary tier=%s fe=%dms visibility=%dms packet=%dms graph=%dms submit=%dms be=%dms present=%dms gpu=%s views=%d ents=%d lights=%d draws=%d uploads=%dKB stalls=%d ring=%d/%dKB overflow=%dKB static=%dKB/%d live=%d/%dKB packets=%s:%d/%d/%d clipped=%d packetOverflow=%d cause=%s materials=%d geometryRecords=%d instances=%d resources=%d geometryRefs=%d instanceRefs=%d geometry=%d sort=%d graph=%d/%d/%d res=%d/%d/%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d invalidate=%d present=%d graphOverflow=%d graphGL=%d/%d handles=%d fbo=%d/%d materialTable=%d/%d records=%d tex=%d fallback=%d missing=%d modernExec=%s shaders=%d shaderFails=%d prep=%d/%d fallback=%d draws=%d resources=%d geometry=%d plan=%d/%d depth=%d materialFamily=%d batches=%d switches=%d submit=%d/%d submitFallback=%d missingVBO=%d missingIBO=%d indexUpload=%d submitted=%d/%d submittedFallback=%d submittedUpload=%d submitBatches=%d/%d/%d visibleDepth=%d/%d alpha=%d skinned=%d fallback=%d mismatch=%d overlay=%d gbuffer=%d/%d fallback=%d mrt=%d bw=%dKB overlay=%d deferred=%d/%d pixels=%d lights=%d reads=%d fallback=%d overlay=%d cluster=%d/%d lights=%d refs=%d overflow=%d/%d build=%dms ubo=%d stateCache=%d/%d invalid=%d legacyReset=%d\n",
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.visibilityMsec,
			rg_rendererMetrics.packetBuildMsec,
			rg_rendererMetrics.graphBuildMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
			rg_rendererMetrics.presentMsec,
			gpuText,
			rg_rendererMetrics.views,
			rg_rendererMetrics.visibleEntities,
			rg_rendererMetrics.viewLights,
			rg_rendererMetrics.drawElements,
			rg_rendererMetrics.uploadBytes / 1024,
			rg_rendererMetrics.bufferStalls,
			uploadStats.frameRingHighWaterBytes / 1024,
			uploadStats.ringSizeBytes / 1024,
			uploadStats.frameOverflowBytes / 1024,
			uploadStats.frameStaticUploadBytes / 1024,
			uploadStats.frameStaticAllocations,
			uploadStats.staticBuffersLive,
			uploadStats.staticBytesLive / 1024,
			R_RendererMetrics_ScenePacketSourceName( rg_rendererMetrics ),
			rg_rendererMetrics.scenePackets,
			rg_rendererMetrics.passPackets,
			rg_rendererMetrics.drawPackets,
			rg_rendererMetrics.clippedDrawPackets,
			rg_rendererMetrics.scenePacketOverflow ? 1 : 0,
			ScenePacketOverflowCause_Name( rg_rendererMetrics.scenePacketOverflowCause ),
			rg_rendererMetrics.materialRecords,
			rg_rendererMetrics.geometryRecords,
			rg_rendererMetrics.instanceRecords,
			rg_rendererMetrics.drawPacketsWithResourceRecord,
			rg_rendererMetrics.drawPacketsWithGeometryRecord,
			rg_rendererMetrics.drawPacketsWithInstanceRecord,
			rg_rendererMetrics.drawPacketsWithGeometry,
			rg_rendererMetrics.sortKeyValidationFailures,
			rg_rendererMetrics.renderGraphPasses,
			rg_rendererMetrics.renderGraphPassPackets,
			rg_rendererMetrics.renderGraphDrawPackets,
			rg_rendererMetrics.renderGraphResources,
			rg_rendererMetrics.renderGraphImportedResources,
			rg_rendererMetrics.renderGraphTransientResources,
			rg_rendererMetrics.renderGraphAliasableTransientResources,
			rg_rendererMetrics.renderGraphResourceAccesses,
			rg_rendererMetrics.renderGraphReadAccesses,
			rg_rendererMetrics.renderGraphWriteAccesses,
			rg_rendererMetrics.renderGraphClearOps,
			rg_rendererMetrics.renderGraphResolveOps,
			rg_rendererMetrics.renderGraphInvalidateOps,
			rg_rendererMetrics.renderGraphPresentOps,
			rg_rendererMetrics.renderGraphOverflow ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.prepared ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.available ? 1 : 0,
			rg_rendererMetrics.renderGraphResourceManager.handles,
			rg_rendererMetrics.renderGraphResourceManager.completeFramebuffers,
			rg_rendererMetrics.renderGraphResourceManager.framebufferCount,
			rg_rendererMetrics.materialResourceTable.prepared ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.available ? 1 : 0,
			rg_rendererMetrics.materialResourceTable.records,
			rg_rendererMetrics.materialResourceTable.textureBindings,
			rg_rendererMetrics.materialResourceTable.fallbackRecords,
			rg_rendererMetrics.materialResourceTable.missingImages,
			R_RendererMetrics_ModernExecutorModeName( rg_rendererMetrics.modernExecutorMode ),
			rg_rendererMetrics.modernExecutorShaderProgramCount,
			rg_rendererMetrics.modernExecutorShaderFailureCount,
			rg_rendererMetrics.modernExecutorPreparedPasses,
			rg_rendererMetrics.modernExecutorGraphPasses,
			rg_rendererMetrics.modernExecutorFallbackPasses,
			rg_rendererMetrics.modernExecutorPreparedDrawPackets,
			rg_rendererMetrics.modernExecutorResourceDrawPackets,
			rg_rendererMetrics.modernExecutorGeometryDrawPackets,
			rg_rendererMetrics.modernExecutorDrawPlanReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorDrawPlanDraws,
			rg_rendererMetrics.modernExecutorDrawPlanDepthDraws,
			rg_rendererMetrics.modernExecutorDrawPlanMaterialDraws,
			rg_rendererMetrics.modernExecutorDrawPlanStateBatches,
			rg_rendererMetrics.modernExecutorDrawPlanProgramSwitches,
			rg_rendererMetrics.modernExecutorSubmitPlanReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorSubmitPlanDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanFallbackDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanMissingAmbientDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanMissingIndexDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanIndexUploadDraws,
			rg_rendererMetrics.modernExecutorSubmitExecuted ? 1 : 0,
			rg_rendererMetrics.modernExecutorSubmittedDraws,
			rg_rendererMetrics.modernExecutorSubmittedFallbackDraws,
			rg_rendererMetrics.modernExecutorSubmittedIndexUploadDraws,
			rg_rendererMetrics.modernExecutorSubmitPlanProgramBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanVertexBufferBatches,
			rg_rendererMetrics.modernExecutorSubmitPlanIndexBufferBatches,
			rg_rendererMetrics.modernExecutorVisibleDepthDraws,
			rg_rendererMetrics.modernExecutorVisibleShadowDepthDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthAlphaTestDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthSkinnedDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthFallbackDraws + rg_rendererMetrics.modernExecutorVisibleShadowFallbackDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthMismatchDraws,
			rg_rendererMetrics.modernExecutorVisibleDepthDebugOverlayDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferExecuted ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferFallbackDraws,
			rg_rendererMetrics.modernExecutorOpaqueGBufferMRTReady ? 1 : 0,
			rg_rendererMetrics.modernExecutorOpaqueGBufferBandwidthKB,
			rg_rendererMetrics.modernExecutorOpaqueGBufferDebugOverlayDraws,
			rg_rendererMetrics.modernDeferredExecuted ? 1 : 0,
			rg_rendererMetrics.modernDeferredRequested ? 1 : 0,
			rg_rendererMetrics.modernDeferredResolvedPixels,
			rg_rendererMetrics.modernDeferredActiveLights,
			rg_rendererMetrics.modernDeferredClusterReads,
			rg_rendererMetrics.modernDeferredResourceFallbacks + rg_rendererMetrics.modernDeferredUnsupportedLightFallbacks,
			rg_rendererMetrics.modernDeferredDebugOverlayDraws,
			rg_rendererMetrics.clusteredLighting.frameValid ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.gridCount,
			rg_rendererMetrics.clusteredLighting.lightCount,
			rg_rendererMetrics.clusteredLighting.lightReferences,
			rg_rendererMetrics.clusteredLighting.overflow ? 1 : 0,
			rg_rendererMetrics.clusteredLighting.overflowClusters,
			rg_rendererMetrics.clusteredLighting.buildMsec,
			rg_rendererMetrics.clusteredLighting.uboFallbackReady ? 1 : 0,
			rg_rendererMetrics.glStateCache.hits,
			rg_rendererMetrics.glStateCache.misses,
			rg_rendererMetrics.glStateCache.forcedInvalidations,
			rg_rendererMetrics.glStateCache.legacyHandoffResets );
		RendererBenchmarks_PrintLatestCapture();
	}
}

void R_RendererMetrics_BeginGpuBackendFrame( void ) {
	rg_gpuTimerQueryActive = false;
	rg_gpuTimerActiveQuery = NULL;
	rg_gpuTimerOverflowThisFrame = false;

	if ( !R_RendererMetrics_GpuTimersEnabled() ) {
		return;
	}

	rg_gpuTimerFrameCursor = rg_gpuTimerBackendFrameCount % static_cast<int>( sizeof( rg_gpuTimerFrames ) / sizeof( rg_gpuTimerFrames[0] ) );
	rg_gpuTimerBackendFrameCount++;

	rendererGpuTimerFrame_t &frame = rg_gpuTimerFrames[rg_gpuTimerFrameCursor];
	R_RendererMetrics_PollGpuTimerFrame( frame );
	frame.numQueries = 0;
	frame.frameCount = tr.frameCount;
}

void R_RendererMetrics_EndGpuBackendFrame( void ) {
	if ( rg_gpuTimerQueryActive ) {
		R_RendererMetrics_EndGpuTimer();
	}
	if ( rg_gpuTimerOverflowThisFrame ) {
		rg_gpuTimerLatest.droppedQueries++;
	}
}

void R_RendererMetrics_BeginGpuTimer( rendererGpuTimerSlot_t slot ) {
	if ( !R_RendererMetrics_GpuTimersEnabled() || rg_gpuTimerQueryActive ) {
		return;
	}
	if ( slot < 0 || slot >= RENDERER_GPU_TIMER_COUNT ) {
		return;
	}

	rendererGpuTimerFrame_t &frame = rg_gpuTimerFrames[rg_gpuTimerFrameCursor];
	if ( frame.numQueries >= static_cast<int>( sizeof( frame.queries ) / sizeof( frame.queries[0] ) ) ) {
		rg_gpuTimerOverflowThisFrame = true;
		return;
	}

	rendererGpuTimerQuery_t &query = frame.queries[frame.numQueries++];
	if ( query.id == 0 ) {
		glGenQueries( 1, &query.id );
	}
	query.slot = slot;
	query.issued = true;

	glBeginQuery( GL_TIME_ELAPSED, query.id );
	rg_gpuTimerActiveQuery = &query;
	rg_gpuTimerQueryActive = true;
}

void R_RendererMetrics_EndGpuTimer( void ) {
	if ( !rg_gpuTimerQueryActive ) {
		return;
	}
	glEndQuery( GL_TIME_ELAPSED );
	rg_gpuTimerActiveQuery = NULL;
	rg_gpuTimerQueryActive = false;
}

void R_RendererMetrics_ShutdownGpuTimers( void ) {
	if ( rg_gpuTimerQueryActive ) {
		R_RendererMetrics_EndGpuTimer();
	}

	for ( int frameIndex = 0; frameIndex < static_cast<int>( sizeof( rg_gpuTimerFrames ) / sizeof( rg_gpuTimerFrames[0] ) ); ++frameIndex ) {
		rendererGpuTimerFrame_t &frame = rg_gpuTimerFrames[frameIndex];
		for ( int queryIndex = 0; queryIndex < static_cast<int>( sizeof( frame.queries ) / sizeof( frame.queries[0] ) ); ++queryIndex ) {
			if ( frame.queries[queryIndex].id != 0 ) {
				glDeleteQueries( 1, &frame.queries[queryIndex].id );
				frame.queries[queryIndex].id = 0;
			}
		}
		frame.numQueries = 0;
	}

	memset( &rg_gpuTimerLatest, 0, sizeof( rg_gpuTimerLatest ) );
	rg_gpuTimerFrameCursor = 0;
	rg_gpuTimerBackendFrameCount = 0;
}

bool R_RendererMetrics_GpuTimersAvailable( void ) {
	return glConfig.backendCaps.hasTimerQuery &&
		glGenQueries != NULL &&
		glDeleteQueries != NULL &&
		glBeginQuery != NULL &&
		glEndQuery != NULL &&
		glGetQueryObjectiv != NULL &&
		glGetQueryObjectuiv != NULL;
}

bool RendererGpuTimer_RunSelfTest( void ) {
	if ( !R_RendererMetrics_GpuTimersAvailable() ) {
		common->Printf( "RendererGpuTimer self-test skipped: GL timer queries are unavailable\n" );
		return true;
	}

	GLuint query = 0;
	glGenQueries( 1, &query );
	if ( query == 0 ) {
		common->Printf( "RendererGpuTimer self-test failed: glGenQueries returned 0\n" );
		return false;
	}

	glBeginQuery( GL_TIME_ELAPSED, query );
	glClear( GL_COLOR_BUFFER_BIT );
	glEndQuery( GL_TIME_ELAPSED );
	glFinish();

	GLint available = 0;
	glGetQueryObjectiv( query, GL_QUERY_RESULT_AVAILABLE, &available );
	if ( !available ) {
		glDeleteQueries( 1, &query );
		common->Printf( "RendererGpuTimer self-test failed: query result unavailable after glFinish\n" );
		return false;
	}

	GLuint64 elapsedNsec = 0;
	if ( glGetQueryObjectui64v != NULL ) {
		glGetQueryObjectui64v( query, GL_QUERY_RESULT, &elapsedNsec );
	} else {
		GLuint elapsedNsec32 = 0;
		glGetQueryObjectuiv( query, GL_QUERY_RESULT, &elapsedNsec32 );
		elapsedNsec = elapsedNsec32;
	}
	glDeleteQueries( 1, &query );

	common->Printf( "RendererGpuTimer self-test passed (%u usec)\n", static_cast<unsigned int>( elapsedNsec / 1000ULL ) );
	return true;
}
