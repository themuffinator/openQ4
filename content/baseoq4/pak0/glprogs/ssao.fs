uniform sampler2D Scene;
uniform sampler2D DepthBuffer;
uniform sampler2D FinalDepthBuffer;
// Native PBR views (draw_common.cpp RB_PrepareSSAOIndirectField): the world
// depth without native PBR surfaces. AOField is the occlusion target mode 3
// wrote (modes 1 and 4) or the field mode 1 wrote before lighting (mode 2).
uniform sampler2D ClassicDepthBuffer;
uniform sampler2D AOField;
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
uniform float ssaoMode;
// 1: the pass is blended as a multiply into the scene target, so it writes the
// factor itself and never reads a copy of the scene (draw_common.cpp RB_STD_SSAO).
uniform float ssaoMultiply;
uniform vec2 fieldInvSize;
// xy: occlusion target size, zw: view size in pixels (modes 1, 3, 4 and 5)
uniform vec4 halfInfo;
// The texel step of the filter pass (mode 5): (1, 0) or (0, 1).
uniform vec2 blurAxis;
uniform vec2 invTexSize;
uniform vec4 projectionInfo;
uniform vec2 depthProjection;
uniform float projectionScale;
uniform float ssaoRadius;
uniform float ssaoBias;
uniform float ssaoIntensity;
uniform float ssaoPower;
uniform float ssaoMaxDistance;
uniform float ssaoSampleCount;
uniform float ssaoDebugView;

const float kGoldenAngle = 2.39996323;
const float kTwoPi = 6.28318531;
const int kMaxSamples = 32;

float SampleDepth( vec2 uv ) {
	return texture2D( DepthBuffer, uv ).x;
}

