#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../PBRMath.h"

// openQ4 Vulkan shadow-receiving interaction â€” fragment stage (Phase F2a).
//
// interaction.frag plus the projected shadow sample of the GL
// shadow_interaction.fs projected CSM contract: view-depth cascade selection
// and split-band blending over one contiguous per-light atlas block. Set 7
// exposes both a LEQUAL comparison sampler and an unfiltered raw-depth
// sampler, allowing the archived r_shadowMapDepthCompare preference and
// PCSS-lite blocker search without pipeline variants. Fixed/rotated Poisson
// modes use the stock 1/5/9/13 tap tiers; receiver-plane derivatives and the
// source-aware radius policy are supplied through the 512-byte shadow ABI.
// The shadow coordinate projects like the GL shader: xy/w for tile-local UV,
// z UNdivided as compare depth. Out-of-frustum receivers stay lit.

layout(set = 0, binding = 0) uniform sampler2D specularTableMap;
layout(set = 1, binding = 0) uniform sampler2D bumpMap;
layout(set = 2, binding = 0) uniform sampler2D lightFalloffMap;
layout(set = 3, binding = 0) uniform sampler2D lightProjectionMap;
layout(set = 4, binding = 0) uniform sampler2D diffuseMap;
layout(set = 5, binding = 0) uniform sampler2D specularMap;
layout(set = 7, binding = 0) uniform sampler2DShadow shadowCompareMap;
layout(set = 7, binding = 2) uniform sampler2D shadowRawMap;
// r_shadowMapTranslucentMoments: the moment atlas, one map per color channel
layout(set = 7, binding = 3) uniform sampler2D shadowMomentMapR;
layout(set = 7, binding = 4) uniform sampler2D shadowMomentMapG;
layout(set = 7, binding = 5) uniform sampler2D shadowMomentMapB;

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
    vec4 texelDepthStep; // stored depth per shadow texel of a slope-1 receiver
    vec4 normalOffsetWorld;
    vec4 viewDepthRow;
    vec4 biasParams;   // x: constant bias, y: normal bias, z: cascade blend, w: cascade count
    vec4 texelSize;    // x,y: 1 / atlas dimensions, z: r_shadowMapTexelBiasScale
    vec4 filterParams; // x: radius, y: taps, z: mode, w: hardware compare
    vec4 pcssParams;   // x: light radius, y: max radius, z: effective radius, w: receiver-plane bias
    vec4 debugParams;  // x: debug mode, y: fallback reason, zw: PCSS ownership UV offset
    // Translucent moments live in a separate atlas whose blocks mirror the
    // depth-atlas blocks at a uniform scale: momentUV = depthUV * x + yz.
    vec4 momentAtlas;  // x: scale (<= 0: no moments for this light), yz: bias, w: 1 / moment atlas size
    vec4 momentParams; // x: density, y: min variance, z: bleed reduction, w: filter radius in texels
} shadow;

#include "shadow_moment_resolve.glsl"

layout(location = 0) in vec2 vBumpTexCoord;
layout(location = 1) in vec2 vDiffuseTexCoord;
layout(location = 2) in vec2 vSpecularTexCoord;
layout(location = 3) in vec4 vLightFalloffTexCoord;
layout(location = 4) in vec4 vLightProjectionTexCoord;
layout(location = 5) in vec3 vLightVector;
layout(location = 6) in vec3 vHalfAngleVector;
layout(location = 7) in vec3 vVertexColor;
layout(location = 8) in vec4 vShadowCoord0;
layout(location = 9) in vec4 vShadowCoord1;
layout(location = 10) in vec4 vShadowCoord2;
layout(location = 11) in vec4 vShadowCoord3;
layout(location = 12) in float vShadowLightCos;
layout(location = 13) in vec3 vViewVector;
layout(location = 14) in float vViewDepth;

// Pack the basis into unused components so the projected receiver remains
// within Vulkan's minimum 16 varying locations (0..15).
layout(location = 12, component = 1) in vec3 vPBRTangent0;
layout(location = 14, component = 1) in vec3 vPBRTangent1;
layout(location = 15) in vec3 vPBRNormal;

layout(location = 0) out vec4 outColor;

// shadowMapDebugMode_t (tr_local.h), matching glprogs/shadow_interaction.fs.
const float kShadowDebugAtlas = 1.0;
const float kShadowDebugCascadeIndex = 2.0;
const float kShadowDebugProjectedUV = 3.0;
const float kShadowDebugProjectedDepth = 4.0;
const float kShadowDebugProjectedW = 5.0;
const float kShadowDebugInvalidMask = 6.0;
const float kShadowDebugBiasHeatmap = 7.0;
const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugCasterOffsetOff = 10.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;
const float kShadowDebugCompareDelta = 12.0;
const float kShadowDebugReceiverEligibility = 13.0;
const float kShadowDebugReceiverFallbackReason = 14.0;

bool ShadowDebugModeIs(float mode) {
    return abs(shadow.debugParams.x - mode) < 0.5;
}

// Modes that replace the lit output entirely, rather than only suppressing a
// term of the ordinary sample.
bool ShadowVisualDebugMode() {
    return (shadow.debugParams.x > 0.5
            && shadow.debugParams.x < kShadowDebugBiasOff - 0.5)
        || ShadowDebugModeIs(kShadowDebugCompareDelta)
        || ShadowDebugModeIs(kShadowDebugReceiverEligibility)
        || ShadowDebugModeIs(kShadowDebugReceiverFallbackReason);
}

