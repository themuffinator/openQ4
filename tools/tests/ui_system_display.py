#!/usr/bin/env python3
"""Compile production display builders/recovery codec against bounded host doubles.

No engine, window, input or device operation runs. This verifies typed snapshots,
actual-state matching and conservative descriptor resolution, not EDID identity.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body
import ui_system_settings_host as host_test

ROOT = Path(__file__).resolve().parents[2]
EXTRA = r'''
#include <set>
#include "src/ui/application/SystemDisplay.h"
#if defined(USE_SDL3)
struct SDL_Rect { int x,y,w,h; };
static bool displayBoundsFail=false;
static const char* videoDriver="windows";
static const char* SDL_GetCurrentVideoDriver() { return videoDriver; }
static const char* SDL_GetDisplayName(unsigned id) { return id==1?"Primary monitor":"Secondary monitor"; }
static bool SDL_GetDisplayBounds(unsigned id,SDL_Rect* value) {
    if(displayBoundsFail)return false;*value={int(id-1)*1920,0,1920,1080};return true;
}
#endif
'''
MAIN = r'''
static SystemDisplayTopology Topo() {
    SystemDisplayTopology t;t.primary=1;t.absolutePlacement=true;
    for(unsigned id:{1,2}) {
        SystemDisplayDescriptor d;d.id=id;d.name=id==1?"Primary monitor":"Secondary monitor";
        d.x=int(id-1)*1920;d.width=1920;d.height=1080;d.desktop={1920,1080,60};
        d.modes={{1280,720,double(59.94f)},{1920,1080,60},{3840,2160,double(59.94f)},{3840,2160,144}};
        t.displays.push_back(d);
    }return t;
}
static rendererDisplayState_t Actual() {
    rendererDisplayState_t s{};s.moduleEpoch=9;s.rendererReady=s.windowValid=true;s.videoRestartCount=5;
    s.presentation.generation=7;s.presentation.available=1;s.presentation.outcome=2;s.presentation.parametersValid=3;
    s.presentation.samples=0;s.presentation.swapInterval=1;s.presentation.submittedSequence=s.presentation.presentedSequence=10;
    auto& w=s.window;w.displayId=2;w.displayIndex=1;w.windowX=1960;w.windowY=50;w.positionValid=true;
    w.logicalWidth=w.pixelWidth=1280;w.logicalHeight=w.pixelHeight=720;
    w.currentModeValid=true;w.modeWidth=w.modePixelWidth=1920;w.modeHeight=w.modePixelHeight=1080;
    w.refreshRate=60;w.displayScale=w.pixelDensityX=w.pixelDensityY=w.modePixelDensity=1;return s;
}
static rendererDisplayState_t Observed(const SystemDisplayPlan& p) {
    auto s=Actual();s.window=p.expected;
    s.window.displayScale=s.window.pixelDensityX=s.window.pixelDensityY=s.window.modePixelDensity=1;
    if(!p.checkPixels) {s.window.pixelWidth=s.window.logicalWidth;s.window.pixelHeight=s.window.logicalHeight;}
    s.presentation.samples=p.request.parms.multiSamples;s.presentation.swapInterval=p.request.swapInterval;return s;
}
static void Portable(const SystemDisplayPlan& plan,const SystemDisplayTopology& topology) {
    StateValues saved;std::string error;assert(CaptureDisplayRecovery(plan,topology,saved,error));
    for(const auto& [key,value]:saved)assert(key!="displayId" && key!="epoch" && key!="owner" && key!="generation" && key!="displayIndex");
    SystemDisplayPlan inspected;SystemDisplayTopology historical;
    assert(InspectDisplayRecovery(saved,inspected,historical,error));
    assert(inspected.request.displayId==1 && historical.primary==1 && historical.displays.size()==plan.monitors.size());
    for(size_t i=0;i<historical.displays.size();++i)assert(historical.displays[i].id==i+1);
    StateValues inspectedSaved;assert(CaptureDisplayRecovery(inspected,historical,inspectedSaved,error) && inspectedSaved==saved);
    assert(historical.displays.front().modes.size()==(plan.request.parms.fullScreen&&!plan.request.fullscreenDesktop?1u:0u));
    auto fresh=topology;fresh.primary+=20;for(auto& d:fresh.displays)d.id+=20;
    std::reverse(fresh.displays.begin(),fresh.displays.end());
    SystemDisplayPlan restored;assert(ResolveDisplayRecovery(saved,fresh,restored,error));
    assert(restored.request.displayId==plan.request.displayId+20);
    StateValues again;assert(CaptureDisplayRecovery(restored,fresh,again,error) && saved==again);
    auto actual=Observed(restored);assert(MatchesDisplay(restored,actual,error));
    for(auto mutation:{0,1,2,3,4,5,6,7,8,9}) {
        auto bad=saved;
        if(mutation==0)bad["unknown"]=true;
        if(mutation==1)bad.erase("width");
        if(mutation==2)bad["width"]=std::string("960");
        if(mutation==3)bad["version"]=2.0;
        if(mutation==4)bad["width"]=1.5;
        if(mutation==5)bad["width"]=std::numeric_limits<double>::infinity();
        if(mutation==6)bad["monitor.0.name"]=std::string(513,'x');
        if(mutation==7)bad["check.mode"]=false;
        if(mutation==8)bad["actual.refresh"]=90.0;
        if(mutation==9)bad["monitor.count"]=0.0;
        restored.request.parms.width=777;
        assert(!ResolveDisplayRecovery(bad,fresh,restored,error) && restored.request.parms.width==777);
    }
    auto missing=fresh;missing.displays.erase(std::remove_if(missing.displays.begin(),missing.displays.end(),[&](const auto& d){return d.id==plan.request.displayId+20;}),missing.displays.end());
    assert(!ResolveDisplayRecovery(saved,missing,restored,error));
    auto renamed=fresh;for(auto& d:renamed.displays)if(d.id==plan.request.displayId+20)d.name+=" changed";
    assert(!ResolveDisplayRecovery(saved,renamed,restored,error));
    auto moved=fresh;for(auto& d:moved.displays)if(d.id==plan.request.displayId+20)++d.x;
    assert(!ResolveDisplayRecovery(saved,moved,restored,error));
    auto duplicate=topology;auto copied=plan.monitors.front();copied.id=99;duplicate.displays.push_back(copied);
    assert(!ResolveDisplayRecovery(saved,duplicate,restored,error));
}
static void RecoveryPairs(const StateValues& values,const rendererDisplayState_t& actual,const SystemDisplayTopology& topology) {
    std::string error;SystemDisplayPlan restore,target,resolved;StateValues savedRestore,savedTarget;
    assert(BuildDisplayRestore(actual,topology,restore,error) && CaptureDisplayRecovery(restore,topology,savedRestore,error));
    auto candidate=values;candidate["r_screen"]=0.0;candidate["r_windowWidth"]=960.0;
    assert(BuildDisplayRequest(candidate,actual,topology,target,error) && CaptureDisplayRecovery(target,topology,savedTarget,error));
    assert(ValidateDisplayRecoveryPair(savedRestore,savedTarget,candidate,error));

    // The unused target may disappear before a Pending startup restores the
    // original display. A Confirmed startup analogously needs only its target.
    auto pending=topology;pending.displays.erase(pending.displays.begin());pending.primary=2;
    auto confirmed=topology;confirmed.displays.pop_back();confirmed.primary=1;
    assert(ResolveDisplayRecovery(savedRestore,pending,resolved,error));
    assert(!ResolveDisplayRecovery(savedTarget,pending,resolved,error));
    assert(ResolveDisplayRecovery(savedTarget,confirmed,resolved,error));
    assert(!ResolveDisplayRecovery(savedRestore,confirmed,resolved,error));
    assert(ValidateDisplayRecoveryPair(savedRestore,savedTarget,candidate,error));
    auto remapped=confirmed;remapped.displays[0].id=80;remapped.primary=80;
    assert(ResolveDisplayRecovery(savedTarget,remapped,resolved,error) && resolved.request.displayId==80);

    for(int defect=0;defect<11;++defect) {
        auto corrupted=savedTarget;
        if(defect==0)corrupted["unknown"]=true;
        if(defect==1)corrupted.erase("actual.modeValid");
        if(defect==2)corrupted["width"]=true;
        if(defect==3)corrupted["actual.refresh"]=std::numeric_limits<double>::quiet_NaN();
        if(defect==4)corrupted["monitor.0.width"]=0.0;
        if(defect==5)corrupted["monitor.0.refresh"]=-1.0;
        if(defect==6)corrupted["monitor.0.refresh"]=1001.0;
        if(defect==7)corrupted["monitor.0.name"]=std::string(513,'x');
        if(defect==8)corrupted["check.mode"]=false;
        if(defect==9)corrupted["actual.logicalWidth"]=961.0;
        if(defect==10)corrupted["monitor.0.modeWidth"]=0.0;
        SystemDisplayPlan untouched;untouched.request.parms.width=777;
        SystemDisplayTopology oldTopology;oldTopology.primary=999;
        assert(!InspectDisplayRecovery(corrupted,untouched,oldTopology,error));
        assert(untouched.request.parms.width==777 && oldTopology.primary==999 && oldTopology.displays.empty());
        assert(!ValidateDisplayRecoveryPair(savedRestore,corrupted,candidate,error));
        assert(!ValidateDisplayRecoveryPair(corrupted,savedTarget,candidate,error));
    }
    auto inconsistent=candidate;inconsistent["r_windowWidth"]=1000.0;
    assert(!ValidateDisplayRecoveryPair(savedRestore,savedTarget,inconsistent,error));
    inconsistent=candidate;inconsistent["r_screen"]=-1.0;
    assert(!ValidateDisplayRecoveryPair(savedRestore,savedTarget,inconsistent,error)); // Auto would retain the original secondary.
    inconsistent=candidate;inconsistent["r_screen"]=32.0;
    assert(!ValidateDisplayRecoveryPair(savedRestore,savedTarget,inconsistent,error));
    inconsistent=candidate;inconsistent.erase("r_mode");assert(!ValidateDisplayRecoveryPair(savedRestore,savedTarget,inconsistent,error));

    // An exclusive record proves only its historical mode; current mode support
    // remains mandatory if this is the selected recovery direction.
    candidate["r_fullscreen"]=true;candidate["r_fullscreenDesktop"]=false;candidate["r_mode"]=-1.0;
    candidate["r_customWidth"]=1280.0;candidate["r_customHeight"]=720.0;candidate["r_displayRefresh"]=60.0;
    assert(BuildDisplayRequest(candidate,actual,topology,target,error) && CaptureDisplayRecovery(target,topology,savedTarget,error));
    assert(ValidateDisplayRecoveryPair(savedRestore,savedTarget,candidate,error));
    SystemDisplayTopology historical;SystemDisplayPlan inspected;
    assert(InspectDisplayRecovery(savedTarget,inspected,historical,error));
    assert(historical.displays[0].modes.size()==1 && historical.displays[0].modes[0].refresh==double(59.94f));
    auto missingMode=confirmed;missingMode.displays[0].modes.clear();
    assert(!ResolveDisplayRecovery(savedTarget,missingMode,resolved,error));
    assert(ResolveDisplayRecovery(savedRestore,pending,resolved,error));
    auto corruptMode=savedTarget;corruptMode["actual.modeWidth"]=1920.0;
    assert(!InspectDisplayRecovery(corruptMode,inspected,historical,error));
    corruptMode=savedTarget;corruptMode["actual.refresh"]=90.0;
    assert(!InspectDisplayRecovery(corruptMode,inspected,historical,error));

    // Pair reconstruction must also use recorded baseline exclusive modes,
    // including when Auto chooses a different mode on that same monitor.
    auto exclusive=actual;exclusive.window.fullscreen=true;exclusive.window.fullscreenDesktop=false;
    exclusive.window.modePixelWidth=1280;exclusive.window.modePixelHeight=720;exclusive.window.refreshRate=59.94f;
    assert(BuildDisplayRestore(exclusive,topology,restore,error) && CaptureDisplayRecovery(restore,topology,savedRestore,error));
    candidate["r_screen"]=-1.0;candidate["r_customWidth"]=3840.0;candidate["r_customHeight"]=2160.0;candidate["r_displayRefresh"]=0.0;
    assert(BuildDisplayRequest(candidate,exclusive,topology,target,error) && CaptureDisplayRecovery(target,topology,savedTarget,error));
    assert(ValidateDisplayRecoveryPair(savedRestore,savedTarget,candidate,error));
    assert(InspectDisplayRecovery(savedRestore,inspected,historical,error) && historical.displays[0].modes[0].width==1280);

    // Spans store their complete historical membership. Self-consistent records
    // that disagree with the other side's monitor set cannot pass pair checks.
    assert(BuildDisplayRestore(actual,topology,restore,error) && CaptureDisplayRecovery(restore,topology,savedRestore,error));
    candidate=values;candidate["r_screen"]=0.0;candidate["r_fullscreen"]=false;candidate["r_borderless"]=true;candidate["r_multiScreen"]=1.0;
    assert(BuildDisplayRequest(candidate,actual,topology,target,error) && CaptureDisplayRecovery(target,topology,savedTarget,error));
    assert(ValidateDisplayRecoveryPair(savedRestore,savedTarget,candidate,error));
    assert(!ResolveDisplayRecovery(savedTarget,confirmed,resolved,error) && ResolveDisplayRecovery(savedRestore,pending,resolved,error));
    auto incomplete=savedTarget;
    for(auto it=incomplete.begin();it!=incomplete.end();)if(it->first.rfind("monitor.1.",0)==0)it=incomplete.erase(it);else ++it;
    incomplete["monitor.count"]=1.0;incomplete["width"]=1920.0;incomplete["actual.logicalWidth"]=1920.0;
    assert(InspectDisplayRecovery(incomplete,inspected,historical,error));
    assert(!ValidateDisplayRecoveryPair(savedRestore,incomplete,candidate,error));
    auto differentCapability=savedRestore;differentCapability["absolutePlacement"]=false;differentCapability["check.position"]=false;differentCapability["placement"]=false;
    assert(InspectDisplayRecovery(differentCapability,inspected,historical,error));
    assert(!ValidateDisplayRecoveryPair(differentCapability,savedTarget,candidate,error));
    assert(writes==0);
#if defined(USE_SDL3)
    assert(refreshCalls==0);
#endif
}
int main() {
    Seed();SystemSettingsHost host;StateValues values;std::string error;assert(host.Read(values,error));
    values["r_swapInterval"]=1.0;auto topology=Topo();auto actual=Actual();SystemDisplayPlan plan;
    RecoveryPairs(values,actual,topology);
    assert(BuildDisplayRequest(values,actual,topology,plan,error));
    assert(plan.request.displayId==2 && plan.request.parms.width==1280 && plan.request.parms.height==720);
    assert(plan.request.restorePlacement && plan.request.windowX==1960 && plan.request.windowY==50 && !plan.request.maximized);
    assert(MatchesDisplay(plan,Observed(plan),error));Portable(plan,topology);
    auto changed=values;changed["r_brightness"]=1.5;changed["r_windowWidth"]=960.0;changed["s_useEAXReverb"]=true;
    assert(host.ChangedEffects(values,changed)==(SystemSettingDisplayRestart|SystemSettingAudioRestart));
    assert(host.ChangedEffects(values,values)==0);
    changed=values;changed["r_screen"]=0.0;assert(BuildDisplayRequest(changed,actual,topology,plan,error));
    assert(plan.request.displayId==1 && plan.request.windowX==320 && plan.request.windowY==180);
    auto reordered=topology;std::reverse(reordered.displays.begin(),reordered.displays.end());
    assert(BuildDisplayRequest(values,actual,reordered,plan,error) && plan.request.displayId==2 && plan.request.displayIndex==0);
    actual.window.displayId=88;assert(BuildDisplayRequest(values,actual,topology,plan,error) && plan.request.displayId==1);actual=Actual();
    auto invalid=values;invalid.erase("r_mode");plan.request.parms.width=777;assert(!BuildDisplayRequest(invalid,actual,topology,plan,error) && plan.request.parms.width==777);
    invalid=values;invalid["r_screen"]=8.0;assert(!BuildDisplayRequest(invalid,actual,topology,plan,error));
    invalid=values;invalid["r_multiSamples"]=3.0;assert(!BuildDisplayRequest(invalid,actual,topology,plan,error));
    invalid=values;invalid["r_windowWidth"]=1.25;assert(!BuildDisplayRequest(invalid,actual,topology,plan,error));
    auto unavailable=actual;unavailable.windowValid=false;assert(!BuildDisplayRequest(values,unavailable,topology,plan,error));
    auto wayland=topology;wayland.absolutePlacement=false;assert(BuildDisplayRequest(values,actual,wayland,plan,error) && !plan.checkPosition && !plan.request.restorePlacement);
    Portable(plan,wayland);
    changed=values;changed["r_fullscreen"]=true;changed["r_fullscreenDesktop"]=false;changed["r_mode"]=-1.0;
    changed["r_customWidth"]=3840.0;changed["r_customHeight"]=2160.0;changed["r_displayRefresh"]=60.0;
    assert(BuildDisplayRequest(changed,actual,topology,plan,error) && plan.checkPixels && plan.expected.refreshRate==59.94f);
    auto high=Observed(plan);high.window.logicalWidth=1920;high.window.logicalHeight=1080;assert(MatchesDisplay(plan,high,error));Portable(plan,topology);
    changed["r_displayRefresh"]=0.0;assert(BuildDisplayRequest(changed,actual,topology,plan,error) && plan.expected.refreshRate==144);
    changed["r_displayRefresh"]=75.0;assert(!BuildDisplayRequest(changed,actual,topology,plan,error));
    auto dense=topology;dense.displays[1].modes={{3840,2160,60}};
    changed["r_displayRefresh"]=60.0;changed["r_customWidth"]=1920.0;changed["r_customHeight"]=1080.0;
    assert(!BuildDisplayRequest(changed,actual,dense,plan,error));
    changed["r_mode"]=-2.0;dense.displays[1].desktop={3840,2160,60};assert(BuildDisplayRequest(changed,actual,dense,plan,error) && plan.request.parms.width==3840);
    changed["r_fullscreenDesktop"]=true;assert(BuildDisplayRequest(changed,actual,topology,plan,error) && plan.expected.fullscreenDesktop);Portable(plan,topology);
    changed["r_multiScreen"]=1.0;assert(BuildDisplayRequest(changed,actual,topology,plan,error));
    assert(plan.request.spanDisplays && !plan.expected.fullscreen && plan.expected.borderless && plan.expected.logicalWidth==3840 && plan.expected.windowX==0);
    Portable(plan,topology);assert(!BuildDisplayRequest(changed,actual,wayland,plan,error));
    changed=values;changed["r_borderless"]=true;assert(BuildDisplayRequest(changed,actual,topology,plan,error) && plan.expected.logicalWidth==1920);Portable(plan,topology);
    actual=Actual();actual.window.maximized=true;assert(BuildDisplayRestore(actual,topology,plan,error) && plan.request.maximized);Portable(plan,topology);
    actual=Actual();assert(BuildDisplayRestore(actual,topology,plan,error));
    assert(plan.checkPixels && plan.request.restorePlacement && plan.request.windowX==actual.window.windowX);
    auto observed=actual;assert(MatchesDisplay(plan,observed,error));Portable(plan,topology);
    for(int mutation=0;mutation<12;++mutation) {
        observed=actual;
        if(mutation==0)observed.window.logicalWidth++;
        if(mutation==1)observed.window.pixelWidth++;
        if(mutation==2)observed.window.displayId=1;
        if(mutation==3)observed.window.windowX++;
        if(mutation==4)observed.window.maximized=true;
        if(mutation==5)observed.window.fullscreen=true;
        if(mutation==6)observed.presentation.samples=4;
        if(mutation==7)observed.presentation.swapInterval=0;
        if(mutation==8)observed.presentation.parametersValid=0;
        if(mutation==9)observed.window.refreshRate=144;
        if(mutation==10)observed.window.modePixelWidth++;
        if(mutation==11)observed.window.minimized=true;
        assert(!MatchesDisplay(plan,observed,error));
    }
    actual.window.borderless=true;actual.window.windowX=0;actual.window.windowY=0;actual.window.logicalWidth=actual.window.pixelWidth=3840;actual.window.logicalHeight=actual.window.pixelHeight=1080;
    assert(BuildDisplayRestore(actual,topology,plan,error) && plan.request.spanDisplays);Portable(plan,topology);
    actual.window.logicalWidth=3000;assert(!BuildDisplayRestore(actual,topology,plan,error));
    assert(writes==0);
#if defined(USE_SDL3)
    SystemDisplayTopology captured;assert(CaptureDisplayTopology(captured,error) && captured.displays.size()==2 && captured.absolutePlacement);
    videoDriver="wayland";assert(CaptureDisplayTopology(captured,error) && !captured.absolutePlacement);videoDriver="windows";
    displayBoundsFail=true;captured.primary=999;assert(!CaptureDisplayTopology(captured,error) && captured.primary==999);displayBoundsFail=false;
    assert(refreshCalls==0 && writes==0);
#else
    SystemDisplayTopology captured;captured.primary=999;assert(!CaptureDisplayTopology(captured,error) && captured.primary==999);
#endif
    std::puts("SYSTEM display builders: pure pixel/policy requests, actual restore matching, strict portable recovery and topology rejection passed");
}
'''


def main():
    host = (ROOT / 'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    display = (ROOT / 'src/ui/application/SystemDisplay.cpp').read_text(encoding='utf-8')
    cvars = (ROOT / 'src/framework/CVarSystem.cpp').read_text(encoding='utf-8')
    document = (ROOT / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    client = display[display.index('#else'):]
    for name in ('bool BuildDisplayRequest(', 'bool BuildDisplayRestore(', 'bool MatchesDisplay(', 'bool CaptureDisplayRecovery(', 'bool InspectDisplayRecovery(', 'bool ValidateDisplayRecoveryPair(', 'bool ResolveDisplayRecovery('):
        body = function_body(client, name)
        assert 'cvarSystem' not in body and 'SDL_' not in body and 'RefreshNativeWindowHandles' not in body
    validation = '\n'.join(function_body(document, name) for name in ('bool Utf8(', 'bool ValidStateValue('))
    production = '\n'.join(line for line in (host + '\n' + display).splitlines() if not line.startswith('#include '))
    code = (host_test.SUPPORT + EXTRA + function_body(cvars, '\nbool CVar_ReadDefault(') +
            '\nnamespace openq4::ui {\n' + validation + '\n}\n' +
            '\n#define min(a,b) (((a)<(b))?(a):(b))\n#define max(a,b) (((a)>(b))?(a):(b))\n' + production +
            '\n#undef min\n#undef max\n' + MAIN)
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-system-display-', dir=ROOT / '.tmp') as directory:
        temp = Path(directory)
        environment = dict(os.environ, TEMP=str(temp), TMP=str(temp))
        source = temp / 'display.cpp'
        source.write_text(code, encoding='utf-8')
        for sdl in (False, True):
            binary = temp / ('sdl.exe' if sdl else 'plain.exe')
            flags = ['-DUSE_SDL3'] if sdl else []
            subprocess.run([compiler, '-std=c++20', *flags, '-I', str(ROOT), str(source),
                            str(ROOT / 'src/ui/retained/Presentation.cpp'), '-o', str(binary)], check=True, env=environment)
            subprocess.run([str(binary)], check=True, env=environment)
        source.write_text('#define ID_DEDICATED\n#include "src/ui/application/SystemDisplay.h"\n#include <cassert>\n'+
                          '\n'.join(line for line in display.splitlines() if not line.startswith('#include '))+
                          '\nint main(){openq4::ui::SystemDisplayTopology t;t.primary=9;std::string e;'
                          'openq4::ui::SystemDisplayPlan p;p.request.parms.width=777;openq4::ui::StateValues v;'
                          'assert(!openq4::ui::CaptureDisplayTopology(t,e)&&t.primary==9);'
                          'assert(!openq4::ui::InspectDisplayRecovery(v,p,t,e)&&p.request.parms.width==777&&t.primary==9);'
                          'assert(!openq4::ui::ValidateDisplayRecoveryPair(v,v,v,e));'
                          'assert(!openq4::ui::ResolveDisplayRecovery(v,t,p,e)&&p.request.parms.width==777);}\n', encoding='utf-8')
        binary=temp/'dedicated.exe'
        subprocess.run([compiler,'-std=c++20','-I',str(ROOT),str(source),'-o',str(binary)],check=True,env=environment)
        subprocess.run([str(binary)],check=True,env=environment)


if __name__ == '__main__':
    main()
