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
// uAlphaTest follows this backend's single-reference convention (see
// R_GLESD3_AlphaTestReference): negative disables, otherwise discard at or
// below the reference.

uniform samplerCube uCubeMap;
uniform vec4 uColor;
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
#endif

in vec3 vTexDir;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 color = texture(uCubeMap, vTexDir) * uColor * vColor;

#ifdef GLESD3_ALPHATEST
    if (uAlphaTest >= 0.0 && color.a <= uAlphaTest) {
        discard;
    }
#endif
    fragColor = color;
}
)";
