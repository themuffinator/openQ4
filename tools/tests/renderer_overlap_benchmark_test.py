#!/usr/bin/env python3
"""Reject incomplete or misleading mode comparison evidence without a game."""
import unittest
from renderer_overlap_benchmark import ORDERS, parse_windows, summarize
from renderer_gameplay_benchmark import RunSpec, build_scripted_capture_lines, parse_benchmark_config


def evidence():
    lines = []
    for repetition, order in enumerate(ORDERS):
        for mode in order:
            label = f"hf_r{repetition}m{mode}"
            lines += [f"Frame pacing snapshot {label} (SP): bound=cpu, samples=100, present=5.00 ms (200 Hz), p50=5.00 ms, p95=6.00 ms, p99=7.00 ms, max=8.00 ms",
                      "Frame timing window (SP): samples=100, span_ms=500, mean_ms=5.000000",
                      f"Frame snapshot stats {label}: frames={100 if mode == 2 else 0} views=100 copies=500 bytes=20000 geometryBytes=10000 cacheBytes=10000 cpuUs=2000 reusedBytes=10000"]
    return "\n".join(lines)


class ComparisonTest(unittest.TestCase):
    def test_sp_mp_capture_pose_commands(self):
        for mode, role, command in (("SP", "sp", "viewpos"),
                                    ("MP", "server", "getviewpos"),
                                    ("MP", "client", "getviewpos")):
            spec = RunSpec(case_id="mp-q4dm1-listen" if mode == "MP" else "sp-airdefense1",
                           mode=mode, map_name="mp/q4dm1" if mode == "MP" else "game/airdefense1",
                           budget_map_name="", purpose="smoke", path_name="default", tier="auto",
                           maxfps="0", swap_interval="0", display_mode="windowed",
                           shadow_preset="default", renderer="best", render_api="vk")
            lines, _ = build_scripted_capture_lines(spec, role, "test", 60, 0, 1000,
                                                    renderer_metrics=False)
            self.assertEqual([line for line in lines if line in ("viewpos", "getviewpos")], [command])
            self.assertEqual(parse_benchmark_config("\n".join(lines)).role, role)

    def test_complete_balanced_windows(self):
        windows = parse_windows(evidence())
        self.assertEqual(len(windows), 9)
        summary = summarize(windows)
        self.assertEqual(summary["2"]["snapshot_us_per_frame"], 20)
        self.assertEqual(summary["2"]["snapshot_bytes_per_frame"], 200)
        self.assertEqual(summary["0"]["snapshot_us_per_frame"], 0)

    def test_incomplete_invalid_and_mislabelled_evidence_fails(self):
        for bad in (evidence().replace("Frame timing window", "missing", 1),
                    evidence().replace("mean_ms=5.000000", "mean_ms=0", 1),
                    evidence().replace("samples=100, span", "samples=99, span", 1),
                    evidence().replace("frames=100", "frames=0", 1),
                    evidence().replace("frames=0", "frames=100", 1),
                    evidence().replace("hf_r0m0", "hf_r0m2", 1),
                    evidence() + "\n" + evidence()):
            with self.subTest(bad=bad[:80]), self.assertRaises(ValueError):
                parse_windows(bad)


if __name__ == "__main__":
    unittest.main()
