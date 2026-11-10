#!/usr/bin/env python3
"""Fault every read/acquisition boundary in production render-handle restores.

Four small Restore methods are compiled whole; larger methods contribute the
exact handle read/acquire blocks, with unrelated fields omitted. Checked render
ownership rejects saved, foreign or already freed handles during cleanup.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <set>
#include <climits>
struct Fault : std::runtime_error { Fault() : std::runtime_error("interrupted restore") {} };
int failAt=-1,steps=0,savedValue=123;
void boundary() { if(steps++==failAt)throw Fault(); }
struct Data { bool noShadows=false; template<class T> void Restore(T*) { boundary(); } const char* GetName() { return "fixture"; } };
struct Args { const char* GetString(const char*,const char* fallback) { return fallback; } };
struct Decl { void FindType(int,const char*,bool,bool) { boundary(); } } decl,*declManager=&decl;
constexpr int DECL_ENTITYDEF=1,WPLIGHT_MAX=3,ENTITYNUM_CLIENT=0;
// Two of the three fixture slots stand in for the flashlight pair, whose shadow
// flag the weapon restore re-derives from the current setting.
constexpr int WPLIGHT_FLASHLIGHT=1,WPLIGHT_FLASHLIGHT_WORLD=2;
bool G_FlashlightShadowsEnabled() { return true; }
struct { void* entities[1]={nullptr}; } gameLocal;
struct World {
    int next=10000;
    std::set<int> owned;
    int AddEntityDef(Data*) { boundary(); int id=next++; owned.insert(id); return id; }
    int AddLightDef(Data* data) { return AddEntityDef(data); }
    void FreeEntityDef(int id) {
        if(!owned.erase(id))throw std::runtime_error("cleanup released a saved or foreign renderer handle");
    }
    void FreeLightDef(int id) { FreeEntityDef(id); }
} world,*gameRenderWorld=&world;
void rvNormalizeProjectedRenderLight(Data&,const char*,const char*) { boundary(); }
struct idRestoreGame {
    // A short read may write a prefix before reporting failure. Model that too.
    void ReadInt(int& value) { value=savedValue; boundary(); }
    template<class T> void ReadRenderEntity(T&,const void*) { boundary(); }
    template<class T> void ReadRenderLight(T&) { boundary(); }
    template<class T> void ReadString(T&) { boundary(); }
    template<class T> void ReadFloat(T&) { boundary(); }
    template<class T> void ReadBool(T&) { boundary(); }
    template<class T> void ReadJoint(T&) { boundary(); }
    template<class T> void ReadVec3(T&) { boundary(); }
    template<class T> void ReadMat3(T&) { boundary(); }
    template<class T> void ReadPhysicsObject(T&, void*) { boundary(); }
    template<class T> void ReadSoundShader(T&) { boundary(); }
    template<class T> void ReadRefSound(T&) { boundary(); }
};
struct Subject {
    int modelDefHandle=-1,entityDefHandle=-1,flashlightHandle=-1,lightDefHandle=-1,ipsHandle=-1,lightHandle=-1;
    int lightHandles[WPLIGHT_MAX]={-1,-1,-1};
    Data renderEntity,renderLight,flashlight,ipsEntity,light,lights[WPLIGHT_MAX],refSound;
    Data trailEffect,physicsObj,flashlightJoint,flashlightOffset,lightOffset;
    Data spawnOrigin,spawnAxis,classname,soundOn,soundOff,bounceSoundShader;
    Data* owner=nullptr; Data definition; Data* weaponDef=&definition; Data* parent=nullptr;
    Args spawnArgs;
    int state=0,ipsTime=0,lightTime=0,explodeFinishTime=0,bounceSoundTime=0;
    float time=0,trailAttenuateSpeed=0;
    bool lightOn=false,mPlayBounceSoundOnce=false,mHasBounced=false;
    const char* GetName() { return "fixture"; }
    void Entity(idRestoreGame*);
    void Actor(idRestoreGame*);
    void Projectile(idRestoreGame*);
    void Barrel(idRestoreGame*);
    void ClientModel(idRestoreGame*);
    void ClientMoveable(idRestoreGame*);
    void VehicleLight(idRestoreGame*);
    void WeaponLights(idRestoreGame*);
    void HoverLight(idRestoreGame*);
    void Cleanup() {
        for(int id : {modelDefHandle,entityDefHandle,flashlightHandle,lightDefHandle,ipsHandle,lightHandle,
                      lightHandles[0],lightHandles[1],lightHandles[2]}) {
            if(id!=-1)world.FreeEntityDef(id);
        }
    }
};
'''

MAIN = r'''
int main(int argc,char** argv) {
    struct Case { const char* name; void(Subject::*method)(idRestoreGame*); };
    Case cases[]={{"entity",&Subject::Entity},{"actor",&Subject::Actor},{"projectile",&Subject::Projectile},
        {"barrel",&Subject::Barrel},{"client-model",&Subject::ClientModel},{"client-moveable",&Subject::ClientMoveable},
        {"vehicle-light",&Subject::VehicleLight},{"weapon-lights",&Subject::WeaponLights},{"hover-light",&Subject::HoverLight}};
    int failures=0;
    for(const auto& test:cases) {
        if(argc>1 && std::string(argv[1])!=test.name)continue;
        int boundaries=0;
        try {
            for(int value : {-1,0,123,INT_MAX}) {
                savedValue=value; failAt=-1; steps=0; world=World();
                Subject valid; idRestoreGame input;
                (valid.*test.method)(&input); boundaries=steps;
                if(value==-1)assert(world.owned.empty());
                else assert(!world.owned.empty());
                valid.Cleanup(); assert(world.owned.empty());
                for(int failure=0;failure<boundaries;++failure) {
                    world=World(); steps=0; failAt=failure; Subject object;
                    bool caught=false;
                    try { (object.*test.method)(&input); } catch(const Fault&) { caught=true; }
                    assert(caught);
                    object.Cleanup(); assert(world.owned.empty());
                }
            }
            std::printf("%s: complete/absent records and all %d positive-handle failure boundaries passed\n",test.name,boundaries);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"%s: %s (saved %d, boundary %d)\n",test.name,error.what(),savedValue,failAt); ++failures;
        }
    }
    return failures ? 1 : 0;
}
'''


def block(text, condition):
    return function_body(text, condition)


def handle_start(body, handle, local):
    declaration = re.search(r'\bint\s+'+local+r'\s*;', body)
    if declaration:
        return declaration.start()
    read = re.search(r'savefile->ReadInt\s*\(\s*(?:\(int\s*&\)\s*)?'+re.escape(handle)+r'\s*\)', body)
    assert read, handle
    return read.start()


def source(mode):
    base = ROOT/'src'/mode
    def method(file, name):
        text=(base/file).read_text(encoding='utf-8')
        signature=re.search(r'void\s+'+name+r'::Restore\s*\(',text)
        assert signature, name
        return function_body(text,signature[0])
    bodies = []
    for name, file, cls in (
        ('ClientModel','client/ClientModel.cpp','rvClientModel'),
        ('ClientMoveable','client/ClientMoveable.cpp','rvClientMoveable'),
        ('Barrel','Moveable.cpp','idExplodingBarrel'),
        ('VehicleLight','vehicle/VehicleParts.cpp','rvVehicleLight'),
    ):
        body = method(file,cls)
        bodies.append('void Subject::'+name+'(idRestoreGame* savefile) '+body[body.index('{'):])
    for name,file,cls,handle,local in (
        ('Actor','Actor.cpp','idActor','flashlightHandle','savedFlashlightHandle'),
        ('Projectile','Projectile.cpp','idProjectile','lightDefHandle','savedLightDefHandle'),
        ('HoverLight','ai/Monster_StroggHover.cpp','rvMonsterStroggHover','lightHandle','savedLightHandle'),
    ):
        body = method(file,cls)
        start = handle_start(body,handle,local)
        condition = next(line.strip() for line in body[start:].splitlines() if re.match(r'if\s*\(',line.strip()))
        branch = block(body[start:],condition)
        end = body.index(branch,start)+len(branch)
        bodies.append('void Subject::'+name+'(idRestoreGame* savefile) {\n'+body[start:end]+'\n}')
    body = method('Entity.cpp','idEntity')
    start = handle_start(body,'modelDefHandle','savedModelDefHandle')
    end = body.index('savefile->ReadRefSound( refSound );',start)+len('savefile->ReadRefSound( refSound );')
    condition = 'if ( savedModelDefHandle != -1 )' if 'savedModelDefHandle' in body else 'if ( modelDefHandle != -1 )'
    bodies.append('void Subject::Entity(idRestoreGame* savefile) {\n'+body[start:end]+'\n'+block(body,condition)+'\n}')
    body = method('Weapon.cpp','rvWeapon')
    start = body.index('// Lights')
    end = body.index('savefile->ReadVec3',start)
    bodies.append('void Subject::WeaponLights(idRestoreGame* savefile) { int i;\n'+body[start:end]+'\n}')
    return SUPPORT+'\n'.join(bodies)+MAIN


def main():
    if len(sys.argv)==4 and sys.argv[1]=='--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]),encoding='utf-8')
        return 0
    compiler=next((path for name in ('clang++','g++','c++') if (path:=shutil.which(name))),None)
    if not compiler: raise RuntimeError('C++17 compiler required')
    failures=0
    with tempfile.TemporaryDirectory(prefix='save-render-handles-',dir=ROOT/'.tmp') as directory:
        for mode in ('game','mpgame'):
            cpp=Path(directory)/(mode+'.cpp'); exe=cpp.with_suffix('.exe')
            cpp.write_text(source(mode),encoding='utf-8')
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
            print(mode,flush=True)
            failures+=subprocess.run([str(exe)]).returncode!=0
    return int(failures!=0)


if __name__=='__main__': raise SystemExit(main())
