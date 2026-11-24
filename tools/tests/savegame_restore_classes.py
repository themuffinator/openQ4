#!/usr/bin/env python3
"""Run production object-table admission with observed factory calls.

The small registry supplies type ranges and factories, not game restore methods.
CreateObjects, IsType and its local exception cleanup come from production.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
static int factoryCalls=0, alive=0;
static constexpr int MAX_SAVEGAME_OBJECTS=65536;
struct idTypeInfo;
struct idStr : std::string {
    using std::string::string;
    bool IsEmpty() const { return empty(); }
};
struct idClass {
    idClass() { ++alive; }
    virtual ~idClass() { --alive; }
    static idTypeInfo* GetClass(const idStr& name);
};
struct idTypeInfo {
    const char* classname;
    int typeNum, lastChild, failure=0;
    bool IsType(const idTypeInfo& type) const;
    idClass* CreateInstance() {
        ++factoryCalls;
        if(failure==1)return nullptr;
        if(failure==2)throw std::runtime_error("abstract factory");
        return new idClass;
    }
};
static idTypeInfo registry[] = {
    {"idClass",0,99},
    {"idEntity",10,19}, {"idPlayer",11,11}, {"rvViewWeapon",12,12},
    {"idThread",20,22}, {"DerivedThread",21,21},
    {"rvClientEntity",30,39,2}, {"rvClientEffect",31,31}, {"NullClient",32,32,1},
    {"rvWeapon",40,49}, {"DerivedWeapon",41,41},
    {"idPhysics",50,59}, {"idPhysics_Player",51,51}, {"Unrelated",60,60}
};
idTypeInfo* idClass::GetClass(const idStr& name) {
    for(auto& type:registry)if(name==type.classname)return &type;
    return nullptr;
}
struct idEntity { static idTypeInfo& GetClassType(){return registry[1];} };
struct idThread { static idTypeInfo& GetClassType(){return registry[4];} };
struct rvClientEntity { static idTypeInfo& GetClassType(){return registry[6];} };
template<class T> struct idList {
    std::vector<T> entries;
    int Num() const { return static_cast<int>(entries.size()); }
    void SetNum(int n) { entries.resize(n); }
    T& operator[](int n) { return entries.at(n); }
    T* Ptr() { return entries.data(); }
    void Clear() { entries.clear(); }
};
struct File { int Tell() const { return 0; } };
struct idRestoreGame {
    File storage;
    File* file=&storage;
    int count=0, cursor=0, begins=0, tracked=0;
    std::vector<idStr> names;
    idList<idClass*> objects;
    explicit idRestoreGame(const std::vector<idStr>& input):count(static_cast<int>(input.size())),names(input) {}
    ~idRestoreGame() { for(auto object:objects.entries)delete object; }
    void ReadInt(int& value) { value=count; }
    void ReadString(idStr& value) { value=names.at(cursor++); }
    void BeginObjectOwnership() { ++begins; }
    void TrackObject(int) { ++tracked; }
    void CreateObjects();
    [[noreturn]] void Error(const char* format,...) {
        char message[512]; va_list args; va_start(args,format);
        vsnprintf(message,sizeof(message),format,args); va_end(args);
        throw std::runtime_error(message);
    }
};
static void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
'''
MAIN = r'''
int main() {
    int failures=0;
    for(const char* bad:{"idClass","rvWeapon","DerivedWeapon","idPhysics","idPhysics_Player","Unrelated","Unknown",""}) {
        for(bool late:{false,true}) {
            try {
                factoryCalls=0;
                std::vector<idStr> names=late?std::vector<idStr>{"idPlayer","rvClientEffect",bad}:std::vector<idStr>{bad,"idPlayer"};
                idRestoreGame restore(names);
                std::string error;
                try { restore.CreateObjects(); } catch(const std::runtime_error& e) { error=e.what(); }
                require(!error.empty(),"class was admitted");
                require(factoryCalls==0 && alive==0 && restore.begins==0,"factory ran before whole table validation");
                require(restore.objects.Num()==0,"rejected table published objects");
                const char* expected=!*bad?"empty class name":std::string(bad)=="Unknown"?"Unknown class":"unsupported object-table class";
                require(error.find(expected)!=std::string::npos,"wrong rejection reason");
            } catch(const std::exception& e) {
                std::fprintf(stderr,"%s %s: %s\n",bad,late?"late":"early",e.what()); ++failures;
            }
        }
    }
    try {
        factoryCalls=0;
        {
            idRestoreGame restore({"idEntity","idPlayer","rvViewWeapon","idThread","DerivedThread","rvClientEffect"});
            restore.CreateObjects();
            require(factoryCalls==6 && alive==6 && restore.tracked==6 && restore.begins==1,"supported family rejected");
            require(restore.objects.Num()==7 && restore.objects[0]==nullptr,"object numbering changed");
        }
        require(alive==0,"successful table leaked");
        for(const char* failed:{"NullClient","rvClientEntity"}) {
            factoryCalls=0;
            idRestoreGame restore({"idPlayer",failed});
            std::string error;
            try { restore.CreateObjects(); } catch(const std::runtime_error& e) { error=e.what(); }
            require(!error.empty() && factoryCalls==2 && restore.begins==1,"allowed factory not reached");
            require(alive==0 && restore.objects.Num()==0,"factory exception leaked table");
        }
        for(int count:{-1,MAX_SAVEGAME_OBJECTS+1}) {
            factoryCalls=0;
            idRestoreGame restore({}); restore.count=count;
            bool rejected=false;
            try { restore.CreateObjects(); } catch(const std::runtime_error&) { rejected=true; }
            require(rejected && !factoryCalls && !restore.begins,"invalid count admitted");
        }
        idRestoreGame empty({}); empty.CreateObjects();
        require(empty.objects.Num()==1 && empty.objects[0]==nullptr,"empty table failed");
    } catch(const std::exception& e) { std::fprintf(stderr,"controls: %s\n",e.what()); ++failures; }
    std::printf("restore class admission: %d failures (16 rejected tables, supported families, count/factory controls)\n",failures);
    return failures?1:0;
}
'''


def source(mode):
    directory = ROOT/'src'/mode/'gamesys'
    saves = (directory/'SaveGame.cpp').read_text()
    cleanup = saves.index('class idScopedUnrestoredObjectCleanup {')
    cleanup_end = saves.index('\n};', cleanup)+3
    return (SUPPORT
            + function_body((directory/'Class.h').read_text(), 'ID_INLINE bool idTypeInfo::IsType(').replace('ID_INLINE ', '')+'\n'
            + function_body(saves, 'static void SaveGame_DeleteUnrestoredObjects(')+'\n'
            + saves[cleanup:cleanup_end]+'\n'
            + function_body(saves, 'void idRestoreGame::CreateObjects(')+'\n'+MAIN)


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    failures = 0
    with tempfile.TemporaryDirectory(prefix='save-classes-', dir=ROOT/'.tmp') as directory:
        for mode in ('game',):
            cpp = Path(directory)/(mode+'.cpp')
            exe = cpp.with_suffix('.exe')
            cpp.write_text(source(mode), encoding='utf-8')
            subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            print(mode, flush=True)
            failures += subprocess.run([str(exe)]).returncode != 0
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
