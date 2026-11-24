// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//

/*
===============================================================================

	OpenXR presentation, front end.

	The engine hands a frame's swapchain images over with SetVRFrame before
	BeginFrame. For that one frame the output is the virtual screen (menus,
	HUD, console, cinematics); SetVRRenderTarget moves the front end to an eye
	and back, switching the extent everything downstream reads (glConfig
	sizes, crop 0, the 2D viewport, the presentation state the game sizes its
	targets from) and queueing RC_VR_TARGET so the backend switches in command
	order. The backend halves are OpenGL/gl_VRPresentation.cpp, which renders
	into the render textures below, and Vulkan/vk_VRPresentation.cpp, which
	keeps its own targets.
	See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

===============================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "TemporalPresentation.h"
#include "ScenePackets.h"

static const int VR_MIN_TARGET_SIZE = 64;
static const int VR_MAX_TARGET_SIZE = 8192;

static const char *const vrTargetImageNames[3][2] = {
	{ "_vrScreenColor", "_vrScreenDepth" },
	{ "_vrEyeColor0", "_vrEyeDepth0" },
	{ "_vrEyeColor1", "_vrEyeDepth1" },
};

static const char *const vrTargetLabels[3] = {
	"openQ4 VR virtual screen",
	"openQ4 VR left eye",
	"openQ4 VR right eye",
};

static bool R_VR_TargetSizeValid( int width, int height ) {
	return width >= VR_MIN_TARGET_SIZE && height >= VR_MIN_TARGET_SIZE
		&& width <= VR_MAX_TARGET_SIZE && height <= VR_MAX_TARGET_SIZE
		&& width <= glConfig.maxTextureSize && height <= glConfig.maxTextureSize;
}

/*
====================
R_VR_EnsureTarget

The stand-ins for the window: RGBA8 colour with depth/stencil, single sample.
Created once and resized when the runtime's sizes change.
====================
*/
static idRenderTexture *R_VR_EnsureTarget( int index, int width, int height ) {
	idRenderTexture *&target = tr.vrTargets[index];
	if ( target != NULL ) {
		int currentWidth = 0, currentHeight = 0;
		tr.GetRenderTextureSize( target, currentWidth, currentHeight );
		if ( currentWidth == width && currentHeight == height ) {
			return target;
		}
		// a failed resize queues the target for destruction and clears it
		tr.ResizeRenderTexture( target, width, height );
		return target;
	}

	idImageOpts opts;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.numLevels = 1;
	opts.textureType = TT_2D;
	opts.isPersistant = true;
	opts.width = width;
	opts.height = height;
	opts.numMSAASamples = 0;
	idImage *color = tr.CreateImage( vrTargetImageNames[index][0], &opts, TF_LINEAR );
	opts.format = FMT_DEPTH_STENCIL;
	idImage *depth = tr.CreateImage( vrTargetImageNames[index][1], &opts, TF_LINEAR );
	if ( color == NULL || depth == NULL ) {
		return NULL;
	}
	target = tr.CreateRenderTexture( color, depth );
	if ( target != NULL ) {
		tr.SetRenderTextureDebugName( target, vrTargetLabels[index] );
	}
	return target;
}

/*
====================
R_VR_DestroyTargets

Render textures hold image pointers, so they go before any image purge.
====================
*/
void R_VR_DestroyTargets( void ) {
	memset( &tr.vrFrame, 0, sizeof( tr.vrFrame ) );
	tr.desktopStereoLayout = STEREO3D_OFF;
	R_VR_ShutdownTargets();
	for ( int i = 0; i < 3; i++ ) {
		if ( tr.vrTargets[i] != NULL ) {
			tr.DestroyRenderTexture( tr.vrTargets[i] );
			tr.vrTargets[i] = NULL;
		}
	}
}

