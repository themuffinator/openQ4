// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ClassicDeformDomain.h"

#include <cmath>
#include <cstring>

namespace {

const std::uint64_t HASH_OFFSET = 1469598103934665603ull;
const std::uint64_t HASH_PRIME = 1099511628211ull;
const std::uint32_t CONTRACT_HASH_VERSION = 1u;
static classicDeformDomainStats_t packetStatsFrames[2];
static classicDeformDomainStats_t &FramePacketStats() {
	const frameData_t *frame = R_BackEndFrameData();
	return packetStatsFrames[frame != NULL ? frame->slot : 0];
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

static std::uint64_t HashName( const char *name ) {
	std::uint64_t hash = HASH_OFFSET;
	if ( name == NULL ) {
		HashByte( hash, 0u );
		return hash;
	}
	for ( const unsigned char *cursor =
			reinterpret_cast<const unsigned char *>( name ); *cursor != 0;
			++cursor ) {
		HashByte( hash, *cursor );
	}
	return hash;
}

static bool FloatIsFinite( float value ) {
	return std::isfinite( static_cast<double>( value ) );
}

static classicDeformKind_t KindForMaterial( const idMaterial *material ) {
	if ( material == NULL ) {
		return CLASSIC_DEFORM_KIND_NONE;
	}
	switch ( material->Deform() ) {
	case DFRM_NONE: return CLASSIC_DEFORM_KIND_NONE;
	case DFRM_SPRITE: return CLASSIC_DEFORM_KIND_SPRITE;
	case DFRM_RECTSPRITE: return CLASSIC_DEFORM_KIND_RECTSPRITE;
	case DFRM_TUBE: return CLASSIC_DEFORM_KIND_TUBE;
	case DFRM_FLARE: return CLASSIC_DEFORM_KIND_FLARE;
	case DFRM_EXPAND: return CLASSIC_DEFORM_KIND_EXPAND;
	case DFRM_MOVE: return CLASSIC_DEFORM_KIND_MOVE;
	case DFRM_TURB: return CLASSIC_DEFORM_KIND_TURBULENT;
	case DFRM_EYEBALL: return CLASSIC_DEFORM_KIND_EYEBALL;
	case DFRM_PARTICLE: return CLASSIC_DEFORM_KIND_PARTICLE;
	case DFRM_PARTICLE2: return CLASSIC_DEFORM_KIND_PARTICLE2;
	default: return CLASSIC_DEFORM_KIND_UNKNOWN;
	}
}

static bool KindIsSupportedCPUDeform( classicDeformKind_t kind ) {
	return kind >= CLASSIC_DEFORM_KIND_SPRITE
		&& kind <= CLASSIC_DEFORM_KIND_EYEBALL;
}

static bool KindIsUnsupported( classicDeformKind_t kind ) {
	return kind == CLASSIC_DEFORM_KIND_PARTICLE
		|| kind == CLASSIC_DEFORM_KIND_PARTICLE2
		|| kind == CLASSIC_DEFORM_KIND_UNKNOWN;
}

static int ParameterCountForKind( classicDeformKind_t kind ) {
	switch ( kind ) {
	case CLASSIC_DEFORM_KIND_FLARE:
	case CLASSIC_DEFORM_KIND_EXPAND:
	case CLASSIC_DEFORM_KIND_MOVE:
		return 1;
	case CLASSIC_DEFORM_KIND_TURBULENT:
		return 3;
	default:
		return 0;
	}
}

static classicDeformCacheLifetime_t GeometryLifetime(
		const srfTriangles_t *geometry ) {
	if ( geometry == NULL ) {
		return CLASSIC_DEFORM_CACHE_UNKNOWN;
	}
	if ( ( geometry->ambientCache != NULL
				&& geometry->ambientCache->tag == TAG_TEMP )
			|| ( geometry->indexCache != NULL
				&& geometry->indexCache->tag == TAG_TEMP ) ) {
		return CLASSIC_DEFORM_CACHE_FRAME_TEMP;
	}
	// deformedSurface is topology/lifetime ownership for generated models. It
	// is deliberately not used as evidence that a material deform executed.
	if ( geometry->tempAmbientCache || geometry->deformedSurface ) {
		return CLASSIC_DEFORM_CACHE_DYNAMIC_BRIDGE;
	}
	if ( geometry->ambientCache != NULL || geometry->indexCache != NULL ) {
		return CLASSIC_DEFORM_CACHE_STATIC;
	}
	if ( geometry->verts != NULL || geometry->indexes != NULL ) {
		return CLASSIC_DEFORM_CACHE_CLIENT_MEMORY;
	}
	return CLASSIC_DEFORM_CACHE_UNKNOWN;
}

static bool CacheHasBacking( const vertCache_t *cache ) {
	return cache != NULL && ( cache->vbo != 0 || cache->virtMem != NULL );
}

static void CaptureGeometryState( const srfTriangles_t *geometry,
		classicDeformGeometryState_t &state ) {
	std::memset( &state, 0, sizeof( state ) );
	state.geometry = geometry;
	state.ambientOffset = -1;
	state.indexOffset = -1;
	state.ambientTag = -1;
	state.indexTag = -1;
	state.captured = true;
	state.lifetime = GeometryLifetime( geometry );
	if ( geometry == NULL ) {
		state.cacheStateValid = true;
		return;
	}

	state.vertexCount = geometry->numVerts;
	state.indexCount = geometry->numIndexes;
	state.ambientCache = geometry->ambientCache;
	state.indexCache = geometry->indexCache;
	state.hasClientVertexData = geometry->verts != NULL;
	state.hasClientIndexData = geometry->indexes != NULL;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	state.hasPrimBatchMesh = geometry->primBatchMesh != NULL;
#endif
	if ( state.ambientCache != NULL ) {
		state.hasAmbientCache = true;
		state.ambientBuffer = state.ambientCache->vbo;
		state.ambientOffset = state.ambientCache->offset;
		state.ambientBytes = state.ambientCache->size;
		state.ambientTag = state.ambientCache->tag;
		state.ambientCacheHasBacking = CacheHasBacking( state.ambientCache );
	}
	if ( state.indexCache != NULL ) {
		state.hasIndexCache = true;
		state.indexBuffer = state.indexCache->vbo;
		state.indexOffset = state.indexCache->offset;
		state.indexBytes = state.indexCache->size;
		state.indexTag = state.indexCache->tag;
		state.indexCacheHasBacking = CacheHasBacking( state.indexCache );
	}
	state.cacheStateValid = state.vertexCount >= 0 && state.indexCount >= 0
		&& ( !state.hasAmbientCache
			|| ( !state.ambientCache->indexBuffer
				&& state.ambientBytes > 0 && state.ambientOffset >= 0
				&& state.ambientCacheHasBacking ) )
		&& ( !state.hasIndexCache
			|| ( state.indexCache->indexBuffer
				&& state.indexBytes > 0 && state.indexOffset >= 0
				&& state.indexCacheHasBacking ) );
}

static bool GeometryStateMatches( const classicDeformGeometryState_t &a,
		const classicDeformGeometryState_t &b ) {
	return a.geometry == b.geometry
		&& a.ambientCache == b.ambientCache
		&& a.indexCache == b.indexCache
		&& a.vertexCount == b.vertexCount && a.indexCount == b.indexCount
		&& a.ambientBuffer == b.ambientBuffer
		&& a.indexBuffer == b.indexBuffer
		&& a.ambientOffset == b.ambientOffset
		&& a.indexOffset == b.indexOffset
		&& a.ambientBytes == b.ambientBytes
		&& a.indexBytes == b.indexBytes
		&& a.ambientTag == b.ambientTag && a.indexTag == b.indexTag
		&& a.lifetime == b.lifetime && a.captured == b.captured
		&& a.hasAmbientCache == b.hasAmbientCache
		&& a.hasIndexCache == b.hasIndexCache
		&& a.ambientCacheHasBacking == b.ambientCacheHasBacking
		&& a.indexCacheHasBacking == b.indexCacheHasBacking
		&& a.hasClientVertexData == b.hasClientVertexData
		&& a.hasClientIndexData == b.hasClientIndexData
		&& a.hasPrimBatchMesh == b.hasPrimBatchMesh
		&& a.cacheStateValid == b.cacheStateValid;
}

static void HashGeometryState( std::uint64_t &hash,
		const classicDeformGeometryState_t &state ) {
	HashInt( hash, state.vertexCount );
	HashInt( hash, state.indexCount );
	HashInt( hash, state.ambientBytes );
	HashInt( hash, state.indexBytes );
	HashInt( hash, state.ambientTag );
	HashInt( hash, state.indexTag );
	HashInt( hash, state.lifetime );
	HashBool( hash, state.captured );
	HashBool( hash, state.hasAmbientCache );
	HashBool( hash, state.hasIndexCache );
	HashBool( hash, state.ambientCacheHasBacking );
	HashBool( hash, state.indexCacheHasBacking );
	HashBool( hash, state.hasClientVertexData );
	HashBool( hash, state.hasClientIndexData );
	HashBool( hash, state.hasPrimBatchMesh );
	HashBool( hash, state.cacheStateValid );
}

static bool MaterialIdentityMatches( const classicDeformRecord_t &record ) {
	return record.sourceMaterial == record.resultMaterial
		&& record.sourceMaterialIndex == record.resultMaterialIndex
		&& record.sourceMaterialNameHash == record.resultMaterialNameHash;
}

static bool CompletedOutputProvable( const classicDeformRecord_t &record ) {
	const classicDeformGeometryState_t &result = record.resultGeometry;
	const std::uint64_t vertexBytes = result.vertexCount > 0
		? static_cast<std::uint64_t>( result.vertexCount ) * sizeof( idDrawVert ) : 0;
	const std::uint64_t indexBytes = result.indexCount > 0
		? static_cast<std::uint64_t>( result.indexCount ) * sizeof( glIndex_t ) : 0;
	return record.role == CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
		&& record.cpuFinalized && KindIsSupportedCPUDeform( record.kind )
		&& record.parametersValid
		&& record.sourceMaterial != NULL && record.resultMaterial != NULL
		&& MaterialIdentityMatches( record )
		&& record.sourceGeometry.captured && result.captured
		&& record.sourceGeometry.geometry != NULL && result.geometry != NULL
		&& result.geometry != record.sourceGeometry.geometry
		&& result.vertexCount > 0 && result.indexCount > 0
		&& result.cacheStateValid && result.hasAmbientCache
		&& result.ambientCache != NULL && result.ambientCacheHasBacking
		&& result.ambientTag == TAG_TEMP
		&& static_cast<std::uint64_t>( result.ambientBytes ) >= vertexBytes
		&& ( ( result.hasIndexCache && result.indexCache != NULL
				&& result.indexCacheHasBacking && result.indexTag == TAG_TEMP
				&& static_cast<std::uint64_t>( result.indexBytes ) >= indexBytes )
			|| result.hasClientIndexData )
		&& result.lifetime == CLASSIC_DEFORM_CACHE_FRAME_TEMP;
}

static bool EmptyOutputProvable( const classicDeformRecord_t &record ) {
	const classicDeformGeometryState_t &result = record.resultGeometry;
	return record.role == CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
		&& record.cpuFinalized && KindIsSupportedCPUDeform( record.kind )
		&& record.parametersValid
		&& record.sourceMaterial != NULL && record.resultMaterial != NULL
		&& MaterialIdentityMatches( record )
		&& record.sourceGeometry.captured && result.captured
		&& record.sourceGeometry.geometry != NULL && result.geometry != NULL
		&& result.geometry != record.sourceGeometry.geometry
		&& result.vertexCount >= 0 && result.indexCount == 0
		&& result.cacheStateValid
		&& !result.hasAmbientCache && result.ambientCache == NULL
		&& !result.hasIndexCache && result.indexCache == NULL
		&& result.hasClientIndexData
		&& result.lifetime == CLASSIC_DEFORM_CACHE_CLIENT_MEMORY;
}

static std::uint64_t CaptureInputSemanticHash( const drawSurf_t *drawSurf ) {
	std::uint64_t hash = HASH_OFFSET;
	const viewDef_t *view = R_RenderThread_OnRenderThread() ? backEnd.viewDef : tr.viewDef;
	if ( drawSurf != NULL && drawSurf->space != NULL ) {
		for ( int i = 0; i < 16; ++i ) {
			HashFloat( hash, drawSurf->space->modelMatrix[ i ] );
		}
	} else {
		HashByte( hash, 0u );
	}
	if ( view != NULL ) {
		for ( int i = 0; i < 3; ++i ) {
			HashFloat( hash, view->renderView.vieworg[ i ] );
		}
		for ( int axis = 0; axis < 3; ++axis ) {
			for ( int component = 0; component < 3; ++component ) {
				HashFloat( hash,
					view->renderView.viewaxis[ axis ][ component ] );
			}
		}
		HashBool( hash, view->isMirror );
	} else {
		HashByte( hash, 0u );
	}
	return hash;
}

static void CaptureMaterialIdentity( const idMaterial *material, int &index,
		std::uint64_t &nameHash ) {
	index = material != NULL ? material->Index() : -1;
	nameHash = HashName( material != NULL ? material->GetName() : NULL );
}

static void ClearParameters( classicDeformRecord_t &record ) {
	record.parameterCount = 0;
	record.parametersValid = true;
	for ( int i = 0; i < CLASSIC_DEFORM_MAX_PARAMETERS; ++i ) {
		record.parameterRegisters[ i ] = -1;
		record.parameterValues[ i ] = 0.0f;
	}
}

static void CaptureParameters( const drawSurf_t *drawSurf,
		classicDeformRecord_t &record ) {
	ClearParameters( record );
	record.parameterCount = ParameterCountForKind( record.kind );
	if ( record.parameterCount == 0 ) {
		return;
	}
	if ( drawSurf == NULL || drawSurf->material == NULL
			|| drawSurf->shaderRegisters == NULL ) {
		record.parametersValid = false;
		return;
	}
	const int registerCount = drawSurf->material->GetNumRegisters();
	for ( int i = 0; i < record.parameterCount; ++i ) {
		const int registerIndex = drawSurf->material->GetDeformRegister( i );
		record.parameterRegisters[ i ] = registerIndex;
		if ( registerIndex < 0 || registerIndex >= registerCount ) {
			record.parametersValid = false;
			continue;
		}
		record.parameterValues[ i ] = drawSurf->shaderRegisters[ registerIndex ];
		if ( !FloatIsFinite( record.parameterValues[ i ] ) ) {
			record.parametersValid = false;
		}
	}
}

static void InitializeRecordFromDrawSurf( const drawSurf_t *drawSurf,
		classicDeformRole_t role, std::uint64_t frameToken, bool captureInputs,
		classicDeformRecord_t &record ) {
	std::memset( &record, 0, sizeof( record ) );
	record.role = role;
	record.frameToken = frameToken;
	record.sourceMaterialIndex = -1;
	record.resultMaterialIndex = -1;
	record.initialized = true;
	record.cpuFinalized = false;
	record.sourceMaterial = drawSurf != NULL ? drawSurf->material : NULL;
	record.resultMaterial = record.sourceMaterial;
	CaptureMaterialIdentity( record.sourceMaterial, record.sourceMaterialIndex,
		record.sourceMaterialNameHash );
	record.resultMaterialIndex = record.sourceMaterialIndex;
	record.resultMaterialNameHash = record.sourceMaterialNameHash;
	record.kind = KindForMaterial( record.sourceMaterial );
	CaptureGeometryState( drawSurf != NULL ? drawSurf->geo : NULL,
		record.sourceGeometry );
	record.resultGeometry = record.sourceGeometry;
	ClearParameters( record );
	record.flareScale = record.kind == CLASSIC_DEFORM_KIND_FLARE
		? r_flareSize.GetFloat() : 0.0f;
	if ( captureInputs && record.kind != CLASSIC_DEFORM_KIND_NONE ) {
		CaptureParameters( drawSurf, record );
		const idDecl *deformDecl = record.sourceMaterial != NULL
			? record.sourceMaterial->GetDeformDecl() : NULL;
		record.deformDeclNameHash = HashName(
			deformDecl != NULL ? deformDecl->GetName() : NULL );
		record.inputSemanticHash = CaptureInputSemanticHash( drawSurf );
	}

	if ( role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE;
	} else if ( record.kind == CLASSIC_DEFORM_KIND_NONE ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_NONE;
	} else if ( r_skipDeforms.GetBool() ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_SKIPPED;
	} else if ( KindIsUnsupported( record.kind ) ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_UNSUPPORTED;
	} else {
		// A requested deform remains failed until EndDrawSurf proves that the
		// authoritative CPU path published a distinct usable result.
		record.outcome = CLASSIC_DEFORM_OUTCOME_FAILED;
	}
}

static bool ValidateRecordInternal( const classicDeformRecord_t &record,
		bool validateHash ) {
	if ( !record.initialized || record.frameToken == 0
			|| record.role <= CLASSIC_DEFORM_ROLE_UNKNOWN
			|| record.role >= CLASSIC_DEFORM_ROLE_COUNT
			|| record.kind < CLASSIC_DEFORM_KIND_NONE
			|| record.kind >= CLASSIC_DEFORM_KIND_COUNT
			|| record.outcome < CLASSIC_DEFORM_OUTCOME_NONE
			|| record.outcome >= CLASSIC_DEFORM_OUTCOME_COUNT
			|| record.parameterCount < 0
			|| record.parameterCount > CLASSIC_DEFORM_MAX_PARAMETERS
			|| !FloatIsFinite( record.flareScale )
			|| !record.sourceGeometry.captured
			|| !record.resultGeometry.captured
			|| !record.sourceGeometry.cacheStateValid
			|| !record.resultGeometry.cacheStateValid ) {
		return false;
	}
	if ( record.kind != CLASSIC_DEFORM_KIND_FLARE
			&& record.flareScale != 0.0f ) {
		return false;
	}
	if ( validateHash && ( record.semanticHash == 0
			|| record.semanticHash
				!= R_ClassicDeformDomain_ComputeSemanticHash( record ) ) ) {
		return false;
	}
	if ( record.parametersValid ) {
		for ( int i = 0; i < record.parameterCount; ++i ) {
			if ( record.parameterRegisters[ i ] < 0
					|| !FloatIsFinite( record.parameterValues[ i ] ) ) {
				return false;
			}
		}
	}

	if ( record.outcome == CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE ) {
		return record.role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
			&& !record.cpuFinalized
			&& record.sourceMaterial == record.resultMaterial
			&& GeometryStateMatches( record.sourceGeometry,
				record.resultGeometry );
	}
	if ( record.role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW ) {
		return false;
	}

	switch ( record.outcome ) {
	case CLASSIC_DEFORM_OUTCOME_NONE:
		return record.kind == CLASSIC_DEFORM_KIND_NONE
			&& record.sourceMaterial == record.resultMaterial
			&& GeometryStateMatches( record.sourceGeometry,
				record.resultGeometry );
	case CLASSIC_DEFORM_OUTCOME_SKIPPED:
		return record.kind != CLASSIC_DEFORM_KIND_NONE
			&& record.sourceMaterial == record.resultMaterial
			&& GeometryStateMatches( record.sourceGeometry,
				record.resultGeometry );
	case CLASSIC_DEFORM_OUTCOME_COMPLETED:
		return CompletedOutputProvable( record );
	case CLASSIC_DEFORM_OUTCOME_EMPTY:
		return EmptyOutputProvable( record );
	case CLASSIC_DEFORM_OUTCOME_FAILED:
		return KindIsSupportedCPUDeform( record.kind )
			&& !CompletedOutputProvable( record )
			&& !EmptyOutputProvable( record );
	case CLASSIC_DEFORM_OUTCOME_UNSUPPORTED:
		return KindIsUnsupported( record.kind )
			&& record.sourceMaterial == record.resultMaterial
			&& GeometryStateMatches( record.sourceGeometry,
				record.resultGeometry );
	case CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE:
	case CLASSIC_DEFORM_OUTCOME_COUNT:
	default:
		return false;
	}
}

static void SealRecord( classicDeformRecord_t &record ) {
	record.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( record );
	record.valid = ValidateRecordInternal( record, true );
}

} // namespace

std::uint64_t R_ClassicDeformDomain_CurrentFrameToken( void ) {
	// Draw surfaces and their frame-temp caches can span all subviews in one
	// renderer frame. A frame-only token remains stable across those view walks.
	return static_cast<std::uint64_t>(
		static_cast<std::uint32_t>( tr.frameCount ) ) + 1ull;
}

std::uint64_t R_ClassicDeformDomain_BackEndFrameToken( void ) {
	return static_cast<std::uint64_t>(
		static_cast<std::uint32_t>( backEnd.frameCount ) ) + 1ull;
}

void R_ClassicDeformDomain_BeginDrawSurf( drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL ) {
		return;
	}
	// R_AddDrawSurf does not clear the whole structure, so always establish a
	// safe sentinel. Full source capture is opt-in: without shared deformation
	// ownership, scene packets keep their named legacy fallback and do not need
	// model/view hashing on every ordinary finalized draw.
	std::memset( &drawSurf->classicDeform, 0,
		sizeof( drawSurf->classicDeform ) );
	const classicDeformKind_t kind = KindForMaterial( drawSurf->material );
	if ( kind == CLASSIC_DEFORM_KIND_NONE
			|| !r_rendererSharedDeform.GetBool() ) {
		return;
	}
	InitializeRecordFromDrawSurf( drawSurf,
		CLASSIC_DEFORM_ROLE_FINALIZED_DRAW,
		R_ClassicDeformDomain_CurrentFrameToken(),
		true, drawSurf->classicDeform );
	// EndDrawSurf owns the only normal-path seal. This avoids hashing the same
	// record twice and keeps DFRM_NONE's ubiquitous path small.
	drawSurf->classicDeform.valid = false;
}

