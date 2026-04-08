#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
uniform sampler2D uShadowMap;
uniform sampler2D uTranslucentShadowMapR;
uniform sampler2D uTranslucentShadowMapG;
uniform sampler2D uTranslucentShadowMapB;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform vec2 uShadowTexelSize;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uShadowFilterRadius;
uniform vec4 uShadowAtlasRect[4];
uniform float uShadowSplitDepths[4];
uniform float uShadowCascadeBiasScale[4];
uniform int uShadowCascadeCount;
uniform float uShadowCascadeBlend;
uniform float uShadowDebugMode;
uniform float uTranslucentShadowEnabled;
uniform float uTranslucentShadowDensity;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec4 vShadowCoord0;
varying vec4 vShadowCoord1;
varying vec4 vShadowCoord2;
varying vec4 vShadowCoord3;
varying vec3 vVertexColor;
varying float vShadowLightCos;
varying float vViewDepth;

const float kShadowCoordWEpsilon = 1.0e-5;
const float kShadowCoordMaxMagnitude = 65536.0;
const float kShadowDebugAtlas = 1.0;
const float kShadowDebugCascadeIndex = 2.0;
const float kShadowDebugProjectedUV = 3.0;
const float kShadowDebugProjectedDepth = 4.0;
const float kShadowDebugProjectedW = 5.0;
const float kShadowDebugInvalidMask = 6.0;
const float kTranslucentMomentMinVariance = 1.0e-5;

float gShadowDebugState = 0.0;

bool ProjectShadowCoord( vec4 shadowCoord, out vec2 localUv, out float depth );

bool ShadowCoordComponentInvalid( float value ) {
	return value != value || abs( value ) > kShadowCoordMaxMagnitude;
}

bool ShadowCoordProjectedInvalid( vec3 value ) {
	return ShadowCoordComponentInvalid( value.x ) || ShadowCoordComponentInvalid( value.y ) || ShadowCoordComponentInvalid( value.z );
}

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
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

float ResolveTranslucentShadowMoments( vec4 moments, float depth ) {
	if ( uTranslucentShadowEnabled < 0.5 ) {
		return 1.0;
	}

	float totalTau = max( moments.x, 0.0 );
	if ( totalTau <= 1.0e-4 ) {
		return 1.0;
	}

	float mean = moments.y / totalTau;
	float variance = max( moments.z / totalTau - mean * mean, kTranslucentMomentMinVariance );
	float sigma = sqrt( variance );
	float fraction = clamp( NormalCdf( ( depth - mean ) / sigma ), 0.0, 1.0 );
	float tau = totalTau * fraction;
	return exp( -min( tau * max( uTranslucentShadowDensity, 0.0 ), 16.0 ) );
}

vec2 ShadowAtlasGuardBand() {
	float guardRadius = max( 0.5, uShadowFilterRadius + 0.75 );
	return uShadowTexelSize * guardRadius;
}

