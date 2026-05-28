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
	idStr::Copynz( stats.status, "empty", sizeof( stats.status ) );
	numCommands = 0;
}

static void R_ModernGLSubmitPlan_SetStatus( modernGLSubmitPlanStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status ? status : "unknown", sizeof( stats.status ) );
}

static bool R_ModernGLSubmitPlan_ScissorEquals( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	return a.scissorX1 == b.scissorX1
		&& a.scissorY1 == b.scissorY1
		&& a.scissorX2 == b.scissorX2
		&& a.scissorY2 == b.scissorY2;
}

static modernGLSubmitCommand_t rg_modernGLSubmitPlanSortScratch[MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS];

static int R_ModernGLSubmitPlan_CompareInt( int a, int b ) {
	return ( a > b ) - ( a < b );
}

static int R_ModernGLSubmitPlan_CompareUInt( unsigned int a, unsigned int b ) {
	return ( a > b ) - ( a < b );
}

static bool R_ModernGLSubmitPlan_CommandCanSort( const modernGLSubmitCommand_t &command ) {
	const modernGLDrawPlanEntry_t *entry = command.drawPlanEntry;
	const drawPacket_t *draw = entry != NULL ? entry->drawPacket : NULL;
	if ( draw == NULL || draw->packetCategory != SCENE_PACKET_CATEGORY_WORLD || command.viewDef == NULL ) {
		return false;
	}
	if ( command.weaponDepthHack || command.visibilityDynamic || command.blendMode != MATERIAL_RESOURCE_BLEND_OPAQUE ) {
		return false;
	}
	switch ( command.pipeline ) {
	case MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH:
	case MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER:
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE:
		return true;
	default:
		return false;
	}
}

static int R_ModernGLSubmitPlan_CullSortKey( const modernGLSubmitCommand_t &command ) {
	int key = command.cullType & 0xff;
	if ( command.twoSided ) {
		key |= 1 << 8;
	}
	if ( command.shouldCreateBackSides ) {
		key |= 1 << 9;
	}
	if ( command.negativeScale ) {
		key |= 1 << 10;
	}
	return key;
}

static int R_ModernGLSubmitPlan_CompareSortKey( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	int cmp = R_ModernGLSubmitPlan_CompareInt( static_cast<int>( a.pipeline ), static_cast<int>( b.pipeline ) );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareUInt( a.program, b.program );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.materialTableIndex, b.materialTableIndex );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareUInt( a.vertexBuffer, b.vertexBuffer );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.ambientCacheOffset, b.ambientCacheOffset );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareUInt( a.indexBuffer, b.indexBuffer );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.indexType, b.indexType );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.vertexStride, b.vertexStride );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( R_ModernGLSubmitPlan_CullSortKey( a ), R_ModernGLSubmitPlan_CullSortKey( b ) );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.scissorX1, b.scissorX1 );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.scissorY1, b.scissorY1 );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.scissorX2, b.scissorX2 );
	if ( cmp != 0 ) {
		return cmp;
	}
	cmp = R_ModernGLSubmitPlan_CompareInt( a.scissorY2, b.scissorY2 );
	if ( cmp != 0 ) {
		return cmp;
	}
	return R_ModernGLSubmitPlan_CompareInt( a.originalSubmitOrder, b.originalSubmitOrder );
}

static bool R_ModernGLSubmitPlan_StateBucketEquals( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	return a.pipeline == b.pipeline
		&& a.program == b.program
		&& a.materialTableIndex == b.materialTableIndex
		&& a.vertexBuffer == b.vertexBuffer
		&& a.ambientCacheOffset == b.ambientCacheOffset
		&& a.indexBuffer == b.indexBuffer
		&& a.indexType == b.indexType
		&& a.vertexStride == b.vertexStride
		&& R_ModernGLSubmitPlan_CullSortKey( a ) == R_ModernGLSubmitPlan_CullSortKey( b )
		&& R_ModernGLSubmitPlan_ScissorEquals( a, b );
}