/*
====================
idRenderSystemLocal::GetVRGraphicsBinding
====================
*/
bool idRenderSystemLocal::GetVRGraphicsBinding( renderVRGraphicsBinding_t &binding ) {
	if ( !glConfig.isInitialized ) {
		memset( &binding, 0, sizeof( binding ) );
		return false;
	}
	return R_VR_QueryGraphicsBinding( binding );
}

/*
====================
idRenderSystemLocal::SetVRFrame
====================
*/
void idRenderSystemLocal::SetVRFrame( const renderVRFrame_t *frame ) {
	// Without a headset the engine clears the frame on every frame. vrFrame is
	// either an active frame or all zero, so clearing a cleared frame changes
	// nothing the backend reads and must not wait for it: that wait removed
	// every overlap of r_vkRenderThread 1 and 2 in ordinary play.
	if ( !vrFrame.active && ( frame == NULL || ( !frame->active && !frame->releaseImages ) ) ) {
		return;
	}
	// the backend reads the frame's images and waits on its queue; a Vulkan
	// render thread must be done with the previous frame first
	R_RenderThread_Sync( "vr frame" );
	memset( &vrFrame, 0, sizeof( vrFrame ) );
	if ( frame != NULL && frame->releaseImages && glConfig.isInitialized ) {
		// the engine is about to destroy swapchains this renderer drew into
		R_VR_BackendReleaseImages();
	}
	if ( frame == NULL || !frame->active || !glConfig.isInitialized || !R_VR_BackendCanPresent() ) {
		return;
	}
	if ( !R_VR_TargetSizeValid( frame->screenWidth, frame->screenHeight ) ) {
		return;
	}
	if ( R_VR_BackendOwnsTargets() ) {
		// the backend's own stand-ins for the window (Vulkan)
		renderVRFrame_t prepared = *frame;
		if ( prepared.stereo && !R_VR_TargetSizeValid( prepared.eyeWidth, prepared.eyeHeight ) ) {
			prepared.stereo = false;
		}
		if ( R_VR_BackendPrepareTargets( prepared ) ) {
			vrFrame = prepared;
		}
		return;
	}
	if ( frame->screenImage == 0 ) {
		return;
	}
	if ( R_VR_EnsureTarget( 0, frame->screenWidth, frame->screenHeight ) == NULL ) {
		return;
	}

	vrFrame = *frame;
	if ( vrFrame.stereo ) {
		const bool usable = vrFrame.eyeImages[0] != 0 && vrFrame.eyeImages[1] != 0
			&& R_VR_TargetSizeValid( vrFrame.eyeWidth, vrFrame.eyeHeight )
			&& R_VR_EnsureTarget( 1, vrFrame.eyeWidth, vrFrame.eyeHeight ) != NULL
			&& R_VR_EnsureTarget( 2, vrFrame.eyeWidth, vrFrame.eyeHeight ) != NULL;
		if ( !usable ) {
			vrFrame.stereo = false;
		}
	}
}

/*
====================
idRenderSystemLocal::GetVRFrameResult
====================
*/
void idRenderSystemLocal::GetVRFrameResult( renderVRFrameResult_t &result ) const {
	result = vrFrameResult;
}

/*
===============================================================================

	Desktop stereoscopic 3D (r_stereo3D)

	Without a headset, a frame can still be a stereo frame: the eyes and the
	virtual screen become offscreen targets the same way, the game renders
	its first-person 3D pass once per eye (idPlayerView::StereoView), and
	everything else, menus, HUD and console, lands on the screen layer. At
	the swap the backend composites the three into the window in the chosen
	layout (stereo3DLayout_t) instead of copying them into swapchains:

	  layout             screen layer     each eye
	  side by side       window           half the width
	  side by side full  half the width   half the width
	  top and bottom     window           half the height
	  anaglyph           window           window
	  interlaced         window           half the height

	The screen layer is the size the game lays its HUD out for. Side by side
	full is for displays that show each half to one eye at full resolution
	(glasses with a 3840x1080 stereo mode), so the game sees one half; the
	half-resolution layouts are what 3D televisions and monitors unpack, and
	the game sees the whole window.

===============================================================================
*/

