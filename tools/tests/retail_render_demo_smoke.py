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

from mp_round_remote_smoke import verify_runtime_is_current  # noqa: E402




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

    def __init__(self, folder: Path, spin: int = 25):
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


class ChainBroken(RuntimeError):
    """The game opened a step file while the harness replaced it (a sharing
    clash on Windows): the chain of console batches has ended. Nothing about
    the game; the caller starts it again."""


CHAIN_BROKEN = re.compile(r"(?m)^couldn't exec rd_step_\d+\.cfg")


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
        broken_from = len(self.text)
        while True:
            self.poll_log()
            match = expression.search(self.text, max(0, searched - 512))
            if match:
                return match
            if CHAIN_BROKEN.search(self.text, broken_from):
                raise ChainBroken(f"the console chain ended before /{pattern}/")
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


def quit_game(game: Game, chain: Chain, timeout: float = 60) -> int:
    chain.publish(["quit"])
    return game.finish(timeout)


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
    except BaseException:
        game.kill()
        raise
    quit_game(game, chain)
    demos = root / "save/q4base/demos"
    report["files"] = {p.name: {"bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(demos.glob("*.demo"))}
    return report


def reference(args) -> dict:
    """Retail plays the fixture with aviDemo: one 640x480 TGA a demo frame.

    Retail's own reader keeps no bounds on its stream and its playback of this
    fixture stops (it crashed after about 130 frames here); every frame it
    wrote before that is a reference.
    """
    fixture = args.output_dir / "retail-record/save/q4base/demos" / f"{FIXTURE}.demo"
    if not fixture.is_file():
        raise SystemExit("no retail fixture: run the record phase first")
    root = args.output_dir / "retail-reference"
    if root.exists():
        shutil.rmtree(root)
    demos = root / "save/q4base/demos"
    demos.mkdir(parents=True)
    shutil.copy2(fixture, demos / fixture.name)
    extra = ["+set", "com_aviDemoWidth", "640", "+set", "com_aviDemoHeight", "480", "+set", "com_aviDemoSamples", "1"]
    game, chain = retail_game(args.install, args.retail_runtime, root, extra)
    game.start()
    crashed = False
    try:
        game.wait_for(r"execing rd_step_0", 240)
        chain.publish([f"aviDemo {FIXTURE}"])
        try:
            game.wait_for(r"captured \d+ frames for " + re.escape(FIXTURE), 900)
            quit_game(game, chain)
        except RuntimeError:
            crashed = True
    except BaseException:
        game.kill()
        raise
    game.kill()
    frames = sorted((demos / FIXTURE).glob("*.tga"))
    return {"frames": len(frames), "retail_crashed": crashed, "folder": str(demos / FIXTURE)}


def client_executable(runtime: Path) -> Path:
    return runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")


def openq4_game(runtime: Path, root: Path, backend: str, extra: dict | None = None) -> tuple[Game, Chain]:
    game_dir = root / "baseoq4"
    chain = Chain(game_dir)
    settings = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": 1,
        "logFile": 2, "logFileName": "logs/retail_demo.log", "developer": 1,
        "fs_savepath": root, "fs_devpath": root, "fs_game": "baseoq4",
        "com_skipLoadingContinue": 1, "com_maxfps": 0, "r_swapInterval": 0,
        "r_fullscreen": 0, "r_borderless": 0, "r_borderlessDefaultMigrated": 1, "r_fullscreenDesktop": 0,
        "r_windowWidth": 640, "r_windowHeight": 480, "r_mode": -1, "r_customWidth": 640, "r_customHeight": 480,
        "ui_retained": 1, "ui_autoJoin": 0, "in_mouse": 0, "s_volume": 0, "com_showDemo": 1,
        "r_renderApi": "vulkan" if backend == "vk" else "gl",
    }
    settings.update(extra or {})
    command = [str(client_executable(runtime))]
    for name, value in settings.items():
        command += ["+set", name, str(value)]
    command += ["+exec", chain.name(0)]
    game = Game(command, runtime, dict(os.environ), game_dir / "logs/retail_demo.log", root / "console.log")
    return game, chain


FRAME_LINE = re.compile(r"(?m)^frame:(\d+) timeUS:")
# what openQ4 says when it refuses a stream or stops one safely
PROBLEM = re.compile(r"(?m)^.*(?:Malformed retail render demo|Malformed render demo|playback stopped safely|"
                     r"Render demo '[^']*' (?:contains|has|ended|did not|uses)|is not a render demo|FATAL|Sys_Error).*$")


def read_tga(path: Path):
    import numpy as np
    data = path.read_bytes()
    id_length, image_type = data[0], data[2]
    width = int.from_bytes(data[12:14], "little")
    height = int.from_bytes(data[14:16], "little")
    depth, descriptor = data[16], data[17]
    if image_type != 2 or depth not in (24, 32):
        raise ValueError(f"{path}: unsupported TGA type {image_type}/{depth}")
    channels = depth // 8
    pixels = np.frombuffer(data, dtype=np.uint8, count=width * height * channels, offset=18 + id_length)
    image = pixels.reshape(height, width, channels)[:, :, :3][:, :, ::-1]
    if not descriptor & 0x20:
        image = image[::-1]
    return image.astype(np.float32)


def thumbnail(path: Path):
    """160x120 luma: coarse enough that filtering and a pixel of offset do not count."""
    image = read_tga(path)
    luma = image @ [0.25, 0.5, 0.25]
    height, width = luma.shape
    luma = luma[: height - height % 120, : width - width % 160]
    return luma.reshape(120, luma.shape[0] // 120, 160, luma.shape[1] // 160).mean(axis=(1, 3))


def view_changes(fixture_none: Path) -> tuple[list[int], int]:
    """The frames (1-based) whose recorded camera differs from the frame before,
    and the frame count, read by the independent decoder."""
    import retail_render_demo as decoder
    views, current = [], {}
    read_view, read_render = decoder.read_render_view, decoder.read_render

    def on_view(r):
        current["view"] = read_view(r)
        return current["view"]

    def on_render(r, stats, **kw):
        ended = read_render(r, stats, **kw)
        if ended and kw.get("probe_effects", True):
            v = current["view"]
            views.append(tuple(round(x, 2) for x in (*v["origin"], *v["axis"])))
        return ended

    decoder.read_render_view, decoder.read_render = on_view, on_render
    try:
        decoder.decode(fixture_none.read_bytes())
    finally:
        decoder.read_render_view, decoder.read_render = read_view, read_render
    return [i + 1 for i in range(1, len(views)) if views[i] != views[i - 1]], len(views)


def play(args, frames: int) -> dict:
    """openQ4 plays each fixture (every compression scheme) to its end."""
    fixtures = sorted((args.output_dir / "retail-record/save/q4base/demos").glob(f"{FIXTURE}*.demo"))
    if not fixtures:
        raise SystemExit("no retail fixture: run the record phase first")
    report = {"backend": args.backend, "fixtures": {}}
    for fixture in fixtures:
        root = args.output_dir / f"play-{args.backend}-{fixture.stem}"
        if root.exists():
            shutil.rmtree(root)
        demos = root / "baseoq4/demos"
        demos.mkdir(parents=True)
        shutil.copy2(fixture, demos / fixture.name)
        game, chain = openq4_game(args.runtime_dir, root, args.backend)
        game.start()
        try:
            game.wait_for(r"execing rd_step_0", 240)
            run_batch(game, chain, [f"playDemo {fixture.stem}"], 300)
            # the stream ends at its last frame and playback returns to the menu
            game.wait_for(rf"(?m)^frame:{frames} timeUS|playback stopped safely|Malformed", 600)
            time.sleep(1.0)
        except BaseException:
            game.kill()
            raise
        code = quit_game(game, chain)
        read = [int(n) for n in FRAME_LINE.findall(game.text)]
        report["fixtures"][fixture.name] = {
            "exit": code, "frames": max(read) if read else 0,
            "problems": sorted(set(m.group(0).strip() for m in PROBLEM.finditer(game.text)))[:20],
            "retail_stream": "reading a retail Quake 4 render demo" in game.text}
    return report


def capture_openq4(args, fixture: Path, frames) -> dict:
    """Screenshots of chosen frames: playback paused, then stepped frame by frame."""
    root = args.output_dir / f"capture-{args.backend}"
    if root.exists():
        shutil.rmtree(root)
    demos = root / "baseoq4/demos"
    demos.mkdir(parents=True)
    shutil.copy2(fixture, demos / f"{FIXTURE}.demo")
    game, chain = openq4_game(args.runtime_dir, root, args.backend)
    shots = {}
    game.start()
    try:
        game.wait_for(r"execing rd_step_0", 240)
        run_batch(game, chain, [f"playDemo {FIXTURE}", "demoPause"], 300)
        run_batch(game, chain, ["wait 30"], 120)
        current = max(int(n) for n in FRAME_LINE.findall(game.text))
        for frame in frames:
            if frame < current:
                continue
            if frame > current:
                # a render demo steps one frame a command, and a frame a step
                steps = []
                for _ in range(frame - current):
                    steps += ["demoStep", "wait 2"]
                run_batch(game, chain, [*steps, "wait 20"], 600)
            current = max(int(n) for n in FRAME_LINE.findall(game.text))
            name = f"screenshots/openq4_{current:05d}.tga"
            run_batch(game, chain, [f"screenshot {name}", "wait 5"], 120)
            shots[current] = str(root / "baseoq4" / name)
        # seeking replays from the start to the time asked; a step back is a seek
        controls = {}
        mark = len(game.text)
        run_batch(game, chain, ["demoSeek 1", "wait 90"], 300)
        controls["seek_1s"] = last_frame(game.text[mark:])
        mark = len(game.text)
        run_batch(game, chain, ["demoStep -1", "wait 90"], 300)
        controls["step_back"] = last_frame(game.text[mark:])
        # timeDemo reads every frame as fast as it can draw them
        mark = len(game.text)
        run_batch(game, chain, ["demoStop", "wait 10", f"timeDemo {FIXTURE}"], 120)
        report = game.wait_for(r"(\d+) frames rendered in ([\d.]+) seconds", 600)
        controls["timedemo_frames"] = int(report.group(1))
        # the library entry for the fixture: its map, read from the stream
        run_batch(game, chain, ["wait 30", f"demoFind {FIXTURE}"], 120)
    except BaseException:
        game.kill()
        raise
    library = re.findall(r"(?m)^DEMO_FIND (\S+)  (.*?)  ", game.text)
    quit_game(game, chain)
    return {"shots": shots, "library": library, "controls": controls}


def last_frame(text: str) -> dict | None:
    lines = re.findall(r"(?m)^frame:(\d+) timeUS:(\d+)", text)
    return {"frame": int(lines[-1][0]), "timeUS": int(lines[-1][1])} if lines else None


def image_jumps(folder: Path, count: int) -> list[int]:
    jumps, previous = [], None
    for k in range(count):
        current = thumbnail(folder / f"{FIXTURE}_{k:05d}.tga")
        if previous is not None and abs(current - previous).mean() > 10.0:
            jumps.append(k)
        previous = current
    return jumps


def compare(args, shots: dict, changes: list[int]) -> dict:
    """Each openQ4 frame against retail's capture of the same camera.

    Retail's aviDemo drifts by a few frames over a recording, so its frames are
    aligned on the camera's moves: the recorded view changes, read from the
    stream, against the jumps in retail's images. A frame passes when it is
    close to retail's frame of its own camera and far from every other camera's.
    """
    folder = args.output_dir / "retail-reference/save/q4base/demos" / FIXTURE
    retail = sorted(folder.glob(f"{FIXTURE}_*.tga"))
    jumps = image_jumps(folder, len(retail))
    landmarks, drift = [], 0
    for change in changes:
        near = [j for j in jumps if 0 <= change - j - drift <= 3]
        if near:
            drift = change - near[0]
            landmarks.append((change, near[0]))
    if not landmarks:
        return {"landmarks": [], "frames": {}, "failures": ["no camera move found in retail's frames"]}
    segments = [1, *changes]

    def offset_for(frame: int) -> int:
        later = [c - j for c, j in landmarks if c > frame]
        return later[0] if later else landmarks[-1][0] - landmarks[-1][1]

    results, failures = {}, []
    for frame, shot in sorted(shots.items(), key=lambda item: int(item[0])):
        frame = int(frame)
        segment = max(i for i, start in enumerate(segments) if start <= frame)
        index = frame - offset_for(frame)
        if not 0 <= index < len(retail):
            continue
        ours = thumbnail(Path(shot))
        own = float(abs(ours - thumbnail(retail[index])).mean())
        others = []
        for other, start in enumerate(segments):
            end = segments[other + 1] if other + 1 < len(segments) else start + 12
            middle = (start + end) // 2
            pick = middle - offset_for(middle)
            if other != segment and 0 <= pick < len(retail):
                others.append(float(abs(ours - thumbnail(retail[pick])).mean()))
        results[frame] = {"retail_frame": index, "difference": round(own, 2),
                          "nearest_other_camera": round(min(others), 2) if others else None}
        if own > args.max_difference or (others and own * 2.0 > min(others)):
            failures.append(frame)
    return {"landmarks": landmarks, "frames": results, "failures": failures}


def damaged_copies(source: Path, folder: Path) -> dict:
    """Cut and corrupted copies of the uncompressed fixture."""
    import retail_render_demo as decoder
    data = source.read_bytes()
    marks = decoder.command_offsets(data)
    folder.mkdir(parents=True, exist_ok=True)
    copies = {"cut_header": data[:20], "cut_mid": data[: len(data) // 2], "cut_last_byte": data[:-1]}
    for kind in ("SCMD_STATE", "LOADMAP", "ENTITYDEF_GUI", "EFFECTDEF", "GUI_MODEL"):
        if kind in marks:
            copies[f"cut_{kind.lower()}"] = data[: marks[kind] + 9]

    def corrupt(offset: int, value: bytes) -> bytes:
        bad = bytearray(data)
        bad[offset:offset + len(value)] = value
        return bytes(bad)

    huge = b"\xff\xff\xff\x7f"
    if "SCMD_STATE" in marks:
        copies["bad_emitter_count"] = corrupt(marks["SCMD_STATE"] + 2 + 12 + 36 + 16, huge)
    if "LOADMAP" in marks:
        copies["bad_map_header"] = corrupt(marks["LOADMAP"] + 2 + 4, huge)
        copies["bad_command"] = corrupt(marks["LOADMAP"] + 1, b"\xee")
    if "ENTITYDEF_GUI" in marks:
        copies["bad_gui_state"] = corrupt(marks["ENTITYDEF_GUI_STATE"], huge)
    if "EFFECTDEF" in marks:
        copies["bad_effect_handle"] = corrupt(marks["EFFECTDEF"] + 2, huge)
    if "GUI_MODEL" in marks:
        copies["bad_gui_vertices"] = corrupt(marks["GUI_MODEL"] + 2, huge)
    if "JOINTS" in marks:
        copies["bad_joint_count"] = corrupt(marks["JOINTS"], huge)
    if "HASH" in marks:
        copies["bad_hash_index"] = corrupt(marks["HASH"], b"\x00\x00\x00\x40")
    for name, payload in copies.items():
        (folder / f"damaged_{name}.demo").write_bytes(payload)
    return {name: len(payload) for name, payload in copies.items()}


def damage(args) -> dict:
    """openQ4 plays every damaged copy in one session: each stops with a warning
    or plays what it can; none crashes or ends in a fatal error."""
    source = args.output_dir / "retail-record/save/q4base/demos" / f"{FIXTURE}_none.demo"
    root = args.output_dir / f"damage-{args.backend}"
    if root.exists():
        shutil.rmtree(root)
    copies = damaged_copies(source, root / "baseoq4/demos")
    outcomes, fatal, restarts = {}, [], 0
    pending = list(copies)

    def start_game():
        log = root / "baseoq4/logs/retail_demo.log"
        if log.exists():
            log.replace(log.with_name(f"retail_demo.{restarts}.log"))
        game, chain = openq4_game(args.runtime_dir, root, args.backend)
        game.start()
        game.wait_for(r"execing rd_step_0", 240)
        return game, chain

    game, chain = start_game()
    try:
        while pending:
            name = pending[0]
            start = len(game.text)
            try:
                run_batch(game, chain, [f"playDemo damaged_{name}"], 300)
                quiet_since, seen = time.monotonic(), -1
                while time.monotonic() - quiet_since < 6.0:
                    game.poll_log()
                    count = len(FRAME_LINE.findall(game.text, start))
                    if count != seen:
                        seen, quiet_since = count, time.monotonic()
                    if game.process.poll() is not None:
                        raise RuntimeError(f"openQ4 exited playing damaged_{name}")
                    time.sleep(.1)
                run_batch(game, chain, ["demoStop", "wait 5"], 120)
            except ChainBroken:
                # the harness's clash, not the game's: start again and redo this copy
                restarts += 1
                if restarts > 3:
                    raise
                fatal += [line for line in game.text.splitlines() if "FATAL" in line or "Sys_Error" in line]
                game.kill()
                game, chain = start_game()
                continue
            text = game.text[start:]
            outcomes[name] = {"frames": max([int(n) for n in FRAME_LINE.findall(text)] or [0]),
                              "messages": sorted(set(m.group(0).strip() for m in PROBLEM.finditer(text)))[:4]}
            pending.pop(0)
    except BaseException:
        game.kill()
        raise
    code = quit_game(game, chain)
    fatal += [line for line in game.text.splitlines() if "FATAL" in line or "Sys_Error" in line]
    return {"exit": code, "fatal": fatal, "copies": outcomes, "harness_restarts": restarts}


def retrying(phase, attempts: int = 3):
    """Runs a phase again when the harness's own step-file clash ended its chain."""
    for attempt in range(attempts):
        try:
            return phase()
        except ChainBroken:
            if attempt == attempts - 1:
                raise


def check(report: dict, frames: int) -> list[str]:
    """What the report must show for the smoke to pass."""
    problems = []
    for name, result in report.get("play", {}).get("fixtures", {}).items():
        if result["frames"] != frames or result["problems"] or not result["retail_stream"] or result["exit"] != 0:
            problems.append(f"play {name}: {result}")
    compared = report.get("compare")
    if compared is not None:
        if compared["failures"] or len(compared["frames"]) < 4:
            problems.append(f"compare: {compared['failures']} of {len(compared['frames'])} frames")
    capture = report.get("capture")
    if capture is not None:
        controls = capture.get("controls", {})
        seek, back = controls.get("seek_1s"), controls.get("step_back")
        if not seek or abs(seek["timeUS"] - 1000000) > 40000:
            problems.append(f"seek to 1 s: {seek}")
        if not seek or not back or back["frame"] != seek["frame"] - 1:
            problems.append(f"step back: {back} after {seek}")
        if abs(controls.get("timedemo_frames", 0) - frames) > 1:
            problems.append(f"timeDemo: {controls.get('timedemo_frames')} of {frames} frames")
    if capture is not None and not any(path.endswith(f"{FIXTURE}.demo") and "hangar" in level.lower()
                                       for path, level in capture.get("library", [])):
        problems.append(f"library entry: {capture.get('library')}")
    damaged = report.get("damage")
    if damaged is not None:
        if damaged["fatal"] or damaged["exit"] != 0:
            problems.append(f"damage: exit {damaged['exit']} {damaged['fatal']}")
        for name, result in damaged["copies"].items():
            if name.startswith("cut_last") or name.startswith("cut_mid"):
                continue
            if not result["messages"]:
                problems.append(f"damage {name}: no warning")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("phase", choices=("record", "reference", "play", "capture", "damage", "verify", "all"))
    parser.add_argument("--install", type=Path, default=DEFAULT_INSTALL)
    parser.add_argument("--retail-runtime", type=Path, default=RETAIL_RUNTIME)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--backend", choices=("gl", "vk"), default="gl")
    parser.add_argument("--max-difference", type=float, default=8.0,
                        help="largest mean luma difference (0-255, at 160x120) from retail's frame")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/retail-demo/smoke")
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {}
    if args.phase in ("record", "all"):
        report["record"] = record(args)
    if args.phase in ("reference", "all"):
        report["reference"] = reference(args)
    demos = args.output_dir / "retail-record/save/q4base/demos"
    frames = 0
    if args.phase in ("play", "capture", "verify", "all"):
        verify_runtime_is_current(args.runtime_dir)
        changes, frames = view_changes(demos / f"{FIXTURE}_none.demo")
    if args.phase in ("play", "verify", "all"):
        report["play"] = retrying(lambda: play(args, frames))
    if args.phase in ("capture", "verify", "all"):
        # the middle of each camera's stretch, away from its moves
        starts = [1, *changes, frames + 1]
        picks = tuple(sorted({(a + b) // 2 for a, b in zip(starts, starts[1:])}))
        captured = retrying(lambda: capture_openq4(args, demos / f"{FIXTURE}.demo", picks))
        report["capture"] = {"view_changes": changes, "frames": frames, **captured}
        report["compare"] = compare(args, captured["shots"], changes)
    if args.phase in ("damage", "verify", "all"):
        report["damage"] = damage(args)
    problems = check(report, frames)
    report["problems"] = problems
    (args.output_dir / f"report-{args.phase}-{args.backend}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print("RETAIL_RENDER_DEMO_SMOKE " + ("FAILED" if problems else "PASSED"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
