uniform sampler2D Scene;
uniform sampler2D DepthBuffer;
uniform sampler2D FinalDepthBuffer;
// Native PBR views (draw_common.cpp RB_PrepareSSAOIndirectField): the world
// depth without native PBR surfaces, and the field this pass wrote before
// lighting. The half-resolution upsample (mode 4) reads its half-size AO here.
uniform sampler2D ClassicDepthBuffer;
uniform sampler2D AOField;
// 0 classic: darken the lit scene. 1 field: before lighting, write the
// occlusion native PBR applies to its indirect light (r) and the factor for
// classic world pixels (g); b marks pixels with world depth. 2 apply: darken
// only the classic world pixels with the field's factor. 3 half: the classic
// AO of the even pixel of each 2x2 block into a half-resolution target (r: AO,
// g: view depth, negative where the pixel takes none). 4 upsample: darken the
// lit scene with the half-resolution AO; AOField is that target.
uniform float ssaoMode;
// 1: the pass is blended as a multiply into the scene target, so it writes the
// factor itself and never reads a copy of the scene (draw_common.cpp RB_STD_SSAO).
uniform float ssaoMultiply;
uniform vec2 fieldInvSize;
// xy: half-resolution target size, zw: view size in pixels (modes 3 and 4)
uniform vec4 halfInfo;
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

vec3 ReconstructNormal( vec2 uv, vec3 centerPos ) {
	vec2 offsetX = vec2( invTexSize.x, 0.0 );
	vec2 offsetY = vec2( 0.0, invTexSize.y );

	vec3 leftPos = ReconstructViewPosition( uv - offsetX, SampleDepth( uv - offsetX ) );
	vec3 rightPos = ReconstructViewPosition( uv + offsetX, SampleDepth( uv + offsetX ) );
	vec3 downPos = ReconstructViewPosition( uv - offsetY, SampleDepth( uv - offsetY ) );
	vec3 upPos = ReconstructViewPosition( uv + offsetY, SampleDepth( uv + offsetY ) );

	vec3 tangent = PickBestDerivative( centerPos, leftPos, rightPos );
	vec3 bitangent = PickBestDerivative( centerPos, downPos, upPos );
	vec3 normal = normalize( cross( tangent, bitangent ) );

	if ( dot( normal, centerPos ) > 0.0 ) {
		normal = -normal;
	}

	return normal;
}

float InterleavedGradientNoise( vec2 pixelPos ) {
	return fract( 52.9829189 * fract( dot( pixelPos, vec2( 0.06711056, 0.00583715 ) ) ) );
}

