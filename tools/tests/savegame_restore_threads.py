#!/usr/bin/env python3
"""Check restored thread ownership and failed-map cleanup with production methods.

The animated destructor's thread delete is observed before destruction, so a
second delete is diagnosed deterministically instead of invoking undefined C++.
Physics, combat models and interpreter execution are outside this fixture.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <string>
#include <vector>
struct idRestoreGame;
struct idClass { virtual ~idClass()=default; virtual bool IsType(int) const { return false; } };
struct idEntity : idClass {
    bool prepared=false;
    static int GetClassType() { return 1; }
    bool IsType(int type) const override { return type==1; }
    virtual void PrepareFailedRestoreShutdown() { prepared=true; }
};
using idAFEntity_Gibbable=idEntity;
struct idThread;
struct idInterpreter {
    idThread* thread=nullptr;
    bool terminateOnExit=false;
    void RestoreThread(idRestoreGame*);
};
struct idThread : idClass {
    bool alive=true,restoreOwnerClaimed=false;
    idInterpreter interpreter;
    std::string threadName="fixture";
    static int GetClassType() { return 2; }
    bool IsType(int type) const override { return type==2; }
    static void RestoreOwnedThread(idRestoreGame*,idThread*&);
    void ValidateRestoreOwnership(idRestoreGame*) const;
};
struct idAnimated : idAFEntity_Gibbable {
    int* combatModel=nullptr;
    idThread* scriptThread=nullptr;
    void Cleanup();
    ANIMATED_OVERRIDE
};
static void CheckedDeleteThread(idThread* thread) {
    if(!thread)return;
    if(!thread->alive)throw std::runtime_error("thread was already destroyed");
    thread->alive=false;
}
template<class T> struct idList {
    std::vector<T> entries;
    int Num() const { return int(entries.size()); }
    T operator[](int index) const { return entries.at(index); }
};
struct { idThread* frameCommandThread=nullptr; } gameLocal;
struct idRestoreGame {
    idList<idClass*> objects;
    idRestoreGame* previousRestore=nullptr;
    static thread_local idRestoreGame* activeRestore;
    static void PrepareMapShutdown();
    idClass* next=nullptr;
    bool interrupt=false;
    void ReadObject(idThread*& result) {
        result=nullptr;
        if(interrupt)throw std::logic_error("interrupted read");
        if(next && !next->IsType(idThread::GetClassType()))Error("wrong thread type");
        result=static_cast<idThread*>(next);
    }
    [[noreturn]] void Error(const char* format,...) {
        char message[256]; va_list args; va_start(args,format);
        std::vsnprintf(message,sizeof(message),format,args); va_end(args);
        throw std::runtime_error(message);
    }
};
thread_local idRestoreGame* idRestoreGame::activeRestore=nullptr;
static void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
'''
MAIN = r'''
static void validation() {
    for(int ownerKind=0;ownerKind<2;++ownerKind) {
        idThread thread,existing;
        idAnimated first,second;
        idRestoreGame restore; restore.next=&thread;
        idThread::RestoreOwnedThread(&restore,first.scriptThread);
        require(first.scriptThread==&thread && thread.restoreOwnerClaimed,"first owner not installed");
        idThread*& other=ownerKind?gameLocal.frameCommandThread:second.scriptThread;
        other=&existing;
        bool rejected=false;
        try { idThread::RestoreOwnedThread(&restore,other); }
        catch(const std::runtime_error& error) { rejected=std::string(error.what()).find("duplicate owning reference")!=std::string::npos; }
        require(rejected && other==&existing,"duplicate owner published");
        gameLocal.frameCommandThread=nullptr;
    }
    for(int failure=0;failure<2;++failure) {
        idThread existing; idEntity wrong;
        idThread* owner=&existing;
        idRestoreGame restore; restore.interrupt=failure==0; restore.next=&wrong;
        bool rejected=false;
        try { idThread::RestoreOwnedThread(&restore,owner); }
        catch(const std::exception&) { rejected=true; }
        require(rejected && owner==&existing && !existing.restoreOwnerClaimed,"failed read changed owner");
    }
    idRestoreGame restore;
    idThread* empty=nullptr;
    idThread::RestoreOwnedThread(&restore,empty);
    require(!empty,"null owner changed");
    for(int claimed=0;claimed<2;++claimed)for(int automatic=0;automatic<2;++automatic) {
        idThread thread; thread.restoreOwnerClaimed=claimed; thread.interpreter.terminateOnExit=automatic;
        bool rejected=false;
        try { thread.ValidateRestoreOwnership(&restore); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected==(claimed && automatic),"wrong owned-thread auto-delete decision");
    }
    for(int reference=0;reference<3;++reference) {
        idThread self,other; self.interpreter.thread=&self;
        restore.next=reference==0?&self:reference==1?&other:nullptr;
        bool rejected=false;
        try { self.interpreter.RestoreThread(&restore); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected==(reference!=0) && self.interpreter.thread==&self,"interpreter owner changed");
    }
    std::puts("thread reads: duplicate/interrupted/wrong-type/null owners, deletion policy and interpreter identity passed");
}
int main(int argc,char** argv) {
    const bool repro=argc>1 && std::string(argv[1])=="repro";
    int failures=0;
    for(int scenario=0;scenario<5;++scenario) {
        idAnimated first,second;
        idThread thread,outside;
        idRestoreGame restore;
        restore.objects.entries={nullptr,&first,&second,&thread};
        idRestoreGame::activeRestore=&restore;
        first.scriptThread=&thread;
        if(scenario==0)second.scriptThread=&thread;
        if(scenario==1)gameLocal.frameCommandThread=&thread;
        if(scenario==2) { restore.objects.entries={nullptr,&thread,&first,&second}; gameLocal.frameCommandThread=&thread; }
        if(scenario==4)gameLocal.frameCommandThread=&outside;
        try {
            if(!repro)idRestoreGame::PrepareMapShutdown();
            if(scenario==2)CheckedDeleteThread(&thread);
            first.Cleanup(); second.Cleanup();
            CheckedDeleteThread(gameLocal.frameCommandThread);
            if(thread.alive)CheckedDeleteThread(&thread); // registry visits only surviving objects
            if(!repro)require(first.prepared && second.prepared,"base preparation omitted");
            if(scenario==4)require(gameLocal.frameCommandThread==&outside && !outside.alive,"unrelated frame owner changed");
            require(!thread.alive,"thread leaked");
            std::printf("thread cleanup %d: passed\n",scenario);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"thread cleanup %d: %s\n",scenario,error.what()); ++failures;
        }
        idRestoreGame::activeRestore=nullptr;
        gameLocal.frameCommandThread=nullptr;
    }
    if(!repro)try { validation(); } catch(const std::exception& error) {
        std::fprintf(stderr,"thread validation: %s\n",error.what()); ++failures;
    }
    return failures?1:0;
}
'''


def source(mode):
    directory = ROOT/'src'/mode
    misc = (directory/'Misc.cpp').read_text()
    thread = (directory/'script/Script_Thread.cpp').read_text()
    saves = (directory/'gamesys/SaveGame.cpp').read_text()
    implemented = 'void idThread::RestoreOwnedThread(' in thread
    code = SUPPORT.replace('ANIMATED_OVERRIDE', 'void PrepareFailedRestoreShutdown() override;' if implemented else '')
    deletion = function_body(misc, 'idAnimated::~idAnimated()')
    assert deletion.count('delete scriptThread;') == 1
    code += deletion.replace('idAnimated::~idAnimated()', 'void idAnimated::Cleanup()').replace('delete scriptThread;', 'CheckedDeleteThread( scriptThread );')
    code += '\n'+function_body(saves, 'void idRestoreGame::PrepareMapShutdown(')
    if implemented:
        code += '\n'+function_body(misc, 'void idAnimated::PrepareFailedRestoreShutdown(')
        code += '\n'+function_body(thread, 'void idThread::RestoreOwnedThread(')
        code += '\n'+function_body(thread, 'void idThread::ValidateRestoreOwnership(')
        code += '\n'+function_body((directory/'script/Script_Interpreter.cpp').read_text(), 'void idInterpreter::RestoreThread(')
        assert 'RestoreThread( savefile );' in function_body((directory/'script/Script_Interpreter.cpp').read_text(), 'void idInterpreter::Restore(')
        assert 'restoreOwnerClaimed = false;' in function_body(thread, 'void idThread::Init(')
        assert 'idThread::RestoreOwnedThread( savefile, scriptThread );' in function_body(misc, 'void idAnimated::Restore(')
        assert 'idThread::RestoreOwnedThread( &savegame, frameCommandThread );' in function_body((directory/'Game_local.cpp').read_text(), 'bool idGameLocal::InitFromSaveGame(')
        restore = function_body(saves, 'void idRestoreGame::RestoreObjects(')
        assert restore.index('ReadSaveGameFooter();') < restore.index('ValidateRestoreOwnership( this )') < restore.index('ent->UpdateVisuals();')
    else:
        code += '\nvoid idThread::RestoreOwnedThread(idRestoreGame* savefile,idThread*& owner) { savefile->ReadObject(owner); }\n'
        code += 'void idThread::ValidateRestoreOwnership(idRestoreGame*) const {}\n'
        code += 'void idInterpreter::RestoreThread(idRestoreGame* savefile) { savefile->ReadObject(thread); }\n'
    return code+MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    failures = 0
    with tempfile.TemporaryDirectory(prefix='save-threads-', dir=ROOT/'.tmp') as directory:
        for mode in ('game',):
            cpp = Path(directory)/(mode+'.cpp')
            exe = cpp.with_suffix('.exe')
            code = source(mode)
            mutations = [('unchanged', None, None)]
            if '--mutation-check' in sys.argv:
                mutations = [
                    ('duplicate owner', 'if ( restoredThread->restoreOwnerClaimed )', 'if ( false )'),
                    ('self deletion', 'if ( restoreOwnerClaimed && interpreter.terminateOnExit )', 'if ( false )'),
                    ('interpreter identity', 'if ( restoredThread != thread )', 'if ( false )'),
                    ('animated detach', 'scriptThread = NULL;', ''),
                    ('frame detach', 'gameLocal.frameCommandThread = NULL;', ''),
                ]
            for label, old, new in mutations:
                if old:
                    assert code.count(old) == 1
                cpp.write_text(code.replace(old, new) if old else code, encoding='utf-8')
                subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
                print(mode, label, flush=True)
                result = subprocess.run([str(exe), *(['repro'] if '--repro' in sys.argv else [])]).returncode
                failures += (result == 0) if old else (result != 0)
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
