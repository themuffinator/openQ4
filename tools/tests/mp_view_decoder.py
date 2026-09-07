"""Decode captured Match Control bytes with the canonical production codec."""
from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MAIN = r'''
static void JsonString(const char *value, int length) {
    putchar('"');
    for (int i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
static void JsonProposal(const mpMatchViewProposalSummary_t &p) {
    printf("{\"present\":%d,\"id\":%u,\"opcode\":%d,\"scope\":%d,\"side\":%d,"
        "\"caller\":%u,\"yes\":%u,\"no\":%u,\"abstain\":%u,\"cast\":%u,"
        "\"eligible\":%u,\"quorum\":%u,\"required_yes\":%u,\"expires\":%llu,"
        "\"recipient_eligible\":%d,\"recipient_ballot\":%d}",
        p.present, p.proposalId, p.opcode, p.scope, p.side, p.callerParticipantId,
        p.yesCount, p.noCount, p.abstainCount, p.castCount, p.eligibleCount,
        p.requiredQuorumCount, p.requiredYesCount, p.expiresAtEngineMsec,
        p.recipientEligible, p.recipientBallot);
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) return 2;
    byte buffer[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 1];
    const size_t size = fread(buffer, 1, sizeof(buffer), input);
    const bool readError = ferror(input) != 0;
    fclose(input);
    if (readError || size == 0 || size > MP_MATCH_VIEW_MAX_MESSAGE_BYTES) return 3;
    idBitMsg message;
    message.Init(buffer, static_cast<int>(size));
    message.SetSize(static_cast<int>(size));
    message.BeginReading();
    mpSessionView view; view.Clear();
    mpMatchViewError_t error; error.Clear();
    if (!MPMatchViewDecode(message, view, &error)) {
        fprintf(stderr, "MatchView rejected bytes: reason=%d field=%u detail=%u\n",
            error.reason, error.fieldId, error.detail);
        return 4;
    }
    const auto &p = view.publicState;
    printf("{\"session\":%llu,\"revision\":%llu,\"slot\":%u,\"participant\":%u,\"generation\":%u,"
        "\"side\":%d,\"competition_side\":%d,\"roles\":%u,\"active\":%d,"
        "\"phase\":%d,\"round\":%d,\"pause\":%d,\"engine\":%llu,\"match\":%llu,"
        "\"ready\":%d,\"ready_eligible\":%d,\"queue_state\":%d,"
        "\"has_queue_position\":%d,\"queue_position\":%u,\"vitals\":[",
        p.sessionId, p.sessionRevision, p.recipient.slot, p.recipient.participantId, p.recipient.bindingGeneration,
        p.recipient.side, p.recipient.competitionSide, p.recipient.publicRoleMask,
        p.recipient.active, p.lifecycle.phase, p.lifecycle.round, p.lifecycle.pauseState,
        p.clocks.engineTimeMsec, p.clocks.matchTimeMsec, p.recipient.ready,
        p.recipient.readyEligible, p.recipient.queueState, p.recipient.hasQueuePosition,
        p.recipient.queuePosition);
    for (int i = 0; i < view.teamVitalCount; ++i) {
        const auto &v = view.teamVitals[i];
        printf("%s{\"participant\":%u,\"side\":%d,\"health\":%u,\"armor\":%u}",
            i ? "," : "", v.participantId, v.participantSide, v.health, v.armor);
    }
    printf("],\"follows\":[");
    for (int i = 0; i < view.followTargetCount; ++i) {
        const auto &f = view.followTargets[i];
        printf("%s{\"participant\":%u,\"side\":%d,\"selectable\":%d}",
            i ? "," : "", f.participantId, f.participantSide, f.selectable);
    }
    printf("],\"items\":[");
    for (int i = 0; i < view.itemTimingCount; ++i) {
        const auto &t = view.itemTimings[i];
        printf("%s{\"available\":%d,\"deadline\":%llu,\"token\":",
            i ? "," : "", t.available, t.matchDeadlineMsec);
        JsonString(t.token, t.tokenLength); putchar('}');
    }
    printf("],\"participants\":[");
    for (int i = 0; i < p.participantSummaryCount; ++i) {
        const auto &v = p.participantSummaries[i];
        printf("%s{\"participant\":%u,\"slot\":%u,\"connected\":%d,\"human\":%d,\"active\":%d,\"side\":%d,\"roles\":%u}",
            i ? "," : "", v.participantId, v.slot, v.connected, v.human, v.active, v.side, v.publicRoleMask);
    }
    printf("],\"roster_seats\":[");
    for (int i = 0; i < view.rosterSeatCount; ++i) {
        const auto &s = view.rosterSeats[i];
        printf("%s{\"seat\":%u,\"side\":%d,\"role\":%d,\"required\":%d,\"occupied\":%d,"
            "\"participant\":%u,\"connected\":%d,\"ready\":%d,\"active\":%d}",
            i ? "," : "", s.seatIndex, s.side, s.role, s.required, s.occupied,
            s.participantId, s.connected, s.ready, s.active);
    }
    printf("],\"invitations\":[");
    for (int i = 0; i < view.invitationCount; ++i) {
        const auto &v = view.invitations[i];
        printf("%s{\"id\":%u,\"side\":%d,\"role\":%d,\"issuer\":%u,\"target\":%u,\"expires\":%llu}",
            i ? "," : "", v.invitationId, v.side, v.role, v.inviterParticipantId,
            v.inviteeParticipantId, v.expiresAtEngineMsec);
    }
    printf("],\"global_proposal\":"); JsonProposal(p.globalProposal);
    printf(",\"side_proposal\":"); JsonProposal(view.ownSideProposal);
    printf(",\"operations\":[");
    for (int i = 0; i < p.operationAvailabilityCount; ++i) {
        const auto &o = p.operationAvailability[i];
        printf("%s{\"opcode\":%d,\"available\":%d,\"reason\":%d}",
            i ? "," : "", o.opcode, o.available, o.reason);
    }
    const auto &s = p.series;
    printf("],\"series\":{\"present\":%d,\"id\":%llu,\"state\":%d,\"revision\":%llu,"
        "\"best_of\":%u,\"map_number\":%u,\"wins\":[%u,%u],\"has_next_map\":%d,"
        "\"next_map\":", s.present, s.seriesId, s.state, s.revision, s.bestOf,
        s.currentMapNumber, s.wins[0], s.wins[1], s.hasNextMap);
    JsonString(s.nextMap, s.nextMapLength);
    printf(",\"veto_step\":%u,\"veto_steps\":%u,\"has_veto\":%d,\"veto_action\":%d,"
        "\"veto_side\":%d,\"maps\":[", s.currentVetoStep, s.vetoStepCount,
        s.hasVetoTurn, s.vetoTurnAction, s.vetoTurnSide);
    for (int i = 0; i < s.mapPoolCount; ++i) {
        const auto &m = s.mapPool[i];
        printf("%s{\"pool_index\":%u,\"disposition\":%d,\"selection\":%u,"
            "\"has_starting_side\":%d,\"starting_side\":%d,\"side_chosen_by\":%d,\"token\":",
            i ? "," : "", m.poolIndex, m.disposition, m.selectionNumber,
            m.hasStartingGameSide, m.startingGameSide, m.gameSideChosenBy);
        JsonString(m.mapToken, m.tokenLength); putchar('}');
    }
    printf("],\"history\":[");
    for (int i = 0; i < s.mapHistoryCount; ++i) {
        const auto &h = s.mapHistory[i];
        printf("%s{\"attempt\":%u,\"pool_index\":%u,\"outcome\":%d,\"winner\":%d,\"scores\":[%u,%u]}",
            i ? "," : "", h.attemptNumber, h.mapPoolIndex, h.outcome,
            h.winnerSide, h.scores[0], h.scores[1]);
    }
    const auto &r = p.terminalResult;
    printf("]},\"result\":{\"outcome\":%d,\"reason\":%d,\"revision\":%llu,"
        "\"winner_side\":%d,\"winner_participant\":%u,\"winner_name\":",
        r.outcome, r.reason, r.resultRevision, r.winnerSide, r.winnerParticipantId);
    JsonString(r.winnerName, r.winnerNameLength);
    puts("}}");
    return 0;
}
'''


