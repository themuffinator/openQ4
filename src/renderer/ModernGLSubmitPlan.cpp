// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLSubmitPlan.h"

idModernGLSubmitPlan::idModernGLSubmitPlan()
	: numCommands( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idModernGLSubmitPlan::Clear( void ) {
	memset( commands, 0, sizeof( commands ) );
	memset( &stats, 0, sizeof( stats ) );
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", "empty" );
	numCommands = 0;
}

static void R_ModernGLSubmitPlan_SetStatus( modernGLSubmitPlanStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
}

static bool R_ModernGLSubmitPlan_ScissorEquals( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	return a.scissorX1 == b.scissorX1
		&& a.scissorY1 == b.scissorY1
		&& a.scissorX2 == b.scissorX2
		&& a.scissorY2 == b.scissorY2;
}

static bool R_ModernGLSubmitPlan_EntryNeedsIndexBuffer( const modernGLDrawPlanEntry_t &entry ) {
	return entry.indexed && entry.indexCount > 0;
}

static bool R_ModernGLSubmitPlan_IsDepthPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH || pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH;
}

static bool R_ModernGLSubmitPlan_IsMaterialPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static bool R_ModernGLSubmitPlan_RunFrameTempIndexCacheSelfTest( void ) {
	static glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 1 };
	vertCache_t *indexBlock = vertexCache.AllocFrameTemp( indexes, sizeof( indexes ), true );
	if ( indexBlock == NULL || indexBlock->tag != TAG_TEMP || !indexBlock->indexBuffer || indexBlock->size != static_cast<int>( sizeof( indexes ) ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: frame-temp index cache tagging mismatch\n" );
		return false;
	}

	idDrawVert verts[3];
	memset( verts, 0, sizeof( verts ) );
	vertCache_t *vertexBlock = vertexCache.AllocFrameTemp( verts, sizeof( verts ) );
	if ( vertexBlock == NULL || vertexBlock->tag != TAG_TEMP || vertexBlock->indexBuffer || vertexBlock->size != static_cast<int>( sizeof( verts ) ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: frame-temp vertex cache tagging mismatch\n" );
		return false;
	}

	return true;
}

bool idModernGLSubmitPlan::AddCommand( const modernGLDrawPlanEntry_t &entry ) {
	if ( numCommands >= MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS ) {
		stats.overflow = true;
		stats.fallbackDraws++;
		return false;
	}

	const drawPacket_t *draw = entry.drawPacket;
	if ( draw == NULL ) {
		stats.missingDrawPacketDraws++;
		stats.fallbackDraws++;
		return false;
	}

	const geometryResourceRecord_t *geo = draw->geometryRecord;
	const instanceRecord_t *instance = draw->instanceRecord;
	if ( geo == NULL || !draw->hasGeometry ) {
		stats.missingGeometryDraws++;
		stats.fallbackDraws++;
		return false;
	}
	if ( instance == NULL || !instance->hasModelMatrix ) {
		stats.missingDrawPacketDraws++;
		stats.fallbackDraws++;
		return false;
	}

	bool submitReady = true;
	const bool needsIndexBuffer = R_ModernGLSubmitPlan_EntryNeedsIndexBuffer( entry );
	const bool hasIndexBuffer = needsIndexBuffer && geo->hasIndexBuffer && geo->indexBuffer != 0;
	const bool canUploadIndexes = needsIndexBuffer && !hasIndexBuffer && geo->legacyIndexData != NULL && entry.indexCount > 0;
	if ( !geo->hasAmbientVertexBuffer || geo->ambientVertexBuffer == 0 ) {
		stats.missingAmbientCacheDraws++;
		if ( geo->ambientVertexBuffer == 0 ) {
			stats.clientVertexFallbackDraws++;
		}
		submitReady = false;
	}
	if ( needsIndexBuffer && !hasIndexBuffer && !canUploadIndexes ) {
		stats.missingIndexCacheDraws++;
		if ( geo->indexBuffer == 0 ) {
			stats.clientVertexFallbackDraws++;
		}
		submitReady = false;
	}
	if ( !submitReady ) {
		stats.fallbackDraws++;
		return false;
	}

	modernGLSubmitCommand_t &command = commands[numCommands];
	memset( &command, 0, sizeof( command ) );
	command.drawPlanEntry = &entry;
	command.viewDef = draw->viewDef;
	command.passCategory = entry.passCategory;
	command.pipeline = entry.pipeline;
	command.shaderKind = entry.shaderKind;
	command.program = entry.program;
	command.vertexBuffer = geo->ambientVertexBuffer;
	command.indexBuffer = hasIndexBuffer ? geo->indexBuffer : 0;
	command.clientIndexData = canUploadIndexes ? geo->legacyIndexData : NULL;
	command.ambientCacheOffset = geo->ambientCacheOffset;
	command.indexCacheOffset = hasIndexBuffer ? geo->indexCacheOffset : 0;
	command.clientIndexBytes = canUploadIndexes ? entry.indexCount * static_cast<int>( sizeof( glIndex_t ) ) : 0;
	command.modelViewProjectionLocation = entry.modelViewProjectionLocation;
	command.debugColorLocation = entry.debugColorLocation;
	command.localParamsLocation = entry.localParamsLocation;
	command.mainTextureLocation = entry.mainTextureLocation;
	command.vertexStride = geo->vertexStride;
	command.indexType = geo->indexType;
	command.indexCount = entry.indexCount;
	command.vertexCount = entry.vertexCount;
	command.materialRecordIndex = entry.materialRecordIndex;
	command.materialTableIndex = entry.materialTableIndex;
	command.geometryRecordIndex = entry.geometryRecordIndex;
	command.instanceRecordIndex = entry.instanceRecordIndex;
	command.materialStableId = entry.materialStableId;
	memcpy( command.modelViewMatrix, instance->modelViewMatrix, sizeof( command.modelViewMatrix ) );
	command.scissorX1 = draw->scissorX1;
	command.scissorY1 = draw->scissorY1;
	command.scissorX2 = draw->scissorX2;
	command.scissorY2 = draw->scissorY2;
	command.indexed = entry.indexed;
	command.uploadIndexBuffer = canUploadIndexes;

	const bool havePrevious = numCommands > 0;
	if ( !havePrevious || commands[numCommands - 1].program != command.program ) {
		stats.programBatches++;
	}
	if ( !havePrevious || commands[numCommands - 1].vertexBuffer != command.vertexBuffer ) {
		stats.vertexBufferBatches++;
	}
	if ( command.indexed && !command.uploadIndexBuffer && ( !havePrevious || commands[numCommands - 1].indexBuffer != command.indexBuffer ) ) {
		stats.indexBufferBatches++;
	}
	if ( !havePrevious || !R_ModernGLSubmitPlan_ScissorEquals( commands[numCommands - 1], command ) ) {
		stats.scissorBatches++;
	}
	if ( !havePrevious || commands[numCommands - 1].materialTableIndex != command.materialTableIndex ) {
		stats.materialBatches++;
	}

	numCommands++;
	stats.readyDraws++;
	stats.uniformUpdates++;
	stats.frameUBOBinds = 1;
	if ( R_ModernGLSubmitPlan_IsDepthPipeline( command.pipeline ) ) {
		stats.depthReadyDraws++;
	} else if ( R_ModernGLSubmitPlan_IsMaterialPipeline( command.pipeline ) ) {
		stats.materialReadyDraws++;
	}
	if ( command.indexed ) {
		stats.indexedReadyDraws++;
		if ( command.uploadIndexBuffer ) {
			stats.indexUploadDraws++;
		} else {
			stats.indexCacheReadyDraws++;
		}
	} else {
		stats.vertexOnlyReadyDraws++;
	}
	if ( entry.glslVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = entry.glslVersion;
	}
	return true;
}

bool idModernGLSubmitPlan::Build( const idModernGLDrawPlan &drawPlan ) {
	Clear();
	const modernGLDrawPlanStats_t &drawStats = drawPlan.Stats();
	stats.sourcePlanDraws = drawPlan.NumEntries();

	if ( !drawStats.available || !drawStats.valid ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "draw-plan-unavailable" );
		stats.fallbackDraws = drawStats.plannedDraws;
		return false;
	}

	stats.available = true;
	for ( int i = 0; i < drawPlan.NumEntries(); ++i ) {
		AddCommand( drawPlan.Entry( i ) );
	}

	stats.valid = stats.available && !stats.overflow;
	if ( stats.overflow ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "overflow" );
	} else if ( stats.readyDraws == 0 && stats.sourcePlanDraws > 0 ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "all-fallback" );
	} else if ( stats.fallbackDraws > 0 ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "ready-with-fallbacks" );
	} else {
		R_ModernGLSubmitPlan_SetStatus( stats, "ready" );
	}
	return stats.valid;
}

