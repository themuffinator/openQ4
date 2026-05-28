// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_DRAW_PLAN_H__
#define __MODERN_GL_DRAW_PLAN_H__

#include "RenderGraph.h"
#include "ModernGLShaderLibrary.h"
#include "MaterialResourceTable.h"

const int MODERN_GL_DRAW_PLAN_MAX_ENTRIES = SCENE_PACKET_MAX_DRAWS;

enum modernGLDrawPlanPipeline_t {
	MODERN_GL_DRAW_PLAN_PIPELINE_NONE = 0,
	MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH,
	MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH,
	MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL,
	MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER,
	MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID,
	MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND,
	MODERN_GL_DRAW_PLAN_PIPELINE_GUI,
	MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE,
	MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST,
	MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT,
	MODERN_GL_DRAW_PLAN_PIPELINE_COUNT
};

typedef struct modernGLDrawPlanEntry_s {
	const drawPacket_t			*drawPacket;
	renderPassCategory_t		passCategory;
	modernGLDrawPlanPipeline_t	pipeline;
	modernGLShaderProgramKind_t	shaderKind;
	unsigned int				program;
	rendererPermutationKey_t		permutation;
	int							modelViewProjectionLocation;
	int							modelViewMatrixLocation;
	int							debugColorLocation;
	int							localParamsLocation;
	int							mainTextureLocation;
	int							normalTextureLocation;
	int							specularTextureLocation;
	int							emissiveTextureLocation;
	int							textureIndicesLocation;
	int							textureTableModeLocation;
	int							materialFlagsLocation;
	int							materialEnhancementLocation;
	int							drawRecordModeLocation;
	int							drawPacketIndex;
	int							materialRecordIndex;
	int							materialTableIndex;
	int							geometryRecordIndex;
	int							instanceRecordIndex;
	unsigned int				materialStableId;
	materialResourceFallbackReason_t materialFallbackReason;
	geometryResourceFallbackReason_t geometryFallbackReason;
	int							glslVersion;
	int							indexCount;
	int							vertexCount;
	bool						indexed;
} modernGLDrawPlanEntry_t;

typedef struct modernGLDrawPlanStats_s {
	bool	available;
	bool	valid;
	bool	overflow;
	int		sourceDrawPackets;
	int		plannedDraws;
	int		depthDraws;
	int		materialDraws;
	int		fallbackDraws;
	int		missingMaterialTableDraws;
	int		missingGeometryRecordDraws;
	int		missingInstanceRecordDraws;
	int		materialFallbackDraws;
	int		geometryFallbackDraws;
	int		geometryDeformFallbackDraws;
	int		geometrySkinnedFallbackDraws;
	int		geometryVertexBufferFallbackDraws;
	int		geometryIndexFallbackDraws;
	int		indexedDraws;
	int		vertexOnlyDraws;
	int		pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_COUNT];
	int		pipelineBatches;
	int		geometryBatches;
	int		textureSetBatches;
	int		scissorBatches;
	int		transparentSortBreaks;
	int		stateBatches;
	int		programSwitches;
	int		materialSwitches;
	int		highestGLSLVersion;
	char	status[96];
} modernGLDrawPlanStats_t;

class idModernGLDrawPlan {
public:
	idModernGLDrawPlan();
	void Clear( void );
	bool Build( const idScenePacketFrame &packetFrame, const idRenderGraph &graph );

	int NumEntries( void ) const;
	const modernGLDrawPlanEntry_t &Entry( int index ) const;
	const modernGLDrawPlanStats_t &Stats( void ) const;

private:
	bool AddEntry( const drawPacket_t &draw, int drawPacketIndex, const materialResourceTableRecord_t &materialRecord, modernGLDrawPlanPipeline_t pipeline, const modernGLShaderProgramInfo_t &program );

	modernGLDrawPlanEntry_t entries[MODERN_GL_DRAW_PLAN_MAX_ENTRIES];
	modernGLDrawPlanStats_t stats;
	int numEntries;
};

const char *ModernGLDrawPlanPipeline_Name( modernGLDrawPlanPipeline_t pipeline );
bool RendererModernGLDrawPlan_RunSelfTest( void );

#endif /* !__MODERN_GL_DRAW_PLAN_H__ */
