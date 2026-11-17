#!/usr/bin/env python3
"""Compile and capture the PBR showcase map (tools/validation/generate_pbr_showcase_map.py).

    python tools/tests/renderer_pbr_showcase.py --runtime-root .tmp/runtime/pbr-showcase --output-dir .tmp/pbr-showcase/run-1 --backend gl

The runtime root must already hold the generated map (real downloads or
--synthetic stand-ins). --binaries copies an engine build into it first
(default: the staged .install). Captures use the shipped renderer paths: the
classic OpenGL light loop owning PBR, or Vulkan composing into the
display-referred frame, with shadow maps on every casting light, PCSS-lite
filtering and production environment lighting. Each camera is captured lit
and in the PBR ownership view; every station must be drawn as PBR.

Like the laboratory, it drives the engine only through its own console: no OS
capture or input injection.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools" / "validation"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))
import generate_pbr_fixture as fixture  # noqa: E402
import generate_pbr_showcase_map as showcase  # noqa: E402
import renderer_pbr_laboratory as lab  # noqa: E402

BINARIES = ("openQ4-client_x64.exe", "openQ4-ded_x64.exe", "renderer-gl_x64.dll", "renderer-vk_x64.dll",
            "OpenAL32.dll", "baseoq4/game-sp_x64.dll", "baseoq4/game-mp_x64.dll", "baseoq4/mod.json",
            "baseoq4/pak0.pk4", "baseoq4/pak1.pk4", "baseoq4/pak2.pk4")
SHOWCASE = {
    # Production lighting: environment from the map's own ambient light, every
    # casting light mapped, PCSS-lite projected filtering.
    "r_pbrLinearScene": "0", "r_pbrAnalyticEnvironment": "0", "r_rendererReflectionProbes": "0",
    "r_shadows": "1", "r_useShadowMap": "1", "r_shadowMapPointLights": "1",
    "r_shadowMapFilterMode": "2", "g_showPlayerShadow": "0",
}
CASES = {"lit": {"r_pbrDebug": "0"}, "ownership": {"r_pbrDebug": "7"}}


def copy_binaries(source: Path, runtime: Path) -> None:
    for relative in BINARIES:
        origin = source / relative
        if not origin.is_file():
            if relative == "OpenAL32.dll" and os.name != "nt":
                continue
            raise FileNotFoundError(f"engine build is missing {origin}")
        target = fixture.validate_contained_target(runtime, runtime / relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, target)


def compile_map(args: argparse.Namespace, cvars: dict[str, str]) -> None:
    save = args.output_dir / "compile"
    save.mkdir(parents=True)
    settings = {**cvars, "fs_savepath": str(save), "r_renderApi": "gl", "r_rendererModernExecutor": "0"}
    executable = next(args.runtime_root.glob("openQ4-ded_*"))
    command = [str(executable), *lab.startup_args(settings, save), "+dmap", showcase.MAP, "+quit"]
    code, _ = lab.run(command, args.runtime_root, save / "process.log", args.timeout)
    log = save / "baseoq4/logs/openq4.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    proc = args.runtime_root / "baseoq4" / f"{showcase.MAP}.proc"
    if code or not proc.is_file() or re.search(r"(?i)\b(?:leaked|fatal error|ERROR:)\b", text):
        raise RuntimeError(f"showcase map did not compile cleanly; see {save}")


def tga_pixels(path: Path) -> tuple[int, int, bytes] | None:
    raw = path.read_bytes() if path.is_file() else b""
    if len(raw) < 18:
        return None
    width, height, depth, descriptor = raw[12] | raw[13] << 8, raw[14] | raw[15] << 8, raw[16], raw[17]
    step = depth // 8
    rows = [raw[18 + y * width * step:18 + (y + 1) * width * step] for y in range(height)]
    if not descriptor & 0x20:
        rows.reverse()
    data = bytearray()
    for row in rows:
        for x in range(width):
            b, g, r = row[x * step:x * step + 3]
            data += bytes((r, g, b))
    return width, height, bytes(data)


def green_share(path: Path, box: tuple[float, float, float, float]) -> float:
    """Share of a normalised screen box drawn as owned PBR (the ownership view's green)."""
    decoded = tga_pixels(path)
    if decoded is None:
        return 0.0
    width, height, data = decoded
    x0, y0, x1, y1 = (int(box[0] * width), int(box[1] * height), int(box[2] * width), int(box[3] * height))
    total = owned = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = data[(y * width + x) * 3:(y * width + x) * 3 + 3]
            total += 1
            owned += g > 160 and r < 120 and b < 120
    return owned / max(total, 1)


def capture(args: argparse.Namespace, cvars: dict[str, str], record: dict) -> list[dict]:
    save = args.output_dir / "capture"
    save.mkdir(parents=True)
    cfg = save / "baseoq4/pbr_showcase_capture.cfg"
    cfg.parent.mkdir(parents=True)
    cameras = record["cameras"]
    names = list(cameras) if args.cameras == "all" else args.cameras.split(",")
    commands = ["wait 15", "god", "notarget", "noclip", "g_stopTime 1"]
    shots = []
    for camera in names:
        commands += ["setviewpos " + " ".join(map(str, cameras[camera]))]
        for case, settings in CASES.items():
            name = f"{camera}-{case}"
            commands += [f'set {k} "{v}"' for k, v in settings.items()]
            commands += ["wait 20", f'echo "PBRSHOWCASE_{name}_BEGIN"', "viewpos", "gfxInfo",
                         f'screenshot "screenshots/{name}.tga"', f'echo "PBRSHOWCASE_{name}_END"']
            shots.append((camera, case, name))
    commands += ["wait 5", "quit"]
    lab.write_capture_commands(cfg, commands)
    run_vars = {**cvars, "fs_savepath": str(save), "fs_devpath": str(save),
                "g_autoExecAfterMapLoad": "pbr_showcase_capture.cfg", "g_autoExecAfterMapLoadDelayMs": "500"}
    executable = next(args.runtime_root.glob("openQ4-client_*"))
    command = [str(executable), *lab.startup_args(run_vars, save), "+map", showcase.MAP]
    (save / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    code, elapsed = lab.run(command, args.runtime_root, save / "process.log", args.timeout)
    log = save / "baseoq4/logs/openq4.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    diagnostics = lab.diagnostic_lines(text)
    results = []
    for camera, case, name in shots:
        shot = save / f"baseoq4/screenshots/{name}.tga"
        failures = [] if shot.is_file() else ["screenshot missing"]
        if f"PBRSHOWCASE_{name}_END" not in text:
            failures.append("capture markers missing")
        result = {"camera": camera, "case": case, "screenshot": str(shot)}
        if case == "ownership" and shot.is_file():
            # A station camera centres its sphere; the overview must be mostly PBR.
            box = (0.4, 0.4, 0.6, 0.6) if camera.startswith("station-") else (0.0, 0.0, 1.0, 1.0)
            share = green_share(shot, box)
            result["ownedShare"] = round(share, 4)
            if share < (0.9 if camera.startswith("station-") else 0.5):
                failures.append(f"PBR owns only {share:.0%} of the checked area")
        result["failures"] = failures
        results.append(result)
    if code:
        results.append({"camera": "-", "case": "exit", "failures": [f"exit={code}"]})
    if diagnostics:
        results.append({"camera": "-", "case": "log", "failures": ["engine diagnostics require review"],
                        "diagnostics": diagnostics[:40]})
    (args.output_dir / "report.json").write_text(json.dumps(
        {"backend": args.backend, "elapsed": elapsed, "synthetic": record.get("synthetic"), "results": results},
        indent=2) + "\n", encoding="utf-8")
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--basepath", type=Path)
    parser.add_argument("--backend", choices=("gl", "vk"), default="gl")
    parser.add_argument("--binaries", type=Path, help="engine build to copy in first (for example .install)")
    parser.add_argument("--cameras", default="all", help="comma-separated camera names, or all")
    parser.add_argument("--skip-compile", action="store_true")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    args.runtime_root = fixture.validate_runtime_root(args.runtime_root)
    # The engine runs from the runtime root: every path it is given is absolute.
    args.output_dir = args.output_dir.resolve()
    if args.output_dir.exists():
        parser.error(f"--output-dir already exists: {args.output_dir}")
    args.output_dir.mkdir(parents=True)
    record_path = args.runtime_root / "pbr-showcase.json"
    if not record_path.is_file():
        parser.error("generate the showcase into --runtime-root first (generate_pbr_showcase_map.py)")
    record = json.loads(record_path.read_text(encoding="utf-8"))
    if args.binaries:
        copy_binaries(args.binaries, args.runtime_root)
    if args.basepath is None:
        import performance_suite
        args.basepath = Path(performance_suite.default_basepath())
    cvars = {**lab.BASE, **SHOWCASE, "fs_game": "baseoq4", "fs_basepath": str(args.basepath),
             "fs_devpath": str(args.runtime_root), "r_renderApi": "vulkan" if args.backend == "vk" else "gl"}
    cvars.update(lab.GL_NATIVE_OVERRIDES if args.backend == "gl" else lab.PRODUCTION_OVERRIDES)
    if not args.skip_compile:
        compile_map(args, cvars)
    results = capture(args, cvars, record)
    failed = [r for r in results if r["failures"]]
    for result in results:
        share = f" owned={result['ownedShare']:.0%}" if "ownedShare" in result else ""
        print(f"  {result['camera']:24s} {result['case']:9s}{share} {result['failures'] or 'ok'}")
    print(f"PBR showcase ({args.backend}{', synthetic' if record.get('synthetic') else ''}): "
          f"{len(results) - len(failed)}/{len(results)} passed; report {args.output_dir / 'report.json'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
