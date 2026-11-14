#!/usr/bin/env python3
"""Compare rendered distance attenuation between retail Quake 4 and openQ4.

AUD-01's audible retail comparison. Both games play the same three original
looping shaders at the same nine distances in a real Hangar 1 session, and each
renders its mix through OpenAL Soft's wave writer, so nothing plays aloud. The
harness measures the power of every placement in the written audio and checks
that openQ4's level at each distance follows retail's, and that both follow the
authored linear curve.

Retail runs from a copy of the installed `Quake4.exe` in a fixed runtime folder
with a 32-bit OpenAL Soft library supplied by the caller (`--retail-openal`).
Retail 1.4.2 sets `AL_ROLLOFF_FACTOR` to 0 and passes the engine's channel
volume as `AL_GAIN` (`0x1016e6a9`), so its OpenAL output carries the same
distance curve as its default software mixer, which multiplies that volume
into its speaker gains. Installed archives are read-only; every write goes to
the output directory. No mouse or keyboard events are sent.

The listener sits in the map's portal-sky room with the four map sounds that
reach it removed, so each placement plays over digital silence. Retail mutes
itself and sleeps while it is not the active application, and faking focus
would be synthetic input, so the retail half waits until a person focuses its
window and refuses the run if focus is lost. openQ4 runs first, unattended;
`--skip-retail` runs only that half, as a partial check against the authored
curve. Peers restaging `.install` close its clients: pass `--runtime-dir` to a
fixed copy such as `.tmp/runtime/audio` on a shared machine.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile

from mp_round_remote_smoke import verify_runtime_is_current, write_script
from sound_distance_runtime import SHADERS, retail_shaders

ROOT = Path(__file__).resolve().parents[2]
RETAIL_RUNTIME = ROOT / ".tmp/runtime/retail-audio"
RETAIL_FILES = ("Quake4.exe", "Mss32.dll", "dbghelp.dll", "libcurl.dll")
MAP = "game/hangar1"
FIXTURE = "cmp_fixture"
# Hangar 1's portal-sky room: a real map area, thousands of units beyond every
# positional speaker. Retail mixes nothing while the listener is outside the
# map, so the comparison cannot use the void.
LISTENER = (-1344.0, -8336.0, 374.0)
# The only map sounds that reach that room: a speaker authored with a
# 30984-unit range and the script-triggered global cannon. Removing them in
# both games leaves the rendered floor far under the quietest fixture. The map
# script ends its cannon loop when they are gone.
QUIET = ("Blinky_Lights001", "ad_biggun_fire_close", "ad_biggun_fire_mid", "ad_biggun_fire_far")
# Seconds: settle after spawning a fixture; after stopping it, the pause before
# the gap that records the floor it played over; and that gap.
SETTLE, CLEAR, GAP = .6, .6, 1.0
QUIET_SETTLE = 6.0        # let removed and out-of-range voices fade out
TRIM = .15                # ignore this much at each end of every window
BLOCK = .05               # levels are the median of 50 ms block powers
PASSES = 2                # every placement is measured this many times
# Each shader's single looping sample and its length in seconds. A measured
# window spans one whole loop, so where in the loop it starts cannot move the
# level; ambient_light_hum01's envelope swings by several dB within its loop.
LOOPS = {
    "weapon_lightninggun_fireloop": ("sound/weapons/lightninggun/loop", 2.752),
    "weapon_lightninggun_hum": ("sound/weapons/lightninggun/hum", 2.239),
    "ambient_light_hum01": ("sound/ambience/lights/lights_hum01", 4.089),
}
RELATIVE_TOLERANCE_DB = 1.0
# Inside the authored minimum distance retail's OpenAL path renders a point
# source, while openQ4 (AL_SOURCE_RADIUS) and retail's default software mixer
# spread the sound around the listener; in stereo that moves its power.
INNER_TOLERANCE_DB = 2.0
ABSOLUTE_TOLERANCE_DB = 1.5
AUTHORED_TOLERANCE_DB = 1.0
MIN_SNR_DB = 10.0         # fixture power above the background to compare a level
SILENT_RATIO_DB = .5      # a silent placement may not raise the floor by more
SILENT_REFERENCE_DB = -60.0  # ... unless it stays this far under the shader's own level
FOCUS_TIMEOUT = 600.0     # seconds to wait for someone to focus the retail window
# Leave OpenAL Soft's EAX extensions exposed: retail only takes its OpenAL path
# when EAX is present and otherwise opens DirectSound on the real device.
ALSOFT = ("[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\nsample-type=float32\n"
          "frequency=48000\nstereo-mode=speakers\nperiod_size=512\n[wave]\nfile={file}\n")


def window_for(shader: str) -> float:
    """Seconds between a placement's on and off markers: one loop plus trims."""
    return LOOPS[shader][1] + 2 * TRIM + .1


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def distances(minimum: float, maximum: float) -> tuple[float, ...]:
    return (0.0, minimum * .25, minimum * .5, minimum,
            minimum + (maximum - minimum) * .25, (minimum + maximum) * .5,
            minimum + (maximum - minimum) * .75, maximum, maximum + 20)


