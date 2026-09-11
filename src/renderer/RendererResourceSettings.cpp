// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#pragma hdrstop
#include "tr_local.h"
#include "RendererResourceSettings.h"
#include "RendererConsumedPolicy.h"
#include "RendererImageRecovery.h"
#include "../idlib/CryptoHash.h"
#include "DisplayPresentation.h"
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "Vulkan/VulkanDevice.h"
#endif
#include <array>
#include <atomic>
#include <mutex>
#include <memory>
#include <thread>
#include <map>
#include <set>
#include <vector>
#include <string>

extern idCVar image_usePrecompressedTextures;
extern idCVar image_downSize, image_downSizeLimit, image_downSizeSpecular, image_downSizeSpecularLimit;
extern idCVar image_downSizeBump, image_downSizeBumpLimit;

namespace {
constexpr size_t MaxImages = 32768;
constexpr size_t MaxMaterials = 32768;
constexpr size_t MaxMaterialBytes = 256 * 1024;
constexpr size_t MaxSourceBytes = 32 * 1024 * 1024;
constexpr const char* DependencyNames[] = {
    "image_picmip", "image_picmipFilter", "image_picmipMinSize", "image_filter", "image_anisotropy",
    "image_useETC2", "image_highQualityCompression", "image_writeGeneratedImages",
    "com_productionMode", "com_makingBuild", "com_SingleDeclFile"
};
constexpr size_t NumDependencies = sizeof(DependencyNames) / sizeof(DependencyNames[0]);


struct ImageProof {
    uint64_t identity = 0;
    bool baselineDefault = false, resident = false;
    bool allocated = false;
    uint64_t generation = 0, storage = 0;
    std::array<uint32_t, 6> mips{};
};
struct MaterialSource {
    const idMaterial* material = nullptr;
    uint64_t identity = 0;
    int index = 0, line = 0;
    bool baselineDefault = false, reparse = false;
    // MSVC debug strings allocate iterator proxies even in their noexcept
    // default/move constructors. Use throwing construction/copy paths so this
    // checked operation can report allocation failure instead of terminating.
    std::string name{""}, file{""}, text{""};
    MaterialSource() = default;
    MaterialSource(const MaterialSource&) = default;
    MaterialSource& operator=(const MaterialSource&) = default;
};
struct DefaultMaterialProof {
    uint64_t identity;
    int index;
    std::string name;
    DefaultMaterialProof(uint64_t identity, int index, const char* name) : identity(identity), index(index), name(name) {}
    DefaultMaterialProof(const DefaultMaterialProof&) = default;
    DefaultMaterialProof& operator=(const DefaultMaterialProof&) = default;
};
struct Baseline {
    uint64_t mutationEpoch = 0;
    std::map<const idMaterial*, DefaultMaterialProof> defaultMaterials;
    std::map<const idImage*, ImageProof> images;
    std::vector<MaterialSource> materials = std::vector<MaterialSource>(size_t{0});
};
struct DependencyText { std::string value{""}; };
struct Prepared;
struct Attempt {
    renderImagePolicyRequest_t request{};
    renderImagePolicyResult_t result{};
    uint64_t initialGeneration = 0, initialFailures = 0;
    uint64_t mutationEpoch = 0;
    bool recording = false, finished = false, mutated = false, published = false;
    bool preparing = false;
    Prepared* prepared = nullptr;
    std::map<const idImage*,std::unique_ptr<idBinaryImage>> cpu;
    std::map<const idImage*,const openq4::imageRecovery::Image*> selectedContent;
    std::set<const idImage*> borrowed;
    std::shared_ptr<const Baseline> original;
    std::array<const idCVar*, NumDependencies> dependencies{};
    std::array<DependencyText, NumDependencies> dependencyText{};
    bool dependenciesReady = false;
    char failure[256]{};
    // Ordered containers also avoid MSVC hash-table internals that allocate
    // debug proxies through noexcept constructors. Inventories remain bounded.
    std::map<const idImage*, ImageProof> images;
    std::vector<MaterialSource> materials = std::vector<MaterialSource>(size_t{0});
};
struct Prepared {
    renderImageRecoveryLease_t lease{};
    uint64_t epoch=0,observationEpoch=0;
    std::shared_ptr<const Baseline> original;
    renderImagePolicy_t initialPolicy{};
    renderImagePolicyResult_t completed{};
    uint64_t completedFailures=0;
    uint32_t completedDirection=0;
    bool touched=false;
    std::unique_ptr<openq4::imageRecovery::Data> directions[2];
    std::map<const idImage*,std::unique_ptr<idBinaryImage>> cpu[2];
    std::map<const idImage*,const openq4::imageRecovery::Image*> selected[2];
    std::string raw[2]={std::string(""),std::string("")};
};

// No mutex is held across engine/native callbacks. The only shared pointer is
// accessed while locked; foreign threads never dereference it or any resource.
std::mutex policyMutex;
Attempt* active = nullptr;
const renderImageOwnerShutdown_t* fullOwnerShutdown = nullptr;
std::thread::id rendererThread;
uint64_t nextAttempt = 0;
std::shared_ptr<const Baseline> recovery;
std::unique_ptr<Prepared> preparedRecovery;
uint64_t nextPreparation=0;
constinit std::atomic<bool> recoveryInvalidated{false};
// These primitives are safe during resource construction/static destruction;
// they access neither policyMutex nor any renderer/engine object.
constinit std::atomic<uint64_t> resourceIdentityCounter{0};
constinit std::atomic<uint64_t> resourceMutationEpoch{1};
void AdvanceMutationEpoch() noexcept {
    uint64_t old = resourceMutationEpoch.load(std::memory_order_relaxed);
    while (old && !resourceMutationEpoch.compare_exchange_weak(old, old == UINT64_MAX ? 0 : old + 1,
        std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

bool Error(char* error, int size, const char* reason) {
    if (error && size > 0) idStr::Copynz(error, reason, size);
    return false;
}
void FailLocked(const char* reason, int32_t nativeError = 0) {
    if (!active || active->failure[0]) return;
    if (nativeError) idStr::snPrintf(active->failure, sizeof(active->failure), "%s (native %d)", reason, nativeError);
    else idStr::Copynz(active->failure, reason, sizeof(active->failure));
}
renderImagePolicy_t ReadPolicy() {
    return {image_downSize.GetInteger(), image_downSizeLimit.GetInteger(),
        image_downSizeSpecular.GetInteger(), image_downSizeSpecularLimit.GetInteger(),
        image_downSizeBump.GetInteger(), image_downSizeBumpLimit.GetInteger(),
        image_usePrecompressedTextures.GetInteger(), image_ignoreHighQuality.GetInteger()};
}
bool SamePolicy(const renderImagePolicy_t& a, const renderImagePolicy_t& b) {
    return a.downSize == b.downSize && a.downSizeLimit == b.downSizeLimit &&
        a.downSizeSpecular == b.downSizeSpecular && a.downSizeSpecularLimit == b.downSizeSpecularLimit &&
        a.downSizeBump == b.downSizeBump && a.downSizeBumpLimit == b.downSizeBumpLimit &&
        a.usePrecompressedTextures == b.usePrecompressedTextures && a.ignoreHighQuality == b.ignoreHighQuality;
}
bool ValidPolicy(const renderImagePolicy_t& p) {
    return (p.downSize == 0 || p.downSize == 1) && (p.downSizeSpecular == 0 || p.downSizeSpecular == 1) &&
        (p.downSizeBump == 0 || p.downSizeBump == 1) && (p.ignoreHighQuality == 0 || p.ignoreHighQuality == 1) &&
        p.downSizeLimit >= 0 && p.downSizeLimit <= 32768 && p.downSizeSpecularLimit >= 0 && p.downSizeSpecularLimit <= 32768 &&
        p.downSizeBumpLimit >= 0 && p.downSizeBumpLimit <= 32768 && p.usePrecompressedTextures >= 0 && p.usePrecompressedTextures <= 2;
}
Attempt* Current() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (active && std::this_thread::get_id() != rendererThread) {
        FailLocked("Image policy work crossed the renderer thread");
        return nullptr;
    }
    return active; // Owner-thread only; its stack scope cannot end concurrently.
}
bool Check(Attempt& a, char* error, int errorSize, bool device = false) {
    const bool policyMatches = SamePolicy(a.request.expectedCurrent, ReadPolicy());
    renderDisplayPresentation_t current{};
    R_GetDisplayPresentation(&current);
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (!a.mutationEpoch || a.mutationEpoch != resourceMutationEpoch.load(std::memory_order_relaxed)) {
        if (a.original) recoveryInvalidated = true;
        FailLocked("Resource lifecycle or unowned content changed during checked work");
    }
    if (a.dependenciesReady) for (size_t i = 0; i < NumDependencies; ++i)
        if (a.dependencyText[i].value != a.dependencies[i]->GetString()) FailLocked("An inherited image-loader dependency changed during restart");
    if (active != &a || !policyMatches) FailLocked("Image policy changed during checked resource work");
    if (device && (!current.available || current.generation != a.result.deviceGeneration ||
        current.failureSequence != a.initialFailures)) FailLocked("Renderer lifetime or outcome changed during image policy work");
    return a.failure[0] ? Error(error, errorSize, a.failure) : true;
}
struct SourcePolicy { bool quality = false, program = false; };
SourcePolicy ClassifySource(const std::string& text) {
    SourcePolicy result;
    // Conservative authored-token scan: comments are ignored, while quoted or
    // path-like tokens may refuse an ambiguous custom-program source. This is
    // a capability preflight, never a substitute material parser.
    const auto consider = [&](std::string token) {
        for (char& c : token) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        result.quality |= token == "highquality" || token == "uncompressed";
        result.program |= token == "program" || token == "vertexprogram" || token == "fragmentprogram" ||
            token == "fp20program" || token == "glslprogram";
    };
    for (size_t i = 0; i < text.size();) {
        // The material lexer permits backslash string concatenation. Without
        // interpreting that grammar here, refuse its ambiguous reconstruction.
        if (text[i] == '\\') return {true, true};
        if (text[i] == '"' || text[i] == '\'') {
            const char quote = text[i++]; const size_t start = i;
            while (i < text.size() && text[i] != quote) { if (text[i] == '\\') return {true, true}; ++i; }
            if (i == text.size()) return {true, true};
            consider(text.substr(start, i - start)); ++i; continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            i += 2; while (i < text.size() && text[i] != '\n') ++i; continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const size_t end = text.find("*/", i + 2); if (end == std::string::npos) return {true, true};
            i = end + 2; continue;
        }
        const auto word = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
        if (!word(text[i])) { ++i; continue; }
        const size_t start = i; while (i < text.size() && word(text[i])) ++i;
        consider(text.substr(start, i - start));
    }
    return result;
}
bool SourceMatches(const MaterialSource& source) {
    if (source.index < 0 || source.index >= declManager->GetNumDecls(DECL_MATERIAL)) return false;
    const idDecl* current = declManager->DeclByIndex(DECL_MATERIAL, source.index, false);
    if (current != source.material || !source.identity || source.material->GetImagePolicyIdentity() != source.identity || current->Index() != source.index || current->GetLineNum() != source.line ||
        source.name != current->GetName() || source.file != current->GetFileName() ||
        current->GetTextLength() != static_cast<int>(source.text.size())) return false;
    std::string text(source.text.size() + 1, '\0');
    current->GetText(text.data());
    return text.back() == '\0' && text.compare(0, source.text.size(), source.text) == 0;
}
bool Preflight(Attempt& a, char* error, int errorSize) {
    if (!globalImages || !declManager || globalImages->insideLevelLoad || globalImages->preloadingMapImages)
        return Error(error, errorSize, "Image policy restart requires idle image/declaration loading");
    for (size_t i = 0; i < NumDependencies; ++i) {
        a.dependencies[i] = cvarSystem->Find(DependencyNames[i]);
        if (!a.dependencies[i]) return Error(error, errorSize, "An image-loader dependency is unregistered");
        const char* text = a.dependencies[i]->GetString();
        size_t length = 0; while (length <= 4096 && text[length]) ++length;
        if (length > 4096) return Error(error, errorSize, "An image-loader dependency exceeds its text budget");
        a.dependencyText[i].value.assign(text, length);
    }
    a.dependenciesReady = true;
    if (cvarSystem->GetCVarBool("com_SingleDeclFile"))
        return Error(error, errorSize, "Checked material reparse requires retained declarations outside packed-decl mode");
    if (!Check(a, error, errorSize)) return false;
    const int imageCount = globalImages->images.Num();
    const int materialCount = declManager->GetNumDecls(DECL_MATERIAL);
    if (imageCount < 0 || static_cast<size_t>(imageCount) > MaxImages || materialCount < 0 || static_cast<size_t>(materialCount) > MaxMaterials)
        return Error(error, errorSize, "Image policy resource inventory exceeds the checked budget");
    const auto previous = a.original ? a.original : recovery;
    for (int i = 0; i < imageCount; ++i) {
        const idImage* image = globalImages->images[i];
        if (!image || !a.images.emplace(image, ImageProof{}).second)
            return Error(error, errorSize, "Image policy inventory contains a missing or duplicate image");
        if (!image->GetImagePolicyIdentity()) return Error(error, errorSize, "Image instance identity is exhausted");
        a.images.at(image).identity = image->GetImagePolicyIdentity();
        if (previous) {
            const auto old = previous->images.find(image);
            if (old != previous->images.end()) {
                if (old->second.identity != image->GetImagePolicyIdentity()) {
                    recoveryInvalidated = true;
                    return Error(error, errorSize, "Original recovery image instance was replaced");
                }
                a.images.at(image) = old->second;
            }
        } else {
            a.images.at(image).baselineDefault = image->IsDefaulted();
            a.images.at(image).resident = image->IsLoaded();
        }
    }
    if (previous) {
        for (const auto& image : previous->images)
            if (!a.images.count(image.first)) { recoveryInvalidated = true; return Error(error, errorSize, "Original recovery image identity was lost"); }
        a.materials = previous->materials;
        for (const auto& source : a.materials)
            if (!SourceMatches(source)) { recoveryInvalidated = true; return Error(error, errorSize, "Original recovery material source changed"); }
        for (const auto& entry : previous->defaultMaterials) {
            const auto& proof = entry.second;
            const idDecl* current = proof.index >= 0 && proof.index < materialCount ? declManager->DeclByIndex(DECL_MATERIAL, proof.index, false) : nullptr;
            if (current != entry.first || !proof.identity || entry.first->GetImagePolicyIdentity() != proof.identity || proof.name != current->GetName()) {
                recoveryInvalidated = true;
                return Error(error, errorSize, "Original default material identity changed");
            }
        }
        a.original = previous;
    }
    size_t bytes = 0;
    for (const auto& source : a.materials) bytes += source.text.size();
    for (int i = 0; i < materialCount; ++i) {
        const auto* material = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, i, false));
        if (!material || material->GetState() == DS_UNPARSED || material->IsImplicit()) continue;
        if (previous && std::any_of(a.materials.begin(), a.materials.end(),
            [material](const MaterialSource& source) { return source.material == material; })) continue;
        const int length = material->GetTextLength();
        if (length <= 0 || static_cast<size_t>(length) > MaxMaterialBytes || static_cast<size_t>(length) > MaxSourceBytes - bytes)
            return Error(error, errorSize, "A parsed material has missing or over-budget retained source");
        MaterialSource source;
        source.identity = material->GetImagePolicyIdentity();
        if (!source.identity) return Error(error, errorSize, "Material instance identity is exhausted");
        source.material = material; source.index = i; source.line = material->GetLineNum();
        source.name = material->GetName(); source.file = material->GetFileName();
        // A target-only declaration introduced by a failed attempt is never
        // promoted into the original default census on restoration.
        source.baselineDefault = !previous && material->GetState() == DS_DEFAULTED;
        source.text.resize(static_cast<size_t>(length) + 1);
        material->GetText(source.text.data());
        if (source.text.back() != '\0' || source.text.find('\0') != static_cast<size_t>(length) ||
            idStr::Icmpn(source.text.c_str(), "{ STUB:", 7) == 0)
            return Error(error, errorSize, "A material has unresolved or malformed retained source");
        source.text.resize(length); bytes += length;
        const SourcePolicy sourcePolicy = ClassifySource(source.text);
        source.reparse = sourcePolicy.quality;
        if (source.reparse) {
            bool customProgram = sourcePolicy.program;
            for (int stage = 0; stage < material->GetNumStages(); ++stage) customProgram |= material->GetStage(stage)->newStage != nullptr;
            if (customProgram) return Error(error, errorSize, "A quality-sensitive material requires unsupported shader-program reconstruction");
        }

        a.materials.push_back(source);
        if (!Check(a, error, errorSize)) return false;
    }
    if (!a.original) {
        auto baseline = std::make_shared<Baseline>();
        baseline->mutationEpoch = a.mutationEpoch;
        for (int i = 0; i < materialCount; ++i) {
            const auto* material = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, i, false));
            if (material && material->GetState() == DS_DEFAULTED) {
                if (!material->GetImagePolicyIdentity()) return Error(error, errorSize, "Default material instance identity is exhausted");
                const char* name = material->GetName(); size_t length = 0;
                while (length <= 4096 && name[length]) ++length;
                if (length > 4096 || length > MaxSourceBytes - bytes) return Error(error, errorSize, "Default material names exceed the checked budget");
                bytes += length;
                const DefaultMaterialProof proof(material->GetImagePolicyIdentity(), i, name);
                baseline->defaultMaterials.emplace(material, proof);
            }
        }
        baseline->images = a.images; baseline->materials = a.materials;
        a.original = std::move(baseline);
    }
    renderDisplayPresentation_t current{}; R_GetDisplayPresentation(&current);
    // Failed teardown is explicitly restorable. It has no old GPU work to wait.
    if (!current.available && previous) return Check(a, error, errorSize);
