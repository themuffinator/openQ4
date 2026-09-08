#!/usr/bin/env python3
"""Check Android cross-toolchain validation and renderer-free engine source split."""

from __future__ import annotations

from pathlib import Path
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/build"))
import android_cross
import prepare_android_deps
import android_elf


def android_library(api: int = 24, alignment: int = 16384, machine: int = 183) -> bytes:
    """Small valid ELF fixture with one load segment and an NDK target note."""
    data = bytearray(200)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, machine, 1)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HHH", data, 52, 64, 56, 2)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0, len(data), len(data), alignment)
    struct.pack_into("<IIQQQQQQ", data, 120, 4, 4, 176, 176, 176, 24, 24, 4)
    struct.pack_into("<III8sI", data, 176, 8, 4, 1, b"Android\0", api)
    return bytes(data)


class AndroidBuildTests(unittest.TestCase):
    def test_library_validation_checks_loadability(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-elf-test-", dir=ROOT / ".tmp") as temporary:
            library = Path(temporary) / "libfixture.so"
            library.write_bytes(android_library(api=21))
            self.assertEqual(android_elf.validate_library(library, 24), 21)
            for contents, error in (
                (android_library(api=28), "above the configured API 24"),
                (android_library(alignment=4096), "16 KiB"),
                (android_library(machine=62), "AArch64"),
                (android_library()[:130], "program headers"),
            ):
                library.write_bytes(contents)
                with self.assertRaisesRegex(ValueError, error):
                    android_elf.validate_library(library, 24)
            library.write_bytes(android_library(api=28))
            self.assertEqual(android_elf.validate_library(library, 28), 28)

    def test_dependency_preparation_preserves_and_requires_notices(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="android-deps-test-", dir=ROOT / ".tmp") as temporary:
            fixture = Path(temporary)
            ndk, sdl, openal = (fixture / name for name in ("ndk", "sdl", "openal"))
            sources = {
                ndk / "build/cmake/android.toolchain.cmake": "# toolchain\n",
                ndk / "NOTICE.toolchain": "C++ runtime notice\n",
                ndk / "NOTICE": "NDK notice\n",
                sdl / "CMakeLists.txt": "# SDL\n",
                sdl / "LICENSE.txt": "SDL notice\n",
                openal / "CMakeLists.txt": "# OpenAL\n",
                openal / "COPYING": "OpenAL notice\n",
                openal / "LICENSE-pffft": "PFFFT notice\n",
                openal / "fmt-11.1.3/LICENSE": "fmt notice\n",
            }
            for path, contents in sources.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            prefix = fixture / "prefix"
            runtime = android_library(api=21)
            for path in (
                ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so",
                prefix / "lib/libSDL3.so", prefix / "lib/libopenal.so",
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(runtime)
            argv = ["prepare_android_deps.py", "--ndk", str(ndk), "--sdl-source", str(sdl),
                    "--openal-source", str(openal), "--prefix", str(prefix),
                    "--build-root", str(fixture / "build"), "--api", "28"]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(prepare_android_deps.platform, "system", return_value="Windows"):
                with mock.patch.object(prepare_android_deps.subprocess, "run") as runner:
                    prepare_android_deps.main()
                # The dependency build must honor the same target API and
                # page-size settings for both independent native libraries.
                self.assertEqual(runner.call_count, 6)
                for call in (runner.call_args_list[0], runner.call_args_list[3]):
                    self.assertIn("-DANDROID_PLATFORM=android-28", call.args[0])
                    self.assertIn("-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", call.args[0])
                for relative, expected in {
                    "licenses/sdl3/LICENSE.txt": "SDL notice\n",
                    "licenses/openal-soft/COPYING": "OpenAL notice\n",
                    "licenses/openal-soft/LICENSE-fmt": "fmt notice\n",
                    "licenses/openal-soft/LICENSE-pffft": "PFFFT notice\n",
                    "licenses/android-ndk/NOTICE.toolchain": "C++ runtime notice\n",
                }.items():
                    self.assertEqual((prefix / relative).read_text(encoding="utf-8"), expected)
                self.assertEqual((prefix / "lib/libc++_shared.so").read_bytes(), runtime)
                (sdl / "LICENSE.txt").unlink()
                with mock.patch.object(prepare_android_deps.subprocess, "run") as runner:
                    with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                        prepare_android_deps.main()
                    self.assertEqual(error.exception.code, 2)
                    runner.assert_not_called()

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
