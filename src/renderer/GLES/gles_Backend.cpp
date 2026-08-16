// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	Backend entry points the GLES module needs from the excluded legacy TUs.

	The module keeps tr_backend.cpp -- the real frame driver, with
	RB_ExecuteBackEndCommands, RB_SetBuffer, RB_SwapBuffers and the ModernGL
	executor hooks -- rather than replacing it. Measured with nm against the
	desktop build, tr_backend.cpp references exactly five symbols defined in
	the translation units this module drops (draw_common.cpp, tr_render.cpp,
	tr_rendertools.cpp, draw_arb2.cpp). Supplying those five here reuses the
	entire frame loop, where the Vulkan module had to write a 1344-line
	backend of its own.

	All five are legacy work by definition:

	  RB_DrawView                        the ARB2/fixed-function scene draw
	  RB_DrawSpecialEffects              legacy BSE effects
	  RB_ApplyResolutionScaleToBackBuffer
	  RB_ApplyCRTToBackBuffer            legacy back-buffer post
	  RB_ApplyColorMappingsToBackBuffer

	Under BE_MODERN the modern executor owns the passes it can and composites
	in R_ModernGLExecutor_ComposeVisibleFrame; nothing here needs to draw for
	the frame to reach the screen.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../ShadowMapArb2Parity.h"
#include "../GLStateCache.h"
#include "../GLES_D3/gles_d3_local.h"
#include "../GLES_D3/gles_program.h"

