// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ClassicGuiDomain.h"

#include <cstring>

namespace {

const std::uint64_t HASH_OFFSET = 1469598103934665603ull;
const std::uint64_t HASH_PRIME = 1099511628211ull;

typedef struct classicGuiDomainState_s {
	classicGuiDomainView_t views[ CLASSIC_GUI_DOMAIN_MAX_VIEWS ];
	classicGuiDomainDraw_t draws[ CLASSIC_GUI_DOMAIN_MAX_DRAWS ];
	rendererEvaluatedMaterialPass_t passes[ CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES ];
	classicGuiDomainStats_t stats;
	int viewCount;
	int drawCount;
	int passCount;
} classicGuiDomainState_t;

classicGuiDomainState_t domain;

static bool RangeFits( int first, int count, int capacity ) {
	return first >= 0 && count >= 0 && first <= capacity && count <= capacity - first;
}

static void HashByte( std::uint64_t &hash, unsigned int value ) {
	hash ^= value & 0xffu;
	hash *= HASH_PRIME;
}

static void HashU32( std::uint64_t &hash, std::uint32_t value ) {
	for ( int i = 0; i < 4; ++i ) {
		HashByte( hash, value >> ( i * 8 ) );
	}
}

static void HashU64( std::uint64_t &hash, std::uint64_t value ) {
	for ( int i = 0; i < 8; ++i ) {
		HashByte( hash, static_cast<unsigned int>( value >> ( i * 8 ) ) );
	}
}

static void HashInt( std::uint64_t &hash, int value ) {
	HashU32( hash, static_cast<std::uint32_t>( value ) );
}

static void HashBool( std::uint64_t &hash, bool value ) {
	HashByte( hash, value ? 1u : 0u );
}

static void HashFloat( std::uint64_t &hash, float value ) {
	std::uint32_t bits = 0;
	static_assert( sizeof( bits ) == sizeof( value ), "float hash width mismatch" );
	std::memcpy( &bits, &value, sizeof( bits ) );
	HashU32( hash, bits );
}

static std::uint64_t HashPass( const rendererEvaluatedMaterialPass_t &pass ) {
	std::uint64_t hash = HASH_OFFSET;
	HashU32( hash, pass.order );
	HashInt( hash, pass.sourceStageIndex );
	HashInt( hash, pass.kind );
	HashInt( hash, pass.textureSemantic );
	// Generation is intentionally excluded so identical records hash equally
	// across frames; the low bits retain record/binding identity.
	HashU32( hash, static_cast<std::uint32_t>( pass.textureResourceId ) );
	HashFloat( hash, pass.condition );
	HashBool( hash, pass.active );
	HashInt( hash, pass.disposition );
	for ( int i = 0; i < 4; ++i ) {
		HashFloat( hash, pass.color[ i ] );
	}
	for ( int i = 0; i < 6; ++i ) {
		HashFloat( hash, pass.textureMatrix[ i ] );
	}
	HashBool( hash, pass.blend.enabled );
	HashInt( hash, pass.blend.sourceColor );
	HashInt( hash, pass.blend.destinationColor );
	HashInt( hash, pass.blend.colorOperation );
	HashInt( hash, pass.blend.sourceAlpha );
	HashInt( hash, pass.blend.destinationAlpha );
	HashInt( hash, pass.blend.alphaOperation );
	HashBool( hash, pass.depth.testEnabled );
	HashBool( hash, pass.depth.writeEnabled );
	HashInt( hash, pass.depth.compareOperation );
	HashInt( hash, pass.cull );
	HashU32( hash, pass.colorWriteMask );
	HashBool( hash, pass.alphaTestEnabled );
	HashInt( hash, pass.alphaTestCompareOperation );
	HashFloat( hash, pass.alphaTest );
	HashInt( hash, pass.texgen );
	HashInt( hash, pass.vertexColor );
	HashBool( hash, pass.polygonOffsetEnabled );
	HashFloat( hash, pass.polygonOffsetFactor );
	HashFloat( hash, pass.polygonOffsetUnits );
	HashInt( hash, pass.programFamily );
	HashU32( hash, pass.programKey );
	return hash;
}

static std::uint64_t HashDraw( const classicGuiDomainDraw_t &draw ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, draw.sourceSurfaceIndex );
	HashInt( hash, draw.sourceSurface );
	HashBool( hash, draw.packetBacked );
	HashInt( hash, draw.materialId );
	HashInt( hash, draw.vertexCount );
	HashInt( hash, draw.firstIndex );
	HashInt( hash, draw.indexCount );
	HashInt( hash, draw.vertexOffset );
	HashInt( hash, draw.scissorX1 );
	HashInt( hash, draw.scissorY1 );
	HashInt( hash, draw.scissorX2 );
	HashInt( hash, draw.scissorY2 );
	HashInt( hash, draw.deformRole );
	HashInt( hash, draw.deformOutcome );
	HashU64( hash, draw.deformContractHash );
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, draw.modelViewMatrix[ i ] );
	}
	HashInt( hash, draw.evaluatedPassCount );
	for ( int i = 0; i < draw.evaluatedPassCount; ++i ) {
		HashU64( hash, HashPass( domain.passes[ draw.firstEvaluatedPass + i ] ) );
	}
	return hash;
}

static std::uint64_t HashView( const classicGuiDomainView_t &view ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, view.scope );
	HashInt( hash, view.sourceSurfaceCount );
	HashInt( hash, view.drawableSurfaceCount );
	HashInt( hash, view.noopSurfaceCount );
	HashInt( hash, view.evaluatedPassCount );
	HashInt( hash, view.drawablePassCount );
	HashInt( hash, view.noopPassCount );
	HashInt( hash, view.materialDeformSurfaceCount );
	HashInt( hash, view.completedDeformSurfaceCount );
	HashInt( hash, view.emptyDeformSurfaceCount );
	HashInt( hash, view.viewportX1 );
	HashInt( hash, view.viewportY1 );
	HashInt( hash, view.viewportX2 );
	HashInt( hash, view.viewportY2 );
	HashInt( hash, view.scissorX1 );
	HashInt( hash, view.scissorY1 );
	HashInt( hash, view.scissorX2 );
	HashInt( hash, view.scissorY2 );
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, view.projectionMatrix[ i ] );
	}
	for ( int i = 0; i < view.drawCount; ++i ) {
		HashU64( hash, domain.draws[ view.firstDraw + i ].hash );
	}
	return hash;
}

static void InitDraw( classicGuiDomainDraw_t &draw ) {
	std::memset( &draw, 0, sizeof( draw ) );
	draw.drawPacketIndex = -1;
	draw.materialTableRecordIndex = -1;
	draw.materialId = -1;
	draw.firstGuiPass = -1;
	draw.firstWorldPass = -1;
	draw.firstEvaluatedPass = -1;
}

static void InitView( classicGuiDomainView_t &view, const viewDef_t *viewDef,
		int scenePacketIndex ) {
	std::memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.scope = viewDef != NULL && viewDef->viewEntitys != NULL
		? CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD
		: CLASSIC_GUI_DOMAIN_SCOPE_ROOT_2D;
	view.scenePacketIndex = scenePacketIndex;
	view.guiPassPacketIndex = -1;
	view.firstDraw = -1;
	view.firstEvaluatedPass = -1;
	view.failurePassPacketIndex = -1;
	view.failureDrawPacketIndex = -1;
	view.failureSourceSurfaceIndex = -1;
	view.failureSourceStageIndex = -1;
	view.guiPassFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE;
	view.evaluationStatus = RENDERER_MATERIAL_PASS_EVALUATION_SUCCESS;
	if ( viewDef != NULL ) {
		view.viewportX1 = viewDef->viewport.x1;
		view.viewportY1 = viewDef->viewport.y1;
		view.viewportX2 = viewDef->viewport.x2;
		view.viewportY2 = viewDef->viewport.y2;
		view.scissorX1 = viewDef->scissor.x1;
		view.scissorY1 = viewDef->scissor.y1;
		view.scissorX2 = viewDef->scissor.x2;
		view.scissorY2 = viewDef->scissor.y2;
		std::memcpy( view.projectionMatrix, viewDef->projectionMatrix,
			sizeof( view.projectionMatrix ) );
		if ( view.scope == CLASSIC_GUI_DOMAIN_SCOPE_ROOT_2D ) {
			view.sourceSurfaceCount = viewDef->numDrawSurfs;
		} else if ( viewDef->drawSurfs != NULL ) {
			for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
				if ( viewDef->drawSurfs[ i ] != NULL
						&& ( viewDef->drawSurfs[ i ]->dsFlags
							& DSF_IN_WORLD_GUI ) != 0 ) {
					view.sourceSurfaceCount++;
				}
			}
		}
	}
}

