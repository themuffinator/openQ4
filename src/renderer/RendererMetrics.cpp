// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"

typedef struct rendererMetricsFrame_s {
	int		frameCount;
	int		frontEndMsec;
	int		sceneExtractionMsec;
	int		visibilityMsec;
	int		submitMsec;
	int		backEndMsec;
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
	int		materialRecords;
	int		drawPacketsWithMaterial;
	int		drawPacketsWithResourceRecord;
	int		drawPacketsWithGeometry;
	int		drawPacketsWithShaderRegisters;
	int		drawPacketsWithIndexCache;
	int		drawPacketsWithAmbientCache;
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
	int		renderGraphPresentOps;
	bool	renderGraphOverflow;
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
	bool	frontEndDerived;
	bool	backendDerived;
	bool	overflow;
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
} rendererModernExecutorLatest_t;

static rendererMetricsFrame_t rg_rendererMetrics;
static int rg_rendererMetricsLastSummaryFrame = -1;
static rendererGpuTimerFrame_t rg_gpuTimerFrames[4];
static rendererGpuTimerLatest_t rg_gpuTimerLatest;
static rendererScenePacketLatest_t rg_scenePacketLatest;
static rendererRenderGraphLatest_t rg_renderGraphLatest;
static rendererModernExecutorLatest_t rg_modernExecutorLatest;
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
	rg_rendererMetrics.materialRecords = rg_scenePacketLatest.materialRecords;
	rg_rendererMetrics.drawPacketsWithMaterial = rg_scenePacketLatest.drawPacketsWithMaterial;
	rg_rendererMetrics.drawPacketsWithResourceRecord = rg_scenePacketLatest.drawPacketsWithResourceRecord;
	rg_rendererMetrics.drawPacketsWithGeometry = rg_scenePacketLatest.drawPacketsWithGeometry;
	rg_rendererMetrics.drawPacketsWithShaderRegisters = rg_scenePacketLatest.drawPacketsWithShaderRegisters;
	rg_rendererMetrics.drawPacketsWithIndexCache = rg_scenePacketLatest.drawPacketsWithIndexCache;
	rg_rendererMetrics.drawPacketsWithAmbientCache = rg_scenePacketLatest.drawPacketsWithAmbientCache;
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
	rg_rendererMetrics.renderGraphPresentOps = rg_renderGraphLatest.presentOps;
	rg_rendererMetrics.renderGraphOverflow = rg_renderGraphLatest.overflow;
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
}

