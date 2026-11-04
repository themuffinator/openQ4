#!/usr/bin/env python3
"""Run the HF-07/HF-08 reference qualification in one command.

For the operator at the reference desktop (presentation-owners.md). Every case
runs presentation_look_runtime.py full screen at the native desktop mode, in
order:

1. the cap matrix 30/60/120/144/165/240/360/uncapped with vsync off, per
   renderer (Vulkan also measures when frames reach the display through
   VK_KHR_present_wait);
2. vsync at 240 and uncapped, and adaptive vsync at 240;
3. variable refresh: vsync with the cap 3 Hz under the display's refresh, which
   the first run reports (turn G-Sync/FreeSync on in the driver first);
4. the engine-side policy cases: hidden, minimized, menu and hitch;
5. the sensor session (--sensor-seconds): press fire with a click-to-photon
   sensor on the left edge; the engine reports its own press-to-swap time for
   the same presses;
6. the captioned by-eye review sessions and their checklist.

--reference is the operator's statement that the window is visible, in the
foreground, on the reference machine; only then are the 240 Hz verdicts
qualifying. --windowed runs every case in a 960x540 window instead, a rehearsal
that checks the procedure end to end without taking over the display. The run writes reference-summary.md and suite.json beside each
case's own output. It reads no device input except the operator's own presses
in the sensor session.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
HARNESS = HERE / "presentation_look_runtime.py"
CAPS = [30, 60, 120, 144, 165, 240, 360, 0]


def run_case(args, output: Path, name: str, options: list[str], timeout: int = 1800) -> dict:
    """One harness invocation; returns its report (or the failure) for the summary."""
    directory = output / name
    # Every case loads from a fresh save path and regenerates its images first, which
    # a busy machine can stretch past the harness's default four minutes per client.
    if "--timeout" not in options:
        options = options + ["--timeout", "600"]
    command = [sys.executable, str(HARNESS), "--basepath", str(args.basepath), "--output-dir", str(directory),
               "--runtime-dir", str(args.runtime_dir)] + options
    if args.reference:
        command.append("--reference")
    started = time.monotonic()
    print(f"== {name}: {' '.join(options)}", flush=True)
    try:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=timeout)
        exit_code, tail = result.returncode, (result.stdout + result.stderr)[-2000:]
    except subprocess.TimeoutExpired:
        exit_code, tail = "timeout", ""
    report = directory / "report.json"
    parsed = None
    try:
        parsed = json.loads(report.read_text(encoding="utf-8"))
        if not isinstance(parsed, dict) or not isinstance(parsed.get("cases"), list) or not parsed["cases"]:
            raise ValueError("report contains no cases")
        if any(not isinstance(case, dict) or "case" not in case or "exit" not in case for case in parsed["cases"]):
            raise ValueError("report contains an invalid case")
        if exit_code == 0 and any(case["exit"] != 0 or case.get("errors") for case in parsed["cases"]):
            exit_code = 1
    except (OSError, ValueError) as error:
        tail += f"\nCannot read complete report: {error}"
        if exit_code == 0:
            exit_code = 1
        parsed = None
    record = {"name": name, "options": options, "exit": exit_code, "seconds": round(time.monotonic() - started, 1),
              "report": parsed}
    if exit_code not in (0, 1) or record["report"] is None:
        record["tail"] = tail
    print(f"   exit {exit_code}, {record['seconds']} s", flush=True)
    return record


def refresh_from(records: list[dict]) -> float | None:
    for record in records:
        for case in (record.get("report") or {}).get("cases", []):
            snapshot = (case.get("pacing") or {}).get("framePacingSnapshot") or {}
            refresh = snapshot.get("refreshHz")
            if isinstance(refresh, (int, float)) and math.isfinite(refresh) and refresh > 3:
                return float(refresh)
    return None


def spread(values: dict | None, first: str, second: str | None = None, digits: int = 2) -> str:
    """'p95/p99' (or one percentile) from a harness percentile record, or a dash."""
    if not values:
        return "—"
    text = f"{values[first]:.{digits}f}"
    return f"{text}/{values[second]:.{digits}f}" if second else text


def summarise(args, records: list[dict], vrr_cap: int | None) -> str:
    output = args.output_dir.resolve()
    statement = ("visible foreground window on the reference machine (--reference)" if args.reference
                 else "NOT declared reference: engine-side rehearsal only")
    if args.windowed:
        statement += ", windowed"
    failed = [record["name"] for record in records if record["exit"] != 0]
    lines = ["# HF-07/HF-08 reference run", "",
             f"Statement: {statement}.",
             f"Runtime: `{args.runtime_dir}`. Variable-refresh cap: {vrr_cap if vrr_cap else 'not run (no refresh reported)'}.",
             f"Runs: {len(records)}; failed or incomplete runs: {', '.join(failed) if failed else 'none'}.", "",
             "HF-07/HF-08 remain pending physical sensor measurements and the operator's motion/visual review;",
             "a successful automated run alone does not close either gate.", "",
             "## Pacing", "",
             "| Run | Case | Swap | Drawn Hz | Draw p95/p99 ms | Presented p95/p99 ms | Queue p50 ms | Late wake p95 ms | Verdict |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for record in records:
        report = record.get("report") or {}
        for case in report.get("cases", []):
            if case.get("scenario") != "pacing":
                continue
            pacing = case.get("pacing")
            if not pacing:
                # a case that drew too few interpolated frames to judge must still show
                lines.append(f"| {record['name']} | {case['case']} | {report.get('swapInterval')} | — | no capture "
                             f"({case.get('samples', 0)} draws, exit {case['exit']}) | — | — | — | not judged |")
                continue
            lines.append(
                f"| {record['name']} | {case['case']} | {report.get('swapInterval')} | {pacing['presentedHz']:.1f} | "
                f"{spread(pacing['intervalMs'], 'p95', 'p99')} | {spread(pacing.get('presentedIntervalMs'), 'p95', 'p99')} | "
                f"{spread(pacing.get('presentQueueMs'), 'p50')} | {spread(pacing.get('throttleLateMs'), 'p95', digits=3)} | "
                f"{pacing.get('verdict', '')} |")
    lines += ["", "## Policy, latency and review", ""]
    for record in records:
        report = record.get("report") or {}
        for case in report.get("cases", []):
            scenario = case.get("scenario")
            if scenario == "pacing":
                continue
            note = (f"- {record['name']} / {case['case']}: exit {case['exit']}, gameplay in draws "
                    f"{case.get('gameplayInDraws')}, errors {len(case.get('errors') or [])}")
            if scenario == "sensor":
                swap = case.get("inputToSwapMs") or {}
                note += (f"; {swap.get('flashes', 0)} presses, engine press-to-swap p50/p95 "
                         f"{spread(swap.get('flashPressToSwap'), 'p50', 'p95')} ms"
                         " (subtract from the sensor's readings for the GPU, compositor and display share)")
            if scenario == "minimized":
                shown = case.get("minimizedPacing") or {}
                note += (f"; cap {shown.get('cap')}, {shown.get('hz') or 0.0:.1f} Hz over {shown.get('frames', 0)} frames"
                         " (at most 60; the OS may slow it further)")
            if scenario == "hidden":
                note += f"; presented {((case.get('pacing') or {}).get('presentedHz') or 0.0):.1f} Hz"
            lines.append(note)
        if record["name"] == "review":
            lines.append(f"- review verdicts: {output / record['name'] / 'review-checklist.md'}")
        if record.get("tail"):
            lines.append(f"- {record['name']}: did not complete ({record['exit']}): {record['tail'][-500:]}")
    lines += ["", "Record the 240 Hz verdicts, the sensor readings minus the engine share and the review",
              "verdicts in HF-07/HF-08 of docs/dev/roadmap.md and the presentation owners contract."]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=HERE.parents[1] / ".install")
    parser.add_argument("--renderers", nargs="+", choices=("gl", "vulkan"), default=["gl", "vulkan"])
    parser.add_argument("--reference", action="store_true",
                        help="operator statement: a visible foreground window on the HF-07 reference machine")
    parser.add_argument("--windowed", action="store_true",
                        help="rehearsal: run every case in a window instead of full screen")
    parser.add_argument("--render-thread", choices=("0", "1", "2"), default="0",
                        help="Vulkan backend mode; record it with the reference evidence (default inline)")
    parser.add_argument("--sensor-seconds", type=int, default=120)
    parser.add_argument("--skip-sensor", action="store_true")
    parser.add_argument("--skip-review", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="print the cases without running them")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    screen = [] if args.windowed else ["--fullscreen"]

    plan = []
    for renderer in args.renderers:
        timing = ["--present-timing"] if renderer == "vulkan" else []
        base = ["--renderer", renderer] + screen + timing
        if renderer == "vulkan":
            base += ["--cvar", "r_vkRenderThread", args.render_thread]
        plan.append((f"{renderer}-caps", ["--scenario", "pacing", "--fps"] + [str(c) for c in CAPS] + base))
        plan.append((f"{renderer}-vsync", ["--scenario", "pacing", "--fps", "240", "0", "--swap-interval", "1"] + base))
        plan.append((f"{renderer}-adaptive", ["--scenario", "pacing", "--fps", "240", "--swap-interval", "-1"] + base))
        plan.append((f"{renderer}-vrr", None))	# filled from the refresh the caps run reports
    plan.append(("policy", ["--scenario", "hidden", "minimized", "menu", "hitch", "--fps", "240"]))
    if not args.skip_sensor:
        plan.append(("sensor", ["--scenario", "sensor", "--fps", "240", "--sensor-seconds", str(args.sensor_seconds),
                                "--timeout", str(args.sensor_seconds + 240)] + screen))
    if not args.skip_review:
        plan.append(("review", ["--scenario", "review", "--fps", "240"] + screen))
    if args.dry_run:
        for name, options in plan:
            print(name, " ".join(options) if options else "(--swap-interval 1, cap = refresh - 3)")
        return 0

    output.mkdir(parents=True, exist_ok=False)
    records, vrr_cap = [], None
    for name, options in plan:
        if options is None:
            renderer = name.split("-")[0]
            refresh = refresh_from([r for r in records if r["name"] == f"{renderer}-caps"])
            if not refresh:
                print(f"== {name}: skipped, the caps run reported no display refresh", flush=True)
                records.append({"name": name, "options": [], "exit": "not run", "seconds": 0,
                                "report": None, "tail": "No usable refresh reported; variable-refresh coverage is incomplete."})
                continue
            vrr_cap = max(30, int(refresh) - 3)
            timing = ["--present-timing"] if renderer == "vulkan" else []
            options = ["--scenario", "pacing", "--fps", str(vrr_cap), "--swap-interval", "1",
                       "--renderer", renderer] + screen + timing
            if renderer == "vulkan":
                options += ["--cvar", "r_vkRenderThread", args.render_thread]
        records.append(run_case(args, output, name, options))
    failed = any(record["exit"] != 0 or not record.get("report") for record in records)
    (output / "suite.json").write_text(json.dumps({"reference": args.reference, "renderThread": int(args.render_thread),
                                                 "automatedPassed": not failed, "physicalQualification": "pending",
                                                 "vrrCap": vrr_cap, "runs": records}, indent=1),
                                       encoding="utf-8")
    (output / "reference-summary.md").write_text(summarise(args, records, vrr_cap), encoding="utf-8")
    print(f"reference suite: {len(records)} runs, summary {output / 'reference-summary.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
