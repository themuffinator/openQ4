#!/usr/bin/env python3
"""Exercise production engine display-service methods with counted platform I/O.

Uses the real 76-key SYSTEM host, display request/recovery helpers, journal codec
and strict JSON/value parser. Native persistence, renderer and placement lease
are counted doubles: this tests ordering/ownership, not devices or power loss.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body
from wrap_sources import wrap_source
import ui_system_settings_host as host_test
import ui_system_display as display_test

ROOT = Path(__file__).resolve().parents[2]

BOUNDARIES = r'''
#include <cstring>
#include <cstdarg>
#include "tools/tests/native/FloatFlushMode.h"
#include "src/ui/SettingsDisplayService.h"
#include "src/framework/SettingsPersistence.h"
#include "src/sound/SoundRestart.h"
using namespace openq4;
static int checks=0;
static void Check(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
static unsigned long long displayGeneration=1;
unsigned long long Sys_DisplayTopologyGeneration(){return displayGeneration;}
struct idLangDict {
    std::map<std::string,std::string> strings;
    const char* GetString(const char* key) const {const auto found=strings.find(key);return found==strings.end()?key:found->second.c_str();}
} languageDict;
struct Common { void Warning(const char*,...){trace.push_back("warning");}
    const idLangDict* GetLanguageDict(){return &languageDict;}
    void Printf(const char* format,...){
        char text[512]{};va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
        std::string line(text);if(!line.empty()&&line.back()=='\n')line.pop_back();trace.push_back("print:"+line);
    } } commonObject,*common=&commonObject;
static bool Traced(const std::string& line){return std::find(trace.begin(),trace.end(),"print:"+line)!=trace.end();}
static std::map<std::string,std::string> files;
static std::set<std::string> leases;
static const std::string journalFile="E:/qualified/baseoq4/ui-settings-recovery.dat", lockFile="E:/qualified/baseoq4/.settings-recovery.lock";
static bool readFailure=false,replaceFailure=false,replacePublishes=false,removeFailure=false,configFailure=false;
static bool changedSaveRoot=false;
static int configWrites=0,restarts=0,initializations=0;
static std::string archived;
static rendererDisplayState_t actual;
static sysWindowPlacementSnapshot_t geometry;
static std::uint64_t geometryToken=0;
static bool geometryFailure=false,geometryPartial=false,queryOkay=true,restartOkay=true;
static sysWindowPlacementSnapshot_t CurrentGeometry() {
    auto p=geometry;p.width=cvarSystem->GetCVarInteger("r_windowWidth");p.height=cvarSystem->GetCVarInteger("r_windowHeight");return p;
}
static bool Same(const sysWindowPlacementSnapshot_t& a,const sysWindowPlacementSnapshot_t& b) {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height && a.normalX==b.normalX && a.normalY==b.normalY &&
        a.normalWidth==b.normalWidth && a.normalHeight==b.normalHeight && a.normalValid==b.normalValid;
}
static bool GeometryError(char* error,int size,const char* text) {std::snprintf(error,size,"%s",text);return false;}
namespace openq4 {
struct DurableFileLease::Impl {std::string path;};
DurableFileLease::DurableFileLease()=default;
DurableFileLease::~DurableFileLease(){Release();}
bool DurableFileLease::IsHeld() const{return bool(impl);}
bool DurableFileLease::TryAcquire(const std::string& path,std::string& error){
    trace.push_back("lock");if(impl || leases.contains(path)){error="busy lease";return false;}
    leases.insert(path);impl=std::make_unique<Impl>();impl->path=path;error.clear();return true;
}
void DurableFileLease::Release(){if(impl){trace.push_back("unlock");leases.erase(impl->path);impl.reset();}}
DurableReadResult DurableReadExact(const std::string& path,std::size_t bound,std::string& bytes,std::string& error){
    trace.push_back("read-journal");if(readFailure){error="read failure";return DurableReadResult::Failed;}
    const auto found=files.find(path);if(found==files.end()){error.clear();return DurableReadResult::Missing;}
    if(found->second.size()>bound){error="read budget";return DurableReadResult::Failed;}
    bytes=found->second;error.clear();return DurableReadResult::Present;
}
bool DurableReplaceExact(const std::string& path,const std::string& bytes,std::string& error){
    trace.push_back("replace-journal");Check(leases.contains(lockFile),"journal replace requires process lease");
    if(!replaceFailure || replacePublishes)files[path]=bytes;
    if(replaceFailure){error="uncertain replacement";return false;}error.clear();return true;
}
bool DurableRemoveExact(const std::string& path,std::string& error){
    trace.push_back("remove-journal");Check(leases.contains(lockFile),"journal removal requires process lease");
    if(removeFailure){error="remove failure";return false;}files.erase(path);error.clear();return true;
}
}
bool Common_SettingsPersistencePaths(std::string& journal,std::string& lock,std::string& error){journal=changedSaveRoot?"E:/other/baseoq4/ui-settings-recovery.dat":journalFile;lock=changedSaveRoot?"E:/other/baseoq4/.settings-recovery.lock":lockFile;error.clear();return true;}
bool Common_WriteSettingsConfiguration(bool owns,std::string& error){
    trace.push_back("config");++configWrites;Check(owns && leases.contains(lockFile),"configuration commit keeps process lease");
    Check(files.contains(journalFile),"configuration commit retains authoritative journal");
    if(configFailure){error="uncertain config commit";return false;}
    archived=files.at(journalFile);error.clear();return true;
}
static bool Sys_GetSecureRandomBytes(void* output,int bytes){std::memset(output,0xab,bytes);return true;}
bool Sys_BeginWindowPlacementLease(std::uint64_t token,sysWindowPlacementSnapshot_t* out,char* error,int size){
    trace.push_back("geometry-begin");if(geometryToken || !token)return GeometryError(error,size,"geometry busy");
    geometryToken=token;*out=CurrentGeometry();return true;
}
bool Sys_ReadWindowPlacementLease(std::uint64_t token,sysWindowPlacementSnapshot_t* out,char* error,int size){
    if(token!=geometryToken || !token)return GeometryError(error,size,"geometry unowned");*out=CurrentGeometry();return true;
}
bool Sys_ApplyWindowPlacementLease(std::uint64_t token,const sysWindowPlacementSnapshot_t* expected,
                                 const sysWindowPlacementSnapshot_t* target,char* error,int size){
    trace.push_back("geometry-apply");
    if(token!=geometryToken || !token || !Same(CurrentGeometry(),*expected))return GeometryError(error,size,"geometry conflict");
    if(geometryPartial){geometryPartial=false;geometry.x=target->x;return GeometryError(error,size,"partial geometry write");}
    if(geometryFailure)return GeometryError(error,size,"geometry refused");
    geometry=*target;
    localCVarSystem.variables.at("r_windowWidth").value=std::to_string(target->width);
    localCVarSystem.variables.at("r_windowHeight").value=std::to_string(target->height);
    return true;
}
bool Sys_FinishWindowPlacementLease(std::uint64_t token,const sysWindowPlacementSnapshot_t* expected,
                                  const sysWindowPlacementSnapshot_t* target,char* error,int size){
    trace.push_back("geometry-end");if(!Sys_ApplyWindowPlacementLease(token,expected,target,error,size))return false;geometryToken=0;return true;
}
bool Sys_BuildWindowPlacementCommit(std::uint64_t token,const sysWindowPlacementSnapshot_t* expected,
                                   const renderWindowState_s* state,sysWindowPlacementSnapshot_t* out,char* error,int size){
    if(token!=geometryToken || !Same(CurrentGeometry(),*expected))return GeometryError(error,size,"geometry commit conflict");
    *out=*expected;
    if(!state->hidden && !state->maximized && !state->fullscreen && !state->borderless){
        out->x=state->windowX;out->y=state->windowY;out->width=state->logicalWidth;out->height=state->logicalHeight;
        out->normalX=out->x;out->normalY=out->y;out->normalWidth=out->width;out->normalHeight=out->height;out->normalValid=true;
    }return true;
}
bool Sys_WindowPlacementLeaseActive(){return geometryToken!=0;}
bool R_RendererModule_QueryDisplay(rendererDisplayState_t* out){if(!queryOkay)return false;*out=actual;return true;}
bool R_RendererModule_QueryLightGridLoad(renderLightGridLoadReceipt_t&,uint64_t&,uint64_t&){return false;}
static renderRendererSelection_t selectionReport{};static uint64_t selectionReportSerial=0;static bool selectionReportValid=true;
// 0 agrees; 1 ignores the request; 2 publishes no new report; 3 reports an unavailable
// request with the automatic pick; 4 claims promotion for an explicit name.
static int selectionFault=0;
static void PublishSelection(){
    if(selectionFault==2)return;
    renderRendererSelection_t s{};const std::string request=localCVarSystem.variables.at("r_renderer").value;
    std::snprintf(s.requested,sizeof(s.requested),"%s",selectionFault==1?"best":request.c_str());
    s.selected=0;s.automatic=0;s.fallback=selectionFault==3?RENDER_SELECTION_UNAVAILABLE:RENDER_SELECTION_REQUESTED;
    s.promotionActive=selectionFault==4;selectionReport=s;++selectionReportSerial;
}
bool R_RendererModule_QueryRendererSelection(renderRendererSelection_t& s,uint64_t& serial,uint64_t& epoch){
    if(!selectionReportValid)return false;s=selectionReport;serial=selectionReportSerial;epoch=actual.moduleEpoch;return true;
}
static rendererModuleStatus_t moduleStatus{};
const rendererModuleStatus_t& R_RendererModule_GetStatus(){return moduleStatus;}
static void ApplyActual(const renderWindowRequest_t& r){
    ++actual.presentation.generation;actual.presentation.samples=r.parms.multiSamples;actual.presentation.swapInterval=r.swapInterval;
    auto& w=actual.window;w.displayId=r.displayId;w.displayIndex=r.displayIndex;
    w.fullscreen=r.parms.fullScreen;w.fullscreenDesktop=r.fullscreenDesktop;w.borderless=r.parms.borderless;
    w.hidden=r.parms.hiddenWindow;w.maximized=r.maximized;
    w.logicalWidth=w.pixelWidth=r.parms.width;w.logicalHeight=w.pixelHeight=r.parms.height;
    if(r.restorePlacement){w.windowX=r.windowX;w.windowY=r.windowY;}
}
static renderWindowRequest_t lastRequest{};
// Vulkan's strict device initialization refuses window samples (vk_Backend.cpp).
static bool VulkanRefuses(const renderWindowRequest_t* r,char* error,int size){
    lastRequest=*r;return moduleStatus.activeApi==RENDER_MODULE_API_VULKAN && r->parms.multiSamples && !GeometryError(error,size,"Vulkan: strict framebuffer request is unsupported");
}
bool R_RendererModule_TryDeviceRestart(const renderWindowRequest_t* r,char* error,int size){
    trace.push_back("restart");++restarts;if(!restartOkay)return GeometryError(error,size,"restart failed");if(VulkanRefuses(r,error,size))return false;ApplyActual(*r);PublishSelection();return true;
}
// The sound device: a restart request leaves it restarting until a case reopens or fails it.
static soundOutputState_t soundState=SOUND_OUTPUT_READY;static int soundRestarts=0;
soundOutputState_t Sound_OutputState(){return soundState;}
bool Sound_RequestOutputRestart(){if(soundState!=SOUND_OUTPUT_READY)return false;trace.push_back("sound-restart");++soundRestarts;soundState=SOUND_OUTPUT_RESTARTING;return true;}
bool R_RendererModule_TryInitializeDisplay(const renderWindowRequest_t* r,char* error,int size){
    trace.push_back("initialize");++initializations;if(!restartOkay)return GeometryError(error,size,"initialize failed");if(VulkanRefuses(r,error,size))return false;ApplyActual(*r);PublishSelection();return true;
}
'''

CASES = r'''
static std::string error;
static StateValues Live(SystemSettingsHost& host){StateValues values;Check(host.Read(values,error),"read live catalog");return values;}
static void ResetFixture(){
    files.clear();leases.clear();geometryToken=0;readFailure=replaceFailure=replacePublishes=removeFailure=configFailure=changedSaveRoot=traceEnabled=false;
    geometryFailure=geometryPartial=false;queryOkay=restartOkay=true;displayOrder={1,2};displayCount=2;primaryDisplay=1;currentDisplay=2;configWrites=restarts=initializations=0;archived.clear();
    Seed();localCVarSystem.variables.at("r_swapInterval").value="1";actual=Actual();actual.window.hidden=false;actual.window.focused=true;
    geometry={actual.window.windowX,actual.window.windowY,1280,720,actual.window.windowX,actual.window.windowY,1280,720,true};
    trace.clear();writes=0;moduleStatus.activeApi=RENDER_MODULE_API_GL;lastRequest={};
    selectionFault=0;selectionReportValid=true;selectionReport={};selectionReportSerial=0;PublishSelection();
    soundState=SOUND_OUTPUT_READY;soundRestarts=0;
}
static void MultisamplingCapability(){
    ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
    for(const auto api:{RENDER_MODULE_API_GL,RENDER_MODULE_API_GL_MODULE,RENDER_MODULE_API_GLES,RENDER_MODULE_API_VULKAN,RENDER_MODULE_API_COUNT}){
        moduleStatus.activeApi=api;
        const bool window=api==RENDER_MODULE_API_GL || api==RENDER_MODULE_API_GL_MODULE || api==RENDER_MODULE_API_GLES;
        Check(host.SupportsMultisampling()==(window || api==RENDER_MODULE_API_VULKAN),"every known ready renderer applies MSAA");
        Check(host.WindowMultisampling()==window,"only the OpenGL family multisamples its window");
    }
    moduleStatus.activeApi=RENDER_MODULE_API_GL;
    queryOkay=false;Check(!host.SupportsMultisampling(),"missing observation disables MSAA");queryOkay=true;
    actual.rendererReady=false;Check(!host.SupportsMultisampling(),"unready renderer disables MSAA");actual.rendererReady=true;
    actual.windowValid=false;Check(!host.SupportsMultisampling(),"missing window disables MSAA");actual.windowValid=true;
    actual.presentation.available=false;Check(!host.SupportsMultisampling(),"missing presentation disables MSAA");actual.presentation.available=true;
    Check(host.SupportsMultisampling(),"capability recovers with valid observation");
    Check(writes==0 && configWrites==0 && restarts==0 && initializations==0 && files.empty() && leases.empty() && geometryToken==0,"capability observation has no setting or device side effects");
}
static SettingsAttempt Attempt(SystemSettingsHost& settings,bool dimensions=true){
    SettingsAttempt a{17,71,Live(settings),{}, {}};a.target=a.baseline;
    a.target["r_multiSamples"]=4.0;a.target["r_swapInterval"]=0.0;
    if(dimensions){a.target["r_windowWidth"]=960.0;a.target["r_windowHeight"]=540.0;}
    for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
    Check(settings.Validate(a.baseline,a.target,error),"validate attempt fixture");return a;
}
static SettingsRecoveryJournal Journal(){SettingsRecoveryJournal j;Check(DecodeSettingsJournal(files.at(journalFile),SystemSettingsHost::Schema(),j,error),"decode real journal");return j;}
static void Store(const SettingsRecoveryJournal& j){std::string bytes;Check(EncodeSettingsJournal(j,SystemSettingsHost::Schema(),bytes,error),"encode altered valid journal fixture");files[journalFile]=bytes;}
static void Present(){++actual.presentation.submittedSequence;++actual.presentation.presentedSequence;}
static void RuntimeJournal(){
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);
     Check(host.Prepare(a,error),"prepare runtime journal");Check(writes==0 && configWrites==0 && leases.contains(lockFile) && geometryToken,"prepare writes no settings/config and owns both leases");
     const auto j=Journal();Check(j.state==SettingsJournalState::Pending && j.baseline==a.baseline && j.target==a.target,"journal has exact immutable pending snapshots");
     Check(settings.Write(a.patch,error),"apply catalog patch");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"apply frozen display request");Check(configWrites==0,"unconfirmed display never archives configuration");
     Present();Check(host.PersistConfirmation(a,error),"persist qualified confirmation");
     Check(Journal().state==SettingsJournalState::Confirmed && configWrites==1,"Confirmed marker precedes config commit");
     const auto config=std::find(trace.begin(),trace.end(),"config"),replace=std::find(trace.rbegin(),trace.rend(),"replace-journal").base()-1;
     Check(replace<config,"durable confirmation record precedes configuration write");
     Check(host.Finish(false,error) && !host.RecoveryActive() && !files.contains(journalFile) && !geometryToken && leases.empty(),"successful Keep retires exact journal then leases");}
    for(bool published:{false,true}){ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);
     replaceFailure=true;replacePublishes=published;Check(!host.Prepare(a,error) && host.RecoveryActive() && writes==0,"uncertain initial journal never writes settings");
     replaceFailure=false;Check(host.CancelPreparation(error) && !host.RecoveryActive() && !files.contains(journalFile),"cancel resolves both pre/post-publication preparation failure");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);files[journalFile]="foreign malformed evidence";
     Check(!host.Prepare(a,error) && writes==0,"existing foreign journal blocks apply before writes");
     Check(host.CancelPreparation(error) && files.at(journalFile)=="foreign malformed evidence" && host.RecoveryActive(),"cancelling unwritten preparation preserves foreign evidence and recovery block");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);Check(host.Prepare(a,error),"prepare tamper fixture");
     files[journalFile]="foreign replacement";Check(!host.CancelPreparation(error) && files.at(journalFile)=="foreign replacement" && host.RecoveryActive(),"owned journal tamper cannot be deleted");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);Check(host.Prepare(a,error),"prepare uncertain Keep");
     Check(settings.Write(a.patch,error),"write uncertain Keep values");SettingsDisplayObservation seen;Check(host.Restart(false,seen,error),"restart uncertain Keep");Present();
     configFailure=true;Check(!host.PersistConfirmation(a,error) && Journal().state==SettingsJournalState::Confirmed && host.RecoveryActive(),"failed config commit retains Confirmed evidence and leases");
     configFailure=false;Check(host.PersistConfirmation(a,error),"retry same uncertain Keep record");removeFailure=true;
     Check(!host.Finish(false,error) && host.RecoveryActive() && files.contains(journalFile),"failed journal cleanup retains ownership");removeFailure=false;
     Check(host.Finish(false,error),"explicit cleanup retry completes");}
}
static SettingsAttempt PendingForStartup(SystemSettingsHost& settings,bool confirmed=false,bool dimensions=true,bool moveMonitor=false){
    EngineSettingsDisplayHost preparing(settings);auto a=Attempt(settings,dimensions);
    if(moveMonitor){a.target["r_screen"]=0.0;a.patch["r_screen"]=0.0;}
    Check(preparing.Prepare(a,error),"prepare startup journal fixture");
    if(confirmed){Check(settings.Write(a.patch,error),"write confirmed startup fixture");SettingsDisplayObservation observed;
        Check(preparing.Restart(false,observed,error),"restart confirmed startup fixture");Present();Check(preparing.PersistConfirmation(a,error),"persist confirmed startup fixture");}
    preparing.Shutdown();Check(files.contains(journalFile) && leases.empty() && !geometryToken,"shutdown retains durable evidence and releases process-local leases");
    configWrites=0;writes=0;trace.clear();return a;
}
static void StartupCases(){
    // A pre-preferences schema-1 record owns only its original 53 keys.
    // Replay the real service, including byte ownership and final persistence.
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=PendingForStartup(settings,confirmed);
      auto legacy=Journal();auto schema=SystemSettingsHost::Schema();
      for(auto it=schema.begin();it!=schema.end();) {const auto key=it->first;if(!SettingsEffectCatalogV1().contains(key)){it=schema.erase(it);legacy.baseline.erase(key);legacy.target.erase(key);}else ++it;}
      std::string bytes;Check(schema.size()==53 && EncodeSettingsJournal(legacy,schema,bytes,error),"encode authentic old catalog journal");files[journalFile]=bytes;
      for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
      localCVarSystem.variables.at("ui_retainedScale").value="1.75";localCVarSystem.variables.at("ui_retainedTextScale").value="1.5";
      EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"old catalog recovers through production startup");
      auto current=Live(settings);Check(current.at("ui_retainedScale")==StateValue(1.75) && current.at("ui_retainedTextScale")==StateValue(1.5),"old recovery never claims new size preferences");
      Check(current.at("r_multiSamples")==(confirmed?a.target:a.baseline).at("r_multiSamples"),"old recovery chooses correct side");
      Check(files.at(journalFile)==bytes && configWrites==0,"original bytes remain authoritative before qualification");
      Check(host.InitializeDisplay(error),"old record initializes recorded display");Present();host.StartupFrame(1,true);
      Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile),"qualified old recovery persists then retires original bytes");}
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=PendingForStartup(settings,confirmed);
      // Simulate config loading either original or transaction-written values;
      // unrelated external archive changes survive both directions.
      for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
      localCVarSystem.variables.at("r_brightness").value="1.7";
      EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays journal-owned patch");
      auto current=Live(settings);Check(current.at("r_brightness")==StateValue(1.7),"startup preserves unrelated live catalog changes");
      Check(current.at("r_multiSamples")== (confirmed?a.target:a.baseline).at("r_multiSamples"),"startup chooses committed direction");
      Check(configWrites==0 && host.StartupActive(),"startup CVar replay cannot archive before actual display qualification");
      Check(host.InitializeDisplay(error),"initialize exact recorded display");host.StartupFrame(1,true);Check(configWrites==0,"startup waits for a new presented frame");
      Present();host.StartupFrame(2,false);Check(configWrites==0,"nested startup frame cannot persist");host.StartupFrame(2,true);
      Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile),"qualified startup commits recovered live frame then retires journal");}
    {ResetFixture();SystemSettingsHost settings;auto a=PendingForStartup(settings);localCVarSystem.variables.at("r_multiSamples").value="8";
     EngineSettingsDisplayHost host(settings);Check(!host.Startup(error) && writes==0 && files.contains(journalFile),"divergent owned startup key blocks before all writes");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);auto j=Journal();j.placement["baseline.width"]=1366.0;Store(j);
     localCVarSystem.variables.at("r_multiSamples").value="4";EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0,"cross-payload placement/catalog mismatch is rejected before CVar replay");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings,true);auto j=Journal();j.displayTarget["samples"]=8.0;Store(j);
     localCVarSystem.variables.at("r_multiSamples").value="0";EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0,"target display metadata cannot contradict target catalog");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);auto j=Journal();
     j.target["r_brightness"]=99.0;j.patch["r_brightness"]=99.0;Store(j);
     EngineSettingsDisplayHost host(settings);Check(!host.Startup(error) && writes==0 && geometryToken==0,
       "even unused Pending target catalog must reject invalid changed immediate value before replay");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);auto j=Journal();j.displayRestore["samples"]=2.0;Store(j);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"captured actual baseline samples may differ from archived baseline intent");
     Check(host.InitializeDisplay(error) && actual.presentation.samples==2,"startup restores actual recorded device independently of old CVar intent");host.Shutdown();}
}

// Vulkan multisamples its scene targets, not its single-sample window: the
// display request asks for no window samples whatever r_multiSamples holds,
// and a saved Vulkan record recovers.
static void VulkanMultisampling(){
    for(bool dimensions:{false,true}){ResetFixture();moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     auto a=Attempt(settings,dimensions);Check(host.Prepare(a,error),"prepare a Vulkan MSAA change");
     Check(Journal().displayTarget.at("samples")==StateValue(0.0) && a.target.at("r_multiSamples")==StateValue(4.0),"the Vulkan target window is single-sample");
     Check(settings.Write(a.patch,error),"write the Vulkan MSAA change");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && lastRequest.parms.multiSamples==0 && actual.presentation.samples==0,"the Vulkan restart asks for no window samples");
     Present();Check(host.PersistConfirmation(a,error) && configWrites==1,"the Vulkan MSAA change keeps");
     Check(host.Finish(false,error) && !files.contains(journalFile) && Live(settings).at("r_multiSamples")==StateValue(4.0),"the kept Vulkan change leaves r_multiSamples at the request");}
    {ResetFixture();moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;localCVarSystem.variables.at("r_multiSamples").value="8";
     SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     SettingsAttempt a{17,71,Live(settings),{},{}};a.target=a.baseline;a.target["r_windowWidth"]=960.0;a.target["r_windowHeight"]=540.0;
     for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
     Check(settings.Validate(a.baseline,a.target,error) && host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a window change under 8x on Vulkan");
     SettingsDisplayObservation seen;Check(host.Restart(false,seen,error) && lastRequest.parms.multiSamples==0,"a window change under MSAA restarts Vulkan");
     Present();Check(host.PersistConfirmation(a,error) && host.Finish(false,error),"the window change keeps under MSAA on Vulkan");
     Check(Live(settings).at("r_multiSamples")==StateValue(8.0),"the change leaves MSAA as it was");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);
     Check(host.Prepare(a,error) && Journal().displayTarget.at("samples")==StateValue(4.0) && settings.Write(a.patch,error),"OpenGL records its window samples");
     SettingsDisplayObservation seen;Check(host.Restart(false,seen,error) && lastRequest.parms.multiSamples==4,"OpenGL asks for window samples");host.Shutdown();}
    for(bool confirmed:{false,true}){ResetFixture();moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;SystemSettingsHost settings;auto a=PendingForStartup(settings,confirmed);
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"a Vulkan record recovers at startup");
     Check(Live(settings).at("r_multiSamples")==(confirmed?a.target:a.baseline).at("r_multiSamples"),"Vulkan recovery chooses the recorded direction");
     Check(host.InitializeDisplay(error) && lastRequest.parms.multiSamples==0,"Vulkan recovery asks for no window samples");
     Present();host.StartupFrame(1,true);Check(configWrites==1 && !files.contains(journalFile),"Vulkan recovery commits and retires its record");}
    // A record written under OpenGL (window samples 4) recovers under Vulkan,
    // whose window is single-sample.
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=PendingForStartup(settings,confirmed);
     Check(Journal().displayTarget.at("samples")==StateValue(4.0),"the OpenGL record keeps its window samples");
     moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"an OpenGL record recovers under Vulkan");
     Check(host.InitializeDisplay(error) && lastRequest.parms.multiSamples==0,"the recovered Vulkan window is single-sample");
     Present();host.StartupFrame(1,true);Check(configWrites==1 && !files.contains(journalFile),"the OpenGL record commits and retires under Vulkan");}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings,true);auto j=Journal();j.displayTarget["samples"]=0.0;Store(j);
     localCVarSystem.variables.at("r_multiSamples").value="0";EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error),"a single-sample saved window agrees with any catalog count");host.Shutdown();}
}

static void GeometryAndStartupFailures(){
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings,false);
     Check(host.Prepare(a,error),"prepare unowned geometry case");Check(settings.Write(a.patch,error),"write owned display fields");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"restart unowned geometry case");
     localCVarSystem.variables.at("r_windowWidth").value="1366";
     Check(settings.Write({{"r_multiSamples",a.baseline.at("r_multiSamples")},{"r_swapInterval",a.baseline.at("r_swapInterval")}},error),"restore only owned fields");
     Check(host.Restart(true,seen,error) && host.Finish(true,error),"complete restore with unrelated external width");
     Check(cvarSystem->GetCVarInteger("r_windowWidth")==1366 && configWrites==0,"runtime Revert preserves unowned live dimensions and never archives candidate");}
    for(bool external:{false,true}){ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);
     a.target["r_screen"]=0.0;a.patch["r_screen"]=0.0;Check(host.Prepare(a,error),"prepare monitor placement move");
     Check(settings.Write(a.patch,error),"write placement move settings");SettingsDisplayObservation seen;Check(host.Restart(false,seen,error),"restart moved monitor");
     Present();geometryPartial=true;Check(!host.PersistConfirmation(a,error) && configWrites==0 && host.RecoveryActive(),"partial geometry setter retains confirmed journal before config");
     if(external){geometry.x=12345;Check(!host.PersistConfirmation(a,error) && geometry.x==12345 && configWrites==0,"retry cannot adopt divergent external geometry");host.Shutdown();}
     else{Check(host.PersistConfirmation(a,error) && configWrites==1,"partial owned geometry reconciles and retries exact Keep");Check(host.Finish(false,error),"finish retried placement commit");}}
    for(bool removal:{false,true}){ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error) && host.InitializeDisplay(error),"startup persistence failure fixture");Present();
     if(removal)removeFailure=true;else configFailure=true;
     host.StartupFrame(1,true);Check(host.RecoveryActive() && files.contains(journalFile) && configWrites==1 && !host.RecoveryError().empty(),"startup config/removal failure retains evidence and guard");
     configFailure=removeFailure=false;host.StartupFrame(2,true);Check(configWrites==1,"startup persistence failure does not retry blindly");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);geometry.x=12345;EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && geometry.x==12345,"external startup position conflicts before CVar replay");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;files[journalFile]="corrupt";EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.at(journalFile)=="corrupt","corrupt startup evidence remains authoritative and unwritten");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;leases.insert(lockFile);EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && !geometryToken,"foreign process lease blocks before geometry and settings");host.Shutdown();leases.clear();}
}
static void StartupClockCases(){
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error) && host.InitializeDisplay(error),"invalid startup clock fixture");Present();host.StartupFrame(std::numeric_limits<double>::quiet_NaN(),true);
     Check(configWrites==0 && host.RecoveryActive() && !host.RecoveryError().empty(),"invalid startup clock cannot qualify persistence");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error) && host.InitializeDisplay(error),"backward startup clock fixture");host.StartupFrame(2,false);Present();host.StartupFrame(1,true);
     Check(configWrites==0 && host.RecoveryActive() && !host.RecoveryError().empty(),"backward startup clock cannot qualify persistence");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error) && host.InitializeDisplay(error),"nested startup timeout fixture");host.StartupFrame(1,false);Present();host.StartupFrame(21,false);host.StartupFrame(22,true);
     Check(configWrites==0 && host.RecoveryActive() && !host.RecoveryError().empty(),"nested startup frames enforce deadline before delayed full-frame persistence");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings);EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error) && host.InitializeDisplay(error),"startup nonpresent submission fixture");++actual.presentation.submittedSequence;host.StartupFrame(1,true);
     Check(configWrites==0 && host.StartupActive(),"startup screenshot submission without present cannot archive recovery");
     ++actual.presentation.failureSequence;host.StartupFrame(2,false);Present();host.StartupFrame(3,true);
     Check(configWrites==0 && host.RecoveryActive(),"startup failed presentation blocks cleanup even in nested frame");host.Shutdown();}
}
static void CommitOwnershipCases(){
    for(bool startupCase:{false,true})for(bool pathChange:{false,true}){
     ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=Attempt(settings);
     if(startupCase){PendingForStartup(settings);Check(host.Startup(error) && host.InitializeDisplay(error),"startup commit ownership fixture");}
     else{Check(host.Prepare(a,error) && settings.Write(a.patch,error),"runtime commit ownership fixture");SettingsDisplayObservation seen;Check(host.Restart(false,seen,error),"restart commit ownership fixture");}
     Present();
     if(pathChange)changedSaveRoot=true;else files[journalFile]="foreign journal before commit";
     if(startupCase)host.StartupFrame(1,true);else Check(!host.PersistConfirmation(a,error),"runtime confirmation rejects changed commit authority");
     Check(configWrites==0 && host.RecoveryActive(),"changed save root or journal blocks all configuration writes");
     Check(files.contains(journalFile),"failed commit ownership check keeps recovery evidence");
     if(!pathChange)Check(files.at(journalFile)=="foreign journal before commit","commit cannot replace foreign journal");
     else Check(!files.contains("E:/other/baseoq4/ui-settings-recovery.dat"),"commit cannot write evidence in changed save root");
     host.Shutdown();
    }
}
static void ReindexedStartupRetry(){
    ResetFixture();SystemSettingsHost settings;PendingForStartup(settings,true,true,true);
    displayOrder={2,1};displayCount=2;
    {EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"reordered explicit monitor remaps by stable descriptor");
     Check(cvarSystem->GetCVarInteger("r_screen")==1,"recovery writes current descriptor index");
     Check(host.InitializeDisplay(error),"initialize remapped display");Present();removeFailure=true;host.StartupFrame(1,true);
     Check(configWrites==1 && files.contains(journalFile),"committed remap retains journal after removal failure");host.Shutdown();}
    removeFailure=false;
    {EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"second startup accepts its own previously committed index remapping");host.Shutdown();}
}
static void UnusedTopologyCases(){
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings,false,true,true);
     displayOrder={2};displayCount=1;primaryDisplay=currentDisplay=2;
     localCVarSystem.variables.at("r_multiSamples").value="4";EngineSettingsDisplayHost host(settings);
     Check(host.Startup(error),"Pending recovery only requires the actual restore monitor, not unused target topology");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;PendingForStartup(settings,true,true,true);
     displayOrder={1};displayCount=1;primaryDisplay=currentDisplay=1;
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"Confirmed recovery only needs approved target on current topology");host.Shutdown();}
}


// Full production catalog with an observed custom ambient subnormal. The host
// allows restoration of this original, but new edits still enforce float-cache
// representability; these cases request zero and never introduce a tiny edit.
struct ExactMode:openq4::test::FloatFlushMode{ExactMode():FloatFlushMode(true){}};
static void SetExactAmbient(std::uint64_t bits){
    std::string text;Check(SettingsNumberText(std::bit_cast<double>(bits),SettingsNumberFormat::FixedShortest,text),"serialize exact observed ambient fixture");
    localCVarSystem.variables.at("r_forceAmbient").value=text;
}
static bool AmbientIs(SystemSettingsHost& settings,std::uint64_t bits){
    return std::bit_cast<std::uint64_t>(std::get<double>(Live(settings).at("r_forceAmbient")))==bits;
}
static void ExactCatalogCases(){
    ExactMode mode;
    // Complete-catalog freeze catches even an untouched key's exact external
    // change before durable preparation or a renderer restart can be authorized.
    {ResetFixture();SystemSettingsHost settings;SetExactAmbient(1);auto a=Attempt(settings);
     SetExactAmbient(2);EngineSettingsDisplayHost host(settings);
     Check(!host.Prepare(a,error) && writes==0 && files.empty() && restarts==0 && geometryToken==0,
       "display preparation rejects bit-distinct complete-catalog baseline");host.Shutdown();}
    for(bool confirmed:{false,true})for(bool conflict:{false,true}){
     ResetFixture();SystemSettingsHost settings;SetExactAmbient(1);auto a=Attempt(settings);
     a.target["r_forceAmbient"]=0.0;a.patch["r_forceAmbient"]=0.0;
     Check(settings.Validate(a.baseline,a.target,error),"observed tiny to explicit zero is a valid new edit");
     {EngineSettingsDisplayHost preparing(settings);Check(preparing.Prepare(a,error),"prepare exact ambient/display journal");
      if(confirmed){Check(settings.Write(a.patch,error),"write exact zero target");SettingsDisplayObservation observation;
       Check(preparing.Restart(false,observation,error),"restart exact target");Present();Check(preparing.PersistConfirmation(a,error),"persist exact target");}
      preparing.Shutdown();}
     auto saved=Journal();Check(saved.patch.contains("r_forceAmbient") &&
       std::bit_cast<std::uint64_t>(std::get<double>(saved.baseline.at("r_forceAmbient")))==1,"journal keeps exact original and explicit zero patch");
     // Simulate either archive side after a crash, or a third-party different
     // subnormal which neither the original nor this transaction ever owned.
     SetExactAmbient(conflict?2:confirmed?1:0);writes=configWrites=0;trace.clear();
     EngineSettingsDisplayHost replay(settings);
     if(conflict){const auto bytes=files.at(journalFile);
      Check(!replay.Startup(error) && writes==0 && configWrites==0 && geometryToken==0,
       "startup rejects bit-distinct divergent owned field before all writes");
      Check(AmbientIs(settings,2) && files.at(journalFile)==bytes,"conflicting replay preserves live value and exact evidence");}
     else{
      Check(replay.Startup(error),"startup restores exact zero/subnormal direction");
      Check(AmbientIs(settings,confirmed?0:1),"startup emits required zero or original tiny patch");
      Check(writes>0 && configWrites==0,"startup changed owned value without premature archive");
      Check(replay.InitializeDisplay(error),"initialize exact recovered display");Present();replay.StartupFrame(1,true);
      Check(configWrites==1 && !files.contains(journalFile) && !replay.RecoveryActive(),"qualified exact recovery commits then removes journal");}
     replay.Shutdown();
    }
}

// The next-map light-grid preload: a schema-2 record with only the next-map
// domain, no restart or placement lease, and the same Pending/Confirmed order.
static SettingsAttempt DeferredAttempt(SystemSettingsHost& settings,bool rider=true){
    SettingsAttempt a{23,91,Live(settings),{},{}};a.completion=SettingsCompletion::Automatic;a.target=a.baseline;
    a.target["r_lightGridPreload"]=!std::get<bool>(a.baseline.at("r_lightGridPreload"));if(rider)a.target["r_brightness"]=1.25;
    for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
    Check(settings.Validate(a.baseline,a.target,error) && SystemSettingsHost::ApplyClassOf(a.baseline,a.target)==SystemApplyClass::Deferred,
          "validate deferred attempt fixture");
    return a;
}
static SettingsEffectRecoveryJournal EffectJournal(){
    SettingsJournalRecord record;
    Check(DecodeSettingsJournalRecord(files.at(journalFile),SystemSettingsHost::Schema(),record,error) && record.Schema()==2,"decode a schema-2 deferred journal");
    return std::get<SettingsEffectRecoveryJournal>(*record.Value());
}
static void StoreEffect(const SettingsEffectRecoveryJournal& j){std::string bytes;Check(EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,error),"encode altered deferred journal");files[journalFile]=bytes;}
static SettingsAttempt DeferredForStartup(SystemSettingsHost& settings,bool confirmed,bool rider=true){
    EngineSettingsDisplayHost preparing(settings);auto a=DeferredAttempt(settings,rider);
    Check(preparing.Prepare(a,error),"prepare deferred startup fixture");
    if(confirmed){Check(settings.Write(a.patch,error),"write confirmed deferred fixture");SettingsDisplayObservation observed;
        Check(preparing.Restart(false,observed,error),"observe confirmed deferred fixture");Present();Check(preparing.PersistConfirmation(a,error),"persist confirmed deferred fixture");}
    preparing.Shutdown();Check(files.contains(journalFile) && leases.empty() && !geometryToken,"shutdown keeps the deferred evidence and releases the lease");
    configWrites=0;writes=0;trace.clear();return a;
}
static void DeferredCases(){
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     Check(host.ReadyForAutomatic(),"a presenting renderer is ready for an automatic attempt");
     queryOkay=false;Check(!host.ReadyForAutomatic(),"no observation is not ready");queryOkay=true;
     actual.rendererReady=false;Check(!host.ReadyForAutomatic(),"an unready renderer is not ready");actual.rendererReady=true;
     actual.presentation.available=false;Check(!host.ReadyForAutomatic(),"no presentation is not ready");actual.presentation.available=true;
     auto deferred=DeferredAttempt(settings);deferred.completion=SettingsCompletion::UserConfirmation;
     auto display=Attempt(settings);display.completion=SettingsCompletion::Automatic;
     Check(!host.Prepare(deferred,error) && !host.Prepare(display,error) && leases.empty() && files.empty() && !geometryToken && writes==0,
           "a completion that disagrees with the effects is refused before any lease or journal");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=DeferredAttempt(settings);
     Check(host.Prepare(a,error),"prepare a deferred attempt");
     Check(writes==0 && configWrites==0 && leases.contains(lockFile) && !geometryToken && restarts==0,"deferred preparation journals under the process lease without geometry or devices");
     const auto j=EffectJournal();
     Check(j.state==SettingsJournalState::Pending && j.baseline==a.baseline && j.target==a.target && j.patch==a.patch && j.plan.domainMask==SystemSettingNextMap,
           "the Pending record carries exact snapshots and only the next-map domain");
     Check(j.deferredRestore.size()==1 && j.deferredRestore.at("lightGridPreload")==a.baseline.at("r_lightGridPreload") &&
           j.deferredTarget.size()==1 && j.deferredTarget.at("lightGridPreload")==a.target.at("r_lightGridPreload") &&
           j.displayRestore.empty() && j.displayTarget.empty() && j.placement.empty(),"the record holds the portable preload policy in each direction and no display state");
     Check(settings.Write(a.patch,error),"write the deferred patch");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && seen.ready && restarts==0 && initializations==0,"a deferred restart only observes the presenting device");
     Present();Check(host.Observe(false,seen,error) && seen.presented==actual.presentation.presentedSequence,"observation reports the later presented frame");
     Check(host.PersistConfirmation(a,error) && EffectJournal().state==SettingsJournalState::Confirmed && configWrites==1,"Confirmed precedes the configuration commit");
     const auto config=std::find(trace.begin(),trace.end(),"config"),replace=std::find(trace.rbegin(),trace.rend(),"replace-journal").base()-1;
     Check(replace<config,"the durable Confirmed record precedes the configuration write");
     Check(host.Finish(false,error) && !host.RecoveryActive() && !files.contains(journalFile) && leases.empty(),"Finish retires the deferred journal and its lease");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=DeferredAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare and write a deferred attempt to restore");
     SettingsDisplayObservation seen;Check(host.Restart(false,seen,error),"observe the deferred attempt");
     StateValues back;for(const auto& [key,value]:a.patch)back[key]=a.baseline.at(key);
     Check(settings.Write(back,error) && host.Restart(true,seen,error) && host.Finish(true,error),"restore observes and retires the deferred attempt");
     Check(configWrites==0 && restarts==0 && !files.contains(journalFile) && !host.RecoveryActive() && Live(settings)==a.baseline,"a restored deferred attempt never archives its candidate");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=DeferredAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a deferred attempt across a module change");
     ++actual.moduleEpoch;SettingsDisplayObservation seen;
     Check(!host.Restart(false,seen,error) && host.RecoveryActive() && files.contains(journalFile),"a replaced renderer module cannot prove the deferred attempt");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=DeferredAttempt(settings);
     actual.rendererReady=false;Check(!host.Prepare(a,error) && writes==0 && !files.contains(journalFile),"no presenting renderer refuses deferred preparation");
     actual.rendererReady=true;Check(host.CancelPreparation(error) && !host.RecoveryActive() && leases.empty(),"the refused preparation releases its lease");}
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=DeferredForStartup(settings,confirmed);
     // Config loading may have left either side live; unrelated changes survive.
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     const bool crt=!std::get<bool>(Live(settings).at("r_crt"));localCVarSystem.variables.at("r_crt").value=crt?"1":"0";
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the deferred record");
     const auto current=Live(settings);
     Check(current.at("r_crt")==StateValue(crt),"deferred startup preserves unrelated live changes");
     for(const auto& [key,value]:a.patch)Check(current.at(key)==(confirmed?a.target:a.baseline).at(key),"deferred startup chooses the committed direction");
     Check(host.RecoveryActive() && !host.StartupActive() && configWrites==0 && files.contains(journalFile),"replayed values wait for a full frame before persistence");
     Check(host.InitializeDisplay(error) && initializations==0,"a deferred record never initializes a recorded display");
     host.StartupFrame(1,false);Check(configWrites==0 && files.contains(journalFile),"a loading frame never persists deferred recovery");
     traceEnabled=true;host.StartupFrame(1,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile) && leases.empty(),"a full frame commits the recovered choice then retires the record");
     Check(Traced(std::string("UI_SETTINGS_STARTUP approved=")+(confirmed?"1":"0")+" deferred=1"),"the completion trace names the deferred executor");}
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=DeferredForStartup(settings,confirmed);
     if(!confirmed)for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(a.baseline.at(key)));
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the deferred record before the renderer");
     // Renderer and window startup settle keys the record does not own.
     localCVarSystem.variables.at("r_windowWidth").value="1366";localCVarSystem.variables.at("r_multiSamples").value="8";
     host.StartupFrame(1,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile) && cvarSystem->GetCVarInteger("r_windowWidth")==1366,
           "startup settling unowned keys still commits the recovered choice");}
    {ResetFixture();SystemSettingsHost settings;auto a=DeferredForStartup(settings,true);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the deferred record");
     // A platform default rewrites an owned rider after the replay.
     localCVarSystem.variables.at("r_brightness").value="1.75";
     host.StartupFrame(1,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile) && Live(settings).at("r_brightness")==a.target.at("r_brightness"),
           "an owned key startup changed goes back to the recovered choice before the commit");}
    for(int failure=0;failure<2;++failure){ResetFixture();SystemSettingsHost settings;DeferredForStartup(settings,true);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the deferred record to fail");
     const auto bytes=files.at(journalFile);
     if(failure==0)configFailure=true;
     if(failure==1)removeFailure=true;
     host.StartupFrame(1,true);
     Check(host.RecoveryActive() && files.contains(journalFile) && files.at(journalFile)==bytes && !host.RecoveryError().empty() && configWrites==1,
           "a failed deferred startup commit keeps its record and reports why");
     configFailure=removeFailure=false;host.StartupFrame(2,true);
     Check(configWrites==1 && files.contains(journalFile),"a failed deferred startup commit is never retried blindly");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;DeferredForStartup(settings,false);auto j=EffectJournal();
     j.deferredTarget["lightGridPreload"]=j.deferredRestore.at("lightGridPreload");StoreEffect(j);
     EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.contains(journalFile) && host.RecoveryActive(),"a deferred record contradicting its snapshots is refused before replay");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;DeferredForStartup(settings,false);
     localCVarSystem.variables.at("r_brightness").value="1.75";EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.contains(journalFile),"a divergent owned key blocks deferred replay");host.Shutdown();}
}

// The deferred replay compares exactly too: a subnormal ambient rider under
// DAZ must not read as zero when choosing or checking an owned key.
static void DeferredExactCases(){
    ExactMode mode;
    for(bool confirmed:{false,true})for(bool conflict:{false,true}){
     ResetFixture();SystemSettingsHost settings;SetExactAmbient(1);auto a=DeferredAttempt(settings,false);
     a.target["r_forceAmbient"]=0.0;a.patch["r_forceAmbient"]=0.0;
     Check(settings.Validate(a.baseline,a.target,error) && SystemSettingsHost::ApplyClassOf(a.baseline,a.target)==SystemApplyClass::Deferred,
           "an exact ambient rider keeps the deferred class");
     {EngineSettingsDisplayHost preparing(settings);Check(preparing.Prepare(a,error),"prepare an exact deferred journal");
      if(confirmed){Check(settings.Write(a.patch,error),"write the exact deferred target");SettingsDisplayObservation observation;
       Check(preparing.Restart(false,observation,error),"observe the exact deferred target");Present();
       Check(preparing.PersistConfirmation(a,error),"persist the exact deferred target");}
      preparing.Shutdown();}
     const auto saved=EffectJournal();Check(saved.patch.contains("r_forceAmbient") &&
       std::bit_cast<std::uint64_t>(std::get<double>(saved.baseline.at("r_forceAmbient")))==1,"the deferred journal keeps the exact original and the zero patch");
     SetExactAmbient(conflict?2:confirmed?1:0);writes=configWrites=0;trace.clear();
     EngineSettingsDisplayHost replay(settings);
     if(conflict){const auto bytes=files.at(journalFile);
      Check(!replay.Startup(error) && writes==0 && configWrites==0,"deferred startup rejects a bit-distinct divergent owned field before all writes");
      Check(AmbientIs(settings,2) && files.at(journalFile)==bytes,"a conflicting deferred replay preserves the live value and the evidence");}
     else{
      Check(replay.Startup(error),"deferred startup restores the exact zero/subnormal direction");
      Check(AmbientIs(settings,confirmed?0:1),"deferred startup emits the required zero or original tiny patch");
      Check(writes>0 && configWrites==0,"deferred startup changes the owned value without a premature archive");
      if(confirmed){
       // A different subnormal reads as zero under DAZ; setting the owned value
       // back must still see it.
       SetExactAmbient(2);replay.StartupFrame(1,true);
       Check(configWrites==1 && !files.contains(journalFile) && !replay.RecoveryActive() && AmbientIs(settings,0),
             "a bit-distinct owned value goes back to the exact recovered choice");
      }else{
       replay.StartupFrame(1,true);
       Check(configWrites==1 && !files.contains(journalFile) && !replay.RecoveryActive(),"exact deferred recovery commits then removes the journal");}}
     replay.Shutdown();
    }
}

// The renderer fallback: a schema-2 record with the renderer domain, a checked
// device restart on the same display, and the renderer's own selection report.
static SettingsAttempt RendererAttempt(SystemSettingsHost& settings,const char* request="arb2"){
    SettingsAttempt a{31,113,Live(settings),{},{}};a.completion=SettingsCompletion::Automatic;a.target=a.baseline;
    a.target["r_renderer"]=std::string(request);
    for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
    Check(settings.Validate(a.baseline,a.target,error) && SystemSettingsHost::ApplyClassOf(a.baseline,a.target)==SystemApplyClass::Renderer,
          "validate renderer attempt fixture");
    return a;
}
static SettingsAttempt RendererForStartup(SystemSettingsHost& settings,bool confirmed){
    EngineSettingsDisplayHost preparing(settings);auto a=RendererAttempt(settings);
    Check(preparing.Prepare(a,error),"prepare renderer startup fixture");
    if(confirmed){Check(settings.Write(a.patch,error),"write confirmed renderer fixture");SettingsDisplayObservation observed;
        Check(preparing.Restart(false,observed,error),"restart confirmed renderer fixture");Present();
        Check(preparing.PersistConfirmation(a,error),"persist confirmed renderer fixture");}
    preparing.Shutdown();Check(files.contains(journalFile) && leases.empty() && !geometryToken,"shutdown keeps the renderer evidence and releases the leases");
    configWrites=0;writes=0;trace.clear();return a;
}
static void RendererCases(){
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     Check(host.SupportsRendererSelection(),"a presenting OpenGL renderer with a selection report supports the fallback");
     moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;Check(!host.SupportsRendererSelection(),"Vulkan has no renderer fallback");
     moduleStatus.activeApi=RENDER_MODULE_API_GL;
     selectionReportValid=false;Check(!host.SupportsRendererSelection(),"no selection report, no renderer change");selectionReportValid=true;
     actual.rendererReady=false;Check(!host.SupportsRendererSelection(),"an unready renderer cannot prove the change");actual.rendererReady=true;
     auto vulkan=RendererAttempt(settings);moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;
     Check(!host.Prepare(vulkan,error) && writes==0 && !files.contains(journalFile) && !geometryToken,"Prepare refuses a renderer change Vulkan cannot report");
     moduleStatus.activeApi=RENDER_MODULE_API_GL;Check(host.CancelPreparation(error) && leases.empty(),"the refused preparation releases its lease");
     auto wrong=RendererAttempt(settings);wrong.completion=SettingsCompletion::UserConfirmation;
     Check(!host.Prepare(wrong,error) && leases.empty() && files.empty() && !geometryToken && writes==0,
           "a renderer change completes automatically or not at all");}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=RendererAttempt(settings);
     Check(host.Prepare(a,error),"prepare a renderer attempt");
     Check(writes==0 && restarts==0 && configWrites==0 && leases.contains(lockFile) && geometryToken,"renderer preparation journals under both leases without writes or restarts");
     const auto j=EffectJournal();
     Check(j.state==SettingsJournalState::Pending && j.plan.domainMask==SystemSettingRendererResources,"the Pending record has only the renderer domain");
     Check(j.displayRestore==j.displayTarget,"both directions preserve the captured display");
     // The fixture archives "modern", a request outside the page's choices.
     Check(j.resourceRestore.at("request")==a.baseline.at("r_renderer") && j.resourceTarget.at("request")==StateValue(std::string("arb2")),"the record holds both requests");
     Check(j.placement.size()==18 && j.deferredRestore.empty(),"the record holds the placement and no deferred policy");
     Check(settings.Write(a.patch,error),"write the renderer request");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && restarts==1 && seen.ready,"the renderer restarts once on the same display");
     Present();Check(host.Observe(false,seen,error),"the selection still agrees after a presented frame");
     ++selectionReportSerial;Check(!host.Observe(false,seen,error),"a selection the restart did not produce is refused");
     Check(!host.PersistConfirmation(a,error) && EffectJournal().state==SettingsJournalState::Pending && configWrites==0,
           "a selection between the last frame and the save refuses the commit");--selectionReportSerial;
     Check(host.PersistConfirmation(a,error) && EffectJournal().state==SettingsJournalState::Confirmed && configWrites==1,"Confirmed precedes the configuration");
     const auto config=std::find(trace.begin(),trace.end(),"config"),replace=std::find(trace.rbegin(),trace.rend(),"replace-journal").base()-1;
     Check(replace<config,"the durable Confirmed record precedes the configuration write");
     Check(host.Finish(false,error) && !host.RecoveryActive() && !files.contains(journalFile) && !geometryToken && leases.empty(),"Finish retires the record and both leases");}
    for(int fault:{1,2,4}){ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=RendererAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a renderer attempt to refuse");
     selectionFault=fault;SettingsDisplayObservation seen;
     Check(!host.Restart(false,seen,error) && host.RecoveryActive() && files.contains(journalFile),"a selection that ignores, omits or promotes past the request is refused");
     host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=RendererAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a renderer attempt whose restart reports nothing");
     // A frame-time selection already names the new request; the restart must still report.
     PublishSelection();selectionFault=2;SettingsDisplayObservation seen;
     Check(!host.Restart(false,seen,error) && host.RecoveryActive(),"a matching report the restart did not produce is refused");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=RendererAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare an unavailable renderer");
     selectionFault=3;SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"a reported fallback to the automatic pick agrees");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=RendererAttempt(settings);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a renderer attempt to restore");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"apply the renderer request");
     StateValues back;for(const auto& [key,value]:a.patch)back[key]=a.baseline.at(key);
     Check(settings.Write(back,error) && host.Restart(true,seen,error) && restarts==2,"restore restarts on the baseline request");
     Present();Check(host.Observe(true,seen,error) && host.Finish(true,error) && configWrites==0 && !files.contains(journalFile) && !geometryToken,
           "a restored renderer attempt never archives its candidate");}
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=RendererForStartup(settings,confirmed);
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the renderer record before the device starts");
     Check(!host.StartupActive() && host.RecoveryActive() && configWrites==0 && Live(settings).at("r_renderer")==(confirmed?a.target:a.baseline).at("r_renderer"),
           "the recovered request is live before the renderer starts the normal way");
     Check(host.InitializeDisplay(error) && initializations==0 && !geometryToken,"a renderer record initializes no recorded display and leases no geometry");
     PublishSelection(); // The renderer starts and resolves the recovered request.
     host.StartupFrame(1,false);Check(configWrites==0,"a loading frame never commits renderer recovery");
     traceEnabled=true;host.StartupFrame(1,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile),"a full frame with an agreeing selection commits and removes the record");
     Check(Traced(std::string("UI_SETTINGS_STARTUP approved=")+(confirmed?"1":"0")+" renderer=1"),"the completion trace names the renderer executor");}
    {ResetFixture();SystemSettingsHost settings;RendererForStartup(settings,true);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays a renderer record that will disagree");
     selectionFault=1;PublishSelection();host.StartupFrame(1,true);
     Check(configWrites==0 && host.RecoveryActive() && files.contains(journalFile) && !host.RecoveryError().empty(),
           "a disagreeing selection blocks the recovery and keeps the record");
     selectionFault=0;PublishSelection();host.StartupFrame(2,true);Check(configWrites==0,"a blocked renderer recovery is never retried blindly");
     host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;RendererForStartup(settings,true);
     // The captured monitor is gone; a renderer record never needed it.
     displayOrder={1};displayCount=1;currentDisplay=1;
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"a renderer record recovers without its captured monitor");
     PublishSelection();host.StartupFrame(1,true);
     Check(configWrites==1 && !files.contains(journalFile),"the recovery commits on the display the engine found");}
    {ResetFixture();SystemSettingsHost settings;RendererForStartup(settings,true);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"a renderer record replays on a Vulkan launch");
     moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;selectionReportValid=false;host.StartupFrame(1,true);
     Check(configWrites==1 && !files.contains(journalFile) && Live(settings).at("r_renderer")==StateValue(std::string("arb2")),
           "Vulkan has no back end to prove, so the recovered request commits");}
    {ResetFixture();SystemSettingsHost settings;RendererForStartup(settings,false);auto j=EffectJournal();
     j.resourceTarget["request"]=std::string("best");StoreEffect(j);EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.contains(journalFile),"a renderer record contradicting its snapshots is refused before replay");host.Shutdown();}
}

// The combined executor: what a Performance Preset writes, and any batch that
// mixes apply classes. A schema-2 record with every domain the batch changes,
// one device restart, a sound restart for a layout or EFX change, and
// confirmation only when the display itself changed.
static SettingsAttempt CombinedAttempt(SystemSettingsHost& settings,bool display,bool renderer=false){
    SettingsAttempt a{41,131,Live(settings),{},{}};a.target=a.baseline;
    a.completion=display?SettingsCompletion::UserConfirmation:SettingsCompletion::Automatic;
    a.target["image_downSize"]=true;a.target["image_picmip"]=1.0;a.target["image_ignoreHighQuality"]=true;
    a.target["r_rendererUploadMegs"]=32.0;a.target["s_numberOfSpeakers"]=2.0;a.target["s_maxEmitterChannels"]=32.0;
    a.target["s_maxSoundsPerShader"]=1.0;a.target["r_lightGridPreload"]=true;a.target["r_brightness"]=1.25;
    a.target["com_performancePreset"]=std::string("low");
    if(renderer)a.target["r_renderer"]=std::string("arb2");
    if(display){a.target["r_multiSamples"]=4.0;a.target["r_windowWidth"]=960.0;a.target["r_windowHeight"]=540.0;}
    for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
    Check(settings.Validate(a.baseline,a.target,error) && SystemSettingsHost::ApplyClassOf(a.baseline,a.target)==SystemApplyClass::Combined,
          "validate combined attempt fixture");
    return a;
}
static SettingsAttempt CombinedForStartup(SystemSettingsHost& settings,bool confirmed,bool display,bool renderer=false){
    EngineSettingsDisplayHost preparing(settings);auto a=CombinedAttempt(settings,display,renderer);
    Check(preparing.Prepare(a,error),"prepare combined startup fixture");
    if(confirmed){Check(settings.Write(a.patch,error),"write confirmed combined fixture");SettingsDisplayObservation observed;
        Check(preparing.Restart(false,observed,error),"restart confirmed combined fixture");soundState=SOUND_OUTPUT_READY;Present();
        Check(preparing.PersistConfirmation(a,error),"persist confirmed combined fixture");}
    preparing.Shutdown();Check(files.contains(journalFile) && leases.empty() && !geometryToken,"shutdown keeps the combined evidence and releases the leases");
    configWrites=0;writes=0;restarts=0;soundRestarts=0;soundState=SOUND_OUTPUT_READY;trace.clear();return a;
}
static void CombinedCases(){
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     auto a=CombinedAttempt(settings,false);auto withRenderer=CombinedAttempt(settings,false,true);
     Check(host.SupportsCombined(a.baseline,a.target) && host.SupportsCombined(withRenderer.baseline,withRenderer.target),
           "a presenting OpenGL renderer supports a combined attempt, with or without a renderer change");
     moduleStatus.activeApi=RENDER_MODULE_API_VULKAN;
     Check(host.SupportsCombined(a.baseline,a.target) && !host.SupportsCombined(withRenderer.baseline,withRenderer.target),
           "Vulkan supports a combined attempt unless it changes the renderer fallback");
     Check(!host.Prepare(withRenderer,error) && writes==0 && !files.contains(journalFile) && !geometryToken,"Prepare refuses a combined renderer change Vulkan cannot report");
     Check(host.CancelPreparation(error) && leases.empty(),"the refused combined preparation releases its lease");
     moduleStatus.activeApi=RENDER_MODULE_API_GL;
     actual.presentation.available=false;Check(!host.SupportsCombined(a.baseline,a.target),"no presentation, no combined attempt");actual.presentation.available=true;
     auto wrong=a;wrong.completion=SettingsCompletion::UserConfirmation;auto shown=CombinedAttempt(settings,true);shown.completion=SettingsCompletion::Automatic;
     Check(!host.Prepare(wrong,error) && !host.Prepare(shown,error) && leases.empty() && files.empty() && !geometryToken && writes==0,
           "a combined attempt asks for confirmation exactly when it changes the display");}
    // Without a display change: one restart on the same display, automatic.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,false);
     Check(host.Prepare(a,error),"prepare a combined attempt");
     Check(writes==0 && restarts==0 && soundRestarts==0 && configWrites==0 && leases.contains(lockFile) && geometryToken,
           "combined preparation journals under both leases without writes or restarts");
     const auto j=EffectJournal();
     Check(j.state==SettingsJournalState::Pending && j.baseline==a.baseline && j.target==a.target && j.patch==a.patch &&
           j.plan.domainMask==(SystemSettingImageReload|SystemSettingAudioRestart|SystemSettingNextMap|SystemSettingRendererResources) &&
           j.plan.completion==SettingsEffectCompletion::Automatic && j.plan.rendererStrategy==SettingsRendererStrategy::CoalescedDevice,
           "the Pending record carries exact snapshots and every changed domain");
     Check(j.displayRestore==j.displayTarget && j.placement.size()==18,"both directions keep the captured display and its placement");
     std::size_t image=0,audio=0,deferred=0,resource=0;
     for(const auto& item:SystemSettingsHost::Catalog()){
         const StateValues* restore=nullptr;const StateValues* target=nullptr;
         if(item.effects==SystemSettingImageReload){restore=&j.imageRestore;target=&j.imageTarget;++image;}
         else if(item.effects==SystemSettingAudioRestart){restore=&j.audioRestore;target=&j.audioTarget;++audio;}
         else if(item.effects==SystemSettingNextMap){restore=&j.deferredRestore;target=&j.deferredTarget;++deferred;}
         else if(item.effects==SystemSettingRendererResources){restore=&j.resourceRestore;target=&j.resourceTarget;++resource;}
         else continue;
         Check(restore->at(item.key)==a.baseline.at(item.key) && target->at(item.key)==a.target.at(item.key),"each domain holds its settings in both directions");
     }
     Check(j.imageRestore.size()==image && j.audioTarget.size()==audio && j.deferredRestore.size()==deferred && j.resourceTarget.size()==resource &&
           image==9 && audio==3 && deferred==3 && resource==4,"each domain holds exactly its catalog settings");
     Check(settings.Write(a.patch,error),"write the combined patch");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && restarts==1 && soundRestarts==1 && seen.ready,"one device restart and one sound restart apply the batch");
     Present();Check(host.Observe(false,seen,error) && !seen.effectsReady,"the attempt waits while the sound device restarts");
     soundState=SOUND_OUTPUT_READY;Check(host.Observe(false,seen,error) && seen.effectsReady,"a reopened sound device completes the effects");
     Check(host.PersistConfirmation(a,error) && EffectJournal().state==SettingsJournalState::Confirmed && configWrites==1,"Confirmed precedes the configuration");
     const auto config=std::find(trace.begin(),trace.end(),"config"),replace=std::find(trace.rbegin(),trace.rend(),"replace-journal").base()-1;
     Check(replace<config,"the durable Confirmed record precedes the configuration write");
     Check(host.Finish(false,error) && !host.RecoveryActive() && !files.contains(journalFile) && !geometryToken && leases.empty(),"Finish retires the record and both leases");}
    // A sound device that does not come back fails the attempt.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,false);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a combined attempt whose sound device fails");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"the device restarts");soundState=SOUND_OUTPUT_FAILED;
     Check(!host.Observe(false,seen,error),"a sound device that did not reopen fails the attempt");
     // The restore asks nothing of a device that is not open; the sound system retries on its own.
     StateValues back;for(const auto& [key,value]:a.patch)back[key]=a.baseline.at(key);
     Check(settings.Write(back,error) && host.Restart(true,seen,error) && restarts==2 && soundRestarts==1,"restore restarts the device on the baseline");
     Present();Check(host.Observe(true,seen,error) && seen.effectsReady && host.Finish(true,error) && configWrites==0 && !files.contains(journalFile) && !geometryToken,
           "a restored combined attempt never archives its candidate");}
    // A restore while the forward sound restart is still pending waits for it too.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,false);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a combined attempt to restore early");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"apply the combined batch");
     StateValues back;for(const auto& [key,value]:a.patch)back[key]=a.baseline.at(key);
     Check(settings.Write(back,error) && host.Restart(true,seen,error) && soundRestarts==1,"the pending sound restart serves the restore");
     Present();Check(host.Observe(true,seen,error) && !seen.effectsReady,"the restore waits for the sound device");
     soundState=SOUND_OUTPUT_READY;Check(host.Observe(true,seen,error) && seen.effectsReady && host.Finish(true,error),"the restore completes once it reopened");}
    // Sound and load policy only: no device restart, no geometry lease.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     SettingsAttempt a{43,137,Live(settings),{},{}};a.completion=SettingsCompletion::Automatic;a.target=a.baseline;
     a.target["s_useEAXReverb"]=true;a.target["s_maxSoundsPerShader"]=2.0;
     for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
     Check(host.Prepare(a,error) && !geometryToken,"a sound-only combined attempt leases no geometry");
     const auto j=EffectJournal();
     Check(j.plan.domainMask==(SystemSettingAudioRestart|SystemSettingNextMap) && j.plan.rendererStrategy==SettingsRendererStrategy::None &&
           j.displayRestore.empty() && j.placement.empty() && j.imageRestore.empty() && j.resourceRestore.empty(),"its record holds only the sound and load-policy domains");
     Check(settings.Write(a.patch,error),"write the sound patch");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && restarts==0 && soundRestarts==1,"only the sound device restarts");
     soundState=SOUND_OUTPUT_READY;Present();
     Check(host.Observe(false,seen,error) && seen.effectsReady && host.PersistConfirmation(a,error) && host.Finish(false,error) && configWrites==1 && leases.empty(),
           "the sound-only attempt commits");}
    // The emitter limit is read by every sound update; sound that is off waits for its next start.
    for(bool off:{false,true}){ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
     SettingsAttempt a{47,139,Live(settings),{},{}};a.completion=SettingsCompletion::Automatic;a.target=a.baseline;
     a.target["s_maxEmitterChannels"]=24.0;if(off){a.target["s_numberOfSpeakers"]=2.0;soundState=SOUND_OUTPUT_OFF;}
     for(const auto& [key,value]:a.target)if(value!=a.baseline.at(key))a.patch[key]=value;
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare an attempt that restarts no sound device");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && restarts==0 && soundRestarts==0,"no device restarts");
     Present();Check(host.Observe(false,seen,error) && seen.effectsReady && host.PersistConfirmation(a,error) && host.Finish(false,error),"the attempt commits at once");}
    // With a display change: the target display, and the user's confirmation.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,true);
     Check(host.Prepare(a,error) && geometryToken,"prepare a combined attempt that changes the display");
     auto j=EffectJournal();
     Check((j.plan.domainMask & SystemSettingDisplayRestart) && j.plan.completion==SettingsEffectCompletion::DisplayConfirmed &&
           !(j.displayRestore==j.displayTarget),"its record holds the display domain and both displays");
     Check(settings.Write(a.patch,error),"write the combined display patch");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error) && restarts==1 && actual.presentation.samples==4 && actual.window.logicalWidth==960,"one restart applies the display and the resources");
     soundState=SOUND_OUTPUT_READY;Present();
     Check(host.Observe(false,seen,error) && seen.effectsReady && host.PersistConfirmation(a,error),"Keep persists the combined display attempt");
     j=EffectJournal();Check(j.state==SettingsJournalState::Confirmed && configWrites==1 && j.placement.at("target.width")==StateValue(960.0),"the Confirmed record holds the committed placement");
     Check(host.Finish(false,error) && !files.contains(journalFile) && !geometryToken && leases.empty(),"Finish retires the combined display attempt");}
    // A renderer change inside a combined attempt still needs the renderer's report.
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,false,true);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare a combined attempt with a renderer change");
     selectionFault=1;SettingsDisplayObservation seen;
     Check(!host.Restart(false,seen,error) && host.RecoveryActive(),"a selection that ignores the combined request is refused");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);auto a=CombinedAttempt(settings,false,true);
     Check(host.Prepare(a,error) && settings.Write(a.patch,error),"prepare an agreeing combined renderer change");SettingsDisplayObservation seen;
     Check(host.Restart(false,seen,error),"the renderer reports the combined request");soundState=SOUND_OUTPUT_READY;Present();
     ++selectionReportSerial;Check(!host.Observe(false,seen,error),"a later selection is refused");--selectionReportSerial;
     Check(host.Observe(false,seen,error) && host.PersistConfirmation(a,error) && host.Finish(false,error),"the combined renderer change commits");}
    // Cold start without a display change: replay before the devices start, commit on a full frame.
    for(bool confirmed:{false,true})for(bool renderer:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=CombinedForStartup(settings,confirmed,false,renderer);
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     localCVarSystem.variables.at("r_bloom").value="1";
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the combined record before the devices start");
     const auto current=Live(settings);bool chosen=true;
     for(const auto& [key,value]:a.patch)chosen=chosen && current.at(key)==(confirmed?a.target:a.baseline).at(key);
     Check(chosen && current.at("r_bloom")==StateValue(true) && !host.StartupActive() && host.RecoveryActive() && configWrites==0,
           "every owned setting takes the recovered side and unrelated settings stay");
     Check(host.InitializeDisplay(error) && initializations==0 && !geometryToken && restarts==0 && soundRestarts==0,
           "a combined record without a display change starts the devices the normal way");
     PublishSelection();host.StartupFrame(1,false);Check(configWrites==0,"a loading frame never commits combined recovery");
     traceEnabled=true;host.StartupFrame(1,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile),"a full frame commits and removes the combined record");
     Check(Traced(std::string("UI_SETTINGS_STARTUP approved=")+(confirmed?"1":"0")+" combined=1"),"the completion trace names the combined executor");}
    {ResetFixture();SystemSettingsHost settings;CombinedForStartup(settings,true,false,true);
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays a combined record whose renderer will disagree");
     selectionFault=1;PublishSelection();host.StartupFrame(1,true);
     Check(configWrites==0 && host.RecoveryActive() && files.contains(journalFile),"a disagreeing selection blocks the combined recovery");host.Shutdown();}
    // Cold start with a display change: the recorded display, then a presented frame.
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;auto a=CombinedForStartup(settings,confirmed,true);
     for(const auto& [key,value]:a.patch)localCVarSystem.variables.at(key).value=FormatPresentationValue(StatePresentation(confirmed?a.baseline.at(key):value));
     EngineSettingsDisplayHost host(settings);Check(host.Startup(error),"startup replays the combined display record");
     const auto current=Live(settings);bool chosen=true;
     for(const auto& [key,value]:a.patch)chosen=chosen && current.at(key)==(confirmed?a.target:a.baseline).at(key);
     Check(chosen && host.StartupActive() && configWrites==0,"the display route recovers every owned setting, not only the display's");
     Check(host.InitializeDisplay(error) && initializations==1 && actual.presentation.samples==(confirmed?4:0),"it initializes the recovered display");
     host.StartupFrame(1,true);Check(configWrites==0,"it waits for a new presented frame");
     Present();host.StartupFrame(2,true);
     Check(configWrites==1 && !host.RecoveryActive() && !files.contains(journalFile) && !geometryToken,"the presented recovery commits and retires the record");}
    // A record whose domain policy contradicts its snapshots is refused before replay.
    for(int domain=0;domain<4;++domain){ResetFixture();SystemSettingsHost settings;CombinedForStartup(settings,false,false);auto j=EffectJournal();
     if(domain==0)j.imageTarget["image_picmip"]=7.0;
     else if(domain==1)j.audioRestore["s_numberOfSpeakers"]=6.0;
     else if(domain==2)j.deferredTarget.erase("s_maxSoundsPerShader");
     else j.resourceTarget["r_rendererUploadMegs"]=64.0;
     StoreEffect(j);EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.contains(journalFile),"a combined record contradicting its snapshots is refused before replay");host.Shutdown();}
    {ResetFixture();SystemSettingsHost settings;CombinedForStartup(settings,false,false);
     localCVarSystem.variables.at("image_picmip").value="5";EngineSettingsDisplayHost host(settings);
     Check(!host.Startup(error) && writes==0 && files.contains(journalFile),"a divergent owned key blocks combined replay");host.Shutdown();}
}

// The display lists' inputs: the stamp, the topology, the window's display
// (the primary without one) and the labels, English where a string is missing.
static void CatalogInputCases(){
    ResetFixture();SystemSettingsHost settings;EngineSettingsDisplayHost host(settings);
    displayGeneration=7;
    languageDict.strings={{"#str_229914","Automatisch"},{"#str_229973","Desktop nativ"},{"#str_229974","Benutzerdefiniert"},
        {"#str_230083","%d x %d"},{"#str_230084","%s [%s]"},{"#str_230085","%d Hertz"},{"#str_230086","%d - %s"},{"#str_230087","#str_230087"}};
    SystemDisplayCatalogStamp stamp;host.CatalogStamp(stamp);
    Check(stamp.generation==7 && stamp.currentDisplay==2,"the stamp holds the topology generation and the window's display");
    const auto& text=stamp.text;
    Check(text.automatic=="Automatisch" && text.desktop=="Desktop nativ" && text.custom=="Benutzerdefiniert" && text.size=="%d x %d" &&
          text.qualified=="%s [%s]" && text.rate=="%d Hertz" && text.display=="%d - %s" && text.missing=="Display %d (not connected)",
          "each label reads its own string, and a missing one keeps English");
    SystemDisplayCatalogInput input;
    Check(host.CaptureCatalogInput(stamp,input,error) && input.topology.displays.size()==2 && input.currentDisplay==2 && input.text==stamp.text,
          "the input holds the topology, the window's display and the stamp's labels");
    actual.windowValid=false;host.CatalogStamp(stamp);
    Check(stamp.currentDisplay==0 && host.CaptureCatalogInput(stamp,input,error) && input.currentDisplay==1,
          "without a window the primary display describes Auto");
    actual.windowValid=true;displayCount=0;input.currentDisplay=55;
    Check(!host.CaptureCatalogInput(stamp,input,error) && input.currentDisplay==55,"a failed capture leaves the input alone");
    displayCount=2;displayGeneration=1;languageDict.strings.clear();
}

// The renderer replay compares exactly too: a subnormal ambient rider under DAZ.
static void RendererExactCases(){
    ExactMode mode;
    for(bool confirmed:{false,true})for(bool conflict:{false,true}){
     ResetFixture();SystemSettingsHost settings;SetExactAmbient(1);auto a=RendererAttempt(settings);
     a.target["r_forceAmbient"]=0.0;a.patch["r_forceAmbient"]=0.0;
     Check(settings.Validate(a.baseline,a.target,error) && SystemSettingsHost::ApplyClassOf(a.baseline,a.target)==SystemApplyClass::Renderer,
           "an exact ambient rider keeps the renderer class");
     {EngineSettingsDisplayHost preparing(settings);Check(preparing.Prepare(a,error),"prepare an exact renderer journal");
      if(confirmed){Check(settings.Write(a.patch,error),"write the exact renderer target");SettingsDisplayObservation observation;
       Check(preparing.Restart(false,observation,error),"restart the exact renderer target");Present();
       Check(preparing.PersistConfirmation(a,error),"persist the exact renderer target");}
      preparing.Shutdown();}
     SetExactAmbient(conflict?2:confirmed?1:0);writes=configWrites=0;trace.clear();
     EngineSettingsDisplayHost replay(settings);
     if(conflict){const auto bytes=files.at(journalFile);
      Check(!replay.Startup(error) && writes==0 && configWrites==0,"renderer startup rejects a bit-distinct divergent owned field before all writes");
      Check(AmbientIs(settings,2) && files.at(journalFile)==bytes,"a conflicting renderer replay preserves the live value and the evidence");}
     else{
      Check(replay.Startup(error),"renderer startup restores the exact zero/subnormal direction");
      Check(AmbientIs(settings,confirmed?0:1),"renderer startup emits the required zero or original tiny patch");
      PublishSelection();replay.StartupFrame(1,true);
      Check(configWrites==1 && !files.contains(journalFile) && !replay.RecoveryActive(),"exact renderer recovery commits then removes the journal");}
     replay.Shutdown();
    }
}

// Production-encoded journals for a cold-recovery probe. Only the preload
// changes, off to on, so an engine at its defaults can replay either side.
static void EmitDeferredJournals(const std::string& directory){
    for(bool confirmed:{false,true}){ResetFixture();SystemSettingsHost settings;DeferredForStartup(settings,confirmed,false);
        const auto& bytes=files.at(journalFile);const auto path=directory+(confirmed?"/confirmed.dat":"/pending.dat");
        std::FILE* file=std::fopen(path.c_str(),"wb");
        Check(file && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size() && std::fclose(file)==0,"emit a production-encoded deferred journal");}
}

int main(){
    if(const char* directory=std::getenv("OPENQ4_EMIT_DEFERRED_JOURNALS")){EmitDeferredJournals(directory);std::printf("Deferred recovery journals emitted: %d checks\n",checks);return 0;}
    MultisamplingCapability();VulkanMultisampling();ExactCatalogCases();RuntimeJournal();StartupCases();GeometryAndStartupFailures();StartupClockCases();CommitOwnershipCases();ReindexedStartupRetry();UnusedTopologyCases();DeferredCases();DeferredExactCases();RendererCases();RendererExactCases();CombinedCases();CatalogInputCases();std::printf("UI settings display service passed: %d checks\n",checks);}

'''


def main(production_mutations=(), emit_directory=None):
    text = lambda path: (ROOT / path).read_text(encoding="utf-8")
    document = text("src/ui/retained/Document.cpp")
    names = ("bool Identifier(", "std::string PointerPart(", "void Diagnose(", "bool Utf8(",
             "bool LexicalForms(", "bool Parse(", "bool ValidStateValue(", "bool ParseStateValues(")
    validation = '\n'.join(document[document.index(name):document.index('bool Parse(', document.index(name))] if name == 'bool LexicalForms(' else function_body(document, name) for name in names)
    support = host_test.SUPPORT.replace("static int writes=0;", "static std::vector<std::string> trace;\nstatic bool traceEnabled=false;\nstatic int writes=0;")
    support = support.replace("struct idStr : std::string { using std::string::string; using std::string::operator=; };",
                              "#include <cstring>\n"
                              "struct idStr : std::string { using std::string::string; using std::string::operator=; "
                              "static int Cmp(const char* a,const char* b){return std::strcmp(a,b);} };")
    support = support.replace("++writes; if(key!=refuse)", '++writes;trace.push_back("cvar-write"); if(key!=refuse)')
    support = support.replace('idCVar* Find(const char* name)', 'bool GetCVarBool(const char* name){return traceEnabled && std::string(name)=="ui_retainedTrace";}\n    int GetCVarInteger(const char* name){return std::atoi(FindInternal(name)->value.c_str());}\n    idCVar* Find(const char* name)')
    support = support.replace("static int displayCount=2;", "static int displayCount=2;static std::vector<unsigned> displayOrder{1,2};")
    support = support.replace("result[i]=i+1;", "result[i]=displayOrder.at(i);")
    support = support.replace("services.RefreshNativeWindowHandles=", 'services.PrepareWindowSystem=+[]{return true;};\n    services.RefreshNativeWindowHandles=')
    production = '\n'.join(line for line in (text("src/ui/application/SystemSettingsHost.cpp") + '\n' +
                 '#define Fail DisplayHelperFail\n' + text("src/ui/application/SystemDisplay.cpp") + '\n#undef Fail\n' + text("src/ui/SettingsDisplayService.cpp")).splitlines()
                 if not line.startswith("#include "))
    for old, new in production_mutations:
        if production.count(old) != 1:
            raise RuntimeError("Production mutation anchor is not unique")
        production = production.replace(old, new)
    actual = function_body(display_test.MAIN, "static rendererDisplayState_t Actual(")
    # Reuse the production presentation conversion rather than formatting typed
    # fixture values with locale-dependent or lossy decimal output.
    value_format = r'''static PresentationValue StatePresentation(const StateValue& v){PresentationValue p;
        if(auto n=std::get_if<double>(&v))p.data[0]=*n;
        else if(auto b=std::get_if<bool>(&v)){p.type=PresentationType::Boolean;p.data[0]=*b?1:0;}
        else{p.type=PresentationType::String;p.text=std::get<std::string>(v);}return p;}
    '''
    code = (support + display_test.EXTRA + BOUNDARIES + function_body(text("src/framework/CVarSystem.cpp"), "\nbool CVar_ReadDefault(") +
            production + '\n' + actual + '\n' + value_format + CASES)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if not compiler:
        raise RuntimeError("C++20 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="ui-settings-display-service-", dir=ROOT / ".tmp"))
    source = out / "service.cpp"
    source.write_text(code, encoding="utf-8")
    values = out / "values.cpp"
    values.write_text('#include "src/ui/retained/Document.h"\n#include <json/json.h>\n#include <cmath>\n#include <algorithm>\n#include <memory>\nnamespace openq4::ui {\nconstexpr size_t MaxSourceBytes=16*1024*1024;\n' + validation + '\n}\n', encoding="utf-8")
    executable = out / ("service.exe" if os.name == "nt" else "service")
    jsoncpp = wrap_source("jsoncpp")
    command = [compiler, "-std=c++20", "-DUSE_SDL3", "-I", str(ROOT), "-I", str(jsoncpp / "include"),
               str(source), str(values), str(ROOT / "src/ui/application/SettingsJournal.cpp"),
               str(ROOT / "src/ui/application/SettingsEffectPlan.cpp"),
               str(ROOT / "src/ui/retained/Presentation.cpp"), str(ROOT / "src/framework/PerformancePreset.cpp"),
               *(str(jsoncpp / "src/lib_json" / f"json_{name}.cpp") for name in ("reader", "value", "writer")),
               "-o", str(executable)]
    environment = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    if emit_directory:
        Path(emit_directory).mkdir(parents=True, exist_ok=True)
        environment["OPENQ4_EMIT_DEFERRED_JOURNALS"] = str(Path(emit_directory).resolve()).replace("\\", "/")
    inputs = [Path(__file__), Path(host_test.__file__), Path(display_test.__file__),
              *(ROOT / name for name in (
                  "src/ui/SettingsDisplayService.h", "src/ui/SettingsDisplayService.cpp", "src/sound/SoundRestart.h",
                  "src/ui/application/SystemDisplay.h", "src/ui/application/SystemDisplay.cpp",
                  "src/framework/PerformancePreset.h", "src/framework/PerformancePreset.cpp", "src/ui/application/SystemSettingsHost.h", "src/ui/application/SystemSettingsHost.cpp",
                  "src/ui/application/SettingsJournal.h", "src/ui/application/SettingsJournal.cpp",
                  "src/ui/application/SettingsEffectPlan.h", "src/ui/application/SettingsEffectPlan.cpp",
                  "src/ui/application/SettingsValue.h", "src/ui/application/SettingsTransaction.h",
                  "src/ui/retained/Document.h", "src/ui/retained/Document.cpp",
                  "src/ui/retained/Presentation.cpp"))]
    hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    execution = []
    with (out / "test.log").open("w", encoding="utf-8") as log:
        for args in (command, [str(executable)]):
            result = subprocess.run(args, cwd=ROOT, env=environment, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False)
            log.write(json.dumps(args) + '\n' + result.stdout + f'\nexit={result.returncode}\n'); log.flush()
            execution.append({"command": args, "exit_code": result.returncode, "output": result.stdout})
            evidence = {"passed": len(execution) == 2 and result.returncode == 0,
                        "sources": hashes, "execution": execution,
                        "extracted_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                        "log_sha256": hashlib.sha256((out / "test.log").read_bytes()).hexdigest(),
                        "scope": "Production service/host/display helpers and real journal codec; native I/O, renderer, SDL and geometry leases are counted doubles."}
            if emit_directory:
                evidence["scope"] = "Emitted production-encoded deferred recovery journals only; the test cases did not run."
                evidence["emitted"] = environment["OPENQ4_EMIT_DEFERRED_JOURNALS"]
            if executable.exists():
                evidence["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
            (out / "result.json").write_text(json.dumps(evidence, indent=2) + '\n', encoding="utf-8")
            print(result.stdout, end="")
            if result.returncode:
                print(f"Failure evidence: {out / 'test.log'}")
                raise SystemExit(result.returncode)
    print(f"Passing evidence: {out / 'test.log'}")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-deferred-journals", type=Path,
                        help="write production-encoded Pending and Confirmed deferred journals for a cold-recovery probe instead of testing")
    main(emit_directory=parser.parse_args().emit_deferred_journals)