void R_ClassicDeformDomain_EndDrawSurf( drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL ) {
		return;
	}
	const std::uint64_t frameToken =
		R_ClassicDeformDomain_CurrentFrameToken();
	if ( !drawSurf->classicDeform.initialized
			|| drawSurf->classicDeform.role
				!= CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
			|| drawSurf->classicDeform.frameToken != frameToken ) {
		// Never reconstruct a successful record after the source pointer has
		// already been replaced. Missing/stale Begin state must remain fail-closed.
		return;
	}

	classicDeformRecord_t &record = drawSurf->classicDeform;
	record.cpuFinalized = true;
	record.resultMaterial = drawSurf->material;
	CaptureMaterialIdentity( record.resultMaterial, record.resultMaterialIndex,
		record.resultMaterialNameHash );
	CaptureGeometryState( drawSurf->geo, record.resultGeometry );

	if ( record.kind == CLASSIC_DEFORM_KIND_NONE ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_NONE;
	} else if ( r_skipDeforms.GetBool() ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_SKIPPED;
	} else if ( KindIsUnsupported( record.kind ) ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_UNSUPPORTED;
	} else if ( EmptyOutputProvable( record ) ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_EMPTY;
	} else if ( CompletedOutputProvable( record ) ) {
		record.outcome = CLASSIC_DEFORM_OUTCOME_COMPLETED;
	} else {
		record.outcome = CLASSIC_DEFORM_OUTCOME_FAILED;
	}
	SealRecord( record );
}

