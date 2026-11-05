#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Live adapter contracts for atomic series, report, and evidence publication."""

from __future__ import annotations

from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MULTIPLAYER_HEADER = ROOT / "src/mpgame/MultiplayerGame.h"
MULTIPLAYER_SOURCE = ROOT / "src/mpgame/MultiplayerGame.cpp"
OPERATIONS_SOURCE = ROOT / "src/mpgame/mp/match/MatchOperations.cpp"
RECOVERY_HEADER = ROOT / "src/mpgame/mp/match/MatchSeriesRecovery.h"
RECOVERY_SOURCE = ROOT / "src/mpgame/mp/match/MatchSeriesRecovery.cpp"


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def require_order(text: str, tokens: tuple[str, ...], context: str) -> None:
    cursor = -1
    for token in tokens:
        position = text.find(token, cursor + 1)
        if position < 0:
            raise AssertionError(f"missing {token!r} in {context}")
        if position <= cursor:
            raise AssertionError(f"out-of-order {token!r} in {context}")
        cursor = position


def region(text: str, start: str, end: str, context: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise AssertionError(f"missing start marker {start!r} in {context}")
    finish = text.find(end, begin + len(start))
    if finish < 0:
        raise AssertionError(f"missing end marker {end!r} in {context}")
    return text[begin:finish]


def function(text: str, signature: str, context: str) -> str:
    """Extract one C++ definition without depending on the next function name."""

    begin = text.find(signature)
    if begin < 0:
        raise AssertionError(f"missing function {signature!r} in {context}")
    opening = text.find("{", begin + len(signature))
    if opening < 0:
        raise AssertionError(f"missing function body for {signature!r} in {context}")
    depth = 0
    for index in range(opening, len(text)):
        character = text[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return text[begin : index + 1]
    raise AssertionError(f"unterminated function {signature!r} in {context}")


def member_contracts(header: str, source: str) -> None:
    for token in (
        '#include "mp/match/MatchSeriesReport.h"',
        '#include "mp/match/MatchSeriesReportStorage.h"',
        "mpCompetitionSeriesReport matchSeriesReport;",
        "mpSeriesReportStorageWorkspace matchSeriesReportWorkspace;",
        "bool\t\t\tmatchSeriesAwaitingMapSession;",
        "PersistCompetitionSeriesCandidate(",
        "CommitCompetitionSeriesMapEvidence(",
    ):
        require(header, token, "multiplayer series/report ownership")
    require(
        source,
        '#include "mp/match/MatchSeriesReportFileSystem.h"',
        "live report filesystem adapter",
    )


def recovery_contracts(
    multiplayer: str, recovery_header: str, recovery_source: str
) -> None:
    require(
        recovery_header,
        "MP_SERIES_RECOVERY_SCHEMA_VERSION = 4",
        "unified checkpoint schema",
    )
    require(
        recovery_header,
        "MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION = 3",
        "schema-3 paired checkpoint compatibility",
    )
    paired_core = function(
        recovery_source,
        "bool MPMatchSeriesRecoveryRestoreCores",
        "paired recovery core",
    )
    require_order(
        paired_core,
        (
            "if ( !record.hasReport )",
            "candidateSeries.RestoreRecoveryState( record.series )",
            "candidateReport.RestoreCheckpointState( record.report )",
            "ValidateReportAgainstSeries( candidateSeries, record.seriesId,",
            "series = candidateSeries;",
            "report = candidateReport;",
        ),
        "paired recovery core",
    )

    restore = function(
        multiplayer,
        "bool idMultiplayerGame::RestoreCompetitionSeriesIfRequested",
        "live paired recovery",
    )
    require_order(
        restore,
        (
            "MPMatchSeriesRecoveryLoadFileSystem(",
            "if ( !record.hasReport )",
            "MPMatchSeriesRecoveryRestoreCores( record, candidate, reportCandidate,",
            "reportCandidate.GetIdentity().rulesDigest",
            "matchSeries = candidate;",
            "matchSeriesReport = reportCandidate;",
            "matchSeriesId = record.seriesId;",
            "matchSeriesLinkedSessionId = record.linkedSessionId;",
        ),
        "live paired recovery",
    )
    if "MPMatchSeriesRecoveryRestore(" in restore:
        raise AssertionError("live recovery retains the unpaired legacy restore path")


def initial_publish_contract(multiplayer: str) -> None:
    configure = region(
        multiplayer,
        "if ( kind == MP_OPERATION_CONTINUATION_SERIES_CONFIGURE_PROFILE )",
        "if ( kind == MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP )",
        "series configuration continuation",
    )
    require_order(
        configure,
        (
            "mpCompetitionSeriesReport reportCandidate;",
            "InitializeCompetitionSeriesReport( candidate, newSeriesId,",
            "PersistCompetitionSeriesCandidate( candidate, reportCandidate,",
            "matchSeries = candidate;",
            "matchSeriesReport = reportCandidate;",
            "matchSeriesId = newSeriesId;",
        ),
        "series configuration transaction",
    )


def map_session_handoff_contracts(multiplayer: str) -> None:
    constructor = function(
        multiplayer,
        "idMultiplayerGame::idMultiplayerGame()",
        "map-session handoff initialization",
    )
    require(
        constructor,
        "matchSeriesAwaitingMapSession = false;",
        "map-session handoff initialization",
    )

    restore = function(
        multiplayer,
        "bool idMultiplayerGame::RestoreCompetitionSeriesIfRequested",
        "restored map-session handoff",
    )
    require_order(
        restore,
        (
            "matchSeries = candidate;",
            "matchSeriesReport = reportCandidate;",
            "matchSeriesAwaitingMapSession =",
            "matchSeries.GetState() == MP_SERIES_MAP_ACTIVE;",
        ),
        "restored MAP_ACTIVE handoff",
    )

    schedule = function(
        multiplayer,
        "bool idMultiplayerGame::ScheduleCompetitionSeriesMap",
        "scheduled map-session handoff",
    )
    require_order(
        schedule,
        (
            "candidate.BeginMap( mapToken,",
            "PersistCompetitionSeriesCandidate( candidate, matchSeriesReport,",
            "matchSeries = candidate;",
            "matchSeriesAwaitingMapSession = true;",
            'gameLocal.sessionCommand = "nextMap";',
        ),
        "checkpointed BeginMap handoff",
    )

    finalizer = function(
        multiplayer,
        "bool idMultiplayerGame::FinalizeMatchEvidence",
        "pre-bind evidence finalizer",
    )
    require_order(
        finalizer,
        (
            "matchSeries.GetState() == MP_SERIES_MAP_ACTIVE",
            "!matchSeriesAwaitingMapSession",
            "!CommitCompetitionSeriesMapEvidence( evidenceStorage )",
        ),
        "pre-bind series commit suppression",
    )

    begin_session = function(
        multiplayer,
        "bool idMultiplayerGame::BeginMatchSession",
        "successful runtime session binding",
    )
    require_order(
        begin_session,
        (
            "matchSeries.GetState() == MP_SERIES_MAP_ACTIVE",
            "PersistCompetitionSeriesCandidate( matchSeries, matchSeriesReport,",
            "matchSeriesLinkedSessionId = matchSession.GetSessionId();",
            "matchSeriesAwaitingMapSession = false;",
        ),
        "successful runtime session binding",
    )

    rollback_functions = (
        "void idMultiplayerGame::ProcessPassedMatchProposals",
        "void idMultiplayerGame::ServerReceiveMatchOperation",
        "bool idMultiplayerGame::ExecuteTrustedLocalMatchOperation",
    )
    for signature in rollback_functions:
        rollback = function(multiplayer, signature, "operation rollback snapshot")
        require_order(
            rollback,
            (
                "const bool awaitingMapSessionBeforeExecution =",
                "matchSeriesAwaitingMapSession;",
                "execution.outcome == MP_OPERATION_REJECTED",
                "matchSeries = seriesBeforeExecution;",
                "matchSeriesReport = reportBeforeExecution;",
                "matchSeriesLinkedSessionId = linkedSessionBeforeExecution;",
                "matchSeriesAwaitingMapSession = awaitingMapSessionBeforeExecution;",
            ),
            f"{signature} rollback snapshot",
        )

    # A MAP_ACTIVE selection whose handoff never reached a session is released
    # as an aborted attempt with no artifacts, so Advance replays the map.
    abort = function(
        multiplayer,
        "bool idMultiplayerGame::AbortUnstartedCompetitionSeriesMap",
        "unstarted series map release",
    )
    require_order(
        abort,
        (
            "series.GetState() != MP_SERIES_MAP_ACTIVE",
            "MP_SERIES_MAP_ABORTED, MP_SERIES_SIDE_NONE, 0, 0, linkedSessionId,",
            "mapInput.outcome = MP_SERIES_REPORT_MAP_ABORTED;",
            "MP_MATCH_TRANSITION_MAP_SHUTDOWN",
            "MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED",
            "report.AppendMapResult( mapInput )",
        ),
        "unstarted series map release",
    )
    require_order(
        begin_session,
        (
            "matchSeries.GetState() == MP_SERIES_MAP_ACTIVE && matchSeriesAwaitingMapSession &&",
            "!CompetitionSeriesMapMatchesRuntime( matchSeries, gameLocal.gameType,",
            "AbortUnstartedCompetitionSeriesMap( seriesCandidate, reportCandidate,",
            "PersistCompetitionSeriesCandidate( seriesCandidate, reportCandidate,",
            "matchSeries = seriesCandidate;",
            "matchSeriesAwaitingMapSession = false;",
            "PersistCompetitionSeriesCandidate( matchSeries, matchSeriesReport,",
        ),
        "failed handoff released before session binding",
    )
    require_order(
        restore,
        (
            "const bool unstartedActiveMap = candidate.GetState() == MP_SERIES_MAP_ACTIVE &&",
            "candidate.GetState() == MP_SERIES_MAP_COMPLETE &&",
            "matchRules.Committed().Digest()",
            "AbortUnstartedCompetitionSeriesMap( candidate, reportCandidate,",
            "PersistCompetitionSeriesCandidate( candidate, reportCandidate,",
            "matchSeries = candidate;",
        ),
        "restored unstarted map released as an aborted attempt",
    )


def evidence_commit_contracts(multiplayer: str) -> None:
    effects = function(
        multiplayer,
        "bool idMultiplayerGame::ApplyCommittedMatchPhaseEffects",
        "committed phase effects",
    )
    require(
        effects,
        "RecordMatchEvidenceResult( transition.reason, transition.authorizer,",
        "committed phase result journal",
    )
    for forbidden in (
        "CommitCompetitionSeriesMapEvidence(",
        ".CommitMapResult(",
        "matchSeries =",
        "matchSeriesReport =",
    ):
        if forbidden in effects:
            raise AssertionError(
                "phase effects bypass evidence sealing with " f"{forbidden!r}"
            )

    finalizer = function(
        multiplayer,
        "bool idMultiplayerGame::FinalizeMatchEvidence",
        "evidence finalizer",
    )
    require_order(
        finalizer,
        (
            "RecordMatchEvidenceFinalStats();",
            "StopMatchMVD(",
            "PersistMatchEvidence( &evidenceStorage )",
            "CommitCompetitionSeriesMapEvidence( evidenceStorage )",
            "return false;",
            "matchEvidenceFinalized = true;",
        ),
        "evidence sealing boundary",
    )
    failed_commit = finalizer.index(
        "CommitCompetitionSeriesMapEvidence( evidenceStorage )"
    )
    finalized = finalizer.index("matchEvidenceFinalized = true;")
    if "matchEvidenceFinalized = true;" in finalizer[:failed_commit] or not (
        failed_commit < finalized
    ):
        raise AssertionError(
            "a failed paired checkpoint can still mark match evidence finalized"
        )

    commit = function(
        multiplayer,
        "bool idMultiplayerGame::CommitCompetitionSeriesMapEvidence",
        "sealed map publication",
    )
    require_order(
        commit,
        (
            "mpCompetitionSeries seriesCandidate = matchSeries;",
            "seriesCandidate.CommitMapResult(",
            "mpCompetitionSeriesReport reportCandidate = matchSeriesReport;",
            "reportCandidate.AppendMapResult(",
            "PersistCompetitionSeriesCandidate( seriesCandidate, reportCandidate,",
            "matchSeries = seriesCandidate;",
            "matchSeriesReport = reportCandidate;",
            "matchSeriesLinkedSessionId = matchSession.GetSessionId();",
            "matchSeriesAwaitingMapSession = false;",
        ),
        "sealed map candidate checkpoint",
    )
    checkpoint = commit.index(
        "PersistCompetitionSeriesCandidate( seriesCandidate, reportCandidate,"
    )
    if "matchSeries =" in commit[:checkpoint] or "matchSeriesReport =" in commit[:checkpoint]:
        raise AssertionError("series/report state is published before its checkpoint")


def terminal_report_contract(multiplayer: str) -> None:
    terminal = function(
        multiplayer,
        "bool idMultiplayerGame::FinalizeCompetitionSeriesReport",
        "terminal report transaction",
    )
    require_order(
        terminal,
        (
            "report.Finalize( finalInput )",
            "MPMatchSeriesReportStoragePersist(",
            "PersistCompetitionSeriesCandidate( series, report, matchSeriesId,",
        ),
        "terminal report JSON and checkpoint ordering",
    )
    require(
        terminal,
        "MP_EVIDENCE_OUTPUT_SERIES_REPORT",
        "typed terminal-report failure evidence",
    )
    require(
        terminal,
        "MP_EVIDENCE_OUTPUT_SERIES_RECOVERY",
        "typed terminal-checkpoint failure evidence",
    )


def mutation_guard_contracts(operations: str) -> None:
    series_owner = function(
        operations,
        "static bool SeriesOwnsCommittedRules",
        "series-owned rules guard",
    )
    for terminal in (
        "state != MP_SERIES_DISABLED",
        "state != MP_SERIES_COMPLETE",
        "state != MP_SERIES_CANCELLED",
    ):
        require(series_owner, terminal, "series-owned rules guard")

    cancellation = region(
        operations,
        "case MP_MATCH_OP_SERIES_CANCEL:",
        "case MP_MATCH_OP_SERIES_ADVANCE:",
        "active-map cancellation guard",
    )
    require_order(
        cancellation,
        (
            "series.GetState() == MP_SERIES_MAP_ACTIVE",
            "Reject( MP_OPERATION_REASON_SERIES_STATE,",
            "series.Cancel(",
        ),
        "active-map cancellation guard",
    )

    select_profile = region(
        operations,
        "case MP_MATCH_OP_RULES_SELECT_PROFILE:",
        "case MP_MATCH_OP_RULES_STAGE_FIELD:",
        "series rules profile guard",
    )
    stage_field = region(
        operations,
        "case MP_MATCH_OP_RULES_STAGE_FIELD:",
        "case MP_MATCH_OP_RULES_COMMIT:",
        "series rules field guard",
    )
    commit_rules = region(
        operations,
        "case MP_MATCH_OP_RULES_COMMIT:",
        "case MP_MATCH_OP_RULES_DISCARD:",
        "series rules commit guard",
    )
    for scoped, context in (
        (select_profile, "series rules profile guard"),
        (stage_field, "series rules field guard"),
        (commit_rules, "series rules commit guard"),
    ):
        require(scoped, "SeriesOwnsCommittedRules( series )", context)
        require(scoped, "MP_OPERATION_REASON_RULE_STATE", context)


def artifact_status_contract(multiplayer: str) -> None:
    commit = function(
        multiplayer,
        "bool idMultiplayerGame::CommitCompetitionSeriesMapEvidence",
        "typed map artifact projection",
    )
    evidence = region(
        commit,
        "mpSeriesReportArtifactInput &evidenceArtifact",
        "mpSeriesReportArtifactInput &mvdArtifact",
        "evidence artifact status",
    )
    mvd = function(
        multiplayer,
        "void idMultiplayerGame::ProjectMatchMVDReportArtifact",
        "durable MVD artifact projection",
    )
    for scoped, kind, context in (
        (evidence, "MP_SERIES_REPORT_ARTIFACT_EVIDENCE", "evidence artifact status"),
        (mvd, "MP_SERIES_REPORT_ARTIFACT_MVD", "MVD artifact status"),
    ):
        for token in (
            kind,
            "MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED",
            "MP_SERIES_REPORT_ARTIFACT_AVAILABLE",
            "MP_SERIES_REPORT_ARTIFACT_FAILED",
            "MPMatchSeriesReportIsSafeArtifactQPath(",
        ):
            require(scoped, token, context)
    for token in (
        "MP_SERIES_REPORT_ARTIFACT_PENDING",
        "ServerCopyMVDRecordingResult",
        "MatchMVDResultForFinalQPath",
        "matchMVDOperatorOwnedBySession",
    ):
        require(mvd, token, "durable MVD artifact status")
    if "!networkSystem->ServerIsMVDRecording()" in mvd:
        raise AssertionError("MVD availability must not be inferred from idle state")


def map_shutdown_contract(multiplayer: str) -> None:
    """Execute the actual teardown with map/entity lifetime checked at sealing."""
    local = (ROOT / "src/mpgame/Game_local.cpp").read_text(encoding="utf-8")
    shutdown = function(local, "void idGameLocal::MapShutdown(", "map teardown")
    prepare = function(multiplayer, "void idMultiplayerGame::PrepareForMapShutdown(", "map evidence lifetime")
    harness = r'''
#include <cassert>
#include <string>
#include <cstdio>
enum { GAMESTATE_ACTIVE, GAMESTATE_SHUTDOWN, GAMESTATE_NOMAP, MAX_CLIENTS=32 };
struct Passive {
    void OnMapShutdown(){} void ShutdownSpecialEffects(){} void ShutdownPlaybacks(){}
    void Shutdown(){} void ShutdownInstances(){} void Clear(){} void Restart(){}
    void PurgeModels(){} void FreeMap(const char*){}
} passive, botManager, gameDebug, gameLogLocal, pvs, clip, instancesEntityIndexWatermarks,
  program, *soundSystem=&passive, *renderSystem=&passive, *gameEdit=&passive,
  *iconManager=&passive, *collisionModelManager=&passive;
struct idEvent { static void ClearEventList(){} };
struct idClipModel { static void ClearTraceModelCache(){} };
// This match-evidence fixture has no active save restore. Ownership and
// attachment teardown are exercised by savegame_restore_cleanup.py.
struct idRestoreGame { static void PrepareMapShutdown(){} static void DeleteOrphanedObjects(){} };
struct idSaveGame { static void ClearDictionaryCache(){} };
struct MapName : std::string { using std::string::operator=; void Clear(){clear();} };
struct idMultiplayerGame {
    int seals=0; bool succeeds=true;
    bool FinalizeMatchEvidence(bool);
    void PrepareForMapShutdown();
};
struct idGameLocal {
    bool isServer=false, isMultiplayer=false, inCinematic=false, entitiesAlive=true;
    int gamestate=GAMESTATE_ACTIVE, clientInstanceFirstFreeIndex=0, warnings=0;
    void *camera=nullptr, *portalSky=nullptr, *gameRenderWorld=nullptr;
    MapName mapFileName;
    idMultiplayerGame mpGame;
    const char *GetMapName(){return mapFileName.c_str();}
    void Printf(const char*){}
    void Warning(const char*){++warnings;}
    void MapClear(bool){entitiesAlive=false;}
    void ShutdownInstances(){}
    void ShutdownAsyncNetwork(){isServer=false;}
    void MapShutdown();
} gameLocal;
bool idMultiplayerGame::FinalizeMatchEvidence(bool aborted) {
    assert(aborted && gameLocal.entitiesAlive && gameLocal.gamestate==GAMESTATE_ACTIVE);
    assert(std::string(gameLocal.GetMapName())=="mp/q4dm5");
    ++seals; return succeeds;
}
@PREPARE@
@SHUTDOWN@
int main() {
    for (int mask=0; mask<16; ++mask) {
        gameLocal=idGameLocal{};
        gameLocal.isServer=(mask&1)!=0;
        gameLocal.isMultiplayer=(mask&2)!=0;
        if(mask&4) gameLocal.mapFileName="mp/q4dm5";
        gameLocal.mpGame.succeeds=(mask&8)!=0;
        const int expected=(mask&7)==7 ? 1:0;
        gameLocal.MapShutdown();
        assert(gameLocal.mpGame.seals==expected);
        assert(gameLocal.warnings==((mask==7) ? 1:0));
        assert(!gameLocal.entitiesAlive && gameLocal.mapFileName.empty());
        assert(gameLocal.gamestate==GAMESTATE_NOMAP);
        // Session unload and module shutdown may request teardown twice.
        gameLocal.MapShutdown();
        assert(gameLocal.mpGame.seals==expected);
    }
    puts("map shutdown lifetime: PASS (16 cases)");
}
'''.replace("@PREPARE@", prepare).replace("@SHUTDOWN@", shutdown)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="map-shutdown-", dir=temporary) as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def mvd_artifact_lifetime_contract(multiplayer: str) -> None:
    from mp_match_series_report_contract import HARNESS, SOURCE

    network = (ROOT / "src/framework/async/NetworkSystem.h").read_text(encoding="utf-8")
    abi = network[network.index("static const int SERVER_MVD_RESULT_QPATH_BYTES"):
                  network.index("} serverMVDRecordingResult_t;") + len("} serverMVDRecordingResult_t;")]
    helper_start = multiplayer.rfind("enum {", 0, multiplayer.index("MATCH_MVD_REPORT_REASON_RESULT_UNAVAILABLE"))
    helpers = multiplayer[helper_start:multiplayer.index("void idMultiplayerGame::ProjectMatchMVDReportArtifact(")]
    projection = function(multiplayer, "void idMultiplayerGame::ProjectMatchMVDReportArtifact(", "MVD path ownership")
    persisted = next(line for line in multiplayer.splitlines() if "evidenceLifecycle.persisted =" in line)
    harness = HARNESS.split("int main() {")[0] + r'''
#include <string>
@ABI@
struct idStr : std::string {
    using std::string::operator=;
    void Clear(){clear();}
    static int Cmp(const char *a,const char *b){return strcmp(a,b);}
};
struct Network {
    serverMVDRecordingResult_t result={};
    bool available=true;
    bool ServerCopyMVDRecordingResult(serverMVDRecordingResult_t &out){out=result;return available;}
} network, *networkSystem=&network;
struct idMultiplayerGame {
    bool matchMVDAttemptedBySession=true, matchMVDOperatorOwnedBySession=false;
    const char *matchMVDQPath="demos/match_11454271811520319503_mp_q4dm5.mvd";
    void ProjectMatchMVDReportArtifact(mpSeriesReportArtifactInput&,idStr&)const;
};
@HELPERS@
@PROJECTION@
int main() {
    for(int state=0;state<SERVER_MVD_RESULT_STATE_COUNT;++state) {
        for(bool owned : {false,true}) {
            idMultiplayerGame mp;
            mp.matchMVDOperatorOwnedBySession=owned;
            network.result={};
            network.result.state=static_cast<serverMVDResultState_t>(state);
            const bool committed=state==SERVER_MVD_RESULT_COMMITTED;
            if(committed) snprintf(network.result.finalQPath,sizeof(network.result.finalQPath),"%s",mp.matchMVDQPath);
            else snprintf(network.result.partialQPath,sizeof(network.result.partialQPath),"%s.part",mp.matchMVDQPath);
            if(state==SERVER_MVD_RESULT_FAILED) network.result.reason=SERVER_MVD_REASON_SYNC_FAILED;
            auto input=Map(1,"mp/q4dm5",MP_SERIES_REPORT_MAP_FORFEIT,0,0,0);
            auto &artifact=input.artifacts[MP_SERIES_REPORT_ARTIFACT_MVD];
            idStr path;
            mp.ProjectMatchMVDReportArtifact(artifact,path);
            // The returned input must reference the caller's surviving owner.
            CHECK(artifact.qpath==path.c_str());
            CHECK(path==std::string(mp.matchMVDQPath)+(committed ? "" : ".part"));
            CHECK(artifact.status==(committed ? MP_SERIES_REPORT_ARTIFACT_AVAILABLE :
                (state==SERVER_MVD_RESULT_PENDING && owned ? MP_SERIES_REPORT_ARTIFACT_PENDING : MP_SERIES_REPORT_ARTIFACT_FAILED)));
            mpCompetitionSeriesReport report;
            CHECK(report.Initialize(Identity()).WasAccepted());
            CHECK(ChangedOnce(report.AppendMapResult(input)));
            CHECK(report.ValidateInvariants());
            CHECK(std::string(report.GetMapResult(0)->artifacts[MP_SERIES_REPORT_ARTIFACT_MVD].qpath)==path);
        }
    }
    for(int mode=0;mode<4;++mode) {
        idMultiplayerGame mp;
        network.available=mode!=2;
        if(mode==0) mp.matchMVDAttemptedBySession=false;
        if(mode==1) mp.matchMVDQPath="../bad.mvd";
        if(mode==3) snprintf(network.result.partialQPath,sizeof(network.result.partialQPath),"%s","demos/different.mvd.part");
        mpSeriesReportArtifactInput artifact={};
        idStr path;
        mp.ProjectMatchMVDReportArtifact(artifact,path);
        CHECK(path.empty() && artifact.qpath[0]=='\0');
        CHECK(artifact.status==(mode==0 ? MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED : MP_SERIES_REPORT_ARTIFACT_FAILED));
    }
    for(bool matchEvidenceFinalized : {false,true}) {
        for(bool matchEvidencePersisted : {false,true}) {
            struct {bool persisted;} evidenceLifecycle;
            @PERSISTED@
            CHECK(evidenceLifecycle.persisted==(matchEvidenceFinalized && matchEvidencePersisted));
        }
    }
    puts("MVD report artifact lifetime: PASS (10 states; 4 pending/finalized projections)");
}
'''
    harness = harness.replace("@ABI@", abi).replace("@HELPERS@", helpers).replace("@PROJECTION@", projection).replace("@PERSISTED@", persisted)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="mvd-path-", dir=ROOT / ".tmp") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST", f"-I{ROOT / 'src'}",
                        str(cpp), str(SOURCE), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def terminal_continuation_contract(multiplayer: str) -> None:
    branch = function(multiplayer, "if ( kind == MP_OPERATION_CONTINUATION_SERIES_PERSIST )", "terminal adapter dispatch")
    recovery = function(multiplayer, "bool idMultiplayerGame::HasRecoveredCompetitionReview", "restored review authority")
    require(multiplayer, "context.seriesReviewRecovered = HasRecoveredCompetitionReview();", "server-owned recovery context")
    harness = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
