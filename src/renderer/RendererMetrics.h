// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_METRICS_H__
#define __RENDERER_METRICS_H__

enum rendererGpuTimerSlot_t {
	RENDERER_GPU_TIMER_SET_BUFFER = 0,
	RENDERER_GPU_TIMER_DRAW3D,
	RENDERER_GPU_TIMER_DRAW2D,
	RENDERER_GPU_TIMER_SPECIAL_EFFECTS,
	RENDERER_GPU_TIMER_RENDER_TARGET,
	RENDERER_GPU_TIMER_COPY_RENDER,
	RENDERER_GPU_TIMER_SWAP_BUFFERS,
	RENDERER_GPU_TIMER_COUNT
};

enum rendererModernExecutorMetricsMode_t {
	RENDERER_MODERN_EXECUTOR_METRICS_OFF = 0,
	RENDERER_MODERN_EXECUTOR_METRICS_UNAVAILABLE,
	RENDERER_MODERN_EXECUTOR_METRICS_PREPARED,
	RENDERER_MODERN_EXECUTOR_METRICS_LEGACY_FALLBACK
};

void R_RendererMetrics_BeginFrame( int frameCount );
void R_RendererMetrics_RecordSubmitMsec( int submitMsec );
void R_RendererMetrics_RecordBackendCommands( int draw3d, int draw2d, int setBuffers, int swapBuffers, int copyRenders, int specialEffects, int renderTargetOps );
void R_RendererMetrics_RecordScenePackets( int scenePackets, int passPackets, int drawPackets, int clippedDrawPackets, int commandPackets, int legacyDrawViews, int materialRecords, int drawPacketsWithMaterial, int drawPacketsWithResourceRecord, int drawPacketsWithGeometry, int drawPacketsWithShaderRegisters, int drawPacketsWithIndexCache, int drawPacketsWithAmbientCache, bool frontEndDerived, bool backendDerived, bool overflow );
void R_RendererMetrics_RecordRenderGraph( int graphPasses, int passPackets, int scenePackets, int drawPackets, int commandPackets, int resources, int importedResources, int transientResources, int aliasableTransientResources, int resourceAccesses, int readAccesses, int writeAccesses, int clearOps, int resolveOps, int presentOps, bool overflow );
void R_RendererMetrics_RecordModernExecutor( rendererModernExecutorMetricsMode_t mode, int graphPasses, int preparedPasses, int fallbackPasses, int preparedDrawPackets, int materialDrawPackets, int resourceDrawPackets, int geometryDrawPackets, bool vaoReady, bool frameUBOReady, bool shaderLibraryReady, int shaderProgramCount, int shaderFailureCount, bool drawPlanReady, bool drawPlanOverflow, int drawPlanDraws, int drawPlanDepthDraws, int drawPlanMaterialDraws, int drawPlanFallbackDraws, int drawPlanStateBatches, int drawPlanProgramSwitches, int drawPlanMaterialSwitches, bool submitPlanReady, bool submitPlanOverflow, int submitPlanDraws, int submitPlanFallbackDraws, int submitPlanDepthDraws, int submitPlanMaterialDraws, int submitPlanMissingAmbientDraws, int submitPlanMissingIndexDraws, int submitPlanIndexUploadDraws, bool submitExecuted, int submittedDraws, int submittedFallbackDraws, int submittedIndexUploadDraws, int submitPlanProgramBatches, int submitPlanVertexBufferBatches, int submitPlanIndexBufferBatches, int submitPlanScissorBatches, int submitPlanMaterialBatches, int submitPlanUniformUpdates, int submitPlanFrameUBOBinds );
void R_RendererMetrics_AddUploadBytes( int bytes );
void R_RendererMetrics_AddBufferStall( void );
void R_RendererMetrics_EndFrame( int frontEndMsec, int backEndMsec, int viewCount, int visibleEntities, int viewLights, int drawElements, int surfaces, int vertexes, int indexes );
void R_RendererMetrics_BeginGpuBackendFrame( void );
void R_RendererMetrics_EndGpuBackendFrame( void );
void R_RendererMetrics_BeginGpuTimer( rendererGpuTimerSlot_t slot );
void R_RendererMetrics_EndGpuTimer( void );
void R_RendererMetrics_ShutdownGpuTimers( void );
bool R_RendererMetrics_GpuTimersAvailable( void );
bool RendererGpuTimer_RunSelfTest( void );

#endif /* !__RENDERER_METRICS_H__ */
