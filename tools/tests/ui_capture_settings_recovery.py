#!/usr/bin/env python3
"""Synthetic adversarial tests of the recovery capture oracle; never launch a game."""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/ui"))
import capture_settings_recovery as recovery


def row(prefix, **fields):
    return prefix + " " + " ".join(f"{key}={value}" for key, value in fields.items()) + "\n"


def probe(width, height, *, submitted=100, presented=100, restart=0, generation=1):
    return row("DISPLAY_PROBE", operation="report", result=1, observed=1, epoch=2, generation=generation,
               ready=1, window=1, available=1, outcome=2, submitted=submitted, presented=presented,
               failures=0, native=0, restart=restart, logical=f"{width}x{height}", pixel=f"{width}x{height}",
               display=1, position="50,50", hidden=1, fullscreen=0, maximized=0, samples=0, interval=1, valid=3)


def journal():
    flat = {"schema": 1, "state": "pending", "attempt": "ab" * 16,
            "baseline.r_windowWidth": 1280, "baseline.r_windowHeight": 720,
            "target.r_windowWidth": 960, "target.r_windowHeight": 540}
    for prefix, width, height in (("displayRestore.", 1280, 720), ("displayTarget.", 960, 540)):
        for key, value in {"actual.logicalWidth": width, "actual.logicalHeight": height,
                           "actual.pixelWidth": width if prefix == "displayRestore." else 0,
                           "actual.pixelHeight": height if prefix == "displayRestore." else 0,
                           "check.pixels": prefix == "displayRestore.", "samples": 0, "interval": 1}.items():
            flat[prefix + key] = value
    return flat