void R_ClassicDeformDomain_SnapshotDrawSurf( const drawSurf_t *drawSurf,
		classicDeformRole_t role, std::uint64_t frameToken,
		classicDeformRecord_t &record ) {
	if ( frameToken == 0 ) {
		frameToken = R_ClassicDeformDomain_CurrentFrameToken();
	}
	if ( role <= CLASSIC_DEFORM_ROLE_UNKNOWN
			|| role >= CLASSIC_DEFORM_ROLE_COUNT ) {
		role = CLASSIC_DEFORM_ROLE_OTHER;
	}

	// Interaction/fog receiver and shadow-volume drawSurfs never pass through
	// R_FinalizeDrawSurf. Do not inspect their uninitialized embedded slot or
	// claim that their material deformation ran.
	if ( role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW ) {
		InitializeRecordFromDrawSurf( drawSurf, role, frameToken, false, record );
		SealRecord( record );
		return;
	}

	if ( drawSurf != NULL
			&& drawSurf->classicDeform.initialized
			&& drawSurf->classicDeform.role
				== CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
			&& drawSurf->classicDeform.frameToken == frameToken
			&& R_ClassicDeformDomain_RecordMatchesDrawSurf(
				drawSurf->classicDeform, drawSurf, frameToken ) ) {
		record = drawSurf->classicDeform;
		return;
	}

	const classicDeformKind_t kind = KindForMaterial(
		drawSurf != NULL ? drawSurf->material : NULL );
	InitializeRecordFromDrawSurf( drawSurf, role, frameToken,
		r_rendererSharedDeform.GetBool()
			&& kind != CLASSIC_DEFORM_KIND_NONE, record );
	SealRecord( record );
}

