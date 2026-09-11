// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <stdint.h>
#include "RenderModuleAPI.h"
struct imagePortableContent_t;

// Exact requested CVar policy. Device/upload-ring/path policies are separate.
struct renderImagePolicy_t {
    int32_t downSize, downSizeLimit, downSizeSpecular, downSizeSpecularLimit;
    int32_t downSizeBump, downSizeBumpLimit, usePrecompressedTextures, ignoreHighQuality;
};
struct renderImageRecoveryLease_t {
    uint64_t owner=0,request=0,preparation=0;
};
struct renderImagePolicyRequest_t {
    renderWindowRequest_t window;
    renderImagePolicy_t expectedCurrent;
    renderImageRecoveryLease_t recovery{};
    uint32_t recoveryDirection=0; // 1 original restore, 2 prepared target; 0 unprepared.
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

// These private, owner-thread calls precede all caller CVar/device writes.
// Capture copies canonical CPU descriptor bytes, never process tokens. False
// preserves outputs. The caller must durably write BOTH directions before
// executing a target. No host/UI/input authority is conferred by this lease.
bool R_PrepareImagePolicyRecovery(uint64_t owner,uint64_t request,const char* attempt,
    const renderImagePolicy_t* target,renderImageRecoveryLease_t* output,char* error,int size);
bool R_CaptureImagePolicyRecovery(const renderImageRecoveryLease_t* lease,uint32_t direction,
    char* output,uint32_t capacity,uint32_t* bytes,char* error,int size);
bool R_PrepareColdImagePolicyRecovery(uint64_t owner,uint64_t request,const char* attempt,uint32_t direction,
    const char* raw,uint32_t bytes,renderImageRecoveryLease_t* output,char* error,int size);
bool R_CancelPreparedImagePolicyRecovery(const renderImageRecoveryLease_t* lease,char* error,int size);
bool R_ReleaseCompletedImagePolicyRecovery(const renderImageRecoveryLease_t*,uint32_t direction,
    const renderImagePolicyResult_t*,char* error,int size);

// ActuallyLoadImage borrows the immutable, already verified CPU candidate.
// Pointer lifetime is the current synchronous checked attempt only; no pointer
// may escape the loader. Both CPU directions survive failed native work. 0 is
// ordinary route, 1 borrowed, -1 refusal; no second VFS read or fallback.
int R_ImagePolicyBorrowPreparedContent(const class idImage*,const class idBinaryImage*&,imagePortableContent_t&);
bool R_ImagePolicyUsesPreparedContent();

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
bool R_ImagePolicyRendererThread();
void R_ImagePolicyObserveError(const char* reason, int32_t nativeError = 0);
bool R_ImagePolicyBindRendererThread();
bool R_ImagePolicyShouldReload(const class idImage* image);
bool R_ImagePolicyBeforeTeardown(char* error, int errorSize);
void R_ImagePolicyBeginDeviceReload();
bool R_ImagePolicyAfterDeviceReload(char* error, int errorSize);
bool R_ImagePolicyFinish(char* error, int errorSize);

// Only the full renderer owner's Shutdown method can create/complete this
// scope. A device restart, empty inventory or changed epoch cannot release
// prepared CPU data. The scope excludes new recovery work through destruction.
class renderImageOwnerShutdown_t {
    friend class idRenderSystemLocal;
    renderImageOwnerShutdown_t();
    ~renderImageOwnerShutdown_t();
    bool Allowed() const { return allowed; }
    bool Complete();
    renderImageOwnerShutdown_t(const renderImageOwnerShutdown_t&) = delete;
    renderImageOwnerShutdown_t& operator=(const renderImageOwnerShutdown_t&) = delete;
    const void* imageOwner = nullptr;
    bool allowed = false, completed = false;
};

class idImage;
// Every backend early return is a refusal unless explicitly completed. Native
// API errors are observed separately before legacy error-draining/log filters.
class renderImageOperation_t {
public:
    explicit renderImageOperation_t(const idImage* image, bool upload = false,
        int mip = 0, int layer = 0, int width = 0, int height = 0);
    ~renderImageOperation_t();
    void Succeeded(uint64_t uploadBatch = 0);
    bool Allowed() const { return allowed; }
    renderImageOperation_t(const renderImageOperation_t&) = delete;
    renderImageOperation_t& operator=(const renderImageOperation_t&) = delete;
private:
    const idImage* image;
    uint64_t attempt;
    uint64_t uploadBatch = 0;
    bool upload, allowed, succeeded;
    int mip, layer, width, height;
};
