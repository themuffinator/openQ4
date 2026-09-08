// Copyright (C) 2026 DarkMatter Productions
// Original GLES support by Emile Belanger (emileb).
// https://github.com/emileb/openQ4/tree/gles-shader-variants
//
/*
===============================================================================

	gles_d3 -- view setup and pass ordering.

	The RB_STD_DrawView equivalent (draw_common.cpp:10647), rebuilt for
	OpenGL ES 3.0.

	RB_GLESD3_DrawView establishes the view -- viewport, scissor,
	depth/stencil clear -- and then runs the passes in the order
	RB_STD_DrawView runs them: depth fill (D3), interactions with stencil
	shadows (D4/D5), the material walk (D2), fog and blend lights (D6), and
	finally the post-process walk (D7, not yet drawable).

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

/*
====================
r_glesD3DebugClearColor

D0 instrumentation, and it earns its keep past D0: a distinctive clear makes
"this backend is running and drawing nothing" visually distinct from "this
backend never got the frame", which on a black-screen bring-up are otherwise
the same picture.

NOT archived, and renamed from r_glesD3ClearColor for that reason. The legacy
RB_BeginDrawingView (tr_render.cpp:759) never clears colour on a 3D view, and
there is a good reason: a portal sky renders as a SUBVIEW into this same
target before the main view runs, so clearing colour here erases it. The D0
bring-up default was "0.15 0 0.2", CVAR_ARCHIVE wrote it into openQ4Config.cfg,
and it kept overriding the "0 0 0" default long after D0 -- every
game/airdefense1 capture from D0 through D7a has a magenta sky where the
overcast skybox should be, and it read as a missing texgen rather than as a
clear.

That is the third time in this port an archived cvar has produced a false
reading; a debug knob that
survives the session it was set in is the shape of the problem. Renaming means
the stale `seta` line in an existing config cannot reach this cvar at all.
====================
*/
static idCVar r_glesD3DebugClearColor( "r_glesD3DebugClearColor", "0 0 0", CVAR_RENDERER,
		"gles_d3: DEBUG -- colour the 3D view clears to before any pass draws. Non-black erases a portal sky, which renders into this target as a subview. \"0 0 0\" for black (the correct value)" );

/*
====================
r_glesD3Report

Per-view telemetry, capped at the first few views of a run. Screenshots have
produced three separate false conclusions on this ES context across the
ModernGL bring-up; an in-frame glReadPixels next to the state that produced
it is the measurement that has actually held up.
====================
*/
idCVar r_glesD3Report( "r_glesD3Report", "0", CVAR_RENDERER | CVAR_INTEGER,
		"gles_d3: 1 = report each of the first views drawn, with an in-frame pixel readback; 2 = also name the first stage-draw call that raises a GL error" );

/*
====================
r_glesD3PresentSceneTarget

Quake 4's game code renders the world into a render texture
(idRenderSystemLocal::SetRenderTexture, RenderSystem.cpp:1914) and resolves it
to the back buffer with a fullscreen material draw -- `postprocess/
resolvepostprocess`. That draw is a material pass, so it does not exist until
D2, and until then a correctly rendered scene sits in an offscreen target that
nothing ever presents. Measured at D0: the view clears to its colour in
target 8 while the back buffer stays black.

This blits the bound target to the back buffer at the end of the view, which
is the same place the ARB2 path presents from
(RB_PresentSceneRenderTargetToBackBuffer, draw_common.cpp:10841). Bring-up
scaffolding: once the resolve material draws for real, this becomes redundant
and should go to 0.
====================
*/
static idCVar r_glesD3PresentSceneTarget( "r_glesD3PresentSceneTarget", "1", CVAR_RENDERER | CVAR_BOOL,
		"gles_d3: blit the scene render target to the back buffer at the end of a 3D view (bring-up scaffolding; the resolve material replaces it)" );

/*
====================
RB_GLESD3_SetScissor
====================
*/
bool RB_GLESD3_SetScissor( const idScreenRect &rect ) {
	if ( rect.IsEmpty() ) {
		// An empty rect covers no pixels. Issuing it means glScissor with a
		// negative width, which GL rejects and ignores -- leaving the previous
		// box live while the tracker moves on. Skip the draw instead.
		return false;
	}

	if ( !r_useScissor.GetBool() || backEnd.currentScissor.Equals( rect ) ) {
		return true;
	}

	backEnd.currentScissor = rect;

	const GLint x = backEnd.viewDef->viewport.x1 + rect.x1;
	const GLint y = backEnd.viewDef->viewport.y1 + rect.y1;
	const GLsizei w = rect.x2 + 1 - rect.x1;
	const GLsizei h = rect.y2 + 1 - rect.y1;
	glScissor( x, y, w, h );

	return true;
}