bool ShadowReceiverDebugMode() {
    return ShadowDebugModeIs(kShadowDebugReceiverEligibility)
        || ShadowDebugModeIs(kShadowDebugReceiverFallbackReason);
}

// Computed in main before any divergent receiver control flow. Derivatives
// inside the cascade selector would be undefined for neighboring fragments
// that choose different cascades.
vec4 gShadowDepthGradients = vec4(0.0);

// Worst coordinate rejection seen while sampling this fragment: 1 = the w
// component was unusable, 2 = the projected coordinate was not finite. The
// invalid-mask view reads it after sampling.
float gShadowDebugState = 0.0;

// Cascade selection and split blend of the sample the lit output used, so the
// visualizations describe the pixel that was actually shaded.
int gShadowCascadeIndex = 0;
float gShadowCascadeBlend = 0.0;

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

#include "enhanced_material.glsl"

vec3 ApplyFlatDiffuseSweep(vec3 diffuse, float localZ) {
    if (inter.flatDiffuseParams.x <= 0.0) {
        return diffuse;
    }
    float height = clamp((localZ - inter.flatDiffuseParams.y)
        * inter.flatDiffuseParams.z, 0.0, 1.0);
    float distanceToBand = abs(height - fract(inter.flatDiffuseParams.w));
    distanceToBand = min(distanceToBand, 1.0 - distanceToBand);
    float band = 1.0 - smoothstep(0.045, 0.16, distanceToBand);
    return mix(diffuse, vec3(1.0), inter.flatDiffuseParams.x * band);
}

#include "pbr_direct.glsl"

int ShadowCascadeCount() {
    return clamp(int(shadow.biasParams.w + 0.5), 1, 4);
}

float CascadeComponent(vec4 values, int cascadeIndex) {
    return values[clamp(cascadeIndex, 0, 3)];
}

vec4 ShadowCoordByIndex(int cascadeIndex) {
    if (cascadeIndex <= 0) {
        return vShadowCoord0;
    }
    if (cascadeIndex == 1) {
        return vShadowCoord1;
    }
    if (cascadeIndex == 2) {
        return vShadowCoord2;
    }
    return vShadowCoord3;
}

vec4 AtlasRectByIndex(int cascadeIndex) {
    if (cascadeIndex <= 0) {
        return shadow.atlasRects[0];
    }
    if (cascadeIndex == 1) {
        return shadow.atlasRects[1];
    }
    if (cascadeIndex == 2) {
        return shadow.atlasRects[2];
    }
    return shadow.atlasRects[3];
}

// Two of the four ring taps of the five-tap kernel; the others mirror them.
const vec2 kShadowFiveTapRingA = vec2(0.670820, 0.223607);
const vec2 kShadowFiveTapRingB = vec2(-0.223607, 0.670820);

float StableShadowHash(vec3 value) {
    return fract(sin(dot(value, vec3(12.9898, 78.233, 37.719)))
        * 43758.5453);
}

// The kernel turns by a hash of the texel within the tile, not within the
// image, so a soft shadow's grain is the same wherever its tile lives: one
// atlas block this view, another the next, or the cached map read in place.
// Keyed on the image texel, the grain changed each time the tile moved.
mat2 ShadowOffsetRotation(vec2 tileTexel, float depth) {
    if (shadow.filterParams.z < 0.5) {
        return mat2(1.0, 0.0, 0.0, 1.0);
    }
    float angle = StableShadowHash(vec3(
        floor(tileTexel),
        floor(depth * 1024.0))) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, s, -s, c);
}

float ShadowDepthGradient(int cascadeIndex) {
    return CascadeComponent(gShadowDepthGradients, cascadeIndex);
}

float ShadowReceiverBias(int cascadeIndex) {
    if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
        return 0.0;
    }
    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    float normalBias = ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)
        ? 0.0 : shadow.biasParams.y;
    float scalarBias = (shadow.biasParams.x + normalBias * sinTheta)
        * CascadeComponent(shadow.cascadeBiasScale, cascadeIndex);
    float texelBias = shadow.texelSize.z * CascadeComponent(
        shadow.texelDepthStep, cascadeIndex) * (1.0 + slopeBias);
    float receiverPlaneBias = 0.0;
    if (shadow.pcssParams.w > 0.5
            && !ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)) {
        receiverPlaneBias = ShadowDepthGradient(cascadeIndex)
            * max(shadow.pcssParams.z, 1.0);
    }
    return max(max(scalarBias, 0.0),
        max(max(texelBias, receiverPlaneBias), 0.0));
}

// Off-centre PCF taps and PCSS blocker probes read the map several texels
// away from the receiver. A receiver at an angle to the light changes depth by
// texelDepthStep * slope per texel, so a tap on the side nearer the light
// would compare the receiver with its own surface and speckle it, more as the
// filter widens. Each tap therefore adds this bias per texel of its distance
// from the centre. The centre sample keeps the plain receiver bias, so contact
// shadows keep their reach. glprogs/shadow_interaction.fs matches it.
float ShadowFootprintBias(int cascadeIndex) {
    if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
        return 0.0;
    }
    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    return max(CascadeComponent(shadow.texelDepthStep, cascadeIndex), 0.0)
        * slopeBias;
}

