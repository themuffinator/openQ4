#!/usr/bin/env python3
"""Exercise production mover teardown and restored activation chains."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <string>
#include <vector>
constexpr int MAX_GENTITIES=8;
struct idRestoreGame;
static int traversals=0;
struct idClass { virtual ~idClass()=default; virtual bool IsType(int) const { return false; } };
struct idEntity : idClass {
    bool alive=true,basePrepared=false;
    mutable int baseValidated=0;
    std::string name="fixture";
    static int GetClassType() { return 1; }
    bool IsType(int type) const override { return type==1; }
    virtual void PrepareFailedRestoreShutdown() { basePrepared=true; }
    virtual void ValidateRestoredBindings(idRestoreGame*) const { ++baseValidated; }
    void SetPhysics(void*) {}
};
struct idMover_Binary;
struct Link {
    idMover_Binary* owner; idMover_Binary* value=nullptr;
    operator idMover_Binary*() const;
    idMover_Binary* operator->() const;
    Link& operator=(idMover_Binary* pointer) { value=pointer; return *this; }
    Link& operator=(const Link& other) { return *this=static_cast<idMover_Binary*>(other); }
};
struct idMover_Binary : idEntity {
    Link moveMaster{this},activateChain{this};
    void Cleanup();
    void RestoreMoverLinks(idRestoreGame*);
    MOVER_OVERRIDES
};
Link::operator idMover_Binary*() const {
    if(++traversals>1024)throw std::runtime_error("cyclic mover traversal during teardown");
    if(!owner->alive || (value && !value->alive))throw std::runtime_error("freed mover link during teardown");
    return value;
}
idMover_Binary* Link::operator->() const {
    auto pointer=static_cast<idMover_Binary*>(*this);
    if(!pointer)throw std::runtime_error("null mover link during teardown");
    return pointer;
}
template<class T> struct idList {
    std::vector<T> entries;
    int Num() const { return int(entries.size()); }
    T operator[](int index) const { return entries.at(index); }
};
struct idRestoreGame {
    idList<idClass*> objects;
    idRestoreGame* previousRestore=nullptr;
    static thread_local idRestoreGame* activeRestore;
    static void PrepareMapShutdown();
    std::vector<idMover_Binary*> references;
    int read=0,failAt=-1;
    void ReadObject(idMover_Binary*& output) { output=references.at(read); if(read++==failAt)throw std::logic_error("interrupted link read"); }
    void ReadObject(Link& output) { output=references.at(read); if(read++==failAt)throw std::logic_error("interrupted link read"); }
    [[noreturn]] void Error(const char* format,...) {
        char message[256]; va_list args; va_start(args,format);
        std::vsnprintf(message,sizeof(message),format,args); va_end(args);
        throw std::runtime_error(message);
    }
};
thread_local idRestoreGame* idRestoreGame::activeRestore=nullptr;
struct { idClass* frameCommandThread=nullptr; } gameLocal;
static void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
'''
VALIDATION=r'''
static void reject(idMover_Binary& mover,idRestoreGame& restore,bool reading) {
    bool failed=false;
    try {
        if(reading)mover.RestoreMoverLinks(&restore);
        else static_cast<idEntity&>(mover).ValidateRestoredBindings(&restore);
    } catch(const std::runtime_error& error) {
        failed=std::string(error.what()).find("idMover_Binary::Restore:")==0;
        if(!failed)throw;
    }
    require(failed,"invalid mover links were accepted");
}
static void validation() {
    for(int interrupted=0;interrupted<2;++interrupted) {
        traversals=0;
        idMover_Binary mover,parent,tail;
        idRestoreGame restore; restore.references={&parent,&tail}; restore.failAt=interrupted;
        bool failed=false;
        try { mover.RestoreMoverLinks(&restore); } catch(const std::logic_error&) { failed=true; }
        require(failed && !mover.moveMaster.value && !mover.activateChain.value,"interrupted read published mover links");
    }
    for(int graph=0;graph<5;++graph) {
        traversals=0;
        idMover_Binary mover,first,second;
        idRestoreGame restore; restore.references={&mover,nullptr};
        if(graph==0)restore.references[1]=&mover;
        if(graph==1) { restore.references[1]=&first; first.activateChain=&mover; }
        if(graph==2) { restore.references[1]=&first; first.activateChain=&second; second.activateChain=&first; }
        if(graph==3) { restore.references[0]=&first; first.moveMaster=&mover; }
        if(graph==4) { restore.references[0]=&first; first.moveMaster=&second; second.moveMaster=&first; }
        reject(mover,restore,true);
        require(!mover.moveMaster.value && !mover.activateChain.value,"invalid mover links published");
    }
    for(int graph=0;graph<5;++graph) {
        traversals=0;
        idMover_Binary root,member,other;
        idRestoreGame restore;
        idMover_Binary* subject=&root;
        if(graph==0)root.activateChain=&member;
        if(graph==1) { root.moveMaster=&member; member.moveMaster=&other; }
        if(graph>=2) { root.moveMaster=&root; root.activateChain=&member; member.moveMaster=&root; }
        if(graph==2) { other.moveMaster=&root; subject=&other; }
        if(graph==3)member.activateChain=&root;
        if(graph==4)member.moveMaster=&other;
        reject(*subject,restore,false);
    }
    for(int reverse=0;reverse<2;++reverse) {
        traversals=0;
        idMover_Binary movers[MAX_GENTITIES];
        for(int i=MAX_GENTITIES-1;i>=0;--i) {
            idRestoreGame restore;
            restore.references={&movers[0],i+1<MAX_GENTITIES?&movers[i+1]:nullptr};
            movers[i].RestoreMoverLinks(&restore);
        }
        for(auto& mover:movers) {
            idRestoreGame restore;
            static_cast<idEntity&>(mover).ValidateRestoredBindings(&restore);
            require(mover.baseValidated==1,"mover bypassed base binding validation");
        }
        for(int i=0;i<MAX_GENTITIES;++i) {
            auto& mover=movers[reverse?MAX_GENTITIES-1-i:i]; mover.Cleanup(); mover.alive=false;
        }
    }
    traversals=0;
    idMover_Binary standalone;
    standalone.moveMaster=&standalone;
    idRestoreGame restore;
    standalone.ValidateRestoredBindings(&restore);
    standalone.Cleanup();
    std::puts("mover validation: interrupted/cyclic/inconsistent links and valid forward references passed");
}
'''
MAIN=r'''
int main(int argc,char** argv) {
    bool repro=argc>1 && std::string(argv[1])=="repro";
    int failures=0;
    for(int scenario=0;scenario<5;++scenario) {
        traversals=0;
        idMover_Binary first,second,outside;
        idRestoreGame restore;
        restore.objects.entries={nullptr,&first,&second}; idRestoreGame::activeRestore=&restore;
        outside.activateChain=&outside;
        if(scenario==0) { first.moveMaster=&first; first.activateChain=&first; }
        if(scenario==1) { first.moveMaster=&first; first.activateChain=&second; second.moveMaster=&first; second.activateChain=&first; }
        if(scenario==2) { first.moveMaster=&first; second.moveMaster=&first; }
        if(scenario==3) { first.moveMaster=&second; second.moveMaster=&first; }
        if(scenario==4) { first.moveMaster=&first; first.activateChain=&second; second.moveMaster=&first; }
        try {
            if(!repro)idRestoreGame::PrepareMapShutdown();
            require(outside.activateChain.value==&outside && !outside.basePrepared,"unrelated mover changed");
            if(!repro)require(first.basePrepared && second.basePrepared,"base cleanup omitted");
            first.Cleanup(); first.alive=false; second.Cleanup(); second.alive=false;
            std::printf("mover cleanup %d: passed\n",scenario);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"mover cleanup %d: %s\n",scenario,error.what()); ++failures;
        }
        idRestoreGame::activeRestore=nullptr;
    }
    if(!repro)try { validation(); } catch(const std::exception& error) {
        std::fprintf(stderr,"mover validation: %s\n",error.what()); ++failures;
    }
    return failures?1:0;
}
'''


def source(mode):
    mover=(ROOT/'src'/mode/'Mover.cpp').read_text()
    saves=(ROOT/'src'/mode/'gamesys/SaveGame.cpp').read_text()
    implemented='void idMover_Binary::PrepareFailedRestoreShutdown(' in mover
    overrides=('void PrepareFailedRestoreShutdown() override;\nvoid ValidateRestoredBindings(idRestoreGame*) const override;'
               if implemented else '')
    code=SUPPORT.replace('MOVER_OVERRIDES',overrides)
    deletion=function_body(mover,'idMover_Binary::~idMover_Binary()')
    code+=deletion.replace('idMover_Binary::~idMover_Binary()','void idMover_Binary::Cleanup()')
    code+='\n'+function_body(saves,'void idRestoreGame::PrepareMapShutdown(')
    if implemented:
        for name in ('PrepareFailedRestoreShutdown','RestoreMoverLinks','ValidateRestoredBindings'):
            code+='\n'+function_body(mover,'void idMover_Binary::'+name+'(')
        assert 'RestoreMoverLinks( savefile );' in function_body(mover,'void idMover_Binary::Restore(')
        entity_header=(ROOT/'src'/mode/'Entity.h').read_text()
        for name in ('PrepareFailedRestoreShutdown','ValidateRestoredBindings'):
            assert any('virtual' in line and name in line for line in entity_header.splitlines())
    else:
        code+='\nvoid idMover_Binary::RestoreMoverLinks(idRestoreGame* savefile) { savefile->ReadObject(moveMaster); savefile->ReadObject(activateChain); }\n'
    return code+VALIDATION+MAIN


def main():
    if len(sys.argv)==4 and sys.argv[1]=='--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]),encoding='utf-8');return 0
    compiler=next((p for name in ('clang++','g++','c++') if(p:=shutil.which(name))),None)
    if not compiler:raise RuntimeError('C++17 compiler required')
    failures=0
    with tempfile.TemporaryDirectory(prefix='save-movers-',dir=ROOT/'.tmp') as directory:
        for mode in ('game',):
            cpp=Path(directory)/(mode+'.cpp');exe=cpp.with_suffix('.exe')
            cpp.write_text(source(mode),encoding='utf-8')
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
            print(mode,flush=True)
            failures+=subprocess.run([str(exe),*(['repro'] if '--repro' in sys.argv else [])]).returncode!=0
    return int(failures!=0)


if __name__=='__main__':raise SystemExit(main())
