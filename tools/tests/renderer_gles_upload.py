#!/usr/bin/env python3
"""Compile the production image upload against a validating GLES transfer mock.

Checks padded compressed mip rectangles and pitched RGB565 uploads, including
the exact source bytes the driver reads after applying unpack row alignment.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required (set CXX or use the openQ4 developer shell)")
    source = (ROOT / "src/renderer/OpenGL/gl_Image.cpp").read_text(encoding="utf-8")
    start = source.index("void idImage::SubImageUpload(")
    production = source[start:source.index("\n/*", start)]
    # The layered (TT_3D / TT_2D_ARRAY) upload path binds through the
    # production target mapping, so the harness compiles that too.
    target_start = source.index("static GLenum R_GLTextureTarget(")
    target_mapping = source[target_start:source.index("\n}\n", target_start) + 3]
    harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
#define GL_APICALL
#define OPENQ4_RENDERER_GLES_MODULE
#define ID_INLINE inline
#include "src/renderer/GLES/qgl_gles.h"
#include "src/renderer/ImageOpts.h"
using byte = unsigned char;
static bool R_ImagePolicyOperationAllowed() { return true; }
static bool R_ImagePolicyActive() { return false; }
struct imageConsumedLoad_t { static bool Active(const void*) { return false; } };
struct idImageUploadErrorScope { static bool Defer() { return false; } };
static void GL_CheckErrors() {}
struct renderImageOperation_t {
    template<class... T> explicit renderImageOperation_t(T...) {}
    bool Allowed() const { return true; }
    void Succeeded() {}
};
template<class T> T Max(T a, T b) { return std::max(a, b); }
static size_t sourceBytes;
template<class T> struct idTempArray {
    std::vector<T> data;
    explicit idTempArray(unsigned int count) : data(count) { sourceBytes = data.size() * sizeof(T); }
    T *Ptr() { return data.data(); }
};
static bool Swap_IsBigEndian() { return false; }
static int rowLength, alignment = 4, uploadedWidth, uploadedHeight, uploadedSize;
static int uploadedZ = -1, uploadCalls;
static GLenum uploadedTarget, boundTarget;
static std::vector<byte> uploaded;
static void R_BindTextureForDirectAccess(GLenum target, GLuint) { boundTarget = target; }
int BitsForFormat(textureFormat_t format) { assert(format == FMT_RGB565); return 16; }
static int R_CompressedTextureSizeInBytes(textureFormat_t, int w, int h) {
    return ((w + 3) / 4) * ((h + 3) / 4) * 8;
}
extern "C" void GL_APIENTRY glPixelStorei(GLenum name, GLint value) {
    if (name == GL_UNPACK_ROW_LENGTH) rowLength = value;
    else if (name == GL_UNPACK_ALIGNMENT) alignment = value;
    else assert(false);
}
extern "C" void GL_APIENTRY glCompressedTexSubImage2D(GLenum, GLint, GLint, GLint,
        GLsizei width, GLsizei height, GLenum, GLsizei size, const void *) {
    uploadedWidth = width; uploadedHeight = height; uploadedSize = size; uploadedZ = -1; ++uploadCalls;
}
extern "C" void GL_APIENTRY glCompressedTexSubImage3D(GLenum target, GLint, GLint, GLint, GLint z,
        GLsizei width, GLsizei height, GLsizei depth, GLenum, GLsizei size, const void *) {
    assert(depth == 1);
    uploadedTarget = target; uploadedWidth = width; uploadedHeight = height; uploadedSize = size;
    uploadedZ = z; ++uploadCalls;
}
extern "C" void GL_APIENTRY glTexSubImage2D(GLenum, GLint, GLint, GLint,
        GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data) {
    assert(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5);
    int stride = (rowLength ? rowLength : width) * 2;
    stride = (stride + alignment - 1) / alignment * alignment;
    assert(size_t(stride * (height - 1) + width * 2) <= sourceBytes);
    uploaded.clear();
    for (int row = 0; row < height; ++row) {
        const byte *start = static_cast<const byte *>(data) + row * stride;
        uploaded.insert(uploaded.end(), start, start + width * 2);
    }
    uploadedZ = -1; ++uploadCalls;
}
extern "C" void GL_APIENTRY glTexSubImage3D(GLenum target, GLint, GLint, GLint, GLint z,
        GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *data) {
    assert(depth == 1);
    glTexSubImage2D(target, 0, 0, 0, width, height, format, type, data);
    uploadedTarget = target; uploadedZ = z;
}
struct idImage {
    idImageOpts opts;
    GLuint texnum = 1;
    GLenum internalFormat = GL_COMPRESSED_RGB8_ETC2, dataFormat = GL_RGB, dataType = GL_UNSIGNED_SHORT_5_6_5;
    bool IsCompressed() const { return opts.format != FMT_RGB565; }
    void SubImageUpload(int, int, int, int, int, int, const void *, int = 0) const;
};
''' + target_mapping + production + r'''
int main() {
    idImage image;
    image.opts.width = image.opts.height = 8;
    image.opts.numLevels = 4;
    byte block[8] = {};
    for (textureFormat_t format : {FMT_DXT1, FMT_ETC2_RGB8}) {
        image.opts.format = format;
        image.opts.textureType = TT_CUBIC;
        for (int level : {2, 3}) {
            image.SubImageUpload(level, 0, 0, 0, 4, 4, block);
            assert(uploadedWidth == (8 >> level) && uploadedHeight == (8 >> level) && uploadedSize == 8);
        }
    }
    image.opts.textureType = TT_2D;
    image.opts.width = 9; image.opts.height = 5;
    image.SubImageUpload(0, 8, 4, 0, 4, 4, block);
    assert(uploadedWidth == 1 && uploadedHeight == 1 && uploadedSize == 8);

    image.opts.format = FMT_RGB565;
    image.opts.width = image.opts.height = 2;
    // Source RGB565 is big-endian. The third pixel is row padding, and the
    // final row contains only the two pixels actually consumed by GL.
    const byte pitched[] = {0xf8,0x00, 0x07,0xe0, 0xab,0xcd, 0x00,0x1f, 0xff,0xff};
    const std::vector<byte> expected = {0x00,0xf8, 0xe0,0x07, 0x1f,0x00, 0xff,0xff};
    image.SubImageUpload(0, 0, 0, 0, 2, 2, pitched, 3);
    assert(uploaded == expected && rowLength == 0);
    const byte tight[] = {0xf8,0x00, 0x07,0xe0, 0x00,0x1f, 0xff,0xff};
    image.SubImageUpload(0, 0, 0, 0, 2, 2, tight);
    assert(uploaded == expected);

    // Layered images upload one slice or layer per call through the 3D entry
    // points, bound to the layered target; a z past the level's depth is a no-op.
    image.opts.textureType = TT_2D_ARRAY;
    image.opts.depth = 3;
    image.SubImageUpload(0, 0, 0, 2, 2, 2, tight);
    assert(uploaded == expected && uploadedZ == 2);
    assert(uploadedTarget == GL_TEXTURE_2D_ARRAY && boundTarget == GL_TEXTURE_2D_ARRAY);
    int calls = uploadCalls;
    image.SubImageUpload(0, 0, 0, 3, 2, 2, tight);
    assert(uploadCalls == calls);
    image.opts.textureType = TT_3D;
    image.opts.format = FMT_DXT1;
    image.opts.width = image.opts.height = 8;
    image.opts.depth = 4;
    image.SubImageUpload(2, 0, 0, 0, 4, 4, block);
    assert(uploadedZ == 0 && uploadedTarget == GL_TEXTURE_3D && boundTarget == GL_TEXTURE_3D);
    assert(uploadedWidth == 2 && uploadedHeight == 2 && uploadedSize == 8);
    calls = uploadCalls;
    image.SubImageUpload(2, 0, 0, 1, 4, 4, block); // level 2 of a 4-deep volume holds one slice
    assert(uploadCalls == calls);
    puts("GLES image upload regression: passed (compressed mip/edge bounds, pitched/tight RGB565 bytes, layered slices)");
}
'''
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="renderer-gles-upload-", dir=temporary_root) as directory:
        work = Path(directory)
        cpp = work / "upload.cpp"
        executable = work / ("upload.exe" if os.name == "nt" else "upload")
        cpp.write_text(harness, encoding="utf-8")
        includes = [ROOT, ROOT / "src/external/gles"]
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/EHsc", "/std:c++17"] + ["/I" + str(path) for path in includes]
            command += [str(cpp), "/Fe:" + str(executable)]
        else:
            command = [compiler, "-std=c++17"]
            for path in includes:
                command += ["-I", str(path)]
            command += [str(cpp), "-o", str(executable)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, check=True)


if __name__ == "__main__":
    main()
