#!/usr/bin/env python3
"""Three hidden windowed clients qualify managed Duel FIFO and leading-player forfeit."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, write_script
from mp_view_decoder import MatchViewDecoder

ROOT = Path(__file__).resolve().parents[2]
ROLES = ("host", "client", "challenger")


class DuelQueueRun:
    def __init__(self, args):
        self.args = args
        self.runtime, self.output = args.runtime_dir.resolve(), args.output_dir.resolve()
        self.games = {role: self.output / role / "baseoq4" for role in ROLES}
        self.logs = {role: game / "logs/openq4.log" for role, game in self.games.items()}
        self.loops, self.processes, self.streams = {}, {}, []
        self.command_numbers = {role: 0 for role in ROLES}
        self.operation, self.stage = 0, "initialization"
        self.started, self.deadline = time.time(), time.monotonic() + args.timeout
        self.views, self.shots, self.failures, self.exits = {}, [], [], {}
        self.evidence = None
        self.output.mkdir(parents=True, exist_ok=True)
        if (self.runtime / "manifest.json").is_file():
            shutil.copy2(self.runtime / "manifest.json", self.output / "runtime-manifest.json")
        pinned_decoder = self.runtime / "match-view-decode.exe"
        self.decoder = MatchViewDecoder(self.output,
            executable=pinned_decoder if pinned_decoder.is_file() else None)
        for role, game in self.games.items():
            game.mkdir(parents=True, exist_ok=True)
            self.loops[role] = game / "duel_queue_commands.cfg"
            write_script(self.loops[role], ["// Waiting for the first operation."])
            write_script(game / "duel_queue_pump.cfg", ["exec duel_queue_commands.cfg", "waitMsec 250",
                         "openq4_reportMPState", "vstr oq4_duel_queue_pump"])
            self.start_script(role, "MP_DUEL_QUEUE_LOADED")

    def start_script(self, role, marker):
        number = self.command_numbers[role] + 1
        write_script(self.loops[role], ["// Waiting for the next operation."])
        write_script(self.games[role] / "duel_queue_start.cfg", [
            f'set oq4_duel_queue_cmd_{number} "exec duel_queue_step_{number}.cfg"',
            'set oq4_duel_queue_pump "exec duel_queue_pump.cfg"',
            "echo " + marker, "exec duel_queue_pump.cfg"])

    def launch(self, role, name=None):
        a = self.args
        cvars = {
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(a.basepath.resolve()), "fs_savepath": str(self.games[role].parent),
            "fs_devpath": str(self.games[role].parent), "fs_game": "baseoq4", "com_gameMode": "MP",
            "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
            "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": str(a.width),
            "r_customHeight": str(a.height), "r_windowWidth": str(a.width), "r_windowHeight": str(a.height),
            "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "r_renderApi": a.renderer, "com_maxfps": "60", "r_swapInterval": "0", "logFile": "2",
            "logFileName": "logs/openq4.log", "developer": "1", "ui_autoJoin": "1",
            "ui_spectate": "Play", "ui_ready": "Not Ready",
            "ui_name": name or (a.winner_name if role == a.winner_role else role),
            "net_allowCheats": "1", "com_skipLoadingContinue": "1",
            "g_autoExecAfterMapLoad": "duel_queue_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
            "net_port": str(a.port + ROLES.index(role)),
        }
        if role == "host":
            cvars.update(net_serverDedicated="0", net_LANServer="1", si_pure="0", si_maxPlayers="4",
                         si_gameType="Duel", bot_minPlayers="0", g_matchProfile="competitive_duel",
                         g_gameReviewPause="35", si_mapCycle="", g_matchEvidence=str(a.evidence))
        command = [str(self.runtime / a.executable_name)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        if len(cvars) + 1 > 64:
            raise RuntimeError("engine startup command limit exceeded")
        command += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{a.port}"]
        (self.output / f"{role}-launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (self.output / f"{role}-console.log").open("w", encoding="utf-8")
        self.streams.append(stream)
        self.processes[role] = subprocess.Popen(command, cwd=self.runtime, stdout=stream, stderr=subprocess.STDOUT)

    def wait(self, label, predicate, timeout=60):
        self.stage = label
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in self.logs.items()}
            if re.search(r"FATAL ERROR|ERROR:|Unknown command|usage: openq4_guiSet|rejected match.*transition",
                         "\n".join(texts.values())):
                raise RuntimeError("engine or diagnostic failure during " + label)
            if any(p.poll() is not None for p in self.processes.values()):
                raise RuntimeError("a process ended during " + label)
            if predicate(texts):
                return texts
            time.sleep(0.2)
        raise RuntimeError("timeout during " + label)

    def inject(self, role, actions):
        self.operation += 1
        self.command_numbers[role] += 1
        number = self.command_numbers[role]
        # Keep the loop itself immutable so a transient Windows read failure
        # on the replaceable selector cannot stop it. Retire every step before
        # executing actions so selector rereads never repeat a match operation.
        write_script(self.games[role] / f"duel_queue_step_{number}.cfg", [
            f'set oq4_duel_queue_cmd_{number} ""',
            f'set oq4_duel_queue_cmd_{number + 1} "exec duel_queue_step_{number + 1}.cfg"', *actions])
        write_script(self.loops[role], [f"vstr oq4_duel_queue_cmd_{number}"])

    def perform(self, role, label, actions):
        begin, end = f"MP_DUEL_QUEUE_BEGIN_{self.operation + 1}", f"MP_DUEL_QUEUE_END_{self.operation + 1}"
        self.inject(role, ["echo " + begin, *actions, "openq4_reportMPState",
                           "openq4_matchControl report", "echo " + end])
        texts = self.wait(label, lambda values: re.search(r"\n" + end + r"(?:\s|$)", values[role]))
        section = re.split(r"\n" + begin + r"(?:\s|$)", texts[role], maxsplit=1)[-1]
        return re.split(r"\n" + end + r"(?:\s|$)", section, maxsplit=1)[0]

    def view(self, role, label, predicate=lambda _: True, timeout=30):
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            self.perform(role, label, ["waitMsec 350"])
            source = self.games[role] / f"match-probe/view-{ROLES.index(role)}.bin"
            if not source.is_file():
                continue
            decoded = self.decoder.decode(source)
            if predicate(decoded):
                folder = self.output / "views"
                folder.mkdir(exist_ok=True)
                key = role + "-" + label
                shutil.copy2(source, folder / (key + ".bin"))
                (folder / (key + ".json")).write_text(json.dumps(decoded, indent=2), encoding="utf-8")
                self.views[key] = decoded
                return decoded
        raise RuntimeError("accepted view did not satisfy " + role + " " + label)

    def screenshot(self, role, name):
        self.perform(role, name + " screenshot", [f'screenshot "screenshots/{name}.tga"'])
        self.shots.append(self.games[role] / "screenshots" / (name + ".tga"))

    def open_match_control(self, role):
        return self.perform(role, role + " Match Control", ["openq4_assertMenuActivation 4000 game reopen", "waitMsec 1500",
            "openq4_guiSet desktop::active 1", "openq4_guiSet desktop::dest 21",
            "GuiEvent hideMain", "GuiEvent chooseCurr", "GuiEvent resetMain0", "waitMsec 1500",
            "openq4_guiSet desktop::match_tab 0", "waitMsec 350", "openq4_guiGet match_status_values::text"])

    def assert_last_result(self, role):
        text = self.open_match_control(role)
        if f'Last result: {self.args.winner_name} wins!' not in text or "Match forfeited" not in text:
            raise RuntimeError("warmup Match Control did not display the retained result for " + role)

    @staticmethod
    def current(text):
        return text.rsplit("\nMP_STATE ", 1)[-1]

    @staticmethod
    def active_slots(view):
        return {p["slot"] for p in view["participants"] if p["connected"] and p["human"] and p["active"]}

    def run(self):
        self.launch("host")
        self.wait("host warmup", lambda t: "MP match phase: 0 -> 1" in t["host"])
        self.launch("client")
        self.wait("two automatic contestants", lambda t: all(re.search(
            rf"MP_PLAYER slot={slot} .*spectating=0 wantSpectate=0 ingame=1", self.current(t["host"]))
            for slot in (0, 1)) and "MP_DUEL_QUEUE_LOADED" in t["client"])
        self.launch("challenger")
        self.wait("third human queued", lambda t: "MP_DUEL_QUEUE_LOADED" in t["challenger"] and
                  re.search(r"MP_PLAYER slot=2 .*wantSpectate=1 ingame=1", self.current(t["host"])))
        for role in ROLES:
            self.open_match_control(role)
        initial = self.view("challenger", "queued", lambda v: v["phase"] == 1 and not v["active"]
                            and v["has_queue_position"] and v["queue_position"] == 1)
        if self.active_slots(initial) != {0, 1}:
            raise RuntimeError("third arrival displaced a seated contestant")
        self.screenshot("challenger", "queued")
        for repeat in range(2):
            self.perform("challenger", "duplicate queue join", ["openq4_matchControl action queue_join", "waitMsec 750"])
            duplicate = self.view("challenger", "duplicate-" + str(repeat))
            if (duplicate["participant"] != initial["participant"] or duplicate["active"] or
                    not duplicate["has_queue_position"] or duplicate["queue_position"] != 1 or
                    self.active_slots(duplicate) != {0, 1}):
                raise RuntimeError("duplicate queue join changed the waiting player's seat or place")
        self.perform("host", "force ready", ["openq4_matchControl action arm_force_ready", "waitMsec 100",
                     "openq4_matchControl action confirm"])
        self.wait("live Duel", lambda t: re.search(r"phase=3 session_phase=3", self.current(t["host"])))
        self.view("client", "live", lambda v: v["phase"] == 3 and v["active"])
        winner_slot = 0 if self.args.winner_role == "host" else 1
        forfeiter_slot = 1 - winner_slot
        forfeiter_role = ROLES[forfeiter_slot]
        identities = {p["slot"]: p["participant"] for p in initial["participants"]}
        self.perform("host", "make forfeiting player the leader", [f"kill {winner_slot}", "waitMsec 1000"])
        scores = self.wait("remote player leads", lambda t:
            re.search(rf"MP_PLAYER slot={winner_slot} .*score=-1 ", self.current(t["host"])) and
            re.search(rf"MP_PLAYER slot={forfeiter_slot} .*score=0 ", self.current(t["host"])))
        self.perform(forfeiter_role, "leading contestant forfeit", ["openq4_matchControl action arm_forfeit",
                     "waitMsec 100", "openq4_matchControl action confirm"])
        for role in ROLES:
            review = self.view(role, "review", lambda v: v["phase"] == 5)
            if self.active_slots(review) != {0, 1}:
                raise RuntimeError("review did not retain both original finalists")
            if role == "challenger" and (not review["has_queue_position"] or review["queue_position"] != 1):
                raise RuntimeError("review changed the challenger's queue position")
            if (review["result"]["outcome"] != 2 or review["result"]["winner_side"] != -1 or
                    review["result"]["winner_participant"] != identities[winner_slot] or
                    review["result"]["winner_name"] != self.args.winner_name):
                raise RuntimeError("accepted terminal forfeit outcome lost its actual winner")
            summary = self.perform(role, "actual forfeit summary", [
                "openq4_guiGet summary_result::text"])
            if f'GUI_VALUE summary_result::text={self.args.winner_name} wins!' not in summary or "Match forfeited" not in summary:
                raise RuntimeError("summary did not display the frozen winner and forfeit")
            self.screenshot(role, "review")
        result_dir = self.games["host"] / "match-results"
        result_glob = f'session-{initial["session"]}_series-0_*.json'
        if self.args.evidence:
            self.wait("persisted Duel result", lambda _: any(result_dir.glob(result_glob)))
        results = list(result_dir.glob(result_glob))
        if len(results) != (1 if self.args.evidence else 0):
            raise RuntimeError("Duel did not persist exactly one final result")
        if results:
            evidence = json.loads(results[0].read_text(encoding="utf-8"))
            terminal = [event["data"] for event in evidence["journal"]["events"] if event["kind"] == "result"]
            scores = {p["participant"]: p["score"] for p in evidence["participantStats"]["entries"]}
            expected_scores = [-1, 0] if winner_slot == 0 else [0, -1]
            if (len(terminal) != 1 or terminal[0]["outcome"] != "forfeit" or
                    terminal[0]["winnerParticipant"] != identities[winner_slot] or
                    terminal[0]["sideScores"] != expected_scores or
                    terminal[0]["authorizer"]["participant"] != identities[forfeiter_slot] or
                    scores.get(identities[winner_slot]) != -1 or scores.get(identities[forfeiter_slot]) != 0):
                raise RuntimeError("persisted forfeit result did not preserve the actual winner and original scores")
            self.evidence = str(results[0])
        self.wait("next warmup", lambda t: re.search(r"phase=1 session_phase=1", self.current(t["host"])), timeout=90)
        challenger = self.view("challenger", "promoted", lambda v: v["phase"] == 1 and v["active"])
        if (self.active_slots(challenger) != {winner_slot, 2} or challenger["ready"] or
                not challenger["ready_eligible"] or challenger["has_queue_position"]):
            raise RuntimeError("challenger did not replace the forfeiter with fresh readiness")
        retired = self.view(forfeiter_role, "retired", lambda v: v["phase"] == 1 and not v["active"])
        if not retired["has_queue_position"] or retired["queue_position"] != 1:
            raise RuntimeError("leading player who forfeited did not reach the FIFO tail")
        winner = self.view(self.args.winner_role, "winner-retained", lambda v: v["phase"] == 1)
        if not winner["active"] or self.active_slots(winner) != {winner_slot, 2}:
            raise RuntimeError("winner was displaced by the forfeiting score leader")
        if winner["result"] != self.views["host-review"]["result"]:
            raise RuntimeError("next warmup did not retain immutable terminal result")
        self.wait("promoted client enters gameplay", lambda t: re.search(
            r"MP_PLAYER slot=2 .*spectating=0 wantSpectate=0 ingame=1", self.current(t["challenger"])))
        for role in ROLES:
            self.assert_last_result(role)
            self.screenshot(role, "next-warmup")
        baseline = challenger["engine"]
        self.view("challenger", "still-needs-ready", lambda v: v["engine"] >= baseline + 2000 and
                  v["phase"] == 1 and not v["ready"] and v["active"])
        if self.args.rejoin_winner:
            self.inject("client", ["quit"])
            self.processes["client"].wait(timeout=15)
            del self.processes["client"]
            self.wait("winner disconnected", lambda t: not re.search(
                r"MP_PLAYER slot=1 ", self.current(t["host"])))
            self.start_script("client", "MP_DUEL_QUEUE_REJOINED")
            self.launch("client", "replacement")
            self.wait("new player joined reused slot", lambda t: "MP_DUEL_QUEUE_REJOINED" in t["client"])
            replacement = self.view("client", "late-join-result", lambda v:
                v["phase"] == 1 and v["participant"] != identities[1])
            if replacement["slot"] != 1 or replacement["result"] != winner["result"]:
                raise RuntimeError("late joining replacement changed the departed winner's result")
            self.assert_last_result("client")
            self.screenshot("client", "late-join")
        self.perform("host", "next match countdown", ["openq4_matchControl action arm_force_ready",
                     "waitMsec 100", "openq4_matchControl action confirm"])
        for role in ROLES:
            fresh = self.view(role, "next-result-cleared", lambda v: v["phase"] in (2, 3))
            if (fresh["result"]["outcome"] != 0 or fresh["result"]["revision"] != 0 or
                    fresh["result"]["winner_participant"] or fresh["result"]["winner_name"]):
                raise RuntimeError("next countdown retained a previous match's result")
        self.stage = "complete"

    def close(self):
        for role, process in self.processes.items():
            if process.poll() is None:
                try:
                    self.inject(role, ["quit"])
                except OSError as error:
                    self.failures.append(f"{role} quit script failed: {error}")
        for role, process in self.processes.items():
            try:
                self.exits[role] = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()  # Only the process created by this fixture.
                process.wait()
                self.exits[role] = -1
        for stream in self.streams:
            stream.close()
        self.decoder.close()

    def report(self):
        if self.stage != "complete" or set(self.exits) != set(ROLES) or any(self.exits.values()):
            self.failures.append(f"incomplete run: stage={self.stage}, exits={self.exits}")
        for role, path in self.logs.items():
            if not path.is_file() or path.stat().st_mtime < self.started:
                self.failures.append(f"missing fresh {role} engine log")
        if len(self.shots) != 7 + int(self.args.rejoin_winner) or any(not p.is_file() or p.stat().st_size <= 18 or
                                      p.stat().st_mtime < self.started for p in self.shots):
            self.failures.append("missing fresh engine screenshots")
        result = {"status": "fail" if self.failures else "pass", "stage": self.stage,
                  "failures": self.failures, "exits": self.exits, "mode": "Duel", "managed": True,
                  "scenario": "leading contestant forfeits; winner stays and FIFO challenger readies again",
                  "renderer": self.args.renderer, "resolution": [self.args.width, self.args.height],
                  "winner_role": self.args.winner_role, "winner_name": self.args.winner_name,
                  "rejoin_winner": self.args.rejoin_winner,
                  "evidence": self.evidence,
                  "logs": {role: str(path) for role, path in self.logs.items()},
                  "screenshots": [str(path) for path in self.shots], "views": self.views}
        (self.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps({key: value for key, value in result.items() if key != "views"}, indent=2), flush=True)
        return int(bool(self.failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-duel-queue-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--port", type=int, default=28921)
    parser.add_argument("--winner-role", choices=("host", "client"), default="host")
    parser.add_argument("--winner-name")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--evidence", type=int, choices=(0, 2), default=2)
    parser.add_argument("--rejoin-winner", action="store_true")
    args = parser.parse_args()
    args.winner_name = args.winner_name or args.winner_role
    if len(args.winner_name.encode("utf-8")) > 64 or any(c in args.winner_name for c in '\n\r"'):
        parser.error("winner name must be a single safely quoted display name of at most 64 UTF-8 bytes")
    if args.rejoin_winner and args.winner_role != "client":
        parser.error("--rejoin-winner requires --winner-role client so the host remains running")
    if not (args.runtime_dir / args.executable_name).is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed Quake 4 assets are required")
    if not 1024 <= args.port <= 65533:
        parser.error("port must reserve three consecutive unprivileged ports")
    run = DuelQueueRun(args)
    try:
        run.run()
    except (RuntimeError, OSError) as error:
        run.failures.append(str(error))
    finally:
        run.close()
    return run.report()


if __name__ == "__main__":
    raise SystemExit(main())
