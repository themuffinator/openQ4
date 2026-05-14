// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_METRICS_H__
#define __RENDERER_METRICS_H__

#include "GLStateCache.h"
#include "RenderGraphResources.h"
#include "MaterialResourceTable.h"
#include "ModernClusteredLighting.h"

enum rendererGpuTimerSlot_t {
	RENDERER_GPU_TIMER_SET_BUFFER = 0,
	RENDERER_GPU_TIMER_DRAW3D,
	RENDERER_GPU_TIMER_DRAW2D,
	RENDERER_GPU_TIMER_SPECIAL_EFFECTS,
	RENDERER_GPU_TIMER_RENDER_TARGET,
	RENDERER_GPU_TIMER_COPY_RENDER,
	RENDERER_GPU_TIMER_SWAP_BUFFERS,
	RENDERER_GPU_TIMER_MODERN_DEFERRED,
	RENDERER_GPU_TIMER_MODERN_FORWARD,
	RENDERER_GPU_TIMER_MODERN_COMPOSITE,
	RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT,
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
void R_RendererMetrics_AddVisibilityMsec( int msec );
void R_RendererMetrics_AddPacketBuildMsec( int msec );
void R_RendererMetrics_AddGraphBuildMsec( int msec );
void R_RendererMetrics_AddPresentMsec( int msec );
void R_RendererMetrics_RecordBackendCommands( int draw3d, int draw2d, int setBuffers, int swapBuffers, int copyRenders, int specialEffects, int renderTargetOps );
void R_RendererMetrics_RecordScenePackets( const scenePacketFrameStats_t &stats );
void R_RendererMetrics_RecordRenderGraph( int graphPasses, int passPackets, int scenePackets, int drawPackets, int commandPackets, int resources, int importedResources, int transientResources, int aliasableTransientResources, int resourceAccesses, int readAccesses, int writeAccesses, int clearOps, int resolveOps, int invalidateOps, int presentOps, bool overflow );
void R_RendererMetrics_RecordRenderGraphResources( const renderGraphResourceManagerStats_t &stats );
void R_RendererMetrics_RecordMaterialResourceTable( const materialResourceTableStats_t &stats );
void R_RendererMetrics_RecordModernExecutor( rendererModernExecutorMetricsMode_t mode, int graphPasses, int preparedPasses, int fallbackPasses, int preparedDrawPackets, int materialDrawPackets, int resourceDrawPackets, int geometryDrawPackets, bool vaoReady, bool frameUBOReady, bool shaderLibraryReady, int shaderProgramCount, int shaderFailureCount, bool drawPlanReady, bool drawPlanOverflow, int drawPlanDraws, int drawPlanDepthDraws, int drawPlanMaterialDraws, int drawPlanFallbackDraws, int drawPlanStateBatches, int drawPlanProgramSwitches, int drawPlanMaterialSwitches, bool submitPlanReady, bool submitPlanOverflow, int submitPlanDraws, int submitPlanFallbackDraws, int submitPlanDepthDraws, int submitPlanMaterialDraws, int submitPlanMissingAmbientDraws, int submitPlanMissingIndexDraws, int submitPlanIndexUploadDraws, bool submitExecuted, int submittedDraws, int submittedFallbackDraws, int submittedIndexUploadDraws, int submitPlanProgramBatches, int submitPlanVertexBufferBatches, int submitPlanIndexBufferBatches, int submitPlanScissorBatches, int submitPlanMaterialBatches, int submitPlanUniformUpdates, int submitPlanFrameUBOBinds, bool visibleDepthRequested, bool visibleDepthExecuted, bool visibleDepthResourceReady, bool visibleShadowResourceReady, bool visibleDepthDebugOverlayReady, int visibleDepthDraws, int visibleDepthAlphaTestDraws, int visibleDepthSkinnedDraws, int visibleDepthFallbackDraws, int visibleShadowDepthDraws, int visibleShadowFallbackDraws, int visibleStencilShadowFallbackDraws, int visibleDepthMismatchDraws, int visibleDepthDebugOverlayDraws, bool opaqueGBufferRequested, bool opaqueGBufferExecuted, bool opaqueGBufferResourcesReady, bool opaqueGBufferMRTReady, bool opaqueGBufferDebugOverlayReady, int opaqueGBufferDraws, int opaqueGBufferFallbackDraws, int opaqueGBufferAttachmentCount, int opaqueGBufferBytesPerPixel, int opaqueGBufferBandwidthKB, int opaqueGBufferDebugOverlayDraws );
void R_RendererMetrics_RecordDeferredResolve( bool requested, bool executed, bool resourcesReady, bool outputReady, bool programReady, bool clusterReady, bool debugOverlayReady, int resolvedPixels, int activeLights, int pointLights, int projectedLights, int lightGridContributions, int clusterReads, int resourceFallbacks, int unsupportedLightFallbacks, int fogFallbackLights, int specialFallbackLights, int overflowClusters, int clearOps, int debugMode, int debugOverlayDraws );
void R_RendererMetrics_RecordForwardPlus( bool requested, bool executed, bool resourcesReady, bool sceneColorReady, bool sceneDepthReady, bool programReady, bool clusterReady, int draws, int opaqueDraws, int alphaTestDraws, int transparentDraws, int viewModelDraws, int fogBlendDraws, int sortedBatches, int fallbackDraws, int resourceFallbackDraws, int materialFallbackDraws, int geometryFallbackDraws, int textureFallbackDraws, int unsupportedBlendFallbackDraws, int specialEffectFallbacks, int sortFallbackDraws, int overdrawEstimate, int clusterReads, int activeLights, int pointLights, int projectedLights, int lightGridContributions, int clearOps );
void R_RendererMetrics_RecordModernVisible( bool requested, bool executed, bool resourcesReady, bool programReady, bool sourceReady, bool backBufferReady, bool blockedByLegacy, int compositions, int pixels, int modernPasses, int legacyPasses, int disabledPasses, int fallbackPasses, int ownerFallbacks, int resourceFallbacks, int guiLegacyPasses, int postLegacyPasses, int specialLegacyPasses, int subviewLegacyPasses, int presentPasses, int clearOps );
void R_RendererMetrics_RecordGpuDriven( bool requested, bool executed, bool resourcesReady, bool validationRequested, bool validationReadbackReady, bool indirectExecuted, bool multiDrawReady, int sourceCommands, int eligibleCommands, int generatedCommands, int culledCommands, int visibleInstances, int cpuGeneratedCommands, int cpuCulledCommands, int cpuVisibleInstances, int gpuGeneratedCommands, int gpuCulledCommands, int gpuVisibleInstances, int cpuClusterBins, int gpuClusterBins, int validationReadbacks, int validationMismatches, int indirectDrawCalls, int multiDrawBatches, int indirectFallbacks, int computeDispatches );
void R_RendererMetrics_RecordLowOverhead( bool requested, bool ready, bool usesDSA, bool usesMultiBind, bool bindlessRequested, bool bindlessAvailable, bool samplerReady, int dsaUpdates, int framebufferDSAUpdates, int samplerDSACreations, int samplerDSAUpdates, int bufferMultiBindBatches, int textureMultiBindBatches, int samplerMultiBindBatches, int classicTextureBinds, int compactedBatches );
void R_RendererMetrics_RecordClusteredLighting( const rendererClusteredLightingStats_t &stats );
void R_RendererMetrics_RecordGLStateCache( const glStateCacheStats_t &stats );
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
