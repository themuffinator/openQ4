uniform float uPointShadowFar;

varying vec3 vPointShadowVector;

vec2 PackDepth16( float depth ) {
	vec2 enc = fract( vec2( 1.0, 255.0 ) * clamp( depth, 0.0, 1.0 ) );
	enc.x -= enc.y * ( 1.0 / 255.0 );
	return enc;
}

void main() {
	float depth = clamp( length( vPointShadowVector ) / uPointShadowFar, 0.0, 1.0 );
	vec2 packed = PackDepth16( depth );
	gl_FragColor = vec4( packed, 0.0, 1.0 );
}
