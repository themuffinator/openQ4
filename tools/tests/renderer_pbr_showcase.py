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

Authoring bay (REN-05): one specimen at a time is spawned at a fixed place, as
a mod would author it. A skin swapping the PBR base for another PBR material
must change the specimen and stay PBR; a skin swapping it for a classic
material must be drawn classically; one skinned model carrying both must keep
each half's owner, on CPU skinning and (second launch) packed MD5R, which must
match. Cutout: a close view of the fence must show both fence and holes, with
no less fence than its alpha map has, and the light the projector throws
through it must be partly blocked, neither fully (an opaque caster) nor not at
all.
--reference <report.json> then compares this backend with another's report.

Decals and probes (REN-06): an authored decal quad must mark the PBR receiver
under it. A third launch turns authored probes on: the room's box probe is
captured in the engine (bakeReflectionProbes) and must change the stations,
survive an image reload, follow a second capture taken with the key lights off
and return with a third. A fourth launch places the specimen and a probe in a
stock map (STOCK_SCENE): the captured probe must light the specimen and leave
the stock pixels away from it alone.

Like the laboratory, it drives the engine only through its own console: no OS
capture or input injection.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools" / "validation"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))
import generate_pbr_fixture as fixture  # noqa: E402
import generate_pbr_showcase_map as showcase  # noqa: E402
import renderer_pbr_laboratory as lab  # noqa: E402
from physics_workloads import wait_for_memory  # noqa: E402

BINARIES = ("openQ4-client_x64.exe", "openQ4-ded_x64.exe", "renderer-gl_x64.dll", "renderer-vk_x64.dll",
            "OpenAL32.dll", "baseoq4/game_x64.dll", "baseoq4/mod.json",
            "baseoq4/pak0.pk4", "baseoq4/pak1.pk4", "baseoq4/pak2.pk4")
SHOWCASE = {
    # Production lighting: environment from the map's own ambient light, every
    # casting light mapped, PCSS-lite projected filtering.
    "r_pbrLinearScene": "0", "r_pbrAnalyticEnvironment": "0", "r_rendererReflectionProbes": "0",
    "r_shadows": "1", "r_useShadowMap": "1", "r_shadowMapPointLights": "1",
    "r_shadowMapFilterMode": "2", "g_showPlayerShadow": "0",
}
CASES = {"lit": {"r_pbrDebug": "0"}, "ownership": {"r_pbrDebug": "7"}}
ALBEDO = {"r_pbrDebug": "1"}
# The laboratory pins g_fov 90: 4:3 vertical field, so half the image height is tan = 0.75.
HALF_HEIGHT_TAN = 0.75
SKIN_MIN_CHANGE = 1.0          # mean absolute levels inside the specimen's disc (an unapplied skin gives 0)
MD5R_MAX_DIFFERENCE = 0.5      # packed MD5R against CPU skinning, same measure
# The close view minifies the fence texture, and alpha-tested coverage grows
# with each mip (averaged alpha passes the test where holes were), so the
# alpha map's own coverage is the lower bound, not the expectation: the view
# must show both fence and holes, and no less fence than the texture has.
COVERAGE_SLACK = 0.05
COVERAGE_MAX = 0.97
TRANSMISSION_RANGE = (0.1, 0.9)
MIN_PROJECTOR_LEVELS = 6.0     # the projector must light the checked floor at all
REFERENCE_COVERAGE = 0.02      # backend against backend
REFERENCE_TRANSMISSION = 0.1
DECAL_SHARE = (0.005, 0.25)    # share of the decal camera's view an authored decal may change
DECAL_MIN_LEVELS = 8.0         # and by how much, on the pixels it changes
BAKE_SIZE = 128
PROBE_MIN_CHANGE = 2.0         # mean levels inside a specimen's disc, probes on against off
PROBE_RESTORE_LEVELS = 0.5     # whole-frame mean after an image reload or a repeated capture
PROBE_REBAKE_LEVELS = 0.3      # whole-frame mean a capture with the key lights off must reach
# Captured cube faces, backend against backend: the median pixel difference.
# A stock scene keeps its live effects, so two launches catch different
# explosions in a face or two; a flipped, rotated or swapped face moves most
# pixels, an explosion a minority.
REFERENCE_FACE_MEDIAN = 6.0
# The specimen floats ahead of and above the eye, clear of the terrain.
STOCK_SCENE = {"map": "game/airdefense1", "pose": (10200, -6800, 40, 0, 170, 0), "distance": 220, "lift": 60}
STOCK_CHANGED_SHARE = 0.001    # stock pixels away from the specimen that probes may move by over 2 levels
PROBE_TELEMETRY = ("Vulkan PBR probes:", "Modern specular probe atlas:", "Modern clustered specular probes:")


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


def image(path: Path) -> np.ndarray | None:
    """Engine TGA as H x W x 3 RGB, top row first."""
    raw = path.read_bytes() if path.is_file() else b""
    if len(raw) < 18 or raw[2] != 2:
        return None
    width, height, step = raw[12] | raw[13] << 8, raw[14] | raw[15] << 8, raw[16] // 8
    start = 18 + raw[0]
    if step not in (3, 4) or len(raw) < start + width * height * step:
        return None
    data = np.frombuffer(raw, dtype=np.uint8, count=width * height * step, offset=start).reshape(height, width, step)
    if not raw[17] & 0x20:
        data = data[::-1]
    return data[..., 2::-1].astype(np.int16)


