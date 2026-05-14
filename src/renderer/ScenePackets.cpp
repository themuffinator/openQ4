// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ScenePackets.h"

static idScenePacketFrame rg_frontEndScenePacketFrame;
static bool rg_frontEndScenePacketFrameOpen = false;

idScenePacketFrame::idScenePacketFrame() {
	Clear();
}

void idScenePacketFrame::Clear( void ) {
	memset( scenes, 0, sizeof( scenes ) );
	memset( passes, 0, sizeof( passes ) );
	memset( drawPackets, 0, sizeof( drawPackets ) );
	memset( materialRecords, 0, sizeof( materialRecords ) );
	memset( geometryRecords, 0, sizeof( geometryRecords ) );
	memset( instanceRecords, 0, sizeof( instanceRecords ) );
	memset( &stats, 0, sizeof( stats ) );
	activeScene = -1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
}

void idScenePacketFrame::SetOverflow( scenePacketOverflowCause_t cause ) {
	stats.overflow = true;
	if ( stats.overflowCause == SCENE_PACKET_OVERFLOW_NONE ) {
		stats.overflowCause = cause;
	}
}

void idScenePacketFrame::CountCategory( scenePacketCategory_t category ) {
	switch ( category ) {
	case SCENE_PACKET_CATEGORY_WORLD:
		stats.worldPackets++;
		break;
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		stats.subviewPackets++;
		break;
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		stats.remoteCameraPackets++;
		break;
	case SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS:
		stats.specialEffectPackets++;
		break;
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		stats.viewmodelPackets++;
		break;
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		stats.renderDemoPackets++;
		break;
	case SCENE_PACKET_CATEGORY_GUI:
		stats.guiPackets++;
		break;
	case SCENE_PACKET_CATEGORY_POST_PROCESS:
		stats.postProcessPackets++;
		break;
	case SCENE_PACKET_CATEGORY_PRESENT:
		stats.presentPackets++;
		break;
	case SCENE_PACKET_CATEGORY_COMMAND:
		stats.commandOnlyPackets++;
		break;
	case SCENE_PACKET_CATEGORY_UNKNOWN:
	default:
		break;
	}
}

static rendererMaterialClass_t R_ScenePackets_MaterialClassForDrawSurf( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return RENDER_MATERIAL_NONE;
	}

	const idMaterial *material = drawSurf->material;
	const float sort = material->GetSort();
	if ( sort == SS_SUBVIEW ) {
		return RENDER_MATERIAL_SUBVIEW;
	}
	if ( sort >= SS_POST_PROCESS ) {
		return RENDER_MATERIAL_POST_PROCESS;
	}
	if ( material->HasGui() || sort == SS_GUI || sort == SS_PREGUI ) {
		return RENDER_MATERIAL_GUI;
	}

	switch ( material->Coverage() ) {
	case MC_OPAQUE:
		return RENDER_MATERIAL_OPAQUE;
	case MC_PERFORATED:
		return RENDER_MATERIAL_PERFORATED;
	case MC_TRANSLUCENT:
		return RENDER_MATERIAL_TRANSLUCENT;
	default:
		break;
	}
	return material->IsDrawn() ? RENDER_MATERIAL_OPAQUE : RENDER_MATERIAL_SHADOW_ONLY;
}

static const idImage *R_ScenePackets_FirstStageImage( const idMaterial *material, stageLighting_t lighting ) {
	if ( material == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->lighting == lighting && stage->texture.image != NULL ) {
			return stage->texture.image;
		}
	}
	return NULL;
}

static const idImage *R_ScenePackets_FirstAmbientImage( const idMaterial *material ) {
	if ( material == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->lighting == SL_AMBIENT && stage->texture.image != NULL ) {
			return stage->texture.image;
		}
	}
	return NULL;
}

static bool R_ScenePackets_DrawSurfUsesSkinning( const drawSurf_t *drawSurf ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	return drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->numSkinToModelTransforms > 0;
#else
	return false;
#endif
}

static bool R_ScenePackets_MaterialUsesRemoteRender( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	for ( int i = 0; i < material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->texture.dynamic == DI_REMOTE_RENDER ) {
			return true;
		}
	}
	return false;
}

