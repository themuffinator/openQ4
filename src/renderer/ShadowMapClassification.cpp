// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ShadowMapClassification.h"

#include <cmath>

int R_ShadowMapHashFloat( const int hash, const float value ) {
	unsigned int bits = 0;
	static_assert( sizeof( bits ) == sizeof( value ), "shadow cache requires 32-bit floats" );
	// Signed zero has identical projection behavior. memcpy avoids aliasing
	// violations and float-to-int overflow for large or non-finite inputs.
	if ( value != 0.0f ) {
		memcpy( &bits, &value, sizeof( bits ) );
	}
	return static_cast<int>( ( static_cast<unsigned int>( hash ) ^ bits ) * 16777619u );
}

static shadowMapLightClass_t R_ShadowMapLightClassForViewLight( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return SHADOWMAP_LIGHT_PROJECTED;
	}
	if ( vLight->lightDef != NULL && vLight->lightDef->parms.globalLight ) {
		return SHADOWMAP_LIGHT_GLOBAL;
	}
	if ( vLight->parallel ) {
		return SHADOWMAP_LIGHT_PARALLEL;
	}
	if ( vLight->pointLight ) {
		return SHADOWMAP_LIGHT_POINT;
	}
	return SHADOWMAP_LIGHT_PROJECTED;
}

static bool R_ShadowMapProjectedLightIsViewScoped( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL ) {
		return false;
	}
	const renderLight_t &parms = vLight->lightDef->parms;
	return parms.allowLightInViewID != 0 || parms.suppressLightInViewID != 0;
}

static bool R_ShadowMapProjectedLightUsesStockFlashlightShader( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return false;
	}

	const idMaterial *lightShader = vLight->lightShader;
	if ( lightShader == NULL && vLight->lightDef != NULL ) {
		lightShader = vLight->lightDef->lightShader;
	}
	if ( lightShader == NULL ) {
		return false;
	}

	const char *shaderName = lightShader->GetName();
	return shaderName != NULL && idStr::Icmp( shaderName, "gfx/lights/flashlight" ) == 0;
}

static bool R_ShadowMapProjectedLightNeedsAuthoredSingleProjection( const viewLight_t *vLight ) {
	return R_ShadowMapProjectedLightIsViewScoped( vLight )
		|| R_ShadowMapProjectedLightUsesStockFlashlightShader( vLight );
}

shadowMapLightClassification_t R_ClassifyShadowMapLight( const viewLight_t *vLight ) {
	shadowMapLightClassification_t classification;
	memset( &classification, 0, sizeof( classification ) );

	classification.lightClass = R_ShadowMapLightClassForViewLight( vLight );
	// Parallel (sun) lights carry pointLight=true with a faked far-away origin;
	// radial cube-map depth saturates for them, so they route through the
	// projected machinery with a synthesized orthographic projection instead
	// (R_ShadowMapBuildParallelClipPlanes).
	classification.pointLight = vLight != NULL && vLight->pointLight && !vLight->parallel;
	classification.projectedLight = !classification.pointLight;
	classification.ordinaryProjectedLight = classification.lightClass == SHADOWMAP_LIGHT_PROJECTED;
	classification.parallelLight = classification.lightClass == SHADOWMAP_LIGHT_PARALLEL;
	classification.globalLight = classification.lightClass == SHADOWMAP_LIGHT_GLOBAL;
	classification.projectedCSMGateApplies = classification.ordinaryProjectedLight;
	// Player weapon lights, including the stock flashlight projector, must keep
	// their authored projection instead of being camera-fitted into cascades.
	classification.projectedCSMEnabled = classification.projectedCSMGateApplies
		&& r_shadowMapProjectedCSM.GetBool()
		&& !R_ShadowMapProjectedLightNeedsAuthoredSingleProjection( vLight );
	classification.cascadeCount = 1;
	classification.atlasDiv = classification.pointLight ? 3 : 1;
	classification.tileCount = classification.pointLight ? 6 : 1;

	if ( vLight == NULL || classification.pointLight ) {
		return classification;
	}

	const int requestedCascadeCount = idMath::ClampInt( 1, SHADOWMAP_CLASSIFICATION_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() );
	classification.csmEnabled = r_shadowMapCSM.GetBool()
		&& requestedCascadeCount > 1
		&& ( !classification.projectedCSMGateApplies || classification.projectedCSMEnabled );

	if ( classification.csmEnabled ) {
		classification.cascadeCount = requestedCascadeCount;
		classification.atlasDiv = 2;
		classification.tileCount = requestedCascadeCount;
	}

	return classification;
}

