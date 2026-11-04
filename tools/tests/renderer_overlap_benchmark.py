#!/usr/bin/env python3
"""Windowed, in-launch mode 0/1/2 comparisons using stock gameplay assets.

All modes visit every order position. No device input is read or injected.
These measurements do not establish physical display or Quality qualification.
"""
from pathlib import Path
from contextlib import redirect_stdout
import argparse
import json
import math
import re
import statistics
import threading
import time

import renderer_gameplay_benchmark as benchmark

ORDERS = ((0, 1, 2), (2, 0, 1), (1, 2, 0))
PACING = re.compile(r"Frame pacing snapshot (hf_r\d+m[012]) \([^)]*\):.*?samples=(\d+),.*?p50=([\d.]+) ms, p95=([\d.]+) ms, p99=([\d.]+) ms, max=([\d.]+) ms")
MEAN = re.compile(r"Frame timing window \([^)]*\): samples=(\d+), span_ms=(\d+), mean_ms=([\d.]+)")
SNAPSHOT = re.compile(r"Frame snapshot stats (hf_r\d+m[012]): (.*)")


class HostLoad:
    """Optional host-load evidence; unavailable monitoring never means quiet."""
    def __init__(self):
        self.samples = []
        self.done = threading.Event()
        self.thread = None
        try:
            import psutil
            self.psutil = psutil
        except ImportError:
            self.psutil = None

    def start(self):
        if self.psutil:
            self.thread = threading.Thread(target=self.collect, daemon=True)
            self.thread.start()

    def collect(self):
        own_runtime = (Path(__file__).resolve().parents[2] / ".install").resolve()
        while not self.done.is_set():
            peers = []
            for process in self.psutil.process_iter(["name", "exe"]):
                name = (process.info["name"] or "").lower()
                executable = process.info["exe"]
                game = name.startswith("openq4")
                compiler = name in ("cl.exe", "link.exe", "ninja.exe", "ninja", "cc1plus", "clang++")
                if game and executable and Path(executable).resolve().parent == own_runtime:
                    continue
                if game or compiler:
                    peers.append({"pid": process.pid, "name": name, "exe": executable})
            self.samples.append({"utc_epoch": time.time(), "cpu_percent": self.psutil.cpu_percent(), "peers": peers})
            self.done.wait(1)

    def stop(self):
        self.done.set()
        if self.thread:
            self.thread.join()
        return {"available": self.psutil is not None, "samples": self.samples,
                "peer_activity_observed": any(s["peers"] for s in self.samples)}


def commands(sample_ms, warmup_ms):
    result = ["applyPerformancePreset quality", "com_maxfps 0", "r_swapInterval 0",
              "vid_restart", "waitMsec 3000", "r_rendererMetrics 0", "r_rendererGpuTimers 0",
              "rendererFrameOwnershipSelfTest", "noclip"]
    for repetition, order in enumerate(ORDERS):
        for mode in order:
            label = f"hf_r{repetition}m{mode}"
            result += [f"r_vkRenderThread {mode}", "setviewpos 10200 -6800 40 0 170 0",
                       f"benchmarkViewSweep 360 {warmup_ms}", f"waitMsec {warmup_ms}",
                       "setviewpos 10200 -6800 40 0 170 0", "renderThreadStats reset",
                       "rendererFrameSnapshotStats reset", "framePacingReset",
                       f"benchmarkViewSweep 360 {sample_ms}", f"waitMsec {sample_ms}",
                       f"framePacingSnapshot {label}", f"rendererFrameSnapshotStats {label}",
                       "renderThreadStats"]
    return result + ["r_vkRenderThread 0"]


def parse_windows(text):
    windows = []
    for match in PACING.finditer(text):
        label, samples, *values = match.groups()
        end = text.find("Frame pacing snapshot ", match.end())
        body = text[match.end():end if end >= 0 else len(text)]
        mean, snapshot = MEAN.search(body), SNAPSHOT.search(body)
        if not mean or not snapshot or snapshot[1] != label:
            raise ValueError(f"Incomplete timing/snapshot evidence: {label}")
        stats = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", snapshot[2])}
        numeric = [float(value) for value in values] + [float(mean[3])]
        if int(samples) < 20 or int(mean[1]) != int(samples) or any(not math.isfinite(v) or v <= 0 for v in numeric):
            raise ValueError(f"Invalid timing window: {label}")
        mode = int(label[-1])
        if mode == 2 and (stats.get("frames", 0) == 0 or stats.get("bytes", 0) == 0):
            raise ValueError(f"Missing snapshot work: {label}")
        if mode != 2 and stats.get("frames", 0) != 0:
            raise ValueError(f"Unexpected snapshot work: {label}")
        windows.append(dict(label=label, mode=mode, samples=int(samples),
                            p50_ms=numeric[0], p95_ms=numeric[1], p99_ms=numeric[2],
                            max_ms=numeric[3], mean_ms=numeric[4], snapshot=stats))
    expected = [f"hf_r{r}m{mode}" for r, order in enumerate(ORDERS) for mode in order]
    if [w["label"] for w in windows] != expected:
        raise ValueError("Missing, duplicated or reordered comparison windows")
    return windows


