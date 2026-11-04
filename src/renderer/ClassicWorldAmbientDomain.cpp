// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "CelShading.h"
#include "ClassicWorldAmbientDomain.h"

#include <cstring>

namespace {

const std::uint64_t HASH_OFFSET = 1469598103934665603ull;
const std::uint64_t HASH_PRIME = 1099511628211ull;

typedef struct classicWorldAmbientDomainState_s {
	classicWorldAmbientDomainView_t views[ CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_VIEWS ];
	classicWorldAmbientDomainDraw_t draws[ CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_DRAWS ];
	rendererEvaluatedMaterialPass_t passes[ CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES ];
	classicWorldAmbientDomainStats_t stats;
	int viewCount;
	int drawCount;
	int passCount;
} classicWorldAmbientDomainState_t;

classicWorldAmbientDomainState_t domain;

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
	// The table generation is deliberately excluded so identical contracts hash
	// equally across frames. Low bits retain record/binding identity.
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

static std::uint64_t HashDraw( const classicWorldAmbientDomainDraw_t &draw ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, draw.sourceSurfaceIndex );
	HashInt( hash, draw.sourceSurface );
	HashInt( hash, draw.phase );
	HashBool( hash, draw.packetBacked );
	HashBool( hash, draw.depthPrerequisite );
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

static std::uint64_t HashView( const classicWorldAmbientDomainView_t &view ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, view.sourceSurfaceCount );
	HashInt( hash, view.drawableSurfaceCount );
	HashInt( hash, view.noopSurfaceCount );
	HashInt( hash, view.packetDrawCount );
	HashInt( hash, view.depthPacketDrawCount );
	HashInt( hash, view.evaluatedPassCount );
	HashInt( hash, view.drawablePassCount );
	HashInt( hash, view.noopPassCount );
	HashInt( hash, view.materialDeformSurfaceCount );
	HashInt( hash, view.completedDeformSurfaceCount );
	HashInt( hash, view.emptyDeformSurfaceCount );
	for ( int phase = 0; phase < CLASSIC_WORLD_AMBIENT_PHASE_COUNT; ++phase ) {
		HashInt( hash, view.phaseSurfaceCount[ phase ] );
		HashInt( hash, view.phaseDrawableSurfaceCount[ phase ] );
		HashInt( hash, view.phaseDrawablePassCount[ phase ] );
		HashInt( hash, view.phaseNoopPassCount[ phase ] );
	}
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

static void InitDraw( classicWorldAmbientDomainDraw_t &draw ) {
	std::memset( &draw, 0, sizeof( draw ) );
	draw.phase = CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG;
	draw.drawPacketIndex = -1;
	draw.depthDrawPacketIndex = -1;
	draw.materialTableRecordIndex = -1;
	draw.materialId = -1;
	draw.firstWorldPass = -1;
	draw.firstEvaluatedPass = -1;
}

static void InitView( classicWorldAmbientDomainView_t &view,
		const viewDef_t *viewDef, int scenePacketIndex ) {
	std::memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.scenePacketIndex = scenePacketIndex;
	view.ambientPassPacketIndex = -1;
	view.depthPassPacketIndex = -1;
	view.firstDraw = -1;
	view.firstEvaluatedPass = -1;
	view.failurePassPacketIndex = -1;
	view.failureDrawPacketIndex = -1;
	view.failureSourceSurfaceIndex = -1;
	view.failureSourceStageIndex = -1;
	view.worldPassFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE;
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
		view.sourceSurfaceCount = viewDef->numDrawSurfs;
	}
}

