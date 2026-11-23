#!/usr/bin/env python3
"""Record a render demo with retail Quake 4 and play it in openQ4 (MP-22).

The retail half runs a fixed copy of the installed `Quake4.exe` from
`.tmp/runtime/retail-audio` (with its OpenAL Soft wave writer, so nothing
plays aloud), loads a single-player level and records a render demo by console
scripts only: `+set`/`+exec` on the command line, then a chain of config files
the harness publishes one at a time while the game spins on them. No keyboard
or mouse events are ever sent. Every write goes to the output directory; the
installation is read-only.

Phases:

* `record`: retail records `retail_fixture.demo` (LZW, retail's default) and,
  through retail's own `compressDemo`, the same stream uncompressed, LZSS and
  Huffman, then plays the fixture with `aviDemo`, which writes one TGA a demo
  frame. Those captures are the reference.
* `play`: openQ4 plays each fixture with `playDemo` (the frames must all be
  read, no warning about the stream may appear) and with `aviDemo` to write its
  own TGA a frame; the harness compares them with retail's frame by frame.
* `damage`: openQ4 plays truncated and corrupted copies; each must stop with a
  warning, never a fatal error or a crash.

`retail_render_demo.py` decodes the uncompressed stream on its own, as an
independent check of the layout both readers assume.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))




def main_checkout() -> Path:
    """The main checkout: fixed runtimes are shared by every worktree."""
    try:
        common = subprocess.check_output(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                         cwd=ROOT, text=True).strip()
        return Path(common).parent
    except (OSError, subprocess.CalledProcessError):
        return ROOT


RETAIL_RUNTIME = main_checkout() / ".tmp/runtime/retail-audio"
RETAIL_FILES = ("Quake4.exe", "Mss32.dll", "dbghelp.dll", "libcurl.dll", "OpenAL32.dll")
DEFAULT_INSTALL = Path(r"E:\SteamLibrary\steamapps\common\Quake 4")
MAP = "game/hangar1"
FIXTURE = "retail_fixture"
SCHEMES = {"none": 0, "lzss": 2, "huffman": 3}
ALSOFT = ("[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\nsample-type=float32\n"
          "frequency=48000\nperiod_size=512\n[wave]\nfile={file}\n")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_atomic(path: Path, lines: list[str]) -> None:
    temporary = path.with_suffix(".part")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    deadline = time.monotonic() + 5
    while True:
        try:
            temporary.replace(path)
            return
        except PermissionError:
            # the engine can hold the old file for a moment while it reads it
            if time.monotonic() > deadline:
                raise
            time.sleep(.05)


class Chain:
    """Console batches the game runs once each, in order.

    The game spins on `step_<n>.cfg`: a file that waits and runs itself again.
    Publishing a batch first writes the next spinning file, then replaces the
    current one with the batch, which ends by moving the game on to the next.
    exec reads a whole file at once, so each batch runs exactly once.
    """

    def __init__(self, folder: Path, spin: int = 10):
        self.folder, self.spin, self.step = folder, spin, 0
        self.folder.mkdir(parents=True, exist_ok=True)
        self._spin_file(0)

    def name(self, step: int) -> str:
        return f"rd_step_{step}.cfg"

    def _spin_file(self, step: int) -> None:
        write_atomic(self.folder / self.name(step), [f"wait {self.spin}", f"exec {self.name(step)}"])

    def publish(self, commands: list[str]) -> str:
        marker = f"RD_STEP_{self.step}_DONE"
        self._spin_file(self.step + 1)
        write_atomic(self.folder / self.name(self.step),
                     [*commands, f"echo {marker}", f"wait {self.spin}", f"exec {self.name(self.step + 1)}"])
        self.step += 1
        return marker


class Game:
    """One game process, its console log followed as it is written."""

    def __init__(self, command: list[str], cwd: Path, env: dict, log: Path, console: Path, show: int = 4):
        self.command, self.cwd, self.env, self.log, self.console, self.show = command, cwd, env, log, console, show
        self.text, self.offset, self.process, self.console_handle = "", 0, None, None

    def start(self) -> None:
        options = {}
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            # 4 = SW_SHOWNOACTIVATE: the window never takes the focus.
            startup.wShowWindow = self.show
            options["startupinfo"] = startup
        self.console_handle = self.console.open("w", encoding="utf-8")
        self.process = subprocess.Popen(self.command, cwd=self.cwd, env=self.env, stdin=subprocess.DEVNULL,
                                        stdout=self.console_handle, stderr=subprocess.STDOUT, **options)

    def poll_log(self) -> str:
        if not self.log.is_file():
            return ""
        with self.log.open("rb") as stream:
            stream.seek(self.offset)
            chunk = stream.read()
        cut = chunk.rfind(b"\n") + 1
        if not cut:
            return ""
        self.offset += cut
        text = chunk[:cut].decode("utf-8", "replace")
        self.text += text
        return text

    def wait_for(self, pattern: str, timeout: float) -> re.Match:
        deadline = time.monotonic() + timeout
        expression = re.compile(pattern, re.M)
        searched = 0
        while True:
            self.poll_log()
            match = expression.search(self.text, max(0, searched - 512))
            if match:
                return match
            searched = len(self.text)
            if self.process.poll() is not None:
                self.poll_log()
                match = expression.search(self.text)
                if match:
                    return match
                raise RuntimeError(f"{Path(self.command[0]).name} exited ({self.process.returncode}) "
                                   f"before /{pattern}/")
            if time.monotonic() > deadline:
                raise TimeoutError(f"timed out waiting for /{pattern}/")
            time.sleep(.02)

    def finish(self, timeout: float) -> int:
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=20)
            raise
        finally:
            self.poll_log()
            if self.console_handle:
                self.console_handle.close()

    def kill(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=20)
        if self.console_handle:
            self.console_handle.close()


def prepare_retail_runtime(install: Path, runtime: Path) -> dict:
    """The fixed retail copy the audio comparisons use, verified, never rebuilt."""
    identity = {}
    for name in RETAIL_FILES:
        target = runtime / name
        if not target.is_file():
            raise SystemExit(f"Missing {target}: prepare the retail runtime with "
                             "sound_distance_retail_compare.py --retail-openal <32-bit OpenAL Soft>")
        identity[name] = sha(target)
    if identity["Quake4.exe"] != sha(install / "Quake4.exe"):
        raise SystemExit(f"{runtime / 'Quake4.exe'} differs from the installed Quake4.exe")
    return identity


def retail_game(install: Path, runtime: Path, root: Path, extra: list[str]) -> tuple[Game, Chain]:
    save = root / "save"
    game_dir = save / "q4base"
    chain = Chain(game_dir)
    conf = root / "alsoft.ini"
    conf.write_text(ALSOFT.format(file=root / "mix.wav"), encoding="utf-8")
    settings = {
        # lower-case search paths: retail warns about each path otherwise
        "fs_basepath": str(install).lower(), "fs_savepath": str(save).lower(), "fs_cdpath": "",
        "fs_game": "", "s_useOpenAL": "1", "s_useEAXReverb": "0", "s_volume": "0",
        "r_fullscreen": "0", "r_mode": "3", "in_mouse": "0", "net_ip": "127.0.0.1", "logFile": "2",
        "developer": "0", "com_allowConsole": "1", "com_skipLevelLoadPause": "1",
        "com_showFPS": "0", "r_swapInterval": "0",
    }
    command = [str(runtime / "Quake4.exe"), *(s for k, v in settings.items() for s in ("+set", k, v)),
               *extra, "+exec", chain.name(0)]
    env = {**os.environ, "ALSOFT_CONF": str(conf), "ALSOFT_DRIVERS": "wave",
           # the unmanifested 32-bit exe trips installer detection
           "__COMPAT_LAYER": "RunAsInvoker"}
    game = Game(command, runtime, env, game_dir / "qconsole.log", root / "console.log")
    return game, chain


def run_batch(game: Game, chain: Chain, commands: list[str], timeout: float = 120) -> None:
    game.wait_for(re.escape(chain.publish(commands)), timeout)


def record(args) -> dict:
    output = args.output_dir
    identity = prepare_retail_runtime(args.install, args.retail_runtime)
    root = output / "retail-record"
    if root.exists():
        shutil.rmtree(root)
    game, chain = retail_game(args.install, args.retail_runtime, root, ["+devmap", MAP])
    report = {"retail_identity": identity, "map": MAP}
    game.start()
    try:
        game.wait_for(r"msec to load " + re.escape(MAP), 300)
        run_batch(game, chain, ["wait 60", "getviewpos"])
        view = re.findall(r"(?m)^\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) ([-\d.]+)\s*$", game.text)
        if not view:
            raise RuntimeError("retail printed no view position")
        x, y, z, yaw = map(float, view[-1])
        report["start_view"] = [x, y, z, yaw]
        # the recorded view turns and moves, so frames differ from each other
        moves = []
        for step in range(8):
            moves += [f"setviewpos {x + 16 * step:g} {y:g} {z - 40:g} {(yaw + 45 * step) % 360:g}", "wait 12"]
        run_batch(game, chain, [f"recordDemo {FIXTURE}", "wait 20", *moves, "wait 20", "stopRecording"], 300)
        for name, scheme in SCHEMES.items():
            run_batch(game, chain, [f"compressDemo {FIXTURE} {scheme}"], 300)
            source = root / "save/q4base/demos" / f"{FIXTURE}_compressed.demo"
            if not source.is_file():
                raise RuntimeError(f"compressDemo {scheme} wrote nothing")
            source.replace(source.with_name(f"{FIXTURE}_{name}.demo"))
        run_batch(game, chain, ["disconnect", "wait 30", f"aviDemo {FIXTURE}"], 300)
        game.wait_for(r"captured \d+ frames for " + re.escape(FIXTURE), 900)
        run_batch(game, chain, ["quit"], 60)
    except BaseException:
        game.kill()
        raise
    game.finish(60)
    demos = root / "save/q4base/demos"
    report["files"] = {p.name: {"bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(demos.glob("*.demo"))}
    report["retail_frames"] = len(list((demos / FIXTURE).glob("*.tga")))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("phase", choices=("record", "play", "damage", "all"))
    parser.add_argument("--install", type=Path, default=DEFAULT_INSTALL)
    parser.add_argument("--retail-runtime", type=Path, default=RETAIL_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/retail-demo/smoke")
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {}
    if args.phase in ("record", "all"):
        report["record"] = record(args)
    (args.output_dir / f"report-{args.phase}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
