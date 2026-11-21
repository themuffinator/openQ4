#version 450

// Screen-space ambient occlusion. A port of
// content/baseoq4/pak0/glprogs/ssao.fs, which RB_STD_SSAO draws on the OpenGL
// backend; the body below is that file with the uniforms moved into a block.
//
// fragUV is OpenGL's texture coordinate (0,0 = bottom-left of the view): the
// pass draws into the scene with a negative-height viewport, and every
// sampled image is a VK_Exec_CopyRender capture, stored bottom-up like an
// OpenGL texture.

layout(set = 0, binding = 0) uniform sampler2D Scene;
layout(set = 1, binding = 0) uniform sampler2D DepthBuffer;
layout(set = 2, binding = 0) uniform sampler2D FinalDepthBuffer;
// Native PBR views (vk_PostProcess.cpp VK_PostProcess_PrepareSSAOField): the
// world depth without native PBR surfaces. AOField is the occlusion target
// mode 3 wrote (modes 1 and 4) or the field mode 1 wrote before lighting
// (mode 2).
layout(set = 3, binding = 0) uniform sampler2D ClassicDepthBuffer;
layout(set = 4, binding = 0) uniform sampler2D AOField;

layout(std140, set = 6, binding = 0) uniform SSAOBlock {
    vec4 texInfo;			// xy: invTexSize, z: projectionScale, w: debug view
    vec4 projection;		// 1/P[0], 1/P[5], P[8], P[9]
    vec4 depthInfo;			// x: P[10], y: P[14]
    vec4 params;			// x: radius, y: bias, z: intensity, w: power
    vec4 params2;			// x: max distance, y: sample count, zw: the filter pass's texel step (mode 5)
    vec4 modeInfo;			// x: mode, y: the field's rows run opposite to this target's
    vec4 halfInfo;			// xy: occlusion target size, zw: view size in pixels
    vec4 methodInfo;		// x: r_ssaoMethod (0 SSAO, 1 HBAO, 2 GTAO, 3 reference), y: temporal pattern turn, z: classic multi-bounce albedo (GTAO)
} block;

#define invTexSize			block.texInfo.xy
#define projectionScale		block.texInfo.z
#define ssaoDebugView		block.texInfo.w
#define projectionInfo		block.projection
#define depthProjection		block.depthInfo.xy
#define ssaoRadius			block.params.x
#define ssaoBias			block.params.y
#define ssaoIntensity		block.params.z
#define ssaoPower			block.params.w
#define ssaoMaxDistance		block.params2.x
#define ssaoSampleCount		block.params2.y
#define blurAxis			block.params2.zw
#define aoInfo				block.methodInfo
// 0 unfiltered: darken the lit scene in one pass, the fallback when the
// occlusion target cannot be created. 1 field: before lighting, write the
// occlusion native PBR applies to its indirect light (r) and the factor for
// classic world pixels (g); b marks pixels with world depth. 2 apply: darken
// only the classic world pixels with the field's factor. 3 occlusion: the raw
// AO of one view pixel per texel of the occlusion target, at full or half
// resolution (r: AO, g: view depth, negative where the pixel takes none,
// ba: the view depth's slope per view pixel). 4 resolve: darken the lit scene
// with the filtered occlusion target. 5 filter: one axis of the separable
// tent that cancels the occlusion target's sample patterns.
#define ssaoMode			block.modeInfo.x
#define ssaoFieldFlip		block.modeInfo.y
#define ssaoMultiply		block.modeInfo.z

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float kGoldenAngle = 2.39996323;
const float kTwoPi = 6.28318531;
const int kMaxSamples = 32;

float SampleDepth( vec2 uv ) {
	return texture( DepthBuffer, uv ).x;
}

float SampleFinalDepth( vec2 uv ) {
	return texture( FinalDepthBuffer, uv ).x;
}

float ViewSpaceZFromDepth( float depth ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = ndcDepth + depthProjection.x;
	if ( abs( denom ) < 0.00001 ) {
		denom = ( denom < 0.0 ) ? -0.00001 : 0.00001;
	}
	return ( -depthProjection.y ) / denom;
}

vec3 ReconstructViewPosition( vec2 uv, float depth ) {
	float viewZ = ViewSpaceZFromDepth( depth );
	vec2 ndc = uv * 2.0 - 1.0;

	return vec3(
		-viewZ * ( ndc.x + projectionInfo.z ) * projectionInfo.x,
		-viewZ * ( ndc.y + projectionInfo.w ) * projectionInfo.y,
		viewZ );
}

