#version 450

// REN-09 volumetric march through a projected (or cascaded sunlight) shadow
// map; see volumetric_inject.glsl.

#extension GL_GOOGLE_include_directive : require
#define VOL_SHADOW_PROJECTED 1
#include "volumetric_inject.glsl"
