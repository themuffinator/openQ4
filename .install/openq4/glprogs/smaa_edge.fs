uniform sampler2D ColorTex;
uniform vec2 invTexSize;

const float kThreshold = 0.1;
const float kLocalContrastAdaptationFactor = 2.0;
const vec3 kLumaWeights = vec3( 0.2126, 0.7152, 0.0722 );

float SampleLuma( vec2 uv ) {
	return dot( texture2D( ColorTex, uv ).rgb, kLumaWeights );
}

void main() {
	vec2 texcoord = gl_TexCoord[0].st;
	vec4 offset0 = invTexSize.xyxy * vec4( -1.0, 0.0, 0.0, -1.0 ) + texcoord.xyxy;
	vec4 offset1 = invTexSize.xyxy * vec4( 1.0, 0.0, 0.0, 1.0 ) + texcoord.xyxy;
	vec4 offset2 = invTexSize.xyxy * vec4( -2.0, 0.0, 0.0, -2.0 ) + texcoord.xyxy;

	float luma = SampleLuma( texcoord );
	float lumaLeft = SampleLuma( offset0.xy );
	float lumaTop = SampleLuma( offset0.zw );

	vec4 delta;
	delta.xy = abs( luma - vec2( lumaLeft, lumaTop ) );

	vec2 edges = step( vec2( kThreshold, kThreshold ), delta.xy );
	if ( dot( edges, vec2( 1.0, 1.0 ) ) == 0.0 ) {
		discard;
	}

	float lumaRight = SampleLuma( offset1.xy );
	float lumaBottom = SampleLuma( offset1.zw );
	delta.zw = abs( luma - vec2( lumaRight, lumaBottom ) );

	vec2 maxDelta = max( delta.xy, delta.zw );

	float lumaLeftLeft = SampleLuma( offset2.xy );
	float lumaTopTop = SampleLuma( offset2.zw );
	delta.zw = abs( vec2( lumaLeft, lumaTop ) - vec2( lumaLeftLeft, lumaTopTop ) );

	maxDelta = max( maxDelta, delta.zw );
	float finalDelta = max( maxDelta.x, maxDelta.y );

	edges *= step( vec2( finalDelta, finalDelta ), vec2( kLocalContrastAdaptationFactor, kLocalContrastAdaptationFactor ) * delta.xy );
	gl_FragColor = vec4( edges, 0.0, 0.0 );
}
