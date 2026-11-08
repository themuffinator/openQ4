// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "RendererMetrics.h"
#include "TemporalPresentation.h"
#include "MotionBlurCore.h"

idCVar r_dynamicResolutionMinScale( "r_dynamicResolutionMinScale", "50",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"minimum automatic 3D scene scale percentage", 10, 100 );
idCVar r_dynamicResolutionMaxScale( "r_dynamicResolutionMaxScale", "100",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum automatic 3D scene scale percentage", 10, 100 );
idCVar r_dynamicResolutionTargetMsec( "r_dynamicResolutionTargetMsec", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"GPU frame-time target in milliseconds; 0 derives it from the presentation cap and display", 0.0f, 1000.0f );
idCVar r_dynamicResolutionTargetUtilization( "r_dynamicResolutionTargetUtilization", "90",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"percentage of the presentation interval available to renderer GPU work", 50, 100 );
idCVar r_dynamicResolutionDropStep( "r_dynamicResolutionDropStep", "5",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum percentage-point reduction per newly retired over-budget sample", 1, 50 );
idCVar r_dynamicResolutionRaiseStep( "r_dynamicResolutionRaiseStep", "2",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"percentage-point increase after a complete under-budget streak", 1, 25 );
idCVar r_dynamicResolutionRaiseFrames( "r_dynamicResolutionRaiseFrames", "30",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"new retired under-budget samples required before increasing scene scale", 1, 1000 );
idCVar r_dynamicResolutionRaiseThreshold( "r_dynamicResolutionRaiseThreshold", "85",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"target-budget percentage below which a sample contributes to scale recovery", 1, 99 );
idCVar r_dynamicResolutionAlignment( "r_dynamicResolutionAlignment", "8",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"scene-target dimension alignment in pixels", 1, 256 );
idCVar r_dynamicResolutionMaxSampleAge( "r_dynamicResolutionMaxSampleAge", "32",
	CVAR_RENDERER | CVAR_INTEGER,
	"maximum delayed GPU-timing sample age accepted by the controller", 1, 1000 );
idCVar r_dynamicResolutionMissingSampleReset( "r_dynamicResolutionMissingSampleReset", "120",
	CVAR_RENDERER | CVAR_INTEGER,
	"frames without a usable retired timing sample before returning to the safe scale ceiling", 1, 10000 );
idCVar r_dynamicResolutionCaptureNative( "r_dynamicResolutionCaptureNative", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"render screenshot/save-preview 3D scenes at 100%; default keeps the active scale and freezes feedback" );
idCVar r_dynamicResolutionDebug( "r_dynamicResolutionDebug", "0",
	CVAR_RENDERER | CVAR_INTEGER,
	"print automatic resolution decisions: 0 = off, 1 = scale/reset, 2 = every frame", 0, 2 );

idCVar r_temporalAA( "r_temporalAA", "2",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"temporal anti-aliasing and upscaling, with automatic SMAA rollback: 0 = off, 1 = always, 2 = automatic (upscale whenever the 3D scene renders below native resolution with a smooth scale mode, or with dynamic resolution)", 0, 2 );
idCVar r_temporalAAFeedback( "r_temporalAAFeedback", "0.90",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"maximum accepted temporal-history contribution", 0.0f, 0.98f );
idCVar r_temporalAAReactiveScale( "r_temporalAAReactiveScale", "1.0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"reactive-mask rejection strength for particles, translucency, deforms, and in-world GUI", 0.0f, 2.0f );
idCVar r_temporalAADebug( "r_temporalAADebug", "0",
	CVAR_RENDERER | CVAR_INTEGER,
	"temporal presentation diagnostic view: 0 = final, 1 = velocity, 2 = reactive, 3 = history weight", 0, 3 );
static bool R_TemporalPresentation_TemporalAAWanted( void );

idCVar r_temporalAAReactiveEffects( "r_temporalAAReactiveEffects", "0.85",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"temporal AA history rejection where particles, effects and translucent moving surfaces cover a pixel", 0.0f, 1.0f );
idCVar r_temporalAASharpness( "r_temporalAASharpness", "0.5",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"contrast-adaptive sharpening applied when temporal AA presents its history (0 = off); the history itself stays unsharpened", 0.0f, 1.0f );

idCVar r_rendererFroxelVolumetrics( "r_rendererFroxelVolumetrics", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"enable bounded view-aligned froxel volumetric integration in the native scene presentation tail" );
idCVar r_froxelVolumetricDensity( "r_froxelVolumetricDensity", "0.00012",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"base extinction density for optional froxel volumetrics", 0.00001f, 0.01f );
idCVar r_froxelVolumetricMaxDistance( "r_froxelVolumetricMaxDistance", "2048",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"maximum world-space distance integrated by optional froxel volumetrics", 64.0f, 8192.0f );
idCVar r_froxelVolumetricSlices( "r_froxelVolumetricSlices", "12",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"bounded view-depth slices integrated by optional froxel volumetrics", 4,
	ADVANCED_SCREEN_SPACE_FROXEL_MAX_SLICES );
idCVar r_rendererSSR( "r_rendererSSR", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"enable bounded depth-derived screen-space reflections in the native scene presentation tail" );
idCVar r_screenReflectionIntensity( "r_screenReflectionIntensity", "0.35",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"maximum contribution of optional screen-space reflections", 0.0f, 1.0f );
idCVar r_screenReflectionMaxDistance( "r_screenReflectionMaxDistance", "512",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"maximum view-space ray distance for optional screen-space reflections", 32.0f, 2048.0f );
idCVar r_screenReflectionSteps( "r_screenReflectionSteps", "10",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"bounded ray-march steps for optional screen-space reflections", 4,
	ADVANCED_SCREEN_SPACE_SSR_MAX_STEPS );
idCVar r_rendererSSGI( "r_rendererSSGI", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"enable bounded eight-tap screen-space diffuse GI in the native scene presentation tail" );
idCVar r_screenGIIntensity( "r_screenGIIntensity", "0.25",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"maximum contribution of optional screen-space diffuse GI", 0.0f, 1.0f );

static const int TEMPORAL_SCALE_HISTORY_COUNT = 128;

typedef struct temporalScaleHistoryEntry_s {
	bool		valid;
	bool		eligibleForFeedback;
	int		frameNumber;
	unsigned int	generation;
	int		scalePercent;
} temporalScaleHistoryEntry_t;

static temporalResolutionState_t rg_temporalResolutionState;
static bool rg_temporalPresentationInitialized = false;
static bool rg_dynamicResolutionEnabledLastFrame = false;
static bool rg_temporalAAEnabledLastFrame = false;
static advancedScreenSpaceConfig_t rg_advancedScreenSpaceLast = {};
static int rg_videoRestartCount = -1;
static temporalPresentationFrameState_t rg_temporalFrameStates[2];
static int rg_temporalLastScalePercent;
static unsigned int rg_temporalFrameGenerations[2];
static char rg_temporalFrameResetReasons[2][128];
static int R_TemporalPresentation_FrameSlot() {
	const frameData_t *frame = R_BackEndFrameData();
	return frame != NULL ? frame->slot : 0;
}
static temporalPresentationFrameState_t &R_TemporalPresentation_CurrentFrame() {
	return rg_temporalFrameStates[R_TemporalPresentation_FrameSlot()];
}
static temporalScaleHistoryEntry_t rg_temporalScaleHistory[TEMPORAL_SCALE_HISTORY_COUNT];
static int rg_temporalScaleHistoryCursor = 0;
static unsigned int rg_temporalHistoryGeneration = 1;
static idStr rg_temporalHistoryResetReason = "renderer initialization";

static const int TEMPORAL_STATS_REASON_SLOTS = 12;
static const int TEMPORAL_VIEW_RESET_COUNT = TEMPORAL_HISTORY_RESET_RESOURCE_LOSS + 1;

typedef struct temporalReasonCount_s {
	char	reason[96];
	int		count;
} temporalReasonCount_t;

typedef struct temporalResolveStats_s {
	int		outcomes[TEMPORAL_RESOLVE_OUTCOME_COUNT];
	int		velocity[4];			// none-attempted, none, partial, complete
	temporalReasonCount_t fallbacks[TEMPORAL_STATS_REASON_SLOTS];
	temporalReasonCount_t invalidations[TEMPORAL_STATS_REASON_SLOTS];
	int		viewResets[TEMPORAL_VIEW_RESET_COUNT];
	int		otherReasons;			// reasons beyond the slots
} temporalResolveStats_t;

static temporalResolveStats_t rg_temporalResolveStats;

static void R_TemporalPresentation_CountReason( temporalReasonCount_t *slots, const char *reason ) {
	if ( reason == NULL || reason[0] == '\0' ) {
		reason = "unspecified";
	}
	for ( int i = 0; i < TEMPORAL_STATS_REASON_SLOTS; i++ ) {
		if ( slots[i].count == 0 ) {
			idStr::Copynz( slots[i].reason, reason, sizeof( slots[i].reason ) );
			slots[i].count = 1;
			return;
		}
		if ( idStr::Cmp( slots[i].reason, reason ) == 0 ) {
			slots[i].count++;
			return;
		}
	}
	rg_temporalResolveStats.otherReasons++;
}


// openQ4 VR: dynamic resolution, screen-space effects and motion blur stand
// down while a VR frame presents. Temporal AA keeps one history per eye
// (VR-05): each eye's view mixes its eye into its identity, so it keeps its
// own camera and object history, and backends keep its images and velocity
// state in its history slot (viewDef_t::temporalHistorySlot). Automatic TAA
// (r_temporalAA 2) only follows a below-native scene, which VR never renders,
// so a headset gets TAA with r_temporalAA 1.
static bool R_TemporalPresentation_VRFrame( void ) {
	return tr.vrFrame.active;
}

// The eye the front end renders this VR frame's view for, -1 otherwise.
static int R_TemporalPresentation_VREye( void ) {
	return R_VR_FrontEndEye();
}

int R_TemporalPresentation_HistorySlot( void ) {
	return R_TemporalPresentation_VREye() == 1 ? 1 : 0;
}

static advancedScreenSpaceConfig_t R_TemporalPresentation_BuildAdvancedScreenSpaceConfig( void ) {
	const bool allowed = !R_TemporalPresentation_VRFrame();
	return AdvancedScreenSpaceCore_Build(
		allowed && r_rendererModernQuality.GetBool(),
		allowed && r_rendererFroxelVolumetrics.GetBool(),
		allowed && r_rendererSSR.GetBool(),
		allowed && r_rendererSSGI.GetBool(),
		r_froxelVolumetricDensity.GetFloat(),
		r_froxelVolumetricMaxDistance.GetFloat(),
		r_froxelVolumetricSlices.GetInteger(),
		r_screenReflectionIntensity.GetFloat(),
		r_screenReflectionMaxDistance.GetFloat(),
		r_screenReflectionSteps.GetInteger(),
		r_screenGIIntensity.GetFloat() );
}

static bool R_TemporalPresentation_AdvancedScreenSpaceEqual(
		const advancedScreenSpaceConfig_t &lhs,
		const advancedScreenSpaceConfig_t &rhs ) {
	return lhs.effectMask == rhs.effectMask
		&& lhs.froxelDensity == rhs.froxelDensity
		&& lhs.froxelMaxDistance == rhs.froxelMaxDistance
		&& lhs.froxelSlices == rhs.froxelSlices
		&& lhs.ssrIntensity == rhs.ssrIntensity
		&& lhs.ssrMaxDistance == rhs.ssrMaxDistance
		&& lhs.ssrSteps == rhs.ssrSteps
		&& lhs.ssgiIntensity == rhs.ssgiIntensity;
}
static int rg_lastCaptureMarkedFrame = -1;

static const int TEMPORAL_VIEW_HISTORY_SLOTS = 32;

typedef struct temporalViewHistorySlot_s {
	bool valid;
	int lastUsedFrame;
	temporalCameraState_t camera;
} temporalViewHistorySlot_t;

static temporalViewHistorySlot_t
	rg_temporalViewHistory[TEMPORAL_VIEW_HISTORY_SLOTS];

static void R_TemporalPresentation_ClearViewHistory( void ) {
	memset( rg_temporalViewHistory, 0, sizeof( rg_temporalViewHistory ) );
}

static void R_TemporalPresentation_ClearScaleHistory( void ) {
	memset( rg_temporalScaleHistory, 0, sizeof( rg_temporalScaleHistory ) );
	rg_temporalScaleHistoryCursor = 0;
}

static int R_TemporalPresentation_FrameAge( int currentFrame, int sampleFrame ) {
	const unsigned int age = static_cast<unsigned int>( currentFrame )
		- static_cast<unsigned int>( sampleFrame );
	if ( age >= 0x80000000u || age > 0x7fffffffu ) {
		return -1;
	}
	return static_cast<int>( age );
}

static int R_TemporalPresentation_FindSampleScale( int frameNumber,
		unsigned int generation ) {
	for ( int i = 0; i < TEMPORAL_SCALE_HISTORY_COUNT; i++ ) {
		const temporalScaleHistoryEntry_t &entry = rg_temporalScaleHistory[i];
		if ( entry.valid && entry.eligibleForFeedback
				&& entry.frameNumber == frameNumber
				&& entry.generation == generation ) {
			return entry.scalePercent;
		}
	}
	return 0;
}

static void R_TemporalPresentation_RecordFrameScale( int frameNumber,
		unsigned int generation, int scalePercent, bool eligibleForFeedback ) {
	temporalScaleHistoryEntry_t &entry =
		rg_temporalScaleHistory[rg_temporalScaleHistoryCursor];
	entry.valid = true;
	entry.eligibleForFeedback = eligibleForFeedback;
	entry.frameNumber = frameNumber;
	entry.generation = generation;
	entry.scalePercent = scalePercent;
	rg_temporalScaleHistoryCursor =
		( rg_temporalScaleHistoryCursor + 1 ) % TEMPORAL_SCALE_HISTORY_COUNT;
}

static unsigned long long R_TemporalPresentation_TargetMicroseconds( void ) {
	const float explicitMsec = r_dynamicResolutionTargetMsec.GetFloat();
	if ( explicitMsec > 0.0f ) {
		return static_cast<unsigned long long>( idMath::ClampFloat(
			0.1f, 1000.0f, explicitMsec ) * 1000.0f + 0.5f );
	}

	int presentationFps = 0;
	if ( r_swapInterval.GetInteger() != 0 && glConfig.displayFrequency > 0 ) {
		presentationFps = glConfig.displayFrequency;
	}
	if ( cvarSystem != NULL ) {
		const int maxFps = Max( 0, cvarSystem->GetCVarInteger( "com_maxfps" ) );
		if ( maxFps > 0 ) {
			presentationFps = presentationFps > 0
				? Min( presentationFps, maxFps ) : maxFps;
		}
	}
	if ( presentationFps <= 0 ) {
		presentationFps = glConfig.displayFrequency > 0
			? glConfig.displayFrequency : 60;
	}

	const unsigned long long interval =
		UINT64_C( 1000000 ) / static_cast<unsigned long long>( Max( 1, presentationFps ) );
	return TemporalResolutionCore_PercentOf( interval,
		idMath::ClampInt( 50, 100, r_dynamicResolutionTargetUtilization.GetInteger() ) );
}

const char *R_TemporalPresentation_DecisionName( temporalResolutionDecision_t decision ) {
	switch ( decision ) {
		case TEMPORAL_RESOLUTION_DECISION_DISABLED: return "disabled";
		case TEMPORAL_RESOLUTION_DECISION_FORCED_NATIVE: return "forced-native";
		case TEMPORAL_RESOLUTION_DECISION_FROZEN: return "frozen";
		case TEMPORAL_RESOLUTION_DECISION_WAITING_FOR_SAMPLE: return "waiting";
		case TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET: return "reset";
		case TEMPORAL_RESOLUTION_DECISION_HOLD: return "hold";
		case TEMPORAL_RESOLUTION_DECISION_DROP: return "drop";
		case TEMPORAL_RESOLUTION_DECISION_RAISE: return "raise";
		default: return "unknown";
	}
}

void R_TemporalPresentation_InvalidateHistory( const char *reason ) {
	if ( R_RenderThread_OnRenderThread() ) {
		const int slot = R_TemporalPresentation_FrameSlot();
		if ( ++rg_temporalFrameGenerations[slot] == 0 ) rg_temporalFrameGenerations[slot] = 1;
		idStr::Copynz( rg_temporalFrameResetReasons[slot], reason != NULL ? reason : "backend reset", 128 );
		return;
	}
	rg_temporalHistoryGeneration++;
	if ( rg_temporalHistoryGeneration == 0 ) {
		rg_temporalHistoryGeneration = 1;
	}
	rg_temporalHistoryResetReason =
		( reason != NULL && reason[0] != '\0' ) ? reason : "unspecified";
	R_TemporalPresentation_CountReason( rg_temporalResolveStats.invalidations,
		rg_temporalHistoryResetReason.c_str() );
	R_TemporalPresentation_ClearViewHistory();
}

unsigned int R_TemporalPresentation_HistoryGeneration( void ) {
	return R_RenderThread_OnRenderThread() ? rg_temporalFrameGenerations[R_TemporalPresentation_FrameSlot()] : rg_temporalHistoryGeneration;
}

const char *R_TemporalPresentation_LastHistoryResetReason( void ) {
	return R_RenderThread_OnRenderThread() ? rg_temporalFrameResetReasons[R_TemporalPresentation_FrameSlot()] : rg_temporalHistoryResetReason.c_str();
}

void R_TemporalPresentation_MarkCurrentFrameCapture( const char *reason ) {
	for ( int i = 0; i < TEMPORAL_SCALE_HISTORY_COUNT; ++i ) {
		temporalScaleHistoryEntry_t &entry = rg_temporalScaleHistory[i];
		if ( entry.valid && entry.frameNumber == tr.frameCount ) {
			entry.eligibleForFeedback = false;
		}
	}
	R_TemporalPresentation_CurrentFrame().captureFrozen = true;
	if ( rg_lastCaptureMarkedFrame == tr.frameCount ) {
		return;
	}
	rg_lastCaptureMarkedFrame = tr.frameCount;
	// A synchronous capture can flush a temporal-resolve command that the game
	// queued before it knew a readback was coming.  The backend rejects both
	// history reads and writes while tr.takingScreenshot is live; advance the
	// image-history generation as well so the game's ping-pong owner cannot use
	// the untouched destination as a valid sample on the following frame.
	R_TemporalPresentation_InvalidateHistory(
		( reason != NULL && reason[0] != '\0' ) ? reason : "late capture" );
	if ( r_dynamicResolutionDebug.GetInteger() >= 1 ) {
		common->Printf(
			"Temporal resolution: frame=%d captureScope=%s scale=%d%% feedbackEligible=0\n",
			tr.frameCount,
			( reason != NULL && reason[0] != '\0' ) ? reason : "late-capture",
			R_TemporalPresentation_CurrentFrame().effectiveScalePercent );
	}
}

const char *R_TemporalPresentation_HistoryResetReasonName(
		temporalHistoryResetReason_t reason ) {
	switch ( reason ) {
		case TEMPORAL_HISTORY_RESET_NONE: return "none";
		case TEMPORAL_HISTORY_RESET_FIRST_SAMPLE: return "first-sample";
		case TEMPORAL_HISTORY_RESET_GENERATION: return "generation";
		case TEMPORAL_HISTORY_RESET_VIEW_IDENTITY: return "view-identity";
		case TEMPORAL_HISTORY_RESET_OUTPUT_EXTENT: return "output-extent";
		case TEMPORAL_HISTORY_RESET_SCENE_EXTENT: return "scene-extent";
		case TEMPORAL_HISTORY_RESET_TIME_DISCONTINUITY: return "time-discontinuity";
		case TEMPORAL_HISTORY_RESET_CAMERA_TRANSLATION: return "camera-translation";
		case TEMPORAL_HISTORY_RESET_CAMERA_ROTATION: return "camera-rotation";
		case TEMPORAL_HISTORY_RESET_PROJECTION: return "projection";
		case TEMPORAL_HISTORY_RESET_EXPLICIT_CUT: return "explicit-cut";
		case TEMPORAL_HISTORY_RESET_CAPTURE: return "capture";
		case TEMPORAL_HISTORY_RESET_RESOURCE_LOSS: return "resource-loss";
		default: return "unknown";
	}
}

static unsigned long long R_TemporalPresentation_MixStringIdentity(
		unsigned long long identity, const char *text ) {
	if ( text == NULL ) {
		return TemporalHistoryCore_MixIdentity( identity, 0 );
	}
	unsigned long long value = TemporalHistoryCore_BeginIdentity();
	for ( const unsigned char *cursor =
			reinterpret_cast<const unsigned char *>( text ); *cursor != 0; ++cursor ) {
		value ^= *cursor;
		value *= UINT64_C( 1099511628211 );
	}
	return TemporalHistoryCore_MixIdentity( identity, value );
}

unsigned long long R_TemporalPresentation_ViewIdentity(
		const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return 0;
	}

	unsigned long long identity = TemporalHistoryCore_BeginIdentity();
	identity = TemporalHistoryCore_MixIdentity( identity,
		static_cast<unsigned long long>( reinterpret_cast<std::uintptr_t>(
			viewDef->renderWorld ) ) );
	identity = R_TemporalPresentation_MixStringIdentity( identity,
		viewDef->renderWorld != NULL
			? viewDef->renderWorld->mapName.c_str() : NULL );
	identity = TemporalHistoryCore_MixIdentity( identity,
		static_cast<unsigned int>( viewDef->renderView.viewID ) );
	identity = TemporalHistoryCore_MixIdentity( identity,
		static_cast<unsigned int>( viewDef->renderFlags ) );
	identity = TemporalHistoryCore_MixIdentity( identity,
		( viewDef->isSubview ? 1u : 0u )
		| ( viewDef->isMirror ? 2u : 0u )
		| ( viewDef->isXraySubview ? 4u : 0u ) );
	// each VR eye is its own camera; flat views keep their identity unchanged
	const int vrEye = R_TemporalPresentation_VREye();
	if ( vrEye >= 0 ) {
		identity = TemporalHistoryCore_MixIdentity( identity, 0x56520000u | static_cast<unsigned int>( vrEye ) );
	}

	const viewDef_t *cursor = viewDef;
	int depth = 0;
	while ( cursor != NULL && depth < 16 ) {
		identity = TemporalHistoryCore_MixIdentity( identity,
			static_cast<unsigned int>( cursor->renderView.viewID ) );
		const drawSurf_t *surface = cursor->subviewSurface;
		if ( surface != NULL ) {
			const int entityIndex = surface->space != NULL
				&& surface->space->entityDef != NULL
				? surface->space->entityDef->index : -1;
			identity = TemporalHistoryCore_MixIdentity( identity,
				static_cast<unsigned int>( entityIndex ) );
			identity = TemporalHistoryCore_MixIdentity( identity,
				surface->geo != NULL
					? static_cast<unsigned int>( surface->geo->myID ) : 0u );
			identity = R_TemporalPresentation_MixStringIdentity( identity,
				surface->material != NULL ? surface->material->GetName() : NULL );
		}
		cursor = cursor->superView;
		depth++;
	}
	identity = TemporalHistoryCore_MixIdentity( identity,
		static_cast<unsigned int>( depth ) );
	return identity != 0 ? identity : 1;
}

static temporalViewHistorySlot_t *R_TemporalPresentation_FindViewHistory(
		unsigned long long identity, bool allocate ) {
	temporalViewHistorySlot_t *oldest = &rg_temporalViewHistory[0];
	for ( int i = 0; i < TEMPORAL_VIEW_HISTORY_SLOTS; ++i ) {
		temporalViewHistorySlot_t &slot = rg_temporalViewHistory[i];
		if ( slot.valid && slot.camera.viewIdentity == identity ) {
			return &slot;
		}
		if ( !slot.valid ) {
			oldest = &slot;
			break;
		}
		if ( slot.lastUsedFrame < oldest->lastUsedFrame ) {
			oldest = &slot;
		}
	}
	if ( !allocate ) {
		return NULL;
	}
	memset( oldest, 0, sizeof( *oldest ) );
	return oldest;
}

static void R_TemporalPresentation_BuildCameraState(
		const viewDef_t *viewDef, temporalCameraState_t &camera ) {
	memset( &camera, 0, sizeof( camera ) );
	camera.valid = true;
	camera.viewIdentity = viewDef->temporalViewIdentity;
	camera.historyGeneration = rg_temporalHistoryGeneration;
	camera.frameNumber = tr.frameCount;
	camera.renderTimeMsec = viewDef->renderView.time;

	const int viewWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	const bool usesPresentationExtent = !viewDef->isSubview
		|| ( viewDef->superView != NULL
			&& viewDef->viewport.Equals( viewDef->superView->viewport ) );
	if ( usesPresentationExtent && rg_temporalPresentationInitialized ) {
		camera.outputWidth = R_TemporalPresentation_CurrentFrame().nativeWidth;
		camera.outputHeight = R_TemporalPresentation_CurrentFrame().nativeHeight;
		camera.sceneWidth = R_TemporalPresentation_CurrentFrame().sceneWidth;
		camera.sceneHeight = R_TemporalPresentation_CurrentFrame().sceneHeight;
	} else {
		camera.outputWidth = viewWidth;
		camera.outputHeight = viewHeight;
		camera.sceneWidth = viewWidth;
		camera.sceneHeight = viewHeight;
	}
	memcpy( camera.viewOrigin, viewDef->renderView.vieworg.ToFloatPtr(),
		sizeof( camera.viewOrigin ) );
	for ( int axisIndex = 0; axisIndex < 3; ++axisIndex ) {
		memcpy( camera.viewAxis + axisIndex * 3,
			viewDef->renderView.viewaxis[axisIndex].ToFloatPtr(),
			3 * sizeof( float ) );
	}
	camera.fovX = viewDef->renderView.fov_x;
	camera.fovY = viewDef->renderView.fov_y;
}

void R_TemporalPresentation_PrepareView( viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->temporalPrepared ) {
		return;
	}
	viewDef->temporalPrepared = true;
	viewDef->temporalViewIdentity = R_TemporalPresentation_ViewIdentity( viewDef );
	viewDef->temporalHistoryGeneration = rg_temporalHistoryGeneration;
	viewDef->temporalHistorySlot = R_TemporalPresentation_HistorySlot();
	viewDef->temporalJitterIndex = tr.frameCount & 7;
	viewDef->temporalJitterPixels.Zero();
	viewDef->temporalPreviousViewOrigin.Zero();
	viewDef->temporalPreviousViewAxis.Identity();
	viewDef->temporalPreviousProjectInfo.Zero();
	viewDef->temporalPreviousJitterPixels.Zero();
	viewDef->temporalHistoryResetReason = TEMPORAL_HISTORY_RESET_FIRST_SAMPLE;
	viewDef->temporalJitterEnabled = false;
	viewDef->temporalHistoryValid = false;
	viewDef->temporalCaptureFrame = tr.takingScreenshot;
	viewDef->temporalPreviousProjectionValid = false;

	if ( !R_TemporalPresentation_TemporalAARequested()
			|| viewDef->renderWorld == NULL || viewDef->isEditor
			|| viewDef->isSubview
			|| ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		// Capture-backed portals, mirrors, and remote views do not yet own an
		// independent image-history target.  Keep their projection stable and
		// let the parent temporal pass treat the sampled surface as reactive.
		return;
	}
	if ( tr.takingScreenshot ) {
		// A known screenshot/save-preview frame is rendered without temporal
		// projection jitter because the backend deliberately bypasses history.
		viewDef->temporalHistoryResetReason = TEMPORAL_HISTORY_RESET_CAPTURE;
		rg_temporalResolveStats.viewResets[TEMPORAL_HISTORY_RESET_CAPTURE]++;
		return;
	}

	const temporalJitterSample_t jitter = TemporalHistoryCore_Jitter(
		static_cast<unsigned int>( viewDef->temporalJitterIndex ) );
	const int projectionWidth = viewDef->viewport.x2
		- viewDef->viewport.x1 + 1;
	const int projectionHeight = viewDef->viewport.y2
		- viewDef->viewport.y1 + 1;
	const int sceneWidth = R_TemporalPresentation_CurrentFrame().sceneWidth > 0
		? R_TemporalPresentation_CurrentFrame().sceneWidth : projectionWidth;
	const int sceneHeight = R_TemporalPresentation_CurrentFrame().sceneHeight > 0
		? R_TemporalPresentation_CurrentFrame().sceneHeight : projectionHeight;
	// R_SetupProjection divides these values by its current front-end viewport.
	// Scale the sample so the final offset remains one render-target subpixel
	// whether the target is selected by a front-end crop or by a backend FBO.
	viewDef->temporalJitterPixels.Set(
		jitter.x * static_cast<float>( projectionWidth )
			/ static_cast<float>( Max( 1, sceneWidth ) ),
		jitter.y * static_cast<float>( projectionHeight )
			/ static_cast<float>( Max( 1, sceneHeight ) ) );
	viewDef->temporalJitterEnabled = true;

	temporalCameraState_t current;
	R_TemporalPresentation_BuildCameraState( viewDef, current );
	temporalViewHistorySlot_t *slot = R_TemporalPresentation_FindViewHistory(
		viewDef->temporalViewIdentity, true );
	const temporalCameraState_t previous = slot->camera;
	if ( previous.valid && previous.projectionValid ) {
		viewDef->temporalPreviousViewOrigin.Set(
			previous.viewOrigin[0], previous.viewOrigin[1],
			previous.viewOrigin[2] );
		for ( int axisIndex = 0; axisIndex < 3; ++axisIndex ) {
			viewDef->temporalPreviousViewAxis[axisIndex].Set(
				previous.viewAxis[axisIndex * 3 + 0],
				previous.viewAxis[axisIndex * 3 + 1],
				previous.viewAxis[axisIndex * 3 + 2] );
		}
		viewDef->temporalPreviousProjectInfo.Set(
			previous.projectInfo[0], previous.projectInfo[1],
			previous.projectInfo[2], previous.projectInfo[3] );
		viewDef->temporalPreviousJitterPixels.Set(
			previous.jitterPixels[0], previous.jitterPixels[1] );
		viewDef->temporalPreviousProjectionValid = true;
	}
	viewDef->temporalHistoryResetReason =
		TemporalHistoryCore_ValidateCameraHistory( current, previous,
			TemporalHistoryCore_DefaultCameraPolicy() );
	if ( viewDef->renderView.forceUpdate ) {
		// Camera animations publish authored cuts through the existing render-view
		// force-update bit. This catches visually discontinuous cuts that happen to
		// remain inside the geometric translation/rotation/FOV thresholds.
		viewDef->temporalHistoryResetReason = TEMPORAL_HISTORY_RESET_EXPLICIT_CUT;
	}
	// Root views only reach here (subviews return above); "none" counts reuse.
	rg_temporalResolveStats.viewResets[idMath::ClampInt( 0, TEMPORAL_VIEW_RESET_COUNT - 1,
		static_cast<int>( viewDef->temporalHistoryResetReason ) )]++;
	viewDef->temporalHistoryValid =
		viewDef->temporalHistoryResetReason == TEMPORAL_HISTORY_RESET_NONE
		&& viewDef->temporalPreviousProjectionValid;

	const bool rootCut = !viewDef->isSubview
		&& ( viewDef->temporalHistoryResetReason
			== TEMPORAL_HISTORY_RESET_TIME_DISCONTINUITY
			|| viewDef->temporalHistoryResetReason
				== TEMPORAL_HISTORY_RESET_CAMERA_TRANSLATION
			|| viewDef->temporalHistoryResetReason
				== TEMPORAL_HISTORY_RESET_CAMERA_ROTATION
			|| viewDef->temporalHistoryResetReason
				== TEMPORAL_HISTORY_RESET_PROJECTION
			|| viewDef->temporalHistoryResetReason
				== TEMPORAL_HISTORY_RESET_EXPLICIT_CUT );
	if ( rootCut ) {
		const char *reason = R_TemporalPresentation_HistoryResetReasonName(
			viewDef->temporalHistoryResetReason );
		R_TemporalPresentation_InvalidateHistory( reason );
		viewDef->temporalHistoryGeneration = rg_temporalHistoryGeneration;
		current.historyGeneration = rg_temporalHistoryGeneration;
		viewDef->temporalHistoryValid = false;
		slot = R_TemporalPresentation_FindViewHistory(
			viewDef->temporalViewIdentity, true );
	}

	slot->valid = true;
	slot->lastUsedFrame = tr.frameCount;
	slot->camera = current;
	slot->camera.valid = false;
	slot->camera.projectionValid = false;
}

void R_TemporalPresentation_FinalizeViewProjection( viewDef_t *viewDef ) {
	if ( viewDef == NULL || !viewDef->temporalPrepared
			|| !viewDef->temporalJitterEnabled
			|| viewDef->temporalCaptureFrame ) {
		return;
	}
	temporalViewHistorySlot_t *slot = R_TemporalPresentation_FindViewHistory(
		viewDef->temporalViewIdentity, false );
	if ( slot == NULL || slot->camera.historyGeneration
			!= viewDef->temporalHistoryGeneration ) {
		return;
	}
	slot->camera.projectInfo[0] = viewDef->projectionMatrix[0];
	slot->camera.projectInfo[1] = viewDef->projectionMatrix[5];
	slot->camera.projectInfo[2] = viewDef->projectionMatrix[8];
	slot->camera.projectInfo[3] = viewDef->projectionMatrix[9];
	slot->camera.depthProjection[0] = viewDef->projectionMatrix[10];
	slot->camera.depthProjection[1] = viewDef->projectionMatrix[14];
	slot->camera.jitterPixels[0] = viewDef->temporalJitterPixels.x;
	slot->camera.jitterPixels[1] = viewDef->temporalJitterPixels.y;
	slot->camera.valid = true;
	slot->camera.projectionValid = true;
}

void R_TemporalPresentation_BeginFrame( int nativeWidth, int nativeHeight,
		bool captureFrame ) {
	const advancedScreenSpaceConfig_t advancedScreenSpace =
		R_TemporalPresentation_BuildAdvancedScreenSpaceConfig();
	if ( !rg_temporalPresentationInitialized ) {
		TemporalResolutionCore_Initialize( rg_temporalResolutionState );
		R_TemporalPresentation_ClearScaleHistory();
		memset( &R_TemporalPresentation_CurrentFrame(), 0, sizeof( R_TemporalPresentation_CurrentFrame() ) );
		rg_temporalPresentationInitialized = true;
		rg_dynamicResolutionEnabledLastFrame = r_rendererDynamicResolution.GetBool();
		rg_temporalAAEnabledLastFrame = R_TemporalPresentation_TemporalAAWanted();
		rg_advancedScreenSpaceLast = advancedScreenSpace;
		rg_videoRestartCount = tr.videoRestartCount;
	}

	const bool dynamicResolutionRequested = r_rendererDynamicResolution.GetBool()
		&& !R_TemporalPresentation_VRFrame();
	const bool temporalAARequested = R_TemporalPresentation_TemporalAAWanted();
	if ( dynamicResolutionRequested != rg_dynamicResolutionEnabledLastFrame ) {
		R_RendererMetrics_ResetGpuFrameTiming( dynamicResolutionRequested
			? "dynamic resolution enabled" : "dynamic resolution disabled" );
		R_TemporalPresentation_ClearScaleHistory();
		TemporalResolutionCore_Initialize( rg_temporalResolutionState );
		R_TemporalPresentation_InvalidateHistory( dynamicResolutionRequested
			? "dynamic resolution enabled" : "dynamic resolution disabled" );
		rg_dynamicResolutionEnabledLastFrame = dynamicResolutionRequested;
	}
	if ( temporalAARequested != rg_temporalAAEnabledLastFrame ) {
		R_TemporalPresentation_InvalidateHistory( temporalAARequested
			? "temporal AA enabled" : "temporal AA disabled" );
		rg_temporalAAEnabledLastFrame = temporalAARequested;
	}
	if ( !R_TemporalPresentation_AdvancedScreenSpaceEqual(
			advancedScreenSpace, rg_advancedScreenSpaceLast ) ) {
		R_TemporalPresentation_InvalidateHistory(
			"advanced screen-space configuration changed" );
		rg_advancedScreenSpaceLast = advancedScreenSpace;
	}
	if ( tr.videoRestartCount != rg_videoRestartCount ) {
		R_TemporalPresentation_ClearScaleHistory();
		TemporalResolutionCore_Initialize( rg_temporalResolutionState );
		R_TemporalPresentation_InvalidateHistory( "video restart" );
		rg_videoRestartCount = tr.videoRestartCount;
	}

	renderGpuFrameTiming_t gpuTiming;
	R_RendererMetrics_GetGpuFrameTiming( gpuTiming );

	// a VR frame's eye resolution already carries vr_renderScale
	const int manualScalePercent = R_TemporalPresentation_VRFrame() ? 100
		: idMath::ClampInt( 10, 200, r_screenFraction.GetInteger() );
	int maximumScalePercent = idMath::ClampInt(
		10, 100, r_dynamicResolutionMaxScale.GetInteger() );
	if ( manualScalePercent < 100 ) {
		maximumScalePercent = Min( maximumScalePercent, manualScalePercent );
	}
	const int minimumScalePercent = Min( maximumScalePercent,
		idMath::ClampInt( 10, 100, r_dynamicResolutionMinScale.GetInteger() ) );

	temporalResolutionConfig_t config = {};
	config.enabled = dynamicResolutionRequested;
	config.minimumScalePercent = minimumScalePercent;
	config.maximumScalePercent = maximumScalePercent;
	config.dropStepPercent = r_dynamicResolutionDropStep.GetInteger();
	config.raiseStepPercent = r_dynamicResolutionRaiseStep.GetInteger();
	config.raiseFrames = r_dynamicResolutionRaiseFrames.GetInteger();
	config.raiseThresholdPercent = r_dynamicResolutionRaiseThreshold.GetInteger();
	config.missingSampleResetFrames = r_dynamicResolutionMissingSampleReset.GetInteger();
	config.maximumSampleAgeFrames = r_dynamicResolutionMaxSampleAge.GetInteger();
	config.targetMicroseconds = R_TemporalPresentation_TargetMicroseconds();
	config.dimensionAlignment = r_dynamicResolutionAlignment.GetInteger();

	temporalResolutionSample_t sample = {};
	sample.supported = gpuTiming.supported;
	sample.valid = gpuTiming.valid;
	sample.backend = static_cast<int>( gpuTiming.backend );
	sample.frameNumber = gpuTiming.frameNumber;
	sample.generation = gpuTiming.generation;
	sample.elapsedMicroseconds = gpuTiming.elapsedMicroseconds;
	sample.scalePercent = gpuTiming.valid
		? R_TemporalPresentation_FindSampleScale(
			gpuTiming.frameNumber, gpuTiming.generation ) : 0;
	sample.ageFrames = gpuTiming.valid
		? R_TemporalPresentation_FrameAge( tr.frameCount, gpuTiming.frameNumber ) : 0;

	const bool forceNative = captureFrame && r_dynamicResolutionCaptureNative.GetBool();
	temporalResolutionOutput_t output = TemporalResolutionCore_Update(
		rg_temporalResolutionState, config, sample,
		nativeWidth, nativeHeight, captureFrame, forceNative );
	if ( !dynamicResolutionRequested ) {
		output.scalePercent = manualScalePercent;
		output.width = Max( 1, static_cast<int>(
			( static_cast<long long>( Max( 0, nativeWidth ) ) * manualScalePercent + 50 ) / 100 ) );
		output.height = Max( 1, static_cast<int>(
			( static_cast<long long>( Max( 0, nativeHeight ) ) * manualScalePercent + 50 ) / 100 ) );
	}

	const int previousScale = rg_temporalLastScalePercent;
	rg_temporalLastScalePercent = output.scalePercent;
	memset( &R_TemporalPresentation_CurrentFrame(), 0, sizeof( R_TemporalPresentation_CurrentFrame() ) );
	R_TemporalPresentation_CurrentFrame().dynamicResolutionRequested = dynamicResolutionRequested;
	R_TemporalPresentation_CurrentFrame().dynamicResolutionActive = dynamicResolutionRequested
		&& gpuTiming.supported;
	R_TemporalPresentation_CurrentFrame().temporalAARequested = temporalAARequested;
	R_TemporalPresentation_CurrentFrame().captureFrozen = captureFrame && !forceNative;
	R_TemporalPresentation_CurrentFrame().captureForcedNative = forceNative;
	R_TemporalPresentation_CurrentFrame().timingSupported = gpuTiming.supported;
	R_TemporalPresentation_CurrentFrame().timingValid = gpuTiming.valid;
	R_TemporalPresentation_CurrentFrame().frameNumber = tr.frameCount;
	R_TemporalPresentation_CurrentFrame().nativeWidth = nativeWidth;
	R_TemporalPresentation_CurrentFrame().nativeHeight = nativeHeight;
	R_TemporalPresentation_CurrentFrame().sceneWidth = output.width;
	R_TemporalPresentation_CurrentFrame().sceneHeight = output.height;
	R_TemporalPresentation_CurrentFrame().manualScalePercent = manualScalePercent;
	R_TemporalPresentation_CurrentFrame().effectiveScalePercent = output.scalePercent;
	R_TemporalPresentation_CurrentFrame().sampledScalePercent = sample.scalePercent;
	R_TemporalPresentation_CurrentFrame().timingFrameNumber = gpuTiming.frameNumber;
	R_TemporalPresentation_CurrentFrame().timingAgeFrames = sample.ageFrames;
	R_TemporalPresentation_CurrentFrame().timingBackend = static_cast<int>( gpuTiming.backend );
	R_TemporalPresentation_CurrentFrame().timingGeneration = gpuTiming.generation;
	R_TemporalPresentation_CurrentFrame().timingMicroseconds = gpuTiming.elapsedMicroseconds;
	R_TemporalPresentation_CurrentFrame().targetMicroseconds = config.targetMicroseconds;
	R_TemporalPresentation_CurrentFrame().temporalFeedback = idMath::ClampFloat(
		0.0f, 0.98f, r_temporalAAFeedback.GetFloat() );
	R_TemporalPresentation_CurrentFrame().temporalReactiveScale = idMath::ClampFloat(
		0.0f, 2.0f, r_temporalAAReactiveScale.GetFloat() );
	R_TemporalPresentation_CurrentFrame().temporalDebugMode = idMath::ClampInt(
		0, 3, r_temporalAADebug.GetInteger() );
	R_TemporalPresentation_CurrentFrame().advancedScreenSpace = advancedScreenSpace;
	R_TemporalPresentation_CurrentFrame().decision = output.decision;

	R_TemporalPresentation_RecordFrameScale( tr.frameCount,
		gpuTiming.generation, output.scalePercent, !captureFrame );

	const int debug = r_dynamicResolutionDebug.GetInteger();
	if ( debug >= 2 || ( debug >= 1
			&& ( previousScale != output.scalePercent
				|| output.decision == TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET ) ) ) {
		common->Printf(
			"Temporal resolution: frame=%d decision=%s scale=%d%% scene=%dx%d output=%dx%d gpu=%lluus target=%lluus sampleFrame=%d sampleScale=%d%% age=%d generation=%u capture=%d\n",
			R_TemporalPresentation_CurrentFrame().frameNumber,
			R_TemporalPresentation_DecisionName( output.decision ),
			output.scalePercent, output.width, output.height,
			nativeWidth, nativeHeight,
			gpuTiming.elapsedMicroseconds, config.targetMicroseconds,
			gpuTiming.frameNumber, sample.scalePercent, sample.ageFrames,
			gpuTiming.generation, captureFrame ? 1 : 0 );
	}
}

const temporalPresentationFrameState_t &R_TemporalPresentation_GetFrameState( void ) {
	return R_TemporalPresentation_CurrentFrame();
}

int R_TemporalPresentation_EffectiveScreenFraction( void ) {
	if ( !rg_temporalPresentationInitialized
			|| R_TemporalPresentation_CurrentFrame().effectiveScalePercent <= 0 ) {
		return idMath::ClampInt( 10, 200, r_screenFraction.GetInteger() );
	}
	return R_TemporalPresentation_CurrentFrame().effectiveScalePercent;
}

bool R_TemporalPresentation_DynamicResolutionRequested( void ) {
	return r_rendererDynamicResolution.GetBool() && !R_TemporalPresentation_VRFrame();
}

// r_temporalAA 2 hands sub-native rendering to TAAU, which reconstructs far
// more detail than a bilinear stretch. It stays off at native resolution
// (where MSAA/SMAA already serve), for the crop and nearest scale modes a
// player picks on purpose, and on backends without a temporal resolve.
static bool R_TemporalPresentation_AutomaticTemporalAAWanted( void ) {
#if defined( OPENQ4_RENDERER_GLES_MODULE )
	return false;
#else
#if !defined( OPENQ4_RENDERER_VK_MODULE )
	if ( !glConfig.GLSLProgramAvailable || !glConfig.GLSL130Available ) {
		return false;
	}
#endif
	// a VR frame renders native eyes without dynamic resolution
	if ( R_TemporalPresentation_VRFrame() ) {
		return false;
	}
	if ( r_rendererDynamicResolution.GetBool() ) {
		return true;
	}
	const int scaleMode = r_resolutionScaleMode.GetInteger();
	return r_screenFraction.GetInteger() < 100 && ( scaleMode == 1 || scaleMode == 2 );
#endif
}

static bool R_TemporalPresentation_TemporalAAWanted( void ) {
	switch ( r_temporalAA.GetInteger() ) {
		case 1:
			return true;
		case 2:
			return R_TemporalPresentation_AutomaticTemporalAAWanted();
		default:
			return false;
	}
}

bool R_TemporalPresentation_TemporalAARequested( void ) {
	return R_TemporalPresentation_TemporalAAWanted();
}

float R_TemporalPresentation_EffectReactiveStrength( void ) {
	return idMath::ClampFloat( 0.0f, 1.0f, r_temporalAAReactiveEffects.GetFloat() );
}

float R_TemporalPresentation_PresentSharpness( void ) {
	return idMath::ClampFloat( 0.0f, 1.0f, r_temporalAASharpness.GetFloat() );
}

static temporalOwnershipStats_t rg_temporalOwnershipStats = { -1, 0, 0, 0, 0, 0, false };

void R_TemporalPresentation_RecordOwnership( const temporalOwnershipStats_t &stats ) {
	rg_temporalOwnershipStats = stats;
}

void R_TemporalPresentation_RecordResolve( temporalResolveOutcome_t outcome,
		const char *fallbackReason, int velocity ) {
	if ( outcome < 0 || outcome >= TEMPORAL_RESOLVE_OUTCOME_COUNT ) {
		return;
	}
	rg_temporalResolveStats.outcomes[outcome]++;
	rg_temporalResolveStats.velocity[idMath::ClampInt( -1, 2, velocity ) + 1]++;
	if ( outcome == TEMPORAL_RESOLVE_FALLBACK ) {
		R_TemporalPresentation_CountReason( rg_temporalResolveStats.fallbacks, fallbackReason );
	}
}

static void R_TemporalPresentation_PrintReasons( const char *label, const temporalReasonCount_t *slots ) {
	idStr line;
	for ( int i = 0; i < TEMPORAL_STATS_REASON_SLOTS && slots[i].count > 0; i++ ) {
		line += va( "%s\"%s\"=%d", line.Length() > 0 ? " " : "", slots[i].reason, slots[i].count );
	}
	common->Printf( "%s: %s\n", label, line.Length() > 0 ? line.c_str() : "none" );
}

static motionBlurStats_t rg_motionBlurStats;
static bool rg_motionBlurStatsInitialized = false;

void R_TemporalPresentation_ResetMotionBlurStats( void ) {
	memset( &rg_motionBlurStats, 0, sizeof( rg_motionBlurStats ) );
	rg_motionBlurStats.lastFrame = -1;
	rg_motionBlurStats.lastShutterScale = 1.0f;
	rg_motionBlurStats.lastDisabledReason = "none";
	rg_motionBlurStatsInitialized = true;
}

motionBlurStats_t &R_TemporalPresentation_MotionBlurStats( void ) {
	if ( !rg_motionBlurStatsInitialized ) {
		R_TemporalPresentation_ResetMotionBlurStats();
	}
	return rg_motionBlurStats;
}

float R_TemporalPresentation_MotionBlurShutterMsec( void ) {
	return idMath::ClampFloat( 0.0f, 100.0f, r_motionBlurShutterMsec.GetFloat() );
}

bool R_TemporalPresentation_PreviousPositionsRequested( void ) {
	if ( R_TemporalPresentation_TemporalAARequested() ) {
		return true;
	}
	return r_motionBlur.GetBool() && r_motionBlurObjectVectors.GetBool()
		&& !r_skipPostProcess.GetBool() && !R_TemporalPresentation_VRFrame();
}

void R_TemporalPresentation_PrintMotionBlurStatus_f( const idCmdArgs &args ) {
	R_RenderThread_Sync( "motion blur stats" );
	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "reset" ) == 0 ) {
		R_TemporalPresentation_ResetMotionBlurStats();
		common->Printf( "Motion blur: statistics reset\n" );
		return;
	}
	const motionBlurStats_t &stats = R_TemporalPresentation_MotionBlurStats();
	common->Printf(
		"Motion blur: requested=%d objectVectors=%d shutterMsec=%.3f taaRequested=%d mainViews=%d drawn=%d objectVectorViews=%d jitterAligned=%d subviewSkips=%d disabled=%d lastDisabled=%s\n",
		r_motionBlur.GetBool() ? 1 : 0,
		r_motionBlurObjectVectors.GetBool() ? 1 : 0,
		R_TemporalPresentation_MotionBlurShutterMsec(),
		R_TemporalPresentation_TemporalAARequested() ? 1 : 0,
		stats.mainViews, stats.drawnViews, stats.objectVectorViews,
		stats.jitterAlignedViews, stats.subviewSkips, stats.disabledViews,
		stats.lastDisabledReason != NULL ? stats.lastDisabledReason : "none" );
	idStr history;
	for ( int i = 0; i < MOTION_BLUR_HISTORY_RESULT_COUNT; i++ ) {
		history += va( "%s%s=%d", i > 0 ? " " : "",
			MotionBlurCore_HistoryResultName( static_cast<motionBlurHistoryResult_t>( i ) ),
			stats.history[i] );
	}
	common->Printf( "Motion blur history: %s\n", history.c_str() );
	common->Printf(
		"Motion blur ownership: rigid=%d posed=%d weapon=%d weaponStill=%d cameraFallback=%d\n",
		stats.rigid, stats.posed, stats.weapon, stats.weaponStill, stats.cameraFallback );
	common->Printf(
		"Motion blur effective: frame=%d strength=%.3f maxPixels=%.1f samples=%d shutterScale=%.3f frameMsec=%.3f\n",
		stats.lastFrame, stats.lastStrength, stats.lastMaxPixels, stats.lastSamples,
		stats.lastShutterScale, stats.lastFrameMsec );
	const float centerSamples = static_cast<float>( Max( 1, stats.centerSamples ) );
	common->Printf(
		"Motion blur camera: samples=%d centerPixelsPerFrame=%.4f scaledCenterPixels=%.4f\n",
		stats.centerSamples, stats.centerPixelsSum / centerSamples,
		stats.scaledCenterPixelsSum / centerSamples );
}

