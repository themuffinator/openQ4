#!/usr/bin/env python3
"""Exercise production device suspension and updates with counted OpenAL calls."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#define ALC_APIENTRY
#define OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED 1
struct ALCdevice {int identity;};
struct ALCcontext {};
static ALCdevice first{1},second{2};static ALCcontext context,foreignContext;
static ALCcontext* current=&context;
static bool extension=true,deferred=false;
static int functions=3,pauses=0,resumes=0,flushed=0,pausedDevice=0,resumedDevice=0;
static float gain=1,pendingGain=1;
static constexpr int AL_GAIN=1,ALC_TRUE=1,AL_NO_ERROR=0;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Pause(ALCdevice* device){++pauses;pausedDevice=device->identity;}
static void Resume(ALCdevice* device){++resumes;resumedDevice=device->identity;}
static int alcIsExtensionPresent(ALCdevice*,const char* name){require(!std::strcmp(name,"ALC_SOFT_pause_device"),"wrong extension");return extension;}
static void* alcGetProcAddress(ALCdevice*,const char* name){
    if(!std::strcmp(name,"alcDevicePauseSOFT"))return functions&1?reinterpret_cast<void*>(&Pause):nullptr;
    require(!std::strcmp(name,"alcDeviceResumeSOFT"),"wrong function");return functions&2?reinterpret_cast<void*>(&Resume):nullptr;
}
static ALCcontext* alcGetCurrentContext(){return current;}
static void alListenerf(int parameter,float value){require(parameter==AL_GAIN,"wrong listener parameter");if(deferred)pendingGain=value;else gain=value;}
static void qalProcessUpdatesSOFT(){++flushed;gain=pendingGain;deferred=false;}
static int CheckALErrors(){return AL_NO_ERROR;}
static int Sys_Milliseconds(){return 3000;}
struct SoundSystem {bool muted=false;bool IsMuted(){return muted;}} systemValue,*soundSystem=&systemValue;
struct Voice {void FlushSourceBuffers(){} bool IsPlaying(){return false;}};
struct idSoundOggStream {static void ReapReleased(bool){}};
template<class T> struct List:std::vector<T> {
    int Num()const{return static_cast<int>(this->size());}
    void AddUnique(T value){this->push_back(value);}
    void RemoveIndex(int index){this->erase(this->begin()+index);}
};
class idSoundHardware_OpenAL {
public:
    ALCdevice* openalDevice=&first;ALCcontext* openalContext=&context;
    /* @PAUSE_FIELDS@ */
    bool deferredUpdatesAvailable=true,deferredUpdatesActive=false;
    int lastResetTime=0,retries=0;
    List<Voice*> zombieVoices,freeVoices;
    void SetDevicePaused(bool);void Update();void EndDeferredUpdates();
    void Init(){++retries;openalDevice=&second;openalContext=&context;current=&context;SetDevicePaused(devicePauseRequested);}
    bool UpdateDeviceMonitoring(){return false;}
    void PrintPerformanceData(){}
};
'''
MAIN = r'''
int main(){try {
    // Missing or partially exported extensions must remain a reversible mute.
    for(bool available:{false,true})for(int mask=0;mask<4;++mask){
        extension=available;functions=mask;pauses=resumes=flushed=0;gain=1;current=&context;systemValue.muted=false;
        idSoundHardware_OpenAL hardware;
        hardware.deferredUpdatesActive=deferred=true;
        hardware.SetDevicePaused(true);
        require(gain==0 && !deferred && flushed==1,"background mute was left deferred");
        const int expected=available && mask==3?1:0;
        require(pauses==expected && resumes==0,"paused without a matching resume function");
        hardware.SetDevicePaused(true);
        require(pauses==expected,"duplicate background event paused twice");
        gain=1;hardware.Update();require(gain==0,"normal update undid the app mute");
        // The exact paired pointer must resume even if later lookup changes.
        functions=0;hardware.SetDevicePaused(false);
        require(gain==1 && resumes==expected,"foreground did not resume the original device");
        hardware.SetDevicePaused(false);require(resumes==expected,"duplicate resume called the device twice");
        systemValue.muted=true;hardware.SetDevicePaused(true);hardware.SetDevicePaused(false);
        hardware.Update();require(gain==0 && systemValue.muted,"resume cleared the player's mute");
    }
    {
        extension=true;functions=3;pauses=resumes=0;systemValue.muted=false;
        idSoundHardware_OpenAL hardware;hardware.openalDevice=nullptr;hardware.openalContext=nullptr;
        hardware.SetDevicePaused(true);
        require(hardware.devicePauseRequested && !pauses,"missing device discarded the suspension");
        hardware.Update();require(hardware.retries==1 && pauses==1 && pausedDevice==2 && gain==0,"recovered device became audible during suspension");
        hardware.SetDevicePaused(false);require(resumes==1 && resumedDevice==2,"resumed a stale device");
    }
    {
        idSoundHardware_OpenAL hardware;current=&foreignContext;gain=.25f;functions=0;
        hardware.SetDevicePaused(true);require(gain==.25f,"suspension changed a foreign context");
        current=&context;hardware.SetDevicePaused(true);require(gain==0,"owned context did not receive pending mute");
        hardware.SetDevicePaused(false);require(gain==1,"fallback remained muted");
    }
    std::puts("OpenAL suspension: paired extension, mute fallback, deferred flush, duplicate events, player mute, device retry and context ownership passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''


def source():
    directory = ROOT / 'src/sound/OpenAL'
    header = (directory / 'AL_SoundHardware.h').read_text()
    fields = re.search(r'\ttypedef void \( ALC_APIENTRY \*devicePauseFunction_t \).*?deviceResume = NULL;', header, re.S)
    assert fields, 'Production pause fields/defaults missing'
    text = (directory / 'AL_SoundHardware.cpp').read_text()
    init = function_body(text, 'void idSoundHardware_OpenAL::Init()')
    shutdown = function_body(text, 'void idSoundHardware_OpenAL::Shutdown()')
    assert init.index('SetDevicePaused( devicePauseRequested );') > init.index('alcMakeContextCurrent( openalContext )')
    assert 'deviceResume = NULL;' in shutdown and 'devicePauseRequested =' not in shutdown
    return SUPPORT.replace('/* @PAUSE_FIELDS@ */', fields[0]) + '\n'.join(function_body(text, signature) for signature in (
        'void idSoundHardware_OpenAL::EndDeferredUpdates()',
        'void idSoundHardware_OpenAL::SetDevicePaused(',
        'void idSoundHardware_OpenAL::Update()')) + MAIN


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--generate-native':
        Path(sys.argv[2]).write_text(source(), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='openal-pause-', dir=ROOT/'.tmp') as directory:
        cpp = Path(directory)/'pause.cpp'; exe = cpp.with_suffix('.exe')
        cpp.write_text(source(), encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
