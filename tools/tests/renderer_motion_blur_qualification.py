#!/usr/bin/env python3
"""REN-08 motion blur qualification on stock Quake 4 assets.

Opt-in, local and hardware-bound: it launches the staged client from
.install into game/airdefense1, pans the camera with benchmarkViewSweep
(game time, no synthesized input), and reads rendererMotionBlurStatus. Each
case changes one dimension - temporal AA, resolution scaling, HDR/bloom/SSAO,
MSAA, frame caps, pause, slow frames, the Vulkan render thread, object
vectors - on OpenGL and Vulkan, and asserts that motion blur keeps drawing
from its own history, that subviews (the portal sky) never reset it, that
skinned and view-weapon surfaces own exact vectors, and that the
shutter-scaled blur input is the same at 60 FPS and uncapped.

Writable state (config, logs, screenshots) is isolated per case under
--output-dir; the executable is reused in place. A JSON report and a Markdown
summary are written to --output-dir.
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
DEFAULT_OUTPUT = ROOT / ".tmp" / "motion-blur-qualification"
MAP = "game/airdefense1"

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b", re.I),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
    re.compile(r"Unknown command 'renderer", re.I),
)


@dataclass
class Case:
    name: str
    description: str
    cvars: dict[str, str] = field(default_factory=dict)
    maxfps: str = "0"
    sweep_degrees: str = "360"
    sweep_msec: int = 6000
    frames: int = 400
    after: list[str] = field(default_factory=list)
    setup: list[str] = field(default_factory=list)
    launch: dict[str, str] = field(default_factory=dict)
    backends: tuple[str, ...] = ("gl", "vk")
    # expectations
    min_drawn_ratio: float = 0.95
    taa_aligned: bool = False
    object_vectors: bool = True
    allowed_resets: tuple[str, ...] = ("first-sample",)
    max_resets: int = 2
    dominant_reset: str | None = None
    screenshot: str | None = None
    # mirror: run on the Milestone D mirror fixture (--mirror-runtime)
    mirror: bool = False
    min_subview_skips: int = 1
    # stock gameplay maps carry skinned actors and a view weapon
    require_actors: bool = True


BASE = {"r_motionBlur": "1", "r_temporalAA": "0"}

CASES: list[Case] = [
    Case("baseline", "motion blur alone with exact object vectors"),
    Case("taa", "temporal AA forced on: blur must cancel the jitter, not reset",
         cvars={"r_temporalAA": "1"}, taa_aligned=True),
    Case("taau-75", "75% bilinear scale with automatic temporal upscaling",
         cvars={"r_temporalAA": "2", "r_screenFraction": "75", "r_resolutionScaleMode": "1"},
         taa_aligned=True),
    Case("post-combo", "HDR tone mapping, bloom, SSAO and temporal AA together",
         cvars={"r_temporalAA": "1", "r_hdrToneMap": "1", "r_bloom": "1", "r_ssao": "1"},
         taa_aligned=True),
    Case("supersample-150", "150% supersampled scene target",
         cvars={"r_screenFraction": "150"}),
    Case("msaa-4", "4x MSAA scene target", launch={"r_multiSamples": "4"}),
    Case("cap-60", "60 FPS presentation cap", maxfps="60", sweep_degrees="30", frames=240),
    Case("cap-uncapped", "uncapped presentation", maxfps="0", sweep_degrees="30", frames=240),
    Case("cap-60-per-frame", "60 FPS with the original per-frame length",
         cvars={"r_motionBlurShutterMsec": "0"}, maxfps="60", sweep_degrees="30", frames=240),
    Case("cap-uncapped-per-frame", "uncapped with the original per-frame length",
         cvars={"r_motionBlurShutterMsec": "0"}, maxfps="0", sweep_degrees="30", frames=240),
    # Resuming can step presentation time back by part of a tic; one frame then
    # restarts the history instead of blurring across it.
    Case("pause", "g_stopTime freezes presentation time mid-pan",
         after=["wait 60", "g_stopTime 1", "wait 120", "g_stopTime 0"], frames=120,
         min_drawn_ratio=0.95, allowed_resets=("first-sample", "time-reversed")),
    Case("slow-frames", "8 FPS: every interval exceeds the 100 ms gap limit",
         maxfps="8", frames=40, min_drawn_ratio=0.0, allowed_resets=("first-sample", "time-gap"),
         max_resets=1000, dominant_reset="time-gap"),
    # Without object vectors a frame in which the camera did not move has
    # nothing to blur and is reported as camera-still.
    Case("camera-only", "object vectors off: camera reprojection only",
         cvars={"r_motionBlurObjectVectors": "0"}, object_vectors=False,
         allowed_resets=("first-sample", "camera-still")),
    Case("render-thread-1", "Vulkan backend overlap (r_vkRenderThread 1)",
         launch={"r_vkRenderThread": "1"}, backends=("vk",)),
    Case("render-thread-2", "Vulkan full front-end overlap (r_vkRenderThread 2)",
         launch={"r_vkRenderThread": "2"}, backends=("vk",)),
    Case("dynamic-resolution", "dynamic resolution with automatic temporal upscaling",
         cvars={"r_temporalAA": "2", "r_rendererDynamicResolution": "1"}, taa_aligned=True),
    Case("mirror", "the mirror fixture's subview renders before every main view",
         mirror=True, sweep_degrees="20", sweep_msec=8000, frames=300, require_actors=False,
         setup=["noclip", "setviewpos 0 -384 96 0 90 0"],
         allowed_resets=("first-sample", "camera-still"), max_resets=40),
    Case("weapon-exact", "debug vectors with exact weapon ownership",
         cvars={"r_motionBlurDebug": "1"}, after=["wait 90", "screenshot screenshots/weapon.tga"],
         frames=60, screenshot="weapon.tga"),
    Case("weapon-camera", "debug vectors with camera-only reprojection",
         cvars={"r_motionBlurDebug": "1", "r_motionBlurObjectVectors": "0"},
         after=["wait 90", "screenshot screenshots/weapon.tga"], frames=60, object_vectors=False,
         screenshot="weapon.tga", allowed_resets=("first-sample", "camera-still")),
]


def parse_status(log_text: str) -> dict:
    status: dict = {}
    for line in log_text.splitlines():
        line = line.strip()
        for prefix, key in (
            ("Motion blur: requested=", "summary"),
            ("Motion blur history:", "history"),
            ("Motion blur ownership:", "ownership"),
            ("Motion blur effective:", "effective"),
            ("Motion blur camera:", "camera"),
        ):
            if line.startswith(prefix):
                body = line.split(":", 1)[1]
                values = dict(re.findall(r"(\w[\w-]*)=([^\s]+)", body))
                status[key] = values
    return status


def number(values: dict, key: str) -> float:
    try:
        return float(values.get(key, "nan"))
    except ValueError:
        return float("nan")


MIRROR_MAP = "maps/tools/milestone_d_nested_dynamic"


def case_map(case: Case) -> str:
    return MIRROR_MAP if case.mirror else MAP


def launch_case(runtime: Path, output: Path, backend: str, case: Case, timeout: int) -> dict:
    run_dir = output / f"{backend}-{case.name}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    cvars = dict(BASE)
    cvars.update(case.cvars)
    lines = ["god", "notarget"]
    lines += [f"set {name} {value}" for name, value in cvars.items()]
    lines += case.setup
    lines += ["wait 120", "rendererMotionBlurStatus reset",
              f"benchmarkViewSweep {case.sweep_degrees} {case.sweep_msec}"]
    lines += case.after
    lines += [f"wait {case.frames}", "rendererMotionBlurStatus", "wait 5", "quit"]
    (game_dir / "motionblur.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")

    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name: str, value) -> None:
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    setv("logFile", 2)
    setv("logFileName", "logs/motionblur.log")
    setv("developer", 1)
    setv("fs_savepath", run_dir)
    setv("fs_devpath", run_dir)
    setv("fs_game", "baseoq4")
    setv("com_skipLoadingContinue", 1)
    setv("com_loadingContinueAutoAdvance", 1)
    setv("g_autoSkipCinematics", 1)
    setv("com_maxfps", case.maxfps)
    setv("r_swapInterval", 0)
    setv("r_fullscreen", 0)
    setv("ui_retained", 1)
    setv("ui_autoJoin", 0)
    setv("r_renderApi", "vulkan" if backend == "vk" else "gl")
    setv("g_autoExecAfterMapLoad", "motionblur.cfg")
    setv("g_autoExecAfterMapLoadDelayMs", 500)
    for name, value in case.launch.items():
        setv(name, value)
    command += ["+devmap", case_map(case)]

    started = time.time()
    try:
        completed = subprocess.run(command, cwd=runtime, timeout=timeout)
        exit_code = completed.returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "motionblur.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    return {
        "backend": backend,
        "case": case.name,
        "description": case.description,
        "exitCode": exit_code,
        "seconds": round(time.time() - started, 1),
        "log": str(log_path),
        "status": parse_status(log_text),
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
        "screenshot": str(game_dir / "screenshots" / case.screenshot) if case.screenshot else None,
    }


def reevaluate_case(output: Path, backend: str, case: Case) -> dict:
    game_dir = output / f"{backend}-{case.name}" / "baseoq4"
    log_path = game_dir / "logs" / "motionblur.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    return {
        "backend": backend, "case": case.name, "description": case.description,
        "exitCode": 0 if log_text else "missing log", "seconds": 0, "log": str(log_path),
        "status": parse_status(log_text),
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
        "screenshot": str(game_dir / "screenshots" / case.screenshot) if case.screenshot else None,
    }


def evaluate(case: Case, result: dict) -> list[str]:
    problems: list[str] = []
    if result["exitCode"] != 0:
        problems.append(f"exit code {result['exitCode']}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    status = result["status"]
    for key in ("summary", "history", "ownership", "effective", "camera"):
        if key not in status:
            problems.append(f"missing rendererMotionBlurStatus '{key}' line")
    if problems:
        return problems
    summary = status["summary"]
    main_views = number(summary, "mainViews")
    drawn = number(summary, "drawn")
    if main_views < 20:
        problems.append(f"only {main_views:.0f} main views sampled")
    elif drawn < case.min_drawn_ratio * main_views:
        problems.append(f"drew {drawn:.0f} of {main_views:.0f} main views")
    if number(summary, "disabled") > 0:
        problems.append(f"disabled views: {summary.get('lastDisabled')}")
    aligned = number(summary, "jitterAligned")
    if case.taa_aligned and aligned < drawn:
        problems.append(f"jitter aligned on {aligned:.0f} of {drawn:.0f} drawn views")
    if not case.taa_aligned and aligned > 0:
        problems.append(f"{aligned:.0f} views claimed TAA jitter without temporal AA")
    history = {name: number(status["history"], name) for name in status["history"]}
    resets = {name: count for name, count in history.items() if name != "usable" and count > 0}
    unexpected = {name: count for name, count in resets.items() if name not in case.allowed_resets}
    if unexpected:
        problems.append(f"unexpected history resets {unexpected}")
    if sum(resets.values()) > case.max_resets:
        problems.append(f"{sum(resets.values()):.0f} history resets (limit {case.max_resets})")
    if case.dominant_reset and resets.get(case.dominant_reset, 0) < 0.8 * main_views:
        problems.append(f"expected '{case.dominant_reset}' to reject most views, got {resets}")
    # airdefense1's portal sky and the fixture's mirror render as subviews
    # before the main view; they must pass without resetting its history.
    if case.min_drawn_ratio > 0 and number(summary, "subviewSkips") < case.min_subview_skips:
        problems.append(f"subviews skipped {summary.get('subviewSkips')} times (expected >= {case.min_subview_skips})")
    ownership = status["ownership"]
    object_views = number(summary, "objectVectorViews")
    if case.object_vectors and case.min_drawn_ratio > 0:
        if object_views < drawn:
            problems.append(f"object vectors on {object_views:.0f} of {drawn:.0f} drawn views")
        if case.require_actors and number(ownership, "posed") <= 0:
            problems.append("no skinned surface owned an exact vector")
        if case.require_actors and number(ownership, "weapon") + number(ownership, "weaponStill") <= 0:
            problems.append("the view weapon owned no vector")
    if not case.object_vectors and object_views > 0:
        problems.append("object vectors drawn while r_motionBlurObjectVectors is 0")
    return problems


def weapon_motion(path: str | None) -> float | None:
    """Mean debug-view motion magnitude (blue channel) over the view weapon."""
    if not path or not Path(path).is_file():
        return None
    try:
        from PIL import Image
    except ImportError:
        return None
    image = Image.open(path).convert("RGB")
    width, height = image.size
    box = image.crop((int(width * 0.62), int(height * 0.70), int(width * 0.76), int(height * 0.95)))
    pixels = list(box.getdata())
    return sum(b for _, _, b in pixels) / (255.0 * max(1, len(pixels)))


def cross_checks(results: dict[tuple[str, str], dict]) -> list[dict]:
    checks: list[dict] = []
    for backend in ("gl", "vk"):
        def scaled(case: str) -> float | None:
            result = results.get((backend, case))
            if not result or "camera" not in result["status"]:
                return None
            return number(result["status"]["camera"], "scaledCenterPixels")

        capped, uncapped = scaled("cap-60"), scaled("cap-uncapped")
        if capped and uncapped:
            ratio = uncapped / capped
            checks.append({
                "backend": backend, "check": "shutter-coherence",
                "value": round(ratio, 4), "pass": abs(ratio - 1.0) <= 0.10,
                "detail": f"scaled centre speed uncapped/60 FPS = {ratio:.3f} (limit 1 +/- 0.10)",
            })
        legacy_capped, legacy_uncapped = scaled("cap-60-per-frame"), scaled("cap-uncapped-per-frame")
        if legacy_capped and legacy_uncapped:
            ratio = legacy_uncapped / legacy_capped
            checks.append({
                "backend": backend, "check": "per-frame-rollback",
                "value": round(ratio, 4), "pass": ratio < 0.8,
                "detail": f"r_motionBlurShutterMsec 0 keeps the frame-rate-dependent length: ratio {ratio:.3f}",
            })
        exact = weapon_motion((results.get((backend, "weapon-exact")) or {}).get("screenshot"))
        camera = weapon_motion((results.get((backend, "weapon-camera")) or {}).get("screenshot"))
        if exact is not None and camera is not None:
            checks.append({
                "backend": backend, "check": "weapon-not-smeared",
                "value": round(exact, 4), "pass": camera >= 0.3 and exact <= 0.5 * camera,
                "detail": f"mean weapon motion while panning: exact {exact:.3f} vs camera-only {camera:.3f}",
            })
    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--cases", default="", help="comma-separated case names (default: all)")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--mirror-runtime", type=Path, default=None,
                        help="runtime staged by renderer_milestone_d_fixture.py; enables the mirror case")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--reevaluate", action="store_true",
                        help="score the logs already in --output-dir again without launching")
    args = parser.parse_args()

    if args.list:
        for case in CASES:
            print(f"{case.name:24} {','.join(case.backends):6} {case.description}")
        return 0
    wanted = {name for name in args.cases.split(",") if name}
    backends = [name for name in args.backends.split(",") if name]
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    results: dict[tuple[str, str], dict] = {}
    report: dict = {"map": MAP, "mirrorMap": MIRROR_MAP, "runtime": str(runtime), "cases": [], "checks": []}
    failed = False
    for case in CASES:
        if wanted and case.name not in wanted:
            continue
        for backend in backends:
            if backend not in case.backends:
                continue
            case_runtime = runtime
            if case.mirror:
                if args.mirror_runtime is None:
                    print(f"SKIP {backend:2} {case.name:24} needs --mirror-runtime", flush=True)
                    continue
                case_runtime = args.mirror_runtime.resolve()
            if args.reevaluate:
                result = reevaluate_case(output, backend, case)
            else:
                result = launch_case(case_runtime, output, backend, case, args.timeout)
            result["problems"] = evaluate(case, result)
            result["pass"] = not result["problems"]
            failed |= not result["pass"]
            results[(backend, case.name)] = result
            report["cases"].append(result)
            summary = result["status"].get("summary", {})
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend:2} {case.name:24} "
                  f"drawn={summary.get('drawn', '?')}/{summary.get('mainViews', '?')} "
                  f"scaled={result['status'].get('camera', {}).get('scaledCenterPixels', '?')} "
                  + ("; ".join(result["problems"]) if result["problems"] else ""), flush=True)
    report["checks"] = cross_checks(results)
    for check in report["checks"]:
        failed |= not check["pass"]
        print(f"{'PASS' if check['pass'] else 'FAIL'} {check['backend']:2} {check['check']:24} {check['detail']}")

    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["# Motion blur qualification", "", f"Map `{MAP}`, runtime `{runtime}`.", "",
             "| Backend | Case | Result | Drawn / main views | Scaled centre px | Notes |",
             "| --- | --- | --- | --- | --- | --- |"]
    for result in report["cases"]:
        summary = result["status"].get("summary", {})
        lines.append(
            f"| {result['backend']} | {result['case']} | {'pass' if result['pass'] else 'FAIL'} | "
            f"{summary.get('drawn', '?')} / {summary.get('mainViews', '?')} | "
            f"{result['status'].get('camera', {}).get('scaledCenterPixels', '?')} | "
            f"{'; '.join(result['problems'])} |")
    lines += ["", "| Backend | Check | Result | Detail |", "| --- | --- | --- | --- |"]
    for check in report["checks"]:
        lines.append(f"| {check['backend']} | {check['check']} | {'pass' if check['pass'] else 'FAIL'} | {check['detail']} |")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
