// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GeometryResources.h"

static void R_GeometryResources_InitMatrix( float matrix[16] ) {
	memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

static void R_GeometryResources_InitCache( vertCache_t &cache, unsigned int vbo, int offset, int size, bool indexBuffer, int tag = TAG_USED ) {
	memset( &cache, 0, sizeof( cache ) );
	cache.vbo = vbo;
	cache.offset = offset;
	cache.size = size;
	cache.indexBuffer = indexBuffer;
	cache.tag = tag;
}

static bool R_GeometryResources_BuildSingleDrawPacket(
	idScenePacketFrame &packetFrame,
	drawSurf_t &drawSurf,
	srfTriangles_t &geometry,
	viewEntity_t &viewEntity,
	viewDef_t &viewDef,
	bool viewmodel,
	bool subview,
	bool renderDemo,
	idRenderEntityLocal *entityDef ) {
	memset( &drawSurf, 0, sizeof( drawSurf ) );
	memset( &geometry, 0, sizeof( geometry ) );
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	memset( &viewDef, 0, sizeof( viewDef ) );

	static glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 1 };
	static float shaderRegisters[MAX_EXPRESSION_REGISTERS];
	memset( shaderRegisters, 0, sizeof( shaderRegisters ) );
	shaderRegisters[EXP_REG_PARM0] = 0.20f;
	shaderRegisters[EXP_REG_PARM1] = 0.40f;
	shaderRegisters[EXP_REG_PARM2] = 0.60f;
	shaderRegisters[EXP_REG_PARM3] = 0.80f;

	static vertCache_t ambientCache;
	static vertCache_t indexCache;
	R_GeometryResources_InitCache( ambientCache, 101, 64, 3 * static_cast<int>( sizeof( idDrawVert ) ), false );
	R_GeometryResources_InitCache( indexCache, 202, 128, 6 * static_cast<int>( sizeof( glIndex_t ) ), true );

	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	geometry.indexes = indexes;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	geometry.bounds.Clear();
	geometry.bounds.AddPoint( idVec3( -1.0f, -2.0f, -3.0f ) );
	geometry.bounds.AddPoint( idVec3( 1.0f, 2.0f, 3.0f ) );

	viewEntity.weaponDepthHack = viewmodel;
	viewEntity.entityDef = entityDef;
	R_GeometryResources_InitMatrix( viewEntity.modelMatrix );
	R_GeometryResources_InitMatrix( viewEntity.modelViewMatrix );
	viewEntity.modelMatrix[12] = 4.0f;
	viewEntity.modelViewMatrix[13] = 5.0f;

	drawSurf.geo = &geometry;
	drawSurf.space = &viewEntity;
	drawSurf.material = tr.defaultMaterial;
	drawSurf.sort = tr.defaultMaterial != NULL ? tr.defaultMaterial->GetSort() : 0.0f;
	drawSurf.shaderRegisters = shaderRegisters;

	drawSurf_t *drawSurfPtrs[1] = { &drawSurf };
	R_GeometryResources_InitMatrix( viewDef.projectionMatrix );
	viewDef.viewEntitys = &viewEntity;
	viewDef.drawSurfs = drawSurfPtrs;
	viewDef.numDrawSurfs = 1;
	viewDef.isSubview = subview;
	viewDef.renderView.viewID = renderDemo ? -1 : 1;

	packetFrame.Clear();
	packetFrame.MarkBackendDerived();
	if ( !packetFrame.AddScene( &viewDef, true ) ) {
		return false;
	}
	if ( !packetFrame.AddPass( RENDER_PASS_DEPTH, true ) ) {
		return false;
	}
	if ( !packetFrame.AddDrawPacket( &drawSurf, RENDER_PASS_DEPTH, 0 ) ) {
		return false;
	}
	packetFrame.FinishScene();
	return true;
}

