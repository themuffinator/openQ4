// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesBumpyEnvironmentShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// gles_d3 TG_REFLECT_CUBE with a bump stage -- vertex stage (D7a).
//
// bumpyEnvironment.vfp semantics, transliterated from
// Vulkan/shaders/bumpy_environment.vert. Unlike the plain environment program
// this one works in GLOBAL space: the cube map is a world-space environment
// and the perturbed normal has to be transformed out of tangent space and then
// out of model space to match it. The stock ARB program does that with
// program.env[6..8], which are the model matrix rows; they arrive here as
// three uniforms.
//
// ARB2 selects this program, rather than environment.vfp, for any
// TG_REFLECT_CUBE stage whose MATERIAL also carries a bump stage
// (draw_common.cpp:4846) -- the choice is a property of the material, not of
// the stage.

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uLocalViewOrigin;
uniform vec4 uModelRow0;
uniform vec4 uModelRow1;
uniform vec4 uModelRow2;

out vec2 vNormalTexCoord;
out vec3 vGlobalToEye;
out vec3 vGlobalTangent;
out vec3 vGlobalBitangent;
out vec3 vGlobalNormal;

vec3 TransformDirectionToGlobal(vec3 direction) {
    return vec3(
        dot(direction, uModelRow0.xyz),
        dot(direction, uModelRow1.xyz),
        dot(direction, uModelRow2.xyz));
}

void main() {
    // the original program deliberately passes the normal-map coordinates
    // through unmodified -- no stage texture matrix is applied
    vNormalTexCoord = inTexCoord;

    vGlobalToEye = TransformDirectionToGlobal(uLocalViewOrigin.xyz - inPosition);
    vGlobalTangent = TransformDirectionToGlobal(inTangent);
    vGlobalBitangent = TransformDirectionToGlobal(inBitangent);
    vGlobalNormal = TransformDirectionToGlobal(inNormal);

    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
