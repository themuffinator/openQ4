#!/usr/bin/env python3
"""Reference-suite failures must survive aggregation; no game or input is opened."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import json
import subprocess
import sys
import tempfile
import unittest

import presentation_reference_suite as suite

ROOT = Path(__file__).resolve().parents[2]


class ReferenceSuiteTest(unittest.TestCase):
    def setUp(self):
        (ROOT / ".tmp").mkdir(exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(prefix="reference-suite-", dir=ROOT / ".tmp")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.args = SimpleNamespace(basepath=self.root, runtime_dir=self.root, reference=False)

    def run_case(self, result=0, report=None):
        if report is not None:
            directory = self.root / "case"
            directory.mkdir(exist_ok=True)
            (directory / "report.json").write_text(report, encoding="utf-8")
        returned = SimpleNamespace(returncode=result, stdout="", stderr="")
        with patch.object(suite.subprocess, "run", return_value=returned), redirect_stdout(StringIO()):
            return suite.run_case(self.args, self.root, "case", [])

    def test_missing_malformed_and_empty_reports_fail(self):
        for report in (None, "{", "[]", '{"cases":[]}', '{"cases":[null]}', '{"cases":[{}]}'):
            with self.subTest(report=report):
                record = self.run_case(report=report)
                self.assertNotEqual(record["exit"], 0)
                self.assertIsNone(record["report"])

    def test_child_failure_is_retained(self):
        record = self.run_case(result=1, report='{"cases":[{"case":"240fps","exit":0}]}')
        self.assertEqual(record["exit"], 1)

    def test_failed_case_cannot_pass_with_zero_child_exit(self):
        record = self.run_case(report='{"cases":[{"case":"240fps","exit":1}]}')
        self.assertEqual(record["exit"], 1)

    def test_timeout_is_retained(self):
        with patch.object(suite.subprocess, "run", side_effect=subprocess.TimeoutExpired("harness", 1)), redirect_stdout(StringIO()):
            record = suite.run_case(self.args, self.root, "case", [], timeout=1)
        self.assertEqual(record["exit"], "timeout")

    def test_refresh_rejects_unusable_values(self):
        for refresh in (None, 0, -1, float("nan"), float("inf"), "240"):
            record = {"report": {"cases": [{"pacing": {"framePacingSnapshot": {"refreshHz": refresh}}}]}}
            self.assertIsNone(suite.refresh_from([record]))

    def main_run(self, name, failure=False, refresh=240, render_thread="1"):
        out = self.root / name
        args = ["suite", "--basepath", str(self.root), "--output-dir", str(out), "--windowed",
                "--skip-sensor", "--skip-review", "--renderers", "vulkan", "--render-thread", render_thread]
        def run_case(args, output, name, options):
            # Only refresh detection needs a capture; other reports may be policy cases.
            case = {"case": "sample", "scenario": "policy", "exit": 0,
                    "pacing": {"framePacingSnapshot": {"refreshHz": refresh}}}
            return {"name": name, "options": options, "exit": 1 if failure and name == "vulkan-caps" else 0,
                    "report": {"cases": [case]}}
        with patch.object(sys, "argv", args), patch.object(suite, "run_case", side_effect=run_case), redirect_stdout(StringIO()):
            code = suite.main()
        report = json.loads((out / "suite.json").read_text(encoding="utf-8"))
        return code, report, (out / "reference-summary.md").read_text(encoding="utf-8")

    def test_main_propagates_child_failure(self):
        code, report, summary = self.main_run("failure", failure=True)
        self.assertEqual(code, 1)
        self.assertFalse(report["automatedPassed"])
        self.assertIn("failed or incomplete runs: vulkan-caps", summary)

    def test_missing_refresh_stays_in_summary(self):
        code, report, summary = self.main_run("missing-refresh", refresh=None)
        self.assertEqual(code, 1)
        self.assertIn("No usable refresh reported", summary)
        self.assertEqual(report["runs"][-2]["exit"], "not run")

    def test_automated_success_is_not_physical_qualification(self):
        code, report, summary = self.main_run("pass")
        self.assertEqual(code, 0)
        self.assertTrue(report["automatedPassed"])
        self.assertEqual(report["physicalQualification"], "pending")
        self.assertIn("remain pending physical sensor", summary)
        self.assertEqual(report["renderThread"], 1)
        for run in report["runs"]:
            if run["name"].startswith("vulkan-"):
                self.assertIn("r_vkRenderThread", run["options"])

    def test_full_overlap_reaches_each_vulkan_case(self):
        code, report, summary = self.main_run("full-overlap", render_thread="2")
        self.assertEqual(code, 0)
        self.assertEqual(report["renderThread"], 2)
        self.assertEqual(report["physicalQualification"], "pending")
        for run in report["runs"]:
            if run["name"].startswith("vulkan-"):
                options = run["options"]
                self.assertEqual(options[options.index("r_vkRenderThread") + 1], "2")


if __name__ == "__main__":
    unittest.main()