shadowMapProjectedFilterSettings_t R_ShadowMapProjectedFilterSettings( const viewLight_t *vLight ) {
	shadowMapProjectedFilterSettings_t settings;
	memset( &settings, 0, sizeof( settings ) );

	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	// A global point light still uses the independently tuned point-light cube
	// policy.  This specialization is only for large projected sources: parallel
	// sunlight (including global+parallel sky lights) and global projectors.
	settings.distantSource = classification.projectedLight && vLight != NULL
		&& ( vLight->parallel || classification.globalLight );
	settings.filterScale = settings.distantSource
		? idMath::ClampFloat( 0.0f, 1.0f, r_shadowMapDistantFilterScale.GetFloat() )
		: 1.0f;
	settings.filterRadius = Max( 0.0f, r_shadowMapFilterRadius.GetFloat() ) * settings.filterScale;
	settings.filterTaps = idMath::ClampInt( 1, 13, r_shadowMapFilterTaps.GetInteger() );
	settings.filterMode = idMath::ClampInt( 0, 2, r_shadowMapFilterMode.GetInteger() );
	settings.pcssLightRadius = Max( 0.0f, r_shadowMapPCSSLightRadius.GetFloat() ) * settings.filterScale;
	settings.pcssMaxRadius = Max( 0.0f, r_shadowMapPCSSMaxRadius.GetFloat() ) * settings.filterScale;
	settings.effectiveFilterRadius = settings.filterRadius;
	if ( settings.filterMode == 2 ) {
		settings.effectiveFilterRadius = Max( settings.effectiveFilterRadius,
			Max( settings.pcssLightRadius, settings.pcssMaxRadius ) );
	}
	return settings;
}

float R_ShadowMapPointFarDistance( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return 1.0f;
	}

	idVec3 adjustedRadius = vLight->lightRadius;
	if ( vLight->lightDef != NULL ) {
		const renderLight_t &parms = vLight->lightDef->parms;
		for ( int component = 0; component < 3; ++component ) {
			adjustedRadius[ component ] = parms.lightRadius[ component ]
				+ idMath::Fabs( parms.lightCenter[ component ] );
		}
	}

	const float farDistance = adjustedRadius.Length()
		* r_shadowMapPointFarScale.GetFloat();
	return std::isfinite( static_cast<double>( farDistance ) )
		? Max( farDistance, 1.0f ) : 1.0f;
}

static float R_ShadowMapNonNegativeFinite( float value ) {
	return std::isfinite( static_cast<double>( value ) ) && value > 0.0f
		? value : 0.0f;
}