static scenePacketCategory_t R_ScenePackets_CategoryForDrawSurf( const viewDef_t *viewDef, const drawSurf_t *drawSurf, renderPassCategory_t passCategory ) {
	if ( passCategory == RENDER_PASS_SPECIAL_EFFECTS ) {
		return SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS;
	}
	if ( passCategory == RENDER_PASS_PRESENT ) {
		return SCENE_PACKET_CATEGORY_PRESENT;
	}
	if ( passCategory == RENDER_PASS_AUTHORED_POST ) {
		return SCENE_PACKET_CATEGORY_POST_PROCESS;
	}
	if ( passCategory == RENDER_PASS_GUI ) {
		return SCENE_PACKET_CATEGORY_GUI;
	}
	if ( viewDef != NULL && viewDef->isSubview ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	if ( viewDef != NULL && viewDef->renderView.viewID < 0 ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	if ( drawSurf == NULL ) {
		return SCENE_PACKET_CATEGORY_COMMAND;
	}
	if ( drawSurf->space != NULL && drawSurf->space->weaponDepthHack ) {
		return SCENE_PACKET_CATEGORY_VIEWMODEL;
	}
	if ( drawSurf->space != NULL && drawSurf->space->entityDef != NULL && drawSurf->space->entityDef->parms.remoteRenderView != NULL ) {
		return SCENE_PACKET_CATEGORY_REMOTE_CAMERA;
	}
	if ( R_ScenePackets_MaterialUsesRemoteRender( drawSurf->material ) ) {
		return SCENE_PACKET_CATEGORY_REMOTE_CAMERA;
	}
	if ( R_ScenePackets_MaterialClassForDrawSurf( drawSurf ) == RENDER_MATERIAL_SUBVIEW ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	return SCENE_PACKET_CATEGORY_WORLD;
}

static scenePacketCategory_t R_ScenePackets_CategoryForCommandPass( renderPassCategory_t passCategory, const viewDef_t *viewDef ) {
	switch ( passCategory ) {
	case RENDER_PASS_SPECIAL_EFFECTS:
		return SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS;
	case RENDER_PASS_AUTHORED_POST:
		return SCENE_PACKET_CATEGORY_POST_PROCESS;
	case RENDER_PASS_GUI:
		return SCENE_PACKET_CATEGORY_GUI;
	case RENDER_PASS_PRESENT:
		return SCENE_PACKET_CATEGORY_PRESENT;
	default:
		break;
	}
	if ( viewDef != NULL && viewDef->isSubview ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	if ( viewDef != NULL && viewDef->renderView.viewID < 0 ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	return SCENE_PACKET_CATEGORY_COMMAND;
}

static void R_ScenePackets_CopyIdentityMatrix( float matrix[16] ) {
	memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

static geometryUploadLifetime_t R_ScenePackets_UploadLifetimeForCache( const vertCache_t *ambientCache, const vertCache_t *indexCache, const srfTriangles_t *geo ) {
	if ( ( ambientCache != NULL && ambientCache->tag == TAG_TEMP ) || ( indexCache != NULL && indexCache->tag == TAG_TEMP ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_FRAME_TEMP;
	}
	if ( geo != NULL && ( geo->deformedSurface || geo->tempAmbientCache ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_DYNAMIC_BRIDGE;
	}
	if ( ambientCache != NULL || indexCache != NULL ) {
		return GEOMETRY_UPLOAD_LIFETIME_STATIC;
	}
	if ( geo != NULL && ( geo->verts != NULL || geo->indexes != NULL ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_CLIENT_MEMORY;
	}
	return GEOMETRY_UPLOAD_LIFETIME_UNKNOWN;
}

static int R_ScenePackets_InstanceVisibilityFlags( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory, bool legacyBridge ) {
	int flags = legacyBridge ? INSTANCE_VISIBILITY_LEGACY_BRIDGE : INSTANCE_VISIBILITY_NONE;
	switch ( packetCategory ) {
	case SCENE_PACKET_CATEGORY_GUI:
		flags |= INSTANCE_VISIBILITY_GUI;
		break;
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		flags |= INSTANCE_VISIBILITY_VIEWMODEL;
		break;
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		flags |= INSTANCE_VISIBILITY_SUBVIEW;
		break;
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		flags |= INSTANCE_VISIBILITY_REMOTE_CAMERA;
		break;
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		flags |= INSTANCE_VISIBILITY_RENDER_DEMO;
		break;
	case SCENE_PACKET_CATEGORY_WORLD:
	default:
		flags |= INSTANCE_VISIBILITY_WORLD;
		break;
	}
	if ( drawSurf != NULL && drawSurf->space != NULL && drawSurf->space->weaponDepthHack ) {
		flags |= INSTANCE_VISIBILITY_VIEWMODEL;
	}
	return flags;
}

static unsigned int R_ScenePackets_LegacySortOrdinal( const drawSurf_t *drawSurf ) {
	float legacySort = drawSurf ? drawSurf->sort : 0.0f;
	unsigned int bits = 0;
	memcpy( &bits, &legacySort, sizeof( bits ) );
	if ( ( bits & 0x80000000u ) != 0 ) {
		bits = ~bits;
	} else {
		bits ^= 0x80000000u;
	}
	return bits;
}

static unsigned long long R_ScenePackets_BuildSortKey( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex ) {
	const unsigned long long categoryBits = static_cast<unsigned long long>( category & 0xff ) << 56;
	const unsigned long long sortBits = static_cast<unsigned long long>( R_ScenePackets_LegacySortOrdinal( drawSurf ) ) << 24;
	const unsigned long long stableIndexBits = static_cast<unsigned int>( drawIndex ) & 0x00ffffffu;
	return categoryBits | sortBits | stableIndexBits;
}

int idScenePacketFrame::FindOrAddMaterialRecord( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return -1;
	}

	const idMaterial *material = drawSurf->material;
	for ( int i = 0; i < stats.materialRecords; ++i ) {
		if ( materialRecords[i].material == material ) {
			return i;
		}
	}

	if ( stats.materialRecords >= SCENE_PACKET_MAX_MATERIAL_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_MATERIALS );
		return -1;
	}

	const int recordIndex = stats.materialRecords++;
	materialResourceRecord_t &record = materialRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.material = material;
	record.diffuseImage = R_ScenePackets_FirstStageImage( material, SL_DIFFUSE );
	if ( record.diffuseImage == NULL ) {
		record.diffuseImage = R_ScenePackets_FirstAmbientImage( material );
	}
	record.normalImage = R_ScenePackets_FirstStageImage( material, SL_BUMP );
	record.specularImage = R_ScenePackets_FirstStageImage( material, SL_SPECULAR );
	record.resourceTableIndex = material->Index();
	record.permutation.materialClass = R_ScenePackets_MaterialClassForDrawSurf( drawSurf );
	record.permutation.lightingMode =
		( material->IsDrawn() ? 1u : 0u ) |
		( material->HasAmbient() ? 2u : 0u ) |
		( material->ReceivesLighting() ? 4u : 0u ) |
		( material->ReceivesFog() ? 8u : 0u );
	record.permutation.shadowMode =
		( material->SurfaceCastsShadow() ? 1u : 0u ) |
		( material->TestMaterialFlag( MF_NOSELFSHADOW ) ? 2u : 0u ) |
		( material->TestMaterialFlag( MF_FORCESHADOWS ) ? 4u : 0u );
	record.permutation.alphaMode = material->Coverage();
	record.permutation.skinningMode = R_ScenePackets_DrawSurfUsesSkinning( drawSurf ) ? 1u : 0u;
	record.permutation.deformMode =
		( drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->deformedSurface ) ? 2u :
		( material->Deform() != DFRM_NONE ? 1u : 0u );
	record.permutation.lightGridMode =
		( material->ReceivesLighting() ? 1u : 0u ) |
		( material->HasAmbient() ? 2u : 0u );
	record.permutation.fogMode = material->ReceivesFog() ? 1u : 0u;
	record.permutation.debugMode = 0u;
	record.permutation.tier = glConfig.rendererTier;
	return recordIndex;
}

int idScenePacketFrame::FindOrAddGeometryRecord( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->geo == NULL ) {
		return -1;
	}

	const srfTriangles_t *geo = drawSurf->geo;
	for ( int i = 0; i < stats.geometryRecords; ++i ) {
		if ( geometryRecords[i].legacyGeometry == geo ) {
			return i;
		}
	}

	if ( stats.geometryRecords >= SCENE_PACKET_MAX_GEOMETRY_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS );
		return -1;
	}

	const int recordIndex = stats.geometryRecords++;
	geometryResourceRecord_t &record = geometryRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.legacyGeometry = geo;
	record.bounds = geo->bounds;
	record.recordIndex = recordIndex;
	record.vertexCount = geo->numVerts;
	record.indexCount = geo->numIndexes;
	record.firstVertex = 0;
	record.firstIndex = 0;
	record.vertexStride = sizeof( idDrawVert );
	record.indexType = GL_INDEX_TYPE;
	record.skinningMode = R_ScenePackets_DrawSurfUsesSkinning( drawSurf ) ? GEOMETRY_SKINNING_GPU_PALETTE : GEOMETRY_SKINNING_NONE;
	record.deformMode = geo->deformedSurface ? GEOMETRY_DEFORM_SURFACE :
		( drawSurf->material != NULL && drawSurf->material->Deform() != DFRM_NONE ? GEOMETRY_DEFORM_MATERIAL : GEOMETRY_DEFORM_NONE );
	record.uploadLifetime = R_ScenePackets_UploadLifetimeForCache( geo->ambientCache, geo->indexCache, geo );
	record.skinningPaletteOffset = 0;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	record.skinningPaletteCount = geo->numSkinToModelTransforms;
	record.hasPrimBatchMesh = geo->primBatchMesh != NULL;
#else
	record.skinningPaletteCount = 0;
	record.hasPrimBatchMesh = false;
#endif
	record.legacyIndexData = geo->indexes;
	record.hasClientIndexData = geo->indexes != NULL;
	record.hasBounds = true;
	if ( geo->ambientCache != NULL ) {
		record.ambientVertexBuffer = geo->ambientCache->vbo;
		record.ambientCacheOffset = geo->ambientCache->offset;
		record.ambientCacheBytes = geo->ambientCache->size;
		record.hasAmbientVertexBuffer = geo->ambientCache->vbo != 0 && !geo->ambientCache->indexBuffer;
	}
	if ( geo->indexCache != NULL ) {
		record.indexBuffer = geo->indexCache->vbo;
		record.indexCacheOffset = geo->indexCache->offset;
		record.indexCacheBytes = geo->indexCache->size;
		record.hasIndexBuffer = geo->indexCache->vbo != 0 && geo->indexCache->indexBuffer;
	}
	return recordIndex;
}

int idScenePacketFrame::FindOrAddInstanceRecord( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory ) {
	if ( drawSurf == NULL || drawSurf->space == NULL ) {
		return -1;
	}

	const viewEntity_t *space = drawSurf->space;
	const float *shaderRegisters = drawSurf->shaderRegisters;
	for ( int i = 0; i < stats.instanceRecords; ++i ) {
		if ( instanceRecords[i].legacySpace == space && instanceRecords[i].legacyShaderRegisters == shaderRegisters ) {
			return i;
		}
	}

	if ( stats.instanceRecords >= SCENE_PACKET_MAX_INSTANCE_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS );
		return -1;
	}

	const int recordIndex = stats.instanceRecords++;
	instanceRecord_t &record = instanceRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.legacySpace = space;
	record.legacyShaderRegisters = shaderRegisters;
	record.recordIndex = recordIndex;
	record.entityIndex = ( space->entityDef != NULL ) ? space->entityDef->index : -1;
	record.shaderRegisterBase = shaderRegisters != NULL ? 0 : -1;
	record.shaderRegisterCount = ( shaderRegisters != NULL && drawSurf->material != NULL ) ? drawSurf->material->GetNumRegisters() : 0;
	record.skinningPaletteOffset = 0;
	record.visibilityFlags = R_ScenePackets_InstanceVisibilityFlags( drawSurf, packetCategory, activeScene >= 0 && scenes[activeScene].legacyBridge );
	memcpy( record.modelMatrix, space->modelMatrix, sizeof( record.modelMatrix ) );
	memcpy( record.previousModelMatrix, space->modelMatrix, sizeof( record.previousModelMatrix ) );
	memcpy( record.modelViewMatrix, space->modelViewMatrix, sizeof( record.modelViewMatrix ) );
	record.hasModelMatrix = true;
	record.hasPreviousModelMatrix = true;
	record.hasShaderRegisters = shaderRegisters != NULL;
	record.legacyBridge = activeScene >= 0 && scenes[activeScene].legacyBridge;
	if ( shaderRegisters != NULL ) {
		record.entityColor[0] = shaderRegisters[EXP_REG_PARM0];
		record.entityColor[1] = shaderRegisters[EXP_REG_PARM1];
		record.entityColor[2] = shaderRegisters[EXP_REG_PARM2];
		record.entityColor[3] = shaderRegisters[EXP_REG_PARM3];
	} else if ( space->entityDef != NULL ) {
		record.entityColor[0] = space->entityDef->parms.shaderParms[SHADERPARM_RED];
		record.entityColor[1] = space->entityDef->parms.shaderParms[SHADERPARM_GREEN];
		record.entityColor[2] = space->entityDef->parms.shaderParms[SHADERPARM_BLUE];
		record.entityColor[3] = space->entityDef->parms.shaderParms[SHADERPARM_ALPHA];
	} else {
		record.entityColor[0] = 1.0f;
		record.entityColor[1] = 1.0f;
		record.entityColor[2] = 1.0f;
		record.entityColor[3] = 1.0f;
	}
	return recordIndex;
}

bool idScenePacketFrame::AddScene( const viewDef_t *viewDef, bool legacyBridge ) {
	if ( stats.scenePackets >= SCENE_PACKET_MAX_SCENES ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_SCENES );
		activeScene = -1;
		activePass = -1;
		return false;
	}

	scenePacket_t &scene = scenes[stats.scenePackets++];
	memset( &scene, 0, sizeof( scene ) );
	scene.viewDef = viewDef;
	scene.packetCategory = viewDef != NULL && viewDef->isSubview ? SCENE_PACKET_CATEGORY_SUBVIEW :
		( viewDef != NULL && viewDef->renderView.viewID < 0 ? SCENE_PACKET_CATEGORY_RENDER_DEMO : SCENE_PACKET_CATEGORY_WORLD );
	scene.firstPassPacket = stats.passPackets;
	scene.firstDrawPacket = stats.drawPackets;
	scene.legacyBridge = legacyBridge;
	activeScene = stats.scenePackets - 1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
	return true;
}

bool idScenePacketFrame::AddPass( renderPassCategory_t category, bool enabled, bool commandOnly ) {
	if ( activeScene < 0 ) {
		if ( !AddScene( NULL, true ) ) {
			return false;
		}
	}
	if ( stats.passPackets >= SCENE_PACKET_MAX_PASSES ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_PASSES );
		activePass = -1;
		return false;
	}

	passPacket_t &pass = passes[stats.passPackets++];
	memset( &pass, 0, sizeof( pass ) );
	pass.passCategory = category;
	pass.packetCategory = R_ScenePackets_CategoryForCommandPass( category, activeScene >= 0 ? scenes[activeScene].viewDef : NULL );
	pass.firstDrawPacket = stats.drawPackets;
	pass.enabled = enabled;
	pass.commandOnly = commandOnly;
	activePass = stats.passPackets - 1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
	scenes[activeScene].passPacketCount++;
	if ( commandOnly ) {
		CountCategory( pass.packetCategory );
	}
	return true;
}

bool idScenePacketFrame::AddDrawPacket( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex ) {
	if ( activePass < 0 ) {
		if ( !AddPass( category, true ) ) {
			return false;
		}
	}
	if ( stats.drawPackets >= SCENE_PACKET_MAX_DRAWS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_DRAWS );
		stats.clippedDrawPackets++;
		return false;
	}

	drawPacket_t &packet = drawPackets[stats.drawPackets++];
	memset( &packet, 0, sizeof( packet ) );
	const int materialRecordIndex = FindOrAddMaterialRecord( drawSurf );
	const scenePacketCategory_t packetCategory = R_ScenePackets_CategoryForDrawSurf( activeScene >= 0 ? scenes[activeScene].viewDef : NULL, drawSurf, category );
	const int geometryRecordIndex = FindOrAddGeometryRecord( drawSurf );
	const int instanceRecordIndex = FindOrAddInstanceRecord( drawSurf, packetCategory );
	packet.legacyDrawSurf = drawSurf;
	packet.viewDef = activeScene >= 0 ? scenes[activeScene].viewDef : NULL;
	packet.space = drawSurf ? drawSurf->space : NULL;
	packet.materialRecord = materialRecordIndex >= 0 ? &materialRecords[materialRecordIndex] : NULL;
	packet.geometryRecord = geometryRecordIndex >= 0 ? &geometryRecords[geometryRecordIndex] : NULL;
	packet.instanceRecord = instanceRecordIndex >= 0 ? &instanceRecords[instanceRecordIndex] : NULL;
	packet.sortKey.value = R_ScenePackets_BuildSortKey( drawSurf, category, drawIndex );
	packet.passCategory = category;
	packet.packetCategory = packetCategory;
	packet.legacySort = drawSurf ? drawSurf->sort : 0.0f;
	packet.materialRecordIndex = materialRecordIndex;
	packet.geometryRecordIndex = geometryRecordIndex;
	packet.instanceRecordIndex = instanceRecordIndex;
	packet.vertexCount = ( drawSurf && drawSurf->geo ) ? drawSurf->geo->numVerts : 0;
	packet.firstIndex = 0;
	packet.indexCount = ( drawSurf && drawSurf->geo ) ? drawSurf->geo->numIndexes : 0;
	packet.vertexOffset = 0;
	packet.instanceOffset = 0;
	packet.instanceCount = drawSurf ? 1 : 0;
	if ( drawSurf != NULL ) {
		packet.scissorX1 = drawSurf->scissorRect.x1;
		packet.scissorY1 = drawSurf->scissorRect.y1;
		packet.scissorX2 = drawSurf->scissorRect.x2;
		packet.scissorY2 = drawSurf->scissorRect.y2;
	}
	packet.hasGeometry = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->numIndexes > 0;
	packet.hasShaderRegisters = drawSurf != NULL && drawSurf->shaderRegisters != NULL;
	packet.hasIndexCache = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->indexCache != NULL;
	packet.hasAmbientCache = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->ambientCache != NULL;

	if ( activePassSortKeyValid && packet.sortKey.value < activePassLastSortKey ) {
		stats.sortKeyValidationFailures++;
	}
	activePassLastSortKey = packet.sortKey.value;
	activePassSortKeyValid = true;
	passes[activePass].packetCategory = packetCategory;
	passes[activePass].drawPacketCount++;
	scenes[activeScene].drawPacketCount++;
	CountCategory( packetCategory );
	if ( drawSurf != NULL && drawSurf->material != NULL ) {
		stats.drawPacketsWithMaterial++;
	}
	if ( packet.materialRecord != NULL ) {
		stats.drawPacketsWithResourceRecord++;
	}
	if ( packet.geometryRecord != NULL ) {
		stats.drawPacketsWithGeometryRecord++;
	}
	if ( packet.instanceRecord != NULL ) {
		stats.drawPacketsWithInstanceRecord++;
	}
	if ( packet.hasGeometry ) {
		stats.drawPacketsWithGeometry++;
	}
	if ( packet.hasShaderRegisters ) {
		stats.drawPacketsWithShaderRegisters++;
	}
	if ( packet.hasIndexCache ) {
		stats.drawPacketsWithIndexCache++;
	}
	if ( packet.hasAmbientCache ) {
		stats.drawPacketsWithAmbientCache++;
	}
	return true;
}

void idScenePacketFrame::FinishScene( void ) {
	activeScene = -1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
}

void idScenePacketFrame::AddCommandPacket( scenePacketCategory_t category ) {
	stats.commandPackets++;
	CountCategory( category );
}

void idScenePacketFrame::AddLegacyDrawView( void ) {
	stats.legacyDrawViews++;
}

void idScenePacketFrame::AddClippedDrawPackets( int count ) {
	if ( count > 0 ) {
		stats.clippedDrawPackets += count;
		SetOverflow( SCENE_PACKET_OVERFLOW_DRAWS );
	}
}

void idScenePacketFrame::MarkFrontEndDerived( void ) {
	stats.frontEndDerived = true;
}

void idScenePacketFrame::MarkBackendDerived( void ) {
	stats.backendDerived = true;
}

int idScenePacketFrame::NumScenes( void ) const {
	return stats.scenePackets;
}

int idScenePacketFrame::NumPasses( void ) const {
	return stats.passPackets;
}

int idScenePacketFrame::NumDrawPackets( void ) const {
	return stats.drawPackets;
}

int idScenePacketFrame::NumMaterialRecords( void ) const {
	return stats.materialRecords;
}

int idScenePacketFrame::NumGeometryRecords( void ) const {
	return stats.geometryRecords;
}

int idScenePacketFrame::NumInstanceRecords( void ) const {
	return stats.instanceRecords;
}

const scenePacket_t &idScenePacketFrame::Scene( int index ) const {
	return scenes[index];
}

const passPacket_t &idScenePacketFrame::Pass( int index ) const {
	return passes[index];
}

const drawPacket_t &idScenePacketFrame::DrawPacket( int index ) const {
	return drawPackets[index];
}

const materialResourceRecord_t &idScenePacketFrame::MaterialRecord( int index ) const {
	return materialRecords[index];
}

const geometryResourceRecord_t &idScenePacketFrame::GeometryRecord( int index ) const {
	return geometryRecords[index];
}

const instanceRecord_t &idScenePacketFrame::InstanceRecord( int index ) const {
	return instanceRecords[index];
}

const scenePacketFrameStats_t &idScenePacketFrame::Stats( void ) const {
	return stats;
}

bool idScenePacketFrame::ValidateSortKeys( void ) const {
	for ( int passIndex = 0; passIndex < stats.passPackets; ++passIndex ) {
		const passPacket_t &pass = passes[passIndex];
		unsigned long long previous = 0;
		bool havePrevious = false;
		for ( int drawIndex = 0; drawIndex < pass.drawPacketCount; ++drawIndex ) {
			const int packetIndex = pass.firstDrawPacket + drawIndex;
			if ( packetIndex < 0 || packetIndex >= stats.drawPackets ) {
				return false;
			}
			const drawPacket_t &packet = drawPackets[packetIndex];
			if ( packet.passCategory != pass.passCategory ) {
				return false;
			}
			if ( havePrevious && packet.sortKey.value < previous ) {
				return false;
			}
			previous = packet.sortKey.value;
			havePrevious = true;
		}
	}
	return stats.sortKeyValidationFailures == 0;
}

const char *RenderPassCategory_Name( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "depth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "stencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "shadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "arb2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "lightGrid";
	case RENDER_PASS_AMBIENT:
		return "ambient";
	case RENDER_PASS_DEFERRED_RESOLVE:
		return "deferredResolve";
	case RENDER_PASS_FORWARD_PLUS:
		return "forwardPlus";
	case RENDER_PASS_FOG_BLEND:
		return "fogBlend";
	case RENDER_PASS_SSAO:
		return "ssao";
	case RENDER_PASS_MOTION_BLUR:
		return "motionBlur";
	case RENDER_PASS_LENS_FLARE:
		return "lensFlare";
	case RENDER_PASS_BLOOM:
		return "bloom";
	case RENDER_PASS_AUTHORED_POST:
		return "authoredPost";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "specialEffects";
	case RENDER_PASS_GUI:
		return "gui";
	case RENDER_PASS_PRESENT:
		return "present";
	default:
		return "unknown";
	}
}

