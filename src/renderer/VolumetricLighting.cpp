// Copyright (C) 2026 DarkMatter Productions
//
// Shadowed volumetric lighting (REN-09): the backend-neutral policy, light
// records and status. See VolumetricLighting.h.

#include "tr_local.h"
#include "VolumetricLighting.h"
#include "ShadowMapProjected.h"
#include "ShadowMapClassification.h"

#include <cmath>

extern idCVar r_rendererModernQuality;
extern idCVar r_rendererFroxelVolumetrics;
extern idCVar r_froxelVolumetricDensity;
extern idCVar r_froxelVolumetricMaxDistance;
extern idCVar r_froxelVolumetricSlices;

idCVar r_volumetricIntensity( "r_volumetricIntensity", "12.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"volumetric lighting: in-scattered light as a multiple of r_froxelVolumetricDensity (how bright beams are for a given haze thickness)", 0.0f, 64.0f );
idCVar r_volumetricAnisotropy( "r_volumetricAnisotropy", "0.35", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"volumetric lighting: forward scattering of the medium (0 scatters evenly, towards 0.9 the light gathers around its source as you look into it)", 0.0f, 0.9f );
idCVar r_volumetricSunIntensity( "r_volumetricSunIntensity", "0.08", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"volumetric lighting: scale of sunlight and other parallel, global or distant lights, whose beams cross the whole view instead of one room", 0.0f, 4.0f );
idCVar r_volumetricResolution( "r_volumetricResolution", "2", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"volumetric lighting resolution: 1 full, 2 half (depth-aware upsampled)", 1, 2, idCmdSystem::ArgCompletion_Integer<1,2> );
idCVar r_volumetricStencilLights( "r_volumetricStencilLights", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"volumetric lighting for lights drawn with stencil shadows, which have no shadow map to sample: 0 add no scattering, 1 scatter unshadowed (beams pass through their casters)", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
idCVar r_volumetricMaxLights( "r_volumetricMaxLights", "32", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"volumetric lighting: most lights that scatter in one view, in view order (0 = no limit)", 0, 256 );
idCVar r_volumetricSubviews( "r_volumetricSubviews", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"volumetric lighting in mirrors, remote cameras and other subviews" );
idCVar r_volumetricDebug( "r_volumetricDebug", "0", CVAR_RENDERER | CVAR_INTEGER,
	"volumetric lighting: 1 show the in-scattered light alone over black, 2 the same with every scattering light unshadowed (the reference its shadow maps are judged against)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

static volumetricViewStats_t	rg_volumetricStats[2];
static const char *		rg_volumetricStatsBackend[2] = { "", "" };
static int			rg_volumetricTargetAllocations = 0;

bool R_Volumetric_Requested() {
	return r_rendererModernQuality.GetBool() && r_rendererFroxelVolumetrics.GetBool();
}

static float R_Volumetric_ClampFinite( float value, float minimum, float maximum, float fallback ) {
	if ( !std::isfinite( value ) ) {
		value = fallback;
	}
	return value < minimum ? minimum : ( value > maximum ? maximum : value );
}

void R_Volumetric_BuildViewSettings( const viewDef_t *view, const char *backendDecline,
		volumetricViewSettings_t &settings ) {
	memset( &settings, 0, sizeof( settings ) );
	settings.declineReason = "";
	settings.requested = R_Volumetric_Requested();
	if ( !settings.requested ) {
		settings.declineReason = "off";
		return;
	}
	if ( view == NULL || view->viewLights == NULL || view->viewEntitys == NULL ) {
		settings.declineReason = "no-3d-view";
		return;
	}
	if ( view->isSubview && !r_volumetricSubviews.GetBool() ) {
		settings.declineReason = "subview";
		return;
	}
	if ( backendDecline != NULL && backendDecline[0] != '\0' ) {
		settings.declineReason = backendDecline;
		return;
	}
	settings.extinction = R_Volumetric_ClampFinite( r_froxelVolumetricDensity.GetFloat(), 0.00001f, 0.01f, 0.00012f );
	settings.scattering = settings.extinction
		* R_Volumetric_ClampFinite( r_volumetricIntensity.GetFloat(), 0.0f, 64.0f, 12.0f );
	settings.anisotropy = R_Volumetric_ClampFinite( r_volumetricAnisotropy.GetFloat(), 0.0f, 0.9f, 0.35f );
	settings.maxDistance = R_Volumetric_ClampFinite( r_froxelVolumetricMaxDistance.GetFloat(), 64.0f, 8192.0f, 2048.0f );
	settings.steps = idMath::ClampInt( 4, VOLUMETRIC_MAX_STEPS, r_froxelVolumetricSlices.GetInteger() );
	settings.resolutionDivisor = r_volumetricResolution.GetInteger() >= 2 ? 2 : 1;
	settings.stencilPolicy = idMath::ClampInt( 0, 1, r_volumetricStencilLights.GetInteger() );
	settings.maxLights = Max( 0, r_volumetricMaxLights.GetInteger() );
	settings.debug = idMath::ClampInt( 0, 2, r_volumetricDebug.GetInteger() );
	// Under temporal AA the march start turns through four offsets, so the
	// history averages four times the steps; without it the pattern holds
	// still and the composite filter takes the dither out.
	settings.noiseOffset = view->temporalJitterEnabled
		? 0.25f * static_cast<float>( tr.frameCount & 3 ) : 0.0f;
	settings.active = true;
}

bool R_Volumetric_ViewTransforms( const viewDef_t *view, float clipToWorld[16], float eye[4], float forward[4] ) {
	if ( view == NULL ) {
		return false;
	}
	// The depth buffer holds the view drawn with exactly this projection,
	// temporal jitter included.
	float modelViewProjection[16];
	myGlMultMatrix( view->worldSpace.modelViewMatrix, view->projectionMatrix, modelViewProjection );
	idMat4 inverse;
	memcpy( inverse.ToFloatPtr(), modelViewProjection, sizeof( modelViewProjection ) );
	// Inverting the array read row-major inverts the column-major matrix too.
	if ( !inverse.InverseSelf() ) {
		return false;
	}
	memcpy( clipToWorld, inverse.ToFloatPtr(), 16 * sizeof( float ) );
	const idVec3 &origin = view->renderView.vieworg;
	const idVec3 &axis = view->renderView.viewaxis[0];
	eye[0] = origin.x;
	eye[1] = origin.y;
	eye[2] = origin.z;
	eye[3] = 1.0f;
	forward[0] = axis.x;
	forward[1] = axis.y;
	forward[2] = axis.z;
	forward[3] = 0.0f;
	return true;
}

bool R_Volumetric_BuildLightRecord( const viewLight_t *vLight, float lightScale, float overBright,
		volumetricLightRecord_t &record ) {
	memset( &record, 0, sizeof( record ) );
	if ( vLight == NULL || vLight->lightShader == NULL || vLight->shaderRegisters == NULL ) {
		return false;
	}
	const idMaterial *lightShader = vLight->lightShader;
	if ( lightShader->IsFogLight() || lightShader->IsBlendLight() || lightShader->IsAmbientLight() ) {
		return false;
	}
	const float *regs = vLight->shaderRegisters;
	float colorScale = lightScale * overBright;
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	if ( classification.parallelLight || classification.globalLight || classification.distantPointLight ) {
		// A sun's beam runs the whole integration distance; a room light's
		// crosses one room. One haze would wash the outdoors out or leave
		// the corridors bare.
		colorScale *= R_Volumetric_ClampFinite( r_volumetricSunIntensity.GetFloat(), 0.0f, 4.0f, 0.08f );
	}
	for ( int stageNum = 0; stageNum < lightShader->GetNumStages() && record.stageCount < VOLUMETRIC_MAX_LIGHT_STAGES; stageNum++ ) {
		const shaderStage_t *stage = lightShader->GetStage( stageNum );
		if ( stage == NULL || !regs[ stage->conditionRegister ] || stage->texture.image == NULL ) {
			continue;
		}
		float color[3];
		for ( int c = 0; c < 3; c++ ) {
			color[c] = colorScale * regs[ stage->color.registers[c] ];
			if ( !std::isfinite( color[c] ) || color[c] < 0.0f ) {
				color[c] = 0.0f;
			}
		}
		if ( color[0] + color[1] + color[2] <= 0.0001f ) {
			continue;
		}
		volumetricLightStage_t &out = record.stages[record.stageCount++];
		out.image = stage->texture.image;
		idPlane planes[3] = { vLight->lightProject[0], vLight->lightProject[1], vLight->lightProject[2] };
		if ( stage->texture.hasMatrix ) {
			// The texture matrix multiplies the S, T and Q texgen exactly as the
			// interactions bake it (RB_BakeTextureMatrixIntoTexgen).
			float m[16];
			RB_GetShaderTextureMatrix( regs, &stage->texture, m );
			const idPlane s = planes[0];
			const idPlane t = planes[1];
			const idPlane q = planes[2];
			for ( int i = 0; i < 4; i++ ) {
				planes[0][i] = s[i] * m[0] + t[i] * m[4] + q[i] * m[12];
				planes[1][i] = s[i] * m[1] + t[i] * m[5] + q[i] * m[13];
			}
		}
		for ( int i = 0; i < 4; i++ ) {
			out.projectS[i] = planes[0][i];
			out.projectT[i] = planes[1][i];
			out.projectQ[i] = planes[2][i];
			out.falloff[i] = vLight->lightProject[3][i];
		}
		out.color[0] = color[0];
		out.color[1] = color[1];
		out.color[2] = color[2];
		out.color[3] = 1.0f;
	}
	if ( record.stageCount == 0 ) {
		return false;
	}
	record.falloffImage = vLight->falloffImage;
	// The phase angle is taken towards globalLightOrigin for every light,
	// as the interactions take their light vector: a parallel light's origin
	// is its far offset centre.
	record.origin[0] = vLight->globalLightOrigin.x;
	record.origin[1] = vLight->globalLightOrigin.y;
	record.origin[2] = vLight->globalLightOrigin.z;
	record.origin[3] = vLight->parallel ? 0.0f : 1.0f;
	idBounds bounds;
	bounds.Clear();
	if ( vLight->frustumTris != NULL ) {
		bounds = vLight->frustumTris->bounds;
	}
	if ( bounds.IsCleared() ) {
		bounds[0].Set( -1.0e6f, -1.0e6f, -1.0e6f );
		bounds[1].Set( 1.0e6f, 1.0e6f, 1.0e6f );
	}
	for ( int i = 0; i < 3; i++ ) {
		record.boundsMin[i] = bounds[0][i];
		record.boundsMax[i] = bounds[1][i];
	}
	return true;
}

volumetricLightOutcome_t R_Volumetric_ClassifyLight( const volumetricViewSettings_t &settings,
		const viewDef_t *view, const volumetricLightRecord_t &record,
		volumetricShadowKind_t shadow, bool stencilShadowed, bool mapExpected, int budgetUsed ) {
	if ( record.stageCount <= 0 ) {
		return VOLUMETRIC_LIGHT_SKIPPED_NO_STAGE;
	}
	if ( view != NULL ) {
		// The march stops at maxDistance from the eye: a light volume wholly
		// past it adds nothing.
		const idVec3 &eye = view->renderView.vieworg;
		float distanceSquared = 0.0f;
		for ( int i = 0; i < 3; i++ ) {
			const float below = record.boundsMin[i] - eye[i];
			const float above = eye[i] - record.boundsMax[i];
			const float outside = Max( 0.0f, Max( below, above ) );
			distanceSquared += outside * outside;
		}
		if ( distanceSquared > settings.maxDistance * settings.maxDistance ) {
			return VOLUMETRIC_LIGHT_SKIPPED_DISTANCE;
		}
	}
	volumetricLightOutcome_t outcome;
	if ( shadow != VOLUMETRIC_SHADOW_NONE ) {
		outcome = VOLUMETRIC_LIGHT_SHADOW_MAPPED;
	} else if ( mapExpected ) {
		// It should have had a map and has none this view: unshadowed beams
		// would cross the very casters its interactions are shadowed by.
		return VOLUMETRIC_LIGHT_SKIPPED_NO_MAP;
	} else if ( stencilShadowed ) {
		if ( settings.stencilPolicy == 0 ) {
			return VOLUMETRIC_LIGHT_SKIPPED_STENCIL;
		}
		outcome = VOLUMETRIC_LIGHT_STENCIL_UNSHADOWED;
	} else {
		outcome = VOLUMETRIC_LIGHT_UNSHADOWED;
	}
	if ( settings.maxLights > 0 && budgetUsed >= settings.maxLights ) {
		return VOLUMETRIC_LIGHT_SKIPPED_BUDGET;
	}
	return outcome;
}

void R_Volumetric_FillProjectedShadow( const shadowMapProjectedLightState_t &state,
		const float composedRects[][4], volumetricShadowParams_t &params ) {
	params.kind = VOLUMETRIC_SHADOW_PROJECTED;
	params.cascadeCount = idMath::ClampInt( 1, VOLUMETRIC_MAX_CASCADES,
		Min( state.cascadeCount, SHADOWMAP_PROJECTED_MAX_CASCADES ) );
	float texelDepthSteps[SHADOWMAP_PROJECTED_MAX_CASCADES];
	R_ShadowMapProjectedTexelDepthSteps( state, texelDepthSteps );
	for ( int c = 0; c < params.cascadeCount; c++ ) {
		for ( int r = 0; r < 4; r++ ) {
			for ( int i = 0; i < 4; i++ ) {
				params.rows[c][r][i] = state.clipPlanes[c][r][i];
			}
		}
		for ( int i = 0; i < 4; i++ ) {
			params.atlasRects[c][i] = composedRects[c][i];
		}
		params.splitDepths[c] = state.splitDepths[c];
		// The receivers' flat-surface bias plus two texel depth steps, so the
		// medium just in front of a lit wall does not shadow itself.
		params.depthBias[c] = Max( 0.0f, r_shadowMapBias.GetFloat() ) * state.biasScale[c]
			+ Max( state.texelDepthBias[c], 0.0f )
			+ 2.0f * Max( texelDepthSteps[c], 0.0f );
	}
}

bool R_Volumetric_OutcomeScatters( volumetricLightOutcome_t outcome ) {
	return outcome == VOLUMETRIC_LIGHT_SHADOW_MAPPED
		|| outcome == VOLUMETRIC_LIGHT_UNSHADOWED
		|| outcome == VOLUMETRIC_LIGHT_STENCIL_UNSHADOWED;
}

void R_Volumetric_TargetExtent( const volumetricViewSettings_t &settings,
		int sceneWidth, int sceneHeight, int &width, int &height ) {
	const int divisor = Max( 1, settings.resolutionDivisor );
	width = Max( 1, ( sceneWidth + divisor - 1 ) / divisor );
	height = Max( 1, ( sceneHeight + divisor - 1 ) / divisor );
}

int R_Volumetric_ChooseTargetSlot( volumetricTargetPool_t &pool, int sceneWidth, int sceneHeight, int divisor ) {
	int slot = -1;
	for ( int i = 0; i < VOLUMETRIC_TARGET_SLOTS; i++ ) {
		if ( pool.lastUse[i] != 0 && pool.sceneWidth[i] == sceneWidth && pool.sceneHeight[i] == sceneHeight
				&& pool.divisor[i] == divisor ) {
			slot = i;
			break;
		}
	}
	if ( slot < 0 ) {
		slot = 0;
		for ( int i = 1; i < VOLUMETRIC_TARGET_SLOTS; i++ ) {
			if ( pool.lastUse[i] < pool.lastUse[slot] ) {
				slot = i;
			}
		}
		pool.sceneWidth[slot] = sceneWidth;
		pool.sceneHeight[slot] = sceneHeight;
		pool.divisor[slot] = divisor;
	}
	pool.lastUse[slot] = ++pool.clock;
	return slot;
}

void R_Volumetric_PackViewParams( const volumetricViewSettings_t &settings,
		float params0[4], float params1[4] ) {
	params0[0] = settings.extinction;
	params0[1] = settings.scattering;
	params0[2] = settings.anisotropy;
	params0[3] = settings.maxDistance;
	params1[0] = static_cast<float>( settings.steps );
	params1[1] = settings.noiseOffset;
	params1[2] = settings.debug != 0 ? 1.0f : 0.0f;
	params1[3] = static_cast<float>( settings.resolutionDivisor );
}

void R_Volumetric_BeginViewStats( volumetricViewStats_t &stats, const volumetricViewSettings_t &settings,
		const viewDef_t *view ) {
	memset( &stats, 0, sizeof( stats ) );
	stats.frame = tr.frameCount;
	stats.subview = view != NULL && view->isSubview;
	stats.declineReason = settings.declineReason;
}

void R_Volumetric_CountOutcome( volumetricViewStats_t &stats, volumetricLightOutcome_t outcome ) {
	if ( outcome >= 0 && outcome < VOLUMETRIC_LIGHT_OUTCOME_COUNT ) {
		stats.outcomes[outcome]++;
	}
	stats.lights++;
}

void R_Volumetric_PublishViewStats( const char *backend, const volumetricViewStats_t &stats ) {
	if ( !stats.composited && stats.declineReason != NULL && idStr::Cmp( stats.declineReason, "no-3d-view" ) == 0 ) {
		// GUI and other 2D views have no lights to scatter; they say nothing.
		return;
	}
	const int slot = ( backend != NULL && idStr::Icmp( backend, "Vulkan" ) == 0 ) ? 1 : 0;
	// A frame's first view replaces the record; its later views add to it.
	volumetricViewStats_t &record = rg_volumetricStats[slot];
	const int composited = stats.composited ? 1 : 0;
	const int subview = stats.composited && stats.subview ? 1 : 0;
	if ( record.frame != stats.frame ) {
		record = stats;
		record.views = composited;
		record.subviews = subview;
		record.viewsDeclined = 1 - composited;
	} else {
		record.views += composited;
		record.subviews += subview;
		record.viewsDeclined += 1 - composited;
		record.lights += stats.lights;
		record.passes += stats.passes;
		for ( int i = 0; i < VOLUMETRIC_LIGHT_OUTCOME_COUNT; i++ ) {
			record.outcomes[i] += stats.outcomes[i];
		}
		record.composited = record.composited || stats.composited;
		if ( stats.width * stats.height > record.width * record.height ) {
			record.width = stats.width;
			record.height = stats.height;
		}
		if ( record.declineReason == NULL || record.declineReason[0] == '\0' ) {
			record.declineReason = stats.declineReason;
		}
	}
	rg_volumetricStatsBackend[slot] = slot == 1 ? "Vulkan" : "OpenGL";
}

void R_Volumetric_NoteTargetAllocation() {
	rg_volumetricTargetAllocations++;
}

const char *R_Volumetric_OutcomeName( volumetricLightOutcome_t outcome ) {
	switch ( outcome ) {
	case VOLUMETRIC_LIGHT_SHADOW_MAPPED: return "shadowMapped";
	case VOLUMETRIC_LIGHT_UNSHADOWED: return "unshadowed";
	case VOLUMETRIC_LIGHT_STENCIL_UNSHADOWED: return "stencilUnshadowed";
	case VOLUMETRIC_LIGHT_SKIPPED_STENCIL: return "skippedStencil";
	case VOLUMETRIC_LIGHT_SKIPPED_NO_MAP: return "skippedNoMap";
	case VOLUMETRIC_LIGHT_SKIPPED_DISTANCE: return "skippedDistance";
	case VOLUMETRIC_LIGHT_SKIPPED_BUDGET: return "skippedBudget";
	case VOLUMETRIC_LIGHT_SKIPPED_NO_STAGE: return "skippedNoStage";
	case VOLUMETRIC_LIGHT_SKIPPED_FAILED: return "skippedFailed";
	default: return "unknown";
	}
}

void R_Volumetric_PrintStatus() {
	const bool requested = R_Volumetric_Requested();
	common->Printf( "Volumetric lighting: requested=%d leaf=%d master=%d density=%.6f intensity=%.2f sun=%.2f anisotropy=%.2f distance=%.0f steps=%d resolution=1/%d stencilLights=%d maxLights=%d subviews=%d targetAllocations=%d\n",
		requested ? 1 : 0, r_rendererFroxelVolumetrics.GetBool() ? 1 : 0, r_rendererModernQuality.GetBool() ? 1 : 0,
		r_froxelVolumetricDensity.GetFloat(), r_volumetricIntensity.GetFloat(), r_volumetricSunIntensity.GetFloat(), r_volumetricAnisotropy.GetFloat(),
		r_froxelVolumetricMaxDistance.GetFloat(), idMath::ClampInt( 4, VOLUMETRIC_MAX_STEPS, r_froxelVolumetricSlices.GetInteger() ),
		r_volumetricResolution.GetInteger() >= 2 ? 2 : 1, r_volumetricStencilLights.GetInteger(),
		r_volumetricMaxLights.GetInteger(), r_volumetricSubviews.GetBool() ? 1 : 0, rg_volumetricTargetAllocations );
	for ( int slot = 0; slot < 2; slot++ ) {
		const volumetricViewStats_t &stats = rg_volumetricStats[slot];
		if ( rg_volumetricStatsBackend[slot][0] == '\0' ) {
			continue;
		}
		const int age = tr.frameCount - stats.frame;
		common->Printf( "Volumetric lighting %s: effective=%d frame=%d age=%d views=%d subviews=%d viewsDeclined=%d target=%dx%d lights=%d passes=%d decline=%s",
			rg_volumetricStatsBackend[slot], stats.composited ? 1 : 0, stats.frame, age, stats.views,
			stats.subviews, stats.viewsDeclined,
			stats.width, stats.height, stats.lights, stats.passes,
			stats.declineReason != NULL && stats.declineReason[0] != '\0' ? stats.declineReason : "none" );
		for ( int i = 0; i < VOLUMETRIC_LIGHT_OUTCOME_COUNT; i++ ) {
			common->Printf( " %s=%d", R_Volumetric_OutcomeName( static_cast<volumetricLightOutcome_t>( i ) ), stats.outcomes[i] );
		}
		common->Printf( "\n" );
	}
}

unsigned int R_Volumetric_ConfigSignature() {
	if ( !R_Volumetric_Requested() ) {
		return 0u;
	}
	unsigned int hash = 2166136261u;
	const float values[] = {
		r_froxelVolumetricDensity.GetFloat(), r_volumetricIntensity.GetFloat(),
		r_volumetricAnisotropy.GetFloat(), r_froxelVolumetricMaxDistance.GetFloat(),
		static_cast<float>( r_froxelVolumetricSlices.GetInteger() ),
		static_cast<float>( r_volumetricResolution.GetInteger() ),
		static_cast<float>( r_volumetricStencilLights.GetInteger() ),
		static_cast<float>( r_volumetricMaxLights.GetInteger() ),
		r_volumetricSubviews.GetBool() ? 1.0f : 0.0f, static_cast<float>( r_volumetricDebug.GetInteger() )
	};
	for ( size_t i = 0; i < sizeof( values ) / sizeof( values[0] ); i++ ) {
		unsigned int bits = 0;
		memcpy( &bits, &values[i], sizeof( bits ) );
		hash = ( hash ^ bits ) * 16777619u;
	}
	return hash | 1u;
}

static void R_Volumetric_PrintStatus_f( const idCmdArgs &args ) {
	R_Volumetric_PrintStatus();
}

void R_Volumetric_RegisterCommands() {
	cmdSystem->AddCommand( "rendererVolumetricStatus", R_Volumetric_PrintStatus_f, CMD_FL_RENDERER,
		"report requested and effective volumetric lighting (REN-09) and how each light was treated" );
}
