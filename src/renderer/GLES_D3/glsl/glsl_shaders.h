// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- GLSL ES 300 shader sources.

	One translation unit per shader stage, each a complete, self-contained
	program in a raw string literal: `#version 300 es` and the precision
	defaults included, so a file can be pasted straight into a validator and
	a disk override (r_glesD3ShaderPath) is byte-identical to what ships.

	Layout follows the d3es/neo/renderer/glsl convention.

	Two ES rules worth remembering while editing these:

	  - a fragment shader MUST declare a default float precision, or it does
	    not compile at all. That is the most common way a desktop-authored
	    shader breaks here.
	  - `highp` is requested explicitly rather than left to the default. On a
	    tiled mobile GPU mediump is 16-bit, and the interaction math (tangent
	    space vectors, light projection) visibly bands at that precision.

	EVERY vertex shader here declares `invariant gl_Position;`. That is not
	defensive tidiness, it is load-bearing, and leaving it out produced a real
	defect:

	  This renderer is multi-pass over a depth prepass. The prepass writes
	  depth with the MATERIAL program; the interaction pass then draws lighting
	  at GLS_DEPTHFUNC_EQUAL with the INTERACTION program, and the cube-texgen
	  stages draw at EQUAL with their own programs. Different program objects.

	  GLSL does not promise that two programs computing the same expression
	  produce bit-identical results -- and ANGLE translates each program to
	  Metal separately, so they genuinely do not. Wherever the two disagree by
	  one ULP, GL_EQUAL rejects the fragment and the surface receives NO
	  LIGHTING: it keeps the black the depth prepass painted.

	  Reported from play as "the hull of the dropship is pitch black in
	  places... there are other black areas around the map", with the decisive
	  clue that the surface reappears when smoke passes in front of it. Smoke
	  is translucent and draws at GLS_DEPTHFUNC_LESS, which has no equality to
	  fail -- so the passes that survived were exactly the ones not testing
	  EQUAL. A screenshot comparison at one viewpoint did NOT show it, because
	  which fragments diverge depends on the view.

	`invariant` forces the compiler to compute gl_Position the same way in
	every program, which is the whole reason the qualifier exists.


===============================================================================
*/

#ifndef __GLES_D3_GLSL_SHADERS_H__
#define __GLES_D3_GLSL_SHADERS_H__

// flat colour with an MVP transform; the D1 proof program and the bring-up
// probe behind r_glesD3TestTriangle
extern const char * const glesDebugShaderVP;
extern const char * const glesDebugShaderFP;

// one authored material stage: texture x stage colour x vertex colour
extern const char * const glesMaterialShaderVP;
extern const char * const glesMaterialShaderFP;

// per-light bump/diffuse/specular, ported from Vulkan/shaders/interaction.*
extern const char * const glesInteractionShaderVP;
extern const char * const glesInteractionShaderFP;

// shadow volume extrusion; consumes shadowCache_t (vec4), not idDrawVert
extern const char * const glesStencilShadowShaderVP;
extern const char * const glesStencilShadowShaderFP;

// fog volumes: the _fog x _fogEnter modulate chain, ported from
// Vulkan/shaders/fog.*
extern const char * const glesFogShaderVP;
extern const char * const glesFogShaderFP;

// blend lights: projection x falloff x stage colour, ported from
// Vulkan/shaders/blend_light.*
extern const char * const glesBlendLightShaderVP;
extern const char * const glesBlendLightShaderFP;

// TG_SKYBOX_CUBE / TG_WOBBLESKY_CUBE / TG_DIFFUSE_CUBE -- a cube sampled along
// a per-vertex direction. Ported from Vulkan/shaders/sky.*
extern const char * const glesCubeMapShaderVP;
extern const char * const glesCubeMapShaderFP;

// TG_REFLECT_CUBE on a material with no bump stage, from
// Vulkan/shaders/environment.*
extern const char * const glesEnvironmentShaderVP;
extern const char * const glesEnvironmentShaderFP;

// TG_REFLECT_CUBE on a material that has one, from
// Vulkan/shaders/bumpy_environment.*
extern const char * const glesBumpyEnvironmentShaderVP;
extern const char * const glesBumpyEnvironmentShaderFP;

// the authored heatHaze material programs, from Vulkan/shaders/heathaze.* and
// glprogs/heatHaze.vfp. One vertex stage for all four variants; the fragment
// stage splits on whether the material carries a mask map.
extern const char * const glesHeatHazeShaderVP;
extern const char * const glesHeatHazeShaderFP;
extern const char * const glesHeatHazeMaskShaderFP;

// monochrome.vfp. A reconstruction, not a port: no shipped archive contains
// the program. See monochromeShaderVP.cpp.
extern const char * const glesMonochromeShaderVP;
extern const char * const glesMonochromeShaderFP;

#endif /* !__GLES_D3_GLSL_SHADERS_H__ */
