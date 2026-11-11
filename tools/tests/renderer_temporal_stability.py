#!/usr/bin/env python3
"""Temporal AA image-stability qualification on stock assets (issue #216).

Opt-in, local and hardware-bound. Launches the staged client into
game/airdefense1 with the camera and game time held still (g_stopTime), lets
temporal AA converge, then reads the resolved history image back on
consecutive frames with `screenshot image` (a readback, not a capture frame,
so history keeps running). No input is synthesized.

A correct resolve cancels the projection jitter: with nothing moving, each
frame's history is the previous one plus a small step toward the same
supersampled image. The gate measures, between consecutive frames,

- the global sub-pixel translation (least-squares optical flow), which was the
  whole per-frame jitter before the fix, and
- how much of the image changes at all,

and, across the jitter cycle, how far the history sits from the mean of the
cycle (the residual ripple of the accumulation). A JSON report and a Markdown
summary go to --output-dir.
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

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "temporal-stability"
MAP = "game/airdefense1"
MP_MAP = "mp/q4dm1"
POSE = "10324 -6964 420 12 0 0"		# renderer_shadow_mapping_outdoor.py skyline view
# game-owned (single player), OpenGL backend-owned, Vulkan backend-owned
HISTORY_IMAGES = ("_temporalHistoryAlbedo", "_backendTemporalHistory", "_vkTemporalDirectHistory")
FRAMES = 24							# three jitter cycles

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
    launch: dict[str, str] = field(default_factory=dict)		# needs the value at startup
    backends: tuple[str, ...] = ("gl", "vk")
    multiplayer: bool = False
    # judge the median grid cell only: the scene cannot be held still
    moving_scene: bool = False
    # limits; the unfixed resolve measured 0.99-1.78 px, 12-13% and 0.8
    max_shift: float = 0.06			# pixels, any consecutive pair
    max_changed: float = 0.005		# share of pixels moving more than 4/255 between frames
    max_ripple: float = 0.15		# mean absolute distance from the cycle mean, 8-bit levels


# Native resolution integrates raw jittered samples, so each frame still moves
# the image by its share of one sample (about 0.03 px). An upscaled frame is
# sampled recentered and has no such term.
CASES: list[Case] = [
    Case("native", "temporal AA at native resolution", {"r_temporalAA": "1"}),
    Case("taau-75", "automatic temporal upscaling from 75%",
         {"r_temporalAA": "2", "r_screenFraction": "75", "r_resolutionScaleMode": "1"}, max_shift=0.02),
    Case("taau-50", "automatic temporal upscaling from 50%",
         {"r_temporalAA": "2", "r_screenFraction": "50", "r_resolutionScaleMode": "1"}, max_shift=0.02),
    Case("native-msaa4", "temporal AA over a 4x MSAA scene", {"r_temporalAA": "1"},
         launch={"r_multiSamples": "4"}),
    Case("taau-67-sharpen", "temporal upscaling from 67% under the sharpened scale mode",
         {"r_temporalAA": "2", "r_screenFraction": "67", "r_resolutionScaleMode": "2"}, max_shift=0.02),
    # g_stopTime is single-player only and the spawn point is random: pickups
    # turn, lights pulse and the view differs from run to run. Its median cell
    # measured 0.05-0.13 px; the unfixed single-player resolve measured 1.0 px
    # by the same statistic.
    Case("mp-native", "temporal AA on the backend-owned multiplayer path", {"r_temporalAA": "1"},
         multiplayer=True, moving_scene=True, max_shift=0.2),
]


def read_tga(path: Path) -> np.ndarray:
    data = path.read_bytes()
    id_length, _, image_type = data[0], data[1], data[2]
    width = int.from_bytes(data[12:14], "little")
    height = int.from_bytes(data[14:16], "little")
    depth, descriptor = data[16], data[17]
    if image_type != 2 or depth not in (24, 32):
        raise ValueError(f"{path}: unsupported TGA type {image_type}/{depth}")
    channels = depth // 8
    pixels = np.frombuffer(data, dtype=np.uint8, count=width * height * channels, offset=18 + id_length)
    image = pixels.reshape(height, width, channels)[:, :, :3][:, :, ::-1]
    if not descriptor & 0x20:
        image = image[::-1]
    return image.astype(np.float32)


def luma(image: np.ndarray) -> np.ndarray:
    return image @ np.array([0.25, 0.5, 0.25], dtype=np.float32)


def global_shift(previous: np.ndarray, current: np.ndarray) -> tuple[float, float]:
    """Least-squares translation, in pixels, that maps previous onto current."""
    mean = 0.5 * (previous + current)
    gx = 0.5 * (mean[1:-1, 2:] - mean[1:-1, :-2])
    gy = 0.5 * (mean[2:, 1:-1] - mean[:-2, 1:-1])
    dt = (current - previous)[1:-1, 1:-1]
    a = np.array([[np.sum(gx * gx), np.sum(gx * gy)], [np.sum(gx * gy), np.sum(gy * gy)]], dtype=np.float64)
    b = -np.array([np.sum(gx * dt), np.sum(gy * dt)], dtype=np.float64)
    if abs(np.linalg.det(a)) < 1e-6:
        return 0.0, 0.0
    dx, dy = np.linalg.solve(a, b)
    return float(dx), float(dy)


def analyse(shots: Path) -> dict:
    """Orders the two ping-pong history images into one frame sequence."""
    frames: list[np.ndarray] = []
    names: list[str] = []
    for prefix in HISTORY_IMAGES:
        if (shots / f"{prefix}0_000.tga").is_file():
            break
    else:
        return {"error": "no history readback was written"}
    previous = [None, None]
    for index in range(FRAMES):
        loaded = []
        for slot in (0, 1):
            path = shots / f"{prefix}{slot}_{index:03d}.tga"
            if not path.is_file():
                return {"error": f"missing {path.name}"}
            loaded.append(luma(read_tga(path)))
        if index > 0:
            # the image written this frame is the one that changed since the last readback
            changed = [float(np.mean(np.abs(loaded[slot] - previous[slot]))) for slot in (0, 1)]
            written = 0 if changed[0] >= changed[1] else 1
            frames.append(loaded[written])
            names.append(f"{prefix}{written}_{index:03d}")
        previous = loaded
    shifts, changed_share, mean_step, cell_shift = [], [], [], []
    height, width = frames[0].shape
    for a, b in zip(frames, frames[1:]):
        dx, dy = global_shift(a, b)
        shifts.append((dx, dy))
        # the median of a 4x3 grid ignores the cells that hold something moving
        cells = []
        for row in range(3):
            for column in range(4):
                ys = slice(row * height // 3, (row + 1) * height // 3)
                xs = slice(column * width // 4, (column + 1) * width // 4)
                cells.append(float(np.hypot(*global_shift(a[ys, xs], b[ys, xs]))))
        cell_shift.append(float(np.median(cells)))
        delta = np.abs(b - a)
        changed_share.append(float(np.mean(delta > 4.0)))
        mean_step.append(float(np.mean(delta)))
    cycle = np.stack(frames[-8:])
    ripple = float(np.mean(np.abs(cycle - cycle.mean(axis=0))))
    magnitudes = [float(np.hypot(dx, dy)) for dx, dy in shifts]
    return {
        "image": prefix, "frames": len(frames), "size": [int(frames[0].shape[1]), int(frames[0].shape[0])],
        "maxShift": max(magnitudes), "rmsShift": float(np.sqrt(np.mean(np.square(magnitudes)))),
        "maxCellShift": max(cell_shift),
        "shifts": [[round(dx, 4), round(dy, 4)] for dx, dy in shifts],
        "maxChangedShare": max(changed_share), "meanStep": float(np.mean(mean_step)),
        "ripple": ripple,
    }


def launch_case(runtime: Path, output: Path, backend: str, case: Case, width: int, height: int,
                hidden: bool, timeout: int) -> dict:
    run_dir = output / f"{backend}-{case.name}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    lines = [f"set {name} {value}" for name, value in case.cvars.items()]
    if not case.multiplayer:
        lines += ["god", "notarget", "noclip", "wait 30", f"setviewpos {POSE}"]
    lines += ["wait 120", "set g_stopTime 1", "wait 120", "rendererTemporalPresentationStatus reset"]
    for index in range(FRAMES):
        for prefix in HISTORY_IMAGES:
            for slot in (0, 1):
                lines.append(f"screenshot image {prefix}{slot} screenshots/{prefix}{slot}_{index:03d}.tga")
        lines.append("wait 1")
    lines += ["rendererTemporalPresentationStatus", "wait 2", "quit"]
    (game_dir / "stability.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")

    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name: str, value) -> None:
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    setv("logFile", 2)
    setv("logFileName", "logs/stability.log")
    setv("developer", 1)
    setv("fs_savepath", run_dir)
    setv("fs_devpath", run_dir)
    setv("fs_game", "baseoq4")
    setv("com_skipLoadingContinue", 1)
    setv("com_loadingContinueAutoAdvance", 1)
    setv("g_autoSkipCinematics", 1)
    setv("com_skipLogoVideos", 1)
    setv("in_mouse", 0)
    setv("com_maxfps", 0)
    setv("r_swapInterval", 0)
    setv("r_fullscreen", 0)
    setv("r_mode", -1)
    setv("r_customWidth", width)
    setv("r_customHeight", height)
    setv("r_windowWidth", width)
    setv("r_windowHeight", height)
    setv("r_borderless", 0)
    setv("r_borderlessDefaultMigrated", 1)
    if hidden:
        setv("r_hiddenWindow", 1)
    setv("r_motionBlur", 0)
    setv("ui_retained", 1)
    setv("ui_autoJoin", 1 if case.multiplayer else 0)
    setv("r_renderApi", "vulkan" if backend == "vk" else "gl")
    setv("g_autoExecAfterMapLoad", "stability.cfg")
    setv("g_autoExecAfterMapLoadDelayMs", 1500)
    for name, value in case.launch.items():
        setv(name, value)
    if case.multiplayer:
        setv("si_pure", 0)
        setv("net_serverDedicated", 0)
        setv("si_gameType", "DM")
        command += ["+devmap", MP_MAP]
    else:
        command += ["+devmap", MAP]

    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "stability.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    resolve = re.findall(r"Temporal resolve: (.*)", log_text)
    result = {
        "backend": backend, "case": case.name, "description": case.description,
        "exitCode": exit_code, "seconds": round(time.time() - started, 1), "log": str(log_path),
        "resolve": resolve[-1].strip() if resolve else "",
        "failures": [line for line in log_text.splitlines()
                     if any(pattern.search(line) for pattern in FAILURE_PATTERNS)],
    }
    result.update(analyse(game_dir / "screenshots"))
    return result


def evaluate(case: Case, result: dict) -> list[str]:
    problems: list[str] = []
    if result["exitCode"] != 0:
        problems.append(f"exit code {result['exitCode']}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    if "error" in result:
        problems.append(result["error"])
        return problems
    if case.moving_scene:
        if result["maxCellShift"] > case.max_shift:
            problems.append(f"the median screen cell moves {result['maxCellShift']:.3f} px between frames "
                            f"(limit {case.max_shift})")
    elif result["maxShift"] > case.max_shift:
        problems.append(f"image moves {result['maxShift']:.3f} px between frames (limit {case.max_shift})")
    if case.moving_scene:
        pass
    elif result["maxChangedShare"] > case.max_changed:
        problems.append(f"{100.0 * result['maxChangedShare']:.2f}% of pixels change between frames "
                        f"(limit {100.0 * case.max_changed:.2f}%)")
    if not case.moving_scene and result["ripple"] > case.max_ripple:
        problems.append(f"accumulation ripple {result['ripple']:.3f} levels (limit {case.max_ripple})")
    history = re.search(r"history=(\d+)", result.get("resolve", ""))
    if history is None or int(history.group(1)) < FRAMES - 2:
        problems.append(f"history-backed resolves: {result.get('resolve', '') or 'no status line'}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--cases", default="", help="comma-separated case names (default: all)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--visible", action="store_true", help="show the window (default: hidden)")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        for case in CASES:
            print(f"{case.name:20} {','.join(case.backends):6} {case.description}")
        return 0
    wanted = {name for name in args.cases.split(",") if name}
    backends = [name for name in args.backends.split(",") if name]
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report: dict = {"map": MAP, "pose": POSE, "runtime": str(runtime), "cases": []}
    failed = False
    for case in CASES:
        if wanted and case.name not in wanted:
            continue
        for backend in backends:
            if backend not in case.backends:
                continue
            result = launch_case(runtime, output, backend, case, args.width, args.height,
                                 not args.visible, args.timeout)
            result["problems"] = evaluate(case, result)
            result["pass"] = not result["problems"]
            failed |= not result["pass"]
            report["cases"].append(result)
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend:2} {case.name:20} "
                  f"shift max={result.get('maxShift', float('nan')):.4f}px rms={result.get('rmsShift', float('nan')):.4f}px "
                  f"cell={result.get('maxCellShift', float('nan')):.4f}px "
                  f"changed={100.0 * result.get('maxChangedShare', float('nan')):.2f}% "
                  f"ripple={result.get('ripple', float('nan')):.3f} "
                  + ("; ".join(result["problems"]) if result["problems"] else ""), flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["# Temporal AA stability", "", f"Map `{MAP}`, pose `{POSE}`, runtime `{runtime}`.", "",
             "| Backend | Case | Result | Max shift (px) | RMS shift (px) | Changed | Ripple | Notes |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for result in report["cases"]:
        lines.append(f"| {result['backend']} | {result['case']} | {'pass' if result['pass'] else 'FAIL'} | "
                     f"{result.get('maxShift', float('nan')):.4f} | {result.get('rmsShift', float('nan')):.4f} | "
                     f"{100.0 * result.get('maxChangedShare', float('nan')):.2f}% | "
                     f"{result.get('ripple', float('nan')):.3f} | {'; '.join(result['problems'])} |")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
