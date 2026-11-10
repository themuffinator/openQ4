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
	if ( !vrFrame.active ) {
		vrFrontEndTarget = -1;
		R_SetVRSwapIntervalBypass( false );
		return false;
	}
	if ( tiledViewport[0] ) {
		// a tiled screenshot owns the frame size; that frame presents to the window
		memset( &vrFrame, 0, sizeof( vrFrame ) );
		vrFrontEndTarget = -1;
		return false;
	}
	R_SetVRSwapIntervalBypass( true );

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

static void R_VR_AddTargetCommand( int target, int width, int height ) {
	vrTargetCommand_t *cmd = (vrTargetCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_VR_TARGET;
	cmd->target = target;
	cmd->width = width;
	cmd->height = height;
	cmd->temporalAA = R_TemporalPresentation_GetFrameState().temporalAARequested;
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
	vrFrontEndTarget = -1;
}
