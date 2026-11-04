// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "ClassicFogBlendDomain.h"

#include <cmath>
#include <cstring>

namespace {

const std::uint64_t HASH_OFFSET = 1469598103934665603ull;
const std::uint64_t HASH_PRIME = 1099511628211ull;

typedef struct classicFogBlendDomainState_s {
	classicFogBlendDomainView_t views[ CLASSIC_FOG_BLEND_DOMAIN_MAX_VIEWS ];
	classicFogBlendDomainLight_t lights[ CLASSIC_FOG_BLEND_DOMAIN_MAX_LIGHTS ];
	classicFogBlendDomainSurface_t surfaces[
		CLASSIC_FOG_BLEND_DOMAIN_MAX_SURFACES ];
	classicFogBlendDomainLightStage_t lightStages[
		CLASSIC_FOG_BLEND_DOMAIN_MAX_LIGHT_STAGES ];
	classicFogBlendDomainPrimitive_t primitives[
		CLASSIC_FOG_BLEND_DOMAIN_MAX_PRIMITIVES ];
	classicFogBlendDomainTexture_t textures[
		CLASSIC_FOG_BLEND_DOMAIN_MAX_TEXTURES ];
	classicFogBlendDomainStats_t stats;
	std::uint32_t generation;
	int viewCount;
	int lightCount;
	int surfaceCount;
	int lightStageCount;
	int primitiveCount;
	int textureCount;
} classicFogBlendDomainState_t;

typedef struct classicFogBlendCheckpoint_s {
	int lightCount;
	int surfaceCount;
	int lightStageCount;
	int primitiveCount;
	int textureCount;
} classicFogBlendCheckpoint_t;

typedef struct classicFogBlendBuildError_s {
	classicFogBlendDomainFailure_t failure;
	int detail;
	int passPacketIndex;
	int drawPacketIndex;
	int lightOrdinal;
	int receiverOrdinal;
	int stageIndex;
} classicFogBlendBuildError_t;

classicFogBlendDomainState_t domain;

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
	for ( int i = 0; i < 4; ++i ) {
		HashByte( hash, value >> ( i * 8 ) );
	}
}

