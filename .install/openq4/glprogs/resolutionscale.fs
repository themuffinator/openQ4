uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform vec2 invLowResSize;
uniform float sharpenAmount;

vec3 SampleLowRes( vec2 uv ) {
	vec2 lowInv = max( invLowResSize, vec2( 0.000001, 0.000001 ) );
	vec2 lowSize = 1.0 / lowInv;

	vec2 p = uv * lowSize - vec2( 0.5, 0.5 );
	vec2 i = floor( p );
	vec2 f = p - i;

	vec2 uv00 = ( i + vec2( 0.5, 0.5 ) ) * lowInv;
	vec2 uv10 = uv00 + vec2( lowInv.x, 0.0 );
	vec2 uv01 = uv00 + vec2( 0.0, lowInv.y );
	vec2 uv11 = uv00 + lowInv;

	uv00 = clamp( uv00, vec2( 0.0, 0.0 ), vec2( 1.0, 1.0 ) );
	uv10 = clamp( uv10, vec2( 0.0, 0.0 ), vec2( 1.0, 1.0 ) );
	uv01 = clamp( uv01, vec2( 0.0, 0.0 ), vec2( 1.0, 1.0 ) );
	uv11 = clamp( uv11, vec2( 0.0, 0.0 ), vec2( 1.0, 1.0 ) );

	vec3 c00 = texture2D( Scene, uv00 ).rgb;
	vec3 c10 = texture2D( Scene, uv10 ).rgb;
	vec3 c01 = texture2D( Scene, uv01 ).rgb;
	vec3 c11 = texture2D( Scene, uv11 ).rgb;

	vec3 c0 = mix( c00, c10, f.x );
	vec3 c1 = mix( c01, c11, f.x );
	return mix( c0, c1, f.y );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );

	vec3 center = SampleLowRes( uv );
	if ( sharpenAmount <= 0.0001 ) {
		gl_FragColor = vec4( center, scene.a );
		return;
	}

	vec3 north = SampleLowRes( uv + vec2( 0.0, invTexSize.y ) );
	vec3 south = SampleLowRes( uv - vec2( 0.0, invTexSize.y ) );
	vec3 east = SampleLowRes( uv + vec2( invTexSize.x, 0.0 ) );
	vec3 west = SampleLowRes( uv - vec2( invTexSize.x, 0.0 ) );
	vec3 northEast = SampleLowRes( uv + vec2( invTexSize.x, invTexSize.y ) );
	vec3 northWest = SampleLowRes( uv + vec2( -invTexSize.x, invTexSize.y ) );
	vec3 southEast = SampleLowRes( uv + vec2( invTexSize.x, -invTexSize.y ) );
	vec3 southWest = SampleLowRes( uv + vec2( -invTexSize.x, -invTexSize.y ) );

	vec3 blur = ( center * 4.0 +
		( north + south + east + west ) * 2.0 +
		( northEast + northWest + southEast + southWest ) ) * ( 1.0 / 16.0 );

	vec3 sharpened = center + ( center - blur ) * sharpenAmount;
	sharpened = clamp( sharpened, 0.0, 1.0 );
	gl_FragColor = vec4( sharpened, scene.a );
}