def summarize(windows):
    summaries = {}
    for mode in range(3):
        selected = [w for w in windows if w["mode"] == mode]
        stats = [w["snapshot"] for w in selected]
        frames = sum(s.get("frames", 0) for s in stats)
        summaries[str(mode)] = {
            "median_of_windows": {key: statistics.median(w[key] for w in selected)
                                  for key in ("mean_ms", "p50_ms", "p95_ms", "p99_ms")},
            "snapshot_us_per_frame": sum(s.get("cpuUs", 0) for s in stats) / frames if frames else 0,
            "snapshot_bytes_per_frame": sum(s.get("bytes", 0) for s in stats) / frames if frames else 0,
            "snapshot_reused_bytes_per_frame": sum(s.get("reusedBytes", 0) for s in stats) / frames if frames else 0,
        }
    return summaries


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--retain-indexes", choices=("0", "1"), default="1")
    parser.add_argument("--sample-ms", type=int, default=8000)
    parser.add_argument("--warmup-ms", type=int, default=4000)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args(argv)
    if args.sample_ms < 1000 or args.warmup_ms < 1000:
        parser.error("sample/warmup windows must last at least 1000 ms")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    bench_args = ["--profile", "smoke", "--cases", "sp-airdefense1", "--tiers", "auto",
                  "--render-api", "vk", "--display-modes", "windowed", "--width", str(args.width),
                  "--height", str(args.height), "--maxfps", "0", "--swap-intervals", "0",
                  "--pacing-only", "--settle-frames", "120", "--sample-msec", "1000",
                  "--timeout", str(300 + 9 * (args.sample_ms + args.warmup_ms) // 1000),
                  "--basepath", args.basepath, "--output-dir", str(output / "gameplay")]
    for key, value in {"in_mouse": 0, "in_joystick": 0, "in_nograb": 1,
                       "r_hiddenWindow": 0, "r_borderless": 0, "r_borderlessDefaultMigrated": 1}.items():
        bench_args += ["--set-launch-cvar", f"{key}={value}"]
    bench_args += ["--set-cvar", f"r_vkSnapshotRetain={args.retain_indexes}"]
    for command in commands(args.sample_ms, args.warmup_ms):
        bench_args += ["--exec-command", command]
    monitor = HostLoad()
    monitor.start()
    try:
        with (output / "runner.log").open("w", encoding="utf-8") as log, redirect_stdout(log):
            exit_code = benchmark.main(bench_args)
    finally:
        host_load = monitor.stop()
        (output / "host-load.json").write_text(json.dumps(host_load, indent=2), encoding="utf-8")
    source = output / "gameplay/renderer_gameplay_benchmark_report.json"
    report = json.loads(source.read_text(encoding="utf-8"))
    failures, windows = [], []
    if exit_code or report["status"] != "pass":
        failures.append("Gameplay/runtime verification failed; inspect the benchmark report")
    try:
        role = report["results"][0]["roles"][0]
        windows = parse_windows(Path(role["log"]).read_text(encoding="utf-8", errors="replace"))
    except (ValueError, KeyError, IndexError) as error:
        failures.append(str(error))
    result = {"status": "fail" if failures else "pass", "failures": failures,
              "qualification": "windowed functional performance comparison; no physical qualification",
              "retained_indexes": args.retain_indexes, "display": [args.width, args.height], "sample_ms": args.sample_ms,
              "host_load_available": host_load["available"],
              "peer_activity_observed": host_load["peer_activity_observed"],
              "warmup_ms": args.warmup_ms, "benchmark_report": str(source),
              "git": report["git"], "windows": windows,
              "modes": summarize(windows) if windows else {}}
    (output / "comparison.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("status", "failures", "modes")}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
