// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __TEMPORAL_PRESENTATION_H__
#define __TEMPORAL_PRESENTATION_H__

#include "AdvancedScreenSpaceCore.h"
#include "TemporalPresentationCore.h"
#include "TemporalHistoryCore.h"

class idCmdArgs;

typedef struct temporalPresentationFrameState_s {
	bool		dynamicResolutionRequested;
	bool		dynamicResolutionActive;
	bool		temporalAARequested;
	bool		captureFrozen;
	bool		captureForcedNative;
	bool		timingSupported;
	bool		timingValid;
	int		frameNumber;
	int		nativeWidth;
	int		nativeHeight;
	int		sceneWidth;
	int		sceneHeight;
	int		manualScalePercent;
	int		effectiveScalePercent;
	int		sampledScalePercent;
	int		timingFrameNumber;
	int		timingAgeFrames;
	int		timingBackend;
	unsigned int	timingGeneration;
	unsigned long long timingMicroseconds;
	unsigned long long targetMicroseconds;
	float		temporalFeedback;
	float		temporalReactiveScale;
	int		temporalDebugMode;
	advancedScreenSpaceConfig_t advancedScreenSpace;
	temporalResolutionDecision_t decision;
} temporalPresentationFrameState_t;

// Called once by the renderer front end after its frame and timing generation
// have advanced. The selected percentage remains immutable until the following
// BeginFrame, including synchronous save-preview command flushes.
void R_TemporalPresentation_BeginFrame( int nativeWidth, int nativeHeight,
	bool captureFrame );

const temporalPresentationFrameState_t &R_TemporalPresentation_GetFrameState( void );
int R_TemporalPresentation_EffectiveScreenFraction( void );
bool R_TemporalPresentation_DynamicResolutionRequested( void );
bool R_TemporalPresentation_TemporalAARequested( void );
bool R_TemporalPresentation_ScreenSpaceEffectsRequested( void );
// openQ4 VR: the presentation extent follows the eye or virtual-screen target
// the front end is rendering to (idRenderSystem::SetVRRenderTarget).
void R_TemporalPresentation_SetVRExtent( int width, int height );
const char *R_TemporalPresentation_DecisionName( temporalResolutionDecision_t decision );

// Temporal image histories use a generation separate from delayed GPU timing.
// Backends may invalidate it for a camera cut, view-identity change, resource
// loss, or another discontinuity without disturbing the timing controller.
unsigned int R_TemporalPresentation_HistoryGeneration( void );
void R_TemporalPresentation_InvalidateHistory( const char *reason );
const char *R_TemporalPresentation_LastHistoryResetReason( void );

// A save-preview readback can begin after BeginFrame and after the game has
// queued its scene. Mark the already-latched frame ineligible for delayed GPU
// feedback without changing its scale or timing generation, and advance the
// separate image-history generation so an untouched ping-pong destination
// cannot be sampled on the following gameplay frame.
void R_TemporalPresentation_MarkCurrentFrameCapture( const char *reason );

// Seals a deterministic per-view identity, camera-cut decision, and sub-pixel
// jitter before the projection matrix is built. Repeated calls for the same
// frame-allocated view are harmless.
void R_TemporalPresentation_PrepareView( struct viewDef_s *viewDef );

// Exact motion for posed surfaces. A model's pose update calls Capture just
// before it overwrites verts, so a surface drawn last frame keeps that frame's
// model-space positions; MarkPositionsDrawn records that verts were drawn this
// frame without a new pose (animation LOD). PreviousPositionCache uploads the
// capture as a frame-temp idVec3 stream for one main-view drawSurf.
void R_TemporalPresentation_CapturePreviousPositions( struct srfTriangles_s *tri );
void R_TemporalPresentation_MarkPositionsDrawn( struct srfTriangles_s *tri );
struct vertCache_s *R_TemporalPresentation_PreviousPositionCache(
	const struct drawSurf_s *drawSurf );
// History rejection [0,1] written for particles, effects and translucent
// entity surfaces (r_temporalAAReactiveEffects).
float R_TemporalPresentation_EffectReactiveStrength( void );
// Contrast-adaptive sharpening [0,1] for presenting a resolved temporal
// history (r_temporalAASharpness); never written back into the history.
float R_TemporalPresentation_PresentSharpness( void );

// What the last temporal velocity pass owned per pixel, for
// rendererTemporalPresentationStatus. Surfaces, not pixels.
typedef struct temporalOwnershipStats_s {
	int		frameNumber;
	int		rigid;			// exact vectors from a previous model transform
	int		posed;			// exact vectors from previous model-space positions
	int		weapon;			// exact view-weapon surfaces (counted in rigid/posed too)
	int		reactive;		// reactive coverage
	int		missed;			// could not be drawn: the view falls back to screen regions
	bool	complete;
} temporalOwnershipStats_t;

void R_TemporalPresentation_RecordOwnership( const temporalOwnershipStats_t &stats );
void R_TemporalPresentation_FinalizeViewProjection( struct viewDef_s *viewDef );
unsigned long long R_TemporalPresentation_ViewIdentity(
	const struct viewDef_s *viewDef );
const char *R_TemporalPresentation_HistoryResetReasonName(
	temporalHistoryResetReason_t reason );

void R_TemporalPresentation_PrintStatus_f( const idCmdArgs &args );

// Motion blur keeps its own camera history, independent of temporal AA, but
// shares the posed-surface previous positions above. Capture them when either
// consumer wants exact skinned motion.
bool R_TemporalPresentation_PreviousPositionsRequested( void );
// Cumulative per-view motion blur counters written by the active backend.
struct motionBlurStats_t &R_TemporalPresentation_MotionBlurStats( void );
void R_TemporalPresentation_ResetMotionBlurStats( void );
// r_motionBlurShutterMsec, clamped; 0 keeps the original per-frame blur length.
float R_TemporalPresentation_MotionBlurShutterMsec( void );
void R_TemporalPresentation_PrintMotionBlurStatus_f( const idCmdArgs &args );

#endif /* !__TEMPORAL_PRESENTATION_H__ */

void R_TemporalPresentation_SubmitFrame( void );
void R_TemporalPresentation_RetireFrame( void );
