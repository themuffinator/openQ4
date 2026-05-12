// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ScenePackets.h"

const char *RenderPassCategory_Name( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "depth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "stencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "shadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "arb2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "lightGrid";
	case RENDER_PASS_AMBIENT:
		return "ambient";
	case RENDER_PASS_FOG_BLEND:
		return "fogBlend";
	case RENDER_PASS_SSAO:
		return "ssao";
	case RENDER_PASS_MOTION_BLUR:
		return "motionBlur";
	case RENDER_PASS_LENS_FLARE:
		return "lensFlare";
	case RENDER_PASS_BLOOM:
		return "bloom";
	case RENDER_PASS_AUTHORED_POST:
		return "authoredPost";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "specialEffects";
	case RENDER_PASS_GUI:
		return "gui";
	case RENDER_PASS_PRESENT:
		return "present";
	default:
		return "unknown";
	}
}