static void R_ModernGLSubmitPlan_StableSortRange( modernGLSubmitCommand_t *commands, int left, int right ) {
	if ( right - left <= 1 ) {
		return;
	}
	const int mid = left + ( right - left ) / 2;
	R_ModernGLSubmitPlan_StableSortRange( commands, left, mid );
	R_ModernGLSubmitPlan_StableSortRange( commands, mid, right );

	for ( int i = left; i < right; ++i ) {
		rg_modernGLSubmitPlanSortScratch[i] = commands[i];
	}
	int a = left;
	int b = mid;
	for ( int out = left; out < right; ++out ) {
		if ( a >= mid ) {
			commands[out] = rg_modernGLSubmitPlanSortScratch[b++];
		} else if ( b >= right ) {
			commands[out] = rg_modernGLSubmitPlanSortScratch[a++];
		} else if ( R_ModernGLSubmitPlan_CompareSortKey( rg_modernGLSubmitPlanSortScratch[a], rg_modernGLSubmitPlanSortScratch[b] ) <= 0 ) {
			commands[out] = rg_modernGLSubmitPlanSortScratch[a++];
		} else {
			commands[out] = rg_modernGLSubmitPlanSortScratch[b++];
		}
	}
}

static void R_ModernGLSubmitPlan_RebuildBatchStats( const modernGLSubmitCommand_t *commands, int numCommands, modernGLSubmitPlanStats_t &stats ) {
	stats.programBatches = 0;
	stats.vertexBufferBatches = 0;
	stats.indexBufferBatches = 0;
	stats.scissorBatches = 0;
	stats.materialBatches = 0;
	stats.sortedStateBuckets = 0;
	for ( int i = 0; i < numCommands; ++i ) {
		const bool havePrevious = i > 0;
		const modernGLSubmitCommand_t &command = commands[i];
		if ( !havePrevious || commands[i - 1].program != command.program ) {
			stats.programBatches++;
		}
		if ( !havePrevious || commands[i - 1].vertexBuffer != command.vertexBuffer ) {
			stats.vertexBufferBatches++;
		}
		if ( command.indexed && !command.uploadIndexBuffer && ( !havePrevious || commands[i - 1].indexBuffer != command.indexBuffer ) ) {
			stats.indexBufferBatches++;
		}
		if ( !havePrevious || !R_ModernGLSubmitPlan_ScissorEquals( commands[i - 1], command ) ) {
			stats.scissorBatches++;
		}
		if ( !havePrevious || commands[i - 1].materialTableIndex != command.materialTableIndex ) {
			stats.materialBatches++;
		}
		if ( !havePrevious || !R_ModernGLSubmitPlan_StateBucketEquals( commands[i - 1], command ) ) {
			stats.sortedStateBuckets++;
		}
	}
}