#ifdef OPENQ4_RENDERER_VK_MODULE
    // Pending uploads from the old device must be observed before teardown;
    // a blocked/failed old upload cannot become successful by destroying it.
    VK_Device_FlushUploadBatch();
    VK_Device_WaitUploadBatch();
    if (vkCtx.presentationBlocked || vkCtx.uploadBatchOpen || vkCtx.uploadBatchInFlight ||
        vkCtx.numUploadBatchPending || vkCtx.numUploadBatchInFlight)
        return Error(error, errorSize, "Existing Vulkan image uploads did not complete");
#else
    if (!GLimp_EnsureActiveContext("checked image policy preflight")) return Error(error, errorSize, "Image policy requires the current GL context");
    glFinish(); GL_CheckErrors();
#endif
    return Check(a, error, errorSize);
}
bool VerifyImage(Attempt& a, const idImage* image, char* error, int errorSize) {
    if (!image) return true;
    bool registered = false;
    for (int i = 0; i < globalImages->images.Num(); ++i)
        if (globalImages->images[i] == image) { registered = true; break; }
    if (!registered) return Error(error, errorSize, "An image identity disappeared during resource work");
    auto it = a.images.find(image);
    if (it == a.images.end() || !it->second.identity || it->second.identity != image->GetImagePolicyIdentity() || !image->IsLoaded() || !it->second.allocated ||
        it->second.generation != a.result.deviceGeneration || it->second.storage != image->GetStorageGeneration())
        return Error(error, errorSize, "An image has no completed allocation in the new renderer device");
    if (image->IsDefaulted() && !it->second.baselineDefault)
        return Error(error, errorSize, "An image newly defaulted during checked resource reload");
    if (image->IsFileBacked()) {
        const idImageOpts& opts = image->GetOpts();
        if (opts.numLevels <= 0 || opts.numLevels > 31 || (opts.textureType != TT_2D && opts.textureType != TT_CUBIC))
            return Error(error, errorSize, "A file image has unsupported mip or layer metadata");
        const uint32_t expected = (uint32_t(1) << opts.numLevels) - 1;
        for (int side = 0; side < (opts.textureType == TT_CUBIC ? 6 : 1); ++side)
            if (it->second.mips[side] != expected)
                return Error(error, errorSize, "A file image has no complete upload for every mip and layer");
    }
    return true;
}
bool EnsureMaterialImage(Attempt& a, const idImage* image, char* error, int size) {
    if (!image) return true;
    bool registered = false;
    for (int i = 0; i < globalImages->images.Num(); ++i)
        if (globalImages->images[i] == image) { registered = true; break; }
    if (!registered) return Error(error, size, "A material references an unregistered image");
    // Reparse can introduce an image or retain a generated material dependency
    // that was absent from the original resident census. Resolve it before the
    // final submission/wait; never reuse IsLoaded as proof of an upload.
    if (!image->IsLoaded()) const_cast<idImage*>(image)->Reload(true);
    return Check(a, error, size, true) && VerifyImage(a, image, error, size);
}
bool VerifyMaterialImages(Attempt& a, const idMaterial& m, char* error, int size, bool ensure = false) {
    std::vector<const idImage*> references(size_t{0});
    const auto append = [&](const idImage* image) { if (image) references.push_back(image); };
    for (int i = 0; i < m.GetNumStages(); ++i) {
        const shaderStage_t& s = *m.GetStage(i);
        append(s.texture.image);
        if (s.newStage) {
            for (int j = 0; j < s.newStage->numFragmentProgramImages; ++j) append(s.newStage->fragmentProgramImages[j]);
            for (int j = 0; j < s.newStage->numShaderTextures; ++j) append(s.newStage->shaderTextureImages[j]);
        }
        if (references.size() > MaxImages) return Error(error, size, "Material image references exceed the checked budget");
    }
    const pbrMaterialInfo_t& p = m.GetPBRInfo();
    const pbrMaterialTexture_t* textures[] = {&p.albedo, &p.normal, &p.orm, &p.metallic, &p.roughness, &p.ao,
        &p.emissive, &p.legacyBump, &p.legacyDiffuse, &p.legacySpecular, &p.legacyEmissive};
    for (const auto* t : textures) if (t->present) append(t->image);
    append(m.GetPortalImage()); append(m.LightFalloffImage()); append(m.GetSpecularProbeInfo().cubeImage);
    for (const idImage* image : references)
        if (!(ensure ? EnsureMaterialImage(a, image, error, size) : VerifyImage(a, image, error, size))) return false;
    return true;
}

