// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDebugShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

uniform vec4 uColor;
uniform float uAlphaTest;

in vec4 vColor;
in vec2 vTexCoord;

out vec4 outColor;

void main() {
    vec4 result = vColor * uColor;

    // ES has no fixed-function alpha test. GL_State's GLS_ATEST bits are
    // translated to this reference value in gles_draw.cpp; < 0 disables.
    if (uAlphaTest >= 0.0 && result.a <= uAlphaTest) {
        discard;
    }

    outColor = result;
}
)";