def build_cases() -> list[dict]:
    cases = []
    for index, (name, (_, minimum, maximum, db)) in enumerate(SHADERS.items()):
        for point, distance in enumerate(distances(minimum, maximum)):
            frac = 1.0 if distance <= minimum else max(0.0, (maximum - distance) / (maximum - minimum))
            gain = 2 ** (db / 6) * .65 * frac
            cases.append({"tag": f"{index}_{point}", "shader": name, "distance": distance,
                          "origin": [LISTENER[0] + distance, LISTENER[1], LISTENER[2]],
                          "falloff": frac, "authored_db": db, "expected_gain": gain})
    return cases


def probe_commands(cases: list[dict], wait, passes: int = PASSES) -> list[str]:
    """One command list for both games; `wait(seconds)` is each game's wait.

    The placements run `passes` times. A map sound that happens to play during
    one window can only add power, so the quieter pass is the clean one.
    """
    x, y, z = LISTENER
    # setviewpos and Teleport each add 0.25 units to the standing eye height.
    commands = ["god", "notarget", "noclip", *(f"remove {name}" for name in QUIET),
                f"setviewpos {x:g} {y:g} {z - .5:g} 0", wait(QUIET_SETTLE), "getviewpos",
                "echo CMP_READY", wait(.3), "echo CMP_BG_BEGIN", wait(GAP * 2), "echo CMP_BG_END"]
    for number in range(1, passes + 1):
        for case in cases:
            ox, oy, oz = case["origin"]
            tag = f"{case['tag']}_P{number}"
            commands += [f'spawn speaker name {FIXTURE} s_shader {case["shader"]} origin "{ox:g} {oy:g} {oz:g}"',
                         wait(SETTLE), f"echo CMP_ON_{tag}", wait(window_for(case["shader"])),
                         f"echo CMP_OFF_{tag}",
                         # A speaker's trigger toggles it off at once; a removed
                         # emitter would play out its fade first.
                         f"trigger {FIXTURE}", f"remove {FIXTURE}", wait(CLEAR),
                         f"echo CMP_GAP_{tag}", wait(GAP), f"echo CMP_GAPEND_{tag}"]
    return commands + ["echo CMP_COMPLETE", wait(.3), "quit"]


def check_loops(install: Path) -> dict:
    """Confirm each fixture's installed loop length, which sizes its window."""
    found = {}
    wanted = {sample.lower(): shader for shader, (sample, _) in LOOPS.items()}
    for package in sorted((install / "q4base").glob("*.pk4"), key=lambda p: p.name.lower()):
        with zipfile.ZipFile(package) as archive:
            for name in archive.namelist():
                stem = name.lower().rsplit(".", 1)[0]
                if stem in wanted and name.lower().endswith(".ogg"):
                    found[wanted[stem]] = (package, name, archive.read(name))
    result = {}
    for shader, (sample, seconds) in LOOPS.items():
        if shader not in found:
            raise ValueError(f"Missing retail sample {sample}")
        package, name, data = found[shader]
        header = data.find(b"\x01vorbis")
        rate = struct.unpack_from("<I", data, header + 12)[0]
        last = data.rfind(b"OggS")
        length = struct.unpack_from("<q", data, last + 6)[0] / rate
        if header < 0 or abs(length - seconds) > .01:
            raise ValueError(f"{sample} loops in {length:.3f} s, not {seconds} s")
        result[shader] = {"package": str(package), "entry": name, "seconds": length,
                          "sha256": hashlib.sha256(data).hexdigest()}
    return result


