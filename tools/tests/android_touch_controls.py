#!/usr/bin/env python3
"""Execute the production Android touch integration with in-memory engine sinks.

The real routing, lifecycle, motion accumulation and overlay state run here.
No OS input is sent and no device/window is controlled.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include "src/sys/android/android_touch_controls.h"
#include "src/framework/TouchInput.h"
#include "mobile/quake4_bridge.h"
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
enum {CVAR_SYSTEM=1,CVAR_ARCHIVE=2,CVAR_BOOL=4,CVAR_FLOAT=8,CVAR_ROM=16,CVAR_NOCHEAT=32,CVAR_INTEGER=64,
      SE_KEY=1,K_ESCAPE=27,CMD_EXEC_INSERT=1};
enum androidTouchPhase_t {ANDROID_TOUCH_DOWN,ANDROID_TOUCH_MOVE,ANDROID_TOUCH_UP,ANDROID_TOUCH_CANCEL};
struct idCVar;
static std::map<std::string,idCVar*> registered;
static std::map<std::string,float> values;
struct idStr {
    std::string text;
    idStr()=default;
    idStr(const char* value):text(value){}
    int Cmp(const char* other)const{return text.compare(other);}
    int Length()const{return static_cast<int>(text.size());}
    const char* c_str()const{return text.c_str();}
};
struct idCVar {
    float value;std::string text;
    idCVar(const char* name,const char* initial,int,const char*,float=0,float=0):value(std::strtof(initial,nullptr)),text(initial){registered[name]=this;}
    float GetFloat()const{return value;}
    int GetInteger()const{return static_cast<int>(value);}
    bool GetBool()const{return value!=0;}
    const char* GetString()const{return text.c_str();}
    void SetFloat(float v){value=v;}
    void SetBool(bool v){value=v?1.0f:0.0f;}
    void SetString(const char* v){text=v;}
};
struct Common {int warnings=0,prints=0;void Warning(const char*,...){++warnings;}void Printf(const char*,...){++prints;}} commonValue,*common=&commonValue;
struct Cvars {
    float GetCVarFloat(const char* name){auto it=registered.find(name);return it==registered.end()?values[name]:it->second->GetFloat();}
    bool GetCVarBool(const char* name){return GetCVarFloat(name)!=0;}
    int GetCVarInteger(const char* name){return static_cast<int>(GetCVarFloat(name));}
    void SetCVarBool(const char* name,bool v){values[name]=v?1.0f:0.0f;}
} vars,*cvarSystem=&vars;
struct Session {
    bool mapSpawned=true,insideExecuteMapChange=false,demo=false,mp=false;
    void* guiActive=nullptr;void* guiTest=nullptr;
    bool IsDemoPlaybackActive(){return demo;}
    bool IsMultiplayer(){return mp;}
} sessLocal,*session=&sessLocal;
struct Console {bool active=false;bool Active(){return active;}} consoleValue,*console=&consoleValue;
struct Game {bool cinematic=false;bool InCinematic(){return cinematic;}} gameValue,*game=&gameValue;
static bool retainedOpen=false,loading=false;
static bool RetainedUI_IsOpen(){return retainedOpen;}
static bool openQ4_AcceptingLoadingContinueInput(){return loading;}
static int now=1000;
static int Sys_Milliseconds(){return now;}
namespace idMath {static float ClampFloat(float lo,float hi,float v){return std::clamp(v,lo,hi);}}
static openq4::touch::MotionState motion;
static void Sys_SetTouchMotion(float side,float forward,float yaw,float pitch,bool active,unsigned buttons,unsigned canceled,int next,int reload){
    motion.Submit({side,forward,yaw,pitch,active,buttons,canceled,next,reload});
}
static void Sys_ResetTouchMotion(){motion.Clear();}
struct Usercmd {std::vector<int> impulses;void TriggerImpulse(int i){impulses.push_back(i);}} usercmd,*usercmdGen=&usercmd;
struct Commands {std::vector<std::string> text;void BufferCommandText(int,const char* s){text.emplace_back(s);}} commands,*cmdSystem=&commands;
static std::vector<int> escape;
static void Posix_QueEvent(int type,int key,int down,int,void*){check(type==SE_KEY && key==K_ESCAPE,"unexpected session input");escape.push_back(down);}
@CVARS@
static const float TOUCH_LOOK_DEGREES_PER_DP=0.25f;
static openq4::touch::Controls touchControls;
static openq4::touch::Contacts touchContacts;
static float touchDisplayScale=1,touchWheelX=0,touchWheelY=0,touchPlayerScale=-1;
static int touchPlayerPreset=-1;
static idStr touchLayoutRecord;
static openq4::touch::OrientationEdits touchLayoutEdits;
static bool touchLayoutValid=false;
static int setupLoads=0;
static void TouchControls_LoadSetup(){++setupLoads;}
// The interface record has its own transaction test (android_interface_setup.py).
static int interfaceLoads=0;
static void Interface_LoadSetup(){++interfaceLoads;}
static bool touchWheelOwned=false,touchAppActive=false,touchPosted=false,touchOverlayHidden=false;
@FUNCTIONS@
static void frame(){AndroidTouchControls_Frame(720,360,1,true);}
static void button(androidTouchPhase_t phase,long long device,long long finger,int control) {
    const auto& layout=touchControls.GetLayout();const auto& at=layout.buttons[control];
    check(AndroidTouchControls_HandleFinger(phase,device,finger,at.x/layout.width,at.y/layout.height),"gameplay contact leaked to menu");
}
static void reset(){
    AndroidTouchControls_Release();sessLocal=Session{};consoleValue.active=retainedOpen=loading=false;
    gameValue.cinematic=false;values.clear();values["in_touchscreen"]=1;
    in_touchControls.SetBool(true);in_touchControlsScale.SetFloat(1);in_touchLookSensitivity.SetFloat(1);
    in_touchLayoutPreset.SetFloat(0);in_touchLayout.SetString("");commonValue=Common{};
    ui_touchCanReload.SetBool(true);ui_touchCanZoom.SetBool(true);
    usercmd.impulses.clear();commands.text.clear();escape.clear();now=1000;frame();motion.Take();
}
int main(){try{
    using namespace openq4::touch;
    reset();int loaded=setupLoads,interfaceLoaded=interfaceLoads;frame();frame();
    check(setupLoads==loaded && interfaceLoads==interfaceLoaded,"setup read every frame");
    AndroidTouchControls_Release();frame();
    check(setupLoads==loaded+1,"setup was not reloaded on foreground entry");
    check(interfaceLoads==interfaceLoaded+1,"interface setup was not checked on foreground entry");
    reset();button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);frame();
    check(motion.Take().buttons&(1u<<QUAKE4_BTN_ATTACK),"preset precondition fire held");
    in_touchLayoutPreset.SetFloat(PRESET_LEFT_HANDED);frame();
    check(touchControls.GetLayout().leftHanded && !motion.Take().buttons,"preset change kept old attack");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_MOVE,1,1,.2f,.2f);frame();
    check(!motion.Take().buttons,"old contact reactivated after preset change");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,1,1,.2f,.2f);
    button(ANDROID_TOUCH_DOWN,1,2,BUTTON_FIRE);frame();
    check(motion.Take().buttons&(1u<<QUAKE4_BTN_ATTACK),"mirrored fire cannot be pressed");
    reset();const float hudTop=AndroidTouchControls_HudTop();
    check(hudTop>0 && hudTop<180,"gameplay HUD must reserve space for Pause");
    sessLocal.guiActive=&sessLocal;check(AndroidTouchControls_HudTop()==0,"menu retained touch HUD reservation");
    sessLocal.guiActive=nullptr;values["ui_radialOwner"]=1;
    check(AndroidTouchControls_HudTop()==hudTop,"weapon wheel moved the HUD");
    AndroidTouchControls_OtherInput();check(AndroidTouchControls_HudTop()==0,"controller retained touch HUD reservation");
    button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);
    check(AndroidTouchControls_HudTop()==hudTop,"touch did not restore HUD reservation");
    AndroidTouchControls_Release();check(AndroidTouchControls_HudTop()==0,"background retained touch HUD reservation");
    // Portrait raises the bottom readouts above the thumb deck; landscape keeps them.
    reset();check(AndroidTouchControls_HudBottomEdge()==0,"landscape reserved a thumb deck");
    AndroidTouchControls_Frame(360,745,1,true);
    check(std::fabs(AndroidTouchControls_HudBottomEdge()-(745*.5f+360*.375f))<.01f,"portrait readouts not raised to the deck");
    sessLocal.guiActive=&sessLocal;check(AndroidTouchControls_HudBottomEdge()==0,"menu kept the deck reservation");
    sessLocal.guiActive=nullptr;AndroidTouchControls_OtherInput();
    check(AndroidTouchControls_HudBottomEdge()==0,"controller kept the deck reservation");
    reset();values["in_joystick"]=0;values["sensitivity"]=0;values["m_yaw"]=0;
    button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);button(ANDROID_TOUCH_UP,1,1,BUTTON_FIRE);frame();frame();
    check(motion.Take().buttons==(1u<<QUAKE4_BTN_ATTACK) && !motion.Take().buttons,"quick tap lost between event pumps and usercmd");
    button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);frame();button(ANDROID_TOUCH_CANCEL,1,1,BUTTON_FIRE);frame();
    check(!motion.Take().buttons,"cancelled unsampled press fired");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_DOWN,1,2,.15f,.8f);
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_MOVE,1,2,.25f,.8f);
    button(ANDROID_TOUCH_DOWN,2,2,BUTTON_FIRE);
    const auto fire=touchControls.GetLayout().buttons[BUTTON_FIRE];
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_MOVE,2,2,(fire.x+20)/720,(fire.y+8)/360);frame();
    auto sample=motion.Take();
    check(sample.side>.9f && std::fabs(sample.yaw+5)<.001f && std::fabs(sample.pitch-2)<.001f
        && (sample.buttons&(1u<<QUAKE4_BTN_ATTACK)),"controller disabled/mouse settings changed touch move, fire or look");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_CANCEL,1,2,0,0);frame();
    check(motion.Take().buttons&(1u<<QUAKE4_BTN_ATTACK),"same finger id on another device released fire");

    // Every gameplay contact keeps its owner through a menu takeover.
    sessLocal.guiActive=&sessLocal;frame();
    check(!motion.Take().buttons,"menu left touch attack held");
    check(AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,2,2,0,0),"old gameplay release became menu pointer");
    check(!AndroidTouchControls_HandleFinger(ANDROID_TOUCH_DOWN,3,1,.5f,.5f),"fresh menu pointer refused");
    check(AndroidTouchControls_HandleFinger(ANDROID_TOUCH_DOWN,4,1,.5f,.5f),"second menu device stole first pointer");
    check(!AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,3,1,.5f,.5f),"menu owner lost release");
    check(AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,4,1,.5f,.5f),"ignored contact inherited menu");

    reset();button(ANDROID_TOUCH_DOWN,1,1,BUTTON_WEAPON);now+=301;frame();
    check(ui_radialTouchActive.GetBool() && (motion.Take().buttons&(1u<<QUAKE4_BTN_WEAPON_WHEEL)),"hold failed to open touch wheel");
    values["ui_radialOwner"]=1;const auto weapon=touchControls.GetLayout().buttons[BUTTON_WEAPON];
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_MOVE,1,1,(weapon.x+72)/720,weapon.y/360);frame();
    check(std::fabs(ui_radialTouchX.GetFloat()-.75f)<.001f && motion.Take().yaw==0,"touch wheel failed to steer independently of view");
    button(ANDROID_TOUCH_CANCEL,1,1,BUTTON_WEAPON);frame();
    check(vars.GetCVarBool("ui_radialCancel") && !(motion.Take().buttons&(1u<<QUAKE4_BTN_WEAPON_WHEEL)),"wheel cancellation selected or stayed held");

    reset();button(ANDROID_TOUCH_DOWN,1,1,BUTTON_RELOAD);frame();
    check(motion.Take().impulse==-1,"reload activated before release");
    button(ANDROID_TOUCH_UP,1,1,BUTTON_RELOAD);frame();check(motion.Take().impulse==13,"reload tap not delivered");
    button(ANDROID_TOUCH_DOWN,1,2,BUTTON_WEAPON);button(ANDROID_TOUCH_UP,1,2,BUTTON_WEAPON);frame();
    check(motion.Take().impulse==14,"weapon tap did not select next");
    ui_touchCanReload.SetBool(false);ui_touchCanZoom.SetBool(false);frame();
    check(!(touchControls.VisibleButtons()&((1u<<BUTTON_RELOAD)|(1u<<BUTTON_ZOOM))),"unsupported weapon buttons visible");
    gameValue.cinematic=true;frame();button(ANDROID_TOUCH_DOWN,1,3,BUTTON_FIRE);frame();
    check(!motion.Take().buttons,"invisible cinematic fire remained active");
    button(ANDROID_TOUCH_DOWN,1,4,BUTTON_PAUSE);button(ANDROID_TOUCH_UP,1,4,BUTTON_PAUSE);frame();
    check(escape==std::vector<int>({1,0}),"cinematic pause did not route Escape");

    reset();button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);frame();motion.Take();
    AndroidTouchControls_OtherInput();check(touchOverlayHidden && !motion.Take().buttons,"controller handoff left invisible fire held");
    button(ANDROID_TOUCH_UP,1,1,BUTTON_FIRE);button(ANDROID_TOUCH_DOWN,1,2,BUTTON_FIRE);frame();
    check(!touchOverlayHidden && (motion.Take().buttons&(1u<<QUAKE4_BTN_ATTACK)),"fresh touch failed to reclaim controls");
    AndroidTouchControls_Frame(360,745,1,true);check(!motion.Take().buttons,"rotation held old control");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_MOVE,1,2,.2f,.2f);AndroidTouchControls_Frame(360,745,1,true);
    check(!motion.Take().buttons,"rotated contact became a new press");
    AndroidTouchControls_Release();check(!motion.Take().buttons,"focus loss left a touch button held");
    check(AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,1,2,.2f,.2f),"orphan release became menu pointer");
    reset();button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);values["in_touchscreen"]=0;frame();
    check(!motion.Take().buttons && !touchControls.HasFingers(),"disabling touch retained actions");
    // The layout editor's record: hidden controls leave the visible set and
    // cannot be pressed, each orientation reads its own edits, and a malformed
    // record falls back to the preset with a warning.
    reset();const std::string untouched="*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0";
    const std::string hideJump="-160,-120,100,35,0;*,*,100,35,1;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0;*,*,100,35,0";
    in_touchLayout.SetString(("1|"+hideJump+"|"+untouched).c_str());button(ANDROID_TOUCH_DOWN,1,1,BUTTON_FIRE);frame();
    check(!(touchControls.VisibleButtons()&(1u<<BUTTON_JUMP)) && touchControls.GetLayout().edited,"hidden control still visible");
    check(std::fabs(touchControls.GetLayout().buttons[BUTTON_FIRE].x-560)<.01f,"moved control not applied");
    check(!(motion.Take().buttons&(1u<<QUAKE4_BTN_ATTACK)),"a layout change kept the old press");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,1,1,0,0);
    const auto jumpAt=touchControls.GetLayout().buttons[BUTTON_JUMP];
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_DOWN,1,2,jumpAt.x/720,jumpAt.y/360);frame();
    check(!(motion.Take().buttons&(1u<<QUAKE4_BTN_MOVE_UP)),"a hidden control was pressed");
    AndroidTouchControls_HandleFinger(ANDROID_TOUCH_UP,1,2,0,0);
    AndroidTouchControls_Frame(360,720,1,true);
    check(!touchControls.GetLayout().edited && (touchControls.VisibleButtons()&(1u<<BUTTON_JUMP)),"portrait used the landscape edits");
    in_touchLayout.SetString("1|broken");AndroidTouchControls_Frame(720,360,1,true);
    check(!touchControls.GetLayout().edited && commonValue.warnings==1,"malformed layout record applied or silent");
    std::puts("PASS: production touch routing, motion, button edges, menu/focus/rotation handoffs, portrait deck HUD reservation, capabilities, radial input and layout edits");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
'''


def source() -> str:
    glue = (ROOT / "src/sys/android/android_touch_controls.cpp").read_text(encoding="utf-8")
    cvars = glue[glue.index("static idCVar in_touchControls("):glue.index("// in_touchLookSensitivity 1:")]
    signatures = (
        "static bool TouchControls_Active(", "static bool TouchControls_InCinematic(",
        "static void TouchControls_LetGo(", "static void TouchControls_Apply(",
        "void AndroidTouchControls_Frame(", "bool AndroidTouchControls_HandleFinger(",
        "void AndroidTouchControls_OtherInput(", "void AndroidTouchControls_Release(",
        "float AndroidTouchControls_HudTop(", "float AndroidTouchControls_HudBottomEdge(",
    )
    functions = "\n".join(function_body(glue, signature) for signature in signatures)
    return SUPPORT.replace("@CVARS@", cvars).replace("@FUNCTIONS@", functions)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate-native", type=Path)
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++"))
    args = parser.parse_args()
    if args.generate_native:
        args.generate_native.write_text(source(), encoding="utf-8")
        return
    if not args.cxx:
        parser.error("C++17 compiler required")
    output = ROOT / ".tmp/android-touch-controls"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "integration.cpp"
    binary = output / ("integration.exe" if os.name == "nt" else "integration")
    unit.write_text(source(), encoding="utf-8")
    env = {**os.environ, "TMP": str(output), "TEMP": str(output), "TMPDIR": str(output)}
    subprocess.run([args.cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT), str(unit), "-o", str(binary)], cwd=output, env=env, check=True)
    subprocess.run([str(binary)], cwd=output, env=env, check=True)


if __name__ == "__main__":
    main()