vec4 SampleFilteredMoments( sampler2D momentMap, vec2 uv, vec2 clampMin, vec2 clampMax ) {
	if ( uShadowFilterRadius <= 0.0 ) {
		return texture2D( momentMap, uv );
	}

	vec2 tap = uShadowTexelSize * max( uShadowFilterRadius, 0.5 );
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

float ShadowReceiverBias( int cascadeIndex ) {
	float lightCos = clamp( vShadowLightCos, 0.0, 1.0 );
	float slopeBias = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	float cascadeScale = CascadeBiasScale( cascadeIndex );
	return ( uShadowBias + uShadowNormalBias * slopeBias ) * cascadeScale;
}

float SampleShadowCompare( vec2 uv, float depth, int cascadeIndex ) {
	float bias = ShadowReceiverBias( cascadeIndex );
	float storedDepth = texture2D( uShadowMap, uv ).r;
	return ( depth - bias <= storedDepth ) ? 1.0 : 0.0;
}

vec4 SampleShadowCascade( vec4 shadowCoord, vec4 atlasRect, int cascadeIndex ) {
	float absW = abs( shadowCoord.w );
	if ( shadowCoord.w != shadowCoord.w || absW < kShadowCoordWEpsilon || absW > kShadowCoordMaxMagnitude ) {
		gShadowDebugState = max( gShadowDebugState, 1.0 );
		return vec4( 1.0, 0.5, 0.5, 1.0 );
	}

	vec3 projected = shadowCoord.xyz / shadowCoord.w;
	if ( ShadowCoordProjectedInvalid( projected ) ) {
		gShadowDebugState = max( gShadowDebugState, 2.0 );
		return vec4( 1.0, 0.5, 0.5, 1.0 );
	}

	vec2 localUv = projected.xy * 0.5 + 0.5;
	float depth = projected.z * 0.5 + 0.5;

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

	if ( uShadowFilterRadius <= 0.0 ) {
		return vec4( SampleShadowCompare( uv, depth, cascadeIndex ), localUv.x, localUv.y, depth );
	}

	vec2 tap = uShadowTexelSize * uShadowFilterRadius;
	float shadow = 0.0;
	shadow += SampleShadowCompare( uv, depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.326212, -0.405805 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.840144, -0.073580 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.695914, 0.457137 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.203345, 0.620716 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.962340, -0.194983 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.473434, -0.480026 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.519456, 0.767022 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.185461, -0.893124 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.507431, 0.064425 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.896420, 0.412458 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.321940, -0.932615 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.791559, -0.597705 ) * tap, clampMin, clampMax ), depth, cascadeIndex );
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
	float absW = abs( shadowCoord.w );
	if ( shadowCoord.w != shadowCoord.w || absW < kShadowCoordWEpsilon || absW > kShadowCoordMaxMagnitude ) {
		gShadowDebugState = max( gShadowDebugState, 1.0 );
		localUv = vec2( 0.0 );
		depth = 0.0;
		return false;
	}

	vec3 projected = shadowCoord.xyz / shadowCoord.w;
	if ( ShadowCoordProjectedInvalid( projected ) ) {
		gShadowDebugState = max( gShadowDebugState, 2.0 );
		localUv = vec2( 0.0 );
		depth = 0.0;
		return false;
	}

	localUv = projected.xy * 0.5 + 0.5;
	depth = projected.z * 0.5 + 0.5;
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

	vec4 nextShadow = SampleCascadeByIndex( cascadeIndex + 1 );
	float blend = clamp( ( vViewDepth - blendStart ) / blendWidth, 0.0, 1.0 );
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

vec4 ShadowDebugOutput( vec4 shadowInfo ) {
	if ( uShadowDebugMode < 0.5 ) {
		return vec4( 0.0 );
	}

	if ( uShadowDebugMode < kShadowDebugCascadeIndex + 0.5 ) {
		int cascadeIndex = int( shadowInfo.y + 0.5 );
		vec2 localUv;
		float depth;
		bool validCoord = ProjectShadowCoord( ShadowCoordByIndex( cascadeIndex ), localUv, depth );
		if ( uShadowDebugMode < kShadowDebugAtlas + 0.5 ) {
			vec2 atlasUv = mix( uShadowAtlasRect[cascadeIndex].xy, uShadowAtlasRect[cascadeIndex].zw, localUv );
			float atlasDepth = texture2D( uShadowMap, atlasUv ).r;
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

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0;
	localNormal = SafeNormalize( localNormal );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	gShadowDebugState = 0.0;

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;
	vec4 shadowInfo = SampleShadow();
	light *= shadowInfo.x;
	light *= SampleTranslucentShadow();

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float specularTerm = clamp( ndoth * 4.0 - 3.0, 0.0, 1.0 );
	specularTerm *= specularTerm;
	vec3 specular = texture2D( uSpecularMap, vSpecularTexCoord ).rgb * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	if ( uShadowDebugMode > 0.5 ) {
		gl_FragColor = ShadowDebugOutput( shadowInfo );
		return;
	}
	gl_FragColor = vec4( color, 0.0 );
}