const char *ScenePacketCategory_Name( scenePacketCategory_t category ) {
	switch ( category ) {
	case SCENE_PACKET_CATEGORY_WORLD:
		return "world";
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		return "subview";
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		return "remoteCamera";
	case SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS:
		return "specialEffects";
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		return "viewmodel";
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		return "renderDemo";
	case SCENE_PACKET_CATEGORY_GUI:
		return "gui";
	case SCENE_PACKET_CATEGORY_POST_PROCESS:
		return "postProcess";
	case SCENE_PACKET_CATEGORY_PRESENT:
		return "present";
	case SCENE_PACKET_CATEGORY_COMMAND:
		return "command";
	case SCENE_PACKET_CATEGORY_UNKNOWN:
	default:
		return "unknown";
	}
}

const char *ScenePacketOverflowCause_Name( scenePacketOverflowCause_t cause ) {
	switch ( cause ) {
	case SCENE_PACKET_OVERFLOW_NONE:
		return "none";
	case SCENE_PACKET_OVERFLOW_SCENES:
		return "scenes";
	case SCENE_PACKET_OVERFLOW_PASSES:
		return "passes";
	case SCENE_PACKET_OVERFLOW_DRAWS:
		return "draws";
	case SCENE_PACKET_OVERFLOW_MATERIALS:
		return "materials";
	case SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS:
		return "geometryRecords";
	case SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS:
		return "instanceRecords";
	default:
		return "unknown";
	}
}

