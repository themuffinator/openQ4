#!/usr/bin/env python3
"""Windowed, in-launch mode 0/1/2 comparisons using stock gameplay assets.

All modes visit every order position. No device input is read or injected.
These measurements do not establish physical display or Quality qualification.

--check parity freezes stock views and compares engine captures and backend
readbacks between the modes; --check bounds cycles the window size and scene
scale in modes 1 and 2 and samples process memory, handles, the retained
snapshot caches and Vulkan heap use. Both launch the staged client directly
with a hidden window and need numpy/psutil respectively.
"""
from pathlib import Path
from contextlib import redirect_stdout
import argparse
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import threading
import time

import renderer_gameplay_benchmark as benchmark

ROOT = Path(__file__).resolve().parents[2]
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
            # Work on the snapshotting thread (cycles on Windows, ns on POSIX):
            # unlike cpuUs it excludes time preempted by other processes.
            "snapshot_thread_work_per_frame": sum(s.get("threadWork", 0) for s in stats) / frames if frames else 0,
            "snapshot_omitted_bytes_per_frame": sum(s.get("omittedBytes", 0) for s in stats) / frames if frames else 0,
            "snapshot_temporary_index_bytes_per_frame": sum(s.get("temporaryIndexBytes", 0) for s in stats) / frames if frames else 0,
            "snapshot_bytes_per_frame": sum(s.get("bytes", 0) for s in stats) / frames if frames else 0,
            "snapshot_reused_bytes_per_frame": sum(s.get("reusedBytes", 0) for s in stats) / frames if frames else 0,
            "snapshot_retained_vertex_bytes_per_frame": sum(s.get("retainedVertexBytes", 0) for s in stats) / frames if frames else 0,
            # Residency at the end of each window, never a per-frame cost.
            "retained_resident_bytes_max": max((s.get("indexResidentBytes", 0) + s.get("vertexResidentBytes", 0) for s in stats), default=0),
            "retained_entries_max": max((s.get("indexEntries", 0) + s.get("vertexEntries", 0) for s in stats), default=0),
        }
    return summaries

FAILURE_PATTERNS = (re.compile(r"^\s*(?:\^\d)?ERROR\b"), re.compile(r"\bFATAL\b"),
                    re.compile(r"Validation Error|VUID-"), re.compile(r"self-test FAILED"))
OWNERSHIP = "Renderer frame ownership self-test passed"
STATS_LABEL = re.compile(r"Frame snapshot stats (\w+): (.*)")
THREAD = re.compile(r"Render thread: enabled=(\d+) running=(\d+) threaded=(\d+) inline=(\d+) waits=(\d+) waitMs=([\d.]+) completed=(\d+)")
OVERLAP = re.compile(r"Render thread overlap: mode=(\d+) frontEndsWhileBusy=(\d+) frontEndsFinishedWhileBusy=(\d+)")
EARLY = re.compile(r"Render thread early sync: (.+)=(\d+)")
# Backend images read back after a frame the render thread drew. Names that a
# configuration does not create are reported as unavailable and skipped.
READBACKS = ("_vkTemporalSceneResolveColor", "_vkWindowViewResolveColor", "_vkPostScene", "_vkLinearScene",
             "_vkHDRClassicAccum", "_vkDisplayColorSource0", "_vkDisplayColorSource1",
             "_temporalHistoryAlbedo0", "_temporalHistoryAlbedo1",
             "_vkTemporalDirectHistory0", "_vkTemporalDirectHistory1", "_currentRender")
