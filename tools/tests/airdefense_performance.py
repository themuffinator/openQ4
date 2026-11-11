#!/usr/bin/env python3
"""Qualify fullscreen airdefense1 rotations with the Remastered HUD at 150 FPS.

Run only when fullscreen testing is authorized. Uses engine commands for camera
motion and screenshots, with mouse/joystick capture disabled. Example:
  python tools/tests/airdefense_performance.py --render-api vk --runs 3

The default workload is native 2560x1440 with Quality, the preset below Ultra.
Its 144 FPS cap is disabled for the 150 FPS qualification. Ultra remains available
as an explicit historical-comparison workload; neither preset is modified here.
The floor is the 99th percentile frame interval at or under 6.67 ms (the user's
decision on 5 November 2026, replacing the earlier every-frame rule, which exact
optimization cannot meet on the laptop reference); the maximum is reported, not
judged. --render-thread selects and records the Vulkan worker mode. Raw engine
logs, captures, launch commands and runtime/Git identity remain in each
underlying gameplay report.
"""
from __future__ import annotations

import argparse
import contextlib
import json
import os
from pathlib import Path
import re
import sys

import renderer_gameplay_benchmark as benchmark

ROOT = Path(__file__).resolve().parents[2]
SAMPLE_MS = 27000  # Includes the complete 24-second game-time sweep and its wall-clock overrun.
# 150 FPS: the engine reports percentiles as the upper edge of 50 us bins, so a
# pass is never rounded in.
P99_FRAME_MS = 6.6667
# Maximum requests every quality maximum and carries no frame-rate target.
MAXIMUM_QUALITY = {
    "com_performancePreset": "maximum", "r_multiSamples": 16, "r_postAA": 3,
    "image_anisotropy": 16, "image_picmip": 0, "image_downSize": 0,
    "image_downSizeBump": 0, "image_downSizeSpecular": 0, "image_ignoreHighQuality": 0,
    "image_usePrecompressedTextures": 1, "r_shadows": 1, "r_useShadowMap": 1,
    "r_shadowMapSize": 4096, "r_shadowMapPointSize": 2048,
    "r_shadowMapFilterTaps": 13, "r_shadowMapPointFilterTaps": 13,
    "r_shadowMapFilterMode": 2, "r_shadowMapPointFilterMode": 1,
    "r_shadowMapAtlasSize": 16384, "r_shadowMapMaxUpdatesPerView": 0,
    "r_shadowMapCSM": 1, "r_shadowMapCascadeCount": 4, "r_shadowMapProjectedCSM": 1,
    "r_shadowMapCascadeStabilize": 1, "r_motionBlurObjectVectors": 1,
    "r_shadowMapHashedAlpha": 1, "r_shadowMapTranslucentReceivers": 1,
    "r_shadowMapPointLights": 1, "r_ssao": 1, "r_ssaoSamples": 32,
    # Automatic: full resolution at 32 samples, half at Quality's 20.
    # The CVar is archived, so a stale value would otherwise pick either.
    "r_ssaoResolution": 0,
    "r_bloom": 1, "r_bloomMipCount": 5, "r_motionBlur": 1, "r_motionBlurSamples": 16,
    "r_hdrSceneTarget": 1, "r_hdrToneMap": 1, "r_useLightGrid": 1,
    "r_skipBump": 0, "r_skipSpecular": 0, "r_skipSky": 0,
    "r_screenFraction": 100, "r_rendererDynamicResolution": 0,
    "g_showHud": 1, "ui_showGun": 1, "ui_retained": 1, "ui_hudLayout": 1,
    "r_skipParticles": 0, "r_skipInteractions": 0, "bse_render": 1, "r_skipSubviews": 0,
}
# FPS-02 Ultra: the 75 Hz class's best image.
ULTRA_QUALITY = {
    **MAXIMUM_QUALITY,
    "com_performancePreset": "ultra", "r_multiSamples": 4,
    "r_shadowMapPointSize": 512, "r_shadowMapPointFilterTaps": 9,
    "r_shadowMapPointFilterMode": 0, "r_shadowMapAtlasSize": 8192,
    "r_shadowMapCSM": 0, "r_motionBlur": 0, "r_motionBlurSamples": 8,
}
QUALITY = {
    **ULTRA_QUALITY,
    "com_performancePreset": "quality", "r_multiSamples": 2, "r_postAA": 2,
    "r_shadowMapSize": 2048, "r_shadowMapFilterTaps": 9, "r_shadowMapAtlasSize": 4096,
    "r_ssaoSamples": 20,
}
QUALITY_PROFILES = {"quality": QUALITY, "ultra": ULTRA_QUALITY, "maximum": MAXIMUM_QUALITY}
POST_AA = {"quality": "SMAA1xHigh", "ultra": "SMAA1xUltra", "maximum": "SMAA1xUltra"}


