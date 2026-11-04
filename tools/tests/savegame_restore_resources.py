#!/usr/bin/env python3
"""Execute restore fixups and resource transactions with failure at every read."""
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
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
template<class T> struct idList {
    std::vector<T> data;
    int Num() const { return int(data.size()); }
    T& operator[](int i) { return data.at(i); }
    int Append(T item) { data.push_back(item); return Num()-1; }
    void Clear() { data.clear(); }
    void SetNum(int n) { data.resize(n); }
    void AssureSize(int n) { if(Num()<n) SetNum(n); }
    void DeleteContents(bool clear) { for(auto p:data) delete p; if(clear) Clear(); }
};
static int clips=0, dicts=0, traces=0, callbacks=0, queued=0, parts=0;
static bool complete=false, validated=false;
static const int EV_PostRestore=1;
struct idRestoreGame;
struct idClass;
struct idTypeInfo {
    bool IsType(int) { return true; }
    idClass* CreateInstance();
};
using idStr=std::string;
static int factoryMode=0;
struct idClass {
    bool restored=false;
    virtual ~idClass()=default;
    virtual bool IsType(int type) const { return type==1; }
    const char* GetClassname() const { return "fixture"; }
    const idTypeInfo* GetType() const { return nullptr; }
    void PostEventMS(const int*,int) { ++queued; }
    void ProcessEvent(const int*) { assert(complete && validated && restored); ++callbacks; }
    static idTypeInfo* GetClass(const idStr&) { static idTypeInfo type; return &type; }
};
struct idEntity : idClass {
    static int GetClassType() { return 1; }
    void ValidateRestoredBindings(idRestoreGame*);
    void UpdateVisuals() { assert(callbacks==2); }
    void Present() {}
};
struct idThread : idClass {
    static int GetClassType() { return 2; }
    void ValidateRestoreOwnership(idRestoreGame*) {}
};
struct rvClientEntity : idClass {
    static int GetClassType() { return 3; }
    void ValidateRestoredBinding(idRestoreGame*) {}
};
struct idClipModel {
    idClipModel() { ++clips; }
    ~idClipModel() { --clips; }
    void Restore(idRestoreGame*);
    static void RestoreTraceModels(idRestoreGame*);
};
struct idDict { idDict(){++dicts;} ~idDict(){--dicts;} };
struct trmCache_t {
    int trm=0, refCount=0, volume=0, centerOfMass=0, inertiaTensor=0, material=0;
    void* collisionModel=nullptr;
    unsigned hash=0;
    trmCache_t(){++traces;} ~trmCache_t(){--traces;}
};
static idList<trmCache_t*> traceModelCache;
struct Hash { void Add(int,int){} } traceModelHash;
static const int MAX_SAVEGAME_TRACE_MODELS=20, OPENQ4_SAVEGAME_PORTABLE_COMPATIBILITY_VERSION=5;
static const int MAX_SAVEGAME_INVENTORY_ITEMS=20, MAX_SAVEGAME_STATIC_MULTI_STATES=20;
static const int MAX_SAVEGAME_STATIC_MULTI_CLIP_MODELS=20;
static const int MAX_SAVEGAME_VEHICLE_PARTS=20, MAX_SAVEGAME_VEHICLE_WEAPONS=20;
static bool validTrace=true;
static bool SaveGameTraceModelBoundsCheck(int){return validTrace;}
static int GetTraceModelHashKey(int v){return v;}
static void ClearTraceModelCache(){traceModelCache.DeleteContents(true);}
static void CacheCollisionModels(){}
struct idRestoreGame {
    idList<idClass*> objects, postRestoreObjects;
    idClass embedded[2];
    int reads=0, fail=-1, count=3, version=5, phase=0;
    bool present=true, graphFailure=false, skipTraces=false;
    void Tick(){if(reads++==fail) throw std::runtime_error("interrupted read");}
    void ReadBool(bool& v){Tick(); v=present;}
    void ReadInt(int& v){Tick(); v=count;}
    void ReadInt(unsigned& v){Tick(); v=0;}
    void ReadFloat(int& v){Tick(); v=0;}
    void ReadVec3(int& v){Tick(); v=0;}
    void ReadMat3(int& v){Tick(); v=0;}
    void ReadMaterial(int& v){Tick(); v=0;}
    void ReadBounds(int& v){Tick(); v=0;}
    void ReadTraceModel(int& v){Tick(); v=0;}
    void Read(void*,size_t){Tick();}
    void ReadDict(idDict*){Tick(); Tick(); Tick();}
    void ReadString(idStr& value){Tick(); value="part";}
    void ReadJoint(int& value){ReadInt(value);}
    template<class T> void ReadObject(T*& v){Tick(); v=nullptr;}
    int GetOpenQ4SaveGameCompatibilityVersion(){return version;}
    [[noreturn]] void Error(const char*,...){throw std::runtime_error("invalid data");}
    void ReadSoundCommands(){Tick();}
    void ReadSyncId(const char*, const char*){Tick();}
    void ReadSaveGameFooter(){Tick(); complete=true;}
    void CallRestore_r(const idTypeInfo*,idClass* obj){
        Tick(); obj->restored=true;
        if(objects.Num()>1 && obj==objects[1]) { ReadStaticObject(embedded[0]); ReadStaticObject(embedded[1]); }
    }
    void ReadClipModel(idClipModel*&);
    void ReadStaticObject(idClass&);
    void RestoreObjects();
};
void idEntity::ValidateRestoredBindings(idRestoreGame* input){
    if(input->graphFailure) throw std::runtime_error("invalid graph");
    assert(complete); validated=true;
}
void idClipModel::Restore(idRestoreGame* input){ input->Tick(); input->Tick(); input->Tick(); }
struct State { int origin=0,axis=0,localOrigin=0,localAxis=0; };
struct idPhysics_StaticMulti {
    idEntity* self=nullptr;
    idList<State> current;
    idList<idClipModel*> clipModels;
    bool hasMaster=false,isOrientated=false;
    ~idPhysics_StaticMulti(){clipModels.DeleteContents(true);}
    void Restore(idRestoreGame*);
};
struct Inventory {
    idList<idDict*> items;
    ~Inventory(){items.DeleteContents(true);}
    void Restore(idRestoreGame*);
};
struct Vehicle {
    int positions=3;
    int* GetPosition(int index) { assert(index>=0 && index<positions); return &positions; }
    int GetNumPositions() const { return positions; }
};
static Vehicle* savedParent=nullptr;
struct ParentRef {
    Vehicle* value=nullptr;
    void Restore(idRestoreGame* input) { input->Tick(); value=savedParent; }
    Vehicle* operator->() const { return value; }
    explicit operator bool() const { return value!=nullptr; }
};
struct rvVehiclePart : idClass {
    ParentRef parent;
    struct { bool active,left,front,useCenterMass,useViewAxis; } fl{};
    idDict spawnArgs;
    int joint=0,soundChannel=0,worldOrigin=0,worldAxis=0,localOffset=0;
    int* position=nullptr;
    rvVehiclePart(){++parts;}
    ~rvVehiclePart(){--parts;}
    static int GetClassType(){return 1;}
    void Restore(idRestoreGame*);
};
template<size_t N> void SaveCodec_ReadFlagBytes(idRestoreGame& input,bool (&flags)[N]) {
    input.Tick(); for(auto& flag:flags) flag=false;
}
idClass* idTypeInfo::CreateInstance() {
    if(factoryMode==1) return nullptr;
    if(factoryMode==2) throw std::runtime_error("throwing factory");
    return new rvVehiclePart;
}
struct { [[noreturn]] void Error(const char*,...){throw std::runtime_error("bad class");} } gameLocal;
struct VehiclePosition {
    idList<rvVehiclePart*> mParts,mWeapons;
    ~VehiclePosition(){mParts.DeleteContents(true); mWeapons.DeleteContents(true);}
    void Restore(idRestoreGame*);
};
void vehicleTest() {
    for(factoryMode=0;factoryMode<3;++factoryMode) for(int fail=-1;fail<70;++fail){
        { idRestoreGame input; input.fail=fail; VehiclePosition position;
          try { position.Restore(&input); } catch(const std::runtime_error&){} }
        assert(parts==0 && dicts==0);
    }
    Vehicle vehicle;
    for(bool present:{false,true}) for(int count:{-1,0,2,3,2147483647}){
        savedParent=present?&vehicle:nullptr;
        idRestoreGame input; input.count=count; rvVehiclePart part;
        bool refused=false;
        try {part.Restore(&input);} catch(const std::runtime_error&){refused=true;}
        bool valid=present && count>=0 && count<vehicle.positions;
        assert(refused!=valid && (part.position!=nullptr)==valid);
    }
    savedParent=nullptr;
}
'''
MAIN = r'''
int main(){
    vehicleTest();
    // A replacement is published only when all its fields are present. Failure
    // must keep the prior model and release the partial one, including on retry.
    for(bool old:{false,true}) for(bool present:{false,true}) for(int fail=-1;fail<5;++fail){
        idClipModel* model=old?new idClipModel:nullptr;
        idClipModel* original=model;
        idRestoreGame input; input.present=present; input.fail=fail;
        bool failed=false;
        try { input.ReadClipModel(model); } catch(const std::runtime_error&){ failed=true; }
        if(failed){assert(model==original && clips==int(old));}
        else {assert(clips==int(present) && bool(model)==present);}
        delete model; assert(clips==0);
    }
    // Include the second and third allocation: earlier completed entries must
    // survive until their owner cleans up after an interrupted later entry.
    for(int fail=-1;fail<60;++fail){
        { idRestoreGame input; input.fail=fail; Inventory inventory;
          try { inventory.Restore(&input); } catch(const std::runtime_error&){} }
        assert(dicts==0);
        { idRestoreGame input; input.fail=fail; idPhysics_StaticMulti physics;
          physics.clipModels.Append(new idClipModel);
          try { physics.Restore(&input); } catch(const std::runtime_error&){} }
        assert(clips==0);
        for(int version:{4,5}) for(bool valid:{false,true}){
            idRestoreGame input; input.fail=fail; input.version=version; validTrace=valid;
            try { idClipModel::RestoreTraceModels(&input); } catch(const std::runtime_error&){}
            assert(traces==traceModelCache.Num());
            ClearTraceModelCache(); assert(traces==0);
        }
    }
    validTrace=true;
    for(int fail=-1;fail<18;++fail) for(bool graphFailure:{false,true}){
        callbacks=queued=0; complete=validated=false;
        idRestoreGame input; input.fail=fail; input.graphFailure=graphFailure; input.count=0;
        idEntity entity; input.objects.Append(nullptr); input.objects.Append(&entity);
        bool failed=false;
        try { input.RestoreObjects(); } catch(const std::runtime_error&){failed=true;}
        assert(queued==0); // Never serialize an embedded object as an event target.
        assert(callbacks==(failed?0:2));
        if(!failed) assert(input.postRestoreObjects.Num()==0);
    }
    std::puts("restore resources: immediate fixups and every interrupted allocation passed");
}
'''


def source(mode: str) -> str:
    tree = ROOT / "src" / mode
    save = (tree / "gamesys/SaveGame.cpp").read_text()
    code = SUPPORT
    for name in ("ReadClipModel", "ReadStaticObject", "RestoreObjects"):
        code += function_body(save, f"void idRestoreGame::{name}(") + "\n"
    code += function_body((tree / "physics/Clip.cpp").read_text(),
                          "void idClipModel::RestoreTraceModels(") + "\n"
    code += function_body((tree / "physics/Physics_StaticMulti.cpp").read_text(),
                          "void idPhysics_StaticMulti::Restore(") + "\n"
    inventory = function_body((tree / "Player.cpp").read_text(), "void idInventory::Restore(")
    # The entire owning-list transaction; other inventory fields need game assets.
    inventory = inventory[inventory.index("// Load Items"):inventory.index("// TORESTORE:")]
    code += "void Inventory::Restore(idRestoreGame* savefile){int i,num;\n" + inventory + "}\n"
    vehicle = function_body((tree / "vehicle/VehiclePosition.cpp").read_text(), "void rvVehiclePosition::Restore (")
    # Complete part/weapon owning-list transactions, excluding unrelated input,
    # seat transforms and flags. Read failures include both factory-created lists.
    vehicle = vehicle[vehicle.index("savefile->ReadInt ( num );"):vehicle.index("savefile->ReadVec3 ( mEyeOrigin );")]
    code += "void VehiclePosition::Restore(idRestoreGame* savefile){int i,num;\n" + vehicle + "}\n"
    code += function_body((tree / "vehicle/VehicleParts.cpp").read_text(), "void rvVehiclePart::Restore") + "\n"
    particle = function_body((tree / "physics/Physics_Particle.cpp").read_text(),
                             "void rvPhysics_Particle::Restore(")
    assert "new idClipModel" not in particle and "delete clipModel" not in particle
    return code + MAIN


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--generate-native":
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding="utf-8")
        return 0
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="restore-resources-", dir=ROOT / ".tmp") as directory:
        scratch = Path(directory)
        for mode in ("game", "mpgame"):
            cpp, exe = scratch / f"{mode}.cpp", scratch / f"{mode}.exe"
            code = source(mode)
            cases = {"production": code}
            if "--mutation-check" in sys.argv:
                cases = {
                    "queued-embedded-target": code.replace("postRestoreObjects.Append( &obj );",
                                                           "obj.PostEventMS( &EV_PostRestore, 0 );"),
                    "lost-clip-owner": code.replace("restored->Restore( this );",
                                                    "idClipModel* leaked = restored.release(); leaked->Restore( this ); restored.reset(leaked);"),
                    "lost-dictionary-owner": code.replace("savefile->ReadDict( itemdict.get() );",
                                                          "idDict* leaked = itemdict.release(); savefile->ReadDict(leaked); itemdict.reset(leaked);"),
                    "lost-trace-owner": code.replace("savefile->ReadFloat( entry->volume );",
                                                     "trmCache_t* leaked = entry.release(); savefile->ReadFloat(leaked->volume); entry.reset(leaked);"),
                    "lost-vehicle-part": code.replace("savefile->ReadStaticObject ( *part );",
                                                      "rvVehiclePart* leaked = part.release(); savefile->ReadStaticObject(*leaked); part.reset(leaked);"),
                    "unbounded-vehicle-position": code.replace("!parent || temp < 0 || temp >= parent->GetNumPositions()", "false"),
                }
            for name, candidate in cases.items():
                assert name == "production" or candidate != code
                cpp.write_text(candidate, encoding="utf-8")
                subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], check=True)
                result = subprocess.run([str(exe)], capture_output=name != "production")
                if name == "production":
                    result.check_returncode()
                else:
                    assert result.returncode != 0, f"{mode}: {name} escaped detection"
                    print(f"{mode}: rejected {name}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
