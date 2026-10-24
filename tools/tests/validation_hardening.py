#!/usr/bin/env python3
"""Regression checks for validation-runner input and staging hardening."""

from __future__ import annotations

import argparse
import configparser
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import uuid
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
WORK_BASE = ROOT / ".tmp" / "validation-hardening-test"


def make_work_root() -> Path:
    return WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"


WORK = make_work_root()


def load_module(name: str, path: Path) -> ModuleType:
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VALIDATOR = load_module("openq4_validation_hardening_test", ROOT / "tools" / "validation" / "openq4_validate.py")


def write_file(path: Path, data: bytes = b"x\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def expect_validation_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except VALIDATOR.ValidationError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_argument_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except argparse.ArgumentTypeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def with_host_flags(windows: bool, linux: bool, macos: bool, callback) -> None:
    original_windows = VALIDATOR.host_is_windows
    original_linux = VALIDATOR.host_is_linux
    original_macos = VALIDATOR.host_is_macos
    VALIDATOR.host_is_windows = lambda: windows
    VALIDATOR.host_is_linux = lambda: linux
    VALIDATOR.host_is_macos = lambda: macos
    try:
        callback()
    finally:
        VALIDATOR.host_is_windows = original_windows
        VALIDATOR.host_is_linux = original_linux
        VALIDATOR.host_is_macos = original_macos


def validate_positive_integer_arguments() -> None:
    if VALIDATOR.positive_int("4") != 4:
        raise AssertionError("positive_int returned the wrong value")

    for value in ("0", "-1", "not-a-number"):
        expect_argument_error(
            lambda value=value: VALIDATOR.positive_int(value),
            "positive integer" if value != "not-a-number" else "not an integer",
            f"positive_int({value!r})",
        )

    source = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    for token in (
        'parser.add_argument("--jobs", "-j", type=positive_int',
        'parser.add_argument("--runtime-timeout", type=positive_int',
    ):
        if token not in source:
            raise AssertionError(f"validation runner is missing argument hardening token {token!r}")


def validate_source_and_build_dir_guards() -> None:
    VALIDATOR.validate_source_root(ROOT)

    source_link = WORK / "source-root-link"
    try:
        os.symlink(ROOT, source_link, target_is_directory=True)
    except (OSError, NotImplementedError):
        pass
    else:
        expect_validation_error(
            lambda: VALIDATOR.validate_source_root(source_link),
            "must not be a symlink",
            "symlink source root",
        )

    fake_root = WORK / "not-openq4"
    fake_root.mkdir(parents=True, exist_ok=True)
    expect_validation_error(
        lambda: VALIDATOR.validate_source_root(fake_root),
        "missing required openQ4 files",
        "invalid source root",
    )

    expect_validation_error(lambda: VALIDATOR.validate_build_dir(ROOT, ROOT), "source root", "source-root build dir")
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, ROOT.parent),
        "contain the source root",
        "ancestor build dir",
    )
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, ROOT / "docs/dev" / "validation-build"),
        "must live under .tmp/ or use a builddir* name",
        "source-controlled build dir",
    )

    file_build_dir = WORK / "build-file"
    write_file(file_build_dir)
    expect_validation_error(
        lambda: VALIDATOR.validate_build_dir(ROOT, file_build_dir),
        "not a directory",
        "file build dir",
    )

    symlink_target = WORK / "build-target"
    symlink_build_dir = WORK / "build-link"
    symlink_target.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(symlink_target, symlink_build_dir)
    except (OSError, NotImplementedError):
        pass
    else:
        expect_validation_error(
            lambda: VALIDATOR.validate_build_dir(ROOT, symlink_build_dir),
            "must not be a symlink",
            "symlink build dir",
        )

    VALIDATOR.validate_build_dir(ROOT, ROOT / "builddir-validation-hardening")
    VALIDATOR.validate_build_dir(ROOT, ROOT / ".tmp" / "validation-hardening" / "build")


