#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
#ifdef OPENQ4_SHADOW_COMPARE
uniform sampler2DShadow uShadowMap;
#else
uniform sampler2D uShadowMap;
#endif
uniform sampler2D uTranslucentShadowMapR;
uniform sampler2D uTranslucentShadowMapG;
uniform sampler2D uTranslucentShadowMapB;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uMaterialEnhanced;
uniform float uMaterialNormalScale;
uniform float uMaterialSpecularBoost;
uniform float uMaterialFresnel;
uniform vec4 uCelParams;
uniform vec4 uFlatDiffuseParams;
uniform vec2 uShadowTexelSize;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uShadowTexelDepthBias[4];
uniform float uShadowTexelDepthStep[4];
uniform float uShadowReceiverPlaneBias;
uniform float uShadowFilterRadius;
uniform float uShadowFilterTaps;
uniform float uShadowFilterMode;
uniform float uShadowPCSSLightRadius;
uniform float uShadowPCSSMaxRadius;
uniform vec4 uShadowAtlasRect[4];
uniform float uShadowSplitDepths[4];
uniform float uShadowCascadeBiasScale[4];
uniform int uShadowCascadeCount;
uniform float uShadowCascadeBlend;
uniform float uShadowDebugMode;
uniform float uShadowReceiverDebugReason;
uniform float uTranslucentShadowEnabled;
uniform float uTranslucentShadowDensity;
uniform float uTranslucentShadowFilterRadius;
uniform float uTranslucentShadowMinVariance;
uniform float uTranslucentShadowBleedReduction;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
varying vec4 vShadowCoord0;
varying vec4 vShadowCoord1;
varying vec4 vShadowCoord2;
varying vec4 vShadowCoord3;
varying vec3 vVertexColor;
varying float vShadowLightCos;
varying float vViewDepth;

const float kShadowCoordWEpsilon = 1.0e-5;
const float kShadowCoordMaxMagnitude = 65536.0;
const float kShadowBiasMinLightCos = 0.20;
const float kShadowBiasMaxSlope = 4.0;
const float kShadowDebugAtlas = 1.0;
const float kShadowDebugCascadeIndex = 2.0;
const float kShadowDebugProjectedUV = 3.0;
const float kShadowDebugProjectedDepth = 4.0;
const float kShadowDebugProjectedW = 5.0;
const float kShadowDebugInvalidMask = 6.0;
const float kShadowDebugBiasHeatmap = 7.0;
const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugCasterOffsetOff = 10.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;
const float kShadowDebugCompareDelta = 12.0;
const float kShadowDebugReceiverEligibility = 13.0;
const float kShadowDebugReceiverFallbackReason = 14.0;

float gShadowDebugState = 0.0;

// Per-cascade screen-space depth gradients, computed in main() before any
// divergent control flow: derivatives taken inside the cascade if-chains or
// after data-dependent early returns are undefined at #version 110.
vec4 gShadowDepthGradients = vec4( 0.0 );

bool ProjectShadowCoord( vec4 shadowCoord, out vec2 localUv, out float depth );

bool ShadowDebugModeIs( float mode ) {
	return abs( uShadowDebugMode - mode ) < 0.5;
}

bool ShadowVisualDebugMode() {
	return ( uShadowDebugMode > 0.5 && uShadowDebugMode < kShadowDebugBiasOff - 0.5 ) ||
		ShadowDebugModeIs( kShadowDebugCompareDelta ) ||
		ShadowDebugModeIs( kShadowDebugReceiverEligibility ) ||
		ShadowDebugModeIs( kShadowDebugReceiverFallbackReason );
}

bool ShadowReceiverDebugMode() {
	return ShadowDebugModeIs( kShadowDebugReceiverEligibility ) ||
		ShadowDebugModeIs( kShadowDebugReceiverFallbackReason );
}

bool ShadowCoordComponentInvalid( float value ) {
	return value != value || abs( value ) > kShadowCoordMaxMagnitude;
}

bool ShadowCoordProjectedInvalid( vec3 value ) {
	return ShadowCoordComponentInvalid( value.x ) || ShadowCoordComponentInvalid( value.y ) || ShadowCoordComponentInvalid( value.z );
}

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

// uFlatDiffuseParams = ( strength, local min Z, inverse height, upward phase ).
// A wrapped, feathered band avoids a visible reset when it reaches the top.
vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ ) {
	if ( uFlatDiffuseParams.x <= 0.0 ) {
		return diffuse;
	}
	float height = clamp( ( localZ - uFlatDiffuseParams.y ) * uFlatDiffuseParams.z, 0.0, 1.0 );
	float distanceToBand = abs( height - fract( uFlatDiffuseParams.w ) );
	distanceToBand = min( distanceToBand, 1.0 - distanceToBand );
	float band = 1.0 - smoothstep( 0.045, 0.16, distanceToBand );
	return mix( diffuse, vec3( 1.0 ), uFlatDiffuseParams.x * band );
}

// ---------------------------------------------------------------------------
// Cel banding. uCelParams is ( bandsEnabled, bandCount, hardSpecular, softness ).
// The same ladder is shared with R_CelQuantizeUnitValue on the CPU side.
// ---------------------------------------------------------------------------

float CelSteps() {
	return max( uCelParams.y - 1.0, 1.0 );
}

// Places a 0..1 value on the band ladder. uCelParams.w widens every boundary
// into a smoothstep centred exactly where the hard step would have landed, so
// the plateaus survive but the transition stops being one texel wide - which
// is what kept a terminator crawling across a curved surface as the camera
// moved. At 0 this is floor( value * steps + 0.5 ) / steps again, to the bit.
float CelLadder( float value ) {
	float steps = CelSteps();
	float scaled = value * steps;

	float softness = clamp( uCelParams.w, 0.0, 1.0 );
	if ( softness <= 0.0 ) {
		return floor( scaled + 0.5 ) / steps;
	}

	// floor( scaled ) names the band below this value and the boundary above it
	// sits half a band away, so the blend window is centred on 0.5.
	float lower = floor( scaled );
	float halfWidth = softness * 0.5;
	float blend = smoothstep( 0.5 - halfWidth, 0.5 + halfWidth, scaled - lower );

	return ( lower + blend ) / steps;
}

