// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "CelShading.h"
#include "ClassicInteractionDomain.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

const std::uint64_t HASH_OFFSET = 1469598103934665603ull;
const std::uint64_t HASH_PRIME = 1099511628211ull;

typedef struct classicInteractionDomainState_s {
	classicInteractionDomainView_t views[ CLASSIC_INTERACTION_DOMAIN_MAX_VIEWS ];
	classicInteractionDomainLight_t lights[ CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS ];
	classicInteractionDomainSurface_t surfaces[ CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES ];
	classicInteractionDomainPrimitive_t primitives[ CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES ];
	classicInteractionDomainShadowCaster_t shadowCasters[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ];
	classicInteractionDomainShadowAlphaStage_t shadowAlphaStages[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES ];
	classicInteractionDomainShadowMapPass_t shadowMapPasses[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES ];
	classicInteractionDomainTexture_t textures[ CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES ];
	classicInteractionDomainStats_t stats;
	std::uint32_t generation;
	int viewCount;
	int lightCount;
	int surfaceCount;
	int primitiveCount;
	int shadowCasterCount;
	int shadowAlphaStageCount;
	int shadowMapPassCount;
	int textureCount;
} classicInteractionDomainState_t;

typedef struct classicInteractionCheckpoint_s {
	int lightCount;
	int surfaceCount;
	int primitiveCount;
	int shadowCasterCount;
	int shadowAlphaStageCount;
	int shadowMapPassCount;
	int textureCount;
} classicInteractionCheckpoint_t;

typedef struct classicInteractionBuildError_s {
	classicInteractionDomainFailure_t failure;
	int detail;
	int passPacketIndex;
	int drawPacketIndex;
	int lightOrdinal;
	int receiverOrdinal;
	int stageIndex;
} classicInteractionBuildError_t;

typedef struct classicInteractionWork_s {
	int bumpStageIndex;
	int diffuseStageIndex;
	int specularStageIndex;
	const idImage *bumpImage;
	const idImage *diffuseImage;
	const idImage *specularImage;
	std::uint64_t bumpImageResourceId;
	std::uint64_t diffuseImageResourceId;
	std::uint64_t specularImageResourceId;
	float diffuseColor[ 4 ];
	float specularColor[ 4 ];
	float bumpMatrix[ 2 ][ 4 ];
	float diffuseMatrix[ 2 ][ 4 ];
	float specularMatrix[ 2 ][ 4 ];
	rendererVertexColorMode_t vertexColor;
} classicInteractionWork_t;

classicInteractionDomainState_t domain;

// Per-frame memo of AddTexture linear-scan results.  Entries are only ever
// copies of authoritative pool-scan results; any pool shrink (frame reset or
// whole-view rollback in FailView) empties the memo by bumping the stamp, so
// a memo hit can never observe rolled-back or stale state.
const int TEXTURE_LOOKUP_SIZE = CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES * 2;
static_assert( ( TEXTURE_LOOKUP_SIZE & ( TEXTURE_LOOKUP_SIZE - 1 ) ) == 0,
	"texture lookup size must be a power of two" );

typedef struct classicInteractionTextureLookupSlot_s {
	const idImage *image;
	std::uint64_t storageGeneration;
	int textureIndex;
	std::uint32_t stamp;
} classicInteractionTextureLookupSlot_t;

classicInteractionTextureLookupSlot_t textureLookup[ TEXTURE_LOOKUP_SIZE ];
std::uint32_t textureLookupStamp;

static void InvalidateTextureLookup( void ) {
	textureLookupStamp++;
	if ( textureLookupStamp == 0 ) {
		std::memset( textureLookup, 0, sizeof( textureLookup ) );
		textureLookupStamp = 1;
	}
}

static int TextureLookupSlot( const idImage *image,
		std::uint64_t storageGeneration ) {
	std::uint64_t key = static_cast<std::uint64_t>(
		reinterpret_cast<std::uintptr_t>( image ) );
	key ^= storageGeneration * HASH_PRIME;
	key *= HASH_PRIME;
	int slot = static_cast<int>( key & ( TEXTURE_LOOKUP_SIZE - 1 ) );
	for ( int probe = 0; probe < TEXTURE_LOOKUP_SIZE; ++probe ) {
		const classicInteractionTextureLookupSlot_t &entry =
			textureLookup[ slot ];
		if ( entry.stamp != textureLookupStamp
				|| ( entry.image == image
					&& entry.storageGeneration == storageGeneration ) ) {
			return slot;
		}
		slot = ( slot + 1 ) & ( TEXTURE_LOOKUP_SIZE - 1 );
	}
	return -1;
}

static bool RangeFits( int first, int count, int capacity ) {
	return first >= 0 && count >= 0 && first <= capacity
		&& count <= capacity - first;
}

static bool FloatIsFinite( float value ) {
	return std::isfinite( value ) != 0;
}

static bool FloatsAreFinite( const float *values, int count ) {
	if ( values == NULL || count < 0 ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		if ( !FloatIsFinite( values[ i ] ) ) {
			return false;
		}
	}
	return true;
}

static void HashByte( std::uint64_t &hash, unsigned int value ) {
	hash ^= value & 0xffu;
	hash *= HASH_PRIME;
}

static void HashU32( std::uint64_t &hash, std::uint32_t value ) {
	hash ^= value;
	hash *= HASH_PRIME;
}

static void HashU64( std::uint64_t &hash, std::uint64_t value ) {
	hash ^= value;
	hash *= HASH_PRIME;
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

static std::uint64_t HashString( const char *text ) {
	std::uint64_t hash = HASH_OFFSET;
	if ( text != NULL ) {
		for ( const unsigned char *cursor =
				reinterpret_cast<const unsigned char *>( text );
				*cursor != 0; ++cursor ) {
			HashByte( hash, *cursor );
		}
	}
	return hash;
}

static std::uint64_t MakeTextureResourceId( int textureIndex ) {
	if ( domain.generation == 0 || textureIndex < 0
			|| textureIndex >= CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES ) {
		return 0;
	}
	return ( static_cast<std::uint64_t>( domain.generation ) << 32 )
		| static_cast<std::uint64_t>( textureIndex + 1 );
}

static int TextureIndexFromResourceId( std::uint64_t resourceId ) {
	if ( resourceId == 0
			|| static_cast<std::uint32_t>( resourceId >> 32 )
				!= domain.generation ) {
		return -1;
	}
	const std::uint32_t encoded = static_cast<std::uint32_t>( resourceId );
	if ( encoded == 0 || encoded > CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES ) {
		return -1;
	}
	return static_cast<int>( encoded - 1 );
}

static void SetError( classicInteractionBuildError_t &error,
		classicInteractionDomainFailure_t failure, int detail = 0,
		int passPacketIndex = -1, int drawPacketIndex = -1,
		int lightOrdinal = -1, int receiverOrdinal = -1,
		int stageIndex = -1 ) {
	error.failure = failure;
	error.detail = detail;
	error.passPacketIndex = passPacketIndex;
	error.drawPacketIndex = drawPacketIndex;
	error.lightOrdinal = lightOrdinal;
	error.receiverOrdinal = receiverOrdinal;
	error.stageIndex = stageIndex;
}

static void InitError( classicInteractionBuildError_t &error ) {
	SetError( error, CLASSIC_INTERACTION_FAILURE_NONE );
}

static void InitTextureMatrix( float matrix[ 2 ][ 4 ] ) {
	std::memset( matrix, 0, sizeof( float ) * 8 );
	matrix[ 0 ][ 0 ] = 1.0f;
	matrix[ 1 ][ 1 ] = 1.0f;
}

static void InitWork( classicInteractionWork_t &work ) {
	std::memset( &work, 0, sizeof( work ) );
	work.bumpStageIndex = -1;
	work.diffuseStageIndex = -1;
	work.specularStageIndex = -1;
	work.vertexColor = RENDERER_VERTEX_COLOR_IGNORE;
	InitTextureMatrix( work.bumpMatrix );
	InitTextureMatrix( work.diffuseMatrix );
	InitTextureMatrix( work.specularMatrix );
}

static void InitPrimitive( classicInteractionDomainPrimitive_t &primitive ) {
	std::memset( &primitive, 0, sizeof( primitive ) );
	primitive.lightIndex = -1;
	primitive.surfaceIndex = -1;
	primitive.lightStageIndex = -1;
	primitive.bumpStageIndex = -1;
	primitive.diffuseStageIndex = -1;
	primitive.specularStageIndex = -1;
	primitive.receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	primitive.disposition = CLASSIC_INTERACTION_PRIMITIVE_NOOP_MISSING_BUMP;
	primitive.depth = CLASSIC_INTERACTION_DEPTH_EQUAL;
	primitive.cull = RENDERER_CULL_FRONT;
	primitive.vertexColor = RENDERER_VERTEX_COLOR_IGNORE;
	primitive.blend.enabled = true;
	primitive.blend.sourceColor = RENDERER_BLEND_ONE;
	primitive.blend.destinationColor = RENDERER_BLEND_ONE;
	primitive.blend.colorOperation = RENDERER_BLEND_OP_ADD;
	primitive.blend.sourceAlpha = RENDERER_BLEND_ONE;
	primitive.blend.destinationAlpha = RENDERER_BLEND_ONE;
	primitive.blend.alphaOperation = RENDERER_BLEND_OP_ADD;
	InitTextureMatrix( primitive.bumpMatrix );
	InitTextureMatrix( primitive.diffuseMatrix );
	InitTextureMatrix( primitive.specularMatrix );
}

static void InitSurface( classicInteractionDomainSurface_t &surface ) {
	std::memset( &surface, 0, sizeof( surface ) );
	surface.drawPacketIndex = -1;
	surface.lightIndex = -1;
	surface.sourceOrdinal = -1;
	surface.receiverOrdinal = -1;
	surface.receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	surface.materialTableRecordIndex = -1;
	surface.materialId = -1;
	surface.firstPrimitive = -1;
}

static void InitShadowAlphaStage(
		classicInteractionDomainShadowAlphaStage_t &stage ) {
	std::memset( &stage, 0, sizeof( stage ) );
	stage.casterIndex = -1;
	stage.stageIndex = -1;
	InitTextureMatrix( stage.textureMatrix );
}

static void InitShadowCaster(
		classicInteractionDomainShadowCaster_t &caster ) {
	std::memset( &caster, 0, sizeof( caster ) );
	caster.drawPacketIndex = -1;
	caster.lightIndex = -1;
	caster.sourceOrdinal = -1;
	caster.chainOrdinal = -1;
	caster.geometryRecordIndex = -1;
	caster.instanceRecordIndex = -1;
	caster.materialTableRecordIndex = -1;
	caster.materialId = -1;
	caster.chain = CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL;
	caster.disposition = CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY;
	caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_FULL;
	caster.cull = RENDERER_CULL_FRONT;
	caster.firstAlphaStage = -1;
	caster.perfectHull = false;
}

static void InitShadowMapPass(
		classicInteractionDomainShadowMapPass_t &pass ) {
	std::memset( &pass, 0, sizeof( pass ) );
	pass.lightIndex = -1;
	pass.receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	pass.resourceOwner = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	pass.disposition = CLASSIC_INTERACTION_SHADOW_MAP_PASS_UNUSED;
	pass.mode = CLASSIC_INTERACTION_SHADOW_NONE;
	pass.lightClass = SHADOWMAP_LIGHT_PROJECTED;
	R_ShadowMapResetProjectedLightState( pass.projected.state );
	pass.point.faceCount = 6;
}

static void InitLight( classicInteractionDomainLight_t &light ) {
	std::memset( &light, 0, sizeof( light ) );
	light.sourceOrdinal = -1;
	light.firstSurface = -1;
	light.firstPrimitive = -1;
	for ( int chain = 0; chain < CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT;
			++chain ) {
		light.firstShadowCaster[ chain ] = -1;
	}
	light.shadowMapPassIndex[ CLASSIC_INTERACTION_RECEIVER_LOCAL ] = -1;
	light.shadowMapPassIndex[ CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = -1;
}

static void InitView( classicInteractionDomainView_t &view,
		const viewDef_t *viewDef, int scenePacketIndex ) {
	std::memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.scenePacketIndex = scenePacketIndex;
	view.interactionPassPacketIndex = -1;
	view.stencilShadowPassPacketIndex = -1;
	view.shadowMapPassPacketIndex = -1;
	view.firstLight = -1;
	view.firstSurface = -1;
	view.firstPrimitive = -1;
	view.firstShadowCaster = -1;
	view.firstShadowMapPass = -1;
	view.stencilReference = 128;
	view.shadowMode = CLASSIC_INTERACTION_SHADOW_NONE;
	view.failurePassPacketIndex = -1;
	view.failureDrawPacketIndex = -1;
	view.failureLightOrdinal = -1;
	view.failureReceiverOrdinal = -1;
	view.failureStageIndex = -1;
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
	}
}

static bool FailureIsOverflow( classicInteractionDomainFailure_t failure ) {
	return failure == CLASSIC_INTERACTION_FAILURE_SCENE_PACKET_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_VIEW_POOL_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_LIGHT_POOL_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_SURFACE_POOL_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_PRIMITIVE_POOL_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW
		|| failure
			== CLASSIC_INTERACTION_FAILURE_SHADOW_ALPHA_STAGE_POOL_OVERFLOW
		|| failure == CLASSIC_INTERACTION_FAILURE_TEXTURE_POOL_OVERFLOW;
}

static bool FailView( classicInteractionDomainView_t &view,
		const classicInteractionCheckpoint_t &checkpoint,
		const classicInteractionBuildError_t &error ) {
	if ( checkpoint.textureCount < domain.textureCount ) {
		InvalidateTextureLookup();
	}
	domain.lightCount = checkpoint.lightCount;
	domain.surfaceCount = checkpoint.surfaceCount;
	domain.primitiveCount = checkpoint.primitiveCount;
	domain.shadowCasterCount = checkpoint.shadowCasterCount;
	domain.shadowAlphaStageCount = checkpoint.shadowAlphaStageCount;
	domain.shadowMapPassCount = checkpoint.shadowMapPassCount;
	domain.textureCount = checkpoint.textureCount;
	view.ready = false;
	view.failure = error.failure == CLASSIC_INTERACTION_FAILURE_NONE
		? CLASSIC_INTERACTION_FAILURE_UNAVAILABLE : error.failure;
	view.failureDetail = error.detail;
	view.failurePassPacketIndex = error.passPacketIndex;
	view.failureDrawPacketIndex = error.drawPacketIndex;
	view.failureLightOrdinal = error.lightOrdinal;
	view.failureReceiverOrdinal = error.receiverOrdinal;
	view.failureStageIndex = error.stageIndex;
	view.firstLight = -1;
	view.lightCount = 0;
	view.firstSurface = -1;
	view.surfaceCount = 0;
	view.firstPrimitive = -1;
	view.primitiveCount = 0;
	view.drawablePrimitiveCount = 0;
	view.noopPrimitiveCount = 0;
	view.firstShadowCaster = -1;
	view.shadowCasterCount = 0;
	view.firstShadowMapPass = -1;
	view.shadowMapPassCount = 0;
	view.drawableShadowCasterCount = 0;
	view.noopShadowCasterCount = 0;
	view.logicalVolumeDrawCount = 0;
	view.preloadVolumeDrawCount = 0;
	view.shadowLightCount = 0;
	view.hybridShadowPassCount = 0;
	view.projectedShadowMapPassCount = 0;
	view.csmShadowMapPassCount = 0;
	view.pointShadowMapPassCount = 0;
	view.projectedShadowLightCount = 0;
	view.pointShadowLightCount = 0;
	view.shadowPacketDrawCount = 0;
	view.shadowMode = CLASSIC_INTERACTION_SHADOW_NONE;
	view.activeLightStageCount = 0;
	view.inactiveLightStageCount = 0;
	view.activeSurfaceStageCount = 0;
	view.inactiveSurfaceStageCount = 0;
	view.materialDeformReceiverCount = 0;
	view.completedDeformCasterCount = 0;
	view.emptyDeformCasterCount = 0;
	std::memset( view.receiverSurfaceCount, 0,
		sizeof( view.receiverSurfaceCount ) );
	std::memset( view.receiverPrimitiveCount, 0,
		sizeof( view.receiverPrimitiveCount ) );
	view.hash = 0;
	domain.stats.fallbackViews++;
	if ( view.failure >= CLASSIC_INTERACTION_FAILURE_NONE
			&& view.failure < CLASSIC_INTERACTION_FAILURE_COUNT ) {
		domain.stats.failureCounts[ view.failure ]++;
	}
	if ( FailureIsOverflow( view.failure ) ) {
		domain.stats.overflow = true;
	}
	return false;
}

static bool MaterialRequestsClassicDeform( const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && drawSurf->material != NULL
		&& drawSurf->material->Deform() != DFRM_NONE;
}

static int DeformContractFailureDetail( const drawSurf_t *drawSurf,
		const drawPacket_t *packet = NULL ) {
	const classicDeformRecord_t *record = packet != NULL
		&& packet->hasClassicDeformRecord
		&& packet->classicDeformRecord != NULL ? packet->classicDeformRecord
		: ( drawSurf != NULL ? &drawSurf->classicDeform : NULL );
	if ( record == NULL ) {
		return -1;
	}
	return static_cast<int>( record->role )
		* CLASSIC_DEFORM_OUTCOME_COUNT
		+ static_cast<int>( record->outcome );
}

static bool ValidateDeformContract( const drawSurf_t *drawSurf,
		const drawPacket_t *packet, classicDeformRole_t expectedRole,
		classicDeformRole_t &role, classicDeformOutcome_t &outcome,
		std::uint64_t &semanticHash ) {
	role = CLASSIC_DEFORM_ROLE_UNKNOWN;
	outcome = CLASSIC_DEFORM_OUTCOME_NONE;
	semanticHash = 0;
	if ( !MaterialRequestsClassicDeform( drawSurf ) ) {
		return true;
	}
	if ( !r_rendererSharedDeform.GetBool() || r_skipDeforms.GetBool() ) {
		return false;
	}
	const std::uint64_t frameToken =
		R_ClassicDeformDomain_BackEndFrameToken();
	const classicDeformRecord_t *packetRecord = packet != NULL
		? packet->classicDeformRecord : NULL;
	const classicDeformRecord_t &record =
		expectedRole == CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
		? drawSurf->classicDeform
		: ( packetRecord != NULL ? *packetRecord : drawSurf->classicDeform );
	if ( record.role != expectedRole
			|| !R_ClassicDeformDomain_ValidateRecordForFrame( record,
				frameToken )
			|| !R_ClassicDeformDomain_RecordMatchesDrawSurf( record, drawSurf, frameToken ) ) {
		return false;
	}
	if ( expectedRole == CLASSIC_DEFORM_ROLE_FINALIZED_DRAW ) {
		if ( !record.cpuFinalized
				|| ( !R_ClassicDeformDomain_HasCompletedOutput( record )
					&& !R_ClassicDeformDomain_HasEmptyOutput( record ) ) ) {
			return false;
		}
	} else if ( record.cpuFinalized
			|| record.outcome != CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE ) {
		return false;
	}
	if ( packet != NULL ) {
		if ( !packet->hasClassicDeformRecord
				|| packetRecord == NULL
				|| packet->geometryRecord == NULL
				|| packetRecord != &packet->geometryRecord->classicDeform
				|| !packet->geometryRecord->hasClassicDeformRecord
				|| !R_ClassicDeformDomain_ValidateRecordForFrame(
					*packetRecord, frameToken )
				|| !R_ClassicDeformDomain_SameProvenance( record,
					*packetRecord ) ) {
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

static bool ReadRegister( const float *registers, int registerCount,
		int registerIndex, float &value,
		classicInteractionBuildError_t &error, int stageIndex ) {
	if ( registers == NULL || registerCount <= 0 ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
			registerCount, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( registerIndex < 0 || registerIndex >= registerCount ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_REGISTER_OUT_OF_RANGE,
			registerIndex, -1, -1, -1, -1, stageIndex );
		return false;
	}
	value = registers[ registerIndex ];
	if ( !FloatIsFinite( value ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			registerIndex, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static bool EvaluateTextureMatrix( const textureStage_t &texture,
		const float *registers, int registerCount, float matrix[ 2 ][ 4 ],
		classicInteractionBuildError_t &error, int stageIndex ) {
	InitTextureMatrix( matrix );
	if ( !texture.hasMatrix ) {
		return true;
	}
	float values[ 2 ][ 3 ];
	for ( int row = 0; row < 2; ++row ) {
		for ( int column = 0; column < 3; ++column ) {
			if ( !ReadRegister( registers, registerCount,
					texture.matrix[ row ][ column ], values[ row ][ column ],
					error, stageIndex ) ) {
				return false;
			}
		}
	}
	for ( int row = 0; row < 2; ++row ) {
		float translation = values[ row ][ 2 ];
		if ( translation < -40.0f || translation > 40.0f ) {
			if ( static_cast<double>( translation ) < -2147483648.0
					|| static_cast<double>( translation ) > 2147483647.0 ) {
				SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
					texture.matrix[ row ][ 2 ], -1, -1, -1, -1, stageIndex );
				return false;
			}
			translation -= static_cast<int>( translation );
		}
		matrix[ row ][ 0 ] = values[ row ][ 0 ];
		matrix[ row ][ 1 ] = values[ row ][ 1 ];
		matrix[ row ][ 2 ] = 0.0f;
		matrix[ row ][ 3 ] = translation;
	}
	return true;
}

static bool EvaluateStageColor( const shaderStage_t &stage,
		const float *registers, int registerCount, float color[ 4 ],
		bool clampColor, classicInteractionBuildError_t &error,
		int stageIndex ) {
	for ( int component = 0; component < 4; ++component ) {
		if ( !ReadRegister( registers, registerCount,
				stage.color.registers[ component ], color[ component ], error,
				stageIndex ) ) {
			return false;
		}
		if ( clampColor ) {
			color[ component ] = idMath::ClampFloat( 0.0f, 1.0f,
				color[ component ] );
		}
	}
	return true;
}

static bool EvaluateCondition( const shaderStage_t &stage,
		const float *registers, int registerCount, bool &active,
		classicInteractionBuildError_t &error, int stageIndex ) {
	float condition = 0.0f;
	if ( !ReadRegister( registers, registerCount, stage.conditionRegister,
			condition, error, stageIndex ) ) {
		return false;
	}
	active = condition != 0.0f;
	return true;
}

static rendererVertexColorMode_t ConvertVertexColor(
		stageVertexColor_t vertexColor,
		classicInteractionBuildError_t &error, int stageIndex ) {
	switch ( vertexColor ) {
	case SVC_IGNORE: return RENDERER_VERTEX_COLOR_IGNORE;
	case SVC_MODULATE: return RENDERER_VERTEX_COLOR_MODULATE;
	case SVC_INVERSE_MODULATE: return RENDERER_VERTEX_COLOR_INVERSE_MODULATE;
	default:
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
			vertexColor, -1, -1, -1, -1, stageIndex );
		return RENDERER_VERTEX_COLOR_IGNORE;
	}
}

static rendererCullMode_t ConvertCull( cullType_t cull,
		classicInteractionBuildError_t &error ) {
	switch ( cull ) {
	case CT_FRONT_SIDED: return RENDERER_CULL_FRONT;
	case CT_BACK_SIDED: return RENDERER_CULL_BACK;
	case CT_TWO_SIDED: return RENDERER_CULL_NONE;
	default:
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE, cull );
		return RENDERER_CULL_FRONT;
	}
}

static bool ImageReady( const idImage *image,
		classicInteractionBuildError_t &error, int stageIndex ) {
	if ( image == NULL ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( R_IsMutableRenderImage( image ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DYNAMIC_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( image->IsDefaulted() ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DEFAULTED_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( !image->IsLoaded() ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNLOADED_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static bool AddTexture( const idImage *image, std::uint64_t &resourceId,
		classicInteractionBuildError_t &error, int stageIndex ) {
	resourceId = 0;
	if ( !ImageReady( image, error, stageIndex ) ) {
		return false;
	}
	const std::uint64_t liveStorageGeneration = image->GetStorageGeneration();
	const int lookupSlot = TextureLookupSlot( image, liveStorageGeneration );
	if ( lookupSlot >= 0
			&& textureLookup[ lookupSlot ].stamp == textureLookupStamp ) {
		resourceId = domain.textures[
			textureLookup[ lookupSlot ].textureIndex ].textureResourceId;
		return true;
	}
	for ( int i = 0; i < domain.textureCount; ++i ) {
		if ( domain.textures[ i ].image == image
				&& domain.textures[ i ].storageGeneration
					== liveStorageGeneration ) {
			if ( lookupSlot >= 0 ) {
				textureLookup[ lookupSlot ].image = image;
				textureLookup[ lookupSlot ].storageGeneration =
					liveStorageGeneration;
				textureLookup[ lookupSlot ].textureIndex = i;
				textureLookup[ lookupSlot ].stamp = textureLookupStamp;
			}
			resourceId = domain.textures[ i ].textureResourceId;
			return true;
		}
	}
	if ( domain.textureCount >= CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_TEXTURE_POOL_OVERFLOW,
			domain.textureCount, -1, -1, -1, -1, stageIndex );
		return false;
	}
	const int textureIndex = domain.textureCount++;
	classicInteractionDomainTexture_t &texture = domain.textures[ textureIndex ];
	std::memset( &texture, 0, sizeof( texture ) );
	texture.textureResourceId = MakeTextureResourceId( textureIndex );
	texture.image = image;
	texture.storageGeneration = image->GetStorageGeneration();
	texture.nameHash = HashString( image->GetName() );
	texture.textureHandle = const_cast<idImage *>( image )->GetDeviceHandle();
	texture.filter = image->GetFilter();
	texture.repeat = image->GetRepeat();
	texture.loaded = image->IsLoaded();
	texture.defaulted = image->IsDefaulted();
	texture.mutableImage = R_IsMutableRenderImage( image );
	if ( lookupSlot >= 0 ) {
		textureLookup[ lookupSlot ].image = image;
		textureLookup[ lookupSlot ].storageGeneration = liveStorageGeneration;
		textureLookup[ lookupSlot ].textureIndex = textureIndex;
		textureLookup[ lookupSlot ].stamp = textureLookupStamp;
	}
	resourceId = texture.textureResourceId;
	return resourceId != 0;
}

static bool TextureStageFixed( const shaderStage_t &stage,
		classicInteractionBuildError_t &error, int stageIndex ) {
	if ( stage.newStage != NULL ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( stage.texture.cinematic != NULL || stage.texture.dynamic != DI_STATIC ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DYNAMIC_RESOURCE,
			stage.texture.dynamic, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( stage.texture.texgen != TG_EXPLICIT ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
			stage.texture.texgen, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static void BakeTextureMatrixIntoProjection( float projection[ 4 ][ 4 ],
		const float textureRows[ 2 ][ 4 ] ) {
	float textureMatrix[ 16 ];
	std::memset( textureMatrix, 0, sizeof( textureMatrix ) );
	textureMatrix[ 0 ] = textureRows[ 0 ][ 0 ];
	textureMatrix[ 4 ] = textureRows[ 0 ][ 1 ];
	textureMatrix[ 12 ] = textureRows[ 0 ][ 3 ];
	textureMatrix[ 1 ] = textureRows[ 1 ][ 0 ];
	textureMatrix[ 5 ] = textureRows[ 1 ][ 1 ];
	textureMatrix[ 13 ] = textureRows[ 1 ][ 3 ];
	textureMatrix[ 10 ] = 1.0f;
	textureMatrix[ 15 ] = 1.0f;

	float generator[ 16 ];
	std::memset( generator, 0, sizeof( generator ) );
	generator[ 0 ] = projection[ 0 ][ 0 ];
	generator[ 4 ] = projection[ 0 ][ 1 ];
	generator[ 8 ] = projection[ 0 ][ 2 ];
	generator[ 12 ] = projection[ 0 ][ 3 ];
	generator[ 1 ] = projection[ 1 ][ 0 ];
	generator[ 5 ] = projection[ 1 ][ 1 ];
	generator[ 9 ] = projection[ 1 ][ 2 ];
	generator[ 13 ] = projection[ 1 ][ 3 ];
	generator[ 3 ] = projection[ 2 ][ 0 ];
	generator[ 7 ] = projection[ 2 ][ 1 ];
	generator[ 11 ] = projection[ 2 ][ 2 ];
	generator[ 15 ] = projection[ 2 ][ 3 ];

	float finalMatrix[ 16 ];
	myGlMultMatrix( generator, textureMatrix, finalMatrix );
	projection[ 0 ][ 0 ] = finalMatrix[ 0 ];
	projection[ 0 ][ 1 ] = finalMatrix[ 4 ];
	projection[ 0 ][ 2 ] = finalMatrix[ 8 ];
	projection[ 0 ][ 3 ] = finalMatrix[ 12 ];
	projection[ 1 ][ 0 ] = finalMatrix[ 1 ];
	projection[ 1 ][ 1 ] = finalMatrix[ 5 ];
	projection[ 1 ][ 2 ] = finalMatrix[ 9 ];
	projection[ 1 ][ 3 ] = finalMatrix[ 13 ];
}

static std::uint64_t StableTextureHash( std::uint64_t resourceId ) {
	const int textureIndex = TextureIndexFromResourceId( resourceId );
	return textureIndex >= 0 && textureIndex < domain.textureCount
		? domain.textures[ textureIndex ].nameHash : 0;
}

static std::uint64_t HashPrimitive(
		const classicInteractionDomainPrimitive_t &primitive,
		int lightArenaBase, int surfaceArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	// Published indices remain absolute arena identities for backend validation,
	// but hashes use view-local identities so unrelated earlier views cannot
	// perturb otherwise identical content.
	HashInt( hash, primitive.lightIndex - lightArenaBase );
	HashInt( hash, primitive.surfaceIndex - surfaceArenaBase );
	HashInt( hash, primitive.lightStageIndex );
	HashInt( hash, primitive.bumpStageIndex );
	HashInt( hash, primitive.diffuseStageIndex );
	HashInt( hash, primitive.specularStageIndex );
	HashInt( hash, primitive.receiver );
	HashInt( hash, primitive.disposition );
	HashInt( hash, primitive.depth );
	HashInt( hash, primitive.cull );
	HashInt( hash, primitive.vertexColor );
	HashBool( hash, primitive.blend.enabled );
	HashInt( hash, primitive.blend.sourceColor );
	HashInt( hash, primitive.blend.destinationColor );
	HashInt( hash, primitive.blend.colorOperation );
	HashInt( hash, primitive.blend.sourceAlpha );
	HashInt( hash, primitive.blend.destinationAlpha );
	HashInt( hash, primitive.blend.alphaOperation );
	HashInt( hash, primitive.vertexCount );
	HashInt( hash, primitive.firstIndex );
	HashInt( hash, primitive.indexCount );
	HashInt( hash, primitive.vertexOffset );
	HashInt( hash, primitive.scissorX1 );
	HashInt( hash, primitive.scissorY1 );
	HashInt( hash, primitive.scissorX2 );
	HashInt( hash, primitive.scissorY2 );
	HashBool( hash, primitive.ambientLight );
	HashBool( hash, primitive.polygonOffsetEnabled );
	HashFloat( hash, primitive.polygonOffsetFactor );
	HashFloat( hash, primitive.polygonOffsetUnits );
	HashU64( hash, StableTextureHash( primitive.lightImageResourceId ) );
	HashU64( hash, StableTextureHash( primitive.lightFalloffImageResourceId ) );
	HashU64( hash, StableTextureHash( primitive.bumpImageResourceId ) );
	HashU64( hash, StableTextureHash( primitive.diffuseImageResourceId ) );
	HashU64( hash, StableTextureHash( primitive.specularImageResourceId ) );
	for ( int component = 0; component < 4; ++component ) {
		HashFloat( hash, primitive.diffuseColor[ component ] );
		HashFloat( hash, primitive.specularColor[ component ] );
		HashFloat( hash, primitive.flatDiffuseParams[ component ] );
		HashFloat( hash, primitive.localLightOrigin[ component ] );
		HashFloat( hash, primitive.localViewOrigin[ component ] );
	}
	for ( int plane = 0; plane < 4; ++plane ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, primitive.lightProjection[ plane ][ component ] );
		}
	}
	for ( int row = 0; row < 2; ++row ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, primitive.bumpMatrix[ row ][ component ] );
			HashFloat( hash, primitive.diffuseMatrix[ row ][ component ] );
			HashFloat( hash, primitive.specularMatrix[ row ][ component ] );
		}
	}
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, primitive.modelMatrix[ i ] );
		HashFloat( hash, primitive.modelViewMatrix[ i ] );
	}
	return hash;
}

static std::uint64_t HashSurface(
		const classicInteractionDomainSurface_t &surface,
		int lightArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, surface.lightIndex - lightArenaBase );
	HashInt( hash, surface.sourceOrdinal );
	HashInt( hash, surface.receiverOrdinal );
	HashInt( hash, surface.receiver );
	HashInt( hash, surface.materialId );
	HashInt( hash, surface.primitiveCount );
	HashInt( hash, surface.drawablePrimitiveCount );
	HashInt( hash, surface.noopPrimitiveCount );
	HashInt( hash, surface.surfaceStageCount );
	HashInt( hash, surface.activeSurfaceStageCount );
	HashInt( hash, surface.inactiveSurfaceStageCount );
	HashInt( hash, surface.vertexCount );
	HashInt( hash, surface.firstIndex );
	HashInt( hash, surface.indexCount );
	HashInt( hash, surface.vertexOffset );
	HashInt( hash, surface.scissorX1 );
	HashInt( hash, surface.scissorY1 );
	HashInt( hash, surface.scissorX2 );
	HashInt( hash, surface.scissorY2 );
	HashInt( hash, surface.deformRole );
	HashInt( hash, surface.deformOutcome );
	HashU64( hash, surface.deformContractHash );
	for ( int i = 0; i < surface.primitiveCount; ++i ) {
		HashU64( hash, domain.primitives[ surface.firstPrimitive + i ].hash );
	}
	return hash;
}

static std::uint64_t HashShadowAlphaStage(
		const classicInteractionDomainShadowAlphaStage_t &stage ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, stage.stageIndex );
	HashInt( hash, stage.alphaTestMode );
	HashFloat( hash, stage.alphaTestValue );
	HashFloat( hash, stage.alphaScale );
	HashFloat( hash, stage.alphaHashMode );
	HashU64( hash, StableTextureHash( stage.textureResourceId ) );
	for ( int row = 0; row < 2; ++row ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, stage.textureMatrix[ row ][ component ] );
		}
	}
	return hash;
}

static std::uint64_t HashShadowCaster(
		const classicInteractionDomainShadowCaster_t &caster,
		int lightArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, caster.lightIndex - lightArenaBase );
	HashInt( hash, caster.sourceOrdinal );
	HashInt( hash, caster.chainOrdinal );
	HashInt( hash, caster.chain );
	HashInt( hash, caster.disposition );
	HashInt( hash, caster.indexSelection );
	HashInt( hash, caster.cull );
	HashInt( hash, caster.materialCoverage );
	HashInt( hash, caster.materialId );
	HashInt( hash, caster.alphaStageCount );
	HashInt( hash, caster.vertexCount );
	HashInt( hash, caster.totalIndexCount );
	HashInt( hash, caster.selectedIndexCount );
	HashInt( hash, caster.scissorX1 );
	HashInt( hash, caster.scissorY1 );
	HashInt( hash, caster.scissorX2 );
	HashInt( hash, caster.scissorY2 );
	HashFloat( hash, caster.depthMin );
	HashFloat( hash, caster.depthMax );
	for ( int component = 0; component < 4; ++component ) {
		HashFloat( hash, caster.localLightOrigin[ component ] );
	}
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, caster.modelMatrix[ i ] );
		HashFloat( hash, caster.modelViewMatrix[ i ] );
	}
	for ( int component = 0; component < 3; ++component ) {
		HashFloat( hash, caster.boundsMin[ component ] );
		HashFloat( hash, caster.boundsMax[ component ] );
	}
	HashBool( hash, caster.external );
	HashBool( hash, caster.preload );
	HashBool( hash, caster.ambientGeometry );
	HashBool( hash, caster.perfectHull );
	HashBool( hash, caster.dynamicCaster );
	HashBool( hash, caster.translucentCaster );
	HashInt( hash, caster.deformRole );
	HashInt( hash, caster.deformOutcome );
	HashU64( hash, caster.deformContractHash );
	for ( int i = 0; i < caster.alphaStageCount; ++i ) {
		HashU64( hash,
			domain.shadowAlphaStages[ caster.firstAlphaStage + i ].hash );
	}
	return hash;
}