namespace ir = openq4::imageRecovery;
bool SameLease(const renderImageRecoveryLease_t& a,const renderImageRecoveryLease_t& b) {
    return a.owner&&a.request&&a.preparation&&a.owner==b.owner&&a.request==b.request&&a.preparation==b.preparation;
}
bool CaptureInventory(Attempt& a,ir::Data& data,bool content,char* error,int size) {
    if (!Check(a,error,size)) return false;
    data.policy=ReadPolicy();
    for(size_t n=0;n<NumDependencies;++n)data.dependencies[n].value=a.dependencyText[n].value;
    if(globalImages->images.Num()!=int(a.images.size()))return Error(error,size,"Image inventory changed during preparation");
    for(const auto& item:a.images){
        const idImage* image=item.first;
        if(!item.second.identity||image->GetImagePolicyIdentity()!=item.second.identity||image->IsDefaulted()||!image->IsFileBacked())
            return Error(error,size,"Portable image recovery does not yet reconstruct default, procedural, scratch or persistent cohorts");
        const auto p=image->GetDeclaredPolicy();ir::Image entry;entry.name=image->GetName();entry.filter=p.filter;entry.repeat=p.repeat;
        entry.usage=p.usage;entry.cube=p.cube;entry.flags=p.flags;entry.allowDownSize=p.allowDownSize;entry.resident=item.second.resident;
        if(content&&entry.resident&&!image->GetPortableContent(entry.content))return Error(error,size,"A resident image has no complete supported portable content observation");
        data.images.push_back(entry);
    }
    std::sort(data.images.begin(),data.images.end(),[](const ir::Image& x,const ir::Image& y){
        return std::tie(x.name,x.filter,x.repeat,x.usage,x.cube,x.flags,x.allowDownSize)<std::tie(y.name,y.filter,y.repeat,y.usage,y.cube,y.flags,y.allowDownSize);});
    const int count=declManager->GetNumDecls(DECL_MATERIAL);
    if(count<0||size_t(count)>MaxMaterials)return Error(error,size,"Material inventory exceeds recovery budget");
    for(int n=0;n<count;++n){
        const auto* material=static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL,n,false));
        if(!material||!material->GetImagePolicyIdentity()||material->GetState()==DS_DEFAULTED)return Error(error,size,"Default or missing material has no portable reconstruction");
        ir::Material m;m.name=material->GetName();m.file=material->GetFileName();m.line=material->GetLineNum();m.state=material->GetState();m.implicit=material->IsImplicit();
        if(m.state!=DS_UNPARSED){
            const auto found=std::find_if(a.materials.begin(),a.materials.end(),[&](const MaterialSource& s){return s.material==material;});
            materialConsumedPolicy_t consumed{};
            if(found==a.materials.end()||!SourceMatches(*found)||!material->GetConsumedPolicy(consumed))return Error(error,size,"A parsed material has no observed retained-source policy");
            m.observed=true;m.ignoreHighQuality=consumed.inputs.ignoreHighQuality;m.makingBuild=consumed.inputs.makingBuild;
            if(m.ignoreHighQuality!=(data.policy.ignoreHighQuality!=0)||m.makingBuild!=cvarSystem->GetCVarBool("com_makingBuild"))
                return Error(error,size,"A historical material policy requires unsupported explicit reconstruction");
            m.sourceBytes=uint32_t(found->text.size());idCrypto::SHA256(found->text.data(),found->text.size(),m.source.bytes);
        }
        data.materials.push_back(m);
        if(!Check(a,error,size))return false;
    }
    std::sort(data.materials.begin(),data.materials.end(),[](const ir::Material& x,const ir::Material& y){return x.name<y.name;});
    return Check(a,error,size);
}
bool InventoryMatches(Attempt& a,const ir::Data& saved,char* error,int size) {
    ir::Data current;
    if(!CaptureInventory(a,current,false,error,size)||current.images.size()!=saved.images.size()||current.materials.size()!=saved.materials.size())
        return Error(error,size,"Complete recovery inventory changed");
    for(size_t n=0;n<NumDependencies;++n)if(current.dependencies[n].value!=saved.dependencies[n].value)return Error(error,size,"Inherited recovery dependency changed");
    for(size_t n=0;n<saved.images.size();++n)if(!ir::SameImageKey(current.images[n],saved.images[n])||current.images[n].resident!=saved.images[n].resident)
        return Error(error,size,"Declared image cohort changed");
    for(size_t n=0;n<saved.materials.size();++n)if(!ir::SameMaterial(current.materials[n],saved.materials[n]))return Error(error,size,"Retained material source or consumed policy changed");
    return true;
}
bool StageImages(Attempt& a,const ir::Data& data,char* error,int size) {
    // CPU storage is independently bounded. The whole inventory is refused;
    // never truncate it to fit. A source read may temporarily own one extra file.
    constexpr uint64_t MaxStagedBytes=512ull*1024*1024,MaxSourceFileBytes=64ull*1024*1024;
    uint64_t bytes=0;
    struct Key {const idImage* image;ir::Image fields;};
    std::vector<Key> keys(size_t{0});keys.reserve(a.images.size());
    for(const auto& item:a.images){const auto p=item.first->GetDeclaredPolicy();Key key;key.image=item.first;key.fields.name=item.first->GetName();
        key.fields.filter=p.filter;key.fields.repeat=p.repeat;key.fields.usage=p.usage;key.fields.cube=p.cube;key.fields.flags=p.flags;key.fields.allowDownSize=p.allowDownSize;keys.push_back(key);}
    const auto less=[](const ir::Image& x,const ir::Image& y){return std::tie(x.name,x.filter,x.repeat,x.usage,x.cube,x.flags,x.allowDownSize)<std::tie(y.name,y.filter,y.repeat,y.usage,y.cube,y.flags,y.allowDownSize);};
    std::sort(keys.begin(),keys.end(),[&](const Key& x,const Key& y){return less(x.fields,y.fields);});
    a.selectedContent.clear();
    for(const auto& entry:data.images)if(entry.resident){
        if(entry.content.file.bytes>MaxSourceFileBytes||entry.content.binary.payloadBytes>MaxStagedBytes-bytes)return Error(error,size,"Prepared image CPU storage exceeds its bound");
        bytes+=entry.content.binary.payloadBytes;
        const auto found=std::lower_bound(keys.begin(),keys.end(),entry,[&](const Key& x,const ir::Image& y){return less(x.fields,y);});
        if(found==keys.end()||!ir::SameImageKey(found->fields,entry)||(found+1!=keys.end()&&ir::SameImageKey((found+1)->fields,entry)))return Error(error,size,"A recovery image is absent or ambiguous");
        const idImage* selected=found->image;
        auto binary=std::make_unique<idBinaryImage>(entry.name.c_str());
        if(!R_ReconstructImageContent(entry.content,*binary)||!Check(a,error,size))return Error(error,size,"Exact recovery image source or output changed");
        a.cpu.emplace(selected,std::move(binary));
        a.selectedContent.emplace(selected,&entry);
    }
    return InventoryMatches(a,data,error,size)&&Check(a,error,size);
}
bool PrepareTarget(Attempt& a,const ir::Data& restore,const renderImagePolicy_t& target,ir::Data& result,char* error,int size) {
    if(target.ignoreHighQuality!=restore.policy.ignoreHighQuality||target.usePrecompressedTextures!=restore.policy.usePrecompressedTextures)
        return Error(error,size,"Portable preparation cannot yet change material quality or source-selection policy");
    result=restore;result.direction=2;result.policy=target;
    imageDownsizeInputs_t inputs=R_ReadImageDownsizeInputs();
    inputs.downSize=target.downSize;inputs.downSizeLimit=target.downSizeLimit;inputs.downSizeSpecular=target.downSizeSpecular;
    inputs.downSizeSpecularLimit=target.downSizeSpecularLimit;inputs.downSizeBump=target.downSizeBump;inputs.downSizeBumpLimit=target.downSizeBumpLimit;
    for(auto& entry:result.images)if(entry.resident){
        auto& c=entry.content;
        if(c.scope==IPC_CACHE_PIXELS_ONLY){if(!SamePolicy(target,restore.policy))return Error(error,size,"Cache pixels cannot authorize a different source policy");continue;}
        R_ResolveImageDownsizePolicy(inputs,entry.name.c_str(),static_cast<textureUsage_t>(entry.usage),entry.allowDownSize,c.resolved);
        idBinaryImage binary(entry.name.c_str());imageReductionResult_t reduction{};
        if(c.file.bytes>64ull*1024*1024||!R_LoadPrecompressedDDS(c.file.qpath,binary,nullptr,static_cast<textureUsage_t>(entry.usage),c.resolved,c.mipmaps,&reduction,&c.file)||
            !R_ImageReductionIsExact(c.resolved,reduction)||!binary.GetContentIdentity(c.binary)||!Check(a,error,size))
            return Error(error,size,"Exact target image policy is unsupported or source content changed");
        c.reduction=reduction;
    }
    return Check(a,error,size);
}
}

