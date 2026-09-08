// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesBlendLightShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 blend light -- fragment stage (D6).
//
// The RB_BlendLight fixed-function result (draw_common.cpp:7910-7975): the
// projected stage texture (S/Q, T/Q) x the falloff (S, 0.5) x the light
// stage's RGBA colour. Unlike a normal light, the ALPHA of the stage colour is
// used too, and the stage's own blend keyword -- not a fixed blend -- puts the
// result into the framebuffer, which the caller programs through GL_State.
//
// textureProj does the /Q divide that GL's GL_Q texgen coordinate stood for.

uniform sampler2D uTexture0;	// the light's projection image
uniform sampler2D uTexture1;	// the light's falloff image
uniform vec4 uColor;			// the light stage colour, alpha included

in vec4 vProjTexCoord;
in vec2 vFalloffTexCoord;

out vec4 fragColor;

void main() {
    fragColor = textureProj(uTexture0, vProjTexCoord)
            * texture(uTexture1, vFalloffTexCoord)
            * uColor;
}
)";
