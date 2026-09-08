// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesHeatHazeShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 heatHaze -- fragment stage, the unmasked variant (D7c). Ported from
// Vulkan/shaders/heathaze.frag / glprogs/heatHaze.vfp.
//
// Two decodes worth naming, both retail behaviour rather than choices:
//
//   localNormal.x = localNormal.a -- Quake 4's RXGB/DXT5 normal maps carry X
//   in alpha. The same swizzle appears in the interaction shader.
//
//   the result forces alpha to 1. The ARB program writes result.color.xyz and
//   leaves w undefined; every heat-haze material blends with a mode that does
//   not read destination alpha, so 1 is the safe reading -- and on this
//   backend it also matters that the stage does not punch a hole in the
//   framebuffer alpha the compositor reads (see r_forceOpaquePresent).
//
// No Y flip, unlike the Vulkan port: _currentRender is filled by
// glCopyTexSubImage2D from the default framebuffer, so its origin already
// agrees with gl_FragCoord's.

uniform sampler2D uTexture0;	// _currentRender
uniform sampler2D uTexture1;	// the deform normal map

// uParms[5] = env[0], the viewport-to-_currentRender scale
// uParms[6].xy = env[1], 1/viewport size
// uParms[7].xy = the viewport origin in window coordinates
uniform vec4 uParms[8];

#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
#endif

in vec2 vMaskTexCoord;
in vec2 vScrollTexCoord;
in vec4 vDeformScale;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 localNormal = texture(uTexture1, vScrollTexCoord);
    localNormal.x = localNormal.a;
    localNormal = localNormal * 2.0 - 1.0;

    vec2 window = gl_FragCoord.xy - uParms[7].xy;
    vec4 screenTexCoord = vec4(window * uParms[6].xy, 0.0, 1.0);
    screenTexCoord = clamp(localNormal * vDeformScale + screenTexCoord, 0.0, 1.0);
    screenTexCoord *= uParms[5];

    vec4 color = vec4(texture(uTexture0, screenTexCoord.xy).rgb, 1.0);

#ifdef GLESD3_ALPHATEST
    if (uAlphaTest >= 0.0 && color.a <= uAlphaTest) {
        discard;
    }
#endif
    fragColor = color;
}
)";
