#!/usr/bin/env python3
"""Show loadscreens through every reader and compare formats (roadmap PKG-03).

Release loadscreens are either TGA beside DXT1 DDS (as shipped through 0.14) or
DDS only. For one runtime this harness loads a single-player map on each
renderer and texture-quality profile, and records what a player sees:

* the loading screen with its expanded background, which the session composes
  from the tiles on the CPU at a wide window and holds only in memory, captured
  by `com_captureLoadingScreen` as the load finishes
  (screenshots/loading_<n>.tga);
* the levelshot drawn by the renderer through `testImage`, as a GUI material is,
  captured with `screenshot`;
* where each tile was read from (fs_debug) and the image list entry.

Run it on the TGA runtime and on a DDS-only runtime with the same --output-dir
parent, then `--compare A B` prints the PSNR of each capture pair.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
PROFILES = {
    "gl": {"r_renderApi": "gl"},
    "gl-low": {"r_renderApi": "gl", "image_downSize": "1", "image_downSizeLimit": "256", "image_picmip": "2"},
    "vulkan": {"r_renderApi": "vulkan"},
    "vulkan-low": {"r_renderApi": "vulkan", "image_downSize": "1", "image_downSizeLimit": "256", "image_picmip": "2"},
}


def run(args, profile: str, overrides: dict[str, str]) -> dict:
    home = args.output_dir.resolve() / profile
    shutil.rmtree(home, ignore_errors=True)
    game = home / "baseoq4"
    game.mkdir(parents=True)
    shot = args.levelshot
    (game / "audit_loaded.cfg").write_text(
        f"listImages\nwait 5\ntestImage gfx/guis/loadscreens/{shot}\nwait 30\nscreenshot audit_testimage\nwait 10\n"
        "testImage\nwait 5\nquit\n", encoding="utf-8")
    cvars = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(home), "fs_devpath": str(home),
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "fs_debug": "1", "s_noSound": "1",
        "r_fullscreen": "0", "r_mode": "-1", "r_customWidth": "1920", "r_customHeight": "1080",
        "in_mouse": "0", "ui_retained": "1", "com_skipLoadingContinue": "1", "com_captureLoadingScreen": "1",
        "g_autoExecAfterMapLoad": "audit_loaded.cfg", "g_autoExecAfterMapLoadDelayMs": "1500",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        **overrides,
    }
    command = [str(args.runtime_dir.resolve() / f"openQ4-client_x64{SUFFIX}")]
    command += [part for key, value in cvars.items() for part in ("+set", key, value)]
    command += ["+devmap", args.map]
    started = time.monotonic()
    completed = subprocess.run(command, cwd=args.runtime_dir.resolve(), timeout=args.timeout,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = (game / "logs" / "openq4.log").read_text(encoding="utf-8", errors="replace") if (game / "logs/openq4.log").is_file() else ""
    reads = sorted(set(re.findall(r"OpenFileRead: (gfx/guis/loadscreens/\S+) \(found in '([^']+)'\)", log)))
    loading = sorted(str(p.relative_to(home)) for p in game.glob("screenshots/loading_*.tga"))
    shots = sorted(str(p.relative_to(home)) for p in game.rglob("audit_testimage*"))
    images = [line.strip() for line in log.splitlines() if "loadscreens/" in line and ("x" in line and ("RGBA" in line or "DXT" in line or "MB" in line))][:12]
    return {
        "profile": profile, "cvars": overrides, "exit_code": completed.returncode,
        "seconds": round(time.monotonic() - started, 1), "fatal": "\nFATAL: " in log,
        "reads": [{"file": file, "pack": Path(pack).name} for file, pack in reads],
        "loading_captures": loading, "testimage_captures": shots, "image_list": images,
    }


def load_rgb(path: Path) -> tuple[int, int, bytes]:
    from PIL import Image
    image = Image.open(path, formats=["TGA"]).convert("RGB")
    return image.width, image.height, image.tobytes()


def psnr(a: Path, b: Path) -> float | None:
    wa, ha, da = load_rgb(a)
    wb, hb, db = load_rgb(b)
    if (wa, ha) != (wb, hb):
        return None
    step = 13
    error = sum((x - y) * (x - y) for x, y in zip(da[::step], db[::step])) / max(1, len(da[::step]))
    return 99.0 if error == 0 else 10 * math.log10(255 * 255 / error)


def compare(a: Path, b: Path) -> dict:
    result = {}
    for profile in PROFILES:
        pa, pb = a / profile, b / profile
        if not pa.is_dir() or not pb.is_dir():
            continue
        pairs = {}
        for kind, pattern in (("testimage", "baseoq4/audit_testimage*"), ("loading", "baseoq4/screenshots/loading_*.tga")):
            fa, fb = sorted(pa.glob(pattern)), sorted(pb.glob(pattern))
            if fa and fb:
                pairs[kind] = psnr(fa[0], fb[0])
        result[profile] = pairs
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--profile", action="append", choices=tuple(PROFILES), default=[])
    parser.add_argument("--map", default="game/airdefense1")
    parser.add_argument("--levelshot", default="airdefense")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--compare", nargs=2, type=Path, metavar=("A", "B"))
    args = parser.parse_args()
    if args.compare:
        print(json.dumps(compare(*args.compare), indent=2))
        return 0
    if args.basepath is None or args.output_dir is None:
        parser.error("--basepath and --output-dir are required")
    runs = [run(args, name, PROFILES[name]) for name in (args.profile or list(PROFILES))]
    failures = [f"{r['profile']}: fatal or no capture" for r in runs if r["fatal"] or not r["testimage_captures"]]
    report = {"status": "fail" if failures else "pass", "runs": runs, "failures": failures}
    args.output_dir.resolve().mkdir(parents=True, exist_ok=True)
    (args.output_dir.resolve() / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures,
                      "reads": {r["profile"]: r["reads"][:6] for r in runs}}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