static void R_ModernGLSubmitPlan_SortCommands( modernGLSubmitCommand_t *commands, int numCommands, modernGLSubmitPlanStats_t &stats ) {
	if ( numCommands <= 0 ) {
		return;
	}

	stats.unsortedStateBuckets = 0;
	for ( int i = 0; i < numCommands; ++i ) {
		commands[i].sortEligible = R_ModernGLSubmitPlan_CommandCanSort( commands[i] );
		commands[i].sortBucket = -1;
		if ( commands[i].sortEligible ) {
			stats.sortEligibleDraws++;
		} else {
			stats.sortLockedDraws++;
		}
		if ( i == 0 || !R_ModernGLSubmitPlan_StateBucketEquals( commands[i - 1], commands[i] ) ) {
			stats.unsortedStateBuckets++;
		}
	}

	int index = 0;
	while ( index < numCommands ) {
		if ( !commands[index].sortEligible ) {
			index++;
			continue;
		}
		const int start = index;
		const renderPassCategory_t passCategory = commands[index].passCategory;
		const viewDef_t *viewDef = commands[index].viewDef;
		while ( index < numCommands
			&& commands[index].sortEligible
			&& commands[index].passCategory == passCategory
			&& commands[index].viewDef == viewDef ) {
			index++;
		}
		if ( index - start > 1 ) {
			stats.sortSpans++;
			R_ModernGLSubmitPlan_StableSortRange( commands, start, index );
		}
	}

	int bucket = -1;
	for ( int i = 0; i < numCommands; ++i ) {
		if ( commands[i].sortEligible ) {
			if ( i == 0 || !commands[i - 1].sortEligible || !R_ModernGLSubmitPlan_StateBucketEquals( commands[i - 1], commands[i] ) ) {
				bucket++;
				stats.sortBuckets++;
			}
			commands[i].sortBucket = bucket;
			if ( commands[i].originalSubmitOrder != i ) {
				stats.sortReorderedDraws++;
			}
		}
	}

	const int unsortedProgramBatches = stats.programBatches;
	const int unsortedVertexBufferBatches = stats.vertexBufferBatches;
	const int unsortedMaterialBatches = stats.materialBatches;
	R_ModernGLSubmitPlan_RebuildBatchStats( commands, numCommands, stats );
	stats.sortStateBucketSavings = Max( 0, stats.unsortedStateBuckets - stats.sortedStateBuckets );
	stats.sortProgramBatchSavings = Max( 0, unsortedProgramBatches - stats.programBatches );
	stats.sortMaterialBatchSavings = Max( 0, unsortedMaterialBatches - stats.materialBatches );
	stats.sortVertexBufferBatchSavings = Max( 0, unsortedVertexBufferBatches - stats.vertexBufferBatches );
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
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GUI
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static int R_ModernGLSubmitPlan_ViewWidth( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidWidth );
	}
	const int viewportWidth = viewDef->viewport.x2 >= viewDef->viewport.x1 ? viewDef->viewport.x2 + 1 - viewDef->viewport.x1 : 0;
	if ( viewportWidth > 0 ) {
		return viewportWidth;
	}
	if ( viewDef->renderView.width > 0 ) {
		return viewDef->renderView.width;
	}
	return Max( 1, glConfig.vidWidth );
}

static int R_ModernGLSubmitPlan_ViewHeight( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidHeight );
	}
	const int viewportHeight = viewDef->viewport.y2 >= viewDef->viewport.y1 ? viewDef->viewport.y2 + 1 - viewDef->viewport.y1 : 0;
	if ( viewportHeight > 0 ) {
		return viewportHeight;
	}
	if ( viewDef->renderView.height > 0 ) {
		return viewDef->renderView.height;
	}
	return Max( 1, glConfig.vidHeight );
}

static bool R_ModernGLSubmitPlan_CommandCanUseMainViewFrustum( const modernGLSubmitCommand_t &command ) {
	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	return draw != NULL
		&& draw->packetCategory == SCENE_PACKET_CATEGORY_WORLD
		&& command.passCategory != RENDER_PASS_SHADOW_MAP
		&& command.passCategory != RENDER_PASS_STENCIL_SHADOW;
}

static bool R_ModernGLSubmitPlan_CommandIsDynamicVisibility( const modernGLSubmitCommand_t &command, const geometryResourceRecord_t &geo, const instanceRecord_t *instance, const materialResourceTableRecord_t *materialRecord ) {
	if ( command.passCategory == RENDER_PASS_SHADOW_MAP || command.passCategory == RENDER_PASS_STENCIL_SHADOW ) {
		return true;
	}
	if ( instance == NULL || instance->weaponDepthHack || ( instance->visibilityFlags & ( INSTANCE_VISIBILITY_VIEWMODEL | INSTANCE_VISIBILITY_SUBVIEW | INSTANCE_VISIBILITY_REMOTE_CAMERA | INSTANCE_VISIBILITY_RENDER_DEMO ) ) != 0 ) {
		return true;
	}
	if ( geo.uploadLifetime != GEOMETRY_UPLOAD_LIFETIME_STATIC ) {
		return true;
	}
	if ( geo.deformMode != GEOMETRY_DEFORM_NONE || geo.skinningMode != GEOMETRY_SKINNING_NONE ) {
		return true;
	}
	if ( materialRecord == NULL || materialRecord->alphaTest || materialRecord->materialClass != RENDER_MATERIAL_OPAQUE || materialRecord->blendMode != MATERIAL_RESOURCE_BLEND_OPAQUE ) {
		return true;
	}
	return false;
}