def encoded(flat):
    payload = json.dumps(flat, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode() + b"\n"
    return recovery.MAGIC + f"{zlib.crc32(payload):08x}\n".encode() + payload


def pending_log():
    log = probe(1280, 720)
    for operation in ("begin", "edit", "apply"):
        log += row("UI_SETTINGS", operation="settings.system." + operation, result=0, phase=1, owner=17, dirty=1)
    log += row("UI_SETTINGS_JOURNAL", state="pending", durable=1)
    log += row("UI_SETTINGS_DEVICE", restore=0, epoch=2, generation=2, submitted=100, presented=100, failures=0, width=960, height=540)
    log += row("UI_SETTINGS_VIEW", owner=17, request=1, epoch=2, generation=2, submitted=100, presented=100, failures=0)
    log += row("UI_SETTINGS_PRESENT", owner=17, request=1, epoch=2, generation=2, submitted=101, presented=101, failures=0)
    values = (2, 1, 1, 1, 1, 1, 1, 0, 14.8, 960, 540, 1280, 720, 960, 540)
    log += "".join(f"GUI_VALUE {key}={value}\n" for key, value in zip(recovery.READS, values))
    return log + probe(960, 540, submitted=112, presented=112, restart=1, generation=2)


def startup_log(approved=False):
    width, height = (960, 540) if approved else (1280, 720)
    log = row("UI_SETTINGS_STARTUP", approved=int(approved), epoch=2, generation=1,
              initialSubmitted=0, initialPresented=0, submitted=1, presented=1, failures=0, width=width, height=height)
    log += "UI_SETTINGS startup_recovery=complete\n"
    return log + probe(width, height) + probe(width, height, submitted=106, presented=106)


def config(width=1280, height=720):
    return f'seta r_windowWidth "{width}"\nseta r_windowHeight "{height}"\n'


class RecoveryOracle(unittest.TestCase):
    def test_canonical_marker_injection_preserves_every_other_byte(self):
        flat = journal()
        flat["external.unusual"] = "é\n.10000000000000001"
        raw = encoded(flat)
        changed = recovery.confirmed_record(raw)
        actual = recovery.decode_journal(changed)
        expected = dict(flat, state="confirmed")
        self.assertEqual(actual, expected)
        self.assertEqual(raw[len(recovery.MAGIC)+9:].replace(b'"state":"pending"', b'"state":"confirmed"'), changed[len(recovery.MAGIC)+9:])
        with self.assertRaises(ValueError):
            recovery.confirmed_record(changed)

    def test_malformed_journals_cannot_be_injected(self):
        valid = encoded(journal())
        cases = [b"", valid[:-1], valid.replace(b"pending", b"pendinG"), encoded(dict(journal(), schema=2)),
                 encoded(dict(journal(), attempt="xx" * 16)), encoded(dict(journal(), attempt="0" * 32)),
                 encoded(dict(journal(), schema=True)), encoded(dict(journal(), surprise=float("nan"))),
                 encoded(dict(journal(), surprise=10**1000)),
                 recovery.MAGIC + b"0" * (4 * 1024 * 1024)]
        payload = b'{"schema":1,"schema":1,"state":"pending","attempt":"abababababababababababababababab"}'
        cases.append(recovery.MAGIC + f"{zlib.crc32(payload):08x}\n".encode() + payload)
        for raw in cases:
            with self.subTest(size=len(raw)), self.assertRaises(ValueError):
                recovery.decode_journal(raw)

    def test_pending_requires_owned_view_followed_by_present_without_archiving(self):
        good = pending_log()
        self.assertEqual(recovery.pending_errors(good, journal(), config()), [])
        cases = [good.replace("operation=settings.system.edit", "operation=settings.system.confirm"),
                 good.replace("result=0 phase=1 owner=17", "result=3 phase=1 owner=17", 1),
                 good.replace("UI_SETTINGS_VIEW owner=17", "UI_SETTINGS_VIEW owner=19"),
                 good.replace("GUI_VALUE canConfirm=1", "GUI_VALUE canConfirm=0"),
                 good.replace("GUI_VALUE remaining=14.8", "GUI_VALUE remaining=nan"),
                 good.replace("GUI_VALUE request=1", "GUI_VALUE request=2"),
                 good.replace("UI_SETTINGS_PRESENT", "IGNORED_PRESENT"),
                 good.replace("submitted=101 presented=101", "submitted=101 presented=100"),
                 good.replace("GUI_VALUE dirty=1\n", ""),
                 good + row("UI_SETTINGS", operation="settings.system.apply", result=0, owner=17),
                 good.replace("state=pending durable=1", "state=confirmed durable=1"),
                 good.replace("restart=1", "restart=2")]
        for log in cases:
            with self.subTest(change=log):
                self.assertTrue(recovery.pending_errors(log, journal(), config()))
        self.assertTrue(recovery.pending_errors(good, journal(), config(960, 540)))
        self.assertTrue(recovery.pending_errors(good, journal(), ""))

    def test_startup_checks_actual_approved_direction_and_durable_cleanup(self):
        for approved in (False, True):
            flat = journal()
            flat["state"] = "confirmed" if approved else "pending"
            cfg = config(960, 540) if approved else config()
            log = startup_log(approved)
            self.assertEqual(recovery.startup_errors(log, flat, cfg, False), [])
            failures = [log.replace("UI_SETTINGS_STARTUP", "IGNORED_STARTUP"),
                        log.replace("initialPresented=0", "initialPresented=1"),
                        log.replace("initialSubmitted=0", "initialSubmitted=1"),
                        log.replace("failures=0", "failures=1", 1),
                        log.replace(f"approved={int(approved)}", f"approved={int(not approved)}"),
                        log.replace("presented=106", "presented=105"),
                        log.replace("interval=1", "interval=0"),
                        log + "RETAINED_GUI_LOADED settings-recovery.q4ui\n",
                        log + row("UI_SETTINGS", operation="settings.system.begin", result=0, owner=19),
                        log + "UI_SETTINGS startup_recovery=complete\n"]
            for altered in failures:
                with self.subTest(approved=approved, changed=altered):
                    self.assertTrue(recovery.startup_errors(altered, flat, cfg, False))
            self.assertTrue(recovery.startup_errors(log, flat, cfg, True))
            self.assertTrue(recovery.startup_errors(log, flat, config(1111, 777), False))
            if approved:
                # The actual windowed target legitimately leaves pixel size
                # unspecified; an explicit pixel policy must still be checked.
                flat["displayTarget.check.pixels"] = True
                self.assertTrue(recovery.startup_errors(log, flat, cfg, False))

    def test_semantic_scripts_do_not_close_or_replay(self):
        first, second = recovery.scripts()
        self.assertEqual(first.count('testGUI "'), 1)
        self.assertNotIn("testGUI\n", first)
        self.assertEqual(first.count('event "apply"'), 1)
        self.assertNotIn('event "keep"', first)
        self.assertNotIn('event "revert"', first)
        self.assertNotIn("testGUI", second)
        self.assertNotIn("openq4_retainedGui", second)
        for script in (first, second):
            self.assertTrue(script.endswith("quit\n"))
            self.assertNotIn("vid_restart", script)
            self.assertNotIn("rendererDisplayProbe apply", script)
            self.assertIn("screenshot ", script)

    def test_profiles_force_private_hidden_windowed_no_input_for_both_processes(self):
        profile = {"args": ["+set", "r_fullscreen", "1", "+seta", "in_mouse", "1", "+set", "ui_autoJoin", "0",
                            "+set", "r_windowWidth", "1280", "+seta", "r_windowHeight", "720", "+map", "game/airdefense1"]}
        for mode in ("sp", "mp"):
            for phase in ("pending", "startup"):
                args = recovery.command(profile, Path("runtime"), Path("assets"), Path("private"), mode, "vulkan", phase)
                values = {}
                for index, value in enumerate(args):
                    if value.lower() in ("+set", "+seta"):
                        self.assertNotIn(args[index+1], values)
                        values[args[index+1]] = args[index+2]
                for key in ("r_fullscreen", "r_fullscreenDesktop", "r_borderless", "in_mouse", "in_joystick", "in_joystickRumble"):
                    self.assertEqual(values[key], "0")
                self.assertEqual(values["r_hiddenWindow"], "1")
                self.assertEqual(values["ui_retainedTrace"], "1")
                self.assertEqual(values["ui_autoJoin"], "1" if mode == "mp" else "0")
                self.assertEqual(values["fs_savepath"], values["fs_devpath"])
                self.assertEqual(values["g_autoExecAfterMapLoad"], f"settings-recovery-{phase}.cfg")
                if phase == "startup":
                    self.assertNotIn("r_windowWidth", values)
                    self.assertNotIn("r_windowHeight", values)
                else:
                    self.assertEqual(values["r_windowWidth"], "1280")
                    self.assertEqual(values["r_windowHeight"], "720")

    def test_default_prepares_without_process_or_existing_evidence_replacement(self):
        scratch = ROOT / ".tmp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="recovery-prepare-test-", dir=scratch) as folder:
            root = Path(folder)
            runtime = root / ".install"
            runtime.mkdir()
            (runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")).write_bytes(b"fake; never execute")
            (root / ".vscode").mkdir()
            (root / ".vscode/launch.json").write_text(json.dumps({"configurations": [{"name": "(SP) airdefense1 test GL", "args": ["+map", "game/airdefense1"]}]}), encoding="utf-8")
            args = argparse.Namespace(output=root / ".tmp/case", runtime=runtime, assets=root / "assets", mode="sp", renderer="gl",
                                      state="pending", warning_baseline=None, execute=False, timeout=30)
            with patch.object(recovery, "ROOT", root), patch.object(recovery, "source_manifest", return_value={}), patch.object(recovery, "run_process", side_effect=AssertionError("must not launch")):
                self.assertEqual(recovery.capture(args), 0)
                report = json.loads((args.output / "capture.json").read_text(encoding="utf-8"))
                self.assertEqual(report["status"], "prepared")
                self.assertEqual(report["phases"], {})
                self.assertFalse((args.output / "save/baseoq4" / recovery.JOURNAL_NAME).exists())
                with self.assertRaises(ValueError):
                    recovery.capture(args)

    def test_common_oracle_rejects_new_diagnostics_wrong_renderer_and_replay(self):
        log = "game/airdefense1\nRenderer API: requested=gl active=gl disposition=selected\nCOMPLETE\n"
        self.assertEqual(recovery.common_errors(log, "sp", "gl", "COMPLETE", []), [])
        for bad in (log + "COMPLETE\n", log.replace("active=gl", "active=vulkan"), log.replace("game/airdefense1", "menu"), log + "WARNING: new warning\n"):
            self.assertTrue(recovery.common_errors(bad, "sp", "gl", "COMPLETE", []))
        self.assertEqual(recovery.common_errors(log + "WARNING: known\n", "sp", "gl", "COMPLETE", ["WARNING: known"]), [])
        self.assertTrue(recovery.common_errors(log + "ERROR: known\n", "sp", "gl", "COMPLETE", ["ERROR: known"]))

    def test_engine_tga_truncation_and_size(self):
        root = ROOT / ".tmp"
        root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="recovery-oracle-", dir=root) as folder:
            path = Path(folder) / "image.tga"
            header = bytearray(18)
            header[2], header[16] = 2, 24
            struct.pack_into("<HH", header, 12, 2, 3)
            path.write_bytes(header + bytes(18))
            self.assertEqual(recovery.image_record(path, (2, 3))["height"], 3)
            with self.assertRaises(ValueError):
                recovery.image_record(path, (3, 2))
            path.write_bytes(header + bytes(17))
            with self.assertRaises(ValueError):
                recovery.image_record(path, (2, 3))

    def test_lossless_unique_png_preview_preserves_orientation_and_alpha(self):
        scratch = ROOT / ".tmp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="recovery-png-test-", dir=scratch) as folder:
            path = Path(folder) / "frame.tga"
            # Bottom-origin RGBA TGA: blue/white bottom, red/green top.
            header = bytearray(18)
            header[2], header[16], header[17] = 2, 32, 8
            struct.pack_into("<HH", header, 12, 2, 2)
            path.write_bytes(header + bytes((255,0,0,11, 255,255,255,22, 0,0,255,33, 0,255,0,44)))
            result = recovery.image_record(path, (2, 2), preview_dir=Path(folder) / "review", preview_prefix="case-startup")
            preview = Path(result["preview"]["path"])
            self.assertIn(result["sha256"][:16], preview.name)
            self.assertTrue(preview.name.startswith("case-startup-"))
            png = preview.read_bytes()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            index, data = 8, b""
            while index < len(png):
                size = struct.unpack_from(">I", png, index)[0]
                kind, payload = png[index+4:index+8], png[index+8:index+8+size]
                self.assertEqual(struct.unpack_from(">I", png, index+8+size)[0], zlib.crc32(kind+payload))
                if kind == b"IDAT":
                    data += payload
                index += size+12
            self.assertEqual(zlib.decompress(data), bytes((0,255,0,0,33,0,255,0,44, 0,0,0,255,11,255,255,255,22)))


if __name__ == "__main__":
    unittest.main()
