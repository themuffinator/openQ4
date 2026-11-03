#!/usr/bin/env python3
"""Exercise production restore cleanup against observed object lifetimes."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
static std::unordered_set<const void*> alive;
struct idClass {
    idClass() { alive.insert(this); } virtual ~idClass();
    virtual bool IsType(int) const { return false; }
};
// The ownership fixture uses non-entity objects; entity links are covered by
// savegame_restore_bindings.py with the production teardown methods.
struct idEntity : idClass {
    static int GetClassType() { return 1; }
    void PrepareFailedRestoreShutdown() {}
};
struct idEvent {
    static void CancelEvents(idClass* object) {
        if(alive.erase(object)!=1) std::terminate();
    }
};
// Observe a stale list lookup before it can dereference a destroyed object.
// Production cleanup still decides which pointers to visit and in what order.
template<class T> class idList {
public:
    std::vector<T> entries;
    struct Slot {
        T& value;
        operator T() const {
            if(value && !alive.count(value)) {
                std::fputs("cleanup revisited an object already destroyed by another owner\n",stderr);
                std::exit(41);
            }
            return value;
        }
        Slot& operator=(T value_) { value=value_; return *this; }
    };
    int Num() const { return static_cast<int>(entries.size()); }
    Slot operator[](int index) { return Slot{entries.at(index)}; }
    T operator[](int index) const { return entries.at(index); }
    void Clear() { entries.clear(); }
};
class idHashIndex {
    std::unordered_map<int,std::vector<int>> chains;
    std::unordered_map<int,int> keys;
public:
    void Clear() { chains.clear(); keys.clear(); }
    void Add(int key,int index) { chains[key].push_back(index); keys[index]=key; }
    int First(int key) const {
        auto found=chains.find(key);
        return found==chains.end() || found->second.empty() ? -1 : found->second.front();
    }
    int Next(int index) const {
        const auto& chain=chains.at(keys.at(index));
        auto found=std::find(chain.begin(),chain.end(),index);
        return found==chain.end() || ++found==chain.end() ? -1 : *found;
    }
    void Remove(int key,int index) {
        auto& chain=chains.at(key); chain.erase(std::find(chain.begin(),chain.end(),index)); keys.erase(index);
    }
};
struct idRestoreGame {
    idList<idClass*> objects;
    idList<idClass*> postRestoreObjects;
    idHashIndex objectHash;
    bool ownsObjects=false;
    idRestoreGame* previousRestore=nullptr;
    static thread_local idRestoreGame* activeRestore;
    ~idRestoreGame();
    void BeginObjectOwnership();
    void TrackObject(int index);
    void ReleaseObjects();
    void DeleteObjects();
    static void ObjectDestroyed(const idClass* object);
    static void PrepareMapShutdown();
    static void DeleteOrphanedObjects();
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("invalid restore ownership"); }
};
thread_local idRestoreGame* idRestoreGame::activeRestore=nullptr;
struct GameLocal {
    idClass* frameCommandThread=nullptr;
    std::vector<idClass*> registered;
    int shutdowns=0;
    void MapShutdown() {
        ++shutdowns;
        idRestoreGame::PrepareMapShutdown();
        for(auto object:registered) if(alive.count(object)) delete object;
        registered.clear();
        idRestoreGame::DeleteOrphanedObjects();
    }
} gameLocal;
struct Owner : idClass { idClass* child=nullptr; ~Owner() { delete child; } };
'''
MAIN = r'''
int main(int argc,char** argv) {
    const std::string scenario=argc>1?argv[1]:"cascade";
    try {
        for(int cycle=0;cycle<1000;++cycle) {
            try {
                idRestoreGame restore;
                auto parent=new Owner;
                auto child=new idClass;
                restore.objects.entries={nullptr,parent,child,nullptr};
                restore.BeginObjectOwnership();
                restore.TrackObject(1); restore.TrackObject(2);
                if(scenario!="independent") parent->child=child;
                if(scenario=="external") delete parent;
                if(scenario=="exception") throw std::logic_error("interrupted restore");
                if(scenario=="rollback") gameLocal.registered.push_back(parent);
                else if(scenario=="commit") {
                    restore.ReleaseObjects();
                    if(alive.size()!=2) throw std::runtime_error("commit deleted live objects");
                    delete parent;
                } else restore.DeleteObjects();
            } catch(const std::logic_error&) { if(scenario!="exception") throw; }
            if(!alive.empty() || idRestoreGame::activeRestore) throw std::runtime_error("cleanup leaked objects or owner");
        }
        if(gameLocal.shutdowns!=((scenario=="rollback" || scenario=="exception")?1000:0)) throw std::runtime_error("wrong map shutdown count");
        // Inner restore scopes must not hide deletions owned by an outer scope.
        {
            idRestoreGame outer,inner;
            auto first=new idClass; auto second=new idClass;
            outer.objects.entries={nullptr,first}; inner.objects.entries={nullptr,second};
            outer.postRestoreObjects.entries={first,second};
            outer.BeginObjectOwnership(); outer.TrackObject(1);
            inner.BeginObjectOwnership(); inner.TrackObject(1);
            delete first;
            if(outer.postRestoreObjects.entries[0]) throw std::runtime_error("destroyed fixup target retained");
            outer.ReleaseObjects();
            if(outer.postRestoreObjects.Num()) throw std::runtime_error("released fixup targets retained");
            if(idRestoreGame::activeRestore!=&inner || inner.previousRestore) throw std::runtime_error("nested owner unlink failed");
            inner.DeleteObjects();
        }
        if(!alive.empty() || idRestoreGame::activeRestore) throw std::runtime_error("nested cleanup leaked");
        std::printf("restore cleanup %s: passed\n",scenario.c_str());
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"restore cleanup %s: %s\n",scenario.c_str(),error.what());
        return 1;
    }
}
'''


def source(mode: str) -> str:
    directory = ROOT / "src" / mode / "gamesys"
    source_text = (directory / "SaveGame.cpp").read_text()
    creation = function_body(source_text, "void idRestoreGame::CreateObjects(")
    assert creation.index("BeginObjectOwnership();") < creation.index("->CreateInstance()") < creation.index("TrackObject( i );")
    game_source = (directory.parent / "Game_local.cpp").read_text()
    restore = function_body(game_source, "bool idGameLocal::InitFromSaveGame(")
    assert restore.index("savegame.ReleaseObjects();") < restore.index("gamestate = GAMESTATE_ACTIVE;")
    shutdown = function_body(game_source, "void idGameLocal::MapShutdown(")
    assert shutdown.index("MapClear( true );") < shutdown.index("idRestoreGame::DeleteOrphanedObjects();") < shutdown.index("program.Restart();")
    code = SUPPORT + function_body(source_text, "static int SaveGame_ObjectHashKey(") + "\n"
    code += function_body(source_text, "static void SaveGame_DeleteUnrestoredObjects(") + "\n"
    for name in ("BeginObjectOwnership", "TrackObject", "ObjectDestroyed", "ReleaseObjects", "PrepareMapShutdown", "DeleteObjects", "DeleteOrphanedObjects"):
        code += function_body(source_text, "void idRestoreGame::" + name + "(") + "\n"
    code += function_body(source_text, "idRestoreGame::~idRestoreGame() {") + "\n"
    return code + function_body((directory / "Class.cpp").read_text(), "idClass::~idClass()") + "\n" + MAIN


def main() -> int:
    if len(sys.argv)==4 and sys.argv[1]=="--generate-native":
        Path(sys.argv[3]).write_text(source(sys.argv[2]),encoding="utf-8")
        return 0
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    failures = 0
    mutation = "--mutation-check" in sys.argv
    with tempfile.TemporaryDirectory(prefix="restore-cleanup-", dir=ROOT / ".tmp") as directory:
        scratch = Path(directory)
        for mode in ("game", "mpgame"):
            cpp, executable = scratch / (mode + ".cpp"), scratch / (mode + ".exe")
            code = source(mode)
            if mutation:
                hook = "idRestoreGame::ObjectDestroyed( this );"
                assert code.count(hook)==1
                code=code.replace(hook, "")
            cpp.write_text(code, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)], check=True)
            for scenario in (("cascade", "external") if mutation else ("independent", "cascade", "external", "commit", "rollback", "exception")):
                result = subprocess.run([str(executable), scenario]).returncode
                failures += result != (41 if mutation else 0)
                if mutation and result==41:
                    print(f"{mode}: missing destruction notification rejected for {scenario}",flush=True)
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