static bool FailView( classicGuiDomainView_t &view,
		classicGuiDomainFailure_t failure, int detail,
		int sourceSurface = -1, int drawPacket = -1, int passPacket = -1,
		int sourceStage = -1,
		materialResourceGuiPassFailure_t guiFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE,
		rendererMaterialPassEvaluationStatus_t evaluationStatus = RENDERER_MATERIAL_PASS_EVALUATION_SUCCESS ) {
	view.ready = false;
	view.failure = failure;
	view.failureDetail = detail;
	view.guiPassFailure = guiFailure;
	view.evaluationStatus = evaluationStatus;
	view.failureSourceSurfaceIndex = sourceSurface;
	view.failureDrawPacketIndex = drawPacket;
	view.failurePassPacketIndex = passPacket;
	view.failureSourceStageIndex = sourceStage;
	view.firstDraw = -1;
	view.drawCount = 0;
	view.firstEvaluatedPass = -1;
	view.evaluatedPassCount = 0;
	view.activePassCount = 0;
	view.drawablePassCount = 0;
	view.inactivePassCount = 0;
	view.activeNoopPassCount = 0;
	view.noopPassCount = 0;
	view.materialDeformSurfaceCount = 0;
	view.completedDeformSurfaceCount = 0;
	view.emptyDeformSurfaceCount = 0;
	view.hash = 0;
	domain.stats.fallbackViews++;
	if ( failure >= CLASSIC_GUI_DOMAIN_FAILURE_NONE
			&& failure < CLASSIC_GUI_DOMAIN_FAILURE_COUNT ) {
		domain.stats.failureCounts[ failure ]++;
	}
	if ( failure == CLASSIC_GUI_DOMAIN_FAILURE_SCENE_PACKET_OVERFLOW
			|| failure == CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_TABLE_OVERFLOW
			|| failure == CLASSIC_GUI_DOMAIN_FAILURE_VIEW_POOL_OVERFLOW
			|| failure == CLASSIC_GUI_DOMAIN_FAILURE_DRAW_POOL_OVERFLOW
			|| failure == CLASSIC_GUI_DOMAIN_FAILURE_EVALUATED_PASS_POOL_OVERFLOW ) {
		domain.stats.overflow = true;
	}
	return false;
}

static bool MaterialRequestsClassicDeform( const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && drawSurf->material != NULL
		&& drawSurf->material->Deform() != DFRM_NONE;
}

static int DeformContractFailureDetail( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL ) {
		return -1;
	}
	return static_cast<int>( drawSurf->classicDeform.role )
		* CLASSIC_DEFORM_OUTCOME_COUNT
		+ static_cast<int>( drawSurf->classicDeform.outcome );
}

static bool ValidateFinalizedDeformContract( const drawSurf_t *drawSurf,
		const drawPacket_t *packet, classicDeformRole_t &role,
		classicDeformOutcome_t &outcome, std::uint64_t &semanticHash ) {
	role = CLASSIC_DEFORM_ROLE_UNKNOWN;
	outcome = CLASSIC_DEFORM_OUTCOME_NONE;
	semanticHash = 0;
	if ( !MaterialRequestsClassicDeform( drawSurf ) ) {
		return true;
	}
	// The deform switch is independent authorization. Without it the complete
	// GUI view rolls back to the unchanged classic owner.
	if ( !r_rendererSharedDeform.GetBool() ) {
		return false;
	}
	const std::uint64_t frameToken =
		R_ClassicDeformDomain_BackEndFrameToken();
	const classicDeformRecord_t &record = drawSurf->classicDeform;
	if ( record.role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
			|| !record.cpuFinalized
			|| !R_ClassicDeformDomain_ValidateRecordForFrame( record,
				frameToken )
			|| !R_ClassicDeformDomain_RecordMatchesDrawSurf( record, drawSurf, frameToken )
			|| ( !R_ClassicDeformDomain_HasCompletedOutput( record )
				&& !R_ClassicDeformDomain_HasEmptyOutput( record ) ) ) {
		return false;
	}
	if ( packet != NULL ) {
		if ( !packet->hasClassicDeformRecord
				|| packet->classicDeformRecord == NULL
				|| packet->geometryRecord == NULL
				|| packet->classicDeformRecord
					!= &packet->geometryRecord->classicDeform
				|| !packet->geometryRecord->hasClassicDeformRecord
				|| !R_ClassicDeformDomain_ValidateRecordForFrame(
					*packet->classicDeformRecord, frameToken )
				|| !R_ClassicDeformDomain_SameProvenance( record,
					*packet->classicDeformRecord ) ) {
			return false;
		}
	}
	role = record.role;
	outcome = record.outcome;
	semanticHash = record.semanticHash;
	return true;
}

static classicGuiDomainSourceSurface_t ClassifySourceSurface(
		classicGuiDomainScope_t scope, const drawSurf_t *drawSurf,
		bool &packetExpected ) {
	packetExpected = false;
	if ( drawSurf == NULL ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NULL_SURFACE;
	}
	if ( drawSurf->material == NULL ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_MISSING_MATERIAL;
	}
	if ( drawSurf->material->GetSort() >= SS_POST_PROCESS ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_POST_PROCESS;
	}
	if ( !drawSurf->material->HasAmbient() ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NO_AMBIENT;
	}
	if ( drawSurf->material->IsPortalSky() ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_PORTAL_SKY;
	}
	if ( drawSurf->geo == NULL || drawSurf->geo->numIndexes <= 0 ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY;
	}
	if ( drawSurf->space == NULL ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_MISSING_SPACE;
	}
	if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DEPTH_HACK;
	}
	if ( drawSurf->decalColorCache != NULL || drawSurf->decalColorStageCount != 0
			|| drawSurf->decalColorStride != 0 || drawSurf->decalColorOffset != 0 ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DECAL_COLOR_STREAM;
	}
	if ( drawSurf->material->SuppressInSubview() ) {
		// The established GL root-2D walker suppresses this flag while the
		// established Vulkan 2D walker draws it. Until that legacy divergence is
		// resolved, neither result is a truthful shared semantic record.
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_SUPPRESSED_IN_SUBVIEW;
	}
	if ( scope == CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD
			&& ( ( drawSurf->dsFlags & DSF_IN_WORLD_GUI ) == 0
				|| drawSurf->space->entityDef != NULL ) ) {
		return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_MISSING_SPACE;
	}
	packetExpected = true;
	return CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_DRAWABLE;
}

static bool CountDisposition( classicGuiDomainDraw_t &draw,
		const rendererEvaluatedMaterialPass_t &pass ) {
	if ( pass.active ) {
		draw.activePassCount++;
	}
	switch ( pass.disposition ) {
	case RENDERER_MATERIAL_PASS_DRAW:
		draw.drawablePassCount++;
		return pass.active;
	case RENDERER_MATERIAL_PASS_INACTIVE_CONDITION:
		draw.inactivePassCount++;
		draw.noopPassCount++;
		return !pass.active;
	case RENDERER_MATERIAL_PASS_NOOP_ZERO_ONE_BLEND:
	case RENDERER_MATERIAL_PASS_NOOP_BLACK_ADDITIVE:
	case RENDERER_MATERIAL_PASS_NOOP_TRANSPARENT_ALPHA:
		draw.activeNoopPassCount++;
		draw.noopPassCount++;
		return pass.active;
	default:
		return false;
	}
}

