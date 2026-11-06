#!/usr/bin/env python3
"""REN-06 runtime qualification of the default-off froxel, SSR and SSGI leaves.

Opt-in, local and hardware-bound. In one launch per backend the staged client
loads game/airdefense1, freezes game time at a fixed outdoor pose and takes
engine screenshots of: the baseline, each leaf alone, all three together, all
three with the master permit r_rendererModernQuality 0, and the baseline
again. No input is synthesized.

Requires: the two baselines are byte-identical (the scene is frozen); the
master-off capture is byte-identical to the baseline (exact rollback); each
leaf and the combination visibly change the frame; and every configuration's
delayed GPU frame time stays inside a declared bound. A JSON report and a
Markdown summary go to --output-dir.
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

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "screen-space-effects-qualification"
MAP = "game/airdefense1"
POSE = "10200 -6800 40 0 170 0"	# renderer_shadow_mapping_outdoor.py "opening"
LEAVES = ("r_rendererFroxelVolumetrics", "r_rendererSSR", "r_rendererSSGI")
CONFIGS = (
    ("base", {}),
    ("froxel", {"r_rendererFroxelVolumetrics": "1"}),
    ("ssr", {"r_rendererSSR": "1"}),
    ("ssgi", {"r_rendererSSGI": "1"}),
    ("all", {leaf: "1" for leaf in LEAVES}),
    ("masteroff", {**{leaf: "1" for leaf in LEAVES}, "r_rendererModernQuality": "0"}),
    ("baseagain", {}),
)
# Visible effect: at least this many changed channel bytes against the
# baseline (a 2560 x 1440 capture has about 11 million). Console tokens split
# at '-', so configuration names are single words.
MIN_EFFECT_BYTES = 20000
# Bounded work: delayed whole-frame GPU time may grow by at most this factor
# plus a fixed allowance over the baseline (2560 x 1440 class targets).
GPU_FACTOR = 2.0
GPU_ALLOWANCE_US = 6000

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
)


def build_cfg() -> str:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0",
             "set r_temporalAA 0", "set r_motionBlur 0",
             "set r_rendererGpuTimers 1", "set r_rendererModernQuality 1"]
    lines += [f"set {leaf} 0" for leaf in LEAVES]
    lines += [f"setviewpos {POSE}", "wait 60", "g_stopTime 1", "wait 60"]
    for name, cvars in CONFIGS:
        lines += ["set r_rendererModernQuality 1"] + [f"set {leaf} 0" for leaf in LEAVES]
        lines += [f"set {cvar} {value}" for cvar, value in cvars.items()]
        lines += ["wait 90", f"echo SCREEN_SPACE_CONFIG {name}", "rendererTemporalPresentationStatus",
                  f"screenshot screenshots/{name}.tga", "wait 5"]
    lines += ["wait 5", "quit"]
    return "\n".join(lines) + "\n"


def parse_timings(log_text: str) -> dict:
    timings: dict = {}
    current = None
    for raw in log_text.splitlines():
        line = raw.strip()
        match = re.search(r"SCREEN_SPACE_CONFIG (\S+)", line)
        if match:
            current = match.group(1)
            continue
        if current and line.startswith("Temporal presentation: dynamicRequested="):
            values = dict(re.findall(r"(\w+)=([^\s{}]+)", line))
            timings[current] = {
                "timingValid": values.get("timingValid"),
                "timingUs": int(values.get("timing", "0us").rstrip("us") or 0),
                "screenEffects": values.get("screenEffects"),
            }
            current = None
    return timings


def load_rgb(path: Path) -> tuple[int, int, bytes] | None:
    try:
        from PIL import Image
    except ImportError:
        return None
    if not path.is_file():
        return None
    image = Image.open(path).convert("RGB")
    return image.size[0], image.size[1], image.tobytes()


def noise_mask(a, b) -> set | None:
    """Pixels that changed between the two baselines: not frozen by g_stopTime."""
    if a is None or b is None or a[:2] != b[:2]:
        return None
    first, second = a[2], b[2]
    return {i // 3 for i in range(len(first)) if first[i] != second[i]}


def compare(a, b, noise: set) -> dict:
    if a is None or b is None or a[:2] != b[:2]:
        return {"comparable": False}
    first, second = a[2], b[2]
    total = 0
    changed = 0
    maximum = 0
    for i, (x, y) in enumerate(zip(first, second)):
        delta = abs(x - y)
        if delta and i // 3 not in noise:
            changed += 1
            total += delta
            if delta > maximum:
                maximum = delta
    return {"comparable": True, "identical": changed == 0, "changedBytes": changed,
            "meanAbs": total / len(first), "maxAbs": maximum}


def run_backend(runtime: Path, output: Path, backend: str, timeout: int) -> dict:
    run_dir = output / backend
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    (game_dir / "screenspace.cfg").write_text(build_cfg(), encoding="ascii")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name, value):
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    for name, value in (("logFile", 2), ("logFileName", "logs/screenspace.log"), ("developer", 1),
                        ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
                        ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
                        ("g_autoSkipCinematics", 1), ("com_maxfps", 0), ("r_swapInterval", 0),
                        ("r_fullscreen", 0), ("ui_retained", 1), ("ui_autoJoin", 0),
                        ("com_fixedTic", 1), ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
                        ("g_autoExecAfterMapLoad", "screenspace.cfg"),
                        ("g_autoExecAfterMapLoadDelayMs", 0)):
        setv(name, value)
    command += ["+devmap", MAP]
    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "screenspace.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    images = {name: load_rgb(game_dir / "screenshots" / f"{name}.tga") for name, _ in CONFIGS}
    result = {
        "backend": backend, "exitCode": exit_code, "seconds": round(time.time() - started, 1),
        "log": str(log_path), "timings": parse_timings(log_text),
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
    }
    noise = noise_mask(images["base"], images["baseagain"])
    result["noisePixels"] = len(noise) if noise is not None else None
    result["comparisons"] = {name: compare(images["base"], images[name], noise or set())
                             for name, _ in CONFIGS if name != "base"}
    problems: list[str] = []
    if exit_code != 0:
        problems.append(f"exit code {exit_code}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    comparisons = result["comparisons"]
    for name, data in comparisons.items():
        if not data.get("comparable"):
            problems.append(f"{name}: capture missing or a different size")
    if not problems:
        width, height = images["base"][:2]
        if result["noisePixels"] is not None and result["noisePixels"] > 0.01 * width * height:
            problems.append(f"{result['noisePixels']} pixels are not frozen between the baselines (over 1%)")
        if not comparisons["masteroff"]["identical"]:
            problems.append(f"master off differs from the baseline ({comparisons['masteroff']['changedBytes']} bytes)")
        for name in ("froxel", "ssr", "ssgi", "all"):
            if comparisons[name]["changedBytes"] < MIN_EFFECT_BYTES:
                problems.append(f"{name}: no visible effect ({comparisons[name]['changedBytes']} bytes changed)")
    timings = result["timings"]
    base_us = timings.get("base", {}).get("timingUs", 0)
    for name, timing in timings.items():
        if timing.get("timingValid") != "1":
            problems.append(f"{name}: no valid GPU timing sample")
        elif base_us and timing["timingUs"] > GPU_FACTOR * base_us + GPU_ALLOWANCE_US:
            problems.append(f"{name}: GPU {timing['timingUs']} us exceeds the bound over the baseline {base_us} us")
    if set(timings) != {name for name, _ in CONFIGS}:
        problems.append(f"timing for {sorted(timings)} only")
    result["problems"] = problems
    result["pass"] = not problems
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"map": MAP, "pose": POSE, "runtime": str(runtime), "backends": []}
    failed = False
    lines = ["# Screen-space effects qualification", "", f"`{MAP}` at `{POSE}`, frozen.", "",
             "| Backend | Config | Identical to base | Mean abs | Changed bytes | GPU us | Effects |",
             "| --- | --- | --- | --- | --- | --- | --- |"]
    for backend in [name for name in args.backends.split(",") if name]:
        result = run_backend(runtime, output, backend, args.timeout)
        report["backends"].append(result)
        failed |= not result["pass"]
        print(f"{'PASS' if result['pass'] else 'FAIL'} {backend}: unfrozen pixels {result.get('noisePixels')}; "
              + ("; ".join(result["problems"]) or "ok"))
        for name, _ in CONFIGS:
            data = result["comparisons"].get(name, {"identical": True, "meanAbs": 0.0, "changedBytes": 0})
            timing = result["timings"].get(name, {})
            line = (f"| {backend} | {name} | {'yes' if data.get('identical') else 'no'} | "
                    f"{data.get('meanAbs', 0):.4f} | {data.get('changedBytes', 0)} | "
                    f"{timing.get('timingUs', '?')} | {timing.get('screenEffects', '?')} |")
            lines.append(line)
            print("  " + line)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
