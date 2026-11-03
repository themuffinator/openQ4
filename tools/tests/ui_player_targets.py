#!/usr/bin/env python3
"""Run production mute/vote/admin guards against delayed player replacement.

The actual game methods, reliable-message branch and player mute storage are
compiled with bounded engine/network doubles. No OS input or game is launched.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import time

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <map>
constexpr int MAX_CLIENTS=32, CMD_EXEC_NOW=0, GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING=19;
constexpr int VOTE_NONE=0,VOTE_KICK=1,VOTE_MULTIFIELD=2,VOTEFLAG_KICK=16;
constexpr int VOTE_ABORTED=1,VOTE_RESET=2,VOTE_PASSED=3,VOTE_FAILED=4;
struct idStr:std::string {
    using std::string::string; using std::string::operator=;
    int Length()const{return int(size());} bool IsEmpty()const{return empty();} void Clear(){clear();}
    int Icmp(const char* text)const{return compare(text);}
    static int Cmp(const char* a,const char* b){return std::strcmp(a,b);}
};
const char* va(const char* format,...){static char text[512];va_list a;va_start(a,format);std::vsnprintf(text,sizeof(text),format,a);va_end(a);return text;}
struct idDict {
    std::map<std::string,std::string> values;
    int GetInt(const char* k)const{auto i=values.find(k);return i==values.end()?0:std::atoi(i->second.c_str());}
    const char* GetString(const char* k)const{auto i=values.find(k);return i==values.end()?"":i->second.c_str();}
};
using byte=unsigned char;
struct idBitMsg {
    std::vector<byte> bytes;mutable size_t cursor=0;
    void Init(byte*,int){} void WriteByte(int v){bytes.push_back(byte(v));}
    void WriteLong(int v){for(int i=0;i<4;++i)WriteByte(uint32_t(v)>>(i*8));}
    int GetRemainingReadBits()const{return int(bytes.size()-cursor)*8;}
    int ReadByte()const{assert(cursor<bytes.size());return bytes[cursor++];}
    int ReadLong()const{uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(ReadByte())<<(i*8);return int(v);}
};
struct idCmdArgs {std::vector<std::string> values;int Argc()const{return int(values.size());}const char* Argv(int i)const{return values.at(i).c_str();}};
struct idEntity {int entityNumber=0;bool player=true;bool IsType(int)const{return player;}};
struct idPlayer:idEntity {
    unsigned mutedPlayers=0;int mutedPlayerIdentities[MAX_CLIENTS]={};
    static int GetClassType(){return 1;}
    void MutePlayer(int,bool);bool IsPlayerMuted(int)const;
    void MutePlayer(idPlayer* p,bool value){MutePlayer(p->entityNumber,value);}
};
struct Common {template<class... T>void DPrintf(T...){}template<class... T>void Printf(T...){}template<class... T>void Warning(T...){}} commonObject,*common=&commonObject;
struct Commands {std::vector<std::string> commands;void BufferCommandText(int,const char* s){commands.emplace_back(s);}} commandsObject,*cmdSystem=&commandsObject;
struct Network {std::vector<idBitMsg> sent;void ClientSendReliableMessage(const idBitMsg& m){sent.push_back(m);}} networkObject,*networkSystem=&networkObject;
struct voteStruct_t {int m_fieldFlags=0,m_kick=-1;};
struct serverAdminData_t {};
static bool IsValidVotePlayerSlot(int);
struct idMultiplayerGame {
    enum {RAA_KICK,RAA_BAN,RAA_SWITCH,RAA_APPLY};
    bool admin=true,managed=false,arena=false;int retainedAdminClient=-1;idStr retainedAdminIdentity;
    int vote=VOTE_NONE,voteExecTime=0,voteTimeOut=0,voteKickIdentity=0,yesVotes=0,noVotes=0,voteEligibleCount=3;
    int executed=0;std::vector<int> updates;idStr voteValue;voteStruct_t currentVoteData;
    bool IsInGame(int)const{return true;}
    idStr RetainedPlayerIdentity(int)const; bool RetainedListed(int)const{return true;}
    bool IsArenaCampaignMatch()const{return arena;}bool IsManagedMatch()const{return managed;}
    bool RetainedAdminChanges(serverAdminData_t&)const{return false;}
    bool RejectManagedLegacyMutation(const char*)const{return managed;}
    void ClientVoiceMute(int,bool);void ServerHandleVoiceMuting(int,int,int,bool);
    bool VoteKickTargetCurrent()const;void CheckVote();bool AbortInheritedVoteForManagedMatch(){return false;}
    void ClientUpdateVote(int v,int,int,const voteStruct_t&){updates.push_back(v);}
    void ExecuteVote(){++executed;}
    void HandleServerAdminKickPlayer(int);void HandleServerAdminBanPlayer(int);void HandleServerAdminForceTeamSwitch(int);
    static void ServerAdminTarget_f(const idCmdArgs&);
    const char* RetainedAdminRefusal(int);
    bool Handle(const char* verb,std::vector<std::string> values){
        if(!admin)return false;idStr sub(verb);idCmdArgs args{values};int icmd=0;
        @SMALL_NUMBER@
        @ADMIN_SELECT@
        @ADMIN_ACTION@
        return false;
    }
};
struct Game {
    bool isClient=false,isServer=true,isListenServer=false,team=true;
    int numClients=MAX_CLIENTS,localClientNum=0,time=100;
    idEntity* entities[MAX_CLIENTS]={};unsigned generations[MAX_CLIENTS]={};
    idDict serverInfo,userInfo[MAX_CLIENTS];idMultiplayerGame mpGame;
    int GetSpawnId(const idEntity* e)const{return int((generations[e->entityNumber]<<12)|unsigned(e->entityNumber));}
    idPlayer* GetLocalPlayer(){return static_cast<idPlayer*>(entities[0]);}
    idPlayer* GetClientByNum(int i){return static_cast<idPlayer*>(entities[i]);}
    idDict* GetUserInfo(int i){return &userInfo[i];}
    bool IsTeamGame()const{return team;}
    template<class... T>void Warning(T...){}
}gameLocal;
@VALID@
@CURRENT@
bool ParseVotePlayerSlot(const char* s,int& slot){
    if(!s||!*s||std::strlen(s)>2||(s[0]=='0'&&s[1]))return false;
    for(const char* p=s;*p;++p)if(*p<'0'||*p>'9')return false;
    slot=std::atoi(s);return IsValidVotePlayerSlot(slot);
}
@METHODS@
void Receive(int clientNum,idBitMsg msg){auto& mpGame=gameLocal.mpGame;switch(GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING){@RECEIVER@}}
int main(){
    auto& g=gameLocal;auto& m=g.mpGame;idPlayer players[MAX_CLIENTS];
    for(int i=0;i<MAX_CLIENTS;++i){players[i].entityNumber=i;g.entities[i]=&players[i];g.generations[i]=2;}
    g.isServer=false;g.isClient=true;
    // Old/unknown servers get no ambiguous packet; local muting still works.
    for(const char* capability:{"","0","2"}){
        g.serverInfo.values["si_uiTargetProtocol"]=capability;m.ClientVoiceMute(31,true);
        assert(players[0].IsPlayerMuted(31)&&networkObject.sent.empty());m.ClientVoiceMute(31,false);
    }
    g.serverInfo.values["si_uiTargetProtocol"]="1";
    m.ClientVoiceMute(31,true);assert(networkObject.sent.size()==1);
    auto packet=networkObject.sent.back();assert(packet.bytes.size()==7&&packet.bytes[0]==GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING);
    packet.bytes.erase(packet.bytes.begin());g.isClient=false;g.isServer=true;
    Receive(1,packet);assert(players[1].IsPlayerMuted(31));
    ++g.generations[31];Receive(1,packet);assert(!players[1].IsPlayerMuted(31));
    // The same pointer and name are insufficient. Stale unmute cannot clear
    // the replacement's independently acquired mark either.
    players[1].MutePlayer(31,true);packet.bytes[1]=0;Receive(1,packet);assert(players[1].IsPlayerMuted(31));
    g.generations[31]=0xfffff;packet.bytes.resize(2);packet.WriteLong(g.GetSpawnId(&players[31]));
    Receive(1,packet);assert(!players[1].IsPlayerMuted(31));
    packet.bytes[1]=1;Receive(1,packet);assert(players[1].IsPlayerMuted(31));
    for(int size=0;size<12;++size){if(size==6)continue;auto bad=packet;bad.bytes.resize(size);players[1].MutePlayer(31,false);Receive(1,bad);assert(!players[1].IsPlayerMuted(31));}
    for(int boolean=2;boolean<256;++boolean){auto bad=packet;bad.bytes[1]=byte(boolean);Receive(1,bad);assert(!players[1].IsPlayerMuted(31));}
    for(int slot:{-1,32,255}){m.ServerHandleVoiceMuting(slot,31,g.GetSpawnId(&players[31]),true);m.ServerHandleVoiceMuting(1,slot,g.GetSpawnId(&players[31]),true);}
    assert(!players[1].IsPlayerMuted(31));
    players[31].player=false;Receive(1,packet);assert(!players[1].IsPlayerMuted(31));players[31].player=true;
    // Both single and mixed votes, before passing and during the delay.
    for(int type:{VOTE_KICK,VOTE_MULTIFIELD})for(bool delayed:{false,true}){
        m.vote=type;m.voteValue="31";m.currentVoteData={VOTEFLAG_KICK,31};m.voteKickIdentity=g.GetSpawnId(&players[31]);
        m.voteExecTime=delayed?g.time-1:0;m.voteTimeOut=g.time+500;m.yesVotes=1;m.noVotes=0;m.updates.clear();
        assert(m.VoteKickTargetCurrent());++g.generations[31];assert(!m.VoteKickTargetCurrent());
        const int before=m.executed;m.CheckVote();assert(m.executed==before&&m.vote==VOTE_NONE&&m.voteExecTime==0&&m.updates.back()==VOTE_ABORTED);
    }
    m.vote=VOTE_KICK;m.voteValue="31";m.voteKickIdentity=g.GetSpawnId(&players[31]);m.voteExecTime=g.time-1;
    m.CheckVote();assert(m.executed==1);
    // A queued selection/action must carry the original identity, and must
    // still match the active selection. Same-slot ABA never revives it.
    auto identity=m.RetainedPlayerIdentity(31);m.Handle("adminSelect",{"31",identity});assert(m.retainedAdminClient==31);
    for(const char* operation:{"adminKick","adminBan","adminSwitch"}){
        m.Handle("adminSelect",{"31",identity});commandsObject.commands.clear();m.Handle(operation,{"31",identity});assert(commandsObject.commands.size()==1);
    }
    m.Handle("adminSelect",{"31",identity});++g.generations[31];commandsObject.commands.clear();
    for(const char* operation:{"adminKick","adminBan","adminSwitch"})m.Handle(operation,{"31",identity});
    assert(commandsObject.commands.empty());m.retainedAdminClient=-1;m.Handle("adminSelect",{"31",identity});assert(m.retainedAdminClient==-1);
    // Authenticated rcon still has to match identity at execution, on the
    // server, rather than trusting a client-side menu check.
    for(const char* operation:{"kick","ban","switch"}){
        idMultiplayerGame::ServerAdminTarget_f({{"serverAdminTarget",operation,"31",identity}});assert(commandsObject.commands.empty());
        auto current=m.RetainedPlayerIdentity(31);idMultiplayerGame::ServerAdminTarget_f({{"serverAdminTarget",operation,"31",current}});assert(commandsObject.commands.size()==1);commandsObject.commands.clear();
        m.managed=true;idMultiplayerGame::ServerAdminTarget_f({{"serverAdminTarget",operation,"31",current}});assert(commandsObject.commands.empty());m.managed=false;
    }
    auto current=m.RetainedPlayerIdentity(31);g.isServer=false;g.isClient=true;
    m.HandleServerAdminKickPlayer(31);assert(commandsObject.commands.back()==std::string("rcon serverAdminTarget kick 31 ")+current);
    commandsObject.commands.clear();g.serverInfo.values["si_uiTargetProtocol"]="0";
    m.retainedAdminClient=31;m.retainedAdminIdentity=current;
    assert(std::strcmp(m.RetainedAdminRefusal(m.RAA_KICK),"#str_231577")==0);
    m.HandleServerAdminKickPlayer(31);m.HandleServerAdminBanPlayer(31);m.HandleServerAdminForceTeamSwitch(31);assert(commandsObject.commands.empty());
    std::puts("PASS: exact mute wire, stale mute/unmute, mixed vote cancellation, admin selection/dispatch/rcon, capability fallback");
}
'''


def main():
    sources = ['src/mpgame/MultiplayerGame.cpp', 'src/mpgame/Game_network.cpp', 'src/mpgame/Player.cpp',
               'tools/tests/ui_player_targets.py']
    digest = lambda p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
    bindings = {p: digest(p) for p in sources}
    game, network, player = ((ROOT / p).read_text() for p in sources[:3])
    methods = [function_body(player, signature) for signature in ('void idPlayer::MutePlayer( int', 'bool idPlayer::IsPlayerMuted( int')]
    for signature in ('idStr idMultiplayerGame::RetainedPlayerIdentity(', 'void idMultiplayerGame::ClientVoiceMute(',
                      'void idMultiplayerGame::ServerHandleVoiceMuting(', 'bool idMultiplayerGame::VoteKickTargetCurrent(',
                      'void idMultiplayerGame::CheckVote(', 'static bool SendRemoteAdminPlayerTarget(',
                      'void idMultiplayerGame::ServerAdminTarget_f(', 'void idMultiplayerGame::HandleServerAdminKickPlayer(',
                      'void idMultiplayerGame::HandleServerAdminBanPlayer(', 'void idMultiplayerGame::HandleServerAdminForceTeamSwitch(',
                      'const char *idMultiplayerGame::RetainedAdminRefusal('):
        methods.append(function_body(game, signature))
    admin = function_body(game, 'bool idMultiplayerGame::HandleRetainedAdminCommand(')
    values = {
        'VALID': function_body(game, 'static bool IsValidVotePlayerSlot('),
        'CURRENT': function_body(game, 'static bool IsCurrentPlayerTarget('),
        'METHODS': '\n'.join(methods),
        'SMALL_NUMBER': function_body(admin, 'const auto smallNumber =') + ';',
        'ADMIN_SELECT': function_body(admin, 'if ( !sub.Icmp( "adminSelect" ) )'),
        'ADMIN_ACTION': function_body(admin, 'if ( !sub.Icmp( "adminKick" )'),
        'RECEIVER': function_body(network, 'case GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING:')
    }
    production = HARNESS
    for key, value in values.items():
        production = production.replace('@' + key + '@', value)
    mutations = {
        'slot-only-mute': ('!IsCurrentPlayerTarget( clientDest, identity )', '!IsValidVotePlayerSlot( clientDest )'),
        'slot-only-vote': ('IsCurrentPlayerTarget( client, voteKickIdentity )', 'IsValidVotePlayerSlot( client )'),
        'slot-only-admin': ('identity != RetainedPlayerIdentity( client ) || RetainedAdminRefusal', 'false || RetainedAdminRefusal'),
        'slot-only-rcon': ('gameLocal.mpGame.RetainedPlayerIdentity( client ) != args.Argv( 3 )', 'false'),
        'old-server-mute': ('gameLocal.isClient && gameLocal.serverInfo.GetInt( "si_uiTargetProtocol" ) == 1', 'gameLocal.isClient'),
    }
    scratch = ROOT / '.tmp/ui-roadmap/player-targets'
    scratch.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('C++ compiler required')
    report = {'status': 'incomplete', 'sources': bindings, 'cases': []}
    report_path = scratch / 'report.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    environment = {**os.environ, 'TEMP': str(scratch), 'TMP': str(scratch), 'TMPDIR': str(scratch)}
    for name in ['production', *mutations]:
        source = production
        if name != 'production':
            before, after = mutations[name]
            assert source.count(before) == 1, name
            source = source.replace(before, after)
            # Remove both independent admin generation checks in this control.
            if name == 'slot-only-admin':
                source = source.replace('retainedAdminIdentity != RetainedPlayerIdentity( retainedAdminClient )', 'false')
        cpp = scratch / 'fixture.cpp'
        exe = scratch / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(source, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', str(cpp), '-o', str(exe)],
                       cwd=scratch, env=environment, check=True, timeout=120)
        run = subprocess.run([str(exe)], cwd=scratch, env=environment, capture_output=True, text=True, timeout=30)
        passed = run.returncode == 0 if name == 'production' else run.returncode != 0 and 'assertion' in run.stderr.lower()
        report['cases'].append({'name': name, 'passed': passed, 'exit': run.returncode, 'output': run.stdout + run.stderr})
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        if not passed:
            raise RuntimeError(name + ': ' + run.stdout + run.stderr)
        print(name + ': PASS')
        cpp.unlink()
        # The Windows image loader/virus scanner can briefly retain a handle
        # after an assertion exits. Do not mistake that for a failed guard.
        for attempt in range(20):
            try:
                exe.unlink()
                break
            except PermissionError:
                if attempt == 19:
                    raise
                time.sleep(.1)
    assert bindings == {p: digest(p) for p in sources}
    report['status'] = 'passed'
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
