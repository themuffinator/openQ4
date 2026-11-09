// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../../idlib/CryptoHash.h"
#include "VulkanDevice.h"
#include "vk_Image.h"
#include "vk_ExecutorHooks.h"
#include "vk_MaterialPrograms.h"
#include "vk_MaterialSourceSignatures.h"
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Developer drill for the descriptor-refusal path on a real driver: frame
// slots create one real pool sized for this many authored draws and never
// chain another, so later draws in the frame reach the driver's own
// VK_ERROR_OUT_OF_POOL_MEMORY. 0 keeps the production chained pools.
static idCVar r_vkMaterialDescriptorPoolTest( "r_vkMaterialDescriptorPoolTest", "0", CVAR_RENDERER | CVAR_INTEGER,
	"authored-material descriptor drill: one unchained pool of this many draws per frame slot (0 = production pools)", 0, 2048 );

namespace {
constexpr int MaxPrograms = 256;
// Authored draws per frame slot: descriptor pools of MaxDrawsPerPool sets are
// chained on demand, up to MaxPoolsPerSlot.
constexpr int MaxDrawsPerPool = 2048;
constexpr int MaxPoolsPerSlot = 4;
constexpr size_t MaxCompilerBytes = 32 * 1024 * 1024;
// Distinct program/reason pairs reported since the last reload; later ones
// are counted, not printed, so a broken mod cannot flood the console.
constexpr size_t MaxDiagnostics = 512;
struct Program {
    std::string name, key, vertexSource, fragmentSource;
    oq4material::CompileResult compiled;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    bool sourcePairFound = false, valid = false, loggedDraw = false, loggedLightingDraw = false;
    // ARB assembly translated to compatibility GLSL; parameters[i] feeds
    // uniform slot i and textures bind by texture image unit.
    bool arb = false;
    std::vector<oq4material::ARBParameter> arbParameters;
    int identity = 0;
    // Compiler storage this version holds, returned when it is evicted.
    size_t bytes = 0;
    // Frame serial of the last bind attempt; a version no frame in flight
    // can reference may be evicted once no current key names it.
    uint64_t lastUseFrame = 0;
    // Since the last reload: draws recorded and draws skipped, with the most
    // recent skip reason. reportShaderPrograms prints them.
    int drawn = 0, skipped = 0;
    std::string lastSkip;
};
std::vector<std::unique_ptr<Program>> programs;
// key -> version index. A key the full table refused maps to the frame
// serial it was refused at in rejectedKeys instead, so its sources are not
// re-read on every draw.
std::map<std::string, int> currentPrograms;
std::map<std::string, uint64_t> rejectedKeys;
std::mutex nativeFamilyMutex;
std::map<std::string, vkGLSLProgramFamily_t> nativeFamilies;
// Advances whenever nativeFamilies is cleared, so verdicts cached from
// VK_MaterialPrograms_NeedsStencil are recomputed against the new sources.
int nativeFamilyGeneration = 0;
std::map<std::string, bool> arbTranslatable;
std::mutex diagnosticMutex;
std::set<std::string> diagnostics;
int suppressedDiagnostics = 0;
int capacityRejections = 0;
int evictions = 0;
size_t compilerBytes = 0;
uint64_t frameSerial = 1;
VkDescriptorSetLayout uniformLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
struct FramePools {
    VkDescriptorPool pools[MaxPoolsPerSlot] = {};
    int created = 0;
    int draws = 0;
    bool ready = false;
    // Draws each pool was created for. Below MaxDrawsPerPool the slot is in
    // the r_vkMaterialDescriptorPoolTest drill: one pool, never chained.
    int poolDraws = MaxDrawsPerPool;
};
FramePools framePools[VK_FRAMES_IN_FLIGHT];
int peakDraws = 0;

std::string SourceDigest( const char *source, int bytes ) {
    if ( source == NULL || bytes <= 0 || static_cast<size_t>( bytes ) > oq4material::MaxSourceBytes ) { return ""; }
    std::string normalized;
    normalized.reserve( bytes );
    for ( int i = 0; i < bytes; ++i ) {
        if ( source[i] == '\r' && i + 1 < bytes && source[i + 1] == '\n' ) { continue; }
        normalized += source[i];
    }
    std::uint8_t digest[idCrypto::SHA256_DIGEST_BYTES];
    idCrypto::SHA256( normalized.data(), normalized.size(), digest );
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for ( std::uint8_t value : digest ) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}

const char *Kind( const Program &program ) { return program.arb ? "ARB" : "GLSL"; }

// One line per distinct subject/reason pair until the next reload.
bool Report( const std::string &subject, const std::string &reason ) {
    std::lock_guard<std::mutex> lock( diagnosticMutex );
    const std::string key = subject + "\n" + reason;
    if ( diagnostics.count( key ) != 0 ) { return false; }
    if ( diagnostics.size() >= MaxDiagnostics ) {
        if ( suppressedDiagnostics++ == 0 ) {
            common->Warning( "Vulkan: further authored material program diagnostics are suppressed until reloadGLSLprograms (see reportShaderPrograms)" );
        }
        return false;
    }
    diagnostics.insert( key );
    return true;
}

// A bind-time rejection. The stage is skipped; the reason is reported once per
// program and reason and kept for reportShaderPrograms.
bool Fail( Program &program, const char *reason ) {
    ++program.skipped;
    program.lastSkip = reason;
    if ( Report( std::string( Kind( program ) ) + "|" + program.name, reason ) ) {
        common->Warning( "Vulkan authored %s '%s': %s; the stage is skipped", Kind( program ), program.name.c_str(), reason );
    }
    return false;
}

bool IsCurrent( int index ) {
    for ( const auto &entry : currentPrograms ) {
        if ( entry.second == index ) { return true; }
    }
    return false;
}

void Release( Program &program ) {
    if ( program.vertex != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program.vertex, NULL ); }
    if ( program.fragment != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program.fragment, NULL ); }
    program.vertex = program.fragment = VK_NULL_HANDLE;
    VK_Exec_ReleaseAuthoredMaterialPipelines( program.identity );
    compilerBytes -= Min( compilerBytes, program.bytes );
    program.bytes = 0;
}

