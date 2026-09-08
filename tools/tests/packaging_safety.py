#!/usr/bin/env python3
"""Regression checks for package, staging, and PK4 safety guards."""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import uuid
from pathlib import Path
from types import ModuleType
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "tools" / "build"
WORK_BASE = ROOT / ".tmp" / "packaging-safety-test"


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
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


OPENQ4_PAK = load_module("openq4_pak_safety_test", BUILD_DIR / "openq4_pak.py")
BUILD_PAK0 = load_module("build_pak0_safety_test", BUILD_DIR / "build_pak0.py")
CHECK_STAGED_CONTENT = load_module("check_staged_content_safety_test", BUILD_DIR / "check_staged_content_edits.py")
PACKAGE = load_module("package_nightly_safety_test", BUILD_DIR / "package_nightly.py")
STAGE_GAMELIBS = load_module("stage_gamelibs_safety_test", BUILD_DIR / "stage_gamelibs.py")
VERSION = load_module("openq4_version_safety_test", BUILD_DIR / "openq4_version.py")
WINDOWS_RUNTIME = load_module("windows_runtime_safety_test", BUILD_DIR / "windows_runtime.py")
MESON_SOURCES = load_module("meson_sources_safety_test", BUILD_DIR / "meson_sources.py")
FAST_STAGE = load_module("stage_fast_install_safety_test", BUILD_DIR / "stage_fast_install.py")


def write_file(path: Path, data: bytes = b"data\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def expect_runtime_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_value_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except ValueError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def make_symlink(target: Path, link: Path, *, target_is_directory: bool = False) -> bool:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (OSError, NotImplementedError):
        return False
    return True


def run_script(script: Path, *args: Path | str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script), *(str(arg) for arg in args)],
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def validate_pk4_source_containment() -> None:
    source_root = WORK / "pak-source-root"
    inside = source_root / "content" / "baseoq4" / "pak0"
    outside = WORK / "outside-pack"
    write_file(inside / "materials" / "ok.mtr")
    write_file(outside / "materials" / "bad.mtr")

    manifest = OPENQ4_PAK.format_pk4_source_manifest(source_root, inside, "pak0.pk4")
    if "materials/ok.mtr" not in manifest:
        raise AssertionError("safe pack source was not listed in manifest")

    expect_runtime_error(
        lambda: OPENQ4_PAK.format_pk4_source_manifest(source_root, outside, "pak0.pk4"),
        "must stay under",
        "pack source containment",
    )
    expect_runtime_error(
        lambda: OPENQ4_PAK.create_game_pk4(source_root / "missing", WORK / "missing.pk4", required_files=set()),
        "source directory not found",
        "missing pack source",
    )
    linked_source = source_root / "content" / "baseoq4" / "linked-pak0"
    if make_symlink(inside, linked_source, target_is_directory=True):
        expect_runtime_error(
            lambda: OPENQ4_PAK.create_game_pk4(linked_source, WORK / "linked.pk4", required_files=set()),
            "source directory must not be a symlink",
            "symlinked PK4 source directory",
        )

    result = run_script(BUILD_DIR / "list_pak_sources.py", source_root, "pak0.pk4", "../outside-pack")
    if result.returncode == 0 or "must stay under" not in result.stderr:
        raise AssertionError(f"list_pak_sources.py accepted an escaped source dir: {result.stderr}")

    linked_source_root = WORK / "linked-pak-source-root"
    if make_symlink(source_root, linked_source_root, target_is_directory=True):
        result = run_script(BUILD_DIR / "list_pak_sources.py", linked_source_root, "pak0.pk4", "content/baseoq4/pak0")
        if result.returncode == 0 or "source root must not be a symlink" not in result.stderr:
            raise AssertionError(f"list_pak_sources.py accepted a symlinked source root: {result.stderr}")
        result = run_script(
            BUILD_DIR / "write_pak_manifest.py",
            linked_source_root,
            "pak0.pk4",
            "content/baseoq4/pak0",
            WORK / "linked.sources",
        )
        if result.returncode == 0 or "source root must not be a symlink" not in result.stderr:
            raise AssertionError(f"write_pak_manifest.py accepted a symlinked source root: {result.stderr}")

    linked_content_root = source_root / "content" / "baseoq4" / "linked-manifest"
    if make_symlink(inside, linked_content_root, target_is_directory=True):
        result = run_script(
            BUILD_DIR / "write_pak_manifest.py",
            source_root,
            "pak0.pk4",
            "content/baseoq4/linked-manifest",
            WORK / "linked-content.sources",
        )
        if result.returncode == 0 or "directory must not be a symlink" not in result.stderr:
            raise AssertionError(f"write_pak_manifest.py accepted a symlinked content root: {result.stderr}")


