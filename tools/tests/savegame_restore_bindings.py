#!/usr/bin/env python3
"""Run actual entity unbind/team teardown against checked partial restore links.

Checked links stop invalid dereferences or endless traversal deterministically.
They are test storage, not a replacement for the production teardown methods.
"""
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
#define assert(value) do { if(!(value))throw std::runtime_error("incomplete team invariant during teardown"); } while(false)
struct idEntity;
struct idRestoreGame;
using jointHandle_t=int;
constexpr int MAX_GENTITIES=8;
static int traversals=0;
static int traversalLimit=128;
struct Link {
    idEntity* owner; idEntity* value=nullptr;
    operator idEntity*() const;
    idEntity* operator->() const;
    Link& operator=(idEntity* pointer) { value=pointer; return *this; }
    Link& operator=(const Link& other) { return *this=static_cast<idEntity*>(other); }
};
struct idClass { virtual ~idClass()=default; virtual bool IsType(int) const { return false; } };
struct Physics { void SetMaster(idEntity*,bool) {} };
constexpr int INVALID_JOINT=-1,EV_Remove=0;
constexpr int NUM_SIGNALS=2;
struct SignalThreads { int pending=1; void Clear() { pending=0; } };
struct signalList_t { SignalThreads signal[NUM_SIGNALS]; };
struct idEntity : idClass {
    bool alive=true;
    std::string name="fixture";
    signalList_t* signals=nullptr;
    Link bindMaster{this},teamMaster{this},teamChain{this};
    Physics* physics=nullptr;
    struct { bool bindOrientated=false; } fl;
    int bindJoint=-1,bindBody=-1;
    static int GetClassType() { return 1; }
    bool IsType(int type) const override { return type==1; }
    void PrepareFailedRestoreShutdown();
    void RestoreBindingLinks(idRestoreGame*);
    void ValidateRestoredBindings(idRestoreGame*) const;
    void Unbind(); void QuitTeam(); void RemoveBinds();
    bool IsBoundTo(const idEntity*) const;
    void PreUnbind() {} void PostUnbind() {} void PostEventMS(const int*,int) {}
    void Cleanup() { RemoveBinds(); Unbind(); QuitTeam(); alive=false; }
};
struct idAFEntity_Base : idEntity {
    static int GetClassType() { return 2; }
    void RemoveBindConstraints() {}
};
Link::operator idEntity*() const {
    if(++traversals>traversalLimit)throw std::runtime_error("cyclic team traversal during teardown");
    if(!owner->alive || (value && !value->alive))throw std::runtime_error("freed entity link during teardown");
    return value;
}
idEntity* Link::operator->() const {
    idEntity* pointer=static_cast<idEntity*>(*this);
    if(!pointer)throw std::runtime_error("null team link during teardown");
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
    std::vector<idEntity*> references;
    int read=0,reference=0,failAt=-1;
    void Boundary() { if(read++==failAt)throw std::logic_error("interrupted read"); }
    void ReadObject(idEntity*& result) { result=references.at(reference++); Boundary(); }
    void ReadJoint(int& result) { result=-1; Boundary(); }
    void ReadInt(int& result) { result=-1; Boundary(); }
    [[noreturn]] void Error(const char* format,...) {
        char message[256]; va_list args; va_start(args,format);
        std::vsnprintf(message,sizeof(message),format,args); va_end(args);
        throw std::runtime_error(message);
    }
};
thread_local idRestoreGame* idRestoreGame::activeRestore=nullptr;
struct { idClass* frameCommandThread=nullptr; } gameLocal;
'''
VALIDATION=r'''
static void expectRejected(idEntity& entity,idRestoreGame& restore,bool reading) {
    bool rejected=false;
    try {
        if(reading)entity.RestoreBindingLinks(&restore);
        else entity.ValidateRestoredBindings(&restore);
    } catch(const std::runtime_error& error) {
        // The production validator must reject the graph before checked links
        // detect unsafe traversal, so a test-storage exception is not a pass.
        rejected=std::string(error.what()).find("idEntity::Restore:")==0;
        if(!rejected)throw;
    }
    assert(rejected);
}
static void runValidation() {
    for(int fail=0;fail<5;++fail) {
        traversals=0;
        idEntity entity,parent,tail;
        entity.bindJoint=17; entity.bindBody=18;
        idRestoreGame restore; restore.references={&parent,&parent,&tail}; restore.failAt=fail;
        bool interrupted=false;
        try { entity.RestoreBindingLinks(&restore); } catch(const std::logic_error&) { interrupted=true; }
        assert(interrupted && !entity.bindMaster.value && !entity.teamMaster.value && !entity.teamChain.value);
        assert(entity.bindJoint==17 && entity.bindBody==18);
    }
    for(int graph=0;graph<8;++graph) {
        traversals=0;
        idEntity entity,first,second;
        idRestoreGame restore; restore.references={nullptr,nullptr,nullptr};
        if(graph==0)restore.references[0]=&entity;
        if(graph==1) { restore.references[0]=&first; first.bindMaster=&entity; }
        if(graph==2) { restore.references[0]=&first; first.bindMaster=&second; second.bindMaster=&first; }
        if(graph==3)restore.references[2]=&entity;
        if(graph==4) { restore.references[2]=&first; first.teamChain=&entity; }
        if(graph==5) { restore.references[2]=&first; first.teamChain=&second; second.teamChain=&first; }
        if(graph==6) { restore.references[1]=&first; first.teamMaster=&entity; }
        if(graph==7) { restore.references[1]=&first; first.teamMaster=&second; second.teamMaster=&first; }
        expectRejected(entity,restore,true);
        assert(!entity.bindMaster.value && !entity.teamMaster.value && !entity.teamChain.value);
    }
    for(int graph=0;graph<10;++graph) {
        traversals=0;
        idEntity root,member,other,tail;
        idRestoreGame restore;
        idEntity* subject=&root;
        if(graph==0)root.teamChain=&member;
        if(graph==1)root.bindMaster=&member;
        if(graph==2) { root.teamMaster=&member; member.teamMaster=&other; }
        if(graph==3)root.teamMaster=&root;
        if(graph>=4) {
            root.teamMaster=&root; root.teamChain=&member; member.teamMaster=&root;
        }
        if(graph==4) { other.teamMaster=&root; subject=&other; } // absent backlink
        if(graph==5)member.teamChain=&root; // cycle, even if reading is bypassed
        if(graph==6)member.teamMaster=&other; // inconsistent root
        if(graph==7)root.bindMaster=&member; // root cannot bind to its child
        if(graph==8)member.bindMaster=&other; // parent outside the team
        if(graph==9) {
            member.bindMaster=&root; member.teamChain=&other; other.teamMaster=&root;
            other.bindMaster=&root; other.teamChain=&tail; tail.teamMaster=&root;
            tail.bindMaster=&member; // descendant after its parent's subtree closed
        }
        expectRejected(*subject,restore,false);
    }
    for(int shape=0;shape<4;++shape) for(int reverse=0;reverse<2;++reverse) {
        traversals=0;
        idEntity entities[MAX_GENTITIES];
        int parents[MAX_GENTITIES]={-1,0,1,1,0,4,-1,6};
        for(int i=1;i<MAX_GENTITIES;++i) {
            if(shape==1)parents[i]=i-1; // maximum-depth binding tree
            if(shape==2)parents[i]=0; // siblings
            if(shape==3)parents[i]=-1; // unbound physics team
        }
        // Read children before parents: a valid save may contain forward links.
        for(int i=MAX_GENTITIES-1;i>=0;--i) {
            idRestoreGame restore;
            restore.references={parents[i]<0?nullptr:&entities[parents[i]],&entities[0],
                                i+1<MAX_GENTITIES?&entities[i+1]:nullptr};
            entities[i].RestoreBindingLinks(&restore);
        }
        for(auto& entity:entities) { idRestoreGame restore; entity.ValidateRestoredBindings(&restore); }
        for(int i=0;i<MAX_GENTITIES;++i)entities[reverse?MAX_GENTITIES-1-i:i].Cleanup();
    }
    idEntity standalone;
    idRestoreGame restore;
    standalone.ValidateRestoredBindings(&restore);
    std::puts("binding validation: 5 interrupted reads, 8 cyclic inputs, 10 invalid teams and 8 forward-linked valid teams passed");
}
'''
MAIN=r'''
int main(int argc,char** argv) {
    const bool repro=argc>1 && std::string(argv[1])=="repro";
    int failures=0;
    for(int scenario=0;scenario<5;++scenario) {
        traversals=0;
        idEntity first,second,outside;
        idClass other;
        idRestoreGame outer,inner;
        outer.objects.entries={nullptr,&first,&other}; inner.objects.entries={nullptr,&second,nullptr};
        inner.previousRestore=&outer;
        idRestoreGame::activeRestore=&inner;
        outside.teamChain=&outside;
        // Teardown must not run SIG_REMOVED threads of partly restored entities.
        signalList_t firstSignals,outsideSignals;
        first.signals=&firstSignals; outside.signals=&outsideSignals;
        if(scenario==0)first.teamChain=&first;
        if(scenario==1) { first.teamChain=&second; second.teamChain=&first; }
        if(scenario==2) { first.teamMaster=&first; } // master without its tail
        if(scenario==3) { second.teamMaster=&first; second.bindMaster=&first; } // parent lacks backlink
        if(scenario==4) { first.teamMaster=&first; first.teamChain=&second; second.teamMaster=&first; second.bindMaster=&first; }
        try {
            if(!repro)idRestoreGame::PrepareMapShutdown();
            assert(outside.teamChain.value==&outside); // unrelated live entities are untouched
            if(!repro) {
                assert(firstSignals.signal[0].pending==0 && firstSignals.signal[1].pending==0);
                assert(outsideSignals.signal[0].pending==1 && outsideSignals.signal[1].pending==1);
            }
            first.Cleanup(); second.Cleanup();
            std::printf("binding cleanup %d: passed\n",scenario);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"binding cleanup %d: %s\n",scenario,error.what()); ++failures;
        }
        idRestoreGame::activeRestore=nullptr;
    }
    // A committed restore has no active owner and preserves ordinary live links.
    traversals=0;
    idEntity root,member;
    root.teamMaster=&root; root.teamChain=&member;
    member.teamMaster=&root; member.bindMaster=&root;
    idRestoreGame::PrepareMapShutdown();
    assert(root.teamMaster.value==&root && root.teamChain.value==&member && member.bindMaster.value==&root);
    root.Cleanup(); member.Cleanup();
    if(!repro) {
        traversalLimit=100000;
        try {
            runValidation();
        } catch(const std::exception& error) {
            std::fprintf(stderr,"binding validation: %s\n",error.what()); ++failures;
        }
    }
    return failures ? 1 : 0;
}
'''


def source(mode):
    entity=(ROOT/'src'/mode/'Entity.cpp').read_text()
    saves=(ROOT/'src'/mode/'gamesys/SaveGame.cpp').read_text()
    code=SUPPORT
    for name in ('RemoveBinds','Unbind','QuitTeam'):
        code+='\n'+function_body(entity,'void idEntity::'+name+'(')
    code+='\n'+function_body(entity,'bool idEntity::IsBoundTo(')
    if 'void idRestoreGame::PrepareMapShutdown(' in saves:
        code+='\n'+function_body(saves,'void idRestoreGame::PrepareMapShutdown(')
        code+='\n'+function_body(entity,'void idEntity::PrepareFailedRestoreShutdown(')
        code+='\n'+function_body(entity,'void idEntity::RestoreBindingLinks(')
        code+='\n'+function_body(entity,'void idEntity::ValidateRestoredBindings(')
        restoring=function_body(entity,'void idEntity::Restore(')
        assert 'RestoreBindingLinks( savefile );' in restoring
        restoring=function_body(saves,'void idRestoreGame::RestoreObjects(')
        assert restoring.index('ReadSaveGameFooter();') < restoring.index('ValidateRestoredBindings( this );') < restoring.index('ent->UpdateVisuals();')
        local=(ROOT/'src'/mode/'Game_local.cpp').read_text()
        shutdown=function_body(local,'void idGameLocal::MapShutdown(')
        assert shutdown.index('idRestoreGame::PrepareMapShutdown();') < shutdown.index('MapClear( true );')
        deletion=function_body(saves,'void idRestoreGame::DeleteObjects(')
        assert deletion.index('PrepareMapShutdown();') < deletion.index('SaveGame_DeleteUnrestoredObjects(')
    else:
        code+='\nvoid idRestoreGame::PrepareMapShutdown() {}\n'
        code+='\nvoid idEntity::RestoreBindingLinks(idRestoreGame*) {}\nvoid idEntity::ValidateRestoredBindings(idRestoreGame*) const {}\n'
    return code+VALIDATION+MAIN


def main():
    if len(sys.argv)==4 and sys.argv[1]=='--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]),encoding='utf-8');return 0
    compiler=next((path for name in ('clang++','g++','c++') if (path:=shutil.which(name))),None)
    if not compiler:raise RuntimeError('C++17 compiler required')
    failures=0
    with tempfile.TemporaryDirectory(prefix='save-bindings-',dir=ROOT/'.tmp') as directory:
        for mode in ('game','mpgame'):
            cpp=Path(directory)/(mode+'.cpp');exe=cpp.with_suffix('.exe')
            cpp.write_text(source(mode),encoding='utf-8')
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
            print(mode,flush=True)
            failures+=subprocess.run([str(exe),*(['repro'] if '--repro' in sys.argv else [])]).returncode!=0
    return int(failures!=0)


if __name__=='__main__':raise SystemExit(main())
