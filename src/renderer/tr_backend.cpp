/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/



#include "tr_local.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "RenderGraph.h"
#include "RenderGraphResources.h"
#include "MaterialResourceTable.h"
#include "ClassicGuiDomain.h"
#include "ClassicCinematicPostDomain.h"
#include "ClassicSpecialFrameDomain.h"
#include "ClassicWorldAmbientDomain.h"
#include "ClassicInteractionDomain.h"
#include "ClassicFogBlendDomain.h"
#include "ClassicSubviewDomain.h"
#include "ModernGLExecutor.h"
#include "ModernClusteredLighting.h"
#include "RendererMetrics.h"


frameData_t		*frameData;
backEndState_t	backEnd;

// true while the dormant-pipeline metric mirrors hold freshly zeroed values;
// re-armed whenever the modern side pipeline records real stats
static bool rg_modernStatMirrorsZeroed = false;

// ~1 MB of packet/record arrays: static storage (like rg_frontEndScenePacketFrame)
// keeps it out of the backend stack frame and its per-call __chkstk probe.
// R_ScenePackets_BuildLegacyCommandStream Clear()s it before each use; only valid
// while a single backend executes at a time.
static idScenePacketFrame rg_backendScenePacketFrame;

static ID_INLINE GLint R_SafeStencilClearValue() {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}


/*
======================
RB_SetDefaultGLState

This should initialize all GL state that any part of the entire program
may touch, including the editor.
======================
*/
void RB_SetDefaultGLState( void ) {
	int		i;
	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );

	RB_LogComment( "--- R_SetDefaultGLState ---\n" );

	glClearDepth( 1.0f );
	glColor4f (1,1,1,1);

	// the vertex array is always enabled
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	//
	// make sure our GL state vector is set correctly
	//
	memset( &backEnd.glState, 0, sizeof( backEnd.glState ) );
	backEnd.glState.forceGlState = true;
	idVertexCache::InvalidateBufferBindings();

	glColorMask( 1, 1, 1, 1 );

	glEnable( GL_DEPTH_TEST );
	glEnable( GL_BLEND );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_CULL_FACE );
	glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	if ( glConfig.backendCaps.hasFixedFunctionCompatibility ) {
		glDisable( GL_LIGHTING );
		glDisable( GL_LINE_STIPPLE );
	}
	glDisable( GL_STENCIL_TEST );

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_ALWAYS );
 
	glCullFace( GL_FRONT_AND_BACK );
	glShadeModel( GL_SMOOTH );

	if ( r_useScissor.GetBool() ) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	}

	for ( i = maxStateUnits - 1 ; i >= 0 ; i-- ) {
		GL_SelectTexture( i );

		// object linear texgen is our default
		glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glTexGenf( GL_Q, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );

		GL_TexEnv( GL_MODULATE );
		if ( !glConfig.backendCaps.hasFixedFunctionCompatibility ) {
			continue;
		}
		glDisable( GL_TEXTURE_2D );
		if ( glConfig.texture3DAvailable ) {
			glDisable( GL_TEXTURE_3D );
		}
		if ( glConfig.cubeMapAvailable ) {
			glDisable( GL_TEXTURE_CUBE_MAP_EXT );
		}
	}
}


/*
====================
RB_LogComment
====================
*/
void RB_LogComment( const char *comment, ... ) {
   va_list marker;

	if ( !tr.logFile ) {
		return;
	}

	fprintf( tr.logFile, "// " );
	va_start( marker, comment );
	vfprintf( tr.logFile, comment, marker );
	va_end( marker );
}


//=============================================================================



/*
====================
GL_SelectTexture
====================
*/
void GL_SelectTexture( int unit ) {
	if ( backEnd.glState.currenttmu == unit ) {
		return;
	}

	if ( unit < 0 || unit >= MAX_MULTITEXTURE_UNITS || unit >= glConfig.maxTextureUnits || unit >= glConfig.maxTextureImageUnits ) {
		common->Warning(
			"GL_SelectTexture: unit = %i (max tracked = %i, max texture units = %i, max image units = %i)",
			unit,
			MAX_MULTITEXTURE_UNITS,
			glConfig.maxTextureUnits,
			glConfig.maxTextureImageUnits
		);
		return;
	}

	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	glClientActiveTextureARB( GL_TEXTURE0_ARB + unit );
	RB_LogComment( "glActiveTextureARB( %i );\nglClientActiveTextureARB( %i );\n", unit, unit );

	backEnd.glState.currenttmu = unit;
}


/*
====================
GL_Cull

This handles the flipping needed when the view being
rendered is a mirored view.
====================
*/
void GL_Cull( int cullType ) {
	if ( backEnd.glState.faceCulling == cullType ) {
		return;
	}

	if ( cullType == CT_TWO_SIDED ) {
		glDisable( GL_CULL_FACE );
	} else  {
		if ( backEnd.glState.faceCulling == CT_TWO_SIDED ) {
			glEnable( GL_CULL_FACE );
		}

		if ( cullType == CT_BACK_SIDED ) {
			if ( backEnd.viewDef->isMirror ) {
				glCullFace( GL_FRONT );
			} else {
				glCullFace( GL_BACK );
			}
		} else {
			if ( backEnd.viewDef->isMirror ) {
				glCullFace( GL_BACK );
			} else {
				glCullFace( GL_FRONT );
			}
		}
	}

	backEnd.glState.faceCulling = cullType;
}

/*
====================
GL_TexEnv
====================
*/
void GL_TexEnv( int env ) {
	tmu_t	*tmu;

	tmu = &backEnd.glState.tmu[backEnd.glState.currenttmu];
	if ( env == tmu->texEnv ) {
		return;
	}

	tmu->texEnv = env;

	switch ( env ) {
	case GL_COMBINE_EXT:
	case GL_MODULATE:
	case GL_REPLACE:
	case GL_DECAL:
	case GL_ADD:
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env );
		break;
	default:
		common->Error( "GL_TexEnv: invalid env '%d' passed\n", env );
		break;
	}
}

