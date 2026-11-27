#version 450
#extension GL_GOOGLE_include_directive : require
#include "pbr_vertex.glsl"

// openQ4 Vulkan shadow-receiving interaction — vertex stage (Phase F2a,
// docs/dev/archive/2026-10-18/plans/2026-07-19-vulkan-phase-f.md).
//
// interaction.vert plus the projected shadow coordinate of the GL
// shadow_interaction.vs contract: four sets of shadow rows are the light's
// cascade clip planes localized to the surface's model space CPU-side per
// space (draw_arb2.cpp:8552-8581), so every shadow coordinate is four dot
// products against the raw model-space position.
// Normal-offset shadows push the sampled point along the geometric normal by
// (world texel size × sinθ) before projecting, fixing self-shadow acne
// structurally where pure depth bias would detach contact shadows.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent0;
layout(location = 4) in vec3 inTangent1;
layout(location = 5) in vec2 inTexCoord;

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

layout(set = 7, binding = 1, std140) uniform ShadowBlock {
    vec4 shadowRow0[4];
    vec4 shadowRow1[4];
    vec4 shadowRow2[4];
    vec4 shadowRow3[4];
    vec4 atlasRects[4];
    vec4 splitDepths;
    vec4 cascadeBiasScale;
    vec4 texelDepthStep;
    vec4 normalOffsetWorld;
    vec4 viewDepthRow;
    vec4 biasParams;   // x: constant bias, y: normal bias, z: cascade blend, w: cascade count
    vec4 texelSize;    // x,y: 1 / atlas dimensions, z: r_shadowMapTexelBiasScale
    vec4 filterParams; // x: radius, y: taps, z: mode, w: hardware compare
    vec4 pcssParams;   // x: light radius, y: max radius, z: effective radius, w: receiver-plane bias
} shadow;

layout(location = 0) out vec2 vBumpTexCoord;
layout(location = 1) out vec2 vDiffuseTexCoord;
layout(location = 2) out vec2 vSpecularTexCoord;
layout(location = 3) out vec4 vLightFalloffTexCoord;
layout(location = 4) out vec4 vLightProjectionTexCoord;
layout(location = 5) out vec3 vLightVector;
layout(location = 6) out vec3 vHalfAngleVector;
layout(location = 7) out vec3 vVertexColor;
layout(location = 8) out vec4 vShadowCoord0;
layout(location = 9) out vec4 vShadowCoord1;
layout(location = 10) out vec4 vShadowCoord2;
layout(location = 11) out vec4 vShadowCoord3;
layout(location = 12) out float vShadowLightCos;
layout(location = 13) out vec3 vViewVector;
layout(location = 14) out float vViewDepth;

// Pack the basis into unused components so the projected receiver remains
// within Vulkan's minimum 16 varying locations (0..15).
layout(location = 12, component = 1) out vec3 vPBRTangent0;
layout(location = 14, component = 1) out vec3 vPBRTangent1;
layout(location = 15) out vec3 vPBRNormal;

vec3 TangentSpaceVector(vec3 objectVector) {
    return vec3(
        dot(inTangent0, objectVector),
        dot(inTangent1, objectVector),
        dot(inNormal, objectVector));
}

// A negative normalOffsetWorld is the offset per unit of the receiver's light
// depth (row 3): a distant point light's texel grows with distance from its
// source (R_ShadowMapProjectedNormalOffsetWorld).
vec4 BuildShadowCoord(vec4 position, vec3 normalOffsetDir,
        float sinTheta, int cascadeIndex) {
    float offsetWorld = shadow.normalOffsetWorld[cascadeIndex];
    if (offsetWorld < 0.0) {
        offsetWorld = -offsetWorld
            * max(dot(position, shadow.shadowRow3[cascadeIndex]), 0.0);
    }
    vec4 offsetPosition = vec4(position.xyz + normalOffsetDir
        * (offsetWorld * sinTheta), 1.0);
    return vec4(
        dot(offsetPosition, shadow.shadowRow0[cascadeIndex]),
        dot(offsetPosition, shadow.shadowRow1[cascadeIndex]),
        dot(offsetPosition, shadow.shadowRow2[cascadeIndex]),
        dot(offsetPosition, shadow.shadowRow3[cascadeIndex]));
}

void main() {
    vec4 position = vec4(inPosition, 1.0);
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);

    vec3 toLight = inter.localLightOrigin.xyz - position.xyz;
    vec3 toView = inter.localViewOrigin.xyz - position.xyz;

    vLightVector = pc.d.x > 0.5 ? toLight : TangentSpaceVector(toLight);
    vHalfAngleVector = TangentSpaceVector(normalize(toLight) + normalize(toView));
    // PBR dot products use the interpolated object-space material normal.
    vViewVector = pc.d.x > 0.5 ? toView : TangentSpaceVector(toView);
    vPBRTangent0 = inTangent0;
    vPBRTangent1 = inTangent1;
    vPBRNormal = inNormal;
    if (pc.d.x > 0.5) {
        PBRVertexFrame(inNormal, inTangent0, inTangent1, vPBRNormal, vPBRTangent0, vPBRTangent1);
    }

    vBumpTexCoord = vec2(dot(texCoord, inter.bumpMatrixS), dot(texCoord, inter.bumpMatrixT));
    vDiffuseTexCoord = vec2(dot(texCoord, inter.diffuseMatrixS), dot(texCoord, inter.diffuseMatrixT));
    vSpecularTexCoord = vec2(dot(texCoord, inter.specularMatrixS), dot(texCoord, inter.specularMatrixT));

    // z is unused by textureProj for this 2D sampler; carry model-local Z.
    vLightFalloffTexCoord = vec4(dot(position, inter.lightFalloffS), 0.5, position.z, 1.0);
    vLightProjectionTexCoord = vec4(
        dot(position, inter.lightProjectionS),
        dot(position, inter.lightProjectionT),
        0.0,
        dot(position, inter.lightProjectionQ));

    vec3 shadowNormal = normalize(inNormal);
    float shadowLightCos = max(dot(shadowNormal, normalize(toLight)), 0.0);
    float shadowSinTheta = sqrt(max(1.0 - shadowLightCos * shadowLightCos, 0.0));
    vShadowCoord0 = BuildShadowCoord(position, shadowNormal, shadowSinTheta, 0);
    vShadowCoord1 = BuildShadowCoord(position, shadowNormal, shadowSinTheta, 1);
    vShadowCoord2 = BuildShadowCoord(position, shadowNormal, shadowSinTheta, 2);
    vShadowCoord3 = BuildShadowCoord(position, shadowNormal, shadowSinTheta, 3);
    vShadowLightCos = shadowLightCos;
    // Unclamped, as shadow_interaction.vs: a vertex behind the eye clamped to
    // zero bends the depth interpolated across its triangle, and the pixels at
    // the player's feet took the next cascade.
    vViewDepth = dot(position, shadow.viewDepthRow);

    vVertexColor = inColor.rgb * pc.a.x + vec3(pc.a.y);

    gl_Position = pc.mvp * position;
}