static void HashU64( std::uint64_t &hash, std::uint64_t value ) {
	for ( int i = 0; i < 8; ++i ) {
		HashByte( hash,
			static_cast<unsigned int>( value >> ( i * 8 ) ) );
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
	static_assert( sizeof( bits ) == sizeof( value ),
		"float hash width mismatch" );
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

static void SetError( classicFogBlendBuildError_t &error,
		classicFogBlendDomainFailure_t failure, int detail = 0,
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

static void InitError( classicFogBlendBuildError_t &error ) {
	SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONE );
}

static void InitTextureMatrix( float matrix[ 2 ][ 4 ] ) {
	std::memset( matrix, 0, sizeof( float ) * 8 );
	matrix[ 0 ][ 0 ] = 1.0f;
	matrix[ 1 ][ 1 ] = 1.0f;
}

static void InitBlend( rendererBlendState_t &blend,
		rendererBlendFactor_t source, rendererBlendFactor_t destination ) {
	std::memset( &blend, 0, sizeof( blend ) );
	blend.enabled = true;
	blend.sourceColor = source;
	blend.destinationColor = destination;
	blend.colorOperation = RENDERER_BLEND_OP_ADD;
	blend.sourceAlpha = source;
	blend.destinationAlpha = destination;
	blend.alphaOperation = RENDERER_BLEND_OP_ADD;
}

static void InitSurface( classicFogBlendDomainSurface_t &surface ) {
	std::memset( &surface, 0, sizeof( surface ) );
	surface.drawPacketIndex = -1;
	surface.lightIndex = -1;
	surface.sourceOrdinal = -1;
	surface.receiverOrdinal = -1;
	surface.receiver = CLASSIC_FOG_BLEND_RECEIVER_GLOBAL;
	surface.geometryRecordIndex = -1;
	surface.instanceRecordIndex = -1;
}

static void InitLightStage( classicFogBlendDomainLightStage_t &stage ) {
	std::memset( &stage, 0, sizeof( stage ) );
	stage.lightIndex = -1;
	stage.sourceStageIndex = -1;
	stage.disposition = CLASSIC_FOG_BLEND_STAGE_DRAW;
	stage.firstPrimitive = -1;
	stage.cull = RENDERER_CULL_FRONT;
	stage.colorWriteMask = RENDERER_COLOR_WRITE_RGBA;
	stage.alphaTestCompareOperation = RENDERER_COMPARE_ALWAYS;
	stage.depth.testEnabled = true;
	stage.depth.writeEnabled = false;
	stage.depth.compareOperation = RENDERER_COMPARE_EQUAL;
	InitBlend( stage.blend, RENDERER_BLEND_ONE, RENDERER_BLEND_ZERO );
	InitTextureMatrix( stage.textureMatrix );
}

static void InitLight( classicFogBlendDomainLight_t &light ) {
	std::memset( &light, 0, sizeof( light ) );
	light.sourceOrdinal = -1;
	light.kind = CLASSIC_FOG_BLEND_LIGHT_FOG;
	light.disposition = CLASSIC_FOG_BLEND_LIGHT_DRAW;
	light.firstSurface = -1;
	light.firstLightStage = -1;
	light.firstPrimitive = -1;
}

static void InitPrimitive( classicFogBlendDomainPrimitive_t &primitive ) {
	std::memset( &primitive, 0, sizeof( primitive ) );
	primitive.lightIndex = -1;
	primitive.lightStageIndex = -1;
	primitive.surfaceIndex = -1;
	primitive.kind = CLASSIC_FOG_BLEND_PRIMITIVE_FOG_RECEIVER;
	primitive.receiver = CLASSIC_FOG_BLEND_RECEIVER_GLOBAL;
	primitive.disposition = CLASSIC_FOG_BLEND_PRIMITIVE_DRAW;
	primitive.geometryRecordIndex = -1;
	primitive.instanceRecordIndex = -1;
	primitive.depth.testEnabled = true;
	primitive.depth.writeEnabled = false;
	primitive.depth.compareOperation = RENDERER_COMPARE_EQUAL;
	primitive.cull = RENDERER_CULL_FRONT;
}

static void InitView( classicFogBlendDomainView_t &view,
		const viewDef_t *viewDef, int scenePacketIndex ) {
	std::memset( &view, 0, sizeof( view ) );
	view.viewDef = viewDef;
	view.scenePacketIndex = scenePacketIndex;
	view.fogBlendPassPacketIndex = -1;
	view.firstLight = -1;
	view.firstSurface = -1;
	view.firstLightStage = -1;
	view.firstPrimitive = -1;
	view.failure = CLASSIC_FOG_BLEND_FAILURE_UNAVAILABLE;
	view.failurePassPacketIndex = -1;
	view.failureDrawPacketIndex = -1;
	view.failureLightOrdinal = -1;
	view.failureReceiverOrdinal = -1;
	view.failureStageIndex = -1;
	for ( int backend = 0; backend < CLASSIC_FOG_BLEND_BACKEND_COUNT;
			++backend ) {
		view.backendOutcome[ backend ] =
			CLASSIC_FOG_BLEND_BACKEND_UNRECORDED;
		view.backendFailure[ backend ] = CLASSIC_FOG_BLEND_FAILURE_NONE;
	}
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

static bool FailureIsOverflow( classicFogBlendDomainFailure_t failure ) {
	return failure == CLASSIC_FOG_BLEND_FAILURE_SCENE_PACKET_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_VIEW_POOL_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_LIGHT_POOL_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_SURFACE_POOL_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_LIGHT_STAGE_POOL_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_PRIMITIVE_POOL_OVERFLOW
		|| failure == CLASSIC_FOG_BLEND_FAILURE_TEXTURE_POOL_OVERFLOW;
}

static bool FailView( classicFogBlendDomainView_t &view,
		const classicFogBlendCheckpoint_t &checkpoint,
		const classicFogBlendBuildError_t &error ) {
	domain.lightCount = checkpoint.lightCount;
	domain.surfaceCount = checkpoint.surfaceCount;
	domain.lightStageCount = checkpoint.lightStageCount;
	domain.primitiveCount = checkpoint.primitiveCount;
	domain.textureCount = checkpoint.textureCount;
	view.ready = false;
	view.failure = error.failure == CLASSIC_FOG_BLEND_FAILURE_NONE
		? CLASSIC_FOG_BLEND_FAILURE_UNAVAILABLE : error.failure;
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
	view.firstLightStage = -1;
	view.lightStageCount = 0;
	view.firstPrimitive = -1;
	view.primitiveCount = 0;
	view.drawablePrimitiveCount = 0;
	view.noopPrimitiveCount = 0;
	view.fogReceiverPrimitiveCount = 0;
	view.fogFrustumPrimitiveCount = 0;
	view.blendPrimitiveCount = 0;
	view.materialDeformReceiverCount = 0;
	view.hash = 0;
	domain.stats.fallbackViews++;
	if ( view.failure >= CLASSIC_FOG_BLEND_FAILURE_NONE
			&& view.failure < CLASSIC_FOG_BLEND_FAILURE_COUNT ) {
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

static int DeformContractFailureDetail( const drawPacket_t *packet ) {
	if ( packet == NULL || !packet->hasClassicDeformRecord
			|| packet->classicDeformRecord == NULL ) {
		return -1;
	}
	return static_cast<int>( packet->classicDeformRecord->role )
		* CLASSIC_DEFORM_OUTCOME_COUNT
		+ static_cast<int>( packet->classicDeformRecord->outcome );
}

static bool ValidateReceiverDeformContract( const drawSurf_t *drawSurf,
		const drawPacket_t &packet, classicDeformRole_t &role,
		classicDeformOutcome_t &outcome, std::uint64_t &semanticHash ) {
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
	if ( !packet.hasClassicDeformRecord
			|| packet.classicDeformRecord == NULL
			|| packet.geometryRecord == NULL
			|| packet.classicDeformRecord
				!= &packet.geometryRecord->classicDeform
			|| !packet.geometryRecord->hasClassicDeformRecord ) {
		return false;
	}
	const classicDeformRecord_t &record = *packet.classicDeformRecord;
	if ( record.role != CLASSIC_DEFORM_ROLE_FOG_RECEIVER
			|| record.cpuFinalized
			|| record.outcome != CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE
			|| !R_ClassicDeformDomain_ValidateRecordForFrame( record,
				frameToken )
			|| !R_ClassicDeformDomain_RecordMatchesDrawSurf( record, drawSurf, frameToken ) ) {
		return false;
	}
	role = record.role;
	outcome = record.outcome;
	semanticHash = record.semanticHash;
	return true;
}

static std::uint64_t MakeTextureResourceId( int textureIndex ) {
	if ( domain.generation == 0 || textureIndex < 0
			|| textureIndex >= CLASSIC_FOG_BLEND_DOMAIN_MAX_TEXTURES ) {
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
	if ( encoded == 0 || encoded > CLASSIC_FOG_BLEND_DOMAIN_MAX_TEXTURES ) {
		return -1;
	}
	return static_cast<int>( encoded - 1 );
}

static bool ImageReady( const idImage *image,
		classicFogBlendBuildError_t &error, int stageIndex ) {
	if ( image == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( R_IsMutableRenderImage( image ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_DYNAMIC_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( image->IsDefaulted() ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_DEFAULTED_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( !image->IsLoaded() ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNLOADED_RESOURCE,
			0, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static bool AddTexture( const idImage *image, std::uint64_t &resourceId,
		classicFogBlendBuildError_t &error, int stageIndex ) {
	resourceId = 0;
	if ( !ImageReady( image, error, stageIndex ) ) {
		return false;
	}
	for ( int i = 0; i < domain.textureCount; ++i ) {
		if ( domain.textures[ i ].image == image
				&& domain.textures[ i ].storageGeneration
					== image->GetStorageGeneration() ) {
			resourceId = domain.textures[ i ].textureResourceId;
			return true;
		}
	}
	if ( domain.textureCount >= CLASSIC_FOG_BLEND_DOMAIN_MAX_TEXTURES ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_TEXTURE_POOL_OVERFLOW,
			domain.textureCount, -1, -1, -1, -1, stageIndex );
		return false;
	}
	const int textureIndex = domain.textureCount++;
	classicFogBlendDomainTexture_t &texture = domain.textures[ textureIndex ];
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
	resourceId = texture.textureResourceId;
	return resourceId != 0;
}

static std::uint64_t StableTextureHash( std::uint64_t resourceId ) {
	const int textureIndex = TextureIndexFromResourceId( resourceId );
	return textureIndex >= 0 && textureIndex < domain.textureCount
		? domain.textures[ textureIndex ].nameHash : 0;
}

static bool ReadRegister( const float *registers, int registerCount,
		int registerIndex, float &value,
		classicFogBlendBuildError_t &error, int stageIndex ) {
	if ( registers == NULL || registerCount <= 0 ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_SHADER_REGISTERS,
			registerCount, -1, -1, -1, -1, stageIndex );
		return false;
	}
	if ( registerIndex < 0 || registerIndex >= registerCount ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_REGISTER_OUT_OF_RANGE,
			registerIndex, -1, -1, -1, -1, stageIndex );
		return false;
	}
	value = registers[ registerIndex ];
	if ( !FloatIsFinite( value ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
			registerIndex, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static bool EvaluateStageColor( const shaderStage_t &source,
		const float *registers, int registerCount, float color[ 4 ],
		classicFogBlendBuildError_t &error, int stageIndex ) {
	for ( int component = 0; component < 4; ++component ) {
		if ( !ReadRegister( registers, registerCount,
				source.color.registers[ component ], color[ component ], error,
				stageIndex ) ) {
			return false;
		}
	}
	return true;
}

static bool EvaluateTextureMatrix( const textureStage_t &texture,
		const float *registers, int registerCount, float matrix[ 2 ][ 4 ],
		classicFogBlendBuildError_t &error, int stageIndex ) {
	InitTextureMatrix( matrix );
	if ( !texture.hasMatrix ) {
		return true;
	}
	for ( int row = 0; row < 2; ++row ) {
		float values[ 3 ];
		for ( int column = 0; column < 3; ++column ) {
			if ( !ReadRegister( registers, registerCount,
					texture.matrix[ row ][ column ], values[ column ], error,
					stageIndex ) ) {
				return false;
			}
		}
		float translation = values[ 2 ];
		if ( translation < -40.0f || translation > 40.0f ) {
			if ( static_cast<double>( translation ) < -2147483648.0
					|| static_cast<double>( translation ) > 2147483647.0 ) {
				SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
					texture.matrix[ row ][ 2 ], -1, -1, -1, -1,
					stageIndex );
				return false;
			}
			translation -= static_cast<int>( translation );
		}
		matrix[ row ][ 0 ] = values[ 0 ];
		matrix[ row ][ 1 ] = values[ 1 ];
		matrix[ row ][ 2 ] = 0.0f;
		matrix[ row ][ 3 ] = translation;
	}
	return true;
}

static bool MapSourceBlendFactor( int drawStateBits,
		rendererBlendFactor_t &factor ) {
	switch ( drawStateBits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO: factor = RENDERER_BLEND_ZERO; return true;
	case GLS_SRCBLEND_ONE: factor = RENDERER_BLEND_ONE; return true;
	case GLS_SRCBLEND_DST_COLOR: factor = RENDERER_BLEND_DST_COLOR; return true;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_DST_COLOR; return true;
	case GLS_SRCBLEND_SRC_ALPHA: factor = RENDERER_BLEND_SRC_ALPHA; return true;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA; return true;
	case GLS_SRCBLEND_DST_ALPHA: factor = RENDERER_BLEND_DST_ALPHA; return true;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_DST_ALPHA; return true;
	case GLS_SRCBLEND_ALPHA_SATURATE:
		factor = RENDERER_BLEND_SRC_ALPHA_SATURATE; return true;
	case GLS_SRCBLEND_SRC_COLOR: factor = RENDERER_BLEND_SRC_COLOR; return true;
	case GLS_SRCBLEND_ONE_MINUS_SRC_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_COLOR; return true;
	default: return false;
	}
}

static bool MapDestinationBlendFactor( int drawStateBits,
		rendererBlendFactor_t &factor ) {
	switch ( drawStateBits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ZERO: factor = RENDERER_BLEND_ZERO; return true;
	case GLS_DSTBLEND_ONE: factor = RENDERER_BLEND_ONE; return true;
	case GLS_DSTBLEND_SRC_COLOR: factor = RENDERER_BLEND_SRC_COLOR; return true;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_COLOR; return true;
	case GLS_DSTBLEND_SRC_ALPHA: factor = RENDERER_BLEND_SRC_ALPHA; return true;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA; return true;
	case GLS_DSTBLEND_DST_ALPHA: factor = RENDERER_BLEND_DST_ALPHA; return true;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_DST_ALPHA; return true;
	default: return false;
	}
}

static std::uint32_t ColorWriteMask( int drawStateBits ) {
	std::uint32_t mask = RENDERER_COLOR_WRITE_RGBA;
	if ( ( drawStateBits & GLS_REDMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_RED );
	}
	if ( ( drawStateBits & GLS_GREENMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_GREEN );
	}
	if ( ( drawStateBits & GLS_BLUEMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_BLUE );
	}
	if ( ( drawStateBits & GLS_ALPHAMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_ALPHA );
	}
	return mask;
}

static bool SealBlendRenderState( int sourceBits,
		classicFogBlendDomainLightStage_t &stage,
		classicFogBlendBuildError_t &error, int stageIndex ) {
	const int supportedStateBits = GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
		| GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK
		| GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL | GLS_ATEST_BITS;
	if ( ( sourceBits & ~supportedStateBits ) != 0
			|| !MapSourceBlendFactor( sourceBits, stage.blend.sourceColor )
			|| !MapDestinationBlendFactor(
				sourceBits, stage.blend.destinationColor ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_RENDER_STATE,
			sourceBits, -1, -1, -1, -1, stageIndex );
		return false;
	}
	stage.blend.enabled = ( sourceBits
		& ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) != 0;
	stage.blend.sourceAlpha = stage.blend.sourceColor;
	stage.blend.destinationAlpha = stage.blend.destinationColor;
	stage.blend.colorOperation = RENDERER_BLEND_OP_ADD;
	stage.blend.alphaOperation = RENDERER_BLEND_OP_ADD;
	stage.depth.testEnabled = true;
	stage.depth.writeEnabled = false;
	// RB_BlendLight ORs GLS_DEPTHFUNC_EQUAL after authored state. GL_State gives
	// EQUAL precedence if an authored ALWAYS bit is also present.
	stage.depth.compareOperation = RENDERER_COMPARE_EQUAL;
	stage.cull = RENDERER_CULL_FRONT;
	stage.colorWriteMask = ColorWriteMask( sourceBits );
	stage.alphaTestEnabled = false;
	stage.alphaTestCompareOperation = RENDERER_COMPARE_ALWAYS;
	stage.alphaTestValue = 0.0f;
	switch ( sourceBits & GLS_ATEST_BITS ) {
	case 0:
		break;
	case GLS_ATEST_EQ_255:
		stage.alphaTestEnabled = true;
		stage.alphaTestCompareOperation = RENDERER_COMPARE_EQUAL;
		stage.alphaTestValue = 1.0f;
		break;
	case GLS_ATEST_LT_128:
		stage.alphaTestEnabled = true;
		stage.alphaTestCompareOperation = RENDERER_COMPARE_LESS;
		stage.alphaTestValue = 0.5f;
		break;
	case GLS_ATEST_GE_128:
		stage.alphaTestEnabled = true;
		stage.alphaTestCompareOperation = RENDERER_COMPARE_GREATER_OR_EQUAL;
		stage.alphaTestValue = 0.5f;
		break;
	default:
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_RENDER_STATE,
			sourceBits, -1, -1, -1, -1, stageIndex );
		return false;
	}
	return true;
}

static std::uint64_t HashSurface(
		const classicFogBlendDomainSurface_t &surface, int lightArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, surface.lightIndex - lightArenaBase );
	HashInt( hash, surface.sourceOrdinal );
	HashInt( hash, surface.receiverOrdinal );
	HashInt( hash, surface.receiver );
	HashInt( hash, surface.vertexCount );
	HashInt( hash, surface.firstIndex );
	HashInt( hash, surface.indexCount );
	HashInt( hash, surface.vertexOffset );
	HashInt( hash, surface.scissorX1 );
	HashInt( hash, surface.scissorY1 );
	HashInt( hash, surface.scissorX2 );
	HashInt( hash, surface.scissorY2 );
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, surface.modelMatrix[ i ] );
		HashFloat( hash, surface.modelViewMatrix[ i ] );
	}
	HashBool( hash, surface.hasAmbientCache );
	HashBool( hash, surface.hasIndexCache );
	HashInt( hash, surface.deformRole );
	HashInt( hash, surface.deformOutcome );
	HashU64( hash, surface.deformContractHash );
	return hash;
}

static std::uint64_t HashPrimitive(
		const classicFogBlendDomainPrimitive_t &primitive,
		int lightArenaBase, int stageArenaBase, int surfaceArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, primitive.lightIndex - lightArenaBase );
	HashInt( hash, primitive.lightStageIndex - stageArenaBase );
	HashInt( hash, primitive.surfaceIndex >= 0
		? primitive.surfaceIndex - surfaceArenaBase : -1 );
	HashInt( hash, primitive.kind );
	HashInt( hash, primitive.receiver );
	HashInt( hash, primitive.disposition );
	HashInt( hash, primitive.vertexCount );
	HashInt( hash, primitive.firstIndex );
	HashInt( hash, primitive.indexCount );
	HashInt( hash, primitive.vertexOffset );
	HashInt( hash, primitive.scissorX1 );
	HashInt( hash, primitive.scissorY1 );
	HashInt( hash, primitive.scissorX2 );
	HashInt( hash, primitive.scissorY2 );
	HashBool( hash, primitive.depth.testEnabled );
	HashBool( hash, primitive.depth.writeEnabled );
	HashInt( hash, primitive.depth.compareOperation );
	HashInt( hash, primitive.cull );
	for ( int i = 0; i < 16; ++i ) {
		HashFloat( hash, primitive.modelMatrix[ i ] );
		HashFloat( hash, primitive.modelViewMatrix[ i ] );
	}
	for ( int plane = 0; plane < 4; ++plane ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash,
				primitive.localLightProject[ plane ][ component ] );
		}
	}
	for ( int unit = 0; unit < 2; ++unit ) {
		for ( int coordinate = 0; coordinate < 2; ++coordinate ) {
			for ( int component = 0; component < 4; ++component ) {
				HashFloat( hash,
					primitive.fogTexgen[ unit ][ coordinate ][ component ] );
			}
		}
	}
	return hash;
}

static std::uint64_t HashLightStage(
		const classicFogBlendDomainLightStage_t &stage, int lightArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, stage.lightIndex - lightArenaBase );
	HashInt( hash, stage.sourceStageIndex );
	HashInt( hash, stage.disposition );
	HashInt( hash, stage.primitiveCount );
	HashInt( hash, stage.drawablePrimitiveCount );
	HashInt( hash, stage.noopPrimitiveCount );
	HashFloat( hash, stage.condition );
	for ( int component = 0; component < 4; ++component ) {
		HashFloat( hash, stage.color[ component ] );
	}
	for ( int row = 0; row < 2; ++row ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, stage.textureMatrix[ row ][ component ] );
		}
	}
	HashU64( hash, StableTextureHash( stage.projectionTextureResourceId ) );
	HashU64( hash, StableTextureHash( stage.falloffTextureResourceId ) );
	HashU64( hash, StableTextureHash( stage.fogTextureResourceId ) );
	HashU64( hash, StableTextureHash( stage.fogEnterTextureResourceId ) );
	HashBool( hash, stage.blend.enabled );
	HashInt( hash, stage.blend.sourceColor );
	HashInt( hash, stage.blend.destinationColor );
	HashInt( hash, stage.blend.colorOperation );
	HashInt( hash, stage.blend.sourceAlpha );
	HashInt( hash, stage.blend.destinationAlpha );
	HashInt( hash, stage.blend.alphaOperation );
	HashBool( hash, stage.depth.testEnabled );
	HashBool( hash, stage.depth.writeEnabled );
	HashInt( hash, stage.depth.compareOperation );
	HashInt( hash, stage.cull );
	HashU32( hash, stage.colorWriteMask );
	HashBool( hash, stage.alphaTestEnabled );
	HashInt( hash, stage.alphaTestCompareOperation );
	HashFloat( hash, stage.alphaTestValue );
	HashBool( hash, stage.hasTextureMatrix );
	HashBool( hash, stage.conditionIgnored );
	return hash;
}

static std::uint64_t HashLight( const classicFogBlendDomainLight_t &light,
		int lightArenaBase, int stageArenaBase, int surfaceArenaBase ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, light.sourceOrdinal );
	HashInt( hash, light.kind );
	HashInt( hash, light.disposition );
	HashInt( hash, light.surfaceCount );
	for ( int receiver = 0; receiver < CLASSIC_FOG_BLEND_RECEIVER_COUNT;
			++receiver ) {
		HashInt( hash, light.receiverSurfaceCount[ receiver ] );
	}
	HashInt( hash, light.lightStageCount );
	HashInt( hash, light.activeLightStageCount );
	HashInt( hash, light.inactiveLightStageCount );
	HashInt( hash, light.noopLightStageCount );
	HashInt( hash, light.primitiveCount );
	HashInt( hash, light.drawablePrimitiveCount );
	HashInt( hash, light.noopPrimitiveCount );
	HashInt( hash, light.fogReceiverPrimitiveCount );
	HashInt( hash, light.fogFrustumPrimitiveCount );
	HashInt( hash, light.blendPrimitiveCount );
	HashInt( hash, light.scissorX1 );
	HashInt( hash, light.scissorY1 );
	HashInt( hash, light.scissorX2 );
	HashInt( hash, light.scissorY2 );
	for ( int plane = 0; plane < 4; ++plane ) {
		for ( int component = 0; component < 4; ++component ) {
			HashFloat( hash, light.lightProject[ plane ][ component ] );
		}
	}
	for ( int component = 0; component < 4; ++component ) {
		HashFloat( hash, light.fogPlane[ component ] );
		HashFloat( hash, light.fogColor[ component ] );
	}
	for ( int unit = 0; unit < 2; ++unit ) {
		for ( int coordinate = 0; coordinate < 2; ++coordinate ) {
			for ( int component = 0; component < 4; ++component ) {
				HashFloat( hash,
					light.fogGlobalTexgen[ unit ][ coordinate ][ component ] );
			}
		}
	}
	HashFloat( hash, light.fogDensity );
	HashFloat( hash, light.fogDistanceScale );
	HashU64( hash, StableTextureHash( light.falloffTextureResourceId ) );
	HashU64( hash, StableTextureHash( light.fogTextureResourceId ) );
	HashU64( hash, StableTextureHash( light.fogEnterTextureResourceId ) );
	HashBool( hash, light.globalChainPresent );
	HashInt( hash, light.materialDeformReceiverCount );
	HashU64( hash, light.deformContractHash );
	for ( int i = 0; i < light.surfaceCount; ++i ) {
		HashU64( hash, domain.surfaces[ light.firstSurface + i ].hash );
	}
	for ( int i = 0; i < light.lightStageCount; ++i ) {
		HashU64( hash, domain.lightStages[ light.firstLightStage + i ].hash );
	}
	for ( int i = 0; i < light.primitiveCount; ++i ) {
		const classicFogBlendDomainPrimitive_t &primitive =
			domain.primitives[ light.firstPrimitive + i ];
		// Recompute with view-local identities in case a caller constructed the
		// record before every arena base was known.
		HashU64( hash, HashPrimitive( primitive, lightArenaBase,
			stageArenaBase, surfaceArenaBase ) );
	}
	return hash;
}