class MatchViewDecoder:
    def __init__(self, output: Path, gamelibs: Path = ROOT.parent / "openQ4-game", *,
                 executable: Path | None = None, production_source: Path | None = None) -> None:
        self.folder = None
        if executable is not None:
            self.executable = executable.resolve()
            if not self.executable.is_file():
                raise RuntimeError(f"pinned MatchView decoder is missing: {self.executable}")
            return
        output = output.resolve()
        output.mkdir(parents=True, exist_ok=True)
        self.folder = tempfile.TemporaryDirectory(prefix="view-decoder-", dir=output)
        folder = Path(self.folder.name)
        self.executable = folder / "decode.exe"
        source = gamelibs / "tools/tests/mp_match_view_contract.py"
        spec = importlib.util.spec_from_file_location("match_view_contract_support", source)
        if spec is None or spec.loader is None:
            raise RuntimeError("the canonical MatchView codec contract is required")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        # Reuse the established byte-aligned BitMsg seam, production .cpp
        # include and localization hook; do not copy a second wire decoder.
        support = module.HARNESS.split("#define CHECK(condition)", 1)[0]
        cpp = folder / "decode.cpp"
        cpp.write_text(support + MAIN, encoding="utf-8")
        compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
        if compiler is None:
            raise RuntimeError("a C++ compiler is required to verify captured MatchView bytes")
        result = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
                                 f"-I{production_source or gamelibs / 'src'}", str(cpp), "-o", str(self.executable)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def decode(self, source: Path) -> dict:
        result = subprocess.run([str(self.executable), str(source)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"captured MatchView failed decoding: {result.stderr}")
        return json.loads(result.stdout)

    def close(self) -> None:
        if self.folder is not None:
            self.folder.cleanup()
