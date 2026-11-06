// Copyright (C) 2026 DarkMatter Productions
//
// Backend-neutral camera-history policy for the motion blur post-process.
// OpenGL (draw_common.cpp) and Vulkan (vk_PostProcess.cpp) keep their own
// state structures and GPU work; both route every history and strength
// decision through these functions so the backends cannot drift apart.

#ifndef __MOTION_BLUR_CORE_H__
#define __MOTION_BLUR_CORE_H__

#include <cmath>

// Why a main view did or did not blend against the previous camera.
enum motionBlurHistoryResult_t {
	MOTION_BLUR_HISTORY_USABLE = 0,
	MOTION_BLUR_HISTORY_FIRST_SAMPLE,		// nothing recorded since the last reset
	MOTION_BLUR_HISTORY_VIDEO_RESTART,
	MOTION_BLUR_HISTORY_WORLD_CHANGED,
	MOTION_BLUR_HISTORY_VIEWPORT_CHANGED,
	MOTION_BLUR_HISTORY_EXPLICIT_CUT,		// renderView.forceUpdate: an authored camera cut
	MOTION_BLUR_HISTORY_TIME_REVERSED,
	MOTION_BLUR_HISTORY_TIME_GAP,
	MOTION_BLUR_HISTORY_CAMERA_STILL,
	MOTION_BLUR_HISTORY_CAMERA_TELEPORT,
	MOTION_BLUR_HISTORY_PROJECTION_CHANGED,
	MOTION_BLUR_HISTORY_RESULT_COUNT
};

static const int MOTION_BLUR_MAX_TIME_GAP_MSEC = 100;
static const float MOTION_BLUR_TELEPORT_DISTANCE = 512.0f;
static const float MOTION_BLUR_STILL_TRANSLATION = 0.10f;
static const float MOTION_BLUR_STILL_AXIS = 0.00075f;

// One camera sample. projectionNeutral holds projectionMatrix[0], [5] and the
// jitter-free [8], [9]: temporal AA moves [8]/[9] by a sub-pixel amount every
// frame, which is not a projection change and must not reset motion blur.
struct motionBlurCameraSample_t {
	int		timeMsec;
	float	origin[3];
	float	axis[3][3];
	float	fov[2];
	float	projectionNeutral[4];
	float	depthProjection[2];
};

struct motionBlurHistoryContext_t {
	bool	historyValid;
	bool	videoRestarted;
	bool	worldChanged;
	bool	viewportChanged;
	bool	explicitCut;
	// Object vectors can blur a moving entity under a still camera.
	bool	allowStillCamera;
};

inline const char *MotionBlurCore_HistoryResultName( motionBlurHistoryResult_t result ) {
	switch ( result ) {
	case MOTION_BLUR_HISTORY_USABLE: return "usable";
	case MOTION_BLUR_HISTORY_FIRST_SAMPLE: return "first-sample";
	case MOTION_BLUR_HISTORY_VIDEO_RESTART: return "video-restart";
	case MOTION_BLUR_HISTORY_WORLD_CHANGED: return "world-changed";
	case MOTION_BLUR_HISTORY_VIEWPORT_CHANGED: return "viewport-changed";
	case MOTION_BLUR_HISTORY_EXPLICIT_CUT: return "explicit-cut";
	case MOTION_BLUR_HISTORY_TIME_REVERSED: return "time-reversed";
	case MOTION_BLUR_HISTORY_TIME_GAP: return "time-gap";
	case MOTION_BLUR_HISTORY_CAMERA_STILL: return "camera-still";
	case MOTION_BLUR_HISTORY_CAMERA_TELEPORT: return "camera-teleport";
	case MOTION_BLUR_HISTORY_PROJECTION_CHANGED: return "projection-changed";
	default: return "unknown";
	}
}

// Reprojection works in normalised view coordinates and the vectors are
// measured in the current viewport's pixels, so a scene-scale change (dynamic
// or manual resolution scaling) keeps the history; only a change of shape
// does not.
inline bool MotionBlurCore_ViewportChanged( int currentWidth, int currentHeight,
		int previousWidth, int previousHeight ) {
	if ( currentWidth <= 0 || currentHeight <= 0 || previousWidth <= 0 || previousHeight <= 0 ) {
		return true;
	}
	const float currentAspect = static_cast<float>( currentWidth ) / static_cast<float>( currentHeight );
	const float previousAspect = static_cast<float>( previousWidth ) / static_cast<float>( previousHeight );
	return std::fabs( currentAspect - previousAspect ) > 0.01f * previousAspect;
}

