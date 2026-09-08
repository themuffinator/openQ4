// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- GLSL ES 300 program manager.

	The Doom 3 shape: a program handle plus a fixed set of uniform and
	attribute locations resolved once at link time, so the draw path costs a
	struct field read rather than a glGetUniformLocation per draw.


===============================================================================
*/

#ifndef __GLES_PROGRAM_H__
#define __GLES_PROGRAM_H__

#ifdef OPENQ4_RENDERER_GLES_MODULE

/*
====================
glesD3Attribute_t

Vertex attribute locations, bound explicitly before link so every program in
the backend shares one layout and one idDrawVert binding routine.

These deliberately match the Vulkan interaction shaders' `layout(location=)`
numbering (Vulkan/shaders/interaction.vert:17-22). The two shader sets are
ports of each other and stay diffable only if the locations agree.
====================
*/
typedef enum {
	GLESD3_ATTR_POSITION = 0,
	GLESD3_ATTR_COLOR = 1,
	GLESD3_ATTR_NORMAL = 2,
	GLESD3_ATTR_TANGENT = 3,
	GLESD3_ATTR_BITANGENT = 4,
	GLESD3_ATTR_TEXCOORD = 5,
	// The cube-map texgen direction. Unlike 0-5 this one does NOT come from the
	// interleaved idDrawVert range: the skybox and wobblesky generators leave a
	// tightly packed vec3 stream in surf->dynamicTexCoords, which is a
	// different buffer object with a different stride. It sits past the Vulkan
	// locations deliberately -- those describe one idDrawVert layout, and
	// overloading one of them would mean re-specifying its format on every
	// switch between a cube stage and an ordinary one.
	GLESD3_ATTR_TEXDIR = 6,
	GLESD3_ATTR_COUNT
} glesD3Attribute_t;

/*
====================
glesProgram_t

A uniform absent from a program links as -1, and glUniform* with -1 is
defined as a no-op, so callers set uniforms unconditionally and programs
carry only what they use.
====================
*/
typedef struct glesProgram_s {
	GLuint		program;
	char		name[ 64 ];

	// common uniforms; -1 when the program does not declare one
	GLint		uMVP;				// mat4, model-to-clip
	GLint		uColor;				// vec4, flat/stage colour
	GLint		uTextureMatrix;		// mat4, stage texture matrix
	GLint		uAlphaTest;			// float, alpha-test reference
	GLint		uAlphaTestFunc;		// int, GL comparison enum
	GLint		uTexture0;			// sampler2D
	GLint		uTexture1;
	GLint		uTexture2;
	GLint		uTexMatrixS;		// vec4, stage texture matrix row S
	GLint		uTexMatrixT;		// vec4, row T
	GLint		uVertexColor;		// vec4, (rgbMul, rgbAdd, alphaMul, alphaAdd) -- the SVC packing

	// interaction program only
	GLint		uLocalLightOrigin;
	GLint		uLocalViewOrigin;
	GLint		uLightProjectionS;
	GLint		uLightProjectionT;
	GLint		uLightProjectionQ;
	GLint		uLightFalloffS;
	GLint		uBumpMatrixS;
	GLint		uBumpMatrixT;
	GLint		uDiffuseMatrixS;
	GLint		uDiffuseMatrixT;
	GLint		uSpecularMatrixS;
	GLint		uSpecularMatrixT;
	GLint		uDiffuseColor;
	GLint		uSpecularColor;
	GLint		uAmbientDir;

	// stencil shadow program only
	GLint		uLightOrigin;		// vec4, local light origin with w = 0

	// fog program only. The blend light program has no uniforms of its own:
	// its projection planes are a strict subset of the interaction program's,
	// so it reuses uLightProjection* / uLightFalloffS / uColor above.
	GLint		uFogDistanceS;		// vec4, tex0 S -- the eye-depth density ramp
	GLint		uFogEnterS;			// vec4, tex1 S -- constant viewer distance
	GLint		uFogEnterT;			// vec4, tex1 T -- distance to the fog plane

	// cube-map texgen programs. uCubeMap is a samplerCube on unit 0 and is
	// bound at link time; the model rows are only used by bumpyEnvironment,
	// which reflects in global space.
	GLint		uCubeMap;
	GLint		uModelRow0;
	GLint		uModelRow1;
	GLint		uModelRow2;

	// material-program stages (D7c). One vec4[8] rather than eight named
	// uniforms, matching the Vulkan StageParms block one-for-one so the two
	// shader sets stay diffable -- the ports were made from each other and the
	// slot numbering is the shared contract.
	GLint		uParms;
} glesProgram_t;