enum {WARMUP=100,COUNTDOWN,GAMEREVIEW};
enum {MP_OPERATION_CONTINUATION_SERIES_PERSIST=1,MP_SERIES_READY,MP_SERIES_MAP_ACTIVE,MP_SERIES_MAP_COMPLETE,
      MP_SERIES_COMPLETE,MP_SERIES_CANCELLED,MP_SERIES_VETO,MP_OPERATION_APPLIED,
      MP_OPERATION_REJECTED,MP_OPERATION_REASON_CORE_REJECTED,MP_MATCH_PROTOCOL_REASON_INTERNAL};
struct mpParticipantId {int id=0;static mpParticipantId Invalid(){return {};}};
struct mpCompetitionSeries {
    int state=MP_SERIES_MAP_ACTIVE;
    int GetState()const{return state;}
    int GetRevision()const{return 42;}
    const char *GetNextMapToken()const{return "mp/q4dm5";}
};
struct mpCompetitionSeriesReport {int final=0;};
struct {bool isListenServer=true;int localClientNum=0;} gameLocal;
struct Execution {
    int outcome=MP_OPERATION_APPLIED,reason=0,protocolReason=0,resultingSeriesRevision=0;
    struct {mpParticipantId actor{9};bool cleared=false;void Clear(){cleared=true;}} continuation;
};
struct Adapter {
    mpCompetitionSeries matchSeries;
    mpCompetitionSeriesReport matchSeriesReport;
    bool succeeds=true;
    int finalCalls=0,persistCalls=0,loadCalls=0,authorizer=-1;
    bool FinalizeCompetitionSeriesReport(mpCompetitionSeries &series,mpCompetitionSeriesReport &report,mpParticipantId actor){
        ++finalCalls;authorizer=actor.id;report.final=series.state;return succeeds;
    }
    bool PersistCompetitionSeries(){++persistCalls;return succeeds;}
    bool ScheduleCompetitionSeriesMap(mpCompetitionSeries&,const char*,Execution&){++loadCalls;return succeeds;}
    bool Execute(Execution &execution,int clientNum) {
        const int kind=MP_OPERATION_CONTINUATION_SERIES_PERSIST;
        @BRANCH@
        return false;
    }
};
struct idMultiplayerGame {
    struct Session {int phase=WARMUP;int GetPhase()const{return phase;}int GetSessionId()const{return 41;}} matchSession;
    mpCompetitionSeries matchSeries;
    int matchSeriesLinkedSessionId=0;
    bool HasRecoveredCompetitionReview()const;
};
@RECOVERY@
int main() {
    for(int phase : {WARMUP,COUNTDOWN,GAMEREVIEW}) {
        for(int state : {MP_SERIES_MAP_COMPLETE,MP_SERIES_MAP_ACTIVE,MP_SERIES_COMPLETE}) {
            for(int linked : {0,41,42}) {
                idMultiplayerGame game;game.matchSession.phase=phase;
                game.matchSeries.state=state;game.matchSeriesLinkedSessionId=linked;
                assert(game.HasRecoveredCompetitionReview()==
                    (phase==WARMUP && state==MP_SERIES_MAP_COMPLETE && linked==42));
            }
        }
    }
    for(int state : {MP_SERIES_READY,MP_SERIES_MAP_ACTIVE,MP_SERIES_COMPLETE,MP_SERIES_CANCELLED,MP_SERIES_VETO}) {
        for(bool succeeds : {false,true}) {
            for(int client : {0,1}) {
                Adapter adapter;adapter.matchSeries.state=state;adapter.succeeds=succeeds;
                Execution execution;
                assert(adapter.Execute(execution,client)==succeeds);
                bool terminal=state==MP_SERIES_COMPLETE || state==MP_SERIES_CANCELLED;
                assert(adapter.finalCalls==(terminal?1:0));
                assert(adapter.loadCalls==(state==MP_SERIES_READY?1:0));
                assert(adapter.persistCalls==(!terminal && state!=MP_SERIES_READY?1:0));
                assert(adapter.matchSeriesReport.final==(terminal && succeeds?state:0));
                if(terminal) assert(adapter.authorizer==(client==0?0:9));
                if(state!=MP_SERIES_READY) {
                    assert(execution.continuation.cleared);
                    assert(execution.outcome==(succeeds?MP_OPERATION_APPLIED:MP_OPERATION_REJECTED));
                    assert(execution.resultingSeriesRevision==(succeeds?42:0));
                }
            }
        }
    }
    puts("terminal series continuation: PASS (20 cases; 27 restored-review authority cases)");
}
'''.replace("@BRANCH@", branch).replace("@RECOVERY@", recovery)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="series-terminal-", dir=ROOT / ".tmp") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def map_asset_preflight_contract(multiplayer: str) -> None:
    scheduling = function(multiplayer,
        "bool idMultiplayerGame::ScheduleCompetitionSeriesMap", "map asset preflight")
    harness = r'''
