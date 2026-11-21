#!/usr/bin/env python3
"""Qualify parallax occlusion mapping (REN-11) in the parallax laboratory.

  --prepare        copy a staged runtime below .tmp, generate the laboratory
                   (tools/validation/generate_parallax_validation_map.py) and
                   dmap it
  --backend gl|vk  capture every case in one client and write a report
  --compare A B    compare an OpenGL and a Vulkan report: a traced case may
                   differ between the backends only as much as its flat one
  --timing         measure GPU cost per setting (median per window, repeats)

Each case is a camera and a cvar set. The checks are non-vacuous: the traced
specimen must change when parallax turns on while its flat control and the
classic room stay byte-identical, the fade must converge on the flat image,
and the admission report must show the requested and effective state.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "validation"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))
import generate_parallax_validation_map as plab  # noqa: E402
import renderer_pbr_laboratory as runner  # noqa: E402

WIDTH, HEIGHT = 1280, 800

# The production owners: the classic OpenGL light loop (draw_pbr.cpp) and
# native Vulkan, both composing into the display-referred framebuffer.
PROFILE = {**runner.BASE, **runner.GL_NATIVE_OVERRIDES, **runner.PRODUCTION_OVERRIDES,
           "r_pbrAnalyticEnvironment": "0", "r_lightScale": "1",
           "r_parallaxOcclusion": "0", "r_parallaxSamples": "32", "r_parallaxDistance": "1536",
           "r_parallaxShadows": "0", "r_shadowMapPointLights": "1"}

# case: (camera, cvars changed from PROFILE)
CASES = {
    "off": ("grazing", {}),
    "on": ("grazing", {"r_parallaxOcclusion": "1"}),
    "shadows": ("grazing", {"r_parallaxOcclusion": "1", "r_parallaxShadows": "1"}),
    "samples-min": ("grazing", {"r_parallaxOcclusion": "1", "r_parallaxSamples": "8"}),
    "samples-max": ("grazing", {"r_parallaxOcclusion": "1", "r_parallaxSamples": "64"}),
    "faded": ("grazing", {"r_parallaxOcclusion": "1", "r_parallaxDistance": "64"}),
    "steep-off": ("steep", {}),
    "steep-on": ("steep", {"r_parallaxOcclusion": "1"}),
    "wall-off": ("wall", {}),
    "wall-on": ("wall", {"r_parallaxOcclusion": "1"}),
    "far-off": ("far", {}),
    "far-on": ("far", {"r_parallaxOcclusion": "1"}),
    "strip-off": ("strip", {}),
    "strip-on": ("strip", {"r_parallaxOcclusion": "1"}),
    # The environment pass: the analytic studio environment lights the
    # specimen through the ambient walk as well as the per-light draws.
    "environment-off": ("grazing", {"r_pbrAnalyticEnvironment": "1"}),
    "environment-on": ("grazing", {"r_pbrAnalyticEnvironment": "1", "r_parallaxOcclusion": "1"}),
    # Shadow-mapped receivers: their own PBR program variants on OpenGL and
    # the shadowed interaction shaders on Vulkan.
    "mapped-off": ("grazing", {"r_shadows": "1", "r_useShadowMap": "1", "r_shadowMapPointLights": "1", "r_shadowMapReport": "1", "r_shadowMapReportInterval": "5"}),
    "mapped-on": ("grazing", {"r_shadows": "1", "r_useShadowMap": "1", "r_shadowMapPointLights": "1",
                              "r_shadowMapReport": "1", "r_shadowMapReportInterval": "5", "r_parallaxOcclusion": "1"}),
    # Raven's Parallaxbump guide (Vulkan only): its single step, then the ray.
    "retail-off": ("retail", {}),
    "retail-on": ("retail", {"r_parallaxOcclusion": "1"}),
    # Back to off after on: a live toggle must restore the exact flat image.
    "off-again": ("grazing", {}),
}

# GPU cost: median whole-frame GPU time over the last frames of each setting.
TIMING = {
    "timing-off": ("timing", {}),
    "timing-on": ("timing", {"r_parallaxOcclusion": "1"}),
    "timing-on-8": ("timing", {"r_parallaxOcclusion": "1", "r_parallaxSamples": "8"}),
    "timing-on-64": ("timing", {"r_parallaxOcclusion": "1", "r_parallaxSamples": "64"}),
    "timing-shadows": ("timing", {"r_parallaxOcclusion": "1", "r_parallaxShadows": "1"}),
    "timing-shadows-64": ("timing", {"r_parallaxOcclusion": "1", "r_parallaxShadows": "1", "r_parallaxSamples": "64"}),
    "timing-flat-off": ("timingFlat", {}),
    "timing-flat-on": ("timingFlat", {"r_parallaxOcclusion": "1"}),
}
TIMING_FRAMES = 120


def timing(args: argparse.Namespace) -> dict:
    save = args.output_dir
    save.mkdir(parents=True, exist_ok=False)
    cfg = save / "baseoq4/parallax_lab_capture.cfg"
    cfg.parent.mkdir()
    manifest = json.loads((args.runtime_root / "parallax-lab.json").read_text())
    cvars = {**PROFILE, "r_renderApi": "vulkan" if args.backend == "vk" else "gl", "r_glTier": args.tier,
             "com_maxfps": "0", "r_swapInterval": "0", "r_rendererGpuTimers": "1",
             "r_customWidth": str(args.size[0]), "r_customHeight": str(args.size[1]),
             "r_windowWidth": str(args.size[0]), "r_windowHeight": str(args.size[1])}
    keys = sorted({key for _, changes in list(CASES.values()) + list(TIMING.values()) for key in changes})
    commands = ["wait 15", "god", "notarget", "noclip", "g_stopTime 1", "r_rendererMetrics 1", "r_rendererGpuTimers 1"]
    for repeat in range(args.repeats):
        for case, (camera, changes) in TIMING.items():
            settings = {key: PROFILE[key] for key in keys}
            settings.update(changes)
            commands += ["setviewpos " + " ".join(map(str, manifest["cameras"][camera]))]
            commands += [f'set {key} "{value}"' for key, value in settings.items()]
            commands += ["wait 30", "framePacingReset", f"wait {TIMING_FRAMES + 10}",
                         f'rendererBenchmarkCapture "timing/{case}-{repeat}.csv"']
    commands += ["wait 5", "quit"]
    runner.write_capture_commands(cfg, commands)
    cvars.update({"fs_game": "baseoq4", "fs_basepath": str(args.basepath), "fs_savepath": str(save),
                  "fs_devpath": str(save), "g_autoExecAfterMapLoad": "parallax_lab_capture.cfg",
                  "g_autoExecAfterMapLoadDelayMs": "500"})
    executable = next(args.runtime_root.glob("openQ4-client_*.exe" if os.name == "nt" else "openQ4-client_*"))
    command = [str(executable), *runner.startup_args(cvars, save), "+map", plab.MAP]
    code, _ = runner.run(command, args.runtime_root, save / "process.log", args.timeout)
    result = {"backend": args.backend, "exitCode": code, "frames": TIMING_FRAMES, "size": list(args.size), "cases": {}}
    for case in TIMING:
        medians = []
        for repeat in range(args.repeats):
            trace = save / f"baseoq4/timing/{case}-{repeat}.csv"
            if not trace.is_file():
                continue
            rows = trace.read_text().splitlines()
            header = rows[0].split(",")
            gpu, valid = header.index("gpuUs"), header.index("gpuValid")
            values = sorted(float(r.split(",")[gpu]) for r in rows[1:][-TIMING_FRAMES:] if r.split(",")[valid] == "1")
            if values:
                medians.append(values[len(values) // 2])
        result["cases"][case] = {"medianGpuUs": sorted(medians)[len(medians) // 2] if medians else None, "repeats": medians}
    base = result["cases"]["timing-off"]["medianGpuUs"]
    for case, row in result["cases"].items():
        reference = result["cases"]["timing-flat-off"]["medianGpuUs"] if case.startswith("timing-flat") else base
        if row["medianGpuUs"] is not None and reference:
            row["costUs"] = round(row["medianGpuUs"] - reference, 1)
    return result


# Screen regions: x < 0 is the left half of the image (the camera looks along
# +Y, so +X is to the right). Rows are image rows from the top.
REGIONS = {
    "grazing": {"pom": (40, 600, 430, 790), "flat": (680, 1240, 430, 790), "wallPom": (40, 600, 40, 300),
                "wallFlat": (680, 1240, 40, 300)},
    "steep": {"pom": (40, 600, 40, 760), "flat": (680, 1240, 40, 760)},
    "wall": {"pom": (40, 600, 40, 760), "flat": (680, 1240, 40, 760)},
    "far": {"pom": (300, 620, 420, 700), "flat": (660, 980, 420, 700)},
    "strip": {"all": (0, WIDTH, 0, HEIGHT)},
    "retail": {"retail": (40, 1240, 430, 790)},
}


def load_rgb(path: Path) -> bytes | None:
    return runner.capture_rgb(path, (WIDTH, HEIGHT))


def region_stats(a: bytes, b: bytes, box: tuple[int, int, int, int]) -> dict:
    x0, x1, y0, y1 = box
    total = changed = peak = 0
    count = 0
    for y in range(y0, y1):
        row = y * WIDTH * 3
        for x in range(x0, x1):
            i = row + x * 3
            d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
            total += d
            peak = max(peak, d)
            changed += d > 2
            count += 1
    return {"meanAbs": round(total / max(count, 1), 4), "peak": peak, "changedShare": round(changed / max(count, 1), 4)}


def region_mean(a: bytes, box: tuple[int, int, int, int]) -> float:
    x0, x1, y0, y1 = box
    total = count = 0
    for y in range(y0, y1):
        row = y * WIDTH * 3
        for x in range(x0, x1):
            i = row + x * 3
            total += a[i] + a[i + 1] + a[i + 2]
            count += 3
    return total / max(count, 1)


def prepare(runtime: Path, install: Path, basepath: Path, output: Path, timeout: int) -> None:
    if runtime.exists():
        raise FileExistsError(f"Refusing to replace existing runtime: {runtime}")
    # Hard links where the filesystem allows (the staged packs are gigabytes);
    # the laboratory only adds new files, so nothing writes through a link.
    def link_or_copy(source: str, target: str) -> None:
        try:
            os.link(source, target)
        except OSError:
            shutil.copy2(source, target)
    shutil.copytree(install, runtime, ignore=shutil.ignore_patterns("*.pdb", "*.lib", "*.exp"),
                    copy_function=link_or_copy)
    plab.generate(runtime)
    compile_save = output / "compile"
    compile_save.mkdir(parents=True)
    args = {**runner.BASE, "fs_game": "baseoq4", "fs_basepath": str(basepath), "fs_savepath": str(compile_save),
            "fs_devpath": str(runtime), "r_rendererModernExecutor": "0", "r_renderApi": "gl"}
    executable = next(runtime.glob("openQ4-ded_*.exe" if os.name == "nt" else "openQ4-ded_*"))
    command = [str(executable), *runner.startup_args(args, compile_save), "+dmap", plab.MAP, "+quit"]
    code, _ = runner.run(command, runtime, compile_save / "process.log", timeout)
    log = compile_save / "baseoq4/logs/openq4.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    if code or not (runtime / "baseoq4" / f"{plab.MAP}.proc").is_file() or re.search(r"(?i)\b(?:leaked|fatal error|ERROR:)\b", text):
        raise RuntimeError(f"parallax map did not compile cleanly; see {compile_save}")


def capture(args: argparse.Namespace, cases: list[str]) -> dict:
    save = args.output_dir
    save.mkdir(parents=True, exist_ok=False)
    cfg = save / "baseoq4/parallax_lab_capture.cfg"
    cfg.parent.mkdir()
    manifest = json.loads((args.runtime_root / "parallax-lab.json").read_text())
    cvars = {**PROFILE, "r_renderApi": "vulkan" if args.backend == "vk" else "gl", "r_glTier": args.tier}
    if args.backend == "vk" and args.validation:
        cvars["r_vkValidation"] = "1"
    commands = ["wait 15", "god", "notarget", "noclip", "g_stopTime 1"]
    changed_keys = sorted({key for _, changes in CASES.values() for key in changes})
    for case in cases:
        camera, changes = CASES[case]
        settings = {key: PROFILE[key] for key in changed_keys}
        settings.update(changes)
        commands += ["setviewpos " + " ".join(map(str, manifest["cameras"][camera]))]
        commands += [f'set {key} "{value}"' for key, value in settings.items()]
        commands += ["wait 20", f'echo "PARALLAX_{case}_BEGIN"', "viewpos", "gfxInfo",
                     f'screenshot "screenshots/{case}.tga"', f'echo "PARALLAX_{case}_END"']
    commands += ["wait 5", "quit"]
    runner.write_capture_commands(cfg, commands)
    cvars.update({"fs_game": "baseoq4", "fs_basepath": str(args.basepath), "fs_savepath": str(save),
                  "fs_devpath": str(save), "g_autoExecAfterMapLoad": "parallax_lab_capture.cfg",
                  "g_autoExecAfterMapLoadDelayMs": "500"})
    executable = next(args.runtime_root.glob("openQ4-client_*.exe" if os.name == "nt" else "openQ4-client_*"))
    command = [str(executable), *runner.startup_args(cvars, save), "+map", plab.MAP]
    (save / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    code, elapsed = runner.run(command, args.runtime_root, save / "process.log", args.timeout)
    log = save / "baseoq4/logs/openq4.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    report = {"backend": args.backend, "runtime": str(args.runtime_root), "exitCode": code, "elapsed": round(elapsed, 1),
              "diagnostics": [line for line in runner.diagnostic_lines(text)
                              # OpenGL has no Parallaxbump program (the retail specimen)
                              if not (args.backend == "gl" and "glsl/Parallaxbump.glsl" in line)],
              "cases": {}}
    for case in cases:
        start, end = f"PARALLAX_{case}_BEGIN", f"PARALLAX_{case}_END"
        section = text.split(start, 1)[-1].split(end, 1)[0] if start in text and end in text else ""
        shot = save / f"baseoq4/screenshots/{case}.tga"
        parallax = re.search(r"Renderer parallax occlusion: (.*)", section)
        gl = re.search(r"OpenGL: native PBR parallax: (.*)", section)
        native = re.search(r"OpenGL: native PBR: (.*)", section)
        report["cases"][case] = {"camera": CASES[case][0], "screenshot": str(shot), "sha256": runner.digest(shot) if shot.is_file() else None,
                                 "parallax": parallax.group(1).strip() if parallax else None,
                                 "glParallax": gl.group(1).strip() if gl else None,
                                 "glNative": native.group(1).strip() if native else None,
                                 "markers": bool(section)}
    return report


def field(line: str | None, name: str) -> str | None:
    if not line:
        return None
    match = re.search(rf"\b{name}=(\S+)", line)
    return match.group(1) if match else None


def check_report(report: dict) -> list[str]:
    """Checks that need one backend's captures only."""
    failures: list[str] = []
    cases = report["cases"]
    if report["exitCode"]:
        failures.append(f"client exit {report['exitCode']}")
    if report["diagnostics"]:
        failures.append("engine diagnostics: " + "; ".join(report["diagnostics"][:6]))
    images = {}
    for case, row in cases.items():
        if not row["markers"]:
            failures.append(f"{case}: capture markers missing")
        image = load_rgb(Path(row["screenshot"]))
        if image is None:
            failures.append(f"{case}: screenshot missing or not {WIDTH}x{HEIGHT}")
        images[case] = image
        requested = CASES[case][1].get("r_parallaxOcclusion", "0")
        if field(row["parallax"], "requested") != requested:
            failures.append(f"{case}: parallax report requested={field(row['parallax'], 'requested')}, expected {requested}")
    comparisons = {}

    def compare(a: str, b: str) -> dict | None:
        if images.get(a) is None or images.get(b) is None:
            return None
        regions = REGIONS[CASES[a][0]]
        result = {name: region_stats(images[a], images[b], box) for name, box in regions.items()}
        result["identical"] = images[a] == images[b]
        comparisons[f"{a}:{b}"] = result
        return result

    for off, on, region in (("off", "on", "pom"), ("steep-off", "steep-on", "pom"), ("wall-off", "wall-on", "pom"),
                            ("environment-off", "environment-on", "pom"), ("mapped-off", "mapped-on", "pom")):
        result = compare(off, on)
        if result is None:
            continue
        if result[region]["changedShare"] < 0.05:
            failures.append(f"{off} -> {on}: the traced specimen did not change ({result[region]})")
        # A flat control draws through another program variant once parallax
        # is on (OpenGL). NVIDIA keeps it byte-identical; Mesa's llvmpipe
        # rounds a few pixels by one level. More than that is a change.
        for control in ("flat", "wallFlat"):
            if control in result and result[control]["peak"] > 1:
                failures.append(f"{off} -> {on}: flat control {control} changed ({result[control]})")
        on_row = cases[on]
        if int(field(on_row["parallax"], "draws") or 0) <= 0:
            failures.append(f"{on}: no parallax draws reported ({on_row['parallax']})")
        if int(field(cases[off]["parallax"], "draws") or 0) != 0:
            failures.append(f"{off}: parallax draws reported while off ({cases[off]['parallax']})")
    if "retail-off" in cases and "retail-on" in cases:
        result = compare("retail-off", "retail-on")
        if result is not None and report["backend"] == "vk":
            if result["retail"]["changedShare"] < 0.05:
                failures.append(f"retail-on: the Parallaxbump guide's ray changed nothing ({result['retail']})")
            if int(field(cases["retail-on"]["parallax"], "draws") or 0) <= 0:
                failures.append(f"retail-on: no parallax draws reported ({cases['retail-on']['parallax']})")
        elif result is not None and result["retail"]["peak"] != 0:
            # OpenGL has no Parallaxbump program: the stage draws nothing either way.
            failures.append(f"retail-on: OpenGL changed a stage it cannot draw ({result['retail']})")
    mapped = cases.get("mapped-on")
    if mapped is not None and report["backend"] == "gl" and int(field(mapped["glNative"], "mapped") or 0) <= 0:
        failures.append(f"mapped-on: no shadow-mapped PBR receiver drew ({mapped['glNative']})")
    result = compare("off", "off-again")
    if result is not None and not result["identical"]:
        failures.append(f"off-again: toggling parallax off did not restore the exact image ({result['pom']})")
    result = compare("off", "faded")
    if result is not None and result["pom"]["meanAbs"] > 1.0:
        failures.append(f"faded: a fully faded ray must converge on the flat surface ({result['pom']})")
    result = compare("on", "shadows")
    if result is not None:
        if result["pom"]["changedShare"] < 0.01:
            failures.append(f"shadows: the self-shadow ray darkened nothing ({result['pom']})")
        if images.get("shadows") is not None and images.get("on") is not None:
            if region_mean(images["shadows"], REGIONS["grazing"]["pom"]) > region_mean(images["on"], REGIONS["grazing"]["pom"]) + 0.01:
                failures.append("shadows: the self-shadow brightened the specimen")
    compare("samples-min", "samples-max")
    compare("far-off", "far-on")
    strip = cases.get("strip-on")
    if strip is not None:
        decline = field(strip["parallax"], "lastDecline")
        if decline in (None, "none"):
            failures.append(f"strip-on: the cutout and emissive specimens reported no decline ({strip['parallax']})")
        if int(field(strip["parallax"], "flat") or 0) <= 0:
            failures.append(f"strip-on: no heightMap surface drew flat ({strip['parallax']})")
        result = compare("strip-off", "strip-on")
    report["comparisons"] = comparisons
    return failures