shadowMapPointReceiverSettings_t R_ClampShadowMapPointReceiverSettings(
		float farDistance, int faceSize, float constantBias, float normalBias,
		float texelBiasScale, float normalOffsetScale, float maxWorldBias ) {
	shadowMapPointReceiverSettings_t settings;
	settings.constantBias = R_ShadowMapNonNegativeFinite( constantBias );
	settings.normalBias = R_ShadowMapNonNegativeFinite( normalBias );
	settings.texelBiasScale = R_ShadowMapNonNegativeFinite( texelBiasScale );
	settings.normalOffsetScale =
		R_ShadowMapNonNegativeFinite( normalOffsetScale );
	settings.worldBiasScale = 1.0f;

	const double safeFar =
		std::isfinite( static_cast<double>( farDistance ) )
			&& farDistance > 0.0f
		? static_cast<double>( farDistance ) : 1.0;
	const double safeFace = static_cast<double>( Max( 1, faceSize ) );
	const double cap = static_cast<double>(
		R_ShadowMapNonNegativeFinite( maxWorldBias ) );
	const double scalarDepthBias =
		static_cast<double>( settings.constantBias )
		+ static_cast<double>( settings.normalBias );
	const double texelDepthBias =
		( static_cast<double>( settings.texelBiasScale ) / safeFace )
		* ( 1.0 + static_cast<double>(
			SHADOWMAP_POINT_RECEIVER_MAX_SLOPE ) );
	const double depthWorldBias = safeFar
		* Max( scalarDepthBias, texelDepthBias );
	const double normalOffsetWorldBias = safeFar
		* ( 2.0 * static_cast<double>( settings.normalOffsetScale )
			/ safeFace );
	const double requestedWorldBias =
		depthWorldBias + normalOffsetWorldBias;

	if ( cap > 0.0 && requestedWorldBias > cap ) {
		settings.worldBiasScale = static_cast<float>(
			cap / requestedWorldBias );
		settings.constantBias *= settings.worldBiasScale;
		settings.normalBias *= settings.worldBiasScale;
		settings.texelBiasScale *= settings.worldBiasScale;
		settings.normalOffsetScale *= settings.worldBiasScale;
	}

	return settings;
}

shadowMapPointReceiverSettings_t R_ShadowMapPointReceiverSettings(
		float farDistance, int faceSize ) {
	return R_ClampShadowMapPointReceiverSettings(
		farDistance, faceSize,
		r_shadowMapPointBias.GetFloat(),
		r_shadowMapPointNormalBias.GetFloat(),
		r_shadowMapTexelBiasScale.GetFloat(),
		r_shadowMapNormalOffsetScale.GetFloat(),
		r_shadowMapPointMaxWorldBias.GetFloat() );
}

shadowMapPointReceiverSettings_t R_ShadowMapPointStorageAdjustedReceiverSettings(
		const shadowMapPointReceiverSettings_t &baseSettings,
		float farDistance, int faceSize, bool depthCompare,
		bool highPrecision ) {
	if ( depthCompare ) {
		return baseSettings;
	}

	// Manual color depth needs a quantization-aware constant floor. Re-run the
	// common clamp after adding it: on an enormous light one fp16 step alone can
	// exceed the configured world-space budget, and the cap deliberately wins
	// that otherwise unsatisfiable tradeoff. Keep this outside the GL backend so
	// modern receivers sampling the same color cube use identical coefficients.
	const float storageStep = highPrecision
		? 1.0f / 2048.0f : 1.0f / 65025.0f;
	return R_ClampShadowMapPointReceiverSettings(
		farDistance, faceSize,
		Max( baseSettings.constantBias, storageStep * 1.5f ),
		baseSettings.normalBias,
		baseSettings.texelBiasScale,
		baseSettings.normalOffsetScale,
		r_shadowMapPointMaxWorldBias.GetFloat() );
}

bool R_ShadowMapCasterTransformNeedsTwoSided( const float modelMatrix[ 16 ] ) {
	if ( modelMatrix == NULL ) {
		return true;
	}

	// Evaluate the affine linear transform in double precision so large, but
	// finite, float scales do not overflow before the validity check.
	const double determinant =
		static_cast<double>( modelMatrix[ 0 ] ) *
			( static_cast<double>( modelMatrix[ 5 ] ) * static_cast<double>( modelMatrix[ 10 ] )
			- static_cast<double>( modelMatrix[ 9 ] ) * static_cast<double>( modelMatrix[ 6 ] ) )
		- static_cast<double>( modelMatrix[ 4 ] ) *
			( static_cast<double>( modelMatrix[ 1 ] ) * static_cast<double>( modelMatrix[ 10 ] )
			- static_cast<double>( modelMatrix[ 9 ] ) * static_cast<double>( modelMatrix[ 2 ] ) )
		+ static_cast<double>( modelMatrix[ 8 ] ) *
			( static_cast<double>( modelMatrix[ 1 ] ) * static_cast<double>( modelMatrix[ 6 ] )
			- static_cast<double>( modelMatrix[ 5 ] ) * static_cast<double>( modelMatrix[ 2 ] ) );
	// A singular transform has no reliable winding either. It can arise from
	// malformed or transient entity state; AUTO must fail conservatively rather
	// than culling one side of the collapsed caster.
	return !std::isfinite( determinant ) || determinant <= 0.0;
}