void R_TemporalPresentation_CapturePreviousPositions( srfTriangles_t *tri ) {
	if ( tri == NULL ) {
		return;
	}
	const int frame = tr.frameCount;
	if ( tri->positionsFrame == frame ) {
		// Posed again this frame (collision instantiation, uncached views): the
		// capture taken by the first pose still describes the previous frame.
		return;
	}
	// verts must hold the pose that was drawn on the immediately preceding
	// frame, or the "previous" positions would span a gap.
	const bool continuous = tri->verts != NULL && tri->numVerts > 0
		&& tri->positionsFrame == frame - 1;
	if ( !continuous || !R_TemporalPresentation_PreviousPositionsRequested() ) {
		tri->previousPositionsFrame = -1;
		return;
	}
	if ( tri->previousPositions == NULL || tri->numPreviousPositions != tri->numVerts ) {
		if ( tri->previousPositions != NULL ) {
			Mem_Free16( tri->previousPositions );
		}
		tri->previousPositions = static_cast<idVec3 *>(
			Mem_Alloc16( static_cast<size_t>( tri->numVerts ) * sizeof( idVec3 ) ) );
		tri->numPreviousPositions = tri->previousPositions != NULL ? tri->numVerts : 0;
		if ( tri->previousPositions == NULL ) {
			tri->previousPositionsFrame = -1;
			return;
		}
	}
	for ( int i = 0; i < tri->numVerts; i++ ) {
		tri->previousPositions[i] = tri->verts[i].xyz;
	}
	tri->previousPositionsFrame = frame;
}

