// Copyright (C) 2026 DarkMatter Productions
#include "GLSLCompiler.h"

#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Include/Types.h>
#include <SPIRV/GlslangToSpv.h>

namespace oq4material {
namespace {

constexpr EShMessages VulkanMessages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
const char *UniformDeclaration = R"(
layout(set=0, binding=0, std140) uniform OQ4MaterialUniforms {
    vec4 parameters[32];
    mat4 modelView;
    mat4 projection;
    mat4 textureMatrix[8];
    vec4 stageColor;
    vec4 controls;
    mat4 modelViewProjection;
    mat4 modelViewProjectionVulkan;
    vec4 textureOrientation;
} oq4;
)";

// Authored coordinates follow OpenGL's bottom-up rows. Images that Vulkan
// rendered keep top-down rows, so their 2D lookups flip Y (and the Y
// gradients) per texture slot. Bias forms only exist in fragment shaders.
const char *FlipHelpers = R"(
bool oq4Flipped(int b) { return (uint(oq4.textureOrientation.x) & (1u << uint(b))) != 0u; }
vec2 oq4FlipY(int b, vec2 c) { return oq4Flipped(b) ? vec2(c.x, 1.0 - c.y) : c; }
vec3 oq4FlipYProj(int b, vec3 c) { return oq4Flipped(b) ? vec3(c.x, c.z - c.y, c.z) : c; }
vec4 oq4FlipYProj(int b, vec4 c) { return oq4Flipped(b) ? vec4(c.x, c.w - c.y, c.z, c.w) : c; }
vec2 oq4FlipD(int b, vec2 d) { return oq4Flipped(b) ? vec2(d.x, -d.y) : d; }
vec4 oq4FlipTexture2D(int b, sampler2D s, vec2 c) { return texture(s, oq4FlipY(b, c)); }
vec4 oq4FlipTexture2DProj(int b, sampler2D s, vec3 c) { return textureProj(s, oq4FlipYProj(b, c)); }
vec4 oq4FlipTexture2DProj(int b, sampler2D s, vec4 c) { return textureProj(s, oq4FlipYProj(b, c)); }
vec4 oq4FlipTexture2DLod(int b, sampler2D s, vec2 c, float l) { return textureLod(s, oq4FlipY(b, c), l); }
vec4 oq4FlipTexture2DProjLod(int b, sampler2D s, vec3 c, float l) { return textureProjLod(s, oq4FlipYProj(b, c), l); }
vec4 oq4FlipTexture2DProjLod(int b, sampler2D s, vec4 c, float l) { return textureProjLod(s, oq4FlipYProj(b, c), l); }
vec4 oq4FlipTexture2DGrad(int b, sampler2D s, vec2 c, vec2 x, vec2 y) { return textureGrad(s, oq4FlipY(b, c), oq4FlipD(b, x), oq4FlipD(b, y)); }
vec4 oq4FlipTexture2DProjGrad(int b, sampler2D s, vec3 c, vec2 x, vec2 y) { return textureProjGrad(s, oq4FlipYProj(b, c), oq4FlipD(b, x), oq4FlipD(b, y)); }
vec4 oq4FlipTexture2DProjGrad(int b, sampler2D s, vec4 c, vec2 x, vec2 y) { return textureProjGrad(s, oq4FlipYProj(b, c), oq4FlipD(b, x), oq4FlipD(b, y)); }
)";
const char *FlipBiasHelpers = R"(
vec4 oq4FlipTexture2D(int b, sampler2D s, vec2 c, float bias) { return texture(s, oq4FlipY(b, c), bias); }
vec4 oq4FlipTexture2DProj(int b, sampler2D s, vec3 c, float bias) { return textureProj(s, oq4FlipYProj(b, c), bias); }
vec4 oq4FlipTexture2DProj(int b, sampler2D s, vec4 c, float bias) { return textureProj(s, oq4FlipYProj(b, c), bias); }
)";

// The 2D lookups whose coordinates name a texture row.
const char *FlipFunction(const std::string &name) {
    static const std::map<std::string, const char *> functions = {
        {"texture2D", "oq4FlipTexture2D"}, {"texture2DProj", "oq4FlipTexture2DProj"},
        {"texture2DLod", "oq4FlipTexture2DLod"}, {"texture2DProjLod", "oq4FlipTexture2DProjLod"},
        {"texture2DGradARB", "oq4FlipTexture2DGrad"}, {"texture2DProjGradARB", "oq4FlipTexture2DProjGrad"},
    };
    const auto found = functions.find(name);
    return found == functions.end() ? nullptr : found->second;
}

