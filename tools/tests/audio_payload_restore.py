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
    voice_source = (ROOT / "src/sound/OpenAL/AL_SoundVoice.cpp").read_text(encoding="utf-8")
    begin = voice_source.index("int idSoundVoice_OpenAL::RestartAt( int offsetSamples )")
    end = voice_source.index("\n/*", begin)
    restart = voice_source[begin:end]
    harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
struct CVar { bool GetBool() const { return false; } } s_debugHardware;
int Sys_Milliseconds() { return 0; }
struct idLib { static void Printf(const char *, ...) {} };
template<class T> struct List : std::vector<T> {
    int Num() const { return static_cast<int>(this->size()); }
    const T *Ptr() const { return this->data(); }
};
static int loads = 0, liveBytes = 0, stoppedOriginal = 0;
static bool missing = false, changedFormat = false, changedSize = false;
class idSoundSample_OpenAL {
public:
    struct Buffer { unsigned char *buffer; int numSamples; int bufferSize; };
    using sampleBuffer_t = Buffer;
    struct Format { int rate = 44100; } format;
    bool payloadReleased = true, keepPayload = false, loaded = true;
    bool original = false;
    unsigned openalBuffer = 42;
    int playBegin = 0, playLength = 16, totalBufferSize = 32;
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
template<class T> T Max(T a, T b) { return a > b ? a : b; }
constexpr int OPENAL_RESTART_SAMPLE_ALIGNMENT = 128;
enum { AL_BUFFER, AL_LOOPING, AL_SAMPLE_OFFSET, AL_FALSE, AL_TRUE, AL_NO_ERROR };
static int staticBuffer = 0;
void alSourcei(unsigned, int property, int value) {
    if (property == AL_BUFFER) staticBuffer = value;
}
int CheckALErrors() { return AL_NO_ERROR; }
class idSoundVoice_OpenAL {
public:
    idSoundSample_OpenAL *leadinSample = nullptr, *loopingSample = nullptr;
    idSoundSample_OpenAL *nextQueuedSample = nullptr;
    int nextQueuedBuffer = 0, nextQueuedOffset = 0;
    unsigned openalSource = 1;
    int streamingAllocations = 0;
    void ResetQueuedBufferState() { nextQueuedSample = nullptr; }
    void FlushSourceBuffers() {}
    bool EnsureStreamingBuffers() { ++streamingAllocations; return true; }
    bool GetPlayableBufferRange(idSoundSample_OpenAL *sample, int,
                               int &start, int &count, int &offset, int &playable) {
        start = 0; count = sample->playLength;
        offset = sample->playBegin; playable = sample->playLength;
        return true;
    }
    int RestartAt(int offsetSamples);
};
''' + restart + r'''
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
    // Uploaded intro/loop samples remain playable even after their source
    // files disappear. Queueing OpenAL's existing buffers needs no CPU decode.
    idSoundSample_OpenAL intro, loop;
    idSoundVoice_OpenAL voice;
    voice.leadinSample = &intro; voice.loopingSample = &loop;
    missing = true; changedFormat = changedSize = false;
    int priorLoads = loads;
    if (voice.RestartAt(0) != 1 || voice.nextQueuedSample != &intro || loads != priorLoads) return 10;
    if (voice.RestartAt(128) != 32 || loads != priorLoads || staticBuffer != 42) return 11;
    if (voice.streamingAllocations != 0 || !intro.payloadReleased || !loop.payloadReleased) return 12;

    // A sample with no uploaded OpenAL buffer still restores its CPU payload
    // before entering the queued streaming path, and fails if restoration fails.
    voice.loopingSample = nullptr; intro.openalBuffer = 0;
    if (voice.RestartAt(0) != 0 || loads != priorLoads + 1) return 13;
    missing = false;
    if (voice.RestartAt(0) != 1 || loads != priorLoads + 2 || voice.streamingAllocations != 1) return 14;
    if (intro.payloadReleased || !intro.buffers[0].buffer || intro.openalBuffer != 0) return 15;
    intro.FreeData();
    assert(liveBytes == 0);
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
    print("Audio payload restore: passed (live voices, intro/loop queues, streaming, ownership, changed/missing source)")


if __name__ == "__main__":
    main()