/*
====================
RB_DrawView

Reproduces the bookkeeping of the desktop implementation (tr_render.cpp)
exactly, and stops where that one calls RB_STD_DrawView() -- the ARB2 and
fixed-function scene render, which has no meaning on an ES context.

The bookkeeping is not optional: backEnd.viewDef is read all over the shared
front-end, and currentRenderCopied / currentDepthCopied drive whether a
SS_POST_PROCESS material re-copies the screen.
====================
*/
void RB_DrawView( const void *data ) {
	const drawSurfsCommand_t *cmd = ( const drawSurfsCommand_t * )data;

	backEnd.viewDef = cmd->viewDef;

	// a new copyTexSubImage of the screen is needed when a SS_POST_PROCESS
	// material is used
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;

	if ( !backEnd.viewDef->numDrawSurfs ) {
		return;
	}

	if ( r_skipRender.GetBool() && backEnd.viewDef->viewEntitys ) {
		return;
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	if ( RB_GLESD3_Active() ) {
		// gles_d3 renders the view itself, the way RB_STD_DrawView does on
		// desktop. This is the backend's only seam into the shared frame loop.
		// It resolves the r_screenFraction crop itself, because it has to do so
		// ahead of its own scene-target present.
		RB_GLESD3_DrawView();
		return;
	}

	// The legacy scene render would run here. On ES the modern executor owns
	// the passes; anything it does not own is simply not drawn, which is the
	// standalone-backend contract.
	RB_GLES_ResolveSceneResolutionScale();
}

/*
====================
RB_DrawSpecialEffects

Legacy BSE effect draw. tr_backend already skips this whenever the modern
executor owns RENDER_PASS_SPECIAL_EFFECTS.
====================
*/
void RB_DrawSpecialEffects( const void *data ) {
	( void )data;
}

/*
====================
RB_GLES_ResolveSceneResolutionScale

The second half of r_screenFraction on ES, and the only half that was not
already written.

r_screenFraction had no implementation here at all. Modes 1 and 2 live in
draw_common.cpp, one of the TUs this module drops, and the frame this module
actually draws never read the cvar -- `+set r_screenFraction 25` reached the
engine, passed its range check, and changed nothing on screen. Mode 0 was the
only path in shared code, and it crops the frame without ever upscaling it,
which is a fill-rate measurement rather than a player setting.

idRenderSystemLocal::PushSceneResolutionScale crops the world render, so the
scene arrives here in the bottom-left corner of whatever target is bound, at
resolutionScaleWidth x resolutionScaleHeight. This puts it back over the view's
native extents. It runs at the end of the scene view rather than at swap time
because the HUD is drawn after the scene and must not be caught by it.

Two blits through a scratch target, not one. The corner and the full viewport
are the same framebuffer, and glBlitFramebuffer between overlapping regions of
one framebuffer is undefined in ES 3.0 -- the driver may read texels the call
has already written. The scratch renderbuffer breaks that aliasing; it is
allocated at the cropped size, so at 50% it is a quarter of the display and both
blits together cost far less than the pixels the crop saved.

Filtering is chosen by r_resolutionScaleMode: 3 points the upscale at
GL_NEAREST, anything else uses GL_LINEAR. Bilinear at a quarter resolution reads
as mud on a phone panel, and nearest at a whole-number fraction is just pixel
doubling, which stays legible. The 1:1 copy in is always GL_NEAREST -- it has
nothing to interpolate.
====================
*/
// r_resolutionScaleMode value that asks for a point-filtered upscale
static const int RB_RESOLUTION_SCALE_MODE_NEAREST = 3;

static GLuint rb_glesResolutionScaleFbo = 0;
static GLuint rb_glesResolutionScaleColor = 0;
static int rb_glesResolutionScaleWidth = 0;
static int rb_glesResolutionScaleHeight = 0;
static bool rb_glesResolutionScaleUnavailable = false;

/*
====================
RB_GLES_AbandonSceneResolutionScale

The crop is pushed by the front end and this pass is the only thing that
resolves it. Refusing to run without also stopping the crop leaves the scene in
the bottom-left corner of the display -- precisely the legacy mode 0 artifact
this pass exists to remove, arrived at from the other direction. Measured on an
SM-S928B: the multisample guard below fired, the crop kept being pushed, and the
frame sat at 50% in the corner for the rest of the session.

So the refusal has to travel back to PushSceneResolutionScale. The frame that
discovers it is already committed; every frame after it renders unscaled.
====================
*/
static void RB_GLES_AbandonSceneResolutionScale( void ) {
	rb_glesResolutionScaleUnavailable = true;
	tr.resolutionScaleSuppressed = true;
}

static bool RB_GLES_EnsureResolutionScaleTarget( int width, int height ) {
	if ( rb_glesResolutionScaleFbo != 0
			&& rb_glesResolutionScaleWidth == width
			&& rb_glesResolutionScaleHeight == height ) {
		return true;
	}

	if ( rb_glesResolutionScaleFbo != 0 ) {
		glDeleteFramebuffers( 1, &rb_glesResolutionScaleFbo );
		rb_glesResolutionScaleFbo = 0;
	}
	if ( rb_glesResolutionScaleColor != 0 ) {
		glDeleteRenderbuffers( 1, &rb_glesResolutionScaleColor );
		rb_glesResolutionScaleColor = 0;
	}
	rb_glesResolutionScaleWidth = 0;
	rb_glesResolutionScaleHeight = 0;

	glGenRenderbuffers( 1, &rb_glesResolutionScaleColor );
	glBindRenderbuffer( GL_RENDERBUFFER, rb_glesResolutionScaleColor );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, width, height );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );

	glGenFramebuffers( 1, &rb_glesResolutionScaleFbo );
	glBindFramebuffer( GL_FRAMEBUFFER, rb_glesResolutionScaleFbo );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_glesResolutionScaleColor );
	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "r_screenFraction: %ix%i upscale target incomplete (0x%04x); scaling disabled.",
				width, height, status );
		glDeleteFramebuffers( 1, &rb_glesResolutionScaleFbo );
		glDeleteRenderbuffers( 1, &rb_glesResolutionScaleColor );
		rb_glesResolutionScaleFbo = 0;
		rb_glesResolutionScaleColor = 0;
		return false;
	}

	rb_glesResolutionScaleWidth = width;
	rb_glesResolutionScaleHeight = height;
	return true;
}