int idModernGLSubmitPlan::NumCommands( void ) const {
	return numCommands;
}

const modernGLSubmitCommand_t &idModernGLSubmitPlan::Command( int index ) const {
	return commands[index];
}

const modernGLSubmitPlanStats_t &idModernGLSubmitPlan::Stats( void ) const {
	return stats;
}

static void R_ModernGLSubmitPlan_InitCache( vertCache_t &cache, unsigned int vbo, int offset, int size, bool indexBuffer, int tag = TAG_USED ) {
	memset( &cache, 0, sizeof( cache ) );
	cache.vbo = vbo;
	cache.offset = offset;
	cache.size = size;
	cache.indexBuffer = indexBuffer;
	cache.tag = tag;
}

static bool R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( bool ambientCacheReady, bool indexCacheReady, int indexCacheTag, bool cpuIndexesReady, idModernGLDrawPlan &drawPlan, idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	static vertCache_t ambientCache;
	static vertCache_t indexCache;
	static srfTriangles_t geometry;
	static drawSurf_t drawSurfs[2];
	static glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 1 };

	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	if ( ambientCacheReady ) {
		R_ModernGLSubmitPlan_InitCache( ambientCache, 101, 64, geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) ), false );
		geometry.ambientCache = &ambientCache;
	}
	if ( indexCacheReady ) {
		R_ModernGLSubmitPlan_InitCache( indexCache, 202, 128, geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) ), true, indexCacheTag );
		geometry.indexCache = &indexCache;
	}
	if ( cpuIndexesReady ) {
		geometry.indexes = indexes;
	}

	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	return drawPlan.Build( packetFrame, graph );
}

