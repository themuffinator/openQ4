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

static rendererMetricsFrame_t rg_rendererMetrics;
static int rg_rendererMetricsLastSummaryFrame = -1;
static rendererGpuTimerFrame_t rg_gpuTimerFrames[4];
static rendererGpuTimerLatest_t rg_gpuTimerLatest;
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
			"rendererMetrics frame=%d tier=%s fe=%dms submit=%dms be=%dms gpu=%s views=%d ents=%d lights=%d draws=%d surf=%d verts=%d idx=%d uploads=%d stalls=%d ring=%d/%dKB allocs=%d overflow=%d writes(p=%d map=%d sub=%d) gpuPass(3d=%d/%d 2d=%d/%d rt=%d/%d copy=%d/%d special=%d/%d setbuf=%d/%d dropped=%d) cmds(3d=%d 2d=%d rt=%d copy=%d swap=%d)\n",
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
			uploadStats.framePersistentWrites,
			uploadStats.frameMapRangeWrites,
			uploadStats.frameSubDataWrites,
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
			"rendererMetrics summary tier=%s fe=%dms submit=%dms be=%dms gpu=%s views=%d ents=%d lights=%d draws=%d uploads=%dKB stalls=%d ring=%d/%dKB overflow=%dKB\n",
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
			uploadStats.frameOverflowBytes / 1024 );
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
