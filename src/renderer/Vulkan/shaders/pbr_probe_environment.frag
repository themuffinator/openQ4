#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../PBRMath.h"

// openQ4 Vulkan interaction pipeline â€” fragment stage (Phase F1).
//
// interaction.vfp parity math: DXT5/RXGB bump decode (alpha=x, green=y,
// blue=z, no renormalization), projected light-falloff and light-projection
// samples, diffuse map Ã— diffuseColor, and specular through the REAL
// _specularTable ramp â€” the table carries clamp((NÂ·H)Â·4âˆ’3)Â² and the CPU-side
// ARB2 path doubles the specular env constant, so the Ã—2 lives here.
// Direction vectors normalize in-shader (no normalization cube map, locked
// Phase F decision). Ambient lights substitute the constant tangent-space
// direction the ambient normal-map cube decodes to (pushed as pc.b, with
// the cube's 8-bit quantization applied CPU-side). The shipped
// Parallaxbump custom-lighting guide enables a scale/bias height offset
// through pc.c; the otherwise-unused red channel of RXGB normal maps holds
// the height while alpha/green/blue retain the tangent-space normal.
// Additive ONE:ONE blend; alpha writes 0 like the GL reference.

layout(set = 0, binding = 0) uniform sampler2D specularTableMap;
layout(set = 1, binding = 0) uniform sampler2D bumpMap;
layout(set = 2, binding = 0) uniform sampler2D lightFalloffMap;
layout(set = 3, binding = 0) uniform sampler2D lightProjectionMap;
layout(set = 4, binding = 0) uniform sampler2D diffuseMap;
layout(set = 5, binding = 0) uniform sampler2D specularMap;

layout(push_constant) uniform InteractionPushConstants {
    mat4 mvp;
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 d;
} pc;

layout(set = 6, binding = 0, std140) uniform InteractionBlock {
    vec4 localLightOrigin;
    vec4 localViewOrigin;
    vec4 lightProjectionS;
    vec4 lightProjectionT;
    vec4 lightProjectionQ;
    vec4 lightFalloffS;
    vec4 bumpMatrixS;
    vec4 bumpMatrixT;
    vec4 diffuseMatrixS;
    vec4 diffuseMatrixT;
    vec4 specularMatrixS;
    vec4 specularMatrixT;
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 flatDiffuseParams;
    // cel banding (RB_SetCelInteractionUniform): x enabled, y band count,
    // z hard specular, w softness; zero when the surface is not banded
    vec4 celParams;
} inter;

layout(location = 0) in vec2 vBumpTexCoord;
layout(location = 1) in vec2 vDiffuseTexCoord;
layout(location = 2) in vec2 vSpecularTexCoord;
layout(location = 3) in vec4 vLightFalloffTexCoord;
layout(location = 4) in vec4 vLightProjectionTexCoord;
layout(location = 5) in vec3 vLightVector;
layout(location = 6) in vec3 vHalfAngleVector;
layout(location = 7) in vec3 vVertexColor;
layout(location = 8) in vec3 vViewVector;

// Pack the basis into unused components so the projected receiver remains
// within Vulkan's minimum 16 varying locations (0..15).
layout(location = 12, component = 1) in vec3 vPBRTangent0;
layout(location = 14, component = 1) in vec3 vPBRTangent1;
layout(location = 15) in vec3 vPBRNormal;

layout(location = 0) out vec4 outColor;

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

#define PBR_AUTHORED_PROBES
#include "pbr_direct.glsl"
#include "pbr_environment.glsl"

#ifdef VK_HDR_DOMAIN_MRT
layout(location = 1) out vec4 outPBRColor;
#define main HDRMaterialMain
#endif

void main() {
    PBRParallaxBegin(true);
    outColor = vec4(EvaluatePBREnvironment(), PBRTransparentAlpha(vDiffuseTexCoord));
}

#ifdef VK_HDR_DOMAIN_MRT
#undef main
void main() {
    HDRMaterialMain();
    // Both attachments use the same blend state. Preserve source alpha in
    // both outputs for coverage/blending; the primary stores scene alpha.
    bool nativePBR = true;
    outPBRColor = vec4(nativePBR ? outColor.rgb : vec3(0.0), outColor.a);
    if (nativePBR) outColor.rgb = vec3(0.0);
}
#endif