def read_wav(path: Path) -> dict:
    """Read OpenAL Soft's float32 stereo wave output, finalised or not."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"Not a wave file: {path}")
    offset, fmt, start = 12, None, None
    while offset + 8 <= len(data):
        chunk, size = data[offset:offset + 4], struct.unpack_from("<I", data, offset + 4)[0]
        if chunk == b"fmt ":
            tag, channels, rate = struct.unpack_from("<HHI", data, offset + 8)
            bits = struct.unpack_from("<H", data, offset + 22)[0]
            if tag == 0xFFFE:
                tag = struct.unpack_from("<H", data, offset + 32)[0]
            fmt = (tag, channels, rate, bits)
        elif chunk == b"data":
            start = offset + 8
            break
        offset += 8 + size + (size & 1)
    if fmt is None or start is None:
        raise ValueError(f"Missing wave format or data: {path}")
    tag, channels, rate, bits = fmt
    if tag != 3 or bits != 32 or channels != 2:
        raise ValueError(f"Expected float32 stereo output, found tag={tag} bits={bits} channels={channels}")
    samples = array("f")
    usable = (len(data) - start) // 8 * 8
    samples.frombytes(data[start:start + usable])
    if sys.byteorder != "little":
        samples.byteswap()
    return {"rate": rate, "channels": channels, "data_offset": start, "samples": samples}


def window_power(wav: dict, begin: float, end: float) -> float:
    """Median over 50 ms blocks of mean L^2 + R^2 between two frame positions.

    A median ignores a short unrelated map sound inside a window and scales
    with gain squared just as a mean does, so relative levels are unchanged.
    """
    samples = wav["samples"]
    first, last = max(0, int(begin)), min(len(samples) // 2, int(end))
    if last - first < wav["rate"] // 10:
        raise ValueError("Measurement window shorter than 0.1 s")
    size = max(1, int(wav["rate"] * BLOCK))
    powers = []
    for start in range(first, last - size + 1, size):
        block = samples[start * 2:(start + size) * 2]
        powers.append(math.fsum(v * v for v in block) / size)
    powers.sort()
    middle = len(powers) // 2
    return powers[middle] if len(powers) % 2 else (powers[middle - 1] + powers[middle]) / 2


def db(power: float) -> float:
    return 10 * math.log10(power) if power > 0 else -math.inf


def measure(wav: dict, markers: dict[str, int], cases: list[dict]) -> list[dict]:
    """Turn marker byte positions in the growing wave file into case levels."""
    rate, trim = wav["rate"], TRIM * wav["rate"]

    def frame(name: str) -> float:
        if name not in markers:
            raise ValueError(f"Missing marker {name}")
        return (markers[name] - wav["data_offset"]) / 8

    def span(begin: str, end: str) -> float:
        a, b = frame(begin), frame(end)
        if not (a + 2 * trim < b):
            raise ValueError(f"Marker window {begin}..{end} is too short")
        if b > len(wav["samples"]) // 2 + rate * .05:
            raise ValueError(f"Marker {end} lies beyond the written audio")
        return window_power(wav, a + trim, b - trim)

    passes = sorted({int(m.group(1)) for m in (re.match(r"CMP_ON_\d+_\d+_P(\d+)$", k) for k in markers) if m})
    if not passes:
        raise ValueError("No measured passes")
    span("CMP_BG_BEGIN", "CMP_BG_END")  # the opening floor must be on record too
    measured = {}
    for number in passes:
        for case in cases:
            tag = f"{case['tag']}_P{number}"
            on = span(f"CMP_ON_{tag}", f"CMP_OFF_{tag}")
            after = span(f"CMP_GAP_{tag}", f"CMP_GAPEND_{tag}")
            # The gap after a placement is the floor it played over; the one
            # before belongs to the previous placement's own fade.
            measured.setdefault(case["tag"], []).append((on, after))
    results = []
    for case in cases:
        runs = measured[case["tag"]]
        if len(runs) != len(passes):
            raise ValueError(f"Missing passes for {case['tag']}")
        # A disturbance only adds power: keep the quieter pass.
        on, background = min(runs, key=lambda run: run[0])
        levels = [db(run[0]) for run in runs]
        net = on - background
        results.append({**case, "on_power": on, "background_power": background,
                        "pass_levels_db": levels, "pass_spread_db": max(levels) - min(levels),
                        "net_db": db(net) if net > 0 else None,
                        "snr_db": db(net) - db(background) if net > 0 and background > 0 else
                        (math.inf if net > 0 else None),
                        "rise_db": db(on) - db(background) if background > 0 else math.inf})
    return results


def comparable(sample: dict) -> bool:
    return sample["net_db"] is not None and sample["snr_db"] is not None and sample["snr_db"] >= MIN_SNR_DB


def assess(retail: list[dict], openq4: list[dict]) -> dict:
    """Compare openQ4's rendered distance curve with retail's and the authored one."""
    if [c["tag"] for c in retail] != [c["tag"] for c in openq4] or not retail:
        raise ValueError("Retail and openQ4 measured different cases")
    rows, failures = [], []
    by_shader: dict[str, list[tuple[dict, dict]]] = {}
    for a, b in zip(retail, openq4):
        by_shader.setdefault(a["shader"], []).append((a, b))
    compared = silent = 0
    for shader, pairs in by_shader.items():
        # The reference is the placement at the authored minimum distance: the
        # last full-volume point and the first outside openQ4's source radius.
        minimum = SHADERS[shader][1]
        reference = next((pair for pair in pairs if pair[0]["distance"] == minimum), None)
        if reference is None or not (comparable(reference[0]) and comparable(reference[1])):
            raise ValueError(f"No usable minimum-distance reference for {shader}")
        r0, q0 = reference[0]["net_db"], reference[1]["net_db"]
        for r, q in pairs:
            row = {"tag": r["tag"], "shader": shader, "distance": r["distance"], "falloff": r["falloff"],
                   "retail_db": r["net_db"], "openq4_db": q["net_db"], "retail_snr_db": r["snr_db"],
                   "openq4_snr_db": q["snr_db"], "retail_rise_db": r["rise_db"], "openq4_rise_db": q["rise_db"],
                   "retail_pass_spread_db": r.get("pass_spread_db"), "openq4_pass_spread_db": q.get("pass_spread_db")}
            if r["falloff"] <= 0:
                row["kind"] = "silent"
                silent += 1
                for owner, sample, zero in (("retail", r, r0), ("openQ4", q, q0)):
                    net = sample["on_power"] - sample["background_power"]
                    quiet = net <= 0 or db(net) - zero <= SILENT_REFERENCE_DB
                    if sample["rise_db"] > SILENT_RATIO_DB and not quiet:
                        failures.append(f"{owner} is audible at {r['tag']} ({shader} at {r['distance']:g}): "
                                        f"+{sample['rise_db']:.2f} dB over the floor")
            elif comparable(r) and comparable(q):
                row["kind"] = "compared"
                compared += 1
                expected = 20 * math.log10(r["falloff"])
                row.update(retail_relative_db=r["net_db"] - r0, openq4_relative_db=q["net_db"] - q0,
                           authored_relative_db=expected, absolute_delta_db=q["net_db"] - r["net_db"])
                row["relative_delta_db"] = row["openq4_relative_db"] - row["retail_relative_db"]
                inner = r["distance"] < minimum
                row["inner_radius"] = inner
                curve = INNER_TOLERANCE_DB if inner else RELATIVE_TOLERANCE_DB
                authored_tolerance = INNER_TOLERANCE_DB if inner else AUTHORED_TOLERANCE_DB
                checks = (("openQ4 vs retail", row["relative_delta_db"], curve),
                          ("retail vs authored", row["retail_relative_db"] - expected, authored_tolerance),
                          ("openQ4 vs authored", row["openq4_relative_db"] - expected, authored_tolerance),
                          ("openQ4 vs retail level", row["absolute_delta_db"],
                           ABSOLUTE_TOLERANCE_DB + (INNER_TOLERANCE_DB - RELATIVE_TOLERANCE_DB if inner else 0)))
                for label, delta, tolerance in checks:
                    if abs(delta) > tolerance:
                        failures.append(f"{label} differs by {delta:+.2f} dB at {r['tag']} "
                                        f"({shader} at {r['distance']:g})")
            else:
                row["kind"] = "below floor"
                # Only the quietest audible placements may sit under the floor;
                # anything louder than -30 dB of authored falloff must measure.
                if r["falloff"] > .03:
                    failures.append(f"Audible placement {r['tag']} ({shader} at {r['distance']:g}) "
                                    "is under the measurement floor")
            rows.append(row)
    if compared < 12:
        failures.append(f"Only {compared} placements were comparable; at least 12 are required")
    if silent != len(by_shader) * 2:
        failures.append("Missing silent placements at the authored maximum and beyond it")
    deltas = [row["relative_delta_db"] for row in rows if row["kind"] == "compared" and not row["inner_radius"]]
    summary = {"compared": compared, "silent": silent, "rows": rows, "failures": failures,
               "max_relative_delta_db": max(map(abs, deltas)) if deltas else None}
    if failures:
        raise AssertionError("; ".join(failures))
    return summary


def foreground_pid() -> int | None:
    """Process owning the foreground window. Reads state only; sends nothing."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    window = user32.GetForegroundWindow()
    if not window:
        return None
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
    return pid.value


