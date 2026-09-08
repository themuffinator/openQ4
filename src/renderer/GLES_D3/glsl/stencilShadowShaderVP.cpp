// Copyright (C) 2026 DarkMatter Productions
//
// Shadow volume extrusion -- vertex stage.
//
// Shadow geometry is shadowCache_t, a bare idVec4 per vertex (Model.h:69),
// using the homogeneous-coordinate trick: w == 1 marks a vertex that stays
// where it is, w == 0 one that must be projected to infinity away from the
// light.
//
// With uLightOrigin carrying w == 0 (the CPU sets it that way, matching the
// ARB2 path's PP_LIGHT_ORIGIN), one expression covers both:
//
//     inPosition.w * uLightOrigin + inPosition - uLightOrigin
//
//   w == 1  ->  inPosition                      the near cap, unmoved
//   w == 0  ->  inPosition - uLightOrigin       a direction, i.e. a point at
//                                               infinity along the light ray
//
// This is what the ARB2 shadow vertex program does, and what the d3es GLES
// port does; it needs no depth clamp, which is fortunate because ES has none.

#include "glsl_shaders.h"

const char * const glesStencilShadowShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// NOTE: vec4, not vec3 -- shadow vertices are shadowCache_t, and the w
// component is the extrusion flag, not padding.
layout(location = 0) in vec4 inPosition;

uniform mat4 uMVP;
uniform vec4 uLightOrigin;

void main() {
    gl_Position = uMVP * (inPosition.w * uLightOrigin + inPosition - uLightOrigin);
}
)";