const char *RendererMaterialClass_Name( rendererMaterialClass_t materialClass ) {
	switch ( materialClass ) {
	case RENDER_MATERIAL_NONE:
		return "none";
	case RENDER_MATERIAL_SHADOW_ONLY:
		return "shadowOnly";
	case RENDER_MATERIAL_OPAQUE:
		return "opaque";
	case RENDER_MATERIAL_PERFORATED:
		return "perforated";
	case RENDER_MATERIAL_TRANSLUCENT:
		return "translucent";
	case RENDER_MATERIAL_GUI:
		return "gui";
	case RENDER_MATERIAL_SUBVIEW:
		return "subview";
	case RENDER_MATERIAL_POST_PROCESS:
		return "postProcess";
	default:
		return "unknown";
	}
}

static void R_ScenePackets_EnsureFrontEndFrame( void ) {
	if ( !rg_frontEndScenePacketFrameOpen ) {
		rg_frontEndScenePacketFrame.Clear();
		rg_frontEndScenePacketFrameOpen = true;
	}
	rg_frontEndScenePacketFrame.MarkFrontEndDerived();
}

void R_ScenePackets_BeginFrame( void ) {
	rg_frontEndScenePacketFrame.Clear();
	rg_frontEndScenePacketFrame.MarkFrontEndDerived();
	rg_frontEndScenePacketFrameOpen = true;
}