static std::uint64_t HashView( const classicFogBlendDomainView_t &view ) {
	std::uint64_t hash = HASH_OFFSET;
	HashInt( hash, view.lightCount );
	HashInt( hash, view.fogLightCount );
	HashInt( hash, view.blendLightCount );
	HashInt( hash, view.noopLightCount );
	HashInt( hash, view.surfaceCount );
	HashInt( hash, view.lightStageCount );
	HashInt( hash, view.activeLightStageCount );
	HashInt( hash, view.inactiveLightStageCount );
	HashInt( hash, view.noopLightStageCount );
	HashInt( hash, view.primitiveCount );
	HashInt( hash, view.drawablePrimitiveCount );
	HashInt( hash, view.noopPrimitiveCount );
	HashInt( hash, view.fogReceiverPrimitiveCount );
	HashInt( hash, view.fogFrustumPrimitiveCount );
	HashInt( hash, view.blendPrimitiveCount );
	HashInt( hash, view.packetDrawCount );
	HashInt( hash, view.materialDeformReceiverCount );
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
	HashBool( hash, view.useScissor );
	HashBool( hash, view.skipBlendLights );
	for ( int i = 0; i < view.lightCount; ++i ) {
		HashU64( hash, domain.lights[ view.firstLight + i ].hash );
	}
	return hash;
}

static bool SceneIsFogBlendCandidate( const idScenePacketFrame &packetFrame,
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
				== RENDER_PASS_FOG_BLEND ) {
			return true;
		}
	}
	return false;
}

static classicFogBlendDomainView_t *FindMutableView(
		const viewDef_t *viewDef ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].viewDef == viewDef ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

static int ViewIndex( const classicFogBlendDomainView_t *view ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( view == &domain.views[ i ] ) {
			return i;
		}
	}
	return -1;
}

static int LightIndex( const classicFogBlendDomainLight_t *light ) {
	for ( int i = 0; i < domain.lightCount; ++i ) {
		if ( light == &domain.lights[ i ] ) {
			return i;
		}
	}
	return -1;
}

static bool ViewLightIsFogOrBlend( const viewLight_t *viewLight ) {
	return viewLight != NULL && viewLight->lightShader != NULL
		&& ( viewLight->lightShader->IsFogLight()
			|| viewLight->lightShader->IsBlendLight() );
}

