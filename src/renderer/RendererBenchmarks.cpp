// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererBenchmarks.h"

const int RENDERER_BENCHMARK_HISTORY = 256;

typedef struct rendererBenchmarkPreset_s {
	const char *	name;
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
} rendererBenchmarkPreset_t;

typedef struct rendererBenchmarkPercentiles_s {
	int				averageMsec;
	int				p50Msec;
	int				p95Msec;
	int				p99Msec;
	int				maxMsec;
} rendererBenchmarkPercentiles_t;

static const rendererBenchmarkPreset_t rg_benchmarkPresets[] = {
	{ "low", 75, true, 4, 3, 8, 32, 16, 512, 2, 0, 33, 50 },
	{ "baseline", 100, false, 6, 4, 12, 64, 32, 1024, 1, 1, 20, 28 },
	{ "modern", 100, false, 8, 6, 16, 96, 64, 1024, 1, 2, 16, 24 },
	{ "high-end", 100, false, 8, 6, 16, 128, 96, 2048, 1, 3, 12, 18 }
};

static rendererBenchmarkFrameSample_t rg_benchmarkSamples[RENDERER_BENCHMARK_HISTORY];
static int rg_benchmarkSampleCursor = 0;
static int rg_benchmarkSampleCount = 0;
static rendererBenchmarkFrameSample_t rg_benchmarkLatestSample;
static rendererBenchmarkBudget_t rg_benchmarkBudget;

static int RendererBenchmarks_NumPresets( void ) {
	return static_cast<int>( sizeof( rg_benchmarkPresets ) / sizeof( rg_benchmarkPresets[0] ) );
}

static const rendererBenchmarkPreset_t &RendererBenchmarks_PresetForName( const char *name ) {
	if ( name != NULL && name[0] != '\0' ) {
		for ( int i = 0; i < RendererBenchmarks_NumPresets(); ++i ) {
			if ( idStr::Icmp( name, rg_benchmarkPresets[i].name ) == 0 ) {
				return rg_benchmarkPresets[i];
			}
		}
	}
	return rg_benchmarkPresets[1];
}

static int RendererBenchmarks_ClampBudgetInt( int value, int minValue, int maxValue ) {
	if ( value < minValue ) {
		return minValue;
	}
	if ( value > maxValue ) {
		return maxValue;
	}
	return value;
}

static const rendererBenchmarkBudget_t &RendererBenchmarks_BuildBudget( void ) {
	const rendererBenchmarkPreset_t &preset = RendererBenchmarks_PresetForName( r_rendererBenchmarkPreset.GetString() );
	rg_benchmarkBudget.presetName = preset.name;
	rg_benchmarkBudget.screenPercentage = RendererBenchmarks_ClampBudgetInt( preset.screenPercentage, 10, 100 );
	rg_benchmarkBudget.dynamicResolutionAllowed = preset.dynamicResolutionAllowed && r_rendererDynamicResolution.GetBool();
	rg_benchmarkBudget.clusterTilesX = RendererBenchmarks_ClampBudgetInt( preset.clusterTilesX, 1, 8 );
	rg_benchmarkBudget.clusterTilesY = RendererBenchmarks_ClampBudgetInt( preset.clusterTilesY, 1, 6 );
	rg_benchmarkBudget.clusterSlicesZ = RendererBenchmarks_ClampBudgetInt( preset.clusterSlicesZ, 1, 16 );
	rg_benchmarkBudget.materialBatchTarget = RendererBenchmarks_ClampBudgetInt( preset.materialBatchTarget, 1, 512 );
	rg_benchmarkBudget.lightBatchTarget = RendererBenchmarks_ClampBudgetInt( preset.lightBatchTarget, 1, 512 );
	rg_benchmarkBudget.shadowMapSize = RendererBenchmarks_ClampBudgetInt( preset.shadowMapSize, 128, 4096 );
	rg_benchmarkBudget.shadowUpdateRate = RendererBenchmarks_ClampBudgetInt( preset.shadowUpdateRate, 1, 16 );
	rg_benchmarkBudget.postProcessQuality = RendererBenchmarks_ClampBudgetInt( preset.postProcessQuality, 0, 3 );
	rg_benchmarkBudget.p95BudgetMsec = r_rendererPerfThresholdP95.GetInteger() > 0 ? r_rendererPerfThresholdP95.GetInteger() : preset.p95BudgetMsec;
	rg_benchmarkBudget.p99BudgetMsec = r_rendererPerfThresholdP99.GetInteger() > 0 ? r_rendererPerfThresholdP99.GetInteger() : preset.p99BudgetMsec;
	return rg_benchmarkBudget;
}

