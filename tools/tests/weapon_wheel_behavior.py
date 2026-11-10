#!/usr/bin/env python3
"""Execute production radial selection lifecycle without devices or a game window."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
template<class T>T Min(T a,T b){return std::min(a,b);}
template<class T>T Max(T a,T b){return std::max(a,b);}
struct idMath {
    static constexpr float PI=3.14159265358979323846f,TWO_PI=2*PI;
    static float Fabs(float v){return std::fabs(v);}
    static int ClampInt(int lo,int hi,int v){return std::clamp(v,lo,hi);}
    static float ClampFloat(float lo,float hi,float v){return std::clamp(v,lo,hi);}
    static float AngleNormalize180(float v){while(v>180)v-=360;while(v<-180)v+=360;return v;}
};
#define DEG2RAD(v) ((v)*idMath::PI/180.0f)
#define MS2SEC(v) ((v)*.001f)
enum {PITCH,YAW,ROLL};
constexpr int BUTTON_WEAPONWHEEL=512,BUTTON_QUICKCHAT=2048,BUTTON_VRAIM=4096,BUTTON_ZOOM=2,MAX_WEAPONS=16,SPECIAL_EFFECT_BLUR=0,CMD_EXEC_NOW=0;
static int now=1000;
int Sys_Milliseconds(){return now;}
float SHORT2ANGLE(short s){return static_cast<unsigned short>(s)*(360.0f/65536.0f);}
struct idAngles {
    float a[3]{};
    float& operator[](int i){return a[i];}
    void Zero(){for(float& v:a)v=0;}
};
struct idVec2 {
    float x=0,y=0;
    float Length()const{return std::hypot(x,y);}
    void Zero(){x=y=0;}
    void operator*=(float v){x*=v;y*=v;}
};
struct usercmd_t {int buttons=0,mx=0,my=0;short angles[3]{};};
struct CVars {
    std::map<std::string,std::string> values;
    int GetCVarInteger(const char* key){return std::atoi(values[key].c_str());}
    bool GetCVarBool(const char* key){return GetCVarInteger(key)!=0;}
    float GetCVarFloat(const char* key){return std::strtof(values[key].c_str(),nullptr);}
    void SetCVarInteger(const char* key,int value){values[key]=std::to_string(value);}
    void SetCVarBool(const char* key,bool value){SetCVarInteger(key,value);}
    void SetCVarFloat(const char* key,float value){values[key]=std::to_string(value);}
} vars,*cvarSystem=&vars;
struct Game {
    bool isMultiplayer=false,inCinematic=false,teamGame=false,blur=false;
    int localClientNum=0;
    usercmd_t usercmds[1];
    bool IsTeamGame()const{return teamGame;}
    void* GetCamera()const{return nullptr;}
    void SetSpecialEffect(int,bool enabled){blur=enabled;}
    void SetSpecialEffectParm(int,int,float){}
} gameLocal;
struct Common {
    float scale=1;
    void SetGameTimeScale(float value){scale=value;}
    const char* GetLocalizedString(const char* key){return key;}
} commonObject,*common=&commonObject;
struct idStr : std::string {
    using std::string::string;
    void Replace(const char* from,const char* to){size_t p=0;while((p=find(from,p))!=npos){replace(p,std::string(from).size(),to);p+=std::string(to).size();}}
};
struct idCmdArgs {std::vector<std::string> args;void AppendArg(const char* text){args.emplace_back(text);}};
struct Commands {std::vector<idCmdArgs> sent;void BufferCommandArgs(int,const idCmdArgs& args){sent.push_back(args);}} commands,*cmdSystem=&commands;
struct idUserInterface {std::vector<std::string> events;void HandleNamedEvent(const char* name){events.emplace_back(name);}};
class idPlayer {
public:
    int entityNumber=0,currentWeapon=2,previousWeapon=1,health=100;
    bool spectating=false,weaponEnabled=true,objectiveSystemOpen=false,menuOpen=false;
    void* privateCameraView=nullptr;
    struct {bool dead=false;} pfl;
    struct {bool IsDriving()const{return false;}} vehicleController;
    struct {int weapons=(1<<11)-1;} inventory;
    int ammo[MAX_WEAPONS];std::vector<int> impulses;
    usercmd_t usercmd;
    bool GuiActive()const{return menuOpen;}
    void PerformImpulse(int slot){impulses.push_back(slot);}
    @WHEEL_STATE@
    @CHAT_STATE@
    idPlayer(){InitWeaponWheel();InitQuickChat();for(int& count:ammo)count=50;}
};
int idPlayer::GetWeaponWheelSlotCount()const{return 11;}
int idPlayer::GetWeaponWheelWeaponForSlot(int slot)const{return slot;}
int idPlayer::GetWeaponWheelSlotForWeapon(int slot)const{return slot;}
int idPlayer::GetWeaponWheelAmmoCount(int slot){return slot>=0 && slot<MAX_WEAPONS?ammo[slot]:0;}
@CONSTANTS@
@FUNCTIONS@
static int checks=0;
#define CHECK(value) do {++checks;if(!(value)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#value);std::abort();}}while(0)
static void Reset(bool mp=false) {
    gameLocal=Game{};gameLocal.isMultiplayer=mp;commonObject.scale=1;now=1000;commands.sent.clear();vars.values.clear();
    vars.SetCVarBool("ui_retained",true);vars.SetCVarBool("ui_radialStickActive",true);
}
static void Stick(float x,float y){vars.SetCVarFloat("ui_radialStickX",x);vars.SetCVarFloat("ui_radialStickY",y);}
static void Slot(int slot,int count=11){const float a=slot*idMath::TWO_PI/count;Stick(std::sin(a),-std::cos(a));}
static void Open(idPlayer& p){p.usercmd.buttons=BUTTON_WEAPONWHEEL;p.UpdateWeaponWheel();CHECK(p.weaponWheelActive);}
int main() {
    // Touch has already qualified its 300ms hold in the overlay. Show its
    // wheel immediately, steer independently of mouse/controller settings,
    // and release the selected weapon even inside the controller tap window.
    Reset();{idPlayer p;vars.SetCVarBool("ui_radialTouchActive",true);
        const float a=5*idMath::TWO_PI/11;
        vars.SetCVarFloat("ui_radialTouchX",std::sin(a));vars.SetCVarFloat("ui_radialTouchY",-std::cos(a));
        Open(p);now+=16;p.usercmd.mx=100;p.usercmd.my=-100;Stick(0,-1);p.UpdateWeaponWheel();
        CHECK(p.weaponWheelHoveredSlot==5 && p.weaponWheelBlend>0);
        p.usercmd.buttons=0;p.UpdateWeaponWheel();CHECK(p.impulses==std::vector<int>{5});
    }
    // Any sub-180ms tap uses previous, even after the pointer crossed to a
    // different weapon; neither the wheel nor transient scene focus appears.
    for(int duration:{0,80,179}) {
        Reset();idPlayer p;Open(p);Slot(5);now+=duration;p.UpdateWeaponWheel();
        CHECK(p.weaponWheelHoveredSlot==5 && p.weaponWheelBlend==0 && common->scale==1);
        p.usercmd.buttons=0;p.UpdateWeaponWheel();
        CHECK(!p.weaponWheelActive && p.impulses==std::vector<int>{1} && p.weaponWheelBlend==0);
        p.UpdateWeaponWheel();CHECK(p.impulses.size()==1 && vars.GetCVarInteger("ui_radialOwner")==0);
    }
    // At the boundary and beyond, release equips the selected weapon once.
    for(bool mp:{false,true}) {
        Reset(mp);idPlayer p;Open(p);Slot(5);now+=180;p.UpdateWeaponWheel();
        CHECK(p.weaponWheelBlend>0 && p.weaponWheelHoveredSlot==5);
        CHECK(mp ? common->scale==1 : common->scale<1);
        p.usercmd.buttons=0;now+=16;p.UpdateWeaponWheel();
        CHECK(p.impulses==std::vector<int>{5});p.UpdateWeaponWheel();CHECK(p.impulses.size()==1);
        now+=100;p.UpdateWeaponWheel();now+=100;p.UpdateWeaponWheel();
        CHECK(p.weaponWheelBlend==0 && common->scale==1);
    }
    // Unowned positions hold their previous selection; an empty owned slot
    // is visible/selectable but release refuses the equip.
    Reset();{idPlayer p;p.inventory.weapons&=~(1<<5);Open(p);Slot(5);now+=200;p.UpdateWeaponWheel();CHECK(p.weaponWheelHoveredSlot==2);}
    Reset();{idPlayer p;p.ammo[5]=0;Open(p);Slot(5);now+=200;p.UpdateWeaponWheel();CHECK(p.weaponWheelHoveredSlot==5);p.usercmd.buttons=0;p.UpdateWeaponWheel();CHECK(p.impulses.empty());}
    // A Back request closes with its animation, consumes the request, and
    // cannot reopen until the original held button is released.
    Reset();{idPlayer p;Open(p);now+=200;p.UpdateWeaponWheel();const float blend=p.weaponWheelBlend;
        vars.SetCVarBool("ui_radialCancel",true);p.UpdateWeaponWheel();
        CHECK(!p.weaponWheelActive && p.weaponWheelCanceled && p.weaponWheelBlend==blend && p.impulses.empty());
        CHECK(!vars.GetCVarBool("ui_radialCancel") && vars.GetCVarInteger("ui_radialOwner")==0);
        now+=30;p.UpdateWeaponWheel();CHECK(!p.weaponWheelActive && p.weaponWheelBlend<blend && p.impulses.empty());
        p.usercmd.buttons=0;p.UpdateWeaponWheel();Open(p);CHECK(!p.weaponWheelCanceled);
    }
    // Losing gameplay ownership cancels the invocation even before it is
    // visible, or while its closing fade could still be on screen.
    for(int duration:{80,200}) {
        Reset();idPlayer p;Open(p);Slot(5);now+=duration;p.UpdateWeaponWheel();
        p.menuOpen=true;p.UpdateWeaponWheel();CHECK(!p.weaponWheelActive && p.weaponWheelCanceled && p.impulses.empty());
        p.menuOpen=false;now+=16;p.UpdateWeaponWheel();CHECK(!p.weaponWheelActive && p.impulses.empty());
        p.usercmd.buttons=0;p.UpdateWeaponWheel();Open(p);CHECK(!p.weaponWheelCanceled);
    }
    // The quick-chat receiver is counted. It gets one localized phrase on
    // All, and reopening without choosing anything sends nothing.
    Reset(true);{idPlayer p;p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();CHECK(p.quickChatActive && !p.quickChatTeam);
        Slot(7,8);now+=100;p.UpdateQuickChat();CHECK(p.quickChatSelected==7);
        p.usercmd.buttons=0;p.UpdateQuickChat();CHECK(commands.sent.size()==1);
        CHECK(commands.sent[0].args==std::vector<std::string>({"say","#str_231268"}));
        p.UpdateQuickChat();CHECK(commands.sent.size()==1);
        Stick(0,0);p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();p.usercmd.buttons=0;p.UpdateQuickChat();CHECK(commands.sent.size()==1);
    }
    // A real selection is retained when the stick returns to neutral, yet
    // releasing there sends nothing. This is distinct from no selection.
    Reset(true);{idPlayer p;p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();Slot(2,8);now+=100;p.UpdateQuickChat();CHECK(p.quickChatSelected==2);
        Stick(0,0);p.UpdateQuickChat();CHECK(p.quickChatSelected==2 && p.quickChatCursor.Length()==0);
        p.usercmd.buttons=0;p.UpdateQuickChat();CHECK(commands.sent.empty());
    }
    // A zoom held during opening must not flip Team to All. A fresh zoom
    // press changes the channel once, regardless of subsequent held frames.
    Reset(true);gameLocal.teamGame=true;{idPlayer p;gameLocal.usercmds[0].buttons=BUTTON_ZOOM;p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();CHECK(p.quickChatTeam);
        p.UpdateQuickChat();CHECK(p.quickChatTeam);gameLocal.usercmds[0].buttons=0;p.UpdateQuickChat();
        gameLocal.usercmds[0].buttons=BUTTON_ZOOM;p.UpdateQuickChat();CHECK(!p.quickChatTeam);p.UpdateQuickChat();CHECK(!p.quickChatTeam);
        gameLocal.usercmds[0].buttons=0;p.UpdateQuickChat();gameLocal.usercmds[0].buttons=BUTTON_ZOOM;p.UpdateQuickChat();CHECK(p.quickChatTeam);
        Slot(0,8);p.UpdateQuickChat();p.usercmd.buttons=0;p.UpdateQuickChat();CHECK(commands.sent.size()==1 && commands.sent[0].args[0]=="sayTeam");
    }
    // Back and menu takeover are unsent; a held originating control cannot
    // immediately open a new wheel. Dead/spectating players may still chat.
    Reset(true);{idPlayer p;p.health=0;p.spectating=true;p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();CHECK(p.quickChatActive);
        Slot(3,8);now+=100;p.UpdateQuickChat();vars.SetCVarBool("ui_radialCancel",true);p.UpdateQuickChat();
        CHECK(!p.quickChatActive && p.quickChatCanceled && commands.sent.empty() && !vars.GetCVarBool("ui_radialCancel"));
        p.UpdateQuickChat();CHECK(!p.quickChatActive);p.usercmd.buttons=0;p.UpdateQuickChat();
        p.usercmd.buttons=BUTTON_QUICKCHAT;p.UpdateQuickChat();Slot(2,8);p.UpdateQuickChat();p.menuOpen=true;p.UpdateQuickChat();
        CHECK(!p.quickChatActive && p.quickChatCanceled && commands.sent.empty());
    }
    std::printf("weapon/quick-chat lifecycle: %d checks passed\n",checks);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'.tmp').resolve()):
        parser.error('use a project-local .tmp output')
    output.mkdir(parents=True,exist_ok=True)
    paths={name:ROOT/path for name,path in {
        'wheel':'src/game/gamesys/WeaponWheel.inl','classic':'src/game/gamesys/WeaponWheelLegacy.inl',
        'chat':'src/mpgame/QuickChat.inl','wheel_state':'src/game/gamesys/WeaponWheelState.h',
        'chat_state':'src/mpgame/QuickChatState.h'}.items()}
    source={name:path.read_text(encoding='utf-8-sig') for name,path in paths.items()}
    functions=[]
    for key,signatures in {
        'classic':['float WeaponWheelLerp(','float WeaponWheelNormalizeAngle('],
        'wheel':['void idPlayer::InitWeaponWheel(','bool idPlayer::CanUseWeaponWheel(','void idPlayer::ResetWeaponWheel(',
                 'bool idPlayer::HasWeaponWheelSlotWeapon(','int idPlayer::GetDefaultWeaponWheelSlot(',
                 'int idPlayer::GetClosestAvailableWeaponWheelSlot(','static float WeaponWheelDeadzoneRadius(',
                 'static void WeaponWheelAdvanceCursor(','void idPlayer::UpdateWeaponWheelCursor(',
                 'void idPlayer::UpdateWeaponWheelEffects(','void idPlayer::UpdateWeaponWheel('],
        'chat':['void idPlayer::InitQuickChat(','bool idPlayer::CanUseQuickChat(',
                'void idPlayer::ResetQuickChat(','void idPlayer::UpdateQuickChat(']}.items():
        functions.extend(function_body(source[key],signature) for signature in signatures)
    constants=[]
    for name in ['CURSOR_RADIUS','DEADZONE_RADIUS','MOUSE_SENSITIVITY','TIMESCALE_SCALE','BLEND_IN_SPEED','BLEND_OUT_SPEED']:
        constants.append(re.search(r'const float\s+WEAPON_WHEEL_'+name+r'\s*=.*?;',source['classic']).group())
    constants.append(source['chat'][source['chat'].index('static const char *QUICK_CHAT_PHRASES'):source['chat'].index('void idPlayer::InitQuickChat')])
    cpp=SUPPORT.replace('@WHEEL_STATE@',source['wheel_state']).replace('@CHAT_STATE@',source['chat_state'])
    cpp=cpp.replace('@CONSTANTS@','\n'.join(constants)).replace('@FUNCTIONS@','\n'.join(functions))
    compiler=shutil.which('clang++') or shutil.which('g++')
    if not compiler:raise RuntimeError('clang++ or g++ required')
    code=output/'behavior.cpp';binary=output/'behavior.exe';code.write_text(cpp,encoding='utf-8')
    env={**os.environ,'TMP':str(output),'TEMP':str(output),'TMPDIR':str(output)}
    build=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(code),'-o',str(binary)],cwd=output,env=env,text=True,capture_output=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    build.check_returncode()
    run=subprocess.run([str(binary)],cwd=output,env=env,text=True,capture_output=True)
    (output/'run.log').write_text(run.stdout+run.stderr)
    (output/'result.json').write_text(json.dumps({'passed':run.returncode==0,'output':run.stdout+run.stderr,
        'sources':{str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths.values()},
        'projection_sha256':hashlib.sha256(code.read_bytes()).hexdigest()},indent=2)+'\n')
    print(run.stdout+run.stderr,end='');run.check_returncode()


if __name__=='__main__':main()
