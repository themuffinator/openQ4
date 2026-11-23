#!/usr/bin/env python3
"""REN-09 cost tiers and tuning candidates of shadowed volumetric lighting.

Opt-in, local and hardware-bound; the companion of renderer_volumetric_lighting.py
(frozen stock views) and renderer_volumetric_motion.py (motion, scale, subviews).
Each scene launches the staged client once per backend, freezes game time at a
fixed pose with the Quality shadow configuration and steps through settings
with console commands only (no input is synthesized).

--mode cost: at --size (2560x1440 by default, the FPS-01 resolution) every tier
  of the leaf is held while the delayed whole-frame GPU time
  (r_rendererGpuTimers) is sampled; the leaf-off frame is sampled before, between
  and after the tiers. The report gives each tier's median frame time, its cost
  over the leaf-off frame and the lights and passes it drew. A hidden window on
  a machine other work shares is not budget evidence: the report says which
  machine state it measured and leaves the FPS-01 gate to a quiet reference run.

--mode tuning: at 1280x720 the defaults and a few candidate settings are
  captured beside the leaf-off frame, with how much each changes the picture,
  and laid out on one contact sheet per scene for review.

A JSON report and a Markdown summary go to --output-dir.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import renderer_volumetric_motion as motion  # noqa: E402
from renderer_temporal_stability import luma, read_tga  # noqa: E402

DEFAULT_OUTPUT = ROOT / ".tmp" / "volumetric-tiers"

# name: (map, pose, commands before the freeze)
SCENES = {
    # renderer_volumetric_lighting.py "outdoor": cascaded sunlight
    "outdoor": ("game/airdefense1", "10200 -6800 40 0 170 0", []),
    "corridor": ("game/process1", "-2496 -2200 200 270", []),
    # a busy stock corridor seen along the flashlight it carries
    "flashlight": ("game/airdefense2", "-1000 1568 84 0 -90 0", ["testFlashlight 1"]),
    # the level's own opening view (no pose): a small cabin under bright lights
    "tram": ("game/tram1", None, []),
}

LEAF = {"r_rendererFroxelVolumetrics": "1"}
DEFAULTS = {"r_rendererFroxelVolumetrics": "0", "r_volumetricResolution": "2", "r_froxelVolumetricSlices": "12",
            "r_volumetricMaxLights": "32", "r_volumetricIntensity": "12", "r_volumetricSunIntensity": "0.08",
            "r_froxelVolumetricDensity": "0.00012", "r_volumetricAnisotropy": "0.35",
            "r_volumetricStencilLights": "0", "r_volumetricDebug": "0"}

TIERS = (
    ("half-8", {"r_volumetricResolution": "2", "r_froxelVolumetricSlices": "8"}),
    ("half-12 (default)", {}),
    ("half-16", {"r_froxelVolumetricSlices": "16"}),
    ("full-8", {"r_volumetricResolution": "1", "r_froxelVolumetricSlices": "8"}),
    ("full-12", {"r_volumetricResolution": "1"}),
    ("full-16", {"r_volumetricResolution": "1", "r_froxelVolumetricSlices": "16"}),
    ("half-12, 4 lights", {"r_volumetricMaxLights": "4"}),
    ("half-12, 8 lights", {"r_volumetricMaxLights": "8"}),
)
SAMPLES = 9

CANDIDATES = (
    ("default", {}),
    ("intensity 3", {"r_volumetricIntensity": "3"}),
    ("intensity 6", {"r_volumetricIntensity": "6"}),
    ("intensity 24", {"r_volumetricIntensity": "24"}),
    ("sun 0.04", {"r_volumetricSunIntensity": "0.04"}),
    ("sun 0.16", {"r_volumetricSunIntensity": "0.16"}),
    ("anisotropy 0.6", {"r_volumetricAnisotropy": "0.6"}),
    ("stencil lights", {"r_volumetricStencilLights": "1"}),
)


def slug(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def settings(cvars: dict[str, str]) -> list[str]:
    return [f"set {name} {value}" for name, value in {**DEFAULTS, **cvars}.items()]


def preamble(pose: str | None, commands: list[str], taa: int) -> list[str]:
    return ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", f"set r_temporalAA {taa}",
            "set r_motionBlur 0", "set r_rendererGpuTimers 1", *settings({}),
            *([f"setviewpos {pose}"] if pose else []), *commands, "wait 60", "g_stopTime 1", "wait 60"]


def cost_cfg(pose: str | None, commands: list[str]) -> list[str]:
    lines = preamble(pose, commands, 0)

    def step(label: str, cvars: dict[str, str]) -> None:
        lines.extend([*settings(cvars), "wait 40", f"echo VOLUMETRIC_CONFIG {label}", "rendererVolumetricStatus"])
        for _ in range(SAMPLES):
            lines.extend(["wait 8", "echo VOLUMETRIC_SAMPLE", "rendererTemporalPresentationStatus"])

    step("off_0", {})
    for index, (name, cvars) in enumerate(TIERS):
        step(slug(name), {**LEAF, **cvars})
        if index == len(TIERS) // 2 - 1:
            step("off_1", {})
    step("off_2", {})
    return lines + ["wait 5", "quit"]


def tuning_cfg(pose: str | None, commands: list[str]) -> list[str]:
    lines = preamble(pose, commands, 0)
    for label, cvars in (("base", {}), *((slug(name), {**LEAF, **cvars}) for name, cvars in CANDIDATES),
                         ("scatter", {**LEAF, "r_volumetricDebug": "1"}), ("base_again", {})):
        lines.extend([*settings(cvars), "wait 30", f"echo VOLUMETRIC_CONFIG {label}", "rendererVolumetricStatus",
                      f"screenshot screenshots/{label}.tga", "wait 3"])
    return lines + ["wait 5", "quit"]


def parse_samples(log_text: str, backend: str) -> dict:
    """Per config: the status of its last frame and every timing sample."""
    label = "Vulkan" if backend == "vk" else "OpenGL"
    configs: dict = {}
    current = None
    for raw in log_text.splitlines():
        line = raw.strip()
        match = re.search(r"VOLUMETRIC_CONFIG (\S+)", line)
        if match:
            current = configs.setdefault(match.group(1), {"samples": [], "status": {}})
            continue
        if current is None:
            continue
        if line.startswith("Temporal presentation: dynamicRequested="):
            values = dict(re.findall(r"(\w+)=([^\s{}]+)", line))
            if values.get("timingValid") == "1":
                current["samples"].append(int(values.get("timing", "0us").rstrip("us") or 0))
            current["scene"] = values.get("scene")
        elif line.startswith(f"Volumetric lighting {label}:"):
            current["status"] = dict(re.findall(r"(\w+)=(\S+)", line))
    return configs


def judge_cost(result: dict, log_text: str, backend: str) -> None:
    problems = result["problems"]
    configs = parse_samples(log_text, backend)
    medians = {name: statistics.median(config["samples"]) for name, config in configs.items() if config["samples"]}
    off = [medians[name] for name in ("off_0", "off_1", "off_2") if name in medians]
    if len(off) < 3:
        problems.append("the leaf-off frame was not timed three times")
        result["metrics"] = {}
        return
    baseline = statistics.mean(off)
    rows = []
    for name, _ in TIERS:
        key = slug(name)
        config = configs.get(key, {})
        status = config.get("status", {})
        if key not in medians:
            problems.append(f"{name}: no valid GPU timing sample")
            continue
        if status.get("effective") != "1":
            problems.append(f"{name}: not effective ({status.get('decline', 'no status')})")
        rows.append({"tier": name, "frameUs": medians[key], "costUs": medians[key] - baseline,
                     "spreadUs": max(config["samples"]) - min(config["samples"]),
                     "target": status.get("target"), "lights": status.get("lights"),
                     "passes": status.get("passes"), "shadowMapped": status.get("shadowMapped"),
                     "skippedBudget": status.get("skippedBudget")})
    result["metrics"] = {"scene": configs.get("off_0", {}).get("scene"), "offUs": off, "offMeanUs": baseline,
                         # how far the leaf-off frame moved while nothing changed: the noise floor
                         "offDriftUs": max(off) - min(off), "tiers": rows}


def judge_tuning(result: dict, log_text: str, backend: str) -> None:
    problems = result["problems"]
    shots = Path(result["shots"])
    images = {}
    for label in ("base", "scatter", "base_again", *(slug(name) for name, _ in CANDIDATES)):
        path = shots / f"{label}.tga"
        if not path.is_file():
            problems.append(f"{label}: capture missing")
            continue
        images[label] = read_tga(path).astype(np.float32)
    if problems:
        result["metrics"] = {}
        return
    base = luma(images["base"])
    rows = []
    for name, _ in CANDIDATES:
        delta = luma(images[slug(name)]) - base
        rows.append({"candidate": name, "meanAdded": float(delta.mean()), "p50": float(np.percentile(delta, 50)),
                     "p99": float(np.percentile(delta, 99)), "over8": float(np.mean(delta > 8.0)),
                     "over32": float(np.mean(delta > 32.0)),
                     "clipped": float(np.mean(luma(images[slug(name)]) >= 254.0))})
    result["metrics"] = {"candidates": rows, "scatterMean": float(luma(images["scatter"]).mean()),
                         "baseAgainIdentical": bool(np.array_equal(images["base_again"], images["base"]))}
    if not result["metrics"]["baseAgainIdentical"]:
        problems.append("the leaf-off frame differs after the candidates ran")
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    labels = ["base", *(slug(name) for name, _ in CANDIDATES), "scatter"]
    titles = ["leaf off", *(name for name, _ in CANDIDATES), "scattered light alone (default)"]
    columns, width, height = 3, 640, 360
    sheet = Image.new("RGB", (columns * width, ((len(labels) + columns - 1) // columns) * height))
    for index, (label, title) in enumerate(zip(labels, titles)):
        tile = Image.fromarray(images[label].astype(np.uint8)).resize((width, height), Image.LANCZOS)
        draw = ImageDraw.Draw(tile)
        draw.rectangle((0, 0, 8 + 7 * len(title), 16), fill=(0, 0, 0))
        draw.text((4, 3), title, fill=(255, 255, 0))
        sheet.paste(tile, ((index % columns) * width, (index // columns) * height))
    sheet_path = shots.parent.parent / "tuning.png"
    sheet.save(sheet_path)
    result["sheet"] = str(sheet_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=motion.DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--mode", choices=("cost", "tuning"), required=True)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--scenes", default="outdoor,corridor,flashlight")
    parser.add_argument("--size", default="2560x1440", help="cost mode window size")
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--machine-state", default="shared machine, hidden window",
                        help="what else the machine was doing, recorded with the cost numbers")
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra startup cvar, for diagnosis")
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    output = (args.output_dir / args.mode).resolve()
    output.mkdir(parents=True, exist_ok=True)
    startup = dict(item.split("=", 1) for item in args.set)
    if args.mode == "cost":
        width, height = args.size.split("x")
        startup.update({"r_customWidth": width, "r_customHeight": height, "r_windowWidth": width,
                        "r_windowHeight": height})
    report = {"runtime": str(runtime), "mode": args.mode, "machineState": args.machine_state, "runs": []}
    lines = [f"# Volumetric lighting {args.mode} (REN-09)", ""]
    if args.mode == "cost":
        lines += [f"Machine state: {args.machine_state}. Whole-frame GPU time at {args.size}, median of {SAMPLES} "
                  "samples; cost is over the mean of three leaf-off medians.", "",
                  "| Backend | Scene | Tier | Frame ms | Cost ms | Sample spread ms | Target | Lights | Passes |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    else:
        lines += ["Added brightness in 8-bit levels over the leaf-off frame.", "",
                  "| Backend | Scene | Candidate | Mean | Median | p99 | Over 8 | Over 32 | Clipped |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    failed = False
    for backend in [name for name in args.backends.split(",") if name]:
        for scene in [name for name in args.scenes.split(",") if name]:
            map_name, pose, commands = SCENES[scene]
            cfg = cost_cfg(pose, commands) if args.mode == "cost" else tuning_cfg(pose, commands)
            result = motion.launch(runtime, output, backend, scene, map_name, cfg, startup, {}, args.timeout)
            log_path = Path(result["log"])
            log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
            result["shots"] = str(result["shots"])
            result.pop("configs", None)
            (judge_cost if args.mode == "cost" else judge_tuning)(result, log_text, backend)
            result["pass"] = not result["problems"]
            failed |= not result["pass"]
            report["runs"].append(result)
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend} {scene}: "
                  + ("; ".join(result["problems"]) or "ok"), flush=True)
            metrics = result.get("metrics", {})
            if args.mode == "cost" and metrics:
                lines.append(f"| {backend} | {scene} | leaf off | {metrics['offMeanUs'] / 1000:.2f} | - | "
                             f"{metrics['offDriftUs'] / 1000:.2f} (drift) | {metrics['scene']} | - | - |")
                for row in metrics["tiers"]:
                    lines.append(f"| {backend} | {scene} | {row['tier']} | {row['frameUs'] / 1000:.2f} | "
                                 f"{row['costUs'] / 1000:+.2f} | {row['spreadUs'] / 1000:.2f} | {row['target']} | "
                                 f"{row['lights']} | {row['passes']} |")
            elif metrics:
                for row in metrics["candidates"]:
                    lines.append(f"| {backend} | {scene} | {row['candidate']} | {row['meanAdded']:.2f} | "
                                 f"{row['p50']:.2f} | {row['p99']:.2f} | {100 * row['over8']:.1f}% | "
                                 f"{100 * row['over32']:.1f}% | {100 * row['clipped']:.2f}% |")
            for line in lines[-(len(TIERS) + 1 if args.mode == "cost" else len(CANDIDATES)):]:
                print("  " + line, flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
