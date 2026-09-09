#!/usr/bin/env python3
"""Exercise production SYSTEM catalog/CVar host and private default lookup.

Uses the real host implementation, presentation codec and UTF-8 validation with
counted CVar/SDL doubles. No engine, display or input-device operation is run.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "src/ui/application/SystemSettingsHost.h"
struct idStr : std::string { using std::string::string; using std::string::operator=; };
enum { CVAR_BOOL=1, CVAR_INTEGER=2, CVAR_FLOAT=4, CVAR_ROM=8, CVAR_INIT=16,
    CVAR_NETWORKSYNC=32, CVAR_CHEAT=64, CVAR_PRIVATE=128 };
static int writes=0;
static std::string refuse;
struct idCVar {
    std::string key;
    idStr value;
    int flags=0;
    float minimum=1,maximum=-1;
    std::vector<std::string> choices;
    std::vector<const char*> pointers;
    int GetFlags() const { return flags; }
    float GetMinValue() const { return minimum; }
    float GetMaxValue() const { return maximum; }
    const char* GetString() const { return value.c_str(); }
    const char** GetValueStrings() {
        pointers.clear(); for(const auto& choice:choices)pointers.push_back(choice.c_str());
        pointers.push_back(nullptr); return pointers.data();
    }
    void SetString(const char* text) { ++writes; if(key!=refuse)value=text; }
};
class idInternalCVar : public idCVar {
    friend bool CVar_ReadDefault(const char*,idStr&);
    idStr resetString;
public:
    void SetDefault(const std::string& text) { resetString=text; }
};
struct Cvars {
    std::map<std::string,idInternalCVar> variables;
    idInternalCVar* FindInternal(const char* name) {
        if(!name)return nullptr;
        std::string wanted=name;
        for(char& c:wanted)if(c>='A' && c<='Z')c+=32;
        for(auto& [key,value]:variables) {
            std::string folded=key;
            for(char& c:folded)if(c>='A' && c<='Z')c+=32;
            if(folded==wanted)return &value;
        }
        return nullptr;
    }
    idCVar* Find(const char* name) { return FindInternal(name); }
} localCVarSystem,*cvarSystem=&localCVarSystem;

#if defined(USE_SDL3)
#include "src/renderer/RenderModuleAPI.h"
using SDL_DisplayID=unsigned;
struct SDL_DisplayMode { SDL_DisplayID displayID=1; int w=1920,h=1080; float refresh_rate=60,pixel_density=1; };
static int displayCount=2;
static bool haveModes=true,haveQuery=true,haveServices=true,haveQueryCallback=true,haveDesktop=true;
static int refreshCalls=0,windowQueries=0;
static unsigned currentDisplay=2,primaryDisplay=1;
static SDL_DisplayID queriedDisplay=0;
static SDL_DisplayMode desktop;
static std::vector<SDL_DisplayMode> modes={{0,1920,1080,60,1},{0,1280,720,59.94f,1},{0,1920,1080,60,2}};
static SDL_DisplayID* SDL_GetDisplays(int* count) {
    *count=displayCount; if(!displayCount)return nullptr;
    auto* result=static_cast<SDL_DisplayID*>(std::malloc(displayCount*sizeof(SDL_DisplayID)));
    for(int i=0;i<displayCount;++i)result[i]=i+1;
    return result;
}
static void SDL_free(void* p) { std::free(p); }
static SDL_DisplayID SDL_GetPrimaryDisplay() { return primaryDisplay; }
static const SDL_DisplayMode* SDL_GetDesktopDisplayMode(SDL_DisplayID display) { queriedDisplay=display;return haveDesktop?&desktop:nullptr; }
static SDL_DisplayMode** SDL_GetFullscreenDisplayModes(SDL_DisplayID display,int* count) {
    queriedDisplay=display;
    *count=haveModes?static_cast<int>(modes.size()):0;
    auto** result=static_cast<SDL_DisplayMode**>(std::malloc(modes.size()*sizeof(SDL_DisplayMode*)));
    for(size_t i=0;i<modes.size();++i)result[i]=&modes[i];
    return result;
}
static void RefreshNativeWindowHandles(renderModuleWindowInfo_t*) {
    ++refreshCalls;localCVarSystem.variables.at("r_windowWidth").SetString("555");
}
static bool QueryWindowState(renderWindowState_t* output) {
    ++windowQueries;if(!haveQuery)return false;*output={};output->displayId=currentDisplay;return true;
}
static const renderWindowServices_t* Sys_GetRenderWindowServices() {
    static renderWindowServices_t services={};
    services.RefreshNativeWindowHandles=RefreshNativeWindowHandles;
    services.QueryWindowState=haveQueryCallback?QueryWindowState:nullptr;
    return haveServices?&services:nullptr;
}
#endif

using namespace openq4::ui;
static void Seed() {
    writes=0; refuse.clear();localCVarSystem.variables.clear();
    for(const auto& item:SystemSettingsHost::Catalog()) {
        auto& variable=localCVarSystem.variables[item.key]; variable.key=item.key;
        variable.flags=item.type==1?CVAR_BOOL:item.type==0?(item.integer?CVAR_INTEGER:CVAR_FLOAT):0;
        StateValue value;
        if(item.type==1)value=false;
        else if(item.type==2) { value=item.stringChoices.front();variable.choices=item.stringChoices; }
        else value=item.numberChoices.empty()?item.minimum:item.numberChoices.front();
        if(item.key=="r_renderer")variable.choices.push_back("modern");
        if(item.type==0) { variable.minimum=static_cast<float>(item.minimum);variable.maximum=static_cast<float>(item.maximum); }
        PresentationValue presentation;
        if(item.type==0)presentation.data[0]=std::get<double>(value);
        else if(item.type==1) { presentation.type=PresentationType::Boolean;presentation.data[0]=std::get<bool>(value)?1:0; }
        else { presentation.type=PresentationType::String;presentation.text=std::get<std::string>(value); }
        variable.value=FormatPresentationValue(presentation);variable.SetDefault(variable.value);
    }
    auto& brightness=localCVarSystem.variables.at("r_brightness");brightness.value="1.2345678901234567";brightness.SetDefault("1");
    localCVarSystem.variables.at("r_screenFraction").value="90";
    localCVarSystem.variables.at("r_renderer").value="modern";
    localCVarSystem.variables.at("image_usePrecompressedTextures").value="2";
    localCVarSystem.variables.at("r_windowWidth").value="1280";
    localCVarSystem.variables.at("r_windowHeight").value="720";
    localCVarSystem.variables.at("r_customWidth").value="1920";
    localCVarSystem.variables.at("r_customHeight").value="1080";
    localCVarSystem.variables.at("r_fullscreenDesktop").value="1";
}
'''

MAIN = r'''
int main() {
    Seed();SystemSettingsHost host;std::string error;StateValues original;
    assert(SystemSettingsHost::Schema().size()==53 && host.Read(original,error));
    assert(original.size()==53 && writes==0);
    assert(std::get<double>(original.at("r_brightness"))==1.2345678901234567);
    assert(original.at("r_screenFraction")==StateValue(90.0));
    assert(original.at("image_usePrecompressedTextures")==StateValue(2.0));
    assert(host.Validate(original,original,error));
    idStr text="sentinel";
    assert(!CVar_ReadDefault(nullptr,text) && text=="sentinel");
    assert(!CVar_ReadDefault("missing",text) && text=="sentinel");
    assert(CVar_ReadDefault("R_BRIGHTNESS",text) && text=="1" && writes==0);
    auto& brightness=localCVarSystem.variables.at("r_brightness");
    brightness.flags|=CVAR_PRIVATE;text="secret-not-returned";
    assert(!CVar_ReadDefault("r_brightness",text) && text=="secret-not-returned");
    StateValues failed={{"sentinel",true}};
    assert(!host.Read(failed,error) && failed==StateValues({{"sentinel",true}}));
    brightness.flags&=~CVAR_PRIVATE;
    StateValues defaults;assert(host.Defaults(defaults,error) && defaults.size()==53 && writes==0);
    assert(defaults.at("r_brightness")==StateValue(1.0));
    brightness.SetDefault("nan");failed={{"sentinel",true}};
    assert(!host.Defaults(failed,error) && failed==StateValues({{"sentinel",true}}));brightness.SetDefault("1");

    auto candidate=original;candidate["r_brightness"]=1.3456789012345678;
    assert(host.Validate(original,candidate,error));
    assert(!host.RequiresDeviceWork(original,candidate) && !host.NeedsConfirmation(original,candidate));
    const int beforeWrites=writes;
    assert(!host.Write({{"r_brightness",1.4},{"unknown",true}},error) && writes==beforeWrites);
    assert(!host.Write({{"r_brightness",true}},error) && writes==beforeWrites);
    assert(!host.Write({{"r_brightness",std::numeric_limits<double>::infinity()}},error) && writes==beforeWrites);
    assert(!host.Write({{"r_brightness",.4}},error) && writes==beforeWrites);
    assert(!host.Write({{"r_mode",1.5}},error) && writes==beforeWrites);
    assert(!host.Write({{"r_renderApi",std::string("vulkan")}},error) && error.find("engine restart")!=std::string::npos);
    assert(!host.Write({{"r_renderer",std::string("bad;quit")}},error) && writes==beforeWrites);
    for(int flag:{CVAR_ROM,CVAR_INIT,CVAR_NETWORKSYNC,CVAR_CHEAT,CVAR_PRIVATE}) {
        brightness.flags|=flag;
        assert(!host.Validate(original,candidate,error));
        assert(!host.Write({{"r_brightness",1.4}},error) && writes==beforeWrites);
        brightness.flags&=~flag;
    }
    assert(host.Write({{"r_brightness",1.3456789012345678}},error));
    StateValues live;assert(host.Read(live,error) && live==candidate);
    assert(std::get<double>(live.at("r_brightness"))==1.3456789012345678);
    assert(host.Write({{"r_brightness",original.at("r_brightness")}},error));
    assert(host.Read(live,error) && live==original);
    candidate=original;candidate["r_screenFraction"]=100.0;
    assert(host.Validate(original,candidate,error));
    assert(!host.Validate(candidate,original,error)); // UI does not author custom90.
    assert(host.ValidateRollback(original,candidate,original,error));
    assert(host.Write({{"r_screenFraction",100.0}},error));
    assert(host.Write({{"r_screenFraction",90.0}},error));
    candidate=original;candidate["r_renderer"]=std::string("best");
    assert(host.Validate(original,candidate,error));assert(!host.Validate(candidate,original,error));
    assert(host.ValidateRollback(original,candidate,original,error));
    auto invalid=candidate;invalid["r_multiSamples"]=3.0;assert(!host.Validate(original,invalid,error));
    invalid=candidate;invalid["r_screenFraction"]=91.0;assert(!host.Validate(original,invalid,error));
    invalid=candidate;invalid["r_windowWidth"]=319.0;assert(!host.Validate(original,invalid,error));
    invalid=candidate;invalid.erase("r_mode");assert(!host.Validate(original,invalid,error));
    invalid=original;invalid["r_brightness"]=1.4;invalid["r_shadows"]=true;
    refuse="r_shadows";
    assert(!host.Write({{"r_brightness",1.4},{"r_shadows",true}},error));
    assert(host.Read(live,error) && live.at("r_brightness")==StateValue(1.4) && live.at("r_shadows")==StateValue(false));
    refuse.clear();assert(host.Write({{"r_brightness",original.at("r_brightness")}},error));
    // Read and rollback preserve an unrelated externally changed custom value.
    live=original;live["r_screenFraction"]=93.0;candidate=live;candidate["r_brightness"]=1.4;
    assert(host.ValidateRollback(original,live,candidate,error));
    for(const char* key:{"r_mode","r_multiSamples","r_displayRefresh","r_windowWidth","r_swapInterval"}) {
        candidate=original;candidate[key]=std::get<double>(candidate.at(key))+1;
        assert(host.NeedsConfirmation(original,candidate) && host.RequiresDeviceWork(original,candidate));
    }
    for(const char* key:{"r_lightGridPreload","image_downSize","s_useEAXReverb"}) {
        candidate=original;candidate[key]=true;assert(host.RequiresDeviceWork(original,candidate));
        assert(!host.NeedsConfirmation(original,candidate));
    }
    candidate=original;candidate["r_screen"]=1.0;
#if defined(USE_SDL3)
    const int preflightWrites=writes;
    StateValues beforePreflight;assert(host.Read(beforePreflight,error));
    assert(host.Validate(original,candidate,error));
    candidate["r_screen"]=2.0;assert(!host.Validate(original,candidate,error));
    candidate=original;candidate["r_fullscreen"]=true;candidate["r_fullscreenDesktop"]=false;candidate["r_mode"]=-1.0;
    candidate["r_displayRefresh"]=60.0;
    assert(host.Validate(original,candidate,error) && queriedDisplay==2);
    candidate["r_displayRefresh"]=144.0;assert(!host.Validate(original,candidate,error));
    candidate["r_displayRefresh"]=60.0;candidate["r_multiScreen"]=1.0;assert(!host.Validate(original,candidate,error));
    candidate["r_multiScreen"]=0.0;candidate["r_customWidth"]=3840.0;candidate["r_customHeight"]=2160.0;
    assert(host.Validate(original,candidate,error)); // high-density pixel mode
    // A point-size match alone is invalid: strict exclusive requests are pixels.
    modes={{2,1920,1080,60,2}};
    candidate["r_customWidth"]=1920.0;candidate["r_customHeight"]=1080.0;
    assert(!host.Validate(original,candidate,error));
    candidate["r_customWidth"]=3840.0;candidate["r_customHeight"]=2160.0;
    assert(host.Validate(original,candidate,error));
    auto native=candidate;native["r_mode"]=-2.0;desktop={2,1920,1080,60,2};
    assert(host.Validate(original,native,error)); // Native means the desktop's pixel dimensions.
    modes={{2,3840,2160,60,1}};assert(host.Validate(original,native,error));
    modes={{2,1920,1080,60,1}};assert(!host.Validate(original,native,error));
    for(const auto size:std::vector<std::pair<int,int>>{{200,120},{32768,2160}}) {
        desktop={2,size.first,size.second,60,1};modes={desktop};
        assert(!host.Validate(original,native,error));
    }
    desktop={2,1920,1080,60,2};modes={desktop};
    for(float density:{0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        desktop.pixel_density=density;assert(!host.Validate(original,native,error));
    }
    haveDesktop=false;assert(!host.Validate(original,native,error));haveDesktop=true;
    desktop={2,1920,1080,60,2};modes={{2,1920,1080,59.94f,2}};
    assert(host.Validate(original,candidate,error));
    for(float density:{0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        modes[0].pixel_density=density;assert(!host.Validate(original,candidate,error));
    }
    modes={{2,1920,1080,59.94f,2}};modes[0].displayID=99;assert(!host.Validate(original,candidate,error));
    modes[0].displayID=0;
    haveQuery=false;assert(host.Validate(original,candidate,error) && queriedDisplay==1);haveQuery=true;
    haveServices=false;assert(host.Validate(original,candidate,error) && queriedDisplay==1);haveServices=true;
    haveQueryCallback=false;assert(host.Validate(original,candidate,error) && queriedDisplay==1);haveQueryCallback=true;
    currentDisplay=99;assert(host.Validate(original,candidate,error) && queriedDisplay==1);
    primaryDisplay=99;assert(!host.Validate(original,candidate,error));primaryDisplay=1;currentDisplay=2;
    candidate["r_screen"]=0.0;const int explicitQueries=windowQueries;
    assert(host.Validate(original,candidate,error) && queriedDisplay==1 && windowQueries==explicitQueries);
    candidate["r_screen"]=-1.0;
    haveModes=false;assert(!host.Validate(original,candidate,error));haveModes=true;
    displayCount=0;assert(!host.Validate(original,candidate,error));displayCount=2;
    displayCount=1025;assert(!host.Validate(original,candidate,error));displayCount=2;
    auto missingDisplay=original;missingDisplay["r_screen"]=8.0;
    assert(!host.ValidateRollback(missingDisplay,candidate,missingDisplay,error));
    StateValues afterPreflight;assert(host.Read(afterPreflight,error));
    assert(windowQueries>0 && refreshCalls==0 && writes==preflightWrites && afterPreflight==beforePreflight);
    std::puts("SYSTEM display preflight: pure actual-window query, primary fallback, exact pixel/native modes and no CVar writes passed");
#else
    assert(!host.Validate(original,candidate,error) && error.find("SDL3")!=std::string::npos);
#endif
    Seed();
    localCVarSystem.variables.at("r_brightness").value="1.2junk";failed={{"sentinel",true}};
    assert(!host.Read(failed,error) && failed==StateValues({{"sentinel",true}}));
    std::puts("SYSTEM settings host: 53-key catalog, defaults, exact decimals, validation/rollback, patch refusal and display classification passed");
}
'''


def source_checks(host_source):
    common = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    preset = common.split('OPENQ4_PERFORMANCE_PRESET_TOUCHED_CVARS[] = {', 1)[1].split('};', 1)[0]
    preset_keys = set(re.findall(r'"([a-zA-Z0-9_]+)"', preset))
    system = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.gui').read_text(encoding='utf-8')
    page = set(re.findall(r'\bcvar\s+"?([a-zA-Z_]\w*)', system)) - {'gui_set_sys_scroll'}
    page |= {'r_mode', 'r_skipBump', 'r_skipSky', 'r_skipSpecular'}
    catalog = set(re.findall(r'(?:Number|Boolean|String)\("(\w+)"', host_source))
    assert {key.lower() for key in page | preset_keys} == {key.lower() for key in catalog}
    assert len(preset_keys) == 32 and len(catalog) == 53
    renderer = (ROOT / 'src/renderer/RenderSystem_init.cpp').read_text(encoding='utf-8')
    modes = renderer.split('vidmode_t r_vidModes[] = {', 1)[1].split('};', 1)[0]
    expected = {int(mode): (int(width), int(height)) for mode, width, height in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', modes)}
    actual = host_source.split('constexpr int LegacyModes[][2] = {', 1)[1].split('};', 1)[0]
    assert [expected[i] for i in range(len(expected))] == [(int(w), int(h)) for w, h in re.findall(r'\{(\d+),(\d+)\}', actual)]
    assert not re.search(r'\b(?:GetFloat|SetCVar\w*|BufferCommand\w*|R_VidRestart_f|FatalError)\s*\(', host_source)
    display = function_body(host_source, 'bool DisplayTuple(')
    assert 'services->RefreshNativeWindowHandles(' not in display and 'SDL_GetDisplayForWindow(' not in display
    assert 'services->QueryWindowState(&observed)' in display and 'pointMatch' not in display


def main():
    host = (ROOT / 'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    source_checks(host)
    host = '\n'.join(line for line in host.splitlines() if not line.startswith('#include '))
    cvars = (ROOT / 'src/framework/CVarSystem.cpp').read_text(encoding='utf-8')
    document = (ROOT / 'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    validation = '\n'.join(function_body(document, name) for name in ('bool Utf8(', 'bool ValidStateValue('))
    # precompiled.h exposes Windows' function-like min/max macros in MSVC
    # builds. Keep them active around the actual production host methods, so
    # numeric_limits calls cannot accidentally pass only the portable double.
    windows_macros = '\n#define min(a,b) (((a)<(b))?(a):(b))\n#define max(a,b) (((a)>(b))?(a):(b))\n'
    code = (SUPPORT + function_body(cvars, '\nbool CVar_ReadDefault(') +
            '\nnamespace openq4::ui {\n' + validation + '\n}\n' + windows_macros + host +
            '\n#undef min\n#undef max\n' + MAIN)
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-settings-host-', dir=ROOT / '.tmp') as temp:
        source = Path(temp) / 'host.cpp'
        source.write_text(code, encoding='utf-8')
        for sdl in (False, True):
            binary = Path(temp) / ('sdl.exe' if sdl else 'plain.exe')
            defines = ['-DUSE_SDL3'] if sdl else []
            subprocess.run([compiler, '-std=c++20', *defines, '-I', str(ROOT), str(source),
                            str(ROOT / 'src/ui/retained/Presentation.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        dedicated = Path(temp) / 'dedicated.cpp'
        dedicated.write_text('#define ID_DEDICATED\n' + host + '\nint main() {}\n', encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', str(dedicated), '-o', str(Path(temp) / 'dedicated.exe')], check=True)


if __name__ == '__main__':
    main()