static bool FailView( classicWorldAmbientDomainView_t &view,
		classicWorldAmbientDomainFailure_t failure, int detail,
		int sourceSurface = -1, int drawPacket = -1, int passPacket = -1,
		int sourceStage = -1,
		materialResourceWorldPassFailure_t worldFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE,
		rendererMaterialPassEvaluationStatus_t evaluationStatus = RENDERER_MATERIAL_PASS_EVALUATION_SUCCESS ) {
	view.ready = false;
	view.failure = failure;
	view.failureDetail = detail;
	view.worldPassFailure = worldFailure;
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
	for ( int phase = 0; phase < CLASSIC_WORLD_AMBIENT_PHASE_COUNT; ++phase ) {
		view.phaseSurfaceCount[ phase ] = 0;
		view.phaseDrawableSurfaceCount[ phase ] = 0;
		view.phaseDrawablePassCount[ phase ] = 0;
		view.phaseNoopPassCount[ phase ] = 0;
	}
	view.hash = 0;
	domain.stats.fallbackViews++;
	if ( failure >= CLASSIC_WORLD_AMBIENT_FAILURE_NONE
			&& failure < CLASSIC_WORLD_AMBIENT_FAILURE_COUNT ) {
		domain.stats.failureCounts[ failure ]++;
	}
	if ( failure == CLASSIC_WORLD_AMBIENT_FAILURE_SCENE_PACKET_OVERFLOW
			|| failure == CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_TABLE_OVERFLOW
			|| failure == CLASSIC_WORLD_AMBIENT_FAILURE_VIEW_POOL_OVERFLOW
			|| failure == CLASSIC_WORLD_AMBIENT_FAILURE_DRAW_POOL_OVERFLOW
			|| failure == CLASSIC_WORLD_AMBIENT_FAILURE_EVALUATED_PASS_POOL_OVERFLOW ) {
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

static bool MatrixHasNegativeScale( const float matrix[ 16 ] ) {
	const float determinant =
		matrix[ 0 ] * ( matrix[ 5 ] * matrix[ 10 ] - matrix[ 9 ] * matrix[ 6 ] )
		- matrix[ 4 ] * ( matrix[ 1 ] * matrix[ 10 ] - matrix[ 9 ] * matrix[ 2 ] )
		+ matrix[ 8 ] * ( matrix[ 1 ] * matrix[ 6 ] - matrix[ 5 ] * matrix[ 2 ] );
	return determinant < 0.0f;
}

static classicWorldAmbientPhase_t PhaseForSurface( const drawSurf_t *drawSurf ) {
	const idMaterial *material = drawSurf != NULL ? drawSurf->material : NULL;
	return material != NULL && material->GetSort() >= SS_MEDIUM
		? CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG
		: CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG;
}

static bool SourceSurfaceIsFallback(
		classicWorldAmbientDomainSourceSurface_t classification ) {
	return classification >= CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NOT_DRAWN
		&& classification < CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_COUNT;
}

static bool SourceSurfaceHasPlayerVisibilityWork( const drawSurf_t *drawSurf ) {
	if ( r_skipPlayerVisibilityEffects.GetBool() || drawSurf == NULL
			|| drawSurf->space == NULL || drawSurf->space->entityDef == NULL
			|| drawSurf->geo == NULL || drawSurf->geo->numIndexes <= 0
			|| drawSurf->material == NULL
			|| ( drawSurf->dsFlags & DSF_BSE_EFFECT ) != 0
			|| drawSurf->space->weaponDepthHack
			|| drawSurf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	if ( !material->IsDrawn() || material->IsPortalSky()
			|| material->SuppressInSubview()
			|| material->GetSort() >= SS_POST_PROCESS || material->HasGui() ) {
		return false;
	}
	const renderEntity_t &entity = drawSurf->space->entityDef->parms;
	return entity.brightSkinColor[ 3 ] > 0.0f
		|| entity.rimlightColor[ 3 ] > 0.0f
		|| ( entity.outlineColor[ 3 ] > 0.0f && entity.outlineWidth > 0.0f );
}

static classicWorldAmbientDomainSourceSurface_t ClassifySourceSurface(
		const viewDef_t *viewDef, const drawSurf_t *drawSurf,
		bool &packetExpected, classicWorldAmbientPhase_t &phase ) {
	packetExpected = false;
	phase = PhaseForSurface( drawSurf );
	if ( drawSurf == NULL ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NULL_SURFACE;
	}
	if ( ( drawSurf->dsFlags & ( DSF_BSE_EFFECT | DSF_OUTLINE_ONLY ) ) != 0
			|| drawSurf->dynamicTexCoords != NULL
			|| drawSurf->texGenTransformAndViewOrg != NULL ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SPECIAL_SURFACE;
	}
	if ( drawSurf->geo != NULL && drawSurf->geo->deformedSurface ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEFORM;
	}
	// Viewmodel/synthetic GUI transforms are domain boundaries even when their
	// material would otherwise be an ambient no-op. Admitting them as no-ops
	// would make packet omission hide a view-level ownership blocker.
	if ( drawSurf->space != NULL ) {
		if ( drawSurf->space->weaponDepthHack
				|| drawSurf->space->modelDepthHack != 0.0f ) {
			return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEPTH_HACK;
		}
		if ( MatrixHasNegativeScale( drawSurf->space->modelMatrix ) ) {
			return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NEGATIVE_SCALE;
		}
		if ( viewDef != NULL && drawSurf->space->entityDef == NULL
				&& drawSurf->space != &viewDef->worldSpace ) {
			return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SYNTHETIC_GUI_SPACE;
		}
	}
	const idMaterial *material = drawSurf->material;
	if ( material == NULL ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_MISSING_MATERIAL;
	}
	if ( material->Deform() != DFRM_NONE
			&& !r_rendererSharedDeform.GetBool() ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEFORM;
	}
	if ( material->GetSort() >= SS_POST_PROCESS ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_POST_PROCESS;
	}
	if ( material->IsPortalSky() ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_PORTAL_SKY;
	}
	if ( material->SuppressInSubview() ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SUPPRESSED_IN_SUBVIEW;
	}
	if ( material->HasGui() || material->GetSort() == SS_GUI
			|| material->GetSort() == SS_PREGUI ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_IN_WORLD_GUI;
	}
	if ( material->HasSubview() || material->GetSort() == SS_SUBVIEW ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SUBVIEW;
	}
	if ( drawSurf->decalColorCache != NULL || drawSurf->decalColorStageCount != 0
			|| drawSurf->decalColorStride != 0 || drawSurf->decalColorOffset != 0
			|| ( material->GetSort() >= SS_DECAL && material->GetSort() < SS_FAR ) ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DECAL;
	}
	if ( SourceSurfaceHasPlayerVisibilityWork( drawSurf ) ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_PLAYER_VISIBILITY;
	}
	if ( !material->HasAmbient() ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NO_AMBIENT;
	}
	if ( !material->IsDrawn() ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NOT_DRAWN;
	}
	if ( drawSurf->geo == NULL || drawSurf->geo->numVerts <= 0
			|| drawSurf->geo->numIndexes <= 0 ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY;
	}
	if ( drawSurf->space == NULL ) {
		return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_MISSING_SPACE;
	}
	packetExpected = true;
	return CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_DRAWABLE;
}

static bool CountDisposition( classicWorldAmbientDomainDraw_t &draw,
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

static bool ValidateAmbientDrawPacket( const idScenePacketFrame &packetFrame,
		int packetIndex, const viewDef_t *viewDef, const drawSurf_t *drawSurf,
		classicWorldAmbientDomainFailure_t &failure ) {
	if ( packetIndex < 0 || packetIndex >= packetFrame.NumDrawPackets() ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH;
		return false;
	}
	const drawPacket_t &packet = packetFrame.DrawPacket( packetIndex );
	if ( packet.passCategory != RENDER_PASS_AMBIENT
			|| packet.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| packet.viewDef != viewDef || packet.legacyDrawSurf != drawSurf ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH;
		return false;
	}
	if ( packet.geometryRecordIndex < 0
			|| packet.geometryRecordIndex >= packetFrame.NumGeometryRecords()
			|| packet.geometryRecord == NULL
			|| packet.geometryRecord != &packetFrame.GeometryRecord( packet.geometryRecordIndex )
			|| packet.geometryRecord->legacyGeometry != drawSurf->geo ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_GEOMETRY_RECORD;
		return false;
	}
	if ( packet.instanceRecordIndex < 0
			|| packet.instanceRecordIndex >= packetFrame.NumInstanceRecords()
			|| packet.instanceRecord == NULL
			|| packet.instanceRecord != &packetFrame.InstanceRecord( packet.instanceRecordIndex )
			|| packet.instanceRecord->legacySpace != drawSurf->space ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_INSTANCE_RECORD;
		return false;
	}
	if ( packet.materialRecordIndex < 0
			|| packet.materialRecordIndex >= packetFrame.NumMaterialRecords()
			|| packet.materialRecord == NULL
			|| packet.materialRecord != &packetFrame.MaterialRecord( packet.materialRecordIndex )
			|| packet.materialRecord->material != drawSurf->material ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_MATERIAL_RECORD;
		return false;
	}
	const geometryResourceRecord_t &geometry = *packet.geometryRecord;
	if ( !packet.hasGeometry || packet.vertexCount <= 0 || packet.indexCount <= 0
			|| geometry.vertexCount != packet.vertexCount
			|| geometry.indexCount != packet.indexCount
			|| ( geometry.deformMode != GEOMETRY_DEFORM_NONE
				&& !( r_rendererSharedDeform.GetBool()
					&& MaterialRequestsClassicDeform( drawSurf ) ) )
			|| geometry.legacyGeometry == NULL || geometry.legacyGeometry->ambientCache == NULL
			|| !packet.hasAmbientCache
			|| ( !packet.hasIndexCache && !geometry.hasClientIndexData ) ) {
		failure = geometry.deformMode != GEOMETRY_DEFORM_NONE
			&& !( r_rendererSharedDeform.GetBool()
				&& MaterialRequestsClassicDeform( drawSurf ) )
			? CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK
			: CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_GEOMETRY_RECORD;
		return false;
	}
	const instanceRecord_t &instance = *packet.instanceRecord;
	if ( ( instance.visibilityFlags & INSTANCE_VISIBILITY_WORLD ) == 0
			|| ( instance.visibilityFlags & ( INSTANCE_VISIBILITY_GUI
				| INSTANCE_VISIBILITY_VIEWMODEL | INSTANCE_VISIBILITY_SUBVIEW
				| INSTANCE_VISIBILITY_REMOTE_CAMERA | INSTANCE_VISIBILITY_RENDER_DEMO ) ) != 0
			|| instance.weaponDepthHack || instance.modelDepthHack != 0.0f ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK;
		return false;
	}
	if ( instance.negativeScale ) {
		failure = CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK;
		return false;
	}
	return true;
}

static bool ValidateDepthPrerequisite( const idScenePacketFrame &packetFrame,
		const passPacket_t *depthPass, const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const drawPacket_t &ambientPacket,
		int &depthPacketCursor, int &depthPacketIndex ) {
	depthPacketIndex = -1;
	if ( drawSurf == NULL || drawSurf->material == NULL
			|| drawSurf->material->Coverage() == MC_TRANSLUCENT ) {
		return true;
	}
	if ( depthPass == NULL || !depthPass->enabled || depthPass->commandOnly
			|| !RangeFits( depthPass->firstDrawPacket, depthPass->drawPacketCount,
				packetFrame.NumDrawPackets() ) ) {
		return false;
	}
	const int depthEnd = depthPass->firstDrawPacket + depthPass->drawPacketCount;
	if ( depthPacketCursor < depthPass->firstDrawPacket
			|| depthPacketCursor > depthEnd ) {
		return false;
	}
	for ( int packetIndex = depthPacketCursor;
			packetIndex < depthEnd; ++packetIndex ) {
		const drawPacket_t &packet = packetFrame.DrawPacket( packetIndex );
		if ( packet.legacyDrawSurf != drawSurf ) {
			continue;
		}
		if ( packet.passCategory != RENDER_PASS_DEPTH
				|| packet.packetCategory != SCENE_PACKET_CATEGORY_WORLD
				|| packet.viewDef != viewDef || packet.materialRecordIndex != ambientPacket.materialRecordIndex
				|| packet.geometryRecordIndex != ambientPacket.geometryRecordIndex
				|| packet.instanceRecordIndex != ambientPacket.instanceRecordIndex
				|| packet.materialRecord != ambientPacket.materialRecord
				|| packet.geometryRecord != ambientPacket.geometryRecord
				|| packet.instanceRecord != ambientPacket.instanceRecord
				|| packet.vertexCount != ambientPacket.vertexCount
				|| packet.firstIndex != ambientPacket.firstIndex
				|| packet.indexCount != ambientPacket.indexCount
				|| packet.vertexOffset != ambientPacket.vertexOffset
				|| !packet.hasGeometry || !packet.hasAmbientCache
				|| ( !packet.hasIndexCache && !packet.geometryRecord->hasClientIndexData ) ) {
			return false;
		}
		depthPacketIndex = packetIndex;
		depthPacketCursor = packetIndex + 1;
		return true;
	}
	return false;
}

static bool ForbiddenPassCategory( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_STENCIL_SHADOW:
	case RENDER_PASS_SHADOW_MAP:
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_LIGHT_GRID:
	case RENDER_PASS_DEFERRED_RESOLVE:
	case RENDER_PASS_FORWARD_PLUS:
	case RENDER_PASS_FOG_BLEND:
	case RENDER_PASS_SSAO:
	case RENDER_PASS_MOTION_BLUR:
	case RENDER_PASS_BLOOM:
	case RENDER_PASS_AUTHORED_POST:
	case RENDER_PASS_SPECIAL_EFFECTS:
	case RENDER_PASS_GUI:
	case RENDER_PASS_PRESENT:
		return true;
	case RENDER_PASS_DEPTH:
	case RENDER_PASS_AMBIENT:
		return false;
	default:
		return true;
	}
}

static bool R_ClassicWorldAmbientDomain_PrepareView(
		const idScenePacketFrame &packetFrame, const scenePacket_t &scene,
		classicWorldAmbientDomainView_t &view ) {
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	const materialResourceTableStats_t &tableStats = R_MaterialResourceTable_Stats();
	if ( packetStats.overflow ) {
		return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_SCENE_PACKET_OVERFLOW,
			packetStats.overflowCause );
	}
	if ( !tableStats.prepared ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_TABLE_NOT_PREPARED, 0 );
	}
	if ( !tableStats.available ) {
		return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_UNAVAILABLE, 0 );
	}
	if ( tableStats.overflow ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_TABLE_OVERFLOW, 0 );
	}

	const viewDef_t *viewDef = view.viewDef;
	const int allowedRenderFlags = RF_NO_GUI | RF_PENUMBRA_MAP | RF_PRIMARY_VIEW;
	if ( viewDef == NULL || scene.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| viewDef->viewEntitys == NULL || viewDef->renderWorld == NULL
			|| viewDef->isSubview || viewDef->isMirror || viewDef->isXraySubview
			|| viewDef->isEditor || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->numClipPlanes != 0
			|| viewDef->renderView.viewID < 0
			|| viewDef->renderView.globalMaterial != NULL
			|| ( viewDef->renderFlags & ~allowedRenderFlags ) != 0
			|| viewDef->numOutlineDrawSurfs != 0 || r_skipAmbient.GetBool() ) {
		return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_UNSUPPORTED_VIEW,
			viewDef != NULL ? viewDef->renderFlags : 0 );
	}
	if ( r_portalsDistanceCull.GetBool() ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_UNPACKETIZED_VIEW_EFFECT, 1 );
	}
	if ( r_forceAmbient.GetFloat() > 0.0f ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_UNPACKETIZED_VIEW_EFFECT, 2 );
	}
	if ( R_CelShadingAnyEnabled() ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_UNPACKETIZED_VIEW_EFFECT, 3 );
	}
	if ( viewDef->numDrawSurfs < 0
			|| ( viewDef->numDrawSurfs > 0 && viewDef->drawSurfs == NULL ) ) {
		return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DRAW_RANGE,
			viewDef->numDrawSurfs );
	}
	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() )
			|| !RangeFits( scene.firstDrawPacket, scene.drawPacketCount,
				packetFrame.NumDrawPackets() ) ) {
		return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_SCENE_RANGE,
			scene.passPacketCount );
	}

	const passPacket_t *ambientPass = NULL;
	const passPacket_t *depthPass = NULL;
	for ( int localPass = 0; localPass < scene.passPacketCount; ++localPass ) {
		const int passPacketIndex = scene.firstPassPacket + localPass;
		const passPacket_t &pass = packetFrame.Pass( passPacketIndex );
		if ( !RangeFits( pass.firstDrawPacket, pass.drawPacketCount,
				packetFrame.NumDrawPackets() )
				|| pass.firstDrawPacket < scene.firstDrawPacket
				|| pass.firstDrawPacket
					> scene.firstDrawPacket + scene.drawPacketCount
				|| pass.drawPacketCount > scene.firstDrawPacket
					+ scene.drawPacketCount - pass.firstDrawPacket ) {
			return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DRAW_RANGE,
				pass.drawPacketCount, -1, -1, passPacketIndex );
		}
		if ( pass.passCategory == RENDER_PASS_AMBIENT ) {
			if ( ambientPass != NULL || !pass.enabled || pass.commandOnly
					|| ( pass.packetCategory != SCENE_PACKET_CATEGORY_WORLD
						&& !( pass.drawPacketCount == 0
							&& pass.packetCategory
								== SCENE_PACKET_CATEGORY_COMMAND ) ) ) {
				return FailView( view,
					CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_AMBIENT_PASS,
					pass.passCategory, -1, -1, passPacketIndex );
			}
			ambientPass = &pass;
			view.ambientPassPacketIndex = passPacketIndex;
			continue;
		}
		if ( pass.passCategory == RENDER_PASS_DEPTH ) {
			if ( depthPass != NULL || !pass.enabled || pass.commandOnly
					|| ( pass.packetCategory != SCENE_PACKET_CATEGORY_WORLD
						&& !( pass.drawPacketCount == 0
							&& pass.packetCategory
								== SCENE_PACKET_CATEGORY_COMMAND ) ) ) {
				return FailView( view,
					CLASSIC_WORLD_AMBIENT_FAILURE_DEPTH_PREREQUISITE_MISSING,
					pass.passCategory, -1, -1, passPacketIndex );
			}
			depthPass = &pass;
			view.depthPassPacketIndex = passPacketIndex;
			view.depthPacketDrawCount = pass.drawPacketCount;
			continue;
		}
		const bool hasWork = pass.commandOnly || pass.drawPacketCount > 0;
		if ( hasWork && ForbiddenPassCategory( pass.passCategory ) ) {
			return FailView( view, CLASSIC_WORLD_AMBIENT_FAILURE_FORBIDDEN_PASS,
				pass.passCategory, -1, -1, passPacketIndex );
		}
	}
	if ( ambientPass == NULL ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_AMBIENT_PASS, -1 );
	}
	view.packetDrawCount = ambientPass->drawPacketCount;
	if ( !RangeFits( domain.drawCount, viewDef->numDrawSurfs,
			CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_DRAWS ) ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_DRAW_POOL_OVERFLOW,
			viewDef->numDrawSurfs );
	}

	const int drawCheckpoint = domain.drawCount;
	const int passCheckpoint = domain.passCount;
	int stagedPassCount = 0;
	int packetCursor = ambientPass->firstDrawPacket;
	const int packetEnd = ambientPass->firstDrawPacket
		+ ambientPass->drawPacketCount;
	int depthPacketCursor = depthPass != NULL
		? depthPass->firstDrawPacket : 0;
	int drawableSurfaces = 0;
	int noopSurfaces = 0;

	for ( int sourceIndex = 0; sourceIndex < viewDef->numDrawSurfs;
			++sourceIndex ) {
		const drawSurf_t *source = viewDef->drawSurfs[ sourceIndex ];
		bool packetExpected = false;
		classicWorldAmbientPhase_t phase = CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG;
		const classicWorldAmbientDomainSourceSurface_t classification =
			ClassifySourceSurface( viewDef, source, packetExpected, phase );
		classicWorldAmbientDomainDraw_t &draw =
			domain.draws[ drawCheckpoint + sourceIndex ];
		InitDraw( draw );
		draw.sourceSurfaceIndex = sourceIndex;
		draw.sourceSurface = classification;
		draw.phase = phase;
		draw.legacyDrawSurf = source;

		if ( SourceSurfaceIsFallback( classification ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK,
				classification, sourceIndex );
		}

		const drawPacket_t *packet = NULL;
		// Modern lighting can emit an ambient packet for PBR or eligible fixed
		// materials with no legacy ambient stage. It is still a classic no-op,
		// but consume and validate its record so it cannot shift the following
		// surface's packet or appear as an unexplained trailing draw. A missing
		// optional packet does not create work for the classic ambient owner.
		const bool optionalPacket =
			classification == CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NO_AMBIENT
				&& packetCursor < packetEnd
				&& packetFrame.DrawPacket( packetCursor ).legacyDrawSurf == source;
		if ( packetExpected || optionalPacket ) {
			classicWorldAmbientDomainFailure_t packetFailure =
				CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DRAW_PACKET;
			if ( packetCursor >= packetEnd
					|| !ValidateAmbientDrawPacket( packetFrame, packetCursor,
						viewDef, source, packetFailure ) ) {
				return FailView( view, packetFailure, packetCursor,
					sourceIndex, packetCursor, view.ambientPassPacketIndex );
			}
			packet = &packetFrame.DrawPacket( packetCursor++ );
		}
		// Consuming modern-only metadata must preserve the classic no-op
		// contract: no owned packet, depth prerequisite or evaluated passes.
		if ( packetExpected ) {
			draw.packetBacked = true;
			draw.drawPacketIndex = packetCursor - 1;
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
			std::memcpy( draw.modelViewMatrix,
				packet->instanceRecord->modelViewMatrix,
				sizeof( draw.modelViewMatrix ) );
		}

		if ( !ValidateFinalizedDeformContract( source, packet,
				draw.deformRole, draw.deformOutcome,
				draw.deformContractHash ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_DEFORM_CONTRACT,
				DeformContractFailureDetail( source ), sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}

		if ( classification != CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_DRAWABLE ) {
			noopSurfaces++;
			draw.hash = HashDraw( draw );
			continue;
		}
		drawableSurfaces++;
		if ( packet == NULL || !packet->hasShaderRegisters
				|| packet->instanceRecord == NULL
				|| !packet->instanceRecord->hasShaderRegisters
				|| packet->instanceRecord->legacyShaderRegisters == NULL
				|| packet->instanceRecord->legacyShaderRegisters
					!= source->shaderRegisters ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_INSTANCE_RECORD, 0,
				sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex );
		}
		if ( packet->instanceRecord->negativeScale
				|| packet->instanceRecord->weaponDepthHack
				|| packet->instanceRecord->modelDepthHack != 0.0f ) {
			const int detail = packet->instanceRecord->negativeScale
				? CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NEGATIVE_SCALE
				: CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEPTH_HACK;
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK,
				detail, sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex );
		}
		if ( packet->geometryRecord == NULL
				|| ( packet->geometryRecord->deformMode
						!= GEOMETRY_DEFORM_NONE
					&& draw.deformContractHash == 0 ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK,
				CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEFORM,
				sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex );
		}
		if ( packet->geometryRecord->skinningMode != GEOMETRY_SKINNING_NONE ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK,
				CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SKINNING,
				sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex );
		}

		if ( !ValidateDepthPrerequisite( packetFrame, depthPass, viewDef,
				source, *packet, depthPacketCursor,
				draw.depthDrawPacketIndex ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_DEPTH_PREREQUISITE_MISSING,
				source->material->Coverage(), sourceIndex,
				draw.drawPacketIndex, view.depthPassPacketIndex );
		}
		draw.depthPrerequisite = source->material->Coverage() != MC_TRANSLUCENT;

		const materialResourceTableRecord_t *materialRecord =
			R_MaterialResourceTable_RecordForIndex(
				packet->materialRecordIndex );
		if ( materialRecord == NULL ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_MATERIAL_RECORD,
				packet->materialRecordIndex, sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}
		if ( materialRecord->sourceMaterialRecordIndex
				!= packet->materialRecordIndex
				|| materialRecord->material != source->material
				|| materialRecord->tableGeneration == 0
				|| ( view.tableGeneration != 0
					&& view.tableGeneration != materialRecord->tableGeneration ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_STALE_MATERIAL_RECORD,
				packet->materialRecordIndex, sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}
		if ( packet->instanceRecord->shaderRegisterCount
				!= materialRecord->registerCount ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_INSTANCE_RECORD,
				packet->instanceRecord->shaderRegisterCount, sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}
		view.tableGeneration = materialRecord->tableGeneration;
		draw.tableGeneration = materialRecord->tableGeneration;
		draw.materialId = materialRecord->materialId;
		draw.firstWorldPass = materialRecord->firstWorldPass;
		draw.worldPassCount = materialRecord->worldPassCount;
		if ( !materialRecord->worldDomainReferenced
				|| !materialRecord->worldPassEligible
				|| materialRecord->worldPassFailure
					!= MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
				|| materialRecord->firstWorldPass < 0
				|| materialRecord->worldPassCount <= 0
				|| !R_MaterialResourceTable_WorldPassEligible(
					*materialRecord ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_INELIGIBLE,
				materialRecord->worldPassFailure, sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex,
				materialRecord->worldPassFailureStage,
				materialRecord->worldPassFailure );
		}
		int directWorldPassCount = 0;
		const rendererMaterialPass_t *directWorldPasses =
			R_MaterialResourceTable_WorldPasses( *materialRecord,
				directWorldPassCount );
		if ( directWorldPasses == NULL
				|| directWorldPassCount != materialRecord->worldPassCount ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_COPY_FAILED,
				directWorldPassCount, sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex );
		}

		rendererMaterialPassList_t compiled;
		if ( !R_MaterialResourceTable_CopyWorldPassList( *materialRecord,
				compiled )
				|| compiled.count != static_cast<std::uint32_t>(
					materialRecord->worldPassCount ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_COPY_FAILED,
				materialRecord->worldPassCount, sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}
		rendererEvaluatedMaterialPassList_t evaluated;
		const rendererMaterialPassEvaluationStatus_t evaluationStatus =
			RendererContracts_EvaluateMaterialPassList( evaluated, compiled,
				packet->instanceRecord->legacyShaderRegisters,
				materialRecord->registerCount > 0
					? static_cast<std::uint32_t>( materialRecord->registerCount )
					: 0u );
		if ( evaluationStatus != RENDERER_MATERIAL_PASS_EVALUATION_SUCCESS
				|| evaluated.count != compiled.count ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_EVALUATION_FAILED,
				evaluationStatus, sourceIndex, draw.drawPacketIndex,
				view.ambientPassPacketIndex, -1,
				MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE, evaluationStatus );
		}
		if ( !RangeFits( passCheckpoint + stagedPassCount,
				static_cast<int>( evaluated.count ),
				CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES ) ) {
			return FailView( view,
				CLASSIC_WORLD_AMBIENT_FAILURE_EVALUATED_PASS_POOL_OVERFLOW,
				static_cast<int>( evaluated.count ), sourceIndex,
				draw.drawPacketIndex, view.ambientPassPacketIndex );
		}
		draw.firstEvaluatedPass = passCheckpoint + stagedPassCount;
		draw.evaluatedPassCount = static_cast<int>( evaluated.count );
		for ( int passIndex = 0; passIndex < draw.evaluatedPassCount;
				++passIndex ) {
			const rendererEvaluatedMaterialPass_t &pass =
				evaluated.passes[ passIndex ];
			if ( pass.kind != RENDERER_MATERIAL_PASS_SURFACE
					|| pass.programFamily != RENDERER_PROGRAM_FIXED
					|| !pass.depth.testEnabled
					|| pass.texgen != RENDERER_TEXGEN_EXPLICIT ) {
				return FailView( view,
					CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_INELIGIBLE,
					passIndex, sourceIndex, draw.drawPacketIndex,
					view.ambientPassPacketIndex, pass.sourceStageIndex );
			}
			const materialResourceTextureBinding_t *binding =
				R_MaterialResourceTable_ResolveTextureResource(
					pass.textureResourceId );
			if ( binding == NULL
					|| binding->textureResourceId != pass.textureResourceId
					|| !binding->loaded || binding->missing
					|| binding->defaulted ) {
				return FailView( view,
					CLASSIC_WORLD_AMBIENT_FAILURE_TEXTURE_RESOURCE_UNAVAILABLE,
					passIndex, sourceIndex, draw.drawPacketIndex,
					view.ambientPassPacketIndex, pass.sourceStageIndex );
			}
			if ( !CountDisposition( draw, pass ) ) {
				return FailView( view,
					CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DISPOSITION,
					pass.disposition, sourceIndex, draw.drawPacketIndex,
					view.ambientPassPacketIndex, pass.sourceStageIndex );
			}
			domain.passes[ draw.firstEvaluatedPass + passIndex ] = pass;
		}
		stagedPassCount += draw.evaluatedPassCount;
		draw.hash = HashDraw( draw );
	}

	if ( packetCursor != packetEnd ) {
		return FailView( view,
			CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor, viewDef->numDrawSurfs, packetCursor,
			view.ambientPassPacketIndex );
	}

	view.firstDraw = drawCheckpoint;
	view.drawCount = viewDef->numDrawSurfs;
	view.drawableSurfaceCount = drawableSurfaces;
	view.noopSurfaceCount = noopSurfaces;
	view.firstEvaluatedPass = passCheckpoint;
	view.evaluatedPassCount = stagedPassCount;
	for ( int i = 0; i < view.drawCount; ++i ) {
		const classicWorldAmbientDomainDraw_t &draw =
			domain.draws[ view.firstDraw + i ];
		const int phase = static_cast<int>( draw.phase );
		view.phaseSurfaceCount[ phase ]++;
		if ( draw.sourceSurface
				== CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_DRAWABLE ) {
			view.phaseDrawableSurfaceCount[ phase ]++;
		}
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
		view.phaseDrawablePassCount[ phase ] += draw.drawablePassCount;
		view.phaseNoopPassCount[ phase ] += draw.noopPassCount;
	}
	domain.drawCount += view.drawCount;
	domain.passCount += view.evaluatedPassCount;
	view.hash = HashView( view );
	view.failure = CLASSIC_WORLD_AMBIENT_FAILURE_NONE;
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
	for ( int phase = 0; phase < CLASSIC_WORLD_AMBIENT_PHASE_COUNT; ++phase ) {
		domain.stats.phaseDrawablePasses[ phase ]
			+= view.phaseDrawablePassCount[ phase ];
		domain.stats.phaseNoopPasses[ phase ]
			+= view.phaseNoopPassCount[ phase ];
	}
	return true;
}