void R_TemporalPresentation_MarkPositionsDrawn( srfTriangles_t *tri ) {
	if ( tri != NULL ) {
		tri->positionsFrame = tr.frameCount;
	}
}

struct vertCache_s *R_TemporalPresentation_PreviousPositionCache(
		const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->geo == NULL || drawSurf->space == NULL
			|| drawSurf->space->entityDef == NULL || drawSurf->material == NULL
			|| tr.viewDef == NULL || tr.viewDef->isSubview
			|| !R_TemporalPresentation_PreviousPositionsRequested() ) {
		return NULL;
	}
	const srfTriangles_t *tri = drawSurf->geo;
	if ( tri->previousPositions == NULL || tri->previousPositionsFrame != tr.frameCount
			|| tri->numVerts <= 0 || tri->numPreviousPositions != tri->numVerts
			|| !drawSurf->material->IsDrawn()
			|| drawSurf->material->Coverage() == MC_TRANSLUCENT ) {
		return NULL;
	}
	return vertexCache.AllocFrameTemp( tri->previousPositions,
		tri->numVerts * static_cast<int>( sizeof( idVec3 ) ) );
}

void R_TemporalPresentation_SetVRTargetState( int width, int height, bool temporalAA ) {
	R_TemporalPresentation_CurrentFrame().nativeWidth = Max( 1, width );
	R_TemporalPresentation_CurrentFrame().nativeHeight = Max( 1, height );
	R_TemporalPresentation_CurrentFrame().sceneWidth = R_TemporalPresentation_CurrentFrame().nativeWidth;
	R_TemporalPresentation_CurrentFrame().sceneHeight = R_TemporalPresentation_CurrentFrame().nativeHeight;
	R_TemporalPresentation_CurrentFrame().manualScalePercent = 100;
	R_TemporalPresentation_CurrentFrame().effectiveScalePercent = 100;
	R_TemporalPresentation_CurrentFrame().dynamicResolutionRequested = false;
	R_TemporalPresentation_CurrentFrame().dynamicResolutionActive = false;
	R_TemporalPresentation_CurrentFrame().temporalAARequested = temporalAA;
}