static bool ValidateDrawPacket( const idScenePacketFrame &packetFrame,
		int packetIndex, const viewDef_t *viewDef, const drawSurf_t *drawSurf,
		classicGuiDomainScope_t scope, classicGuiDomainFailure_t &failure ) {
	if ( packetIndex < 0 || packetIndex >= packetFrame.NumDrawPackets() ) {
		failure = CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH;
		return false;
	}
	const drawPacket_t &packet = packetFrame.DrawPacket( packetIndex );
	const scenePacketCategory_t expectedCategory =
		scope == CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD
			? SCENE_PACKET_CATEGORY_WORLD : SCENE_PACKET_CATEGORY_GUI;
	if ( packet.passCategory != RENDER_PASS_GUI
			|| packet.packetCategory != expectedCategory
			|| packet.viewDef != viewDef || packet.legacyDrawSurf != drawSurf ) {
		failure = CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH;
		return false;
	}
	if ( packet.geometryRecordIndex < 0
			|| packet.geometryRecordIndex >= packetFrame.NumGeometryRecords()
			|| packet.geometryRecord != &packetFrame.GeometryRecord( packet.geometryRecordIndex )
			|| packet.geometryRecord->legacyGeometry != drawSurf->geo ) {
		failure = CLASSIC_GUI_DOMAIN_FAILURE_MISSING_GEOMETRY_RECORD;
		return false;
	}
	if ( packet.instanceRecordIndex < 0
			|| packet.instanceRecordIndex >= packetFrame.NumInstanceRecords()
			|| packet.instanceRecord != &packetFrame.InstanceRecord( packet.instanceRecordIndex )
			|| packet.instanceRecord->legacySpace != drawSurf->space ) {
		failure = CLASSIC_GUI_DOMAIN_FAILURE_MISSING_INSTANCE_RECORD;
		return false;
	}
	if ( packet.materialRecordIndex < 0
			|| packet.materialRecordIndex >= packetFrame.NumMaterialRecords()
			|| packet.materialRecord != &packetFrame.MaterialRecord( packet.materialRecordIndex )
			|| packet.materialRecord->material != drawSurf->material ) {
		failure = CLASSIC_GUI_DOMAIN_FAILURE_MISSING_MATERIAL_RECORD;
		return false;
	}
	return true;
}

