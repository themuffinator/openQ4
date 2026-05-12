// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererMetrics.h"

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
} rendererMetricsFrame_t;

static rendererMetricsFrame_t rg_rendererMetrics;
static int rg_rendererMetricsLastSummaryFrame = -1;

void R_RendererMetrics_BeginFrame( int frameCount ) {
	memset( &rg_rendererMetrics, 0, sizeof( rg_rendererMetrics ) );
	rg_rendererMetrics.frameCount = frameCount;
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
	if ( detail >= 2 ) {
		common->Printf(
			"rendererMetrics frame=%d tier=%s fe=%dms submit=%dms be=%dms gpu=not-sampled views=%d ents=%d lights=%d draws=%d surf=%d verts=%d idx=%d uploads=%d stalls=%d cmds(3d=%d 2d=%d rt=%d copy=%d swap=%d)\n",
			rg_rendererMetrics.frameCount,
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
			rg_rendererMetrics.views,
			rg_rendererMetrics.visibleEntities,
			rg_rendererMetrics.viewLights,
			rg_rendererMetrics.drawElements,
			rg_rendererMetrics.surfaces,
			rg_rendererMetrics.vertexes,
			rg_rendererMetrics.indexes,
			rg_rendererMetrics.uploadBytes,
			rg_rendererMetrics.bufferStalls,
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
			"rendererMetrics summary tier=%s fe=%dms submit=%dms be=%dms gpu=not-sampled views=%d ents=%d lights=%d draws=%d uploads=%dKB stalls=%d\n",
			RendererTier_Name( glConfig.rendererTier ),
			rg_rendererMetrics.frontEndMsec,
			rg_rendererMetrics.submitMsec,
			rg_rendererMetrics.backEndMsec,
			rg_rendererMetrics.views,
			rg_rendererMetrics.visibleEntities,
			rg_rendererMetrics.viewLights,
			rg_rendererMetrics.drawElements,
			rg_rendererMetrics.uploadBytes / 1024,
			rg_rendererMetrics.bufferStalls );
	}
}