static bool R_GeometryResources_RunRecordSelfTest( void ) {
	idScenePacketFrame packetFrame;
	drawSurf_t drawSurf;
	srfTriangles_t geometry;
	viewEntity_t viewEntity;
	viewDef_t viewDef;
	if ( !R_GeometryResources_BuildSingleDrawPacket( packetFrame, drawSurf, geometry, viewEntity, viewDef, true, false, false, NULL ) ) {
		common->Printf( "RendererGeometryResource self-test failed: packet build failed\n" );
		return false;
	}

	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	if ( stats.geometryRecords != 1 || stats.instanceRecords != 1 || stats.drawPacketsWithGeometryRecord != 1 || stats.drawPacketsWithInstanceRecord != 1 || stats.viewmodelPackets != 1 || stats.overflow || !packetFrame.ValidateSortKeys() ) {
		common->Printf(
			"RendererGeometryResource self-test failed: record stats geom=%d inst=%d geomRefs=%d instRefs=%d viewmodel=%d overflow=%d cause=%s sort=%d\n",
			stats.geometryRecords,
			stats.instanceRecords,
			stats.drawPacketsWithGeometryRecord,
			stats.drawPacketsWithInstanceRecord,
			stats.viewmodelPackets,
			stats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( stats.overflowCause ),
			stats.sortKeyValidationFailures );
		return false;
	}

	const drawPacket_t &draw = packetFrame.DrawPacket( 0 );
	const geometryResourceRecord_t &geo = packetFrame.GeometryRecord( draw.geometryRecordIndex );
	const instanceRecord_t &instance = packetFrame.InstanceRecord( draw.instanceRecordIndex );
	if ( geo.vertexCount != 3 || geo.indexCount != 6 || geo.ambientVertexBuffer != 101 || geo.indexBuffer != 202 || geo.ambientCacheOffset != 64 || geo.indexCacheOffset != 128 || geo.vertexStride != static_cast<int>( sizeof( idDrawVert ) ) || geo.indexType != GL_INDEX_TYPE || !geo.hasAmbientVertexBuffer || !geo.hasIndexBuffer || !geo.hasClientIndexData || geo.uploadLifetime != GEOMETRY_UPLOAD_LIFETIME_STATIC || geo.fallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE || geo.fallbackFlags != 0 ) {
		common->Printf( "RendererGeometryResource self-test failed: geometry record mismatch\n" );
		return false;
	}
	if ( instance.modelMatrix[12] != 4.0f || instance.modelViewMatrix[13] != 5.0f || instance.entityColor[0] != 0.20f || instance.entityColor[1] != 0.40f || instance.entityColor[2] != 0.60f || instance.entityColor[3] != 0.80f || ( instance.visibilityFlags & INSTANCE_VISIBILITY_VIEWMODEL ) == 0 || !instance.weaponDepthHack || instance.modelDepthHack != 0.0f || instance.negativeScale || !instance.hasShaderRegisters || !instance.legacyBridge ) {
		common->Printf( "RendererGeometryResource self-test failed: instance record mismatch\n" );
		return false;
	}
	return true;
}

static bool R_GeometryResources_RunCategorySelfTest( void ) {
	idScenePacketFrame packetFrame;
	drawSurf_t drawSurf;
	srfTriangles_t geometry;
	viewEntity_t viewEntity;
	viewDef_t viewDef;

	if ( !R_GeometryResources_BuildSingleDrawPacket( packetFrame, drawSurf, geometry, viewEntity, viewDef, false, true, false, NULL ) || packetFrame.Stats().subviewPackets != 1 ) {
		common->Printf( "RendererGeometryResource self-test failed: subview category mismatch\n" );
		return false;
	}

	idRenderEntityLocal remoteEntity;
	renderView_t remoteView;
	memset( &remoteView, 0, sizeof( remoteView ) );
	remoteEntity.parms.remoteRenderView = &remoteView;
	if ( !R_GeometryResources_BuildSingleDrawPacket( packetFrame, drawSurf, geometry, viewEntity, viewDef, false, false, false, &remoteEntity ) || packetFrame.Stats().remoteCameraPackets != 1 ) {
		common->Printf( "RendererGeometryResource self-test failed: remote-camera category mismatch\n" );
		return false;
	}

	if ( !R_GeometryResources_BuildSingleDrawPacket( packetFrame, drawSurf, geometry, viewEntity, viewDef, false, false, true, NULL ) || packetFrame.Stats().renderDemoPackets != 1 ) {
		common->Printf( "RendererGeometryResource self-test failed: render-demo category mismatch\n" );
		return false;
	}

	packetFrame.Clear();
	packetFrame.MarkBackendDerived();
	packetFrame.AddScene( &viewDef, true );
	packetFrame.AddPass( RENDER_PASS_SPECIAL_EFFECTS, true, true );
	packetFrame.FinishScene();
	packetFrame.AddScene( &viewDef, true );
	packetFrame.AddPass( RENDER_PASS_AUTHORED_POST, true, true );
	packetFrame.FinishScene();
	packetFrame.AddScene( &viewDef, true );
	packetFrame.AddPass( RENDER_PASS_PRESENT, true, true );
	packetFrame.FinishScene();
	const scenePacketFrameStats_t &commandStats = packetFrame.Stats();
	if ( commandStats.specialEffectPackets != 1 || commandStats.postProcessPackets != 1 || commandStats.presentPackets != 1 ) {
		common->Printf( "RendererGeometryResource self-test failed: command category mismatch\n" );
		return false;
	}
	return true;
}