void R_ScenePackets_EndFrame( void ) {
	rg_frontEndScenePacketFrame.Clear();
	rg_frontEndScenePacketFrameOpen = false;
}

static void R_ScenePackets_AddDrawSurfPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef, renderPassCategory_t category ) {
	if ( !packetFrame.AddPass( category, true ) ) {
		return;
	}
	if ( viewDef == NULL || viewDef->drawSurfs == NULL || viewDef->numDrawSurfs <= 0 ) {
		return;
	}

	for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
		if ( !packetFrame.AddDrawPacket( viewDef->drawSurfs[i], category, i ) ) {
			packetFrame.AddClippedDrawPackets( viewDef->numDrawSurfs - i - 1 );
			break;
		}
	}
}

static void R_ScenePackets_AddDrawView( idScenePacketFrame &packetFrame, const viewDef_t *viewDef, bool legacyBridge ) {
	packetFrame.AddLegacyDrawView();
	if ( !packetFrame.AddScene( viewDef, legacyBridge ) ) {
		return;
	}

	const bool worldView = viewDef != NULL && viewDef->viewEntitys != NULL;
	if ( worldView ) {
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_DEPTH );
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_ARB2_INTERACTION );
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_LIGHT_GRID );
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_AMBIENT );
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_FOG_BLEND );
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_AUTHORED_POST );
	} else {
		R_ScenePackets_AddDrawSurfPass( packetFrame, viewDef, RENDER_PASS_GUI );
	}

	packetFrame.FinishScene();
}

