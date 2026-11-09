#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../PBRMath.h"
#include "point_shadow_math.glsl"

// openQ4 Vulkan point-shadow-receiving interaction â€” fragment stage (Phase
// F2b).
//
// interaction.frag plus the point-light cube shadow sample of the GL
// shadow_point_interaction.fs contract. Set 7 exposes both LEQUAL comparison
// and unfiltered raw-depth cube samplers, selected by
// r_shadowMapPointDepthCompare at runtime. The stock fixed/stable-rotated
// tangent-disc kernel uses exact 1/5/9/13 Poisson tap tiers. The compare value
// is the normalized radial distance from the light â€” exactly what the
// caster's fragment stage wrote into native depth. Out-of-envelope receivers
// stay lit (factor 1.0). Vulkan cube sampling is always seamless (parity with
// the GL path's GL_TEXTURE_CUBE_MAP_SEAMLESS enable).

layout(set = 0, binding = 0) uniform sampler2D specularTableMap;
layout(set = 1, binding = 0) uniform sampler2D bumpMap;
layout(set = 2, binding = 0) uniform sampler2D lightFalloffMap;
layout(set = 3, binding = 0) uniform sampler2D lightProjectionMap;
layout(set = 4, binding = 0) uniform sampler2D diffuseMap;
layout(set = 5, binding = 0) uniform sampler2D specularMap;
layout(set = 7, binding = 0) uniform samplerCubeShadow shadowCompareMap;
layout(set = 7, binding = 2) uniform samplerCube shadowRawMap;
// r_shadowMapTranslucentMoments: the moment atlas, one map per color channel;
// a point light's six faces are a 3x2 block of face tiles in it
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
    vec4 modelRow0;      // model -> world matrix rows
    vec4 modelRow1;
    vec4 modelRow2;
    vec4 lightOriginFar; // xyz: world-space light origin, w: far envelope
    vec4 biasParams;     // x: constant bias, y: normal bias, z: texel depth bias, w: per-distance normal-offset factor
    vec4 filterParams;   // x: radius, y: taps, z: mode, w: cube texel scale
    vec4 samplingParams; // x: hardware compare enabled
    vec4 debugParams;    // x: r_shadowMapDebugMode, y: receiver fallback reason
    vec4 momentRect;     // xy: face-block origin UV, z: face tile UV size (<= 0: no moments), w: 1 / moment atlas size
    vec4 momentParams;   // x: density, y: min variance, z: bleed reduction, w: filter radius in texels
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
layout(location = 8) in vec3 vPointShadowVector;
layout(location = 9) in float vShadowLightCos;
layout(location = 10) in vec3 vViewVector;

// Pack the basis into unused components so the projected receiver remains
// within Vulkan's minimum 16 varying locations (0..15).
layout(location = 12, component = 1) in vec3 vPBRTangent0;
layout(location = 14, component = 1) in vec3 vPBRTangent1;
layout(location = 15) in vec3 vPBRNormal;

layout(location = 0) out vec4 outColor;

// The point-cube subset of shadowMapDebugMode_t (tr_local.h), matching
// glprogs/shadow_point_interaction.fs: a cube has no atlas rect, cascade
// ladder, or projected w to visualize.
const float kShadowDebugProjectedDepth = 4.0;
const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;
const float kShadowDebugCompareDelta = 12.0;
const float kShadowDebugReceiverEligibility = 13.0;
const float kShadowDebugReceiverFallbackReason = 14.0;

bool ShadowDebugModeIs(float mode) {
    return abs(shadow.debugParams.x - mode) < 0.5;
}

bool ShadowReceiverDebugMode() {
    return ShadowDebugModeIs(kShadowDebugReceiverEligibility)
        || ShadowDebugModeIs(kShadowDebugReceiverFallbackReason);
}

bool ShadowVisualDebugMode() {
    return ShadowDebugModeIs(1.0)
        || ShadowDebugModeIs(kShadowDebugProjectedDepth)
        || ShadowDebugModeIs(kShadowDebugCompareDelta)
        || ShadowReceiverDebugMode();
}

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

// Two of the four ring taps of the five-tap kernel; the others mirror them.
const vec2 kShadowFiveTapRingA = vec2(0.670820, 0.223607);
const vec2 kShadowFiveTapRingB = vec2(-0.223607, 0.670820);

float StableShadowHash(vec3 value) {
    return fract(sin(dot(value, vec3(12.9898, 78.233, 37.719)))
        * 43758.5453);
}

