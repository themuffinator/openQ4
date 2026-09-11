// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "tr_local.h"
#include "RendererResourceSettings.h"
#include "RendererConsumedPolicy.h"
#include "DisplayPresentation.h"
#include <atomic>
#include <exception>
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "Vulkan/VulkanDevice.h"
#endif

namespace {
constinit std::atomic<uint64_t> observationEpoch{1};
thread_local imageConsumedLoad_t* activeLoad = nullptr;
// Only the renderer owner touches these; foreign threads invalidate the atomic
// epoch and never touch resource-owned observation storage.
uint64_t issuedGL = 0, completedGL = 0;
bool completing = false, completionFailed = false;
}

uint64_t R_ConsumedPolicyObservationEpoch() noexcept { return observationEpoch.load(); }
void R_ConsumedPolicyInvalidateThread() noexcept {
    uint64_t value = observationEpoch.load();
    while (value && !observationEpoch.compare_exchange_weak(value, value == UINT64_MAX ? 0 : value + 1)) {}
}
bool R_ConsumedPolicyThread() {
    if (R_ImagePolicyRendererThread()) return true;
    R_ConsumedPolicyInvalidateThread();
    return false;
}
void R_ConsumedPolicyObserveError() {
    R_ConsumedPolicyInvalidateThread();
    imageConsumedLoad_t::Error();
    if (R_ImagePolicyRendererThread() && completing) completionFailed = true;
}

void idImage::InvalidateConsumedPolicy() {
    if (R_ConsumedPolicyThread()) consumedPolicy = {};
}
bool idMaterial::GetConsumedPolicy(materialConsumedPolicy_t& output) const {
    if (!R_ImagePolicyRendererThread() || consumedParseDepth || !consumedPolicy.parsed || !consumedPolicy.revision ||
        consumedPolicy.instance != imagePolicyIdentity || consumedPolicy.revision != consumedParseRevision || !consumedPolicy.observationEpoch ||
        consumedPolicy.observationEpoch != R_ConsumedPolicyObservationEpoch() ||
        GetState() != DS_PARSED || TestMaterialFlag(MF_DEFAULTED)) return false;
    for (int i = 0; i < GetNumStages(); ++i) {
        if (GetStage(i)->newStage) return false; // Program reconstruction is outside this policy receipt.
    }
    output = consumedPolicy;
    return true;
}
bool idImage::GetConsumedPolicy(imageConsumedPolicy_t& output) const {
    if (!R_ImagePolicyRendererThread() || imageConsumedLoad_t::Active(this)) return false;
    const imageConsumedPolicy_t& value = consumedPolicy;
    if (value.completion != ICC_PENDING || !value.instance || !value.revision ||
        value.instance != imagePolicyIdentity || value.revision != consumedLoadRevision || !value.observationEpoch ||
        value.observationEpoch != R_ConsumedPolicyObservationEpoch() || !IsFileBacked() || defaulted || !IsLoaded() ||
        !value.storage || value.storage != storageGeneration || value.width != opts.width || value.height != opts.height ||
        value.levels != opts.numLevels || value.usage != usage || value.filter != filter ||
        value.repeat != repeat || value.cube != cubeFiles || value.flags != flags ||
        value.allowDownSize != allowDownSize) return false;
    renderDisplayPresentation_t device{};
    R_GetDisplayPresentation(&device);
    if (!device.available || !value.device || value.device != device.generation ||
        value.deviceFailures != device.failureSequence) return false;
#ifdef OPENQ4_RENDERER_VK_MODULE
    if (vkCtx.presentationBlocked || !value.batch || value.batch > vkCtx.uploadBatchCompletedSerial) return false;
#else
    if (!value.batch || value.batch > completedGL) return false;
#endif
    imageConsumedPolicy_t complete = value;
    complete.completion = ICC_COMPLETE;
    output = complete;
    return true;
}