static int ViewIndex( const classicWorldAmbientDomainView_t *view ) {
	// O(1) equivalent of the linear pool scan: a pointer identifies a live view
	// exactly when it is element [index] of the pool with index < viewCount.
	const auto index = view - domain.views;
	return index >= 0 && index < domain.viewCount
		&& view == &domain.views[ index ] ? static_cast<int>( index ) : -1;
}

static int DrawIndex( const classicWorldAmbientDomainDraw_t *draw ) {
	const auto index = draw - domain.draws;
	return index >= 0 && index < domain.drawCount
		&& draw == &domain.draws[ index ] ? static_cast<int>( index ) : -1;
}

static classicWorldAmbientDomainView_t *FindMutableView(
		const viewDef_t *viewDef ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].viewDef == viewDef ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

static bool SceneIsWorldAmbientCandidate( const idScenePacketFrame &packetFrame,
		const scenePacket_t &scene ) {
	if ( scene.viewDef == NULL
			|| scene.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| scene.viewDef->viewEntitys == NULL ) {
		return false;
	}
	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() ) ) {
		return true;
	}
	for ( int passIndex = 0; passIndex < scene.passPacketCount; ++passIndex ) {
		if ( packetFrame.Pass( scene.firstPassPacket + passIndex ).passCategory
				== RENDER_PASS_AMBIENT ) {
			return true;
		}
	}
	// Supplemental world command scenes (post, present, special effects) may
	// legitimately reuse the viewDef. They are outside this ambient owner.
	return false;
}