struct Process {
    bool initialized = glslang::InitializeProcess();
    std::mutex mutex;
    ~Process() { if (initialized) { glslang::FinalizeProcess(); } }
};

struct Token {
    std::string text;
    size_t begin;
    size_t end;
};

bool Identifier(const std::string &s) {
    if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_')) { return false; }
    for (unsigned char c : s) { if (!std::isalnum(c) && c != '_') { return false; } }
    return s.rfind("gl_", 0) != 0 && s.rfind("oq4", 0) != 0;
}

// Token offsets preserve preprocessor #line records and diagnostic line numbers.
// Comments have already been removed by glslang, not by a regular expression.
std::vector<Token> Tokens(const std::string &source) {
    std::vector<Token> result;
    for (size_t i = 0; i < source.size();) {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (std::isspace(c)) { ++i; continue; }
        const size_t begin = i++;
        if (c == '#') {
            while (i < source.size() && source[i] != '\n') { ++i; }
        } else if (std::isalnum(c) || c == '_') {
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) { ++i; }
        }
        result.push_back({source.substr(begin, i - begin), begin, i});
    }
    return result;
}

bool Bindings(const std::vector<Binding> &bindings, int limit,
              std::map<std::string, int> &byName, std::string &error) {
    std::set<int> slots;
    for (const Binding &binding : bindings) {
        if (!Identifier(binding.name) || binding.slot < 0 || binding.slot >= limit ||
            binding.components < 0 || binding.components > 4 ||
            !byName.emplace(binding.name, binding.slot).second || !slots.insert(binding.slot).second) {
            error = "invalid or duplicate material binding '" + binding.name + "'";
            return false;
        }
    }
    return true;
}

// glslang retains the pointer arrays as well as their contents until parsing.
struct SourceShader : glslang::TShader {
    const char *text;
    const char *file;
    int length;
    SourceShader(EShLanguage stage, const std::string &source, const std::string &name)
        : glslang::TShader(stage), text(source.c_str()), file(name.c_str()), length(static_cast<int>(source.size())) {
        setStringsWithLengthsAndNames(&text, &length, &file, 1);
    }
};

std::unique_ptr<SourceShader> Shader(EShLanguage stage, const std::string &source, const std::string &name) {
    return std::make_unique<SourceShader>(stage, source, name);
}

bool Preprocess(EShLanguage stage, const std::string &source, const std::string &name,
                std::string &preprocessed, std::string &error) {
    auto shader = Shader(stage, source, name);
    glslang::TShader::ForbidIncluder includer;
    if (!shader->preprocess(GetDefaultResources(), 110, ENoProfile, false, false,
                            EShMsgDefault, &preprocessed, includer)) {
        error = name + ": preprocessing failed\n" + shader->getInfoLog();
        return false;
    }
    return true;
}

struct Rewrite {
    std::string source;
    std::string initialization;
    std::map<std::string, std::string> samplers;
    bool flips = false;
};

int AttributeLocation(const std::string &name) {
    // These are the names bound explicitly by the OpenGL material linker.
    static const char *names[] = {"attr_Position", "", "attr_Normal", "attr_Tangent", "attr_Bitangent", "attr_TexCoord0"};
    for (int i = 0; i < 6; ++i) { if (name == names[i]) { return i; } }
    return -1;
}

