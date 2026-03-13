uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
uniform samplerCube uPointShadowMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uPointShadowFar;
uniform float uShadowBias;
uniform float uShadowFilterRadius;
uniform float uPointShadowTexelScale;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vPointShadowVector;
varying vec3 vVertexColor;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

float SamplePointShadowCompare( vec3 direction, float depth ) {
	float storedDepth = UnpackDepth16( textureCube( uPointShadowMap, direction ).rg );
	return ( depth - uShadowBias <= storedDepth ) ? 1.0 : 0.0;
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

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float specularTerm = clamp( ndoth * 4.0 - 3.0, 0.0, 1.0 );
	specularTerm *= specularTerm;
	vec3 specular = texture2D( uSpecularMap, vSpecularTexCoord ).rgb * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
