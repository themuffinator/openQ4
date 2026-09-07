// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLSubmitPlan.h"
#include "RendererContracts.h"

idModernGLSubmitPlan::idModernGLSubmitPlan()
	: numCommands( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idModernGLSubmitPlan::Clear( void ) {
#ifdef _DEBUG
	memset( commands, 0, sizeof( commands ) );
#endif
	memset( &stats, 0, sizeof( stats ) );
	idStr::Copynz( stats.status, "empty", sizeof( stats.status ) );
	numCommands = 0;
}

static void R_ModernGLSubmitPlan_SetStatus( modernGLSubmitPlanStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status ? status : "unknown", sizeof( stats.status ) );
}

static float R_ModernGLSubmitPlan_CalcFovForAspect( float fovX, float width, float height ) {
	const float clampedFovX = idMath::ClampFloat( 1.0f, 179.0f, fovX );
	const float safeWidth = Max( width, 1.0f );
	const float safeHeight = Max( height, 1.0f );
	const float x = safeWidth / idMath::Tan( DEG2RAD( clampedFovX ) * 0.5f );
	return RAD2DEG( idMath::ATan( safeHeight / Max( x, idMath::FLOAT_EPSILON ) ) ) * 2.0f;
}

void R_ModernGLSubmitCommand_BuildModelViewProjection( modernGLSubmitCommand_t &command ) {
	float projectionMatrix[16];
	if ( command.viewDef == NULL ) {
		memset( command.modelViewProjectionMatrix, 0, sizeof( command.modelViewProjectionMatrix ) );
		return;
	}
	memcpy( projectionMatrix, command.viewDef->projectionMatrix, sizeof( projectionMatrix ) );
	if ( command.modelDepthHack != 0.0f ) {
		projectionMatrix[14] -= command.modelDepthHack;
	} else if ( command.weaponDepthHack ) {
		const float weaponFovOverride = cl_gunfov.GetFloat();
		if ( weaponFovOverride > 0.0f ) {
			const float viewportWidth = static_cast<float>( Max( 1, command.viewDef->viewport.x2 - command.viewDef->viewport.x1 + 1 ) );
			const float viewportHeight = static_cast<float>( Max( 1, command.viewDef->viewport.y2 - command.viewDef->viewport.y1 + 1 ) );

			float weaponFovX = idMath::ClampFloat( 30.0f, 160.0f, weaponFovOverride );
			float weaponFovY = 0.0f;
			if ( cl_gunfov_adjust.GetBool() ) {
				weaponFovY = R_ModernGLSubmitPlan_CalcFovForAspect( weaponFovX, 4.0f, 3.0f );
				weaponFovX = R_ModernGLSubmitPlan_CalcFovForAspect( weaponFovY, viewportHeight, viewportWidth );
			} else {
				weaponFovY = R_ModernGLSubmitPlan_CalcFovForAspect( weaponFovX, viewportWidth, viewportHeight );
			}

			weaponFovX = idMath::ClampFloat( 1.0f, 179.0f, weaponFovX );
			weaponFovY = idMath::ClampFloat( 1.0f, 179.0f, weaponFovY );
			projectionMatrix[0] = 1.0f / idMath::Tan( DEG2RAD( weaponFovX ) * 0.5f );
			projectionMatrix[5] = 1.0f / idMath::Tan( DEG2RAD( weaponFovY ) * 0.5f );
		}
		projectionMatrix[14] *= 0.25f;
	}
	float canonicalModelViewProjection[ 16 ];
	myGlMultMatrix( command.modelViewMatrix, projectionMatrix, canonicalModelViewProjection );
	RendererContracts_ConvertClipMatrix( command.modelViewProjectionMatrix,
		canonicalModelViewProjection, RendererContracts_GLClipSpace(),
		RendererContracts_GLClipSpace() );
}

static bool R_ModernGLSubmitPlan_ScissorEquals( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	return a.scissorX1 == b.scissorX1
		&& a.scissorY1 == b.scissorY1
		&& a.scissorX2 == b.scissorX2
		&& a.scissorY2 == b.scissorY2;
}

