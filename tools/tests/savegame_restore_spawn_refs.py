#!/usr/bin/env python3
"""Compile saved spawn-reference readers against checked entity tables.

The production weak-pointer methods decide generation validity and restoration;
the fixture supplies entity types and bounded tables, without downcasting a
rejected reference or changing the normal stale-reference contract.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <array>
#include <cstdio>
#include <cstdarg>
#include <limits>
#include <stdexcept>
#include <string>
#define ID_INLINE inline
constexpr int GENTITYNUM_BITS=3,CENTITYNUM_BITS=3;
struct idTypeInfo { const char* classname; unsigned mask; };
struct idEntity {
    virtual ~idEntity()=default;
    static const idTypeInfo& GetClassType() { static idTypeInfo type{"idEntity",1}; return type; }
    virtual const idTypeInfo& GetType() const { return GetClassType(); }
    bool IsType(const idTypeInfo& type) const { return (GetType().mask&type.mask)==type.mask; }
    const char* GetClassname() const { return GetType().classname; }
};
struct idActor : idEntity {
    static const idTypeInfo& GetClassType() { static idTypeInfo type{"idActor",3}; return type; }
    const idTypeInfo& GetType() const override { return GetClassType(); }
};
struct idPlayer : idActor {
    static const idTypeInfo& GetClassType() { static idTypeInfo type{"idPlayer",7}; return type; }
    const idTypeInfo& GetType() const override { return GetClassType(); }
};
struct rvClientEntity : idEntity {
    static const idTypeInfo& GetClassType() { static idTypeInfo type{"rvClientEntity",9}; return type; }
    const idTypeInfo& GetType() const override { return GetClassType(); }
};
struct rvClientModel : rvClientEntity {
    static const idTypeInfo& GetClassType() { static idTypeInfo type{"rvClientModel",25}; return type; }
    const idTypeInfo& GetType() const override { return GetClassType(); }
};
template<class T> struct Table {
    std::array<T,8> values{};
    T& operator[](int index) { return values.at(index); }
};
struct GameLocal {
    Table<int> spawnIds,clientSpawnIds;
    Table<idEntity*> entities;
    Table<rvClientEntity*> clientEntities;
} gameLocal;
struct idRestoreGame {
    int value; bool fail=false;
    void ReadInt(int& output) { output=value; if(fail)throw std::logic_error("short read after write"); }
    [[noreturn]] void Error(const char* format,...) {
        char message[256]; va_list args; va_start(args,format);
        std::vsnprintf(message,sizeof(message),format,args); va_end(args);
        throw std::runtime_error(message);
    }
};
template<class type> struct idEntityPtr {
    int spawnId=0;
    void Restore(idRestoreGame*);
    bool IsValid() const;
    int GetEntityNum() const;
    type* GetEntity() const;
};
template<class type> struct rvClientEntityPtr {
    int spawnId=0;
    void Restore(idRestoreGame*);
    bool IsValid() const;
    int GetEntityNum() const;
    type* GetEntity() const;
};
static int assertions=0;
static void require(bool condition,const char* message) {
    ++assertions;
    if(!condition)throw std::runtime_error(message);
}
'''
MAIN = r'''
template<class Pointer> static void check(int id,bool rejected,const char* expectedType) {
    Pointer pointer; pointer.spawnId=17;
    idRestoreGame input{id};
    bool failed=false;
    try { pointer.Restore(&input); }
    catch(const std::runtime_error& error) {
        failed=true;
        require(std::string(error.what()).find(expectedType)!=std::string::npos,"wrong type diagnostic");
    }
    require(failed==rejected,"wrong-type saved spawn reference was accepted");
    require(pointer.spawnId==(rejected?17:id),"restore changed reference before validation");
    input.fail=true; pointer.spawnId=17;
    bool interrupted=false;
    try { pointer.Restore(&input); } catch(const std::logic_error&) { interrupted=true; }
    require(interrupted && pointer.spawnId==17,"short read changed live spawn reference");
}
int main(int argc,char** argv) {
    try {
        gameLocal.spawnIds.values.fill(-1); gameLocal.clientSpawnIds.values.fill(-1);
        idEntity plain;
        idActor actor;
        idPlayer player;
        rvClientEntity client;
        rvClientModel model;
        gameLocal.entities[1]=&plain; gameLocal.entities[2]=&actor; gameLocal.entities[3]=&player;
        gameLocal.clientEntities[1]=&client; gameLocal.clientEntities[2]=&model;
        for(int slot=1;slot<=3;++slot)gameLocal.spawnIds[slot]=73;
        for(int slot=1;slot<=2;++slot)gameLocal.clientSpawnIds[slot]=73;
        const int plainId=PackEntitySpawnId(73,1),actorId=PackEntitySpawnId(73,2),playerId=PackEntitySpawnId(73,3);
        const int clientId=PackClientEntitySpawnId(73,1),modelId=PackClientEntitySpawnId(73,2);
        if(argc>1) {
            const std::string scenario=argv[1];
            if(scenario=="entity")check<idEntityPtr<idActor>>(plainId,true,"idActor");
            else if(scenario=="client")check<rvClientEntityPtr<rvClientModel>>(clientId,true,"rvClientModel");
            else if(scenario=="entity-interrupt")check<idEntityPtr<idEntity>>(plainId,false,"");
            else if(scenario=="client-interrupt")check<rvClientEntityPtr<rvClientModel>>(modelId,false,"");
            else throw std::logic_error("unknown scenario");
            return 0;
        }
        check<idEntityPtr<idActor>>(plainId,true,"idActor");
        check<idEntityPtr<idPlayer>>(actorId,true,"idPlayer");
        check<rvClientEntityPtr<rvClientModel>>(clientId,true,"rvClientModel");
        for(int id : {plainId,actorId,playerId})check<idEntityPtr<idEntity>>(id,false,"");
        check<idEntityPtr<idActor>>(actorId,false,"");
        check<idEntityPtr<idActor>>(playerId,false,"");
        check<idEntityPtr<const idActor>>(playerId,false,"");
        check<rvClientEntityPtr<rvClientModel>>(modelId,false,"");
        check<rvClientEntityPtr<rvClientEntity>>(modelId,false,"");
        // Stale generations and empty slots stay representable, including
        // negative stored values. They must resolve to null without a downcast.
        for(int id : {0,PackEntitySpawnId(72,1),PackEntitySpawnId(73,7),
                      std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),-1}) {
            check<idEntityPtr<idPlayer>>(id,false,"");
            check<rvClientEntityPtr<rvClientModel>>(id,false,"");
            idRestoreGame input{id}; idEntityPtr<idPlayer> entity; rvClientEntityPtr<rvClientModel> effect;
            entity.Restore(&input); effect.Restore(&input);
            require(entity.GetEntity()==nullptr && effect.GetEntity()==nullptr,"stale reference resolved");
        }
        // Client references retain their existing unsigned generation behavior
        // even when the packed ID has the sign bit set.
        const int highGeneration=1<<(31-CENTITYNUM_BITS);
        gameLocal.clientSpawnIds[1]=gameLocal.clientSpawnIds[2]=highGeneration;
        check<rvClientEntityPtr<rvClientModel>>(PackClientEntitySpawnId(highGeneration,1),true,"rvClientModel");
        check<rvClientEntityPtr<rvClientModel>>(PackClientEntitySpawnId(highGeneration,2),false,"");
        std::printf("saved spawn references: %d checks passed\n",assertions);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"saved spawn references: %s\n",error.what()); return 1;
    }
}
'''


def source(mode):
    local = (ROOT/'src'/mode/'Game_local.h').read_text()
    client = (ROOT/'src'/mode/'client/ClientEntity.h').read_text()
    code = SUPPORT
    for name in ('PackEntitySpawnId', 'UnpackEntitySpawnId', 'PackClientEntitySpawnId'):
        code += '\n'+function_body(local, 'ID_INLINE int '+name+'(')
    for text, pointer in ((local, 'idEntityPtr'), (client, 'rvClientEntityPtr')):
        for result, method in (('void', 'Restore'), ('bool', 'IsValid'), ('int', 'GetEntityNum'), ('type *', 'GetEntity')):
            signature = 'ID_INLINE '+result+('' if result.endswith('*') else ' ')+pointer+'<type>::'+method+'('
            code += '\ntemplate<class type>\n'+function_body(text, signature)
    return code+MAIN


def main():
    if len(sys.argv)==4 and sys.argv[1]=='--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p:=shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    failures=0
    mutation='--mutation-check' in sys.argv
    with tempfile.TemporaryDirectory(prefix='save-spawn-refs-', dir=ROOT/'.tmp') as directory:
        for mode in ('game',):
            cpp=Path(directory)/(mode+'.cpp'); executable=cpp.with_suffix('.exe')
            code=source(mode)
            if mutation:
                for pointer in ('idEntityPtr','rvClientEntityPtr'):
                    signature='ID_INLINE void '+pointer+'<type>::Restore('
                    body=function_body(code,signature)
                    code=code.replace(body,body[:body.index('{')+1]+'\n savefile->ReadInt(spawnId);\n}')
            cpp.write_text(code,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(executable)],check=True)
            print(mode,flush=True)
            if mutation:
                for scenario in ('entity','client','entity-interrupt','client-interrupt'):
                    result=subprocess.run([str(executable),scenario]).returncode
                    failures+=result!=1
                    if result==1:print(mode+': rejected unchecked '+scenario+' reader',flush=True)
            else:
                failures+=subprocess.run([str(executable)]).returncode!=0
    return int(failures!=0)


if __name__=='__main__':raise SystemExit(main())
