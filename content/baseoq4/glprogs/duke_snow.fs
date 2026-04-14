uniform sampler2D Scene;

void main() {
	vec4 sceneSample = texture2D( Scene, gl_TexCoord[0].st );
	float luminance = dot( sceneSample.rgb, vec3( 0.299, 0.587, 0.114 ) );
	vec3 snowColor = vec3( luminance );
	snowColor = mix( snowColor, vec3( 1.0 ), 0.35 );
	snowColor = clamp( snowColor * 1.18, 0.0, 1.0 );
	gl_FragColor = vec4( snowColor, sceneSample.a );
}
