// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VOLUMETRIC_LIGHTING_H__
#define __VOLUMETRIC_LIGHTING_H__

/*
===============================================================================

	Shadowed volumetric lighting (REN-09).

	The froxel leaf (r_rendererFroxelVolumetrics) scatters light from the
	view's interaction lights through a homogeneous medium. Each admitted light
	stage adds one scissored pass, run while the light's own shadow map is
	still bound: a jittered march from the eye to the opaque depth, clipped to
	the light's bounds, that evaluates the stage's projection and falloff
	images and the light's shadow map at every step. The passes add into one
	scattering target, which is composited over the opaque scene before fog,
	blend lights and translucent surfaces, so glass, particles and BSE effects
	sit in front of the medium instead of under it.

	Lights drawn with stencil shadows have no map to sample. By default they
	add no scattering at all (r_volumetricStencilLights 0), so a beam never
	leaks through a wall the stencil volume would have shadowed; 1 lets them
	scatter unshadowed. Fog, blend and ambient lights never scatter: stock fog
	and blend lights keep their classic look.

	The leaf is off by default, follows the r_rendererModernQuality master and
	leaves the frame byte-identical when either is off. Both backends share
	this policy, the light records and the status report.

===============================================================================
*/

class idImage;
struct viewDef_s;
struct viewLight_s;

const int VOLUMETRIC_MAX_STEPS = 16;
const int VOLUMETRIC_MAX_LIGHT_STAGES = 4;

typedef enum volumetricLightOutcome_e {
	VOLUMETRIC_LIGHT_SHADOW_MAPPED,		// scattered, shadowed by its own map
	VOLUMETRIC_LIGHT_UNSHADOWED,		// scattered, the light casts no shadows
	VOLUMETRIC_LIGHT_STENCIL_UNSHADOWED,	// scattered unshadowed by r_volumetricStencilLights 1
	VOLUMETRIC_LIGHT_SKIPPED_STENCIL,	// stencil-shadowed, r_volumetricStencilLights 0
	VOLUMETRIC_LIGHT_SKIPPED_NO_MAP,	// its shadow map failed this view
	VOLUMETRIC_LIGHT_SKIPPED_DISTANCE,	// entirely past r_froxelVolumetricMaxDistance
	VOLUMETRIC_LIGHT_SKIPPED_BUDGET,	// past r_volumetricMaxLights
	VOLUMETRIC_LIGHT_SKIPPED_NO_STAGE,	// no enabled stage with visible colour
	VOLUMETRIC_LIGHT_SKIPPED_FAILED,	// the backend could not draw the pass
	VOLUMETRIC_LIGHT_OUTCOME_COUNT
} volumetricLightOutcome_t;

typedef enum volumetricShadowKind_e {
	VOLUMETRIC_SHADOW_NONE,
	VOLUMETRIC_SHADOW_PROJECTED,	// atlas tile(s), cascades for sunlight
	VOLUMETRIC_SHADOW_POINT		// depth cube
} volumetricShadowKind_t;

// Settings for one view; constant across its lights.
typedef struct volumetricViewSettings_s {
	bool		requested;		// leaf and master on
	bool		active;			// requested and this view may run it
	const char *	declineReason;		// why requested but not active, else ""
	float		extinction;		// per world unit
	float		scattering;		// in-scattering per world unit
	float		anisotropy;		// Henyey-Greenstein g
	float		maxDistance;
	int		steps;
	int		resolutionDivisor;	// 1 full, 2 half
	int		stencilPolicy;		// 0 skip, 1 unshadowed
	int		maxLights;		// 0 unlimited
	float		noiseOffset;		// per-frame march offset under TAA, else 0
	int		debug;			// 1 show the scattering alone, 2 the same with no shadow term
} volumetricViewSettings_t;

// One light stage in world space, as both backends upload it.
typedef struct volumetricLightStage_s {
	idImage *	image;
	float		projectS[4];
	float		projectT[4];
	float		projectQ[4];
	float		falloff[4];
	float		color[4];		// rgb scattering colour, a unused
} volumetricLightStage_t;

typedef struct volumetricLightRecord_s {
	int		stageCount;
	volumetricLightStage_t stages[VOLUMETRIC_MAX_LIGHT_STAGES];
	float		origin[4];		// xyz globalLightOrigin; w 1 local light, 0 parallel
	float		boundsMin[4];		// world bounds of the light volume
	float		boundsMax[4];
	idImage *	falloffImage;
} volumetricLightRecord_t;

// The shadow map a light's receivers sampled, in world space. Projected
// lights (and parallel sunlight, cascaded) use rows/atlasRects/splitDepths;
// point lights the radial depth cube.
const int VOLUMETRIC_MAX_CASCADES = 4;
typedef struct volumetricShadowParams_s {
	volumetricShadowKind_t kind;
	idImage *	image;			// OpenGL: the depth texture or cube to sample raw
	int		cascadeCount;
	float		rows[VOLUMETRIC_MAX_CASCADES][4][4];	// clip rows S, T, depth, W per cascade
	float		atlasRects[VOLUMETRIC_MAX_CASCADES][4];	// u0, v0, u1, v1 in the bound texture
	float		splitDepths[VOLUMETRIC_MAX_CASCADES];	// view depth of each interior split
	float		depthBias[VOLUMETRIC_MAX_CASCADES];
	float		pointFar;
	float		pointBias;
	float		pointDepthMode;		// 1 raw red depth, 0 packed rg (OpenGL colour cube)
} volumetricShadowParams_t;

