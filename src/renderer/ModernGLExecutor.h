// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_EXECUTOR_H__
#define __MODERN_GL_EXECUTOR_H__

#include "RendererCaps.h"
#include "RenderGraph.h"

typedef struct modernGLExecutorStats_s {
	bool	available;
	bool	enabled;
	bool	initialized;
	bool	vaoReady;
	bool	frameUBOReady;
	bool	shaderLibraryReady;
	bool	gpuDrivenReady;
	bool	lowOverheadReady;
	bool	sceneSSBOReady;
	bool	indirectBufferReady;
	bool	validationSSBOReady;
	bool	computeValidationReady;
	bool	tierUsesDSA;
	bool	tierUsesMultiBind;
	bool	legacyFallback;
	bool	drawPlanReady;
	bool	drawPlanOverflow;
	bool	submitPlanReady;
	bool	submitPlanOverflow;
	int		graphPasses;
	int		preparedPasses;
	int		fallbackPasses;
	int		drawPackets;
	int		preparedDrawPackets;
	int		materialDrawPackets;
	int		resourceDrawPackets;
	int		geometryDrawPackets;
	int		guiDrawPackets;
	int		worldDrawPackets;
	int		shaderProgramCount;
	int		shaderFailureCount;
	int		highestGLSLVersion;
	int		gpuDrivenSceneRecords;
	int		gpuDrivenIndirectRecords;
	int		gpuDrivenSceneBytes;
	int		gpuDrivenIndirectBytes;
	int		gpuDrivenValidationBytes;
	int		gpuDrivenComputeDispatches;
	int		lowOverheadDSAUpdates;
	int		lowOverheadMultiBindBatches;
	int		drawPlanDraws;
	int		drawPlanDepthDraws;
	int		drawPlanMaterialDraws;
	int		drawPlanFallbackDraws;
	int		drawPlanIndexedDraws;
	int		drawPlanVertexOnlyDraws;
	int		drawPlanStateBatches;
	int		drawPlanProgramSwitches;
	int		drawPlanMaterialSwitches;
	int		submitPlanDraws;
	int		submitPlanFallbackDraws;
	int		submitPlanDepthDraws;
	int		submitPlanMaterialDraws;
	int		submitPlanMissingAmbientDraws;
	int		submitPlanMissingIndexDraws;
	int		submitPlanIndexUploadDraws;
	int		submitPlanProgramBatches;
	int		submitPlanVertexBufferBatches;
	int		submitPlanIndexBufferBatches;
	int		submitPlanScissorBatches;
	int		submitPlanMaterialBatches;
	int		submitPlanUniformUpdates;
	int		submitPlanFrameUBOBinds;
	bool	submitExecuted;
	int		submittedDraws;
	int		submittedDepthDraws;
	int		submittedMaterialDraws;
	int		submittedIndexUploadDraws;
	int		submittedFallbackDraws;
	char	status[96];
} modernGLExecutorStats_t;

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernGLExecutor_Shutdown( void );
void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph );
const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void );
void R_ModernGLExecutor_PrintGfxInfo( void );
bool RendererModernGLExecutor_RunSelfTest( void );

#endif /* !__MODERN_GL_EXECUTOR_H__ */