// Fresh PCSS GLOBAL maps may store only local casters. The matching LOCAL
// block contains global casters. All reads are NEAREST, so minimum depth here
// is the exact union used by both blocker search and the later PCF comparisons.
float RawShadowDepth(vec2 uv) {
    float depth = texture(shadowRawMap, uv).r;
    if (any(notEqual(shadow.debugParams.zw, vec2(0.0)))) {
        depth = min(depth, texture(shadowRawMap, uv + shadow.debugParams.zw).r);
    }
    return depth;
}

float SampleShadowCompare(vec2 uv, float depth, int cascadeIndex,
        float tapBias) {
    float compareDepth = depth - ShadowReceiverBias(cascadeIndex) - tapBias;
    if (shadow.filterParams.w > 0.5) {
        return texture(shadowCompareMap, vec3(uv, compareDepth));
    }
    float storedDepth = RawShadowDepth(uv);
    return compareDepth <= storedDepth ? 1.0 : 0.0;
}

float ProjectedPCSSRadius(vec2 uv, float depth, int cascadeIndex,
        vec2 clampMin, vec2 clampMax, vec2 texelStep, mat2 rotation) {
    float baseRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.0 : shadow.filterParams.x;
    if (ShadowDebugModeIs(kShadowDebugPCFOff)
            || shadow.filterParams.z < 1.5 || shadow.filterParams.w > 0.5
            || shadow.pcssParams.x <= 0.0
            || shadow.pcssParams.y <= 0.0) {
        return baseRadius;
    }

    float compareDepth = depth - ShadowReceiverBias(cascadeIndex);
    float searchRadius = max(shadow.pcssParams.x, 0.5);
    vec2 searchTap = texelStep * searchRadius;
    // Without the footprint bias a tilted receiver finds itself as a blocker.
    float searchBias = ShadowFootprintBias(cascadeIndex) * searchRadius;
    float blockerDepth = 0.0;
    float blockerCount = 0.0;
    float d0 = RawShadowDepth(uv);
    if (d0 < compareDepth) {
        blockerDepth += d0;
        blockerCount += 1.0;
    }
    vec2 o1 = rotation * vec2(-0.326212, -0.405805);
    vec2 o2 = rotation * vec2(-0.840144, -0.073580);
    vec2 o3 = rotation * vec2(-0.695914, 0.457137);
    vec2 o4 = rotation * vec2(-0.203345, 0.620716);
    vec2 o5 = rotation * vec2(0.962340, -0.194983);
    vec2 o6 = rotation * vec2(0.473434, -0.480026);
    vec2 o7 = rotation * vec2(0.519456, 0.767022);
    vec2 o8 = rotation * vec2(0.185461, -0.893124);
    float d1 = RawShadowDepth(clamp(uv + o1 * searchTap,
        clampMin, clampMax));
    float d2 = RawShadowDepth(clamp(uv + o2 * searchTap,
        clampMin, clampMax));
    float d3 = RawShadowDepth(clamp(uv + o3 * searchTap,
        clampMin, clampMax));
    float d4 = RawShadowDepth(clamp(uv + o4 * searchTap,
        clampMin, clampMax));
    float d5 = RawShadowDepth(clamp(uv + o5 * searchTap,
        clampMin, clampMax));
    float d6 = RawShadowDepth(clamp(uv + o6 * searchTap,
        clampMin, clampMax));
    float d7 = RawShadowDepth(clamp(uv + o7 * searchTap,
        clampMin, clampMax));
    float d8 = RawShadowDepth(clamp(uv + o8 * searchTap,
        clampMin, clampMax));
    if (d1 < compareDepth - length(o1) * searchBias) {
        blockerDepth += d1;
        blockerCount += 1.0;
    }
    if (d2 < compareDepth - length(o2) * searchBias) {
        blockerDepth += d2;
        blockerCount += 1.0;
    }
    if (d3 < compareDepth - length(o3) * searchBias) {
        blockerDepth += d3;
        blockerCount += 1.0;
    }
    if (d4 < compareDepth - length(o4) * searchBias) {
        blockerDepth += d4;
        blockerCount += 1.0;
    }
    if (d5 < compareDepth - length(o5) * searchBias) {
        blockerDepth += d5;
        blockerCount += 1.0;
    }
    if (d6 < compareDepth - length(o6) * searchBias) {
        blockerDepth += d6;
        blockerCount += 1.0;
    }
    if (d7 < compareDepth - length(o7) * searchBias) {
        blockerDepth += d7;
        blockerCount += 1.0;
    }
    if (d8 < compareDepth - length(o8) * searchBias) {
        blockerDepth += d8;
        blockerCount += 1.0;
    }
    if (blockerCount <= 0.0) {
        return 0.0;
    }

    float averageBlocker = blockerDepth / blockerCount;
    float separation = max(compareDepth - averageBlocker, 0.0);
    float penumbra = separation
        / max(averageBlocker, 1.0e-4);
    float maxRadius = max(baseRadius, shadow.pcssParams.y);
    return clamp(max(baseRadius, penumbra * shadow.pcssParams.x),
        baseRadius, maxRadius);
}

bool ProjectShadowCoord(vec4 shadowCoord, out vec2 localUv,
        out float depth) {
    float w = shadowCoord.w;
    // !(w > eps) also rejects NaN.
    if (!(w > 1.0e-5) || w > 65536.0) {
        gShadowDebugState = max(gShadowDebugState, 1.0);
        localUv = vec2(0.0);
        depth = 0.0;
        return false;
    }

    vec2 projectedXY = shadowCoord.xy / w;
    float projectedDepth = shadowCoord.z;
    vec3 projected = vec3(projectedXY, projectedDepth);
    if (any(isnan(projected)) || any(isinf(projected))
            || any(greaterThan(abs(projected), vec3(65536.0)))) {
        gShadowDebugState = max(gShadowDebugState, 2.0);
        localUv = vec2(0.0);
        depth = 0.0;
        return false;
    }
    localUv = projectedXY * 0.5 + 0.5;
    depth = projectedDepth;
    return true;
}