static void RecordFallback( classicWorldAmbientDomainView_t *view,
		classicWorldAmbientDomainBackend_t backend,
		classicWorldAmbientDomainFailure_t failure, int detail,
		int preFogDrawnPasses, int postFogDrawnPasses, int noopPasses ) {
	classicWorldAmbientDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view == NULL ) {
		coverage.untrackedFallbacks++;
		return;
	}
	const int viewIndex = ViewIndex( view );
	if ( viewIndex < 0
			|| view->backendOutcome[ backend ]
				!= CLASSIC_WORLD_AMBIENT_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return;
	}
	view->backendOutcome[ backend ] = CLASSIC_WORLD_AMBIENT_BACKEND_FALLBACK;
	view->backendFailure[ backend ] = failure == CLASSIC_WORLD_AMBIENT_FAILURE_NONE
		? CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED : failure;
	view->backendFailureDetail[ backend ] = detail;
	view->backendDrawnPasses[ backend ][ CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] =
		preFogDrawnPasses;
	view->backendDrawnPasses[ backend ][ CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] =
		postFogDrawnPasses;
	view->backendNoopPasses[ backend ] = noopPasses;
	coverage.fallbackViewMask |= 1ull << viewIndex;
	coverage.fallbackViews++;
	coverage.fallbackSourceSurfaces += view->sourceSurfaceCount;
	coverage.fallbackDrawablePasses += view->drawablePassCount;
	coverage.fallbackNoopPasses += view->noopPassCount;
	for ( int phase = 0; phase < CLASSIC_WORLD_AMBIENT_PHASE_COUNT; ++phase ) {
		coverage.fallbackPhaseDrawablePasses[ phase ]
			+= view->phaseDrawablePassCount[ phase ];
	}
	if ( failure == CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_COVERAGE_MISMATCH ) {
		coverage.coverageMismatches++;
	}
	if ( view->backendFailure[ backend ] >= CLASSIC_WORLD_AMBIENT_FAILURE_NONE
			&& view->backendFailure[ backend ]
				< CLASSIC_WORLD_AMBIENT_FAILURE_COUNT ) {
		domain.stats.failureCounts[ view->backendFailure[ backend ] ]++;
	}
}

} // namespace

