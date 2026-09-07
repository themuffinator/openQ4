#!/usr/bin/env python3
"""Exercise two-client Match Control and repeated pauses using engine commands."""
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-control-smoke")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28861,
                        help="first of three consecutive ports reserved for this fixture")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--duel", action="store_true", help="exercise per-contestant Duel timeouts")
    mode.add_argument("--role-matrix", action="store_true", help="verify remote observer role grants and revocation")
    parser.add_argument("--menu-budget-ms", type=int, default=3000,
                        help="cold menu activation limit; actual latency is retained in the report")
    parser.add_argument("--timeout", type=int, default=200)
    parser.add_argument("--startup-timeout", type=int, default=90,
                        help="per-client cold map loading and admission budget in seconds")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    args = parser.parse_args()
    if not 1024 <= args.port <= 65533:
        parser.error("port must reserve three consecutive unprivileged ports")
    if args.startup_timeout <= 0:
        parser.error("startup-timeout must be positive")
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file():
        parser.error("a staged runtime is required")
    output.mkdir(parents=True, exist_ok=True)
    if (runtime / "manifest.json").is_file():
        shutil.copy2(runtime / "manifest.json", output / "runtime-manifest.json")
    roles = ("host", "client", "observer") if args.role_matrix else ("host", "client")
    games = {role: output / role / "baseoq4" for role in roles}
    logs = {role: path / "logs/openq4.log" for role, path in games.items()}
    loops, processes, streams, failures, exits = {}, {}, [], [], {}
    command_numbers = {role: 0 for role in roles}
    decoder, observed_views, observed_follows = None, {}, {}
    referee_credential = "mprolesmokeonly"
    for role, game in games.items():
        game.mkdir(parents=True, exist_ok=True)
        loops[role] = game / "control_commands.cfg"
        write_script(loops[role], ["// Waiting for the first operation."])
        # This loop stays immutable. A transient read failure while replacing
        # the selector cannot interrupt reporting or prevent a later quit.
        write_script(game / "control_pump.cfg", ["exec control_commands.cfg", "waitMsec 250",
                     "openq4_reportMPState", "vstr oq4_control_pump"])
        write_script(game / "control_start.cfg", [
            'set oq4_control_cmd_1 "exec control_step_1.cfg"',
            'set oq4_control_pump "exec control_pump.cfg"',
            "echo MP_CONTROL_LOADED", "exec control_pump.cfg"])

    def inject(role: str, actions: list[str]) -> None:
        command_numbers[role] += 1
        number = command_numbers[role]
        step = games[role] / f"control_step_{number}.cfg"
        # Retire each unique command before its actions and arm the next one.
        # Re-reading the selector cannot repeat a camera or match operation.
        write_script(step, [f'set oq4_control_cmd_{number} ""',
                     f'set oq4_control_cmd_{number + 1} "exec control_step_{number + 1}.cfg"', *actions])
        write_script(loops[role], [f"vstr oq4_control_cmd_{number}"])

    def launch(role: str) -> None:
        cvars = {
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(games[role].parent),
            "fs_devpath": str(games[role].parent), "fs_game": "baseoq4", "com_gameMode": "MP",
            "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
            "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": str(args.width),
            "r_customHeight": str(args.height), "r_windowWidth": str(args.width), "r_windowHeight": str(args.height),
            "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "r_renderApi": args.renderer, "com_maxfps": "60", "r_swapInterval": "0", "logFile": "2",
            "logFileName": "logs/openq4.log", "developer": "1", "ui_autoJoin": "1", "ui_spectate": "Play",
            "ui_name": role, "ui_team": "Marine" if role == "host" else "Strogg", "net_allowCheats": "1",
            "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "control_start.cfg",
            "g_autoExecAfterMapLoadDelayMs": "500", "net_port": str(args.port + roles.index(role)),
        }
        if role == "host":
            cvars.update(net_serverDedicated="0", net_LANServer="1", si_pure="0", si_maxPlayers="4",
                         si_gameType="Duel" if args.duel else "Team DM", bot_minPlayers="0",
                         g_matchProfile="competitive_duel" if args.duel else "competitive_tdm", g_gameReviewPause="10")
            if args.role_matrix:
                cvars["g_refPassword"] = referee_credential
        command = [str(exe)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        command += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{args.port}"]
        (output / f"{role}-launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)

    def report(marker: str) -> list[str]:
        return ["echo " + marker, "openq4_matchControl report"]

    started, stage = time.time(), "startup"
    deadline = time.monotonic() + args.timeout
    operation_number = 0
    last_follow_time = 0

    def wait_for(label: str, predicate, timeout: int = 45):
        until = min(deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in logs.items()}
            if re.search(r"FATAL ERROR|ERROR:|Unknown command|openq4_gui(?:Set|Get): unknown|usage: openq4_gui(?:Set|Get)", "\n".join(texts.values())):
                raise RuntimeError(f"engine or diagnostic command failure during {label}")
            if any(process.poll() is not None for process in processes.values()):
                raise RuntimeError(f"a process ended during {label}")
            if predicate(texts):
                return texts
            time.sleep(0.2)
        raise RuntimeError(f"timeout during {label}")

    def perform(role: str, label: str, actions: list[str]) -> str:
        nonlocal operation_number, stage
        stage = label
        operation_number += 1
        begin, end = f"MP_CONTROL_BEGIN_{operation_number}", f"MP_CONTROL_END_{operation_number}"
        inject(role, ["echo " + begin, *actions, "openq4_matchControl report", "echo " + end])
        text = wait_for(label, lambda texts: "\n" + end + " " in texts[role])[role]
        return text.partition("\n" + begin + " ")[2].partition("\n" + end + " ")[0]

    def active_players(texts) -> bool:
        host = texts["host"].rsplit("\nMP_STATE ", 1)[-1]
        remote = texts["client"].rsplit("\nMP_STATE ", 1)[-1]
        return ("MP_CONTROL_LOADED" in texts["client"] and "MP_LOCAL client=1 entity=1 " in remote
                and re.search(r"MP_PLAYER slot=1 .*spectating=0 wantSpectate=0 ingame=1", remote)
                and all(re.search(rf"MP_PLAYER slot={slot} .*spectating=0 wantSpectate=0 ingame=1", host)
                        for slot in (0, 1)))

    def pause_sequence(role: str, kind: str) -> None:
        def view_state(section: str) -> tuple[int, ...]:
            states = re.findall(r"MP_VIEW .*phase=(\d+) round=\d+ pause=(\d+) engine=(\d+) match=(\d+)", section)
            if not states:
                raise RuntimeError("missing accepted pause view")
            return tuple(map(int, states[-1]))

        state = view_state(perform(role, kind, ["openq4_matchControl action " + kind, "waitMsec 1000"]))
        until = min(deadline, time.monotonic() + 25)
        while state[1] != 2 and time.monotonic() < until:
            state = view_state(perform(role, "await pause", ["waitMsec 500"]))
        if state[1] != 2:
            raise RuntimeError(f"{role} pause was not accepted: {state}")
        first = view_state(perform(role, "paused clock start", report("MP_PAUSE_A")))
        # waitMsec follows wall time; a busy server's simulation may advance
        # more slowly. Measure the accepted engine clock, not host wall time.
        until = min(deadline, time.monotonic() + 25)
        while state[2] - first[2] < 3000 and time.monotonic() < until:
            state = view_state(perform(role, "paused clock hold", ["waitMsec 1000"]))
        if state[1] != 2 or state[3] != first[3] or state[2] - first[2] < 3000:
            raise RuntimeError(f"{role} paused clock advanced or timed out: {first}, {state}")
        perform(role, "paused capture", [*report("MP_PAUSE_B"), 'screenshot "screenshots/paused.tga"'])
        state = view_state(perform(role, "resume", ["openq4_matchControl action resume", "waitMsec 1000"]))
        until = min(deadline, time.monotonic() + 25)
        while (state[1] != 0 or state[3] <= first[3]) and time.monotonic() < until:
            state = view_state(perform(role, "await resume", ["waitMsec 1000"]))
        if state[1] != 0 or state[3] <= first[3]:
            raise RuntimeError(f"{role} did not resume gameplay: {state}")
        perform(role, "resumed clock", report("MP_PAUSE_DONE"))

    def capture_view(label: str, expected_roles: int, side: int, visible_sides: set[int], items: bool) -> dict:
        until = min(deadline, time.monotonic() + 20)
        while True:
            perform("observer", label + " accepted view", ["waitMsec 500"])
            view = decoder.decode(games["observer"] / "match-probe/view-2.bin")
            if view["roles"] == expected_roles and view["side"] == side and not view["active"]:
                break
            if time.monotonic() >= until:
                raise RuntimeError(f"{label}: observer identity not accepted: {view}")
        folder = output / "views"
        folder.mkdir(exist_ok=True)
        shutil.copy2(games["observer"] / "match-probe/view-2.bin", folder / (label + ".bin"))
        (folder / (label + ".json")).write_text(json.dumps(view, indent=2), encoding="utf-8")
        observed_views[label] = view
        if view["phase"] != 3 or {v["side"] for v in view["vitals"]} != visible_sides:
            raise RuntimeError(f"{label}: incorrect live vitals disclosure")
        if {f["side"] for f in view["follows"]} != visible_sides or any(not f["selectable"] for f in view["follows"]):
            raise RuntimeError(f"{label}: incorrect selectable follow targets")
        if bool(view["items"]) != items:
            raise RuntimeError(f"{label}: incorrect item timing disclosure")
        menu = perform("observer", label + " capture", [f'screenshot "screenshots/{label}.tga"'])
        if "MP_CONTROL match_follow_visible=1" not in menu:
            raise RuntimeError(f"{label}: live spectator camera controls are hidden")
        section = perform("host", label + " camera permissions", ["openq4_reportMPState"])
        cameras = {int(target): bool(int(allowed)) for target, allowed in
                   re.findall(r"MP_CAMERA observer=2 target=(\d+) allowed=(\d+)", section)}
        if cameras != {0: 0 in visible_sides, 1: 1 in visible_sides, 2: False}:
            raise RuntimeError(f"{label}: server camera permissions disagree with accepted view: {cameras}")
        view["server_camera_permissions"] = cameras
        return view

    def set_broadcaster() -> None:
        roster = perform("host", "current observer row", ["waitMsec 2200"])
        row = re.search(r"MP_CONTROL match_team_rows_item_(\d+)=observer\t", roster)
        if row is None:
            raise RuntimeError("no current observer participant row")
        perform("host", "toggle broadcaster", [
            f"openq4_matchControl set match_team_rows_sel_0 {row[1]}",
            "openq4_matchControl action select_team_row", "openq4_matchControl action broadcaster_set"])

    def follow_camera(label: str, command: str | None, expected: int, permitted: set[int]) -> None:
        nonlocal last_follow_time

        def camera_state(texts: dict[str, str]) -> tuple[int, dict[int, tuple[bool, bool]], int]:
            host = texts["host"].rsplit("\nMP_STATE ", 1)[-1]
            remote = texts["observer"].rsplit("\nMP_STATE ", 1)[-1]
            clock = re.match(r"time=(\d+) ", host)
            cameras = {int(target): (bool(int(allowed)), bool(int(following)))
                       for target, allowed, following in re.findall(
                           r"MP_CAMERA observer=2 target=(\d+) allowed=(\d+) following=(\d+)", host)}
            local = re.search(r"MP_LOCAL client=2 entity=2 .*spectator=(\d+) fake=0 spectating=1 ", remote)
            return int(clock[1]) if clock else -1, cameras, int(local[1]) if local else -1

        # User requests share the attack control's simulation-time cooldown.
        # Wait for that clock instead of assuming wall time advances a busy server.
        texts = wait_for(label + " cooldown", lambda values: camera_state(values)[0] > last_follow_time + 650)
        issued_at = camera_state(texts)[0]
        if command is not None:
            perform("observer", label + " command", [command, "waitMsec 500", "openq4_reportMPState"])

        def accepted(values: dict[str, str]) -> bool:
            server_time, cameras, local = camera_state(values)
            return (server_time >= issued_at + 500 and local == expected
                    and cameras == {slot: (slot in permitted, slot == expected) for slot in range(3)})

        texts = wait_for(label + " camera convergence", accepted, 20)
        last_follow_time, cameras, local = camera_state(texts)
        observed_follows[label] = {"command": command, "server_time": last_follow_time,
                                  "server_cameras": cameras, "remote_target": local}
        (output / "follow-results.json").write_text(json.dumps(observed_follows, indent=2), encoding="utf-8")

    try:
        if args.role_matrix:
            pinned_decoder = runtime / "match-view-decode.exe"
            decoder = MatchViewDecoder(output,
                executable=pinned_decoder if pinned_decoder.is_file() else None)
        launch("host")
        wait_for("host warmup", lambda texts: "MP match phase: 0 -> 1" in texts["host"], args.startup_timeout)
        launch("client")
        wait_for("automatic remote admission", active_players, args.startup_timeout)
        if args.role_matrix:
            launch("observer")
            wait_for("observer admission", lambda texts: "MP_CONTROL_LOADED" in texts["observer"] and
                     re.search(r"MP_PLAYER slot=2 .*spectating=0 wantSpectate=0 ingame=1",
                               texts["observer"].rsplit("\nMP_STATE ", 1)[-1]), args.startup_timeout)
        for role in games:
            # Let the menu's initial join-panel animation finish before using
            # the same outgoing/incoming transition as its Match Control button.
            actions = ["openq4_reportMPState", "openq4_assertMPClientActive",
                       f"openq4_assertMenuActivation {args.menu_budget_ms} game", "waitMsec 1500",
                       "openq4_guiSet desktop::active 1", "openq4_guiSet desktop::dest 21",
                       "GuiEvent hideMain", "GuiEvent chooseCurr", "GuiEvent resetMain0", "waitMsec 1500"]
            for tab, name in enumerate(("status", "teams", "proposals", "rules", "series", "evidence")):
                actions += [f"openq4_guiSet desktop::match_tab {tab}", "waitMsec 350",
                            f'screenshot "screenshots/{name}.tga"']
            actions += ["openq4_guiSet desktop::match_tab 0", *report("MP_CONTROL_INITIALIZED")]
            menu = perform(role, f"{role} panels", actions)
            if "MP_CONTROL match_follow_visible=0" not in menu:
                raise RuntimeError(f"{role}: active player received spectator camera controls")

        # Captain roles occupy declared roster seats. Configure one active
        # seat per side through the real Rules workflow before assigning them.
        roster = "" if args.duel else perform("host", "configure rosters", [
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 1", "openq4_matchControl action rules_stage_field", "waitMsec 500"])
        if not args.duel and not re.search(r"MP_CONTROL match_rule_rows_item_9=[^\n]*\t1\s*\n", roster):
            raise RuntimeError("the roster-size rule was not committed")
        for name in (() if args.duel else roles):
            row = re.search(rf"MP_CONTROL match_team_rows_item_(\d+)={name}\t", roster)
            if row is None:
                raise RuntimeError(f"no accepted participant row for {name}")
            roster = perform("host", f"assign {name} roster role", [
                # Rule changes and role assignment share the two-second
                # privileged-operation cooldown on the operator connection.
                "waitMsec 2200",
                f"openq4_matchControl set match_team_rows_sel_0 {row[1]}",
                "openq4_matchControl action select_team_row",
                "openq4_matchControl set match_role_choice " + ("3" if name == "observer" else "2"),
                "openq4_matchControl action role_assign", "waitMsec 500"])
        recipient = perform("client", "remote contestant view", ["waitMsec 750"])
        expected_recipient = r"MP_VIEW slot=1 side=-1 roles=2 active=1" if args.duel else r"MP_VIEW slot=1 side=1 roles=6 active=1"
        if not re.search(expected_recipient, recipient):
            raise RuntimeError("the remote contestant role was not accepted")
        perform("host", "force ready", ["waitMsec 2200", "openq4_matchControl action arm_force_ready", "waitMsec 100",
                "openq4_matchControl action confirm"])
        wait_for("countdown into gameplay", lambda texts: all("phase=3 " in text.rsplit("\nMP_STATE ", 1)[-1]
                                                              for text in texts.values()))
        if args.role_matrix:
            capture_view("coach", 8, 1, {1}, False)
            follow_camera("coach-free", "openq4_matchControl action follow_free", 2, {1})
            follow_camera("coach-teammate", "follow 1", 1, {1})
            follow_camera("coach-opponent-denied", "follow 0", 1, {1})
            follow_camera("coach-previous", "openq4_matchControl action follow_prev", 1, {1})
            follow_camera("coach-next", "openq4_matchControl action follow_next", 1, {1})
            perform("observer", "leave coach roster", ["openq4_matchControl action roster_leave"])
            capture_view("spectator", 2, -1, set(), False)
            follow_camera("coach-revoked", None, 2, set())
            follow_camera("spectator-direct-denied", "follow 1", 2, set())
            follow_camera("spectator-next-denied", "follow next", 2, set())
            set_broadcaster()
            capture_view("broadcaster", 16, -1, {0, 1}, True)
            follow_camera("broadcaster-next", "follow next", 0, {0, 1})
            follow_camera("broadcaster-next-wrap", "follownext", 1, {0, 1})
            follow_camera("broadcaster-previous", "follow prev", 0, {0, 1})
            follow_camera("broadcaster-direct", "follow 1", 1, {0, 1})
            follow_camera("broadcaster-free", "followfree", 2, {0, 1})
            follow_camera("broadcaster-before-revocation", "follow 0", 0, {0, 1})
            set_broadcaster()
            capture_view("broadcaster-revoked", 2, -1, set(), False)
            follow_camera("broadcaster-revoked", None, 2, set())
            perform("observer", "referee sign in", [
                f'openq4_guiSet match_referee_credential::text "{referee_credential}"',
                "openq4_matchControl action referee_login"])
            capture_view("referee", 32, -1, {0, 1}, True)
            follow_camera("referee-direct", "follow 1", 1, {0, 1})
            pause_sequence("observer", "tech_pause")
            perform("observer", "referee sign out", ["openq4_matchControl action referee_logout"])
            capture_view("referee-revoked", 2, -1, set(), False)
            follow_camera("referee-revoked", None, 2, set())
        pause_sequence("host", "tech_pause")
        pause_sequence("client", "timeout")
        summary_report = ["waitMsec 250", "openq4_guiGet summary_result::text"]
        if not args.duel:
            summary_report += ["openq4_guiGet summ_marine_teamname::text",
                               "openq4_guiGet summ_strogg_teamname::text"]
        perform("host", "operator abort", ["openq4_matchControl action arm_abort", "waitMsec 100",
                "openq4_matchControl action confirm", "waitMsec 2000", *report("MP_CONTROL_ABORTED"),
                *summary_report, 'screenshot "screenshots/review.tga"'])
        for role in roles[1:]:
            perform(role, "remote abort result", [*report("MP_CONTROL_ABORTED"), *summary_report,
                    'screenshot "screenshots/review.tga"'])
        stage = "complete"
    except (RuntimeError, OSError) as error:
        failures.append(str(error))
    finally:
        for role, process in processes.items():
            if process.poll() is None:
                try:
                    inject(role, ["quit"])
                except OSError as error:
                    failures.append(f"{role} quit-script publication failed: {error}")
        for role, process in processes.items():
            try:
                exits[role] = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                exits[role] = -1
        for stream in streams:
            stream.close()
        if decoder is not None:
            decoder.close()
    menu_activation_ms = {}
    for role, path in logs.items():
        log = read_log(path)
        activation = re.search(r"OPENQ4_MENU_ACTIVATION PASS elapsed=(\d+)ms limit=(\d+)ms", log)
        menu_activation_ms[role] = int(activation[1]) if activation else None
        if not activation or int(activation[2]) != args.menu_budget_ms:
            failures.append(f"{role} did not open Match Control within the configured menu budget")
        states = []
        for marker in ("MP_PAUSE_A", "MP_PAUSE_B", "MP_PAUSE_DONE"):
            section = log.partition("\n" + marker + " ")[2]
            found = re.search(r"MP_VIEW .*phase=(\d+) round=\d+ pause=(\d+) engine=(\d+) match=(\d+)", section)
            states.append(tuple(map(int, found.groups())) if found else ())
        if any(not state for state in states):
            failures.append(f"{role} did not complete its pause sequence")
        elif not (states[0][0] == states[1][0] == 3 and states[0][1] == states[1][1] == 2
                  and states[0][3] == states[1][3] and states[1][2] - states[0][2] >= 2500
                  and states[2][1] == 0 and states[2][3] > states[1][3]):
            failures.append(f"{role} pause clocks or resume failed: {states}")
        expected_recipient = r"MP_VIEW slot=1 side=-1 roles=2 active=1" if args.duel else r"MP_VIEW slot=1 side=1 roles=6 active=1"
        if role == "client" and not re.search(r"MP_PAUSE_A.*?" + expected_recipient, log, re.DOTALL):
            failures.append("the remote player did not receive the expected contestant role before requesting its timeout")
        if not re.search(r"MP_CONTROL_ABORTED.*?MP_VIEW .*phase=5 ", log, re.DOTALL):
            failures.append(f"{role} did not observe the typed abort result")
        if "GUI_VALUE summary_result::text=Match aborted" not in log:
            failures.append(f"{role} did not display the authoritative aborted outcome")
        if not args.duel and not all(value in log for value in (
                "GUI_VALUE summ_marine_teamname::text=MARINES",
                "GUI_VALUE summ_strogg_teamname::text=STROGG")):
            failures.append(f"{role} did not show neutral managed team summary headings")
    if stage != "complete" or len(exits) != len(roles) or any(exits.values()):
        failures.append(f"incomplete run: stage={stage}, exits={exits}")
    images = [path for game in games.values() for path in (game / "screenshots").glob("*.tga")]
    if len(images) != (30 if args.role_matrix else 16) or any(p.stat().st_mtime < started or p.stat().st_size <= 18 for p in images):
        failures.append("missing fresh engine screenshots")
    result = {"status": "fail" if failures else "pass", "failures": failures, "stage": stage, "exits": exits,
              "resolution": [args.width, args.height], "renderer": args.renderer, "menu_budget_ms": args.menu_budget_ms,
              "mode": "Duel" if args.duel else "Team DM",
              "role_matrix": args.role_matrix, "observed_views": observed_views,
              "observed_follows": observed_follows,
              "menu_activation_ms": menu_activation_ms, "logs": {r: str(p) for r, p in logs.items()},
              "screenshots": [str(p) for p in images]}
    (output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "observed_views"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