static void R_ScenePackets_AddCommandPass( idScenePacketFrame &packetFrame, renderPassCategory_t category, const viewDef_t *viewDef, bool legacyBridge ) {
	packetFrame.AddCommandPacket( SCENE_PACKET_CATEGORY_UNKNOWN );
	if ( !packetFrame.AddScene( viewDef, legacyBridge ) ) {
		return;
	}
	packetFrame.AddPass( category, true, true );
	packetFrame.FinishScene();
}

void R_ScenePackets_AddRenderView( const viewDef_t *viewDef ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddDrawView( rg_frontEndScenePacketFrame, viewDef, false );
}

void R_ScenePackets_AddSpecialEffects( const viewDef_t *viewDef ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_SPECIAL_EFFECTS, viewDef, false );
}

void R_ScenePackets_AddRenderTargetOp( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_AUTHORED_POST, NULL, false );
}

void R_ScenePackets_AddCopyRender( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_AUTHORED_POST, NULL, false );
}

void R_ScenePackets_AddPresent( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_PRESENT, NULL, false );
}

void R_ScenePackets_AddCommandOnly( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	rg_frontEndScenePacketFrame.AddCommandPacket();
}

const idScenePacketFrame &R_ScenePackets_FrontEndFrame( void ) {
	return rg_frontEndScenePacketFrame;
}

bool R_ScenePackets_FrontEndFrameAvailable( void ) {
	const scenePacketFrameStats_t &stats = rg_frontEndScenePacketFrame.Stats();
	return rg_frontEndScenePacketFrameOpen
		&& stats.frontEndDerived
		&& ( stats.scenePackets > 0 || stats.passPackets > 0 || stats.drawPackets > 0 || stats.commandPackets > 0 );
}

void R_ScenePackets_BuildLegacyCommandStream( const emptyCommand_t *cmds, idScenePacketFrame &packetFrame ) {
	packetFrame.Clear();
	packetFrame.MarkBackendDerived();

	for ( const emptyCommand_t *cmd = cmds; cmd != NULL; cmd = reinterpret_cast<const emptyCommand_t *>( cmd->next ) ) {
		switch ( cmd->commandId ) {
		case RC_DRAW_VIEW:
			R_ScenePackets_AddDrawView( packetFrame, reinterpret_cast<const drawSurfsCommand_t *>( cmd )->viewDef, true );
			break;
		case RC_DRAW_SPECIAL_EFFECTS:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_SPECIAL_EFFECTS, reinterpret_cast<const drawSurfsCommand_t *>( cmd )->viewDef, true );
			break;
		case RC_SET_RENDERTEXTURE:
		case RC_RESOLVE_MSAA:
		case RC_CLEAR_RENDERTARGET:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_AUTHORED_POST, NULL, true );
			break;
		case RC_COPY_RENDER:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_AUTHORED_POST, NULL, true );
			break;
		case RC_SET_BUFFER:
			packetFrame.AddCommandPacket();
			break;
		case RC_SWAP_BUFFERS:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_PRESENT, NULL, true );
			break;
		default:
			break;
		}
	}
}

