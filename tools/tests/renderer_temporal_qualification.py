#!/usr/bin/env python3
"""REN-04 temporal AA and dynamic-resolution qualification on stock assets.

Opt-in, local and hardware-bound. Launches the staged client from .install
into game/airdefense1 (or the Milestone D mirror fixture), pans the camera
with benchmarkViewSweep (game time, no synthesized input) and reads the
cumulative counters of rendererTemporalPresentationStatus twice: once before
and once after a disruptive event - a screenshot, a teleport cut, vid_restart,
a pause, withheld/lost Vulkan presents, a forced device loss, or forced and
stale dynamic-resolution timing.

Each case requires temporal AA to keep resolving with history and complete
per-pixel ownership in steady state, never to fall back silently, to record
the expected history decision for the event, and to return to history-backed
resolves afterwards. A JSON report and a Markdown summary go to --output-dir.
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
DEFAULT_OUTPUT = ROOT / ".tmp" / "temporal-qualification"
MAP = "game/airdefense1"
MIRROR_MAP = "maps/tools/milestone_d_nested_dynamic"
CUT_POSE = "9400 -7450 220 8 175 0"	# renderer_shadow_mapping_outdoor.py "tank"

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
    re.compile(r"Unknown command 'renderer"),
)


@dataclass
class Case:
    name: str
    description: str
    cvars: dict[str, str] = field(default_factory=dict)
    launch: dict[str, str] = field(default_factory=dict)
    setup: list[str] = field(default_factory=list)
    events: list[str] = field(default_factory=list)
    frames_before: int = 150
    frames_after: int = 240
    backends: tuple[str, ...] = ("gl", "vk")
    mirror: bool = False
    # Wait one frame at a time after the event. Vulkan presentation recovery
    # inserts vid_restart at the head of the command buffer, but a pending
    # "wait N" holds the whole buffer until it expires.
    fine_waits: bool = False
    # expectations
    min_history_ratio: float = 0.9		# history resolves per frame, both windows
    min_recovery_ratio: float = 0.8		# history resolves per frame after the event
    min_complete_ratio: float = 0.9		# complete vector passes per history resolve
    allowed_fallbacks: tuple[str, ...] = ()
    required_view_resets: tuple[str, ...] = ()
    required_invalidations: tuple[str, ...] = ()
    require_dynamic: bool = False
    require_resolution: tuple[str, ...] = ()	# drops/raises/rejected/resets > 0
    require_final_scale: int | None = None	# effectiveScale at the end, percent


BASE = {"r_temporalAA": "1", "r_motionBlur": "0"}
DYNAMIC = {"r_temporalAA": "2", "r_rendererDynamicResolution": "1"}

CASES: list[Case] = [
    Case("native", "temporal AA at native resolution"),
    Case("taau-75", "automatic temporal upscaling from 75%",
         cvars={"r_temporalAA": "2", "r_screenFraction": "75", "r_resolutionScaleMode": "1"}),
    Case("dynamic", "dynamic resolution with automatic temporal upscaling",
         cvars=DYNAMIC, require_dynamic=True),
    # A target change resets the controller to its safe ceiling, so recovery
    # may arrive as a reset rather than as raise steps.
    Case("scale-recovery", "an unreachable GPU target drops the scale, a generous one recovers it",
         cvars={**DYNAMIC, "r_dynamicResolutionTargetMsec": "0.5"},
         events=["set r_dynamicResolutionTargetMsec 1000"], frames_after=600,
         require_dynamic=True, require_resolution=("drops",), require_final_scale=100),
    Case("stale-timing", "every delayed GPU sample is too old: the controller returns to its safe ceiling",
         cvars={**DYNAMIC, "r_dynamicResolutionMaxSampleAge": "1",
                "r_dynamicResolutionMissingSampleReset": "30"},
         require_dynamic=True, require_resolution=("rejected", "resets")),
    Case("pause", "g_stopTime: equal simulation times keep the history",
         events=["g_stopTime 1", "wait 120", "g_stopTime 0"]),
    Case("capture", "a screenshot bypasses history, which resumes afterwards",
         events=["screenshot screenshots/temporal.tga"], required_view_resets=("capture",)),
    Case("cut", "a teleport is a camera cut", events=[f"setviewpos {CUT_POSE}"],
         required_view_resets=("camera-translation",)),
    Case("vid-restart", "vid_restart discards history and temporal targets", events=["vid_restart"],
         frames_after=360, required_invalidations=("video restart",)),
    Case("rollback", "switching temporal AA off falls back to SMAA/spatial at once, and back on reseeds",
         cvars={"r_postAA": "1"}, events=["set r_temporalAA 0", "wait 120", "set r_temporalAA 1"],
         required_invalidations=("temporal AA disabled", "temporal AA enabled")),
    Case("msaa-4", "4x MSAA scene", launch={"r_multiSamples": "4"}),
    Case("render-thread-2", "Vulkan full front-end overlap", launch={"r_vkRenderThread": "2"},
         backends=("vk",)),
    Case("present-withheld", "every swapchain image withheld for 10 frames, as a display that is off",
         events=["set r_vkPresentationFailureTest 3"] + ["wait 1"] * 10 + ["set r_vkPresentationFailureTest 0"],
         backends=("vk",), allowed_fallbacks=("frame unavailable",), fine_waits=True),
    Case("surface-lost", "the presentation surface is reported lost and recreated",
         events=["set r_vkPresentationFailureTest 2"], frames_after=360, backends=("vk",),
         allowed_fallbacks=("frame unavailable",), fine_waits=True),
    Case("device-lost", "a lost device restarts the renderer", events=["set r_vkPresentationFailureTest 1"],
         frames_after=480, backends=("vk",), allowed_fallbacks=("frame unavailable",), fine_waits=True),
    Case("mirror", "temporal AA with the mirror fixture's subview", mirror=True,
         setup=["noclip", "setviewpos 0 -384 96 0 90 0"]),
]


def parse_values(body: str) -> dict:
    return dict(re.findall(r"(\w[\w-]*)=([^\s{}]+)", body))


def parse_reasons(body: str) -> dict:
    return {name: int(count) for name, count in re.findall(r'"([^"]*)"=(\d+)', body)}


def parse_snapshots(log_text: str) -> list[dict]:
    snapshots: list[dict] = []
    current: dict | None = None
    for raw in log_text.splitlines():
        line = raw.strip()
        if line.startswith("Temporal presentation: cumulative statistics reset"):
            snapshots.clear()
            current = None
            continue
        if line.startswith("Temporal presentation: dynamicRequested="):
            current = {"presentation": parse_values(line.split(":", 1)[1])}
            snapshots.append(current)
            continue
        if current is None:
            continue
        for prefix, key, parser in (
            ("Temporal ownership:", "ownership", parse_values),
            ("Temporal resolve:", "resolve", parse_values),
            ("Temporal view history:", "views", parse_values),
            ("Temporal resolve fallbacks:", "fallbacks", parse_reasons),
            ("Temporal history invalidations:", "invalidations", parse_reasons),
            ("Vulkan temporal motion:", "vulkan", parse_values),
        ):
            if line.startswith(prefix):
                current[key] = parser(line.split(":", 1)[1])
    return snapshots


def count(snapshot: dict, section: str, key: str) -> int:
    try:
        return int(snapshot.get(section, {}).get(key, 0))
    except ValueError:
        return 0


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
    lines += ["wait 120", "rendererTemporalPresentationStatus reset", "benchmarkViewSweep 360 9000",
              f"wait {case.frames_before}", "rendererTemporalPresentationStatus"]
    lines += case.events
    lines += ["wait 1"] * case.frames_after if case.fine_waits else [f"wait {case.frames_after}"]
    lines += ["rendererTemporalPresentationStatus", "wait 5", "quit"]
    (game_dir / "temporal.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")

    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name: str, value) -> None:
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    setv("logFile", 2)
    setv("logFileName", "logs/temporal.log")
    setv("developer", 1)
    setv("fs_savepath", run_dir)
    setv("fs_devpath", run_dir)
    setv("fs_game", "baseoq4")
    setv("com_skipLoadingContinue", 1)
    setv("com_loadingContinueAutoAdvance", 1)
    setv("g_autoSkipCinematics", 1)
    setv("com_maxfps", 0)
    setv("r_swapInterval", 0)
    setv("r_fullscreen", 0)
    setv("ui_retained", 1)
    setv("ui_autoJoin", 0)
    setv("r_renderApi", "vulkan" if backend == "vk" else "gl")
    setv("g_autoExecAfterMapLoad", "temporal.cfg")
    setv("g_autoExecAfterMapLoadDelayMs", 500)
    for name, value in case.launch.items():
        setv(name, value)
    command += ["+devmap", MIRROR_MAP if case.mirror else MAP]

    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "temporal.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    return {
        "backend": backend, "case": case.name, "description": case.description,
        "exitCode": exit_code, "seconds": round(time.time() - started, 1), "log": str(log_path),
        "snapshots": parse_snapshots(log_text),
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
    }


def reevaluate_case(output: Path, backend: str, case: Case) -> dict:
    log_path = output / f"{backend}-{case.name}" / "baseoq4" / "logs" / "temporal.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    return {
        "backend": backend, "case": case.name, "description": case.description,
        "exitCode": 0 if log_text else "missing log", "seconds": 0, "log": str(log_path),
        "snapshots": parse_snapshots(log_text),
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
    }


def evaluate(case: Case, result: dict) -> list[str]:
    problems: list[str] = []
    if result["exitCode"] != 0:
        problems.append(f"exit code {result['exitCode']}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    snapshots = result["snapshots"]
    if len(snapshots) < 2:
        problems.append(f"expected two status snapshots, found {len(snapshots)}")
        return problems
    before, after = snapshots[0], snapshots[-1]
    for snapshot in (before, after):
        for key in ("resolve", "views", "fallbacks", "invalidations"):
            if key not in snapshot:
                problems.append(f"missing '{key}' status line")
    if problems:
        return problems

    history_before = count(before, "resolve", "history")
    history_after = count(after, "resolve", "history") - history_before
    if history_before < case.min_history_ratio * case.frames_before:
        problems.append(f"{history_before} history resolves in the first {case.frames_before} frames")
    if history_after < case.min_recovery_ratio * case.frames_after:
        problems.append(f"{history_after} history resolves in the {case.frames_after} frames after the event")
    total_history = count(after, "resolve", "history")
    complete = count(after, "resolve", "velocityComplete")
    if total_history > 0 and complete < case.min_complete_ratio * total_history:
        problems.append(f"complete ownership on {complete} of {total_history} history resolves")

    unexpected = {reason: n for reason, n in after["fallbacks"].items()
                  if not any(allowed in reason for allowed in case.allowed_fallbacks)}
    if unexpected:
        problems.append(f"unexpected fallbacks {unexpected}")
    for reason in case.required_view_resets:
        if count(after, "views", reason) - count(before, "views", reason) <= 0:
            problems.append(f"no '{reason}' view history decision after the event")
    for reason in case.required_invalidations:
        gained = after["invalidations"].get(reason, 0) - before["invalidations"].get(reason, 0)
        if gained <= 0:
            problems.append(f"no '{reason}' history invalidation after the event")
    presentation = after.get("presentation", {})
    if presentation.get("taaRequested") != "1":
        problems.append("temporal AA was not requested at the end")
    if case.require_dynamic and presentation.get("dynamicActive") != "1":
        problems.append("dynamic resolution was not active at the end")
    for key in case.require_resolution:
        if count(after, "presentation", key) <= 0:
            problems.append(f"dynamic-resolution controller recorded no '{key}'")
    if "drops" in case.require_resolution and before.get("presentation", {}).get("effectiveScale") == "100%":
        problems.append("the unreachable target never lowered the scale before the event")
    if case.require_final_scale is not None:
        final = presentation.get("effectiveScale", "?")
        if final != f"{case.require_final_scale}%":
            problems.append(f"effective scale {final} at the end, expected {case.require_final_scale}%")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--cases", default="", help="comma-separated case names (default: all)")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--mirror-runtime", type=Path, default=None,
                        help="runtime staged by renderer_milestone_d_fixture.py; enables the mirror case")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--reevaluate", action="store_true",
                        help="score the logs already in --output-dir again without launching")
    args = parser.parse_args()
    if args.list:
        for case in CASES:
            print(f"{case.name:18} {','.join(case.backends):6} {case.description}")
        return 0
    wanted = {name for name in args.cases.split(",") if name}
    backends = [name for name in args.backends.split(",") if name]
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report: dict = {"map": MAP, "mirrorMap": MIRROR_MAP, "runtime": str(runtime), "cases": []}
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
                    print(f"SKIP {backend:2} {case.name:18} needs --mirror-runtime", flush=True)
                    continue
                case_runtime = args.mirror_runtime.resolve()
            if args.reevaluate:
                result = reevaluate_case(output, backend, case)
            else:
                result = launch_case(case_runtime, output, backend, case, args.timeout)
            result["problems"] = evaluate(case, result)
            result["pass"] = not result["problems"]
            failed |= not result["pass"]
            report["cases"].append(result)
            last = result["snapshots"][-1] if result["snapshots"] else {}
            resolve = last.get("resolve", {})
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend:2} {case.name:18} "
                  f"history={resolve.get('history', '?')} seeded={resolve.get('seeded', '?')} "
                  f"fallback={resolve.get('fallback', '?')} complete={resolve.get('velocityComplete', '?')} "
                  + ("; ".join(result["problems"]) if result["problems"] else ""), flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["# Temporal AA qualification", "", f"Map `{MAP}`, runtime `{runtime}`.", "",
             "| Backend | Case | Result | History | Seeded | Fallback | Complete | Notes |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for result in report["cases"]:
        resolve = (result["snapshots"][-1] if result["snapshots"] else {}).get("resolve", {})
        lines.append(f"| {result['backend']} | {result['case']} | {'pass' if result['pass'] else 'FAIL'} | "
                     f"{resolve.get('history', '?')} | {resolve.get('seeded', '?')} | "
                     f"{resolve.get('fallback', '?')} | {resolve.get('velocityComplete', '?')} | "
                     f"{'; '.join(result['problems'])} |")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
