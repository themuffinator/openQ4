#!/usr/bin/env python3
"""Run production articulated-figure readers with mismatched and interrupted data."""
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
#include <cctype>
#include <stdexcept>
#include <string>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
struct idStr : std::string {
    using std::string::string;
    using std::string::operator=;
    int Icmp(const idStr& other) const {
        std::string a=*this,b=other;
        for(auto& c:a) c=char(std::tolower(static_cast<unsigned char>(c)));
        for(auto& c:b) c=char(std::tolower(static_cast<unsigned char>(c)));
        return a.compare(b);
    }
};
struct idAnimator {
    int clears=0;
    void ClearAllAnims(int,int) { ++clears; }
    void ClearAllJoints() { ++clears; }
};
struct Physics;
struct idEntity {
    idAnimator* animator=nullptr;
    int switches=0;
    idAnimator* GetAnimator() { return animator; }
    void RestorePhysics(Physics*) { ++switches; }
};
struct Physics {
    idEntity* self=nullptr;
    int enabled=0;
    void SetSelf(idEntity* value) { self=value; }
    void EnableClip() { ++enabled; }
};
struct { int time=10; } gameLocal;
constexpr int DECL_AF=1,DS_DEFAULTED=2;
struct idDeclAF { int state=0; int GetState() const { return state; } } fixtureDecl;
struct Decls {
    bool present=true;
    const idDeclAF* FindType(int,const idStr&,bool makeDefault) {
        assert(!makeDefault); return present?&fixtureDecl:nullptr;
    }
} fixtureDecls;
Decls* declManager=&fixtureDecls;
struct idRestoreGame {
    int steps=0, failAt=-1, booleans=0, integers=0;
    bool loaded=true,active=true,bind=false;
    idEntity* owner=nullptr;
    idStr savedName="figure";
    int savedType=1,boxIndex=-1;
    void Tick() { if(steps++==failAt) throw std::runtime_error("interrupted field"); }
    void ReadObject(idEntity*& value) { value=owner; Tick(); }
    void ReadString(idStr& value) { value=savedName; Tick(); }
    void ReadBool(bool& value) { int n=booleans++; value=n==0?bind:n==1?loaded:active; Tick(); }
    void ReadInt(int& value) { int n=integers++; value=n==0?savedType:n<=6?boxIndex:0; Tick(); }
    void ReadFloat(float& value) { value=0; Tick(); }
    void ReadVec3(int& value) { value=0; Tick(); }
    void ReadVec6(int& value) { value=0; Tick(); }
    void ReadMat3(int& value) { value=0; Tick(); }
    void ReadPhysicsObject(Physics& value,idEntity* entity) {
        value.SetSelf(entity); Tick(); Tick(); Tick();
    }
    [[noreturn]] void Error(const char*,...) { throw std::runtime_error("invalid figure"); }
};
template<size_t N> void SaveCodec_ReadFlagBytes(idRestoreGame& input,bool (&bits)[N]) {
    for(auto& bit:bits) bit=false;
    input.Tick();
}
struct idAF {
    idEntity* self=nullptr;
    idAnimator* animator=nullptr;
    idStr name;
    int baseOrigin=0,baseAxis=0,poseTime=0,restStartTime=0,modifiedAnim=0;
    bool hasBindConstraints=false,isLoaded=false,isActive=false,loadWorks=true;
    int binds=0,updates=0,loads=0;
    Physics physicsObj;
    void SetAnimator(idAnimator* value) { animator=value; }
    bool Load(idEntity*,const idStr&) {
        ++loads;
        if(!loadWorks || !animator) return false;
        isLoaded=true; return true;
    }
    void AddBindConstraints() { assert(isLoaded); ++binds; }
    void UpdateAnimation() { ++updates; }
    void Restore(idRestoreGame*,idEntity*);
};
enum constraintType_t { CONSTRAINT_INVALID, CONSTRAINT_FIXED, CONSTRAINT_HINGE };
struct idAFConstraint {
    constraintType_t type=CONSTRAINT_FIXED;
    idStr name="joint";
    int boxIndex[6]={-1,-1,-1,-1,-1,-1},firstIndex=0;
    struct { bool allowPrimary=false,frameConstraint=false,noCollision=false,isPrimary=false,isZero=false,transient=false; } fl;
    void Restore(idRestoreGame*);
};
struct idAFBody {
    idStr name="body";
    float linearFriction=0,angularFriction=0,contactFriction=0,bouncyness=0;
    int clipMask=0,frictionDir=0,contactMotorDir=0;
    float contactMotorVelocity=0,contactMotorForce=0,mass=0,invMass=0;
    int centerOfMass=0,inertiaTensor=0,inverseInertiaTensor=0;
    struct State { int worldOrigin=0,worldAxis=0,spatialVelocity=0,externalForce=0; } state;
    State* current=&state;
    int atRestOrigin=0,atRestAxis=0,numResponses=0,maxAuxiliaryIndex=0,maxSubTreeAuxiliaryIndex=0;
    struct { bool clipMaskSet=false,selfCollision=false,spatialInertiaSparse=false,useFrictionDir=false,useContactMotorDir=false,isZero=false; } fl;
    void Restore(idRestoreGame*);
};
'''
MAIN = r'''
void figures() {
    int boundaries=0;
    for(int fail=-1;fail<boundaries;++fail) {
        idAnimator animator; idEntity owner; owner.animator=&animator;
        idRestoreGame input; input.owner=&owner; input.failAt=fail;
        idAF figure; bool refused=false;
        try { figure.Restore(&input,&owner); } catch(const std::runtime_error&) { refused=true; }
        if(fail==-1) boundaries=input.steps;
        assert(refused==(fail>=0));
        assert(figure.self==nullptr || figure.self==&owner);
        assert(figure.physicsObj.self==nullptr || figure.physicsObj.self==&owner);
        assert(owner.switches==(fail==-1?1:0));
        assert(animator.clears==(fail==-1?2:0));
        assert(figure.physicsObj.enabled==(fail==-1?1:0));
    }
    for(int mode=0;mode<12;++mode) {
        idAnimator animator; idEntity owner,foreign; owner.animator=&animator;
        idRestoreGame input; input.owner=&owner;
        idAF figure;
        if(mode==0) input.owner=&foreign;
        if(mode==1) input.owner=nullptr;
        if(mode==2) input.loaded=false;
        if(mode==3) { input.loaded=false; input.active=false; input.bind=true; }
        if(mode==4) figure.loadWorks=false;
        if(mode==5) owner.animator=nullptr;
        if(mode==6) { input.owner=nullptr; input.loaded=false; input.active=false; }
        if(mode==7) { input.loaded=false; input.active=false; figure.loadWorks=false; }
        if(mode==8) input.active=false;
        if(mode==10) fixtureDecls.present=false;
        if(mode==11) fixtureDecl.state=DS_DEFAULTED;
        bool refused=false;
        try { figure.Restore(&input,mode==9?nullptr:&owner); }
        catch(const std::runtime_error&) { refused=true; }
        assert(refused==(mode<6 || mode>=9));
        if(mode>=10) assert(figure.loads==0);
        assert(owner.switches==0 && foreign.switches==0);
        if(mode==6 || mode==7) assert(!figure.isLoaded && !figure.isActive);
        if(mode==8) assert(figure.isLoaded && !figure.isActive);
        fixtureDecls.present=true; fixtureDecl.state=0;
    }
}
template<class Record> void records(const char* name) {
    int boundaries=0;
    for(int fail=-1;fail<boundaries;++fail) {
        Record record;
        idRestoreGame input; input.savedName=name; input.failAt=fail;
        bool refused=false;
        try { record.Restore(&input); } catch(const std::runtime_error&) { refused=true; }
        if(fail==-1) boundaries=input.steps;
        assert(refused==(fail>=0));
        assert(record.name.Icmp(name)==0);
    }
    Record record;
    idRestoreGame input; input.savedName="wrong";
    bool refused=false;
    try { record.Restore(&input); } catch(const std::runtime_error&) { refused=true; }
    assert(refused && record.name==name);
    input=idRestoreGame(); input.savedName=name;
    for(auto& c:input.savedName) c=char(std::toupper(static_cast<unsigned char>(c)));
    record.Restore(&input); // AF declaration lookup also ignores name case.
    assert(record.name==name);
}
int main() {
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    figures(); records<idAFConstraint>("joint"); records<idAFBody>("body");
    for(int type : {-1,0,2,2147483647}) {
        idAFConstraint constraint;
        idRestoreGame input; input.savedName="joint"; input.savedType=type;
        bool refused=false;
        try { constraint.Restore(&input); } catch(const std::runtime_error&) { refused=true; }
        assert(refused && input.steps==1 && constraint.type==CONSTRAINT_FIXED);
    }
    idAFConstraint constraint;
    idRestoreGame input; input.savedName="joint"; input.boxIndex=0;
    bool refused=false;
    try { constraint.Restore(&input); } catch(const std::runtime_error&) { refused=true; }
    assert(refused);
    std::puts("AF restore: owner, required resources, structure and interrupted fields passed");
}
'''


def source(mode):
    tree = ROOT/'src'/mode
    physics = (tree/'physics/Physics_AF.cpp').read_text()
    return SUPPORT + function_body((tree/'AF.cpp').read_text(), 'void idAF::Restore(') + '\n' + \
        function_body(physics, 'void idAFConstraint::Restore(') + '\n' + \
        function_body(physics, 'void idAFBody::Restore(') + MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='restore-af-', dir=ROOT/'.tmp') as directory:
        scratch = Path(directory)
        for mode in ('game','mpgame'):
            code = source(mode)
            cases = {'production':code}
            if '--mutation-check' in sys.argv:
                for label, before, after in (
                    ('owner', 'owner == NULL || ( savedSelf != NULL && savedSelf != owner )', 'false'),
                    ('state', '( ( isLoaded || isActive || hasBindConstraints ) && self == NULL )\n\t\t\t|| ( ( isActive || hasBindConstraints ) && !isLoaded )', 'false'),
                    ('resource', '!Load( self, name ) && ( isLoaded || isActive || hasBindConstraints )', '!Load( self, name ) && false'),
                    ('definition', 'savedDecl == NULL || savedDecl->GetState() == DS_DEFAULTED', 'false'),
                    ('constraint-type', 't != type', 'false'),
                    ('constraint-name', 'savedName.Icmp( name ) != 0', 'false'),
                    ('box-index', 'boxIndex[i] != -1', 'false'),
                ):
                    assert before in code
                    cases[label] = code.replace(before,after,1)
                before = 'savedName.Icmp( name ) != 0'
                assert code.count(before)==2
                prefix,suffix=code.rsplit(before,1)
                cases['body-name']=prefix+'false'+suffix
            for name,candidate in cases.items():
                cpp,exe=scratch/f'{mode}.cpp',scratch/f'{mode}.exe'
                cpp.write_text(candidate,encoding='utf-8')
                subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
                result=subprocess.run([str(exe)],capture_output=name!='production',timeout=30)
                if name=='production':
                    result.check_returncode()
                else:
                    assert result.returncode!=0, f'{mode}: {name} escaped detection'
                    print(f'{mode}: rejected {name}',flush=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