def green(data: np.ndarray) -> np.ndarray:
    return (data[..., 1] > 160) & (data[..., 0] < 120) & (data[..., 2] < 120)


def disc(shape: tuple, camera: list, origin: list, radius: float, scale: float = 0.85) -> tuple[np.ndarray, float]:
    """Pixels inside a sphere's silhouette for a camera looking along +Y, and the silhouette's centre row."""
    height, width = shape[:2]
    per_tan = (height / 2) / HALF_HEIGHT_TAN
    distance = origin[1] - camera[1]
    cx = width / 2 + (origin[0] - camera[0]) / distance * per_tan
    cy = height / 2 - (origin[2] - camera[2]) / distance * per_tan
    r = math.tan(math.asin(radius / distance)) * per_tan * scale
    y, x = np.mgrid[0:height, 0:width]
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r, cy


def shot_commands(name: str, settings: dict[str, str], extra: tuple[str, ...] = ()) -> list[str]:
    return [*(f'set {k} "{v}"' for k, v in settings.items()), "wait 20", f'echo "PBRSHOWCASE_{name}_BEGIN"',
            "viewpos", "gfxInfo", *extra, f'screenshot "screenshots/{name}.tga"', f'echo "PBRSHOWCASE_{name}_END"']


def authoring_commands(bay: dict, mixed_only: bool) -> tuple[list[str], list[tuple[str, str, str]]]:
    origin = " ".join(map(str, bay["origin"]))
    remove = ["g_stopTime 0", 'script "$sc_authoring.hide(); $sc_authoring.remove()"', "wait 5"]
    static = f'spawn func_static name sc_authoring model "{bay["model"]}" origin "{origin}" solid 0'
    specimens = [("mixed", f'spawn func_animate name sc_authoring model "{bay["mixedModel"]}" anim idle '
                           f'origin "{origin}" angle 0 solid 0')]
    if not mixed_only:
        specimens = [("base", static), ("skin-pbr", f'{static} skin {bay["skins"]["pbr"]}'),
                     ("skin-classic", f'{static} skin {bay["skins"]["classic"]}')] + specimens
    commands = ["setviewpos " + " ".join(map(str, bay["camera"]))]
    shots = []
    if not mixed_only:
        commands += shot_commands("authoring-empty", CASES["lit"])
        shots.append(("authoring", "empty", "authoring-empty"))
    for index, (name, spawn) in enumerate(specimens):
        commands += (remove if index else ["g_stopTime 0"]) + [spawn, "wait 30", "g_stopTime 1"]
        for case, settings in CASES.items():
            shot = f"authoring-{name}" + ("" if case == "lit" else "-ownership")
            extra = ("printModel " + showcase.MIXED_MESH,) if name == "mixed" and case == "lit" else ()
            commands += shot_commands(shot, settings, extra)
            shots.append(("authoring", name + ("" if case == "lit" else "-ownership"), shot))
    return commands + remove + ["g_stopTime 1"], shots


def cutout_commands(cutout: dict) -> tuple[list[str], list[tuple[str, str, str]]]:
    close = "setviewpos " + " ".join(map(str, cutout["closeCamera"]))
    shadow = "setviewpos " + " ".join(map(str, cutout["shadowCamera"]))
    entity, projector = cutout["entity"], cutout["projector"]

    def scene(*script: str) -> list[str]:
        return ["g_stopTime 0", *(f'script "{line}"' for line in script), "wait 30", "g_stopTime 1"]

    commands = [close, *shot_commands("cutout-albedo-fence", ALBEDO),
                *shot_commands("cutout-ownership-fence", CASES["ownership"]),
                shadow, *shot_commands("cutout-shadow-fence", CASES["lit"]),
                *scene(f"${projector}.Off()"), *shot_commands("cutout-shadow-fence-dark", CASES["lit"]),
                *scene(f"${entity}.hide(); ${entity}.remove()"), *shot_commands("cutout-shadow-open-dark", CASES["lit"]),
                *scene(f"${projector}.On()"), *shot_commands("cutout-shadow-open", CASES["lit"]),
                close, *shot_commands("cutout-albedo-open", ALBEDO)]
    names = ("albedo-fence", "ownership-fence", "shadow-fence", "shadow-fence-dark", "shadow-open-dark",
             "shadow-open", "albedo-open")
    return commands, [("cutout", name, "cutout-" + name) for name in names]