static bool R_ClassicGuiDomain_PrepareView( const idScenePacketFrame &packetFrame,
		const scenePacket_t &scene, classicGuiDomainView_t &view ) {
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	const materialResourceTableStats_t &tableStats = R_MaterialResourceTable_Stats();
	if ( packetStats.overflow ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_SCENE_PACKET_OVERFLOW,
			packetStats.overflowCause );
	}
	if ( !tableStats.prepared ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_TABLE_NOT_PREPARED, 0 );
	}
	if ( !tableStats.available ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_UNAVAILABLE, 0 );
	}
	if ( tableStats.overflow ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_TABLE_OVERFLOW, 0 );
	}
	const viewDef_t *viewDef = view.viewDef;
	const bool inWorld = view.scope == CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD;
	if ( viewDef == NULL
			|| ( !inWorld && viewDef->viewEntitys != NULL )
			|| ( inWorld && ( viewDef->viewEntitys == NULL
					|| viewDef->renderWorld == NULL || r_skipAmbient.GetBool() ) )
			|| viewDef->isSubview || viewDef->isMirror || viewDef->isXraySubview
			|| viewDef->isEditor || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->numClipPlanes != 0
			|| viewDef->renderView.viewID < 0 ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_UNSUPPORTED_VIEW, 0 );
	}
	if ( viewDef->numDrawSurfs < 0
			|| ( viewDef->numDrawSurfs > 0 && viewDef->drawSurfs == NULL ) ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DRAW_RANGE,
			viewDef->numDrawSurfs );
	}
	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() ) ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_SCENE_RANGE,
			scene.passPacketCount );
	}
	const passPacket_t *guiPass = NULL;
	int passPacketIndex = -1;
	for ( int localPass = 0; localPass < scene.passPacketCount; ++localPass ) {
		const int candidateIndex = scene.firstPassPacket + localPass;
		const passPacket_t &candidate = packetFrame.Pass( candidateIndex );
		if ( candidate.passCategory != RENDER_PASS_GUI ) {
			continue;
		}
		if ( guiPass != NULL || !candidate.enabled || candidate.commandOnly
				|| candidate.packetCategory != ( inWorld
					? SCENE_PACKET_CATEGORY_WORLD : SCENE_PACKET_CATEGORY_GUI ) ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_GUI_PASS,
				candidate.passCategory, -1, -1, candidateIndex );
		}
		guiPass = &candidate;
		passPacketIndex = candidateIndex;
	}
	if ( guiPass == NULL || ( !inWorld && scene.passPacketCount != 1 ) ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_GUI_PASS,
			scene.passPacketCount );
	}
	view.guiPassPacketIndex = passPacketIndex;
	if ( !RangeFits( scene.firstDrawPacket, scene.drawPacketCount,
			packetFrame.NumDrawPackets() )
			|| !RangeFits( guiPass->firstDrawPacket, guiPass->drawPacketCount,
				packetFrame.NumDrawPackets() )
			|| ( !inWorld && ( scene.firstDrawPacket != guiPass->firstDrawPacket
				|| scene.drawPacketCount != guiPass->drawPacketCount ) ) ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DRAW_RANGE,
			guiPass->drawPacketCount, -1, -1, passPacketIndex );
	}
	view.packetDrawCount = guiPass->drawPacketCount;
	if ( !RangeFits( domain.drawCount, view.sourceSurfaceCount,
			CLASSIC_GUI_DOMAIN_MAX_DRAWS ) ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_DRAW_POOL_OVERFLOW,
			view.sourceSurfaceCount );
	}

	const int drawCheckpoint = domain.drawCount;
	const int passCheckpoint = domain.passCount;
	int stagedPassCount = 0;
	int packetCursor = guiPass->firstDrawPacket;
	const int packetEnd = guiPass->firstDrawPacket + guiPass->drawPacketCount;
	int drawableSurfaces = 0;
	int noopSurfaces = 0;

	int sourceSlot = 0;
	for ( int sourceIndex = 0; sourceIndex < viewDef->numDrawSurfs; ++sourceIndex ) {
		const drawSurf_t *source = viewDef->drawSurfs[ sourceIndex ];
		if ( inWorld && ( source == NULL
				|| ( source->dsFlags & DSF_IN_WORLD_GUI ) == 0 ) ) {
			continue;
		}
		bool packetExpected = false;
		const classicGuiDomainSourceSurface_t classification =
			ClassifySourceSurface( view.scope, source, packetExpected );
		classicGuiDomainDraw_t &draw = domain.draws[ drawCheckpoint + sourceSlot++ ];
		InitDraw( draw );
		draw.sourceSurfaceIndex = sourceIndex;
		draw.sourceSurface = classification;
		draw.legacyDrawSurf = source;

		if ( classification == CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_SUPPRESSED_IN_SUBVIEW
				|| classification == CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_MISSING_SPACE
				|| classification == CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DEPTH_HACK
				|| classification == CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DECAL_COLOR_STREAM
				|| classification == CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_POST_PROCESS ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_SURFACE_FALLBACK,
				classification, sourceIndex );
		}

		const drawPacket_t *packet = NULL;
		if ( packetExpected ) {
			classicGuiDomainFailure_t packetFailure = CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DRAW_PACKET;
			if ( packetCursor >= packetEnd
					|| !ValidateDrawPacket( packetFrame, packetCursor, viewDef,
						source, view.scope, packetFailure ) ) {
				return FailView( view, packetFailure, packetCursor,
					sourceIndex, packetCursor, passPacketIndex );
			}
			packet = &packetFrame.DrawPacket( packetCursor );
			draw.packetBacked = true;
			draw.drawPacketIndex = packetCursor++;
			draw.materialTableRecordIndex = packet->materialRecordIndex;
			draw.vertexCount = packet->vertexCount;
			draw.firstIndex = packet->firstIndex;
			draw.indexCount = packet->indexCount;
			draw.vertexOffset = packet->vertexOffset;
			draw.scissorX1 = packet->scissorX1;
			draw.scissorY1 = packet->scissorY1;
			draw.scissorX2 = packet->scissorX2;
			draw.scissorY2 = packet->scissorY2;
			draw.hasIndexCache = packet->hasIndexCache;
			draw.hasAmbientCache = packet->hasAmbientCache;
			std::memcpy( draw.modelViewMatrix, packet->instanceRecord->modelViewMatrix,
				sizeof( draw.modelViewMatrix ) );
		}

		if ( !ValidateFinalizedDeformContract( source, packet,
				draw.deformRole, draw.deformOutcome,
				draw.deformContractHash ) ) {
			return FailView( view,
				CLASSIC_GUI_DOMAIN_FAILURE_DEFORM_CONTRACT,
				DeformContractFailureDetail( source ), sourceIndex,
				draw.drawPacketIndex, passPacketIndex );
		}

		if ( classification != CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_DRAWABLE ) {
			noopSurfaces++;
			draw.hash = HashDraw( draw );
			continue;
		}
		drawableSurfaces++;
		if ( packet == NULL || !packet->hasShaderRegisters
				|| packet->instanceRecord == NULL
				|| !packet->instanceRecord->hasShaderRegisters
				|| packet->instanceRecord->legacyShaderRegisters == NULL
				|| packet->instanceRecord->legacyShaderRegisters != source->shaderRegisters ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MISSING_INSTANCE_RECORD,
				0, sourceIndex, draw.drawPacketIndex, passPacketIndex );
		}
		if ( packet->instanceRecord->negativeScale ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_SURFACE_FALLBACK,
				CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_NEGATIVE_SCALE,
				sourceIndex, draw.drawPacketIndex, passPacketIndex );
		}
		const materialResourceTableRecord_t *materialRecord =
			R_MaterialResourceTable_RecordForIndex( packet->materialRecordIndex );
		if ( materialRecord == NULL ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MISSING_MATERIAL_RECORD,
				packet->materialRecordIndex, sourceIndex, draw.drawPacketIndex, passPacketIndex );
		}
		if ( materialRecord->sourceMaterialRecordIndex != packet->materialRecordIndex
				|| materialRecord->material != source->material
				|| materialRecord->tableGeneration == 0
				|| ( view.tableGeneration != 0
					&& view.tableGeneration != materialRecord->tableGeneration ) ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_STALE_MATERIAL_RECORD,
				packet->materialRecordIndex, sourceIndex, draw.drawPacketIndex, passPacketIndex );
		}
		if ( packet->instanceRecord->shaderRegisterCount != materialRecord->registerCount ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MISSING_INSTANCE_RECORD,
				packet->instanceRecord->shaderRegisterCount, sourceIndex,
				draw.drawPacketIndex, passPacketIndex );
		}
		view.tableGeneration = materialRecord->tableGeneration;
		draw.tableGeneration = materialRecord->tableGeneration;
		draw.materialId = materialRecord->materialId;
		rendererMaterialPassList_t compiled;
		if ( inWorld ) {
			draw.firstWorldPass = materialRecord->firstWorldPass;
			draw.worldPassCount = materialRecord->worldPassCount;
			if ( !materialRecord->worldDomainReferenced
					|| !materialRecord->worldPassEligible
					|| materialRecord->worldPassFailure
						!= MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
					|| materialRecord->firstWorldPass < 0
					|| materialRecord->worldPassCount <= 0
					|| !R_MaterialResourceTable_WorldPassEligible( *materialRecord ) ) {
				return FailView( view,
					CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_INELIGIBLE,
					materialRecord->worldPassFailure, sourceIndex,
					draw.drawPacketIndex, passPacketIndex,
					materialRecord->worldPassFailureStage,
					materialRecord->worldPassFailure );
			}
			if ( !R_MaterialResourceTable_CopyWorldPassList( *materialRecord,
						compiled )
					|| compiled.count != static_cast<std::uint32_t>(
						materialRecord->worldPassCount ) ) {
				return FailView( view,
					CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_COPY_FAILED,
					materialRecord->worldPassCount, sourceIndex,
					draw.drawPacketIndex, passPacketIndex );
			}
		} else {
			draw.firstGuiPass = materialRecord->firstGuiPass;
			draw.guiPassCount = materialRecord->guiPassCount;
			if ( !materialRecord->guiDomainReferenced
					|| !materialRecord->guiPassEligible
					|| materialRecord->guiPassFailure
						!= MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
					|| materialRecord->firstGuiPass < 0
					|| materialRecord->guiPassCount <= 0
					|| !R_MaterialResourceTable_GuiPassEligible( *materialRecord ) ) {
				return FailView( view,
					CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_INELIGIBLE,
					materialRecord->guiPassFailure, sourceIndex,
					draw.drawPacketIndex, passPacketIndex,
					materialRecord->guiPassFailureStage,
					materialRecord->guiPassFailure );
			}
			if ( !R_MaterialResourceTable_CopyGuiPassList( *materialRecord,
						compiled )
					|| compiled.count != static_cast<std::uint32_t>(
						materialRecord->guiPassCount ) ) {
				return FailView( view,
					CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_COPY_FAILED,
					materialRecord->guiPassCount, sourceIndex,
					draw.drawPacketIndex, passPacketIndex );
			}
		}
		rendererEvaluatedMaterialPassList_t evaluated;
		const rendererMaterialPassEvaluationStatus_t evaluationStatus =
			RendererContracts_EvaluateMaterialPassList( evaluated, compiled,
				packet->instanceRecord->legacyShaderRegisters,
				materialRecord->registerCount > 0
					? static_cast<std::uint32_t>( materialRecord->registerCount ) : 0u );
		if ( evaluationStatus != RENDERER_MATERIAL_PASS_EVALUATION_SUCCESS
				|| evaluated.count != compiled.count ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_EVALUATION_FAILED,
				evaluationStatus, sourceIndex, draw.drawPacketIndex, passPacketIndex,
				-1, MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE, evaluationStatus );
		}
		if ( !RangeFits( passCheckpoint + stagedPassCount,
				static_cast<int>( evaluated.count ),
				CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES ) ) {
			return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_EVALUATED_PASS_POOL_OVERFLOW,
				static_cast<int>( evaluated.count ), sourceIndex, draw.drawPacketIndex,
				passPacketIndex );
		}
		draw.firstEvaluatedPass = passCheckpoint + stagedPassCount;
		draw.evaluatedPassCount = static_cast<int>( evaluated.count );
		for ( int passIndex = 0; passIndex < draw.evaluatedPassCount; ++passIndex ) {
			const rendererEvaluatedMaterialPass_t &pass = evaluated.passes[ passIndex ];
			const materialResourceTextureBinding_t *binding =
				R_MaterialResourceTable_ResolveTextureResource( pass.textureResourceId );
			if ( binding == NULL || binding->textureResourceId != pass.textureResourceId
					|| !binding->loaded || binding->missing || binding->defaulted ) {
				return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_TEXTURE_RESOURCE_UNAVAILABLE,
					passIndex, sourceIndex, draw.drawPacketIndex, passPacketIndex,
					pass.sourceStageIndex );
			}
			if ( !CountDisposition( draw, pass ) ) {
				return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DISPOSITION,
					pass.disposition, sourceIndex, draw.drawPacketIndex, passPacketIndex,
					pass.sourceStageIndex );
			}
			domain.passes[ draw.firstEvaluatedPass + passIndex ] = pass;
		}
		stagedPassCount += draw.evaluatedPassCount;
		draw.hash = HashDraw( draw );
	}

	if ( sourceSlot != view.sourceSurfaceCount || packetCursor != packetEnd ) {
		return FailView( view, CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor, sourceSlot, packetCursor,
			passPacketIndex );
	}

	view.firstDraw = drawCheckpoint;
	view.drawCount = view.sourceSurfaceCount;
	view.drawableSurfaceCount = drawableSurfaces;
	view.noopSurfaceCount = noopSurfaces;
	view.firstEvaluatedPass = passCheckpoint;
	view.evaluatedPassCount = stagedPassCount;
	for ( int i = 0; i < view.drawCount; ++i ) {
		const classicGuiDomainDraw_t &draw = domain.draws[ view.firstDraw + i ];
		view.activePassCount += draw.activePassCount;
		view.drawablePassCount += draw.drawablePassCount;
		view.inactivePassCount += draw.inactivePassCount;
		view.activeNoopPassCount += draw.activeNoopPassCount;
		view.noopPassCount += draw.noopPassCount;
		if ( draw.deformContractHash != 0 ) {
			view.materialDeformSurfaceCount++;
			if ( draw.deformOutcome == CLASSIC_DEFORM_OUTCOME_COMPLETED ) {
				view.completedDeformSurfaceCount++;
			} else if ( draw.deformOutcome == CLASSIC_DEFORM_OUTCOME_EMPTY ) {
				view.emptyDeformSurfaceCount++;
			}
		}
	}
	domain.drawCount += view.drawCount;
	domain.passCount += view.evaluatedPassCount;
	view.hash = HashView( view );
	view.failure = CLASSIC_GUI_DOMAIN_FAILURE_NONE;
	view.ready = true;
	domain.stats.readyViews++;
	domain.stats.drawableSurfaces += view.drawableSurfaceCount;
	domain.stats.noopSurfaces += view.noopSurfaceCount;
	domain.stats.draws += view.drawCount;
	domain.stats.evaluatedPasses += view.evaluatedPassCount;
	domain.stats.activePasses += view.activePassCount;
	domain.stats.drawablePasses += view.drawablePassCount;
	domain.stats.inactivePasses += view.inactivePassCount;
	domain.stats.activeNoopPasses += view.activeNoopPassCount;
	domain.stats.noopPasses += view.noopPassCount;
	domain.stats.materialDeformSurfaces += view.materialDeformSurfaceCount;
	domain.stats.completedDeformSurfaces += view.completedDeformSurfaceCount;
	domain.stats.emptyDeformSurfaces += view.emptyDeformSurfaceCount;
	return true;
}