void R_ClassicDeformDomain_SnapshotDrawSurf( const drawSurf_t *drawSurf,
		classicDeformRole_t role, classicDeformRecord_t &record ) {
	R_ClassicDeformDomain_SnapshotDrawSurf( drawSurf, role,
		R_ClassicDeformDomain_CurrentFrameToken(), record );
}

std::uint64_t R_ClassicDeformDomain_ComputeSemanticHash(
		const classicDeformRecord_t &record ) {
	std::uint64_t hash = HASH_OFFSET;
	HashU32( hash, CONTRACT_HASH_VERSION );
	HashInt( hash, record.role );
	HashInt( hash, record.kind );
	HashInt( hash, record.outcome );
	HashInt( hash, record.sourceMaterialIndex );
	HashInt( hash, record.resultMaterialIndex );
	HashU64( hash, record.sourceMaterialNameHash );
	HashU64( hash, record.resultMaterialNameHash );
	HashU64( hash, record.deformDeclNameHash );
	HashU64( hash, record.inputSemanticHash );
	HashGeometryState( hash, record.sourceGeometry );
	HashGeometryState( hash, record.resultGeometry );
	HashInt( hash, record.parameterCount );
	for ( int i = 0; i < CLASSIC_DEFORM_MAX_PARAMETERS; ++i ) {
		HashInt( hash, record.parameterRegisters[ i ] );
		HashFloat( hash, record.parameterValues[ i ] );
	}
	HashFloat( hash, record.flareScale );
	HashBool( hash, record.parametersValid );
	HashBool( hash, record.cpuFinalized );
	return hash;
}