def check_authoring(bay: dict, images: dict, by_name: dict, text: str, mixed_only: bool) -> None:
    sample = next((data for data in images.values() if data is not None), None)
    if sample is None:
        return
    mask, _ = disc(sample.shape, bay["camera"], bay["origin"], bay["radius"])

    def change(a: str, b: str) -> float | None:
        first, second = images.get(a), images.get(b)
        return None if first is None or second is None else float(np.abs(first - second)[mask].mean())

    if not mixed_only:
        present = change("authoring-empty", "authoring-base")
        by_name["authoring-base"]["specimenChange"] = None if present is None else round(present, 3)
        if present is None or present < SKIN_MIN_CHANGE:
            by_name["authoring-base"]["failures"].append("the authoring specimen did not appear")
        for name, pbr in (("base", True), ("skin-pbr", True), ("skin-classic", False)):
            data = images.get(f"authoring-{name}-ownership")
            if data is None:
                continue
            owned = float(green(data)[mask].mean())
            result = by_name[f"authoring-{name}-ownership"]
            result["ownedShare"] = round(owned, 4)
            if pbr and owned < 0.98:
                result["failures"].append(f"PBR owns only {owned:.0%} of the specimen")
            if not pbr and owned > 0.02:
                result["failures"].append(f"the classic skin substitute was drawn as PBR on {owned:.0%} of the specimen")
        for name in ("skin-pbr", "skin-classic"):
            moved = change("authoring-base", f"authoring-{name}")
            by_name[f"authoring-{name}"]["skinChange"] = None if moved is None else round(moved, 3)
            if moved is not None and moved < SKIN_MIN_CHANGE:
                by_name[f"authoring-{name}"]["failures"].append("the skin did not change the specimen")
    # One model, two materials: PBR above the equator, classic below. The
    # margin keeps clear of the equator, which the camera sees edge-on.
    data = images.get("authoring-mixed-ownership")
    if data is not None:
        inside, centre = disc(data.shape, bay["camera"], bay["origin"], bay["mixedRadius"])
        rows = np.arange(data.shape[0])[:, None]
        per_tan = (data.shape[0] / 2) / HALF_HEIGHT_TAN
        margin = 0.15 * bay["mixedRadius"] / (bay["origin"][1] - bay["camera"][1]) * per_tan
        upper = float(green(data)[inside & (rows < centre - margin)].mean())
        lower = float(green(data)[inside & (rows > centre + margin)].mean())
        result = by_name["authoring-mixed-ownership"]
        result["mixedOwnership"] = {"upper": round(upper, 4), "lower": round(lower, 4)}
        if upper < 0.98:
            result["failures"].append(f"the PBR half is PBR on only {upper:.0%}")
        if lower > 0.02:
            result["failures"].append(f"the classic half was drawn as PBR on {lower:.0%}")
    if mixed_only and "MD5R model." not in text:
        by_name["authoring-mixed"]["failures"].append("the mixed model was not converted to MD5R")


def check_cutout(cutout: dict, images: dict, by_name: dict) -> dict:
    proof: dict = {"expectedCoverage": cutout["coverage"]}
    fence, opened = images.get("cutout-albedo-fence"), images.get("cutout-albedo-open")
    if fence is not None and opened is not None:
        coverage = float((np.abs(fence - opened).max(axis=2) > 12).mean())
        proof["coverage"] = round(coverage, 4)
        if coverage < cutout["coverage"] - COVERAGE_SLACK:
            by_name["cutout-albedo-fence"]["failures"].append(
                f"the fence covers {coverage:.1%} of the close view; its alpha map covers {cutout['coverage']:.1%}")
        if coverage > COVERAGE_MAX:
            by_name["cutout-albedo-fence"]["failures"].append(
                f"the fence covers {coverage:.1%} of the close view: its cutout holes are not drawn")
    owner = images.get("cutout-ownership-fence")
    if owner is not None:
        owned = float(green(owner).mean())
        by_name["cutout-ownership-fence"]["ownedShare"] = round(owned, 4)
        if owned < 0.98:
            by_name["cutout-ownership-fence"]["failures"].append(f"PBR owns only {owned:.0%} of the fence view")
    means = {}
    for name in ("shadow-fence", "shadow-fence-dark", "shadow-open-dark", "shadow-open"):
        data = images.get("cutout-" + name)
        if data is None:
            return proof
        height, width = data.shape[:2]
        box = data[int(0.35 * height):int(0.75 * height), int(0.3 * width):int(0.7 * width)]
        means[name] = float(box.mean())
    through = means["shadow-fence"] - means["shadow-fence-dark"]
    full = means["shadow-open"] - means["shadow-open-dark"]
    proof.update(projectorLevels=round(full, 2), throughFenceLevels=round(through, 2))
    result = by_name["cutout-shadow-fence"]
    if full < MIN_PROJECTOR_LEVELS:
        result["failures"].append(f"the projector adds only {full:.1f} levels to the checked floor")
    else:
        transmission = through / full
        proof["transmission"] = round(transmission, 4)
        low, high = TRANSMISSION_RANGE
        if transmission < low:
            result["failures"].append(
                f"the cutout fence blocks the projector like an opaque caster ({transmission:.0%} passes)")
        if transmission > high:
            result["failures"].append(f"the cutout fence casts no shadow ({transmission:.0%} passes)")
    return proof


