// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesBumpyEnvironmentShaderFP = R"(#version 300 es
precision highp float;
precision highp int;
precision highp samplerCube;

// gles_d3 TG_REFLECT_CUBE with a bump stage -- fragment stage (D7a).
//
// Transliterated from Vulkan/shaders/bumpy_environment.frag, including two
// deliberate infidelities-to-textbook that are faithful to the retail ARB
// program:
//
//   - the interpolated tangent frame is NOT renormalized before the tangent
//     -> global transform, only the perturbed result is used as-is;
//   - the result ignores both stage and vertex colour, and forces alpha to 1.
//     The retail program has the vertex-colour multiply commented out and uses
//     MOV. Stage registers still gate the draw through the renderer's ordinary
//     skip rules, they just do not tint it.

uniform samplerCube uCubeMap;
uniform sampler2D uTexture1;	// the material's bump stage
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
uniform int uAlphaTestFunc;
#endif

in vec2 vNormalTexCoord;
in vec3 vGlobalToEye;
in vec3 vGlobalTangent;
in vec3 vGlobalBitangent;
in vec3 vGlobalNormal;

out vec4 fragColor;

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

void main() {
    vec4 bumpSample = texture(uTexture1, vNormalTexCoord);

    // X in alpha, Y in green, and Z from blue unless the format has no blue --
    // EAC_RG11 decodes it to -1.0, which no real normal reaches. The same
    // decode the interaction shader uses; see there for why it is a test on
    // the value rather than a shader permutation.
    vec2 localNormalXY = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;
    float storedZ = bumpSample.b * 2.0 - 1.0;
    float rebuiltZ = sqrt(max(1.0 - dot(localNormalXY, localNormalXY), 0.0));
    vec3 localNormal = vec3(localNormalXY, storedZ < 0.0 ? rebuiltZ : storedZ);
    localNormal = SafeNormalize(localNormal);

    vec3 globalNormal =
        localNormal.x * vGlobalTangent +
        localNormal.y * vGlobalBitangent +
        localNormal.z * vGlobalNormal;
    vec3 globalEye = SafeNormalize(vGlobalToEye);

    vec3 reflectionVector = 2.0 * dot(globalEye, globalNormal) * globalNormal - globalEye;

    vec4 color = vec4(texture(uCubeMap, reflectionVector).rgb, 1.0);

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