/*
=================
GL_ClearStateDelta

Clears the state delta bits, so the next GL_State
will set every item
=================
*/
void GL_ClearStateDelta( void ) {
	backEnd.glState.forceGlState = true;
}

/*
====================
GL_State

This routine is responsible for setting the most commonly changed state
====================
*/
void GL_State( int stateBits ) {
	int	diff;
	
	if ( !r_useStateCaching.GetBool() || backEnd.glState.forceGlState ) {
		// make sure everything is set all the time, so we
		// can see if our delta checking is screwing up
		diff = -1;
		backEnd.glState.forceGlState = false;
	} else {
		diff = stateBits ^ backEnd.glState.glStateBits;
		if ( !diff ) {
			return;
		}
	}

	//
	// check depthFunc bits
	//
	if ( diff & ( GLS_DEPTHFUNC_EQUAL | GLS_DEPTHFUNC_LESS | GLS_DEPTHFUNC_ALWAYS ) ) {
		if ( stateBits & GLS_DEPTHFUNC_EQUAL ) {
			glDepthFunc( GL_EQUAL );
		} else if ( stateBits & GLS_DEPTHFUNC_ALWAYS ) {
			glDepthFunc( GL_ALWAYS );
		} else {
			glDepthFunc( GL_LEQUAL );
		}
	}


	//
	// check blend bits
	//
	if ( diff & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
		GLenum srcFactor, dstFactor;

		switch ( stateBits & GLS_SRCBLEND_BITS ) {
		case GLS_SRCBLEND_ZERO:
			srcFactor = GL_ZERO;
			break;
		case GLS_SRCBLEND_ONE:
			srcFactor = GL_ONE;
			break;
		case GLS_SRCBLEND_DST_COLOR:
			srcFactor = GL_DST_COLOR;
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
			srcFactor = GL_ONE_MINUS_DST_COLOR;
			break;
		case GLS_SRCBLEND_SRC_COLOR:
			srcFactor = GL_SRC_COLOR;
			break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_COLOR:
			srcFactor = GL_ONE_MINUS_SRC_COLOR;
			break;
		case GLS_SRCBLEND_SRC_ALPHA:
			srcFactor = GL_SRC_ALPHA;
			break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
			srcFactor = GL_ONE_MINUS_SRC_ALPHA;
			break;
		case GLS_SRCBLEND_DST_ALPHA:
			srcFactor = GL_DST_ALPHA;
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
			srcFactor = GL_ONE_MINUS_DST_ALPHA;
			break;
		case GLS_SRCBLEND_ALPHA_SATURATE:
			srcFactor = GL_SRC_ALPHA_SATURATE;
			break;
		default:
			srcFactor = GL_ONE;		// to get warning to shut up
			common->Error( "GL_State: invalid src blend state bits\n" );
			break;
		}

		switch ( stateBits & GLS_DSTBLEND_BITS ) {
		case GLS_DSTBLEND_ZERO:
			dstFactor = GL_ZERO;
			break;
		case GLS_DSTBLEND_ONE:
			dstFactor = GL_ONE;
			break;
		case GLS_DSTBLEND_SRC_COLOR:
			dstFactor = GL_SRC_COLOR;
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
			dstFactor = GL_ONE_MINUS_SRC_COLOR;
			break;
		case GLS_DSTBLEND_SRC_ALPHA:
			dstFactor = GL_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
			dstFactor = GL_ONE_MINUS_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_DST_ALPHA:
			dstFactor = GL_DST_ALPHA;
			break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
			dstFactor = GL_ONE_MINUS_DST_ALPHA;
			break;
		default:
			dstFactor = GL_ONE;		// to get warning to shut up
			common->Error( "GL_State: invalid dst blend state bits\n" );
			break;
		}

		glBlendFunc( srcFactor, dstFactor );
	}

	//
	// check depthmask
	//
	if ( diff & GLS_DEPTHMASK ) {
		if ( stateBits & GLS_DEPTHMASK ) {
			glDepthMask( GL_FALSE );
		} else {
			glDepthMask( GL_TRUE );
		}
	}

	//
	// check colormask
	//
	if ( diff & (GLS_REDMASK|GLS_GREENMASK|GLS_BLUEMASK|GLS_ALPHAMASK) ) {
		GLboolean		r, g, b, a;
		r = ( stateBits & GLS_REDMASK ) ? 0 : 1;
		g = ( stateBits & GLS_GREENMASK ) ? 0 : 1;
		b = ( stateBits & GLS_BLUEMASK ) ? 0 : 1;
		a = ( stateBits & GLS_ALPHAMASK ) ? 0 : 1;
		glColorMask( r, g, b, a );
	}

	//
	// fill/line mode
	//
	if ( diff & GLS_POLYMODE_LINE ) {
		if ( stateBits & GLS_POLYMODE_LINE ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		} else {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	//
	// alpha test
	//
	// GL_ALPHA_TEST is fixed-function: it does not exist as an enable in any
	// OpenGL ES profile, and glEnable/glDisable with it raises GL_INVALID_ENUM
	// on every state change that touches these bits. glAlphaFunc is already a
	// no-op stub on ES (GLES/gles_GLStubs.cpp), so the enable was the only half
	// still reaching the driver -- and it was the source of a persistent
	// GL_INVALID_ENUM that outlived every frame it was raised in, corrupting
	// per-draw glGetError checks elsewhere.
	//
	// Backends without fixed-function alpha test evaluate the same state bits
	// in the fragment shader instead (gles_d3: R_GLESD3_AlphaTestReference).
	if ( ( diff & GLS_ATEST_BITS ) && glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES ) {
		switch ( stateBits & GLS_ATEST_BITS ) {
		case 0:
			glDisable( GL_ALPHA_TEST );
			break;
		case GLS_ATEST_EQ_255:
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_EQUAL, 1 );
			break;
		case GLS_ATEST_LT_128:
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_LESS, 0.5 );
			break;
		case GLS_ATEST_GE_128:
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GEQUAL, 0.5 );
			break;
		default:
			assert( 0 );
			break;
		}
	}

	backEnd.glState.glStateBits = stateBits;
}