/*
====================
RB_GLESD3_SamplePixels

Five points, not one.

Every readback in this backend sampled vidWidth/2, vidHeight/2 -- and on
game/airdefense1 the artifact's live box is the door frame's scissor rect,
718,285..1245,868, which CONTAINS that point. So every measurement taken while
chasing the frozen frame was taken from the one region still working, and all
of them came back healthy.

A centre sample cannot distinguish "the frame rendered" from "a box in the
middle of the frame rendered". The corners can.
====================
*/
static void RB_GLESD3_SamplePixels( idStr &out ) {
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
	const int px[ 5 ] = { w / 10, w / 2, w * 9 / 10, w / 10, w * 9 / 10 };
	const int py[ 5 ] = { h / 10, h / 2, h / 2,      h * 9 / 10, h * 9 / 10 };

	out = "";
	for ( int i = 0; i < 5; i++ ) {
		GLubyte c[ 4 ] = { 0, 0, 0, 0 };
		glReadPixels( px[i], py[i], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c );
		out += va( "(%i,%i)=%i,%i,%i ", px[i], py[i], c[0], c[1], c[2] );
	}
}

static int rb_glesD3Reported3DViews = 0;
static int rb_glesD3Reported2DViews = 0;
static GLenum rb_glesD3EntryError = 0;

/*
====================
RB_GLESD3_PresentSceneTarget

glBlitFramebuffer is ES 3.0 core. Two details it is easy to lose:
scissor clips a blit, and the read buffer belongs to the read framebuffer --
both are set explicitly here rather than inherited.
====================
*/
static void RB_GLESD3_PresentSceneTarget( void ) {
	if ( !r_glesD3PresentSceneTarget.GetBool() || backEnd.renderTexture == NULL ) {
		return;
	}
	const GLuint sourceFbo = (GLuint)backEnd.renderTexture->GetDeviceHandle();
	if ( sourceFbo == 0 ) {
		return;
	}

	const int width = glConfig.vidWidth;
	const int height = glConfig.vidHeight;

	glBindFramebuffer( GL_READ_FRAMEBUFFER, sourceFbo );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );

	const bool scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST ) == GL_TRUE;
	if ( scissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}
	glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}

	// leave the target framebuffer bound the way the frame loop expects, and
	// re-bind the render texture so later commands still address it
	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, sourceFbo );
}

/*
====================
RB_GLESD3_SafeStencilClearValue

Mirrors the static helper in tr_render.cpp: some hardware carries fewer than
8 stencil bits, so the "shadowed" midpoint cannot be assumed to be 128.
====================
*/
static ID_INLINE GLint RB_GLESD3_SafeStencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

/*
====================
RB_GLESD3_BeginDrawingView

RB_BeginDrawingView (tr_render.cpp:737) without the fixed-function halves.
glMatrixMode/glLoadMatrixf are no-op stubs on ES; the projection matrix
reaches the shaders as a uniform from D1 on, so it is simply not set here.

Everything else is carried across unchanged, including the detail that a 2D
view clears neither depth nor stencil and disables both tests.
====================
*/
static void RB_GLESD3_BeginDrawingView( void ) {
	const viewDef_t *viewDef = backEnd.viewDef;

	glViewport( tr.viewportOffset[0] + viewDef->viewport.x1,
		tr.viewportOffset[1] + viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );

	// the scissor may be smaller than the viewport for subviews
	glScissor( tr.viewportOffset[0] + viewDef->viewport.x1 + viewDef->scissor.x1,
		tr.viewportOffset[1] + viewDef->viewport.y1 + viewDef->scissor.y1,
		viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
		viewDef->scissor.y2 + 1 - viewDef->scissor.y1 );
	backEnd.currentScissor = viewDef->scissor;

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	if ( viewDef->viewEntitys ) {
		float clear[ 3 ] = { 0.0f, 0.0f, 0.0f };
		const bool clearColor =
				sscanf( r_glesD3DebugClearColor.GetString(), "%f %f %f", &clear[0], &clear[1], &clear[2] ) == 3
				&& ( clear[0] != 0.0f || clear[1] != 0.0f || clear[2] != 0.0f );

		glStencilMask( 0xff );
		glClearStencil( RB_GLESD3_SafeStencilClearValue() );
		if ( clearColor ) {
			glClearColor( clear[0], clear[1], clear[2], 1.0f );
			glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		} else {
			glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		}
		glEnable( GL_DEPTH_TEST );
	} else {
		// 2D: no depth or stencil buffer to clear, and both tests off
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_STENCIL_TEST );
	}

	backEnd.glState.faceCulling = -1;		// force face culling to set next time
	GL_Cull( CT_FRONT_SIDED );
}

