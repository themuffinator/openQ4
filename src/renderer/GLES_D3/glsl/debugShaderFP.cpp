// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDebugShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

uniform vec4 uColor;
uniform float uAlphaTest;
uniform int uAlphaTestFunc;

in vec4 vColor;
in vec2 vTexCoord;

out vec4 outColor;

void main() {
    vec4 result = vColor * uColor;

    // ES has no fixed-function alpha test. GL_State's GLS_ATEST bits are
    // translated to this reference value in gles_draw.cpp; < 0 disables.
    // Match the fixed-function alpha comparison, including equality boundaries.
    float alpha = clamp(result.a, 0.0, 1.0);
    if ((uAlphaTestFunc == 514 && alpha != uAlphaTest) || // GL_EQUAL
        (uAlphaTestFunc == 513 && alpha >= uAlphaTest) || // GL_LESS
        (uAlphaTestFunc == 518 && alpha < uAlphaTest) ||  // GL_GEQUAL
        (uAlphaTestFunc == 516 && alpha <= uAlphaTest)) { // GL_GREATER
        discard;
    }

    outColor = result;
}
)";
