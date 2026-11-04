#!/usr/bin/env python3
"""Opt-in stock SP/MP Vulkan HDR integration, with engine captures and isolated saves.

Qualifies actual scene formats, live HDR toggles, asynchronous/synchronous
exposure, temporal scaling, resize/restart and capture continuity. This is
correctness evidence, not a performance or release-promotion result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

import renderer_gameplay_benchmark as gameplay
import renderer_validation_matrix as matrix

ROOT = Path(__file__).resolve().parents[2]
HDR_INFO = re.compile(r"Vulkan HDR: ([^\r\n]+)")


def snapshot(name: str, *, capture: bool = True) -> list[str]:
    lines = []
    if name in ("scaled", "resized", "partial"):
        # A single sampled frame can legitimately reject history after a camera
        # cut, hitch or newly visible entity. Prove fresh complete draws over an
        # interval, without treating that conservative fallback as a failure.
        lines += [f"echo HDR_MOTION_START_{name}", "rendererTemporalPresentationStatus", "wait 60"]
    lines += [f"echo HDR_CHECK_{name}", "rendererVulkanHDRInfo", "rendererTemporalPresentationStatus"]
    if capture:
        lines += [f'screenshot "screenshots/{name}.tga"']
    return lines


def write_script(gamepath: Path) -> str:
    lines = ["r_mode -1", "wait 150", "framePacingReset", "wait 150", "framePacingSnapshot"]
    lines += snapshot("initial")
    # The screenshot itself must not reset, sample or advance adapted exposure.
    lines += snapshot("after_capture", capture=False)
    lines += ["r_hdrSceneTarget 0", "wait 90"] + snapshot("ldr")
    lines += ["r_hdrSceneTarget 1", "r_rendererModernQuality 1", "r_temporalAA 1", "r_screenFraction 75", "wait 120"]
    lines += snapshot("scaled")
    lines += ["r_hdrAutoExposureAsync 0", "wait 30"] + snapshot("synchronous")
    lines += ["r_hdrAutoExposureAsync 1", "r_screenFraction 100", "r_windowWidth 1024", "r_windowHeight 640",
              "r_customWidth 1024", "r_customHeight 640", "vid_restart", "wait 150"]
    lines += snapshot("resized")
    # Keep the capture and partial restart in the same command batch. Vulkan
    # resumes a recording scope after screenshots; an intervening wait hides
    # the stale-swapchain lifetime regression this sequence must exercise.
    lines += ["r_windowWidth 960", "r_windowHeight 600", "r_customWidth 960", "r_customHeight 600",
              "vid_restart partial", "wait 90"]
    lines += snapshot("partial")
    lines += ["r_hdrAutoExposure 0", "wait 15"] + snapshot("manual")
    lines += ["r_hdrToneMap 0", "r_temporalAA 0", "wait 30"] + snapshot("disabled")
    lines += ["gfxInfo", "wait 5", "quit"]
    name = "vulkan-hdr.cfg"
    (gamepath / name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return name


def read_states(log: str) -> dict[str, dict[str, str]]:
    states = {}
    # Anchor to the printed echo output, not a command/config dump.
    for match in re.finditer(r"(?m)^HDR_CHECK_([a-z_]+)\s*$", log):
        tail = log[match.end():]
        info = HDR_INFO.search(tail.split("HDR_CHECK_", 1)[0])
        if info:
            states[match[1]] = dict(re.findall(r"(\w+)=(\S+)", info[1]))
    return states


def validate_states(states: dict[str, dict[str, str]], samples: int, mode: str | None = None) -> list[str]:
    failures = []
    for name in ("initial", "after_capture", "ldr", "scaled", "synchronous", "resized", "partial", "manual", "disabled"):
        state = states.get(name)
        if state is None:
            failures.append(f"missing HDR state: {name}")
            continue
        expected_format = "LDR" if name in ("ldr", "disabled") else "RGBA16F"
        active = name not in ("manual", "disabled")
        expected_extent = ("960x540" if name in ("scaled", "synchronous") else "960x600"
                           if name in ("partial", "manual", "disabled") else "1024x640"
                           if name == "resized" else "1280x720")
        if state.get("sceneFormat") != expected_format or state.get("extent") != expected_extent:
            failures.append(f"{name}: wrong scene format/extent: {state}")
        if state.get("samples") != str(samples):
            failures.append(f"{name}: actual MSAA differs from requested {samples}: {state.get('samples')}")
        if mode == "sp" and state.get("skyPreserved") != str(int(name != "disabled")):
            failures.append(f"{name}: portal sky was not preserved by the actual HDR owner")
        if state.get("async") != ("0" if name == "synchronous" else "1"):
            failures.append(f"{name}: exposure readback mode differs from the requested control")
        if state.get("autoExposure") != str(int(active)) or state.get("initialized") != str(int(active)):
            failures.append(f"{name}: exposure initialization/enable mismatch")
        if active:
            try:
                values = [float(state[field]) for field in ("average", "target", "exposure")]
                if not all(math.isfinite(value) and value > 0 for value in values):
                    failures.append(f"{name}: nonfinite/nonpositive exposure")
                if int(state["completed"]) < 10 or int(state["queued"]) < int(state["completed"]):
                    failures.append(f"{name}: insufficient or impossible readback completions")
            except (KeyError, ValueError) as exc:
                failures.append(f"{name}: malformed exposure state: {exc}")
    if "initial" in states and "after_capture" in states:
        # Existing pending copies may retire during screenshot readback. The
        # screenshot must not queue a new sample or advance adaptation itself.
        for field in ("generation", "queued", "exposure", "average"):
            if states["initial"].get(field) != states["after_capture"].get(field):
                failures.append(f"engine screenshot changed exposure {field}")
    for earlier, later in (("initial", "ldr"), ("ldr", "scaled"), ("resized", "partial")):
        if earlier in states and later in states:
            try:
                if int(states[later]["generation"]) <= int(states[earlier]["generation"]):
                    failures.append(f"{later}: scene discontinuity did not reset exposure generation")
            except (KeyError, ValueError):
                failures.append(f"{later}: malformed exposure generation")
    return failures


def validate_temporal_motion(log: str) -> tuple[dict, list[str]]:
    """Require real completed rigid draws in the TAA parts of stock gameplay."""
    states, failures = {}, []
    for name in ("scaled", "resized", "partial"):
        beginning = re.search(rf"(?m)^HDR_MOTION_START_{name}\s*$([\s\S]*?)(?=^HDR_CHECK_|\Z)", log)
        section = re.search(rf"(?m)^HDR_CHECK_{name}\s*$([\s\S]*?)(?=^HDR_CHECK_|\Z)", log)
        before = re.search(r"Vulkan temporal motion: ([^\r\n]+)", beginning[1]) if beginning else None
        info = re.search(r"Vulkan temporal motion: ([^\r\n]+)", section[1]) if section else None
        if not info or not before:
            failures.append(f"{name}: missing rigid temporal motion telemetry")
            continue
        start = dict(re.findall(r"(\w+)=(\S+)", before[1]))
        state = dict(re.findall(r"(\w+)=(\S+)", info[1]))
        states[name] = state
        try:
            completed = int(state["completedViews"]) - int(start["completedViews"])
            state["intervalCompletedViews"] = str(completed)
            presentation = re.search(r"Temporal presentation: ([^\r\n]+)", section[1])
            active = dict(re.findall(r"(\w+)=(\S+)", presentation[1])) if presentation else {}
            if (completed < 10 or int(state["drawn"]) > int(state["eligible"])
                    or (state["complete"] == "1" and state["drawn"] != state["eligible"])
                    or state["complete"] not in ("0", "1")
                    or active.get("taaRequested") != "1" or active.get("frame") != state["frame"]
                    or active.get("historyGeneration") != state["generation"]
                    or int(state["frame"]) < 0 or int(state["generation"]) <= 0):
                failures.append(f"{name}: rigid temporal draws not proven complete: {state}")
        except (KeyError, ValueError) as exc:
            failures.append(f"{name}: malformed rigid temporal telemetry: {exc}")
    return states, failures


def run_case(runtime: Path, output: Path, basepath: Path, mode: str, samples: int, timeout: int) -> dict:
    case_id = f"hdr-{mode}-msaa{samples}"
    evidence = output / case_id
    evidence.mkdir()
    savepath = evidence / "savepath"
    gamepath = savepath / "baseoq4"
    (gamepath / "screenshots").mkdir(parents=True)
    script = write_script(gamepath)
    args = matrix.common_args(runtime, case_id, str(basepath), savepath, False)
    settings = {
        "r_renderApi": "vulkan", "r_vkValidation": "1", "r_hiddenWindow": "1",
        "in_mouse": "0", "in_joystick": "0", "s_noSound": "1", "r_mode": "-1",
        "r_windowWidth": "1280", "r_windowHeight": "720", "r_customWidth": "1280", "r_customHeight": "720",
        "r_multiSamples": str(samples), "r_swapInterval": "0", "com_maxFps": "240",
        "r_hdrToneMap": "1", "r_hdrSceneTarget": "1", "r_hdrAutoExposure": "1", "r_hdrAutoExposureAsync": "1",
        "r_bloom": "0",  # keep the disabled-HDR control independent of the first-run preset
        "com_skipLoadingContinue": "1", "g_autoSkipCinematics": "1",
        "g_autoExecAfterMapLoad": script, "g_autoExecAfterMapLoadDelayMs": "1000",
        "si_gameType": "singleplayer" if mode == "sp" else "DM", "net_serverDedicated": "0",
        "ui_autoJoin": "1", "net_port": "28767", "net_allowCheats": "1", "net_LANServer": "1",
    }
    for name, value in settings.items():
        args += ["+set", name, value]
    map_name = "game/airdefense1" if mode == "sp" else "mp/q4dm1"
    args += ["+gfxInfo", "+map" if mode == "sp" else "+spawnServer", map_name]
    if sum(arg.startswith("+") for arg in args) > matrix.ENGINE_MAX_STARTUP_COMMANDS:
        raise ValueError("HDR gameplay exceeds the engine startup command limit")
    stdout, stderr = evidence / "stdout.txt", evidence / "stderr.txt"
    exit_code, timed_out, elapsed = gameplay.launch_and_wait(
        matrix.find_client_executable(runtime), args, runtime, stdout, stderr, timeout)
    log_path = matrix.find_log(savepath, f"openq4_validation_{case_id}.log")
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path else ""
    diagnostics = "\n".join((log, stdout.read_text(encoding="utf-8", errors="replace"),
                              stderr.read_text(encoding="utf-8", errors="replace")))
    warnings = matrix.count_warning_signatures(diagnostics)
    checks = [["Renderer API: requested=vulkan active=vulkan disposition=module"],
              ["Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)"],
              [f"maps/{map_name}.map"], [f"AutoExecAfterMapLoad: executed {script}"],
              [f"Frame pacing snapshot ({mode.upper()}):"], ["idRenderSystem::Shutdown()"]]
    _, failures = matrix.evaluate_checks(diagnostics, checks, warnings,
        ["Vulkan: HDR luminance sample unavailable", "Vulkan: r_bloom/r_hdrToneMap pass could not run",
         "cannot be changed in multiplayer"])
    states = read_states(log)
    failures += validate_states(states, samples, mode)
    motion, motion_failures = validate_temporal_motion(log)
    failures += motion_failures
    aa = re.findall(r"Renderer AA: MSAA requested=(\d+) effective=(\d+)", log)
    if not aa or aa[-1] != (str(samples), str(samples)):
        failures.append("final AA telemetry does not describe the requested and rendered scene samples")
    if exit_code != 0 or timed_out:
        failures.append(f"process exit={exit_code}, timedOut={timed_out}")
    screenshots = {}
    for name in ("initial", "ldr", "scaled", "synchronous", "resized", "partial", "manual", "disabled"):
        path = gamepath / "screenshots" / f"{name}.tga"
        try:
            width, height, rgb = gameplay.load_tga_rgb(path)
            expected_size = ((960, 600) if name in ("partial", "manual", "disabled") else (1024, 640)
                             if name == "resized" else (1280, 720))
            if (width, height) != expected_size or max(rgb) == min(rgb):
                failures.append(f"{name}: blank or incorrectly sized engine capture")
            screenshots[name] = {"path": str(path), "width": width, "height": height,
                                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        except (OSError, ValueError) as exc:
            failures.append(f"{name}: invalid engine capture: {exc}")
    result = {"id": case_id, "status": "fail" if failures else "pass", "map": map_name,
              "exitCode": exit_code, "timedOut": timed_out, "elapsedSeconds": round(elapsed, 2),
              "launchArgs": args, "log": str(log_path), "states": states, "temporalMotion": motion, "screenshots": screenshots,
              "warningSignatures": warnings, "failures": failures}
    (evidence / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{case_id}: {result['status']} ({elapsed:.1f}s)", flush=True)
    for failure in failures:
        print(f"  {failure}", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--modes", nargs="+", choices=("sp", "mp"), default=["sp", "mp"])
    parser.add_argument("--samples", type=int, nargs="+", choices=(0, 4), default=[0, 4])
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    runtime = gameplay.validate_runtime_dir(args.runtime_dir, ROOT)
    output, basepath = args.output_dir.resolve(), args.basepath.resolve()
    if not (basepath / "q4base").is_dir() or not matrix.vk_module_path(runtime).is_file():
        parser.error("installed Quake 4 assets and a staged Vulkan module are required")
    if output == runtime or runtime in output.parents:
        parser.error("evidence must be outside the tested runtime")
    output.mkdir(parents=True, exist_ok=False)
    harness_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    before = gameplay.collect_runtime_files(runtime)
    results = [run_case(runtime, output, basepath, mode, samples, args.timeout)
               for mode in dict.fromkeys(args.modes) for samples in dict.fromkeys(args.samples)]
    after = gameplay.collect_runtime_files(runtime)
    passed = before == after and all(result["status"] == "pass" for result in results)
    report = {"kind": "vulkan-hdr-gameplay", "status": "pass" if passed else "fail", "runtime": str(runtime),
              "harnessSHA256": harness_hash,
              "runtimeUnchanged": before == after, "runtimeFilesBefore": before, "runtimeFilesAfter": after,
              "results": results}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
