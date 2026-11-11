// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __SHADOWMAP_PROJECTED_H__
#define __SHADOWMAP_PROJECTED_H__

#include "ShadowMapClassification.h"

typedef struct viewDef_s viewDef_t;
typedef struct viewLight_s viewLight_t;

static const int SHADOWMAP_PROJECTED_MAX_CASCADES = SHADOWMAP_CLASSIFICATION_MAX_CASCADES;
static const int SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT = 23;

typedef enum {
	SHADOWMAP_PROJECTED_FALLBACK_NONE = 0,
	SHADOWMAP_PROJECTED_FALLBACK_INSUFFICIENT_VALID_SAMPLES,
	SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS,
	SHADOWMAP_PROJECTED_FALLBACK_COLLAPSED_BOUNDS
} shadowMapProjectedFallbackReason_t;

typedef struct shadowMapProjectedCascadeFit_s {
	bool				attempted;
	bool				valid;
	bool				mixedWSigns;
	bool				collapsedBounds;
	int					fallbackReason;
	int					sampleCount;
	int					validPoints;
	int					skippedPoints;
	int					positiveWPoints;
	int					negativeWPoints;
	int					nearZeroWPoints;
	int					nanWPoints;
	int					invalidNdcPoints;
	float				sliceNear;
	float				sliceFar;
} shadowMapProjectedCascadeFit_t;

typedef struct shadowMapProjectedLightState_s {
	bool				valid;
	bool				cascadeFallback;
	float				projectionPad;
	float				projectionScale;
	int					fallbackReason;
	int					requestedCascadeCount;
	int					fallbackCascade;
	int					cascadeCount;
	int					atlasDiv;
	int					tileSize;
	idPlane				baseClipPlanes[4];
	float				baseShadowMatrix[16];
	float				splitDepths[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				biasScale[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				texelDepthBias[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				worldTexelSize[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				sliceNear[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				sliceFar[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				depthRange[SHADOWMAP_PROJECTED_MAX_CASCADES];
	float				clipZExtent[SHADOWMAP_PROJECTED_MAX_CASCADES];
	idPlane				clipPlanes[SHADOWMAP_PROJECTED_MAX_CASCADES][4];
	idVec4				atlasRect[SHADOWMAP_PROJECTED_MAX_CASCADES];
	shadowMapProjectedCascadeFit_t cascadeFit[SHADOWMAP_PROJECTED_MAX_CASCADES];
} shadowMapProjectedLightState_t;

void R_ShadowMapResetProjectedLightState( shadowMapProjectedLightState_t &state );
const char *R_ShadowMapProjectedFallbackReasonName( int reason );
float R_ShadowMapProjectionPad();
float R_ShadowMapProjectionScale( float projectionPad );
void R_ShadowMapBuildClipPlanes( const idPlane lightProject[4], idPlane clipPlanes[4] );
bool R_ShadowMapBuildParallelClipPlanes( const viewLight_t *vLight, idPlane clipPlanes[4] );
bool R_ShadowMapBuildDistantPointClipPlanes( const viewLight_t *vLight, idPlane clipPlanes[4], float *farWidth = NULL );
void R_ShadowMapBuildBaseClipPlanesForLight( const viewLight_t *vLight, idPlane clipPlanes[4] );
// Whether a projection shows the light's view mirrored, which swaps the face
// the rasterizer reports for every triangle. A cube face is a camera at the
// light and never does. A projected map takes its S and T rows from the
// light's authored right and up vectors, or from the pair
// idVec3::NormalVectors hands a parallel or distant point light, and either
// set can be left-handed against the direction the light travels: the stock
// projector with its right vector the other way round, and every parallel and
// distant point light. The reference is the right-handed authored projector,
// for which discarding the rasterizer's back face keeps the triangles that
// face away from the light.
bool R_ShadowMapClipPlanesMirrorWinding( const idPlane clipPlanes[4] );
bool R_ShadowMapLightMirrorsCasterWinding( const viewLight_t *vLight );
void R_ShadowMapClipPlanesToGLMatrix( const idPlane clipPlanes[4], float matrix[16] );
idVec4 R_ShadowMapBuildAtlasRect( int cascadeIndex, int atlasDiv );
float R_ShadowMapTexelDepthBias( float worldTexelSize, float depthRange );
// Stored depth that one shadow texel of a receiver spans per unit of slope
// (tan of its angle to the light); R_ShadowMapTexelDepthBias scales it by
// r_shadowMapTexelBiasScale. Projected receivers add step * slope * the tap's
// distance from the centre, in texels, to each off-centre filter tap's bias.
float R_ShadowMapTexelDepthStep( float worldTexelSize, float depthRange );
// The step for every cascade of a projected state, zero for unused cascades.
void R_ShadowMapProjectedTexelDepthSteps( const shadowMapProjectedLightState_t &state, float steps[SHADOWMAP_PROJECTED_MAX_CASCADES] );
float R_ShadowMapQuantizeCascadeExtent( float rawExtent, int tileSize );
float R_ShadowMapSnapCascadeCenter( float rawCenter, float quantizedExtent, int tileSize );
int R_ShadowMapProjectedStateHash( int hash, const shadowMapProjectedLightState_t &state );
// pins the stabilization guarantees (shadow plan invariant I5)
bool R_ShadowMapCascadeStabilitySelfTest( void );
bool R_ShadowMapDistantPointProjectionSelfTest( void );
bool R_ShadowMapCasterFacesSelfTest( void );
void R_BuildShadowMapProjectedLightState( const viewLight_t *vLight, const viewDef_t *viewDef, int tileSize, shadowMapProjectedLightState_t &state );

#endif /* !__SHADOWMAP_PROJECTED_H__ */