static bool ValidateDrawPacketIdentity( const drawPacket_t &packet,
		int packetIndex,
		const viewDef_t *viewDef, const viewLight_t *viewLight,
		int lightOrdinal, sceneFogBlendReceiverClass_t receiverClass,
		int receiverOrdinal, int sourceOrdinal, const drawSurf_t *drawSurf,
		classicFogBlendBuildError_t &error ) {
	if ( packet.passCategory != RENDER_PASS_FOG_BLEND
			|| packet.packetCategory != SCENE_PACKET_CATEGORY_WORLD
			|| packet.viewDef != viewDef || packet.legacyDrawSurf != drawSurf
			|| packet.fogBlendLight != viewLight
			|| packet.fogBlendLightOrdinal != lightOrdinal
			|| packet.fogBlendReceiverClass != receiverClass
			|| packet.fogBlendReceiverOrdinal != receiverOrdinal
			|| packet.fogBlendSourceOrdinal != sourceOrdinal ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SOURCE_PACKET_MISMATCH,
			sourceOrdinal, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	return true;
}

static bool ValidateDrawPacket( const idScenePacketFrame &packetFrame,
		const drawPacket_t &packet, int packetIndex,
		const viewDef_t *viewDef, const viewLight_t *viewLight,
		int lightOrdinal, sceneFogBlendReceiverClass_t receiverClass,
		int receiverOrdinal, int sourceOrdinal, const drawSurf_t *drawSurf,
		classicFogBlendBuildError_t &error ) {
	if ( !ValidateDrawPacketIdentity( packet, packetIndex, viewDef, viewLight,
			lightOrdinal, receiverClass, receiverOrdinal, sourceOrdinal,
			drawSurf, error ) ) {
		return false;
	}
	if ( drawSurf == NULL || drawSurf->geo == NULL || drawSurf->space == NULL
			|| packet.geometryRecordIndex < 0
			|| packet.geometryRecordIndex >= packetFrame.NumGeometryRecords()
			|| packet.geometryRecord == NULL
			|| packet.geometryRecord
				!= &packetFrame.GeometryRecord( packet.geometryRecordIndex )
			|| packet.geometryRecord->legacyGeometry != drawSurf->geo ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_GEOMETRY_RECORD,
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
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_INSTANCE_RECORD,
			packet.instanceRecordIndex, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}

	const geometryResourceRecord_t &geometry = *packet.geometryRecord;
	const instanceRecord_t &instance = *packet.instanceRecord;
	bool admittedMaterialDeform = false;
	if ( MaterialRequestsClassicDeform( drawSurf )
			&& r_rendererSharedDeform.GetBool() ) {
		classicDeformRole_t role;
		classicDeformOutcome_t outcome;
		std::uint64_t semanticHash = 0;
		if ( !ValidateReceiverDeformContract( drawSurf, packet, role,
				outcome, semanticHash )
				|| role != CLASSIC_DEFORM_ROLE_FOG_RECEIVER
				|| outcome != CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE
				|| semanticHash == 0 ) {
			SetError( error, CLASSIC_FOG_BLEND_FAILURE_DEFORM_CONTRACT,
				DeformContractFailureDetail( &packet ), -1, packetIndex,
				lightOrdinal, receiverOrdinal );
			return false;
		}
		admittedMaterialDeform = true;
	}
	if ( ( geometry.deformMode != GEOMETRY_DEFORM_NONE
				&& !admittedMaterialDeform )
			|| drawSurf->geo->deformedSurface ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_DEFORM,
			geometry.deformMode, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( geometry.skinningMode != GEOMETRY_SKINNING_NONE
			&& geometry.skinningMode != GEOMETRY_SKINNING_CPU ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SKINNING,
			geometry.skinningMode, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	if ( geometry.hasPrimBatchMesh ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SPECIAL_SURFACE,
			geometry.hasPrimBatchMesh ? 1 : 0, -1, packetIndex,
			lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( instance.weaponDepthHack || instance.modelDepthHack != 0.0f ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_DEPTH_HACK,
			instance.weaponDepthHack ? 1 : 2, -1, packetIndex,
			lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( instance.negativeScale ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_NEGATIVE_SCALE,
			1, -1, packetIndex, lightOrdinal, receiverOrdinal );
		return false;
	}
	if ( !packet.hasGeometry || !packet.hasAmbientCache
			|| geometry.vertexCount <= 0 || geometry.indexCount <= 0
			|| geometry.indexCount % 3 != 0
			|| packet.vertexCount != geometry.vertexCount
			|| packet.indexCount != geometry.indexCount
			|| ( !packet.hasIndexCache && !geometry.hasClientIndexData )
			|| !FloatsAreFinite( instance.modelMatrix, 16 )
			|| !FloatsAreFinite( instance.modelViewMatrix, 16 ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_INVALID_DRAW_PACKET,
			geometry.indexCount, -1, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	return true;
}

static bool AppendSurface( const idScenePacketFrame &packetFrame,
		const classicFogBlendDomainView_t &view,
		classicFogBlendDomainLight_t &light, int absoluteLightIndex,
		const viewLight_t *viewLight, const drawSurf_t *drawSurf,
		int lightOrdinal, classicFogBlendDomainReceiver_t receiver,
		int receiverOrdinal, int sourceOrdinal, int passPacketIndex,
		int packetIndex, int lightArenaBase,
		classicFogBlendBuildError_t &error ) {
	const sceneFogBlendReceiverClass_t packetReceiver =
		receiver == CLASSIC_FOG_BLEND_RECEIVER_GLOBAL
			? SCENE_FOG_BLEND_RECEIVER_GLOBAL
			: SCENE_FOG_BLEND_RECEIVER_LOCAL;
	const drawPacket_t &packet = packetFrame.DrawPacket( packetIndex );
	if ( !ValidateDrawPacket( packetFrame, packet, packetIndex, view.viewDef,
			viewLight, lightOrdinal, packetReceiver, receiverOrdinal,
			sourceOrdinal, drawSurf, error ) ) {
		error.passPacketIndex = passPacketIndex;
		return false;
	}
	if ( domain.surfaceCount >= CLASSIC_FOG_BLEND_DOMAIN_MAX_SURFACES ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SURFACE_POOL_OVERFLOW,
			domain.surfaceCount, passPacketIndex, packetIndex, lightOrdinal,
			receiverOrdinal );
		return false;
	}
	classicFogBlendDomainSurface_t &surface =
		domain.surfaces[ domain.surfaceCount++ ];
	InitSurface( surface );
	surface.legacyDrawSurf = drawSurf;
	surface.legacyGeometry = drawSurf->geo;
	surface.drawPacketIndex = packetIndex;
	surface.lightIndex = absoluteLightIndex;
	surface.sourceOrdinal = sourceOrdinal;
	surface.receiverOrdinal = receiverOrdinal;
	surface.receiver = receiver;
	surface.geometryRecordIndex = packet.geometryRecordIndex;
	surface.instanceRecordIndex = packet.instanceRecordIndex;
	surface.vertexCount = packet.vertexCount;
	surface.firstIndex = packet.firstIndex;
	surface.indexCount = packet.indexCount;
	surface.vertexOffset = packet.vertexOffset;
	surface.scissorX1 = packet.scissorX1;
	surface.scissorY1 = packet.scissorY1;
	surface.scissorX2 = packet.scissorX2;
	surface.scissorY2 = packet.scissorY2;
	std::memcpy( surface.modelMatrix, packet.instanceRecord->modelMatrix,
		sizeof( surface.modelMatrix ) );
	std::memcpy( surface.modelViewMatrix,
		packet.instanceRecord->modelViewMatrix,
		sizeof( surface.modelViewMatrix ) );
	surface.hasAmbientCache = packet.hasAmbientCache;
	surface.hasIndexCache = packet.hasIndexCache;
	if ( MaterialRequestsClassicDeform( drawSurf ) ) {
		surface.deformRole = packet.classicDeformRecord->role;
		surface.deformOutcome = packet.classicDeformRecord->outcome;
		surface.deformContractHash = packet.classicDeformRecord->semanticHash;
		if ( light.materialDeformReceiverCount == 0 ) {
			light.deformContractHash = HASH_OFFSET;
		}
		light.materialDeformReceiverCount++;
		HashU64( light.deformContractHash, surface.deformContractHash );
	}
	surface.hash = HashSurface( surface, lightArenaBase );
	light.surfaceCount++;
	light.receiverSurfaceCount[ receiver ]++;
	return true;
}

static void LocalizePlane( const float modelMatrix[ 16 ],
		const float globalPlane[ 4 ], float localPlane[ 4 ] ) {
	idPlane source;
	idPlane destination;
	for ( int component = 0; component < 4; ++component ) {
		source[ component ] = globalPlane[ component ];
	}
	R_GlobalPlaneToLocal( modelMatrix, source, destination );
	std::memcpy( localPlane, destination.ToFloatPtr(), sizeof( float ) * 4 );
}

static void CopySurfaceToPrimitive(
		const classicFogBlendDomainSurface_t &surface,
		classicFogBlendDomainPrimitive_t &primitive ) {
	primitive.legacyDrawSurf = surface.legacyDrawSurf;
	primitive.legacyGeometry = surface.legacyGeometry;
	primitive.geometryRecordIndex = surface.geometryRecordIndex;
	primitive.instanceRecordIndex = surface.instanceRecordIndex;
	primitive.vertexCount = surface.vertexCount;
	primitive.firstIndex = surface.firstIndex;
	primitive.indexCount = surface.indexCount;
	primitive.vertexOffset = surface.vertexOffset;
	primitive.scissorX1 = surface.scissorX1;
	primitive.scissorY1 = surface.scissorY1;
	primitive.scissorX2 = surface.scissorX2;
	primitive.scissorY2 = surface.scissorY2;
	std::memcpy( primitive.modelMatrix, surface.modelMatrix,
		sizeof( primitive.modelMatrix ) );
	std::memcpy( primitive.modelViewMatrix, surface.modelViewMatrix,
		sizeof( primitive.modelViewMatrix ) );
}

static bool AppendPrimitive( classicFogBlendDomainLight_t &light,
		classicFogBlendDomainLightStage_t &stage,
		classicFogBlendDomainPrimitive_t &source, int lightArenaBase,
		int stageArenaBase, int surfaceArenaBase,
		classicFogBlendBuildError_t &error ) {
	if ( domain.primitiveCount >= CLASSIC_FOG_BLEND_DOMAIN_MAX_PRIMITIVES ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_PRIMITIVE_POOL_OVERFLOW,
			domain.primitiveCount, -1, -1, light.sourceOrdinal,
			source.receiver, stage.sourceStageIndex );
		return false;
	}
	source.hash = HashPrimitive( source, lightArenaBase, stageArenaBase,
		surfaceArenaBase );
	domain.primitives[ domain.primitiveCount++ ] = source;
	light.primitiveCount++;
	stage.primitiveCount++;
	if ( source.disposition == CLASSIC_FOG_BLEND_PRIMITIVE_DRAW ) {
		light.drawablePrimitiveCount++;
		stage.drawablePrimitiveCount++;
	} else {
		light.noopPrimitiveCount++;
		stage.noopPrimitiveCount++;
	}
	return true;
}

static void SealFogTexgen( const classicFogBlendDomainLight_t &light,
		const float modelMatrix[ 16 ], float texgen[ 2 ][ 2 ][ 4 ] ) {
	LocalizePlane( modelMatrix, light.fogGlobalTexgen[ 0 ][ 0 ],
		texgen[ 0 ][ 0 ] );
	texgen[ 0 ][ 0 ][ 3 ] += 0.5f;
	std::memset( texgen[ 0 ][ 1 ], 0, sizeof( texgen[ 0 ][ 1 ] ) );
	texgen[ 0 ][ 1 ][ 3 ] = 0.5f;
	LocalizePlane( modelMatrix, light.fogGlobalTexgen[ 1 ][ 0 ],
		texgen[ 1 ][ 0 ] );
	LocalizePlane( modelMatrix, light.fogGlobalTexgen[ 1 ][ 1 ],
		texgen[ 1 ][ 1 ] );
	texgen[ 1 ][ 1 ][ 3 ] += FOG_ENTER;
}

static bool AppendFogReceiverPrimitive(
		classicFogBlendDomainLight_t &light,
		classicFogBlendDomainLightStage_t &stage, int absoluteLightIndex,
		int absoluteStageIndex, int absoluteSurfaceIndex, int lightArenaBase,
		int stageArenaBase, int surfaceArenaBase,
		classicFogBlendBuildError_t &error ) {
	const classicFogBlendDomainSurface_t &surface =
		domain.surfaces[ absoluteSurfaceIndex ];
	classicFogBlendDomainPrimitive_t primitive;
	InitPrimitive( primitive );
	CopySurfaceToPrimitive( surface, primitive );
	primitive.lightIndex = absoluteLightIndex;
	primitive.lightStageIndex = absoluteStageIndex;
	primitive.surfaceIndex = absoluteSurfaceIndex;
	primitive.kind = CLASSIC_FOG_BLEND_PRIMITIVE_FOG_RECEIVER;
	primitive.receiver = surface.receiver;
	primitive.disposition = CLASSIC_FOG_BLEND_PRIMITIVE_DRAW;
	primitive.depth = stage.depth;
	primitive.cull = stage.cull;
	SealFogTexgen( light, primitive.modelMatrix, primitive.fogTexgen );
	if ( !AppendPrimitive( light, stage, primitive, lightArenaBase,
			stageArenaBase, surfaceArenaBase, error ) ) {
		return false;
	}
	light.fogReceiverPrimitiveCount++;
	return true;
}

static bool AppendBlendPrimitive(
		classicFogBlendDomainLight_t &light,
		classicFogBlendDomainLightStage_t &stage, int absoluteLightIndex,
		int absoluteStageIndex, int absoluteSurfaceIndex,
		classicFogBlendDomainPrimitiveDisposition_t disposition,
		int lightArenaBase, int stageArenaBase, int surfaceArenaBase,
		classicFogBlendBuildError_t &error ) {
	const classicFogBlendDomainSurface_t &surface =
		domain.surfaces[ absoluteSurfaceIndex ];
	classicFogBlendDomainPrimitive_t primitive;
	InitPrimitive( primitive );
	CopySurfaceToPrimitive( surface, primitive );
	primitive.lightIndex = absoluteLightIndex;
	primitive.lightStageIndex = absoluteStageIndex;
	primitive.surfaceIndex = absoluteSurfaceIndex;
	primitive.kind = CLASSIC_FOG_BLEND_PRIMITIVE_BLEND_RECEIVER;
	primitive.receiver = surface.receiver;
	primitive.disposition = disposition;
	primitive.depth = stage.depth;
	primitive.cull = stage.cull;
	for ( int plane = 0; plane < 4; ++plane ) {
		LocalizePlane( primitive.modelMatrix, light.lightProject[ plane ],
			primitive.localLightProject[ plane ] );
	}
	if ( !AppendPrimitive( light, stage, primitive, lightArenaBase,
			stageArenaBase, surfaceArenaBase, error ) ) {
		return false;
	}
	if ( disposition == CLASSIC_FOG_BLEND_PRIMITIVE_DRAW ) {
		light.blendPrimitiveCount++;
	}
	return true;
}

static bool PrepareFogLight( const classicFogBlendDomainView_t &view,
		classicFogBlendDomainLight_t &light, int absoluteLightIndex,
		const viewLight_t &viewLight, int lightArenaBase, int stageArenaBase,
		int surfaceArenaBase, classicFogBlendBuildError_t &error ) {
	if ( viewLight.lightShader == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_SHADER,
			0, -1, -1, light.sourceOrdinal );
		return false;
	}
	const int registerCount = viewLight.lightShader->GetNumRegisters();
	if ( viewLight.shaderRegisters == NULL || registerCount <= 0 ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_SHADER_REGISTERS,
			registerCount, -1, -1, light.sourceOrdinal );
		return false;
	}
	if ( viewLight.lightShader->GetNumStages() <= 0
			|| viewLight.lightShader->GetStage( 0 ) == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_STAGE,
			0, -1, -1, light.sourceOrdinal, -1, 0 );
		return false;
	}
	const srfTriangles_t *frustum = viewLight.frustumTris;
	if ( frustum == NULL || frustum->ambientCache == NULL
			|| frustum->numVerts <= 0 || frustum->numIndexes <= 0
			|| frustum->numIndexes % 3 != 0
			|| ( frustum->indexCache == NULL && frustum->indexes == NULL )
			|| frustum->deformedSurface || R_TriHasPrimBatchMesh( frustum )
			|| !FloatsAreFinite( frustum->bounds[ 0 ].ToFloatPtr(), 3 )
			|| !FloatsAreFinite( frustum->bounds[ 1 ].ToFloatPtr(), 3 ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_FOG_FRUSTUM_GEOMETRY,
			frustum != NULL ? frustum->numIndexes : 0, -1, -1,
			light.sourceOrdinal );
		return false;
	}
	if ( globalImages == NULL || globalImages->fogImage == NULL
			|| globalImages->fogEnterImage == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_RESOURCE,
			0, -1, -1, light.sourceOrdinal, -1, 0 );
		return false;
	}
	if ( domain.lightStageCount
			>= CLASSIC_FOG_BLEND_DOMAIN_MAX_LIGHT_STAGES ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_LIGHT_STAGE_POOL_OVERFLOW,
			domain.lightStageCount, -1, -1, light.sourceOrdinal, -1, 0 );
		return false;
	}

	const int absoluteStageIndex = domain.lightStageCount++;
	classicFogBlendDomainLightStage_t &stage =
		domain.lightStages[ absoluteStageIndex ];
	InitLightStage( stage );
	stage.lightIndex = absoluteLightIndex;
	stage.sourceStageIndex = 0;
	stage.disposition = CLASSIC_FOG_BLEND_STAGE_DRAW;
	stage.firstPrimitive = domain.primitiveCount;
	stage.condition = 1.0f;
	stage.conditionIgnored = true; // RB_FogPass never tests stage 0 condition.
	InitBlend( stage.blend, RENDERER_BLEND_SRC_ALPHA,
		RENDERER_BLEND_ONE_MINUS_SRC_ALPHA );
	stage.depth.testEnabled = true;
	stage.depth.writeEnabled = false;
	stage.depth.compareOperation = RENDERER_COMPARE_EQUAL;
	stage.cull = RENDERER_CULL_FRONT;
	stage.colorWriteMask = RENDERER_COLOR_WRITE_RGBA;
	stage.alphaTestEnabled = false;
	stage.alphaTestCompareOperation = RENDERER_COMPARE_ALWAYS;
	const shaderStage_t &sourceStage = *viewLight.lightShader->GetStage( 0 );
	if ( !EvaluateStageColor( sourceStage, viewLight.shaderRegisters,
			registerCount, stage.color, error, 0 )
			|| !AddTexture( globalImages->fogImage,
				stage.fogTextureResourceId, error, 0 )
			|| !AddTexture( globalImages->fogEnterImage,
				stage.fogEnterTextureResourceId, error, 0 ) ) {
		error.lightOrdinal = light.sourceOrdinal;
		return false;
	}
	light.fogTextureResourceId = stage.fogTextureResourceId;
	light.fogEnterTextureResourceId = stage.fogEnterTextureResourceId;
	for ( int component = 0; component < 3; ++component ) {
		light.fogColor[ component ] = stage.color[ component ];
	}
	light.fogColor[ 3 ] = 1.0f; // glColor3fv leaves fog draw alpha at one.
	light.fogDensity = stage.color[ 3 ];
	light.fogDistanceScale = light.fogDensity <= 1.0f
		? -0.5f / DEFAULT_FOG_DISTANCE : -0.5f / light.fogDensity;
	if ( !FloatIsFinite( light.fogDistanceScale ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
			3, -1, -1, light.sourceOrdinal, -1, 0 );
		return false;
	}
	const float *worldModelView = view.viewDef->worldSpace.modelViewMatrix;
	light.fogGlobalTexgen[ 0 ][ 0 ][ 0 ] =
		light.fogDistanceScale * worldModelView[ 2 ];
	light.fogGlobalTexgen[ 0 ][ 0 ][ 1 ] =
		light.fogDistanceScale * worldModelView[ 6 ];
	light.fogGlobalTexgen[ 0 ][ 0 ][ 2 ] =
		light.fogDistanceScale * worldModelView[ 10 ];
	light.fogGlobalTexgen[ 0 ][ 0 ][ 3 ] =
		light.fogDistanceScale * worldModelView[ 14 ];
	light.fogGlobalTexgen[ 0 ][ 1 ][ 0 ] =
		light.fogDistanceScale * worldModelView[ 0 ];
	light.fogGlobalTexgen[ 0 ][ 1 ][ 1 ] =
		light.fogDistanceScale * worldModelView[ 4 ];
	light.fogGlobalTexgen[ 0 ][ 1 ][ 2 ] =
		light.fogDistanceScale * worldModelView[ 8 ];
	light.fogGlobalTexgen[ 0 ][ 1 ][ 3 ] =
		light.fogDistanceScale * worldModelView[ 12 ];
	for ( int component = 0; component < 4; ++component ) {
		light.fogGlobalTexgen[ 1 ][ 1 ][ component ] =
			0.001f * light.fogPlane[ component ];
	}
	const float viewerDistance =
		view.viewDef->renderView.vieworg[ 0 ]
			* light.fogGlobalTexgen[ 1 ][ 1 ][ 0 ]
		+ view.viewDef->renderView.vieworg[ 1 ]
			* light.fogGlobalTexgen[ 1 ][ 1 ][ 1 ]
		+ view.viewDef->renderView.vieworg[ 2 ]
			* light.fogGlobalTexgen[ 1 ][ 1 ][ 2 ]
		+ light.fogGlobalTexgen[ 1 ][ 1 ][ 3 ];
	std::memset( light.fogGlobalTexgen[ 1 ][ 0 ], 0,
		sizeof( light.fogGlobalTexgen[ 1 ][ 0 ] ) );
	light.fogGlobalTexgen[ 1 ][ 0 ][ 3 ] = FOG_ENTER + viewerDistance;
	if ( !FloatsAreFinite( &light.fogGlobalTexgen[ 0 ][ 0 ][ 0 ], 16 ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
			4, -1, -1, light.sourceOrdinal, -1, 0 );
		return false;
	}

	for ( int localSurfaceIndex = 0;
			localSurfaceIndex < light.surfaceCount; ++localSurfaceIndex ) {
		if ( !AppendFogReceiverPrimitive( light, stage, absoluteLightIndex,
				absoluteStageIndex, light.firstSurface + localSurfaceIndex,
				lightArenaBase, stageArenaBase, surfaceArenaBase, error ) ) {
			return false;
		}
	}

	classicFogBlendDomainPrimitive_t cap;
	InitPrimitive( cap );
	cap.legacyGeometry = frustum;
	cap.lightIndex = absoluteLightIndex;
	cap.lightStageIndex = absoluteStageIndex;
	cap.kind = CLASSIC_FOG_BLEND_PRIMITIVE_FOG_FRUSTUM_CAP;
	cap.receiver = CLASSIC_FOG_BLEND_RECEIVER_FRUSTUM;
	cap.disposition = CLASSIC_FOG_BLEND_PRIMITIVE_DRAW;
	cap.vertexCount = frustum->numVerts;
	cap.firstIndex = 0;
	cap.indexCount = frustum->numIndexes;
	cap.vertexOffset = 0;
	cap.scissorX1 = view.scissorX1;
	cap.scissorY1 = view.scissorY1;
	cap.scissorX2 = view.scissorX2;
	cap.scissorY2 = view.scissorY2;
	cap.depth.testEnabled = true;
	cap.depth.writeEnabled = false;
	cap.depth.compareOperation = RENDERER_COMPARE_LESS_OR_EQUAL;
	cap.cull = RENDERER_CULL_BACK;
	std::memcpy( cap.modelMatrix, view.viewDef->worldSpace.modelMatrix,
		sizeof( cap.modelMatrix ) );
	std::memcpy( cap.modelViewMatrix, view.viewDef->worldSpace.modelViewMatrix,
		sizeof( cap.modelViewMatrix ) );
	SealFogTexgen( light, cap.modelMatrix, cap.fogTexgen );
	if ( !AppendPrimitive( light, stage, cap, lightArenaBase,
			stageArenaBase, surfaceArenaBase, error ) ) {
		return false;
	}
	light.fogFrustumPrimitiveCount++;
	light.lightStageCount = 1;
	light.activeLightStageCount = 1;
	stage.hash = HashLightStage( stage, lightArenaBase );
	return true;
}

