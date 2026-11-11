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
static int boundsWidth=1920,boundsHeight=1080;
static const char* videoDriver="windows";
static const char* SDL_GetCurrentVideoDriver() { return videoDriver; }
static const char* SDL_GetDisplayName(unsigned id) { return id==1?"Primary monitor":"Secondary monitor"; }
static bool SDL_GetDisplayBounds(unsigned id,SDL_Rect* value) {
    if(displayBoundsFail)return false;*value={int(id-1)*1920,0,boundsWidth,boundsHeight};return true;
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
    // A single-sample (Vulkan) target agrees with any catalog count; a window
    // count must still be the catalog's.
    auto sampled=candidate;sampled["r_multiSamples"]=4.0;StateValues vulkanTarget,windowTarget;
    assert(BuildDisplayRequest(sampled,actual,topology,target,error,false) && target.request.parms.multiSamples==0 &&
           CaptureDisplayRecovery(target,topology,vulkanTarget,error) && ValidateDisplayRecoveryPair(savedRestore,vulkanTarget,sampled,error));
    assert(BuildDisplayRequest(sampled,actual,topology,target,error) && CaptureDisplayRecovery(target,topology,windowTarget,error) &&
           ValidateDisplayRecoveryPair(savedRestore,windowTarget,sampled,error));
    auto otherCount=windowTarget;otherCount["samples"]=2.0;assert(!ValidateDisplayRecoveryPair(savedRestore,otherCount,sampled,error));
    assert(!ValidateDisplayRecoveryPair(savedRestore,windowTarget,candidate,error));

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

// The SYSTEM display lists: pure builder and selection patches.
static StateValues DisplayDraft(double screen,double mode,double customWidth,double customHeight,double refresh) {
    return {{"r_screen",screen},{"r_mode",mode},{"r_customWidth",customWidth},{"r_customHeight",customHeight},{"r_displayRefresh",refresh}};
}
static size_t CodePoints(const std::string& text) {
    size_t points=0;for(unsigned char c:text)if((c&0xC0)!=0x80)++points;return points;
}
static void Catalogs() {
    SystemDisplayCatalogInput input;input.topology=Topo();input.currentDisplay=2;std::string error;
    assert(SystemSettingsHost::LegacyModeForSize(1920,1080)==3 && SystemSettingsHost::LegacyModeForSize(640,480)==11 &&
           SystemSettingsHost::LegacyModeForSize(1234,567)==-1);
    SystemDisplayCatalog c;
    // Two displays, the custom mode at a listed size: the stock menu selects the size.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(-1,-1,1920,1080,0),c,error));
    assert(c.available && c.count==2 && c.spanAvailable && c.devices.size()==2);
    assert(c.devices[0]=="1: Primary monitor" && c.devices[1]=="2: Secondary monitor");
    assert(c.modes.size()==5 && c.modes.front().kind==SystemDisplayModeSlot::Kind::Desktop && c.modes.back().kind==SystemDisplayModeSlot::Kind::Custom);
    assert(c.modes[0].label=="Desktop Native (1920 \xC3\x97 1080)" && c.modes[1].label=="1280 \xC3\x97 720 (16:9)");
    assert(c.modes[3].label=="3840 \xC3\x97 2160 (16:9)" && c.modes[4].label=="Custom (1920 \xC3\x97 1080)");
    assert(c.modeSelected==2 && c.unlistedMode.empty());
    // Refresh rounds 59.94 to 60 for the requested size; the desktop rate is not added.
    assert(c.refresh.size()==2 && c.refresh[0].rate==0 && c.refresh[0].label=="Auto" && c.refresh[1].rate==60 && c.refresh[1].label=="60 Hz");
    assert(c.refreshSelected==0);
    // Several rates for one size, a listed rate selected.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(0,8,1920,1080,144),c,error));
    assert(c.modeSelected==3 && c.refresh.size()==3 && c.refresh[1].rate==60 && c.refresh[2].rate==144 && c.refreshSelected==2);
    // An unlisted current refresh rate sits in the reserved last slot.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(0,8,1920,1080,75),c,error));
    assert(c.unlistedRefresh.size()==1 && c.unlistedRefresh[0].rate==75 && c.refreshSelected==SystemDisplayRefreshSlots-1);
    // Desktop Native; Custom at an unlisted size; a legacy size the display lacks.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(-1,-2,1000,700,0),c,error) && c.modeSelected==0 && c.unlistedMode.empty());
    assert(c.refresh.size()==2 && c.refresh[1].rate==60);
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(-1,-1,1000,700,0),c,error) && c.modeSelected==int(c.modes.size())-1);
    assert(c.modes.back().label=="Custom (1000 \xC3\x97 700)");
    // A size the display does not offer has no rates; the desktop rate is not added.
    assert(c.refresh.size()==1 && c.refresh[0].rate==0);
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(-1,11,1000,700,0),c,error));
    assert(c.unlistedMode.size()==1 && c.unlistedMode[0].width==640 && c.unlistedMode[0].height==480 &&
           c.unlistedMode[0].label=="640 \xC3\x97 480 (4:3)" && c.modeSelected==SystemDisplayModeSlots-1);
    // Patches: a listed size writes both dimensions and its legacy mode; Custom
    // writes the size it names; re-picking the current entry writes nothing.
    StateValues patch;
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(1,-1,1000,700,0),c,error) && c.modeSelected==int(c.modes.size())-1 && c.deviceSelected==1);
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,2,patch,error) &&
           patch==StateValues({{"r_customWidth",1920.0},{"r_customHeight",1080.0},{"r_mode",3.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,0,patch,error) && patch==StateValues({{"r_mode",-2.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,int(c.modes.size())-1,patch,error) && patch.empty());
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,-1,patch,error) && patch==StateValues({{"r_screen",-1.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,0,patch,error) && patch==StateValues({{"r_screen",0.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,1,patch,error) && patch.empty());
    // Re-picking the selected size writes nothing, so no same-size display restart.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(1,-1,1920,1080,0),c,error) && c.modeSelected==2);
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,2,patch,error) && patch.empty());
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Refresh,1,patch,error) && patch==StateValues({{"r_displayRefresh",60.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Refresh,0,patch,error) && patch.empty());
    // Custom writes the clamped size it names.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(0,-2,100,20000,0),c,error) && c.modes.back().label=="Custom (320 \xC3\x97 16384)");
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,int(c.modes.size())-1,patch,error) &&
           patch==StateValues({{"r_mode",-1.0},{"r_customWidth",320.0},{"r_customHeight",16384.0}}));
    // A size pick returns a rate the new size lacks to Auto; Auto's display is
    // the window's (display 2 here).
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(1,8,3840,2160,144),c,error) && c.autoDisplay==1 && c.refreshSelected==2);
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,2,patch,error) &&
           patch==StateValues({{"r_customWidth",1920.0},{"r_customHeight",1080.0},{"r_mode",3.0},{"r_displayRefresh",0.0}}));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,0,patch,error) && patch==StateValues({{"r_mode",-2.0},{"r_displayRefresh",0.0}}));
    // On the Auto display (r_screen -1: the window's, display 2 here) too.
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(-1,8,3840,2160,144),c,error) && c.deviceSelected==-1 && c.refreshSelected==2);
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,2,patch,error) &&
           patch==StateValues({{"r_customWidth",1920.0},{"r_customHeight",1080.0},{"r_mode",3.0},{"r_displayRefresh",0.0}}));
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(1,8,3840,2160,60),c,error));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,1,patch,error) &&
           patch==StateValues({{"r_customWidth",1280.0},{"r_customHeight",720.0},{"r_mode",0.0}})); // 59.94 keeps 60
    // A display pick returns a listed size the display lacks to Desktop Native
    // and a rate it lacks to Auto; a typed Custom size stays.
    {auto narrow=input;narrow.topology.displays[0].modes={{1920,1080,60}};
     assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,8,3840,2160,144),c,error));
     assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,0,patch,error) &&
            patch==StateValues({{"r_screen",0.0},{"r_mode",-2.0},{"r_displayRefresh",0.0}}));
     assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,8,3840,2160,60),c,error));
     assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,0,patch,error) && patch==StateValues({{"r_screen",0.0},{"r_mode",-2.0}}));
     assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,-1,3000,2000,144),c,error));
     assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,0,patch,error) && patch==StateValues({{"r_screen",0.0},{"r_displayRefresh",0.0}}));
     narrow.currentDisplay=1;assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,8,3840,2160,144),c,error) && c.autoDisplay==0);
     assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,-1,patch,error) &&
            patch==StateValues({{"r_screen",-1.0},{"r_mode",-2.0},{"r_displayRefresh",0.0}}));
     narrow.currentDisplay=2;assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,8,3840,2160,144),c,error));
     assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,-1,patch,error) && patch==StateValues({{"r_screen",-1.0}}));
     narrow.currentDisplay=9;assert(BuildSystemDisplayCatalog(narrow,DisplayDraft(1,8,3840,2160,144),c,error) && c.autoDisplay==0);}
    patch={{"untouched",true}};
    assert(!SystemDisplaySelectionPatch(c,SystemDisplayList::Device,2,patch,error) && patch.contains("untouched"));
    assert(!SystemDisplaySelectionPatch(c,SystemDisplayList::Device,-2,patch,error));
    assert(!SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,SystemDisplayModeSlots-1,patch,error));
    assert(!SystemDisplaySelectionPatch(c,SystemDisplayList::Refresh,int(c.refresh.size()),patch,error));
    assert(!SystemDisplaySelectionPatch(SystemDisplayCatalog{},SystemDisplayList::Device,-1,patch,error));
    // A non-legacy size picks Custom; sizes outside 320x240..16384x16384 are not listed.
    input.topology.displays[0].modes={{1234,567,60},{20000,10000,60},{300,200,60},{2560,1440,120}};
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(0,-2,1000,700,0),c,error) && c.modes.size()==4);
    assert(c.modes[1].width==1234 && c.modes[1].legacyMode==-1 && c.modes[2].width==2560 && c.modes[2].legacyMode==6);
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Mode,1,patch,error) && patch.at("r_mode")==StateValue(-1.0));
    // A stale display index: connected displays, then numbered placeholders; it has no sizes.
    input.topology=Topo();
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(4,-2,1000,700,0),c,error));
    assert(c.count==2 && c.devices.size()==5 && c.devices[2]=="Display 3 (not connected)" && c.devices[4]=="Display 5 (not connected)");
    assert(c.modes.size()==2 && c.modes[0].label=="Desktop Native" && c.refresh.size()==1);
    // A display that is not connected cannot be picked; the stale current one re-picks as nothing.
    assert(!SystemDisplaySelectionPatch(c,SystemDisplayList::Device,2,patch,error));
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,4,patch,error) && patch.empty());
    assert(SystemDisplaySelectionPatch(c,SystemDisplayList::Device,1,patch,error) && patch==StateValues({{"r_screen",1.0}}));
    assert(BuildSystemDisplayCatalog(input,DisplayDraft(40,-2,1000,700,0),c,error) && c.devices.size()==size_t(SystemDisplayDeviceSlots));
    // One display: no span; the window's display, else the primary, describes Auto.
    auto single=input;single.topology.displays.resize(1);single.currentDisplay=9;
    assert(BuildSystemDisplayCatalog(single,DisplayDraft(-1,-2,1000,700,0),c,error) && c.count==1 && !c.spanAvailable && c.devices.size()==1);
    assert(c.modes[0].label=="Desktop Native (1920 \xC3\x97 1080)");
    // Auto describes the window's display, else the primary.
    auto differing=input;differing.topology.displays[1].desktop={2560,1440,60};differing.topology.displays[1].modes={{2560,1440,60}};
    differing.currentDisplay=2;
    assert(BuildSystemDisplayCatalog(differing,DisplayDraft(-1,-2,1000,700,0),c,error) && c.modes[0].label=="Desktop Native (2560 \xC3\x97 1440)");
    differing.currentDisplay=1;
    assert(BuildSystemDisplayCatalog(differing,DisplayDraft(-1,-2,1000,700,0),c,error) && c.modes[0].label=="Desktop Native (1920 \xC3\x97 1080)");
    differing.currentDisplay=9;
    assert(BuildSystemDisplayCatalog(differing,DisplayDraft(-1,-2,1000,700,0),c,error) && c.modes[0].label=="Desktop Native (1920 \xC3\x97 1080)");
    // The desktop rate is not added to the size's own rates, unlike the stock menu.
    auto desktopRate=input;desktopRate.topology.displays[0].desktop={1920,1080,75};desktopRate.topology.displays[0].modes={{1920,1080,60}};
    assert(BuildSystemDisplayCatalog(desktopRate,DisplayDraft(0,-2,1000,700,0),c,error) && c.refresh.size()==2 && c.refresh[1].rate==60);
    // Spanning needs the displays together within 16384 pixels.
    auto wide=input;for(auto& d:wide.topology.displays){d.width=9000;d.x=int(d.id-1)*9000;}
    assert(BuildSystemDisplayCatalog(wide,DisplayDraft(-1,-2,1000,700,0),c,error) && c.count==2 && !c.spanAvailable);
    auto positionless=input;positionless.topology.absolutePlacement=false;
    assert(BuildSystemDisplayCatalog(positionless,DisplayDraft(-1,-2,1000,700,0),c,error) && !c.spanAvailable);
    // Density 2: the topology's sizes are pixels.
    auto dense=input;dense.topology.displays[0].desktop={3840,2160,60};dense.topology.displays[0].modes={{3840,2160,60},{2560,1440,60}};dense.currentDisplay=1;
    assert(BuildSystemDisplayCatalog(dense,DisplayDraft(-1,-2,1000,700,0),c,error) && c.modes[0].label=="Desktop Native (3840 \xC3\x97 2160)");
    // Long and multibyte names are cut at a code point to 48, the last an ellipsis.
    auto named=input;std::string name;for(int i=0;i<60;++i)name+=i%2?"\xC3\xA9":"a";named.topology.displays[0].name=name;
    assert(BuildSystemDisplayCatalog(named,DisplayDraft(-1,-2,1000,700,0),c,error));
    assert(CodePoints(c.devices[0])==3+48 && c.devices[0].ends_with("\xE2\x80\xA6") && ValidStateValue(StateValue(c.devices[0])));
    named.topology.displays[0].name=name.substr(0,48+24);
    assert(BuildSystemDisplayCatalog(named,DisplayDraft(-1,-2,1000,700,0),c,error) && c.devices[0]=="1: "+name.substr(0,48+24));
    // Overflow keeps Desktop, Custom, the current size and the largest sizes.
    auto many=input;many.topology.displays[0].modes.clear();
    for(int i=0;i<50;++i)many.topology.displays[0].modes.push_back({640+i*16,480,60});
    assert(BuildSystemDisplayCatalog(many,DisplayDraft(0,-1,640,480,0),c,error));
    assert(c.modes.size()==size_t(SystemDisplayModeSlots-1) && c.modes[1].width==640 && c.modes[2].width==640+14*16 && c.modes[c.modes.size()-2].width==640+49*16);
    assert(c.modeSelected==1);
    for(int i=0;i<20;++i)many.topology.displays[0].modes.push_back({1920,1080,double(30+i*10)});
    assert(BuildSystemDisplayCatalog(many,DisplayDraft(0,3,1920,1080,40),c,error));
    assert(c.refresh.size()==size_t(SystemDisplayRefreshSlots-1) && c.refresh[1].rate==40 && c.refresh.back().rate==220 && c.refreshSelected==1);
    // A translation that changes a format's placeholders falls back to English.
    auto translated=input;translated.text.size="%s x %d";translated.text.rate="%d Гц";translated.text.missing="Écran %d %d";translated.text.desktop="Bureau %s";
    assert(BuildSystemDisplayCatalog(translated,DisplayDraft(3,-2,1000,700,60),c,error));
    assert(c.modes[0].label=="Desktop Native" && c.devices[2]=="Display 3 (not connected)" && c.unlistedRefresh.at(0).label=="60 Гц");
    assert(c.modes.back().label=="Custom (1000 \xC3\x97 700)");
    // A capture without displays builds nothing and leaves the output alone.
    auto empty=input;empty.topology.displays.clear();c.count=77;
    assert(!BuildSystemDisplayCatalog(empty,DisplayDraft(-1,-2,1000,700,0),c,error) && c.count==77);
    auto missing=DisplayDraft(-1,-2,1000,700,0);missing.erase("r_mode");
    assert(!BuildSystemDisplayCatalog(input,missing,c,error) && c.count==77);
    missing=DisplayDraft(-1,-2.5,1000,700,0);assert(!BuildSystemDisplayCatalog(input,missing,c,error) && c.count==77);
    // Descriptors name monitors by name, bounds and desktop mode.
    auto moved=Topo();assert(SameSystemDisplay(moved.displays[0],Topo().displays[0]));
    moved.displays[0].x+=1;assert(!SameSystemDisplay(moved.displays[0],Topo().displays[0]));
    assert(SameSystemDisplay(Topo().displays[0],Topo().displays[0]) && !SameSystemDisplay(Topo().displays[0],Topo().displays[1]));
}
int main() {
    Catalogs();
    Seed();SystemSettingsHost host;StateValues values;std::string error;assert(host.Read(values,error));
    values["r_swapInterval"]=1.0;auto topology=Topo();auto actual=Actual();SystemDisplayPlan plan;
    RecoveryPairs(values,actual,topology);
    assert(BuildDisplayRequest(values,actual,topology,plan,error));
    assert(plan.request.displayId==2 && plan.request.parms.width==1280 && plan.request.parms.height==720);
    assert(plan.request.restorePlacement && plan.request.windowX==1960 && plan.request.windowY==50 && !plan.request.maximized);
    assert(MatchesDisplay(plan,Observed(plan),error));Portable(plan,topology);
    // A window that leaves fullscreen or borderless is centred on its display:
    // the captured window sits at the display's origin, where a bordered
    // window's title bar would be off the screen. A bordered one keeps its place.
    {auto covering=actual;auto& w=covering.window;w.windowX=1920;w.windowY=0;w.logicalWidth=w.pixelWidth=1920;w.logicalHeight=w.pixelHeight=1080;
     SystemDisplayPlan left;w.fullscreen=w.fullscreenDesktop=true;
     assert(BuildDisplayRequest(values,covering,topology,left,error) && left.request.windowX==1920+320 && left.request.windowY==180);
     assert(left.request.restorePlacement && MatchesDisplay(left,Observed(left),error));
     w.fullscreen=w.fullscreenDesktop=false;w.borderless=true;
     assert(BuildDisplayRequest(values,covering,topology,left,error) && left.request.windowX==1920+320 && left.request.windowY==180);
     w.borderless=false;
     assert(BuildDisplayRequest(values,covering,topology,left,error) && left.request.windowX==1920 && left.request.windowY==0);}
    // Vulkan's window is single-sample; r_multiSamples reaches only its scene targets.
    auto sampled=values;sampled["r_multiSamples"]=4.0;
    assert(BuildDisplayRequest(sampled,actual,topology,plan,error,false) && plan.request.parms.multiSamples==0);
    assert(MatchesDisplay(plan,Observed(plan),error));Portable(plan,topology);
    auto windowSampled=Observed(plan);windowSampled.presentation.samples=4;assert(!MatchesDisplay(plan,windowSampled,error));
    assert(BuildDisplayRequest(sampled,actual,topology,plan,error,true) && plan.request.parms.multiSamples==4);
    assert(BuildDisplayRequest(values,actual,topology,plan,error));
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
    // Auto takes the rate nearest the desktop's (60 Hz): 59.94, not the highest.
    changed["r_displayRefresh"]=0.0;assert(BuildDisplayRequest(changed,actual,topology,plan,error) && plan.expected.refreshRate==59.94f);
    Portable(plan,topology);
    {auto fast=topology;for(auto& d:fast.displays)d.desktop.refresh=144;
     assert(BuildDisplayRequest(changed,actual,fast,plan,error) && plan.expected.refreshRate==144);
     // Of two equally near rates the lower wins.
     auto tie=topology;tie.displays[1].modes={{1920,1080,70},{1920,1080,50}};auto even=changed;even["r_customWidth"]=1920.0;even["r_customHeight"]=1080.0;
     assert(BuildDisplayRequest(even,actual,tie,plan,error) && plan.expected.refreshRate==50);}
    // A restore whose whole hertz names a neighbour (60.00 beside 59.94) replays Auto.
    {auto tv=topology;tv.displays[1].desktop={1920,1080,double(59.94f)};tv.displays[1].modes={{1920,1080,60},{1920,1080,double(59.94f)}};
     auto exclusive=actual;auto& w=exclusive.window;w.fullscreen=true;w.fullscreenDesktop=false;w.borderless=false;
     w.logicalWidth=w.pixelWidth=w.modeWidth=w.modePixelWidth=1920;w.logicalHeight=w.pixelHeight=w.modeHeight=w.modePixelHeight=1080;w.refreshRate=59.94f;
     SystemDisplayPlan restore;assert(BuildDisplayRestore(exclusive,tv,restore,error) && restore.request.parms.displayHz==0);Portable(restore,tv);
     w.refreshRate=60;assert(BuildDisplayRestore(exclusive,tv,restore,error) && restore.request.parms.displayHz==60);
     tv.displays[1].desktop.refresh=60;w.refreshRate=59.94f;assert(!BuildDisplayRestore(exclusive,tv,restore,error));}
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
    // An exclusive mode resizes the display's bounds while it is active; the
    // descriptors keep the desktop size, so a Revert still finds the monitor.
    for(const char* driver:{"windows","x11","cocoa","wayland"}) {
     videoDriver=driver;auto positionless=Actual();positionless.window.positionValid=false;SystemDisplayPlan held,resolved;StateValues saved;
     SystemDisplayTopology desktopTopology;assert(CaptureDisplayTopology(desktopTopology,error));
     assert(BuildDisplayRestore(positionless,desktopTopology,held,error) && CaptureDisplayRecovery(held,desktopTopology,saved,error));
     boundsWidth=1280;boundsHeight=720;SystemDisplayTopology exclusive;
     assert(CaptureDisplayTopology(exclusive,error) && exclusive.displays[1].width==1920 && exclusive.displays[1].height==1080 && exclusive.displays[1].x==1920);
     assert(ResolveDisplayRecovery(saved,exclusive,resolved,error) && resolved.request.displayId==2);
     boundsWidth=1920;boundsHeight=1080;
    }
    videoDriver="windows";
    displayBoundsFail=true;captured.primary=999;assert(!CaptureDisplayTopology(captured,error) && captured.primary==999);displayBoundsFail=false;
    assert(refreshCalls==0 && writes==0);
#else
    SystemDisplayTopology captured;captured.primary=999;assert(!CaptureDisplayTopology(captured,error) && captured.primary==999);
#endif
    std::puts("SYSTEM display builders: pure pixel/policy requests, actual restore matching, strict portable recovery, topology rejection and the display lists passed");
}
'''


def check_catalog_declarations():
    import sys
    sys.path.insert(0, str(ROOT / 'tools/ui'))
    import json
    import system_display_catalog as catalog
    header = (ROOT / 'src/ui/application/SystemDisplay.h').read_text(encoding='utf-8')
    slots = f'SystemDisplayDeviceSlots = {catalog.DEVICE_SLOTS}, SystemDisplayModeSlots = {catalog.MODE_SLOTS}, SystemDisplayRefreshSlots = {catalog.REFRESH_SLOTS};'
    assert slots in header, 'the page declares different display list slot counts from the builder'
    # The page declares exactly these keys, with these types and initial values.
    page = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.rui').read_text(encoding='utf-8')
    state = json.loads(page[page.index('{'):])['state']
    declared = {key: value for key, value in state.items() if key.startswith(catalog.PREFIX)}
    assert declared == catalog.keys(), 'the page declares different display list keys'
    # The settings schema types each key the same way.
    service = (ROOT / 'src/ui/SettingsService.cpp').read_text(encoding='utf-8')
    types = {'boolean': 1, 'number': 0, 'string': 2}
    for key, declaration in catalog.keys().items():
        if not key.endswith('.label'):
            assert '{"' + key + '",' + str(types[declaration['type']]) + '}' in service, f'{key} is not typed {declaration["type"]} in the settings schema'
    for stem in ('settings.display.', 'settings.display.mode.', 'settings.display.refresh.'):
        assert 'result.emplace("' + stem + '"+std::to_string(i)+".label",2);' in service, f'the settings schema lacks the {stem} labels'
    # Display events, and the window moving to another display or scale,
    # advance the topology generation the cached lists depend on.
    sdl = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
    assert '++sdl3DisplayTopologyGeneration;' in function_body(sdl, 'static void SDL3_HandleDisplayEvent('), 'display events must advance the topology generation'
    window = 'case SDL_EVENT_WINDOW_DISPLAY_CHANGED:\n\t\tcase SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:\n\t\t\t++sdl3DisplayTopologyGeneration;'
    assert window in sdl, 'a window moving display or scale must advance the topology generation'


def main(production_mutations=()):
    check_catalog_declarations()
    host = (ROOT / 'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    display = (ROOT / 'src/ui/application/SystemDisplay.cpp').read_text(encoding='utf-8')
    for old, new in production_mutations:
        if display.count(old) != 1:
            raise RuntimeError('Production mutation anchor is not unique')
        display = display.replace(old, new)
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
                            str(ROOT / 'src/ui/retained/Presentation.cpp'), str(ROOT / 'src/framework/PerformancePreset.cpp'), '-o', str(binary)], check=True, env=environment)
            subprocess.run([str(binary)], check=True, env=environment)
        source.write_text('#define ID_DEDICATED\n#include "src/ui/application/SystemDisplay.h"\n#include <cassert>\n'+
                          '\n'.join(line for line in display.splitlines() if not line.startswith('#include '))+
                          '\nint main(){openq4::ui::SystemDisplayTopology t;t.primary=9;std::string e;'
                          'openq4::ui::SystemDisplayPlan p;p.request.parms.width=777;openq4::ui::StateValues v;'
                          'assert(!openq4::ui::CaptureDisplayTopology(t,e)&&t.primary==9);'
                          'assert(!openq4::ui::InspectDisplayRecovery(v,p,t,e)&&p.request.parms.width==777&&t.primary==9);'
                          'assert(!openq4::ui::ValidateDisplayRecoveryPair(v,v,v,e));'
                          'assert(!openq4::ui::ResolveDisplayRecovery(v,t,p,e)&&p.request.parms.width==777);'
                          'openq4::ui::SystemDisplayCatalogInput in;openq4::ui::SystemDisplayCatalog c;c.count=5;'
                          'assert(!openq4::ui::BuildSystemDisplayCatalog(in,v,c,e)&&c.count==5);'
                          'assert(!openq4::ui::SystemDisplaySelectionPatch(c,openq4::ui::SystemDisplayList::Device,-1,v,e));'
                          'assert(!openq4::ui::SameSystemDisplay({},{}));}\n', encoding='utf-8')
        binary=temp/'dedicated.exe'
        subprocess.run([compiler,'-std=c++20','-I',str(ROOT),str(source),'-o',str(binary)],check=True,env=environment)
        subprocess.run([str(binary)],check=True,env=environment)


if __name__ == '__main__':
    main()