vec3 PickBestDerivative( vec3 centerPos, vec3 negativePos, vec3 positivePos ) {
	vec3 negativeDelta = centerPos - negativePos;
	vec3 positiveDelta = positivePos - centerPos;
	return ( abs( negativeDelta.z ) < abs( positiveDelta.z ) ) ? negativeDelta : positiveDelta;
}

// The surface normal, and in slope the change of view depth across one view
// pixel to the right and one up, from the same differences.
vec3 ReconstructNormal( vec2 uv, vec3 centerPos, out vec2 slope ) {
	vec2 offsetX = vec2( invTexSize.x, 0.0 );
	vec2 offsetY = vec2( 0.0, invTexSize.y );

	vec3 leftPos = ReconstructViewPosition( uv - offsetX, SampleDepth( uv - offsetX ) );
	vec3 rightPos = ReconstructViewPosition( uv + offsetX, SampleDepth( uv + offsetX ) );
	vec3 downPos = ReconstructViewPosition( uv - offsetY, SampleDepth( uv - offsetY ) );
	vec3 upPos = ReconstructViewPosition( uv + offsetY, SampleDepth( uv + offsetY ) );

	vec3 tangent = PickBestDerivative( centerPos, leftPos, rightPos );
	vec3 bitangent = PickBestDerivative( centerPos, downPos, upPos );
	vec3 normal = normalize( cross( tangent, bitangent ) );
	slope = vec2( -tangent.z, -bitangent.z );

	if ( dot( normal, centerPos ) > 0.0 ) {
		normal = -normal;
	}

	return normal;
}

// The ordered-dither index (0..15) of a texel. Neighbours differ as much as
// possible and every 4x4 window holds each index once, so the separable tent
// of the filter (FilterMain) weighs all sixteen sample patterns equally and
// the pattern cancels instead of showing as dither.
float Bayer2( vec2 cell ) {
	return 2.0 * abs( cell.x - cell.y ) + cell.y;
}

float PatternIndex( vec2 texel ) {
	return 4.0 * Bayer2( mod( texel, 2.0 ) ) + Bayer2( mod( floor( texel * 0.5 ), 2.0 ) );
}

// ---- r_ssaoMethod 1 to 3 (REN-10): 1 HBAO, 2 GTAO, 3 the brute-force
// reference. aoInfo.x selects the kernel, aoInfo.y turns the sample patterns
// by a fraction of one pattern per frame under temporal AA (HBAO and GTAO),
// aoInfo.z is the grey albedo classic surfaces bounce light with (GTAO; 0 off).

const float kPi = 3.14159265;
const float kHalfPi = 1.57079633;

float FastAcos( float x ) {
	float ax = min( abs( x ), 1.0 );
	float r = ( -0.156583 * ax + kHalfPi ) * sqrt( 1.0 - ax );
	return x >= 0.0 ? r : kPi - r;
}

// The cosine-weighted visible arc of one side of a slice, from the view
// direction out to horizon angle h, with the normal at angle n in the slice
// (Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect
// Occlusion").
float IntegrateArc( float h, float n, float cosN, float sinN ) {
	return 0.25 * ( -cos( 2.0 * h - n ) + cosN + 2.0 * h * sinN );
}

vec2 ProjectViewPosition( vec3 position ) {
	vec2 ndc = vec2( position.x / ( -position.z * projectionInfo.x ) - projectionInfo.z,
		position.y / ( -position.z * projectionInfo.y ) - projectionInfo.w );
	return ndc * 0.5 + 0.5;
}

// Horizon-based ambient occlusion (Bavoil, Sainz and Dimitrov 2008,
// "Image-Space Horizon-Based Ambient Occlusion"): per screen direction, march
// outward and, each time a sample rises above the highest horizon so far
// (its elevation measured above the pixel's tangent plane), add the rise in
// the horizon's sine, attenuated by 1 - (distance / radius)^2. The result is
// one minus the mean over directions. Directions are weighted evenly, not by
// cosine, so a raised horizon darkens more than GTAO's (sin h against
// sin^2 h for a horizon at elevation h all round), while the angle bias and
// the attenuation discount near and far occluders; r_ssaoIntensity and
// r_ssaoPower set the strength, as for SSAO. r_ssaoSamples is the depth budget: four directions of up to
// eight steps below 24 samples (Quality's 12 is four of three), eight at 24
// and above (Ultra's 32 is eight of four). The sixteen patterns turn the
// directions by a sixteenth of the angle between them and offset the steps,
// so the filter averages 64 (Quality) or 128 (Ultra) directions. The tangent
// plane is biased up by a sine of 0.1 so a surface never occludes itself.
const float kHBAOAngleBias = 0.1;

