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
uniform float uShadowNormalBias;
uniform float uShadowFilterRadius;
uniform vec4 uShadowAtlasRect[4];
uniform float uShadowSplitDepths[4];
uniform int uShadowCascadeCount;
uniform float uShadowCascadeBlend;

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

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

float ShadowReceiverBias() {
	float lightCos = clamp( vShadowLightCos, 0.0, 1.0 );
	float slopeBias = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	return uShadowBias + uShadowNormalBias * slopeBias;
}

float SampleShadowCompare( vec2 uv, float depth ) {
	float bias = ShadowReceiverBias();
	float storedDepth = texture2D( uShadowMap, uv ).r;
	return ( depth - bias <= storedDepth ) ? 1.0 : 0.0;
}

float SampleShadowCascade( vec4 shadowCoord, vec4 atlasRect ) {
	if ( shadowCoord.w <= 0.0 ) {
		return 1.0;
	}

	vec3 projected = shadowCoord.xyz / shadowCoord.w;
	vec2 localUv = projected.xy * 0.5 + 0.5;
	float depth = projected.z * 0.5 + 0.5;

	if ( localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 ) {
		return 1.0;
	}
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return 1.0;
	}

	vec2 atlasMin = atlasRect.xy;
	vec2 atlasMax = atlasRect.zw;
	vec2 uv = atlasMin + localUv * ( atlasMax - atlasMin );
	vec2 clampMin = atlasMin + uShadowTexelSize * 1.5;
	vec2 clampMax = atlasMax - uShadowTexelSize * 1.5;
	clampMin = min( clampMin, clampMax );
	uv = clamp( uv, clampMin, clampMax );

	if ( uShadowFilterRadius <= 0.0 ) {
		return SampleShadowCompare( uv, depth );
	}

	vec2 tap = uShadowTexelSize * uShadowFilterRadius;
	float shadow = 0.0;
	shadow += SampleShadowCompare( uv, depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.326212, -0.405805 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.840144, -0.073580 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.695914, 0.457137 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.203345, 0.620716 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.962340, -0.194983 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.473434, -0.480026 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.519456, 0.767022 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.185461, -0.893124 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.507431, 0.064425 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( 0.896420, 0.412458 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.321940, -0.932615 ) * tap, clampMin, clampMax ), depth );
	shadow += SampleShadowCompare( clamp( uv + vec2( -0.791559, -0.597705 ) * tap, clampMin, clampMax ), depth );
	return shadow * ( 1.0 / 13.0 );
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

float SampleCascadeByIndex( int index ) {
	if ( index <= 0 ) {
		return SampleShadowCascade( vShadowCoord0, uShadowAtlasRect[0] );
	}
	if ( index == 1 ) {
		return SampleShadowCascade( vShadowCoord1, uShadowAtlasRect[1] );
	}
	if ( index == 2 ) {
		return SampleShadowCascade( vShadowCoord2, uShadowAtlasRect[2] );
	}
	return SampleShadowCascade( vShadowCoord3, uShadowAtlasRect[3] );
}

int SelectCascade( float viewDepth ) {
	if ( uShadowCascadeCount <= 1 || viewDepth < uShadowSplitDepths[0] ) {
		return 0;
	}
	if ( uShadowCascadeCount <= 2 || viewDepth < uShadowSplitDepths[1] ) {
		return 1;
	}
	if ( uShadowCascadeCount <= 3 || viewDepth < uShadowSplitDepths[2] ) {
		return 2;
	}
	return 3;
}

float SampleShadow() {
	int cascadeIndex = SelectCascade( vViewDepth );
	float shadow = SampleCascadeByIndex( cascadeIndex );

	if ( cascadeIndex >= uShadowCascadeCount - 1 || uShadowCascadeBlend <= 0.0 ) {
		return shadow;
	}

	float previousSplit = ( cascadeIndex == 0 ) ? 0.0 : CascadeSplitDepth( cascadeIndex - 1 );
	float currentSplit = CascadeSplitDepth( cascadeIndex );
	float blendWidth = max( 1.0, ( currentSplit - previousSplit ) * uShadowCascadeBlend );
	float blendStart = currentSplit - blendWidth;
	if ( vViewDepth <= blendStart ) {
		return shadow;
	}

	float nextShadow = SampleCascadeByIndex( cascadeIndex + 1 );
	float blend = clamp( ( vViewDepth - blendStart ) / blendWidth, 0.0, 1.0 );
	return mix( shadow, nextShadow, blend );
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
	light *= SampleShadow();

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float specularTerm = clamp( ndoth * 4.0 - 3.0, 0.0, 1.0 );
	specularTerm *= specularTerm;
	vec3 specular = texture2D( uSpecularMap, vSpecularTexCoord ).rgb * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