bool idImage::GetPortableContent(imagePortableContent_t& output) const {
    imageConsumedPolicy_t actual;
    if (!GetConsumedPolicy(actual) || actual.usage < TD_SPECULAR || actual.usage > TD_MATERIAL_DATA ||
        !R_ImageFileContentValid(actual.fileContent) ||
        actual.binaryContent.version != 1 || actual.binaryContent.width != opts.width || actual.binaryContent.height != opts.height ||
        actual.binaryContent.levels != opts.numLevels || actual.binaryContent.textureType != opts.textureType ||
        actual.binaryContent.format != opts.format || actual.binaryContent.colorFormat != opts.colorFormat) return false;
    imagePortableContent_t candidate;
    candidate.version=1; candidate.file=actual.fileContent; candidate.binary=actual.binaryContent;
    candidate.usage=actual.usage;
    if (actual.source == ICS_DIRECT_DDS && actual.fileContent.kind == IFC_DIRECT_DDS &&
        R_ImageReductionIsExact(actual.resolved,actual.reduction)) {
        candidate.scope=IPC_DIRECT_SOURCE; candidate.resolved=actual.resolved; candidate.reduction=actual.reduction;
        candidate.mipmaps=(actual.flags & IMAGEFLAG_NOMIPS) == 0 && actual.filter != TF_LINEAR && actual.filter != TF_NEAREST;
    } else if (actual.source == ICS_GENERATED && actual.fileContent.kind == IFC_OBSERVED_BIMAGE) {
        // Intentionally zero policy/reduction fields: cache restoration cannot
        // acquire original-source policy authority from a filename or timestamp.
        candidate.scope=IPC_CACHE_PIXELS_ONLY;
    } else return false;
    output=candidate; return true;
}
void imageConsumedLoad_t::Content(const idBinaryImage& binary, imageConsumedSource_t source) {
    candidate.fileContent={}; candidate.binaryContent={};
    if (!observing) return;
    const imageFileContent_t file=binary.GetFileContent();
    // Decoded/image-program/default output has no supported same-read source
    // identity. Refuse before hashing potentially large unsupported payloads.
    if (!R_ImageFileContentValid(file) ||
        !((source == ICS_DIRECT_DDS && file.kind == IFC_DIRECT_DDS) ||
          (source == ICS_GENERATED && file.kind == IFC_OBSERVED_BIMAGE))) return;
    imageBinaryContent_t output;
    if (binary.GetContentIdentity(output)) {
        candidate.fileContent=file; candidate.binaryContent=output;
    }
}