class Session:
    """Launch one game and record wave-file positions as its markers appear."""

    def __init__(self, command, cwd: Path, env: dict, log: Path, wav: Path, console: Path, show: int = 4):
        self.command, self.cwd, self.env, self.log, self.wav, self.console = command, cwd, env, log, wav, console
        # 4 = SW_SHOWNOACTIVATE (openQ4 needs no focus); retail uses 1, SW_SHOWNORMAL.
        self.show = show
        self.markers: dict[str, int] = {}
        self.text = ""
        self.process = None

    def wav_size(self) -> int:
        try:
            return self.wav.stat().st_size
        except FileNotFoundError:
            return 0

    def run(self, timeout: float, on_text=None, on_poll=None) -> int:
        options = {}
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = self.show
            options["startupinfo"] = startup
        offset, generation_size = 0, 0
        with self.console.open("w", encoding="utf-8") as console:
            process = subprocess.Popen(self.command, cwd=self.cwd, env=self.env, stdin=subprocess.DEVNULL,
                                       stdout=console, stderr=subprocess.STDOUT, **options)
            self.process = process
            deadline = time.monotonic() + timeout
            try:
                while True:
                    finished = process.poll() is not None
                    if on_poll and not finished:
                        on_poll(self)
                    if self.log.is_file():
                        with self.log.open("rb") as stream:
                            stream.seek(offset)
                            chunk = stream.read()
                        cut = chunk.rfind(b"\n") + 1
                        if cut:
                            size = self.wav_size()
                            if size < generation_size:
                                # The writer restarted its file: older positions are void.
                                self.markers = {k: v for k, v in self.markers.items() if k == "__reset__"}
                                self.markers["__reset__"] = self.markers.get("__reset__", 0) + 1
                            generation_size = size
                            text = chunk[:cut].decode("utf-8", "replace")
                            offset += cut
                            self.text += text
                            for name in re.findall(r"(?m)^(CMP_[A-Z0-9_]+)\s*$", text):
                                self.markers.setdefault(name, size)
                            if on_text:
                                on_text(self.text)
                    if finished:
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError(f"{self.command[0]} did not finish")
                    time.sleep(.005)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=20)
        self.markers.pop("__reset__", None)
        return process.returncode