// Both the version and storage budgets must reclaim only stale, retired
// versions. Reloading a large source can hit the byte budget well before the
// version limit; keeping those stale sources used to poison every later edit.
int EvictionCandidate() {
    int best = -1;
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { continue; }
        const Program &candidate = *programs[i];
        if ( candidate.lastUseFrame + VK_FRAMES_IN_FLIGHT > frameSerial || IsCurrent( static_cast<int>( i ) ) ) { continue; }
        if ( best < 0 || candidate.lastUseFrame < programs[best]->lastUseFrame ) { best = static_cast<int>( i ); }
    }
    return best;
}

void Evict( int slot ) {
    Release( *programs[slot] );
    programs[slot].reset();
    ++evictions;
}

// Pipeline pressure can precede the source-version and byte limits. Reuse the
// same retirement policy: current and in-flight programs must keep their GPU
// resources. A stale version may own no pipelines, so keep searching until a
// slot is freed or the finite version table has no eligible victim left.
bool ReclaimPipelineCapacity() {
    if ( !VK_Exec_ProgramPipelineCapacityExhausted() ) { return false; }
    do {
        const int slot = EvictionCandidate();
        if ( slot < 0 ) { return false; }
        Evict( slot );
    } while ( VK_Exec_ProgramPipelineCapacityExhausted() );
    return true;
}

int AcquireSlot() {
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { return static_cast<int>( i ); }
    }
    if ( programs.size() < MaxPrograms ) { return static_cast<int>( programs.size() ); }
    const int slot = EvictionCandidate();
    if ( slot >= 0 ) { Evict( slot ); }
    return slot;
}

bool ReserveCompilerStorage( size_t bytes ) {
    if ( bytes > MaxCompilerBytes ) { return false; }
    while ( compilerBytes > MaxCompilerBytes - bytes ) {
        const int slot = EvictionCandidate();
        if ( slot < 0 ) { return false; }
        Evict( slot );
    }
    return true;
}

// A key the full table refused. Retried once the versions in flight have
// retired, or at the next reload; until then its sources are not read again.
bool RecentlyRejected( const std::string &key ) {
    const auto found = rejectedKeys.find( key );
    if ( found == rejectedKeys.end() ) { return false; }
    if ( found->second + VK_FRAMES_IN_FLIGHT > frameSerial ) { return true; }
    rejectedKeys.erase( found );
    return false;
}

void Reject( const std::string &key, const char *kind, const std::string &name ) {
    rejectedKeys[key] = frameSerial;
    ++capacityRejections;
    if ( Report( std::string( kind ) + "|" + name, "capacity" ) ) {
        common->Warning( "Vulkan authored %s '%s': all %d program versions are current or in flight; the stage is skipped until one retires",
            kind, name.c_str(), MaxPrograms );
    }
}

// Places a new version at slot and accounts its storage.
Program *Install( std::unique_ptr<Program> program, int slot ) {
    program->identity = slot;
    if ( !ReserveCompilerStorage( program->bytes ) ) {
        program->compiled = {};
        // clear() retains string capacity and would defeat the storage bound.
        std::string().swap( program->vertexSource );
        std::string().swap( program->fragmentSource );
        std::vector<oq4material::ARBParameter>().swap( program->arbParameters );
        program->compiled.diagnostic = "material compiler storage exhausted";
        program->valid = false;
        program->bytes = 0;
    }
    compilerBytes += program->bytes;
    currentPrograms[program->key] = slot;
    if ( slot == static_cast<int>( programs.size() ) ) {
        programs.push_back( std::move( program ) );
    } else {
        programs[slot] = std::move( program );
    }
    return programs[slot].get();
}

