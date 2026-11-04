#!/usr/bin/env python3
"""Execute physics restore and destruction across every interrupted field read.

The readers, destructors, contact cleanup and owner entry point come from each
production game tree. Checked entity access exposes dead/foreign owners and
unset contact entries without depending on a particular allocator's leftovers.
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
#include <vector>
constexpr int MAX_GENTITIES=16, MAX_SAVEGAME_PHYSICS_CONTACTS=8;
constexpr int MAX_SAVEGAME_PHYSICS_CONTACT_ENTITIES=8;
constexpr int MAX_SAVEGAME_STATIC_MULTI_STATES=8, MAX_SAVEGAME_STATIC_MULTI_CLIP_MODELS=8;
template<class T> struct idList {
    std::vector<T> data;
    int Num() const { return int(data.size()); }
    T& operator[](int i) { return data.at(i); }
    void SetNum(int n, bool=true) { data.resize(n); }
    void AssureSize(int n) { if(Num()<n) SetNum(n); }
    void Clear() { data.clear(); }
    void Append(T value) { data.push_back(value); }
    void DeleteContents(bool clear) { for(auto p:data) delete p; if(clear) Clear(); }
};
struct idPhysics;
struct idEntity {
    bool alive=true;
    idPhysics* physics=nullptr;
    idPhysics* GetPhysics() { assert(alive); return physics; }
    void SetPhysics(idPhysics* value) { assert(alive); physics=value; }
    void RemoveContactEntity(idEntity* owner) { assert(alive && owner && owner->alive); }
};
struct EntityTable {
    std::vector<idEntity*> data=std::vector<idEntity*>(MAX_GENTITIES);
    idEntity*& operator[](int index) { return data.at(index); }
};
struct { EntityTable entities; } gameLocal;
struct contactInfo_t { int entityNum=MAX_GENTITIES+51; };
struct idRestoreGame;
struct EntityRef { void Restore(idRestoreGame*); };
struct idClipModel {};
struct State { int origin=0,axis=0,localOrigin=0,localAxis=0; };
struct idPhysics {
    idEntity* self=nullptr;
    virtual ~idPhysics()=default;
    void SetSelf(idEntity* owner) { assert(owner); self=owner; }
    virtual void Restore(idRestoreGame*)=0;
    const char* GetClassname() const { return "physics fixture"; }
};
struct idForce { static void DeletePhysics(idPhysics*) {} };
struct idPhysics_Base : idPhysics {
    int clipMask=0, gravityVector=0, gravityNormal=0;
    idList<contactInfo_t> contacts;
    idList<EntityRef> contactEntities;
    void Restore(idRestoreGame*) override;
    ~idPhysics_Base() override;
    void ClearContacts();
};
struct idPhysics_Static : idPhysics {
    State current;
    idClipModel* clipModel=nullptr;
    bool hasMaster=false, isOrientated=false;
    void Restore(idRestoreGame*) override;
    ~idPhysics_Static() override;
};
struct idPhysics_StaticMulti : idPhysics {
    idList<State> current;
    idList<idClipModel*> clipModels;
    bool hasMaster=false, isOrientated=false;
    void Restore(idRestoreGame*) override;
    ~idPhysics_StaticMulti() override;
};
struct idRestoreGame {
    int steps=0, failAt=-1, entityNumber=1, count=3;
    idEntity* savedOwner=nullptr;
    void Tick() { if(steps++==failAt) throw std::runtime_error("interrupted field"); }
    void ReadObject(idEntity*& value) { value=savedOwner; Tick(); }
    void ReadInt(int& value) { value=count; Tick(); }
    void ReadVec3(int& value) { value=0; Tick(); }
    void ReadMat3(int& value) { value=0; Tick(); }
    void ReadBool(bool& value) { value=false; Tick(); }
    void ReadClipModel(idClipModel*&) { Tick(); }
    void ReadContactInfo(contactInfo_t& value) {
        Tick(); Tick(); value.entityNum=entityNumber; Tick(); Tick();
    }
    void ReadStaticObject(idPhysics& value) { Tick(); value.Restore(this); Tick(); }
    void ReadPhysicsObject(idPhysics&,idEntity*);
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("invalid data"); }
};
void EntityRef::Restore(idRestoreGame* input) { input->Tick(); }
'''
MAIN = r'''
template<class Physics> void check() {
    int boundaries=0;
    for(int failure=-1; failure<boundaries; ++failure) {
        // NULL, a valid owner and a different entity are all historical pointer
        // inputs. None may replace the entity actually containing the physics.
        for(int saved=0; saved<3; ++saved) {
            idEntity owner,foreign,contact;
            gameLocal.entities[1]=&contact;
            idRestoreGame input; input.failAt=failure;
            input.savedOwner=saved==0?nullptr:saved==1?&owner:&foreign;
            auto* physics=new Physics;
            owner.physics=physics;
            try { input.ReadPhysicsObject(*physics,&owner); }
            catch(const std::runtime_error&) { assert(failure>=0); }
            if(failure==-1) boundaries=input.steps;
            assert(physics->self==&owner);
            // MapShutdown can delete the earlier referenced entity first.
            foreign.alive=false;
            delete physics;
            assert(owner.physics==nullptr);
        }
    }
    idRestoreGame input;
    Physics physics;
    bool refused=false;
    try { input.ReadPhysicsObject(physics,nullptr); }
    catch(const std::runtime_error&) { refused=true; }
    assert(refused && physics.self==nullptr && input.steps==0);
}
int main() {
    check<idPhysics_Base>(); check<idPhysics_Static>(); check<idPhysics_StaticMulti>();
    for(int number : {-1,0,MAX_GENTITIES-1,MAX_GENTITIES,2147483647}) {
        idEntity owner;
        idRestoreGame input; input.entityNumber=number;
        idPhysics_Base physics;
        bool refused=false;
        try { input.ReadPhysicsObject(physics,&owner); }
        catch(const std::runtime_error&) { refused=true; }
        bool invalid=number<0 || number>=MAX_GENTITIES;
        assert(refused==invalid && physics.contacts.Num()==(invalid?0:3));
    }
    std::puts("physics restore: every interrupted read, owner and contact bounds passed");
}
'''


def source(mode: str) -> str:
    tree = ROOT / "src" / mode
    # All embedded physics, including shards, AF and client moveables, must enter
    # through the owner-setting helper. Force fields keep the generic reader.
    for path in tree.rglob("*.cpp"):
        for argument in re.findall(r"savefile->ReadStaticObject\s*\((.*?)\)", path.read_text()):
            assert argument.strip() in ("forceField", "*part"), f"{path}: physics bypasses its owner entry point"
    save = (tree / "gamesys/SaveGame.cpp").read_text()
    code = SUPPORT + function_body(save, "void idRestoreGame::ReadPhysicsObject(") + "\n"
    for kind in ("Base", "Static", "StaticMulti"):
        text = (tree / "physics" / f"Physics_{kind}.cpp").read_text()
        for signature in (f"void idPhysics_{kind}::Restore(", f"idPhysics_{kind}::~idPhysics_{kind}("):
            code += function_body(text, signature) + "\n"
        if kind == "Base":
            code += function_body(text, "void idPhysics_Base::ClearContacts(") + "\n"
    return code + MAIN


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--generate-native":
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding="utf-8")
        return 0
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="restore-physics-", dir=ROOT / ".tmp") as directory:
        scratch = Path(directory)
        for mode in ("game", "mpgame"):
            cpp, exe = scratch / f"{mode}.cpp", scratch / f"{mode}.exe"
            code = source(mode)
            cases = {"production": code}
            if "--mutation-check" in sys.argv:
                for kind in ("Base", "Static", "StaticMulti"):
                    body = function_body(code, f"void idPhysics_{kind}::Restore(")
                    cases[f"foreign-{kind}-owner"] = code.replace(body, body.replace("ReadObject( savedSelf )", "ReadObject( self )"))
                cases["late-owner"] = code.replace("obj.SetSelf( owner );\n\tReadStaticObject( obj );",
                                                   "ReadStaticObject( obj );\n\tobj.SetSelf( owner );")
                cases["partial-contact-list"] = code.replace("contacts.Clear();", "contacts.SetNum( num );") \
                    .replace("contactInfo_t contact;", "contactInfo_t &contact = contacts[i];") \
                    .replace("contacts.Append( contact );", "")
                cases["unbounded-contact"] = code.replace("contact.entityNum < 0 || contact.entityNum >= MAX_GENTITIES", "false")
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
