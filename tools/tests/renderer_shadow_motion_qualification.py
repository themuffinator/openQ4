#!/usr/bin/env python3
"""REN-03 continuous-motion shadow-map qualification on stock assets.

Opt-in, local and hardware-bound. Each case launches the staged client into
game/airdefense1 with shadow maps on, tours the matched outdoor poses of
renderer_shadow_mapping_outdoor.py while panning continuously with
benchmarkViewSweep (game time, no synthesized input), and reads the periodic
r_shadowMapReport summaries: OpenGL's modern shadow plan and ARB2 cache
estimate, Vulkan's shadow cache and timing lines.

Requires, on every sampled report: a valid prepared plan; no unshadowed
light; no receiver fallback; at the default budget no budget fallback, while
a constrained atlas may fall back but must keep every light shadowed by the
stencil path; on Vulkan no fallback, no dropped GPU query and bounded,
stable resident shadow memory. Stock-only motion is used: the movers,
cutout and translucent receiver fixtures remain in their own suites.

The `receivers` case adds an image check: at every tour pose it stops the
sweep, freezes game time and captures the frame with the cache the motion left
behind (`warm`), the same frame again (pixels that differ are unfrozen noise),
and the frame with the static cache off so every shadow map renders fresh
(`cold`). Outside the noise, warm and cold must match: a stale, missing or
doubled caster from motion would change receivers between them. A fourth
capture with shadows off is the sensitivity control: the tour must show
visible shadows, so a missing caster could not hide.
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
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "shadow-motion-qualification"
MAP = "game/airdefense1"
# renderer_shadow_mapping_outdoor.py OUTDOOR_VIEWS: terrain and stock casters
TOUR = (
    "9750 -6180 80 8 240 0",
    "10200 -6800 40 0 170 0",
    "9800 -7200 80 8 230 0",
    "9400 -7450 220 8 175 0",
    "9000 -8200 500 20 60 0",
    "8400 -9600 500 18 65 0",
    "10900 -9000 550 12 175 0",
    "8100 -11800 650 15 90 0",
)
# Every setting the cases depend on is set here. The tour's lights are point
# lights, and a host whose preset keeps point lights on stencil (Enhanced does
# on OpenGL) would otherwise map nothing and qualify nothing.
SHADOWS = {"r_shadows": "1", "r_useShadowMap": "1", "r_shadowMapPointLights": "1",
           "r_shadowMapReport": "1",
           "r_shadowMapReportInterval": "15", "r_motionBlur": "0", "r_temporalAA": "0"}

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
)


@dataclass
class Case:
    name: str
    description: str
    cvars: dict[str, str] = field(default_factory=dict)
    backends: tuple[str, ...] = ("gl", "vk")
    constrained: bool = False
    images: bool = False


CASES = [
    Case("default", "default shadow budget through the outdoor tour"),
    Case("csm", "cascaded shadow maps for parallel and projected lights",
         cvars={"r_shadowMapCSM": "1", "r_shadowMapProjectedCSM": "1", "r_shadowMapCascadeCount": "4"}),
    Case("constrained", "one shadow-map update per view forces budget fallback; lights must stay shadowed",
         cvars={"r_shadowMapMaxUpdatesPerView": "1"}, constrained=True),
    Case("receivers", "receivers after motion match a fresh render at every tour pose",
         cvars={"g_showHud": "0", "ui_showGun": "0"}, images=True),
]
# Receiver images: bytes outside the noise mask that may differ between the
# warm and fresh captures (a stale or doubled caster changes far more).
RECEIVER_TOLERANCE = 0.0005
# Sensitivity control: bytes that turning shadows off changes, at least this
# fraction on most tour poses, so the comparison has receivers to judge.
SHADOW_SIGNAL = 0.01
SHADOW_SIGNAL_POSES = 6
SWEEP = "benchmarkViewSweep 1440 24000"


def build_cfg(case: Case) -> str:
    cvars = dict(SHADOWS)
    cvars.update(case.cvars)
    lines = ["god", "notarget", "noclip"] + [f"set {k} {v}" for k, v in cvars.items()]
    lines += ["wait 60", SWEEP]
    for index, pose in enumerate(TOUR):
        lines += [f"setviewpos {pose}", "wait 90"]
        if case.images:
            # The teleport lands on the next game tic and the exposure then
            # settles on the new view. Let both happen before time is frozen:
            # frozen at once, the first capture could still show the turning
            # view, or differ from the second by a drifting exposure.
            lines += ["benchmarkViewSweep 0 0", f"setviewpos {pose}", "wait 60", "g_stopTime 1", "wait 3",
                      f"screenshot screenshots/warm{index}.tga", "wait 3",
                      f"screenshot screenshots/again{index}.tga",
                      "set r_shadowMapStaticCache 0", "wait 10",
                      f"screenshot screenshots/cold{index}.tga",
                      "set r_shadows 0", "wait 10", f"screenshot screenshots/unshadowed{index}.tga",
                      "set r_shadows 1", "set r_shadowMapStaticCache 1", "g_stopTime 0", SWEEP]
    lines += ["wait 10", "quit"]
    return "\n".join(lines) + "\n"


def values(body: str) -> dict:
    return dict(re.findall(r"(\w+)=([^\s()]+)", body))


def parse(log_text: str) -> dict:
    gl_plans, gl_caches, vk_caches, vk_timings = [], [], [], []
    for raw in log_text.splitlines():
        line = raw.strip()
        if line.startswith("modernShadowPlan status="):
            gl_plans.append(values(line.split("modernShadowPlan", 1)[1].split("casters(", 1)[0]))
        elif line.startswith("modernShadowPlan arb2Cache("):
            body = line.split("arb2Cache(", 1)[1]
            lights, _, passes = body.partition("passes")
            gl_caches.append({"lights": values(lights), "passes": values(passes)})
        elif line.startswith("Vulkan shadow cache:"):
            entry = values(line)
            resident = re.search(r"resident=([\d.]+)MB", line)
            entry["residentMB"] = float(resident.group(1)) if resident else None
            slots = re.search(r"slots=(\d+)/(\d+) projected (\d+)/(\d+) point", line)
            entry["slots"] = tuple(int(v) for v in slots.groups()) if slots else None
            vk_caches.append(entry)
        elif line.startswith("Vulkan shadow timings:"):
            vk_timings.append(values(line))
    return {"glPlans": gl_plans, "glCaches": gl_caches, "vkCaches": vk_caches, "vkTimings": vk_timings}


def number(entry: dict, key: str) -> float:
    try:
        return float(entry.get(key, "0"))
    except ValueError:
        return 0.0


def evaluate(case: Case, backend: str, data: dict) -> list[str]:
    problems: list[str] = []
    if backend == "gl":
        if len(data["glPlans"]) < 10:
            return [f"only {len(data['glPlans'])} shadow-plan reports"]
        for plan in data["glPlans"]:
            if plan.get("valid") != "1" or not plan.get("status", "").startswith("prepared"):
                problems.append(f"plan not prepared: {plan.get('status')}")
                break
        passes = [cache["passes"] for cache in data["glCaches"]]
        unshadowed = sum(number(p, "unshadowed") for p in passes)
        receiver = sum(number(p, "receiverFallback") for p in passes)
        budget = sum(number(p, "budgetFallback") for p in passes)
        stencil = sum(number(p, "stencilOnly") for p in passes)
        if unshadowed:
            problems.append(f"{unshadowed:.0f} unshadowed light passes")
        if receiver:
            problems.append(f"{receiver:.0f} receiver fallback passes")
        if not case.constrained and budget:
            problems.append(f"{budget:.0f} budget fallback passes at the default budget")
        if case.constrained and budget + stencil <= 0:
            problems.append("the constrained atlas never fell back; the case did not constrain anything")
        fresh = sum(number(p, "fresh") for p in passes)
        if fresh <= 0:
            problems.append("no fresh shadow map rendered while the camera toured the map")
    else:
        caches = data["vkCaches"]
        if len(caches) < 10:
            return [f"only {len(caches)} Vulkan shadow-cache reports"]
        if any(number(c, "fallback") for c in caches) and not case.constrained:
            problems.append("Vulkan shadow fallback at the default budget")
        # Bounded by slots: resident megabytes follow which lights are cached
        # along the tour (reported, not judged); slot use must stay in capacity.
        slots = [c["slots"] for c in caches if c.get("slots")]
        if not slots:
            problems.append("no slot usage reported")
        for used_projected, cap_projected, used_point, cap_point in slots:
            if used_projected > cap_projected or used_point > cap_point:
                problems.append(f"slots over capacity {used_projected}/{cap_projected} {used_point}/{cap_point}")
                break
        dropped = sum(number(t, "dropped") for t in data["vkTimings"])
        if dropped:
            problems.append(f"{dropped:.0f} dropped shadow GPU queries")
    return problems


def load_rgb(path: Path):
    try:
        from PIL import Image
    except ImportError:
        return None
    if not path.is_file():
        return None
    image = Image.open(path).convert("RGB")
    return image.size, image.tobytes()


def compare_receivers(shots: Path) -> tuple[list[dict], list[str]]:
    rows, problems = [], []
    for index in range(len(TOUR)):
        warm, again, cold, unshadowed = (load_rgb(shots / f"{name}{index}.tga")
                                         for name in ("warm", "again", "cold", "unshadowed"))
        if None in (warm, again, cold, unshadowed) or not (warm[0] == again[0] == cold[0] == unshadowed[0]):
            problems.append(f"pose {index}: missing or mismatched captures (Pillow required)")
            continue
        noise = {i for i, (a, b) in enumerate(zip(warm[1], again[1])) if a != b}
        changed = [abs(a - b) for i, (a, b) in enumerate(zip(warm[1], cold[1])) if a != b and i not in noise]
        fraction = len(changed) / len(warm[1])
        signal = sum(1 for i, (a, b) in enumerate(zip(cold[1], unshadowed[1])) if a != b and i not in noise)
        rows.append({"pose": index, "noiseBytes": len(noise), "changedBytes": len(changed),
                     "changedFraction": round(fraction, 6), "maxAbs": max(changed, default=0),
                     "shadowSignalFraction": round(signal / len(warm[1]), 4)})
        if len(noise) > 0.01 * len(warm[1]):
            problems.append(f"pose {index}: {len(noise)} bytes not frozen between identical captures")
        if fraction > RECEIVER_TOLERANCE:
            problems.append(f"pose {index}: receivers after motion differ from a fresh render "
                            f"({len(changed)} bytes, max {max(changed)})")
    signalled = sum(1 for row in rows if row["shadowSignalFraction"] >= SHADOW_SIGNAL)
    if rows and signalled < SHADOW_SIGNAL_POSES:
        problems.append(f"only {signalled} poses show shadows (control); the image check cannot judge receivers")
    return rows, problems


def run(runtime: Path, output: Path, backend: str, case: Case, timeout: int) -> dict:
    run_dir = output / f"{backend}-{case.name}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    (game_dir / "shadowmotion.cfg").write_text(build_cfg(case), encoding="ascii")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]
    for name, value in (("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1),
                        ("logFile", 2), ("logFileName", "logs/shadowmotion.log"), ("developer", 1),
                        ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
                        ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
                        ("g_autoSkipCinematics", 1), ("com_maxfps", 0), ("r_swapInterval", 0),
                        ("r_fullscreen", 0), ("ui_retained", 1), ("ui_autoJoin", 0),
                        ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
                        ("g_autoExecAfterMapLoad", "shadowmotion.cfg"),
                        ("g_autoExecAfterMapLoadDelayMs", 500)):
        command += ["+set", name, str(value)]
    command += ["+devmap", MAP]
    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "shadowmotion.log"
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    data = parse(text)
    problems = [] if exit_code == 0 else [f"exit code {exit_code}"]
    problems += [f"log: {l.strip()}" for l in text.splitlines() if any(p.search(l) for p in FAILURE_PATTERNS)][:5]
    problems += evaluate(case, backend, data)
    receivers = []
    if case.images:
        receivers, image_problems = compare_receivers(game_dir / "screenshots")
        problems += image_problems
    return {"backend": backend, "case": case.name, "seconds": round(time.time() - started, 1),
            "log": str(log_path), "reports": {k: len(v) for k, v in data.items()},
            "receivers": receivers, "problems": problems, "pass": not problems, "data": data}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--cases", default="")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    wanted = {name for name in args.cases.split(",") if name}
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for case in CASES:
        if wanted and case.name not in wanted:
            continue
        for backend in [b for b in args.backends.split(",") if b]:
            if backend not in case.backends:
                continue
            result = run(args.runtime_dir.resolve(), output, backend, case, args.timeout)
            results.append(result)
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend} {case.name:12} reports={result['reports']} "
                  + "; ".join(result["problems"]), flush=True)
    (output / "report.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return 0 if all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
