// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesHeatHazeMaskShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 heatHaze -- fragment stage, the masked variants (D7c). Ported from
// Vulkan/shaders/heathaze_mask.frag and heathaze_mask_vertex.frag
// (glprogs/heatHazeWithMask.vfp).
//
// Covers three of the four authored variants:
//
//   heatHazeWithMask            uParms[7].z = 0
//   heatHazeWithMaskAndVertex   uParms[7].z = 1, which fades the mask by the
//                               primary vertex colour before the kill test --
//                               the only difference in the retail program
//   heatHazeGrayWithMask        no such program exists in the shipped
//                               archives; the material reference is dangling,
//                               and the ordinary mask contract is the nearest
//                               stock-defined behaviour. Vulkan resolves it the
//                               same way (vk_GuiExecutor.cpp), so the two
//                               backends stay comparable.
//
// The KIL is the retail program's, reproduced as a discard: a mask whose red
// or green is below 0.01 kills the fragment outright rather than distorting by
// almost nothing, which is what keeps the effect confined to the authored
// blob instead of veiling the whole surface.

uniform sampler2D uTexture0;	// _currentRender
uniform sampler2D uTexture1;	// the deform normal map
uniform sampler2D uTexture2;	// the mask

// uParms[5] = env[0], the viewport-to-_currentRender scale
// uParms[6].xy = env[1], 1/viewport size
// uParms[7].xy = the viewport origin in window coordinates
// uParms[7].z  = 1 for the AndVertex variant, 0 otherwise
uniform vec4 uParms[8];

#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
uniform int uAlphaTestFunc;
#endif

in vec2 vMaskTexCoord;
in vec2 vScrollTexCoord;
in vec4 vDeformScale;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 mask = texture(uTexture2, vMaskTexCoord);
    mask.xy *= mix(vec2(1.0), vColor.xy, uParms[7].z);
    mask.xy -= 0.01;
    if (mask.x < 0.0 || mask.y < 0.0) {
        discard;
    }

    vec4 localNormal = texture(uTexture1, vScrollTexCoord);
    localNormal.x = localNormal.a;
    localNormal = localNormal * 2.0 - 1.0;
    localNormal *= mask;

    vec2 window = gl_FragCoord.xy - uParms[7].xy;
    vec4 screenTexCoord = vec4(window * uParms[6].xy, 0.0, 1.0);
    screenTexCoord = clamp(localNormal * vDeformScale + screenTexCoord, 0.0, 1.0);
    screenTexCoord *= uParms[5];

    vec4 color = vec4(texture(uTexture0, screenTexCoord.xy).rgb, 1.0);

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