static void R_ModernGLSubmitPlan_FillVisibilityPacket( modernGLSubmitCommand_t &command, const geometryResourceRecord_t &geo, const instanceRecord_t *instance, const materialResourceTableRecord_t *materialRecord ) {
	command.visibilityScreenX1 = 0;
	command.visibilityScreenY1 = 0;
	command.visibilityScreenX2 = -1;
	command.visibilityScreenY2 = -1;
	command.visibilityDepthMin = 1.0f;
	command.visibilityDepthMax = 0.0f;
	command.visibilityBoundsValid = false;
	command.visibilityFrustumEligible = R_ModernGLSubmitPlan_CommandCanUseMainViewFrustum( command );
	command.visibilityFrustumRejected = false;
	command.visibilityScreenRectValid = false;
	command.visibilityNearPlaneClipped = false;
	command.visibilityShadowCaster = command.passCategory == RENDER_PASS_SHADOW_MAP || command.passCategory == RENDER_PASS_STENCIL_SHADOW;
	command.visibilityDynamic = R_ModernGLSubmitPlan_CommandIsDynamicVisibility( command, geo, instance, materialRecord );
	command.visibilityHiZCandidate = false;

	if ( !command.visibilityFrustumEligible || command.viewDef == NULL || !geo.hasBounds || geo.bounds.IsCleared() ) {
		return;
	}

	const int viewWidth = R_ModernGLSubmitPlan_ViewWidth( command.viewDef );
	const int viewHeight = R_ModernGLSubmitPlan_ViewHeight( command.viewDef );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return;
	}

	command.visibilityBoundsValid = true;
	int outside[6] = { 0, 0, 0, 0, 0, 0 };
	float minX = 1.0f;
	float minY = 1.0f;
	float maxX = -1.0f;
	float maxY = -1.0f;
	float minDepth = 1.0f;
	float maxDepth = 0.0f;

	for ( int i = 0; i < 8; ++i ) {
		idVec3 corner;
		corner.x = geo.bounds[( i & 1 ) ? 1 : 0].x;
		corner.y = geo.bounds[( i & 2 ) ? 1 : 0].y;
		corner.z = geo.bounds[( i & 4 ) ? 1 : 0].z;
		idPlane eye;
		idPlane clip;
		R_TransformModelToClip( corner, command.modelViewMatrix, command.viewDef->projectionMatrix, eye, clip );
		if ( clip[3] <= 1.0e-4f ) {
			command.visibilityNearPlaneClipped = true;
			continue;
		}
		if ( clip[0] < -clip[3] ) {
			outside[0]++;
		}
		if ( clip[0] > clip[3] ) {
			outside[1]++;
		}
		if ( clip[1] < -clip[3] ) {
			outside[2]++;
		}
		if ( clip[1] > clip[3] ) {
			outside[3]++;
		}
		if ( clip[2] < -clip[3] ) {
			outside[4]++;
		}
		if ( clip[2] > clip[3] ) {
			outside[5]++;
		}

		const float invW = 1.0f / clip[3];
		const float ndcX = idMath::ClampFloat( -1.0f, 1.0f, clip[0] * invW );
		const float ndcY = idMath::ClampFloat( -1.0f, 1.0f, clip[1] * invW );
		const float ndcZ = idMath::ClampFloat( -1.0f, 1.0f, clip[2] * invW );
		minX = Min( minX, ndcX );
		minY = Min( minY, ndcY );
		maxX = Max( maxX, ndcX );
		maxY = Max( maxY, ndcY );
		const float depth = idMath::ClampFloat( 0.0f, 1.0f, ndcZ * 0.5f + 0.5f );
		minDepth = Min( minDepth, depth );
		maxDepth = Max( maxDepth, depth );
	}

	if ( command.visibilityNearPlaneClipped ) {
		return;
	}
	for ( int i = 0; i < 6; ++i ) {
		if ( outside[i] == 8 ) {
			command.visibilityFrustumRejected = true;
			return;
		}
	}

	const int screenX1 = idMath::FtoiFast( idMath::Floor( ( minX * 0.5f + 0.5f ) * static_cast<float>( viewWidth - 1 ) ) );
	const int screenY1 = idMath::FtoiFast( idMath::Floor( ( minY * 0.5f + 0.5f ) * static_cast<float>( viewHeight - 1 ) ) );
	const int screenX2 = idMath::FtoiFast( idMath::Ceil( ( maxX * 0.5f + 0.5f ) * static_cast<float>( viewWidth - 1 ) ) );
	const int screenY2 = idMath::FtoiFast( idMath::Ceil( ( maxY * 0.5f + 0.5f ) * static_cast<float>( viewHeight - 1 ) ) );
	command.visibilityScreenX1 = idMath::ClampInt( 0, viewWidth - 1, screenX1 - 1 );
	command.visibilityScreenY1 = idMath::ClampInt( 0, viewHeight - 1, screenY1 - 1 );
	command.visibilityScreenX2 = idMath::ClampInt( 0, viewWidth - 1, screenX2 + 1 );
	command.visibilityScreenY2 = idMath::ClampInt( 0, viewHeight - 1, screenY2 + 1 );
	command.visibilityDepthMin = minDepth;
	command.visibilityDepthMax = maxDepth;
	command.visibilityScreenRectValid = command.visibilityScreenX1 <= command.visibilityScreenX2 && command.visibilityScreenY1 <= command.visibilityScreenY2;
	command.visibilityHiZCandidate = command.visibilityScreenRectValid && !command.visibilityDynamic;
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
	command.modelViewMatrixLocation = entry.modelViewMatrixLocation;
	command.debugColorLocation = entry.debugColorLocation;
	command.localParamsLocation = entry.localParamsLocation;
	command.mainTextureLocation = entry.mainTextureLocation;
	command.normalTextureLocation = entry.normalTextureLocation;
	command.specularTextureLocation = entry.specularTextureLocation;
	command.emissiveTextureLocation = entry.emissiveTextureLocation;
	command.textureIndicesLocation = entry.textureIndicesLocation;
	command.textureTableModeLocation = entry.textureTableModeLocation;
	command.materialFlagsLocation = entry.materialFlagsLocation;
	command.materialEnhancementLocation = entry.materialEnhancementLocation;
	command.drawRecordModeLocation = entry.drawRecordModeLocation;
	command.vertexStride = geo->vertexStride;
	command.indexType = geo->indexType;
	command.indexCount = entry.indexCount;
	command.vertexCount = entry.vertexCount;
	command.materialRecordIndex = entry.materialRecordIndex;
	command.materialTableIndex = entry.materialTableIndex;
	command.geometryRecordIndex = entry.geometryRecordIndex;
	command.instanceRecordIndex = entry.instanceRecordIndex;
	command.originalSubmitOrder = numCommands;
	command.sortBucket = -1;
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( entry.materialTableIndex );
	command.blendMode = materialRecord != NULL ? materialRecord->blendMode : MATERIAL_RESOURCE_BLEND_OPAQUE;
	command.cullType = materialRecord != NULL ? materialRecord->cullType : CT_FRONT_SIDED;
	command.materialStableId = entry.materialStableId;
	memcpy( command.modelViewMatrix, instance->modelViewMatrix, sizeof( command.modelViewMatrix ) );
	command.modelDepthHack = instance->modelDepthHack;
	command.scissorX1 = draw->scissorX1;
	command.scissorY1 = draw->scissorY1;
	command.scissorX2 = draw->scissorX2;
	command.scissorY2 = draw->scissorY2;
	command.indexed = entry.indexed;
	command.uploadIndexBuffer = canUploadIndexes;
	command.twoSided = materialRecord != NULL && materialRecord->twoSided;
	command.shouldCreateBackSides = materialRecord != NULL && materialRecord->shouldCreateBackSides;
	command.weaponDepthHack = instance->weaponDepthHack;
	command.negativeScale = instance->negativeScale;
	command.sortEligible = false;
	R_ModernGLSubmitPlan_FillVisibilityPacket( command, *geo, instance, materialRecord );

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
	R_ModernGLSubmitPlan_SortCommands( commands, numCommands, stats );

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

