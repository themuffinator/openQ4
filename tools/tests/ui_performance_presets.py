#!/usr/bin/env python3
"""Compile shared profile/transaction and unchanged Common/host/adapter bodies.
No engine, device, settings I/O or native input is used. Counted host boundaries
qualify draft publication and legacy calls, not actual hardware application.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile
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
 Check(keys.size()==32&&original.size()==53,"catalog union");
 for(const auto& preset:openq4::PerformancePresets()){
  SettingsTransaction tx(host);Check(tx.Begin(7).code==SettingsCode::Ok,"begin real host");
  Check(tx.Edit(7,{{"r_brightness",1.375},{"r_forceAmbient",0.1375}}).code==SettingsCode::Ok,"custom outside draft");
  const auto previous=tx.Draft();
  StateValues patch;Check(host.BuildPreset(preset.name,patch,error)&&patch.size()==32,"real typed expansion");
  Check(tx.EditGenerated(7,[&](StateValues& out,std::string& why){return host.BuildPreset(preset.name,out,why);}).code==SettingsCode::Ok,"whole preset atomically drafted");
  int untouched=0;for(const auto& [key,value]:previous){
   Check(SettingsValueEqual(tx.Draft().at(key),keys.contains(key)?patch.at(key):value),"32 replacements and exact outside preservation");
   if(!keys.contains(key))++untouched;
  }
  Check(untouched==21&&SettingsValuesEqual(tx.Baseline(),original),"21 outside keys and baseline preserved");
  Check(writes==0,"draft preset never invokes setter");
  StateValues readback;Check(host.Read(readback,error)&&SettingsValuesEqual(original,readback),"all 53 live unchanged");
  Check((SystemSettingsHost::ChangedEffects(tx.Baseline(),tx.Draft())&(SystemSettingPresetExpansion|SystemSettingImageReload|SystemSettingAudioRestart|SystemSettingRendererResources))!=0,"profile remains real unsupported effect footprint");
 }
 SettingsTransaction tx(host);Check(tx.Begin(1).code==SettingsCode::Ok,"detection owner");
 presetSignals.systemRamMB=16384;presetSignals.videoRamMB=6144;
 Check(tx.EditGenerated(1,[&](StateValues& out,std::string& why){return host.BuildDetectedPreset(out,why);}).code==SettingsCode::Ok,"read-only auto detection");
 Check(tx.Draft().at("com_performancePreset")==StateValue(std::string("quality"))&&presetSignalReads==1&&writes==0,"auto quality without setters");
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
#include <string>
#include <vector>
#include "src/framework/PerformancePreset.h"
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
};
struct Cvars{
 std::map<std::string,idCVar> vars;std::vector<std::string> calls;int modified=64;std::string refuse;
 idCVar* Find(const char* name){auto i=vars.find(name);return i==vars.end()?nullptr:&i->second;}
 void SetCVarString(const char* key,const char* value,int flags){calls.push_back(key);if(refuse==key)return;auto& v=vars.at(key);v.value=value;v.flags|=flags;modified|=flags;}
 void SetCVarInteger(const char* key,int value,int flags){if(std::string(key)=="image_anisotropy"&&value>16)value=16;SetCVarString(key,std::to_string(value).c_str(),flags);}
 int GetModifiedFlags()const{return modified;}void ClearModifiedFlags(int f){modified&=~f;}void SetModifiedFlags(int f){modified|=f;}
} cvars,*cvarSystem=&cvars;
struct Common{int warnings=0,prints=0;void Warning(const char*,...){++warnings;}void Printf(const char*,...){++prints;}} instance,*common=&instance;
static idCVar com_platformProfile,com_performancePreset;
static openq4::PerformancePresetSignals raw;
static std::vector<std::string> observations;
static bool Common_HasExplicitLowPowerHostSignal(){observations.push_back("explicit");return raw.explicitLowPower;}
static bool Common_HasRaspberryPiHostSignal(){observations.push_back("pi");return raw.raspberryPi;}
static bool Common_HasSteamDeckHostSignal(){observations.push_back("deck");return raw.steamDeck;}
static bool Common_HostCpuIsArm64(){observations.push_back("arm");return raw.arm64;}
static int Sys_GetSystemRam(){observations.push_back("ram");return raw.systemRamMB;}
static int Sys_GetVideoRam(){observations.push_back("vram");return raw.videoRamMB;}
struct Renderer{void GetCardCaps(bool& old,bool& nv){observations.push_back("caps");old=raw.legacyRenderer;nv=false;}} renderer,*renderSystem=&renderer;
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
  Check(cvars.calls.size()==32&&instance.warnings==0&&instance.prints==1,"exact successful call count");
  for(size_t i=0;i<32;++i){Check(cvars.calls[i]==expected[i].key,"old setter ordering and marker last");
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
  Check(cvars.calls.size()==(marker?64:63)&&instance.warnings==1&&cvars.modified==64,"rollback and global flags");
  for(const auto& [key,value]:cvars.vars)Check(value.value=="sentinel"&&value.flags==16,"all target values and flags restored");
  if(!marker)Check(cvars.calls[31]=="com_performancePreset","first marker setter belongs only to rollback");
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
 for(int flags=0;flags<32;++flags){
  Seed();raw={};raw.explicitLowPower=flags&1;raw.raspberryPi=flags&2;raw.steamDeck=flags&4;raw.legacyRenderer=flags&8;raw.arm64=flags&16;raw.systemRamMB=16384;raw.videoRamMB=6144;
  observations.clear();com_platformProfile.value="desktop";openq4::PerformancePresetSignals got;std::string error;
  Check(Common_CapturePerformancePresetSignals(got,error),"actual readonly observation");
  std::vector<std::string> expected={"explicit"};
  if(!raw.explicitLowPower){expected.push_back("pi");if(!raw.raspberryPi){expected.push_back("deck");if(!raw.steamDeck){expected.insert(expected.end(),{"ram","vram","caps"});if(!raw.legacyRenderer)expected.push_back("arm");}}}
  Check(observations==expected&&cvars.calls.empty()&&cvars.modified==64,"old observation order and zero writes");
  Check(openq4::DetectPerformancePreset(got).preset==openq4::DetectPerformancePreset(raw).preset,"actual capture uses pure branch selection");
 }
 raw={};observations.clear();com_platformProfile.value="steamdeck";openq4::PerformancePresetSignals got;std::string error;
 Check(Common_CapturePerformancePresetSignals(got,error)&&got.steamDeck&&observations==std::vector<std::string>({"explicit","pi"}),"archived Deck classification short circuits host query");
 com_platformProfile.value="desktop";renderSystem=nullptr;got.systemRamMB=123;
 Check(!Common_CapturePerformancePresetSignals(got,error)&&got.systemRamMB==123,"failed observation output unchanged");
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
 return {
 'native':('#include <cmath>\n#include "src/ui/retained/Document.h"\n'+valid+'\n'+source('tools/tests/native/UiPerformancePresetTest.cpp'),['src/framework/PerformancePreset.cpp','src/ui/application/SettingsTransaction.cpp']),
 'host':(host_test.SUPPORT+'\n#include <set>\n'+function_body(source('src/framework/CVarSystem.cpp'),'\nbool CVar_ReadDefault(')+valid+no_includes(source('src/ui/application/SystemSettingsHost.cpp'))+HOST_MAIN,['src/framework/PerformancePreset.cpp','src/ui/retained/Presentation.cpp','src/ui/application/SettingsTransaction.cpp']),
 'common':(COMMON_SUPPORT+common[start:end]+COMMON_MAIN,['src/framework/PerformancePreset.cpp']),
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
 bound_paths=list((ROOT/'src').rglob('*.h'))+list((ROOT/'src').rglob('*.cpp'))
 bound_paths+=[Path(__file__),ROOT/'tools/tests/ui_system_settings_host.py',ROOT/'tools/tests/filesystem_case_segments.py',ROOT/'tools/tests/native/UiPerformancePresetTest.cpp']
 bound_paths+=list((ROOT/'tools/tests').glob('ui_settings*.py'))+[ROOT/'tools/tests/ui_system_display.py',ROOT/'tools/tests/native/UiSettingsTransactionTest.cpp',ROOT/'tools/tests/native/UiSettingsDisplayControllerTest.cpp',ROOT/'tools/tests/native/UiSettingsExactValueTest.cpp']
 initial_hashes={p.relative_to(ROOT).as_posix():digest(p) for p in bound_paths}
 units=programs()
 if args.adjacent:
  validation=units['native'][0].split('// Copyright (C) 2026',1)[0]
  for name,extra in [('UiSettingsTransactionTest',[]),('UiSettingsDisplayControllerTest',['src/ui/application/SettingsDisplayController.cpp']),('UiSettingsExactValueTest',[])]:
   units[name]=(validation+source('tools/tests/native/'+name+'.cpp'),['src/ui/application/SettingsTransaction.cpp']+extra)
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
    ('detection-boundary','native','src/framework/PerformancePreset.cpp','result.systemRamMB<=4096||result.videoRamMB<=1024','result.systemRamMB<4096||result.videoRamMB<1024'),
    ('fixed-specular','native','src/framework/PerformancePreset.cpp','{"image_downSizeSpecularLimit",64}','{"image_downSizeSpecularLimit",63}'),
    ('omit-edit','native','src/ui/application/SettingsTransaction.cpp','candidate[key]=value;','candidate[key]=declaration->second;'),
    ('merged-validation','native','src/ui/application/SettingsTransaction.cpp','if(!Validate(candidate,error))return false;','if(false)return false;'),
    ('reentry-publication','native','src/ui/application/SettingsTransaction.cpp','if(reentered)return Result(SettingsCode::Busy,"Generated settings edit was interrupted by reentry");','if(false)return Result(SettingsCode::Busy,"Generated settings edit was interrupted by reentry");'),
    ('missing-reentry-observation','native','src/ui/application/SettingsTransaction.cpp','if(generatedReentry)*generatedReentry=true;','if(false)*generatedReentry=true;'),
    ('wrong-owner','native','src/ui/application/SettingsTransaction.cpp','if (requestedOwner != owner) return Result','if (false) return Result'),
    ('host-extra-key','host',None,'candidate.size()!=openq4::PerformancePresetTargetCount','(candidate["r_brightness"]=1.0,candidate.size()!=openq4::PerformancePresetTargetCount+1)'),
    ('host-detection-ignored','host',None,'return BuildPreset(detection.preset->name,patch,error);','return BuildPreset("minimum",patch,error);'),
    ('marker-on-failure','common',None,'if ( applied ) {','if ( true ) {'),
    ('skip-rollback','common',None,'Common_RestorePerformancePresetCVars( backups );','/* omitted restoration */'),
    ('skip-flags','common',None,'Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags );','/* omitted modified flags */'),
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
if __name__=='__main__':main()
