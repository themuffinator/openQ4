#!/usr/bin/env python3
"""Execute the MP HUD's production context and respawn projections without a game."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include "src/mpgame/mp/MatchPhase.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
enum {MP_MATCH_VIEW_PAUSE_RUNNING=0,GTF_ELIMINATION=1,AS_INACTIVE=0,AS_WARMUP=1,AS_ROUND=2,AS_SUDDEN_DEATH=3,AS_DONE=4,MAX_ARENAS=8};
struct GUI {
    std::map<std::string,std::string> values;
    void SetStateString(const char* key,const char* value){values[key]=value;}
    void SetStateInt(const char* key,int value){values[key]=std::to_string(value);}
    void SetStateBool(const char* key,bool value){SetStateInt(key,value);}
    const char* GetStateString(const char* key){return values[key].c_str();}
    int GetStateInt(const char* key){return std::atoi(GetStateString(key));}
};
struct mpMatchViewLifecycle_t {mpGameState_t phase=GAMEON;int pauseState=MP_MATCH_VIEW_PAUSE_RUNNING;};
struct Present {bool present=false;};
// MatchView.h's public roles and MPMatchViewRoleBit, which the casual caster
// panel (MP-16) reads from the recipient's public role mask.
typedef enum {MP_MATCH_VIEW_ROLE_NONE=0,MP_MATCH_VIEW_ROLE_PLAYER,MP_MATCH_VIEW_ROLE_CAPTAIN,MP_MATCH_VIEW_ROLE_COACH,
    MP_MATCH_VIEW_ROLE_BROADCASTER,MP_MATCH_VIEW_ROLE_REFEREE,MP_MATCH_VIEW_ROLE_COUNT} mpMatchViewPublicRole_t;
unsigned MPMatchViewRoleBit(mpMatchViewPublicRole_t role){
    return role>MP_MATCH_VIEW_ROLE_NONE&&role<MP_MATCH_VIEW_ROLE_COUNT?(1u<<static_cast<unsigned>(role)):0u;}
struct View {
    struct Public {mpMatchViewLifecycle_t lifecycle;Present globalProposal,series;struct {unsigned publicRoleMask=0;} recipient;} publicState;
    Present ownSideProposal;
    int teamVitalCount=0;
};
int casterProjections=0;
void ProjectCasterVitals(GUI*){++casterProjections;}
void ProjectContext(GUI* gui,const View& clientMatchView) {
@CONTEXT@
}
struct idPlayer {
    bool spectating=false,leader=false;int health=0,minRespawnTime=0,arena=0;
    struct {bool dead=true;} pfl;
    bool IsLeader()const{return leader;}
    int GetArena()const{return arena;}
};
struct rvTourneyArena {
    int state=AS_ROUND;std::array<idPlayer*,2> players{};
    int GetState()const{return state;}
    idPlayer** GetPlayers(){return players.data();}
};
struct rvTourneyGameState {
    bool allow=true;std::array<rvTourneyArena,MAX_ARENAS> arenas;
    bool AllowRespawn(idPlayer*)const{return allow;}
    rvTourneyArena& GetArena(int arena){return arenas.at(arena);}
} fixtureState,*gameState=&fixtureState;
struct {int time=10000,gameType=0;} gameLocal;
bool campaign=false;
bool IsArenaCampaignMatch(){return campaign;}
bool MPGameTypeHasAny(int type,int flags){return (type&flags)!=0;}
void ProjectRespawn(GUI* _mphud,idPlayer* localPlayer,mpGameState_t state,bool tourney=false,bool teamGame=false) {
@RESPAWN@
}
int checks=0;
#define CHECK(value) do {++checks;if(!(value)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value);std::exit(1);}} while(false)
int main() {
    GUI gui;View view;
    gui.SetStateInt("match_context_visible",1);
    gui.SetStateString("match_context_proposal","Proposal");
    gui.SetStateString("match_context_series","Series 1-0");
    gui.SetStateString("match_context_timeouts","Timeouts 1/2");
    for (auto phase:{INACTIVE,WARMUP,COUNTDOWN,GAMEON,SUDDENDEATH,GAMEREVIEW,NEXTGAME}) for(int pause:{0,1}) {
        view.publicState.lifecycle={phase,pause};ProjectContext(&gui,view);
        const bool live=(phase==GAMEON||phase==SUDDENDEATH)&&pause==0;
        CHECK(gui.GetStateInt("match_context_compact")==live);
        CHECK(gui.values["match_context_summary"]==(live?"Timeouts 1/2":""));
    }
    view.publicState.lifecycle={GAMEON,0};
    for(bool series:{false,true}) for(bool global:{false,true}) for(bool own:{false,true}) {
        view.publicState.series.present=series;view.publicState.globalProposal.present=global;view.ownSideProposal.present=own;
        ProjectContext(&gui,view);
        CHECK(gui.values["match_context_summary"]==(global||own?"Proposal":series?"Series 1-0":"Timeouts 1/2"));
        CHECK(gui.values["match_context_series"]=="Series 1-0" && gui.values["match_context_timeouts"]=="Timeouts 1/2" &&
            gui.values["match_context_proposal"]=="Proposal");
    }
    CHECK(casterProjections==0);
    gui.SetStateInt("match_context_visible",0);ProjectContext(&gui,view);
    CHECK(!gui.GetStateInt("match_context_compact") && gui.values["match_context_summary"].empty());
    // The caster panel: only outside a managed record, only with disclosed
    // vitals, and only for the broadcaster's role.
    CHECK(casterProjections==0);
    const unsigned broadcaster=MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_BROADCASTER);
    const unsigned playerRole=MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
    for(int visible:{0,1}) for(int vitals:{0,2}) for(unsigned mask:{0u,playerRole,broadcaster,playerRole|broadcaster}) {
        gui.SetStateInt("match_context_visible",visible);
        view.teamVitalCount=vitals;view.publicState.recipient.publicRoleMask=mask;
        const int before=casterProjections;ProjectContext(&gui,view);
        CHECK(casterProjections-before==(!visible&&vitals>0&&(mask&broadcaster)?1:0));
    }
    view.teamVitalCount=0;view.publicState.recipient.publicRoleMask=0;
    gui.SetStateInt("match_context_visible",0);
    idPlayer player;
    for(int held:{-1,0,1,999,1000,1001,3000,9100}) {
        player.minRespawnTime=gameLocal.time+held;ProjectRespawn(&gui,&player,GAMEON);
        CHECK(gui.GetStateInt("oq4_respawn")== (held>0?2:1));
        CHECK(gui.GetStateInt("oq4_respawn_in")== (held>0?(held+999)/1000:0));
    }
    player.minRespawnTime=gameLocal.time+3000;
    player.spectating=true;ProjectRespawn(&gui,&player,GAMEON);CHECK(gui.GetStateInt("oq4_respawn")==0);
    player.spectating=false;player.health=100;ProjectRespawn(&gui,&player,GAMEON);CHECK(gui.GetStateInt("oq4_respawn")==0);
    player.health=0;gameLocal.gameType=GTF_ELIMINATION;ProjectRespawn(&gui,&player,GAMEON);CHECK(gui.GetStateInt("oq4_respawn")==0);
    ProjectRespawn(&gui,&player,WARMUP);CHECK(gui.GetStateInt("oq4_respawn")==2);
    gameLocal.gameType=0;fixtureState.allow=false;ProjectRespawn(&gui,&player,GAMEON);CHECK(gui.GetStateInt("oq4_respawn")==0);
    fixtureState.allow=true;ProjectRespawn(&gui,&player,SUDDENDEATH);CHECK(gui.GetStateInt("oq4_respawn")==0);
    player.leader=true;ProjectRespawn(&gui,&player,SUDDENDEATH);CHECK(gui.GetStateInt("oq4_respawn")==2);
    player.leader=false;ProjectRespawn(&gui,&player,SUDDENDEATH,false,true);CHECK(gui.GetStateInt("oq4_respawn")==2);
    auto& arena=fixtureState.arenas[0];arena.players[0]=&player;
    ProjectRespawn(&gui,&player,GAMEON,true);CHECK(gui.GetStateInt("oq4_respawn")==2);
    arena.state=AS_DONE;ProjectRespawn(&gui,&player,GAMEON,true);CHECK(gui.GetStateInt("oq4_respawn")==0);
    arena.state=AS_ROUND;arena.players[0]=nullptr;ProjectRespawn(&gui,&player,GAMEON,true);CHECK(gui.GetStateInt("oq4_respawn")==0);
    player.arena=MAX_ARENAS;ProjectRespawn(&gui,&player,GAMEON,true);CHECK(gui.GetStateInt("oq4_respawn")==2);
    ProjectRespawn(&gui,&player,GAMEREVIEW);CHECK(gui.GetStateInt("oq4_respawn")==0);
    std::printf("MP HUD context/respawn projection: %d checks passed\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('use a project-local .tmp output')
    output.mkdir(parents=True, exist_ok=True)
    path = ROOT / 'src/mpgame/MultiplayerGame.cpp'
    source = path.read_text(encoding='utf-8-sig')
    context = function_body(source, 'void idMultiplayerGame::ProjectClientManagedMatchContext(')
    start = context.index('\t// Keep the full record')
    context = context[start:context.index('\n\tif ( projectedRevision != NULL ) {', start)]
    respawn = function_body(source, 'void idMultiplayerGame::PublishRemasteredHud(')
    start = respawn.index('\tbool mayRespawn = false;')
    respawn = respawn[start:respawn.index('\n\t// The HUD\'s looping motion', start)]
    cpp = SUPPORT.replace('@CONTEXT@', context).replace('@RESPAWN@', respawn)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    code, binary = output / 'behavior.cpp', output / 'behavior.exe'
    code.write_text(cpp, encoding='utf-8')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(code), '-o', str(binary)],
                           cwd=output, env=env, text=True, capture_output=True)
    (output / 'build.log').write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=output, env=env, text=True, capture_output=True)
    (output / 'run.log').write_text(run.stdout + run.stderr)
    (output / 'result.json').write_text(json.dumps({'passed': run.returncode == 0, 'output': run.stdout + run.stderr,
        'sources': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()},
        'projection_sha256': hashlib.sha256(code.read_bytes()).hexdigest()}, indent=2) + '\n')
    print(run.stdout + run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
