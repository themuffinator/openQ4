// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __SHADOWMAP_CLASSIFICATION_H__
#define __SHADOWMAP_CLASSIFICATION_H__

typedef struct viewLight_s viewLight_t;

static const int SHADOWMAP_CLASSIFICATION_MAX_CASCADES = 4;

typedef enum {
	SHADOWMAP_LIGHT_PROJECTED = 0,
	SHADOWMAP_LIGHT_POINT,
	SHADOWMAP_LIGHT_PARALLEL,
	SHADOWMAP_LIGHT_GLOBAL
} shadowMapLightClass_t;

typedef struct shadowMapLightClassification_s {
	shadowMapLightClass_t	lightClass;
	bool					projectedLight;
	bool					pointLight;
	bool					ordinaryProjectedLight;
	bool					parallelLight;
	bool					globalLight;
	bool					projectedCSMGateApplies;
	bool					projectedCSMEnabled;
	bool					csmEnabled;
	int						cascadeCount;
	int						atlasDiv;
	int						tileCount;
} shadowMapLightClassification_t;

// Receiver filtering is expressed in shadow texels, but a texel from a
// parallel/global (sky) projection covers substantially more world space than
// one from a local projector.  Keep the source-aware policy in one place so
// the cascade fitter, legacy receiver, and modern descriptor agree.
typedef struct shadowMapProjectedFilterSettings_s {
	bool					distantSource;
	float				filterScale;
	float				filterRadius;
	int					filterTaps;
	int					filterMode;
	float				pcssLightRadius;
	float				pcssMaxRadius;
	float				effectiveFilterRadius;
} shadowMapProjectedFilterSettings_t;

// Point receivers express depth bias in normalized radial depth and normal
// offset in cube texels.  On very large authored lights both therefore grow
// into a large world-space displacement.  Keep their common CPU policy in one
// place so classic/shared GL and Vulkan upload identical effective values.
static const float SHADOWMAP_POINT_RECEIVER_MAX_SLOPE = 4.0f;

typedef struct shadowMapPointReceiverSettings_s {
	float				constantBias;
	float				normalBias;
	float				texelBiasScale;
	float				normalOffsetScale;
	float				worldBiasScale;
} shadowMapPointReceiverSettings_t;

shadowMapLightClassification_t R_ClassifyShadowMapLight( const viewLight_t *vLight );
// Preserve projection coefficients exactly: quantizing to world-coordinate
// precision can erase entire rows of a large light's projection.
int R_ShadowMapHashFloat( int hash, float value );
shadowMapProjectedFilterSettings_t R_ShadowMapProjectedFilterSettings( const viewLight_t *vLight );
float R_ShadowMapPointFarDistance( const viewLight_t *vLight );
shadowMapPointReceiverSettings_t R_ClampShadowMapPointReceiverSettings(
	float farDistance, int faceSize, float constantBias, float normalBias,
	float texelBiasScale, float normalOffsetScale, float maxWorldBias );
shadowMapPointReceiverSettings_t R_ShadowMapPointReceiverSettings(
	float farDistance, int faceSize );
shadowMapPointReceiverSettings_t R_ShadowMapPointStorageAdjustedReceiverSettings(
	const shadowMapPointReceiverSettings_t &baseSettings,
	float farDistance, int faceSize, bool depthCompare, bool highPrecision );
// Mirrored or invalid model transforms make authored front-face winding
// unreliable. Invalid/unknown inputs return true so AUTO culling can fall
// back conservatively to two-sided depth.
bool R_ShadowMapCasterTransformNeedsTwoSided( const float modelMatrix[ 16 ] );
// A sealed hull is only safe for one-sided near-shell rendering when its
// light is outside the caster. Invalid/unknown inputs return true so AUTO
// culling fails conservatively to two-sided depth.
bool R_ShadowMapLightOriginInsideCasterBounds( const viewLight_t *vLight,
	const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
	const float boundsMax[ 3 ] );
// Conservative cube-face rejection, shared by GL and Vulkan. Invalid bounds
// or transforms remain visible. Face order is +X,-X,+Y,-Y,+Z,-Z.
bool R_ShadowMapCasterOutsidePointFace( const viewLight_t *vLight,
	const float modelMatrix[ 16 ], const float boundsMin[ 3 ],
	const float boundsMax[ 3 ], int cubeFace );
const char *R_ShadowMapLightClassName( shadowMapLightClass_t lightClass );

#endif /* !__SHADOWMAP_CLASSIFICATION_H__ */