bool R_ClassicDeformDomain_ValidateRecord(
		const classicDeformRecord_t &record ) {
	return record.valid && ValidateRecordInternal( record, true );
}

bool R_ClassicDeformDomain_RecordFreshForFrame(
		const classicDeformRecord_t &record, std::uint64_t frameToken ) {
	return frameToken != 0 && record.initialized
		&& record.frameToken == frameToken;
}

bool R_ClassicDeformDomain_ValidateRecordForFrame(
		const classicDeformRecord_t &record, std::uint64_t frameToken ) {
	return R_ClassicDeformDomain_RecordFreshForFrame( record, frameToken )
		&& R_ClassicDeformDomain_ValidateRecord( record );
}

bool R_ClassicDeformDomain_RecordMatchesDrawSurf(
		const classicDeformRecord_t &record, const drawSurf_t *drawSurf, std::uint64_t frameToken ) {
	if ( drawSurf == NULL
			|| !R_ClassicDeformDomain_ValidateRecordForFrame( record, frameToken )
			|| record.resultMaterial != drawSurf->material ) {
		return false;
	}
	classicDeformGeometryState_t current;
	CaptureGeometryState( drawSurf->geo, current );
	return GeometryStateMatches( record.resultGeometry, current );
}

bool R_ClassicDeformDomain_SameProvenance(
		const classicDeformRecord_t &a, const classicDeformRecord_t &b ) {
	return R_ClassicDeformDomain_ValidateRecord( a )
		&& R_ClassicDeformDomain_ValidateRecord( b )
		&& a.frameToken == b.frameToken && a.semanticHash == b.semanticHash
		&& a.role == b.role && a.kind == b.kind && a.outcome == b.outcome
		&& a.cpuFinalized == b.cpuFinalized
		&& a.sourceMaterial == b.sourceMaterial
		&& a.resultMaterial == b.resultMaterial
		&& GeometryStateMatches( a.sourceGeometry, b.sourceGeometry )
		&& GeometryStateMatches( a.resultGeometry, b.resultGeometry );
}

