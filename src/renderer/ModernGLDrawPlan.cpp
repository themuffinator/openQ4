// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLDrawPlan.h"
#include "RendererBootstrap.h"

idModernGLDrawPlan::idModernGLDrawPlan()
	: numEntries( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idModernGLDrawPlan::Clear( void ) {
	memset( entries, 0, sizeof( entries ) );
	memset( &stats, 0, sizeof( stats ) );
	idStr::Copynz( stats.status, "empty", sizeof( stats.status ) );
	numEntries = 0;
}

const char *ModernGLDrawPlanPipeline_Name( modernGLDrawPlanPipeline_t pipeline ) {
	switch ( pipeline ) {
	case MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH:
		return "depth";
	case MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH:
		return "shadowDepth";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL:
		return "flatMaterial";
	case MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER:
		return "gbuffer";
	case MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID:
		return "lightGrid";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND:
		return "fogBlend";
	case MODERN_GL_DRAW_PLAN_PIPELINE_GUI:
		return "gui";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE:
		return "forwardPlusOpaque";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST:
		return "forwardPlusAlphaTest";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT:
		return "forwardPlusTransparent";
	case MODERN_GL_DRAW_PLAN_PIPELINE_COUNT:
	case MODERN_GL_DRAW_PLAN_PIPELINE_NONE:
	default:
		return "none";
	}
}

static void R_ModernGLDrawPlan_SetStatus( modernGLDrawPlanStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status ? status : "unknown", sizeof( stats.status ) );
}

static bool R_ModernGLDrawPlan_CategoryPipeline( renderPassCategory_t category, modernGLDrawPlanPipeline_t &pipeline, modernGLShaderProgramKind_t &shaderKind ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH;
		shaderKind = MODERN_GL_SHADER_DEPTH;
		return true;
	case RENDER_PASS_STENCIL_SHADOW:
	case RENDER_PASS_SHADOW_MAP:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH;
		shaderKind = MODERN_GL_SHADER_SHADOW_DEPTH;
		return true;
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_AMBIENT:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL;
		shaderKind = MODERN_GL_SHADER_FLAT_MATERIAL;
		return true;
	case RENDER_PASS_LIGHT_GRID:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID;
		shaderKind = MODERN_GL_SHADER_LIGHT_GRID;
		return true;
	case RENDER_PASS_FOG_BLEND:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND;
		shaderKind = MODERN_GL_SHADER_FOG_BLEND;
		return true;
	case RENDER_PASS_GUI:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_GUI;
		shaderKind = MODERN_GL_SHADER_GUI;
		return true;
	default:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_NONE;
		shaderKind = MODERN_GL_SHADER_DEPTH;
		return false;
	}
}

static bool R_ModernGLDrawPlan_HasGraphPass( const idRenderGraph &graph, renderPassCategory_t category ) {
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		if ( pass.category == category && pass.enabled && pass.packetBacked ) {
			return true;
		}
	}
	return false;
}

static bool R_ModernGLDrawPlan_IsDepthPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH || pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH;
}

static bool R_ModernGLDrawPlan_IsMaterialPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GUI
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static geometryResourceFallbackReason_t R_ModernGLDrawPlan_GeometryFallbackReason( const drawPacket_t &draw ) {
	const geometryResourceRecord_t *geometry = draw.geometryRecord;
	if ( geometry == NULL || draw.geometryRecordIndex < 0 ) {
		return GEOMETRY_RESOURCE_FALLBACK_MISSING_GEOMETRY;
	}
	if ( ( geometry->fallbackFlags & GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_DEFORM ) != 0 ) {
		return GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM;
	}
	if ( ( geometry->fallbackFlags & GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_GPU_SKINNING ) != 0 ) {
		return GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING;
	}
	if ( ( geometry->fallbackFlags & GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_VERTEX_BUFFER ) != 0 ) {
		return GEOMETRY_RESOURCE_FALLBACK_MISSING_VERTEX_BUFFER;
	}
	if ( ( geometry->fallbackFlags & GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_INDEX_DATA ) != 0 ) {
		return GEOMETRY_RESOURCE_FALLBACK_MISSING_INDEX_DATA;
	}
	return GEOMETRY_RESOURCE_FALLBACK_NONE;
}

static bool R_ModernGLDrawPlan_ModernVisibleRequested( void ) {
	return r_rendererModernVisible.GetBool() || RendererBootstrap_ShouldAutoPromoteModernVisible();
}

