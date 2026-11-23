#!/usr/bin/env python3
"""Qualify a player's recording of their own view on a joined server (MP-22).

A pure dedicated server with cheats off plays Deathmatch with two bots over
loopback. A hidden client joins, records its view with `record`, plays on for
a while and stops with `stopRecord`; later a second hidden game, with no
server, plays the file back. With cheats off a client may not set console
variables of its own, so each game runs one fixed script from its map load
instead of the step pump the other smokes use. The run must show:

* `record` on a joined server recording the player's view, not refused for
  not hosting, and `mvdStatus` calling it a client view;
* a second `record` under the same name getting a new one, though the pure
  server's file system hides the first from the client;
* `stopRecord` committing `demos/<name>.mvd`, and `mvdInfo` reading it as a
  clean one-player recording with snapshots and the recorder's commands;
* playback as the recorder's own slot, every snapshot accepted by the game,
  the camera refusing to follow anyone else, a rewind that replays from the
  start, and the playback running to its clean end with no warning;
* a screenshot during playback that is not one flat colour.

Peers bind 127.0.0.1 only, so a runtime at a new path raises no firewall
prompt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
import zlib
from pathlib import Path

from mp_map_rotation_smoke import Role
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
MAP = "mp/q4dm1"
DEMO = "clientdemo_smoke"
TEXT_VIEW_ONLY = "This demo was recorded by a player and shows their view only"


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def flat_image(path: Path) -> bool | None:
    """True when an uncompressed or RLE TGA, or any other file, holds one value; None when unreadable."""
    data = path.read_bytes()
    if len(data) < 18:
        return None
    if path.suffix.lower() == ".tga" and data[2] == 2:
        pixels = data[18 + data[0]:]
        return len(set(pixels[i:i + 3] for i in range(0, min(len(pixels), 3 * 200000), 3 * 97))) <= 1
    # compressed formats: a flat image compresses to almost nothing
    return len(set(data[len(data) // 4:])) <= 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28201)
    parser.add_argument("--client-port", type=int, default=28202)
    parser.add_argument("--record-seconds", type=int, default=32)
    parser.add_argument("--timeout", type=int, default=120, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
    args = parser.parse_args()
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game-mp_x64" + (".dll" if os.name == "nt" else ".so")),
    }
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("a complete staged runtime and the retail PK4 assets are required")
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "status": "fail",
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "checks": [], "commands": {}, "exit_codes": {}, "failures": [],
    }
    # The recorder and the viewer share a save folder: the viewer plays the recorder's file.
    games = {"server": output / "server" / "baseoq4", "player": output / "player" / "baseoq4"}
    for game in games.values():
        game.mkdir(parents=True, exist_ok=True)
    server = Role("server", games["server"])
    logs_of = {"server": server.log, "rec": games["player"] / "logs/openq4.log",
               "viewer": games["player"] / "logs/openq4-viewer.log",
               "timer": games["player"] / "logs/openq4-timer.log"}
    write_script(games["server"] / "rot_server_start.cfg", [
        "set bot_minPlayers 0", "set bot_skill 2", "addbot", "addbot", "echo ROT_SERVER_READY", *server.start_lines()])

    def section(name: str, commands: list[str]) -> list[str]:
        return [f"echo MP22_{name}_BEGIN", *commands, f"echo MP22_{name}_END"]

    write_script(games["player"] / "mp22_rec.cfg", [
        "echo MP22_REC_LOADED", "waitMsec 5000",
        *section("RECORD", [f"record {DEMO}", "waitMsec 1000", "mvdStatus"]),
        f"waitMsec {args.record_seconds * 1000}",
        *section("STOP", ["stopRecord", "waitMsec 500", "mvdStatus"]),
        # a second recording under the same name: a pure server hides the first from the file system
        *section("AGAIN", [f"record {DEMO}", "waitMsec 1000", "stopRecord", "waitMsec 500"]),
        "waitMsec 1000", "quit"])
    write_script(games["player"] / "mp22_viewer_boot.cfg", [
        "waitMsec 2000", *section("INFO", [f"mvdInfo {DEMO}"]), *section("FIND", ["demoFind recorder"]),
        f"playMVD {DEMO}"])
    write_script(games["player"] / "mp22_viewer.cfg", [
        "echo MP22_VIEW_LOADED", "waitMsec 3000",
        *section("STATUS", ["mvdStatus", "mvdMarkers"]),
        *section("FOLLOW", ["demoFollow next", "demoFreeRoam"]),
        *section("SHOT", ["screenshot", "waitMsec 1500"]),
        # paused, three frames on, then one back from the keyframe before it
        *section("STEP", ["demoPause 1", "waitMsec 500", "mvdStatus", "demoStep 3", "waitMsec 1500", "mvdStatus",
                          "demoStep -1", "waitMsec 3000", "mvdStatus", "demoPause 0"]),
        *section("SEEK", ["demoSeek 1", "waitMsec 6000", "mvdStatus"]),
        # well past the keyframe at ten seconds, then back to it
        "waitMsec 15000",
        *section("KEYFRAME", ["demoSeek 15", "waitMsec 3000", "mvdStatus"]),
        # the rest plays out; then the game is closed
        f"waitMsec {( args.record_seconds + 30 ) * 1000}", "quit"])
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "0",
        "net_LANServer": "1", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1", "g_autoExecAfterMapLoadDelayMs": "500",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    window = {
        "ui_spectate": "Play", "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0",
        "r_borderless": "0", "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "640", "r_customHeight": "360", "r_windowWidth": "640", "r_windowHeight": "360",
        "com_maxfps": "60", "r_swapInterval": "0", "ui_retained": "0", "fs_savepath": str(games["player"].parent),
        "fs_devpath": str(games["player"].parent), "net_port": str(args.client_port),
    }
    processes, streams = {}, []

    def launch(role: str, executable: Path, cvars: dict[str, str], tail: list[str]) -> None:
        command = [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def logs() -> dict[str, str]:
        return {name: read_log(path) for name, path in logs_of.items()}

    def wait(label: str, predicate, seconds: int | None = None, may_end: tuple[str, ...] = ()) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            texts = logs()
            if re.search(r"FATAL ERROR|Unknown command", "\n".join(texts.values())):
                raise RuntimeError("engine error during " + label)
            if predicate(texts):
                return texts
            ended = [name for name, process in processes.items() if process.poll() is not None and name not in may_end]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            if predicate(texts):
                return texts
            time.sleep(0.25)
        raise RuntimeError("timeout during " + label)

    def part(role: str, name: str, seconds: int | None = None, may_end: tuple[str, ...] = ()) -> str:
        """What one section of a game's script printed, once it has."""
        end = f"\nMP22_{name}_END"
        text = wait(f"{role} {name.lower()}", lambda t: end in t[role], seconds, may_end)[role]
        return text.rpartition(end)[0].rpartition(f"\nMP22_{name}_BEGIN")[2]

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"].append({"name": name, "passed": bool(passed), "detail": detail[-600:]})
        print(("ok   " if passed else "FAIL ") + name, flush=True)
        if not passed:
            report["failures"].append(name)

    def finish(name: str, seconds: int) -> None:
        """The game's script ends with quit."""
        report["exit_codes"][name] = processes.pop(name).wait(timeout=seconds)

    started = time.monotonic()
    try:
        launch("server", paths["dedicated"],
               dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                    logFileName="logs/openq4.log", net_port=str(args.port), net_serverDedicated="1", si_pure="1",
                    si_name="MP22 client demo", si_maxPlayers="8", si_warmup="0", si_timeLimit="0", si_fragLimit="0",
                    si_mapCycle="", g_mapRotation="0", g_autoExecAfterMapLoad="rot_server_start.cfg"),
               ["+spawnServer", MAP])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)

        launch("rec", paths["engine"],
               dict(common | window, logFileName="logs/openq4.log", ui_name="Recorder", ui_autoJoin="1",
                    g_autoExecAfterMapLoad="mp22_rec.cfg"),
               ["+connect", f"127.0.0.1:{args.port}"])
        wait("the recorder joins", lambda t: "\nMP22_REC_LOADED" in t["rec"], args.startup_timeout)
        began = part("rec", "RECORD")
        check("record on a joined server records the player's view",
              f"Recording your view to 'demos/{DEMO}.mvd'" in began and "MVD_RECORDING view=client" in began, began)
        stopped = part("rec", "STOP", args.record_seconds + args.timeout, may_end=("rec",))
        check("stopRecord commits the recording",
              f"Stopped MVD recording (console command): 'demos/{DEMO}.mvd'" in stopped and
              f'MVD_LAST_RESULT state=committed reason=0 final="demos/{DEMO}.mvd"' in stopped, stopped)
        again = part("rec", "AGAIN", args.timeout, may_end=("rec",))
        check("a second recording under the same name gets a new name",
              f"Recording your view to 'demos/{DEMO}_000.mvd'" in again, again)
        finish("rec", 60)
        thumb = games["player"] / "demos" / f"{DEMO}_thumb.png"
        head = thumb.read_bytes()[:24] if thumb.is_file() else b""
        check("the recording leaves a 320x240 thumbnail beside it for the library",
              head[:8] == b"\x89PNG\r\n\x1a\n" and int.from_bytes(head[16:20], "big") == 320 and
              int.from_bytes(head[20:24], "big") == 240, str(thumb))
        server.inject(["quit"])
        report["exit_codes"]["server"] = processes.pop("server").wait(timeout=60)

        launch("viewer", paths["engine"],
               dict(common | window, logFileName="logs/openq4-viewer.log", g_autoExecAfterMapLoad="mp22_viewer.cfg"),
               ["+exec", "mp22_viewer_boot.cfg"])
        # Read where no pure server hides loose files: the recorder's own file system could not.
        described = part("viewer", "INFO", args.startup_timeout)
        info = re.search(r"records: (\d+), snapshots: (\d+), reliables: (\d+)[\s\S]*?duration: ([\d.]+) seconds, "
                         r"clean end: (\w+)[\s\S]*?view: ([^,]+), recorder's commands: (\d+)", described)
        report["info"] = info.groups() if info else None
        check("mvdInfo reads a clean one-player recording with snapshots and the recorder's commands",
              info is not None and int(info[2]) > 50 and info[5] == "yes" and info[6].startswith("one player's") and
              int(info[7]) > 100 and float(info[4]) >= args.record_seconds - 2, described[-700:])
        found = part("viewer", "FIND", args.startup_timeout)
        check("demoFind finds a recording by its recorder, and the library knows whose view it is",
              re.search(rf"DEMO_FIND demos/{DEMO}\.mvd\s+mp/q4dm1\s+DM\s+view of Recorder", found) is not None, found[-600:])
        texts = wait("playback starts", lambda t: "\nMP22_VIEW_LOADED" in t["viewer"], args.startup_timeout)
        playing = re.search(r"Playing the recorded view of client (\d+) 'demos/" + DEMO + r"\.mvd'", texts["viewer"])
        check("playMVD plays the recording as its recorder's view", playing is not None, texts["viewer"][-600:])
        status = part("viewer", "STATUS")
        view = re.search(r"MVD_VIEW client=(\d+) sequence=(\d+) snapshots=(\d+)", status)
        listed = re.search(r"markers: (\d+)", described)
        check("a client recording marks what its game saw: kills and the match's changes",
              listed is not None and int(listed[1]) >= 1 and re.search(r"\bkill\b", status) is not None, status[-600:])
        check("the game takes the recorded snapshots as the recorder's client",
              view is not None and playing is not None and view[1] == playing[1] and int(view[3]) > 20, status)
        refused = part("viewer", "FOLLOW")
        check("the camera stays on the recorder", refused.count(TEXT_VIEW_ONLY) == 2, refused)
        shot = part("viewer", "SHOT")
        found = re.search(r"Wrote (\S+)", shot)
        image = None
        if found:
            candidates = list(games["player"].rglob(Path(found[1]).name))
            image = candidates[0] if candidates else None
        flat = flat_image(image) if image else None
        report["screenshot"] = str(image) if image else shot[-300:]
        check("a screenshot of the playback is not one flat colour", flat is False, report["screenshot"])
        stepped = part("viewer", "STEP", args.timeout)
        times = [int(value) for value in re.findall(r"MVD_STATUS playing=1 paused=1 seeking=0 time=(\d+)", stepped)]
        report["steps"] = times
        check("demoStep moves a paused demo by whole frames, three on and one back",
              len(times) == 3 and times[1] - times[0] == 48 and times[2] - times[1] == -16, json.dumps(times))
        rewound = part("viewer", "SEEK")
        after = re.search(r"MVD_STATUS playing=1 paused=0 seeking=0 time=(\d+)", rewound)
        check("a rewind replays from the start and plays on", after is not None and int(after[1]) >= 1000, rewound)
        keyed = part("viewer", "KEYFRAME", args.timeout)
        restored = re.search(r"MVD_KEYFRAME restored time=(\d+)", keyed)
        landed = re.search(r"MVD_STATUS playing=1 paused=0 seeking=0 time=(\d+)", keyed)
        check("a rewind past a keyframe restores it instead of replaying from the start",
              restored is not None and 8000 <= int(restored[1]) <= 15000 and landed is not None and int(landed[1]) >= 15000,
              keyed)
        ended = wait("playback ends", lambda t: f"Stopped MVD playback of 'demos/{DEMO}.mvd'" in t["viewer"],
                     args.record_seconds * 2 + args.timeout, may_end=("viewer",))["viewer"]
        check("playback runs to its clean end with no warning",
              f"Stopped MVD playback of 'demos/{DEMO}.mvd' (complete)" in ended and "MVD playback stopped:" not in ended,
              ended[-800:])
        finish("viewer", args.record_seconds + 60)

        # timeDemo: one simulation tic a frame, uncapped, then quit
        write_script(games["player"] / "mp22_timer.cfg", ["waitMsec 2000", f"timeDemoQuit {DEMO}.mvd"])
        launch("timer", paths["engine"], dict(common | window, logFileName="logs/openq4-timer.log"), ["+exec", "mp22_timer.cfg"])
        timed = wait("the timedemo", lambda t: "MVD_TIMEDEMO" in t["timer"], args.startup_timeout + args.record_seconds,
                     may_end=("timer",))["timer"]
        result = re.search(r"MVD_TIMEDEMO frames=(\d+) seconds=([\d.]+) fps=([\d.]+) slowest=(\d+)ms match=([\d.]+)s complete=(\d)",
                           timed)
        report["timedemo"] = result.groups() if result else None
        check("timeDemo plays the recording one tic a frame to its end and reports the frame rate",
              result is not None and result[6] == "1" and int(result[1]) >= float(result[5]) * 50 and float(result[3]) > 0,
              timed[-500:])
        finish("timer", 60)

        # demoExport: a PNG frame each tenth of a second of the match, then quit
        logs_of["exporter"] = games["player"] / "logs/openq4-exporter.log"
        write_script(games["player"] / "mp22_exporter.cfg", ["waitMsec 2000", f"demoExport {DEMO}.mvd 10 quit"])
        launch("exporter", paths["engine"], dict(common | window, logFileName="logs/openq4-exporter.log"),
               ["+exec", "mp22_exporter.cfg"])
        exported = wait("the export", lambda t: "MVD_EXPORT" in t["exporter"], args.startup_timeout + args.record_seconds * 3,
                        may_end=("exporter",))["exporter"]
        finish("exporter", 120)
        result = re.search(r"MVD_EXPORT frames=(\d+) directory=(\S+) fps=(\d+) complete=(\d)", exported)
        frames = sorted((games["player"] / "exports" / DEMO).glob("frame*.png"))
        report["export"] = {"line": result.groups() if result else None, "files": len(frames)}
        check("demoExport writes the recording as a PNG frame a tenth of a second",
              result is not None and result[4] == "1" and result[3] == "10" and len(frames) == int(result[1]) and
              len(frames) >= args.record_seconds * 9 and frames[0].read_bytes()[:8] == b"\x89PNG\r\n\x1a\n",
              json.dumps(report["export"]))

        # A recording cut short, and one whose game data differs from this game's
        demo = (games["player"] / "demos" / f"{DEMO}.mvd").read_bytes()
        (games["player"] / "demos" / "cut.mvd.part").write_bytes(demo[: len(demo) * 6 // 10])
        header_bytes = int.from_bytes(demo[8:12], "little")
        changed = bytearray(demo)
        content = int.from_bytes(changed[52:56], "little")
        changed[52:56] = ((content + 1) & 0xffffffff).to_bytes(4, "little")
        changed[header_bytes - 4:header_bytes] = zlib.crc32(bytes(changed[:header_bytes - 4])).to_bytes(4, "little")
        (games["player"] / "demos" / "mismatch.mvd").write_bytes(bytes(changed))
        logs_of["checker"] = games["player"] / "logs/openq4-checker.log"
        write_script(games["player"] / "mp22_checker.cfg", [
            "waitMsec 2000", *section("RECOVER", ["mvdRecover cut.mvd.part", "mvdInfo cut_recovered.mvd"]),
            *section("MISMATCH", ["playMVD mismatch", "waitMsec 1000"]), "quit"])
        launch("checker", paths["engine"], dict(common | window, logFileName="logs/openq4-checker.log"),
               ["+exec", "mp22_checker.cfg"])
        recovered = part("checker", "RECOVER", args.startup_timeout, may_end=("checker",))
        check("mvdRecover saves the complete part of a client-view recording cut short",
              "clean end: yes" in recovered and "view: one player's" in recovered, recovered[-600:])
        refused = part("checker", "MISMATCH", args.timeout, may_end=("checker",))
        check("a recording with other game data is refused and the packs are compared",
              "MVD content checksum" in refused and "Every pack matches the recording's" in refused, refused[-600:])
        finish("checker", 60)
    except Exception as error:  # noqa: BLE001 - the report records the failure
        report["failures"].append(f"exception: {error}")
        print(f"FAIL {error}", flush=True)
    finally:
        for name, process in list(processes.items()):
            if process.poll() is None:
                process.kill()  # Only the processes this test launched.
                process.wait()
        for stream in streams:
            stream.close()
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["seconds"] = round(time.monotonic() - started, 1)
    report["status"] = "pass" if not report["failures"] else "fail"
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": report["failures"], "seconds": report["seconds"]}, indent=2), flush=True)
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