static void R_ModernGLSubmitPlan_InitSortSelfTestCommand(
	modernGLSubmitCommand_t &command,
	modernGLDrawPlanEntry_t &entry,
	drawPacket_t &draw,
	const viewDef_t *viewDef,
	int originalOrder,
	unsigned int program,
	int materialTableIndex,
	unsigned int vertexBuffer,
	bool sortable ) {
	memset( &draw, 0, sizeof( draw ) );
	draw.packetCategory = sortable ? SCENE_PACKET_CATEGORY_WORLD : SCENE_PACKET_CATEGORY_GUI;
	draw.passCategory = sortable ? RENDER_PASS_DEPTH : RENDER_PASS_GUI;
	memset( &entry, 0, sizeof( entry ) );
	entry.drawPacket = &draw;
	entry.pipeline = sortable ? MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH : MODERN_GL_DRAW_PLAN_PIPELINE_GUI;
	entry.program = program;
	entry.materialTableIndex = materialTableIndex;
	entry.materialRecordIndex = materialTableIndex;

	memset( &command, 0, sizeof( command ) );
	command.drawPlanEntry = &entry;
	command.viewDef = viewDef;
	command.passCategory = draw.passCategory;
	command.pipeline = entry.pipeline;
	command.program = program;
	command.vertexBuffer = vertexBuffer;
	command.indexBuffer = 301;
	command.ambientCacheOffset = 64;
	command.indexCacheOffset = 128;
	command.vertexStride = static_cast<int>( sizeof( idDrawVert ) );
	command.indexType = GL_UNSIGNED_INT;
	command.indexCount = 6;
	command.vertexCount = 3;
	command.materialTableIndex = materialTableIndex;
	command.materialRecordIndex = materialTableIndex;
	command.geometryRecordIndex = originalOrder;
	command.instanceRecordIndex = originalOrder;
	command.blendMode = sortable ? MATERIAL_RESOURCE_BLEND_OPAQUE : MATERIAL_RESOURCE_BLEND_GUI;
	command.cullType = CT_FRONT_SIDED;
	command.originalSubmitOrder = originalOrder;
	command.sortBucket = -1;
	command.scissorX1 = 0;
	command.scissorY1 = 0;
	command.scissorX2 = 639;
	command.scissorY2 = 479;
	command.indexed = true;
	command.uploadIndexBuffer = false;
	command.visibilityDynamic = false;
	command.sortEligible = false;
}