std::string Builtin(const std::string &name, bool vertex) {
    static const std::map<std::string, std::string> common = {
        {"gl_ModelViewProjectionMatrix", "oq4.modelViewProjection"},
        {"gl_ModelViewMatrix", "oq4.modelView"},
        {"gl_ProjectionMatrix", "oq4.projection"},
        {"gl_TextureMatrix", "oq4.textureMatrix"},
        {"gl_NormalMatrix", "transpose(inverse(mat3(oq4.modelView)))"},
        {"gl_TexCoord", "oq4TexCoord"},
        {"gl_FrontColor", "oq4FrontColor"},
        {"gl_BackColor", "oq4BackColor"},
        {"texture2D", "texture"}, {"textureCube", "texture"},
        {"texture2DProj", "textureProj"},
        {"texture2DLod", "textureLod"}, {"textureCubeLod", "textureLod"},
        {"texture2DProjLod", "textureProjLod"},
        {"texture2DGradARB", "textureGrad"}, {"textureCubeGradARB", "textureGrad"},
        {"texture2DProjGradARB", "textureProjGrad"},
        {"dFdy", "oq4DfdY"},
        {"ftransform", "oq4Transform"}, {"main", "oq4AuthoredMain"},
    };
    auto found = common.find(name);
    if (found != common.end()) { return found->second; }
    if (name == "gl_Color") { return vertex ? "oq4VertexColor()" : "(gl_FrontFacing ? oq4FrontColor : oq4BackColor)"; }
    if (vertex) {
        if (name == "gl_Vertex") { return "oq4Position"; }
        if (name == "gl_Normal") { return "oq4Normal.xyz"; }
        if (name == "gl_MultiTexCoord0") { return "oq4InputTexCoord"; }
    } else {
        if (name == "gl_FragColor") { return "oq4FragColor"; }
        if (name == "gl_FragCoord") { return "vec4(gl_FragCoord.x, oq4LowerOrigin ? gl_FragCoord.y : oq4.controls.w - gl_FragCoord.y, gl_FragCoord.zw)"; }
    }
    return name;
}

bool Translate(const std::string &source, bool vertex, const std::map<std::string, int> &parameters,
               const std::map<std::string, int> &textures, Rewrite &rewrite, std::string &error) {
    const auto tokens = Tokens(source);
    size_t cursor = 0;
    int depth = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];
        rewrite.source += source.substr(cursor, token.begin - cursor);
        cursor = token.end;
        if (token.text.rfind("#version", 0) == 0) {
            // No silent reinterpretation of a newer language as compatibility GLSL.
            const std::string version = token.text.substr(8);
            if (version.find("110") == std::string::npos && version.find("120") == std::string::npos) {
                error = "only GLSL 1.10/1.20 material sources are supported";
                return false;
            }
            continue;
        }
        if (token.text.rfind("#extension", 0) == 0) {
            if (token.text.find("GL_ARB_shader_texture_lod") == std::string::npos) {
                error = "unsupported material extension: " + token.text;
                return false;
            }
            continue; // Core 450 implements the explicit LOD/gradient operations.
        }
        if (token.text.rfind("oq4", 0) == 0) { error = "reserved compiler identifier: " + token.text; return false; }
        if (depth == 0 && (token.text == "uniform" || token.text == "attribute" || token.text == "varying")) {
            const std::string qualifier = token.text;
            if (++i >= tokens.size()) { error = "incomplete interface declaration"; return false; }
            const std::string type = tokens[i].text;
            std::string replacement;
            for (;;) {
                if (++i >= tokens.size() || !Identifier(tokens[i].text)) { error = "invalid interface name"; return false; }
                const std::string name = tokens[i].text;
                if (qualifier == "uniform") {
                    if (type == "sampler2D" || type == "samplerCube") {
                        const auto binding = textures.find(name);
                        if (binding == textures.end()) { error = "sampler has no shaderTexture binding: " + name; return false; }
                        rewrite.samplers[name] = type;
                        replacement += "layout(set=1, binding=" + std::to_string(binding->second) + ") uniform " + type + " " + name + ";";
                    } else {
                        const int width = type == "float" ? 1 : type == "vec2" ? 2 : type == "vec3" ? 3 : type == "vec4" ? 4 : 0;
                        if (!width) { error = "unsupported shaderParm type '" + type + "' for " + name; return false; }
                        replacement += type + " " + name + ";";
                        const auto binding = parameters.find(name);
                        const char *swizzles[] = {"", ".x", ".xy", ".xyz", ""};
                        const std::string value = binding == parameters.end() ? type + "(0.0)" :
                            "oq4.parameters[" + std::to_string(binding->second) + "]" + swizzles[width];
                        rewrite.initialization += name + " = " + value + ";\n";
                    }
                } else if (qualifier == "attribute") {
                    const int location = AttributeLocation(name);
                    if (!vertex || location < 0) { error = "unsupported vertex attribute: " + name; return false; }
                    const int width = type == "float" ? 1 : type == "vec2" ? 2 : type == "vec3" ? 3 : type == "vec4" ? 4 : 0;
                    if (!width) { error = "unsupported vertex attribute type: " + type; return false; }
                    const char *inputs[] = {"oq4Position", "oq4Color", "oq4Normal", "oq4Tangent", "oq4Bitangent", "oq4InputTexCoord"};
                    const char *swizzles[] = {"", ".x", ".xy", ".xyz", ""};
                    replacement += type + " " + name + ";";
                    rewrite.initialization += name + " = " + inputs[location] + swizzles[width] + ";\n";
                } else {
                    replacement += std::string(vertex ? "out " : "in ") + type + " " + name + ";";
                }
                if (++i >= tokens.size()) { error = "unterminated interface declaration"; return false; }
                if (tokens[i].text == ";") { break; }
                if (tokens[i].text != ",") { error = "arrays or initializers are unsupported in material interface: " + name; return false; }
            }
            // Keep the original number of newlines for diagnostics after a rewrite.
            for (size_t p = token.begin; p < tokens[i].end; ++p) { if (source[p] == '\n') { replacement += '\n'; } }
            rewrite.source += replacement;
            cursor = tokens[i].end;
            continue;
        }
        if (token.text == "{") { ++depth; }
        if (token.text == "}") { --depth; }
        // texture2D(Image, ...) on a declared 2D sampler becomes
        // oq4FlipTexture2D(<slot>, Image, ...). A sampler reached through a
        // function parameter keeps the plain lookup.
        const char *flip = FlipFunction(token.text);
        if (flip != nullptr && i + 2 < tokens.size() && tokens[i + 1].text == "(") {
            const auto sampler = rewrite.samplers.find(tokens[i + 2].text);
            const auto binding = textures.find(tokens[i + 2].text);
            if (sampler != rewrite.samplers.end() && sampler->second == "sampler2D" && binding != textures.end()) {
                rewrite.source += flip;
                rewrite.source += source.substr(cursor, tokens[i + 1].begin - cursor);
                rewrite.source += "(" + std::to_string(binding->second) + ", ";
                cursor = tokens[i + 1].end;
                rewrite.flips = true;
                ++i;
                continue;
            }
        }
        rewrite.source += Builtin(token.text, vertex);
    }
    rewrite.source += source.substr(cursor);
    return true;
}