// Quantizes a light contribution without shifting its hue: the brightest
// channel picks the band and the others follow it. Black and overbright pass
// through untouched so unlit surfaces stay unlit and headroom is preserved.
vec3 CelQuantizeLight( vec3 light ) {
	if ( uCelParams.x <= 0.5 ) {
		return light;
	}

	float peak = max( max( light.r, light.g ), light.b );
	if ( peak <= 0.0 || peak >= 1.0 ) {
		return light;
	}

	return light * ( CelLadder( peak ) / peak );
}

// Collapses the specular falloff into flat plateaus on the same ladder, which
// is what gives cel highlights their hard edge.
float CelSpecularTerm( float term ) {
	if ( uCelParams.x <= 0.5 || uCelParams.z <= 0.5 ) {
		return term;
	}

	return CelLadder( clamp( term, 0.0, 1.0 ) );
}

vec3 DecodeLocalNormal( vec4 bumpSample ) {
	if ( uMaterialEnhanced < 0.5 ) {
		return SafeNormalize( vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0 );
	}

	vec2 localNormalXY = vec2( bumpSample.a, bumpSample.g ) * 2.0 - 1.0;
	localNormalXY *= max( uMaterialNormalScale, 0.0 );

	float xyLengthSq = dot( localNormalXY, localNormalXY );
	if ( xyLengthSq > 1.0 ) {
		localNormalXY *= inversesqrt( xyLengthSq );
		xyLengthSq = 1.0;
	}

	float encodedZ = max( bumpSample.b * 2.0 - 1.0, 0.0 );
	float reconstructedZ = sqrt( max( 1.0 - xyLengthSq, 0.0 ) );
	return SafeNormalize( vec3( localNormalXY, mix( encodedZ, reconstructedZ, 0.75 ) ) );
}

float EnhancedSpecularTerm( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float ndotv = max( dot( viewDir, localNormal ), 0.0 );
	float gloss = clamp( max( max( specularSample.r, specularSample.g ), specularSample.b ), 0.0, 1.0 );
	float specularPower = mix( 10.0, 40.0, gloss );
	float fresnel = 1.0 + ( pow( 1.0 - ndotv, 5.0 ) * 2.0 * clamp( uMaterialFresnel, 0.0, 1.0 ) );
	return pow( ndoth, specularPower ) * max( uMaterialSpecularBoost, 0.0 ) * fresnel;
}

float LegacySpecularTerm( vec3 halfAngle, vec3 localNormal ) {
	float specular = clamp( dot( halfAngle, localNormal ) * 4.0 - 3.0, 0.0, 1.0 );
	return specular * specular;
}

vec3 InteractionSpecular( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	if ( uMaterialEnhanced >= 0.5 ) {
		return specularSample * uSpecularColor.rgb * CelSpecularTerm( EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample ) );
	}
	return specularSample * ( uSpecularColor.rgb * 2.0 ) * CelSpecularTerm( LegacySpecularTerm( halfAngle, localNormal ) );
}

float ApproxErf( float x ) {
	float s = sign( x );
	float ax = abs( x );
	float t = 1.0 / ( 1.0 + 0.3275911 * ax );
	float y = 1.0 - ( ( ( ( ( 1.061405429 * t - 1.453152027 ) * t ) + 1.421413741 ) * t - 0.284496736 ) * t + 0.254829592 ) * t * exp( -ax * ax );
	return s * y;
}

float NormalCdf( float x ) {
	return 0.5 * ( 1.0 + ApproxErf( x * 0.70710678 ) );
}

// Two of the four ring taps of the five-tap kernel; the others mirror them.
const vec2 kShadowFiveTapRingA = vec2( 0.670820, 0.223607 );
const vec2 kShadowFiveTapRingB = vec2( -0.223607, 0.670820 );

float StableShadowHash( vec3 value ) {
	return fract( sin( dot( value, vec3( 12.9898, 78.233, 37.719 ) ) ) * 43758.5453 );
}

// The kernel turns by a hash of the texel within the tile, not within the
// image, so a soft shadow's grain is the same wherever its tile lives: a
// cached cell of the atlas, another cell later, or the scratch target.
// Keyed on the image texel, the grain changed each time the tile moved.
mat2 ShadowOffsetRotation( vec2 tileTexel, float depth ) {
	if ( uShadowFilterMode < 0.5 ) {
		return mat2( 1.0, 0.0, 0.0, 1.0 );
	}
	float angle = StableShadowHash( vec3( floor( tileTexel ), floor( depth * 1024.0 ) ) ) * 6.2831853;
	float s = sin( angle );
	float c = cos( angle );
	return mat2( c, s, -s, c );
}

float TranslucentFilterRadius() {
	return ( uTranslucentShadowFilterRadius >= 0.0 ) ? uTranslucentShadowFilterRadius : uShadowFilterRadius;
}

float EffectiveShadowFilterRadius() {
	if ( ShadowDebugModeIs( kShadowDebugPCFOff ) ) {
		return 0.0;
	}
	float radius = uShadowFilterRadius;
#ifndef OPENQ4_SHADOW_COMPARE
	if ( uShadowFilterMode > 1.5 ) {
		radius = max( radius, max( max( uShadowPCSSMaxRadius, 0.0 ), max( uShadowPCSSLightRadius, 0.0 ) ) );
	}
#endif
	return radius;
}