mat2 ShadowOffsetRotation(vec3 direction) {
    if (shadow.filterParams.z < 0.5) {
        return mat2(1.0, 0.0, 0.0, 1.0);
    }
    float angle = StableShadowHash(floor(direction * 37.0)) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, s, -s, c);
}

float ShadowReceiverBias() {
    if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
        return 0.0;
    }
    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    float normalBias = ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)
        ? 0.0 : shadow.biasParams.y;
    float scalarBias = shadow.biasParams.x + normalBias * sinTheta;
    float texelBias = shadow.biasParams.z * (1.0 + slopeBias);
    return max(max(scalarBias, 0.0), max(texelBias, 0.0));
}

float PointShadowCompareDepth(vec3 direction, float compareDepth) {
    if (shadow.samplingParams.x > 0.5) {
        return texture(shadowCompareMap, vec4(direction, compareDepth));
    }
    float storedDepth = texture(shadowRawMap, direction).r;
    return compareDepth <= storedDepth ? 1.0 : 0.0;
}

// The plane is evaluated before divergent control flow. Smooth vertex normals
// describe lighting, not the triangle rasterized into the shadow cube.
vec4 gPointReceiverPlane = vec4(0.0);

void PointShadowCubeAxes(vec3 direction, out vec3 face, out vec3 axisS, out vec3 axisT) {
    vec3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        float side = direction.x >= 0.0 ? 1.0 : -1.0;
        face = vec3(side, 0.0, 0.0);
        axisS = vec3(0.0, 0.0, -side);
        axisT = vec3(0.0, -1.0, 0.0);
    } else if (a.y >= a.z) {
        float side = direction.y >= 0.0 ? 1.0 : -1.0;
        face = vec3(0.0, side, 0.0);
        axisS = vec3(1.0, 0.0, 0.0);
        axisT = vec3(0.0, 0.0, side);
    } else {
        float side = direction.z >= 0.0 ? 1.0 : -1.0;
        face = vec3(0.0, 0.0, side);
        axisS = vec3(side, 0.0, 0.0);
        axisT = vec3(0.0, -1.0, 0.0);
    }
}

vec3 PointShadowTexelDirection(vec3 direction, float texelScale) {
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);
    // Re-select the face for taps across a cube seam, then snap to its actual
    // texel center. A hardware comparison there is a single depth comparison.
    st = (floor((st + 1.0) / texelScale) + 0.5) * texelScale - 1.0;
    return normalize(face + axisS * st.x + axisT * st.y);
}

float PointShadowPlaneDepth(vec3 direction, float depth) {
    float denominator = dot(gPointReceiverPlane.xyz, direction);
    float distance = gPointReceiverPlane.w / (abs(denominator) > 1.0e-5 ? denominator : 1.0);
    // Near the plane horizon a neighbouring ray may not reach the receiver.
    // Keep the ordinary comparison for that ill-conditioned extrapolation.
    return abs(denominator) > 1.0e-5 && distance > 0.0 && distance < 1.0
        ? distance : depth;
}

float PointShadowPlaneCompare(vec3 direction, float depth, float bias, float texelScale) {
    vec3 sampleDirection = PointShadowTexelDirection(direction, texelScale);
    float sampleDepth = PointShadowPlaneDepth(sampleDirection, depth);
    return PointShadowCompareDepth(sampleDirection, sampleDepth - bias);
}

