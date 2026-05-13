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
	memset( &stats, 0, sizeof( stats ) );
	activeScene = -1;
	activePass = -1;
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
		stats.overflow = true;
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
	record.permutation.tier = glConfig.rendererTier;
	return recordIndex;
}

bool idScenePacketFrame::AddScene( const viewDef_t *viewDef, bool legacyBridge ) {
	if ( stats.scenePackets >= SCENE_PACKET_MAX_SCENES ) {
		stats.overflow = true;
		activeScene = -1;
		activePass = -1;
		return false;
	}

	scenePacket_t &scene = scenes[stats.scenePackets++];
	memset( &scene, 0, sizeof( scene ) );
	scene.viewDef = viewDef;
	scene.firstPassPacket = stats.passPackets;
	scene.firstDrawPacket = stats.drawPackets;
	scene.legacyBridge = legacyBridge;
	activeScene = stats.scenePackets - 1;
	activePass = -1;
	return true;
}

bool idScenePacketFrame::AddPass( renderPassCategory_t category, bool enabled, bool commandOnly ) {
	if ( activeScene < 0 ) {
		if ( !AddScene( NULL, true ) ) {
			return false;
		}
	}
	if ( stats.passPackets >= SCENE_PACKET_MAX_PASSES ) {
		stats.overflow = true;
		activePass = -1;
		return false;
	}

	passPacket_t &pass = passes[stats.passPackets++];
	memset( &pass, 0, sizeof( pass ) );
	pass.passCategory = category;
	pass.firstDrawPacket = stats.drawPackets;
	pass.enabled = enabled;
	pass.commandOnly = commandOnly;
	activePass = stats.passPackets - 1;
	scenes[activeScene].passPacketCount++;
	return true;
}

