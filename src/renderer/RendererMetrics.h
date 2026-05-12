// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_METRICS_H__
#define __RENDERER_METRICS_H__

void R_RendererMetrics_BeginFrame( int frameCount );
void R_RendererMetrics_RecordSubmitMsec( int submitMsec );
void R_RendererMetrics_RecordBackendCommands( int draw3d, int draw2d, int setBuffers, int swapBuffers, int copyRenders, int specialEffects, int renderTargetOps );
void R_RendererMetrics_AddUploadBytes( int bytes );
void R_RendererMetrics_AddBufferStall( void );
void R_RendererMetrics_EndFrame( int frontEndMsec, int backEndMsec, int viewCount, int visibleEntities, int viewLights, int drawElements, int surfaces, int vertexes, int indexes );

#endif /* !__RENDERER_METRICS_H__ */