def write_zip(path: Path, entries: list[tuple[ZipInfo, bytes]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as archive:
        for info, data in entries:
            archive.writestr(info, data)


def zip_info(name: str, mode: int = 0o100644) -> ZipInfo:
    info = ZipInfo(name)
    info.compress_type = ZIP_DEFLATED
    info.external_attr = mode << 16
    return info


def validate_pk4_archive_member_guards() -> None:
    for name, message in (
        ("../escape.txt", "unsafe archive path"),
        ("C:/escape.txt", "unsafe archive path"),
        ("folder//escape.txt", "unsafe archive path"),
    ):
        archive_path = WORK / "bad-archives" / f"{name.replace(':', '_').replace('/', '_').replace(chr(92), '_')}.pk4"
        write_zip(archive_path, [(zip_info(name), b"x")])
        expect_runtime_error(
            lambda archive_path=archive_path: OPENQ4_PAK.inspect_game_pk4(archive_path, required_files=set()),
            message,
            f"unsafe PK4 member {name!r}",
        )
    expect_runtime_error(
        lambda: OPENQ4_PAK.validate_pk4_arcname("folder\\escape.txt", "pak0.pk4"),
        "unsafe archive path",
        "PK4 backslash member",
    )

    duplicate_path = WORK / "bad-archives" / "duplicate.pk4"
    write_zip(duplicate_path, [(zip_info("foo.txt"), b"1"), (zip_info("FOO.txt"), b"2")])
    expect_runtime_error(
        lambda: OPENQ4_PAK.inspect_game_pk4(duplicate_path, required_files=set()),
        "duplicate archive path",
        "duplicate PK4 member",
    )

    symlink_path = WORK / "bad-archives" / "symlink.pk4"
    write_zip(symlink_path, [(zip_info("linked.txt", stat.S_IFLNK | 0o777), b"target")])
    expect_runtime_error(
        lambda: OPENQ4_PAK.inspect_game_pk4(symlink_path, required_files=set()),
        "non-regular archive entry",
        "PK4 symlink member",
    )


def validate_copy_helpers_do_not_follow_destination_symlinks() -> None:
    source = WORK / "copy" / "source.txt"
    destination = WORK / "copy" / "dest.txt"
    outside = WORK / "copy-outside.txt"
    write_file(source, b"new\n")
    write_file(outside, b"outside\n")

    if not make_symlink(outside, destination):
        return

    copied = OPENQ4_PAK.copy_file_if_changed(source, destination)
    if not copied:
        raise AssertionError("copy_file_if_changed should replace a destination symlink")
    if outside.read_bytes() != b"outside\n":
        raise AssertionError("copy_file_if_changed followed a destination symlink")
    if destination.is_symlink() or destination.read_bytes() != b"new\n":
        raise AssertionError("copy_file_if_changed did not replace the symlink with the source file")

    source_link = WORK / "copy" / "source-link.txt"
    if make_symlink(source, source_link):
        expect_runtime_error(
            lambda: OPENQ4_PAK.copy_file_if_changed(source_link, WORK / "copy" / "dest-from-link.txt"),
            "symlinked source file",
            "copy_file_if_changed source symlink guard",
        )


def validate_pk4_replace_helper_does_not_preserve_destination_symlink() -> None:
    source = WORK / "replace" / "source.pk4.tmp"
    destination = WORK / "replace" / "pak0.pk4"
    outside = WORK / "replace-outside.pk4"
    write_file(source, b"same\n")
    write_file(outside, b"same\n")

    if not make_symlink(outside, destination):
        return

    OPENQ4_PAK._replace_file_if_changed(source, destination)
    if outside.read_bytes() != b"same\n":
        raise AssertionError("_replace_file_if_changed followed a destination symlink")
    if destination.is_symlink() or destination.read_bytes() != b"same\n":
        raise AssertionError("_replace_file_if_changed did not replace the destination symlink")


def validate_generated_text_writers_do_not_follow_symlinks() -> None:
    outside = WORK / "generated-text-outside.h"
    write_file(outside, b"outside\n")

    pak_header = WORK / "generated" / "openq4_paks_generated.h"
    pak_header.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, pak_header):
        OPENQ4_PAK.write_text_if_changed(pak_header, "inside\n")
        if outside.read_bytes() != b"outside\n":
            raise AssertionError("openq4_pak.write_text_if_changed followed a destination symlink")
        if pak_header.is_symlink() or pak_header.read_text(encoding="utf-8") != "inside\n":
            raise AssertionError("openq4_pak.write_text_if_changed did not replace the destination symlink")

    version_header = WORK / "generated" / "openq4_version_generated.h"
    if make_symlink(outside, version_header):
        VERSION.write_if_changed(version_header, "version\n")
        if outside.read_bytes() != b"outside\n":
            raise AssertionError("openq4_version.write_if_changed followed a destination symlink")
        if version_header.is_symlink() or version_header.read_text(encoding="utf-8") != "version\n":
            raise AssertionError("openq4_version.write_if_changed did not replace the destination symlink")


def validate_legacy_build_pak0_copy_does_not_follow_symlink() -> None:
    source = WORK / "build-pak0-copy" / "pak0.pk4"
    destination = WORK / "build-pak0-copy" / "stage" / "pak0.pk4"
    outside = WORK / "build-pak0-copy-outside.pk4"
    write_file(source, b"pak\n")
    write_file(outside, b"outside\n")
    destination.parent.mkdir(parents=True, exist_ok=True)

    if not make_symlink(outside, destination):
        return

    BUILD_PAK0.copy_if_changed(source, destination)
    if outside.read_bytes() != b"outside\n":
        raise AssertionError("build_pak0.copy_if_changed followed a destination symlink")
    if destination.is_symlink() or destination.read_bytes() != b"pak\n":
        raise AssertionError("build_pak0.copy_if_changed did not replace the destination symlink")


def validate_legacy_build_pak0_cli_guards() -> None:
    source_root = WORK / "build-pak0-cli"
    pak0_source = source_root / "pak0"
    pak1_source = source_root / "pak1"
    write_file(pak0_source / "materials" / "ok.mtr")
    write_file(pak1_source / "gfx" / "loadscreen.tga")

    pak0_link = source_root / "pak0-link"
    if make_symlink(pak0_source, pak0_link, target_is_directory=True):
        result = run_script(
            BUILD_DIR / "build_pak0.py",
            "--pak0-source-dir",
            pak0_link,
            "--pak1-source-dir",
            pak1_source,
            "--pak0-out",
            WORK / "build-pak0-cli" / "pak0.pk4",
            "--pak1-out",
            WORK / "build-pak0-cli" / "pak1.pk4",
            "--header-out",
            WORK / "build-pak0-cli" / "openq4_paks_generated.h",
        )
        if result.returncode == 0 or "pak0.pk4 source directory must not be a symlink" not in result.stderr:
            raise AssertionError(f"build_pak0.py accepted a symlinked pak0 source: {result.stderr}")


def validate_list_sources_guards() -> None:
    source_root = WORK / "source-list-root"
    write_file(source_root / "src" / "ok.cpp", b"// ok\n")
    outside = WORK / "source-list-outside"
    write_file(outside / "bad.cpp", b"// bad\n")

    result = run_script(BUILD_DIR / "list_sources.py", source_root, "../source-list-outside")
    if result.returncode == 0 or "must stay under" not in result.stderr:
        raise AssertionError(f"list_sources.py accepted an escaped source dir: {result.stderr}")

    source_root_link = WORK / "source-list-root-link"
    if make_symlink(source_root, source_root_link, target_is_directory=True):
        result = run_script(BUILD_DIR / "list_sources.py", source_root_link, "src")
        if result.returncode == 0 or "source root must not be a symlink" not in result.stderr:
            raise AssertionError(f"list_sources.py accepted a symlinked source root: {result.stderr}")

    source_subdir_link = source_root / "linked-src"
    if make_symlink(source_root / "src", source_subdir_link, target_is_directory=True):
        result = run_script(BUILD_DIR / "list_sources.py", source_root, "linked-src")
        if result.returncode == 0 or "source subtree must not be a symlink" not in result.stderr:
            raise AssertionError(f"list_sources.py accepted a symlinked source subtree: {result.stderr}")

    link = source_root / "src" / "linked.cpp"
    if make_symlink(source_root / "src" / "ok.cpp", link):
        result = run_script(BUILD_DIR / "list_sources.py", source_root, "src")
        if result.returncode == 0 or "refusing to list symlinked source file" not in result.stderr:
            raise AssertionError(f"list_sources.py accepted a symlinked source: {result.stderr}")


# stage_fast_install.py stages by the host's own runtime policy, so the
# guard/copy fixture must use the host platform's artifact names; explicit
# per-platform policy output is pinned by validate_fast_stage_cross_platform_policy.
FAST_STAGE_HOST_FIXTURES = {
    "win32": ("openQ4-client_x64.exe", "renderer-gl_x64.dll", "game-sp_x64.dll"),
    "linux": ("openQ4-client_x64", "renderer-gl_x64.so", "game-sp_x64.so"),
    "darwin": ("openQ4-client_x64", "renderer-vk_x64.dylib", "game-sp_x64.dylib"),
}


def validate_fast_stage_guards_and_copy() -> None:
    if sys.platform not in FAST_STAGE_HOST_FIXTURES:
        raise AssertionError(f"unsupported fast-stage host platform: {sys.platform}")
    client_name, renderer_name, game_name = FAST_STAGE_HOST_FIXTURES[sys.platform]
    source_root = WORK / "fast-stage" / "openQ4"
    build_dir = source_root / "builddir"
    install_dir = source_root / ".install"
    write_file(build_dir / client_name, b"client\n")
    write_file(build_dir / renderer_name, b"renderer\n")
    write_file(build_dir / "baseoq4" / game_name, b"game\n")
    write_file(build_dir / "baseoq4" / "pak0.pk4", b"pak0\n")

    bad_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        source_root / ".tmp" / "not-install",
    )
    if bad_result.returncode == 0 or "install directory must be" not in bad_result.stderr:
        raise AssertionError(f"stage_fast_install.py accepted an unsafe install dir: {bad_result.stderr}")

    overlap_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        install_dir / "nested-builddir",
        "--install-dir",
        install_dir,
    )
    if overlap_result.returncode == 0 or "must not overlap" not in overlap_result.stderr:
        raise AssertionError(f"stage_fast_install.py accepted build dir under install dir: {overlap_result.stderr}")

    symlink_root = WORK / "fast-stage-symlink"
    symlink_source_root = symlink_root / "openQ4"
    symlink_build_dir = symlink_source_root / "builddir"
    symlink_install_dir = symlink_source_root / ".install"
    symlink_build_target = symlink_root / "real-builddir"
    symlink_build_target.mkdir(parents=True, exist_ok=True)
    symlink_source_root.mkdir(parents=True, exist_ok=True)
    if make_symlink(symlink_build_target, symlink_build_dir, target_is_directory=True):
        symlink_result = run_script(
            BUILD_DIR / "stage_fast_install.py",
            "--source-root",
            symlink_source_root,
            "--build-dir",
            symlink_build_dir,
            "--install-dir",
            symlink_install_dir,
        )
        if symlink_result.returncode == 0 or "build directory must not be a link or junction" not in symlink_result.stderr:
            raise AssertionError(f"stage_fast_install.py accepted a symlinked build dir: {symlink_result.stderr}")

    result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        install_dir,
    )
    if result.returncode != 0:
        raise AssertionError(f"stage_fast_install.py failed safe staging: {result.stderr}")
    if not (install_dir / client_name).is_file():
        raise AssertionError("fast stage did not copy root runtime binary")
    if not (install_dir / "baseoq4" / game_name).is_file():
        raise AssertionError("fast stage did not copy game runtime binary")
    if not (install_dir / renderer_name).is_file():
        raise AssertionError("fast stage did not copy renderer runtime binary")

    escaped_temporary = source_root / ".tmp" / "other-runtime" / "capture"
    escaped_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        escaped_temporary,
        "--temporary-runtime",
    )
    if escaped_result.returncode == 0 or "must stay below" not in escaped_result.stderr:
        raise AssertionError(
            f"stage_fast_install.py accepted an escaped temporary runtime: {escaped_result.stderr}"
        )

    temporary_runtime = source_root / ".tmp" / "stock-runtime" / "capture"
    temporary_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        temporary_runtime,
        "--temporary-runtime",
    )
    if temporary_result.returncode != 0:
        raise AssertionError(
            f"stage_fast_install.py failed isolated temporary staging: {temporary_result.stderr}"
        )
    if not (temporary_runtime / client_name).is_file():
        raise AssertionError("temporary fast stage did not copy root runtime binary")
    if not (temporary_runtime / renderer_name).is_file():
        raise AssertionError("temporary fast stage did not copy renderer runtime binary")
    repeated_result = run_script(
        BUILD_DIR / "stage_fast_install.py",
        "--source-root",
        source_root,
        "--build-dir",
        build_dir,
        "--install-dir",
        temporary_runtime,
        "--temporary-runtime",
    )
    if repeated_result.returncode == 0 or "must be new" not in repeated_result.stderr:
        raise AssertionError(
            f"stage_fast_install.py reused a temporary runtime: {repeated_result.stderr}"
        )