static void HashProjectedShadowState( std::uint64_t &hash,
		const classicInteractionDomainShadowProjectedState_t &projected ) {
	const shadowMapProjectedLightState_t &state = projected.state;
	HashBool( hash, state.valid );
	HashBool( hash, state.cascadeFallback );
	HashFloat( hash, state.projectionPad );
	HashFloat( hash, state.projectionScale );
	HashInt( hash, state.fallbackReason );
	HashInt( hash, state.requestedCascadeCount );
	HashInt( hash, state.fallbackCascade );
	HashInt( hash, state.cascadeCount );
	HashInt( hash, state.atlasDiv );
	HashInt( hash, state.tileSize );
	for ( int plane = 0; plane < 4; ++plane ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, state.baseClipPlanes[ plane ][ component ] );
		}
	}
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, state.baseShadowMatrix[ i ] );
	}
	for ( int cascade = 0; cascade < SHADOWMAP_PROJECTED_MAX_CASCADES;
			++cascade ) {
		HashFloat( hash, state.splitDepths[ cascade ] );
		HashFloat( hash, state.biasScale[ cascade ] );
		HashFloat( hash, state.texelDepthBias[ cascade ] );
		HashFloat( hash, state.worldTexelSize[ cascade ] );
		HashFloat( hash, state.sliceNear[ cascade ] );
		HashFloat( hash, state.sliceFar[ cascade ] );
		HashFloat( hash, state.depthRange[ cascade ] );
		HashFloat( hash, state.clipZExtent[ cascade ] );
		for ( int plane = 0; plane < 4; ++plane ) {
			for ( int component = 0; component < 4; ++component ) {
				HashFloat( hash,
					state.clipPlanes[ cascade ][ plane ][ component ] );
			}
		}
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, state.atlasRect[ cascade ][ component ] );
		}
	}
	HashBool( hash, projected.filter.distantSource );
	HashFloat( hash, projected.filter.filterScale );
	HashFloat( hash, projected.filter.filterRadius );
	HashInt( hash, projected.filter.filterTaps );
	HashInt( hash, projected.filter.filterMode );
	HashFloat( hash, projected.filter.pcssLightRadius );
	HashFloat( hash, projected.filter.pcssMaxRadius );
	HashFloat( hash, projected.filter.effectiveFilterRadius );
	HashFloat( hash, projected.constantBias );
	HashFloat( hash, projected.normalBias );
	HashFloat( hash, projected.normalOffsetScale );
	HashFloat( hash, projected.cascadeBlend );
	HashFloat( hash, projected.texelBiasScale );
	HashBool( hash, projected.depthCompare );
	HashBool( hash, projected.receiverPlaneBias );
}

static std::uint64_t HashShadowMapPass(
		const classicInteractionDomainShadowMapPass_t &pass ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, pass.receiver );
	HashInt( hash, pass.resourceOwner );
	HashInt( hash, pass.disposition );
	HashInt( hash, pass.mode );
	HashInt( hash, pass.lightClass );
	HashInt( hash, pass.receiverMask );
	HashInt( hash, pass.mappedCasterCount );
	HashInt( hash, pass.supplementCasterCount );
	HashInt( hash, pass.drawableMappedCasters );
	HashInt( hash, pass.noopMappedCasters );
	HashInt( hash, pass.drawableSupplementCasters );
	HashInt( hash, pass.noopSupplementCasters );
	HashInt( hash, pass.casterSignature );
	HashInt( hash, pass.incompleteMapMask );
	HashInt( hash, pass.incompleteStencilMask );
	HashInt( hash, pass.hybridIncompleteMask );
	HashInt( hash, pass.prelightMapMissingMask );
	HashInt( hash, pass.prelightStencilRequiredMask );
	HashInt( hash, pass.prelightStencilReadyMask );
	HashBool( hash, pass.resourceAlias );
	HashBool( hash, pass.mapRequired );
	HashBool( hash, pass.mapComplete );
	HashBool( hash, pass.stencilComplete );
	HashBool( hash, pass.hybridComplete );
	HashBool( hash, pass.hasStaticCasters );
	HashBool( hash, pass.hasDynamicCasters );
	HashBool( hash, pass.hasAlphaCasters );
	HashBool( hash, pass.hasTranslucentCasters );
	HashBool( hash, pass.allowCacheReuse );
	HashBool( hash, pass.allowCacheUpdate );
	HashBool( hash, pass.allowScratch );
	HashBool( hash, pass.hashedAlpha );
	HashBool( hash, pass.stableAlphaHash );
	HashInt( hash, pass.casterCullMode );
	HashFloat( hash, pass.polygonFactor );
	HashFloat( hash, pass.polygonOffset );
	if ( pass.lightClass == SHADOWMAP_LIGHT_POINT ) {
		HashBool( hash, pass.point.valid );
		HashInt( hash, pass.point.faceCount );
		HashInt( hash, pass.point.faceSize );
		for ( int i = 0; i < 4; ++i ) {
			HashFloat( hash, pass.point.lightOrigin[ i ] );
		}
		HashFloat( hash, pass.point.farDistance );
		HashFloat( hash, pass.point.constantBias );
		HashFloat( hash, pass.point.normalBias );
		HashFloat( hash, pass.point.normalOffsetScale );
		HashFloat( hash, pass.point.texelBiasScale );
		HashFloat( hash, pass.point.filterRadius );
		HashInt( hash, pass.point.filterTaps );
		HashInt( hash, pass.point.filterMode );
		HashBool( hash, pass.point.depthCompare );
		HashBool( hash, pass.point.highPrecision );
	} else {
		HashProjectedShadowState( hash, pass.projected );
	}
	return hash;
}

static std::uint64_t HashLight(
		const classicInteractionDomainLight_t &light ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, light.sourceOrdinal );
	HashInt( hash, light.surfaceCount );
	HashInt( hash, light.primitiveCount );
	HashInt( hash, light.drawablePrimitiveCount );
	HashInt( hash, light.noopPrimitiveCount );
	HashInt( hash, light.lightStageCount );
	HashInt( hash, light.activeLightStageCount );
	HashInt( hash, light.inactiveLightStageCount );
	for ( int receiver = 0; receiver < CLASSIC_INTERACTION_RECEIVER_COUNT;
			receiver++ ) {
		HashInt( hash, light.receiverSurfaceCount[ receiver ] );
		HashInt( hash, light.receiverPrimitiveCount[ receiver ] );
		HashInt( hash, light.receiverShadowMode[ receiver ] );
		if ( receiver < 2 && light.shadowMapPassIndex[ receiver ] >= 0 ) {
			HashU64( hash, domain.shadowMapPasses[
				light.shadowMapPassIndex[ receiver ] ].hash );
		}
	}
	for ( int chain = 0; chain < CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT;
			++chain ) {
		HashInt( hash, light.shadowCasterCount[ chain ] );
	}
	HashInt( hash, light.shadowCasterTotal );
	HashInt( hash, light.drawableShadowCasters );
	HashInt( hash, light.noopShadowCasters );
	HashInt( hash, light.logicalVolumeDraws );
	HashInt( hash, light.preloadVolumeDraws );
	HashInt( hash, light.scissorX1 );
	HashInt( hash, light.scissorY1 );
	HashInt( hash, light.scissorX2 );
	HashInt( hash, light.scissorY2 );
	for ( int component = 0; component < 4; ++component ) {
		HashFloat( hash, light.globalLightOrigin[ component ] );
		HashFloat( hash, light.lightRadius[ component ] );
	}
	for ( int plane = 0; plane < 4; ++plane ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, light.lightProject[ plane ][ component ] );
		}
	}
	HashBool( hash, light.pointLight );
	HashBool( hash, light.parallel );
	HashBool( hash, light.ambientLight );
	HashBool( hash, light.shadowClassified );
	HashBool( hash, light.clearStencil );
	for ( int chain = 0; chain < CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT;
			++chain ) {
		for ( int i = 0; i < light.shadowCasterCount[ chain ]; ++i ) {
			HashU64( hash, domain.shadowCasters[
				light.firstShadowCaster[ chain ] + i ].hash );
		}
	}
	for ( int i = 0; i < light.surfaceCount; ++i ) {
		HashU64( hash, domain.surfaces[ light.firstSurface + i ].hash );
	}
	return hash;
}

static std::uint64_t HashView( const classicInteractionDomainView_t &view ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, view.lightCount );
	HashInt( hash, view.surfaceCount );
	HashInt( hash, view.primitiveCount );
	HashInt( hash, view.drawablePrimitiveCount );
	HashInt( hash, view.noopPrimitiveCount );
	HashInt( hash, view.shadowCasterCount );
	HashInt( hash, view.drawableShadowCasterCount );
	HashInt( hash, view.noopShadowCasterCount );
	HashInt( hash, view.logicalVolumeDrawCount );
	HashInt( hash, view.preloadVolumeDrawCount );
	HashInt( hash, view.shadowLightCount );
	HashInt( hash, view.shadowMapPassCount );
	HashInt( hash, view.hybridShadowPassCount );
	HashInt( hash, view.projectedShadowMapPassCount );
	HashInt( hash, view.csmShadowMapPassCount );
	HashInt( hash, view.pointShadowMapPassCount );
	HashInt( hash, view.projectedShadowLightCount );
	HashInt( hash, view.pointShadowLightCount );
	HashInt( hash, view.packetDrawCount );
	HashInt( hash, view.shadowPacketDrawCount );
	HashInt( hash, view.activeLightStageCount );
	HashInt( hash, view.inactiveLightStageCount );
	HashInt( hash, view.activeSurfaceStageCount );
	HashInt( hash, view.inactiveSurfaceStageCount );
	HashInt( hash, view.materialDeformReceiverCount );
	HashInt( hash, view.completedDeformCasterCount );
	HashInt( hash, view.emptyDeformCasterCount );
	for ( int receiver = 0; receiver < CLASSIC_INTERACTION_RECEIVER_COUNT;
			receiver++ ) {
		HashInt( hash, view.receiverSurfaceCount[ receiver ] );
		HashInt( hash, view.receiverPrimitiveCount[ receiver ] );
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
	HashFloat( hash, view.maxLightValue );
	HashFloat( hash, view.lightScale );
	HashFloat( hash, view.overBright );
	HashFloat( hash, view.shadowPolygonFactor );
	HashFloat( hash, view.shadowPolygonUnits );
	HashInt( hash, view.stencilReference );
	HashInt( hash, view.shadowMode );
	HashBool( hash, view.useScissor );
	HashBool( hash, view.useShadowVertexProgram );
	HashBool( hash, view.preferTwoSidedStencil );
	HashBool( hash, view.useDepthBounds );
	HashBool( hash, view.stencilTranslucentShadows );
	for ( int i = 0; i < view.lightCount; ++i ) {
		HashU64( hash, domain.lights[ view.firstLight + i ].hash );
	}
	return hash;
}

static bool SurfacePacketEligible( const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && drawSurf->geo != NULL
		&& drawSurf->geo->numIndexes > 0 && drawSurf->space != NULL
		&& drawSurf->material != NULL
		&& drawSurf->material->ReceivesLighting()
		&& !drawSurf->material->IsPortalSky();
}

static bool ShadowPacketSurfaceEligible( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->geo == NULL
			|| drawSurf->geo->numVerts <= 0
			|| drawSurf->geo->numIndexes <= 0 ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	if ( material == NULL ) {
		return true;
	}
	if ( material->IsDedicatedCollisionSurface()
			|| material->HasGui() || material->HasSubview() ) {
		return false;
	}
	return material->Coverage() == MC_TRANSLUCENT
		|| material->SurfaceCastsShadow();
}

static bool ViewLightHasInteractions( const viewLight_t *viewLight ) {
	return viewLight != NULL && viewLight->lightShader != NULL
		&& !viewLight->lightShader->IsFogLight()
		&& !viewLight->lightShader->IsBlendLight()
		&& ( viewLight->localInteractions != NULL
			|| viewLight->globalInteractions != NULL
			|| viewLight->translucentInteractions != NULL );
}

static bool ViewLightContributesToClassicScale( const viewLight_t *viewLight ) {
	// RB_DetermineLightScale deliberately considers every light with any
	// interaction receiver chain.  Fog and blend classification does not alter
	// that membership, even though those lights are not owned by this domain.
	return viewLight != NULL
		&& ( viewLight->localInteractions != NULL
			|| viewLight->globalInteractions != NULL
			|| viewLight->translucentInteractions != NULL );
}

static bool AccumulateClassicLightScaleValue( float authoredScale,
		float component, float &maximum ) {
	const float value = authoredScale * component;
	if ( !FloatIsFinite( value ) ) {
		return false;
	}
	if ( value > maximum ) {
		maximum = value;
	}
	return true;
}

static bool ViewLightHasMappedShadowState( const viewLight_t &viewLight ) {
	return viewLight.globalShadowMapStencilSupplements != NULL
		|| viewLight.localShadowMapStencilSupplements != NULL
		|| viewLight.localShadowMapCasters != NULL
		|| viewLight.globalShadowMapCasters != NULL
		|| viewLight.localShadowMapDynamicCasters != NULL
		|| viewLight.globalShadowMapDynamicCasters != NULL
		|| viewLight.localTranslucentShadowMapCasters != NULL
		|| viewLight.globalTranslucentShadowMapCasters != NULL;
}

static bool SceneShadowChainToDomain( sceneShadowCasterClass_t source,
		classicInteractionDomainShadowChain_t &destination ) {
	switch ( source ) {
	case SCENE_SHADOW_CASTER_STENCIL_GLOBAL:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL;
		return true;
	case SCENE_SHADOW_CASTER_STENCIL_LOCAL:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL;
		return true;
	case SCENE_SHADOW_CASTER_MAP_GLOBAL_STATIC:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC;
		return true;
	case SCENE_SHADOW_CASTER_MAP_LOCAL_STATIC:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC;
		return true;
	case SCENE_SHADOW_CASTER_MAP_GLOBAL_DYNAMIC:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC;
		return true;
	case SCENE_SHADOW_CASTER_MAP_LOCAL_DYNAMIC:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC;
		return true;
	case SCENE_SHADOW_CASTER_MAP_GLOBAL_TRANSLUCENT:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_TRANSLUCENT;
		return true;
	case SCENE_SHADOW_CASTER_MAP_LOCAL_TRANSLUCENT:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT;
		return true;
	case SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL;
		return true;
	case SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL:
		destination = CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL;
		return true;
	case SCENE_SHADOW_CASTER_NONE:
	case SCENE_SHADOW_CASTER_COUNT:
	default:
		return false;
	}
}

static bool ValidateShadowPacket( const idScenePacketFrame &packetFrame,
		const drawPacket_t &packet, int packetIndex, const viewDef_t *viewDef,
		const viewLight_t *viewLight, int lightOrdinal,
		renderPassCategory_t passCategory,
		sceneShadowCasterClass_t casterClass, int chainOrdinal,
		int sourceOrdinal, const drawSurf_t *drawSurf,
		classicInteractionBuildError_t &error ) {
	if ( packet.passCategory != passCategory
			|| packet.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| packet.viewDef != viewDef || packet.legacyDrawSurf != drawSurf
			|| packet.shadowLight != viewLight
			|| packet.shadowLightOrdinal != lightOrdinal
			|| packet.shadowCasterClass != casterClass
			|| packet.shadowChainOrdinal != chainOrdinal
			|| packet.shadowSourceOrdinal != sourceOrdinal ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
			sourceOrdinal, -1, packetIndex, lightOrdinal, chainOrdinal );
		return false;
	}
	if ( drawSurf == NULL || drawSurf->geo == NULL || drawSurf->space == NULL
			|| packet.geometryRecordIndex < 0
			|| packet.geometryRecordIndex >= packetFrame.NumGeometryRecords()
			|| packet.geometryRecord == NULL
			|| packet.geometryRecord
				!= &packetFrame.GeometryRecord( packet.geometryRecordIndex )
			|| packet.geometryRecord->legacyGeometry != drawSurf->geo
			|| packet.instanceRecordIndex < 0
			|| packet.instanceRecordIndex >= packetFrame.NumInstanceRecords()
			|| packet.instanceRecord == NULL
			|| packet.instanceRecord
				!= &packetFrame.InstanceRecord( packet.instanceRecordIndex )
			|| packet.instanceRecord->legacySpace != drawSurf->space ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY,
			packet.geometryRecordIndex, -1, packetIndex, lightOrdinal,
			chainOrdinal );
		return false;
	}
	const geometryResourceRecord_t &geometry = *packet.geometryRecord;
	const instanceRecord_t &instance = *packet.instanceRecord;
	const bool stencilPacket = passCategory == RENDER_PASS_STENCIL_SHADOW
		|| casterClass == SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL
		|| casterClass == SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL;
	bool admittedMaterialDeform = false;
	if ( !stencilPacket && MaterialRequestsClassicDeform( drawSurf )
			&& r_rendererSharedDeform.GetBool() ) {
		classicDeformRole_t role;
		classicDeformOutcome_t outcome;
		std::uint64_t semanticHash = 0;
		if ( !ValidateDeformContract( drawSurf, &packet,
				CLASSIC_DEFORM_ROLE_FINALIZED_DRAW, role, outcome,
				semanticHash ) ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT,
				DeformContractFailureDetail( drawSurf, &packet ), -1, packetIndex,
				lightOrdinal, chainOrdinal );
			return false;
		}
		admittedMaterialDeform = true;
	}
	const srfTriangles_t *tri = drawSurf->geo;
	const srfTriangles_t *casterTri = !stencilPacket
		&& tri->ambientSurface != NULL ? tri->ambientSurface : tri;
	if ( !packet.hasGeometry || geometry.legacyGeometry == NULL
			|| ( geometry.deformMode != GEOMETRY_DEFORM_NONE
				&& !admittedMaterialDeform )
			|| ( geometry.skinningMode != GEOMETRY_SKINNING_NONE
				&& geometry.skinningMode != GEOMETRY_SKINNING_CPU )
			|| geometry.hasPrimBatchMesh || tri->deformedSurface
			|| casterTri == NULL || casterTri->numVerts <= 0
			|| casterTri->numIndexes <= 0 || casterTri->numIndexes % 3 != 0
			|| instance.weaponDepthHack || instance.modelDepthHack != 0.0f
			|| instance.negativeScale
			|| MatrixHasNegativeScale( instance.modelMatrix )
			|| !FloatsAreFinite( instance.modelMatrix, 16 )
			|| !FloatsAreFinite( instance.modelViewMatrix, 16 )
			|| !FloatIsFinite( drawSurf->scissorRect.zmin )
			|| !FloatIsFinite( drawSurf->scissorRect.zmax )
			|| !FloatsAreFinite( casterTri->bounds[0].ToFloatPtr(), 3 )
			|| !FloatsAreFinite( casterTri->bounds[1].ToFloatPtr(), 3 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY,
			casterTri != NULL ? casterTri->numIndexes : 0, -1, packetIndex,
			lightOrdinal, chainOrdinal );
		return false;
	}
	if ( stencilPacket ) {
		if ( tri->shadowCache == NULL || tri->indexes == NULL
				|| tri->numShadowIndexesNoFrontCaps < 0
				|| tri->numShadowIndexesNoFrontCaps > tri->numIndexes
				|| tri->numShadowIndexesNoCaps < 0
				|| tri->numShadowIndexesNoCaps > tri->numIndexes
				|| tri->numShadowIndexesNoFrontCaps % 3 != 0
				|| tri->numShadowIndexesNoCaps % 3 != 0 ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY,
				tri->numIndexes, -1, packetIndex, lightOrdinal,
				chainOrdinal );
			return false;
		}
	} else {
		if ( casterTri->ambientCache == NULL
				|| ( casterTri->indexCache == NULL
					&& casterTri->indexes == NULL )
				|| drawSurf->material == NULL
				|| drawSurf->shaderRegisters == NULL
				|| packet.materialRecordIndex < 0
				|| packet.materialRecordIndex
					>= packetFrame.NumMaterialRecords()
				|| packet.materialRecord == NULL
				|| packet.materialRecord
					!= &packetFrame.MaterialRecord(
						packet.materialRecordIndex )
				|| packet.materialRecord->material != drawSurf->material
				|| !packet.hasShaderRegisters
				|| !instance.hasShaderRegisters
				|| instance.legacyShaderRegisters
					!= drawSurf->shaderRegisters ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY,
				casterTri->numIndexes, -1, packetIndex, lightOrdinal,
				chainOrdinal );
			return false;
		}
	}
	return true;
}

