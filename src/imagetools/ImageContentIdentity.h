// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>

// Portable byte identities, not native upload/driver or source-policy receipts.
// No timestamps, absolute paths, pointers, process tokens or owning allocations.
constexpr std::size_t IMAGE_CONTENT_PATH_BYTES = 512;
constexpr std::uint64_t IMAGE_CONTENT_MAX_BYTES = 1ull << 30;
enum imageFileContentKind_t : std::uint32_t {
    IFC_UNOBSERVED = 0, IFC_DIRECT_DDS = 1, IFC_OBSERVED_BIMAGE = 2
};
struct imageContentDigest_t {
    std::uint8_t bytes[32]{};
};
struct imageFileContent_t {
    std::uint32_t kind = IFC_UNOBSERVED;
    std::uint64_t bytes = 0;
    char qpath[IMAGE_CONTENT_PATH_BYTES]{};
    imageContentDigest_t digest{};
};
struct imageBinaryContent_t {
    std::uint32_t version = 0;
    int textureType = 0, format = 0, colorFormat = 0;
    int width = 0, height = 0, levels = 0, layers = 0;
    std::uint64_t payloadBytes = 0;
    imageContentDigest_t digest{};
};
// Borrowed only while computing an output identity; never stored/serialized.
struct imageContentMipView_t {
    int level = 0, layer = 0, width = 0, height = 0, bytes = 0;
    const void* data = nullptr;
};
bool R_ImageContentPathValid(const char* qpath) noexcept;
bool R_ImageFileContentValid(const imageFileContent_t&) noexcept;
bool R_ImageFileContentEqual(const imageFileContent_t&, const imageFileContent_t&) noexcept;
bool R_ImageBinaryContentEqual(const imageBinaryContent_t&, const imageBinaryContent_t&) noexcept;
// Exact owned read only. False preserves output and never reads beyond a bound.
bool R_MakeImageFileContent(imageFileContentKind_t, const char* qpath,
    const void* data, std::size_t bytes, imageFileContent_t& out) noexcept;
// Hashes an explicit LE header and ordered (layer,level) metadata + SHA256 of
// every complete mip. The supplied type/format/byte extents are independently
// checked by idBinaryImage against the actual format before this function.
bool R_MakeImageBinaryContent(const imageBinaryContent_t& header,
    const imageContentMipView_t* mips, std::size_t count, imageBinaryContent_t& out) noexcept;
