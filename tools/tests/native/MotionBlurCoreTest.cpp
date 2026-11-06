// Copyright (C) 2026 DarkMatter Productions
//
// MotionBlurCore.h: the camera-history policy, jitter cancellation and
// frame-rate normalisation shared by the OpenGL and Vulkan motion blur.

#include "src/renderer/MotionBlurCore.h"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;

static void Check( bool condition, const char *message ) {
	if ( condition ) {
		return;
	}
	std::fprintf( stderr, "MotionBlurCoreTest: %s\n", message );
	failures++;
}

static bool Near( float a, float b, float tolerance = 0.0001f ) {
	return std::fabs( a - b ) <= tolerance;
}

static motionBlurCameraSample_t Camera( int timeMsec ) {
	motionBlurCameraSample_t sample = {};
	sample.timeMsec = timeMsec;
	sample.axis[0][0] = 1.0f;
	sample.axis[1][1] = 1.0f;
	sample.axis[2][2] = 1.0f;
	sample.fov[0] = 90.0f;
	sample.fov[1] = 59.0f;
	sample.projectionNeutral[0] = 1.0f;
	sample.projectionNeutral[1] = 1.7f;
	sample.depthProjection[0] = -0.999f;
	sample.depthProjection[1] = -6.0f;
	return sample;
}

static motionBlurHistoryContext_t Context() {
	motionBlurHistoryContext_t context = {};
	context.historyValid = true;
	return context;
}