def prepare_retail_runtime(install: Path, openal: Path, runtime: Path) -> dict:
    """Keep a fixed retail copy so firewall and loader state stay stable."""
    runtime.mkdir(parents=True, exist_ok=True)
    identity = {}
    for name in RETAIL_FILES:
        source, target = install / name, runtime / name
        if not source.is_file():
            raise ValueError(f"Missing retail file: {source}")
        if not target.is_file() or sha(target) != sha(source):
            shutil.copy2(source, target)
        identity[name] = sha(target)
    data = openal.read_bytes()
    if data[:2] != b"MZ" or struct.unpack_from("<H", data, struct.unpack_from("<I", data, 0x3C)[0] + 4)[0] != 0x14C:
        raise ValueError("--retail-openal must be a 32-bit (x86) OpenAL Soft library")
    version = re.search(rb"v?(1\.\d+\.\d+)(?:-[0-9a-f]+)?", data[data.find(b"Initializing library"):]
                        if b"Initializing library" in data else data)
    if b"ALSOFT" not in data:
        raise ValueError("--retail-openal is not OpenAL Soft (no wave writer)")
    target = runtime / "OpenAL32.dll"
    if not target.is_file() or sha(target) != hashlib.sha256(data).hexdigest():
        target.write_bytes(data)
    identity["OpenAL32.dll"] = sha(target)
    identity["openal_source"] = str(openal)
    identity["openal_version_hint"] = version.group(1).decode() if version else None
    return identity


