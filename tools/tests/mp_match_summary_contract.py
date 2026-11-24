#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute the real summary builder without inventing wins from managed scores."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_evidence_storage_contract import ROOT, _resolve_engine_root
from mp_match_flow_contract import function


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>
#define ID_INLINE inline
#include "idlib/containers/List.h"
#include "idlib/containers/Pair.h"
#include "game/mp/GameTypeIds.h"
#include "game/mp/match/MatchSession.h"
#include "game/mp/match/MatchView.h"
#include "game/mp/match/MatchRules.h"
#define BIT(x) (1 << (x))
struct idDict;
#include "game/mp/GameTypes.h"
template<class T> static T Min(T a, T b) { return std::min(a, b); }
static const int MAX_CLIENTS = 32, MAX_WEAPONS = 16;
static const int MP_PLAYER_MINFRAGS = -100, MP_PLAYER_MAXFRAGS = 999;
static const int C_COLOR_BLUE = 0, C_COLOR_RED = 1, C_COLOR_YELLOW = 2;
struct idVec4 { float values[4] = {}; float &operator[](int n) { return values[n]; } };
class idStr : public std::string {
public:
    using std::string::string; using std::string::operator=;
    void Clear() { clear(); } void Append(const char *value) { append(value); }
    int Length() const { return int(size()); }
    static int Icmp(const char *a, const char *b) {
        while (*a && *b && std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) { ++a; ++b; }
        return std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
    }
    template<class... T> static int snPrintf(char *out, int size, const char *format, T... values) {
        return std::snprintf(out, size, format, values...);
    }
    static idVec4 ColorForIndex(int) { return {}; }
};
struct idMath { static int ClampInt(int lo, int hi, int n) { return std::max(lo, std::min(hi, n)); } };
struct idDict {
    std::map<std::string, std::string> values;
    const char *GetString(const char *key) const {
        if (!std::strcmp(key, "ui_name")) return "Player";
        if (!std::strcmp(key, "ui_clan")) return "Clan";
        auto it = values.find(key); return it == values.end() ? "" : it->second.c_str();
    }
    void Set(const char *key, const char *value) { values[key] = value; }
    void SetInt(const char *key, int value) { values[key] = std::to_string(value); }
    void SetBool(const char *key, bool value) { SetInt(key, value ? 1 : 0); }
    void SetFloat(const char *key, float value) { values[key] = std::to_string(value); }
    bool GetBool(const char *key) const { return std::atoi(GetString(key)) != 0; }
};
enum { CVAR_GAME = 1, CVAR_SERVERINFO = 2, CVAR_ROM = 4, CVAR_BOOL = 8,
       CVAR_INTEGER = 16, CVAR_FLOAT = 32, PC_CVAR_ARCHIVE = 64, CVAR_ARCHIVE = 64 };
