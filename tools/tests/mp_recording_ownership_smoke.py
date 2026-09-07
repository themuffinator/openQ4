#!/usr/bin/env python3
"""Qualify manual MVD ownership and automatic recording failures/limits in a BO1."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path

from mp_round_remote_smoke import read_log
from mp_series_smoke import ROOT, SeriesRun


class RecordingRun(SeriesRun):
    def __init__(self, args):
        super().__init__(args)
        self.observations = {}
        self.manual = self.games["host"] / "demos/operator-owned.mvd"
        self.partial = self.manual.with_suffix(".mvd.part")
        for name in ("manifest.json", "codec-manifest.json"):
            if (self.runtime / name).is_file():
                shutil.copy2(self.runtime / name, self.output / name)

    def assert_pending(self, label):
        if self.manual.exists() or not self.partial.is_file() or self.partial.stat().st_size == 0:
            raise RuntimeError(f"{label}: the operator's unfinished stream was stopped or lost")
        before = self.partial.stat().st_size
        self.perform("host", label, ["waitMsec 1500"])
        after = self.partial.stat().st_size
        if after <= before:
            raise RuntimeError(f"{label}: the operator's stream stopped growing")
        self.observations[label] = {"partial_bytes_before": before, "partial_bytes_after": after}

    def stop_manual(self):
        self.perform("host", "operator explicitly stops recording", ["stopMVD", "waitMsec 500", 'mvdInfo "operator-owned.mvd"'])
        if not self.manual.is_file() or self.partial.exists():
            raise RuntimeError("the explicit operator stop did not publish its recording")
        log = read_log(self.logs["host"])
        if "clean end: yes" not in log or not re.search(r"Stopped MVD recording[^\n]*demos/operator-owned.mvd", log):
            raise RuntimeError("the operator's recording has no validated clean end")

    def action(self, role, token, **kwargs):
        if role == "host" and token == "force_ready":
            if self.args.recording_case == "duration-limit":
                self.perform("host", "set automatic recording duration limit", ["set mvd_maxDurationMinutes 1"])
            elif self.args.recording_case == "size-limit":
                self.perform("host", "set automatic recording size limit", [
                    "set mvd_maxSizeMB 1", "set mvd_snapshotDelay 16", "set mvd_maxDurationMinutes 0"])
            elif self.args.recording_case == "start-failure":
                blocker = self.games["host"] / "demos"
                if blocker.exists():
                    raise RuntimeError("recording-start blocker requires a fresh demos path")
                blocker.write_text("Deliberate recording-start failure.\n", encoding="utf-8")
            else:
                self.perform("host", "operator starts recording before match", ['recordMVD "operator-owned"', "waitMsec 1500"])
                self.assert_pending("before-match")
        elif role == "host" and token == "forfeit":
            if self.args.recording_case == "duration-limit":
                self.wait("automatic duration limit", lambda texts:
                    "Stopped MVD recording (duration limit reached)" in texts["host"], timeout=90)
                view = self.view("host")
                if view["phase"] != 3 or view["series"]["state"] != 4:
                    raise RuntimeError("recording duration limit changed live match state")
                self.observations["live_after_duration_limit"] = {"session": view["session"], "phase": view["phase"]}
            elif self.args.recording_case == "size-limit":
                self.wait("automatic size limit", lambda texts:
                    "MVD recording size limit reached" in texts["host"], timeout=240)
                view = self.view("host")
                if view["phase"] != 3 or view["series"]["state"] != 4:
                    raise RuntimeError("recording size limit changed live match state")
                self.observations["live_after_size_limit"] = {"session": view["session"], "phase": view["phase"]}
            elif self.args.recording_case != "start-failure":
                self.assert_pending("during-live-play")
                if self.args.recording_case == "stop-before-result":
                    self.stop_manual()
        elif role == "host" and token == "series_advance" and self.args.recording_case in ("stop-after-result", "open-at-seal"):
            self.assert_pending("during-map-review")
            view = self.view("host")
            series_id = view["series"]["id"]
            paths = list((self.games["host"] / "match-results").glob(f"session-*_series-{series_id}_*.json"))
            if len(paths) != 1:
                raise RuntimeError("map review did not publish exactly one result")
            report = json.loads(paths[0].read_text(encoding="utf-8"))
            if any(a["kind"] == "mvd" for a in report["artifacts"]):
                raise RuntimeError("map evidence linked the operator's unfinished recording")
            self.observations["review_evidence"] = str(paths[0])
            if self.args.recording_case == "stop-after-result":
                self.stop_manual()
        return super().action(role, token, **kwargs)

    def validate_reports(self, series_id):
        if self.args.recording_case == "duration-limit":
            paths = super().validate_reports(series_id)
            if read_log(self.logs["host"]).count("Stopped MVD recording (duration limit reached)") != 1:
                raise RuntimeError("automatic duration limit did not stop exactly one recording")
            return paths
        game = self.games["host"]
        series_path = game / f"match-results/series-{series_id}.json"
        series = json.loads(series_path.read_text(encoding="utf-8"))
        if (series["schema"] != 1 or series["seriesId"] != series_id or
                series["seriesScore"] != [1, 0] or series["final"]["outcome"] != "complete" or
                series["final"]["winner"] != 0 or series["maps"]["accepted"] != 1 or series["maps"]["dropped"]):
            raise RuntimeError("recording ownership changed the final series outcome")
        entry = series["maps"]["entries"][0]
        evidence_artifact, mvd = entry["artifacts"]["evidence"], entry["artifacts"]["mvd"]
        evidence_path = (game / evidence_artifact["qpath"]).resolve()
        if not evidence_path.is_relative_to(game) or evidence_artifact["status"] != "available":
            raise RuntimeError("recording state prevented match evidence publication")
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        live = next(c for c in self.captures if c["label"] == "map-1-live-host")
        if evidence["sessionId"] != entry["sessionId"] or entry["sessionId"] != live["session"] or evidence["seriesId"] != series_id:
            raise RuntimeError("recording evidence and accepted match identities differ")
        journal = evidence["journal"]
        events = journal["events"]
        if journal["dropped"] or journal["accepted"] != len(events) or [e["sequence"] for e in events] != list(range(1, len(events) + 1)):
            raise RuntimeError("recording scenario lost journal events")
        results = [e["data"] for e in events if e["kind"] == "result"]
        if len(results) != 1 or results[0]["outcome"] != "forfeit" or results[0]["winnerSide"] != 0:
            raise RuntimeError("recording scenario did not seal exactly one matching result")
        links = [a["qpath"] for a in evidence["artifacts"] if a["kind"] == "mvd"]
        expected_links = ["demos/operator-owned.mvd"] if self.args.recording_case == "stop-before-result" else []
        if links != expected_links:
            raise RuntimeError(f"match evidence advertised an incorrect recording: {links}")
        failures = [e["data"] for e in events if e["kind"] == "outputFailure"]
        if self.args.recording_case == "start-failure":
            starts = [e for e in failures if e["output"] == "mvdStart"]
            if len(starts) != 1 or not starts[0]["reason"] or mvd["status"] != "failed" or not mvd["reason"] or mvd["qpath"]:
                raise RuntimeError("automatic recording-start failure was not reported without a playable link")
            if (game / "demos").read_text(encoding="utf-8") != "Deliberate recording-start failure.\n":
                raise RuntimeError("recording-start failure modified its blocker")
        elif self.args.recording_case == "size-limit":
            stops = [e for e in failures if e["output"] == "mvdStop"]
            partial = (game / mvd["qpath"]).resolve()
            if (len(failures) != 1 or len(stops) != 1 or not stops[0]["reason"] or
                    mvd["status"] != "failed" or not mvd["reason"] or
                    not partial.is_relative_to(game) or not partial.name.endswith(".mvd.part") or
                    not partial.is_file() or not 0 < partial.stat().st_size <= 1024 * 1024 or
                    partial.with_suffix("").exists()):
                raise RuntimeError("recording size limit did not retain a bounded partial stream and one failure")
            self.observations["partial_bytes"] = partial.stat().st_size
        else:
            if failures:
                raise RuntimeError(f"manual ownership generated an output failure: {failures}")
            if self.args.recording_case == "open-at-seal":
                if mvd != {"status": "pending", "reason": 5, "qpath": "demos/operator-owned.mvd.part"}:
                    raise RuntimeError(f"sealed series misrepresented the operator's unfinished recording: {mvd}")
                self.assert_pending("after-series-seal")
                sealed = hashlib.sha256(series_path.read_bytes()).hexdigest()
                self.stop_manual()
                self.perform("host", "sealed report remains immutable", ["waitMsec 1500"])
                if hashlib.sha256(series_path.read_bytes()).hexdigest() != sealed:
                    raise RuntimeError("an operator stop rewrote the sealed series report")
            elif mvd != {"status": "available", "reason": 0, "qpath": "demos/operator-owned.mvd"}:
                raise RuntimeError(f"series did not reconcile the explicitly stopped recording: {mvd}")
            if not self.manual.is_file() or self.partial.exists():
                raise RuntimeError("the operator's final recording was not committed")
            starts = re.findall(r"Recording multi-view demo to '([^']+)'", read_log(self.logs["host"]))
            if starts != ["demos/operator-owned.mvd"]:
                raise RuntimeError(f"the match replaced or duplicated the operator's recording: {starts}")
        self.observations["series_mvd"] = mvd
        self.observations["output_failures"] = failures
        recordings = [str(partial)] if self.args.recording_case == "size-limit" else ([str(self.manual)] if self.manual.is_file() else [])
        return [str(series_path), str(evidence_path), *recordings]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--port", type=int, default=28921)
    parser.add_argument("--timeout", type=int, default=480)
    parser.add_argument("--recording-case", choices=("stop-before-result", "stop-after-result", "open-at-seal", "start-failure", "duration-limit", "size-limit"), required=True)
    parser.set_defaults(best_of=1, duel=False, network_trace=False, full_distance=False, pause_before_forfeit=False,
                        fail_artifact=None, failure_map=1, restart_after_map=0)
    args = parser.parse_args()
    if args.output_dir.exists() or not 1024 <= args.port <= 65534:
        parser.error("a fresh output directory and two consecutive unprivileged ports are required")
    if not (args.runtime_dir / args.executable_name).is_file():
        parser.error("a staged runtime executable is required")
    run = RecordingRun(args)
    result, failures = {}, []
    try:
        result = run.run()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        failures.append(str(error))
    finally:
        exits = run.close()
    if len(exits) != 2 or any(exits.values()):
        failures.append(f"incomplete shutdown: {exits}")
    result.update(status="fail" if failures else "pass", failures=failures, exits=exits,
                  recording_case=args.recording_case, observations=run.observations,
                  captures=run.captures, logs={r: str(p) for r, p in run.logs.items()})
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "captures"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