float SampleShadowCascade(vec4 shadowCoord, vec4 atlasRect,
        int cascadeIndex) {
    vec2 localUv;
    float depth;
    if (!ProjectShadowCoord(shadowCoord, localUv, depth)) {
        return 1.0;
    }
    if (localUv.x <= 0.0 || localUv.x >= 1.0
            || localUv.y <= 0.0 || localUv.y >= 1.0) {
        return 1.0;
    }
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }

    vec2 uv = mix(atlasRect.xy, atlasRect.zw, localUv);
    // The composed rect's v span is inverted (Vulkan y-down atlas rows).
    // Keep Poisson offsets in tile-local orientation while clamping with
    // component-wise rect bounds.
    vec2 rectMin = min(atlasRect.xy, atlasRect.zw);
    vec2 rectMax = max(atlasRect.xy, atlasRect.zw);
    vec2 texelStep = shadow.texelSize.xy
        * sign(atlasRect.zw - atlasRect.xy);
    float guardRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.5 : max(0.5, shadow.pcssParams.z + 0.75);
    vec2 guardBand = abs(shadow.texelSize.xy) * guardRadius;
    vec2 clampMin = rectMin + guardBand;
    vec2 clampMax = rectMax - guardBand;
    clampMin = min(clampMin, clampMax);
    uv = clamp(uv, clampMin, clampMax);
    // Blocker search and PCF use the same stable kernel orientation. Compute
    // the hash/trigonometry once so PCSS has one bounded rotation per receiver.
    // localUv is the same bit for bit in every placement of the tile, and the
    // tile's edge in texels is exact, so the rotation is too. The clamp is
    // the guard band's, in texels.
    vec2 tileTexels = floor(abs(atlasRect.zw - atlasRect.xy)
        / max(abs(shadow.texelSize.xy), vec2(1.0e-6)) + vec2(0.5));
    vec2 tileTexel = clamp(localUv * tileTexels, vec2(guardRadius),
        max(tileTexels - vec2(guardRadius), vec2(guardRadius)));
    mat2 rotation = ShadowOffsetRotation(tileTexel, depth);

    float filterRadius = ProjectedPCSSRadius(uv, depth, cascadeIndex,
        clampMin, clampMax, texelStep, rotation);
    if (filterRadius <= 0.0) {
        return SampleShadowCompare(uv, depth, cascadeIndex, 0.0);
    }

    vec2 tap = texelStep * filterRadius;
    // Each tap's bias grows with its distance from the centre, filterRadius * |o|.
    float tapBias = ShadowFootprintBias(cascadeIndex) * filterRadius;
    float result = SampleShadowCompare(uv, depth, cascadeIndex, 0.0);
    if (shadow.filterParams.y <= 1.0) {
        return result;
    }
    // The first four taps of the nine- and thirteen-tap sets all lie to one
    // side of the centre. Alone they pull the shadow 0.4 radius sideways and
    // soften one side of its edge only, so the five-tap kernel uses a ring.
    vec2 o1 = rotation * vec2(-0.326212, -0.405805);
    vec2 o2 = rotation * vec2(-0.840144, -0.073580);
    vec2 o3 = rotation * vec2(-0.695914, 0.457137);
    vec2 o4 = rotation * vec2(-0.203345, 0.620716);
    if (shadow.filterParams.y <= 5.0) {
        o1 = rotation * kShadowFiveTapRingA;
        o2 = rotation * kShadowFiveTapRingB;
        o3 = -o1;
        o4 = -o2;
    }
    result += SampleShadowCompare(clamp(uv + o1 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o1) * tapBias);
    result += SampleShadowCompare(clamp(uv + o2 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o2) * tapBias);
    result += SampleShadowCompare(clamp(uv + o3 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o3) * tapBias);
    result += SampleShadowCompare(clamp(uv + o4 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o4) * tapBias);
    if (shadow.filterParams.y <= 5.0) {
        return result * (1.0 / 5.0);
    }
    vec2 o5 = rotation * vec2(0.962340, -0.194983);
    vec2 o6 = rotation * vec2(0.473434, -0.480026);
    vec2 o7 = rotation * vec2(0.519456, 0.767022);
    vec2 o8 = rotation * vec2(0.185461, -0.893124);
    result += SampleShadowCompare(clamp(uv + o5 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o5) * tapBias);
    result += SampleShadowCompare(clamp(uv + o6 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o6) * tapBias);
    result += SampleShadowCompare(clamp(uv + o7 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o7) * tapBias);
    result += SampleShadowCompare(clamp(uv + o8 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o8) * tapBias);
    if (shadow.filterParams.y <= 9.0) {
        return result * (1.0 / 9.0);
    }
    vec2 o9 = rotation * vec2(0.507431, 0.064425);
    vec2 o10 = rotation * vec2(0.896420, 0.412458);
    vec2 o11 = rotation * vec2(-0.321940, -0.932615);
    vec2 o12 = rotation * vec2(-0.791559, -0.597705);
    result += SampleShadowCompare(clamp(uv + o9 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o9) * tapBias);
    result += SampleShadowCompare(clamp(uv + o10 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o10) * tapBias);
    result += SampleShadowCompare(clamp(uv + o11 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o11) * tapBias);
    result += SampleShadowCompare(clamp(uv + o12 * tap,
        clampMin, clampMax), depth, cascadeIndex,
        length(o12) * tapBias);
    return result * (1.0 / 13.0);
}

