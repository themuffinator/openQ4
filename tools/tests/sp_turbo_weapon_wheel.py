#!/usr/bin/env python3
"""Execute production turbo/reload, new-game handoff and wheel input code.

Small engine adapters let these regressions run without game assets or input
devices. The tested methods and state guards are extracted from the real sources.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GAME = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT))


def read(path):
    return path.read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct idMath {
    static int ClampInt(int a,int b,int v){return std::clamp(v,a,b);}
    static int ClampChar(int v){return ClampInt(-128,127,v);}
    static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
    static float AngleNormalize180(float v){while(v>180)v-=360;while(v<-180)v+=360;return v;}
    static float Fabs(float v){return std::fabs(v);}
    static constexpr float TWO_PI=6.2831853071795864769f;
    static constexpr float PI=TWO_PI*.5f;
};
#define DEG2RAD(v) ((v)*idMath::TWO_PI/360.0f)
struct idStr {
    std::string s;
    idStr(const char* v=""):s(v){}
    const char* c_str()const{return s.c_str();}
    static int Icmp(const char* a,const char* b){return std::string(a).compare(b);}
};
const char* va(const char* fmt,...) {
    static char b[256]; va_list v; va_start(v,fmt);vsnprintf(b,sizeof(b),fmt,v);va_end(v);return b;
}
struct CVars {
    std::map<std::string,std::string> values;
    int GetCVarInteger(const char* n){return atoi(values[n].c_str());}
    bool GetCVarBool(const char* n){return GetCVarInteger(n)!=0;}
    float GetCVarFloat(const char* n){return std::strtof(values[n].c_str(),nullptr);}
    const char* GetCVarString(const char* n){return values[n].c_str();}
    void SetCVarInteger(const char* n,int v){values[n]=std::to_string(v);}
    void SetCVarBool(const char* n,bool v){SetCVarInteger(n,v);}
    void SetCVarFloat(const char* n,float v){values[n]=std::to_string(v);}
} vars, *cvarSystem=&vars;
struct idKeyInput {static bool controller;static bool LastInputWasController(){return controller;}static bool LastPointerWasTouch(){return false;}};
bool idKeyInput::controller=false;
struct CVar {float v=0; bool GetBool()const{return v!=0;} float GetFloat()const{return v;}};
CVar g_turboMode, in_angleSpeedKey{1.5f}, in_yawSpeed{140}, in_pitchSpeed{140};
struct UserInfo { bool GetBool(const char*)const{return true;} };
struct Game {bool isMultiplayer=false,isClient=false;int time=1000;UserInfo userInfo[1];float aspect=16.0f/9.0f;float GetScreenAspectRatio()const{return aspect;}} gameLocal;
struct Common {float seconds=1.0f/60;float GetUserCmdSec()const{return seconds;}
    void Printf(const char*,...){} } commonObject,*common=&commonObject;
@TURBO@
static bool OpenQ4_TurboWeaponReloadsDisabled(){return OpenQ4_TurboModeActive();}
struct Inventory {
    int ammo=40;
    int HasAmmo(int,int required)const{return ammo<0?-1:(required?ammo/required:-1);}
    void UseAmmo(int,int n){if(ammo>=0)ammo=std::max(0,ammo-n);}
};
struct Owner {Inventory inventory;int entityNumber=0;float PowerUpModifier(int){return 1;}} owner;
struct View {void PostGUIEvent(const char*){}} view;
enum {WP_READY,WP_OUTOFAMMO,ANIMCHANNEL_ALL,PMOD_FIRERATE,SRESULT_DONE,SRESULT_WAIT,SRESULT_ERROR};
using stateResult_t=int;
#define SRESULT_STAGE(x) (100+(x))
struct stateParms_t {int blendFrames=0,time=1000,stage=0;};
struct rvWeapon {
    Owner* owner=&::owner;View* viewModel=&view;
    int clipSize=8,ammoClip=8,ammoType=0,ammoRequired=1;
    int fireRate=800,nextAttackTime=0,hitscans=10;float spread=7;
    int shots=0,ejected=0,status=WP_READY;std::string state,anim;
    struct {bool reload=false,netReload=false,netEndReload=false,lowerWeapon=false,attack=false;} wsfl;
    int AmmoAvailable()const;int AmmoInClip()const;int ClipSize()const;
    void Reload();bool AutoReload();bool SkipReload();void UseAmmo(int);
    void SetState(const char* s,int){state=s;}
    void SetRocketState(const char* s,int){state=s;}
    void SetStatus(int s){status=s;}
    void PlayCycle(int,const char* s,int){anim=s;}
    void PlayAnim(int,const char* s,int){anim=s;}
    bool AnimDone(int,int){return false;}
    void EjectBrass(){++ejected;}
    void Attack(bool,int,float,int,float){++shots;UseAmmo(1);}
    @GUARDS@
    @ROCKET_GUARD@
};
struct rvWeaponShotgun:rvWeapon {int State_Idle(const stateParms_t&);int State_Fire(const stateParms_t&);};
@WEAPON@
@SHOTGUN@
struct idCmdArgs {
    std::vector<std::string> values;
    void AppendArg(const char* s){values.emplace_back(s);}
    int Argc()const{return static_cast<int>(values.size());}
    const char* Argv(int i)const{return values.at(i).c_str();}
};
struct Session {bool started=false,turbo=false,dev=false;int skill=-1;std::string map,filter;
    void StartNewGame(const char* m,bool d,const char* f){started=true;map=m;dev=d;filter=f;
        turbo=cvarSystem->GetCVarBool("g_turboMode");skill=cvarSystem->GetCVarInteger("g_skill");}
} sessLocal;
@HANDOFF@
struct idWinVar {std::string s;const char* c_str()const{return s.c_str();}};
struct idWindow {
    std::map<std::string,idWinVar> values;
    idWinVar* GetWinVarByName(const char* n,bool){auto i=values.find(n);return i==values.end()?nullptr:&i->second;}
};
struct Dict {
    std::map<std::string,int> values;
    bool GetInt(const char* n,const char* fallback,int& value){auto i=values.find(n);
        value=i==values.end()?atoi(fallback):i->second;return i!=values.end();}
};
struct idUserInterface {
    idWindow desktop;Dict dict;Dict& State(){return dict;}
    bool GetPresentationValue(const char* name,idStr& out) {
        const char* local=std::strncmp(name,"desktop::",9)==0?name+9:name;
        auto found=desktop.values.find(local);
        if(found==desktop.values.end())return false;
        out=idStr(found->second.s.c_str());return true;
    }
};
@MENU_OPTION@
enum {PITCH,YAW,ROLL,AXIS_ROLL=0,AXIS_SIDE,AXIS_FORWARD,AXIS_YAW,AXIS_PITCH,AXIS_UP,UB_STRAFE};
constexpr int BUTTON_WEAPONWHEEL=512,BUTTON_QUICKCHAT=2048,BUTTON_VRAIM=4096;
static int Sys_Milliseconds(){return 0;}
constexpr float JOYSTICK_AXIS_LOOK_SCALE=1.0f/127.0f;
float SHORT2ANGLE(short s){return (unsigned short)s*(360.0f/65536.0f);}
short ANGLE2SHORT(float a){return static_cast<short>(static_cast<int>(a*65536.0f/360.0f)&65535);}
struct idAngles {float values[3]{};float& operator[](int i){return values[i];}const float& operator[](int i)const{return values[i];}};
struct Vec2 {float x=0,y=0;float Length()const{return std::hypot(x,y);}void operator*=(float s){x*=s;y*=s;}};
struct Usercmd {int buttons=0,mx=0,my=0,rightmove=0,forwardmove=0,upmove=0;short angles[3]{};};
using usercmd_t=Usercmd;using idVec2=Vec2;
struct idPlayer {
    Usercmd usercmd;int weaponWheelLastMouseX=0,weaponWheelLastMouseY=0;
    idAngles weaponWheelLastCmdAngles;Vec2 weaponWheelCursor;
    void UpdateWeaponWheelCursor();
    int weaponWheelHoveredSlot=0,slotCount=11,owned=(1<<13)-1;
    int GetWeaponWheelSlotCount()const{return slotCount;}
    bool HasWeaponWheelSlotWeapon(int slot)const{return slot>=0 && slot<slotCount && (owned&(1<<slot));}
    int GetClosestAvailableWeaponWheelSlot(float angle)const;
};
constexpr float WEAPON_WHEEL_MOUSE_SENSITIVITY=.70f,WEAPON_WHEEL_CURSOR_RADIUS=118,WEAPON_WHEEL_DEADZONE_RADIUS=24;
template<class T> T Min(T a,T b){return std::min(a,b);}
template<class T> T Max(T a,T b){return std::max(a,b);}
using Sint16=short;
@RADIAL@
@WHEEL@
struct idUsercmdGenLocal {
    int joystickAxis[6]{};Usercmd cmd;idAngles viewangles;bool run=false,strafe=false;float zoom=1;
    // The held scoreboard's stick scrolling (14.14): scrolled rows, never pitch.
    bool scoreboardScrolls=false;int scoreboardStickDirection=0,scoreboardStickRepeat=0,scrolled=0;
    bool IsRunButtonActive(){return run;}bool ButtonState(int){return strafe;}
    float GetZoomLookSensitivityScale(){return zoom;}void JoystickMove();
    bool ScoreboardScrolls(){return scoreboardScrolls;}void ScrollScoreboard(int rows){scrolled+=rows;}
};
@JOYSTICK@
int main() {
    // Scale follows the HUD preference, but physical ring bounds stay inside
    // every safe inset and both viewport dimensions without saving a clamp.
    for(float aspect:{.5625f,4.0f/3.0f,16.0f/9.0f,32.0f/9.0f})for(float scale:{.5f,1.0f,1.25f,2.0f})for(float inset:{0.0f,2.5f,10.0f})for(float preference:{-40.0f,0.0f,27.0f,40.0f}) {
        gameLocal.aspect=aspect;vars.SetCVarFloat("ui_hudScale",scale);vars.SetCVarFloat("ui_hudSafeInset",inset);vars.SetCVarFloat("ui_weaponWheelOffset",preference);
        float offset=0,fit=0;WeaponWheelPlacement(offset,fit);const float width=720*aspect;
        assert(fit<=scale+.0001f && fit>0);
        assert(560*fit<=width-2*std::max(16.0f,width*inset*.01f)+.001f);
        assert(560*fit<=720-2*std::max(16.0f,720*inset*.01f)+.001f);
        assert(std::fabs(offset)+264*fit<=width*.5f-std::max(16.0f,width*inset*.01f)+.001f);
        assert(vars.GetCVarFloat("ui_hudScale")==scale && vars.GetCVarFloat("ui_weaponWheelOffset")==preference);
        if(preference==0)assert(offset==0);
        if(aspect>1.3f && scale<=1 && inset<=2.5f)assert(std::fabs(fit-scale)<.001f);
    }
    gameLocal.aspect=16.0f/9.0f;vars.SetCVarFloat("ui_hudScale",1);vars.SetCVarFloat("ui_hudSafeInset",0);vars.SetCVarFloat("ui_weaponWheelOffset",27);
    {float offset,fit;WeaponWheelPlacement(offset,fit);assert(std::fabs(offset-345.6f)<.001f && fit==1);}
    // Off is authoritative even if a module-local handle still says On.
    vars.SetCVarBool("g_turboMode",false);g_turboMode.v=1;
    assert(!OpenQ4_TurboModeActive());
    vars.SetCVarBool("g_turboMode",true);assert(OpenQ4_TurboModeActive());
    gameLocal.isMultiplayer=true;assert(!OpenQ4_TurboModeActive());gameLocal.isMultiplayer=false;
    rvWeaponShotgun w;stateParms_t p;p.stage=1;w.ammoClip=0;w.wsfl.attack=true;
    // Shoot more than a full magazine, including with an initially empty clip.
    for(int n=0;n<24;++n) {
        w.state.clear();w.State_Idle(p);assert(w.state=="Fire");
        p.stage=0;p.time=gameLocal.time;w.State_Fire(p);
        assert(w.AmmoAvailable()==39-n && w.AmmoInClip()==39-n && w.ClipSize()==0);
        w.Reload();assert(!w.wsfl.reload && !w.AutoReload());
        p.stage=1;gameLocal.time=p.time+249;w.state.clear();w.State_Fire(p);assert(w.state.empty());
        ++gameLocal.time;w.State_Fire(p);assert(w.state=="Idle");
        gameLocal.time=p.time+801;
    }
    assert(w.shots==24 && w.ejected==24);
    // Every reload entry/continuation (including restored stages) is bypassed.
    @GUARD_TESTS@
    for(int stage=0;stage<2;++stage){p.stage=stage;assert(w.RocketReload(p)==SRESULT_DONE && w.state=="Rocket_Idle");}
    owner.inventory.ammo=0;w.state.clear();w.State_Idle(p);assert(w.state!="Fire");
    vars.SetCVarBool("g_turboMode",false);owner.inventory.ammo=40;w.ammoClip=8;
    w.UseAmmo(1);assert(w.AmmoAvailable()==39 && w.AmmoInClip()==7 && w.ClipSize()==8);
    w.Reload();assert(w.wsfl.reload && w.AutoReload() && !w.SkipReload());
    assert(w.RocketReload(p)==-1);
    p.time=gameLocal.time-300;w.state.clear();w.State_Fire(p);assert(w.ejected==24);
    // Preserve both Off and On after config replay, with and without a filter.
    for(int turbo:{0,1})for(const char* filter:{"","second"}) {
        vars.SetCVarInteger("g_turboMode",turbo);vars.SetCVarInteger("g_skill",4);
        idCmdArgs reloadArgs;idStr normalizedMapName("game/airdefense1"),normalizedEntityFilter(filter);bool devmap=true;
        @CAPTURE@
        vars.SetCVarInteger("g_turboMode",1-turbo);vars.SetCVarInteger("g_skill",1);
        Session_openQ4StartSingleplayer_f(reloadArgs);
        assert(sessLocal.started && sessLocal.turbo==bool(turbo) && sessLocal.skill==4 && sessLocal.dev);
        assert(sessLocal.map=="game/airdefense1" && sessLocal.filter==filter);
    }
    idUserInterface gui;gui.desktop.values["turboMode"].s="0";gui.dict.values["turboMode"]=1;
    assert(MainMenuGetNewGameOption(&gui,"desktop::turboMode","turboMode",1)==0);
    gui.desktop.values["turboMode"].s="1";
    assert(MainMenuGetNewGameOption(&gui,"desktop::turboMode","turboMode",0)==1);
    // Controller wheel travel is independent of slow aim, zoom, run and tic rate.
    vars.values["com_activeGameModule"]="game_sp";
    for(float sensitivity:{.25f,4.0f,16.0f})for(int hz:{30,60,120})for(float aim:{.1f,.75f,4.0f}) {
        vars.values["in_weaponWheelSensitivity"]=std::to_string(sensitivity);
        vars.values["in_joystickLookSensitivity"]=std::to_string(aim);
        common->seconds=1.0f/hz;idUsercmdGenLocal gen;idPlayer player;
        gen.cmd.buttons=BUTTON_WEAPONWHEEL;gen.joystickAxis[AXIS_ROLL]=1;gen.joystickAxis[AXIS_SIDE]=127;
        gen.zoom=.1f;gen.run=true;
        for(int i=0;i<hz/10;++i){gen.JoystickMove();player.usercmd.angles[YAW]=ANGLE2SHORT(gen.viewangles[YAW]);player.UpdateWeaponWheelCursor();}
        assert(std::fabs(player.weaponWheelCursor.x-std::min(118.0f,14.0f*sensitivity))<.1f);
        assert(player.weaponWheelCursor.y==0);
    }
    // Down/up and mouse direction, no double counting, radial clamp and yaw wrap.
    vars.values["in_weaponWheelSensitivity"]="4";
    for(int sign:{-1,1}){idPlayer p2;p2.usercmd.angles[PITCH]=ANGLE2SHORT(sign*10.0f);p2.UpdateWeaponWheelCursor();assert(std::fabs(p2.weaponWheelCursor.y-sign*40)<.1f);}
    idPlayer mouse;mouse.usercmd.mx=10;mouse.usercmd.my=-20;mouse.usercmd.angles[YAW]=ANGLE2SHORT(-100);
    mouse.UpdateWeaponWheelCursor();assert(mouse.weaponWheelCursor.x==7 && mouse.weaponWheelCursor.y==-14);
    idPlayer wrap;wrap.weaponWheelLastCmdAngles[YAW]=1;wrap.usercmd.angles[YAW]=ANGLE2SHORT(359);wrap.UpdateWeaponWheelCursor();assert(std::fabs(wrap.weaponWheelCursor.x-8)<.1f);
    // Native controller selection points directly, independently of frame rate,
    // slow aim, wheel sensitivity and look inversion. Returning to neutral
    // actually returns to the dead zone so quick chat can decline to send.
    vars.SetCVarBool("ui_retained",true);idKeyInput::controller=true;
    for(int hz:{30,60,120})for(bool inverted:{false,true})for(float deadzone:{0.0f,.18f,.5f})for(float curve:{1.0f,1.35f,3.0f}) {
        common->seconds=1.0f/hz;vars.SetCVarBool("in_joystickInvertLook",inverted);
        idUsercmdGenLocal gen;idPlayer player;gen.cmd.buttons=BUTTON_WEAPONWHEEL;
        gen.joystickAxis[AXIS_ROLL]=1;gen.joystickAxis[AXIS_SIDE]=127;
        auto sampled=[&](Sint16 x,Sint16 y) {
            SDL3_PublishRadialStick(x,y);
            SDL3_NormalizeStickAxes(x,y,deadzone,curve,1.0f,gen.joystickAxis[AXIS_SIDE],gen.joystickAxis[AXIS_FORWARD]);
            if(inverted)gen.joystickAxis[AXIS_FORWARD]=-gen.joystickAxis[AXIS_FORWARD];
            gen.JoystickMove();player.UpdateWeaponWheelCursor();
        };
        sampled(32767,0);
        for(int frame=0;frame<hz;++frame){gen.JoystickMove();player.UpdateWeaponWheelCursor();assert(std::fabs(player.weaponWheelCursor.x-118)<.001f);}
        sampled(0,16384);assert(std::fabs(player.weaponWheelCursor.y-118.0f*16384/32767)<.001f);
        sampled(11466,0);assert(player.weaponWheelCursor.Length()<WeaponWheelDeadzoneRadius()); // 34.99% physical
        sampled(11472,0);assert(player.weaponWheelCursor.Length()>WeaponWheelDeadzoneRadius()); // 35.01% physical
        sampled(8108,8108);assert(player.weaponWheelCursor.Length()<WeaponWheelDeadzoneRadius());
        sampled(8112,8112);assert(player.weaponWheelCursor.Length()>WeaponWheelDeadzoneRadius());
        sampled(-32768,0);assert(std::fabs(player.weaponWheelCursor.x+118)<.001f);
        sampled(0,0);assert(player.weaponWheelCursor.Length()==0);
    }
    idKeyInput::controller=false;vars.SetCVarBool("ui_radialStickActive",false);
    assert(WeaponWheelDeadzoneRadius()==32);
    // Fixed slots do not jump over unowned weapons, and hysteresis extends
    // four degrees beyond each boundary in either direction, including zero.
    for(int slots:{11,13}) {
        idPlayer player;player.slotCount=slots;const float slice=idMath::TWO_PI/slots;
        assert(player.GetClosestAvailableWeaponWheelSlot(slice*.5f+DEG2RAD(3.9f))==0);
        assert(player.GetClosestAvailableWeaponWheelSlot(slice*.5f+DEG2RAD(4.1f))==1);
        player.owned&=~(1<<1);assert(player.GetClosestAvailableWeaponWheelSlot(slice)==0);
        assert(player.GetClosestAvailableWeaponWheelSlot(idMath::TWO_PI-slice*.5f-DEG2RAD(3.9f))==0);
        assert(player.GetClosestAvailableWeaponWheelSlot(idMath::TWO_PI-slice*.5f-DEG2RAD(4.1f))==slots-1);
    }
    vars.SetCVarBool("ui_retained",false);vars.SetCVarBool("in_joystickInvertLook",false);
    // Normal aiming keeps its sensitivity in either module.
    for(const char* module:{"game_sp","game_mp"}) {
        vars.values["com_activeGameModule"]=module;vars.values["in_joystickLookSensitivity"]="0.5";
        idUsercmdGenLocal gen;gen.cmd.buttons=0;
        gen.joystickAxis[AXIS_ROLL]=1;gen.joystickAxis[AXIS_SIDE]=127;common->seconds=1.0f/60;
        gen.JoystickMove();assert(std::fabs(gen.viewangles[YAW]+140.0f*.5f/60)<.001f);
    }
    // The quick-chat wheel steers like the weapon wheel: no slow aim, zoom or run.
    {
        vars.values["in_joystickLookSensitivity"]="0.1";idUsercmdGenLocal gen;gen.cmd.buttons=BUTTON_QUICKCHAT;
        gen.joystickAxis[AXIS_ROLL]=1;gen.joystickAxis[AXIS_SIDE]=127;gen.zoom=.1f;gen.run=true;common->seconds=1.0f/60;
        gen.JoystickMove();assert(std::fabs(gen.viewangles[YAW]+140.0f/60)<.001f);
    }
    // While the held scoreboard scrolls, a push past half the stick's travel
    // scrolls a row and the view's pitch stays put; the first repeat waits.
    {
        idUsercmdGenLocal gen;gen.scoreboardScrolls=true;gen.joystickAxis[AXIS_ROLL]=1;gen.joystickAxis[AXIS_FORWARD]=100;
        gen.JoystickMove();assert(gen.scrolled==1 && gen.viewangles[PITCH]==0);
        gen.JoystickMove();assert(gen.scrolled==1);
        gen.joystickAxis[AXIS_FORWARD]=0;gen.JoystickMove();gen.joystickAxis[AXIS_FORWARD]=-100;
        gen.JoystickMove();assert(gen.scrolled==0 && gen.viewangles[PITCH]==0);
    }
    puts("turbo reload/ammo/shotgun, new-game Off/On handoff, wheel speed/direction/tic-rate: PASS");
}
'''


def main():
    weapon = read(GAME / "src/game/Weapon.cpp")
    shotgun = read(GAME / "src/game/weapon/WeaponShotgun.cpp")
    player = read(GAME / "src/game/gamesys/WeaponWheel.inl")
    session = read(ROOT / "src/framework/Session.cpp")
    guards, guard_tests = [], []
    for path in sorted((GAME / "src/game/weapon").glob("*.cpp")):
        source = read(path)
        for match in re.finditer(r"stateResult_t \w+::(State_(?:Empty)?Reload)\s*\(", source):
            body = function(source, match[0]).split("{", 1)[1].split("\tenum", 1)[0]
            name = path.stem + "_" + match[1]
            guards.append(f"int {name}(const stateParms_t&) {{ {body} return -1; }}")
            guard_tests.append(f'''for(int stage=0;stage<8;++stage){{p.stage=stage;w.wsfl.reload=true;
                assert(w.{name}(p)==SRESULT_DONE && !w.wsfl.reload && w.state=="Idle");}}
                w.wsfl.lowerWeapon=true;assert(w.{name}(p)==SRESULT_DONE && w.state=="Lower");w.wsfl.lowerWeapon=false;''')
    assert len(guards) == 9, "Cover every stock weapon reload state, including empty-magazine reloads"
    rocket = function(read(GAME / "src/game/weapon/WeaponRocketLauncher.cpp"),
                      "stateResult_t rvWeaponRocketLauncher::State_Rocket_Reload")
    rocket_guard = rocket.split("{", 1)[1].split("\tenum", 1)[0]
    capture = function(session, "void idSessionLocal::StartNewGame(")
    capture = capture[capture.index('\t\treloadArgs.AppendArg( "openq4_startSingleplayer" );'):capture.index('\t\tcmdSystem->SetupReloadGameModule')]
    substitutions = {
        "TURBO": function(read(GAME / "src/game/Game_local.cpp"), "bool OpenQ4_TurboModeActive("),
        "WEAPON": "\n".join(function(weapon, signature) for signature in (
            "int rvWeapon::AmmoAvailable(", "int rvWeapon::AmmoInClip(", "int\trvWeapon::ClipSize(",
            "void rvWeapon::UseAmmo", "void rvWeapon::Reload(", "bool rvWeapon::AutoReload", "bool rvWeapon::SkipReload")),
        "SHOTGUN": "\n".join(function(shotgun, "stateResult_t rvWeaponShotgun::" + method) for method in ("State_Idle(", "State_Fire(")),
        "GUARDS": "\n".join(guards), "GUARD_TESTS": "\n".join(guard_tests), "CAPTURE": capture,
        "ROCKET_GUARD": f"int RocketReload(const stateParms_t&) {{ {rocket_guard} return -1; }}",
        "HANDOFF": function(session, "static void Session_openQ4StartSingleplayer_f("),
        "MENU_OPTION": function(read(ROOT / "src/framework/Session_menu.cpp"), "static int MainMenuGetNewGameOption("),
        "WHEEL": function(player, "static void WeaponWheelPlacement(") + "\n" +
                 function(read(GAME / "src/game/gamesys/WeaponWheelLegacy.inl"), "float WeaponWheelNormalizeAngle(") + "\n" +
                 function(player, "static float WeaponWheelDeadzoneRadius(") + "\n" +
                 function(player, "static void WeaponWheelAdvanceCursor(") + "\n" +
                 function(player, "void idPlayer::UpdateWeaponWheelCursor(") + "\n" +
                 function(player, "int idPlayer::GetClosestAvailableWeaponWheelSlot("),
        "JOYSTICK": function(read(ROOT / "src/framework/UsercmdGen.cpp"), "void idUsercmdGenLocal::JoystickMove("),
        "RADIAL": "\n".join(function(read(ROOT / "src/sys/sdl3/sdl3_backend.cpp"), signature) for signature in (
            "static int SDL3_ClampJoystickValue(", "static float SDL3_ClampRange(", "static float SDL3_ClampUnit(",
            "static float SDL3_NormalizeSignedAxisFloat(", "static void SDL3_PublishRadialStick(",
            "static int SDL3_AxisFloatToJoystickValue(", "static void SDL3_NormalizeStickAxes(")),
    }
    source = HARNESS
    for name, value in substitutions.items():
        source = source.replace(f"@{name}@", value)
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("clang++ or g++ is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sp-turbo-wheel-", dir=ROOT / ".tmp") as directory:
        cpp, exe = Path(directory) / "test.cpp", Path(directory) / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True, cwd=directory)
        subprocess.run([str(exe)], check=True, cwd=directory)


if __name__ == "__main__":
    main()
