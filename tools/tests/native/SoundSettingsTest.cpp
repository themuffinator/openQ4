// Actual SoundSettings.cpp + checked voice/render bodies, counted API boundary.
// No device, context, SDL audio, mixer or audibility is exercised.
#define AL_LIBTYPE_STATIC
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include "SoundSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

static int checks=0;
static bool denyAllocation=false;
void* operator new(std::size_t n) {if(denyAllocation)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct idCVar { int value=0; std::string text; bool GetBool() const { return value!=0; } int GetInteger() const { return value; } const char* GetString() const { return text.c_str(); } };
idCVar s_noSound, s_useOpenAL, s_deviceName, s_useEAXReverb, s_openALHRTF, s_numberOfSpeakers, s_maxEmitterChannels, s_openALEfxDebugMode;
template<class T> struct List : std::vector<T> { int Num() const {return static_cast<int>(this->size());} void AddUnique(T x){if(std::find(this->begin(),this->end(),x)==this->end())this->push_back(x);} void RemoveIndex(int i){this->erase(this->begin()+i);} };
class idSoundVoice_OpenAL {
public:
	ALuint openalSource=0,openalDirectFilter=0,openalAuxFilter=0;
	std::uint64_t soundSettingsSourceGeneration=0;
	void (AL_APIENTRY *soundSettingsDeleteFilters)(ALsizei,const ALuint*)=nullptr;void DestroyWetDryFilters();
	float gain=1,wetLevel=1,dryLevel=1,occlusion=0,environmentMuffle=0;
	bool ApplyWetDryRoutingChecked(bool,bool,ALuint,SoundSettingsSourceReceipt&);
	void FlushSourceBuffers(){} bool IsPlaying(){return false;}
};
class idSoundHardware_OpenAL {
public:
	ALCdevice* openalDevice=nullptr; ALCcontext* openalContext=nullptr;
	bool initFailed=false,deferredUpdatesActive=false,efxFiltersAvailable=true,efxEnabled=false;
	ALuint auxEffectSlot=0,auxReverbEffect=0;
	int openedSpeakerCount=2,openedHrtfMode=0,lastResetTime=0;
	List<idSoundVoice_OpenAL> voices; List<idSoundVoice_OpenAL*> zombieVoices,freeVoices;
	ALCdevice* GetOpenALDevice()const{return openalDevice;} ALCcontext* GetOpenALContext()const{return openalContext;} bool InitFailed()const{return initFailed;} void Init(); bool UpdateDeviceMonitoring(); void Update();
	void PrintPerformanceData(){}
};
struct World { int updates=0,budget=0; bool throws=false; void Update(){++updates;budget=s_maxEmitterChannels.GetInteger();if(throws)throw std::runtime_error("world failure");} float CurrentRumbleAmplitude(){return 0;} };
struct idSoundSystemLocal { idSoundHardware_OpenAL hardware; bool muted=false,needsRestart=false; World* currentSoundWorld=nullptr; int soundTime=0,restarts=0;
	bool IsMuted() const{return muted;} void Render(); void Restart(){++restarts;} };
idSoundSystemLocal soundSystemLocal; idSoundSystemLocal* soundSystem=&soundSystemLocal;
struct Session {World* sw=nullptr;} sessionValue; Session* session=&sessionValue;
static void Sound_UpdateControllerRumble(float){}
static int Sys_Milliseconds(){return 2000;}
static bool mainThread=true; bool SDL_IsMainThread(){return mainThread;}
static int automaticMonitor=0;
void idSoundHardware_OpenAL::Init(){CHECK(!SoundSettings_BlockAutomaticRestart());}
bool idSoundHardware_OpenAL::UpdateDeviceMonitoring(){if(SoundSettings_BlockAutomaticRestart())return false;++automaticMonitor;return false;}

namespace Fake {
ALCdevice* device=reinterpret_cast<ALCdevice*>(0x1000); ALCcontext* context=reinterpret_cast<ALCcontext*>(0x2000);
ALenum alError=AL_NO_ERROR; ALCenum alcError=ALC_NO_ERROR;
bool connected=true,support=true,resetResult=true,ignoreReset=false,resetFalseButApplied=false,efxSupport=true,filterMutation=false;
int mode=ALC_STEREO_SOFT,hrtf=0,status=ALC_HRTF_DISABLED_SOFT,calls=0,writes=0,resets=0,failWrite=0;
int stereoObserved=0,callbackAt=0,callbackKind=0,writesAtCallback=0;bool reentryRejected=false;
SoundSettingsPolicy* callerPolicy=nullptr;SoundSettingsLease* callerLease=nullptr;
SoundSettingsPolicy policyMutation{3,true,999};
void Tick();
float listener=1; std::string actual="Physical",defaultName="Physical",missingProc;
unsigned next=100;
std::map<unsigned,int> slots;
struct Route {unsigned direct=0,aux=0,slot=0;float gain=0;int writes=0;};
std::map<unsigned,Route> sources;
std::map<unsigned,std::array<float,2>> filters;
std::map<unsigned,int> effects;
bool Write() { Tick();++writes;if(failWrite && writes==failWrite){alError=AL_OUT_OF_MEMORY;return false;}return true; }
void AL_APIENTRY GenEffects(ALsizei n,ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write()){x[i]=next++;effects[x[i]]=0;}}
void AL_APIENTRY DelEffects(ALsizei n,const ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write())effects.erase(x[i]);}
void AL_APIENTRY Effecti(ALuint x,ALenum,ALint v) noexcept {if(Write())effects[x]=v;}
void AL_APIENTRY GenSlots(ALsizei n,ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write()){x[i]=next++;slots[x[i]]=0;}}
void AL_APIENTRY DelSlots(ALsizei n,const ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write())slots.erase(x[i]);}
void AL_APIENTRY Sloti(ALuint x,ALenum p,ALint v) noexcept {if(Write()){if(p!=AL_EFFECTSLOT_EFFECT||!slots.count(x))alError=AL_INVALID_VALUE;else slots[x]=v;}}
void AL_APIENTRY QueryEffect(ALuint x,ALenum p,ALint* v) noexcept {Tick();if(p!=AL_EFFECT_TYPE||!effects.count(x))alError=AL_INVALID_VALUE;else *v=effects[x];}
void AL_APIENTRY QuerySlot(ALuint x,ALenum p,ALint* v) noexcept {Tick();if(p!=AL_EFFECTSLOT_EFFECT||!slots.count(x))alError=AL_INVALID_VALUE;else *v=slots[x];}
ALboolean AL_APIENTRY IsFilter(ALuint x) noexcept {Tick();return filters.count(x)?AL_TRUE:AL_FALSE;}
void AL_APIENTRY GenFilters(ALsizei n,ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write()){x[i]=next++;filters[x[i]]={1,1};}}
void AL_APIENTRY DelFilters(ALsizei n,const ALuint* x) noexcept {for(int i=0;i<n;++i)if(Write())filters.erase(x[i]);}
void AL_APIENTRY Filteri(ALuint x,ALenum p,ALint v) noexcept {if(Write()&&(!filters.count(x)||p!=AL_FILTER_TYPE||v!=AL_FILTER_LOWPASS))alError=AL_INVALID_VALUE;}
void AL_APIENTRY Filterf(ALuint x,ALenum p,ALfloat v) noexcept {if(Write()){if(!filters.count(x))alError=AL_INVALID_VALUE;else filters[x][p==AL_LOWPASS_GAIN?0:1]=v;}}
ALCboolean ALC_APIENTRY Reset(ALCdevice*,const ALCint* attrs) noexcept {Tick();++resets;if(!resetResult&&!resetFalseButApplied)return ALC_FALSE;if(!ignoreReset){for(int i=0;attrs[i];i+=2){if(attrs[i]==ALC_OUTPUT_MODE_SOFT)mode=attrs[i+1]==ALC_STEREO_SOFT&&stereoObserved?stereoObserved:attrs[i+1];if(attrs[i]==ALC_HRTF_SOFT && attrs[i+1]!=ALC_DONT_CARE_SOFT)hrtf=attrs[i+1];}status=hrtf?ALC_HRTF_ENABLED_SOFT:ALC_HRTF_DISABLED_SOFT;}return resetResult?ALC_TRUE:ALC_FALSE;}
}
extern "C" {
ALenum AL_APIENTRY alGetError() noexcept {Fake::Tick();const auto e=Fake::alError;Fake::alError=AL_NO_ERROR;return e;}
ALCenum ALC_APIENTRY alcGetError(ALCdevice*) noexcept {Fake::Tick();const auto e=Fake::alcError;Fake::alcError=ALC_NO_ERROR;return e;}
ALCcontext* ALC_APIENTRY alcGetCurrentContext() noexcept {Fake::Tick();return Fake::context;}
ALCdevice* ALC_APIENTRY alcGetContextsDevice(ALCcontext*) noexcept {Fake::Tick();return Fake::device;}
ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice*,const ALCchar* e) noexcept {Fake::Tick();if(std::strcmp(e,"ALC_EXT_EFX")==0)return Fake::efxSupport;return Fake::support;}
const ALCchar* ALC_APIENTRY alcGetString(ALCdevice*,ALCenum p) noexcept {Fake::Tick();return p==ALC_DEFAULT_ALL_DEVICES_SPECIFIER||p==ALC_DEFAULT_DEVICE_SPECIFIER?Fake::defaultName.c_str():Fake::actual.c_str();}
void ALC_APIENTRY alcGetIntegerv(ALCdevice*,ALCenum p,ALCsizei,ALCint* v) noexcept {Fake::Tick();switch(p){case ALC_CONNECTED:*v=Fake::connected;break;case ALC_OUTPUT_MODE_SOFT:*v=Fake::mode;break;case ALC_HRTF_SOFT:*v=Fake::hrtf;break;case ALC_HRTF_STATUS_SOFT:*v=Fake::status;break;default:Fake::alcError=ALC_INVALID_ENUM;}}
void* ALC_APIENTRY alcGetProcAddress(ALCdevice*,const ALCchar* p) noexcept {Fake::Tick();return Fake::missingProc==p?nullptr:reinterpret_cast<void*>(&Fake::Reset);}
void* AL_APIENTRY alGetProcAddress(const ALchar* p) noexcept {
	Fake::Tick();if(Fake::missingProc==p)return nullptr;
#define PROC(name,fn) if(std::strcmp(p,name)==0)return reinterpret_cast<void*>(&Fake::fn)
	PROC("alGetEffecti",QueryEffect);PROC("alGenEffects",GenEffects);PROC("alDeleteEffects",DelEffects);PROC("alEffecti",Effecti);
	PROC("alGenAuxiliaryEffectSlots",GenSlots);PROC("alDeleteAuxiliaryEffectSlots",DelSlots);PROC("alAuxiliaryEffectSloti",Sloti);PROC("alGetAuxiliaryEffectSloti",QuerySlot);
	PROC("alDeleteFilters",DelFilters);PROC("alIsFilter",IsFilter);PROC("alGenFilters",GenFilters);PROC("alFilteri",Filteri);PROC("alFilterf",Filterf);
#undef PROC
	return nullptr;
}
ALboolean AL_APIENTRY alIsSource(ALuint x) noexcept {Fake::Tick();return Fake::sources.count(x)?AL_TRUE:AL_FALSE;}
void AL_APIENTRY alSourcei(ALuint x,ALenum p,ALint v) noexcept {if(Fake::Write()){if(p!=AL_DIRECT_FILTER||!Fake::sources.count(x))Fake::alError=AL_INVALID_ENUM;else{Fake::sources[x].direct=v;++Fake::sources[x].writes;}}}
void AL_APIENTRY alSourcef(ALuint x,ALenum p,ALfloat v) noexcept {if(Fake::Write()){if(p!=AL_GAIN||!Fake::sources.count(x))Fake::alError=AL_INVALID_ENUM;else{Fake::sources[x].gain=v;++Fake::sources[x].writes;}}}
void AL_APIENTRY alSource3i(ALuint x,ALenum p,ALint a,ALint b,ALint c) noexcept {if(Fake::Write()){if(p!=AL_AUXILIARY_SEND_FILTER||b!=0||!Fake::sources.count(x))Fake::alError=AL_INVALID_ENUM;else{Fake::sources[x].slot=a;Fake::sources[x].aux=c;++Fake::sources[x].writes;if(Fake::filterMutation)++soundSystemLocal.hardware.voices[0].soundSettingsSourceGeneration;}}}
void AL_APIENTRY alListenerf(ALenum p,ALfloat v) noexcept {if(Fake::Write()){if(p!=AL_GAIN)Fake::alError=AL_INVALID_ENUM;else Fake::listener=v;}}
void AL_APIENTRY alGetListenerf(ALenum p,ALfloat* v) noexcept {Fake::Tick();if(p!=AL_GAIN)Fake::alError=AL_INVALID_ENUM;else *v=Fake::listener;}
}

