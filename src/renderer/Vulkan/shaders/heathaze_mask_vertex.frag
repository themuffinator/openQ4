#version 450
#extension GL_GOOGLE_include_directive : require
#include "framebuffer_coords.glsl"
#include "heathaze_depth_reject.glsl"

// Native Vulkan port of heatHazeWithMaskAndVertex.vfp. It differs from the
// ordinary mask variant only by fading mask.xy with primary vertex color
// before the 0.01 kill threshold.

layout(set = 0, binding = 0) uniform sampler2D currentRenderMap;
layout(set = 1, binding = 0) uniform sampler2D normalMap;
layout(set = 2, binding = 0) uniform sampler2D maskMap;

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

layout(location = 0) in vec2 vMaskTexCoord;
layout(location = 1) in vec2 vScrollTexCoord;
layout(location = 2) in vec4 vDeformScale;
layout(location = 3) in vec4 vVertexColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 mask = texture(maskMap, vMaskTexCoord);
    mask.xy *= vVertexColor.xy;
    mask.xy -= 0.01;
    if (mask.x < 0.0 || mask.y < 0.0) {
        discard;
    }

    vec4 localNormal = texture(normalMap, vScrollTexCoord);
    localNormal.x = localNormal.a;
    localNormal = localNormal * 2.0 - 1.0;
    localNormal *= mask;

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
