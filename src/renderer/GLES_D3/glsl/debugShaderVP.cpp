// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDebugShaderVP = R"(#version 300 es
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

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    vColor = inColor;
    vTexCoord = inTexCoord;
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