std::string Interface(bool vertex, const std::set<std::string> &builtins) {
    std::string result = "#version 450\n";
    result += UniformDeclaration;
    const char *direction = vertex ? "out " : "in ";
    if (builtins.count("gl_TexCoord")) { result += std::string(direction) + "vec4 oq4TexCoord[8];\n"; }
    const bool colors = builtins.count("gl_Color") || builtins.count("gl_FrontColor") || builtins.count("gl_BackColor");
    if (colors) { result += std::string(direction) + "vec4 oq4FrontColor;\n" + direction + "vec4 oq4BackColor;\n"; }
    if (vertex) {
        if (builtins.count("gl_Vertex") || builtins.count("ftransform") || builtins.count("attr_Position") || builtins.count("gl_ModelViewProjectionMatrix")) { result += "layout(location=0) in vec4 oq4Position;\n"; }
        if (builtins.count("gl_Normal") || builtins.count("attr_Normal")) { result += "layout(location=2) in vec4 oq4Normal;\n"; }
        if (builtins.count("attr_Tangent")) { result += "layout(location=3) in vec4 oq4Tangent;\n"; }
        if (builtins.count("attr_Bitangent")) { result += "layout(location=4) in vec4 oq4Bitangent;\n"; }
        if (builtins.count("gl_MultiTexCoord0") || builtins.count("attr_TexCoord0")) { result += "layout(location=5) in vec4 oq4InputTexCoord;\n"; }
        if (colors) {
            result += R"(
layout(location=1) in vec4 oq4Color;
vec4 oq4VertexColor() {
    if (oq4.controls.x < 0.5) return oq4.stageColor;
    // draw_common's authored GLSL path exposes the color array directly.
    // Stage tint/inverse modulation only apply when the authored shader does it.
    return oq4Color;
}
)";
        }
        if (builtins.count("ftransform")) { result += "vec4 oq4Transform() { return oq4.modelViewProjection * oq4Position; }\n"; }
    } else {
        result += "layout(constant_id=15) const bool oq4LowerOrigin=false;\n";
        result += "layout(location=0) out vec4 oq4FragColor;\n";
        if (builtins.count("dFdy")) {
            // The executor uses a negative-height viewport. Authored GL window
            // Y points up, while Vulkan fragment coordinates point down.
            for (const char *type : {"float", "vec2", "vec3", "vec4"}) {
                result += std::string(type) + " oq4DfdY(" + type + " v) { return (oq4LowerOrigin ? 1.0 : -1.0)*dFdy(v); }\n";
            }
        }
    }
    return result;
}

