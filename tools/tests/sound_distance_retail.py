#!/usr/bin/env python3
"""Reject false passes in the retail/openQ4 rendered distance comparison."""
from array import array
import math
from pathlib import Path
import random
import struct
import tempfile
import unittest

from sound_distance_retail_compare import (CLEAR, GAP, PASSES, SETTLE, assess, build_cases, measure,
                                           probe_commands, read_wav, window_for)

RATE = 4800
FLOOR = 1e-4  # background amplitude, about -77 dB


def authored(case):
    return case["expected_gain"]


def synth(cases, gain=authored, floor=FLOOR, seed=1, drop=None, offset=68, disturb=None):
    """Render a marker timeline like the engines': loop noise at each gain.

    `disturb` maps a pass-qualified tag to an extra amplitude played over that
    placement's whole window, like an unrelated map sound.
    """
    rng = random.Random(seed)
    samples, markers = array("f"), {}

    def mark(name):
        if name != drop:
            markers[name] = offset + len(samples) * 4

    def play(seconds, amplitude):
        for _ in range(int(seconds * RATE)):
            noise = floor * rng.uniform(-1.7, 1.7)
            tone = amplitude * rng.uniform(-1.7, 1.7)
            samples.extend((tone + noise, tone + noise))

    play(.3, 0)
    mark("CMP_BG_BEGIN")
    play(GAP * 2, 0)
    mark("CMP_BG_END")
    for number in range(1, PASSES + 1):
        for case in cases:
            tag = f"{case['tag']}_P{number}"
            level = gain(case) + (disturb or {}).get(tag, 0)
            play(SETTLE, level)
            mark(f"CMP_ON_{tag}")
            play(window_for(case["shader"]), level)
            mark(f"CMP_OFF_{tag}")
            play(CLEAR, 0)
            mark(f"CMP_GAP_{tag}")
            play(GAP, 0)
            mark(f"CMP_GAPEND_{tag}")
    return {"rate": RATE, "channels": 2, "data_offset": offset, "samples": samples}, markers


class RetailComparisonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = build_cases()
        cls.retail = measure(*synth(cls.cases, seed=2), cls.cases)

    def compare(self, gain=authored, **options):
        return assess(self.retail, measure(*synth(self.cases, gain, **options), self.cases))

    def test_matching_curves_pass(self):
        summary = self.compare()
        self.assertGreaterEqual(summary["compared"], 12)
        self.assertEqual(summary["silent"], 6)
        self.assertLess(summary["max_relative_delta_db"], .5)

    def test_small_level_offset_passes_but_large_one_fails(self):
        self.compare(lambda c: authored(c) * 10 ** (1 / 20))
        with self.assertRaisesRegex(AssertionError, "level"):
            self.compare(lambda c: authored(c) * 10 ** (3 / 20))

    def test_rejects_metre_conversion_either_way(self):
        def converted(scale):
            def gain(case):
                minimum, maximum = self.range(case)
                d = case["distance"] * scale
                frac = 1.0 if d <= minimum else max(0.0, (maximum - d) / (maximum - minimum))
                return 2 ** (case["authored_db"] / 6) * .65 * frac
            return gain
        for scale in (39.37, 1 / 39.37):
            with self.subTest(scale=scale), self.assertRaises((AssertionError, ValueError)):
                self.compare(converted(scale))

    def test_rejects_double_attenuation(self):
        # OpenAL's inverse-distance model on top of the engine curve.
        def doubled(case):
            minimum, _ = self.range(case)
            return authored(case) * min(1.0, minimum / case["distance"]) if case["distance"] else authored(case)
        with self.assertRaisesRegex(AssertionError, "openQ4 vs retail"):
            self.compare(doubled)

    def test_rejects_quadratic_curve(self):
        with self.assertRaises(AssertionError):
            self.compare(lambda c: authored(c) * c["falloff"])

    def test_rejects_audible_sound_beyond_maximum(self):
        with self.assertRaisesRegex(AssertionError, "audible"):
            self.compare(lambda c: authored(c) or .01)

    def test_rejects_loud_placement_hidden_under_floor(self):
        with self.assertRaises((AssertionError, ValueError)):
            self.compare(floor=.05)
        with self.assertRaisesRegex(AssertionError, "under the measurement floor"):
            self.compare(floor=.004)

    def test_rejects_missing_markers_and_mismatched_cases(self):
        for name in ("CMP_BG_END", "CMP_ON_0_3_P1", "CMP_GAPEND_2_8_P2", "CMP_OFF_1_4_P2"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                measure(*synth(self.cases, drop=name), self.cases)
        with self.assertRaises(ValueError):
            assess(self.retail, self.retail[:-1])

    def test_rejects_marker_past_written_audio(self):
        wav, markers = synth(self.cases)
        markers["CMP_GAPEND_2_8_P2"] += RATE * 8
        with self.assertRaises(ValueError):
            measure(wav, markers, self.cases)

    def test_disturbed_pass_is_discarded_but_not_both(self):
        # A loud unrelated sound over one pass of a quiet far placement.
        summary = self.compare(disturb={"0_6_P1": .05})
        row = next(r for r in summary["rows"] if r["tag"] == "0_6")
        self.assertGreater(row["openq4_pass_spread_db"], 3)
        self.assertLess(abs(row["relative_delta_db"]), .5)
        with self.assertRaises(AssertionError):
            self.compare(disturb={"0_6_P1": .05, "0_6_P2": .05})

    def test_commands_share_one_sequence(self):
        retail = probe_commands(self.cases, lambda s: f"wait {round(s * 60)}")
        openq4 = probe_commands(self.cases, lambda s: f"waitMsec {round(s * 1000)}")
        self.assertEqual(len(retail), len(openq4))
        strip = lambda lines: [line for line in lines if not line.startswith("wait")]
        self.assertEqual(strip(retail), strip(openq4))
        self.assertTrue(any(line.startswith("setviewpos ") and len(line.split()) == 5 for line in retail))
        self.assertEqual(sum(line.startswith("spawn ") for line in retail), len(self.cases) * PASSES)
        # Every placement is stopped before it is removed, so no fade fills the gap.
        for index, line in enumerate(retail):
            if line.startswith("remove cmp_fixture"):
                self.assertEqual(retail[index - 1], "trigger cmp_fixture")

    def test_reads_float_wave_header(self):
        frames = array("f", [.5, -.5] * 960)
        fmt = struct.pack("<HHIIHHHHIH14s", 0xFFFE, 2, 48000, 384000, 8, 32, 22, 32, 3, 3,
                          b"\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71")
        data = b"RIFF" + struct.pack("<I", 0) + b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
        data += b"data" + struct.pack("<I", 0xFFFFFFFF) + frames.tobytes()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "mix.wav"
            path.write_bytes(data)
            wav = read_wav(path)
            self.assertEqual((wav["rate"], wav["data_offset"], len(wav["samples"])), (48000, 68, 1920))
            path.write_bytes(data.replace(struct.pack("<H", 3) + b"\x00\x00\x00\x00\x10", struct.pack("<H", 1)
                                          + b"\x00\x00\x00\x00\x10", 1))
            with self.assertRaises(ValueError):
                read_wav(path)

    def range(self, case):
        from sound_distance_runtime import SHADERS
        _, minimum, maximum, _ = SHADERS[case["shader"]]
        return minimum, maximum


if __name__ == "__main__":
    unittest.main()
