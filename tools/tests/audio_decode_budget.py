#!/usr/bin/env python3
"""Exercise production decode admission, join and cancellation without a device."""
from pathlib import Path
import os
import subprocess
from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'src/sound/OpenAL/AL_SoundSample.cpp').read_text()
    production = source[source.index('static bool SoundSample_AdmitDecode('):source.index('void idSoundSample_OpenAL::PrintDecodeMemoryInfo()')]
    production += body(source, 'void idSoundSample_OpenAL::FinishDecode()')
    production += body(source, 'void idSoundSample_OpenAL::CancelDecode()')
    harness = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "src/sound/SoundMemoryBudget.h"
using uint64 = uint64_t;
constexpr uint64 MiB = 1024 * 1024;
static void Check(bool ok) { if (!ok) std::abort(); }
template<class T> T Max(T a, T b) { return std::max(a, b); }
struct idMath { static int ClampInt(int a, int b, int c) { return std::clamp(c, a, b); } };
struct Cvar { int value=16; int GetInteger() const { return value; } } s_decodeMemoryMB;
static uint64 soundSamplePendingDecodeBytes=0, soundSampleDecodePeakBytes=0, soundSampleDecodeWaits=0;
static int soundSamplePendingDecodes=0, joined=0, cancelled=0, uploaded=0, reloaded=0, freed=0, closed=0;
struct JobList {
    void Wait() { Check(soundSamplePendingDecodeBytes > 0); ++joined; }
    void Cancel() { ++cancelled; }
};
struct idSoundSampleDecode {
    uint64 reservedBytes=0;
    std::unique_ptr<JobList> list;
    bool headerMismatch=false;
    unsigned decodedFrames=1;
    void* vorbis=nullptr;
    void* fileData=nullptr;
    unsigned char* envelope=nullptr;
};
static void FreeBuffer(void*) { ++freed; }
struct SoundVorbisStream { static constexpr uint64 ArenaBytes=2*MiB; };
static SoundMemoryBudget aggregate;
static void SoundMemoryRelease(uint64 bytes) { aggregate.Release(bytes); }
class SoundMemoryReservation {
    uint64 bytes=0;
public:
    ~SoundMemoryReservation() { aggregate.Release(bytes); }
    bool Reserve(uint64 count) { if(count<=bytes)return true; if(!aggregate.Acquire(count-bytes,64*MiB))return false;bytes=count;return true; }
    uint64 Transfer() { auto result=bytes;bytes=0;return result; }
};
static void stb_vorbis_close(void*) { ++closed; }
static void SoundSample_RunDecode(idSoundSampleDecode&) { Check(joined > 0); }
class idSoundSample_OpenAL {
public:
    idSoundSampleDecode* pendingDecode=nullptr;
    const char* pendingDecodeName="fixture";
    void FinishDecode();
    void CancelDecode();
    void LoadResource() { ++reloaded; Check(!pendingDecode); }
    void CreateOpenALBuffer() { ++uploaded; }
    bool FinishOggDecode(const char*, unsigned frames) { return frames > 0; }
};
template<class T> struct List : std::vector<T> { int Num() const { return int(this->size()); } };
struct System { List<idSoundSample_OpenAL*> samples; } soundSystemLocal;
''' + production + r'''
static void Submit(idSoundSample_OpenAL& sample, uint64 size) {
    SoundSampleDecodeReservation reservation;
    Check(reservation.Grow(size));
    sample.pendingDecode=new idSoundSampleDecode;
    sample.pendingDecode->reservedBytes=reservation.Transfer();
    sample.pendingDecode->list=std::make_unique<JobList>();
    ++soundSamplePendingDecodes;
}
int main() {
    idSoundSample_OpenAL a,b;
    soundSystemLocal.samples.push_back(&a); soundSystemLocal.samples.push_back(&b);
    {
        SoundSampleDecodeReservation input;
        Check(input.Grow(MiB));
        Check(!input.Grow(17*MiB));
        Check(soundSamplePendingDecodeBytes==MiB);
        Check(input.Grow(16*MiB));
        Check(!input.Grow(16*MiB+1));
    }
    Check(soundSamplePendingDecodeBytes==0);
    Submit(a,10*MiB);
    Check(soundSamplePendingDecodeBytes==10*MiB);
    // A 9 MiB second job joins/uploads the first before reserving memory.
    Submit(b,9*MiB);
    Check(a.pendingDecode==nullptr && uploaded==1 && joined==1);
    Check(soundSamplePendingDecodeBytes==9*MiB && soundSamplePendingDecodes==1);
    b.pendingDecode->vorbis=&a; b.pendingDecode->fileData=&a;
    b.CancelDecode(); b.CancelDecode();
    Check(cancelled==1 && joined==2 && freed==1 && closed==1);
    Check(soundSamplePendingDecodeBytes==0 && soundSamplePendingDecodes==0);
    Submit(a,MiB); a.pendingDecode->headerMismatch=true; a.FinishDecode();
    Check(reloaded==1 && soundSamplePendingDecodeBytes==0);
    Submit(a,MiB); a.pendingDecode->decodedFrames=0; a.FinishDecode();
    Check(reloaded==2 && soundSamplePendingDecodeBytes==0);
    Submit(a,MiB); a.FinishDecode(); a.FinishDecode();
    Check(uploaded==2 && soundSamplePendingDecodes==0);
    // A streamed track's envelope job joins without an upload or a reload.
    unsigned char envelope[4]={};
    Submit(a,MiB); a.pendingDecode->envelope=envelope; a.FinishDecode();
    Check(uploaded==2 && reloaded==2 && soundSamplePendingDecodeBytes==0 && soundSamplePendingDecodes==0);
    Submit(a,MiB); a.pendingDecode->envelope=envelope; a.pendingDecode->headerMismatch=true; a.FinishDecode();
    Check(uploaded==2 && reloaded==3 && soundSamplePendingDecodeBytes==0);
    Check(soundSampleDecodePeakBytes<=16*MiB && soundSampleDecodeWaits>=1);
    // Runtime budget reduction must join existing work before admitting more.
    s_decodeMemoryMB.value=32; Submit(a,24*MiB);
    s_decodeMemoryMB.value=16; Submit(b,MiB);
    Check(a.pendingDecode==nullptr && soundSamplePendingDecodeBytes==MiB);
    b.CancelDecode();
    Check(soundSamplePendingDecodeBytes==0);
    std::puts("PASS: byte admission, backpressure, retained reservations, join/cancel and failed-decode recovery");
}
'''
    # Windows and WSL may validate this checkout concurrently; never replace a
    # running executable (or its generated source) with the other platform's.
    scratch = ROOT / '.tmp/audio/decode-budget' / os.name
    scratch.mkdir(parents=True, exist_ok=True)
    cpp, exe = scratch / 'test.cpp', scratch / 'test.exe'
    cpp.write_text(harness)
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(exe)]
    subprocess.run(command, cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)

    # The budget model alone cannot prove cancellation of a worker already inside
    # the codec. Execute the production loops on the real portable job system.
    decode = body(source, 'struct idSoundSampleDecode\n') + ';\n'
    decode += body(source, 'static void SoundSample_AccumulateEnvelope(') + '\n'
    decode += body(source, 'static void SoundSample_DecodeFrames(') + '\n'
    decode += 'static void SoundSample_RunDecode(idSoundSampleDecode&, const idJobContext* = nullptr);\n'
    decode += body(source, 'static void SoundSample_RunDecode( idSoundSampleDecode& decode, const idJobContext* context )') + '\n'
    decode += body(source, 'static void SoundSample_DecodeJob( const idJobContext& context )\n') + '\n'
    join = body(source, 'void idSoundSample_OpenAL::FinishDecode()') + '\n'
    join += body(source, 'void idSoundSample_OpenAL::CancelDecode()')
    cpp = scratch / 'worker.cpp'
    cpp.write_text((ROOT / 'tools/tests/native/SoundSampleDecodeTest.cpp').read_text()
                   .replace('// @DECODE@', decode).replace('// @JOIN@', join))
    exe = scratch / 'worker.exe'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT), str(cpp), str(ROOT / 'src/framework/ParallelJobSystem.cpp'), '-o', str(exe)]
    if os.name != 'nt':
        command += ['-pthread']
    subprocess.run(command, cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True, timeout=30)

    system = (ROOT / 'src/sound/snd_system.cpp').read_text()
    cpp = scratch / 'trim.cpp'
    cpp.write_text((ROOT / 'tools/tests/native/SoundMemoryTrimRuntimeTest.cpp').read_text().replace(
        '// @PRODUCTION@', body(system, 'void idSoundSystemLocal::TrimMemory(') + '\n' +
        body(system, 'void idSoundSystemLocal::EndLevelLoad(') + '\n' +
        body(source, 'static void SoundMemoryReclaim() {')))
    exe = scratch / 'trim.exe'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT), str(cpp), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