bool R_ShadowMapLightOriginInsideCasterBounds( const viewLight_t *vLight,
		const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
		const float boundsMax[ 3 ] ) {
	if ( vLight == NULL || modelMatrix == NULL
			|| boundsMin == NULL || boundsMax == NULL ) {
		return true;
	}

	idVec3 localLightOrigin;
	R_GlobalPointToLocal( modelMatrix, vLight->globalLightOrigin,
		localLightOrigin );
	for ( int component = 0; component < 3; ++component ) {
		const float local = localLightOrigin[ component ];
		const float minimum = boundsMin[ component ];
		const float maximum = boundsMax[ component ];
		if ( !std::isfinite( static_cast<double>( local ) )
				|| !std::isfinite( static_cast<double>( minimum ) )
				|| !std::isfinite( static_cast<double>( maximum ) )
				|| minimum > maximum ) {
			return true;
		}
		if ( local < minimum || local > maximum ) {
			return false;
		}
	}
	return true;
}

bool R_ShadowMapCasterOutsidePointFace( const viewLight_t *vLight,
		const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
		const float boundsMax[ 3 ], int cubeFace ) {
	if ( vLight == NULL || modelMatrix == NULL || boundsMin == NULL ||
			boundsMax == NULL || cubeFace < 0 || cubeFace >= 6 ) {
		return false;
	}
	for ( int index = 0; index < 16; ++index ) {
		if ( !std::isfinite( modelMatrix[index] ) ) {
			return false;
		}
	}
	if ( modelMatrix[3] != 0.0f || modelMatrix[7] != 0.0f ||
			modelMatrix[11] != 0.0f || modelMatrix[15] != 1.0f ) {
		return false;
	}
	double center[3], extent[3];
	for ( int axis = 0; axis < 3; ++axis ) {
		if ( !std::isfinite( boundsMin[axis] ) || !std::isfinite( boundsMax[axis] ) ||
				boundsMin[axis] > boundsMax[axis] ) {
			return false;
		}
		center[axis] = ( static_cast<double>( boundsMin[axis] ) + boundsMax[axis] ) * 0.5;
		extent[axis] = ( static_cast<double>( boundsMax[axis] ) - boundsMin[axis] ) * 0.5;
	}
	double delta[3], worldExtent[3];
	for ( int row = 0; row < 3; ++row ) {
		delta[row] = static_cast<double>( modelMatrix[12 + row] ) - vLight->globalLightOrigin[row];
		worldExtent[row] = 0.0;
		for ( int column = 0; column < 3; ++column ) {
			const double coefficient = modelMatrix[column * 4 + row];
			delta[row] += coefficient * center[column];
			worldExtent[row] += std::fabs( coefficient ) * extent[column];
		}
		if ( !std::isfinite( delta[row] ) || !std::isfinite( worldExtent[row] ) ) {
			return false;
		}
	}
	// The world AABB encloses scale, shear, rotation, and reflections. Its
	// support radius for each 45-degree plane is the sum of the two relevant
	// extents: tighter and cheaper than an enclosing sphere (no square root).
	const int axis = cubeFace >> 1;
	const double forward = ( cubeFace & 1 ) ? -delta[axis] : delta[axis];
	for ( int side = 1; side <= 2; ++side ) {
		const int other = ( axis + side ) % 3;
		if ( forward - std::fabs( delta[other] ) + worldExtent[axis] + worldExtent[other] < -1.0 ) {
			return true;
		}
	}
	return false;
}

const char *R_ShadowMapLightClassName( shadowMapLightClass_t lightClass ) {
	switch ( lightClass ) {
	case SHADOWMAP_LIGHT_POINT:
		return "point";
	case SHADOWMAP_LIGHT_PARALLEL:
		return "parallel";
	case SHADOWMAP_LIGHT_GLOBAL:
		return "global";
	default:
		return "projected";
	}
}
