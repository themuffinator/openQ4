#!/usr/bin/env python3
"""Compile the actual SP/MP registration methods against checked table storage."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <stdexcept>
constexpr int MAX_CENTITIES=8, CENTITYNUM_BITS=3, INITIAL_SPAWN_COUNT=1, EV_Remove=0;
template<class T> struct CheckedTable {
    std::array<T,MAX_CENTITIES> entries{};
    T& operator[](int index) {
        if(index<0 || index>=MAX_CENTITIES) throw std::runtime_error("client registration accessed beyond table capacity");
        return entries[index];
    }
};
struct Args { int value=0; void TransferKeyValues(Args& source) { value=source.value; source.value=0; } };
struct Node { int added=0,removed=0; void AddToEnd(Node&) { ++added; } void Remove() { ++removed; } };
struct rvClientEntity {
    int entityNumber=-1, removals=0;
    Node spawnNode,bindNode; Args spawnArgs;
    void PostEventMS(const int*,int) { ++removals; }
};
struct idGameLocal {
    CheckedTable<rvClientEntity*> clientEntities;
    CheckedTable<int> clientSpawnIds;
    int firstFreeClientIndex=0,clientSpawnCount=1,num_clientEntities=0,warnings=0;
    Node clientSpawnedEntities; Args spawnArgs;
    void Warning(const char*,...) { ++warnings; }
    void RegisterClientEntity(rvClientEntity*);
    void UnregisterClientEntity(rvClientEntity*);
};
'''
MAIN=r'''
int main() {
    try {
        for(int cycle=0;cycle<1000;++cycle) {
            idGameLocal game;
            std::array<rvClientEntity,MAX_CENTITIES> entities;
            for(int i=0;i<MAX_CENTITIES;++i) {
                game.spawnArgs.value=100+i;
                game.RegisterClientEntity(&entities[i]);
                assert(entities[i].entityNumber==i && game.clientEntities[i]==&entities[i]);
                assert(entities[i].spawnArgs.value==100+i && entities[i].spawnNode.added==1);
            }
            rvClientEntity overflow;
            game.RegisterClientEntity(&overflow);
            assert(overflow.entityNumber==-1 && overflow.removals==1 && game.warnings==1);
            assert(game.firstFreeClientIndex==MAX_CENTITIES && game.num_clientEntities==MAX_CENTITIES);
            game.firstFreeClientIndex=0;
            game.RegisterClientEntity(&overflow);
            assert(overflow.removals==2 && game.warnings==2);
            game.UnregisterClientEntity(&entities[3]);
            assert(entities[3].entityNumber==-1 && game.firstFreeClientIndex==3 && game.clientEntities[3]==nullptr);
            assert(game.clientSpawnIds[3]==-1);
            rvClientEntity replacement;
            game.RegisterClientEntity(&replacement);
            assert(replacement.entityNumber==3 && game.clientEntities[3]==&replacement && replacement.removals==0);
            game.UnregisterClientEntity(&overflow);
            assert(overflow.spawnNode.removed==0);
        }
        std::puts("client registration: full-table hint/scan refusal, valid registration and freed-slot reuse passed (1000 cycles)");
    } catch(const std::exception& error) {
        std::fprintf(stderr,"%s\n",error.what()); return 1;
    }
}
'''

def source(mode):
    text=(ROOT/'src'/mode/'Game_local.cpp').read_text()
    return SUPPORT+'\n'.join(function_body(text,'void idGameLocal::'+name+'(') for name in ('RegisterClientEntity','UnregisterClientEntity'))+'\n'+MAIN

def refusal_contract(mode):
    """A refused registration (full table) must not end the level.

    The refusal skips the spawn-arg transfer, so a refused moveable has no model;
    its Spawn used to raise "cannot load collision model" and drop the map. It
    now returns before reading spawn args, and SpawnClientEntityDef reports the
    refusal before handing the half-spawned object to its caller.
    """
    moveable=function_body((ROOT/'src'/mode/'client'/'ClientMoveable.cpp').read_text(encoding='latin-1'),
                           'void rvClientMoveable::Spawn ( void ) {')
    guard=moveable.find('if ( entityNumber == -1 ) {')
    if guard<0 or not guard<moveable.find('ParseSpawnArgsToRenderEntity')<moveable.find('gameLocal.Error'):
        raise AssertionError(f'{mode}: rvClientMoveable::Spawn must return for a refused registration before using spawn args')
    spawn=function_body((ROOT/'src'/mode/'Game_local.cpp').read_text(encoding='latin-1'),
                        'bool idGameLocal::SpawnClientEntityDef(')
    called=spawn.find('obj->CallSpawn();')
    refused=spawn.find('->entityNumber == -1', called)
    handed=spawn.find('*cent = static_cast<rvClientEntity*>(obj);', called)
    if not 0<=called<refused<handed or 'return false;' not in spawn[refused:handed]:
        raise AssertionError(f'{mode}: SpawnClientEntityDef must report a refused client entity instead of returning it')

def main():
    if len(sys.argv)==4 and sys.argv[1]=='--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]),encoding='utf-8'); return 0
    for mode in ('game','mpgame'):
        refusal_contract(mode)
    print('client registration: refused spawns are dropped, not fatal (game, mpgame)')
    compiler=next((path for name in ('clang++','g++','c++') if (path:=shutil.which(name))),None)
    if not compiler: raise RuntimeError('C++17 compiler required')
    failures=0
    with tempfile.TemporaryDirectory(prefix='client-register-',dir=ROOT/'.tmp') as directory:
        for mode in ('game','mpgame'):
            cpp=Path(directory)/(mode+'.cpp'); executable=cpp.with_suffix('.exe')
            cpp.write_text(source(mode),encoding='utf-8')
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(executable)],check=True)
            failures+=subprocess.run([str(executable)]).returncode!=0
    return int(failures!=0)

if __name__=='__main__': raise SystemExit(main())