/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

/*
=============
RB_SetGL2D

This is not used by the normal game paths, just by some tools
=============
*/
void RB_SetGL2D( void ) {
	// set 2D virtual screen size
	glViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	if ( r_useScissor.GetBool() ) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	}
	glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
	glOrtho( 0, 640, 480, 0, 0, 1 );		// always assume 640x480 virtual coordinates
	glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

	GL_State( GLS_DEPTHFUNC_ALWAYS |
			  GLS_SRCBLEND_SRC_ALPHA |
			  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	GL_Cull( CT_TWO_SIDED );

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
}



/*
=============
RB_SetBuffer

=============
*/
static void	RB_SetBuffer( const void *data ) {
	const setBufferCommand_t	*cmd;

	// see which draw buffer we want to render the frame to

	cmd = (const setBufferCommand_t *)data;

	backEnd.frameCount = cmd->frameCount;

	glDrawBuffer( cmd->buffer );

	// clear screen for debugging
	// automatically enable this with several other debug tools
	// that might leave unrendered portions of the screen
	if ( r_clear.GetFloat() || idStr::Length( r_clear.GetString() ) != 1 || r_lockSurfaces.GetBool() || r_singleArea.GetBool() || r_showOverDraw.GetBool() ) {
		float c[3];
		if ( sscanf( r_clear.GetString(), "%f %f %f", &c[0], &c[1], &c[2] ) == 3 ) {
			glClearColor( c[0], c[1], c[2], 1 );
		} else if ( r_clear.GetInteger() == 2 ) {
			glClearColor( 0.0f, 0.0f,  0.0f, 1.0f );
		} else if ( r_showOverDraw.GetBool() ) {
			glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
		} else {
			glClearColor( 0.4f, 0.0f, 0.25f, 1.0f );
		}
		glClear( GL_COLOR_BUFFER_BIT );
	}
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.
===============
*/
void RB_ShowImages( void ) {
	
}


/*
=============
RB_SwapBuffers

=============
*/
/*
====================
r_forceOpaquePresent

idTech 4 writes MEANINGFUL alpha into the colour buffer. Every `maskcolor`
stage does it deliberately -- the dropship hull's second stage is
`maskcolor / map makealpha(...)`, and the HUD's ekg widget is the same -- so
that a later stage can blend through GL_DST_ALPHA.

That is harmless as long as nothing downstream believes the alpha. On the
desktop GL path nothing does: the NSOpenGL surface is opaque and the channel
is ignored. On the ES path ANGLE presents through a Metal-backed layer, the
macOS compositor honours the surface alpha, and every pixel one of those
stages touched becomes transparent -- which reads as black.

The failure is invisible in a screenshot, because R_ReadTiledPixels reads
RGBA and packs down to RGB, discarding exactly the channel that is wrong. A
whole session of captures came back correct while the display was black.

The alpha channel itself comes from SDL3_BuildFramebufferDesc
(OpenGL/gl_ContextSDL3.cpp:342), which requests alphaBits = 8 for every
profile including ES.

Why this is fixed at PRESENT time rather than by asking for a config without
alpha: the back buffer's alpha is load-bearing. gfx/guis/hud/ekg writes a mask
with `maskcolor` and its second stage blends through GL_DST_ALPHA, and both
are 2D draws to the default framebuffer, not to a render texture. Drop the
channel and that widget blends at full strength everywhere instead of through
its mask. The channel has to exist for the frame and be neutral only at the
moment the compositor reads it.

Cost is one alpha-only full-screen write per frame -- a masked clear, so not
the driver's fast-clear path. At 1280x720 that is under a millisecond and it
happens once, after all rendering. The cheaper alternatives are worse: making
the game's final resolve emit alpha 1 would be free but depends on
identifying that draw, and dropping the channel breaks the HUD as above.
====================
*/
static idCVar r_forceOpaquePresent( "r_forceOpaquePresent", "1", CVAR_RENDERER | CVAR_BOOL,
		"write alpha=1 over the back buffer before presenting on ES, where the compositor honours surface alpha" );

static void RB_ForceOpaquePresentAlpha( void ) {
	if ( !r_forceOpaquePresent.GetBool() ) {
		return;
	}
	if ( glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES ) {
		return;
	}
	// nothing to neutralise when the surface carries no alpha, and the write
	// would be pure cost
	if ( glConfig.alphaBits <= 0 ) {
		return;
	}

	// The DEFAULT framebuffer specifically. Whatever the frame left bound is
	// not necessarily it, and clearing a render texture's alpha here would
	// both miss the fix and corrupt a buffer a later frame samples.
	GLint previousFbo = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFbo );
	if ( previousFbo != 0 ) {
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	// alpha only: RGB must survive untouched, and the scissor must not clip
	// this to whatever rect the last pass left behind
	const bool scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST ) == GL_TRUE;
	if ( scissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE );
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}

	if ( previousFbo != 0 ) {
		glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)previousFbo );
	}

	// glColorMask was issued behind GL_State's back, so its cached mask bits no
	// longer describe the driver. Force the next GL_State to re-issue
	// everything rather than delta against a stale record.
	backEnd.glState.forceGlState = true;
}