def validate_fast_stage_cross_platform_policy() -> None:
    source_root = WORK / "fast-stage-platform-policy" / "builddir"
    source_game = source_root / "baseoq4"
    root_files = (
        "openQ4-client_x64",
        "openQ4-ded_x64",
        "openQ4-client_x64.exe",
        "openQ4-client_x64.pdb",
        "openQ4-ded_x64.exe",
        "openQ4-ded_x64.pdb",
        "renderer-gl_x64.dll",
        "renderer-gl_x64.pdb",
        "renderer-vk_x64.dll",
        "renderer-vk_x64.pdb",
        "renderer-gl_x64.so",
        "renderer-vk_x64.so",
        "renderer-gl_x64.dylib",
        "renderer-vk_x64.dylib",
        # MoltenVK is provider-staged and verified separately; it must never
        # be mistaken for a Meson-produced fast-stage artifact.
        "libMoltenVK.dylib",
        "OpenAL32.dll",
    )
    game_files = (
        "game-sp_x64.dll",
        "game-sp_x64.pdb",
        "game-mp_x64.dll",
        "game-mp_x64.pdb",
        "game-sp_x64.so",
        "game-mp_x64.so",
        "game-sp_x64.dylib",
        "game-mp_x64.dylib",
        "mod.json",
        "pak0.pk4",
        "pak1.pk4",
    )
    for filename in root_files:
        write_file(source_root / filename, filename.encode("utf-8"))
    for filename in game_files:
        write_file(source_game / filename, filename.encode("utf-8"))

    expected = {
        "win32": (
            {
                "openQ4-client_x64.exe",
                "openQ4-client_x64.pdb",
                "openQ4-ded_x64.exe",
                "openQ4-ded_x64.pdb",
                "renderer-gl_x64.dll",
                "renderer-gl_x64.pdb",
                "renderer-vk_x64.dll",
                "renderer-vk_x64.pdb",
                "OpenAL32.dll",
            },
            {
                "game-sp_x64.dll",
                "game-sp_x64.pdb",
                "game-mp_x64.dll",
                "game-mp_x64.pdb",
                "mod.json",
                "pak0.pk4",
                "pak1.pk4",
            },
        ),
        "linux": (
            {
                "openQ4-client_x64",
                "openQ4-ded_x64",
                "renderer-gl_x64.so",
                "renderer-vk_x64.so",
            },
            {
                "game-sp_x64.so",
                "game-mp_x64.so",
                "mod.json",
                "pak0.pk4",
                "pak1.pk4",
            },
        ),
        "darwin": (
            {
                "openQ4-client_x64",
                "openQ4-ded_x64",
                "renderer-vk_x64.dylib",
            },
            {
                "game-sp_x64.dylib",
                "game-mp_x64.dylib",
                "mod.json",
                "pak0.pk4",
                "pak1.pk4",
            },
        ),
    }
    for platform_name, (expected_root, expected_game) in expected.items():
        policy = FAST_STAGE.runtime_stage_policy(platform_name)
        destination = WORK / "fast-stage-platform-policy" / platform_name
        destination_game = destination / "baseoq4"
        FAST_STAGE.copy_matches(
            source_root,
            destination,
            policy.root_copy_patterns,
            policy.root_copy_exclude_patterns,
        )
        FAST_STAGE.copy_matches(
            source_game, destination_game, policy.game_copy_patterns
        )
        actual_root = {
            path.name for path in destination.iterdir() if path.is_file()
        }
        actual_game = {
            path.name for path in destination_game.iterdir() if path.is_file()
        }
        if actual_root != expected_root:
            raise AssertionError(
                f"{platform_name} fast-stage root policy copied {sorted(actual_root)!r}; "
                f"expected {sorted(expected_root)!r}"
            )
        if actual_game != expected_game:
            raise AssertionError(
                f"{platform_name} fast-stage game policy copied {sorted(actual_game)!r}; "
                f"expected {sorted(expected_game)!r}"
            )

    cleanup_root = WORK / "fast-stage-platform-policy" / "windows-cleanup"
    cleanup_game = cleanup_root / "baseoq4"
    retained_root = cleanup_root / "renderer-gl_x64.dll"
    retained_game = cleanup_game / "game-sp_x64.dll"
    foreign_paths = (
        cleanup_root / "renderer-gl_x64.so",
        cleanup_root / "renderer-vk_x64.dylib",
        cleanup_game / "game-sp_x64.so",
        cleanup_game / "game-mp_x64.dylib",
    )
    for path in (*foreign_paths, retained_root, retained_game):
        write_file(path)
    FAST_STAGE.remove_non_runtime_files(
        cleanup_root,
        cleanup_game,
        FAST_STAGE.runtime_stage_policy("win32"),
    )
    remaining_foreign = [path for path in foreign_paths if path.exists()]
    if remaining_foreign:
        raise AssertionError(
            "Windows fast-stage cleanup retained foreign modules: "
            + ", ".join(str(path) for path in remaining_foreign)
        )
    if not retained_root.is_file() or not retained_game.is_file():
        raise AssertionError("Windows fast-stage cleanup removed native DLL payloads")


