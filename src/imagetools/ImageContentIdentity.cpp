// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ImageContentIdentity.h"
#include "../idlib/CryptoHash.h"
#include <cstring>
#include <initializer_list>

namespace {
bool DigestValid(const imageContentDigest_t& value) noexcept {
    for (auto byte : value.bytes) if (byte) return true;
    return false;
}
bool DigestEqual(const imageContentDigest_t& a, const imageContentDigest_t& b) noexcept {
    return std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)) == 0;
}
void Word(std::uint8_t*& cursor, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8) *cursor++ = std::uint8_t(value >> shift);
}
}
bool R_ImageContentPathValid(const char* path) noexcept {
    if (!path || !path[0] || path[0] == '/') return false;
    std::size_t size = 0, segment = 0;
    for (; size < IMAGE_CONTENT_PATH_BYTES && path[size]; ++size) {
        const auto c = static_cast<unsigned char>(path[size]);
        // First portable version supports the stock ASCII qpath namespace.
        // It does not reinterpret platform encoding or normalize a foreign path.
        if (c < 32 || c >= 127 || c == '\\' || c == ':') return false;
        if (c == '/') {
            if (size == segment || (size-segment == 1 && path[segment] == '.') ||
                (size-segment == 2 && path[segment] == '.' && path[segment+1] == '.')) return false;
            segment = size + 1;
        }
    }
    return size < IMAGE_CONTENT_PATH_BYTES && size > segment &&
        !(size-segment == 1 && path[segment] == '.') &&
        !(size-segment == 2 && path[segment] == '.' && path[segment+1] == '.');
}
bool R_ImageFileContentValid(const imageFileContent_t& value) noexcept {
    return (value.kind == IFC_DIRECT_DDS || value.kind == IFC_OBSERVED_BIMAGE) &&
        value.bytes && value.bytes <= IMAGE_CONTENT_MAX_BYTES && R_ImageContentPathValid(value.qpath) && DigestValid(value.digest);
}
bool R_ImageFileContentEqual(const imageFileContent_t& a, const imageFileContent_t& b) noexcept {
    return R_ImageFileContentValid(a) && R_ImageFileContentValid(b) && a.kind == b.kind &&
        a.bytes == b.bytes && std::strlen(a.qpath) == std::strlen(b.qpath) &&
        std::memcmp(a.qpath,b.qpath,std::strlen(a.qpath)) == 0 && DigestEqual(a.digest,b.digest);
}
bool R_ImageBinaryContentEqual(const imageBinaryContent_t& a, const imageBinaryContent_t& b) noexcept {
    return a.version == 1 && b.version == 1 && DigestValid(a.digest) && DigestValid(b.digest) &&
        a.textureType == b.textureType && a.format == b.format && a.colorFormat == b.colorFormat &&
        a.width == b.width && a.height == b.height && a.levels == b.levels && a.layers == b.layers &&
        a.payloadBytes == b.payloadBytes && DigestEqual(a.digest,b.digest);
}
bool R_MakeImageFileContent(imageFileContentKind_t kind, const char* qpath,
    const void* data, std::size_t bytes, imageFileContent_t& out) noexcept {
    if ((kind != IFC_DIRECT_DDS && kind != IFC_OBSERVED_BIMAGE) || !R_ImageContentPathValid(qpath) ||
        !data || !bytes || bytes > IMAGE_CONTENT_MAX_BYTES) return false;
    imageFileContent_t value;
    value.kind = kind; value.bytes = bytes;
    std::memcpy(value.qpath,qpath,std::strlen(qpath)+1);
    idCrypto::SHA256(data,bytes,value.digest.bytes);
    if (!DigestValid(value.digest)) return false;
    out = value; return true;
}
bool R_MakeImageBinaryContent(const imageBinaryContent_t& header,
    const imageContentMipView_t* mips, std::size_t count, imageBinaryContent_t& out) noexcept {
    if (!mips || header.width < 1 || header.width > 32768 || header.height < 1 || header.height > 32768 ||
        header.levels < 1 || header.levels > 32 || (header.layers != 1 && header.layers != 6) ||
        (header.layers == 6 && header.width != header.height) ||
        count != std::size_t(header.levels * header.layers)) return false;
    const imageContentMipView_t* ordered[6][32]{};
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& mip = mips[i];
        if (!mip.data || mip.bytes <= 0 || mip.level < 0 || mip.level >= header.levels ||
            mip.layer < 0 || mip.layer >= header.layers || ordered[mip.layer][mip.level]) return false;
        const int width = header.width >> mip.level, height = header.height >> mip.level;
        if (mip.width != (width ? width : 1) || mip.height != (height ? height : 1) ||
            std::uint64_t(mip.bytes) > IMAGE_CONTENT_MAX_BYTES - total) return false;
        total += std::uint64_t(mip.bytes); ordered[mip.layer][mip.level] = &mip;
    }
    imageBinaryContent_t value = header;
    value.version = 1; value.payloadBytes = total;
    std::uint8_t canonical[16 + 8*4 + 6*32*(5*4+32)]{};
    std::uint8_t* cursor = canonical;
    const char domain[] = "OQ4IMAGEOUTPUT1";
    std::memcpy(cursor,domain,sizeof(domain)); cursor += 16;
    for (auto field : {value.version,std::uint32_t(value.textureType),std::uint32_t(value.format),
        std::uint32_t(value.colorFormat),std::uint32_t(value.width),std::uint32_t(value.height),
        std::uint32_t(value.levels),std::uint32_t(value.layers)}) Word(cursor,field);
    for (int layer = 0; layer < value.layers; ++layer) for (int level = 0; level < value.levels; ++level) {
        const auto* mip = ordered[layer][level]; if (!mip) return false;
        Word(cursor,std::uint32_t(layer)); Word(cursor,std::uint32_t(level));
        Word(cursor,std::uint32_t(mip->width)); Word(cursor,std::uint32_t(mip->height)); Word(cursor,std::uint32_t(mip->bytes));
        idCrypto::SHA256(mip->data,std::size_t(mip->bytes),cursor); cursor += 32;
    }
    idCrypto::SHA256(canonical,std::size_t(cursor-canonical),value.digest.bytes);
    if (!DigestValid(value.digest)) return false;
    out = value; return true;
}