int SelectShadowCascade(float viewDepth) {
    int interiorSplitCount = ShadowCascadeCount() - 1;
    if (interiorSplitCount <= 0 || viewDepth < shadow.splitDepths.x) {
        return 0;
    }
    if (interiorSplitCount <= 1 || viewDepth < shadow.splitDepths.y) {
        return 1;
    }
    if (interiorSplitCount <= 2 || viewDepth < shadow.splitDepths.z) {
        return 2;
    }
    return 3;
}

float SampleCascadeByIndex(int cascadeIndex) {
    return SampleShadowCascade(ShadowCoordByIndex(cascadeIndex),
        AtlasRectByIndex(cascadeIndex), cascadeIndex);
}

float SampleShadowFactor() {
    int cascadeIndex = SelectShadowCascade(vViewDepth);
    gShadowCascadeIndex = cascadeIndex;
    gShadowCascadeBlend = 0.0;
    float shadowFactor = SampleCascadeByIndex(cascadeIndex);
    int lastInteriorIndex = ShadowCascadeCount() - 2;
    float cascadeBlend = shadow.biasParams.z;
    if (cascadeIndex > lastInteriorIndex || cascadeBlend <= 0.0) {
        return shadowFactor;
    }

    float previousSplit = cascadeIndex == 0 ? 0.0
        : CascadeComponent(shadow.splitDepths, cascadeIndex - 1);
    float currentSplit = CascadeComponent(shadow.splitDepths, cascadeIndex);
    float blendWidth = max(1.0,
        (currentSplit - previousSplit) * cascadeBlend);
    float blendStart = currentSplit - blendWidth;
    if (vViewDepth <= blendStart) {
        return shadowFactor;
    }

    float blend = clamp((vViewDepth - blendStart) / blendWidth, 0.0, 1.0);
    if (blend <= 0.02) {
        return shadowFactor;
    }
    gShadowCascadeBlend = blend;
    return mix(shadowFactor, SampleCascadeByIndex(cascadeIndex + 1),
        blend);
}

// GL SampleFilteredMoments: the center plus four half-offset taps, all
// clamped inside the tile.
vec4 SampleFilteredMoments(sampler2D momentMap, vec2 uv, vec2 clampMin,
        vec2 clampMax, vec2 tap) {
    vec4 moments = texture(momentMap, uv);
    if (tap.x <= 0.0) {
        return moments;
    }
    moments += texture(momentMap, clamp(uv + vec2(-0.5, -0.5) * tap, clampMin, clampMax));
    moments += texture(momentMap, clamp(uv + vec2(0.5, -0.5) * tap, clampMin, clampMax));
    moments += texture(momentMap, clamp(uv + vec2(-0.5, 0.5) * tap, clampMin, clampMax));
    moments += texture(momentMap, clamp(uv + vec2(0.5, 0.5) * tap, clampMin, clampMax));
    return moments * 0.2;
}

// GL SampleTranslucentShadowCascade. The tile-local lookup, guard band and
// filter footprint are formed in depth-atlas UV exactly as GL forms them in
// its moment target, then mapped into the moment atlas, where the clamp also
// keeps every bilinear footprint inside this light's moment tile.
vec3 SampleTranslucentShadowCascade(vec4 shadowCoord, vec4 atlasRect) {
    vec2 localUv;
    float depth;
    if (!ProjectShadowCoord(shadowCoord, localUv, depth)) {
        return vec3(1.0);
    }
    if (localUv.x <= 0.0 || localUv.x >= 1.0
            || localUv.y <= 0.0 || localUv.y >= 1.0) {
        return vec3(1.0);
    }
    if (depth <= 0.0 || depth >= 1.0) {
        return vec3(1.0);
    }

    vec2 uv = mix(atlasRect.xy, atlasRect.zw, localUv);
    vec2 rectMin = min(atlasRect.xy, atlasRect.zw);
    vec2 rectMax = max(atlasRect.xy, atlasRect.zw);
    float guardRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.5 : max(0.5, shadow.pcssParams.z + 0.75);
    vec2 guardBand = abs(shadow.texelSize.xy) * guardRadius;
    vec2 clampMin = min(rectMin + guardBand, rectMax - guardBand);
    vec2 clampMax = rectMax - guardBand;
    uv = clamp(uv, clampMin, clampMax);

    float scale = shadow.momentAtlas.x;
    vec2 bias = shadow.momentAtlas.yz;
    vec2 halfTexel = vec2(0.5 * shadow.momentAtlas.w);
    vec2 momentMin = max(clampMin * scale + bias, rectMin * scale + bias + halfTexel);
    vec2 momentMax = min(clampMax * scale + bias, rectMax * scale + bias - halfTexel);
    momentMin = min(momentMin, momentMax);
    vec2 momentUv = clamp(uv * scale + bias, momentMin, momentMax);

    float filterRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.0 : shadow.momentParams.w;
    vec2 tap = filterRadius > 0.0
        ? abs(shadow.texelSize.xy) * max(filterRadius, 0.5) * scale : vec2(0.0);
    return vec3(
        ResolveTranslucentShadowMoments(SampleFilteredMoments(shadowMomentMapR,
            momentUv, momentMin, momentMax, tap), depth, shadow.momentParams),
        ResolveTranslucentShadowMoments(SampleFilteredMoments(shadowMomentMapG,
            momentUv, momentMin, momentMax, tap), depth, shadow.momentParams),
        ResolveTranslucentShadowMoments(SampleFilteredMoments(shadowMomentMapB,
            momentUv, momentMin, momentMax, tap), depth, shadow.momentParams));
}