def run_retail(output: Path, install: Path, runtime: Path, cases: list[dict]) -> tuple[Session, dict]:
    root = output / "retail"
    save = root / "save"
    game = save / "q4base"
    game.mkdir(parents=True)
    probe = probe_commands(cases, lambda s: f"wait {max(1, round(s * 60))}")
    stage = game / "cmp_stage.cfg"
    # Retail runs command-line commands while the map is still loading, and it
    # has no after-load hook. Spin on a self-repeating file until the log shows
    # the map loaded, then publish the probe into it.
    write_script(stage, ["wait 15", "exec cmp_stage.cfg"])
    conf = root / "alsoft.ini"
    conf.write_text(ALSOFT.format(file=root / "mix.wav"), encoding="utf-8")
    settings = {
        # Lower-case search paths: retail prints a warning per path otherwise.
        "fs_basepath": str(install).lower(), "fs_savepath": str(save).lower(), "fs_cdpath": "",
        "fs_game": "", "s_useOpenAL": "1", "s_useEAXReverb": "0", "s_useEAXOcclusion": "0",
        "s_useOcclusion": "0", "s_volume": "1", "s_speakerFraction": ".65", "s_quadraticFalloff": "0",
        "s_numberOfSpeakers": "2", "s_deviceName": "", "r_fullscreen": "0", "r_mode": "3",
        "in_mouse": "0", "net_ip": "127.0.0.1", "logFile": "2", "developer": "0", "com_allowConsole": "1",
    }
    command = [str(runtime / "Quake4.exe"), *(s for k, v in settings.items() for s in ("+set", k, v)),
               "+devmap", MAP, "+exec", "cmp_stage.cfg"]
    env = {**os.environ, "ALSOFT_CONF": str(conf), "ALSOFT_DRIVERS": "wave",
           "ALSOFT_LOGFILE": str(root / "openal.log"), "ALSOFT_LOGLEVEL": "3",
           # The unmanifested 32-bit exe trips installer detection; run it as
           # the invoking user rather than elevated. Process-local only.
           "__COMPAT_LAYER": "RunAsInvoker"}
    session = Session(command, runtime, env, game / "qconsole.log", root / "mix.wav", root / "console.log",
                      show=1)
    state = {"loaded": None, "published": False, "prompted": False, "focus_lost": [], "focused_at": None,
             "aborted": None, "idle": False, "active_at": None}
    x, y, z = LISTENER
    # After the load the idle loop keeps placing the player in the sky room and
    # printing the drawn view. Retail only draws, and so only moves that view,
    # while it is the active application: the printed position is its own
    # confirmation that it is active, independent of the foreground check.
    idle = [f"setviewpos {x:g} {y:g} {z - .5:g} 0", "wait 20", "getviewpos", "exec cmp_stage.cfg"]

    def active(text: str) -> bool:
        views = re.findall(r"(?m)^\(([-\d.]+) ([-\d.]+) [-\d.]+\) [-\d.]+\s*$", text[-4000:])
        return bool(views) and math.dist(tuple(map(float, views[-1])), (x, y)) < 1.0

    def on_text(text: str) -> None:
        # Never let retail play aloud: without OpenAL it opens DirectSound on
        # the real device. Stop it the moment the log says so.
        if re.search(r"(?m)^sound: (STEREO|5\.1|QUAD|MONO)", text) or "Could not initialize OpenAL" in text:
            state["aborted"] = "retail fell back from OpenAL to DirectSound"
            session.process.kill()
        if state["loaded"] is None and re.search(r"msec to load " + re.escape(MAP), text):
            state["loaded"] = time.monotonic()

    def on_poll(current: Session) -> None:
        focused = foreground_pid() == current.process.pid
        if state["published"]:
            # Retail selects no sound world and sleeps while it is not the
            # active application, so every moment of the probe needs focus.
            # Stop at once rather than finish a run that cannot be used.
            if not focused:
                state["focus_lost"].append(current.wav_size())
                state["aborted"] = ("retail lost focus during the probe; rerun and leave its window "
                                    "focused until it closes")
                current.process.kill()
            return
        if state["loaded"] is None or time.monotonic() - state["loaded"] < 3:
            return
        if not state["idle"]:
            write_script(stage, idle)
            state["idle"] = True
        if not focused:
            if not state["prompted"]:
                print("Retail Quake 4 must be the focused window while it plays (it mutes itself otherwise).\n"
                      "Click its window once and leave it alone until it closes; the harness sends no input.",
                      flush=True)
                state["prompted"] = True
            state["focused_at"] = None
            return
        # Require two seconds of continuous focus and retail's own sign that it
        # is drawing again before starting the probe.
        state["focused_at"] = state["focused_at"] or time.monotonic()
        if time.monotonic() - state["focused_at"] >= 2 and active(current.text):
            write_script(stage, probe)
            state["published"] = True

    code = session.run(FOCUS_TIMEOUT + 600, on_text, on_poll)
    return session, {"command": command, "exit_code": code, "settings": settings,
                     "probe_published": state["published"], "focus_lost_wav_bytes": state["focus_lost"][:20],
                     "focus_lost_samples": len(state["focus_lost"]), "aborted": state["aborted"],
                     "focus_prompted": state["prompted"]}


