#!/usr/bin/env python3
"""Regression checks for the Linux ARM64 cross-compilation contract."""

from __future__ import annotations

import ast
import configparser
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_cross_file() -> None:
    source = read("tools/cross/linux-arm64.ini")

    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(source)
    if parser.sections() != ["binaries", "host_machine", "properties"]:
        raise AssertionError(f"Unexpected ARM64 cross-file sections: {parser.sections()!r}")
    pkg_config_libdir = ast.literal_eval(parser["properties"]["pkg_config_libdir"])
    if not isinstance(pkg_config_libdir, list) or not pkg_config_libdir:
        raise AssertionError("ARM64 pkg_config_libdir must be a non-empty Meson list")

    require(source, "c = 'aarch64-linux-gnu-gcc'", "ARM64 cross file")
    require(source, "cpp = 'aarch64-linux-gnu-g++'", "ARM64 cross file")
    require(source, "pkg-config = 'pkg-config'", "ARM64 cross file")
    reject(source, "\npkgconfig =", "deprecated ARM64 pkg-config machine-file key")
    require(source, "system = 'linux'", "ARM64 cross file")
    require(source, "cpu_family = 'aarch64'", "ARM64 cross file")
    require(source, "endian = 'little'", "ARM64 cross file")
    require(source, "'/usr/lib/aarch64-linux-gnu/pkgconfig'", "ARM64 pkg-config isolation")
    require(source, "needs_exe_wrapper = true", "non-runnable cross-build contract")
    reject(source, "'/usr/lib/x86_64-linux-gnu/pkgconfig'", "ARM64 pkg-config isolation")
    reject(source, "\nexe_wrapper =", "canonical cross file executable wrapper")


def validate_package_manifest() -> None:
    source = read("tools/cross/ubuntu-linux-arm64-packages.txt")

    for package in (
        "crossbuild-essential-arm64",
        "libwayland-bin",
        "libdbus-1-dev:arm64",
        "libdecor-0-dev:arm64",
        "libegl-dev:arm64",
        "libgl-dev:arm64",
        "libopengl-dev:arm64",
        "libopenal-dev:arm64",
        "libpipewire-0.3-dev:arm64",
        "libudev-dev:arm64",
        "libwayland-dev:arm64",
        "libxkbcommon-dev:arm64",
    ):
        require(source, package, "Ubuntu ARM64 package manifest")

    reject(source, "libwayland-bin:arm64", "native Wayland scanner package")


def validate_workflow() -> None:
    source = read(".github/workflows/linux-arm64-cross.yml")

    require(source, "runs-on: ubuntu-24.04", "x64 cross-build workflow")
    require(source, "sudo dpkg --add-architecture arm64", "x64 cross-build workflow")
    require(source, "Architectures: amd64", "x64 apt source isolation")
    require(source, "Architectures: arm64", "ARM64 apt source isolation")
    require(source, "tools/cross/ubuntu-linux-arm64-packages.txt", "cross-build package manifest")
    require(source, "--cross-file tools/cross/linux-arm64.ini", "Meson cross setup")
    require(source, "-Dplatform_backend=sdl3", "SDL3 ARM64 cross setup")
    require(source, "-Dlinux_x11=disabled", "Wayland-only ARM64 cross setup")
    require(source, "builddir-arm64-cross/openQ4-client_arm64", "ARM64 client verification")
    require(source, "builddir-arm64-cross/openQ4-ded_arm64", "ARM64 dedicated verification")
    require(source, "builddir-arm64-cross/baseoq4/game-sp_arm64.so", "ARM64 SP module verification")
    require(source, "builddir-arm64-cross/baseoq4/game-mp_arm64.so", "ARM64 MP module verification")
    require(source, "Machine:[[:space:]]+AArch64", "ARM64 ELF header verification")
    require(source, "Wayland-only ARM64 artifact unexpectedly depends on X11/GLX", "ARM64 Wayland-only dependency verification")
    require(source, "libX11|libXext|libGLX", "ARM64 Wayland-only forbidden dependency set")
    require(source, '($5 == "GLOBAL" || $5 == "WEAK" || $5 == "UNIQUE")', "ARM64 public symbol binding verification")
    require(source, '($6 == "DEFAULT" || $6 == "PROTECTED")', "ARM64 public symbol visibility verification")
    require(source, '$7 != "UND" &&', "ARM64 defined module export verification")
    require(source, '$4 == "FUNC" && $5 == "GLOBAL" && $6 == "DEFAULT" && $8 == "GetGameAPI"', "ARM64 function export verification")
    require(source, '$4 == "FUNC" && $5 == "GLOBAL" && $6 == "DEFAULT" && $8 == "openQ4_Mem_GetModuleStats"', "ARM64 memory-counter function export")
    require(source, "count != 2 || api != 1 || stats != 1 || unexpected != 0", "only API and per-module memory counters are public")
    require(source, 'cmp -s "${artifacts[2]}" "${artifacts[3]}"', "ARM64 SP/MP module separation")
    require(source, "meson_setup.sh install -C builddir-arm64-cross --no-rebuild --skip-subprojects", "ARM64 package staging")
    require(source, "python tools/validation/openq4_validate.py push", "ARM64 staged package validation")
    require(source, "--build-dir builddir-arm64-cross", "ARM64 staged package build directory")
    require(source, '--game-libs-repo "${OPENQ4_GAMELIBS_REPO}"', "ARM64 staged companion source validation")
    require(source, "--skip-python-tests", "ARM64 focused staged package validation")
    require(source, "--skip-build", "ARM64 post-build staged package validation")
    require(source, "--install", "ARM64 staged package validation")