void R_ClassicWorldAmbientDomain_ResetFrame( void ) {
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	domain.viewCount = 0;
	domain.drawCount = 0;
	domain.passCount = 0;
	idStr::Copynz( domain.stats.status, "empty", sizeof( domain.stats.status ) );
}

void R_ClassicWorldAmbientDomain_PrepareFrame(
		const idScenePacketFrame &packetFrame ) {
	R_ClassicWorldAmbientDomain_ResetFrame();
	domain.stats.prepared = true;
	domain.stats.sourceScenes = packetFrame.NumScenes();
	domain.stats.overflow = packetFrame.Stats().overflow;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( !SceneIsWorldAmbientCandidate( packetFrame, scene ) ) {
			continue;
		}
		if ( FindMutableView( scene.viewDef ) != NULL ) {
			continue;
		}
		domain.stats.worldViews++;
		if ( domain.viewCount >= CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_VIEWS ) {
			domain.stats.overflow = true;
			domain.stats.fallbackViews++;
			domain.stats.failureCounts[
				CLASSIC_WORLD_AMBIENT_FAILURE_VIEW_POOL_OVERFLOW ]++;
			continue;
		}
		classicWorldAmbientDomainView_t &view =
			domain.views[ domain.viewCount++ ];
		InitView( view, scene.viewDef, sceneIndex );
		if ( view.sourceSurfaceCount > 0 ) {
			domain.stats.sourceSurfaces += view.sourceSurfaceCount;
		}
		R_ClassicWorldAmbientDomain_PrepareView( packetFrame, scene, view );
	}

	domain.stats.frameValid = !domain.stats.overflow
		&& domain.stats.fallbackViews == 0;
	std::uint64_t frameHash = HASH_OFFSET;
	HashInt( frameHash, domain.stats.sourceScenes );
	HashInt( frameHash, domain.stats.worldViews );
	for ( int i = 0; i < domain.viewCount; ++i ) {
		HashInt( frameHash, domain.views[ i ].scenePacketIndex );
		HashBool( frameHash, domain.views[ i ].ready );
		HashInt( frameHash, domain.views[ i ].failure );
		HashU64( frameHash, domain.views[ i ].hash );
	}
	domain.stats.hash = frameHash;
	if ( domain.stats.worldViews == 0 ) {
		idStr::Copynz( domain.stats.status, "empty",
			sizeof( domain.stats.status ) );
	} else if ( domain.stats.frameValid ) {
		idStr::Copynz( domain.stats.status, "ready",
			sizeof( domain.stats.status ) );
	} else if ( domain.stats.readyViews == 0 ) {
		idStr::Copynz( domain.stats.status, "fallback",
			sizeof( domain.stats.status ) );
	} else {
		idStr::Copynz( domain.stats.status, "mixed-view-fallback",
			sizeof( domain.stats.status ) );
	}
}