def validate_game_libs_repo_guards() -> None:
    game_libs_target = WORK / "openQ4-game-real"
    game_libs_link = WORK / "openQ4-game-link"
    write_file(game_libs_target / "src" / "game" / "Game_local.cpp")

    expect_validation_error(
        lambda: VALIDATOR.ensure_game_libs_repo({"OPENQ4_GAMELIBS_REPO": str(game_libs_target)}),
        "multiplayer game source directory was not found",
        "missing multiplayer GameLibs source tree",
    )
    write_file(game_libs_target / "src" / "mpgame" / "Game_local.cpp")

    try:
        os.symlink(game_libs_target, game_libs_link, target_is_directory=True)
    except (OSError, NotImplementedError):
        return

    expect_validation_error(
        lambda: VALIDATOR.ensure_game_libs_repo({"OPENQ4_GAMELIBS_REPO": str(game_libs_link)}),
        "must not be a symlink",
        "symlink GameLibs repository",
    )

    args = argparse.Namespace(game_libs_repo=str(game_libs_link), build_gamelibs=False, skip_icon_sync=False)
    expect_validation_error(
        lambda: VALIDATOR.validation_env(args, ROOT),
        "must not be a symlink",
        "symlink GameLibs CLI argument",
    )

    default_root = WORK / "default-gamelibs" / "openQ4"
    default_target = WORK / "default-gamelibs" / "openQ4-game-real"
    default_link = WORK / "default-gamelibs" / "openQ4-game"
    write_file(default_root / "meson.build")
    write_file(default_target / "src" / "game" / "Game_local.cpp")
    try:
        os.symlink(default_target, default_link, target_is_directory=True)
    except (OSError, NotImplementedError):
        return

    default_args = argparse.Namespace(game_libs_repo="", build_gamelibs=False, skip_icon_sync=False)
    original_default = os.environ.get("OPENQ4_GAMELIBS_REPO")
    try:
        os.environ.pop("OPENQ4_GAMELIBS_REPO", None)
        env = VALIDATOR.validation_env(default_args, default_root)
        if Path(env["OPENQ4_GAMELIBS_REPO"]) != default_root.resolve():
            raise AssertionError("the canonical default must ignore a linked companion checkout")
    finally:
        if original_default is None:
            os.environ.pop("OPENQ4_GAMELIBS_REPO", None)
        else:
            os.environ["OPENQ4_GAMELIBS_REPO"] = original_default


def validate_staged_symlink_guard() -> None:
    install_root_link_root = WORK / "symlink-install-root"
    install_root_target = WORK / "symlink-install-target"
    install_root_link_root.mkdir(parents=True, exist_ok=True)
    install_root_target.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(install_root_target, install_root_link_root / ".install", target_is_directory=True)
    except (OSError, NotImplementedError):
        pass
    else:
        expect_validation_error(
            lambda: VALIDATOR.validate_staged_payload(install_root_link_root, dry_run=False),
            "Install root must not be a symlink",
            "symlink install root",
        )

    game_dir_link_root = WORK / "symlink-game-root"
    game_dir_target = WORK / "symlink-game-target"
    (game_dir_link_root / ".install").mkdir(parents=True, exist_ok=True)
    game_dir_target.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(game_dir_target, game_dir_link_root / ".install" / "baseoq4", target_is_directory=True)
    except (OSError, NotImplementedError):
        pass
    else:
        expect_validation_error(
            lambda: VALIDATOR.validate_staged_payload(game_dir_link_root, dry_run=False),
            "Staged game directory must not be a symlink",
            "symlink staged game root",
        )

    root = WORK / "symlink-payload"
    install_root = root / ".install"
    target = root / "outside.txt"
    link = install_root / "baseoq4" / "linked.txt"
    write_file(target)
    link.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.symlink(target, link)
    except (OSError, NotImplementedError):
        return

    expect_validation_error(
        lambda: VALIDATOR.validate_no_staged_symlinks(root, install_root),
        "symlink entries",
        "staged symlink guard",
    )


def validate_recursive_non_runtime_scan() -> None:
    root = WORK / "non-runtime"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    write_file(game_dir / "nested" / "debug.lib")
    expect_validation_error(
        lambda: VALIDATOR.validate_no_non_runtime_artifacts(
            root,
            (install_root, game_dir),
            VALIDATOR.NON_RUNTIME_PATTERNS,
            "Non-runtime artifacts remain staged",
        ),
        "nested/debug.lib",
        "recursive non-runtime artifact scan",
    )


