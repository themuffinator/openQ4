// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- a Doom 3-shaped GLES 3.0 backend.

	Shared declarations for the GLES_D3 translation units.

	This backend is a peer of BE_MODERN inside the renderer-gles module, not a
	module of its own: GLES is still GL, so tr_backend.cpp's frame loop,
	OpenGL/gl_Image.cpp's uploads and the module's ANGLE resolution are all
	reused. The only seam is RB_DrawView in GLES/gles_Backend.cpp, which
	dispatches here when tr.backEndRenderer == BE_GLES_D3.

	Nothing in this directory may run under BE_MODERN. The modern path is
	under active repair on the same context; a shared-state regression
	introduced here would be attributed to that work.

===============================================================================
*/

#ifndef __GLES_D3_LOCAL_H__
#define __GLES_D3_LOCAL_H__

#ifdef OPENQ4_RENDERER_GLES_MODULE

/*
====================
RB_GLESD3_DrawView

The RB_STD_DrawView equivalent: the whole scene render for one view, 2D and
3D alike. Called from RB_DrawView (GLES/gles_Backend.cpp) with
backEnd.viewDef already set and the zero-drawsurf and r_skipRender cases
already filtered out.
====================
*/
void RB_GLESD3_DrawView( void );

// Defined in GLES/gles_Backend.cpp, next to the scratch target it owns. Resolves
// the r_screenFraction crop inside whatever framebuffer is bound, so it has to
// run before anything presents or samples that framebuffer.
void RB_GLES_ResolveSceneResolutionScale( void );

/*
====================
RB_GLESD3_Active

True when this backend owns the frame. Every entry point in this directory
tests it, so a stray call under BE_MODERN is inert rather than corrupting.
====================
*/
ID_INLINE bool RB_GLESD3_Active( void ) {
	return tr.backEndRenderer == BE_GLES_D3;
}

struct glesProgram_s;

// GLES/gles_Backend.cpp -- the reverse of this module's R_FindARBProgram.
// newShaderStage_t stores only the handle, so this is the only way back to the
// program name a material author wrote, which is what D7c dispatches on.
const char *	R_GLES_MaterialProgramName( int ident );

// gles_d3_backend.cpp -- bring-up telemetry, shared so each TU can gate its
// own probes on the same switch
extern idCVar r_glesD3Report;

// gles_program.cpp
void	R_GLESD3_Programs_RegisterCommands( void );

// gles_draw.cpp -- vertex attribute binding and indexed draws
void	R_GLESD3_Draw_Init( void );
void	R_GLESD3_Draw_Shutdown( void );
void	R_GLESD3_Draw_BeginView( void );
void	R_GLESD3_Draw_EndView( void );
bool	R_GLESD3_BindDrawVertAttributes( const srfTriangles_t *tri );
bool	R_GLESD3_BindCubeTexDirAttribute( const drawSurf_t *surf, bool fromNormal );
void	R_GLESD3_DisableCubeTexDirAttribute( void );
void	R_GLESD3_EnableAttributesForShadow( void );
void	R_GLESD3_InvalidateAttributeState( void );
vertCache_t *	R_GLESD3_EnsureIndexCache( const srfTriangles_t *tri );
bool	R_GLESD3_DrawElements( const srfTriangles_t *tri );
float	R_GLESD3_AlphaTestReference( int stateBits );
void	R_GLESD3_SetAlphaTest( const struct glesProgram_s *program, int stateBits );
void	R_GLESD3_SetupProgramForDraw( const struct glesProgram_s *program,
				const float mvp[16], const idVec4 &color );

/*
====================
RB_GLESD3_EnsureResources

Programs and the vertex array are created on first use inside the frame,
where a current GL context is guaranteed. Returns false if the backend has
nothing usable, in which case the view is cleared and left empty rather than
drawn with program 0.
====================
*/
bool	RB_GLESD3_EnsureResources( void );

// gles_shaderpasses.cpp -- authored material stages, 2D and GUI.
// Returns the number of surfaces consumed; stops at the first SS_POST_PROCESS
// surface that still needs a _currentRender copy.
//
// recordPostProcessSkips is false on the post-fog re-entry: that walk stops on
// the same surfaces as the first one until the _currentRender capture exists
// (D7), and counting them twice would report double the post-process surfaces
// a view actually holds.
int		RB_GLESD3_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs,
				bool recordPostProcessSkips = true );

// Copies the finished view into _currentRender, which is what every
// SS_POST_PROCESS material samples. Idempotent within a view: it does nothing
// once backEnd.currentRenderCopied is set, so the on-demand calls inside the
// stage loop and the one the view driver makes before the post-process walk
// cannot double-copy. _currentDepth is not captured -- see gles_shaderpasses.cpp.
void	R_GLESD3_CaptureCurrentRender( void );

/*
====================
RB_GLESD3_SetScissor

The one place this backend changes the scissor. Every pass used to carry its
own copy of the same delta-code, and each copy repeated the same two defects:

  - An EMPTY rect (idScreenRect::Clear() leaves 32000,32000..-32000,-32000, and
    R_AddModelSurfaces really does produce them -- measured on airdefense1,
    five of nineteen view entities in one frame) computes a width of
    x2 + 1 - x1 = -63999. glScissor raises GL_INVALID_VALUE and DOES NOTHING,
    so GL silently keeps the previous box while backEnd.currentScissor records
    the new one. Every later surface that matches the tracked rect then skips
    the re-issue and draws through whatever box happened to be left.
  - backEnd.currentScissor was assigned before the r_useScissor check in one
    caller and after it in the others.

Returns false when the rect is empty, meaning "this surface can cover no
pixels" -- the caller skips the draw rather than issuing a rejected call.
====================
*/
bool	RB_GLESD3_SetScissor( const idScreenRect &rect );

// The depth prepass. Not optional before the material pass: idMaterial gives
// every opaque and perforated stage GLS_DEPTHFUNC_EQUAL, so an unfilled depth
// buffer rejects the entire opaque world.
void	RB_GLESD3_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs );

// gles_interaction.cpp -- per-light bump/diffuse/specular. Runs between the
// depth prepass and the material pass; without it a Quake 4 scene is black,
// because its surfaces carry no ambient stage.
void	RB_GLESD3_DrawInteractions( void );
void	RB_GLESD3_DetermineLightScale( void );

// The two texgen helpers from the excluded draw_common.cpp. Defined in
// gles_interaction.cpp and shared with the blend light pass, which projects
// through the same planes with the same stage texture matrices.
void	R_GLESD3_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float textureMatrix[16] );
void	R_GLESD3_GetShaderTextureMatrix( const float *shaderRegisters,
				const textureStage_t *texture, float matrix[16] );

// gles_fog.cpp -- fog volumes and blend lights, the last two Doom 3 core
// passes. Runs between the two material walks, where RB_STD_DrawView puts it.
void	RB_GLESD3_FogAllLights( void );
void	RB_GLESD3_FogBlendCounts( int &fogLights, int &blendLights, int &draws, int &skipped );

// gles_stencilshadow.cpp -- shadow volumes into the stencil buffer. Leaves the
// stencil function at GL_GEQUAL 128, which is what the interaction pass draws
// through.
void	RB_GLESD3_StencilShadowPass( const drawSurf_t *drawSurfs );
int		RB_GLESD3_ShadowDrawCount( void );
void	RB_GLESD3_ResetShadowDrawCount( void );
void	R_GLESD3_ResetSkipCounts( void );
void	R_GLESD3_ReportSkipCounts( void );

#endif /* OPENQ4_RENDERER_GLES_MODULE */

#endif /* !__GLES_D3_LOCAL_H__ */
