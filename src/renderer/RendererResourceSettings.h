// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <stdint.h>
#include "RenderModuleAPI.h"

// Exact requested CVar policy. Device/upload-ring/path policies are separate.
struct renderImagePolicy_t {
    int32_t downSize, downSizeLimit, downSizeSpecular, downSizeSpecularLimit;
    int32_t downSizeBump, downSizeBumpLimit, usePrecompressedTextures, ignoreHighQuality;
};
struct renderImagePolicyRequest_t {
    renderWindowRequest_t window;
    renderImagePolicy_t expectedCurrent;
};
struct renderImagePolicyResult_t {
    uint64_t attempt; // Nonreused within a loaded renderer module.
    uint64_t deviceGeneration;
    uint32_t imagesVerified, fileImagesVerified, materialSourcesReparsed;
    uint32_t baselineDefaultImages, baselineDefaultMaterials;
    uint32_t allocations, uploads;
    uint32_t reserved;
};

// Synchronous checked resource work, not a UI/font-page readiness or present
// receipt. Pair with the engine's module epoch. A fresh owner draw/present is
// still required. Copies request before callbacks. False leaves *output exactly
// unchanged, including after partial teardown/mutation; caller owns restore of
// its original actual display/image baseline. Diagnostics use error only; error
// storage must not alias request/output.
bool R_TryImagePolicyRestart(const renderImagePolicyRequest_t* request,
    renderImagePolicyResult_t* output, char* error, int errorSize);

// Renderer-private hooks. Outside an attempt, operation observation is inert;
// lifecycle/content hooks advance a monotonic epoch to invalidate failed recovery.
// A foreign thread is refused and poisons the active attempt before content work.
bool R_ImagePolicyOperationAllowed();
// Constructor/destructor primitives are constant-initialized atomics only:
// no allocator, mutex, resource access or callbacks, including static teardown.
uint64_t R_ImagePolicyNewResourceIdentity() noexcept;
void R_ImagePolicyResourceDestroyed() noexcept;
void R_ImagePolicyLifecycleChanged() noexcept;
bool R_ImagePolicyContentMutation();
bool R_ImagePolicyActive();
void R_ImagePolicyObserveError(const char* reason, int32_t nativeError = 0);
void R_ImagePolicyBindRendererThread();
bool R_ImagePolicyShouldReload(const class idImage* image);
bool R_ImagePolicyBeforeTeardown(char* error, int errorSize);
void R_ImagePolicyBeginDeviceReload();
bool R_ImagePolicyAfterDeviceReload(char* error, int errorSize);
bool R_ImagePolicyFinish(char* error, int errorSize);

class idImage;
// Every backend early return is a refusal unless explicitly completed. Native
// API errors are observed separately before legacy error-draining/log filters.
class renderImageOperation_t {
public:
    explicit renderImageOperation_t(const idImage* image, bool upload = false,
        int mip = 0, int layer = 0, int width = 0, int height = 0);
    ~renderImageOperation_t();
    void Succeeded();
    bool Allowed() const { return allowed; }
    renderImageOperation_t(const renderImageOperation_t&) = delete;
    renderImageOperation_t& operator=(const renderImageOperation_t&) = delete;
private:
    const idImage* image;
    uint64_t attempt;
    bool upload, allowed, succeeded;
    int mip, layer, width, height;
};