size_t StorageBytes( const Program &program ) {
    return program.vertexSource.size() + program.fragmentSource.size() + program.compiled.diagnostic.size()
        + program.compiled.vertexSource.size() + program.compiled.fragmentSource.size()
        + (program.compiled.vertex.size() + program.compiled.fragment.size()) * sizeof( uint32_t );
}

int ParameterComponents( const newShaderStage_t *stage, int slot ) {
    switch ( stage->shaderParmBindings[slot] ) {
    case GLSL_SHADERPARM_REGISTERS: return stage->shaderParmNumRegisters[slot];
    case GLSL_SHADERPARM_POSTPROCESS_INV_TEX_SIZE:
    case GLSL_SHADERPARM_POSTPROCESS_TEX_SIZE:
    case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_ORIGIN:
    case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_SIZE:
    case GLSL_SHADERPARM_CURRENT_RENDER_TEXTURE_SCALE: return 2;
    default: return 4;
    }
}

Program *Find( const newShaderStage_t *stage ) {
    if ( stage == NULL || !stage->glslProgram ) { return NULL; }
    // Binding names, order and semantic types distinguish shared program names.
    // Numeric register values are per draw and must not cause recompilation.
    std::string key = stage->glslProgramName;
    for ( int i = 0; i < stage->numShaderParms; ++i ) {
        key += "|p:" + std::string( stage->shaderParmNames[i] ) + ":" + std::to_string( stage->shaderParmBindings[i] )
            + ":" + std::to_string( stage->shaderParmNumRegisters[i] );
    }
    for ( int i = 0; i < stage->numShaderTextures; ++i ) { key += "|t:" + std::string( stage->shaderTextureNames[i] ); }
    auto found = currentPrograms.find( key );
    if ( found != currentPrograms.end() ) { return programs[found->second].get(); }
    if ( RecentlyRejected( key ) ) { return NULL; }
    oq4material::CompileRequest request;
    std::string vertex, fragment;
    int vertexBytes = 0, fragmentBytes = 0;
    idStr vertexPath, fragmentPath;
    const bool sourcePairFound = R_FindGLSLSourcePair( stage->glslProgramName, vertexPath, fragmentPath,
        vertex, fragment, &vertexBytes, &fragmentBytes );
    if ( sourcePairFound ) {
        request.vertexName = vertexPath.c_str(); request.fragmentName = fragmentPath.c_str();
        if ( vertexBytes > 0 && fragmentBytes > 0
            && static_cast<size_t>( vertexBytes ) <= oq4material::MaxSourceBytes
            && static_cast<size_t>( fragmentBytes ) <= oq4material::MaxSourceBytes ) {
            request.vertexSource = vertex;
            request.fragmentSource = fragment;
        }
    }
    // Reuse precedes the capacity check: an unchanged reload must still work
    // when all version slots are occupied. Failed versions are reusable too.
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { continue; }
        Program &old = *programs[i];
        if ( old.key == key && old.sourcePairFound == sourcePairFound
            && old.vertexSource == request.vertexSource && old.fragmentSource == request.fragmentSource ) {
            currentPrograms[key] = static_cast<int>( i );
            if ( !old.valid ) { common->Warning( "Vulkan authored GLSL '%s': %s", old.name.c_str(), old.compiled.diagnostic.c_str() ); }
            return &old;
        }
    }
    const int slot = AcquireSlot();
    if ( slot < 0 ) { Reject( key, "GLSL", stage->glslProgramName ); return NULL; }
    auto program = std::make_unique<Program>();
    program->name = stage->glslProgramName;
    program->key = key;
    program->sourcePairFound = sourcePairFound;
    program->vertexSource = request.vertexSource;
    program->fragmentSource = request.fragmentSource;
    if ( sourcePairFound ) {
        for ( int i = 0; i < stage->numShaderParms; ++i ) {
            request.parameters.push_back( {stage->shaderParmNames[i], i, ParameterComponents( stage, i )} );
        }
        for ( int i = 0; i < stage->numShaderTextures; ++i ) { request.textures.push_back( {stage->shaderTextureNames[i], i} ); }
        if ( ReserveCompilerStorage( request.vertexSource.size() + request.fragmentSource.size() ) ) {
            program->valid = oq4material::CompileGLSL( request, program->compiled );
        } else { program->compiled.diagnostic = "material compiler storage exhausted"; }
    } else { program->compiled.diagnostic = "could not find a complete vertex/fragment source pair"; }
    program->bytes = StorageBytes( *program );
    Program *installed = Install( std::move( program ), slot );
    if ( !installed->valid ) {
        common->Warning( "Vulkan authored GLSL '%s': %s", installed->name.c_str(), installed->compiled.diagnostic.c_str() );
    } else {
        common->Printf( "Vulkan: compiled authored GLSL '%s' (%s, %s)\n", installed->name.c_str(), vertexPath.c_str(), fragmentPath.c_str() );
    }
    return installed;
}

