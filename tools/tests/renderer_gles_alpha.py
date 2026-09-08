#!/usr/bin/env python3
"""Exercise production GLES alpha state, optionally rendering its shader with EGL.

The default native harness needs only a C++ compiler. On a Linux EGL/GLES
development host, --gpu also draws the embedded material shaders into a 1x1
pbuffer and checks the fragments retained at every comparison boundary:
    EGL_PLATFORM=surfaceless python3 tools/tests/renderer_gles_alpha.py --gpu
No game, display capture or input events are involved.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gpu", action="store_true", help="require real EGL/GLES shader execution")
    args = parser.parse_args()
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required (set CXX or use the openQ4 developer shell)")
    source = (ROOT / "src/renderer/GLES_D3/gles_draw.cpp").read_text(encoding="utf-8")
    start = source.index("float R_GLESD3_AlphaTestReference(")
    end = source.index("\n/*", start)
    production = source[start:end]
    state_header = (ROOT / "src/renderer/tr_local.h").read_text(encoding="utf-8")
    state_defines = "\n".join(line for line in state_header.splitlines() if line.startswith("const int GLS_ATEST"))
    harness = r'''
#include <cassert>
#include <cstdio>
#include <string>
#ifdef TEST_GPU
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "src/renderer/GLES_D3/glsl/glsl_shaders.h"
#else
#define GL_APICALL
#include "src/renderer/GLES/qgl_gles.h"
static float recordedReference;
static int recordedFunction;
extern "C" void GL_APIENTRY glUniform1f(GLint location, GLfloat value) {
    assert(location == 1); recordedReference = value;
}
extern "C" void GL_APIENTRY glUniform1i(GLint location, GLint value) {
    assert(location == 2); recordedFunction = value;
}
#endif
struct glesProgram_t { GLint uAlphaTest, uAlphaTestFunc; };
''' + state_defines + "\n" + production + r'''

#ifdef TEST_GPU
static GLuint compile(GLenum type, const char *source) {
    std::string text(source);
    text.insert(text.find('\n') + 1, "#define GLESD3_ALPHATEST 1\n");
    source = text.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[8192]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); fprintf(stderr, "%s\n", log); }
    assert(ok);
    return shader;
}
#endif

int main() {
#ifdef TEST_GPU
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    assert(eglInitialize(display, &major, &minor));
    const EGLint configAttrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    EGLConfig config;
    assert(eglChooseConfig(display, configAttrs, &config, 1, &count) && count);
    const EGLint contextAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    const EGLint surfaceAttrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    assert(eglMakeCurrent(display, surface, surface, context));
    printf("%s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    GLuint vp = compile(GL_VERTEX_SHADER, glesMaterialShaderVP);
    GLuint fp = compile(GL_FRAGMENT_SHADER, glesMaterialShaderFP);
    GLuint handle = glCreateProgram();
    glAttachShader(handle, vp); glAttachShader(handle, fp); glLinkProgram(handle);
    GLint linked;
    glGetProgramiv(handle, GL_LINK_STATUS, &linked);
    assert(linked);
    glUseProgram(handle);
    glesProgram_t program = { glGetUniformLocation(handle, "uAlphaTest"), glGetUniformLocation(handle, "uAlphaTestFunc") };
    assert(program.uAlphaTest >= 0 && program.uAlphaTestFunc >= 0);
    const float identity[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(handle, "uMVP"), 1, GL_FALSE, identity);
    glUniform4f(glGetUniformLocation(handle, "uTexMatrixS"), 1, 0, 0, 0);
    glUniform4f(glGetUniformLocation(handle, "uTexMatrixT"), 0, 1, 0, 0);
    glUniform4f(glGetUniformLocation(handle, "uVertexColor"), 0, 1, 0, 1);
    glUniform1i(glGetUniformLocation(handle, "uTexture0"), 0);
    const GLint colorLocation = glGetUniformLocation(handle, "uColor");
    GLuint texture, buffer;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char white[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const float vertices[] = {-1,-1,0, 3,-1,0, -1,3,0};
    glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glViewport(0, 0, 1, 1);
#else
    glesProgram_t program = {1, 2};
#endif
    struct Test { int state; GLenum comparison; float reference; };
    const Test tests[] = { {0, GL_ALWAYS, -1}, {GLS_ATEST_EQ_255, GL_EQUAL, 1},
        {GLS_ATEST_LT_128, GL_LESS, .5f}, {GLS_ATEST_GE_128, GL_GEQUAL, .5f} };
    int checked = 0;
    for (const Test &test : tests) {
        R_GLESD3_SetAlphaTest(&program, test.state);
#ifdef TEST_GPU
        GLfloat reference; GLint comparison;
        glGetUniformfv(handle, program.uAlphaTest, &reference);
        glGetUniformiv(handle, program.uAlphaTestFunc, &comparison);
#else
        float reference = recordedReference; int comparison = recordedFunction;
#endif
        assert(reference == test.reference && comparison == (GLint)test.comparison);
#ifdef TEST_GPU
        for (float alpha : {-.25f, 0.f, .25f, .5f, .75f, 254.f/255.f, 1.f, 2.f}) {
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
            glUniform4f(colorLocation, 1, 1, 1, alpha);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unsigned char pixel[4] = {};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            assert(glGetError() == GL_NO_ERROR);
            // Independent fixed-function contract, with color clamped before the test.
            bool expected = test.comparison == GL_ALWAYS ||
                (test.comparison == GL_EQUAL && alpha >= 1.f) ||
                (test.comparison == GL_LESS && alpha < .5f) ||
                (test.comparison == GL_GEQUAL && alpha >= .5f);
            if ((pixel[0] != 0) != expected) {
                fprintf(stderr, "comparison=%d alpha=%f pixel=%d\n", comparison, alpha, pixel[0]);
                return 1;
            }
            ++checked;
        }
#else
        ++checked;
#endif
    }
#ifdef TEST_GPU
    // Arbitrary material depth cutouts use strict GL_GREATER, unlike GE_128.
    glUniform1i(program.uAlphaTestFunc, GL_GREATER);
    for (float reference : {0.f, .5f, 1.f}) {
        glUniform1f(program.uAlphaTest, reference);
        for (float alpha : {0.f, .25f, .5f, .75f, 1.f}) {
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform4f(colorLocation, 1, 1, 1, alpha);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unsigned char pixel[4] = {};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            assert(glGetError() == GL_NO_ERROR && ((pixel[0] != 0) == (alpha > reference)));
            ++checked;
        }
    }
    glDeleteBuffers(1, &buffer); glDeleteTextures(1, &texture);
    glDeleteProgram(handle); glDeleteShader(vp); glDeleteShader(fp);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
#endif
    printf("GLES alpha regression: passed (%d cases)\n", checked);
}
'''
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="renderer-gles-alpha-", dir=temporary_root) as directory:
        work = Path(directory)
        cpp = work / "alpha.cpp"
        executable = work / ("alpha.exe" if os.name == "nt" else "alpha")
        cpp.write_text(harness, encoding="utf-8")
        includes = [ROOT, ROOT / "src/external/gles"]
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            if args.gpu:
                raise SystemExit("--gpu requires a Linux EGL/GLES development host")
            command = [compiler, "/nologo", "/EHsc", "/std:c++17"] + ["/I" + str(path) for path in includes]
            command += [str(cpp), "/Fe:" + str(executable)]
        else:
            command = [compiler, "-std=c++17"]
            for path in includes:
                command += ["-I", str(path)]
            command += [str(cpp), "-o", str(executable)]
            if args.gpu:
                command += ["-DTEST_GPU", str(ROOT / "src/renderer/GLES_D3/glsl/materialShaderVP.cpp"),
                            str(ROOT / "src/renderer/GLES_D3/glsl/materialShaderFP.cpp"), "-lEGL", "-lGLESv2"]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, check=True)


if __name__ == "__main__":
    main()