static bool PrepareStencilShadowChain(
		const idScenePacketFrame &packetFrame,
		const classicInteractionDomainView_t &view,
		classicInteractionDomainLight_t &light, const drawSurf_t *chain,
		sceneShadowCasterClass_t packetClass,
		classicInteractionDomainShadowChain_t domainChain,
		renderPassCategory_t passCategory, int passPacketIndex,
		int &packetCursor, int packetEnd,
		int lightArenaBase, classicInteractionBuildError_t &error ) {
	if ( chain == NULL ) {
		return true;
	}
	light.firstShadowCaster[ domainChain ] = domain.shadowCasterCount;
	int chainOrdinal = 0;
	for ( const drawSurf_t *drawSurf = chain; drawSurf != NULL;
			drawSurf = drawSurf->nextOnLight, ++chainOrdinal ) {
		if ( !ShadowPacketSurfaceEligible( drawSurf ) ) {
			continue;
		}
		if ( packetCursor < 0 || packetCursor >= packetEnd ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
				packetCursor, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		const drawPacket_t &packet = packetFrame.DrawPacket( packetCursor );
		classicInteractionDomainShadowChain_t packetDomainChain;
		if ( !SceneShadowChainToDomain( packet.shadowCasterClass,
				packetDomainChain ) || packetDomainChain != domainChain ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
				packet.shadowCasterClass, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		const int sourceOrdinal = packetCursor
			- packetFrame.Pass( passPacketIndex ).firstDrawPacket;
		if ( !ValidateShadowPacket( packetFrame, packet, packetCursor,
				view.viewDef, light.legacyViewLight, light.sourceOrdinal,
				passCategory,
				packetClass, chainOrdinal, sourceOrdinal, drawSurf, error ) ) {
			return false;
		}
		if ( domain.shadowCasterCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW,
				domain.shadowCasterCount, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}

		classicInteractionDomainShadowCaster_t caster;
		InitShadowCaster( caster );
		caster.legacyDrawSurf = drawSurf;
		caster.legacyViewLight = light.legacyViewLight;
		caster.legacyCasterGeometry = drawSurf->geo;
		caster.perfectHull = drawSurf->geo->perfectHull;
		caster.drawPacketIndex = packetCursor;
		caster.lightIndex = static_cast<int>( &light - domain.lights );
		caster.sourceOrdinal = sourceOrdinal;
		caster.chainOrdinal = chainOrdinal;
		caster.geometryRecordIndex = packet.geometryRecordIndex;
		caster.instanceRecordIndex = packet.instanceRecordIndex;
		caster.chain = domainChain;
		caster.vertexCount = drawSurf->geo->numVerts;
		caster.totalIndexCount = drawSurf->geo->numIndexes;
		caster.scissorX1 = drawSurf->scissorRect.x1;
		caster.scissorY1 = drawSurf->scissorRect.y1;
		caster.scissorX2 = drawSurf->scissorRect.x2;
		caster.scissorY2 = drawSurf->scissorRect.y2;
		caster.depthMin = idMath::ClampFloat( 0.0f, 1.0f,
			drawSurf->scissorRect.zmin );
		caster.depthMax = idMath::ClampFloat( caster.depthMin, 1.0f,
			drawSurf->scissorRect.zmax );
		std::memcpy( caster.modelMatrix, packet.instanceRecord->modelMatrix,
			sizeof( caster.modelMatrix ) );
		std::memcpy( caster.modelViewMatrix,
			packet.instanceRecord->modelViewMatrix,
			sizeof( caster.modelViewMatrix ) );
		std::memcpy( caster.boundsMin,
			drawSurf->geo->bounds[0].ToFloatPtr(), sizeof( caster.boundsMin ) );
		std::memcpy( caster.boundsMax,
			drawSurf->geo->bounds[1].ToFloatPtr(), sizeof( caster.boundsMax ) );
		if ( view.useShadowVertexProgram ) {
			idVec4 localLight;
			R_GlobalPointToLocal( drawSurf->space->modelMatrix,
				light.legacyViewLight->globalLightOrigin,
				localLight.ToVec3() );
			localLight.w = 0.0f;
			std::memcpy( caster.localLightOrigin, localLight.ToFloatPtr(),
				sizeof( caster.localLightOrigin ) );
		}

		const srfTriangles_t *tri = drawSurf->geo;
		const int externalSetting = r_useExternalShadows.GetInteger();
		if ( externalSetting == 0 ) {
			caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_FULL;
			caster.selectedIndexCount = tri->numIndexes;
		} else if ( externalSetting == 2
				|| !( drawSurf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
			caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_NO_CAPS;
			caster.selectedIndexCount = tri->numShadowIndexesNoCaps;
			caster.external = externalSetting != 2;
		} else if ( !light.legacyViewLight->viewInsideLight
				&& !( tri->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
			if ( light.legacyViewLight->viewSeesShadowPlaneBits
					& tri->shadowCapPlaneBits ) {
				caster.indexSelection =
					CLASSIC_INTERACTION_SHADOW_INDEX_NO_FRONT_CAPS;
				caster.selectedIndexCount =
					tri->numShadowIndexesNoFrontCaps;
			} else {
				caster.indexSelection =
					CLASSIC_INTERACTION_SHADOW_INDEX_NO_CAPS;
				caster.selectedIndexCount = tri->numShadowIndexesNoCaps;
			}
			caster.external = true;
		} else {
			caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_FULL;
			caster.selectedIndexCount = tri->numIndexes;
		}
		if ( caster.selectedIndexCount == tri->numIndexes ) {
			caster.external = false;
			caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_FULL;
		}
		caster.preload = !caster.external && caster.selectedIndexCount > 0;
		caster.disposition = caster.selectedIndexCount > 0
			? CLASSIC_INTERACTION_SHADOW_CASTER_DRAW
			: CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY;
		caster.hash = HashShadowCaster( caster, lightArenaBase );
		domain.shadowCasters[ domain.shadowCasterCount++ ] = caster;
		light.shadowCasterCount[ domainChain ]++;
		light.shadowCasterTotal++;
		if ( caster.disposition == CLASSIC_INTERACTION_SHADOW_CASTER_DRAW ) {
			light.drawableShadowCasters++;
			light.logicalVolumeDraws++;
			if ( caster.preload ) {
				light.preloadVolumeDraws++;
			}
		} else {
			light.noopShadowCasters++;
		}
		packetCursor++;
	}
	return true;
}

static float ShadowMapAlphaHashMode( const float modelMatrix[ 16 ] ) {
	if ( !r_shadowMapHashedAlpha.GetBool() ) {
		return 0.0f;
	}
	if ( !r_shadowMapStableAlphaHash.GetBool() ) {
		return 1.0f;
	}
	const float seedBase =
		idMath::Floor( modelMatrix[ 12 ] * 0.5f ) * 0.06711056f
		+ idMath::Floor( modelMatrix[ 13 ] * 0.5f ) * 0.00583715f
		+ idMath::Floor( modelMatrix[ 14 ] * 0.5f ) * 0.01327111f;
	return 2.0f + seedBase - idMath::Floor( seedBase );
}

static bool AppendEmptyMappedDeformCaster(
		classicInteractionDomainLight_t &light, const drawSurf_t *drawSurf,
		classicInteractionDomainShadowChain_t domainChain, int chainOrdinal,
		int sourceOrdinal, int passPacketIndex, int packetCursor,
		int lightArenaBase, classicInteractionBuildError_t &error ) {
	classicDeformRole_t role;
	classicDeformOutcome_t outcome;
	std::uint64_t semanticHash = 0;
	if ( !ValidateDeformContract( drawSurf, NULL,
			CLASSIC_DEFORM_ROLE_FINALIZED_DRAW, role, outcome,
			semanticHash ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT,
			DeformContractFailureDetail( drawSurf ), passPacketIndex,
			packetCursor, light.sourceOrdinal, chainOrdinal );
		return false;
	}
	if ( outcome != CLASSIC_DEFORM_OUTCOME_EMPTY ) {
		return true;
	}
	const materialCoverage_t coverage = drawSurf != NULL
		&& drawSurf->material != NULL ? drawSurf->material->Coverage()
		: MC_BAD;
	if ( drawSurf == NULL || drawSurf->geo == NULL || drawSurf->space == NULL
			|| drawSurf->material == NULL
			|| ( coverage != MC_OPAQUE && coverage != MC_PERFORATED )
			|| drawSurf->classicDeform.resultGeometry.indexCount != 0
			|| domain.shadowCasterCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ) {
		SetError( error, domain.shadowCasterCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS
			? CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW
			: CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT,
			DeformContractFailureDetail( drawSurf ), passPacketIndex,
			packetCursor, light.sourceOrdinal, chainOrdinal );
		return false;
	}
	classicInteractionDomainShadowCaster_t caster;
	InitShadowCaster( caster );
	caster.legacyDrawSurf = drawSurf;
	caster.legacyViewLight = light.legacyViewLight;
	caster.legacyCasterGeometry = drawSurf->geo;
	caster.perfectHull = drawSurf->geo->perfectHull;
	caster.lightIndex = static_cast<int>( &light - domain.lights );
	caster.sourceOrdinal = sourceOrdinal;
	caster.chainOrdinal = chainOrdinal;
	caster.chain = domainChain;
	caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_AMBIENT;
	caster.cull = ConvertCull( drawSurf->material->GetCullType(), error );
	if ( error.failure != CLASSIC_INTERACTION_FAILURE_NONE ) {
		return false;
	}
	caster.materialCoverage = coverage;
	caster.scissorX1 = drawSurf->scissorRect.x1;
	caster.scissorY1 = drawSurf->scissorRect.y1;
	caster.scissorX2 = drawSurf->scissorRect.x2;
	caster.scissorY2 = drawSurf->scissorRect.y2;
	caster.depthMin = idMath::ClampFloat( 0.0f, 1.0f,
		drawSurf->scissorRect.zmin );
	caster.depthMax = idMath::ClampFloat( caster.depthMin, 1.0f,
		drawSurf->scissorRect.zmax );
	std::memcpy( caster.modelMatrix, drawSurf->space->modelMatrix,
		sizeof( caster.modelMatrix ) );
	std::memcpy( caster.modelViewMatrix, drawSurf->space->modelViewMatrix,
		sizeof( caster.modelViewMatrix ) );
	std::memcpy( caster.boundsMin, drawSurf->geo->bounds[0].ToFloatPtr(),
		sizeof( caster.boundsMin ) );
	std::memcpy( caster.boundsMax, drawSurf->geo->bounds[1].ToFloatPtr(),
		sizeof( caster.boundsMax ) );
	caster.ambientGeometry = true;
	caster.dynamicCaster = domainChain
		== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC
		|| domainChain == CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC;
	caster.deformRole = role;
	caster.deformOutcome = outcome;
	caster.deformContractHash = semanticHash;
	caster.hash = HashShadowCaster( caster, lightArenaBase );
	domain.shadowCasters[ domain.shadowCasterCount++ ] = caster;
	light.shadowCasterCount[ domainChain ]++;
	light.shadowCasterTotal++;
	light.noopShadowCasters++;
	return true;
}

static bool PrepareMappedShadowChain(
		const idScenePacketFrame &packetFrame,
		classicInteractionDomainView_t &view,
		classicInteractionDomainLight_t &light, const drawSurf_t *chain,
		sceneShadowCasterClass_t packetClass,
		classicInteractionDomainShadowChain_t domainChain,
		int passPacketIndex, int &packetCursor, int packetEnd,
		int lightArenaBase, classicInteractionBuildError_t &error ) {
	if ( chain == NULL ) {
		return true;
	}
	if ( domainChain == CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_TRANSLUCENT
			|| domainChain
				== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT ) {
		// Moment-map caster ownership remains outside this fixed-classic
		// corridor. Reject the whole view instead of turning blended coverage
		// into opaque depth or silently omitting it.
		SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
			domainChain, passPacketIndex, packetCursor,
			light.sourceOrdinal );
		return false;
	}

	light.firstShadowCaster[ domainChain ] = domain.shadowCasterCount;
	int chainOrdinal = 0;
	for ( const drawSurf_t *drawSurf = chain; drawSurf != NULL;
			drawSurf = drawSurf->nextOnLight, ++chainOrdinal ) {
		if ( !ShadowPacketSurfaceEligible( drawSurf ) ) {
			if ( MaterialRequestsClassicDeform( drawSurf ) ) {
				// The validator owns both switch isolation and the explicit
				// EMPTY proof. Never let an ineligible deform caster disappear
				// from the chain merely because deform ownership is disabled.
				if ( !AppendEmptyMappedDeformCaster( light, drawSurf,
						domainChain, chainOrdinal,
						packetCursor
							- packetFrame.Pass( passPacketIndex ).firstDrawPacket,
						passPacketIndex, packetCursor, lightArenaBase,
							error ) ) {
					return false;
				}
			}
			continue;
		}
		if ( packetCursor < 0 || packetCursor >= packetEnd ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
				packetCursor, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		const drawPacket_t &packet = packetFrame.DrawPacket( packetCursor );
		classicInteractionDomainShadowChain_t packetDomainChain;
		if ( !SceneShadowChainToDomain( packet.shadowCasterClass,
				packetDomainChain ) || packetDomainChain != domainChain ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
				packet.shadowCasterClass, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		const int sourceOrdinal = packetCursor
			- packetFrame.Pass( passPacketIndex ).firstDrawPacket;
		if ( !ValidateShadowPacket( packetFrame, packet, packetCursor,
				view.viewDef, light.legacyViewLight, light.sourceOrdinal,
				RENDER_PASS_SHADOW_MAP, packetClass, chainOrdinal,
				sourceOrdinal, drawSurf, error ) ) {
			return false;
		}
		if ( domain.shadowCasterCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW,
				domain.shadowCasterCount, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}

		const idMaterial *material = drawSurf->material;
		const materialCoverage_t coverage = material->Coverage();
		if ( coverage != MC_OPAQUE && coverage != MC_PERFORATED
				|| ( material->Deform() != DFRM_NONE
					&& !r_rendererSharedDeform.GetBool() )
				|| material->HasGui() || material->HasSubview()
				|| drawSurf->dynamicTexCoords != NULL
				|| drawSurf->texGenTransformAndViewOrg != NULL
				|| drawSurf->decalColorCache != NULL
				|| drawSurf->decalColorStageCount != 0 ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
				coverage, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		const materialResourceTableRecord_t *tableRecord =
			R_MaterialResourceTable_RecordForIndex(
				packet.materialRecordIndex );
		if ( tableRecord == NULL || tableRecord->material != material
				|| tableRecord->sourceMaterialRecordIndex
					!= packet.materialRecordIndex
				|| tableRecord->tableGeneration == 0
				|| ( view.tableGeneration != 0
					&& view.tableGeneration
						!= tableRecord->tableGeneration )
				|| packet.instanceRecord->shaderRegisterCount
					!= material->GetNumRegisters()
				|| tableRecord->registerCount
					!= material->GetNumRegisters() ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_MISSING_MATERIAL_RECORD,
				packet.materialRecordIndex, passPacketIndex, packetCursor,
				light.sourceOrdinal, chainOrdinal );
			return false;
		}
		view.tableGeneration = tableRecord->tableGeneration;

		const srfTriangles_t *casterGeometry =
			drawSurf->geo->ambientSurface != NULL
				? drawSurf->geo->ambientSurface : drawSurf->geo;
		classicInteractionDomainShadowCaster_t caster;
		InitShadowCaster( caster );
		caster.legacyDrawSurf = drawSurf;
		caster.legacyViewLight = light.legacyViewLight;
		caster.legacyCasterGeometry = casterGeometry;
		caster.perfectHull = casterGeometry->perfectHull;
		caster.drawPacketIndex = packetCursor;
		caster.lightIndex = static_cast<int>( &light - domain.lights );
		caster.sourceOrdinal = sourceOrdinal;
		caster.chainOrdinal = chainOrdinal;
		caster.geometryRecordIndex = packet.geometryRecordIndex;
		caster.instanceRecordIndex = packet.instanceRecordIndex;
		caster.materialTableRecordIndex = packet.materialRecordIndex;
		caster.materialId = tableRecord->materialId;
		caster.tableGeneration = tableRecord->tableGeneration;
		caster.chain = domainChain;
		caster.indexSelection = CLASSIC_INTERACTION_SHADOW_INDEX_AMBIENT;
		caster.cull = ConvertCull( material->GetCullType(), error );
		if ( error.failure != CLASSIC_INTERACTION_FAILURE_NONE ) {
			return false;
		}
		caster.materialCoverage = coverage;
		caster.vertexCount = casterGeometry->numVerts;
		caster.totalIndexCount = casterGeometry->numIndexes;
		caster.selectedIndexCount = casterGeometry->numIndexes;
		caster.scissorX1 = drawSurf->scissorRect.x1;
		caster.scissorY1 = drawSurf->scissorRect.y1;
		caster.scissorX2 = drawSurf->scissorRect.x2;
		caster.scissorY2 = drawSurf->scissorRect.y2;
		caster.depthMin = idMath::ClampFloat( 0.0f, 1.0f,
			drawSurf->scissorRect.zmin );
		caster.depthMax = idMath::ClampFloat( caster.depthMin, 1.0f,
			drawSurf->scissorRect.zmax );
		std::memcpy( caster.modelMatrix,
			packet.instanceRecord->modelMatrix, sizeof( caster.modelMatrix ) );
		std::memcpy( caster.modelViewMatrix,
			packet.instanceRecord->modelViewMatrix,
			sizeof( caster.modelViewMatrix ) );
		std::memcpy( caster.boundsMin,
			casterGeometry->bounds[0].ToFloatPtr(),
			sizeof( caster.boundsMin ) );
		std::memcpy( caster.boundsMax,
			casterGeometry->bounds[1].ToFloatPtr(),
			sizeof( caster.boundsMax ) );
		caster.ambientGeometry = true;
		caster.dynamicCaster = domainChain
			== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC
			|| domainChain
				== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC;
		if ( MaterialRequestsClassicDeform( drawSurf ) ) {
			caster.deformRole = drawSurf->classicDeform.role;
			caster.deformOutcome = drawSurf->classicDeform.outcome;
			caster.deformContractHash = drawSurf->classicDeform.semanticHash;
		}

		bool activeAlphaStage = false;
		bool drawableAlphaStage = false;
		if ( coverage == MC_PERFORATED ) {
			caster.firstAlphaStage = domain.shadowAlphaStageCount;
			const float *registers = drawSurf->shaderRegisters;
			const int registerCount = material->GetNumRegisters();
			for ( int stageIndex = 0;
					stageIndex < material->GetNumStages(); ++stageIndex ) {
				const shaderStage_t *stage = material->GetStage( stageIndex );
				if ( stage == NULL || !stage->hasAlphaTest ) {
					continue;
				}
				bool active = false;
				if ( !EvaluateCondition( *stage, registers, registerCount,
						active, error, stageIndex ) ) {
					return false;
				}
				if ( !active ) {
					continue;
				}
				activeAlphaStage = true;
				float alphaScale = 0.0f;
				if ( !ReadRegister( registers, registerCount,
						stage->color.registers[3], alphaScale, error,
						stageIndex ) ) {
					return false;
				}
				if ( alphaScale <= 0.0f ) {
					continue;
				}
				if ( stage->alphaTestMode != GL_LESS
						&& stage->alphaTestMode != GL_EQUAL
						&& stage->alphaTestMode != GL_GREATER
						|| !TextureStageFixed( *stage, error, stageIndex ) ) {
					if ( error.failure == CLASSIC_INTERACTION_FAILURE_NONE ) {
						SetError( error,
							CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
							stage->alphaTestMode, passPacketIndex,
							packetCursor, light.sourceOrdinal,
							chainOrdinal, stageIndex );
					}
					return false;
				}
				if ( domain.shadowAlphaStageCount
						>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES ) {
					SetError( error,
						CLASSIC_INTERACTION_FAILURE_SHADOW_ALPHA_STAGE_POOL_OVERFLOW,
						domain.shadowAlphaStageCount, passPacketIndex,
						packetCursor, light.sourceOrdinal,
						chainOrdinal, stageIndex );
					return false;
				}
				classicInteractionDomainShadowAlphaStage_t alpha;
				InitShadowAlphaStage( alpha );
				alpha.casterIndex = domain.shadowCasterCount;
				alpha.stageIndex = stageIndex;
				alpha.alphaTestMode = stage->alphaTestMode;
				if ( !ReadRegister( registers, registerCount,
						stage->alphaTestRegister, alpha.alphaTestValue,
						error, stageIndex )
						|| !EvaluateTextureMatrix( stage->texture,
							registers, registerCount, alpha.textureMatrix,
							error, stageIndex )
						|| !AddTexture( stage->texture.image,
							alpha.textureResourceId, error, stageIndex ) ) {
					return false;
				}
				alpha.alphaScale = alphaScale;
				alpha.alphaHashMode = ShadowMapAlphaHashMode(
					caster.modelMatrix );
				alpha.hash = HashShadowAlphaStage( alpha );
				domain.shadowAlphaStages[
					domain.shadowAlphaStageCount++ ] = alpha;
				caster.alphaStageCount++;
				drawableAlphaStage = true;
			}
		}
		caster.disposition = coverage != MC_PERFORATED
				|| !activeAlphaStage || drawableAlphaStage
			? CLASSIC_INTERACTION_SHADOW_CASTER_DRAW
			: CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY;
		if ( caster.disposition
				== CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY ) {
			// Match the classic perforated caster loop: an active alpha-test
			// stage whose evaluated alpha scale is zero emits no indexes. Keep
			// the full geometry identity in totalIndexCount, but seal the work
			// count as zero so both backends consume the deliberate no-op.
			caster.selectedIndexCount = 0;
		}
		caster.hash = HashShadowCaster( caster, lightArenaBase );
		domain.shadowCasters[ domain.shadowCasterCount++ ] = caster;
		light.shadowCasterCount[ domainChain ]++;
		light.shadowCasterTotal++;
		if ( caster.disposition == CLASSIC_INTERACTION_SHADOW_CASTER_DRAW ) {
			light.drawableShadowCasters++;
		} else {
			light.noopShadowCasters++;
		}
		packetCursor++;
	}
	return true;
}

static int ShadowChainDrawableCount(
		const classicInteractionDomainLight_t &light,
		classicInteractionDomainShadowChain_t chain ) {
	int count = 0;
	for ( int i = 0; i < light.shadowCasterCount[ chain ]; ++i ) {
		const classicInteractionDomainShadowCaster_t &caster =
			domain.shadowCasters[ light.firstShadowCaster[ chain ] + i ];
		if ( caster.disposition == CLASSIC_INTERACTION_SHADOW_CASTER_DRAW ) {
			count++;
		}
	}
	return count;
}

static int ShadowMapReceiverMask(
		classicInteractionDomainReceiver_t receiver ) {
	return receiver == CLASSIC_INTERACTION_RECEIVER_LOCAL
		? SHADOWMAP_RECEIVER_MASK_LOCAL : SHADOWMAP_RECEIVER_MASK_GLOBAL;
}

// The tile the front end gave the light for this view
// (R_ShadowMapAssignAtlasTiles), else r_shadowMapSize: the same edge both
// back ends read, so the sealed state matches what they allocate.
static int ShadowMapProjectedTileSize( const viewLight_t *viewLight,
		const shadowMapLightClassification_t &classification ) {
	const int atlasDiv = Max( 1, classification.atlasDiv );
	const int maxTextureSize = glConfig.maxTextureSize > 0
		? glConfig.maxTextureSize : 4096;
	const int atlasSize = idMath::ClampInt( 2048, 16384,
		r_shadowMapAtlasSize.GetInteger() );
	return R_ShadowMapProjectedTileSize( viewLight, atlasDiv,
		Min( maxTextureSize, atlasSize ) );
}

static std::uint64_t MakeShadowMapResourcePlanId( int passIndex ) {
	return domain.generation != 0 && passIndex >= 0
		? ( static_cast<std::uint64_t>( domain.generation ) << 32 )
			| static_cast<std::uint64_t>( passIndex + 1 )
		: 0;
}

static bool BuildShadowMapPasses(
		classicInteractionDomainView_t &view,
		classicInteractionDomainLight_t &light,
		classicInteractionBuildError_t &error ) {
	const viewLight_t *viewLight = light.legacyViewLight;
	if ( viewLight == NULL || !r_useShadowMap.GetBool()
			|| !r_shadows.GetBool() || light.ambientLight
			|| !viewLight->lightShader->LightCastsShadows() ) {
		return true;
	}

	const shadowMapLightClassification_t classification =
		R_ClassifyShadowMapLight( viewLight );
	const bool classSupported = !classification.pointLight
		|| r_shadowMapPointLights.GetBool();
	classicInteractionDomainShadowProjectedState_t projected;
	std::memset( &projected, 0, sizeof( projected ) );
	R_ShadowMapResetProjectedLightState( projected.state );
	classicInteractionDomainShadowPointState_t point;
	std::memset( &point, 0, sizeof( point ) );

	if ( classSupported && classification.pointLight ) {
		point.valid = true;
		point.faceCount = 6;
		const int maxTextureSize = glConfig.maxTextureSize > 0
			? glConfig.maxTextureSize : 2048;
		point.faceSize = idMath::ClampInt( 128,
			Min( maxTextureSize, 2048 ),
			r_shadowMapPointSize.GetInteger() );
		point.lightOrigin[0] = viewLight->globalLightOrigin[0];
		point.lightOrigin[1] = viewLight->globalLightOrigin[1];
		point.lightOrigin[2] = viewLight->globalLightOrigin[2];
		point.lightOrigin[3] = 1.0f;
		point.farDistance = R_ShadowMapPointFarDistance( viewLight );
		const shadowMapPointReceiverSettings_t receiverSettings =
			R_ShadowMapPointReceiverSettings(
				point.farDistance, point.faceSize );
		point.constantBias = receiverSettings.constantBias;
		point.normalBias = receiverSettings.normalBias;
		point.normalOffsetScale = receiverSettings.normalOffsetScale;
		point.texelBiasScale = receiverSettings.texelBiasScale;
		point.filterRadius =
			Max( 0.0f, r_shadowMapPointFilterRadius.GetFloat() );
		point.filterTaps = idMath::ClampInt( 1, 13,
			r_shadowMapPointFilterTaps.GetInteger() );
		point.filterMode = idMath::ClampInt( 0, 1,
			r_shadowMapPointFilterMode.GetInteger() );
		point.depthCompare = r_shadowMapPointDepthCompare.GetBool();
		point.highPrecision = r_shadowMapPointHighPrecision.GetBool();
	} else if ( classSupported ) {
		const int tileSize = ShadowMapProjectedTileSize( viewLight, classification );
		if ( tileSize <= 0 ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
				tileSize, view.shadowMapPassPacketIndex, -1,
				light.sourceOrdinal );
			return false;
		}
		R_BuildShadowMapProjectedLightState( viewLight, view.viewDef,
			tileSize, projected.state );
		if ( !projected.state.valid
				|| projected.state.cascadeCount < 1
				|| projected.state.cascadeCount
					> SHADOWMAP_PROJECTED_MAX_CASCADES
				|| projected.state.atlasDiv < 1
				|| projected.state.atlasDiv > 2 ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
				projected.state.fallbackReason,
				view.shadowMapPassPacketIndex, -1,
				light.sourceOrdinal );
			return false;
		}
		projected.filter = R_ShadowMapProjectedFilterSettings( viewLight );
		projected.constantBias = r_shadowMapBias.GetFloat();
		projected.normalBias = r_shadowMapNormalBias.GetFloat();
		projected.normalOffsetScale =
			Max( 0.0f, r_shadowMapNormalOffsetScale.GetFloat() );
		projected.cascadeBlend =
			Max( 0.0f, r_shadowMapCascadeBlend.GetFloat() );
		projected.texelBiasScale =
			Max( 0.0f, r_shadowMapTexelBiasScale.GetFloat() );
		projected.depthCompare = r_shadowMapDepthCompare.GetBool();
		projected.receiverPlaneBias =
			r_shadowMapReceiverPlaneBias.GetBool();
	}
	if ( classification.pointLight ) {
		const float pointValues[] = {
			point.lightOrigin[0], point.lightOrigin[1], point.lightOrigin[2],
			point.farDistance, point.constantBias, point.normalBias,
			point.normalOffsetScale, point.texelBiasScale,
			point.filterRadius
		};
		if ( !FloatsAreFinite( pointValues,
				static_cast<int>( sizeof( pointValues ) / sizeof( pointValues[0] ) ) ) ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
				21, view.shadowMapPassPacketIndex, -1,
				light.sourceOrdinal );
			return false;
		}
	} else if ( classSupported ) {
		const float projectedValues[] = {
			projected.filter.filterScale,
			projected.filter.filterRadius,
			projected.filter.pcssLightRadius,
			projected.filter.pcssMaxRadius,
			projected.filter.effectiveFilterRadius,
			projected.constantBias, projected.normalBias,
			projected.normalOffsetScale, projected.cascadeBlend,
			projected.texelBiasScale
		};
		if ( !FloatsAreFinite( projectedValues,
				static_cast<int>( sizeof( projectedValues )
					/ sizeof( projectedValues[0] ) ) ) ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
				22, view.shadowMapPassPacketIndex, -1,
				light.sourceOrdinal );
			return false;
		}
	}

	const int globalStaticMapCount = light.shadowCasterCount[
		CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC ];
	const int localStaticMapCount = light.shadowCasterCount[
		CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC ];
	const int globalDynamicMapCount = light.shadowCasterCount[
		CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC ];
	const int localDynamicMapCount = light.shadowCasterCount[
		CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ];
	const int globalMapCount = globalStaticMapCount
		+ globalDynamicMapCount;
	const int localMapCount = localStaticMapCount
		+ localDynamicMapCount;
	const int globalSupplementCount =
		light.shadowCasterCount[
			CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL ];
	const int localSupplementCount =
		light.shadowCasterCount[
			CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL ];
	const int globalStencilCount =
		light.shadowCasterCount[
			CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL ];
	const int localStencilCount =
		light.shadowCasterCount[
			CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL ];

	for ( int receiverIndex = CLASSIC_INTERACTION_RECEIVER_LOCAL;
			receiverIndex <= CLASSIC_INTERACTION_RECEIVER_GLOBAL;
			++receiverIndex ) {
		const classicInteractionDomainReceiver_t receiver =
			static_cast<classicInteractionDomainReceiver_t>( receiverIndex );
		const bool receiverNeeded = receiver
			== CLASSIC_INTERACTION_RECEIVER_LOCAL
			? viewLight->localInteractions != NULL
			: viewLight->globalInteractions != NULL
				|| ( r_shadowMapTranslucentReceivers.GetBool()
					&& viewLight->translucentInteractions != NULL );
		if ( !receiverNeeded ) {
			continue;
		}
		const int receiverMask = ShadowMapReceiverMask( receiver );
		const int mappedCasterCount = globalMapCount
			+ ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				? localMapCount : 0 );
		const int supplementCount = globalSupplementCount
			+ ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				? localSupplementCount : 0 );
		const int fullStencilCount = globalStencilCount
			+ ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				? localStencilCount : 0 );
		const int incompleteMapMask =
			viewLight->shadowMapIncompleteMapMask
			| viewLight->shadowMapPrelightMapMissingMask;
		const int incompleteStencilMask =
			viewLight->shadowMapIncompleteStencilMask
			| ( viewLight->shadowMapPrelightStencilRequiredMask
				& ~viewLight->shadowMapPrelightStencilReadyMask );
		const int hybridIncompleteMask =
			viewLight->shadowMapHybridIncompleteMask
			| viewLight->shadowMapPrelightMapMissingMask;
		const bool mapComplete =
			( incompleteMapMask & receiverMask ) == 0;
		const bool stencilComplete = fullStencilCount > 0
			&& ( incompleteStencilMask & receiverMask ) == 0;
		const bool hybridComplete = !mapComplete
			&& supplementCount > 0
			&& ( hybridIncompleteMask & receiverMask ) == 0;

		if ( mappedCasterCount <= 0 || !classSupported
				|| ( !mapComplete && !hybridComplete ) ) {
			if ( !stencilComplete && mappedCasterCount > 0 ) {
				SetError( error, CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
					receiverMask, view.shadowMapPassPacketIndex, -1,
					light.sourceOrdinal, receiverIndex );
				return false;
			}
			continue;
		}
		if ( domain.shadowMapPassCount
				>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW,
				domain.shadowMapPassCount,
				view.shadowMapPassPacketIndex, -1,
				light.sourceOrdinal, receiverIndex );
			return false;
		}

		const int passIndex = domain.shadowMapPassCount++;
		classicInteractionDomainShadowMapPass_t &pass =
			domain.shadowMapPasses[ passIndex ];
		InitShadowMapPass( pass );
		pass.legacyViewLight = viewLight;
		pass.lightIndex = static_cast<int>( &light - domain.lights );
		pass.receiver = receiver;
		pass.resourceOwner = receiver;
		pass.disposition = hybridComplete
			? CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID
			: CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED;
		pass.mode = hybridComplete
			? CLASSIC_INTERACTION_SHADOW_HYBRID
			: ( classification.pointLight
				? CLASSIC_INTERACTION_SHADOW_POINT
				: CLASSIC_INTERACTION_SHADOW_PROJECTED );
		pass.lightClass = classification.lightClass;
		pass.receiverMask = receiverMask;
		pass.mappedCasterCount = mappedCasterCount;
		pass.supplementCasterCount = supplementCount;
		pass.casterSignature = viewLight->shadowMapCasterSignature;
		pass.incompleteMapMask = viewLight->shadowMapIncompleteMapMask;
		pass.incompleteStencilMask =
			viewLight->shadowMapIncompleteStencilMask;
		pass.hybridIncompleteMask =
			viewLight->shadowMapHybridIncompleteMask;
		pass.prelightMapMissingMask =
			viewLight->shadowMapPrelightMapMissingMask;
		pass.prelightStencilRequiredMask =
			viewLight->shadowMapPrelightStencilRequiredMask;
		pass.prelightStencilReadyMask =
			viewLight->shadowMapPrelightStencilReadyMask;
		pass.resourcePlanId = MakeShadowMapResourcePlanId( passIndex );
		pass.resourceGeneration = domain.generation;
		// Publication itself selects mapped ownership for this receiver. A
		// complete retained volume is rollback capability, not permission for a
		// backend to silently substitute stencil under the same settings.
		pass.mapRequired = true;
		pass.mapComplete = mapComplete;
		pass.stencilComplete = stencilComplete;
		pass.hybridComplete = mapComplete || hybridComplete;
		pass.hasStaticCasters = globalStaticMapCount
			+ ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				? localStaticMapCount : 0 ) > 0;
		pass.hasDynamicCasters = globalDynamicMapCount
			+ ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				? localDynamicMapCount : 0 ) > 0;
		pass.hasAlphaCasters = false;
		for ( int chainIndex =
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC;
				chainIndex
					<= CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC;
				++chainIndex ) {
			const classicInteractionDomainShadowChain_t chain =
				static_cast<classicInteractionDomainShadowChain_t>(
					chainIndex );
			if ( receiver == CLASSIC_INTERACTION_RECEIVER_LOCAL
					&& ( chain
						== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC
						|| chain
							== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ) ) {
				continue;
			}
			pass.drawableMappedCasters +=
				ShadowChainDrawableCount( light, chain );
			pass.noopMappedCasters +=
				light.shadowCasterCount[ chain ]
				- ShadowChainDrawableCount( light, chain );
			for ( int casterIndex = 0;
					casterIndex < light.shadowCasterCount[ chain ];
					++casterIndex ) {
				if ( domain.shadowCasters[
						light.firstShadowCaster[ chain ]
							+ casterIndex ].alphaStageCount > 0 ) {
					pass.hasAlphaCasters = true;
				}
			}
		}
		const classicInteractionDomainShadowChain_t supplementChains[2] = {
			CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL,
			CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL
		};
		for ( int supplementIndex = 0;
				supplementIndex < ( receiver
					== CLASSIC_INTERACTION_RECEIVER_GLOBAL ? 2 : 1 );
				++supplementIndex ) {
			const classicInteractionDomainShadowChain_t chain =
				supplementChains[ supplementIndex ];
			pass.drawableSupplementCasters +=
				ShadowChainDrawableCount( light, chain );
			pass.noopSupplementCasters +=
				light.shadowCasterCount[ chain ]
				- ShadowChainDrawableCount( light, chain );
		}
		pass.allowCacheReuse = r_shadowMapStaticCache.GetBool()
			&& !pass.hasDynamicCasters && !pass.hasAlphaCasters
			&& ( classification.cascadeCount == 1
				|| r_shadowMapCacheCSM.GetBool() );
		pass.allowCacheUpdate = true;
		pass.allowScratch = true;
		pass.hashedAlpha = r_shadowMapHashedAlpha.GetBool();
		pass.stableAlphaHash = r_shadowMapStableAlphaHash.GetBool();
		pass.casterCullMode = idMath::ClampInt( 0, 2,
			r_shadowMapCasterCulling.GetInteger() );
		pass.polygonFactor = r_shadowMapPolygonFactor.GetFloat();
		pass.polygonOffset = r_shadowMapPolygonOffset.GetFloat();
		pass.projected = projected;
		pass.point = point;

		if ( receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL
				&& localMapCount == 0
				&& light.shadowMapPassIndex[
					CLASSIC_INTERACTION_RECEIVER_LOCAL ] >= 0 ) {
			pass.resourceAlias = true;
			pass.resourceOwner = CLASSIC_INTERACTION_RECEIVER_LOCAL;
		}
		pass.hash = HashShadowMapPass( pass );
		light.shadowMapPassIndex[ receiverIndex ] = passIndex;
		light.receiverShadowMode[ receiverIndex ] = pass.mode;
	}

	const int globalPassIndex = light.shadowMapPassIndex[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ];
	if ( r_shadowMapTranslucentReceivers.GetBool()
			&& viewLight->translucentInteractions != NULL
			&& globalPassIndex >= 0 ) {
		light.receiverShadowMode[
			CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ] =
			domain.shadowMapPasses[ globalPassIndex ].mode;
	}
	return true;
}

