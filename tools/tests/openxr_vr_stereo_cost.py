#!/usr/bin/env python3
"""Measure what stereo costs a frame (VR-04), against the openQ4 OpenXR test
runtime (requires a staged client built with OpenXR and retail assets).

One single-player process holds a fixed view of airdefense1 with game time
stopped (g_stopTime 1), so every sampled frame draws the same world. It records
the renderer's per-frame timing trace (rendererBenchmarkCapture, 256 frames)
three times: flat on the desktop, in stereo, and in stereo again after a
vr_restart, so a cold session does not pass for the steady cost. The test
runtime runs unpaced (OPENQ4_XR_TEST_RATE 0) and the window does not wait for
vsync, and the trace's CPU and GPU times exclude the frame limiter, so they
measure work rather than waiting. It reports CPU frame,
front-end, back-end and GPU percentiles for each and the stereo/flat ratios,
with the views and draws of the last frame of each.

This is the desktop GPU's cost, not a headset's: a real compositor adds its own
pacing and reprojection, and the eyes here are the size the runtime is told to
recommend (--eye-size). It establishes where two eyes spend their time; the
headset budgets come from the refresh rate (11.1 ms at 90 Hz, 13.9 ms at 72 Hz).
--system-runtime measures on the installed OpenXR runtime and a real headset
instead (VR-02's qualification checklist): its eyes are the headset's own and
its frames are paced by the headset's compositor, so the CPU and GPU work, not
the frame interval, is the comparable figure. --preset applies a performance
preset first (quality and ultra render shadow maps), keeping the harness's
unpaced frame settings, and the report goes to its own profile directory.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
# airdefense1, near the start, looking over the battlefield (x y z yaw)
POSE = "10310.89 -6950.34 6.96 150.7"
PHASES = ("flat", "stereo", "stereo_warm")


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def summarise(path: Path) -> dict:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    # the oldest samples can predate the phase: keep the last 200
    rows = rows[-200:]
    cpu = [int(r["cpuUs"]) / 1000.0 for r in rows]
    front = [int(r["frontEndMs"]) for r in rows]
    back = [int(r["backendMs"]) for r in rows]
    gpu = [int(r["gpuUs"]) / 1000.0 for r in rows if r["gpuValid"] == "1" and int(r["gpuUs"]) > 0]
    # the frame's build (game draw and renderer front end) and submission, in microseconds
    build = [int(r["frameBuildUs"]) / 1000.0 for r in rows if int(r["frameBuildUs"]) > 0]
    submit = [int(r["frameSubmitUs"]) / 1000.0 for r in rows if int(r["frameSubmitUs"]) > 0]
    result = {"samples": len(rows)}
    for name, values in (("cpu_ms", cpu), ("build_ms", build), ("submit_ms", submit), ("front_end_ms", front),
                         ("back_end_ms", back), ("gpu_ms", gpu)):
        result[name] = {"p50": percentile(values, 0.5), "p95": percentile(values, 0.95), "p99": percentile(values, 0.99),
                        "mean": statistics.fmean(values) if values else float("nan"), "count": len(values)}
    return result


def work(text: str, tag: str) -> dict:
    tail = text.partition(tag)[2]
    found = re.search(r"rendererBenchmark capture\(.*?work\(upload=(\d+)KB draws=(\d+) surf=(\d+) verts=(\d+) idx=(\d+) "
                      r"ents=(\d+) lights=(\d+)", tail)
    if not found:
        return {}
    keys = ("upload_kb", "draws", "surfaces", "vertexes", "indexes", "entities", "lights")
    return dict(zip(keys, (int(v) for v in found.groups())))


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=400)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--eye-size", default="1440x1600", help="the per-eye size the runtime recommends")
    parser.add_argument("--window", default="1920x1080", help="the flat window size")
    parser.add_argument("--extra", action="append", default=[], help="another cvar=value for both phases (A/B tests)")
    parser.add_argument("--preset", help="applyPerformancePreset first (for example quality), keeping unpaced frames")
    parser.add_argument("--system-runtime", action="store_true",
                        help="use the installed OpenXR runtime and headset, not the test runtime")
    options = parser.parse_args(argv)

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not options.system_runtime and not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-stereo-cost").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / (f"{options.render_api}-{options.preset}" if options.preset else options.render_api)
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for phase in PHASES:
        (game / f"stereo_cost_{phase}.csv").unlink(missing_ok=True)
    (game / "logs/openq4.log").unlink(missing_ok=True)
    width, height = options.window.split("x")

    # 256 frames fill the trace; 400 more than that keeps the last phase's frames only
    settle = "wait 400"
    vulkan_restart = 6000 if options.render_api == "vulkan" else 2500
    # a preset sets the frame cap and vsync its own way; measure unpaced again
    unpaced = []
    if options.preset:
        unpaced = ["com_maxfps 0", "r_swapInterval 0"]
        unpaced += [" ".join(item.partition("=")[::2]) for item in options.extra]
    commands = [
        *unpaced, "waitMsec 2500", "god", "notarget", f"setviewpos {POSE}", "g_stopTime 1", "waitMsec 1500",
        "r_rendererMetrics 1", settle, "echo VR_COST_FLAT", "rendererBenchmarkCapture stereo_cost_flat.csv",
        "vr_enable 1", "vr_restart", f"waitMsec {vulkan_restart}", "vr_status", settle,
        "echo VR_COST_STEREO", "rendererBenchmarkCapture stereo_cost_stereo.csv",
        "vr_restart", f"waitMsec {vulkan_restart}", settle, settle,
        "echo VR_COST_STEREO_WARM", "rendererBenchmarkCapture stereo_cost_stereo_warm.csv", "vr_status",
        "r_rendererMetrics 0", "echo VR_COST_COMPLETE", "quit",
    ]
    (game / "openxr_vr_stereo_cost.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        # hidden: the flat phase is then capped at 60 frames a second, which
        # the trace's per-frame work excludes; stereo frames are paced only by
        # the unpaced runtime's xrWaitFrame
        "r_hiddenWindow": "1", "r_windowWidth": width, "r_windowHeight": height,
        "r_mode": "-1", "r_customWidth": width, "r_customHeight": height,
        "r_swapInterval": "0", "com_maxfps": "0", "r_vkValidation": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_stereo_cost.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1", "s_noSound": "1",
        "vr_enable": "0", "vr_debug": "0", "vr_aimMode": "1", "vr_roomScale": "0", "vr_mirror": "1",
        "r_renderApi": options.render_api,
    }
    for item in options.extra:
        key, _, value = item.partition("=")
        settings[key] = value
    for key, value in settings.items():
        args += ["+set", key, value]
    if options.preset:
        args += ["+applyPerformancePreset", options.preset]
    args += ["+map", "game/airdefense1"]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    environment = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_")}
    if not options.system_runtime:
        environment.update({"XR_RUNTIME_JSON": str(manifest), "OPENQ4_XR_TEST_EYE_SIZE": options.eye_size,
                            "OPENQ4_XR_TEST_RATE": "0", "OPENQ4_XR_TEST_LOG": str(profile / "xr.jsonl")})
    with (profile / "process.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT, env=environment,
                                timeout=options.timeout)
    text = (game / "logs/openq4.log").read_text(encoding="utf-8", errors="replace")
    assert result.returncode == 0 and "VR_COST_COMPLETE" in text, "the measurement did not complete"
    assert "OpenXR: presenting to the headset" in text, "the stereo phases never presented"

    eye_size = re.search(r"OpenXR: session created, eyes (\d+)x(\d+)", text)
    report = {"render_api": options.render_api, "runtime": "system" if options.system_runtime else "test",
              "eye_size": f"{eye_size.group(1)}x{eye_size.group(2)}" if options.system_runtime and eye_size else options.eye_size,
              "window": options.window, "extra": options.extra, "preset": options.preset, "phases": {}}
    for phase in PHASES:
        report["phases"][phase] = summarise(game / f"stereo_cost_{phase}.csv")
        report["phases"][phase]["work"] = work(text, f"VR_COST_{phase.upper()}")
    flat, warm = report["phases"]["flat"], report["phases"]["stereo_warm"]
    report["stereo_over_flat"] = {key: round(warm[key]["p50"] / flat[key]["p50"], 2) if flat[key]["p50"] else None
                                  for key in ("cpu_ms", "build_ms", "submit_ms", "gpu_ms")}
    (profile / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for phase in PHASES:
        p = report["phases"][phase]
        print(f"{options.render_api} {phase:12s} cpu p50 {p['cpu_ms']['p50']:.2f} p95 {p['cpu_ms']['p95']:.2f} p99 {p['cpu_ms']['p99']:.2f} | "
              f"build p50 {p['build_ms']['p50']:.2f} | submit p50 {p['submit_ms']['p50']:.2f} | "
              f"gpu p50 {p['gpu_ms']['p50']:.2f} p95 {p['gpu_ms']['p95']:.2f} | work {p['work']}")
    print(f"stereo/flat p50: {report['stereo_over_flat']}; report={profile / 'report.json'}")


if __name__ == "__main__":
    main()
