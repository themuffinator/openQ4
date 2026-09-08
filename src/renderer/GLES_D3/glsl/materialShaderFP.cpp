// Copyright (C) 2026 DarkMatter Productions
//
// One authored material stage. Replaces the fixed-function TexEnv COMBINE
// configuration the legacy path spends ~40 glTexEnv calls building per stage
// (draw_common.cpp:7335-7378).
//
// The vertex-colour term is packed as (rgbModulate, rgbAdd, alphaModulate,
// alphaAdd) and applied as `vColor * modulate + add`, with RGB and alpha taken
// separately -- the packing the Vulkan interaction shader uses and the stock
// interaction.vfp implies:
//
//     SVC_MODULATE          (1, 0, 1, 0)   ->  rgb = vColor.rgb   a = vColor.a
//     SVC_INVERSE_MODULATE  (-1, 1, 1, 0)  ->  rgb = 1-vColor.rgb a = vColor.a
//     SVC_IGNORE            (0, 1, 0, 1)   ->  rgb = 1            a = 1
//
// Alpha is deliberately NOT inverted for SVC_INVERSE_MODULATE. The legacy path
// configures only the RGB combiner (draw_common.cpp:7338-7352) and leaves the
// alpha combiner at the GL_COMBINE default, which modulates -- so alpha comes
// out `texA * vertA`. Inverting it too zeroed the alpha mask that every
// `*_to_smolder` material writes for its second stage to blend through.
//
// Keeping one packing across both shaders is what lets an interaction bug be
// diffed against the material path instead of re-derived.

#include "glsl_shaders.h"

const char * const glesMaterialShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

uniform sampler2D uTexture0;
uniform vec4 uColor;
uniform vec4 uVertexColor;
#ifdef GLESD3_ALPHATEST
uniform float uAlphaTest;
uniform int uAlphaTestFunc;
#endif

in vec4 vColor;
in vec2 vTexCoord;

out vec4 outColor;

void main() {
    vec4 texel = texture(uTexture0, vTexCoord);
    vec4 vertexTerm = vec4(vColor.rgb * uVertexColor.x + uVertexColor.y,
                           vColor.a   * uVertexColor.z + uVertexColor.w);
    vec4 result = texel * uColor * vertexTerm;

#ifdef GLESD3_ALPHATEST
    // ES has no fixed-function alpha test; see gles_draw.cpp. Compiled in
    // only for alpha-tested stages: a discard anywhere in a program disables
    // early-Z/LRZ on tile-based GPUs even for draws that never take it, so
    // the base variant must not contain one (gles_program.cpp, D8).
    // Match the fixed-function alpha comparison, including equality boundaries.
    float alpha = clamp(result.a, 0.0, 1.0);
    if ((uAlphaTestFunc == 514 && alpha != uAlphaTest) || // GL_EQUAL
        (uAlphaTestFunc == 513 && alpha >= uAlphaTest) || // GL_LESS
        (uAlphaTestFunc == 518 && alpha < uAlphaTest) ||  // GL_GEQUAL
        (uAlphaTestFunc == 516 && alpha <= uAlphaTest)) { // GL_GREATER
        discard;
    }
#endif

    outColor = result;
}
)";
