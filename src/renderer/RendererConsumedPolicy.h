// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <stdint.h>

// Process-local value observations, never portable content/recovery descriptors.
// No record contains a native handle, pointer, CVar reference or owning string.
struct imageDownsizeInputs_t {
    int downSize, downSizeLimit, downSizeSpecular, downSizeSpecularLimit;
    int downSizeBump, downSizeBumpLimit, picmip, picmipFilter, picmipMinSize;
};
struct imageDownsizePolicy_t {
    int maxDimension = 0, mipShift = 0, minDimension = 1;
    bool IsActive() const { return maxDimension > 0 || mipShift > 0; }
};
struct materialQualityInputs_t { bool ignoreHighQuality, makingBuild; };
struct materialConsumedPolicy_t {
    uint64_t instance = 0, revision = 0, observationEpoch = 0;
    materialQualityInputs_t inputs{};
    bool parsed = false;
};
enum imageConsumedCompletion_t : uint32_t {
    ICC_UNOBSERVED = 0, ICC_PENDING, ICC_COMPLETE, ICC_FAILED, ICC_UNSUPPORTED
};
enum imageConsumedSource_t : uint32_t {
    ICS_UNKNOWN = 0, ICS_GENERATED, ICS_DECODED_2D, ICS_DECODED_CUBE,
    ICS_DIRECT_DDS, ICS_DEFAULT
};
struct imageConsumedPolicy_t {
    uint64_t instance = 0, revision = 0, observationEpoch = 0;
    uint64_t device = 0, deviceFailures = 0, storage = 0, batch = 0;
    imageDownsizeInputs_t inputs{};
    imageDownsizePolicy_t resolved{};
    int usage = 0, filter = 0, repeat = 0, cube = 0;
    unsigned int flags = 0;
    bool allowDownSize = false;
    int width = 0, height = 0, levels = 0, layers = 0;
    uint32_t source = ICS_UNKNOWN, completion = ICC_UNOBSERVED;
};

imageDownsizeInputs_t R_ReadImageDownsizeInputs();
// Renderer thread only. A bulk finish observes already issued work; no reload,
// no per-image wait. False publishes no completion watermark. Getters never call it.
bool R_CompleteConsumedImageUploads();
uint64_t R_ConsumedPolicyObservationEpoch() noexcept;
bool R_ConsumedPolicyThread();
void R_ConsumedPolicyObserveError();
void R_ConsumedPolicyInvalidateThread() noexcept;

class idImage;
class imageConsumedLoad_t {
public:
    explicit imageConsumedLoad_t(idImage& image);
    ~imageConsumedLoad_t();
    const imageDownsizePolicy_t& Policy() const { return candidate.resolved; }
    void Loaded(imageConsumedSource_t source);
    static void BeforeOperation(const idImage* image);
    static bool Active(const idImage* image);
    static void Operation(const idImage* image, bool upload, bool succeeded,
        int mip, int layer, int width, int height, uint64_t batch);
    static void Error();
    imageConsumedLoad_t(const imageConsumedLoad_t&) = delete;
    imageConsumedLoad_t& operator=(const imageConsumedLoad_t&) = delete;
private:
    idImage& image;
    imageConsumedLoad_t* previous = nullptr;
    imageConsumedPolicy_t candidate{};
    uint32_t mips[6]{};
    int initialExceptions = 0;
    bool observing = false, allocated = false, loaded = false, failed = false;
};
