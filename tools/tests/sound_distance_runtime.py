#!/usr/bin/env python3
"""Measure authored distance curves through actual SP emitters and OpenAL.

Uses the installed retail archives read-only, an isolated profile and engine
commands. Null OpenAL is the default; physical output needs a listening check.
No mouse/keyboard events are sent. Captures use the engine screenshot command.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SHADERS = {
    "weapon_lightninggun_fireloop": ("sound/weapons.sndshd", 40.0, 1000.0, -6.0),
    "weapon_lightninggun_hum": ("sound/weapons.sndshd", 80.0, 900.0, -3.0),
    "ambient_light_hum01": ("sound/ambient.sndshd", 40.0, 250.0, -28.0),
}
LISTENER = (-5256.0, 2864.0, -5819.8)


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def retail_shaders(base: Path) -> dict:
    """Bind the selected authored values to their effective installed source."""
    sources = {}
    for package in sorted((base / "q4base").glob("*.pk4"), key=lambda p: p.name.lower()):
        with zipfile.ZipFile(package) as archive:
            names = {name.lower(): name for name in archive.namelist()}
            for relative in {spec[0] for spec in SHADERS.values()}:
                if relative in names:
                    data = archive.read(names[relative])
                    sources[relative] = (package, names[relative], data)
    result = {}
    for name, (relative, minimum, maximum, db) in SHADERS.items():
        if relative not in sources:
            raise ValueError(f"Missing retail shader source: {relative}")
        package, entry, data = sources[relative]
        match = re.search(r"(?m)^sound\s+" + re.escape(name) + r"\s*\{([^}]+)\}", data.decode("utf-8"))
        if not match:
            raise ValueError(f"Missing retail shader: {name}")
        body = match.group(1)
        for key, expected in (("minDistance", minimum), ("maxDistance", maximum), ("volumeDb", db)):
            value = re.search(r"(?im)^\s*" + key + r"\s+([-\d.]+)", body)
            if not value or float(value.group(1)) != expected:
                raise ValueError(f"Unexpected retail {key} for {name}")
        if not re.search(r"(?m)^\s*looping\s*$", body):
            raise ValueError(f"Expected looping retail shader: {name}")
        result[name] = {"package": str(package), "entry": entry, "source_sha256": hashlib.sha256(data).hexdigest(),
                        "min": minimum, "max": maximum, "volume_db": db}
    return result


def scripts(output: Path, physical: bool, quadratic: bool) -> tuple[Path, list[dict]]:
    game = output / "save/baseoq4"
    game.mkdir(parents=True)
    # setviewpos and Teleport each add 0.25 units to the standing eye height.
    view = (LISTENER[0], LISTENER[1], LISTENER[2] - .5)
    commands = ["god", "notarget", "noclip",
                f"setviewpos {' '.join(map(str, view))} 0 0 0", "waitMsec 1000",
                "s_volume", "s_volume_dB", "s_speakerFraction", "s_quadraticFalloff", "s_sourceRadiusScale",
                "echo DISTANCE_GAMEPLAY_READY", 'screenshot "screenshots/distance-fixture.tga"']
    cases = []
    for index, (name, (_, minimum, maximum, db)) in enumerate(SHADERS.items()):
        distances = (0.0, minimum * .25, minimum * .5, minimum,
                     minimum + (maximum - minimum) * .25, (minimum + maximum) * .5,
                     minimum + (maximum - minimum) * .75, maximum, maximum + 20)
        for point, distance in enumerate(distances):
            tag = f"{index}_{point}"
            x, y, z = LISTENER
            commands += [f'spawn speaker name distance_fixture s_shader {name} origin "{x + distance:g} {y:g} {z:g}"',
                         f"waitMsec {2500 if physical else 750}", f"echo DISTANCE_SAMPLE_{tag}",
                         f"s_soundDistances {name}", "echo DISTANCE_SAMPLE_END",
                         "remove distance_fixture", "wait 5"]
            frac = 1.0 if distance <= minimum else max(0.0, (maximum - distance) / (maximum - minimum))
            cases.append({"tag": tag, "shader": name, "distance": distance,
                          "origin": [x + distance, y, z], "quadratic": quadratic,
                          "falloff": frac * frac if quadratic else frac, "authored_db": db})
    commands += ["echo DISTANCE_COMPLETE", "wait 5", "quit"]
    write_script(game / "distance_probe.cfg", commands)
    return game, cases


def fields(line: str) -> dict[str, str]:
    return {m.group(1): m.group(2).strip('"') for m in re.finditer(r'(\w+)=("[^"]*"|\S+)', line)}


def vector(value: str) -> tuple[float, float, float]:
    parts = tuple(map(float, value.split(",")))
    if len(parts) != 3 or not all(math.isfinite(v) for v in parts):
        raise ValueError("Invalid diagnostic position")
    return parts


def collect(log: str, cases: list[dict]) -> list[dict]:
    if "DISTANCE_COMPLETE" not in log or "DISTANCE_GAMEPLAY_READY" not in log:
        raise ValueError("Missing gameplay/completion marker")
    if re.search(r"Unknown command|FATAL ERROR|ERROR:", log):
        raise ValueError("Engine reports a failed diagnostic action")
    measured = []
    for case in cases:
        block = re.search(r"(?m)^DISTANCE_SAMPLE_" + case["tag"] + r"\s*\n(.*?)^DISTANCE_SAMPLE_END",
                          log, re.S | re.M)
        if not block:
            raise ValueError(f"Missing distance sample {case['tag']}")
        worlds = re.findall(r"(?m)^SOUND_DISTANCE_WORLD (.+)$", block.group(1))
        if len(worlds) != 1:
            raise ValueError(f"Missing listener at {case['tag']}")
        world = fields(worlds[0])
        rows = [tuple(map(fields, row)) for row in re.findall(
            r"(?m)^SOUND_DISTANCE (.+)\nSOUND_DISTANCE_BACKEND (.+)$", block.group(1))]
        # A stock map may contain this shader too. Identify the injected emitter
        # by its known origin rather than choosing the first or loudest channel.
        expected_origin = case["origin"]
        matches = [(channel, backend) for channel, backend in rows
                   if channel["shader"] == case["shader"]
                   and math.dist(vector(channel["origin"]), expected_origin) < .001]
        if len(matches) != 1:
            raise ValueError(f"Expected one fixture at {case['tag']}, found {len(matches)}")
        channel, backend = matches[0]
        measured.append({**case, "world": world, "channel": channel, "backend": backend,
                         "matching_shader_channels": len(rows)})
    return measured


def assess(measured: list[dict], physical: bool) -> None:
    for case in measured:
        world, channel, backend = case["world"], case["channel"], case["backend"]
        if world["available"] != "1" or int(world["area"]) < 0:
            raise ValueError(f"Listener is outside the map at {case['tag']}")
        expected = SHADERS[case["shader"]]
        for key, value in (("min", expected[1]), ("max", expected[2])):
            if not math.isclose(float(channel[key]), value, abs_tol=.001):
                raise ValueError(f"{key} changed at {case['tag']}: {channel[key]} != {value}")
        geometric = math.dist(vector(channel["origin"]), vector(world["origin"]))
        direct = float(channel["direct"])
        # LengthFast has about 0.17% approximation error. This still rejects
        # conversion to metres or a changed source/listener placement.
        if not math.isclose(geometric, case["distance"], abs_tol=.001):
            raise ValueError(f"Fixture/listener moved at {case['tag']}")
        if not math.isclose(direct, geometric, rel_tol=.002, abs_tol=.001):
            raise ValueError(f"Distance unit mismatch at {case['tag']}: {direct} != {geometric}")
        if not math.isclose(float(channel["spatial"]), direct, abs_tol=.001) or channel["portals"] != "0":
            raise ValueError(f"Dry fixture unexpectedly uses portal distance at {case['tag']}")
        # Quake 4's dBToScale and the mixer's DBtoLinear both use 2^(dB/6).
        authored = 2 ** (case["authored_db"] / 6)
        if not math.isclose(float(channel["authoredGain"]), authored, rel_tol=.001, abs_tol=.00001):
            raise ValueError(f"Authored gain changed at {case['tag']}")
        frac = min(1.0, max(0.0, (expected[2] - direct) / (expected[2] - expected[1])))
        falloff = frac * frac if case["quadratic"] else frac
        channel_gain = authored * .65 * falloff
        expected_gain = channel_gain * (.25 if physical else 1)
        case.update(geometric_distance=geometric, measured_falloff=falloff,
                    expected_gain=expected_gain)
        if backend["available"] == "0":
            if backend.get("error") or channel_gain >= 1 / 1024:
                raise ValueError(f"Audible fixture has no hardware voice at {case['tag']}")
            continue
        if backend["playing"] != "1":
            raise ValueError(f"Fixture voice is stopped at {case['tag']}")
        gain = float(backend["gain"]) * float(backend["listenerGain"])
        case["observed_gain"] = gain
        if not math.isclose(gain, expected_gain, rel_tol=.001, abs_tol=.00001):
            raise ValueError(f"Gain mismatch at {case['tag']}: {gain} != {expected_gain}")
        if not math.isclose(math.dist(vector(backend["position"]), (0, 0, 0)), geometric, abs_tol=.001):
            raise ValueError(f"OpenAL position changed units at {case['tag']}")
        if float(backend["rolloff"]) != 0:
            raise ValueError("OpenAL adds distance attenuation on top of the engine curve")
        if not math.isclose(float(backend["innerRadius"]), expected[1], abs_tol=.001):
            raise ValueError("Authored inner radius was converted to another unit")
        if not math.isclose(float(backend["sourceRadius"]), expected[1] * .5, abs_tol=.001):
            raise ValueError("Unexpected OpenAL source radius (or unsupported extension)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--audio-output", choices=("null", "physical"), default="null")
    parser.add_argument("--quadratic", action="store_true")
    args = parser.parse_args()
    output, runtime = args.output_dir.resolve(), args.runtime_dir.resolve()
    if not output.is_relative_to(ROOT / ".tmp"):
        parser.error("Evidence output must be inside this checkout's .tmp directory")
    verify_runtime_is_current(runtime)
    authored = retail_shaders(args.basepath.resolve())
    output.mkdir(parents=True, exist_ok=False)
    physical = args.audio_output == "physical"
    game, cases = scripts(output, physical, args.quadratic)
    settings = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(output / "save"),
        "fs_devpath": str(output / "save"), "fs_game": "baseoq4",
        "com_gameMode": "SP", "si_gameType": "singleplayer", "ui_autoJoin": "0", "ui_retained": "1",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "com_maxfps": "60", "r_swapInterval": "0", "in_mouse": "0", "in_joystick": "0", "in_tty": "0",
        "sys_consoleWindow": "0", "s_noSound": "0", "s_muteUnfocused": "0", "s_deviceName": "",
        "s_volume_dB": "-12" if physical else "0", "s_volume": "1", "s_speakerFraction": ".65",
        "s_quadraticFalloff": "1" if args.quadratic else "0", "s_sourceRadiusScale": ".5",
        "s_useOcclusion": "0", "s_useEAXReverb": "0", "s_airAbsorption": "0",
        # Without this, a fresh profile's one-time audio migration turns room
        # reverb back on for the minimum/lowpower/performance presets.
        "s_audioDefaultsMigrated": "2",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "distance_probe.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(executable), *(s for key, value in settings.items() for s in ("+set", key, value)),
               "+devmap", "game/hangar1"]
    env = {**os.environ, "ALSOFT_LOGFILE": str(output / "openal.log"), "ALSOFT_LOGLEVEL": "3"}
    if physical:
        env.pop("ALSOFT_DRIVERS", None)
    else:
        env["ALSOFT_DRIVERS"] = "null"
    options = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        options["startupinfo"] = startup
    report = {"status": "fail", "command": command, "retail_shaders": authored, "cases": cases,
              "audio_output": args.audio_output, "routing": "dry, no portal occlusion or air filter",
              "audible_result": "pending listening check" if physical else "not tested",
              "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_sha256": {}, "runtime_sha256": {}}
    for name in ("src/sound/snd_system.cpp", "src/sound/snd_emitter.cpp", "src/sound/snd_shader.cpp",
                 "src/sound/sound.h", "src/sound/OpenAL/AL_SoundHardware.cpp", "src/sound/OpenAL/AL_SoundVoice.cpp",
                 "tools/tests/sound_distance_runtime.py"):
        report["source_sha256"][name] = sha(ROOT / name)
    for path in (executable, runtime / "baseoq4/game-sp_x64.dll", runtime / "baseoq4/game-sp_x64.so",
                 runtime / "OpenAL32.dll", runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"):
        if path.is_file():
            report["runtime_sha256"][path.relative_to(runtime).as_posix()] = sha(path)
    (output / "runner.py").write_bytes(Path(__file__).read_bytes())
    log = game / "logs/openq4.log"
    try:
        with (output / "console.log").open("w", encoding="utf-8") as console:
            process = subprocess.Popen(command, cwd=runtime, env=env, stdin=subprocess.DEVNULL,
                                       stdout=console, stderr=subprocess.STDOUT, **options)
            try:
                deadline, seen = time.monotonic() + 240, set()
                while process.poll() is None:
                    text = log.read_text(errors="replace") if log.is_file() else ""
                    for marker in re.findall(r"(?m)^DISTANCE_SAMPLE_(\d+_\d+)\s*$", text):
                        if marker not in seen:
                            seen.add(marker)
                            print("Distance sample " + marker, flush=True)
                    if time.monotonic() > deadline:
                        raise TimeoutError("Distance fixture did not finish")
                    time.sleep(.5)
                report["exit_code"] = process.returncode
                if process.returncode:
                    raise ValueError(f"Engine exited {process.returncode}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=20)
                    report["forced_stop"] = True
        report["samples"] = collect(log.read_text(errors="replace"), cases)
        assess(report["samples"], physical)
        capture = game / "screenshots/distance-fixture.tga"
        if not capture.is_file():
            raise ValueError("Missing engine gameplay capture")
        report.update(status="pass", capture_sha256=sha(capture))
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: actual emitter/OpenAL distance measurements; audible acceptance is separate.", flush=True)


if __name__ == "__main__":
    main()