void RB_GLES_ResolveSceneResolutionScale( void ) {
	if ( rb_glesResolutionScaleUnavailable || backEnd.viewDef == NULL ) {
		return;
	}

	// The scene view only. Mirror and portal subviews render into the same crop
	// and are resolved along with the view they belong to; 2D views are outside
	// the crop entirely, which is what keeps the HUD and the menus sharp.
	if ( backEnd.viewDef->viewEntitys == NULL || backEnd.viewDef->isSubview ) {
		return;
	}

	// A portal sky is NOT a subview. The game emits it as its own top-level
	// RenderScene, with its own crop, into the same target immediately before the
	// view it backs -- so it arrives here looking exactly like a main view.
	//
	// Resolving after it upscales the sky on its own; the main view then draws
	// into the crop over an already-upscaled backdrop, and the second resolve
	// magnifies that corner again. The sky comes out enlarged by the scale factor
	// a second time and pans at that multiple when the camera turns: at 25% it
	// reads as a sky four times too big moving four times too fast. Measured on
	// game/airdefense1, and it is the reason the report above prints portalSky.
	//
	// Skipping it is the whole fix: the sky stays in the crop, the main view
	// draws over it there, and one resolve at the end lifts both out together.
	if ( ( backEnd.viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		return;
	}

	const int displayWidth = glConfig.vidWidth;
	const int displayHeight = glConfig.vidHeight;
	const int sourceWidth = Min( backEnd.resolutionScaleWidth, displayWidth );
	const int sourceHeight = Min( backEnd.resolutionScaleHeight, displayHeight );
	if ( sourceWidth <= 0 || sourceHeight <= 0 ) {
		return;
	}
	if ( sourceWidth == displayWidth && sourceHeight == displayHeight ) {
		return;
	}

	GLint previousFbo = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFbo );

	// A multisampled draw framebuffer cannot receive a blit on ES 3.0, and there
	// is no cheap way around it here. Report once and stay out of the way rather
	// than raising GL_INVALID_OPERATION every frame.
	//
	// SAMPLE_BUFFERS, not SAMPLES. SAMPLES is what the driver would use if there
	// were a sample buffer, and it is free to report a count when there is none:
	// an SM-S928B answers SAMPLE_BUFFERS=0 SAMPLES=4 for a window created with
	// r_multiSamples 4 and no multisample config available. Reading SAMPLES there
	// disabled scaling on a framebuffer that was single-sampled all along, which
	// is how this pass first shipped broken on device. The blit restriction is
	// written against SAMPLE_BUFFERS and so is this test.
	GLint sampleBuffers = 0;
	glGetIntegerv( GL_SAMPLE_BUFFERS, &sampleBuffers );
	if ( sampleBuffers > 0 ) {
		common->Warning( "r_screenFraction: the scene target is multisampled, which cannot receive a blit on ES; scaling disabled." );
		RB_GLES_AbandonSceneResolutionScale();
		return;
	}

	if ( !RB_GLES_EnsureResolutionScaleTarget( sourceWidth, sourceHeight ) ) {
		RB_GLES_AbandonSceneResolutionScale();
		glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)previousFbo );
		return;
	}

	// Both blits are clipped by the draw framebuffer's scissor, and the view ends
	// with whatever box its last surface set.
	const bool scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST ) == GL_TRUE;
	if ( scissorWasEnabled ) {
		glDisable( GL_SCISSOR_TEST );
	}
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	const GLenum sourceReadBuffer = ( previousFbo == 0 ) ? GL_BACK : GL_COLOR_ATTACHMENT0;

	glBindFramebuffer( GL_READ_FRAMEBUFFER, (GLuint)previousFbo );
	glReadBuffer( sourceReadBuffer );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, rb_glesResolutionScaleFbo );
	glBlitFramebuffer( 0, 0, sourceWidth, sourceHeight,
			0, 0, sourceWidth, sourceHeight,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );

	const GLenum upscaleFilter =
		( r_resolutionScaleMode.GetInteger() == RB_RESOLUTION_SCALE_MODE_NEAREST ) ? GL_NEAREST : GL_LINEAR;

	glBindFramebuffer( GL_READ_FRAMEBUFFER, rb_glesResolutionScaleFbo );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, (GLuint)previousFbo );
	glBlitFramebuffer( 0, 0, sourceWidth, sourceHeight,
			0, 0, displayWidth, displayHeight,
			GL_COLOR_BUFFER_BIT, upscaleFilter );

	glBindFramebuffer( GL_READ_FRAMEBUFFER, (GLuint)previousFbo );
	glReadBuffer( sourceReadBuffer );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)previousFbo );

	// glColorMask was issued behind GL_State's back, the same way
	// RB_ForceOpaquePresentAlpha does it, so the cached mask bits no longer
	// describe the driver.
	backEnd.glState.forceGlState = true;
}

