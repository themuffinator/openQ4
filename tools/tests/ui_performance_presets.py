#!/usr/bin/env python3
"""Compile shared profile/transaction and unchanged Common/host/adapter bodies.
No engine, device, settings I/O or native input is used. Counted host boundaries
qualify draft publication and legacy calls, not actual hardware application.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from filesystem_case_segments import function_body
import ui_system_settings_host as host_test
ROOT=Path(__file__).resolve().parents[2]

HOST_MAIN=r'''
static int checks=0;
static void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 Seed();SystemSettingsHost host;std::string error;StateValues original;
 Check(host.Read(original,error),"complete host baseline");
 const std::set<std::string> keys(openq4::PerformancePresetTargets().begin(),openq4::PerformancePresetTargets().end());
 Check(keys.size()==55&&original.size()==76,"catalog union");
 for(const auto& preset:openq4::PerformancePresets()){
  SettingsTransaction tx(host);Check(tx.Begin(7).code==SettingsCode::Ok,"begin real host");
  Check(tx.Edit(7,{{"r_brightness",1.375},{"r_forceAmbient",0.1375}}).code==SettingsCode::Ok,"custom outside draft");
  const auto previous=tx.Draft();
  StateValues patch;Check(host.BuildPreset(preset.name,patch,error)&&patch.size()==55,"real typed expansion");
  Check(tx.EditGenerated(7,[&](StateValues& out,std::string& why){return host.BuildPreset(preset.name,out,why);}).code==SettingsCode::Ok,"whole preset atomically drafted");
  int untouched=0;for(const auto& [key,value]:previous){
   Check(SettingsValueEqual(tx.Draft().at(key),keys.contains(key)?patch.at(key):value),"55 replacements and exact outside preservation");
   if(!keys.contains(key))++untouched;
  }
  Check(untouched==21&&SettingsValuesEqual(tx.Baseline(),original),"21 outside keys and baseline preserved");
  Check(writes==0,"draft preset never invokes setter");
  StateValues readback;Check(host.Read(readback,error)&&SettingsValuesEqual(original,readback),"all 76 live unchanged");
  Check((SystemSettingsHost::ChangedEffects(tx.Baseline(),tx.Draft())&(SystemSettingPresetExpansion|SystemSettingImageReload|SystemSettingAudioRestart|SystemSettingRendererResources))!=0,"profile remains real unsupported effect footprint");
 }
 SettingsTransaction tx(host);Check(tx.Begin(1).code==SettingsCode::Ok,"detection owner");
 presetSignals.systemRamMB=16384;presetSignals.videoRamMB=8192;presetSignals.gpuClass=RendererGpuClass::Discrete;presetSignals.modernEffects=true;presetSignals.logicalCpus=8;presetSignals.drawableWidth=1920;presetSignals.drawableHeight=1080;
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildDetectedPreset(out,why);}).code==SettingsCode::Ok,"read-only auto detection");
 Check(tx.Draft().at("com_performancePreset")==StateValue(std::string("enhanced"))&&presetSignalReads==1&&writes==0,"auto enhanced without setters");
 presetSignals.gpuClass=RendererGpuClass::Integrated;presetSignals.drawableWidth=3840;presetSignals.drawableHeight=2160;presetSignals.modernEffects=false;
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildDetectedPreset(out,why);}).code==SettingsCode::Ok,"4K shared GPU draft");
 Check(tx.Draft().at("com_performancePreset")==StateValue(std::string("performance"))&&tx.Draft().at("r_screenFraction")==StateValue(50.0)&&tx.Draft().at("r_postAA")==StateValue(0.0)&&writes==0,"typed host retains detected scale and unsupported-AA result");
 const auto saved=tx.Draft();failPresetSignals=true;
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildDetectedPreset(out,why);}).code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"capability failure unchanged");
 failPresetSignals=false;presetSignalCallback=[&]{Check(tx.Cancel(1).code==SettingsCode::Busy,"capability collection under transaction guard");};
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildDetectedPreset(out,why);}).code==SettingsCode::Busy&&SettingsValuesEqual(tx.Draft(),saved),"capability reentry aborts expansion");presetSignalCallback={};
 StateValues untouched={{"sentinel",true}};
 for(const auto& bad:std::vector<std::string>{"","missing","ULTRA ",std::string("quality\0bad",11)})
  Check(!host.BuildPreset(bad,untouched,error)&&untouched==StateValues({{"sentinel",true}}),"invalid names preserve output");
 // The complete merged validation sees unavailable/readonly targets before
 // publication; expansion itself must never normalize by invoking a setter.
 auto& samples=localCVarSystem.variables.at("r_multiSamples");samples.flags|=CVAR_ROM;
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildPreset("balanced",out,why);}).code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"readonly target rejects whole batch");
 Check(writes==0,"zero actual setters across all host cases");
 std::printf("real SYSTEM preset host: %d checks passed\n",checks);
}
'''
COMMON_SUPPORT=r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "src/framework/PerformancePreset.h"
#include "src/framework/CVarAssignmentCapture.h"
#include "src/sys/DisplayModeRule.h"
static int checks=0;
static void Check(bool v,const char* m){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
struct idStr:std::string{
 using std::string::string;using std::string::operator=;
 void Clear(){clear();}
 static int Icmp(const char* a,const char* b){for(;*a&&*b;++a,++b){auto x=std::tolower((unsigned char)*a),y=std::tolower((unsigned char)*b);if(x!=y)return x-y;}return *a-*b;}
};
using cvarFlags_t=int;enum{CVAR_ARCHIVE=1,CVAR_ALL=-1};
struct idCVar{
 std::string value="sentinel";int flags=16;
 const char* GetString()const{return value.c_str();}int GetInteger()const{return std::atoi(value.c_str());}
 int GetFlags()const{return flags;}void RemoveFlag(int f){flags&=~f;}void SetFlag(int f){flags|=f;}void ClearModified(){}
 void SetInteger(int n){value=std::to_string(n);}
};
struct Cvars{
 std::map<std::string,idCVar> vars;std::vector<std::string> calls;int modified=64;std::string refuse;
 idCVar* Find(const char* name){auto i=vars.find(name);return i==vars.end()?nullptr:&i->second;}
 void SetCVarString(const char* key,const char* value,int flags){calls.push_back(key);if(refuse==key)return;openq4::CVarAssignmentCapture::Record(key);auto& v=vars.at(key);v.value=std::string(key)=="com_performancePreset"&&!*value?"balanced":value;v.flags|=flags;modified|=flags;}
 void SetCVarInteger(const char* key,int value,int flags){if(std::string(key)=="image_anisotropy"&&value>16)value=16;SetCVarString(key,std::to_string(value).c_str(),flags);}
 float GetCVarFloat(const char* key){auto i=vars.find(key);return i==vars.end()?0.0f:(float)std::atof(i->second.value.c_str());}
 int GetModifiedFlags()const{return modified;}void ClearModifiedFlags(int f){modified&=~f;}void SetModifiedFlags(int f){modified|=f;}
} cvars,*cvarSystem=&cvars;
struct Common{int warnings=0,prints=0;void Warning(const char*,...){++warnings;}void Printf(const char*,...){++prints;}void SetMachineSpec();} instance,*common=&instance;
using idCommonLocal=Common;
static idCVar com_platformProfile,com_performancePreset,com_machineSpec,com_videoRam;
static std::set<std::string> startupVariables;
static bool Common_HasStartupVariable(const char* name){return startupVariables.contains(name);}
static openq4::PerformancePresetSignals raw;
static std::vector<std::string> observations;
static bool Common_HasExplicitLowPowerHostSignal(){observations.push_back("explicit");return raw.explicitLowPower;}
static bool Common_HasRaspberryPiHostSignal(){observations.push_back("pi");return raw.raspberryPi;}
static bool Common_HasSteamDeckHostSignal(){observations.push_back("deck");return raw.steamDeck;}
static bool Common_HostCpuIsArm64(){observations.push_back("arm");return raw.arm64;}
// Legacy platform getters can manufacture a fallback; the SDL observation must
// retain zero for unavailable telemetry instead of reporting constrained RAM.
static int Sys_GetSystemRam(){observations.push_back("legacy-ram");return raw.systemRamMB>0?raw.systemRamMB:512;}
static int SDL_GetSystemRAM(){observations.push_back("ram");return raw.systemRamMB;}
static int Sys_GetVideoRam(){observations.push_back("vram");return raw.videoRamMB;}
#define USE_SDL3 1
static int SDL_GetNumLogicalCPUCores(){observations.push_back("cpu");return raw.logicalCpus;}
// The display rate a detected start caps at (FPS-02); not an observation the
// ordering checks count.
static float Sys_SDL_GetWindowRefreshHz(){return (float)raw.displayRefreshHz;}
struct Config{bool isInitialized=true;int dedicatedVideoMemoryMB=0,vidWidth=0,vidHeight=0,maxTextureSize=0;RendererGpuClass gpuClass=RendererGpuClass::Unknown;const char* version_string="OpenGL";bool GLSL130Available=true,GLSLProgramAvailable=true,ARBVertexProgramAvailable=true,ARBFragmentProgramAvailable=true;struct{bool hasFBO=true;}backendCaps;};
struct Renderer{
 bool initialized=true;
 bool overrideConfig=false;Config config;
 Config GetGLConfig(){if(overrideConfig)return config;Config c;c.isInitialized=initialized;c.dedicatedVideoMemoryMB=raw.videoRamMB;c.vidWidth=raw.drawableWidth;c.vidHeight=raw.drawableHeight;c.gpuClass=raw.gpuClass;c.GLSL130Available=raw.modernEffects;c.GLSLProgramAvailable=c.ARBVertexProgramAvailable=c.ARBFragmentProgramAvailable=!raw.legacyRenderer;c.backendCaps.hasFBO=!raw.legacyRenderer;c.maxTextureSize=raw.modernEffects?4096:2048;return c;}
} renderer,*renderSystem=&renderer;
static idStr Sys_FormatMemoryMB(int n){return idStr(std::to_string(n).c_str());}
static const char* va(const char* format,...){static char text[2048];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);return text;}
struct idCmdArgs{std::vector<std::string> args;int Argc()const{return (int)args.size();}const char* Argv(int i)const{return args.at(i).c_str();}};
static void Seed(){cvars={};instance={};for(const auto* key:openq4::PerformancePresetTargets())cvars.vars.emplace(key,idCVar{});}
'''
COMMON_MAIN=r'''
int main(){
 for(const auto& preset:openq4::PerformancePresets()){
  Seed();openq4::PerformancePresetAssignments expected;std::string error;
  Check(openq4::ExpandPerformancePreset(preset,expected,error),"shared expansion");
  Check(Common_ApplyPerformancePreset(preset),"actual Common apply");
  Check(cvars.calls.size()==55&&instance.warnings==0&&instance.prints==1,"exact successful call count");
  for(size_t i=0;i<expected.size();++i){Check(cvars.calls[i]==expected[i].key,"old setter ordering and marker last");
   const auto& value=expected[i].value;Check(cvars.vars.at(expected[i].key).value==(value.index()==0?std::to_string(std::get<int>(value)):std::get<std::string>(value)),"real accepted readback");
   Check(cvars.vars.at(expected[i].key).flags==17,"archive flags preserved on success");}
 }
 for(const auto* key:openq4::PerformancePresetTargets()){
  Seed();cvars.vars.erase(key);Check(!Common_ApplyPerformancePreset(openq4::PerformancePresets()[3]),"missing declaration rejected");
  Check(cvars.calls.empty()&&instance.warnings==1&&cvars.modified==64,"all targets checked before any setter");
 }
 for(bool marker:{false,true}){
  Seed();cvars.refuse=marker?"com_performancePreset":"r_postAA";
  Check(!Common_ApplyPerformancePreset(openq4::PerformancePresets()[3]),"setter refusal rejected");
  Check(cvars.calls.size()==(marker?110:109)&&instance.warnings==1&&cvars.modified==64,"rollback and global flags");
  for(const auto& [key,value]:cvars.vars)Check(value.value=="sentinel"&&value.flags==16,"all target values and flags restored");
  if(!marker)Check(cvars.calls[54]=="com_performancePreset","first marker setter belongs only to rollback");
 }
 Seed();auto invalid=openq4::PerformancePresets()[2];invalid.anisotropy=17;
 Check(!Common_ApplyPerformancePreset(invalid,true)&&instance.warnings==0&&instance.prints==0,"normalization rejected quietly");
 for(const auto& [key,value]:cvars.vars)Check(value.value=="sentinel"&&value.flags==16,"normalization rollback");
 Seed();Check(!Common_SetPerformancePresetInt("profile","not-declared",1,false)&&cvars.calls.empty(),"undeclared setters forbidden");
 Seed();com_performancePreset.value="unrecognized";
 Check(Common_ApplyPerformancePresetCommand({{"applyPerformancePreset"}},true)&&cvars.vars.at("com_performancePreset").value=="balanced","invalid saved selection uses old default");
 Seed();Check(!Common_ApplyPerformancePresetCommand({{"applyPerformancePreset","unknown"}},true)&&cvars.calls.empty(),"explicit invalid name refused");
 Check(!Common_ApplyPerformancePresetCommand({{"applyPerformancePreset","quality","extra"}},true)&&cvars.calls.empty(),"extra arguments refused");
 Check(!Common_AutoDetectPerformancePresetCommand({{"autoDetectPerformancePreset","extra"}},true)&&cvars.calls.empty(),"auto extra arguments refused");
 for(int spec=0;spec<4;++spec){
  Seed();com_machineSpec.value=std::to_string(spec);Com_ExecMachineSpec_f({{"execMachineSpec"}});
  const char* names[]={"low","performance","balanced","ultra"};
  Check(cvars.calls.size()==55&&cvars.vars.at("com_performancePreset").value==names[spec],"actual compatibility command uses complete canonical expansion only");
  Check(cvars.vars.at("r_shadows").value=="1","every stock quality choice keeps the original shadows");
 }
 for(const char* spec:{"-1","4","2147483647"}){
  Seed();com_machineSpec.value=spec;Com_ExecMachineSpec_f({{"execMachineSpec"}});Check(cvars.calls.empty(),"unset or invalid stock spec is nondestructive");
 }
 Seed();com_machineSpec.value="3";Com_ExecMachineSpec_f({{"execMachineSpec","extra"}});Check(cvars.calls.empty(),"compatibility command rejects extra arguments");
 Seed();raw={};renderer.initialized=false;com_platformProfile.value="desktop";observations.clear();
 instance.SetMachineSpec();
 Check(com_machineSpec.GetInteger()==1&&com_videoRam.GetInteger()==0&&cvars.calls.empty(),"pre-device unknown RAM keeps conservative classification without applying a preset");
 Check(std::find(observations.begin(),observations.end(),"legacy-ram")==observations.end(),"pre-device classification also avoids invented RAM fallback");
 renderer.initialized=true;
 for(int flags=0;flags<32;++flags){
  Seed();raw={};raw.explicitLowPower=flags&1;raw.raspberryPi=flags&2;raw.steamDeck=flags&4;raw.legacyRenderer=flags&8;raw.arm64=flags&16;raw.systemRamMB=16384;raw.videoRamMB=8192;raw.gpuClass=RendererGpuClass::Discrete;raw.modernEffects=!raw.legacyRenderer;raw.logicalCpus=8;raw.drawableWidth=1920;raw.drawableHeight=1080;
  observations.clear();com_platformProfile.value="desktop";openq4::PerformancePresetSignals got;std::string error;
  Check(Common_CapturePerformancePresetSignals(got,error),"actual readonly observation");
  Check(observations==std::vector<std::string>({"explicit","pi","deck","arm","ram","cpu"})&&cvars.calls.empty()&&cvars.modified==64,"active device observation never reads host-wide VRAM or writes settings");
  Check(got.gpuClass==raw.gpuClass&&got.videoRamMB==raw.videoRamMB&&got.drawableWidth==1920&&got.logicalCpus==8,"active device facts captured");
  Check(openq4::DetectPerformancePreset(got).preset==openq4::DetectPerformancePreset(raw).preset,"actual capture uses pure branch selection");
 }
 raw={};observations.clear();com_platformProfile.value="steamdeck";openq4::PerformancePresetSignals got;std::string error;
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.steamDeck&&observations==std::vector<std::string>({"explicit","pi","arm","ram","cpu"}),"Deck hint still observes capabilities");
 com_platformProfile.value="desktop";raw.systemRamMB=16384;raw.logicalCpus=8;
 renderer.overrideConfig=true;renderer.config={};renderer.config.gpuClass=RendererGpuClass::Integrated;
 renderer.config.vidWidth=1920;renderer.config.vidHeight=1080;renderer.config.maxTextureSize=16384;
 renderer.config.GLSL130Available=false;
 Check(Common_CapturePerformancePresetSignals(got,error)&&!got.legacyRenderer&&!got.modernEffects&&!got.postAAAvailable,"Apple GLSL 1.20 lacks modern effects, not classic material quality");
 Check(std::string(openq4::DetectPerformancePreset(got).preset->name)=="balanced","Apple compatibility context keeps native classic quality");
 renderer.config.GLSLProgramAvailable=false;
 Check(Common_CapturePerformancePresetSignals(got,error)&&!got.legacyRenderer,"complete ARB programs retain classic rendering without GLSL");
 renderer.config.ARBFragmentProgramAvailable=false;
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.legacyRenderer&&std::string(openq4::DetectPerformancePreset(got).preset->name)=="minimum","incomplete classic material pipeline remains constrained");
 renderer.config.GLSLProgramAvailable=true;renderer.config.maxTextureSize=1024;
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.legacyRenderer,"small texture limits remain constrained even with shader objects");
 renderer.config.maxTextureSize=16384;renderer.config.backendCaps.hasFBO=false;
 Check(Common_CapturePerformancePresetSignals(got,error)&&!got.legacyRenderer&&!got.modernEffects,"missing optional FBOs do not rank classic throughput");
 raw.systemRamMB=0;
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.systemRamMB==0&&std::string(openq4::DetectPerformancePreset(got).preset->name)=="performance","failed SDL RAM observation stays unknown without a fabricated 512 MiB fallback");
 raw.systemRamMB=16384;renderer.config.vidWidth=3840;renderer.config.vidHeight=2160;
 Seed();Check(Common_AutoDetectPerformancePresetCommand({{"autoDetectPerformancePreset"}},true),"actual 4K classic auto command");
 Check(cvars.vars.at("r_screenFraction").value=="50"&&cvars.vars.at("r_postAA").value=="0"&&cvars.vars.at("com_performancePreset").value=="performance","console retains automatic scene/AA adjustments");
 renderer.config.version_string="Vulkan";
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.postAAAvailable&&got.modernEffects&&!got.legacyRenderer,"native Vulkan capability independent of GL flags");
 renderer.overrideConfig=false;
 com_platformProfile.value="desktop";got.systemRamMB=123;renderer.initialized=false;
 Check(!Common_CapturePerformancePresetSignals(got,error)&&got.systemRamMB==123,"uninitialized observation output unchanged");
 Seed();Check(!Common_AutoDetectPerformancePresetCommand({{"autoDetectPerformancePreset"}},true)&&cvars.calls.empty(),"unavailable auto never applies a fabricated fallback");
 renderer.initialized=true;renderSystem=nullptr;
 Check(!Common_CapturePerformancePresetSignals(got,error)&&got.systemRamMB==123,"absent observation output unchanged");
 renderSystem=&renderer;
 // Execute the actual first-launch merge, not a second copy of its policy.
 bool assigned[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT]={};
 const auto bootstrap=[&](auto& baseline){
  Seed();startupVariables.clear();com_platformProfile.value="desktop";
  std::fill_n(assigned,OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT,false);
  Check(Common_BootstrapStartupPerformancePreset(baseline),"actual small pre-device/bootstrap after shipped defaults");
  Check(cvars.vars.at("com_performancePreset").value=="minimum","bootstrap keeps a valid enum marker");
  com_performancePreset.value="minimum";cvars.calls.clear();
  raw={};raw.systemRamMB=32768;raw.videoRamMB=8192;raw.gpuClass=RendererGpuClass::Discrete;
  raw.modernEffects=true;raw.logicalCpus=8;raw.drawableWidth=1920;raw.drawableHeight=1080;
 };
 openQ4PerformancePresetCVarBackup_t baseline[OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT];
 bootstrap(baseline);
 openq4::CVarAssignmentCapture capture;capture.Begin(openq4::PerformancePresetTargets().data(),OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT,assigned);
 cvarSystem->SetCVarInteger("r_screenFraction",90,CVAR_ARCHIVE);
 cvarSystem->SetCVarInteger("r_multiSamples",0,CVAR_ARCHIVE); // autoexec value equal to bootstrap
 capture.End();cvars.calls.clear();
 Check(Common_ApplyStartupPerformancePreset(baseline,assigned),"actual initialized first-launch detection");
 Check(cvars.vars.at("com_performancePreset").value=="enhanced"&&cvars.vars.at("r_useShadowMap").value=="1","first launch uses canonical modern ladder");
 Check(cvars.vars.at("r_screenFraction").value=="90"&&cvars.vars.at("r_multiSamples").value=="0","configured and equal-valued autoexec overrides preserved");
 bootstrap(baseline);raw.gpuClass=RendererGpuClass::Integrated;raw.drawableWidth=3840;raw.drawableHeight=2160;raw.modernEffects=false;
 Check(Common_ApplyStartupPerformancePreset(baseline,assigned)&&cvars.vars.at("r_screenFraction").value=="50"&&cvars.vars.at("r_postAA").value=="0","first launch retains the same automatic scene/AA recommendation");
 for(const auto* choice:{"minimum","quality","ultra"}){
  bootstrap(baseline);com_performancePreset.value=choice;
  capture.Begin(openq4::PerformancePresetTargets().data(),OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVAR_COUNT,assigned);
  cvarSystem->SetCVarString("com_performancePreset",choice,CVAR_ARCHIVE);capture.End();
  Check(Common_ApplyStartupPerformancePreset(baseline,assigned)&&cvars.vars.at("com_performancePreset").value==choice,"explicit first-launch profile respected including equal Minimum");
 }
 bootstrap(baseline);renderer.initialized=false;
 Check(!Common_ApplyStartupPerformancePreset(baseline,assigned)&&cvars.calls.empty(),"unavailable first-launch observation does not invent a device");
 renderer.initialized=true;
 bootstrap(baseline);cvars.refuse="r_ssao";
 Check(!Common_ApplyStartupPerformancePreset(baseline,assigned)&&cvars.vars.at("r_useShadowMap").value=="0","failed first-launch expansion rolls back");
 std::printf("actual Common preset commands/observation: %d checks passed\n",checks);
}
'''
BARRIER_SUPPORT=r'''
#include <cstdio>
#include <cstdlib>
#include "src/ui/retained/Document.h"
using namespace openq4::ui;
struct PendingAction{ActionInvocation invocation;struct{unsigned editSession=0;}source;std::string control;};
struct NumberDraftSummary{struct Item{std::string control;};std::vector<Item> blocking;};
struct ModelDouble{std::map<std::string,Action> actions;const Node* FindNode(const std::string&)const{return nullptr;}};
struct DocumentDouble{ModelDouble model;const ModelDouble& Model()const{return model;}};
struct Boundary{DocumentDouble document;
'''
BARRIER_MAIN=r'''
};
int main(){Boundary boundary;PendingAction action;NumberDraftSummary clear,dirty;dirty.blocking.push_back({"number"});
 for(const char* op:{"settings.system.preset","settings.system.autodetect","settings.system.apply","settings.system.applyExit","settings.system.defaults"}){
  action.invocation.operation=op;if(boundary.ConflictsWithNumberDraft(action,clear)||!boundary.ConflictsWithNumberDraft(action,dirty))return 1;
 }
 // A size pick writes the custom width and height that Number fields edit,
 // and a display pick can return the size to Desktop Native (r_mode).
 for(const char* op:{"settings.system.displayMode","settings.system.display"}){
  action.invocation.operation=op;if(!boundary.ConflictsWithNumberDraft(action,dirty))return 3;
 }
 action.invocation.operation="settings.system.displayRefresh";if(boundary.ConflictsWithNumberDraft(action,dirty))return 4;
 action.invocation.operation="settings.system.cancel";if(boundary.ConflictsWithNumberDraft(action,dirty))return 2;
 std::puts("actual adapter local Number bulk guard passed");
}
'''

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def source(path):return (ROOT/path).read_text(encoding='utf-8')
def no_includes(text):return '\n'.join(line for line in text.splitlines() if not line.startswith('#include '))
def programs():
 doc=source('src/ui/retained/Document.cpp')
 valid='namespace openq4::ui {\n'+ '\n'.join(function_body(doc,x) for x in ('bool Utf8(', 'bool ValidStateValue('))+'\n}\n'
 common=source('src/framework/Common.cpp');start=common.index('using openQ4PerformancePreset_t =');end=common.index('static const char *commonCommandCompletionMatch',start)
 init=function_body(common,'void idCommonLocal::InitGame( void ) {')
 assert init.index('Common_ApplyStartupPerformancePreset( startupPresetBootstrap, startupPresetAssigned )')>init.index('session->Init();')
 assert init.index('OpenFileWrite( CONFIG_SPEC )')>init.index('Common_ApplyStartupPerformancePreset( startupPresetBootstrap, startupPresetAssigned )')
 assert 'sysDetect && !SafeMode() && fileSystem->ReadFile( CONFIG_FILE, NULL ) < 0' in init
 assert 'sysDetect && startupPresetReady && !startupRecovery' in init
 assert init.count('Common_BootstrapStartupPerformancePreset( startupPresetBootstrap )')==2
 assert 'Com_ExecMachineSpec_f( args );' not in init, 'repairing an unset legacy marker must not overwrite saved settings'
 menu=source('src/framework/Session_menu.cpp')
 video=menu[menu.index('if ( !idStr::Icmp( cmd, "video" ) )'):]
 video=video[:video.index('if ( idStr::Icmp( vcmd, "restart" )')]
 for command in ('applyPerformancePreset low\\n','applyPerformancePreset performance','applyPerformancePreset balanced','applyPerformancePreset ultra','autoDetectPerformancePreset'):
  assert command in video, 'stock video choices must route through shared profiles'
 assert 'oldSpec' not in video and 'execMachineSpec' not in video and 'setMachineSpec' not in video
 assert init.index('Common_BootstrapStartupPerformancePreset( startupPresetBootstrap )',init.index('exec openq4_defaults.cfg'))<init.index('Common_BuildPlatformProfileConfigName(')
 setter=function_body(source('src/framework/CVarSystem.cpp'),'void idInternalCVar::Set(')
 assert setter.index('CVarAssignmentCapture::Record(')>setter.index('if ( !force )')
 assert setter.index('CVarAssignmentCapture::Record(')<setter.index('if ( unchanged )')
 gl=source('src/renderer/RenderSystem_init.cpp')
 gl_start=gl.index('\t\tconst char *glslVersionString = (const char *)glGetString( GL_SHADING_LANGUAGE_VERSION );')
 gl_end=gl.index('\t\tcommon->Printf(',gl_start)
 shader_caps=r'''#include <cstdio>
#include <cstring>
struct idStr{static int Icmpn(const char* a,const char* b,int n){return std::strncmp(a,b,n);}};
static bool Probe(const char* version){struct {bool GLSL130Available=false;} glConfig;
#define glGetString(unused) version
''' + gl[gl_start:gl_end] + r'''
return glConfig.GLSL130Available;}
int main(){for(auto value:{"1.30","4.60 NVIDIA","OpenGL ES GLSL ES 3.00","OpenGL ES GLSL ES 3.20 V@1"})if(!Probe(value))return 1;
for(auto value:{"1.20","OpenGL ES GLSL ES 1.00","unknown",""})if(Probe(value))return 2;
if(Probe(nullptr))return 3;std::puts("production GL/GLES shader capability parsing passed");}
'''
 shader_caps='#include <initializer_list>\n'+shader_caps
 return {
 'shader_caps':(shader_caps,[]),
 'native':('#include <cmath>\n#include "src/ui/retained/Document.h"\n'+valid+'\n'+source('tools/tests/native/UiPerformancePresetTest.cpp'),['src/framework/PerformancePreset.cpp','src/ui/application/SettingsTransaction.cpp']),
 'host':(host_test.SUPPORT+'\n#include <set>\n'+function_body(source('src/framework/CVarSystem.cpp'),'\nbool CVar_ReadDefault(')+valid+no_includes(source('src/ui/application/SystemSettingsHost.cpp'))+HOST_MAIN,['src/framework/PerformancePreset.cpp','src/ui/retained/Presentation.cpp','src/ui/application/SettingsTransaction.cpp']),
 'common':(COMMON_SUPPORT+common[start:end]+function_body(common,'void idCommonLocal::SetMachineSpec( void ) {')+COMMON_MAIN,['src/framework/PerformancePreset.cpp']),
 # Parse the host library before selecting the extracted Android observer;
 # macOS libc++ otherwise requires Android SDK headers for this host fixture.
 'android_observer':(COMMON_SUPPORT+'\n#define __ANDROID__ 1\n'+function_body(common,'bool Common_CapturePerformancePresetSignals(')+r'''
int main(){raw.systemRamMB=16384;raw.gpuClass=RendererGpuClass::Integrated;raw.modernEffects=true;raw.logicalCpus=8;raw.drawableWidth=1920;raw.drawableHeight=1080;
openq4::PerformancePresetSignals got;std::string error;if(!Common_CapturePerformancePresetSignals(got,error)||!got.mobile||std::string(openq4::DetectPerformancePreset(got).preset->name)!="performance")return 1;
std::puts("production Android observer keeps a mobile power ceiling");}
''',['src/framework/PerformancePreset.cpp']),
 'barrier':(BARRIER_SUPPORT+function_body(source('src/ui/UserInterfaceRetained.cpp'),'bool ConflictsWithNumberDraft(')+BARRIER_MAIN,[]),
 }

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--compiler');parser.add_argument('--sanitize',action='store_true');parser.add_argument('--mutations',action='store_true');parser.add_argument('--adjacent',action='store_true');parser.add_argument('--debug-stl',action='store_true');args=parser.parse_args()
 compiler=args.compiler or shutil.which('clang++') or shutil.which('g++')
 if not compiler:raise RuntimeError('C++20 compiler required')
 out=Path(tempfile.mkdtemp(prefix='performance-presets-',dir=ROOT/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 if args.sanitize:env['ASAN_OPTIONS']='detect_leaks=1'
 result={'passed':False,'compiler':compiler,'sanitized':args.sanitize,'debug_stl':args.debug_stl,'programs':{},'files':{},'limitations':'Counted CVar/renderer/service doubles; no actual device application or production control gesture.'}
 def compile_command(cpp,deps,binary,includes=()):
  if Path(compiler).name.lower() in ('cl','cl.exe'):
   if args.sanitize:raise RuntimeError('This runner qualifies sanitizers with GCC/Clang only')
   defines=['/MDd','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/Od'] if args.debug_stl else ['/MD','/O1']
   dirs=[str(ROOT)]+[includes[i+1] for i in range(0,len(includes),2)]
   return [compiler,'/nologo','/std:c++20','/EHsc',*defines,'/D_CRT_SECURE_NO_WARNINGS',*['/I'+p for p in dirs],'/Fo'+str(out)+os.sep,str(cpp),*deps,'/Fe:'+str(binary)]
  if args.debug_stl:raise RuntimeError('--debug-stl requires actual MSVC cl')
  return [compiler,'-std=c++20','-O1','-D_CRT_SECURE_NO_WARNINGS',*(['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else []),'-I',str(ROOT),*includes,str(cpp),*deps,'-o',str(binary)]
 units=programs()
 if args.adjacent:
  validation=units['native'][0].split('// Copyright (C) 2026',1)[0]
  for name,extra in [('UiSettingsTransactionTest',[]),('UiSettingsDisplayControllerTest',['src/ui/application/SettingsDisplayController.cpp']),('UiSettingsExactValueTest',[])]:
   units[name]=(validation+source('tools/tests/native/'+name+'.cpp'),['src/ui/application/SettingsTransaction.cpp']+extra)
 # Bind actual compiled inputs and excerpt sources. Unrelated engine work is
 # not a dependency of these isolated programs (which do not include engine
 # PCHs). Walk quoted includes so a shared type change still fails closed.
 bound_paths={ROOT/p for p in ['src/framework/Common.cpp','src/framework/CVarSystem.cpp',
  'src/renderer/RenderSystem_init.cpp','src/ui/retained/Document.cpp',
  'src/ui/application/SystemSettingsHost.cpp','src/ui/UserInterfaceRetained.cpp']}
 def bind_includes(code,context):
  for name in re.findall(r'^\s*#include\s+"([^"\n]+)"',code,re.M):
   path=next((p.resolve() for p in [context/name,ROOT/name] if p.is_file()),None)
   if path is not None and path.is_relative_to(ROOT) and path not in bound_paths:
    bound_paths.add(path);bind_includes(path.read_text(encoding='utf-8'),path.parent)
 for code,deps in units.values():
  bind_includes(code,ROOT)
  for dep in deps:
   path=(ROOT/dep).resolve();bound_paths.add(path)
   bind_includes(path.read_text(encoding='utf-8'),path.parent)
 bound_paths=list(bound_paths)
 if args.adjacent:
  # Adjacent runners extract other engine services and keep the wider scope.
  bound_paths+=list((ROOT/'src').rglob('*.h'))+list((ROOT/'src').rglob('*.cpp'))
 bound_paths+=[Path(__file__),ROOT/'tools/tests/ui_system_settings_host.py',ROOT/'tools/tests/filesystem_case_segments.py',ROOT/'tools/tests/native/UiPerformancePresetTest.cpp']
 bound_paths+=list((ROOT/'tools/tests').glob('ui_settings*.py'))+[ROOT/'tools/tests/ui_system_display.py',ROOT/'tools/tests/native/UiSettingsTransactionTest.cpp',ROOT/'tools/tests/native/UiSettingsDisplayControllerTest.cpp',ROOT/'tools/tests/native/UiSettingsExactValueTest.cpp']
 initial_hashes={p.relative_to(ROOT).as_posix():digest(p) for p in bound_paths}
 try:
  for name,(code,deps) in units.items():
   cpp=out/(name+'.cpp');cpp.write_text(code,encoding='utf-8',newline='\n');binary=out/(name+('.exe' if os.name=='nt' else ''))
   command=compile_command(cpp,[str(ROOT/p) for p in deps],binary)
   build=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env);(out/(name+'-compile.log')).write_text(build.stdout)
   if build.returncode:raise RuntimeError(name+' compilation failed: '+build.stdout[-8000:])
   run=subprocess.run([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env);(out/(name+'-run.log')).write_text(run.stdout)
   result['programs'][name]={'exit':run.returncode,'output':run.stdout,'compile':command,'source_sha256':digest(cpp)}
   print(run.stdout.strip(),flush=True)
   if run.returncode:raise RuntimeError(name+' test failed')
  if args.mutations:
   # Each altered production body/TU must compile, then fail a positive oracle.
   mutations=[
    ('lattice-profile','native','src/framework/PerformancePreset.cpp','85, 0, 1, 60,','86, 0, 1, 60,'),
    ('detection-boundary','native','src/framework/PerformancePreset.cpp','ram>0&&ram<=4096','ram>0&&ram<4096'),
    ('classic-uma-demotion','native','src/framework/PerformancePreset.cpp','tier=Balanced;result.reason=Reason::Shared;','tier=signals.modernEffects?Balanced:Performance;result.reason=Reason::Shared;'),
    ('shadowless-floor','native','src/framework/PerformancePreset.cpp','{"r_shadows",preset.shadows}','{"r_shadows",1}'),
    ('phone-keeps-shadows','native','src/framework/PerformancePreset.cpp','tier=signals.mobile?LowPower:Low;','tier=Low;'),
    ('top-tier-without-cpu','native','src/framework/PerformancePreset.cpp','if(signals.logicalCpus>=8&&signals.logicalCpus<=4096){','if(true){'),
    ('capacity-class','native','src/framework/PerformancePreset.cpp','else if(vram>=PerformancePresetVram12GiB)tier=Quality;','else if(vram>=PerformancePresetVram8GiB)tier=Quality;'),
    ('resolution-step','native','src/framework/PerformancePreset.cpp','ceiling=Balanced+capacity;','ceiling=Ultra;'),
    ('power-cap-ignored','native','src/framework/PerformancePreset.cpp','if(!powerLimited||signals.displayRefreshHz<result.maxFps)','if(true)'),
    ('phone-scene-budget','native','src/framework/PerformancePreset.cpp','if(ram<PerformancePresetMobileFlagshipRamMB)budget','if(false)budget'),
    ('fixed-specular','native','src/framework/PerformancePreset.cpp','{"image_downSizeSpecularLimit",64}','{"image_downSizeSpecularLimit",63}'),
    ('omit-edit','native','src/ui/application/SettingsTransaction.cpp','candidate[key]=value;','candidate[key]=declaration->second;'),
    ('merged-validation','native','src/ui/application/SettingsTransaction.cpp','if(!ValidateDraft(candidate,error))return false;','if(false)return false;'),
    ('reentry-publication','native','src/ui/application/SettingsTransaction.cpp','if(reentered)return Result(SettingsCode::Busy,"Generated settings edit was interrupted by reentry");','if(false)return Result(SettingsCode::Busy,"Generated settings edit was interrupted by reentry");'),
    ('missing-reentry-observation','native','src/ui/application/SettingsTransaction.cpp','if(generatedReentry)*generatedReentry=true;','if(false)*generatedReentry=true;'),
    ('wrong-owner','native','src/ui/application/SettingsTransaction.cpp','if (requestedOwner != owner) return Result','if (false) return Result'),
    ('host-extra-key','host',None,'candidate.size()!=openq4::PerformancePresetTargetCount','(candidate["r_brightness"]=1.0,candidate.size()!=openq4::PerformancePresetTargetCount+1)'),
    ('host-detection-ignored','host',None,'return BuildPerformancePresetPatch(resolved,patch,error);','return BuildPreset(detection.preset->name,patch,error);'),
    ('scene-budget-ignored','native','src/framework/PerformancePreset.cpp','if(pixels*scale*scale<=budget*10000||scale==10)','if(true)'),
    ('unsupported-aa-enabled','native','src/framework/PerformancePreset.cpp','(signals.postAAAvailable||signals.modernEffects)?result.preset->postAA:0','result.preset->postAA'),
    ('common-detection-ignored','common','src/framework/PerformancePreset.cpp','output=resolved;return true;','output=*detection.preset;return true;'),
    ('marker-on-failure','common',None,'if ( applied ) {','if ( true ) {'),
    ('skip-rollback','common',None,'Common_RestorePerformancePresetCVars( backups );','/* omitted restoration */'),
    ('skip-flags','common',None,'Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );','/* omitted modified flags */'),
    ('optional-post-demotion','common',None,'(!classicPrograms || config.maxTextureSize<2048)','(!config.GLSL130Available || !config.backendCaps.hasFBO)'),
    ('fabricated-ram','common',None,'signals.systemRamMB=SDL_GetSystemRAM();','signals.systemRamMB=Sys_GetSystemRam();'),
    ('pre-device-fabricated-ram','common',None,'signals.systemRamMB = SDL_GetSystemRAM();','signals.systemRamMB = Sys_GetSystemRam();'),
    ('preset-draft-guard','barrier',None,'operation == "settings.system.preset"','false'),
    ('auto-draft-guard','barrier',None,'operation == "settings.system.autodetect"','false'),
   ]
   if args.debug_stl:
    mutations.append(('publish-on-failed-return','native','src/ui/application/SettingsTransaction.cpp','if(std::uncaught_exceptions()!=exceptions)return;','/* publish incorrectly while unwinding return storage */'))
   result['mutations']=[]
   for label,program,dep,old,new in mutations:
    code,deps=units[program];original=source(dep) if dep else code
    if old not in original:raise RuntimeError('missing mutation '+label)
    # The memory boundary occurs in both ARM and desktop branches; alter both.
    if original.count(old)!=1 and label!='detection-boundary':raise RuntimeError('ambiguous mutation '+label)
    changed=original.replace(old,new);changed_deps=list(deps)
    cpp=out/('mutant-'+label+'.cpp')
    if dep:
     edited=out/('mutant-'+label+'-production.cpp');edited.write_text(changed,encoding='utf-8',newline='\n')
     # Relative includes in copied implementation keep original directory scope.
     includes=['-I',str((ROOT/dep).parent)];changed_deps=[str(edited) if p==dep else str(ROOT/p) for p in deps]
    else:code=changed;includes=[];changed_deps=[str(ROOT/p) for p in deps]
    cpp.write_text(code,encoding='utf-8',newline='\n');binary=out/('mutant-'+label+('.exe' if os.name=='nt' else ''))
    command=compile_command(cpp,changed_deps,binary,includes)
    build=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env);(out/('mutant-'+label+'-compile.log')).write_text(build.stdout)
    if build.returncode:raise RuntimeError('mutant did not compile '+label+': '+build.stdout[-3000:])
    run=subprocess.run([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env);(out/('mutant-'+label+'-run.log')).write_text(run.stdout)
    result['mutations'].append({'name':label,'exit':run.returncode,'rejected':run.returncode!=0,'output':run.stdout})
    if run.returncode==0:raise RuntimeError('mutant survived '+label)
    print('rejected '+label,flush=True)
  if args.adjacent:
   result['adjacent']=[]
   for name in ['ui_system_settings_host.py','ui_settings_service.py','ui_settings_decimal_precision.py','ui_system_display.py','ui_settings_display_service.py']:
    import sys
    command=[sys.executable,str(ROOT/'tools/tests'/name)]
    if name=='ui_settings_decimal_precision.py':command+=['--compiler',compiler]+(['--sanitize'] if args.sanitize else [])
    run=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env,cwd=ROOT)
    (out/(name+'.log')).write_text(run.stdout);result['adjacent'].append({'runner':name,'exit':run.returncode,'command':command})
    if run.returncode:raise RuntimeError('adjacent failed '+name+': '+run.stdout[-3000:])
    print('passed '+name,flush=True)
  result['passed']=True
 finally:
  result['files']=initial_hashes
  result['inputs_unchanged']=all((ROOT/p).is_file() and digest(ROOT/p)==sha for p,sha in initial_hashes.items())
  if not result['inputs_unchanged']:result['passed']=False
  (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True)
 if not result['passed']:
  raise RuntimeError('performance-preset validation inputs changed during the run')
if __name__=='__main__':main()