def validate_staged_casefold_collision_guard() -> None:
    collision = VALIDATOR.first_staged_casefold_path_collision(
        ["baseoq4/pak0.pk4", "OpenQ4.icns", "openQ4.icns"]
    )
    if collision != ("OpenQ4.icns", "openQ4.icns"):
        raise AssertionError(f"unexpected casefold collision result: {collision!r}")

    root = WORK / "casefold-payload"
    install_root = root / ".install"
    write_file(install_root / "OpenQ4.icns")
    if not (install_root / "openQ4.icns").exists():
        write_file(install_root / "openQ4.icns")
        expect_validation_error(
            lambda: VALIDATOR.validate_no_staged_casefold_path_collisions(root, install_root),
            "case-insensitive duplicate paths",
            "staged casefold collision guard",
        )


def validate_engine_architecture_mismatch() -> None:
    root = WORK / "engine-arch"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_arm64.exe"
    write_file(client)
    write_file(dedicated)

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "engine binary architecture mismatch",
            "engine architecture mismatch",
        ),
    )


def validate_game_module_suffix_guard() -> None:
    root = WORK / "module-suffix"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_x64.exe"
    write_file(client)
    write_file(dedicated)
    write_file(game_dir / "game-sp_x64.so")
    write_file(game_dir / "game-mp_x64.dll")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "wrong platform suffix",
            "game module suffix mismatch",
        ),
    )


def validate_game_module_architecture_match() -> None:
    root = WORK / "module-arch"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    client = install_root / "openQ4-client_x64.exe"
    dedicated = install_root / "openQ4-ded_x64.exe"
    write_file(client)
    write_file(dedicated)
    write_file(game_dir / "game-sp_x64.dll")
    write_file(game_dir / "game-mp_arm64.dll")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated]),
            "game module architecture mismatch",
            "game module architecture mismatch",
        ),
    )

    (game_dir / "game-mp_arm64.dll").unlink()
    write_file(game_dir / "game-mp_x64.dll")

    def assert_valid_arch_set() -> None:
        arches = VALIDATOR.validate_staged_architecture_set(root, game_dir, [client], [dedicated])
        if arches != {"x64"}:
            raise AssertionError(f"unexpected staged architecture set: {arches!r}")

    with_host_flags(True, False, False, assert_valid_arch_set)


def validate_game_module_distinctness_guard() -> None:
    root = WORK / "module-distinctness"
    game_dir = root / ".install" / "baseoq4"
    sp_module = game_dir / "game-sp_x64.so"
    mp_module = game_dir / "game-mp_x64.so"
    write_file(sp_module, b"same-module\n")
    write_file(mp_module, b"same-module\n")

    expect_validation_error(
        lambda: VALIDATOR.validate_distinct_game_modules(root, [sp_module], [mp_module]),
        "byte-identical",
        "SP/MP game module distinctness",
    )

    write_file(mp_module, b"mp-module!!\n")
    VALIDATOR.validate_distinct_game_modules(root, [sp_module], [mp_module])


