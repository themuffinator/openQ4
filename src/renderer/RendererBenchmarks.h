// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_BENCHMARKS_H__
#define __RENDERER_BENCHMARKS_H__

#include "RendererMetrics.h"

typedef struct rendererBenchmarkBudget_s {
	const char *	presetName;
	int				screenPercentage;
	bool			dynamicResolutionAllowed;
	int				clusterTilesX;
	int				clusterTilesY;
	int				clusterSlicesZ;
	int				materialBatchTarget;
	int				lightBatchTarget;
	int				shadowMapSize;
	int				shadowUpdateRate;
	int				postProcessQuality;
	int				p95BudgetMsec;
	int				p99BudgetMsec;
} rendererBenchmarkBudget_t;

typedef struct rendererBenchmarkFrameSample_s {
	int				frameMsec;
	int				frontEndMsec;
	int				visibilityMsec;
	int				packetBuildMsec;
	int				graphBuildMsec;
	int				submitMsec;
	int				backEndMsec;
	int				presentMsec;
	int				gpuMsec;
	int				gpuPassMsec[RENDERER_GPU_TIMER_COUNT];
	int				gpuPassSamples[RENDERER_GPU_TIMER_COUNT];
	int				uploadBytes;
	int				drawElements;
	int				surfaces;
	int				vertexes;
	int				indexes;
	int				visibleEntities;
	int				viewLights;
	int				scenePackets;
	int				passPackets;
	int				drawPackets;
	int				renderGraphPasses;
	int				renderGraphResources;
	int				clusterCount;
	int				clusterActiveCount;
	int				clusterLightCount;
	int				clusterReferenceCount;
	int				clusterOverflowCount;
	int				drawPlanFallbacks;
	int				submitPlanFallbacks;
	int				opaqueGBufferFallbacks;
	int				deferredFallbacks;
	int				forwardFallbacks;
	int				visibleFallbacks;
	int				visibleOwnerFallbacks;
	int				visibleResourceFallbacks;
} rendererBenchmarkFrameSample_t;

const rendererBenchmarkBudget_t &RendererBenchmarks_CurrentBudget( void );
bool RendererBenchmarks_AdaptiveClusterGridEnabled( void );
void RendererBenchmarks_RecordFrame( const rendererBenchmarkFrameSample_t &sample );
void RendererBenchmarks_PrintLatestCapture( void );
void RendererBenchmarks_PrintGfxInfo( void );
bool RendererBenchmark_RunSelfTest( void );

#endif /* !__RENDERER_BENCHMARKS_H__ */
