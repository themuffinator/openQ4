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
template<class T> T Min(T a,T b){return std::min(a,b);}
struct idMath {
 static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
 static float Exp(float f){return std::exp(f);}
};
// idlib's vector and matrix, reduced to what the lens uses (idVec3 * idVec3 is the dot product).
struct idVec3 {
 float x=0,y=0,z=0;
 idVec3 operator-(const idVec3&o)const{return {x-o.x,y-o.y,z-o.z};}
 float operator*(const idVec3&o)const{return x*o.x+y*o.y+z*o.z;}
};
struct idMat3 {
 idVec3 mat[3]{{1,0,0},{0,1,0},{0,0,1}};
 const idVec3& operator[](int i)const{return mat[i];}idVec3& operator[](int i){return mat[i];}
};
// RenderWorld.h renderView_t: the fields the view effects read.
struct renderView_t {idVec3 vieworg;idMat3 viewaxis;bool asymmetricFov=false;};
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
 bool guiFocus=false;idVec3 guiPoint; // Player.h: focusType == FOCUS_GUI with focusUI, at focusGuiPoint
 float GetWeaponWheelBlend(){return wheel;}bool IsInVehicle(){return inBed;}
 bool GetFocusGuiPoint(idVec3& point)const{if(!guiFocus)return false;point=guiPoint;return true;}
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
// RenderSystem.h, as declared there.
static const int VIEW_DOF_FOCUS_VIEW_WEAPON = 1 << 0;
static const int VIEW_DOF_SHARP_VIEW_WEAPON = 1 << 1;
typedef struct renderViewDepthOfField_s {
 float amount = 0.0f;
 int flags = 0;
 float focusDistance = 0.0f;
 float fStop = 2.8f;
 float focalLength = 0.0f;
 float focusRange = 0.0f;
 float maxBlur = 0.0f;
} renderViewDepthOfField_t;
struct RenderSystem {
 bool available=true;float amount=-1;int flags=0;float focus=0;renderViewDepthOfField_t last;
 bool SetViewDepthOfField(const renderViewDepthOfField_t& d){last=d;amount=d.amount;flags=d.flags;focus=d.focusDistance;return d.amount<=.001f||available;}
} renderer;
auto* renderSystem=&renderer;
struct idPlayerView {
 Player* player=nullptr;idUserInterface* presentationFx=nullptr;
 int deathFxStart=-1,recoveryFxUntil=0,crashFxStart=-1,crashFxDuration=0;
 bool recoverySequence=false,crashFxClosing=false,wheelSceneFocus=false;
 float guiFocusBlend=0,guiFocusDistance=0;int guiFocusTime=-1;
 ~idPlayerView();void ResetRetainedViewEffects();void RetainedRecoveryFade(const idVec4&,int);
 void ResetSubtitles(){} // subtitle state is covered by audio_subtitles.py
 void DrawRetainedViewEffects(bool wheelOnly=false);
 void RequestViewDepthOfField(bool allowed,const renderView_t& view,const renderViewDepthOfField_t& scene);
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
 renderView_t flat{},headset{};headset.asymmetricFov=true;
 v.RequestViewDepthOfField(true,flat,lens);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==1 && renderer.flags==VIEW_DOF_FOCUS_VIEW_WEAPON && v.wheelSceneFocus);
 assert(v.value("wheel_amount")==1 && v.value("wheel_blur")==0);
 // A headset eye, a declined pass or a hidden view keeps the blur and asks for nothing.
 v.RequestViewDepthOfField(true,headset,lens);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==0 && !v.wheelSceneFocus && v.value("wheel_blur")==1);
 renderer.available=false;v.RequestViewDepthOfField(true,flat,none);v.DrawRetainedViewEffects(true);
 assert(renderer.amount==1 && !v.wheelSceneFocus && v.value("wheel_blur")==1);
 renderer.available=true;v.RequestViewDepthOfField(false,flat,none);assert(renderer.amount==0 && !v.wheelSceneFocus);
 // With the wheel closed a map script's lens goes through untouched.
 p.wheel=0;v.RequestViewDepthOfField(true,flat,lens);
 assert(renderer.amount==.5f && renderer.flags==0 && renderer.focus==200 && !v.wheelSceneFocus);
 {
  // A focused in-world screen becomes the focus plane of a thin 50 mm f/2 lens.
  // It runs on game time, in steps of at most 100 ms, and wins over a script's lens.
  renderViewDepthOfField_t script{};script.amount=.7f;script.focusDistance=500;script.fStop=8;
  const auto near=[](float a,float b,float e){return std::fabs(a-b)<=e;};
  const auto isScript=[&]{const auto& d=renderer.last;return d.amount==.7f && d.flags==0 && d.focusDistance==500 && d.fStop==8;};
  const auto isScreen=[&]{const auto& d=renderer.last;return d.flags==VIEW_DOF_SHARP_VIEW_WEAPON && d.focalLength==50 && d.fStop==2 && d.maxBlur==12;};
  const auto step=[&](int ms,const renderView_t& view=renderView_t{}){gameLocal.time+=ms;v.RequestViewDepthOfField(true,view,script);};
  step(0);assert(isScript() && !v.wheelSceneFocus);
  // The screen is 100 units down the view axis; the sideways offset does not count.
  p.guiFocus=true;p.guiPoint={100,30,0};step(0);assert(isScript());
  step(50);step(50);const float early=renderer.last.amount;assert(isScreen() && early>0 && early<.5f);
  step(0);assert(renderer.last.amount==early); // a paused game holds the lens
  step(50);step(50);assert(isScreen() && renderer.last.amount>.85f && renderer.last.amount<.95f);
  step(50);assert(isScreen() && renderer.last.amount>.999f && !v.wheelSceneFocus); // in over 0.25 s
  assert(near(renderer.last.focusDistance,100,.001f) && renderer.last.focusRange==24); // 0.3 x 100, clamped to 24
  // Focus pulls toward a new distance instead of jumping, and the kept-sharp range follows it.
  p.guiPoint={50,0,0};step(100);assert(renderer.last.focusDistance>50.5f && renderer.last.focusDistance<99);
  for(int i=0;i<30;++i)step(100);
  assert(near(renderer.last.focusDistance,50,.01f) && near(renderer.last.focusRange,15,.01f));
  p.guiPoint={10,0,0};for(int i=0;i<30;++i)step(100);
  assert(near(renderer.last.focusDistance,10,.01f) && renderer.last.focusRange==8); // clamped to 8
  // Distance is measured from the eye along its axis, never closer than 4 units.
  renderView_t moved{};moved.vieworg={-30,0,0};for(int i=0;i<30;++i)step(100,moved);
  assert(near(renderer.last.focusDistance,40,.01f) && near(renderer.last.focusRange,12,.01f));
  p.guiPoint={-50,0,0};for(int i=0;i<30;++i)step(100);assert(near(renderer.last.focusDistance,4,.01f));
  p.guiPoint={100,0,0};for(int i=0;i<30;++i)step(100);
  // The weapon wheel wins over the screen; closing it hands focus straight back.
  p.wheel=1;step(50);
  assert(renderer.last.amount==1 && renderer.last.flags==VIEW_DOF_FOCUS_VIEW_WEAPON && renderer.last.focusDistance==0 && v.wheelSceneFocus);
  p.wheel=0;step(50);assert(isScreen() && renderer.last.amount>.999f && !v.wheelSceneFocus);
  // Looking away eases it out over 0.4 s, after which the script's lens returns.
  p.guiFocus=false;for(int i=0;i<4;++i)step(50);
  assert(isScreen() && near(renderer.last.amount,.5f,.02f) && near(renderer.last.focusDistance,100,.01f));
  for(int i=0;i<3;++i)step(50);assert(isScreen() && renderer.last.amount>0);
  step(50);step(50);assert(isScript());
  // A long frame advances it by at most 100 ms.
  p.guiFocus=true;step(1000);assert(isScreen() && near(v.guiFocusBlend,.4f,.001f));
  for(int i=0;i<3;++i)step(100);assert(renderer.last.amount>.999f);
  // A headset view drops it at once and asks for no depth of field at all.
  step(50,headset);assert(renderer.last.amount==0 && v.guiFocusBlend==0);
  step(50);assert(isScreen() && renderer.last.amount<.2f); // and it eases in again afterwards
  for(int i=0;i<5;++i)step(50);
  // A spectator, or a view that declines the effects, gets no screen lens: the script's passes.
  p.spectating=true;step(50);assert(isScript() && v.guiFocusBlend==0);p.spectating=false;
  for(int i=0;i<5;++i)step(50);assert(isScreen());
  gameLocal.time+=50;v.RequestViewDepthOfField(false,flat,script);assert(isScript() && v.guiFocusBlend==0);
  p.guiFocus=false;step(50);assert(isScript());
 }
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
 std::puts("retained view effects: death, wheel focus, screen focus, accessibility, crash sequence, cleanup and treatment passed");
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