def capture(args: argparse.Namespace, cvars: dict[str, str], record: dict, md5r: bool = False) -> tuple[list[dict], dict]:
    save = args.output_dir / ("capture-md5r" if md5r else "capture")
    save.mkdir(parents=True)
    seed_probe_faces(save, record)
    cameras = record["cameras"]
    names = [] if md5r else list(cameras) if args.cameras == "all" else [n for n in args.cameras.split(",") if n]
    commands = ["wait 15", "god", "notarget", "noclip", "g_stopTime 1"]
    shots = []
    for camera in names:
        commands += ["setviewpos " + " ".join(map(str, cameras[camera]))]
        for case, settings in CASES.items():
            name = f"{camera}-{case}"
            commands += shot_commands(name, settings)
            shots.append((camera, case, name))
    bay, cutout = record.get("authoring"), record.get("cutout")
    if bay and not args.skip_authoring:
        more, added = authoring_commands(bay, md5r)
        commands += more
        shots += added
    decals = record.get("decals")
    if decals and not md5r and not args.skip_authoring:
        more, added = decal_commands(decals)
        commands += more
        shots += added
    if cutout and not md5r and not args.skip_authoring:
        # Last: it removes the fence.
        more, added = cutout_commands(cutout)
        commands += more
        shots += added
    commands += ["wait 5", "quit"]
    code, elapsed, text = launch(args, cvars, save, commands, ("+map", showcase.MAP),
                                 {"r_convertMD5toMD5R": "1"} if md5r else None)
    diagnostics = lab.diagnostic_lines(text)
    results = []
    images = {}
    suffix = "-md5r" if md5r else ""
    for camera, case, name in shots:
        shot = save / f"baseoq4/screenshots/{name}.tga"
        failures = [] if shot.is_file() else ["screenshot missing"]
        if f"PBRSHOWCASE_{name}_END" not in text:
            failures.append("capture markers missing")
        result = {"camera": camera + suffix, "case": case, "screenshot": str(shot)}
        if camera in ("authoring", "cutout", "decal"):
            images[name] = image(shot)
        elif case == "ownership" and shot.is_file():
            # A station camera centres its sphere; the overview must be mostly PBR.
            box = (0.4, 0.4, 0.6, 0.6) if camera.startswith("station-") else (0.0, 0.0, 1.0, 1.0)
            share = green_share(shot, box)
            result["ownedShare"] = round(share, 4)
            if share < (0.9 if camera.startswith("station-") else 0.5):
                failures.append(f"PBR owns only {share:.0%} of the checked area")
        result["failures"] = failures
        results.append(result)
    by_name = {name: result for (_, _, name), result in zip(shots, results)}
    proof: dict = {}
    if any(name.startswith("authoring-") for name in by_name):
        check_authoring(bay, images, by_name, text, md5r)
    if any(name.startswith("decal-") for name in by_name):
        proof["decals"] = check_decals(decals, images, by_name)
    if "cutout-albedo-fence" in by_name:
        proof["cutout"] = check_cutout(cutout, images, by_name)
    if code:
        results.append({"camera": "-", "case": "exit" + suffix, "failures": [f"exit={code}"]})
    if diagnostics:
        results.append({"camera": "-", "case": "log" + suffix, "failures": ["engine diagnostics require review"],
                        "diagnostics": diagnostics[:40]})
    proof["elapsed"] = elapsed
    return results, proof


def seed_probe_faces(save: Path, record: dict) -> None:
    """Placeholder cube faces in the run's save path.

    The map precaches its probe material, and bakeReflectionProbes writes the
    captured faces over the placeholders there. openQ4 searches the save path
    last, so a copy in the runtime would load instead of the capture.
    """
    probes = record.get("probes")
    if not probes:
        return
    face = bytes((0, 0, 2)) + bytes(9) + struct.pack("<HHBB", 16, 16, 24, 0x20) + bytes((40, 40, 40)) * 256
    for probe in (probes["room"], probes["stock"]):
        for suffix in probes["faces"]:
            target = save / f"baseoq4/{probe['cubeMap']}_{suffix}.tga"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(face)


def run_guarded(command: list[str], cwd: Path, output: Path, timeout: int, limit_gb: float) -> tuple[int, float, float]:
    """lab.run with a private-memory ceiling: a runaway client must not exhaust a shared machine."""
    try:
        import psutil
    except ImportError:
        code, elapsed = lab.run(command, cwd, output, timeout)
        return code, elapsed, 0.0
    start, peak = time.monotonic(), 0.0
    with output.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        handle = psutil.Process(process.pid)
        code = None
        while code is None:
            try:
                code = process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    peak = max(peak, handle.memory_info().private / 2 ** 30)
                except (psutil.Error, AttributeError):
                    pass
                over_time = time.monotonic() - start > timeout
                if over_time or (limit_gb and peak > limit_gb):
                    process.kill()
                    process.wait(timeout=10)
                    print(f"Engine stopped: {'timed out' if over_time else f'{peak:.1f} GB private memory'}; see {output}", flush=True)
                    code = 124 if over_time else 125
    return code, time.monotonic() - start, peak


