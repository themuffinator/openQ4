// Copyright (C) 2026 DarkMatter Productions
//
// Shadow volume extrusion -- fragment stage.
//
// The shadow pass writes only stencil: GL_State masks colour and depth
// (GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHMASK), so this output is never
// stored. It still has to exist and to be cheap.

#include "glsl_shaders.h"

const char * const glesStencilShadowShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

out vec4 outColor;

void main() {
    outColor = vec4(1.0);
}
)";