static bool R_ModernGLSubmitPlan_RunSortSelfTest( void ) {
	viewDef_t viewDef;
	memset( &viewDef, 0, sizeof( viewDef ) );
	modernGLSubmitCommand_t commands[5];
	modernGLDrawPlanEntry_t entries[5];
	drawPacket_t draws[5];
	R_ModernGLSubmitPlan_InitSortSelfTestCommand( commands[0], entries[0], draws[0], &viewDef, 0, 200, 20, 20, true );
	R_ModernGLSubmitPlan_InitSortSelfTestCommand( commands[1], entries[1], draws[1], &viewDef, 1, 100, 10, 10, true );
	R_ModernGLSubmitPlan_InitSortSelfTestCommand( commands[2], entries[2], draws[2], &viewDef, 2, 200, 20, 20, true );
	R_ModernGLSubmitPlan_InitSortSelfTestCommand( commands[3], entries[3], draws[3], &viewDef, 3, 100, 10, 10, true );
	R_ModernGLSubmitPlan_InitSortSelfTestCommand( commands[4], entries[4], draws[4], &viewDef, 4, 300, 30, 30, false );

	modernGLSubmitPlanStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	R_ModernGLSubmitPlan_RebuildBatchStats( commands, 5, stats );
	R_ModernGLSubmitPlan_SortCommands( commands, 5, stats );
	if ( !commands[0].sortEligible
		|| !commands[1].sortEligible
		|| !commands[2].sortEligible
		|| !commands[3].sortEligible
		|| commands[4].sortEligible
		|| commands[0].program != 100
		|| commands[1].program != 100
		|| commands[2].program != 200
		|| commands[3].program != 200
		|| commands[4].originalSubmitOrder != 4
		|| commands[0].originalSubmitOrder != 1
		|| commands[1].originalSubmitOrder != 3
		|| commands[2].originalSubmitOrder != 0
		|| commands[3].originalSubmitOrder != 2 ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: sort order or lock mismatch\n" );
		return false;
	}
	if ( stats.sortEligibleDraws != 4
		|| stats.sortLockedDraws != 1
		|| stats.sortSpans != 1
		|| stats.sortBuckets != 2
		|| stats.sortReorderedDraws != 4
		|| stats.unsortedStateBuckets != 5
		|| stats.sortedStateBuckets != 3
		|| stats.sortStateBucketSavings != 2
		|| stats.sortProgramBatchSavings != 2
		|| stats.sortMaterialBatchSavings != 2
		|| stats.sortVertexBufferBatchSavings != 2 ) {
		common->Printf(
			"RendererModernGLSubmitPlan self-test failed: sort stats mismatch (eligible=%d locked=%d spans=%d buckets=%d moved=%d state=%d/%d saved=%d program=%d material=%d vbo=%d)\n",
			stats.sortEligibleDraws,
			stats.sortLockedDraws,
			stats.sortSpans,
			stats.sortBuckets,
			stats.sortReorderedDraws,
			stats.unsortedStateBuckets,
			stats.sortedStateBuckets,
			stats.sortStateBucketSavings,
			stats.sortProgramBatchSavings,
			stats.sortMaterialBatchSavings,
			stats.sortVertexBufferBatchSavings );
		return false;
	}
	return true;
}

