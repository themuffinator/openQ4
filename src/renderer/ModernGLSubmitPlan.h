// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_SUBMIT_PLAN_H__
#define __MODERN_GL_SUBMIT_PLAN_H__

#include "ModernGLDrawPlan.h"

const int MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS = MODERN_GL_DRAW_PLAN_MAX_ENTRIES;

typedef struct modernGLSubmitCommand_s {
	const modernGLDrawPlanEntry_t *drawPlanEntry;
	renderPassCategory_t		passCategory;
	modernGLDrawPlanPipeline_t	pipeline;
	modernGLShaderProgramKind_t	shaderKind;
	unsigned int				program;
	unsigned int				vertexBuffer;
	unsigned int				indexBuffer;
	const void					*clientIndexData;
	int							ambientCacheOffset;
	int							indexCacheOffset;
	int							clientIndexBytes;
	int							modelViewProjectionLocation;
	int							debugColorLocation;
	int							localParamsLocation;
	int							mainTextureLocation;
	int							vertexStride;
	int							indexType;
	int							indexCount;
	int							vertexCount;
	int							materialRecordIndex;
	int							scissorX1;
	int							scissorY1;
	int							scissorX2;
	int							scissorY2;
	bool						indexed;
	bool						uploadIndexBuffer;
} modernGLSubmitCommand_t;

typedef struct modernGLSubmitPlanStats_s {
	bool	available;
	bool	valid;
	bool	overflow;
	int		sourcePlanDraws;
	int		readyDraws;
	int		fallbackDraws;
	int		depthReadyDraws;
	int		materialReadyDraws;
	int		indexedReadyDraws;
	int		vertexOnlyReadyDraws;
	int		missingDrawPacketDraws;
	int		missingGeometryDraws;
	int		missingAmbientCacheDraws;
	int		missingIndexCacheDraws;
	int		clientVertexFallbackDraws;
	int		indexCacheReadyDraws;
	int		indexUploadDraws;
	int		programBatches;
	int		vertexBufferBatches;
	int		indexBufferBatches;
	int		scissorBatches;
	int		materialBatches;
	int		uniformUpdates;
	int		frameUBOBinds;
	int		highestGLSLVersion;
	char	status[96];
} modernGLSubmitPlanStats_t;

class idModernGLSubmitPlan {
public:
	idModernGLSubmitPlan();
	void Clear( void );
	bool Build( const idModernGLDrawPlan &drawPlan );

	int NumCommands( void ) const;
	const modernGLSubmitCommand_t &Command( int index ) const;
	const modernGLSubmitPlanStats_t &Stats( void ) const;

private:
	bool AddCommand( const modernGLDrawPlanEntry_t &entry );

	modernGLSubmitCommand_t commands[MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS];
	modernGLSubmitPlanStats_t stats;
	int numCommands;
};

bool RendererModernGLSubmitPlan_RunSelfTest( void );

#endif /* !__MODERN_GL_SUBMIT_PLAN_H__ */