#include "mpgame/mp/match/MatchSeries.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
enum { MAX_CLIENTS=2, MP_OPERATION_REJECTED=1, MP_OPERATION_APPLIED,
    MP_OPERATION_NEEDS_ADAPTER, MP_OPERATION_REASON_NONE=0,
    MP_OPERATION_REASON_SERIES_STATE, MP_OPERATION_REASON_CORE_REJECTED,
    MP_OPERATION_REASON_INVARIANT, MP_MATCH_PROTOCOL_REASON_CONFLICT=1,
    MP_MATCH_PROTOCOL_REASON_INTERNAL };
enum findFile_t { FIND_NO, FIND_YES, FIND_ADDON };
struct idStr : std::string {
    using std::string::string;
    using std::string::operator=;
    int Length() const { return static_cast<int>(size()); }
    static int Icmp(const char *a,const char *b) { return std::strcmp(a,b); }
    void SetFileExtension(const char *extension) { append(extension); }
};
struct idDict {
    std::map<std::string,std::string> values;
    void Set(const char *key,const char *value) { values[key]=value; }
};
struct idEntity { bool IsType(int) const { return true; } } entities[MAX_CLIENTS];
struct idPlayer { static int GetClassType() { return 1; } };
const char *teamNames[] = { "Marine", "Strogg" };
struct GameLocal {
    int gameType=2, numClients=MAX_CLIENTS, userInfoChanges=0, warnings=0;
    bool teamGame=true;
    idStr sessionCommand;
    idDict serverInfo, userInfo[MAX_CLIENTS];
    idEntity *entities[MAX_CLIENTS]={&::entities[0],&::entities[1]};
    bool IsTeamGame() const { return teamGame; }
    void SetUserInfo(int slot,const idDict &dict,bool) { userInfo[slot]=dict; ++userInfoChanges; }
    template<class... Args> void Warning(const char*,Args...) { ++warnings; }
} gameLocal;
struct MapCvar {
    std::string value="mp/lobby";
    int writes=0;
    void SetString(const char *text) { value=text; ++writes; }
} si_map;
struct FileSystem {
    findFile_t result=FIND_YES;
    int calls=0;
    bool scheduled=false;
    std::string qpath;
    findFile_t FindFile(const char *path,bool scheduleAddons) {
        ++calls; scheduled=scheduleAddons; qpath=path; return result;
    }
} files;
FileSystem *fileSystem=&files;
bool hasDecl=true, modeSupported=true;
idDict mapDecl;
const idDict *MultiplayerResolveMapDecl(const char*) { return hasDecl ? &mapDecl : NULL; }
bool MPMapSupportsGameType(const idDict*,int) { return modeSupported; }
const char *MPGameTypeName(int) { return "test mode"; }
struct mpCompetitionSeriesReport {};
struct mpOperationExecutionResult_t {
    int outcome=MP_OPERATION_NEEDS_ADAPTER, reason=-1, protocolReason=0;
    mpSeriesReason_t seriesReason=MP_SERIES_REASON_NONE;
    uint64_t resultingSeriesRevision=0;
    struct { bool cleared=false; void Clear() { cleared=true; } } continuation;
};
struct idMultiplayerGame {
    mpCompetitionSeries matchSeries, savedSeries;
    mpCompetitionSeriesReport matchSeriesReport;
    uint64_t matchSeriesId=99, matchSeriesLinkedSessionId=55, savedLinkedSession=0;
    bool matchSeriesAwaitingMapSession=false, persistenceSucceeds=true;
    int persistCalls=0;
    int matchSeriesGameSideForCompetition[2]={0,1};
    int matchSeriesCompetitionSide[2]={0,1};
    uint64_t matchSeriesCompetitionConnection[2]={11,12}, matchConnectionId[2]={11,12};
    struct Session { uint64_t GetSessionId() const { return 77; } } matchSession;
    bool PersistCompetitionSeriesCandidate(const mpCompetitionSeries &candidate,
            const mpCompetitionSeriesReport&,uint64_t seriesId,uint64_t sessionId) {
        CHECK(seriesId==99);
        ++persistCalls;
        if(persistenceSucceeds) { savedSeries=candidate; savedLinkedSession=sessionId; }
        return persistenceSucceeds;
    }
    bool ScheduleCompetitionSeriesMap(mpCompetitionSeries&,const char*,mpOperationExecutionResult_t&);
};
@SCHEDULING@
static mpCompetitionSeries ReadySeries(bool teamGame) {
    mpSeriesConfiguration configuration;
    const char *maps[] = { "mp/series_asset_probe" };
    mpSeriesReason_t reason=MP_SERIES_REASON_NONE;
    CHECK(MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_ONE,2,123,0,
        teamGame,maps,1,configuration,reason));
    mpCompetitionSeries series;
    CHECK(series.Configure(configuration,series.GetRevision()).WasApplied());
    CHECK(series.Start(series.GetRevision()).WasApplied());
    CHECK(series.ApplyVeto(0,MP_SERIES_VETO_DECIDER,maps[0],MP_SERIES_SIDE_NONE,
        series.GetRevision()).WasApplied());
    if(teamGame) {
        CHECK(series.ApplyVeto(1,MP_SERIES_VETO_SIDE,maps[0],0,
            series.GetRevision()).WasApplied());
    }
    CHECK(series.GetState()==MP_SERIES_READY && series.ValidateInvariants());
    return series;
}
int main() {
    int cases=0;
    for(bool teamGame : {false,true}) for(bool persists : {true,false}) {
        // Missing metadata/mode remains rejected; a valid declaration alone
        // must not allow an absent map to reach the durable MAP_ACTIVE state.
        for(int availability=0;availability<5;++availability) {
            gameLocal=GameLocal();gameLocal.teamGame=teamGame;
            gameLocal.serverInfo.Set("si_map","mp/lobby");
            si_map=MapCvar();files=FileSystem();
            hasDecl=availability!=3;modeSupported=availability!=4;
            files.result=availability==0 ? FIND_NO :
                (availability==2 ? FIND_ADDON : FIND_YES);
            idMultiplayerGame game;
            game.persistenceSucceeds=persists;
            game.matchSeries=ReadySeries(teamGame);
            auto candidate=game.matchSeries;
            const auto beforeRevision=candidate.GetRevision();
            const bool available=availability==1 || availability==2;
            const bool loads=available && persists;
            mpOperationExecutionResult_t execution;
            CHECK(game.ScheduleCompetitionSeriesMap(candidate,
                "mp/series_asset_probe",execution)==persists);
            CHECK(game.persistCalls==1);
            CHECK(game.matchSeries.GetState()==(loads ? MP_SERIES_MAP_ACTIVE : MP_SERIES_READY));
            CHECK(!files.scheduled);
            CHECK(files.calls==((hasDecl && modeSupported) ? 1 : 0));
            if(files.calls) CHECK(files.qpath=="maps/mp/series_asset_probe.map");
            CHECK(execution.continuation.cleared);
            CHECK(game.matchSeries.GetAttemptCount()==0);
            CHECK(game.matchSeries.GetMapLoadFailureCount()==(!available && persists ? 1 : 0));
            CHECK(game.matchSeries.GetRevision()==beforeRevision+(persists ? 1 : 0));
            CHECK(game.matchSeries.ValidateInvariants());
            CHECK(game.matchSeriesAwaitingMapSession==loads);
            CHECK(game.matchSeriesLinkedSessionId==(loads ? 77 : 55));
            CHECK(gameLocal.sessionCommand==(loads ? "nextMap" : ""));
            CHECK(si_map.value==(loads ? "mp/series_asset_probe" : "mp/lobby"));
            CHECK(si_map.writes==(loads ? 1 : 0));
            CHECK(gameLocal.serverInfo.values["si_map"]==si_map.value);
            CHECK(gameLocal.userInfoChanges==(loads && teamGame ? 2 : 0));
            CHECK(game.matchSeriesGameSideForCompetition[0]==(loads && teamGame ? 1 : 0));
            CHECK(game.matchSeriesGameSideForCompetition[1]==(loads && teamGame ? 0 : 1));
            if(persists) {
                CHECK(game.savedSeries.GetState()==(available ? MP_SERIES_MAP_ACTIVE : MP_SERIES_READY));
                CHECK(game.savedLinkedSession==(available ? 77 : 55));
                CHECK(execution.outcome==MP_OPERATION_APPLIED);
                CHECK(execution.seriesReason==(available ? MP_SERIES_REASON_NONE : MP_SERIES_REASON_INVALID_MAP_TOKEN));
            } else {
                CHECK(game.savedSeries.GetState()==MP_SERIES_DISABLED);
                CHECK(execution.outcome==MP_OPERATION_REJECTED);
                CHECK(execution.protocolReason==MP_MATCH_PROTOCOL_REASON_INTERNAL);
            }
            if(!available) {
                CHECK(candidate.GetState()==MP_SERIES_READY);
                CHECK(candidate.GetCurrentSelectionIndex()==-1);
            }
            if(availability==0 && persists) {
                // Restoring the asset allows a deliberate retry of the same
                // selection without inventing a result or advancing the series.
                files.result=FIND_YES;candidate=game.matchSeries;
                mpOperationExecutionResult_t retry;
                CHECK(game.ScheduleCompetitionSeriesMap(candidate,
                    "mp/series_asset_probe",retry));
                CHECK(game.matchSeries.GetState()==MP_SERIES_MAP_ACTIVE);
                CHECK(game.matchSeries.GetMapLoadFailureCount()==1);
                CHECK(game.matchSeries.GetAttemptCount()==0);
                CHECK(game.matchSeries.GetRevision()==beforeRevision+2);
            }
            ++cases;
        }
    }
    std::printf("series map asset preflight: PASS (%d cases; 2 restored-asset retries)\n",cases);
}
'''.replace("@SCHEDULING@", scheduling)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="series-map-preflight-", dir=ROOT / ".tmp") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *shlex.split(os.environ.get("CXXFLAGS", "")),
                        "-DMP_MATCH_SERIES_STANDALONE_TEST", f"-I{ROOT / 'src'}",
                        str(cpp), str(ROOT / "src/mpgame/mp/match/MatchSeries.cpp"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def main() -> None:
    header = MULTIPLAYER_HEADER.read_text(encoding="utf-8", errors="strict")
    multiplayer = MULTIPLAYER_SOURCE.read_text(encoding="utf-8", errors="strict")
    operations = OPERATIONS_SOURCE.read_text(encoding="utf-8", errors="strict")
    recovery_header = RECOVERY_HEADER.read_text(encoding="utf-8", errors="strict")
    recovery_source = RECOVERY_SOURCE.read_text(encoding="utf-8", errors="strict")

    member_contracts(header, multiplayer)
    recovery_contracts(multiplayer, recovery_header, recovery_source)
    initial_publish_contract(multiplayer)
    map_session_handoff_contracts(multiplayer)
    evidence_commit_contracts(multiplayer)
    terminal_report_contract(multiplayer)
    mutation_guard_contracts(operations)
    artifact_status_contract(multiplayer)
    map_shutdown_contract(multiplayer)
    mvd_artifact_lifetime_contract(multiplayer)
    terminal_continuation_contract(multiplayer)
    map_asset_preflight_contract(multiplayer)
    print("mp_match_series_live_transaction_contract: PASS")


if __name__ == "__main__":
    main()
