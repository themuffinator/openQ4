#!/usr/bin/env python3
"""Execute the production SP/MP restore identity reads before subsequent fields.

An error may immediately tear down the partially restored map. Rejected entity
numbers must therefore never overwrite the index that owns the live object.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
constexpr int MAX_GENTITIES=256, MAX_CENTITIES=128, ENTITYNUM_NONE=MAX_GENTITIES-1;
struct idRestoreGame {
    int value;
    std::vector<void*> references;
    int next=0, failAfter=-1;
    void ReadInt(int& output) {
        if(references.empty()) { output=value; return; }
        if(next==failAfter) Error("interrupted spawn ID");
        output=100+next++;
    }
    template<class T> void ReadObject(T*& output) { output=static_cast<T*>(references.at(next)); }
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("restore failed"); }
};
struct idStr {};
class idEntity { public: int entityNumber=3; void Restore(idRestoreGame*); };
class rvClientEntity { public: int entityNumber=3; void Restore(idRestoreGame*); };
'''
MAIN = r'''
template<class Entity> void exercise(const char* label,int invalidLower,int limit,int none) {
    for(int value : {INT_MIN, invalidLower, limit, INT_MAX, 4, none}) {
        Entity object;
        idRestoreGame input{value};
        bool failed=false;
        try { object.Restore(&input); } catch(const std::runtime_error&) { failed=true; }
        if(!failed || object.entityNumber!=3) {
            std::fprintf(stderr,"%s: rejected identity %d left live object at %d (error %d)\n",
                         label,value,object.entityNumber,failed);
            throw std::runtime_error("restore identity was published before validation");
        }
    }
    Entity registered;
    idRestoreGame valid{3}; registered.Restore(&valid);
    assert(registered.entityNumber==3);
    // An object missing from the master table has no number of its own: taking
    // a saved one would alias references to whatever is registered there.
    Entity unregistered; unregistered.entityNumber=none;
    bool rejected=false;
    try { unregistered.Restore(&valid); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && unregistered.entityNumber==none);
    idRestoreGame unnumbered{none}; unregistered.Restore(&unnumbered);
    assert(unregistered.entityNumber==none);
}
template<class Entity> void tableCases(int none) {
    for(int scenario=0;scenario<4;++scenario) {
        Entity first,second; first.entityNumber=second.entityNumber=none;
        // rvClientEffect constructors register before LoadMap clears the table.
        // Saved slots must replace those constructor identities, not reject them.
        if(scenario==3) first.entityNumber=second.entityNumber=0;
        Entity* table[4]={}; int spawnIds[4]={-1,-1,-1,-1};
        idRestoreGame input{0}; input.references={&first,scenario==1?&first:&second,nullptr,nullptr};
        if(scenario==2) input.failAfter=1;
        bool failed=false;
        try { GameLocal_RestoreEntityTable(input,table,spawnIds,none,"fixture"); }
        catch(const std::runtime_error&) { failed=true; }
        assert(failed==(scenario==1 || scenario==2));
        assert(table[0]==&first && first.entityNumber==0 && spawnIds[0]==100);
        if(failed) {
            assert(table[1]==nullptr && spawnIds[1]==-1 && second.entityNumber==none);
        } else {
            assert(table[1]==&second && second.entityNumber==1 && spawnIds[1]==101);
            assert(table[2]==nullptr && table[3]==nullptr);
        }
    }
}
void reservedSlot() {
    idEntity object; object.entityNumber=ENTITYNUM_NONE;
    idEntity* table[MAX_GENTITIES]={}; int spawnIds[MAX_GENTITIES]={};
    idRestoreGame input{0}; input.references.resize(MAX_GENTITIES);
    input.references[ENTITYNUM_NONE]=&object;
    bool failed=false;
    try { GameLocal_RestoreEntityTable(input,table,spawnIds,ENTITYNUM_NONE,"fixture"); }
    catch(const std::runtime_error&) { failed=true; }
    assert(failed && table[ENTITYNUM_NONE]==nullptr && object.entityNumber==ENTITYNUM_NONE);
}
int main() {
    try {
        exercise<idEntity>("entity",-1,MAX_GENTITIES,ENTITYNUM_NONE);
        exercise<rvClientEntity>("client entity",-2,MAX_CENTITIES,-1);
        tableCases<idEntity>(ENTITYNUM_NONE); tableCases<rvClientEntity>(-1); reservedSlot();
        std::puts("restore identity: bounds, mismatch, duplicate registration, reserved slot, interrupted and valid reads passed");
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
'''


def source(mode: str) -> str:
    code = SUPPORT
    code += "template< class T, int Count >\n" + function_body(
        (ROOT / "src" / mode / "Game_local.cpp").read_text(),
        "static void GameLocal_RestoreEntityTable(") + "\n"
    for relative, signature, boundary in (
        ("Entity.cpp", "void idEntity::Restore(", "savefile->ReadInt( entityDefNumber );"),
        ("client/ClientEntity.cpp", "void rvClientEntity::Restore(", "// idLinkList<rvClientEntity>"),
    ):
        body = function_body((ROOT / "src" / mode / relative).read_text(), signature)
        # The fixture executes the complete identity field transaction. Later
        # unrelated fields and rendering/script dependencies are outside it.
        code += body[:body.index(boundary)] + "}\n"
    return code + MAIN


def main() -> None:
    if len(sys.argv) == 4 and sys.argv[1] == "--generate-native":
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding="utf-8")
        return
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    with tempfile.TemporaryDirectory(prefix="restore-identity-", dir=ROOT / ".tmp") as directory:
        scratch = Path(directory)
        for mode in sys.argv[1:] or ("game", "mpgame"):
            cpp, executable = scratch / (mode + ".cpp"), scratch / (mode + ".exe")
            cpp.write_text(source(mode), encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
