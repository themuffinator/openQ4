#!/usr/bin/env python3
"""Check Android cross-toolchain validation and renderer-free engine source split."""

from __future__ import annotations

from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/build"))
import android_cross


class AndroidBuildTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows Meson wrapper")
    def test_windows_wrapper_refreshes_android_game_sources(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-wrapper-test-", dir=ROOT / ".tmp") as temporary:
            fixture = Path(temporary)
            project = fixture / "openQ4"
            game = fixture / "openQ4-game"
            build = project / "builddir"
            scripts = project / "tools/build"
            scripts.mkdir(parents=True)
            for filename in ("meson_setup.ps1", "gamelibs_stage_path.py"):
                shutil.copy2(ROOT / "tools/build" / filename, scripts / filename)
            for module in ("game", "mpgame"):
                (game / "src" / module).mkdir(parents=True)
                (game / "src" / module / "Game_local.cpp").write_text("// game\n", encoding="utf-8")
            (project / "src").mkdir()
            (build / "meson-private").mkdir(parents=True)
            (build / "meson-info").mkdir()
            (build / "meson-private/coredata.dat").touch()
            (build / "build.ninja").touch()
            (build / "meson-info/intro-machines.json").write_text(
                json.dumps({"host": {"system": "android", "cpu_family": "aarch64"}}), encoding="utf-8")
            (build / "meson-info/intro-buildoptions.json").write_text(
                json.dumps([{"name": "build_engine", "value": True}, {"name": "build_games", "value": True}]), encoding="utf-8")
            # Record wrapper dispatch without loading MSVC or running a compiler.
            fake_meson = fixture / "python/mesonbuild"
            fake_meson.mkdir(parents=True)
            (fake_meson / "__init__.py").touch()
            log = fixture / "meson.jsonl"
            (fake_meson / "mesonmain.py").write_text(
                "import json, os, sys\n"
                "if __name__ == '__main__':\n"
                "    with open(os.environ['OPENQ4_TEST_MESON_LOG'], 'a') as output:\n"
                "        output.write(json.dumps(sys.argv[1:]) + '\\n')\n", encoding="utf-8")
            env = os.environ.copy()
            env.update({"PYTHONPATH": str(fake_meson.parent), "OPENQ4_GAMELIBS_REPO": str(game),
                        "OPENQ4_TEST_MESON_LOG": str(log)})

            def invoke() -> list[list[str]]:
                log.unlink(missing_ok=True)
                result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                         str(scripts / "meson_setup.ps1"), "compile", "-Cbuilddir"],
                                        cwd=project, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            self.assertEqual([call[0] for call in invoke()], ["setup", "compile"])
            stage = project / ".tmp/gamelibs_stage-android-arm64"
            subprocess.run([sys.executable, str(ROOT / "tools/build/stage_gamelibs.py"),
                            str(project), str(game), str(stage)], check=True, capture_output=True)
            self.assertEqual([call[0] for call in invoke()], ["compile"])
            (game / "src/game/Game_local.cpp").write_text("// changed game\n", encoding="utf-8")
            self.assertEqual([call[0] for call in invoke()], ["setup", "compile"])

    def test_ndk_target_and_missing_tools(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-build-test-", dir=ROOT / ".tmp") as temporary:
            ndk = Path(temporary)
            toolchain = ndk / "toolchains/llvm/prebuilt/windows-x86_64"
            binaries = toolchain / "bin"
            binaries.mkdir(parents=True)
            for name in ("clang.exe", "clang++.exe", "llvm-ar.exe", "llvm-strip.exe"):
                (binaries / name).touch()
            (toolchain / "sysroot/usr/lib/aarch64-linux-android/24").mkdir(parents=True)
            contents = android_cross.generate(ndk, 24, "windows-x86_64")
            self.assertIn("--target=aarch64-linux-android24", contents)
            self.assertIn("system = 'android'", contents)
            self.assertIn("needs_exe_wrapper = true", contents)
            self.assertNotIn(".cmd", contents)
            self.assertIn("max-page-size=16384", contents)
            with self.assertRaisesRegex(ValueError, "API 24"):
                android_cross.generate(ndk, 23, "windows-x86_64")
            with self.assertRaisesRegex(ValueError, "requested arm64 API"):
                android_cross.generate(ndk, 25, "windows-x86_64")
            (binaries / "clang.exe").unlink()
            with self.assertRaisesRegex(ValueError, "Missing NDK tool"):
                android_cross.generate(ndk, 24, "windows-x86_64")

    def test_module_only_android_sources(self) -> None:
        command = [sys.executable, str(ROOT / "tools/build/meson_sources.py"),
                   "--host-system", "android", "--include-game", "false", "--renderer", "module"]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        sources = result.stdout.splitlines()
        self.assertEqual([p for p in sources if p.startswith("src/renderer/")],
                         ["src/renderer/RendererModule.cpp"])
        self.assertIn("src/sys/android/android_main.cpp", sources)
        self.assertIn("src/sys/android/android_sdl3.cpp", sources)
        self.assertFalse(any("/sys/linux/" in p or "/sys/win32/" in p for p in sources))
        for rejected in (["--platform-backend", "native"], ["--target-kind", "dedicated"]):
            result = subprocess.run(command + rejected, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Android requires the SDL3 client", result.stderr)


if __name__ == "__main__":
    unittest.main()