float HorizonBasedOcclusion( vec2 uv, vec3 centerPos, vec3 normal, float radiusPixels, float patternIndex ) {
	float directionCount = ssaoSampleCount >= 24.0 ? 8.0 : 4.0;
	float stepCount = clamp( floor( ssaoSampleCount / directionCount ), 2.0, 8.0 );
	float stepOffset = fract( ( mod( patternIndex * 7.0, 16.0 ) + 0.5 ) / 16.0 + aoInfo.y * 0.618034 );
	float radiusSq = ssaoRadius * ssaoRadius;
	float occlusion = 0.0;
	for ( int d = 0; d < 8; ++d ) {
		if ( float( d ) >= directionCount ) {
			break;
		}
		float phi = ( float( d ) + ( patternIndex + 0.5 + aoInfo.y ) / 16.0 ) / directionCount * kTwoPi;
		vec2 direction = vec2( cos( phi ), sin( phi ) );
		float horizon = kHBAOAngleBias;
		float directional = 0.0;
		for ( int i = 0; i < 8; ++i ) {
			if ( float( i ) >= stepCount ) {
				break;
			}
			// Even spacing out to the radius; whole-pixel offsets keep each
			// reconstructed position on the texel its depth came from.
			float t = ( float( i ) + stepOffset ) / stepCount;
			vec2 sampleUv = uv + floor( direction * ( t * radiusPixels + 1.0 ) + 0.5 ) * invTexSize;
			if ( sampleUv.x < 0.0 || sampleUv.y < 0.0 || sampleUv.x > 1.0 || sampleUv.y > 1.0 ) {
				break;
			}
			float sampleDepth = SampleDepth( sampleUv );
			if ( sampleDepth >= 0.99999 ) {
				continue;
			}
			vec3 delta = ReconstructViewPosition( sampleUv, sampleDepth ) - centerPos;
			float distanceSq = dot( delta, delta );
			if ( distanceSq > radiusSq || distanceSq < 0.0001 ) {
				continue;
			}
			float elevation = dot( normal, delta ) * inversesqrt( distanceSq );
			if ( elevation > horizon ) {
				directional += ( elevation - horizon ) * ( 1.0 - distanceSq / radiusSq );
				horizon = elevation;
			}
		}
		occlusion += directional;
	}
	// Like SSAO, and unlike GTAO, HBAO is an estimate rather than a
	// visibility: r_ssaoIntensity and r_ssaoPower set its strength.
	float ao = clamp( 1.0 - occlusion / directionCount * ssaoIntensity, 0.0, 1.0 );
	return pow( ao, max( ssaoPower, 0.001 ) );
}

