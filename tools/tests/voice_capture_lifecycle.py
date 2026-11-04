#!/usr/bin/env python3
"""Run the production SDL voice capture path against deterministic device faults.

No real microphone is opened. Only SDL, CVar and engine host interfaces are
faked; the callback, partial-frame assembly, ring, consent and retry logic are
compiled directly from VoiceChat.cpp and VoicePacket.h.
"""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    production = (ROOT / 'src/sound/VoiceChat.cpp').read_text()
    production = re.sub(r'^#include .*\n', '', production, flags=re.M)
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "VoicePacket.h"
#define USE_SDL3 1
#define SDLCALL
#define FLOAT_IS_NAN(x) std::isnan(x)
using byte=uint8_t;
constexpr int CVAR_SOUND=1,CVAR_ARCHIVE=2,CVAR_BOOL=4,CVAR_NOCHEAT=8,CVAR_INTEGER=16,CVAR_ROM=32,CVAR_FLOAT=64,CVAR_CHEAT=128;
constexpr int CMD_FL_SOUND=1,CMD_FL_CHEAT=2;
struct idCVar {
    float value;
    idCVar(const char*,const char* initial,int,const char*):value(std::strtof(initial,nullptr)){}
    idCVar(const char* name,const char* initial,int flags,const char* help,float,float):idCVar(name,initial,flags,help){}
    bool GetBool() const{return value!=0;} int GetInteger() const{return int(value);} float GetFloat() const{return value;}
    void SetBool(bool v){value=v;} void SetInteger(int v){value=float(v);} void SetFloat(float v){value=v;}
};
idCVar s_volume("", "1", 0, ""), s_noSound("", "0", 0, "");
struct Cvars {
    std::map<std::string,float> values{{"s_voiceChatSend",1},{"s_voiceChatReceive",1},{"s_micInputLevel",5}};
    bool GetCVarBool(const char* name){return values[name]!=0;}
    float GetCVarFloat(const char* name){return values[name];}
} cvars, *cvarSystem=&cvars;
struct idMath {
    static int ClampInt(int a,int b,int c){return std::clamp(c,a,b);}
    static float ClampFloat(float a,float b,float c){return std::clamp(c,a,b);}
};
struct idCmdArgs {int Argc() const{return 1;} const char* Argv(int) const{return "0";}};
struct Commands {void AddCommand(const char*,void(*)(const idCmdArgs&),int,const char*){}} commands, *cmdSystem=&commands;
struct Common {template<class...T> void Printf(const char*,T...) {}} commonObject,*common=&commonObject;
bool muted=false;
struct idSoundSystemLocal {
    bool IsMuted() const{return muted;}
    void ResetVoiceChat(); bool UpdateVoiceChat(bool,bool,bool);
    int GetVoiceData(byte*,int); void PlayVoiceData(int,unsigned,const byte*,int);
    void StopVoiceData(int); int GetVoiceTalker() const; float GetVoiceLevel() const;
} soundSystemLocal;
constexpr int SDL_INIT_AUDIO=1,SDL_AUDIO_S16=2,SDL_AUDIO_DEVICE_DEFAULT_RECORDING=3;
struct SDL_AudioSpec {int format,channels,freq;};
struct SDL_AudioStream;
using Callback=void(*)(void*,SDL_AudioStream*,int,int);
struct SDL_AudioStream {std::vector<int16_t> samples; Callback callback=nullptr; bool locked=false;};
uint32_t now=100;
int initCount=0,quitCount=0,openCount=0,destroyCount=0,clearCount=0,lockCount=0;
bool initFail=false,openFail=false,callbackFail=false,resumeFail=false,readFail=false;
uint32_t SDL_GetTicks(){return now;}
const char* SDL_GetError(){return "injected device failure";}
const char* SDL_GetCurrentAudioDriver(){return "fixture";}
bool SDL_InitSubSystem(int){++initCount; return !initFail;}
void SDL_QuitSubSystem(int){++quitCount;}
SDL_AudioStream* SDL_OpenAudioDeviceStream(int device,const SDL_AudioSpec* spec,Callback,void*) {
    ++openCount;
    assert(device==SDL_AUDIO_DEVICE_DEFAULT_RECORDING && spec->format==SDL_AUDIO_S16 && spec->channels==1 && spec->freq==8000);
    return openFail ? nullptr : new SDL_AudioStream;
}
bool SDL_SetAudioStreamPutCallback(SDL_AudioStream* stream,Callback callback,void*){stream->callback=callback;return !callbackFail;}
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream*){return !resumeFail;}
void SDL_DestroyAudioStream(SDL_AudioStream* stream){assert(!stream->locked);++destroyCount;delete stream;}
void SDL_LockAudioStream(SDL_AudioStream* stream){assert(!stream->locked);stream->locked=true;++lockCount;}
void SDL_UnlockAudioStream(SDL_AudioStream* stream){assert(stream->locked);stream->locked=false;}
int SDL_GetAudioStreamAvailable(SDL_AudioStream* stream){assert(stream->locked);return int(stream->samples.size()*2);}
void SDL_ClearAudioStream(SDL_AudioStream* stream){assert(stream->locked);stream->samples.clear();++clearCount;}
int SDL_GetAudioStreamData(SDL_AudioStream* stream,void* buffer,int bytes){
    assert(stream->locked); if(readFail)return -1;
    const int count=std::min(int(stream->samples.size()),bytes/2);
    std::memcpy(buffer,stream->samples.data(),count*2);
    stream->samples.erase(stream->samples.begin(),stream->samples.begin()+count);
    return count*2;
}
@PRODUCTION@
void Feed(int count,int16_t value=2000) {
    assert(captureStream && captureStream->callback);
    captureStream->samples.insert(captureStream->samples.end(),count,value);
    SDL_LockAudioStream(captureStream);
    captureStream->callback(nullptr,captureStream,count*2,count*2);
    SDL_UnlockAudioStream(captureStream);
}
int main(){
    static_assert(PlaybackChannels==3);
    auto& sound=soundSystemLocal;
    byte packet[oq4voice::PacketBytes]; int16_t decoded[oq4voice::Samples];
    assert(!sound.UpdateVoiceChat(true,true,false)); // archived Send is not consent
    assert(openCount==0 && s_voiceChatStatus.GetInteger()==1);
    s_voiceChatCapture.SetBool(true);
    assert(!sound.UpdateVoiceChat(true,false,false)); assert(openCount==0);
    assert(sound.UpdateVoiceChat(true,true,false)); assert(openCount==1 && captureOpens==1);
    Feed(80); assert(!sound.GetVoiceData(packet,sizeof(packet)));
    Feed(80); assert(sound.GetVoiceData(packet,sizeof(packet))==sizeof(packet));
    assert(oq4voice::Decode(packet,sizeof(packet),decoded) && std::abs(decoded[0]-2000)<100);
    assert(sound.GetVoiceLevel()>.06f && sound.GetVoiceLevel()<.062f);
    assert(!sound.GetVoiceData(packet,sizeof(packet)));
    // Releasing and quickly pressing PTT again must not look like device failure.
    assert(!sound.UpdateVoiceChat(true,false,false)); assert(!captureStream && destroyCount==1);
    now+=10; assert(sound.UpdateVoiceChat(true,true,false)); assert(openCount==2);
    cvars.values["s_micInputLevel"]=10;
    Feed(160,30000); assert(sound.GetVoiceData(packet,sizeof(packet)));
    oq4voice::Decode(packet,sizeof(packet),decoded); assert(decoded[0]==32124 && sound.GetVoiceLevel()>.99f);
    cvars.values["s_micInputLevel"]=std::numeric_limits<float>::quiet_NaN();
    Feed(160); assert(sound.GetVoiceData(packet,sizeof(packet)));
    oq4voice::Decode(packet,sizeof(packet),decoded); assert(decoded[0]==0);
    cvars.values["s_micInputLevel"]=5;
    Feed(800); assert(clearCount==1 && captureQueue.count==0);
    for(int i=0;i<1000;++i){now+=20; Feed(160);}
    assert(captureQueue.count==4 && captureQueue.dropped>=996);
    now+=121; assert(!sound.GetVoiceData(packet,sizeof(packet)) && captureQueue.count==0);
    // Local test consumes capture without admitting anything for network send.
    assert(!sound.UpdateVoiceChat(true,true,true));
    Feed(160); const auto before=captured;
    assert(!sound.UpdateVoiceChat(true,true,true)); assert(captured==before+1);
    assert(!sound.GetVoiceData(packet,sizeof(packet)));
    muted=true; assert(!sound.UpdateVoiceChat(true,true,false)); assert(!captureStream);
    muted=false; cvars.values["s_voiceChatSend"]=0;
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(!captureStream);
    assert(!sound.UpdateVoiceChat(true,false,true)); assert(captureStream); // local test works with Send off
    cvars.values["s_voiceChatSend"]=1;
    now+=2001; const int priorOpens=openCount;
    assert(sound.UpdateVoiceChat(true,true,false)); assert(openCount==priorOpens+1); // stalled endpoint recovery
    sound.ResetVoiceChat(); assert(!captureStream && quitCount==1 && s_voiceChatStatus.GetInteger()==0);
    now+=1001; openFail=true;
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(s_voiceChatStatus.GetInteger()==4);
    const int failedOpens=openCount; now+=500;
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(openCount==failedOpens);
    openFail=false; now+=500; assert(sound.UpdateVoiceChat(true,true,false));
    sound.ResetVoiceChat(); now+=1001; resumeFail=true;
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(!captureStream);
    resumeFail=false; now+=1001; callbackFail=true;
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(!captureStream);
    callbackFail=false; now+=1001; assert(sound.UpdateVoiceChat(true,true,false));
    readFail=true; Feed(160); assert(captureQueue.count==0);
    readFail=false; now+=2001; assert(sound.UpdateVoiceChat(true,true,false));
    s_voiceChatCapture.SetBool(false); assert(!sound.UpdateVoiceChat(true,true,true)); assert(!captureStream);
    sound.ResetVoiceChat(); now+=1001; initFail=true; s_voiceChatCapture.SetBool(true);
    assert(!sound.UpdateVoiceChat(true,true,false)); assert(!ownsAudioSubsystem);
    initFail=false; now+=1001; assert(sound.UpdateVoiceChat(true,true,false));
    now+=251; SoundVoiceChat_Render(); assert(!sessionActive && !captureStream);
    assert(lockCount>1000 && quitCount==initCount-1); // one failed SDL init never owned a reference
    // A local test can leave a fourth queued frame and half an assembled frame.
    // Neither may become a network packet when PTT is already down on test exit.
    assert(!sound.UpdateVoiceChat(true,false,true));
    Feed(640,12000); Feed(80,12000);
    const auto localBefore=captured;
    assert(!sound.UpdateVoiceChat(true,false,true));
    assert(captured==localBefore+3 && captureQueue.count==1 && partialSamples==80);
    assert(sound.UpdateVoiceChat(true,true,false));
    assert(!sound.GetVoiceData(packet,sizeof(packet)) && captureQueue.count==0 && partialSamples==0);
    Feed(80,2000); assert(!sound.GetVoiceData(packet,sizeof(packet)));
    Feed(80,2000); assert(sound.GetVoiceData(packet,sizeof(packet)));
    oq4voice::Decode(packet,sizeof(packet),decoded);
    for(auto sample:decoded) assert(std::abs(sample-2000)<100);
    // Entering the local test also discards queued and partial network speech.
    Feed(640,12000); Feed(80,12000);
    const auto networkBefore=captured;
    assert(!sound.UpdateVoiceChat(true,true,true));
    assert(captured==networkBefore && captureQueue.count==0 && partialSamples==0);
    Feed(160); assert(!sound.UpdateVoiceChat(true,false,true));
    assert(captured==networkBefore+1);
    // Failed reopening still discards every test byte before retrying PTT.
    Feed(640,12000); Feed(80,12000); openFail=true;
    assert(!sound.UpdateVoiceChat(true,true,false));
    assert(!captureStream && captureQueue.count==0 && partialSamples==0);
    assert(!sound.GetVoiceData(packet,sizeof(packet)));
    openFail=false; now+=1000; assert(sound.UpdateVoiceChat(true,true,false));
    assert(!sound.GetVoiceData(packet,sizeof(packet)));
    // A partial frame has an age too: later callbacks must not timestamp old
    // speech as new. Exercise both one long gap and several short gaps, and wrap.
    for(bool wrap:{false,true}) {
        if(wrap) now=0xffffffc0u;
        const auto droppedBefore=captureQueue.dropped;
        Feed(40,12000); now+=60; Feed(40,12000); now+=61; Feed(80,2000);
        assert(!sound.GetVoiceData(packet,sizeof(packet)) && partialSamples==80);
        assert(captureQueue.dropped==droppedBefore+1);
        Feed(80,2000); assert(sound.GetVoiceData(packet,sizeof(packet)));
        oq4voice::Decode(packet,sizeof(packet),decoded);
        for(auto sample:decoded) assert(std::abs(sample-2000)<100);
        Feed(80,12000); now+=121; Feed(80,2000);
        assert(!sound.GetVoiceData(packet,sizeof(packet)) && partialSamples==80);
        Feed(80,2000); assert(sound.GetVoiceData(packet,sizeof(packet)));
        oq4voice::Decode(packet,sizeof(packet),decoded);
        for(auto sample:decoded) assert(std::abs(sample-2000)<100);
    }
    Feed(80,12000); now+=oq4voice::MaxAgeMsec; Feed(80,2000);
    assert(sound.GetVoiceData(packet,sizeof(packet)));
    oq4voice::Decode(packet,sizeof(packet),decoded);
    for(int i=0;i<160;++i) assert(decoded[i]==oq4voice::Expand(oq4voice::Compress(i<80?12000:2000)));
    // The diagnostic clock pair must describe the timestamp actually encoded,
    // including skew and 32-bit wrap, without any receiver/network observation.
    for(uint32_t start:{1000u,0xfffffff0u}) for(float rate:{.999f,1.001f}) {
        now=start; s_voiceTestClockRate.SetFloat(1); stampSkewed=false;
        Feed(160); assert(sound.GetVoiceData(packet,sizeof(packet)));
        const uint32_t first=oq4voice::Read32(packet+5);
        s_voiceTestClockRate.SetFloat(rate); now+=10000;
        Feed(160); assert(sound.GetVoiceData(packet,sizeof(packet)));
        const uint32_t last=oq4voice::Read32(packet+5);
        assert(stampSource==now && uint32_t(int64_t(stampClock))==last);
        assert(std::abs(int(last-first)-(rate<1?9990:10010))<=1);
    }
    s_voiceTestClockRate.SetFloat(1);
    sound.ResetVoiceChat();
    std::puts("PASS: capture consent, PTT/test isolation, partial age, packet-clock observations, PCM/gain, callback bounds and fault recovery");
}
'''.replace('@PRODUCTION@', production)
    scratch = ROOT / '.tmp/audio/capture-lifecycle'
    scratch.mkdir(parents=True, exist_ok=True)
    cpp, exe = scratch / 'test.cpp', scratch / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', '-Werror', '-I', str(ROOT / 'src/sound'), str(cpp), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