float PointShadowFilteredCompare(vec3 direction, float depth, float bias, float texelScale, bool hardwareCompare) {
    if (texelScale <= 0.0 || dot(gPointReceiverPlane.xyz, gPointReceiverPlane.xyz) < 0.5
        || ShadowDebugModeIs(kShadowDebugBiasOff)
        || ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)) {
        return PointShadowCompareDepth(direction, depth - bias);
    }
    // Keep the inexpensive native lookup when the existing bias already
    // covers this tap's plane displacement and entire texel footprint. The
    // extra comparisons are needed only for under-resolved sloped receivers.
    float planeCos = abs(dot(gPointReceiverPlane.xyz, direction));
    float planeSlope = sqrt(max(1.0 - planeCos * planeCos, 0.0)) / max(planeCos, 1.0e-5);
    float planeDepth = PointShadowPlaneDepth(direction, depth);
    float footprint = 2.0 * planeDepth * texelScale * (planeSlope + texelScale);
    if (abs(planeDepth - depth) + footprint < bias * 0.5) {
        return PointShadowCompareDepth(direction, depth - bias);
    }
    if (!hardwareCompare) {
        return PointShadowPlaneCompare(direction, depth, bias, texelScale);
    }
    // One bilinear lookup against this tap's own point on the receiver plane
    // still holds while the bias covers the depth the plane spans across the
    // four texels it reads, a texel diagonal at most. That is most lit
    // surface; only steep or distant receivers need each texel compared
    // against its own ray, at four lookups and four face selections a tap.
    if (planeDepth * texelScale * (planeSlope + texelScale) * 1.4142136 < bias) {
        return PointShadowCompareDepth(direction, planeDepth - bias);
    }
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);
    vec2 grid = (st + 1.0) / texelScale - 0.5;
    vec2 weight = fract(grid);
    vec2 lower = (floor(grid) + 0.5) * texelScale - 1.0;
    vec3 corner = face + axisS * lower.x + axisT * lower.y;
    // Hardware PCF compares all four depths with one radial reference. On a
    // distant sloped receiver those depths can differ by many world units,
    // exceeding the contact-preserving bias cap. Compare each texel against
    // the ray/receiver-plane intersection and retain the bilinear weights.
    float a = PointShadowPlaneCompare(corner, depth, bias, texelScale);
    float b = PointShadowPlaneCompare(corner + axisS * texelScale, depth, bias, texelScale);
    float c = PointShadowPlaneCompare(corner + axisT * texelScale, depth, bias, texelScale);
    float d = PointShadowPlaneCompare(corner + (axisS + axisT) * texelScale, depth, bias, texelScale);
    return mix(mix(a, b, weight.x), mix(c, d, weight.x), weight.y);
}

float SamplePointShadowCompare(vec3 direction, float depth) {
    return PointShadowFilteredCompare(direction, depth, ShadowReceiverBias(), shadow.filterParams.w, shadow.samplingParams.x > 0.5);
}

float RawPointShadowDepth(vec3 direction) {
    return texture(shadowRawMap, direction).r;
}

// The default kernel: nine bilinear comparisons on a grid one texel apart.
// Together they read a four-by-four block of texels, weighted
// (1-f, 1, 1, f) along each axis. Where the bias covers the depth the
// receiver plane spans across that block, the nine hardware lookups are the
// kernel. Elsewhere each of the sixteen texels is compared once, against its
// own ray's point on the receiver plane, and given the same weight; taken tap
// by tap that cost four face selections and four lookups for each of the
// nine taps, most of them repeats of a texel already read.
// (GL shadow_point_interaction.fs SamplePointShadowGrid.)
float SamplePointShadowGrid(vec3 direction, float depth) {
    float bias = ShadowReceiverBias();
    float texelScale = shadow.filterParams.w;
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);

    bool perTexel = dot(gPointReceiverPlane.xyz, gPointReceiverPlane.xyz) >= 0.5
        && !ShadowDebugModeIs(kShadowDebugBiasOff)
        && !ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff);
    if (perTexel) {
        float planeCos = abs(dot(gPointReceiverPlane.xyz, direction));
        float planeSlope = sqrt(max(1.0 - planeCos * planeCos, 0.0)) / max(planeCos, 1.0e-5);
        // the block reaches two texels from the receiver in each direction
        perTexel = 4.0 * depth * texelScale * (planeSlope + texelScale) >= bias * 0.5;
    }
    if (!perTexel) {
        float lit = 0.0;
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                vec2 tapSt = st + vec2(float(i), float(j)) * texelScale;
                lit += PointShadowCompareDepth(face + axisS * tapSt.x + axisT * tapSt.y, depth - bias);
            }
        }
        return lit * (1.0 / 9.0);
    }

    vec2 grid = (st + 1.0) / texelScale - 0.5;
    vec2 weight = fract(grid);
    vec2 lower = (floor(grid) + 0.5) * texelScale - 1.0;
    float lit = 0.0;
    for (int j = 0; j < 4; j++) {
        float t = lower.y + (float(j) - 1.0) * texelScale;
        float weightT = (j == 0) ? 1.0 - weight.y : ((j == 3) ? weight.y : 1.0);
        for (int i = 0; i < 4; i++) {
            float s = lower.x + (float(i) - 1.0) * texelScale;
            float weightS = (i == 0) ? 1.0 - weight.x : ((i == 3) ? weight.x : 1.0);
            vec3 texel = face + axisS * s + axisT * t;
            float texelLit;
            if (max(abs(s), abs(t)) > 1.0) {
                // past the edge of this face the neighbouring face has its own grid
                texelLit = PointShadowPlaneCompare(texel, depth, bias, texelScale);
            } else {
                float denominator = dot(gPointReceiverPlane.xyz, texel);
                float planeDepth = gPointReceiverPlane.w * length(texel)
                    / (abs(denominator) > 1.0e-5 ? denominator : 1.0);
                float texelDepth = (abs(denominator) > 1.0e-5 && planeDepth > 0.0 && planeDepth < 1.0)
                    ? planeDepth : depth;
                texelLit = PointShadowCompareDepth(texel, texelDepth - bias);
            }
            lit += texelLit * weightS * weightT;
        }
    }
    return lit * (1.0 / 9.0);
}

