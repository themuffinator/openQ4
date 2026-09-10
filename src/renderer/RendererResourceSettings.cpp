// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#pragma hdrstop
#include "tr_local.h"
#include "RendererResourceSettings.h"
#include "RendererConsumedPolicy.h"
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
struct Attempt {
    renderImagePolicyRequest_t request{};
    renderImagePolicyResult_t result{};
    uint64_t initialGeneration = 0, initialFailures = 0;
    uint64_t mutationEpoch = 0;
    bool recording = false, finished = false, mutated = false, published = false;
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

// No mutex is held across engine/native callbacks. The only shared pointer is
// accessed while locked; foreign threads never dereference it or any resource.
std::mutex policyMutex;
Attempt* active = nullptr;
std::thread::id rendererThread;
uint64_t nextAttempt = 0;
std::shared_ptr<const Baseline> recovery;
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
    const auto previous = recovery;
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
    if (active) {
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

void R_ImagePolicyBindRendererThread() {
    const std::lock_guard<std::mutex> lock(policyMutex);
    if (rendererThread == std::thread::id()) rendererThread = std::this_thread::get_id();
    else if (rendererThread != std::this_thread::get_id()) FailLocked("Renderer initialization changed threads");
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
    if (!active) return true;
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
    if (Attempt* a = Current()) { a->mutated = true; return Check(*a, error, size); }
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
            if (active) { FailLocked("Reentrant image-policy request"); return Error(error, size, "A resource restart is already in progress"); }
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
        if (!Preflight(candidate, error, size)) return false;
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
        candidate.published = true;
        return true;
    } catch (const std::bad_alloc&) {
        return Error(error, size, "Image-policy receipt or source allocation failed");
    }
}
