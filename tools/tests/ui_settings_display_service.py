#!/usr/bin/env python3
"""Exercise production engine display-service methods with counted platform I/O.

Uses the real 53-key SYSTEM host, display request/recovery helpers, journal codec
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
import ui_system_settings_host as host_test
import ui_system_display as display_test

ROOT = Path(__file__).resolve().parents[2]

BOUNDARIES = r'''
#include <cstring>
#include "src/ui/SettingsDisplayService.h"
#include "src/framework/SettingsPersistence.h"
using namespace openq4;
static int checks=0;
static void Check(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
struct Common { void Warning(const char*,...){trace.push_back("warning");} void Printf(const char*,...){trace.push_back("print");} } commonObject,*common=&commonObject;
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
static void ApplyActual(const renderWindowRequest_t& r){
    ++actual.presentation.generation;actual.presentation.samples=r.parms.multiSamples;actual.presentation.swapInterval=r.swapInterval;
    auto& w=actual.window;w.displayId=r.displayId;w.displayIndex=r.displayIndex;
    w.fullscreen=r.parms.fullScreen;w.fullscreenDesktop=r.fullscreenDesktop;w.borderless=r.parms.borderless;
    w.hidden=r.parms.hiddenWindow;w.maximized=r.maximized;
    w.logicalWidth=w.pixelWidth=r.parms.width;w.logicalHeight=w.pixelHeight=r.parms.height;
    if(r.restorePlacement){w.windowX=r.windowX;w.windowY=r.windowY;}
}
bool R_RendererModule_TryDeviceRestart(const renderWindowRequest_t* r,char* error,int size){
    trace.push_back("restart");++restarts;if(!restartOkay)return GeometryError(error,size,"restart failed");ApplyActual(*r);return true;
}
bool R_RendererModule_TryInitializeDisplay(const renderWindowRequest_t* r,char* error,int size){
    trace.push_back("initialize");++initializations;if(!restartOkay)return GeometryError(error,size,"initialize failed");ApplyActual(*r);return true;
}
'''

CASES = r'''
static std::string error;
static StateValues Live(SystemSettingsHost& host){StateValues values;Check(host.Read(values,error),"read live catalog");return values;}
static void ResetFixture(){
    files.clear();leases.clear();geometryToken=0;readFailure=replaceFailure=replacePublishes=removeFailure=configFailure=changedSaveRoot=false;
    geometryFailure=geometryPartial=false;queryOkay=restartOkay=true;displayOrder={1,2};displayCount=2;primaryDisplay=1;currentDisplay=2;configWrites=restarts=initializations=0;archived.clear();
    Seed();localCVarSystem.variables.at("r_swapInterval").value="1";actual=Actual();actual.window.hidden=false;actual.window.focused=true;
    geometry={actual.window.windowX,actual.window.windowY,1280,720,actual.window.windowX,actual.window.windowY,1280,720,true};
    trace.clear();writes=0;
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

int main(){RuntimeJournal();StartupCases();GeometryAndStartupFailures();StartupClockCases();CommitOwnershipCases();ReindexedStartupRetry();UnusedTopologyCases();std::printf("UI settings display service passed: %d checks\n",checks);}

'''


def main():
    text = lambda path: (ROOT / path).read_text(encoding="utf-8")
    document = text("src/ui/retained/Document.cpp")
    names = ("bool Identifier(", "std::string PointerPart(", "void Diagnose(", "bool Utf8(",
             "bool LexicalForms(", "bool Parse(", "bool ValidStateValue(", "bool ParseStateValues(")
    validation = '\n'.join(document[document.index(name):document.index('bool Parse(', document.index(name))] if name == 'bool LexicalForms(' else function_body(document, name) for name in names)
    support = host_test.SUPPORT.replace("static int writes=0;", "static std::vector<std::string> trace;\nstatic int writes=0;")
    support = support.replace("++writes; if(key!=refuse)", '++writes;trace.push_back("cvar-write"); if(key!=refuse)')
    support = support.replace('idCVar* Find(const char* name)', 'bool GetCVarBool(const char*){return false;}\n    int GetCVarInteger(const char* name){return std::atoi(FindInternal(name)->value.c_str());}\n    idCVar* Find(const char* name)')
    support = support.replace("static int displayCount=2;", "static int displayCount=2;static std::vector<unsigned> displayOrder{1,2};")
    support = support.replace("result[i]=i+1;", "result[i]=displayOrder.at(i);")
    support = support.replace("services.RefreshNativeWindowHandles=", 'services.PrepareWindowSystem=+[]{return true;};\n    services.RefreshNativeWindowHandles=')
    production = '\n'.join(line for line in (text("src/ui/application/SystemSettingsHost.cpp") + '\n' +
                 '#define Fail DisplayHelperFail\n' + text("src/ui/application/SystemDisplay.cpp") + '\n#undef Fail\n' + text("src/ui/SettingsDisplayService.cpp")).splitlines()
                 if not line.startswith("#include "))
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
    jsoncpp = ROOT / "subprojects/jsoncpp-1.9.6"
    command = [compiler, "-std=c++20", "-DUSE_SDL3", "-I", str(ROOT), "-I", str(jsoncpp / "include"),
               str(source), str(values), str(ROOT / "src/ui/application/SettingsJournal.cpp"),
               str(ROOT / "src/ui/retained/Presentation.cpp"),
               *(str(jsoncpp / "src/lib_json" / f"json_{name}.cpp") for name in ("reader", "value", "writer")),
               "-o", str(executable)]
    environment = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    inputs = [Path(__file__), Path(host_test.__file__), Path(display_test.__file__),
              *(ROOT / name for name in (
                  "src/ui/SettingsDisplayService.h", "src/ui/SettingsDisplayService.cpp",
                  "src/ui/application/SystemDisplay.h", "src/ui/application/SystemDisplay.cpp",
                  "src/ui/application/SystemSettingsHost.h", "src/ui/application/SystemSettingsHost.cpp",
                  "src/ui/application/SettingsJournal.h", "src/ui/application/SettingsJournal.cpp",
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
            if executable.exists():
                evidence["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
            (out / "result.json").write_text(json.dumps(evidence, indent=2) + '\n', encoding="utf-8")
            print(result.stdout, end="")
            if result.returncode:
                print(f"Failure evidence: {out / 'test.log'}")
                raise SystemExit(result.returncode)
    print(f"Passing evidence: {out / 'test.log'}")


if __name__ == "__main__":
    main()