void R_RendererMetrics_RecordSubmitMsec( int submitMsec ) {
	rg_rendererMetrics.submitMsec += submitMsec;
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

void R_RendererMetrics_RecordScenePackets( int scenePackets, int passPackets, int drawPackets, int clippedDrawPackets, int commandPackets, int legacyDrawViews, int materialRecords, int drawPacketsWithMaterial, int drawPacketsWithResourceRecord, int drawPacketsWithGeometry, int drawPacketsWithShaderRegisters, int drawPacketsWithIndexCache, int drawPacketsWithAmbientCache, bool frontEndDerived, bool backendDerived, bool overflow ) {
	rg_scenePacketLatest.scenePackets = scenePackets;
	rg_scenePacketLatest.passPackets = passPackets;
	rg_scenePacketLatest.drawPackets = drawPackets;
	rg_scenePacketLatest.clippedDrawPackets = clippedDrawPackets;
	rg_scenePacketLatest.commandPackets = commandPackets;
	rg_scenePacketLatest.legacyDrawViews = legacyDrawViews;
	rg_scenePacketLatest.materialRecords = materialRecords;
	rg_scenePacketLatest.drawPacketsWithMaterial = drawPacketsWithMaterial;
	rg_scenePacketLatest.drawPacketsWithResourceRecord = drawPacketsWithResourceRecord;
	rg_scenePacketLatest.drawPacketsWithGeometry = drawPacketsWithGeometry;
	rg_scenePacketLatest.drawPacketsWithShaderRegisters = drawPacketsWithShaderRegisters;
	rg_scenePacketLatest.drawPacketsWithIndexCache = drawPacketsWithIndexCache;
	rg_scenePacketLatest.drawPacketsWithAmbientCache = drawPacketsWithAmbientCache;
	rg_scenePacketLatest.frontEndDerived = frontEndDerived;
	rg_scenePacketLatest.backendDerived = backendDerived;
	rg_scenePacketLatest.overflow = overflow;
}

void R_RendererMetrics_RecordRenderGraph( int graphPasses, int passPackets, int scenePackets, int drawPackets, int commandPackets, int resources, int importedResources, int transientResources, int aliasableTransientResources, int resourceAccesses, int readAccesses, int writeAccesses, int clearOps, int resolveOps, int presentOps, bool overflow ) {
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
	rg_renderGraphLatest.presentOps = presentOps;
	rg_renderGraphLatest.overflow = overflow;
}

void R_RendererMetrics_RecordModernExecutor( rendererModernExecutorMetricsMode_t mode, int graphPasses, int preparedPasses, int fallbackPasses, int preparedDrawPackets, int materialDrawPackets, int resourceDrawPackets, int geometryDrawPackets, bool vaoReady, bool frameUBOReady, bool shaderLibraryReady, int shaderProgramCount, int shaderFailureCount, bool drawPlanReady, bool drawPlanOverflow, int drawPlanDraws, int drawPlanDepthDraws, int drawPlanMaterialDraws, int drawPlanFallbackDraws, int drawPlanStateBatches, int drawPlanProgramSwitches, int drawPlanMaterialSwitches, bool submitPlanReady, bool submitPlanOverflow, int submitPlanDraws, int submitPlanFallbackDraws, int submitPlanDepthDraws, int submitPlanMaterialDraws, int submitPlanMissingAmbientDraws, int submitPlanMissingIndexDraws, int submitPlanIndexUploadDraws, bool submitExecuted, int submittedDraws, int submittedFallbackDraws, int submittedIndexUploadDraws, int submitPlanProgramBatches, int submitPlanVertexBufferBatches, int submitPlanIndexBufferBatches, int submitPlanScissorBatches, int submitPlanMaterialBatches, int submitPlanUniformUpdates, int submitPlanFrameUBOBinds ) {
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
}

void R_RendererMetrics_AddUploadBytes( int bytes ) {
	if ( bytes > 0 ) {
		rg_rendererMetrics.uploadBytes += bytes;
	}
}

void R_RendererMetrics_AddBufferStall( void ) {
	rg_rendererMetrics.bufferStalls++;
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

	const int detail = r_rendererMetrics.GetInteger();
	const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
	char gpuText[32];
	R_RendererMetrics_FormatGpuMsec( rg_rendererMetrics, gpuText, sizeof( gpuText ) );
	if ( detail >= 2 ) {
		common->Printf(
			"rendererMetrics frame=%d tier=%s fe=%dms submit=%dms be=%dms gpu=%s views=%d ents=%d lights=%d draws=%d surf=%d verts=%d idx=%d uploads=%d stalls=%d ring=%d/%dKB allocs=%d overflow=%d static=%dKB/%d live=%d/%dKB writes(p=%d map=%d sub=%d) packets(source=%s scene=%d pass=%d draw=%d clipped=%d cmd=%d views=%d overflow=%d) resources(materials=%d withMaterial=%d records=%d geometry=%d regs=%d ibo=%d vbo=%d) graph(pass=%d packets=%d scenes=%d draw=%d cmd=%d res=%d imported=%d transient=%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d present=%d overflow=%d) modernExec(mode=%s vao=%d ubo=%d shaderLib=%d shaders=%d shaderFails=%d passes=%d/%d fallback=%d draws=%d material=%d resources=%d geometry=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d switches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitDepth=%d submitMaterial=%d submitFallback=%d missing(vbo=%d ibo=%d) indexUpload=%d submitted=%d/%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d) gpuPass(3d=%d/%d 2d=%d/%d rt=%d/%d copy=%d/%d special=%d/%d setbuf=%d/%d dropped=%d) cmds(3d=%d 2d=%d rt=%d copy=%d swap=%d)\n",
			rg_rendererMetrics.frameCount,
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
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
			rg_rendererMetrics.materialRecords,
			rg_rendererMetrics.drawPacketsWithMaterial,
			rg_rendererMetrics.drawPacketsWithResourceRecord,
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
			rg_rendererMetrics.renderGraphPresentOps,
			rg_rendererMetrics.renderGraphOverflow ? 1 : 0,
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
			rg_rendererMetrics.gpuTimerDroppedQueries,
			rg_rendererMetrics.draw3d,
			rg_rendererMetrics.draw2d,
			rg_rendererMetrics.renderTargetOps,
			rg_rendererMetrics.copyRenders,
			rg_rendererMetrics.swapBuffers );
		return;
	}

	if ( rg_rendererMetricsLastSummaryFrame < 0 || rg_rendererMetrics.frameCount - rg_rendererMetricsLastSummaryFrame >= 60 ) {
		rg_rendererMetricsLastSummaryFrame = rg_rendererMetrics.frameCount;
		common->Printf(
			"rendererMetrics summary tier=%s fe=%dms submit=%dms be=%dms gpu=%s views=%d ents=%d lights=%d draws=%d uploads=%dKB stalls=%d ring=%d/%dKB overflow=%dKB static=%dKB/%d live=%d/%dKB packets=%s:%d/%d/%d clipped=%d packetOverflow=%d materials=%d resources=%d geometry=%d graph=%d/%d/%d res=%d/%d/%d aliasable=%d access=%d read=%d write=%d clear=%d resolve=%d present=%d graphOverflow=%d modernExec=%s shaders=%d shaderFails=%d prep=%d/%d fallback=%d draws=%d resources=%d geometry=%d plan=%d/%d depth=%d materialFamily=%d batches=%d switches=%d submit=%d/%d submitFallback=%d missingVBO=%d missingIBO=%d indexUpload=%d submitted=%d/%d submittedFallback=%d submittedUpload=%d submitBatches=%d/%d/%d\n",
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
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
			rg_rendererMetrics.materialRecords,
			rg_rendererMetrics.drawPacketsWithResourceRecord,
			rg_rendererMetrics.drawPacketsWithGeometry,
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
			rg_rendererMetrics.renderGraphPresentOps,
			rg_rendererMetrics.renderGraphOverflow ? 1 : 0,
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
			rg_rendererMetrics.modernExecutorSubmitPlanIndexBufferBatches );
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