std::string Wrap(const Rewrite &rewrite, bool vertex, const std::set<std::string> &builtins) {
    std::string result = Interface(vertex, builtins);
    if (rewrite.flips) {
        result += FlipHelpers;
        if (!vertex) { result += FlipBiasHelpers; }
    }
    result += "#line 1\n" + rewrite.source + "\nvoid main() {\n";
    result += rewrite.initialization;
    if (vertex) {
        if (builtins.count("gl_TexCoord")) { result += "for (int i=0; i<8; ++i) oq4TexCoord[i]=vec4(0.0);\n"; }
        if (builtins.count("gl_Color") || builtins.count("gl_FrontColor") || builtins.count("gl_BackColor")) {
            result += "oq4FrontColor=oq4VertexColor(); oq4BackColor=oq4FrontColor;\n";
        }
        result += "oq4AuthoredMain();\n";
        if (builtins.count("ftransform") || builtins.count("gl_ModelViewProjectionMatrix")) {
            // ftransform promises the fixed-function position used by the
            // depth fill. Converting its already-rounded Z after the vertex
            // shader differs from converting the CPU matrix before the draw,
            // which creates holes under EQUAL. Preserve the engine transform
            // only when the authored program left that position unchanged.
            // Displaced/custom positions still receive ordinary GL->VK Z.
            result += "if (all(equal(gl_Position, oq4.modelViewProjection * oq4Position))) {\n"
                      "gl_Position = oq4.modelViewProjectionVulkan * oq4Position;\n} else ";
        }
        // textureOrientation.y is 1 when the pipeline clips depth in OpenGL's
        // [-w, w] range (VK_EXT_depth_clip_control); the position is then final.
        result += "{ if (oq4.textureOrientation.y < 0.5) gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5; }\n";
    } else {
        result += R"(
oq4AuthoredMain();
if (oq4.controls.y > 0.5) {
    if (oq4.controls.y < 1.5 && oq4FragColor.a <= oq4.controls.z) discard;
    if (oq4.controls.y >= 1.5 && oq4.controls.y < 2.5 && oq4FragColor.a >= oq4.controls.z) discard;
    if (oq4.controls.y >= 2.5 && oq4.controls.y < 3.5 && oq4FragColor.a < oq4.controls.z) discard;
    if (oq4.controls.y >= 3.5 && oq4FragColor.a != oq4.controls.z) discard;
}
)";
    }
    return result + "}\n";
}

