#!/usr/bin/env python3
"""Exercise production vehicle HUD updates for invulnerable/unshielded vehicles."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
constexpr int MAX_RENDERENTITY_GUI=3;
template<class T> T Max(T a,T b) { return std::max(a,b); }
struct idMath {
    static int FtoiFast(float v) { return int(v); }
    static float ClampFloat(float lo,float hi,float v) { return v<lo?lo:v>hi?hi:v; }
};
struct idUserInterface {
    std::map<std::string,float> fields;
    void SetStateFloat(const char* name,float value) { fields[name]=value; }
    void SetStateInt(const char* name,int value) { fields[name]=float(value); }
    void SetStateString(const char*,const char*) {}
};
struct Angles { float yaw=0; };
struct Axis { Angles ToAngles() { return {}; } };
struct Vec { float Length() { return 0; } };
struct Physics {
    Axis GetAxis() { return {}; }
    Vec GetLinearVelocity() { return {}; }
};
struct idEntity { Physics physics; Physics* GetPhysics() { return &physics; } };
struct idPlayer : idEntity { Axis firstPersonViewAxis; };
struct { int time=10; idPlayer* GetLocalPlayer() { return nullptr; } } gameLocal;
struct Common { const char* KeysFromBinding(const char*) { return "space"; } } commonObject;
Common* common=&commonObject;
struct Args { float health=1; float GetFloat(const char*,const char*) { return health; } };
struct Shield { int value=0; int GetCurrentValue(int) { return value; } };
struct Position { bool updated=false; void UpdateHUD(idUserInterface*) { updated=true; } };
struct rvVehicle : idEntity {
    int health=0,shieldMaxHealth=0,hazardWarningTime=0;
    Args spawnArgs;
    Shield shieldHealth;
    struct { idUserInterface* gui[MAX_RENDERENTITY_GUI]={}; } renderEntity;
    Position positions[1];
    bool IsLocked() { return false; }
    idEntity* GetBindMaster() { return nullptr; }
    void UpdateHUD(int,idUserInterface*);
};
'''
MAIN = r'''
int main() {
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    struct Case { int health; float capacity; int shield, shieldCapacity; float expectedHealth,expectedShield; };
    const Case cases[]={
        {0,0,0,0,1,0}, // invulnerable Awakening chaingun, no shield
        {1000,1000,0,0,1,0}, // walker without shields
        {250,1000,25,100,.25f,.25f}, // damaged vehicle and shield
        {0,1000,0,100,0,0}, // depleted
        {-10,1000,-5,100,0,0}, // overkill never creates negative geometry
        {1200,1000,125,100,1,1}, // scripted overcharge stays in the bar
        {0,-1,0,-1,1,0}, // nonpositive capacities are not divisors
    };
    for(const auto& c:cases) {
        rvVehicle vehicle;
        vehicle.health=c.health; vehicle.spawnArgs.health=c.capacity;
        vehicle.shieldHealth.value=c.shield; vehicle.shieldMaxHealth=c.shieldCapacity;
        idUserInterface hud,screen; vehicle.renderEntity.gui[1]=&screen;
        vehicle.UpdateHUD(0,&hud);
        float health=hud.fields.at("vehicle_health"),shield=hud.fields.at("vehicle_shield");
        assert(std::isfinite(health) && std::isfinite(shield));
        assert(health==c.expectedHealth && shield==c.expectedShield);
        assert(vehicle.positions[0].updated);
    }
    std::puts("vehicle HUD: invulnerable, unshielded, damaged and bounded fractions passed");
}
'''


def source(mode):
    text = (ROOT/'src'/mode/'vehicle/Vehicle.cpp').read_text()
    return SUPPORT + function_body(text, 'void rvVehicle::UpdateHUD (') + MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vehicle-hud-', dir=ROOT/'.tmp') as directory:
        scratch = Path(directory)
        for mode in ('game','mpgame'):
            code = source(mode)
            cases = {'production':code}
            if '--mutation-check' in sys.argv:
                for name, before, after in (
                    ('zero-health', 'maxHealth > 0.0f ? idMath::ClampFloat( 0.0f, 1.0f, health / maxHealth ) : 1.0f', 'health / maxHealth'),
                    ('zero-shield', 'shieldMaxHealth > 0 ? idMath::ClampFloat( 0.0f, 1.0f, (float)shieldHealth.GetCurrentValue(gameLocal.time) / (float)shieldMaxHealth ) : 0.0f', '(float)shieldHealth.GetCurrentValue(gameLocal.time) / (float)shieldMaxHealth'),
                    ('health-overflow', 'idMath::ClampFloat( 0.0f, 1.0f, health / maxHealth )', '(health / maxHealth)'),
                ):
                    assert before in code
                    cases[name] = code.replace(before, after)
            for name,candidate in cases.items():
                cpp,exe = scratch/f'{mode}.cpp',scratch/f'{mode}.exe'
                cpp.write_text(candidate,encoding='utf-8')
                subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
                result = subprocess.run([str(exe)],capture_output=name!='production',timeout=30)
                if name=='production':
                    result.check_returncode()
                else:
                    assert result.returncode!=0, f'{mode}: {name} escaped detection'
                    print(f'{mode}: rejected {name}',flush=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
