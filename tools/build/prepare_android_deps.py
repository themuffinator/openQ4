#!/usr/bin/env python3
"""Build SDL3/OpenAL Soft Android dependencies from separately obtained sources.

The engine remains a Meson project. CMake here is each dependency's own build
entry point, using the same NDK/API/ABI as tools/build/android_cross.py.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import platform
import shutil
import subprocess

from android_elf import validate_library


def dependency_notices(ndk: Path, sdl_source: Path, openal_source: Path) -> list[tuple[Path, Path]]:
    """Collect notices before building, so incomplete sources cannot ship quietly."""
    notices: list[tuple[Path, Path]] = []
    for label, source, required, optional in (
        ("sdl3", sdl_source, ("LICENSE.txt",), ()),
        ("openal-soft", openal_source, ("COPYING",), ("LICENSE-pffft", "LICENSE-fmt", "LICENSE-gsl")),
        ("android-ndk", ndk, ("NOTICE.toolchain",), ("NOTICE",)),
    ):
        for name in required + optional:
            path = source / name
            if path.is_file():
                notices.append((path, Path("licenses") / label / name))
            elif name in required:
                raise ValueError(f"Missing required dependency notice: {path}")
    # OpenAL 1.24 keeps the bundled fmt notice in its source subdirectory.
    for path in openal_source.glob("fmt-*/LICENSE"):
        notices.append((path, Path("licenses/openal-soft/LICENSE-fmt")))
    return notices


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--sdl-source", type=Path, required=True)
    parser.add_argument("--openal-source", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--api", type=int, default=24)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    if args.api < 24 or args.jobs < 1:
        parser.error("API must be at least 24 and jobs must be positive")
    for path in (args.sdl_source, args.openal_source):
        if not (path / "CMakeLists.txt").is_file():
            parser.error(f"Missing dependency source CMakeLists.txt: {path}")
    toolchain = args.ndk.resolve() / "build/cmake/android.toolchain.cmake"
    if not toolchain.is_file():
        parser.error(f"Missing NDK CMake toolchain: {toolchain}")
    try:
        notices = dependency_notices(args.ndk, args.sdl_source, args.openal_source)
    except ValueError as exc:
        parser.error(str(exc))
    host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}.get(platform.system())
    if host is None:
        parser.error("Unsupported NDK build host")
    runtime = args.ndk / "toolchains/llvm/prebuilt" / host / "sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    if not runtime.is_file():
        parser.error(f"Missing shared NDK C++ runtime: {runtime}")
    try:
        validate_library(runtime, args.api)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    common = [
        "-G", "Ninja", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{args.api}",
        "-DANDROID_STL=c++_shared", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={args.prefix.resolve()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
    ]
    for name, source, options in (
        ("sdl3", args.sdl_source, ["-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"]),
        ("openal", args.openal_source, ["-DLIBTYPE=SHARED", "-DALSOFT_UTILS=OFF", "-DALSOFT_EXAMPLES=OFF", "-DALSOFT_TESTS=OFF"]),
    ):
        build = args.build_root.resolve() / name
        subprocess.run(["cmake", "-S", str(source.resolve()), "-B", str(build), *common, *options], check=True)
        subprocess.run(["cmake", "--build", str(build), "--parallel", str(args.jobs)], check=True)
        subprocess.run(["cmake", "--install", str(build)], check=True)
    destination = args.prefix / "lib/libc++_shared.so"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(runtime, destination)
    for name in ("libSDL3.so", "libopenal.so"):
        validate_library(args.prefix / "lib" / name, args.api)
    for source, relative_destination in notices:
        destination = args.prefix / relative_destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


if __name__ == "__main__":
    main()