static int ViewIndex( const classicGuiDomainView_t *view ) {
	// O(1) equivalent of the linear pool scan: a pointer identifies a live view
	// exactly when it is element [index] of the pool with index < viewCount.
	const auto index = view - domain.views;
	return index >= 0 && index < domain.viewCount
		&& view == &domain.views[ index ] ? static_cast<int>( index ) : -1;
}

static int DrawIndex( const classicGuiDomainDraw_t *draw ) {
	const auto index = draw - domain.draws;
	return index >= 0 && index < domain.drawCount
		&& draw == &domain.draws[ index ] ? static_cast<int>( index ) : -1;
}

static classicGuiDomainView_t *FindMutableView( const viewDef_t *viewDef ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].viewDef == viewDef ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

static bool SceneContainsGuiPass( const idScenePacketFrame &packetFrame,
		const scenePacket_t &scene ) {
	if ( scene.viewDef == NULL ) {
		return false;
	}
	if ( scene.viewDef->viewEntitys != NULL ) {
		if ( !r_rendererSharedInWorldGui.GetBool() ) {
			return false;
		}
	} else if ( !r_rendererSharedGui.GetBool() ) {
		return false;
	}
	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() ) ) {
		// Preserve a diagnostic descriptor for a malformed 2D draw scene.  Valid
		// scenes are identified by their GUI pass rather than the broad scene
		// packet category, which is WORLD for ordinary 2D command lists.
		return scene.viewDef->viewEntitys == NULL;
	}
	for ( int passIndex = 0; passIndex < scene.passPacketCount; ++passIndex ) {
		if ( packetFrame.Pass( scene.firstPassPacket + passIndex ).passCategory
				== RENDER_PASS_GUI ) {
			return true;
		}
	}
	return false;
}

static void RecordFallback( classicGuiDomainView_t *view,
		classicGuiDomainBackend_t backend, classicGuiDomainFailure_t failure,
		int detail, int drawnPasses, int noopPasses ) {
	classicGuiDomainBackendCoverage_t &coverage = domain.stats.backend[ backend ];
	if ( view == NULL ) {
		coverage.untrackedFallbacks++;
		return;
	}
	const int viewIndex = ViewIndex( view );
	if ( viewIndex < 0 || view->backendOutcome[ backend ] != CLASSIC_GUI_DOMAIN_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return;
	}
	view->backendOutcome[ backend ] = CLASSIC_GUI_DOMAIN_BACKEND_FALLBACK;
	view->backendFailure[ backend ] = failure == CLASSIC_GUI_DOMAIN_FAILURE_NONE
		? CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED : failure;
	view->backendFailureDetail[ backend ] = detail;
	view->backendDrawnPasses[ backend ] = drawnPasses;
	view->backendNoopPasses[ backend ] = noopPasses;
	coverage.fallbackViewMask |= 1ull << viewIndex;
	coverage.fallbackViews++;
	coverage.fallbackSourceSurfaces += view->sourceSurfaceCount;
	coverage.fallbackDrawablePasses += view->drawablePassCount;
	coverage.fallbackNoopPasses += view->noopPassCount;
	if ( failure == CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_COVERAGE_MISMATCH ) {
		coverage.coverageMismatches++;
	}
	if ( view->backendFailure[ backend ] >= CLASSIC_GUI_DOMAIN_FAILURE_NONE
			&& view->backendFailure[ backend ] < CLASSIC_GUI_DOMAIN_FAILURE_COUNT ) {
		domain.stats.failureCounts[ view->backendFailure[ backend ] ]++;
	}
}

} // namespace

void R_ClassicGuiDomain_ResetFrame( void ) {
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	domain.viewCount = 0;
	domain.drawCount = 0;
	domain.passCount = 0;
	idStr::Copynz( domain.stats.status, "empty", sizeof( domain.stats.status ) );
}

