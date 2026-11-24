#!/usr/bin/env python3
"""Measure high-refresh presentation (HF-03 to HF-08) in live gameplay without device input.

Runs staged clients on retail maps. The cheat-gated in_presentationViewProbe feeds
synthetic mouse counts through the ordinary mouse path at a constant rate, and
the extended g_presentationClockCapture records, for every prepared draw, the
host time, the drawn camera yaw, the authoritative view yaw, the late-look input
sample and owner counters. Scenarios:

- look: single player, late look off and on;
- thirdperson: the third-person camera on the tic and interpolated;
- death, ragdoll, debris: the death camera, AF ragdoll blending and client debris;
- vehicle: a spawned walker entered by script, its joint-bound cockpit eye
  turning under the probe;
- flash (HF-08): com_latencyFlash, the white bar click-to-photon sensors watch;
  latencyFlashTest arms it as an attack press would, and each arming must draw
  exactly one frame;
- flick: rapid turns, the probe at ten times its rate (2200 deg/s), late look
  off and on;
- zoom: a railgun held zoomed by the cheat-gated in_presentationViewProbeZoom,
  late look off and on (zoomed look turns at pm_zoomedSlow, 50%);
- lift: riding airdefense2's func_elevator_3 up a floor, late look off and on;
- subview (HF-06): views rendered inside each draw besides the main one, with
  late look on: tram1's bigScreen monitor (a cameraTarget remote view) and
  airdefense1's portal sky. r_rendererMetrics 2 runs for a moment so the case
  reports how many views and subview/remote packets each frame drew;
- combined (HF-06): one session with temporal AA, shadow maps, late look,
  retained UI and audio all on, captured live, after a save and load (a map
  reset), after vid_restart (new renderer resources) and after an AVI capture
  (the fixed-tic path); every capture must record draws and no gameplay, and
  screenshots under late look and TAA are kept for review;
- vr (HF-06): stereo frames through the in-tree OpenXR test runtime (selected
  for that process only by XR_RUNTIME_JSON), so both eyes of every draw pass the
  gameplay guard; the runtime's log counts the frames it received;
- demo (HF-06): a render demo recorded while late look turns and the capture
  runs, then played back by timeDemoQuit through the demo bypass in the same
  session;
- mphost: a loopback listen host's own player, late look off and on;
- mpclient: a remote client of a loopback listen host (client prediction), late
  look off and on;
- mploss: the remote client again with net_forceLatency 40, net_forceJitter 10
  and net_forceDrop 5 on the client (each way), so delayed, reordered and lost
  snapshots and commands make prediction replay and recover, late look off and
  on;
- spectate: a loopback listen host with two bots, playing, spectating free and
  following a bot; remote bodies join the presentation pass only while the
  local player spectates (listed= counts the pass);
- hitch (HF-07): once a second one frame stalls 100 ms
  (com_presentationStallProbe), so the game catches up several ticks; the drawn
  view must keep turning one way through it, late look off and on;
- menu (HF-07): the main menu, no map, at each --fps cap; menus follow
  com_maxfps like gameplay;
- hidden (HF-07): the same turn in a hidden window (r_hiddenWindow 1), which
  must present at about 60 Hz whatever the cap;
- minimized (HF-07): the same turn after the cheat-gated windowMinimizeTest
  minimizes the window, as leaving full screen does; it must also present at
  about 60 Hz, on every platform;
- pacing (HF-07): late look on at each --fps cap, judged against the declared
  240 Hz budget. Only --reference runs (a visible foreground window on the
  reference machine) qualify; every other run reports engine-side pacing;
- sensor (HF-08): an operator-driven session for click-to-photon sensors. The
  operator's own mouse works (the other cases never read devices), late look and
  com_latencyFlash are on, and for --sensor-seconds every fire press flashes the
  left edge; the snapshot then reports the engine's press-to-swap time for those
  same presses, to subtract from the sensor's readings;
- review (HF-08): operator-attended sessions for the by-eye review, best run
  --fullscreen on the reference machine. On-screen captions announce each
  segment while late look is switched off and on: steady and rapid turns under
  temporal AA, firing (muzzle flash, its light, tracers) through the
  cheat-gated in_presentationViewProbeAttack, third person, a ragdoll with
  barrel debris and the death camera on airdefense1; airdefense1's opening
  cinematic with its cuts; tram1's monitor. The engine-side checks still apply;
  the verdicts go in review-checklist.md, which the run writes for the
  operator.

For turning scenarios it reports the authoritative turn rate (input reaches the
game exactly once), input-to-draw latency against the input line, the drawn yaw
against the sampled input, and the frame-to-frame residual. Every scenario fails
if a prepared draw ran gameplay (event dispatch, scripts, physics or effect
spawns), which g_presentationClockCapture counts as gameplay=. Effects BSE
starts while the renderer services it (particle impacts, nested segments) are
reported apart as bse=; they start once per particle or effect step.

This is engine-side evidence on a hidden or visible window. It does not include
display scan-out, compositor or panel latency, and it is not physical input
qualification (HF-07/HF-08). MP cases bind 127.0.0.1 only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = re.compile(
    r"presentationClockSample rt=(-?\d+) t=(-?\d+) f=([-\d.]+) hrt=([-\d.]+) step=(\d+) hz=(\d+) phase=([-\d.]+)"
    r" drawYaw=([-\d.]+) authYaw=([-\d.]+) look=(\d) lookYaw=([-\d.]+) inYaw=([-\d.]+)"
    r"(?: debris=(\d+) afBlends=(\d+))?(?: listed=(\d+) afFrames=(\d+))?(?: gameplay=(\d+))?(?: bse=(\d+))?(?: drawZ=([-\d.]+))?(?: prepMs=([-\d.]+))?")
TURNING = ("look", "thirdperson", "flick", "hidden", "minimized", "hitch", "zoom", "lift", "demo", "mphost", "mpclient", "mploss", "pacing")
RENDERER = re.compile(r"rendererMetrics summary tier=\S+ fe=(\d+)ms .*? be=(\d+)ms present=(\d+)ms gpu=(\S+) views=(\d+)")
VIEWS = re.compile(r"rendererMetrics frame=\d+ .*? views=(\d+) ents=.*?categories\(world=(\d+) subview=(\d+) remote=(\d+)")
THROTTLE = re.compile(r"Presentation throttle snapshot hf(?: - )?pacing: waits=(\d+) late p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms"
                      r"(?:, spin mean=([\d.]+) ms)?")
# engine-side input to swap (HF-08): the draw's first late-look sample, and the press behind a drawn flash
LATENCY = re.compile(r"Presentation latency snapshot hf(?: - )?pacing: lookSamples=(\d+) sampleToSwap "
                     r"(?:p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms|none); flashes=(\d+) pressToSwap "
                     r"(?:p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms|none)")
# scenarios that end with framePacingSnapshot
SNAPSHOTS = ("pacing", "hidden", "minimized", "look", "flash", "sensor")
# Vulkan with r_vkPresentTiming 1: when frames reached the display (VK_KHR_present_wait)
PRESENTED = re.compile(r"Presented frames hf(?: - )?pacing: frames=(\d+) interval (?:p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms|none);"
                       r" queue (?:p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms|none); replaced=(\d+) dropped=(\d+)")
IMPAIR = re.compile(r"NET_IMPAIR port=(\d+) dir=(\w+) offered=(\d+) dropped=(\d+) duplicated=(\d+) released=(\d+) reordered=(\d+)")
DUMP = re.compile(r"^presentationClock: (\d+) samples$", re.M)
TIMEDEMO = re.compile(r"(\d+) frames rendered in ([\d.]+) seconds = ([\d.]+) fps")
# HF-07 budgets (presentation-owners.md): draw interval p95/p99 and late-look sample-to-draw, in ms
BUDGETS = {240: {"intervalP95": 4.6, "intervalP99": 6.3, "lookSampleToDrawP95": 1.0}}
SNAPSHOT = re.compile(r"Frame pacing snapshot hf(?: - )?pacing \((SP|MP)\): bound=([\w-]+), samples=(\d+), present=([\d.]+) ms \(([\d.]+) Hz\)"
                      r", p50=([\d.]+) ms, p95=([\d.]+) ms, p99=([\d.]+) ms, max=([\d.]+) ms(?:.*?refresh=([\d.]+) Hz)?")
# the presentation cap the engine applied (after the window policy), and the mean frame interval since framePacingReset
SNAPSHOT_CAP = re.compile(r"Frame pacing snapshot hf(?: - )?pacing \((?:SP|MP)\): .*?, cap=(\d+), refresh=")
WINDOW = re.compile(r"Frame timing window \((?:SP|MP)\): samples=(\d+), span_ms=(\d+), mean_ms=([\d.]+)")


def unwrap(values):
    out, offset, previous = [], 0.0, None
    for value in values:
        if previous is not None:
            step = value - previous
            if step > 180.0:
                offset -= 360.0
            elif step < -180.0:
                offset += 360.0
        out.append(value + offset)
        previous = value
    return out


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return slope, my - slope * mx


def step_jitter(samples):
    """How far each draw's yaw step departs from the previous step's rate (degrees).

    Zero for any smooth turn whatever its rate, so it also judges views that do
    not follow the input line (a vehicle turning at its own speed); a view stepping
    at 60 Hz between faster draws alternates still and doubled steps.
    """
    hrt = [s["hrt"] for s in samples]
    yaw = unwrap([s["drawYaw"] for s in samples])
    out = []
    for i in range(2, len(samples)):
        dt, previous_dt = hrt[i] - hrt[i - 1], hrt[i - 1] - hrt[i - 2]
        if dt > 0 and previous_dt > 0:
            out.append(abs((yaw[i] - yaw[i - 1]) - (yaw[i - 1] - yaw[i - 2]) * dt / previous_dt))
    return {"p50": percentile(out, 0.5), "p95": percentile(out, 0.95), "max": max(out)} if out else None


def tick_linearity(samples, key="drawYaw"):
    """Separate presentation error from the simulated motion (degrees).

    Draws inside one tick interpolate between the same two samples, so without
    late look the drawn yaw is linear in the draw's phase: the residual from a
    per-tick line is presentation error. The change of that line from tick to
    tick is the simulated motion itself (input arriving per tick, vehicle turns).
    """
    yaw = unwrap([s[key] for s in samples]) if key == "drawYaw" else [s[key] for s in samples]
    ticks = {}
    for i, s in enumerate(samples):
        ticks.setdefault(s["t"], []).append(i)
    residual, slopes = [], []
    for indices in ticks.values():
        if len(indices) < 3:
            continue
        f = [samples[i]["f"] for i in indices]
        y = [yaw[i] for i in indices]
        mf, my = sum(f) / len(f), sum(y) / len(y)
        sff = sum((a - mf) ** 2 for a in f)
        if sff < 1e-6:
            continue
        slope = sum((a - mf) * (b - my) for a, b in zip(f, y)) / sff
        residual += [abs(b - (my + slope * (a - mf))) for a, b in zip(f, y)]
        slopes.append(slope)
    if len(slopes) < 2:
        return None
    change = [abs(slopes[i] - slopes[i - 1]) for i in range(1, len(slopes))]
    return {"ticks": len(slopes), "intraTickResidualP95": percentile(residual, 0.95),
            "perTickChangeP50": percentile(change, 0.5), "perTickChangeP95": percentile(change, 0.95)}


def analyse(samples, expected_rate):
    hrt = [s["hrt"] for s in samples]
    drawn = unwrap([s["drawYaw"] for s in samples])
    auth = unwrap([s["authYaw"] for s in samples])
    # The first draw of each tic sees the usercmd that was built moments before it
    # (com_asyncInput 0 builds commands as the game runs them), so those draws
    # pin the input line: authoritative yaw = input at that moment.
    first = [i for i in range(1, len(samples)) if samples[i]["t"] != samples[i - 1]["t"]]
    slope, intercept = fit([hrt[i] for i in first], [auth[i] for i in first])
    if abs(slope) < 1e-6:
        return {"draws": len(samples), "tics": len(first), "note": "the authoritative view did not turn"}
    # hrt is in ms, so the slope is degrees per ms and this is already ms
    latency = [((slope * hrt[i] + intercept) - drawn[i]) / slope for i in range(len(samples))]
    residual = [drawn[i] - (slope * hrt[i] + intercept) for i in range(len(samples))]
    jitter = [abs(residual[i] - residual[i - 1]) for i in range(1, len(samples))]
    intervals = [hrt[i] - hrt[i - 1] for i in range(1, len(samples))]
    result = {
        "draws": len(samples),
        "tics": len(first),
        "lookActiveFraction": sum(s["look"] for s in samples) / len(samples),
        "authoritativeRateDegPerSec": slope * 1000.0,
        "expectedRateDegPerSec": expected_rate,
        "rateError": (abs(slope * 1000.0) - abs(expected_rate)) / abs(expected_rate) if expected_rate else None,
        "latencyMs": {"mean": sum(latency) / len(latency), "p50": percentile(latency, 0.5),
                      "p95": percentile(latency, 0.95), "max": max(latency), "min": min(latency)},
        "residualStepDeg": {"p50": percentile(jitter, 0.5), "p95": percentile(jitter, 0.95), "max": max(jitter)},
        "drawIntervalMs": {"p50": percentile(intervals, 0.5), "p95": percentile(intervals, 0.95), "p99": percentile(intervals, 0.99)},
        "latencyNote": "relative to the input line fitted at usercmd build time; both modes carry the same build-to-draw bias",
    }
    # Late look samples the generator at the draw, so the drawn yaw can be checked
    # against the input it actually saw: a constant view delta, no delay.
    active = [i for i in range(len(samples)) if samples[i]["look"]]
    if len(active) >= 16:
        sampled = unwrap([samples[i]["inYaw"] for i in active])
        drawn_active = unwrap([samples[i]["drawYaw"] for i in active])
        offsets = [d - s for d, s in zip(drawn_active, sampled)]
        median = percentile(offsets, 0.5)
        spread = [abs(o - median) for o in offsets]
        result["drawnVsSampledInputDeg"] = {"p50": percentile(spread, 0.5), "p95": percentile(spread, 0.95), "max": max(spread)}
        result["buildToDrawBiasMs"] = -result["latencyMs"]["p50"]
        result["lookSampleToDrawMs"] = {k: v / abs(slope) for k, v in result["drawnVsSampledInputDeg"].items()}
    return result


def pacing(samples, fps, text, reference):
    """HF-07: the prepared-draw cadence at one cap, against the declared budget."""
    hrt = [s["hrt"] for s in samples]
    intervals = [hrt[i] - hrt[i - 1] for i in range(1, len(samples))]
    ticks = [samples[i]["t"] - samples[i - 1]["t"] for i in range(1, len(samples)) if samples[i]["t"] != samples[i - 1]["t"]]
    p50 = percentile(intervals, 0.5)
    result = {
        "cap": fps,
        "presentedHz": 1000.0 / p50 if p50 > 0 else None,
        "intervalMs": {"p50": p50, "p95": percentile(intervals, 0.95), "p99": percentile(intervals, 0.99), "max": max(intervals)},
        # a tick the game ran but no draw showed (expected below 60 Hz)
        "skippedTicks": sum(1 for d in ticks if d > 25),
        "ticks": len(ticks),
        "presentedBelowCap": bool(fps) and p50 > 0 and 1000.0 / p50 < 0.9 * fps,
        "reference": reference,
    }
    snapshot = SNAPSHOT.search(text)
    if snapshot:
        # the engine's own frame-start intervals at 50 us resolution, and the display's refresh
        result["framePacingSnapshot"] = {"bound": snapshot[2], "samples": int(snapshot[3]), "presentMs": float(snapshot[4]),
                                         "hz": float(snapshot[5]), "p50": float(snapshot[6]), "p95": float(snapshot[7]),
                                         "p99": float(snapshot[8]), "max": float(snapshot[9]),
                                         "refreshHz": float(snapshot[10]) if snapshot[10] else None}
    presented = PRESENTED.search(text)
    if presented and presented[2]:
        # display-side pacing: the interval between frames reaching the display, and how long
        # each waited after vkQueuePresentKHR (r_vkPresentTiming)
        result["presentedIntervalMs"] = {"frames": int(presented[1]), "p50": float(presented[2]), "p95": float(presented[3]),
                                         "p99": float(presented[4]), "max": float(presented[5]),
                                         "replaced": int(presented[10]), "dropped": int(presented[11])}
        result["presentQueueMs"] = {"p50": float(presented[6]), "p95": float(presented[7]), "p99": float(presented[8]),
                                    "max": float(presented[9])}
    throttle = THROTTLE.search(text)
    if throttle:
        # the limiter's own precision: how late each wait woke against its target, and how long it busy-waited
        result["throttleLateMs"] = {"waits": int(throttle[1]), "p50": float(throttle[2]), "p95": float(throttle[3]),
                                    "p99": float(throttle[4]), "max": float(throttle[5]),
                                    "spinMean": float(throttle[6]) if throttle[6] else None}
    budget = BUDGETS.get(fps)
    if budget is None:
        result["verdict"] = "reported only"
        return result
    misses = []
    if result["intervalMs"]["p95"] > budget["intervalP95"]:
        misses.append(f"interval p95 {result['intervalMs']['p95']:.2f} > {budget['intervalP95']} ms")
    if result["intervalMs"]["p99"] > budget["intervalP99"]:
        misses.append(f"interval p99 {result['intervalMs']['p99']:.2f} > {budget['intervalP99']} ms")
    if result["skippedTicks"]:
        misses.append(f"{result['skippedTicks']} skipped ticks")
    shown = result.get("presentedIntervalMs")
    if shown:
        # the cadence frames reached the display is held to the same budget
        if shown["p95"] > budget["intervalP95"]:
            misses.append(f"presented interval p95 {shown['p95']:.2f} > {budget['intervalP95']} ms")
        if shown["p99"] > budget["intervalP99"]:
            misses.append(f"presented interval p99 {shown['p99']:.2f} > {budget['intervalP99']} ms")
    result["budgetMisses"] = misses
    if not reference:
        result["verdict"] = "not qualifying: engine-side run (pass --reference only for a visible foreground window on the reference machine)"
    else:
        result["verdict"] = "within budget" if not misses else "over budget"
    return result


def parse(text):
    return [{"rt": int(m[0]), "t": int(m[1]), "f": float(m[2]), "hrt": float(m[3]), "step": int(m[4]),
             "hz": int(m[5]), "phase": float(m[6]), "drawYaw": float(m[7]), "authYaw": float(m[8]),
             "look": int(m[9]), "lookYaw": float(m[10]), "inYaw": float(m[11]),
             "debris": int(m[12] or 0), "afBlends": int(m[13] or 0),
             "listed": int(m[14] or 0), "afFrames": int(m[15] or 0), "gameplay": int(m[16] or 0),
             "bse": int(m[17] or 0), "drawZ": float(m[18] or 0.0),
             "prepMs": float(m[19]) if m[19] else None}
            for m in SAMPLE.findall(text)]


def summarise(name, scenario, fps, exit_code, started, text, expected_rate, reference=False):
    samples = parse(text)
    case = {"case": name, "scenario": scenario, "fps": fps, "exit": exit_code,
            "seconds": round(time.monotonic() - started, 1), "samples": len(samples),
            "maxDebris": max((s["debris"] for s in samples), default=0),
            "afBlends": sum(s["afBlends"] for s in samples),
            "afFrames": sum(s["afFrames"] for s in samples),
            "maxListed": max((s["listed"] for s in samples), default=0),
            "gameplayInDraws": sum(s["gameplay"] for s in samples),
            # effects BSE started while servicing inside RenderScene (particle
            # impacts, nested segments): once per particle or effect step, expected
            "bseEffectsInDraws": sum(s["bse"] for s in samples)}
    if len(samples) >= 64 and scenario in TURNING:
        case.update(analyse(samples, expected_rate))
    if len(samples) >= 64:
        case["drawnStepJitterDeg"] = step_jitter(samples)
        case["tickLinearityDeg"] = tick_linearity(samples)
        heights = [s["drawZ"] for s in samples]
        if max(heights) - min(heights) > 1.0:
            # a carrier moved the eye: the same split for its height, in units
            case["eyeHeightRange"] = [min(heights), max(heights)]
            case["tickLinearityHeight"] = tick_linearity(samples, "drawZ")
    if len(samples) >= 64 and scenario == "hitch":
        # catch-up: several ticks run in one frame after a stall; the drawn view
        # keeps turning the way the input does (no step backwards) and the
        # stalls show as long draw intervals
        yaw = unwrap([s["drawYaw"] for s in samples])
        steps = [yaw[i] - yaw[i - 1] for i in range(1, len(yaw))]
        sign = -1.0 if expected_rate < 0 else 1.0
        intervals = [samples[i]["hrt"] - samples[i - 1]["hrt"] for i in range(1, len(samples))]
        case["hitch"] = {"stalls": sum(1 for d in intervals if d > 80.0),
                         "backsteps": sum(1 for d in steps if d * sign < -0.001),
                         "largestStepDeg": max(abs(d) for d in steps)}
    if len(samples) >= 64 and scenario in ("hidden", "minimized"):
        # the background policy: a window that shows nothing is capped at 60 whatever the cap
        case["pacing"] = pacing(samples, 60, text, False)
    if scenario == "minimized":
        # Judged on the engine's own frame rate: the OS may throttle a minimized
        # window further and its scene need not interpolate, so the capture can be
        # empty. The cap must read 60 and the frames must not come faster.
        case["minimizedReported"] = "windowMinimizeTest: minimized=1 shown=0" in text
        cap, window = SNAPSHOT_CAP.search(text), WINDOW.search(text)
        case["minimizedPacing"] = {"cap": int(cap[1]) if cap else None,
                                   "frames": int(window[1]) if window else 0,
                                   "hz": 1000.0 / float(window[3]) if window and float(window[3]) > 0 else None}
    kept = re.search(r'"win_keepTimerResolution" is:"(\d)"', text)
    if kept:
        case["timerResolutionKeptWhenOccluded"] = kept[1] == "1"
    if len(samples) >= 64 and scenario == "pacing":
        case["pacing"] = pacing(samples, fps, text, reference)
        budget = BUDGETS.get(fps)
        look = case.get("lookSampleToDrawMs")
        if budget and look and look["p95"] > budget["lookSampleToDrawP95"]:
            case["pacing"]["budgetMisses"].append(f"late-look sample-to-draw p95 {look['p95']:.3f} > {budget['lookSampleToDrawP95']} ms")
            if reference:
                case["pacing"]["verdict"] = "over budget"
    case["errors"] = [line for line in text.splitlines()
                      if re.search(r"\b(ERROR|FATAL)\b|Assertion failed", line)][:20]
    case["gameplayReports"] = [line for line in text.splitlines() if line.startswith("presentationGameplay:")][:8]
    prep = [s["prepMs"] for s in samples if s["prepMs"] is not None]
    if prep:
        # preparing the draw: camera, late look, entity poses and client debris
        case["prepareMs"] = {"p50": percentile(prep, 0.5), "p95": percentile(prep, 0.95), "max": max(prep)}
    summaries = RENDERER.findall(text)
    if summaries:
        fe, be, present, gpu, views_ = summaries[-1]
        case["rendererSummary"] = {"frontEndMs": int(fe), "backEndMs": int(be), "presentMs": int(present), "gpu": gpu, "views": int(views_)}
    views = VIEWS.findall(text)
    if views:
        case["rendererViews"] = {"frames": len(views), "maxViews": max(int(v[0]) for v in views),
                                 "maxSubviewPackets": max(int(v[2]) for v in views),
                                 "maxRemotePackets": max(int(v[3]) for v in views)}
    if scenario == "mploss":
        # the cheat-gated conditions survived the join only if the port shows them
        lines = IMPAIR.findall(text)
        case["netImpairment"] = {direction: {"offered": int(o), "dropped": int(d), "reordered": int(r)}
                                 for _, direction, o, d, _, _, r in lines} if lines else None
    if scenario == "combined":
        # each g_presentationClockCapture dump in order: live, after load, after
        # vid_restart, after the AVI capture
        starts = [m.end() for m in DUMP.finditer(text)]
        segments = []
        for index, start in enumerate(starts):
            end = starts[index + 1] if index + 1 < len(starts) else len(text)
            part = parse(text[start:end])
            segments.append({"samples": len(part), "gameplay": sum(s["gameplay"] for s in part),
                             "lookActive": sum(s["look"] for s in part)})
        case["segments"] = segments
    if scenario == "menu":
        snapshot = SNAPSHOT.search(text)
        case["menuPacing"] = {"bound": snapshot[2], "samples": int(snapshot[3]), "hz": float(snapshot[5]),
                              "p50": float(snapshot[6]), "p95": float(snapshot[7]), "p99": float(snapshot[8])} if snapshot else None
    if scenario == "flash":
        case["latencyFlashes"] = text.count("latencyFlash: drawn at ")
    throttle = THROTTLE.search(text)
    if throttle and scenario != "pacing":
        # how late the limiter's waits woke against their targets (pacing cases carry it in "pacing")
        case["throttleLateMs"] = {"waits": int(throttle[1]), "p50": float(throttle[2]), "p95": float(throttle[3]),
                                  "p99": float(throttle[4]), "max": float(throttle[5]),
                                  "spinMean": float(throttle[6]) if throttle[6] else None}
    latency = LATENCY.search(text)
    if latency:
        def spread(first):
            return ({"p50": float(latency[first]), "p95": float(latency[first + 1]), "p99": float(latency[first + 2]),
                     "max": float(latency[first + 3])} if latency[first] else None)
        case["inputToSwapMs"] = {"lookSamples": int(latency[1]), "lookSampleToSwap": spread(2),
                                 "flashes": int(latency[6]), "flashPressToSwap": spread(7)}
    if scenario == "demo":
        played = TIMEDEMO.search(text)
        case["timeDemo"] = {"frames": int(played[1]), "seconds": float(played[2]), "fps": float(played[3])} if played else None
    return case


def reanalyse(output: Path, probe_rate: float) -> int:
    report = json.loads((output / "report.json").read_text(encoding="utf-8"))
    for case in report["cases"]:
        log = output / case["case"] / "baseoq4/logs/openq4.log"
        samples = parse(log.read_text(encoding="utf-8", errors="replace"))
        if len(samples) >= 64 and case.get("scenario") in TURNING:
            scale = {"zoom": 0.5, "flick": 10.0}.get(case.get("scenario"), 1.0)
            case.update(analyse(samples, -probe_rate * 5.0 * 0.022 * scale))
        if len(samples) >= 64:
            case["drawnStepJitterDeg"] = step_jitter(samples)
            case["tickLinearityDeg"] = tick_linearity(samples)
            heights = [s["drawZ"] for s in samples]
            if max(heights) - min(heights) > 1.0:
                case["eyeHeightRange"] = [min(heights), max(heights)]
                case["tickLinearityHeight"] = tick_linearity(samples, "drawZ")
        if len(samples) >= 64 and case.get("scenario") == "pacing":
            case["pacing"] = pacing(samples, case["fps"], log.read_text(encoding="utf-8", errors="replace"),
                                    report.get("reference", False))
        print(json.dumps({k: case[k] for k in case if k not in ("errors", "gameplayReports")}), flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 0


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def caption(text: str) -> str:
    """An on-screen caption: con_noPrint 0 shows console prints as notify lines."""
    return f'echo "{text}"'


def review_segments(args, probe: str) -> list[dict]:
    """HF-08's by-eye review: what each session shows and what the operator judges.

    Each session switches late look off and on under a caption, so the same
    motion is seen both ways. Firing uses in_presentationViewProbeAttack, which
    holds the attack button as the zoom probe holds zoom.
    """
    rate = args.probe_rate
    shown = ["con_noPrint", "0", "con_notifyTime", "8", "r_temporalAA", "1", "ui_autoSwitch", "1"]
    play = [
        "god", "notarget", "noclip", f"setviewpos {args.viewpos}", "give weapon_machinegun", "waitMsec 2000",
        # the whole session, up to the capture's 8192 draws
        f"g_presentationClockCapture {min(8192, args.samples * 16)}",
        # the first screenshot logs the post-AA resources once; take it before any caption so the log
        # cannot push a caption off the notify lines
        "screenshot", "waitMsec 500",
        caption("Review 1 of 6: steady turn under temporal AA, late look OFF"), "in_presentationView 0", probe, "waitMsec 6000",
        caption("Review 1 of 6: late look ON, the same turn"), "in_presentationView 1", "waitMsec 6000",
        caption("Review 2 of 6: rapid turns, late look OFF"), "in_presentationView 0",
        f"in_presentationViewProbe {rate * 10:g}", "waitMsec 4000",
        caption("Review 2 of 6: rapid turns, late look ON"), "in_presentationView 1", "waitMsec 4000",
        caption("Review 3 of 6: firing while turning, late look OFF"), "in_presentationView 0", "give ammo",
        f"in_presentationViewProbe {rate / 4:g}", "in_presentationViewProbeAttack 1", "waitMsec 1000", "screenshot", "waitMsec 3000",
        caption("Review 3 of 6: firing while turning, late look ON"), "in_presentationView 1", "waitMsec 2000",
        "screenshot", "waitMsec 2000",
        "in_presentationViewProbeAttack 0",
        caption("Review 4 of 6: third person, late look stands down"), "pm_thirdPerson 1",
        f"in_presentationViewProbe {rate:g}", "waitMsec 6000", "pm_thirdPerson 0",
        caption("Review 5 of 6: a ragdoll and barrel debris, late look ON"), f"in_presentationViewProbe {rate / 20:g}",
        "spawn monster_strogg_marine name hf_review_ragdoll", "spawn launching_barrel_small name hf_review_barrel", "waitMsec 1500",
        "damage hf_review_ragdoll 5000 damage_triggerhurt_1000", "damage hf_review_barrel 1000 damage_triggerhurt_1000",
        "waitMsec 1000", "screenshot", "waitMsec 5000",
        # dump what the session captured so far (a count below the samples taken dumps on the next draw),
        # then capture the death camera on its own
        "g_presentationClockCapture 1", "waitMsec 500", "g_presentationClockCapture 0", "waitMsec 100",
        f"g_presentationClockCapture {args.samples}",
        caption("Review 6 of 6: the death camera"), "in_presentationViewProbe 0", "god", "noclip", "testDeath", "waitMsec 6000",
    ]
    return [
        {"name": "review-play", "mode": "sp", "cvars": shown + ["pm_thirdPersonDeath", "1"], "lines": play,
         "judge": [
             "1: the steady turn is smooth in both modes, and with late look ON it follows the mouse sooner; TAA shows no ghosting or doubled edges",
             "2: rapid turns: no stepping or tearing beyond the off case; with late look ON the view does not overshoot or settle back",
             "3: the muzzle flash, its light and the gun stay together in both modes; tracers and impacts start where the shot left",
             "4: third person: the camera moves smoothly; nothing jumps when late look is ON (it stands down)",
             "5: the ragdoll falls smoothly (no 60 Hz stepping); barrel debris and its trails move smoothly",
             "6: the death camera orbits smoothly, without a jump at the start",
         ]},
        {"name": "review-cinematic", "mode": "sp", "cvars": shown + ["g_autoSkipCinematics", "0", "in_presentationView", "1"],
         "lines": [caption("Review: the opening cinematic; camera moves are smooth and every cut is clean"),
                   f"g_presentationClockCapture {args.samples}", "waitMsec 10000", "screenshot", "waitMsec 30000"],
         "judge": ["camera moves are smooth at the display rate", "every authored cut is a clean cut, with no blend or slide between shots"]},
        {"name": "review-monitor", "mode": "sp:game/tram1", "cvars": shown + ["in_presentationView", "1"],
         "lines": ["god", "notarget", "noclip", "setviewpos 15915 -1201 169 16 -40 0", "waitMsec 2500",
                   "screenshot", "waitMsec 500", f"g_presentationClockCapture {args.samples}",
                   caption("Review: the monitor's remote camera; what it shows moves smoothly"), "waitMsec 2000",
                   "screenshot", "waitMsec 6000"],
         "judge": ["the remote camera image updates smoothly; nothing in it steps at 60 Hz while the main view is smooth"]},
    ]


def write_review_checklist(path: Path, args, report: dict) -> None:
    """The operator's sheet: what each session showed and what to judge by eye."""
    lines = ["# HF-08 by-eye review", "",
             f"Runtime: `{report['runtime']}`, renderer {report['renderer']}, caps {', '.join(str(f) for f in args.fps)},",
             f"{'full screen' if args.fullscreen else 'windowed'}, swap interval {args.swap_interval}.",
             "Engine-side checks (no gameplay in draws, no errors) are in report.json;",
             "mark each item below pass or fail by eye, with a note for anything seen.", ""]
    cases = {c["case"]: c for c in report["cases"]}
    for fps in args.fps:
        for segment in review_segments(args, ""):
            case = cases.get(f"{fps}fps-{segment['name']}", {})
            lines += [f"## {segment['name']} at {fps} fps",
                      f"Engine-side: {case.get('samples', 0)} draws captured, gameplay in draws {case.get('gameplayInDraws', 'n/a')},"
                      f" errors {len(case.get('errors') or [])}.", ""]
            lines += [f"- [ ] {item} — verdict: ______" for item in segment["judge"]]
            lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--map", default="game/airdefense1")
    parser.add_argument("--mp-map", default="mp/q4dm1")
    parser.add_argument("--port", type=int, default=28995, help="loopback MP server port")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--fps", type=int, nargs="+", default=[240])
    parser.add_argument("--swap-interval", type=int, default=0, help="r_swapInterval: 0 tears, 1 waits for vsync (with VRR, cap a few Hz under the refresh), -1 adaptive vsync")
    parser.add_argument("--xr-runtime-json", type=Path, default=ROOT / "builddir" / "openq4_xr_test_runtime.json",
                        help="the in-tree OpenXR test runtime manifest, for the vr case")
    parser.add_argument("--probe-rate", type=float, default=2000.0, help="synthetic mouse counts per second")
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--min-fps", type=float, default=30.0,
                        help="slowest draw rate the capture wait allows for (occluded or loaded machines draw slowly)")
    parser.add_argument("--viewpos", default="10200 -6800 40 0 170 0",
                        help="noclip setviewpos used by the HF-02 clock controls on airdefense1")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--scenario", nargs="+",
                        choices=("look", "thirdperson", "flick", "flash", "combined", "hidden", "minimized", "hitch", "menu", "death", "ragdoll", "debris", "vehicle", "zoom", "lift", "demo", "subview", "vr",
                                 "mphost", "mpclient", "mploss", "spectate", "pacing", "review", "sensor"),
                        default=["look"])
    parser.add_argument("--fullscreen", action="store_true",
                        help="full screen at the native desktop resolution instead of a 960x540 window (reference runs)")
    parser.add_argument("--reference", action="store_true",
                        help="operator statement: a visible foreground window on the HF-07 reference machine; only then does pacing qualify")
    parser.add_argument("--sensor-seconds", type=int, default=120, help="length of the operator's sensor session")
    parser.add_argument("--present-timing", action="store_true",
                        help="Vulkan: also measure when frames reach the display (r_vkPresentTiming 1, VK_KHR_present_wait)")
    parser.add_argument("--cvar", nargs=2, action="append", default=[], metavar=("NAME", "VALUE"),
                        help="an extra +set for every case, for A/B runs (for example com_preciseFrameWait 0)")
    parser.add_argument("--reanalyse", action="store_true", help="recompute report.json from an existing output directory")
    args = parser.parse_args()
    if args.reanalyse:
        return reanalyse(args.output_dir.resolve(), args.probe_rate)

    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    output.mkdir(parents=True, exist_ok=False)
    client = runtime / "openQ4-client_x64.exe"
    # sensitivity 5 x m_yaw 0.022 with no acceleration: degrees per count
    expected_rate = -args.probe_rate * 5.0 * 0.022
    report = {"runtime": str(runtime), "map": args.map, "mpMap": args.mp_map, "renderer": args.renderer,
              "probeRate": args.probe_rate, "reference": args.reference, "cvars": args.cvar, "fullscreen": args.fullscreen, "swapInterval": args.swap_interval,
              "binaries": {name: sha256(runtime / name) for name in
                           ("openQ4-client_x64.exe", "baseoq4/game_x64.dll",
                            f"renderer-{args.renderer}_x64.dll")
                           if (runtime / name).is_file()},
              "cases": []}
    capture_wait = lambda fps: f"waitMsec {int(args.samples / (min(fps, args.min_fps) if fps else args.min_fps) * 1000) + 2000}"
    probe = f"in_presentationViewProbe {args.probe_rate:g}"
    turn_sp = ["god", "notarget", "noclip", f"setviewpos {args.viewpos}", "waitMsec 500", probe, "waitMsec 1500"]
    turn_mp = ["waitMsec 3000", probe, "waitMsec 1500"]
    # net_LANServer 1: a host bound to 127.0.0.1 must not heartbeat the master server
    loopback = ["net_ip", "127.0.0.1", "net_enableIPv6", "0", "net_LANServer", "1", "net_allowCheats", "1", "sv_cheats", "1",
                "win_allowMultipleInstances", "1", "ui_autoJoin", "1"]
    # name, launch cvars, cfg lines after map load, mode
    scenarios = {
        # late look off/on while the probe turns the view
        "look": [(f"look{late}", ["in_presentationView", str(late)], turn_sp, "sp") for late in (0, 1)],
        # HF-05: the third-person camera on the 60 Hz tic (0) or interpolated (1)
        "thirdperson": [(f"thirdperson-interp{interp}", ["in_presentationView", "0", "pm_thirdPerson", "1",
                                                          "g_presentationInterpolation", str(interp)], turn_sp, "sp")
                        for interp in (0, 1)],
        # HF-05: the death camera orbiting the player's falling body
        "death": [("death", ["in_presentationView", "0", "pm_thirdPersonDeath", "1"],
                   ["notarget", f"setviewpos {args.viewpos}", "waitMsec 1500",
                    f"g_presentationClockCapture {args.samples}", "testDeath"], "sp")],
        # HF-04: an articulated figure falling as a ragdoll
        "ragdoll": [("ragdoll", ["in_presentationView", "0"],
                     ["god", "notarget", "g_debugDamage 1", f"setviewpos {args.viewpos}", "waitMsec 500",
                      "spawn monster_strogg_marine name hf_ragdoll", "waitMsec 2000",
                      f"g_presentationClockCapture {args.samples}",
                      "damage hf_ragdoll 5000 damage_triggerhurt_1000"], "sp")],
        # HF-04: staged explodables throw unbound client debris (def_debris) with trails
        "debris": [("debris", ["in_presentationView", "0"],
                    ["god", "notarget", "g_debugDamage 1", f"setviewpos {args.viewpos}", "waitMsec 500",
                     "spawn launching_barrel_small name hf_barrel", "waitMsec 2000",
                     f"g_presentationClockCapture {args.samples}",
                     "damage hf_barrel 1000 damage_triggerhurt_1000"], "sp")],
        # HF-05: a joint-bound cockpit eye; the walker turns at its own rate, so judge
        # it by drawnStepJitterDeg rather than against the input line
        "vehicle": [("vehicle", ["in_presentationView", "0"],
                     ["god", "notarget", f"setviewpos {args.viewpos}", "waitMsec 500",
                      "spawn vehicle_walker name hf_walker", "waitMsec 1500",
                      'script "$player1.enterVehicle( $hf_walker );"', "waitMsec 2000", probe, "waitMsec 1500"], "sp")],
        # HF-08: the latency flash draws once per press
        "flash": [("flash", ["in_presentationView", "1", "com_latencyFlash", "1"],
                   ["god", "notarget", "noclip", f"setviewpos {args.viewpos}", "waitMsec 1000"] +
                   ["latencyFlashTest", "waitMsec 250"] * 5, "sp")],
        # HF-08: rapid turns
        "flick": [(f"flick-look{late}", ["in_presentationView", str(late)],
                   ["god", "notarget", "noclip", f"setviewpos {args.viewpos}", "waitMsec 500",
                    f"in_presentationViewProbe {args.probe_rate * 10:g}", "waitMsec 1500"], "sp")
                  for late in (0, 1)],
        # HF-08: zoomed look; railgun because airdefense1 may already hold the machinegun
        "zoom": [(f"zoom-look{late}", ["in_presentationView", str(late), "ui_autoSwitch", "1", "pm_zoomedSlow", "50"],
                  ["god", "notarget", "noclip", f"setviewpos {args.viewpos}", "give weapon_railgun", "waitMsec 2000",
                   "in_presentationViewProbeZoom 1", "waitMsec 1500", probe, "waitMsec 1500"], "sp")
                 for late in (0, 1)],
        # HF-08: late look while a lift carries the eye
        "lift": [(f"lift-look{late}", ["in_presentationView", str(late)],
                  ["god", "notarget", "setviewpos -480 1472 100 0 0 0", "waitMsec 2000", probe, "waitMsec 1000",
                   'script "$func_elevator_3.gotoFloor( 2 );"', "waitMsec 300"], "sp:game/airdefense2")
                 for late in (0, 1)],
        # HF-06: a remote camera view rendered as a subview of every draw
        "subview": [(f"subview-{place}", ["in_presentationView", "1"],
                     ["god", "notarget", "noclip", f"setviewpos {viewpos}", "waitMsec 2500", "screenshot",
                      "r_rendererMetrics 2", "waitMsec 300", "r_rendererMetrics 0"], mode)
                    for place, viewpos, mode in (("monitor", "15915 -1201 169 16 -40 0", "sp:game/tram1"),
                                                 ("portalsky", args.viewpos, "sp"))],
        # HF-06: the cross-system run: one session through a map reset, a renderer
        # restart and an AVI capture with TAA, shadow maps and late look on
        "combined": [("combined", ["in_presentationView", "1", "r_temporalAA", "1", "r_useShadowMap", "1"],
                      turn_sp + [
                          "g_presentationClockCapture 0", f"g_presentationClockCapture {args.samples // 2}", "waitMsec 4000",
                          "screenshot", "g_presentationClockCapture 0", "saveGame hf_combined", "waitMsec 1500",
                          # the load must not run this script again
                          'g_autoExecAfterMapLoad ""', "loadGame hf_combined", "waitMsec 4000",
                          f"g_presentationClockCapture {args.samples // 2}", "waitMsec 4000", "screenshot",
                          "g_presentationClockCapture 0", "vid_restart", "waitMsec 5000",
                          f"g_presentationClockCapture {args.samples // 2}", "waitMsec 4000", "g_presentationClockCapture 0",
                          "aviGame hf_combined", "waitMsec 1000", "aviGame", "waitMsec 1000",
                          f"g_presentationClockCapture {args.samples // 2}", "waitMsec 4000"], "sp")],
        # HF-06: stereo frames from the OpenXR test runtime; late look stands down in VR
        "vr": [("vr", ["vr_enable", "1", "vr_debug", "0", "in_presentationView", "1"],
                ["god", "notarget", "noclip", f"setviewpos {args.viewpos}", "waitMsec 2000"], "sp")],
        # HF-06: record a render demo under late look, then play it back in the same session
        "demo": [("demo", ["in_presentationView", "1"], turn_sp + ["recordDemo hf_presentation"], "sp")],
        # HF-08: a loopback listen host's own player
        "mphost": [(f"mphost-look{late}", loopback + ["in_presentationView", str(late)], turn_mp, "mphost")
                   for late in (0, 1)],
        # HF-08: a remote client of a loopback listen host, through client prediction
        "mpclient": [(f"mpclient-look{late}", loopback + ["in_presentationView", str(late)], turn_mp, "mpclient")
                     for late in (0, 1)],
        # HF-08: prediction replay and recovery under client-side delay, jitter and loss
        "mploss": [(f"mploss-look{late}", loopback + ["in_presentationView", str(late),
                                                       "net_forceLatency", "40", "net_forceJitter", "10", "net_forceDrop", "5"], turn_mp, "mpclient")
                   for late in (0, 1)],
        # HF-05: remote bodies (two bots) while the host plays, spectates free and follows
        "spectate": [(f"spectate-{kind}", loopback + ["in_presentationView", "0"],
                      ["waitMsec 2000", "addbot", "addbot", "waitMsec 5000"] + extra, "mphost")
                     for kind, extra in (("play", []),
                                         ("free", ["ui_spectate Spectate", "waitMsec 2000"]),
                                         ("follow", ["ui_spectate Spectate", "waitMsec 2000", "follow next", "waitMsec 1500"]))],
        # HF-07: low-FPS catch-up after a 100 ms stall once a second
        "hitch": [(f"hitch-look{late}", ["in_presentationView", str(late)],
                   turn_sp + ["com_presentationStallProbe 100"], "sp") for late in (0, 1)],
        # HF-07: the main menu follows com_maxfps
        "menu": [("menu", ["com_showFramePacing", "2"],
                  ["waitMsec 5000", "framePacingReset", "waitMsec 4000", "framePacingSnapshot hfpacing", "quit"], "menu")],
        # HF-07: the background policy, a hidden window presents at about 60 Hz
        "hidden": [("hidden", ["in_presentationView", "1", "r_hiddenWindow", "1", "com_showFramePacing", "2"],
                    turn_sp + ["framePacingReset"], "sp")],
        # HF-07: a minimized window is capped at 60 too, on every platform
        "minimized": [("minimized", ["in_presentationView", "1", "com_showFramePacing", "2"],
                       turn_sp + ["windowMinimizeTest", "waitMsec 1000", "framePacingReset", "waitMsec 5000"], "sp")],
        # HF-08: click-to-photon with a sensor; the operator presses fire, nothing is synthesised
        "sensor": [("sensor", ["in_presentationView", "1", "com_latencyFlash", "1", "in_mouse", "1", "in_nograb", "0",
                               "con_noPrint", "0", "con_notifyTime", "8", "ui_autoSwitch", "1"],
                    ["god", "notarget", f"setviewpos {args.viewpos}", "give weapon_machinegun", "give ammo", "waitMsec 1500",
                     "framePacingReset", caption(f"Latency sensor: aim it at the left edge and press fire; ends in {args.sensor_seconds} s"),
                     f"waitMsec {args.sensor_seconds * 1000}"], "sp")],
        # HF-08: the by-eye review, captioned on screen for an operator
        "review": [(segment["name"], segment["cvars"], segment["lines"], segment["mode"])
                   for segment in review_segments(args, probe)],
        # HF-07: the draw cadence at this cap while turning with late look
        "pacing": [("pacing", ["in_presentationView", "1", "com_showFramePacing", "2", "r_rendererMetrics", "1"],
                    turn_sp + ["framePacingReset"], "sp")],
    }

    def command_for(home, fps, cvars, launch, cfg):
        extra = []
        for index in range(0, len(cvars), 2):
            extra += ["+set", cvars[index], cvars[index + 1]]
        for name, value in args.cvar:
            extra += ["+set", name, value]
        if args.present_timing:
            extra += ["+set", "r_vkPresentTiming", "1"]
        return [
            str(client),
            "+set", "fs_basepath", str(args.basepath), "+set", "fs_savepath", str(home),
            "+set", "fs_game", "baseoq4",
            "+set", "r_renderApi", args.renderer, "+set", "ui_retained", "1",
            "+set", "sv_cheats", "1", "+set", "developer", "1",
            "+set", "logFile", "2", "+set", "logFileName", "logs/openq4.log",
            "+set", "com_maxfps", str(fps), "+set", "r_swapInterval", str(args.swap_interval),
            # --fullscreen: the native desktop mode, in front of everything else
            "+set", "r_fullscreen", "1" if args.fullscreen else "0", "+set", "r_mode", "-2" if args.fullscreen else "-1",
            "+set", "r_customWidth", "960", "+set", "r_customHeight", "540",
            "+set", "r_windowWidth", "960", "+set", "r_windowHeight", "540",
            "+set", "r_borderlessDefaultMigrated", "1", "+set", "r_borderless", "0",
            "+set", "in_mouse", "0", "+set", "in_nograb", "1",
            "+set", "sensitivity", "5", "+set", "m_yaw", "0.022", "+set", "cl_mouseAccel", "0",
            "+set", "m_smooth", "1", "+set", "m_filter", "0",
            # before the case's own cvars, so a case can watch a cinematic
            "+set", "g_autoSkipCinematics", "1",
        ] + extra + [
            "+set", "com_skipLoadingContinue", "1",
            "+set", "g_autoExecAfterMapLoad", cfg, "+set", "g_autoExecAfterMapLoadDelayMs", "0",
        ] + launch

    for fps in args.fps:
        for scenario in args.scenario:
            for variant, cvars, lines, mode in scenarios[scenario]:
                name = f"{fps}fps-{variant}"
                home = output / name
                game = home / "baseoq4"
                game.mkdir(parents=True)
                body = ["waitMsec 4000"] + lines
                if scenario in SNAPSHOTS and "framePacingReset" not in lines:
                    # before the case's own lines, so the flash presses are counted
                    body.insert(1, "framePacingReset")
                if scenario in SNAPSHOTS and args.present_timing:
                    # presented frames are counted from the same point as the frame pacing
                    reset = body.index("framePacingReset") if "framePacingReset" in body else 1
                    body.insert(reset + 1, "rendererPresentTiming reset")
                if not any(line.startswith("g_presentationClockCapture") for line in lines):
                    body.append(f"g_presentationClockCapture {args.samples}")
                body.append(capture_wait(fps))
                if scenario in SNAPSHOTS:
                    body += ["framePacingSnapshot hfpacing", "win_keepTimerResolution"]
                    if args.present_timing:
                        body.append("rendererPresentTiming hfpacing")
                # A capture still short of its count when the wait ends (a slow window: occluded,
                # throttled or behind vsync) dumps what it has: a count below the samples taken
                # dumps on the next draw, and an already dumped capture ignores it.
                body += ["g_presentationClockCapture 1", "waitMsec 250",
                         "g_presentationClockCapture 0", "in_presentationViewProbe 0", "in_presentationViewProbeZoom 0"]
                if scenario == "mploss":
                    body.append("netImpairment")
                body += ["stopRecording", "waitMsec 500", "timeDemoQuit hf_presentation"] if scenario == "demo" else ["quit"]
                if mode == "menu":
                    # no map: the script runs from the command line at the main menu
                    body = lines
                write_script(game / "look_probe.cfg", body)
                host = None
                if mode == "menu":
                    launch = ["+exec", "look_probe.cfg"]
                elif mode == "sp" or mode.startswith("sp:"):
                    launch = ["+set", "si_gameType", "singleplayer", "+map", mode[3:] if mode.startswith("sp:") else args.map]
                elif mode == "mphost":
                    launch = ["+set", "si_gameType", "DM", "+set", "net_serverDedicated", "0",
                              "+set", "net_port", str(args.port), "+spawnServer", args.mp_map]
                else:
                    # a host that stays up for the client's run, then is stopped
                    host_home = output / f"{name}-host"
                    (host_home / "baseoq4").mkdir(parents=True)
                    write_script(host_home / "baseoq4" / "host.cfg", ["waitMsec 1000"])
                    host_cmd = command_for(host_home, 60, loopback + ["in_presentationView", "0"], [
                        "+set", "si_gameType", "DM", "+set", "net_serverDedicated", "0",
                        "+set", "net_port", str(args.port), "+spawnServer", args.mp_map], "host.cfg")
                    (host_home / "launch.json").write_text(json.dumps({"cwd": str(runtime), "args": host_cmd}, indent=1), encoding="utf-8")
                    host = subprocess.Popen(host_cmd, cwd=runtime, stdin=subprocess.DEVNULL,
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    time.sleep(25)
                    launch = ["+set", "net_port", str(args.port + 1), "+connect", f"127.0.0.1:{args.port}"]
                command = command_for(home, fps, cvars, launch, "look_probe.cfg")
                environment = None
                if scenario == "vr":
                    (home / "xr_script.txt").write_text("", encoding="utf-8")
                    environment = dict(os.environ)
                    environment.update({"XR_RUNTIME_JSON": str(args.xr_runtime_json.resolve()),
                                        "OPENQ4_XR_TEST_LOG": str(home / "xr_log.jsonl"),
                                        "OPENQ4_XR_TEST_SCRIPT": str(home / "xr_script.txt"),
                                        "OPENQ4_XR_TEST_EYE_SIZE": "800x880", "OPENQ4_XR_TEST_RATE": "90"})
                (home / "launch.json").write_text(json.dumps({"cwd": str(runtime), "args": command}, indent=1), encoding="utf-8")
                started = time.monotonic()
                try:
                    exit_code = subprocess.run(command, cwd=runtime, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                               stderr=subprocess.DEVNULL, timeout=args.timeout, env=environment).returncode
                except subprocess.TimeoutExpired:
                    exit_code = "timeout"
                finally:
                    if host is not None:
                        host.terminate()
                        try:
                            host.wait(timeout=30)
                        except subprocess.TimeoutExpired:
                            host.kill()
                log = game / "logs/openq4.log"
                text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
                case = summarise(name, scenario, fps, exit_code, started, text,
                                 expected_rate * {"zoom": 0.5, "flick": 10.0}.get(scenario, 1.0), args.reference)
                if scenario == "vr":
                    xr_log = home / "xr_log.jsonl"
                    frames = [int(f) for f in re.findall(r'"event":"end_frame","frame":(\d+)',
                                                         xr_log.read_text(encoding="utf-8", errors="replace"))] if xr_log.is_file() else []
                    case["xrFramesSubmitted"] = max(frames, default=0)
                report["cases"].append(case)
                print(json.dumps({k: case[k] for k in case if k not in ("errors", "gameplayReports")}), flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    if "review" in args.scenario:
        write_review_checklist(output / "review-checklist.md", args, report)
    # Scenes drawn on the authoritative clock record no samples by design (thirdperson-interp0).
    failures = [c["case"] for c in report["cases"] if c["exit"] != 0 or c["errors"] or c["gameplayInDraws"] or
                (c["scenario"] == "demo" and not c.get("timeDemo")) or
                (c["scenario"] == "vr" and not c.get("xrFramesSubmitted")) or
                (c["scenario"] == "flash" and (c.get("latencyFlashes") != 5 or
                                               (c.get("inputToSwapMs") or {}).get("flashes") != 5)) or
                (c["scenario"] == "hitch" and (not c.get("hitch") or c["hitch"]["backsteps"] or c["hitch"]["stalls"] < 2)) or
                (c["scenario"] == "menu" and not c.get("menuPacing")) or
                (c["scenario"] == "hidden" and not 50.0 <= (c.get("pacing", {}).get("presentedHz") or 0.0) <= 63.0) or
                (c["scenario"] == "minimized" and (not c.get("minimizedReported") or c["minimizedPacing"]["cap"] != 60 or
                                                   not c["minimizedPacing"]["frames"] or c["minimizedPacing"]["hz"] > 63.0)) or
                (c["scenario"] == "combined" and (len(c.get("segments") or []) != 4 or
                                                  any(s["samples"] < 64 or s["gameplay"] for s in c["segments"]))) or
                (c["scenario"] == "mploss" and not any(v["dropped"] for v in (c.get("netImpairment") or {}).values())) or
                (c["samples"] < 64 and not c["case"].endswith("interp0") and c["scenario"] not in ("menu", "minimized", "sensor")) or
                c.get("pacing", {}).get("verdict") == "over budget"]
    print(f"presentation look runtime: {len(report['cases'])} cases, failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
