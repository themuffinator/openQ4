#version 110

uniform sampler2D uAlphaMap;
uniform float uAlphaRef;
uniform float uAlphaScale;
uniform float uAlphaHashEnabled;

varying vec2 vAlphaTexCoord;

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

	gl_FragColor = vec4( 0.0 );
}