static idCVar r_stereo3D( "r_stereo3D", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"stereoscopic 3D without a headset: 0 off, 1 side by side, 2 side by side at full resolution "
	"(double-width displays), 3 top and bottom, 4 anaglyph (red/cyan glasses), "
	"5 row interlaced (passive 3D displays, fullscreen)", 0, STEREO3D_LAYOUT_COUNT - 1,
	idCmdSystem::ArgCompletion_Integer<0, STEREO3D_LAYOUT_COUNT - 1> );
static idCVar r_stereoSeparation( "r_stereoSeparation", "2.5", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"stereoscopic 3D: the distance between the eyes in game units (about 2.5 is a person's); "
	"more deepens the effect, 0 flattens it", 0.0f, 32.0f );
static idCVar r_stereoConvergence( "r_stereoConvergence", "128", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"stereoscopic 3D: how far ahead, in game units, the world meets the screen's surface; "
	"nearer things stand out of the screen, farther ones sink into it", 8.0f, 4096.0f );
static idCVar r_stereoSwapEyes( "r_stereoSwapEyes", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"stereoscopic 3D: show each eye's image to the other eye (displays that start on the right, "
	"or cross-eyed viewing of side by side)" );

// The eye and screen-layer extents of a layout in a window, false when they
// are too small or too large to be targets.
static bool R_DesktopStereo_Extents( int layout, int windowWidth, int windowHeight,
		int &screenWidth, int &screenHeight, int &eyeWidth, int &eyeHeight ) {
	screenWidth = windowWidth;
	screenHeight = windowHeight;
	eyeWidth = windowWidth;
	eyeHeight = windowHeight;
	switch ( layout ) {
		case STEREO3D_SIDE_BY_SIDE:
			eyeWidth = windowWidth / 2;
			break;
		case STEREO3D_SIDE_BY_SIDE_FULL:
			screenWidth = eyeWidth = windowWidth / 2;
			break;
		case STEREO3D_TOP_BOTTOM:
		case STEREO3D_INTERLACED:
			eyeHeight = windowHeight / 2;
			break;
		case STEREO3D_ANAGLYPH:
			break;
		default:
			return false;
	}
	return R_VR_TargetSizeValid( screenWidth, screenHeight ) && R_VR_TargetSizeValid( eyeWidth, eyeHeight );
}

// Why r_stereo3D cannot run: reported once each time the reason changes.
static void R_DesktopStereo_Decline( const char *reason ) {
	static idStr reported;
	if ( reported.Icmp( reason ) != 0 ) {
		reported = reason;
		if ( reason[0] != '\0' ) {
			common->Warning( "r_stereo3D: %s; the frame is flat", reason );
		}
	}
}