float ResolveTranslucentShadowMoments( vec4 moments, float depth ) {
	if ( uTranslucentShadowEnabled < 0.5 ) {
		return 1.0;
	}

	float totalTau = max( moments.x, 0.0 );
	if ( totalTau <= 1.0e-4 ) {
		return 1.0;
	}

	float mean = moments.y / totalTau;
	float variance = max( moments.z / totalTau - mean * mean, max( uTranslucentShadowMinVariance, 1.0e-6 ) );
	float sigma = sqrt( variance );
	float fraction = clamp( NormalCdf( ( depth - mean ) / sigma ), 0.0, 1.0 );
	float bleed = clamp( uTranslucentShadowBleedReduction, 0.0, 0.95 );
	fraction = clamp( ( fraction - bleed ) / max( 1.0 - bleed, 1.0e-4 ), 0.0, 1.0 );
	float tau = totalTau * fraction;
	return exp( -min( tau * max( uTranslucentShadowDensity, 0.0 ), 16.0 ) );
}

vec2 ShadowAtlasGuardBand() {
	float guardRadius = max( 0.5, EffectiveShadowFilterRadius() + 0.75 );
	return uShadowTexelSize * guardRadius;
}

vec4 SampleFilteredMoments( sampler2D momentMap, vec2 uv, vec2 clampMin, vec2 clampMax ) {
	float filterRadius = ShadowDebugModeIs( kShadowDebugPCFOff ) ? 0.0 : TranslucentFilterRadius();
	if ( filterRadius <= 0.0 ) {
		return texture2D( momentMap, uv );
	}

	vec2 tap = uShadowTexelSize * max( filterRadius, 0.5 );
	vec4 moments = texture2D( momentMap, uv );
	moments += texture2D( momentMap, clamp( uv + vec2( -0.5, -0.5 ) * tap, clampMin, clampMax ) );
	moments += texture2D( momentMap, clamp( uv + vec2( 0.5, -0.5 ) * tap, clampMin, clampMax ) );
	moments += texture2D( momentMap, clamp( uv + vec2( -0.5, 0.5 ) * tap, clampMin, clampMax ) );
	moments += texture2D( momentMap, clamp( uv + vec2( 0.5, 0.5 ) * tap, clampMin, clampMax ) );
	return moments * 0.2;
}

float CascadeBiasScale( int cascadeIndex ) {
	if ( cascadeIndex <= 0 ) {
		return uShadowCascadeBiasScale[0];
	}
	if ( cascadeIndex == 1 ) {
		return uShadowCascadeBiasScale[1];
	}
	if ( cascadeIndex == 2 ) {
		return uShadowCascadeBiasScale[2];
	}
	return uShadowCascadeBiasScale[3];
}

float CascadeTexelDepthBias( int cascadeIndex ) {
	if ( cascadeIndex <= 0 ) {
		return uShadowTexelDepthBias[0];
	}
	if ( cascadeIndex == 1 ) {
		return uShadowTexelDepthBias[1];
	}
	if ( cascadeIndex == 2 ) {
		return uShadowTexelDepthBias[2];
	}
	return uShadowTexelDepthBias[3];
}

float CascadeTexelDepthStep( int cascadeIndex ) {
	if ( cascadeIndex <= 0 ) {
		return uShadowTexelDepthStep[0];
	}
	if ( cascadeIndex == 1 ) {
		return uShadowTexelDepthStep[1];
	}
	if ( cascadeIndex == 2 ) {
		return uShadowTexelDepthStep[2];
	}
	return uShadowTexelDepthStep[3];
}

float ShadowDepthGradient( int cascadeIndex ) {
	if ( cascadeIndex <= 0 ) {
		return gShadowDepthGradients.x;
	}
	if ( cascadeIndex == 1 ) {
		return gShadowDepthGradients.y;
	}
	if ( cascadeIndex == 2 ) {
		return gShadowDepthGradients.z;
	}
	return gShadowDepthGradients.w;
}

float ShadowReceiverBias( int cascadeIndex, float depth ) {
	if ( ShadowDebugModeIs( kShadowDebugBiasOff ) ) {
		return 0.0;
	}
	float lightCos = clamp( vShadowLightCos, kShadowBiasMinLightCos, 1.0 );
	float sinTheta = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	float slopeBias = min( sinTheta / lightCos, kShadowBiasMaxSlope );
	float cascadeScale = CascadeBiasScale( cascadeIndex );
	float normalBias = ShadowDebugModeIs( kShadowDebugReceiverPlaneBiasOff ) ? 0.0 : uShadowNormalBias;
	float scalarBias = ( uShadowBias + normalBias * sinTheta ) * cascadeScale;
	float texelBias = CascadeTexelDepthBias( cascadeIndex ) * ( 1.0 + slopeBias );
	float receiverPlaneBias = 0.0;
	if ( uShadowReceiverPlaneBias > 0.5 && !ShadowDebugModeIs( kShadowDebugReceiverPlaneBiasOff ) ) {
		receiverPlaneBias = ShadowDepthGradient( cascadeIndex ) * max( EffectiveShadowFilterRadius(), 1.0 );
	}
	float texelAwareBias = max( texelBias, receiverPlaneBias );
	return max( max( scalarBias, 0.0 ), max( texelAwareBias, 0.0 ) );
}

// Off-centre PCF taps and PCSS blocker probes read the map several texels
// away from the receiver. A receiver at an angle to the light changes depth by
// texelDepthStep * slope per texel, so a tap on the side nearer the light
// would compare the receiver with its own surface and speckle it, more as the
// filter widens. Each tap therefore adds this bias per texel of its distance
// from the centre. The centre sample keeps the plain receiver bias, so contact
// shadows keep their reach.
float ShadowFootprintBias( int cascadeIndex ) {
	if ( ShadowDebugModeIs( kShadowDebugBiasOff ) ) {
		return 0.0;
	}
	float lightCos = clamp( vShadowLightCos, kShadowBiasMinLightCos, 1.0 );
	float sinTheta = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	float slopeBias = min( sinTheta / lightCos, kShadowBiasMaxSlope );
	return max( CascadeTexelDepthStep( cascadeIndex ), 0.0 ) * slopeBias;
}

float SampleShadowCompare( vec2 uv, float depth, int cascadeIndex, float tapBias ) {
	float bias = ShadowReceiverBias( cascadeIndex, depth ) + tapBias;
#ifdef OPENQ4_SHADOW_COMPARE
	return shadow2D( uShadowMap, vec3( uv, depth - bias ) ).r;
#else
	float storedDepth = texture2D( uShadowMap, uv ).r;
	return ( depth - bias <= storedDepth ) ? 1.0 : 0.0;
#endif
}

