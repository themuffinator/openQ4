// Copyright (C) 2026 DarkMatter Productions
#include "../../../src/renderer/materialprogram/GLSLCompiler.h"
#include "../../../src/renderer/BuiltinModelPreviewShader.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <future>
#include <string>

namespace material = oq4material;
static int passed = 0;
static std::filesystem::path output;

static void Require(bool value, const std::string &description) {
    if (!value) { std::fprintf(stderr, "MaterialCompilerTest: %s\n", description.c_str()); std::exit(1); }
}

static material::CompileRequest Basic() {
    material::CompileRequest request;
    request.vertexName = "glprogs/custom_test.vs";
    request.fragmentName = "glprogs/custom_test.fs";
    request.vertexSource = R"(#version 120
uniform vec4 offset;
varying vec2 uv;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * (gl_Vertex + vec4(offset.xyz,0.0));
    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    gl_FrontColor = gl_Color;
})";
    request.fragmentSource = R"(#version 120
uniform sampler2D image;
uniform vec4 tint;
varying vec2 uv;
void main() { gl_FragColor = texture2D(image, uv) * tint * gl_Color; }
)";
    request.parameters = {{"offset", 0, 4}, {"tint", 31, 4}};
    request.textures = {{"image", 7}};
    return request;
}

static material::CompileResult Accept(const char *label, const material::CompileRequest &request) {
    material::CompileResult result;
    const bool compiled = material::CompileGLSL(request, result);
    Require(compiled, std::string(label) + ": " + result.diagnostic);
    Require(result.vertex.size() > 5 && result.fragment.size() > 5 &&
        result.vertex[0] == 0x07230203 && result.fragment[0] == 0x07230203, std::string(label) + ": missing SPIR-V");
    if (!output.empty()) {
        for (int stage = 0; stage < 2; ++stage) {
            const auto &words = stage == 0 ? result.vertex : result.fragment;
            const std::string name = std::string(label) + (stage == 0 ? ".vert" : ".frag");
            std::ofstream binary(output / (name + ".spv"), std::ios::binary);
            binary.write(reinterpret_cast<const char *>(words.data()), words.size() * sizeof(uint32_t));
            Require(bool(binary), "write SPIR-V evidence");
            std::ofstream source(output / name);
            source << (stage == 0 ? result.vertexSource : result.fragmentSource);
        }
    }
    ++passed;
    return result;
}

static void Reject(const char *label, const material::CompileRequest &request, const char *diagnostic) {
    material::CompileResult result;
    result.vertex = {1, 2, 3}; // Failure must invalidate any previous successful result.
    result.fragment = {1, 2, 3};
    Require(!material::CompileGLSL(request, result), std::string(label) + ": accepted invalid source");
    Require(result.vertex.empty() && result.fragment.empty(), std::string(label) + ": stale compiled modules");
    Require(result.diagnostic.find(diagnostic) != std::string::npos, std::string(label) + ": " + result.diagnostic);
    ++passed;
}