inline float MotionBlurCore_DistanceSquared( const float a[3], const float b[3] ) {
	const float x = a[0] - b[0];
	const float y = a[1] - b[1];
	const float z = a[2] - b[2];
	return x * x + y * y + z * z;
}

inline bool MotionBlurCore_CameraMovedEnough( const motionBlurCameraSample_t &current,
		const motionBlurCameraSample_t &previous ) {
	if ( MotionBlurCore_DistanceSquared( current.origin, previous.origin )
			>= MOTION_BLUR_STILL_TRANSLATION * MOTION_BLUR_STILL_TRANSLATION ) {
		return true;
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( MotionBlurCore_DistanceSquared( current.axis[i], previous.axis[i] )
				>= MOTION_BLUR_STILL_AXIS * MOTION_BLUR_STILL_AXIS ) {
			return true;
		}
	}
	return false;
}

inline bool MotionBlurCore_ProjectionChanged( const motionBlurCameraSample_t &current,
		const motionBlurCameraSample_t &previous ) {
	if ( std::fabs( current.fov[0] - previous.fov[0] ) > 0.01f
			|| std::fabs( current.fov[1] - previous.fov[1] ) > 0.01f ) {
		return true;
	}
	for ( int i = 0; i < 4; i++ ) {
		if ( std::fabs( current.projectionNeutral[i] - previous.projectionNeutral[i] ) > 0.0001f ) {
			return true;
		}
	}
	for ( int i = 0; i < 2; i++ ) {
		if ( std::fabs( current.depthProjection[i] - previous.depthProjection[i] ) > 0.0001f ) {
			return true;
		}
	}
	return false;
}

// Presentation frames at high refresh rates (and every frame while the game is
// paused) may share one millisecond; that is not a discontinuity. Only time
// running backwards or a gap longer than a slow frame resets the history.
inline motionBlurHistoryResult_t MotionBlurCore_ValidateHistory(
		const motionBlurHistoryContext_t &context,
		const motionBlurCameraSample_t &current,
		const motionBlurCameraSample_t &previous ) {
	if ( !context.historyValid ) {
		return MOTION_BLUR_HISTORY_FIRST_SAMPLE;
	}
	if ( context.videoRestarted ) {
		return MOTION_BLUR_HISTORY_VIDEO_RESTART;
	}
	if ( context.worldChanged ) {
		return MOTION_BLUR_HISTORY_WORLD_CHANGED;
	}
	if ( context.viewportChanged ) {
		return MOTION_BLUR_HISTORY_VIEWPORT_CHANGED;
	}
	if ( context.explicitCut ) {
		return MOTION_BLUR_HISTORY_EXPLICIT_CUT;
	}
	const int deltaMsec = current.timeMsec - previous.timeMsec;
	if ( deltaMsec < 0 ) {
		return MOTION_BLUR_HISTORY_TIME_REVERSED;
	}
	if ( deltaMsec > MOTION_BLUR_MAX_TIME_GAP_MSEC ) {
		return MOTION_BLUR_HISTORY_TIME_GAP;
	}
	if ( !context.allowStillCamera && !MotionBlurCore_CameraMovedEnough( current, previous ) ) {
		return MOTION_BLUR_HISTORY_CAMERA_STILL;
	}
	if ( MotionBlurCore_DistanceSquared( current.origin, previous.origin )
			> MOTION_BLUR_TELEPORT_DISTANCE * MOTION_BLUR_TELEPORT_DISTANCE ) {
		return MOTION_BLUR_HISTORY_CAMERA_TELEPORT;
	}
	if ( MotionBlurCore_ProjectionChanged( current, previous ) ) {
		return MOTION_BLUR_HISTORY_PROJECTION_CHANGED;
	}
	return MOTION_BLUR_HISTORY_USABLE;
}

// The previous frame's projection offsets as the shaders must use them: the
// jitter-free previous offsets plus the jitter applied to the current frame.
// A depth-reconstructed point then reprojects to (current pixel + motion),
// so neither frame's sub-pixel jitter appears as velocity.
inline void MotionBlurCore_AlignPreviousOffsets( const float currentApplied[2],
		const float currentNeutral[2], const float previousNeutral[2], float out[2] ) {
	for ( int i = 0; i < 2; i++ ) {
		out[i] = previousNeutral[i] + ( currentApplied[i] - currentNeutral[i] );
	}
}

// Frame-rate-independent blur length. Velocities are measured between two
// presented frames; scaling them by shutter / frame interval makes a given
// camera speed blur by the same amount at 60, 144 or 240 Hz. Presentation
// time is integer milliseconds, so the interval is smoothed before use.
struct motionBlurShutterState_t {
	bool	valid;
	float	smoothedFrameMsec;
};

