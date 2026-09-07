#version 450

// openQ4 Vulkan shadow-map caster — fragment stage (Phase F2a).
//
// GL shadow_proj_caster.fs alpha coverage parity:
// - perforated casters alpha-test against the material's alpha map;
// - optional hashed coverage uses screen-space or stable model/world-seeded
//   coordinates according to r_shadowMapStableAlphaHash;
// - casters past the falloff plane clamp to far depth instead of vanishing
//   (better behaved near the falloff boundary); only apex-side geometry
//   (depth <= 0), which cannot occlude anything inside the light volume,
//   is discarded — after the texture fetch so derivatives stay defined;
// - shader-written fragment depth bypasses the fixed-function depth bias, so
//   the classic slope-scale caster offset (r_shadowMapPolygonFactor /
//   r_shadowMapPolygonOffset) is applied here via screen-space derivatives,
//   exactly like the GL caster program.

layout(set = 0, binding = 0) uniform sampler2D alphaMap;

layout(push_constant) uniform CasterPushConstants {
    mat4 mvp;
    vec4 depthRow;   // model-local shadow depth plane (clip plane 2)
    vec4 alphaS;     // alpha-test texture matrix S row; z = slope-scale depth factor
    vec4 alphaT;     // alpha-test texture matrix T row; z = constant depth offset
    vec4 params;     // x: alpha mode, y: alphaRef, z: alphaScale, w: hash mode + stable world phase
} pc;

layout(location = 0) in vec2 vAlphaTexCoord;
layout(location = 1) in float vShadowDepth;
layout(location = 2) in vec3 vAlphaHashCoord;

float AlphaHashThreshold(vec2 fragmentCoord) {
    vec2 texel = floor(fragmentCoord);
    return fract(52.9829189 * fract(
        texel.x * 0.06711056 + texel.y * 0.00583715));
}

float StableAlphaHashThreshold(vec3 hashCoord, float worldPhase) {
    vec3 texel = floor(hashCoord * 0.5);
    return fract(52.9829189 * fract(
        texel.x * 0.06711056
        + texel.y * 0.00583715
        + texel.z * 0.01327111
        + worldPhase));
}

float AlphaCoverage(float alpha) {
    float alphaRef = clamp(pc.params.y, 0.0, 0.9999);
    float scaledAlpha = clamp(alpha, 0.0, 1.0);
    return clamp((scaledAlpha - alphaRef)
        / max(1.0 - alphaRef, 1.0e-4), 0.0, 1.0);
}

void main() {
    // Derivatives need the complete fragment quad, before cutout/depth discard.
    float depthSlope = max(abs(dFdx(vShadowDepth)), abs(dFdy(vShadowDepth)));
    if (pc.params.x != 0.0) {
        float alpha = texture(alphaMap, vAlphaTexCoord).a * pc.params.z;
        if (pc.params.w > 0.5 && pc.params.x > 0.5
                && pc.params.x < 1.5) {
            float coverage = AlphaCoverage(alpha);
            float threshold = pc.params.w > 1.5
                ? StableAlphaHashThreshold(vAlphaHashCoord, fract(pc.params.w))
                : AlphaHashThreshold(gl_FragCoord.xy);
            if (coverage <= 0.0 || coverage <= threshold) {
                discard;
            }
        } else {
            bool testPassed;
            if (pc.params.x < -0.5) {
                testPassed = alpha < pc.params.y;
            } else if (pc.params.x > 1.5) {
                testPassed = abs(alpha - pc.params.y) <= (0.5 / 255.0);
            } else {
                testPassed = alpha > pc.params.y;
            }
            if (!testPassed) {
                discard;
            }
        }
    }

    if (vShadowDepth <= 0.0) {
        discard;
    }

    gl_FragDepth = clamp(vShadowDepth + pc.alphaS.z * depthSlope + pc.alphaT.z, 0.0, 1.0);
}
