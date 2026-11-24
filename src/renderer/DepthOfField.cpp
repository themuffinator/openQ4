// Copyright (C) 2026 DarkMatter Productions
//
// Depth of field: the cvars, the game's request and the per-view settings
// both back ends draw from (DepthOfFieldCore.h holds the arithmetic). The
// passes live in draw_common.cpp (glprogs/dof.fs) and Vulkan/vk_PostProcess.cpp
// (shaders/post_dof.frag).
//
// The r_dof camera is a capture tool for promotional shots and has no menu
// entry: every cvar is a cheat, so it works in single player and on a
// multiplayer server only with cheats on (and is reset when they are not).
//
// r_depthOfField is the player's Depth of Field setting (Settings > System,
// owned by the performance presets): it lets the game's requests draw, the
// weapon wheel's focus, a focused in-world screen and map scripts' cinematic
// lens. It does not touch the
// r_dof camera.

#include "tr_local.h"
#include "DepthOfFieldCore.h"

idCVar r_depthOfField( "r_depthOfField", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"depth of field where the game asks for it: the weapon wheel's focus on your weapon, focus on the in-world screen you are using, and cinematic focus from map scripts" );
idCVar r_dof( "r_dof", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_BOOL,
	"cinematic depth of field for promotional captures: a thin-lens camera focused by r_dofFocusDistance (or the depth under r_dofFocusX/Y)" );
idCVar r_dofFocusDistance( "r_dofFocusDistance", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"depth of field focus distance in world units; 0 = autofocus on the depth under r_dofFocusX/Y", 0.0f, 65536.0f );
idCVar r_dofFocusX( "r_dofFocusX", "0.5", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"autofocus point across the view, 0 = left edge, 1 = right edge", 0.0f, 1.0f );
idCVar r_dofFocusY( "r_dofFocusY", "0.5", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"autofocus point down the view, 0 = top edge, 1 = bottom edge", 0.0f, 1.0f );
idCVar r_dofFocusSpeed( "r_dofFocusSpeed", "4", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"how quickly autofocus pulls to a new distance (per second); 0 = at once", 0.0f, 100.0f );
idCVar r_dofFStop( "r_dofFStop", "2.8", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"lens aperture as an f-number: smaller opens the lens and narrows the depth of field", 0.5f, 64.0f );
idCVar r_dofFocalLength( "r_dofFocalLength", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"lens focal length in millimetres; 0 = the length that gives the view's field of view on a 36 mm sensor (zoom in for a shallower focus)", 0.0f, 1200.0f );
idCVar r_dofBlurScale( "r_dofBlurScale", "1", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"multiplies the lens's blur, for a look beyond the physical lens", 0.0f, 16.0f );
idCVar r_dofFocusRange( "r_dofFocusRange", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"depth kept fully sharp on each side of the focus distance, in world units", 0.0f, 65536.0f );
idCVar r_dofNearBlur( "r_dofNearBlur", "1", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"scales the blur in front of the focus plane", 0.0f, 4.0f );
idCVar r_dofFarBlur( "r_dofFarBlur", "1", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"scales the blur behind the focus plane", 0.0f, 4.0f );
idCVar r_dofMaxBlur( "r_dofMaxBlur", "32", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"largest blur radius, in pixels of a 1080-line view (scaled to the view's height)", 1.0f, 64.0f );
idCVar r_dofQuality( "r_dofQuality", "2", CVAR_RENDERER | CVAR_CHEAT | CVAR_INTEGER,
	"depth of field quality: 0 = 32 samples, 1 = 64, 2 = 128 (half resolution), 3 = 192 at full resolution", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar r_dofBlades( "r_dofBlades", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_INTEGER,
	"aperture blades shaping the bokeh: 0 = round, 3 to 12 = a polygon", 0, 12, idCmdSystem::ArgCompletion_Integer<0,12> );
idCVar r_dofBladeRotation( "r_dofBladeRotation", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"rotation of the aperture polygon, in degrees", -360.0f, 360.0f );
idCVar r_dofBladeRoundness( "r_dofBladeRoundness", "0.25", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"curvature of the aperture blades: 0 = straight edges, 1 = round", 0.0f, 1.0f );
idCVar r_dofHighlights( "r_dofHighlights", "1", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"how strongly bright points bloom into bokeh discs", 0.0f, 8.0f );
idCVar r_dofHighlightThreshold( "r_dofHighlightThreshold", "0.7", CVAR_RENDERER | CVAR_CHEAT | CVAR_FLOAT,
	"brightness above which a point counts as a highlight", 0.0f, 16.0f );
idCVar r_dofDebug( "r_dofDebug", "0", CVAR_RENDERER | CVAR_CHEAT | CVAR_BOOL,
	"show the circle of confusion instead of the image: orange in front of focus, blue behind, and a ring at the autofocus point" );

/*
====================
idRenderSystemLocal::SetViewDepthOfField
====================
*/
bool idRenderSystemLocal::SetViewDepthOfField( const renderViewDepthOfField_t &dof ) {
	depthOfFieldRequest = dof;
	depthOfFieldRequest.amount = idMath::ClampFloat( 0.0f, 1.0f, dof.amount );
	depthOfFieldRequestFrame = frameCount;
	if ( depthOfFieldRequest.amount <= 0.001f ) {
		return true;
	}
	return r_depthOfField.GetBool() && RB_DepthOfFieldAvailable();
}

/*
====================
R_DepthOfField_LatchRequest

A request belongs to the frame it was made in; a game that stops asking stops
getting it.
====================
*/
void R_DepthOfField_LatchRequest( viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return;
	}
	const bool current = tr.depthOfFieldRequestFrame == tr.frameCount && r_depthOfField.GetBool();
	viewDef->depthOfFieldRequest = current ? tr.depthOfFieldRequest : renderViewDepthOfField_t();
}

