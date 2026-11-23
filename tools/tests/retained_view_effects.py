#!/usr/bin/env python3
"""Exercise the shared production view effects with a controlled game clock."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath {static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}};
struct idStr {
 std::string s; idStr(const char* v):s(v){}
 void StripFileExtension(){auto i=s.rfind('.');if(i!=s.npos)s.resize(i);}
 void StripPath(){auto i=s.find_last_of("/\\");if(i!=s.npos)s=s.substr(i+1);}
 static int Icmp(const char*a,const char*b){std::string x(a),y(b);for(auto&c:x)c=std::tolower(c);for(auto&c:y)c=std::tolower(c);return x.compare(y);}
 int Icmp(const char*b)const{return Icmp(s.c_str(),b);}
};
struct idVec4 {float v[4];float operator[](int i)const{return v[i];}};
struct idCamera {const char* name="cin_opening";const char* GetName()const{return name;}};
struct Vehicle {const char* GetName()const{return "mcc2_playerBed";}};
struct Controller {Vehicle vehicle;Vehicle* GetVehicle(){return &vehicle;}};
struct Player {
 int health=100;bool spectating=false,inBed=false;float wheel=0;Controller controller;
 float GetWeaponWheelBlend(){return wheel;}bool IsInVehicle(){return inBed;}
 Controller& GetVehicleController(){return controller;}
};
struct Game {
 bool isMultiplayer=false;int time=1000;const char* map="game/airdefense1";idCamera* camera=nullptr;
 const char* GetMapName(){return map;}idCamera* GetCamera(){return camera;}
 template<class...T>void Printf(const char*,T...){}
} gameLocal;
struct Cvars {
 std::map<std::string,bool> values{{"ui_retained",true}};
 bool GetCVarBool(const char*k){return values[k];}
} cv;
auto* cvarSystem=&cv;
struct idUserInterface {
 std::map<std::string,float> state;int draws=0;
 void SetStateFloat(const char*k,float v){state[k]=v;}
 void StateChanged(int){}void Redraw(int){++draws;}
};
struct Manager {
 int loads=0,frees=0;
 idUserInterface* FindGui(const char*,bool,bool){++loads;return new idUserInterface;}
 void DeAlloc(idUserInterface*g){++frees;delete g;}
} manager;
auto* uiManager=&manager;
const int VIEW_DOF_FOCUS_VIEW_WEAPON=1;
struct renderViewDepthOfField_t {float amount=0;int flags=0;float focusDistance=0,fStop=2.8f,focalLength=0,focusRange=0,maxBlur=0;};
struct RenderSystem {
 bool available=true;float amount=-1;int flags=0;float focus=0;
 bool SetViewDepthOfField(const renderViewDepthOfField_t& d){amount=d.amount;flags=d.flags;focus=d.focusDistance;return d.amount<=.001f||available;}
} renderer;
auto* renderSystem=&renderer;
struct idPlayerView {
 Player* player=nullptr;idUserInterface* presentationFx=nullptr;
 int deathFxStart=-1,recoveryFxUntil=0,crashFxStart=-1,crashFxDuration=0;
 bool recoverySequence=false,crashFxClosing=false,wheelSceneFocus=false;
 ~idPlayerView();void ResetRetainedViewEffects();void RetainedRecoveryFade(const idVec4&,int);
 void ResetSubtitles(){} // subtitle state is covered by audio_subtitles.py
 void DrawRetainedViewEffects(bool wheelOnly=false);
 void RequestRetainedWheelFocus(bool allowed,bool headset,const renderViewDepthOfField_t& scene);
 float value(const char*k){return presentationFx?presentationFx->state[k]:0;}
};
'''
CHECKS = r'''
int main(){
 Player p;idPlayerView v;v.player=&p;
 v.DrawRetainedViewEffects();assert(manager.loads==0);
 p.health=0;v.DrawRetainedViewEffects();gameLocal.time+=320;v.DrawRetainedViewEffects();
 assert(v.value("death_focus")>.99f && v.value("death_shock")>0 && v.value("death_tunnel")<.01f);
 gameLocal.time+=2400;v.DrawRetainedViewEffects();
 assert(v.value("death_amount")==1 && v.value("death_focus")==0 && v.value("death_shock")==0 && v.value("death_tunnel")==1);
 // Wheel submission clears the scene-specific states in the shared document.
 p.wheel=1;v.DrawRetainedViewEffects(true);
 assert(v.value("wheel_amount")==1 && v.value("death_amount")==0 && v.value("death_tunnel")==0);
 assert(v.value("wheel_blur")==1);
 // The renderer's depth of field focuses the view weapon in place of the blur.
 const renderViewDepthOfField_t none{};renderViewDepthOfField_t lens{};lens.amount=.5f;lens.focusDistance=200;
 v.RequestRetainedWheelFocus(true,false,lens);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==1 && renderer.flags==VIEW_DOF_FOCUS_VIEW_WEAPON && v.wheelSceneFocus);
 assert(v.value("wheel_amount")==1 && v.value("wheel_blur")==0);
 // A headset eye, a declined pass or a hidden view keeps the blur and asks for nothing.
 v.RequestRetainedWheelFocus(true,true,lens);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==0 && !v.wheelSceneFocus && v.value("wheel_blur")==1);
 renderer.available=false;v.RequestRetainedWheelFocus(true,false,none);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==1 && !v.wheelSceneFocus && v.value("wheel_blur")==1);
 renderer.available=true;v.RequestRetainedWheelFocus(false,false,none);assert(renderer.amount==0 && !v.wheelSceneFocus);
 // With the wheel closed a map script's lens goes through untouched.
 p.wheel=0;v.RequestRetainedWheelFocus(true,false,lens);
 assert(renderer.amount==.5f && renderer.flags==0 && renderer.focus==200 && !v.wheelSceneFocus);
 p.wheel=1;
 p.wheel=0;p.health=100;v.DrawRetainedViewEffects();assert(v.deathFxStart==-1);
 for(const char* setting:{"ui_retainedReducedMotion","ui_reducedFlashing"}){
  cv.values[setting]=true;p.health=0;v.DrawRetainedViewEffects();gameLocal.time+=320;v.DrawRetainedViewEffects();
  assert(v.value("death_focus")==0 && v.value("death_shock")==0 && v.value("death_amount")>0);
  p.health=100;v.DrawRetainedViewEffects();cv.values[setting]=false;
 }
 // Actual death grading is shared with multiplayer, without the SP concussion.
 gameLocal.isMultiplayer=true;p.health=0;v.DrawRetainedViewEffects();gameLocal.time+=600;v.DrawRetainedViewEffects();assert(v.value("death_focus")>0);
 gameLocal.time=1;v.DrawRetainedViewEffects();assert(v.deathFxStart==1);
 gameLocal.isMultiplayer=false;p.health=100;v.DrawRetainedViewEffects();
 idCamera camera;gameLocal.camera=&camera;
 const idVec4 wake{{0,0,0,0}},black{{0,0,0,1}},white{{1,1,1,0}};
 // Only the correct map, camera, colour and game mode arm waking effects.
 gameLocal.map="game/other";v.RetainedRecoveryFade(wake,2300);assert(v.crashFxStart==-1);
 gameLocal.map="game/airdefense1";camera.name="cin_camera_intro_cin_1";v.RetainedRecoveryFade(wake,2300);assert(v.crashFxStart==-1);
 camera.name="cin_opening";gameLocal.isMultiplayer=true;v.RetainedRecoveryFade(wake,2300);assert(v.crashFxStart==-1);
 gameLocal.isMultiplayer=false;v.RetainedRecoveryFade(white,2300);assert(v.crashFxStart==-1);
 v.RetainedRecoveryFade(black,100);assert(v.crashFxStart==-1);
 // Three authored wakes, two blackouts. No effect survives the last wake.
 for(int i=0;i<3;++i){
  v.RetainedRecoveryFade(wake,2300);v.DrawRetainedViewEffects();assert(v.value("crash_focus")==1 && v.value("death_amount")==0);
  gameLocal.time+=2500;v.DrawRetainedViewEffects();const float normal=v.value("crash_focus");
  cv.values["ui_retainedReducedMotion"]=true;v.DrawRetainedViewEffects();assert(v.value("crash_focus")<normal);cv.values["ui_retainedReducedMotion"]=false;
  cv.values["ui_reducedFlashing"]=true;v.DrawRetainedViewEffects();assert(v.value("crash_focus")<normal);cv.values["ui_reducedFlashing"]=false;
  gameLocal.time+=2200;const int draws=v.presentationFx->draws;v.DrawRetainedViewEffects();assert(v.presentationFx->draws==draws);
  if(i<2){v.RetainedRecoveryFade(black,100);gameLocal.time+=50;v.DrawRetainedViewEffects();assert(v.value("crash_lids")>0 && v.value("crash_lids")<1);gameLocal.time+=2100;}
 }
 v.RetainedRecoveryFade(wake,2300);gameLocal.camera=nullptr;v.DrawRetainedViewEffects();assert(v.crashFxStart==-1);
 gameLocal.camera=&camera;v.RetainedRecoveryFade(wake,2300);v.ResetRetainedViewEffects();assert(v.crashFxStart==-1 && !v.presentationFx);
 v.RetainedRecoveryFade(wake,2300);cv.values["ui_retained"]=false;v.DrawRetainedViewEffects();assert(v.crashFxStart==-1);
 cv.values["ui_retained"]=true;v.RetainedRecoveryFade(wake,2300);p.spectating=true;v.DrawRetainedViewEffects();assert(v.crashFxStart==-1);p.spectating=false;
 // Existing treatment recovery remains isolated and consumes the final bed exit.
 gameLocal.camera=nullptr;gameLocal.map="game/mcc_2";p.inBed=true;v.RetainedRecoveryFade(white,2000);
 v.DrawRetainedViewEffects();assert(v.value("recovery_amount")==1 && v.value("crash_focus")==0);
 p.inBed=false;v.RetainedRecoveryFade(white,2000);assert(!v.recoverySequence);
 std::puts("retained view effects: death, wheel focus, accessibility, crash sequence, cleanup and treatment passed");
}
'''


def main():
    compiler = next((shutil.which(c) for c in ('clang++', 'g++', 'c++') if shutil.which(c)), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    temporary = ROOT / '.tmp'
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='view-effects-', dir=temporary) as directory:
        source = Path(directory) / 'effects.cpp'
        binary = Path(directory) / 'effects.exe'
        production = (ROOT / 'src/game/gamesys/RetainedViewEffects.inl').as_posix()
        source.write_text(SUPPORT + f'\n#include "{production}"\n' + CHECKS, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
