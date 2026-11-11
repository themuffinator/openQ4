#version 450
#extension GL_GOOGLE_include_directive : require
#include "framebuffer_coords.glsl"
#include "heathaze_depth_reject.glsl"

// Native Vulkan port of heatHaze.vfp. Quake 4 encodes the normal's X
// component in alpha (RXGB convention), then offsets _currentRender by the
// distance-scaled decoded normal.

layout(set = 0, binding = 0) uniform sampler2D currentRenderMap;
layout(set = 1, binding = 0) uniform sampler2D normalMap;

layout(set = 5, binding = 0) uniform sampler2D sceneDepthMap;

layout(set = 6, binding = 0, std140) uniform StageParms {
    vec4 parms[8];
} sp;

layout(push_constant) uniform StagePushConstants {
    mat4 mvp;
    vec4 stageColor;
    vec4 texMatrixS;
    vec4 texMatrixT;
    vec4 params;
} pc;

layout(location = 1) in vec2 vScrollTexCoord;
layout(location = 2) in vec4 vDeformScale;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 localNormal = texture(normalMap, vScrollTexCoord);
    localNormal.x = localNormal.a;
    localNormal = localNormal * 2.0 - 1.0;

    // gl_FragCoord is top-left-oriented under the Vulkan viewport while the
    // GL program and _currentRender capture use bottom-left window space.
    vec2 windowGL = vec2(
        gl_FragCoord.x - sp.parms[7].x,
        CanonicalWindowY(sp.parms[6].z) - sp.parms[7].y);
    vec4 screenTexCoord = vec4(windowGL * sp.parms[6].xy, 0.0, 1.0);
    vec4 undistortedTexCoord = screenTexCoord;
    screenTexCoord = clamp(
        localNormal * vDeformScale + screenTexCoord,
        0.0,
        1.0);
    // parms[7].zw: 0..1 window space to the _currentDepth capture
    screenTexCoord.xy = HeatHazeRejectNearer(sceneDepthMap, sp.parms[7].zw,
        screenTexCoord.xy, undistortedTexCoord.xy);
    screenTexCoord *= sp.parms[5];

    vec4 color = vec4(texture(currentRenderMap, screenTexCoord.xy).rgb, 1.0);
    if (pc.params.y > 0.5) {
        if (pc.params.y > 1.5) {
            if (abs(color.a - pc.params.z) > (0.5 / 255.0)) {
                discard;
            }
        } else if (color.a <= pc.params.z) {
            discard;
        }
    } else if (pc.params.y < -0.5 && color.a >= pc.params.z) {
        discard;
    }
    outColor = color;
}