// Reads glprogs/<file>. An empty name selects the fixed-function stage.
bool ReadARBSource( const char *file, std::string &source ) {
    source.clear();
    if ( file == NULL || file[0] == '\0' ) { return true; }
    idStr path = "glprogs/";
    path += file;
    path.BackSlashesToSlashes();
    void *buffer = NULL;
    const int length = fileSystem->ReadFile( path.c_str(), &buffer, NULL );
    if ( buffer == NULL || length <= 0 ) {
        if ( buffer != NULL ) { fileSystem->FreeFile( buffer ); }
        return false;
    }
    source.assign( static_cast<const char *>( buffer ), static_cast<size_t>( length ) );
    fileSystem->FreeFile( buffer );
    return true;
}

Program *FindARB( const char *vertexFile, const char *fragmentFile ) {
    const std::string vertexName = vertexFile != NULL ? vertexFile : "";
    const std::string fragmentName = fragmentFile != NULL ? fragmentFile : "";
    if ( vertexName.empty() && fragmentName.empty() ) { return NULL; }
    const std::string key = "arb|" + vertexName + "|" + fragmentName;
    auto found = currentPrograms.find( key );
    if ( found != currentPrograms.end() ) { return programs[found->second].get(); }
    if ( RecentlyRejected( key ) ) { return NULL; }
    oq4material::ARBTranslateRequest request;
    request.vertexName = vertexName.empty() ? "" : "glprogs/" + vertexName;
    request.fragmentName = fragmentName.empty() ? "" : "glprogs/" + fragmentName;
    const bool vertexRead = ReadARBSource( vertexFile, request.vertexSource );
    const bool fragmentRead = ReadARBSource( fragmentFile, request.fragmentSource );
    const bool sourcesFound = vertexRead && fragmentRead;
    // Reuse precedes the capacity check, as for authored GLSL.
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { continue; }
        Program &old = *programs[i];
        if ( old.key == key && old.sourcePairFound == sourcesFound
            && old.vertexSource == request.vertexSource && old.fragmentSource == request.fragmentSource ) {
            currentPrograms[key] = static_cast<int>( i );
            if ( !old.valid ) { common->Warning( "Vulkan authored ARB '%s': %s", old.name.c_str(), old.compiled.diagnostic.c_str() ); }
            return &old;
        }
    }
    const std::string name = vertexName.empty() ? fragmentName
        : fragmentName.empty() || vertexName == fragmentName ? vertexName : vertexName + " + " + fragmentName;
    const int slot = AcquireSlot();
    if ( slot < 0 ) { Reject( key, "ARB", name ); return NULL; }
    auto program = std::make_unique<Program>();
    program->arb = true;
    program->name = name;
    program->key = key;
    program->sourcePairFound = sourcesFound;
    program->vertexSource = request.vertexSource;
    program->fragmentSource = request.fragmentSource;
    if ( !sourcesFound ) {
        program->compiled.diagnostic = !vertexRead ? "glprogs/" + vertexName + ": file not found"
            : "glprogs/" + fragmentName + ": file not found";
    } else if ( !ReserveCompilerStorage( request.vertexSource.size() + request.fragmentSource.size() ) ) {
        program->compiled.diagnostic = "material compiler storage exhausted";
    } else {
        oq4material::ARBTranslation translation;
        if ( !oq4material::TranslateARB( request, translation ) ) {
            program->compiled.diagnostic = translation.diagnostic;
        } else {
            program->valid = oq4material::CompileGLSL( translation.request, program->compiled );
            program->arbParameters = translation.parameters;
        }
    }
    program->bytes = StorageBytes( *program );
    Program *installed = Install( std::move( program ), slot );
    if ( !installed->valid ) {
        common->Warning( "Vulkan authored ARB '%s': %s", installed->name.c_str(), installed->compiled.diagnostic.c_str() );
    } else {
        common->Printf( "Vulkan: translated authored ARB '%s'\n", installed->name.c_str() );
    }
    return installed;
}

