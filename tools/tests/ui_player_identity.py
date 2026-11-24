#!/usr/bin/env python3
"""Execute production player marks and retained dispatch against slot reuse.

Engine entities and connection generations are bounded stand-ins. This does
not qualify remote voice transport, kick/admin targets or persistent friends.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SIGNATURES = ('bool idPlayer::IsPlayerMuted( int', 'void idPlayer::MutePlayer( int',
              'bool idPlayer::IsFriend( int', 'void idPlayer::SetFriend( int')

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
constexpr int MAX_CLIENTS=32;
struct idStr : std::string {
    using std::string::string;
    idStr()=default;
    int Length()const{return int(size());}
    bool IsEmpty()const{return empty();}
    int Icmp(const char* other)const{return compare(other);}
};
const char* va(const char* format,unsigned int value){static char text[32];std::snprintf(text,sizeof(text),format,value);return text;}
struct idEntity {int entityNumber=0;bool player=true;bool IsType(int)const{return player;}};
struct idPlayer : idEntity {
    unsigned int mutedPlayers=0,friendPlayers=0;
    int mutedPlayerIdentities[MAX_CLIENTS]={},friendPlayerIdentities[MAX_CLIENTS]={};
    static int GetClassType(){return 1;}
    bool IsPlayerMuted(int)const;void MutePlayer(int,bool);
    bool IsFriend(int)const;void SetFriend(int,bool);
};
struct Game {
    idEntity* entities[MAX_CLIENTS]={};
    unsigned int generations[MAX_CLIENTS]={};
    int GetSpawnId(const idEntity* e)const{return int((generations[e->entityNumber]<<12)|unsigned(e->entityNumber));}
    idPlayer* GetLocalPlayer(){return static_cast<idPlayer*>(entities[0]);}
} gameLocal;
struct idCmdArgs {
    std::vector<std::string> values;
    int Argc()const{return int(values.size());}
    const char* Argv(int i)const{return values.at(i).c_str();}
};
struct idMultiplayerGame {
    bool listed[MAX_CLIENTS]={};int retainedStatClient=-1,mutes=0,friends=0;
    idStr retainedStatIdentity;
    idStr RetainedPlayerIdentity(int)const;
    bool RetainedListed(int i)const{return listed[i];}
    void ClientVoiceMute(int i,bool v){++mutes;gameLocal.GetLocalPlayer()->MutePlayer(i,v);}
    void ToggleFriend(int i){++friends;auto p=gameLocal.GetLocalPlayer();p->SetFriend(i,!p->IsFriend(i));}
    bool Handle(const char* verb,std::vector<std::string> operands){
        idStr sub(verb);idCmdArgs args{operands};int icmd=0;
        @DISPATCH@
        return false;
    }
};
@MARKS@
@IDENTITY@
int main(){
    idPlayer viewer,other,players[MAX_CLIENTS];idMultiplayerGame menu;
    for(int slot=0;slot<MAX_CLIENTS;++slot){
        players[slot].entityNumber=slot;gameLocal.entities[slot]=&players[slot];gameLocal.generations[slot]=2;
        menu.listed[slot]=true;
        assert(!viewer.IsFriend(slot) && !viewer.IsPlayerMuted(slot));
        viewer.SetFriend(slot,true);viewer.MutePlayer(slot,true);
        assert(viewer.IsFriend(slot) && viewer.IsPlayerMuted(slot));
        assert(!other.IsFriend(slot) && !other.IsPlayerMuted(slot));
        // Same connection and entity survives a death/respawn; marks remain.
        assert(viewer.IsFriend(slot) && viewer.IsPlayerMuted(slot));
        ++gameLocal.generations[slot]; // allocator may reuse the same pointer
        assert(!viewer.IsFriend(slot) && !viewer.IsPlayerMuted(slot));
        viewer.SetFriend(slot,true);viewer.MutePlayer(slot,true);
        assert(viewer.IsFriend(slot) && viewer.IsPlayerMuted(slot));
        gameLocal.entities[slot]=nullptr;
        assert(!viewer.IsFriend(slot) && !viewer.IsPlayerMuted(slot));
        viewer.SetFriend(slot,false);viewer.MutePlayer(slot,false);
        gameLocal.entities[slot]=&players[slot];
        assert(!viewer.IsFriend(slot) && !viewer.IsPlayerMuted(slot));
        players[slot].player=false;viewer.SetFriend(slot,true);viewer.MutePlayer(slot,true);
        assert(!viewer.IsFriend(slot) && !viewer.IsPlayerMuted(slot));players[slot].player=true;
    }
    for(int invalid : {-1,32,999}) {
        viewer.SetFriend(invalid,true);viewer.MutePlayer(invalid,true);
        assert(!viewer.IsFriend(invalid) && !viewer.IsPlayerMuted(invalid));
        assert(menu.RetainedPlayerIdentity(invalid).empty());
    }
    for(const auto* verb : {"select","mute","friend"}) {
        const auto original=menu.RetainedPlayerIdentity(31);
        const int startMutes=menu.mutes,startFriends=menu.friends;
        menu.Handle(verb,{"31",original});
        assert(!std::strcmp(verb,"select") ? menu.retainedStatClient==31 && menu.retainedStatIdentity==original :
               !std::strcmp(verb,"mute") ? menu.mutes==startMutes+1 : menu.friends==startFriends+1);
        ++gameLocal.generations[31];
        const int selected=menu.retainedStatClient,mutes=menu.mutes,friends=menu.friends;
        const auto selectedIdentity=menu.retainedStatIdentity;
        assert(!menu.Handle(verb,{"31",original}));
        assert(selected==menu.retainedStatClient && selectedIdentity==menu.retainedStatIdentity &&
               mutes==menu.mutes && friends==menu.friends);
        const auto current=menu.RetainedPlayerIdentity(31);
        menu.Handle(verb,{"31",current});
        assert(!std::strcmp(verb,"select") ? menu.retainedStatIdentity==current :
               !std::strcmp(verb,"mute") ? menu.mutes==mutes+1 : menu.friends==friends+1);
        const int validMutes=menu.mutes,validFriends=menu.friends;
        const auto validIdentity=menu.retainedStatIdentity;
        assert(!menu.Handle(verb,{"31"}) && !menu.Handle(verb,{"31",""}));
        assert(!menu.Handle(verb,{"-1",current}) && !menu.Handle(verb,{"32",current}));
        assert(!menu.Handle(verb,{"31",current+";quit"}));
        menu.listed[31]=false;assert(!menu.Handle(verb,{"31",current}));menu.listed[31]=true;
        gameLocal.entities[31]=nullptr;assert(!menu.Handle(verb,{"31",current}));gameLocal.entities[31]=&players[31];
        assert(menu.mutes==validMutes && menu.friends==validFriends && menu.retainedStatIdentity==validIdentity);
    }
    gameLocal.generations[31]=0xfffff;
    assert(menu.RetainedPlayerIdentity(31)=="4294963231");
    menu.Handle("select",{"31",menu.RetainedPlayerIdentity(31)});
    assert(menu.retainedStatIdentity==menu.RetainedPlayerIdentity(31));
    const int mutes=menu.mutes,friends=menu.friends;
    menu.Handle("mute",{"0",menu.RetainedPlayerIdentity(0)});
    menu.Handle("friend",{"0",menu.RetainedPlayerIdentity(0)});
    assert(menu.mutes==mutes && menu.friends==friends);
    std::puts("PASS: all 32 slots, generation reuse, independent viewers, disconnected/non-player targets, frozen dispatch and unsigned identities");
}
'''


def main():
    paths = ['src/game/Player.cpp', 'src/game/MultiplayerGame.cpp', 'tools/tests/ui_player_identity.py']
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    bindings = {p: digest(ROOT / p) for p in paths}
    player = (ROOT / paths[0]).read_text()
    game = (ROOT / paths[1]).read_text()
    marks = '\n'.join(function_body(player, signature) for signature in SIGNATURES)
    identity = function_body(game, 'idStr idMultiplayerGame::RetainedPlayerIdentity(')
    dispatch = function_body(game, 'if ( ( !sub.Icmp( "select" ) || !sub.Icmp( "mute" )')
    production = HARNESS.replace('@MARKS@', marks).replace('@IDENTITY@', identity).replace('@DISPATCH@', dispatch)
    cases = {'production': production}
    for field in ('mutedPlayerIdentities', 'friendPlayerIdentities'):
        anchor = f'{field}[ clientNum ] == gameLocal.GetSpawnId( target )'
        assert production.count(anchor) == 1
        cases[field + '-unguarded'] = production.replace(anchor, 'true')
    anchor = 'identity != RetainedPlayerIdentity( client )'
    assert production.count(anchor) == 1
    cases['dispatch-unguarded'] = production.replace(anchor, 'false')
    scratch = ROOT / '.tmp/ui-player-identity/marks'
    scratch.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('C++ compiler required')
    report = {'sources': bindings, 'cases': [], 'passed': False}
    for name, source in cases.items():
        cpp, exe = scratch / f'{name}.cpp', scratch / (name + ('.exe' if os.name == 'nt' else '.bin'))
        cpp.write_text(source, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], cwd=scratch, check=True)
        run = subprocess.run([str(exe)], cwd=scratch, capture_output=True, text=True)
        passed = run.returncode == 0 if name == 'production' else run.returncode != 0 and 'assertion' in run.stderr.lower()
        report['cases'].append({'name': name, 'passed': passed, 'exit': run.returncode, 'output': run.stdout + run.stderr})
        if not passed:
            raise RuntimeError(run.stdout + run.stderr)
        print(f'{name}: PASS')
        cpp.unlink(); exe.unlink()
    assert bindings == {p: digest(ROOT / p) for p in paths}
    report['passed'] = True
    (scratch / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