/*
====================
R_DepthOfField_ResolveView

The settings for one view; false when it draws no depth of field. Only the
main scene view qualifies (the back ends check that), and never a headset's
eye: a defocused world strapped to the face is no promo shot.
====================
*/
bool R_DepthOfField_ResolveView( const viewDef_t *viewDef, int viewWidth, int viewHeight,
		depthOfFieldSettings_t &settings ) {
	settings = depthOfFieldSettings_t();
	if ( viewDef == NULL || viewDef->renderView.asymmetricFov || r_skipPostProcess.GetBool() ) {
		return false;
	}
	const renderViewDepthOfField_t &request = viewDef->depthOfFieldRequest;
	if ( !r_dof.GetBool() && request.amount <= 0.001f ) {
		return false;
	}
	depthOfFieldInputs_t in;
	in.enabled = r_dof.GetBool();
	in.requestAmount = request.amount;
	in.requestFlags = ( ( request.flags & VIEW_DOF_FOCUS_VIEW_WEAPON ) != 0 ? DOF_REQUEST_FOCUS_VIEW_WEAPON : 0 )
		| ( ( request.flags & VIEW_DOF_SHARP_VIEW_WEAPON ) != 0 ? DOF_REQUEST_SHARP_VIEW_WEAPON : 0 );
	in.requestFocusDistance = request.focusDistance;
	in.requestFStop = request.fStop;
	in.requestFocalLength = request.focalLength;
	in.requestFocusRange = request.focusRange;
	in.requestMaxBlur = request.maxBlur;
	in.focusDistance = r_dofFocusDistance.GetFloat();
	in.focusX = r_dofFocusX.GetFloat();
	in.focusY = r_dofFocusY.GetFloat();
	in.fStop = r_dofFStop.GetFloat();
	in.focalLength = r_dofFocalLength.GetFloat();
	in.blurScale = r_dofBlurScale.GetFloat();
	in.focusRange = r_dofFocusRange.GetFloat();
	in.nearBlur = r_dofNearBlur.GetFloat();
	in.farBlur = r_dofFarBlur.GetFloat();
	in.maxBlur = r_dofMaxBlur.GetFloat();
	in.quality = r_dofQuality.GetInteger();
	in.blades = r_dofBlades.GetInteger();
	in.bladeRotation = r_dofBladeRotation.GetFloat();
	in.bladeRoundness = r_dofBladeRoundness.GetFloat();
	in.highlights = r_dofHighlights.GetFloat();
	in.highlightThreshold = r_dofHighlightThreshold.GetFloat();
	in.debug = r_dofDebug.GetBool() ? 1 : 0;
	in.fovX = viewDef->renderView.fov_x;
	in.viewWidth = viewWidth;
	in.viewHeight = viewHeight;
	return DepthOfFieldCore_Resolve( in, settings );
}

float R_DepthOfField_FocusElapsed( int &lastMsec ) {
	const int now = Sys_Milliseconds();
	const int previous = lastMsec;
	lastMsec = now;
	if ( previous <= 0 || now < previous ) {
		return 0.0f;
	}
	return static_cast<float>( now - previous ) * 0.001f;
}

float R_DepthOfField_FocusSpeed( void ) {
	return r_dofFocusSpeed.GetFloat();
}
