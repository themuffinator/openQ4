// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesCubeMapShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// gles_d3 cube-map texgen -- vertex stage (D7a).
//
// Covers TG_SKYBOX_CUBE, TG_WOBBLESKY_CUBE and TG_DIFFUSE_CUBE. All three are
// ordinary material stages in the GL path (RB_STD_T_RenderShaderPasses) that
// differ from TG_EXPLICIT only in where the texture coordinate comes from, so
// this carries the same stage colour, vertex colour and alpha test the
// material program does.
//
// The direction arrives on its own attribute rather than being computed here:
//
//   skybox / wobblesky  R_SkyboxTexGen and R_WobbleskyTexGen (tr_light.cpp:517,
//                       :563) run in the FRONT end and leave a tightly packed
//                       vec3 stream in surf->dynamicTexCoords. Skybox is just
//                       (xyz - localViewOrigin), which the shader could redo,
//                       but wobblesky also applies a per-frame rotation, so
//                       taking the stream covers both with one program and no
//                       second code path to keep in sync.
//   diffuse cube        the idDrawVert normal, bound into the same attribute.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 6) in vec3 inTexDir;

uniform mat4 uMVP;
uniform vec4 uVertexColor;		// (rgbMul, rgbAdd, alphaMul, alphaAdd) -- the SVC packing

out vec3 vTexDir;
out vec4 vColor;

void main() {
    vTexDir = inTexDir;
    vColor = vec4(inColor.rgb * uVertexColor.x + uVertexColor.y,
                  inColor.a   * uVertexColor.z + uVertexColor.w);
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