static void R_ModernGLDrawPlan_CountGeometryFallback( modernGLDrawPlanStats_t &stats, geometryResourceFallbackReason_t reason ) {
	stats.fallbackDraws++;
	stats.geometryFallbackDraws++;
	switch ( reason ) {
	case GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM:
		stats.geometryDeformFallbackDraws++;
		break;
	case GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING:
		stats.geometrySkinnedFallbackDraws++;
		break;
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_VERTEX_BUFFER:
		stats.geometryVertexBufferFallbackDraws++;
		break;
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_INDEX_DATA:
		stats.geometryIndexFallbackDraws++;
		break;
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_GEOMETRY:
	case GEOMETRY_RESOURCE_FALLBACK_NONE:
	default:
		break;
	}
}

static bool R_ModernGLDrawPlan_ShouldUseGBuffer( const materialResourceTableRecord_t &materialRecord ) {
	if ( !R_ModernGLDrawPlan_ModernVisibleRequested()
		&& !r_rendererModernOpaque.GetBool()
		&& r_rendererModernGBufferDebug.GetInteger() <= 0
		&& !r_rendererModernDeferred.GetBool()
		&& r_rendererModernDeferredDebug.GetInteger() <= 0 ) {
		return false;
	}
	return materialRecord.materialClass == RENDER_MATERIAL_OPAQUE
		|| materialRecord.materialClass == RENDER_MATERIAL_PERFORATED
		|| materialRecord.alphaTest;
}

static bool R_ModernGLDrawPlan_ShouldUseForwardPlus( renderPassCategory_t passCategory, const materialResourceTableRecord_t &materialRecord, modernGLDrawPlanPipeline_t &pipeline, modernGLShaderProgramKind_t &shaderKind ) {
	if ( !R_ModernGLDrawPlan_ModernVisibleRequested() && !r_rendererForwardPlus.GetBool() ) {
		return false;
	}
	if ( materialRecord.materialClass == RENDER_MATERIAL_GUI
		|| materialRecord.materialClass == RENDER_MATERIAL_POST_PROCESS
		|| materialRecord.materialClass == RENDER_MATERIAL_SUBVIEW
		|| materialRecord.materialClass == RENDER_MATERIAL_SHADOW_ONLY ) {
		return false;
	}
	if ( passCategory == RENDER_PASS_ARB2_INTERACTION || passCategory == RENDER_PASS_AMBIENT ) {
		if ( materialRecord.alphaTest || materialRecord.materialClass == RENDER_MATERIAL_PERFORATED ) {
			pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST;
			shaderKind = MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST;
			return true;
		}
		if ( materialRecord.materialClass == RENDER_MATERIAL_OPAQUE ) {
			pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE;
			shaderKind = MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE;
			return true;
		}
	}
	if ( passCategory == RENDER_PASS_FOG_BLEND ) {
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
		shaderKind = MODERN_GL_SHADER_TRANSPARENT_FORWARD;
		return true;
	}
	return false;
}

