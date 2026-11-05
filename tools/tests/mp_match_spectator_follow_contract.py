#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute live follow commands and packets against the real disclosure policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from mp_match_disclosure_policy_contract import HARNESS, function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <string>
#include <vector>
template<class T>T Min(T a,T b){return std::min(a,b);}
static const int MAX_CLIENTS=32,MAX_ARENAS=4,DEMO_NONE=0,GAME_TOURNEY=1;
static const int AS_INACTIVE=0,AS_DONE=2,BUTTON_ATTACK=1;
struct idStr {static int Icmp(const char*a,const char*b){
    std::string x=a,y=b;for(char&c:x)c=static_cast<char>(tolower(c));for(char&c:y)c=static_cast<char>(tolower(c));return x.compare(y);
}};
struct idCmdArgs {
    std::vector<std::string> values;
    idCmdArgs(std::initializer_list<const char*> args){for(auto a:args)values.emplace_back(a);}
    int Argc()const{return static_cast<int>(values.size());}
    const char *Argv(int n)const{return n>=0&&n<Argc()?values[n].c_str():"";}
};
struct Common {
    std::vector<std::string> printed;
    const char *GetLocalizedString(const char *key){return key;}
    void Printf(const char*,...){printed.emplace_back("feedback");}
} commonValue;
Common *common=&commonValue;
struct Bots {bool bots[MAX_CLIENTS]={};bool IsBot(int slot)const{return slot>=0&&slot<MAX_CLIENTS&&bots[slot];}}botManager;
struct idEntity {bool player=true;int spawnId=1;bool IsType(int)const{return player;}};
class idPlayer:public idEntity {
public:
    enum spectatorFollow_t {@ENUM@};
    enum {EVENT_SPECTATOR_FOLLOW=12};
    int entityNumber=0,spectator=0,currentWeapon=0,lastSpectateChange=0,lastArenaChange=0,arena=0,hudUpdates=0,freeMoves=0;
    int spectatorFollowSlot=-1,spectatorFollowSpawnId=0;
    bool spectating=false,fake=false,wantSpectate=false,connected=true;
    struct {int upmove=0,buttons=0;}usercmd;
    std::vector<byte> sent;
    static int GetClassType(){return 1;}bool IsFakeClient()const{return fake;}bool IsHidden()const{return spectating;}
    int GetArena()const{return arena;}void JoinInstance(int value){arena=value;}
    void UpdateHudWeapon(int){++hudUpdates;}
    void SpectateFreeFly(bool force);
    void ClientSendEvent(int event,const idBitMsg*msg){assert(event==EVENT_SPECTATOR_FOLLOW);sent.assign(msg->GetData(),msg->GetData()+msg->GetSize());}
    void SpectateCycle(int direction=1);bool CycleSpectatorFollow(int direction);
    void RequestSpectatorFollow(spectatorFollow_t operation,int targetSlot=-1);
    void ServerSpectatorFollow(int operation,int targetSlot,int targetSpawnId);
    void UpdateSpectating();bool ReceiveFollow(const idBitMsg&msg);
};
struct Arena {int state=1;idPlayer *players[2]={};int GetState()const{return state;}};
struct rvTourneyGameState {
    Arena arenas[MAX_ARENAS];int next=0,prev=0;
    Arena &GetArena(int n){return arenas[n];}idPlayer **GetArenaPlayers(int n){return arenas[n].players;}
    void SpectateCycleNext(idPlayer*p){++next;p->CycleSpectatorFollow(1);}
    void SpectateCyclePrev(idPlayer*p){++prev;p->CycleSpectatorFollow(-1);}
};
struct Multiplayer {
    mpMatchDisclosurePolicy_t policy;
    mpMatchDisclosureRecipient_t recipient;
    bool ingame[MAX_CLIENTS]={};int sides[MAX_CLIENTS]={};
    rvTourneyGameState state;
    Multiplayer(){policy.Clear();recipient.Clear();}
    bool IsInGame(int slot)const{return slot>=0&&slot<MAX_CLIENTS&&ingame[slot];}
    bool CanSpectatorFollow(int observer,int target)const;
    rvTourneyGameState *GetGameState(){return &state;}
};
struct Game {
    bool isServer=true,isClient=false,isMultiplayer=true,isRepeater=false;
    int numClients=4,time=1000,gameType=0,localClientNum=0,demo=DEMO_NONE;
    idEntity *entities[MAX_CLIENTS]={};Multiplayer mpGame;
    std::vector<std::string> feedback;
    int GetDemoState()const{return demo;}
    int GetSpawnId(const idEntity *p)const{return p->spawnId;}
    idPlayer *GetLocalPlayer()const{return localClientNum>=0&&localClientNum<MAX_CLIENTS?static_cast<idPlayer*>(entities[localClientNum]):nullptr;}
    idPlayer *GetClientByNum(int slot)const{if(slot==MAX_CLIENTS)return nullptr;if(slot<0||slot>=numClients)slot=0;return static_cast<idPlayer*>(entities[slot]);}
    void ServerSendChatMessage(int slot,const char*,const char*text,const char*){assert(slot==0);feedback.emplace_back(text);}
} gameLocal;
bool Multiplayer::CanSpectatorFollow(int observer,int target)const {
    if(!gameLocal.isServer||observer<0||target<0||observer>=gameLocal.numClients||target>=gameLocal.numClients||observer==target)return false;
    const idPlayer *p=gameLocal.GetClientByNum(observer),*t=gameLocal.GetClientByNum(target);
    if(!p||!t||!p->player||!t->player||p->fake||botManager.IsBot(observer)||!p->spectating||!p->connected||
       t->spectating||t->wantSpectate||!t->connected||!IsInGame(target))return false;
    return MPMatchDisclosureCanFollow(policy,recipient,static_cast<uint64_t>(target+100),sides[target],true);
}
void idPlayer::SpectateFreeFly(bool force){
    if(force||gameLocal.time>lastSpectateChange){spectator=entityNumber;++freeMoves;lastSpectateChange=gameLocal.time+500;}
}
'''

MAIN = r'''
static void Tick(){gameLocal.time+=501;}
static void Deliver(idPlayer &observer,const std::vector<byte>&bytes){
    idBitMsg msg;msg.Init(bytes.data(),static_cast<int>(bytes.size()));observer.ReceiveFollow(msg);
}
static std::vector<byte> Packet(int op,int slot,int spawn){
    byte buffer[6];idBitMsg msg;msg.Init(buffer,6);msg.BeginWriting();msg.WriteByte(op);msg.WriteByte(slot+1);msg.WriteLong(spawn);return {buffer,buffer+6};
}
static void Prepare(idPlayer (&players)[4]){
    gameLocal=Game();botManager=Bots();
    gameLocal.mpGame.policy.lockedSpectatorSideMask=0;
    for(int i=0;i<4;++i){players[i]=idPlayer();players[i].entityNumber=i;players[i].spectator=i;players[i].spawnId=i+20;
        gameLocal.entities[i]=&players[i];gameLocal.mpGame.ingame[i]=true;gameLocal.mpGame.sides[i]=(i==2?1:0);}
    players[0].spectating=true;
    auto&r=gameLocal.mpGame.recipient;r.sessionId=1;r.sessionRevision=1;r.participantId=100;r.slot=0;r.bindingGeneration=1;
    r.side=MP_MATCH_SIDE_NONE;r.roles=0;r.active=false;r.repeater=false;
}
int main(){
    idPlayer players[4];Prepare(players);idPlayer &observer=players[0];
    // Console directions and attack execute the same authorized candidate walk.
    for(auto op:{idPlayer::SPECTATOR_FOLLOW_NEXT,idPlayer::SPECTATOR_FOLLOW_PREV}){
        observer.spectator=0;Tick();observer.ServerSpectatorFollow(op,-1,0);
        assert(observer.spectator==(op==idPlayer::SPECTATOR_FOLLOW_NEXT?1:3));
    }
    observer.spectator=0;Tick();observer.SpectateCycle();assert(observer.spectator==1);
    gameLocal.mpGame.policy.lockedSpectatorSideMask=1;
    observer.spectator=0;Tick();observer.SpectateCycle();assert(observer.spectator==2);
    observer.spectator=0;Tick();observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_NEXT,-1,0);assert(observer.spectator==2);
    // Shared production disclosure policy keeps neutral/coach/privileged roles separate.
    for(int role:{-1,static_cast<int>(MP_MATCH_ROLE_COACH),static_cast<int>(MP_MATCH_ROLE_BROADCASTER),static_cast<int>(MP_MATCH_ROLE_REFEREE)}){
        gameLocal.mpGame.recipient.roles=role<0?0:MPMatchRoleBit(static_cast<mpMatchRole_t>(role));
        gameLocal.mpGame.recipient.side=role==MP_MATCH_ROLE_COACH?0:MP_MATCH_SIDE_NONE;
        gameLocal.mpGame.policy.allowCoachObservation=true;gameLocal.mpGame.policy.allowLiveBroadcasterObservation=true;gameLocal.mpGame.policy.allowRefereeObservation=true;
        for(int slot=1;slot<=2;++slot){
            const bool allowed=gameLocal.mpGame.CanSpectatorFollow(0,slot);observer.spectator=0;Tick();
            observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_PLAYER,slot,players[slot].spawnId);
            assert((observer.spectator==slot)==allowed);
        }
    }
    Prepare(players);
    const auto direct=Packet(idPlayer::SPECTATOR_FOLLOW_PLAYER,1,players[1].spawnId);
    for(int bytes=0;bytes<=7;++bytes){
        std::vector<byte> payload=direct;payload.resize(bytes);observer.spectator=0;Tick();Deliver(observer,payload);
        assert(observer.spectator==(bytes==6?1:0));
    }
    for(int invalid:{-1,4,255}){observer.spectator=0;Tick();Deliver(observer,Packet(invalid,1,players[1].spawnId));assert(observer.spectator==0);}
    for(int slot:{-1,32,254}){observer.spectator=0;Tick();Deliver(observer,Packet(idPlayer::SPECTATOR_FOLLOW_PLAYER,slot,players[1].spawnId));assert(observer.spectator==0);}
    observer.spectator=0;Tick();Deliver(observer,Packet(idPlayer::SPECTATOR_FOLLOW_NEXT,1,players[1].spawnId));assert(observer.spectator==0);
    // Exact spawn identity prevents a queued direct request following a replacement connection.
    ++players[1].spawnId;Tick();Deliver(observer,direct);assert(observer.spectator==0);
    --players[1].spawnId;Tick();Deliver(observer,direct);assert(observer.spectator==1);
    Tick();gameLocal.entities[1]=nullptr;observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_PLAYER,1,21);assert(observer.spectator==1);observer.UpdateSpectating();assert(observer.spectator==0);
    gameLocal.entities[1]=&players[1];players[1].player=false;Tick();Deliver(observer,direct);assert(observer.spectator==0);players[1].player=true;
    players[1].fake=true;Tick();Deliver(observer,direct);assert(observer.spectator==0);players[1].fake=false;
    gameLocal.numClients=1;assert(SpectatorFollowClient(1)==nullptr);gameLocal.numClients=4;
    for(int invalid=0;invalid<4;++invalid){
        players[1].spectating=invalid==0;players[1].wantSpectate=invalid==1;players[1].connected=invalid!=2;gameLocal.mpGame.ingame[1]=invalid!=3;
        Tick();Deliver(observer,direct);assert(observer.spectator==0);
    }
    Prepare(players);botManager.bots[1]=true;Tick();Deliver(observer,direct);assert(observer.spectator==1);
    // Bot actors and synthetic cameras never acquire a human observer's authority.
    observer.spectator=0;botManager.bots[0]=true;Tick();Deliver(observer,direct);assert(observer.spectator==0);botManager.bots[0]=false;
    observer.fake=true;Tick();Deliver(observer,direct);assert(observer.spectator==0);observer.fake=false;
    observer.spectating=false;Tick();Deliver(observer,direct);assert(observer.spectator==0);observer.spectating=true;
    // A role or policy change revokes the current camera before any input can run.
    Tick();Deliver(observer,direct);assert(observer.spectator==1);
    gameLocal.mpGame.policy.lockedSpectatorSideMask=3;observer.UpdateSpectating();assert(observer.spectator==0);
    // A replacement connection in the followed slot - a different entity with no
    // empty frame between - releases the camera by identity, not by slot number.
    gameLocal.mpGame.policy.lockedSpectatorSideMask=0;
    Tick();Deliver(observer,direct);assert(observer.spectator==1);
    observer.UpdateSpectating();assert(observer.spectator==1);
    ++players[1].spawnId;observer.UpdateSpectating();assert(observer.spectator==0);
    --players[1].spawnId;
    // Following again latches the slot's current entity afresh.
    Tick();Deliver(observer,direct);assert(observer.spectator==1);
    observer.UpdateSpectating();observer.UpdateSpectating();assert(observer.spectator==1);
    gameLocal.mpGame.policy.lockedSpectatorSideMask=3;observer.UpdateSpectating();assert(observer.spectator==0);
    // A request queued while privileged is checked against the role at receipt.
    gameLocal.mpGame.recipient.roles=MPMatchRoleBit(MP_MATCH_ROLE_BROADCASTER);
    gameLocal.mpGame.policy.allowLiveBroadcasterObservation=true;
    Tick();Deliver(observer,direct);assert(observer.spectator==1);
    gameLocal.mpGame.recipient.roles=0;
    observer.UpdateSpectating();assert(observer.spectator==0);
    Tick();Deliver(observer,direct);assert(observer.spectator==0);
    Tick();observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_NEXT,-1,0);assert(observer.spectator==0&&gameLocal.feedback.back()=="#str_42883");
    const auto feedbackCount=gameLocal.feedback.size();observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_NEXT,-1,0);assert(gameLocal.feedback.size()==feedbackCount);
    Tick();observer.ServerSpectatorFollow(idPlayer::SPECTATOR_FOLLOW_FREE,-1,0);assert(observer.spectator==0&&gameLocal.feedback.back()=="#str_42884");
    Prepare(players);gameLocal.isServer=false;gameLocal.isClient=true;
    Cmd_Follow_f({"follow","1"});assert(observer.sent==direct&&observer.spectator==0);
    for(auto alias:{"follownext","followprev","followfree"}){observer.sent.clear();Cmd_Follow_f({alias});assert(observer.sent.size()==6&&observer.sent[1]==0);}
    for(auto value:{"-1","32","999999999999999999","1;quit","1x","","1.5"}){observer.sent.clear();Cmd_Follow_f({"follow",value});assert(observer.sent.empty());}
    observer.sent.clear();Cmd_Follow_f({"follownext","1"});assert(observer.sent.empty());
    for(int invalid=0;invalid<5;++invalid){
        gameLocal.demo=invalid==0?1:0;gameLocal.isRepeater=invalid==1;observer.spectating=invalid!=2;observer.fake=invalid==3;gameLocal.localClientNum=invalid==4?-1:0;
        observer.sent.clear();Cmd_Follow_f({"follow","1"});assert(observer.sent.empty());
    }
    // Menu buttons dispatch the same typed request and cannot grant authority
    // through a forged GUI visibility value or an invalid local entity.
    Prepare(players);gameLocal.isServer=false;gameLocal.isClient=true;
    assert(MatchControlFollowPlayer()==&observer);
    for(auto token:{"follow_prev","follow_next","follow_free"}){
        observer.sent.clear();assert(MatchControlFollowCommand(token));assert(observer.sent.size()==6);
        const int expected=strcmp(token,"follow_prev")==0?idPlayer::SPECTATOR_FOLLOW_PREV:
            strcmp(token,"follow_next")==0?idPlayer::SPECTATOR_FOLLOW_NEXT:idPlayer::SPECTATOR_FOLLOW_FREE;
        assert(observer.sent[0]==expected&&observer.sent[1]==0);
    }
    for(auto token:{"follow 1","follow_prev extra","follow_next;quit",""}){
        observer.sent.clear();assert(!MatchControlFollowCommand(token));assert(observer.sent.empty());
    }
    for(int invalid=0;invalid<9;++invalid){
        Prepare(players);gameLocal.isServer=false;gameLocal.isClient=true;
        gameLocal.demo=invalid==0?1:0;gameLocal.isRepeater=invalid==1;observer.spectating=invalid!=2;
        observer.fake=invalid==3;botManager.bots[0]=invalid==4;gameLocal.mpGame.ingame[0]=invalid!=5;
        observer.player=invalid!=6;gameLocal.numClients=invalid==7?0:4;observer.entityNumber=invalid==8?1:0;
        assert(MatchControlFollowPlayer()==nullptr);assert(MatchControlFollowCommand("follow_next"));assert(observer.sent.empty());
    }
    Prepare(players);gameLocal.gameType=GAME_TOURNEY;
    Tick();Cmd_Follow_f({"follow","next"});assert(gameLocal.mpGame.state.next==1);
    Tick();Cmd_Follow_f({"follow","prev"});assert(gameLocal.mpGame.state.prev==1);
    observer.spectator=0;players[1].arena=1;gameLocal.mpGame.state.arenas[1].players[0]=&players[1];
    Tick();Deliver(observer,direct);assert(observer.spectator==1&&observer.arena==1);
    observer.spectator=0;gameLocal.mpGame.state.arenas[1].state=AS_DONE;Tick();Deliver(observer,direct);assert(observer.spectator==0);
    // Empty scans cannot divide by zero or retain an old target.
    gameLocal.gameType=0;gameLocal.numClients=0;Tick();observer.SpectateCycle();assert(observer.spectator==0);
}
'''


def main() -> None:
    player = (ROOT / "src/mpgame/Player.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/mpgame/Player.h").read_text(encoding="utf-8")
    commands = (ROOT / "src/mpgame/gamesys/SysCmds.cpp").read_text(encoding="utf-8")
    multiplayer = (ROOT / "src/mpgame/MultiplayerGame.cpp").read_text(encoding="utf-8")
    network = (ROOT / "src/mpgame/Game_network.cpp").read_text(encoding="utf-8")
    assert "entPtr.GetEntity() != entities[ event->sender ]" in network
    assert "event->time = time;" in network
    assert "CheckClientReliableFlood" in network
    for command in ("follow", "follownext", "followprev", "followfree"):
        assert f'AddCommand( "{command}", Cmd_Follow_f, CMD_FL_GAME' in commands
    signatures = (
        "static idPlayer *SpectatorFollowClient( int slot )",
        "bool idPlayer::CycleSpectatorFollow( int direction )",
        "void idPlayer::SpectateCycle( int direction )",
        "void idPlayer::RequestSpectatorFollow( spectatorFollow_t operation, int targetSlot )",
        "void idPlayer::ServerSpectatorFollow( int operation, int targetSlot, int targetSpawnId )",
        "void idPlayer::UpdateSpectating( void )",
    )
    extracted = "\n".join(signature + " {" + function_body(player, signature) + "}\n" for signature in signatures)
    extracted += "bool idPlayer::ReceiveFollow(const idBitMsg &msg) {" + function_body(player, "case EVENT_SPECTATOR_FOLLOW:") + "}\n"
    extracted += "static void Cmd_Follow_f(const idCmdArgs &args) {" + function_body(commands, "static void Cmd_Follow_f(") + "}\n"
    for signature in ("static idPlayer *MatchControlFollowPlayer( void )",
                      "static bool MatchControlFollowCommand( const char *token )"):
        extracted += signature + " {" + function_body(multiplayer, signature) + "}\n"
    assert "MatchControlFollowCommand( token )" in function_body(multiplayer, "bool idMultiplayerGame::HandleMatchControlCommand(")
    # A casual session mirrors participation in every phase. Syncing only in
    # warmup/countdown left bots added mid-match inactive, so the server refused
    # them as follow targets (and a player who switched to spectating kept a
    # player's view rights) until the next warmup.
    run = function_body(multiplayer, "void idMultiplayerGame::Run( void )")
    sync = run.index("SynchronizeAllMatchParticipants();")
    guard = run.rfind("if (", 0, sync)
    assert "matchSession.GetPhase() == WARMUP" in run[guard:sync] and \
        "!IsManagedMatch()" in run[guard:sync], "casual sessions must sync participants in every phase"
    update = function_body(multiplayer, "void idMultiplayerGame::UpdateMainGui(")
    assert update.index('SetStateBool( "match_follow_visible", MatchControlFollowPlayer() != NULL )') < update.index("mainGui->StateChanged")
    preamble = HARNESS.split("#define CHECK")[0].replace(
        "bool IsOverflowed() const { return overflowed; }",
        "bool IsOverflowed() const { return overflowed; } bool IsReadOverflowed() const { return overflowed; }")
    code = preamble + SUPPORT.replace("@ENUM@", function_body(header, "enum spectatorFollow_t")) + extracted + MAIN
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="spectator-follow-", dir=temporary) as directory:
        source, binary = Path(directory) / "follow.cpp", Path(directory) / "follow.exe"
        source.write_text(code, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DMP_MATCH_SESSION_STANDALONE_TEST", "-DMP_MATCH_DISCLOSURE_STANDALONE_TEST",
            "-I" + str(ROOT / "src"), str(source),
            str(ROOT / "src/mpgame/mp/match/MatchDisclosurePolicy.cpp"),
            str(ROOT / "src/mpgame/mp/match/MatchSession.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("mp_match_spectator_follow_contract: PASS (production command/camera/event paths and disclosure policy)")


if __name__ == "__main__":
    main()