typedef struct volumetricViewStats_s {
	int		frame;
	bool		subview;		// this view is a mirror, remote camera or other subview
	int		views;			// views that scattered and composited this frame
	int		subviews;		// of those, subviews
	int		viewsDeclined;		// requested views the leaf declined this frame
	int		lights;
	int		passes;
	int		outcomes[VOLUMETRIC_LIGHT_OUTCOME_COUNT];
	int		width;
	int		height;
	bool		composited;
	const char *	declineReason;
} volumetricViewStats_t;

bool		R_Volumetric_Requested();
// Fills the settings of view; backendDecline names a backend limit (an
// unsupported tier, a missing target or shader) and leaves it inactive.
void		R_Volumetric_BuildViewSettings( const struct viewDef_s *view,
			const char *backendDecline, volumetricViewSettings_t &settings );
// The inverse view-projection (OpenGL clip space to world, column-major),
// the eye and the view's forward axis. False for a singular projection.
bool		R_Volumetric_ViewTransforms( const struct viewDef_s *view, float clipToWorld[16],
			float eye[4], float forward[4] );
// Builds the world-space stage records of vLight. False when no enabled stage
// has visible colour. overBright is the scale the frame applies after the
// interactions (backEnd.overBright on GL, 1 where none follows).
bool		R_Volumetric_BuildLightRecord( const struct viewLight_s *vLight,
			float lightScale, float overBright, volumetricLightRecord_t &record );
// The policy of a light that has stages: shadow is the map the backend can
// sample for it now (NONE for none), stencilShadowed true when its shadows
// are stencil volumes. budgetUsed counts the lights already scattered.
volumetricLightOutcome_t R_Volumetric_ClassifyLight( const volumetricViewSettings_t &settings,
			const struct viewDef_s *view, const volumetricLightRecord_t &record,
			volumetricShadowKind_t shadow, bool stencilShadowed, bool mapExpected,
			int budgetUsed );
// Rows, splits and bias of a projected light's map; composedRects are its
// cascade rectangles in the texture the backend binds.
void		R_Volumetric_FillProjectedShadow( const struct shadowMapProjectedLightState_s &state,
			const float composedRects[][4], volumetricShadowParams_t &params );
bool		R_Volumetric_OutcomeScatters( volumetricLightOutcome_t outcome );
// Scissor-sized half or full resolution extent of a width x height scene.
void		R_Volumetric_TargetExtent( const volumetricViewSettings_t &settings,
			int sceneWidth, int sceneHeight, int &width, int &height );
// Packs the per-view constants both shaders read:
// params0 = extinction, scattering, anisotropy, maxDistance
// params1 = steps, noiseOffset, debug, resolutionDivisor
void		R_Volumetric_PackViewParams( const volumetricViewSettings_t &settings,
			float params0[4], float params1[4] );

// The scratch targets of a view: its depth copy and its scattering target.
// The main view, capture-backed mirrors and remote cameras differ in size, and
// one pair resized for each of them is reallocated several times a frame, so
// each backend keeps a few pairs and a view takes the pair of its own size.
const int VOLUMETRIC_TARGET_SLOTS = 4;
typedef struct volumetricTargetPool_s {
	int		sceneWidth[VOLUMETRIC_TARGET_SLOTS];
	int		sceneHeight[VOLUMETRIC_TARGET_SLOTS];
	int		divisor[VOLUMETRIC_TARGET_SLOTS];
	int		lastUse[VOLUMETRIC_TARGET_SLOTS];	// 0 never used
	int		clock;
} volumetricTargetPool_t;
// The slot holding targets of this size, else the least recently used one,
// which the backend then resizes.
int		R_Volumetric_ChooseTargetSlot( volumetricTargetPool_t &pool, int sceneWidth, int sceneHeight, int divisor );

// Status: each backend records the views of its last frame.
void		R_Volumetric_BeginViewStats( volumetricViewStats_t &stats, const volumetricViewSettings_t &settings,
			const struct viewDef_s *view );
void		R_Volumetric_CountOutcome( volumetricViewStats_t &stats, volumetricLightOutcome_t outcome );
void		R_Volumetric_PublishViewStats( const char *backend, const volumetricViewStats_t &stats );
const char *	R_Volumetric_OutcomeName( volumetricLightOutcome_t outcome );
void		R_Volumetric_PrintStatus();
// A backend created or resized a scattering or depth target. The status
// reports the running count: it must hold still while the views do.
void		R_Volumetric_NoteTargetAllocation();
void		R_Volumetric_RegisterCommands();
// Nonzero while the leaf is requested; changes whenever a setting that shapes
// the scattered light does, so temporal history resets with it.
unsigned int	R_Volumetric_ConfigSignature();

// OpenGL backend (draw_volumetric.cpp).
void		RB_Volumetric_GL_BeginView( const struct viewDef_s *view );
void		RB_Volumetric_GL_DeclineView( const char *reason );
void		RB_Volumetric_GL_InjectLight( const struct viewLight_s *vLight,
			const volumetricShadowParams_t *shadow, bool stencilShadowed, bool mapExpected );
void		RB_Volumetric_GL_Composite();
void		RB_Volumetric_GL_Shutdown();

#endif /* !__VOLUMETRIC_LIGHTING_H__ */