// Ground-truth ambient occlusion: per slice through the view direction, the
// highest horizon on each side out to the radius, then the cosine-weighted
// visibility between the two horizons, integrated analytically. r_ssaoSamples
// is the depth budget: one slice of up to eight steps a side below 24 samples,
// two slices at 24 and above. The sixteen patterns turn the slices by a
// sixteenth of a half turn and offset the steps, so the filter averages
// sixteen slice directions. Slices turn evenly about the view direction in view
// space and are walked along their own line on the screen: even turns of the
// screen direction are uneven about an off-centre pixel's view direction and
// would darken open surfaces toward the screen's edges. Horizons fade to the surface's own over the last
// fifth of the radius, so the result follows the radius as the brute-force
// reference does.
float GroundTruthOcclusion( vec2 uv, vec3 centerPos, vec3 normal, float radiusPixels, float patternIndex ) {
	vec3 viewDir = normalize( -centerPos );
	vec3 sliceX = normalize( vec3( viewDir.z, 0.0, -viewDir.x ) );
	vec3 sliceY = cross( viewDir, sliceX );
	float sliceCount = ssaoSampleCount >= 24.0 ? 2.0 : 1.0;
	float stepCount = clamp( floor( ssaoSampleCount / ( 2.0 * sliceCount ) ), 2.0, 8.0 );
	float stepOffset = fract( ( mod( patternIndex * 7.0, 16.0 ) + 0.5 ) / 16.0 + aoInfo.y * 0.618034 );
	float falloff = 1.0 / ( ssaoRadius * 0.2 );
	float visibility = 0.0;
	for ( int slice = 0; slice < 2; ++slice ) {
		if ( float( slice ) >= sliceCount ) {
			break;
		}
		float phi = ( ( patternIndex + 0.5 + aoInfo.y ) / 16.0 + float( slice ) ) / sliceCount * kPi;
		vec3 ortho = cos( phi ) * sliceX + sin( phi ) * sliceY;
		vec2 direction = normalize( ( ProjectViewPosition( centerPos + ortho * ( 0.01 * -centerPos.z ) ) - uv ) / invTexSize );
		vec3 axis = cross( ortho, viewDir );
		vec3 projected = normal - axis * dot( normal, axis );
		float projectedLength = length( projected );
		float cosN = clamp( dot( projected, viewDir ) / max( projectedLength, 0.00001 ), -1.0, 1.0 );
		float n = ( dot( projected, ortho ) >= 0.0 ? 1.0 : -1.0 ) * FastAcos( cosN );
		float lowPositive = cos( n + kHalfPi );
		float lowNegative = cos( n - kHalfPi );
		float horizonPositive = lowPositive;
		float horizonNegative = lowNegative;
		for ( int i = 0; i < 8; ++i ) {
			if ( float( i ) >= stepCount ) {
				break;
			}
			float t = ( float( i ) + stepOffset ) / stepCount;
			// Quadratic spacing: dense near the pixel, where occluders matter most;
			// the first step clears the pixel itself. Whole-pixel offsets keep each
			// reconstructed position on the texel its depth came from.
			vec2 offset = floor( direction * ( t * t * radiusPixels + 1.0 ) + 0.5 ) * invTexSize;
			for ( int side = 0; side < 2; ++side ) {
				vec2 sampleUv = side == 0 ? uv + offset : uv - offset;
				// Off the view there is nothing to see; a clamped edge texel would
				// stand in as a false wall along the screen's border.
				if ( sampleUv.x < 0.0 || sampleUv.y < 0.0 || sampleUv.x > 1.0 || sampleUv.y > 1.0 ) {
					continue;
				}
				float sampleDepth = SampleDepth( sampleUv );
				if ( sampleDepth >= 0.99999 ) {
					continue;
				}
				vec3 delta = ReconstructViewPosition( sampleUv, sampleDepth ) - centerPos;
				float distanceToSample = length( delta );
				float cosH = dot( delta, viewDir ) / max( distanceToSample, 0.0001 );
				float weight = clamp( ( ssaoRadius - distanceToSample ) * falloff, 0.0, 1.0 );
				if ( side == 0 ) {
					horizonPositive = max( horizonPositive, mix( lowPositive, cosH, weight ) );
				} else {
					horizonNegative = max( horizonNegative, mix( lowNegative, cosH, weight ) );
				}
			}
		}
		float h1 = n + clamp( FastAcos( horizonPositive ) - n, -kHalfPi, kHalfPi );
		float h0 = n + clamp( -FastAcos( horizonNegative ) - n, -kHalfPi, kHalfPi );
		float cn = cos( n );
		float sn = sin( n );
		visibility += projectedLength * ( IntegrateArc( h0, n, cn, sn ) + IntegrateArc( h1, n, cn, sn ) );
	}
	// One slice of an open surface the view meets at an angle can exceed 1;
	// only the average over slice directions is 1. Keep it for the filter to
	// average and clamp the resolved value (PixelOcclusion), or the clamp would
	// darken every tilted surface.
	return clamp( visibility / sliceCount, 0.0, 2.0 );
}

