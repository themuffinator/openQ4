#!/usr/bin/env python3
"""Execute production animation/IK readers with interrupted and foreign owners."""
from pathlib import Path
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
#include <vector>
#include <memory>
#include <algorithm>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
using idStr=std::string;
constexpr int ANIMCHANNEL_ALL=0, ANIM_NumAnimChannels=5;
struct idAnimator {
    bool modelDef=true, model=true, animation=true;
    void* ModelDef() { return modelDef?this:nullptr; }
    void* ModelHandle() { return model?this:nullptr; }
    int GetAnim(const idStr& name) { return animation && !name.empty()?1:0; }
};
struct idEntity {
    bool alive=true;
    idAnimator* animator=nullptr;
    idAnimator* GetAnimator() { assert(alive); return animator; }
};
struct idRestoreGame {
    int steps=0, failAt=-1, channel=1, booleans=0, integers=0, objects=0;
    bool ik=false, initialized=true, active=true;
    idEntity* first=nullptr;
    idEntity* second=nullptr;
    idStr animation="ik_pose";
    void Tick() { if(steps++==failAt) throw std::runtime_error("interrupted read"); }
    void ReadBool(bool& value) { value=ik?(booleans++==0?initialized:active):false; Tick(); }
    void ReadInt(int& value) { value=integers++==2?channel:0; Tick(); }
    void ReadObject(idEntity*& value) { value=objects++==0?first:second; Tick(); }
    void ReadString(idStr& value) { value=animation; Tick(); }
    void ReadVec3(int& value) { value=0; Tick(); }
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("invalid data"); }
};
struct StateThread {
    idEntity* owner=nullptr;
    void Restore(idRestoreGame* input,idEntity* value) {
        owner=value; input->Tick(); input->Tick();
    }
};
struct idAnimState {
    bool idleAnim=true, disabled=true;
    int animBlendFrames=0, lastAnimBlendFrames=0, channel=0;
    idEntity* self=nullptr;
    idAnimator* animator=nullptr;
    StateThread stateThread;
    void Restore(idRestoreGame*,idEntity*);
};
struct idIK {
    bool initialized=false, ik_activate=false;
    idEntity* self=nullptr;
    idAnimator* animator=nullptr;
    int modifiedAnim=0, modelOffset=0;
    void Restore(idRestoreGame*,idEntity*);
};
'''
STATE_SUPPORT = r'''
namespace StateFixture {
using idStr=std::string;
constexpr int OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION=3, MAX_SAVEGAME_STATE_CALLS=16;
constexpr int SFLAG_ONCLEAR=1, SFLAG_ONCLEARONLY=2;
int BIT(int value) { return 1<<value; }
enum stateResult_t { SRESULT_DONE=0 };
int live=0;
struct idRestoreGame {
    int steps=0, failAt=-1, integers=0, strings=0, unknownAt=-1, version=5, count=2;
    void Tick() { if(steps++==failAt) throw std::runtime_error("interrupted field"); }
    void ReadString(idStr& value) { value=strings++==unknownAt?"missing":"valid"; Tick(); }
    void ReadInt(int& value) {
        int index=integers++;
        int first=version>=3?2:1;
        value=index==first || index==first+1+5*count?count:0;
        Tick();
    }
    void Read(void*,size_t) { Tick(); }
    int GetOpenQ4SaveGameCompatibilityVersion() { return version; }
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("invalid state"); }
};
struct stateParms_t {
    int blendFrames=0,time=0,stage=0;
    void Restore(idRestoreGame*);
};
struct idClass {
    const char* FindState(const idStr& name) const { return name=="missing"?nullptr:"valid"; }
    const char* GetClassname() const { return "fixture owner"; }
    void ProcessState(const char*,const stateParms_t&) { assert(false); }
};
struct stateCall_t;
struct StateList {
    std::vector<stateCall_t*> data;
    stateCall_t* Next() { return data.empty()?nullptr:data.front(); }
    void Clear() { assert(data.empty()); }
};
struct Node {
    StateList* list=nullptr;
    stateCall_t* owner=nullptr;
    void SetOwner(stateCall_t* value) { owner=value; }
    void AddToEnd(StateList& value) { assert(owner); list=&value; list->data.push_back(owner); }
    void Remove() {
        assert(list);
        list->data.erase(std::find(list->data.begin(),list->data.end(),owner)); list=nullptr;
    }
};
struct stateCall_t {
    Node node;
    const char* state=nullptr;
    int flags=0,delay=0;
    stateParms_t parms;
    stateCall_t() { ++live; }
    ~stateCall_t() { assert(!node.list); --live; }
    void Restore(idRestoreGame*,const idClass*);
};
struct rvStateThread {
    idStr name;
    idClass* owner=nullptr;
    stateCall_t* insertAfter=nullptr;
    stateResult_t lastResult=SRESULT_DONE;
    struct { bool stateCleared=false,stateInterrupted=false,executing=false; } fl;
    StateList states,interrupted;
    void Restore(idRestoreGame*,idClass*);
    void Clear(bool);
    ~rvStateThread() { Clear(true); }
};
'''
STATE_MAIN = r'''
void check() {
    for(int version : {2,5}) for(int count : {0,1,2,3}) for(int unknown=-1; unknown<=2*count; ++unknown) {
        int boundaries=0;
        for(int failure=-1; failure<boundaries; ++failure) {
            idRestoreGame input; input.failAt=failure; input.version=version;
            input.unknownAt=unknown; input.count=count;
            idClass owner;
            bool refused=false;
            {
                rvStateThread thread;
                try { thread.Restore(&input,&owner); }
                catch(const std::runtime_error&) { refused=true; }
                if(failure==-1) boundaries=input.steps;
                assert(refused==(failure>=0 || unknown>0));
                if(!refused) assert(thread.states.data.size()==size_t(count) && thread.interrupted.data.size()==size_t(count));
                for(auto* call:thread.states.data) assert(call->state);
                for(auto* call:thread.interrupted.data) assert(call->state);
            }
            assert(live==0);
        }
    }
    std::puts("state restore: every interrupted allocation and unknown callback released");
}
} // namespace StateFixture
'''
MAIN = r'''
void animation() {
    for(int first=0; first<3; ++first) for(int second=0; second<3; ++second)
    for(int channel : {-1,0,1,4,5}) for(bool hasAnimator : {false,true}) {
        idAnimator animator;
        idEntity owner,foreign; owner.animator=hasAnimator?&animator:nullptr;
        // No reader is allowed to dereference a foreign saved object.
        foreign.alive=false;
        idEntity* refs[]={&owner,nullptr,&foreign};
        bool valid=first==0 && second==0 && hasAnimator && channel>=0 && channel<5;
        int boundaries=0;
        for(int failure=-1; failure<boundaries; ++failure) {
            idRestoreGame input; input.first=refs[first]; input.second=refs[second];
            input.channel=channel; input.failAt=failure;
            idAnimState state;
            bool refused=false;
            try { state.Restore(&input,&owner); }
            catch(const std::runtime_error&) { refused=true; }
            if(failure==-1) boundaries=input.steps;
            assert(refused==(!valid || failure>=0));
            assert(state.self==&owner && state.animator==owner.animator);
            assert(!state.stateThread.owner || state.stateThread.owner==&owner);
            if(input.steps>7) assert(state.stateThread.owner==&owner);
        }
    }
    idRestoreGame input; idAnimState state;
    bool refused=false;
    try { state.Restore(&input,nullptr); }
    catch(const std::runtime_error&) { refused=true; }
    assert(refused && input.steps==0);
}
void ik() {
    for(int saved=0; saved<3; ++saved) for(int resource=0; resource<6; ++resource)
    for(bool initialized : {false,true}) for(bool active : {false,true}) {
        idAnimator animator;
        animator.modelDef=resource!=2; animator.model=resource!=3; animator.animation=resource!=4;
        idEntity owner,foreign; owner.animator=resource==1?nullptr:&animator; foreign.alive=false;
        idEntity* refs[]={&owner,nullptr,&foreign};
        bool valid=saved!=2 && (!(initialized || active) || (saved==0 && resource==0));
        int boundaries=0;
        for(int failure=-1; failure<boundaries; ++failure) {
            idRestoreGame input; input.ik=true; input.first=refs[saved]; input.failAt=failure;
            input.initialized=initialized; input.active=active;
            if(resource==5) input.animation="";
            idIK state;
            bool refused=false;
            try { state.Restore(&input,&owner); }
            catch(const std::runtime_error&) { refused=true; }
            if(failure==-1) boundaries=input.steps;
            assert(refused==(!valid || failure>=0));
            if(refused) {
                assert(!state.self && !state.animator && !state.initialized && !state.ik_activate);
            } else {
                assert(state.self==(saved==0?&owner:nullptr));
                assert(state.animator==(saved==0?owner.animator:nullptr));
                assert(state.initialized==initialized && state.ik_activate==active);
            }
        }
    }
    idRestoreGame input; input.ik=true; idIK state;
    bool refused=false;
    try { state.Restore(&input,nullptr); }
    catch(const std::runtime_error&) { refused=true; }
    assert(refused && !state.self && !state.initialized && !state.ik_activate);
}
int main() {
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    animation(); ik(); StateFixture::check();
    std::puts("animation restore: owner, resource and every read-failure boundary passed");
}
'''


def source(mode):
    tree = ROOT / 'src' / mode
    actor = (tree / 'Actor.cpp').read_text()
    ik = (tree / 'IK.cpp').read_text()
    for name in ('headAnim', 'torsoAnim', 'legsAnim', 'walkIK'):
        assert f'{name}.Restore( savefile, this );' in actor
    for kind in ('Walk', 'Reach'):
        assert 'idIK::Restore( savefile, owner );' in function_body(ik, f'void idIK_{kind}::Restore(')
    state = (tree / 'gamesys/State.cpp').read_text()
    code = SUPPORT + function_body(actor, 'void idAnimState::Restore(') + '\n' + \
        function_body(ik, 'void idIK::Restore(') + STATE_SUPPORT
    for signature in ('void stateParms_t::Restore(', 'void stateCall_t::Restore(',
                      'void rvStateThread::Restore(', 'void rvStateThread::Clear ('):
        code += function_body(state, signature) + '\n'
    return code + STATE_MAIN + MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='restore-animation-', dir=ROOT / '.tmp') as directory:
        scratch = Path(directory)
        for mode in ('game', 'mpgame'):
            code = source(mode)
            cases = {'production': code}
            if '--mutation-check' in sys.argv:
                for label, before, after in (
                    ('animation-owner', 'savedSelf != owner || savedAnimatorOwner != owner', 'false'),
                    ('animation-animator', 'if ( animator == NULL )', 'if ( false )'),
                    ('animation-channel', 'channel < ANIMCHANNEL_ALL || channel >= ANIM_NumAnimChannels', 'false'),
                    ('animation-callback', 'stateThread.Restore( savefile, self );', 'stateThread.Restore( savefile, NULL );'),
                    ('ik-owner', 'owner == NULL || ( savedSelf != NULL && savedSelf != owner )', 'false'),
                    ('ik-resource', '( restoredInitialized || restoredActive ) &&', 'false &&'),
                    ('ik-early-publication', 'ReadBool( restoredActive );', 'ReadBool( restoredActive );\n initialized = restoredInitialized;\n ik_activate = restoredActive;'),
                ):
                    assert before in code
                    cases[label] = code.replace(before, after, 1)
                before = 'call->Restore( saveFile, owner );'
                after = 'stateCall_t* leaked = call.release(); leaked->Restore( saveFile, owner ); call.reset( leaked );'
                assert code.count(before) == 2
                cases['lost-active-state'] = code.replace(before, after, 1)
                prefix, suffix = code.rsplit(before, 1)
                cases['lost-interrupted-state'] = prefix + after + suffix
            for name, candidate in cases.items():
                cpp, exe = scratch / f'{mode}.cpp', scratch / f'{mode}.exe'
                cpp.write_text(candidate, encoding='utf-8')
                subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
                result = subprocess.run([str(exe)], capture_output=name != 'production', timeout=30)
                if name == 'production':
                    result.check_returncode()
                else:
                    assert result.returncode != 0, f'{mode}: {name} escaped detection'
                    print(f'{mode}: rejected {name}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