def check_rotation(text: str, api: str, preset: str = "quality") -> list[str]:
    """Require completed motion inside the measured interval, and a real HUD."""
    failures = []
    quality = QUALITY_PROFILES[preset]
    markers = [
        "benchmarkViewSweep: start, 360.0 degrees over 12000 ms",
        "benchmarkViewSweep: complete, 360.0 degrees over 12000 ms",
        "benchmarkViewSweep: start, 720.0 degrees over 24000 ms",
        "Frame pacing stats reset",
        "benchmarkViewSweep: complete, 720.0 degrees over 24000 ms",
        "Frame pacing snapshot",
    ]
    position = 0
    for marker in markers:
        found = text.find(marker, position)
        if found < 0:
            failures.append(f"missing or out-of-order rotation evidence: {marker}")
            break
        position = found + len(marker)
    reset = text.find("Frame pacing stats reset")
    if text.count("Frame pacing stats reset") != 1:
        failures.append("expected exactly one sampling reset")
    warmup = text[:reset] if reset >= 0 else ""
    profile = re.search(r'Retained UI root profile: (\{[^\n]+\})', warmup)
    try:
        complete_profile = profile and json.loads(profile[1])["frames"] == 1000
    except (ValueError, KeyError):
        complete_profile = False
    if not complete_profile or "Retained UI document work: path=guis/hud/remastered_marine.rui" not in warmup:
        failures.append("Remastered HUD warm-up profile did not complete before sampling")
    backend = "Vulkan" if api == "vk" else "OpenGL"
    for effect in ("SSAO", "motion blur", "bloom/tone map"):
        if effect == "motion blur" and not quality["r_motionBlur"]:
            continue
        if f"{backend}: first {effect} pass drew" not in warmup:
            failures.append(f"enabled effect did not draw before sampling: {effect}")
    active = "vulkan" if api == "vk" else "gl"
    if not re.search(rf"^Renderer API: requested={active} active={active} disposition=", text, re.M):
        failures.append("requested renderer did not remain active")
    samples = quality["r_multiSamples"]
    effective = "|".join(str(n) for n in (2, 4, 8, 16) if n <= samples)
    if not re.search(rf"^Renderer AA: MSAA requested={samples} effective=(?:{effective}) .*postAAEffective=1 postAAReason={POST_AA[preset]} screenFraction=100%", text, re.M):
        failures.append("MSAA/SMAA/native scene settings differ from the requested workload")
    for name, value in quality.items():
        observed = re.findall(rf'^"{re.escape(name)}" is:"([^"\n]+)"', text, re.M)
        if observed != [str(value)]:
            failures.append(f"{preset} quality readback missing or incorrect: {name}={value}")
    if re.search(r"required shadow resource unavailable|shadow map pass failed|fall back unshadowed", text):
        failures.append("rendering lost required shadow resources")
    poses = re.findall(r"^origin: \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", text, re.M)
    if len(poses) != 1 or any(abs(float(value)-expected) > 1 for value, expected in zip(poses[0], (10325, -6962.8, 7))):
        failures.append("camera did not stay at the airdefense1 starting position")
    snapshots = re.findall(r"^Frame pacing snapshot.*$", text, re.M)
    if len(snapshots) != 1 or not re.search(r"swapInterval=0, cap=0,", snapshots[0]):
        failures.append("sample was capped, hidden, or had ambiguous pacing snapshots")
    windows = re.findall(r"^Frame timing window .*samples=(\d+), span_ms=(\d+), mean_ms=([\d.]+)", text, re.M)
    if len(windows) != 1:
        failures.append("expected exactly one complete frame interval window")
    else:
        count, span, mean = int(windows[0][0]), int(windows[0][1]), float(windows[0][2])
        if count < 1000:
            failures.append(f"sample count below qualification minimum: {count}<1000")
        if not SAMPLE_MS-100 <= span <= SAMPLE_MS+1000 or mean <= 0:
            failures.append("sample interval is incomplete or invalid")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-api", choices=("gl", "vk"), default="gl")
    parser.add_argument("--preset", choices=tuple(QUALITY_PROFILES), default="quality")
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--render-thread", type=int, choices=(0, 1, 2), default=0,
                        help="Vulkan worker mode (r_vkRenderThread); 0 is the shipped default")
    parser.add_argument("--runtime-dir", default="",
                        help="staged runtime (default .install; alternates live below .tmp/stock-runtime/)")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args(argv)
    quality = QUALITY_PROFILES[args.preset]
    if not 1 <= args.runs <= 10:
        parser.error("--runs must be between 1 and 10")
    if not 320 <= args.width <= 16384 or not 240 <= args.height <= 16384:
        parser.error("resolution is outside the engine's supported range")
    output = (args.output_dir or ROOT / f".tmp/airdefense-perf/qualification-{args.preset}-{args.render_api}").resolve()
    if not output.is_relative_to(ROOT / ".tmp"):
        parser.error("--output-dir must be inside this project's .tmp directory")
    if output.exists() and any(output.iterdir()):
        parser.error("output contains evidence already; choose an empty directory")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    old_hint = os.environ.get("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS")
    os.environ["SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"] = "0"
    try:
        for run in range(1, args.runs + 1):
            directory = output / f"run-{run}"
            directory.mkdir()
            command = [
                "--profile", "smoke", "--cases", "sp-airdefense1", "--tiers", "auto",
                "--basepath", "",  # Use the engine's installed-asset discovery.
                "--maxfps", "0", "--swap-intervals", "0", "--display-modes", "fullscreen",
                "--render-api", args.render_api, "--width", str(args.width), "--height", str(args.height),
                "--pacing-only", "--no-gpu-timers", "--sample-msec", str(SAMPLE_MS),
                "--min-pacing-hz", "150", "--max-p99-ms", str(P99_FRAME_MS),
                "--timeout", "300", "--output-dir", str(directory),
                *(["--runtime-dir", args.runtime_dir] if args.runtime_dir else []),
            ]
            for name, value in {
                "in_mouse": 0, "in_joystick": 0, "ui_retained": 1, "ui_hudLayout": 1,
                # Keep the launch below the engine's 64-command limit. Only
                # allocation/sampler settings need to precede loading the map;
                # the full preset and its quality readback run in the cfg.
                **{key: quality[key] for key in (
                    "r_multiSamples", "image_anisotropy", "image_picmip", "image_downSize",
                    "image_downSizeBump", "image_downSizeSpecular", "image_ignoreHighQuality",
                    "image_usePrecompressedTextures")}, "r_hiddenWindow": 0,
                "r_ignoreGLErrors": 1,  # Runtime defaults; correctness runs use the diagnostic harness.
                "r_vkRenderThread": args.render_thread,
            }.items():
                command += ["--set-launch-cvar", f"{name}={value}"]
            for name, value in {
                **quality,
            }.items():
                command += ["--set-cvar", f"{name}={value}"]
            for line in (
                f"applyPerformancePreset {args.preset}", "com_maxfps 0",
                "ui_retainedProfile 1000", "benchmarkViewSweep 360 12000", "waitMsec 45000",
                *quality,
                "benchmarkViewSweep 720 24000",
            ):
                command += ["--exec-command", line]
            print(f"Fullscreen {args.width}x{args.height} {args.render_api}, run {run}/{args.runs}", flush=True)
            with (output / f"runner-{run}.log").open("w", encoding="utf-8") as log, contextlib.redirect_stdout(log):
                code = benchmark.main(command)
            report_path = directory / "renderer_gameplay_benchmark_report.json"
            report = json.loads(report_path.read_text(encoding="utf-8"))
            role = report["results"][0]["roles"][0]
            failures = list(role["missing"])
            failures += check_rotation(Path(role["log"]).read_text(encoding="utf-8", errors="replace"), args.render_api, args.preset)
            if code or report["status"] != "pass":
                failures.append("underlying gameplay qualification failed")
            result = {"run": run, "status": "fail" if failures else "pass", "failures": failures,
                      "summary": role["summary"], "report": str(report_path)}
            results.append(result)
            payload = {"status": "pass" if len(results) == args.runs and all(r["status"] == "pass" for r in results) else "fail",
                       "width": args.width, "height": args.height, "renderApi": args.render_api,
                       "preset": args.preset, "quality": quality, "postAA": POST_AA[args.preset], "targetFps": 150,
                       "p99FrameMs": P99_FRAME_MS, "renderThread": args.render_thread,
                       "requestedRuns": args.runs, "results": results}
            (output / "qualification.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
            print(f"run {run}: {result['status']}; mean={role['summary'].get('pacingHz')} FPS, "
                  f"p99={role['summary'].get('pacingP99Ms')} ms, max={role['summary'].get('pacingMaxMs')} ms", flush=True)
    finally:
        if old_hint is None:
            os.environ.pop("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", None)
        else:
            os.environ["SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"] = old_hint
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
