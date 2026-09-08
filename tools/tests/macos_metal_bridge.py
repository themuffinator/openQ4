#!/usr/bin/env python3
"""Regression checks for the macOS Metal bridge build contract."""

import importlib.util
import contextlib
import io
import json
import os
import plistlib
import re
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path
from types import SimpleNamespace
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def expect_runtime_error(fragment: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if fragment not in str(exc):
            raise AssertionError(f"Unexpected error for {context}: {exc}") from exc
        return
    raise AssertionError(f"Expected RuntimeError containing {fragment!r} for {context}")


def shell_heredoc_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing shell heredoc marker {marker!r} in {context}")
    heredoc_start = source.find("<<'PY'\n", start)
    if heredoc_start == -1:
        raise AssertionError(f"Missing Python heredoc start after {marker!r} in {context}")
    heredoc_start += len("<<'PY'\n")
    heredoc_end = source.find("\nPY\n}", heredoc_start)
    if heredoc_end == -1:
        raise AssertionError(f"Missing Python heredoc end after {marker!r} in {context}")
    return source[heredoc_start:heredoc_end]


def run_embedded_python(body: str, argv: list[str], context: str) -> tuple[int, str]:
    old_argv = sys.argv[:]
    stderr = io.StringIO()
    try:
        sys.argv = ["embedded-python"] + argv
        with contextlib.redirect_stderr(stderr):
            try:
                exec(compile(body, f"<{context}>", "exec"), {"__name__": "__embedded__"})
            except SystemExit as exc:
                code = exc.code if isinstance(exc.code, int) else 1
                return code, stderr.getvalue()
        return 0, stderr.getvalue()
    finally:
        sys.argv = old_argv


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def python_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")
    end = source.find("\ndef ", start + 1)
    if end == -1:
        end = len(source)
    return source[start:end]


def load_package_module():
    package_path = ROOT / "tools" / "build" / "package_nightly.py"
    spec = importlib.util.spec_from_file_location("package_nightly_for_macos_test", package_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import package helper: {package_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_validation_module():
    validation_path = ROOT / "tools" / "validation" / "openq4_validate.py"
    spec = importlib.util.spec_from_file_location("openq4_validate_for_macos_test", validation_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import validation helper: {validation_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_macos_plist_bytes(package, version: str) -> bytes:
    plist = dict(package.MACOS_EXPECTED_PLIST_VALUES)
    plist.update(
        {
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "NSHighResolutionCapable": True,
            "NSSupportsAutomaticGraphicsSwitching": True,
        }
    )
    return plistlib.dumps(plist)


def make_version_manifest_bytes(version: str, version_tag: str, platform: str, arch: str) -> bytes:
    return (
        "openQ4\n"
        f"version={version}\n"
        f"version_tag={version_tag}\n"
        f"platform={platform}\n"
        f"arch={arch}\n"
    ).encode("utf-8")


def make_macos_localized_info_bytes(version: str) -> bytes:
    return (
        "/* Localized versions of Info.plist keys */\n\n"
        'CFBundleName = "openQ4";\n'
        f'CFBundleShortVersionString = "{version}";\n'
        f'CFBundleGetInfoString = "openQ4 version {version}, Copyright 2026 DarkMatter Productions";\n'
        'NSHumanReadableCopyright = "Copyright 2026 DarkMatter Productions";\n'
    ).encode("utf-8")


def make_macos_package_root_error_bytes(package, locale: str = "English") -> bytes:
    lines = [
        "/* Localized startup error for incomplete adjacent package roots */",
        "",
    ]
    for key, value in package.MACOS_PACKAGE_ROOT_ERROR_STRINGS[locale].items():
        lines.append(f'{key} = "{value}";')
    return ("\n".join(lines) + "\n").encode("utf-8")


def make_macos_support_info_script_bytes(package) -> bytes:
    lines = [
        "#!/bin/sh",
        "OPENQ4_PACKAGE_ROOT=${OPENQ4_PACKAGE_ROOT:-.}",
        "HOME_DIR=${HOME:-}",
        "ARCHIVE_TMP=${ARCHIVE_TMP:-openq4-macos-support.tmp.tar.gz}",
        "MAX_SUPPORT_TEXT_BYTES=2097152",
        "MAX_CRASH_REPORT_BYTES=8388608",
        "MAX_SUPPORT_ARCHIVE_BYTES=134217728",
        "sanitize_text() { cat; }",
        "limit_stream_tail() { cat; }",
        'echo "Support report output is limited to the final"',
        "write_bounded_report() {",
        "write_openq4_log_candidate_paths() {",
        "write_openq4_renderer_config_candidate_paths() {",
        "redact_text() { cat; }",
        "contains_control_chars() { return 1; }",
        'echo "Support package root must not contain control characters"',
        'echo "Support output directory must not contain control characters"',
        'echo "HOME was not set; home-scoped openq4.log files were skipped."',
        'echo "HOME was not set; saved openQ4Config.cfg paths were skipped."',
        'echo "logs/renderer-config.txt"',
        'echo "Game module search failed:"',
        'echo "Game module load failed:"',
        'echo "dlopen .* failed:"',
        'echo "Only renderer and performance settings are copied"',
        'echo "HOME was not set; the macOS DiagnosticReports directory could not be located."',
        'echo ".XXXXXX.tar.gz.tmp"',
        'echo "does not dump the environment"',
        'echo "does not launch openQ4"',
        'echo "does not copy retail q4base PK4 assets"',
        'echo "truncated copy failed; source was not copied"',
        'COPYFILE_DISABLE=1 tar -czf "${ARCHIVE_TMP}" .',
        'COPYFILE_DISABLE=1 tar -tzf "${ARCHIVE_TMP}"',
        'echo "Support archive is empty or unreadable before publish"',
        'echo "Support archive validation failed before publish"',
        'echo "Support archive is too large before publish"',
        'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"',
    ]
    script = "\n".join(lines) + "\n"
    for token in package.MACOS_SUPPORT_INFO_REQUIRED_TOKENS:
        require(script, token, "synthetic macOS support collector")
    return script.encode("utf-8")


def make_validation_support_info_script_bytes(validator) -> bytes:
    lines = [
        "#!/bin/sh",
        "OPENQ4_PACKAGE_ROOT=${OPENQ4_PACKAGE_ROOT:-.}",
        "HOME_DIR=${HOME:-}",
        "ARCHIVE_TMP=${ARCHIVE_TMP:-openq4-macos-support.tmp.tar.gz}",
        "MAX_SUPPORT_TEXT_BYTES=2097152",
        "MAX_CRASH_REPORT_BYTES=8388608",
        "MAX_SUPPORT_ARCHIVE_BYTES=134217728",
        "sanitize_text() { cat; }",
        "limit_stream_tail() { cat; }",
        'echo "Support report output is limited to the final"',
        "write_bounded_report() {",
        "write_openq4_log_candidate_paths() {",
        "write_openq4_renderer_config_candidate_paths() {",
        "redact_text() { cat; }",
        "contains_control_chars() { return 1; }",
        'echo "Support package root must not contain control characters"',
        'echo "Support output directory must not contain control characters"',
        'echo "HOME was not set; home-scoped openq4.log files were skipped."',
        'echo "HOME was not set; saved openQ4Config.cfg paths were skipped."',
        'echo "logs/renderer-config.txt"',
        'echo "Game module search failed:"',
        'echo "Game module load failed:"',
        'echo "dlopen .* failed:"',
        'echo "Only renderer and performance settings are copied"',
        'echo "HOME was not set; the macOS DiagnosticReports directory could not be located."',
        'echo ".XXXXXX.tar.gz.tmp"',
        'echo "does not dump the environment"',
        'echo "does not launch openQ4"',
        'echo "does not copy retail q4base PK4 assets"',
        'echo "truncated copy failed; source was not copied"',
        'COPYFILE_DISABLE=1 tar -czf "${ARCHIVE_TMP}" .',
        'COPYFILE_DISABLE=1 tar -tzf "${ARCHIVE_TMP}"',
        'echo "Support archive is empty or unreadable before publish"',
        'echo "Support archive validation failed before publish"',
        'echo "Support archive is too large before publish"',
        'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"',
    ]
    script = "\n".join(lines) + "\n"
    for token in validator.MACOS_SUPPORT_INFO_REQUIRED_TOKENS:
        require(script, token, "synthetic staged macOS support collector")
    return script.encode("utf-8")


def make_macos_code_resources_bytes() -> bytes:
    return plistlib.dumps({"files": {}, "files2": {}, "rules": {}, "rules2": {}})


def make_macos_symbol_manifest_bytes(package_name: str, arch: str, version: str, version_tag: str) -> bytes:
    return (
        "openQ4 macOS symbols\n"
        "format=1\n"
        f"version={version}\n"
        f"version_tag={version_tag}\n"
        "platform=macos\n"
        f"arch={arch}\n"
        "package_suffix=-opengl\n"
        f"runtime_archive={package_name}.tar.gz\n"
        f"symbol_archive={package_name}-symbols.tar.xz\n"
        "\n"
        "binaries:\n"
        "- path=openQ4.app/Contents/MacOS/openQ4\n"
        f"  sha256={'0' * 64}\n"
        "  size=1\n"
        "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000001 (arm64) openQ4\n"
        "  dsym=dSYMs/openQ4.app.dSYM\n"
        f"- path=openQ4-client_{arch}\n"
        f"  sha256={'1' * 64}\n"
        "  size=1\n"
        f"  macho_uuid=UUID: 00000000-0000-0000-0000-000000000002 (arm64) openQ4-client_{arch}\n"
        f"  dsym=dSYMs/openQ4-client_{arch}.dSYM\n"
        f"- path=openQ4-ded_{arch}\n"
        f"  sha256={'2' * 64}\n"
        "  size=1\n"
        f"  macho_uuid=UUID: 00000000-0000-0000-0000-000000000003 (arm64) openQ4-ded_{arch}\n"
        f"  dsym=dSYMs/openQ4-ded_{arch}.dSYM\n"
        f"- path=openQ4.app/Contents/Frameworks/game-sp_{arch}.dylib\n"
        f"  sha256={'3' * 64}\n"
        "  size=1\n"
        f"  macho_uuid=UUID: 00000000-0000-0000-0000-000000000004 (arm64) game-sp_{arch}.dylib\n"
        f"  dsym=dSYMs/game-sp_{arch}.dylib.dSYM\n"
        f"- path=openQ4.app/Contents/Frameworks/game-mp_{arch}.dylib\n"
        f"  sha256={'4' * 64}\n"
        "  size=1\n"
        f"  macho_uuid=UUID: 00000000-0000-0000-0000-000000000005 (arm64) game-mp_{arch}.dylib\n"
        f"  dsym=dSYMs/game-mp_{arch}.dylib.dSYM\n"
        # The Vulkan renderer module is openQ4-built, so it carries a dSYM and
        # belongs in the manifest exactly like the game modules. libMoltenVK.dylib
        # ships beside it in Contents/Frameworks but is deliberately absent here:
        # it is a third-party prebuilt with no DWARF, and macos_symbol_targets()
        # excludes it for the same reason.
        f"- path=openQ4.app/Contents/Frameworks/renderer-vk_{arch}.dylib\n"
        f"  sha256={'5' * 64}\n"
        "  size=1\n"
        f"  macho_uuid=UUID: 00000000-0000-0000-0000-000000000006 (arm64) renderer-vk_{arch}.dylib\n"
        f"  dsym=dSYMs/renderer-vk_{arch}.dylib.dSYM\n"
    ).encode("utf-8")


def make_macos_archive_entries(
    package,
    package_name: str,
    arch: str,
    plist_bytes: bytes,
    *,
    client_mode: int = 0o755,
    dedicated_mode: int = 0o755,
    app_exec_mode: int = 0o755,
    extra_entries: dict[str, tuple[bytes, int]] | None = None,
) -> dict[str, tuple[bytes, int]]:
    prefix = package_name + "/"
    client_bytes = b"client-binary\n"
    version = "0.2.000"
    version_tag = "v0.2.000"
    localized_info = make_macos_localized_info_bytes(version)
    entries = {
        f"{prefix}VERSION.txt": (make_version_manifest_bytes(version, version_tag, "macos", arch), 0o644),
        f"{prefix}{package.MACOS_SYMBOL_MANIFEST_NAME}": (
            make_macos_symbol_manifest_bytes(package_name, arch, version, version_tag),
            0o644,
        ),
        f"{prefix}{package.MACOS_SUPPORT_INFO_SCRIPT_NAME}": (make_macos_support_info_script_bytes(package), 0o755),
        f"{prefix}openQ4-client_{arch}": (client_bytes, client_mode),
        f"{prefix}openQ4-ded_{arch}": (b"dedicated-binary\n", dedicated_mode),
        f"{prefix}openQ4.app/Contents/Frameworks/game-sp_{arch}.dylib": (b"sp-module\n", 0o755),
        f"{prefix}openQ4.app/Contents/Frameworks/game-mp_{arch}.dylib": (b"mp-module\n", 0o755),
        # The Vulkan renderer module and the MoltenVK runtime it loads are both
        # signed executable code inside Contents/Frameworks, so both carry the
        # same 0o755 archive mode the game modules do.
        f"{prefix}openQ4.app/Contents/Frameworks/renderer-vk_{arch}.dylib": (b"renderer-vk-module\n", 0o755),
        f"{prefix}openQ4.app/Contents/Frameworks/{package.MACOS_MOLTENVK_DYLIB_NAME}": (
            b"moltenvk-runtime\n",
            0o755,
        ),
        f"{prefix}openQ4.app/Contents/Frameworks/{package.MACOS_OPENAL_SOFT_DYLIB_NAME}": (
            b"openal-soft-runtime\n",
            0o755,
        ),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/COPYING": (b"lgpl\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/LICENSE-pffft": (b"pffft-license\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/LICENSE-fmt": (b"fmt-license\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/LICENSE-gsl": (b"gsl-license\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/SOURCE.md": (b"source\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/licenses/openal-soft/openal-soft-1.25.1.tar.gz": (
            b"source-archive\n",
            0o644,
        ),
        f"{prefix}openQ4.app/Contents/Resources/{package.GAME_DIR_NAME}/mod.json": (b'{"version":"0.2.000"}\n', 0o644),
        f"{prefix}openQ4.app/Contents/Resources/{package.GAME_DIR_NAME}/pak0.pk4": (b"pk4\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/{package.GAME_DIR_NAME}/pak1.pk4": (b"pk4\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/assets/splash/quake4_rt_bitmap_4001.bmp": (b"bmp\n", 0o644),
        f"{prefix}openQ4.app/Contents/Info.plist": (plist_bytes, 0o644),
        f"{prefix}openQ4.app/Contents/PkgInfo": (package.MACOS_PKGINFO_BYTES, 0o644),
        f"{prefix}openQ4.app/Contents/MacOS/openQ4": (client_bytes, app_exec_mode),
        f"{prefix}openQ4.app/Contents/Resources/openQ4.icns": (b"icns\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/VERSION.txt": (
            make_version_manifest_bytes(version, version_tag, "macos", arch),
            0o644,
        ),
        f"{prefix}openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (localized_info, 0o644),
        f"{prefix}openQ4.app/Contents/Resources/French.lproj/InfoPlist.strings": (localized_info, 0o644),
        f"{prefix}openQ4.app/Contents/Resources/English.lproj/{package.MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}": (
            make_macos_package_root_error_bytes(package, "English"),
            0o644,
        ),
        f"{prefix}openQ4.app/Contents/Resources/French.lproj/{package.MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}": (
            make_macos_package_root_error_bytes(package, "French"),
            0o644,
        ),
    }
    if extra_entries:
        entries.update(extra_entries)
    return entries


def make_macos_symbol_archive_entries(
    package,
    package_name: str,
    symbol_root_name: str,
    arch: str,
    version: str,
    version_tag: str,
) -> dict[str, tuple[bytes, int]]:
    prefix = symbol_root_name + "/"
    return {
        f"{prefix}{package.MACOS_SYMBOL_MANIFEST_NAME}": (
            make_macos_symbol_manifest_bytes(package_name, arch, version, version_tag),
            0o644,
        ),
        f"{prefix}dSYMs/openQ4.app.dSYM/Contents/Resources/DWARF/openQ4": (b"app-dsym\n", 0o644),
        f"{prefix}dSYMs/{package.PRODUCT_NAME}-client_{arch}.dSYM/Contents/Resources/DWARF/{package.PRODUCT_NAME}-client_{arch}": (
            b"client-dsym\n",
            0o644,
        ),
        f"{prefix}dSYMs/{package.PRODUCT_NAME}-ded_{arch}.dSYM/Contents/Resources/DWARF/{package.PRODUCT_NAME}-ded_{arch}": (
            b"ded-dsym\n",
            0o644,
        ),
        f"{prefix}dSYMs/game-sp_{arch}.dylib.dSYM/Contents/Resources/DWARF/game-sp_{arch}.dylib": (
            b"sp-dsym\n",
            0o644,
        ),
        f"{prefix}dSYMs/game-mp_{arch}.dylib.dSYM/Contents/Resources/DWARF/game-mp_{arch}.dylib": (
            b"mp-dsym\n",
            0o644,
        ),
        f"{prefix}dSYMs/renderer-vk_{arch}.dylib.dSYM/Contents/Resources/DWARF/renderer-vk_{arch}.dylib": (
            b"renderer-vk-dsym\n",
            0o644,
        ),
        # No libMoltenVK.dylib.dSYM entry: MoltenVK is a third-party prebuilt with
        # no DWARF to harvest, so package_nightly.py keeps it out of the symbol
        # archive on purpose.
    }


def entries_with_runtime_archive_name(
    entries: dict[str, tuple[bytes, int]],
    archive_name: str,
) -> dict[str, tuple[bytes, int]]:
    updated: dict[str, tuple[bytes, int]] = {}
    for name, (data, mode) in entries.items():
        if name.endswith("/SYMBOLS.txt"):
            data = re.sub(
                rb"^runtime_archive=.*$",
                f"runtime_archive={archive_name}".encode("utf-8"),
                data,
                flags=re.MULTILINE,
            )
        updated[name] = (data, mode)
    return updated


def write_test_targz_archive(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    *,
    rewrite_runtime_archive: bool = True,
) -> None:
    if rewrite_runtime_archive:
        entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))


def write_test_tarxz_archive(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    *,
    rewrite_runtime_archive: bool = True,
) -> None:
    if rewrite_runtime_archive:
        entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with tarfile.open(archive_path, "w:xz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))


def write_test_targz_archive_with_symlink(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    link_name: str,
) -> None:
    entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))
        link = tarfile.TarInfo(link_name)
        link.type = tarfile.SYMTYPE
        link.linkname = "../escaped"
        link.mode = 0o777
        link.mtime = 0
        archive.addfile(link)


def write_test_targz_archive_with_fifo(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    fifo_name: str,
) -> None:
    entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))
        fifo = tarfile.TarInfo(fifo_name)
        fifo.type = tarfile.FIFOTYPE
        fifo.mode = 0o644
        fifo.mtime = 0
        archive.addfile(fifo)


def write_test_zip_archive(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    *,
    rewrite_runtime_archive: bool = True,
) -> None:
    if rewrite_runtime_archive:
        entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with ZipFile(archive_path, "w", compression=ZIP_DEFLATED) as archive:
        for name, (data, mode) in entries.items():
            info = ZipInfo(name)
            info.create_system = 3
            info.compress_type = ZIP_DEFLATED
            info.external_attr = (mode & 0o777) << 16
            archive.writestr(info, data)


def write_test_zip_archive_with_corrupt_member(
    archive_path: Path,
    entries: dict[str, tuple[bytes, int]],
    corrupt_name: str,
) -> None:
    entries = entries_with_runtime_archive_name(entries, archive_path.name)
    with ZipFile(archive_path, "w", compression=ZIP_STORED) as archive:
        for name, (data, mode) in entries.items():
            info = ZipInfo(name)
            info.create_system = 3
            info.compress_type = ZIP_STORED
            info.external_attr = (mode & 0o777) << 16
            archive.writestr(info, data)

    marker = entries[corrupt_name][0]
    archive_bytes = bytearray(archive_path.read_bytes())
    marker_index = archive_bytes.find(marker)
    if marker_index < 0:
        raise AssertionError(f"Unable to find stored zip member payload for corruption: {corrupt_name}")
    archive_bytes[marker_index] ^= 0x01
    archive_path.write_bytes(archive_bytes)


def write_test_zip_archive_with_directory_entry(
    archive_path: Path, entries: dict[str, tuple[bytes, int]], directory_name: str
) -> None:
    write_test_zip_archive(archive_path, entries)
    with ZipFile(archive_path, "a", compression=ZIP_DEFLATED) as archive:
        info = ZipInfo(directory_name.rstrip("/") + "/")
        info.create_system = 0
        info.compress_type = ZIP_DEFLATED
        info.external_attr = 0
        archive.writestr(info, b"")


def write_test_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, mode)


def make_symlink(target: Path | str, link: Path, *, target_is_directory: bool = False) -> bool:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (AttributeError, NotImplementedError, OSError):
        return False
    return True


def validate_macos_app_bundle_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-app-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    app_root = package_root / "openQ4.app"
    app_contents = app_root / "Contents"
    arch = "arm64"
    version = "0.2.000"

    shutil.rmtree(work, ignore_errors=True)
    try:
        client_bytes = b"client-binary\n"
        write_test_file(package_root / f"openQ4-client_{arch}", client_bytes, 0o755)
        write_test_file(app_contents / "Info.plist", make_macos_plist_bytes(package, version))
        write_test_file(app_contents / "PkgInfo", package.MACOS_PKGINFO_BYTES)
        write_test_file(app_contents / "MacOS" / "openQ4", client_bytes, 0o755)
        write_test_file(app_contents / "Frameworks" / f"game-sp_{arch}.dylib", b"sp-module\n", 0o755)
        write_test_file(app_contents / "Frameworks" / f"game-mp_{arch}.dylib", b"mp-module\n", 0o755)
        write_test_file(app_contents / "Frameworks" / f"renderer-vk_{arch}.dylib", b"renderer-vk-module\n", 0o755)
        write_test_file(
            app_contents / "Frameworks" / package.MACOS_MOLTENVK_DYLIB_NAME,
            b"moltenvk-runtime\n",
            0o755,
        )
        write_test_file(
            app_contents / "Frameworks" / package.MACOS_OPENAL_SOFT_DYLIB_NAME,
            b"openal-soft-runtime\n",
            0o755,
        )
        for filename in package.MACOS_OPENAL_SOFT_LICENSE_FILES:
            write_test_file(
                app_contents / "Resources" / "licenses" / "openal-soft" / filename,
                f"openal-soft-{filename}\n".encode(),
            )
        write_test_file(app_contents / "Resources" / "openQ4.icns", b"icns\n")
        write_test_file(app_contents / "Resources" / "VERSION.txt", b"openQ4\n")
        write_test_file(app_contents / "Resources" / package.GAME_DIR_NAME / "mod.json", b"{}\n")
        write_test_file(app_contents / "Resources" / package.GAME_DIR_NAME / "pak0.pk4", b"pk4\n")
        write_test_file(app_contents / "Resources" / package.GAME_DIR_NAME / "pak1.pk4", b"pk4\n")
        write_test_file(
            app_contents / "Resources" / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp",
            b"bmp\n",
        )
        localized_info = make_macos_localized_info_bytes(version)
        write_test_file(app_contents / "Resources" / "English.lproj" / "InfoPlist.strings", localized_info)
        write_test_file(app_contents / "Resources" / "French.lproj" / "InfoPlist.strings", localized_info)
        write_test_file(
            app_contents / "Resources" / "English.lproj" / package.MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME,
            make_macos_package_root_error_bytes(package, "English"),
        )
        write_test_file(
            app_contents / "Resources" / "French.lproj" / package.MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME,
            make_macos_package_root_error_bytes(package, "French"),
        )

        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        signature_resources = app_contents / "_CodeSignature" / "CodeResources"
        write_test_file(signature_resources, make_macos_code_resources_bytes())
        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        write_test_file(signature_resources, b"")
        expect_runtime_error(
            "code signature resources is empty",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app bundle with empty code signature resources",
        )
        write_test_file(signature_resources, make_macos_code_resources_bytes())

        unexpected_signature_file = app_contents / "_CodeSignature" / "stale"
        write_test_file(unexpected_signature_file, b"stale\n")
        expect_runtime_error(
            "unexpected files",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app bundle with unexpected code signature payload",
        )
        unexpected_signature_file.unlink()
        signature_resources.unlink()
        signature_resources.parent.rmdir()

        unexpected_bundle_file = app_contents / "Frameworks" / "stale-helper.dylib"
        write_test_file(unexpected_bundle_file, b"stale\n", 0o755)
        expect_runtime_error(
            "unexpected files",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app bundle with unexpected nested payload",
        )
        unexpected_bundle_file.unlink()

        unexpected_bundle_dir = app_contents / "PlugIns"
        unexpected_bundle_dir.mkdir()
        expect_runtime_error(
            "unexpected directories",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app bundle with unexpected empty directory",
        )
        unexpected_bundle_dir.rmdir()

        write_test_file(app_contents / "MacOS" / "openQ4", b"bundle-scoped-signature\n", 0o755)
        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        write_test_file(app_contents / "MacOS" / "openQ4", client_bytes, 0o755)
        write_test_file(app_contents / "Resources" / "openQ4.icns", b"")
        expect_runtime_error(
            "icon is empty",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "empty macOS app icon",
        )

        write_test_file(app_contents / "Resources" / "openQ4.icns", b"icns\n")
        write_test_file(app_contents / "Info.plist", plistlib.dumps(["not-a-dictionary"]))
        expect_runtime_error(
            "dictionary root",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app Info.plist with non-dictionary root",
        )

        write_test_file(app_contents / "Info.plist", make_macos_plist_bytes(package, version))
        missing_layout_plist = dict(package.MACOS_EXPECTED_PLIST_VALUES)
        missing_layout_plist.pop(package.MACOS_RUNTIME_LAYOUT_KEY)
        missing_layout_plist.update(
            {
                "CFBundleShortVersionString": version,
                "CFBundleVersion": version,
                "NSHighResolutionCapable": True,
                "NSSupportsAutomaticGraphicsSwitching": True,
            }
        )
        write_test_file(app_contents / "Info.plist", plistlib.dumps(missing_layout_plist))
        expect_runtime_error(
            "OpenQ4RuntimeLayout",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app Info.plist without self-contained runtime marker",
        )
        write_test_file(app_contents / "Info.plist", make_macos_plist_bytes(package, version))
        write_test_file(app_contents / "PkgInfo", b"BROKEN!!")
        expect_runtime_error(
            "valid PkgInfo",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "invalid macOS app PkgInfo",
        )

        missing_icon_package = work / "missing-icon-package"
        missing_icon_install = work / "missing-icon-install"
        write_test_file(missing_icon_package / f"openQ4-client_{arch}", client_bytes, 0o755)
        write_test_file(missing_icon_package / package.GAME_DIR_NAME / "mod.json", b"{}\n")
        write_test_file(missing_icon_package / package.GAME_DIR_NAME / "pak0.pk4", b"pk4\n")
        write_test_file(missing_icon_package / package.GAME_DIR_NAME / "pak1.pk4", b"pk4\n")
        write_test_file(
            missing_icon_package / package.GAME_DIR_NAME / f"game-sp_{arch}.dylib",
            b"sp-module\n",
            0o755,
        )
        write_test_file(
            missing_icon_package / package.GAME_DIR_NAME / f"game-mp_{arch}.dylib",
            b"mp-module\n",
            0o755,
        )
        # meson installs the renderer module beside the engine binaries in the
        # package root, and MoltenVK is staged into the install tree by
        # tools/build/prepare_macos_moltenvk.sh. Both are consumed before the icon
        # lookup, so both must be present for the icon failure to be the one that
        # is actually exercised here.
        write_test_file(
            missing_icon_package / f"renderer-vk_{arch}.dylib",
            b"renderer-vk-module\n",
            0o755,
        )
        write_test_file(
            missing_icon_package / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp",
            b"bmp\n",
        )
        missing_icon_install.mkdir(parents=True, exist_ok=True)
        write_test_file(
            missing_icon_install / package.MACOS_MOLTENVK_DYLIB_NAME,
            b"moltenvk-runtime\n",
            0o755,
        )
        write_test_file(
            missing_icon_install / "Frameworks" / package.MACOS_OPENAL_SOFT_DYLIB_NAME,
            b"openal-soft-runtime\n",
            0o755,
        )
        for filename in package.MACOS_OPENAL_SOFT_LICENSE_FILES:
            write_test_file(
                missing_icon_install / "licenses" / "openal-soft" / filename,
                f"openal-soft-{filename}\n".encode(),
            )
        expect_runtime_error(
            "app icon source was not found",
            lambda: package.create_macos_app_bundle(
                missing_icon_package,
                missing_icon_install,
                arch,
                version,
                "v0.2.000",
            ),
            "macOS app bundle creation without staged icon",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_self_contained_app_creation_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-self-contained-app-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    install_dir = work / "install"
    arch = "arm64"
    version = "0.2.000"

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(package_root / f"openQ4-client_{arch}", b"client\n", 0o755)
        write_test_file(package_root / package.GAME_DIR_NAME / "mod.json", b"{}\n")
        write_test_file(package_root / package.GAME_DIR_NAME / "pak0.pk4", b"pak0\n")
        write_test_file(package_root / package.GAME_DIR_NAME / "pak1.pk4", b"pak1\n")
        write_test_file(
            package_root / package.GAME_DIR_NAME / f"game-sp_{arch}.dylib",
            b"sp\n",
            0o755,
        )
        write_test_file(
            package_root / package.GAME_DIR_NAME / f"game-mp_{arch}.dylib",
            b"mp\n",
            0o755,
        )
        # The renderer module is installed into the package root and moved into
        # Contents/Frameworks; MoltenVK is copied there from the install tree.
        write_test_file(package_root / f"renderer-vk_{arch}.dylib", b"renderer-vk\n", 0o755)
        write_test_file(install_dir / package.MACOS_MOLTENVK_DYLIB_NAME, b"moltenvk\n", 0o755)
        write_test_file(
            install_dir / "Frameworks" / package.MACOS_OPENAL_SOFT_DYLIB_NAME,
            b"openal-soft\n",
            0o755,
        )
        for filename in package.MACOS_OPENAL_SOFT_LICENSE_FILES:
            write_test_file(
                install_dir / "licenses" / "openal-soft" / filename,
                f"openal-soft-{filename}\n".encode(),
            )
        write_test_file(
            install_dir / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp",
            b"bmp\n",
        )
        write_test_file(install_dir / "openQ4.icns", b"icns\n")

        app_root = package.create_macos_app_bundle(
            package_root,
            install_dir,
            arch,
            version,
            "v0.2.000",
        )
        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        if (package_root / package.GAME_DIR_NAME).exists():
            raise AssertionError("macOS package must not duplicate game data beside a self-contained app")
        if (package_root / "assets" / "splash").exists():
            raise AssertionError("macOS package must not retain a duplicate splash tree beside the app")
        if (package_root / f"renderer-vk_{arch}.dylib").exists():
            raise AssertionError("macOS package must not retain the renderer module beside the app")
        for relative_path in (
            package.MACOS_APP_GAME_DATA_DIR / "mod.json",
            package.MACOS_APP_GAME_DATA_DIR / "pak0.pk4",
            package.MACOS_APP_GAME_DATA_DIR / "pak1.pk4",
            package.MACOS_APP_SPLASH_DIR / "quake4_rt_bitmap_4001.bmp",
            package.MACOS_APP_FRAMEWORKS_DIR / f"game-sp_{arch}.dylib",
            package.MACOS_APP_FRAMEWORKS_DIR / f"game-mp_{arch}.dylib",
            package.MACOS_APP_FRAMEWORKS_DIR / f"renderer-vk_{arch}.dylib",
            package.MACOS_APP_FRAMEWORKS_DIR / package.MACOS_MOLTENVK_DYLIB_NAME,
            package.MACOS_APP_FRAMEWORKS_DIR / package.MACOS_OPENAL_SOFT_DYLIB_NAME,
        ):
            if not (app_root / relative_path).is_file():
                raise AssertionError(f"macOS self-contained app is missing {relative_path}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_dmg_source_preflight_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-dmg-source-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(package_root / "VERSION.txt", b"openQ4\n")
        write_test_file(package_root / "openQ4.app" / "Contents" / "Info.plist", b"plist\n")
        write_test_file(package_root / package.MACOS_SUPPORT_INFO_SCRIPT_NAME, make_macos_support_info_script_bytes(package), 0o755)
        package.validate_macos_dmg_source_tree(package_root)

        write_test_file(package_root / package.MACOS_SUPPORT_INFO_SCRIPT_NAME, b"#!/bin/sh\nexit 0\n", 0o755)
        expect_runtime_error(
            "support collector is missing required marker",
            lambda: package.validate_macos_dmg_source_tree(package_root),
            "macOS DMG source tree with placeholder support collector",
        )
        write_test_file(package_root / package.MACOS_SUPPORT_INFO_SCRIPT_NAME, make_macos_support_info_script_bytes(package), 0o755)

        bad_metadata = package_root / ".DS_Store"
        write_test_file(bad_metadata, b"finder\n")
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_dmg_source_tree(package_root),
            "macOS DMG source tree with Finder metadata",
        )
        bad_metadata.unlink()

        case_collision_a = package_root / "Readme.txt"
        case_collision_b = package_root / "readme.txt"
        write_test_file(case_collision_a, b"one\n")
        if not case_collision_b.exists():
            write_test_file(case_collision_b, b"two\n")
            expect_runtime_error(
                "case-insensitive duplicate paths",
                lambda: package.validate_macos_dmg_source_tree(package_root),
                "macOS DMG source tree with case-insensitive collision",
            )
            case_collision_b.unlink()
        case_collision_a.unlink(missing_ok=True)

        if make_symlink("VERSION.txt", package_root / "VERSION.link"):
            expect_runtime_error(
                "symlink entries",
                lambda: package.validate_macos_dmg_source_tree(package_root),
                "macOS DMG source tree with symlink",
            )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_package_main_collateral_error_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-main-collateral-contract"
    source_root = work / "source"
    install_root = work / "install"
    output_root = work / "out"

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(source_root / "assets" / "release" / "README.html", b"<html></html>\n")
        write_test_file(source_root / "LICENSE", b"license\n")
        write_test_file(source_root / "LICENSES" / "KHRONOS-GLES-MIT.txt", b"khronos notice\n")
        write_test_file(source_root / "CONTRIBUTORS.md", b"contributors\n")
        write_test_file(
            source_root / package.MACOS_SUPPORT_INFO_SCRIPT_PATH,
            b"#!/bin/sh\nexit 0\n",
            0o755,
        )
        (install_root / package.GAME_DIR_NAME).mkdir(parents=True, exist_ok=True)

        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            result = package.main(
                [
                    "package_nightly.py",
                    "--platform",
                    "macos",
                    "--arch",
                    "arm64",
                    "--version",
                    "0.2.000",
                    "--version-tag",
                    "v0.2.000",
                    "--source-root",
                    str(source_root),
                    "--install-dir",
                    str(install_root),
                    "--output-dir",
                    str(output_root),
                    "--archive-format",
                    "tar.gz",
                    "--package-suffix=-opengl",
                    "--allow-missing-binaries",
                ]
            )
        if result != 1:
            raise AssertionError(f"macOS package main should reject malformed collector, got {result}")
        error_text = stderr.getvalue()
        if "error:" not in error_text or "support collector is missing required marker" not in error_text:
            raise AssertionError(f"macOS package main printed an unexpected collateral error: {error_text!r}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_dmg_output_preflight_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-dmg-output-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    archive_path = work / "openq4-v0.2.000-macos-arm64-opengl.dmg"

    original_platform = package.sys.platform
    original_which = package.shutil.which
    original_run_macos_command = package.run_macos_command

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(package_root / "VERSION.txt", b"openQ4\n")
        write_test_file(package_root / package.MACOS_SUPPORT_INFO_SCRIPT_NAME, make_macos_support_info_script_bytes(package), 0o755)
        calls = []

        def fake_which(tool_name):
            if tool_name == "hdiutil":
                return "/usr/bin/hdiutil"
            return original_which(tool_name)

        def fake_run_macos_command(command, *, label):
            calls.append((command, label))
            Path(command[-1]).write_bytes(b"dmg\n")
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        package.sys.platform = "darwin"
        package.shutil.which = fake_which
        package.run_macos_command = fake_run_macos_command
        package.create_macos_dmg(package_root, archive_path)
        if not archive_path.is_file() or not any("-format" in command for command, _label in calls):
            raise AssertionError("macOS DMG creation did not call hdiutil create")

        inside_archive = package_root / "self-contained-output.dmg"
        expect_runtime_error(
            "must not be inside the archive input tree",
            lambda: package.create_macos_dmg(package_root, inside_archive),
            "macOS DMG output inside package root",
        )

        inside_zip_archive = package_root / "self-contained-output.zip"
        expect_runtime_error(
            "must not be inside the archive input tree",
            lambda: package.create_release_archive(package_root, inside_zip_archive, "zip"),
            "macOS release archive output inside package root",
        )

        archive_dir = work / "directory-output.dmg"
        archive_dir.mkdir()
        expect_runtime_error(
            "macOS DMG output path is a directory",
            lambda: package.create_macos_dmg(package_root, archive_dir),
            "macOS DMG output directory",
        )

        archive_target = work / "linked-target.dmg"
        archive_link = work / "linked-output.dmg"
        write_test_file(archive_target, b"target\n")
        if make_symlink(archive_target, archive_link):
            expect_runtime_error(
                "macOS DMG output must not be a symlink",
                lambda: package.create_macos_dmg(package_root, archive_link),
                "macOS DMG output symlink",
            )

        parent_target = work / "parent-target"
        parent_target.mkdir()
        parent_link = work / "parent-link"
        if make_symlink(parent_target, parent_link, target_is_directory=True):
            expect_runtime_error(
                "macOS DMG output parent must not be a symlink",
                lambda: package.create_macos_dmg(package_root, parent_link / "out.dmg"),
                "macOS DMG output parent symlink",
            )
    finally:
        package.sys.platform = original_platform
        package.shutil.which = original_which
        package.run_macos_command = original_run_macos_command
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_archive_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-archive-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    arch = "arm64"
    version = "0.2.000"
    plist_bytes = make_macos_plist_bytes(package, version)

    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    try:
        package_root.mkdir(parents=True, exist_ok=True)
        write_test_file(package_root / f"openQ4-client_{arch}", b"client\n", 0o755)
        write_test_file(package_root / f"openQ4-ded_{arch}", b"ded\n", 0o755)
        package.validate_macos_package_root_engine_binaries(package_root, arch)
        write_test_file(package_root / "openQ4-client_x64", b"stale\n", 0o755)
        expect_runtime_error(
            "stale or mismatched root engine binaries",
            lambda: package.validate_macos_package_root_engine_binaries(package_root, arch),
            "macOS package root with stale engine binary",
        )
        shutil.rmtree(package_root)

        entries = make_macos_archive_entries(package, package_root.name, arch, plist_bytes)
        for archive_format, archive_name, writer in (
            ("tar.gz", "good.tar.gz", write_test_targz_archive),
            ("tar.xz", "good.tar.xz", write_test_tarxz_archive),
            ("zip", "good.zip", write_test_zip_archive),
        ):
            archive_path = work / archive_name
            writer(archive_path, entries)
            package.validate_macos_archive_contents(
                package_root,
                archive_path,
                archive_format,
                arch,
                version,
            )

        mislabeled_xz_archive = work / "mislabeled-xz-as-gz.tar.gz"
        write_test_tarxz_archive(
            mislabeled_xz_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
        )
        expect_runtime_error(
            "not a valid tar.gz archive",
            lambda: package.validate_macos_archive_contents(
                package_root,
                mislabeled_xz_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive validator rejects tar.xz data labeled tar.gz",
        )

        mislabeled_gz_archive = work / "mislabeled-gz-as-xz.tar.xz"
        write_test_targz_archive(
            mislabeled_gz_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
        )
        expect_runtime_error(
            "not a valid tar.xz archive",
            lambda: package.validate_macos_archive_contents(
                package_root,
                mislabeled_gz_archive,
                "tar.xz",
                arch,
                version,
            ),
            "macOS archive validator rejects tar.gz data labeled tar.xz",
        )

        bad_zip_archive = work / "bad.zip"
        bad_zip_archive.write_bytes(b"not a zip archive\n")
        expect_runtime_error(
            "not a valid zip archive",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_zip_archive,
                "zip",
                arch,
                version,
            ),
            "macOS archive validator rejects malformed zip input",
        )

        corrupt_zip_archive = work / "corrupt-member.zip"
        corrupt_member = f"{package_root.name}/openQ4.app/Contents/Info.plist"
        write_test_zip_archive_with_corrupt_member(
            corrupt_zip_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            corrupt_member,
        )
        expect_runtime_error(
            "not a valid zip archive",
            lambda: package.validate_macos_archive_contents(
                package_root,
                corrupt_zip_archive,
                "zip",
                arch,
                version,
            ),
            "macOS archive validator rejects zip member read failures",
        )

        bad_runtime_archive_name = work / "bad-runtime-archive-name.tar.gz"
        write_test_targz_archive(
            bad_runtime_archive_name,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            rewrite_runtime_archive=False,
        )
        expect_runtime_error(
            "runtime_archive",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_runtime_archive_name,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive whose symbol manifest names a different runtime archive",
        )

        missing_archive = work / "missing.tar.gz"
        expect_runtime_error(
            "macOS archive was not created",
            lambda: package.validate_macos_archive_contents(
                package_root,
                missing_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive validator missing input",
        )

        expect_runtime_error(
            "Unsupported macOS archive format",
            lambda: package.validate_macos_archive_contents(
                package_root,
                work / "good.tar.gz",
                "rar",
                arch,
                version,
            ),
            "macOS archive validator unsupported format",
        )

        linked_archive = work / "linked-good.tar.gz"
        if make_symlink(work / "good.tar.gz", linked_archive):
            expect_runtime_error(
                "macOS archive path must not be a symlink",
                lambda: package.validate_macos_archive_contents(
                    package_root,
                    linked_archive,
                    "tar.gz",
                    arch,
                    version,
                ),
                "macOS archive validator symlink input",
            )

        previous_runtime_member_cap = package.MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS
        package.MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS = max(1, len(entries) - 1)
        try:
            expect_runtime_error(
                "macOS archive contains too many members",
                lambda: package.validate_macos_archive_contents(
                    package_root,
                    work / "good.tar.gz",
                    "tar.gz",
                    arch,
                    version,
                ),
                "macOS archive validator member-count cap",
            )
        finally:
            package.MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS = previous_runtime_member_cap

        previous_runtime_total_cap = package.MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES
        package.MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES = 4
        try:
            expect_runtime_error(
                "macOS archive total expanded size is too large",
                lambda: package.validate_macos_archive_contents(
                    package_root,
                    work / "good.tar.gz",
                    "tar.gz",
                    arch,
                    version,
                ),
                "macOS archive validator total expanded size cap",
            )
        finally:
            package.MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES = previous_runtime_total_cap

        signed_entries = make_macos_archive_entries(
            package,
            package_root.name,
            arch,
            plist_bytes,
            extra_entries={
                f"{package_root.name}/openQ4.app/Contents/_CodeSignature/CodeResources": (
                    make_macos_code_resources_bytes(),
                    0o644,
                ),
            },
        )
        for archive_format, archive_name, writer in (
            ("tar.gz", "good-signed.tar.gz", write_test_targz_archive),
            ("zip", "good-signed.zip", write_test_zip_archive),
        ):
            archive_path = work / archive_name
            writer(archive_path, signed_entries)
            package.validate_macos_archive_contents(
                package_root,
                archive_path,
                archive_format,
                arch,
                version,
            )

        bad_signature_archive = work / "bad-signature.tar.gz"
        write_test_targz_archive(
            bad_signature_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/_CodeSignature/CodeResources": (
                        b"not-a-plist\n",
                        0o644,
                    ),
                },
            ),
        )
        expect_runtime_error(
            "code signature resources is not a valid plist",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_signature_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with malformed code signature resources",
        )

        bad_signature_extra_archive = work / "bad-signature-extra.tar.gz"
        write_test_targz_archive(
            bad_signature_extra_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/_CodeSignature/stale": (
                        b"stale\n",
                        0o644,
                    ),
                },
            ),
        )
        expect_runtime_error(
            "app bundle contains unexpected entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_signature_extra_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with unexpected code signature payload",
        )

        bad_exec_archive = work / "bad-exec.tar.gz"
        write_test_targz_archive(
            bad_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                client_mode=0o644,
            ),
        )
        expect_runtime_error(
            "macOS archive entry is not executable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_exec_archive,
                "tar.gz",
                arch,
                version,
            ),
            "non-executable macOS archive client",
        )

        bad_support_archive = work / "bad-support-collector.tar.gz"
        write_test_targz_archive(
            bad_support_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/{package.MACOS_SUPPORT_INFO_SCRIPT_NAME}": (
                        b"#!/bin/sh\nexit 0\n",
                        0o755,
                    )
                },
            ),
        )
        expect_runtime_error(
            "support collector is missing required marker",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_support_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with placeholder support collector",
        )

        bad_support_privacy_archive = work / "bad-support-collector-privacy.tar.gz"
        write_test_targz_archive(
            bad_support_privacy_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/{package.MACOS_SUPPORT_INFO_SCRIPT_NAME}": (
                        make_macos_support_info_script_bytes(package) + b"printenv\n",
                        0o755,
                    )
                },
            ),
        )
        expect_runtime_error(
            "forbidden privacy/no-launch pattern",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_support_privacy_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with privacy-unsafe support collector",
        )

        bad_mode_archive = work / "bad-mode.tar.gz"
        write_test_targz_archive(
            bad_mode_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/world-writable.txt": (b"bad\n", 0o666)},
            ),
        )
        expect_runtime_error(
            "group/other writable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_mode_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with unsafe mode bits",
        )

        bad_zip_mode_archive = work / "bad-mode.zip"
        write_test_zip_archive(
            bad_zip_mode_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                client_mode=0o775,
            ),
        )
        expect_runtime_error(
            "group/other writable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_zip_mode_archive,
                "zip",
                arch,
                version,
            ),
            "macOS zip archive with unsafe non-final mode bits",
        )

        bad_metadata_archive = work / "bad-metadata.tar.gz"
        write_test_targz_archive(
            bad_metadata_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/.DS_Store": (b"metadata\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_metadata_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with Finder metadata",
        )

        bad_lower_metadata_archive = work / "bad-lower-metadata.tar.gz"
        write_test_targz_archive(
            bad_lower_metadata_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/.ds_store": (b"metadata\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_lower_metadata_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with lowercase Finder metadata",
        )

        bad_appledouble_archive = work / "bad-appledouble.tar.gz"
        write_test_targz_archive(
            bad_appledouble_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/openQ4.app/Contents/Resources/._openQ4.icns": (b"metadata\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_appledouble_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with AppleDouble metadata",
        )

        bad_macosx_archive = work / "bad-macosx-dir.tar.gz"
        write_test_targz_archive(
            bad_macosx_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/__MACOSX/openQ4": (b"metadata\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "non-runtime metadata/debug entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_macosx_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with __MACOSX metadata",
        )

        bad_app_extra_archive = work / "bad-app-extra.tar.gz"
        write_test_targz_archive(
            bad_app_extra_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/Frameworks/stale-helper.dylib": (b"stale\n", 0o755)
                },
            ),
        )
        expect_runtime_error(
            "app bundle contains unexpected entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_app_extra_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with unexpected app bundle payload",
        )

        bad_duplicate_archive = work / "bad-duplicate.tar.gz"
        duplicate_entries = entries_with_runtime_archive_name(
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            bad_duplicate_archive.name,
        )
        with tarfile.open(bad_duplicate_archive, "w:gz") as archive:
            for name, (data, mode) in duplicate_entries.items():
                for _ in range(2 if name.endswith("VERSION.txt") else 1):
                    member = tarfile.TarInfo(name)
                    member.mode = mode
                    member.size = len(data)
                    member.mtime = 0
                    archive.addfile(member, io.BytesIO(data))
        expect_runtime_error(
            "duplicate entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_duplicate_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with duplicate entry",
        )

        bad_zip_directory_archive = work / "bad-directory-entry.zip"
        write_test_zip_archive_with_directory_entry(
            bad_zip_directory_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            f"{package_root.name}/openQ4.app/Contents/Resources/EmptyDir",
        )
        expect_runtime_error(
            "non-regular entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_zip_directory_archive,
                "zip",
                arch,
                version,
            ),
            "macOS zip archive with directory entry",
        )

        for archive_format, archive_name, writer in (
            ("tar.gz", "bad-case-duplicate.tar.gz", write_test_targz_archive),
            ("zip", "bad-case-duplicate.zip", write_test_zip_archive),
        ):
            bad_case_duplicate_archive = work / archive_name
            writer(
                bad_case_duplicate_archive,
                make_macos_archive_entries(
                    package,
                    package_root.name,
                    arch,
                    plist_bytes,
                    extra_entries={f"{package_root.name}/version.txt": (b"ambiguous\n", 0o644)},
                ),
            )
            expect_runtime_error(
                "case-insensitive duplicate",
                lambda archive_path=bad_case_duplicate_archive, archive_format=archive_format: package.validate_macos_archive_contents(
                    package_root,
                    archive_path,
                    archive_format,
                    arch,
                    version,
                ),
                f"macOS {archive_format} archive with case-insensitive duplicate entry",
            )

        bad_path_archive = work / "bad-path.tar.gz"
        write_test_targz_archive(
            bad_path_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}\\bad": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "unsafe",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_path_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with backslash path",
        )

        for archive_format, archive_name, writer in (
            ("tar.gz", "bad-control-path.tar.gz", write_test_targz_archive),
            ("zip", "bad-control-path.zip", write_test_zip_archive),
        ):
            bad_control_path_archive = work / archive_name
            writer(
                bad_control_path_archive,
                make_macos_archive_entries(
                    package,
                    package_root.name,
                    arch,
                    plist_bytes,
                    extra_entries={f"{package_root.name}/baseoq4/bad\nname.txt": (b"bad\n", 0o644)},
                ),
            )
            expect_runtime_error(
                "unsafe",
                lambda archive_path=bad_control_path_archive, archive_format=archive_format: package.validate_macos_archive_contents(
                    package_root,
                    archive_path,
                    archive_format,
                    arch,
                    version,
                ),
                f"macOS {archive_format} archive with control-character path",
            )

        bad_pkginfo_archive = work / "bad-pkginfo.tar.gz"
        write_test_targz_archive(
            bad_pkginfo_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/PkgInfo": (b"BROKEN!!", 0o644)
                },
            ),
        )
        expect_runtime_error(
            "PkgInfo",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_pkginfo_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with invalid PkgInfo",
        )

        bad_manifest_archive = work / "bad-manifest.tar.gz"
        write_test_targz_archive(
            bad_manifest_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/VERSION.txt": (
                        make_version_manifest_bytes(version, "wrong-tag", "macos", arch),
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "version_tag",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_manifest_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with mismatched version manifest",
        )

        bad_localized_archive = work / "bad-localized.tar.gz"
        write_test_targz_archive(
            bad_localized_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (
                        b'CFBundleName = "openQ4";\n',
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "CFBundleShortVersionString",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_localized_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with incomplete localized plist strings",
        )

        bad_duplicate_localized_archive = work / "bad-duplicate-localized.tar.gz"
        write_test_targz_archive(
            bad_duplicate_localized_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (
                        make_macos_localized_info_bytes(version) + b'CFBundleName = "openQ4";\n',
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "duplicate key",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_duplicate_localized_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with duplicate localized plist key",
        )

        bad_huge_metadata_archive = work / "bad-huge-metadata.tar.gz"
        write_test_targz_archive(
            bad_huge_metadata_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/Info.plist": (
                        b"x" * (package.MAX_MACOS_METADATA_MEMBER_BYTES + 1),
                        0o644,
                    )
                },
            ),
        )
        expect_runtime_error(
            "metadata member is too large",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_huge_metadata_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with oversized app metadata",
        )

        bad_symlink_archive = work / "bad-symlink.tar.gz"
        write_test_targz_archive_with_symlink(
            bad_symlink_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            f"{package_root.name}/openQ4.app/Contents/MacOS/linked",
        )
        expect_runtime_error(
            "symlink entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_symlink_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with symlink entry",
        )

        bad_special_archive = work / "bad-special.tar.gz"
        write_test_targz_archive_with_fifo(
            bad_special_archive,
            make_macos_archive_entries(package, package_root.name, arch, plist_bytes),
            f"{package_root.name}/openQ4.app/Contents/MacOS/fifo",
        )
        expect_runtime_error(
            "non-regular entry",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_special_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with non-regular entry",
        )

        bad_stale_module_archive = work / "bad-stale-module.tar.gz"
        write_test_targz_archive(
            bad_stale_module_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/{package.GAME_DIR_NAME}/game-sp_x64.dylib": (b"stale\n", 0o755)},
            ),
        )
        expect_runtime_error(
            "stale or mismatched game modules",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_stale_module_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with stale game module",
        )

        bad_wrong_platform_module_archive = work / "bad-wrong-platform-module.tar.gz"
        write_test_targz_archive(
            bad_wrong_platform_module_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/{package.GAME_DIR_NAME}/game-mp_{arch}.dll": (b"wrong\n", 0o755)},
            ),
        )
        expect_runtime_error(
            "wrong-platform entries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_wrong_platform_module_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with Windows game module",
        )

        bad_stale_root_binary_archive = work / "bad-stale-root-binary.tar.gz"
        write_test_targz_archive(
            bad_stale_root_binary_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/openQ4-ded_x64": (b"stale\n", 0o755)},
            ),
        )
        expect_runtime_error(
            "stale or mismatched root engine binaries",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_stale_root_binary_archive,
                "tar.gz",
                arch,
                version,
            ),
            "macOS archive with stale root engine binary",
        )

        bad_plist_archive = work / "bad-plist-version.tar.gz"
        write_test_targz_archive(
            bad_plist_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                make_macos_plist_bytes(package, "0.1.000"),
            ),
        )
        expect_runtime_error(
            "CFBundleShortVersionString",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_plist_archive,
                "tar.gz",
                arch,
                version,
            ),
            "mismatched macOS archive plist version",
        )

        signed_app_exec_archive = work / "signed-app-exec.tar.gz"
        write_test_targz_archive(
            signed_app_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/MacOS/openQ4": (
                        b"other-binary\n",
                        0o755,
                    )
                },
            ),
        )
        package.validate_macos_archive_contents(
            package_root,
            signed_app_exec_archive,
            "tar.gz",
            arch,
            version,
        )

        unsafe_archive = work / "unsafe.tar.gz"
        write_test_targz_archive(
            unsafe_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={"other-package/escaped": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "macOS archive contains unsafe or out-of-package path",
            lambda: package.validate_macos_archive_contents(
                package_root,
                unsafe_archive,
                "tar.gz",
                arch,
                version,
            ),
            "out-of-package macOS archive entry",
        )

        unsafe_empty_segment_archive = work / "unsafe-empty-segment.tar.gz"
        write_test_targz_archive(
            unsafe_empty_segment_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}//escaped": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "macOS archive contains unsafe or out-of-package path",
            lambda: package.validate_macos_archive_contents(
                package_root,
                unsafe_empty_segment_archive,
                "tar.gz",
                arch,
                version,
            ),
            "empty-segment macOS archive entry",
        )

        unsafe_dot_segment_archive = work / "unsafe-dot-segment.tar.gz"
        write_test_targz_archive(
            unsafe_dot_segment_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={f"{package_root.name}/./escaped": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "macOS archive contains unsafe or out-of-package path",
            lambda: package.validate_macos_archive_contents(
                package_root,
                unsafe_dot_segment_archive,
                "tar.gz",
                arch,
                version,
            ),
            "dot-segment macOS archive entry",
        )

        version_tag = "v0.2.000"
        package_suffix = "-opengl"
        symbol_root_name = package.macos_symbol_archive_stem(version_tag, arch, package_suffix)
        symbol_entries = make_macos_symbol_archive_entries(
            package,
            package_root.name,
            symbol_root_name,
            arch,
            version,
            version_tag,
        )
        good_symbol_archive = work / f"{symbol_root_name}.tar.xz"
        write_test_tarxz_archive(good_symbol_archive, symbol_entries, rewrite_runtime_archive=False)
        package.validate_macos_symbol_archive_contents(
            good_symbol_archive,
            symbol_root_name,
            version=version,
            version_tag=version_tag,
            arch=arch,
            package_suffix=package_suffix,
            runtime_archive_name=f"{package_root.name}.tar.gz",
        )

        mislabeled_symbol_archive = work / f"{symbol_root_name}-gzip.tar.xz"
        write_test_targz_archive(mislabeled_symbol_archive, symbol_entries, rewrite_runtime_archive=False)
        expect_runtime_error(
            "not a valid xz-compressed tar archive",
            lambda: package.validate_macos_symbol_archive_contents(
                mislabeled_symbol_archive,
                symbol_root_name,
                version=version,
                version_tag=version_tag,
                arch=arch,
                package_suffix=package_suffix,
                runtime_archive_name=f"{package_root.name}.tar.gz",
            ),
            "macOS symbol archive validator rejects gzip data labeled tar.xz",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_signing_config_runtime() -> None:
    package = load_package_module()

    def args(**overrides):
        values = {
            "macos_signing_mode": "ad-hoc",
            "macos_code_sign_identity": "",
            "macos_entitlements": "",
            "macos_notarize": False,
            "macos_notary_keychain_profile": "",
            "macos_notary_keychain": "",
        }
        values.update(overrides)
        return SimpleNamespace(**values)

    ad_hoc = package.resolve_macos_signing_config(args())
    if ad_hoc.mode != "ad-hoc" or ad_hoc.identity != "-" or ad_hoc.hardened_runtime or ad_hoc.timestamp:
        raise AssertionError("macOS ad-hoc signing config should keep local/debug signing lightweight")

    expect_runtime_error(
        "notarization requires --macos-signing-mode=developer-id",
        lambda: package.resolve_macos_signing_config(args(macos_notarize=True)),
        "ad-hoc macOS notarization rejection",
    )
    expect_runtime_error(
        "requires --macos-code-sign-identity",
        lambda: package.resolve_macos_signing_config(args(macos_signing_mode="developer-id")),
        "Developer ID signing without identity",
    )

    work = ROOT / ".tmp" / "macos-entitlements-contract"
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True)
        valid_entitlements = work / "valid.entitlements"
        invalid_entitlements = work / "invalid.entitlements"
        list_entitlements = work / "list.entitlements"
        sandbox_entitlements = work / "sandbox.entitlements"
        debug_entitlements = work / "debug.entitlements"
        notary_keychain = work / "notary.keychain-db"

        valid_entitlements.write_bytes(plistlib.dumps({"com.apple.security.files.user-selected.read-only": True}))
        invalid_entitlements.write_text("not a plist\n", encoding="utf-8")
        list_entitlements.write_bytes(plistlib.dumps(["not", "a", "dict"]))
        sandbox_entitlements.write_bytes(plistlib.dumps({"com.apple.security.app-sandbox": True}))
        debug_entitlements.write_bytes(plistlib.dumps({"com.apple.security.get-task-allow": True}))
        notary_keychain.write_bytes(b"test keychain\n")

        expect_runtime_error(
            "not a valid plist",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(invalid_entitlements))),
            "malformed macOS entitlements rejection",
        )
        expect_runtime_error(
            "dictionary root",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(list_entitlements))),
            "non-dictionary macOS entitlements rejection",
        )
        expect_runtime_error(
            "com.apple.security.app-sandbox",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(sandbox_entitlements))),
            "unsupported macOS App Sandbox entitlement rejection",
        )
        expect_runtime_error(
            "com.apple.security.get-task-allow",
            lambda: package.resolve_macos_signing_config(args(macos_entitlements=str(debug_entitlements))),
            "unsupported macOS debug entitlement rejection",
        )

        developer_id = package.resolve_macos_signing_config(
            args(
                macos_signing_mode="developer-id",
                macos_code_sign_identity="Developer ID Application: DarkMatter Productions (TEAMID1234)",
                macos_entitlements=str(valid_entitlements),
                macos_notarize=True,
                macos_notary_keychain_profile="openq4-release-notary",
                macos_notary_keychain=str(notary_keychain),
            )
        )
        if (
            developer_id.mode != "developer-id"
            or not developer_id.hardened_runtime
            or not developer_id.timestamp
            or developer_id.entitlements != valid_entitlements.resolve()
            or not developer_id.notarize
            or developer_id.notary_keychain_profile != "openq4-release-notary"
            or developer_id.notary_keychain != notary_keychain.resolve()
        ):
            raise AssertionError("Developer ID macOS signing config did not enable release signing requirements")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_notary_archive_output_preflight_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-notary-output-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    notary_archive = package_root.parent / f"{package_root.name}-notary.zip"
    config = package.MacOSSigningConfig(
        mode="developer-id",
        identity="Developer ID Application: DarkMatter Productions (TEAMID1234)",
        hardened_runtime=True,
        timestamp=True,
        entitlements=None,
        notarize=True,
        notary_keychain_profile="openq4-release-notary",
        notary_keychain=None,
    )

    original_platform = package.sys.platform
    original_which = package.shutil.which
    original_run_macos_command = package.run_macos_command

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(package_root / "VERSION.txt", b"openQ4\n")
        write_test_file(package_root / package.MACOS_SUPPORT_INFO_SCRIPT_NAME, make_macos_support_info_script_bytes(package), 0o755)
        calls = []

        def fake_which(tool_name):
            if tool_name in {"ditto", "xcrun", "spctl"}:
                return f"/usr/bin/{tool_name}"
            return original_which(tool_name)

        def fake_run_macos_command(command, *, label):
            calls.append((command, label))
            if command[0] == "ditto":
                Path(command[-1]).write_bytes(b"notary archive\n")
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        package.sys.platform = "darwin"
        package.shutil.which = fake_which
        package.run_macos_command = fake_run_macos_command
        package.notarize_macos_app_bundle(package_root, config)
        if not any(command[0] == "ditto" for command, _label in calls):
            raise AssertionError("macOS app notarization did not create a notary archive")
        if notary_archive.exists():
            raise AssertionError("macOS app notarization should remove the temporary notary archive")

        notary_archive.mkdir()
        expect_runtime_error(
            "macOS notarization archive output path is a directory",
            lambda: package.notarize_macos_app_bundle(package_root, config),
            "macOS notarization archive output directory",
        )
        notary_archive.rmdir()

        notary_target = work / "notary-target.zip"
        write_test_file(notary_target, b"target\n")
        if make_symlink(notary_target, notary_archive):
            expect_runtime_error(
                "macOS notarization archive output must not be a symlink",
                lambda: package.notarize_macos_app_bundle(package_root, config),
                "macOS notarization archive output symlink",
            )
    finally:
        package.sys.platform = original_platform
        package.shutil.which = original_which
        package.run_macos_command = original_run_macos_command
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_signing_keeps_standalone_client_signature_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-standalone-signing-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    app_root = package_root / "openQ4.app"
    app_executable = app_root / "Contents" / "MacOS" / "openQ4"
    arch = "arm64"

    original_platform = package.sys.platform
    original_which = package.shutil.which
    original_codesign_target = package.macos_codesign_target

    shutil.rmtree(work, ignore_errors=True)
    try:
        client_binary = package_root / f"openQ4-client_{arch}"
        write_test_file(client_binary, b"unsigned-client\n", 0o755)
        write_test_file(package_root / f"openQ4-ded_{arch}", b"dedicated\n", 0o755)
        framework_root = app_root / package.MACOS_APP_FRAMEWORKS_DIR
        write_test_file(framework_root / f"game-sp_{arch}.dylib", b"sp\n", 0o755)
        write_test_file(framework_root / f"game-mp_{arch}.dylib", b"mp\n", 0o755)
        write_test_file(framework_root / f"renderer-vk_{arch}.dylib", b"renderer-vk\n", 0o755)
        write_test_file(framework_root / package.MACOS_MOLTENVK_DYLIB_NAME, b"moltenvk\n", 0o755)
        write_test_file(framework_root / package.MACOS_OPENAL_SOFT_DYLIB_NAME, b"openal-soft\n", 0o755)
        write_test_file(app_executable, b"stale-app-executable\n", 0o755)

        calls = []

        def fake_which(tool_name):
            if tool_name == "codesign":
                return "/usr/bin/codesign"
            return original_which(tool_name)

        def fake_codesign_target(codesign_path, target, config, *, include_entitlements=True):
            del codesign_path, config
            calls.append((target.relative_to(package_root).as_posix(), include_entitlements))
            if target == app_root:
                write_test_file(app_executable, app_executable.read_bytes() + b"bundle-signed\n", 0o755)
            else:
                target.write_bytes(target.read_bytes() + b"standalone-signed\n")

        package.sys.platform = "darwin"
        package.shutil.which = fake_which
        package.macos_codesign_target = fake_codesign_target
        package.sign_macos_payload(
            package_root,
            arch,
            package.MacOSSigningConfig(
                mode="ad-hoc",
                identity="-",
                hardened_runtime=False,
                timestamp=False,
                entitlements=None,
                notarize=False,
                notary_keychain_profile="",
                notary_keychain=None,
            ),
        )

        # Every nested Contents/Frameworks image is signed inside-out and without
        # the app entitlements, including the vendored libMoltenVK.dylib, which
        # arrives ad-hoc signed upstream and must be re-signed with the release
        # identity before notarization.
        expected_calls = [
            (f"openQ4-client_{arch}", True),
            (f"openQ4-ded_{arch}", True),
            (f"openQ4.app/Contents/Frameworks/game-sp_{arch}.dylib", False),
            (f"openQ4.app/Contents/Frameworks/game-mp_{arch}.dylib", False),
            (f"openQ4.app/Contents/Frameworks/renderer-vk_{arch}.dylib", False),
            (f"openQ4.app/Contents/Frameworks/{package.MACOS_MOLTENVK_DYLIB_NAME}", False),
            (f"openQ4.app/Contents/Frameworks/{package.MACOS_OPENAL_SOFT_DYLIB_NAME}", False),
            ("openQ4.app", True),
        ]
        if calls != expected_calls:
            raise AssertionError(f"Unexpected macOS signing order: {calls!r}")
        if client_binary.read_bytes() != b"unsigned-client\nstandalone-signed\n":
            raise AssertionError("macOS signing should leave the loose client standalone-signed")
        if app_executable.read_bytes() != b"unsigned-client\nstandalone-signed\nbundle-signed\n":
            raise AssertionError("macOS signing should bundle-sign only the app executable copy")
        if client_binary.read_bytes() == app_executable.read_bytes():
            raise AssertionError("macOS signing should not recopy the bundle-scoped app signature to the loose client")
        if not os.access(client_binary, os.X_OK):
            raise AssertionError("macOS signing should preserve the loose client executable bit")
    finally:
        package.sys.platform = original_platform
        package.shutil.which = original_which
        package.macos_codesign_target = original_codesign_target
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_install_name_normalization_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-install-name-contract"

    original_platform = package.sys.platform
    original_which = package.shutil.which
    original_otool_install_name = package.macos_otool_install_name
    original_run_macos_command = package.run_macos_command

    shutil.rmtree(work, ignore_errors=True)
    try:
        install_names: dict[tuple[Path, str], str] = {}
        calls = []
        active_arch = ""

        def fake_which(tool_name):
            if tool_name == "install_name_tool":
                return "/usr/bin/install_name_tool"
            return original_which(tool_name)

        def fake_otool_install_name(binary_path, *, macho_arch=None):
            if macho_arch is None:
                raise AssertionError("install-name normalization did not select a Mach-O slice")
            return install_names[(Path(binary_path), macho_arch)]

        def fake_run_macos_command(command, *, label):
            calls.append((command, label))
            module = Path(command[3])
            for macho_arch in package.macos_expected_lipo_arches(active_arch):
                install_names[(module, macho_arch)] = command[2]
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        package.sys.platform = "darwin"
        package.shutil.which = fake_which
        package.macos_otool_install_name = fake_otool_install_name
        package.run_macos_command = fake_run_macos_command

        def run_case(
            package_root: Path,
            arch: str,
            stale_ids: dict[tuple[str, str], str],
            expected_updated_keys: tuple[str, ...],
        ) -> None:
            nonlocal active_arch
            active_arch = arch
            calls.clear()
            install_names.clear()
            modules = package.macos_loadable_module_install_names(package_root, arch)
            module_keys = {
                "game-sp": package.macos_embedded_game_module_paths(package_root, arch)[0],
                "game-mp": package.macos_embedded_game_module_paths(package_root, arch)[1],
                "renderer-vk": package.macos_embedded_renderer_module_path(package_root, arch),
                "openal-soft": package.macos_embedded_openal_soft_path(package_root),
            }
            explicit_modules = {
                module_keys["game-sp"]: f"@loader_path/game-sp_{arch}.dylib",
                module_keys["game-mp"]: f"@loader_path/game-mp_{arch}.dylib",
                module_keys["renderer-vk"]: f"@loader_path/renderer-vk_{arch}.dylib",
                module_keys["openal-soft"]: package.MACOS_OPENAL_SOFT_INSTALL_NAME,
            }
            if modules != explicit_modules:
                raise AssertionError(f"Unexpected macOS loadable module install-name map: {modules!r}")
            for module in modules:
                write_test_file(module, b"module\n", 0o755)
            for key, module in module_keys.items():
                expected = modules[module]
                for macho_arch in package.macos_expected_lipo_arches(arch):
                    install_names[(module, macho_arch)] = stale_ids.get((key, macho_arch), expected)

            package.normalize_macos_loadable_module_install_names(package_root, arch)

            expected_calls = [
                (
                    ["/usr/bin/install_name_tool", "-id", modules[module_keys[key]], str(module_keys[key])],
                    f"setting macOS loadable module install name for {module_keys[key]}",
                )
                for key in expected_updated_keys
            ]
            if calls != expected_calls:
                raise AssertionError(f"Unexpected macOS install-name normalization calls: {calls!r}")
            for module, expected in modules.items():
                for macho_arch in package.macos_expected_lipo_arches(arch):
                    actual = install_names[(module, macho_arch)]
                    if actual != expected:
                        raise AssertionError(
                            f"macOS loadable module install name was not normalized for {macho_arch}: "
                            f"{module} has {actual!r}, expected {expected!r}"
                        )

        run_case(
            work / "openq4-v0.2.000-macos-arm64-opengl",
            "arm64",
            {
                ("game-sp", "arm64"): "/tmp/openq4/.install/baseoq4/game-sp_arm64.dylib",
                ("renderer-vk", "arm64"): "/tmp/openq4/.install/renderer-vk_arm64.dylib",
            },
            ("game-sp", "renderer-vk"),
        )
        run_case(
            work / "openq4-v0.2.000-macos-universal2-metal",
            "universal2",
            {
                ("renderer-vk", "x86_64"): "/tmp/openq4/.install/renderer-vk_universal2.dylib",
            },
            ("renderer-vk",),
        )
    finally:
        package.sys.platform = original_platform
        package.shutil.which = original_which
        package.macos_otool_install_name = original_otool_install_name
        package.run_macos_command = original_run_macos_command
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_pk4_symlink_guard_runtime() -> None:
    if not hasattr(os, "symlink"):
        return

    package = load_package_module()
    work = ROOT / ".tmp" / "macos-pk4-symlink-contract"
    install_game_dir = work / "baseoq4"
    destination_pk4 = work / "pak0.pk4"

    shutil.rmtree(work, ignore_errors=True)
    try:
        write_test_file(install_game_dir / "mod.json", b"{}\n")
        write_test_file(install_game_dir / "normal.txt", b"ok\n")
        try:
            os.symlink("normal.txt", install_game_dir / "linked.txt")
        except (OSError, NotImplementedError):
            return

        expect_runtime_error(
            "refusing to package symlink",
            lambda: package.create_game_pk4(install_game_dir, destination_pk4),
            "macOS package PK4 symlink source",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_legacy_macos_plist_runtime() -> None:
    package = load_package_module()
    info_plist_path = ROOT / "src" / "sys" / "osx" / "Info.plist"
    version_plist_path = ROOT / "src" / "sys" / "osx" / "version.plist"

    with info_plist_path.open("rb") as handle:
        info_plist = plistlib.load(handle)
    with version_plist_path.open("rb") as handle:
        version_plist = plistlib.load(handle)

    version = version_plist["CFBundleVersion"]
    if version_plist.get("CFBundleShortVersionString") != version:
        raise AssertionError("legacy macOS version.plist short version must match CFBundleVersion")

    package.validate_macos_plist_values(info_plist, "legacy macOS Info.plist", version)


def validate_macos_staged_payload_validator_runtime() -> None:
    validator = load_validation_module()
    work = ROOT / ".tmp" / "macos-staged-contract"
    install_root = work / ".install"
    game_dir = install_root / "baseoq4"
    arch = "arm64"
    expected_lipo_arch = validator.macos_expected_lipo_arch(arch)
    real_macos_lipo_arches = validator.macos_lipo_arches
    fake_lipo_arches: dict[Path, set[str]] = {}

    def fake_macos_lipo_arches(binary_path: Path) -> set[str]:
        return fake_lipo_arches.get(binary_path, {expected_lipo_arch})

    shutil.rmtree(work, ignore_errors=True)
    try:
        validator.macos_lipo_arches = fake_macos_lipo_arches
        client = install_root / f"openQ4-client_{arch}"
        dedicated = install_root / f"openQ4-ded_{arch}"
        support_script = install_root / validator.MACOS_SUPPORT_INFO_SCRIPT_NAME
        write_test_file(install_root / "openQ4.icns", b"icns\n")
        write_test_file(install_root / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp", b"bmp\n")
        write_test_file(support_script, make_validation_support_info_script_bytes(validator), 0o755)
        write_test_file(client, b"client\n", 0o755)
        write_test_file(dedicated, b"ded\n", 0o755)
        openal_runtime = install_root / "Frameworks" / validator.MACOS_OPENAL_SOFT_DYLIB_NAME
        write_test_file(openal_runtime, b"openal-soft\n", 0o755)
        for filename in validator.MACOS_OPENAL_SOFT_LICENSE_FILES:
            write_test_file(
                install_root / "licenses" / "openal-soft" / filename,
                f"openal-soft-{filename}\n".encode(),
            )
        sp_module = game_dir / f"game-sp_{arch}.dylib"
        mp_module = game_dir / f"game-mp_{arch}.dylib"
        write_test_file(sp_module, b"sp\n", 0o755)
        write_test_file(mp_module, b"mp\n", 0o755)

        validator.validate_macos_staged_metadata(
            ROOT,
            install_root,
            game_dir,
            [client],
            [dedicated],
        )

        if validator.host_is_macos():
            fake_lipo_arches[client] = {"x86_64"}
            expect_runtime_error(
                "architecture mismatch",
                lambda: validator.validate_macos_staged_metadata(
                    ROOT,
                    install_root,
                    game_dir,
                    [client],
                    [dedicated],
                ),
                "macOS staged payload with mismatched client architecture",
            )
            fake_lipo_arches[client] = {expected_lipo_arch}

        stale_client = install_root / "openQ4-client_x64"
        stale_dedicated = install_root / "openQ4-ded_x64"
        write_test_file(stale_client, b"stale-client\n", 0o755)
        write_test_file(stale_dedicated, b"stale-ded\n", 0o755)
        expect_runtime_error(
            "stale or mismatched root engine binaries",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client, stale_client],
                [dedicated, stale_dedicated],
            ),
            "macOS staged payload with stale root engine binaries",
        )
        stale_client.unlink()
        stale_dedicated.unlink()

        support_script.unlink()
        expect_runtime_error(
            "missing support collector",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload missing support collector",
        )
        write_test_file(support_script, b"#!/bin/sh\nexit 0\n", 0o755)
        expect_runtime_error(
            "support collector is missing required marker",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with placeholder support collector",
        )
        write_test_file(support_script, make_validation_support_info_script_bytes(validator).replace(b"\n", b"\r\n"), 0o755)
        expect_runtime_error(
            "CRLF or carriage returns",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with CRLF support collector",
        )
        write_test_file(support_script, make_validation_support_info_script_bytes(validator) + b"printenv\n", 0o755)
        expect_runtime_error(
            "forbidden privacy/no-launch pattern",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with privacy-unsafe support collector",
        )
        write_test_file(support_script, make_validation_support_info_script_bytes(validator), 0o755)

        support_script_target = install_root / "collect_macos_support_info.real.sh"
        write_test_file(support_script_target, make_validation_support_info_script_bytes(validator), 0o755)
        support_script.unlink()
        try:
            support_script.symlink_to(support_script_target.name)
        except (OSError, NotImplementedError):
            write_test_file(support_script, make_validation_support_info_script_bytes(validator), 0o755)
        else:
            expect_runtime_error(
                "Staged payload contains symlink entries",
                lambda: validator.validate_macos_staged_metadata(
                    ROOT,
                    install_root,
                    game_dir,
                    [client],
                    [dedicated],
                ),
                "macOS staged payload with symlinked support collector",
            )
            support_script.unlink()
            write_test_file(support_script, make_validation_support_info_script_bytes(validator), 0o755)
        support_script_target.unlink()

        bad_module = game_dir / f"game-sp_{arch}.so"
        write_test_file(bad_module, b"wrong\n")
        expect_runtime_error(
            "non-dylib game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with Linux game module",
        )
        bad_module.unlink()

        if os.name != "nt":
            os.chmod(mp_module, 0o644)
            expect_runtime_error(
                "not executable",
                lambda: validator.validate_macos_staged_metadata(
                    ROOT,
                    install_root,
                    game_dir,
                    [client],
                    [dedicated],
                ),
                "macOS staged payload with non-executable game module",
            )
            os.chmod(mp_module, 0o755)

        bad_dsym = install_root / f"openQ4-client_{arch}.dSYM"
        bad_dsym.mkdir()
        expect_runtime_error(
            "non-runtime artifacts",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with debug symbol bundle",
        )
        shutil.rmtree(bad_dsym)

        bad_metadata = install_root / ".DS_Store"
        write_test_file(bad_metadata, b"finder\n")
        expect_runtime_error(
            "non-runtime artifacts",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with Finder metadata",
        )
        bad_metadata.unlink()

        bad_appledouble = game_dir / "._pak0.pk4"
        write_test_file(bad_appledouble, b"appledouble\n")
        expect_runtime_error(
            "non-runtime artifacts",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with AppleDouble metadata",
        )
        bad_appledouble.unlink()

        mp_module.unlink()
        expect_runtime_error(
            "architecture-matched game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload missing matched MP module",
        )
        write_test_file(mp_module, b"mp\n", 0o755)

        openal_runtime.unlink()
        expect_runtime_error(
            "missing executable OpenAL Soft runtime",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
                openal_provider="system",
            ),
            "macOS OpenAL Soft staged payload without its runtime",
        )
        validator.validate_macos_staged_metadata(
            ROOT,
            install_root,
            game_dir,
            [client],
            [dedicated],
            openal_provider="apple_framework",
        )

        build_dir = work / "builddir"
        write_test_file(
            build_dir / "meson-info" / "intro-buildoptions.json",
            json.dumps([{"name": "macos_openal_provider", "value": "apple_framework"}]).encode(),
        )
        if validator.selected_macos_openal_provider(build_dir) != "apple_framework":
            raise AssertionError("macOS staged validation did not preserve the selected Apple OpenAL provider")
    finally:
        validator.macos_lipo_arches = real_macos_lipo_arches
        shutil.rmtree(work, ignore_errors=True)


