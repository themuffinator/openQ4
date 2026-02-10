uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float range;
uniform float focus;
uniform vec4 approachColor;
uniform float approachPercent;
uniform float distanceScale;

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );

	float rangeNorm = clamp( ( range - 1.0 ) * 0.2, 0.0, 1.0 );
	float focusNorm = clamp( focus, 0.0, 1.0 );
	float centerDistance = distance( uv, vec2( 0.5, 0.5 ) );
	float edgeBlur = smoothstep( focusNorm * 0.25, focusNorm * 0.9 + 0.05, centerDistance );
	float distanceBias = clamp( distanceScale / 2000.0, 0.0, 1.0 );
	float blurAmount = rangeNorm * edgeBlur * ( 0.7 + 0.3 * distanceBias );

	vec2 tap = invTexSize * ( 1.0 + rangeNorm * 6.0 );
	vec4 blur = scene * 0.227027;
	blur += texture2D( Scene, uv + vec2( tap.x, 0.0 ) * blurAmount ) * 0.1945946;
	blur += texture2D( Scene, uv - vec2( tap.x, 0.0 ) * blurAmount ) * 0.1945946;
	blur += texture2D( Scene, uv + vec2( 0.0, tap.y ) * blurAmount ) * 0.1945946;
	blur += texture2D( Scene, uv - vec2( 0.0, tap.y ) * blurAmount ) * 0.1945946;
	blur += texture2D( Scene, uv + vec2( tap.x, tap.y ) * blurAmount ) * 0.1216216;
	blur += texture2D( Scene, uv + vec2( tap.x, -tap.y ) * blurAmount ) * 0.1216216;
	blur += texture2D( Scene, uv + vec2( -tap.x, tap.y ) * blurAmount ) * 0.1216216;
	blur += texture2D( Scene, uv + vec2( -tap.x, -tap.y ) * blurAmount ) * 0.1216216;

	vec4 mixed = mix( scene, blur, clamp( blurAmount * 1.25, 0.0, 1.0 ) );
	float tintAmount = clamp( approachPercent * approachColor.a, 0.0, 1.0 );
	mixed.rgb = mix( mixed.rgb, approachColor.rgb, tintAmount );
	mixed.a = scene.a;

	gl_FragColor = mixed;
}
