#!/usr/bin/env python3
"""Play the network demos Quake 4 1.4.2 ships, in openQ4 (MP-22).

Quake 4 1.4.2 installs two network demos it recorded itself in q4mp/demos:
id_perftest.ndmo85, one player's view of a nine-player match on q4dm7, and
id_serverdemo.ndmo85, a server's recording of the same match. This smoke plays
them with the staged runtime, hidden, from a save path of its own, driven only
by console scripts (never input):

* client: the demo found by `mvdInfo` and `demoFind`, listed playable in the
  Demos library and started from the library's Play action; the recorded
  player's view (the camera refuses to follow anyone else), seeking forward, a
  rewind that replays from the start, paused frame steps, a screenshot that is
  not one flat colour, and playback to its clean end;
* server: `playNetDemo`; following a player and the free camera, to the end;
* timedemo: `timeDemoQuit` timing the server demo to its end;
* damaged: copies of both demos each with one damaged length or field
  (retail_netdemo_contract.damaged_variants), every one described and played:
  each must be refused, or stopped with a warning, and the game must carry on
  to its scripted quit with no fatal error;
* numbering (when Quake4Ded.exe is present): the decl numbering the engine
  rebuilds for a retail recording, entity definitions, effects, sounds,
  materials and skins, compared with the lists the retail dedicated server
  itself prints.

The engine must read each demo with the packet, snapshot and reliable-message
counts the contract's independent reader finds. Peers bind 127.0.0.1 only and
nothing listens, so no firewall prompt appears.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script  # noqa: E402
from retail_netdemo_contract import DEFAULT_BASEPATH, FIXTURES, Refused, damaged_variants, decode, parse  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
CLIENT, SERVER = "id_perftest.ndmo85", "id_serverdemo.ndmo85"
TEXT_VIEW_ONLY = "This demo was recorded by a player and shows their view only"
LEGACY_NET = 4  # demoLibraryType_t DEMO_LIBRARY_LEGACY_NET
CAP_PLAY, CAP_PAUSE, CAP_RATE, CAP_SEEK, CAP_STEP, CAP_FREE_ROAM, CAP_FOLLOW = 1, 2, 4, 8, 16, 32, 64
BAD_LINES = re.compile(r"FATAL ERROR|^ERROR:|ClientReadSnapshot: |game module rejected|MVD playback stopped", re.M)


def flat_image(path: Path) -> bool | None:
    data = path.read_bytes()
    if len(data) < 18:
        return None
    if path.suffix.lower() == ".tga" and data[2] == 2:
        pixels = data[18 + data[0]:]
        return len(set(pixels[i:i + 3] for i in range(0, min(len(pixels), 3 * 200000), 3 * 97))) <= 1
    return len(set(data[64::97])) <= 1


def section(name: str, commands: list[str]) -> list[str]:
    return [f"echo RND_{name}_BEGIN", *commands, f"echo RND_{name}_END"]


def part(text: str, name: str) -> str:
    match = re.search(rf"RND_{name}_BEGIN(.*?)RND_{name}_END", text, re.S)
    return match.group(1) if match else ""


def statuses(text: str) -> list[dict[str, int]]:
    return [{k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", m)} for m in re.findall(r"MVD_STATUS ([^\n]*)", text)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--runtime", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, default=DEFAULT_BASEPATH, help="Quake 4 1.4.2 installation")
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp" / "mp-retail-netdemo-smoke")
    parser.add_argument("--timeout", type=int, default=420, help="seconds each game may take")
    parser.add_argument("--only", help="comma-separated runs: client,server,timedemo,damaged,numbering")
    args = parser.parse_args()

    runtime = args.runtime.resolve()
    exe = runtime / f"openQ4-client_x64{SUFFIX}"
    fixtures = {name: args.basepath / "q4mp" / "demos" / name for name in FIXTURES}
    if not exe.is_file():
        parser.error(f"no staged runtime at {runtime}")
    missing = [str(path) for path in fixtures.values() if not path.is_file()]
    if missing:
        print(f"mp_retail_netdemo_smoke: SKIP: no Quake 4 1.4.2 demos ({', '.join(missing)})")
        return 0
    verify_runtime_is_current(runtime)
    selected = set((args.only or "client,server,timedemo,damaged,numbering").split(","))
    output = args.output.resolve()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    report: dict[str, object] = {"checks": [], "runs": {}}
    failures: list[str] = []

    def check(label: str, ok: bool, detail: str = "") -> None:
        report["checks"].append({"check": label, "ok": bool(ok), "detail": detail[-1500:]})
        if not ok:
            failures.append(label)
        print(f"{'PASS' if ok else 'FAIL'}: {label}" + ("" if ok else f"\n    {detail[-600:]}"))

    # what the contract's own reader finds in each demo
    expected = {}
    for name, path in fixtures.items():
        data = path.read_bytes()
        demo = parse(data)
        decode(data, demo)
        expected[name] = (len(demo.packets), demo.snapshots, demo.reliables)

    def game(run: str, demos: dict[str, bytes], boot: list[str], view: list[str] | None,
             extra: dict[str, str] | None = None) -> tuple[str, int | None]:
        save = output / run / "save"
        folder = save / "baseoq4"
        (folder / "demos").mkdir(parents=True)
        for name, blob in demos.items():
            (folder / "demos" / name).write_bytes(blob)
        write_script(folder / "rnd_boot.cfg", boot)
        cvars = {
            "fs_basepath": str(args.basepath), "fs_game": "baseoq4", "fs_savepath": str(save), "fs_devpath": str(save),
            "com_gameMode": "MP", "com_nextGameModule": "game_mp", "logFile": "2", "logFileName": "logs/openq4.log",
            "developer": "1", "s_noSound": "1", "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1",
            "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "960",
            "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540", "com_maxfps": "60",
            "r_swapInterval": "0", "ui_retained": "1", "ui_autoJoin": "0", "net_ip": "127.0.0.1",
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        }
        if view is not None:
            write_script(folder / "rnd_view.cfg", view)
            cvars |= {"g_autoExecAfterMapLoad": "rnd_view.cfg", "g_autoExecAfterMapLoadDelayMs": "500"}
        cvars |= extra or {}
        command = [str(exe)] + [p for k, v in cvars.items() for p in ("+set", k, v)] + ["+exec", "rnd_boot.cfg"]
        started = time.monotonic()
        with (output / run / "console.log").open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=30)
                code = None
        log = read_log(folder / "logs" / "openq4.log")
        report["runs"][run] = {"exit": code, "seconds": round(time.monotonic() - started, 1), "log": str(folder / "logs" / "openq4.log")}
        return log, code

    def read_line(log: str, kind: str, name: str) -> tuple[int, int, int] | None:
        match = re.search(rf"Read Quake 4 1\.4\.2 {kind} demo 'demos/{re.escape(name)}': (\d+) packets, (\d+) snapshots, (\d+) reliable", log)
        return tuple(int(x) for x in match.groups()) if match else None

    if "client" in selected:
        boot = ["echo RND_BOOT", "waitMsec 1500", *section("INFO", [f"mvdInfo {CLIENT}"]),
                *section("FIND", ["demoFind SyncError"]), "demoMenu", "waitMsec 1000",
                *section("LIBRARY", ["openq4_demoLibrary report"]), "openq4_demoLibrary play"]
        view = ["echo RND_LOADED", "waitMsec 6000", *section("STATUS", ["mvdStatus", 'screenshot "screenshots/client.tga"', "waitMsec 1000"]),
                *section("FOLLOW", ["demoFollow next", "demoFreeRoam"]),
                *section("SEEK", ["demoSeek 60", "waitMsec 10000", "mvdStatus"]),
                *section("REWIND", ["demoSeek 20", "waitMsec 15000", "mvdStatus"]),
                *section("STEP", ["demoPause 1", "waitMsec 500", "mvdStatus", "demoStep 3", "waitMsec 1500", "mvdStatus", "demoPause 0"]),
                "demoSpeed 8", "waitMsec 40000", "echo RND_CLIENT_DONE", "quit"]
        log, code = game("client", {CLIENT: fixtures[CLIENT].read_bytes()}, boot, view)
        info = part(log, "INFO")
        check("mvdInfo describes the client demo as Quake 4 1.4.2's and playable",
              "version 12, protocol 2.85, recorded by" in info and "playable: yes" in info and "mp/q4dm7" in info, info)
        found = part(log, "FIND")
        check("demoFind finds it by its recorder", re.search(rf"DEMO_FIND demos/{CLIENT}\s+mp/q4dm7\s+DM\s+view of", found) is not None, found)
        library = part(log, "LIBRARY")
        entry = re.search(rf"DEMO_LIBRARY_ENTRY index=\d+ type=(\d+) playable=(\d) capabilities=(\d+) path=demos/{re.escape(CLIENT)}", library)
        caps = int(entry.group(3)) if entry else 0
        check("the Demos library lists it playable with the controls a recorded view has",
              entry is not None and int(entry.group(1)) == LEGACY_NET and entry.group(2) == "1" and
              caps & (CAP_PLAY | CAP_PAUSE | CAP_RATE | CAP_SEEK | CAP_STEP) == (CAP_PLAY | CAP_PAUSE | CAP_RATE | CAP_SEEK | CAP_STEP) and
              caps & (CAP_FREE_ROAM | CAP_FOLLOW) == 0, library)
        read = read_line(log, "client", CLIENT)
        check("the engine reads the client demo as the contract's reader does", read == expected[CLIENT], f"{read} vs {expected[CLIENT]}")
        check("the library's Play action plays it as the recorder's view", re.search(r"Playing the recorded view of client 2 'demos/" + re.escape(CLIENT), log) is not None, log[-2000:])
        status = statuses(part(log, "STATUS"))
        views = [int(v) for v in re.findall(r"MVD_VIEW client=2 sequence=\d+ snapshots=(\d+)", log)]
        check("snapshots arrive and the clock runs", bool(status) and status[0].get("playing") == 1 and status[0].get("time", 0) > 3000 and
              bool(views) and views[0] > 50, part(log, "STATUS"))
        check("the camera stays on the recorder", part(log, "FOLLOW").count(TEXT_VIEW_ONLY) == 2, part(log, "FOLLOW"))
        seek = statuses(part(log, "SEEK"))
        check("seeking forward plays on from there", bool(seek) and seek[-1].get("time", 0) >= 60000, part(log, "SEEK"))
        rewind = statuses(part(log, "REWIND"))
        check("a rewind replays from the start to the earlier time", bool(rewind) and 20000 <= rewind[-1].get("time", 0) <= 40000, part(log, "REWIND"))
        steps = [s.get("time", 0) for s in statuses(part(log, "STEP"))]
        check("a paused demo steps by whole frames", len(steps) == 2 and steps[1] - steps[0] == 48, json.dumps(steps))
        shots = list((output / "client").rglob("client.tga"))
        check("a screenshot of the playback is not one flat colour", bool(shots) and flat_image(shots[0]) is False, str(shots))
        check("playback runs to its clean end", f"Stopped MVD playback of 'demos/{CLIENT}' (complete)" in log, log[-1500:])
        bad = BAD_LINES.findall(log)
        check("no snapshot is rejected and no error is raised", not bad, "\n".join(bad[:10]))
        check("the game quits from its script", code == 0 and "RND_CLIENT_DONE" in log, f"exit {code}")

    if "server" in selected:
        boot = ["echo RND_BOOT", "waitMsec 1500", f"playNetDemo {SERVER}"]
        view = ["echo RND_LOADED", "waitMsec 4000", *section("FREE", ["mvdStatus"]),
                *section("FOLLOW", ["demoFollow next", "waitMsec 1500", "mvdStatus", 'screenshot "screenshots/server.tga"', "waitMsec 1000"]),
                *section("ROAM", ["demoFreeRoam", "waitMsec 1000", "mvdStatus"]),
                "demoSpeed 4", "waitMsec 25000", "echo RND_SERVER_DONE", "quit"]
        extra = {"net_retailDemoDebug": "2"} if "numbering" in selected else {}
        log, code = game("server", {SERVER: fixtures[SERVER].read_bytes()}, boot, view, extra)
        read = read_line(log, "server", SERVER)
        check("the engine reads the server demo as the contract's reader does", read == expected[SERVER], f"{read} vs {expected[SERVER]}")
        free, follow, roam = (statuses(part(log, s)) for s in ("FREE", "FOLLOW", "ROAM"))
        check("a server demo starts on the free camera", bool(free) and free[-1].get("follow") == -1 and free[-1].get("playing") == 1, part(log, "FREE"))
        check("the camera follows a player", bool(follow) and 0 <= follow[-1].get("follow", -1) < 32, part(log, "FOLLOW"))
        check("and returns to the free camera", bool(roam) and roam[-1].get("follow") == -1, part(log, "ROAM"))
        shots = list((output / "server").rglob("server.tga"))
        check("a screenshot of a followed player is not one flat colour", bool(shots) and flat_image(shots[0]) is False, str(shots))
        check("the server demo plays to its clean end", f"Stopped MVD playback of 'demos/{SERVER}' (complete)" in log, log[-1500:])
        bad = BAD_LINES.findall(log)
        check("no server snapshot is rejected and no error is raised", not bad, "\n".join(bad[:10]))
        check("the server game quits from its script", code == 0 and "RND_SERVER_DONE" in log, f"exit {code}")
        if "numbering" in selected:
            report["server_log"] = log

    if "timedemo" in selected:
        log, code = game("timedemo", {SERVER: fixtures[SERVER].read_bytes()}, ["waitMsec 1500", f"timeDemoQuit {SERVER}"], None)
        timed = re.search(r"MVD_TIMEDEMO frames=(\d+) seconds=([\d.]+) fps=([\d.]+) slowest=(\d+)ms match=([\d.]+)s complete=(\d)", log)
        report["timedemo"] = timed.groups() if timed else None
        check("timeDemoQuit times the server demo to its end, a tic a frame",
              timed is not None and timed.group(6) == "1" and int(timed.group(1)) >= float(timed.group(5)) * 55 and code == 0,
              log[-800:])

    if "damaged" in selected:
        variants = []
        for name, path in fixtures.items():
            variants += damaged_variants(name, path.read_bytes())
        boot = ["echo RND_BOOT", "waitMsec 1500"]
        for name, _, _ in variants:
            wait = "waitMsec 20000" if "payload" in name else "waitMsec 300"
            boot += [f"echo RND_TRY {name}", f"mvdInfo {name}", f"playNetDemo {name}", wait, "stopNetDemo"]
        boot += ["demoFind bad_", "echo RND_DAMAGED_DONE", "quit"]
        log, code = game("damaged", {name: blob for name, blob, _ in variants}, boot, None)
        check("every damaged demo is met, the library scan included, and the game quits from its script",
              code == 0 and "RND_DAMAGED_DONE" in log and "FATAL" not in log, f"exit {code}\n{log[-1500:]}")
        for name, _, what in variants:
            if "payload" in name:
                # the container is whole: refused at decode, or stopped where the game meets it
                handled = f"Cannot play 'demos/{name}'" in log or f"Stopped MVD playback of 'demos/{name}'" in log
                check(f"a demo with {what} stops cleanly ({name})", handled, "")
                continue
            if "messagesize" in name:
                # damage inside a packet: the container reads (mvdInfo describes it),
                # and the channel decode refuses it before playback starts
                refused = f"Cannot play 'demos/{name}': packet 0 holds a damaged message" in log
            else:
                refused = f"Cannot play 'demos/{name}'" in log and f"Invalid network demo 'demos/{name}'" in log
            check(f"a demo with a damaged {what} is refused before any game code reads it ({name})", refused, "")

    if "numbering" in selected:
        ded = args.basepath / "Quake4Ded.exe"
        log = report.pop("server_log", "")
        if not ded.is_file() or not log:
            print("SKIP: retail numbering (no Quake4Ded.exe, or the server run did not run)")
        else:
            save = output / "numbering" / "retail-save"
            save.mkdir(parents=True)
            command = [str(ded), "+set", "fs_basepath", str(args.basepath).lower(), "+set", "fs_savepath", str(save).lower(),
                       "+set", "fs_game", "q4mp", "+set", "logFile", "2", "+set", "net_ip", "127.0.0.1", "+set", "net_port", "28999",
                       "+wait", "+listEntityDefs", "all", "+listEffects", "all", "+listSoundShaders", "all", "+listMaterials", "all",
                       "+listSkins", "all", "+wait", "+quit"]
            startup = None
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            subprocess.run(command, cwd=args.basepath, timeout=180, startupinfo=startup, capture_output=True)
            retail_log = read_log(save / "q4base" / "qconsole.log")
            lists, current = {}, []
            for line in retail_log.splitlines():
                m = re.match(r"^[\s*]*(\d+):\s+(?:\d+:\s+)?(\S+)", line)
                if m:
                    current.append(m.group(2))
                    continue
                m = re.match(r"^(\d+) of (\d+) (\w+)", line)
                if m:
                    lists[m.group(3)] = current
                    current = []
            for kind in ("entityDef", "effect", "sound", "material", "skin"):
                engine = re.findall(rf"^retail {kind} \d+ = \d+ (\S+) ", log, re.M)
                retail = lists.get(kind, [])
                same = len(engine) > 0 and len(retail) > 0 and [n.lower() for n in engine[:len(retail)]] == [n.lower() for n in retail[:len(engine)]]
                check(f"the rebuilt {kind} numbering is retail Quake 4's ({len(engine)} rebuilt, {len(retail)} listed by Quake4Ded)", same,
                      f"engine {engine[:5]} retail {retail[:5]}")

    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"mp_retail_netdemo_smoke: {'FAIL' if failures else 'ok'} "
          f"({len(report['checks']) - len(failures)}/{len(report['checks'])} checks; report {output / 'report.json'})")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refused as error:
        print(f"mp_retail_netdemo_smoke: FAIL: the contract reader refused a shipped demo: {error}")
        sys.exit(1)