def compare_reports(a: dict, b: dict) -> tuple[dict, list[str]]:
    failures: list[str] = []
    out = {}
    for case in sorted(set(a["cases"]) & set(b["cases"])):
        ia, ib = load_rgb(Path(a["cases"][case]["screenshot"])), load_rgb(Path(b["cases"][case]["screenshot"]))
        if ia is None or ib is None:
            failures.append(f"{case}: missing capture")
            continue
        regions = REGIONS[CASES[case][0]]
        stats = {name: region_stats(ia, ib, box) for name, box in regions.items()}
        stats["identical"] = ia == ib
        out[case] = stats
    # Each traced case may differ between backends little more than its flat
    # counterpart does. The allowance is the ray's precision limit: where it
    # grazes a height step either side is a valid hit, and implementations
    # round differently there (one pixel on NVIDIA; 0.27% of the traced
    # specimen between Mesa's llvmpipe and lavapipe).
    pairs = {"on": "off", "shadows": "off", "samples-min": "off", "samples-max": "off",
             "steep-on": "steep-off", "wall-on": "wall-off", "far-on": "far-off",
             "environment-on": "environment-off", "mapped-on": "mapped-off", "strip-on": "strip-off"}
    for on, off in pairs.items():
        if on not in out or off not in out:
            continue
        for region, stats in out[on].items():
            if not isinstance(stats, dict):
                continue
            base = out[off][region]
            if stats["meanAbs"] > base["meanAbs"] + 0.25 or stats["changedShare"] > base["changedShare"] + 0.005:
                failures.append(f"{on}: backends disagree in {region} ({stats} against {off} {out[off][region]})")
    return out, failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-root", type=Path)
    parser.add_argument("--install", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, default=Path(r"E:\SteamLibrary\steamapps\common\Quake 4"))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--backend", choices=("gl", "vk"))
    parser.add_argument("--tier", default="gl45")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--validation", action="store_true", help="Vulkan validation layers (never for timing)")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--compare", nargs=2, type=Path, metavar=("A", "B"))
    parser.add_argument("--timing", action="store_true", help="measure GPU cost instead of capturing")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--size", type=lambda v: tuple(int(x) for x in v.split("x")), default=(2560, 1440),
                        help="timing resolution, WxH (FPS-01 measures 2560x1440)")
    args = parser.parse_args()
    if args.compare:
        a, b = (json.loads(path.read_text()) for path in args.compare)
        stats, failures = compare_reports(a, b)
        print(json.dumps({"cases": stats, "failures": failures}, indent=2))
        return 1 if failures else 0
    if args.runtime_root is None or args.output_dir is None:
        parser.error("--runtime-root and --output-dir are required")
    # The client runs with the runtime as its working directory.
    args.runtime_root = args.runtime_root.resolve()
    args.output_dir = args.output_dir.resolve()
    args.install = args.install.resolve()
    if args.prepare:
        prepare(args.runtime_root, args.install, args.basepath, args.output_dir, args.timeout)
        print(f"Prepared {args.runtime_root}")
        return 0
    if args.backend is None:
        parser.error("--backend is required to capture")
    if args.timing:
        result = timing(args)
        (args.output_dir / "timing.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if result["exitCode"] == 0 else 1
    cases = [case for case in args.cases.split(",") if case]
    unknown = [case for case in cases if case not in CASES]
    if unknown:
        parser.error(f"unknown cases: {', '.join(unknown)}")
    report = capture(args, cases)
    failures = check_report(report)
    report["failures"] = failures
    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"backend": args.backend, "failures": failures}, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