def launch(args: argparse.Namespace, cvars: dict[str, str], save: Path, commands: list[str],
           map_command: tuple[str, str], extra: dict[str, str] | None = None) -> tuple[int, float, str]:
    cfg = save / "baseoq4/pbr_showcase_capture.cfg"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    lab.write_capture_commands(cfg, commands)
    run_vars = {**cvars, "fs_savepath": str(save), "fs_devpath": str(save),
                "g_autoExecAfterMapLoad": "pbr_showcase_capture.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
                **(extra or {})}
    executable = next(args.runtime_root.glob("openQ4-client_*"))
    command = [str(executable), *lab.startup_args(run_vars, save), *map_command]
    (save / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    wait_for_memory(args.min_free_gb)
    code, elapsed, peak = run_guarded(command, args.runtime_root, save / "process.log", args.timeout, args.max_private_gb)
    print(f"  launch {save.name}: exit {code}, {elapsed:.0f} s, peak private {peak:.1f} GB", flush=True)
    log = save / "baseoq4/logs/openq4.log"
    return code, elapsed, log.read_text(errors="replace") if log.exists() else ""


def collect(save: Path, shots: list[tuple[str, str, str]], text: str) -> tuple[list[dict], dict, dict]:
    """One result per shot, the results by shot name, and the decoded images."""
    results, images = [], {}
    for camera, case, name in shots:
        shot = save / f"baseoq4/screenshots/{name}.tga"
        failures = [] if shot.is_file() else ["screenshot missing"]
        if f"PBRSHOWCASE_{name}_END" not in text:
            failures.append("capture markers missing")
        results.append({"camera": camera, "case": case, "screenshot": str(shot), "failures": failures})
        images[name] = image(shot)
    return results, {name: result for (_, _, name), result in zip(shots, results)}, images


def telemetry(text: str, name: str) -> list[str]:
    """The probe resource lines gfxInfo printed for one shot."""
    begin = text.find(f"PBRSHOWCASE_{name}_BEGIN")
    end = text.find(f"PBRSHOWCASE_{name}_END", begin)
    section = text[begin:end] if 0 <= begin < end else ""
    return [line.strip() for line in section.splitlines() if line.strip().startswith(PROBE_TELEMETRY)]


def baked_faces(save: Path, cube: str, faces: list[str]) -> dict:
    """Size and hash of each captured face: placeholders are 16 pixels, captures BAKE_SIZE."""
    found = {}
    for suffix in faces:
        path = save / f"baseoq4/{cube}_{suffix}.tga"
        raw = path.read_bytes() if path.is_file() else b""
        found[suffix] = {"path": str(path), "size": raw[12] | raw[13] << 8 if len(raw) > 18 else 0,
                         "sha256": hashlib.sha256(raw).hexdigest()}
    return found


def mean_difference(a: np.ndarray | None, b: np.ndarray | None, mask: np.ndarray | None = None) -> float | None:
    if a is None or b is None or a.shape != b.shape:
        return None
    delta = np.abs(a - b)
    return float((delta if mask is None else delta[mask]).mean())


def decal_commands(decals: dict) -> tuple[list[str], list[tuple[str, str, str]]]:
    commands, shots = [], []
    for name, decal in decals.items():
        entity = decal["entity"]
        commands += ["setviewpos " + " ".join(map(str, decal["camera"])), *shot_commands(f"decal-{name}", CASES["lit"]),
                     "g_stopTime 0", f'script "${entity}.hide(); ${entity}.remove()"', "wait 30", "g_stopTime 1",
                     *shot_commands(f"decal-{name}-open", CASES["lit"])]
        shots += [("decal", name, f"decal-{name}"), ("decal", name + "-open", f"decal-{name}-open")]
    return commands, shots


def check_decals(decals: dict, images: dict, by_name: dict) -> dict:
    """An authored decal must mark the PBR receiver under it, and only a decal-sized part of the view."""
    proof = {}
    for name, decal in decals.items():
        marked, bare = images.get(f"decal-{name}"), images.get(f"decal-{name}-open")
        if marked is None or bare is None:
            continue
        delta = np.abs(marked - bare).max(axis=2)
        changed = delta > 6
        share = float(changed.mean())
        strength = float(delta[changed].mean()) if changed.any() else 0.0
        proof[name] = {"blend": decal["blend"], "changedShare": round(share, 4), "meanLevels": round(strength, 2)}
        low, high = DECAL_SHARE
        if not low <= share <= high:
            by_name[f"decal-{name}"]["failures"].append(f"the {decal['blend']} decal changes {share:.1%} of its view")
        elif strength < DECAL_MIN_LEVELS:
            by_name[f"decal-{name}"]["failures"].append(f"the {decal['blend']} decal changes its pixels by only {strength:.1f} levels")
    return proof


def capture_probes(args: argparse.Namespace, cvars: dict[str, str], record: dict, main_results: list[dict]) -> tuple[list[dict], dict]:
    """The room probe, captured in the engine, on the showcase's own lighting (REN-06).

    Probes on must change the stations against the probes-off launch; an image
    reload must restore the frame; a capture taken with the key lights off must
    change the reflections (a new source generation reaches the atlas), and a
    third capture with them on again must restore the first frame.
    """
    save = args.output_dir / "capture-probes"
    save.mkdir(parents=True)
    seed_probe_faces(save, record)
    probes, cameras = record["probes"], record["cameras"]
    names = ["overview"] + [name for name in cameras if name.startswith("station-")]
    bake = ["wait 10", f"bakeReflectionProbes {BAKE_SIZE}", "wait 30", "g_stopTime 1", "pbrProbeSettle", "wait 2"]
    off = "; ".join(f"${light}.Off()" for light in probes["lights"])
    on = "; ".join(f"${light}.On()" for light in probes["lights"])
    commands = ["wait 15", "god", "notarget", "noclip", *bake]
    shots = []
    for camera in names:
        commands += ["setviewpos " + " ".join(map(str, cameras[camera])), *shot_commands(f"probes-{camera}", CASES["lit"])]
        shots.append(("probes", camera, f"probes-{camera}"))
    commands += ["setviewpos " + " ".join(map(str, cameras["overview"])),
                 "reloadImages all", "wait 30", "pbrProbeSettle", "wait 2", *shot_commands("probes-reloaded", CASES["lit"]),
                 "g_stopTime 0", f'script "{off}"', "wait 10", f"bakeReflectionProbes {BAKE_SIZE}", "wait 5",
                 f'script "{on}"', "wait 30", "g_stopTime 1", "pbrProbeSettle", "wait 2",
                 *shot_commands("probes-darkbake", CASES["lit"]),
                 "g_stopTime 0", *bake, *shot_commands("probes-rebaked", CASES["lit"]), "wait 5", "quit"]
    shots += [("probes", name, "probes-" + name) for name in ("reloaded", "darkbake", "rebaked")]
    code, elapsed, text = launch(args, cvars, save, commands, ("+map", showcase.MAP), {"r_rendererReflectionProbes": "1"})
    results, by_name, images = collect(save, shots, text)
    proof: dict = {"elapsed": elapsed, "bakeSize": BAKE_SIZE, "change": {},
                   "telemetry": {name: telemetry(text, "probes-" + name) for name in ("overview", "darkbake", "rebaked")},
                   "faces": baked_faces(save, probes["room"]["cubeMap"], probes["faces"])}
    for suffix, face in proof["faces"].items():
        if face["size"] != BAKE_SIZE:
            by_name["probes-overview"]["failures"].append(f"face {suffix} was not captured ({face['size']} pixels)")
    if not proof["telemetry"]["overview"]:
        by_name["probes-overview"]["failures"].append("no probe resource telemetry in gfxInfo")
    unlit = {(r["camera"], r["case"]): r for r in main_results}
    for camera in names:
        base = unlit.get((camera, "lit"))
        lit = images.get(f"probes-{camera}")
        if not base or lit is None:
            continue
        mask = None
        if camera.startswith("station-"):
            station = next(s for s in record["stations"] if "station-" + s["id"] == camera)
            mask, _ = disc(lit.shape, cameras[camera], station["sphere"], showcase.SPHERE_RADIUS)
        change = mean_difference(lit, image(Path(base["screenshot"])), mask)
        if change is not None:
            proof["change"][camera] = round(change, 3)
            by_name[f"probes-{camera}"]["probeChange"] = round(change, 3)
    stations = [value for name, value in proof["change"].items() if name.startswith("station-")]
    if not stations or max(stations) < PROBE_MIN_CHANGE:
        by_name["probes-overview"]["failures"].append("the probe changed no station against the probes-off launch")
    first = images.get("probes-overview")
    for name, limit, must_change in (("reloaded", PROBE_RESTORE_LEVELS, False), ("darkbake", PROBE_REBAKE_LEVELS, True),
                                     ("rebaked", PROBE_RESTORE_LEVELS, False)):
        difference = mean_difference(first, images.get("probes-" + name))
        if difference is None:
            continue
        by_name["probes-" + name]["againstFirstCapture"] = round(difference, 3)
        proof[name] = round(difference, 3)
        if must_change and difference < limit:
            by_name["probes-" + name]["failures"].append(
                f"a capture with the key lights off changed the frame by only {difference:.2f} levels")
        if not must_change and difference > limit:
            by_name["probes-" + name]["failures"].append(f"the frame is {difference:.2f} levels from the first capture")
    return finish(results, code, text, "-probes"), proof


def capture_stock(args: argparse.Namespace, cvars: dict[str, str], record: dict) -> tuple[list[dict], dict]:
    """A probe and a PBR specimen placed in a stock map (REN-06).

    The probe is captured from the stock scene and must light the specimen;
    probe lights are metadata, so stock pixels away from the specimen must not
    change when probes turn on.
    """
    save = args.output_dir / "capture-stock"
    save.mkdir(parents=True)
    seed_probe_faces(save, record)
    bay, probes = record["authoring"], record["probes"]
    pose, distance, lift = STOCK_SCENE["pose"], STOCK_SCENE["distance"], STOCK_SCENE["lift"]
    yaw = math.radians(pose[4])
    origin = " ".join(f"{value:g}" for value in (pose[0] + distance * math.cos(yaw), pose[1] + distance * math.sin(yaw), pose[2] + lift))
    material = probes["stock"]["material"]
    probe_on, probe_off = {"r_rendererReflectionProbes": "1", "r_pbrDebug": "0"}, {"r_rendererReflectionProbes": "0", "r_pbrDebug": "0"}
    commands = ["wait 15", "god", "notarget", "noclip", "setviewpos " + " ".join(map(str, pose)), "wait 60",
                f'spawn func_static name sc_stock model "{bay["model"]}" origin "{origin}" solid 0',
                f'spawn light name sc_stock_probe texture "{material}" origin "{origin}" light_radius "400 400 400" '
                '_color "1 1 1" noshadows 1',
                "wait 10", f"bakeReflectionProbes {BAKE_SIZE}", "wait 30", "g_stopTime 1", "pbrProbeSettle", "wait 2",
                *shot_commands("stock-probes", probe_on), *shot_commands("stock-noprobes", probe_off),
                *shot_commands("stock-probes-again", probe_on),
                *shot_commands("stock-ownership", {"r_rendererReflectionProbes": "1", "r_pbrDebug": "7"}),
                "g_stopTime 0", 'script "$sc_stock.hide(); $sc_stock.remove()"', "wait 30", "g_stopTime 1",
                *shot_commands("stock-empty", probe_on), "wait 5", "quit"]
    shots = [("stock", name, "stock-" + name) for name in ("probes", "noprobes", "probes-again", "ownership", "empty")]
    code, elapsed, text = launch(args, cvars, save, commands, ("+devmap", STOCK_SCENE["map"]), {"r_rendererReflectionProbes": "1"})
    results, by_name, images = collect(save, shots, text)
    proof: dict = {"elapsed": elapsed, "map": STOCK_SCENE["map"], "pose": list(pose), "specimen": origin,
                   "telemetry": telemetry(text, "stock-probes"),
                   "faces": baked_faces(save, probes["stock"]["cubeMap"], probes["faces"])}
    for suffix, face in proof["faces"].items():
        if face["size"] != BAKE_SIZE:
            by_name["stock-probes"]["failures"].append(f"face {suffix} was not captured ({face['size']} pixels)")
    lit, unlit, again = images.get("stock-probes"), images.get("stock-noprobes"), images.get("stock-probes-again")
    if lit is not None and unlit is not None and again is not None:
        ahead = ([0, 0, 0], [0, distance, lift])
        mask, _ = disc(lit.shape, *ahead, bay["radius"])
        guard, _ = disc(lit.shape, *ahead, bay["radius"], scale=1.3)
        frozen = np.abs(lit - again).max(axis=2) == 0
        reflected = mean_difference(lit, unlit, mask)
        away = ~guard & frozen
        delta = np.abs(lit - unlit).max(axis=2)[away]
        proof.update(specimenChange=round(reflected, 3), unfrozenShare=round(float(1 - frozen.mean()), 5),
                     stockPixelsChanged=round(float((delta > 0).mean()), 5),
                     stockPixelsOver2Levels=round(float((delta > 2).mean()), 5))
        if reflected < PROBE_MIN_CHANGE:
            by_name["stock-probes"]["failures"].append(f"the captured probe changes the specimen by only {reflected:.2f} levels")
        if proof["stockPixelsOver2Levels"] > STOCK_CHANGED_SHARE:
            by_name["stock-noprobes"]["failures"].append(
                f"probes changed {proof['stockPixelsOver2Levels']:.2%} of the stock pixels away from the specimen")
        if proof["unfrozenShare"] > 0.01:
            by_name["stock-probes-again"]["failures"].append(f"{proof['unfrozenShare']:.1%} of the frozen scene still moves")
        appeared = mean_difference(lit, images.get("stock-empty"), mask)
        if appeared is not None:
            proof["specimenPresence"] = round(appeared, 3)
            if appeared < PROBE_MIN_CHANGE:
                by_name["stock-empty"]["failures"].append("the specimen is not visible at the stock pose")
        owner = images.get("stock-ownership")
        if owner is not None:
            owned = float(green(owner)[mask].mean())
            by_name["stock-ownership"]["ownedShare"] = round(owned, 4)
            if owned < 0.98:
                by_name["stock-ownership"]["failures"].append(f"PBR owns only {owned:.0%} of the specimen")
    # A stock map logs warnings of its own, and it cannot precache the
    # specimen and probe it never named. Only renderer faults and anything else
    # naming the showcase content fail this launch; the rest are counted.
    diagnostics = [line for line in lab.diagnostic_lines(text) if "Loading non pre-cached" not in line]
    ours = [line for line in diagnostics if re.search(r"(?i)pbr_showcase|ERROR:|VUID-|GL_INVALID|shader compile failed", line)]
    proof["stockMapDiagnostics"] = len(diagnostics) - len(ours)
    if code:
        results.append({"camera": "-", "case": "exit-stock", "failures": [f"exit={code}"]})
    if ours:
        results.append({"camera": "-", "case": "log-stock", "failures": ["engine diagnostics require review"],
                        "diagnostics": ours[:40]})
    return results, proof


def finish(results: list[dict], code: int, text: str, suffix: str) -> list[dict]:
    diagnostics = lab.diagnostic_lines(text)
    if code:
        results.append({"camera": "-", "case": "exit" + suffix, "failures": [f"exit={code}"]})
    if diagnostics:
        results.append({"camera": "-", "case": "log" + suffix, "failures": ["engine diagnostics require review"],
                        "diagnostics": diagnostics[:40]})
    return results


def compare_faces(ours: dict, theirs: dict) -> tuple[dict, float | None]:
    """Captured cube faces of this backend against another's: one scene, one convention."""
    per_face = {}
    for suffix, face in ours.items():
        other = theirs.get(suffix)
        a, b = image(Path(face["path"])), image(Path(other["path"])) if other else None
        if a is not None and b is not None and a.shape == b.shape:
            delta = np.abs(a - b)
            per_face[suffix] = {"mean": round(float(delta.mean()), 3), "median": float(np.median(delta.max(axis=2)))}
    return per_face, max(face["median"] for face in per_face.values()) if per_face else None


def compare_md5r(bay: dict, results: list[dict]) -> None:
    """Packed MD5R must draw the mixed model like CPU skinning."""
    by_key = {(r["camera"], r["case"]): r for r in results}
    for case in ("mixed", "mixed-ownership"):
        cpu, packed = by_key.get(("authoring", case)), by_key.get(("authoring-md5r", case))
        if not cpu or not packed:
            continue
        a, b = image(Path(cpu["screenshot"])), image(Path(packed["screenshot"]))
        if a is None or b is None:
            continue
        mask, _ = disc(a.shape, bay["camera"], bay["origin"], bay["mixedRadius"], scale=1.1)
        difference = float(np.abs(a - b)[mask].mean())
        packed["md5rDifference"] = round(difference, 3)
        if difference > MD5R_MAX_DIFFERENCE:
            packed["failures"].append(f"packed MD5R draws the mixed model {difference:.2f} levels from CPU skinning")


def compare_reference(report: dict, reference: dict) -> dict:
    """This backend against another's report: cutout numbers are gated, image differences are recorded."""
    comparison: dict = {"reference": reference.get("backend"), "failures": [], "images": {}}
    ours, theirs = report.get("cutout", {}), reference.get("cutout", {})
    for key, limit in (("coverage", REFERENCE_COVERAGE), ("transmission", REFERENCE_TRANSMISSION)):
        if key in ours and key in theirs:
            comparison[key] = {"this": ours[key], "reference": theirs[key]}
            if abs(ours[key] - theirs[key]) > limit:
                comparison["failures"].append(
                    f"cutout {key} differs between backends: {ours[key]} against {theirs[key]}")
    for scene in ("probes", "stock"):
        if "faces" in report.get(scene, {}) and "faces" in reference.get(scene, {}):
            per_face, worst = compare_faces(report[scene]["faces"], reference[scene]["faces"])
            comparison[scene + "Faces"] = per_face
            if worst is None or len(per_face) != len(report[scene]["faces"]) or worst > REFERENCE_FACE_MEDIAN:
                comparison["failures"].append(f"{scene} probe faces differ between backends: {per_face}")
    other = {(r["camera"], r["case"]): r for r in reference["results"] if "screenshot" in r}
    for result in report["results"]:
        match = other.get((result["camera"], result["case"]))
        if not match or "ownership" in result["case"] or "screenshot" not in result:
            continue
        a, b = image(Path(result["screenshot"])), image(Path(match["screenshot"]))
        if a is not None and b is not None and a.shape == b.shape:
            delta = np.abs(a - b)
            comparison["images"][f'{result["camera"]}/{result["case"]}'] = {
                "meanLevels": round(float(delta.mean()), 3),
                "over8Levels": round(float((delta.max(axis=2) > 8).mean()), 4)}
    return comparison


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--basepath", type=Path)
    parser.add_argument("--backend", choices=("gl", "vk"), default="gl")
    parser.add_argument("--binaries", type=Path, help="engine build to copy in first (for example .install)")
    parser.add_argument("--cameras", default="all", help="comma-separated camera names, or all")
    parser.add_argument("--skip-compile", action="store_true")
    parser.add_argument("--skip-authoring", action="store_true", help="station cameras only: no authoring bay or cutout proof")
    parser.add_argument("--skip-probes", action="store_true", help="no probe launches (room probe, stock scene)")
    parser.add_argument("--skip-stock", action="store_true", help="no stock-map probe launch")
    parser.add_argument("--reference", type=Path, help="another backend's report.json to compare with")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--min-free-gb", type=float, default=5.0,
                        help="wait (up to 15 minutes) for this much free commit before each launch")
    parser.add_argument("--max-private-gb", type=float, default=8.0,
                        help="stop a client whose private memory passes this (0: no ceiling; needs psutil)")
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
    results, proof = capture(args, cvars, record)
    report = {"backend": args.backend, "elapsed": proof.pop("elapsed"), "synthetic": record.get("synthetic"), **proof}
    if record.get("authoring") and not args.skip_authoring:
        # r_convertMD5toMD5R converts the model at load: its own launch.
        packed, packed_proof = capture(args, cvars, record, md5r=True)
        results += packed
        report["elapsedMD5R"] = packed_proof["elapsed"]
        compare_md5r(record["authoring"], results)
    if record.get("probes") and not args.skip_probes:
        more, report["probes"] = capture_probes(args, cvars, record, results)
        results += more
        if record.get("authoring") and not args.skip_stock:
            more, report["stock"] = capture_stock(args, cvars, record)
            results += more
    report["results"] = results
    if args.reference:
        report["comparison"] = compare_reference(report, json.loads(args.reference.read_text(encoding="utf-8")))
        if report["comparison"]["failures"]:
            results.append({"camera": "-", "case": "reference", "failures": report["comparison"]["failures"]})
    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    failed = [r for r in results if r["failures"]]
    for result in results:
        share = f" owned={result['ownedShare']:.0%}" if result.get("ownedShare") is not None else ""
        print(f"  {result['camera']:24s} {result['case']:18s}{share} {result['failures'] or 'ok'}")
    for key in ("cutout", "decals"):
        if key in report:
            print(f"  {key}: {report[key]}")
    for key in ("probes", "stock"):
        if key in report:
            print(f"  {key}: " + str({k: v for k, v in report[key].items() if k not in ("faces", "telemetry")}))
    if "comparison" in report:
        print("  comparison: " + str({k: v for k, v in report["comparison"].items() if k != "images"}))
    print(f"PBR showcase ({args.backend}{', synthetic' if record.get('synthetic') else ''}): "
          f"{len(results) - len(failed)}/{len(results)} passed; report {args.output_dir / 'report.json'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