float RawShadowDepth( vec2 uv ) {
#ifdef OPENQ4_SHADOW_COMPARE
	return shadow2D( uShadowMap, vec3( uv, 0.5 ) ).r;
#else
	return texture2D( uShadowMap, uv ).r;
#endif
}

#ifndef OPENQ4_SHADOW_COMPARE
float ProjectedPCSSRadius( vec2 uv, float depth, int cascadeIndex, vec2 clampMin, vec2 clampMax, mat2 rotation ) {
	// The CPU already applies the distant-source scale to every radius. Keep
	// the contact floor at the configured PCF radius; the larger PCSS search and
	// maximum radii are guard/search bounds, not minimum blur.
	float baseRadius = max( uShadowFilterRadius, 0.0 );
	if ( uShadowFilterMode < 1.5 || uShadowPCSSLightRadius <= 0.0 || uShadowPCSSMaxRadius <= 0.0 ) {
		return baseRadius;
	}

	float compareDepth = depth - ShadowReceiverBias( cascadeIndex, depth );
	float searchRadius = max( uShadowPCSSLightRadius, 0.5 );
	vec2 searchTap = uShadowTexelSize * searchRadius;
	// Without the footprint bias a tilted receiver finds itself as a blocker.
	float searchBias = ShadowFootprintBias( cascadeIndex ) * searchRadius;
	float blockerDepth = 0.0;
	float blockerCount = 0.0;
	float d0 = RawShadowDepth( uv );
	if ( d0 < compareDepth ) {
		blockerDepth += d0;
		blockerCount += 1.0;
	}
	vec2 o1 = rotation * vec2( -0.326212, -0.405805 );
	vec2 o2 = rotation * vec2( -0.840144, -0.073580 );
	vec2 o3 = rotation * vec2( -0.695914, 0.457137 );
	vec2 o4 = rotation * vec2( -0.203345, 0.620716 );
	vec2 o5 = rotation * vec2( 0.962340, -0.194983 );
	vec2 o6 = rotation * vec2( 0.473434, -0.480026 );
	vec2 o7 = rotation * vec2( 0.519456, 0.767022 );
	vec2 o8 = rotation * vec2( 0.185461, -0.893124 );
	float d1 = RawShadowDepth( clamp( uv + o1 * searchTap, clampMin, clampMax ) );
	float d2 = RawShadowDepth( clamp( uv + o2 * searchTap, clampMin, clampMax ) );
	float d3 = RawShadowDepth( clamp( uv + o3 * searchTap, clampMin, clampMax ) );
	float d4 = RawShadowDepth( clamp( uv + o4 * searchTap, clampMin, clampMax ) );
	float d5 = RawShadowDepth( clamp( uv + o5 * searchTap, clampMin, clampMax ) );
	float d6 = RawShadowDepth( clamp( uv + o6 * searchTap, clampMin, clampMax ) );
	float d7 = RawShadowDepth( clamp( uv + o7 * searchTap, clampMin, clampMax ) );
	float d8 = RawShadowDepth( clamp( uv + o8 * searchTap, clampMin, clampMax ) );
	if ( d1 < compareDepth - length( o1 ) * searchBias ) { blockerDepth += d1; blockerCount += 1.0; }
	if ( d2 < compareDepth - length( o2 ) * searchBias ) { blockerDepth += d2; blockerCount += 1.0; }
	if ( d3 < compareDepth - length( o3 ) * searchBias ) { blockerDepth += d3; blockerCount += 1.0; }
	if ( d4 < compareDepth - length( o4 ) * searchBias ) { blockerDepth += d4; blockerCount += 1.0; }
	if ( d5 < compareDepth - length( o5 ) * searchBias ) { blockerDepth += d5; blockerCount += 1.0; }
	if ( d6 < compareDepth - length( o6 ) * searchBias ) { blockerDepth += d6; blockerCount += 1.0; }
	if ( d7 < compareDepth - length( o7 ) * searchBias ) { blockerDepth += d7; blockerCount += 1.0; }
	if ( d8 < compareDepth - length( o8 ) * searchBias ) { blockerDepth += d8; blockerCount += 1.0; }
	if ( blockerCount <= 0.0 ) {
		return 0.0;
	}

	float averageBlocker = blockerDepth / blockerCount;
	float separation = max( compareDepth - averageBlocker, 0.0 );
	float penumbra = separation / max( averageBlocker, 1.0e-4 );
	// Keep the clamp ordered when a user deliberately sets a PCSS maximum
	// below the base PCF radius.  A zero maximum still disables PCSS above.
	float maxRadius = max( baseRadius, uShadowPCSSMaxRadius );
	return clamp( max( baseRadius, penumbra * uShadowPCSSLightRadius ), baseRadius, maxRadius );
}
#endif

