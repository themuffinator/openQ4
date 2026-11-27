#!/usr/bin/env python3
"""Execute renderer-selection boot/reload bodies with counted lifecycle seams."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
// Select extracted Android branches only after parsing the host library.
// Defining __ANDROID__ on the command line changes libc++'s system headers.
#if defined(OPENQ4_TEST_ANDROID) && !defined(__ANDROID__)
#define __ANDROID__ 1
#endif
using idStr=std::string;
enum rendererModuleApi_t { RENDER_MODULE_API_GL,RENDER_MODULE_API_VULKAN,RENDER_MODULE_API_GLES };
#ifdef __ANDROID__
static const auto rm_platformDefaultApi=RENDER_MODULE_API_GLES;
#else
static const auto rm_platformDefaultApi=RENDER_MODULE_API_VULKAN;
#endif
struct Cvar { std::string value;const char* GetString()const{return value.c_str();}void SetString(const char* v){value=v;} };
static Cvar r_renderApi{"best"},r_renderDevice{"auto"},r_actualRenderApi{"gl"};
static bool rm_selectionReloadPending=false,allowed=true,ownersRetired=false;
static int boots=0,registered=0,unloads=0,epochs=0,releases=0;
static std::map<std::string,std::string> config,cli;
static std::vector<std::string> events;
struct Cvars {std::string vr="0";void SetCVarString(const char* key,const char* value){assert(std::string(key)=="vr_enable");vr=value;}} cvars,*cvarSystem=&cvars;
static const int CMD_FL_RENDERER=8,RENDER_MODULE_DISPOSITION_NONE=0;
static struct { bool everBooted=true;struct {int disposition=1;}status; } rm_state;
static bool RM_AllowImageModuleChange(){return allowed;}
static void RM_RegisterCommands(){++registered;}
static bool RM_PeekConfigRendererSetting(const char* key,char* out,int length){
 auto it=config.find(key);if(it==config.end())return false;
 std::snprintf(out,length,"%s",it->second.c_str());return true;
}
static bool RM_PeekConfigRenderApi(char* out,int length){return RM_PeekConfigRendererSetting("r_renderApi",out,length);}
static void R_RendererModule_Boot(){++boots;events.push_back(r_renderApi.value+":"+r_renderDevice.value);}
struct Common {void StartupVariable(const char* key,bool once){
 auto found=cli.find(key);if(found==cli.end())return;
 if(std::string(key)=="r_renderApi")r_renderApi.SetString(found->second.c_str());
 if(std::string(key)=="r_renderDevice")r_renderDevice.SetString(found->second.c_str());
 if(std::string(key)=="vr_enable")cvarSystem->SetCVarString(key,found->second.c_str());
 if(once)cli.erase(found);
}} commonValue,*common=&commonValue;
struct Commands {void RemoveFlaggedCommands(int flags){assert(flags==CMD_FL_RENDERER&&ownersRetired);events.push_back("commands");}} commands,*cmdSystem=&commands;
static bool RM_UnloadModule(){assert(ownersRetired);++unloads;events.push_back("unload");return true;}
static void RM_ReleaseDisplayVideoPin(){++releases;}
static void RM_AdvanceDisplayEpoch(){++epochs;}
// VR Mode decides at boot, after vr_enable is read (VR-08)
static int vrDecides=0;static std::string vrDecidedWith;
static bool VR_BootDecide(){++vrDecides;vrDecidedWith=cvars.vr;return false;}
'''
TEST = r'''
int main(){
 assert(RM_AutomaticApi(false)==rm_platformDefaultApi);
#if defined(OPENQ4_OPENXR) && !defined(__ANDROID__)
 assert(RM_AutomaticApi(true)==RENDER_MODULE_API_GL);
#else
 assert(RM_AutomaticApi(true)==rm_platformDefaultApi);
#endif
 R_RendererModule_BootEarly();assert(boots==1&&events.back()=="best:auto");
 config={{"r_renderApi","vulkan"},{"r_renderDevice","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}};
 R_RendererModule_BootEarly();assert(events.back()=="vulkan:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
 cli={{"r_renderApi","gl"},{"r_renderDevice","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},{"r_fullscreen","0"},{"in_mouse","0"},{"r_vkDevice","-1"}};
 R_RendererModule_BootEarly();assert(events.back()=="gl:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
 assert(cli.size()==5); // Ordinary boot never consumes launch overrides.
 allowed=false;R_RendererModule_BootEarly();assert(boots==3&&registered==3);allowed=true;
 assert(R_RendererModule_FinishSelectionReload()&&!unloads&&!epochs);
 r_renderApi.value="vulkan";r_renderDevice.value="cccccccccccccccccccccccccccccccc";
 R_RendererModule_UseSavedSelectionOnReload();
 assert(r_renderApi.value=="vulkan"&&r_renderDevice.value=="cccccccccccccccccccccccccccccccc");
 assert(rm_selectionReloadPending&&!unloads&&cli.size()==3&&cli.at("r_fullscreen")=="0"&&cli.at("in_mouse")=="0");
 allowed=false;assert(!R_RendererModule_FinishSelectionReload()&&rm_selectionReloadPending&&!unloads);allowed=true;
 ownersRetired=true;assert(R_RendererModule_FinishSelectionReload());
 assert(unloads==1&&epochs==1&&releases==1&&!rm_state.everBooted&&!rm_selectionReloadPending);
 assert(events[events.size()-2]=="commands"&&events.back()=="unload"&&r_actualRenderApi.value=="UNINITIALIZED");
 assert(R_RendererModule_FinishSelectionReload()&&unloads==1); // One-shot boundary.
 config={{"r_renderApi",r_renderApi.value},{"r_renderDevice",r_renderDevice.value}};
 R_RendererModule_BootEarly();assert(events.back()=="vulkan:cccccccccccccccccccccccccccccccc");
 config["vr_enable"]="1";R_RendererModule_BootEarly();assert(cvars.vr=="1"&&vrDecidedWith=="1");
 cli["vr_enable"]="0";R_RendererModule_BootEarly();assert(cvars.vr=="0"&&cli.contains("vr_enable"));
 // the boot decision sees the launch override, not the config, and runs once a boot
 assert(vrDecidedWith=="0"&&vrDecides==boots);
 std::puts("Renderer selection startup: config/CLI precedence, launch override consumption, owner teardown and one-shot module reload passed");
}
'''


def main():
    source = (ROOT/'src/renderer/RendererModule.cpp').read_text(encoding='utf-8')
    bodies = '\n'.join(function_body(source, signature) for signature in (
        'static rendererModuleApi_t RM_AutomaticApi(', 'void R_RendererModule_BootEarly(', 'void R_RendererModule_UseSavedSelectionOnReload(',
        'bool R_RendererModule_FinishSelectionReload('))
    common = function_body((ROOT/'src/framework/Common.cpp').read_text(encoding='utf-8'), 'void Com_ReloadEngine_f(')
    markers = ['commonLocal.ShutdownGame( true );', 'R_RendererModule_FinishSelectionReload()', 'commonLocal.InitGame();']
    assert [common.index(m) for m in markers] == sorted(common.index(m) for m in markers)
    # Never unload from a menu callback or from a renderer-module stack frame.
    assert source.count('R_RendererModule_FinishSelectionReload(') == 1
    boot = function_body(source, 'void R_RendererModule_Boot(')
    # the automatic choice follows what VR Mode wants now (VR-08), decided at boot
    assert 'RM_AutomaticApi(VR_WantsRenderer())' in boot
    early = function_body(source, 'void R_RendererModule_BootEarly(')
    assert early.index('StartupVariable( "vr_enable"') < early.index('VR_BootDecide();') < early.index('R_RendererModule_Boot();')
    assert 'idStr::Icmp(requestedValue,"best")==0' in boot
    base = ROOT/'.tmp/rendering-selection'
    base.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='startup-',dir=base) as directory:
        path=Path(directory); cpp=path/'startup.cpp';exe=path/'startup.exe'
        cpp.write_text(SUPPORT+bodies+TEST,encoding='utf-8')
        compiler=shutil.which('clang++') or shutil.which('g++')
        environment=dict(os.environ,TEMP=directory,TMP=directory,TMPDIR=directory)
        for defines in ([],['-DOPENQ4_OPENXR=1'],['-DOPENQ4_OPENXR=1','-DOPENQ4_TEST_ANDROID=1']):
            subprocess.run([compiler,'-std=c++20',*defines,str(cpp),'-o',str(exe)],check=True,cwd=path,env=environment)
            subprocess.run([str(exe)],check=True,cwd=path,env=environment)


if __name__=='__main__':main()