vec3 SampleTranslucentCascadeByIndex(int cascadeIndex) {
    return SampleTranslucentShadowCascade(ShadowCoordByIndex(cascadeIndex),
        AtlasRectByIndex(cascadeIndex));
}

// GL SampleTranslucentShadow: the opaque cascade selection and split-band
// blend, applied to the colored transmittance.
vec3 SampleTranslucentShadow() {
    if (shadow.momentAtlas.x <= 0.0) {
        return vec3(1.0);
    }
    int cascadeIndex = SelectShadowCascade(vViewDepth);
    vec3 transmittance = SampleTranslucentCascadeByIndex(cascadeIndex);
    int lastInteriorIndex = ShadowCascadeCount() - 2;
    float cascadeBlend = shadow.biasParams.z;
    if (cascadeIndex > lastInteriorIndex || cascadeBlend <= 0.0) {
        return transmittance;
    }

    float previousSplit = cascadeIndex == 0 ? 0.0
        : CascadeComponent(shadow.splitDepths, cascadeIndex - 1);
    float currentSplit = CascadeComponent(shadow.splitDepths, cascadeIndex);
    float blendWidth = max(1.0,
        (currentSplit - previousSplit) * cascadeBlend);
    float blendStart = currentSplit - blendWidth;
    if (vViewDepth <= blendStart) {
        return transmittance;
    }
    float blend = clamp((vViewDepth - blendStart) / blendWidth, 0.0, 1.0);
    return mix(transmittance, SampleTranslucentCascadeByIndex(cascadeIndex + 1), blend);
}

vec4 CascadeDebugColor(int cascadeIndex) {
    if (cascadeIndex <= 0) {
        return vec4(1.0, 0.2, 0.2, 1.0);
    }
    if (cascadeIndex == 1) {
        return vec4(0.2, 1.0, 0.2, 1.0);
    }
    if (cascadeIndex == 2) {
        return vec4(0.2, 0.5, 1.0, 1.0);
    }
    return vec4(1.0, 0.85, 0.2, 1.0);
}

vec4 ShadowCoordWDebugOutput(vec4 shadowCoord) {
    if (isnan(shadowCoord.w)) {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }
    float absW = abs(shadowCoord.w);
    float danger = 1.0 - clamp(absW / 0.25, 0.0, 1.0);
    float intensity = 0.3 + 0.7 * clamp(absW / 4.0, 0.0, 1.0);
    vec3 signColor = (shadowCoord.w < 0.0)
        ? vec3(1.0, 0.2, 0.2) : vec3(0.2, 0.6, 1.0);
    vec3 color = mix(signColor, vec3(1.0, 1.0, 0.0), danger);
    return vec4(color * intensity, 1.0);
}

vec4 ShadowCompareDeltaDebugOutput(int cascadeIndex) {
    vec2 localUv;
    float depth;
    if (!ProjectShadowCoord(ShadowCoordByIndex(cascadeIndex), localUv,
            depth)) {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }
    if (localUv.x <= 0.0 || localUv.x >= 1.0
            || localUv.y <= 0.0 || localUv.y >= 1.0
            || depth <= 0.0 || depth >= 1.0) {
        return vec4(1.0, 1.0, 0.0, 1.0);
    }

    vec4 atlasRect = AtlasRectByIndex(cascadeIndex);
    vec2 atlasUv = mix(atlasRect.xy, atlasRect.zw, localUv);
    float storedDepth = RawShadowDepth(atlasUv);
    float delta = depth - ShadowReceiverBias(cascadeIndex) - storedDepth;
    float magnitude = clamp(abs(delta) * 64.0, 0.0, 1.0);
    vec3 litColor = vec3(0.1, 0.35, 1.0);
    vec3 shadowColor = vec3(1.0, 0.16, 0.08);
    vec3 nearColor = vec3(0.0, 1.0, 0.22);
    vec3 signColor = (delta > 0.0) ? shadowColor : litColor;
    return vec4(mix(nearColor, signColor, magnitude), 1.0);
}

// Vulkan admits shadow maps per light rather than per receiver surface, so a
// receiver that reached this shader is always eligible and the reason the CPU
// uploads is 0. The ladder is kept whole so the two backends read alike if a
// per-surface reason ever appears.
vec4 ShadowReceiverDebugOutput() {
    float reason = floor(shadow.debugParams.y + 0.5);
    if (ShadowDebugModeIs(kShadowDebugReceiverEligibility)) {
        if (reason < 0.5) {
            return vec4(0.0, 0.95, 0.18, 1.0);
        }
        if (reason < 1.5) {
            return vec4(0.0, 0.85, 1.0, 1.0);
        }
        return vec4(1.0, 0.18, 0.08, 1.0);
    }

    if (reason < 0.5) {
        return vec4(0.0, 0.85, 0.16, 1.0);
    }
    if (reason < 1.5) {
        return vec4(0.0, 0.82, 1.0, 1.0);
    }
    if (reason < 2.5) {
        return vec4(1.0, 0.08, 0.08, 1.0);
    }
    if (reason < 3.5) {
        return vec4(0.95, 0.12, 1.0, 1.0);
    }
    if (reason < 4.5) {
        return vec4(1.0, 0.86, 0.08, 1.0);
    }
    return vec4(1.0, 0.45, 0.0, 1.0);
}