def validate_stale_content_prune_symlink_handling() -> None:
    source_root = WORK / "stale-content" / "openQ4"
    staged_game_dir = source_root / ".install" / "baseoq4"
    outside = WORK / "stale-content-outside.cfg"
    write_file(outside, b"outside\n")
    staged_game_dir.mkdir(parents=True, exist_ok=True)

    stale_link = staged_game_dir / "default.cfg"
    if make_symlink(outside, stale_link):
        removed = CHECK_STAGED_CONTENT.prune_stale_loose_content(source_root)
        if Path("default.cfg") not in removed:
            raise AssertionError("stale-content prune did not report the stale symlink")
        if outside.read_bytes() != b"outside\n":
            raise AssertionError("stale-content prune followed a stale symlink target")
        if stale_link.exists() or stale_link.is_symlink():
            raise AssertionError("stale-content prune did not unlink the stale symlink")

    linked_game_root = WORK / "stale-content-linked" / "openQ4"
    linked_target = WORK / "stale-content-linked-target"
    linked_game_dir = linked_game_root / ".install" / "baseoq4"
    linked_game_dir.parent.mkdir(parents=True, exist_ok=True)
    linked_target.mkdir(parents=True, exist_ok=True)
    if make_symlink(linked_target, linked_game_dir):
        expect_runtime_error(
            lambda: CHECK_STAGED_CONTENT.prune_stale_loose_content(linked_game_root),
            "symlinked staged game directory",
            "stale-content symlinked game directory guard",
        )

    source_link = WORK / "stale-content-source-link"
    if make_symlink(source_root, source_link, target_is_directory=True):
        result = run_script(
            BUILD_DIR / "check_staged_content_edits.py",
            "--source-root",
            source_link,
        )
        if result.returncode == 0 or "source root must not be a symlink" not in result.stderr:
            raise AssertionError(f"check_staged_content_edits.py accepted a symlinked source root: {result.stderr}")


def validate_package_name_and_copy_guards() -> None:
    if PACKAGE.validate_package_version("0.1.010-nightly.20260623.1+gabcdef12") != "0.1.010-nightly.20260623.1+gabcdef12":
        raise AssertionError("package version validation rejected a safe prerelease/build metadata version")
    expect_value_error(
        lambda: PACKAGE.validate_package_version("0.1.010\nversion_tag=evil"),
        "semver-style",
        "package version newline injection",
    )
    expect_value_error(
        lambda: PACKAGE.validate_package_path_token("0.1/escape", "version tag"),
        "file-name-safe",
        "package version tag slash",
    )
    expect_value_error(
        lambda: PACKAGE.resolve_package_root(WORK / "out", "../escape"),
        "escapes output directory",
        "package root escape",
    )
    package_output = WORK / "package-root-output"
    package_output.mkdir(parents=True, exist_ok=True)
    write_file(package_output / "openq4-file")
    expect_value_error(
        lambda: PACKAGE.resolve_package_root(package_output, "openq4-file"),
        "not a directory",
        "package root existing file",
    )
    package_link_target = WORK / "package-root-target"
    package_link_target.mkdir(parents=True, exist_ok=True)
    package_link = package_output / "openq4-link"
    if make_symlink(package_link_target, package_link):
        expect_value_error(
            lambda: PACKAGE.resolve_package_root(package_output, "openq4-link"),
            "must not be a symlink",
            "package root symlink",
        )
    directory_target = WORK / "package-directory-target"
    directory_target.mkdir(parents=True, exist_ok=True)
    directory_link = WORK / "package-directory-link"
    if make_symlink(directory_target, directory_link, target_is_directory=True):
        expect_value_error(
            lambda: PACKAGE.require_package_directory(directory_link, "install directory", must_exist=True),
            "must not be a symlink",
            "package root input symlink",
        )

    source = WORK / "package-copy" / "source.txt"
    outside = WORK / "package-copy-outside.txt"
    link = WORK / "package-copy" / "link.txt"
    write_file(source)
    write_file(outside)
    if make_symlink(outside, link):
        expect_runtime_error(
            lambda: PACKAGE.copy_regular_file(link, WORK / "package-copy" / "dest.txt"),
            "refusing to package symlinked file",
            "package symlinked file source",
        )

    share = WORK / "package-share" / "share"
    write_file(share / "applications" / "openq4.desktop")
    if make_symlink(outside, share / "leak.txt"):
        expect_runtime_error(
            lambda: PACKAGE.copy_regular_tree(share, WORK / "package-share" / "dest"),
            "refusing to package symlink from tree",
            "package share symlink source",
        )

    archive_root = WORK / "package-archive-root"
    write_file(archive_root / "file.txt")
    archive_dir = WORK / "package-archive.zip"
    archive_dir.mkdir(parents=True, exist_ok=True)
    expect_runtime_error(
        lambda: PACKAGE.create_release_archive(archive_root, archive_dir, "zip"),
        "path is a directory",
        "release archive output directory",
    )
    archive_link = WORK / "package-archive-link.zip"
    archive_target = WORK / "package-archive-target.zip"
    write_file(archive_target)
    if make_symlink(archive_target, archive_link):
        expect_runtime_error(
            lambda: PACKAGE.create_release_archive(archive_root, archive_link, "zip"),
            "must not be a symlink",
            "release archive output symlink",
        )

    executable = archive_root / "bin" / "openQ4-client_x64"
    write_file(executable, b"#!/bin/sh\n")
    zip_path = WORK / "package-archive-metadata.zip"
    PACKAGE.create_release_archive(
        archive_root,
        zip_path,
        "zip",
        {Path("bin") / "openQ4-client_x64"},
    )
    with ZipFile(zip_path, "r") as archive:
        for info in archive.infolist():
            if info.filename.endswith("/"):
                continue
            if info.date_time != PACKAGE.DETERMINISTIC_ARCHIVE_TIMESTAMP:
                raise AssertionError(f"zip entry kept host mtime: {info.filename} {info.date_time}")
            if info.create_system != 3:
                raise AssertionError(f"zip entry did not use portable Unix mode metadata: {info.filename}")
        executable_info = archive.getinfo(f"{archive_root.name}/bin/openQ4-client_x64")
        if ((executable_info.external_attr >> 16) & 0o777) != 0o755:
            raise AssertionError("zip executable mode was not normalized to 0755")

    tar_path = WORK / "package-archive-metadata.tar.gz"
    PACKAGE.create_release_archive(
        archive_root,
        tar_path,
        "tar.gz",
        {Path("bin") / "openQ4-client_x64"},
    )
    with tarfile.open(tar_path, "r:gz") as archive:
        for member in archive.getmembers():
            if member.uid != 0 or member.gid != 0 or member.uname or member.gname:
                raise AssertionError(f"tar entry kept host ownership metadata: {member.name}")
            if member.mtime != PACKAGE.DETERMINISTIC_TAR_MTIME:
                raise AssertionError(f"tar entry kept host mtime: {member.name}")
        executable_member = archive.getmember(f"{archive_root.name}/bin/openQ4-client_x64")
        if executable_member.mode != 0o755:
            raise AssertionError("tar executable mode was not normalized to 0755")

    manifest_outside = WORK / "package-version-outside.txt"
    manifest_path = WORK / "package-version" / "VERSION.txt"
    write_file(manifest_outside, b"outside\n")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(manifest_outside, manifest_path):
        PACKAGE.write_version_manifest(
            manifest_path,
            version="0.1.010",
            version_tag="0.1.010",
            platform="linux",
            arch="x64",
        )
        if manifest_outside.read_bytes() != b"outside\n":
            raise AssertionError("write_version_manifest followed a destination symlink")
        if manifest_path.is_symlink():
            raise AssertionError("write_version_manifest did not replace the destination symlink")
        if b"\r\n" in manifest_path.read_bytes():
            raise AssertionError("write_version_manifest emitted platform-dependent CRLF newlines")


