#!/usr/bin/env python3
"""REN-09 runtime qualification of shadowed volumetric lighting.

Opt-in, local and hardware-bound. One launch per backend and scene: the staged
client loads the scene's map, freezes game time at a fixed pose with shadow
maps on (cascaded sunlight included) and takes engine screenshots of the
baseline, the volumetric leaf, the leaf with stencil-shadowed lights scattering
unshadowed, the scattered light alone (r_volumetricDebug), the leaf with the
master permit r_rendererModernQuality 0, the leaf at full resolution, and the
baseline again. No input is synthesized.

Requires: the two baselines are byte-identical (the scene is frozen); the
master-off capture is byte-identical to the baseline (exact rollback); the leaf
visibly changes the frame; rendererVolumetricStatus reports the leaf effective
with at least one shadow-mapped light; and every configuration's delayed GPU
frame time is reported. A JSON report and a Markdown summary go to
--output-dir.
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
DEFAULT_OUTPUT = ROOT / ".tmp" / "volumetric-lighting-qualification"
SCENES = {
    # renderer_shadow_mapping_outdoor.py "opening": cascaded sunlight
    "outdoor": ("game/airdefense1", "10200 -6800 40 0 170 0"),
    # the swinging-light corridor of renderer_shadow_light_motion.py
    # swinging lights keep moving under g_stopTime: effect and status only
    "corridor": ("game/airdefense1", "8956 -4200 150 90"),
    "process": ("game/process1", "-2496 -2200 200 270"),
}
MOVING_SCENES = {"corridor"}
LEAF = {"r_rendererFroxelVolumetrics": "1"}
CONFIGS = (
    ("base", {}),
    ("vol", LEAF),
    ("stencil", {**LEAF, "r_volumetricStencilLights": "1"}),
    ("debug", {**LEAF, "r_volumetricDebug": "1"}),
    ("full", {**LEAF, "r_volumetricResolution": "1"}),
    ("masteroff", {**LEAF, "r_rendererModernQuality": "0"}),
    ("masteroffbase", {"r_rendererModernQuality": "0"}),
    ("baseagain", {}),
)
RESET = {"r_rendererFroxelVolumetrics": "0", "r_volumetricStencilLights": "0",
         "r_volumetricDebug": "0", "r_volumetricResolution": "2", "r_rendererModernQuality": "1"}
MIN_EFFECT_BYTES = 20000

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
    re.compile(r"GLSL .* failed for builtin/volumetric"),
    re.compile(r"volumetric lighting pass could not run"),
)


def build_cfg(pose: str, taa: bool = False) -> str:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0",
             f"set r_temporalAA {1 if taa else 0}", "set r_motionBlur 0", "set r_rendererGpuTimers 1",
             "set r_useShadowMap 1", "set r_shadowMapCSM 1", "set r_shadowMapPointLights 1"]
    lines += [f"set {cvar} {value}" for cvar, value in RESET.items()]
    lines += [f"setviewpos {pose}", "wait 60", "g_stopTime 1", "wait 60"]
    for name, cvars in CONFIGS:
        lines += [f"set {cvar} {value}" for cvar, value in RESET.items()]
        lines += [f"set {cvar} {value}" for cvar, value in cvars.items()]
        lines += ["wait 90", f"echo VOLUMETRIC_CONFIG {name}", "rendererTemporalPresentationStatus",
                  "rendererVolumetricStatus", f"screenshot screenshots/{name}.tga", "wait 5"]
    lines += ["wait 5", "quit"]
    return "\n".join(lines) + "\n"


def parse_log(log_text: str, backend: str) -> dict:
    configs: dict = {}
    current = None
    label = "Vulkan" if backend == "vk" else "OpenGL"
    for raw in log_text.splitlines():
        line = raw.strip()
        match = re.search(r"VOLUMETRIC_CONFIG (\S+)", line)
        if match:
            current = match.group(1)
            configs[current] = {}
            continue
        if current is None:
            continue
        if line.startswith("Temporal presentation: dynamicRequested="):
            values = dict(re.findall(r"(\w+)=([^\s{}]+)", line))
            configs[current]["timingValid"] = values.get("timingValid")
            configs[current]["timingUs"] = int(values.get("timing", "0us").rstrip("us") or 0)
        elif line.startswith(f"Volumetric lighting {label}:"):
            configs[current]["status"] = dict(re.findall(r"(\w+)=(\S+)", line))
        elif line.startswith("Volumetric lighting: requested="):
            configs[current]["requested"] = dict(re.findall(r"(\w+)=(\S+)", line)).get("requested")
    return configs


def load_rgb(path: Path):
    try:
        from PIL import Image
    except ImportError:
        return None
    if not path.is_file():
        return None
    image = Image.open(path).convert("RGB")
    return image.size[0], image.size[1], image.tobytes()


def compare(a, b) -> dict:
    if a is None or b is None or a[:2] != b[:2]:
        return {"comparable": False}
    first, second = a[2], b[2]
    changed = sum(1 for x, y in zip(first, second) if x != y)
    total = sum(abs(x - y) for x, y in zip(first, second))
    return {"comparable": True, "identical": changed == 0, "changedBytes": changed,
            "meanAbs": total / max(1, len(first))}


def run_scene(runtime: Path, output: Path, backend: str, scene: str, timeout: int,
              extra: list[str] | None = None, taa: bool = False) -> dict:
    map_name, pose = SCENES[scene]
    run_dir = output / f"{backend}-{scene}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    (game_dir / "volumetric.cfg").write_text(build_cfg(pose, taa), encoding="ascii")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name, value):
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    for name, value in (("logFile", 2), ("logFileName", "logs/volumetric.log"), ("developer", 1),
                        ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
                        ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
                        ("g_autoSkipCinematics", 1), ("com_maxfps", 0), ("r_swapInterval", 0),
                        ("r_fullscreen", 0), ("ui_retained", 1), ("ui_autoJoin", 0),
                        ("com_fixedTic", 1), ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
                        ("g_autoExecAfterMapLoad", "volumetric.cfg"),
                        ("g_autoExecAfterMapLoadDelayMs", 0),
                        # the Quality/Ultra shadow configuration, decided before the map loads
                        ("r_useShadowMap", 1), ("r_shadowMapCSM", 1), ("r_shadowMapPointLights", 1)):
        setv(name, value)
    for item in extra or []:
        name, _, value = item.partition("=")
        setv(name, value)
    command += ["+devmap", map_name]
    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "volumetric.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    images = {name: load_rgb(game_dir / "screenshots" / f"{name}.tga") for name, _ in CONFIGS}
    configs = parse_log(log_text, backend)
    result = {
        "backend": backend, "scene": scene, "map": map_name, "pose": pose, "exitCode": exit_code,
        "seconds": round(time.time() - started, 1), "log": str(log_path), "configs": configs,
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
        "comparisons": {name: compare(images["base"], images[name]) for name, _ in CONFIGS if name != "base"},
    }
    problems: list[str] = []
    if exit_code != 0:
        problems.append(f"exit code {exit_code}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    comparisons = result["comparisons"]
    for name, data in comparisons.items():
        if not data.get("comparable"):
            problems.append(f"{name}: capture missing or a different size")
    if not problems:
        moving = scene in MOVING_SCENES
        if not moving and not comparisons["baseagain"]["identical"]:
            problems.append(f"baselines differ ({comparisons['baseagain']['changedBytes']} bytes): scene not frozen")
        # The master also rolls back other modern domains, so the leaf's own
        # rollback compares master-off captures with and without it.
        rollback = compare(images["masteroffbase"], images["masteroff"])
        result["masterRollback"] = rollback
        if not moving and not rollback.get("identical"):
            problems.append(f"master off with the leaf differs from master off without it ({rollback.get('changedBytes')} bytes)")
        if comparisons["vol"]["changedBytes"] < MIN_EFFECT_BYTES:
            problems.append(f"vol: no visible effect ({comparisons['vol']['changedBytes']} bytes changed)")
    status = configs.get("vol", {}).get("status", {})
    if status.get("effective") != "1":
        problems.append(f"vol: not effective ({status.get('decline', 'no status')})")
    elif int(status.get("shadowMapped", "0")) <= 0:
        problems.append("vol: no shadow-mapped light scattered")
    if configs.get("masteroff", {}).get("requested") != "0":
        problems.append("masteroff: the leaf still reports requested")
    for name, _ in CONFIGS:
        if configs.get(name, {}).get("timingValid") != "1":
            problems.append(f"{name}: no valid GPU timing sample")
    result["problems"] = problems
    result["pass"] = not problems
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--scenes", default=",".join(SCENES))
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--taa", action="store_true", help="run with temporal AA on")
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra startup cvar, for diagnosis")
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"runtime": str(runtime), "runs": []}
    failed = False
    lines = ["# Volumetric lighting qualification (REN-09)", "",
             "| Backend | Scene | Config | Identical to base | Mean abs | GPU us | Lights shadowMapped/stencil/skipped | Passes |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for backend in [name for name in args.backends.split(",") if name]:
        for scene in [name for name in args.scenes.split(",") if name]:
            result = run_scene(runtime, output, backend, scene, args.timeout, args.set, args.taa)
            report["runs"].append(result)
            failed |= not result["pass"]
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend} {scene}: "
                  + ("; ".join(result["problems"]) or "ok"))
            for name, _ in CONFIGS:
                data = result["comparisons"].get(name, {"identical": True, "meanAbs": 0.0})
                config = result["configs"].get(name, {})
                status = config.get("status", {})
                skipped = sum(int(value) for key, value in status.items()
                              if key.startswith("skipped") and value.isdigit())
                stencil = int(status.get("stencilUnshadowed", 0)) + int(status.get("skippedStencil", 0))
                line = (f"| {backend} | {scene} | {name} | {'yes' if data.get('identical') else 'no'} | "
                        f"{data.get('meanAbs', 0):.4f} | {config.get('timingUs', '?')} | "
                        f"{status.get('shadowMapped', '-')}/{stencil if status else '-'}/{skipped if status else '-'} | "
                        f"{status.get('passes', '-')} |")
                lines.append(line)
                print("  " + line)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