/*
====================
Legacy back-buffer post

Resolution scaling, CRT emulation and colour mapping are all implemented in
draw_common.cpp against fixed-function state. The CRT and colour-mapping ones
are cosmetic passes over the finished frame; leaving them out costs those
effects and nothing else.

Resolution scaling is not cosmetic, and this module implements it for real in
RB_GLES_ResolveSceneResolutionScale above -- scoped to the scene view, where it
can leave the HUD alone. Nothing is left to do at swap time.
====================
*/

void RB_ApplyResolutionScaleToBackBuffer( void ) {
}

void RB_ApplyCRTToBackBuffer( void ) {
}

void RB_ApplyColorMappingsToBackBuffer( void ) {
}

/*
===============================================================================
	Remaining legacy entry points referenced by the shared front-end.

	Found the same way as the five above: build the module and read the
	undefined-symbol closure. shared_library (rather than shared_module) is
	what makes that closure exist on darwin at all -- an MH_BUNDLE with
	-undefined dynamic_lookup would have linked silently and failed at load.

	Three groups, none of which can execute on an ES context:

	  R_ARB2_*, R_FindARBProgram, R_ValidateGLSLProgram
	      ARB assembly programs. glProgramStringARB and friends are NULL
	      pointers here, so nothing reaches these.

	  RB_*DebugLine / Polygon / Text
	      immediate-mode debug drawing from tr_rendertools.cpp.

	  RB_ShadowMap*Arb2*, RB_ResetAppleGL21RouteCounters, RB_Shutdown*
	      ARB2 shadow parity bookkeeping and teardown for resources this
	      module never creates.
===============================================================================
*/

void R_ARB2_Init( void ) {
	// The ARB2 bridge cannot exist on ES. Leaving allowARB2Path false is what
	// selects BE_MODERN in R_PickBestBackEndRenderer.
	common->Printf( "Not available: OpenGL ES has no ARB assembly programs; the modern executor renders standalone.\n" );
	glConfig.allowARB2Path = false;
}

/*
====================
R_FindARBProgram

Registers the program name and returns a non-zero handle. Returning 0 -- which
this did until 2026-08-10 -- is not a harmless stub: idMaterial only allocates
a stage's newShaderStage_t when a program handle is non-zero
(Material.cpp:2436), so a zero handle makes the parser DISCARD the fact that
the stage is a custom-program stage at all.

The stage then looks like an ordinary textured stage to every back end in this
module, and gets drawn as one. Measured on game/airdefense1: seven
`gfx/effects/energy_sparks/warp_mask` stages -- authored as heatHazeWithMask
programs sampling _currentRender -- drew as opaque black quads over the scene,
and were invisible to the unsupported-feature counters precisely because the
information had already been thrown away.

So the handle is real and the *capability* is reported false, which is the
honest split: the program exists as an authored asset, and this module has no
implementation for it yet (D7). R_IsARBProgramValid answers that question, and
the renderer skips and counts the stage instead of drawing it wrong.

Mirrors what the Vulkan module does (vk_Backend.cpp:881).
====================
*/
static const int GLES_MAX_MATERIAL_PROGRAMS = 256;
static char gles_materialProgramNames[ GLES_MAX_MATERIAL_PROGRAMS ][ MAX_OSPATH ];
static int gles_numMaterialPrograms = 0;