vec4 SampleShadowCascade( vec4 shadowCoord, vec4 atlasRect, int cascadeIndex ) {
	if ( shadowCoord.w != shadowCoord.w || shadowCoord.w < kShadowCoordWEpsilon || shadowCoord.w > kShadowCoordMaxMagnitude ) {
		gShadowDebugState = max( gShadowDebugState, 1.0 );
		return vec4( 1.0, 0.5, 0.5, 1.0 );
	}

	vec2 projectedXY = shadowCoord.xy / shadowCoord.w;
	float projectedDepth = shadowCoord.z;
	vec3 projected = vec3( projectedXY, projectedDepth );
	if ( ShadowCoordProjectedInvalid( projected ) ) {
		gShadowDebugState = max( gShadowDebugState, 2.0 );
		return vec4( 1.0, 0.5, 0.5, 1.0 );
	}

	vec2 localUv = projectedXY * 0.5 + 0.5;
	float depth = projectedDepth;

	if ( localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 ) {
		return vec4( 1.0, localUv.x, localUv.y, depth );
	}
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return vec4( 1.0, localUv.x, localUv.y, depth );
	}

	vec2 atlasMin = atlasRect.xy;
	vec2 atlasMax = atlasRect.zw;
	vec2 uv = atlasMin + localUv * ( atlasMax - atlasMin );
	vec2 guardBand = ShadowAtlasGuardBand();
	vec2 clampMin = atlasMin + guardBand;
	vec2 clampMax = atlasMax - guardBand;
	clampMin = min( clampMin, clampMax );
	uv = clamp( uv, clampMin, clampMax );
	// Blocker search and PCF use the same stable kernel orientation. Computing
	// the hash/trigonometry once also keeps PCSS cost bounded per receiver.
	// localUv is the same bit for bit in every placement of the tile, and the
	// tile's edge in texels is exact, so the rotation is too. The clamp is
	// the guard band's, in texels.
	vec2 shadowTexel = max( abs( uShadowTexelSize.xy ), vec2( 1.0e-6 ) );
	vec2 tileTexels = floor( abs( atlasMax - atlasMin ) / shadowTexel + vec2( 0.5 ) );
	vec2 guardTexels = guardBand / shadowTexel;
	vec2 tileTexel = clamp( localUv * tileTexels, guardTexels, max( tileTexels - guardTexels, guardTexels ) );
	mat2 rotation = ShadowOffsetRotation( tileTexel, depth );

	float filterRadius = EffectiveShadowFilterRadius();
#ifndef OPENQ4_SHADOW_COMPARE
	if ( !ShadowDebugModeIs( kShadowDebugPCFOff ) ) {
		filterRadius = ProjectedPCSSRadius( uv, depth, cascadeIndex, clampMin, clampMax, rotation );
	}
#endif
	if ( filterRadius <= 0.0 ) {
		return vec4( SampleShadowCompare( uv, depth, cascadeIndex, 0.0 ), localUv.x, localUv.y, depth );
	}

	vec2 tap = uShadowTexelSize * filterRadius;
	// Each tap's bias grows with its distance from the centre, filterRadius * |o|.
	float tapBias = ShadowFootprintBias( cascadeIndex ) * filterRadius;
	float shadow = 0.0;
	shadow += SampleShadowCompare( uv, depth, cascadeIndex, 0.0 );
	if ( uShadowFilterTaps <= 1.0 ) {
		return vec4( shadow, localUv.x, localUv.y, depth );
	}
	// The first four taps of the nine- and thirteen-tap sets all lie to one
	// side of the centre. Alone they pull the shadow 0.4 radius sideways and
	// soften one side of its edge only, so the five-tap kernel uses a ring.
	vec2 o1 = rotation * vec2( -0.326212, -0.405805 );
	vec2 o2 = rotation * vec2( -0.840144, -0.073580 );
	vec2 o3 = rotation * vec2( -0.695914, 0.457137 );
	vec2 o4 = rotation * vec2( -0.203345, 0.620716 );
	if ( uShadowFilterTaps <= 5.0 ) {
		o1 = rotation * kShadowFiveTapRingA;
		o2 = rotation * kShadowFiveTapRingB;
		o3 = -o1;
		o4 = -o2;
	}
	shadow += SampleShadowCompare( clamp( uv + o1 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o1 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o2 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o2 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o3 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o3 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o4 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o4 ) * tapBias );
	if ( uShadowFilterTaps <= 5.0 ) {
		return vec4( shadow * ( 1.0 / 5.0 ), localUv.x, localUv.y, depth );
	}
	vec2 o5 = rotation * vec2( 0.962340, -0.194983 );
	vec2 o6 = rotation * vec2( 0.473434, -0.480026 );
	vec2 o7 = rotation * vec2( 0.519456, 0.767022 );
	vec2 o8 = rotation * vec2( 0.185461, -0.893124 );
	shadow += SampleShadowCompare( clamp( uv + o5 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o5 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o6 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o6 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o7 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o7 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o8 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o8 ) * tapBias );
	if ( uShadowFilterTaps <= 9.0 ) {
		return vec4( shadow * ( 1.0 / 9.0 ), localUv.x, localUv.y, depth );
	}
	vec2 o9 = rotation * vec2( 0.507431, 0.064425 );
	vec2 o10 = rotation * vec2( 0.896420, 0.412458 );
	vec2 o11 = rotation * vec2( -0.321940, -0.932615 );
	vec2 o12 = rotation * vec2( -0.791559, -0.597705 );
	shadow += SampleShadowCompare( clamp( uv + o9 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o9 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o10 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o10 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o11 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o11 ) * tapBias );
	shadow += SampleShadowCompare( clamp( uv + o12 * tap, clampMin, clampMax ), depth, cascadeIndex, length( o12 ) * tapBias );
	return vec4( shadow * ( 1.0 / 13.0 ), localUv.x, localUv.y, depth );
}