# The report commands join the frame in flight, and mode 2's ordinary join at
# EndFrame is counted under "handoff". Anything else is an early join.
EXPECTED_SYNCS = {"stats", "snapshot stats"}
PARITY_CASES = {
    "sp_airdefense1": dict(map="game/airdefense1", multiplayer=False, must_freeze=True,
                           poses={"arena": "10200 -6800 40 0 170 0", "skyline": "10324 -6964 420 12 0 0"}),
    # must_freeze: g_stopTime holds these views still, so mode 0 must repeat itself.
    "sp_hub2": dict(map="game/hub2", multiplayer=False, must_freeze=True, poses={"spawn": None}),
    "sp_medlabs": dict(map="game/medlabs", multiplayer=False, must_freeze=True, poses={"spawn": None}),
    # No single-client world freeze exists in multiplayer: pickups turn and
    # lights pulse, so modes are judged against the mode 0 frame-to-frame floor.
    "mp_q4dm1": dict(map="mp/q4dm1", multiplayer=True, poses={"spawn": None}),
}
SIZES = ((1280, 720), (1024, 576), (1600, 900), (960, 540))
FRACTIONS = (100, 75, 50, 67, 125)
CYCLE_PERIOD = 20		# least common multiple of the two rotations


def launch(args, run_dir, lines, map_name, multiplayer, validation, watch=None):
    """Runs one hidden-window Vulkan client through a console script; returns (exit, log text)."""
    game_dir = run_dir / "baseoq4"
    (game_dir / "screenshots").mkdir(parents=True)
    (game_dir / "overlap.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")
    runtime = args.runtime_dir.resolve()
    command = [str(runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))]
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": 1,
        "logFile": 2, "logFileName": "logs/overlap.log", "developer": 0,
        "fs_savepath": run_dir, "fs_devpath": run_dir, "fs_game": "baseoq4",
        "com_skipLoadingContinue": 1, "com_loadingContinueAutoAdvance": 1, "g_autoSkipCinematics": 1,
        "com_skipLogoVideos": 1, "in_mouse": 0, "in_joystick": 0, "in_nograb": 1, "s_noSound": 1,
        "r_fullscreen": 0, "r_mode": -1, "r_customWidth": args.width, "r_customHeight": args.height,
        "r_windowWidth": args.width, "r_windowHeight": args.height, "r_borderless": 0,
        "r_borderlessDefaultMigrated": 1, "r_hiddenWindow": 0 if args.visible else 1,
        "ui_retained": 1, "ui_autoJoin": 1 if multiplayer else 0, "r_renderApi": "vulkan",
        "r_vkValidation": 1 if validation else 0, "r_vkSnapshotRetain": args.retain,
        "g_autoExecAfterMapLoad": "overlap.cfg", "g_autoExecAfterMapLoadDelayMs": 1500,
    }
    if args.basepath:
        cvars["fs_basepath"] = args.basepath
    if multiplayer:
        cvars.update({"si_pure": 0, "net_serverDedicated": 0, "net_LANServer": 1, "net_ip": "127.0.0.1",
                      "net_enableIPv6": 0, "si_gameType": "DM"})
    for name, value in cvars.items():
        command += ["+set", name, str(value)]
    command += ["+devmap", map_name]
    log_path = game_dir / "logs" / "overlap.log"
    process = subprocess.Popen(command, cwd=runtime)
    stop = threading.Event()
    watcher = threading.Thread(target=watch, args=(process, log_path, stop), daemon=True) if watch else None
    if watcher:
        watcher.start()
    try:
        exit_code = process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        exit_code = "timeout"
    stop.set()
    if watcher:
        watcher.join()
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    return exit_code, text