// The moment-atlas texel a cube lookup would reach. Face selection and face
// coordinates follow the cube-map rules PointShadowCubeAxes encodes, and the
// face tiles were drawn with the depth cube's own face transforms, so the
// tile texel matches the cube texel. Each tap stays half a texel inside its
// face tile, so filtering never reads a neighbouring face.
vec2 PointMomentUv(vec3 direction) {
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);
    float faceIndex = face.x != 0.0 ? (face.x > 0.0 ? 0.0 : 1.0)
        : face.y != 0.0 ? (face.y > 0.0 ? 2.0 : 3.0)
        : (face.z > 0.0 ? 4.0 : 5.0);
    vec2 cell = vec2(mod(faceIndex, 3.0), floor(faceIndex / 3.0));
    float size = shadow.momentRect.z;
    float halfTexel = 0.5 * shadow.momentRect.w;
    vec2 faceUv = clamp((st * 0.5 + 0.5) * size, vec2(halfTexel), vec2(size - halfTexel));
    return shadow.momentRect.xy + cell * size + faceUv;
}

// GL SamplePointTranslucentShadow with SampleFilteredPointMoments: the center
// plus four half-offset taps around the receiver direction.
vec3 SamplePointTranslucentShadow() {
    float far = shadow.lightOriginFar.w;
    if (shadow.momentRect.z <= 0.0 || !(far > 0.0)) {
        return vec3(1.0);
    }
    float depth = PointShadowRadialDepth(vPointShadowVector, far);
    if (depth <= 0.0 || depth >= 1.0) {
        return vec3(1.0);
    }

    vec3 direction = SafeNormalize(vPointShadowVector);
    vec2 uv = PointMomentUv(direction);
    vec4 momentsR = texture(shadowMomentMapR, uv);
    vec4 momentsG = texture(shadowMomentMapG, uv);
    vec4 momentsB = texture(shadowMomentMapB, uv);
    float filterRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.0 : shadow.momentParams.w;
    float texelScale = shadow.filterParams.w;
    if (filterRadius > 0.0 && texelScale > 0.0) {
        vec3 up = abs(direction.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 tangent = SafeNormalize(cross(up, direction));
        vec3 bitangent = cross(direction, tangent);
        float tap = texelScale * max(filterRadius, 0.5);
        for (int i = 0; i < 4; i++) {
            vec2 offset = vec2((i & 1) != 0 ? 0.5 : -0.5, (i & 2) != 0 ? 0.5 : -0.5);
            vec2 tapUv = PointMomentUv(SafeNormalize(direction
                + (tangent * offset.x + bitangent * offset.y) * tap));
            momentsR += texture(shadowMomentMapR, tapUv);
            momentsG += texture(shadowMomentMapG, tapUv);
            momentsB += texture(shadowMomentMapB, tapUv);
        }
        momentsR *= 0.2;
        momentsG *= 0.2;
        momentsB *= 0.2;
    }
    return vec3(
        ResolveTranslucentShadowMoments(momentsR, depth, shadow.momentParams),
        ResolveTranslucentShadowMoments(momentsG, depth, shadow.momentParams),
        ResolveTranslucentShadowMoments(momentsB, depth, shadow.momentParams));
}

float SampleShadowFactor() {
    float far = shadow.lightOriginFar.w;
    // !(far > 0) also rejects NaN
    if (!(far > 0.0)) {
        return 1.0;
    }

    float depth = PointShadowRadialDepth(vPointShadowVector, far);
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }
    vec3 direction = SafeNormalize(vPointShadowVector);

    float filterRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.0 : shadow.filterParams.x;
    if (filterRadius <= 0.0 || shadow.filterParams.w <= 0.0) {
        return SamplePointShadowCompare(direction, depth);
    }
    // nine fixed taps at a one-texel radius, hardware comparison: the shipped default
    if (shadow.samplingParams.x > 0.5 && shadow.filterParams.z < 0.5
            && shadow.filterParams.y > 5.0 && shadow.filterParams.y <= 9.0
            && abs(filterRadius - 1.0) < 0.01) {
        return SamplePointShadowGrid(direction, depth);
    }

    vec3 up = abs(direction.z) < 0.99
        ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = SafeNormalize(cross(up, direction));
    vec3 bitangent = cross(direction, tangent);
    float tap = shadow.filterParams.w * filterRadius;

    float result = SamplePointShadowCompare(direction, depth);
    if (shadow.filterParams.y <= 1.0) {
        return result;
    }
    mat2 rotation = ShadowOffsetRotation(direction);
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
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o1.x + bitangent * o1.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o2.x + bitangent * o2.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o3.x + bitangent * o3.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o4.x + bitangent * o4.y) * tap), depth);
    if (shadow.filterParams.y <= 5.0) {
        return result * (1.0 / 5.0);
    }
    vec2 o5 = rotation * vec2(0.962340, -0.194983);
    vec2 o6 = rotation * vec2(0.473434, -0.480026);
    vec2 o7 = rotation * vec2(0.519456, 0.767022);
    vec2 o8 = rotation * vec2(0.185461, -0.893124);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o5.x + bitangent * o5.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o6.x + bitangent * o6.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o7.x + bitangent * o7.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o8.x + bitangent * o8.y) * tap), depth);
    if (shadow.filterParams.y <= 9.0) {
        return result * (1.0 / 9.0);
    }
    vec2 o9 = rotation * vec2(0.507431, 0.064425);
    vec2 o10 = rotation * vec2(0.896420, 0.412458);
    vec2 o11 = rotation * vec2(-0.321940, -0.932615);
    vec2 o12 = rotation * vec2(-0.791559, -0.597705);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o9.x + bitangent * o9.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o10.x + bitangent * o10.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o11.x + bitangent * o11.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o12.x + bitangent * o12.y) * tap), depth);
    return result * (1.0 / 13.0);
}