void R_ClassicGuiDomain_PrepareFrame( const idScenePacketFrame &packetFrame ) {
	R_ClassicGuiDomain_ResetFrame();
	domain.stats.prepared = true;
	domain.stats.sourceScenes = packetFrame.NumScenes();
	domain.stats.overflow = packetFrame.Stats().overflow;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( !SceneContainsGuiPass( packetFrame, scene ) ) {
			continue;
		}
		if ( FindMutableView( scene.viewDef ) != NULL ) {
			continue;
		}
		domain.stats.guiViews++;
		if ( scene.viewDef != NULL && scene.viewDef->viewEntitys != NULL ) {
			domain.stats.inWorldViews++;
		} else {
			domain.stats.rootViews++;
		}
		if ( domain.viewCount >= CLASSIC_GUI_DOMAIN_MAX_VIEWS ) {
			domain.stats.overflow = true;
			domain.stats.fallbackViews++;
			domain.stats.failureCounts[ CLASSIC_GUI_DOMAIN_FAILURE_VIEW_POOL_OVERFLOW ]++;
			continue;
		}
		classicGuiDomainView_t &view = domain.views[ domain.viewCount++ ];
		InitView( view, scene.viewDef, sceneIndex );
		if ( view.sourceSurfaceCount > 0 ) {
			domain.stats.sourceSurfaces += view.sourceSurfaceCount;
		}
		R_ClassicGuiDomain_PrepareView( packetFrame, scene, view );
	}

	domain.stats.frameValid = !domain.stats.overflow
		&& domain.stats.fallbackViews == 0;
	std::uint64_t frameHash = HASH_OFFSET;
	HashInt( frameHash, domain.stats.sourceScenes );
	HashInt( frameHash, domain.stats.guiViews );
	HashInt( frameHash, domain.stats.rootViews );
	HashInt( frameHash, domain.stats.inWorldViews );
	for ( int i = 0; i < domain.viewCount; ++i ) {
		HashInt( frameHash, domain.views[ i ].scenePacketIndex );
		HashInt( frameHash, domain.views[ i ].scope );
		HashBool( frameHash, domain.views[ i ].ready );
		HashInt( frameHash, domain.views[ i ].failure );
		HashU64( frameHash, domain.views[ i ].hash );
	}
	domain.stats.hash = frameHash;
	if ( domain.stats.guiViews == 0 ) {
		idStr::Copynz( domain.stats.status, "empty", sizeof( domain.stats.status ) );
	} else if ( domain.stats.frameValid ) {
		idStr::Copynz( domain.stats.status, "ready", sizeof( domain.stats.status ) );
	} else if ( domain.stats.readyViews == 0 ) {
		idStr::Copynz( domain.stats.status, "fallback", sizeof( domain.stats.status ) );
	} else {
		idStr::Copynz( domain.stats.status, "mixed-view-fallback", sizeof( domain.stats.status ) );
	}
}

const classicGuiDomainStats_t &R_ClassicGuiDomain_Stats( void ) {
	return domain.stats;
}

int R_ClassicGuiDomain_NumViews( void ) {
	return domain.viewCount;
}

const classicGuiDomainView_t *R_ClassicGuiDomain_ViewByIndex( int index ) {
	return index >= 0 && index < domain.viewCount ? &domain.views[ index ] : NULL;
}

const classicGuiDomainView_t *R_ClassicGuiDomain_ViewForScenePacket( int scenePacketIndex ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].scenePacketIndex == scenePacketIndex ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

const classicGuiDomainView_t *R_ClassicGuiDomain_FindView( const viewDef_t *viewDef ) {
	return FindMutableView( viewDef );
}

const classicGuiDomainView_t *R_ClassicGuiDomain_FindRootView(
		const viewDef_t *viewDef ) {
	classicGuiDomainView_t *view = FindMutableView( viewDef );
	return view != NULL && view->scope == CLASSIC_GUI_DOMAIN_SCOPE_ROOT_2D
		? view : NULL;
}

const classicGuiDomainView_t *R_ClassicGuiDomain_FindInWorldView(
		const viewDef_t *viewDef ) {
	classicGuiDomainView_t *view = FindMutableView( viewDef );
	return view != NULL && view->scope == CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD
		? view : NULL;
}

const classicGuiDomainDraw_t *R_ClassicGuiDomain_ViewDraw(
		const classicGuiDomainView_t &view, int drawIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || drawIndex < 0
			|| drawIndex >= view.drawCount
			|| !RangeFits( view.firstDraw, view.drawCount, domain.drawCount ) ) {
		return NULL;
	}
	return &domain.draws[ view.firstDraw + drawIndex ];
}

const rendererEvaluatedMaterialPass_t *R_ClassicGuiDomain_DrawPass(
		const classicGuiDomainDraw_t &draw, int passIndex ) {
	if ( DrawIndex( &draw ) < 0 || passIndex < 0 || passIndex >= draw.evaluatedPassCount
			|| !RangeFits( draw.firstEvaluatedPass, draw.evaluatedPassCount,
				domain.passCount ) ) {
		return NULL;
	}
	return &domain.passes[ draw.firstEvaluatedPass + passIndex ];
}

const materialResourceTextureBinding_t *R_ClassicGuiDomain_DrawPassTexture(
		const classicGuiDomainDraw_t &draw, int passIndex ) {
	const rendererEvaluatedMaterialPass_t *pass =
		R_ClassicGuiDomain_DrawPass( draw, passIndex );
	return pass != NULL
		? R_MaterialResourceTable_ResolveTextureResource( pass->textureResourceId ) : NULL;
}

bool R_ClassicGuiDomain_RecordOwned( const viewDef_t *viewDef,
		classicGuiDomainBackend_t backend, int drawnPasses, int noopPasses ) {
	if ( backend < CLASSIC_GUI_DOMAIN_BACKEND_GL
			|| backend >= CLASSIC_GUI_DOMAIN_BACKEND_COUNT ) {
		return false;
	}
	classicGuiDomainView_t *view = FindMutableView( viewDef );
	if ( view == NULL ) {
		return false;
	}
	classicGuiDomainBackendCoverage_t &coverage = domain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ] != CLASSIC_GUI_DOMAIN_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return view->backendOutcome[ backend ] == CLASSIC_GUI_DOMAIN_BACKEND_OWNED
			&& view->backendDrawnPasses[ backend ] == drawnPasses
			&& view->backendNoopPasses[ backend ] == noopPasses;
	}
	if ( !view->ready ) {
		RecordFallback( view, backend, CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY,
			view->failure, drawnPasses, noopPasses );
		return false;
	}
	if ( drawnPasses != view->drawablePassCount || noopPasses != view->noopPassCount ) {
		RecordFallback( view, backend,
			CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_COVERAGE_MISMATCH,
			drawnPasses, drawnPasses, noopPasses );
		return false;
	}
	const int viewIndex = ViewIndex( view );
	view->backendOutcome[ backend ] = CLASSIC_GUI_DOMAIN_BACKEND_OWNED;
	view->backendDrawnPasses[ backend ] = drawnPasses;
	view->backendNoopPasses[ backend ] = noopPasses;
	coverage.ownedViewMask |= 1ull << viewIndex;
	coverage.ownedViews++;
	coverage.ownedSourceSurfaces += view->sourceSurfaceCount;
	coverage.ownedDrawablePasses += drawnPasses;
	coverage.ownedNoopPasses += noopPasses;
	return true;
}

void R_ClassicGuiDomain_RecordBackendFallback( const viewDef_t *viewDef,
		classicGuiDomainBackend_t backend, classicGuiDomainFailure_t failure,
		int detail ) {
	if ( backend < CLASSIC_GUI_DOMAIN_BACKEND_GL
			|| backend >= CLASSIC_GUI_DOMAIN_BACKEND_COUNT ) {
		return;
	}
	RecordFallback( FindMutableView( viewDef ), backend, failure, detail, 0, 0 );
}

const classicGuiDomainBackendCoverage_t &R_ClassicGuiDomain_BackendCoverage(
		classicGuiDomainBackend_t backend ) {
	static const classicGuiDomainBackendCoverage_t empty = {};
	return backend >= CLASSIC_GUI_DOMAIN_BACKEND_GL
		&& backend < CLASSIC_GUI_DOMAIN_BACKEND_COUNT
		? domain.stats.backend[ backend ] : empty;
}