bool R_ClassicDeformDomain_HasCompletedOutput(
		const classicDeformRecord_t &record ) {
	return record.outcome == CLASSIC_DEFORM_OUTCOME_COMPLETED
		&& R_ClassicDeformDomain_ValidateRecord( record )
		&& CompletedOutputProvable( record );
}

bool R_ClassicDeformDomain_HasEmptyOutput(
		const classicDeformRecord_t &record ) {
	return record.outcome == CLASSIC_DEFORM_OUTCOME_EMPTY
		&& R_ClassicDeformDomain_ValidateRecord( record )
		&& EmptyOutputProvable( record );
}

bool R_ClassicDeformDomain_IsFailClosed(
		const classicDeformRecord_t &record ) {
	if ( !R_ClassicDeformDomain_ValidateRecord( record ) ) {
		return true;
	}
	return record.kind != CLASSIC_DEFORM_KIND_NONE
		&& record.outcome != CLASSIC_DEFORM_OUTCOME_COMPLETED
		&& record.outcome != CLASSIC_DEFORM_OUTCOME_EMPTY
		&& record.outcome != CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE;
}

void R_ClassicDeformDomain_BeginPacketFrame( std::uint64_t frameToken ) {
	std::memset( &FramePacketStats(), 0, sizeof( FramePacketStats() ) );
	FramePacketStats().frameToken = frameToken != 0
		? frameToken : R_ClassicDeformDomain_CurrentFrameToken();
}

void R_ClassicDeformDomain_RecordPacket(
		const classicDeformRecord_t &record ) {
	if ( FramePacketStats().frameToken == 0
			|| FramePacketStats().frameToken != record.frameToken ) {
		R_ClassicDeformDomain_BeginPacketFrame( record.frameToken );
	}
	FramePacketStats().records++;
	if ( FramePacketStats().hash == 0 ) {
		FramePacketStats().hash = HASH_OFFSET;
	}
	const bool valid = R_ClassicDeformDomain_ValidateRecordForFrame(
		record, FramePacketStats().frameToken );
	HashBool( FramePacketStats().hash, valid );
	HashInt( FramePacketStats().hash, record.role );
	HashInt( FramePacketStats().hash, record.outcome );
	HashU64( FramePacketStats().hash, record.semanticHash );
	if ( !valid ) {
		FramePacketStats().invalid++;
		return;
	}
	switch ( record.outcome ) {
	case CLASSIC_DEFORM_OUTCOME_NONE:
		FramePacketStats().none++;
		break;
	case CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE:
		FramePacketStats().notApplicable++;
		break;
	case CLASSIC_DEFORM_OUTCOME_COMPLETED:
		FramePacketStats().completed++;
		break;
	case CLASSIC_DEFORM_OUTCOME_EMPTY:
		FramePacketStats().empty++;
		break;
	case CLASSIC_DEFORM_OUTCOME_SKIPPED:
		FramePacketStats().skipped++;
		break;
	case CLASSIC_DEFORM_OUTCOME_FAILED:
		FramePacketStats().failed++;
		break;
	case CLASSIC_DEFORM_OUTCOME_UNSUPPORTED:
		FramePacketStats().unsupported++;
		break;
	case CLASSIC_DEFORM_OUTCOME_COUNT:
	default:
		FramePacketStats().invalid++;
		break;
	}
}

const classicDeformDomainStats_t &R_ClassicDeformDomain_Stats( void ) {
	return FramePacketStats();
}

const char *ClassicDeformRole_Name( classicDeformRole_t role ) {
	switch ( role ) {
	case CLASSIC_DEFORM_ROLE_UNKNOWN: return "unknown";
	case CLASSIC_DEFORM_ROLE_FINALIZED_DRAW: return "finalizedDraw";
	case CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER: return "interactionReceiver";
	case CLASSIC_DEFORM_ROLE_FOG_RECEIVER: return "fogReceiver";
	case CLASSIC_DEFORM_ROLE_SHADOW_VOLUME: return "shadowVolume";
	case CLASSIC_DEFORM_ROLE_OTHER: return "other";
	case CLASSIC_DEFORM_ROLE_COUNT:
	default: return "invalid";
	}
}

const char *ClassicDeformKind_Name( classicDeformKind_t kind ) {
	switch ( kind ) {
	case CLASSIC_DEFORM_KIND_NONE: return "none";
	case CLASSIC_DEFORM_KIND_SPRITE: return "sprite";
	case CLASSIC_DEFORM_KIND_RECTSPRITE: return "rectsprite";
	case CLASSIC_DEFORM_KIND_TUBE: return "tube";
	case CLASSIC_DEFORM_KIND_FLARE: return "flare";
	case CLASSIC_DEFORM_KIND_EXPAND: return "expand";
	case CLASSIC_DEFORM_KIND_MOVE: return "move";
	case CLASSIC_DEFORM_KIND_TURBULENT: return "turbulent";
	case CLASSIC_DEFORM_KIND_EYEBALL: return "eyeball";
	case CLASSIC_DEFORM_KIND_PARTICLE: return "particle";
	case CLASSIC_DEFORM_KIND_PARTICLE2: return "particle2";
	case CLASSIC_DEFORM_KIND_UNKNOWN: return "unknown";
	case CLASSIC_DEFORM_KIND_COUNT:
	default: return "invalid";
	}
}

