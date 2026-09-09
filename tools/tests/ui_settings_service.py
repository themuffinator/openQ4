#!/usr/bin/env python3
"""Exercise production settings service, transaction and config-write guards.

Compiles the real service/transaction and Common persistence function bodies,
with real public headers and value validation. A four-field SystemSettingsHost
and counted engine I/O stand in for the 53-CVar catalog, devices and filesystem.
Forced confirmation models service lifecycle states; it does not qualify any
display/audio restart, recovery journal, rendering, production page or editor.
Each scenario starts a fresh process, preserving the production singleton.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body, require_order

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "src/ui/SettingsService.h"
#include "src/ui/application/SystemSettingsHost.h"
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
bool SystemSettingsHost::NeedsConfirmation(const StateValues&,const StateValues&) const {
    return host.confirm; // Fault-injected service state, never a device result.
}
}
struct Common {
    int warnings=0,prints=0;
    void Warning(const char*,...) { ++warnings; }
    void Printf(const char*,...) { ++prints; }
} commonObject,*common=&commonObject;
enum { CVAR_ARCHIVE=1 };
struct idFile {} file;
struct Files {
    int opens=0,closes=0;
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
    void WriteConfigToFile(const char* filename);
    void WriteConfiguration();
    void Printf(const char*,...) {}
} commonLocal;
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
    Check(values.size()==6,"nonowner gets status only, no catalog snapshots");
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
    Check(schema.size()==14 && schema.at("settings.message")==2 && schema.at("settings.open")==1 &&
          schema.at("settings.phase")==0,"service status schema types");
    for(const auto& [key,type]:SystemSettingsHost::Schema())
        Check(schema.at("settings.draft."+key)==type && schema.at("settings.baseline."+key)==type,"typed snapshot schema");
    std::string error;
    for(const char* operation:{"begin","defaults","cancel","apply","confirm","revert"}) {
        auto invocation=Invocation(operation);Check(UI_SettingsInvocation(invocation,error),"known no-argument operation");
        invocation.arguments={{"r_brightness",1.0}};
        Check(!UI_SettingsInvocation(invocation,error),"no-argument operation rejects extras");
    }
    Check(UI_SettingsInvocation(Invocation("edit",Initial()),error),"typed complete edit shape accepted");
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
    for(const char* op:{"begin","edit","defaults","apply","confirm","revert","cancel"}) {
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
    Check(!Dispatch(owner,"edit",{{"r_brightness",1.4}}),"closed edit rejected");
    Expect(owner,"message",std::string("#str_229984"));
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
int main(int argc,char** argv) {
    Check(argc==2,"scenario required");const std::string name=argv[1];
    if(name=="validation")Validation();else if(name=="ownership")Ownership();
    else if(name=="drafts")Drafts();else if(name=="devices")Devices();
    else if(name=="conflict")Conflict();else if(name=="apply_failure")ApplyFailure();
    else if(name=="confirmation")Confirmation();else if(name=="abandon_editing")AbandonEditing();
    else if(name=="abandon_pending")AbandonPending();else if(name=="orphan_refusal")Orphan(false);
    else if(name=="orphan_divergence")Orphan(true);else if(name=="waiting_close")Waiting(false);
    else if(name=="waiting_release")Waiting(true);else if(name=="persistence")Persistence();
    else if(name=="timeout_frame")TimeoutFrame();else Check(false,"unknown scenario");
    std::printf("settings service: %s passed\n",name.c_str());
}
'''

SCENARIOS = (
    'validation', 'ownership', 'drafts', 'devices', 'conflict', 'apply_failure',
    'confirmation', 'abandon_editing', 'abandon_pending', 'orphan_refusal',
    'orphan_divergence', 'waiting_close', 'waiting_release', 'persistence',
    'timeout_frame',
)


def without_includes(source):
    return '\n'.join(line for line in source.splitlines() if not line.startswith('#include '))


def persistence_bodies():
    source = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    explicit = function_body(source, 'void idCommonLocal::WriteConfigToFile(')
    automatic = function_body(source, 'void idCommonLocal::WriteConfiguration(')
    frame = function_body(source, 'void idCommonLocal::Frame(')
    guard = 'if ( UI_SettingsBlocksConfigWrite() ) return;'
    require_order(explicit, guard, 'OpenFileWrite(', 'explicit config guard')
    require_order(automatic, guard, 'ClearModifiedFlags(', 'automatic archive dirty guard')
    require_order(automatic, guard, 'WriteConfigToFile(', 'automatic config guard')
    require_order(frame, 'UI_SettingsFrame();', 'WriteConfiguration();', 'frame recovery before persistence')
    return explicit + '\n' + automatic


def main():
    service = without_includes((ROOT / 'src/ui/SettingsService.cpp').read_text(encoding='utf-8'))
    document = (ROOT / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    validation = '\n'.join(function_body(document, name) for name in ('bool Utf8(', 'bool ValidStateValue('))
    code = (SUPPORT + '\nnamespace openq4::ui {\n' + validation + '\n}\n' +
            service + '\n' + persistence_bodies() + MAIN)
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-settings-service-', dir=ROOT / '.tmp') as directory:
        source = Path(directory) / 'service.cpp'
        binary = Path(directory) / 'service.exe'
        source.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(source),
                        str(ROOT / 'src/ui/application/SettingsTransaction.cpp'), '-o', str(binary)], check=True)
        for scenario in SCENARIOS:
            subprocess.run([str(binary), scenario], check=True)
        dedicated = Path(directory) / 'dedicated.cpp'
        dedicated.write_text('#define ID_DEDICATED\n#include <cassert>\n#include "src/ui/SettingsService.h"\n' +
                             service + '\nint main() { assert(UI_SettingsCreateOwner()==0); '
                             'UI_SettingsCloseOwner(1); UI_SettingsReleaseOwner(1); UI_SettingsFrame(); '
                             'assert(!UI_SettingsBlocksConfigWrite()); }\n', encoding='utf-8')
        dedicated_binary = Path(directory) / 'dedicated.exe'
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(dedicated), '-o', str(dedicated_binary)], check=True)
        subprocess.run([str(dedicated_binary)], check=True)
        print(f'settings service: {len(SCENARIOS)} production-body scenarios, dedicated stubs and config/frame source guards passed', flush=True)


if __name__ == '__main__':
    main()