static const float MOTION_BLUR_SHUTTER_SMOOTHING = 0.125f;
static const float MOTION_BLUR_SHUTTER_MIN_SCALE = 0.125f;
static const float MOTION_BLUR_SHUTTER_MAX_SCALE = 8.0f;

inline void MotionBlurCore_ResetShutter( motionBlurShutterState_t &state ) {
	state.valid = false;
	state.smoothedFrameMsec = 0.0f;
}

inline void MotionBlurCore_UpdateShutter( motionBlurShutterState_t &state, int deltaMsec ) {
	if ( deltaMsec < 0 || deltaMsec > MOTION_BLUR_MAX_TIME_GAP_MSEC ) {
		return;
	}
	if ( !state.valid ) {
		if ( deltaMsec <= 0 ) {
			return;		// seed only from an interval that advanced
		}
		state.smoothedFrameMsec = static_cast<float>( deltaMsec );
		state.valid = true;
		return;
	}
	state.smoothedFrameMsec += ( static_cast<float>( deltaMsec ) - state.smoothedFrameMsec )
		* MOTION_BLUR_SHUTTER_SMOOTHING;
}

// shutterMsec <= 0 keeps the original per-frame behaviour (scale 1).
inline float MotionBlurCore_ShutterScale( const motionBlurShutterState_t &state, float shutterMsec ) {
	if ( !( shutterMsec > 0.0f ) || !state.valid ) {
		return 1.0f;
	}
	const float frameMsec = state.smoothedFrameMsec > 0.25f ? state.smoothedFrameMsec : 0.25f;
	float scale = shutterMsec / frameMsec;
	if ( scale < MOTION_BLUR_SHUTTER_MIN_SCALE ) {
		scale = MOTION_BLUR_SHUTTER_MIN_SCALE;
	} else if ( scale > MOTION_BLUR_SHUTTER_MAX_SCALE ) {
		scale = MOTION_BLUR_SHUTTER_MAX_SCALE;
	}
	return scale;
}

// Screen-space speed, in pixels per frame, that camera rotation gives a
// distant point at the centre of the view. The blur pass scales its velocities
// by the shutter scale, so this times the scale is the frame-rate-independent
// quantity the qualification compares across frame caps.
inline float MotionBlurCore_CenterRotationPixels( const motionBlurCameraSample_t &current,
		const motionBlurCameraSample_t &previous, float viewportWidth, float viewportHeight ) {
	const float *direction = previous.axis[0];
	const float forward = direction[0] * current.axis[0][0] + direction[1] * current.axis[0][1]
		+ direction[2] * current.axis[0][2];
	if ( forward <= 0.0001f ) {
		return 0.0f;
	}
	const float left = direction[0] * current.axis[1][0] + direction[1] * current.axis[1][1]
		+ direction[2] * current.axis[1][2];
	const float up = direction[0] * current.axis[2][0] + direction[1] * current.axis[2][1]
		+ direction[2] * current.axis[2][2];
	const float x = current.projectionNeutral[0] * ( left / forward ) * 0.5f * viewportWidth;
	const float y = current.projectionNeutral[1] * ( up / forward ) * 0.5f * viewportHeight;
	return std::sqrt( x * x + y * y );
}

// Per-view counters published by both backends (rendererMotionBlurStatus).
struct motionBlurStats_t {
	int		mainViews;				// main views that requested motion blur
	int		drawnViews;				// views the blur pass actually drew
	int		objectVectorViews;		// drawn views with an object-vector target
	int		jitterAlignedViews;		// drawn views that cancelled TAA jitter
	int		subviewSkips;			// subviews passed over without touching history
	int		disabledViews;			// requested but disabled (reason below)
	int		history[MOTION_BLUR_HISTORY_RESULT_COUNT];
	// object-vector ownership, summed over drawn views
	int		rigid;
	int		posed;
	int		weapon;
	int		weaponStill;			// weapon surfaces with no exact history: zero motion
	int		cameraFallback;			// surfaces that fell back to camera reprojection
	// MotionBlurCore_CenterRotationPixels over drawn views
	int		centerSamples;
	float	centerPixelsSum;		// per presented frame
	float	scaledCenterPixelsSum;	// after the shutter scale
	// most recent main view
	int		lastFrame;
	float	lastShutterScale;
	float	lastFrameMsec;
	float	lastStrength;
	float	lastMaxPixels;
	int		lastSamples;
	const char *lastDisabledReason;
};

#endif // !__MOTION_BLUR_CORE_H__