def validate_linux_runtime_dependency_guards() -> None:
    root = WORK / "linux-dedicated-dependencies"
    client_x64 = root / ".install" / "openQ4-client_x64"
    client_arm64 = root / ".install" / "openQ4-client_arm64"
    dedicated_x64 = root / ".install" / "openQ4-ded_x64"
    dedicated_arm64 = root / ".install" / "openQ4-ded_arm64"
    mp_x64 = root / ".install" / "baseoq4" / "game-mp_x64.so"
    mp_arm64 = root / ".install" / "baseoq4" / "game-mp_arm64.so"
    original_readelf_output = VALIDATOR.readelf_output
    dynamic_section = ""

    def fake_readelf_output(path: Path, args: list[str], source_root: Path) -> str:
        if args != ["-W", "-d"]:
            raise AssertionError(f"unexpected Linux dependency readelf arguments: {args!r}")
        if path not in (
            client_x64,
            client_arm64,
            dedicated_x64,
            dedicated_arm64,
            mp_x64,
            mp_arm64,
        ) or source_root != root:
            raise AssertionError(f"unexpected Linux dependency path: {path}")
        return dynamic_section

    try:
        VALIDATOR.readelf_output = fake_readelf_output
        dynamic_section = """
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
"""
        VALIDATOR.validate_linux_client_runtime_dependencies(
            root,
            [client_x64, client_arm64],
        )
        VALIDATOR.validate_linux_dedicated_runtime_dependencies(
            root,
            [
                (dedicated_x64, "x64"),
                (dedicated_arm64, "arm64"),
                (mp_x64, "x64"),
                (mp_arm64, "arm64"),
            ],
        )

        dynamic_section = """
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libc++.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc++abi.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libunwind.so.1]
"""
        VALIDATOR.validate_linux_dedicated_runtime_dependencies(
            root,
            [(dedicated_x64, "x64"), (mp_x64, "x64")],
        )

        dynamic_section = """
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libatomic.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
"""
        VALIDATOR.validate_linux_dedicated_runtime_dependencies(
            root,
            [(dedicated_arm64, "arm64"), (mp_arm64, "arm64")],
        )

        dynamic_section = (
            " 0x0000000000000001 (NEEDED)             "
            "Shared library: [libpipewire-0.3.so.0]\n"
        )
        expect_validation_error(
            lambda: VALIDATOR.validate_linux_client_runtime_dependencies(
                root,
                [client_x64],
            ),
            "libpipewire-0.3.so.0",
            "Linux client direct PipeWire dependency guard",
        )

        for forbidden_library in (
            "libopenal.so.1",
            "libOpenGL.so.0",
            "libGL.so.1",
            "libGLX.so.0",
            "libEGL.so.1",
            "libGLESv2.so.2",
            "libvulkan.so.1",
            "libSDL3.so.0",
            "libpipewire-0.3.so.0",
            "libwayland-client.so.0",
            "libdecor-0.so.0",
            "libX11.so.6",
            "libXext.so.6",
            "libxcb.so.1",
            "libfuture-client-runtime.so.1",
        ):
            dynamic_section = (
                " 0x0000000000000001 (NEEDED)             "
                f"Shared library: [{forbidden_library}]\n"
            )
            expect_validation_error(
                lambda: VALIDATOR.validate_linux_dedicated_runtime_dependencies(
                    root,
                    [(mp_x64, "x64")],
                ),
                forbidden_library,
                f"Linux dedicated dependency guard for {forbidden_library}",
            )

        dynamic_section = (
            " 0x0000000000000001 (NEEDED)             "
            "Shared library: [ld-linux-aarch64.so.1]\n"
        )
        expect_validation_error(
            lambda: VALIDATOR.validate_linux_dedicated_runtime_dependencies(
                root,
                [(dedicated_x64, "x64")],
            ),
            "ld-linux-aarch64.so.1",
            "cross-architecture dedicated loader dependency",
        )
        expect_validation_error(
            lambda: VALIDATOR.validate_linux_dedicated_runtime_dependencies(
                root,
                [(dedicated_x64, "riscv64")],
            ),
            "riscv64",
            "unsupported dedicated dependency architecture",
        )
    finally:
        VALIDATOR.readelf_output = original_readelf_output


def validate_windows_pdb_architecture_match() -> None:
    root = WORK / "windows-pdb"
    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    write_file(install_root / "OpenAL32.dll")
    write_file(install_root / "openQ4-client_arm64.pdb")

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}),
            "missing architecture-matched diagnostic PDB",
            "missing exact PDB architecture",
        ),
    )

    for path in (
        install_root / "openQ4-client_x64.pdb",
        install_root / "openQ4-ded_x64.pdb",
        game_dir / "game-sp_x64.pdb",
        game_dir / "game-mp_x64.pdb",
    ):
        write_file(path)

    with_host_flags(
        True,
        False,
        False,
        lambda: expect_validation_error(
            lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}),
            "stale or architecture-mismatched PDB",
            "stale PDB architecture",
        ),
    )

    (install_root / "openQ4-client_arm64.pdb").unlink()
    with_host_flags(True, False, False, lambda: VALIDATOR.validate_windows_symbols(root, install_root, game_dir, {"x64"}))