// Production insertions made by the runner below.
static LPALDELETEFILTERS qalDeleteFilters=nullptr;
// @PRODUCTION@

void Fake::Tick() {
	++calls;if(!callbackAt||calls!=callbackAt)return;callbackAt=0;writesAtCallback=writes;
	if(callbackKind==1)SoundSettings_DeviceEvent();
	if(callbackKind==2){SoundSettings_DeviceDestroyed();soundSystemLocal.hardware.openalDevice=reinterpret_cast<ALCdevice*>(0x3000);}
	if(callbackKind==3){SoundSettingsObservation o;o.generation=999;char e[80];reentryRejected=!SoundSettings_TryApply(state.lease,o,e,sizeof(e))&&o.generation==999;}
	if(callbackKind==5)s_numberOfSpeakers.value=2;
	if(callbackKind==6){const auto prior=state.phase;SoundSettings_Abandon(state.lease);reentryRejected=state.phase==prior;}
	if(callbackKind==7)s_openALHRTF.value=1;
	if(callbackKind==8)s_openALEfxDebugMode.value=1;
	if(callbackKind==9&&callerPolicy)*callerPolicy=policyMutation;
	if(callbackKind==10&&callerLease)*callerLease={901,902,903};
	if(callbackKind==4)context=reinterpret_cast<ALCcontext*>(0x4000);
}
static World world;
static char error[256];
static void ResetFixture(bool efx=false) {
	SoundSettings_DeviceDestroyed();soundSystemLocal={};world={};soundSystemLocal.currentSoundWorld=&world;
	auto& h=soundSystemLocal.hardware;h.openalDevice=Fake::device;h.openalContext=Fake::context;h.voices.resize(3);
	Fake::context=reinterpret_cast<ALCcontext*>(0x2000);h.openalContext=Fake::context;Fake::alError=0;Fake::alcError=0;Fake::mode=ALC_STEREO_SOFT;Fake::hrtf=0;Fake::status=ALC_HRTF_DISABLED_SOFT;
	Fake::connected=true;Fake::support=true;Fake::efxSupport=true;Fake::resetResult=true;Fake::ignoreReset=false;Fake::resetFalseButApplied=false;Fake::filterMutation=false;
	Fake::calls=Fake::writes=Fake::resets=Fake::failWrite=0;Fake::stereoObserved=Fake::callbackAt=Fake::callbackKind=Fake::writesAtCallback=0;Fake::reentryRejected=false;Fake::listener=1;Fake::actual=Fake::defaultName="Physical";Fake::missingProc.clear();
	Fake::sources.clear();Fake::slots.clear();Fake::effects.clear();Fake::filters.clear();
	Fake::callerPolicy=nullptr;Fake::callerLease=nullptr;
	for(unsigned i=0;i<3;++i){h.voices[i].openalSource=i+1;h.voices[i].soundSettingsSourceGeneration=SoundSettings_SourceCreated();Fake::sources[i+1]={};h.voices[i].openalDirectFilter=20+i*2;h.voices[i].openalAuxFilter=21+i*2;Fake::filters[20+i*2]={1,1};Fake::filters[21+i*2]={1,1};}
	s_noSound.value=0;s_useOpenAL.value=1;s_deviceName.text="";s_openALHRTF.value=0;s_openALEfxDebugMode.value=0;
	s_numberOfSpeakers.value=2;s_useEAXReverb.value=efx;s_maxEmitterChannels.value=48;mainThread=true;automaticMonitor=0;
	if(efx){h.efxEnabled=true;h.auxEffectSlot=10;h.auxReverbEffect=11;Fake::slots[10]=11;Fake::effects[11]=AL_EFFECT_EAXREVERB;}
}
static void Patch(const SoundSettingsPolicy& p){s_numberOfSpeakers.value=p.speakers;s_useEAXReverb.value=p.efx;s_maxEmitterChannels.value=p.maxEmitterChannels;}
static SoundSettingsLease Begin(SoundSettingsPolicy target,SoundSettingsObservation& b){SoundSettingsLease l;CHECK(SoundSettings_Begin(1,2,target,l,b,error,sizeof(error)));CHECK(l.token&&l.owner==1&&l.request==2);return l;}
static void Complete(SoundSettingsLease l){SoundSettingsObservation o;CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));soundSystemLocal.Render();const bool finished=SoundSettings_Finish(l,o,error,sizeof(error));if(!finished)std::fprintf(stderr,"finish: %s phase=%u gen=%llu route=%llu update=%llu attempted=%llu calls=%d writes=%d fail=%d\n",error,unsigned(state.phase),(unsigned long long)generation,(unsigned long long)state.routing.routingGeneration,(unsigned long long)state.routing.routingUpdate,(unsigned long long)state.attemptUpdate,Fake::calls,Fake::writes,Fake::failWrite);CHECK(finished);CHECK(o.routingUpdate>0&&o.routingGeneration==o.generation);CHECK(o.sourceCount==3);CHECK(!SoundSettings_BlockAutomaticRestart());}
static void Positive() {
	for(bool baseEfx:{false,true})for(bool targetEfx:{false,true})for(int speakers:{2,6})for(bool mute:{false,true}) {
		ResetFixture(baseEfx);soundSystemLocal.muted=mute;SoundSettingsObservation b,o;SoundSettingsPolicy p{speakers,targetEfx,12};auto l=Begin(p,b);
		CHECK(b.requested.efx==baseEfx&&b.efx==baseEfx&&b.muted==mute&&b.outputMode==ALC_STEREO_SOFT);
		CHECK(Fake::writes==0&&b.sourceCount==3&&b.routingGeneration==0);Patch(p);CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));
		CHECK(o.outputMode==(speakers==6?ALC_SURROUND_5_1_SOFT:ALC_STEREO_SOFT));CHECK(o.efx==targetEfx&&o.sourceCount==3);
		for(unsigned i=0;i<3;++i){CHECK(o.sources[i].source==i+1);CHECK(o.sources[i].lifetime==soundSystemLocal.hardware.voices[i].soundSettingsSourceGeneration);CHECK(Fake::sources[i+1].slot==(targetEfx?o.slot:0));CHECK(Fake::sources[i+1].direct!=0);}
		Complete(l);CHECK(world.updates==1&&world.budget==12);CHECK(soundSystemLocal.muted==mute);CHECK(automaticMonitor==0);
	}
	ResetFixture(true);SoundSettingsObservation b,o;auto l=Begin({6,false,12},b);Patch({6,false,12});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));CHECK(o.slot==b.slot&&o.effect==b.effect&&o.outputMode==b.outputMode&&o.efx==b.efx);Complete(l);
	// Requested EFX/surround had already fallen back in legacy startup. Preserve
	// that observed baseline when changing only the channel budget, and restore it.
	ResetFixture();s_useEAXReverb.value=1;s_numberOfSpeakers.value=6;s_openALHRTF.value=2;Fake::mode=ALC_STEREO_HRTF_SOFT;Fake::hrtf=1;Fake::status=ALC_HRTF_ENABLED_SOFT;
	l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(!o.efx&&o.hrtf&&o.outputMode==ALC_STEREO_HRTF_SOFT&&Fake::resets==0);Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
}
static void Negative() {
	for(int which=0;which<12;++which){ResetFixture();SoundSettingsLease l{9,9,9};SoundSettingsObservation b;b.generation=777;
		SoundSettingsPolicy p{6,true,12};switch(which){case 0:p.speakers=3;break;case 1:p.maxEmitterChannels=0;break;case 2:p.maxEmitterChannels=49;break;case 3:s_noSound.value=1;break;case 4:s_useOpenAL.value=0;break;case 5:soundSystemLocal.needsRestart=true;break;case 6:Fake::support=false;break;case 7:Fake::connected=false;break;case 8:s_openALHRTF.value=2;break;case 9:mainThread=false;break;case 10:Fake::actual.assign(1024,'x');break;case 11:Fake::mode=ALC_ANY_SOFT;break;}
		CHECK(!SoundSettings_Begin(1,2,p,l,b,error,sizeof(error)));CHECK(l.token==9&&b.generation==777&&Fake::writes==0);CHECK(!SoundSettings_BlockAutomaticRestart());}
	ResetFixture();SoundSettingsObservation b,o;auto l=Begin({6,true,8},b);SoundSettingsLease stranger=l;stranger.owner=99;o.generation=777;
	CHECK(!SoundSettings_Query(stranger,o,error,sizeof(error)));CHECK(o.generation==777);CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(Fake::writes==0);
	Patch({6,true,8});Fake::ignoreReset=true;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(o.generation==777&&Fake::resets==1);CHECK(SoundSettings_BlockAutomaticRestart());Fake::ignoreReset=false;Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});Fake::resetResult=false;Fake::resetFalseButApplied=true;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));Fake::resetResult=true;Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
	// Every checked native write can refuse, including live-source filters and
	// wet sends. No partial result publishes; explicit restoration remains owned.
	for(int failure=1;failure<=39;++failure){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});Fake::failWrite=failure;o.generation=777;const bool applied=SoundSettings_TryApply(l,o,error,sizeof(error));if(!applied){CHECK(o.generation==777);CHECK(SoundSettings_BlockAutomaticRestart());Fake::failWrite=0;Patch(b.requested);const bool restored=SoundSettings_TryRestore(l,o,error,sizeof(error));if(!restored)std::fprintf(stderr,"restore failure=%d: %s phase=%u\n",failure,error,unsigned(state.phase));CHECK(restored);Complete(l);}else{CHECK(Fake::writes<failure);Fake::failWrite=0;Complete(l);}}
	for(int which=0;which<7;++which){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));switch(which){case 0:s_deviceName.text="External";break;case 1:s_openALHRTF.value=1;break;case 2:s_openALEfxDebugMode.value=2;break;case 3:SoundSettings_DeviceEvent();break;case 4:Fake::defaultName="Another";break;case 5:Fake::connected=false;break;case 6:soundSystemLocal.needsRestart=true;break;}
		soundSystemLocal.Render();CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));CHECK(SoundSettings_BlockAutomaticRestart());CHECK(soundSystemLocal.restarts==0);}
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));world.throws=true;try{soundSystemLocal.Render();CHECK(false);}catch(const std::runtime_error&){}world.throws=false;Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});Fake::filterMutation=true;o.generation=777;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(o.generation==777);Fake::filterMutation=false;Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));SoundSettings_Abandon(l);CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));Complete(l);
	ResetFixture();l=Begin({6,true,8},b);SoundSettings_DeviceDestroyed();CHECK(!SoundSettings_Query(l,o,error,sizeof(error)));
}
static void Adversarial() {
	SoundSettingsObservation b,o;SoundSettingsLease l;
	ResetFixture(true);auto& voice=soundSystemLocal.hardware.voices[0];voice.gain=3;voice.dryLevel=.5f;voice.wetLevel=.25f;voice.occlusion=1;voice.environmentMuffle=1;
	l=Begin({2,true,8},b);Patch({2,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));
	CHECK(std::abs(Fake::filters[voice.openalDirectFilter][0]-.15749013f)<.000001f);
	CHECK(std::abs(Fake::filters[voice.openalDirectFilter][1]-.009843133f)<.000001f);
	CHECK(std::abs(Fake::filters[voice.openalAuxFilter][0]-.15749013f)<.000001f);CHECK(Fake::sources[1].gain==3);
	Complete(l);
	ResetFixture(true);l=Begin({2,false,48},b);Patch({2,false,48});
	denyAllocation=true;CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));SoundSettings_Abandon(l);CHECK(SoundSettings_BlockAutomaticRestart());
	Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));soundSystemLocal.Render();CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));denyAllocation=false;
	for(int mode:{ALC_STEREO_BASIC_SOFT,ALC_STEREO_UHJ_SOFT,ALC_STEREO_HRTF_SOFT}) {
		ResetFixture();s_numberOfSpeakers.value=6;Fake::mode=ALC_SURROUND_5_1_SOFT;Fake::stereoObserved=mode;
		if(mode==ALC_STEREO_HRTF_SOFT){Fake::hrtf=1;Fake::status=ALC_HRTF_ENABLED_SOFT;}
		l=Begin({2,false,48},b);Patch({2,false,48});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(o.outputMode==mode);Complete(l);
	}
	ResetFixture();soundSystemLocal.hardware.efxFiltersAvailable=false;Fake::efxSupport=false;
	for(auto& v:soundSystemLocal.hardware.voices){v.openalDirectFilter=0;v.openalAuxFilter=0;}
	l=Begin({6,false,8},b);Patch({6,false,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));Complete(l);

	ResetFixture(true);Fake::effects[11]=AL_EFFECT_REVERB;l=Begin({2,false,48},b);Patch({2,false,48});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(Fake::effects[11]==AL_EFFECT_REVERB);Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));CHECK(o.effectType==AL_EFFECT_REVERB&&o.effect==11&&o.slot==10);Complete(l);
	ResetFixture();soundSystemLocal.hardware.voices[1].openalDirectFilter=0;SoundSettingsLease untouched{9,9,9};b.generation=888;CHECK(!SoundSettings_Begin(1,2,{6,true,8},untouched,b,error,sizeof(error)));CHECK(Fake::writes==0&&untouched.token==9&&b.generation==888);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});Fake::mode=ALC_MONO_SOFT;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(Fake::writes==0);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));soundSystemLocal.muted=true;soundSystemLocal.Render();CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));CHECK(o.muted&&Fake::listener==0);
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));soundSystemLocal.Render();++soundSystemLocal.hardware.voices[0].soundSettingsSourceGeneration;CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));soundSystemLocal.Render();CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});int start=Fake::calls;CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));const int applyCalls=Fake::calls-start;
	for(int kind:{1,2,4,5})for(int offset=1;offset<=applyCalls;++offset){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;o.generation=999;const bool result=SoundSettings_TryApply(l,o,error,sizeof(error));CHECK(Fake::callbackAt==0);CHECK(!result&&o.generation==999);CHECK(Fake::writes<=Fake::writesAtCallback+1);}
	// A later normal update can introduce a new valid source lifetime. Both
	// newly allocated filters and the real wet send are checked before completion.
	for(bool failure:{false,true}) {
		ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));
		auto& h=soundSystemLocal.hardware;h.voices.resize(4);h.voices[3].openalSource=4;h.voices[3].soundSettingsSourceGeneration=SoundSettings_SourceCreated();Fake::sources[4]={};
		if(failure)Fake::failWrite=Fake::writes+34; // first new filter allocation after listener + three routes
		soundSystemLocal.Render();
		if(failure){CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));Fake::failWrite=0;Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));soundSystemLocal.Render();}
		CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));CHECK(o.sourceCount==4&&o.sources[3].source==4&&o.sources[3].directFilter!=0);const auto direct=h.voices[3].openalDirectFilter,aux=h.voices[3].openalAuxFilter;h.voices[3].DestroyWetDryFilters();CHECK(!Fake::filters.count(direct)&&!Fake::filters.count(aux)&&!h.voices[3].soundSettingsDeleteFilters);
	}

	ResetFixture();start=Fake::calls;l=Begin({6,true,8},b);const int beginCalls=Fake::calls-start;
	for(int kind:{1,2,4})for(int offset=1;offset<=beginCalls;++offset){ResetFixture();Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;SoundSettingsLease captureLease{7,8,9};o.generation=999;CHECK(!SoundSettings_Begin(1,2,{6,true,8},captureLease,o,error,sizeof(error)));CHECK(o.generation==999&&captureLease.token==9&&Fake::writes==0);}
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));soundSystemLocal.Render();start=Fake::calls;CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));const int finishCalls=Fake::calls-start;
	for(int kind:{1,2,4,5})for(int offset=1;offset<=finishCalls;++offset){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));soundSystemLocal.Render();Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;o.generation=999;CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));CHECK(o.generation==999&&soundSystemLocal.hardware.openedSpeakerCount==2);}

	// All actual callbacks during normal validation see the held update latch.
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));start=Fake::calls;soundSystemLocal.Render();const int updateCalls=Fake::calls-start;
	for(int kind:{3,6})for(int offset=1;offset<=updateCalls;++offset){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;soundSystemLocal.Render();CHECK(Fake::callbackAt==0&&Fake::reentryRejected);CHECK(SoundSettings_Finish(l,o,error,sizeof(error)));}
}
static void Recovery() {
	SoundSettingsObservation b,o;SoundSettingsLease l;
	for(bool partial:{false,true}) {
		ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});
		if(partial){Fake::failWrite=2;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));Fake::failWrite=0;}
		else {CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));soundSystemLocal.Render();}
		const auto originalToken=l.token,priorGeneration=generation;SoundSettings_DeviceEvent();Patch(b.requested);
		const int writes=Fake::writes;CHECK(SoundSettings_RevalidateRestore(l,o,error,sizeof(error)));CHECK(Fake::writes==writes);
		CHECK(state.restoreOnly&&state.lease.token==originalToken&&state.baseline.device==b.device&&state.baseline.outputMode==b.outputMode);
		CHECK(state.baseline.effect==b.effect&&state.baseline.slot==b.slot&&Same(state.baseline.requested,b.requested));
		CHECK(o.generation>priorGeneration&&o.routingGeneration==0&&o.routingUpdate==0&&!o.effectsVerified);
		CHECK(!state.validUpdate&&state.operationToken==0&&SoundSettings_BlockAutomaticRestart());
		CHECK(!SoundSettings_Finish(l,o,error,sizeof(error)));CHECK(!SoundSettings_NativeOperationCurrent());
		Patch({6,true,8});const int beforeCalls=Fake::calls;CHECK(!SoundSettings_TryApply(l,o,error,sizeof(error)));CHECK(Fake::calls==beforeCalls&&Fake::writes==writes);
		Patch(b.requested);CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));CHECK(o.effectsVerified&&o.outputMode==b.outputMode&&o.efx==b.efx&&o.slot==b.slot&&o.effect==b.effect);Complete(l);
		// A fresh request after proved restoration has ordinary Apply authority.
		l=Begin({6,true,8},b);CHECK(!state.restoreOnly);SoundSettings_Abandon(l);
	}
	// No event, non-baseline CVars, foreign owner, changed native dependencies
	// and genuinely unavailable devices cannot renew or silently release a lease.
	for(int which=0;which<11;++which){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));Patch(b.requested);if(which!=0)SoundSettings_DeviceEvent();
		switch(which){case 1:s_numberOfSpeakers.value=6;break;case 2:++l.owner;break;case 3:Fake::defaultName="Different";break;case 4:Fake::connected=false;break;case 5:Fake::actual="Different";break;case 6:s_openALHRTF.value=1;break;case 7:s_openALEfxDebugMode.value=1;break;case 8:soundSystemLocal.needsRestart=true;break;case 9:mainThread=false;break;case 10:Fake::support=false;break;}
		const auto oldEvents=state.events,oldGeneration=generation;const auto oldPhase=state.phase;const int oldWrites=Fake::writes;o.generation=888;
		CHECK(!SoundSettings_RevalidateRestore(l,o,error,sizeof(error)));CHECK(o.generation==888&&state.events==oldEvents&&generation==oldGeneration&&state.phase==oldPhase&&Fake::writes==oldWrites);
		CHECK(SoundSettings_BlockAutomaticRestart());SoundSettings_Abandon(l);CHECK(SoundSettings_BlockAutomaticRestart());}
	// Each failed observation preserves the old watermark. Only a later complete
	// same-context observation authorizes baseline restoration.
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));SoundSettings_DeviceEvent();Patch(b.requested);
	int start=Fake::calls;CHECK(SoundSettings_RevalidateRestore(l,o,error,sizeof(error)));const int calls=Fake::calls-start;
	for(int kind:{1,2,4,7,8})for(int offset=1;offset<=calls;++offset){ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));SoundSettings_DeviceEvent();Patch(b.requested);const auto oldEvents=state.events,oldGeneration=generation;const int oldWrites=Fake::writes;Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;o.generation=888;
		CHECK(!SoundSettings_RevalidateRestore(l,o,error,sizeof(error)));CHECK(o.generation==888&&state.events==oldEvents&&generation==oldGeneration&&Fake::writes==oldWrites);}
	// Repeated healthy groups retain their original baseline and can recover
	// from more than one event before restoration has completed.
	ResetFixture();l=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(l,o,error,sizeof(error)));Patch(b.requested);
	for(int i=0;i<3;++i){SoundSettings_DeviceEvent();CHECK(SoundSettings_RevalidateRestore(l,o,error,sizeof(error)));CHECK(SoundSettings_TryRestore(l,o,error,sizeof(error)));}
	Complete(l);
}