bool RendererModernGLSubmitPlan_RunSelfTest( void ) {
	if ( !R_ModernGLSubmitPlan_RunSortSelfTest() ) {
		return false;
	}
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
	const int expectedReadyDraws = expectedDepthDraws + expectedMaterialDraws;
	const int expectedGeometryFallbackDraws = ( expectedDepthEligible ? 2 : 0 ) + ( expectedAmbientEligible ? 2 : 0 );
	if ( readyStats.sourcePlanDraws != expectedReadyDraws || readyStats.readyDraws != expectedReadyDraws || readyStats.fallbackDraws != 0 || readyStats.overflow ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: ready draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 ) {
		const int expectedProgramBatches = ( expectedDepthDraws > 0 ? 1 : 0 ) + ( expectedMaterialDraws > 0 ? 1 : 0 );
		if ( readyStats.depthReadyDraws != expectedDepthDraws
			|| readyStats.materialReadyDraws != expectedMaterialDraws
			|| readyStats.programBatches != expectedProgramBatches
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
	const modernGLDrawPlanStats_t &missingDrawStats = missingCacheDrawPlan.Stats();
	const modernGLSubmitPlanStats_t &fallbackStats = missingCacheSubmitPlan.Stats();
	if ( missingDrawStats.sourceDrawPackets != missingCachePacketFrame.NumDrawPackets()
		|| missingDrawStats.plannedDraws != 0
		|| missingDrawStats.geometryFallbackDraws != expectedGeometryFallbackDraws
		|| missingDrawStats.geometryVertexBufferFallbackDraws != expectedGeometryFallbackDraws
		|| fallbackStats.sourcePlanDraws != 0
		|| fallbackStats.readyDraws != 0
		|| fallbackStats.fallbackDraws != 0 ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: early geometry fallback count mismatch\n" );
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
		"RendererModernGLSubmitPlan self-test passed (ready=%d fallback=%d tempIndex=%d uploadIndex=%d programBatches=%d vertexBatches=%d indexBatches=%d sortEligible=%d sortSaved=%d)\n",
		readyStats.readyDraws,
		fallbackStats.fallbackDraws,
		tempIndexStats.indexCacheReadyDraws,
		uploadStats.indexUploadDraws,
		readyStats.programBatches,
		readyStats.vertexBufferBatches,
		readyStats.indexBufferBatches,
		readyStats.sortEligibleDraws,
		readyStats.sortStateBucketSavings );
	return true;
}
