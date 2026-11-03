#!/usr/bin/env python3
"""Move saves between platforms and check that each restores (roadmap Q4-06 stage 4).

A portable save (payload version 5, wire ABI ``le64-portable1``) must restore on
every 64-bit little-endian build. This opt-in runtime test needs the retail
Quake 4 PK4s. It runs one *leg* on the local platform, and a driver moves the
files between platforms (WSL, SSH or a shared folder) and runs the legs in turn:

* ``write`` starts ``--map`` fresh, plays ``--settle-frames`` and saves slot
  ``<tag>``.
* ``load`` copies an input save in as ``<tag>_in`` and restores it with game
  time stopped (``g_stopTime 1``). It saves ``<tag>_frozen`` before any game time
  passes, so that save holds exactly what the restore rebuilt. It then resumes
  time, plays ``--play-frames`` tics and saves ``<tag>``, the resave that
  travels back to the source platform.

The frozen save runs before the first game frame after the load. It must be
loadable too: use it as another load leg's input to exercise immediate resaves.
Every leg runs with ``com_fixedTic 1``, one game tic per frame, so frame counts
are game time on any platform.

A leg passes when the client loaded the map or restored the save without an
error, ran its after-load config to the end, and wrote every save as payload
version 5 stamped ``le64-portable1``. Every save, including the frozen save,
must log no unresolved reference, and a
load leg's frozen save must begin with its input's object table: the class of
every saved object, in order. Objects the restore itself creates (an Awakening
level restarts some looping client effects) may follow; the report counts them
by class. Each leg writes ``report.json`` beside its client log.

``compare A B`` compares two saves: their object tables, and their payloads byte
for byte after the build stamp. Comparing the frozen saves two platforms wrote
from the same input shows whether both rebuilt the same objects and how closely
they re-encode the same state.

Saves are player data: a leg reads its input and writes only below ``--home``,
which must not contain the input. ``--self-test`` checks the payload parser and
the comparisons on synthetic saves, without a client.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path
from typing import Any


PAYLOAD_MAGIC = b"OQ4S"
INTEGRITY_BYTES = 16
PORTABLE_VERSION = 5
PORTABLE_WIRE_ABI = "le64-portable1"
LOG_NAME = "savegame-cross-load.log"
CFG_REL = "savegame_cross_load/after_load.cfg"
DONE_MARKER = "OPENQ4_CROSS_LOAD_DONE"

ERROR_PATTERN = re.compile(r"^(?:\^[0-9])?(?:FATAL )?ERROR:.*$", re.MULTILINE)
SIGNAL_PATTERN = re.compile(r"signal caught|segmentation fault|Sys_Error:", re.IGNORECASE)
RESTORE_STARTED = re.compile(r"Game Map Init SaveGame")
MAP_STARTED = re.compile(r"-+ Game Map Init -+|Game Map Init SaveGame")
PREFLIGHT_REJECTED = re.compile(r"WARNING:.*Savegame '[^']+' (?:has|was|wire|is|failed)[^\r\n]*")
UNRESOLVED_REFERENCE = re.compile(r"WriteObject FindIndex failed")
# A restore that fails after map teardown returns to the Load Game page, where
# the client would wait for input until the timeout.
RESTORE_FAILED = re.compile(r"could not restore saved state[^\r\n]*")
RESAVE_MARKER = "OPENQ4_CROSS_LOAD_RESAVE"


class Payload:
    """The openQ4 save payload of a .save file: its stamp and object table."""

    def __init__(self, data: bytes) -> None:
        offset = data.find(PAYLOAD_MAGIC)
        if offset < 0:
            raise ValueError("no openQ4 payload stamp")
        self.data = data
        self.offset = offset
        self.position = offset + 4
        self.version = self.integer()
        self.build = self.integer()
        self.source = self.string()
        self.source_files = self.integer()
        self.wire_abi = self.string() if self.version >= 3 else ""
        # Everything after the stamp is the saved state; the stamp itself names the
        # build that wrote it, which legitimately differs between platforms.
        self.state_offset = self.position
        count = self.integer()
        if not 0 <= count <= 1 << 20:
            raise ValueError(f"invalid object count {count}")
        self.objects = [self.string() for _ in range(count)]

    def integer(self) -> int:
        if self.position + 4 > len(self.data):
            raise ValueError("payload truncated")
        value = struct.unpack_from("<i", self.data, self.position)[0]
        self.position += 4
        return value

    def string(self) -> str:
        length = self.integer()
        if not 0 <= length <= 4096 or self.position + length > len(self.data):
            raise ValueError(f"invalid string length {length}")
        text = self.data[self.position:self.position + length].decode("latin-1")
        self.position += length
        return text

    def state(self) -> bytes:
        """The saved state, without the stamp or the integrity trailer (its CRC covers the stamp)."""
        return self.data[self.state_offset:len(self.data) - INTEGRITY_BYTES]

    def summary(self) -> dict[str, Any]:
        return {"version": self.version, "build": self.build, "wireAbi": self.wire_abi,
                "objects": len(self.objects), "bytes": len(self.data)}


def portable(payload: Payload) -> bool:
    return payload.version == PORTABLE_VERSION and payload.wire_abi == PORTABLE_WIRE_ABI


def compare(a: Payload, b: Payload) -> dict[str, Any]:
    """Object tables and saved state of two payloads."""
    first_table = next((i for i, (x, y) in enumerate(zip(a.objects, b.objects)) if x != y), None)
    if first_table is None and len(a.objects) != len(b.objects):
        first_table = min(len(a.objects), len(b.objects))
    state_a, state_b = a.state(), b.state()
    differing = sum(x != y for x, y in zip(state_a, state_b)) + abs(len(state_a) - len(state_b))
    first_byte = next((i for i, (x, y) in enumerate(zip(state_a, state_b)) if x != y), None)
    if first_byte is None and len(state_a) != len(state_b):
        first_byte = min(len(state_a), len(state_b))
    return {
        "sameObjectTable": first_table is None,
        "firstTableDifference": None if first_table is None else {
            "index": first_table,
            "a": a.objects[first_table] if first_table < len(a.objects) else None,
            "b": b.objects[first_table] if first_table < len(b.objects) else None,
        },
        "objects": [len(a.objects), len(b.objects)],
        "identicalState": differing == 0,
        "stateBytes": [len(state_a), len(state_b)],
        "differingBytes": differing,
        "firstDifferingByte": first_byte,
    }


def restore_created(saved: Payload, restored: Payload) -> dict[str, int] | None:
    """Objects a restore appended after rebuilding every saved one, by class; None if it lost or moved any."""
    if restored.objects[:len(saved.objects)] != saved.objects:
        return None
    created: dict[str, int] = {}
    for name in restored.objects[len(saved.objects):]:
        created[name] = created.get(name, 0) + 1
    return dict(sorted(created.items()))


def client_path(install_root: Path) -> Path:
    machine = platform.machine().lower()
    arch = "arm64" if machine in ("arm64", "aarch64") else "x64"
    if sys.platform == "darwin":
        return install_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4"
    return install_root / f"openQ4-client_{arch}{'.exe' if sys.platform == 'win32' else ''}"


def stage_input(source: Path, savegames: Path, slot: str) -> None:
    savegames.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, savegames / f"{slot}.save")
    for extension in (".txt", ".tga"):
        companion = source.with_suffix(extension)
        if companion.is_file():
            shutil.copyfile(companion, savegames / f"{slot}{extension}")


def after_load_lines(args: argparse.Namespace) -> list[str]:
    tag = args.tag
    if args.mode == "write":
        lines = ["god", "notarget", f"wait {args.settle_frames}", f"echo {RESAVE_MARKER}", f"saveGame {tag}", "wait 5"]
    else:
        lines = ["wait 3", f"saveGame {tag}_frozen", "wait 5", "set g_stopTime 0", f"wait {args.play_frames}",
                 f"echo {RESAVE_MARKER}", f"saveGame {tag}", "wait 5"]
    return ['set g_autoExecAfterMapLoad ""'] + list(args.after_load) + lines + [f"echo {DONE_MARKER}", "quit", ""]


def launch_command(args: argparse.Namespace, client: Path, home: Path) -> list[str]:
    settings = [
        ("sys_allowMultipleInstances", "1"), ("win_allowMultipleInstances", "1"),
        ("logFile", "2"), ("logFileName", f"logs/{LOG_NAME}"), ("developer", "1"),
        ("fs_basepath", str(args.basepath)), ("fs_homepath", str(home)), ("fs_savepath", str(home)),
        ("fs_game", args.game), ("si_gameType", "singleplayer"), ("r_renderApi", "gl"),
        ("r_fullscreen", "0"), ("r_borderless", "0"), ("r_borderlessDefaultMigrated", "1"),
        ("r_mode", "-1"), ("r_customWidth", "960"), ("r_customHeight", "540"),
        ("r_hiddenWindow", "1"), ("r_swapInterval", "0"), ("com_maxfps", "60"),
        ("s_noSound", "1"), ("in_mouse", "0"), ("in_joystick", "0"), ("in_tty", "0"),
        ("com_skipLogoVideos", "1"), ("com_skipLoadingContinue", "1"),
        ("com_loadingContinueAutoAdvance", "1"), ("g_autoSkipCinematics", "1"),
        ("g_autoExecAfterMapLoad", CFG_REL), ("g_autoExecAfterMapLoadDelayMs", "0"),
        ("com_fixedTic", "1"),
    ]
    if args.mode == "load":
        settings.append(("g_stopTime", "1"))
    settings += [tuple(pair) for pair in args.set]
    command = [str(client)]
    for name, value in settings:
        command += ["+set", name, value]
    if args.mode == "write":
        command += ["+map", args.map]
    else:
        command += ["+loadGame", f"{args.tag}_in"]
    if args.xvfb:
        command = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24"] + command
    return command


def stop_process(process: subprocess.Popen) -> None:
    """Stop the client, with its whole session on POSIX."""
    def signal_all(name: str) -> None:
        if sys.platform == "win32":
            getattr(process, name)()
            return
        try:
            os.killpg(process.pid, signal.SIGTERM if name == "terminate" else signal.SIGKILL)
        except ProcessLookupError:
            pass

    if sys.platform != "win32":
        # The client may have quit while xvfb-run's Xvfb is still in the group.
        signal_all("terminate")
    elif process.poll() is None:
        signal_all("terminate")
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        signal_all("kill")
        process.wait(timeout=15)


def run_leg(args: argparse.Namespace) -> int:
    home = args.home.resolve()
    client = client_path(args.install_root.resolve())
    if not client.is_file():
        raise SystemExit(f"no client at {client}")
    game_dir = home / args.game
    savegames = game_dir / "savegames"
    if args.mode == "load":
        source = args.input.resolve()
        if home == source or home in source.parents:
            raise SystemExit(f"refusing input {source}: it is inside --home")
        stage_input(source, savegames, f"{args.tag}_in")
    for slot in (args.tag, f"{args.tag}_frozen"):
        for extension in (".save", ".txt", ".tga"):
            (savegames / f"{slot}{extension}").unlink(missing_ok=True)
    cfg = game_dir / CFG_REL
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text("\n".join(after_load_lines(args)), encoding="utf-8")
    log = game_dir / "logs" / LOG_NAME
    log.unlink(missing_ok=True)

    environment = dict(os.environ)
    if sys.platform.startswith("linux"):
        # Software GL under Xvfb and no audio device, as the Linux CI smokes run.
        for name, value in (("LIBGL_ALWAYS_SOFTWARE", "1"), ("SDL_VIDEODRIVER", "x11"),
                            ("ALSOFT_DRIVERS", "null"), ("SDL_AUDIODRIVER", "dummy")):
            environment.setdefault(name, value)
    command = launch_command(args, client, home)
    stdout_path = game_dir / "logs" / "savegame-cross-load.stdout.txt"
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    timed_out = False
    startup = None
    if sys.platform == "win32":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        # On POSIX the client gets its own session, so stopping it also stops
        # xvfb-run's Xvfb and every other child.
        process = subprocess.Popen(command, cwd=client.parent, stdin=subprocess.DEVNULL, stdout=stdout,
                                   stderr=subprocess.STDOUT, env=environment, startupinfo=startup,
                                   start_new_session=sys.platform != "win32")
        deadline = started + args.timeout
        while process.poll() is None:
            if time.monotonic() >= deadline:
                timed_out = True
                break
            text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
            if ERROR_PATTERN.search(text) or SIGNAL_PATTERN.search(text) or RESTORE_FAILED.search(text) \
                    or PREFLIGHT_REJECTED.search(text):
                break
            time.sleep(0.5)
        stop_process(process)

    text = "\n".join(path.read_text(encoding="utf-8", errors="replace")
                     for path in (log, stdout_path) if path.is_file())
    errors = ERROR_PATTERN.findall(text) + SIGNAL_PATTERN.findall(text) + RESTORE_FAILED.findall(text)
    rejections = PREFLIGHT_REJECTED.findall(text)
    log_text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    unresolved = len(UNRESOLVED_REFERENCE.findall(log_text))
    started_map = (RESTORE_STARTED if args.mode == "load" else MAP_STARTED).search(text) is not None
    problems = []
    if timed_out:
        problems.append("timed out")
    if not started_map:
        problems.append("restore did not start" if args.mode == "load" else "map did not start")
    if DONE_MARKER not in text:
        problems.append("after-load config did not finish")
    if errors or rejections:
        problems.append("errors or preflight rejections in the log")
    if unresolved:
        problems.append(f"{unresolved} unresolved save references")

    saves: dict[str, Any] = {}
    payloads: dict[str, Payload] = {}
    slots = [args.tag] if args.mode == "write" else [f"{args.tag}_in", f"{args.tag}_frozen", args.tag]
    for slot in slots:
        path = savegames / f"{slot}.save"
        if not path.is_file():
            problems.append(f"missing save {slot}")
            continue
        try:
            payloads[slot] = Payload(path.read_bytes())
        except ValueError as error:
            problems.append(f"{slot}: {error}")
            continue
        saves[slot] = payloads[slot].summary()
        if slot != f"{args.tag}_in" and not portable(payloads[slot]):
            problems.append(f"{slot} is not payload v{PORTABLE_VERSION} {PORTABLE_WIRE_ABI}")
    comparison = None
    if args.mode == "load" and f"{args.tag}_in" in payloads and f"{args.tag}_frozen" in payloads:
        comparison = compare(payloads[f"{args.tag}_in"], payloads[f"{args.tag}_frozen"])
        comparison["createdByRestore"] = restore_created(payloads[f"{args.tag}_in"], payloads[f"{args.tag}_frozen"])
        if comparison["createdByRestore"] is None:
            problems.append("the frozen save does not begin with its input's object table")

    report = {
        "mode": args.mode, "tag": args.tag, "platform": f"{sys.platform}-{platform.machine().lower()}",
        "map": args.map, "game": args.game, "input": str(args.input) if args.input else None,
        "client": str(client), "passed": not problems, "problems": problems,
        "unresolvedReferences": unresolved,
        "seconds": round(time.monotonic() - started, 1), "errors": (errors + rejections)[:5],
        "saves": saves, "inputVsFrozen": comparison,
        "objectTable": payloads[slots[-1]].objects if slots[-1] in payloads else [],
    }
    (game_dir / "logs" / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    brief = {key: report[key] for key in ("mode", "tag", "platform", "passed", "problems", "seconds", "saves")}
    if comparison is not None:
        brief["inputVsFrozen"] = {key: comparison[key] for key in
                                  ("sameObjectTable", "createdByRestore", "objects", "identicalState", "differingBytes")}
    print(json.dumps(brief))
    return 0 if not problems else 1


def run_compare(args: argparse.Namespace) -> int:
    a = Payload(args.saves[0].read_bytes())
    b = Payload(args.saves[1].read_bytes())
    result = compare(a, b)
    result["stamps"] = [a.summary(), b.summary()]
    print(json.dumps(result, indent=2))
    return 0 if result["sameObjectTable"] else 1


def synthetic_save(objects: list[str], state: bytes, build: int = 2023,
                   version: int = PORTABLE_VERSION, abi: str = PORTABLE_WIRE_ABI) -> bytes:
    def string(text: str) -> bytes:
        raw = text.encode("latin-1")
        return struct.pack("<i", len(raw)) + raw

    payload = PAYLOAD_MAGIC + struct.pack("<ii", version, build) + string("ab" * 32) + struct.pack("<i", 437)
    payload += string(abi) + struct.pack("<i", len(objects)) + b"".join(string(name) for name in objects) + state
    header = string("Quake4") + struct.pack("<i", 1834) + string("game/airdefense1")
    data = header + payload
    return data + b"OQ4I" + struct.pack("<II", 1, len(data)) + struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF)


def self_test() -> int:
    base = synthetic_save(["idPlayer", "idLight", "idMover"], b"\x01\x02\x03\x04")
    payload = Payload(base)
    assert portable(payload) and payload.objects == ["idPlayer", "idLight", "idMover"], payload.summary()
    assert payload.state().startswith(struct.pack("<i", 3)) and payload.state().endswith(b"\x01\x02\x03\x04")

    # Another platform's build stamps another build number: the state still matches.
    rebuilt = compare(payload, Payload(synthetic_save(["idPlayer", "idLight", "idMover"], b"\x01\x02\x03\x04", 1999)))
    assert rebuilt["sameObjectTable"] and rebuilt["identicalState"], rebuilt
    changed = compare(payload, Payload(synthetic_save(["idPlayer", "idLight", "idMover"], b"\x01\x02\x07\x04")))
    assert changed["sameObjectTable"] and changed["differingBytes"] == 1, changed
    missing = compare(payload, Payload(synthetic_save(["idPlayer", "idLight"], b"\x01\x02\x03\x04")))
    assert not missing["sameObjectTable"] and missing["firstTableDifference"]["index"] == 2, missing
    shorter = compare(payload, Payload(synthetic_save(["idPlayer", "idLight", "idMover"], b"\x01\x02\x03")))
    assert shorter["differingBytes"] == 1 and shorter["firstDifferingByte"] == shorter["stateBytes"][1], shorter

    assert not portable(Payload(synthetic_save(["idPlayer"], b"", version=4, abi="windows-msvcabi-x64-le-raw1")))

    # A restore may append objects it creates, never lose or reorder saved ones.
    appended = Payload(synthetic_save(["idPlayer", "idLight", "idMover", "rvClientEffect", "rvClientEffect"], b""))
    assert restore_created(payload, appended) == {"rvClientEffect": 2}
    assert restore_created(payload, payload) == {}
    assert restore_created(payload, Payload(synthetic_save(["idPlayer", "idMover", "idLight"], b""))) is None
    assert restore_created(payload, Payload(synthetic_save(["idPlayer", "idLight"], b""))) is None
    for broken in (b"no payload here", base[:base.find(PAYLOAD_MAGIC) + 10]):
        try:
            Payload(broken)
        except ValueError:
            continue
        raise AssertionError("a damaged payload parsed")

    # The load leg saves the frozen restore, resumes time, and marks the resave.
    args = argparse.Namespace(mode="load", tag="leg", play_frames=180, settle_frames=240, after_load=[],
                              basepath=Path("base"), game="baseoq4", set=[], map=None, xvfb=True)
    lines = after_load_lines(args)
    assert lines.index("saveGame leg_frozen") < lines.index("set g_stopTime 0") < lines.index("wait 180") \
        < lines.index(f"echo {RESAVE_MARKER}") < lines.index("saveGame leg") and lines[-2] == "quit", lines
    command = launch_command(args, Path("client"), Path("home"))
    assert command[0] == "xvfb-run" and command[-2:] == ["+loadGame", "leg_in"], command
    assert command[command.index("com_fixedTic") + 1] == "1" and command[command.index("g_stopTime") + 1] == "1"

    # A leg refuses an input inside its own --home.
    scratch = Path(__file__).resolve().parents[2] / ".tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="save-cross-load-", dir=scratch) as temp:
        home = Path(temp) / "home"
        inside = home / "baseoq4" / "savegames" / "x.save"
        inside.parent.mkdir(parents=True)
        inside.write_bytes(base)
        install = Path(temp) / "install"
        client = client_path(install)
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_bytes(b"")
        leg = argparse.Namespace(mode="load", input=inside, home=home, install_root=install, game="baseoq4", tag="x")
        try:
            run_leg(leg)
        except SystemExit as error:
            assert "inside --home" in str(error), error
        else:
            raise AssertionError("an input inside --home was accepted")
    print("savegame_cross_load: self-test ok")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true", help="Check the parser and comparisons only.")
    sub = parser.add_subparsers(dest="command")
    for mode in ("write", "load"):
        leg = sub.add_parser(mode, help=f"Run a {mode} leg on this platform.")
        leg.set_defaults(mode=mode)
        leg.add_argument("--install-root", type=Path, required=True, help="Staged package root.")
        leg.add_argument("--basepath", type=Path, required=True, help="Retail Quake 4 root (q4base PK4s).")
        leg.add_argument("--home", type=Path, required=True, help="Savepath for this platform; reused across legs.")
        leg.add_argument("--game", default="baseoq4", help="fs_game: baseoq4, or q4xbase for The Awakening.")
        leg.add_argument("--tag", required=True, help="Slot name for the saves this leg writes.")
        leg.add_argument("--timeout", type=float, default=900.0)
        leg.add_argument("--set", nargs=2, action="append", default=[], metavar=("CVAR", "VALUE"))
        leg.add_argument("--xvfb", action="store_true", help="Run the client under xvfb-run (Linux).")
        leg.add_argument("--after-load", action="append", default=[], metavar="COMMAND",
                         help="Console command to run first after the load, for diagnosis. Repeatable.")
        if mode == "write":
            leg.add_argument("--map", required=True)
            leg.add_argument("--settle-frames", type=int, default=240, help="Game tics before the save.")
            leg.set_defaults(input=None)
        else:
            leg.add_argument("--input", type=Path, required=True, help="Save to load (read only).")
            leg.add_argument("--play-frames", type=int, default=180, help="Game tics before the resave.")
            leg.set_defaults(map=None)
    pair = sub.add_parser("compare", help="Compare two saves' object tables and state.")
    pair.add_argument("saves", nargs=2, type=Path)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return self_test()
    if args.command in ("write", "load"):
        return run_leg(args)
    if args.command == "compare":
        return run_compare(args)
    raise SystemExit("choose write, load, compare or --self-test")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
