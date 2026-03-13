uniform samplerCube uPointShadowMap;

uniform float uPointShadowFar;
uniform float uShadowBias;
uniform float uShadowFilterRadius;
uniform float uPointShadowTexelScale;

varying vec3 vPointShadowVector;

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

float SamplePointShadowCompare( vec3 dir, float depth ) {
	float storedDepth = UnpackDepth16( textureCube( uPointShadowMap, dir ).rg );
	return ( depth - uShadowBias <= storedDepth ) ? 1.0 : 0.0;
}

float ScreenDoorThreshold( vec2 fragCoord ) {
	return fract( 52.9829189 * fract( 0.06711056 * fragCoord.x + 0.00583715 * fragCoord.y ) );
}

float SamplePointShadow() {
	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return 1.0;
	}

	vec3 dir = normalize( vPointShadowVector );
	if ( uShadowFilterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return SamplePointShadowCompare( dir, depth );
	}

	vec3 up = ( abs( dir.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = normalize( cross( up, dir ) );
	vec3 bitangent = cross( dir, tangent );
	float tap = uPointShadowTexelScale;

	float shadow = 0.0;
	shadow += SamplePointShadowCompare( dir, depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * -0.613392 + bitangent * 0.617481 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * 0.170019 + bitangent * -0.040254 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * -0.299417 + bitangent * 0.791925 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * 0.645680 + bitangent * 0.493210 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * -0.651784 + bitangent * 0.717887 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * 0.421003 + bitangent * 0.027070 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * -0.817194 + bitangent * -0.271096 ) * tap ), depth );
	shadow += SamplePointShadowCompare( normalize( dir + ( tangent * -0.705374 + bitangent * -0.668203 ) * tap ), depth );
	return shadow / 9.0;
}

void main() {
	float visibility = SamplePointShadow();
	if ( visibility <= 0.0 ) {
		discard;
	}
	if ( visibility < 1.0 && visibility <= ScreenDoorThreshold( gl_FragCoord.xy ) ) {
		discard;
	}
	gl_FragColor = vec4( 1.0 );
}
