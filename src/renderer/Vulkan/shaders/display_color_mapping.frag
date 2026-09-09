#version 450

// Final SDR display mapping, matching RB_ApplyColorMappingsToBackBuffer.
// The swapchain and copy are UNORM: no implicit sRGB decode/encode is wanted.
layout(set = 0, binding = 0) uniform sampler2D scene;
layout(location = 0) in vec2 fragUV;
layout(push_constant) uniform DisplayMapping {
    vec4 settings; // brightness, gamma, reserved, reserved
} pc;
layout(location = 0) out vec4 outColor;

vec4 ApplyDisplayColorMapping(vec4 sampleColor, vec2 mapping) {
    vec3 color = clamp(sampleColor.rgb * mapping.x, 0.0, 1.0);
    float safeGamma = max(mapping.y, 0.001);
    color = pow(color, vec3(1.0 / safeGamma));
    return vec4(color, sampleColor.a);
}

void main() {
    // CopyRender preserves the engine's bottom-up capture convention. Undo
    // that flip once, fetching one texel per output pixel without filtering.
    ivec2 pixel = ivec2(fragUV * vec2(textureSize(scene, 0)));
    pixel.y = textureSize(scene, 0).y - 1 - pixel.y;
    outColor = ApplyDisplayColorMapping(texelFetch(scene, pixel, 0), pc.settings.xy);
}