const classicWorldAmbientDomainStats_t &R_ClassicWorldAmbientDomain_Stats( void ) {
	return domain.stats;
}

int R_ClassicWorldAmbientDomain_NumViews( void ) {
	return domain.viewCount;
}

const classicWorldAmbientDomainView_t *R_ClassicWorldAmbientDomain_ViewByIndex(
		int index ) {
	return index >= 0 && index < domain.viewCount ? &domain.views[ index ] : NULL;
}

const classicWorldAmbientDomainView_t *R_ClassicWorldAmbientDomain_ViewForScenePacket(
		int scenePacketIndex ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].scenePacketIndex == scenePacketIndex ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

const classicWorldAmbientDomainView_t *R_ClassicWorldAmbientDomain_FindView(
		const viewDef_t *viewDef ) {
	return FindMutableView( viewDef );
}

const classicWorldAmbientDomainDraw_t *R_ClassicWorldAmbientDomain_ViewDraw(
		const classicWorldAmbientDomainView_t &view, int drawIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || drawIndex < 0
			|| drawIndex >= view.drawCount
			|| !RangeFits( view.firstDraw, view.drawCount, domain.drawCount ) ) {
		return NULL;
	}
	return &domain.draws[ view.firstDraw + drawIndex ];
}

const rendererEvaluatedMaterialPass_t *R_ClassicWorldAmbientDomain_DrawPass(
		const classicWorldAmbientDomainDraw_t &draw, int passIndex ) {
	if ( DrawIndex( &draw ) < 0 || passIndex < 0
			|| passIndex >= draw.evaluatedPassCount
			|| !RangeFits( draw.firstEvaluatedPass, draw.evaluatedPassCount,
				domain.passCount ) ) {
		return NULL;
	}
	return &domain.passes[ draw.firstEvaluatedPass + passIndex ];
}

const materialResourceTextureBinding_t *R_ClassicWorldAmbientDomain_DrawPassTexture(
		const classicWorldAmbientDomainDraw_t &draw, int passIndex ) {
	const rendererEvaluatedMaterialPass_t *pass =
		R_ClassicWorldAmbientDomain_DrawPass( draw, passIndex );
	return pass != NULL
		? R_MaterialResourceTable_ResolveTextureResource(
			pass->textureResourceId ) : NULL;
}

bool R_ClassicWorldAmbientDomain_RecordOwned( const viewDef_t *viewDef,
		classicWorldAmbientDomainBackend_t backend, int preFogDrawnPasses,
		int postFogDrawnPasses, int noopPasses ) {
	if ( backend < CLASSIC_WORLD_AMBIENT_BACKEND_GL
			|| backend >= CLASSIC_WORLD_AMBIENT_BACKEND_COUNT ) {
		return false;
	}
	classicWorldAmbientDomainView_t *view = FindMutableView( viewDef );
	if ( view == NULL ) {
		return false;
	}
	classicWorldAmbientDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ]
			!= CLASSIC_WORLD_AMBIENT_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return view->backendOutcome[ backend ]
				== CLASSIC_WORLD_AMBIENT_BACKEND_OWNED
			&& view->backendDrawnPasses[ backend ][
				CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] == preFogDrawnPasses
			&& view->backendDrawnPasses[ backend ][
				CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] == postFogDrawnPasses
			&& view->backendNoopPasses[ backend ] == noopPasses;
	}
	if ( !view->ready ) {
		RecordFallback( view, backend,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY,
			view->failure, preFogDrawnPasses, postFogDrawnPasses,
			noopPasses );
		return false;
	}
	if ( preFogDrawnPasses != view->phaseDrawablePassCount[
			CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ]
			|| postFogDrawnPasses != view->phaseDrawablePassCount[
				CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ]
			|| noopPasses != view->noopPassCount ) {
		RecordFallback( view, backend,
			CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_COVERAGE_MISMATCH,
			preFogDrawnPasses + postFogDrawnPasses,
			preFogDrawnPasses, postFogDrawnPasses, noopPasses );
		return false;
	}
	const int viewIndex = ViewIndex( view );
	view->backendOutcome[ backend ] = CLASSIC_WORLD_AMBIENT_BACKEND_OWNED;
	view->backendDrawnPasses[ backend ][
		CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] = preFogDrawnPasses;
	view->backendDrawnPasses[ backend ][
		CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] = postFogDrawnPasses;
	view->backendNoopPasses[ backend ] = noopPasses;
	coverage.ownedViewMask |= 1ull << viewIndex;
	coverage.ownedViews++;
	coverage.ownedSourceSurfaces += view->sourceSurfaceCount;
	coverage.ownedDrawablePasses += preFogDrawnPasses + postFogDrawnPasses;
	coverage.ownedNoopPasses += noopPasses;
	coverage.ownedPhaseDrawablePasses[
		CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] += preFogDrawnPasses;
	coverage.ownedPhaseDrawablePasses[
		CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] += postFogDrawnPasses;
	return true;
}