static bool PrepareBlendLight( classicFogBlendDomainLight_t &light,
		int absoluteLightIndex, const viewLight_t &viewLight,
		bool skipBlendLights,
		int lightArenaBase, int stageArenaBase, int surfaceArenaBase,
		classicFogBlendBuildError_t &error ) {
	// Preserve RB_BlendLight's historically observable early-return ordering:
	// a missing GLOBAL head suppresses the LOCAL chain before skip/state checks.
	if ( !light.globalChainPresent ) {
		light.disposition =
			CLASSIC_FOG_BLEND_LIGHT_NOOP_MISSING_GLOBAL_CHAIN;
		return true;
	}
	if ( skipBlendLights ) {
		light.disposition = CLASSIC_FOG_BLEND_LIGHT_NOOP_SKIP_BLEND;
		return true;
	}
	if ( viewLight.lightShader == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_SHADER,
			0, -1, -1, light.sourceOrdinal );
		return false;
	}
	const int registerCount = viewLight.lightShader->GetNumRegisters();
	if ( viewLight.shaderRegisters == NULL || registerCount <= 0 ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_SHADER_REGISTERS,
			registerCount, -1, -1, light.sourceOrdinal );
		return false;
	}
	if ( !AddTexture( viewLight.falloffImage,
			light.falloffTextureResourceId, error, -1 ) ) {
		error.lightOrdinal = light.sourceOrdinal;
		return false;
	}
	const int sourceStageCount = viewLight.lightShader->GetNumStages();
	for ( int sourceStageIndex = 0; sourceStageIndex < sourceStageCount;
			++sourceStageIndex ) {
		const shaderStage_t *sourceStage =
			viewLight.lightShader->GetStage( sourceStageIndex );
		if ( sourceStage == NULL ) {
			SetError( error, CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_STAGE,
				sourceStageIndex, -1, -1, light.sourceOrdinal, -1,
				sourceStageIndex );
			return false;
		}
		if ( domain.lightStageCount
				>= CLASSIC_FOG_BLEND_DOMAIN_MAX_LIGHT_STAGES ) {
			SetError( error,
				CLASSIC_FOG_BLEND_FAILURE_LIGHT_STAGE_POOL_OVERFLOW,
				domain.lightStageCount, -1, -1, light.sourceOrdinal, -1,
				sourceStageIndex );
			return false;
		}
		const int absoluteStageIndex = domain.lightStageCount++;
		classicFogBlendDomainLightStage_t &stage =
			domain.lightStages[ absoluteStageIndex ];
		InitLightStage( stage );
		stage.lightIndex = absoluteLightIndex;
		stage.sourceStageIndex = sourceStageIndex;
		stage.firstPrimitive = domain.primitiveCount;
		stage.falloffTextureResourceId = light.falloffTextureResourceId;
		if ( !ReadRegister( viewLight.shaderRegisters, registerCount,
				sourceStage->conditionRegister, stage.condition, error,
				sourceStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			return false;
		}
		const bool active = stage.condition != 0.0f;
		if ( !active ) {
			stage.disposition =
				CLASSIC_FOG_BLEND_STAGE_NOOP_INACTIVE_CONDITION;
			light.inactiveLightStageCount++;
			light.noopLightStageCount++;
			for ( int localSurfaceIndex = 0;
					localSurfaceIndex < light.surfaceCount;
					++localSurfaceIndex ) {
				if ( !AppendBlendPrimitive( light, stage, absoluteLightIndex,
						absoluteStageIndex,
						light.firstSurface + localSurfaceIndex,
						CLASSIC_FOG_BLEND_PRIMITIVE_NOOP_INACTIVE_STAGE,
						lightArenaBase, stageArenaBase, surfaceArenaBase,
						error ) ) {
					return false;
				}
			}
			stage.hash = HashLightStage( stage, lightArenaBase );
			light.lightStageCount++;
			continue;
		}
		if ( sourceStage->newStage != NULL
				|| sourceStage->texture.cinematic != NULL
				|| sourceStage->texture.dynamic != DI_STATIC ) {
			SetError( error,
				sourceStage->newStage != NULL
					? CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_MATERIAL
					: CLASSIC_FOG_BLEND_FAILURE_DYNAMIC_RESOURCE,
				sourceStageIndex, -1, -1, light.sourceOrdinal, -1,
				sourceStageIndex );
			return false;
		}
		stage.disposition = CLASSIC_FOG_BLEND_STAGE_DRAW;
		stage.hasTextureMatrix = sourceStage->texture.hasMatrix;
		if ( !EvaluateStageColor( *sourceStage, viewLight.shaderRegisters,
				registerCount, stage.color, error, sourceStageIndex )
				|| !EvaluateTextureMatrix( sourceStage->texture,
					viewLight.shaderRegisters, registerCount,
					stage.textureMatrix, error, sourceStageIndex )
				|| !SealBlendRenderState( sourceStage->drawStateBits,
					stage, error, sourceStageIndex )
				|| !AddTexture( sourceStage->texture.image,
					stage.projectionTextureResourceId, error,
					sourceStageIndex ) ) {
			error.lightOrdinal = light.sourceOrdinal;
			return false;
		}
		for ( int localSurfaceIndex = 0;
				localSurfaceIndex < light.surfaceCount; ++localSurfaceIndex ) {
			if ( !AppendBlendPrimitive( light, stage, absoluteLightIndex,
					absoluteStageIndex,
					light.firstSurface + localSurfaceIndex,
					CLASSIC_FOG_BLEND_PRIMITIVE_DRAW, lightArenaBase,
					stageArenaBase, surfaceArenaBase, error ) ) {
				return false;
			}
		}
		stage.hash = HashLightStage( stage, lightArenaBase );
		light.lightStageCount++;
		light.activeLightStageCount++;
	}
	return true;
}