def run_openq4(output: Path, install: Path, runtime: Path, cases: list[dict]) -> tuple[Session, dict]:
    root = output / "openq4"
    game = root / "save/baseoq4"
    game.mkdir(parents=True)
    write_script(game / "cmp_probe.cfg", probe_commands(cases, lambda s: f"waitMsec {round(s * 1000)}"))
    conf = root / "alsoft.ini"
    conf.write_text(ALSOFT.format(file=root / "mix.wav"), encoding="utf-8")
    settings = {
        "fs_basepath": str(install), "fs_savepath": str(root / "save"), "fs_devpath": str(root / "save"),
        "fs_game": "baseoq4", "com_gameMode": "SP", "si_gameType": "singleplayer", "ui_autoJoin": "0",
        "ui_retained": "1", "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "com_maxfps": "60", "r_swapInterval": "0", "in_mouse": "0", "in_joystick": "0", "in_tty": "0",
        "sys_consoleWindow": "0", "s_noSound": "0", "s_muteUnfocused": "0", "s_deviceName": "",
        "s_volume_dB": "0", "s_volume": "1", "s_speakerFraction": ".65", "s_quadraticFalloff": "0",
        "s_sourceRadiusScale": ".5", "s_useOcclusion": "0", "s_useEAXReverb": "0", "s_airAbsorption": "0",
        "s_numberOfSpeakers": "0",
        # A fresh profile runs the one-time audio migration, which turns room
        # reverb back on for some presets; mark it applied so the mix stays dry.
        "s_audioDefaultsMigrated": "2", "logFile": "2", "logFileName": "logs/openq4.log", "developer": "0",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "cmp_probe.cfg",
        "g_autoExecAfterMapLoadDelayMs": "3000",
    }
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(executable), *(s for k, v in settings.items() for s in ("+set", k, v)), "+devmap", MAP]
    env = {**os.environ, "ALSOFT_CONF": str(conf), "ALSOFT_DRIVERS": "wave",
           "ALSOFT_LOGFILE": str(root / "openal.log"), "ALSOFT_LOGLEVEL": "3"}
    session = Session(command, runtime, env, game / "logs/openq4.log", root / "mix.wav", root / "console.log")
    code = session.run(600)
    return session, {"command": command, "exit_code": code, "settings": settings}