struct idCVar {
    std::string name, value; int flags;
    static std::vector<idCVar *> &Registry() { static std::vector<idCVar *> all; return all; }
    template<class... T> idCVar(const char *key, const char *initial, int mask, T...) : name(key), value(initial), flags(mask) {
        Registry().push_back(this);
    }
    void SetInteger(int number) { value = std::to_string(number); }
    void SetBool(bool enabled) { SetInteger(enabled ? 1 : 0); }
    void SetFloat(float number) { value = std::to_string(number); }
};
@CVARS@
struct CVarSystem {
    void SetCVarInteger(const char *name, int value) {
        for (auto *cvar : idCVar::Registry()) if (cvar->name == name) { cvar->SetInteger(value); return; }
        std::abort();
    }
    void SetCVarBool(const char *name, bool value) { SetCVarInteger(name, value ? 1 : 0); }
    idDict MoveCVarsToDict(int flags) const {
        idDict result;
        for (const auto *cvar : idCVar::Registry()) if ((cvar->flags & flags) != 0) result.Set(cvar->name.c_str(), cvar->value.c_str());
        return result;
    }
} cvarSystemValue, *cvarSystem = &cvarSystemValue;
@RULE_CORE@
struct idEntity { virtual ~idEntity() = default; virtual bool IsType(int) const { return false; } };
struct idPlayer : idEntity {
    int entityNumber = 0, team = 0, rank = -1;
    bool wantSpectate = false;
    idDict userInfo;
    bool IsType(int) const override { return true; }
    static int GetClassType() { return 1; }
    int GetRank() const { return rank; }
    void SetRank(int value) { rank = value; }
    const idDict *GetUserInfo() const { return &userInfo; }
};
struct rvDuelGameState {
    static int GetClassType() { return 2; }
    bool IsType(int type) const { return type == GetClassType(); }
    bool IsContender(int slot) const { return slot < 2; }
};
struct rvPlayerStat { int weaponShots[MAX_WEAPONS] = {}, weaponHits[MAX_WEAPONS] = {}; };
struct StatManager {
    rvPlayerStat stats;
    rvPlayerStat *GetPlayerStat(int slot) { return slot == 0 ? nullptr : &stats; }
} statManagerValue, *statManager = &statManagerValue;
struct Common {
    template<class... T> void DPrintf(const char *, T...) {}
    const char *GetLocalizedString(const char *key) {
        return !std::strcmp(key, "#str_41313") ? "%s wins!" : key;
    }
} commonValue, *common = &commonValue;
enum { AS_GENERAL_YOU_WIN = 1 };
char *va(const char *format, ...) {
    static char buffers[8][512]; static int index = 0;
    char *buffer = buffers[index++ % 8];
    va_list args; va_start(args, format); std::vsnprintf(buffer, 512, format, args); va_end(args);
    return buffer;
}
struct idUserInterface {
    std::map<std::string, std::string> strings;
    std::map<std::string, int> ints;
    std::vector<std::string> events;
    int changed = 0, redraws = 0;
    void HandleNamedEvent(const char *event) { events.push_back(event); }
    void SetStateString(const char *key, const char *value) { strings[key] = value; }
    void SetStateInt(const char *key, int value) { ints[key] = value; }
    void SetStateBool(const char *key, bool value) { ints[key] = value; }
    void SetStateVec4(const char *, const idVec4 &) {}
    void DeleteStateVar(const char *key) { strings.erase(key); }
    void StateChanged(int) { ++changed; }
    void Redraw(int) { ++redraws; }
};
@RANK_ENUM@
struct AcceptedModel {
    bool ready = false;
    uint64_t session = 0, revision = 0;
    mpMatchViewRecipient_t recipient = {};
    bool IsReady() const { return ready; }
    uint64_t SessionId() const { return session; }
    uint64_t ViewRevision() const { return revision; }
    const mpMatchViewRecipient_t &Recipient() const { return recipient; }
};
struct idMultiplayerGame {
    struct Score { int fragCount = 0, teamFragCount = 0, wins = 0; bool ingame = true; } playerState[MAX_CLIENTS];
    idList<rvPair<idPlayer *, int>> rankedPlayers;
    idList<idPlayer *> unrankedPlayers;
    rvDuelGameState *gameState = nullptr;
    int teamScore[TEAM_MAX] = {};
    bool campaign = false, clientMatchViewValid = false;
    mpMatchSession matchSession;
    mpCompetitiveRules matchRules;
    mpSessionView clientMatchView = {};
    AcceptedModel clientMatchControlModel;
    uint64_t clientSummaryAnnouncedSession = 0, clientSummaryAnnouncedResult = 0;
    std::vector<int> announcements;
    void ScheduleAnnouncerSound(int sound, int) { announcements.push_back(sound); }
    const mpMatchViewTerminalResult_t *ClientTerminalResult();
    int GetScoreForTeam(int team)const { return teamScore[team]; }
    bool UpdateManagedSummaryResult(idUserInterface *, bool);
    bool IsArenaCampaignMatch() const { return campaign; }
    bool IsManagedMatch() const;
    void MirrorCompetitiveRulesToLegacy();
    int GetScore(idPlayer *p) const { return playerState[p->entityNumber].fragCount; }
    int GetTeamScore(idPlayer *p) const { return playerState[p->entityNumber].teamFragCount; }
    int GetWins(idPlayer *p) const { return playerState[p->entityNumber].wins; }
    bool CanPlay(idPlayer *);
    bool IsRankedParticipant(idPlayer *);
    void UpdatePlayerRanks(playerRankMode_t = PRM_AUTO);
    const char *BuildSummaryListString(idPlayer *, int);
    void UpdateSummaryBoard(idUserInterface *);
};
struct GameLocal {
    int numClients = 4, localClientNum = 0, time = 100;
    gameType_t gameType = GAME_TDM;
    bool teamGame = true, isServer = true, isClient = false;
    idDict serverInfo;
    idEntity *entities[MAX_CLIENTS] = {};
    bool IsTeamGame() const { return teamGame; }
    idPlayer *GetLocalPlayer() const { return localClientNum < 0 ? nullptr : static_cast<idPlayer *>(entities[localClientNum]); }
    template<class... T> void Error(const char *, T...) { std::abort(); }
} gameLocal;
@PRODUCTION@
static void Review(idMultiplayerGame &mp, mpMatchTransitionReason_t reason) {
    auto &session = mp.matchSession;
    assert(session.Reset(77, mpMatchEngineTime::FromMilliseconds(0)));
    mpMatchReadinessPolicy policy;
    policy.policy = MP_MATCH_READY_INDIVIDUAL;
    policy.minimumActiveHumans = 2;
    policy.readyThresholdBasisPoints = 10000;
    assert(!session.ConfigureReadiness(policy, session.GetSessionRevision()).WasRejected());
    assert(!session.TransitionPhase(WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED,
        mpParticipantId::Invalid(), session.GetSessionRevision()).WasRejected());
    for (int slot = 0; slot < 2; ++slot) {
        mpParticipantId participant;
        assert(!session.BindParticipant(slot, true, MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),
            session.GetSessionRevision(), participant).WasRejected());
        assert(!session.SetParticipantActive(participant, true, session.GetSessionRevision()).WasRejected());
        assert(!session.SetParticipantReady(participant, true, session.GetSessionRevision()).WasRejected());
    }
    assert(!session.BeginCountdown(1, 42, MP_MATCH_TRANSITION_REFEREE_FORCE_READY,
        mpParticipantId::Invalid(), session.GetSessionRevision()).WasRejected());
    assert(!session.TransitionPhase(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE,
        mpParticipantId::Invalid(), session.GetSessionRevision()).WasRejected());
    assert(!session.TransitionPhase(GAMEREVIEW, reason, mpParticipantId::Invalid(),
        session.GetSessionRevision()).WasRejected());
}
static void Rules(idMultiplayerGame &mp, bool managed) {
    mpMatchRulesDraft draft; mpRuleValidationFailure_t failure;
    const auto profile = !managed ? MP_MATCH_PROFILE_CASUAL :
        gameLocal.gameType == GAME_DUEL ? MP_MATCH_PROFILE_COMPETITIVE_DUEL : MP_MATCH_PROFILE_COMPETITIVE_TDM;
    assert(mp.matchRules.BeginDraftFromProfile(profile, gameLocal.gameType, draft, failure));
    assert(mp.matchRules.Commit(draft, {}, MP_RULES_OPEN_FOR_COMMIT).Succeeded());
}
static void Accepted(idMultiplayerGame &mp, bool managed) {
    mp.clientMatchView = {};
    auto &state = mp.clientMatchView.publicState;
    state.sessionId = 77; state.viewRevision = 17;
    state.recipient.slot = gameLocal.localClientNum;
    state.recipient.participantId = 1; state.recipient.bindingGeneration = 1;
    state.committedRules.present = true;
    state.committedRules.valueCount = 1;
    state.committedRules.values[0].fieldId = MP_RULE_MANAGED_MATCH;
    state.committedRules.values[0].type = MP_MATCH_VIEW_RULE_BOOL;
    state.committedRules.values[0].value = managed ? 1 : 0;
    mp.clientMatchViewValid = true;
    auto &model = mp.clientMatchControlModel;
    model.ready = true; model.session = state.sessionId; model.revision = state.viewRevision;
    model.recipient = state.recipient;
}
static void ManagedIdentityBoundaries(idMultiplayerGame &mp) {
    Review(mp, MP_MATCH_TRANSITION_MATCH_ABORTED);
    gameLocal.isServer = true; gameLocal.isClient = false;
    // Execute the production mirror and rebuild from registered SERVERINFO
    // CVars, just as Session.cpp does before SetServerInfo replaces the dict.
    for (bool managed : {true, false, true}) {
        Rules(mp, managed);
        for (bool campaign : {false, true, false}) {
            mp.campaign = campaign;
            mp.MirrorCompetitiveRulesToLegacy();
            assert(gameLocal.serverInfo.GetBool("si_managedMatch") == (managed && !campaign));
            gameLocal.serverInfo.SetBool("temporary_unregistered_marker", true);
            gameLocal.serverInfo = cvarSystem->MoveCVarsToDict(CVAR_SERVERINFO);
            assert(!gameLocal.serverInfo.GetBool("temporary_unregistered_marker"));
            assert(gameLocal.serverInfo.GetBool("si_managedMatch") == (managed && !campaign));
            mp.clientMatchViewValid = false;
            gameLocal.isServer = false; gameLocal.isClient = true;
            assert(mp.IsManagedMatch() == (managed && !campaign)); // Initial handshake.
            gameLocal.isServer = true; gameLocal.isClient = false;
        }
    }
    assert((si_managedMatch.flags & (CVAR_ROM | CVAR_SERVERINFO | CVAR_BOOL)) ==
        (CVAR_ROM | CVAR_SERVERINFO | CVAR_BOOL));
    gameLocal.isServer = false; gameLocal.isClient = true;
    for (bool managed : {true, false}) {
        Accepted(mp, managed);
        gameLocal.serverInfo.SetBool("si_managedMatch", !managed);
        assert(mp.IsManagedMatch() == managed); // Committed view overrides a lagging bit both ways.
    }
    for (int invalid = 0; invalid < 12; ++invalid) {
        Accepted(mp, true); gameLocal.serverInfo.SetBool("si_managedMatch", false);
        auto &state = mp.clientMatchView.publicState;
        switch (invalid) {
            case 0: mp.clientMatchViewValid = false; break;
            case 1: mp.clientMatchControlModel.ready = false; break;
            case 2: state.sessionId = 0; break;
            case 3: ++state.sessionId; break;
            case 4: ++state.viewRevision; break;
            case 5: ++state.recipient.slot; break;
            case 6: ++state.recipient.bindingGeneration; break;
            case 7: ++state.recipient.participantId; break;
            case 8: state.committedRules.present = false; break;
            case 9: state.committedRules.valueCount = 0; break;
            case 10: state.committedRules.values[0].type = MP_MATCH_VIEW_RULE_INTEGER; break;
            case 11: ++mp.clientMatchControlModel.recipient.slot; break;
        }
        assert(!mp.IsManagedMatch());
    }
    Accepted(mp, true); gameLocal.serverInfo.SetBool("si_managedMatch", true);
    mp.campaign = true; assert(!mp.IsManagedMatch()); mp.campaign = false;
    gameLocal.isServer = true; gameLocal.isClient = false;
    Accepted(mp, false); Rules(mp, true); assert(mp.IsManagedMatch());
    Rules(mp, false); Accepted(mp, true); assert(!mp.IsManagedMatch());
}
static void TerminalPresentation(idMultiplayerGame &mp) {
    idUserInterface gui;
    gameLocal.isServer = false; gameLocal.isClient = true;
    gameLocal.localClientNum = 0; gameLocal.teamGame = true;
    Accepted(mp, true);
    auto &state = mp.clientMatchView.publicState;
    auto &result = state.terminalResult;
    result.winnerSide = -1;
    assert(mp.UpdateManagedSummaryResult(&gui, true));
    assert(gui.strings["summary_result_text"] == "#str_42872");
    assert(mp.announcements.empty());
    result.resultRevision = 12;
    state.lifecycle.phase = GAMEREVIEW;
    for (int outcome : {MP_MATCH_VIEW_RESULT_ABORTED, MP_MATCH_VIEW_RESULT_DRAW}) {
        result.outcome = static_cast<mpMatchViewResultOutcome_t>(outcome);
        mp.UpdateManagedSummaryResult(&gui, true);
        assert(gui.events.back() == "managed_summary");
        assert(gui.strings["summary_result_text"] ==
            (outcome == MP_MATCH_VIEW_RESULT_ABORTED ? "#str_42870" : "#str_42871"));
        assert(mp.announcements.empty());
        ++result.resultRevision;
    }
    result.outcome = MP_MATCH_VIEW_RESULT_FORFEIT;
    for (int winner : {TEAM_MARINE, TEAM_STROGG}) {
        result.winnerSide = winner;
        mp.teamScore[winner] = -1; mp.teamScore[1-winner] = 20;
        mp.UpdateManagedSummaryResult(&gui, true);
        assert(gui.events.back() == (winner == TEAM_MARINE ? "managed_marine_wins" : "managed_strogg_wins"));
        assert(gui.strings["summary_result_text"] ==
            (winner == TEAM_MARINE ? "#str_201012\n#str_41315" : "#str_201013\n#str_41315"));
        assert(mp.announcements.empty());
        ++result.resultRevision;
    }
    // Frozen individual identity wins even if today's roster, team scores and
    // mode differ. A reused recipient binding cannot consume the old view.
    result.winnerSide = -1; result.winnerParticipantId = 44;
    std::memcpy(result.winnerName, "Departed winner", 16); result.winnerNameLength = 15;
    mp.UpdateManagedSummaryResult(&gui, true);
    assert(gui.strings["summary_result_text"] == "Departed winner wins!\n#str_41315");
    assert(gui.events.back() == "managed_summary" && mp.announcements.empty());
    ++result.resultRevision;
    result.winnerParticipantId = state.recipient.participantId;
    mp.UpdateManagedSummaryResult(&gui, true);
    mp.UpdateManagedSummaryResult(&gui, true);
    assert(mp.announcements.size() == 1 && mp.announcements[0] == AS_GENERAL_YOU_WIN);
    state.lifecycle.phase = WARMUP; ++result.resultRevision;
    mp.UpdateManagedSummaryResult(&gui, true);
    assert(mp.announcements.size() == 1);
    ++mp.clientMatchControlModel.recipient.bindingGeneration;
    mp.UpdateManagedSummaryResult(&gui, true);
    assert(gui.strings["summary_result_text"] != "Departed winner wins!\n#str_41315");
    mp.announcements.clear();
    mp.clientSummaryAnnouncedSession = mp.clientSummaryAnnouncedResult = 0;
}
int main() {
    idPlayer players[4]; idMultiplayerGame mp; idUserInterface gui;
    for (int slot = 0; slot < 4; ++slot) {
        players[slot].entityNumber = slot; players[slot].team = slot % 2;
        gameLocal.entities[slot] = &players[slot];
        mp.playerState[slot].fragCount = 9 - slot * 3;
    }
    ManagedIdentityBoundaries(mp);
    TerminalPresentation(mp);
    // The same GUI is reused across normal, aborted and forfeited reviews.
    // Team score ordering must not become a claimed terminal winner.
    for (bool remote : {false, true}) {
        gameLocal.isClient = remote; gameLocal.isServer = !remote;
        for (auto reason : {MP_MATCH_TRANSITION_MATCH_ABORTED, MP_MATCH_TRANSITION_FORFEIT,
                            MP_MATCH_TRANSITION_LIMIT_REACHED}) {
            for (int leader : {-1, int(TEAM_MARINE), int(TEAM_STROGG)}) {
                Review(mp, reason);
                mp.teamScore[0] = leader == 0 ? 12 : 0;
                mp.teamScore[1] = leader == 1 ? 12 : 0;
                for (bool managed : {false, true, false, true}) {
                    Rules(mp, managed);
                    Accepted(mp, managed);
                    // Reproduce the remote failure: legacy metadata claims
                    // the opposite while its accepted committed rules are current.
                    gameLocal.serverInfo.SetBool("si_managedMatch", !managed);
                    mp.UpdateSummaryBoard(&gui);
                    assert(gui.events.back() == (managed || leader < 0 ? "managed_summary" :
                        leader == TEAM_MARINE ? "marine_wins" : "strogg_wins"));
                    assert(mp.teamScore[0] == (leader == 0 ? 12 : 0));
                    assert(mp.teamScore[1] == (leader == 1 ? 12 : 0));
                    assert(mp.rankedPlayers.Num() == 4);
                    for (int slot = 0; slot < 4; ++slot) {
                        assert(mp.rankedPlayers[slot].First() == &players[slot]);
                        assert(mp.rankedPlayers[slot].Second() == 9 - slot * 3);
                    }
                    assert(gui.strings["summary_marine_names_item_0"] == "1. Player\tClan\t9\t\t");
                    assert(gui.strings["summary_strogg_names_item_0"] == "2. Player\tClan\t6\t\t");
                    assert(gui.strings["summary_marine_names_item_1"] == "3. Player\tClan\t3\t\t");
                    assert(gui.strings["summary_strogg_names_item_1"] == "4. Player\tClan\t0\t\t");
                }
            }
        }
    }
    // Duel remains a raw-score list. This team-summary fix must not change
    // contender eligibility, reorder a leading forfeiter or invent a winner.
    gameLocal.teamGame = false; gameLocal.gameType = GAME_DUEL;
    rvDuelGameState duel; mp.gameState = &duel;
    for (int forfeiter : {0, 1}) {
        Review(mp, MP_MATCH_TRANSITION_FORFEIT);
        mp.playerState[forfeiter].fragCount = 10;
        mp.playerState[1-forfeiter].fragCount = -1;
        const auto eventCount = gui.events.size();
        mp.UpdateSummaryBoard(&gui);
        assert(gui.events.size() == eventCount && mp.rankedPlayers.Num() == 2);
        assert(mp.rankedPlayers[0].First() == &players[forfeiter]);
        assert(gui.strings["summary_names_item_0"] == "1. Player\tClan\t10\t\t");
        assert(gui.strings["summary_names_item_1"] == "2. Player\tClan\t-1\t\t");
    }
    const int priorRedraws = gui.redraws;
    gameLocal.localClientNum = -1; mp.UpdateSummaryBoard(&gui);
    assert(gui.redraws == priorRedraws);
    std::puts("managed identity, terminal outcome, once-only audio and unchanged raw rankings: PASS");
}
'''


def verify_gui() -> None:
    engine = _resolve_engine_root()
    gui = engine / "content/baseoq4/pak0/guis/summary.gui"
    if not gui.is_file():
        print("mp_match_summary_contract: companion engine GUI check skipped (checkout unavailable)")
        return
    body = function(gui.read_text(encoding="utf-8"), "onNamedEvent managed_summary")
    assignments = dict(re.findall(r'set\s+"([^"]+)"\s+"([^"]+)"\s*;', body))
    assert assignments == {
        "summ_marine_teamname::text": "#str_200197",
        "summ_strogg_teamname::text": "#str_200199",
        "summ_marine::rect": "93,109,448,118",
        "summ_strogg::rect": "93,224,448,118",
    }
    strings = (engine / "content/baseoq4/pak0/strings/english_guis.lang").read_text(encoding="utf-8")
    assert re.search(r'"#str_200197"\s+"MARINES"', strings)
    assert re.search(r'"#str_200199"\s+"STROGG"', strings)


def main() -> None:
    verify_gui()
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/game/MultiplayerGame.h").read_text(encoding="utf-8")
    enum_end = header.index("} playerRankMode_t;") + len("} playerRankMode_t;")
    enum_start = header.rindex("typedef enum", 0, enum_end)
    production = "\n".join(function(source, signature) for signature in (
        "const mpMatchViewTerminalResult_t *idMultiplayerGame::ClientTerminalResult(",
        "int ComparePlayersByScore(", "bool idMultiplayerGame::CanPlay(",
        "bool idMultiplayerGame::IsRankedParticipant(", "void idMultiplayerGame::UpdatePlayerRanks(",
        "const char* idMultiplayerGame::BuildSummaryListString(", "void idMultiplayerGame::UpdateSummaryBoard(",
        "bool idMultiplayerGame::UpdateManagedSummaryResult(",
        "bool idMultiplayerGame::IsManagedMatch(", "void idMultiplayerGame::MirrorCompetitiveRulesToLegacy(",
    ))
    mirror = function(source, "void idMultiplayerGame::MirrorCompetitiveRulesToLegacy(")
    cvar_names = set(re.findall(r'(si_\w+)\.(?:SetBool|SetInteger|SetFloat)', mirror))
    cvar_names.update(re.findall(r'MIRROR_MATCH_(?:BOOL|INT)\(\s*(si_\w+)', mirror))
    cvar_names.update(re.findall(r'SetCVar(?:Bool|Integer)\(\s*"(si_\w+)"', mirror))
    cvar_source = (ROOT / "src/game/gamesys/SysCvar.cpp").read_text(encoding="utf-8")
    cvar_declarations = []
    for name in sorted(cvar_names):
        declaration = re.search(r'^idCVar\s+' + name + r'\([^\n]+\);', cvar_source, re.MULTILINE)
        assert declaration, name
        cvar_declarations.append(declaration.group(0))
    rules = (ROOT / "src/game/mp/match/MatchRules.cpp").read_text(encoding="utf-8")
    rules = re.sub(r'^#(?:include|pragma).*$', '', rules, flags=re.MULTILINE)
    types = (ROOT / "src/game/mp/GameTypes.cpp").read_text(encoding="utf-8")
    begin = types.index("static const mpGameTypeInfo_t mpGameTypeInfoTable[]")
    end = types.index("\n/*", types.index("const int mpNumGameTypeInfo", begin))
    definitions = types[begin:end] + "\n".join(function(types, signature) for signature in (
        "const mpGameTypeInfo_t *MPGameType(", "int MPGameTypeFlags(",
        "const char *MPGameTypeName(", "bool MPGameTypeIsSelectable(", "bool MPGameTypeHasAny(",
    ))
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for summary regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-summary-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp = path / "summary.cpp"
        exe = path / ("summary.exe" if os.name == "nt" else "summary")
        cpp.write_text(HARNESS.replace("@RANK_ENUM@", header[enum_start:enum_end])
                       .replace("@CVARS@", "\n".join(cvar_declarations))
                       .replace("@RULE_CORE@", definitions + rules)
                       .replace("@PRODUCTION@", production), encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-DMP_MATCH_SESSION_STANDALONE_TEST",
                   f"-I{ROOT / 'src'}", str(cpp), str(ROOT / "src/game/mp/match/MatchSession.cpp"), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_summary_contract: PASS")


if __name__ == "__main__":
    main()