static bool PrepareView( const idScenePacketFrame &packetFrame,
		const scenePacket_t &scene, classicFogBlendDomainView_t &view ) {
	const classicFogBlendCheckpoint_t checkpoint = {
		domain.lightCount, domain.surfaceCount, domain.lightStageCount,
		domain.primitiveCount, domain.textureCount
	};
	classicFogBlendBuildError_t error;
	InitError( error );
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	if ( packetStats.overflow ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SCENE_PACKET_OVERFLOW,
			packetStats.overflowCause );
		return FailView( view, checkpoint, error );
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
			|| !FloatsAreFinite( viewDef->projectionMatrix, 16 )
			|| !FloatsAreFinite( viewDef->worldSpace.modelMatrix, 16 )
			|| !FloatsAreFinite( viewDef->worldSpace.modelViewMatrix, 16 )
			|| !FloatsAreFinite( viewDef->renderView.vieworg.ToFloatPtr(), 3 ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_VIEW,
			viewDef != NULL ? viewDef->renderFlags : 0 );
		return FailView( view, checkpoint, error );
	}
	// The classic outer early-out suppresses both fog and blend. Keep that
	// exact behavior on the untouched fallback rather than preparing either
	// light class independently.
	if ( r_skipFogLights.GetBool() ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_STATE, 1 );
		return FailView( view, checkpoint, error );
	}
	if ( r_showOverDraw.GetInteger() != 0 || r_singleTriangle.GetBool() ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_STATE,
			r_showOverDraw.GetInteger() != 0
				? 100 + r_showOverDraw.GetInteger() : 200 );
		return FailView( view, checkpoint, error );
	}
	if ( viewDef->worldSpace.weaponDepthHack
			|| viewDef->worldSpace.modelDepthHack != 0.0f ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_DEPTH_HACK, 3 );
		return FailView( view, checkpoint, error );
	}
	view.useScissor = r_useScissor.GetBool();
	view.skipBlendLights = r_skipBlendLights.GetBool();

	if ( !RangeFits( scene.firstPassPacket, scene.passPacketCount,
			packetFrame.NumPasses() )
			|| !RangeFits( scene.firstDrawPacket, scene.drawPacketCount,
				packetFrame.NumDrawPackets() ) ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_INVALID_SCENE_RANGE,
			scene.passPacketCount );
		return FailView( view, checkpoint, error );
	}
	const passPacket_t *fogBlendPass = NULL;
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
			SetError( error, CLASSIC_FOG_BLEND_FAILURE_INVALID_DRAW_RANGE,
				pass.drawPacketCount, passPacketIndex );
			return FailView( view, checkpoint, error );
		}
		if ( pass.passCategory != RENDER_PASS_FOG_BLEND ) {
			continue;
		}
		if ( fogBlendPass != NULL || !pass.enabled || pass.commandOnly
				|| ( pass.packetCategory != SCENE_PACKET_CATEGORY_WORLD
					&& !( pass.drawPacketCount == 0
						&& pass.packetCategory
							== SCENE_PACKET_CATEGORY_COMMAND ) ) ) {
			SetError( error,
				CLASSIC_FOG_BLEND_FAILURE_INVALID_FOG_BLEND_PASS,
				pass.passCategory, passPacketIndex );
			return FailView( view, checkpoint, error );
		}
		fogBlendPass = &pass;
		view.fogBlendPassPacketIndex = passPacketIndex;
	}
	if ( fogBlendPass == NULL ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_INVALID_FOG_BLEND_PASS,
			-1 );
		return FailView( view, checkpoint, error );
	}
	view.packetDrawCount = fogBlendPass->drawPacketCount;
	const int packetEnd = fogBlendPass->firstDrawPacket
		+ fogBlendPass->drawPacketCount;
	int packetCursor = fogBlendPass->firstDrawPacket;
	int expectedSourceOrdinal = 0;
	int sourceLightOrdinal = 0;
	for ( const viewLight_t *viewLight = viewDef->viewLights;
			viewLight != NULL;
			viewLight = viewLight->next, ++sourceLightOrdinal ) {
		if ( !ViewLightIsFogOrBlend( viewLight ) ) {
			continue;
		}
		if ( domain.lightCount >= CLASSIC_FOG_BLEND_DOMAIN_MAX_LIGHTS ) {
			SetError( error, CLASSIC_FOG_BLEND_FAILURE_LIGHT_POOL_OVERFLOW,
				domain.lightCount, view.fogBlendPassPacketIndex,
				packetCursor, sourceLightOrdinal );
			return FailView( view, checkpoint, error );
		}
		const int absoluteLightIndex = domain.lightCount++;
		classicFogBlendDomainLight_t &light =
			domain.lights[ absoluteLightIndex ];
		InitLight( light );
		light.sourceOrdinal = sourceLightOrdinal;
		light.kind = viewLight->lightShader->IsFogLight()
			? CLASSIC_FOG_BLEND_LIGHT_FOG : CLASSIC_FOG_BLEND_LIGHT_BLEND;
		light.firstSurface = domain.surfaceCount;
		light.firstLightStage = domain.lightStageCount;
		light.firstPrimitive = domain.primitiveCount;
		light.scissorX1 = viewLight->scissorRect.x1;
		light.scissorY1 = viewLight->scissorRect.y1;
		light.scissorX2 = viewLight->scissorRect.x2;
		light.scissorY2 = viewLight->scissorRect.y2;
		light.globalChainPresent = viewLight->globalInteractions != NULL;
		const bool noopBlend =
			light.kind == CLASSIC_FOG_BLEND_LIGHT_BLEND
			&& ( !light.globalChainPresent || view.skipBlendLights );
		if ( light.kind == CLASSIC_FOG_BLEND_LIGHT_FOG ) {
			if ( !FloatsAreFinite( viewLight->fogPlane.ToFloatPtr(), 4 ) ) {
				SetError( error, CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
					0, view.fogBlendPassPacketIndex, packetCursor,
					sourceLightOrdinal );
				return FailView( view, checkpoint, error );
			}
			std::memcpy( light.fogPlane, viewLight->fogPlane.ToFloatPtr(),
				sizeof( light.fogPlane ) );
		} else if ( !noopBlend ) {
			for ( int plane = 0; plane < 4; ++plane ) {
				if ( !FloatsAreFinite(
						viewLight->lightProject[ plane ].ToFloatPtr(), 4 ) ) {
					SetError( error,
						CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE,
						plane, view.fogBlendPassPacketIndex, packetCursor,
						sourceLightOrdinal );
					return FailView( view, checkpoint, error );
				}
				std::memcpy( light.lightProject[ plane ],
					viewLight->lightProject[ plane ].ToFloatPtr(),
					sizeof( light.lightProject[ plane ] ) );
			}
		}

		const drawSurf_t *chains[ 2 ] = {
			viewLight->globalInteractions, viewLight->localInteractions
		};
		const classicFogBlendDomainReceiver_t receivers[ 2 ] = {
			CLASSIC_FOG_BLEND_RECEIVER_GLOBAL,
			CLASSIC_FOG_BLEND_RECEIVER_LOCAL
		};
		for ( int chainIndex = 0; chainIndex < 2; ++chainIndex ) {
			int receiverOrdinal = 0;
			for ( const drawSurf_t *drawSurf = chains[ chainIndex ];
					drawSurf != NULL;
					drawSurf = drawSurf->nextOnLight, ++receiverOrdinal ) {
				if ( packetCursor < fogBlendPass->firstDrawPacket
						|| packetCursor >= packetEnd ) {
					SetError( error,
						CLASSIC_FOG_BLEND_FAILURE_SOURCE_PACKET_MISMATCH,
						expectedSourceOrdinal,
						view.fogBlendPassPacketIndex, packetCursor,
						sourceLightOrdinal, receiverOrdinal );
					return FailView( view, checkpoint, error );
				}
				const sceneFogBlendReceiverClass_t packetReceiver =
					receivers[ chainIndex ] ==
						CLASSIC_FOG_BLEND_RECEIVER_GLOBAL
						? SCENE_FOG_BLEND_RECEIVER_GLOBAL
						: SCENE_FOG_BLEND_RECEIVER_LOCAL;
				if ( noopBlend ) {
					const drawPacket_t &packet =
						packetFrame.DrawPacket( packetCursor );
					if ( !ValidateDrawPacketIdentity( packet, packetCursor,
							view.viewDef, viewLight, sourceLightOrdinal,
							packetReceiver, receiverOrdinal,
							expectedSourceOrdinal, drawSurf, error ) ) {
						error.passPacketIndex = view.fogBlendPassPacketIndex;
						return FailView( view, checkpoint, error );
					}
					if ( MaterialRequestsClassicDeform( drawSurf ) ) {
						classicDeformRole_t deformRole;
						classicDeformOutcome_t deformOutcome;
						std::uint64_t deformHash = 0;
						if ( !ValidateReceiverDeformContract( drawSurf, packet,
								deformRole, deformOutcome, deformHash ) ) {
							SetError( error,
								CLASSIC_FOG_BLEND_FAILURE_DEFORM_CONTRACT,
								DeformContractFailureDetail( &packet ),
								view.fogBlendPassPacketIndex, packetCursor,
								sourceLightOrdinal, receiverOrdinal );
							return FailView( view, checkpoint, error );
						}
						if ( deformHash != 0
								&& deformRole
									== CLASSIC_DEFORM_ROLE_FOG_RECEIVER
								&& deformOutcome
									== CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE ) {
							if ( light.materialDeformReceiverCount == 0 ) {
								light.deformContractHash = HASH_OFFSET;
							}
							light.materialDeformReceiverCount++;
							HashU64( light.deformContractHash, deformHash );
						}
					}
				} else if ( !AppendSurface( packetFrame, view, light,
						absoluteLightIndex, viewLight, drawSurf,
						sourceLightOrdinal, receivers[ chainIndex ],
						receiverOrdinal, expectedSourceOrdinal,
						view.fogBlendPassPacketIndex, packetCursor,
						checkpoint.lightCount, error ) ) {
					return FailView( view, checkpoint, error );
				}
				packetCursor++;
				expectedSourceOrdinal++;
			}
		}

		const bool prepared = light.kind == CLASSIC_FOG_BLEND_LIGHT_FOG
			? PrepareFogLight( view, light, absoluteLightIndex, *viewLight,
				checkpoint.lightCount, checkpoint.lightStageCount,
				checkpoint.surfaceCount, error )
			: PrepareBlendLight( light, absoluteLightIndex, *viewLight,
				view.skipBlendLights,
				checkpoint.lightCount, checkpoint.lightStageCount,
				checkpoint.surfaceCount, error );
		if ( !prepared ) {
			error.passPacketIndex = view.fogBlendPassPacketIndex;
			return FailView( view, checkpoint, error );
		}
		if ( light.kind == CLASSIC_FOG_BLEND_LIGHT_BLEND
				&& light.disposition != CLASSIC_FOG_BLEND_LIGHT_DRAW ) {
			view.noopLightCount++;
		}
		light.hash = HashLight( light, checkpoint.lightCount,
			checkpoint.lightStageCount, checkpoint.surfaceCount );
	}
	if ( packetCursor != packetEnd ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SOURCE_PACKET_MISMATCH,
			packetEnd - packetCursor, view.fogBlendPassPacketIndex,
			packetCursor );
		return FailView( view, checkpoint, error );
	}

	view.firstLight = checkpoint.lightCount;
	view.lightCount = domain.lightCount - checkpoint.lightCount;
	view.firstSurface = checkpoint.surfaceCount;
	view.surfaceCount = domain.surfaceCount - checkpoint.surfaceCount;
	view.firstLightStage = checkpoint.lightStageCount;
	view.lightStageCount = domain.lightStageCount - checkpoint.lightStageCount;
	view.firstPrimitive = checkpoint.primitiveCount;
	view.primitiveCount = domain.primitiveCount - checkpoint.primitiveCount;
	for ( int lightIndex = 0; lightIndex < view.lightCount; ++lightIndex ) {
		const classicFogBlendDomainLight_t &light =
			domain.lights[ view.firstLight + lightIndex ];
		if ( light.kind == CLASSIC_FOG_BLEND_LIGHT_FOG ) {
			view.fogLightCount++;
		} else {
			view.blendLightCount++;
		}
		view.activeLightStageCount += light.activeLightStageCount;
		view.inactiveLightStageCount += light.inactiveLightStageCount;
		view.noopLightStageCount += light.noopLightStageCount;
		view.drawablePrimitiveCount += light.drawablePrimitiveCount;
		view.noopPrimitiveCount += light.noopPrimitiveCount;
		view.fogReceiverPrimitiveCount += light.fogReceiverPrimitiveCount;
		view.fogFrustumPrimitiveCount += light.fogFrustumPrimitiveCount;
		view.blendPrimitiveCount += light.blendPrimitiveCount;
		view.materialDeformReceiverCount +=
			light.materialDeformReceiverCount;
		for ( int receiver = 0; receiver < CLASSIC_FOG_BLEND_RECEIVER_COUNT;
				++receiver ) {
			view.receiverSurfaceCount[ receiver ]
				+= light.receiverSurfaceCount[ receiver ];
		}
	}
	if ( view.drawablePrimitiveCount + view.noopPrimitiveCount
			!= view.primitiveCount
			|| view.fogReceiverPrimitiveCount
				+ view.fogFrustumPrimitiveCount + view.blendPrimitiveCount
				!= view.drawablePrimitiveCount ) {
		SetError( error, CLASSIC_FOG_BLEND_FAILURE_SOURCE_PACKET_MISMATCH,
			view.primitiveCount, view.fogBlendPassPacketIndex );
		return FailView( view, checkpoint, error );
	}
	view.failure = CLASSIC_FOG_BLEND_FAILURE_NONE;
	view.hash = HashView( view );
	view.ready = true;
	domain.stats.readyViews++;
	domain.stats.lights += view.lightCount;
	domain.stats.fogLights += view.fogLightCount;
	domain.stats.blendLights += view.blendLightCount;
	domain.stats.noopLights += view.noopLightCount;
	domain.stats.surfaces += view.surfaceCount;
	domain.stats.lightStages += view.lightStageCount;
	domain.stats.activeLightStages += view.activeLightStageCount;
	domain.stats.inactiveLightStages += view.inactiveLightStageCount;
	domain.stats.noopLightStages += view.noopLightStageCount;
	domain.stats.primitives += view.primitiveCount;
	domain.stats.drawablePrimitives += view.drawablePrimitiveCount;
	domain.stats.noopPrimitives += view.noopPrimitiveCount;
	domain.stats.fogReceiverPrimitives += view.fogReceiverPrimitiveCount;
	domain.stats.fogFrustumPrimitives += view.fogFrustumPrimitiveCount;
	domain.stats.blendPrimitives += view.blendPrimitiveCount;
	domain.stats.materialDeformReceivers +=
		view.materialDeformReceiverCount;
	for ( int receiver = 0; receiver < CLASSIC_FOG_BLEND_RECEIVER_COUNT;
			++receiver ) {
		domain.stats.receiverSurfaces[ receiver ]
			+= view.receiverSurfaceCount[ receiver ];
	}
	return true;
}

static void RecordFallback( classicFogBlendDomainView_t *view,
		classicFogBlendDomainBackend_t backend,
		classicFogBlendDomainFailure_t failure, int detail,
		int fogReceiverPrimitives, int fogFrustumPrimitives,
		int blendPrimitives, int noopPrimitives, int noopLightStages,
		int noopLights ) {
	classicFogBlendDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view == NULL ) {
		coverage.untrackedFallbacks++;
		return;
	}
	const int viewIndex = ViewIndex( view );
	if ( viewIndex < 0 || view->backendOutcome[ backend ]
			!= CLASSIC_FOG_BLEND_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return;
	}
	view->backendOutcome[ backend ] = CLASSIC_FOG_BLEND_BACKEND_FALLBACK;
	view->backendFailure[ backend ] = failure == CLASSIC_FOG_BLEND_FAILURE_NONE
		? CLASSIC_FOG_BLEND_FAILURE_BACKEND_REJECTED : failure;
	view->backendFailureDetail[ backend ] = detail;
	view->backendFogReceiverPrimitives[ backend ] = fogReceiverPrimitives;
	view->backendFogFrustumPrimitives[ backend ] = fogFrustumPrimitives;
	view->backendBlendPrimitives[ backend ] = blendPrimitives;
	view->backendNoopPrimitives[ backend ] = noopPrimitives;
	view->backendNoopLightStages[ backend ] = noopLightStages;
	view->backendNoopLights[ backend ] = noopLights;
	coverage.fallbackViewMask |= 1ull << viewIndex;
	coverage.fallbackViews++;
	coverage.fallbackLights += view->lightCount;
	coverage.fallbackSurfaces += view->surfaceCount;
	coverage.fallbackFogReceiverPrimitives
		+= view->fogReceiverPrimitiveCount;
	coverage.fallbackFogFrustumPrimitives
		+= view->fogFrustumPrimitiveCount;
	coverage.fallbackBlendPrimitives += view->blendPrimitiveCount;
	coverage.fallbackNoopPrimitives += view->noopPrimitiveCount;
	coverage.fallbackNoopLightStages += view->noopLightStageCount;
	coverage.fallbackNoopLights += view->noopLightCount;
	if ( view->backendFailure[ backend ]
			== CLASSIC_FOG_BLEND_FAILURE_BACKEND_COVERAGE_MISMATCH ) {
		coverage.coverageMismatches++;
	}
	if ( view->backendFailure[ backend ] >= CLASSIC_FOG_BLEND_FAILURE_NONE
			&& view->backendFailure[ backend ]
				< CLASSIC_FOG_BLEND_FAILURE_COUNT ) {
		domain.stats.failureCounts[ view->backendFailure[ backend ] ]++;
	}
}

} // namespace

