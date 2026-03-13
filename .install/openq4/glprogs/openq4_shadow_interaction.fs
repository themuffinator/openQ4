uniform sampler2D uShadowMap;

uniform vec2 uShadowTexelSize;
uniform float uShadowBias;
uniform float uShadowFilterRadius;

varying vec4 vShadowCoord;

float SampleShadowCompare( vec2 uv, float depth ) {
	float storedDepth = texture2D( uShadowMap, uv ).r;
	return ( depth - uShadowBias <= storedDepth ) ? 1.0 : 0.0;
}

float ScreenDoorThreshold( vec2 fragCoord ) {
	return fract( 52.9829189 * fract( 0.06711056 * fragCoord.x + 0.00583715 * fragCoord.y ) );
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
	shadow += SampleShadowCompare( uv + vec2( -0.613392, 0.617481 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.170019, -0.040254 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.299417, 0.791925 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.645680, 0.493210 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.651784, 0.717887 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( 0.421003, 0.027070 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.817194, -0.271096 ) * tap, depth );
	shadow += SampleShadowCompare( uv + vec2( -0.705374, -0.668203 ) * tap, depth );
	return shadow / 9.0;
}

void main() {
	float visibility = SampleShadow( vShadowCoord );
	if ( visibility <= 0.0 ) {
		discard;
	}
	if ( visibility < 1.0 && visibility <= ScreenDoorThreshold( gl_FragCoord.xy ) ) {
		discard;
	}
	gl_FragColor = vec4( 1.0 );
}
