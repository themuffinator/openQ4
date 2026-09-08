// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesFogShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// gles_d3 fog light -- vertex stage (D6).
//
// Port of the RB_T_BasicFog texgen contract (draw_common.cpp:7993-8019), and a
// straight transliteration of Vulkan/shaders/fog.vert. Three model-local
// planes produce the two fog texture coordinates:
//
//   tex0 (_fog)       S = the eye-depth density ramp (the view-space Z plane
//                         scaled by the fog distance, with the 0.5 centre bias
//                         folded in CPU-side)
//                     T = the constant 0.5 row. The GL path computes
//                         FOG_DISTANCE_PLANE_T and then overwrites it with
//                         (0,0,0,0.5) on every surface, so the constant is
//                         written here instead of carried as a uniform.
//   tex1 (_fogEnter)  S = the viewer's constant scaled distance to the fog
//                         plane -- a zero-normal plane, so it localizes to
//                         itself and is the same value for every vertex
//                     T = this fragment's scaled distance to the fog plane,
//                         with FOG_ENTER folded in CPU-side
//
// Position is the only attribute read: the fog volume has no lighting, no
// vertex colour and no authored texture coordinate.

layout(location = 0) in vec3 inPosition;

uniform mat4 uMVP;
uniform vec4 uFogDistanceS;		// tex0 S: local fog distance plane (+0.5)
uniform vec4 uFogEnterT;		// tex1 T: local fog enter plane (+FOG_ENTER)
uniform vec4 uFogEnterS;		// tex1 S: constant viewer-distance plane

out vec2 vFogTexCoord;
out vec2 vEnterTexCoord;

void main() {
    vec4 position = vec4(inPosition, 1.0);

    vFogTexCoord = vec2(dot(position, uFogDistanceS), 0.5);
    vEnterTexCoord = vec2(dot(position, uFogEnterS), dot(position, uFogEnterT));

    gl_Position = uMVP * position;
}
)";