static bool R_GeometryResources_RunOverflowSelfTest( void ) {
	idScenePacketFrame packetFrame;
	viewEntity_t viewEntity;
	viewDef_t viewDef;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	memset( &viewDef, 0, sizeof( viewDef ) );
	R_GeometryResources_InitMatrix( viewEntity.modelMatrix );
	R_GeometryResources_InitMatrix( viewEntity.modelViewMatrix );
	R_GeometryResources_InitMatrix( viewDef.projectionMatrix );
	viewDef.viewEntitys = &viewEntity;

	packetFrame.MarkBackendDerived();
	packetFrame.AddScene( &viewDef, true );
	packetFrame.AddPass( RENDER_PASS_DEPTH, true );

	static srfTriangles_t geometries[SCENE_PACKET_MAX_GEOMETRY_RECORDS + 1];
	static drawSurf_t drawSurfs[SCENE_PACKET_MAX_GEOMETRY_RECORDS + 1];
	memset( geometries, 0, sizeof( geometries ) );
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	for ( int i = 0; i < SCENE_PACKET_MAX_GEOMETRY_RECORDS + 1; ++i ) {
		geometries[i].numVerts = 3;
		geometries[i].numIndexes = 3;
		drawSurfs[i].geo = &geometries[i];
		drawSurfs[i].space = &viewEntity;
		drawSurfs[i].sort = static_cast<float>( i );
		packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_DEPTH, i );
	}

	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	if ( !stats.overflow || stats.overflowCause != SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS || stats.geometryRecords != SCENE_PACKET_MAX_GEOMETRY_RECORDS || stats.drawPackets != SCENE_PACKET_MAX_GEOMETRY_RECORDS + 1 ) {
		common->Printf(
			"RendererGeometryResource self-test failed: overflow mismatch overflow=%d cause=%s geometry=%d draws=%d\n",
			stats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( stats.overflowCause ),
			stats.geometryRecords,
			stats.drawPackets );
		return false;
	}
	return true;
}

void R_GeometryResources_PrintGfxInfo( void ) {
	common->Printf(
		"Renderer geometry records: packet V2, maxGeometryRecords=%d, maxInstanceRecords=%d, indexedType=0x%x, legacyPointers=debug/provenance\n",
		SCENE_PACKET_MAX_GEOMETRY_RECORDS,
		SCENE_PACKET_MAX_INSTANCE_RECORDS,
		GL_INDEX_TYPE );
}

bool RendererGeometryResource_RunSelfTest( void ) {
	if ( !R_GeometryResources_RunRecordSelfTest() ) {
		return false;
	}
	if ( !R_GeometryResources_RunCategorySelfTest() ) {
		return false;
	}
	if ( !R_GeometryResources_RunOverflowSelfTest() ) {
		return false;
	}
	common->Printf(
		"RendererGeometryResource self-test passed (maxGeometry=%d maxInstances=%d categories=ok overflow=ok)\n",
		SCENE_PACKET_MAX_GEOMETRY_RECORDS,
		SCENE_PACKET_MAX_INSTANCE_RECORDS );
	return true;
}
