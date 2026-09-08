// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesHeatHazeShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 heatHaze -- vertex stage, shared by all four heatHaze variants
// (D7c). Ported from Vulkan/shaders/heathaze.vert, which is itself the port of
// the vertex half of Quake 4's glprogs/heatHaze.vfp and heatHazeWithMask.vfp.
//
// The distance scaling is the odd-looking part and is faithful to the retail
// ARB program: it builds the point (1, 0, eyeZ, 1), pushes it through the
// projection's row 0 and row 3, and divides. That yields how much one unit of
// eye-space X shrinks at this surface's depth, which is what keeps a heat
// plume the same apparent strength whether it is two metres away or twenty.
// The MAX against 1 is the retail guard for polygons crossing the view plane,
// and the MIN against 0.02 is its clamp on how wacky the deformation may get
// up close. Both are reproduced exactly -- they are the difference between a
// shimmer and a smeared screen.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;

// uParms[0] = program.local[0], the normal-map scroll
// uParms[1] = program.local[1], the deform magnitude
// uParms[2] = model-view row 2 (eye-space Z)
// uParms[3] = projection row 0
// uParms[4] = projection row 3
uniform vec4 uParms[8];

out vec2 vMaskTexCoord;
out vec2 vScrollTexCoord;
out vec4 vDeformScale;
out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(inPosition, 1.0);

    vColor = inColor;
    vMaskTexCoord = inTexCoord;
    vScrollTexCoord = inTexCoord + uParms[0].xy;

    vec4 projectionPosition = vec4(
        1.0,
        0.0,
        dot(vec4(inPosition, 1.0), uParms[2]),
        1.0);
    float projectedDistance = dot(projectionPosition, uParms[3]);
    float projectedW = max(dot(projectionPosition, uParms[4]), 1.0);
    float distanceScale = min(projectedDistance / projectedW, 0.02);
    vDeformScale = vec4(distanceScale) * uParms[1];
}
)";