int main(int argc, char **argv) {
    if (argc == 2) { output = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(output); }
    material::CompileRequest model;
    model.vertexName="builtin:retained/modelResolve.vp"; model.fragmentName="builtin:retained/modelResolve.fp";
    model.vertexSource=RetainedModelVertexSource; model.fragmentSource=RetainedModelFragmentSource;
    model.textures={{"Colour",0},{"Depth",1}};
    Require(Accept("retained_model",model).textureMask==3,"model resolve binds colour and depth in actual Vulkan compiler");
    if (argc == 3 && std::string(argv[1]) == "--retained") {
        // Compile the shipped source pairs and their material interfaces
        // through the actual Vulkan
        // compatibility compiler rather than a duplicate test shader.
        for (const char *name : {"retained_blur", "retained_linear_mask", "retained_streak", "retained_glitch"}) {
            material::CompileRequest retained;
            const auto base = std::filesystem::path(argv[2]) / name;
            const auto read = [&](const char *extension, std::string &path) {
                path = base.string() + extension;
                std::ifstream file(path, std::ios::binary);
                Require(bool(file), "read production retained shader: " + path);
                return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            };
            retained.vertexSource = read(".glslvp", retained.vertexName);
            retained.fragmentSource = read(".glslfp", retained.fragmentName);
            const std::string program = name;
            retained.parameters = {{program == "retained_blur" ? "blur" : program == "retained_streak" ? "streak" :
                program == "retained_glitch" ? "fault" : "maskConfig", 0, 4}};
            retained.textures = {{"Source", 0}};
            Require(Accept(name, retained).textureMask == 1, "retained source texture binding");
        }
    }
    auto request = Basic();
    const auto first = Accept("textured", request);
    Require(first.textureMask == (1u << 7) && first.cubeTextureMask == 0, "2D texture binding metadata");
    Require(first.vertexInputMask == 0x23, "reflected position/color/UV inputs");
    const auto again = Accept("repeat", request);
    Require(first.vertex == again.vertex && first.fragment == again.fragment, "non-deterministic compilation");

    request.vertexSource = R"(#version 120
attribute vec4 attr_Position;
attribute vec2 attr_TexCoord0;
attribute vec3 attr_Normal, attr_Tangent, attr_Bitangent;
varying vec2 uv;
void main() { gl_Position=gl_ModelViewProjectionMatrix*attr_Position;
uv=attr_TexCoord0 + (attr_Normal.xy+attr_Tangent.xy+attr_Bitangent.xy)*0.001;
gl_FrontColor=vec4(1.0); }
)";
    Accept("named_attributes", request);
    request.vertexSource = R"(#version 120
attribute vec4 attr_Position;
attribute vec2 attr_TexCoord0;
varying vec2 uv;
void main() { gl_Position=gl_ModelViewProjectionMatrix*((attr_Position+gl_Vertex)*0.5);
uv=(attr_TexCoord0+gl_MultiTexCoord0.xy)*0.5; gl_FrontColor=gl_Color; }
)";
    Accept("attribute_aliases", request);
    request = Basic();
    request.fragmentSource = R"(#version 120
#define FACTOR vec4(0.5)
#if 0
this is not shader code!
#endif
uniform vec4 tint;
varying vec2 uv;
void main() { float tint = 0.75; gl_FragColor=FACTOR * tint + vec4(uv,0.0,0.0); }
)";
    Accept("macro_shadow", request);
    request = Basic();
    request.vertexSource = R"(#version 120
void main() { gl_Position=ftransform(); gl_TexCoord[0]=gl_MultiTexCoord0; }
)";
    request.fragmentSource = R"(#version 120
uniform samplerCube environment;
uniform float gain;
uniform vec2 bias;
uniform vec3 unbound;
void main() { gl_FragColor=textureCube(environment,gl_TexCoord[0].xyz)*gain+vec4(bias,unbound.x,0.0); }
)";
    request.parameters = {{"gain", 1}, {"bias", 2}};
    request.textures = {{"environment", 0}};
    Require(Accept("cube_builtin_varyings", request).cubeTextureMask == 1, "cube texture metadata");
    request.fragmentSource = R"(#version 120
#extension GL_ARB_shader_texture_lod : enable
uniform sampler2D image;
void main() { vec2 uv=gl_TexCoord[0].xy;
gl_FragColor=texture2DGradARB(image,uv,dFdx(uv),dFdy(uv)); }
)";
    request.textures = {{"image", 0}};
    Accept("explicit_gradients", request);
    request.fragmentSource = R"(#version 120
#extension GL_ARB_shader_texture_lod : enable
uniform sampler2D image;
void main() { gl_FragColor=texture2DLod(image,gl_TexCoord[0].xy,1.5); }
)";
    Accept("explicit_lod", request);
    request.fragmentSource = "#version 120\nvoid main(){gl_FragColor=vec4(gl_FragCoord.xy/256.0,0.0,1.0);}";
    Accept("fragment_coordinates", request);
    request.fragmentSource = R"(#version 120
void main(){ float a=dFdy(gl_FragCoord.y); vec2 b=dFdy(gl_FragCoord.xy);
vec3 c=dFdy(gl_FragCoord.xyz); vec4 d=dFdy(gl_FragCoord);
gl_FragColor=d+vec4(c,0.0)+vec4(b,a,0.0); }
)";
    Accept("window_derivatives", request);
    request = Basic();
    request.vertexSource = "void main(){gl_Position=ftransform(); gl_FrontColor=gl_Color;}";
    request.fragmentSource = "void main(){gl_FragColor=gl_Color;}";
    Accept("implicit_version", request);
    request.vertexSource = R"(#version 120