const rendererBenchmarkBudget_t &RendererBenchmarks_CurrentBudget( void ) {
	return RendererBenchmarks_BuildBudget();
}

bool RendererBenchmarks_AdaptiveClusterGridEnabled( void ) {
	return r_rendererAdaptiveClusterGrid.GetBool();
}

static int RendererBenchmarks_PercentileIndex( int sampleCount, int percentile ) {
	if ( sampleCount <= 0 ) {
		return 0;
	}
	const int rank = ( sampleCount * percentile + 99 ) / 100;
	return RendererBenchmarks_ClampBudgetInt( rank - 1, 0, sampleCount - 1 );
}

static void RendererBenchmarks_SortFrameTimes( int *values, int sampleCount ) {
	for ( int i = 1; i < sampleCount; ++i ) {
		const int key = values[i];
		int j = i - 1;
		while ( j >= 0 && values[j] > key ) {
			values[j + 1] = values[j];
			j--;
		}
		values[j + 1] = key;
	}
}

static rendererBenchmarkPercentiles_t RendererBenchmarks_CalculatePercentiles( const rendererBenchmarkFrameSample_t *samples, int sampleCount ) {
	rendererBenchmarkPercentiles_t result;
	memset( &result, 0, sizeof( result ) );
	if ( samples == NULL || sampleCount <= 0 ) {
		return result;
	}

	int frameTimes[RENDERER_BENCHMARK_HISTORY];
	int total = 0;
	const int count = Min( sampleCount, RENDERER_BENCHMARK_HISTORY );
	for ( int i = 0; i < count; ++i ) {
		frameTimes[i] = Max( 0, samples[i].frameMsec );
		total += frameTimes[i];
	}
	RendererBenchmarks_SortFrameTimes( frameTimes, count );

	result.averageMsec = ( total + count / 2 ) / count;
	result.p50Msec = frameTimes[RendererBenchmarks_PercentileIndex( count, 50 )];
	result.p95Msec = frameTimes[RendererBenchmarks_PercentileIndex( count, 95 )];
	result.p99Msec = frameTimes[RendererBenchmarks_PercentileIndex( count, 99 )];
	result.maxMsec = frameTimes[count - 1];
	return result;
}

static rendererBenchmarkPercentiles_t RendererBenchmarks_CurrentPercentiles( void ) {
	rendererBenchmarkFrameSample_t orderedSamples[RENDERER_BENCHMARK_HISTORY];
	const int count = rg_benchmarkSampleCount;
	for ( int i = 0; i < count; ++i ) {
		const int sampleIndex = ( rg_benchmarkSampleCursor - count + i + RENDERER_BENCHMARK_HISTORY ) % RENDERER_BENCHMARK_HISTORY;
		orderedSamples[i] = rg_benchmarkSamples[sampleIndex];
	}
	return RendererBenchmarks_CalculatePercentiles( orderedSamples, count );
}

void RendererBenchmarks_RecordFrame( const rendererBenchmarkFrameSample_t &sample ) {
	rg_benchmarkLatestSample = sample;
	rg_benchmarkSamples[rg_benchmarkSampleCursor] = sample;
	rg_benchmarkSampleCursor = ( rg_benchmarkSampleCursor + 1 ) % RENDERER_BENCHMARK_HISTORY;
	if ( rg_benchmarkSampleCount < RENDERER_BENCHMARK_HISTORY ) {
		rg_benchmarkSampleCount++;
	}
}

