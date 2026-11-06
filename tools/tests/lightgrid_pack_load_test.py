#!/usr/bin/env python3
"""Load every shipped light-grid pack the way a map load does (roadmap PKG-03).

The release light-grid pack (pak1) carries only each map's .lightgridpack and
.lightgrid, not the loose atlas TGAs a bake also writes; those were the
fallback a renderer used when it rejected a pack. This harness proves that
fallback is never needed: the staged client's lightGridPackTest command
builds a render world from every map that has a pack, loads the pack through
the map-load path and uploads every area's atlases. Each renderer profile
must accept every pack with every chunk resident:

  gl            OpenGL, DXT atlases sampled by the GPU
  gl-software   OpenGL with r_lightGridSoftwareDecode 1, the path a renderer
                without S3TC (most OpenGL ES GPUs) takes
  vulkan        the Vulkan renderer

fs_searchAddons 1 mounts every addon pk4, so the community map pack's maps
(q4cmp*) load when the pack is installed. A map whose .proc is not installed is
reported and fails the run, since a shipped pack must have a map to light.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
LINE = re.compile(r"^LIGHTGRID_PACK_TEST (\S+) (accepted|rejected|loose-only)(.*)$", re.MULTILINE)
DONE = re.compile(r"^LIGHTGRID_PACK_TEST_DONE tested=(\d+) accepted=(\d+) rejected=(\d+) loose_only=(\d+)", re.MULTILINE)
PROFILES = {
    "gl": {"r_renderApi": "gl"},
    "gl-software": {"r_renderApi": "gl", "r_lightGridSoftwareDecode": "1"},
    "vulkan": {"r_renderApi": "vulkan"},
}


def run_profile(args, name: str, overrides: dict[str, str]) -> dict:
    home = args.output_dir.resolve() / name
    log = home / "baseoq4" / "logs" / "openq4.log"
    home.mkdir(parents=True)
    cvars = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(home), "fs_devpath": str(home),
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "r_hiddenWindow": "1", "r_fullscreen": "0", "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "360",
        "in_mouse": "0", "in_joystick": "0", "ui_retained": "1", "r_useLightGrid": "1",
        # Mount every addon pk4: the community map pack's maps live in one.
        "fs_searchAddons": "1",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        **overrides,
    }
    command = [str(args.runtime_dir.resolve() / f"openQ4-client_x64{SUFFIX}")]
    command += [part for key, value in cvars.items() for part in ("+set", key, value)]
    command += ["+lightGridPackTest", *args.map, "+quit"]
    started = time.monotonic()
    completed = subprocess.run(command, cwd=args.runtime_dir.resolve(), stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=args.timeout,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    results = [{"map": m[0], "result": m[1], "detail": m[2].strip()} for m in LINE.findall(text)]
    done = DONE.search(text)
    return {
        "profile": name,
        "cvars": overrides,
        "exit_code": completed.returncode,
        "seconds": round(time.monotonic() - started, 1),
        "summary": dict(zip(("tested", "accepted", "rejected", "loose_only"), map(int, done.groups()))) if done else None,
        "results": results,
        "fatal": "\nFATAL: " in text,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--profile", action="append", choices=tuple(PROFILES), default=[])
    parser.add_argument("--map", action="append", default=[], help="test only these maps")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    args.output_dir.resolve().mkdir(parents=True, exist_ok=False)

    failures: list[str] = []
    runs = []
    for name in args.profile or list(PROFILES):
        run = run_profile(args, name, PROFILES[name])
        runs.append(run)
        if run["fatal"] or run["summary"] is None:
            failures.append(f"{name}: the test did not finish (exit {run['exit_code']})")
            continue
        for result in run["results"]:
            if result["result"] == "rejected":
                failures.append(f"{name}: {result['map']} {result['detail']}")
        if not any(result["result"] == "accepted" for result in run["results"]):
            failures.append(f"{name}: no pack was accepted")
    report = {"status": "fail" if failures else "pass", "runs": runs, "failures": failures}
    (args.output_dir.resolve() / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"],
                      "summaries": {run["profile"]: run["summary"] for run in runs},
                      "loose_only": sorted({r["map"] for run in runs for r in run["results"] if r["result"] == "loose-only"}),
                      "failures": failures}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