// The normal is reconstructed only once the distance fade has kept the pixel:
// a faded-out pixel returns before it, with the value it always had.
float ComputeAmbientOcclusion( vec2 uv, vec3 centerPos ) {
	float viewDepth = -centerPos.z;
	float fade = 1.0 - smoothstep( ssaoMaxDistance * 0.5, ssaoMaxDistance, viewDepth );
	if ( fade <= 0.0 ) {
		return 1.0;
	}
	vec3 centerNormal = ReconstructNormal( uv, centerPos );

	float radiusPixels = clamp( ssaoRadius * projectionScale / max( viewDepth, 1.0 ), 2.0, 96.0 );
	float rotation = InterleavedGradientNoise( uv / invTexSize ) * kTwoPi;
	float radiusSq = ssaoRadius * ssaoRadius;
	float accumulatedOcclusion = 0.0;
	float accumulatedWeight = 0.0;

	for ( int i = 0; i < kMaxSamples; ++i ) {
		if ( float( i ) >= ssaoSampleCount ) {
			break;
		}

		float sampleIndex = float( i ) + 0.5;
		float sampleFraction = sampleIndex / max( ssaoSampleCount, 1.0 );
		float angle = sampleIndex * kGoldenAngle + rotation;
		float spiralRadius = sqrt( sampleFraction );
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
	return mix( 1.0, ao, fade );
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
	vec3 centerPos = ReconstructViewPosition( uv, depth );
	float ao = ComputeAmbientOcclusion( uv, centerPos );
	// A classic world surface is the visible one where the frame did not
	// settle in front of the classic-only snapshot.
	float classicDepth = texture2D( ClassicDepthBuffer, uv ).x;
	bool classic = classicDepth < 0.99999 && !PixelIsForeground( uv, classicDepth );
	gl_FragColor = vec4( ao, classic ? ao : 1.0, 1.0, 1.0 );
}

void ApplyMain( vec2 uv ) {
	vec4 scene = texture2D( Scene, uv );
	vec3 field = texture2D( AOField, gl_FragCoord.xy * fieldInvSize ).rgb;
	if ( ssaoDebugView > 0.5 ) {
		gl_FragColor = field.b > 0.5 ? vec4( vec3( field.r ), scene.a ) : scene;
		return;
	}
	gl_FragColor = vec4( scene.rgb * field.g, scene.a );
}

// Mode 3, drawn over the half-size target with unit texture coordinates. Each
// texel takes the AO of the even pixel of its 2x2 block of the view, computed
// exactly as the full-resolution pass computes it there, so those pixels match
// it after the upsample. Vulkan's post_ssao.frag does the same.
void HalfMain( vec2 uv ) {
	vec2 halfTexel = min( floor( uv * halfInfo.xy ), halfInfo.xy - 1.0 );
	vec2 fullPixel = min( halfTexel * 2.0, halfInfo.zw - 1.0 );
	vec2 fullUv = ( fullPixel + 0.5 ) * invTexSize;
	float depth = SampleDepth( fullUv );
	if ( depth >= 0.99999 || PixelIsForeground( fullUv, depth ) ) {
		gl_FragColor = vec4( 1.0, -1.0, 0.0, 1.0 );
		return;
	}
	vec3 centerPos = ReconstructViewPosition( fullUv, depth );
	gl_FragColor = vec4( ComputeAmbientOcclusion( fullUv, centerPos ), -centerPos.z, 0.0, 1.0 );
}

// Mode 4. A pixel's half-resolution neighbours are its own block's texel and,
// on odd rows or columns, the next block's: one, two or four equally weighted
// taps, read at texel centres of the nearest-filtered target. A tap counts only
// near this pixel's depth, so AO does not bleed across silhouettes; with none
// near, the closest in depth stands in.
float UpsampledOcclusion( vec2 uv, float viewDepth ) {
	vec2 pixel = floor( uv * halfInfo.zw );
	vec2 base = floor( pixel * 0.5 );
	vec2 odd = pixel - base * 2.0;
	vec2 last = halfInfo.xy - 1.0;
	float tolerance = max( viewDepth * 0.04, 2.0 );
	float weighted = 0.0;
	float weight = 0.0;
	float closestAo = 1.0;
	float closestDelta = 1e30;
	for ( int y = 0; y < 2; ++y ) {
		if ( float( y ) > odd.y ) {
			break;
		}
		for ( int x = 0; x < 2; ++x ) {
			if ( float( x ) > odd.x ) {
				break;
			}
			vec2 texel = min( base + vec2( float( x ), float( y ) ), last );
			vec2 tap = texture2D( AOField, ( texel + 0.5 ) / halfInfo.xy ).rg;
			if ( tap.g < 0.0 ) {
				continue;
			}
			float delta = abs( tap.g - viewDepth );
			float w = max( 0.0, 1.0 - delta / tolerance );
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

void UpsampleMain( vec2 uv ) {
	float depth = SampleDepth( uv );
	if ( depth >= 0.99999 || PixelIsForeground( uv, depth ) ) {
		gl_FragColor = vec4( 1.0 );
		return;
	}
	float viewDepth = -ViewSpaceZFromDepth( depth );
	if ( viewDepth >= ssaoMaxDistance ) {
		gl_FragColor = vec4( 1.0 );
		return;
	}
	float ao = UpsampledOcclusion( uv, viewDepth );
	gl_FragColor = vec4( ao, ao, ao, 1.0 );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	if ( ssaoMode > 3.5 ) {
		UpsampleMain( uv );
		return;
	}
	if ( ssaoMode > 2.5 ) {
		HalfMain( uv );
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
		gl_FragColor = scene;
		return;
	}

	if ( PixelIsForeground( uv, depth ) ) {
		gl_FragColor = ( ssaoDebugView > 0.5 ) ? vec4( 1.0, 1.0, 1.0, scene.a ) : scene;
		return;
	}

	vec3 centerPos = ReconstructViewPosition( uv, depth );
	float ao = ComputeAmbientOcclusion( uv, centerPos );

	if ( ssaoDebugView > 0.5 ) {
		gl_FragColor = vec4( vec3( ao ), scene.a );
		return;
	}

	gl_FragColor = vec4( scene.rgb * ao, scene.a );
}