bool CreateLayouts() {
    if ( pipelineLayout != VK_NULL_HANDLE ) { return true; }
    VkDescriptorSetLayoutBinding binding = {};
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1; info.pBindings = &binding;
    if ( uniformLayout == VK_NULL_HANDLE && vkCreateDescriptorSetLayout( vkCtx.device, &info, NULL, &uniformLayout ) != VK_SUCCESS ) { return false; }
    VkDescriptorSetLayoutBinding textures[oq4material::MaxTextures] = {};
    for ( int i = 0; i < oq4material::MaxTextures; ++i ) {
        textures[i].binding = i; textures[i].descriptorCount = 1;
        textures[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textures[i].stageFlags = binding.stageFlags;
    }
    info.bindingCount = oq4material::MaxTextures; info.pBindings = textures;
    if ( textureLayout == VK_NULL_HANDLE && vkCreateDescriptorSetLayout( vkCtx.device, &info, NULL, &textureLayout ) != VK_SUCCESS ) { return false; }
    const VkDescriptorSetLayout layouts[] = {uniformLayout, textureLayout};
    VkPipelineLayoutCreateInfo pipeline = {};
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 2; pipeline.pSetLayouts = layouts;
    return vkCreatePipelineLayout( vkCtx.device, &pipeline, NULL, &pipelineLayout ) == VK_SUCCESS;
}

bool CreateModule( const std::vector<uint32_t> &code, VkShaderModule &module ) {
    if ( module != VK_NULL_HANDLE ) { return true; }
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof( uint32_t ); info.pCode = code.data();
    return vkCreateShaderModule( vkCtx.device, &info, NULL, &module ) == VK_SUCCESS;
}

// The pool this slot's next draw allocates from, chaining a new one when the
// current pool is full; NULL past MaxPoolsPerSlot.
VkDescriptorPool DrawPool( int slot ) {
    FramePools &frame = framePools[slot];
    const bool drill = frame.poolDraws < MaxDrawsPerPool;
    // The drill keeps allocating from its single full pool, so the refusal
    // comes from the driver rather than from this quota.
    const int index = drill ? 0 : frame.draws / MaxDrawsPerPool;
    if ( index >= MaxPoolsPerSlot ) { return VK_NULL_HANDLE; }
    if ( index >= frame.created ) {
        const uint32_t draws = static_cast<uint32_t>( frame.poolDraws );
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, draws},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, draws * oq4material::MaxTextures}};
        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = draws * 2; info.poolSizeCount = 2; info.pPoolSizes = sizes;
        if ( vkCreateDescriptorPool( vkCtx.device, &info, NULL, &frame.pools[index] ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
        frame.created = index + 1;
    }
    return frame.pools[index];
}
} // namespace

vkGLSLProgramFamily_t VK_MaterialPrograms_NativeFamily( const char *program ) {
    std::lock_guard<std::mutex> lock( nativeFamilyMutex );
    if ( program == NULL ) { return VK_GLSL_PROGRAM_FAMILY_UNKNOWN; }
    const auto cached = nativeFamilies.find( program );
    if ( cached != nativeFamilies.end() ) { return cached->second; }
    idStr canonical = program;
    canonical.BackSlashesToSlashes(); canonical.ToLower();
    if ( canonical.Cmpn( "glprogs/", 8 ) == 0 ) { canonical = canonical.Mid( 8, canonical.Length() - 8 ); }
    vkGLSLProgramFamily_t candidate = VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    for ( const auto &signature : vkNativeMaterialSources ) {
        if ( canonical == signature.program ) { candidate = signature.family; break; }
    }
    if ( candidate == VK_GLSL_PROGRAM_FAMILY_UNKNOWN ) { return candidate; }
    if ( nativeFamilies.size() >= MaxPrograms ) {
        if ( Report( "native", "capacity" ) ) {
            common->Warning( "Vulkan: native material source cache exhausted; '%s' is treated as authored", program );
        }
        return VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    }
    std::string vertex, fragment;
    int vertexBytes = 0, fragmentBytes = 0;
    bool anySource = false;
    idStr vertexPath, fragmentPath;
    const bool pair = R_FindGLSLSourcePair( program, vertexPath, fragmentPath,
        vertex, fragment, &vertexBytes, &fragmentBytes, &anySource );
    vkGLSLProgramFamily_t selected = VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    if ( pair ) {
        const std::string vertexHash = SourceDigest( vertex.data(), vertexBytes );
        const std::string fragmentHash = SourceDigest( fragment.data(), fragmentBytes );
        for ( const auto &signature : vkNativeMaterialSources ) {
            if ( canonical == signature.program && !vertexHash.empty() && !fragmentHash.empty()
                && vertexHash == signature.vertexSHA256 && fragmentHash == signature.fragmentSHA256 ) {
                selected = signature.family; break;
            }
        }
    } else if ( !anySource ) {
        // Some retail guide families ship without GLSL files. Keep their
        // built-in compatibility implementation only for the canonical name
        // with no authored source at all; a partial override must fail openly.
        selected = candidate;
    }
    nativeFamilies.emplace( program, selected );
    common->Printf( "Vulkan: GLSL source selection '%s': %s\n", program,
        selected == VK_GLSL_PROGRAM_FAMILY_UNKNOWN ? "authored source" : pair ? "verified native source" : "built-in default (no source)" );
    return selected;
}

bool VK_MaterialPrograms_Validate( const newShaderStage_t *stage ) {
    Program *program = Find( stage );
    return program != NULL && program->valid;
}

bool VK_MaterialPrograms_NeedsStencil( const idMaterial *material, const float *registers ) {
    if ( material == NULL || !material->HasActiveCustomGLSLLighting( registers ) ) { return false; }
    for ( int i = 0; i < material->GetNumStages(); ++i ) {
        const shaderStage_t *stage = material->GetStage( i );
        const newShaderStage_t *program = stage->newStage;
        if ( program == NULL || !program->glslProgram || !program->customLighting
            || ( registers != NULL && registers[stage->conditionRegister] == 0.0f ) ) { continue; }
        const vkGLSLProgramFamily_t family = VK_MaterialPrograms_NativeFamily( program->glslProgramName );
        if ( family != VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT && family != VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP ) { return true; }
    }
    return false;
}

