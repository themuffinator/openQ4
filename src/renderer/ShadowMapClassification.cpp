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

bool R_ShadowMapUsesDistantPointProjection( const renderLight_t &parms ) {
	if ( !parms.pointLight || parms.parallel ) {
		return false;
	}
	idVec3 direction = parms.lightCenter;
	const float distance = direction.Normalize();
	const float extent = idMath::Fabs( direction.x * parms.lightRadius.x )
		+ idMath::Fabs( direction.y * parms.lightRadius.y )
		+ idMath::Fabs( direction.z * parms.lightRadius.z );
	return std::isfinite( static_cast<double>( distance ) )
		&& std::isfinite( static_cast<double>( extent ) )
		&& distance > extent + Max( 1.0f, distance * 0.01f );
}

shadowMapLightClassification_t R_ClassifyShadowMapLight( const viewLight_t *vLight ) {
	shadowMapLightClassification_t classification;
	memset( &classification, 0, sizeof( classification ) );

	classification.lightClass = R_ShadowMapLightClassForViewLight( vLight );
	// Parallel (sun) lights carry pointLight=true with a faked far-away origin;
	// radial cube-map depth saturates for them, so they route through the
	// projected machinery with a synthesized orthographic projection instead
	// (R_ShadowMapBuildParallelClipPlanes).
	classification.distantPointLight = vLight != NULL && vLight->lightDef != NULL
		&& r_shadowMapPointLights.GetBool()
		&& R_ShadowMapUsesDistantPointProjection( vLight->lightDef->parms );
	classification.pointLight = vLight != NULL && vLight->pointLight && !vLight->parallel
		&& !classification.distantPointLight;
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
	// policy. This specialization covers parallel sunlight, global projectors,
	// and distant point sources fitted into the projected path.
	settings.distantSource = classification.projectedLight && vLight != NULL
		&& ( vLight->parallel || classification.globalLight || classification.distantPointLight );
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

int R_ShadowMapDynamicCasterFramesFor( float frameMsec ) {
	if ( !std::isfinite( static_cast<double>( frameMsec ) ) || frameMsec <= 0.0f ) {
		return SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES;
	}
	const double frames = std::ceil(
		static_cast<double>( SHADOWMAP_DYNAMIC_CASTER_WINDOW_MSEC ) / frameMsec );
	if ( frames >= static_cast<double>( SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES ) ) {
		return SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES;
	}
	return Max( SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES, static_cast<int>( frames ) );
}

// A moving light is updated once a game tic, so on a fast display it holds
// still on the frames between tics. Seventy milliseconds bridge two tics at
// thirty or sixty a second. The casters' quarter second is not used: it would
// keep a light that has stopped out of the cache that long, drawing its maps
// afresh every frame meanwhile.
static const int SHADOWMAP_MOVING_LIGHT_WINDOW_MSEC = 70;

int R_ShadowMapMovingLightFramesFor( float frameMsec ) {
	if ( !std::isfinite( static_cast<double>( frameMsec ) ) || frameMsec <= 0.0f ) {
		return SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES;
	}
	const double frames = std::ceil(
		static_cast<double>( SHADOWMAP_MOVING_LIGHT_WINDOW_MSEC ) / frameMsec ) + 1.0;
	if ( frames >= static_cast<double>( SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES ) ) {
		return SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES;
	}
	return Max( 2, static_cast<int>( frames ) );
}

// Written once per frame before the front end runs; front-end jobs only read.
static int shadowMapMovingLightFrames = SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES;
static int shadowMapDynamicCasterFrames = SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES;
static int shadowMapLastFrameMsec = 0;
static float shadowMapSmoothedFrameMsec = 0.0f;

void R_ShadowMapBeginFrame( int nowMsec ) {
	const int elapsed = nowMsec - shadowMapLastFrameMsec;
	shadowMapLastFrameMsec = nowMsec;
	if ( elapsed <= 0 || elapsed > 1000 ) {
		// first frame, a clock that did not advance, or a load: keep the estimate
		if ( shadowMapSmoothedFrameMsec <= 0.0f ) {
			shadowMapDynamicCasterFrames = SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES;
		}
		return;
	}
	// A fast frame shortens the interval at once and a slow one lengthens it
	// gradually, so a hitch cannot close the window on a moving caster.
	const float sample = static_cast<float>( elapsed );
	if ( shadowMapSmoothedFrameMsec <= 0.0f || sample < shadowMapSmoothedFrameMsec ) {
		shadowMapSmoothedFrameMsec = sample;
	} else {
		shadowMapSmoothedFrameMsec += ( sample - shadowMapSmoothedFrameMsec ) * 0.05f;
	}
	shadowMapDynamicCasterFrames =
		R_ShadowMapDynamicCasterFramesFor( shadowMapSmoothedFrameMsec );
	shadowMapMovingLightFrames =
		R_ShadowMapMovingLightFramesFor( shadowMapSmoothedFrameMsec );
}

bool R_ShadowMapLightIsMoving( const idRenderLightLocal *lightDef, int frame ) {
	return lightDef != NULL && lightDef->lastMovedFrameNum > 0
		&& lightDef->lastMovedFrameNum >= frame - shadowMapMovingLightFrames;
}

bool R_ShadowMapMovingLightSkipsCache( const viewLight_t *vLight, int frame ) {
	return !r_shadowMapCacheMovingLights.GetBool() && vLight != NULL
		&& R_ShadowMapLightIsMoving( vLight->lightDef, frame );
}

int R_ShadowMapDynamicCasterFrames( void ) {
	return shadowMapDynamicCasterFrames;
}

int R_ShadowMapPointCacheSlotLimitFor( int requestedSlots, int budgetMegs,
		int faceSize, int bytesPerTexel, int maxSlots ) {
	const int slotCeiling = idMath::ClampInt( 0,
		SHADOWMAP_POINT_CACHE_MAX_SLOTS, maxSlots );
	const int requested = idMath::ClampInt( 0, slotCeiling, requestedSlots );
	if ( requested <= SHADOWMAP_POINT_CACHE_MIN_BUDGET_SLOTS ) {
		return requested;
	}
	// 64-bit: six 2048 faces of eight bytes are 201 MiB each.
	const double entryBytes = 6.0
		* static_cast<double>( Max( 1, faceSize ) )
		* static_cast<double>( Max( 1, faceSize ) )
		* static_cast<double>( Max( 1, bytesPerTexel ) );
	const double budgetBytes = static_cast<double>( Max( 0, budgetMegs ) )
		* 1048576.0;
	const double affordable = std::floor( budgetBytes / entryBytes );
	const int budgetSlots = affordable >= static_cast<double>( slotCeiling )
		? slotCeiling : static_cast<int>( affordable );
	return Min( requested,
		Max( SHADOWMAP_POINT_CACHE_MIN_BUDGET_SLOTS, budgetSlots ) );
}

int R_ShadowMapPointCacheSlotLimit( int faceSize, int bytesPerTexel,
		int maxSlots ) {
	return R_ShadowMapPointCacheSlotLimitFor(
		r_shadowMapPointCacheSize.GetInteger(),
		r_shadowMapPointCacheMegs.GetInteger(),
		faceSize, bytesPerTexel, maxSlots );
}

int R_ShadowMapAtlasBlockEdge( int tileSize, int atlasDiv ) {
	if ( tileSize <= 0 || atlasDiv <= 0 ) {
		return 0;
	}
	const long long blockSize = static_cast<long long>( tileSize ) * atlasDiv;
	long long edge = 1;
	while ( edge < blockSize ) {
		edge <<= 1;
	}
	return edge > 0x40000000 ? 0 : static_cast<int>( edge );
}

int R_ShadowMapAtlasRootEdge( int atlasSize ) {
	if ( atlasSize <= 0 ) {
		return 0;
	}
	int edge = 1;
	while ( edge <= atlasSize / 2 ) {
		edge <<= 1;
	}
	return edge;
}

int R_ShadowMapAtlasBlockCapacity( int atlasSize, int tileSize, int atlasDiv ) {
	const int blockEdge = R_ShadowMapAtlasBlockEdge( tileSize, atlasDiv );
	const int rootEdge = R_ShadowMapAtlasRootEdge( atlasSize );
	if ( blockEdge <= 0 || rootEdge <= 0 ) {
		return 0;
	}
	const int perRow = rootEdge / blockEdge;
	return perRow * perRow;
}

int R_ShadowMapAtlasMinTileSize( int tileSize ) {
	return Min( tileSize, Max( 128, tileSize / 4 ) );
}

static long long R_ShadowMapAtlasRequestArea( const shadowMapAtlasRequest_t &request ) {
	const long long edge = R_ShadowMapAtlasBlockEdge( request.tileSize, request.atlasDiv );
	return edge * edge * Max( 1, request.passes );
}

bool R_ShadowMapFitAtlasTiles( int atlasSize, shadowMapAtlasRequest_t *requests, int count ) {
	const long long rootEdge = R_ShadowMapAtlasRootEdge( atlasSize );
	const long long capacity = rootEdge * rootEdge;
	if ( requests == NULL || count <= 0 ) {
		return true;
	}
	count = Min( count, SHADOWMAP_ATLAS_MAX_REQUESTS );

	long long demand = 0;
	int order[SHADOWMAP_ATLAS_MAX_REQUESTS];
	for ( int i = 0; i < count; ++i ) {
		demand += R_ShadowMapAtlasRequestArea( requests[i] );
		// ascending extent; among equals the later light gives way first
		int slot = i;
		while ( slot > 0 && requests[order[slot - 1]].extent >= requests[i].extent ) {
			order[slot] = order[slot - 1];
			--slot;
		}
		order[slot] = i;
	}

	// One halving for each light in turn before any light takes a second:
	// the view loses resolution evenly from its smallest lights up.
	while ( demand > capacity ) {
		bool halved = false;
		for ( int i = 0; i < count && demand > capacity; ++i ) {
			shadowMapAtlasRequest_t &request = requests[order[i]];
			const int smaller = request.tileSize / 2;
			if ( smaller < request.minTileSize || smaller < 1 ) {
				continue;
			}
			demand -= R_ShadowMapAtlasRequestArea( request );
			request.tileSize = smaller;
			demand += R_ShadowMapAtlasRequestArea( request );
			halved = true;
		}
		if ( !halved ) {
			break;
		}
	}
	return demand <= capacity;
}

int R_ShadowMapProjectedTileSize( const viewLight_t *vLight, int atlasDiv, int maxBlockSize ) {
	const int maxTileSize = maxBlockSize / Max( 1, atlasDiv );
	if ( maxTileSize < 128 ) {
		return 0;
	}
	const int wanted = ( vLight != NULL && vLight->shadowMapTileSize > 0 )
		? vLight->shadowMapTileSize : r_shadowMapSize.GetInteger();
	return idMath::ClampInt( 128, maxTileSize, wanted );
}

long long R_ShadowMapProjectedCacheTexelBudgetFor( int cacheSize, int tileSize ) {
	const long long tiles = idMath::ClampInt( 0, 16, cacheSize );
	const long long edge = idMath::ClampInt( 128, 4096, tileSize );
	return tiles * edge * edge;
}

long long R_ShadowMapProjectedCacheTexelBudget( void ) {
	return R_ShadowMapProjectedCacheTexelBudgetFor(
		r_shadowMapProjectedCacheSize.GetInteger(), r_shadowMapSize.GetInteger() );
}

bool R_ShadowMapProjectedCacheAdmits( long long budgetTexels, long long residentTexels, int tileSize ) {
	if ( tileSize <= 0 || budgetTexels <= 0 ) {
		return false;
	}
	const long long tile = static_cast<long long>( tileSize ) * tileSize;
	return Max( 0LL, residentTexels ) + tile <= budgetTexels;
}

// Pins the pure policies above: the point cache holds a dense view's lights
// at the shipped cube size inside its memory bound, never fewer than the
// twelve slots it always had; the live-caster window spans a game tic at
// every display rate; a view's atlas lights are cut down, smallest first,
// until they fit; and the projected cache is counted in texels, so smaller
// tiles take a proportional share of it.
bool R_ShadowMapCachePolicySelfTest( void ) {
	// Quality and Ultra cut four tiles, Enhanced sixteen; Maximum's cascades
	// take a quarter of its atlas each
	if ( R_ShadowMapAtlasBlockCapacity( 4096, 2048, 1 ) != 4
			|| R_ShadowMapAtlasBlockCapacity( 8192, 4096, 1 ) != 4
			|| R_ShadowMapAtlasBlockCapacity( 4096, 1024, 1 ) != 16
			|| R_ShadowMapAtlasBlockCapacity( 16384, 4096, 2 ) != 4
			|| R_ShadowMapAtlasBlockCapacity( 4096, 2048, 2 ) != 1
			|| R_ShadowMapAtlasBlockCapacity( 4096, 4096, 2 ) != 0
			|| R_ShadowMapAtlasBlockCapacity( 0, 1024, 1 ) != 0
			|| R_ShadowMapAtlasBlockCapacity( 4096, 0, 1 ) != 0
			// blocks are power-of-two squares inside a power-of-two root
			|| R_ShadowMapAtlasBlockEdge( 1500, 1 ) != 2048
			|| R_ShadowMapAtlasBlockEdge( 1024, 2 ) != 2048
			|| R_ShadowMapAtlasRootEdge( 6000 ) != 4096
			|| R_ShadowMapAtlasRootEdge( 4096 ) != 4096
			|| R_ShadowMapAtlasMinTileSize( 2048 ) != 512
			|| R_ShadowMapAtlasMinTileSize( 256 ) != 128
			|| R_ShadowMapAtlasMinTileSize( 128 ) != 128 ) {
		return false;
	}
	{
		// Quality: two lights with both ownerships fill the four tiles and
		// keep their size
		shadowMapAtlasRequest_t fits[2] = {
			{ 2048, 512, 1, 2, 300.0f }, { 2048, 512, 1, 2, 900.0f } };
		if ( !R_ShadowMapFitAtlasTiles( 4096, fits, 2 )
				|| fits[0].tileSize != 2048 || fits[1].tileSize != 2048 ) {
			return false;
		}
		// a third light: the two smallest halve, the largest keeps its tile
		shadowMapAtlasRequest_t three[3] = {
			{ 2048, 512, 1, 2, 900.0f }, { 2048, 512, 1, 2, 300.0f }, { 2048, 512, 1, 2, 600.0f } };
		if ( !R_ShadowMapFitAtlasTiles( 4096, three, 3 )
				|| three[0].tileSize != 2048 || three[1].tileSize != 1024 || three[2].tileSize != 1024 ) {
			return false;
		}
		// thirty-two lights at the floor are the most the atlas takes
		shadowMapAtlasRequest_t many[33];
		for ( int i = 0; i < 33; ++i ) {
			const shadowMapAtlasRequest_t request = { 2048, 512, 1, 2, static_cast<float>( i ) };
			many[i] = request;
		}
		if ( !R_ShadowMapFitAtlasTiles( 4096, many, 32 ) || many[0].tileSize != 512 || many[31].tileSize != 512 ) {
			return false;
		}
		for ( int i = 0; i < 33; ++i ) {
			many[i].tileSize = 2048;
		}
		if ( R_ShadowMapFitAtlasTiles( 4096, many, 33 ) || many[32].tileSize != 512 ) {
			return false;
		}
		// a cascaded light's block is the whole Quality atlas until it halves
		shadowMapAtlasRequest_t cascaded[2] = {
			{ 2048, 512, 2, 1, 1.0e9f }, { 2048, 512, 1, 1, 400.0f } };
		if ( !R_ShadowMapFitAtlasTiles( 4096, cascaded, 2 )
				|| cascaded[0].tileSize != 1024 || cascaded[1].tileSize != 1024 ) {
			return false;
		}
	}
	{
		// The projected cache is counted in full-size maps. Quality's eight
		// hold eight maps of 2048 tiles and no ninth, as eight slots did, or
		// thirty-two of the 1024 tiles a crowded view is cut to.
		const long long quality = R_ShadowMapProjectedCacheTexelBudgetFor( 8, 2048 );
		const long long full = 2048LL * 2048LL;
		if ( quality != 8 * full
				|| !R_ShadowMapProjectedCacheAdmits( quality, 7 * full, 2048 )
				|| R_ShadowMapProjectedCacheAdmits( quality, 8 * full, 2048 )
				|| !R_ShadowMapProjectedCacheAdmits( quality, 31 * ( full / 4 ), 1024 )
				|| R_ShadowMapProjectedCacheAdmits( quality, 32 * ( full / 4 ), 1024 )
				// a mixed view: two full maps and twenty-four half-size ones
				|| !R_ShadowMapProjectedCacheAdmits( quality, 2 * full + 23 * ( full / 4 ), 1024 )
				// a tile larger than the whole budget is never resident
				|| R_ShadowMapProjectedCacheAdmits( R_ShadowMapProjectedCacheTexelBudgetFor( 1, 1024 ), 0, 2048 )
				// 0 turns the cache off; the count and the tile are clamped
				|| R_ShadowMapProjectedCacheTexelBudgetFor( 0, 2048 ) != 0
				|| R_ShadowMapProjectedCacheAdmits( 0, 0, 128 )
				|| R_ShadowMapProjectedCacheAdmits( quality, 0, 0 )
				|| R_ShadowMapProjectedCacheTexelBudgetFor( 99, 2048 ) != 16 * full
				|| R_ShadowMapProjectedCacheTexelBudgetFor( 8, 16 ) != 8LL * 128 * 128 ) {
			return false;
		}
	}
	const int maxSlots = SHADOWMAP_POINT_CACHE_MAX_SLOTS;
	// 512 cubes of colour and depth are 12 MiB each: 42 fit in 512 MiB
	if ( R_ShadowMapPointCacheSlotLimitFor( 64, 512, 512, 8, maxSlots ) != 42 ) {
		return false;
	}
	// depth-only 512 cubes are 6 MiB: the requested 64 fit
	if ( R_ShadowMapPointCacheSlotLimitFor( 64, 512, 512, 4, maxSlots ) != 64 ) {
		return false;
	}
	// 2048 cubes are 192 MiB each, yet the first twelve slots stay available
	if ( R_ShadowMapPointCacheSlotLimitFor( 64, 512, 2048, 8, maxSlots ) != 12 ) {
		return false;
	}
	// an explicit small count is honoured, the record bound is never exceeded
	if ( R_ShadowMapPointCacheSlotLimitFor( 4, 512, 512, 8, maxSlots ) != 4
			|| R_ShadowMapPointCacheSlotLimitFor( 0, 512, 512, 8, maxSlots ) != 0
			|| R_ShadowMapPointCacheSlotLimitFor( 4096, 1 << 20, 128, 4, maxSlots ) != maxSlots
			|| R_ShadowMapPointCacheSlotLimitFor( 64, 512, 512, 8, 16 ) != 16 ) {
		return false;
	}
	// 60 Hz tics are 16.7 ms apart: the window must exceed one tic's worth of
	// frames at 60, 144, 240 and 1000 frames a second, with room to spare
	const float frameMsec[] = { 16.667f, 6.944f, 4.167f, 1.0f };
	for ( int i = 0; i < 4; ++i ) {
		const int frames = R_ShadowMapDynamicCasterFramesFor( frameMsec[i] );
		const float ticFrames = 16.667f / frameMsec[i];
		if ( frames < SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES
				|| frames > SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES
				|| static_cast<float>( frames ) < ticFrames * 4.0f ) {
			return false;
		}
	}
	// a stalled or broken clock holds casters live rather than freezing them
	return R_ShadowMapDynamicCasterFramesFor( 0.0f ) == SHADOWMAP_DYNAMIC_CASTER_MAX_FRAMES
		&& R_ShadowMapDynamicCasterFramesFor( 1000.0f ) == SHADOWMAP_DYNAMIC_CASTER_MIN_FRAMES;
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

// The registers a cutout stage's coverage is read from: whether the stage is
// on, its alpha scale, its alpha reference and its texture matrix. A register
// is constant when the parser folded it to a literal; one that follows time,
// an entity parm, a global or a table is written by an expression every view
// (idMaterial::GetConstantRegisterValue). The material's whole-register
// constness (idMaterial::ConstantRegisters()) is not asked: an animated glow
// stage beside a fixed alpha stage does not move the holes, and that answer
// also follows r_useConstantMaterials.
static bool R_ShadowMapCutoutStageRegistersConstant( const idMaterial *shader, const shaderStage_t *pStage ) {
	float value = 0.0f;
	if ( !shader->GetConstantRegisterValue( pStage->conditionRegister, value )
			|| !shader->GetConstantRegisterValue( pStage->color.registers[3], value )
			|| !shader->GetConstantRegisterValue( pStage->alphaTestRegister, value ) ) {
		return false;
	}
	if ( pStage->texture.hasMatrix ) {
		for ( int row = 0; row < 2; ++row ) {
			for ( int column = 0; column < 3; ++column ) {
				if ( !shader->GetConstantRegisterValue( pStage->texture.matrix[row][column], value ) ) {
					return false;
				}
			}
		}
	}
	return true;
}

bool R_ShadowMapCutoutCoverageAnimates( const idMaterial *shader ) {
	if ( shader == NULL || shader->Coverage() != MC_PERFORATED ) {
		return false;
	}
	if ( !r_shadowMapStaticCutouts.GetBool() || shader->Deform() != DFRM_NONE ) {
		return true;
	}
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; ++stage ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( pStage == NULL || !pStage->hasAlphaTest ) {
			continue;
		}
		if ( !R_ShadowMapCutoutStageRegistersConstant( shader, pStage ) ) {
			return true;
		}
		// a video or a render target changes under a constant material
		if ( pStage->texture.cinematic != NULL || pStage->texture.dynamic != DI_STATIC ) {
			return true;
		}
		// An image that is not there yet is not the coverage the cutout will
		// have: stay live until it is, and the signature then renders the map.
		const idImage *image = pStage->texture.image;
		if ( image == NULL || !image->IsLoaded() || image->IsDefaulted() ) {
			return true;
		}
	}
	return false;
}

int R_ShadowMapCutoutCoverageSignature( const idMaterial *shader, int hash ) {
	if ( shader == NULL || shader->Coverage() != MC_PERFORATED ) {
		return hash;
	}
	unsigned int h = static_cast<unsigned int>( hash );
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; ++stage ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( pStage == NULL || !pStage->hasAlphaTest ) {
			continue;
		}
		const idImage *image = pStage->texture.image;
		const uint64_t generation = image != NULL ? image->GetStorageGeneration() : 0;
		h = ( h ^ static_cast<unsigned int>( generation ) ) * 16777619u;
		h = ( h ^ static_cast<unsigned int>( generation >> 32 ) ) * 16777619u;
		h = ( h ^ static_cast<unsigned int>( pStage->alphaTestMode ) ) * 16777619u;
	}
	h = ( h ^ ( r_shadowMapHashedAlpha.GetBool() ? 1u : 0u ) ) * 16777619u;
	h = ( h ^ ( r_shadowMapStableAlphaHash.GetBool() ? 1u : 0u ) ) * 16777619u;
	return static_cast<int>( h );
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