bool idModernGLDrawPlan::AddEntry( const drawPacket_t &draw, int drawPacketIndex, const materialResourceTableRecord_t &materialRecord, modernGLDrawPlanPipeline_t pipeline, const modernGLShaderProgramInfo_t &program ) {
	if ( numEntries >= MODERN_GL_DRAW_PLAN_MAX_ENTRIES ) {
		stats.overflow = true;
		stats.fallbackDraws++;
		return false;
	}

	modernGLDrawPlanEntry_t &entry = entries[numEntries++];
	memset( &entry, 0, sizeof( entry ) );
	entry.drawPacket = &draw;
	entry.passCategory = draw.passCategory;
	entry.pipeline = pipeline;
	entry.shaderKind = program.kind;
	entry.program = program.program;
	entry.permutation = program.permutation;
	entry.modelViewProjectionLocation = program.modelViewProjectionLocation;
	entry.modelViewMatrixLocation = program.modelViewMatrixLocation;
	entry.debugColorLocation = program.debugColorLocation;
	entry.localParamsLocation = program.localParamsLocation;
	entry.mainTextureLocation = program.mainTextureLocation;
	entry.normalTextureLocation = program.normalTextureLocation;
	entry.specularTextureLocation = program.specularTextureLocation;
	entry.emissiveTextureLocation = program.emissiveTextureLocation;
	entry.textureIndicesLocation = program.textureIndicesLocation;
	entry.textureTableModeLocation = program.textureTableModeLocation;
	entry.materialFlagsLocation = program.materialFlagsLocation;
	entry.materialEnhancementLocation = program.materialEnhancementLocation;
	entry.drawRecordModeLocation = program.drawRecordModeLocation;
	entry.drawPacketIndex = drawPacketIndex;
	entry.materialRecordIndex = draw.materialRecordIndex;
	entry.materialTableIndex = materialRecord.tableIndex;
	entry.geometryRecordIndex = draw.geometryRecordIndex;
	entry.instanceRecordIndex = draw.instanceRecordIndex;
	entry.materialStableId = materialRecord.materialId >= 0 ? static_cast<unsigned int>( materialRecord.materialId ) : 0xffffffffu;
	entry.materialFallbackReason = materialRecord.fallbackReason;
	entry.geometryFallbackReason = draw.geometryRecord != NULL ? draw.geometryRecord->fallbackReason : GEOMETRY_RESOURCE_FALLBACK_MISSING_GEOMETRY;
	entry.glslVersion = program.glslVersion;
	entry.indexCount = draw.indexCount;
	entry.vertexCount = draw.vertexCount;
	entry.indexed = draw.hasIndexCache || draw.indexCount > 0;

	stats.plannedDraws++;
	if ( R_ModernGLDrawPlan_IsDepthPipeline( pipeline ) ) {
		stats.depthDraws++;
	} else if ( R_ModernGLDrawPlan_IsMaterialPipeline( pipeline ) ) {
		stats.materialDraws++;
	}
	if ( entry.indexed ) {
		stats.indexedDraws++;
	} else {
		stats.vertexOnlyDraws++;
	}
	if ( pipeline > MODERN_GL_DRAW_PLAN_PIPELINE_NONE && pipeline < MODERN_GL_DRAW_PLAN_PIPELINE_COUNT ) {
		stats.pipelineDraws[pipeline]++;
	}
	if ( entry.glslVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = entry.glslVersion;
	}
	return true;
}

