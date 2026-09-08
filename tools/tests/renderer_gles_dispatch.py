#!/usr/bin/env python3
"""Exercise the actual GLES dispatch resolver against mocked driver contexts.

A driver can export ES 3.1 functions while the active context only supports
ES 3.0. This compiles the production resolver and verifies that exports alone
cannot enable those paths, including after context recreation.
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
    source = (ROOT / "src/renderer/GLES/gles_GLStubs.cpp").read_text(encoding="utf-8")
    production = source[source.index("PFN_glBindVertexBuffer"):source.index("\n/*\n===============================================================================\n\tGLEW initialisation shim.")]
    harness = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#define GL_APICALL
#include "src/renderer/GLES/qgl_gles.h"
static int majorVersion = 3;
static int minorVersion = 0;
static std::vector<std::string> extensions;
static std::vector<std::string> lookups;
extern "C" void GL_APIENTRY glGetIntegerv(GLenum name, GLint *value) {
    if (name == GL_MAJOR_VERSION) *value = majorVersion;
    else if (name == GL_MINOR_VERSION) *value = minorVersion;
    else if (name == GL_NUM_EXTENSIONS) *value = (GLint)extensions.size();
    else assert(false);
}
extern "C" const GLubyte *GL_APIENTRY glGetStringi(GLenum name, GLuint index) {
    assert(name == GL_EXTENSIONS && index < extensions.size());
    return reinterpret_cast<const GLubyte *>(extensions[index].c_str());
}
static void exportedFunction() {}
void *GLimp_ExtensionPointer(const char *name) {
    lookups.push_back(name);
    return reinterpret_cast<void *>(&exportedFunction);
}
''' + production + r'''
static bool lookedUp(const char *name) {
    for (const std::string &lookup : lookups) if (lookup == name) return true;
    return false;
}
int main() {
    // All symbols exist, but an ES 3.0 context advertises none of them.
    GLES_ResolveOptionalEntryPoints();
    assert(lookups.empty());
    assert(!glDispatchCompute && !glBindVertexBuffer && !glBufferStorage);
    assert(!glQueryCounter && !glDebugMessageCallback);

    minorVersion = 1;
    GLES_ResolveOptionalEntryPoints();
    assert(glDispatchCompute && glBindVertexBuffer && glGetProgramResourceIndex);
    assert(!glBufferStorage && !glDebugMessageCallback);

    // Extension names are exact tokens, never prefix matches.
    minorVersion = 0;
    extensions = {"GL_EXT_buffer_storage_extra", "GL_KHR_debug_extra"};
    lookups.clear();
    GLES_ResolveOptionalEntryPoints();
    assert(lookups.empty() && !glDispatchCompute && !glBindVertexBuffer);

    extensions = {"GL_EXT_buffer_storage", "GL_EXT_multi_draw_indirect",
                  "GL_EXT_disjoint_timer_query", "GL_KHR_debug"};
    GLES_ResolveOptionalEntryPoints();
    assert(glBufferStorage && glMultiDrawElementsIndirect && glQueryCounter);
    assert(glDebugMessageCallback && lookedUp("glDebugMessageCallbackKHR"));
    assert(!glDispatchCompute && !glBindVertexBuffer);

    extensions.clear();
    minorVersion = 2;
    lookups.clear();
    GLES_ResolveOptionalEntryPoints();
    assert(glDebugMessageCallback && lookedUp("glDebugMessageCallback"));
    assert(!lookedUp("glDebugMessageCallbackKHR"));
    assert(!glBufferStorage && !glQueryCounter);

    // Recreating a baseline context drops every prior optional binding.
    minorVersion = 0;
    lookups.clear();
    GLES_ResolveOptionalEntryPoints();
    assert(lookups.empty() && !glDispatchCompute && !glDebugMessageCallback);
    assert(!glGetBufferSubData && !glCreateTextures && !glBindSamplers);
    return 0;
}
'''
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="renderer-gles-dispatch-", dir=temporary_root) as directory:
        work = Path(directory)
        cpp = work / "dispatch.cpp"
        executable = work / ("dispatch.exe" if os.name == "nt" else "dispatch")
        cpp.write_text(harness, encoding="utf-8")
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/EHsc", "/std:c++17", "/I" + str(ROOT),
                       "/I" + str(ROOT / "src/external/gles"), str(cpp), "/Fe:" + str(executable)]
        else:
            command = [compiler, "-std=c++17", "-I", str(ROOT), "-I", str(ROOT / "src/external/gles"),
                       str(cpp), "-o", str(executable)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, check=True)
    print("GLES dispatch regression: passed (ES 3.0/3.1/3.2, extension tokens, context recreation)")


if __name__ == "__main__":
    main()
