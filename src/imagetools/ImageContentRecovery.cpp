// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../renderer/Image.h"
#include "BinaryImage.h"

namespace {
bool EmptyReduction(const imageReductionResult_t& value) {
    return !value.sourceWidth && !value.sourceHeight && !value.requestedWidth && !value.requestedHeight &&
        !value.selectedWidth && !value.selectedHeight && !value.authoredLevels && !value.firstLevel && value.status == IR_UNOBSERVED;
}
bool SameReduction(const imageReductionResult_t& a, const imageReductionResult_t& b) {
    return a.sourceWidth==b.sourceWidth && a.sourceHeight==b.sourceHeight &&
        a.requestedWidth==b.requestedWidth && a.requestedHeight==b.requestedHeight &&
        a.selectedWidth==b.selectedWidth && a.selectedHeight==b.selectedHeight &&
        a.authoredLevels==b.authoredLevels && a.firstLevel==b.firstLevel && a.status==b.status;
}
}
bool R_ReconstructImageContent(const imagePortableContent_t& requested, idBinaryImage& output) {
    // The caller may borrow requested from an object affected by VFS callbacks.
    // Freeze every value before opening files. No current CVar participates.
    const imagePortableContent_t expected=requested;
    if (expected.version != 1 || !R_ImageFileContentValid(expected.file) || expected.binary.version != 1 ||
        expected.usage < TD_SPECULAR || expected.usage > TD_MATERIAL_DATA) return false;
    const bool direct=expected.scope == IPC_DIRECT_SOURCE;
    if (direct) {
        if (expected.file.kind != IFC_DIRECT_DDS || expected.binary.layers != 1 || expected.binary.textureType != TT_2D ||
            expected.resolved.maxDimension < 0 || expected.resolved.maxDimension > 32768 ||
            expected.resolved.mipShift < 0 || expected.resolved.mipShift > 30 ||
            expected.resolved.minDimension < 1 || expected.resolved.minDimension > 32768 ||
            expected.reduction.sourceWidth < 1 || expected.reduction.sourceWidth > 32768 ||
            expected.reduction.sourceHeight < 1 || expected.reduction.sourceHeight > 32768 ||
            expected.reduction.authoredLevels < 1 || !R_ImageReductionIsExact(expected.resolved,expected.reduction) ||
            expected.binary.width != expected.reduction.selectedWidth || expected.binary.height != expected.reduction.selectedHeight ||
            expected.binary.levels != (expected.mipmaps ? expected.reduction.authoredLevels-expected.reduction.firstLevel : 1)) return false;
    } else {
        if (expected.scope != IPC_CACHE_PIXELS_ONLY || expected.file.kind != IFC_OBSERVED_BIMAGE ||
            expected.resolved.maxDimension || expected.resolved.mipShift || expected.resolved.minDimension != 1 ||
            expected.mipmaps || !EmptyReduction(expected.reduction)) return false;
    }
    try {
        idBinaryImage candidate(output.GetName());
        if (direct) {
            imageReductionResult_t reduction;
            if (!R_LoadPrecompressedDDS(expected.file.qpath,candidate,NULL,(textureUsage_t)expected.usage,
                expected.resolved,expected.mipmaps,&reduction,&expected.file) || !SameReduction(expected.reduction,reduction)) return false;
        } else if (!candidate.LoadExactContentFile(expected.file)) return false;
        imageBinaryContent_t actual;
        if (!R_ImageFileContentEqual(expected.file,candidate.GetFileContent()) ||
            !candidate.GetContentIdentity(actual) || !R_ImageBinaryContentEqual(expected.binary,actual)) return false;
        // All VFS callbacks and fallible CPU work have ended. This transfers only
        // bounded owned CPU storage; native allocation/upload is a separate step.
        output.SwapContent(candidate); return true;
    } catch (...) { return false; }
}