void R_TemporalPresentation_SetVRExtent( int width, int height ) {
	// an eye resolves temporal AA from its own history (VR-05); the virtual
	// screen carries no 3D scene to resolve
	R_TemporalPresentation_SetVRTargetState( width, height,
		R_TemporalPresentation_VREye() >= 0 && R_TemporalPresentation_TemporalAAWanted() );
}

bool R_TemporalPresentation_ScreenSpaceEffectsRequested( void ) {
	return AdvancedScreenSpaceCore_Requested(
		R_TemporalPresentation_BuildAdvancedScreenSpaceConfig() );
}

void R_TemporalPresentation_PrintStatus_f( const idCmdArgs &args ) {
	R_RenderThread_Sync( "temporal stats" );
	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "reset" ) == 0 ) {
		memset( &rg_temporalResolveStats, 0, sizeof( rg_temporalResolveStats ) );
		common->Printf( "Temporal presentation: cumulative statistics reset\n" );
		return;
	}
	const temporalPresentationFrameState_t &state =
		R_TemporalPresentation_GetFrameState();
	common->Printf(
		"Temporal presentation: dynamicRequested=%d dynamicActive=%d taaRequested=%d screenEffects=0x%x froxel={density=%.6f distance=%.0f slices=%d} ssr={intensity=%.2f distance=%.0f steps=%d} ssgi={intensity=%.2f taps=%d} frame=%d decision=%s manualScale=%d%% effectiveScale=%d%% scene=%dx%d output=%dx%d target=%lluus feedback=%.3f reactiveScale=%.3f debug=%d timingSupported=%d timingValid=%d timingBackend=%d timingFrame=%d timingScale=%d%% timingAge=%d timingGeneration=%u timing=%lluus captureFrozen=%d captureForcedNative=%d historyGeneration=%u historyReset=%s processed=%llu rejected=%llu drops=%llu raises=%llu resets=%llu\n",
		state.dynamicResolutionRequested ? 1 : 0,
		state.dynamicResolutionActive ? 1 : 0,
		state.temporalAARequested ? 1 : 0,
		state.advancedScreenSpace.effectMask,
		state.advancedScreenSpace.froxelDensity,
		state.advancedScreenSpace.froxelMaxDistance,
		state.advancedScreenSpace.froxelSlices,
		state.advancedScreenSpace.ssrIntensity,
		state.advancedScreenSpace.ssrMaxDistance,
		state.advancedScreenSpace.ssrSteps,
		state.advancedScreenSpace.ssgiIntensity,
		ADVANCED_SCREEN_SPACE_SSGI_TAPS,
		state.frameNumber,
		R_TemporalPresentation_DecisionName( state.decision ),
		state.manualScalePercent,
		state.effectiveScalePercent,
		state.sceneWidth, state.sceneHeight,
		state.nativeWidth, state.nativeHeight,
		state.targetMicroseconds,
		state.temporalFeedback,
		state.temporalReactiveScale,
		state.temporalDebugMode,
		state.timingSupported ? 1 : 0,
		state.timingValid ? 1 : 0,
		state.timingBackend,
		state.timingFrameNumber,
		state.sampledScalePercent,
		state.timingAgeFrames,
		state.timingGeneration,
		state.timingMicroseconds,
		state.captureFrozen ? 1 : 0,
		state.captureForcedNative ? 1 : 0,
		rg_temporalHistoryGeneration,
		rg_temporalHistoryResetReason.c_str(),
		rg_temporalResolutionState.processedSamples,
		rg_temporalResolutionState.rejectedSamples,
		rg_temporalResolutionState.droppedScaleChanges,
		rg_temporalResolutionState.raisedScaleChanges,
		rg_temporalResolutionState.discontinuityResets );
	common->Printf(
		"Temporal ownership: frame=%d rigid=%d posed=%d weapon=%d reactive=%d missed=%d complete=%d\n",
		rg_temporalOwnershipStats.frameNumber,
		rg_temporalOwnershipStats.rigid,
		rg_temporalOwnershipStats.posed,
		rg_temporalOwnershipStats.weapon,
		rg_temporalOwnershipStats.reactive,
		rg_temporalOwnershipStats.missed,
		rg_temporalOwnershipStats.complete ? 1 : 0 );
	const temporalResolveStats_t &resolve = rg_temporalResolveStats;
	common->Printf(
		"Temporal resolve: history=%d seeded=%d capture=%d spatial=%d fallback=%d velocityNotAttempted=%d velocityNone=%d velocityPartial=%d velocityComplete=%d\n",
		resolve.outcomes[TEMPORAL_RESOLVE_HISTORY], resolve.outcomes[TEMPORAL_RESOLVE_SEEDED],
		resolve.outcomes[TEMPORAL_RESOLVE_CAPTURE], resolve.outcomes[TEMPORAL_RESOLVE_SPATIAL],
		resolve.outcomes[TEMPORAL_RESOLVE_FALLBACK], resolve.velocity[0], resolve.velocity[1],
		resolve.velocity[2], resolve.velocity[3] );
	idStr viewResets;
	for ( int i = 0; i < TEMPORAL_VIEW_RESET_COUNT; i++ ) {
		viewResets += va( "%s%s=%d", i > 0 ? " " : "",
			R_TemporalPresentation_HistoryResetReasonName( static_cast<temporalHistoryResetReason_t>( i ) ),
			resolve.viewResets[i] );
	}
	common->Printf( "Temporal view history: %s\n", viewResets.c_str() );
	R_TemporalPresentation_PrintReasons( "Temporal resolve fallbacks", resolve.fallbacks );
	R_TemporalPresentation_PrintReasons( "Temporal history invalidations", resolve.invalidations );
	if ( resolve.otherReasons > 0 ) {
		common->Printf( "Temporal reasons beyond the table: %d\n", resolve.otherReasons );
	}
#ifdef OPENQ4_RENDERER_VK_MODULE
	extern void VK_PostProcess_PrintTemporalMotion( void );
	VK_PostProcess_PrintTemporalMotion();
#endif
}

void R_TemporalPresentation_SubmitFrame( void ) {
	const int slot = R_TemporalPresentation_FrameSlot();
	rg_temporalFrameGenerations[slot] = rg_temporalHistoryGeneration;
	rg_temporalFrameResetReasons[slot][0] = '\0';
}

void R_TemporalPresentation_RetireFrame( void ) {
	const int slot = R_TemporalPresentation_FrameSlot();
	if ( rg_temporalFrameResetReasons[slot][0] != '\0' ) {
		R_TemporalPresentation_InvalidateHistory( rg_temporalFrameResetReasons[slot] );
		rg_temporalFrameResetReasons[slot][0] = '\0';
	}
}