const char *ClassicDeformOutcome_Name( classicDeformOutcome_t outcome ) {
	switch ( outcome ) {
	case CLASSIC_DEFORM_OUTCOME_NONE: return "none";
	case CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE: return "notApplicable";
	case CLASSIC_DEFORM_OUTCOME_SKIPPED: return "skipped";
	case CLASSIC_DEFORM_OUTCOME_COMPLETED: return "completed";
	case CLASSIC_DEFORM_OUTCOME_EMPTY: return "empty";
	case CLASSIC_DEFORM_OUTCOME_FAILED: return "failed";
	case CLASSIC_DEFORM_OUTCOME_UNSUPPORTED: return "unsupported";
	case CLASSIC_DEFORM_OUTCOME_COUNT:
	default: return "invalid";
	}
}

const char *ClassicDeformCacheLifetime_Name(
		classicDeformCacheLifetime_t lifetime ) {
	switch ( lifetime ) {
	case CLASSIC_DEFORM_CACHE_UNKNOWN: return "unknown";
	case CLASSIC_DEFORM_CACHE_STATIC: return "static";
	case CLASSIC_DEFORM_CACHE_FRAME_TEMP: return "frameTemp";
	case CLASSIC_DEFORM_CACHE_CLIENT_MEMORY: return "clientMemory";
	case CLASSIC_DEFORM_CACHE_DYNAMIC_BRIDGE: return "dynamicBridge";
	case CLASSIC_DEFORM_CACHE_COUNT:
	default: return "invalid";
	}
}