bool RendererModernGLSubmitPlan_RunSelfTest( void ) {
	if ( !R_ModernGLSubmitPlan_RunFrameTempIndexCacheSelfTest() ) {
		return false;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernGLSubmitPlan self-test passed (shader library unavailable)\n" );
		return true;
	}

	idScenePacketFrame packetFrame;
	idRenderGraph graph;
	idModernGLDrawPlan drawPlan;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, true, TAG_USED, false, drawPlan, packetFrame, graph );

	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	const modernGLSubmitPlanStats_t &readyStats = submitPlan.Stats();
	const int expectedReadyDraws = tr.defaultMaterial != NULL ? 10 : 0;
	if ( readyStats.sourcePlanDraws != expectedReadyDraws || readyStats.readyDraws != expectedReadyDraws || readyStats.fallbackDraws != 0 || readyStats.overflow ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: ready draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 ) {
		if ( readyStats.depthReadyDraws != 2
			|| readyStats.materialReadyDraws != 8
			|| readyStats.programBatches != 5
			|| readyStats.vertexBufferBatches != 1
			|| readyStats.indexBufferBatches != 1
			|| readyStats.scissorBatches != 1
			|| readyStats.materialBatches != 1
			|| readyStats.uniformUpdates != expectedReadyDraws
			|| readyStats.frameUBOBinds != 1
			|| readyStats.indexCacheReadyDraws != expectedReadyDraws
			|| readyStats.indexUploadDraws != 0
			|| submitPlan.NumCommands() != expectedReadyDraws ) {
			common->Printf( "RendererModernGLSubmitPlan self-test failed: ready state-batch mismatch\n" );
			return false;
		}
		if ( submitPlan.Command( 0 ).uploadIndexBuffer || submitPlan.Command( 0 ).indexBuffer != 202 ) {
			common->Printf( "RendererModernGLSubmitPlan self-test failed: ready index-source mismatch\n" );
			return false;
		}
		if ( submitPlan.Command( 0 ).materialTableIndex != submitPlan.Command( 0 ).materialRecordIndex ) {
			common->Printf( "RendererModernGLSubmitPlan self-test failed: material table index mismatch\n" );
			return false;
		}
	}

	idModernGLDrawPlan missingCacheDrawPlan;
	idScenePacketFrame missingCachePacketFrame;
	idRenderGraph missingCacheGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( false, false, TAG_FREE, false, missingCacheDrawPlan, missingCachePacketFrame, missingCacheGraph );
	idModernGLSubmitPlan missingCacheSubmitPlan;
	missingCacheSubmitPlan.Build( missingCacheDrawPlan );
	const modernGLSubmitPlanStats_t &fallbackStats = missingCacheSubmitPlan.Stats();
	if ( fallbackStats.sourcePlanDraws != expectedReadyDraws || fallbackStats.readyDraws != 0 || fallbackStats.fallbackDraws != expectedReadyDraws ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: fallback draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 && ( fallbackStats.missingAmbientCacheDraws != expectedReadyDraws || fallbackStats.missingIndexCacheDraws != expectedReadyDraws ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: missing-cache reason mismatch\n" );
		return false;
	}

	idModernGLDrawPlan tempIndexDrawPlan;
	idScenePacketFrame tempIndexPacketFrame;
	idRenderGraph tempIndexGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, true, TAG_TEMP, false, tempIndexDrawPlan, tempIndexPacketFrame, tempIndexGraph );
	idModernGLSubmitPlan tempIndexSubmitPlan;
	tempIndexSubmitPlan.Build( tempIndexDrawPlan );
	const modernGLSubmitPlanStats_t &tempIndexStats = tempIndexSubmitPlan.Stats();
	if ( tempIndexStats.sourcePlanDraws != expectedReadyDraws || tempIndexStats.readyDraws != expectedReadyDraws || tempIndexStats.fallbackDraws != 0 || tempIndexStats.indexCacheReadyDraws != expectedReadyDraws || tempIndexStats.indexUploadDraws != 0 ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: temp index-cache draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 && ( tempIndexSubmitPlan.Command( 0 ).uploadIndexBuffer || tempIndexSubmitPlan.Command( 0 ).indexBuffer != 202 ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: temp index-cache source mismatch\n" );
		return false;
	}

	idModernGLDrawPlan uploadIndexDrawPlan;
	idScenePacketFrame uploadIndexPacketFrame;
	idRenderGraph uploadIndexGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, false, TAG_FREE, true, uploadIndexDrawPlan, uploadIndexPacketFrame, uploadIndexGraph );
	idModernGLSubmitPlan uploadIndexSubmitPlan;
	uploadIndexSubmitPlan.Build( uploadIndexDrawPlan );
	const modernGLSubmitPlanStats_t &uploadStats = uploadIndexSubmitPlan.Stats();
	if ( uploadStats.sourcePlanDraws != expectedReadyDraws || uploadStats.readyDraws != expectedReadyDraws || uploadStats.fallbackDraws != 0 || uploadStats.indexUploadDraws != expectedReadyDraws || uploadStats.indexCacheReadyDraws != 0 ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: index-upload draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 && ( !uploadIndexSubmitPlan.Command( 0 ).uploadIndexBuffer || uploadIndexSubmitPlan.Command( 0 ).indexBuffer != 0 || uploadIndexSubmitPlan.Command( 0 ).clientIndexData == NULL ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: index-upload source mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererModernGLSubmitPlan self-test passed (ready=%d fallback=%d tempIndex=%d uploadIndex=%d programBatches=%d vertexBatches=%d indexBatches=%d)\n",
		readyStats.readyDraws,
		fallbackStats.fallbackDraws,
		tempIndexStats.indexCacheReadyDraws,
		uploadStats.indexUploadDraws,
		readyStats.programBatches,
		readyStats.vertexBufferBatches,
		readyStats.indexBufferBatches );
	return true;
}
