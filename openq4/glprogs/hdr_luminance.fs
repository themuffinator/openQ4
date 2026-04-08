uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float sourceIsColor;

float SampleValue( vec2 uv ) {
	vec3 sampleColor = texture2D( Scene, uv ).rgb;
	if ( sourceIsColor > 0.5 ) {
		float luminance = dot( sampleColor, vec3( 0.2126, 0.7152, 0.0722 ) );
		return log( max( luminance, 0.0001 ) );
	}
	return sampleColor.r;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec2 halfTexel = invTexSize * 0.5;

	float value = 0.0;
	value += SampleValue( uv + vec2( -halfTexel.x, -halfTexel.y ) );
	value += SampleValue( uv + vec2( halfTexel.x, -halfTexel.y ) );
	value += SampleValue( uv + vec2( -halfTexel.x, halfTexel.y ) );
	value += SampleValue( uv + vec2( halfTexel.x, halfTexel.y ) );
	value *= 0.25;

	gl_FragColor = vec4( value, value, value, 1.0 );
}
