// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesEnvironmentShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// gles_d3 TG_REFLECT_CUBE without a bump stage -- vertex stage (D7a).
//
// The environment.vfp semantics, transliterated from
// Vulkan/shaders/environment.vert. The reflection is built in MODEL space from
// the model-local view origin, which is where the cube map lives; the GL
// fixed-function fallback instead uses GL_REFLECTION_MAP in eye space and
// undoes the view rotation with a texture matrix (draw_common.cpp:4890), and
// the two are equivalent.
//
// Which of the two ARB2 takes depends on whether the MATERIAL has a bump stage
// (draw_common.cpp:4846): with one it binds bumpyEnvironment, without one it
// binds environment. This program is the second case.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;

uniform mat4 uMVP;
uniform vec4 uLocalViewOrigin;
uniform vec4 uVertexColor;		// (rgbMul, rgbAdd, alphaMul, alphaAdd) -- the SVC packing

out vec3 vNormal;
out vec3 vToEye;
out vec4 vColor;

void main() {
    vNormal = inNormal;
    vToEye = uLocalViewOrigin.xyz - inPosition;

    // The ARB environment fragment program bypasses GL texture unit 1, so both
    // vertexColor modes observe the raw colour array (Vulkan/shaders/
    // environment.vert:33). An SVC_IGNORE packing leaves the stage colour to
    // do the work in the fragment stage.
    vColor = vec4(inColor.rgb * uVertexColor.x + uVertexColor.y,
                  inColor.a   * uVertexColor.z + uVertexColor.w);

    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
