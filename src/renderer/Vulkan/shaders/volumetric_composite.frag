#version 450

// Shadowed volumetric lighting (REN-09): lays the scattering target over the
// opaque scene before fog and translucent surfaces, scene * transmittance +
// scattering (blend ONE, SRC_ALPHA). A depth-aware 3x3 filter of the target
// upsamples a half-resolution march and takes its dither out. The OpenGL twin
// is rbVolumetricCompositeSource in src/renderer/draw_volumetric.cpp.
//
// fragUV is OpenGL's texture coordinate of the scene (VK_Post_DrawSceneRect);
// the depth capture and the scattering target both store rows bottom-up.

layout(set = 0, binding = 0) uniform sampler2D uDepth;
layout(set = 1, binding = 0) uniform sampler2D uScatter;

layout(std140, set = 6, binding = 0) uniform VolumetricCompositeBlock {
    mat4 clipToWorld;
    vec4 eye;
    vec4 sceneInfo;		// scene width/height, target width/height
    vec4 params0;		// extinction, scattering, anisotropy, max distance
    vec4 params1;		// steps, march offset, debug, divisor
} comp;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

float ViewDistance(vec2 uv) {
    float d = textureLod(uDepth, uv, 0.0).r;
    if (d >= 0.99999) {
        return comp.params0.w;
    }
    vec4 world = comp.clipToWorld * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return length(world.xyz / world.w - comp.eye.xyz);
}

void main() {
    vec2 pixel = fragUV * comp.sceneInfo.xy;
    vec2 sceneUV = clamp(fragUV, vec2(0.0), vec2(1.0));
    float center = ViewDistance(sceneUV);
    float divisor = comp.params1.w;
    vec2 targetPos = pixel / divisor;
    vec2 base = floor(targetPos);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 texel = clamp(base + vec2(float(x), float(y)), vec2(0.0), comp.sceneInfo.zw - 1.0);
            vec3 scatter = textureLod(uScatter, (texel + 0.5) / comp.sceneInfo.zw, 0.0).rgb;
            float distance = ViewDistance(clamp((texel + 0.5) * divisor / comp.sceneInfo.xy,
                vec2(0.0), vec2(1.0)));
            vec2 offset = texel + 0.5 - targetPos;
            float spatial = exp(-0.5 * dot(offset, offset));
            float range = exp(-abs(distance - center) / (0.03 * center + 2.0));
            float weight = spatial * range + 1.0e-4 * spatial;
            sum += scatter * weight;
            weightSum += weight;
        }
    }
    vec3 scatter = sum / max(weightSum, 1.0e-5);
    float transmittance = exp(-comp.params0.x * min(center, comp.params0.w));
    outColor = vec4(scatter, comp.params1.z > 0.5 ? 0.0 : transmittance);
}
