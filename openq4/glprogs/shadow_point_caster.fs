#version 110

uniform float uPointShadowFar;
uniform sampler2D uAlphaMap;
uniform float uAlphaRef;
uniform float uAlphaScale;
uniform float uAlphaTestEnabled;
uniform float uAlphaHashEnabled;

varying vec3 vPointShadowVector;
varying vec2 vAlphaTexCoord;

vec2 PackDepth16( float depth ) {
	vec2 enc = fract( vec2( 1.0, 255.0 ) * clamp( depth, 0.0, 1.0 ) );
	enc.x -= enc.y * ( 1.0 / 255.0 );
	return enc;
}

float AlphaHashThreshold( vec2 fragmentCoord ) {
	vec2 texel = floor( fragmentCoord );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 ) );
}

float AlphaCoverage( float alpha ) {
	float alphaRef = clamp( uAlphaRef, 0.0, 0.9999 );
	float scaledAlpha = clamp( alpha, 0.0, 1.0 );
	return clamp( ( scaledAlpha - alphaRef ) / max( 1.0 - alphaRef, 1.0e-4 ), 0.0, 1.0 );
}

void main() {
	if ( uAlphaTestEnabled > 0.5 ) {
		float alpha = texture2D( uAlphaMap, vAlphaTexCoord ).a * uAlphaScale;
		if ( uAlphaHashEnabled > 0.5 ) {
			float coverage = AlphaCoverage( alpha );
			if ( coverage <= 0.0 || coverage <= AlphaHashThreshold( gl_FragCoord.xy ) ) {
				discard;
			}
		} else {
			if ( alpha <= uAlphaRef ) {
				discard;
			}
		}
	}

	float depth = clamp( length( vPointShadowVector ) / uPointShadowFar, 0.0, 1.0 );
	vec2 packedDepth = PackDepth16( depth );
	gl_FragColor = vec4( packedDepth, 0.0, 1.0 );
}