vec4 ShadowDebugOutput() {
    if (ShadowReceiverDebugMode()) {
        return ShadowReceiverDebugOutput();
    }

    int cascadeIndex = gShadowCascadeIndex;
    vec2 localUv;
    float depth;
    bool validCoord = ProjectShadowCoord(ShadowCoordByIndex(cascadeIndex),
        localUv, depth);

    if (shadow.debugParams.x < kShadowDebugCascadeIndex + 0.5) {
        if (shadow.debugParams.x < kShadowDebugAtlas + 0.5) {
            if (!validCoord) {
                return vec4(1.0, 0.0, 1.0, 1.0);
            }
            vec4 atlasRect = AtlasRectByIndex(cascadeIndex);
            vec2 atlasUv = mix(atlasRect.xy, atlasRect.zw, localUv);
            return vec4(atlasUv, RawShadowDepth(atlasUv), 1.0);
        }
        vec4 cascadeColor = CascadeDebugColor(cascadeIndex);
        if (gShadowCascadeBlend > 0.0) {
            int nextCascadeIndex =
                (cascadeIndex + 1 < ShadowCascadeCount())
                    ? (cascadeIndex + 1) : (ShadowCascadeCount() - 1);
            cascadeColor.rgb = mix(cascadeColor.rgb,
                CascadeDebugColor(nextCascadeIndex).rgb,
                gShadowCascadeBlend);
        }
        return cascadeColor;
    }

    if (shadow.debugParams.x < kShadowDebugProjectedUV + 0.5) {
        if (!validCoord) {
            return vec4(1.0, 0.0, 1.0, 1.0);
        }
        return vec4(localUv, 1.0 - localUv.x, 1.0);
    }

    if (shadow.debugParams.x < kShadowDebugProjectedDepth + 0.5) {
        if (!validCoord) {
            return vec4(1.0, 0.0, 1.0, 1.0);
        }
        return vec4(vec3(depth), 1.0);
    }

    if (shadow.debugParams.x < kShadowDebugProjectedW + 0.5) {
        return ShadowCoordWDebugOutput(ShadowCoordByIndex(cascadeIndex));
    }

    if (ShadowDebugModeIs(kShadowDebugCompareDelta)) {
        return ShadowCompareDeltaDebugOutput(cascadeIndex);
    }

    if (shadow.debugParams.x > kShadowDebugInvalidMask + 0.5
            && shadow.debugParams.x < kShadowDebugBiasHeatmap + 0.5) {
        if (!validCoord) {
            return vec4(1.0, 0.0, 1.0, 1.0);
        }
        float bias = ShadowReceiverBias(cascadeIndex);
        float heat = clamp(bias * 400.0, 0.0, 1.0);
        return vec4(heat, 1.0 - abs(heat - 0.5) * 2.0, 1.0 - heat, 1.0);
    }

    vec3 invalidColor = vec3(0.0);
    if (gShadowDebugState > 1.5) {
        invalidColor = vec3(1.0, 0.0, 1.0);
    } else if (gShadowDebugState > 0.5) {
        invalidColor = vec3(1.0, 0.0, 0.0);
    } else if (!validCoord) {
        invalidColor = vec3(1.0, 0.0, 1.0);
    } else if (localUv.x <= 0.0 || localUv.x >= 1.0
            || localUv.y <= 0.0 || localUv.y >= 1.0
            || depth <= 0.0 || depth >= 1.0) {
        invalidColor = vec3(1.0, 1.0, 0.0);
    }
    return vec4(invalidColor, 1.0);
}

// ---------------------------------------------------------------------------
// Cel banding, from glprogs/material_interaction.fs and the shadow interaction
// programs. inter.celParams is ( bandsEnabled, bandCount, hardSpecular,
// softness ); R_CelQuantizeUnitValue is the CPU copy of the ladder.
// ---------------------------------------------------------------------------

float CelSteps() {
    return max(inter.celParams.y - 1.0, 1.0);
}

// Places a 0..1 value on the band ladder; softness widens every boundary into
// a smoothstep centred where the hard step would land.
float CelLadder(float value) {
    float steps = CelSteps();
    float scaled = value * steps;

    float softness = clamp(inter.celParams.w, 0.0, 1.0);
    if (softness <= 0.0) {
        return floor(scaled + 0.5) / steps;
    }

    float lower = floor(scaled);
    float halfWidth = softness * 0.5;
    float blend = smoothstep(0.5 - halfWidth, 0.5 + halfWidth, scaled - lower);

    return (lower + blend) / steps;
}

// Quantizes a light contribution without shifting its hue: the brightest
// channel picks the band. Black and overbright pass through untouched.
vec3 CelQuantizeLight(vec3 light) {
    if (inter.celParams.x <= 0.5) {
        return light;
    }

    float peak = max(max(light.r, light.g), light.b);
    if (peak <= 0.0 || peak >= 1.0) {
        return light;
    }

    return light * (CelLadder(peak) / peak);
}

