#!/usr/bin/env python3
"""Compile the unchanged rendering-settings adapter with counted engine boundaries.

No window, device, config file or user input is touched. Covers live versus saved
selection, stale inventory refusal, UUID persistence and checked restart gating.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#define _CRT_SECURE_NO_WARNINGS
#include "src/ui/RenderingSettings.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
// Exercise Android adapter branches without retargeting host libc++ headers.
#if defined(OPENQ4_TEST_ANDROID) && !defined(__ANDROID__)
#define __ANDROID__ 1
#endif
static int checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} }while(false)
using namespace openq4::ui;
static renderDeviceCatalog_t inventory{};
static bool inventoryAvailable=true,persist=true,mapSpawned=false,loading=false;
static bool carry=false,saveOk=true;
static int captures=0,persists=0,queued=0,reloads=0,saves=0,carried=0;
static std::string bootApi="gl",bootDevice="auto";
struct Cvars {
 int overrideDevice=-1;
 const char* GetCVarString(const char* key) { CHECK(std::string(key)=="r_actualRenderApi");return "gl"; }
 int GetCVarInteger(const char* key) { CHECK(std::string(key)=="r_vkDevice");return overrideDevice; }
} cvars,*cvarSystem=&cvars;
struct Session {
 bool IsLoadingSaveGame()const { return loading; }
 bool IsMapSpawned()const { return mapSpawned; }
 // A single-player game the restart may save, reload around and load again.
 bool CanSaveForRendererRestart()const { return mapSpawned && !loading && carry; }
 bool SaveForRendererRestart() { CHECK(mapSpawned && carry);++saves;return saveOk; }
 void QueueRendererRestart() { CHECK(saveOk);++carried; }
} sessLocal,*session=&sessLocal;
struct GLConfig { const char* renderer_string="Actual OpenGL GPU"; };
struct Renderer { GLConfig config;const GLConfig& GetGLConfig()const {return config;} } renderer,*renderSystem=&renderer;
enum { RENDER_MODULE_API_GL,RENDER_MODULE_API_VULKAN,RENDER_MODULE_API_GL_MODULE,RENDER_MODULE_API_GLES,CMD_EXEC_APPEND };
struct Status {int activeApi=RENDER_MODULE_API_GL,requestedApi=RENDER_MODULE_API_GL;} status;
const Status& R_RendererModule_GetStatus() {return status;}
bool R_RendererModule_QueryRenderingDevices(renderDeviceCatalog_t& output) {
 ++captures;output=inventoryAvailable?inventory:renderDeviceCatalog_t{};return inventoryAvailable;
}
bool R_RendererModule_RenderSelectionPending(const char* api,const char* device) { return api!=bootApi || device!=bootDevice; }
void R_RendererModule_UseSavedSelectionOnReload() { CHECK(persist && persists>=2);++reloads; }
bool Common_WriteSettingsConfiguration(bool locked,std::string& error) {
 ++persists;CHECK(!locked);if(!persist)error="persistence refused";return persist;
}
struct Commands {void BufferCommandText(int mode,const char* text){CHECK(mode==CMD_EXEC_APPEND && std::string(text)=="reloadEngine menu\n");++queued;}} commands,*cmdSystem=&commands;
'''
TEST = r'''
int main() {
 RenderingSettings hardware;std::string error;StateValues patch,values;
 StateValues draft{{"r_renderApi",std::string("gl")},{"r_renderDevice",std::string("auto")}};
 inventory.count=2;
 std::strcpy(inventory.devices[0].id,"00000000000000000000000000000001");
 std::strcpy(inventory.devices[0].name,"Integrated GPU");inventory.devices[0].usable=true;
 std::strcpy(inventory.devices[1].id,"00000000000000000000000000000002");
 std::strcpy(inventory.devices[1].name,"Discrete GPU");inventory.devices[1].usable=true;inventory.devices[1].preferred=true;
 CHECK(RenderDevicePreferenceValid("auto") && RenderDevicePreferenceValid(inventory.devices[0].id));
 for(const char* bad:{"","aut","a","AUTO","-1","2","0000000000000000000000000000000z","000000000000000000000000000000000"}) CHECK(!RenderDevicePreferenceValid(bad));
 hardware.Read(draft,true,values);
 CHECK(values.size()==RenderingSettings::Schema().size());
 for(const auto& [key,type]:RenderingSettings::Schema()) CHECK(values.contains(key) && values.at(key).index()==type);
 CHECK(captures==1 && std::get<bool>(values.at("settings.render.autoDetect")) && !std::get<bool>(values.at("settings.render.pending")));
 CHECK(std::get<std::string>(values.at("settings.render.active")).find("Actual OpenGL GPU")!=std::string::npos);
 const auto token=std::get<std::string>(values.at("settings.render.catalog"));
 hardware.Read(draft,true,values);CHECK(captures==1); // No driver calls per frame.
 CHECK(hardware.Select(1,token,patch,error));
 CHECK(patch.at("r_renderApi")==StateValue(std::string("vulkan")) && patch.at("r_renderDevice")==StateValue(std::string(inventory.devices[1].id)));
 CHECK(persists==0 && queued==0); // A choice edits a draft only.
 const auto originalPatch=patch;
 CHECK(!hardware.Select(0,"0",patch,error) && patch==originalPatch);
 CHECK(!hardware.Select(.5,token,patch,error) && patch==originalPatch);
 CHECK(!hardware.Select(std::numeric_limits<double>::quiet_NaN(),token,patch,error));
 CHECK(!hardware.Select(2,token,patch,error));
 cvars.overrideDevice=0;CHECK(!hardware.Select(0,token,patch,error));
 hardware.Read(draft,true,values);CHECK(!std::get<bool>(values.at("settings.render.canChoose")));cvars.overrideDevice=-1;
 CHECK(hardware.Select(-1,token,patch,error) && patch.at("r_renderDevice")==StateValue(std::string("auto")));
 std::swap(inventory.devices[0],inventory.devices[1]);
 CHECK(!hardware.Select(0,token,patch,error)); // Same slot now names a different GPU.
 hardware.Read(draft,true,values);const auto fresh=std::get<std::string>(values.at("settings.render.catalog"));CHECK(fresh!=token);
 CHECK(!hardware.Select(0,token,patch,error));CHECK(hardware.Select(0,fresh,patch,error));
 CHECK(patch.at("r_renderDevice")==StateValue(std::string("00000000000000000000000000000002")));
 inventory.devices[0].usable=false;CHECK(!hardware.Select(0,fresh,patch,error));
 hardware.Read(draft,true,values);CHECK(!hardware.Select(0,std::get<std::string>(values.at("settings.render.catalog")),patch,error));
 draft["r_renderApi"]=std::string("vulkan");draft["r_renderDevice"]=std::string(inventory.devices[1].id);
 hardware.Read(draft,false,values);CHECK(std::get<bool>(values.at("settings.render.pending")) && !std::get<bool>(values.at("settings.render.autoDetect")) && !std::get<bool>(values.at("settings.render.canRestart")));
 hardware.Read(draft,true,values);CHECK(std::get<bool>(values.at("settings.render.canRestart")));
 mapSpawned=true;hardware.Read(draft,true,values);CHECK(!std::get<bool>(values.at("settings.render.canRestart")));
 CHECK(std::get<std::string>(values.at("settings.render.notice"))=="#str_230149");
 CHECK(!RenderingSettings::Restart(error) && !persists && !queued && !saves);
 // A game that can be saved is carried: the restart is offered inside it, and
 // a save that fails stops it before anything is written or queued.
 carry=true;hardware.Read(draft,true,values);CHECK(std::get<bool>(values.at("settings.render.canRestart")));
 CHECK(std::get<std::string>(values.at("settings.render.notice"))=="#str_231767");
 hardware.Read(draft,false,values);CHECK(!std::get<bool>(values.at("settings.render.canRestart")));
 saveOk=false;CHECK(!RenderingSettings::Restart(error) && saves==1 && !persists && !queued && !carried && !reloads);
 saveOk=true;loading=true;hardware.Read(draft,true,values);CHECK(!std::get<bool>(values.at("settings.render.canRestart")));
 CHECK(!RenderingSettings::Restart(error) && saves==1 && !persists);loading=false;
 carry=false;mapSpawned=false;
 loading=true;CHECK(!RenderingSettings::Restart(error) && !persists && !queued);loading=false;
 persist=false;CHECK(!RenderingSettings::Restart(error) && persists==1 && !queued && !reloads);
 persist=true;CHECK(RenderingSettings::Restart(error) && persists==2 && queued==1 && reloads==1 && !carried && saves==1);
 hardware.Read(draft,true,values);CHECK(std::get<std::string>(values.at("settings.render.notice"))=="#str_230148");
 // Inside the game: save, write the settings, then the reload that loads the save.
 mapSpawned=true;carry=true;
 persist=false;CHECK(!RenderingSettings::Restart(error) && saves==2 && persists==3 && !carried && reloads==1);
 persist=true;CHECK(RenderingSettings::Restart(error) && saves==3 && persists==4 && carried==1 && queued==1 && reloads==2);
 mapSpawned=false;carry=false;
 bootApi="vulkan";bootDevice=std::get<std::string>(draft.at("r_renderDevice"));
 hardware.Read(draft,true,values);CHECK(!std::get<bool>(values.at("settings.render.pending")) && std::get<bool>(values.at("settings.render.autoDetect")));
 draft["r_renderDevice"]=std::string("ffffffffffffffffffffffffffffffff");
 hardware.Read(draft,true,values);CHECK(std::get<double>(values.at("settings.render.selected"))==RENDER_DEVICE_LIMIT);
 CHECK(hardware.Backend("best",patch,error));CHECK(hardware.Backend("vulkan",patch,error));
#ifdef __ANDROID__
 CHECK(hardware.Backend("gles",patch,error));CHECK(!hardware.Backend("gl",patch,error));
#else
 CHECK(hardware.Backend("gl",patch,error));CHECK(!hardware.Backend("gles",patch,error));
#endif
 CHECK(!hardware.Backend("invented",patch,error));
 hardware.Reset();inventoryAvailable=false;hardware.Read(draft,true,values);
 CHECK(std::get<bool>(values.at("settings.render.canChoose")) && !std::get<bool>(values.at("settings.render.vulkan")));
 CHECK(hardware.Select(-1,std::get<std::string>(values.at("settings.render.catalog")),patch,error) && patch.at("r_renderApi")==StateValue(std::string("best")));
 CHECK(hardware.Backend("best",patch,error));CHECK(!hardware.Backend("vulkan",patch,error));
 std::printf("Rendering settings: %d checks passed\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=None)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    compiler = args.compiler or next((p for n in ('clang++', 'g++') if (p := shutil.which(n))), None)
    if not compiler: raise RuntimeError('C++ compiler required')
    source = (ROOT / 'src/ui/RenderingSettings.cpp').read_text(encoding='utf-8')
    source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include '))
    base = ROOT / '.tmp/rendering-selection'
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='adapter-', dir=base) as name:
        temp = Path(name)
        path = temp / 'adapter.cpp'
        path.write_text(SUPPORT + '\n' + source + '\n' + TEST, encoding='utf-8')
        env = dict(os.environ, TEMP=name, TMP=name, TMPDIR=name)
        for android in (False, True):
            exe = temp / ('android.exe' if android else 'desktop.exe')
            flags = ['-DOPENQ4_TEST_ANDROID=1'] if android else []
            if args.sanitize: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), *flags,
                            str(path), '-o', str(exe)], check=True, cwd=temp, env=env)
            subprocess.run([str(exe)], check=True, cwd=temp, env=env)


if __name__ == '__main__': main()
