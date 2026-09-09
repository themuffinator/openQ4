#!/usr/bin/env python3
"""Compile the authored display confirmation through real canonical UI code.

The source document, expressions and action descriptors are production parsed;
host settings updates are typed stand-ins. No renderer, window or input runs.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/ui'))
from capture_legacy_baseline import interaction_script

FIXTURE = ROOT/'tools/ui/fixtures/display-settings-smoke.q4ui'
SOURCE = r'''
#include "src/ui/retained/Document.h"
#include "src/ui/retained/State.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
using namespace openq4::ui;
static struct {std::set<std::uint64_t> owners{17},confirmationOwners;} service;
static auto& Settings(){return service;}
static const std::map<std::string,std::size_t>& UI_SettingsStateSchema(){
    static const std::map<std::string,std::size_t> schema{{"settings.request",2},{"settings.confirmationVisible",1},
        {"settings.canConfirm",1},{"settings.canRevert",1},{"settings.canRetry",1},{"settings.remaining",0}};
    return schema;
}
// CAPABILITY_FUNCTION
int main(int argc,char** argv) {
    assert(argc==2);std::ifstream file(argv[1],std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)),{});
    Document document;std::vector<Diagnostic> errors;
    if(!document.Load(source,errors)){
        for(const auto& error:errors)std::fprintf(stderr,"%s: %s\n",error.pointer.c_str(),error.message.c_str());
        return 1;
    }
    assert(document.Source()==source && document.Model().id=="display-settings-smoke");
    const auto& model=document.Model();State state;std::string error;
    UI_SettingsConfirmationDocument(17,model);assert(service.confirmationOwners.contains(17));
    UI_SettingsConfirmationDocument(0,model);UI_SettingsConfirmationDocument(23,model);
    assert(service.confirmationOwners==std::set<std::uint64_t>{17});
    for(const char* name:{"keep","revert","retry"}){
        auto changed=model;changed.actions.at(name).arguments.at("request").op="state";
        UI_SettingsConfirmationDocument(17,changed);assert(!service.confirmationOwners.contains(17));
        changed=model;changed.actions.at(name).arguments.at("request").state="settings.message";
        UI_SettingsConfirmationDocument(17,changed);assert(!service.confirmationOwners.contains(17));
        changed=model;changed.actions.at(name).arguments.at("request").presentation="request";
        UI_SettingsConfirmationDocument(17,changed);assert(!service.confirmationOwners.contains(17));
    }
    UI_SettingsConfirmationDocument(17,model);assert(service.confirmationOwners.contains(17));
    assert(state.Reset(model,error));
    auto property=[&](const char* node,const char* key)->const Value& {return state.Properties().at({node,key});};
    auto shown=[&](const char* node){return property(node,"display").text!="none";};
    assert(!shown("confirmation-panel") && !shown("countdown-row"));
    assert(!state.Enabled().at("settings_keep") && !state.Enabled().at("settings_revert") && !state.Enabled().at("settings_retry"));
    for(const auto& [id,operation]:std::map<std::string,std::string>{
        {"settings_keep","settings.system.confirm"},{"settings_revert","settings.system.revert"},{"settings_retry","settings.system.retry"}}){
        const auto* node=model.FindNode(id);assert(node && node->control && !node->control->label.empty());
        const auto& action=model.actions.at(node->control->action);
        assert(action.operation==operation && action.arguments.size()==1);
        const auto& request=action.arguments.at("request");
        assert(request.type==2 && request.op.empty() && request.state=="settings.request" && request.presentation.empty() && request.args.empty());
    }
    // The owner has been redrawn but has not passed the fresh-present gate:
    // Revert is available immediately; Keep and the countdown stay unavailable.
    const std::string request="18446744073709551614";
    assert(state.Set({{"settings.phase",4.0},{"settings.open",true},{"settings.dirty",true},
        {"settings.request",request},{"settings.confirmationVisible",true},{"settings.canRevert",true}},error));
    assert(shown("confirmation-panel") && !shown("countdown-row") && !shown("settings_retry"));
    assert(!state.Enabled().at("settings_keep") && state.Enabled().at("settings_revert"));
    assert(property("settings-panel","pointer-events").text=="none" && property("settings-panel","opacity").data[0]==.2);
    assert(state.Set({{"settings.phase",2.0},{"settings.canConfirm",true},{"settings.remaining",14.5}},error));
    assert(shown("countdown-row") && state.Enabled().at("settings_keep") && property("countdown-value","text").text=="14.5");
    ActionInvocation invocation;
    assert(model.ResolveAction("keep",state.Variables(),invocation,error));
    assert(invocation.operation=="settings.system.confirm" && std::get<std::string>(invocation.arguments.at("request"))==request);
    // An accepted Keep whose durable save failed exposes Retry. Revert cannot
    // claim to undo committed intent, and the old deadline does not keep ticking.
    assert(state.Set({{"settings.phase",3.0},{"settings.canConfirm",false},{"settings.canRevert",false},
        {"settings.canRetry",true},{"settings.remaining",0.0},{"settings.message",std::string("#str_229997")}},error));
    assert(shown("confirmation-panel") && shown("settings_retry") && !shown("settings_keep") && !shown("settings_revert"));
    assert(!shown("countdown-row") && state.Enabled().at("settings_retry") && !state.Enabled().at("settings_revert"));
    assert(property("confirmation-message","text").text=="#str_229997");
    assert(model.ResolveAction("retry",state.Variables(),invocation,error));
    assert(invocation.operation=="settings.system.retry" && std::get<std::string>(invocation.arguments.at("request"))==request);
    assert(state.Set({{"settings.phase",1.0},{"settings.confirmationVisible",false},{"settings.canRetry",false},
        {"settings.request",std::string()},{"settings.draft.r_windowWidth",960.0},{"settings.draft.r_windowHeight",540.0}},error));
    assert(!shown("confirmation-panel") && property("settings-panel","pointer-events").text=="auto");
    assert(property("width-reading","text").text=="960" && property("height-reading","text").text=="540");
    assert(document.Source()==source);
    std::puts("Display settings fixture: real canonical parse, pre-present Revert, gated Keep/countdown, exact request identity, Retry-only persistence recovery and read-only scripts passed");
}
'''


def main():
    data = json.loads('\n'.join(line for line in FIXTURE.read_text(encoding='utf-8').splitlines()
                                if not line.startswith('//')))
    assert data['id'] == 'display-settings-smoke'
    for key in ('request','confirmationVisible','canConfirm','canRevert','canRetry','remaining'):
        assert not data['state']['settings.'+key].get('cvar')
    setup = interaction_script(FIXTURE.with_suffix('.cfg'), managed=True)
    resume = interaction_script(FIXTURE.with_name('display-settings-smoke-resume.cfg'), managed=True, observe_only=True)
    assert len(setup.splitlines()) < 256 and 'event "edit"' in setup and 'event "second"' in setup
    assert setup.count('event "apply"') == 2 and setup.count('menu accept 0') == 2
    assert 'focus "settings_keep"' in setup and 'focus "settings_revert"' in setup
    assert 'event ' not in resume and 'menu ' not in resume and 'state ' not in resume
    compiler = next((found for name in ('clang++','g++','c++') if (found := shutil.which(name))), None)
    if not compiler: raise RuntimeError('C++ compiler required')
    jsoncpp = ROOT/'subprojects/jsoncpp-1.9.6'
    with tempfile.TemporaryDirectory(prefix='display-settings-fixture-',dir=ROOT/'.tmp') as temp:
        env = dict(os.environ,TEMP=temp,TMP=temp)
        service_source = (ROOT/'src/ui/SettingsService.cpp').read_text(encoding='utf-8')
        capability = function_body(service_source,'void UI_SettingsConfirmationDocument(std::uint64_t owner, const DocumentModel& document)')
        source = Path(temp)/'fixture.cpp';source.write_text(SOURCE.replace('// CAPABILITY_FUNCTION',capability),encoding='utf-8')
        binary = Path(temp)/'fixture.exe'
        sources = [ROOT/'src/ui/retained'/name for name in ('Document.cpp','State.cpp','Motion.cpp','Presentation.cpp')]
        sources += [jsoncpp/'src/lib_json'/name for name in ('json_reader.cpp','json_value.cpp','json_writer.cpp')]
        subprocess.run([compiler,'-std=c++20','-I',str(ROOT),'-I',str(jsoncpp/'include'),str(source),
                        *map(str,sources),'-o',str(binary)],check=True,env=env)
        subprocess.run([str(binary),str(FIXTURE)],check=True,env=env)


if __name__ == '__main__':
    main()