/*
====================
R_DesktopStereo_BeginFrame

Opens this frame as a desktop stereo frame when r_stereo3D asks for one and
nothing else owns the frame: a headset's own frame, a tiled screenshot or a
capture. False leaves vrFrame inactive.
====================
*/
static bool R_DesktopStereo_BeginFrame( int windowWidth, int windowHeight ) {
	const int layout = r_stereo3D.GetInteger();
	if ( layout <= STEREO3D_OFF || layout >= STEREO3D_LAYOUT_COUNT ) {
		R_DesktopStereo_Decline( "" );
		return false;
	}
	// A window-sized screenshot renders one frame of the window's own size, and
	// reads the composite back; a tiled one needs its frames flat.
	const bool singleTileScreenshot = tr.tiledViewport[0] == tr.tiledWindow[0] && tr.tiledViewport[1] == tr.tiledWindow[1]
		&& tr.viewportOffset[0] == 0 && tr.viewportOffset[1] == 0;
	if ( !glConfig.isInitialized || ( tr.tiledViewport[0] && !singleTileScreenshot ) ) {
		return false;
	}
	if ( !R_VR_BackendCanPresent() || !R_VR_BackendCanPresentDesktopStereo() ) {
		R_DesktopStereo_Decline( "this renderer cannot composite a stereo frame" );
		return false;
	}
	renderVRFrame_t frame;
	memset( &frame, 0, sizeof( frame ) );
	if ( !R_DesktopStereo_Extents( layout, windowWidth, windowHeight,
			frame.screenWidth, frame.screenHeight, frame.eyeWidth, frame.eyeHeight ) ) {
		R_DesktopStereo_Decline( "the window is too small or too large for its eye targets" );
		return false;
	}
	frame.active = true;
	frame.stereo = true;
	frame.mirror = RENDER_VR_MIRROR_NONE;

	// a backend thread may still read the last frame's state
	R_RenderThread_Sync( "desktop stereo frame" );
	// the backend reads the layout while it prepares its own targets
	tr.desktopStereoLayout = layout;
	bool usable;
	if ( R_VR_BackendOwnsTargets() ) {
		usable = R_VR_BackendPrepareTargets( frame ) && frame.stereo;
	} else {
		usable = R_VR_EnsureTarget( 0, frame.screenWidth, frame.screenHeight ) != NULL
			&& R_VR_EnsureTarget( 1, frame.eyeWidth, frame.eyeHeight ) != NULL
			&& R_VR_EnsureTarget( 2, frame.eyeWidth, frame.eyeHeight ) != NULL;
	}
	if ( !usable ) {
		tr.desktopStereoLayout = STEREO3D_OFF;
		R_DesktopStereo_Decline( "its eye targets could not be created" );
		return false;
	}
	R_DesktopStereo_Decline( "" );
	tr.vrFrame = frame;
	tr.desktopStereoSwapEyes = r_stereoSwapEyes.GetBool();
	return true;
}

/*
====================
idRenderSystemLocal::BeginVRFrame

Called by BeginFrame before it latches the frame's size. A VR frame's output
is the virtual screen; the real window size is kept for the mirror.
====================
*/
bool idRenderSystemLocal::BeginVRFrame( int &windowWidth, int &windowHeight ) {
	memset( &vrFrameResult, 0, sizeof( vrFrameResult ) );
	// no target yet: BeginFrame's SetVRRenderTarget( -1 ) queues the first switch
	vrFrontEndTarget = -2;
	desktopStereoLayout = STEREO3D_OFF;
	if ( !vrFrame.active && !R_DesktopStereo_BeginFrame( windowWidth, windowHeight ) ) {
		vrFrontEndTarget = -1;
		R_SetVRSwapIntervalBypass( false );
		return false;
	}
	if ( tiledViewport[0] && desktopStereoLayout == STEREO3D_OFF ) {
		// a tiled screenshot owns the frame size; that frame presents to the window
		memset( &vrFrame, 0, sizeof( vrFrame ) );
		desktopStereoLayout = STEREO3D_OFF;
		vrFrontEndTarget = -1;
		return false;
	}
	// a headset paces its own frames; a stereo display is the window, which keeps vsync
	R_SetVRSwapIntervalBypass( desktopStereoLayout == STEREO3D_OFF );

	vrWindowWidth = windowWidth;
	vrWindowHeight = windowHeight;
	vrSavedUIViewport[0] = glConfig.uiViewportX;
	vrSavedUIViewport[1] = glConfig.uiViewportY;
	vrSavedUIViewport[2] = glConfig.uiViewportWidth;
	vrSavedUIViewport[3] = glConfig.uiViewportHeight;

	windowWidth = vrFrame.screenWidth;
	windowHeight = vrFrame.screenHeight;
	glConfig.uiViewportX = 0;
	glConfig.uiViewportY = 0;
	glConfig.uiViewportWidth = windowWidth;
	glConfig.uiViewportHeight = windowHeight;

	vrFrameResult.presented = true;
	return true;
}