int R_FindARBProgram( unsigned int target, const char *program ) {
	( void )target;
	if ( program == NULL || program[0] == '\0' ) {
		return 0;
	}

	for ( int i = 0; i < gles_numMaterialPrograms; i++ ) {
		if ( idStr::Icmp( gles_materialProgramNames[ i ], program ) == 0 ) {
			return i + 1;	// 1-based: 0 means "no program" to the parser
		}
	}

	if ( gles_numMaterialPrograms >= GLES_MAX_MATERIAL_PROGRAMS ) {
		// out of slots: fall back to the old behaviour for this one rather
		// than hand back a handle that names the wrong program
		return 0;
	}

	idStr::Copynz( gles_materialProgramNames[ gles_numMaterialPrograms ], program,
			sizeof( gles_materialProgramNames[ 0 ] ) );
	gles_numMaterialPrograms++;
	return gles_numMaterialPrograms;
}

/*
====================
R_GLES_MaterialProgramName

The reverse of R_FindARBProgram, for backends in this module that implement
some of the material programs natively (GLES_D3/gles_shaderpasses.cpp, D7c).
newShaderStage_t stores only the handle, and the handle is a slot index in the
table above -- so without this the name the material author wrote is
unrecoverable at draw time, and there is nothing to dispatch on.

NULL for a handle this table never issued, which callers must treat as "not a
program we implement" rather than as an error: the parser hands out handles for
programs that exist as assets whether or not any backend can run them.
====================
*/
const char *R_GLES_MaterialProgramName( int ident ) {
	if ( ident <= 0 || ident > gles_numMaterialPrograms ) {
		return NULL;
	}
	return gles_materialProgramNames[ ident - 1 ];
}

bool R_IsARBProgramValid( unsigned int target, unsigned int ident ) {
	( void )target;
	( void )ident;
	return false;
}

void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	( void )args;
}

void R_ReportShaderPrograms_f( const idCmdArgs &args ) {
	( void )args;
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	( void )stage;
	return false;
}

void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
	( void )color; ( void )start; ( void )end; ( void )lifeTime; ( void )depthTest;
}

void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	( void )color; ( void )winding; ( void )lifeTime; ( void )depthTest;
}

void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest ) {
	( void )text; ( void )origin; ( void )scale; ( void )color; ( void )viewAxis; ( void )align; ( void )lifetime; ( void )depthTest;
}

void RB_ClearDebugLines( int time ) { ( void )time; }
void RB_ClearDebugPolygons( int time ) { ( void )time; }
void RB_ClearDebugText( int time ) { ( void )time; }
void RB_ShutdownDebugTools( void ) { }

bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf ) {
	( void )surf;
	return false;
}

void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) {
	( void )shaderRegisters;
	( void )texture;
	// identity: callers expect a usable matrix even when the stage has none
	for ( int i = 0; i < 16; i++ ) {
		matrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}
}

void RB_ResetAppleGL21RouteCounters( void ) { }

bool RB_ShadowMapBuildArb2ParityState( const viewLight_t *vLight, const viewDef_t *viewDef, int shadowMapSize, shadowMapArb2ParityState_t &state ) {
	( void )vLight; ( void )viewDef; ( void )shadowMapSize; ( void )state;
	return false;
}

bool RB_ShadowMapResourcesKnownGood( bool pointLight ) {
	( void )pointLight;
	return false;
}

bool RB_ShadowMapTextureBindings( rendererShadowTextureBindings_t &bindings ) {
	( void )bindings;
	return false;
}

void RB_ShutdownScenePostProcess( void ) { }
void RB_ShutdownShadowMapResources( void ) { }

void RB_ResetARB2InteractionHandoffBreadcrumb( void ) { }

bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {
	// The self-test validates ARB2 receiver parity. Reporting success would be
	// a lie on a backend with no ARB2; reporting failure is accurate and the
	// caller treats it as "parity path unavailable".
	return false;
}

bool RB_ShadowMapEstimateArb2CacheOwnership( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate ) {
	( void )vLight; ( void )viewDef; ( void )estimate;
	return false;
}

bool RB_ShadowMapProjectedAtlasSlotForLight( int lightDefIndex, shadowMapArb2AtlasSlot_t &slot ) {
	( void )lightDefIndex; ( void )slot;
	return false;
}

void RB_ShadowMapProjectedAtlasSlotMarkUsed( int lightDefIndex ) { ( void )lightDefIndex; }