uint64_t R_ImagePolicyNewResourceIdentity() noexcept {
    uint64_t old = resourceIdentityCounter.load(std::memory_order_relaxed);
    while (old != UINT64_MAX) {
        if (resourceIdentityCounter.compare_exchange_weak(old, old + 1,
            std::memory_order_relaxed, std::memory_order_relaxed)) return old + 1;
    }
    return 0;
}
void R_ImagePolicyResourceDestroyed() noexcept { AdvanceMutationEpoch(); }
void R_ImagePolicyLifecycleChanged() noexcept { AdvanceMutationEpoch(); }
bool R_ImagePolicyContentMutation() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (fullOwnerShutdown && std::this_thread::get_id() != rendererThread) return false;
    if (active) {
        if (active->preparing) {
            AdvanceMutationEpoch(); FailLocked("Resource mutation during read-only recovery preparation"); return false;
        }
        if (std::this_thread::get_id() != rendererThread) {
            AdvanceMutationEpoch(); FailLocked("Content mutation crossed the renderer thread"); return false;
        }
        if (!active->mutationEpoch || active->mutationEpoch != resourceMutationEpoch.load(std::memory_order_relaxed)) {
            if (active->original) recoveryInvalidated = true;
            FailLocked("Resource lifecycle changed before content mutation");
        }
        return !active->failure[0];
    }
    AdvanceMutationEpoch();
    return true;
}