// The brute-force reference (developer comparison only, many times GTAO's
// cost): 256 cosine-weighted rays per pixel marched in 24 steps out to the
// radius through the depth buffer. A ray is occluded where it passes behind a
// visible surface within the radius of the pixel, every such surface taken as
// solid behind it. That is the scene GTAO's horizons describe, sampled directly
// instead of through horizons; a surface farther away, such as a cable well in
// front of a wall, occludes neither.
float ReferenceOcclusion( vec3 centerPos, vec3 normal, float patternIndex ) {
	vec3 helper = abs( normal.z ) < 0.999 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 );
	vec3 tangent = normalize( cross( normal, helper ) );
	vec3 bitangent = cross( normal, tangent );
	float rotation = ( patternIndex + 0.5 ) / 16.0;
	float visible = 0.0;
	for ( int ray = 0; ray < 256; ++ray ) {
		float u = ( float( ray ) + 0.5 ) / 256.0;
		float phi = fract( float( ray ) * 0.618033989 + rotation ) * 2.0 * kPi;
		float r = sqrt( u );
		vec3 direction = tangent * ( r * cos( phi ) ) + bitangent * ( r * sin( phi ) ) + normal * sqrt( max( 0.0, 1.0 - u ) );
		float hit = 0.0;
		for ( int i = 0; i < 24; ++i ) {
			vec3 position = centerPos + direction * ( ssaoRadius * ( float( i ) + 0.5 ) / 24.0 );
			vec2 sampleUv = ProjectViewPosition( position );
			if ( sampleUv.x < 0.0 || sampleUv.y < 0.0 || sampleUv.x > 1.0 || sampleUv.y > 1.0 ) {
				break;
			}
			float sampleDepth = SampleDepth( sampleUv );
			if ( sampleDepth >= 0.99999 ) {
				continue;
			}
			float surfaceDepth = -ViewSpaceZFromDepth( sampleDepth );
			if ( -position.z > surfaceDepth * 1.002 + 0.05
					&& length( ReconstructViewPosition( sampleUv, sampleDepth ) - centerPos ) < ssaoRadius ) {
				hit = 1.0;
				break;
			}
		}
		visible += 1.0 - hit;
	}
	return visible / 256.0;
}

// Classic surfaces bounce the light their occluders trap: GTAO's multi-bounce
// fit (PBRMath.h PBRMultiBounceAO) with one grey albedo. Native PBR applies it
// per channel with its own albedo, and specular occlusion, to the field.
float ClassicOcclusionFactor( float ao ) {
	if ( aoInfo.z <= 0.0 ) {
		return ao;
	}
	float v = clamp( ao, 0.0, 1.0 );
	float x = aoInfo.z;
	float bounced = ( ( v * ( 2.0404 * x - 0.3324 ) + ( 0.6417 - 4.7951 * x ) ) * v + ( 2.7552 * x + 0.6903 ) ) * v;
	return clamp( bounced, v, 1.0 );
}

