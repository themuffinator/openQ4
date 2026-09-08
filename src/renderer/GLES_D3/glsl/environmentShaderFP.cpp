// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesEnvironmentShaderFP = R"(#version 300 es
precision highp float;
precision highp int;
precision highp samplerCube;

// gles_d3 TG_REFLECT_CUBE without a bump stage -- fragment stage (D7a).
//
// Transliterated from Vulkan/shaders/environment.frag. The reflection is the
// standard R = 2(E.N)N - E about the interpolated normal, both renormalized
// here rather than in the vertex stage.

uniform samplerCube uCubeMap;
uniform vec4 uColor;
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
#endif

in vec3 vNormal;
in vec3 vToEye;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 toEye = normalize(vToEye);
    vec3 r = 2.0 * dot(toEye, n) * n - toEye;

    vec4 color = texture(uCubeMap, r) * uColor * vColor;

#ifdef GLESD3_ALPHATEST
    if (uAlphaTest >= 0.0 && color.a <= uAlphaTest) {
        discard;
    }
#endif
    fragColor = color;
}
)";