void RendererBenchmarks_PrintLatestCapture( void ) {
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	const rendererBenchmarkPercentiles_t percentiles = RendererBenchmarks_CurrentPercentiles();
	const bool thresholdsPassed =
		rg_benchmarkSampleCount == 0 ||
		( percentiles.p95Msec <= budget.p95BudgetMsec && percentiles.p99Msec <= budget.p99BudgetMsec );
	const rendererBenchmarkFrameSample_t &sample = rg_benchmarkLatestSample;

	common->Printf(
		"rendererBenchmark capture(preset=%s samples=%d frame(avg=%d p50=%d p95=%d p99=%d max=%d latest=%d thresholds=%d/%d pass=%d) cpu(fe=%d visibility=%d packet=%d graph=%d submit=%d backend=%d present=%d) gpu(total=%d 3d=%d/%d 2d=%d/%d rt=%d/%d copy=%d/%d special=%d/%d setbuf=%d/%d swap=%d/%d deferred=%d/%d forward=%d/%d composite=%d/%d indirect=%d/%d) work(upload=%dKB draws=%d surf=%d verts=%d idx=%d ents=%d lights=%d packets=%d/%d/%d graph=%d/%d clusters=%d/%d lights=%d refs=%d overflow=%d fallback(draw=%d submit=%d gbuffer=%d deferred=%d forward=%d visible=%d owner=%d resource=%d)) budget(screen=%d dynamic=%d cluster=%dx%dx%d materialBatch=%d lightBatch=%d shadow=%d update=%d post=%d adaptiveCluster=%d))\n",
		budget.presetName,
		rg_benchmarkSampleCount,
		percentiles.averageMsec,
		percentiles.p50Msec,
		percentiles.p95Msec,
		percentiles.p99Msec,
		percentiles.maxMsec,
		sample.frameMsec,
		budget.p95BudgetMsec,
		budget.p99BudgetMsec,
		thresholdsPassed ? 1 : 0,
		sample.frontEndMsec,
		sample.visibilityMsec,
		sample.packetBuildMsec,
		sample.graphBuildMsec,
		sample.submitMsec,
		sample.backEndMsec,
		sample.presentMsec,
		sample.gpuMsec,
		sample.gpuPassMsec[RENDERER_GPU_TIMER_DRAW3D],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_DRAW3D],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_DRAW2D],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_DRAW2D],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_RENDER_TARGET],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_RENDER_TARGET],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_COPY_RENDER],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_COPY_RENDER],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_SPECIAL_EFFECTS],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_SPECIAL_EFFECTS],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_SET_BUFFER],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_SET_BUFFER],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_SWAP_BUFFERS],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_SWAP_BUFFERS],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_MODERN_DEFERRED],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_MODERN_DEFERRED],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_MODERN_FORWARD],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_MODERN_FORWARD],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_MODERN_COMPOSITE],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_MODERN_COMPOSITE],
		sample.gpuPassMsec[RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT],
		sample.gpuPassSamples[RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT],
		sample.uploadBytes / 1024,
		sample.drawElements,
		sample.surfaces,
		sample.vertexes,
		sample.indexes,
		sample.visibleEntities,
		sample.viewLights,
		sample.scenePackets,
		sample.passPackets,
		sample.drawPackets,
		sample.renderGraphPasses,
		sample.renderGraphResources,
		sample.clusterCount,
		sample.clusterActiveCount,
		sample.clusterLightCount,
		sample.clusterReferenceCount,
		sample.clusterOverflowCount,
		sample.drawPlanFallbacks,
		sample.submitPlanFallbacks,
		sample.opaqueGBufferFallbacks,
		sample.deferredFallbacks,
		sample.forwardFallbacks,
		sample.visibleFallbacks,
		sample.visibleOwnerFallbacks,
		sample.visibleResourceFallbacks,
		budget.screenPercentage,
		budget.dynamicResolutionAllowed ? 1 : 0,
		budget.clusterTilesX,
		budget.clusterTilesY,
		budget.clusterSlicesZ,
		budget.materialBatchTarget,
		budget.lightBatchTarget,
		budget.shadowMapSize,
		budget.shadowUpdateRate,
		budget.postProcessQuality,
		RendererBenchmarks_AdaptiveClusterGridEnabled() ? 1 : 0 );
}