varying vec3 normal;
void main(){ gl_Position=ftransform(); normal=normalize(gl_NormalMatrix*gl_Normal); }
)";
    request.fragmentSource = R"(#version 120
varying vec3 normal;
void main(){ float lighting=clamp(dot(normalize(normal),vec3(0.0,0.0,1.0)),0.0,1.0);
gl_FragColor=vec4(vec3(lighting),1.0); }
)";
    Accept("normal_transform", request);

    request = Basic(); request.fragmentSource = "#version 120\nvoid main(){gl_FragColor=missing;}";
    Reject("syntax", request, "authored GLSL compilation failed");
    request = Basic(); request.fragmentSource = "#version 120\nuniform vec4 tint;void main(){tint=vec4(1.0);gl_FragColor=tint;}";
    Reject("uniform_write", request, "authored GLSL compilation failed");
    request = Basic(); request.fragmentSource = "#version 120\nvarying vec3 uv;void main(){gl_FragColor=vec4(uv,1.0);}";
    Reject("varying_mismatch", request, "link failed");
    request = Basic(); request.textures.clear();
    Reject("unbound_sampler", request, "no shaderTexture binding");
    request = Basic(); request.parameters.push_back({"tint", 2});
    Reject("duplicate_name", request, "duplicate material binding");
    request = Basic(); request.parameters.push_back({"another", 0});
    Reject("duplicate_slot", request, "duplicate material binding");
    request = Basic(); request.parameters[0].slot = 32;
    Reject("parameter_limit", request, "invalid or duplicate");
    request = Basic(); request.parameters[0].components = 3;
    Reject("parameter_width", request, "component count mismatch");
    request = Basic(); request.parameters[0].components = 5;
    Reject("parameter_invalid_width", request, "invalid or duplicate");
    request = Basic(); request.textures[0].slot = 8;
    Reject("texture_limit", request, "invalid or duplicate");
    request = Basic(); request.parameters.push_back({"image", 2});
    Reject("binding_collision", request, "name collision");
    request = Basic(); request.parameters[0].name = "bad;name";
    Reject("binding_identifier", request, "invalid or duplicate");
    request = Basic(); request.fragmentSource = "#version 120\nuniform vec4 colors[2];void main(){gl_FragColor=colors[0];}";
    Reject("uniform_array", request, "arrays or initializers");
    request = Basic(); request.fragmentSource = "#version 120\nuniform mat4 matrix;void main(){gl_FragColor=matrix[0];}";
    Reject("uniform_matrix", request, "unsupported shaderParm type");
    request = Basic(); request.vertexSource = "#version 120\nattribute vec4 unknown;void main(){gl_Position=unknown;}";
    Reject("unknown_attribute", request, "unsupported vertex attribute");
    request = Basic(); request.fragmentSource = "#version 130\nvoid main(){gl_FragColor=vec4(1.0);}";
    Reject("newer_language", request, "only GLSL 1.10/1.20");
    request = Basic(); request.fragmentSource.clear();
    Reject("empty", request, "empty");
    request = Basic(); request.fragmentSource.append(1, '\0');
    Reject("nul", request, "embedded-NUL");
    request = Basic(); request.fragmentSource.assign(material::MaxSourceBytes + 1, ' ');
    Reject("oversized", request, "oversized");
    request = Basic(); request.fragmentSource = "#version 120\nvoid main(){float oq4Reserved=1.0;gl_FragColor=vec4(oq4Reserved);}";
    Reject("reserved_name", request, "reserved compiler identifier");

    // Shared compiler process lifetime must survive independent worker callers.
    auto worker = [] { material::CompileResult r; return material::CompileGLSL(Basic(), r); };
    auto a = std::async(std::launch::async, worker);
    auto b = std::async(std::launch::async, worker);
    Require(a.get() && b.get(), "concurrent compile callers"); ++passed;
    std::printf("MaterialCompilerTest: %d cases passed\n", passed);
    return 0;
}