int VK_MaterialPrograms_NeedsStencilGeneration() {
    std::lock_guard<std::mutex> lock( nativeFamilyMutex );
    return nativeFamilyGeneration;
}

bool VK_MaterialPrograms_ReportOnce( const char *subject, const char *reason ) {
    return Report( subject != NULL ? subject : "", reason != NULL ? reason : "" );
}

void VK_MaterialPrograms_Reload() {
    R_RenderThread_Sync( "material program reload" );
    currentPrograms.clear();
    rejectedKeys.clear();
    {
        std::lock_guard<std::mutex> lock( nativeFamilyMutex );
        nativeFamilies.clear();
        ++nativeFamilyGeneration;
    }
    arbTranslatable.clear();
    diagnostics.clear();
    suppressedDiagnostics = 0;
    capacityRejections = 0;
    for ( auto &program : programs ) {
        if ( program == nullptr ) { continue; }
        program->drawn = program->skipped = 0;
        program->lastSkip.clear();
    }
    common->Printf( "Vulkan: authored GLSL sources will be reloaded on use\n" );
}

void VK_MaterialPrograms_Report( bool detailed ) {
    int live = 0, current = 0, invalid = 0;
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { continue; }
        ++live;
        current += IsCurrent( static_cast<int>( i ) ) ? 1 : 0;
        invalid += programs[i]->valid ? 0 : 1;
    }
    common->Printf( "Vulkan authored GLSL/ARB programs: %d of %d versions (%d current, %d invalid, %d evicted), %zu of %zu compiler bytes\n",
        live, MaxPrograms, current, invalid, evictions, compilerBytes, MaxCompilerBytes );
    common->Printf( "  since reload: %d capacity rejections, %d suppressed diagnostics; peak %d of %d authored draws per frame\n",
        capacityRejections, suppressedDiagnostics, peakDraws, MaxDrawsPerPool * MaxPoolsPerSlot );
    if ( !detailed ) { return; }
    for ( size_t i = 0; i < programs.size(); ++i ) {
        if ( programs[i] == nullptr ) { continue; }
        const Program &program = *programs[i];
        common->Printf( "  %s %s %s%s drawn=%d skipped=%d%s%s\n", program.valid ? "compiled" : "invalid", Kind( program ),
            program.name.c_str(), IsCurrent( static_cast<int>( i ) ) ? "" : " (stale)", program.drawn, program.skipped,
            program.lastSkip.empty() ? "" : " last skip: ", program.lastSkip.c_str() );
        if ( !program.valid && !program.compiled.diagnostic.empty() ) {
            common->Printf( "    reason: %s\n", program.compiled.diagnostic.c_str() );
        }
    }
}

void VK_MaterialPrograms_BeginFrame( int slot ) {
    // The slot's fence has just been waited: everything it last recorded,
    // and every frame before it, has retired.
    ++frameSerial;
    FramePools &frame = framePools[slot];
    peakDraws = Max( peakDraws, frame.draws );
    frame.draws = 0;
    frame.ready = true;
    // Entering or leaving the descriptor drill replaces this slot's pools;
    // nothing in flight references them once its fence has been waited.
    const int test = r_vkMaterialDescriptorPoolTest.GetInteger();
    const int poolDraws = test > 0 && test < MaxDrawsPerPool ? test : MaxDrawsPerPool;
    if ( poolDraws != frame.poolDraws ) {
        for ( int i = 0; i < frame.created; ++i ) { vkDestroyDescriptorPool( vkCtx.device, frame.pools[i], NULL ); frame.pools[i] = VK_NULL_HANDLE; }
        frame.created = 0;
        frame.poolDraws = poolDraws;
        common->Printf( "Vulkan: authored descriptor pools for frame slot %d now hold %d draws%s\n", slot, poolDraws,
            poolDraws < MaxDrawsPerPool ? " (r_vkMaterialDescriptorPoolTest, unchained)" : "" );
    }
    for ( int i = 0; i < frame.created; ++i ) {
        if ( vkResetDescriptorPool( vkCtx.device, frame.pools[i], 0 ) != VK_SUCCESS ) { frame.ready = false; }
    }
}