void R_ScenePackets_LogIfVerbose( const idScenePacketFrame &packetFrame ) {
	if ( r_rendererMetrics.GetInteger() < 2 ) {
		return;
	}

	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	common->Printf(
		"scenePackets source=%s scenes=%d passes=%d draws=%d clipped=%d cmds=%d drawViews=%d materials=%d geometryRecords=%d instanceRecords=%d withMaterial=%d resources=%d geometryRecordRefs=%d instanceRefs=%d geometry=%d regs=%d indexCache=%d ambientCache=%d categories(world=%d subview=%d remote=%d fx=%d viewmodel=%d demo=%d gui=%d post=%d present=%d command=%d) sortFailures=%d overflow=%d cause=%s\n",
		stats.frontEndDerived ? "frontend" : ( stats.backendDerived ? "backend" : "unknown" ),
		stats.scenePackets,
		stats.passPackets,
		stats.drawPackets,
		stats.clippedDrawPackets,
		stats.commandPackets,
		stats.legacyDrawViews,
		stats.materialRecords,
		stats.geometryRecords,
		stats.instanceRecords,
		stats.drawPacketsWithMaterial,
		stats.drawPacketsWithResourceRecord,
		stats.drawPacketsWithGeometryRecord,
		stats.drawPacketsWithInstanceRecord,
		stats.drawPacketsWithGeometry,
		stats.drawPacketsWithShaderRegisters,
		stats.drawPacketsWithIndexCache,
		stats.drawPacketsWithAmbientCache,
		stats.worldPackets,
		stats.subviewPackets,
		stats.remoteCameraPackets,
		stats.specialEffectPackets,
		stats.viewmodelPackets,
		stats.renderDemoPackets,
		stats.guiPackets,
		stats.postProcessPackets,
		stats.presentPackets,
		stats.commandOnlyPackets,
		stats.sortKeyValidationFailures,
		stats.overflow ? 1 : 0,
		ScenePacketOverflowCause_Name( stats.overflowCause ) );
	for ( int i = 0; i < packetFrame.NumPasses(); ++i ) {
		const passPacket_t &pass = packetFrame.Pass( i );
		common->Printf(
			"scenePackets pass[%d]=%s category=%s draws=%d enabled=%d\n",
			i,
			RenderPassCategory_Name( pass.passCategory ),
			ScenePacketCategory_Name( pass.packetCategory ),
			pass.drawPacketCount,
			pass.enabled ? 1 : 0 );
	}
	for ( int i = 0; i < packetFrame.NumMaterialRecords() && i < 8; ++i ) {
		const materialResourceRecord_t &record = packetFrame.MaterialRecord( i );
		common->Printf(
			"scenePackets material[%d]=%s class=%s diffuse=%s normal=%s specular=%s table=%d\n",
			i,
			record.material ? record.material->GetName() : "<null>",
			RendererMaterialClass_Name( static_cast<rendererMaterialClass_t>( record.permutation.materialClass ) ),
			record.diffuseImage ? record.diffuseImage->GetName() : "<none>",
			record.normalImage ? record.normalImage->GetName() : "<none>",
			record.specularImage ? record.specularImage->GetName() : "<none>",
			record.resourceTableIndex );
	}
	for ( int i = 0; i < packetFrame.NumGeometryRecords() && i < 8; ++i ) {
		const geometryResourceRecord_t &record = packetFrame.GeometryRecord( i );
		common->Printf(
			"scenePackets geometry[%d]=verts:%d indexes:%d vbo:%d@%d ibo:%d@%d lifetime=%d skin=%d deform=%d bounds=%s\n",
			i,
			record.vertexCount,
			record.indexCount,
			record.ambientVertexBuffer,
			record.ambientCacheOffset,
			record.indexBuffer,
			record.indexCacheOffset,
			record.uploadLifetime,
			record.skinningMode,
			record.deformMode,
			record.hasBounds ? "yes" : "no" );
	}
	for ( int i = 0; i < packetFrame.NumInstanceRecords() && i < 8; ++i ) {
		const instanceRecord_t &record = packetFrame.InstanceRecord( i );
		common->Printf(
			"scenePackets instance[%d]=entity:%d flags=0x%x regs=%d+%d color=(%.2f %.2f %.2f %.2f) legacy=%d\n",
			i,
			record.entityIndex,
			record.visibilityFlags,
			record.shaderRegisterBase,
			record.shaderRegisterCount,
			record.entityColor[0],
			record.entityColor[1],
			record.entityColor[2],
			record.entityColor[3],
			record.legacyBridge ? 1 : 0 );
	}
}