static void TestHistoryPolicy() {
	const motionBlurHistoryContext_t context = Context();
	motionBlurCameraSample_t previous = Camera( 1000 );
	motionBlurCameraSample_t current = Camera( 1007 );
	current.origin[0] = 4.0f;
	Check( MotionBlurCore_ValidateHistory( context, current, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"a moving camera one frame later must blend" );

	// The defect fixed for REN-08: temporal AA moved projectionMatrix[8]/[9]
	// by up to a pixel every frame, which the old test read as a projection
	// change, so motion blur never drew while TAA was active. The neutral
	// offsets carry no jitter, so jittered frames remain usable.
	motionBlurCameraSample_t jittered = current;
	jittered.projectionNeutral[2] = previous.projectionNeutral[2];
	Check( MotionBlurCore_ValidateHistory( context, jittered, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"jitter-free offsets must not read as a projection change" );
	motionBlurCameraSample_t zoomed = current;
	zoomed.projectionNeutral[0] = 1.2f;
	Check( MotionBlurCore_ValidateHistory( context, zoomed, previous ) == MOTION_BLUR_HISTORY_PROJECTION_CHANGED,
		"a real projection change must still reset" );
	motionBlurCameraSample_t offAxis = current;
	offAxis.projectionNeutral[3] = 0.05f;
	Check( MotionBlurCore_ValidateHistory( context, offAxis, previous ) == MOTION_BLUR_HISTORY_PROJECTION_CHANGED,
		"an off-axis frustum change must still reset" );
	motionBlurCameraSample_t fov = current;
	fov.fov[0] = 80.0f;
	Check( MotionBlurCore_ValidateHistory( context, fov, previous ) == MOTION_BLUR_HISTORY_PROJECTION_CHANGED,
		"a field-of-view change must reset" );

	// High-refresh presentation frames may share one millisecond.
	motionBlurCameraSample_t sameTime = current;
	sameTime.timeMsec = previous.timeMsec;
	Check( MotionBlurCore_ValidateHistory( context, sameTime, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"frames that share a millisecond must keep the history" );
	motionBlurCameraSample_t reversed = current;
	reversed.timeMsec = previous.timeMsec - 1;
	Check( MotionBlurCore_ValidateHistory( context, reversed, previous ) == MOTION_BLUR_HISTORY_TIME_REVERSED,
		"time running backwards must reset" );
	motionBlurCameraSample_t gap = current;
	gap.timeMsec = previous.timeMsec + MOTION_BLUR_MAX_TIME_GAP_MSEC + 1;
	Check( MotionBlurCore_ValidateHistory( context, gap, previous ) == MOTION_BLUR_HISTORY_TIME_GAP,
		"a slow frame must reset" );
	gap.timeMsec = previous.timeMsec + MOTION_BLUR_MAX_TIME_GAP_MSEC;
	Check( MotionBlurCore_ValidateHistory( context, gap, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"the time-gap limit is inclusive" );

	motionBlurCameraSample_t still = Camera( 1007 );
	Check( MotionBlurCore_ValidateHistory( context, still, previous ) == MOTION_BLUR_HISTORY_CAMERA_STILL,
		"a still camera without object vectors has nothing to blur" );
	motionBlurHistoryContext_t objectVectors = context;
	objectVectors.allowStillCamera = true;
	Check( MotionBlurCore_ValidateHistory( objectVectors, still, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"object vectors may blur moving entities under a still camera" );
	motionBlurCameraSample_t turned = still;
	turned.axis[0][1] = 0.01f;
	Check( MotionBlurCore_ValidateHistory( context, turned, previous ) == MOTION_BLUR_HISTORY_USABLE,
		"a turning camera must blend" );

	motionBlurCameraSample_t teleport = current;
	teleport.origin[0] = MOTION_BLUR_TELEPORT_DISTANCE + 1.0f;
	Check( MotionBlurCore_ValidateHistory( context, teleport, previous ) == MOTION_BLUR_HISTORY_CAMERA_TELEPORT,
		"a teleport must reset" );

	motionBlurHistoryContext_t cut = context;
	cut.explicitCut = true;
	Check( MotionBlurCore_ValidateHistory( cut, current, previous ) == MOTION_BLUR_HISTORY_EXPLICIT_CUT,
		"an authored camera cut must reset even inside the teleport limit" );
	motionBlurHistoryContext_t first = context;
	first.historyValid = false;
	Check( MotionBlurCore_ValidateHistory( first, current, previous ) == MOTION_BLUR_HISTORY_FIRST_SAMPLE,
		"no history means a first sample" );
	motionBlurHistoryContext_t restart = context;
	restart.videoRestarted = true;
	Check( MotionBlurCore_ValidateHistory( restart, current, previous ) == MOTION_BLUR_HISTORY_VIDEO_RESTART,
		"a video restart must reset" );
	motionBlurHistoryContext_t world = context;
	world.worldChanged = true;
	Check( MotionBlurCore_ValidateHistory( world, current, previous ) == MOTION_BLUR_HISTORY_WORLD_CHANGED,
		"a world change must reset" );
	motionBlurHistoryContext_t viewport = context;
	viewport.viewportChanged = true;
	Check( MotionBlurCore_ValidateHistory( viewport, current, previous ) == MOTION_BLUR_HISTORY_VIEWPORT_CHANGED,
		"a viewport change must reset" );

	// Dynamic resolution changes the scene extent without changing its shape.
	Check( !MotionBlurCore_ViewportChanged( 1872, 1048, 2560, 1440 ),
		"an aligned scene-scale change must keep the history" );
	Check( !MotionBlurCore_ViewportChanged( 1280, 720, 2560, 1440 ),
		"a half-scale scene must keep the history" );
	Check( MotionBlurCore_ViewportChanged( 1920, 1440, 2560, 1440 ),
		"a change of aspect ratio must reset" );
	Check( MotionBlurCore_ViewportChanged( 0, 720, 1280, 720 ),
		"an empty viewport must reset" );

	for ( int i = 0; i < MOTION_BLUR_HISTORY_RESULT_COUNT; i++ ) {
		const char *name = MotionBlurCore_HistoryResultName( static_cast<motionBlurHistoryResult_t>( i ) );
		Check( name != NULL && name[0] != '\0' && std::string( name ) != "unknown",
			"every history result needs a name" );
	}
}

// Project a view-space point with offsets (x, y) the way post_motionblur does.
static float ProjectX( float scaleX, float offsetX, float viewX, float viewZ ) {
	const float w = -viewZ;
	return ( scaleX * viewX + offsetX * viewZ ) / w;
}

static void TestJitterAlignment() {
	// A static world point under a static camera: the only difference between
	// the frames is temporal AA jitter. Its blur velocity must be zero.
	const float scale = 1.0f;
	const float neutral[2] = { 0.0f, 0.0f };
	const float jitterCurrent[2] = { 0.00078f, -0.00031f };	// half a pixel at 2560 wide
	const float currentApplied[2] = { neutral[0] + jitterCurrent[0], neutral[1] + jitterCurrent[1] };
	float aligned[2];
	MotionBlurCore_AlignPreviousOffsets( currentApplied, neutral, neutral, aligned );
	Check( Near( aligned[0], currentApplied[0] ) && Near( aligned[1], currentApplied[1] ),
		"an unmoved projection must reproject with the current jitter" );
	const float viewX = 3.0f;
	const float viewZ = -40.0f;
	const float currentNdc = ProjectX( scale, currentApplied[0], viewX, viewZ );
	const float previousNdc = ProjectX( scale, aligned[0], viewX, viewZ );
	Check( Near( currentNdc, previousNdc, 1.0e-6f ), "jitter alone must produce no velocity" );

	// A genuine off-axis change survives alignment unchanged.
	const float previousNeutral[2] = { 0.01f, 0.0f };
	MotionBlurCore_AlignPreviousOffsets( currentApplied, neutral, previousNeutral, aligned );
	Check( Near( aligned[0] - currentApplied[0], previousNeutral[0] - neutral[0] ),
		"the neutral difference must remain after alignment" );

	// The backend may already have recentred the applied offsets.
	MotionBlurCore_AlignPreviousOffsets( neutral, neutral, neutral, aligned );
	Check( Near( aligned[0], 0.0f ) && Near( aligned[1], 0.0f ),
		"recentred projections need no correction" );
}

static void TestShutter() {
	motionBlurShutterState_t state;
	MotionBlurCore_ResetShutter( state );
	Check( Near( MotionBlurCore_ShutterScale( state, 16.667f ), 1.0f ),
		"no interval yet keeps the per-frame length" );
	MotionBlurCore_UpdateShutter( state, 0 );
	Check( !state.valid, "a zero interval cannot seed the estimate" );
	MotionBlurCore_UpdateShutter( state, 16 );
	Check( state.valid && Near( state.smoothedFrameMsec, 16.0f ), "the first interval seeds the estimate" );
	Check( Near( MotionBlurCore_ShutterScale( state, 0.0f ), 1.0f ),
		"shutter 0 keeps the original per-frame behaviour" );

	// 144 Hz presentation in integer milliseconds: 7,7,7,6 repeating.
	MotionBlurCore_ResetShutter( state );
	const int pattern[4] = { 7, 7, 7, 6 };
	float minScale = 100.0f;
	float maxScale = 0.0f;
	for ( int i = 0; i < 400; i++ ) {
		MotionBlurCore_UpdateShutter( state, pattern[i & 3] );
		if ( i >= 100 ) {
			const float scale = MotionBlurCore_ShutterScale( state, 16.667f );
			minScale = scale < minScale ? scale : minScale;
			maxScale = scale > maxScale ? scale : maxScale;
		}
	}
	Check( Near( state.smoothedFrameMsec, 6.75f, 0.2f ), "the estimate converges on the mean interval" );
	Check( maxScale - minScale < 0.1f * minScale,
		"integer-millisecond quantisation must not pulse the blur length" );

	// The same camera speed gives the same blur length at 60 and 240 Hz.
	const float speedPixelsPerMsec = 0.18f;
	motionBlurShutterState_t at60;
	motionBlurShutterState_t at240;
	MotionBlurCore_ResetShutter( at60 );
	MotionBlurCore_ResetShutter( at240 );
	for ( int i = 0; i < 200; i++ ) {
		MotionBlurCore_UpdateShutter( at60, ( i % 3 ) == 2 ? 17 : 16 );
		MotionBlurCore_UpdateShutter( at240, ( i & 1 ) ? 4 : 5 );
	}
	const float length60 = speedPixelsPerMsec * at60.smoothedFrameMsec * MotionBlurCore_ShutterScale( at60, 16.667f );
	const float length240 = speedPixelsPerMsec * at240.smoothedFrameMsec * MotionBlurCore_ShutterScale( at240, 16.667f );
	Check( Near( length60, length240, 0.05f * length60 ), "blur length must not depend on the frame rate" );

	// Equal-time frames feed a zero sample without resetting.
	MotionBlurCore_ResetShutter( state );
	for ( int i = 0; i < 200; i++ ) {
		MotionBlurCore_UpdateShutter( state, i & 1 );
	}
	Check( state.valid && Near( state.smoothedFrameMsec, 0.5f, 0.1f ), "sub-millisecond frames average out" );
	Check( Near( MotionBlurCore_ShutterScale( state, 16.667f ), MOTION_BLUR_SHUTTER_MAX_SCALE ),
		"very high frame rates clamp the scale" );
	MotionBlurCore_UpdateShutter( state, MOTION_BLUR_MAX_TIME_GAP_MSEC + 1 );
	Check( Near( state.smoothedFrameMsec, 0.5f, 0.1f ), "a discontinuity sample is ignored" );

	MotionBlurCore_ResetShutter( state );
	MotionBlurCore_UpdateShutter( state, 90 );
	Check( Near( MotionBlurCore_ShutterScale( state, 16.667f ), 16.667f / 90.0f ),
		"slow frames shorten the per-frame length" );
	MotionBlurCore_ResetShutter( state );
	MotionBlurCore_UpdateShutter( state, 100 );
	Check( MotionBlurCore_ShutterScale( state, 1.0f ) >= MOTION_BLUR_SHUTTER_MIN_SCALE,
		"the scale has a lower bound" );
}

static void TestCenterRotation() {
	motionBlurCameraSample_t previous = Camera( 1000 );
	motionBlurCameraSample_t current = Camera( 1007 );
	Check( Near( MotionBlurCore_CenterRotationPixels( current, previous, 1920.0f, 1080.0f ), 0.0f ),
		"an unrotated camera has no centre motion" );
	// yaw the current camera by 1 degree: forward (1,0,0), left (0,1,0)
	const float angle = 1.0f * 3.14159265f / 180.0f;
	current.axis[0][0] = std::cos( angle );
	current.axis[0][1] = std::sin( angle );
	current.axis[1][0] = -std::sin( angle );
	current.axis[1][1] = std::cos( angle );
	const float expected = current.projectionNeutral[0] * std::tan( angle ) * 0.5f * 1920.0f;
	Check( Near( MotionBlurCore_CenterRotationPixels( current, previous, 1920.0f, 1080.0f ), expected, 0.01f ),
		"a 1 degree yaw moves the centre by P[0] tan(1 deg) half-widths" );
	motionBlurCameraSample_t behind = Camera( 1007 );
	behind.axis[0][0] = -1.0f;
	Check( Near( MotionBlurCore_CenterRotationPixels( behind, previous, 1920.0f, 1080.0f ), 0.0f ),
		"a reversal has no finite centre motion" );
}

int main() {
	TestHistoryPolicy();
	TestJitterAlignment();
	TestShutter();
	TestCenterRotation();
	if ( failures != 0 ) {
		std::fprintf( stderr, "MotionBlurCoreTest: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "MotionBlurCoreTest: passed\n" );
	return 0;
}