float SampleFinalDepth( vec2 uv ) {
	return texture2D( FinalDepthBuffer, uv ).x;
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

// r: AO, gb: the view depth's slope (zero where the distance fade leaves no
// AO). The pattern index rotates the golden-angle spiral by one sixteenth of a
// turn per index and moves its radii by a permuted sixteenth, so the sixteen
// patterns a resolve averages cover the disk evenly. The normal is
// reconstructed only once the distance fade has kept the pixel.
vec3 ComputeAmbientOcclusion( vec2 uv, vec3 centerPos, float patternIndex ) {
	float viewDepth = -centerPos.z;
	float fade = 1.0 - smoothstep( ssaoMaxDistance * 0.5, ssaoMaxDistance, viewDepth );
	if ( fade <= 0.0 ) {
		return vec3( 1.0, 0.0, 0.0 );
	}
	vec2 slope;
	vec3 centerNormal = ReconstructNormal( uv, centerPos, slope );

	float radiusPixels = clamp( ssaoRadius * projectionScale / max( viewDepth, 1.0 ), 2.0, 96.0 );
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
float OcclusionScale() {
	return floor( halfInfo.z / halfInfo.x + 0.5 );
}

// The view pixel whose AO an occlusion texel holds.
vec2 OcclusionPixel( vec2 texel, float scale ) {
	return min( texel * scale, halfInfo.zw - 1.0 );
}

// Mode 3, drawn over the occlusion target with unit texture coordinates.
void OcclusionMain( vec2 uv ) {
	vec2 texel = min( floor( uv * halfInfo.xy ), halfInfo.xy - 1.0 );
	vec2 fullPixel = OcclusionPixel( texel, OcclusionScale() );
	vec2 fullUv = ( fullPixel + 0.5 ) * invTexSize;
	float depth = SampleDepth( fullUv );
	if ( depth >= 0.99999 || PixelIsForeground( fullUv, depth ) ) {
		gl_FragColor = vec4( 1.0, -1.0, 0.0, 0.0 );
		return;
	}
	vec3 centerPos = ReconstructViewPosition( fullUv, depth );
	vec3 occlusion = ComputeAmbientOcclusion( fullUv, centerPos, PatternIndex( texel ) );
	gl_FragColor = vec4( occlusion.x, -centerPos.z, occlusion.yz );
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
	vec2 texel = min( floor( uv * halfInfo.xy ), halfInfo.xy - 1.0 );
	vec4 centreTap = texture2D( AOField, ( texel + 0.5 ) / halfInfo.xy );
	if ( centreTap.g < 0.0 ) {
		gl_FragColor = centreTap;
		return;
	}
	float scale = OcclusionScale();
	vec2 pixel = OcclusionPixel( texel, scale );
	vec2 last = halfInfo.xy - 1.0;
	float tolerance = max( centreTap.g * 0.03, 1.0 );
	float weighted = centreTap.r;
	float weight = 1.0;
	for ( int i = -2; i <= 2; ++i ) {
		if ( i == 0 ) {
			continue;
		}
		vec2 position = clamp( texel + blurAxis * float( i ), vec2( 0.0 ), last );
		vec4 tap = texture2D( AOField, ( position + 0.5 ) / halfInfo.xy );
		if ( tap.g < 0.0 ) {
			continue;
		}
		float predicted = tap.g + dot( tap.ba, pixel - OcclusionPixel( position, scale ) );
		float w = ( abs( float( i ) ) > 1.5 ? 0.5 : 1.0 )
			* max( 0.0, 1.0 - abs( predicted - centreTap.g ) / tolerance );
		weighted += tap.r * w;
		weight += w;
	}
	gl_FragColor = vec4( weighted / weight, centreTap.gba );
}

// The AO of a view pixel from the filtered occlusion target: at full
// resolution its own texel; at half resolution the texels of the even pixels
// around it, bilinearly (one, two or four), so the occlusion slides with every
// view pixel instead of stepping with each texel. A texel counts by how well
// its plane predicts this pixel's depth; with none on this pixel's surface the
// closest in depth stands in.
float ResolvedOcclusion( vec2 uv, float viewDepth ) {
	vec2 pixel = floor( uv * halfInfo.zw );
	float scale = OcclusionScale();
	vec2 base = floor( pixel / scale );
	vec2 fraction = ( pixel - base * scale ) / scale;
	vec2 last = halfInfo.xy - 1.0;
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
			vec2 texel = min( base + corner, last );
			vec4 tap = texture2D( AOField, ( texel + 0.5 ) / halfInfo.xy );
			if ( tap.g < 0.0 ) {
				continue;
			}
			float predicted = tap.g + dot( tap.ba, pixel - OcclusionPixel( texel, scale ) );
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
	return ResolvedOcclusion( uv, viewDepth );
}

// Mode 1. The field reads the occlusion target exactly as the classic resolve
// does, so a classic pixel darkens the same with and without native PBR.
void FieldMain( vec2 uv ) {
	float depth = SampleDepth( uv );
	if ( depth >= 0.99999 ) {
		// No world depth (sky): untouched, as the classic pass leaves it.
		gl_FragColor = vec4( 1.0, 1.0, 0.0, 1.0 );
		return;
	}
	if ( PixelIsForeground( uv, depth ) ) {
		// A model in front of the world: untouched.
		gl_FragColor = vec4( 1.0 );
		return;
	}
	float ao = PixelOcclusion( uv, depth );
	// A classic world surface is the visible one where the frame did not
	// settle in front of the classic-only snapshot.
	float classicDepth = texture2D( ClassicDepthBuffer, uv ).x;
	bool classic = classicDepth < 0.99999 && !PixelIsForeground( uv, classicDepth );
	gl_FragColor = vec4( ao, classic ? ao : 1.0, 1.0, 1.0 );
}

// Mode 2. The debug view shows the field's AO, white where no AO applies, as
// the classic resolve's debug view does.
void ApplyMain( vec2 uv ) {
	vec3 field = texture2D( AOField, gl_FragCoord.xy * fieldInvSize ).rgb;
	if ( ssaoDebugView > 0.5 ) {
		gl_FragColor = vec4( vec3( field.b > 0.5 ? field.r : 1.0 ), 1.0 );
		return;
	}
	vec4 scene = texture2D( Scene, uv );
	gl_FragColor = vec4( scene.rgb * field.g, scene.a );
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
	if ( ssaoMultiply > 0.5 || ssaoDebugView > 0.5 ) {
		gl_FragColor = vec4( ao, ao, ao, 1.0 );
		return;
	}
	vec4 scene = texture2D( Scene, uv );
	gl_FragColor = vec4( scene.rgb * ao, scene.a );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
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
	vec4 scene = ssaoMultiply > 0.5 ? vec4( 1.0 ) : texture2D( Scene, uv );
	float depth = SampleDepth( uv );

	if ( depth >= 0.99999 ) {
		gl_FragColor = ( ssaoDebugView > 0.5 ) ? vec4( 1.0 ) : scene;
		return;
	}

	if ( PixelIsForeground( uv, depth ) ) {
		gl_FragColor = ( ssaoDebugView > 0.5 ) ? vec4( 1.0 ) : scene;
		return;
	}

	vec3 centerPos = ReconstructViewPosition( uv, depth );
	float ao = ComputeAmbientOcclusion( uv, centerPos, PatternIndex( floor( uv / invTexSize ) ) ).x;

	if ( ssaoDebugView > 0.5 ) {
		gl_FragColor = vec4( vec3( ao ), 1.0 );
		return;
	}

	gl_FragColor = vec4( scene.rgb * ao, scene.a );
}