bool RendererClassicDeformDomain_RunSelfTest( void ) {
	if ( idStr::Cmp( ClassicDeformRole_Name(
			CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER ), "interactionReceiver" )
			|| idStr::Cmp( ClassicDeformKind_Name(
				CLASSIC_DEFORM_KIND_RECTSPRITE ), "rectsprite" )
			|| idStr::Cmp( ClassicDeformOutcome_Name(
				CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE ), "notApplicable" )
			|| idStr::Cmp( ClassicDeformCacheLifetime_Name(
				CLASSIC_DEFORM_CACHE_FRAME_TEMP ), "frameTemp" ) ) {
		return false;
	}

	classicDeformRecord_t completed;
	std::memset( &completed, 0, sizeof( completed ) );
	completed.role = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	completed.kind = CLASSIC_DEFORM_KIND_MOVE;
	completed.outcome = CLASSIC_DEFORM_OUTCOME_COMPLETED;
	completed.sourceMaterial = reinterpret_cast<const idMaterial *>(
		static_cast<std::uintptr_t>( 0x100 ) );
	completed.resultMaterial = completed.sourceMaterial;
	completed.sourceGeometry.geometry =
		reinterpret_cast<const srfTriangles_t *>(
			static_cast<std::uintptr_t>( 0x200 ) );
	completed.resultGeometry.geometry =
		reinterpret_cast<const srfTriangles_t *>(
			static_cast<std::uintptr_t>( 0x300 ) );
	completed.resultGeometry.ambientCache =
		reinterpret_cast<const vertCache_t *>(
			static_cast<std::uintptr_t>( 0x400 ) );
	completed.sourceMaterialIndex = 7;
	completed.resultMaterialIndex = 7;
	completed.frameToken = 1;
	completed.sourceMaterialNameHash = 0x1111ull;
	completed.resultMaterialNameHash = 0x1111ull;
	completed.deformDeclNameHash = 0x2222ull;
	completed.inputSemanticHash = 0x3333ull;
	completed.parameterCount = 1;
	for ( int i = 0; i < CLASSIC_DEFORM_MAX_PARAMETERS; ++i ) {
		completed.parameterRegisters[ i ] = -1;
	}
	completed.parameterRegisters[ 0 ] = 3;
	completed.parameterValues[ 0 ] = 2.0f;
	completed.parametersValid = true;
	completed.cpuFinalized = true;
	completed.initialized = true;
	completed.sourceGeometry.vertexCount = 4;
	completed.sourceGeometry.indexCount = 6;
	completed.sourceGeometry.captured = true;
	completed.sourceGeometry.hasClientVertexData = true;
	completed.sourceGeometry.hasClientIndexData = true;
	completed.sourceGeometry.cacheStateValid = true;
	completed.sourceGeometry.lifetime = CLASSIC_DEFORM_CACHE_CLIENT_MEMORY;
	completed.resultGeometry.vertexCount = 4;
	completed.resultGeometry.indexCount = 6;
	completed.resultGeometry.captured = true;
	completed.resultGeometry.hasAmbientCache = true;
	completed.resultGeometry.ambientCacheHasBacking = true;
	completed.resultGeometry.ambientBuffer = 1;
	completed.resultGeometry.ambientBytes = 4 * sizeof( idDrawVert );
	completed.resultGeometry.ambientOffset = 0;
	completed.resultGeometry.ambientTag = TAG_TEMP;
	completed.resultGeometry.hasClientIndexData = true;
	completed.resultGeometry.lifetime = CLASSIC_DEFORM_CACHE_FRAME_TEMP;
	completed.resultGeometry.cacheStateValid = true;
	completed.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( completed );
	completed.valid = true;
	if ( !R_ClassicDeformDomain_ValidateRecord( completed )
			|| !R_ClassicDeformDomain_ValidateRecordForFrame( completed, 1 )
			|| !R_ClassicDeformDomain_HasCompletedOutput( completed )
			|| R_ClassicDeformDomain_IsFailClosed( completed ) ) {
		return false;
	}
	classicDeformRecord_t driftedMaterial = completed;
	driftedMaterial.resultMaterial = reinterpret_cast<const idMaterial *>(
		static_cast<std::uintptr_t>( 0x101 ) );
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasCompletedOutput( driftedMaterial ) ) {
		return false;
	}
	driftedMaterial = completed;
	driftedMaterial.resultMaterialIndex++;
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasCompletedOutput( driftedMaterial ) ) {
		return false;
	}
	driftedMaterial = completed;
	driftedMaterial.resultMaterialNameHash++;
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasCompletedOutput( driftedMaterial ) ) {
		return false;
	}

	classicDeformRecord_t same = completed;
	same.frameToken = 2;
	same.resultGeometry.ambientBuffer = 99;
	if ( R_ClassicDeformDomain_ComputeSemanticHash( same )
			!= completed.semanticHash
			|| R_ClassicDeformDomain_SameProvenance( completed, same ) ) {
		return false;
	}
	same = completed;
	same.parameterValues[ 0 ] = 3.0f;
	same.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( same );
	same.valid = true;
	if ( same.semanticHash == completed.semanticHash
			|| R_ClassicDeformDomain_SameProvenance( completed, same ) ) {
		return false;
	}

	classicDeformRecord_t empty = completed;
	empty.kind = CLASSIC_DEFORM_KIND_FLARE;
	empty.outcome = CLASSIC_DEFORM_OUTCOME_EMPTY;
	empty.flareScale = 1.0f;
	empty.resultGeometry.geometry =
		reinterpret_cast<const srfTriangles_t *>(
			static_cast<std::uintptr_t>( 0x500 ) );
	empty.resultGeometry.ambientCache = NULL;
	empty.resultGeometry.indexCache = NULL;
	empty.resultGeometry.vertexCount = 16;
	empty.resultGeometry.indexCount = 0;
	empty.resultGeometry.hasAmbientCache = false;
	empty.resultGeometry.hasIndexCache = false;
	empty.resultGeometry.ambientCacheHasBacking = false;
	empty.resultGeometry.indexCacheHasBacking = false;
	empty.resultGeometry.ambientBuffer = 0;
	empty.resultGeometry.ambientBytes = 0;
	empty.resultGeometry.ambientOffset = -1;
	empty.resultGeometry.ambientTag = -1;
	empty.resultGeometry.indexOffset = -1;
	empty.resultGeometry.indexTag = -1;
	empty.resultGeometry.lifetime = CLASSIC_DEFORM_CACHE_CLIENT_MEMORY;
	empty.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( empty );
	empty.valid = true;
	if ( !R_ClassicDeformDomain_ValidateRecord( empty )
			|| !R_ClassicDeformDomain_HasEmptyOutput( empty )
			|| R_ClassicDeformDomain_IsFailClosed( empty ) ) {
		return false;
	}
	driftedMaterial = empty;
	driftedMaterial.resultMaterial = reinterpret_cast<const idMaterial *>(
		static_cast<std::uintptr_t>( 0x101 ) );
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasEmptyOutput( driftedMaterial ) ) {
		return false;
	}
	driftedMaterial = empty;
	driftedMaterial.resultMaterialIndex++;
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasEmptyOutput( driftedMaterial ) ) {
		return false;
	}
	driftedMaterial = empty;
	driftedMaterial.resultMaterialNameHash++;
	driftedMaterial.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( driftedMaterial );
	driftedMaterial.valid = true;
	if ( R_ClassicDeformDomain_ValidateRecord( driftedMaterial )
			|| R_ClassicDeformDomain_HasEmptyOutput( driftedMaterial ) ) {
		return false;
	}

	classicDeformRecord_t notApplicable = completed;
	notApplicable.role = CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER;
	notApplicable.outcome = CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE;
	notApplicable.cpuFinalized = false;
	notApplicable.resultGeometry = notApplicable.sourceGeometry;
	notApplicable.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( notApplicable );
	notApplicable.valid = true;
	if ( !R_ClassicDeformDomain_ValidateRecord( notApplicable )
			|| R_ClassicDeformDomain_IsFailClosed( notApplicable ) ) {
		return false;
	}

	classicDeformRecord_t skipped = completed;
	skipped.outcome = CLASSIC_DEFORM_OUTCOME_SKIPPED;
	skipped.resultGeometry = skipped.sourceGeometry;
	skipped.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( skipped );
	skipped.valid = true;
	if ( !R_ClassicDeformDomain_ValidateRecord( skipped )
			|| !R_ClassicDeformDomain_IsFailClosed( skipped ) ) {
		return false;
	}

	classicDeformRecord_t failed = completed;
	failed.outcome = CLASSIC_DEFORM_OUTCOME_FAILED;
	failed.resultGeometry = failed.sourceGeometry;
	failed.semanticHash = R_ClassicDeformDomain_ComputeSemanticHash( failed );
	failed.valid = true;
	if ( !R_ClassicDeformDomain_ValidateRecord( failed )
			|| !R_ClassicDeformDomain_IsFailClosed( failed ) ) {
		return false;
	}

	classicDeformRecord_t unsupported = failed;
	unsupported.kind = CLASSIC_DEFORM_KIND_PARTICLE;
	unsupported.outcome = CLASSIC_DEFORM_OUTCOME_UNSUPPORTED;
	unsupported.parameterCount = 0;
	unsupported.parameterRegisters[ 0 ] = -1;
	unsupported.parameterValues[ 0 ] = 0.0f;
	unsupported.semanticHash =
		R_ClassicDeformDomain_ComputeSemanticHash( unsupported );
	unsupported.valid = true;
	return R_ClassicDeformDomain_ValidateRecord( unsupported )
		&& R_ClassicDeformDomain_IsFailClosed( unsupported );
}

void R_ClassicDeformDomain_RebaseFrameCopy( classicDeformRecord_t &record,
		const srfTriangles_t *source, const srfTriangles_t *result ) {
	if ( !record.initialized || !record.valid ) return;
	CaptureGeometryState( source, record.sourceGeometry );
	CaptureGeometryState( result, record.resultGeometry );
	SealRecord( record );
}