bool R_ImagePolicyBindRendererThread() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (fullOwnerShutdown) return false;
    if (rendererThread == std::thread::id()) rendererThread = std::this_thread::get_id();
    else if (rendererThread != std::this_thread::get_id()) {
        FailLocked("Renderer initialization changed threads"); return false;
    }
    return true;
}
bool R_ImagePolicyRendererThread() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    return rendererThread != std::thread::id() && rendererThread == std::this_thread::get_id();
}
bool R_ImagePolicyActive() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    return active != nullptr;
}
bool R_ImagePolicyOperationAllowed() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (fullOwnerShutdown) return false;
    if (!active) return true;
    if (active->preparing) { FailLocked("Native resource operation during recovery preparation"); return false; }
    if (std::this_thread::get_id() != rendererThread) {
        FailLocked("Image policy work crossed the renderer thread"); return false;
    }
    if (!active->mutationEpoch || active->mutationEpoch != resourceMutationEpoch.load(std::memory_order_relaxed)) {
        if (active->original) recoveryInvalidated = true;
        FailLocked("Resource lifecycle changed before image work");
    }
    if (!SamePolicy(active->request.expectedCurrent, ReadPolicy())) FailLocked("Image policy changed before backend resource work");
    return !active->failure[0];
}
void R_ImagePolicyObserveError(const char* reason, int32_t nativeError) {
    R_ConsumedPolicyObserveError();
    const std::lock_guard<std::mutex> lock(policyMutex);
    FailLocked(reason, nativeError);
}
renderImageOperation_t::renderImageOperation_t(const idImage* image, bool upload, int mip, int layer, int width, int height)
    : image(image), attempt(0), upload(upload), allowed(R_ImagePolicyOperationAllowed()), succeeded(false),
      mip(mip), layer(layer), width(width), height(height) {
    imageConsumedLoad_t::BeforeOperation(image);
    if (allowed && image && image->IsFileBacked()) allowed = R_ImagePolicyContentMutation();
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (active && allowed && active->recording) attempt = active->result.attempt;
}
void renderImageOperation_t::Succeeded(uint64_t batch) { succeeded = true; uploadBatch = batch; }
renderImageOperation_t::~renderImageOperation_t() {
    imageConsumedLoad_t::Operation(image, upload, allowed && succeeded, mip, layer, width, height, uploadBatch);
    if (!attempt) return;
    renderDisplayPresentation_t device{}; R_GetDisplayPresentation(&device);
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (!active || active->result.attempt != attempt) return;
    if (!succeeded) { FailLocked(upload ? "Backend image upload was refused" : "Backend image allocation was refused"); return; }
    if (device.generation <= active->initialGeneration) { FailLocked("Image operation used the old device lifetime"); return; }
    try {
        if (!image || (active->images.size() >= MaxImages && active->images.find(image) == active->images.end())) {
            FailLocked("Image policy inventory exceeded its budget during reload"); return;
        }
        ImageProof& proof = active->images[image];
        if (!image->GetImagePolicyIdentity() || (proof.identity && proof.identity != image->GetImagePolicyIdentity())) {
            recoveryInvalidated = true; FailLocked("Image instance changed during resource operation"); return;
        }
        proof.identity = image->GetImagePolicyIdentity();
        if (!upload) {
            proof.allocated = true; proof.generation = device.generation;
            proof.storage = image->GetStorageGeneration(); proof.mips.fill(0);
            if (active->result.allocations == UINT32_MAX) { FailLocked("Image allocation receipt overflow"); return; }
            ++active->result.allocations;
        } else {
            if (!proof.allocated || proof.generation != device.generation || proof.storage != image->GetStorageGeneration()) {
                FailLocked("Image upload has no matching allocation"); return;
            }
            if (active->result.uploads == UINT32_MAX) { FailLocked("Image upload receipt overflow"); return; }
            ++active->result.uploads;
            const idImageOpts& opts = image->GetOpts();
            if (mip >= 0 && mip < opts.numLevels && mip < 31 && layer >= 0 && layer < 6 &&
                width == Max(1, opts.width >> mip) && height == Max(1, opts.height >> mip))
                proof.mips[layer] |= uint32_t(1) << mip;
        }
    } catch (const std::bad_alloc&) { FailLocked("Image receipt storage allocation failed"); }
}
bool R_ImagePolicyShouldReload(const idImage* image) {
    Attempt* a = Current(); if (!a) return true;
    const auto it = a->images.find(image);
    return it == a->images.end() || it->second.resident;
}
bool R_ImagePolicyBeforeTeardown(char* error, int size) {
    if (Attempt* a = Current()) {
        a->mutated = true;
        if(a->prepared){a->prepared->touched=true;a->prepared->completed={};a->prepared->completedDirection=0;}
        return Check(*a, error, size);
    }
    return true;
}
void R_ImagePolicyBeginDeviceReload() {
    if (Attempt* a = Current()) a->recording = true;
}
bool R_ImagePolicyAfterDeviceReload(char* error, int size) {
    Attempt* a = Current(); if (!a) return true;
    renderDisplayPresentation_t device{}; R_GetDisplayPresentation(&device);
    if (!device.available || device.generation <= a->initialGeneration)
        return Error(error, size, "Image policy restart produced no new device lifetime");
    a->result.deviceGeneration = device.generation;
    if (!Check(*a, error, size, true)) return false;
    for (const MaterialSource& source : a->materials) {
        if (!SourceMatches(source) || !Check(*a, error, size, true)) return Error(error, size, "Retained material source changed before reparse");
        if (!source.reparse) continue;
        idMaterial* material = const_cast<idMaterial*>(source.material);
        material->Invalidate();
        material->EnsureNotPurged();
        if (!Check(*a, error, size, true) || !SourceMatches(source)) return Error(error, size, "Retained material identity changed during reparse");
        if (material->GetState() != DS_PARSED && !(source.baselineDefault && material->GetState() == DS_DEFAULTED))
            return Error(error, size, "A material newly defaulted during image-policy reparse");
        ++a->result.materialSourcesReparsed;

    }
    return Check(*a, error, size, true);
}
bool R_ImagePolicyFinish(char* error, int size) {
    Attempt* a = Current(); if (!a) return true;
    if (!Check(*a, error, size, true)) return false;
    const int resolvedMaterials = declManager->GetNumDecls(DECL_MATERIAL);
    if (resolvedMaterials < 0 || static_cast<size_t>(resolvedMaterials) > MaxMaterials)
        return Error(error, size, "Material dependency inventory exceeds the checked budget");
    for (int i = 0; i < resolvedMaterials; ++i) {
        const auto* material = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, i, false));
        if (material && material->GetState() != DS_UNPARSED && !VerifyMaterialImages(*a, *material, error, size, true)) return false;
    }