void R_ClassicFogBlendDomain_ResetFrame( void ) {
	std::memset( &domain.stats, 0, sizeof( domain.stats ) );
	domain.viewCount = 0;
	domain.lightCount = 0;
	domain.surfaceCount = 0;
	domain.lightStageCount = 0;
	domain.primitiveCount = 0;
	domain.textureCount = 0;
	domain.generation++;
	if ( domain.generation == 0 ) {
		domain.generation = 1;
	}
	idStr::Copynz( domain.stats.status, "empty",
		sizeof( domain.stats.status ) );
}

void R_ClassicFogBlendDomain_PrepareFrame(
		const idScenePacketFrame &packetFrame ) {
	R_ClassicFogBlendDomain_ResetFrame();
	domain.stats.prepared = true;
	domain.stats.sourceScenes = packetFrame.NumScenes();
	domain.stats.overflow = packetFrame.Stats().overflow;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes();
			++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( !SceneIsFogBlendCandidate( packetFrame, scene ) ) {
			continue;
		}
		if ( FindMutableView( scene.viewDef ) != NULL ) {
			continue;
		}
		domain.stats.fogBlendViews++;
		if ( domain.viewCount >= CLASSIC_FOG_BLEND_DOMAIN_MAX_VIEWS ) {
			domain.stats.overflow = true;
			domain.stats.fallbackViews++;
			domain.stats.failureCounts[
				CLASSIC_FOG_BLEND_FAILURE_VIEW_POOL_OVERFLOW ]++;
			continue;
		}
		classicFogBlendDomainView_t &view =
			domain.views[ domain.viewCount++ ];
		InitView( view, scene.viewDef, sceneIndex );
		PrepareView( packetFrame, scene, view );
	}
	domain.stats.textures = domain.textureCount;
	domain.stats.frameValid = !domain.stats.overflow
		&& domain.stats.fallbackViews == 0;
	std::uint64_t frameHash = HASH_OFFSET;
	HashInt( frameHash, domain.stats.sourceScenes );
	HashInt( frameHash, domain.stats.fogBlendViews );
	for ( int i = 0; i < domain.viewCount; ++i ) {
		HashInt( frameHash, domain.views[ i ].scenePacketIndex );
		HashBool( frameHash, domain.views[ i ].ready );
		HashInt( frameHash, domain.views[ i ].failure );
		HashU64( frameHash, domain.views[ i ].hash );
	}
	domain.stats.hash = frameHash;
	if ( domain.stats.fogBlendViews == 0 ) {
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

const classicFogBlendDomainStats_t &R_ClassicFogBlendDomain_Stats( void ) {
	return domain.stats;
}

int R_ClassicFogBlendDomain_NumViews( void ) {
	return domain.viewCount;
}

const classicFogBlendDomainView_t *R_ClassicFogBlendDomain_ViewByIndex(
		int index ) {
	return index >= 0 && index < domain.viewCount
		? &domain.views[ index ] : NULL;
}

const classicFogBlendDomainView_t *
		R_ClassicFogBlendDomain_ViewForScenePacket( int scenePacketIndex ) {
	for ( int i = 0; i < domain.viewCount; ++i ) {
		if ( domain.views[ i ].scenePacketIndex == scenePacketIndex ) {
			return &domain.views[ i ];
		}
	}
	return NULL;
}

const classicFogBlendDomainView_t *R_ClassicFogBlendDomain_FindView(
		const viewDef_t *viewDef ) {
	return FindMutableView( viewDef );
}

const classicFogBlendDomainLight_t *R_ClassicFogBlendDomain_ViewLight(
		const classicFogBlendDomainView_t &view, int lightIndex ) {
	if ( !view.ready || lightIndex < 0 || lightIndex >= view.lightCount
			|| !RangeFits( view.firstLight, view.lightCount,
				domain.lightCount ) ) {
		return NULL;
	}
	return &domain.lights[ view.firstLight + lightIndex ];
}

const classicFogBlendDomainLightStage_t *R_ClassicFogBlendDomain_LightStage(
		const classicFogBlendDomainLight_t &light, int stageIndex ) {
	if ( LightIndex( &light ) < 0 || stageIndex < 0
			|| stageIndex >= light.lightStageCount
			|| !RangeFits( light.firstLightStage, light.lightStageCount,
				domain.lightStageCount ) ) {
		return NULL;
	}
	return &domain.lightStages[ light.firstLightStage + stageIndex ];
}

const classicFogBlendDomainPrimitive_t *
		R_ClassicFogBlendDomain_ViewPrimitive(
			const classicFogBlendDomainView_t &view, int primitiveIndex ) {
	if ( !view.ready || primitiveIndex < 0
			|| primitiveIndex >= view.primitiveCount
			|| !RangeFits( view.firstPrimitive, view.primitiveCount,
				domain.primitiveCount ) ) {
		return NULL;
	}
	return &domain.primitives[ view.firstPrimitive + primitiveIndex ];
}

const classicFogBlendDomainTexture_t *R_ClassicFogBlendDomain_ResolveTexture(
		std::uint64_t textureResourceId ) {
	const int textureIndex = TextureIndexFromResourceId( textureResourceId );
	if ( textureIndex < 0 || textureIndex >= domain.textureCount ) {
		return NULL;
	}
	const classicFogBlendDomainTexture_t &texture =
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

bool R_ClassicFogBlendDomain_RecordOwned( const viewDef_t *viewDef,
		classicFogBlendDomainBackend_t backend, int fogReceiverPrimitives,
		int fogFrustumPrimitives, int blendPrimitives, int noopPrimitives,
		int noopLightStages, int noopLights ) {
	if ( backend < CLASSIC_FOG_BLEND_BACKEND_GL
			|| backend >= CLASSIC_FOG_BLEND_BACKEND_COUNT ) {
		return false;
	}
	classicFogBlendDomainView_t *view = FindMutableView( viewDef );
	if ( view == NULL ) {
		return false;
	}
	classicFogBlendDomainBackendCoverage_t &coverage =
		domain.stats.backend[ backend ];
	if ( view->backendOutcome[ backend ]
			!= CLASSIC_FOG_BLEND_BACKEND_UNRECORDED ) {
		coverage.duplicateReports++;
		return false;
	}
	if ( !view->ready ) {
		RecordFallback( view, backend,
			CLASSIC_FOG_BLEND_FAILURE_BACKEND_NOT_READY, view->failure,
			fogReceiverPrimitives, fogFrustumPrimitives, blendPrimitives,
			noopPrimitives, noopLightStages, noopLights );
		return false;
	}
	if ( fogReceiverPrimitives != view->fogReceiverPrimitiveCount
			|| fogFrustumPrimitives != view->fogFrustumPrimitiveCount
			|| blendPrimitives != view->blendPrimitiveCount
			|| noopPrimitives != view->noopPrimitiveCount
			|| noopLightStages != view->noopLightStageCount
			|| noopLights != view->noopLightCount ) {
		const int detail = ( fogReceiverPrimitives
				- view->fogReceiverPrimitiveCount ) * 100000
			+ ( fogFrustumPrimitives
				- view->fogFrustumPrimitiveCount ) * 10000
			+ ( blendPrimitives - view->blendPrimitiveCount ) * 1000
			+ ( noopPrimitives - view->noopPrimitiveCount ) * 100
			+ ( noopLightStages - view->noopLightStageCount ) * 10
			+ ( noopLights - view->noopLightCount );
		RecordFallback( view, backend,
			CLASSIC_FOG_BLEND_FAILURE_BACKEND_COVERAGE_MISMATCH, detail,
			fogReceiverPrimitives, fogFrustumPrimitives, blendPrimitives,
			noopPrimitives, noopLightStages, noopLights );
		return false;
	}
	const int viewIndex = ViewIndex( view );
	view->backendOutcome[ backend ] = CLASSIC_FOG_BLEND_BACKEND_OWNED;
	view->backendFailure[ backend ] = CLASSIC_FOG_BLEND_FAILURE_NONE;
	view->backendFailureDetail[ backend ] = 0;
	view->backendFogReceiverPrimitives[ backend ] = fogReceiverPrimitives;
	view->backendFogFrustumPrimitives[ backend ] = fogFrustumPrimitives;
	view->backendBlendPrimitives[ backend ] = blendPrimitives;
	view->backendNoopPrimitives[ backend ] = noopPrimitives;
	view->backendNoopLightStages[ backend ] = noopLightStages;
	view->backendNoopLights[ backend ] = noopLights;
	coverage.ownedViewMask |= 1ull << viewIndex;
	coverage.ownedViews++;
	coverage.ownedLights += view->lightCount;
	coverage.ownedSurfaces += view->surfaceCount;
	coverage.ownedFogReceiverPrimitives += fogReceiverPrimitives;
	coverage.ownedFogFrustumPrimitives += fogFrustumPrimitives;
	coverage.ownedBlendPrimitives += blendPrimitives;
	coverage.ownedNoopPrimitives += noopPrimitives;
	coverage.ownedNoopLightStages += noopLightStages;
	coverage.ownedNoopLights += noopLights;
	return true;
}

void R_ClassicFogBlendDomain_RecordBackendFallback(
		const viewDef_t *viewDef, classicFogBlendDomainBackend_t backend,
		classicFogBlendDomainFailure_t failure, int detail ) {
	if ( backend < CLASSIC_FOG_BLEND_BACKEND_GL
			|| backend >= CLASSIC_FOG_BLEND_BACKEND_COUNT ) {
		return;
	}
	RecordFallback( FindMutableView( viewDef ), backend, failure, detail,
		0, 0, 0, 0, 0, 0 );
}

const classicFogBlendDomainBackendCoverage_t &
		R_ClassicFogBlendDomain_BackendCoverage(
			classicFogBlendDomainBackend_t backend ) {
	static const classicFogBlendDomainBackendCoverage_t empty = {};
	return backend >= CLASSIC_FOG_BLEND_BACKEND_GL
			&& backend < CLASSIC_FOG_BLEND_BACKEND_COUNT
		? domain.stats.backend[ backend ] : empty;
}

const char *ClassicFogBlendDomainLightKind_Name(
		classicFogBlendDomainLightKind_t kind ) {
	switch ( kind ) {
	case CLASSIC_FOG_BLEND_LIGHT_FOG: return "fog";
	case CLASSIC_FOG_BLEND_LIGHT_BLEND: return "blend";
	case CLASSIC_FOG_BLEND_LIGHT_KIND_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainReceiver_Name(
		classicFogBlendDomainReceiver_t receiver ) {
	switch ( receiver ) {
	case CLASSIC_FOG_BLEND_RECEIVER_GLOBAL: return "global";
	case CLASSIC_FOG_BLEND_RECEIVER_LOCAL: return "local";
	case CLASSIC_FOG_BLEND_RECEIVER_FRUSTUM: return "frustum";
	case CLASSIC_FOG_BLEND_RECEIVER_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainLightDisposition_Name(
		classicFogBlendDomainLightDisposition_t disposition ) {
	switch ( disposition ) {
	case CLASSIC_FOG_BLEND_LIGHT_DRAW: return "draw";
	case CLASSIC_FOG_BLEND_LIGHT_NOOP_SKIP_BLEND: return "skip-blend";
	case CLASSIC_FOG_BLEND_LIGHT_NOOP_MISSING_GLOBAL_CHAIN:
		return "missing-global-chain";
	case CLASSIC_FOG_BLEND_LIGHT_DISPOSITION_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainLightStageDisposition_Name(
		classicFogBlendDomainLightStageDisposition_t disposition ) {
	switch ( disposition ) {
	case CLASSIC_FOG_BLEND_STAGE_DRAW: return "draw";
	case CLASSIC_FOG_BLEND_STAGE_NOOP_INACTIVE_CONDITION:
		return "inactive-condition";
	case CLASSIC_FOG_BLEND_STAGE_NOOP_SKIP_BLEND: return "skip-blend";
	case CLASSIC_FOG_BLEND_STAGE_NOOP_MISSING_GLOBAL_CHAIN:
		return "missing-global-chain";
	case CLASSIC_FOG_BLEND_STAGE_DISPOSITION_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainPrimitiveKind_Name(
		classicFogBlendDomainPrimitiveKind_t kind ) {
	switch ( kind ) {
	case CLASSIC_FOG_BLEND_PRIMITIVE_FOG_RECEIVER: return "fog-receiver";
	case CLASSIC_FOG_BLEND_PRIMITIVE_FOG_FRUSTUM_CAP: return "fog-cap";
	case CLASSIC_FOG_BLEND_PRIMITIVE_BLEND_RECEIVER: return "blend-receiver";
	case CLASSIC_FOG_BLEND_PRIMITIVE_KIND_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainPrimitiveDisposition_Name(
		classicFogBlendDomainPrimitiveDisposition_t disposition ) {
	switch ( disposition ) {
	case CLASSIC_FOG_BLEND_PRIMITIVE_DRAW: return "draw";
	case CLASSIC_FOG_BLEND_PRIMITIVE_NOOP_INACTIVE_STAGE:
		return "inactive-stage";
	case CLASSIC_FOG_BLEND_PRIMITIVE_NOOP_SKIP_BLEND: return "skip-blend";
	case CLASSIC_FOG_BLEND_PRIMITIVE_NOOP_MISSING_GLOBAL_CHAIN:
		return "missing-global-chain";
	case CLASSIC_FOG_BLEND_PRIMITIVE_DISPOSITION_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainFailure_Name(
		classicFogBlendDomainFailure_t failure ) {
	switch ( failure ) {
	case CLASSIC_FOG_BLEND_FAILURE_NONE: return "none";
	case CLASSIC_FOG_BLEND_FAILURE_UNAVAILABLE: return "unavailable";
	case CLASSIC_FOG_BLEND_FAILURE_SCENE_PACKET_OVERFLOW: return "scene-packet-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_VIEW_POOL_OVERFLOW: return "view-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_LIGHT_POOL_OVERFLOW: return "light-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_SURFACE_POOL_OVERFLOW: return "surface-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_LIGHT_STAGE_POOL_OVERFLOW: return "light-stage-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_PRIMITIVE_POOL_OVERFLOW: return "primitive-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_TEXTURE_POOL_OVERFLOW: return "texture-pool-overflow";
	case CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_VIEW: return "unsupported-view";
	case CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_STATE: return "unsupported-state";
	case CLASSIC_FOG_BLEND_FAILURE_INVALID_SCENE_RANGE: return "invalid-scene-range";
	case CLASSIC_FOG_BLEND_FAILURE_INVALID_FOG_BLEND_PASS: return "invalid-fog-blend-pass";
	case CLASSIC_FOG_BLEND_FAILURE_INVALID_DRAW_RANGE: return "invalid-draw-range";
	case CLASSIC_FOG_BLEND_FAILURE_SOURCE_PACKET_MISMATCH: return "source-packet-mismatch";
	case CLASSIC_FOG_BLEND_FAILURE_INVALID_DRAW_PACKET: return "invalid-draw-packet";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_GEOMETRY_RECORD: return "missing-geometry-record";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_INSTANCE_RECORD: return "missing-instance-record";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_SHADER: return "missing-light-shader";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_SHADER_REGISTERS: return "missing-shader-registers";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_LIGHT_STAGE: return "missing-light-stage";
	case CLASSIC_FOG_BLEND_FAILURE_REGISTER_OUT_OF_RANGE: return "register-out-of-range";
	case CLASSIC_FOG_BLEND_FAILURE_NONFINITE_VALUE: return "nonfinite-value";
	case CLASSIC_FOG_BLEND_FAILURE_FOG_FRUSTUM_GEOMETRY: return "fog-frustum-geometry";
	case CLASSIC_FOG_BLEND_FAILURE_DEFORM: return "deform";
	case CLASSIC_FOG_BLEND_FAILURE_DEFORM_CONTRACT: return "deform-contract";
	case CLASSIC_FOG_BLEND_FAILURE_SKINNING: return "skinning";
	case CLASSIC_FOG_BLEND_FAILURE_SPECIAL_SURFACE: return "special-surface";
	case CLASSIC_FOG_BLEND_FAILURE_DEPTH_HACK: return "depth-hack";
	case CLASSIC_FOG_BLEND_FAILURE_NEGATIVE_SCALE: return "negative-scale";
	case CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_MATERIAL: return "unsupported-material";
	case CLASSIC_FOG_BLEND_FAILURE_UNSUPPORTED_RENDER_STATE: return "unsupported-render-state";
	case CLASSIC_FOG_BLEND_FAILURE_DYNAMIC_RESOURCE: return "dynamic-resource";
	case CLASSIC_FOG_BLEND_FAILURE_MISSING_RESOURCE: return "missing-resource";
	case CLASSIC_FOG_BLEND_FAILURE_DEFAULTED_RESOURCE: return "defaulted-resource";
	case CLASSIC_FOG_BLEND_FAILURE_UNLOADED_RESOURCE: return "unloaded-resource";
	case CLASSIC_FOG_BLEND_FAILURE_BACKEND_NOT_READY: return "backend-not-ready";
	case CLASSIC_FOG_BLEND_FAILURE_BACKEND_COVERAGE_MISMATCH: return "backend-coverage-mismatch";
	case CLASSIC_FOG_BLEND_FAILURE_BACKEND_REJECTED: return "backend-rejected";
	case CLASSIC_FOG_BLEND_FAILURE_COUNT: break;
	}
	return "unknown";
}

const char *ClassicFogBlendDomainBackend_Name(
		classicFogBlendDomainBackend_t backend ) {
	switch ( backend ) {
	case CLASSIC_FOG_BLEND_BACKEND_GL: return "GL";
	case CLASSIC_FOG_BLEND_BACKEND_VULKAN: return "Vulkan";
	case CLASSIC_FOG_BLEND_BACKEND_COUNT: break;
	}
	return "unknown";
}

bool RendererClassicFogBlendDomain_RunSelfTest( void ) {
	R_ClassicFogBlendDomain_ResetFrame();
	classicFogBlendDomainSurface_t deformHashSurface;
	InitSurface( deformHashSurface );
	const std::uint64_t undeformedSurfaceHash =
		HashSurface( deformHashSurface, 0 );
	deformHashSurface.deformRole = CLASSIC_DEFORM_ROLE_FOG_RECEIVER;
	deformHashSurface.deformOutcome =
		CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE;
	deformHashSurface.deformContractHash = 0x91234ull;
	if ( HashSurface( deformHashSurface, 0 ) == undeformedSurfaceHash
			|| idStr::Cmp( ClassicFogBlendDomainFailure_Name(
				CLASSIC_FOG_BLEND_FAILURE_DEFORM_CONTRACT ),
				"deform-contract" ) ) {
		return false;
	}
	viewDef_t viewDef;
	std::memset( &viewDef, 0, sizeof( viewDef ) );
	domain.viewCount = 1;
	classicFogBlendDomainView_t &view = domain.views[ 0 ];
	InitView( view, &viewDef, 7 );
	view.ready = true;
	view.failure = CLASSIC_FOG_BLEND_FAILURE_NONE;
	view.firstLight = 0;
	view.lightCount = 1;
	view.fogLightCount = 1;
	view.firstLightStage = 0;
	view.lightStageCount = 1;
	view.activeLightStageCount = 1;
	view.firstPrimitive = 0;
	view.primitiveCount = 2;
	view.drawablePrimitiveCount = 2;
	view.fogReceiverPrimitiveCount = 1;
	view.fogFrustumPrimitiveCount = 1;
	domain.lightCount = 1;
	domain.lightStageCount = 1;
	domain.primitiveCount = 2;
	InitLight( domain.lights[ 0 ] );
	domain.lights[ 0 ].firstLightStage = 0;
	domain.lights[ 0 ].lightStageCount = 1;
	domain.lights[ 0 ].firstPrimitive = 0;
	domain.lights[ 0 ].primitiveCount = 2;
	InitLightStage( domain.lightStages[ 0 ] );
	domain.lightStages[ 0 ].lightIndex = 0;
	InitPrimitive( domain.primitives[ 0 ] );
	domain.primitives[ 0 ].lightIndex = 0;
	domain.primitives[ 0 ].lightStageIndex = 0;
	domain.primitives[ 0 ].kind =
		CLASSIC_FOG_BLEND_PRIMITIVE_FOG_RECEIVER;
	InitPrimitive( domain.primitives[ 1 ] );
	domain.primitives[ 1 ].lightIndex = 0;
	domain.primitives[ 1 ].lightStageIndex = 0;
	domain.primitives[ 1 ].kind =
		CLASSIC_FOG_BLEND_PRIMITIVE_FOG_FRUSTUM_CAP;
	const std::uint64_t receiverHash = HashPrimitive( domain.primitives[ 0 ],
		0, 0, 0 );
	const std::uint64_t capHash = HashPrimitive( domain.primitives[ 1 ],
		0, 0, 0 );
	const bool accessors = R_ClassicFogBlendDomain_FindView( &viewDef ) == &view
		&& R_ClassicFogBlendDomain_ViewForScenePacket( 7 ) == &view
		&& R_ClassicFogBlendDomain_ViewLight( view, 0 ) == &domain.lights[ 0 ]
		&& R_ClassicFogBlendDomain_LightStage( domain.lights[ 0 ], 0 )
			== &domain.lightStages[ 0 ]
		&& R_ClassicFogBlendDomain_ViewPrimitive( view, 1 )
			== &domain.primitives[ 1 ];
	const bool stableOrder = receiverHash != capHash;
	const bool owned = R_ClassicFogBlendDomain_RecordOwned( &viewDef,
		CLASSIC_FOG_BLEND_BACKEND_GL, 1, 1, 0, 0, 0, 0 );
	const bool duplicateRejected = !R_ClassicFogBlendDomain_RecordOwned(
		&viewDef, CLASSIC_FOG_BLEND_BACKEND_GL, 1, 1, 0, 0, 0, 0 )
		&& domain.stats.backend[ CLASSIC_FOG_BLEND_BACKEND_GL ].duplicateReports
			== 1;

	R_ClassicFogBlendDomain_ResetFrame();
	domain.viewCount = 1;
	InitView( domain.views[ 0 ], &viewDef, 0 );
	domain.views[ 0 ].ready = true;
	domain.views[ 0 ].failure = CLASSIC_FOG_BLEND_FAILURE_NONE;
	domain.views[ 0 ].fogReceiverPrimitiveCount = 2;
	const bool mismatchRejected = !R_ClassicFogBlendDomain_RecordOwned(
		&viewDef, CLASSIC_FOG_BLEND_BACKEND_VULKAN, 1, 0, 0, 0, 0, 0 )
		&& domain.views[ 0 ].backendFailure[
			CLASSIC_FOG_BLEND_BACKEND_VULKAN ]
			== CLASSIC_FOG_BLEND_FAILURE_BACKEND_COVERAGE_MISMATCH
		&& domain.stats.backend[
			CLASSIC_FOG_BLEND_BACKEND_VULKAN ].coverageMismatches == 1;

	classicFogBlendDomainView_t rollbackView;
	InitView( rollbackView, NULL, -1 );
	const classicFogBlendCheckpoint_t checkpoint = { 0, 0, 0, 0, 0 };
	domain.lightCount = 1;
	domain.surfaceCount = 1;
	domain.lightStageCount = 1;
	domain.primitiveCount = 1;
	domain.textureCount = 1;
	classicFogBlendBuildError_t rollbackError;
	InitError( rollbackError );
	SetError( rollbackError,
		CLASSIC_FOG_BLEND_FAILURE_PRIMITIVE_POOL_OVERFLOW, 9 );
	const bool rollbackRejected = !FailView( rollbackView, checkpoint,
		rollbackError ) && domain.lightCount == 0 && domain.surfaceCount == 0
		&& domain.lightStageCount == 0 && domain.primitiveCount == 0
		&& domain.textureCount == 0 && rollbackView.firstLight == -1
		&& rollbackView.failure
			== CLASSIC_FOG_BLEND_FAILURE_PRIMITIVE_POOL_OVERFLOW;
	const bool names = idStr::Cmp(
		ClassicFogBlendDomainBackend_Name(
			CLASSIC_FOG_BLEND_BACKEND_VULKAN ), "Vulkan" ) == 0
		&& idStr::Cmp( ClassicFogBlendDomainPrimitiveKind_Name(
			CLASSIC_FOG_BLEND_PRIMITIVE_FOG_FRUSTUM_CAP ), "fog-cap" ) == 0;
	const bool passed = accessors && stableOrder && owned
		&& duplicateRejected && mismatchRejected && rollbackRejected && names;
	R_ClassicFogBlendDomain_ResetFrame();
	return passed;
}