imageConsumedLoad_t::imageConsumedLoad_t(idImage& target) : image(target), initialExceptions(std::uncaught_exceptions()) {
    // Even an unsupported/unobserved load resolves one immutable policy for its
    // cache key and CPU reduction. Observation never changes the legacy result.
    candidate.inputs = R_ReadImageDownsizeInputs();
    R_ResolveImageDownsizePolicy(candidate.inputs, image.GetName(), image.usage, image.allowDownSize, candidate.resolved);
    if (!R_ConsumedPolicyThread()) return;
    image.consumedPolicy = {};
    if (!image.IsFileBacked()) return;
    candidate.instance = image.imagePolicyIdentity;
    candidate.revision = R_ImagePolicyNewResourceIdentity();
    image.consumedLoadRevision = candidate.revision;
    candidate.observationEpoch = R_ConsumedPolicyObservationEpoch();
    candidate.usage = image.usage; candidate.filter = image.filter; candidate.repeat = image.repeat;
    candidate.cube = image.cubeFiles; candidate.flags = image.flags; candidate.allowDownSize = image.allowDownSize;
    renderDisplayPresentation_t device{}; R_GetDisplayPresentation(&device);
    candidate.device = device.generation; candidate.deviceFailures = device.failureSequence;
    observing = candidate.instance && candidate.revision && candidate.observationEpoch && candidate.device;
    if (observing) { previous = activeLoad; activeLoad = this; }
}
imageConsumedLoad_t::~imageConsumedLoad_t() {
    if (!observing) return;
    if (activeLoad != this) { R_ConsumedPolicyInvalidateThread(); return; }
    activeLoad = previous;
    candidate.completion = ICC_FAILED;
    if (std::uncaught_exceptions() != initialExceptions || !loaded || failed || !allocated || image.defaulted || !image.IsLoaded() || image.consumedLoadRevision != candidate.revision || candidate.observationEpoch != R_ConsumedPolicyObservationEpoch()) {
        image.consumedPolicy = candidate;
        return;
    }
    const idImageOpts& opts = image.opts;
    candidate.width = opts.width; candidate.height = opts.height; candidate.levels = opts.numLevels;
    candidate.layers = opts.textureType == TT_CUBIC ? 6 : opts.textureType == TT_2D ? 1 : 0;
    if (!candidate.layers || opts.numLevels < 1 || opts.numLevels > 30 || opts.width < 1 || opts.height < 1) {
        image.consumedPolicy = candidate; return;
    }
    const uint32_t expected = (uint32_t(1) << opts.numLevels) - 1;
    for (int layer = 0; layer < candidate.layers; ++layer) {
        if (mips[layer] != expected) { image.consumedPolicy = candidate; return; }
    }
    renderDisplayPresentation_t device{}; R_GetDisplayPresentation(&device);
    if (candidate.device != device.generation || candidate.deviceFailures != device.failureSequence ||
        candidate.storage != image.storageGeneration) { image.consumedPolicy = candidate; return; }
    // Actual DDS/decoded dimensions must match the once-resolved request.
    // Generated cache headers have no original source extents: their existing
    // completion proves admitted cache bytes/output only, not reconstruction.
    if (candidate.source == ICS_GENERATED) candidate.reduction = {};
    const imageReductionResult_t& reduction = candidate.reduction;
    const bool decoded = candidate.source == ICS_DECODED_2D || candidate.source == ICS_DECODED_CUBE;
    const bool direct = candidate.source == ICS_DIRECT_DDS;
    if ((decoded || direct) && (!R_ImageReductionIsExact(candidate.resolved, reduction) ||
        reduction.selectedWidth != opts.width || reduction.selectedHeight != opts.height ||
        (decoded && reduction.authoredLevels != 0) ||
        (direct && (candidate.layers != 1 || reduction.authoredLevels <= 0 || opts.numLevels > reduction.authoredLevels - reduction.firstLevel)) ||
        (candidate.source == ICS_DECODED_CUBE && (candidate.layers != 6 || reduction.sourceWidth != reduction.sourceHeight)))) {
        candidate.completion = ICC_UNSUPPORTED;
    } else {
#ifndef OPENQ4_RENDERER_VK_MODULE
        candidate.batch = candidate.revision;
        if (issuedGL < candidate.batch) issuedGL = candidate.batch;
#endif
        candidate.completion = candidate.batch ? ICC_PENDING : ICC_FAILED;
    }
    image.consumedPolicy = candidate;
}
void imageConsumedLoad_t::Loaded(imageConsumedSource_t source) {
    loaded = source == ICS_GENERATED || source == ICS_DECODED_2D || source == ICS_DECODED_CUBE || source == ICS_DIRECT_DDS;
    candidate.source = source;
}
void imageConsumedLoad_t::BeforeOperation(const idImage* image) {
    if (image && !Active(image)) const_cast<idImage*>(image)->InvalidateConsumedPolicy();
}
bool imageConsumedLoad_t::Active(const idImage* image) {
    for (imageConsumedLoad_t* scope = activeLoad; scope; scope = scope->previous) {
        if (&scope->image == image) return true;
    }
    return false;
}
void imageConsumedLoad_t::Error() {
    for (imageConsumedLoad_t* scope = activeLoad; scope; scope = scope->previous) scope->failed = true;
}
void imageConsumedLoad_t::Operation(const idImage* target, bool upload, bool succeeded,
    int mip, int layer, int width, int height, uint64_t batch) {
    if (!target || !R_ConsumedPolicyThread()) return;
    imageConsumedLoad_t* scope = activeLoad;
    while (scope && &scope->image != target) scope = scope->previous;
    if (!scope) { const_cast<idImage*>(target)->consumedPolicy = {}; return; }
    if (!succeeded || scope->candidate.revision != target->consumedLoadRevision) { scope->failed = true; return; }
    renderDisplayPresentation_t device{}; R_GetDisplayPresentation(&device);
    if (scope->candidate.device != device.generation || scope->candidate.deviceFailures != device.failureSequence) {
        scope->failed = true; return;
    }
    if (!upload) {
        scope->allocated = true;
        scope->candidate.storage = target->storageGeneration;
        for (uint32_t& levels : scope->mips) levels = 0;
        return;
    }
    const idImageOpts& opts = target->opts;
    if (!scope->allocated || scope->candidate.storage != target->storageGeneration ||
        mip < 0 || mip >= opts.numLevels || mip >= 30 || layer < 0 || layer >= 6 ||
        width != Max(1, opts.width >> mip) || height != Max(1, opts.height >> mip)) {
        scope->failed = true; return;
    }
#ifdef OPENQ4_RENDERER_VK_MODULE
    if (!batch) { scope->failed = true; return; }
#endif
    scope->mips[layer] |= uint32_t(1) << mip;
    if (scope->candidate.batch < batch) scope->candidate.batch = batch;
}

bool R_CompleteConsumedImageUploads() {
#ifdef ID_DEDICATED
    return false;
#else
    if (!R_ConsumedPolicyThread() || activeLoad || completing) return false;
    renderDisplayPresentation_t before{}; R_GetDisplayPresentation(&before);
    if (!before.available) return false;
    const uint64_t epoch = R_ConsumedPolicyObservationEpoch();
    if (!epoch) return false;
    completing = true; completionFailed = false;
    struct Close { ~Close() { completing = false; } } close;
    try {
#ifdef OPENQ4_RENDERER_VK_MODULE
    VK_Device_FlushUploadBatch();
    VK_Device_WaitUploadBatch();
    if (vkCtx.presentationBlocked || vkCtx.uploadBatchOpen || vkCtx.uploadBatchInFlight) return false;
#else
    const uint64_t through = issuedGL;
    GL_CheckErrors();
    glFinish();
    GL_CheckErrors();
#endif
    renderDisplayPresentation_t after{}; R_GetDisplayPresentation(&after);
    if (completionFailed || epoch != R_ConsumedPolicyObservationEpoch() ||
        before.generation != after.generation || !after.available || before.failureSequence != after.failureSequence) return false;
#ifndef OPENQ4_RENDERER_VK_MODULE
    completedGL = through;
#endif
    return true;
    } catch (...) {
        R_ConsumedPolicyInvalidateThread();
        return false;
    }
#endif
}