/*
====================
idRenderSystemLocal::ApplyVRTargetExtent

The front-end half of a target switch: everything that sizes 2D, crops and
the game's scene targets now reads the target's extent.
====================
*/
void idRenderSystemLocal::ApplyVRTargetExtent( int width, int height ) {
	glConfig.vidWidth = width;
	glConfig.vidHeight = height;
	glConfig.uiViewportX = 0;
	glConfig.uiViewportY = 0;
	glConfig.uiViewportWidth = width;
	glConfig.uiViewportHeight = height;
	renderCrops[0].x = 0;
	renderCrops[0].y = 0;
	renderCrops[0].width = width;
	renderCrops[0].height = height;
	postProcessTexelSize.Set( 1.0f / static_cast<float>( width ), 1.0f / static_cast<float>( height ),
		static_cast<float>( width ), static_cast<float>( height ) );
	R_TemporalPresentation_SetVRExtent( width, height );
}

/*
====================
R_VR_PublishWindowSize, R_VR_PublishUIViewport

A backend learns the window's live size while it presents: a swap polls the
window, a resize recreates the swapchain. During a VR frame glConfig holds the
frame's target extent and FinishVRFrame puts the window's back, so the size
it puts back has to follow, or a window resized in VR kept the size the frame
began with for as long as VR frames ran: the desktop mirror stayed at the old
size, and the flat frames after VR used it too.
====================
*/
void R_VR_PublishWindowSize( int width, int height ) {
	if ( tr.vrFrame.active && width > 0 && height > 0 ) {
		tr.vrWindowWidth = width;
		tr.vrWindowHeight = height;
	}
}

void R_VR_PublishUIViewport( int x, int y, int width, int height ) {
	if ( tr.vrFrame.active ) {
		tr.vrSavedUIViewport[0] = x;
		tr.vrSavedUIViewport[1] = y;
		tr.vrSavedUIViewport[2] = width;
		tr.vrSavedUIViewport[3] = height;
	}
}

/*
====================
R_VR_FrontEndEye
====================
*/
int R_VR_FrontEndEye( void ) {
	return tr.vrFrame.active && tr.vrFrame.stereo && ( tr.vrFrontEndTarget == 0 || tr.vrFrontEndTarget == 1 )
		? tr.vrFrontEndTarget : -1;
}

/*
====================
R_VR_EyeSubviewFrameKey

A texture subview whose camera is derived from the viewer (a mirror,
reflection, refraction or x-ray render map) is rendered once per frame and
eye: the second eye reusing the first eye's capture showed it the first
eye's reflection. Unsigned arithmetic keeps the key defined when the frame
count wraps; equality is all callers test.
====================
*/
int R_VR_EyeSubviewFrameKey( void ) {
	const unsigned int eye = (unsigned int)( R_VR_FrontEndEye() + 1 );	// 0 flat or screen, 1/2 the eyes
	return (int)( (unsigned int)tr.frameCount * 4u + eye );
}

