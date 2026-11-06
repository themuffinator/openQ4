uniform sampler2D Scene;
uniform sampler2D DepthBuffer;
uniform sampler2D VelocityBuffer;
uniform vec2 invTexSize;
uniform vec2 viewportSize;
uniform vec4 currentReconstructInfo;
uniform vec4 previousProjectInfo;
uniform vec2 depthProjection;
uniform vec4 currentViewOrigin;
uniform vec4 currentViewAxis0;
uniform vec4 currentViewAxis1;
uniform vec4 currentViewAxis2;
uniform vec4 previousViewOrigin;
uniform vec4 previousViewAxis0;
uniform vec4 previousViewAxis1;
uniform vec4 previousViewAxis2;
uniform vec4 motionBlurParams;
uniform vec4 motionBlurObjectParams;

const int kMaxSamples = 16;
const float kFarFadeStart = 2048.0;
const float kFarFadeEnd = 8192.0;
const float kFarDepthFadeStart = 0.9985;
const float kFarDepthFadeEnd = 0.9998;

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

vec3 ReconstructCurrentViewPosition( vec2 uv, float depth ) {
	float viewZ = ViewSpaceZFromDepth( depth );
	vec2 ndc = uv * 2.0 - 1.0;

	return vec3(
		-viewZ * ( ndc.x + currentReconstructInfo.z ) * currentReconstructInfo.x,
		-viewZ * ( ndc.y + currentReconstructInfo.w ) * currentReconstructInfo.y,
		viewZ );
}

vec3 CurrentViewToWorld( vec3 viewPos ) {
	return currentViewOrigin.xyz
		+ currentViewAxis0.xyz * ( -viewPos.z )
		+ currentViewAxis1.xyz * ( -viewPos.x )
		+ currentViewAxis2.xyz * viewPos.y;
}

vec3 WorldToPreviousView( vec3 worldPos ) {
	vec3 delta = worldPos - previousViewOrigin.xyz;
	float forward = dot( delta, previousViewAxis0.xyz );
	float left = dot( delta, previousViewAxis1.xyz );
	float up = dot( delta, previousViewAxis2.xyz );
	return vec3( -left, up, -forward );
}

vec2 ProjectPreviousViewToUV( vec3 previousViewPos ) {
	float w = -previousViewPos.z;
	if ( abs( w ) < 0.00001 ) {
		return vec2( -1000.0 );
	}

	vec2 clipXY = previousProjectInfo.xy * previousViewPos.xy + previousProjectInfo.zw * previousViewPos.z;
	vec2 ndc = clipXY / w;
	return ndc * 0.5 + 0.5;
}

vec3 MotionDebugColor( vec2 velocityPixels, float speedPixels ) {
	float maxPixels = max( motionBlurParams.y, 1.0 );
	float magnitude = clamp( speedPixels / maxPixels, 0.0, 1.0 );
	if ( magnitude <= 0.001 ) {
		return vec3( 0.0 );
	}

	vec2 direction = clamp( velocityPixels / maxPixels, vec2( -1.0 ), vec2( 1.0 ) );
	return vec3( direction * 0.5 + 0.5, 1.0 ) * magnitude;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );
	float centerDepth = SampleDepth( uv );
	bool debugView = motionBlurParams.w > 0.5;

	if ( centerDepth >= 0.99999 ) {
		gl_FragColor = debugView ? vec4( 0.0, 0.0, 0.0, scene.a ) : scene;
		return;
	}

	float centerViewZ = ViewSpaceZFromDepth( centerDepth );
	float centerViewDepth = max( -centerViewZ, 1.0 );
	vec3 currentViewPos = ReconstructCurrentViewPosition( uv, centerDepth );
	vec3 worldPos = CurrentViewToWorld( currentViewPos );
	vec3 previousViewPos = WorldToPreviousView( worldPos );
	vec2 previousUV = ProjectPreviousViewToUV( previousViewPos );
	vec2 cameraVelocityPixels = ( uv - previousUV ) * viewportSize;
	vec4 objectVelocity = texture2D( VelocityBuffer, uv );
	bool objectVectorValid = motionBlurObjectParams.x > 0.5 && objectVelocity.a > 0.5;
	bool cameraVectorValid = motionBlurObjectParams.y > 0.5;
	vec2 velocityPixels = objectVectorValid ? objectVelocity.xy : ( cameraVectorValid ? cameraVelocityPixels : vec2( 0.0 ) );
	// z: shutter / smoothed frame interval, so the blur length follows the
	// camera's speed rather than the frame rate (1 = per-frame, as before).
	velocityPixels *= ( motionBlurObjectParams.z > 0.0 ) ? motionBlurObjectParams.z : 1.0;
	float speedPixels = length( velocityPixels );

	if ( debugView ) {
		gl_FragColor = vec4( MotionDebugColor( velocityPixels, speedPixels ), scene.a );
		return;
	}

	if ( speedPixels < 0.5 ) {
		gl_FragColor = scene;
		return;
	}

	// Very distant depth values are quantized heavily by the infinite projection.
	// Fade them out so sky-adjacent silhouettes do not swim after camera stops.
	float farViewFade = 1.0 - smoothstep( kFarFadeStart, kFarFadeEnd, centerViewDepth );
	float farDepthFade = 1.0 - smoothstep( kFarDepthFadeStart, kFarDepthFadeEnd, centerDepth );
	float farFade = clamp( min( farViewFade, farDepthFade ), 0.0, 1.0 );

	float vectorFade = objectVectorValid ? 1.0 : farFade;
	float radiusPixels = clamp( speedPixels * motionBlurParams.x * vectorFade, 0.0, motionBlurParams.y );
	if ( radiusPixels < 0.5 ) {
		gl_FragColor = scene;
		return;
	}

	vec2 dirPixels = velocityPixels / max( speedPixels, 0.0001 );
	vec2 sampleDeltaUV = dirPixels * radiusPixels * invTexSize;
	float sampleCount = clamp( floor( motionBlurParams.z + 0.5 ), 1.0, float( kMaxSamples ) );
	float depthTolerance = clamp( centerViewDepth * 0.015, 4.0, 96.0 );

	vec4 sum = scene;
	float weightSum = 1.0;

	for ( int i = 1; i <= kMaxSamples; ++i ) {
		if ( float( i ) > sampleCount ) {
			break;
		}

		float t = float( i ) / sampleCount;
		vec2 sampleUV = uv - sampleDeltaUV * t;
		if ( sampleUV.x < 0.0 || sampleUV.y < 0.0 || sampleUV.x > 1.0 || sampleUV.y > 1.0 ) {
			continue;
		}

		float sampleDepth = SampleDepth( sampleUV );
		if ( sampleDepth >= 0.99999 ) {
			continue;
		}

		float sampleViewZ = ViewSpaceZFromDepth( sampleDepth );
		float depthWeight = 1.0 - smoothstep( depthTolerance, depthTolerance * 4.0, abs( sampleViewZ - centerViewZ ) );
		if ( depthWeight <= 0.001 ) {
			continue;
		}

		float sampleWeight = ( 1.0 - t * 0.45 ) * depthWeight;
		sum += texture2D( Scene, sampleUV ) * sampleWeight;
		weightSum += sampleWeight;
	}

	gl_FragColor = sum / max( weightSum, 0.0001 );
}