bool idModernGLDrawPlan::Build( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	Clear();
	stats.sourceDrawPackets = packetFrame.NumDrawPackets();

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		R_ModernGLDrawPlan_SetStatus( stats, "shader-library-unavailable" );
		stats.fallbackDraws = stats.sourceDrawPackets;
		return false;
	}
	const materialResourceTableStats_t &materialStats = R_MaterialResourceTable_Stats();
	if ( !materialStats.prepared || !materialStats.available ) {
		R_ModernGLDrawPlan_SetStatus( stats, "material-table-unavailable" );
		stats.fallbackDraws = stats.sourceDrawPackets;
		stats.missingMaterialTableDraws = stats.sourceDrawPackets;
		return false;
	}

	modernGLDrawPlanPipeline_t previousPipeline = MODERN_GL_DRAW_PLAN_PIPELINE_NONE;
	renderPassCategory_t previousPass = RENDER_PASS_DEPTH;
	int previousMaterial = -2;
	int previousGeometry = -2;
	int previousScissorX1 = 0;
	int previousScissorY1 = 0;
	int previousScissorX2 = 0;
	int previousScissorY2 = 0;
	unsigned int previousProgram = 0;
	unsigned long long previousTransparentSortKey = 0;
	bool haveTransparentSortKey = false;
	bool havePrevious = false;

	stats.available = true;
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		modernGLDrawPlanPipeline_t pipeline;
		modernGLShaderProgramKind_t shaderKind;
		if ( !R_ModernGLDrawPlan_CategoryPipeline( draw.passCategory, pipeline, shaderKind ) ) {
			stats.fallbackDraws++;
			continue;
		}
		if ( !R_ModernGLDrawPlan_HasGraphPass( graph, draw.passCategory ) ) {
			stats.fallbackDraws++;
			continue;
		}
		if ( draw.materialRecordIndex < 0 ) {
			stats.fallbackDraws++;
			continue;
		}
		if ( draw.geometryRecord == NULL || draw.geometryRecordIndex < 0 ) {
			stats.fallbackDraws++;
			stats.missingGeometryRecordDraws++;
			continue;
		}
		const geometryResourceFallbackReason_t geometryFallbackReason = R_ModernGLDrawPlan_GeometryFallbackReason( draw );
		if ( geometryFallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE ) {
			R_ModernGLDrawPlan_CountGeometryFallback( stats, geometryFallbackReason );
			continue;
		}
		if ( draw.instanceRecord == NULL || draw.instanceRecordIndex < 0 ) {
			stats.fallbackDraws++;
			stats.missingInstanceRecordDraws++;
			continue;
		}
		const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( draw.materialRecordIndex );
		if ( materialRecord == NULL || materialRecord->sourceMaterialRecordIndex != draw.materialRecordIndex ) {
			stats.fallbackDraws++;
			stats.missingMaterialTableDraws++;
			continue;
		}
		if ( materialRecord->fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
			stats.fallbackDraws++;
			stats.materialFallbackDraws++;
			continue;
		}

		if ( draw.passCategory == RENDER_PASS_AMBIENT && R_ModernGLDrawPlan_ShouldUseGBuffer( *materialRecord ) ) {
			pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER;
			shaderKind = ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED )
				? MODERN_GL_SHADER_GBUFFER_ALPHA_TEST
				: MODERN_GL_SHADER_GBUFFER_OPAQUE;
		}
		if ( R_ModernGLDrawPlan_ShouldUseForwardPlus( draw.passCategory, *materialRecord, pipeline, shaderKind ) ) {
			if ( draw.passCategory == RENDER_PASS_AMBIENT && R_ModernGLDrawPlan_ShouldUseGBuffer( *materialRecord ) ) {
				pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER;
				shaderKind = ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED )
					? MODERN_GL_SHADER_GBUFFER_ALPHA_TEST
					: MODERN_GL_SHADER_GBUFFER_OPAQUE;
			}
		}

		const modernGLShaderProgramInfo_t *program = R_ModernGLShaderLibrary_FindProgram( shaderKind, shaderStats.highestGLSLVersion );
		if ( program == NULL || program->program == 0 || !program->linked ) {
			stats.fallbackDraws++;
			continue;
		}

		if ( AddEntry( draw, i, *materialRecord, pipeline, *program ) ) {
			if ( !havePrevious
				|| previousPipeline != pipeline
				|| previousPass != draw.passCategory
				|| previousProgram != program->program
				|| previousMaterial != draw.materialRecordIndex ) {
				stats.stateBatches++;
				if ( !havePrevious || previousPipeline != pipeline ) {
					stats.pipelineBatches++;
				}
				if ( havePrevious && previousProgram != program->program ) {
					stats.programSwitches++;
				}
				if ( havePrevious && previousMaterial != draw.materialRecordIndex ) {
					stats.materialSwitches++;
				}
			}
			if ( !havePrevious || previousGeometry != draw.geometryRecordIndex ) {
				stats.geometryBatches++;
			}
			if ( !havePrevious || previousMaterial != draw.materialRecordIndex ) {
				stats.textureSetBatches++;
			}
			if ( !havePrevious
				|| previousScissorX1 != draw.scissorX1
				|| previousScissorY1 != draw.scissorY1
				|| previousScissorX2 != draw.scissorX2
				|| previousScissorY2 != draw.scissorY2 ) {
				stats.scissorBatches++;
			}
			if ( pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT ) {
				if ( haveTransparentSortKey && draw.sortKey.value < previousTransparentSortKey ) {
					stats.transparentSortBreaks++;
				}
				previousTransparentSortKey = draw.sortKey.value;
				haveTransparentSortKey = true;
			}
			previousPipeline = pipeline;
			previousPass = draw.passCategory;
			previousProgram = program->program;
			previousMaterial = draw.materialRecordIndex;
			previousGeometry = draw.geometryRecordIndex;
			previousScissorX1 = draw.scissorX1;
			previousScissorY1 = draw.scissorY1;
			previousScissorX2 = draw.scissorX2;
			previousScissorY2 = draw.scissorY2;
			havePrevious = true;
		}
	}

	stats.valid = stats.available && !stats.overflow;
	R_ModernGLDrawPlan_SetStatus( stats, stats.valid ? "planned" : "overflow" );
	return stats.valid;
}

int idModernGLDrawPlan::NumEntries( void ) const {
	return numEntries;
}

const modernGLDrawPlanEntry_t &idModernGLDrawPlan::Entry( int index ) const {
	return entries[index];
}

const modernGLDrawPlanStats_t &idModernGLDrawPlan::Stats( void ) const {
	return stats;
}

