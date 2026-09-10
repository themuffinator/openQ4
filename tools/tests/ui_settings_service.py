#!/usr/bin/env python3
"""Exercise production settings service, transaction and config-write guards.

Compiles the real service/transaction/display controller and Common public
config wrappers, with real core headers and value validation. A four-field host,
counted device observations and checked-config edge stand in for engine I/O.
The checked writer itself has native fault tests in
settings_configuration_persistence.py. No synthetic observation qualifies an
actual display/audio restart, journal, renderer, production page or editor.
Each scenario starts a fresh process, preserving the production singleton.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body, require_order

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "src/ui/SettingsService.h"
#include "src/ui/application/SystemSettingsHost.h"
#include "src/ui/application/SettingsDisplayController.h"
using namespace openq4::ui;
static void Check(bool condition,const char* message) {
    if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
static StateValues Initial() {
    return {{"r_brightness",1.0},{"r_mode",0.0},{"r_renderer",std::string("best")},{"r_shadows",true}};
}
static struct HostData {
    StateValues live=Initial(),defaults=Initial();
    int reads=0,defaultReads=0,validations=0;
    std::vector<StateValues> writes;
    bool failRead=false,failDefaults=false,refuseWrite=false,partialWrite=false,confirm=false;
} host;
namespace openq4::ui {
const std::map<std::string,size_t>& SystemSettingsHost::Schema() {
    static const std::map<std::string,size_t> schema={{"r_brightness",0},{"r_mode",0},{"r_renderer",2},{"r_shadows",1}};
    return schema;
}
bool SystemSettingsHost::Read(StateValues& result,std::string& error) {
    ++host.reads;if(host.failRead) { error="bounded host read failed";return false; }
    result=host.live;return true;
}
bool SystemSettingsHost::Defaults(StateValues& result,std::string& error) {
    ++host.defaultReads;if(host.failDefaults) { error="bounded defaults failed";return false; }
    result=host.defaults;return true;
}
bool SystemSettingsHost::Validate(const StateValues&,const StateValues& target,std::string& error) {
    ++host.validations;
    if(target.size()!=Schema().size()) { error="bounded catalog size";return false; }
    for(const auto& [key,type]:Schema()) {
        auto found=target.find(key);
        if(found==target.end() || found->second.index()!=type) { error="bounded catalog type";return false; }
    }
    double brightness=std::get<double>(target.at("r_brightness")),mode=std::get<double>(target.at("r_mode"));
    const auto& renderer=std::get<std::string>(target.at("r_renderer"));
    if(brightness<.5 || brightness>2 || mode<0 || mode>2 || mode!=std::floor(mode) ||
       (renderer!="best" && renderer!="arb2")) { error="bounded range/choice";return false; }
    return true;
}
bool SystemSettingsHost::ValidateRollback(const StateValues&,const StateValues& current,
                                         const StateValues& target,std::string& error) {
    return Validate(current,target,error);
}
bool SystemSettingsHost::Write(const StateValues& patch,std::string& error) {
    host.writes.push_back(patch);
    if(host.refuseWrite) { error="bounded write refused";return false; }
    if(host.partialWrite) {
        host.partialWrite=false;
        host.live.at(patch.begin()->first)=patch.begin()->second;
        error="bounded partial write";return false;
    }
    for(const auto& [key,value]:patch)host.live.at(key)=value;
    return true;
}
bool SystemSettingsHost::RequiresDeviceWork(const StateValues& before,const StateValues& target) {
    return before.at("r_mode")!=target.at("r_mode") || before.at("r_renderer")!=target.at("r_renderer");
}
unsigned SystemSettingsHost::ChangedEffects(const StateValues& before,const StateValues& target) {
    if(before.empty() || target.empty())return 0;
    return (before.at("r_mode")!=target.at("r_mode")?unsigned(SystemSettingDisplayRestart):0u) |
        (before.at("r_renderer")!=target.at("r_renderer")?unsigned(SystemSettingRendererResources):0u);
}
bool SystemSettingsHost::ChangedRequiresDisplayRestart(const StateValues& before,const StateValues& target) {
    return (ChangedEffects(before,target)&SystemSettingDisplayRestart)!=0;
}
bool SystemSettingsHost::NeedsConfirmation(const StateValues&,const StateValues&) const {
    return host.confirm; // Fault-injected service state, never a device result.
}
}
struct Common {
    int warnings=0,prints=0;
    std::vector<std::string> lines;
    void Warning(const char*,...) { ++warnings; }
    void Printf(const char* format,...) {
        ++prints;char line[2048];va_list args;va_start(args,format);
        std::vsnprintf(line,sizeof(line),format,args);va_end(args);lines.emplace_back(line);
    }
} commonObject,*common=&commonObject;
enum { CVAR_ARCHIVE=1 };
struct idFile {} file;
struct Files {
    int opens=0,closes=0;
    bool lockFailure=false,commitFailure=false;
    idFile* OpenFileWrite(const char*) { ++opens;return &file; }
    void CloseFile(idFile* value) { Check(value==&file,"close correct file");++closes; }
} files,*fileSystem=&files;
struct Cvars {
    int flags=CVAR_ARCHIVE,clears=0,serializations=0;
    bool trace=false;
    bool GetCVarBool(const char* key) const { Check(std::string(key)=="ui_retainedTrace","trace lookup only");return trace; }
    int GetModifiedFlags() const { return flags; }
    void ClearModifiedFlags(int value) { flags&=~value;++clears; }
    void WriteFlaggedVariables(int value,const char*,idFile* output) {
        Check(value==CVAR_ARCHIVE && output==&file,"serialize archive to open file");++serializations;
    }
} cvars,*cvarSystem=&cvars;
struct idKeyInput { static inline int serializations=0;static void WriteBindings(idFile*) { ++serializations; } };
struct Arena { bool NeedsCleanup() const { return false; } } arenaCampaign;
struct Developer { bool value=true;bool GetBool() const { return value; } void SetBool(bool next) { value=next; } } com_developer;
static bool com_fullyInitialized=true;
static const char* CONFIG_FILE="test.cfg";
struct Session { int writes=0;void WriteCDKey() { ++writes; } } sessionObject,*session=&sessionObject;
struct idCommonLocal {
    bool WriteConfigToFileChecked(const char*,bool coordinatorOwnsLock,std::string& error) {
        if((!coordinatorOwnsLock && UI_SettingsBlocksConfigWrite()) || files.lockFailure){error="blocked/locked";return false;}
        ++files.opens;++files.closes;++cvars.serializations;++idKeyInput::serializations;
        if(files.commitFailure){error="durable commit failed";return false;}
        error.clear();return true;
    }
    void WriteConfigToFile(const char* filename);
    void WriteConfiguration();
    void Printf(const char*,...) {}
} commonLocal;

// The engine adapter's platform edge is deliberately isolated. All sequencing,
// request identities, transaction writes and service ownership use production.
static struct DeviceData {
    SettingsDisplayObservation observation{1,1,0,0,0,true,false,true,false};
    bool held=false,startup=false,blocked=false,refusePrepare=false,refusePersist=false,refuseRestart=false,refuseFinish=false;
    int prepares=0,cancels=0,restarts=0,restores=0,observes=0,persists=0,finishes=0,startups=0,frames=0,shutdowns=0;
} deviceData;
class EngineSettingsDisplayHost final:public SettingsDisplayHost {
public:
    explicit EngineSettingsDisplayHost(SystemSettingsHost&){}
    bool Prepare(const SettingsAttempt&,std::string& error)override{
        ++deviceData.prepares;deviceData.held=true;
        if(deviceData.refusePrepare){error="native preparation refused";return false;}return true;
    }
    bool CancelPreparation(std::string&)override{++deviceData.cancels;deviceData.held=false;return true;}
    bool Restart(bool restoring,SettingsDisplayObservation& observed,std::string& error)override{
        ++deviceData.restarts;if(restoring)++deviceData.restores;
        if(deviceData.refuseRestart){error="native restart refused";return false;}
        ++deviceData.observation.generation;
        deviceData.observation.submitted=deviceData.observation.presented=0;
        observed=deviceData.observation;return true;
    }
    bool Observe(bool,SettingsDisplayObservation& observed,std::string&)override{
        ++deviceData.observes;observed=deviceData.observation;return true;
    }
    bool PersistConfirmation(const SettingsAttempt&,std::string& error)override{
        ++deviceData.persists;
        if(deviceData.refusePersist){error="durable confirmation refused";return false;}
        if(!commonLocal.WriteConfigToFileChecked(CONFIG_FILE,true,error))return false;
        cvars.ClearModifiedFlags(CVAR_ARCHIVE);return true;
    }
    bool Finish(bool,std::string& error)override{
        ++deviceData.finishes;if(deviceData.refuseFinish){error="journal removal refused";return false;}
        deviceData.held=false;return true;
    }
    bool Startup(std::string&) {++deviceData.startups;return !deviceData.blocked;}
    bool InitializeDisplay(std::string&) {return !deviceData.blocked;}
    void StartupFrame(double,bool) {++deviceData.frames;}
    void Shutdown(){++deviceData.shutdowns;deviceData.held=false;}
    bool RecoveryActive()const noexcept{return deviceData.held || deviceData.startup || deviceData.blocked;}
    bool StartupActive()const noexcept{return deviceData.startup;}
};
// Execute the production Session::UpdateScreen frame boundary with a counted
// synchronous renderer. No window, GPU or input APIs are used by these doubles.
static std::function<void()> beginHook, drawHook, endHook;
static int completedFrames=0;
struct FrameRenderer {
    int GetScreenWidth()const{return 1280;}
    int GetScreenHeight()const{return 720;}
    void SetLoadingScreenSwapIntervalBypass(bool){}
    void BeginFrame(int,int){if(beginHook)beginHook();}
    void EndFrame(int*,int*){if(endHook)endHook();}
} frameRenderer,*renderSystem=&frameRenderer;
struct Speeds {bool value=false;bool GetBool()const{return value;}} com_speeds;
static int com_editors=0;
static bool Sys_IsWindowVisible(){return true;}
static void Sys_GrabMouseCursor(bool){Check(false,"input path forbidden in test");}
static void RetainedUI_FrameSubmitted(){++completedFrames;}
struct idSessionLocal {
    bool insideUpdateScreen=false,insideExecuteMapChange=false;
    int time_frontend=0,time_backend=0;
    void Draw(){if(drawHook)drawHook();}
    void UpdateScreen(bool outOfSequence=false);
};
'''

MAIN = r'''
static ActionInvocation Invocation(const std::string& suffix,StateValues arguments={}) {
    ActionInvocation result;result.operation="settings.system."+suffix;result.arguments=std::move(arguments);return result;
}
static bool Dispatch(std::uint64_t owner,const std::string& suffix,StateValues arguments={}) {
    std::string error;return UI_SettingsDispatch(owner,Invocation(suffix,std::move(arguments)),error);
}
static StateValues Read(std::uint64_t owner) {
    StateValues values={{"unrelated",std::string("sentinel")}};
    Check(UI_SettingsRead(owner,values),"registered owner can read");
    Check(!values.contains("unrelated"),"read publishes a complete service-only result");
    return values;
}
static void Expect(std::uint64_t owner,const std::string& key,const StateValue& value) {
    Check(Read(owner).at("settings."+key)==value,key.c_str());
}
static std::uint64_t Begin() {
    const auto owner=UI_SettingsCreateOwner();
    Check(owner && Dispatch(owner,"begin"),"create and begin");
    Expect(owner,"open",true);Expect(owner,"message",std::string("#str_229982"));return owner;
}
static void Private(std::uint64_t owner,bool busy) {
    const auto values=Read(owner);
    Check(values.size()==12,"nonowner gets status only, no catalog snapshots");
    Check(values.at("settings.open")==StateValue(false) && values.at("settings.busy")==StateValue(busy),"nonowner ownership flags");
    Check(values.at("settings.dirty")==StateValue(false) && values.at("settings.canApply")==StateValue(false),"nonowner cannot inspect draft");
    Check(values.at("settings.phase")==StateValue(0.0),"nonowner phase is closed");
}
static std::uint64_t Pending() {
    const auto owner=Begin();host.confirm=true;
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}) && Dispatch(owner,"apply"),"apply a bounded confirmation change");
    Expect(owner,"phase",static_cast<double>(SettingsPhase::Confirming));
    Expect(owner,"message",std::string("#str_229988"));
    Check(UI_SettingsBlocksConfigWrite(),"confirmation blocks all config writes");return owner;
}
static void Validation() {
    const auto& schema=UI_SettingsStateSchema();
    Check(schema.size()==20 && schema.at("settings.message")==2 && schema.at("settings.open")==1 &&
          schema.at("settings.phase")==0,"service status schema types");
    for(const auto& [key,type]:SystemSettingsHost::Schema())
        Check(schema.at("settings.draft."+key)==type && schema.at("settings.baseline."+key)==type,"typed snapshot schema");
    for(const auto& [key,type]:std::map<std::string,size_t>{{"settings.request",2},{"settings.confirmationVisible",1},
        {"settings.canConfirm",1},{"settings.canRevert",1},{"settings.canRetry",1},{"settings.remaining",0}})
        Check(schema.at(key)==type,"display confirmation status schema types");
    std::string error;
    for(const char* operation:{"begin","defaults","cancel","apply","applyExit","confirm","revert"}) {
        auto invocation=Invocation(operation);Check(UI_SettingsInvocation(invocation,error),"known no-argument operation");
        invocation.arguments={{"r_brightness",1.0}};
        Check(!UI_SettingsInvocation(invocation,error),"no-argument operation rejects extras");
    }
    Check(UI_SettingsInvocation(Invocation("edit",Initial()),error),"typed complete edit shape accepted");
    for(const char* op:{"confirm","revert","retry"}) {
        Check(UI_SettingsInvocation(Invocation(op,{{"request",std::string("123")}}),error),"typed displayed request accepted");
        Check(!UI_SettingsInvocation(Invocation(op,{{"request",123.0}}),error),"request token is never an imprecise number");
        Check(!UI_SettingsInvocation(Invocation(op,{{"request",std::string("123")},{"extra",true}}),error),"request argument has exact shape");
    }
    Check(!UI_SettingsInvocation(Invocation("retry"),error),"retry always requires displayed request identity");
    for(const auto& patch:std::vector<StateValues>{{},{{"unknown",true}},{{"r_brightness",true}},
        {{"r_shadows",1.0}},{{"r_renderer",0.0}},{{"r_brightness",std::numeric_limits<double>::infinity()}},
        {{"r_brightness",std::numeric_limits<double>::quiet_NaN()}},{{"r_brightness",1e13}},
        {{"r_renderer",std::string(65537,'x')}},{{"r_renderer",std::string("a\0b",3)}},
        {{"r_renderer",std::string("\xc0\xaf",2)}},{{"r_mode;quit",0.0}}})
        Check(!UI_SettingsInvocation(Invocation("edit",patch),error),"invalid invocation rejected before host access");
    for(const char* operation:{"", "exec", "begin;quit", "Begin", "edit.extra"})
        Check(!UI_SettingsInvocation(Invocation(operation),error),"operation allowlist is exact");
    Action descriptor;descriptor.operation="settings.system.edit";
    Expression value;value.type=0;descriptor.arguments["r_brightness"]=value;
    Check(UI_SettingsOperation(descriptor,error),"compiled number expression accepted");
    descriptor.arguments["r_brightness"].type=1;
    Check(!UI_SettingsOperation(descriptor,error),"compiled expression type mismatch rejected");
    Check(host.reads==0 && host.validations==0 && host.writes.empty(),"operation validation is side-effect free");
}
static void Ownership() {
    StateValues output={{"sentinel",true}};const auto sentinel=output;
    Check(!UI_SettingsRead(0,output) && output==sentinel && !UI_SettingsRead(9876,output) && output==sentinel,"invalid read preserves caller output");
    Check(!Dispatch(0,"begin") && !Dispatch(9876,"begin") && host.reads==0,"unknown owners cannot acquire");
    const auto first=Begin(),second=UI_SettingsCreateOwner();Check(second!=first,"owner tokens unique");
    Check(Dispatch(first,"edit",{{"r_brightness",1.5}}),"owner edits draft");
    const auto draft=Read(first);const int reads=host.reads,validations=host.validations;
    for(const char* op:{"begin","edit","defaults","apply","applyExit","confirm","revert","cancel"}) {
        Check(!Dispatch(second,op,std::string(op)=="edit"?StateValues{{"r_shadows",false}}:StateValues{}),"foreign operation rejected");
        Private(second,true);Expect(second,"message",std::string("#str_229983"));
    }
    Check(Read(first)==draft && host.reads==reads && host.validations==validations && host.writes.empty(),"foreign owner cannot inspect or mutate host/draft");
    Check(Dispatch(first,"begin") && Read(first)==draft && host.reads==reads,"same-owner begin keeps unsaved draft");
    UI_SettingsCloseOwner(second);Check(Read(first)==draft,"closing nonowner preserves current draft");
    UI_SettingsReleaseOwner(second);output=sentinel;
    Check(!UI_SettingsRead(second,output) && output==sentinel && !Dispatch(second,"begin"),"released token cannot read or reacquire");
    UI_SettingsReleaseOwner(first);Check(host.writes.empty(),"editing release never writes live state");
}
static void Drafts() {
    const auto owner=Begin();
    Check(!Dispatch(owner,"edit",{{"r_brightness",1.5},{"r_mode",1.5}}),"whole merged draft validated atomically");
    Expect(owner,"draft.r_brightness",1.0);
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5},{"r_shadows",false}}),"draft immediate settings");
    Expect(owner,"message",std::string("#str_229989"));Expect(owner,"canApply",true);
    Check(host.live==Initial() && host.writes.empty() && !UI_SettingsBlocksConfigWrite(),"editing does not apply or block durable baseline");
    Check(Dispatch(owner,"apply"),"apply immediate draft");
    Check(host.writes==std::vector<StateValues>{{{"r_brightness",1.5},{"r_shadows",false}}},"write only changed values");
    Expect(owner,"baseline.r_brightness",1.5);Expect(owner,"dirty",false);
    Check(Dispatch(owner,"apply") && host.writes.size()==1,"unchanged apply has no host write");
    Check(Dispatch(owner,"defaults") && host.defaultReads==1 && host.writes.size()==1,"defaults remain a draft");
    Expect(owner,"draft.r_brightness",1.0);Expect(owner,"baseline.r_brightness",1.5);
    const auto draft=Read(owner);host.failDefaults=true;
    Check(!Dispatch(owner,"defaults"),"failed defaults rejected");
    Expect(owner,"draft.r_brightness",1.0);host.failDefaults=false;
    Check(Dispatch(owner,"apply") && host.live==Initial(),"default draft can apply immediate values");
    Check(Dispatch(owner,"edit",{{"r_brightness",1.7}}) && Dispatch(owner,"revert"),"editing revert discards draft");
    Expect(owner,"draft.r_brightness",1.0);
    Check(Dispatch(owner,"edit",{{"r_shadows",false}}),"edit before cancel");
    const auto writes=host.writes.size();const int reads=host.reads;
    host.live["r_brightness"]=1.8;
    Check(Dispatch(owner,"cancel") && host.writes.size()==writes && host.reads==reads,"cancel closes without host I/O");
    Private(owner,false);Check(host.live.at("r_brightness")==StateValue(1.8),"cancel preserves external host values");
    Check(Dispatch(owner,"cancel") && host.writes.size()==writes && host.reads==reads,
        "repeated closed cancel permits local-only discard without host I/O");
    UI_SettingsReleaseOwner(owner);
    Check(!Dispatch(owner,"cancel"),"closed cancel still requires a registered live owner");
    const auto fresh=UI_SettingsCreateOwner();
    Check(Dispatch(fresh,"cancel"),"registered local-only owner can discard without opening a transaction");
    Check(!Dispatch(fresh,"edit",{{"r_brightness",1.4}}),"closed edit rejected");
    Expect(fresh,"message",std::string("#str_229984"));
}
static void Devices() {
    const auto owner=Begin();
    for(const auto& patch:std::vector<StateValues>{{{"r_mode",1.0},{"r_brightness",1.5}},
                                                 {{"r_renderer",std::string("arb2")},{"r_shadows",false}}}) {
        Check(Dispatch(owner,"edit",patch),"device changes may be drafted");
        Expect(owner,"canApply",false);const auto baseline=host.live;const int reads=host.reads;
        Check(!Dispatch(owner,"apply") && host.writes.empty() && host.reads==reads && host.live==baseline,"reject full device batch before any live write/read");
        Expect(owner,"message",std::string("#str_229985"));Expect(owner,"phase",static_cast<double>(SettingsPhase::Editing));
        Check(Dispatch(owner,"revert"),"discard device draft");
    }
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}) && Dispatch(owner,"apply"),"immediate edits remain usable after device rejection");
}
static void Conflict() {
    const auto owner=Begin();Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}),"draft before conflict");
    host.live["r_shadows"]=false;
    Check(!Dispatch(owner,"apply") && host.writes.empty(),"external change rejects stale batch without writes");
    Expect(owner,"message",std::string("#str_229986"));
    UI_SettingsFrame();Expect(owner,"message",std::string("#str_229986"));
    Check(Dispatch(owner,"cancel") && Dispatch(owner,"begin"),"reopen refreshes external baseline");
    Expect(owner,"baseline.r_shadows",false);
    Check(host.live.at("r_shadows")==StateValue(false),"conflict handling preserves external values");
}
static void ApplyFailure() {
    const auto owner=Begin();Check(Dispatch(owner,"edit",{{"r_brightness",1.5},{"r_shadows",false}}),"draft before partial failure");
    host.partialWrite=true;
    Check(!Dispatch(owner,"apply") && host.live==Initial(),"partial apply restores exact owned writes");
    Check(host.writes.size()==2 && host.writes.back()==StateValues{{"r_brightness",1.0}},"rollback restores only successfully changed key");
    Expect(owner,"draft.r_brightness",1.5);Expect(owner,"draft.r_shadows",false);
    Expect(owner,"message",std::string("#str_229985"));
    UI_SettingsFrame();Expect(owner,"message",std::string("#str_229985"));
    Check(Dispatch(owner,"apply") && host.live.at("r_brightness")==StateValue(1.5),"failed immediate draft remains retryable");
}
static void Confirmation() {
    const auto owner=Pending();Expect(owner,"canApply",false);
    Check(Dispatch(owner,"confirm") && !UI_SettingsBlocksConfigWrite(),"confirmation commits and releases persistence block");
    Expect(owner,"baseline.r_brightness",1.5);Expect(owner,"message",std::string("#str_229982"));
    Check(Dispatch(owner,"edit",{{"r_brightness",1.7}}) && Dispatch(owner,"apply"),"second pending change");
    Check(Dispatch(owner,"cancel") && !UI_SettingsBlocksConfigWrite(),"cancel pending change restores baseline");
    Expect(owner,"open",true);Expect(owner,"phase",static_cast<double>(SettingsPhase::Editing));
    Check(host.live.at("r_brightness")==StateValue(1.5),"cancel restores last committed value");
    Check(Dispatch(owner,"cancel"),"second cancel closes editing");Private(owner,false);
}
static void AbandonEditing() {
    const auto owner=Begin();Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}),"draft before lifecycle close");
    const int reads=host.reads;UI_SettingsCloseOwner(owner);
    Check(host.reads==reads && host.writes.empty(),"editing close has no host I/O");Private(owner,false);
    const auto next=Begin();Check(next!=owner,"same-frame replacement can acquire new owner");
    UI_SettingsReleaseOwner(owner);Expect(next,"open",true);UI_SettingsReleaseOwner(next);
}
static void AbandonPending() {
    const auto owner=Pending();const auto writes=host.writes.size();const int reads=host.reads;
    UI_SettingsReleaseOwner(owner);
    Check(host.writes.size()==writes && host.reads==reads && UI_SettingsBlocksConfigWrite(),"destructor only queues pending recovery");
    StateValues sentinel={{"sentinel",true}},output=sentinel;
    Check(!UI_SettingsRead(owner,output) && output==sentinel,"destroyed owner cannot read pending state");
    UI_SettingsFrame();Check(host.live==Initial() && host.writes.size()==writes+1 && !UI_SettingsBlocksConfigWrite(),"frame restores and abandons pending owner");
    const int after=host.reads;UI_SettingsFrame();Check(host.reads==after && commonObject.warnings==0,"successful cleanup occurs once");
    Begin();
}
static void Orphan(bool divergent) {
    const auto owner=Pending(),waiter=UI_SettingsCreateOwner();
    if(divergent)host.live["r_brightness"]=1.75;else host.refuseWrite=true;
    UI_SettingsReleaseOwner(owner);UI_SettingsFrame();
    Check(UI_SettingsBlocksConfigWrite() && commonObject.warnings==1,"failed abandonment retains persistence block");
    const auto afterFailure=host.live;const auto writes=host.writes.size();const int reads=host.reads;
    UI_SettingsFrame();UI_SettingsFrame();
    Check(host.writes.size()==writes && host.reads==reads && commonObject.warnings==1,"frames never blindly retry failed recovery");
    Check(!Dispatch(waiter,"begin") && host.writes.size()==writes && host.reads==reads,"explicit begin queues recovery without synchronous host I/O");
    Private(waiter,true);UI_SettingsFrame();
    Check(host.live==afterFailure && UI_SettingsBlocksConfigWrite(),"failed recovery never transfers ownership or overwrites divergence");
    Expect(waiter,"message",std::string(divergent?"#str_229986":"#str_229987"));Private(waiter,true);
    const int failedReads=host.reads;UI_SettingsFrame();Check(host.reads==failedReads,"failed queued attempt is not retried each frame");
    if(divergent)host.live["r_brightness"]=1.0;else host.refuseWrite=false;
    Check(!Dispatch(waiter,"begin"),"safe recovery still waits for frame");
    UI_SettingsFrame();Expect(waiter,"open",true);Expect(waiter,"message",std::string("#str_229982"));
    Expect(waiter,"baseline.r_brightness",1.0);
    Check(!UI_SettingsBlocksConfigWrite() && host.live==Initial(),"safe recovery opens waiting owner with fresh baseline");
}
static void Waiting(bool release) {
    const auto owner=Pending(),waiter=UI_SettingsCreateOwner(),other=UI_SettingsCreateOwner();
    UI_SettingsReleaseOwner(owner);
    Check(!Dispatch(waiter,"begin") && !Dispatch(other,"begin"),"first waiter reserves pending open");
    if(release)UI_SettingsReleaseOwner(waiter);else UI_SettingsCloseOwner(waiter);
    UI_SettingsFrame();Private(other,false);
    Check(!UI_SettingsBlocksConfigWrite() && host.live==Initial(),"closing waiter still permits original recovery");
    if(release) { StateValues output;Check(!UI_SettingsRead(waiter,output),"released waiter remains unavailable"); }
    else Private(waiter,false);
    Check(Dispatch(other,"begin"),"other owner must explicitly open after canceled wait");
}
static void Persistence() {
    const auto owner=Pending();
    commonLocal.WriteConfigToFile("explicit.cfg");commonLocal.WriteConfiguration();
    Check(files.opens==0 && cvars.clears==0 && cvars.flags==CVAR_ARCHIVE && idKeyInput::serializations==0 && sessionObject.writes==0,"confirmation guard precedes open, dirty clearing, bindings and key output");
    host.live["r_brightness"]=1.75;
    Check(!Dispatch(owner,"revert") && UI_SettingsBlocksConfigWrite(),"divergent pending write requires recovery");
    commonLocal.WriteConfigToFile("explicit.cfg");commonLocal.WriteConfiguration();
    Check(files.opens==0 && cvars.clears==0 && cvars.flags==CVAR_ARCHIVE,"recovery guard protects explicit and automatic persistence");
    host.live["r_brightness"]=1.0;Check(Dispatch(owner,"revert"),"safe explicit recovery");
    commonLocal.WriteConfiguration();
    Check(files.opens==1 && files.closes==1 && cvars.clears==1 && cvars.flags==0 && cvars.serializations==1 &&
          idKeyInput::serializations==1 && sessionObject.writes==1 && com_developer.value,"recovered baseline follows ordinary durable config path");
    commonLocal.WriteConfiguration();Check(files.opens==1,"clean archive does not rewrite");
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}),"draft after recovery");
    commonLocal.WriteConfigToFile("explicit.cfg");Check(files.opens==2 && host.live==Initial(),"editing may serialize committed host state without applying draft");
    cvars.flags=CVAR_ARCHIVE;files.lockFailure=true;
    const int clears=cvars.clears,writes=sessionObject.writes;
    commonLocal.WriteConfiguration();commonLocal.WriteConfigToFile("locked.cfg");
    Check(files.opens==2 && cvars.flags==CVAR_ARCHIVE && cvars.clears==clears && sessionObject.writes==writes,"checked lease failure preserves archive dirty and ancillary writes");
    files.lockFailure=false;files.commitFailure=true;commonLocal.WriteConfiguration();
    Check(files.opens==3 && cvars.flags==CVAR_ARCHIVE && cvars.clears==clears && sessionObject.writes==writes,"checked durability failure preserves archive dirty and ancillary writes");
    files.commitFailure=false;commonLocal.WriteConfiguration();
    Check(files.opens==4 && cvars.flags==0 && cvars.clears==clears+1 && sessionObject.writes==writes+1,"successful retry clears archive dirty exactly once");
}
static DocumentModel ConfirmationDocument() {
    DocumentModel document;
    for(const auto& [key,type]:UI_SettingsStateSchema()) {
        StateDeclaration declaration;
        declaration.initial=type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string());
        document.state.emplace(key,declaration);
    }
    for(const auto& [id,operation]:std::map<std::string,std::string>{{"settings_keep","confirm"},{"settings_revert","revert"},{"settings_retry","retry"}}) {
        Node node;node.id=id;node.control=Control{};node.control->label="#str_fixture_label";node.control->action=id;
        document.root.children.push_back(node);
        Action action;action.operation="settings.system."+operation;
        Expression request;request.type=2;request.state="settings.request"; // Canonical direct state lookup has an empty op.
        action.arguments["request"]=request;document.actions[id]=action;
    }
    return document;
}
static void ConfirmationCapability() {
    const auto owner=Begin();
    Check(Dispatch(owner,"edit",{{"r_mode",1.0}}),"draft display value before capability proof");
    Expect(owner,"canApply",false);
    const auto valid=ConfirmationDocument();
    UI_SettingsConfirmationDocument(owner,valid);Expect(owner,"canApply",true);
    for(const char* key:{"settings.request","settings.confirmationVisible","settings.canConfirm","settings.canRevert","settings.canRetry","settings.remaining"}) {
        auto invalid=valid;invalid.state.erase(key);
        UI_SettingsConfirmationDocument(owner,invalid);Expect(owner,"canApply",false);
        invalid=valid;invalid.state[key].cvar="r_mode";
        UI_SettingsConfirmationDocument(owner,invalid);Expect(owner,"canApply",false);
        invalid=valid;invalid.state[key].initial=key==std::string("settings.request")?StateValue(false):StateValue(std::string());
        UI_SettingsConfirmationDocument(owner,invalid);Expect(owner,"canApply",false);
    }
    for(int control=0;control<3;++control) {
        for(int defect=0;defect<8;++defect) {
            auto invalid=valid;auto& node=invalid.root.children[control];auto& action=invalid.actions.at(node.control->action);
            if(defect==0)node.id="wrong-id";
            if(defect==1)node.control.reset();
            if(defect==2)node.control->label.clear();
            if(defect==3)action.operation="settings.system.cancel";
            if(defect==4)action.arguments.at("request").op="state"; // Not the compiler's canonical state representation.
            if(defect==5)action.arguments.at("request").state="old.request";
            if(defect==6)action.arguments.emplace("extra",Expression{});
            if(defect==7)action.arguments.at("request").op="literal";
            UI_SettingsConfirmationDocument(owner,invalid);Expect(owner,"canApply",false);
        }
    }
    UI_SettingsConfirmationDocument(owner,valid);Expect(owner,"canApply",true);
    Check(Dispatch(owner,"edit",{{"r_renderer",std::string("arb2")},{"r_brightness",1.5}}),"mixed unsupported resource batch can be drafted");
    Expect(owner,"canApply",false);
    Check(!Dispatch(owner,"apply") && host.writes.empty() && deviceData.prepares==0,"capability cannot authorize unsupported effects in a mixed batch");
    UI_SettingsConfirmationDocument(owner,DocumentModel{});Check(Dispatch(owner,"revert"),"clear mixed draft");
    Check(Dispatch(owner,"edit",{{"r_mode",1.0}}),"display redraft");Expect(owner,"canApply",false);
}
static std::string AwaitDisplay(std::uint64_t owner,const char* operation="apply") {
    UI_SettingsConfirmationDocument(owner,ConfirmationDocument());
    Check(Dispatch(owner,"edit",{{"r_mode",1.0},{"r_brightness",1.5}}) && Dispatch(owner,operation),"capable owner queues typed display apply");
    const auto request=std::get<std::string>(Read(owner).at("settings.request"));
    Check(!request.empty() && host.writes.empty() && deviceData.prepares==0 && UI_SettingsBlocksConfigWrite(),"actions only freeze request before frame work");
    Expect(owner,"phase",static_cast<double>(SettingsPhase::Applying));Expect(owner,"confirmationVisible",false);
    UI_SettingsFrame(false);
    Check(host.writes.empty() && deviceData.prepares==0,"loading frame never starts device or persistence work");
    UI_SettingsFrame();
    Check(host.writes.size()==1 && deviceData.prepares==1 && deviceData.restarts==1,"normal frame journals then writes and requests restart");
    Expect(owner,"confirmationVisible",true);Expect(owner,"canConfirm",false);
    return request;
}
static void Presented(std::uint64_t owner,const std::string& request) {
    idSessionLocal screen;
    drawHook=[&]{UI_SettingsOwnerDrawn(owner,request);};
    endHook=[] {++deviceData.observation.submitted;++deviceData.observation.presented;};
    screen.UpdateScreen();UI_SettingsFrame(false);
    Expect(owner,"canConfirm",false);
    // A second receipt must not move the first acknowledged counter threshold.
    screen.UpdateScreen();
    UI_SettingsFrame(false);
    Expect(owner,"phase",static_cast<double>(SettingsPhase::Confirming));Expect(owner,"canConfirm",true);
    Check(std::get<double>(Read(owner).at("settings.remaining"))>0,"countdown starts after exact owner frame receipt and one conservative later present");
    drawHook={};endHook={};
}
static void FrameReceipt(const std::string& defect) {
    const auto owner=Begin();const auto request=AwaitDisplay(owner);
    idSessionLocal screen;
    auto present=[] {++deviceData.observation.submitted;++deviceData.observation.presented;};
    drawHook=[&]{UI_SettingsOwnerDrawn(owner,request);};endHook=present;
    if(defect=="outside") {
        UI_SettingsOwnerDrawn(owner,request);drawHook={};
    } else if(defect=="skipped") endHook=[]{};
    else if(defect=="submit_only")endHook=[] {++deviceData.observation.submitted;};
    else if(defect=="present_only")endHook=[] {++deviceData.observation.presented;};
    else if(defect=="readback_before_draw")beginHook=[] {++deviceData.observation.submitted;};
    else if(defect=="readback_after_draw")drawHook=[&] {UI_SettingsOwnerDrawn(owner,request);++deviceData.observation.submitted;};
    else if(defect=="readback_during_end")endHook=[] {deviceData.observation.submitted+=2;++deviceData.observation.presented;};
    else if(defect=="nested")drawHook=[&] {
        UI_SettingsOwnerDrawn(owner,request);
        UI_SettingsRenderFrame nested;UI_SettingsOwnerDrawn(owner,request);nested.Submitting();nested.Presented();
    };
    else if(defect=="aborted")endHook=[&] {present();throw 7;};
    else if(defect=="begin_aborted")beginHook=[] {throw 7;};
    else if(defect=="draw_aborted")drawHook=[&] {UI_SettingsOwnerDrawn(owner,request);throw 7;};
    else if(defect=="wrong_request")drawHook=[&] {UI_SettingsOwnerDrawn(owner,"99999");};
    else if(defect=="wrong_owner")drawHook=[&] {UI_SettingsOwnerDrawn(UI_SettingsCreateOwner(),request);};
    else if(defect=="revoked")drawHook=[&] {UI_SettingsOwnerDrawn(owner,request);UI_SettingsConfirmationDocument(owner,DocumentModel{});};
    else if(defect=="shutdown")drawHook=[] {UI_SettingsShutdown();};
    else if(defect=="historical_offset")deviceData.observation.submitted=17;
    else Check(false,"unknown frame receipt defect");
    try {screen.UpdateScreen();} catch(int value){Check(value==7,"expected bounded renderer abort");}
    beginHook={};drawHook={};endHook=present;
    if(defect=="revoked")UI_SettingsConfirmationDocument(owner,ConfirmationDocument());
    // A later frame without this owner must never revive an aborted/skipped
    // draw. Use a new Session stand-in after abort; scope cleanup is production.
    idSessionLocal next;next.UpdateScreen();UI_SettingsFrame(false);
    if(defect=="shutdown") {
        Check(!UI_SettingsBlocksConfigWrite(),"shutdown abandons only this in-memory frame latch");return;
    }
    if(defect=="historical_offset") {
        Expect(owner,"canConfirm",true);
        Check(deviceData.observation.submitted-deviceData.observation.presented==17,"historical no-present readback offset preserved");
    } else {
        Expect(owner,"canConfirm",false);
        Presented(owner,request); // A fresh healthy owner frame can still qualify.
    }
}
static void FrameReceiptTrace() {
    const auto owner=Begin();const auto request=AwaitDisplay(owner);
    deviceData.observation.submitted=23;deviceData.observation.presented=7;cvars.trace=true;
    Presented(owner,request);
    int views=0,presents=0;
    for(const auto& line:commonObject.lines) {
        if(line.starts_with("UI_SETTINGS_VIEW ")) {
            ++views;Check(line.find("submitted=24 presented=8 failures=0")!=std::string::npos,
                "VIEW is exact post-EndFrame receipt, never the earlier draw counter");
        }
        if(line.starts_with("UI_SETTINGS_PRESENT ")) {
            ++presents;Check(line.find("submitted=25 presented=9 failures=0")!=std::string::npos,
                "PRESENT follows immutable receipt by one conservative presented frame");
        }
    }
    Check(views==1 && presents==1,"one receipt and one confirmation trace per request despite repeated owner redraw");
}
static void FrameReceiptIdentity(const std::string& defect) {
    const auto owner=Begin();const auto request=AwaitDisplay(owner);
    const auto original=deviceData.observation;
    idSessionLocal screen;drawHook=[&]{UI_SettingsOwnerDrawn(owner,request);};
    endHook=[&] {
        ++deviceData.observation.submitted;++deviceData.observation.presented;
        if(defect=="epoch")++deviceData.observation.epoch;
        else if(defect=="generation")++deviceData.observation.generation;
        else if(defect=="failure")++deviceData.observation.failures;
        else if(defect=="not_ready")deviceData.observation.ready=false;
        else if(defect=="counter_regression")deviceData.observation.submitted=0;
        else Check(false,"unknown receipt identity defect");
    };
    screen.UpdateScreen();drawHook={};endHook=[]{};
    // Restore the observation before a later unrelated healthy frame. Without
    // frame-scoped rejection, a stale marker could otherwise qualify here.
    deviceData.observation=original;
    ++deviceData.observation.submitted;++deviceData.observation.presented;
    UI_SettingsFrame(false);Expect(owner,"canConfirm",false);
    Presented(owner,request);
}
static void DisplayDelivery(bool failPersistence) {
    const auto owner=Begin(),other=UI_SettingsCreateOwner();
    const auto request=AwaitDisplay(owner);
    int observations=deviceData.observes;
    UI_SettingsOwnerDrawn(other,request);
    for(const char* invalid:{"","0","01","-1","+1","1x","18446744073709551616"}) {
        UI_SettingsOwnerDrawn(owner,invalid);
        Check(!Dispatch(owner,"confirm",{{"request",std::string(invalid)}}),"malformed displayed request rejected");
    }
    Check(deviceData.observes==observations,"unqualified draw cannot furnish presentation witness");
    Check(!Dispatch(owner,"confirm") && !Dispatch(other,"confirm",{{"request",request}}),"active display requires identity and owner");
    Presented(owner,request);Private(other,true);
    Check(Dispatch(owner,"confirm",{{"request",request}}) && deviceData.persists==0,"Keep queues its durable boundary");
    UI_SettingsFrame(false);Check(deviceData.persists==0,"loading frame cannot persist Keep");
    deviceData.refusePersist=failPersistence;UI_SettingsFrame();
    if(failPersistence) {
        Check(deviceData.persists==1 && deviceData.held && UI_SettingsBlocksConfigWrite(),"uncertain Keep retains journal ownership and write block");
        Expect(owner,"phase",static_cast<double>(SettingsPhase::RecoveryRequired));
        const auto nextRequest=std::get<std::string>(Read(owner).at("settings.request"));
        Check(nextRequest!=request && !Dispatch(owner,"retry",{{"request",request}}),"prepare-confirm renews identity and rejects stale button data");
        Expect(owner,"canRetry",true);Expect(owner,"canRevert",false);
        Check(!Dispatch(owner,"revert",{{"request",nextRequest}}),"Revert never silently retries approved Keep");
        UI_SettingsFrame();Check(deviceData.persists==1,"recovery does not blindly repeat persistence");
        deviceData.refusePersist=false;
        Check(Dispatch(owner,"retry",{{"request",nextRequest}}),"explicit recovery retries immutable Keep intent");
        UI_SettingsFrame();
    }
    Check(!UI_SettingsBlocksConfigWrite() && !deviceData.held && deviceData.finishes==1 && deviceData.restores==0,"qualified durable Keep releases once without reverting accepted values");
    Expect(owner,"phase",static_cast<double>(SettingsPhase::Editing));Expect(owner,"baseline.r_mode",1.0);Expect(owner,"request",std::string());
}
static void DisplayClose(bool written) {
    const auto owner=Begin();
    if(written)AwaitDisplay(owner);
    else {
        UI_SettingsConfirmationDocument(owner,ConfirmationDocument());
        Check(Dispatch(owner,"edit",{{"r_mode",1.0}}) && Dispatch(owner,"apply"),"queue unwritten display before close");
    }
    const auto writes=host.writes.size();UI_SettingsReleaseOwner(owner);
    Check(host.writes.size()==writes && UI_SettingsBlocksConfigWrite(),"GUI destructor only queues cleanup");
    UI_SettingsFrame(false);Check(host.writes.size()==writes,"loading frame cannot restore display");
    UI_SettingsFrame();
    if(written) {
        Check(deviceData.restores==1 && UI_SettingsBlocksConfigWrite(),"restoration waits for separate presentation witness");
        ++deviceData.observation.submitted;++deviceData.observation.presented;
        UI_SettingsFrame();
    }
    Check(!UI_SettingsBlocksConfigWrite() && host.live==Initial(),"closed display returns verified baseline before release");
    if(!written)Check(host.writes.empty() && deviceData.prepares==0 && deviceData.restarts==0,"queued cancellation performs no host or device work");
    Begin();
}
static void StartupShutdown() {
    std::string error;
    Check(UI_SettingsStartup(error) && UI_SettingsInitializeDisplay(error) && !UI_SettingsStartupActive(),"service startup routes engine adapter");
    deviceData.startup=true;
    const auto owner=UI_SettingsCreateOwner();
    Check(UI_SettingsStartupActive() && UI_SettingsBlocksConfigWrite() && !Dispatch(owner,"begin"),"startup recovery blocks editing and configuration");
    Check(!Dispatch(owner,"cancel"),"closed cancel cannot bypass startup recovery");
    UI_SettingsFrame(false);Check(deviceData.frames==1,"loading frame forwards poll-only recovery");
    deviceData.startup=false;Check(Dispatch(owner,"begin"),"settled startup admits owner");
    Check(Dispatch(owner,"cancel"),"close owner before separate recovery");
    deviceData.blocked=true;Check(!Dispatch(owner,"cancel"),"closed cancel cannot bypass unresolved device recovery");deviceData.blocked=false;
    UI_SettingsShutdown();Check(deviceData.shutdowns==1,"service shutdown releases engine host once");
    const auto replacement=UI_SettingsCreateOwner();Check(replacement>owner,"service recreation never reuses owner identity");
    StateValues sentinel={{"sentinel",true}},output=sentinel;
    Check(!UI_SettingsRead(owner,output) && output==sentinel,"old owner is invalid after shutdown");
}
static void StaleDisplayActions() {
    const auto owner=Begin();
    const auto request=AwaitDisplay(owner);Presented(owner,request);
    Check(Dispatch(owner,"confirm",{{"request",request}}),"queue Keep before stale action replay");
    UI_SettingsFrame();
    Check(Dispatch(owner,"edit",{{"r_brightness",1.7}}),"edit a later draft after display completion");
    const auto writes=host.writes.size();
    for(const char* operation:{"revert","confirm","retry"}) {
        Check(!Dispatch(owner,operation,{{"request",request}}),"stale display identity cannot enter immediate draft path");
        Expect(owner,"draft.r_brightness",1.7);Expect(owner,"baseline.r_brightness",1.5);
        Check(host.writes.size()==writes && !UI_SettingsBlocksConfigWrite(),"stale display action has no host side effect");
    }
    Check(Dispatch(owner,"revert"),"legacy no-argument draft revert remains available");
    Expect(owner,"draft.r_brightness",1.5);
}
static void TimeoutFrame() {
    const auto owner=Begin();host.confirm=true;
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5}}),"draft before timeout");
    // Only the timing setup reaches through the extraction TU: the production
    // transaction gets an expired epoch, then the real service Frame handles it.
    Check(Settings().transaction.Apply(owner,0,0.001).code==SettingsCode::Ok,"seed already-expired confirmation without sleeping");
    Check(UI_SettingsBlocksConfigWrite(),"expired confirmation stays blocked until frame");
    UI_SettingsFrame();
    Check(host.live==Initial() && !UI_SettingsBlocksConfigWrite(),"normal frame expires and rolls back confirmation");
    Expect(owner,"phase",static_cast<double>(SettingsPhase::Editing));Expect(owner,"message",std::string("#str_229982"));
}
static void NoExit(std::uint64_t owner) {
    Check(!UI_SettingsExitReady(owner) && !UI_SettingsConsumeExit(owner) && !UI_SettingsExitReady(owner),
        "no exit receipt without successful service-owned closure");
}
static void ExitImmediate(bool noop) {
    const auto owner=Begin(),other=UI_SettingsCreateOwner();cvars.trace=true;
    NoExit(owner);
    Check(Dispatch(owner,"apply"),"ordinary clean apply remains open");NoExit(owner);
    if(!noop)Check(Dispatch(owner,"edit",{{"r_brightness",1.5},{"r_shadows",false}}),"immediate draft before apply-exit");
    const auto expected=noop?Initial():StateValues{{"r_brightness",1.5},{"r_mode",0.0},{"r_renderer",std::string("best")},{"r_shadows",false}};
    const auto writes=host.writes.size();
    Check(Dispatch(owner,"applyExit"),"successful immediate/no-op apply-exit");
    Check(host.live==expected && host.writes.size()==writes+(noop?0:1),"exact apply patch and no cancellation writes");
    Expect(owner,"open",false);Expect(owner,"phase",0.0);
    Check(!UI_SettingsBlocksConfigWrite(),"closed immediate transaction releases config guard");
    const auto reads=host.reads,validations=host.validations;
    Check(UI_SettingsExitReady(owner) && UI_SettingsExitReady(owner),"peek preserves successful receipt");
    NoExit(other);NoExit(0);NoExit(99999);
    UI_SettingsFrame(false);UI_SettingsFrame();
    Check(UI_SettingsExitReady(owner) && host.reads==reads && host.validations==validations,"receipt polling/frames do no host work");
    Check(UI_SettingsConsumeExit(owner),"own successful closure consumes exactly once");NoExit(owner);
    int ready=0,consumed=0,canceled=0;
    for(const auto& line:commonObject.lines)if(line.starts_with("UI_SETTINGS_EXIT ")) {
        Check(line.find("owner="+std::to_string(owner)+" request=0 ")!=std::string::npos,"immediate trace binds owner and no device request");
        ready+=line.find("event=ready")!=std::string::npos;
        consumed+=line.find("event=consumed")!=std::string::npos;
        canceled+=line.find("event=canceled")!=std::string::npos;
    }
    Check(ready==1 && consumed==1 && canceled==0,"one successful receipt trace, never canceled on success");
    Check(!Dispatch(owner,"applyExit"),"closed transaction cannot manufacture another exit");NoExit(owner);
}
static void ExitInvalidate(const std::string& operation) {
    const auto owner=Begin();Check(Dispatch(owner,"applyExit") && UI_SettingsExitReady(owner),"seed legitimate unconsumed receipt");
    if(operation=="begin")Check(Dispatch(owner,"begin"),"new transaction for same owner");
    else if(operation=="other_begin") {
        const auto other=Begin();NoExit(owner);Check(Dispatch(other,"cancel"),"closing replacement never revives prior receipt");
    } else if(operation=="close")UI_SettingsCloseOwner(owner);
    else if(operation=="release")UI_SettingsReleaseOwner(owner);
    else if(operation=="shutdown") {UI_SettingsShutdown();Check(UI_SettingsCreateOwner()>owner,"shutdown invalidates receipt identity");}
    else if(operation=="invalid")Check(!Dispatch(owner,"applyExit",{{"exit",true}}),"dictionary flag cannot authorize exit");
    else if(operation=="edit")Check(!Dispatch(owner,"edit",{{"r_brightness",1.7}}),"new operation invalidates old receipt even when closed");
    else if(operation=="cancel")Check(Dispatch(owner,"cancel"),"closed cancel succeeds but invalidates old exit receipt");
    else Check(!Dispatch(owner,operation),"closed transaction operation fails");
    NoExit(owner);Check(host.writes.empty(),"receipt invalidation never writes CVars");
}
static void ExitFailure(const std::string& failure) {
    const auto owner=Begin();
    Check(Dispatch(owner,"edit",{{"r_brightness",1.5},{"r_shadows",false}}),"draft before failed apply-exit");
    if(failure=="unsupported")Check(Dispatch(owner,"edit",{{"r_renderer",std::string("arb2")}}),"unsupported effect remains a valid draft");
    else if(failure=="no_view")Check(Dispatch(owner,"edit",{{"r_mode",1.0}}),"display draft without owner confirmation view");
    else if(failure=="write")host.refuseWrite=true;
    else if(failure=="partial")host.partialWrite=true;
    else if(failure=="read")host.failRead=true;
    else if(failure=="conflict")host.live.at("r_shadows")=false;
    else if(failure=="confirmation")host.confirm=true; // Never infer completion from a successful but provisional write.
    else Check(false,"unknown immediate apply-exit failure");
    Check(!Dispatch(owner,"applyExit"),"unsuccessful or provisional apply cannot close");NoExit(owner);
    Expect(owner,"open",true);
    host.refuseWrite=host.failRead=false;
    if(failure=="confirmation")Check(Dispatch(owner,"confirm"),"legacy compatibility confirmation remains usable");
    if(failure=="unsupported" || failure=="no_view" || failure=="read")Check(host.writes.empty(),"preflight refuses whole batch before write");
    if(failure=="partial")Check(host.live==Initial() && host.writes.size()==2,"partial write rolls back without exit");
    NoExit(owner);
}
static void FinishDisplayRestore(std::uint64_t owner) {
    UI_SettingsFrame();
    if(Settings().display.Stage()==SettingsDisplayStage::AwaitRestore) {
        ++deviceData.observation.submitted;++deviceData.observation.presented;UI_SettingsFrame();
    }
    Check(!UI_SettingsBlocksConfigWrite(),"restoration is fully finalized");NoExit(owner);
}
static void ExitDisplayKeep(const std::string& cancellation) {
    const auto owner=Begin(),other=UI_SettingsCreateOwner();cvars.trace=true;
    const auto request=AwaitDisplay(owner,"applyExit");NoExit(owner);Presented(owner,request);NoExit(owner);
    // Resource re-registration and another owner cannot consume/cancel intent.
    UI_SettingsConfirmationDocument(owner,ConfirmationDocument());
    Check(!Dispatch(other,"confirm",{{"request",request}}),"foreign owner cannot confirm another transaction");NoExit(other);
    if(cancellation=="stale")Check(!Dispatch(owner,"confirm",{{"request",std::string("999999")}}),"stale request fails and cancels owning exit intent");
    else if(cancellation=="invalid")Check(!Dispatch(owner,"confirm",{{"request",1.0}}),"invalid own command cancels exit intent");
    else if(cancellation=="edit")Check(!Dispatch(owner,"edit",{{"r_brightness",1.7}}),"new draft operation during confirmation cancels exit intent");
    else Check(cancellation.empty(),"known Keep cancellation");
    Check(Dispatch(owner,"confirm",{{"request",request}}),"matching Keep queues persistence");NoExit(owner);
    UI_SettingsFrame(false);Check(deviceData.persists==0,"poll-only frame cannot authorize exit");NoExit(owner);
    UI_SettingsFrame();
    Check(deviceData.persists==1 && deviceData.finishes==1 && !deviceData.held,"Keep persisted and finished before exit eligibility");
    if(cancellation.empty()) {
        Expect(owner,"open",false);Check(UI_SettingsExitReady(owner),"successful persisted Keep closes owning transaction");
        Check(UI_SettingsConsumeExit(owner),"asynchronous receipt consumed once");NoExit(owner);
    } else {Expect(owner,"open",true);Expect(owner,"dirty",false);NoExit(owner);}
    int ready=0,consumed=0,armed=0,canceled=0;
    for(const auto& line:commonObject.lines)if(line.starts_with("UI_SETTINGS_EXIT ")) {
        Check(line.find("owner="+std::to_string(owner)+" request="+request+" ")!=std::string::npos,"exit trace retains matching displayed apply request");
        ready+=line.find("event=ready")!=std::string::npos;consumed+=line.find("event=consumed")!=std::string::npos;
        armed+=line.find("event=armed")!=std::string::npos;canceled+=line.find("event=canceled")!=std::string::npos;
    }
    Check(armed==1 && ready==(cancellation.empty()?1:0) && consumed==ready && canceled==(cancellation.empty()?0:1),
        "qualified Keep has one receipt; canceled intent never revives on later successful Keep");
}
static void ExitDisplayRestore(const std::string& cancellation) {
    const auto owner=Begin();const auto request=AwaitDisplay(owner,"applyExit");Presented(owner,request);
    if(cancellation=="revert")Check(Dispatch(owner,"revert",{{"request",request}}),"Revert cancels pending exit");
    else if(cancellation=="cancel")Check(Dispatch(owner,"cancel"),"Cancel cancels pending exit");
    else if(cancellation=="close")UI_SettingsCloseOwner(owner);
    else if(cancellation=="release")UI_SettingsReleaseOwner(owner);
    else if(cancellation=="failure") {++deviceData.observation.failures;UI_SettingsFrame(false);}
    else Check(false,"unknown display restoration cancellation");
    NoExit(owner);FinishDisplayRestore(owner);
    Check(host.live==Initial(),"Revert/close restores original values without authorizing exit");
    if(cancellation=="revert" || cancellation=="cancel") {
        Expect(owner,"open",true);Expect(owner,"dirty",false);
        Check(Dispatch(owner,"apply"),"later ordinary no-op apply stays open");NoExit(owner);
    }
}
static void ExitDisplayRetry(const std::string& failure) {
    const auto owner=Begin();const auto request=AwaitDisplay(owner,"applyExit");Presented(owner,request);
    Check(Dispatch(owner,"confirm",{{"request",request}}),"Keep before durable failure");
    if(failure=="persist")deviceData.refusePersist=true;
    else if(failure=="config")files.commitFailure=true;
    else if(failure=="finish")deviceData.refuseFinish=true;
    else Check(false,"unknown Keep failure");
    UI_SettingsFrame();NoExit(owner);
    Expect(owner,"canRetry",true);Expect(owner,"open",true);
    if(failure=="finish")Expect(owner,"dirty",false); // Clean state still has an unresolved journal.
    const auto retry=std::get<std::string>(Read(owner).at("settings.request"));
    Check(retry!=request,"confirmation preparation renews request before I/O");
    Check(!Dispatch(owner,"retry",{{"request",request}}),"stale retry cannot mutate current recovery");
    deviceData.refusePersist=deviceData.refuseFinish=files.commitFailure=false;
    Check(Dispatch(owner,"retry",{{"request",retry}}),"explicit Retry completes durable recovery");
    UI_SettingsFrame();NoExit(owner);Expect(owner,"open",true);Expect(owner,"dirty",false);
    Check(!deviceData.held && !UI_SettingsBlocksConfigWrite(),"recovered Keep is retained but canceled exit remains canceled");
    Check(Dispatch(owner,"applyExit") && UI_SettingsConsumeExit(owner),"fresh explicit apply-exit can close recovered clean transaction");NoExit(owner);
}
static void ExitDisplayApplyFailure(const std::string& failure) {
    const auto owner=Begin();UI_SettingsConfirmationDocument(owner,ConfirmationDocument());
    Check(Dispatch(owner,"edit",{{"r_mode",1.0},{"r_brightness",1.5}}) && Dispatch(owner,"applyExit"),"freeze display exit before execution");
    const auto request=std::get<std::string>(Read(owner).at("settings.request"));
    if(failure=="prepare")deviceData.refusePrepare=true;
    else if(failure=="partial")host.partialWrite=true;
    else if(failure=="restart")deviceData.refuseRestart=true;
    else if(failure=="close")UI_SettingsCloseOwner(owner);
    else if(failure=="release")UI_SettingsReleaseOwner(owner);
    else Check(false,"unknown queued display failure");
    UI_SettingsFrame();NoExit(owner);
    deviceData.refusePrepare=deviceData.refuseRestart=false;
    if(failure=="prepare")Check(Dispatch(owner,"retry",{{"request",request}}),"retry cancels uncertain unwritten preparation");
    FinishDisplayRestore(owner);Check(host.live==Initial(),"failed display Apply leaves baseline restored");
    if(failure=="close" || failure=="release")Check(host.writes.empty() && deviceData.restarts==0,"unwritten owner close only cancels queued attempt");
}
int main(int argc,char** argv) {
    Check(argc==2,"scenario required");const std::string name=argv[1];
    if(name=="validation")Validation();else if(name=="ownership")Ownership();
    else if(name=="drafts")Drafts();else if(name=="devices")Devices();
    else if(name=="conflict")Conflict();else if(name=="apply_failure")ApplyFailure();
    else if(name=="confirmation")Confirmation();else if(name=="abandon_editing")AbandonEditing();
    else if(name=="abandon_pending")AbandonPending();else if(name=="orphan_refusal")Orphan(false);
    else if(name=="orphan_divergence")Orphan(true);else if(name=="waiting_close")Waiting(false);
    else if(name=="waiting_release")Waiting(true);else if(name=="persistence")Persistence();
    else if(name=="timeout_frame")TimeoutFrame();else if(name=="capability")ConfirmationCapability();
    else if(name=="display_keep")DisplayDelivery(false);else if(name=="display_persist_failure")DisplayDelivery(true);
    else if(name=="display_close_queued")DisplayClose(false);else if(name=="display_close_written")DisplayClose(true);
    else if(name=="startup_shutdown")StartupShutdown();else if(name=="stale_display_actions")StaleDisplayActions();
    else if(name=="frame_trace")FrameReceiptTrace();
    else if(name=="exit_immediate")ExitImmediate(false);else if(name=="exit_noop")ExitImmediate(true);
    else if(name.starts_with("exit_invalidate_"))ExitInvalidate(name.substr(16));
    else if(name.starts_with("exit_failure_"))ExitFailure(name.substr(13));
    else if(name=="exit_display_keep")ExitDisplayKeep("");
    else if(name.starts_with("exit_display_keep_"))ExitDisplayKeep(name.substr(18));
    else if(name.starts_with("exit_display_restore_"))ExitDisplayRestore(name.substr(21));
    else if(name.starts_with("exit_display_retry_"))ExitDisplayRetry(name.substr(19));
    else if(name.starts_with("exit_display_apply_"))ExitDisplayApplyFailure(name.substr(19));
    else if(name.starts_with("frame_identity_"))FrameReceiptIdentity(name.substr(15));
    else if(name.starts_with("frame_"))FrameReceipt(name.substr(6));
    else Check(false,"unknown scenario");
    std::printf("settings service: %s passed\n",name.c_str());
}
'''

SCENARIOS = (
    'validation', 'ownership', 'drafts', 'devices', 'conflict', 'apply_failure',
    'confirmation', 'abandon_editing', 'abandon_pending', 'orphan_refusal',
    'orphan_divergence', 'waiting_close', 'waiting_release', 'persistence',
    'timeout_frame', 'capability', 'display_keep', 'display_persist_failure',
    'display_close_queued', 'display_close_written', 'startup_shutdown', 'stale_display_actions',
    'frame_outside','frame_skipped','frame_submit_only','frame_present_only',
    'frame_readback_before_draw','frame_readback_after_draw','frame_readback_during_end',
    'frame_nested','frame_aborted','frame_begin_aborted','frame_wrong_request','frame_shutdown',
    'frame_draw_aborted','frame_wrong_owner','frame_revoked','frame_trace',
    'frame_historical_offset','frame_identity_epoch','frame_identity_generation','frame_identity_failure',
    'frame_identity_not_ready','frame_identity_counter_regression',
    'exit_immediate','exit_noop',
    'exit_invalidate_begin','exit_invalidate_other_begin','exit_invalidate_close','exit_invalidate_release',
    'exit_invalidate_shutdown','exit_invalidate_invalid','exit_invalidate_edit','exit_invalidate_cancel','exit_invalidate_revert',
    'exit_failure_unsupported','exit_failure_no_view','exit_failure_write','exit_failure_partial',
    'exit_failure_read','exit_failure_conflict','exit_failure_confirmation',
    'exit_display_keep','exit_display_keep_stale','exit_display_keep_invalid','exit_display_keep_edit',
    'exit_display_restore_revert','exit_display_restore_cancel','exit_display_restore_close','exit_display_restore_release',
    'exit_display_restore_failure','exit_display_retry_persist','exit_display_retry_config','exit_display_retry_finish',
    'exit_display_apply_prepare','exit_display_apply_partial','exit_display_apply_restart',
    'exit_display_apply_close','exit_display_apply_release',
)


def without_includes(source):
    return '\n'.join(line for line in source.splitlines() if not line.startswith('#include '))


def persistence_bodies():
    source = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    explicit = function_body(source, 'void idCommonLocal::WriteConfigToFile(')
    automatic = function_body(source, 'void idCommonLocal::WriteConfiguration(')
    frame = function_body(source, 'void idCommonLocal::Frame(')
    guard = 'if ( UI_SettingsBlocksConfigWrite() ) return;'
    checked = function_body(source, 'bool idCommonLocal::WriteConfigToFileChecked(')
    require_order(explicit, guard, 'WriteConfigToFileChecked(', 'explicit config guard')
    require_order(automatic, guard, 'ClearModifiedFlags(', 'automatic archive dirty guard')
    require_order(automatic, guard, 'WriteConfigToFileChecked(', 'automatic config guard')
    require_order(automatic, 'WriteConfigToFileChecked(', 'ClearModifiedFlags(', 'archive dirty retained until checked success')
    require_order(checked, 'lease.TryAcquire(', 'DurableReadExact(', 'lease held while inspecting journal')
    require_order(checked, 'DurableReadExact(', 'DurableReplaceExact(', 'journal exclusion before config commit')
    require_order(frame, 'UI_SettingsFrame();', 'WriteConfiguration();', 'frame recovery before persistence')
    return explicit + '\n' + automatic


def render_frame_body():
    session = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    body = function_body(session, 'void idSessionLocal::UpdateScreen(')
    common = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    # Common's standalone splash frame is also bracketed, so it cannot borrow
    # an owner marker from an earlier Session frame or nested loading callback.
    splash = common[common.index('UI_SettingsRenderFrame settingsFrame;'):common.index('UI_SettingsRenderFrame settingsFrame;') + 7000]
    splash = splash[:splash.index('UI_SettingsFrame( false );')]
    for label, source in (('Session frame',body),('Common splash frame',splash)):
        require_order(source,'UI_SettingsRenderFrame settingsFrame;','renderSystem->BeginFrame(',label+' scope before renderer begin')
        require_order(source,'renderSystem->BeginFrame(','settingsFrame.Submitting();',label+' pre-submit observation')
        require_order(source,'settingsFrame.Submitting();','renderSystem->EndFrame(',label+' exact EndFrame bracket')
        require_order(source,'renderSystem->EndFrame(','settingsFrame.Presented();',label+' receipt after synchronous end')
        require_order(source,'settingsFrame.Presented();','RetainedUI_FrameSubmitted();',label+' receipt before unrelated callbacks')
    # This engine-only receipt relies explicitly on today's synchronous backend
    # dispatch. If it becomes asynchronous these guards require a frame-tag API.
    renderer = (ROOT / 'src/renderer/RenderSystem.cpp').read_text(encoding='utf-8')
    issue = function_body(renderer,'static void R_IssueRenderCommands(')
    require_order(issue,'RB_ExecuteBackEndCommands( frameData->cmdHead );','R_ClearCommandChain();','synchronous backend consumption')
    end = function_body(renderer,'void idRenderSystemLocal::EndFrame(')
    require_order(end,'cmd->commandId = RC_SWAP_BUFFERS;','R_IssueRenderCommands();','swap belongs to exact EndFrame batch')
    vk = function_body((ROOT/'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text(encoding='utf-8'),'static bool VK_GuiExecutor_SubmitFrame( bool present ) {')
    require_order(vk,'vkQueueSubmit2(','R_DisplayPresentationSubmitted();','Vulkan successful submission counter')
    require_order(vk,'R_DisplayPresentationSubmitted();','vkQueuePresentKHR(','Vulkan same-stack present')
    require_order(vk,'vkQueuePresentKHR(','R_DisplayPresentationPresented();','Vulkan accepted API present counter')
    gl = function_body((ROOT/'src/renderer/OpenGL/gl_ContextSDL3.cpp').read_text(encoding='utf-8'),'void GLimp_SwapBuffers(')
    require_order(gl,'s_glWindowServices->SwapGLWindow()','R_DisplayPresentationSubmitted(); R_DisplayPresentationPresented();','GL accepted synchronous swap counters')
    return body


def main():
    service = without_includes((ROOT / 'src/ui/SettingsService.cpp').read_text(encoding='utf-8'))
    document = (ROOT / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    validation = '\n'.join(function_body(document, name) for name in (
        'bool Utf8(', 'bool ValidStateValue(', 'const Node* Find(', 'const Node* DocumentModel::FindNode('))
    code = (SUPPORT + '\nnamespace openq4::ui {\n' + validation + '\n}\n' +
            service + '\n' + persistence_bodies() + '\n' + render_frame_body() + MAIN)
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-settings-service-', dir=ROOT / '.tmp') as directory:
        source = Path(directory) / 'service.cpp'
        binary = Path(directory) / 'service.exe'
        source.write_text(code, encoding='utf-8')
        environment = dict(os.environ, TEMP=directory, TMP=directory, TMPDIR=directory)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(source),
                        str(ROOT / 'src/ui/application/SettingsTransaction.cpp'),
                        str(ROOT / 'src/ui/application/SettingsDisplayController.cpp'), '-o', str(binary)], check=True, env=environment)
        for scenario in SCENARIOS:
            subprocess.run([str(binary), scenario], check=True)
        dedicated = Path(directory) / 'dedicated.cpp'
        dedicated.write_text('#define ID_DEDICATED\n#include <cassert>\n#include "src/ui/SettingsService.h"\n' +
                             service + '\nint main() { assert(UI_SettingsCreateOwner()==0); '
                             'UI_SettingsCloseOwner(1); UI_SettingsReleaseOwner(1); UI_SettingsFrame(); '
                             'assert(!UI_SettingsExitReady(1) && !UI_SettingsConsumeExit(1)); '
                             'assert(!UI_SettingsBlocksConfigWrite()); }\n', encoding='utf-8')
        dedicated_binary = Path(directory) / 'dedicated.exe'
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(dedicated), '-o', str(dedicated_binary)], check=True, env=environment)
        subprocess.run([str(dedicated_binary)], check=True)
        print(f'settings service: {len(SCENARIOS)} production-body scenarios, dedicated stubs and config/frame source guards passed', flush=True)


if __name__ == '__main__':
    main()