def labelled_stats(text):
    """Each labelled snapshot-stats line and the render-thread report printed after it."""
    result = {}
    matches = list(STATS_LABEL.finditer(text))
    for index, match in enumerate(matches):
        body = text[match.end():matches[index + 1].start() if index + 1 < len(matches) else len(text)]
        entry = {"snapshot": {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", match[2])}}
        thread, overlap = THREAD.search(body), OVERLAP.search(body)
        if thread and overlap:
            entry.update(threaded=int(thread[3]), inline=int(thread[4]), waits=int(thread[5]),
                         completed=int(thread[7]), mode=int(overlap[1]), front_ends_while_busy=int(overlap[2]),
                         front_ends_finished_while_busy=int(overlap[3]),
                         early_syncs={m[1]: int(m[2]) for m in EARLY.finditer(body[:overlap.end() + 2000].split("Wrote ")[0])})
        result[match[1]] = entry
    return result


def log_failures(text):
    return [line.strip() for line in text.splitlines() if any(p.search(line) for p in FAILURE_PATTERNS)]


def read_tga(path):
    import numpy as np
    data = path.read_bytes()
    width, height = int.from_bytes(data[12:14], "little"), int.from_bytes(data[14:16], "little")
    channels = data[16] // 8
    if data[2] != 2 or channels not in (3, 4):
        raise ValueError(f"{path}: unsupported TGA")
    return np.frombuffer(data, dtype=np.uint8, count=width * height * channels,
                         offset=18 + data[0]).reshape(height, width, channels)[:, :, :3].astype(np.int16)


def differing_share(a, b, threshold=8):
    """Share of pixels whose largest channel difference exceeds threshold (8-bit levels)."""
    import numpy as np
    if a.shape != b.shape:
        return 1.0
    return float(np.mean(np.max(np.abs(a - b), axis=2) > threshold))


def parity_lines(case, sequence):
    # Uncapped: under a frame cap the backend finishes while the main thread
    # sleeps, which hides the overlap these windows must observe.
    lines = ["applyPerformancePreset quality", "com_maxfps 0", "r_swapInterval 0", "vid_restart", "wait 240"]
    if not case["multiplayer"]:
        lines.append("noclip")
    lines += ["rendererFrameOwnershipSelfTest", "r_temporalAA", "r_motionBlur", "r_multiSamples", "r_screenFraction"]
    labels = []
    for pose, position in case["poses"].items():
        if position:
            lines.append(f"setviewpos {position}")
        lines.append("wait 120")
        if not case["multiplayer"]:
            lines += ["g_stopTime 1", "wait 60"]
        # g_stopTime freezes the world, not the HUD's own clock: its top-centre
        # element keeps animating. "quality" keeps the HUD and is judged against
        # the mode 0 floor; "exact" hides it, so the frozen Quality frame
        # (temporal AA included) must repeat bit for bit in every mode.
        for series in ("quality", "exact"):
            lines += [f"g_showHud {0 if series == 'exact' else 1}", "wait 30"]
            for index, mode in enumerate(sequence):
                label = f"{pose}_{series}_{index}_m{mode}"
                labels.append(label)
                lines += [f"r_vkRenderThread {mode}", "wait 90", "renderThreadStats reset",
                          "rendererFrameSnapshotStats reset", "wait 150",
                          f"rendererFrameSnapshotStats {label}", "renderThreadStats"]
                lines += [f"screenshot image {image} screenshots/{label}_rb_{image.strip('_')}.tga" for image in READBACKS]
                lines += [f"screenshot screenshots/{label}_cap.tga", "wait 4"]
        if not case["multiplayer"]:
            lines += ["g_stopTime 0", "wait 30"]
    lines += ["r_vkRenderThread 0", "wait 10", "rendererFrameOwnershipSelfTest", "wait 2", "quit"]
    return lines, labels


def check_window(label, entry, failures):
    """A measured window in mode 1 or 2 must have run on the render thread without unexpected joins."""
    mode = int(label[-1])
    if "threaded" not in entry:
        failures.append(f"{label}: no render-thread report")
        return
    expected = EXPECTED_SYNCS | ({"handoff"} if mode == 2 else set())
    unexpected = {reason: count for reason, count in entry["early_syncs"].items() if reason not in expected}
    if mode == 0:
        if entry["threaded"] != 0:
            failures.append(f"{label}: {entry['threaded']} threaded frames in mode 0")
        return
    if entry["threaded"] < 60 or entry["inline"] != 0:
        failures.append(f"{label}: threaded={entry['threaded']} inline={entry['inline']}")
    if unexpected:
        failures.append(f"{label}: unexpected early syncs {unexpected}")
    if mode == 2 and (entry["front_ends_while_busy"] == 0 or entry["snapshot"].get("frames", 0) == 0):
        failures.append(f"{label}: no front-end overlap or snapshot work in mode 2")


def run_parity(args):
    import numpy  # noqa: F401  (image comparison; fail before launching anything)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"check": "parity", "snapshot_retain": args.retain, "display": [args.width, args.height],
              "qualification": "hidden-window functional parity; no physical qualification", "cases": {}}
    failures = []
    for name in args.cases.split(","):
        case = PARITY_CASES[name]
        sequence = (0, 0, 1, 2, 0) if case["multiplayer"] else (0, 1, 2, 0)
        lines, labels = parity_lines(case, sequence)
        exit_code, text = launch(args, output / name, lines, case["map"], case["multiplayer"], args.validation)
        shots = output / name / "baseoq4" / "screenshots"
        stats = labelled_stats(text)
        result = {"exit_code": exit_code, "log_failures": log_failures(text),
                  "ownership_self_tests": text.count(OWNERSHIP), "windows": {}, "groups": {}}
        if exit_code != 0 or result["log_failures"] or result["ownership_self_tests"] < 2:
            failures.append(f"{name}: exit {exit_code}, {len(result['log_failures'])} log failures, "
                            f"{result['ownership_self_tests']} ownership self-tests")
        for label in labels:
            entry = stats.get(label, {})
            check_window(f"{name}/{label}", entry, failures)
            result["windows"][label] = entry
        # Group the files of one pose/series by kind (capture or a readback image).
        for pose in case["poses"]:
            for series in ("quality", "exact"):
                group = [label for label in labels if label.startswith(f"{pose}_{series}_")]
                kinds = sorted({path.name[len(group[0]) + 1:] for path in shots.glob(f"{group[0]}_*.tga")})
                for kind in kinds:
                    paths = [shots / f"{label}_{kind}" for label in group]
                    if not all(path.is_file() for path in paths):
                        failures.append(f"{name}/{pose}/{series}/{kind}: missing images")
                        continue
                    images = [read_tga(path) for path in paths]
                    hashes = [hashlib.sha256(image.tobytes()).hexdigest()[:16] for image in images]
                    reference = [i for i, label in enumerate(group) if label.endswith("m0")]
                    floor = max((differing_share(images[a], images[b]) for a in reference for b in reference if a < b), default=0.0)
                    shares = {label: differing_share(images[reference[0]], image) for label, image in zip(group, images)}
                    identical = len(set(hashes)) == 1
                    # Frozen: every mode 0 image of the group is the same image. A view
                    # that g_stopTime cannot hold still (effects keep their own clock,
                    # multiplayer has no single-client freeze) is judged against its floor instead.
                    frozen = len({hashes[i] for i in reference}) == 1
                    entry = {"identical": identical, "frozen": frozen, "hashes": dict(zip(group, hashes)),
                             "mode0_floor_share": floor, "differing_share_vs_first_mode0": shares,
                             "black": bool(max(int(image.max()) for image in images) == 0)}
                    result["groups"][f"{pose}/{series}/{kind}"] = entry
                    key = f"{name}/{pose}/{series}/{kind}"
                    if entry["black"]:
                        if kind == "cap.tga":
                            failures.append(f"{key}: black capture")
                    elif series == "exact" and frozen and kind == "cap.tga":
                        # A readback keeps accumulating history between frames, so it is
                        # held to the mode 0 floor below; a frozen capture frame must repeat.
                        if not identical:
                            failures.append(f"{key}: frozen images differ between modes {entry['hashes']}")
                    elif series == "exact" and kind == "cap.tga" and case.get("must_freeze"):
                        failures.append(f"{key}: the view did not hold still in mode 0 {entry['hashes']}")
                    elif max(shares.values()) > floor * 1.5 + 0.005:
                        failures.append(f"{key}: modes differ beyond the mode 0 floor ({max(shares.values()):.4f} vs {floor:.4f})")
                if not any(key.startswith(f"{pose}/{series}/cap") for key in result["groups"]):
                    failures.append(f"{name}/{pose}/{series}: no captures")
        report["cases"][name] = result
    report.update(status="fail" if failures else "pass", failures=failures)
    (output / "parity.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures}, indent=2))
    return bool(failures)


def bounds_lines(mode, cycles):
    lines = ["applyPerformancePreset quality", "com_maxfps 0", "r_swapInterval 0", "vid_restart", "wait 240",
             "noclip", "setviewpos 10200 -6800 40 0 170 0", "wait 60", "rendererFrameOwnershipSelfTest",
             f"r_vkRenderThread {mode}", "wait 120", "benchmarkViewSweep 360 20000", "renderThreadStats reset"]
    for cycle in range(1, cycles + 1):
        width, height = SIZES[cycle % len(SIZES)]
        lines += ["rendererFrameSnapshotStats reset", f"r_windowWidth {width}", f"r_windowHeight {height}",
                  "vid_restart partial", "wait 12", f"r_screenFraction {FRACTIONS[cycle % len(FRACTIONS)]}", "wait 12"]
        if cycle % CYCLE_PERIOD == 0:
            # Same window size and scale every period: comparable samples.
            lines += ["benchmarkViewSweep 360 20000", "rendererVulkanMemoryInfo", "wait 2"]
        lines += [f"rendererFrameSnapshotStats c{cycle}", "renderThreadStats", "renderThreadStats reset"]
    lines += ["rendererFrameSnapshotStats reset", "vid_restart", "wait 240", "rendererFrameOwnershipSelfTest",
              "renderThreadStats reset", "wait 240", "rendererFrameSnapshotStats restart", "renderThreadStats",
              "rendererVulkanMemoryInfo", "r_vkRenderThread 0", "wait 10", "quit"]
    return lines


def run_bounds(args):
    import psutil
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"check": "bounds", "snapshot_retain": args.retain, "cycles": args.cycles,
              "sizes": SIZES, "scene_scales": FRACTIONS, "vulkan_validation": bool(args.validation),
              "qualification": "hidden-window resource bounds; no physical qualification", "modes": {}}
    failures = []
    for mode in (0, 1, 2):		# mode 0 is the control: the restart path's own drift
        samples = {}

        def watch(process, log_path, stop, samples=samples):
            # Sample the process the moment each cycle's marker reaches the log.
            handle, offset, pending = psutil.Process(process.pid), 0, ""
            while not stop.is_set():
                if log_path.is_file():
                    with log_path.open("r", encoding="utf-8", errors="replace") as log:
                        log.seek(offset)
                        pending += log.read()
                        offset = log.tell()
                    *complete, pending = pending.split("\n")
                    for line in complete:
                        match = re.search(r"Frame snapshot stats (c\d+|restart):", line)
                        if match:
                            try:
                                memory = handle.memory_info()
                                samples[match[1]] = {"private_bytes": getattr(memory, "private", memory.rss),
                                                     "working_set": memory.rss,
                                                     "handles": handle.num_handles() if hasattr(handle, "num_handles") else 0,
                                                     "threads": handle.num_threads()}
                            except psutil.Error:
                                pass
                stop.wait(0.05)

        exit_code, text = launch(args, output / f"mode{mode}", bounds_lines(mode, args.cycles),
                                 "game/airdefense1", False, args.validation, watch)
        stats = labelled_stats(text)
        # One report per rendererVulkanMemoryInfo call: every period, then after the restart.
        vulkan_reports = []
        for match in re.finditer(r"Vulkan memory: heap (\d+) \S+ used=([\d.]+) MiB budget=[\d.]+ MiB blocks=\d+ \(([\d.]+) MiB\) "
                                 r"allocations=(\d+) \(([\d.]+) MiB\)", text):
            if match[1] == "0":
                vulkan_reports.append({"used_mib": 0.0, "block_mib": 0.0, "allocations": 0, "allocation_mib": 0.0})
            if vulkan_reports:
                total = vulkan_reports[-1]
                total["used_mib"] += float(match[2])
                total["block_mib"] += float(match[3])
                total["allocations"] += int(match[4])
                total["allocation_mib"] += float(match[5])
        cycles = {}
        for cycle in range(1, args.cycles + 1):
            entry = dict(stats.get(f"c{cycle}", {}))
            entry.update(samples.get(f"c{cycle}", {}))
            cycles[cycle] = entry
        result = {"exit_code": exit_code, "log_failures": log_failures(text),
                  "ownership_self_tests": text.count(OWNERSHIP),
                  "partial_restarts": args.cycles,
                  "validation_lines": [line.strip() for line in text.splitlines() if re.search(r"validation", line, re.I)][:6],
                  "restart": dict(stats.get("restart", {}), **samples.get("restart", {})),
                  "vulkan_memory_reports": vulkan_reports,
                  "cycles": cycles}
        key = f"mode{mode}"
        if exit_code != 0 or result["log_failures"] or result["ownership_self_tests"] < 2:
            failures.append(f"{key}: exit {exit_code}, {len(result['log_failures'])} log failures, "
                            f"{result['ownership_self_tests']} ownership self-tests")
        measured = [c for c in cycles.values() if "threaded" in c and "private_bytes" in c]
        if len(measured) != args.cycles:
            failures.append(f"{key}: {len(measured)} of {args.cycles} cycles reported and sampled")
        threaded = sum(c.get("threaded", 0) for c in cycles.values())
        result["threaded_frames"] = threaded
        if (threaded < args.cycles * 10) if mode else threaded:
            failures.append(f"{key}: {threaded} threaded frames across the cycles")
        # Predeclared bounds. The caches: 64 MiB / 32,768 index entries and
        # 128 MiB / 32,768 vertex entries at every cycle.
        for cycle, c in cycles.items():
            snapshot = c.get("snapshot", {})
            if (snapshot.get("indexResidentBytes", 0) > 64 << 20 or snapshot.get("vertexResidentBytes", 0) > 128 << 20
                    or snapshot.get("indexEntries", 0) > 32768 or snapshot.get("vertexEntries", 0) > 32768):
                failures.append(f"{key}: retained cache over its bound at cycle {cycle}: {snapshot}")
                break
        # The process. One sample swings by about 150 MiB (image reloads are in
        # flight when a cycle's marker is logged), so compare medians over whole
        # periods, each of which visits every window size and scale once: the
        # last period against the second (the first warms up). Private bytes
        # within 5%, and within 64 MiB of the mode 0 control's own drift (checked
        # below); handles within 16.
        periods = args.cycles // CYCLE_PERIOD

        def period_median(field, period):
            values = [cycles[c][field] for c in range((period - 1) * CYCLE_PERIOD + 1, period * CYCLE_PERIOD + 1)
                      if field in cycles[c]]
            return statistics.median(values) if values else 0

        medians = [period_median("private_bytes", period) for period in range(1, periods + 1)]
        if len(measured) == args.cycles and medians[1]:
            growth = medians[-1] - medians[1]
            handles = period_median("handles", periods) - period_median("handles", 2)
            result["growth"] = {"from_period": 2, "to_period": periods, "private_bytes": growth, "handles": handles,
                                "private_bytes_share": growth / medians[1], "period_median_private_bytes": medians,
                                "period_median_vertex_cache_bytes": [
                                    statistics.median(cycles[c].get("snapshot", {}).get("vertexResidentBytes", 0)
                                                      for c in range((period - 1) * CYCLE_PERIOD + 1, period * CYCLE_PERIOD + 1))
                                    for period in range(1, periods + 1)]}
            if growth > 0.05 * medians[1]:
                failures.append(f"{key}: private bytes grew {growth / 2**20:.0f} MiB between periods 2 and {periods}")
            if handles > 16:
                failures.append(f"{key}: handles grew {handles} between periods 2 and {periods}")
        else:
            failures.append(f"{key}: not enough samples for the growth comparison")
        # Vulkan allocations at the same two cycles: within 5% and 32 MiB.
        periods = args.cycles // CYCLE_PERIOD
        if len(vulkan_reports) != periods + 1:
            failures.append(f"{key}: {len(vulkan_reports)} Vulkan memory reports, expected {periods + 1}")
        else:
            first, last = vulkan_reports[1], vulkan_reports[periods - 1]
            vulkan_growth = last["allocation_mib"] - first["allocation_mib"]
            result["growth"] = dict(result.get("growth", {}), vulkan_allocation_mib=vulkan_growth,
                                    vulkan_allocations=last["allocations"] - first["allocations"])
            if vulkan_growth > 32 or vulkan_growth > 0.05 * first["allocation_mib"]:
                failures.append(f"{key}: Vulkan allocations grew {vulkan_growth:.1f} MiB between cycles "
                                f"{2 * CYCLE_PERIOD} and {periods * CYCLE_PERIOD}")
        report["modes"][key] = result
    control = report["modes"]["mode0"].get("growth", {}).get("private_bytes")
    for key in ("mode1", "mode2"):
        growth = report["modes"][key].get("growth", {})
        if control is not None and "private_bytes" in growth:
            growth["private_bytes_over_mode0"] = growth["private_bytes"] - control
            if growth["private_bytes_over_mode0"] > 64 << 20:
                failures.append(f"{key}: private bytes grew {growth['private_bytes_over_mode0'] / 2**20:.0f} MiB more than mode 0")
    report.update(status="fail" if failures else "pass", failures=failures)
    (output / "bounds.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures,
                      "growth": {k: v.get("growth") for k, v in report["modes"].items()}}, indent=2))
    return bool(failures)