bool RendererScenePacket_RunSelfTest( void ) {
	srfTriangles_t geo;
	memset( &geo, 0, sizeof( geo ) );
	geo.numVerts = 3;
	geo.numIndexes = 6;
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	drawSurfs[0].geo = &geo;
	drawSurfs[1].geo = &geo;
	float shaderRegisters[MAX_EXPRESSION_REGISTERS];
	memset( shaderRegisters, 0, sizeof( shaderRegisters ) );
	shaderRegisters[EXP_REG_PARM0] = 0.25f;
	shaderRegisters[EXP_REG_PARM1] = 0.50f;
	shaderRegisters[EXP_REG_PARM2] = 0.75f;
	shaderRegisters[EXP_REG_PARM3] = 1.00f;
	if ( tr.defaultMaterial != NULL ) {
		drawSurfs[0].material = tr.defaultMaterial;
		drawSurfs[0].sort = tr.defaultMaterial->GetSort();
		drawSurfs[0].shaderRegisters = shaderRegisters;
		drawSurfs[1].material = tr.defaultMaterial;
		drawSurfs[1].sort = tr.defaultMaterial->GetSort() + 0.000001f;
		drawSurfs[1].shaderRegisters = shaderRegisters;
	}
	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	R_ScenePackets_CopyIdentityMatrix( viewEntity.modelMatrix );
	R_ScenePackets_CopyIdentityMatrix( viewEntity.modelViewMatrix );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	R_ScenePackets_CopyIdentityMatrix( worldView.projectionMatrix );
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
	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	const int expectedMaterialRecords = tr.defaultMaterial != NULL ? 1 : 0;
	const int expectedDrawsWithMaterial = tr.defaultMaterial != NULL ? 12 : 0;
	if ( stats.scenePackets != 3 || stats.passPackets != 8 || stats.drawPackets != 12 || stats.legacyDrawViews != 1 || stats.commandPackets != 2 || stats.materialRecords != expectedMaterialRecords || stats.geometryRecords != 1 || stats.instanceRecords != 1 || stats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || stats.drawPacketsWithGeometryRecord != 12 || stats.drawPacketsWithInstanceRecord != 12 || stats.drawPacketsWithGeometry != 12 || stats.worldPackets != 10 || stats.postProcessPackets != 2 || stats.specialEffectPackets != 1 || stats.presentPackets != 1 || stats.sortKeyValidationFailures != 0 || !stats.backendDerived || stats.frontEndDerived || stats.overflow || !packetFrame.ValidateSortKeys() ) {
		common->Printf(
			"RendererScenePacket self-test failed: scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d geometryRecords=%d instances=%d resources=%d geometryRefs=%d instanceRefs=%d geometry=%d world=%d post=%d special=%d present=%d sortFailures=%d overflow=%d cause=%s\n",
			stats.scenePackets,
			stats.passPackets,
			stats.drawPackets,
			stats.legacyDrawViews,
			stats.commandPackets,
			stats.materialRecords,
			stats.geometryRecords,
			stats.instanceRecords,
			stats.drawPacketsWithResourceRecord,
			stats.drawPacketsWithGeometryRecord,
			stats.drawPacketsWithInstanceRecord,
			stats.drawPacketsWithGeometry,
			stats.worldPackets,
			stats.postProcessPackets,
			stats.specialEffectPackets,
			stats.presentPackets,
			stats.sortKeyValidationFailures,
			stats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( stats.overflowCause ) );
		return false;
	}
	if ( expectedMaterialRecords > 0 ) {
		const drawPacket_t &packet = packetFrame.DrawPacket( 0 );
		if ( packet.materialRecord == NULL || packet.materialRecord->material != tr.defaultMaterial || packet.materialRecordIndex != 0 || packet.geometryRecord == NULL || packet.geometryRecordIndex != 0 || packet.instanceRecord == NULL || packet.instanceRecordIndex != 0 || packet.indexCount != 6 || packet.vertexCount != 3 ) {
			common->Printf(
				"RendererScenePacket self-test failed: bad packet records material=%d geometry=%d instance=%d indexCount=%d vertexCount=%d\n",
				packet.materialRecordIndex,
				packet.geometryRecordIndex,
				packet.instanceRecordIndex,
				packet.indexCount,
				packet.vertexCount );
			return false;
		}
		const instanceRecord_t &instance = packetFrame.InstanceRecord( packet.instanceRecordIndex );
		if ( instance.entityColor[0] != 0.25f || instance.entityColor[1] != 0.50f || instance.entityColor[2] != 0.75f || instance.entityColor[3] != 1.00f || !instance.hasShaderRegisters ) {
			common->Printf( "RendererScenePacket self-test failed: instance color/register capture mismatch\n" );
			return false;
		}
	}

	R_ScenePackets_BeginFrame();
	R_ScenePackets_AddRenderView( &worldView );
	R_ScenePackets_AddSpecialEffects( &worldView );
	R_ScenePackets_AddPresent();
	const idScenePacketFrame &frontEndPacketFrame = R_ScenePackets_FrontEndFrame();
	const scenePacketFrameStats_t &frontEndStats = frontEndPacketFrame.Stats();
	if ( frontEndStats.scenePackets != 3 || frontEndStats.passPackets != 8 || frontEndStats.drawPackets != 12 || frontEndStats.legacyDrawViews != 1 || frontEndStats.commandPackets != 2 || frontEndStats.materialRecords != expectedMaterialRecords || frontEndStats.geometryRecords != 1 || frontEndStats.instanceRecords != 1 || frontEndStats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || frontEndStats.drawPacketsWithGeometryRecord != 12 || frontEndStats.drawPacketsWithInstanceRecord != 12 || frontEndStats.drawPacketsWithGeometry != 12 || frontEndStats.worldPackets != 10 || frontEndStats.postProcessPackets != 2 || frontEndStats.specialEffectPackets != 1 || frontEndStats.presentPackets != 1 || frontEndStats.sortKeyValidationFailures != 0 || !frontEndStats.frontEndDerived || frontEndStats.backendDerived || frontEndStats.overflow || !frontEndPacketFrame.ValidateSortKeys() ) {
		common->Printf(
			"RendererScenePacket self-test failed: frontend scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d geometryRecords=%d instances=%d resources=%d geometryRefs=%d instanceRefs=%d geometry=%d source(frontend=%d backend=%d) world=%d post=%d special=%d present=%d sortFailures=%d overflow=%d cause=%s\n",
			frontEndStats.scenePackets,
			frontEndStats.passPackets,
			frontEndStats.drawPackets,
			frontEndStats.legacyDrawViews,
			frontEndStats.commandPackets,
			frontEndStats.materialRecords,
			frontEndStats.geometryRecords,
			frontEndStats.instanceRecords,
			frontEndStats.drawPacketsWithResourceRecord,
			frontEndStats.drawPacketsWithGeometryRecord,
			frontEndStats.drawPacketsWithInstanceRecord,
			frontEndStats.drawPacketsWithGeometry,
			frontEndStats.frontEndDerived ? 1 : 0,
			frontEndStats.backendDerived ? 1 : 0,
			frontEndStats.worldPackets,
			frontEndStats.postProcessPackets,
			frontEndStats.specialEffectPackets,
			frontEndStats.presentPackets,
			frontEndStats.sortKeyValidationFailures,
			frontEndStats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( frontEndStats.overflowCause ) );
		R_ScenePackets_EndFrame();
		return false;
	}
	if ( frontEndPacketFrame.NumScenes() > 0 && frontEndPacketFrame.Scene( 0 ).legacyBridge ) {
		common->Printf( "RendererScenePacket self-test failed: frontend scene marked as legacy bridge\n" );
		R_ScenePackets_EndFrame();
		return false;
	}
	R_ScenePackets_EndFrame();

	common->Printf( "RendererScenePacket self-test passed (backend and frontend)\n" );
	return true;
}
