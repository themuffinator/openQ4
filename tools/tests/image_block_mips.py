#!/usr/bin/env python3
"""Exercise production edge padding and cube mip headers with complete blocks."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    begin = source.index(signature)
    end = source.index("\n/*", begin)
    return source[begin:end]


def main():
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    source = (ROOT / "src/imagetools/BinaryImage.cpp").read_text(encoding="utf-8")
    padding = function(source, "static void R_PadRGBAImageTo4x4Blocks(")
    cube_padding = function(source, "static void PadImageTo4x4(")
    cube = function(source, "bool idBinaryImage::LoadCubeFromMemory(")
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
static const int MAX_BINARY_IMAGE_DIMENSION=32768,MAX_BINARY_IMAGE_LEVELS=32,MAX_BINARY_IMAGE_DATA_SIZE=1<<30;
#include <cstdlib>
#include <cstring>
#include <vector>
using byte = unsigned char;
#define ALIGN16(declaration) alignas(16) declaration
template<class T> T Min(T a, T b) { return std::min(a, b); }
template<class T> T Max(T a, T b) { return std::max(a, b); }
enum textureFormat_t { FMT_DXT1, FMT_DXT5, FMT_RGBA8, FMT_OTHER };
enum { TT_CUBIC, CFM_DEFAULT };
static int mipAllocations = 0, gammaCalls = 0;
void* Mem_Alloc(size_t n) { ++mipAllocations; return std::malloc(n); }
void Mem_Free(void *p) { --mipAllocations; std::free(p); }
byte *R_MipMap(const byte *src, int width, int height) {
    int size = Max(1, width / 2) * Max(1, height / 2) * 4;
    byte *out = static_cast<byte *>(std::malloc(size));
    ++mipAllocations;
    for (int i = 0; i < size; i += 4) std::memcpy(out + i, src, 4);
    return out;
}
byte *R_MipMapWithGamma(const byte *src, int w, int h) {
    ++gammaCalls; return R_MipMap(src, w, h);
}
struct idDxtEncoder {
    void Compress(const byte *src, byte *dst, int w, int h, int divisor) {
        assert(w >= 4 && h >= 4 && w % 4 == 0 && h % 4 == 0);
        for (int i = 0; i < w * h * 4; ++i) assert(src[i] == src[i % 4]);
        std::memset(dst, src[0], w * h / divisor);
    }
    void CompressImageDXT1Fast(const byte *s, byte *d, int w, int h) { Compress(s, d, w, h, 2); }
    void CompressImageDXT5Fast(const byte *s, byte *d, int w, int h) { Compress(s, d, w, h, 1); }
};
struct idBinaryImageData {
    int level = -1, destZ = -1, width = -1, height = -1, dataSize = 0;
    byte *data = nullptr;
    std::vector<byte> owned;
    void Alloc(int size) { dataSize = size; owned.resize(size); data = owned.data(); }
};
template<class T> struct List : std::vector<T> {
    void SetNum(int count) { this->resize(count); }
};
struct idBinaryImage {
    struct Header { int textureType, colorFormat, width, height, numLevels; textureFormat_t format; } fileData{};
    List<idBinaryImageData> images;
    void Clear() { images.clear(); fileData = {}; }
    bool LoadCubeFromMemory(int, const byte *[6], int, textureFormat_t &, bool);
};
''' + padding + cube_padding + cube + r'''
int main() {
    // Every padded texel must retain the nearest source edge, including the
    // corners. Guard bytes and a source copy detect writes outside the target.
    for (int width = 1; width <= 9; ++width) for (int height = 1; height <= 9; ++height) {
        int pw = (width + 3) & ~3, ph = (height + 3) & ~3;
        std::vector<byte> src(width * height * 4), dst(pw * ph * 4 + 32, 0xa5);
        for (int i = 0; i < static_cast<int>(src.size()); ++i) src[i] = static_cast<byte>(i * 37 + 11);
        const auto saved = src;
        R_PadRGBAImageTo4x4Blocks(src.data(), width, height, dst.data() + 16, pw, ph);
        for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x) for (int c = 0; c < 4; ++c)
            assert(dst[16 + (y * pw + x) * 4 + c] == src[(Min(y, height - 1) * width + Min(x, width - 1)) * 4 + c]);
        for (int i = 0; i < 16; ++i) assert(dst[i] == 0xa5 && dst[dst.size() - 1 - i] == 0xa5);
        assert(src == saved);
    }
    // Execute the actual cube producer. The encoder is an observable stand-in:
    // it requires complete blocks while headers must keep logical mip sizes.
    // Production ETC compression is separately exercised with GLES readback.
    idBinaryImage image;
    for (int width : {1, 2, 4, 8}) for (auto inputFormat : {FMT_DXT1, FMT_DXT5, FMT_RGBA8, FMT_OTHER}) {
        for (bool gamma : {false, true}) {
            int levels = 1;
            for (int w = width; w > 1; w >>= 1) ++levels;
            std::vector<byte> faces[6];
            const byte *pics[6];
            for (int side = 0; side < 6; ++side) {
                faces[side].resize(width * width * 4);
                for (int i = 0; i < width * width * 4; ++i) faces[side][i] = byte(side * 20 + i % 4 + 1);
                pics[side] = faces[side].data();
            }
            int previousGamma = gammaCalls;
            auto format = inputFormat;
            assert(image.LoadCubeFromMemory(width, pics, levels, format, gamma));
            assert(mipAllocations == 0);
            assert(gammaCalls - previousGamma == (gamma ? 6 * (levels - 1) : 0));
            assert(image.fileData.width == width && image.fileData.height == width);
            assert(image.fileData.format == format && image.fileData.numLevels == levels);
            assert(format == (inputFormat == FMT_OTHER ? FMT_RGBA8 : inputFormat));
            assert(image.images.size() == static_cast<size_t>(levels * 6));
            for (int level = 0; level < levels; ++level) for (int side = 0; side < 6; ++side) {
                const auto &img = image.images[level * 6 + side];
                int logical = Max(1, width >> level), padded = Max(4, logical);
                assert(img.level == level && img.destZ == side);
                assert(img.width == logical && img.height == logical);
                int bytes = format == FMT_DXT1 ? padded * padded / 2 :
                            format == FMT_DXT5 ? padded * padded : logical * logical * 4;
                assert(img.dataSize == bytes);
                for (int i = 0; i < bytes; ++i)
                    assert(img.data[i] == byte(side * 20 + 1 + (format == FMT_RGBA8 ? i % 4 : 0)));
            }
            for (int side = 0; side < 6; ++side) for (int i = 0; i < width * width * 4; ++i)
                assert(faces[side][i] == byte(side * 20 + i % 4 + 1));
        }
    }
}
'''
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="image-block-mips-", dir=ROOT / ".tmp") as directory:
        work = Path(directory)
        cpp = work / "mips.cpp"
        executable = work / ("mips.exe" if os.name == "nt" else "mips")
        cpp.write_text(harness, encoding="utf-8")
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe:" + str(executable)]
        else:
            command = [compiler, "-std=c++17", str(cpp), "-o", str(executable)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, check=True)
    print("Image block mips: passed (81 padded dimensions; 32 complete cube chains, logical headers, source ownership)")


if __name__ == "__main__":
    main()
