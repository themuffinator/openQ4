// openQ4 underwater view.
//
// A scene pass: it runs inside the 3D view, on the finished world, before the HUD and any menu are
// drawn, so only the world goes under the water.
//
// The thing that makes water read as a volume rather than a colour filter is that everything it
// does gets stronger with distance. So this is built around the depth buffer:
//
//   1. Absorption (Beer-Lambert). Light loses red first, then green, then blue, exponentially with
//      how far it travelled. underwaterTint is the transmittance at fogDistance, so the per-channel
//      extinction falls out of it directly and a designer only has to pick a colour and a range.
//   2. In-scattering. What absorption takes away, the water puts back as its own colour - that is
//      why distant things go flat and pale rather than simply dark. Together these two are what
//      turns a tint into a volume.
//   3. Scattering blur. Distance softens; near surfaces stay sharp. A rotated six-tap ring, so the
//      falloff has no axis to give it away, with a small radial term on top for the mask.
//   4. Bloom. Suspended particulate throws light sideways, so every bright source grows a halo -
//      and the halo grows with the distance the light has travelled to reach you. This is the most
//      recognisable single cue that a scene is underwater.
//   5. Refraction, chromatic aberration, vignette, caustics and marine snow: the lens and the
//      surface overhead.

uniform sampler2D Scene;
uniform sampler2D SceneDepth;

uniform vec2 invTexSize;
uniform vec2 texScale;				// viewport size / scene texture size
uniform vec2 depthProjection;		// projectionMatrix[10], [14]
uniform float underwaterAmount;		// 0 out of the liquid, 1 fully submerged
uniform vec3 underwaterTint;		// what this liquid lets through at fogDistance
uniform vec4 fogParams;				// x fogDistance, y hasDepth, z aspect, w unused
uniform vec4 effectParams0;			// x warp, y blur, z vignette, w caustics
uniform vec4 effectParams1;			// x bloom, y aberration, z particles, w unused
uniform float timeSeconds;

const float PI = 3.14159265;

float ViewSpaceZFromDepth( float depth ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = ndcDepth + depthProjection.x;
	if ( abs( denom ) < 0.00001 ) {
		denom = ( denom < 0.0 ) ? -0.00001 : 0.00001;
	}
	return ( -depthProjection.y ) / denom;
}

// How far light travelled to reach the eye, as a fraction of this liquid's range. Without a depth
// buffer everything is treated as mid-range, which degrades to the old flat look rather than to
// nothing.
float TravelFraction( vec2 uv ) {
	if ( fogParams.y < 0.5 ) {
		return 0.5;
	}

	float depth = texture2D( SceneDepth, uv ).x;
	if ( depth >= 0.9999 ) {
		return 1.0;		// sky, or nothing drawn: as far as this water goes
	}

	float viewZ = abs( ViewSpaceZFromDepth( depth ) );
	return clamp( viewZ / max( fogParams.x, 1.0 ), 0.0, 1.0 );
}

// Two crossed sine layers, in normalised view space. Deliberately low frequency: high frequency
// reads as noise on a moving image, not as water.
vec2 RefractionOffset( vec2 norm ) {
	float slow = timeSeconds * 0.9;
	float fast = timeSeconds * 1.7;

	float waveA = sin( norm.y * 11.0 + slow ) * 0.5 + sin( norm.y * 23.0 - fast * 0.6 ) * 0.5;
	float waveB = sin( norm.x *  8.0 - slow * 0.8 ) * 0.5 + sin( norm.x * 17.0 + fast * 0.4 ) * 0.5;

	// the horizontal wobble is the one the eye notices, so it gets the larger share
	return vec2( waveA, waveB * 0.55 ) * effectParams0.x;
}