void R_ClassicWorldAmbientDomain_RecordBackendFallback(
		const viewDef_t *viewDef, classicWorldAmbientDomainBackend_t backend,
		classicWorldAmbientDomainFailure_t failure, int detail ) {
	if ( backend < CLASSIC_WORLD_AMBIENT_BACKEND_GL
			|| backend >= CLASSIC_WORLD_AMBIENT_BACKEND_COUNT ) {
		return;
	}
	RecordFallback( FindMutableView( viewDef ), backend, failure, detail,
		0, 0, 0 );
}

const classicWorldAmbientDomainBackendCoverage_t &R_ClassicWorldAmbientDomain_BackendCoverage(
		classicWorldAmbientDomainBackend_t backend ) {
	static const classicWorldAmbientDomainBackendCoverage_t empty = {};
	return backend >= CLASSIC_WORLD_AMBIENT_BACKEND_GL
		&& backend < CLASSIC_WORLD_AMBIENT_BACKEND_COUNT
		? domain.stats.backend[ backend ] : empty;
}

const char *ClassicWorldAmbientPhase_Name( classicWorldAmbientPhase_t phase ) {
	switch ( phase ) {
	case CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG: return "preFog";
	case CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG: return "postFog";
	case CLASSIC_WORLD_AMBIENT_PHASE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicWorldAmbientDomainSourceSurface_Name(
		classicWorldAmbientDomainSourceSurface_t sourceSurface ) {
	switch ( sourceSurface ) {
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_DRAWABLE: return "drawable";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NULL_SURFACE: return "noopNullSurface";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_MISSING_MATERIAL: return "noopMissingMaterial";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_NO_AMBIENT: return "noopNoAmbient";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_NOOP_EMPTY_GEOMETRY: return "noopEmptyGeometry";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NOT_DRAWN: return "fallbackNotDrawn";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_PORTAL_SKY: return "fallbackPortalSky";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SUPPRESSED_IN_SUBVIEW: return "fallbackSuppressedInSubview";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_IN_WORLD_GUI: return "fallbackInWorldGui";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SUBVIEW: return "fallbackSubview";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_MISSING_SPACE: return "fallbackMissingSpace";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SYNTHETIC_GUI_SPACE: return "fallbackSyntheticGuiSpace";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SPECIAL_SURFACE: return "fallbackSpecialSurface";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEFORM: return "fallbackDeform";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SKINNING: return "fallbackSkinning";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DEPTH_HACK: return "fallbackDepthHack";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_NEGATIVE_SCALE: return "fallbackNegativeScale";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_DECAL: return "fallbackDecal";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_POST_PROCESS: return "fallbackPostProcess";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_PLAYER_VISIBILITY: return "fallbackPlayerVisibility";
	case CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicWorldAmbientDomainFailure_Name(
		classicWorldAmbientDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_WORLD_AMBIENT_FAILURE_NONE: return "none";
	case CLASSIC_WORLD_AMBIENT_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_WORLD_AMBIENT_FAILURE_SCENE_PACKET_OVERFLOW: return "scenePacketOverflow";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_TABLE_NOT_PREPARED: return "materialTableNotPrepared";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_TABLE_OVERFLOW: return "materialTableOverflow";
	case CLASSIC_WORLD_AMBIENT_FAILURE_VIEW_POOL_OVERFLOW: return "viewPoolOverflow";
	case CLASSIC_WORLD_AMBIENT_FAILURE_DRAW_POOL_OVERFLOW: return "drawPoolOverflow";
	case CLASSIC_WORLD_AMBIENT_FAILURE_EVALUATED_PASS_POOL_OVERFLOW: return "evaluatedPassPoolOverflow";
	case CLASSIC_WORLD_AMBIENT_FAILURE_UNSUPPORTED_VIEW: return "unsupportedView";
	case CLASSIC_WORLD_AMBIENT_FAILURE_UNPACKETIZED_VIEW_EFFECT: return "unpacketizedViewEffect";
	case CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_SCENE_RANGE: return "invalidSceneRange";
	case CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_AMBIENT_PASS: return "invalidAmbientPass";
	case CLASSIC_WORLD_AMBIENT_FAILURE_FORBIDDEN_PASS: return "forbiddenPass";
	case CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DRAW_RANGE: return "invalidDrawRange";
	case CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_SURFACE_FALLBACK: return "sourceSurfaceFallback";
	case CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH: return "sourcePacketMismatch";
	case CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DRAW_PACKET: return "invalidDrawPacket";
	case CLASSIC_WORLD_AMBIENT_FAILURE_DEFORM_CONTRACT: return "deformContract";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_GEOMETRY_RECORD: return "missingGeometryRecord";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_INSTANCE_RECORD: return "missingInstanceRecord";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MISSING_MATERIAL_RECORD: return "missingMaterialRecord";
	case CLASSIC_WORLD_AMBIENT_FAILURE_STALE_MATERIAL_RECORD: return "staleMaterialRecord";
	case CLASSIC_WORLD_AMBIENT_FAILURE_DEPTH_PREREQUISITE_MISSING: return "depthPrerequisiteMissing";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_INELIGIBLE: return "materialPassIneligible";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_COPY_FAILED: return "materialPassCopyFailed";
	case CLASSIC_WORLD_AMBIENT_FAILURE_MATERIAL_PASS_EVALUATION_FAILED: return "materialPassEvaluationFailed";
	case CLASSIC_WORLD_AMBIENT_FAILURE_TEXTURE_RESOURCE_UNAVAILABLE: return "textureResourceUnavailable";
	case CLASSIC_WORLD_AMBIENT_FAILURE_INVALID_DISPOSITION: return "invalidDisposition";
	case CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_NOT_READY: return "backendNotReady";
	case CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_COVERAGE_MISMATCH: return "backendCoverageMismatch";
	case CLASSIC_WORLD_AMBIENT_FAILURE_BACKEND_REJECTED: return "backendRejected";
	case CLASSIC_WORLD_AMBIENT_FAILURE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicWorldAmbientDomainBackend_Name(
		classicWorldAmbientDomainBackend_t backend ) {
	switch ( backend ) {
	case CLASSIC_WORLD_AMBIENT_BACKEND_GL: return "GL";
	case CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN: return "Vulkan";
	case CLASSIC_WORLD_AMBIENT_BACKEND_COUNT:
	default: return "unknown";
	}
}

bool RendererClassicWorldAmbientDomain_RunSelfTest( void ) {
	if ( CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_VIEWS != 64
			|| CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_DRAWS != 4096
			|| CLASSIC_WORLD_AMBIENT_DOMAIN_MAX_EVALUATED_PASSES != 8192
			|| !RangeFits( 8191, 1, 8192 ) || RangeFits( 8192, 1, 8192 )
			|| RangeFits( -1, 1, 8192 )
			|| idStr::Cmp( ClassicWorldAmbientPhase_Name(
				CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ), "preFog" )
			|| idStr::Cmp( ClassicWorldAmbientDomainFailure_Name(
				CLASSIC_WORLD_AMBIENT_FAILURE_DEPTH_PREREQUISITE_MISSING ),
				"depthPrerequisiteMissing" )
			|| idStr::Cmp( ClassicWorldAmbientDomainSourceSurface_Name(
				CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_SKINNING ),
				"fallbackSkinning" )
			|| idStr::Cmp( ClassicWorldAmbientDomainSourceSurface_Name(
				CLASSIC_WORLD_AMBIENT_SOURCE_SURFACE_FALLBACK_PLAYER_VISIBILITY ),
				"fallbackPlayerVisibility" )
			|| idStr::Cmp( ClassicWorldAmbientDomainFailure_Name(
				CLASSIC_WORLD_AMBIENT_FAILURE_UNPACKETIZED_VIEW_EFFECT ),
				"unpacketizedViewEffect" )
			|| idStr::Cmp( ClassicWorldAmbientDomainFailure_Name(
				CLASSIC_WORLD_AMBIENT_FAILURE_DEFORM_CONTRACT ),
				"deformContract" )
			|| ForbiddenPassCategory( RENDER_PASS_DEPTH )
			|| ForbiddenPassCategory( RENDER_PASS_AMBIENT )
			|| !ForbiddenPassCategory( RENDER_PASS_ARB2_INTERACTION )
			|| !ForbiddenPassCategory( static_cast<renderPassCategory_t>(
				0x7fffffff ) ) ) {
		return false;
	}

	rendererEvaluatedMaterialPass_t first;
	std::memset( &first, 0, sizeof( first ) );
	first.sourceStageIndex = 7;
	first.kind = RENDERER_MATERIAL_PASS_SURFACE;
	first.textureResourceId = 0x123400020003ull;
	first.condition = 1.0f;
	first.active = true;
	first.disposition = RENDERER_MATERIAL_PASS_DRAW;
	first.color[ 0 ] = 1.0f;
	first.textureMatrix[ 0 ] = 1.0f;
	first.textureMatrix[ 4 ] = 1.0f;
	first.blend.sourceColor = RENDERER_BLEND_SRC_ALPHA;
	first.blend.destinationColor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA;
	first.depth.testEnabled = true;
	first.depth.compareOperation = RENDERER_COMPARE_EQUAL;
	first.alphaTestCompareOperation = RENDERER_COMPARE_GREATER;
	first.programFamily = RENDERER_PROGRAM_FIXED;
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
	if ( HashPass( same ) != firstHash ) {
		return false;
	}
	classicWorldAmbientDomainDraw_t deformHashDraw;
	InitDraw( deformHashDraw );
	const std::uint64_t undeformedHash = HashDraw( deformHashDraw );
	deformHashDraw.deformRole = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	deformHashDraw.deformOutcome = CLASSIC_DEFORM_OUTCOME_COMPLETED;
	deformHashDraw.deformContractHash = 0x61234ull;
	if ( HashDraw( deformHashDraw ) == undeformedHash ) {
		return false;
	}

	classicWorldAmbientDomainDraw_t dispositionDraw;
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

	const classicWorldAmbientDomainStats_t savedStats = domain.stats;
	classicWorldAmbientDomainView_t rollbackView;
	InitView( rollbackView, NULL, -1 );
	rollbackView.ready = true;
	rollbackView.firstDraw = 17;
	rollbackView.drawCount = 3;
	rollbackView.firstEvaluatedPass = 29;
	rollbackView.evaluatedPassCount = 5;
	rollbackView.drawablePassCount = 2;
	rollbackView.phaseDrawablePassCount[
		CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] = 1;
	rollbackView.phaseDrawablePassCount[
		CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] = 1;
	rollbackView.noopPassCount = 3;
	rollbackView.hash = 1;
	const bool rollbackResult = FailView( rollbackView,
		CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH, 41,
		2, 7, 3 );
	const bool rollbackOk = !rollbackResult && !rollbackView.ready
		&& rollbackView.failure
			== CLASSIC_WORLD_AMBIENT_FAILURE_SOURCE_PACKET_MISMATCH
		&& rollbackView.failureDetail == 41
		&& rollbackView.failureSourceSurfaceIndex == 2
		&& rollbackView.failureDrawPacketIndex == 7
		&& rollbackView.failurePassPacketIndex == 3
		&& rollbackView.firstDraw == -1 && rollbackView.drawCount == 0
		&& rollbackView.firstEvaluatedPass == -1
		&& rollbackView.evaluatedPassCount == 0
		&& rollbackView.drawablePassCount == 0
		&& rollbackView.noopPassCount == 0 && rollbackView.hash == 0;
	domain.stats = savedStats;
	if ( !rollbackOk ) {
		return false;
	}

	const int savedViewCount = domain.viewCount;
	const classicWorldAmbientDomainView_t savedView = domain.views[ 0 ];
	viewDef_t coverageView;
	std::memset( &coverageView, 0, sizeof( coverageView ) );
	InitView( domain.views[ 0 ], &coverageView, 0 );
	domain.viewCount = 1;
	domain.views[ 0 ].ready = true;
	domain.views[ 0 ].sourceSurfaceCount = 4;
	domain.views[ 0 ].drawablePassCount = 3;
	domain.views[ 0 ].phaseDrawablePassCount[
		CLASSIC_WORLD_AMBIENT_PHASE_PRE_FOG ] = 2;
	domain.views[ 0 ].phaseDrawablePassCount[
		CLASSIC_WORLD_AMBIENT_PHASE_POST_FOG ] = 1;
	domain.views[ 0 ].noopPassCount = 3;
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	const bool firstOwned = R_ClassicWorldAmbientDomain_RecordOwned(
		&coverageView, CLASSIC_WORLD_AMBIENT_BACKEND_GL, 2, 1, 3 );
	const bool repeatedOwned = R_ClassicWorldAmbientDomain_RecordOwned(
		&coverageView, CLASSIC_WORLD_AMBIENT_BACKEND_GL, 2, 1, 3 );
	const bool mismatchRejected = !R_ClassicWorldAmbientDomain_RecordOwned(
		&coverageView, CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN, 1, 1, 3 );
	const bool coverageOk = firstOwned && repeatedOwned && mismatchRejected
		&& domain.views[ 0 ].backendOutcome[
			CLASSIC_WORLD_AMBIENT_BACKEND_GL ]
			== CLASSIC_WORLD_AMBIENT_BACKEND_OWNED
		&& domain.views[ 0 ].backendOutcome[
			CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN ]
			== CLASSIC_WORLD_AMBIENT_BACKEND_FALLBACK
		&& domain.stats.backend[
			CLASSIC_WORLD_AMBIENT_BACKEND_GL ].ownedViews == 1
		&& domain.stats.backend[
			CLASSIC_WORLD_AMBIENT_BACKEND_GL ].duplicateReports == 1
		&& domain.stats.backend[
			CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN ].fallbackViews == 1
		&& domain.stats.backend[
			CLASSIC_WORLD_AMBIENT_BACKEND_VULKAN ].coverageMismatches == 1;
	domain.views[ 0 ] = savedView;
	domain.viewCount = savedViewCount;
	domain.stats = savedStats;
	return coverageOk;
}
