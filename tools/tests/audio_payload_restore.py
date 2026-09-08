#!/usr/bin/env python3
"""Run the production payload restore method with observable audio lifetimes."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    source = (ROOT / "src/sound/OpenAL/AL_SoundSample.cpp").read_text(encoding="utf-8")
    begin = source.index("bool idSoundSample_OpenAL::EnsureCpuPayload()")
    end = source.index("\n/*", begin)
    production = source[begin:end]
    harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
struct CVar { bool GetBool() const { return false; } } s_debugHardware;
int Sys_Milliseconds() { return 0; }
struct idLib { static void Printf(const char *, ...) {} };
template<class T> struct List : std::vector<T> {
    int Num() const { return static_cast<int>(this->size()); }
};
static int loads = 0, liveBytes = 0, stoppedOriginal = 0;
static bool missing = false, changedFormat = false, changedSize = false;
class idSoundSample_OpenAL {
public:
    struct Buffer { unsigned char *buffer; int numSamples; int bufferSize; };
    struct Format { int rate = 44100; } format;
    bool payloadReleased = true, keepPayload = false, loaded = true;
    bool original = false;
    unsigned openalBuffer = 42;
    int playBegin = 0, playLength = 16;
    List<Buffer> buffers;
    const char *name = "sample";
    idSoundSample_OpenAL() { buffers.push_back({nullptr, 16, 32}); }
    void SetName(const char *n) { name = n; }
    const char *GetName() const { return name; }
    void FreeData() {
        if (original) ++stoppedOriginal;
        for (auto &b : buffers) if (b.buffer) { delete[] b.buffer; --liveBytes; }
        buffers.clear(); openalBuffer = 0; loaded = false;
    }
    void LoadResource() {
        ++loads;
        FreeData();
        loaded = !missing;
        format.rate = changedFormat ? 22050 : 44100;
        buffers.push_back({new unsigned char[32], 16, changedSize ? 20 : 32});
        ++liveBytes;
        payloadReleased = false;
        openalBuffer = 77;
    }
    bool EnsureCpuPayload();
};
''' + production + r'''
int main() {
    idSoundSample_OpenAL s;
    s.original = true;
    assert(s.EnsureCpuPayload());
    assert(s.openalBuffer == 42 && stoppedOriginal == 0);
    assert(s.buffers[0].buffer && !s.payloadReleased && s.keepPayload);
    assert(liveBytes == 1 && loads == 1);
    assert(s.EnsureCpuPayload() && loads == 1);
    s.original = false; s.FreeData();
    assert(liveBytes == 0);
    for (int failure = 0; failure < 3; ++failure) {
        missing = failure == 0; changedFormat = failure == 1; changedSize = failure == 2;
        idSoundSample_OpenAL f;
        f.original = true;
        assert(!f.EnsureCpuPayload());
        assert(f.openalBuffer == 42 && f.payloadReleased && stoppedOriginal == 0);
        assert(f.format.rate == 44100 && f.buffers[0].bufferSize == 32);
        assert(f.buffers[0].buffer == nullptr && liveBytes == 0);
    }
}
'''
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="audio-payload-", dir=ROOT / ".tmp") as directory:
        work = Path(directory)
        cpp = work / "restore.cpp"
        executable = work / ("restore.exe" if os.name == "nt" else "restore")
        cpp.write_text(harness, encoding="utf-8")
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe:" + str(executable)]
        else:
            command = [compiler, "-std=c++17", str(cpp), "-o", str(executable)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, check=True)
    print("Audio payload restore: passed (live voices, ownership, changed/missing source, repeat reads)")


if __name__ == "__main__":
    main()