def validate_renderer_module_staging() -> None:
    install_dir = WORK / "renderer-module-staging" / "install"
    package_root = WORK / "renderer-module-staging" / "package"
    required_files = (
        *PACKAGE.get_required_root_binaries("linux", "x64"),
        *PACKAGE.get_required_renderer_module_binaries("linux", "x64"),
    )
    for filename in required_files:
        write_file(install_dir / filename, filename.encode("utf-8"))

    missing = PACKAGE.copy_required_binaries(
        "linux",
        "x64",
        install_dir,
        package_root,
        allow_missing_binaries=False,
    )
    if missing:
        raise AssertionError(f"Linux renderer-module staging reported missing files: {missing}")
    for filename in required_files:
        if not (package_root / filename).is_file():
            raise AssertionError(f"Release package is missing required renderer runtime module: {filename}")

    # macOS keeps its statically linked OpenGL renderer AND ships the Vulkan
    # renderer module, which runs there on MoltenVK. The GL module is still not
    # built on darwin, so exactly one renderer module is required.
    macos_modules = PACKAGE.get_required_renderer_module_binaries("macos", "arm64")
    if macos_modules != ("renderer-vk_arm64.dylib",):
        raise AssertionError(
            "macOS must require exactly the Vulkan renderer module (MoltenVK-backed, opt-in); "
            f"got {macos_modules!r}"
        )


def validate_build_pack_and_header_cli_guards() -> None:
    pack_source = WORK / "build-pack" / "source"
    write_file(pack_source / "materials" / "ok.mtr")
    source_link = WORK / "build-pack" / "source-link"
    if make_symlink(pack_source, source_link, target_is_directory=True):
        result = run_script(
            BUILD_DIR / "build_openq4_pack.py",
            "--pak-name",
            "pak0.pk4",
            "--source-dir",
            source_link,
            "--out",
            WORK / "build-pack" / "out.pk4",
        )
        if result.returncode == 0 or "source directory must not be a symlink" not in result.stderr:
            raise AssertionError(f"build_openq4_pack.py accepted a symlinked source dir: {result.stderr}")

    manifest_target = WORK / "build-pack" / "pak0.sources"
    write_file(manifest_target)
    manifest_link = WORK / "build-pack" / "pak0-link.sources"
    if make_symlink(manifest_target, manifest_link):
        result = run_script(
            BUILD_DIR / "build_openq4_pack.py",
            "--pak-name",
            "pak0.pk4",
            "--source-dir",
            pack_source,
            "--manifest",
            manifest_link,
            "--out",
            WORK / "build-pack" / "out.pk4",
        )
        if result.returncode == 0 or "source manifest must not be a symlink" not in result.stderr:
            raise AssertionError(f"build_openq4_pack.py accepted a symlinked manifest: {result.stderr}")

    pak_target = WORK / "pak-header" / "pak0.pk4"
    pak_link = WORK / "pak-header" / "pak0-link.pk4"
    write_file(pak_target)
    if make_symlink(pak_target, pak_link):
        result = run_script(
            BUILD_DIR / "generate_pak_header.py",
            "--pak0",
            pak_link,
            "--pak1",
            pak_target,
            "--header-out",
            WORK / "pak-header" / "openq4_paks_generated.h",
        )
        if result.returncode == 0 or "pak0.pk4 must not be a symlink" not in result.stderr:
            raise AssertionError(f"generate_pak_header.py accepted a symlinked PK4 input: {result.stderr}")


def validate_version_manifest_integrity() -> None:
    expect_runtime_error(
        lambda: PACKAGE.parse_version_manifest_bytes(
            b"openQ4\nversion=1\nversion=2\nversion_tag=1\nplatform=linux\narch=x64\n",
            "test",
        ),
        "duplicate key",
        "duplicate version manifest key",
    )
    expect_runtime_error(
        lambda: PACKAGE.parse_version_manifest_bytes(
            b"openQ4\n=1\nversion_tag=1\nplatform=linux\narch=x64\n",
            "test",
        ),
        "empty key",
        "empty version manifest key",
    )


