#!/usr/bin/env python3
"""REN-09 runtime qualification of shadowed volumetric lighting in motion, at
scaled resolutions and in subviews.

Opt-in, local and hardware-bound; the companion of
renderer_volumetric_lighting.py, which covers frozen stock views. Every case
launches the staged client once per backend, drives it with console commands
and stock entities only (no input is synthesized) and reads engine
screenshots, temporal-history readbacks and rendererVolumetricStatus.

Cases:

- caster: a stock door pair closes and opens between a spawned shadow-casting
  light and the camera, in airdefense2, while the camera and the flashlight it
  carries pan through every door movement. At each paused door state the
  scattered light alone (r_volumetricDebug 1) is captured from the
  motion-warmed shadow caches, from fresh shadow maps and with the same
  lights unshadowed (r_volumetricDebug 2). Requires: the flashlight and the
  spawned light scatter shadow-mapped; the cached capture matches the fresh
  one; shadowed scattering is never brighter than unshadowed scattering (no
  leak); and where the doors change the scattered light, it returns with
  them.
- smear: the same doors under temporal AA, frozen halfway through opening.
  The resolved history is read back 1 to 16 frames after the freeze and once
  it has settled, with the leaf off and on: the trail of the moving shaft
  must clear from the history, and as fast as the trail of the moving doors
  clears without the leaf.
- scale: a frozen outdoor view under temporal upscaling at 50% and under
  dynamic resolution forced down and released. The scattering target must
  follow the scene size, the leaf must stay effective, and the frame must
  return to the baseline when the leaf goes off.
- subview: the REN-01 validation rooms with a direct mirror, a capture-backed
  mirror, a reflection and a remote camera. With r_volumetricSubviews 1 the
  subviews scatter too and the picture inside them changes; 0 leaves them
  as they were and reports them declined; the scratch targets are not
  reallocated while the views hold still; the leaf off is the baseline.
- decline: a backend limit under which the leaf must stand down and say why.
  With post-processing skipped the status reports the leaf requested, not
  effective, with the reason, and the frame is the frame without the leaf.
  The GLSL 1.20 (Apple GL 2.1) and GLES limits decline through the same
  path; no desktop driver here can be made to report them, so they stay with
  the physical platform runs.

A JSON report and a Markdown summary go to --output-dir.
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

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from renderer_temporal_stability import HISTORY_IMAGES, luma, read_tga  # noqa: E402

DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "volumetric-motion-qualification"

LEAF = {"r_rendererFroxelVolumetrics": "1"}
RESET = {"r_rendererFroxelVolumetrics": "0", "r_volumetricStencilLights": "0", "r_volumetricDebug": "0",
         "r_volumetricSubviews": "1", "r_useShadowMap": "1", "r_shadowMapStaticCache": "1"}
SHADOW_STARTUP = {"r_useShadowMap": "1", "r_shadowMapCSM": "1", "r_shadowMapPointLights": "1",
                  "g_dynamicLightShadows": "1"}

# renderer_shadow_mapping_movers.py door-projected: a stock door pair in the open
DOOR_MAP = "game/airdefense2"
DOOR_POSE = "-1000 1568 84 0 -90 0"
DOOR_ORIGIN = "-1000 1300 32"
# behind the doors as the camera sees them, so the leaves shadow the air in front
DOOR_LIGHT = 'spawn light name volumetric_light origin "-1000 1150 96" light_radius "520 520 320" _color "1 0.85 0.6"'
SWEEP_DEGREES, SWEEP_MSEC = 540, 4000
# The stock door between the camera and the fixture (func_door_274/275) is
# opened once by a trigger at the player start. Captures taken while it is
# still moving do not show the scene the later ones do: open it and let it
# finish before the first one.
STOCK_DOOR_OPEN = ["testDoor func_door_275 open", "wait 150"]
# The flashlight swings back behind the view for a few tics after the pan
# stops; the doors are at rest by then, so the pose settles before the freeze.
POSE_SETTLE = "wait 40"

SCALE_MAP, SCALE_POSE = "game/airdefense1", "10200 -6800 40 0 170 0"

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Validation Error|VUID-"),
    re.compile(r"GLSL .* failed for builtin/volumetric"),
    re.compile(r"volumetric lighting pass could not run"),
)


def setting_lines(cvars: dict[str, str]) -> list[str]:
    return [f"set {name} {value}" for name, value in {**RESET, **cvars}.items()]


def capture_lines(label: str, cvars: dict[str, str], settle: int = 12) -> list[str]:
    return setting_lines(cvars) + [f"wait {settle}", f"echo VOLUMETRIC_CONFIG {label}",
                                   "rendererTemporalPresentationStatus", "rendererVolumetricStatus",
                                   f"screenshot screenshots/{label}.tga", "wait 3"]


def history_lines(label: str) -> list[str]:
    lines = []
    for prefix in HISTORY_IMAGES:
        for slot in (0, 1):
            lines.append(f"screenshot image {prefix}{slot} screenshots/{label}_{prefix}{slot}.tga")
    return lines


def door_spawns() -> list[str]:
    lines = []
    for side, direction in (("left", 0), ("right", 180)):
        lines.append(
            f"spawn func_door name shadow_door_{side} "
            f"model models/mapobjects/strogg/doors/generic/prison/door_{side}.lwo "
            f'origin "{DOOR_ORIGIN}" angle 90 '
            f"movedir {direction} team shadow_door wait -1 no_touch 1 time 1 lip 0")
    return lines


def door_move(action: str, ticks: int, sweep: bool) -> list[str]:
    lines = ["g_stopTime 0"]
    if sweep:
        lines.append(f"benchmarkViewSweep {SWEEP_DEGREES} {SWEEP_MSEC}")
    lines += [f"testDoor shadow_door_left {action}", f"wait {ticks}"]
    if sweep:
        lines += ["benchmarkViewSweep 0 0", f"setviewpos {DOOR_POSE}", POSE_SETTLE]
    lines += ["g_stopTime 1", "testDoor shadow_door_left"]
    return lines


CASTER_STATES = ("closed", "open", "reclosed")
CASTER_CONFIGS = (
    ("base", {}),
    ("vol", LEAF),
    ("cached", {**LEAF, "r_volumetricDebug": "1"}),
    ("fresh", {**LEAF, "r_volumetricDebug": "1", "r_shadowMapStaticCache": "0"}),
    # the same lights, in the same order, with no shadow term
    ("unshadowed", {**LEAF, "r_volumetricDebug": "2"}),
)


def caster_cfg() -> list[str]:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", "set r_temporalAA 0",
             "set r_motionBlur 0", *setting_lines({}), "g_stopTime 0", f"setviewpos {DOOR_POSE}", "testFlashlight 1",
             *STOCK_DOOR_OPEN, *door_spawns(), DOOR_LIGHT, "wait 40", "g_stopTime 1"]

    def state(label: str) -> None:
        # a caster stays live for a quarter second of real time after it last moved
        lines.append("waitMsec 350")
        for name, cvars in CASTER_CONFIGS:
            lines.extend(capture_lines(f"{label}_{name}", cvars))
        lines.extend(setting_lines({}))

    state("closed")
    lines += ["g_stopTime 0", *setting_lines(LEAF), f"benchmarkViewSweep {SWEEP_DEGREES} {SWEEP_MSEC}",
              "testDoor shadow_door_left open", "wait 24", "echo VOLUMETRIC_CONFIG live",
              "rendererVolumetricStatus", "screenshot screenshots/live.tga", "wait 46",
              "benchmarkViewSweep 0 0", f"setviewpos {DOOR_POSE}", POSE_SETTLE, "g_stopTime 1",
              "testDoor shadow_door_left"]
    state("open")
    lines += door_move("close", 70, True)
    state("reclosed")
    return lines + ["wait 5", "quit"]


SMEAR_FRAMES = (1, 2, 4, 8, 16)
SMEAR_MOVE_TICS = 30


def smear_cfg() -> list[str]:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", "set r_temporalAA 1",
             "set r_motionBlur 0", *setting_lines({}), "g_stopTime 0", f"setviewpos {DOOR_POSE}", "testFlashlight 1",
             *STOCK_DOOR_OPEN, *door_spawns(), DOOR_LIGHT, "wait 40"]
    for name, cvars in (("off", {}), ("on", LEAF)):
        # Freeze the doors halfway through opening: the frame before held the
        # leaves, and the shaft between them, a step behind.
        lines += [*setting_lines(cvars), "testDoor shadow_door_left close", "wait 110",
                  "testDoor shadow_door_left open", f"wait {SMEAR_MOVE_TICS}", "g_stopTime 1",
                  f"echo VOLUMETRIC_CONFIG {name}", "testDoor shadow_door_left", "rendererVolumetricStatus"]
        waited = 0
        for frame in SMEAR_FRAMES:
            lines += [f"wait {frame - waited}", *history_lines(f"{name}_frame{frame}")]
            waited = frame
        lines += ["wait 120", *history_lines(f"{name}_late"), "rendererTemporalPresentationStatus",
                  "g_stopTime 0", "wait 60"]
    return lines + ["wait 5", "quit"]


SCALE_STEPS = (
    ("base", {}, {}),
    ("vol", LEAF, {}),
    ("taau50", LEAF, {"r_screenFraction": "50"}),
    ("dynamic_low", LEAF, {"r_rendererDynamicResolution": "1", "r_dynamicResolutionTargetMsec": "0.05"}),
    ("dynamic_high", LEAF, {"r_rendererDynamicResolution": "1", "r_dynamicResolutionTargetMsec": "500"}),
    ("vol_again", LEAF, {}),
    ("base_again", {}, {}),
)
SCALE_RESET = {"r_screenFraction": "100", "r_rendererDynamicResolution": "0", "r_dynamicResolutionTargetMsec": "0"}


def scale_cfg() -> list[str]:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", "set r_temporalAA 2",
             "set r_resolutionScaleMode 1", "set r_motionBlur 0", "set r_rendererGpuTimers 1",
             *setting_lines({}), f"setviewpos {SCALE_POSE}", "wait 60", "g_stopTime 1", "wait 60"]
    for label, cvars, scale in SCALE_STEPS:
        lines += [f"set {name} {value}" for name, value in {**SCALE_RESET, **scale}.items()]
        lines += capture_lines(label, cvars, settle=300 if "dynamic" in label else 90)
    return lines + ["wait 5", "quit"]


SUBVIEW_STEPS = (
    ("base", {}),
    ("subviews", LEAF),
    # nothing changes: the scattering targets must hold still across the views
    ("subviews_hold", LEAF),
    ("mainonly", {**LEAF, "r_volumetricSubviews": "0"}),
    ("base_again", {}),
)
# renderer_shared_domain_qualification.py validation rooms A and B
SUBVIEW_POSES = {"panels": "0 -100 128 0 90 0", "nested": "0 700 128 0 90 0"}
# The inside of the direct mirror the nested pose faces, as rows and columns
# of the 1280x720 capture. A backend that shows no reflection there with the
# leaf off has no picture for the leaf to change; that is recorded as a note
# on the baseline, and the status checks still apply.
SUBVIEW_MIRROR = {"nested": (230, 470, 520, 760)}
SUBVIEW_MIRROR_EMPTY = 4.0


def subview_cfg() -> list[str]:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", "set r_temporalAA 0",
             "set r_motionBlur 0", *setting_lines({})]
    for room, pose in SUBVIEW_POSES.items():
        lines += [f"setviewpos {pose}", "wait 60", "g_stopTime 1", "wait 30"]
        for label, cvars in SUBVIEW_STEPS:
            lines += capture_lines(f"{room}_{label}", cvars, settle=30)
        lines += [*setting_lines({}), "g_stopTime 0"]
    return lines + ["wait 5", "quit"]


DECLINE_STEPS = (
    ("base", {}),
    ("vol", LEAF),
    ("skippost_base", {"r_skipPostProcess": "1"}),
    ("skippost", {**LEAF, "r_skipPostProcess": "1"}),
    ("base_again", {}),
)
DECLINE_RESET = {"r_skipPostProcess": "0"}


def decline_cfg() -> list[str]:
    lines = ["god", "notarget", "noclip", "set g_showHud 0", "set ui_showGun 0", "set r_temporalAA 0",
             "set r_motionBlur 0", *setting_lines({}), f"setviewpos {SCALE_POSE}", "wait 60", "g_stopTime 1", "wait 60"]
    for label, cvars in DECLINE_STEPS:
        lines += [f"set {name} {value}" for name, value in DECLINE_RESET.items()]
        lines += capture_lines(label, cvars, settle=30)
    return lines + ["wait 5", "quit"]


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
            configs[current]["scale"] = int(values.get("effectiveScale", "0%").rstrip("%") or 0)
            configs[current]["scene"] = values.get("scene")
        elif line.startswith(f"Volumetric lighting {label}:"):
            configs[current]["status"] = dict(re.findall(r"(\w+)=(\S+)", line))
        elif line.startswith("Volumetric lighting: requested="):
            values = dict(re.findall(r"(\w+)=(\S+)", line))
            configs[current]["requested"] = values.get("requested")
            configs[current]["allocations"] = int(values.get("targetAllocations", "0"))
        elif line.startswith("testDoor: name="):
            configs[current].setdefault("door", line)
    return configs


STALL_SECONDS = 420


def run_client(command: list[str], runtime: Path, log_path: Path, timeout: int, attempts: int = 3):
    """Run the client to its exit. On a machine shared with other renderer
    sessions a launch sometimes stalls while the driver compiles the start-up
    programs: a log that stops growing before the script's first capture is
    killed and launched again, never counted as a result."""
    for attempt in range(1, attempts + 1):
        if log_path.is_file():
            log_path.unlink()
        process = subprocess.Popen(command, cwd=runtime)
        began = progress = time.time()
        size = -1
        stalled = False
        while process.poll() is None:
            time.sleep(1.0)
            now = time.time()
            current = log_path.stat().st_size if log_path.is_file() else 0
            if current != size:
                size, progress = current, now
            in_script = log_path.is_file() and b"VOLUMETRIC_CONFIG" in log_path.read_bytes()[-200000:]
            if now - began > timeout or (not in_script and now - progress > STALL_SECONDS):
                stalled = not in_script and now - began <= timeout
                process.kill()
                process.wait()
                break
        else:
            return process.returncode, attempt
        if not stalled:
            return "timeout", attempt
        print(f"  launch stalled before the script ran (attempt {attempt}); relaunching", flush=True)
    return "stalled", attempts


def launch(runtime: Path, output: Path, backend: str, case: str, map_name: str, cfg: list[str],
           startup: dict[str, str], files: dict[str, bytes], timeout: int) -> dict:
    run_dir = output / f"{backend}-{case}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    for relative, content in files.items():
        path = game_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    (game_dir / "volumetric.cfg").write_text("\n".join(cfg) + "\n", encoding="ascii")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(exe)]

    def setv(name, value):
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    for name, value in (("logFile", 2), ("logFileName", "logs/volumetric.log"), ("developer", 1),
                        ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
                        ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
                        ("g_autoSkipCinematics", 1), ("com_skipLogoVideos", 1), ("in_mouse", 0),
                        ("com_maxfps", 0), ("r_swapInterval", 0), ("r_fullscreen", 0), ("r_mode", -1),
                        ("r_customWidth", 1280), ("r_customHeight", 720), ("r_windowWidth", 1280),
                        ("r_windowHeight", 720), ("r_borderless", 0), ("r_borderlessDefaultMigrated", 1),
                        ("r_hiddenWindow", 1), ("ui_retained", 1), ("ui_autoJoin", 0), ("com_fixedTic", 1),
                        ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
                        ("g_autoExecAfterMapLoad", "volumetric.cfg"), ("g_autoExecAfterMapLoadDelayMs", 0),
                        *SHADOW_STARTUP.items(), *startup.items()):
        setv(name, value)
    command += ["+devmap", map_name]
    started = time.time()
    log_path = game_dir / "logs" / "volumetric.log"
    exit_code, attempts = run_client(command, runtime, log_path, timeout)
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    result = {"backend": backend, "case": case, "map": map_name, "exitCode": exit_code,
              "seconds": round(time.time() - started, 1), "attempts": attempts, "log": str(log_path),
              "configs": parse_log(log_text, backend),
              "failures": [line for line in log_text.splitlines()
                           if any(pattern.search(line) for pattern in FAILURE_PATTERNS)]}
    problems: list[str] = []
    if exit_code != 0:
        problems.append(f"exit code {exit_code}")
    problems += [f"log: {line.strip()}" for line in result["failures"][:5]]
    result["problems"] = problems
    result["shots"] = game_dir / "screenshots"
    return result


def shot(result: dict, name: str):
    path = result["shots"] / f"{name}.tga"
    return read_tga(path) if path.is_file() else None


def effective(result: dict, label: str, problems: list[str], mapped: int = 1) -> dict:
    status = result["configs"].get(label, {}).get("status", {})
    if status.get("effective") != "1":
        problems.append(f"{label}: not effective ({status.get('decline', 'no status')})")
    elif int(status.get("shadowMapped", "0")) < mapped:
        problems.append(f"{label}: {status.get('shadowMapped', '0')} shadow-mapped lights scattered "
                        f"(at least {mapped})")
    return status


def judge_caster(result: dict) -> None:
    problems = result["problems"]
    metrics: dict = {}
    images = {f"{state}_{name}": shot(result, f"{state}_{name}")
              for state in CASTER_STATES for name, _ in CASTER_CONFIGS}
    missing = [name for name, image in images.items() if image is None]
    if missing:
        problems.append(f"captures missing: {', '.join(missing[:4])}")
        result["metrics"] = metrics
        return
    for state in CASTER_STATES:
        # the flashlight and the spawned light, both through their own maps
        effective(result, f"{state}_vol", problems, mapped=2)
        cached, fresh = luma(images[f"{state}_cached"]), luma(images[f"{state}_fresh"])
        open_beam = luma(images[f"{state}_unshadowed"])
        entry = {
            "effect": float(np.mean(np.abs(images[f"{state}_vol"] - images[f"{state}_base"]))),
            "scatter": float(cached.mean()), "unshadowed": float(open_beam.mean()),
            "cachedFreshMeanAbs": float(np.mean(np.abs(cached - fresh))),
            "cachedFreshChanged": float(np.mean(np.abs(cached - fresh) > 2.0)),
            # shadowed scattering brighter than the same light unshadowed: a leak
            "leakShare": float(np.mean(cached > open_beam + 3.0)),
            "shadowedShare": float(np.mean(open_beam > cached + 3.0)),
        }
        entry["deficit"] = float(np.clip(open_beam - cached, 0.0, None).mean())
        metrics[state] = entry
        if entry["scatter"] < 0.25:
            problems.append(f"{state}: no scattered light in view (mean {entry['scatter']:.3f})")
        if entry["cachedFreshChanged"] > 0.002:
            problems.append(f"{state}: scattering from motion-warmed caches differs from fresh maps in "
                            f"{100 * entry['cachedFreshChanged']:.2f}% of pixels")
        if entry["leakShare"] > 0.002:
            problems.append(f"{state}: shadowed scattering is brighter than unshadowed scattering in "
                            f"{100 * entry['leakShare']:.2f}% of pixels (leak)")
    # Stock lights, GUIs and the idle sway of the carried flashlight keep
    # running between the states, so the frames are compared where the doors
    # changed the scattered light, not everywhere.
    fixture = float(np.mean(np.abs(luma(images["reclosed_base"]) - luma(images["closed_base"]))))
    metrics["fixtureReturned"] = fixture
    if fixture > 3.0:
        problems.append(f"fixture: the scene without the leaf did not return with the doors (mean {fixture:.3f})")
    scatter = {state: luma(images[f"{state}_cached"]) for state in CASTER_STATES}
    moved_by = np.abs(scatter["closed"] - scatter["open"])
    mask = moved_by > 16.0
    metrics["shadowMovedShare"] = float(mask.mean())
    metrics["shadowedShareClosed"] = metrics["closed"]["shadowedShare"]
    if mask.mean() < 0.01:
        problems.append(f"the scattered light did not follow the doors ({100 * mask.mean():.2f}% of pixels changed "
                        "by more than 16 levels between closed and open)")
    else:
        moved = float(moved_by[mask].mean())
        returned = float(np.abs(scatter["closed"] - scatter["reclosed"])[mask].mean())
        metrics["shadowMoved"] = moved
        metrics["shadowReturned"] = returned
        if returned > 0.5 * moved:
            problems.append(f"the shadow in the medium did not return with the doors ({returned:.2f} levels from "
                            f"the closed frame where the doors had moved it {moved:.2f})")
    if metrics["closed"]["shadowedShare"] < 0.01:
        problems.append("closed: the casters shadow under 1% of the scattered light in view")
    live = effective(result, "live", problems, mapped=1)
    metrics["livePasses"] = live.get("passes")
    result["metrics"] = metrics


def history_pair(result: dict, label: str):
    for prefix in HISTORY_IMAGES:
        loaded = [shot(result, f"{label}_{prefix}{slot}") for slot in (0, 1)]
        if all(image is not None for image in loaded):
            return [luma(image) for image in loaded]
    return None


def judge_smear(result: dict) -> None:
    problems = result["problems"]
    metrics: dict = {}
    for name in ("off", "on"):
        late = history_pair(result, f"{name}_late")
        if late is None:
            problems.append(f"{name}: no temporal history readback")
            continue
        settled = 0.5 * (late[0] + late[1])
        residual = {}
        for frame in SMEAR_FRAMES:
            pair = history_pair(result, f"{name}_frame{frame}")
            if pair is None:
                problems.append(f"{name}: history readback of frame {frame} missing")
                break
            # either slot is the history written this frame or the one before
            residual[frame] = min(float(np.mean(np.abs(pair[slot] - settled))) for slot in (0, 1))
        door = result["configs"].get(name, {}).get("door", "")
        metrics[name] = {"residual": residual, "settledMean": float(settled.mean()),
                         # what the settled history still changes by from frame to frame
                         "floor": float(np.mean(np.abs(late[0] - late[1]))),
                         "frozenMoving": " state=2 " in door}
        if not metrics[name]["frozenMoving"]:
            problems.append(f"{name}: the doors were not frozen in motion ({door or 'no door status'})")
    effective(result, "on", problems, mapped=2)
    complete = all(name in metrics and len(metrics[name]["residual"]) == len(SMEAR_FRAMES) for name in ("off", "on"))
    if complete:
        first, last = SMEAR_FRAMES[0], SMEAR_FRAMES[-1]
        off, on = metrics["off"], metrics["on"]
        if off["residual"][first] < 0.05:
            problems.append(f"fixture: the frozen doors left nothing in the history to clear "
                            f"({off['residual'][first]:.3f} levels)")
        # The trail of the moving shaft must clear, and as fast as the trail
        # of the moving doors does without the leaf.
        metrics["clearedOff"] = off["residual"][last] / max(off["residual"][first], 1e-6)
        metrics["clearedOn"] = on["residual"][last] / max(on["residual"][first], 1e-6)
        if on["residual"][last] > max(0.25 * on["residual"][first], on["floor"] + 0.15):
            problems.append(f"the leaf's trail is still in the history {last} frames after the doors stop "
                            f"({on['residual'][last]:.3f} of {on['residual'][first]:.3f} levels)")
        if metrics["clearedOn"] > 1.5 * metrics["clearedOff"] + 0.15:
            problems.append(f"the history clears more slowly with the leaf ({metrics['clearedOn']:.3f} of the "
                            f"first frame's residual left after {last} frames, {metrics['clearedOff']:.3f} without)")
    result["metrics"] = metrics


def judge_scale(result: dict) -> None:
    problems = result["problems"]
    metrics: dict = {}
    images = {label: shot(result, label) for label, _, _ in SCALE_STEPS}
    if any(image is None for image in images.values()):
        problems.append("captures missing")
        result["metrics"] = metrics
        return
    for label, cvars, _ in SCALE_STEPS:
        config = result["configs"].get(label, {})
        entry = {"scale": config.get("scale"), "scene": config.get("scene")}
        if cvars:
            status = effective(result, label, problems)
            entry["target"] = status.get("target")
            scene = re.fullmatch(r"(\d+)x(\d+)", config.get("scene") or "")
            target = re.fullmatch(r"(\d+)x(\d+)", status.get("target") or "")
            if scene is None or target is None:
                problems.append(f"{label}: scene or scattering target size not reported")
            else:
                for axis in (1, 2):
                    expected = (int(scene.group(axis)) + 1) // 2
                    if abs(int(target.group(axis)) - expected) > 1:
                        problems.append(f"{label}: scattering target {status.get('target')} does not follow the "
                                        f"scene {config.get('scene')} at half resolution")
                        break
            entry["effect"] = float(np.mean(np.abs(images[label] - images["base"])))
        metrics[label] = entry
    if metrics["taau50"]["scale"] != 50:
        problems.append(f"taau50: effective scale {metrics['taau50']['scale']}%")
    if not metrics["dynamic_low"]["scale"] or metrics["dynamic_low"]["scale"] >= 100:
        problems.append(f"dynamic_low: dynamic resolution did not drop ({metrics['dynamic_low']['scale']}%)")
    if (metrics["dynamic_high"]["scale"] or 0) <= (metrics["dynamic_low"]["scale"] or 0):
        problems.append(f"dynamic_high: dynamic resolution did not recover ({metrics['dynamic_high']['scale']}%)")
    if metrics["vol"]["effect"] < 0.05:
        problems.append(f"vol: no visible effect (mean {metrics['vol']['effect']:.3f})")
    for label in ("taau50", "dynamic_low"):
        if metrics[label]["effect"] < 0.05:
            problems.append(f"{label}: no visible effect at the scaled scene (mean {metrics[label]['effect']:.3f})")
    again = float(np.mean(np.abs(images["vol_again"] - images["vol"])))
    base_again = float(np.mean(np.abs(images["base_again"] - images["base"])))
    metrics["volAgainMeanAbs"] = again
    metrics["baseAgainMeanAbs"] = base_again
    if again > 0.5:
        problems.append(f"the leaf at native scale differs after the scale changes (mean {again:.3f})")
    if not np.array_equal(images["base_again"], images["base"]):
        problems.append(f"the baseline differs after the scale changes (mean {base_again:.4f})")
    result["metrics"] = metrics


def judge_subview(result: dict) -> None:
    problems = result["problems"]
    metrics: dict = {}
    for room in SUBVIEW_POSES:
        images = {label: shot(result, f"{room}_{label}") for label, _ in SUBVIEW_STEPS}
        if any(image is None for image in images.values()):
            problems.append(f"{room}: captures missing")
            continue
        configs = result["configs"]
        with_subviews = configs.get(f"{room}_subviews", {}).get("status", {})
        main_only = configs.get(f"{room}_mainonly", {}).get("status", {})
        entry = {
            "views": int(with_subviews.get("views", "0")), "subviews": int(with_subviews.get("subviews", "0")),
            "mainOnlyViews": int(main_only.get("views", "0")),
            "mainOnlySubviews": int(main_only.get("subviews", "0")),
            "mainOnlyDeclined": int(main_only.get("viewsDeclined", "0")),
            "allocationsHeld": configs.get(f"{room}_subviews_hold", {}).get("allocations", -1)
                               - configs.get(f"{room}_subviews", {}).get("allocations", -2),
            "effect": float(np.mean(np.abs(images["subviews"] - images["base"]))),
            "subviewShare": float(np.mean(np.abs(luma(images["subviews"]) - luma(images["mainonly"])) > 2.0)),
            "holdIdentical": bool(np.array_equal(images["subviews_hold"], images["subviews"])),
            "baseAgainIdentical": bool(np.array_equal(images["base_again"], images["base"])),
        }
        metrics[room] = entry
        for label, status in (("subviews", with_subviews), ("mainonly", main_only)):
            if status.get("effective") != "1":
                problems.append(f"{room}_{label}: not effective ({status.get('decline', 'no status')})")
        if entry["subviews"] < 1:
            problems.append(f"{room}: no subview scattered ({entry['views']} views, {entry['subviews']} subviews)")
        if entry["mainOnlySubviews"] != 0 or entry["mainOnlyViews"] != 1:
            problems.append(f"{room}: r_volumetricSubviews 0 scattered in {entry['mainOnlyViews']} views, "
                            f"{entry['mainOnlySubviews']} of them subviews")
        if entry["mainOnlyDeclined"] < 1:
            problems.append(f"{room}: r_volumetricSubviews 0 reported no declined subview")
        if entry["allocationsHeld"] != 0:
            problems.append(f"{room}: the scattering targets were reallocated {entry['allocationsHeld']} times "
                            "while nothing changed (a resize per view)")
        if not entry["holdIdentical"]:
            problems.append(f"{room}: the frozen frame with scattering subviews is not stable")
        mirror = SUBVIEW_MIRROR.get(room)
        if mirror is not None:
            top, bottom, left, right = mirror
            entry["baselineMirror"] = float(images["base"][top:bottom, left:right].mean())
        if entry.get("baselineMirror", 255.0) < SUBVIEW_MIRROR_EMPTY:
            result.setdefault("notes", []).append(
                f"{room}: the direct mirror shows no reflection with the leaf off on this backend "
                f"(mean {entry['baselineMirror']:.2f}), so there is no picture inside it to compare")
        elif entry["subviewShare"] < 0.002:
            problems.append(f"{room}: the picture inside the subviews did not change "
                            f"({100 * entry['subviewShare']:.2f}% of pixels)")
        if not entry["baseAgainIdentical"]:
            problems.append(f"{room}: the baseline differs after the leaf ran")
    result["metrics"] = metrics


def judge_decline(result: dict) -> None:
    problems = result["problems"]
    metrics: dict = {}
    images = {label: shot(result, label) for label, _ in DECLINE_STEPS}
    if any(image is None for image in images.values()):
        problems.append("captures missing")
        result["metrics"] = metrics
        return
    configs = result["configs"]

    def declined(label: str, reason: str, reference: str) -> None:
        status = configs.get(label, {}).get("status", {})
        entry = {"requested": configs.get(label, {}).get("requested"), "effective": status.get("effective"),
                 "decline": status.get("decline"),
                 "identical": bool(np.array_equal(images[label], images[reference]))}
        metrics[label] = entry
        if entry["requested"] != "1":
            problems.append(f"{label}: the leaf is not reported as requested")
        if entry["effective"] != "0" or entry["decline"] != reason:
            problems.append(f"{label}: expected the leaf declined with {reason}, the status says "
                            f"effective={entry['effective']} decline={entry['decline']}")
        if not entry["identical"]:
            problems.append(f"{label}: the declined frame differs from {reference}")

    effective(result, "vol", problems)
    metrics["vol"] = {"effect": float(np.mean(np.abs(images["vol"] - images["base"])))}
    if metrics["vol"]["effect"] < 0.05:
        problems.append(f"vol: no visible effect (mean {metrics['vol']['effect']:.3f})")
    declined("skippost", "post-process-skipped", "skippost_base")
    if not np.array_equal(images["base_again"], images["base"]):
        problems.append("the baseline differs after the declined steps")
    result["metrics"] = metrics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--cases", default="caster,smear,scale,subview,decline")
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra startup cvar, for diagnosis")
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    extra = dict(item.split("=", 1) for item in args.set)
    cases = [name for name in args.cases.split(",") if name]
    fixture_files: dict[str, bytes] = {}
    fixture_map = ""
    if "subview" in cases:
        import renderer_shared_domain_qualification as shared
        fixture_files = shared.prepare_fixture(runtime, output, args.timeout)
        fixture_map = shared.FIXTURE_MAP
    plans = {
        "caster": (DOOR_MAP, caster_cfg, {}, {}, judge_caster),
        "smear": (DOOR_MAP, smear_cfg, {}, {}, judge_smear),
        "scale": (SCALE_MAP, scale_cfg, {}, {}, judge_scale),
        "subview": (fixture_map, subview_cfg, {}, fixture_files, judge_subview),
        "decline": (SCALE_MAP, decline_cfg, {}, {}, judge_decline),
    }
    report = {"runtime": str(runtime), "runs": []}
    failed = False
    lines = ["# Volumetric lighting in motion, at scale and in subviews (REN-09)", "",
             "| Backend | Case | Result | Notes |", "| --- | --- | --- | --- |"]
    for backend in [name for name in args.backends.split(",") if name]:
        for case in cases:
            map_name, cfg, startup, files, judge = plans[case]
            result = launch(runtime, output, backend, case, map_name, cfg(), {**startup, **extra}, files,
                            args.timeout)
            judge(result)
            result["shots"] = str(result["shots"])
            result["pass"] = not result["problems"]
            failed |= not result["pass"]
            report["runs"].append(result)
            notes = result.get("notes", [])
            print(f"{'PASS' if result['pass'] else 'FAIL'} {backend} {case}: "
                  + ("; ".join(result["problems"]) or "ok")
                  + ("".join(f" [note: {note}]" for note in notes)), flush=True)
            print("  " + json.dumps(result.get("metrics", {})), flush=True)
            lines.append(f"| {backend} | {case} | {'pass' if result['pass'] else 'FAIL'} | "
                         f"{'; '.join(result['problems'] + ['note: ' + note for note in notes])} |")
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
