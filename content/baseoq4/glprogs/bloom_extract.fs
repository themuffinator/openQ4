uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float bloomThreshold;
uniform float bloomSoftKnee;

float BrightMask( vec3 color ) {
	float brightness = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	float knee = max( bloomThreshold * bloomSoftKnee, 0.0 );
	if ( bloomThreshold <= 0.0001 ) {
		return 1.0;
	}

	if ( knee > 0.0001 ) {
		float kneeStart = max( bloomThreshold - knee, 0.0 );
		return smoothstep( kneeStart, bloomThreshold, brightness );
	}

	return brightness >= bloomThreshold ? 1.0 : 0.0;
}

vec3 FilteredScene( vec2 uv ) {
	vec2 texel = invTexSize;
	vec3 color = texture2D( Scene, uv ).rgb * 0.25;

	color += texture2D( Scene, uv + vec2( texel.x, 0.0 ) ).rgb * 0.125;
	color += texture2D( Scene, uv - vec2( texel.x, 0.0 ) ).rgb * 0.125;
	color += texture2D( Scene, uv + vec2( 0.0, texel.y ) ).rgb * 0.125;
	color += texture2D( Scene, uv - vec2( 0.0, texel.y ) ).rgb * 0.125;

	color += texture2D( Scene, uv + vec2( texel.x, texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv + vec2( -texel.x, texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv + vec2( texel.x, -texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv - vec2( texel.x, texel.y ) ).rgb * 0.0625;

	return color;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec3 color = FilteredScene( uv );
	gl_FragColor = vec4( color * BrightMask( color ), 1.0 );
}
