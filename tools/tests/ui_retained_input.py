#!/usr/bin/env python3
"""Execute production retained key routing and usercmd release gates in memory.

SDL queue/window services are counted stubs. No device is polled, no OS event is
injected, and no game window is controlled by this test.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>
#include <memory>
#include <limits>
#include "src/ui/RetainedUI.h"
#include "src/ui/retained/Input.h"
bool retainedOpen = false;
bool RetainedUI_IsOpen() { return retainedOpen; }
struct Console { bool active=false; bool Active() const { return active; } } consoleObject;
Console* console=&consoleObject;
std::vector<retainedUIInput_t> queued;
void RetainedUI_QueueInput(const retainedUIInput_t& input, int) { queued.push_back(input); }
unsigned char Sys_GetConsoleKey(bool shifted) { return shifted ? '~' : '`'; }
bool s_retainedKeyOwners[K_LAST_KEY]={};
using Uint8=unsigned char;
constexpr int SDL_GAMEPAD_BUTTON_COUNT=4, SDL3_MAX_JOYSTICK_BUTTONS=4;
constexpr Uint8 SDL_HAT_CENTERED=0, SDL_HAT_UP=1, SDL_HAT_RIGHT=2, SDL_HAT_DOWN=4, SDL_HAT_LEFT=8;
bool s_gamepadButtonsDown[SDL_GAMEPAD_BUTTON_COUNT]={}, s_joystickButtonsDown[SDL3_MAX_JOYSTICK_BUTTONS]={};
bool s_gamepadLeftTriggerDown=false, s_gamepadRightTriggerDown=false;
Uint8 s_joystickHatState=SDL_HAT_CENTERED;
int SDL3_MapGamepadButton(Uint8 button) { return K_JOY3+button; }
int SDL3_JoyKeyFromOrdinal(int button) { return K_JOY3+button; }
void SDL3_PostControllerKeyEvent(int key,bool down,int time);
std::map<int,int> inputKeys;
struct idKeyInput {
    static inline bool held[K_LAST_KEY]={};
    static inline int bindings[K_LAST_KEY]={};
    static bool IsDown(int key) { return key>=0 && key<K_LAST_KEY && held[key]; }
    static int GetUsercmdAction(int key) { return bindings[key]; }
    static void PreliminaryKeyEvent(int key, bool down) { held[key]=down; }
    static void ClearStates() { std::memset(held,0,sizeof(held)); }
};
namespace idMath { int Abs(int x) { return std::abs(x); } int ClampChar(int x) { return std::clamp(x,-128,127); } }
constexpr int UB_NONE=0, UB_WEAPONWHEEL=8, UB_IMPULSE0=20, UB_IMPULSE127=147, UB_MAX_BUTTONS=160, UCF_IMPULSE_SEQUENCE=1;
constexpr int AXIS_SIDE=0, AXIS_FORWARD=1, AXIS_UP=2, AXIS_ROLL=3, AXIS_YAW=4, AXIS_PITCH=5, MAX_JOYSTICK_AXIS=6;
int physicalAxes[MAX_JOYSTICK_AXIS]={};
bool Sys_GetJoystickAxisState(int axis, int& value) { value=physicalAxes[axis]; return true; }
int Sys_PollJoystickInputEvents() { return MAX_JOYSTICK_AXIS; }
bool Sys_ReturnJoystickInputEvent(int i, int& axis, int& value) { axis=i; value=physicalAxes[i]; return true; }
void Sys_EndJoystickInputEvents() {}
bool IsWeaponSelectionImpulse(int) { return false; }
struct idUsercmdGenLocal {
    int buttonState[UB_MAX_BUTTONS]={};
    bool keyState[K_LAST_KEY]={}, directButtonState[UB_MAX_BUTTONS]={};
    bool retainedKeyBlocked[K_LAST_KEY]={}, retainedDirectBlocked[UB_MAX_BUTTONS]={}, retainedAxisBlocked[MAX_JOYSTICK_AXIS]={};
    int joystickAxis[MAX_JOYSTICK_AXIS]={};
    int inhibitCommands=0, mouseDx=0, mouseDy=0, mouseButton=0, filtersReset=0;
    bool mouseDown=false;
    struct Toggle { void Clear() {} } toggled_zoom;
    struct Cmd { int impulse=0,flags=0; } cmd;
    void ResetMouseFilter() { ++filtersReset; }
    bool Inhibited(); void Clear(); void Key(int,bool); void SetButtonAction(int,bool);
    void SetUsercmdButton(int,bool); void Joystick(); void RetainedInputChanged();
};
template<class T> T Max(T a,T b) { return std::max(a,b); }
struct FakeRuntime {
    std::vector<openq4::ui::RoutedInput> received;
    std::vector<openq4::ui::ControlAction> actions;
    float x=0,y=0;
    int cancels=0;
    void CancelInput(double) { ++cancels; }
    void PointerButton(bool down,double) { received.push_back({openq4::ui::RoutedInput::Kind::PointerButton,openq4::ui::MenuInput::Accept,down}); }
    void PointerMove(float a,float b,double) { x=a;y=b; }
    void MenuAction(openq4::ui::MenuInput menu,bool down,double) {
        received.push_back({openq4::ui::RoutedInput::Kind::Menu,menu,down});
        if(menu==openq4::ui::MenuInput::Back && down) actions.push_back({openq4::ui::ControlAction::Kind::Back,"doc","",""});
    }
    bool PopModal(double) { return false; }
    std::vector<openq4::ui::ControlAction> TakeActions() { auto result=actions;actions.clear();return result; }
};
std::unique_ptr<FakeRuntime> runtime;
openq4::ui::Input input;
bool inputFocused=true,inputSuspended=false,analogNeedsNeutral=true;
int analogDirection=-1;
unsigned inputGeneration=1,closes=0;
double now=0;
double PresentationTime() { return now; }
std::vector<openq4::ui::ControlAction> applicationRequests;
struct { int uiViewportWidth=1280,uiViewportHeight=720; } engineWindowState;
struct Common { void Warning(const char*) { assert(false); } } commonObject;
Common* common=&commonObject;
void Close() { ++closes;retainedOpen=false; }
'''

MAIN = r'''
int main() {
    using openq4::ui::MenuInput;
    MenuInput action=MenuInput::Back;
    assert(MapMenuKey(K_JOY3,action) && action==MenuInput::Accept);
    assert(MapMenuKey(K_JOY4,action) && action==MenuInput::Back);
    assert(!MapMenuKey(K_JOY1,action)); // SDL maps this to the left shoulder.
    inputKeys[101]=K_SHIFT; inputKeys[102]=K_SHIFT;
    assert(MapMenuKey(K_TAB,action) && action==MenuInput::Previous);
    inputKeys.erase(101);
    assert(MapMenuKey(K_TAB,action) && action==MenuInput::Previous);
    inputKeys.clear(); assert(MapMenuKey(K_TAB,action) && action==MenuInput::Next);
    retainedOpen=true;
    assert(SDL3_QueueRetainedKey(K_ENTER,true,false,1050,0));
    assert(queued.size()==1 && queued[0].source==1050 && queued[0].down && !queued[0].repeated);
    assert(SDL3_QueueRetainedKey(K_ENTER,true,true,1050,1));
    assert(queued.back().repeated);
    retainedOpen=false;
    assert(SDL3_QueueRetainedKey(K_ENTER,true,true,1050,2)); // Old-owner repeat must not run a binding.
    assert(queued.size()==2);
    assert(!SDL3_QueueRetainedKey(K_ENTER,false,false,1050,3));
    assert(!SDL3_QueueRetainedKey(K_ENTER,true,false,1050,4));
    retainedOpen=true;
    assert(!SDL3_QueueRetainedKey('`',true,false,1051,5));
    assert(!SDL3_QueueRetainedKey('~',true,false,1051,5));
    consoleObject.active=true;
    assert(!SDL3_QueueRetainedKey(K_UPARROW,true,false,1052,6));
    consoleObject.active=false;
    SDL3_QueueRetainedPointer(91.125f,42.375f,7);
    assert(queued.back().x==91.125f && queued.back().y==42.375f);
    SDL3_QueueRetainedFocus(false,8);
    assert(queued.back().kind==retainedUIInput_t::FOCUS && !queued.back().down);
    assert(!SDL3_QueueRetainedKey(-1,true,false,0,9));
    queued.clear();s_gamepadButtonsDown[0]=true;s_gamepadButtonsDown[1]=true;
    s_gamepadLeftTriggerDown=true;s_gamepadRightTriggerDown=true;
    SDL3_ReleaseGamepadState(10);
    assert(queued.size()==5 && queued.front().kind==retainedUIInput_t::CANCEL);
    for(size_t i=1;i<queued.size();++i) assert(queued[i].kind==retainedUIInput_t::KEY && !queued[i].down);
    assert(!s_gamepadButtonsDown[0] && !s_gamepadButtonsDown[1] && !s_gamepadLeftTriggerDown && !s_gamepadRightTriggerDown);
    queued.clear();s_joystickButtonsDown[0]=true;s_joystickHatState=SDL_HAT_UP|SDL_HAT_RIGHT;
    SDL3_ReleaseJoystickState(11);
    assert(queued.size()==4 && queued.front().kind==retainedUIInput_t::CANCEL);
    for(size_t i=1;i<queued.size();++i) assert(queued[i].kind==retainedUIInput_t::KEY && !queued[i].down);
    assert(!s_joystickButtonsDown[0] && s_joystickHatState==SDL_HAT_CENTERED);

    idUsercmdGenLocal usercmd;
    usercmd.Clear(); retainedOpen=false;
    idKeyInput::bindings['w']=3; idKeyInput::bindings[K_ENTER]=2;
    idKeyInput::bindings[K_F5]=UB_IMPULSE0+4;
    idKeyInput::held['w']=true;
    usercmd.Key('w',true); assert(usercmd.buttonState[3]==1);
    usercmd.mouseDx=73; usercmd.mouseDy=-44;
    physicalAxes[AXIS_YAW]=90; physicalAxes[AXIS_ROLL]=127;
    retainedOpen=true; usercmd.RetainedInputChanged();
    assert(usercmd.Inhibited() && usercmd.buttonState[3]==0);
    assert(usercmd.mouseDx==0 && usercmd.mouseDy==0 && usercmd.filtersReset==1);
    usercmd.Key('w',true); usercmd.Key(K_ENTER,true); usercmd.Key(K_F5,true);
    idKeyInput::held[K_ENTER]=true; idKeyInput::held[K_F5]=true;
    usercmd.SetUsercmdButton(5,true);
    assert(usercmd.buttonState[3]==0 && usercmd.buttonState[2]==0 && usercmd.cmd.flags==0);
    usercmd.Joystick(); assert(usercmd.joystickAxis[AXIS_YAW]==0);
    retainedOpen=false; usercmd.RetainedInputChanged();
    assert(!usercmd.Inhibited());
    usercmd.Key('w',true); usercmd.Key(K_ENTER,true); usercmd.Key(K_F5,true);
    usercmd.SetUsercmdButton(5,true); usercmd.Joystick();
    assert(usercmd.buttonState[2]==0 && usercmd.buttonState[5]==0 && usercmd.cmd.flags==0);
    assert(usercmd.joystickAxis[AXIS_YAW]==0 && usercmd.joystickAxis[AXIS_ROLL]==127);
    usercmd.Key('w',false); usercmd.Key(K_ENTER,false); usercmd.Key(K_F5,false); usercmd.SetUsercmdButton(5,false);
    idKeyInput::held['w']=false; idKeyInput::held[K_ENTER]=false; idKeyInput::held[K_F5]=false;
    physicalAxes[AXIS_YAW]=8; usercmd.Joystick(); assert(usercmd.joystickAxis[AXIS_YAW]==0);
    physicalAxes[AXIS_YAW]=90; usercmd.Joystick(); assert(usercmd.joystickAxis[AXIS_YAW]==90);
    usercmd.Key(K_ENTER,true); usercmd.Key(K_F5,true); usercmd.SetUsercmdButton(5,true);
    assert(usercmd.buttonState[2]==1 && usercmd.buttonState[5]==1 && usercmd.cmd.impulse==4 && usercmd.cmd.flags==1);
    usercmd.inhibitCommands=2; usercmd.RetainedInputChanged(); assert(usercmd.Inhibited());
    usercmd.Clear(); assert(!usercmd.Inhibited());
    retainedOpen=true;usercmd.RetainedInputChanged();usercmd.Key(K_ENTER,true);
    // The ordered UI path sees key-up before the polled path drains it.
    idKeyInput::held[K_ENTER]=false;retainedOpen=false;usercmd.RetainedInputChanged();
    usercmd.Key(K_ENTER,true);assert(usercmd.buttonState[2]==1);
    usercmd.Clear();
    // Exercise the production engine transport/clock dispatcher against a
    // counted runtime sink. The native retained suite separately checks that
    // sink's real layout, hit testing, feedback and action behavior.
    physicalAxes[AXIS_YAW]=0; idKeyInput::ClearStates(); queued.clear(); inputKeys.clear();
    runtime=std::make_unique<FakeRuntime>(); retainedOpen=true;
    physicalAxes[AXIS_YAW]=90;RetainedUI_FrameInput();assert(runtime->received.empty());
    physicalAxes[AXIS_YAW]=0;RetainedUI_FrameInput();assert(!analogNeedsNeutral);
    physicalAxes[AXIS_YAW]=60;RetainedUI_FrameInput();
    assert(runtime->received.size()==1 && runtime->received.back().menu==MenuInput::Right && runtime->received.back().down);
    now=.321;RetainedUI_FrameInput();assert(runtime->received.size()==2);
    physicalAxes[AXIS_YAW]=37;RetainedUI_FrameInput();assert(runtime->received.size()==3 && !runtime->received.back().down);
    physicalAxes[AXIS_YAW]=49;RetainedUI_FrameInput();assert(runtime->received.size()==3);
    physicalAxes[AXIS_YAW]=60;RetainedUI_FrameInput();assert(runtime->received.size()==4);
    physicalAxes[AXIS_YAW]=0;RetainedUI_FrameInput();runtime->received.clear();
    auto send=[&](retainedUIInput_t payload,int generation=-1,int size=sizeof(retainedUIInput_t)) {
        sysEvent_t event{};event.evType=SE_RETAINED_UI;event.evValue=generation<0?inputGeneration:generation;
        event.evPtr=&payload;event.evPtrLength=size;
        assert(RetainedUI_ProcessEvent(&event));
    };
    retainedUIInput_t payload;payload.source=1100;payload.key=K_ENTER;payload.down=1;
    send(payload);assert(runtime->received.size()==1 && runtime->received.back().down && idKeyInput::IsDown(K_ENTER));
    payload.source=1101;payload.key=K_KP_ENTER;send(payload);assert(runtime->received.size()==1);
    payload.source=1100;payload.key=K_ENTER;payload.down=0;send(payload);assert(runtime->received.size()==1);
    payload.source=1101;payload.key=K_KP_ENTER;send(payload);assert(runtime->received.size()==2 && !runtime->received.back().down);
    payload.kind=retainedUIInput_t::POINTER;payload.x=83.125f;payload.y=57.375f;send(payload);
    assert(runtime->x==83.125f && runtime->y==57.375f);
    // Execute actual SDL device-release ordering through the actual decoder.
    // Cancellation disarms first; artificial ups only remove quarantined holds.
    runtime->received.clear();queued.clear();
    SDL3_PostControllerKeyEvent(K_JOY3,true,12);send(queued.back());
    assert(runtime->received.size()==1 && runtime->received.back().down);
    s_gamepadButtonsDown[0]=true;queued.clear();SDL3_ReleaseGamepadState(13);
    assert(queued.size()==2 && queued[0].kind==retainedUIInput_t::CANCEL);
    send(queued[0]);assert(runtime->cancels==1 && analogNeedsNeutral);
    runtime->received.clear();send(queued[1]);assert(runtime->received.empty());
    queued.clear();SDL3_PostControllerKeyEvent(K_JOY3,true,14);send(queued.back());
    assert(runtime->received.size()==1 && runtime->received.back().down);
    SDL3_PostControllerKeyEvent(K_JOY3,false,15);send(queued.back());
    assert(runtime->received.size()==2 && !runtime->received.back().down);
    // A cancelled virtual stick hold must recover after neutral, including
    // document replacement while the physical stick remains deflected.
    physicalAxes[AXIS_YAW]=60;RetainedUI_FrameInput();
    assert(analogDirection==static_cast<int>(MenuInput::Right));
    CancelInput();runtime->received.clear();
    RetainedUI_FrameInput();assert(runtime->received.empty() && analogNeedsNeutral);
    physicalAxes[AXIS_YAW]=0;RetainedUI_FrameInput();assert(!analogNeedsNeutral);
    physicalAxes[AXIS_YAW]=60;RetainedUI_FrameInput();
    assert(runtime->received.size()==1 && runtime->received.back().menu==MenuInput::Right && runtime->received.back().down);
    physicalAxes[AXIS_YAW]=0;RetainedUI_FrameInput();runtime->received.clear();
    payload.kind=retainedUIInput_t::KEY;payload.source=1100;payload.key=K_ENTER;payload.down=1;send(payload);
    payload.kind=retainedUIInput_t::FOCUS;payload.down=0;send(payload);
    assert(inputSuspended && runtime->cancels==3 && !idKeyInput::IsDown(K_ENTER));
    payload.down=1;send(payload);assert(!inputSuspended);
    runtime->received.clear();payload.kind=retainedUIInput_t::KEY;payload.source=1100;payload.key=K_ENTER;payload.repeated=1;send(payload);
    assert(runtime->received.empty());
    payload.repeated=0;send(payload);assert(runtime->received.size()==1);
    input.Cancel();ApplyInput();++inputGeneration;runtime->received.clear();
    payload.down=0;send(payload,inputGeneration-1);
    assert(runtime->received.empty() && !idKeyInput::IsDown(K_ENTER));
    payload.down=1;send(payload,inputGeneration-1);assert(runtime->received.empty());
    // A release queued before the ownership change retires only quarantine;
    // the first fresh press works, and later stale releases cannot end it.
    send(payload);assert(runtime->received.size()==1 && runtime->received.back().down);
    payload.down=0;send(payload,inputGeneration-1);assert(runtime->received.size()==1);
    payload.down=1;send(payload);assert(runtime->received.size()==1);
    payload.down=0;send(payload);assert(runtime->received.size()==2 && !runtime->received.back().down);
    runtime->received.clear();
    payload.kind=retainedUIInput_t::POINTER_BUTTON;payload.down=1;send(payload);
    CancelInput();++inputGeneration;runtime->received.clear();
    payload.down=0;send(payload,inputGeneration-1);assert(runtime->received.empty());
    payload.down=1;send(payload);assert(runtime->received.size()==1 && runtime->received.back().down);
    payload.down=0;send(payload);assert(runtime->received.size()==2 && !runtime->received.back().down);
    runtime->received.clear();payload.kind=retainedUIInput_t::KEY;payload.down=1;
    send(payload,-1,0);assert(runtime->received.empty());
    payload.kind=static_cast<retainedUIInput_t::kind_t>(99);send(payload);assert(runtime->received.empty());
    payload.kind=retainedUIInput_t::KEY;payload.key=K_ESCAPE;payload.source=1102;send(payload);
    assert(closes==1 && !retainedOpen);
    runtime->received.clear();payload.key=K_ENTER;payload.source=1100;payload.down=0;send(payload);
    assert(runtime->received.empty() && !idKeyInput::IsDown(K_ENTER));
    std::puts("retained input: production source mapping, transport generations/focus/disconnect/close, stick recovery, console routing, absolute coordinates and gameplay release gates passed");
}
'''


def main():
    keys=(ROOT/'src/framework/KeyInput.h').read_text()
    key_enum=re.search(r'typedef enum \{.*?\} keyNum_t;', keys, re.S).group()
    system=(ROOT/'src/sys/sys_public.h').read_text()
    event_enum=re.search(r'typedef enum \{\s*SE_NONE,.*?\} sysEventType_t;',system,re.S).group()
    event_struct=re.search(r'typedef struct sysEvent_s \{.*?\} sysEvent_t;',system,re.S).group()
    ui=(ROOT/'src/ui/RetainedUI.cpp').read_text()
    sdl=(ROOT/'src/sys/sdl3/sdl3_backend.cpp').read_text()
    usercmd=(ROOT/'src/framework/UsercmdGen.cpp').read_text()
    functions='\n'.join(function_body(source,signature) for source,signature in [
        (ui,'bool MapMenuKey('),
        (ui,'void ApplyInput('), (ui,'void CancelInput('), (ui,'void SuspendInput('),
        (ui,'void RetainedUI_FrameInput('), (ui,'bool RetainedUI_ProcessEvent('),
        (sdl,'static bool SDL3_QueueRetainedKey('),
        (sdl,'static void SDL3_QueueRetainedPointer('),
        (sdl,'static void SDL3_QueueRetainedFocus('),
        (sdl,'static void SDL3_QueueRetainedCancel('),
        (sdl,'static void SDL3_SetJoystickHat('),
        (sdl,'static void SDL3_ReleaseGamepadState('),
        (sdl,'static void SDL3_ReleaseJoystickState('),
        *[(usercmd,signature) for signature in [
            'bool idUsercmdGenLocal::Inhibited(', 'void idUsercmdGenLocal::Clear(',
            'void idUsercmdGenLocal::Key(', 'void idUsercmdGenLocal::SetButtonAction(',
            'void idUsercmdGenLocal::SetUsercmdButton(', 'void idUsercmdGenLocal::Joystick(',
            'void idUsercmdGenLocal::RetainedInputChanged(',
        ]],
    ])
    compiler=next((found for name in ('clang++','g++','c++') if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-input-',dir=ROOT/'.tmp') as temp:
        source=Path(temp)/'input.cpp'; binary=Path(temp)/'input.exe'
        controller_stub='void SDL3_PostControllerKeyEvent(int key,bool down,int time) { assert(SDL3_QueueRetainedKey(key,down,false,2048+key,time)); }\n'
        source.write_text(key_enum+event_enum+event_struct+SUPPORT+functions+controller_stub+MAIN,encoding='utf-8')
        subprocess.run([compiler,'-std=c++20','-I',str(ROOT),str(source),str(ROOT/'src/ui/retained/Input.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
