#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
#ifdef OPENQ4_POINT_SHADOW_COMPARE
uniform samplerCubeShadow uPointShadowMap;
#else
uniform samplerCube uPointShadowMap;
#endif
uniform samplerCube uPointTranslucentShadowMapR;
uniform samplerCube uPointTranslucentShadowMapG;
uniform samplerCube uPointTranslucentShadowMapB;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uMaterialEnhanced;
uniform float uMaterialNormalScale;
uniform float uMaterialSpecularBoost;
uniform float uMaterialFresnel;
uniform float uPointShadowFar;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uPointShadowTexelDepthBias;
uniform float uShadowFilterRadius;
uniform float uShadowFilterTaps;
uniform float uShadowFilterMode;
uniform float uShadowDebugMode;
uniform float uPointShadowTexelScale;
uniform float uPointShadowDepthMode;
uniform float uTranslucentShadowEnabled;
uniform float uTranslucentShadowDensity;
uniform float uTranslucentShadowFilterRadius;
uniform float uTranslucentShadowMinVariance;
uniform float uTranslucentShadowBleedReduction;

const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
varying vec3 vPointShadowVector;
varying vec3 vVertexColor;
varying float vShadowLightCos;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

bool ShadowDebugModeIs( float mode ) {
	return abs( uShadowDebugMode - mode ) < 0.5;
}

float EffectiveShadowFilterRadius() {
	return ShadowDebugModeIs( kShadowDebugPCFOff ) ? 0.0 : uShadowFilterRadius;
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
		return specularSample * uSpecularColor.rgb * EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample );
	}
	return specularSample * ( uSpecularColor.rgb * 2.0 ) * LegacySpecularTerm( halfAngle, localNormal );
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

float StableShadowHash( vec3 value ) {
	return fract( sin( dot( value, vec3( 12.9898, 78.233, 37.719 ) ) ) * 43758.5453 );
}

vec2 RotateShadowOffset( vec2 offset, vec3 direction ) {
	if ( uShadowFilterMode < 0.5 ) {
		return offset;
	}
	float angle = StableShadowHash( floor( direction * 37.0 ) ) * 6.2831853;
	float s = sin( angle );
	float c = cos( angle );
	return vec2( c * offset.x - s * offset.y, s * offset.x + c * offset.y );
}

float TranslucentFilterRadius() {
	return ( uTranslucentShadowFilterRadius >= 0.0 ) ? uTranslucentShadowFilterRadius : uShadowFilterRadius;
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

vec4 SampleFilteredPointMoments( samplerCube momentMap, vec3 direction ) {
	float filterRadius = ShadowDebugModeIs( kShadowDebugPCFOff ) ? 0.0 : TranslucentFilterRadius();
	if ( filterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return textureCube( momentMap, direction );
	}

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * max( filterRadius, 0.5 );
	vec4 moments = textureCube( momentMap, direction );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * 0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * 0.5 ) * tap ) );
	return moments * 0.2;
}

float ShadowReceiverBias() {
	if ( ShadowDebugModeIs( kShadowDebugBiasOff ) ) {
		return 0.0;
	}
	float lightCos = clamp( vShadowLightCos, 1.0e-3, 1.0 );
	float sinTheta = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	float slopeBias = sinTheta / lightCos;
	float normalBias = ShadowDebugModeIs( kShadowDebugReceiverPlaneBiasOff ) ? 0.0 : uShadowNormalBias;
	float scalarBias = uShadowBias + normalBias * sinTheta;
	float texelBias = uPointShadowTexelDepthBias * ( 1.0 + slopeBias );
	return max( max( scalarBias, 0.0 ), max( texelBias, 0.0 ) );
}

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

float DecodePointShadowDepth( vec4 encodedDepth ) {
	if ( uPointShadowDepthMode > 0.5 ) {
		return encodedDepth.r;
	}
	return UnpackDepth16( encodedDepth.rg );
}

float SamplePointShadowCompare( vec3 direction, float depth ) {
	float bias = ShadowReceiverBias();
#ifdef OPENQ4_POINT_SHADOW_COMPARE
	return texture( uPointShadowMap, vec4( direction, depth - bias ) );
#else
	float storedDepth = DecodePointShadowDepth( textureCube( uPointShadowMap, direction ) );
	return ( depth - bias <= storedDepth ) ? 1.0 : 0.0;
#endif
}

float SamplePointShadow() {
	if ( uPointShadowFar <= 0.0 ) {
		return 1.0;
	}

	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return 1.0;
	}

	vec3 direction = SafeNormalize( vPointShadowVector );
	float filterRadius = EffectiveShadowFilterRadius();
	if ( filterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return SamplePointShadowCompare( direction, depth );
	}

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * filterRadius;

	float shadow = 0.0;
	shadow += SamplePointShadowCompare( direction, depth );
	if ( uShadowFilterTaps <= 1.0 ) {
		return shadow;
	}
	vec2 o1 = RotateShadowOffset( vec2( -0.326212, -0.405805 ), direction );
	vec2 o2 = RotateShadowOffset( vec2( -0.840144, -0.073580 ), direction );
	vec2 o3 = RotateShadowOffset( vec2( -0.695914, 0.457137 ), direction );
	vec2 o4 = RotateShadowOffset( vec2( -0.203345, 0.620716 ), direction );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o1.x + bitangent * o1.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o2.x + bitangent * o2.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o3.x + bitangent * o3.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o4.x + bitangent * o4.y ) * tap ), depth );
	if ( uShadowFilterTaps <= 5.0 ) {
		return shadow * ( 1.0 / 5.0 );
	}
	vec2 o5 = RotateShadowOffset( vec2( 0.962340, -0.194983 ), direction );
	vec2 o6 = RotateShadowOffset( vec2( 0.473434, -0.480026 ), direction );
	vec2 o7 = RotateShadowOffset( vec2( 0.519456, 0.767022 ), direction );
	vec2 o8 = RotateShadowOffset( vec2( 0.185461, -0.893124 ), direction );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o5.x + bitangent * o5.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o6.x + bitangent * o6.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o7.x + bitangent * o7.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o8.x + bitangent * o8.y ) * tap ), depth );
	if ( uShadowFilterTaps <= 9.0 ) {
		return shadow * ( 1.0 / 9.0 );
	}
	vec2 o9 = RotateShadowOffset( vec2( 0.507431, 0.064425 ), direction );
	vec2 o10 = RotateShadowOffset( vec2( 0.896420, 0.412458 ), direction );
	vec2 o11 = RotateShadowOffset( vec2( -0.321940, -0.932615 ), direction );
	vec2 o12 = RotateShadowOffset( vec2( -0.791559, -0.597705 ), direction );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o9.x + bitangent * o9.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o10.x + bitangent * o10.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o11.x + bitangent * o11.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o12.x + bitangent * o12.y ) * tap ), depth );
	return shadow * ( 1.0 / 13.0 );
}

vec3 SamplePointTranslucentShadow() {
	if ( uTranslucentShadowEnabled < 0.5 || uPointShadowFar <= 0.0 ) {
		return vec3( 1.0 );
	}

	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return vec3( 1.0 );
	}

	vec3 direction = SafeNormalize( vPointShadowVector );
	return vec3(
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapR, direction ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapG, direction ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapB, direction ), depth ) );
}

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = DecodeLocalNormal( bumpSample );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;
	light *= SamplePointShadow();
	light *= SamplePointTranslucentShadow();

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	vec3 specular = InteractionSpecular( halfAngle, viewDir, localNormal, specularSample );

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