namespace {
// Records the program's images and draw state into the active command buffer.
// images[i] is consulted only for slots the compiled program samples.
bool BindProgram( Program *program, idImage *const images[oq4material::MaxTextures],
        const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor,
        const drawInteraction_t *interaction ) {
    if ( program == NULL || !program->valid ) { return false; }
    program->lastUseFrame = frameSerial;
    if ( !VK_Exec_MainRenderingScopeOpen() ) { return Fail( *program, "no open rendering scope" ); }
    if ( !CreateLayouts() || !CreateModule( program->compiled.vertex, program->vertex )
        || !CreateModule( program->compiled.fragment, program->fragment ) ) { return Fail( *program, "shader resource allocation failed" ); }
    const int slot = VK_Exec_ActiveFrameSlot();
    if ( !framePools[slot].ready ) { return Fail( *program, "frame descriptor pool reset failed" ); }
    const VkDescriptorPool pool = DrawPool( slot );
    if ( pool == VK_NULL_HANDLE ) { return Fail( *program, "frame descriptor capacity exhausted" ); }
    VkDescriptorImageInfo descriptors[oq4material::MaxTextures] = {};
    // Images Vulkan rendered keep top-down rows; the compiled program flips
    // its 2D coordinates for those slots (GLSLCompiler.cpp FlipHelpers).
    unsigned flipMask = 0;
    for ( int i = 0; i < oq4material::MaxTextures; ++i ) {
        const bool used = (program->compiled.textureMask & (1u << i)) != 0;
        idImage *image = used ? images[i] : globalImages->whiteImage;
        if ( image == NULL ) { return Fail( *program, "missing shader texture" ); }
        const bool cube = (program->compiled.cubeTextureMask & (1u << i)) != 0;
        if ( VK_Exec_ImageDescriptor( image->GetDeviceHandle(), !cube ) == VK_NULL_HANDLE ) { return Fail( *program, "shader texture is not resident" ); }
        const vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
        if ( entry == NULL || entry->isCube != cube || entry->samples != VK_SAMPLE_COUNT_1_BIT ) { return Fail( *program, "shader texture type/sample mismatch" ); }
        if ( used && !cube && entry->materialSampleFlipY ) { flipMask |= 1u << i; }
        descriptors[i].sampler = entry->sampler; descriptors[i].imageView = entry->view; descriptors[i].imageLayout = entry->layout;
    }
    oq4material::UniformBlock block = uniforms;
    block.textureOrientation[0] = static_cast<float>( flipMask );
    block.textureOrientation[1] = vkCtx.depthClipNegativeOneToOne ? 1.0f : 0.0f;
    VkPipeline pipeline = VK_Exec_AuthoredMaterialPipeline( program->identity,
        program->vertex, program->fragment, pipelineLayout, stateBits, separateColor, program->compiled.vertexInputMask );
    if ( pipeline == VK_NULL_HANDLE && ReclaimPipelineCapacity() ) {
        pipeline = VK_Exec_AuthoredMaterialPipeline( program->identity,
            program->vertex, program->fragment, pipelineLayout, stateBits, separateColor, program->compiled.vertexInputMask );
    }
    if ( pipeline == VK_NULL_HANDLE ) { return Fail( *program, "material pipeline allocation failed" ); }
    VkDescriptorBufferInfo buffer = {};
    const int uniformCheckpoint = VK_Exec_InteractionUniformCheckpoint();
    if ( !VK_Exec_AuthoredUniformAlloc( &block, sizeof( block ), buffer ) ) { return Fail( *program, "material uniform ring exhausted" ); }
    const VkDescriptorSetLayout layouts[] = {uniformLayout, textureLayout};
    VkDescriptorSet sets[2] = {};
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool; allocate.descriptorSetCount = 2; allocate.pSetLayouts = layouts;
    if ( vkAllocateDescriptorSets( vkCtx.device, &allocate, sets ) != VK_SUCCESS ) {
        // No descriptor or command references this reservation. Repeated
        // refusals must leave the remaining ring available to later draws.
        VK_Exec_InteractionUniformRestore( uniformCheckpoint );
        return Fail( *program, "material descriptor allocation failed" );
    }
    ++framePools[slot].draws;
    VkWriteDescriptorSet writes[1 + oq4material::MaxTextures] = {};
    for ( int i = 0; i <= oq4material::MaxTextures; ++i ) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = sets[i == 0 ? 0 : 1]; writes[i].dstBinding = i == 0 ? 0 : i - 1;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if ( i == 0 ) { writes[i].pBufferInfo = &buffer; } else { writes[i].pImageInfo = &descriptors[i - 1]; }
    }
    vkUpdateDescriptorSets( vkCtx.device, 1 + oq4material::MaxTextures, writes, 0, NULL );
    const VkCommandBuffer cmd = VK_Exec_ActiveCmd();
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0, NULL );
    ++program->drawn;
    if ( !program->loggedDraw ) {
        common->Printf( "Vulkan: drawing authored %s '%s'\n", Kind( *program ), program->name.c_str() );
        program->loggedDraw = true;
    }
    if ( interaction != NULL && !program->loggedLightingDraw ) {
        common->Printf( "Vulkan: drawing authored %s lighting '%s'\n", Kind( *program ), program->name.c_str() );
        program->loggedLightingDraw = true;
    }
    return true;
}
} // namespace

