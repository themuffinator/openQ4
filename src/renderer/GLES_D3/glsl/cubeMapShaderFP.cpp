// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesCubeMapShaderFP = R"(#version 300 es
precision highp float;
precision highp int;
precision highp samplerCube;

// gles_d3 cube-map texgen -- fragment stage (D7a).
//
// The GL reference feeds the same three-float texcoord to a
// GL_TEXTURE_CUBE_MAP and the cube faces upload in the same +X..-Z layer
// order, so sampling along the interpolated direction is all there is to it.
//
// uAlphaTest and uAlphaTestFunc reproduce the fixed-function comparison.

uniform samplerCube uCubeMap;
uniform vec4 uColor;
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
uniform int uAlphaTestFunc;
#endif

in vec3 vTexDir;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 color = texture(uCubeMap, vTexDir) * uColor * vColor;

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