/*
===============================================================================

	Four more references out of the kept front end into the dropped
	fixed-function translation units (draw_arb2.cpp, tr_render.cpp,
	tr_rendertools.cpp). macOS never needed them because -dead_strip removes
	the callers before the linker resolves them; a platform that links without
	dead-stripping does. Same treatment as renderer/Vulkan/vk_Backend.cpp.

===============================================================================
*/

void GL_SelectTextureNoClient( int unit ) {
	( void )unit;
}

void RB_DrawBounds( const idBounds &bounds ) {
	( void )bounds;
}

float RB_DrawTextLength( const char *text, float scale, int len ) {
	( void )text; ( void )scale; ( void )len;
	return 0.0f;
}

void RB_DrawElementsImmediate( const srfTriangles_t *tri ) {
	( void )tri;
}

#ifdef __ANDROID__
/*
===============================================================================

	RB_GLES_RestoreStateAfterOverlay

	The host app's touch controls are drawn from inside SDL_GL_SwapWindow --
	between engine frames, behind this renderer's back. They bind their own
	program, VAO, framebuffer, sampler and texture, drop the array/element/
	uniform buffer bindings, switch to texture unit 0, disable depth test and
	cull, force their own blend func, and reset the viewport.

	Nothing puts any of that back, and worse, the renderer's several "I know
	what is already bound" caches still describe the pre-overlay driver, so
	the next frame skips re-issuing exactly the state that changed. The
	symptom is a black screen the moment the overlay is switched on.

	Called from GLimp_SwapBuffers once the swap returns. Cheap: a couple of
	dozen state calls once per frame.

===============================================================================
*/
void RB_GLES_RestoreStateAfterOverlay( void ) {
	// Put the driver back where R_SetDefaultGLState leaves it. The renderer
	// treats these enables as permanent and only ever toggles depth/stencil
	// per view, so they have to be true again before the next frame starts.
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_UNIFORM_BUFFER, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindSampler( 0, 0 );
	glUseProgram( 0 );

	glEnable( GL_DEPTH_TEST );
	glEnable( GL_BLEND );
	glEnable( GL_SCISSOR_TEST );
	glDisable( GL_STENCIL_TEST );
	glDepthMask( GL_TRUE );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	// backEnd.currentScissor is the tracker every GLES_D3 draw path delta-codes
	// against, and it is a sibling of backEnd.glState rather than a member of
	// it, so neither the memset below nor forceGlState reaches it. Without this
	// the box set above and the tracked rect disagree until the next
	// RB_GLESD3_BeginDrawingView re-issues both.
	//
	// Nothing draws in that window today, so this is a latent inconsistency
	// rather than a live defect -- but it is exactly the class of cache this
	// function exists to clear, and a draw that ever landed there would
	// silently under-clip.
	backEnd.currentScissor.x1 = 0;
	backEnd.currentScissor.y1 = 0;
	backEnd.currentScissor.x2 = glConfig.vidWidth - 1;
	backEnd.currentScissor.y2 = glConfig.vidHeight - 1;

	// Culling is left off to match the overlay, and the cache is told so:
	// GL_Cull only re-issues glEnable( GL_CULL_FACE ) when it believes the
	// previous state was CT_TWO_SIDED, so claiming anything else here would
	// leave culling disabled for the rest of the frame.
	glDisable( GL_CULL_FACE );
	backEnd.glState.faceCulling = CT_TWO_SIDED;

	// Now drop every cached record of what is bound, so the next draw re-issues
	// its real state instead of delta-ing against the overlay's.
	memset( backEnd.glState.tmu, 0, sizeof( backEnd.glState.tmu ) );
	backEnd.glState.currenttmu = 0;
	backEnd.glState.forceGlState = true;

	R_GLESD3_InvalidateProgramState();
	R_GLESD3_InvalidateAttributeState();
	R_GLStateCache_InvalidateAll( "android touch overlay" );
	idVertexCache::InvalidateBufferBindings();
}
#endif /* __ANDROID__ */

#endif /* OPENQ4_RENDERER_GLES_MODULE */