def validate_meson_contract() -> None:
    options = read("meson_options.txt")
    meson = read("meson.build")
    baseoq4_meson = read("content/baseoq4/meson.build")
    setup_sh = read("tools/build/meson_setup.sh")
    setup_ps1 = read("tools/build/meson_setup.ps1")

    require(options, "'macos_graphics_bridge'", "Meson options")
    require(options, "choices: ['opengl', 'metal']", "Meson options")
    require(options, "Metal-ready SDL3/Cocoa integration surface", "Meson option description")
    require(options, "'macos_openal_provider'", "Meson options")
    require(options, "choices: ['apple_framework', 'system']", "Meson options")
    require(options, "OpenAL Soft-style AL/... headers", "Meson option description")

    require(meson, "macos_graphics_bridge = get_option('macos_graphics_bridge')", "Meson bridge option")
    require(meson, "macos_openal_provider = get_option('macos_openal_provider')", "Meson OpenAL provider option")
    require(meson, "macos_graphics_bridge != 'opengl'", "non-macOS bridge guard")
    require(meson, "macos_openal_provider != 'apple_framework'", "non-macOS OpenAL provider guard")
    require(meson, "macos_graphics_bridge == 'metal' and platform_backend_requested != 'sdl3'", "SDL3 bridge guard")
    require(meson, "use_macos_metal_bridge", "Metal bridge build predicate")
    require(meson, "dependency('appleframeworks', modules: ['OpenAL'], required: true)", "macOS Apple OpenAL provider")
    require(meson, "dependency('openal', required: true, method: 'pkg-config')", "macOS system OpenAL provider")
    require(meson, "-DUSE_OPENAL_SOFT_INCLUDES=1", "macOS system OpenAL include mode")
    require(meson, "modules: ['Metal', 'QuartzCore']", "Metal bridge framework dependency")
    require(meson, "macos_framework_modules = ['Cocoa', 'OpenGL', 'ApplicationServices']", "macOS SDL3 framework list")
    require(meson, "if not use_sdl3_backend\n      macos_framework_modules += ['Carbon']", "native macOS Carbon isolation")
    require(meson, "modules: macos_framework_modules", "macOS conditional framework dependency")
    reject(meson, "modules: ['Cocoa', 'OpenGL', 'ApplicationServices', 'Carbon']", "macOS SDL3 Carbon isolation")
    require(meson, "-DOPENQ4_MACOS_METAL_BRIDGE=1", "Metal bridge compile define")
    require(meson, "shared_objcpp_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']", "Metal bridge ObjC++ compile define")
    require(meson, "shared_objc_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']", "Metal bridge ObjC compile define")
    require(meson, "-fstack-protector-strong", "macOS compile hardening")
    require(meson, "-D_FORTIFY_SOURCE=2", "macOS compile hardening")
    require(meson, "-Wl,-pie", "macOS executable hardening")
    require(meson, "-Wl,-dead_strip", "macOS link hardening")
    require(meson, "'macOS graphics bridge': macos_graphics_bridge", "Meson summary")
    require(meson, "'macOS OpenAL provider': macos_openal_provider", "Meson summary")

    require(baseoq4_meson, "elif host_system == 'darwin'", "macOS game module source branch")
    require(baseoq4_meson, "shared_library(\n      game_sp_binary_name,", "macOS SP game module dylib target type")
    require(baseoq4_meson, "shared_library(\n      game_mp_binary_name,", "macOS MP game module dylib target type")
    require(baseoq4_meson, "name_suffix: 'dylib'", "macOS game module dylib suffix")
    require(baseoq4_meson, "-Wl,-install_name,@loader_path/", "macOS game module install name")

    sdl3_vendored_meson = read("subprojects/packagefiles/sdl3/meson.build")
    sdl3_audio_meson = read("subprojects/packagefiles/sdl3/src/audio/meson.build")
    require_before(
        sdl3_vendored_meson,
        "elif host_machine.system() == 'darwin'",
        "cdata.set('SDL_VIDEO_RENDER_METAL', 1)",
        "vendored SDL3 Darwin Metal render driver",
    )
    require(sdl3_vendored_meson, "'AudioToolbox'", "vendored SDL3 Darwin AudioQueue framework")
    require_before(
        sdl3_audio_meson,
        "elif host_machine.system() == 'darwin'",
        "cdata.set('SDL_AUDIO_DRIVER_COREAUDIO', 1)",
        "vendored SDL3 Darwin CoreAudio driver",
    )

    require(setup_sh, "macos_graphics_bridge", "Bash Meson wrapper option preservation")
    require(setup_sh, "macos_openal_provider", "Bash Meson wrapper OpenAL provider preservation")
    require(setup_sh, "configure_macos_deployment_target()", "Bash Meson wrapper macOS deployment-target setup")
    require(setup_sh, 'export MACOSX_DEPLOYMENT_TARGET=11.0', "Bash Meson wrapper default macOS deployment target")
    require(setup_sh, 'MACOSX_DEPLOYMENT_TARGET must be a dotted macOS version', "Bash Meson wrapper deployment-target validation")
    require_before(setup_sh, "configure_macos_deployment_target", "resolve_meson_cmd", "Bash Meson wrapper deployment target precedes Meson/subproject execution")
    require(setup_ps1, '"macos_graphics_bridge"', "PowerShell Meson wrapper option preservation")
    require(setup_ps1, '"macos_openal_provider"', "PowerShell Meson wrapper OpenAL provider preservation")


