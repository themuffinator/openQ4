#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
uniform samplerCube uPointShadowMap;
uniform samplerCube uPointTranslucentShadowMapR;
uniform samplerCube uPointTranslucentShadowMapG;
uniform samplerCube uPointTranslucentShadowMapB;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uPointShadowFar;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uShadowFilterRadius;
uniform float uPointShadowTexelScale;
uniform float uTranslucentShadowEnabled;
uniform float uTranslucentShadowDensity;

const float kTranslucentMomentMinVariance = 1.0e-5;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vPointShadowVector;
varying vec3 vVertexColor;
varying float vShadowLightCos;

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

vec4 SampleFilteredPointMoments( samplerCube momentMap, vec3 direction ) {
	if ( uShadowFilterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return textureCube( momentMap, direction );
	}

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * max( uShadowFilterRadius, 0.5 );
	vec4 moments = textureCube( momentMap, direction );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * 0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * 0.5 ) * tap ) );
	return moments * 0.2;
}

float ShadowReceiverBias() {
	float lightCos = clamp( vShadowLightCos, 0.0, 1.0 );
	float slopeBias = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	return uShadowBias + uShadowNormalBias * slopeBias;
}

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

float SamplePointShadowCompare( vec3 direction, float depth ) {
	float bias = ShadowReceiverBias();
	float storedDepth = UnpackDepth16( textureCube( uPointShadowMap, direction ).rg );
	return ( depth - bias <= storedDepth ) ? 1.0 : 0.0;
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
	if ( uShadowFilterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return SamplePointShadowCompare( direction, depth );
	}

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * uShadowFilterRadius;

	float shadow = 0.0;
	shadow += SamplePointShadowCompare( direction, depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.326212 + bitangent * -0.405805 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.840144 + bitangent * -0.073580 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.695914 + bitangent * 0.457137 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.203345 + bitangent * 0.620716 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.962340 + bitangent * -0.194983 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.473434 + bitangent * -0.480026 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.519456 + bitangent * 0.767022 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.185461 + bitangent * -0.893124 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.507431 + bitangent * 0.064425 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * 0.896420 + bitangent * 0.412458 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.321940 + bitangent * -0.932615 ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * -0.791559 + bitangent * -0.597705 ) * tap ), depth );
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
	vec3 localNormal = vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0;
	localNormal = SafeNormalize( localNormal );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;
	light *= SamplePointShadow();
	light *= SamplePointTranslucentShadow();

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float specularTerm = clamp( ndoth * 4.0 - 3.0, 0.0, 1.0 );
	specularTerm *= specularTerm;
	vec3 specular = texture2D( uSpecularMap, vSpecularTexCoord ).rgb * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