const void	RB_SwapBuffers( const void *data ) {
	// texture swapping test
	if ( r_showImages.GetInteger() != 0 ) {
		RB_ShowImages();
	}

	// force a gl sync if requested
	if ( r_finish.GetBool() ) {
		glFinish();
	}

	RB_LogComment( "***************** RB_SwapBuffers *****************\n\n\n" );

	if ( !r_frontBuffer.GetBool() ) {
		RB_ApplyResolutionScaleToBackBuffer();
		RB_ApplyCRTToBackBuffer();
		RB_ApplyColorMappingsToBackBuffer();
	}

	// Screenshot readback consumes the fully post-processed back buffer below.
	// Keep that buffer owned by OpenGL until R_ReadTiledPixels has copied it;
	// presenting an EGL window surface may discard its contents immediately.
	// All ordinary frames retain the existing presentation path.
	RB_ForceOpaquePresentAlpha();

	if ( !r_frontBuffer.GetBool() && !tr.takingScreenshot ) {
	    GLimp_SwapBuffers();
	}
}

/*
=============
RB_CopyRender

Copy part of the current framebuffer to an image
=============
*/
const void	RB_CopyRender( const void *data ) {
	const copyRenderCommand_t	*cmd;

	cmd = (const copyRenderCommand_t *)data;

	if ( r_skipCopyTexture.GetBool() ) {
		return;
	}

    RB_LogComment( "***************** RB_CopyRender *****************\n" );

	if (cmd->image) {
		if ( cmd->copyDepth ) {
			cmd->image->CopyDepthbuffer( cmd->x, cmd->y, cmd->imageWidth,
				cmd->imageHeight, cmd->cubeFace );
		} else {
			cmd->image->CopyFramebuffer( cmd->x, cmd->y, cmd->imageWidth,
				cmd->imageHeight, cmd->cubeFace );
		}
	}
}

// jmarshall
/*
=============
RB_SetRenderTexture
=============
*/
static void RB_SetRenderTexture(const void* data) {
	const setRenderTargetCommand_t* cmd;

	cmd = (setRenderTargetCommand_t*)data;

	if ( cmd->renderTexture != NULL && cmd->renderTexture->MakeCurrent() ) {
		backEnd.renderTexture = cmd->renderTexture;
		backEnd.feedbackRenderTexture = cmd->feedbackRenderTexture;
	}
	else {
		backEnd.renderTexture = nullptr;
		backEnd.feedbackRenderTexture = nullptr;
		idRenderTexture::BindNull();
	}
}

static void RB_RestoreTrackedRenderTexture( void ) {
	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->MakeCurrent() ) {
		return;
	}
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;
	idRenderTexture::BindNull();
}

/*
============
RB_ResolveMSAA
============
*/
static void RB_ResolveMSAA(const void* data) {
	const resolveRenderTargetCommand_t* cmd;

	cmd = (resolveRenderTargetCommand_t*)data;
	if ( cmd->resolveDepth ) {
		// A failed or incomplete attempt must not leave a successful stamp from
		// an earlier resolve of the same target in this backend frame.
		RB_InvalidateTemporalDepthStamp( cmd->destRenderTexture );
	}

	if ( cmd->msaaRenderTexture == NULL || cmd->destRenderTexture == NULL ||
		!cmd->msaaRenderTexture->EnsureDeviceHandle() ||
		!cmd->destRenderTexture->EnsureDeviceHandle() ) {
		RB_RestoreTrackedRenderTexture();
		return;
	}

	const GLuint sourceHandle = cmd->msaaRenderTexture->GetDeviceHandle();
	const GLuint destinationHandle = cmd->destRenderTexture->GetDeviceHandle();
	if ( sourceHandle == 0 || destinationHandle == 0 ) {
		RB_RestoreTrackedRenderTexture();
		return;
	}

	int width = cmd->msaaRenderTexture->GetWidth();
	int height = cmd->msaaRenderTexture->GetHeight();
	if ( width <= 0 || height <= 0 ) {
		RB_RestoreTrackedRenderTexture();
		return;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceHandle);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destinationHandle);

	// A resolve is a whole-target operation, and glBlitFramebuffer is clipped
	// by the scissor; it must not depend on whatever box the previous view
	// left. Measured on Android: a leaked per-surface box clipped this blit to
	// one door frame and froze everything outside it.
	const GLboolean resolveScissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	if ( resolveScissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}

	// Resolve all of the render targets.
	const int colorImageCount = cmd->msaaRenderTexture->GetNumColorImages();
	for (int i = 0; i < colorImageCount; i++)
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
		glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
		glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	GL_CheckErrors();

	// Resolve the depth buffer only when explicitly requested.
	if ( cmd->resolveDepth ) {
		glReadBuffer( GL_NONE );
		glDrawBuffer( GL_NONE );
		glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST );

		const GLenum depthResolveError = glGetError();
		if ( depthResolveError == GL_NO_ERROR ) {
			RB_StampTemporalDepthResolved( cmd->destRenderTexture,
				cmd->frameNumber, cmd->historyGeneration );
		} else {
			static int lastDepthResolveWarningGeneration = -1;
			if ( lastDepthResolveWarningGeneration != tr.glContextGeneration ) {
				common->Warning(
					"RB_ResolveMSAA: depth resolve failed with GL error 0x%04x; temporal presentation will use spatial reconstruction",
					static_cast<unsigned int>( depthResolveError ) );
				lastDepthResolveWarningGeneration = tr.glContextGeneration;
			}
			GL_CheckErrors();
		}
	}

	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	if ( resolveScissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}

	// restore the tracked render target so backEnd.renderTexture stays in
	// sync with the bound framebuffer
	RB_RestoreTrackedRenderTexture();
}