// r: AO, gb: the view depth's slope (zero where the distance fade leaves no
// AO). The pattern index rotates the golden-angle spiral by one sixteenth of a
// turn per index and moves its radii by a permuted sixteenth, so the sixteen
// patterns a resolve averages cover the disk evenly. The normal is
// reconstructed only once the distance fade has kept the pixel.
vec3 ComputeAmbientOcclusion( vec2 uv, vec3 centerPos, float patternIndex ) {
	vec2 slope = vec2( 0.0 );
	vec3 centerNormal = vec3( 0.0 );
	// r_vkSSAODeferredNormal 0: the reference order, the normal before the fade.
	if ( block.modeInfo.w < 0.5 ) centerNormal = ReconstructNormal( uv, centerPos, slope );
	float viewDepth = -centerPos.z;
	float fade = 1.0 - smoothstep( ssaoMaxDistance * 0.5, ssaoMaxDistance, viewDepth );
	if ( fade <= 0.0 ) {
		return vec3( 1.0, 0.0, 0.0 );
	}
	if ( block.modeInfo.w > 0.5 ) centerNormal = ReconstructNormal( uv, centerPos, slope );

	float radiusPixels = clamp( ssaoRadius * projectionScale / max( viewDepth, 1.0 ), 2.0, 96.0 );

	if ( aoInfo.x > 0.5 ) {
		float visibility = aoInfo.x > 2.5 ? ReferenceOcclusion( centerPos, centerNormal, patternIndex )
			: aoInfo.x > 1.5 ? GroundTruthOcclusion( uv, centerPos, centerNormal, radiusPixels, patternIndex )
			: HorizonBasedOcclusion( uv, centerPos, centerNormal, radiusPixels, patternIndex );
		return vec3( mix( 1.0, visibility, fade ), slope );
	}
	float rotation = ( patternIndex + 0.5 ) * ( kTwoPi / 16.0 );
	float radialOffset = ( mod( patternIndex * 7.0, 16.0 ) + 0.5 ) / 16.0;
	float invSampleCount = 1.0 / max( ssaoSampleCount, 1.0 );
	float radiusSq = ssaoRadius * ssaoRadius;
	float accumulatedOcclusion = 0.0;
	float accumulatedWeight = 0.0;

	for ( int i = 0; i < kMaxSamples; ++i ) {
		if ( float( i ) >= ssaoSampleCount ) {
			break;
		}

		float angle = float( i ) * kGoldenAngle + rotation;
		float spiralRadius = sqrt( ( float( i ) + radialOffset ) * invSampleCount );
		vec2 sampleOffset = vec2( cos( angle ), sin( angle ) ) * spiralRadius * radiusPixels;
		vec2 sampleUv = uv + sampleOffset * invTexSize;
		float sampleDepth = SampleDepth( sampleUv );

		if ( sampleDepth >= 0.99999 ) {
			continue;
		}

		vec3 samplePos = ReconstructViewPosition( sampleUv, sampleDepth );
		vec3 toSample = samplePos - centerPos;
		float distanceSq = dot( toSample, toSample );

		if ( distanceSq <= 0.0001 || distanceSq > radiusSq ) {
			continue;
		}

		float distanceToSample = sqrt( distanceSq );
		float horizon = dot( centerNormal, toSample ) - ssaoBias;
		float angularWeight = clamp( horizon / distanceToSample, 0.0, 1.0 );
		float rangeWeight = 1.0 - smoothstep( ssaoRadius * 0.35, ssaoRadius, distanceToSample );
		float depthWeight = 1.0 - smoothstep( ssaoRadius * 0.5, ssaoRadius * 1.5, abs( samplePos.z - centerPos.z ) );
		float sampleWeight = rangeWeight * depthWeight;

		accumulatedOcclusion += angularWeight * sampleWeight;
		accumulatedWeight += sampleWeight;
	}

	float obscurance = ( accumulatedWeight > 0.0 ) ? ( accumulatedOcclusion / accumulatedWeight ) : 0.0;
	float ao = 1.0 - obscurance * ssaoIntensity;
	ao = clamp( pow( clamp( ao, 0.0, 1.0 ), max( ssaoPower, 0.001 ) ), 0.0, 1.0 );
	return vec3( mix( 1.0, ao, fade ), slope );
}

bool PixelIsForeground( vec2 uv, float sourceDepth ) {
	float finalDepth = SampleFinalDepth( uv );
	if ( finalDepth >= 0.99999 || sourceDepth >= 0.99999 ) {
		return false;
	}

	float sourceViewDepth = -ViewSpaceZFromDepth( sourceDepth );
	float finalViewDepth = -ViewSpaceZFromDepth( finalDepth );
	return finalViewDepth + 1.0 < sourceViewDepth;
}

// View pixels per occlusion texel: 1 at full resolution, 2 at half, where the
// target is the view's size rounded up to even and halved.
int OcclusionScale() {
	return int( floor( block.halfInfo.z / block.halfInfo.x + 0.5 ) );
}

// The view pixel whose AO an occlusion texel holds.
ivec2 OcclusionPixel( ivec2 texel, int scale ) {
	return min( texel * scale, ivec2( block.halfInfo.zw ) - 1 );
}

// Mode 3, drawn over the occlusion target.
void OcclusionMain( vec2 uv ) {
	ivec2 texel = min( ivec2( uv * block.halfInfo.xy ), ivec2( block.halfInfo.xy ) - 1 );
	ivec2 fullPixel = OcclusionPixel( texel, OcclusionScale() );
	vec2 fullUv = ( vec2( fullPixel ) + 0.5 ) * invTexSize;
	float depth = SampleDepth( fullUv );
	if ( depth >= 0.99999 || PixelIsForeground( fullUv, depth ) ) {
		outColor = vec4( 1.0, -1.0, 0.0, 0.0 );
		return;
	}
	vec3 centerPos = ReconstructViewPosition( fullUv, depth );
	vec3 occlusion = ComputeAmbientOcclusion( fullUv, centerPos, PatternIndex( vec2( texel ) ) );
	outColor = vec4( occlusion.x, -centerPos.z, occlusion.yz );
}