static void CopiedInputs() {
	// Count every real Begin observation call first. Mutating caller storage is
	// independent of native/CVar ownership: the originally submitted value wins.
	SoundSettingsObservation b,o;SoundSettingsLease lease;
	ResetFixture();SoundSettingsPolicy policy{6,true,8};
	CHECK(SoundSettings_Begin(1,2,policy,lease,b,error,sizeof(error)));const int beginCalls=Fake::calls;
	for(const SoundSettingsPolicy mutation: {SoundSettingsPolicy{3,true,999},SoundSettingsPolicy{2,false,48}})
		for(int offset=1;offset<=beginCalls;++offset) {
			ResetFixture();policy={6,true,8};Fake::callerPolicy=&policy;Fake::policyMutation=mutation;
			Fake::callbackKind=9;Fake::callbackAt=offset;
			CHECK(SoundSettings_Begin(1,2,policy,lease,b,error,sizeof(error)));
			CHECK(Fake::callbackAt==0&&Same(policy,mutation));
			CHECK(Same(state.target,SoundSettingsPolicy{6,true,8})&&Fake::writes==0);
			Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));Complete(lease);
		}
	const auto prepare=[&](int operation) {
		ResetFixture();lease=Begin({6,true,8},b);
		if(operation==0)Patch({6,true,8});
		if(operation>=2) {
			Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));
			if(operation==2)Patch(b.requested);
			if(operation==3)soundSystemLocal.Render();
			if(operation==4){SoundSettings_DeviceEvent();Patch(b.requested);}
		}
	};
	const auto invoke=[&](int operation,SoundSettingsLease& caller) {
		switch(operation) {
		case 0:return SoundSettings_TryApply(caller,o,error,sizeof(error));
		case 1:return SoundSettings_Query(caller,o,error,sizeof(error));
		case 2:return SoundSettings_TryRestore(caller,o,error,sizeof(error));
		case 3:return SoundSettings_Finish(caller,o,error,sizeof(error));
		default:return SoundSettings_RevalidateRestore(caller,o,error,sizeof(error));
		}
	};
	for(int operation=0;operation<5;++operation) {
		prepare(operation);int start=Fake::calls;CHECK(invoke(operation,lease));const int calls=Fake::calls-start;
		for(int offset=1;offset<=calls;++offset) {
			prepare(operation);const SoundSettingsLease original=lease;const auto baseline=state.baseline;
			Fake::callerLease=&lease;Fake::callbackKind=10;Fake::callbackAt=Fake::calls+offset;
			CHECK(invoke(operation,lease));CHECK(Fake::callbackAt==0&&lease.owner==901&&lease.request==902&&lease.token==903);
			CHECK(operation==3?!SoundSettings_BlockAutomaticRestart():Same(state.lease,original));
			CHECK(state.baseline.device==baseline.device&&Same(state.baseline.requested,baseline.requested));
			if(operation==4){CHECK(state.restoreOnly);CHECK(SoundSettings_TryRestore(original,o,error,sizeof(error)));Complete(original);}
		}
	}
}
static void PreparationAndCompletionLease() {
	SoundSettingsObservation b,o;SoundSettingsLease lease;
	for(bool update:{false,true}) {
		ResetFixture();lease=Begin({6,true,8},b);if(update)soundSystemLocal.Render();
		const int writes=Fake::writes;const auto baseline=state.baseline;
		soundSystemLocal.muted=true;
		CHECK(SoundSettings_CancelCaptured(lease,o,error,sizeof(error)));
		CHECK(!SoundSettings_BlockAutomaticRestart()&&Fake::writes==writes&&soundSystemLocal.muted);
		CHECK(o.phase==SoundSettingsPhase::Captured&&Same(o.requested,baseline.requested)&&state.baseline.device==baseline.device);
		CHECK(soundSystemLocal.hardware.openedSpeakerCount==2);
		o.generation=999;CHECK(!SoundSettings_CancelCaptured(lease,o,error,sizeof(error)));CHECK(o.generation==999);
	}
	for(int defect=0;defect<6;++defect) {
		ResetFixture();lease=Begin({6,true,8},b);
		if(defect==0)++lease.request;
		if(defect==1)SoundSettings_Abandon(lease);
		if(defect==2){Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));Patch(b.requested);}
		if(defect==3)Fake::mode=ALC_SURROUND_5_1_SOFT;
		if(defect==4)s_numberOfSpeakers.value=6;
		if(defect==5)SoundSettings_DeviceEvent();
		const int writes=Fake::writes;o.generation=999;
		CHECK(!SoundSettings_CancelCaptured(lease,o,error,sizeof(error)));
		CHECK(o.generation==999&&Fake::writes==writes&&SoundSettings_BlockAutomaticRestart());
	}
	for(bool restore:{false,true}) {
		ResetFixture();lease=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));
		if(restore){Patch(b.requested);CHECK(SoundSettings_TryRestore(lease,o,error,sizeof(error)));}
		o.generation=999;CHECK(!SoundSettings_CheckCompletion(lease,o,error,sizeof(error)));CHECK(o.generation==999);
		soundSystemLocal.Render();const int writes=Fake::writes;
		for(int i=0;i<2;++i) {
			CHECK(SoundSettings_CheckCompletion(lease,o,error,sizeof(error)));
			CHECK(SoundSettings_BlockAutomaticRestart()&&Same(state.lease,lease)&&Fake::writes==writes);
			CHECK(o.routingUpdate>0&&o.routingGeneration==o.generation&&o.sourceCount==3);
			CHECK(soundSystemLocal.hardware.openedSpeakerCount==2);
		}
		SoundSettings_DeviceEvent();o.generation=999;
		CHECK(!SoundSettings_Finish(lease,o,error,sizeof(error)));CHECK(o.generation==999&&SoundSettings_BlockAutomaticRestart());
	}
	// Each real query boundary may revoke the backend, attempt reentry or mutate
	// the caller's copied lease. Only the original non-revoked operation can win.
	for(bool completion:{false,true}) {
		const auto prepare=[&] {
			ResetFixture();lease=Begin({6,true,8},b);
			if(completion){Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));soundSystemLocal.Render();}
		};
		const auto invoke=[&] {
			return completion?SoundSettings_CheckCompletion(lease,o,error,sizeof(error)):SoundSettings_CancelCaptured(lease,o,error,sizeof(error));
		};
		prepare();int start=Fake::calls;CHECK(invoke());const int calls=Fake::calls-start;
		for(int kind:{1,2,3,4,6,7,8,10})for(int offset=1;offset<=calls;++offset) {
			prepare();const auto original=lease;const int writes=Fake::writes;
			Fake::callerLease=&lease;Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;o.generation=999;
			const bool succeeded=invoke();CHECK(Fake::callbackAt==0&&Fake::writes==writes);
			if(kind==3||kind==6||kind==10) {
				CHECK(succeeded);CHECK(completion?Same(state.lease,original):!SoundSettings_BlockAutomaticRestart());
				if(kind==10)CHECK(lease.token==903);else CHECK(Fake::reentryRejected);
			} else CHECK(!succeeded&&o.generation==999);
		}
	}
	ResetFixture();lease=Begin({6,true,8},b);Patch({6,true,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));
	soundSystemLocal.Render();CHECK(SoundSettings_CheckCompletion(lease,o,error,sizeof(error)));
	CHECK(SoundSettings_Finish(lease,o,error,sizeof(error)));CHECK(!SoundSettings_BlockAutomaticRestart()&&soundSystemLocal.hardware.openedSpeakerCount==6);
}
int main(){Positive();Negative();Adversarial();Recovery();CopiedInputs();PreparationAndCompletionLease();std::printf("%d checks passed\n",checks);}
