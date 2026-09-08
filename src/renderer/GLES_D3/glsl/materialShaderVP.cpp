// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesMaterialShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;

// the stage texture matrix, as two rows against (s, t, 0, 1) -- the same form
// R_SetDrawInteraction builds for the interaction path. Always written, even
// when identity: GL zero-initialises unset uniforms and a zero matrix
// collapses every texture coordinate onto one texel.
uniform vec4 uTexMatrixS;
uniform vec4 uTexMatrixT;

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    vColor = inColor;

    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);
    vTexCoord = vec2(dot(texCoord, uTexMatrixS), dot(texCoord, uTexMatrixT));

    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
