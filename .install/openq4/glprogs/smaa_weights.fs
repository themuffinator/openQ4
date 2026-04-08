uniform sampler2D EdgesTex;
uniform sampler2D AreaTex;
uniform sampler2D SearchTex;
uniform vec2 invTexSize;

const float kMaxSearchSteps = 8.0;
const float kAreaMaxDistance = 16.0;
const vec2 kAreaTexPixelSize = vec2( 1.0 / 160.0, 1.0 / 560.0 );
const float kAreaTexSubtexSize = 1.0 / 7.0;
const vec2 kSearchTexSize = vec2( 66.0, 33.0 );
const vec2 kSearchTexPackedSize = vec2( 64.0, 16.0 );

vec2 RoundVec2( vec2 value ) {
	return floor( value + vec2( 0.5, 0.5 ) );
}

float SampleSearchLength( vec2 e, float offsetValue ) {
	vec2 scale = kSearchTexSize * vec2( 0.5, -1.0 );
	vec2 bias = kSearchTexSize * vec2( offsetValue, 1.0 );

	scale += vec2( -1.0, 1.0 );
	bias += vec2( 0.5, -0.5 );

	scale /= kSearchTexPackedSize;
	bias /= kSearchTexPackedSize;

	return texture2D( SearchTex, scale * e + bias ).r;
}

float SearchXLeft( vec2 texcoord, float endValue ) {
	vec2 e = vec2( 0.0, 1.0 );
	while ( texcoord.x > endValue && e.g > 0.8281 && e.r == 0.0 ) {
		e = texture2D( EdgesTex, texcoord ).rg;
		texcoord -= vec2( 2.0 * invTexSize.x, 0.0 );
	}

	float offsetValue = -( 255.0 / 127.0 ) * SampleSearchLength( e, 0.0 ) + 3.25;
	return invTexSize.x * offsetValue + texcoord.x;
}

float SearchXRight( vec2 texcoord, float endValue ) {
	vec2 e = vec2( 0.0, 1.0 );
	while ( texcoord.x < endValue && e.g > 0.8281 && e.r == 0.0 ) {
		e = texture2D( EdgesTex, texcoord ).rg;
		texcoord += vec2( 2.0 * invTexSize.x, 0.0 );
	}

	float offsetValue = -( 255.0 / 127.0 ) * SampleSearchLength( e, 0.5 ) + 3.25;
	return -invTexSize.x * offsetValue + texcoord.x;
}

float SearchYUp( vec2 texcoord, float endValue ) {
	vec2 e = vec2( 1.0, 0.0 );
	while ( texcoord.y > endValue && e.r > 0.8281 && e.g == 0.0 ) {
		e = texture2D( EdgesTex, texcoord ).rg;
		texcoord -= vec2( 0.0, 2.0 * invTexSize.y );
	}

	float offsetValue = -( 255.0 / 127.0 ) * SampleSearchLength( e.gr, 0.0 ) + 3.25;
	return invTexSize.y * offsetValue + texcoord.y;
}

float SearchYDown( vec2 texcoord, float endValue ) {
	vec2 e = vec2( 1.0, 0.0 );
	while ( texcoord.y < endValue && e.r > 0.8281 && e.g == 0.0 ) {
		e = texture2D( EdgesTex, texcoord ).rg;
		texcoord += vec2( 0.0, 2.0 * invTexSize.y );
	}

	float offsetValue = -( 255.0 / 127.0 ) * SampleSearchLength( e.gr, 0.5 ) + 3.25;
	return -invTexSize.y * offsetValue + texcoord.y;
}

vec2 SampleArea( vec2 dist, float e1, float e2, float offsetValue ) {
	vec2 texcoord = vec2( kAreaMaxDistance, kAreaMaxDistance ) * RoundVec2( 4.0 * vec2( e1, e2 ) ) + dist;
	texcoord = kAreaTexPixelSize * texcoord + 0.5 * kAreaTexPixelSize;
	texcoord.y = kAreaTexSubtexSize * offsetValue + texcoord.y;
	return texture2D( AreaTex, texcoord ).rg;
}

void main() {
	vec2 texcoord = gl_TexCoord[0].st;
	vec2 rtSize = 1.0 / max( invTexSize, vec2( 0.000001, 0.000001 ) );
	vec2 pixcoord = texcoord * rtSize;

	vec4 offset0 = invTexSize.xyxy * vec4( -0.25, -0.125, 1.25, -0.125 ) + texcoord.xyxy;
	vec4 offset1 = invTexSize.xyxy * vec4( -0.125, -0.25, -0.125, 1.25 ) + texcoord.xyxy;
	vec4 offset2 = vec4( invTexSize.x, invTexSize.x, invTexSize.y, invTexSize.y ) *
		( vec4( -2.0, 2.0, -2.0, 2.0 ) * kMaxSearchSteps ) +
		vec4( offset0.x, offset0.z, offset1.y, offset1.w );

	vec4 weights = vec4( 0.0, 0.0, 0.0, 0.0 );
	vec2 e = texture2D( EdgesTex, texcoord ).rg;

	if ( e.g > 0.0 ) {
		vec3 coords;
		coords.x = SearchXLeft( offset0.xy, offset2.x );
		coords.y = offset1.y;
		float leftEdge = texture2D( EdgesTex, coords.xy ).r;

		coords.z = SearchXRight( offset0.zw, offset2.y );
		vec2 d = abs( RoundVec2( rtSize.xx * vec2( coords.x, coords.z ) - pixcoord.xx ) );
		vec2 sqrtD = sqrt( d );
		float rightEdge = texture2D( EdgesTex, coords.zy + vec2( invTexSize.x, 0.0 ) ).r;

		weights.rg = SampleArea( sqrtD, leftEdge, rightEdge, 0.0 );
	}

	if ( e.r > 0.0 ) {
		vec3 coords;
		coords.y = SearchYUp( offset1.xy, offset2.z );
		coords.x = offset0.x;
		float topEdge = texture2D( EdgesTex, coords.xy ).g;

		coords.z = SearchYDown( offset1.zw, offset2.w );
		vec2 d = abs( RoundVec2( rtSize.yy * vec2( coords.y, coords.z ) - pixcoord.yy ) );
		vec2 sqrtD = sqrt( d );
		float bottomEdge = texture2D( EdgesTex, coords.xz + vec2( 0.0, invTexSize.y ) ).g;

		weights.ba = SampleArea( sqrtD, topEdge, bottomEdge, 0.0 );
	}

	gl_FragColor = weights;
}
