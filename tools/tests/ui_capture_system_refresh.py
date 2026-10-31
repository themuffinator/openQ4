#!/usr/bin/env python3
"""Negative controls for external SYSTEM refresh capture evidence; no game launch."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ui"))
import capture_system_refresh as capture


def trace(mode="sp"):
    lines = ["OPENQ4_MENU_ACTIVATION PASS"]
    for row in capture.stages():
        lines += [f'{capture.MARKER} {row["name"]}',
                  f"OPENQ4_SYSTEM enabled=1 active={capture.page.PAGE} parent={capture.page.PARENT} "
                  f"child=1 guiTest=0 menu=1 map=1 multiplayer={int(mode == 'mp')}"]
        lines += [f"GUI_VALUE {key}={value}" for key, value in zip(capture.FIELDS, row["values"])]
        if row["conflict"] is not None:
            lines.append(f'RETAINED_GUI_NUMBER id={capture.NUMBER} active=1 bytes=1 conflict={row["conflict"]} ')
    return "\n".join([*lines, capture.COMPLETE, ""])


class RefreshEvidence(unittest.TestCase):
    def test_current_route_and_all_readbacks(self):
        for mode in ("sp", "mp"):
            self.assertTrue(capture.qualify(trace(mode), mode)["passed"])

    def test_corrupted_evidence_rejected(self):
        source = trace()
        mutations = [
            ("GUI_VALUE draftPreset=minimum", "GUI_VALUE draftPreset=balanced"),
            ("GUI_VALUE baselinePreset=minimum", "GUI_VALUE baselinePreset=balanced"),
            ("GUI_VALUE dirty=1", "GUI_VALUE dirty=0"),
            ("GUI_VALUE message=#str_230011", "GUI_VALUE message=#str_230007"),
            ("GUI_VALUE numberDraftsPending=1", "GUI_VALUE numberDraftsPending=0"),
            ("GUI_VALUE draftBrightness=1.1", "GUI_VALUE draftBrightness=1"),
            ("GUI_VALUE canApply=0", "GUI_VALUE canApply=1"),
            ("GUI_VALUE baselineBrightness=1.5", "GUI_VALUE baselineBrightness=1"),
            ("map=1", "map=0"), ("guiTest=0", "guiTest=1"), ("multiplayer=0", "multiplayer=1"),
            ("conflict=1", "conflict=0"), ("bytes=1 ", "bytes=0 "),
            (f"{capture.MARKER} ultra", f"{capture.MARKER} minimum"),
            ("GUI_VALUE open=1\n", ""),
            (capture.COMPLETE, ""), ("OPENQ4_MENU_ACTIVATION PASS", ""),
        ]
        for before, after in mutations:
            with self.subTest(before=before):
                self.assertIn(before, source)
                self.assertFalse(capture.qualify(source.replace(before, after, 1), "sp")["passed"])
        self.assertFalse(capture.qualify(source + "\nSYSTEM_REFRESH_STAGE open\n", "sp")["passed"])
        self.assertFalse(capture.qualify(source.replace("GUI_VALUE open=1", "RETAINED_GUI_OPERATION number failed\nGUI_VALUE open=1", 1), "sp")["passed"])

    def test_window_capture_uses_semantic_commands(self):
        script = capture.script()
        self.assertIn("openq4_assertMenuActivation 10000", script)
        self.assertEqual(script.count("screenshot "), 9)
        self.assertEqual(script.count("vid_restart\n"), 2)
        self.assertNotIn("testGUI", script)
        self.assertEqual(script.splitlines()[-1], "quit")
        # The Number answers only on its tab: each stage that focuses it switches first.
        for row in capture.stages():
            focus = [i for i, command in enumerate(row["commands"]) if command.startswith("openq4_retainedGui focus")]
            if focus:
                self.assertEqual(row["commands"][focus[0] - 2:focus[0]], ["openq4_retainedGui event tab_image", "wait 3"])

    def test_effective_renderer_identity(self):
        self.assertEqual(capture.active_renderer("Renderer API: gl (module)\n"), "gl")
        self.assertEqual(capture.active_renderer("Renderer API: vulkan (module)\n"), "vulkan")
        self.assertEqual(capture.active_renderer("Renderer API: requested=vulkan active=gl\n"), "gl")
        self.assertIsNone(capture.active_renderer("set r_renderApi vulkan\n"))
        self.assertIsNone(capture.active_renderer("Renderer API: unknown\n"))


if __name__ == "__main__":
    unittest.main()
