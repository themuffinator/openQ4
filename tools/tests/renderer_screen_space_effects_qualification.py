#!/usr/bin/env python3
"""REN-06 runtime qualification of the default-off SSR and SSGI leaves.

Opt-in, local and hardware-bound. In one launch per scene and backend the
staged client loads a stock map, freezes game time at a fixed pose and takes
engine screenshots of: the baseline, each leaf alone, all three together, all
three with the master permit r_rendererModernQuality 0, and the baseline
again. No input is synthesized.

Requires: the two baselines are byte-identical (the scene is frozen); the
master-off capture is byte-identical to the baseline (exact rollback); the
combination visibly changes every scene; SSR and SSGI each visibly change at
least one scene on every backend (a pose with nothing bright to reflect
leaves SSR nothing to add, which is recorded, not failed); and every
configuration's delayed GPU frame time stays inside a declared bound. A JSON
report and a Markdown summary go to --output-dir.

The third leaf, r_rendererFroxelVolumetrics, is REN-09's shadowed volumetric
lighting since 21 November 2026: it scatters light only from shadow-mapped
lights, so whether it changes a pose depends on that pose's lights and is
REN-09's evidence, not this harness's. It stays in the matrix because the
combination and the master-off rollback must hold with it enabled; its own
effect is recorded and only required with --require-volumetric.

--scenes picks from SCENES (default: the original airdefense1 opening pose);
"all" runs every scene. A scene without a pose is captured where the player
starts, after its settle time.
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from physics_workloads import wait_for_memory  # noqa: E402
DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "screen-space-effects-qualification"
# name: (map, "x y z pitch yaw roll" or None for the player start, settle milliseconds before the freeze)
SCENES = {
    # renderer_shadow_mapping_outdoor.py poses: open terrain under sunlight.
    "airdefense1-opening": ("game/airdefense1", "10200 -6800 40 0 170 0", 0),
    "airdefense1-wreckage": ("game/airdefense1", "9800 -7200 80 8 230 0", 0),
    "airdefense1-tank": ("game/airdefense1", "9400 -7450 220 8 175 0", 0),
    # Interiors where the player starts. (storage1 and mcc_landing are no use
    # here: one opens on a scripted camera that game time does not freeze, the
    # other on a black screen, and the checks below reject both.)
    "medlabs-start": ("game/medlabs", None, 4000),
    "tram1-start": ("game/tram1", None, 6000),
    "hangar1-start": ("game/hangar1", None, 6000),
    "building_b-start": ("game/building_b", None, 6000),
}
DEFAULT_SCENE = "airdefense1-opening"
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
REQUIRED_EVERYWHERE = ("all",)
REQUIRED_SOMEWHERE = ("ssr", "ssgi")
# Visible effect: at least this share of the capture's channel bytes changes
# against the baseline (20,000 of a 2560 x 1440 capture's 11 million). Console
# tokens split at '-', so configuration names are single words.
MIN_EFFECT_SHARE = 20000 / (2560 * 1440 * 3)
# A frozen pose that shows nothing proves nothing: the baseline must have content.
MIN_BASE_MEAN = 4.0
MIN_BASE_DISTINCT = 64
# Bounded work: delayed whole-frame GPU time may grow by at most this factor
# plus a fixed allowance over the baseline (2560 x 1440 class targets).
GPU_FACTOR = 2.0
GPU_ALLOWANCE_US = 6000

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
)


def build_cfg(pose: str | None, settle_msec: int) -> str:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0",
             "set r_temporalAA 0", "set r_motionBlur 0",
             "set r_rendererGpuTimers 1", "set r_rendererModernQuality 1"]
    lines += [f"set {leaf} 0" for leaf in LEAVES]
    if settle_msec:
        lines += [f"waitMsec {settle_msec}"]
    if pose:
        lines += [f"setviewpos {pose}"]
    lines += ["wait 60", "g_stopTime 1", "wait 60", "viewpos"]
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


def run_backend(args: argparse.Namespace, runtime: Path, output: Path, scene: str, backend: str) -> dict:
    map_name, pose, settle_msec = SCENES[scene]
    run_dir = output / scene / backend
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    (game_dir / "screenspace.cfg").write_text(build_cfg(pose, settle_msec), encoding="ascii")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name, value):
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    settings = [("logFile", 2), ("logFileName", "logs/screenspace.log"), ("developer", 1),
                ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
                ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
                ("g_autoSkipCinematics", 1), ("com_maxfps", 0), ("r_swapInterval", 0),
                ("r_fullscreen", 0), ("ui_retained", 1), ("ui_autoJoin", 0),
                ("com_fixedTic", 1), ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
                ("g_autoExecAfterMapLoad", "screenspace.cfg"),
                ("g_autoExecAfterMapLoadDelayMs", 0)]
    if args.hidden_window:
        settings += [("r_hiddenWindow", 1), ("in_mouse", 0), ("s_noSound", 1)]
    if args.size:
        width, height = args.size
        settings += [("r_mode", -1), ("r_customWidth", width), ("r_customHeight", height),
                     ("r_windowWidth", width), ("r_windowHeight", height), ("r_borderless", 0)]
    for name, value in settings:
        setv(name, value)
    command += ["+devmap", map_name]
    wait_for_memory(args.min_free_gb)
    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=args.timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "screenspace.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    images = {name: load_rgb(game_dir / "screenshots" / f"{name}.tga") for name, _ in CONFIGS}
    viewpos = re.findall(r"origin: \(([^)]*)\) angles: \(([^)]*)\)", log_text)
    result = {
        "scene": scene, "map": map_name, "pose": pose or "player start",
        "viewpos": " ".join(viewpos[-1]) if viewpos else None,
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
        width, height, base = images["base"]
        result["size"] = [width, height]
        result["baseMean"] = round(sum(base) / len(base), 2)
        result["baseDistinct"] = len(set(base[i:i + 3] for i in range(0, len(base), 3 * 97)))
        if result["baseMean"] < MIN_BASE_MEAN or result["baseDistinct"] < MIN_BASE_DISTINCT:
            problems.append(f"the baseline shows no scene (mean {result['baseMean']}, "
                            f"{result['baseDistinct']} sampled colours)")
        if result["noisePixels"] is not None and result["noisePixels"] > 0.01 * width * height:
            problems.append(f"{result['noisePixels']} pixels are not frozen between the baselines (over 1%)")
        if not comparisons["masteroff"]["identical"]:
            problems.append(f"master off differs from the baseline ({comparisons['masteroff']['changedBytes']} bytes)")
        minimum = MIN_EFFECT_SHARE * width * height * 3
        result["visible"] = {name: comparisons[name]["changedBytes"] >= minimum for name in ("froxel", "ssr", "ssgi", "all")}
        for name in REQUIRED_EVERYWHERE + (("froxel",) if args.require_volumetric else ()):
            if not result["visible"][name]:
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


def parse_size(text: str) -> tuple[int, int] | None:
    if text in ("", "native"):
        return None
    width, height = (int(part) for part in text.lower().split("x"))
    return width, height


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--scenes", default=DEFAULT_SCENE, help=f"comma-separated, or all: {', '.join(SCENES)}")
    parser.add_argument("--hidden-window", action="store_true", help="render in a hidden window (shared machines)")
    parser.add_argument("--size", type=parse_size, default=None, help="window size WxH (default: the client's own)")
    parser.add_argument("--require-volumetric", action="store_true",
                        help="also require the volumetric leaf to change the frame (poses with shadow-mapped lights)")
    parser.add_argument("--min-free-gb", type=float, default=5.0,
                        help="wait (up to 15 minutes) for this much free commit before each launch")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    scenes = list(SCENES) if args.scenes == "all" else [name for name in args.scenes.split(",") if name]
    unknown = [name for name in scenes if name not in SCENES]
    if unknown:
        parser.error(f"unknown scene(s): {', '.join(unknown)}")
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"runtime": str(runtime), "scenes": scenes, "runs": []}
    failed = False
    lines = ["# Screen-space effects qualification", "",
             "| Scene | Backend | Config | Identical to base | Mean abs | Changed bytes | GPU us | Effects |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for scene in scenes:
        for backend in [name for name in args.backends.split(",") if name]:
            result = run_backend(args, runtime, output, scene, backend)
            report["runs"].append(result)
            failed |= not result["pass"]
            print(f"{'PASS' if result['pass'] else 'FAIL'} {scene} {backend}: unfrozen pixels "
                  f"{result.get('noisePixels')}; " + ("; ".join(result["problems"]) or "ok"), flush=True)
            for name, _ in CONFIGS:
                data = result["comparisons"].get(name, {"identical": True, "meanAbs": 0.0, "changedBytes": 0})
                timing = result["timings"].get(name, {})
                line = (f"| {scene} | {backend} | {name} | {'yes' if data.get('identical') else 'no'} | "
                        f"{data.get('meanAbs', 0):.4f} | {data.get('changedBytes', 0)} | "
                        f"{timing.get('timingUs', '?')} | {timing.get('screenEffects', '?')} |")
                lines.append(line)
                print("  " + line, flush=True)
    for backend in sorted({run["backend"] for run in report["runs"]}):
        for name in REQUIRED_SOMEWHERE:
            if not any(run.get("visible", {}).get(name) for run in report["runs"] if run["backend"] == backend):
                failed = True
                report.setdefault("problems", []).append(f"{backend}: {name} visibly changed no scene")
                print(f"FAIL {backend}: {name} visibly changed no scene", flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