// Mode 5, drawn over a second target of the occlusion target's size: one axis
// (blurAxis) of a tent over five occlusion texels, weights 1/2, 1, 1, 1, 1/2.
// The sixteen sample patterns repeat every four texels, so the two halves
// weigh one pattern together and each pattern counts exactly once along the
// axis; the horizontal and then the vertical pass cancel the pattern. Each tap
// also counts by how well the plane it sampled (its depth and slope) predicts
// this texel's depth, within 3% of the depth or one unit, so occlusion keeps
// to its surface across silhouettes and grazing floors alike. The texel keeps
// its own depth and slope; one that takes no AO passes through.
void FilterMain( vec2 uv ) {
	ivec2 texel = min( ivec2( uv * block.halfInfo.xy ), ivec2( block.halfInfo.xy ) - 1 );
	vec4 centreTap = texelFetch( AOField, texel, 0 );
	if ( centreTap.g < 0.0 ) {
		outColor = centreTap;
		return;
	}
	int scale = OcclusionScale();
	ivec2 pixel = OcclusionPixel( texel, scale );
	ivec2 last = ivec2( block.halfInfo.xy ) - 1;
	ivec2 axis = ivec2( blurAxis );
	float tolerance = max( centreTap.g * 0.03, 1.0 );
	float weighted = centreTap.r;
	float weight = 1.0;
	for ( int i = -2; i <= 2; ++i ) {
		if ( i == 0 ) {
			continue;
		}
		ivec2 position = clamp( texel + axis * i, ivec2( 0 ), last );
		vec4 tap = texelFetch( AOField, position, 0 );
		if ( tap.g < 0.0 ) {
			continue;
		}
		float predicted = tap.g + dot( tap.ba, vec2( pixel - OcclusionPixel( position, scale ) ) );
		float w = ( abs( i ) > 1 ? 0.5 : 1.0 )
			* max( 0.0, 1.0 - abs( predicted - centreTap.g ) / tolerance );
		weighted += tap.r * w;
		weight += w;
	}
	outColor = vec4( weighted / weight, centreTap.gba );
}

// The AO of a view pixel from the filtered occlusion target: at full
// resolution its own texel; at half resolution the texels of the even pixels
// around it, bilinearly (one, two or four), so the occlusion slides with every
// view pixel instead of stepping with each texel. A texel counts by how well
// its plane predicts this pixel's depth; with none on this pixel's surface the
// closest in depth stands in.
float ResolvedOcclusion( vec2 uv, float viewDepth ) {
	ivec2 pixel = ivec2( uv * block.halfInfo.zw );
	int scale = OcclusionScale();
	ivec2 base = pixel / scale;
	vec2 fraction = vec2( pixel - base * scale ) / float( scale );
	ivec2 last = ivec2( block.halfInfo.xy ) - 1;
	float tolerance = max( viewDepth * 0.03, 1.0 );
	float weighted = 0.0;
	float weight = 0.0;
	float closestAo = 1.0;
	float closestDelta = 1e30;
	for ( int y = 0; y < 2; ++y ) {
		for ( int x = 0; x < 2; ++x ) {
			vec2 corner = vec2( float( x ), float( y ) );
			vec2 bilinear = mix( 1.0 - fraction, fraction, corner );
			if ( bilinear.x * bilinear.y <= 0.0 ) {
				continue;
			}
			ivec2 texel = min( base + ivec2( x, y ), last );
			vec4 tap = texelFetch( AOField, texel, 0 );
			if ( tap.g < 0.0 ) {
				continue;
			}
			float predicted = tap.g + dot( tap.ba, vec2( pixel - OcclusionPixel( texel, scale ) ) );
			float delta = abs( predicted - viewDepth );
			float w = bilinear.x * bilinear.y * max( 0.0, 1.0 - delta / tolerance );
			weighted += tap.r * w;
			weight += w;
			if ( delta < closestDelta ) {
				closestDelta = delta;
				closestAo = tap.r;
			}
		}
	}
	return weight > 0.0 ? weighted / weight : closestAo;
}

// The resolved AO of a world pixel at view depth viewDepth; none past the
// fade distance.
float PixelOcclusion( vec2 uv, float depth ) {
	float viewDepth = -ViewSpaceZFromDepth( depth );
	if ( viewDepth >= ssaoMaxDistance ) {
		return 1.0;
	}
	return min( ResolvedOcclusion( uv, viewDepth ), 1.0 );
}