bool VK_MaterialPrograms_Bind( const newShaderStage_t *stage,
        const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor,
        const drawInteraction_t *interaction ) {
    Program *program = Find( stage );
    if ( program == NULL || !program->valid ) { return false; }
    idImage *images[oq4material::MaxTextures] = {};
    for ( int i = 0; i < oq4material::MaxTextures; ++i ) {
        if ( (program->compiled.textureMask & (1u << i)) == 0 ) { continue; }
        images[i] = RB_ResolveGLSLShaderTextureImage( stage, i, interaction );
        if ( images[i] != NULL ) { images[i]->SetSamplerState( stage->shaderTextureFilters[i], stage->shaderTextureRepeats[i] ); }
    }
    return BindProgram( program, images, uniforms, stateBits, separateColor, interaction );
}

const std::vector<oq4material::ARBParameter> *VK_MaterialPrograms_ARBParameters(
        const char *vertexFile, const char *fragmentFile ) {
    Program *program = FindARB( vertexFile, fragmentFile );
    return program != NULL && program->valid ? &program->arbParameters : NULL;
}

bool VK_MaterialPrograms_BindARB( const char *vertexFile, const char *fragmentFile,
        const newShaderStage_t *stage, const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor ) {
    Program *program = FindARB( vertexFile, fragmentFile );
    if ( program == NULL || !program->valid || stage == NULL ) { return false; }
    idImage *images[oq4material::MaxTextures] = {};
    for ( int unit = 0; unit < oq4material::MaxTextures; ++unit ) {
        if ( (program->compiled.textureMask & (1u << unit)) == 0 ) { continue; }
        // fragmentMap images keep their own sampler state, as glBind does.
        // Light images only exist in an interaction; an ambient stage that
        // names one samples white instead of whatever GL left bound.
        if ( unit < stage->numFragmentProgramImages && stage->fragmentProgramImages[unit] != NULL ) {
            images[unit] = stage->fragmentProgramImages[unit];
        } else if ( unit < MAX_FRAGMENT_IMAGES && ( stage->fragmentProgramBindings[unit] == LEGACY_FRAGMENT_BINDING_LIGHT_FALLOFF
            || stage->fragmentProgramBindings[unit] == LEGACY_FRAGMENT_BINDING_LIGHT_IMAGE ) ) {
            images[unit] = globalImages->whiteImage;
        } else {
            program->lastUseFrame = frameSerial;
            return Fail( *program, "fragment program samples a texture unit the material has no fragmentMap for" );
        }
    }
    return BindProgram( program, images, uniforms, stateBits, separateColor, NULL );
}

bool VK_MaterialPrograms_ARBTranslatable( unsigned int target, const char *file ) {
    if ( file == NULL || file[0] == '\0' ) { return false; }
    const std::string key = std::string( target == GL_VERTEX_PROGRAM_ARB ? "v|" : "f|" ) + file;
    const auto cached = arbTranslatable.find( key );
    if ( cached != arbTranslatable.end() ) { return cached->second; }
    oq4material::ARBTranslateRequest request;
    std::string source;
    bool translatable = ReadARBSource( file, source );
    if ( translatable ) {
        if ( target == GL_VERTEX_PROGRAM_ARB ) { request.vertexName = file; request.vertexSource = source; }
        else { request.fragmentName = file; request.fragmentSource = source; }
        oq4material::ARBTranslation translation;
        translatable = oq4material::TranslateARB( request, translation );
    }
    if ( arbTranslatable.size() < MaxPrograms ) { arbTranslatable.emplace( key, translatable ); }
    return translatable;
}

void VK_MaterialPrograms_Shutdown() {
    for ( auto &program : programs ) {
        if ( program == nullptr ) { continue; }
        if ( program->vertex != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program->vertex, NULL ); }
        if ( program->fragment != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program->fragment, NULL ); }
    }
    for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; ++i ) {
        FramePools &frame = framePools[i];
        for ( int pool = 0; pool < frame.created; ++pool ) { vkDestroyDescriptorPool( vkCtx.device, frame.pools[pool], NULL ); }
        frame = FramePools();
    }
    if ( pipelineLayout != VK_NULL_HANDLE ) { vkDestroyPipelineLayout( vkCtx.device, pipelineLayout, NULL ); }
    if ( uniformLayout != VK_NULL_HANDLE ) { vkDestroyDescriptorSetLayout( vkCtx.device, uniformLayout, NULL ); }
    if ( textureLayout != VK_NULL_HANDLE ) { vkDestroyDescriptorSetLayout( vkCtx.device, textureLayout, NULL ); }
    pipelineLayout = VK_NULL_HANDLE; uniformLayout = textureLayout = VK_NULL_HANDLE;
    programs.clear(); currentPrograms.clear(); rejectedKeys.clear(); arbTranslatable.clear();
    {
        std::lock_guard<std::mutex> lock( nativeFamilyMutex );
        nativeFamilies.clear();
        ++nativeFamilyGeneration;
    }
    diagnostics.clear(); suppressedDiagnostics = capacityRejections = evictions = peakDraws = 0;
    compilerBytes = 0; frameSerial = 1;
}
#endif