bool idScenePacketFrame::AddDrawPacket( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex ) {
	if ( activePass < 0 ) {
		if ( !AddPass( category, true ) ) {
			return false;
		}
	}
	if ( stats.drawPackets >= SCENE_PACKET_MAX_DRAWS ) {
		stats.overflow = true;
		stats.clippedDrawPackets++;
		return false;
	}

	drawPacket_t &packet = drawPackets[stats.drawPackets++];
	memset( &packet, 0, sizeof( packet ) );
	const int materialRecordIndex = FindOrAddMaterialRecord( drawSurf );
	packet.legacyDrawSurf = drawSurf;
	packet.viewDef = activeScene >= 0 ? scenes[activeScene].viewDef : NULL;
	packet.space = drawSurf ? drawSurf->space : NULL;
	packet.materialRecord = materialRecordIndex >= 0 ? &materialRecords[materialRecordIndex] : NULL;
	packet.sortKey.value = R_ScenePackets_BuildSortKey( drawSurf, category, drawIndex );
	packet.passCategory = category;
	packet.legacySort = drawSurf ? drawSurf->sort : 0.0f;
	packet.materialRecordIndex = materialRecordIndex;
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

	passes[activePass].drawPacketCount++;
	scenes[activeScene].drawPacketCount++;
	if ( drawSurf != NULL && drawSurf->material != NULL ) {
		stats.drawPacketsWithMaterial++;
	}
	if ( packet.materialRecord != NULL ) {
		stats.drawPacketsWithResourceRecord++;
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
}

void idScenePacketFrame::AddCommandPacket( void ) {
	stats.commandPackets++;
}

void idScenePacketFrame::AddLegacyDrawView( void ) {
	stats.legacyDrawViews++;
}

void idScenePacketFrame::AddClippedDrawPackets( int count ) {
	if ( count > 0 ) {
		stats.clippedDrawPackets += count;
		stats.overflow = true;
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

const scenePacketFrameStats_t &idScenePacketFrame::Stats( void ) const {
	return stats;
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
	packetFrame.AddCommandPacket();
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
		"scenePackets source=%s scenes=%d passes=%d draws=%d clipped=%d cmds=%d drawViews=%d materials=%d withMaterial=%d resources=%d geometry=%d regs=%d indexCache=%d ambientCache=%d overflow=%d\n",
		stats.frontEndDerived ? "frontend" : ( stats.backendDerived ? "backend" : "unknown" ),
		stats.scenePackets,
		stats.passPackets,
		stats.drawPackets,
		stats.clippedDrawPackets,
		stats.commandPackets,
		stats.legacyDrawViews,
		stats.materialRecords,
		stats.drawPacketsWithMaterial,
		stats.drawPacketsWithResourceRecord,
		stats.drawPacketsWithGeometry,
		stats.drawPacketsWithShaderRegisters,
		stats.drawPacketsWithIndexCache,
		stats.drawPacketsWithAmbientCache,
		stats.overflow ? 1 : 0 );
	for ( int i = 0; i < packetFrame.NumPasses(); ++i ) {
		const passPacket_t &pass = packetFrame.Pass( i );
		common->Printf(
			"scenePackets pass[%d]=%s draws=%d enabled=%d\n",
			i,
			RenderPassCategory_Name( pass.passCategory ),
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
	if ( tr.defaultMaterial != NULL ) {
		drawSurfs[0].material = tr.defaultMaterial;
		drawSurfs[0].sort = tr.defaultMaterial->GetSort();
		drawSurfs[1].material = tr.defaultMaterial;
		drawSurfs[1].sort = tr.defaultMaterial->GetSort() + 0.000001f;
	}
	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
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
	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	const int expectedMaterialRecords = tr.defaultMaterial != NULL ? 1 : 0;
	const int expectedDrawsWithMaterial = tr.defaultMaterial != NULL ? 12 : 0;
	if ( stats.scenePackets != 3 || stats.passPackets != 8 || stats.drawPackets != 12 || stats.legacyDrawViews != 1 || stats.commandPackets != 2 || stats.materialRecords != expectedMaterialRecords || stats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || stats.drawPacketsWithGeometry != 12 || !stats.backendDerived || stats.frontEndDerived || stats.overflow ) {
		common->Printf(
			"RendererScenePacket self-test failed: scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d resources=%d geometry=%d overflow=%d\n",
			stats.scenePackets,
			stats.passPackets,
			stats.drawPackets,
			stats.legacyDrawViews,
			stats.commandPackets,
			stats.materialRecords,
			stats.drawPacketsWithResourceRecord,
			stats.drawPacketsWithGeometry,
			stats.overflow ? 1 : 0 );
		return false;
	}
	if ( expectedMaterialRecords > 0 ) {
		const drawPacket_t &packet = packetFrame.DrawPacket( 0 );
		if ( packet.materialRecord == NULL || packet.materialRecord->material != tr.defaultMaterial || packet.materialRecordIndex != 0 || packet.indexCount != 6 || packet.vertexCount != 3 ) {
			common->Printf(
				"RendererScenePacket self-test failed: bad material packet record=%d indexCount=%d vertexCount=%d\n",
				packet.materialRecordIndex,
				packet.indexCount,
				packet.vertexCount );
			return false;
		}
	}

	R_ScenePackets_BeginFrame();
	R_ScenePackets_AddRenderView( &worldView );
	R_ScenePackets_AddSpecialEffects( &worldView );
	R_ScenePackets_AddPresent();
	const idScenePacketFrame &frontEndPacketFrame = R_ScenePackets_FrontEndFrame();
	const scenePacketFrameStats_t &frontEndStats = frontEndPacketFrame.Stats();
	if ( frontEndStats.scenePackets != 3 || frontEndStats.passPackets != 8 || frontEndStats.drawPackets != 12 || frontEndStats.legacyDrawViews != 1 || frontEndStats.commandPackets != 2 || frontEndStats.materialRecords != expectedMaterialRecords || frontEndStats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || frontEndStats.drawPacketsWithGeometry != 12 || !frontEndStats.frontEndDerived || frontEndStats.backendDerived || frontEndStats.overflow ) {
		common->Printf(
			"RendererScenePacket self-test failed: frontend scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d resources=%d geometry=%d source(frontend=%d backend=%d) overflow=%d\n",
			frontEndStats.scenePackets,
			frontEndStats.passPackets,
			frontEndStats.drawPackets,
			frontEndStats.legacyDrawViews,
			frontEndStats.commandPackets,
			frontEndStats.materialRecords,
			frontEndStats.drawPacketsWithResourceRecord,
			frontEndStats.drawPacketsWithGeometry,
			frontEndStats.frontEndDerived ? 1 : 0,
			frontEndStats.backendDerived ? 1 : 0,
			frontEndStats.overflow ? 1 : 0 );
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