typedef enum {
	// flat-coloured geometry with an MVP transform. The D1 proof program, and
	// the fallback any later pass can fall back to for a visible defect.
	GLESD3_PROGRAM_DEBUG = 0,
	// one authored material stage: texture x stage colour x vertex colour,
	// with the stage texture matrix and shader-side alpha test. Draws the
	// world's ambient stages, every GUI and the 2D console alike.
	GLESD3_PROGRAM_MATERIAL,
	// per-light bump/diffuse/specular; the port of the Vulkan interaction pair
	GLESD3_PROGRAM_INTERACTION,
	// shadow volume extrusion, writing stencil only
	GLESD3_PROGRAM_STENCIL_SHADOW,
	// fog volumes: the _fog x _fogEnter density chain over a light's receivers
	// and its frustum cap
	GLESD3_PROGRAM_FOG,
	// blend lights: the light's projection x falloff x stage colour, blended
	// by the stage's own blend keyword
	GLESD3_PROGRAM_BLEND_LIGHT,
	// a cube sampled along a per-vertex direction: TG_SKYBOX_CUBE,
	// TG_WOBBLESKY_CUBE, TG_DIFFUSE_CUBE
	GLESD3_PROGRAM_CUBEMAP,
	// TG_REFLECT_CUBE, the two variants ARB2 picks between on whether the
	// material carries a bump stage
	GLESD3_PROGRAM_ENVIRONMENT,
	GLESD3_PROGRAM_BUMPY_ENVIRONMENT,
	// the authored material programs (D7c). heatHaze.vfp and its three mask
	// variants share one vertex stage and split on the fragment stage.
	GLESD3_PROGRAM_HEATHAZE,
	GLESD3_PROGRAM_HEATHAZE_MASK,
	GLESD3_PROGRAM_MONOCHROME,
	GLESD3_PROGRAM_COUNT
} glesD3ProgramId_t;

/*
====================
glesD3ProgramVariant_t

Compile-time shader specialisation (D8). A `discard` anywhere in a fragment
shader disables early-Z / LRZ / FPK on tile-based mobile GPUs for every draw
that binds it, even when the test never fires -- so the shader-side alpha
test cannot live behind a runtime uniform in the common path. Each program
that alpha-tests is therefore linked twice: the BASE variant carries no
uAlphaTest and no discard, and the ALPHATEST variant is the same source with
GLESD3_ALPHATEST defined. The interaction program splits the same way on
GLESD3_AMBIENT, removing the per-fragment uniform test (and the always-black
specular chain) from the per-light hot path.

The define is spliced in directly after the `#version` line, identically for
the embedded source and a disk override, so a shader file on disk stays
byte-identical to what ships and compiles standalone as the BASE variant.

A program declares at most one alternate define, so the table is two slots
wide; asking for a variant a program does not declare returns NULL, the same
fail-closed contract as a program that failed to link.
====================
*/
typedef enum {
	GLESD3_VARIANT_BASE = 0,
	GLESD3_VARIANT_ALPHATEST,	// GLESD3_ALPHATEST: uAlphaTest + discard compiled in
	GLESD3_VARIANT_AMBIENT		// GLESD3_AMBIENT: interaction lit by uAmbientDir
} glesD3ProgramVariant_t;

// Builds every program. Safe to call repeatedly; reload goes through here.
// Returns false if any program failed, having logged each failure by name.
bool			R_GLESD3_Programs_Init( void );
void			R_GLESD3_Programs_Shutdown( void );

// NULL when the id is out of range, the program failed to link, or the
// program does not declare the requested variant, so callers can fail closed
// on a per-pass basis rather than drawing with program 0.
glesProgram_t *	R_GLESD3_Program( glesD3ProgramId_t id,
		glesD3ProgramVariant_t variant = GLESD3_VARIANT_BASE );

// glUseProgram with redundancy filtering against the backend's own shadow of
// the current program.
void			R_GLESD3_UseProgram( const glesProgram_t *program );

// Drops the shadow after anything outside this backend has issued glUseProgram.
void			R_GLESD3_InvalidateProgramState( void );

// gles_shaders.cpp: the embedded GLSL ES 300 sources.
// stage is GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
const char *	R_GLESD3_EmbeddedShaderSource( glesD3ProgramId_t id, GLenum stage );
const char *	R_GLESD3_ProgramName( glesD3ProgramId_t id );

#endif /* OPENQ4_RENDERER_GLES_MODULE */

#endif /* !__GLES_PROGRAM_H__ */