// Mode 1. The field reads the occlusion target exactly as the classic resolve
// does, so a classic pixel darkens the same with and without native PBR.
void FieldMain( vec2 uv ) {
	float depth = SampleDepth( uv );
	if ( depth >= 0.99999 ) {
		// No world depth (sky): untouched, as the classic pass leaves it.
		outColor = vec4( 1.0, 1.0, 0.0, 1.0 );
		return;
	}
	if ( PixelIsForeground( uv, depth ) ) {
		// A model in front of the world: untouched.
		outColor = vec4( 1.0 );
		return;
	}
	float ao = PixelOcclusion( uv, depth );
	// A classic world surface is the visible one where the frame did not
	// settle in front of the classic-only snapshot.
	float classicDepth = texture( ClassicDepthBuffer, uv ).x;
	bool classic = classicDepth < 0.99999 && !PixelIsForeground( uv, classicDepth );
	outColor = vec4( ao, classic ? ClassicOcclusionFactor( ao ) : 1.0, 1.0, 1.0 );
}

// Mode 2. The debug view shows the field's AO, white where no AO applies, as
// the classic resolve's debug view does.
void ApplyMain( vec2 uv ) {
	ivec2 texel = ivec2( gl_FragCoord.xy );
	if ( ssaoFieldFlip > 0.5 ) {
		texel.y = textureSize( AOField, 0 ).y - 1 - texel.y;
	}
	vec3 field = texelFetch( AOField, texel, 0 ).rgb;
	if ( ssaoDebugView > 0.5 ) {
		outColor = vec4( vec3( field.b > 0.5 ? field.r : 1.0 ), 1.0 );
		return;
	}
	vec4 scene = ssaoMultiply > 0.5 ? vec4( 1.0 ) : texture( Scene, uv );
	outColor = vec4( scene.rgb * field.g, scene.a );
}

// Mode 4. Multiplied into the scene, or with the debug view, the pass writes
// the factor (white where no AO applies); the validation reference darkens a
// copy of the scene instead.
void ResolveMain( vec2 uv ) {
	float depth = SampleDepth( uv );
	float ao = 1.0;
	if ( depth < 0.99999 && !PixelIsForeground( uv, depth ) ) {
		ao = PixelOcclusion( uv, depth );
	}
	if ( ssaoDebugView > 0.5 ) {
		outColor = vec4( ao, ao, ao, 1.0 );
		return;
	}
	ao = ClassicOcclusionFactor( ao );
	if ( ssaoMultiply > 0.5 ) {
		outColor = vec4( ao, ao, ao, 1.0 );
		return;
	}
	vec4 scene = texture( Scene, uv );
	outColor = vec4( scene.rgb * ao, scene.a );
}

void main() {
	vec2 uv = fragUV;
	if ( ssaoMode > 4.5 ) {
		FilterMain( uv );
		return;
	}
	if ( ssaoMode > 3.5 ) {
		ResolveMain( uv );
		return;
	}
	if ( ssaoMode > 2.5 ) {
		OcclusionMain( uv );
		return;
	}
	if ( ssaoMode > 1.5 ) {
		ApplyMain( uv );
		return;
	}
	if ( ssaoMode > 0.5 ) {
		FieldMain( uv );
		return;
	}
	vec4 scene = ssaoMultiply > 0.5 ? vec4( 1.0 ) : texture( Scene, uv );
	float depth = SampleDepth( uv );

	if ( depth >= 0.99999 ) {
		outColor = ( ssaoDebugView > 0.5 ) ? vec4( 1.0 ) : scene;
		return;
	}

	if ( PixelIsForeground( uv, depth ) ) {
		outColor = ( ssaoDebugView > 0.5 ) ? vec4( 1.0 ) : scene;
		return;
	}

	vec3 centerPos = ReconstructViewPosition( uv, depth );
	float ao = min( ComputeAmbientOcclusion( uv, centerPos, PatternIndex( floor( uv / invTexSize ) ) ).x, 1.0 );

	if ( ssaoDebugView > 0.5 ) {
		outColor = vec4( vec3( ao ), 1.0 );
		return;
	}

	outColor = vec4( scene.rgb * ao, scene.a );
}