def validate_macos_signing_input_guards() -> None:
    signing_root = WORK / "macos-signing-inputs"
    entitlements_target = signing_root / "entitlements.plist"
    entitlements_link = signing_root / "linked-entitlements.plist"
    write_file(entitlements_target, b"<?xml version=\"1.0\"?><plist version=\"1.0\"><dict/></plist>\n")

    if make_symlink(entitlements_target, entitlements_link):
        args = type(
            "Args",
            (),
            {
                "macos_entitlements": str(entitlements_link),
                "macos_signing_mode": "ad-hoc",
                "macos_notarize": False,
                "macos_code_sign_identity": "",
                "macos_notary_keychain_profile": "",
                "macos_notary_keychain": "",
            },
        )()
        expect_runtime_error(
            lambda: PACKAGE.resolve_macos_signing_config(args),
            "entitlements file must not be a symlink",
            "macOS entitlements symlink guard",
        )

    keychain_target = signing_root / "notary.keychain-db"
    keychain_link = signing_root / "linked-notary.keychain-db"
    write_file(keychain_target, b"keychain\n")
    if make_symlink(keychain_target, keychain_link):
        args = type(
            "Args",
            (),
            {
                "macos_entitlements": "",
                "macos_signing_mode": "developer-id",
                "macos_notarize": True,
                "macos_code_sign_identity": "Developer ID Application: Test",
                "macos_notary_keychain_profile": "openq4-test",
                "macos_notary_keychain": str(keychain_link),
            },
        )()
        expect_runtime_error(
            lambda: PACKAGE.resolve_macos_signing_config(args),
            "notary keychain must not be a symlink",
            "macOS notary keychain symlink guard",
        )

    args = type(
        "Args",
        (),
        {
            "macos_entitlements": "",
            "macos_signing_mode": "developer-id",
            "macos_notarize": True,
            "macos_code_sign_identity": "Developer ID Application: Test",
            "macos_notary_keychain_profile": "openq4-test",
            "macos_notary_keychain": str(signing_root / "missing.keychain-db"),
        },
    )()
    expect_runtime_error(
        lambda: PACKAGE.resolve_macos_signing_config(args),
        "notary keychain does not exist",
        "macOS notary keychain missing-file guard",
    )


def validate_stage_manifest_rejects_unsafe_paths() -> None:
    stage_root = WORK / "stage-manifest"
    write_file(stage_root / "src" / "game" / "Game_local.cpp")
    manifest_path = stage_root / STAGE_GAMELIBS.MANIFEST_NAME
    manifest_path.write_text(
        json.dumps(
            {
                "format": 1,
                "fileCount": 1,
                "files": [{"path": "../escape.cpp", "sha256": "0" * 64}],
            }
        ),
        encoding="utf-8",
    )
    expect_runtime_error(
        lambda: STAGE_GAMELIBS.validate_stage_manifest(stage_root),
        "unsafe path",
        "unsafe staged manifest path",
    )

    manifest_link_root = WORK / "stage-manifest-link"
    manifest_link_root.mkdir(parents=True, exist_ok=True)
    manifest_target = WORK / "external-stage-manifest.json"
    write_file(manifest_target, b'{"format": 1, "fileCount": 0, "files": []}\n')
    if make_symlink(manifest_target, manifest_link_root / STAGE_GAMELIBS.MANIFEST_NAME):
        expect_runtime_error(
            lambda: STAGE_GAMELIBS.validate_stage_manifest(manifest_link_root),
            "manifest must not be a symlink",
            "symlinked staged manifest",
        )

    bad_format_root = WORK / "stage-manifest-format"
    write_file(bad_format_root / "src" / "game" / "Game_local.cpp")
    (bad_format_root / STAGE_GAMELIBS.MANIFEST_NAME).write_text(
        json.dumps(
            {
                "format": 999,
                "fileCount": 1,
                "files": [{"path": "src/game/Game_local.cpp", "sha256": "0" * 64}],
            }
        ),
        encoding="utf-8",
    )
    expect_runtime_error(
        lambda: STAGE_GAMELIBS.validate_stage_manifest(bad_format_root),
        "unsupported format",
        "unsupported staged manifest format",
    )

    bad_hash_root = WORK / "stage-manifest-hash"
    write_file(bad_hash_root / "src" / "game" / "Game_local.cpp")
    (bad_hash_root / STAGE_GAMELIBS.MANIFEST_NAME).write_text(
        json.dumps(
            {
                "format": 1,
                "fileCount": 1,
                "files": [{"path": "src/game/Game_local.cpp", "sha256": "not-a-sha"}],
            }
        ),
        encoding="utf-8",
    )
    expect_runtime_error(
        lambda: STAGE_GAMELIBS.validate_stage_manifest(bad_hash_root),
        "malformed sha256",
        "malformed staged manifest hash",
    )


def validate_stage_gamelibs_raw_stage_root_symlink_guard() -> None:
    root = WORK / "gamelibs-stage-root-symlink"
    project_root = root / "openQ4"
    gamelibs_root = root / "openQ4-game"
    stage_target = project_root / ".tmp" / "real-stage"
    stage_link = project_root / ".tmp" / "linked-stage"

    write_file(project_root / "meson.build", b"project('openQ4')\n")
    write_file(gamelibs_root / "src" / "game" / "Game_local.cpp", b"// game\n")
    stage_target.mkdir(parents=True, exist_ok=True)
    stage_link.parent.mkdir(parents=True, exist_ok=True)
    if not make_symlink(stage_target, stage_link):
        return

    result = run_script(BUILD_DIR / "stage_gamelibs.py", project_root, gamelibs_root, stage_link)
    if result.returncode == 0 or "refusing to stage into symlink" not in result.stderr:
        raise AssertionError(f"stage_gamelibs.py accepted a symlinked stage root: {result.stderr}")


def validate_stage_gamelibs_raw_source_root_symlink_guards() -> None:
    root = WORK / "gamelibs-source-root-symlink"
    project_root = root / "openQ4"
    gamelibs_root = root / "openQ4-game"
    stage_root = project_root / ".tmp" / "stage"
    write_file(project_root / "meson.build", b"project('openQ4')\n")
    write_file(gamelibs_root / "src" / "game" / "Game_local.cpp", b"// game\n")

    project_link = root / "openQ4-link"
    if make_symlink(project_root, project_link, target_is_directory=True):
        result = run_script(BUILD_DIR / "stage_gamelibs.py", project_link, gamelibs_root, stage_root)
        if result.returncode == 0 or "openQ4 root must not be a symlink" not in result.stderr:
            raise AssertionError(f"stage_gamelibs.py accepted a symlinked project root: {result.stderr}")

    gamelibs_link = root / "openQ4-game-link"
    if make_symlink(gamelibs_root, gamelibs_link, target_is_directory=True):
        result = run_script(BUILD_DIR / "stage_gamelibs.py", project_root, gamelibs_link, stage_root)
        if result.returncode == 0 or "GameLibs root must not be a symlink" not in result.stderr:
            raise AssertionError(f"stage_gamelibs.py accepted a symlinked GameLibs root: {result.stderr}")


def validate_meson_source_symlink_guards() -> None:
    source_root = WORK / "meson-source-symlinks" / "src"
    outside = WORK / "meson-source-symlinks" / "outside.cpp"
    write_file(outside, b"// outside\n")

    required_link = source_root / "sys" / "linux" / "main.cpp"
    required_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, required_link):
        expect_runtime_error(
            lambda: MESON_SOURCES.add_required_source(set(), [], source_root, "sys/linux/main.cpp"),
            "symlinked source file",
            "required Meson source symlink guard",
        )

    glob_link = source_root / "renderer" / "linked.cpp"
    glob_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, glob_link):
        expect_runtime_error(
            lambda: MESON_SOURCES.add_globbed_sources(set(), [], source_root, "renderer/*.cpp"),
            "symlinked source file",
            "globbed Meson source symlink guard",
        )