def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", choices=("compare", "parity", "bounds"), default="compare")
    parser.add_argument("--basepath", default="")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install", help="parity/bounds: staged runtime")
    parser.add_argument("--cases", default=",".join(PARITY_CASES), help="parity: comma-separated case names")
    parser.add_argument("--cycles", type=int, default=100, help="bounds: resize and scene-scale cycles per mode")
    parser.add_argument("--validation", type=int, choices=(0, 1), default=1, help="parity/bounds: r_vkValidation")
    parser.add_argument("--visible", action="store_true", help="parity/bounds: show the window")
    parser.add_argument("--timeout", type=int, default=1500, help="parity/bounds: seconds per launch")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--retain", "--retain-indexes", dest="retain", choices=("0", "1", "2"), default="2",
                        help="r_vkSnapshotRetain: 0 per-view copies, 1 index streams, 2 index and static vertex streams")
    parser.add_argument("--sample-ms", type=int, default=8000)
    parser.add_argument("--warmup-ms", type=int, default=4000)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args(argv)
    if args.check == "parity":
        unknown = [name for name in args.cases.split(",") if name not in PARITY_CASES]
        if unknown:
            parser.error(f"unknown parity cases: {unknown}")
        return run_parity(args)
    if args.check == "bounds":
        if args.cycles < 3 * CYCLE_PERIOD:
            parser.error(f"bounds needs at least {3 * CYCLE_PERIOD} cycles")
        return run_bounds(args)
    if not args.basepath:
        parser.error("--basepath is required for the mode comparison")
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
    bench_args += ["--set-cvar", f"r_vkSnapshotRetain={args.retain}"]
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
              "snapshot_retain": args.retain, "retained_indexes": "0" if args.retain == "0" else "1", "display": [args.width, args.height], "sample_ms": args.sample_ms,
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