// Vulkan admits shadow maps per light rather than per receiver surface, so a
// receiver that reached this shader is always eligible and the reason the CPU
// uploads is 0. The ladder is kept whole so the two backends read alike if a
// per-surface reason ever appears.
vec4 PointReceiverDebugOutput() {
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

vec4 PointShadowDebugOutput() {
    if (ShadowReceiverDebugMode()) {
        return PointReceiverDebugOutput();
    }
    float far = shadow.lightOriginFar.w;
    if (!(far > 0.0)) {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }

    float depth = PointShadowRadialDepth(vPointShadowVector, far);
    if (depth <= 0.0 || depth >= 1.0) {
        return vec4(1.0, 1.0, 0.0, 1.0);
    }
    vec3 direction = SafeNormalize(vPointShadowVector);
    float storedDepth = RawPointShadowDepth(direction);

    if (ShadowDebugModeIs(kShadowDebugCompareDelta)) {
        float delta = depth - ShadowReceiverBias() - storedDepth;
        float magnitude = clamp(abs(delta) * 64.0, 0.0, 1.0);
        vec3 litColor = vec3(0.1, 0.35, 1.0);
        vec3 shadowColor = vec3(1.0, 0.16, 0.08);
        vec3 nearColor = vec3(0.0, 1.0, 0.22);
        vec3 signColor = (delta > 0.0) ? shadowColor : litColor;
        return vec4(mix(nearColor, signColor, magnitude), 1.0);
    }

    if (ShadowDebugModeIs(kShadowDebugProjectedDepth)) {
        return vec4(vec3(depth), 1.0);
    }

    // mode 1: the cube lookup direction with its stored radial depth
    return vec4(direction * 0.5 + 0.5, clamp(storedDepth, 0.0, 1.0));
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
    vec3 receiverNormal = cross(dFdx(vPointShadowVector), dFdy(vPointShadowVector));
    receiverNormal *= inversesqrt(max(dot(receiverNormal, receiverNormal), 1.0e-20));
    gPointReceiverPlane = vec4(receiverNormal, dot(receiverNormal, vPointShadowVector) / max(shadow.lightOriginFar.w, 1.0));
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
        if (ShadowVisualDebugMode()) {
            outColor = PointShadowDebugOutput();
            return;
        }
        // Direct radiance is linear in the light color, so the colored
        // translucent transmittance can filter the evaluated result.
        packed *= SamplePointTranslucentShadow();
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
    // A light's box reaches well past its falloff, and a bumped surface turns
    // away from it: a fragment the light does not reach has no shadow to
    // resolve, and the filter is most of this shader's cost.
    bool lit = max(light.r, max(light.g, light.b)) > 0.0 || ShadowVisualDebugMode();
    if (lit) {
        light *= SampleShadowFactor();
    }
    if (ShadowVisualDebugMode()) {
        outColor = PointShadowDebugOutput();
        return;
    }
    if (lit) {
        light *= SamplePointTranslucentShadow();
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
