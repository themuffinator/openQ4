uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
uniform sampler2D uShadowMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform vec2 uShadowTexelSize;
uniform float uShadowBias;
uniform float uShadowFilterRadius;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec4 vShadowCoord;
varying vec3 vVertexColor;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

float SampleShadowCompare( vec2 uv, float depth ) {
	float storedDepth = texture2D( uShadowMap, uv ).r;
	return ( depth - uShadowBias <= storedDepth ) ? 1.0 : 0.0;
}

float SampleShadow( vec4 shadowCoord ) {
	if ( shadowCoord.w <= 0.0 ) {
		return 1.0;
	}

	vec3 projected = shadowCoord.xyz / shadowCoord.w;
	vec2 uv = projected.xy * 0.5 + 0.5;
	float depth = projected.z * 0.5 + 0.5;

	if ( uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 ) {
		return 1.0;
	}
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return 1.0;
	}

	if ( uShadowFilterRadius <= 0.0 ) {
		return SampleShadowCompare( uv, depth );
	}

	vec2 tap = uShadowTexelSize * uShadowFilterRadius;
	float shadow = 0.0;
	shadow += SampleShadowCompare( uv, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.326212, -0.405805 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.840144, -0.073580 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.695914, 0.457137 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.203345, 0.620716 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.962340, -0.194983 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.473434, -0.480026 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.519456, 0.767022 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.185461, -0.893124 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.507431, 0.064425 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.896420, 0.412458 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.321940, -0.932615 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.791559, -0.597705 ) * tap, depth );
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
	light *= SampleShadow( vShadowCoord );

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float specularTerm = clamp( ndoth * 4.0 - 3.0, 0.0, 1.0 );
	specularTerm *= specularTerm;
	vec3 specular = texture2D( uSpecularMap, vSpecularTexCoord ).rgb * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