def validate_windows_runtime_staging_guards() -> None:
    expected_stale_stage_files = {
        f"openQ4-{kind}_{arch}"
        for kind in ("client", "ded")
        for arch in ("x86", "x64", "arm64")
    } | {
        f"renderer-{renderer}_{arch}.dll.mainbak"
        for renderer in ("gl", "vk")
        for arch in ("x86", "x64", "arm64")
    }
    if set(WINDOWS_RUNTIME.WINDOWS_STALE_STAGE_FILE_MANIFEST) != expected_stale_stage_files:
        raise AssertionError("Windows stage cleanup must remain an exact stale-file allowlist")
    if WINDOWS_RUNTIME.WINDOWS_EMPTY_STAGE_DIRECTORY_MANIFEST != ("baseoq4/skins",):
        raise AssertionError("Windows stage cleanup must only prune the known empty skins directory")

    hygiene_root = WORK / "windows-runtime" / "stage-hygiene"
    stale_client = hygiene_root / "openQ4-client_x64"
    stale_dedicated = hygiene_root / "openQ4-ded_x64"
    stale_backup = hygiene_root / "renderer-vk_x64.dll.mainbak"
    current_client = hygiene_root / "openQ4-client_x64.exe"
    current_renderer = hygiene_root / "renderer-vk_x64.dll"
    unlisted_extensionless = hygiene_root / "openQ4-client_custom"
    empty_skins = hygiene_root / "baseoq4" / "skins"
    for path in (stale_client, stale_dedicated, stale_backup, current_client, current_renderer, unlisted_extensionless):
        write_file(path)
    empty_skins.mkdir(parents=True)

    hygiene_result = WINDOWS_RUNTIME.cleanup_windows_stage_target(hygiene_root)
    if set(hygiene_result["removed_stale_files"]) != {
        "openQ4-client_x64",
        "openQ4-ded_x64",
        "renderer-vk_x64.dll.mainbak",
    }:
        raise AssertionError(f"unexpected Windows stale-stage cleanup result: {hygiene_result!r}")
    if hygiene_result["removed_empty_directories"] != ["baseoq4/skins"]:
        raise AssertionError(f"empty Windows stage directory was not pruned: {hygiene_result!r}")
    for removed_path in (stale_client, stale_dedicated, stale_backup, empty_skins):
        if removed_path.exists() or removed_path.is_symlink():
            raise AssertionError(f"known stale Windows stage entry was retained: {removed_path}")
    for preserved_path in (current_client, current_renderer, unlisted_extensionless):
        if not preserved_path.is_file():
            raise AssertionError(f"Windows stage cleanup removed an unlisted runtime entry: {preserved_path}")

    populated_root = WORK / "windows-runtime" / "stage-hygiene-populated"
    populated_skin = populated_root / "baseoq4" / "skins" / "custom.skin"
    write_file(populated_skin)
    populated_result = WINDOWS_RUNTIME.cleanup_windows_stage_target(populated_root)
    if populated_result["removed_empty_directories"] or not populated_skin.is_file():
        raise AssertionError("Windows stage cleanup must preserve a populated baseoq4/skins directory")

    stale_directory_root = WORK / "windows-runtime" / "stage-hygiene-stale-directory"
    (stale_directory_root / "openQ4-client_x64").mkdir(parents=True)
    expect_runtime_error(
        lambda: WINDOWS_RUNTIME.cleanup_windows_stage_target(stale_directory_root),
        "not a regular file",
        "Windows stage stale-file directory guard",
    )

    empty_path_file_root = WORK / "windows-runtime" / "stage-hygiene-empty-path-file"
    write_file(empty_path_file_root / "baseoq4" / "skins")
    expect_runtime_error(
        lambda: WINDOWS_RUNTIME.cleanup_windows_stage_target(empty_path_file_root),
        "is not a directory",
        "Windows stage empty-directory path guard",
    )

    stale_link_root = WORK / "windows-runtime" / "stage-hygiene-stale-link"
    stale_link_root.mkdir(parents=True)
    stale_link_target = WORK / "windows-runtime" / "stage-hygiene-link-target"
    write_file(stale_link_target, b"outside\n")
    stale_link = stale_link_root / "renderer-vk_x64.dll.mainbak"
    if make_symlink(stale_link_target, stale_link):
        link_result = WINDOWS_RUNTIME.cleanup_windows_stage_target(stale_link_root)
        if link_result["removed_stale_files"] != ["renderer-vk_x64.dll.mainbak"]:
            raise AssertionError("Windows stage cleanup did not report the known stale symlink")
        if stale_link.exists() or stale_link.is_symlink():
            raise AssertionError("Windows stage cleanup did not unlink the known stale symlink")
        if stale_link_target.read_bytes() != b"outside\n":
            raise AssertionError("Windows stage cleanup followed a known stale symlink target")

    linked_game_root = WORK / "windows-runtime" / "stage-hygiene-linked-game-root"
    linked_game_target = WORK / "windows-runtime" / "stage-hygiene-linked-game-target"
    linked_game_root.mkdir(parents=True)
    (linked_game_target / "skins").mkdir(parents=True)
    if make_symlink(linked_game_target, linked_game_root / "baseoq4", target_is_directory=True):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.cleanup_windows_stage_target(linked_game_root),
            "cleanup parent is a link or junction",
            "Windows stage cleanup linked game-root guard",
        )
        if not (linked_game_target / "skins").is_dir():
            raise AssertionError("Windows stage cleanup followed a linked baseoq4 directory")

    malformed_pe = WORK / "windows-runtime" / "truncated.exe"
    malformed_pe.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(0x48)
    data[0:2] = b"MZ"
    data[0x3C:0x40] = (0x40).to_bytes(4, "little")
    data[0x40:0x44] = b"PE\0\0"
    malformed_pe.write_bytes(data)
    if WINDOWS_RUNTIME.scan_runtime_imports(malformed_pe):
        raise AssertionError("malformed PE should not report runtime imports")

    mixed_root = WORK / "windows-runtime" / "mixed"
    write_file(mixed_root / "openQ4-client_x64.exe", b"MZ\n")
    write_file(mixed_root / "baseoq4" / "game-sp_arm64.dll", b"MZ\n")
    expect_runtime_error(
        lambda: WINDOWS_RUNTIME.detect_binary_arch(mixed_root),
        "Mixed openQ4 binary architectures",
        "mixed Windows runtime architecture detection",
    )

    missing_openal_source = WORK / "windows-runtime" / "missing-openal-source"
    missing_openal_build = WORK / "windows-runtime" / "missing-openal-build"
    write_file(missing_openal_build / "openQ4-client_x64.exe", b"MZ\n")
    original_openal_root = os.environ.pop("OPENQ4_OPENAL_ROOT", None)
    try:
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.stage_runtime_payloads(
                missing_openal_source,
                missing_openal_build,
                [WORK / "windows-runtime" / "missing-openal-target"],
            ),
            "OpenAL32.dll runtime not found",
            "missing OpenAL runtime",
        )
    finally:
        if original_openal_root is not None:
            os.environ["OPENQ4_OPENAL_ROOT"] = original_openal_root

    symlink_binary_root = WORK / "windows-runtime" / "symlink-binary"
    real_binary = symlink_binary_root / "real.exe"
    link_binary = symlink_binary_root / "openQ4-client_x64.exe"
    write_file(real_binary, b"MZ\n")
    if make_symlink(real_binary, link_binary):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.collect_runtime_binaries(symlink_binary_root),
            "symlinked runtime binary",
            "symlinked Windows runtime binary",
        )

    runtime_source = WORK / "windows-runtime" / "runtime-source"
    runtime_source.mkdir(parents=True, exist_ok=True)
    runtime_source_link = WORK / "windows-runtime" / "runtime-source-link"
    if make_symlink(runtime_source, runtime_source_link, target_is_directory=True):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.stage_runtime_payloads(
                runtime_source_link,
                mixed_root,
                [WORK / "windows-runtime" / "source-link-target"],
            ),
            "source root must not be a symlink",
            "Windows runtime source-root symlink guard",
        )

    runtime_build_link = WORK / "windows-runtime" / "runtime-build-link"
    if make_symlink(mixed_root, runtime_build_link, target_is_directory=True):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.stage_runtime_payloads(
                runtime_source,
                runtime_build_link,
                [WORK / "windows-runtime" / "build-link-target"],
            ),
            "build root must not be a symlink",
            "Windows runtime build-root symlink guard",
        )

    runtime_target = WORK / "windows-runtime" / "runtime-target"
    runtime_target_link = WORK / "windows-runtime" / "runtime-target-link"
    runtime_target.mkdir(parents=True, exist_ok=True)
    if make_symlink(runtime_target, runtime_target_link, target_is_directory=True):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.stage_runtime_payloads(runtime_source, mixed_root, [runtime_target_link]),
            "runtime target must not be a symlink",
            "Windows runtime target symlink guard",
        )

    symlink_tree_source = WORK / "windows-runtime" / "symlink-tree-source"
    symlink_tree_build = WORK / "windows-runtime" / "symlink-tree-build"
    generated_game = symlink_tree_build / "content" / "baseoq4"
    write_file(generated_game / "mod.json", b"{}\n")
    link_payload = generated_game / "leak.txt"
    if make_symlink(real_binary, link_payload):
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.stage_build_game_directory(symlink_tree_source, symlink_tree_build),
            "symlink from generated runtime tree",
            "symlinked generated runtime tree payload",
        )

    original_openal_root = os.environ.get("OPENQ4_OPENAL_ROOT")
    try:
        override_root = WORK / "windows-runtime" / "openal-override"
        override_root_link = WORK / "windows-runtime" / "openal-override-link"
        write_file(override_root / "bin" / "OpenAL32.dll", b"dll\n")
        if make_symlink(override_root, override_root_link, target_is_directory=True):
            os.environ["OPENQ4_OPENAL_ROOT"] = str(override_root_link)
            expect_runtime_error(
                lambda: WINDOWS_RUNTIME.resolve_openal_runtime_path(runtime_source, "x64"),
                "OpenAL override root must not be a symlink",
                "OpenAL override root symlink guard",
            )

        override_runtime_link_root = WORK / "windows-runtime" / "openal-runtime-link-root"
        outside_runtime = WORK / "windows-runtime" / "external-OpenAL32.dll"
        write_file(outside_runtime, b"dll\n")
        (override_runtime_link_root / "bin").mkdir(parents=True, exist_ok=True)
        if make_symlink(outside_runtime, override_runtime_link_root / "bin" / "OpenAL32.dll"):
            os.environ["OPENQ4_OPENAL_ROOT"] = str(override_runtime_link_root)
            expect_runtime_error(
                lambda: WINDOWS_RUNTIME.resolve_openal_runtime_path(runtime_source, "x64"),
                "OpenAL override runtime must not be a symlink",
                "OpenAL override runtime symlink guard",
            )

        missing_override = WORK / "windows-runtime" / "openal-missing"
        missing_override.mkdir(parents=True, exist_ok=True)
        os.environ["OPENQ4_OPENAL_ROOT"] = str(missing_override)
        expect_runtime_error(
            lambda: WINDOWS_RUNTIME.resolve_openal_runtime_path(runtime_source, "x64"),
            "OpenAL override runtime not found",
            "OpenAL override missing runtime guard",
        )
    finally:
        if original_openal_root is None:
            os.environ.pop("OPENQ4_OPENAL_ROOT", None)
        else:
            os.environ["OPENQ4_OPENAL_ROOT"] = original_openal_root