bool CompilePair(const std::string &vertex, const std::string &fragment, const CompileRequest &request,
                 bool spirv, CompileResult &result) {
    auto vs = Shader(EShLangVertex, vertex, request.vertexName);
    auto fs = Shader(EShLangFragment, fragment, request.fragmentName);
    for (auto *shader : {vs.get(), fs.get()}) {
        if (spirv) {
            shader->setEnvInput(glslang::EShSourceGlsl, shader == vs.get() ? EShLangVertex : EShLangFragment, glslang::EShClientVulkan, 450);
            shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
            shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
            shader->setAutoMapLocations(true);
        }
        if (!shader->parse(GetDefaultResources(), spirv ? 450 : 110, false, spirv ? VulkanMessages : EShMsgDefault)) {
            result.diagnostic = (shader == vs.get() ? request.vertexName : request.fragmentName) +
                (spirv ? ": Vulkan compilation failed\n" : ": authored GLSL compilation failed\n") + shader->getInfoLog();
            return false;
        }
    }
    glslang::TProgram program;
    program.addShader(vs.get());
    program.addShader(fs.get());
    if (!program.link(spirv ? VulkanMessages : EShMsgDefault) || (spirv && !program.mapIO())) {
        result.diagnostic = request.vertexName + " + " + request.fragmentName + ": link failed\n" + program.getInfoLog();
        return false;
    }
    if (!program.buildReflection()) { result.diagnostic = "could not reflect material interface"; return false; }
    if (!spirv) {
        for (int i = 0; i < program.getNumUniformVariables(); ++i) {
            const auto &uniform = program.getUniform(i);
            for (const Binding &binding : request.parameters) {
                if (binding.name == uniform.name && binding.components != 0 &&
                    uniform.getType()->getVectorSize() != binding.components) {
                    result.diagnostic = "shaderParm component count mismatch: " + binding.name;
                    return false;
                }
            }
        }
        return true;
    }
    static const char *inputNames[] = {"oq4Position", "oq4Color", "oq4Normal", "oq4Tangent", "oq4Bitangent", "oq4InputTexCoord"};
    for (int i = 0; i < program.getNumPipeInputs(); ++i) {
        const auto &input = program.getPipeInput(i);
        bool found = false;
        for (int location = 0; location < 6; ++location) {
            if (input.name == inputNames[location]) { result.vertexInputMask |= 1u << location; found = true; break; }
        }
        if (!found) { result.diagnostic = "unsupported reflected vertex input: " + input.name; return false; }
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.validate = false; // SPIRV-Tools is not a runtime dependency.
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), result.vertex, &logger, &options);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), result.fragment, &logger, &options);
    if (result.vertex.size() < 5 || result.fragment.size() < 5 || !logger.getAllMessages().empty()) {
        result.diagnostic = "SPIR-V generation failed: " + logger.getAllMessages();
        result.vertex.clear();
        result.fragment.clear();
        return false;
    }
    return true;
}

} // namespace

bool CompileGLSL(const CompileRequest &request, CompileResult &result) {
    result = {};
    for (const std::string *source : {&request.vertexSource, &request.fragmentSource}) {
        if (source->empty() || source->size() > MaxSourceBytes || source->find('\0') != std::string::npos) {
            result.diagnostic = "empty, oversized or embedded-NUL material source";
            return false;
        }
    }
    std::map<std::string, int> parameters, textures;
    if (!Bindings(request.parameters, MaxParameters, parameters, result.diagnostic) ||
        !Bindings(request.textures, MaxTextures, textures, result.diagnostic)) { return false; }
    for (const auto &parameter : parameters) {
        if (textures.count(parameter.first)) { result.diagnostic = "shaderParm/shaderTexture name collision: " + parameter.first; return false; }
    }
    static Process process;
    std::lock_guard<std::mutex> lock(process.mutex);
    if (!process.initialized) { result.diagnostic = "glslang process initialization failed"; return false; }
    // Validate the original language before making uniforms writable globals.
    // Otherwise, illegal writes to an authored uniform could become valid code.
    if (!CompilePair(request.vertexSource, request.fragmentSource, request, false, result)) { return false; }
    std::string vertex, fragment;
    if (!Preprocess(EShLangVertex, request.vertexSource, request.vertexName, vertex, result.diagnostic) ||
        !Preprocess(EShLangFragment, request.fragmentSource, request.fragmentName, fragment, result.diagnostic)) { return false; }
    std::set<std::string> builtins;
    for (const auto *source : {&vertex, &fragment}) {
        for (const Token &token : Tokens(*source)) { builtins.insert(token.text); }
    }
    Rewrite vs, fs;
    if (!Translate(vertex, true, parameters, textures, vs, result.diagnostic)) {
        result.diagnostic = request.vertexName + ": " + result.diagnostic;
        return false;
    }
    if (!Translate(fragment, false, parameters, textures, fs, result.diagnostic)) {
        result.diagnostic = request.fragmentName + ": " + result.diagnostic;
        return false;
    }
    std::map<std::string, std::string> samplers = vs.samplers;
    samplers.insert(fs.samplers.begin(), fs.samplers.end());
    for (const auto &sampler : samplers) {
        const uint32_t bit = 1u << textures.at(sampler.first);
        result.textureMask |= bit;
        if (sampler.second == "samplerCube") { result.cubeTextureMask |= bit; }
    }
    result.vertexSource = Wrap(vs, true, builtins);
    result.fragmentSource = Wrap(fs, false, builtins);
    return CompilePair(result.vertexSource, result.fragmentSource, request, true, result);
}

} // namespace oq4material
