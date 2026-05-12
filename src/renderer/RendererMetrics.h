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

void R_RendererMetrics_BeginFrame( int frameCount );
void R_RendererMetrics_RecordSubmitMsec( int submitMsec );
void R_RendererMetrics_RecordBackendCommands( int draw3d, int draw2d, int setBuffers, int swapBuffers, int copyRenders, int specialEffects, int renderTargetOps );
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
