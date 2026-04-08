uniform sampler2D ColorTex;
uniform sampler2D BlendTex;
uniform vec2 invTexSize;

void main() {
	vec2 texcoord = gl_TexCoord[0].st;

	vec4 a;
	a.x = texture2D( BlendTex, texcoord + vec2( invTexSize.x, 0.0 ) ).a;
	a.y = texture2D( BlendTex, texcoord + vec2( 0.0, invTexSize.y ) ).g;
	a.wz = texture2D( BlendTex, texcoord ).xz;

	if ( dot( a, vec4( 1.0, 1.0, 1.0, 1.0 ) ) < 0.00001 ) {
		gl_FragColor = texture2D( ColorTex, texcoord );
		return;
	}

	bool horizontal = max( a.x, a.z ) > max( a.y, a.w );
	vec4 blendingOffset = vec4( 0.0, a.y, 0.0, a.w );
	vec2 blendingWeight = a.yw;

	if ( horizontal ) {
		blendingOffset = vec4( a.x, 0.0, a.z, 0.0 );
		blendingWeight = a.xz;
	}

	blendingWeight /= max( dot( blendingWeight, vec2( 1.0, 1.0 ) ), 0.00001 );

	vec4 blendingCoord = blendingOffset * vec4( invTexSize.xy, -invTexSize.xy ) + texcoord.xyxy;
	vec4 color = blendingWeight.x * texture2D( ColorTex, blendingCoord.xy );
	color += blendingWeight.y * texture2D( ColorTex, blendingCoord.zw );

	gl_FragColor = color;
}
