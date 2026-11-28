#!/usr/bin/env python3
"""Exercise aggregate admission, actual allocation ownership and the RoQ loader."""
from pathlib import Path
import os
import subprocess
from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'src/sound/OpenAL/AL_SoundSample.cpp').read_text()
    admission = source[source.index('static SoundMemoryBudget soundMemory;'):source.index('idCVar s_streamThresholdKB')]
    allocation = source[source.index('struct alignas(16) SoundAllocationHeader'):source.index('template<class type>')]
    roq = body(source, 'bool idSoundSample_OpenAL::LoadRoQ(')
    harness = r'''
#include "src/sound/SoundMemoryBudget.h"
#include "src/sound/SoundMemoryClass.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#undef INT_MAX
using uint64=std::uint64_t; using byte=unsigned char;
using int16=std::int16_t; using uint16=std::uint16_t; using int32=std::int32_t;
using int8=std::int8_t;using uint8=std::uint8_t;using uint32=std::uint32_t;
constexpr int MAX_HARDWARE_VOICES=48,MAX_QUEUED_BUFFERS=3;
constexpr uint64 MiB=1024*1024;
template<class T> T Min(T a,T b){return std::min(a,b);}
struct idMath { static constexpr int INT_MAX=2147483647; static int ClampInt(int a,int b,int c){return std::clamp(c,a,b);} };
struct Cvar { int value=32; int GetInteger()const{return value;} } s_audioMemoryMB;
struct NameCvar { const char* value="auto"; const char* GetString()const{return value;} } s_audioMemoryClass;
static int systemRam=16384, ramQueries=0;
static int Sys_GetSystemRam(){++ramQueries;return systemRam;}
struct idLib { static void Warning(const char*,...){} };
static int reclaims=0;
@ADMISSION@
static void SoundMemoryReclaim(){++reclaims;}
static bool failAllocation=false;
static int allocations=0;
static void* Mem_Alloc(int n){if(failAllocation)return nullptr;++allocations;return std::malloc(n);}
static void Mem_Free(void* p){if(p){--allocations;std::free(p);}}
template<class T> struct idList : std::vector<T> {
    void Clear(){std::vector<T>().swap(*this);} void SetGranularity(int){}
    void SetNum(int n){this->resize(n);} int Num()const{return int(this->size());}
    T* Ptr(){return this->data();}
};
@ALLOCATION@
struct idStr : std::string { using std::string::string;
    void SetFileExtension(const char* ext){auto p=rfind('.');if(p!=npos)resize(p);append(".");append(ext);}
};
enum { FS_SEEK_SET };
static std::vector<byte> fixture;
static int maximumRead=0;
static bool truncateRead=false;
struct idFile {
    int cursor=0;
    int Length(){return int(fixture.size());} int Timestamp(){return 12;}
    int Seek(int n,int){if(n<0||n>Length())return -1;cursor=n;return 0;}
    int Read(void* out,int n){maximumRead=std::max(maximumRead,n);n=std::min(n,Length()-cursor);if(truncateRead&&cursor>=16)return 0;std::memcpy(out,fixture.data()+cursor,n);cursor+=n;return n;}
};
struct VFS { idFile* OpenFileRead(const idStr&){return new idFile;} } vfs;
static VFS* fileSystem=&vfs;
struct idFileLocal : std::unique_ptr<idFile> { using std::unique_ptr<idFile>::unique_ptr; operator idFile*()const{return get();} };
@READ@
@DELTA@
@ADPCM_SIZE@
enum { ROQ_FILE_MAGIC=0x1084, ROQ_SOUND_MONO=0x1020, ROQ_SOUND_STEREO=0x1021,
       ROQ_AUDIO_SAMPLE_RATE=22050, ROQ_AUDIO_MIN_SAMPLE=-32768, ROQ_AUDIO_MAX_SAMPLE=32767 };
struct idWaveFile { enum { FORMAT_PCM=1 }; };
struct Format { struct { uint16 formatTag,numChannels;int samplesPerSec;uint16 bitsPerSample,blockSize;int avgBytesPerSec; } basic;
    struct {struct {uint16 samplesPerBlock,numCoef;struct {int16 coef1,coef2;} aCoef[7];} adpcm;} extra;
};
class idSoundSample_OpenAL {
public:
    struct Buffer { int bufferSize=0,numSamples=0;void* buffer=nullptr; };
    idList<Buffer> buffers;
    Format format{}; int timestamp=0,playBegin=0,playLength=0,totalBufferSize=0;
    bool LoadRoQ(const idStr&); bool LoadAmplitude(const idStr&){return false;}
    struct MS_ADPCM_decodeState_t {uint8 hPredictor;int16 coef1,coef2;uint16 iDelta;int16 iSamp1,iSamp2;};
    int32 MS_ADPCM_nibble(MS_ADPCM_decodeState_t*,int8);
    int MS_ADPCM_decode(uint8**,uint32*);
    const char* GetName(){return "original-fixture";}
    ~idSoundSample_OpenAL(){for(auto& b:buffers)FreeBuffer(b.buffer);}
};
@ROQ@
@ADPCM_NIBBLE@
@ADPCM_DECODE@
static void Header(){fixture={0x84,0x10,0,0,0,0,0,0};}
static void Chunk(int type,const std::vector<byte>& data,int argument=0){
    int n=int(data.size());fixture.insert(fixture.end(),{byte(type),byte(type>>8),byte(n),byte(n>>8),byte(n>>16),0,byte(argument),byte(argument>>8)});
    fixture.insert(fixture.end(),data.begin(),data.end());
}
int main(){
    // A budget of 0 follows the device class; any other value is the player's.
    s_audioMemoryMB.value=0;
    assert(SoundMemory_Class()==SoundMemoryClass::Desktop&&SoundMemory_PayloadMB()==256&&ramQueries==1);
    s_audioMemoryClass.value="constrained-mobile";assert(SoundMemory_PayloadMB()==192);
    s_audioMemoryClass.value="mobile";assert(SoundMemory_PayloadMB()==256);
    s_audioMemoryClass.value="handheld";assert(SoundMemory_Class()==SoundMemoryClass::Desktop&&ramQueries==1);
    s_audioMemoryClass.value="constrained-mobile";s_audioMemoryMB.value=8;assert(SoundMemory_PayloadMB()==32);
    s_audioMemoryMB.value=4096;assert(SoundMemory_PayloadMB()==2048);
    s_audioMemoryClass.value="auto";s_audioMemoryMB.value=32;
    // Overflow, exact saturation, budget reduction and concurrent worker release.
    SoundMemoryBudget budget;
    assert(!budget.Acquire(UINT64_MAX,16*MiB));
    assert(budget.Acquire(16*MiB,16*MiB)&&!budget.Acquire(1,16*MiB));
    assert(!budget.Acquire(1,8*MiB));
    std::thread worker([&]{budget.Release(16*MiB);});worker.join();
    assert(budget.Used()==0&&budget.Peak()==16*MiB&&budget.Acquire(8*MiB,8*MiB));budget.Release(8*MiB);
    std::vector<void*> blocks;
    for(int i=0;i<12;++i){void* p=AllocBuffer(int(MiB)-16,"saturation");assert(p&&reinterpret_cast<std::uintptr_t>(p)%16==0);blocks.push_back(p);}
    assert(!AllocBuffer(1,"full")&&soundMemory.Used()==12*MiB&&reclaims>0);
    for(void* p:blocks){FreeBuffer(p);}
    assert(soundMemory.Used()==0&&allocations==0);
    failAllocation=true;assert(!AllocBuffer(32,"allocator fault")&&soundMemory.Used()==0);failAllocation=false;
    assert(!AllocBuffer(-1,"invalid")&&!AllocBuffer(2147483647,"overflow"));
    {SoundMemoryReservation r;assert(r.Reserve(4*MiB));assert(r.Reserve(2*MiB));assert(!r.Reserve(13*MiB));}
    assert(soundMemory.Used()==0);
    idList<byte> amp;uint64 reserved=0;
    assert(SoundResizeAmplitude(amp,reserved,17)&&reserved==32&&soundMemory.Used()==32);
    assert(!SoundResizeAmplitude(amp,reserved,13*MiB)&&amp.empty()&&reserved==0&&soundMemory.Used()==0);
    Header();Chunk(0x1011,std::vector<byte>(100000,0));Chunk(ROQ_SOUND_MONO,{1,2,0x83,0x7f});
    {idSoundSample_OpenAL s;assert(s.LoadRoQ("test.roq")&&s.playLength==4&&s.format.basic.numChannels==1);
     const int16 expected[]={1,5,-4,16125};assert(!std::memcmp(s.buffers[0].buffer,expected,sizeof(expected)));}
    assert(maximumRead<=4096&&allocations==0&&soundMemory.Used()==0);
    Header();Chunk(ROQ_SOUND_STEREO,{1,0x81,2,0x82},0x0110);
    {idSoundSample_OpenAL s;assert(s.LoadRoQ("stereo.roq")&&s.playLength==2);
     const int16 expected[]={257,4095,261,4091};assert(!std::memcmp(s.buffers[0].buffer,expected,sizeof(expected)));}
    Header();Chunk(ROQ_SOUND_MONO,std::vector<byte>(5000,1));
    {idSoundSample_OpenAL s;assert(s.LoadRoQ("block.roq"));auto* p=static_cast<int16*>(s.buffers[0].buffer);assert(p[0]==1&&p[4095]==4096&&p[4999]==5000);}
    for(int bad=0;bad<4;++bad){Header();Chunk(ROQ_SOUND_MONO,{1,2});
      if(bad==0)fixture.pop_back();
      if(bad==1)Chunk(ROQ_SOUND_STEREO,{1,2});
      if(bad==2){Header();Chunk(ROQ_SOUND_STEREO,{1});}
      if(bad==3)truncateRead=true;
      idSoundSample_OpenAL s;assert(!s.LoadRoQ("bad.roq"));truncateRead=false;
    }
    Header();Chunk(ROQ_SOUND_MONO,{1,2});
    void* held=AllocBuffer(int(12*MiB)-16,"busy");assert(held);
    {idSoundSample_OpenAL s;assert(!s.LoadRoQ("pressure.roq"));}
    FreeBuffer(held);
    {idSoundSample_OpenAL s;assert(s.LoadRoQ("retry.roq"));}
    // Actual ADPCM conversion owns both input and output until commit. Failed
    // admission/validation leaves the caller's original allocation untouched.
    for(int channels:{1,2}){
        idSoundSample_OpenAL s;s.format.basic.numChannels=channels;
        s.format.basic.blockSize=channels==1?8:15;s.format.extra.adpcm.samplesPerBlock=channels==1?4:3;
        s.format.extra.adpcm.numCoef=1;s.format.extra.adpcm.aCoef[0]={256,0};
        const std::vector<byte> encoded=channels==1?std::vector<byte>{0,16,0,100,0,80,0,0x12}:std::vector<byte>{0,0,16,0,16,0,100,0,200,0,80,0,180,0,0x12};
        auto* data=static_cast<uint8*>(AllocBuffer(int(encoded.size()),"adpcm"));std::memcpy(data,encoded.data(),encoded.size());
        uint32 size=uint32(encoded.size());auto* original=data;
        data[0]=7;assert(s.MS_ADPCM_decode(&data,&size)<0&&data==original&&size==encoded.size());data[0]=0;
        held=AllocBuffer(int(12*MiB-soundMemory.Used())-16,"full");assert(held);
        assert(s.MS_ADPCM_decode(&data,&size)<0&&data==original&&size==encoded.size());FreeBuffer(held);
        assert(s.MS_ADPCM_decode(&data,&size)==0&&data!=original);
        const std::vector<int16> expected=channels==1?std::vector<int16>{80,100,116,148}:std::vector<int16>{80,180,100,200,116,232};
        assert(size==expected.size()*2&&!std::memcmp(data,expected.data(),size));FreeBuffer(data);
    }
    assert(soundMemory.Used()==0&&allocations==0&&soundMemory.Peak()<=12*MiB);
    std::puts("PASS: aggregate overflow/saturation/reduction, worker release, allocator/envelope ownership, bounded RoQ and ADPCM PCM, failure/retry");
}
'''
    for tag, code in {'ADMISSION': admission, 'ALLOCATION': allocation, 'READ': body(source, 'static bool openQ4_ReadExact('),
                      'DELTA': body(source, 'static ID_INLINE int RoQAudioDelta(').replace('ID_INLINE', 'inline'), 'ROQ': roq,
                      'ADPCM_SIZE': body(source, 'static bool openQ4_GetMSADPCMDecodedSize('),
                      'ADPCM_NIBBLE': body(source, 'int32 idSoundSample_OpenAL::MS_ADPCM_nibble('),
                      'ADPCM_DECODE': body(source, 'int idSoundSample_OpenAL::MS_ADPCM_decode(')}.items():
        harness = harness.replace('@' + tag + '@', code)
    scratch = ROOT / '.tmp/audio/aggregate-memory'
    scratch.mkdir(parents=True, exist_ok=True)
    cpp, exe = scratch / 'test.cpp', scratch / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                    str(cpp), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
