// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_MATERIALPROGRAM_GLSLCOMPILER_H
#define OPENQ4_MATERIALPROGRAM_GLSLCOMPILER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oq4material {

constexpr int MaxParameters = 32;
constexpr int MaxTextures = 8;
constexpr size_t MaxSourceBytes = 1024 * 1024;

// std140 ABI, independent of the engine's idMath types and of Vulkan handles.
// Matrices are column-major, in the authored OpenGL coordinate convention.
struct UniformBlock {
    std::array<float, 4> parameters[MaxParameters];
    std::array<float, 16> modelView;
    std::array<float, 16> projection;
    std::array<float, 16> textureMatrix[MaxTextures];
    std::array<float, 4> stageColor;
    // Use vertex color array, AlphaCompare value, threshold, framebuffer height.
    std::array<float, 4> controls;
    // CPU-composed canonical transforms. The second matches the depth fill's
    // Vulkan clip conversion exactly; authored matrix reads retain GL space.
    std::array<float, 16> modelViewProjection;
    std::array<float, 16> modelViewProjectionVulkan;
    // x: one bit per texture slot whose image stores rows top-down (Vulkan
    // render targets); authored 2D coordinates are flipped for those images.
    // y: 1 when the pipeline clips depth in OpenGL's [-w, w] range, so a
    // displaced authored position keeps its OpenGL clip depth unchanged.
    std::array<float, 4> textureOrientation;
};
static_assert(sizeof(UniformBlock) == 1328, "material uniform std140 ABI");
static_assert(offsetof(UniformBlock, modelView) == 512 && offsetof(UniformBlock, projection) == 576 &&
    offsetof(UniformBlock, textureMatrix) == 640 && offsetof(UniformBlock, stageColor) == 1152 &&
    offsetof(UniformBlock, controls) == 1168 && offsetof(UniformBlock, modelViewProjection) == 1184 &&
    offsetof(UniformBlock, modelViewProjectionVulkan) == 1248 && offsetof(UniformBlock, textureOrientation) == 1312,
    "material uniform std140 offsets");

enum class AlphaCompare { Disabled, Greater, Less, GreaterEqual, Equal };

struct Binding {
    std::string name;
    int slot = 0;
    // shaderParm upload width; zero leaves width unconstrained for callers
    // that do not carry the engine's glUniformN contract. Unused for textures.
    int components = 0;
};

struct CompileRequest {
    std::string vertexName;
    std::string fragmentName;
    std::string vertexSource;
    std::string fragmentSource;
    std::vector<Binding> parameters;
    std::vector<Binding> textures;
};

struct CompileResult {
    std::vector<uint32_t> vertex;
    std::vector<uint32_t> fragment;
    std::string vertexSource;
    std::string fragmentSource;
    std::string diagnostic;
    uint32_t textureMask = 0;
    uint32_t cubeTextureMask = 0;
    uint32_t vertexInputMask = 0;
};

// Compiles a compatibility GLSL 1.10/1.20 pair with named scalar/vector
// parameters and 2D/cube textures. Does not read files, invoke an SDK, create
// GPU objects, or retain source pointers. Failure always clears both modules.
bool CompileGLSL(const CompileRequest &request, CompileResult &result);

} // namespace oq4material
#endif
