#!/usr/bin/env python3
"""Qualify the display-referred fallback of stock content under the linear HDR scene (VK-05).

Stock Quake 4 materials stay display-referred by design: their blended,
dynamic, texgen, cinematic and late-sort stages, and the subviews that draw
them, are declined by complete-view admission instead of being re-blended in
linear light. This harness freezes stock single-player maps and toggles the
laboratory linear scene (r_pbrLinearScene) under tone mapping and under the
HDR-off PBR preview. On Vulkan every request, and its removal, must leave the
engine capture byte-identical to the display-referred frame, and the
ownership report must show the request with no admitted main view. OpenGL is
held to one display level, because its tone-mapped airdefense1 frame wanders
by one level even between captures with identical settings.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

import renderer_gameplay_benchmark as gameplay
import renderer_validation_matrix as matrix

ROOT = Path(__file__).resolve().parents[2]
MAPS = ("game/airdefense1", "game/process1")
CONTROLS = (
    ("display", {"r_hdrToneMap": 1, "r_pbrLinearScene": 0}),
    ("linear", {"r_pbrLinearScene": 1}),
    ("linear-restored", {"r_pbrLinearScene": 0}),
    ("preview-off", {"r_hdrToneMap": 0}),
    ("preview", {"r_pbrLinearScene": 1}),
    ("preview-restored", {"r_pbrLinearScene": 0}),
)
GROUPS = (("display", "linear", "linear-restored"), ("preview-off", "preview", "preview-restored"))
ADMITTED = ("complete", "accumulating", "output")


def section(log: str, name: str) -> str:
    return log.split("HDR_FALLBACK_" + name + "_BEGIN", 1)[-1].split("HDR_FALLBACK_" + name + "_END", 1)[0]


def ownership(text: str) -> dict:
    linear = re.search(r"Vulkan HDR scene ownership: requested=(\d) committed=(\d) linearActive=(\d) reason=(\S+)", text)
    preview = re.search(r"Vulkan PBR preview: requested=(\d) committed=(\d) reason=(\S+)", text)
    main = re.search(r"Vulkan HDR scene views on this map: main view reason=(\S+)", text)
    outcomes = {f"{kind} {reason}": int(count)
                for kind, reason, count in re.findall(r"^\s+(linear|preview) (\S+): (\d+)", text, re.M)}
    return {"linear": linear.groups() if linear else None, "preview": preview.groups() if preview else None,
            "mainViewReason": main[1] if main else None, "outcomes": outcomes}


def validate(captures: dict, reports: dict, backend: str) -> list[str]:
    failures = []
    # Vulkan must reproduce the frozen frame byte for byte. OpenGL's tone-mapped
    # airdefense1 frame wanders by one level between captures with identical
    # settings, so the reference backend is held to one display level.
    limit = 0 if backend == "vulkan" else 1
    for group in GROUPS:
        if any(name not in captures for name in group):
            failures.append("missing capture in " + "/".join(group))
            continue
        for other in group[1:]:
            error = captures[group[0]]["maximumErrorTo"].get(other, 255)
            if error > limit:
                failures.append(f"linear-scene request changed the stock frame: {group[0]}/{other} ({error} levels)")
    if backend != "vulkan":
        return failures
    linear, preview = reports.get("linear", {}), reports.get("preview", {})
    if not linear.get("linear") or linear["linear"][0] != "1" or linear["linear"][2] != "0":
        failures.append("linear request was not recorded, or a stock view became linear")
    if linear.get("mainViewReason") in (None, *ADMITTED):
        failures.append(f"stock main view was admitted to the linear scene ({linear.get('mainViewReason')})")
    if any(key.split(" ", 1)[1] in ADMITTED for key in linear.get("outcomes", {}) if key.startswith("linear ")):
        failures.append("a stock view was admitted to the linear scene")
    if not preview.get("preview") or preview["preview"][1] != "0":
        failures.append("the HDR-off PBR preview committed on stock content")
    return failures


def run_map(args, runtime: Path, basepath: Path, output: Path, map_name: str) -> dict:
    slug = map_name.split("/")[-1]
    save = output / slug / "savepath"
    game = save / "baseoq4"
    (game / "screenshots").mkdir(parents=True)
    case_id = f"hdr-fallback-{args.backend}-{slug}"
    settings = {
        "r_renderApi": args.backend, "r_vkValidation": "1",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_mode": "-1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_customWidth": "1280", "r_customHeight": "720", "r_multiSamples": "0",
        "r_swapInterval": "0", "com_maxFps": "60", "ui_autoJoin": "1",
        "r_hdrToneMap": "1", "r_hdrSceneTarget": "1", "r_hdrAutoExposure": "0", "r_hdrExposure": "1",
        "r_pbrMaterials": "1", "r_pbrInferFromLegacyMaterials": "0", "r_rendererModernQuality": "1",
        "r_pbrLinearScene": "0", "r_temporalAA": "0", "r_screenFraction": "100", "r_postAA": "0",
        "com_skipLoadingContinue": "1", "g_autoSkipCinematics": "1",
        "g_autoExecAfterMapLoad": "fallback.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "si_gameType": "singleplayer",
        # A frozen frame must repeat exactly: no view weapon or HUD animating
        # on real time. (com_fixedTic 1 stalls this map-load autoexec flow.)
        "ui_showGun": "0", "g_showHud": "0",
    }
    launch = matrix.common_args(runtime, case_id, str(basepath), save, False)
    for name, value in settings.items():
        launch += ["+set", name, value]
    launch += ["+map", map_name]
    if sum(s.startswith("+") for s in launch) > matrix.ENGINE_MAX_STARTUP_COMMANDS:
        raise ValueError("HDR fallback exceeds the engine startup command limit")
    # Let the first tone-mapped frames settle before the first capture.
    commands = ["wait 30", "god", "notarget", "g_stopTime 1", "wait 300"]
    for name, changes in CONTROLS:
        commands += [f"set {key} {value}" for key, value in changes.items()]
        commands += ["wait 90", f"echo HDR_FALLBACK_{name}_BEGIN", "gfxInfo"]
        if args.backend == "vulkan":
            commands += ["rendererVulkanHDRInfo"]
        commands += [f'screenshot "screenshots/{name}.tga"', f"echo HDR_FALLBACK_{name}_END"]
    commands += ["quit"]
    (game / "fallback.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")
    exit_code, timeout, elapsed = gameplay.launch_and_wait(matrix.find_client_executable(runtime), launch,
        runtime, output / slug / "stdout.txt", output / slug / "stderr.txt", 420)
    log_path = matrix.find_log(save, "openq4_validation_" + case_id + ".log")
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path else ""
    warnings = matrix.count_warning_signatures(log)
    checks = [[f"Renderer API: requested={args.backend} active={args.backend} disposition=module"],
              [f"maps/{map_name}.map"], ["AutoExecAfterMapLoad: executed fallback.cfg"],
              ["idRenderSystem::Shutdown()"]]
    if args.backend == "vulkan":
        checks += [["Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)"]]
    _, failures = matrix.evaluate_checks(log, checks, warnings)
    captures, reports, pixels = {}, {}, {}
    for name, _ in CONTROLS:
        path = game / "screenshots" / (name + ".tga")
        try:
            width, height, rgb = gameplay.load_tga_rgb(path)
            if (width, height) != (1280, 720) or max(rgb) == min(rgb):
                raise ValueError("incorrect extent or blank frame")
            captures[name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "path": str(path),
                              "maximumErrorTo": {}}
            pixels[name] = rgb
        except (OSError, ValueError) as exc:
            failures.append(f"{name}: invalid engine capture: {exc}")
        reports[name] = ownership(section(log, name))
    for group in GROUPS:
        for other in group[1:]:
            if group[0] in pixels and other in pixels:
                captures[group[0]]["maximumErrorTo"][other] = max(
                    abs(a - b) for a, b in zip(pixels[group[0]], pixels[other]))
    failures += validate(captures, reports, args.backend)
    if exit_code != 0 or timeout:
        failures.append(f"process failure: exit={exit_code} timeout={timeout}")
    return {"map": map_name, "status": "fail" if failures else "pass", "exitCode": exit_code,
            "timedOut": timeout, "elapsedSeconds": elapsed, "log": str(log_path), "warnings": warnings,
            "captures": captures, "ownership": reports, "failures": failures}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--backend", choices=("gl", "vulkan"), default="vulkan")
    args = parser.parse_args()
    runtime = gameplay.validate_runtime_dir(args.runtime_dir, ROOT)
    output, basepath = args.output_dir.resolve(), args.basepath.resolve()
    if not (basepath / "q4base").is_dir() or output == runtime or runtime in output.parents:
        parser.error("installed Quake 4 assets and an evidence directory outside the runtime are required")
    output.mkdir(parents=True, exist_ok=False)
    before = gameplay.collect_runtime_files(runtime)
    maps = [run_map(args, runtime, basepath, output, name) for name in MAPS]
    after = gameplay.collect_runtime_files(runtime)
    failures = [f"{entry['map']}: {failure}" for entry in maps for failure in entry["failures"]]
    if before != after:
        failures.append("runtime files changed during the run")
    report = {"status": "fail" if failures else "pass", "backend": args.backend, "runtime": str(runtime),
              "harnessSHA256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runtimeUnchanged": before == after, "maps": maps, "failures": failures,
              "scope": "Stock content's display-referred fallback; not linear-HDR admission of classic stages."}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"HDR stock fallback {args.backend}: {report['status']}", flush=True)
    for failure in failures:
        print("  " + failure, flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
