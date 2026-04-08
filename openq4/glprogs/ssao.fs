uniform sampler2D Scene;
uniform sampler2D DepthBuffer;
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

float ComputeAmbientOcclusion( vec2 uv, vec3 centerPos, vec3 centerNormal ) {
	float viewDepth = -centerPos.z;
	float fade = 1.0 - smoothstep( ssaoMaxDistance * 0.5, ssaoMaxDistance, viewDepth );
	if ( fade <= 0.0 ) {
		return 1.0;
	}

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

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );
	float depth = SampleDepth( uv );

	if ( depth >= 0.99999 ) {
		gl_FragColor = scene;
		return;
	}

	vec3 centerPos = ReconstructViewPosition( uv, depth );
	vec3 centerNormal = ReconstructNormal( uv, centerPos );
	float ao = ComputeAmbientOcclusion( uv, centerPos, centerNormal );

	if ( ssaoDebugView > 0.5 ) {
		gl_FragColor = vec4( vec3( ao ), scene.a );
		return;
	}

	gl_FragColor = vec4( scene.rgb * ao, scene.a );
}