def validate_build_machine_generators() -> None:
    meson = read("meson.build")
    baseoq4_meson = read("content/baseoq4/meson.build")
    swap_h = read("src/idlib/Swap.h")
    sdl_meson = read("subprojects/packagefiles/sdl3/meson.build")
    sdl_hidapi_meson = read("subprojects/packagefiles/sdl3/src/hidapi/meson.build")

    require(meson, "py = find_program('python', 'python3', native: true", "Windows native Python generator")
    require(meson, "py = find_program('python3', 'python', native: true", "Unix native Python generator")
    require(sdl_meson, "find_program('wayland-scanner', native: true)", "native Wayland scanner")
    require(sdl_meson, "host_machine.system() == 'darwin'", "portable SDL host OS detection")
    require(sdl_hidapi_meson, "host_machine.system() == 'darwin'", "portable SDL HIDAPI host OS detection")
    reject(sdl_meson, "host_machine.subsystem()", "SDL host OS detection")
    reject(sdl_hidapi_meson, "host_machine.subsystem()", "SDL HIDAPI host OS detection")

    require(baseoq4_meson, "game_sp_module_target = shared_module(", "SP linker target capture")
    require(baseoq4_meson, "game_mp_module_target = shared_module(", "MP linker target capture")
    # two Linux targets plus the two darwin targets, which hide their symbols
    # for the same reason (issue #90)
    if baseoq4_meson.count("gnu_symbol_visibility: 'hidden'") != 4:
        raise AssertionError("Linux and darwin SP and MP modules must hide non-API symbols")
    require(meson, "gnu_symbol_visibility: host_system == 'windows' ? 'default' : 'hidden'", "integrated game idlib visibility")
    require(meson, "'openq4_stage_game_sp_direct_run'", "SP direct-run staging target")
    require(meson, "input: game_sp_module_target", "SP direct-run staging dependency")
    require(meson, "'openq4_stage_game_mp_direct_run'", "MP direct-run staging target")
    require(meson, "input: game_mp_module_target", "MP direct-run staging dependency")
    require(meson, "'stage_direct_run_game_module.py'", "direct-run staging command")
    require(meson, "build_always_stale: true", "deleted direct-run module restoration")
    require(baseoq4_meson, "'openq4_stage_mod_manifest_direct_run'", "direct-run mod manifest target")
    require(baseoq4_meson, "input: baseoq4_manifest", "direct-run mod manifest dependency")
    require(
        baseoq4_meson,
        "meson.project_build_root() / 'baseoq4' / 'mod.json'",
        "direct-run mod manifest destination",
    )
    require(swap_h, "static constexpr uint32_t idSwapBig32( const uint32_t value )", "single-evaluation BIG32 helper")
    require(swap_h, "static constexpr uint16_t idSwapBig16( const uint16_t value )", "single-evaluation BIG16 helper")
    require(swap_h, "return static_cast<uint16_t>( ( value >> 8 ) | ( value << 8 ) );", "truncated BIG16 result")
    require(swap_h, 'static_assert( idSwapBig32( 0x12345678u ) == 0x78563412u', "compile-time BIG32 value contract")
    require(swap_h, 'static_assert( idSwapBig16( 0x1234u ) == 0x3412u', "compile-time BIG16 value contract")
    reject(swap_h, "((uint32)(v))", "undefined BIG32 alias")
    reject(swap_h, "((uint16)(v))", "undefined BIG16 alias")


def validate_direct_run_module_staging() -> None:
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    script = ROOT / "tools" / "build" / "stage_direct_run_game_module.py"

    with tempfile.TemporaryDirectory(prefix="arm64-module-stage-", dir=temporary_root) as work:
        work_root = Path(work)
        source = work_root / "linked" / "game-sp_arm64.so"
        destination = work_root / "builddir" / "baseoq4" / source.name
        stamp = work_root / "builddir" / "stage-game-sp-arm64.stamp"
        source.parent.mkdir(parents=True)

        def stage() -> None:
            subprocess.run(
                [
                    sys.executable,
                    str(script),
                    "--source",
                    str(source),
                    "--destination",
                    str(destination),
                    "--stamp",
                    str(stamp),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

        source.write_bytes(b"arm64-module-v1")
        stage()
        if destination.read_bytes() != b"arm64-module-v1" or not stamp.is_file():
            raise AssertionError("Initial direct-run game module staging failed")

        source.write_bytes(b"arm64-module-v2")
        stage()
        if destination.read_bytes() != b"arm64-module-v2":
            raise AssertionError("Incremental direct-run game module refresh failed")

        destination.unlink()
        stage()
        if destination.read_bytes() != b"arm64-module-v2":
            raise AssertionError("Deleted direct-run game module was not restored")


def validate_documentation() -> None:
    source = read("docs/dev/linux-arm64-cross-compilation.md")

    require(source, "tools/cross/linux-arm64.ini", "cross-build documentation")
    require(source, "OPENQ4_GAMELIBS_REPO", "cross-build companion repository guidance")
    require(source, "PKG_CONFIG_ALLOW_CROSS=1", "cross-build pkg-config guidance")
    require(source, "-Dlinux_x11=disabled", "Wayland-only cross-build guidance")
    require(source, "Native ARM64 CI remains authoritative", "native runtime authority guidance")
    require(source, "does not set `sys_root`", "multiarch sysroot guidance")


def main() -> None:
    validate_cross_file()
    validate_package_manifest()
    validate_workflow()
    validate_build_machine_generators()
    validate_direct_run_module_staging()
    validate_documentation()
    print("linux_arm64_cross_compile: ok")


if __name__ == "__main__":
    main()