static void RecomputePlannedStencilWork(
		classicInteractionDomainLight_t &light ) {
	light.logicalVolumeDraws = 0;
	light.preloadVolumeDraws = 0;
	const auto volumeMode = []( classicInteractionDomainShadowMode_t mode ) {
		return mode == CLASSIC_INTERACTION_SHADOW_STENCIL
			|| mode == CLASSIC_INTERACTION_SHADOW_HYBRID;
	};
	const auto chainFor = []( classicInteractionDomainShadowMode_t mode,
			bool local ) {
		if ( mode == CLASSIC_INTERACTION_SHADOW_HYBRID ) {
			return local
				? CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL
				: CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL;
		}
		return local ? CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL
			: CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL;
	};
	const auto addChain = [&light](
			classicInteractionDomainShadowChain_t chain ) {
		for ( int casterIndex = 0;
				casterIndex < light.shadowCasterCount[ chain ];
				++casterIndex ) {
			const classicInteractionDomainShadowCaster_t &caster =
				domain.shadowCasters[ light.firstShadowCaster[ chain ]
					+ casterIndex ];
			if ( caster.disposition
					!= CLASSIC_INTERACTION_SHADOW_CASTER_DRAW ) {
				continue;
			}
			light.logicalVolumeDraws++;
			if ( caster.preload ) {
				light.preloadVolumeDraws++;
			}
		}
	};

	// Model the exact classic receiver order rather than counting each selected
	// chain only once. Switching between full stencil and hybrid supplements
	// clears the stencil state and can require a deliberate resubmission; both
	// backends reconcile their committed logical/preload submissions to this
	// backend-neutral plan.
	classicInteractionDomainShadowMode_t preparedMode =
		CLASSIC_INTERACTION_SHADOW_NONE;
	bool preparedIncludesLocal = false;
	const classicInteractionDomainShadowMode_t localMode =
		light.receiverShadowMode[ CLASSIC_INTERACTION_RECEIVER_LOCAL ];
	if ( volumeMode( localMode ) ) {
		addChain( chainFor( localMode, false ) );
		preparedMode = localMode;
		preparedIncludesLocal = false;
	}
	const classicInteractionDomainShadowMode_t globalMode =
		light.receiverShadowMode[ CLASSIC_INTERACTION_RECEIVER_GLOBAL ];
	if ( volumeMode( globalMode ) ) {
		if ( preparedMode != globalMode ) {
			addChain( chainFor( globalMode, false ) );
		}
		if ( preparedMode != globalMode || !preparedIncludesLocal ) {
			addChain( chainFor( globalMode, true ) );
		}
		preparedMode = globalMode;
		preparedIncludesLocal = true;
	}
	const classicInteractionDomainShadowMode_t translucentMode =
		light.receiverShadowMode[ CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ];
	if ( volumeMode( translucentMode ) ) {
		if ( preparedMode != translucentMode ) {
			addChain( chainFor( translucentMode, false ) );
			addChain( chainFor( translucentMode, true ) );
		} else if ( !preparedIncludesLocal ) {
			addChain( chainFor( translucentMode, true ) );
		}
	}
	// An admitted shadow chain can contain only empty/no-op records. The
	// classic path still clears before testing that receiver, otherwise stencil
	// values left by the previous light could darken an otherwise unshadowed
	// contribution. Keep clear ownership tied to the selected mode, not only to
	// drawable volume count.
	light.clearStencil = volumeMode( localMode ) || volumeMode( globalMode )
		|| volumeMode( translucentMode );
}

