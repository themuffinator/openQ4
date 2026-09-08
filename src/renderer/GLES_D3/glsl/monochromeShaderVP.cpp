// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesMonochromeShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 monochrome.vfp -- vertex stage (D7c). Ported from
// Vulkan/shaders/monochrome.vert.
//
// monochrome.vfp is source-less: no shipped archive contains it, so this is a
// reconstruction from the one material that references it
// (gfx/effects/testmonochrome in smoke.mtr), which supplies only position,
// base ST and fragmentMap 0. Vulkan reconstructs it the same way, so the two
// backends agree on a program neither can verify against an original.

layout(location = 0) in vec3 inPosition;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;

out vec2 vTexCoord;

void main() {
    vTexCoord = inTexCoord;
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