static void R_VR_AddTargetCommand( int target, int width, int height, bool revisit = false ) {
	vrTargetCommand_t *cmd = (vrTargetCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_VR_TARGET;
	cmd->target = target;
	cmd->width = width;
	cmd->height = height;
	cmd->temporalAA = R_TemporalPresentation_GetFrameState().temporalAARequested;
	cmd->revisit = revisit;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
====================
idRenderSystemLocal::SetVRRenderTarget
====================
*/
bool idRenderSystemLocal::SetVRRenderTarget( int eye ) {
	if ( !vrFrame.active || eye < -1 || eye > 1 ) {
		return false;
	}
	if ( eye >= 0 && !vrFrame.stereo ) {
		return false;
	}
	if ( eye == vrFrontEndTarget ) {
		return true;
	}
	if ( currentRenderCrop != 0 ) {
		// a save preview or subview capture holds a crop; its pixels belong to the current target
		common->Warning( "SetVRRenderTarget( %d ): a render crop is open", eye );
		return false;
	}

	// pending 2D belongs to the target it was drawn for
	guiModel->EmitFullScreen();
	guiModel->Clear();
	activeRenderTexture = NULL;

	const int width = eye < 0 ? vrFrame.screenWidth : vrFrame.eyeWidth;
	const int height = eye < 0 ? vrFrame.screenHeight : vrFrame.eyeHeight;
	// the temporal frame state that follows the extent reads which target this is
	vrFrontEndTarget = eye;
	ApplyVRTargetExtent( width, height );
	R_VR_AddTargetCommand( eye, width, height );
	if ( eye >= 0 ) {
		vrFrameResult.eyeRendered[eye] = true;
	}
	return true;
}

/*
====================
R_DesktopStereo_BeginCapture, R_DesktopStereo_EndCapture

A capture of the finished frame (CaptureRenderToImage) taken on a desktop
stereo frame's screen layer would read the layer, not the scene under it:
the retained menus' blurred backdrop came out black. Once the left eye is
drawn, the capture reads it instead, as the flat frame's scene, and what the
caller draws from it lands on the layer over both eyes. The eye is finished
already, so the visit neither finishes nor clears it again.
====================
*/
bool R_DesktopStereo_BeginCapture( void ) {
	if ( tr.desktopStereoLayout == STEREO3D_OFF || tr.vrFrontEndTarget != -1 || !tr.vrFrameResult.eyeRendered[0]
			|| tr.currentRenderCrop != 0 || tr.activeRenderTexture != NULL ) {
		return false;
	}
	tr.guiModel->EmitFullScreen();
	tr.guiModel->Clear();
	tr.vrFrontEndTarget = 0;
	tr.ApplyVRTargetExtent( tr.vrFrame.eyeWidth, tr.vrFrame.eyeHeight );
	R_VR_AddTargetCommand( 0, tr.vrFrame.eyeWidth, tr.vrFrame.eyeHeight, true );
	return true;
}

void R_DesktopStereo_EndCapture( void ) {
	tr.vrFrontEndTarget = -1;
	tr.ApplyVRTargetExtent( tr.vrFrame.screenWidth, tr.vrFrame.screenHeight );
	R_VR_AddTargetCommand( -1, tr.vrFrame.screenWidth, tr.vrFrame.screenHeight, true );
}

/*
====================
idRenderSystemLocal::EndVRFrame

Before the swap command: the frame ends on the virtual screen, which the
backend presents last.
====================
*/
void idRenderSystemLocal::EndVRFrame( void ) {
	if ( !vrFrame.active ) {
		return;
	}
	if ( vrFrontEndTarget != -1 ) {
		SetVRRenderTarget( -1 );
	}
	vrFrameResult.frameNumber = frameCount;
}

/*
====================
idRenderSystemLocal::FinishVRFrame

After the backend presented: the window is the output again until the
engine opens the next VR frame.
====================
*/
void idRenderSystemLocal::FinishVRFrame( void ) {
	if ( !vrFrame.active ) {
		return;
	}
	glConfig.vidWidth = vrWindowWidth;
	glConfig.vidHeight = vrWindowHeight;
	glConfig.uiViewportX = vrSavedUIViewport[0];
	glConfig.uiViewportY = vrSavedUIViewport[1];
	glConfig.uiViewportWidth = vrSavedUIViewport[2];
	glConfig.uiViewportHeight = vrSavedUIViewport[3];
	renderCrops[0].x = 0;
	renderCrops[0].y = 0;
	renderCrops[0].width = vrWindowWidth;
	renderCrops[0].height = vrWindowHeight;
	memset( &vrFrame, 0, sizeof( vrFrame ) );
	desktopStereoLayout = STEREO3D_OFF;
	vrFrontEndTarget = -1;
}