/*
============
RB_ClearRenderTarget
============
*/
static void RB_ClearRenderTarget(const void* data) {
	const renderClearBufferCommand_t* cmd;

	cmd = (renderClearBufferCommand_t*)data;

	// this command means "clear the whole target": it must not be clipped by
	// whatever scissor box the previous view left (same hazard as the alpha
	// clear at the top of this file, and measured on Android clipping the
	// postprocess target's clear to one surface's box)
	const GLboolean clearScissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	if ( clearScissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}

	if ( cmd->clearColor ) {
		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	}
	if ( cmd->clearDepth ) {
		glDepthMask( GL_TRUE );
	}

	if (cmd->clearDepth) {
		glStencilMask(0xff);
		// some cards may have 7 bit stencil buffers, so don't assume this
		// should be 128
		glClearStencil( R_SafeStencilClearValue() );
		glClearDepth(cmd->clearDepthValue);
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	if (cmd->clearColor) {
		glClearColor(cmd->clearColorValue[0], cmd->clearColorValue[1], cmd->clearColorValue[2], cmd->clearColorValue[3]);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	glClearDepth(1.0f);

	if ( clearScissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}

	GL_ClearStateDelta();

}
// jmarshall end

/*
====================
RB_ExecuteBackEndCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
int		backEndStartTime, backEndFinishTime;

/*
====================
RB_ClassicSubview_CopyOwned

The shared subview corridor owns the *capture edge*, not the child scene
walker.  Once the sealed child-scene/capture record exactly matches the legacy
command, source the copy arguments from that record rather than from mutable
command storage.  The child draw still deliberately uses the established
classic renderer until its complete material/light ownership has a dedicated
domain.
====================
*/
static bool RB_ClassicSubview_CopyOwned( const classicSubviewDomainView_t &view ) {
	if ( view.captureImage == NULL || !view.captureImage->IsLoaded()
			|| view.captureWidth <= 0 || view.captureHeight <= 0 ) {
		return false;
	}

	RB_LogComment( "***************** RB_ClassicSubview_CopyOwned *****************\n" );
	return view.captureCopyDepth
		? view.captureImage->CopyDepthbuffer( view.captureX, view.captureY,
			view.captureWidth, view.captureHeight, view.captureCubeFace )
		: view.captureImage->CopyFramebuffer( view.captureX, view.captureY,
			view.captureWidth, view.captureHeight, view.captureCubeFace );
}

static const classicSubviewDomainView_t *RB_ClassicSubview_Preflight(
		const viewDef_t *viewDef ) {
	if ( !r_rendererSharedSubview.GetBool() || viewDef == NULL
			|| !viewDef->isSubview ) {
		return NULL;
	}
	const classicSubviewDomainView_t *view =
		R_ClassicSubviewDomain_FindView( viewDef );
	if ( view == NULL ) {
		R_ClassicSubviewDomain_RecordBackendFallback( viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
			CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NOT_READY, 0 );
		return NULL;
	}
	if ( view->backendOutcome[CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
			!= CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
		// A descendant or sibling has already rejected this sealed nested
		// transaction. Continue with the untouched command stream; do not turn
		// the remaining parent edge into a mixed shared/classic ownership case.
		return NULL;
	}
	const bool nestedDynamicReady =
		R_ClassicCinematicPostDomain_SubviewTransactionReady( viewDef,
			CLASSIC_CINEMATIC_POST_BACKEND_GL );
	if ( !view->ready || !R_ClassicSubviewDomain_ViewSemanticsMatch( *view )
			|| ( R_ClassicSubviewDomain_IsCaptureBacked( *view )
				&& ( view->captureImage == NULL || !view->captureImage->IsLoaded() ) )
			|| !R_ClassicSubviewDomain_ReadyForBackend( *view,
				CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ) ) {
		R_ClassicSubviewDomain_RecordBackendFallback( viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
			!view->ready ? view->failure
				: ( !R_ClassicSubviewDomain_ViewSemanticsMatch( *view )
					? CLASSIC_SUBVIEW_DOMAIN_FAILURE_VIEW_SEMANTICS_MISMATCH
					: ( !nestedDynamicReady
						? CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTED_CINEMATIC_POST_INCOMPLETE
						: ( !R_ClassicSubviewDomain_ReadyForBackend( *view,
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL )
						? CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTING_INCOMPLETE
						: CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_REJECTED ) ) ),
			!view->ready ? view->failureDetail : 1 );
		return NULL;
	}
	return view;
}

static void RB_DrawSharedDirectSubview( const void *data,
		const classicSubviewDomainView_t &view ) {
	// The direct SS_SUBVIEW path has no RC_COPY_RENDER edge. Its sealed view
	// semantics select the mature full 3D executor, then ownership is published
	// only after that complete child view returns.
	RB_DrawView( data );
	if ( view.backendOutcome[CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
			!= CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
		// A nested cinematic/post range rejected while the mature child-view
		// executor was active. Its rollback already covered this complete tree.
		return;
	}
	if ( !R_ClassicSubviewDomain_RecordDirectOwned( view.viewDef,
			CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL ) ) {
		common->Warning( "OpenGL: shared direct subview coverage rejected after committed view" );
	}
}

// Render-demo playback is a complete recorded 3D frame. The shared boundary
// seals its packet/session provenance, then deliberately dispatches the mature
// full view executor; it cannot safely substitute only one of that view's
// depth, interaction, ambient, fog, subview, or feedback ranges.
static bool RB_DrawSharedRenderDemoView( const void *data ) {
	const drawSurfsCommand_t *cmd =
		reinterpret_cast<const drawSurfsCommand_t *>( data );
	const viewDef_t *viewDef = cmd != NULL ? cmd->viewDef : NULL;
	if ( !R_ClassicSpecialFrameDomain_ReadyForBackend( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
			CLASSIC_SPECIAL_FRAME_BACKEND_GL ) ) {
		R_ClassicSpecialFrameDomain_RecordBackendFallback( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
			CLASSIC_SPECIAL_FRAME_BACKEND_GL,
			CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_NOT_READY, 0 );
		return false;
	}
	RB_DrawView( data );
	if ( !R_ClassicSpecialFrameDomain_RecordOwned( viewDef,
			CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO,
			CLASSIC_SPECIAL_FRAME_BACKEND_GL, viewDef->numDrawSurfs ) ) {
		common->Warning( "OpenGL: shared render-demo coverage rejected after committed view" );
	}
	return true;
}

void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	// r_debugRenderToTexture
	int	c_draw3d = 0, c_draw2d = 0, c_setBuffers = 0, c_swapBuffers = 0, c_copyRenders = 0, c_specialEffects = 0, c_renderTargetOps = 0;

	R_GLDebugOutput_FlushMessages();
	// Clear frame-local pointers and ownership before every command stream,
	// including the empty-frame fast path below.
	R_ClassicGuiDomain_ResetFrame();
	R_ClassicCinematicPostDomain_ResetFrame();
	R_ClassicSpecialFrameDomain_ResetFrame();
	R_ClassicWorldAmbientDomain_ResetFrame();
	R_ClassicInteractionDomain_ResetFrame();
	R_ClassicFogBlendDomain_ResetFrame();
	R_ClassicSubviewDomain_ResetFrame();
	R_ModernClusteredLighting_ResetDecalsForFrame();
	if ( cmds->commandId == RC_NOP && !cmds->next ) {
		return;
	}

	R_GLStateCache_BeginFrame();
	// The legacy backend issues raw GL between the last modern pass of the previous
	// frame and this frame's modern submits; cached state from last frame is stale.
	R_GLStateCache_InvalidateAll( "backend frame begin" );
	if ( R_ScenePackets_SidePipelineRequired() ) {
		const int packetBuildStart = Sys_Milliseconds();
		const idScenePacketFrame *scenePackets = NULL;
		if ( R_ScenePackets_FrontEndFrameAvailable() ) {
			scenePackets = &R_ScenePackets_FrontEndFrame();
		} else {
			R_ScenePackets_BuildLegacyCommandStream( cmds, rg_backendScenePacketFrame );
			scenePackets = &rg_backendScenePacketFrame;
		}
		R_RendererMetrics_AddPacketBuildMsec( Sys_Milliseconds() - packetBuildStart );
		R_ScenePackets_LogIfVerbose( *scenePackets );

		const int graphBuildStart = Sys_Milliseconds();
		idRenderGraph legacyGraph;
		R_RenderGraph_BuildFromScenePackets( *scenePackets, legacyGraph );
		R_RendererMetrics_AddGraphBuildMsec( Sys_Milliseconds() - graphBuildStart );
		R_RenderGraph_LogIfVerbose( legacyGraph );
		{
			const scenePacketFrameStats_t &packetStats = scenePackets->Stats();
			R_RendererMetrics_RecordScenePackets( packetStats );
			const renderGraphStats_t &graphStats = legacyGraph.Stats();
			R_RendererMetrics_RecordRenderGraph(
				graphStats.graphPasses,
				graphStats.passPackets,
				graphStats.scenePackets,
				graphStats.drawPackets,
				graphStats.commandPackets,
				graphStats.resources,
				graphStats.importedResources,
				graphStats.transientResources,
				graphStats.aliasableTransientResources,
				graphStats.resourceAccesses,
				graphStats.readAccesses,
				graphStats.writeAccesses,
				graphStats.clearOps,
				graphStats.resolveOps,
				graphStats.invalidateOps,
				graphStats.presentOps,
				graphStats.overflow );
		}
		R_RenderGraphResources_PrepareFrame( legacyGraph );
		R_RendererMetrics_RecordRenderGraphResources( R_RenderGraphResources_Stats() );
		R_MaterialResourceTable_PrepareFrame( *scenePackets );
		R_RendererMetrics_RecordMaterialResourceTable( R_MaterialResourceTable_Stats() );
		if ( r_rendererSharedGui.GetBool()
				|| r_rendererSharedInWorldGui.GetBool() ) {
			R_ClassicGuiDomain_PrepareFrame( *scenePackets );
		}
		// Special-view topology must be sealed before cinematic/post ranges can
		// join one of its atomic root transactions.
		if ( r_rendererSharedSubview.GetBool() ) {
			R_ClassicSubviewDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedCinematicPost.GetBool() ) {
			R_ClassicCinematicPostDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedSpecialFrame.GetBool() ) {
			R_ClassicSpecialFrameDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldAmbient.GetBool() ) {
			R_ClassicWorldAmbientDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldInteraction.GetBool() ) {
			R_ClassicInteractionDomain_PrepareFrame( *scenePackets );
		}
		if ( r_rendererSharedWorldFogBlend.GetBool() ) {
			R_ClassicFogBlendDomain_PrepareFrame( *scenePackets );
		}
		R_ModernGLExecutor_PrepareFrame( *scenePackets, legacyGraph );
		rg_modernStatMirrorsZeroed = false;	// active frame wrote real stats; re-zero on next dormant frame
	} else {
		// the zeroed stat mirrors cannot change while the side pipeline is
		// skipped: refresh them once on the active->dormant transition, and
		// per-frame only while metrics output actually consumes them
		if ( !rg_modernStatMirrorsZeroed || r_rendererMetrics.GetInteger() > 0 ) {
			scenePacketFrameStats_t packetStats;
			memset( &packetStats, 0, sizeof( packetStats ) );
			R_RendererMetrics_RecordScenePackets( packetStats );
			R_RendererMetrics_RecordRenderGraph( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false );
			renderGraphResourceManagerStats_t graphResourceStats;
			memset( &graphResourceStats, 0, sizeof( graphResourceStats ) );
			R_RendererMetrics_RecordRenderGraphResources( graphResourceStats );
			materialResourceTableStats_t materialStats;
			memset( &materialStats, 0, sizeof( materialStats ) );
			R_RendererMetrics_RecordMaterialResourceTable( materialStats );
			rendererClusteredLightingStats_t clusterStats;
			memset( &clusterStats, 0, sizeof( clusterStats ) );
			R_RendererMetrics_RecordClusteredLighting( clusterStats );
			rg_modernStatMirrorsZeroed = true;
		}
		R_ModernGLExecutor_SkipFrame();
	}
	backEndStartTime = Sys_Milliseconds();
	R_RendererMetrics_BeginGpuBackendFrame();
	R_GLStateCache_LegacyHandoffReset( "legacy ARB2 backend" );

	// needed for editor rendering
	RB_SetDefaultGLState();
	backEnd.renderTexture = NULL;
	backEnd.postProcessTexelSize = tr.postProcessTexelSize;
	backEnd.resolutionScaleWidth = tr.resolutionScaleWidth;
	backEnd.resolutionScaleHeight = tr.resolutionScaleHeight;
	backEnd.postProcessSourceColorSpace = tr.postProcessSourceColorSpace;
	backEnd.postProcessSMAAQuality = tr.postProcessSMAAQuality;
	idRenderTexture::BindNull();
	const classicSubviewDomainView_t *pendingSharedSubview = NULL;

	// upload any image loads that have completed
	//globalImages->CompleteBackgroundImageLoads();

	for ( ; cmds ; cmds = (const emptyCommand_t *)cmds->next ) {
		if ( pendingSharedSubview != NULL
				&& cmds->commandId != RC_COPY_RENDER ) {
			if ( pendingSharedSubview->backendOutcome[
					CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
					== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
				R_ClassicSubviewDomain_RecordBackendFallback(
					pendingSharedSubview->viewDef, CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
					CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH,
					static_cast<int>( cmds->commandId ) );
			}
			pendingSharedSubview = NULL;
		}
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW: {
			const viewDef_t *drawView = ((const drawSurfsCommand_t *)cmds)->viewDef;
			const classicSubviewDomainView_t *sharedSubview =
				RB_ClassicSubview_Preflight( drawView );
			pendingSharedSubview = sharedSubview != NULL
				&& R_ClassicSubviewDomain_IsCaptureBacked( *sharedSubview )
				? sharedSubview : NULL;
			R_RendererMetrics_BeginGpuTimer( drawView->viewEntitys ? RENDERER_GPU_TIMER_DRAW3D : RENDERER_GPU_TIMER_DRAW2D );
			const bool sharedRenderDemo = drawView->viewEntitys != NULL
				&& r_rendererSharedSpecialFrame.GetBool()
				&& R_ClassicSpecialFrameDomain_FindRenderDemoView( drawView ) != NULL;
			const bool sharedCinematicRoot = !drawView->viewEntitys
				&& r_rendererSharedCinematicPost.GetBool()
				&& R_ClassicCinematicPostDomain_FindRootCinematicView( drawView ) != NULL;
			if ( sharedSubview != NULL && R_ClassicSubviewDomain_IsDirect( *sharedSubview ) ) {
				RB_DrawSharedDirectSubview( cmds, *sharedSubview );
			} else if ( sharedRenderDemo ) {
				if ( !RB_DrawSharedRenderDemoView( cmds ) ) {
					RB_DrawView( cmds );
				}
			} else if ( sharedCinematicRoot ) {
				if ( !RB_DrawSharedCinematicRootView( drawView ) ) {
					RB_DrawView( cmds );
				}
			} else if ( !drawView->viewEntitys
					&& r_rendererSharedGui.GetBool() ) {
				// The shared owner is view-atomic.  If its complete preflight or
				// execution gate rejects this view, execute the untouched classic
				// view immediately; never mix shared and aggregate GUI stages.
				if ( !RB_DrawSharedGuiView( drawView ) ) {
					RB_DrawView( cmds );
				}
			} else if ( !drawView->viewEntitys && R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_GUI ) ) {
				R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_GUI );
			} else {
				RB_DrawView( cmds );
			}
			R_RendererMetrics_EndGpuTimer();
			if ( ((const drawSurfsCommand_t *)cmds)->viewDef->viewEntitys ) {
				c_draw3d++;
			}
			else {
				c_draw2d++;
			}
			break;
		}
		case RC_DRAW_SPECIAL_EFFECTS:
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_SPECIAL_EFFECTS );
			if ( r_rendererSharedSpecialFrame.GetBool()
					&& R_ClassicSpecialFrameDomain_FindRavenEffectsView(
						reinterpret_cast<const drawSurfsCommand_t *>( cmds )->viewDef ) != NULL
					&& !R_ClassicSpecialFrameDomain_ReadyForBackend(
						reinterpret_cast<const drawSurfsCommand_t *>( cmds )->viewDef,
						CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
						CLASSIC_SPECIAL_FRAME_BACKEND_GL ) ) {
				R_ClassicSpecialFrameDomain_RecordBackendFallback(
					reinterpret_cast<const drawSurfsCommand_t *>( cmds )->viewDef,
					CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS,
					CLASSIC_SPECIAL_FRAME_BACKEND_GL,
					CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_NOT_READY, 0 );
				RB_DrawSpecialEffects( cmds );
			} else if ( R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SPECIAL_EFFECTS ) ) {
				R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_SPECIAL_EFFECTS );
			} else {
				RB_DrawSpecialEffects( cmds );
			}
			R_RendererMetrics_EndGpuTimer();
			c_specialEffects++;
			break;
// jmarshall
		case RC_SET_RENDERTEXTURE:
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_RENDER_TARGET );
			RB_SetRenderTexture(cmds);
			R_RendererMetrics_EndGpuTimer();
			c_renderTargetOps++;
			break;
		case RC_RESOLVE_MSAA:
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_RENDER_TARGET );
			RB_ResolveMSAA(cmds);
			R_RendererMetrics_EndGpuTimer();
			c_renderTargetOps++;
			break;
		case RC_RESOLVE_TEMPORAL_PRESENTATION: {
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_RENDER_TARGET );
			resolveTemporalPresentationCommand_t executionCommand =
				*reinterpret_cast<const resolveTemporalPresentationCommand_t *>( cmds );
			if ( tr.takingScreenshot ) {
				executionCommand.captureFrame = true;
				executionCommand.historyValid = false;
			}
			(void)RB_ResolveTemporalPresentation( executionCommand );
			R_RendererMetrics_EndGpuTimer();
			c_renderTargetOps++;
			break;
		}
		case RC_CLEAR_RENDERTARGET:
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_RENDER_TARGET );
			RB_ClearRenderTarget(cmds);
			R_RendererMetrics_EndGpuTimer();
			c_renderTargetOps++;
			break;
		case RC_SET_POSTPROCESS_SOURCE_SIZE:
			backEnd.postProcessTexelSize = ((const setPostProcessSourceSizeCommand_t *)cmds)->texelSize;
			break;
		case RC_SET_POSTPROCESS_SOURCE_COLOR_SPACE:
			backEnd.postProcessSourceColorSpace = ((const setPostProcessSourceColorSpaceCommand_t *)cmds)->colorSpace;
			break;
		case RC_SET_POSTPROCESS_SMAA_QUALITY:
			backEnd.postProcessSMAAQuality = ((const setPostProcessSMAAQualityCommand_t *)cmds)->quality;
			break;