void RendererBenchmarks_PrintGfxInfo( void ) {
	const rendererBenchmarkBudget_t &budget = RendererBenchmarks_CurrentBudget();
	const rendererBenchmarkPercentiles_t percentiles = RendererBenchmarks_CurrentPercentiles();
	const bool thresholdsPassed =
		rg_benchmarkSampleCount == 0 ||
		( percentiles.p95Msec <= budget.p95BudgetMsec && percentiles.p99Msec <= budget.p99BudgetMsec );

	common->Printf(
		"Renderer benchmark: preset=%s samples=%d frame(avg=%d p50=%d p95=%d p99=%d max=%d) thresholds(p95=%d p99=%d pass=%d) budget(screen=%d dynamic=%d cluster=%dx%dx%d adaptiveCluster=%d materialBatch=%d lightBatch=%d shadow=%d update=%d post=%d)\n",
		budget.presetName,
		rg_benchmarkSampleCount,
		percentiles.averageMsec,
		percentiles.p50Msec,
		percentiles.p95Msec,
		percentiles.p99Msec,
		percentiles.maxMsec,
		budget.p95BudgetMsec,
		budget.p99BudgetMsec,
		thresholdsPassed ? 1 : 0,
		budget.screenPercentage,
		budget.dynamicResolutionAllowed ? 1 : 0,
		budget.clusterTilesX,
		budget.clusterTilesY,
		budget.clusterSlicesZ,
		RendererBenchmarks_AdaptiveClusterGridEnabled() ? 1 : 0,
		budget.materialBatchTarget,
		budget.lightBatchTarget,
		budget.shadowMapSize,
		budget.shadowUpdateRate,
		budget.postProcessQuality );
	common->Printf(
		"Performance regression thresholds: preset=%s p95=%dms p99=%dms custom=%d\n",
		budget.presetName,
		budget.p95BudgetMsec,
		budget.p99BudgetMsec,
		( r_rendererPerfThresholdP95.GetInteger() > 0 || r_rendererPerfThresholdP99.GetInteger() > 0 ) ? 1 : 0 );
}

bool RendererBenchmark_RunSelfTest( void ) {
	rendererBenchmarkFrameSample_t samples[5];
	memset( samples, 0, sizeof( samples ) );
	samples[0].frameMsec = 33;
	samples[1].frameMsec = 16;
	samples[2].frameMsec = 8;
	samples[3].frameMsec = 17;
	samples[4].frameMsec = 24;

	const rendererBenchmarkPercentiles_t percentiles = RendererBenchmarks_CalculatePercentiles( samples, 5 );
	bool ok = true;
	if ( percentiles.p50Msec != 17 || percentiles.p95Msec != 33 || percentiles.p99Msec != 33 || percentiles.maxMsec != 33 ) {
		common->Printf(
			"RendererBenchmark self-test failed: percentile mismatch (p50=%d p95=%d p99=%d max=%d)\n",
			percentiles.p50Msec,
			percentiles.p95Msec,
			percentiles.p99Msec,
			percentiles.maxMsec );
		ok = false;
	}

	for ( int i = 0; i < RendererBenchmarks_NumPresets(); ++i ) {
		if ( rg_benchmarkPresets[i].clusterTilesX < 1 || rg_benchmarkPresets[i].clusterTilesX > 8 ||
			rg_benchmarkPresets[i].clusterTilesY < 1 || rg_benchmarkPresets[i].clusterTilesY > 6 ||
			rg_benchmarkPresets[i].clusterSlicesZ < 1 || rg_benchmarkPresets[i].clusterSlicesZ > 16 ||
			rg_benchmarkPresets[i].p95BudgetMsec <= 0 || rg_benchmarkPresets[i].p99BudgetMsec < rg_benchmarkPresets[i].p95BudgetMsec ) {
			common->Printf( "RendererBenchmark self-test failed: invalid preset '%s'\n", rg_benchmarkPresets[i].name );
			ok = false;
		}
	}

	if ( !ok ) {
		return false;
	}

	common->Printf(
		"RendererBenchmark self-test passed (presets=%d samples=5 p50=%d p95=%d p99=%d)\n",
		RendererBenchmarks_NumPresets(),
		percentiles.p50Msec,
		percentiles.p95Msec,
		percentiles.p99Msec );
	return true;
}