def validate_python_test_failure_aggregation() -> None:
    original_run_command = VALIDATOR.run_command
    observed: list[str] = []
    simulated_failures = {"arena_campaign.py", "vscode_fast_build.py"}

    def fake_run_command(command, *, cwd, env, title, dry_run) -> None:
        test_name = Path(command[1]).name
        observed.append(test_name)
        if test_name in simulated_failures:
            raise VALIDATOR.ValidationError(f"simulated failure: {test_name}")

    VALIDATOR.run_command = fake_run_command
    try:
        expect_validation_error(
            lambda: VALIDATOR.run_python_tests(
                argparse.Namespace(dry_run=False, python_jobs=1),
                ROOT,
                {},
            ),
            "2 Python validation check(s) failed",
            "aggregated Python validation failures",
        )
    finally:
        VALIDATOR.run_command = original_run_command

    # The parallel runner must aggregate the same failures from both ends.
    original_captured = VALIDATOR.run_python_check_captured
    parallel_observed: list[str] = []

    def fake_captured(test_script, test_args, root, env):
        parallel_observed.append(Path(test_script).name)
        failed = Path(test_script).name in simulated_failures
        return (1 if failed else 0), f"simulated {'failure' if failed else 'pass'}\n", 0.0

    VALIDATOR.run_python_check_captured = fake_captured
    try:
        expect_validation_error(
            lambda: VALIDATOR.run_python_tests(
                argparse.Namespace(dry_run=False, python_jobs=4),
                ROOT,
                {},
            ),
            "2 Python validation check(s) failed",
            "aggregated parallel Python validation failures",
        )
    finally:
        VALIDATOR.run_python_check_captured = original_captured
    # Checks create their temporary directories under .tmp without making it, so
    # the parallel runner must create it before any check can start.
    with tempfile.TemporaryDirectory(prefix="validation-hardening-tmp-", dir=ROOT / ".tmp") as fresh:
        fresh_root = Path(fresh)
        VALIDATOR.run_python_check_captured = lambda *args: (0, "", 0.0)
        try:
            VALIDATOR.run_python_checks_parallel([(fresh_root / "probe.py", [])], fresh_root, {}, 2)
        finally:
            VALIDATOR.run_python_check_captured = original_captured
        if not (fresh_root / ".tmp").is_dir():
            raise AssertionError("the parallel Python runner must create .tmp before starting checks")

    if sorted(parallel_observed) != sorted(observed):
        raise AssertionError("the parallel Python runner did not run the same checks as the sequential one")

    if not simulated_failures.issubset(observed):
        raise AssertionError(
            "Python validation stopped before reporting failures from both ends of the suite"
        )


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    settings_coverage = (ROOT / "tools" / "tests" / "settings_menu_coverage.py").read_text(
        encoding="utf-8"
    )
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    release_notes = (ROOT / "docs/dev" / "release-completion.md").read_text(encoding="utf-8")

    for token in (
        "positive_int",
        "validate_source_root",
        "validate_game_libs_repo_path",
        "validate_build_dir",
        "validate_no_staged_symlinks",
        "validate_no_staged_casefold_path_collisions",
        "validate_staged_architecture_set",
        "validate_distinct_game_modules",
        "validate_linux_client_runtime_dependencies",
        "validate_linux_dedicated_runtime_dependencies",
        "validate_windows_symbols",
        "validate_no_non_runtime_artifacts",
        "-Dbuild_native_tests=true",
        "test_args",
        "Meson native tests",
        "Install root must not be a symlink",
        "Staged game directory must not be a symlink",
    ):
        if token not in validator:
            raise AssertionError(f"validation runner is missing {token}")

    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "validation_hardening.py" not in text:
            raise AssertionError(f"validation_hardening.py is not wired into {context}")

    if 'os.environ.get("OPENQ4_GAMELIBS_REPO"' not in settings_coverage:
        raise AssertionError("settings menu coverage ignores the configured GameLibs repository")

    # Runtime drivers need a built target package or retail assets. Helpers are
    # exercised by their native harnesses or runtime callers. Keep both out of
    # the argument-free lightweight suite; CI still checks their syntax.
    smoke_wiring_allowlist = {
        # Stage 2 drivers: each launches a staged client, dedicated server,
        # WSL peer or OpenXR runtime on retail assets; the workflows py_compile
        # them and their production-extraction siblings are wired.
        "arena_sound_runtime.py",
        "presentation_look_runtime.py",
        "mp_wsl_peer_smoke.py",
        "openxr_lifecycle_runtime.py",
        "savegame_restore_cleanup_runtime.py",
        "savegame_restore_recovery_runtime.py",
        "ui_generated_image_runtime.py",
        # Audio/voice drivers: retail assets, a staged OpenAL Soft provider,
        # ffmpeg or a launched game; their native-compile siblings are wired.
        "audio_asset_inventory.py",
        "audio_center.py",
        "audio_dialogue.py",
        "audio_dialogue_coverage.py",
        "audio_memory_runtime.py",
        "audio_output_recovery_runtime.py",
        "audio_mod_reverb.py",
        "audio_scene_audit.py",
        "audio_streaming.py",
        "audio_subtitles_runtime.py",
        "voice_chat_runtime.py",
        "voice_chat_soak.py",
        "voice_mixed_version.py",
        "german_localization_smoke.py",
        "localization_smoke.py",
        "level_editor_runtime.py",
        "level_editor_workspace.py",
        "mp_bot_gameplay_smoke.py",
        "renderer_classic_colors.py",
        "renderer_font_reload.py",
        "renderer_hdr_highlights.py",
        "renderer_hdr_linear_capture.py",
        "renderer_hdr_linear_compare.py",
        "renderer_image_capture.py",
        "renderer_image_recovery_fixture.py",
        "renderer_pbr_ambient_parity.py",
        "renderer_pbr_diagnostic_parity.py",
        "renderer_pbr_environment_parity.py",
        "renderer_pbr_geometry_parity.py",
        "renderer_pbr_laboratory.py",
        "renderer_pbr_native_gl_parity.py",
        "renderer_pbr_preview.py",
        "renderer_pbr_probe_parity.py",
        "renderer_render_target_lifetime.py",
        "renderer_smaa_orientation.py",
        "renderer_vulkan_arb_programs.py",
        "renderer_vulkan_hdr_gameplay.py",
        "renderer_vulkan_hdr_post.py",
        "renderer_vulkan_hdr_post_compare.py",
        "renderer_vulkan_hdr_scene.py",
        "renderer_vulkan_hdr_scene_compare.py",
        "renderer_vulkan_material_lighting.py",
        "renderer_vulkan_material_programs.py",
        "renderer_vulkan_material_reload.py",
        "renderer_vulkan_pbr_ambient.py",
        "renderer_vulkan_pbr_baked.py",
        "renderer_vulkan_pbr_capacity.py",
        "renderer_vulkan_pbr_cutout.py",
        "renderer_vulkan_pbr_cutout_alpha.py",
        "renderer_vulkan_pbr_diagnostics.py",
        "renderer_vulkan_pbr_fog.py",
        "renderer_vulkan_pbr_geometry.py",
        "renderer_vulkan_pbr_probes.py",
        "renderer_vulkan_pbr_resources.py",
        "renderer_vulkan_pbr_transparency.py",
        "renderer_vulkan_recovery_gameplay.py",
        "ui_choice_scroll_runtime.py",
        "ui_document_canvas.py",
        "ui_document_canvas_mutations.py",
        "ui_document_view_projection.py",
        "ui_focus_reveal.py",
        "ui_popup_placement_runtime.py",
        "ui_scrollbar_runtime.py",
        "ui_system_presets_runtime.py",
        "windows_text_collection_bridge.py",
        "windows_text_session.py",
        "windows_text_store.py",
        # Windows-only runtime smoke needs a staged package; its source
        # contract and workflow cover it outside the portable Python suite.
        "windows_dedicated_server_smoke.py",
        "chat_gameplay_smoke.py",
        "mp_browser_smoke.py",
        "mp_control_smoke.py",
        "mp_duel_queue_smoke.py",
        "mp_match_control_team_join_smoke.py",
        "mp_match_flow_smoke.py",
        "mp_mvd_smoke.py",
        "mp_pause_lifecycle_smoke.py",
        "mp_pause_world_smoke.py",
        "mp_recording_ownership_smoke.py",
        "mp_roster_smoke.py",
        "mp_round_remote_smoke.py",
        "mp_series_smoke.py",
        "mp_tourney_smoke.py",
        "renderer_shadow_mapping_maps.py",
        "renderer_shadow_mapping_outdoor.py",
        "renderer_shadow_mapping_terrain.py",
        "renderer_shadow_mapping_movers.py",
        "renderer_shadow_mapping_scenes.py",
        "renderer_shadow_mapping_transitions.py",
        "generate_file_memory_test.py",
        # Generated production excerpts are compiled by the Meson native tests.
        "generate_aas_tactical_test.py",
        "generate_bitmsg_test.py",
        "mp_view_decoder.py",
        "linux_dedicated_server_smoke.py",
        "linux_dedicated_stock_map_smoke.py",
        "linux_wayland_stock_sp_smoke.py",
        "macos_dedicated_server_smoke.py",
        "windows_dedicated_server_smoke.py",
        "renderer_gameplay_benchmark.py",
        # Vulkan support evidence runs need a staged client, a GPU and retail
        # assets.
        "renderer_vulkan_qualification.py",
        # Campaign runtime checks require user-supplied retail/expansion assets.
        "campaign_runtime.py",
        "campaign_selection_runtime.py",
        "awakening_m01_cinematic.py",
        "awakening_status_runtime.py",
        "monster_hit_materials.py",
        "mp_bot_audit.py",
        # Isolated bot jump replays launch a staged MP server and client.
        "mp_bot_jump_replay.py",
        # Impulse-death replays launch a staged MP server and client; their
        # policy is exercised without assets by mp_bot_death_policy.py.
        "mp_bot_impulse_replay.py",
        # Stock sound distance measurements launch a staged client on retail assets.
        "sound_distance_runtime.py",
        # Drives a rooted x86_64 Android emulator; the workflows py_compile it.
        "android_emulator_smoke.py",
        "renderer_milestone_d_acceptance.py",
        "renderer_milestone_d_fixture.py",
        # Radio graph evidence plays decoded audio through a staged client on
        # retail assets.
        "audio_graphs.py",
        # Need a staged OpenXR client, the built test runtime and retail assets.
        "openxr_vr_smoke.py",
        "openxr_vr_menu_smoke.py",
        "openxr_vr_mp_smoke.py",
        # Imported by the UI harnesses that compile wrap sources; the workflows
        # py_compile it, and it is exercised through those harnesses.
        "wrap_sources.py",
        # The NumPy reference the PBR laboratory oracles import; its fitted
        # kernels are tested natively in PBRMathTest.
        "pbr_reference.py",
    }
    discovered_tests = sorted(path.name for path in (ROOT / "tools" / "tests").glob("*.py"))
    if not discovered_tests:
        raise AssertionError("no python tests discovered under tools/tests")
    unknown_allowlist_entries = smoke_wiring_allowlist.difference(discovered_tests)
    if unknown_allowlist_entries:
        raise AssertionError(f"smoke wiring allowlist names missing tests: {sorted(unknown_allowlist_entries)}")

    # Recovered game checks run through one explicit registry in the native
    # suite. Validate that route before accepting its members as registered;
    # an arbitrary missing filename must still fail the checks below.
    game_suite = load_module("openq4_registered_game_suite", ROOT / "tools/tests/game_regression_suite.py")
    native_game_checks = {Path(path).name for path in game_suite.discover()}
    meson = (ROOT / "meson.build").read_text(encoding="utf-8")
    for token in ("test('openq4-imported-game-regressions', py",
                  "files('tools/tests/game_regression_suite.py')"):
        if token not in meson:
            raise AssertionError("recovered game checks are disconnected from the native suite")
    for workflow_name, workflow in (("push", push), ("commit", commit)):
        if "python tools/tests/game_regression_suite.py --check-discovery" not in workflow:
            raise AssertionError(f"recovered game discovery is missing from {workflow_name} workflow")
    for test_name in discovered_tests:
        if test_name in smoke_wiring_allowlist:
            continue
        if test_name in native_game_checks:
            continue
        if test_name not in validator:
            raise AssertionError(f"{test_name} is not wired into local validation")
        if test_name not in push:
            raise AssertionError(f"{test_name} is not wired into push workflow smoke checks")
        if test_name not in commit:
            raise AssertionError(f"{test_name} is not wired into commit workflow smoke checks")

    for build_script in (
        "package_nightly.py",
        "package_release.py",
        "stage_windows_runtime.py",
        "sync_icons.py",
    ):
        if build_script not in push or build_script not in commit:
            raise AssertionError(f"{build_script} is not covered by workflow py_compile smoke checks")

    windows_start = commit.index("\n  windows-x64:")
    windows_end = commit.index("\n  linux-arm64:", windows_start)
    windows_job = commit[windows_start:windows_end]
    openal_step_name = "- name: Prepare OpenAL Soft (Windows)"
    if windows_job.count(openal_step_name) != 1:
        raise AssertionError(f"Expected exactly one {openal_step_name!r} in Windows commit validation")
    openal_start = windows_job.index(openal_step_name)
    openal_end = windows_job.index("\n      - name:", openal_start + len(openal_step_name))
    openal_step = windows_job[openal_start:openal_end]
    for token in (
        "tools/build/prepare_windows_openal.ps1",
        '$env:OPENQ4_VS_TARGET_ARCH = "x64"',
        '-Architecture "x64" -OutputRoot $outputRoot',
        "OPENQ4_OPENAL_ROOT=$outputRoot",
    ):
        if token not in openal_step:
            raise AssertionError(f"Windows commit validation is missing OpenAL runtime preparation token {token!r}")
    if "\n        if:" in openal_step:
        raise AssertionError("Windows commit validation conditionally skips OpenAL runtime preparation")
    if windows_job.index(openal_step_name) >= windows_job.index("Run selected validation profile"):
        raise AssertionError("Windows commit validation prepares OpenAL after the build/staging profile")

    if "Validation runs now fail earlier" not in release_notes:
        raise AssertionError("release notes do not mention validation hardening")