float Hash( vec2 p ) {
	return fract( sin( dot( p, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

// Six taps on a ring, rotated per pixel so the pattern does not line up with the view axes.
vec3 SoftFocus( vec2 uv, vec2 radius, vec2 uvMax ) {
	vec3 total = texture2D( Scene, uv ).rgb;
	if ( radius.x <= 0.0 ) {
		return total;
	}

	float angle = Hash( uv ) * PI * 2.0;

	for ( int i = 0; i < 6; i++ ) {
		float step = angle + float( i ) * ( PI / 3.0 );
		vec2 offset = vec2( cos( step ), sin( step ) ) * radius;
		total += texture2D( Scene, clamp( uv + offset, vec2( 0.0 ), uvMax ) ).rgb;
	}

	return total / 7.0;
}

// Bright-pass gathered over a widening spiral. Not a real mip chain - this is one pass - but two
// turns of twelve taps is enough for the soft halo that suspended particulate gives every light.
vec3 Bloom( vec2 uv, vec2 radius, vec2 uvMax ) {
	vec3 total = vec3( 0.0 );
	float angle = Hash( uv + vec2( 0.37, 0.11 ) ) * PI * 2.0;

	for ( int i = 0; i < 12; i++ ) {
		float t = ( float( i ) + 0.5 ) / 12.0;
		float step = angle + t * PI * 4.0;
		vec2 offset = vec2( cos( step ), sin( step ) ) * radius * sqrt( t );

		vec3 c = texture2D( Scene, clamp( uv + offset, vec2( 0.0 ), uvMax ) ).rgb;
		// keep only what is brighter than the scene's own mid tones, softly
		float bright = max( max( c.r, c.g ), c.b );
		total += c * smoothstep( 0.55, 1.0, bright ) * ( 1.0 - t * 0.5 );
	}

	return total / 12.0;
}

// A slow interference pattern standing in for light through a rippled surface. Additive and weak;
// it should be felt rather than seen.
float Caustics( vec2 norm ) {
	vec2 p = norm * 9.0;
	float t = timeSeconds * 0.6;
	float a = sin( p.x + t ) + sin( p.y * 1.3 - t * 0.8 );
	float b = sin( ( p.x + p.y ) * 0.7 - t * 1.1 );
	float pattern = ( a + b ) * 0.25 + 0.5;
	return pow( clamp( pattern, 0.0, 1.0 ), 3.0 );
}

// Marine snow: specks of suspended matter drifting up past the eye. Two sparse layers at different
// depths and speeds, so it has some parallax to it.
float Particles( vec2 norm ) {
	float total = 0.0;

	for ( int layer = 0; layer < 2; layer++ ) {
		float scale = 60.0 + float( layer ) * 45.0;
		float drift = timeSeconds * ( 0.02 + float( layer ) * 0.015 );

		vec2 p = norm * scale + vec2( sin( timeSeconds * 0.3 + float( layer ) ) * 0.5, -drift * scale );
		vec2 cell = floor( p );
		vec2 frac = fract( p ) - 0.5;

		float seed = Hash( cell + float( layer ) * 37.0 );
		if ( seed > 0.985 ) {
			float d = length( frac );
			total += ( 1.0 - smoothstep( 0.0, 0.35, d ) ) * ( 0.6 + 0.4 * seed );
		}
	}

	return total;
}

void main() {
	vec2 uv = gl_TexCoord[0].xy;
	float amount = clamp( underwaterAmount, 0.0, 1.0 );

	if ( amount <= 0.0 ) {
		gl_FragColor = vec4( texture2D( Scene, uv ).rgb, 1.0 );
		return;
	}

	// the last texel the view actually owns; sampling past it would drag in whatever else is
	// resident in the scene texture
	vec2 uvMax = texScale - invTexSize;
	vec2 norm = uv / max( texScale, vec2( 0.0001 ) );

	// Distance from the centre of the view, in normalised space so the mask tracks the frame
	// rather than the pixel aspect: 1 at the edge midpoints, ~1.41 in the corners.
	vec2 centred = norm * 2.0 - 1.0;
	float radius = length( centred );

	// The focus mask. This is a vignette in shape only - it never darkens anything. It picks out
	// where the view goes soft, which is what a dive mask and the water in front of it actually do:
	// the centre stays readable, the periphery loses its edges.
	float focusMask = smoothstep( 0.35, 1.15, radius );

	vec2 warped = uv + RefractionOffset( norm ) * amount * texScale;
	warped = clamp( warped, vec2( 0.0 ), uvMax );

	float travel = TravelFraction( warped );

	// Two reasons the image goes soft, kept separate so they can be tuned against each other:
	// distance through the water, and the focus mask toward the edges of the view.
	float blurTexels = amount * ( effectParams0.y * travel * 4.0 + effectParams0.z * focusMask * 7.0 );
	vec3 scene = SoftFocus( warped, invTexSize * blurTexels, uvMax );

	// Chromatic aberration, pulling the channels apart toward the edges the way a curved faceplate
	// does. Sampled from the already-blurred position so it does not fight the soft focus.
	float aberration = effectParams1.y * amount * focusMask;
	if ( aberration > 0.0 ) {
		vec2 dir = ( radius > 0.0001 ) ? normalize( centred ) : vec2( 0.0 );
		vec2 shift = dir * aberration * invTexSize * 6.0;
		scene.r = texture2D( Scene, clamp( warped + shift, vec2( 0.0 ), uvMax ) ).r;
		scene.b = texture2D( Scene, clamp( warped - shift, vec2( 0.0 ), uvMax ) ).b;
	}

	// 1. Absorption. underwaterTint is the transmittance at fogDistance, so raising it to the
	//    travelled fraction is Beer-Lambert with the extinction implied by the colour itself.
	vec3 transmittance = pow( max( underwaterTint, vec3( 0.004 ) ), vec3( travel ) );
	transmittance = mix( vec3( 1.0 ), transmittance, amount );

	// 2. In-scattering. What the water takes out it puts back as its own colour, which is what
	//    stops distance reading as simple darkness. How much it puts back is the light lost across
	//    all three channels: weighting each channel by its own loss put back the colours the liquid
	//    absorbs instead, so lava, which never absorbs red, filled with a teal haze.
	vec3 scatterColor = underwaterTint * ( 0.50 + 0.30 * ( 1.0 - radius * 0.5 ) );
	float extinction = 1.0 - dot( transmittance, vec3( 1.0 / 3.0 ) );
	vec3 lit = scene * transmittance + scatterColor * extinction * amount;

	// 3. Bloom. The halo grows with the water the light had to cross to reach the eye.
	float bloomAmount = effectParams1.x * amount * ( 0.45 + travel * 0.85 );
	if ( bloomAmount > 0.0 ) {
		vec2 bloomRadius = invTexSize * ( 14.0 + travel * 26.0 );
		lit += Bloom( warped, bloomRadius, uvMax ) * underwaterTint * bloomAmount;
	}

	// caustics from the surface overhead, strongest on what is close
	lit += underwaterTint * Caustics( norm ) * effectParams0.w * amount * ( 1.0 - travel * 0.7 );

	// marine snow, drifting between the eye and the world
	lit += vec3( 0.8, 0.9, 1.0 ) * underwaterTint * Particles( norm ) * effectParams1.z * amount;

	gl_FragColor = vec4( lit, 1.0 );
}
