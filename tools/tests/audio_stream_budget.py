#!/usr/bin/env python3
"""Execute production live-stream admission, off-thread block decoding and cleanup
with counted VFS/decoder ownership and a deferring job system."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def production_stream():
    """The production idSoundOggStream declaration and implementation."""
    header = (ROOT / 'src/sound/OpenAL/AL_SoundSample.h').read_text()
    declaration = header[header.index('struct idSoundOggStreamShape {'):header.index('class idSoundSample_OpenAL\n')]
    source = (ROOT / 'src/sound/OpenAL/AL_SoundSample.cpp').read_text()
    implementation = source[source.index('static uint64 soundStreamBytes'):source.index('/*\n========================\nRoQAudioDelta')]
    return declaration, implementation


def main():
    declaration, production = production_stream()
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "src/sound/SoundMemoryBudget.h"
#include "src/sound/SoundQueue.h"
#include "tools/tests/native/SoundStreamJobStubs.h"
#undef INT_MAX
using uint64=uint64_t;
template<class T> T Max(T a,T b){return std::max(a,b);}
template<class T> T Min(T a,T b){return std::min(a,b);}
struct idMath { static const int INT_MAX=2147483647; static int ClampInt(int a,int b,int c){return std::clamp(c,a,b);} };
idCVar s_streamMemoryMB("s_streamMemoryMB","8",0,"");
idCVar s_debugHardware("s_debugHardware","0",0,"");
static int warnings=0;
struct idLib { static void Printf(const char*,...){} static void Warning(const char*,...){++warnings;} };
static bool noFile=false,badDecoder=false,badSeek=false;
static SoundMemoryBudget aggregate;
static uint64 aggregateLimit=64*1024*1024;
static bool SoundMemoryAcquire(uint64 bytes) { return aggregate.Acquire(bytes,aggregateLimit); }
static void SoundMemoryRelease(uint64 bytes) { aggregate.Release(bytes); }
static int files=0,decoders=0,decoderOpens=0,seeks=0,reads=0;
enum fsOrigin_t { FS_SEEK_SET };
struct idFile {
    idFile(){++files;} ~idFile(){--files;}
    int Length(){return 64;} int Tell(){return 0;}
    int Seek(long,fsOrigin_t){return 0;} int Read(void*,int bytes){return bytes;}
};
struct VFS { idFile* OpenFileRead(const char*){return noFile?nullptr:new idFile;} void CloseFile(idFile* file){delete file;} } vfs;
static VFS* fileSystem=&vfs;
struct stb_vorbis_openq4_granule_page { unsigned page_start, page_end, granule; };
struct SoundVorbisStream {
    static constexpr int ArenaBytes=2*1024*1024, ChunkFrames=4096;
    char cache[64*1024];
    unsigned frames=22050, cursor=0;
    SoundVorbisStream(){++decoders;} ~SoundVorbisStream(){--decoders;}
    bool Open(int(*)(void*,unsigned,void*,int),void*,unsigned){++decoderOpens; cursor=0; return !badDecoder;}
    int Error(){return 1;}
    bool Seek(unsigned frame){++seeks; cursor=frame; return !badSeek;}
    bool SetCountedLength(unsigned counted,const stb_vorbis_openq4_granule_page& page){
        if(!counted || page.page_end<=page.page_start) return false; frames=counted; return true; }
    // Mono: each sample is its frame number, so a block shows where it was decoded from.
    int Read(short* out,int count){++reads; int n=std::min<int>(count,static_cast<int>(frames-cursor));
        for(int i=0;i<n;++i) out[i]=static_cast<short>((cursor+i)&0x7fff); cursor+=n; return n;}
    int Channels(){return 1;} int Rate(){return 22050;} unsigned Frames(){return frames;}
};
@DECLARATION@
@PRODUCTION@
static idSoundOggStreamShape Shape(unsigned frames=22050){ idSoundOggStreamShape s; s.channels=1; s.rate=22050; s.frames=frames; return s; }
static bool Starts(const idSoundOggStream& stream, unsigned frame){ const short* b=stream.Block(); return b && b[0]==static_cast<short>(frame&0x7fff); }
int main(){
    // Admission and the file open are main-thread work; the decoder opens with the first block.
    idSoundOggStream a,b,c,d;
    assert(a.Open("a",Shape()) && b.Open("b",Shape()) && c.Open("c",Shape()));
    assert(!d.Open("full",Shape()) && d.BlockState()==idSoundOggStream::BLOCK_FAILED);
    assert(files==3 && decoders==3 && soundStreamRejected==1 && decoderOpens==0);
    assert(soundStreamBytes==3*SOUND_STREAM_BYTES && soundStreamPeakBytes<=8*1024*1024);
    b.Close(); assert(d.Open("after stop",Shape()));
    a.Close(); c.Close(); d.Close();
    assert(files==0 && decoders==0 && soundStreamBytes==0);
    assert(aggregate.Used()==0);
    aggregateLimit=SOUND_STREAM_BYTES-1;
    assert(!a.Open("aggregate cap",Shape()) && files==0 && decoders==0 && aggregate.Used()==0);
    aggregateLimit=64*1024*1024;
    noFile=true; assert(!a.Open("missing",Shape())); noFile=false;
    assert(!a.Open("no shape",Shape(0)) && files==0);
    assert(files==0 && decoders==0 && soundStreamBytes==0);
    s_streamMemoryMB.SetInteger(32);
    { idSoundOggStream more[8]; for(auto& stream:more) assert(stream.Open("live",Shape()));
      s_streamMemoryMB.SetInteger(8);
      assert(!a.Open("budget reduced",Shape())); }
    assert(a.Open("retry",Shape()) && files==1 && decoders==1);
    assert(a.Open("replace",Shape()) && files==1 && decoders==1 && soundStreamBytes==SOUND_STREAM_BYTES);
    a.Close(); a.Close();
    assert(files==0 && decoders==0 && soundStreamBytes==0);

    // The first block opens, seeks and decodes on a worker; nothing runs on the caller.
    assert(a.Open("async",Shape()));
    assert(a.RequestBlock(0,4096) && a.IsBlockPending() && decoderOpens==0 && reads==0);
    assert(!a.RequestBlock(4096,4096) && a.PollBlock()==idSoundOggStream::BLOCK_PENDING && !a.Block());
    stubMilliseconds+=7;
    assert(StubRunPendingJobs()==1 && decoderOpens==1 && seeks==0);
    assert(a.PollBlock()==idSoundOggStream::BLOCK_READY && a.BlockFrames()==4096 && Starts(a,0));
    assert(soundStreamFirstBlocks==1 && soundStreamStartWaitMaxMs==7);
    a.ConsumeBlock();
    // Contiguous blocks keep the cursor; another position seeks exactly once.
    assert(a.RequestBlock(4096,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_READY && seeks==0 && Starts(a,4096));
    a.ConsumeBlock();
    assert(a.RequestBlock(11025,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_READY && seeks==1 && Starts(a,11025));
    a.ConsumeBlock();
    // The end of the stream gives a short block, and nothing beyond it is requested.
    assert(a.RequestBlock(20000,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_READY && a.BlockFrames()==2050);
    a.ConsumeBlock();
    assert(!a.RequestBlock(22050,1) && a.BlockState()==idSoundOggStream::BLOCK_FAILED);
    a.Close();
    assert(files==0 && decoders==0 && soundStreamBytes==0 && soundStreamFirstBlocks==1);

    // A block cancelled before it ran leaves the stream reusable at an exact cursor.
    assert(a.Open("cancelled",Shape()) && a.RequestBlock(0,4096));
    idSoundOggStream* released=new idSoundOggStream;
    assert(released->Open("released while pending",Shape()) && released->RequestBlock(8192,4096));
    const int opensBefore=decoderOpens;
    idSoundOggStream::Release(released);
    // Never waits: the worker still owns the stream, its file and its reservation.
    assert(idSoundOggStream::ReleasedCount()==1 && files==2 && soundStreamBytes==2*SOUND_STREAM_BYTES);
    idSoundOggStream::ReapReleased(false);
    assert(idSoundOggStream::ReleasedCount()==1);
    assert(StubRunPendingJobs()==2 && stubJobsCancelled==1 && decoderOpens==opensBefore+1);
    idSoundOggStream::ReapReleased(false);
    assert(idSoundOggStream::ReleasedCount()==0 && files==1 && soundStreamBytes==SOUND_STREAM_BYTES);
    assert(a.PollBlock()==idSoundOggStream::BLOCK_READY && Starts(a,0));
    a.Close();
    // Shutdown joins a released block that never got a worker.
    released=new idSoundOggStream;
    assert(released->Open("shutdown",Shape()) && released->RequestBlock(0,4096));
    idSoundOggStream::Release(released);
    idSoundOggStream::ReapReleased(true);
    assert(idSoundOggStream::ReleasedCount()==0 && stubPendingJobs.empty() && files==0 && decoders==0 && soundStreamBytes==0);
    // An idle stream is freed at once.
    released=new idSoundOggStream; assert(released->Open("idle",Shape()));
    idSoundOggStream::Release(released); assert(idSoundOggStream::ReleasedCount()==0 && files==0);

    // Damaged streams fail on the main thread's poll, with the warning printed there.
    badDecoder=true; assert(a.Open("corrupt",Shape()) && a.RequestBlock(0,4096));
    assert(StubRunPendingJobs()==1 && warnings==0 && a.PollBlock()==idSoundOggStream::BLOCK_FAILED && warnings==1);
    assert(!a.RequestBlock(0,4096)); badDecoder=false;
    assert(a.Open("header disagrees",Shape(44100)) && a.RequestBlock(0,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_FAILED);
    badSeek=true; assert(a.Open("seek",Shape()) && a.RequestBlock(100,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_FAILED); badSeek=false;
    // A counted length is applied by the decode; a bad page fails it.
    idSoundOggStreamShape counted=Shape(30000); counted.countedLength=true; counted.pageStart=0; counted.pageEnd=100; counted.pageGranule=11025;
    assert(a.Open("counted",counted) && a.RequestBlock(29000,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_READY && a.BlockFrames()==1000);
    counted.pageEnd=0; assert(a.Open("bad page",counted) && a.RequestBlock(0,4096) && StubRunPendingJobs()==1 && a.PollBlock()==idSoundOggStream::BLOCK_FAILED);
    assert(!a.Open("ok",Shape()) || a.RequestBlock(0,idSoundOggStream::BLOCK_SAMPLES+1)==false);
    a.Close();

    // Without accepting workers the block decodes inline, exactly as before.
    for(int mode=0; mode<4; ++mode){
        stubJobsSynchronous=mode==0; stubQueueFull=mode==1; stubJobsInitialized=mode!=2; s_asyncStreamDecode.SetBool(mode!=3);
        assert(a.Open("inline",Shape()) && a.RequestBlock(512,4096) && !a.IsBlockPending() && stubPendingJobs.empty());
        assert(a.PollBlock()==idSoundOggStream::BLOCK_READY && Starts(a,512));
        a.Close();
    }
    stubJobsSynchronous=stubQueueFull=false; stubJobsInitialized=true; s_asyncStreamDecode.SetBool(true);
    assert(files==0 && decoders==0 && soundStreamBytes==0 && aggregate.Used()==0);
    std::puts("PASS: stream byte budget, saturation, stop/retry, VFS/decoder failures, replacement, budget reduction, worker blocks, exact seeks, cancel/release/reap and inline fallback");
}
'''.replace('@DECLARATION@', declaration).replace('@PRODUCTION@', production)
    scratch = ROOT / '.tmp/audio/stream-budget'
    scratch.mkdir(parents=True, exist_ok=True)
    cpp, exe = scratch / 'test.cpp', scratch / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