static bool ValidateDrawPacket( const idScenePacketFrame &packetFrame,
		const drawPacket_t &packet, int packetIndex, const viewDef_t *viewDef,
		const viewLight_t *viewLight, int lightOrdinal,
		sceneInteractionReceiverClass_t receiverClass, int receiverOrdinal,
		int sourceOrdinal, const drawSurf_t *drawSurf,
		classicInteractionBuildError_t &error ) {
	if ( packet.passCategory != RENDER_PASS_ARB2_INTERACTION
			|| packet.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| packet.viewDef != viewDef || packet.legacyDrawSurf != drawSurf
			|| packet.interactionLight != viewLight
			|| packet.interactionLightOrdinal != lightOrdinal
			|| packet.interactionReceiverClass != receiverClass
			|| packet.interactionReceiverOrdinal != receiverOrdinal
			|| packet.interactionSourceOrdinal != sourceOrdinal ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
			sourceOrdinal, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( packet.geometryRecordIndex < 0
			|| packet.geometryRecordIndex >= packetFrame.NumGeometryRecords()
			|| packet.geometryRecord == NULL
			|| packet.geometryRecord
				!= &packetFrame.GeometryRecord( packet.geometryRecordIndex )
			|| packet.geometryRecord->legacyGeometry != drawSurf->geo ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_GEOMETRY_RECORD,
			packet.geometryRecordIndex, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( packet.instanceRecordIndex < 0
			|| packet.instanceRecordIndex >= packetFrame.NumInstanceRecords()
			|| packet.instanceRecord == NULL
			|| packet.instanceRecord
				!= &packetFrame.InstanceRecord( packet.instanceRecordIndex )
			|| packet.instanceRecord->legacySpace != drawSurf->space ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_INSTANCE_RECORD,
			packet.instanceRecordIndex, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( packet.materialRecordIndex < 0
			|| packet.materialRecordIndex >= packetFrame.NumMaterialRecords()
			|| packet.materialRecord == NULL
			|| packet.materialRecord
				!= &packetFrame.MaterialRecord( packet.materialRecordIndex )
			|| packet.materialRecord->material != drawSurf->material ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_MATERIAL_RECORD,
			packet.materialRecordIndex, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	const geometryResourceRecord_t &geometry = *packet.geometryRecord;
	if ( !packet.hasGeometry || packet.vertexCount <= 0 || packet.indexCount <= 0
			|| geometry.vertexCount != packet.vertexCount
			|| geometry.indexCount != packet.indexCount
			|| geometry.legacyGeometry == NULL || !packet.hasAmbientCache
			|| geometry.legacyGeometry->ambientCache == NULL
			|| ( !packet.hasIndexCache && !geometry.hasClientIndexData ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_GEOMETRY_RECORD,
			packet.indexCount, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	bool admittedMaterialDeform = false;
	if ( MaterialRequestsClassicDeform( drawSurf )
			&& r_rendererSharedDeform.GetBool() ) {
		classicDeformRole_t role;
		classicDeformOutcome_t outcome;
		std::uint64_t semanticHash = 0;
		if ( !ValidateDeformContract( drawSurf, &packet,
				CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER, role, outcome,
				semanticHash ) ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT,
				DeformContractFailureDetail( drawSurf, &packet ), -1, packetIndex,
				lightOrdinal, receiverOrdinal );
			return false;
		}
		admittedMaterialDeform = true;
	}
	if ( ( geometry.deformMode != GEOMETRY_DEFORM_NONE
				&& !admittedMaterialDeform )
			|| ( drawSurf->geo != NULL && drawSurf->geo->deformedSurface ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DEFORM,
			geometry.deformMode, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	// CPU-skinned surfaces already expose the authoritative deformed idDrawVert
	// stream in the sealed ambient cache.  GPU-palette surfaces require a
	// different vertex contract and remain an atomic whole-view boundary.
	if ( geometry.skinningMode != GEOMETRY_SKINNING_NONE
			&& geometry.skinningMode != GEOMETRY_SKINNING_CPU ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SKINNING,
			geometry.skinningMode, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( geometry.hasPrimBatchMesh ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SPECIAL_SURFACE,
			1, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	const instanceRecord_t &instance = *packet.instanceRecord;
	if ( !packet.hasShaderRegisters || !instance.hasShaderRegisters
			|| instance.legacyShaderRegisters == NULL
			|| instance.legacyShaderRegisters != drawSurf->shaderRegisters ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
			instance.shaderRegisterCount, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( instance.weaponDepthHack || instance.modelDepthHack != 0.0f ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DEPTH_HACK,
			instance.weaponDepthHack ? 1 : 2, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( instance.negativeScale || MatrixHasNegativeScale( instance.modelMatrix ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NEGATIVE_SCALE,
			1, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( !FloatsAreFinite( instance.modelMatrix, 16 )
			|| !FloatsAreFinite( instance.modelViewMatrix, 16 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			0, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	return true;
}

static bool DetermineLightScale( const viewDef_t &viewDef,
		classicInteractionDomainView_t &view,
		classicInteractionBuildError_t &error ) {
	const float authoredScale = r_lightScale.GetFloat();
	const float rendererMaximum = tr.backEndRendererMaxLight;
	if ( !FloatIsFinite( authoredScale ) || !FloatIsFinite( rendererMaximum )
			|| rendererMaximum <= 0.0f ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE, 1 );
		return false;
	}
	float maximum = 1.0f;
	int lightOrdinal = 0;
	for ( const viewLight_t *viewLight = viewDef.viewLights; viewLight != NULL;
			viewLight = viewLight->next, ++lightOrdinal ) {
		if ( !ViewLightContributesToClassicScale( viewLight ) ) {
			continue;
		}
		const idMaterial *shader = viewLight->lightShader;
		if ( shader == NULL ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
				0, -1, -1, lightOrdinal );
			return false;
		}
		const float *registers = viewLight->shaderRegisters;
		const int registerCount = shader->GetNumRegisters();
		if ( registers == NULL || registerCount <= 0 ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
				registerCount, -1, -1, lightOrdinal );
			return false;
		}
		for ( int stageIndex = 0; stageIndex < shader->GetNumStages();
				++stageIndex ) {
			const shaderStage_t *stage = shader->GetStage( stageIndex );
			if ( stage == NULL ) {
				SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
					stageIndex, -1, -1, lightOrdinal, -1, stageIndex );
				return false;
			}
			for ( int component = 0; component < 3; ++component ) {
				float value = 0.0f;
				if ( !ReadRegister( registers, registerCount,
						stage->color.registers[ component ], value, error,
						stageIndex ) ) {
					error.lightOrdinal = lightOrdinal;
					return false;
				}
				if ( !AccumulateClassicLightScaleValue( authoredScale, value,
						maximum ) ) {
					SetError( error,
						CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
						component, -1, -1, lightOrdinal, -1, stageIndex );
					return false;
				}
			}
		}
	}
	view.maxLightValue = maximum;
	if ( maximum <= rendererMaximum ) {
		view.lightScale = authoredScale;
		view.overBright = 1.0f;
	} else {
		view.lightScale = authoredScale * rendererMaximum / maximum;
		view.overBright = maximum / rendererMaximum;
	}
	if ( !FloatIsFinite( view.lightScale ) || !FloatIsFinite( view.overBright ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE, 2 );
		return false;
	}
	return true;
}

static bool ValidateLight( const viewLight_t &viewLight, int lightOrdinal,
		classicInteractionBuildError_t &error ) {
	const idMaterial *shader = viewLight.lightShader;
	if ( shader == NULL || shader->IsFogLight() || shader->IsBlendLight()
			|| !shader->IsDrawn() || shader->GetNumStages() <= 0 ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
			0, -1, -1, lightOrdinal );
		return false;
	}
	if ( viewLight.shaderRegisters == NULL || shader->GetNumRegisters() <= 0 ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
			shader->GetNumRegisters(), -1, -1, lightOrdinal );
		return false;
	}
	if ( !FloatsAreFinite( viewLight.globalLightOrigin.ToFloatPtr(), 3 )
			|| !FloatsAreFinite( viewLight.lightRadius.ToFloatPtr(), 3 )
			|| !FloatsAreFinite( viewLight.lightProject[ 0 ].ToFloatPtr(), 16 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			0, -1, -1, lightOrdinal );
		return false;
	}
	if ( !ImageReady( viewLight.falloffImage, error, -1 ) ) {
		error.lightOrdinal = lightOrdinal;
		return false;
	}
	for ( int stageIndex = 0; stageIndex < shader->GetNumStages();
			++stageIndex ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( stage == NULL || !TextureStageFixed( *stage, error, stageIndex ) ) {
			error.lightOrdinal = lightOrdinal;
			return false;
		}
	}
	return true;
}

static bool ValidateSurfaceMaterial( const drawPacket_t &packet,
		const drawSurf_t &drawSurf, int lightOrdinal, int receiverOrdinal,
		classicInteractionDomainView_t &view,
		const materialResourceTableRecord_t *&tableRecord,
		classicInteractionBuildError_t &error ) {
	const idMaterial *material = drawSurf.material;
	if ( material == NULL || !material->IsDrawn()
			|| !material->ReceivesLighting() || material->IsPortalSky()
			|| material->SuppressInSubview() || material->HasGui()
			|| material->HasSubview() || material->GetSort() >= SS_POST_PROCESS ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
			0, -1, -1, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( ( material->Deform() != DFRM_NONE
			&& !r_rendererSharedDeform.GetBool() )
			|| ( drawSurf.geo != NULL && drawSurf.geo->deformedSurface ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_DEFORM,
			material->Deform(), -1, -1, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( ( drawSurf.dsFlags & ( DSF_BSE_EFFECT | DSF_OUTLINE_ONLY ) ) != 0
			|| drawSurf.dynamicTexCoords != NULL
			|| drawSurf.texGenTransformAndViewOrg != NULL
			|| drawSurf.decalColorCache != NULL
			|| drawSurf.decalColorStageCount != 0 ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SPECIAL_SURFACE,
			drawSurf.dsFlags, -1, -1, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( RB_FlatDiffuseSurfaceActive( &drawSurf ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_FLAT_DIFFUSE,
			0, -1, -1, lightOrdinal, receiverOrdinal );
		return false;
	}
	tableRecord = R_MaterialResourceTable_RecordForIndex(
		packet.materialRecordIndex );
	if ( tableRecord == NULL || tableRecord->material != material
			|| tableRecord->sourceMaterialRecordIndex
				!= packet.materialRecordIndex ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MISSING_MATERIAL_RECORD,
			packet.materialRecordIndex, -1, -1, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( tableRecord->tableGeneration == 0
			|| ( view.tableGeneration != 0
				&& view.tableGeneration != tableRecord->tableGeneration ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_STALE_MATERIAL_RECORD,
			tableRecord->tableGeneration, -1, -1, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( packet.instanceRecord == NULL
			|| packet.instanceRecord->shaderRegisterCount
				!= tableRecord->registerCount
			|| tableRecord->registerCount != material->GetNumRegisters() ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS,
			tableRecord->registerCount, -1, -1, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( tableRecord->hasPBR || tableRecord->hasCustomProgram
			|| tableRecord->hasCustomGLSL ) {
		SetError( error, tableRecord->hasPBR
			? CLASSIC_INTERACTION_FAILURE_ENHANCED_MATERIAL
			: CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING,
			packet.materialRecordIndex, -1, -1, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	const float polygonFactor = r_offsetFactor.GetFloat();
	const float polygonUnits = r_offsetUnits.GetFloat()
		* material->GetPolygonOffset();
	if ( !FloatIsFinite( polygonFactor ) || !FloatIsFinite( polygonUnits ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			0, -1, -1, lightOrdinal, receiverOrdinal );
		return false;
	}
	for ( int stageIndex = 0; stageIndex < material->GetNumStages();
			++stageIndex ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( stage == NULL ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL,
				stageIndex, -1, -1, lightOrdinal, receiverOrdinal,
				stageIndex );
			return false;
		}
		if ( stage->newStage != NULL ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING,
				stageIndex, -1, -1, lightOrdinal, receiverOrdinal,
				stageIndex );
			return false;
		}
		if ( stage->lighting != SL_AMBIENT
				&& !TextureStageFixed( *stage, error, stageIndex ) ) {
			error.lightOrdinal = lightOrdinal;
			error.receiverOrdinal = receiverOrdinal;
			return false;
		}
	}
	view.tableGeneration = tableRecord->tableGeneration;
	return true;
}

static bool BuildBasePrimitive( classicInteractionDomainPrimitive_t &base,
		const classicInteractionDomainView_t &view,
		const classicInteractionDomainLight_t &light,
		const classicInteractionDomainSurface_t &surface,
		const drawPacket_t &packet, const shaderStage_t &lightStage,
		int lightStageIndex, std::uint64_t lightImageResourceId,
		std::uint64_t falloffImageResourceId,
		const float lightTextureMatrix[ 2 ][ 4 ],
		classicInteractionBuildError_t &error ) {
	InitPrimitive( base );
	const drawSurf_t *drawSurf = surface.legacyDrawSurf;
	const viewLight_t *viewLight = light.legacyViewLight;
	if ( drawSurf == NULL || drawSurf->space == NULL || viewLight == NULL
			|| view.viewDef == NULL || packet.instanceRecord == NULL ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_PACKET,
			0, -1, surface.drawPacketIndex, light.sourceOrdinal,
			surface.receiverOrdinal, lightStageIndex );
		return false;
	}
	base.legacyDrawSurf = drawSurf;
	base.legacyViewLight = viewLight;
	base.lightIndex = surface.lightIndex;
	base.surfaceIndex = static_cast<int>( &surface - domain.surfaces );
	base.lightStageIndex = lightStageIndex;
	base.receiver = surface.receiver;
	base.depth = surface.receiver == CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT
		? CLASSIC_INTERACTION_DEPTH_LESS_OR_EQUAL
		: CLASSIC_INTERACTION_DEPTH_EQUAL;
	base.cull = ConvertCull( drawSurf->material->GetCullType(), error );
	if ( error.failure != CLASSIC_INTERACTION_FAILURE_NONE ) {
		return false;
	}
	base.vertexCount = packet.vertexCount;
	base.firstIndex = packet.firstIndex;
	base.indexCount = packet.indexCount;
	base.vertexOffset = packet.vertexOffset;
	base.scissorX1 = packet.scissorX1;
	base.scissorY1 = packet.scissorY1;
	base.scissorX2 = packet.scissorX2;
	base.scissorY2 = packet.scissorY2;
	base.ambientLight = viewLight->lightShader->IsAmbientLight();
	base.polygonOffsetEnabled =
		drawSurf->material->TestMaterialFlag( MF_POLYGONOFFSET );
	base.polygonOffsetFactor = r_offsetFactor.GetFloat();
	base.polygonOffsetUnits = r_offsetUnits.GetFloat()
		* drawSurf->material->GetPolygonOffset();
	base.lightImageResourceId = lightImageResourceId;
	base.lightFalloffImageResourceId = falloffImageResourceId;
	std::memcpy( base.modelMatrix, packet.instanceRecord->modelMatrix,
		sizeof( base.modelMatrix ) );
	std::memcpy( base.modelViewMatrix, packet.instanceRecord->modelViewMatrix,
		sizeof( base.modelViewMatrix ) );

	idVec3 localPoint;
	R_GlobalPointToLocal( drawSurf->space->modelMatrix,
		viewLight->globalLightOrigin, localPoint );
	base.localLightOrigin[ 0 ] = localPoint[ 0 ];
	base.localLightOrigin[ 1 ] = localPoint[ 1 ];
	base.localLightOrigin[ 2 ] = localPoint[ 2 ];
	base.localLightOrigin[ 3 ] = 0.0f;
	R_GlobalPointToLocal( drawSurf->space->modelMatrix,
		view.viewDef->renderView.vieworg, localPoint );
	base.localViewOrigin[ 0 ] = localPoint[ 0 ];
	base.localViewOrigin[ 1 ] = localPoint[ 1 ];
	base.localViewOrigin[ 2 ] = localPoint[ 2 ];
	base.localViewOrigin[ 3 ] = 1.0f;
	for ( int planeIndex = 0; planeIndex < 4; ++planeIndex ) {
		idPlane localPlane;
		R_GlobalPlaneToLocal( drawSurf->space->modelMatrix,
			viewLight->lightProject[ planeIndex ], localPlane );
		std::memcpy( base.lightProjection[ planeIndex ], localPlane.ToFloatPtr(),
			sizeof( base.lightProjection[ planeIndex ] ) );
	}
	if ( lightStage.texture.hasMatrix ) {
		BakeTextureMatrixIntoProjection( base.lightProjection,
			lightTextureMatrix );
	}
	if ( !FloatsAreFinite( base.localLightOrigin, 4 )
			|| !FloatsAreFinite( base.localViewOrigin, 4 )
			|| !FloatsAreFinite( &base.lightProjection[ 0 ][ 0 ], 16 )
			|| !FloatsAreFinite( base.modelViewMatrix, 16 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			0, -1, surface.drawPacketIndex, light.sourceOrdinal,
			surface.receiverOrdinal, lightStageIndex );
		return false;
	}
	return true;
}

static bool PositiveRGB( const float color[ 4 ] ) {
	return color[ 0 ] > 0.0f || color[ 1 ] > 0.0f || color[ 2 ] > 0.0f;
}

static bool SubmitPrimitive( const classicInteractionDomainPrimitive_t &base,
		const classicInteractionWork_t &work,
		classicInteractionDomainSurface_t &surface,
		int lightArenaBase, int surfaceArenaBase,
		classicInteractionBuildError_t &error ) {
	if ( domain.primitiveCount >= CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES ) {
		const int lightOrdinal = base.lightIndex >= 0
				&& base.lightIndex < domain.lightCount
			? domain.lights[ base.lightIndex ].sourceOrdinal : -1;
		SetError( error, CLASSIC_INTERACTION_FAILURE_PRIMITIVE_POOL_OVERFLOW,
			domain.primitiveCount, -1, surface.drawPacketIndex,
			lightOrdinal,
			surface.receiverOrdinal, base.lightStageIndex );
		return false;
	}
	classicInteractionDomainPrimitive_t primitive = base;
	primitive.bumpStageIndex = work.bumpStageIndex;
	primitive.diffuseStageIndex = work.diffuseStageIndex;
	primitive.specularStageIndex = work.specularStageIndex;
	primitive.vertexColor = work.vertexColor;
	std::memcpy( primitive.diffuseColor, work.diffuseColor,
		sizeof( primitive.diffuseColor ) );
	std::memcpy( primitive.specularColor, work.specularColor,
		sizeof( primitive.specularColor ) );
	std::memcpy( primitive.bumpMatrix, work.bumpMatrix,
		sizeof( primitive.bumpMatrix ) );
	std::memcpy( primitive.diffuseMatrix, work.diffuseMatrix,
		sizeof( primitive.diffuseMatrix ) );
	std::memcpy( primitive.specularMatrix, work.specularMatrix,
		sizeof( primitive.specularMatrix ) );

	if ( work.bumpImage == NULL ) {
		primitive.disposition =
			CLASSIC_INTERACTION_PRIMITIVE_NOOP_MISSING_BUMP;
	} else {
		const idImage *bumpImage = r_skipBump.GetBool()
			? ( globalImages != NULL ? globalImages->flatNormalMap : NULL )
			: work.bumpImage;
		const idImage *diffuseImage = work.diffuseImage == NULL
			|| r_skipDiffuse.GetBool()
			? ( globalImages != NULL ? globalImages->blackImage : NULL )
			: work.diffuseImage;
		const idImage *specularImage = work.specularImage == NULL
			|| r_skipSpecular.GetBool() || primitive.ambientLight
			? ( globalImages != NULL ? globalImages->blackImage : NULL )
			: work.specularImage;
		if ( !AddTexture( bumpImage, primitive.bumpImageResourceId, error,
				work.bumpStageIndex )
				|| !AddTexture( diffuseImage, primitive.diffuseImageResourceId,
					error, work.diffuseStageIndex )
				|| !AddTexture( specularImage,
					primitive.specularImageResourceId, error,
					work.specularStageIndex ) ) {
			error.drawPacketIndex = surface.drawPacketIndex;
			error.receiverOrdinal = surface.receiverOrdinal;
			return false;
		}
		const classicInteractionDomainTexture_t *diffuseTexture =
			R_ClassicInteractionDomain_ResolveTexture(
				primitive.diffuseImageResourceId );
		const classicInteractionDomainTexture_t *specularTexture =
			R_ClassicInteractionDomain_ResolveTexture(
				primitive.specularImageResourceId );
		const bool diffuseBlack = diffuseTexture == NULL || globalImages == NULL
			|| diffuseTexture->image == globalImages->blackImage;
		const bool specularBlack = specularTexture == NULL || globalImages == NULL
			|| specularTexture->image == globalImages->blackImage;
		primitive.disposition =
			( PositiveRGB( primitive.diffuseColor ) && !diffuseBlack )
				|| ( PositiveRGB( primitive.specularColor ) && !specularBlack )
			? CLASSIC_INTERACTION_PRIMITIVE_DRAW
			: CLASSIC_INTERACTION_PRIMITIVE_NOOP_BLACK;
	}
	if ( !FloatsAreFinite( primitive.diffuseColor, 4 )
			|| !FloatsAreFinite( primitive.specularColor, 4 )
			|| !FloatsAreFinite( &primitive.bumpMatrix[ 0 ][ 0 ], 8 )
			|| !FloatsAreFinite( &primitive.diffuseMatrix[ 0 ][ 0 ], 8 )
			|| !FloatsAreFinite( &primitive.specularMatrix[ 0 ][ 0 ], 8 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
			0, -1, surface.drawPacketIndex, -1, surface.receiverOrdinal,
			base.lightStageIndex );
		return false;
	}
	primitive.hash = HashPrimitive( primitive, lightArenaBase,
		surfaceArenaBase );
	domain.primitives[ domain.primitiveCount++ ] = primitive;
	surface.primitiveCount++;
	if ( primitive.disposition == CLASSIC_INTERACTION_PRIMITIVE_DRAW ) {
		surface.drawablePrimitiveCount++;
	} else {
		surface.noopPrimitiveCount++;
	}
	return true;
}

static bool PopulateLightRecord( classicInteractionDomainLight_t &light,
		const viewLight_t &viewLight, int sourceOrdinal,
		classicInteractionBuildError_t &error ) {
	InitLight( light );
	light.legacyViewLight = &viewLight;
	light.sourceOrdinal = sourceOrdinal;
	light.firstSurface = domain.surfaceCount;
	light.firstPrimitive = domain.primitiveCount;
	light.scissorX1 = viewLight.scissorRect.x1;
	light.scissorY1 = viewLight.scissorRect.y1;
	light.scissorX2 = viewLight.scissorRect.x2;
	light.scissorY2 = viewLight.scissorRect.y2;
	light.globalLightOrigin[ 0 ] = viewLight.globalLightOrigin[ 0 ];
	light.globalLightOrigin[ 1 ] = viewLight.globalLightOrigin[ 1 ];
	light.globalLightOrigin[ 2 ] = viewLight.globalLightOrigin[ 2 ];
	light.globalLightOrigin[ 3 ] = 1.0f;
	light.lightRadius[ 0 ] = viewLight.lightRadius[ 0 ];
	light.lightRadius[ 1 ] = viewLight.lightRadius[ 1 ];
	light.lightRadius[ 2 ] = viewLight.lightRadius[ 2 ];
	light.lightRadius[ 3 ] = 0.0f;
	for ( int plane = 0; plane < 4; ++plane ) {
		std::memcpy( light.lightProject[ plane ],
			viewLight.lightProject[ plane ].ToFloatPtr(),
			sizeof( light.lightProject[ plane ] ) );
	}
	light.pointLight = viewLight.pointLight;
	light.parallel = viewLight.parallel;
	light.ambientLight = viewLight.lightShader->IsAmbientLight();
	light.shadowClassified = true;
	light.lightStageCount = viewLight.lightShader->GetNumStages();
	for ( int stageIndex = 0; stageIndex < light.lightStageCount; ++stageIndex ) {
		bool active = false;
		if ( !EvaluateCondition( *viewLight.lightShader->GetStage( stageIndex ),
				viewLight.shaderRegisters,
				viewLight.lightShader->GetNumRegisters(), active, error,
				stageIndex ) ) {
			error.lightOrdinal = sourceOrdinal;
			return false;
		}
		if ( active ) {
			light.activeLightStageCount++;
		} else {
			light.inactiveLightStageCount++;
		}
	}
	return true;
}

static bool PrepareSurfacePrimitives(
		const classicInteractionDomainView_t &view,
		const classicInteractionDomainLight_t &light,
		classicInteractionDomainSurface_t &surface,
		const drawPacket_t &packet,
		int lightArenaBase, int surfaceArenaBase,
		classicInteractionBuildError_t &error ) {
	const drawSurf_t *drawSurf = surface.legacyDrawSurf;
	const viewLight_t *viewLight = light.legacyViewLight;
	const idMaterial *surfaceShader = drawSurf->material;
	const idMaterial *lightShader = viewLight->lightShader;
	const float *surfaceRegisters = drawSurf->shaderRegisters;
	const float *lightRegisters = viewLight->shaderRegisters;
	const int surfaceRegisterCount = surfaceShader->GetNumRegisters();
	const int lightRegisterCount = lightShader->GetNumRegisters();
	surface.firstPrimitive = domain.primitiveCount;

	for ( int lightStageIndex = 0;
			lightStageIndex < lightShader->GetNumStages(); ++lightStageIndex ) {
		const shaderStage_t &lightStage =
			*lightShader->GetStage( lightStageIndex );
		bool lightActive = false;
		if ( !EvaluateCondition( lightStage, lightRegisters,
				lightRegisterCount, lightActive, error, lightStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			error.receiverOrdinal = surface.receiverOrdinal;
			return false;
		}
		if ( !lightActive ) {
			continue;
		}

		std::uint64_t lightImageResourceId = 0;
		std::uint64_t falloffImageResourceId = 0;
		if ( !AddTexture( lightStage.texture.image, lightImageResourceId,
				error, lightStageIndex )
				|| !AddTexture( viewLight->falloffImage,
					falloffImageResourceId, error, lightStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			error.receiverOrdinal = surface.receiverOrdinal;
			return false;
		}
		float lightTextureMatrix[ 2 ][ 4 ];
		if ( !EvaluateTextureMatrix( lightStage.texture, lightRegisters,
				lightRegisterCount, lightTextureMatrix, error,
				lightStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			error.receiverOrdinal = surface.receiverOrdinal;
			return false;
		}
		float lightColor[ 4 ];
		if ( !EvaluateStageColor( lightStage, lightRegisters,
				lightRegisterCount, lightColor, false, error,
				lightStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			error.receiverOrdinal = surface.receiverOrdinal;
			return false;
		}
		lightColor[ 0 ] *= view.lightScale;
		lightColor[ 1 ] *= view.lightScale;
		lightColor[ 2 ] *= view.lightScale;
		if ( !FloatsAreFinite( lightColor, 4 ) ) {
			SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE,
				0, -1, surface.drawPacketIndex, light.sourceOrdinal,
				surface.receiverOrdinal, lightStageIndex );
			return false;
		}

		classicInteractionDomainPrimitive_t base;
		if ( !BuildBasePrimitive( base, view, light, surface, packet,
				lightStage, lightStageIndex, lightImageResourceId,
				falloffImageResourceId, lightTextureMatrix, error ) ) {
			return false;
		}
		classicInteractionWork_t work;
		InitWork( work );
		for ( int surfaceStageIndex = 0;
				surfaceStageIndex < surfaceShader->GetNumStages();
				++surfaceStageIndex ) {
			const shaderStage_t &surfaceStage =
				*surfaceShader->GetStage( surfaceStageIndex );
			if ( surfaceStage.lighting == SL_AMBIENT ) {
				continue;
			}
			surface.surfaceStageCount++;
			bool surfaceActive = false;
			if ( !EvaluateCondition( surfaceStage, surfaceRegisters,
					surfaceRegisterCount, surfaceActive, error,
					surfaceStageIndex ) ) {
				error.lightOrdinal = light.sourceOrdinal;
				error.receiverOrdinal = surface.receiverOrdinal;
				return false;
			}
			if ( !surfaceActive ) {
				surface.inactiveSurfaceStageCount++;
				continue;
			}
			surface.activeSurfaceStageCount++;
			switch ( surfaceStage.lighting ) {
			case SL_BUMP:
				if ( !SubmitPrimitive( base, work, surface, lightArenaBase,
						surfaceArenaBase, error ) ) {
					return false;
				}
				work.diffuseImage = NULL;
				work.specularImage = NULL;
				work.diffuseImageResourceId = 0;
				work.specularImageResourceId = 0;
				work.diffuseStageIndex = -1;
				work.specularStageIndex = -1;
				work.bumpImage = surfaceStage.texture.image;
				work.bumpStageIndex = surfaceStageIndex;
				if ( !EvaluateTextureMatrix( surfaceStage.texture,
						surfaceRegisters, surfaceRegisterCount, work.bumpMatrix,
						error, surfaceStageIndex ) ) {
					return false;
				}
				break;
			case SL_DIFFUSE: {
				if ( work.diffuseImage != NULL
						&& !SubmitPrimitive( base, work, surface,
							lightArenaBase, surfaceArenaBase, error ) ) {
					return false;
				}
				work.diffuseImage = surfaceStage.texture.image;
				work.diffuseStageIndex = surfaceStageIndex;
				if ( !EvaluateTextureMatrix( surfaceStage.texture,
						surfaceRegisters, surfaceRegisterCount,
						work.diffuseMatrix, error, surfaceStageIndex )
						|| !EvaluateStageColor( surfaceStage,
							surfaceRegisters, surfaceRegisterCount,
							work.diffuseColor, true, error,
							surfaceStageIndex ) ) {
					return false;
				}
				for ( int component = 0; component < 4; ++component ) {
					work.diffuseColor[ component ] *= lightColor[ component ];
				}
				work.vertexColor = ConvertVertexColor(
					surfaceStage.vertexColor, error, surfaceStageIndex );
				if ( error.failure != CLASSIC_INTERACTION_FAILURE_NONE ) {
					return false;
				}
				break;
			}
			case SL_SPECULAR: {
				if ( work.specularImage != NULL
						&& !SubmitPrimitive( base, work, surface,
							lightArenaBase, surfaceArenaBase, error ) ) {
					return false;
				}
				work.specularImage = surfaceStage.texture.image;
				work.specularStageIndex = surfaceStageIndex;
				if ( !EvaluateTextureMatrix( surfaceStage.texture,
						surfaceRegisters, surfaceRegisterCount,
						work.specularMatrix, error, surfaceStageIndex )
						|| !EvaluateStageColor( surfaceStage,
							surfaceRegisters, surfaceRegisterCount,
							work.specularColor, true, error,
							surfaceStageIndex ) ) {
					return false;
				}
				for ( int component = 0; component < 4; ++component ) {
					work.specularColor[ component ] *= lightColor[ component ];
				}
				work.vertexColor = ConvertVertexColor(
					surfaceStage.vertexColor, error, surfaceStageIndex );
				if ( error.failure != CLASSIC_INTERACTION_FAILURE_NONE ) {
					return false;
				}
				break;
			}
			case SL_AMBIENT:
			default:
				SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
					surfaceStage.lighting, -1, surface.drawPacketIndex,
					light.sourceOrdinal, surface.receiverOrdinal,
					surfaceStageIndex );
				return false;
			}
		}
		if ( !SubmitPrimitive( base, work, surface, lightArenaBase,
				surfaceArenaBase, error ) ) {
			return false;
		}
	}
	return true;
}

static bool SceneIsInteractionCandidate( const idScenePacketFrame &packetFrame,
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
	for ( int localPass = 0; localPass < scene.passPacketCount; ++localPass ) {
		if ( packetFrame.Pass( scene.firstPassPacket + localPass ).passCategory
				== RENDER_PASS_ARB2_INTERACTION ) {
			return true;
		}
	}
	return false;
}

static classicInteractionDomainView_t *FindMutableView(
		const viewDef_t *viewDef ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].viewDef == viewDef ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

static int ViewIndex( const classicInteractionDomainView_t *view ) {
	if ( view == NULL ) {
		return -1;
	}
	const ptrdiff_t index = view - domain.views;
	if ( index < 0 || index >= domain.viewCount
			|| view != &domain.views[ index ] ) {
		return -1;
	}
	return static_cast<int>( index );
}

static int LightIndex( const classicInteractionDomainLight_t *light ) {
	if ( light == NULL ) {
		return -1;
	}
	const ptrdiff_t index = light - domain.lights;
	if ( index < 0 || index >= domain.lightCount
			|| light != &domain.lights[ index ] ) {
		return -1;
	}
	return static_cast<int>( index );
}

static int SurfaceIndex( const classicInteractionDomainSurface_t *surface ) {
	if ( surface == NULL ) {
		return -1;
	}
	const ptrdiff_t index = surface - domain.surfaces;
	if ( index < 0 || index >= domain.surfaceCount
			|| surface != &domain.surfaces[ index ] ) {
		return -1;
	}
	return static_cast<int>( index );
}

static int ShadowCasterIndex(
		const classicInteractionDomainShadowCaster_t *caster ) {
	if ( caster == NULL ) {
		return -1;
	}
	const ptrdiff_t index = caster - domain.shadowCasters;
	if ( index < 0 || index >= domain.shadowCasterCount
			|| caster != &domain.shadowCasters[ index ] ) {
		return -1;
	}
	return static_cast<int>( index );
}

static bool R_ClassicInteractionDomain_PrepareView(
		const idScenePacketFrame &packetFrame, const scenePacket_t &scene,
		classicInteractionDomainView_t &view ) {
	const classicInteractionCheckpoint_t arenaCheckpoint = {
		domain.lightCount, domain.surfaceCount, domain.primitiveCount,
		domain.shadowCasterCount, domain.shadowAlphaStageCount,
		domain.shadowMapPassCount, domain.textureCount
	};
	classicInteractionBuildError_t error;
	InitError( error );
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	const materialResourceTableStats_t &tableStats =
		R_MaterialResourceTable_Stats();
	if ( packetStats.overflow ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SCENE_PACKET_OVERFLOW,
			packetStats.overflowCause );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( !tableStats.prepared ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_NOT_PREPARED );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( !tableStats.available ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNAVAILABLE );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( tableStats.overflow ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_OVERFLOW );
		return FailView( view, arenaCheckpoint, error );
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
			|| viewDef->numOutlineDrawSurfs != 0
			|| !FloatsAreFinite( viewDef->projectionMatrix, 16 ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_VIEW,
			viewDef != NULL ? viewDef->renderFlags : 0 );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( r_skipInteractions.GetBool() ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE, 1 );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( R_CelShadingAnyEnabled() ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_CEL_SHADING );
		return FailView( view, arenaCheckpoint, error );
	}
	// These reinterpret stock materials. r_pbrMaterials (default on) changes
	// only PBR-authored materials, and ValidateSurfaceMaterial rejects every
	// PBR receiver, so a view without one stays eligible.
	if ( r_enhancedMaterials.GetBool()
			|| r_pbrInferFromLegacyMaterials.GetBool() ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_ENHANCED_MATERIAL );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( r_useSimpleInteraction.GetBool() || r_testARBProgram.GetBool()
			|| r_appleARB2Interactions.GetInteger() != 0
			|| glConfig.disableARB2Interactions ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_ALTERNATE_INTERACTION_PROGRAM );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( !FloatIsFinite( r_offsetFactor.GetFloat() )
			|| !FloatIsFinite( r_offsetUnits.GetFloat() )
			|| !FloatIsFinite( r_shadowPolygonFactor.GetFloat() )
			|| !FloatIsFinite( r_shadowPolygonOffset.GetFloat() ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE, 3 );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( r_showShadows.GetInteger() != 0
			|| r_useExternalShadows.GetInteger() < 0
			|| r_useExternalShadows.GetInteger() > 2 ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE,
			r_showShadows.GetInteger() != 0
				? r_showShadows.GetInteger()
				: 100 + r_useExternalShadows.GetInteger() );
		return FailView( view, arenaCheckpoint, error );
	}
	view.useScissor = r_useScissor.GetBool();
	view.useShadowVertexProgram = r_useShadowVertexProgram.GetBool();
	view.preferTwoSidedStencil = r_useTwoSidedStencil.GetBool();
	view.useDepthBounds = r_useDepthBoundsTest.GetBool();
	view.stencilTranslucentShadows =
		r_stencilTranslucentShadows.GetBool();
	view.shadowPolygonFactor = r_shadowPolygonFactor.GetFloat();
	view.shadowPolygonUnits = -r_shadowPolygonOffset.GetFloat();
	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() )
			|| !RangeFits( scene.firstDrawPacket, scene.drawPacketCount,
				packetFrame.NumDrawPackets() ) ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_INVALID_SCENE_RANGE,
			scene.passPacketCount );
		return FailView( view, arenaCheckpoint, error );
	}

	const passPacket_t *interactionPass = NULL;
	const passPacket_t *stencilShadowPass = NULL;
	const passPacket_t *shadowMapPass = NULL;
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
			SetError( error, CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_RANGE,
				pass.drawPacketCount, passPacketIndex );
			return FailView( view, arenaCheckpoint, error );
		}
		if ( pass.passCategory == RENDER_PASS_ARB2_INTERACTION ) {
			if ( interactionPass != NULL || !pass.enabled || pass.commandOnly
					|| ( pass.packetCategory != SCENE_PACKET_CATEGORY_WORLD
						&& !( pass.drawPacketCount == 0
							&& pass.packetCategory
								== SCENE_PACKET_CATEGORY_COMMAND ) ) ) {
				SetError( error,
					CLASSIC_INTERACTION_FAILURE_INVALID_INTERACTION_PASS,
					pass.passCategory, passPacketIndex );
				return FailView( view, arenaCheckpoint, error );
			}
			interactionPass = &pass;
			view.interactionPassPacketIndex = passPacketIndex;
			continue;
		}
		if ( pass.passCategory == RENDER_PASS_STENCIL_SHADOW
				|| pass.passCategory == RENDER_PASS_SHADOW_MAP ) {
			const bool stencilPass =
				pass.passCategory == RENDER_PASS_STENCIL_SHADOW;
			const passPacket_t *&selected = stencilPass
				? stencilShadowPass : shadowMapPass;
			if ( selected != NULL || !pass.enabled || pass.commandOnly
					|| pass.packetCategory != SCENE_PACKET_CATEGORY_WORLD ) {
				SetError( error,
					stencilPass
						? CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH
						: CLASSIC_INTERACTION_FAILURE_SHADOW_MAP,
					pass.passCategory, passPacketIndex );
				return FailView( view, arenaCheckpoint, error );
			}
			selected = &pass;
			if ( stencilPass ) {
				view.stencilShadowPassPacketIndex = passPacketIndex;
			} else {
				view.shadowMapPassPacketIndex = passPacketIndex;
			}
		}
	}
	if ( interactionPass == NULL ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_INVALID_INTERACTION_PASS, -1 );
		return FailView( view, arenaCheckpoint, error );
	}
	view.packetDrawCount = interactionPass->drawPacketCount;
	view.shadowPacketDrawCount = stencilShadowPass != NULL
		? stencilShadowPass->drawPacketCount : 0;
	if ( shadowMapPass != NULL ) {
		view.shadowPacketDrawCount += shadowMapPass->drawPacketCount;
	}
	if ( !DetermineLightScale( *viewDef, view, error ) ) {
		return FailView( view, arenaCheckpoint, error );
	}

	int packetCursor = interactionPass->firstDrawPacket;
	const int packetEnd = interactionPass->firstDrawPacket
		+ interactionPass->drawPacketCount;
	int expectedSourceOrdinal = 0;
	int shadowPacketCursor = stencilShadowPass != NULL
		? stencilShadowPass->firstDrawPacket : -1;
	const int shadowPacketEnd = stencilShadowPass != NULL
		? stencilShadowPass->firstDrawPacket
			+ stencilShadowPass->drawPacketCount : -1;
	int mapPacketCursor = shadowMapPass != NULL
		? shadowMapPass->firstDrawPacket : -1;
	const int mapPacketEnd = shadowMapPass != NULL
		? shadowMapPass->firstDrawPacket
			+ shadowMapPass->drawPacketCount : -1;
	int sourceLightOrdinal = 0;
	for ( const viewLight_t *viewLight = viewDef->viewLights; viewLight != NULL;
			viewLight = viewLight->next, ++sourceLightOrdinal ) {
		if ( !ViewLightHasInteractions( viewLight ) ) {
			continue;
		}
		if ( domain.lightCount >= CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS ) {
			SetError( error,
				CLASSIC_INTERACTION_FAILURE_LIGHT_POOL_OVERFLOW,
				domain.lightCount, view.interactionPassPacketIndex, -1,
				sourceLightOrdinal );
			return FailView( view, arenaCheckpoint, error );
		}
		if ( !ValidateLight( *viewLight, sourceLightOrdinal, error ) ) {
			return FailView( view, arenaCheckpoint, error );
		}
		const int absoluteLightIndex = domain.lightCount++;
		classicInteractionDomainLight_t &light =
			domain.lights[ absoluteLightIndex ];
		if ( !PopulateLightRecord( light, *viewLight, sourceLightOrdinal,
				error ) ) {
			return FailView( view, arenaCheckpoint, error );
		}

		const bool stencilShadowing = r_shadows.GetBool()
			&& viewLight->lightShader->LightCastsShadows()
			&& !viewLight->lightShader->IsAmbientLight()
			&& ( viewLight->globalShadows != NULL
				|| viewLight->localShadows != NULL );
		if ( stencilShadowing ) {
			if ( stencilShadowPass == NULL ) {
				SetError( error,
					CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
					0, -1, -1, sourceLightOrdinal );
				return FailView( view, arenaCheckpoint, error );
			}
			if ( !PrepareStencilShadowChain( packetFrame, view, light,
					viewLight->globalShadows,
					SCENE_SHADOW_CASTER_STENCIL_GLOBAL,
					CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL,
					RENDER_PASS_STENCIL_SHADOW,
					view.stencilShadowPassPacketIndex,
					shadowPacketCursor, shadowPacketEnd,
					arenaCheckpoint.lightCount, error )
					|| !PrepareStencilShadowChain( packetFrame, view, light,
						viewLight->localShadows,
						SCENE_SHADOW_CASTER_STENCIL_LOCAL,
						CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL,
						RENDER_PASS_STENCIL_SHADOW,
						view.stencilShadowPassPacketIndex,
						shadowPacketCursor, shadowPacketEnd,
						arenaCheckpoint.lightCount, error ) ) {
				return FailView( view, arenaCheckpoint, error );
			}
			light.clearStencil = true;
			light.receiverShadowMode[
				CLASSIC_INTERACTION_RECEIVER_LOCAL ] =
				viewLight->globalShadows != NULL
					? CLASSIC_INTERACTION_SHADOW_STENCIL
					: CLASSIC_INTERACTION_SHADOW_NONE;
			light.receiverShadowMode[
				CLASSIC_INTERACTION_RECEIVER_GLOBAL ] =
				CLASSIC_INTERACTION_SHADOW_STENCIL;
			light.receiverShadowMode[
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ] =
				view.stencilTranslucentShadows
					? CLASSIC_INTERACTION_SHADOW_STENCIL
					: CLASSIC_INTERACTION_SHADOW_NONE;
			view.shadowMode = CLASSIC_INTERACTION_SHADOW_STENCIL;
		}

		const bool mappedShadowing = shadowMapPass != NULL
			&& r_useShadowMap.GetBool()
			&& r_shadows.GetBool()
			&& viewLight->lightShader->LightCastsShadows()
			&& !viewLight->lightShader->IsAmbientLight()
			&& ViewLightHasMappedShadowState( *viewLight );
		if ( mappedShadowing ) {
			if ( !PrepareMappedShadowChain( packetFrame, view, light,
					viewLight->globalShadowMapCasters,
					SCENE_SHADOW_CASTER_MAP_GLOBAL_STATIC,
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC,
					view.shadowMapPassPacketIndex, mapPacketCursor,
					mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareMappedShadowChain( packetFrame, view, light,
						viewLight->localShadowMapCasters,
						SCENE_SHADOW_CASTER_MAP_LOCAL_STATIC,
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareMappedShadowChain( packetFrame, view, light,
						viewLight->globalShadowMapDynamicCasters,
						SCENE_SHADOW_CASTER_MAP_GLOBAL_DYNAMIC,
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareMappedShadowChain( packetFrame, view, light,
						viewLight->localShadowMapDynamicCasters,
						SCENE_SHADOW_CASTER_MAP_LOCAL_DYNAMIC,
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareMappedShadowChain( packetFrame, view, light,
						viewLight->globalTranslucentShadowMapCasters,
						SCENE_SHADOW_CASTER_MAP_GLOBAL_TRANSLUCENT,
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_TRANSLUCENT,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareMappedShadowChain( packetFrame, view, light,
						viewLight->localTranslucentShadowMapCasters,
						SCENE_SHADOW_CASTER_MAP_LOCAL_TRANSLUCENT,
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareStencilShadowChain( packetFrame, view, light,
						viewLight->globalShadowMapStencilSupplements,
						SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL,
						CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL,
						RENDER_PASS_SHADOW_MAP,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error )
					|| !PrepareStencilShadowChain( packetFrame, view, light,
						viewLight->localShadowMapStencilSupplements,
						SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL,
						CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL,
						RENDER_PASS_SHADOW_MAP,
						view.shadowMapPassPacketIndex, mapPacketCursor,
						mapPacketEnd, arenaCheckpoint.lightCount, error ) ) {
				return FailView( view, arenaCheckpoint, error );
			}
		}
		if ( !BuildShadowMapPasses( view, light, error ) ) {
			return FailView( view, arenaCheckpoint, error );
		}
		RecomputePlannedStencilWork( light );
		if ( light.shadowMapPassIndex[
				CLASSIC_INTERACTION_RECEIVER_LOCAL ] >= 0
				|| light.shadowMapPassIndex[
					CLASSIC_INTERACTION_RECEIVER_GLOBAL ] >= 0 ) {
			const bool anyHybrid =
				light.receiverShadowMode[
					CLASSIC_INTERACTION_RECEIVER_LOCAL ]
					== CLASSIC_INTERACTION_SHADOW_HYBRID
				|| light.receiverShadowMode[
					CLASSIC_INTERACTION_RECEIVER_GLOBAL ]
					== CLASSIC_INTERACTION_SHADOW_HYBRID;
			view.shadowMode = anyHybrid
				? CLASSIC_INTERACTION_SHADOW_HYBRID
				: ( light.pointLight
					? CLASSIC_INTERACTION_SHADOW_POINT
					: CLASSIC_INTERACTION_SHADOW_PROJECTED );
		}

		const drawSurf_t *chains[ CLASSIC_INTERACTION_RECEIVER_COUNT ] = {
			viewLight->localInteractions,
			viewLight->globalInteractions,
			r_skipTranslucent.GetBool() ? NULL
				: viewLight->translucentInteractions
		};
		const sceneInteractionReceiverClass_t packetReceivers[
				CLASSIC_INTERACTION_RECEIVER_COUNT ] = {
			SCENE_INTERACTION_RECEIVER_LOCAL,
			SCENE_INTERACTION_RECEIVER_GLOBAL,
			SCENE_INTERACTION_RECEIVER_TRANSLUCENT
		};
		for ( int receiverIndex = 0;
				receiverIndex < CLASSIC_INTERACTION_RECEIVER_COUNT;
				++receiverIndex ) {
			int receiverOrdinal = 0;
			for ( const drawSurf_t *drawSurf = chains[ receiverIndex ];
					drawSurf != NULL;
					drawSurf = drawSurf->nextOnLight, ++receiverOrdinal ) {
				if ( !SurfacePacketEligible( drawSurf ) ) {
					SetError( error,
						CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_PACKET,
						receiverIndex, view.interactionPassPacketIndex,
						packetCursor, sourceLightOrdinal, receiverOrdinal );
					return FailView( view, arenaCheckpoint, error );
				}
				if ( packetCursor < interactionPass->firstDrawPacket
						|| packetCursor >= packetEnd ) {
					SetError( error,
						CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
						expectedSourceOrdinal,
						view.interactionPassPacketIndex, packetCursor,
						sourceLightOrdinal, receiverOrdinal );
					return FailView( view, arenaCheckpoint, error );
				}
				const drawPacket_t &packet =
					packetFrame.DrawPacket( packetCursor );
				if ( !ValidateDrawPacket( packetFrame, packet, packetCursor,
						viewDef, viewLight, sourceLightOrdinal,
						packetReceivers[ receiverIndex ], receiverOrdinal,
						expectedSourceOrdinal, drawSurf, error ) ) {
					error.passPacketIndex = view.interactionPassPacketIndex;
					return FailView( view, arenaCheckpoint, error );
				}
				if ( domain.surfaceCount
						>= CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES ) {
					SetError( error,
						CLASSIC_INTERACTION_FAILURE_SURFACE_POOL_OVERFLOW,
						domain.surfaceCount,
						view.interactionPassPacketIndex, packetCursor,
						sourceLightOrdinal, receiverOrdinal );
					return FailView( view, arenaCheckpoint, error );
				}
				const int absoluteSurfaceIndex = domain.surfaceCount++;
				classicInteractionDomainSurface_t &surface =
					domain.surfaces[ absoluteSurfaceIndex ];
				InitSurface( surface );
				surface.legacyDrawSurf = drawSurf;
				surface.legacyViewLight = viewLight;
				surface.drawPacketIndex = packetCursor;
				surface.lightIndex = absoluteLightIndex;
				surface.sourceOrdinal = expectedSourceOrdinal;
				surface.receiverOrdinal = receiverOrdinal;
				surface.receiver = static_cast<
					classicInteractionDomainReceiver_t>( receiverIndex );
				surface.materialTableRecordIndex = packet.materialRecordIndex;
				surface.vertexCount = packet.vertexCount;
				surface.firstIndex = packet.firstIndex;
				surface.indexCount = packet.indexCount;
				surface.vertexOffset = packet.vertexOffset;
				surface.scissorX1 = packet.scissorX1;
				surface.scissorY1 = packet.scissorY1;
				surface.scissorX2 = packet.scissorX2;
				surface.scissorY2 = packet.scissorY2;
				if ( MaterialRequestsClassicDeform( drawSurf ) ) {
					surface.deformRole = packet.classicDeformRecord->role;
					surface.deformOutcome = packet.classicDeformRecord->outcome;
					surface.deformContractHash =
						packet.classicDeformRecord->semanticHash;
				}
				const materialResourceTableRecord_t *tableRecord = NULL;
				if ( !ValidateSurfaceMaterial( packet, *drawSurf,
						sourceLightOrdinal, receiverOrdinal, view, tableRecord,
						error ) ) {
					error.passPacketIndex = view.interactionPassPacketIndex;
					error.drawPacketIndex = packetCursor;
					return FailView( view, arenaCheckpoint, error );
				}
				surface.materialId = tableRecord->materialId;
				surface.tableGeneration = tableRecord->tableGeneration;
				if ( !PrepareSurfacePrimitives( view, light, surface, packet,
						arenaCheckpoint.lightCount,
						arenaCheckpoint.surfaceCount, error ) ) {
					error.passPacketIndex = view.interactionPassPacketIndex;
					error.drawPacketIndex = packetCursor;
					error.lightOrdinal = sourceLightOrdinal;
					error.receiverOrdinal = receiverOrdinal;
					return FailView( view, arenaCheckpoint, error );
				}
				surface.hash = HashSurface( surface,
					arenaCheckpoint.lightCount );
				light.surfaceCount++;
				light.primitiveCount += surface.primitiveCount;
				light.drawablePrimitiveCount
					+= surface.drawablePrimitiveCount;
				light.noopPrimitiveCount += surface.noopPrimitiveCount;
				light.receiverSurfaceCount[ receiverIndex ]++;
				light.receiverPrimitiveCount[ receiverIndex ]
					+= surface.primitiveCount;
				packetCursor++;
				expectedSourceOrdinal++;
			}
		}
		light.hash = HashLight( light );
	}
	if ( packetCursor != packetEnd ) {
		SetError( error, CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor, view.interactionPassPacketIndex,
			packetCursor );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( stencilShadowPass != NULL
			&& shadowPacketCursor != shadowPacketEnd ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
			shadowPacketEnd - shadowPacketCursor,
			view.stencilShadowPassPacketIndex, shadowPacketCursor );
		return FailView( view, arenaCheckpoint, error );
	}
	if ( shadowMapPass != NULL && mapPacketCursor != mapPacketEnd ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH,
			mapPacketEnd - mapPacketCursor,
			view.shadowMapPassPacketIndex, mapPacketCursor );
		return FailView( view, arenaCheckpoint, error );
	}

	// Publish only after the full light/receiver/stage walk reconciles exactly.
	// Until ready becomes true, accessors reject every staged arena range.
	view.firstLight = arenaCheckpoint.lightCount;
	view.lightCount = domain.lightCount - arenaCheckpoint.lightCount;
	view.firstSurface = arenaCheckpoint.surfaceCount;
	view.surfaceCount = domain.surfaceCount - arenaCheckpoint.surfaceCount;
	view.firstPrimitive = arenaCheckpoint.primitiveCount;
	view.primitiveCount = domain.primitiveCount - arenaCheckpoint.primitiveCount;
	view.firstShadowCaster = arenaCheckpoint.shadowCasterCount;
	view.shadowCasterCount = domain.shadowCasterCount
		- arenaCheckpoint.shadowCasterCount;
	view.firstShadowMapPass = arenaCheckpoint.shadowMapPassCount;
	view.shadowMapPassCount = domain.shadowMapPassCount
		- arenaCheckpoint.shadowMapPassCount;
	for ( int i = 0; i < view.lightCount; ++i ) {
		const classicInteractionDomainLight_t &light =
			domain.lights[ view.firstLight + i ];
		view.drawablePrimitiveCount += light.drawablePrimitiveCount;
		view.noopPrimitiveCount += light.noopPrimitiveCount;
		view.drawableShadowCasterCount += light.drawableShadowCasters;
		view.noopShadowCasterCount += light.noopShadowCasters;
		view.logicalVolumeDrawCount += light.logicalVolumeDraws;
		view.preloadVolumeDrawCount += light.preloadVolumeDraws;
		if ( light.shadowCasterTotal > 0 ) {
			view.shadowLightCount++;
		}
		bool mappedLight = false;
		for ( int receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
				receiver <= CLASSIC_INTERACTION_RECEIVER_GLOBAL; ++receiver ) {
			const int passIndex = light.shadowMapPassIndex[ receiver ];
			if ( passIndex < 0 ) {
				continue;
			}
			mappedLight = true;
			if ( domain.shadowMapPasses[ passIndex ].disposition
					== CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID ) {
				view.hybridShadowPassCount++;
			}
			const classicInteractionDomainShadowMapPass_t &mapPass =
				domain.shadowMapPasses[ passIndex ];
			if ( mapPass.lightClass == SHADOWMAP_LIGHT_POINT ) {
				view.pointShadowMapPassCount++;
			} else {
				view.projectedShadowMapPassCount++;
				if ( mapPass.projected.state.cascadeCount > 1 ) {
					view.csmShadowMapPassCount++;
				}
			}
		}
		if ( mappedLight ) {
			if ( light.pointLight ) {
				view.pointShadowLightCount++;
			} else {
				view.projectedShadowLightCount++;
			}
		}
		view.activeLightStageCount += light.activeLightStageCount;
		view.inactiveLightStageCount += light.inactiveLightStageCount;
		for ( int receiver = 0;
				receiver < CLASSIC_INTERACTION_RECEIVER_COUNT; ++receiver ) {
			view.receiverSurfaceCount[ receiver ]
				+= light.receiverSurfaceCount[ receiver ];
			view.receiverPrimitiveCount[ receiver ]
				+= light.receiverPrimitiveCount[ receiver ];
		}
	}
	for ( int i = 0; i < view.surfaceCount; ++i ) {
		const classicInteractionDomainSurface_t &surface =
			domain.surfaces[ view.firstSurface + i ];
		view.activeSurfaceStageCount += surface.activeSurfaceStageCount;
		view.inactiveSurfaceStageCount += surface.inactiveSurfaceStageCount;
		if ( surface.deformContractHash != 0 ) {
			view.materialDeformReceiverCount++;
		}
	}
	for ( int i = 0; i < view.shadowCasterCount; ++i ) {
		const classicInteractionDomainShadowCaster_t &caster =
			domain.shadowCasters[ view.firstShadowCaster + i ];
		if ( caster.deformContractHash == 0 ) {
			continue;
		}
		if ( caster.deformOutcome == CLASSIC_DEFORM_OUTCOME_COMPLETED ) {
			view.completedDeformCasterCount++;
		} else if ( caster.deformOutcome == CLASSIC_DEFORM_OUTCOME_EMPTY ) {
			view.emptyDeformCasterCount++;
		}
	}
	bool anyStencilMode = false;
	bool anyProjectedMode = false;
	bool anyPointMode = false;
	bool anyHybridMode = false;
	for ( int lightIndex = 0; lightIndex < view.lightCount; ++lightIndex ) {
		const classicInteractionDomainLight_t &light =
			domain.lights[ view.firstLight + lightIndex ];
		for ( int receiver = 0;
				receiver < CLASSIC_INTERACTION_RECEIVER_COUNT; ++receiver ) {
			switch ( light.receiverShadowMode[ receiver ] ) {
			case CLASSIC_INTERACTION_SHADOW_STENCIL:
				anyStencilMode = true;
				break;
			case CLASSIC_INTERACTION_SHADOW_PROJECTED:
				anyProjectedMode = true;
				break;
			case CLASSIC_INTERACTION_SHADOW_POINT:
				anyPointMode = true;
				break;
			case CLASSIC_INTERACTION_SHADOW_HYBRID:
				anyHybridMode = true;
				break;
			default:
				break;
			}
		}
	}
	const int distinctShadowModes = ( anyStencilMode ? 1 : 0 )
		+ ( anyProjectedMode ? 1 : 0 ) + ( anyPointMode ? 1 : 0 )
		+ ( anyHybridMode ? 1 : 0 );
	if ( anyHybridMode || distinctShadowModes > 1 ) {
		view.shadowMode = CLASSIC_INTERACTION_SHADOW_HYBRID;
	} else if ( anyPointMode ) {
		view.shadowMode = CLASSIC_INTERACTION_SHADOW_POINT;
	} else if ( anyProjectedMode ) {
		view.shadowMode = CLASSIC_INTERACTION_SHADOW_PROJECTED;
	} else if ( anyStencilMode ) {
		view.shadowMode = CLASSIC_INTERACTION_SHADOW_STENCIL;
	} else {
		view.shadowMode = CLASSIC_INTERACTION_SHADOW_NONE;
	}
	if ( view.drawablePrimitiveCount + view.noopPrimitiveCount
			!= view.primitiveCount
			|| view.drawableShadowCasterCount
				+ view.noopShadowCasterCount != view.shadowCasterCount ) {
		SetError( error,
			CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
			view.primitiveCount );
		return FailView( view, arenaCheckpoint, error );
	}
	view.failure = CLASSIC_INTERACTION_FAILURE_NONE;
	view.hash = HashView( view );
	view.ready = true;
	domain.stats.readyViews++;
	domain.stats.lights += view.lightCount;
	domain.stats.surfaces += view.surfaceCount;
	domain.stats.primitives += view.primitiveCount;
	domain.stats.drawablePrimitives += view.drawablePrimitiveCount;
	domain.stats.noopPrimitives += view.noopPrimitiveCount;
	domain.stats.shadowCasters += view.shadowCasterCount;
	domain.stats.drawableShadowCasters += view.drawableShadowCasterCount;
	domain.stats.noopShadowCasters += view.noopShadowCasterCount;
	domain.stats.logicalVolumeDraws += view.logicalVolumeDrawCount;
	domain.stats.preloadVolumeDraws += view.preloadVolumeDrawCount;
	domain.stats.shadowLights += view.shadowLightCount;
	domain.stats.shadowMapPasses += view.shadowMapPassCount;
	domain.stats.hybridShadowPasses += view.hybridShadowPassCount;
	domain.stats.projectedShadowMapPasses
		+= view.projectedShadowMapPassCount;
	domain.stats.csmShadowMapPasses += view.csmShadowMapPassCount;
	domain.stats.pointShadowMapPasses += view.pointShadowMapPassCount;
	domain.stats.projectedShadowLights += view.projectedShadowLightCount;
	domain.stats.pointShadowLights += view.pointShadowLightCount;
	domain.stats.activeLightStages += view.activeLightStageCount;
	domain.stats.inactiveLightStages += view.inactiveLightStageCount;
	domain.stats.activeSurfaceStages += view.activeSurfaceStageCount;
	domain.stats.inactiveSurfaceStages += view.inactiveSurfaceStageCount;
	domain.stats.materialDeformReceivers += view.materialDeformReceiverCount;
	domain.stats.completedDeformCasters += view.completedDeformCasterCount;
	domain.stats.emptyDeformCasters += view.emptyDeformCasterCount;
	for ( int receiver = 0; receiver < CLASSIC_INTERACTION_RECEIVER_COUNT;
			receiver++ ) {
		domain.stats.receiverSurfaces[ receiver ]
			+= view.receiverSurfaceCount[ receiver ];
		domain.stats.receiverPrimitives[ receiver ]
			+= view.receiverPrimitiveCount[ receiver ];
	}
	return true;
}

static void RecordFallback( classicInteractionDomainView_t *view,
		classicInteractionDomainBackend_t backend,
		classicInteractionDomainFailure_t failure, int detail,
		int drawnPrimitives, int noopPrimitives,
		int submittedShadowCasters, int noopShadowCasters,
		int logicalVolumeDraws, int preloadVolumeDraws,
		int shadowMapPasses, int hybridPasses ) {
	classicInteractionDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view == NULL ) {
		coverage.untrackedFallbacks++;
		return;
	}
	const int viewIndex = ViewIndex( view );
	if ( viewIndex < 0 || view->backendOutcome[ backend ]
			!= CLASSIC_INTERACTION_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return;
	}
	view->backendOutcome[ backend ] = CLASSIC_INTERACTION_BACKEND_FALLBACK;
	view->backendFailure[ backend ] = failure == CLASSIC_INTERACTION_FAILURE_NONE
		? CLASSIC_INTERACTION_FAILURE_BACKEND_REJECTED : failure;
	view->backendFailureDetail[ backend ] = detail;
	view->backendDrawnPrimitives[ backend ] = drawnPrimitives;
	view->backendNoopPrimitives[ backend ] = noopPrimitives;
	view->backendShadowCasters[ backend ] = submittedShadowCasters;
	view->backendNoopShadowCasters[ backend ] = noopShadowCasters;
	view->backendLogicalVolumeDraws[ backend ] = logicalVolumeDraws;
	view->backendPreloadVolumeDraws[ backend ] = preloadVolumeDraws;
	view->backendShadowMapPasses[ backend ] = shadowMapPasses;
	view->backendHybridPasses[ backend ] = hybridPasses;
	coverage.fallbackViewMask |= 1ull << viewIndex;
	coverage.fallbackViews++;
	coverage.fallbackLights += view->lightCount;
	coverage.fallbackSurfaces += view->surfaceCount;
	coverage.fallbackDrawablePrimitives += view->drawablePrimitiveCount;
	coverage.fallbackNoopPrimitives += view->noopPrimitiveCount;
	coverage.fallbackShadowCasters += view->drawableShadowCasterCount;
	coverage.fallbackNoopShadowCasters += view->noopShadowCasterCount;
	coverage.fallbackLogicalVolumeDraws += view->logicalVolumeDrawCount;
	coverage.fallbackPreloadVolumeDraws += view->preloadVolumeDrawCount;
	coverage.fallbackShadowMapPasses += view->shadowMapPassCount;
	coverage.fallbackHybridPasses += view->hybridShadowPassCount;
	if ( failure == CLASSIC_INTERACTION_FAILURE_BACKEND_COVERAGE_MISMATCH ) {
		coverage.coverageMismatches++;
	}
	if ( view->backendFailure[ backend ] >= CLASSIC_INTERACTION_FAILURE_NONE
			&& view->backendFailure[ backend ]
				< CLASSIC_INTERACTION_FAILURE_COUNT ) {
		domain.stats.failureCounts[ view->backendFailure[ backend ] ]++;
	}
}

} // namespace

void R_ClassicInteractionDomain_ResetFrame( void ) {
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	domain.viewCount = 0;
	domain.lightCount = 0;
	domain.surfaceCount = 0;
	domain.primitiveCount = 0;
	domain.shadowCasterCount = 0;
	domain.shadowAlphaStageCount = 0;
	domain.shadowMapPassCount = 0;
	domain.textureCount = 0;
	InvalidateTextureLookup();
	domain.generation++;
	if ( domain.generation == 0 ) {
		domain.generation = 1;
	}
	idStr::Copynz( domain.stats.status, "empty",
		sizeof( domain.stats.status ) );
}

void R_ClassicInteractionDomain_PrepareFrame(
		const idScenePacketFrame &packetFrame ) {
	R_ClassicInteractionDomain_ResetFrame();
	domain.stats.prepared = true;
	domain.stats.sourceScenes = packetFrame.NumScenes();
	domain.stats.overflow = packetFrame.Stats().overflow;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes();
			++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( !SceneIsInteractionCandidate( packetFrame, scene ) ) {
			continue;
		}
		if ( FindMutableView( scene.viewDef ) != NULL ) {
			continue;
		}
		domain.stats.interactionViews++;
		if ( domain.viewCount >= CLASSIC_INTERACTION_DOMAIN_MAX_VIEWS ) {
			domain.stats.overflow = true;
			domain.stats.fallbackViews++;
			domain.stats.failureCounts[
				CLASSIC_INTERACTION_FAILURE_VIEW_POOL_OVERFLOW ]++;
			continue;
		}
		classicInteractionDomainView_t &view =
			domain.views[ domain.viewCount++ ];
		InitView( view, scene.viewDef, sceneIndex );
		R_ClassicInteractionDomain_PrepareView( packetFrame, scene, view );
	}

	domain.stats.textures = domain.textureCount;
	domain.stats.shadowAlphaStages = domain.shadowAlphaStageCount;
	domain.stats.frameValid = !domain.stats.overflow
		&& domain.stats.fallbackViews == 0;
	std::uint64_t frameHash = HASH_OFFSET;
	HashInt( frameHash, domain.stats.sourceScenes );
	HashInt( frameHash, domain.stats.interactionViews );
	for ( int i = 0; i < domain.viewCount; ++i ) {
		HashInt( frameHash, domain.views[ i ].scenePacketIndex );
		HashBool( frameHash, domain.views[ i ].ready );
		HashInt( frameHash, domain.views[ i ].failure );
		HashU64( frameHash, domain.views[ i ].hash );
	}
	domain.stats.hash = frameHash;
	if ( domain.stats.interactionViews == 0 ) {
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

const classicInteractionDomainStats_t &R_ClassicInteractionDomain_Stats(
		void ) {
	return domain.stats;
}

int R_ClassicInteractionDomain_NumViews( void ) {
	return domain.viewCount;
}

const classicInteractionDomainView_t *R_ClassicInteractionDomain_ViewByIndex(
		int index ) {
	return index >= 0 && index < domain.viewCount
		? &domain.views[ index ] : NULL;
}

const classicInteractionDomainView_t *R_ClassicInteractionDomain_ViewForScenePacket(
		int scenePacketIndex ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].scenePacketIndex == scenePacketIndex ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

const classicInteractionDomainView_t *R_ClassicInteractionDomain_FindView(
		const viewDef_t *viewDef ) {
	return FindMutableView( viewDef );
}

const classicInteractionDomainLight_t *R_ClassicInteractionDomain_ViewLight(
		const classicInteractionDomainView_t &view, int lightIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || lightIndex < 0
			|| lightIndex >= view.lightCount
			|| !RangeFits( view.firstLight, view.lightCount,
				domain.lightCount ) ) {
		return NULL;
	}
	return &domain.lights[ view.firstLight + lightIndex ];
}

const classicInteractionDomainSurface_t *R_ClassicInteractionDomain_ViewSurface(
		const classicInteractionDomainView_t &view, int surfaceIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || surfaceIndex < 0
			|| surfaceIndex >= view.surfaceCount
			|| !RangeFits( view.firstSurface, view.surfaceCount,
				domain.surfaceCount ) ) {
		return NULL;
	}
	return &domain.surfaces[ view.firstSurface + surfaceIndex ];
}

const classicInteractionDomainPrimitive_t *R_ClassicInteractionDomain_ViewPrimitive(
		const classicInteractionDomainView_t &view, int primitiveIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || primitiveIndex < 0
			|| primitiveIndex >= view.primitiveCount
			|| !RangeFits( view.firstPrimitive, view.primitiveCount,
				domain.primitiveCount ) ) {
		return NULL;
	}
	return &domain.primitives[ view.firstPrimitive + primitiveIndex ];
}

const classicInteractionDomainShadowCaster_t *R_ClassicInteractionDomain_ViewShadowCaster(
		const classicInteractionDomainView_t &view, int casterIndex ) {
	if ( !view.ready || ViewIndex( &view ) < 0 || casterIndex < 0
			|| casterIndex >= view.shadowCasterCount
			|| !RangeFits( view.firstShadowCaster, view.shadowCasterCount,
				domain.shadowCasterCount ) ) {
		return NULL;
	}
	return &domain.shadowCasters[ view.firstShadowCaster + casterIndex ];
}

const classicInteractionDomainShadowCaster_t *R_ClassicInteractionDomain_LightShadowCaster(
		const classicInteractionDomainLight_t &light,
		classicInteractionDomainShadowChain_t chain, int casterIndex ) {
	if ( LightIndex( &light ) < 0
			|| chain < CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL
			|| chain >= CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT
			|| casterIndex < 0 || casterIndex >= light.shadowCasterCount[ chain ]
			|| !RangeFits( light.firstShadowCaster[ chain ],
				light.shadowCasterCount[ chain ], domain.shadowCasterCount ) ) {
		return NULL;
	}
	return &domain.shadowCasters[
		light.firstShadowCaster[ chain ] + casterIndex ];
}

bool R_ClassicInteractionDomain_ShadowCasterNoopValid(
		const classicInteractionDomainShadowCaster_t &caster ) {
	if ( caster.disposition != CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY
			|| caster.selectedIndexCount != 0 || caster.alphaStageCount != 0
			|| caster.firstAlphaStage != -1
			|| ( caster.materialCoverage != MC_OPAQUE
				&& caster.materialCoverage != MC_PERFORATED ) ) {
		return false;
	}
	const bool sealedEmptyDeform = caster.deformContractHash != 0
		&& caster.deformRole == CLASSIC_DEFORM_ROLE_FINALIZED_DRAW
		&& caster.deformOutcome == CLASSIC_DEFORM_OUTCOME_EMPTY
		&& caster.vertexCount == 0 && caster.totalIndexCount == 0;
	const bool inactivePerforated = caster.deformContractHash == 0
		&& caster.materialCoverage == MC_PERFORATED
		&& caster.vertexCount > 0 && caster.totalIndexCount > 0;
	return sealedEmptyDeform || inactivePerforated;
}

const classicInteractionDomainShadowAlphaStage_t *R_ClassicInteractionDomain_ShadowAlphaStage(
		const classicInteractionDomainShadowCaster_t &caster, int stageIndex ) {
	const int casterIndex = ShadowCasterIndex( &caster );
	if ( casterIndex < 0 || casterIndex >= domain.shadowCasterCount
			|| stageIndex < 0 || stageIndex >= caster.alphaStageCount
			|| !RangeFits( caster.firstAlphaStage, caster.alphaStageCount,
				domain.shadowAlphaStageCount ) ) {
		return NULL;
	}
	return &domain.shadowAlphaStages[ caster.firstAlphaStage + stageIndex ];
}

const classicInteractionDomainShadowMapPass_t *R_ClassicInteractionDomain_LightShadowMapPass(
		const classicInteractionDomainLight_t &light,
		classicInteractionDomainReceiver_t receiver ) {
	if ( LightIndex( &light ) < 0
			|| receiver < CLASSIC_INTERACTION_RECEIVER_LOCAL
			|| receiver > CLASSIC_INTERACTION_RECEIVER_GLOBAL ) {
		return NULL;
	}
	const int passIndex = light.shadowMapPassIndex[ receiver ];
	if ( passIndex < 0 || passIndex >= domain.shadowMapPassCount ) {
		return NULL;
	}
	const classicInteractionDomainShadowMapPass_t &pass =
		domain.shadowMapPasses[ passIndex ];
	return pass.lightIndex == LightIndex( &light )
		&& pass.receiver == receiver ? &pass : NULL;
}

const classicInteractionDomainSurface_t *R_ClassicInteractionDomain_LightSurface(
		const classicInteractionDomainLight_t &light, int surfaceIndex ) {
	if ( LightIndex( &light ) < 0 || surfaceIndex < 0
			|| surfaceIndex >= light.surfaceCount
			|| !RangeFits( light.firstSurface, light.surfaceCount,
				domain.surfaceCount ) ) {
		return NULL;
	}
	return &domain.surfaces[ light.firstSurface + surfaceIndex ];
}

const classicInteractionDomainPrimitive_t *R_ClassicInteractionDomain_SurfacePrimitive(
		const classicInteractionDomainSurface_t &surface, int primitiveIndex ) {
	if ( SurfaceIndex( &surface ) < 0 || primitiveIndex < 0
			|| primitiveIndex >= surface.primitiveCount
			|| !RangeFits( surface.firstPrimitive, surface.primitiveCount,
				domain.primitiveCount ) ) {
		return NULL;
	}
	return &domain.primitives[ surface.firstPrimitive + primitiveIndex ];
}

const classicInteractionDomainTexture_t *R_ClassicInteractionDomain_ResolveTexture(
		std::uint64_t textureResourceId ) {
	const int textureIndex = TextureIndexFromResourceId( textureResourceId );
	if ( textureIndex < 0 || textureIndex >= domain.textureCount ) {
		return NULL;
	}
	const classicInteractionDomainTexture_t &texture =
		domain.textures[ textureIndex ];
	if ( texture.textureResourceId != textureResourceId
			|| texture.image == NULL || !texture.loaded || texture.defaulted
			|| texture.mutableImage
			|| texture.image->GetStorageGeneration()
				!= texture.storageGeneration ) {
		return NULL;
	}
	return &texture;
}

bool R_ClassicInteractionDomain_RecordOwned( const viewDef_t *viewDef,
		classicInteractionDomainBackend_t backend, int drawnPrimitives,
		int noopPrimitives, int submittedShadowCasters,
		int noopShadowCasters, int logicalVolumeDraws,
		int preloadVolumeDraws, int shadowMapPasses,
		int hybridPasses ) {
	if ( backend < CLASSIC_INTERACTION_BACKEND_GL
			|| backend >= CLASSIC_INTERACTION_BACKEND_COUNT ) {
		return false;
	}
	classicInteractionDomainView_t *view = FindMutableView( viewDef );
	if ( view == NULL ) {
		return false;
	}
	classicInteractionDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ]
			!= CLASSIC_INTERACTION_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return view->backendOutcome[ backend ]
				== CLASSIC_INTERACTION_BACKEND_OWNED
			&& view->backendDrawnPrimitives[ backend ] == drawnPrimitives
			&& view->backendNoopPrimitives[ backend ] == noopPrimitives
			&& view->backendShadowCasters[ backend ]
				== submittedShadowCasters
			&& view->backendNoopShadowCasters[ backend ]
				== noopShadowCasters
			&& view->backendLogicalVolumeDraws[ backend ]
				== logicalVolumeDraws
			&& view->backendPreloadVolumeDraws[ backend ]
				== preloadVolumeDraws
			&& view->backendShadowMapPasses[ backend ]
				== shadowMapPasses
			&& view->backendHybridPasses[ backend ] == hybridPasses;
	}
	if ( !view->ready ) {
		RecordFallback( view, backend,
			CLASSIC_INTERACTION_FAILURE_BACKEND_NOT_READY,
			view->failure, drawnPrimitives, noopPrimitives,
			submittedShadowCasters, noopShadowCasters,
			logicalVolumeDraws, preloadVolumeDraws,
			shadowMapPasses, hybridPasses );
		return false;
	}
	if ( drawnPrimitives != view->drawablePrimitiveCount
			|| noopPrimitives != view->noopPrimitiveCount
			|| submittedShadowCasters != view->drawableShadowCasterCount
			|| noopShadowCasters != view->noopShadowCasterCount
			|| logicalVolumeDraws != view->logicalVolumeDrawCount
			|| preloadVolumeDraws != view->preloadVolumeDrawCount
			|| shadowMapPasses != view->shadowMapPassCount
			|| hybridPasses != view->hybridShadowPassCount ) {
		RecordFallback( view, backend,
			CLASSIC_INTERACTION_FAILURE_BACKEND_COVERAGE_MISMATCH,
			drawnPrimitives + noopPrimitives, drawnPrimitives,
			noopPrimitives, submittedShadowCasters, noopShadowCasters,
			logicalVolumeDraws, preloadVolumeDraws,
			shadowMapPasses, hybridPasses );
		return false;
	}
	const int viewIndex = ViewIndex( view );
	view->backendOutcome[ backend ] = CLASSIC_INTERACTION_BACKEND_OWNED;
	view->backendDrawnPrimitives[ backend ] = drawnPrimitives;
	view->backendNoopPrimitives[ backend ] = noopPrimitives;
	view->backendShadowCasters[ backend ] = submittedShadowCasters;
	view->backendNoopShadowCasters[ backend ] = noopShadowCasters;
	view->backendLogicalVolumeDraws[ backend ] = logicalVolumeDraws;
	view->backendPreloadVolumeDraws[ backend ] = preloadVolumeDraws;
	view->backendShadowMapPasses[ backend ] = shadowMapPasses;
	view->backendHybridPasses[ backend ] = hybridPasses;
	coverage.ownedViewMask |= 1ull << viewIndex;
	coverage.ownedViews++;
	coverage.ownedLights += view->lightCount;
	coverage.ownedSurfaces += view->surfaceCount;
	coverage.ownedDrawablePrimitives += drawnPrimitives;
	coverage.ownedNoopPrimitives += noopPrimitives;
	coverage.ownedShadowCasters += submittedShadowCasters;
	coverage.ownedNoopShadowCasters += noopShadowCasters;
	coverage.ownedLogicalVolumeDraws += logicalVolumeDraws;
	coverage.ownedPreloadVolumeDraws += preloadVolumeDraws;
	coverage.ownedShadowMapPasses += shadowMapPasses;
	coverage.ownedHybridPasses += hybridPasses;
	return true;
}

void R_ClassicInteractionDomain_RecordBackendFallback(
		const viewDef_t *viewDef, classicInteractionDomainBackend_t backend,
		classicInteractionDomainFailure_t failure, int detail ) {
	if ( backend < CLASSIC_INTERACTION_BACKEND_GL
			|| backend >= CLASSIC_INTERACTION_BACKEND_COUNT ) {
		return;
	}
	RecordFallback( FindMutableView( viewDef ), backend, failure, detail,
		0, 0, 0, 0, 0, 0, 0, 0 );
}

const classicInteractionDomainBackendCoverage_t &R_ClassicInteractionDomain_BackendCoverage(
		classicInteractionDomainBackend_t backend ) {
	static const classicInteractionDomainBackendCoverage_t empty = {};
	return backend >= CLASSIC_INTERACTION_BACKEND_GL
		&& backend < CLASSIC_INTERACTION_BACKEND_COUNT
		? domain.stats.backend[ backend ] : empty;
}

const char *ClassicInteractionDomainReceiver_Name(
		classicInteractionDomainReceiver_t receiver ) {
	switch ( receiver ) {
	case CLASSIC_INTERACTION_RECEIVER_LOCAL: return "local";
	case CLASSIC_INTERACTION_RECEIVER_GLOBAL: return "global";
	case CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT: return "translucent";
	case CLASSIC_INTERACTION_RECEIVER_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainPrimitiveDisposition_Name(
		classicInteractionDomainPrimitiveDisposition_t disposition ) {
	switch ( disposition ) {
	case CLASSIC_INTERACTION_PRIMITIVE_DRAW: return "draw";
	case CLASSIC_INTERACTION_PRIMITIVE_NOOP_INACTIVE_LIGHT:
		return "noopInactiveLight";
	case CLASSIC_INTERACTION_PRIMITIVE_NOOP_INACTIVE_SURFACE:
		return "noopInactiveSurface";
	case CLASSIC_INTERACTION_PRIMITIVE_NOOP_MISSING_BUMP:
		return "noopMissingBump";
	case CLASSIC_INTERACTION_PRIMITIVE_NOOP_BLACK: return "noopBlack";
	case CLASSIC_INTERACTION_PRIMITIVE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainDepth_Name(
		classicInteractionDomainDepth_t depth ) {
	switch ( depth ) {
	case CLASSIC_INTERACTION_DEPTH_EQUAL: return "equal";
	case CLASSIC_INTERACTION_DEPTH_LESS_OR_EQUAL: return "lessOrEqual";
	case CLASSIC_INTERACTION_DEPTH_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainShadowMode_Name(
		classicInteractionDomainShadowMode_t mode ) {
	switch ( mode ) {
	case CLASSIC_INTERACTION_SHADOW_NONE: return "none";
	case CLASSIC_INTERACTION_SHADOW_STENCIL: return "stencil";
	case CLASSIC_INTERACTION_SHADOW_PROJECTED: return "projected";
	case CLASSIC_INTERACTION_SHADOW_POINT: return "point";
	case CLASSIC_INTERACTION_SHADOW_HYBRID: return "hybrid";
	case CLASSIC_INTERACTION_SHADOW_MODE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainShadowChain_Name(
		classicInteractionDomainShadowChain_t chain ) {
	switch ( chain ) {
	case CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL:
		return "stencilGlobal";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL:
		return "stencilLocal";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC:
		return "mapGlobalStatic";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC:
		return "mapLocalStatic";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC:
		return "mapGlobalDynamic";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC:
		return "mapLocalDynamic";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_TRANSLUCENT:
		return "mapGlobalTranslucent";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT:
		return "mapLocalTranslucent";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL:
		return "supplementGlobal";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL:
		return "supplementLocal";
	case CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainFailure_Name(
		classicInteractionDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_INTERACTION_FAILURE_NONE: return "none";
	case CLASSIC_INTERACTION_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_INTERACTION_FAILURE_SCENE_PACKET_OVERFLOW:
		return "scenePacketOverflow";
	case CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_NOT_PREPARED:
		return "materialTableNotPrepared";
	case CLASSIC_INTERACTION_FAILURE_MATERIAL_TABLE_OVERFLOW:
		return "materialTableOverflow";
	case CLASSIC_INTERACTION_FAILURE_VIEW_POOL_OVERFLOW:
		return "viewPoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_LIGHT_POOL_OVERFLOW:
		return "lightPoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_SURFACE_POOL_OVERFLOW:
		return "surfacePoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_PRIMITIVE_POOL_OVERFLOW:
		return "primitivePoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_SHADOW_CASTER_POOL_OVERFLOW:
		return "shadowCasterPoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_SHADOW_ALPHA_STAGE_POOL_OVERFLOW:
		return "shadowAlphaStagePoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_TEXTURE_POOL_OVERFLOW:
		return "texturePoolOverflow";
	case CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_VIEW:
		return "unsupportedView";
	case CLASSIC_INTERACTION_FAILURE_INVALID_SCENE_RANGE:
		return "invalidSceneRange";
	case CLASSIC_INTERACTION_FAILURE_INVALID_INTERACTION_PASS:
		return "invalidInteractionPass";
	case CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_RANGE:
		return "invalidDrawRange";
	case CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH:
		return "sourcePacketMismatch";
	case CLASSIC_INTERACTION_FAILURE_INVALID_DRAW_PACKET:
		return "invalidDrawPacket";
	case CLASSIC_INTERACTION_FAILURE_MISSING_GEOMETRY_RECORD:
		return "missingGeometryRecord";
	case CLASSIC_INTERACTION_FAILURE_MISSING_INSTANCE_RECORD:
		return "missingInstanceRecord";
	case CLASSIC_INTERACTION_FAILURE_MISSING_MATERIAL_RECORD:
		return "missingMaterialRecord";
	case CLASSIC_INTERACTION_FAILURE_STALE_MATERIAL_RECORD:
		return "staleMaterialRecord";
	case CLASSIC_INTERACTION_FAILURE_MISSING_SHADER_REGISTERS:
		return "missingShaderRegisters";
	case CLASSIC_INTERACTION_FAILURE_REGISTER_OUT_OF_RANGE:
		return "registerOutOfRange";
	case CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE:
		return "nonfiniteValue";
	case CLASSIC_INTERACTION_FAILURE_SHADOWS: return "shadows";
	case CLASSIC_INTERACTION_FAILURE_SHADOW_MAP: return "shadowMap";
	case CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH:
		return "shadowPacketMismatch";
	case CLASSIC_INTERACTION_FAILURE_SHADOW_GEOMETRY:
		return "shadowGeometry";
	case CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING:
		return "customLighting";
	case CLASSIC_INTERACTION_FAILURE_DEFORM: return "deform";
	case CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT:
		return "deformContract";
	case CLASSIC_INTERACTION_FAILURE_SKINNING: return "skinning";
	case CLASSIC_INTERACTION_FAILURE_SPECIAL_SURFACE:
		return "specialSurface";
	case CLASSIC_INTERACTION_FAILURE_DEPTH_HACK: return "depthHack";
	case CLASSIC_INTERACTION_FAILURE_NEGATIVE_SCALE:
		return "negativeScale";
	case CLASSIC_INTERACTION_FAILURE_FLAT_DIFFUSE: return "flatDiffuse";
	case CLASSIC_INTERACTION_FAILURE_ENHANCED_MATERIAL:
		return "enhancedMaterial";
	case CLASSIC_INTERACTION_FAILURE_CEL_SHADING: return "celShading";
	case CLASSIC_INTERACTION_FAILURE_ALTERNATE_INTERACTION_PROGRAM:
		return "alternateInteractionProgram";
	case CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_MATERIAL:
		return "unsupportedMaterial";
	case CLASSIC_INTERACTION_FAILURE_UNSUPPORTED_STATE:
		return "unsupportedState";
	case CLASSIC_INTERACTION_FAILURE_DYNAMIC_RESOURCE:
		return "dynamicResource";
	case CLASSIC_INTERACTION_FAILURE_MISSING_RESOURCE:
		return "missingResource";
	case CLASSIC_INTERACTION_FAILURE_DEFAULTED_RESOURCE:
		return "defaultedResource";
	case CLASSIC_INTERACTION_FAILURE_UNLOADED_RESOURCE:
		return "unloadedResource";
	case CLASSIC_INTERACTION_FAILURE_BACKEND_NOT_READY:
		return "backendNotReady";
	case CLASSIC_INTERACTION_FAILURE_BACKEND_COVERAGE_MISMATCH:
		return "backendCoverageMismatch";
	case CLASSIC_INTERACTION_FAILURE_BACKEND_REJECTED:
		return "backendRejected";
	case CLASSIC_INTERACTION_FAILURE_COUNT:
	default: return "unknown";
	}
}

const char *ClassicInteractionDomainBackend_Name(
		classicInteractionDomainBackend_t backend ) {
	switch ( backend ) {
	case CLASSIC_INTERACTION_BACKEND_GL: return "GL";
	case CLASSIC_INTERACTION_BACKEND_VULKAN: return "Vulkan";
	case CLASSIC_INTERACTION_BACKEND_COUNT:
	default: return "unknown";
	}
}

bool RendererClassicInteractionDomain_RunSelfTest( void ) {
	classicInteractionDomainShadowCaster_t noopCaster;
	InitShadowCaster( noopCaster );
	noopCaster.disposition = CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY;
	noopCaster.materialCoverage = MC_OPAQUE;
	noopCaster.deformRole = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	noopCaster.deformOutcome = CLASSIC_DEFORM_OUTCOME_EMPTY;
	noopCaster.deformContractHash = 0x7a1234ull;
	if ( !R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster ) ) {
		return false;
	}
	noopCaster.deformRole = CLASSIC_DEFORM_ROLE_UNKNOWN;
	if ( R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster ) ) {
		return false;
	}
	noopCaster.deformRole = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	noopCaster.deformOutcome = CLASSIC_DEFORM_OUTCOME_COMPLETED;
	if ( R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster ) ) {
		return false;
	}
	noopCaster.deformOutcome = CLASSIC_DEFORM_OUTCOME_EMPTY;
	noopCaster.deformContractHash = 0;
	if ( R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster ) ) {
		return false;
	}
	noopCaster.materialCoverage = MC_PERFORATED;
	noopCaster.vertexCount = 4;
	noopCaster.totalIndexCount = 6;
	if ( !R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster ) ) {
		return false;
	}

	if ( CLASSIC_INTERACTION_DOMAIN_MAX_VIEWS != 64
			|| CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS != 4096
			|| CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES != 4096
			|| CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES != 16384
			|| CLASSIC_INTERACTION_DOMAIN_MAX_TEXTURES != 8192
			|| CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS != 4096
			|| CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES != 8192
			|| CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES != 8192
			|| !RangeFits( 16383, 1, 16384 )
			|| RangeFits( 16384, 1, 16384 )
			|| RangeFits( -1, 1, 16384 )
			|| idStr::Cmp( ClassicInteractionDomainReceiver_Name(
				CLASSIC_INTERACTION_RECEIVER_LOCAL ), "local" )
			|| idStr::Cmp( ClassicInteractionDomainReceiver_Name(
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ), "translucent" )
			|| idStr::Cmp( ClassicInteractionDomainDepth_Name(
				CLASSIC_INTERACTION_DEPTH_LESS_OR_EQUAL ), "lessOrEqual" )
			|| idStr::Cmp( ClassicInteractionDomainShadowMode_Name(
				CLASSIC_INTERACTION_SHADOW_STENCIL ), "stencil" )
			|| idStr::Cmp( ClassicInteractionDomainShadowMode_Name(
				CLASSIC_INTERACTION_SHADOW_HYBRID ), "hybrid" )
			|| idStr::Cmp( ClassicInteractionDomainShadowChain_Name(
				CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL ),
				"supplementLocal" )
			|| idStr::Cmp( ClassicInteractionDomainFailure_Name(
				CLASSIC_INTERACTION_FAILURE_SHADOWS ), "shadows" )
			|| idStr::Cmp( ClassicInteractionDomainFailure_Name(
				CLASSIC_INTERACTION_FAILURE_SHADOW_PACKET_MISMATCH ),
				"shadowPacketMismatch" )
			|| idStr::Cmp( ClassicInteractionDomainFailure_Name(
				CLASSIC_INTERACTION_FAILURE_CUSTOM_LIGHTING ),
				"customLighting" )
			|| idStr::Cmp( ClassicInteractionDomainFailure_Name(
				CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT ),
				"deformContract" )
			|| idStr::Cmp( ClassicInteractionDomainFailure_Name(
				CLASSIC_INTERACTION_FAILURE_BACKEND_COVERAGE_MISMATCH ),
				"backendCoverageMismatch" ) ) {
		return false;
	}

	// Register evaluation preserves classic large-scroll wrapping and clamping.
	shaderStage_t stage;
	std::memset( &stage, 0, sizeof( stage ) );
	stage.texture.hasMatrix = true;
	stage.texture.matrix[ 0 ][ 0 ] = 0;
	stage.texture.matrix[ 0 ][ 1 ] = 1;
	stage.texture.matrix[ 0 ][ 2 ] = 2;
	stage.texture.matrix[ 1 ][ 0 ] = 3;
	stage.texture.matrix[ 1 ][ 1 ] = 4;
	stage.texture.matrix[ 1 ][ 2 ] = 5;
	stage.color.registers[ 0 ] = 6;
	stage.color.registers[ 1 ] = 7;
	stage.color.registers[ 2 ] = 8;
	stage.color.registers[ 3 ] = 9;
	float registers[ 10 ] = {
		1.0f, 0.0f, 41.25f, 0.0f, 1.0f, -41.75f,
		-1.0f, 0.25f, 2.0f, 1.0f
	};
	classicInteractionBuildError_t evaluationError;
	InitError( evaluationError );
	float matrix[ 2 ][ 4 ];
	float color[ 4 ];
	if ( !EvaluateTextureMatrix( stage.texture, registers, 10, matrix,
			evaluationError, 4 )
			|| matrix[ 0 ][ 3 ] != 0.25f || matrix[ 1 ][ 3 ] != -0.75f
			|| !EvaluateStageColor( stage, registers, 10, color, true,
				evaluationError, 4 )
			|| color[ 0 ] != 0.0f || color[ 1 ] != 0.25f
			|| color[ 2 ] != 1.0f || color[ 3 ] != 1.0f ) {
		return false;
	}
	InitError( evaluationError );
	stage.conditionRegister = 10;
	bool active = false;
	if ( EvaluateCondition( stage, registers, 10, active, evaluationError, 4 )
			|| evaluationError.failure
				!= CLASSIC_INTERACTION_FAILURE_REGISTER_OUT_OF_RANGE ) {
		return false;
	}
	registers[ 0 ] = std::numeric_limits<float>::quiet_NaN();
	InitError( evaluationError );
	stage.conditionRegister = 0;
	if ( EvaluateCondition( stage, registers, 10, active, evaluationError, 4 )
			|| evaluationError.failure
				!= CLASSIC_INTERACTION_FAILURE_NONFINITE_VALUE ) {
		return false;
	}

	// RB_DetermineLightScale membership is based solely on the presence of an
	// interaction chain.  Keep the shader deliberately unset here: a future
	// fog/blend/material ownership filter must not leak into scale membership.
	drawSurf_t scaleReceiver;
	std::memset( &scaleReceiver, 0, sizeof( scaleReceiver ) );
	viewLight_t scaleLight;
	std::memset( &scaleLight, 0, sizeof( scaleLight ) );
	scaleLight.localInteractions = &scaleReceiver;
	if ( !ViewLightContributesToClassicScale( &scaleLight ) ) {
		return false;
	}
	scaleLight.localInteractions = NULL;
	scaleLight.globalInteractions = &scaleReceiver;
	if ( !ViewLightContributesToClassicScale( &scaleLight ) ) {
		return false;
	}
	scaleLight.globalInteractions = NULL;
	scaleLight.translucentInteractions = &scaleReceiver;
	if ( !ViewLightContributesToClassicScale( &scaleLight ) ) {
		return false;
	}
	scaleLight.translucentInteractions = NULL;
	if ( ViewLightContributesToClassicScale( &scaleLight ) ) {
		return false;
	}
	// A bright fog/blend light follows this exact accumulator once its receiver
	// chain admits it; ownership classification remains a separate decision.
	float scaleMaximum = 1.0f;
	if ( !AccumulateClassicLightScaleValue( 2.0f, 4.0f, scaleMaximum )
			|| scaleMaximum != 8.0f
			|| !AccumulateClassicLightScaleValue( 2.0f, -100.0f,
				scaleMaximum )
			|| scaleMaximum != 8.0f ) {
		return false;
	}

	const classicInteractionDomainStats_t savedStats = domain.stats;
	const std::uint32_t savedGeneration = domain.generation;
	const int savedViewCount = domain.viewCount;
	const int savedLightCount = domain.lightCount;
	const int savedSurfaceCount = domain.surfaceCount;
	const int savedPrimitiveCount = domain.primitiveCount;
	const int savedShadowCasterCount = domain.shadowCasterCount;
	const int savedShadowAlphaStageCount = domain.shadowAlphaStageCount;
	const int savedShadowMapPassCount = domain.shadowMapPassCount;
	const int savedTextureCount = domain.textureCount;
	const classicInteractionDomainView_t savedView = domain.views[ 0 ];
	const classicInteractionDomainLight_t savedLight = domain.lights[ 0 ];
	const classicInteractionDomainSurface_t savedSurface = domain.surfaces[ 0 ];
	const classicInteractionDomainPrimitive_t savedPrimitive =
		domain.primitives[ 0 ];
	const classicInteractionDomainTexture_t savedTexture = domain.textures[ 0 ];
	const classicInteractionDomainPrimitive_t savedCoveragePrimitive =
		domain.primitives[ 1 ];
	const classicInteractionDomainLight_t savedShiftedLight =
		domain.lights[ 3 ];
	const classicInteractionDomainSurface_t savedShiftedSurface =
		domain.surfaces[ 5 ];
	const classicInteractionDomainPrimitive_t savedShiftedPrimitive =
		domain.primitives[ 7 ];
	classicInteractionDomainShadowCaster_t savedShadowCasters[ 4 ];
	for ( int i = 0; i < 4; ++i ) {
		savedShadowCasters[ i ] = domain.shadowCasters[ i ];
	}
	classicInteractionDomainShadowMapPass_t savedMapPasses[ 4 ];
	for ( int i = 0; i < 4; ++i ) {
		savedMapPasses[ i ] = domain.shadowMapPasses[ i ];
	}
	classicInteractionDomainShadowAlphaStage_t alphaStageContract;
	InitShadowAlphaStage( alphaStageContract );
	alphaStageContract.stageIndex = 3;
	alphaStageContract.alphaTestMode = GL_GREATER;
	alphaStageContract.alphaTestValue = 0.5f;
	alphaStageContract.alphaScale = 1.0f;
	alphaStageContract.hash = HashShadowAlphaStage( alphaStageContract );
	if ( alphaStageContract.hash == 0 ) {
		return false;
	}
	classicInteractionDomainShadowCaster_t deformHashCaster;
	InitShadowCaster( deformHashCaster );
	const std::uint64_t undeformedCasterHash =
		HashShadowCaster( deformHashCaster, 0 );
	deformHashCaster.deformRole = CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	deformHashCaster.deformOutcome = CLASSIC_DEFORM_OUTCOME_EMPTY;
	deformHashCaster.deformContractHash = 0x81234ull;
	if ( HashShadowCaster( deformHashCaster, 0 )
			== undeformedCasterHash ) {
		return false;
	}

	// Stable hashes exclude generation-scoped resource-id high bits.
	domain.generation = 7;
	domain.textureCount = 1;
	std::memset( &domain.textures[ 0 ], 0, sizeof( domain.textures[ 0 ] ) );
	domain.textures[ 0 ].textureResourceId = MakeTextureResourceId( 0 );
	domain.textures[ 0 ].nameHash = HashString( "selftest/interaction" );
	classicInteractionDomainPrimitive_t first;
	InitPrimitive( first );
	first.lightImageResourceId = domain.textures[ 0 ].textureResourceId;
	first.diffuseColor[ 0 ] = 1.0f;
	const std::uint64_t firstHash = HashPrimitive( first, 0, 0 );
	classicInteractionDomainPrimitive_t same = first;
	if ( firstHash == 0 || HashPrimitive( same, 0, 0 ) != firstHash ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.lightCount = savedLightCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.lights[ 0 ] = savedLight;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}
	same.diffuseColor[ 0 ] = 0.5f;
	if ( HashPrimitive( same, 0, 0 ) == firstHash ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.lightCount = savedLightCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.lights[ 0 ] = savedLight;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}
	domain.generation = 8;
	domain.textures[ 0 ].textureResourceId = MakeTextureResourceId( 0 );
	first.lightImageResourceId = domain.textures[ 0 ].textureResourceId;
	if ( HashPrimitive( first, 0, 0 ) != firstHash ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.lightCount = savedLightCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.lights[ 0 ] = savedLight;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}

	// Identical published content must hash identically even when unrelated
	// earlier views move every arena checkpoint.  Public indices intentionally
	// remain arena-absolute; only their view-local identity enters the hashes.
	domain.lightCount = 4;
	domain.surfaceCount = 6;
	domain.primitiveCount = 8;
	classicInteractionDomainPrimitive_t originPrimitive;
	InitPrimitive( originPrimitive );
	originPrimitive.lightIndex = 0;
	originPrimitive.surfaceIndex = 0;
	originPrimitive.lightStageIndex = 2;
	originPrimitive.receiver = CLASSIC_INTERACTION_RECEIVER_GLOBAL;
	originPrimitive.disposition = CLASSIC_INTERACTION_PRIMITIVE_DRAW;
	originPrimitive.lightImageResourceId =
		domain.textures[ 0 ].textureResourceId;
	originPrimitive.diffuseColor[ 0 ] = 0.75f;
	originPrimitive.hash = HashPrimitive( originPrimitive, 0, 0 );
	domain.primitives[ 0 ] = originPrimitive;
	classicInteractionDomainPrimitive_t shiftedPrimitive = originPrimitive;
	shiftedPrimitive.lightIndex = 3;
	shiftedPrimitive.surfaceIndex = 5;
	shiftedPrimitive.hash = HashPrimitive( shiftedPrimitive, 3, 5 );
	domain.primitives[ 7 ] = shiftedPrimitive;

	classicInteractionDomainSurface_t originSurface;
	InitSurface( originSurface );
	originSurface.lightIndex = 0;
	originSurface.sourceOrdinal = 11;
	originSurface.receiverOrdinal = 2;
	originSurface.receiver = CLASSIC_INTERACTION_RECEIVER_GLOBAL;
	originSurface.materialId = 17;
	originSurface.firstPrimitive = 0;
	originSurface.primitiveCount = 1;
	originSurface.drawablePrimitiveCount = 1;
	originSurface.surfaceStageCount = 1;
	originSurface.activeSurfaceStageCount = 1;
	originSurface.hash = HashSurface( originSurface, 0 );
	classicInteractionDomainSurface_t deformSurface = originSurface;
	deformSurface.deformRole = CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER;
	deformSurface.deformOutcome = CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE;
	deformSurface.deformContractHash = 0x71234ull;
	if ( HashSurface( deformSurface, 0 ) == originSurface.hash ) {
		return false;
	}
	domain.surfaces[ 0 ] = originSurface;
	classicInteractionDomainSurface_t shiftedSurface = originSurface;
	shiftedSurface.lightIndex = 3;
	shiftedSurface.firstPrimitive = 7;
	shiftedSurface.hash = HashSurface( shiftedSurface, 3 );
	domain.surfaces[ 5 ] = shiftedSurface;

	classicInteractionDomainLight_t originLight;
	InitLight( originLight );
	originLight.sourceOrdinal = 4;
	originLight.firstSurface = 0;
	originLight.surfaceCount = 1;
	originLight.firstPrimitive = 0;
	originLight.primitiveCount = 1;
	originLight.drawablePrimitiveCount = 1;
	originLight.lightStageCount = 1;
	originLight.activeLightStageCount = 1;
	originLight.receiverSurfaceCount[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	originLight.receiverPrimitiveCount[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	originLight.hash = HashLight( originLight );
	domain.lights[ 0 ] = originLight;
	classicInteractionDomainLight_t shiftedLight = originLight;
	shiftedLight.firstSurface = 5;
	shiftedLight.firstPrimitive = 7;
	shiftedLight.hash = HashLight( shiftedLight );
	domain.lights[ 3 ] = shiftedLight;

	classicInteractionDomainView_t originView;
	InitView( originView, NULL, -1 );
	originView.ready = true;
	originView.firstLight = 0;
	originView.lightCount = 1;
	originView.firstSurface = 0;
	originView.surfaceCount = 1;
	originView.firstPrimitive = 0;
	originView.primitiveCount = 1;
	originView.drawablePrimitiveCount = 1;
	originView.packetDrawCount = 1;
	originView.activeLightStageCount = 1;
	originView.activeSurfaceStageCount = 1;
	originView.receiverSurfaceCount[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	originView.receiverPrimitiveCount[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	originView.maxLightValue = 8.0f;
	originView.lightScale = 0.25f;
	originView.overBright = 8.0f;
	originView.hash = HashView( originView );
	classicInteractionDomainView_t shiftedView = originView;
	shiftedView.firstLight = 3;
	shiftedView.firstSurface = 5;
	shiftedView.firstPrimitive = 7;
	shiftedView.hash = HashView( shiftedView );
	const bool arenaHashInvariant = originPrimitive.hash == shiftedPrimitive.hash
		&& originSurface.hash == shiftedSurface.hash
		&& originLight.hash == shiftedLight.hash
		&& originView.hash == shiftedView.hash;
	domain.lightCount = savedLightCount;
	domain.surfaceCount = savedSurfaceCount;
	domain.primitiveCount = savedPrimitiveCount;
	domain.lights[ 0 ] = savedLight;
	domain.lights[ 3 ] = savedShiftedLight;
	domain.surfaces[ 0 ] = savedSurface;
	domain.surfaces[ 5 ] = savedShiftedSurface;
	domain.primitives[ 0 ] = savedPrimitive;
	domain.primitives[ 7 ] = savedShiftedPrimitive;
	if ( !arenaHashInvariant ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}

	// Mixed receiver modes can require a deliberate volume resubmission after
	// switching the stencil buffer between full volumes and hybrid supplements.
	// The shared plan counts that exact LOCAL -> GLOBAL -> TRANSLUCENT sequence,
	// while identical consecutive modes reuse the already prepared global half.
	domain.shadowCasterCount = 4;
	InitLight( domain.lights[ 0 ] );
	const classicInteractionDomainShadowChain_t planChains[ 4 ] = {
		CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL,
		CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL,
		CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL,
		CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL
	};
	for ( int i = 0; i < 4; ++i ) {
		InitShadowCaster( domain.shadowCasters[ i ] );
		domain.shadowCasters[ i ].disposition =
			CLASSIC_INTERACTION_SHADOW_CASTER_DRAW;
		domain.shadowCasters[ i ].preload = ( i & 1 ) == 0;
		domain.lights[ 0 ].firstShadowCaster[ planChains[ i ] ] = i;
		domain.lights[ 0 ].shadowCasterCount[ planChains[ i ] ] = 1;
	}
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_LOCAL ] =
		CLASSIC_INTERACTION_SHADOW_STENCIL;
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] =
		CLASSIC_INTERACTION_SHADOW_HYBRID;
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ] =
		CLASSIC_INTERACTION_SHADOW_STENCIL;
	RecomputePlannedStencilWork( domain.lights[ 0 ] );
	const bool mixedStencilPlan = domain.lights[ 0 ].logicalVolumeDraws == 5
		&& domain.lights[ 0 ].preloadVolumeDraws == 3
		&& domain.lights[ 0 ].clearStencil;
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_LOCAL ] =
		CLASSIC_INTERACTION_SHADOW_HYBRID;
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] =
		CLASSIC_INTERACTION_SHADOW_HYBRID;
	domain.lights[ 0 ].receiverShadowMode[
		CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT ] =
		CLASSIC_INTERACTION_SHADOW_HYBRID;
	RecomputePlannedStencilWork( domain.lights[ 0 ] );
	const bool reusedHybridPlan = domain.lights[ 0 ].logicalVolumeDraws == 2
		&& domain.lights[ 0 ].preloadVolumeDraws == 1;
	for ( int i = 0; i < 4; ++i ) {
		domain.shadowCasters[ i ].disposition =
			CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY;
	}
	RecomputePlannedStencilWork( domain.lights[ 0 ] );
	const bool noopStencilClearPlan =
		domain.lights[ 0 ].logicalVolumeDraws == 0
		&& domain.lights[ 0 ].preloadVolumeDraws == 0
		&& domain.lights[ 0 ].clearStencil;
	domain.shadowCasterCount = savedShadowCasterCount;
	domain.lights[ 0 ] = savedLight;
	for ( int i = 0; i < 4; ++i ) {
		domain.shadowCasters[ i ] = savedShadowCasters[ i ];
	}
	if ( !mixedStencilPlan || !reusedHybridPlan || !noopStencilClearPlan ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.lightCount = savedLightCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.shadowAlphaStageCount = savedShadowAlphaStageCount;
		domain.shadowMapPassCount = savedShadowMapPassCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}

	// Mapped ownership records distinguish projected-single, CSM/parallel,
	// point-cube and hybrid supplement semantics without backend handles.
	domain.lightCount = 1;
	domain.shadowMapPassCount = 4;
	InitLight( domain.lights[ 0 ] );
	classicInteractionDomainShadowMapPass_t &projectedPass =
		domain.shadowMapPasses[ 0 ];
	InitShadowMapPass( projectedPass );
	projectedPass.lightIndex = 0;
	projectedPass.receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	projectedPass.resourceOwner = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	projectedPass.disposition = CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED;
	projectedPass.mode = CLASSIC_INTERACTION_SHADOW_PROJECTED;
	projectedPass.lightClass = SHADOWMAP_LIGHT_PROJECTED;
	projectedPass.receiverMask = SHADOWMAP_RECEIVER_MASK_LOCAL;
	projectedPass.mappedCasterCount = 1;
	projectedPass.drawableMappedCasters = 1;
	projectedPass.resourcePlanId = 0x0000000700000001ull;
	projectedPass.resourceGeneration = 7;
	projectedPass.mapComplete = true;
	projectedPass.hybridComplete = true;
	projectedPass.mapRequired = true;
	projectedPass.projected.state.valid = true;
	projectedPass.projected.state.cascadeCount = 1;
	projectedPass.projected.state.atlasDiv = 1;
	projectedPass.projected.state.tileSize = 1024;
	projectedPass.hash = HashShadowMapPass( projectedPass );

	classicInteractionDomainShadowMapPass_t &csmParallelPass =
		domain.shadowMapPasses[ 1 ];
	csmParallelPass = projectedPass;
	csmParallelPass.receiver = CLASSIC_INTERACTION_RECEIVER_GLOBAL;
	csmParallelPass.resourceOwner = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	csmParallelPass.resourceAlias = true;
	csmParallelPass.lightClass = SHADOWMAP_LIGHT_PARALLEL;
	csmParallelPass.receiverMask = SHADOWMAP_RECEIVER_MASK_GLOBAL;
	csmParallelPass.projected.state.cascadeCount = 4;
	csmParallelPass.projected.state.atlasDiv = 2;
	csmParallelPass.hash = HashShadowMapPass( csmParallelPass );

	classicInteractionDomainShadowMapPass_t &pointCubePass =
		domain.shadowMapPasses[ 2 ];
	InitShadowMapPass( pointCubePass );
	pointCubePass.lightIndex = 0;
	pointCubePass.receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
	pointCubePass.disposition = CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED;
	pointCubePass.mode = CLASSIC_INTERACTION_SHADOW_POINT;
	pointCubePass.lightClass = SHADOWMAP_LIGHT_POINT;
	pointCubePass.point.valid = true;
	pointCubePass.point.faceCount = 6;
	pointCubePass.point.faceSize = 512;
	pointCubePass.point.farDistance = 256.0f;
	pointCubePass.mapComplete = true;
	pointCubePass.hybridComplete = true;
	pointCubePass.hash = HashShadowMapPass( pointCubePass );

	classicInteractionDomainShadowMapPass_t &hybridPass =
		domain.shadowMapPasses[ 3 ];
	hybridPass = pointCubePass;
	hybridPass.receiver = CLASSIC_INTERACTION_RECEIVER_GLOBAL;
	hybridPass.disposition = CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID;
	hybridPass.mode = CLASSIC_INTERACTION_SHADOW_HYBRID;
	hybridPass.mapComplete = false;
	hybridPass.supplementCasterCount = 1;
	hybridPass.drawableSupplementCasters = 1;
	hybridPass.hash = HashShadowMapPass( hybridPass );
	domain.lights[ 0 ].shadowMapPassIndex[
		CLASSIC_INTERACTION_RECEIVER_LOCAL ] = 0;
	domain.lights[ 0 ].shadowMapPassIndex[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	classicInteractionDomainShadowMapPass_t changedPass = projectedPass;
	changedPass.projected.filter.filterRadius = 2.0f;
	classicInteractionDomainShadowMapPass_t identityChangedPass =
		projectedPass;
	identityChangedPass.resourcePlanId = 0x0000006300000011ull;
	identityChangedPass.resourceGeneration = 99;
	classicInteractionDomainShadowMapPass_t supplementChangedPass =
		projectedPass;
	supplementChangedPass.supplementCasterCount = 1;
	supplementChangedPass.drawableSupplementCasters = 1;
	classicInteractionDomainShadowMapPass_t dynamicChangedPass = projectedPass;
	dynamicChangedPass.hasDynamicCasters = true;
	classicInteractionDomainShadowMapPass_t alphaChangedPass = projectedPass;
	alphaChangedPass.hasAlphaCasters = true;
	classicInteractionDomainShadowMapPass_t completenessChangedPass =
		projectedPass;
	completenessChangedPass.mapComplete = false;
	completenessChangedPass.hybridComplete = false;
	classicInteractionDomainView_t mappedViewContract;
	InitView( mappedViewContract, NULL, -1 );
	mappedViewContract.shadowMapPassCount = 4;
	mappedViewContract.hybridShadowPassCount = 1;
	mappedViewContract.projectedShadowMapPassCount = 2;
	mappedViewContract.csmShadowMapPassCount = 1;
	mappedViewContract.pointShadowMapPassCount = 2;
	const std::uint64_t mappedViewHash = HashView( mappedViewContract );
	const bool mappedContractOk = projectedPass.hash != 0
		&& csmParallelPass.hash != 0 && pointCubePass.hash != 0
		&& hybridPass.hash != 0
		&& projectedPass.hash == HashShadowMapPass( projectedPass )
		&& projectedPass.hash != HashShadowMapPass( changedPass )
		&& projectedPass.hash == HashShadowMapPass( identityChangedPass )
		&& projectedPass.hash != HashShadowMapPass( supplementChangedPass )
		&& projectedPass.hash != HashShadowMapPass( dynamicChangedPass )
		&& projectedPass.hash != HashShadowMapPass( alphaChangedPass )
		&& projectedPass.hash != HashShadowMapPass(
			completenessChangedPass )
		&& R_ClassicInteractionDomain_LightShadowMapPass(
			domain.lights[ 0 ], CLASSIC_INTERACTION_RECEIVER_LOCAL )
			== &projectedPass
		&& R_ClassicInteractionDomain_LightShadowMapPass(
			domain.lights[ 0 ], CLASSIC_INTERACTION_RECEIVER_GLOBAL )
			== &csmParallelPass
		&& csmParallelPass.resourceAlias
		&& csmParallelPass.resourceOwner
			== CLASSIC_INTERACTION_RECEIVER_LOCAL
		&& csmParallelPass.projected.state.cascadeCount == 4
		&& pointCubePass.point.faceCount == 6
		&& mappedViewHash != 0
		&& hybridPass.disposition
			== CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID;
	domain.lightCount = savedLightCount;
	domain.shadowMapPassCount = savedShadowMapPassCount;
	domain.lights[ 0 ] = savedLight;
	for ( int i = 0; i < 4; ++i ) {
		domain.shadowMapPasses[ i ] = savedMapPasses[ i ];
	}
	if ( !mappedContractOk ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.shadowCasterCount = savedShadowCasterCount;
		domain.shadowAlphaStageCount = savedShadowAlphaStageCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}

	// A failed preparation rewinds every arena and publishes no partial range.
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	domain.lightCount = 5;
	domain.surfaceCount = 7;
	domain.primitiveCount = 11;
	domain.shadowCasterCount = 13;
	domain.shadowAlphaStageCount = 17;
	domain.shadowMapPassCount = 19;
	domain.textureCount = 23;
	classicInteractionDomainView_t rollbackView;
	InitView( rollbackView, NULL, -1 );
	rollbackView.ready = true;
	rollbackView.firstLight = 2;
	rollbackView.lightCount = 3;
	rollbackView.firstSurface = 3;
	rollbackView.surfaceCount = 4;
	rollbackView.firstPrimitive = 5;
	rollbackView.primitiveCount = 6;
	rollbackView.hash = 1;
	const classicInteractionCheckpoint_t checkpoint = {
		2, 3, 5, 7, 11, 13, 17
	};
	classicInteractionBuildError_t rollbackError;
	SetError( rollbackError,
		CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH,
		41, 2, 7, 3, 4, 5 );
	const bool rollbackResult = FailView( rollbackView, checkpoint,
		rollbackError );
	const bool rollbackOk = !rollbackResult && !rollbackView.ready
		&& rollbackView.failure
			== CLASSIC_INTERACTION_FAILURE_SOURCE_PACKET_MISMATCH
		&& rollbackView.failureDetail == 41
		&& rollbackView.failurePassPacketIndex == 2
		&& rollbackView.failureDrawPacketIndex == 7
		&& rollbackView.failureLightOrdinal == 3
		&& rollbackView.failureReceiverOrdinal == 4
		&& rollbackView.failureStageIndex == 5
		&& rollbackView.firstLight == -1 && rollbackView.lightCount == 0
		&& rollbackView.firstSurface == -1
		&& rollbackView.surfaceCount == 0
		&& rollbackView.firstPrimitive == -1
		&& rollbackView.primitiveCount == 0 && rollbackView.hash == 0
		&& domain.lightCount == 2 && domain.surfaceCount == 3
		&& domain.primitiveCount == 5
		&& domain.shadowCasterCount == 7
		&& domain.shadowAlphaStageCount == 11
		&& domain.shadowMapPassCount == 13
		&& domain.textureCount == 17;
	if ( !rollbackOk ) {
		domain.stats = savedStats;
		domain.generation = savedGeneration;
		domain.viewCount = savedViewCount;
		domain.lightCount = savedLightCount;
		domain.surfaceCount = savedSurfaceCount;
		domain.primitiveCount = savedPrimitiveCount;
		domain.shadowCasterCount = savedShadowCasterCount;
		domain.shadowAlphaStageCount = savedShadowAlphaStageCount;
		domain.shadowMapPassCount = savedShadowMapPassCount;
		domain.textureCount = savedTextureCount;
		domain.views[ 0 ] = savedView;
		domain.lights[ 0 ] = savedLight;
		domain.surfaces[ 0 ] = savedSurface;
		domain.primitives[ 0 ] = savedPrimitive;
		domain.textures[ 0 ] = savedTexture;
		return false;
	}

	// Prove absolute arena identities and light -> surface -> primitive ordering.
	viewDef_t coverageView;
	std::memset( &coverageView, 0, sizeof( coverageView ) );
	domain.viewCount = 1;
	domain.lightCount = 1;
	domain.surfaceCount = 1;
	domain.primitiveCount = 2;
	domain.shadowCasterCount = 4;
	domain.shadowAlphaStageCount = 0;
	domain.shadowMapPassCount = 2;
	InitView( domain.views[ 0 ], &coverageView, 0 );
	domain.views[ 0 ].ready = true;
	domain.views[ 0 ].firstLight = 0;
	domain.views[ 0 ].lightCount = 1;
	domain.views[ 0 ].firstSurface = 0;
	domain.views[ 0 ].surfaceCount = 1;
	domain.views[ 0 ].firstPrimitive = 0;
	domain.views[ 0 ].primitiveCount = 2;
	domain.views[ 0 ].drawablePrimitiveCount = 1;
	domain.views[ 0 ].noopPrimitiveCount = 1;
	domain.views[ 0 ].firstShadowCaster = 0;
	domain.views[ 0 ].shadowCasterCount = 4;
	domain.views[ 0 ].drawableShadowCasterCount = 3;
	domain.views[ 0 ].noopShadowCasterCount = 1;
	domain.views[ 0 ].logicalVolumeDrawCount = 1;
	domain.views[ 0 ].preloadVolumeDrawCount = 1;
	domain.views[ 0 ].firstShadowMapPass = 0;
	domain.views[ 0 ].shadowMapPassCount = 2;
	domain.views[ 0 ].hybridShadowPassCount = 1;
	domain.views[ 0 ].projectedShadowMapPassCount = 1;
	domain.views[ 0 ].pointShadowMapPassCount = 1;
	domain.views[ 0 ].shadowMode = CLASSIC_INTERACTION_SHADOW_HYBRID;
	InitLight( domain.lights[ 0 ] );
	domain.lights[ 0 ].firstSurface = 0;
	domain.lights[ 0 ].surfaceCount = 1;
	InitSurface( domain.surfaces[ 0 ] );
	domain.surfaces[ 0 ].lightIndex = 0;
	domain.surfaces[ 0 ].firstPrimitive = 0;
	domain.surfaces[ 0 ].primitiveCount = 2;
	InitPrimitive( domain.primitives[ 0 ] );
	InitPrimitive( domain.primitives[ 1 ] );
	domain.primitives[ 0 ].lightIndex = 0;
	domain.primitives[ 0 ].surfaceIndex = 0;
	domain.primitives[ 1 ].lightIndex = 0;
	domain.primitives[ 1 ].surfaceIndex = 0;
	InitShadowMapPass( domain.shadowMapPasses[ 0 ] );
	domain.shadowMapPasses[ 0 ].lightIndex = 0;
	domain.shadowMapPasses[ 0 ].receiver =
		CLASSIC_INTERACTION_RECEIVER_LOCAL;
	domain.shadowMapPasses[ 0 ].disposition =
		CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED;
	domain.shadowMapPasses[ 0 ].mode =
		CLASSIC_INTERACTION_SHADOW_PROJECTED;
	InitShadowMapPass( domain.shadowMapPasses[ 1 ] );
	domain.shadowMapPasses[ 1 ].lightIndex = 0;
	domain.shadowMapPasses[ 1 ].receiver =
		CLASSIC_INTERACTION_RECEIVER_GLOBAL;
	domain.shadowMapPasses[ 1 ].disposition =
		CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID;
	domain.shadowMapPasses[ 1 ].mode = CLASSIC_INTERACTION_SHADOW_HYBRID;
	domain.lights[ 0 ].shadowMapPassIndex[
		CLASSIC_INTERACTION_RECEIVER_LOCAL ] = 0;
	domain.lights[ 0 ].shadowMapPassIndex[
		CLASSIC_INTERACTION_RECEIVER_GLOBAL ] = 1;
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	const bool ordered = R_ClassicInteractionDomain_ViewLight(
		domain.views[ 0 ], 0 ) == &domain.lights[ 0 ]
		&& R_ClassicInteractionDomain_LightSurface(
			domain.lights[ 0 ], 0 ) == &domain.surfaces[ 0 ]
		&& R_ClassicInteractionDomain_SurfacePrimitive(
			domain.surfaces[ 0 ], 0 ) == &domain.primitives[ 0 ]
		&& R_ClassicInteractionDomain_SurfacePrimitive(
			domain.surfaces[ 0 ], 1 ) == &domain.primitives[ 1 ]
		&& R_ClassicInteractionDomain_SurfacePrimitive(
			domain.surfaces[ 0 ], 2 ) == NULL;
	const bool firstOwned = R_ClassicInteractionDomain_RecordOwned(
		&coverageView, CLASSIC_INTERACTION_BACKEND_GL, 1, 1,
		3, 1, 1, 1, 2, 1 );
	const bool repeatedOwned = R_ClassicInteractionDomain_RecordOwned(
		&coverageView, CLASSIC_INTERACTION_BACKEND_GL, 1, 1,
		3, 1, 1, 1, 2, 1 );
	const bool mismatchRejected = !R_ClassicInteractionDomain_RecordOwned(
		&coverageView, CLASSIC_INTERACTION_BACKEND_VULKAN, 1, 1,
		3, 1, 1, 1, 1, 0 );
	const bool coverageOk = ordered && firstOwned && repeatedOwned
		&& mismatchRejected
		&& domain.views[ 0 ].backendOutcome[
			CLASSIC_INTERACTION_BACKEND_GL ]
				== CLASSIC_INTERACTION_BACKEND_OWNED
		&& domain.views[ 0 ].backendOutcome[
			CLASSIC_INTERACTION_BACKEND_VULKAN ]
				== CLASSIC_INTERACTION_BACKEND_FALLBACK
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_GL ].ownedViews == 1
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_GL ].duplicateReports == 1
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_GL ].ownedShadowMapPasses == 2
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_GL ].ownedHybridPasses == 1
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_VULKAN ].fallbackViews == 1
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_VULKAN ].coverageMismatches == 1
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_VULKAN ].fallbackShadowMapPasses == 2
		&& domain.stats.backend[
			CLASSIC_INTERACTION_BACKEND_VULKAN ].fallbackHybridPasses == 1;

	domain.stats = savedStats;
	domain.generation = savedGeneration;
	domain.viewCount = savedViewCount;
	domain.lightCount = savedLightCount;
	domain.surfaceCount = savedSurfaceCount;
	domain.primitiveCount = savedPrimitiveCount;
	domain.shadowCasterCount = savedShadowCasterCount;
	domain.shadowAlphaStageCount = savedShadowAlphaStageCount;
	domain.shadowMapPassCount = savedShadowMapPassCount;
	domain.textureCount = savedTextureCount;
	domain.views[ 0 ] = savedView;
	domain.lights[ 0 ] = savedLight;
	domain.surfaces[ 0 ] = savedSurface;
	domain.primitives[ 0 ] = savedPrimitive;
	domain.primitives[ 1 ] = savedCoveragePrimitive;
	for ( int i = 0; i < 4; ++i ) {
		domain.shadowCasters[ i ] = savedShadowCasters[ i ];
	}
	for ( int i = 0; i < 4; ++i ) {
		domain.shadowMapPasses[ i ] = savedMapPasses[ i ];
	}
	domain.textures[ 0 ] = savedTexture;
	return coverageOk;
}