def validate_prepare_windows_openal_path_boundary_guard() -> None:
    script = (BUILD_DIR / "prepare_windows_openal.ps1").read_text(encoding="utf-8")
    for token in (
        "TrimEndingDirectorySeparator",
        "$rootPrefix",
        "StartsWith($rootPrefix",
    ):
        if token not in script:
            raise AssertionError(f"prepare_windows_openal.ps1 is missing strict path boundary token {token!r}")


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "packaging_safety.py" not in text:
            raise AssertionError(f"packaging_safety.py is not wired into {context}")


def validate_target_repository_metadata() -> None:
    project_root = WORK / "target-repository-metadata"
    targets = (
        ("windows", "x64", "gamelibs_stage", "1" * 40),
        ("linux", "x64", "gamelibs_stage-linux-x64", "2" * 40),
        ("linux", "arm64", "gamelibs_stage-linux-arm64", "3" * 40),
        ("macos", "arm64", "gamelibs_stage-darwin-arm64", "4" * 40),
    )
    for platform, arch, directory, revision in targets:
        manifest = project_root / ".tmp" / directory / "openq4_gamelibs_stage_manifest.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"gameLibsGitCommit": revision, "gameLibsGitDirty": False}), encoding="utf-8")
    for platform, arch, directory, revision in targets:
        metadata = PACKAGE.read_staged_repository_metadata(project_root, platform, arch)
        if metadata.get("openq4_game_commit") != revision:
            raise AssertionError(f"{platform}/{arch} package read another target's source provenance: {metadata}")
    if PACKAGE.read_staged_repository_metadata(project_root, "macos", "universal2"):
        raise AssertionError("Universal2 must not read an unrelated thin-build stage")


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_target_repository_metadata()
        validate_pk4_source_containment()
        validate_pk4_archive_member_guards()
        validate_copy_helpers_do_not_follow_destination_symlinks()
        validate_pk4_replace_helper_does_not_preserve_destination_symlink()
        validate_generated_text_writers_do_not_follow_symlinks()
        validate_legacy_build_pak0_copy_does_not_follow_symlink()
        validate_legacy_build_pak0_cli_guards()
        validate_list_sources_guards()
        validate_fast_stage_guards_and_copy()
        validate_fast_stage_cross_platform_policy()
        validate_stale_content_prune_symlink_handling()
        validate_package_name_and_copy_guards()
        validate_renderer_module_staging()
        validate_build_pack_and_header_cli_guards()
        validate_version_manifest_integrity()
        validate_macos_signing_input_guards()
        validate_stage_manifest_rejects_unsafe_paths()
        validate_stage_gamelibs_raw_stage_root_symlink_guard()
        validate_stage_gamelibs_raw_source_root_symlink_guards()
        validate_meson_source_symlink_guards()
        validate_windows_runtime_staging_guards()
        validate_prepare_windows_openal_path_boundary_guard()
        validate_validation_wiring()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("packaging_safety: ok")


if __name__ == "__main__":
    main()