/*
====================
r_glesD3TestTriangle

D1 proof: one triangle in world space, placed relative to the view so it is
visible on any map rather than at some hardcoded map coordinate. It exercises
the whole path in one draw -- program, MVP uniform, the interleaved
idDrawVert attribute binding and an indexed draw out of a real buffer object.

Kept past D1 as a bring-up probe: when a later pass draws nothing, this
answers "is the draw path broken, or is the pass not submitting?" without
adding instrumentation.
====================
*/
static idCVar r_glesD3TestTriangle( "r_glesD3TestTriangle", "0", CVAR_RENDERER | CVAR_BOOL,
		"gles_d3: draw a test triangle in front of the view through the full draw path" );

static GLuint	rb_glesD3TestVbo = 0;
static GLuint	rb_glesD3TestIbo = 0;
static bool		rb_glesD3ResourcesReady = false;
static bool		rb_glesD3ResourcesFailed = false;

/*
====================
RB_GLESD3_EnsureResources
====================
*/
bool RB_GLESD3_EnsureResources( void ) {
	if ( rb_glesD3ResourcesReady ) {
		return true;
	}
	if ( rb_glesD3ResourcesFailed ) {
		return false;
	}

	R_GLESD3_Draw_Init();
	if ( !R_GLESD3_Programs_Init() ) {
		// fail once and stay failed: retrying per frame would flood the log
		// with the same compile errors at frame rate
		rb_glesD3ResourcesFailed = true;
		common->Warning( "gles_d3: shader programs unavailable; the backend will clear only" );
		return false;
	}

	rb_glesD3ResourcesReady = true;
	return true;
}

/*
====================
RB_GLESD3_DrawTestTriangle

World-space vertices built each frame from the view origin and axis, so the
triangle sits 64 units ahead of the camera whatever the map is.
====================
*/
static void RB_GLESD3_DrawTestTriangle( void ) {
	const glesProgram_t *program = R_GLESD3_Program( GLESD3_PROGRAM_DEBUG );
	if ( program == NULL ) {
		return;
	}

	const renderView_t &view = backEnd.viewDef->renderView;
	const idVec3 origin = view.vieworg;
	const idVec3 forward = view.viewaxis[0];
	const idVec3 left = view.viewaxis[1];
	const idVec3 up = view.viewaxis[2];

	const idVec3 center = origin + forward * 64.0f;
	idDrawVert verts[ 3 ];
	memset( verts, 0, sizeof( verts ) );
	verts[0].xyz = center + up * 16.0f;
	verts[1].xyz = center - up * 8.0f + left * 16.0f;
	verts[2].xyz = center - up * 8.0f - left * 16.0f;
	verts[0].color[0] = 255; verts[0].color[3] = 255;
	verts[1].color[1] = 255; verts[1].color[3] = 255;
	verts[2].color[2] = 255; verts[2].color[3] = 255;

	static const unsigned short indexes[ 3 ] = { 0, 1, 2 };

	if ( rb_glesD3TestVbo == 0 ) {
		glGenBuffers( 1, &rb_glesD3TestVbo );
		glGenBuffers( 1, &rb_glesD3TestIbo );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, rb_glesD3TestIbo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( indexes ), indexes, GL_STATIC_DRAW );
	}
	glBindBuffer( GL_ARRAY_BUFFER, rb_glesD3TestVbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_DYNAMIC_DRAW );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, rb_glesD3TestIbo );

	// the vertex-cache shadow cannot describe buffers bound behind its back
	idVertexCache::InvalidateBufferBindings();

	const GLsizei stride = sizeof( idDrawVert );
	glEnableVertexAttribArray( GLESD3_ATTR_POSITION );
	glEnableVertexAttribArray( GLESD3_ATTR_COLOR );
	glVertexAttribPointer( GLESD3_ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( NULL, offsetof( idDrawVert, xyz ) ) );
	glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
			RB_DrawVertAttributePointer( NULL, offsetof( idDrawVert, color ) ) );

	float mvp[ 16 ];
	myGlMultMatrix( backEnd.viewDef->worldSpace.modelViewMatrix,
			backEnd.viewDef->projectionMatrix, mvp );

	GL_State( GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );
	R_GLESD3_SetupProgramForDraw( program, mvp, idVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );

	glDrawElements( GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL );

	glDisableVertexAttribArray( GLESD3_ATTR_POSITION );
	glDisableVertexAttribArray( GLESD3_ATTR_COLOR );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	idVertexCache::InvalidateBufferBindings();

	if ( r_glesD3Report.GetInteger() > 0 && rb_glesD3Reported3DViews <= 1 ) {
		GLubyte px[ 4 ] = { 0, 0, 0, 0 };
		glReadPixels( glConfig.vidWidth / 2, glConfig.vidHeight / 2, 1, 1,
				GL_RGBA, GL_UNSIGNED_BYTE, px );
		common->Printf( "glesd3 testTriangle: prog=%u center=%i,%i,%i "
				"mid=%i,%i,%i,%i err=0x%04x\n",
				program->program, (int)center.x, (int)center.y, (int)center.z,
				px[0], px[1], px[2], px[3], glGetError() );
	}
}

