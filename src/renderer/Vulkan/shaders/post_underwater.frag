#version 450

// openQ4 underwater view. A port of content/baseoq4/pak0/glprogs/underwater.fs,
// which RB_STD_Underwater draws on the OpenGL backend; see that file for how
// absorption, in-scattering, scattering blur, bloom and the surface effects
// build the volume. The body below is that file with its uniforms moved into a
// block. fragUV is OpenGL's texture coordinate; see post_ssao.frag.

layout(set = 0, binding = 0) uniform sampler2D Scene;
layout(set = 1, binding = 0) uniform sampler2D SceneDepth;

layout(std140, set = 6, binding = 0) uniform UnderwaterBlock {
    vec4 texInfo;			// xy: invTexSize, zw: texScale ( viewport size / scene texture size )
    vec4 depthInfo;			// xy: depthProjection ( P[10], P[14] ), z: underwaterAmount, w: timeSeconds
    vec4 tint;				// rgb: what this liquid lets through at fogDistance
    vec4 fogParams;			// x: fogDistance, y: hasDepth, z: aspect
    vec4 effectParams0;		// x: warp, y: blur, z: vignette, w: caustics
    vec4 effectParams1;		// x: bloom, y: aberration, z: particles
} block;

#define invTexSize			block.texInfo.xy
#define texScale			block.texInfo.zw
#define depthProjection		block.depthInfo.xy
#define underwaterAmount	block.depthInfo.z
#define timeSeconds			block.depthInfo.w
#define underwaterTint		block.tint.rgb
#define fogParams			block.fogParams
#define effectParams0		block.effectParams0
#define effectParams1		block.effectParams1

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

float ViewSpaceZFromDepth( float depth ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = ndcDepth + depthProjection.x;
	if ( abs( denom ) < 0.00001 ) {
		denom = ( denom < 0.0 ) ? -0.00001 : 0.00001;
	}
	return ( -depthProjection.y ) / denom;
}

float TravelFraction( vec2 uv ) {
	if ( fogParams.y < 0.5 ) {
		return 0.5;
	}

	float depth = texture( SceneDepth, uv ).x;
	if ( depth >= 0.9999 ) {
		return 1.0;
	}

	float viewZ = abs( ViewSpaceZFromDepth( depth ) );
	return clamp( viewZ / max( fogParams.x, 1.0 ), 0.0, 1.0 );
}

vec2 RefractionOffset( vec2 norm ) {
	float slow = timeSeconds * 0.9;
	float fast = timeSeconds * 1.7;

	float waveA = sin( norm.y * 11.0 + slow ) * 0.5 + sin( norm.y * 23.0 - fast * 0.6 ) * 0.5;
	float waveB = sin( norm.x *  8.0 - slow * 0.8 ) * 0.5 + sin( norm.x * 17.0 + fast * 0.4 ) * 0.5;

	return vec2( waveA, waveB * 0.55 ) * effectParams0.x;
}

float Hash( vec2 p ) {
	return fract( sin( dot( p, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

vec3 SoftFocus( vec2 uv, vec2 radius, vec2 uvMax ) {
	vec3 total = texture( Scene, uv ).rgb;
	if ( radius.x <= 0.0 ) {
		return total;
	}

	float angle = Hash( uv ) * PI * 2.0;

	for ( int i = 0; i < 6; i++ ) {
		float step = angle + float( i ) * ( PI / 3.0 );
		vec2 offset = vec2( cos( step ), sin( step ) ) * radius;
		total += texture( Scene, clamp( uv + offset, vec2( 0.0 ), uvMax ) ).rgb;
	}

	return total / 7.0;
}

vec3 Bloom( vec2 uv, vec2 radius, vec2 uvMax ) {
	vec3 total = vec3( 0.0 );
	float angle = Hash( uv + vec2( 0.37, 0.11 ) ) * PI * 2.0;

	for ( int i = 0; i < 12; i++ ) {
		float t = ( float( i ) + 0.5 ) / 12.0;
		float step = angle + t * PI * 4.0;
		vec2 offset = vec2( cos( step ), sin( step ) ) * radius * sqrt( t );

		vec3 c = texture( Scene, clamp( uv + offset, vec2( 0.0 ), uvMax ) ).rgb;
		float bright = max( max( c.r, c.g ), c.b );
		total += c * smoothstep( 0.55, 1.0, bright ) * ( 1.0 - t * 0.5 );
	}

	return total / 12.0;
}

float Caustics( vec2 norm ) {
	vec2 p = norm * 9.0;
	float t = timeSeconds * 0.6;
	float a = sin( p.x + t ) + sin( p.y * 1.3 - t * 0.8 );
	float b = sin( ( p.x + p.y ) * 0.7 - t * 1.1 );
	float pattern = ( a + b ) * 0.25 + 0.5;
	return pow( clamp( pattern, 0.0, 1.0 ), 3.0 );
}

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
	vec2 uv = fragUV * texScale;
	float amount = clamp( underwaterAmount, 0.0, 1.0 );

	if ( amount <= 0.0 ) {
		outColor = vec4( texture( Scene, uv ).rgb, 1.0 );
		return;
	}

	vec2 uvMax = texScale - invTexSize;
	vec2 norm = uv / max( texScale, vec2( 0.0001 ) );

	vec2 centred = norm * 2.0 - 1.0;
	float radius = length( centred );

	float focusMask = smoothstep( 0.35, 1.15, radius );

	vec2 warped = uv + RefractionOffset( norm ) * amount * texScale;
	warped = clamp( warped, vec2( 0.0 ), uvMax );

	float travel = TravelFraction( warped );

	float blurTexels = amount * ( effectParams0.y * travel * 4.0 + effectParams0.z * focusMask * 7.0 );
	vec3 scene = SoftFocus( warped, invTexSize * blurTexels, uvMax );

	float aberration = effectParams1.y * amount * focusMask;
	if ( aberration > 0.0 ) {
		vec2 dir = ( radius > 0.0001 ) ? normalize( centred ) : vec2( 0.0 );
		vec2 shift = dir * aberration * invTexSize * 6.0;
		scene.r = texture( Scene, clamp( warped + shift, vec2( 0.0 ), uvMax ) ).r;
		scene.b = texture( Scene, clamp( warped - shift, vec2( 0.0 ), uvMax ) ).b;
	}

	vec3 transmittance = pow( max( underwaterTint, vec3( 0.004 ) ), vec3( travel ) );
	transmittance = mix( vec3( 1.0 ), transmittance, amount );

	// In-scattering puts back the liquid's own colour by the light lost across all three channels,
	// as underwater.fs does; per-channel loss put back the absorbed colours (teal in lava).
	vec3 scatterColor = underwaterTint * ( 0.50 + 0.30 * ( 1.0 - radius * 0.5 ) );
	float extinction = 1.0 - dot( transmittance, vec3( 1.0 / 3.0 ) );
	vec3 lit = scene * transmittance + scatterColor * extinction * amount;

	float bloomAmount = effectParams1.x * amount * ( 0.45 + travel * 0.85 );
	if ( bloomAmount > 0.0 ) {
		vec2 bloomRadius = invTexSize * ( 14.0 + travel * 26.0 );
		lit += Bloom( warped, bloomRadius, uvMax ) * underwaterTint * bloomAmount;
	}

	lit += underwaterTint * Caustics( norm ) * effectParams0.w * amount * ( 1.0 - travel * 0.7 );

	lit += vec3( 0.8, 0.9, 1.0 ) * underwaterTint * Particles( norm ) * effectParams1.z * amount;

	outColor = vec4( lit, 1.0 );
}