static modernGLSubmitCommand_t rg_modernGLSubmitPlanSortScratch[MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS];
// The comparator ends with the unique originalSubmitOrder, so the sorted
// permutation is a strict total order: sorting an index array through the
// same comparator and applying it with two struct copies per element yields
// the byte-identical command order without O(n log n) struct moves.
static int rg_modernGLSubmitPlanSortOrder[MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS];
static int rg_modernGLSubmitPlanSortOrderScratch[MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS];

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
	return command.cullSortKey;
}

static int R_ModernGLSubmitPlan_BuildCullSortKey( const modernGLSubmitCommand_t &command ) {
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

static void R_ModernGLSubmitPlan_TransformPointToClip( const idVec3 &src, const float mvp[16], idPlane &clip ) {
	for ( int i = 0; i < 4; ++i ) {
		clip[i] =
			src[0] * mvp[i + 0 * 4] +
			src[1] * mvp[i + 1 * 4] +
			src[2] * mvp[i + 2 * 4] +
			mvp[i + 3 * 4];
	}
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

static const int MODERN_GL_SUBMIT_PLAN_INSERTION_SORT_THRESHOLD = 16;

static bool R_ModernGLSubmitPlan_RangeAlreadySorted( const modernGLSubmitCommand_t *commands, int left, int right ) {
	for ( int i = left + 1; i < right; ++i ) {
		if ( R_ModernGLSubmitPlan_CompareSortKey( commands[i - 1], commands[i] ) > 0 ) {
			return false;
		}
	}
	return true;
}

static void R_ModernGLSubmitPlan_InsertionSortRange( const modernGLSubmitCommand_t *commands, int *order, int left, int right ) {
	for ( int i = left + 1; i < right; ++i ) {
		const int key = order[i];
		int j = i;
		while ( j > left && R_ModernGLSubmitPlan_CompareSortKey( commands[order[j - 1]], commands[key] ) > 0 ) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = key;
	}
}

static void R_ModernGLSubmitPlan_StableSortRange( const modernGLSubmitCommand_t *commands, int *order, int left, int right ) {
	if ( right - left <= 1 ) {
		return;
	}
	if ( right - left <= MODERN_GL_SUBMIT_PLAN_INSERTION_SORT_THRESHOLD ) {
		R_ModernGLSubmitPlan_InsertionSortRange( commands, order, left, right );
		return;
	}
	const int mid = left + ( right - left ) / 2;
	R_ModernGLSubmitPlan_StableSortRange( commands, order, left, mid );
	R_ModernGLSubmitPlan_StableSortRange( commands, order, mid, right );
	if ( R_ModernGLSubmitPlan_CompareSortKey( commands[order[mid - 1]], commands[order[mid]] ) <= 0 ) {
		return;
	}

	for ( int i = left; i < right; ++i ) {
		rg_modernGLSubmitPlanSortOrderScratch[i] = order[i];
	}
	int a = left;
	int b = mid;
	for ( int out = left; out < right; ++out ) {
		if ( a >= mid ) {
			order[out] = rg_modernGLSubmitPlanSortOrderScratch[b++];
		} else if ( b >= right ) {
			order[out] = rg_modernGLSubmitPlanSortOrderScratch[a++];
		} else if ( R_ModernGLSubmitPlan_CompareSortKey( commands[rg_modernGLSubmitPlanSortOrderScratch[a]], commands[rg_modernGLSubmitPlanSortOrderScratch[b]] ) <= 0 ) {
			order[out] = rg_modernGLSubmitPlanSortOrderScratch[a++];
		} else {
			order[out] = rg_modernGLSubmitPlanSortOrderScratch[b++];
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
	bool sortedAnySpan = false;
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
			if ( !R_ModernGLSubmitPlan_RangeAlreadySorted( commands, start, index ) ) {
				for ( int i = start; i < index; ++i ) {
					rg_modernGLSubmitPlanSortOrder[i] = i;
				}
				R_ModernGLSubmitPlan_StableSortRange( commands, rg_modernGLSubmitPlanSortOrder, start, index );
				for ( int i = start; i < index; ++i ) {
					rg_modernGLSubmitPlanSortScratch[i] = commands[i];
				}
				for ( int i = start; i < index; ++i ) {
					commands[i] = rg_modernGLSubmitPlanSortScratch[rg_modernGLSubmitPlanSortOrder[i]];
				}
				sortedAnySpan = true;
			}
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

	if ( !sortedAnySpan ) {
		stats.sortedStateBuckets = stats.unsortedStateBuckets;
		stats.sortStateBucketSavings = 0;
		stats.sortProgramBatchSavings = 0;
		stats.sortMaterialBatchSavings = 0;
		stats.sortVertexBufferBatchSavings = 0;
		return;
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
	float visibilityModelViewProjectionMatrix[16];
	const float *visibilityMVP = command.modelViewProjectionMatrix;
	if ( command.modelDepthHack != 0.0f || command.weaponDepthHack ) {
		myGlMultMatrix( command.modelViewMatrix, command.viewDef->projectionMatrix, visibilityModelViewProjectionMatrix );
		visibilityMVP = visibilityModelViewProjectionMatrix;
	}

	for ( int i = 0; i < 8; ++i ) {
		idVec3 corner;
		corner.x = geo.bounds[( i & 1 ) ? 1 : 0].x;
		corner.y = geo.bounds[( i & 2 ) ? 1 : 0].y;
		corner.z = geo.bounds[( i & 4 ) ? 1 : 0].z;
		idPlane clip;
		R_ModernGLSubmitPlan_TransformPointToClip( corner, visibilityMVP, clip );
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

static unsigned int R_ModernGLSubmitPlan_ImageHandleOrZero( const idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		return const_cast<idImage *>( image )->GetDeviceHandle();
	}
	return 0;
}

static unsigned int R_ModernGLSubmitPlan_FallbackTextureForSemantic( materialResourceTextureSemantic_t semantic ) {
	if ( globalImages == NULL ) {
		return 0;
	}
	switch ( semantic ) {
	case MATERIAL_RESOURCE_TEXTURE_BUMP:
		if ( const unsigned int handle = R_ModernGLSubmitPlan_ImageHandleOrZero( globalImages->flatNormalMap ) ) {
			return handle;
		}
		break;
	case MATERIAL_RESOURCE_TEXTURE_SPECULAR:
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE:
		if ( const unsigned int handle = R_ModernGLSubmitPlan_ImageHandleOrZero( globalImages->blackImage ) ) {
			return handle;
		}
		break;
	default:
		break;
	}
	if ( const unsigned int handle = R_ModernGLSubmitPlan_ImageHandleOrZero( globalImages->whiteImage ) ) {
		return handle;
	}
	return R_ModernGLSubmitPlan_ImageHandleOrZero( globalImages->defaultImage );
}

static int R_ModernGLSubmitPlan_TextureTableIndexForHandle( unsigned int textureHandle ) {
	return R_MaterialResourceTable_TextureArrayTableIndexForHandle( textureHandle );
}

static int R_ModernGLSubmitPlan_TextureTableIndexForBinding( const materialResourceTextureBinding_t *binding ) {
	if ( binding == NULL || binding->textureHandle == 0 ) {
		return -1;
	}
	if ( binding->textureArrayCandidate && binding->textureArrayLayer >= 0 ) {
		return binding->textureArrayLayer;
	}
	return R_ModernGLSubmitPlan_TextureTableIndexForHandle( binding->textureHandle );
}

static const materialResourceTextureBinding_t *R_ModernGLSubmitPlan_PrimaryTextureBinding( const materialResourceTableRecord_t *materialRecord ) {
	if ( materialRecord == NULL ) {
		return NULL;
	}
	static const materialResourceTextureSemantic_t primarySemantics[3] = {
		MATERIAL_RESOURCE_TEXTURE_DIFFUSE,
		MATERIAL_RESOURCE_TEXTURE_GUI,
		MATERIAL_RESOURCE_TEXTURE_POST_PROCESS
	};
	for ( int i = 0; i < 3; ++i ) {
		const materialResourceTextureBinding_t *binding = R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord, primarySemantics[i] );
		if ( binding != NULL && binding->textureHandle != 0 ) {
			return binding;
		}
	}
	return NULL;
}

static void R_ModernGLSubmitPlan_ResetMaterialTextureCache( modernGLSubmitCommand_t &command ) {
	for ( int i = 0; i < MODERN_GL_SUBMIT_MATERIAL_TEXTURE_COUNT; ++i ) {
		command.materialTextureHandles[i] = 0;
		command.materialTextureTableIndices[i] = -1;
	}
	command.materialLoadedTextureSemanticMask = 0;
	command.materialAlphaTestRegister = 0;
	command.alphaTestMaterial = false;
	command.alphaTestOrPerforatedMaterial = false;
}

static void R_ModernGLSubmitPlan_SetTextureCacheSlot(
	modernGLSubmitCommand_t &command,
	int slot,
	const materialResourceTextureBinding_t *binding,
	materialResourceTextureSemantic_t fallbackSemantic ) {
	if ( slot < 0 || slot >= MODERN_GL_SUBMIT_MATERIAL_TEXTURE_COUNT ) {
		return;
	}
	if ( binding != NULL && binding->textureHandle != 0 ) {
		command.materialTextureHandles[slot] = binding->textureHandle;
		command.materialTextureTableIndices[slot] = R_ModernGLSubmitPlan_TextureTableIndexForBinding( binding );
		return;
	}
	const unsigned int fallbackHandle = R_ModernGLSubmitPlan_FallbackTextureForSemantic( fallbackSemantic );
	command.materialTextureHandles[slot] = fallbackHandle;
	command.materialTextureTableIndices[slot] = R_ModernGLSubmitPlan_TextureTableIndexForHandle( fallbackHandle );
}

static void R_ModernGLSubmitPlan_FillMaterialTextureCache( modernGLSubmitCommand_t &command, const materialResourceTableRecord_t *materialRecord ) {
	R_ModernGLSubmitPlan_ResetMaterialTextureCache( command );
	const bool pbrModernMaterial = materialRecord != NULL
		&& R_MaterialResourceTable_PBRModernPathEligible( *materialRecord );
	if ( materialRecord != NULL ) {
		command.materialLoadedTextureSemanticMask = materialRecord->loadedTextureSemanticMask;
		command.materialAlphaTestRegister = materialRecord->alphaTestRegister;
		command.alphaTestMaterial = materialRecord->alphaTest;
		command.alphaTestOrPerforatedMaterial = materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED;
	}
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_MAIN,
		pbrModernMaterial
			? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord, MATERIAL_RESOURCE_TEXTURE_ALBEDO )
			: R_ModernGLSubmitPlan_PrimaryTextureBinding( materialRecord ),
		pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_ALBEDO : MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_NORMAL,
		materialRecord != NULL ? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord,
			pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_NORMAL : MATERIAL_RESOURCE_TEXTURE_BUMP ) : NULL,
		pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_NORMAL : MATERIAL_RESOURCE_TEXTURE_BUMP );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_SPECULAR,
		materialRecord != NULL ? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord,
			pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_ORM : MATERIAL_RESOURCE_TEXTURE_SPECULAR ) : NULL,
		pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_ORM : MATERIAL_RESOURCE_TEXTURE_SPECULAR );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_EMISSIVE,
		materialRecord != NULL ? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord,
			pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR : MATERIAL_RESOURCE_TEXTURE_EMISSIVE ) : NULL,
		pbrModernMaterial ? MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR : MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_METALLIC,
		pbrModernMaterial && materialRecord->pbrSeparateMaterialData
			? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord, MATERIAL_RESOURCE_TEXTURE_METALLIC ) : NULL,
		MATERIAL_RESOURCE_TEXTURE_METALLIC );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_ROUGHNESS,
		pbrModernMaterial && materialRecord->pbrSeparateMaterialData
			? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord, MATERIAL_RESOURCE_TEXTURE_ROUGHNESS ) : NULL,
		MATERIAL_RESOURCE_TEXTURE_ROUGHNESS );
	R_ModernGLSubmitPlan_SetTextureCacheSlot(
		command,
		MODERN_GL_SUBMIT_MATERIAL_TEXTURE_AO,
		pbrModernMaterial && materialRecord->pbrSeparateMaterialData
			? R_MaterialResourceTable_TextureBindingForSemantic( *materialRecord, MATERIAL_RESOURCE_TEXTURE_AO ) : NULL,
		MATERIAL_RESOURCE_TEXTURE_AO );
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
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( entry.materialTableIndex );
	if ( materialRecord != NULL
		&& !R_MaterialResourceTable_ClassicModernPathEligible( *materialRecord )
		&& !R_MaterialResourceTable_PBRModernPathEligible( *materialRecord ) ) {
		stats.fallbackDraws++;
		return false;
	}

	modernGLSubmitCommand_t &command = commands[numCommands];
	memset( &command, 0, sizeof( command ) );
	command.drawPlanEntry = &entry;
	command.materialRecord = materialRecord;
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
	command.pbrIBLLocation = entry.pbrIBLLocation;
	command.mainTextureLocation = entry.mainTextureLocation;
	command.normalTextureLocation = entry.normalTextureLocation;
	command.specularTextureLocation = entry.specularTextureLocation;
	command.emissiveTextureLocation = entry.emissiveTextureLocation;
	command.metallicTextureLocation = entry.metallicTextureLocation;
	command.roughnessTextureLocation = entry.roughnessTextureLocation;
	command.aoTextureLocation = entry.aoTextureLocation;
	command.textureIndicesLocation = entry.textureIndicesLocation;
	command.textureTableModeLocation = entry.textureTableModeLocation;
	command.materialFlagsLocation = entry.materialFlagsLocation;
	command.materialEnhancementLocation = entry.materialEnhancementLocation;
	command.drawRecordModeLocation = entry.drawRecordModeLocation;
	command.drawRecordCountLocation = entry.drawRecordCountLocation;
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
	command.blendMode = materialRecord != NULL ? materialRecord->blendMode : MATERIAL_RESOURCE_BLEND_OPAQUE;
	command.cullType = materialRecord != NULL ? materialRecord->cullType : CT_FRONT_SIDED;
	command.materialStableId = entry.materialStableId;
	R_ModernGLSubmitPlan_FillMaterialTextureCache( command, materialRecord );
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
	command.cullSortKey = R_ModernGLSubmitPlan_BuildCullSortKey( command );
	command.sortEligible = false;
	command.forwardPlusDecal =
		command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT
		&& materialRecord != NULL
		&& draw->passCategory == RENDER_PASS_AMBIENT
		&& ( materialRecord->sortGroup == MATERIAL_RESOURCE_SORT_DECAL
			|| ( draw->legacyDrawSurf != NULL && draw->legacyDrawSurf->decalColorCache != NULL ) );
	R_ModernGLSubmitCommand_BuildModelViewProjection( command );
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
	const int drawPlanEntries = drawPlan.NumEntries();
	for ( int i = 0; i < drawPlanEntries; ++i ) {
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
	R_ModernGLSubmitPlan_ResetMaterialTextureCache( command );
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
	command.cullSortKey = R_ModernGLSubmitPlan_BuildCullSortKey( command );
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

	// Four packet, draw and submit arenas remain alive together. All three
	// arena types must stay off the finite render-thread stack, even though
	// each fixture uses only a handful of records.
	idAutoPtr<idScenePacketFrame> packetFrame( new idScenePacketFrame );
	idRenderGraph graph;
	idAutoPtr<idModernGLDrawPlan> drawPlanStorage( new idModernGLDrawPlan );
	idModernGLDrawPlan &drawPlan = *drawPlanStorage;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, true, TAG_USED, false,
		drawPlan, *packetFrame, graph );

	idAutoPtr<idModernGLSubmitPlan> submitPlanStorage( new idModernGLSubmitPlan );
	idModernGLSubmitPlan &submitPlan = *submitPlanStorage;
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
		for ( int i = 0; i < submitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = submitPlan.Command( i );
			if ( command.drawPlanEntry == NULL
				|| command.drawRecordModeLocation != command.drawPlanEntry->drawRecordModeLocation
				|| command.drawRecordCountLocation != command.drawPlanEntry->drawRecordCountLocation
				|| ( command.drawRecordModeLocation >= 0 ) != ( command.drawRecordCountLocation >= 0 ) ) {
				common->Printf( "RendererModernGLSubmitPlan self-test failed: draw-record uniform propagation mismatch\n" );
				return false;
			}
		}
	}

	idAutoPtr<idModernGLDrawPlan> missingCacheDrawPlanStorage( new idModernGLDrawPlan );
	idModernGLDrawPlan &missingCacheDrawPlan = *missingCacheDrawPlanStorage;
	idAutoPtr<idScenePacketFrame> missingCachePacketFrame(
		new idScenePacketFrame );
	idRenderGraph missingCacheGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( false, false, TAG_FREE, false,
		missingCacheDrawPlan, *missingCachePacketFrame, missingCacheGraph );
	idAutoPtr<idModernGLSubmitPlan> missingCacheSubmitPlanStorage( new idModernGLSubmitPlan );
	idModernGLSubmitPlan &missingCacheSubmitPlan = *missingCacheSubmitPlanStorage;
	missingCacheSubmitPlan.Build( missingCacheDrawPlan );
	const modernGLDrawPlanStats_t &missingDrawStats = missingCacheDrawPlan.Stats();
	const modernGLSubmitPlanStats_t &fallbackStats = missingCacheSubmitPlan.Stats();
	if ( missingDrawStats.sourceDrawPackets
			!= missingCachePacketFrame->NumDrawPackets()
		|| missingDrawStats.plannedDraws != 0
		|| missingDrawStats.geometryFallbackDraws != expectedGeometryFallbackDraws
		|| missingDrawStats.geometryVertexBufferFallbackDraws != expectedGeometryFallbackDraws
		|| fallbackStats.sourcePlanDraws != 0
		|| fallbackStats.readyDraws != 0
		|| fallbackStats.fallbackDraws != 0 ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: early geometry fallback count mismatch\n" );
		return false;
	}

	idAutoPtr<idModernGLDrawPlan> tempIndexDrawPlanStorage( new idModernGLDrawPlan );
	idModernGLDrawPlan &tempIndexDrawPlan = *tempIndexDrawPlanStorage;
	idAutoPtr<idScenePacketFrame> tempIndexPacketFrame(
		new idScenePacketFrame );
	idRenderGraph tempIndexGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, true, TAG_TEMP, false,
		tempIndexDrawPlan, *tempIndexPacketFrame, tempIndexGraph );
	idAutoPtr<idModernGLSubmitPlan> tempIndexSubmitPlanStorage( new idModernGLSubmitPlan );
	idModernGLSubmitPlan &tempIndexSubmitPlan = *tempIndexSubmitPlanStorage;
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

	idAutoPtr<idModernGLDrawPlan> uploadIndexDrawPlanStorage( new idModernGLDrawPlan );
	idModernGLDrawPlan &uploadIndexDrawPlan = *uploadIndexDrawPlanStorage;
	idAutoPtr<idScenePacketFrame> uploadIndexPacketFrame(
		new idScenePacketFrame );
	idRenderGraph uploadIndexGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, false, TAG_FREE, true,
		uploadIndexDrawPlan, *uploadIndexPacketFrame, uploadIndexGraph );
	idAutoPtr<idModernGLSubmitPlan> uploadIndexSubmitPlanStorage( new idModernGLSubmitPlan );
	idModernGLSubmitPlan &uploadIndexSubmitPlan = *uploadIndexSubmitPlanStorage;
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
