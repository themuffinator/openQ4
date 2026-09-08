// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesMonochromeShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 monochrome.vfp -- fragment stage (D7c). Ported from
// Vulkan/shaders/monochrome.frag; see monochromeShaderVP.cpp on why this is a
// reconstruction rather than a port of an original.
//
// Sampled alpha is preserved rather than replaced, so the material's authored
// blend stays authoritative. The 0.33 weights are Vulkan's and are kept rather
// than corrected to luma coefficients: matching the other backend matters more
// than being right about a program that has no original to be right about.

uniform sampler2D uTexture0;
uniform vec4 uColor;
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
uniform int uAlphaTestFunc;
#endif

in vec2 vTexCoord;

out vec4 fragColor;

void main() {
    vec4 sampled = texture(uTexture0, vTexCoord);
    float luminance = dot(sampled.rgb, vec3(0.33));
    vec4 color = vec4(vec3(luminance), sampled.a) * uColor;

#ifdef GLESD3_ALPHATEST
    // Match the fixed-function alpha comparison, including equality boundaries.
    float alpha = clamp(color.a, 0.0, 1.0);
    if ((uAlphaTestFunc == 514 && alpha != uAlphaTest) || // GL_EQUAL
        (uAlphaTestFunc == 513 && alpha >= uAlphaTest) || // GL_LESS
        (uAlphaTestFunc == 518 && alpha < uAlphaTest) ||  // GL_GEQUAL
        (uAlphaTestFunc == 516 && alpha <= uAlphaTest)) { // GL_GREATER
        discard;
    }
#endif
    fragColor = color;
}
)";