def check_session(name: str, session: Session, info: dict) -> None:
    text = session.text
    if info.get("aborted"):
        raise ValueError(f"{name}: {info['aborted']}")
    if info.get("focus_lost_samples"):
        raise ValueError(f"{name} lost focus during the probe ({info['focus_lost_samples']} samples); "
                         "rerun and leave its window focused until it closes")
    if name == "retail" and "OpenAL: found OpenAL Soft" not in text:
        raise ValueError("retail did not render through the supplied OpenAL Soft library")
    if info["exit_code"]:
        raise ValueError(f"{name} exited {info['exit_code']}")
    if "CMP_COMPLETE" not in session.markers or "CMP_READY" not in session.markers:
        raise ValueError(f"{name} did not complete the probe")
    if re.search(r"(?mi)^(Unknown command|entity not found|FATAL ERROR)", text):
        raise ValueError(f"{name} reports a failed probe action")
    view = re.findall(r"(?m)^\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) ([-\d.]+)\s*$", text)
    if not view:
        raise ValueError(f"{name} printed no view position")
    eye = tuple(map(float, view[-1][:3]))
    if math.dist(eye, LISTENER) > 1.0 or abs((float(view[-1][3]) + 180) % 360 - 180) > .5:
        raise ValueError(f"{name} listener is at {eye}, not {LISTENER}")
    info["view"] = {"origin": eye, "yaw": float(view[-1][3])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--basepath", type=Path, required=True, help="Quake 4 1.4.2 install")
    parser.add_argument("--retail-openal", type=Path,
                        help="32-bit OpenAL Soft library (OpenAL32.dll or soft_oal.dll) for retail")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--retail-runtime", type=Path, default=RETAIL_RUNTIME)
    parser.add_argument("--openq4-report", type=Path,
                        help="reuse the openQ4 levels of an earlier report.json from this harness")
    parser.add_argument("--skip-retail", action="store_true",
                        help="unattended partial check: openQ4 against the authored curve only")
    args = parser.parse_args()
    if not args.skip_retail and not args.retail_openal:
        parser.error("--retail-openal is required unless --skip-retail is given")
    if os.name != "nt":
        parser.error("Retail Quake 4 is a Windows x86 program")
    output, runtime, install = args.output_dir.resolve(), args.runtime_dir.resolve(), args.basepath.resolve()
    if not output.is_relative_to(ROOT / ".tmp"):
        parser.error("Evidence output must be inside this checkout's .tmp directory")
    verify_runtime_is_current(runtime)
    authored = retail_shaders(install)
    loops = check_loops(install)
    output.mkdir(parents=True, exist_ok=False)
    cases = build_cases()
    report = {"status": "fail", "cases": cases, "retail_shaders": authored, "loops": loops,
              "routing": "dry: no occlusion, reverb or air filter; stereo, HRTF off, wave writer",
              "tolerances_db": {"relative": RELATIVE_TOLERANCE_DB, "inner_radius": INNER_TOLERANCE_DB,
                                "absolute": ABSOLUTE_TOLERANCE_DB,
                                "authored": AUTHORED_TOLERANCE_DB, "min_snr": MIN_SNR_DB,
                                "silent_rise": SILENT_RATIO_DB},
              "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_sha256": {}, "runtime_sha256": {},
              "stale_runtime_override": os.environ.get("OPENQ4_ALLOW_STALE_RUNTIME", "").strip() == "1"}
    for name in ("src/sound/snd_system.cpp", "src/sound/snd_emitter.cpp", "src/sound/snd_world.cpp",
                 "src/sound/sound.h", "src/sound/OpenAL/AL_SoundVoice.cpp",
                 "tools/tests/sound_distance_retail_compare.py"):
        report["source_sha256"][name] = sha(ROOT / name)
    executable = runtime / "openQ4-client_x64.exe"
    for path in (executable, runtime / "baseoq4/game-sp_x64.dll", runtime / "OpenAL32.dll",
                 runtime / "baseoq4/pak0.pk4"):
        if path.is_file():
            report["runtime_sha256"][path.relative_to(runtime).as_posix()] = sha(path)
    (output / "runner.py").write_bytes(Path(__file__).read_bytes())
    try:
        measured = {}
        runners = [] if args.openq4_report else [("openq4", run_openq4)]
        if args.openq4_report:
            earlier = json.loads(args.openq4_report.read_text(encoding="utf-8"))
            levels = earlier.get("openq4", {}).get("levels")
            if not levels or [c["tag"] for c in levels] != [c["tag"] for c in cases]:
                raise ValueError("--openq4-report has no openQ4 levels for these placements")
            if earlier.get("source_sha256", {}).get("tools/tests/sound_distance_retail_compare.py") is None:
                raise ValueError("--openq4-report did not come from this harness")
            report["openq4"] = {**earlier["openq4"], "reused_from": str(args.openq4_report.resolve()),
                                "reused_runtime_sha256": earlier.get("runtime_sha256")}
            measured["openq4"] = levels
        if not args.skip_retail:
            report["retail_runtime"] = prepare_retail_runtime(install, args.retail_openal.resolve(),
                                                              args.retail_runtime.resolve())
            runners.append(("retail", run_retail))
        for name, runner in runners:
            print(f"Running {name} ...", flush=True)
            session, info = runner(output, install, runtime if name == "openq4" else args.retail_runtime.resolve(),
                                   cases)
            report[name] = info
            info["markers"] = session.markers
            check_session(name, session, info)
            wav = read_wav(session.wav)
            info["wav"] = {"rate": wav["rate"], "frames": len(wav["samples"]) // 2, "sha256": sha(session.wav)}
            measured[name] = measure(wav, session.markers, cases)
            report[name]["levels"] = measured[name]
        # Without retail, comparing openQ4 with itself leaves only the authored
        # and silence checks: a partial result, never the retail acceptance.
        report["comparison"] = assess(measured.get("retail", measured["openq4"]), measured["openq4"])
        report["status"] = "partial: openQ4 only, retail not run" if args.skip_retail else "pass"
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2, default=str) + "\n")
    for row in report["comparison"]["rows"]:
        if row["kind"] == "compared" and args.skip_retail:
            print(f"{row['tag']:>4} {row['shader']:30} {row['distance']:7.1f}  authored {row['authored_relative_db']:7.2f}"
                  f"  openQ4 {row['openq4_relative_db']:7.2f}")
        elif row["kind"] == "compared":
            print(f"{row['tag']:>4} {row['shader']:30} {row['distance']:7.1f}  authored {row['authored_relative_db']:7.2f}"
                  f"  retail {row['retail_relative_db']:7.2f}  openQ4 {row['openq4_relative_db']:7.2f}"
                  f"  level delta {row['absolute_delta_db']:+5.2f}")
        else:
            print(f"{row['tag']:>4} {row['shader']:30} {row['distance']:7.1f}  {row['kind']}")
    if args.skip_retail:
        print(f"PARTIAL: openQ4 follows the authored curve at {report['comparison']['compared']} placements; "
              "retail was not run, so this is not the AUD-01 retail comparison.", flush=True)
        return
    print(f"PASS: {report['comparison']['compared']} placements match retail within "
          f"{RELATIVE_TOLERANCE_DB} dB (largest {report['comparison']['max_relative_delta_db']:.2f} dB).", flush=True)


if __name__ == "__main__":
    main()