def wrap_tree_stem(directory: str) -> str:
    return re.sub(r"-[0-9][0-9A-Za-z.]*$", "", directory)


def validate_workflow_wrap_paths() -> None:
    # A workflow step that reads an unpacked wrap tree must name the version the
    # wrap pins. Local checkouts keep old unpacked trees, so a stale path passes
    # locally and fails only on a clean runner: the SDL 3.4.16 bump left the
    # Windows text-store step reading subprojects/SDL3-3.4.10.
    pinned: dict[str, str] = {}
    for wrap in sorted((ROOT / "subprojects").glob("*.wrap")):
        config = configparser.ConfigParser(interpolation=None)
        config.read(wrap, encoding="utf-8")
        for section in config.sections():
            directory = config[section].get("directory")
            if directory:
                pinned[wrap_tree_stem(directory)] = directory
    if "SDL3" not in pinned:
        raise AssertionError("subprojects/sdl3.wrap does not pin an SDL3 directory")

    for workflow in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        text = workflow.read_text(encoding="utf-8")
        for match in re.finditer(r"subprojects[/\\]([A-Za-z0-9_.+-]+)", text):
            directory = match.group(1)
            expected = pinned.get(wrap_tree_stem(directory))
            if expected is not None and directory != expected:
                raise AssertionError(
                    f"{workflow.name} reads subprojects/{directory}, but the wrap pins {expected}"
                )


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_positive_integer_arguments()
        validate_source_and_build_dir_guards()
        validate_game_libs_repo_guards()
        validate_staged_symlink_guard()
        validate_recursive_non_runtime_scan()
        validate_staged_casefold_collision_guard()
        validate_engine_architecture_mismatch()
        validate_game_module_suffix_guard()
        validate_game_module_architecture_match()
        validate_game_module_distinctness_guard()
        validate_linux_runtime_dependency_guards()
        validate_windows_pdb_architecture_match()
        validate_python_test_failure_aggregation()
        validate_validation_wiring()
        validate_workflow_wrap_paths()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("validation_hardening: ok")


if __name__ == "__main__":
    main()
