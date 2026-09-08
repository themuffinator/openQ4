// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesFogShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 fog light -- fragment stage (D6).
//
// The RB_FogPass fixed-function modulate chain (draw_common.cpp:8070-8135):
// primary colour (the light's stage-0 fog colour, alpha pinned to 1 by
// glColor3fv) x _fog (GL_MODULATE) x _fogEnter (GL_MODULATE).
//
// Both intrinsic images are RGBA with WHITE rgb and the fog fraction in ALPHA
// (Image_intrinsic.cpp), so the product lands as
// vec4(fogColour.rgb, fog.a * enter.a) and the pass's fixed
// SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend turns that alpha into the fog density.
// That is why the colour's own alpha is pinned rather than passed through:
// uColor.a carries the fog DISTANCE on the CPU side, not an opacity.

uniform sampler2D uTexture0;	// _fog
uniform sampler2D uTexture1;	// _fogEnter
uniform vec4 uColor;			// fog colour; rgb only

in vec2 vFogTexCoord;
in vec2 vEnterTexCoord;

out vec4 fragColor;

void main() {
    fragColor = vec4(uColor.rgb, 1.0)
            * texture(uTexture0, vFogTexCoord)
            * texture(uTexture1, vEnterTexCoord);
}
)";