vec3 SampleTranslucentShadowCascade( vec4 shadowCoord, vec4 atlasRect ) {
	if ( uTranslucentShadowEnabled < 0.5 ) {
		return vec3( 1.0 );
	}

	vec2 localUv;
	float depth;
	if ( !ProjectShadowCoord( shadowCoord, localUv, depth ) ) {
		return vec3( 1.0 );
	}
	if ( localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 ) {
		return vec3( 1.0 );
	}
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return vec3( 1.0 );
	}

	vec2 atlasMin = atlasRect.xy;
	vec2 atlasMax = atlasRect.zw;
	vec2 uv = atlasMin + localUv * ( atlasMax - atlasMin );
	vec2 guardBand = ShadowAtlasGuardBand();
	vec2 clampMin = atlasMin + guardBand;
	vec2 clampMax = atlasMax - guardBand;
	clampMin = min( clampMin, clampMax );
	uv = clamp( uv, clampMin, clampMax );

	return vec3(
		ResolveTranslucentShadowMoments( SampleFilteredMoments( uTranslucentShadowMapR, uv, clampMin, clampMax ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredMoments( uTranslucentShadowMapG, uv, clampMin, clampMax ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredMoments( uTranslucentShadowMapB, uv, clampMin, clampMax ), depth ) );
}

float CascadeSplitDepth( int index ) {
	if ( index <= 0 ) {
		return uShadowSplitDepths[0];
	}
	if ( index == 1 ) {
		return uShadowSplitDepths[1];
	}
	if ( index == 2 ) {
		return uShadowSplitDepths[2];
	}
	return uShadowSplitDepths[3];
}

vec4 SampleCascadeByIndex( int index ) {
	if ( index <= 0 ) {
		return SampleShadowCascade( vShadowCoord0, uShadowAtlasRect[0], 0 );
	}
	if ( index == 1 ) {
		return SampleShadowCascade( vShadowCoord1, uShadowAtlasRect[1], 1 );
	}
	if ( index == 2 ) {
		return SampleShadowCascade( vShadowCoord2, uShadowAtlasRect[2], 2 );
	}
	return SampleShadowCascade( vShadowCoord3, uShadowAtlasRect[3], 3 );
}

vec4 ShadowCoordByIndex( int index ) {
	if ( index <= 0 ) {
		return vShadowCoord0;
	}
	if ( index == 1 ) {
		return vShadowCoord1;
	}
	if ( index == 2 ) {
		return vShadowCoord2;
	}
	return vShadowCoord3;
}

bool ProjectShadowCoord( vec4 shadowCoord, out vec2 localUv, out float depth ) {
	if ( shadowCoord.w != shadowCoord.w || shadowCoord.w < kShadowCoordWEpsilon || shadowCoord.w > kShadowCoordMaxMagnitude ) {
		gShadowDebugState = max( gShadowDebugState, 1.0 );
		localUv = vec2( 0.0 );
		depth = 0.0;
		return false;
	}

	vec2 projectedXY = shadowCoord.xy / shadowCoord.w;
	float projectedDepth = shadowCoord.z;
	vec3 projected = vec3( projectedXY, projectedDepth );
	if ( ShadowCoordProjectedInvalid( projected ) ) {
		gShadowDebugState = max( gShadowDebugState, 2.0 );
		localUv = vec2( 0.0 );
		depth = 0.0;
		return false;
	}

	localUv = projectedXY * 0.5 + 0.5;
	depth = projectedDepth;
	return true;
}

int SelectCascade( float viewDepth ) {
	int interiorSplitCount = ( uShadowCascadeCount > 1 ) ? ( uShadowCascadeCount - 1 ) : 0;
	if ( interiorSplitCount <= 0 || viewDepth < uShadowSplitDepths[0] ) {
		return 0;
	}
	if ( interiorSplitCount <= 1 || viewDepth < uShadowSplitDepths[1] ) {
		return 1;
	}
	if ( interiorSplitCount <= 2 || viewDepth < uShadowSplitDepths[2] ) {
		return 2;
	}
	return 3;
}

vec4 SampleShadow() {
	int cascadeIndex = SelectCascade( vViewDepth );
	vec4 shadowInfo = SampleCascadeByIndex( cascadeIndex );
	int lastInteriorIndex = uShadowCascadeCount - 2;

	if ( cascadeIndex > lastInteriorIndex || uShadowCascadeBlend <= 0.0 ) {
		return vec4( shadowInfo.x, float( cascadeIndex ), 0.0, 0.0 );
	}

	float previousSplit = ( cascadeIndex == 0 ) ? 0.0 : CascadeSplitDepth( cascadeIndex - 1 );
	float currentSplit = CascadeSplitDepth( cascadeIndex );
	float blendWidth = max( 1.0, ( currentSplit - previousSplit ) * uShadowCascadeBlend );
	float blendStart = currentSplit - blendWidth;
	if ( vViewDepth <= blendStart ) {
		return vec4( shadowInfo.x, float( cascadeIndex ), 0.0, 0.0 );
	}

	float blend = clamp( ( vViewDepth - blendStart ) / blendWidth, 0.0, 1.0 );
	if ( blend <= 0.02 ) {
		// entering the band: the second cascade's contribution is invisible,
		// skip its full filter kernel
		return vec4( shadowInfo.x, float( cascadeIndex ), 0.0, 0.0 );
	}
	vec4 nextShadow = SampleCascadeByIndex( cascadeIndex + 1 );
	float shadow = mix( shadowInfo.x, nextShadow.x, blend );
	return vec4( shadow, float( cascadeIndex ), blend, 0.0 );
}

vec4 CascadeDebugColor( float cascadeIndex ) {
	if ( cascadeIndex < 0.5 ) {
		return vec4( 1.0, 0.2, 0.2, 1.0 );
	}
	if ( cascadeIndex < 1.5 ) {
		return vec4( 0.2, 1.0, 0.2, 1.0 );
	}
	if ( cascadeIndex < 2.5 ) {
		return vec4( 0.2, 0.5, 1.0, 1.0 );
	}
	return vec4( 1.0, 0.85, 0.2, 1.0 );
}

vec4 ShadowCoordWDebugOutput( vec4 shadowCoord ) {
	if ( shadowCoord.w != shadowCoord.w ) {
		return vec4( 1.0, 0.0, 1.0, 1.0 );
	}

	float absW = abs( shadowCoord.w );
	float danger = 1.0 - clamp( absW / 0.25, 0.0, 1.0 );
	float intensity = 0.3 + 0.7 * clamp( absW / 4.0, 0.0, 1.0 );
	vec3 signColor = ( shadowCoord.w < 0.0 ) ? vec3( 1.0, 0.2, 0.2 ) : vec3( 0.2, 0.6, 1.0 );
	vec3 color = mix( signColor, vec3( 1.0, 1.0, 0.0 ), danger );
	return vec4( color * intensity, 1.0 );
}

vec4 ShadowCompareDeltaDebugOutput( vec4 shadowInfo ) {
	int cascadeIndex = int( shadowInfo.y + 0.5 );
	vec2 localUv;
	float depth;
	if ( !ProjectShadowCoord( ShadowCoordByIndex( cascadeIndex ), localUv, depth ) ) {
		return vec4( 1.0, 0.0, 1.0, 1.0 );
	}
	if ( localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 || depth <= 0.0 || depth >= 1.0 ) {
		return vec4( 1.0, 1.0, 0.0, 1.0 );
	}

	vec4 atlasRect = uShadowAtlasRect[cascadeIndex];
	vec2 atlasUv = mix( atlasRect.xy, atlasRect.zw, localUv );
	float storedDepth = RawShadowDepth( atlasUv );
	float compareDepth = depth - ShadowReceiverBias( cascadeIndex, depth );
	float delta = compareDepth - storedDepth;
	float magnitude = clamp( abs( delta ) * 64.0, 0.0, 1.0 );
	vec3 litColor = vec3( 0.1, 0.35, 1.0 );
	vec3 shadowColor = vec3( 1.0, 0.16, 0.08 );
	vec3 nearColor = vec3( 0.0, 1.0, 0.22 );
	vec3 signColor = ( delta > 0.0 ) ? shadowColor : litColor;
	return vec4( mix( nearColor, signColor, magnitude ), 1.0 );
}

vec4 ShadowReceiverDebugOutput() {
	float reason = floor( uShadowReceiverDebugReason + 0.5 );
	if ( ShadowDebugModeIs( kShadowDebugReceiverEligibility ) ) {
		if ( reason < 0.5 ) {
			return vec4( 0.0, 0.95, 0.18, 1.0 );
		}
		if ( reason < 1.5 ) {
			return vec4( 0.0, 0.85, 1.0, 1.0 );
		}
		return vec4( 1.0, 0.18, 0.08, 1.0 );
	}

	if ( reason < 0.5 ) {
		return vec4( 0.0, 0.85, 0.16, 1.0 );
	}
	if ( reason < 1.5 ) {
		return vec4( 0.0, 0.82, 1.0, 1.0 );
	}
	if ( reason < 2.5 ) {
		return vec4( 1.0, 0.08, 0.08, 1.0 );
	}
	if ( reason < 3.5 ) {
		return vec4( 0.95, 0.12, 1.0, 1.0 );
	}
	if ( reason < 4.5 ) {
		return vec4( 1.0, 0.86, 0.08, 1.0 );
	}
	return vec4( 1.0, 0.45, 0.0, 1.0 );
}

vec4 ShadowDebugOutput( vec4 shadowInfo ) {
	if ( uShadowDebugMode < 0.5 ) {
		return vec4( 0.0 );
	}

	if ( ShadowReceiverDebugMode() ) {
		return ShadowReceiverDebugOutput();
	}

	if ( uShadowDebugMode < kShadowDebugCascadeIndex + 0.5 ) {
		int cascadeIndex = int( shadowInfo.y + 0.5 );
		vec2 localUv;
		float depth;
		bool validCoord = ProjectShadowCoord( ShadowCoordByIndex( cascadeIndex ), localUv, depth );
		if ( uShadowDebugMode < kShadowDebugAtlas + 0.5 ) {
			vec2 atlasUv = mix( uShadowAtlasRect[cascadeIndex].xy, uShadowAtlasRect[cascadeIndex].zw, localUv );
#ifdef OPENQ4_SHADOW_COMPARE
			float atlasDepth = shadow2D( uShadowMap, vec3( atlasUv, depth ) ).r;
#else
			float atlasDepth = texture2D( uShadowMap, atlasUv ).r;
#endif
			if ( !validCoord ) {
				return vec4( 1.0, 0.0, 1.0, 1.0 );
			}
			return vec4( atlasUv, atlasDepth, 1.0 );
		}
		vec4 cascadeColor = CascadeDebugColor( float( cascadeIndex ) );
		if ( shadowInfo.z > 0.0 ) {
			int nextCascadeIndex = ( cascadeIndex + 1 < uShadowCascadeCount ) ? ( cascadeIndex + 1 ) : ( uShadowCascadeCount - 1 );
			cascadeColor.rgb = mix( cascadeColor.rgb, CascadeDebugColor( float( nextCascadeIndex ) ).rgb, shadowInfo.z );
		}
		return cascadeColor;
	}

	if ( uShadowDebugMode < kShadowDebugProjectedUV + 0.5 ) {
		vec2 localUv;
		float depth;
		if ( !ProjectShadowCoord( ShadowCoordByIndex( int( shadowInfo.y + 0.5 ) ), localUv, depth ) ) {
			return vec4( 1.0, 0.0, 1.0, 1.0 );
		}
		return vec4( localUv, 1.0 - localUv.x, 1.0 );
	}

	if ( uShadowDebugMode < kShadowDebugProjectedDepth + 0.5 ) {
		vec2 localUv;
		float depth;
		if ( !ProjectShadowCoord( ShadowCoordByIndex( int( shadowInfo.y + 0.5 ) ), localUv, depth ) ) {
			return vec4( 1.0, 0.0, 1.0, 1.0 );
		}
		return vec4( vec3( depth ), 1.0 );
	}

	if ( uShadowDebugMode < kShadowDebugProjectedW + 0.5 ) {
		return ShadowCoordWDebugOutput( ShadowCoordByIndex( int( shadowInfo.y + 0.5 ) ) );
	}

	if ( ShadowDebugModeIs( kShadowDebugCompareDelta ) ) {
		return ShadowCompareDeltaDebugOutput( shadowInfo );
	}

	if ( uShadowDebugMode > kShadowDebugInvalidMask + 0.5 && uShadowDebugMode < kShadowDebugBiasHeatmap + 0.5 ) {
		int cascadeIndex = int( shadowInfo.y + 0.5 );
		vec2 localUv;
		float depth;
		if ( !ProjectShadowCoord( ShadowCoordByIndex( cascadeIndex ), localUv, depth ) ) {
			return vec4( 1.0, 0.0, 1.0, 1.0 );
		}
		float bias = ShadowReceiverBias( cascadeIndex, depth );
		float heat = clamp( bias * 400.0, 0.0, 1.0 );
		return vec4( heat, 1.0 - abs( heat - 0.5 ) * 2.0, 1.0 - heat, 1.0 );
	}

	vec3 invalidColor = vec3( 0.0 );
	if ( gShadowDebugState > 1.5 ) {
		invalidColor = vec3( 1.0, 0.0, 1.0 );
	} else if ( gShadowDebugState > 0.5 ) {
		invalidColor = vec3( 1.0, 0.0, 0.0 );
	} else {
		vec2 localUv;
		float depth;
		bool validCoord = ProjectShadowCoord( ShadowCoordByIndex( int( shadowInfo.y + 0.5 ) ), localUv, depth );
		if ( !validCoord ) {
			invalidColor = vec3( 1.0, 0.0, 1.0 );
		} else if ( localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 || depth <= 0.0 || depth >= 1.0 ) {
			invalidColor = vec3( 1.0, 1.0, 0.0 );
		}
	}
	return vec4( invalidColor, 1.0 );
}

vec3 SampleTranslucentShadowCascadeByIndex( int index ) {
	if ( index <= 0 ) {
		return SampleTranslucentShadowCascade( vShadowCoord0, uShadowAtlasRect[0] );
	}
	if ( index == 1 ) {
		return SampleTranslucentShadowCascade( vShadowCoord1, uShadowAtlasRect[1] );
	}
	if ( index == 2 ) {
		return SampleTranslucentShadowCascade( vShadowCoord2, uShadowAtlasRect[2] );
	}
	return SampleTranslucentShadowCascade( vShadowCoord3, uShadowAtlasRect[3] );
}

vec3 SampleTranslucentShadow() {
	if ( uTranslucentShadowEnabled < 0.5 ) {
		return vec3( 1.0 );
	}

	int cascadeIndex = SelectCascade( vViewDepth );
	vec3 shadow = SampleTranslucentShadowCascadeByIndex( cascadeIndex );
	int lastInteriorIndex = uShadowCascadeCount - 2;

	if ( cascadeIndex > lastInteriorIndex || uShadowCascadeBlend <= 0.0 ) {
		return shadow;
	}

	float previousSplit = ( cascadeIndex == 0 ) ? 0.0 : CascadeSplitDepth( cascadeIndex - 1 );
	float currentSplit = CascadeSplitDepth( cascadeIndex );
	float blendWidth = max( 1.0, ( currentSplit - previousSplit ) * uShadowCascadeBlend );
	float blendStart = currentSplit - blendWidth;
	if ( vViewDepth <= blendStart ) {
		return shadow;
	}

	vec3 nextShadow = SampleTranslucentShadowCascadeByIndex( cascadeIndex + 1 );
	float blend = clamp( ( vViewDepth - blendStart ) / blendWidth, 0.0, 1.0 );
	return mix( shadow, nextShadow, blend );
}

#ifdef OPENQ4_PBR
// Native PBR shading for one light, given its shadow visibility; draw_pbr.cpp
// appends the library that defines it to this source.
vec3 OpenQ4PBRDirect( vec3 shadow );
// The coverage of a translucent owner's replayed light; zero otherwise.
float OpenQ4PBRAlpha();
#endif

void main() {
#ifdef OPENQ4_PBR
	gShadowDebugState = 0.0;
#ifdef OPENQ4_PBR_UNSHADOWED
	gl_FragColor = vec4( OpenQ4PBRDirect( vec3( 1.0 ) ), OpenQ4PBRAlpha() );
#else
	if ( uShadowReceiverPlaneBias > 0.5 ) {
		gShadowDepthGradients = vec4(
			abs( dFdx( vShadowCoord0.z ) ) + abs( dFdy( vShadowCoord0.z ) ),
			abs( dFdx( vShadowCoord1.z ) ) + abs( dFdy( vShadowCoord1.z ) ),
			abs( dFdx( vShadowCoord2.z ) ) + abs( dFdy( vShadowCoord2.z ) ),
			abs( dFdx( vShadowCoord3.z ) ) + abs( dFdy( vShadowCoord3.z ) ) );
	}
	vec4 pbrShadowInfo = SampleShadow();
	if ( ShadowVisualDebugMode() ) {
		gl_FragColor = ShadowDebugOutput( pbrShadowInfo );
		return;
	}
	gl_FragColor = vec4( OpenQ4PBRDirect( pbrShadowInfo.x * SampleTranslucentShadow() ), OpenQ4PBRAlpha() );
#endif
	return;
#endif
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = DecodeLocalNormal( bumpSample );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	gShadowDebugState = 0.0;
	if ( uShadowReceiverPlaneBias > 0.5 ) {
		gShadowDepthGradients = vec4(
			abs( dFdx( vShadowCoord0.z ) ) + abs( dFdy( vShadowCoord0.z ) ),
			abs( dFdx( vShadowCoord1.z ) ) + abs( dFdy( vShadowCoord1.z ) ),
			abs( dFdx( vShadowCoord2.z ) ) + abs( dFdy( vShadowCoord2.z ) ),
			abs( dFdx( vShadowCoord3.z ) ) + abs( dFdy( vShadowCoord3.z ) ) );
	}

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;
	// A fragment the light does not reach (outside the projected image, past
	// the falloff, turned away) has no shadow to resolve, and the filter is
	// most of this shader's cost. The debug views still show every fragment.
	vec4 shadowInfo = vec4( 1.0, 0.0, 0.0, 0.0 );
	if ( max( light.r, max( light.g, light.b ) ) > 0.0 || ShadowVisualDebugMode() ) {
		shadowInfo = SampleShadow();
		light *= shadowInfo.x;
		light *= SampleTranslucentShadow();
	}

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;
	diffuse = ApplyFlatDiffuseSweep( diffuse, vLightFalloffTexCoord.z );

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	vec3 specular = InteractionSpecular( halfAngle, viewDir, localNormal, specularSample );

	vec3 color = ( diffuse + specular ) * CelQuantizeLight( light ) * vVertexColor;
	if ( ShadowVisualDebugMode() ) {
		gl_FragColor = ShadowDebugOutput( shadowInfo );
		return;
	}
	gl_FragColor = vec4( color, 0.0 );
}