/*
====================
RB_GLESD3_DrawView
====================
*/
void RB_GLESD3_DrawView( void ) {
	if ( !RB_GLESD3_Active() || backEnd.viewDef == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_GLESD3_DrawView ----------\n" );

	// drained at entry so a report can attribute an error to work that ran
	// before this backend was handed the view, rather than to the view setup
	rb_glesD3EntryError = ( r_glesD3Report.GetInteger() > 0 ) ? glGetError() : 0;

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	RB_GLESD3_BeginDrawingView();

	// 3D views are what this backend is being built for, and a run opens with
	// dozens of 2D loading-screen views; reporting both would push the first
	// scene view out of any sane cap.
	const bool is3D = backEnd.viewDef->viewEntitys != NULL;

	// Re-arm whenever the cvar is touched. The cap keeps a run from flooding,
	// but the view worth seeing is usually minutes in -- setting r_glesD3Report
	// again from the console resets the counters so the NEXT frame reports.
	// Without this the reporting is only ever armed during startup, which is
	// exactly when nothing interesting is on screen.
	if ( r_glesD3Report.IsModified() ) {
		r_glesD3Report.ClearModified();
		rb_glesD3Reported3DViews = 0;
		rb_glesD3Reported2DViews = 0;
	}

	const bool reportThisView = r_glesD3Report.GetInteger() > 0
			&& ( is3D ? rb_glesD3Reported3DViews : rb_glesD3Reported2DViews ) < 4;
	if ( reportThisView ) {
		if ( is3D ) {
			rb_glesD3Reported3DViews++;
		} else {
			rb_glesD3Reported2DViews++;
		}
		const GLenum entryErr = rb_glesD3EntryError;
		idStr samples;
		RB_GLESD3_SamplePixels( samples );
		// The scissor is what separates a subview from the main view: a mirror,
		// remote camera or GUI surface renders with viewDef->scissor set to
		// that surface's screen rect, while the main view gets the whole
		// viewport. A box anchored to world geometry is that rect.
		common->Printf( "glesd3 %s view %i: viewport=%i,%i %ix%i scissor=%i,%i..%i,%i "
				"isSubview=%i portalSky=%i target=%i surfs=%i lights=%s "
				"afterClear[ %s] entryErr=0x%04x err=0x%04x\n",
				is3D ? "3d" : "2d",
				is3D ? rb_glesD3Reported3DViews : rb_glesD3Reported2DViews,
				backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
				backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1,
				backEnd.viewDef->scissor.x1, backEnd.viewDef->scissor.y1,
				backEnd.viewDef->scissor.x2, backEnd.viewDef->scissor.y2,
				backEnd.viewDef->isSubview ? 1 : 0,
				// A portal sky is a top-level view, NOT a subview: the game emits
				// it as its own RenderScene immediately before the view it backs.
				// Reading isSubview=0 and concluding "this is the main view" is
				// wrong on any map with a sky, and cost a wrong resolution-scale
				// resolve that upscaled the sky twice.
				( backEnd.viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ? 1 : 0,
				backEnd.renderTexture != NULL ? (int)backEnd.renderTexture->GetDeviceHandle() : 0,
				backEnd.viewDef->numDrawSurfs,
				backEnd.viewDef->viewLights ? "yes" : "no",
				samples.c_str(), entryErr, glGetError() );
		R_GLESD3_ResetSkipCounts();
	}

	if ( RB_GLESD3_EnsureResources() ) {
		R_GLESD3_Draw_BeginView();

		drawSurf_t **drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
		const int numDrawSurfs = backEnd.viewDef->numDrawSurfs;

		// fill the depth buffer and clear colour to black, except on subviews
		RB_GLESD3_FillDepthBuffer( drawSurfs, numDrawSurfs );

		// the main light renderer; D5 adds stencil shadows inside it
		RB_GLESD3_DrawInteractions();

		// false: stopping at the first post-process surface is now a DEFERRAL,
		// not a drop -- the capture below lets the re-entry draw them. The
		// re-entry does the counting, so a surface only reports as skipped if
		// it is still refused after the screen copy exists.
		const int processed = RB_GLESD3_DrawShaderPasses( drawSurfs, numDrawSurfs, false );

		// fog volumes and blend lights, in the slot RB_STD_DrawView gives them
		// (draw_common.cpp:10803) -- after the ambient/material walk so they
		// tint what it drew, before the post-process walk so they are part of
		// the frame those materials sample
		RB_GLESD3_FogAllLights();

		// post-process materials sample the finished frame, so they draw after
		// everything that writes it -- and they can only draw once that frame
		// has been copied into _currentRender.
		//
		// This used to CLEAR currentRenderCopied here, which guaranteed the
		// second walk stopped on the same surface as the first: nothing else in
		// the backend ever set it, so every SS_POST_PROCESS surface in every
		// view was dropped. The capture is D7b's prerequisite and it belongs
		// exactly here, between the fog pass and the re-entry.
		if ( processed < numDrawSurfs ) {
			R_GLESD3_CaptureCurrentRender();
			RB_GLESD3_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed, true );
		}

		if ( is3D && r_glesD3TestTriangle.GetBool() ) {
			RB_GLESD3_DrawTestTriangle();
		}

		R_GLESD3_Draw_EndView();
	}

	// Reported for the same views the entry probe resets, NOT just the first.
	// A map with a portal sky opens with a sky subview, so view 1 is the
	// subview and view 2 is the real scene; gating the report at "<= 1"
	// printed the subview's skips and silently dropped the main view's --
	// which is how seven wrongly-drawn heatHaze stages on game/airdefense1
	// stayed invisible to the counters that exist to catch exactly that.
	if ( r_glesD3Report.GetInteger() > 0 && reportThisView && is3D ) {
		idStr samples;
		RB_GLESD3_SamplePixels( samples );
		int fogLights = 0, blendLights = 0, fogDraws = 0, fogSkips = 0;
		RB_GLESD3_FogBlendCounts( fogLights, blendLights, fogDraws, fogSkips );
		common->Printf( "glesd3 afterMaterial(view %i): drawElements=%i px[ %s] "
				"fog=%i/%i draws=%i skipped=%i err=0x%04x\n",
				rb_glesD3Reported3DViews, backEnd.pc.c_drawElements,
				samples.c_str(),
				fogLights, blendLights, fogDraws, fogSkips, glGetError() );
		R_GLESD3_ReportSkipCounts();
	}

	if ( is3D ) {
		// Before the present, not after: the crop is resolved INSIDE the bound
		// target, and RB_GLESD3_PresentSceneTarget copies that target to the back
		// buffer. Resolving afterwards would present the un-upscaled corner and
		// leave the fix visible only to whatever sampled the target later.
		RB_GLES_ResolveSceneResolutionScale();
		RB_GLESD3_PresentSceneTarget();
	}

	// Hand the context back with a full scissor. The engine runs
	// scissor-sensitive commands BETWEEN views with no scissor handling of
	// their own -- RB_ResolveMSAA blits the scene into
	// _forwardRenderResolvedAlbedo and RB_ClearRenderTarget clears the
	// postprocess target, and both inherit whatever box the last drawn surface
	// left.
	//
	// Measured on Android (game/airdefense1, storm active): the 3D view ended
	// with the door-frame surface's box 819,334 534x570, the resolve and the
	// postprocess clear were clipped to it, and the presented frame was last
	// frame's resolve everywhere outside that box -- a frozen screen with one
	// live square. Which surface draws last depends on the scene; the
	// lightning flicker and the door state change it, which is why the
	// artifact tracked the storm.
	//
	// ARB2 does this same restore at the end of its present
	// (draw_arb2.cpp:10831); this backend dropped it.
	glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	backEnd.currentScissor.x1 = 0;
	backEnd.currentScissor.y1 = 0;
	backEnd.currentScissor.x2 = glConfig.vidWidth - 1;
	backEnd.currentScissor.y2 = glConfig.vidHeight - 1;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