#ifdef OPENQ4_RENDERER_VK_MODULE
    VK_Device_FlushUploadBatch(); VK_Device_WaitUploadBatch();
    if (vkCtx.presentationBlocked || vkCtx.uploadBatchOpen || vkCtx.uploadBatchInFlight ||
        vkCtx.numUploadBatchPending || vkCtx.numUploadBatchInFlight)
        return Error(error, size, "Vulkan image upload submission or completion failed");
#else
    if (!GLimp_EnsureActiveContext("checked image policy completion")) return Error(error, size, "GL context was lost during image policy work");
    glFinish(); GL_CheckErrors();
    if (!GLimp_EnsureActiveContext("checked image policy completed context")) return Error(error, size, "GL context changed during image completion");
#endif
    if (!Check(*a, error, size, true)) return false;
    const int count = globalImages->images.Num();
    if (count < 0 || static_cast<size_t>(count) > MaxImages) return Error(error, size, "Final image inventory exceeds the checked budget");
    std::set<const idImage*> seen;
    for (int i = 0; i < count; ++i) {
        const idImage* image = globalImages->images[i];
        if (!image || !seen.insert(image).second) return Error(error, size, "Final image inventory contains a missing or duplicate image");
        const auto old = a->images.find(image);
        if (!image->IsLoaded() && old != a->images.end() && !old->second.resident) continue;
        if (!VerifyImage(*a, image, error, size)) return false;
        ++a->result.imagesVerified;
        if (image->IsFileBacked()) ++a->result.fileImagesVerified;
        if (image->IsDefaulted()) ++a->result.baselineDefaultImages;
    }
    for (const auto& original : a->original->images)
        if (original.second.resident && !seen.count(original.first)) return Error(error, size, "An original resident image disappeared during reload");
    const int materialCount = declManager->GetNumDecls(DECL_MATERIAL);
    if (materialCount < 0 || static_cast<size_t>(materialCount) > MaxMaterials) return Error(error, size, "Final material inventory exceeds the checked budget");
    for (int i = 0; i < materialCount; ++i) {
        const auto* material = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, i, false));
        if (material && material->GetState() == DS_DEFAULTED) {
            const auto allowed = a->original->defaultMaterials.find(material);
            if (allowed == a->original->defaultMaterials.end() || !allowed->second.identity || allowed->second.identity != material->GetImagePolicyIdentity() ||
                allowed->second.index != i || allowed->second.name != material->GetName())
                return Error(error, size, "A newly referenced material defaulted during reload");
            ++a->result.baselineDefaultMaterials;
        }
        if (material && material->GetState() != DS_UNPARSED && !VerifyMaterialImages(*a, *material, error, size)) return false;
    }
    for (const MaterialSource& source : a->materials)
        if (!SourceMatches(source)) return Error(error, size, "A retained material changed after resource rebuild");
    if(a->prepared){
        if(a->borrowed.size()!=a->prepared->cpu[a->request.recoveryDirection-1].size()||!R_CompleteConsumedImageUploads())return Error(error,size,"Prepared image candidates did not all reach checked completion");
        const auto& selected=*a->prepared->directions[a->request.recoveryDirection-1];
        if(!InventoryMatches(*a,selected,error,size))return false;
        for(const auto& proof:a->selectedContent){
            const idImage* image=proof.first;const auto& entry=*proof.second;
            imagePortableContent_t actual{};
            if(!image||!image->GetPortableContent(actual)||!R_ImageFileContentEqual(actual.file,entry.content.file)||
                !R_ImageBinaryContentEqual(actual.binary,entry.content.binary)||actual.scope!=entry.content.scope||
                actual.resolved.maxDimension!=entry.content.resolved.maxDimension||actual.resolved.mipShift!=entry.content.resolved.mipShift||
                actual.resolved.minDimension!=entry.content.resolved.minDimension||actual.mipmaps!=entry.content.mipmaps)
                return Error(error,size,"Prepared image output did not preserve the exact selected descriptor");
            if(!Check(*a,error,size,true))return false;
        }
    }
    if (!Check(*a, error, size, true)) return false;
    a->finished = true;
    return true;
}
bool R_TryImagePolicyRestart(const renderImagePolicyRequest_t* request, renderImagePolicyResult_t* output, char* error, int size) {
    if (!request || !output) return Error(error, size, "An image-policy request and output are required");
    const renderImagePolicyRequest_t immutable = *request;
    try {
        Attempt candidate;
        candidate.request = immutable;
        candidate.mutationEpoch = resourceMutationEpoch.load(std::memory_order_relaxed);
        {
            const std::lock_guard<std::mutex> lock(policyMutex);
            if (fullOwnerShutdown) return Error(error, size, "The renderer owner is shutting down");
            if (active) { FailLocked("Reentrant image-policy request"); return Error(error, size, "A resource restart is already in progress"); }
            if (preparedRecovery) {
                if (!SameLease(immutable.recovery,preparedRecovery->lease)||immutable.recoveryDirection<1||immutable.recoveryDirection>2||
                    !preparedRecovery->directions[immutable.recoveryDirection-1]||preparedRecovery->epoch!=candidate.mutationEpoch)
                    return Error(error,size,"Prepared image recovery ownership or direction changed");
                candidate.prepared=preparedRecovery.get();candidate.original=preparedRecovery->original;
            } else if (immutable.recovery.owner||immutable.recovery.request||immutable.recovery.preparation||immutable.recoveryDirection)
                return Error(error,size,"Prepared image recovery lease is absent");
            if (recovery && recovery->mutationEpoch != candidate.mutationEpoch) recoveryInvalidated = true;
            if (recoveryInvalidated || !candidate.mutationEpoch)
                return Error(error, size, "Image-policy recovery was invalidated by intervening resource work; renderer lifetime must end");
            if (rendererThread != std::this_thread::get_id() || nextAttempt == UINT64_MAX)
                return Error(error, size, "Image-policy restart requires its renderer thread and a fresh attempt identity");
            candidate.result.attempt = ++nextAttempt;
            active = &candidate;
        }
        struct Scope {
            Attempt& candidate;
            ~Scope() {
                const std::lock_guard<std::mutex> lock(policyMutex);
                if (candidate.published) recovery.reset();
                else if (candidate.mutated) recovery = candidate.original;
                active = nullptr;
            }
        } scope{candidate};
        if (error && size > 0) error[0] = '\0';
        renderDisplayPresentation_t before{}; R_GetDisplayPresentation(&before);
        candidate.initialGeneration = before.generation; candidate.initialFailures = before.failureSequence;
        if (!ValidPolicy(immutable.expectedCurrent) || !SamePolicy(immutable.expectedCurrent, ReadPolicy()))
            return Error(error, size, "Image policy request does not match the current supported CVar policy");
        candidate.preparing=candidate.prepared!=nullptr;
        if (!Preflight(candidate, error, size)) return false;
        if (candidate.prepared) {
            const auto& selected=*candidate.prepared->directions[immutable.recoveryDirection-1];
            if(!SamePolicy(selected.policy,immutable.expectedCurrent))return Error(error,size,"Selected recovery policy differs from execution request");
            candidate.preparing=true;
            if(!InventoryMatches(candidate,selected,error,size))return false;
            // The selected CPU set was read, hashed and retained before any
            // caller policy write. Execution performs no VFS read or clone.
            candidate.selectedContent=candidate.prepared->selected[immutable.recoveryDirection-1];
            candidate.preparing=false;
        }
        if (!R_TryFullVidRestartForImagePolicy(&candidate.request.window, error, size)) return false;
        if (!candidate.finished || !Check(candidate, error, size, true)) return false;
        // Clear only this exact verified policy. No callbacks or allocating
        // result copies occur between final validation and POD publication.
        const std::lock_guard<std::mutex> lock(policyMutex);
        if (candidate.mutationEpoch != resourceMutationEpoch.load(std::memory_order_relaxed)) {
            recoveryInvalidated = true;
            return Error(error, size, "Resource lifecycle changed before receipt publication");
        }
        if (candidate.failure[0]) return Error(error, size, candidate.failure);
        globalImages->ClearCheckedImagePolicyChanges();
        *output = candidate.result;
        if(candidate.prepared){candidate.prepared->completed=candidate.result;candidate.prepared->completedDirection=immutable.recoveryDirection;candidate.prepared->completedFailures=candidate.initialFailures;}
        candidate.published = true;
        return true;
    } catch (const std::bad_alloc&) {
        return Error(error, size, "Image-policy receipt or source allocation failed");
    }
}