// Collapses the specular falloff into flat plateaus on the same ladder.
float CelSpecularTerm(float term) {
    if (inter.celParams.x <= 0.5 || inter.celParams.z <= 0.5) {
        return term;
    }

    return CelLadder(clamp(term, 0.0, 1.0));
}

#ifdef VK_HDR_DOMAIN_MRT
layout(location = 1) out vec4 outPBRColor;
#define main HDRMaterialMain
#endif

void main() {
    if (shadow.pcssParams.w > 0.5) {
        gShadowDepthGradients = vec4(
            abs(dFdx(vShadowCoord0.z)) + abs(dFdy(vShadowCoord0.z)),
            abs(dFdx(vShadowCoord1.z)) + abs(dFdy(vShadowCoord1.z)),
            abs(dFdx(vShadowCoord2.z)) + abs(dFdy(vShadowCoord2.z)),
            abs(dFdx(vShadowCoord3.z)) + abs(dFdy(vShadowCoord3.z)));
    }

    if (pc.d.x > 2.5) {
        // Emission is drawn once per surface in the ambient walk. An ordered
        // transparent draw still owes the frame its coverage, so it composites
        // black through the authored alpha instead of contributing nothing.
        outColor = vec4(0.0, 0.0, 0.0, PBRTransparentAlpha(vDiffuseTexCoord));
        return;
    }
    if (pc.d.x > 1.5) {
        outColor = vec4(0.0, 1.0, 0.0, PBRTransparentAlpha(vDiffuseTexCoord));
        return;
    }

    vec2 bumpTexCoord = vBumpTexCoord;
    vec2 diffuseTexCoord = vDiffuseTexCoord;
    vec2 specularTexCoord = vSpecularTexCoord;
    if (pc.c.z > 0.5) {
        float height = texture(bumpMap, bumpTexCoord).r;
        vec2 offset = SafeNormalize(vViewVector).xy * (height * pc.c.x + pc.c.y);
        bumpTexCoord += offset;
        diffuseTexCoord += offset;
        specularTexCoord += offset;
    }

    vec4 bumpSample = texture(bumpMap, bumpTexCoord);
    if (pc.d.x > 0.5) {
        vec3 localNormal = PBRDirectNormal(bumpTexCoord);
        vec3 packed = EvaluatePBRDirect(localNormal,
            diffuseTexCoord, specularTexCoord, SampleShadowFactor());
        // Sample first: the views describe the cascade the lit path chose.
        if (ShadowVisualDebugMode()) {
            outColor = ShadowDebugOutput();
            return;
        }
        // Direct radiance is linear in the light color, so the colored
        // translucent transmittance can filter the evaluated result.
        packed *= SampleTranslucentShadow();
        outColor = vec4(packed, PBRTransparentAlpha(diffuseTexCoord));
        return;
    }
    vec3 localNormal = EnhancedMaterialActive() ? EnhancedLocalNormal(bumpSample)
        : vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;

    vec3 lightDir = (pc.a.z > 0.5) ? pc.b.xyz : SafeNormalize(vLightVector);
    float ndotl = max(dot(lightDir, localNormal), 0.0);

    vec3 light = vec3(ndotl);
    light *= textureProj(lightFalloffMap, vLightFalloffTexCoord).rgb;
    light *= textureProj(lightProjectionMap, vLightProjectionTexCoord).rgb;
    // A fragment the light does not reach (outside the projected image, past
    // the falloff, turned away) has no shadow to resolve, and the filter is
    // most of this shader's cost. The debug views still show every fragment.
    bool lit = max(light.r, max(light.g, light.b)) > 0.0 || ShadowVisualDebugMode();
    if (lit) {
        light *= SampleShadowFactor();
    }
    if (ShadowVisualDebugMode()) {
        outColor = ShadowDebugOutput();
        return;
    }
    if (lit) {
        light *= SampleTranslucentShadow();
    }

    vec3 diffuse = texture(diffuseMap, diffuseTexCoord).rgb * inter.diffuseColor.rgb;
    diffuse = ApplyFlatDiffuseSweep(diffuse, vLightFalloffTexCoord.z);

    vec3 halfAngle = SafeNormalize(vHalfAngleVector);
    vec3 specularSample = texture(specularMap, specularTexCoord).rgb;
    float specularTerm;
    if (EnhancedMaterialActive()) {
        // shadow_interaction.fs InteractionSpecular: no legacy doubling.
        specularTerm = CelSpecularTerm(EnhancedSpecularTerm(halfAngle, SafeNormalize(vViewVector), localNormal, specularSample));
    } else {
        float specularDot = clamp(dot(halfAngle, localNormal), 0.0, 1.0);
        specularTerm = texture(specularTableMap, vec2(specularDot, 0.5)).r * 2.0;
        specularTerm = CelSpecularTerm(specularTerm * 0.5) * 2.0;
    }
    vec3 specular = specularSample * inter.specularColor.rgb * specularTerm;

    light = CelQuantizeLight(light);
    outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);
}

#ifdef VK_HDR_DOMAIN_MRT
#undef main
void main() {
    HDRMaterialMain();
    // Both attachments use the same blend state. Preserve source alpha in
    // both outputs for coverage/blending; the primary stores scene alpha.
    bool nativePBR = pc.d.x > 0.5;
    outPBRColor = vec4(nativePBR ? outColor.rgb : vec3(0.0), outColor.a);
    if (nativePBR) outColor.rgb = vec3(0.0);
}
#endif
