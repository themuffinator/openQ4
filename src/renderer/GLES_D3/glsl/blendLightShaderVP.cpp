// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesBlendLightShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// gles_d3 blend light -- vertex stage (D6).
//
// Port of the RB_T_BlendLight texgen contract (draw_common.cpp:7864-7898), and
// a transliteration of Vulkan/shaders/blend_light.vert.
//
// tex0 projects S/T/Q from the model-local lightProject[0..2] planes; tex1
// takes S from lightProject[3] (the falloff) with T pinned at 0.5, which is
// GL's glTexCoord2f(0, 0.5) for the second unit.
//
// A light stage's texture matrix is folded into the S and T planes CPU-side by
// R_GLESD3_BakeTextureMatrixIntoTexgen, which is what replaces GL's texture
// matrix -- so the shader stays a plain projection with no matrix of its own.
// The whole thing is a strict subset of the interaction shader's projection
// math, and shares its uniform names for that reason.

layout(location = 0) in vec3 inPosition;

uniform mat4 uMVP;
uniform vec4 uLightProjectionS;
uniform vec4 uLightProjectionT;
uniform vec4 uLightProjectionQ;
uniform vec4 uLightFalloffS;

out vec4 vProjTexCoord;
out vec2 vFalloffTexCoord;

void main() {
    vec4 position = vec4(inPosition, 1.0);

    vProjTexCoord = vec4(
            dot(position, uLightProjectionS),
            dot(position, uLightProjectionT),
            0.0,
            dot(position, uLightProjectionQ));
    vFalloffTexCoord = vec2(dot(position, uLightFalloffS), 0.5);

    gl_Position = uMVP * position;
}
)";