def validate_sdl3_runtime_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    # Phase B5b: GLimp_Init lives in the renderer-owned context seam TU and
    # reaches the window layer through renderWindowServices_t
    context_source = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    syscon = read("src/sys/posix/posix_syscon.cpp")
    hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    summary = function_body(source, "static void SDL3_PrintGraphicsBridgeSummary(void) {")
    prepare = function_body(source, "static bool SDL3_WindowServices_PrepareWindowSystem(void) {")
    init = function_body(context_source, "bool GLimp_Init(glimpParms_t parms) {")
    hint_wrapper = function_body(source, "void Sys_SDL_ApplyVideoHintDefaults(void) {")
    support_renderer = function_body(syscon, "static SDL_Renderer *Posix_CreateSupportRenderer( SDL_Window *window, const char *purpose ) {")
    console_create = function_body(syscon, "static bool Posix_ConsoleCreateWindow( void ) {")
    console_presentation = function_body(syscon, "static void Posix_ConsoleApplyLogicalPresentation( int width, int height ) {")
    console_coordinates = function_body(syscon, "static void Posix_ConsoleWindowToRenderCoordinates( float &x, float &y ) {")
    console_click = function_body(syscon, "static void Posix_ConsoleClickButton( float x, float y ) {")
    splash_create = function_body(syscon, "void Sys_ShowSplash( void ) {")
    splash_drain = function_body(syscon, "static void Posix_SplashDrainEvents( SDL_WindowID windowID ) {")
    splash_ensure = function_body(syscon, "static bool Posix_SplashEnsureVideo( void ) {")
    console_ensure = function_body(syscon, "static bool Posix_ConsoleEnsureVideo( void ) {")

    require(source, "OPENQ4_MACOS_METAL_BRIDGE", "SDL3 Metal bridge compile guard")
    require(source, "SDL3_IsMacOSMetalBridge", "SDL3 Metal bridge predicate")
    require(source, "macOS Metal bridge (SDL3/Cocoa host, OpenGL renderer compatibility path)", "SDL3 bridge description")
    require(source, "static void SDL3_SetHintDefaultLogged", "SDL3 Metal bridge logged hint helper")
    require(source, "failed to set %s hint", "SDL3 Metal bridge logged hint failure")

    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_DRIVER", "macOS Metal bridge SDL video driver hint")
    require(hints, '"cocoa"', "macOS Metal bridge SDL video driver hint")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_RENDER_DRIVER", "macOS Metal bridge render hint")
    require(hints, '"metal,gpu,software"', "macOS Metal bridge render-driver fallback list")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_GPU_DRIVER", "macOS Metal bridge GPU hint")
    require(hints, "SDL3_SetHintDefaultLogged(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "macOS Metal bridge drawable hint")

    require(summary, "no native Metal renderer rewrite is selected", "SDL3 Metal bridge log")
    require(summary, "SDL_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "SDL3 Metal bridge hint log")
    require(init, "PrepareWindowSystem()", "SDL3 GL initialization window bring-up")
    require(prepare, "SDL3_PrintGraphicsBridgeSummary();", "SDL3 GL initialization bridge summary")

    require(hint_wrapper, "SDL3_SetVideoHintDefaults();", "early-startup SDL video hint wrapper")
    require_before(splash_ensure, "Sys_SDL_ApplyVideoHintDefaults();", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "splash hint defaults before SDL video init")
    require_before(console_ensure, "Sys_SDL_ApplyVideoHintDefaults();", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "console hint defaults before SDL video init")

    require(support_renderer, "OPENQ4_MACOS_METAL_BRIDGE", "Metal bridge support-window renderer path")
    require(support_renderer, "SDL_CreateRenderer( window, NULL )", "Metal bridge support-window default renderer")
    require(support_renderer, "SDL_GetRendererName( renderer )", "Metal bridge support-window created-driver probe")
    require(support_renderer, "macOS Metal bridge created '%s' driver", "Metal bridge support-window created-driver log")
    require(support_renderer, "falling back to software", "Metal bridge support-window renderer fallback")
    require(support_renderer, 'SDL_CreateRenderer( window, "software" )', "Metal bridge support-window software fallback")
    require(console_create, 'Posix_CreateSupportRenderer( s_consoleWindow.window, "system console" )', "Metal bridge system console renderer")
    require(console_create, "Posix_ConsoleUpdateLayout();", "Metal bridge system console initial HiDPI layout")
    require(console_presentation, "SDL_SetRenderLogicalPresentation(", "Retina system console logical presentation")
    require(console_presentation, "SDL_LOGICAL_PRESENTATION_STRETCH", "Retina system console full-window presentation")
    require(console_presentation, "s_consoleWindow.logicalWidth = width;", "Retina system console presentation cache")
    require(console_coordinates, "SDL_RenderCoordinatesFromWindow(", "Retina system console pointer conversion")
    require(console_click, "Posix_ConsoleWindowToRenderCoordinates( x, y );", "Retina system console clickable controls")
    require(splash_create, 'Posix_CreateSupportRenderer( s_splashWindow.window, "splash" )', "Metal bridge splash renderer")
    require_before(console_create, "if ( s_consoleWindow.windowID == 0 )", 'Posix_CreateSupportRenderer( s_consoleWindow.window, "system console" )', "Metal bridge system console window-id guard")
    require_before(splash_create, "if ( s_splashWindow.windowID == 0 )", 'Posix_CreateSupportRenderer( s_splashWindow.window, "splash" )', "Metal bridge splash window-id guard")
    require(splash_drain, "if ( !SDL_PushEvent( &event ) ) {", "Metal bridge splash event requeue failure guard")
    require(splash_drain, "failed to requeue non-splash event", "Metal bridge splash event requeue failure guard")
    reject(console_create, 'SDL_CreateRenderer( s_consoleWindow.window, "software" )', "Metal bridge system console hard-coded software renderer")
    reject(splash_create, 'SDL_CreateRenderer( s_splashWindow.window, "software" )', "Metal bridge splash hard-coded software renderer")


def validate_packaging_and_release_contract() -> None:
    package = read("tools/build/package_nightly.py")
    pak_helper = read("tools/build/openq4_pak.py")
    plist = read("src/sys/osx/Info.plist")
    release = read(".github/workflows/manual-release.yml")
    compat = read("src/sys/osx/macosx_compat.mm")
    main = read("src/sys/osx/macosx_sdl3_main.cpp")
    posix = read("src/sys/posix/posix_main.cpp")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    create_macos_dmg_body = python_function_body(package, "def create_macos_dmg(")
    notarize_macos_app_body = python_function_body(package, "def notarize_macos_app_bundle(")

    require(package, "--package-suffix", "release packaging variant suffix")
    require(package, "normalize_package_suffix", "release packaging variant suffix")
    require(package, '"macos": "dmg"', "macOS DMG default archive format")
    require(package, '"dmg": ".dmg"', "macOS DMG archive suffix")
    require(package, "import stat", "macOS archive symlink validation")
    require(package, "import subprocess", "macOS binary dependency validation")
    require(package, "shutil.copy2(client_binary, app_executable)", "macOS app executable creation")
    require(package, "macos_codesign_target(codesign_path, target, config)", "macOS standalone client signing")
    require(package, "include_entitlements=False", "macOS nested-code entitlement isolation")
    reject(package, "if target == client_binary", "macOS standalone client signing")
    reject(package, "shutil.copy2(app_executable, client_binary)", "macOS signed app executable recopy")
    require(package, "get_package_executable_archive_paths", "POSIX archive executable mode preservation")
    require(package, "DETERMINISTIC_ARCHIVE_TIMESTAMP", "POSIX archive deterministic metadata")
    require(package, "ZipInfo(arcname, date_time=DETERMINISTIC_ARCHIVE_TIMESTAMP)", "POSIX archive deterministic metadata")
    require(package, "mode = 0o100755", "POSIX archive executable mode preservation")
    require(package, "info.mode = 0o755", "POSIX archive executable mode preservation")
    require(package, "create_macos_dmg", "macOS DMG creation")
    require(package, "hdiutil_path", "macOS DMG hdiutil lookup")
    require(package, '"-format",\n            "UDZO"', "macOS compressed DMG format")
    require(package, "validate_macos_dmg_image", "macOS DMG validation")
    require(package, "notarize_macos_dmg_image", "macOS DMG notarization")
    require(package, "archive format 'dmg' is only supported for macOS packages", "macOS DMG platform guard")
    require(package, "MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES", "macOS binary dependency validation")
    require(package, "macos_otool_dependencies", "macOS binary dependency validation")
    require(package, "macos_otool_install_name", "macOS game module install-name validation")
    require(package, "normalize_macos_loadable_module_install_names", "macOS loadable module install-name normalization")
    require(package, "install_name_tool", "macOS game module install-name normalization")
    require(package, "setting macOS loadable module install name", "macOS loadable module install-name normalization")
    require_before(
        package,
        "normalize_macos_loadable_module_install_names(package_root, args.arch)",
        "sign_macos_payload(package_root, args.arch, macos_signing)",
        "macOS install-name normalization before signing",
    )
    require(package, "validate_macos_binary_dependencies", "macOS binary dependency validation")
    require(package, "otool_path, \"-L\"", "macOS binary dependency validation")
    require(package, "otool_path, \"-D\"", "macOS game module install-name validation")
    require(package, "macOS binary has unbundled non-system dependencies", "macOS binary dependency validation")
    require(package, "macOS loadable module install name is not package-relative", "macOS loadable module install-name validation")
    require(package, "MACOS_FORBIDDEN_XATTRS", "macOS package quarantine validation")
    require(package, "strip_macos_forbidden_xattrs", "macOS package quarantine validation")
    require(package, "validate_no_macos_forbidden_xattrs", "macOS package quarantine validation")
    require(package, "MACOS_FORBIDDEN_ARCHIVE_NAMES", "macOS archive metadata validation")
    require(package, "is_macos_metadata_sidecar_path", "macOS metadata sidecar validation")
    require(package, "MACOS_PKGINFO_BYTES", "macOS app PkgInfo validation")
    require(package, "MACOS_LOCALIZED_INFO_LOCALES", "macOS localized plist validation")
    require(package, "MACOS_EXPECTED_APP_BUNDLE_DIRS", "macOS app bundle directory allowlist")
    require(package, "MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_DIRS", "macOS app signature directory allowlist")
    require(package, "MACOS_EXPECTED_APP_BUNDLE_FILES", "macOS app bundle allowlist")
    require(package, "MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_FILES", "macOS app signature file allowlist")
    require(package, "macOS app bundle contains unexpected files", "macOS app bundle allowlist validation")
    require(package, "macOS app bundle contains unexpected directories", "macOS app bundle directory allowlist validation")
    require(package, "validate_macos_code_resources_bytes", "macOS app code-signature resource validation")
    require(package, "macOS app code signature resources", "macOS app code-signature resource validation")
    require(package, "macOS archive app bundle contains unexpected entries", "macOS archive app bundle allowlist validation")
    require(package, "macOS archive code signature resources", "macOS archive code-signature resource validation")
    require(package, "MAX_MACOS_METADATA_MEMBER_BYTES", "macOS metadata size validation")
    require(package, "MACOS_LIPO_ARCHES", "macOS binary architecture validation")
    require(package, "validate_macos_package_file_modes", "macOS package mode validation")
    require(package, "validate_no_package_symlinks", "macOS package symlink validation")
    require(package, "validate_no_package_special_files", "macOS package special-file validation")
    require(package, "prepare_archive_output_path", "macOS archive output preflight")
    require(package, "validate_archive_output_outside_input_tree", "macOS archive output containment preflight")
    require(package, "must not be inside the archive input tree", "macOS archive output containment preflight")
    require(package, "macOS DMG output", "macOS DMG output preflight")
    require(package, "macOS notarization archive output", "macOS notarization archive output preflight")
    require(package, "validate_macos_dmg_source_tree", "macOS DMG source preflight")
    require(package, "validate_macos_package_support_collector", "macOS DMG source support collector preflight")
    require(package, "macOS package support collector is missing", "macOS DMG source support collector preflight")
    require(package, "macOS package support collector is not executable", "macOS DMG source support collector preflight")
    require(package, "except (FileNotFoundError, RuntimeError) as exc", "macOS collateral packaging error handling")
    require_before(
        create_macos_dmg_body,
        'prepare_archive_output_path(archive_path, "macOS DMG output")',
        'hdiutil_path = shutil.which("hdiutil")',
        "macOS DMG output preflight before hdiutil",
    )
    require_before(
        notarize_macos_app_body,
        'prepare_archive_output_path(notary_archive, "macOS notarization archive output")',
        '["ditto", "-c", "-k", "--keepParent"',
        "macOS notarization archive output preflight before ditto",
    )
    require_before(
        create_macos_dmg_body,
        "validate_macos_dmg_source_tree(package_root)",
        "hdiutil_path = shutil.which(\"hdiutil\")",
        "macOS DMG source preflight before hdiutil",
    )
    require_before(
        notarize_macos_app_body,
        "validate_macos_dmg_source_tree(package_root)",
        "notary_archive = package_root.parent",
        "macOS notarization source preflight before notary archive creation",
    )
    require(pak_helper, "refusing to package symlink", "macOS PK4 symlink validation")
    require(package, "macOS archive contains non-regular entry", "macOS archive special-file validation")
    require(package, "validate_version_manifest_bytes", "macOS version manifest validation")
    require(package, "validate_macos_version_manifests", "macOS version manifest validation")
    require(package, "validate_macos_localized_info_bytes", "macOS localized plist validation")
    require(package, "parse_macos_localized_info_strings", "macOS localized plist parsing")
    require(package, "contains duplicate key", "macOS localized plist duplicate validation")
    require(package, "validate_macos_archive_metadata_member_size", "macOS archive metadata size validation")
    require(package, "macOS archive metadata member is too large", "macOS archive metadata size validation")
    require(package, "validate_macos_support_info_script_bytes", "macOS archive support collector validation")
    require(package, "MACOS_SUPPORT_INFO_REQUIRED_TOKENS", "macOS archive support collector validation")
    require(package, "MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS", "macOS archive support collector privacy validation")
    require(package, "forbidden privacy/no-launch pattern", "macOS archive support collector privacy validation")
    require(package, "MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES", "macOS archive support collector size validation")
    require(package, "macOS archive support collector", "macOS archive support collector validation")
    require(package, "does not launch openQ4", "macOS archive support collector no-launch validation")
    require(package, "does not copy retail q4base PK4 assets", "macOS archive support collector privacy validation")
    require(package, "MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS", "macOS runtime archive member-count validation")
    require(package, "MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS", "macOS symbol archive member-count validation")
    require(package, "MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES", "macOS runtime archive total-size validation")
    require(package, "MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES", "macOS symbol archive total-size validation")
    require(package, "macOS archive contains too many members", "macOS runtime archive member-count validation")
    require(package, "macOS symbol archive contains too many members", "macOS symbol archive member-count validation")
    require(package, "macOS archive total expanded size is too large", "macOS runtime archive total-size validation")
    require(package, "macOS symbol archive total expanded size is too large", "macOS symbol archive total-size validation")
    require(package, "require_non_empty_package_file", "macOS app non-empty metadata validation")
    require(package, "macOS app icon source was not found", "macOS app icon source validation")
    require(package, "validate_macos_localized_info_files", "macOS localized plist validation")
    require(package, "macos_package_version_tag_from_name", "macOS archive version manifest validation")
    require(package, "macos_package_suffix_from_name", "macOS archive package-suffix validation")
    require(package, "validate_macos_archive_name", "macOS archive path validation")
    require(package, "validate_macos_manifest_archive_filename", "macOS symbol manifest archive filename validation")
    require(package, "prepare_macos_symbol_staging_root", "macOS symbol staging preflight")
    require(package, "prepare_macos_dsym_output_path", "macOS dSYM output preflight")
    require(package, "macOS symbol staging root must not be a symlink", "macOS symbol staging symlink guard")
    require(package, "macOS symbol staging root exists but is not a directory", "macOS symbol staging file guard")
    require(package, "macOS dSYM output must not be a symlink", "macOS dSYM output symlink guard")
    require(package, "macOS dSYM output exists but is not a directory", "macOS dSYM output file guard")
    require(package, "macOS symbol archive input root must not be a symlink", "macOS symbol archive input root guard")
    require(package, "macOS symbol archive output must not be inside the symbol staging root", "macOS symbol archive self-include guard")
    require(package, "macOS symbol archive is not a valid xz-compressed tar archive", "macOS symbol archive exact compression validation")
    require(package, "ord(character) < 32", "macOS archive control-character path validation")
    require(package, "validate_macos_archive_mode", "macOS archive mode validation")
    require(package, "record_macos_archive_entry", "macOS archive duplicate validation")
    require(package, "contains duplicate key", "macOS symbol manifest duplicate key validation")
    require(package, "contains unexpected header key", "macOS symbol manifest unknown key validation")
    require(package, "contains unsafe archive filename", "macOS symbol manifest archive filename validation")
    require(package, "package_suffix", "macOS symbol manifest package-suffix validation")
    require(package, "runtime_archive", "macOS symbol manifest runtime archive validation")
    require(package, "runtime_archive_name=archive_path.name", "macOS runtime archive exact manifest validation")
    require(package, "symbol_archive", "macOS symbol manifest symbol archive validation")
    require(package, "contains duplicate binary entry", "macOS symbol manifest duplicate binary validation")
    require(package, "contains unexpected binary entries", "macOS symbol manifest unexpected binary validation")
    require(package, "is missing binary entries", "macOS symbol manifest missing binary validation")
    require(package, "has invalid sha256", "macOS symbol manifest hash validation")
    require(package, "has invalid size", "macOS symbol manifest size validation")
    require(package, "has invalid macho_uuid", "macOS symbol manifest Mach-O UUID validation")
    require(package, "dsym is", "macOS symbol manifest dSYM mapping validation")
    require(package, "validate_no_macos_casefold_path_collisions", "macOS package case-collision validation")
    require(package, "case-insensitive duplicate", "macOS archive case-collision validation")
    require(package, "info.is_dir()", "macOS zip directory-entry validation")
    require(package, "release archive input contains symlink entries", "macOS package symlink validation")
    require(package, "macOS archive contains duplicate entry", "macOS archive duplicate validation")
    require(package, "macOS archive entry is group/other writable", "macOS archive mode validation")
    require(package, "macOS archive contains symlink entry", "macOS archive symlink validation")
    require(package, "validate_macos_package_root_engine_binaries", "macOS package root stale engine binary validation")
    require(package, "is_macos_root_engine_binary_name", "macOS package root stale engine binary validation")
    require(package, "macOS package contains stale or mismatched root engine binaries", "macOS package root stale engine binary validation")
    require(package, "macOS archive contains stale or mismatched game modules", "macOS archive stale module validation")
    require(package, "macOS archive contains stale or mismatched root engine binaries", "macOS archive stale root engine binary validation")
    require(package, "wrong-platform entries", "macOS archive wrong-platform module validation")
    require(package, "macOS archive version manifests are unreadable", "macOS archive version manifest validation")
    require(package, "macos_lipo_arches", "macOS binary architecture validation")
    require(package, "validate_macos_binary_architectures", "macOS binary architecture validation")
    require(package, "lipo_path, \"-archs\"", "macOS binary architecture validation")
    require(package, "MACOS_SIGNING_MODES", "macOS signing mode validation")
    require(package, "class MacOSSigningConfig", "macOS signing config")
    require(package, "--macos-signing-mode", "macOS signing CLI")
    require(package, "--macos-code-sign-identity", "macOS Developer ID signing CLI")
    require(package, "--macos-entitlements", "macOS signing entitlements CLI")
    require(package, "--macos-notarize", "macOS notarization CLI")
    require(package, "MACOS_FORBIDDEN_ENTITLEMENTS", "macOS entitlements policy")
    require(package, "validate_macos_entitlements_file", "macOS entitlements validation")
    require(package, "com.apple.security.app-sandbox", "macOS App Sandbox entitlement policy")
    require(package, "com.apple.security.get-task-allow", "macOS debug entitlement policy")
    require(package, "resolve_macos_signing_config", "macOS signing config")
    require(package, "sign_macos_payload", "macOS shared signing")
    require(package, "ad_hoc_sign_macos_payload", "macOS ad-hoc signing")
    require(package, "verify_macos_codesignature", "macOS code-sign validation")
    require(package, "verify_macos_developer_id_signature", "macOS Developer ID validation")
    require(package, "notarize_macos_app_bundle", "macOS notarization")
    require(package, "\"--force\"", "macOS code-sign force option")
    require(package, "\"--timestamp=none\"", "macOS ad-hoc signing timestamp suppression")
    require(package, "\"--options\", \"runtime\"", "macOS Hardened Runtime signing")
    require(package, "\"Authority=Developer ID Application:\"", "macOS Developer ID validation")
    require(package, "\"Runtime Version=\"", "macOS Hardened Runtime validation")
    require(package, "\"xcrun\",\n        \"notarytool\",\n        \"submit\"", "macOS notarytool submission")
    require(package, "\"xcrun\", \"stapler\", \"staple\"", "macOS stapling")
    require(package, "\"spctl\", \"--assess\"", "macOS Gatekeeper assessment")
    require(package, "code signature verification", "macOS code-sign validation")
    require(package, "game-sp_{arch}.dylib", "macOS archive game module validation")
    require(package, "game-mp_{arch}.dylib", "macOS archive game module validation")
    require(package, "pak1.pk4", "macOS archive level pack validation")
    require(package, "macOS archive contains non-runtime metadata/debug entries", "macOS archive metadata validation")
    require(package, "MACOS_FORBIDDEN_ARCHIVE_PREFIXES", "macOS archive metadata validation")
    require(package, "MACOS_FORBIDDEN_ARCHIVE_SUFFIXES", "macOS archive metadata validation")
    require(package, "validate_no_macos_metadata_artifacts", "macOS package metadata validation")
    require(package, "__MACOSX", "macOS archive metadata validation")
    require(package, "._", "macOS archive metadata validation")
    require(package, "MACOS_EXPECTED_PLIST_VALUES", "macOS package Info.plist validation")
    require(package, "validate_macos_plist_values", "macOS package Info.plist validation")
    require(package, "must contain a dictionary root", "macOS package Info.plist root validation")
    require(package, "validate_macos_app_bundle", "macOS package app validation")
    require(package, "validate_macos_archive_contents", "macOS package archive validation")
    require(package, '"CFBundleDisplayName": "openQ4"', "macOS package Info.plist validation")
    require(package, '"CFBundleIconFile": "openQ4.icns"', "macOS package Info.plist validation")
    require(package, '"CFBundleName": "openQ4"', "macOS package Info.plist validation")
    require(package, '"LSApplicationCategoryType": "public.app-category.games"', "macOS package Info.plist validation")
    require(package, 'MACOS_RUNTIME_LAYOUT_VALUE = "self-contained-v1"', "macOS self-contained runtime layout marker")
    require(package, '("CFBundleShortVersionString", "CFBundleVersion")', "macOS package Info.plist validation")
    require(package, "openQ4.app/Contents/PkgInfo", "macOS package PkgInfo validation")
    require(package, "openQ4-ded_{arch}", "macOS package archive validation")
    require(package, "English.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "French.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "OpenQ4PackageRoot.strings", "macOS package localized package-root error validation")
    require(package, "write_macos_package_root_error_strings", "macOS package localized package-root error writer")
    require(package, "validate_macos_package_root_error_bytes", "macOS package localized package-root error validation")
    require(package, "macOS archive entry is not executable", "macOS package archive validation")
    require(package, "macOS archive contains unsafe or out-of-package path", "macOS package archive validation")
    require(package, "macOS archive Info.plist", "macOS package archive validation")
    require(package, "plistlib.loads", "macOS package Info.plist validation")
    require(package, "NSSupportsAutomaticGraphicsSwitching", "macOS package Info.plist validation")
    require(package, '"LSMinimumSystemVersion": "11.0"', "macOS package compatibility floor")
    require(package, '"NSPrincipalClass": "NSApplication"', "macOS package Cocoa app metadata")

    require(compat, "_NSGetExecutablePath", "macOS executable path resolution")
    require(compat, "Sys_CopyPathIfFits", "macOS executable path truncation guard")
    require(compat, "Sys_CopyExecutablePath", "macOS executable path resolution")
    require(compat, "malloc( bufferSize )", "macOS executable path long-buffer handling")
    require(compat, "realpath( pathBuffer, resolvedPath )", "macOS executable path canonicalization")
    require(compat, "Sys_PathIsSymlink", "macOS app-only package-root symlink guard")
    require(compat, "lstat( testPath.c_str(), &st )", "macOS app-only package-root symlink guard")
    require(compat, "return lstat( testPath.c_str(), &st ) != -1;", "macOS app-only mismatched-entry symlink diagnostic")
    require(compat, 'Sys_AppendMacOSPackageRootIssue( missingEntries, entry, "symlink" )', "macOS app-only package-root symlink diagnostic")
    require(compat, "!Sys_PathIsSymlink( path )", "macOS app-only executable symlink guard")
    require(compat, "Sys_DirectoryContainsGameDir", "macOS base path validation")
    require(compat, "BASE_GAMEDIR", "macOS base path validation")
    require(compat, "Sys_SelectMacOSAppBundleRuntimeRoots", "macOS app bundle runtime-root validation")
    require(compat, "Sys_MacOSAppBundleDeclaresSelfContainedRuntime", "macOS self-contained app plist runtime marker")
    require(compat, '@"OpenQ4RuntimeLayout"', "macOS self-contained app plist runtime marker")
    require(compat, "Sys_GetSiblingSelfContainedAppRuntimeRoots", "macOS loose-binary self-contained runtime discovery")
    require(compat, 'resourceDirectory.AppendPath( "Resources" )', "macOS self-contained app resource root")
    require(compat, 'frameworkDirectory.AppendPath( "Frameworks" )', "macOS self-contained app module root")
    require(compat, "\"/Contents/MacOS\"", "macOS app bundle base path validation")
    require(compat, "Sys_IsMacOSAppBundleDirectoryName", "macOS renamed app bundle base path validation")
    require(compat, 'static const char appBundleSuffix[] = ".app";', "macOS renamed app bundle base path validation")
    require(compat, "idStr::Icmp( suffixStart, appBundleSuffix )", "macOS renamed app bundle base path validation")
    reject(compat, 'appName.Icmp( "openQ4.app" )', "macOS renamed app bundle base path validation")
    require(compat, 'Sys_UseBasePathCandidate( packageRoot, "app runtime" )', "macOS app bundle base path validation")
    require(compat, "Sys_ErrorIfMacOSAppBundleRuntimeIncomplete", "macOS damaged self-contained app diagnostic")
    require(compat, "OpenQ4BundleRuntimeMissingTitle", "macOS self-contained app localized startup diagnostic")
    require(compat, "Expected self-contained app contract: data in Contents/Resources/baseoq4 and signed game modules in Contents/Frameworks", "macOS self-contained app contract diagnostic")
    require(compat, "Sys_ErrorIfMacOSAppBundlePackageRootIncomplete", "macOS app-only package-root startup diagnostic")
    require(compat, "OpenQ4PackageRootMissingTitle", "macOS app-only package-root localized startup diagnostic")
    require(compat, "Expected adjacent package-root contract: openQ4.app, loose binaries, and baseoq4/ together", "macOS app-only package-root contract diagnostic")
    require(compat, 'idStr::snPrintf( spModuleEntry, sizeof( spModuleEntry ), "%s/game-sp_%s.dylib", OPENQ4_GAMEDIR, arch )', "macOS app package-root expects baseoq4 SP module")
    require(compat, 'idStr::snPrintf( mpModuleEntry, sizeof( mpModuleEntry ), "%s/game-mp_%s.dylib", OPENQ4_GAMEDIR, arch )', "macOS app package-root expects baseoq4 MP module")
    require(compat, "Sys_RequireMacOSPackageRootDirectory( packageDirectory, OPENQ4_GAMEDIR, missingEntries )", "macOS app package-root requires baseoq4 runtime directory")
    require(compat, "Sys_AppendAlternateMacOSPackageRootGameModuleEntries", "macOS app package-root mismatched module diagnostics")
    require(compat, "Sys_AppendAlternateMacOSPackageRootGameModuleEntries( packageDirectory, BASE_GAMEDIR, expectedArch, foundEntries )", "macOS app package-root reports q4base misplaced dylibs")
    reject(compat, 'idStr::snPrintf( spModuleEntry, sizeof( spModuleEntry ), "%s/game-sp_%s.dylib", BASE_GAMEDIR, arch )', "macOS app package-root must not expect SP module under q4base")
    reject(compat, 'idStr::snPrintf( mpModuleEntry, sizeof( mpModuleEntry ), "%s/game-mp_%s.dylib", BASE_GAMEDIR, arch )', "macOS app package-root must not expect MP module under q4base")
    reject(compat, "Sys_RequireMacOSPackageRootDirectory( packageDirectory, BASE_GAMEDIR, missingEntries )", "macOS app package-root must not require q4base as runtime directory")
    require(main, "SDL_MAIN_HANDLED", "macOS SDL3 launch initialization")
    require(main, "static int SDLCALL OpenQ4_Main", "macOS SDL3 launch initialization")
    require(main, "SDL_RunApp(argc, argv, OpenQ4_Main, NULL)", "macOS SDL3 launch initialization")
    require(posix, "realpath( path, resolvedPath )", "macOS dylib path canonicalization")
    require(posix, "RTLD_NOW | RTLD_LOCAL", "macOS dylib local symbol scope")
    require(validator, "host_is_macos", "macOS staged payload validation")
    require(validator, "macos_binary_arch", "macOS staged payload validation")
    require(validator, "validate_macos_staged_metadata", "macOS staged payload validation")
    require(validator, "validate_macos_staged_root_engine_binaries", "macOS staged root engine binary validation")
    require(validator, "is_macos_root_engine_binary", "macOS staged root engine binary validation")
    require(validator, "macOS staged payload contains stale or mismatched root engine binaries", "macOS staged root engine binary validation")
    require(validator, "validate_macos_support_collector_script", "macOS staged support collector validation")
    require(validator, "MACOS_SUPPORT_INFO_SCRIPT_NAME", "macOS staged support collector validation")
    require(validator, "MACOS_SUPPORT_INFO_REQUIRED_TOKENS", "macOS staged support collector validation")
    require(validator, "MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS", "macOS staged support collector privacy validation")
    require(validator, "forbidden privacy/no-launch pattern", "macOS staged support collector privacy validation")
    require(validator, "macOS support collector contains CRLF or carriage returns", "macOS staged support collector line-ending validation")
    require(validator, "validate_no_macos_symlinks(root, install_root)", "macOS staged symlink validation")
    require(validator, "openQ4.icns", "macOS staged payload validation")
    require(validator, "non-dylib game modules", "macOS staged payload validation")
    require(validator, "architecture-matched game modules", "macOS staged payload validation")
    require(validator, "macOS staged game module is not executable", "macOS staged game module validation")
    require(validator, "MACOS_FORBIDDEN_XATTRS", "macOS staged quarantine validation")
    require(validator, "MACOS_NON_RUNTIME_PATTERNS", "macOS staged debug artifact validation")
    require(validator, ".DS_Store", "macOS staged metadata validation")
    require(validator, "._*", "macOS staged metadata validation")
    require(validator, "__MACOSX", "macOS staged metadata validation")
    require(validator, "validate_no_macos_casefold_path_collisions", "macOS staged case-collision validation")
    require(validator, "macOS staged payload contains case-insensitive duplicate paths", "macOS staged case-collision validation")
    require(validator, "MACOS_LIPO_ARCHES", "macOS staged architecture validation")
    require(validator, "validate_no_macos_symlinks", "macOS staged symlink validation")
    require(validator, "validate_no_macos_unsafe_file_modes", "macOS staged mode validation")
    require(validator, "Staged payload contains symlink entries", "cross-platform staged symlink validation")
    require(validator, "macOS staged payload contains unsafe file modes", "macOS staged mode validation")
    require(validator, "stale or mismatched game modules", "macOS staged stale module validation")
    require(validator, "validate_macos_binary_architectures", "macOS staged architecture validation")
    require(validator, "lipo_path, \"-archs\"", "macOS staged architecture validation")
    require(commit, "macOS ARM64 ${{ matrix.bridge_label }} Commit Validation", "commit validation macOS job")
    require(commit, "macos_graphics_bridge: opengl", "commit validation macOS OpenGL job")
    require(commit, "macos_graphics_bridge: metal", "commit validation macOS Metal job")
    require(commit, "macos_openal_provider: system", "commit validation macOS OpenAL Soft provider")
    require(commit, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "commit validation macOS bridge setup")
    require(commit, "--extra-setup-arg=-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "commit validation macOS OpenAL setup")
    require(commit, "runs-on: macos-15", "commit validation macOS job")
    require(commit, "bash tools/validation/validate_pr.sh \\", "commit validation macOS job")
    require(commit, "--fail-on-dirty \\", "commit validation macOS job")
    arm64_commit_job = commit[
        commit.index("\n  macos-arm64:") : commit.index("\n  macos-x64:")
    ]
    x64_commit_job = commit[
        commit.index("\n  macos-x64:") : commit.index("\n  macos-universal2:")
    ]
    thin_normalize = "python tools/build/assemble_macos_universal2.py prepare"
    moltenvk_stage = "bash tools/build/prepare_macos_moltenvk.sh --output-dir .install"
    moltenvk_verify = "bash tools/build/prepare_macos_moltenvk.sh --verify-only --output-dir .install"
    for job, normalize_step_name, arch_token, record_step, context in (
        (
            arm64_commit_job,
            "- name: Normalize ARM64 thin payload",
            "--arch arm64",
            "name: Record macOS ARM64 thin-build provenance",
            "ARM64 thin payload",
        ),
        (
            x64_commit_job,
            "- name: Normalize Intel thin payload",
            "--arch x64",
            "name: Record macOS Intel thin-build provenance",
            "Intel thin payload",
        ),
    ):
        if job.count(normalize_step_name) != 1:
            raise AssertionError(f"Expected exactly one {normalize_step_name!r} in {context}")
        normalize_start = job.index(normalize_step_name)
        normalize_end = job.index("\n      - name:", normalize_start + len(normalize_step_name))
        normalize_step = job[normalize_start:normalize_end]
        require(normalize_step, thin_normalize, f"{context} install-name normalization")
        require(normalize_step, "--install-root .install", f"{context} normalization install root")
        require(normalize_step, arch_token, f"{context} normalization architecture")
        reject(normalize_step, "\n        if:", f"{context} unconditional normalization")
        require(job, moltenvk_stage, f"{context} MoltenVK staging")
        require(job, moltenvk_verify, f"{context} MoltenVK verification")
        require_before(job, "name: Run selected validation profile", normalize_step_name, f"{context} build normalization order")
        require_before(job, normalize_step_name, "name: Run macOS dedicated server smoke", f"{context} smoke normalization order")
        require_before(job, normalize_step_name, record_step, f"{context} normalization order")
        require_before(job, moltenvk_stage, record_step, f"{context} MoltenVK stage order")
        require_before(job, moltenvk_verify, record_step, f"{context} MoltenVK verification order")
    require(push, "macOS OpenGL Push Verification", "push verification macOS OpenGL job")
    require(push, "macOS Metal Push Verification", "push verification macOS Metal job")
    if push.count("runtime_smoke: true") < 3:
        raise AssertionError("push verification must require runtime smoke for Linux ARM64 and both macOS bridge jobs")
    require(push, 'if [[ "${{ matrix.os }}" == ubuntu-* ]]', "push verification OS-specific runtime environment")
    require(push, 'bash tools/validation/validate_push.sh "${validation_args[@]}"', "macOS hosted runtime smoke invocation")
    require(push, "macos-opengl", "push verification macOS OpenGL artifact")
    require(push, "macos-metal", "push verification macOS Metal artifact")
    require(push, "macos_openal_provider: system", "push verification macOS OpenAL Soft provider")
    require(push, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "push verification macOS bridge setup")
    require(push, "--extra-setup-arg=-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "push verification macOS OpenAL setup")

    for source, context in ((package, "macOS package Info.plist"), (plist, "legacy macOS Info.plist")):
        require(source, "CFBundleDisplayName", context)
        require(source, "CFBundleName", context)
        require(source, "LSApplicationCategoryType", context)
        require(source, "NSHighResolutionCapable", context)
        require(source, "NSPrincipalClass", context)
        require(source, "NSSupportsAutomaticGraphicsSwitching", context)
        require(source, "LSMinimumSystemVersion", context)
        require(source, "11.0", context)

    require(plist, "<string>openQ4.icns</string>", "legacy macOS Info.plist icon")
    require(plist, "CFBundleShortVersionString", "legacy macOS Info.plist version")

    require(release, "macos_signed_release_enabled", "manual release credential-aware macOS matrix")
    require(release, "macOS unsigned/unnotarized tar.gz release artifacts enabled", "manual release macOS credential fallback")
    require(release, "fromJSON(needs.metadata.outputs.release_matrix)", "manual release dynamic matrix")
    require(release, "macOS ARM64 OpenGL{macos_unsigned_label}", "manual release macOS OpenGL matrix")
    require(release, '"macos_graphics_bridge": "opengl"', "manual release OpenGL bridge matrix")
    require(release, '"platform": "macos"', "manual release macOS platform matrix")
    require(release, '"macos_openal_provider": "system"', "manual release macOS OpenAL Soft provider")
    require(release, '"macos_release_mode": macos_release_mode', "manual release macOS release mode")
    require(release, '"package_suffix": f"-opengl{macos_unsigned_suffix}"', "manual release OpenGL package suffix")
    require(release, 'macos_archive_format = "dmg" if macos_signed_release_enabled else "tar.gz"', "manual release macOS archive format selection")
    require(release, 'macos_archive_ext = ".dmg" if macos_signed_release_enabled else ".tar.gz"', "manual release macOS archive extension selection")
    require(release, "macOS ARM64 Metal{macos_unsigned_label}", "manual release macOS Metal matrix")
    require(release, '"macos_graphics_bridge": "metal"', "manual release macOS matrix")
    require(release, '"package_suffix": f"-metal{macos_unsigned_suffix}"', "manual release Metal package suffix")
    require(release, "-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "manual release setup")
    require(release, "-Dmacos_openal_provider=${{ matrix.macos_openal_provider }}", "manual release OpenAL setup")
    # Meson never installs MoltenVK - openQ4 does not build it - so without an
    # explicit staging step resolve_macos_moltenvk_source aborts the macOS
    # package and the whole release skips publishing. The release lane went out
    # without this once already.
    require(
        release,
        "bash tools/build/prepare_macos_moltenvk.sh --output-dir .install",
        "manual release MoltenVK staging",
    )
    require_before(
        release,
        "bash tools/build/prepare_macos_moltenvk.sh --output-dir .install",
        "name: Prepare package",
        "manual release MoltenVK staged before packaging",
    )
    require(release, "Import macOS Developer ID certificate", "manual release Developer ID setup")
    require(release, "matrix.macos_release_mode == 'signed'", "manual release conditional Developer ID setup")
    require(release, "MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64", "manual release Developer ID certificate secret")
    require(release, "MACOS_DEVELOPER_ID_APPLICATION_IDENTITY", "manual release Developer ID identity secret")
    require(release, "MACOS_NOTARY_APP_PASSWORD", "manual release notarization secret")
    require(release, "security import", "manual release Developer ID certificate import")
    require(release, "xcrun notarytool store-credentials", "manual release notarytool profile")
    require(release, "--package-suffix=\"${{ matrix.package_suffix }}\"", "manual release packaging")
    require(release, "--macos-signing-mode developer-id", "manual release Developer ID package signing")
    require(release, "--macos-signing-mode ad-hoc", "manual release unsigned macOS package signing")
    require(release, "--macos-notarize", "manual release notarized package signing")
    reject(release, "--macos-entitlements", "manual release default entitlements policy")
    reject(release, 'cmp -s "${app_exec}" "${client_binary}"', "manual release macOS app validation")
    reject(release, "macOS app executable does not match the packaged client binary", "manual release macOS app validation")
    require(release, "Missing or invalid macOS app PkgInfo", "manual release macOS app validation")
    require(release, "Missing or non-executable macOS dedicated binary", "manual release macOS app validation")
    require(release, "Missing or non-executable macOS package game module", "manual release macOS app validation")
    require(release, "sp_module=", "manual release macOS dependency validation")
    require(release, "check_macos_binary_architecture", "manual release macOS architecture validation")
    require(release, "lipo -archs", "manual release macOS architecture validation")
    require(release, "check_macos_codesignature", "manual release macOS signature validation")
    require(release, "codesign --verify --strict", "manual release macOS signature validation")
    require(release, "check_macos_developer_id_signature", "manual release Developer ID signature validation")
    require(release, "check_macos_ad_hoc_signature", "manual release unsigned signature validation")
    require(release, "Authority=Developer ID Application:", "manual release Developer ID authority validation")
    require(release, "Runtime Version=", "manual release Hardened Runtime validation")
    require(release, "xcrun stapler validate", "manual release stapled notarization validation")
    require(release, "macOS unsigned release archive is ad-hoc signed and not notarized.", "manual release unsigned notarization notice")
    require(release, "hdiutil verify", "manual release DMG validation")
    require(release, "Smoke test macOS app-bundle path resolution", "manual release macOS app runtime smoke")
    require(release, "finder-style-cwd", "manual release Finder-style working-directory smoke")
    require(release, "+rendererDefaultSafetySelfTest", "manual release app renderer self-test")
    require(release, "smoke_exit_status", "manual release app exit-status capture")
    require(release, "smoke_console", "manual release app console-output capture")
    require(release, "require_smoke_output", "manual release app console-output validation")
    require(release, "Shutting down OpenGL subsystem (SDL3 backend)", "manual release app clean-shutdown validation")
    require(release, "macOS app-bundle smoke output contains a fatal startup diagnostic.", "manual release app fatal-diagnostic validation")
    require(release, "fs_cdpath='${package_dir}/openQ4.app/Contents/Resources'", "manual release embedded app-resource search validation")
    require(release, "hdiutil imageinfo", "manual release DMG validation")
    require(release, "spctl --assess --type execute", "manual release Gatekeeper validation")
    require(release, "check_macos_install_name", "manual release macOS install-name validation")
    require(release, "@loader_path/game-sp_${{ matrix.binary_arch }}.dylib", "manual release macOS install-name validation")
    require(release, "@loader_path/renderer-vk_${{ matrix.binary_arch }}.dylib", "manual release renderer install-name validation")
    require(release, "check_macos_binary_dependencies", "manual release macOS dependency validation")
    require(release, 'otool -arch "${macho_arch}" -L', "manual release macOS dependency validation")
    require(release, "unbundled non-system dependency", "manual release macOS dependency validation")
    require(release, "Missing macOS staged payload file", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-sp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-mp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, ".install/collect_macos_support_info.sh", "manual release macOS support collector staging")
    require(release, "Missing or non-executable macOS support collector", "manual release macOS support collector validation")
    require(release, "contains_control_chars()", "manual release macOS support collector path-control validation")
    require(release, "sanitize_text()", "manual release macOS support collector text sanitization validation")
    require(release, "limit_stream_tail()", "manual release macOS support collector stream-bounding validation")
    require(release, "Support report output is limited to the final", "manual release macOS support collector stream-bounding validation")
    require(release, ".XXXXXX.tar.gz.tmp", "manual release macOS support collector mktemp validation")
    require(release, "does not launch openQ4", "manual release macOS support collector no-launch validation")
    require(release, "does not copy retail q4base PK4 assets", "manual release macOS support collector privacy validation")
    require(release, "truncated copy failed; source was not copied", "manual release macOS support collector truncation validation")
    require(release, "forbidden privacy/no-launch pattern", "manual release macOS support collector forbidden-token validation")
    require(release, "openQ4-client_x64 >", "manual release macOS support collector x64 launch denial")
    require(release, "openQ4-ded_x64 >", "manual release macOS support collector x64 launch denial")
    require(release, "|| cat", "manual release macOS support collector fallback denial")
    require(release, 'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat', "manual release macOS support collector fallback denial")
    require(release, "macOS staged payload contains non-dylib game modules", "manual release macOS staged validation")
    require(release, "macOS staged payload contains stale or mismatched root engine binary", "manual release macOS stale root binary validation")
    require(release, "macOS staged payload contains stale or mismatched game module", "manual release macOS stale game-module validation")
    require(release, "check_plist_value CFBundleDisplayName openQ4", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleIconFile openQ4.icns", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleName openQ4", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleShortVersionString", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleVersion", "manual release macOS app validation")
    require(release, "check_plist_value LSMinimumSystemVersion 11.0", "manual release macOS app validation")
    require(release, "check_plist_value LSApplicationCategoryType public.app-category.games", "manual release macOS app validation")
    require(release, "check_plist_value NSPrincipalClass NSApplication", "manual release macOS app validation")
    require(release, "check_plist_value NSSupportsAutomaticGraphicsSwitching true", "manual release macOS app validation")
    require(release, "check_plist_value OpenQ4RuntimeLayout self-contained-v1", "manual release self-contained runtime marker validation")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl.dmg", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal.dmg", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-unsigned.tar.gz", "manual release unsigned expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-unsigned.tar.gz", "manual release unsigned expected assets")


def validate_macos_workflow_security_contract() -> None:
    host = read("tools/macos/Invoke-openQ4MacOSWorkflow.ps1")
    bootstrap = read("tools/macos/guest/openq4-macos-bootstrap.sh")
    assets = read("tools/macos/guest/openq4-macos-install-quake4-assets.sh")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    signoff_validator = read("tools/macos/validate_signoff_archive.py")
    debug = read(".github/workflows/macos-debug.yml")
    workflow_doc = read("docs/dev/macos-vm-testing-workflow.md")
    validation_runner = read("tools/validation/openq4_validate.py")

    require(host, "Assert-NoArchiveLinks", "macOS host archive symlink guard")
    require(host, "Assert-NoCaseInsensitiveArchiveCollisions", "macOS host archive case-collision guard")
    require(host, "Assert-NoMacOSMetadataEntries", "macOS host archive metadata guard")
    require(host, "Assert-ValidMacUser", "macOS host user validation")
    require(host, "Assert-ValidMacHost", "macOS host target validation")
    require(host, "Assert-SafeRemoteWorkflowPath", "macOS host remote path validation")
    require(host, "Assert-MacBasePathNotReservedWorkspaceChild", "macOS host reserved workspace child guard")
    require(host, "ConvertTo-RemoteWorkflowComparisonPath", "macOS host resolved remote path comparison")
    require(host, "$MacHome.TrimEnd", "macOS host resolved remote path comparison")
    require(host, "Normalize([System.Text.NormalizationForm]::FormC).ToLowerInvariant()", "macOS host normalized remote path comparison")
    require(host, "MacBasePath must not target reserved workflow directory", "macOS host reserved workspace child diagnostic")
    require(host, "[System.StringComparison]::OrdinalIgnoreCase", "macOS host reserved workspace child case-insensitive guard")
    require(host, "Join-RemotePosixPath", "macOS host remote child path joining")
    require(host, '[string]$MacHome = ""', "macOS host guest-home override parameter")
    require(host, "Assert-SafeRemoteGuestHomePath", "macOS host guest-home override validation")
    require(host, "Invalid MacHome", "macOS host guest-home override validation")
    require(host, "Use an absolute POSIX path such as /Users/codex", "macOS host guest-home override validation")
    require(host, "OPENQ4_GUEST_HOME=$(Quote-Sh $MacHome)", "macOS host guest-home override environment")
    require(host, "Use an absolute POSIX path or a ~/ path", "macOS host relative remote path rejection")
    require(host, "Empty POSIX path segments are not allowed", "macOS host empty remote path segment rejection")
    require(host, "Dot path segments are not allowed", "macOS host dot remote path segment rejection")
    require(host, "Assert-LocalRegularFile", "macOS host identity file validation")
    require(host, "Assert-LocalOutputFileTarget", "macOS host copy-back target validation")
    require(host, "target already exists; refusing to overwrite", "macOS host copy-back no-overwrite validation")
    require(host, "New-LocalOutputTempPath", "macOS host copy-back temporary publish")
    require(host, "Initialize-LocalDirectory", "macOS host local temp/result path validation")
    require(host, "MacBasePath must not be the same directory as MacWorkspace", "macOS host destructive asset target guard")
    require(host, 'Assert-LocalRegularFile -Path $Source -Label "macOS copy source"', "macOS host scp source validation")
    require(host, 'Assert-LocalOutputFileTarget -Path $destinationFullPath -Label "macOS copy destination" -MustNotExist', "macOS host scp destination validation")
    require(host, '[System.IO.File]::Move($temporaryDestination, $destinationFullPath)', "macOS host copy-back atomic publish")
    require(host, 'Assert-LocalRegularFile -Path $temporaryDestination -Label "macOS temporary copy destination"', "macOS host copy-back temporary validation")
    require(host, "macOS temporary copy destination is empty after scp", "macOS host copy-back empty archive validation")
    require(host, "Remove-Item -LiteralPath $temporaryDestination -Force", "macOS host copy-back temporary cleanup")
    require(host, 'Assert-LocalRegularFile -Path $localScript -Label "Guest script"', "macOS host guest script symlink validation")
    require(host, "Source path was not a directory", "macOS host archive source validation")
    require(host, "$sourceItem.Attributes", "macOS host archive root symlink validation")
    require(host, 'Initialize-LocalDirectory -Path $archiveParent -Label "macOS transfer archive directory"', "macOS host transfer archive parent validation")
    require(host, 'Assert-LocalOutputFileTarget -Path $archiveFullPath -Label "macOS transfer archive"', "macOS host transfer archive output validation")
    require(host, 'Initialize-LocalDirectory -Path $transferRoot -Label "macOS transfer root"', "macOS host transfer root validation")
    require(host, "Refusing to archive non-runtime macOS metadata/debug entries", "macOS host transfer metadata validation")
    require(host, "[ValidateRange(1, 65535)]", "macOS host SSH port validation")
    require(host, '[ValidateSet("plain", "debug", "debugoptimized", "release", "minsize", "custom")]', "macOS host build type validation")
    require(host, "[ValidateRange(1, 100000)]", "macOS host smoke limit validation")
    require(host, "Invalid BuildDir", "macOS host builddir validation")
    require(host, "Assert-SafeMacOSBuildDir", "macOS host builddir validation")
    require(host, "BuildDir must live under .tmp/ or use a builddir* name", "macOS host builddir output-tree guard")
    require(host, "RemoteTempRoot", "macOS host per-run remote temp root")
    require(host, "function Remove-RemoteTempRoot", "macOS host remote temp cleanup")
    require(host, r"\A/tmp/openq4-macos-[0-9a-fA-F]{32}\z", "macOS host remote temp cleanup path guard")
    require(host, "Refusing to remove unexpected macOS remote temp root", "macOS host remote temp cleanup path guard")
    require(host, "Failed to remove macOS remote temp root", "macOS host remote temp cleanup warning")
    require(host, "try {\n    Invoke-MacOSWorkflowMain\n} finally {\n    Remove-RemoteTempRoot", "macOS host remote temp cleanup finally")
    require(host, "BatchMode=yes", "macOS host non-interactive SSH")
    require(host, "expand_remote_path", "macOS host remote tilde expansion")
    require(host, "require_remote_home", "macOS host embedded remote home validation")
    require(host, "REMOTE_HOME", "macOS host embedded remote home validation")
    require(host, "guest_home_raw=__GUEST_HOME__", "macOS host result collection home placeholder")
    require(host, 'Replace("__GUEST_HOME__", $guestHomeQ)', "macOS host result collection home placeholder replacement")
    require(host, "MacHome/HOME must be an absolute POSIX path", "macOS host embedded remote home validation")
    require(host, "MacHome/HOME must not contain control characters", "macOS host embedded remote home validation")
    require(host, "grep -q '[[:cntrl:]]'", "macOS host embedded remote home control-character validation")
    require(host, "HOME must be set or MacHome must provide the macOS guest home directory", "macOS host embedded remote home validation")
    require(host, "target_raw", "macOS host remote target path expansion")
    require(host, "workspace_raw=__WORKSPACE__", "macOS host result workspace path expansion")
    require(host, "require_regular_remote_archive", "macOS host remote archive regular-file validation")
    require(host, "Remote archive must be a regular file", "macOS host remote archive symlink validation")
    require(host, "prepare_remote_extract_target", "macOS host remote extract target validation")
    require(host, "Remote extraction target must not be a symlink", "macOS host remote extract target symlink validation")
    require(host, "require_single_archive_root", "macOS host remote archive root validation")
    require(host, "Archive must contain exactly one top-level source root", "macOS host remote archive root validation")
    require(host, "reject_unsafe_remote_archive_entries", "macOS host structured archive validation")
    require(host, "tarfile.open", "macOS host structured archive validation")
    require(host, "Unable to inspect remote archive", "macOS host malformed archive validation")
    require(host, "Unsafe archive path", "macOS host archive path validation")
    require(host, "case-insensitive path collision", "macOS host archive case-collision validation")
    require(host, "Archive contains duplicate member", "macOS host duplicate archive validation")
    require(host, "Archive contains non-runtime macOS metadata/debug entry", "macOS host metadata archive validation")
    require(host, "Archive contains a symlink or special file", "macOS host archive special-file validation")
    require(host, "COPYFILE_DISABLE=1 tar -xf", "macOS host archive extraction copyfile suppression")
    require(host, "tar -tvf", "macOS host archive type validation")
    require(host, "including hardlinks", "macOS host archive hardlink validation")
    require(host, "rsync -a --delete", "macOS host safe temp extraction sync")
    require(host, 'Join-RemotePosixPath -Base $MacWorkspace -Child "openQ4"', "macOS host remote source sync path joining")
    require(host, 'Join-RemotePosixPath -Base $MacWorkspace -Child "openQ4-game"', "macOS host remote GameLibs sync path joining")
    require(host, "openQ4/.git", "macOS host source metadata exclusion")
    require(host, "openQ4/.codex", "macOS host local agent metadata exclusion")
    require(host, "openQ4-game/.git", "macOS host GameLibs metadata exclusion")
    require(host, "$assetRootName/q4base/*.cfg", "macOS host personal config exclusion")
    require(host, "$assetRootName/q4base/q4key", "macOS host private key exclusion")
    require(host, '"Signoff"', "macOS host signoff action")
    require(host, '"CollectResults"', "macOS host result collection action")
    require(host, '-ScriptAction "signoff"', "macOS host signoff action")
    require(host, '[string]$ResultCollectionDir', "macOS host result collection directory")
    require(host, '[string]$MacOSRunId', "macOS host result collection run ID")
    require(host, "$script:MacOSResultTokenMaxLength = 80", "macOS host result token length guard")
    require(host, '[switch]$SkipResultCollection', "macOS host result collection opt-out")
    require(host, '[switch]$SkipResultArchiveValidation', "macOS host result archive validation opt-out")
    require(host, '[switch]$RequireCompletedSignoffChecklist', "macOS host completed checklist validation")
    require(host, "function Copy-FromMac", "macOS host result copy helper")
    require(host, "function Collect-MacOSResults", "macOS host result collection helper")
    require(host, "function Test-MacOSResultArchive", "macOS host result archive validation helper")
    require(host, "reject_unsafe_result_tree", "macOS host result pre-archive tree validation")
    require(host, "require_result_file_under", "macOS host result evidence preflight")
    require(host, "reject_result_case_collisions", "macOS host result case-collision preflight")
    require(host, "Unsafe macOS workspace path for result collection", "macOS host result workspace validation")
    require(host, "Unsafe macOS result archive directory", "macOS host result archive directory validation")
    require(host, "Unsafe macOS result archive target", "macOS host result archive target validation")
    require(host, "macOS result archive target already exists", "macOS host result archive no-overwrite validation")
    require(host, "cleanup_result_archive_tmp", "macOS host remote result archive temporary cleanup")
    require(host, 'archive_tmp="$(mktemp "${archive_parent}/.${archive##*/}.XXXXXX.tmp")"', "macOS host remote result archive temporary publish")
    require(host, 'COPYFILE_DISABLE=1 tar -czf "${archive_tmp}"', "macOS host remote result archive temporary publish")
    require(host, "macOS result archive is empty before publish", "macOS host remote result archive empty validation")
    require(host, 'COPYFILE_DISABLE=1 tar -tzf "${archive_tmp}"', "macOS host remote result archive listing validation")
    require(host, "macOS result archive validation failed before publish", "macOS host remote result archive listing validation")
    require(host, 'ln "${archive_tmp}" "${archive}"', "macOS host remote result archive no-overwrite publish")
    require(host, "COPYFILE_DISABLE=1 tar -czf", "macOS host result tar copyfile suppression")
    require(host, "contains non-runtime metadata/debug entry", "macOS host result metadata preflight")
    require(host, "contains a symlink or special file", "macOS host result symlink/special preflight")
    require(host, "has no file evidence under", "macOS host result renderer-output preflight")
    require(host, 'require_result_file_under "${path}" "renderer-mp-smoke"', "macOS host result MP-smoke preflight")
    require(host, 'openq4-macos-results-${RunId}.tar.gz', "macOS host result archive naming")
    require(host, 'expected_bridges=(__BRIDGES__)', "macOS host bridge-specific result collection")
    require(host, '${run_id}-signoff-${bridge}', "macOS host signoff-only result collection")
    require(host, "macOS signoff result directory is incomplete", "macOS host incomplete signoff result guard")
    require(host, "No macOS result directories matched", "macOS host missing result guard")
    require(host, "-Action CollectResults requires -MacOSRunId <id>", "macOS host collect-results run ID guard")
    require(host, "$MacOSRunId.Length -gt $script:MacOSResultTokenMaxLength", "macOS host run ID length validation")
    require(host, "validate_signoff_archive.py", "macOS host result archive validator")
    require(host, '"--run-id"', "macOS host result archive validator run ID")
    require(host, '"--bridges"', "macOS host result archive validator bridge list")
    require(host, '"--require-completed-checklist"', "macOS host completed checklist validator flag")
    require(host, '[ValidateSet("opengl", "metal", "both")]', "macOS host dual bridge selection")
    require(host, '[ValidateSet("apple_framework", "system")]', "macOS host OpenAL provider selection")
    require(host, "function Get-MacOSGraphicsBridgeRuns", "macOS host bridge run expansion")
    require(host, 'return @("opengl", "metal")', "macOS host bridge run expansion")
    require(host, "function Get-MacOSBridgeBuildDir", "macOS host bridge-specific builddir")
    require(host, 'return "${BuildDir}-${Bridge}"', "macOS host bridge-specific builddir")
    require(host, "function Invoke-BuildTestActionForBridge", "macOS host bridge action environment")
    require(host, '"OPENQ4_MACOS_OPENAL_PROVIDER" = $MacOSOpenALProvider', "macOS host OpenAL provider environment")
    require(host, '"OPENQ4_MACOS_RUN_ID" = $MacOSRunId', "macOS host run ID environment")
    require(host, 'foreach ($bridge in (Get-MacOSGraphicsBridgeRuns))', "macOS host multi-bridge loop")
    require(host, "-MacOSGraphicsBridge both is supported for Build, Signoff, and All", "macOS host multi-bridge guard")
    require(host, "Collect-MacOSResults -RunId $MacOSRunId", "macOS host automatic signoff result collection")
    collect_results_index = host.find('"CollectResults" {')
    all_action_index = host.find('"All" {', collect_results_index)
    if collect_results_index == -1 or all_action_index == -1:
        raise AssertionError("Missing macOS host collect-results switch arm")
    require(
        host[collect_results_index:all_action_index],
        "$shouldCollectResults = $true",
        "macOS host collect-results action",
    )
    require(host, "Test-MacOSResultArchive -ArchivePath $archivePath -RunId $MacOSRunId", "macOS host automatic signoff result validation")

    for tool in ("xcrun", "plutil", "lipo", "otool", "codesign"):
        require(bootstrap, f"require_command {tool}", f"macOS bootstrap {tool} validation")
    for source, context in (
        (bootstrap, "macOS bootstrap guest-home validation"),
        (assets, "macOS asset guest-home validation"),
        (guest, "macOS build/test guest-home validation"),
    ):
        require(source, "require_guest_home", context)
        require(source, "OPENQ4_GUEST_HOME", context)
        require(source, "contains_control_chars", context)
        require(source, "OPENQ4_GUEST_HOME/HOME must not contain control characters", context)
        require(source, 'GUEST_HOME="$(require_guest_home)"', context)
        require(source, 'export HOME="${GUEST_HOME}"', context)
        require(source, "macOS guest home directory does not exist", context)

    require(assets, "This asset script must run inside macOS.", "macOS asset Darwin guard")
    require(guest, "This build/test script must run inside macOS.", "macOS guest Darwin guard")
    require_before(assets, 'if [[ "$(uname -s)" != "Darwin" ]]', 'GUEST_HOME="$(require_guest_home)"', "macOS asset validates host before guest-home resolution")
    require_before(guest, 'if [[ "$(uname -s)" != "Darwin" ]]', 'GUEST_HOME="$(require_guest_home)"', "macOS guest validates host before guest-home resolution")

    require(assets, "reject_unsafe_tar_entries", "macOS asset tar path validation")
    require(assets, "tarfile.open", "macOS asset tar structured validation")
    require(assets, "ord(character) < 32", "macOS asset tar control-character validation")
    require(assets, "Asset archive contains duplicate member", "macOS asset duplicate tar member validation")
    require(assets, "case-insensitive duplicate entries", "macOS asset tar case-collision validation")
    require(assets, "Unable to inspect asset archive", "macOS asset tar malformed-archive validation")
    require(assets, "expand_guest_path", "macOS asset guest tilde expansion")
    require(assets, "reject_unsafe_tree_entries", "macOS asset symlink validation")
    require(assets, "reject_macos_non_runtime_metadata_entries", "macOS asset metadata validation")
    require(assets, "reject_case_insensitive_tree_collisions", "macOS asset case-collision validation")
    require(assets, "require_safe_asset_roots", "macOS asset root validation")
    require(assets, "workspace_raw = sys.argv[1]", "macOS asset raw root validation")
    require(assets, "guest_home_raw = sys.argv[3]", "macOS asset explicit guest-home root validation")
    require(assets, "home = pathlib.Path(guest_home_raw).resolve()", "macOS asset explicit guest-home root validation")
    require(assets, "import unicodedata", "macOS asset root casefold dependency")
    require(assets, "must not contain control characters", "macOS asset raw root control-character validation")
    require(assets, "segments = path_text.strip", "macOS asset raw path segment validation")
    require(assets, "must be an absolute path after ~ expansion", "macOS asset relative root rejection")
    require(assets, "must not contain dot or empty path segments", "macOS asset ambiguous root segment rejection")
    require(assets, "require_basepath_outside_reserved_workspace_children", "macOS asset reserved workspace child guard")
    require(assets, 'reserved_children = ("openQ4", "openQ4-game", "incoming-quake4", "results")', "macOS asset reserved workspace child list")
    require(assets, "path_is_same_or_under", "macOS asset reserved workspace child case-insensitive guard")
    require(assets, "casefold()", "macOS asset reserved workspace child case-insensitive guard")
    require(assets, "Asset install basepath must not target a reserved workflow directory", "macOS asset reserved workspace child diagnostic")
    require(assets, "Asset install basepath must not live under a reserved workflow directory", "macOS asset reserved workspace subtree diagnostic")
    require(assets, "OPENQ4_Q4_TAR must not be a symlink", "macOS asset archive symlink validation")
    require(assets, "OPENQ4_Q4_TAR must not contain control characters", "macOS asset archive path control-character validation")
    require(assets, "COPYFILE_DISABLE=1 tar -xf", "macOS asset archive copyfile suppression")
    require(assets, "rsync --delete would remove workflow files", "macOS asset destructive target guard")
    require(assets, "Unsafe asset archive path", "macOS asset tar path validation")
    require(assets, "Asset archive contains a symlink or special file", "macOS asset special-file validation")
    require(assets, "contains non-runtime macOS metadata/debug entry", "macOS asset metadata validation")
    require(assets, "contains a case-insensitive path collision", "macOS asset case-collision validation")
    require(assets, '"Asset archive"', "macOS incoming asset metadata/case-collision validation")
    require(assets, '"Installed Quake 4 asset tree"', "macOS installed asset metadata/case-collision validation")
    require(assets, "tar -tvf", "macOS asset archive type validation")
    require(assets, "including hardlinks", "macOS asset archive hardlink validation")
    require(assets, "--exclude '/q4base/*.cfg'", "macOS asset personal config exclusion")
    require(assets, "--exclude '/q4base/*.log'", "macOS asset personal log exclusion")
    require(assets, "--exclude '/q4base/q4key'", "macOS asset private key exclusion")
    require(assets, "--exclude '/q4base/quake4key'", "macOS asset private key exclusion")
    require(assets, "--exclude '/q4mp/*.cfg'", "macOS multiplayer asset personal config exclusion")
    require(assets, "--exclude '/q4mp/*.log'", "macOS multiplayer asset personal log exclusion")
    require(assets, "multiple q4base directories", "macOS asset ambiguity validation")
    require(assets, "Installed asset directory has no q4base PK4 files", "macOS asset PK4 presence validation")
    require(assets, "Installed Quake 4 asset PK4 count", "macOS asset PK4 install evidence")
    require(assets, "/tmp/openq4-macos-*/*", "macOS asset copied archive cleanup")

    require(guest, "resolve_under_repo", "macOS guest builddir containment")
    require(guest, "Path escapes the openQ4 repository", "macOS guest builddir containment")
    require(guest, "expand_guest_path", "macOS guest tilde expansion")
    require(guest, "require_safe_builddir", "macOS guest builddir safety validation")
    require(guest, "OPENQ4_BUILDDIR must not resolve to the openQ4 repository root", "macOS guest builddir root guard")
    require(guest, "OPENQ4_BUILDDIR must live under .tmp/ or use a builddir* name", "macOS guest builddir output-tree guard")
    require(guest, "OPENQ4_BUILDDIR must not target source, content, tool, git, or staged runtime directories", "macOS guest builddir owned-tree guard")
    require(guest, "require_positive_integer", "macOS guest numeric option validation")
    require(guest, "require_safe_guest_roots", "macOS guest workspace/basepath validation")
    require(guest, "workspace_raw = sys.argv[1]", "macOS guest raw root validation")
    require(guest, "guest_home_raw = sys.argv[3]", "macOS guest explicit guest-home root validation")
    require(guest, "home = pathlib.Path(guest_home_raw).resolve()", "macOS guest explicit guest-home root validation")
    require(guest, "must not contain control characters", "macOS guest raw root control-character validation")
    require(guest, "segments = path_text.strip", "macOS guest raw path segment validation")
    require(guest, "must be an absolute path after ~ expansion", "macOS guest relative root rejection")
    require(guest, "must not contain dot or empty path segments", "macOS guest ambiguous root segment rejection")
    require(guest, "OPENQ4_BASEPATH must not contain OPENQ4_GUEST_WORKSPACE", "macOS guest destructive basepath guard")
    require(guest, "require_basepath_outside_reserved_workspace_children", "macOS guest reserved workspace child guard")
    require(guest, 'reserved_children = ("openQ4", "openQ4-game", "incoming-quake4", "results")', "macOS guest reserved workspace child list")
    require(guest, "path_is_same_or_under", "macOS guest reserved workspace child case-insensitive guard")
    require(guest, "casefold()", "macOS guest reserved workspace child case-insensitive guard")
    require(guest, "OPENQ4_BASEPATH must not target reserved workflow directory", "macOS guest reserved workspace child diagnostic")
    require(guest, "OPENQ4_BASEPATH must not live under reserved workflow directory", "macOS guest reserved workspace subtree diagnostic")
    require(guest, "OPENQ4_ALLOW_CROSS_ARCH_CLIENT", "macOS guest cross-arch launch opt-in")
    require(guest, "Missing staged macOS client for host architecture", "macOS guest exact-arch client selection")
    require(guest, "Staged macOS client must not be a symlink", "macOS guest client symlink validation")
    require(guest, "count_q4base_pk4s", "macOS guest asset PK4 validation")
    require(guest, "validate_asset_basepath", "macOS guest asset basepath validation")
    require(guest, "Quake 4 asset basepath must not be a symlink", "macOS guest asset basepath symlink validation")
    require(guest, "Quake 4 asset basepath contains a symlink or special file", "macOS guest asset basepath special-file validation")
    require(guest, "Quake 4 asset basepath contains non-runtime macOS metadata/debug entry", "macOS guest asset basepath metadata validation")
    require(guest, "Quake 4 asset basepath contains a case-insensitive path collision", "macOS guest asset basepath case-collision validation")
    require(guest, "Quake 4 asset basepath has no q4base PK4 files", "macOS guest asset basepath validation")
    require(guest, "Validated Quake 4 asset basepath", "macOS guest asset validation evidence")
    require(guest, 'graphics_bridge="${OPENQ4_MACOS_GRAPHICS_BRIDGE:-opengl}"', "macOS guest bridge environment")
    require(guest, 'openal_provider="${OPENQ4_MACOS_OPENAL_PROVIDER:-system}"', "macOS guest OpenAL Soft provider environment")
    require(guest, 'stamp="${OPENQ4_MACOS_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"', "macOS guest host-controlled run ID")
    require(guest, "RESULT_TOKEN_MAX_LENGTH=80", "macOS guest result token length guard")
    require(guest, "require_result_token", "macOS guest result token validation")
    require(guest, "require_choice", "macOS guest option choice validation")
    require(guest, 'require_result_token "OPENQ4_MACOS_RUN_ID" "${stamp}"', "macOS guest run ID validation")
    require(guest, 'require_choice "macOS workflow action" "${action}" build smoke renderer launcher signoff all', "macOS guest action validation")
    require(guest, 'require_choice "OPENQ4_MACOS_GRAPHICS_BRIDGE" "${graphics_bridge}" opengl metal', "macOS guest bridge validation")
    require(guest, 'require_choice "OPENQ4_MACOS_OPENAL_PROVIDER" "${openal_provider}" apple_framework system', "macOS guest OpenAL provider validation")
    require(guest, "prepare_run_dir", "macOS guest result directory preparation")
    require(guest, "require_empty_result_directory", "macOS guest stale result directory guard")
    require(guest, "macOS result directory already exists and is not empty", "macOS guest stale result directory diagnostic")
    require(guest, "Use a fresh OPENQ4_MACOS_RUN_ID or remove the stale result directory before rerunning", "macOS guest stale result directory diagnostic")
    require(guest, 'find "${candidate}" -mindepth 1 -print -quit', "macOS guest stale result directory hidden-file guard")
    require(guest, "macOS results root must not be a symlink", "macOS guest result root symlink validation")
    require(guest, "macOS result directory must not be a symlink", "macOS guest result directory symlink validation")
    require(guest, "macOS workflow log path must not be a symlink or directory", "macOS guest workflow log symlink validation")
    require(guest, "macOS workflow log path must be a regular file", "macOS guest workflow log special-file validation")
    require_before(guest, 'require_choice "macOS workflow action" "${action}" build smoke renderer launcher signoff all', 'prepare_run_dir', "macOS guest validates action before result directory creation")
    require_before(guest, 'prepare_run_dir', 'exec > >(tee -a "${run_dir}/openq4-macos-workflow.log")', "macOS guest validates result directory before logging")
    require_before(guest, 'require_choice "OPENQ4_MACOS_GRAPHICS_BRIDGE" "${graphics_bridge}" opengl metal', 'run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"', "macOS guest validates bridge before result directory naming")
    require_before(guest, 'require_choice "OPENQ4_MACOS_OPENAL_PROVIDER" "${openal_provider}" apple_framework system', 'run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"', "macOS guest validates provider before logging starts")
    require(guest, 'run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"', "macOS guest bridge-specific results")
    require(guest, 'require_choice "OPENQ4_BUILDTYPE" "${buildtype}" plain debug debugoptimized release minsize custom', "macOS guest buildtype validation")
    require(guest, 'require_choice "OPENQ4_PLATFORM_BACKEND" "${platform_backend}" sdl3 native', "macOS guest platform backend validation")
    require_before(guest, 'require_safe_builddir "${raw_builddir}" "${builddir}"', 'bash tools/build/meson_setup.sh setup --wipe "${builddir}" .', "macOS guest validates builddir before Meson setup")
    require_before(guest, 'require_choice "OPENQ4_PLATFORM_BACKEND" "${platform_backend}" sdl3 native', 'bash tools/build/meson_setup.sh setup --wipe "${builddir}" .', "macOS guest validates backend before Meson setup")
    require_before(guest, 'require_positive_integer "OPENQ4_SMOKE_LIMIT" "${smoke_limit}" 100000', 'python3 tools/tests/renderer_gameplay_benchmark.py', "macOS guest validates smoke limit before benchmark")
    require_before(guest, 'require_positive_integer "OPENQ4_SMOKE_TIMEOUT" "${smoke_timeout}" 86400', 'python3 tools/tests/renderer_gameplay_benchmark.py', "macOS guest validates smoke timeout before benchmark")
    require(guest, "run_mp_smoke", "macOS guest multiplayer smoke action")
    require(guest, "OPENQ4_MP_SMOKE_TIMEOUT", "macOS guest MP smoke timeout control")
    require(guest, "--cases mp-q4dm1-listen", "macOS guest MP smoke case")
    require(guest, '--output-dir "${run_dir}/renderer-mp-smoke"', "macOS guest MP smoke output directory")
    require(guest, "Running openQ4 macOS multiplayer smoke", "macOS guest MP smoke log evidence")
    require_before(guest, 'require_positive_integer "OPENQ4_MP_SMOKE_TIMEOUT" "${mp_smoke_timeout}" 86400', '--cases mp-q4dm1-listen', "macOS guest validates MP smoke timeout before benchmark")
    require_before(guest, 'require_positive_integer "OPENQ4_RENDERER_TIMEOUT" "${renderer_timeout}" 86400', 'python3 tools/tests/renderer_validation_matrix.py', "macOS guest validates renderer timeout before matrix")
    require(guest, '"-Dmacos_graphics_bridge=${graphics_bridge}"', "macOS guest bridge setup option")
    require(guest, '"-Dmacos_openal_provider=${openal_provider}"', "macOS guest OpenAL setup option")
    require(guest, "shell_quote", "macOS guest launcher quoting")
    require(guest, "shlex.quote", "macOS guest launcher quoting")
    require(guest, "exec ${client_q} +set fs_basepath ${basepath_q}", "macOS guest launcher quoted exec")
    require(guest, "Refusing to replace unsafe macOS launcher path", "macOS guest launcher symlink validation")
    require(guest, 'launcher_tmp="$(mktemp "${GUEST_HOME}/Desktop/openQ4.command.XXXXXX")"', "macOS guest launcher atomic write")
    require(guest, "Refusing to replace unsafe macOS signoff report path", "macOS guest report symlink validation")
    require(guest, 'report_tmp="$(mktemp "${run_dir}/macos-runtime-signoff.md.XXXXXX")"', "macOS guest report atomic write")
    require(guest, 'mv -f "${report_tmp}" "${report}"', "macOS guest report atomic publish")
    require(guest, "run_signoff", "macOS guest runtime signoff action")
    require(guest, "macos-runtime-signoff.md", "macOS guest runtime signoff report")
    require(guest, "Manual Hardware Checklist", "macOS guest runtime signoff checklist")
    require(guest, "Graphics bridge: ${graphics_bridge}", "macOS guest signoff bridge metadata")
    require(guest, "OpenAL provider: ${openal_provider}", "macOS guest signoff OpenAL metadata")
    require(guest, "Bridge-specific build and staged install completed.", "macOS guest signoff build evidence")
    require(guest, "validate_staged_macos_payload", "macOS guest staged payload validation")
    require(guest, "Staged macOS payload integrity checks completed.", "macOS guest staged payload evidence")
    require(guest, "Quake 4 asset basepath validation completed.", "macOS guest asset validation signoff evidence")
    require(guest, "Multiplayer listen-server smoke completed with retail Quake 4 assets.", "macOS guest signoff MP smoke evidence")
    require(guest, "MP game module path is present in the staged payload.", "macOS guest signoff MP module evidence")
    require(guest, "Validated staged macOS payload", "macOS guest staged payload log evidence")
    require(guest, "Missing staged macOS support file", "macOS guest staged icon/splash validation")
    require(guest, "collect_macos_support_info.sh", "macOS guest staged support collector validation")
    require(guest, "required_scripts", "macOS guest support script separated from Mach-O validation")
    require(guest, "Missing or non-executable staged macOS support script", "macOS guest staged support collector executable validation")
    require(guest, "macOS staged support collector is missing marker", "macOS guest staged support collector validation")
    require(guest, "contains_control_chars()", "macOS guest staged support collector path-control validation")
    require(guest, "sanitize_text()", "macOS guest staged support collector text sanitization validation")
    require(guest, "limit_stream_tail()", "macOS guest staged support collector stream-bounding validation")
    require(guest, "Support report output is limited to the final", "macOS guest staged support collector stream-bounding validation")
    require(guest, ".XXXXXX.tar.gz.tmp", "macOS guest staged support collector mktemp validation")
    require(guest, "does not launch openQ4", "macOS guest staged support collector no-launch validation")
    require(guest, "does not copy retail q4base PK4 assets", "macOS guest staged support collector privacy validation")
    require(guest, "truncated copy failed; source was not copied", "macOS guest staged support collector truncation validation")
    require(guest, "forbidden privacy/no-launch pattern", "macOS guest staged support collector forbidden-token validation")
    require(guest, "openQ4-client_x64 >", "macOS guest staged support collector x64 launch denial")
    require(guest, "openQ4-ded_x64 >", "macOS guest staged support collector x64 launch denial")
    require(guest, "|| cat", "macOS guest staged support collector fallback denial")
    require(guest, 'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat', "macOS guest staged support collector fallback denial")
    require(guest, "macOS staged install contains a symlink or special file", "macOS guest staged symlink/special validation")
    require(guest, "macOS staged install contains non-runtime metadata/debug entry", "macOS guest staged metadata validation")
    require(guest, "macOS staged install contains a case-insensitive path collision", "macOS guest staged case-collision validation")
    require(guest, "unicodedata.normalize", "macOS guest staged case-collision validation")
    require(guest, ".Spotlight-V100", "macOS guest staged metadata validation")
    require(guest, ".Trashes", "macOS guest staged metadata validation")
    require(guest, "macOS staged install contains non-dylib game modules", "macOS guest stale module validation")
    require(guest, "macOS staged install contains stale or mismatched root engine binary", "macOS guest stale root binary validation")
    require(guest, "macOS staged install contains stale or mismatched game module", "macOS guest stale dylib validation")
    require(guest, "lipo -archs", "macOS guest staged architecture validation")
    require(guest, "Staged Binary Architectures", "macOS guest staged architecture evidence")
    require(guest, "Dedicated server: ${dedicated:-not found}", "macOS guest dedicated server signoff metadata")
    require(guest, "SPDisplaysDataType", "macOS guest display inventory")
    require(guest, "SPHardwareDataType", "macOS guest hardware inventory")
    require(guest, "SPAudioDataType", "macOS guest audio inventory")
    require(guest, "SPUSBDataType", "macOS guest USB inventory")
    require(guest, "SPBluetoothDataType", "macOS guest Bluetooth inventory")
    require(workflow_doc, "OPENQ4_GUEST_HOME", "macOS workflow guest-home override documentation")
    require(workflow_doc, "archive-expansion and result-collection snippets", "macOS workflow remote home fallback documentation")
    require(guest, "build_openq4\n    run_smoke\n    run_mp_smoke\n    run_renderer_matrix\n    install_launcher\n    write_signoff_report", "macOS guest signoff run order")

    require(signoff_validator, "validate_signoff_archive", "macOS signoff archive validator")
    require(signoff_validator, "Archive path escapes through '..'", "macOS signoff archive path guard")
    require(signoff_validator, "Archive path must not be a symlink", "macOS signoff archive input symlink guard")
    require(signoff_validator, "Archive path contains a control character", "macOS signoff archive path control-character guard")
    require(signoff_validator, "Archive path contains an empty segment", "macOS signoff archive path guard")
    require(signoff_validator, "Archive path contains a dot segment", "macOS signoff archive path guard")
    require(signoff_validator, "Archive contains a duplicate member", "macOS signoff archive duplicate guard")
    require(signoff_validator, "Archive contains case-insensitive duplicate members", "macOS signoff archive case-collision guard")
    require(signoff_validator, "is_macos_non_runtime_metadata_path", "macOS signoff archive metadata guard")
    require(signoff_validator, "Archive contains non-runtime macOS metadata/debug entry", "macOS signoff archive metadata guard")
    require(signoff_validator, "has_file_under", "macOS signoff archive output-file guard")
    require(signoff_validator, "Archive contains a non-regular entry", "macOS signoff archive entry-type guard")
    require(signoff_validator, "Archive member has special mode bits", "macOS signoff archive mode guard")
    require(signoff_validator, "Archive member is group/other writable", "macOS signoff archive mode guard")
    require(signoff_validator, "MAX_TEXT_MEMBER_BYTES", "macOS signoff archive text size guard")
    require(signoff_validator, "MAX_ARCHIVE_MEMBER_BYTES", "macOS signoff archive member size guard")
    require(signoff_validator, "MAX_ARCHIVE_MEMBERS", "macOS signoff archive member-count guard")
    require(signoff_validator, "MAX_ARCHIVE_TOTAL_BYTES", "macOS signoff archive total-size guard")
    require(signoff_validator, "MAX_RESULT_TOKEN_CHARS", "macOS signoff archive result token length guard")
    require(signoff_validator, "RESULT_TOKEN_PATTERN", "macOS signoff archive action token guard")
    require(signoff_validator, "Archive text member is too large", "macOS signoff archive text size guard")
    require(signoff_validator, "Archive member is too large", "macOS signoff archive member size guard")
    require(signoff_validator, "Archive contains too many members", "macOS signoff archive member-count guard")
    require(signoff_validator, "Archive total expanded size is too large", "macOS signoff archive total-size guard")
    require(signoff_validator, "did not record a staged client path", "macOS signoff archive client evidence guard")
    require(signoff_validator, "did not record a staged dedicated server path", "macOS signoff archive dedicated evidence guard")
    require(signoff_validator, "does not reference its renderer-smoke output directory", "macOS signoff archive report-output guard")
    require(signoff_validator, "does not reference its renderer-mp-smoke output directory", "macOS signoff archive MP report-output guard")
    require(signoff_validator, "does not reference the expected signoff report path", "macOS signoff archive log-report guard")
    require(signoff_validator, 'validate_result_token("action", action)', "macOS signoff archive action token guard")
    require(signoff_validator, "token is too long", "macOS signoff archive result token length guard")
    require(signoff_validator, "validate_result_token", "macOS signoff archive run ID token guard")
    require(signoff_validator, "expected_signoff_archive_name", "macOS signoff archive filename/run ID guard")
    require(signoff_validator, "Archive file name does not match run ID", "macOS signoff archive filename/run ID guard")
    require(signoff_validator, "Bridge list contains duplicates", "macOS signoff archive duplicate bridge guard")
    require(signoff_validator, "Staged macOS payload integrity checks completed.", "macOS signoff archive staged payload evidence")
    require(signoff_validator, "Quake 4 asset basepath validation completed.", "macOS signoff archive asset evidence")
    require(signoff_validator, "Archive contains unexpected top-level result directories", "macOS signoff archive exact top-dir guard")
    require(signoff_validator, "macos-runtime-signoff.md", "macOS signoff archive report check")
    require(signoff_validator, "openq4-macos-workflow.log", "macOS signoff archive log check")
    require(signoff_validator, "renderer-smoke", "macOS signoff archive smoke output check")
    require(signoff_validator, "renderer-mp-smoke", "macOS signoff archive MP smoke output check")
    require(signoff_validator, "renderer-matrix", "macOS signoff archive matrix output check")
    require(signoff_validator, "## Staged Binary Architectures", "macOS signoff archive staged architecture evidence")
    require(signoff_validator, "## Hardware", "macOS signoff archive hardware profile evidence")
    require(signoff_validator, "## Displays", "macOS signoff archive hardware inventory evidence")
    require(signoff_validator, "require_completed_checklist", "macOS signoff archive completed checklist gate")
    require(validation_runner, "macos_signoff_archive.py", "validation runner macOS signoff archive test")

    require(debug, "bash -n tools/macos/guest/openq4-macos-bootstrap.sh", "macOS debug workflow guest script syntax check")
    require(debug, "bash -n tools/macos/guest/openq4-macos-install-quake4-assets.sh", "macOS debug workflow asset script syntax check")
    require(debug, "bash -n tools/macos/guest/openq4-macos-sync-build-test.sh", "macOS debug workflow build script syntax check")
    for token in (
        "Record hosted evidence scope",
        "macos-debug-evidence-scope.txt",
        "requested_bridge=${requested}",
        "artifact_bridge=${bridge}",
        "openq4_commit=\"$(git rev-parse HEAD)\"",
        "openq4_game_commit=\"$(git -C \"${OPENQ4_GAMELIBS_REPO}\" rev-parse HEAD)\"",
        "openq4_game_dirty=${openq4_game_dirty}",
        "hosted build/package validation only; not a completed manual Apple-hardware gameplay signoff",
        "both requested; this artifact contains only ${bridge}",
        "the other bridge was intentionally not selected",
    ):
        require(debug, token, "macOS debug workflow evidence-scope contract")
    require(workflow_doc, "-Action Signoff", "macOS workflow signoff documentation")
    require(workflow_doc, "-Action CollectResults", "macOS workflow collect-results documentation")
    require(workflow_doc, "-MacOSGraphicsBridge both", "macOS workflow dual bridge signoff documentation")
    require(workflow_doc, "<timestamp>-signoff-opengl", "macOS workflow bridge-specific signoff documentation")
    require(workflow_doc, "<timestamp>-signoff-metal", "macOS workflow bridge-specific signoff documentation")
    require(workflow_doc, ".tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz", "macOS workflow result collection documentation")
    require(workflow_doc, "-MacOSRunId <id>", "macOS workflow run ID documentation")
    require(workflow_doc, "-ResultCollectionDir", "macOS workflow result directory documentation")
    require(workflow_doc, "-SkipResultCollection", "macOS workflow result collection opt-out documentation")
    require(workflow_doc, "-SkipResultArchiveValidation", "macOS workflow result archive validation opt-out documentation")
    require(workflow_doc, "-RequireCompletedSignoffChecklist", "macOS workflow completed checklist validation documentation")
    require(workflow_doc, "must be absolute POSIX paths or use a leading `~/`", "macOS workflow absolute path documentation")
    require(workflow_doc, "guest scripts recheck that the paths are absolute and control-character-free after `~` expansion", "macOS workflow guest path validation documentation")
    require(workflow_doc, "host preflight expands `~/` before that reserved-child comparison", "macOS workflow host reserved path documentation")
    require(workflow_doc, "does not create double-slash remote extraction targets", "macOS workflow remote sync path joining documentation")
    require(workflow_doc, "-Action CollectResults -MacOSRunId <run-id>", "macOS workflow collect-results expected validation documentation")
    require(workflow_doc, "same-directory temporary file", "macOS workflow local copy-back temporary publish documentation")
    require(workflow_doc, "published without\noverwriting an existing archive", "macOS workflow local copy-back no-overwrite documentation")
    require(workflow_doc, "remote\ntarball is also written to a same-directory temporary file", "macOS workflow remote result archive temporary publish documentation")
    require(workflow_doc, "hard-linked into the final copy-back name without replacing", "macOS workflow remote result archive no-overwrite documentation")
    require(workflow_doc, "Guest result directories must be new or empty before logging starts", "macOS workflow stale result directory documentation")
    require(workflow_doc, "tools/macos/validate_signoff_archive.py", "macOS workflow signoff archive validator documentation")
    require(workflow_doc, "--require-completed-checklist", "macOS workflow signoff archive completed checklist documentation")
    require(workflow_doc, "-MacOSOpenALProvider system", "macOS workflow OpenAL provider documentation")
    require(workflow_doc, "macos-runtime-signoff.md", "macOS workflow signoff documentation")
    require(workflow_doc, "keyboard/mouse/controller input", "macOS workflow signoff documentation")
    require(workflow_doc, "multiplayer `renderer-mp-smoke` output", "macOS workflow MP smoke documentation")
    require(workflow_doc, "renderer_gameplay_benchmark.py --profile smoke --cases mp-q4dm1-listen", "macOS workflow MP smoke command documentation")


def validate_macos_shell_entrypoints() -> None:
    for relative_path in (
        "tools/build/meson_setup.sh",
        "tools/validation/validate_pr.sh",
        "tools/validation/validate_push.sh",
    ):
        source = read(relative_path)
        reject(source, "dirname --", relative_path)
        require(source, 'case "${BASH_SOURCE[0]}" in', relative_path)
        require(source, 'script_dir="$(CDPATH= cd "${script_dir}" && pwd)"', relative_path)

    collector = read("tools/macos/collect_macos_support_info.sh")
    reject(collector, "dirname --", "macOS support collector BSD dirname compatibility")
    require(collector, 'case "$0" in', "macOS support collector script directory resolution")
    require(collector, 'SCRIPT_DIR=$(CDPATH= cd "${script_dir}" && pwd -P)', "macOS support collector script directory resolution")


def validate_docs_and_ci_hooks() -> None:
    building = read("BUILDING.md")
    platform_support = read("docs/dev/platform-support.md")
    migration = read("docs/dev/sdl3-linux-macos-migration.md")
    getting_started = read("docs/user/getting-started.md")
    package_readme = read("assets/release/README.html")
    release_notes = read("docs/dev/release-completion.md")
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require(building, "-Dmacos_graphics_bridge=metal", "build documentation")
    require(building, "without a native renderer rewrite", "build documentation")
    require(building, "when a signed/notarized experimental macOS release is required", "macOS release signing documentation")
    require(building, "MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64", "macOS release signing documentation")
    require(building, "xcrun stapler validate", "macOS release signing documentation")
    require(building, "openq4-<version>-macos-arm64-opengl.dmg", "macOS DMG release asset documentation")
    require(building, "final DMG notarization/stapling", "macOS DMG release documentation")
    require(building, "do not pass a custom entitlements file by default", "macOS entitlement policy documentation")
    require(building, "rejects App Sandbox or `get-task-allow` entitlements", "macOS entitlement policy documentation")
    require(building, "Credentialed runs publish signed/notarized DMGs", "macOS release credential policy documentation")
    require(building, "`-unsigned.tar.gz` archives", "macOS release credential fallback documentation")
    require(building, "Experimental manual macOS release artifacts are Apple Silicon/arm64 only", "macOS architecture policy documentation")
    require(building, "Intel Mac and universal2 packages are not published", "macOS architecture policy documentation")
    require(building, "SDL3 is the default platform path and does not link Carbon", "macOS Carbon isolation documentation")
    require(building, "macos_openal_provider", "macOS OpenAL provider documentation")
    require(building, "dependency('openal', method: 'pkg-config')", "macOS OpenAL provider documentation")
    require(getting_started, "signed/notarized OpenGL and Metal bridge DMGs", "getting started guide")
    require(getting_started, "drag `openQ4.app` to `/Applications`", "getting started guide")
    require(getting_started, "signed SP/MP modules", "getting started self-contained app guide")
    require(getting_started, "publish clearly labeled `-unsigned.tar.gz` archives", "getting started macOS credential fallback")
    require(getting_started, "ad-hoc signed only for bundle validity", "getting started macOS unsigned policy")
    require(getting_started, "macOS support is experimental. Current packages are for Apple Silicon/arm64 Macs", "getting started macOS architecture policy")
    require(getting_started, "Intel Mac and universal2 packages are not published yet", "getting started macOS architecture policy")
    require(package_readme, "signed/notarized OpenGL and Metal bridge DMGs", "release package README")
    require(package_readme, "drag <code>openQ4.app</code> to <code>/Applications</code>", "release package README")
    require(package_readme, "signed SP/MP modules", "release package self-contained app guide")
    require(package_readme, "clearly labeled <code>-unsigned.tar.gz</code> archives", "release package macOS credential fallback")
    require(package_readme, "ad-hoc signed only for bundle validity", "release package macOS unsigned policy")
    require(package_readme, "macOS support is experimental. Current packages are for Apple Silicon/arm64 Macs", "release package macOS architecture policy")
    require(package_readme, "Intel Mac and universal2 packages are not published yet", "release package macOS architecture policy")
    require(release_notes, "Experimental macOS builds now retain the OpenGL package while adding a separate Metal bridge package", "release completion notes")
    require(release_notes, "Final experimental macOS package validation", "release completion notes")
    require(release_notes, "Experimental macOS release packaging now has a Developer ID/notarization lane", "release completion notes")
    require(release_notes, "Experimental macOS release entitlement handling is now explicit and fail-fast", "release completion entitlement policy")
    require(release_notes, "Credentialed experimental macOS release artifacts now use compressed DMG images", "release completion DMG packaging")
    require(release_notes, "Manual releases keep experimental macOS downloads visible", "release completion unsigned macOS fallback")
    require(release_notes, "Apple Silicon/arm64-only experimental macOS support policy", "release completion macOS architecture policy")
    require(release_notes, "Intel Mac and universal2 packages are not advertised", "release completion macOS architecture policy")
    require(release_notes, "Experimental macOS SDL3 release builds now avoid linking the legacy Carbon framework", "release completion Carbon isolation note")
    require(release_notes, "Large single-player levels on macOS no longer return to the menu", "release completion OpenAL Soft fix note")
    require(platform_support, "Linux and experimental macOS now use the shared SDL3 runtime path", "platform support roadmap")
    require(platform_support, "Experimental macOS SDL3 builds select `src/sys/osx/macosx_sdl3.cpp`", "platform support roadmap")
    require(platform_support, "Credentialed release runs publish signed/notarized DMGs", "platform support macOS credential policy")
    require(platform_support, "`-unsigned.tar.gz` archives", "platform support macOS credential fallback")
    require(platform_support, "Experimental manual macOS release artifacts are Apple Silicon/arm64 only", "platform support macOS architecture policy")
    require(platform_support, "Intel Mac and universal2 packages are intentionally not claimed", "platform support macOS architecture policy")
    require(platform_support, "final compressed DMG creation", "platform support DMG packaging")
    require(platform_support, "DMG notarization/stapling", "platform support DMG packaging")
    require(platform_support, "keeps the legacy Carbon framework isolated to `-Dplatform_backend=native`", "platform support Carbon isolation")
    require(platform_support, "Hardened Runtime without custom entitlements by default", "platform support entitlement policy")
    require(platform_support, "App Sandbox or `get-task-allow` entitlements are rejected", "platform support entitlement policy")
    require(platform_support, "checksum-pinned OpenAL Soft 1.25.1 CoreAudio runtime", "platform support OpenAL Soft provider policy")
    require(platform_support, "`-Dmacos_openal_provider=system`", "platform support OpenAL provider switch")
    require(migration, "leaves Carbon isolated to the native Cocoa fallback", "SDL3 migration Carbon isolation")
    require(migration, "experimental macOS CI covers OpenGL and Metal bridge configure/build/install/package validation", "SDL3 migration plan")

    require(validator, "macos_metal_bridge.py", "validation runner")
    require(push, "tools/tests/macos_metal_bridge.py", "push verification workflow")
    require(commit, "tools/tests/macos_metal_bridge.py", "commit validation workflow")


def validate_embedded_guest_root_validators_runtime() -> None:
    assets = read("tools/macos/guest/openq4-macos-install-quake4-assets.sh")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    asset_body = shell_heredoc_body(
        assets,
        "require_safe_asset_roots() {",
        "macOS asset root validator",
    )
    guest_body = shell_heredoc_body(
        guest,
        "require_safe_guest_roots() {",
        "macOS guest root validator",
    )

    temp_parent = ROOT / ".tmp"
    temp_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-root-validator-", dir=temp_parent) as temp_dir:
        root = Path(temp_dir)
        home = root / "home"
        home.mkdir()
        workspace = home / "openq4-work"
        basepath = workspace / "Quake4"

        valid_args = [workspace.as_posix(), basepath.as_posix(), home.as_posix()]
        for body, context in (
            (asset_body, "macOS asset root validator"),
            (guest_body, "macOS guest root validator"),
        ):
            code, stderr = run_embedded_python(body, valid_args, context)
            if code != 0:
                raise AssertionError(f"{context} rejected valid synthetic roots: {stderr}")

            bad_args = [f"{workspace.as_posix()}\nunsafe", basepath.as_posix(), home.as_posix()]
            code, stderr = run_embedded_python(body, bad_args, context)
            if code == 0 or "must not contain control characters" not in stderr:
                raise AssertionError(f"{context} did not reject control-character workspace paths: {stderr}")


def main() -> None:
    validate_meson_contract()
    validate_sdl3_runtime_contract()
    validate_packaging_and_release_contract()
    validate_macos_workflow_security_contract()
    validate_embedded_guest_root_validators_runtime()
    validate_macos_shell_entrypoints()
    validate_macos_app_bundle_validator_runtime()
    validate_macos_self_contained_app_creation_runtime()
    validate_macos_dmg_source_preflight_runtime()
    validate_macos_package_main_collateral_error_runtime()
    validate_macos_dmg_output_preflight_runtime()
    validate_macos_archive_validator_runtime()
    validate_macos_signing_config_runtime()
    validate_macos_notary_archive_output_preflight_runtime()
    validate_macos_signing_keeps_standalone_client_signature_runtime()
    validate_macos_install_name_normalization_runtime()
    validate_macos_pk4_symlink_guard_runtime()
    validate_legacy_macos_plist_runtime()
    validate_macos_staged_payload_validator_runtime()
    validate_docs_and_ci_hooks()
    print("macos_metal_bridge: ok")


if __name__ == "__main__":
    main()