bool R_ClassicGuiDomain_IsLegacyInWorldDrawOwned(
		const viewDef_t *viewDef, classicGuiDomainBackend_t backend,
		const drawSurf_t *drawSurf ) {
	if ( backend < CLASSIC_GUI_DOMAIN_BACKEND_GL
			|| backend >= CLASSIC_GUI_DOMAIN_BACKEND_COUNT || drawSurf == NULL
			|| ( drawSurf->dsFlags & DSF_IN_WORLD_GUI ) == 0 ) {
		return false;
	}
	const classicGuiDomainView_t *view = R_ClassicGuiDomain_FindInWorldView(
		viewDef );
	if ( view == NULL || view->backendOutcome[ backend ]
			!= CLASSIC_GUI_DOMAIN_BACKEND_OWNED ) {
		return false;
	}
	for ( int i = 0; i < view->drawCount; ++i ) {
		const classicGuiDomainDraw_t *draw = R_ClassicGuiDomain_ViewDraw(
			*view, i );
		if ( draw != NULL && draw->legacyDrawSurf == drawSurf ) {
			return true;
		}
	}
	return false;
}

const char *ClassicGuiDomainSourceSurface_Name(
		classicGuiDomainSourceSurface_t sourceSurface ) {
	switch ( sourceSurface ) {
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_DRAWABLE: return "drawable";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NULL_SURFACE: return "noopNullSurface";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_MISSING_MATERIAL: return "noopMissingMaterial";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_NO_AMBIENT: return "noopNoAmbient";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_PORTAL_SKY: return "noopPortalSky";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY: return "noopEmptyGeometry";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_SUPPRESSED_IN_SUBVIEW: return "fallbackSuppressedInSubview";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_MISSING_SPACE: return "fallbackMissingSpace";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DEPTH_HACK: return "fallbackDepthHack";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_DECAL_COLOR_STREAM: return "fallbackDecalColorStream";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_NEGATIVE_SCALE: return "fallbackNegativeScale";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_FALLBACK_POST_PROCESS: return "fallbackPostProcess";
	case CLASSIC_GUI_DOMAIN_SOURCE_SURFACE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicGuiDomainFailure_Name( classicGuiDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_GUI_DOMAIN_FAILURE_NONE: return "none";
	case CLASSIC_GUI_DOMAIN_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_GUI_DOMAIN_FAILURE_SCENE_PACKET_OVERFLOW: return "scenePacketOverflow";
	case CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_TABLE_NOT_PREPARED: return "materialTableNotPrepared";
	case CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_TABLE_OVERFLOW: return "materialTableOverflow";
	case CLASSIC_GUI_DOMAIN_FAILURE_VIEW_POOL_OVERFLOW: return "viewPoolOverflow";
	case CLASSIC_GUI_DOMAIN_FAILURE_DRAW_POOL_OVERFLOW: return "drawPoolOverflow";
	case CLASSIC_GUI_DOMAIN_FAILURE_EVALUATED_PASS_POOL_OVERFLOW: return "evaluatedPassPoolOverflow";
	case CLASSIC_GUI_DOMAIN_FAILURE_UNSUPPORTED_VIEW: return "unsupportedView";
	case CLASSIC_GUI_DOMAIN_FAILURE_INVALID_SCENE_RANGE: return "invalidSceneRange";
	case CLASSIC_GUI_DOMAIN_FAILURE_INVALID_GUI_PASS: return "invalidGuiPass";
	case CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DRAW_RANGE: return "invalidDrawRange";
	case CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_SURFACE_FALLBACK: return "sourceSurfaceFallback";
	case CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH: return "sourcePacketMismatch";
	case CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DRAW_PACKET: return "invalidDrawPacket";
	case CLASSIC_GUI_DOMAIN_FAILURE_DEFORM_CONTRACT: return "deformContract";
	case CLASSIC_GUI_DOMAIN_FAILURE_MISSING_GEOMETRY_RECORD: return "missingGeometryRecord";
	case CLASSIC_GUI_DOMAIN_FAILURE_MISSING_INSTANCE_RECORD: return "missingInstanceRecord";
	case CLASSIC_GUI_DOMAIN_FAILURE_MISSING_MATERIAL_RECORD: return "missingMaterialRecord";
	case CLASSIC_GUI_DOMAIN_FAILURE_STALE_MATERIAL_RECORD: return "staleMaterialRecord";
	case CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_INELIGIBLE: return "materialPassIneligible";
	case CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_COPY_FAILED: return "materialPassCopyFailed";
	case CLASSIC_GUI_DOMAIN_FAILURE_MATERIAL_PASS_EVALUATION_FAILED: return "materialPassEvaluationFailed";
	case CLASSIC_GUI_DOMAIN_FAILURE_TEXTURE_RESOURCE_UNAVAILABLE: return "textureResourceUnavailable";
	case CLASSIC_GUI_DOMAIN_FAILURE_INVALID_DISPOSITION: return "invalidDisposition";
	case CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_NOT_READY: return "backendNotReady";
	case CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_COVERAGE_MISMATCH: return "backendCoverageMismatch";
	case CLASSIC_GUI_DOMAIN_FAILURE_BACKEND_REJECTED: return "backendRejected";
	case CLASSIC_GUI_DOMAIN_FAILURE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicGuiDomainBackend_Name( classicGuiDomainBackend_t backend ) {
	switch ( backend ) {
	case CLASSIC_GUI_DOMAIN_BACKEND_GL: return "GL";
	case CLASSIC_GUI_DOMAIN_BACKEND_VULKAN: return "Vulkan";
	case CLASSIC_GUI_DOMAIN_BACKEND_COUNT:
	default: return "unknown";
	}
}

bool RendererClassicGuiDomain_RunSelfTest( void ) {
	if ( CLASSIC_GUI_DOMAIN_MAX_VIEWS != 64
			|| CLASSIC_GUI_DOMAIN_MAX_DRAWS != 4096
			|| CLASSIC_GUI_DOMAIN_MAX_EVALUATED_PASSES != 8192
			|| !RangeFits( 8191, 1, 8192 ) || RangeFits( 8192, 1, 8192 )
			|| RangeFits( -1, 1, 8192 )
			|| idStr::Cmp( ClassicGuiDomainFailure_Name(
				CLASSIC_GUI_DOMAIN_FAILURE_DEFORM_CONTRACT ),
				"deformContract" ) ) {
		return false;
	}
	rendererEvaluatedMaterialPass_t first;
	std::memset( &first, 0, sizeof( first ) );
	first.sourceStageIndex = 7;
	first.kind = RENDERER_MATERIAL_PASS_GUI;
	first.textureResourceId = 0x123400020003ull;
	first.condition = 1.0f;
	first.active = true;
	first.disposition = RENDERER_MATERIAL_PASS_DRAW;
	first.color[ 0 ] = 1.0f;
	first.textureMatrix[ 0 ] = 1.0f;
	first.textureMatrix[ 4 ] = 1.0f;
	first.blend.sourceColor = RENDERER_BLEND_SRC_ALPHA;
	first.blend.destinationColor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA;
	first.depth.compareOperation = RENDERER_COMPARE_ALWAYS;
	first.alphaTestCompareOperation = RENDERER_COMPARE_GREATER;
	const std::uint64_t firstHash = HashPass( first );
	rendererEvaluatedMaterialPass_t same = first;
	if ( firstHash == 0 || HashPass( same ) != firstHash ) {
		return false;
	}
	same.color[ 0 ] = 0.5f;
	if ( HashPass( same ) == firstHash ) {
		return false;
	}
	same = first;
	same.textureResourceId += 1ull << 32;
	if ( HashPass( same ) != firstHash
			|| idStr::Cmp( ClassicGuiDomainFailure_Name(
				CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH ),
				"sourcePacketMismatch" ) ) {
		return false;
	}
	classicGuiDomainDraw_t deformHashDraw;
	InitDraw( deformHashDraw );
	const std::uint64_t undeformedHash = HashDraw( deformHashDraw );
	deformHashDraw.deformRole = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	deformHashDraw.deformOutcome = CLASSIC_DEFORM_OUTCOME_COMPLETED;
	deformHashDraw.deformContractHash = 0x51234ull;
	if ( HashDraw( deformHashDraw ) == undeformedHash ) {
		return false;
	}

	classicGuiDomainDraw_t dispositionDraw;
	InitDraw( dispositionDraw );
	rendererEvaluatedMaterialPass_t dispositionPass = first;
	dispositionPass.active = false;
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_INACTIVE_CONDITION;
	if ( !CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	dispositionPass.active = true;
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_NOOP_ZERO_ONE_BLEND;
	if ( !CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_NOOP_BLACK_ADDITIVE;
	if ( !CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_NOOP_TRANSPARENT_ALPHA;
	if ( !CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_DRAW;
	if ( !CountDisposition( dispositionDraw, dispositionPass )
			|| dispositionDraw.activePassCount != 4
			|| dispositionDraw.drawablePassCount != 1
			|| dispositionDraw.inactivePassCount != 1
			|| dispositionDraw.activeNoopPassCount != 3
			|| dispositionDraw.noopPassCount != 4 ) {
		return false;
	}
	dispositionPass.disposition =
		static_cast<rendererMaterialPassDisposition_t>( 0x7fffffff );
	if ( CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	InitDraw( dispositionDraw );
	dispositionPass.active = false;
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_DRAW;
	if ( CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}
	InitDraw( dispositionDraw );
	dispositionPass.active = true;
	dispositionPass.disposition = RENDERER_MATERIAL_PASS_INACTIVE_CONDITION;
	if ( CountDisposition( dispositionDraw, dispositionPass ) ) {
		return false;
	}

	const classicGuiDomainStats_t savedStats = domain.stats;
	classicGuiDomainView_t rollbackView;
	InitView( rollbackView, NULL, -1 );
	rollbackView.ready = true;
	rollbackView.firstDraw = 17;
	rollbackView.drawCount = 3;
	rollbackView.firstEvaluatedPass = 29;
	rollbackView.evaluatedPassCount = 5;
	rollbackView.activePassCount = 4;
	rollbackView.drawablePassCount = 2;
	rollbackView.inactivePassCount = 1;
	rollbackView.activeNoopPassCount = 1;
	rollbackView.noopPassCount = 2;
	rollbackView.hash = 1;
	const bool rollbackResult = FailView( rollbackView,
		CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH, 41, 2, 7, 3 );
	const bool rollbackOk = !rollbackResult && !rollbackView.ready
		&& rollbackView.failure == CLASSIC_GUI_DOMAIN_FAILURE_SOURCE_PACKET_MISMATCH
		&& rollbackView.failureDetail == 41
		&& rollbackView.failureSourceSurfaceIndex == 2
		&& rollbackView.failureDrawPacketIndex == 7
		&& rollbackView.failurePassPacketIndex == 3
		&& rollbackView.firstDraw == -1 && rollbackView.drawCount == 0
		&& rollbackView.firstEvaluatedPass == -1
		&& rollbackView.evaluatedPassCount == 0
		&& rollbackView.activePassCount == 0
		&& rollbackView.drawablePassCount == 0
		&& rollbackView.inactivePassCount == 0
		&& rollbackView.activeNoopPassCount == 0
		&& rollbackView.noopPassCount == 0 && rollbackView.hash == 0;
	domain.stats = savedStats;
	if ( !rollbackOk ) {
		return false;
	}

	const int savedViewCount = domain.viewCount;
	const int savedDrawCount = domain.drawCount;
	const classicGuiDomainView_t savedView = domain.views[ 0 ];
	const classicGuiDomainDraw_t savedDraw = domain.draws[ 0 ];
	viewDef_t coverageView;
	std::memset( &coverageView, 0, sizeof( coverageView ) );
	InitView( domain.views[ 0 ], &coverageView, 0 );
	domain.viewCount = 1;
	domain.views[ 0 ].ready = true;
	domain.views[ 0 ].sourceSurfaceCount = 4;
	domain.views[ 0 ].drawablePassCount = 2;
	domain.views[ 0 ].noopPassCount = 3;
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	const bool firstOwned = R_ClassicGuiDomain_RecordOwned( &coverageView,
		CLASSIC_GUI_DOMAIN_BACKEND_GL, 2, 3 );
	const bool repeatedOwned = R_ClassicGuiDomain_RecordOwned( &coverageView,
		CLASSIC_GUI_DOMAIN_BACKEND_GL, 2, 3 );
	const bool mismatchRejected = !R_ClassicGuiDomain_RecordOwned( &coverageView,
		CLASSIC_GUI_DOMAIN_BACKEND_VULKAN, 1, 3 );
	const bool coverageOk = firstOwned && repeatedOwned && mismatchRejected
		&& domain.views[ 0 ].backendOutcome[ CLASSIC_GUI_DOMAIN_BACKEND_GL ]
			== CLASSIC_GUI_DOMAIN_BACKEND_OWNED
		&& domain.views[ 0 ].backendOutcome[ CLASSIC_GUI_DOMAIN_BACKEND_VULKAN ]
			== CLASSIC_GUI_DOMAIN_BACKEND_FALLBACK
		&& domain.stats.backend[ CLASSIC_GUI_DOMAIN_BACKEND_GL ].ownedViews == 1
		&& domain.stats.backend[ CLASSIC_GUI_DOMAIN_BACKEND_GL ].duplicateReports == 1
		&& domain.stats.backend[ CLASSIC_GUI_DOMAIN_BACKEND_VULKAN ].fallbackViews == 1
		&& domain.stats.backend[ CLASSIC_GUI_DOMAIN_BACKEND_VULKAN ].coverageMismatches == 1;

	viewEntity_t inWorldEntity;
	std::memset( &inWorldEntity, 0, sizeof( inWorldEntity ) );
	drawSurf_t inWorldSurface;
	std::memset( &inWorldSurface, 0, sizeof( inWorldSurface ) );
	inWorldSurface.dsFlags = DSF_IN_WORLD_GUI;
	drawSurf_t *inWorldSurfaces[] = { &inWorldSurface };
	viewDef_t inWorldView;
	std::memset( &inWorldView, 0, sizeof( inWorldView ) );
	inWorldView.viewEntitys = &inWorldEntity;
	inWorldView.drawSurfs = inWorldSurfaces;
	inWorldView.numDrawSurfs = 1;
	classicGuiDomainView_t scopeView;
	InitView( scopeView, &inWorldView, 1 );
	const bool scopeOk = scopeView.scope
		== CLASSIC_GUI_DOMAIN_SCOPE_IN_WORLD
		&& scopeView.sourceSurfaceCount == 1
		&& HashView( scopeView ) != 0;

	InitView( domain.views[ 0 ], &inWorldView, 1 );
	domain.viewCount = 1;
	domain.drawCount = 1;
	domain.views[ 0 ].ready = true;
	domain.views[ 0 ].firstDraw = 0;
	domain.views[ 0 ].drawCount = 1;
	domain.views[ 0 ].backendOutcome[ CLASSIC_GUI_DOMAIN_BACKEND_GL ] =
		CLASSIC_GUI_DOMAIN_BACKEND_OWNED;
	InitDraw( domain.draws[ 0 ] );
	domain.draws[ 0 ].legacyDrawSurf = &inWorldSurface;
	const bool ownershipQueryOk =
		R_ClassicGuiDomain_IsLegacyInWorldDrawOwned( &inWorldView,
			CLASSIC_GUI_DOMAIN_BACKEND_GL, &inWorldSurface )
		&& !R_ClassicGuiDomain_IsLegacyInWorldDrawOwned( &inWorldView,
			CLASSIC_GUI_DOMAIN_BACKEND_VULKAN, &inWorldSurface );
	domain.views[ 0 ] = savedView;
	domain.viewCount = savedViewCount;
	domain.draws[ 0 ] = savedDraw;
	domain.drawCount = savedDrawCount;
	domain.stats = savedStats;
	return coverageOk && scopeOk && ownershipQueryOk;
}