bool RendererModernGLDrawPlan_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernGLDrawPlan self-test passed (shader library unavailable)\n" );
		return true;
	}

	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	static glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 1 };
	static vertCache_t ambientCache;
	static vertCache_t indexCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	memset( &indexCache, 0, sizeof( indexCache ) );
	ambientCache.vbo = 101;
	ambientCache.offset = 64;
	ambientCache.size = geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	indexCache.vbo = 202;
	indexCache.offset = 128;
	indexCache.size = geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.indexes = indexes;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
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
	drawSurfsCommand_t fxCmd;
	memset( &fxCmd, 0, sizeof( fxCmd ) );
	fxCmd.commandId = RC_DRAW_SPECIAL_EFFECTS;
	fxCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &fxCmd.commandId;
	fxCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );

	idModernGLDrawPlan plan;
	plan.Build( packetFrame, graph );
	const modernGLDrawPlanStats_t &stats = plan.Stats();

	const bool expectedDepthEligible = tr.defaultMaterial != NULL
		&& tr.defaultMaterial->IsDrawn()
		&& tr.defaultMaterial->Coverage() != MC_TRANSLUCENT;
	const bool expectedAmbientEligible = tr.defaultMaterial != NULL
		&& tr.defaultMaterial->HasAmbient()
		&& !tr.defaultMaterial->IsPortalSky()
		&& !tr.defaultMaterial->SuppressInSubview()
		&& tr.defaultMaterial->GetSort() < SS_POST_PROCESS;
	const materialResourceTableRecord_t *defaultRecord = R_MaterialResourceTable_FindRecordForMaterial( tr.defaultMaterial );
	const bool materialModernEligible = defaultRecord != NULL && defaultRecord->fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	const int expectedDepthDraws = expectedDepthEligible && materialModernEligible ? 2 : 0;
	const int expectedMaterialDraws = expectedAmbientEligible && materialModernEligible ? 2 : 0;
	const int expectedPlanned = expectedDepthDraws + expectedMaterialDraws;
	const int expectedFallback = packetFrame.NumDrawPackets() - expectedPlanned;
	if ( stats.sourceDrawPackets != packetFrame.NumDrawPackets() || stats.plannedDraws != expectedPlanned || stats.fallbackDraws != expectedFallback ) {
		common->Printf( "RendererModernGLDrawPlan self-test failed: draw count mismatch\n" );
		return false;
	}
	if ( expectedPlanned > 0 ) {
		const int expectedStateBatches = ( expectedDepthDraws > 0 ? 1 : 0 ) + ( expectedMaterialDraws > 0 ? 1 : 0 );
		const int expectedProgramSwitches = expectedDepthDraws > 0 && expectedMaterialDraws > 0 ? 1 : 0;
		if ( stats.depthDraws != expectedDepthDraws || stats.materialDraws != expectedMaterialDraws || stats.stateBatches != expectedStateBatches || stats.programSwitches != expectedProgramSwitches || stats.overflow ) {
			common->Printf( "RendererModernGLDrawPlan self-test failed: plan classification mismatch\n" );
			return false;
		}
		if ( plan.NumEntries() != stats.plannedDraws ) {
			common->Printf( "RendererModernGLDrawPlan self-test failed: entry count mismatch\n" );
			return false;
		}
		bool sawDepth = false;
		bool sawMaterial = false;
		for ( int i = 0; i < plan.NumEntries(); ++i ) {
			const modernGLDrawPlanEntry_t &entry = plan.Entry( i );
			sawDepth = sawDepth || entry.shaderKind == MODERN_GL_SHADER_DEPTH;
			sawMaterial = sawMaterial || R_ModernGLDrawPlan_IsMaterialPipeline( entry.pipeline );
			if ( entry.modelViewProjectionLocation < 0 || entry.permutation.materialClass == RENDER_MATERIAL_NONE ) {
				common->Printf( "RendererModernGLDrawPlan self-test failed: shader metadata mismatch\n" );
				return false;
			}
			if ( entry.materialTableIndex < 0 || entry.materialTableIndex != entry.materialRecordIndex || entry.geometryRecordIndex < 0 || entry.instanceRecordIndex < 0 ) {
				common->Printf( "RendererModernGLDrawPlan self-test failed: material table index mismatch\n" );
				return false;
			}
		}
		if ( ( expectedDepthDraws > 0 && !sawDepth ) || ( expectedMaterialDraws > 0 && !sawMaterial ) ) {
			common->Printf( "RendererModernGLDrawPlan self-test failed: shader-kind coverage mismatch\n" );
			return false;
		}
	}

	common->Printf(
		"RendererModernGLDrawPlan self-test passed (planned=%d fallback=%d batches=%d glsl=%d)\n",
		stats.plannedDraws,
		stats.fallbackDraws,
		stats.stateBatches,
		stats.highestGLSLVersion );
	return true;
}