namespace {
bool PrepareRecovery(uint64_t owner,uint64_t request,const char* attempt,const renderImagePolicy_t* target,
    unsigned direction,const char* raw,uint32_t bytes,renderImageRecoveryLease_t* output,char* error,int size) {
    if(!owner||!request||!attempt||!output)return Error(error,size,"Complete recovery owner and output are required");
    size_t length=0;while(length<33&&attempt[length])++length;
    if(length!=32||!ir::Attempt(std::string_view(attempt,length)))return Error(error,size,"Invalid durable recovery attempt");
    const renderImagePolicy_t requested=target?*target:renderImagePolicy_t{};
    try {
        const std::string id(attempt,length);
        // Freeze borrowed serialized input before any engine/VFS callback.
        ir::Record decoded;std::string diagnostic(0,'\0');
        if(!target&&(!raw||!ir::Decode(std::string_view(raw,bytes),direction,id,decoded,diagnostic)))return Error(error,size,"Invalid cold image recovery record");
        if(target&&!ValidPolicy(requested))return Error(error,size,"Invalid target image policy");
        Attempt a;a.preparing=true;a.request.expectedCurrent=ReadPolicy();a.mutationEpoch=resourceMutationEpoch.load(std::memory_order_relaxed);
        auto prepared=std::make_unique<Prepared>();
        {
            const std::lock_guard<std::mutex> lock(policyMutex);
            if(active){FailLocked("Reentrant image preparation");return Error(error,size,"Image recovery work is already active");}
            if(fullOwnerShutdown)return Error(error,size,"The renderer owner is shutting down");
            if(preparedRecovery||recovery||recoveryInvalidated||!a.mutationEpoch||rendererThread!=std::this_thread::get_id()||nextPreparation==UINT64_MAX)
                return Error(error,size,"Image recovery preparation requires an idle fresh owner lifetime");
            prepared->lease={owner,request,++nextPreparation};active=&a;
        }
        struct Close {~Close(){const std::lock_guard<std::mutex> lock(policyMutex);active=nullptr;}} close;
        renderDisplayPresentation_t before{};R_GetDisplayPresentation(&before);a.initialGeneration=before.generation;a.initialFailures=before.failureSequence;
        if(!Preflight(a,error,size)||!R_CompleteConsumedImageUploads()||!Check(a,error,size))return Error(error,size,"Image recovery preflight or completion failed");
        prepared->original=a.original;prepared->epoch=a.mutationEpoch;prepared->observationEpoch=R_ConsumedPolicyObservationEpoch();prepared->initialPolicy=a.request.expectedCurrent;
        if(target){
            prepared->directions[0]=std::make_unique<ir::Data>();auto& restore=*prepared->directions[0];restore.direction=1;restore.attempt=id;
            if(!CaptureInventory(a,restore,true,error,size)||!StageImages(a,restore,error,size))return false;
            prepared->cpu[0].swap(a.cpu);prepared->selected[0].swap(a.selectedContent);prepared->directions[1]=std::make_unique<ir::Data>();
            if(!PrepareTarget(a,restore,requested,*prepared->directions[1],error,size))return false;
            uint64_t total=0;for(unsigned n=0;n<2;++n)for(const auto& i:prepared->directions[n]->images)if(i.resident){
                if(i.content.binary.payloadBytes>512ull*1024*1024-total)return Error(error,size,"Combined prepared CPU directions exceed their retained bound");total+=i.content.binary.payloadBytes;}
            if(!StageImages(a,*prepared->directions[1],error,size))return false;
            prepared->cpu[1].swap(a.cpu);
            prepared->selected[1].swap(a.selectedContent);
        } else {
            prepared->directions[direction-1]=std::make_unique<ir::Data>(*decoded.Get());
            if(!InventoryMatches(a,*prepared->directions[direction-1],error,size)||!StageImages(a,*prepared->directions[direction-1],error,size))return false;
            prepared->cpu[direction-1].swap(a.cpu);
            prepared->selected[direction-1].swap(a.selectedContent);
        }
        for(unsigned i=0;i<2;++i)if(prepared->directions[i]&&!ir::Encode(*prepared->directions[i],prepared->raw[i],diagnostic))return Error(error,size,diagnostic.c_str());
        if(!Check(a,error,size)||prepared->observationEpoch!=R_ConsumedPolicyObservationEpoch())return Error(error,size,"Image recovery observation lifetime changed");
        const renderImageRecoveryLease_t result=prepared->lease;
        // No callbacks/allocating result construction after final validation.
        const std::lock_guard<std::mutex> lock(policyMutex);
        if(a.failure[0]||a.mutationEpoch!=resourceMutationEpoch.load(std::memory_order_relaxed))return Error(error,size,"Image preparation changed before publication");
        preparedRecovery.swap(prepared);*output=result;return true;
    }catch(...){return Error(error,size,"Image recovery preparation allocation or source read failed");}
}
}
bool R_PrepareImagePolicyRecovery(uint64_t owner,uint64_t request,const char* attempt,const renderImagePolicy_t* target,
    renderImageRecoveryLease_t* output,char* error,int size) {
    if(!target)return Error(error,size,"Image target policy is required");
    return PrepareRecovery(owner,request,attempt,target,0,nullptr,0,output,error,size);
}
bool R_PrepareColdImagePolicyRecovery(uint64_t owner,uint64_t request,const char* attempt,uint32_t direction,
    const char* raw,uint32_t bytes,renderImageRecoveryLease_t* output,char* error,int size) {
    return PrepareRecovery(owner,request,attempt,nullptr,direction,raw,bytes,output,error,size);
}
bool R_CaptureImagePolicyRecovery(const renderImageRecoveryLease_t* requested,uint32_t direction,char* output,uint32_t capacity,
    uint32_t* bytes,char* error,int size) {
    if(!requested||!output||!bytes)return Error(error,size,"Image capture output is required");
    const auto lease=*requested;const std::lock_guard<std::mutex> lock(policyMutex);
    if(active||fullOwnerShutdown||rendererThread!=std::this_thread::get_id()||!preparedRecovery||!SameLease(lease,preparedRecovery->lease)||direction<1||direction>2||
        !preparedRecovery->directions[direction-1]||preparedRecovery->touched||preparedRecovery->epoch!=resourceMutationEpoch.load(std::memory_order_relaxed))
        return Error(error,size,"Image capture lease is stale");
    const auto& raw=preparedRecovery->raw[direction-1];if(raw.size()>capacity)return Error(error,size,"Image capture buffer is too small");
    std::memcpy(output,raw.data(),raw.size());*bytes=uint32_t(raw.size());return true;
}
bool R_CancelPreparedImagePolicyRecovery(const renderImageRecoveryLease_t* requested,char* error,int size) {
    if(!requested)return Error(error,size,"Image cancellation lease is required");
    const auto lease=*requested;std::unique_ptr<Prepared> retired;
    {
        const std::lock_guard<std::mutex> lock(policyMutex);
        if(active||fullOwnerShutdown||rendererThread!=std::this_thread::get_id()||!preparedRecovery||!SameLease(lease,preparedRecovery->lease)||preparedRecovery->touched||
            preparedRecovery->epoch!=resourceMutationEpoch.load(std::memory_order_relaxed)||!SamePolicy(preparedRecovery->initialPolicy,ReadPolicy()))
            return Error(error,size,"Image cancellation lease changed");
        // Failed teardown recovery remains sticky; canceling a preparation is
        // never permission to forget the original resident/default census.
        retired.swap(preparedRecovery);
    }
    return true;
}
bool R_ImagePolicyUsesPreparedContent() {Attempt* a=Current();return a&&a->prepared;}
bool R_ReleaseCompletedImagePolicyRecovery(const renderImageRecoveryLease_t* requested,uint32_t direction,
    const renderImagePolicyResult_t* result,char* error,int size) {
    if(!requested||!result)return Error(error,size,"A completed image recovery receipt is required");
    const auto lease=*requested;const auto receipt=*result;
    {
        const std::lock_guard<std::mutex> lock(policyMutex);
        if(fullOwnerShutdown||rendererThread!=std::this_thread::get_id())
            return Error(error,size,"Completed image release requires its available renderer owner");
    }
    renderDisplayPresentation_t device{};R_GetDisplayPresentation(&device);
    std::unique_ptr<Prepared> retired;
    {
        const std::lock_guard<std::mutex> lock(policyMutex);
        if(active||fullOwnerShutdown||!preparedRecovery||!SameLease(lease,preparedRecovery->lease)||!preparedRecovery->touched||direction<1||direction>2||
            direction!=preparedRecovery->completedDirection||preparedRecovery->epoch!=resourceMutationEpoch.load(std::memory_order_relaxed))
            return Error(error,size,"Completed image release ownership changed");
        const auto& exact=preparedRecovery->completed;
        if(!exact.attempt||receipt.attempt!=exact.attempt||receipt.deviceGeneration!=exact.deviceGeneration||receipt.imagesVerified!=exact.imagesVerified||
            receipt.fileImagesVerified!=exact.fileImagesVerified||receipt.materialSourcesReparsed!=exact.materialSourcesReparsed||
            receipt.baselineDefaultImages!=exact.baselineDefaultImages||receipt.baselineDefaultMaterials!=exact.baselineDefaultMaterials||
            receipt.allocations!=exact.allocations||receipt.uploads!=exact.uploads||receipt.reserved!=exact.reserved||!device.available||
            device.generation!=exact.deviceGeneration||device.failureSequence!=preparedRecovery->completedFailures||
            !SamePolicy(ReadPolicy(),preparedRecovery->directions[direction-1]->policy))return Error(error,size,"Completed image release receipt is stale");
        retired.swap(preparedRecovery);
    }
    return true;
}
renderImageOwnerShutdown_t::renderImageOwnerShutdown_t() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (active) { FailLocked("Full renderer shutdown interrupted checked image work"); return; }
    if (fullOwnerShutdown || (rendererThread != std::thread::id() && rendererThread != std::this_thread::get_id())) return;
    // Early startup may fail before Init bound the thread. No preparation can
    // exist in that state; binding here permits its actual owner cleanup.
    if (rendererThread == std::thread::id()) rendererThread = std::this_thread::get_id();
    imageOwner = globalImages;
    fullOwnerShutdown = this; allowed = true;
}
renderImageOwnerShutdown_t::~renderImageOwnerShutdown_t() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (fullOwnerShutdown != this) return;
    // Unwinding an incomplete teardown is not disposal proof. Retain both CPU
    // directions and the old census; another complete owner shutdown may retry.
    if (!completed) recoveryInvalidated = true;
    fullOwnerShutdown = nullptr;
}
bool renderImageOwnerShutdown_t::Complete() {
    std::unique_ptr<Prepared> retired;
    std::shared_ptr<const Baseline> retiredBaseline;
    {
        const std::lock_guard<std::mutex> lock(policyMutex);
        if (!allowed || completed || fullOwnerShutdown != this || active || rendererThread != std::this_thread::get_id()) return false;
        // The actual full Shutdown must have emptied this original manager.
        // A callback cannot substitute a different empty manager or repopulate
        // the original after its Shutdown and still certify owner completion.
        if (!globalImages || globalImages != imageOwner || globalImages->images.Num() != 0) return false;
        retired.swap(preparedRecovery); retiredBaseline.swap(recovery);
        recoveryInvalidated = false;
        AdvanceMutationEpoch();
        completed = true;
    }
    // Both CPU directions are now unreachable. Their callback-free allocator
    // destruction occurs outside the mutex while this scope still excludes new
    // preparation/initialization. Identity, attempt and preparation counters stay
    // monotonic across built-in owner shutdown/reinitialization.
    return true;
}
int R_ImagePolicyBorrowPreparedContent(const idImage* image,const idBinaryImage*& output,imagePortableContent_t& descriptor) {
    Attempt* a=Current();if(!a||!a->prepared)return 0;
    if(!image||a->preparing||!R_ImagePolicyOperationAllowed())return -1;
    const auto& cpu=a->prepared->cpu[a->request.recoveryDirection-1];const auto found=cpu.find(image);
    if(found==cpu.end()||!found->second){R_ImagePolicyObserveError("An image has no prepared CPU candidate");return -1;}
    const auto p=image->GetDeclaredPolicy();ir::Image key;key.name=image->GetName();key.filter=p.filter;key.repeat=p.repeat;
    key.usage=p.usage;key.cube=p.cube;key.flags=p.flags;key.allowDownSize=p.allowDownSize;
    const auto proof=a->selectedContent.find(image);
    if(proof==a->selectedContent.end()||!ir::SameImageKey(key,*proof->second)||!proof->second->resident){R_ImagePolicyObserveError("Prepared image metadata changed before upload");return -1;}
    if(!a->borrowed.insert(image).second){R_ImagePolicyObserveError("A prepared image was loaded twice in one attempt");return -1;}
    descriptor=proof->second->content;output=found->second.get();return 1;
}