// jmarshall end
		case RC_SET_BUFFER:
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_SET_BUFFER );
			RB_SetBuffer( cmds );
			R_RendererMetrics_EndGpuTimer();
			c_setBuffers++;
			break;
		case RC_SWAP_BUFFERS: {
			R_ModernGLExecutor_ComposeVisibleFrame();
			R_ModernGLExecutor_DrawDepthDebugOverlay();
			R_ModernGLExecutor_DrawGBufferDebugOverlay();
			R_ModernGLExecutor_DrawDeferredDebugOverlay();
			R_ModernClusteredLighting_DrawDebugOverlay();
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_SWAP_BUFFERS );
			const int presentStart = Sys_Milliseconds();
			RB_SwapBuffers( cmds );
			R_RendererMetrics_AddPresentMsec( Sys_Milliseconds() - presentStart );
			R_RendererMetrics_EndGpuTimer();
			c_swapBuffers++;
			break;
		}
		case RC_COPY_RENDER: {
			R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_COPY_RENDER );
			const copyRenderCommand_t *copy =
				reinterpret_cast<const copyRenderCommand_t *>( cmds );
			const bool sharedCaptureMatches = pendingSharedSubview != NULL
				&& pendingSharedSubview->backendOutcome[
					CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
					== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED
				&& !r_skipCopyTexture.GetBool()
				&& R_ClassicSubviewDomain_CaptureMatches( *pendingSharedSubview,
					copy->image, copy->x, copy->y, copy->imageWidth,
					copy->imageHeight, copy->cubeFace, copy->copyDepth );
			const bool copied = sharedCaptureMatches
				? RB_ClassicSubview_CopyOwned( *pendingSharedSubview )
				: ( !r_skipCopyTexture.GetBool()
					? ( RB_CopyRender( cmds ), true ) : false );
			if ( pendingSharedSubview != NULL
					&& pendingSharedSubview->backendOutcome[
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
						== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
				if ( sharedCaptureMatches && copied ) {
					R_ClassicSubviewDomain_RecordOwned(
						pendingSharedSubview->viewDef,
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
						pendingSharedSubview->captureImage,
						pendingSharedSubview->captureX,
						pendingSharedSubview->captureY,
						pendingSharedSubview->captureWidth,
						pendingSharedSubview->captureHeight,
						pendingSharedSubview->captureCubeFace,
						pendingSharedSubview->captureCopyDepth );
				} else if ( r_skipCopyTexture.GetBool() || !copied ) {
					R_ClassicSubviewDomain_RecordBackendFallback(
						pendingSharedSubview->viewDef,
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
						CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_REJECTED, 2 );
				} else {
					R_ClassicSubviewDomain_RecordBackendFallback(
						pendingSharedSubview->viewDef,
						CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
						CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH, 2 );
				}
			}
			pendingSharedSubview = NULL;
			R_RendererMetrics_EndGpuTimer();
			c_copyRenders++;
			break;
		}
		default:
			common->Error( "RB_ExecuteBackEndCommands: bad commandId" );
			break;
		}
	}

	if ( pendingSharedSubview != NULL ) {
		if ( pendingSharedSubview->backendOutcome[
				CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL]
				== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED ) {
			R_ClassicSubviewDomain_RecordBackendFallback(
				pendingSharedSubview->viewDef, CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL,
				CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH, 3 );
		}
	}
	R_ClassicSpecialFrameDomain_FinalizeBackendFrame(
		CLASSIC_SPECIAL_FRAME_BACKEND_GL );

	// go back to the default texture so the editor doesn't mess up a bound image
	GL_SelectTexture( 0 );
	R_BindTextureForDirectAccess( GL_TEXTURE_2D, 0 );

	// stop rendering on this thread
	R_RendererMetrics_EndGpuBackendFrame();
	backEndFinishTime = Sys_Milliseconds();
	backEnd.pc.msec = backEndFinishTime - backEndStartTime;
	R_RendererMetrics_RecordGLStateCache( R_GLStateCache_Stats() );
	R_RendererMetrics_RecordBackendCommands( c_draw3d, c_draw2d, c_setBuffers, c_swapBuffers, c_copyRenders, c_specialEffects, c_renderTargetOps );

	if ( r_debugRenderToTexture.GetInteger() == 1 ) {
		common->Printf( "3d: %i, 2d: %i, SetBuf: %i, SwpBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_swapBuffers, c_copyRenders, backEnd.c_copyFrameBuffer );
		backEnd.c_copyFrameBuffer = 0;
	}
}
