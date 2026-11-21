// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_DRAW_PBR_H
#define OPENQ4_DRAW_PBR_H

// Native per-surface PBR in the classic OpenGL light loop (draw_pbr.cpp):
// Stage B of docs/dev/archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md. An
// admitted surface keeps the classic depth fill, stencil shadows and shadow
// maps; only its interaction draws, environment term and emission change
// owner, exactly as on native Vulkan. Include after tr_local.h.

struct pbrNativeMaterial_s;
class idScenePacketFrame;

// Uniform locations a PBR receiver program variant adds to its classic ABI.
typedef struct glPBRReceiverUniforms_s {
	GLint	material;		// x metallic, y roughness, z normal scale, w AO scalar
	GLint	layout;			// x packed ORM, y metallic map, z roughness map, w AO map
	GLint	mode;			// x normal encoding, y display-referred, z specular AA, w ambient light
	GLint	metallicMap;
	GLint	aoMap;
	GLint	transparent;	// x authored stage alpha scale (0: additive), y coverage only
	GLint	screenAO;		// x SSAO field bound, yz 1 / its size
	GLint	screenAOMap;
	GLint	parallax;		// REN-11: scale, bias, most samples, self-shadow; zero when flat
	GLint	parallaxFade;	// x fade start, y fade end
} glPBRReceiverUniforms_t;

// True when this surface's lighting is owned by the native PBR draws in the
// current view: the shared material contract plus the OpenGL resources.
bool		RB_GLPBR_SurfaceOwned( const drawSurf_t *surf );

// The classic decomposition (RB_CreateSingleDrawInteractionsFiltered) calls
// this first: a classic pass skips owned surfaces and a native pass skips the
// rest, so every surface is lit by exactly one owner for each light.
bool		RB_GLPBR_DecompositionSkips( const drawSurf_t *surf );
bool		RB_GLPBR_NativePassActive( void );
void		RB_GLPBR_BeginNativePass( void );
void		RB_GLPBR_EndNativePass( void );
bool		RB_GLPBR_ChainHasOwned( const drawSurf_t *chain );

// Unshadowed and stencil-shadowed lights: draw the owned surfaces of a chain
// under the current stencil state.
void		RB_GLPBR_DrawInteractionChain( const drawSurf_t *chain );

// Shadow-mapped receivers compile their own sources as PBR variants: these
// defines follow the version line and the libraries are appended.
const char *RB_GLPBR_ReceiverDefines( bool unshadowed );
// True when the PBR programs are built with parallax occlusion (REN-11):
// r_parallaxOcclusion is set and the context samples with explicit gradients.
// Program caches key on it, so toggling the cvar rebuilds them.
bool		RB_GLPBR_ParallaxCompiled( void );
const char *RB_GLPBR_VertexLibrary( void );
const char *RB_GLPBR_FragmentLibrary( void );
void		RB_GLPBR_LookupReceiverUniforms( GLhandleARB program, glPBRReceiverUniforms_t &uniforms );
// Binds the PBR material (units 0, 3, 4, 9, 10) over a receiver's classic
// bindings and returns the geometry the draw must use.
const srfTriangles_t *RB_GLPBR_BindReceiverInteraction( const glPBRReceiverUniforms_t &uniforms, const drawInteraction_t *din );
void		RB_GLPBR_UnbindReceiverUnits( void );
void		RB_GLPBR_CountMappedInteraction( void );

// Ambient walk: one environment draw per owned surface, the native emission
// in place of its classic glow stage, and the albedo as its depth-fill mask.
void		RB_GLPBR_DrawEnvironment( const drawSurf_t *surf );
bool		RB_GLPBR_DrawEmissionStage( const drawSurf_t *surf, int stageIndex );
// A translucent owner's authored source-alpha stage: its coverage, every
// light that reaches it and its environment, composited through the alpha.
bool		RB_GLPBR_DrawTransparentStage( const drawSurf_t *surf, int stageIndex );
idImage *	RB_GLPBR_DepthCoverageImage( const drawSurf_t *surf, const shaderStage_t *stage );

// Once per frame while the modern executor is idle: begins the environment
// atlas frame and prepares authored probes from the frame's packets.
void		RB_GLPBR_PrepareFrame( const idScenePacketFrame &frame );
// When the modern executor runs as a sidecar instead: adopt its probe frame.
void		RB_GLPBR_AdoptExecutorFrame( void );
void		RB_GLPBR_BeginView( void );
void		RB_GLPBR_Shutdown( void );
void		RB_GLPBR_PrintInfo( void );

#endif
