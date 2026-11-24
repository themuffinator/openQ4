#!/usr/bin/env python3
"""Static and fixture checks for macOS symbolication packaging policy."""

from __future__ import annotations

import importlib.util
import os
import shutil
import sys
import tarfile
import uuid
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
WORK_BASE = ROOT / ".tmp" / "macos-symbolication-policy-test"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def expect_runtime_error(fragment: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if fragment not in str(exc):
            raise AssertionError(f"unexpected error for {context}: {exc}") from exc
        return
    raise AssertionError(f"expected RuntimeError containing {fragment!r} for {context}")


def make_symlink(target: Path | str, link: Path, *, target_is_directory: bool = False) -> bool:
    try:
        link.symlink_to(target, target_is_directory=target_is_directory)
    except (OSError, NotImplementedError):
        return False
    return True


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


PACKAGE = load_module("package_nightly_symbolication_under_test", ROOT / "tools" / "build" / "package_nightly.py")


def make_work_root() -> Path:
    return WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"


def add_tar_bytes(archive: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    archive.addfile(info, fileobj=__import__("io").BytesIO(data))


def symbol_manifest_text(
    *,
    package_suffix: str = "-opengl",
    runtime_archive: str = "openq4-0.6.5-macos-arm64-opengl.dmg",
    symbol_archive: str = "openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz",
    duplicate_key: str = "",
) -> str:
    header = [
        "openQ4 macOS symbols",
        "format=1",
        "version=0.6.5",
        "version_tag=0.6.5",
        "platform=macos",
        "arch=arm64",
        f"package_suffix={package_suffix}",
        f"runtime_archive={runtime_archive}",
        f"symbol_archive={symbol_archive}",
    ]
    if duplicate_key:
        header.append(f"{duplicate_key}=duplicate")
    return "\n".join(
        header
        + [
            "",
            "binaries:",
            "- path=openQ4.app/Contents/MacOS/openQ4",
            "  sha256=" + "0" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000001 (arm64) openQ4",
            "  dsym=dSYMs/openQ4.app.dSYM",
            "- path=openQ4-client_arm64",
            "  sha256=" + "1" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000002 (arm64) openQ4-client_arm64",
            "  dsym=dSYMs/openQ4-client_arm64.dSYM",
            "- path=openQ4-ded_arm64",
            "  sha256=" + "2" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000003 (arm64) openQ4-ded_arm64",
            "  dsym=dSYMs/openQ4-ded_arm64.dSYM",
            "- path=openQ4.app/Contents/Frameworks/game_arm64.dylib",
            "  sha256=" + "3" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000004 (arm64) game_arm64.dylib",
            "  dsym=dSYMs/game_arm64.dylib.dSYM",
            # The Vulkan renderer module is openQ4-built and therefore carries a
            # dSYM, so it is a manifest binary exactly like the game module.
            # libMoltenVK.dylib ships next to it in Contents/Frameworks but is
            # deliberately NOT listed: it is a third-party prebuilt with no DWARF,
            # and macos_symbol_targets() excludes it for that reason.
            "- path=openQ4.app/Contents/Frameworks/renderer-vk_arm64.dylib",
            "  sha256=" + "5" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000006 (arm64) renderer-vk_arm64.dylib",
            "  dsym=dSYMs/renderer-vk_arm64.dylib.dSYM",
            "",
        ]
    )


def create_symbol_archive(
    path: Path,
    *,
    leak_runtime: bool = False,
    duplicate_manifest: bool = False,
    casefold_manifest: bool = False,
    huge_manifest: bool = False,
    metadata_sidecar: bool = False,
    runtime_archive: str = "openq4-0.6.5-macos-arm64-opengl.dmg",
    symbol_archive: str = "openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz",
    package_suffix: str = "-opengl",
    duplicate_key: str = "",
    manifest_replacements: tuple[tuple[str, str], ...] = (),
) -> str:
    root_name = "openq4-0.6.5-macos-arm64-opengl-symbols"
    manifest_data = symbol_manifest_text(
        package_suffix=package_suffix,
        runtime_archive=runtime_archive,
        symbol_archive=symbol_archive,
        duplicate_key=duplicate_key,
    )
    for old, new in manifest_replacements:
        manifest_data = manifest_data.replace(old, new)
    manifest_data = manifest_data.encode("utf-8")
    if huge_manifest:
        manifest_data = b"x" * (PACKAGE.MAX_MACOS_METADATA_MEMBER_BYTES + 1)
    entries = {
        f"{root_name}/SYMBOLS.txt": manifest_data,
        f"{root_name}/dSYMs/openQ4.app.dSYM/Contents/Resources/DWARF/openQ4": b"dsym\n",
        f"{root_name}/dSYMs/openQ4-client_arm64.dSYM/Contents/Resources/DWARF/openQ4-client_arm64": b"dsym\n",
        f"{root_name}/dSYMs/openQ4-ded_arm64.dSYM/Contents/Resources/DWARF/openQ4-ded_arm64": b"dsym\n",
        f"{root_name}/dSYMs/game_arm64.dylib.dSYM/Contents/Resources/DWARF/game_arm64.dylib": b"dsym\n",
        f"{root_name}/dSYMs/renderer-vk_arm64.dylib.dSYM/Contents/Resources/DWARF/renderer-vk_arm64.dylib": b"dsym\n",
        # No libMoltenVK.dylib.dSYM: MoltenVK is third-party, has no debug symbols
        # to ship, and package_nightly.py never stages one here.
    }
    if leak_runtime:
        entries[f"{root_name}/openQ4-client_arm64"] = b"runtime\n"
    if metadata_sidecar:
        entries[f"{root_name}/dSYMs/.DS_Store"] = b"finder\n"

    with tarfile.open(path, "w:xz") as archive:
        for name, data in sorted(entries.items()):
            add_tar_bytes(archive, name, data)
        if duplicate_manifest:
            add_tar_bytes(archive, f"{root_name}/SYMBOLS.txt", symbol_manifest_text().encode("utf-8"))
        if casefold_manifest:
            add_tar_bytes(archive, f"{root_name}/symbols.txt", b"ambiguous\n")
    return root_name


def validate_packager_contract() -> None:
    packaging = read("tools/build/package_nightly.py")

    for token in (
        'MACOS_SYMBOL_MANIFEST_NAME = "SYMBOLS.txt"',
        'MACOS_SYMBOL_ARCHIVE_SUFFIX = ".tar.xz"',
        "def macos_symbol_archive_stem(",
        "def macos_symbol_targets(package_root: Path, arch: str)",
        'Path("openQ4.app") / "Contents" / "MacOS" / "openQ4"',
        f'Path(f"{{PRODUCT_NAME}}-client_{{arch}}")',
        f'Path(f"{{PRODUCT_NAME}}-ded_{{arch}}")',
        "game_module = macos_embedded_game_module_path(package_root, arch)",
        'Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / game_module.name',
        'Path("dSYMs") / f"{game_module.name}.dSYM"',
        'Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"renderer-vk_{arch}.dylib"',
        "dwarfdump",
        "dsymutil",
        "create_macos_dsym_bundle",
        "prepare_macos_symbol_staging_root",
        "prepare_macos_dsym_output_path",
        "contains duplicate key",
        "contains unexpected header key",
        "validate_macos_manifest_archive_filename",
        "contains unsafe archive filename",
        "package_suffix",
        "runtime_archive",
        "unsupported archive suffix",
        "macOS symbol staging root must not be a symlink",
        "macOS symbol staging root exists but is not a directory",
        "macOS dSYM output must not be a symlink",
        "macOS dSYM output exists but is not a directory",
        "macOS symbol archive input root must not be a symlink",
        "macOS symbol archive output must not be inside the symbol staging root",
        "write_macos_symbol_manifest",
        "validate_macos_symbol_archive_contents",
        "macOS symbol archive path must not be a symlink",
        "macOS symbol archive contains duplicate entry",
        "macOS symbol archive contains case-insensitive duplicate entries",
        "is_macos_metadata_sidecar_path(Path(name))",
        "macOS symbol archive contains non-runtime metadata/debug entry",
        "MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS",
        "macOS symbol archive contains too many members",
        "MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES",
        "macOS symbol archive total expanded size is too large",
        "validate_macos_archive_metadata_member_size(name, member.size)",
        "contains duplicate binary entry",
        "contains unexpected binary entries",
        "is missing binary entries",
        "has invalid sha256",
        "has invalid size",
        "has invalid macho_uuid",
        "dsym is",
        "macos_expected_lipo_arches(arch)",
        "forbidden_runtime_entries",
        "macos_symbol_archive_path = create_macos_symbol_archive",
        "runtime_archive_name=archive_path.name",
        f"f\"{{package_prefix}}{{MACOS_SYMBOL_MANIFEST_NAME}}\"",
        "validate_no_macos_metadata_artifacts(package_root)",
        '".dSYM",',
    ):
        require(packaging, token, "macOS symbolication packaging contract")


def validate_symbol_archive_fixture() -> None:
    work = make_work_root()
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True, exist_ok=True)
        good_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz"
        root_name = create_symbol_archive(good_archive)
        PACKAGE.validate_macos_symbol_archive_contents(
            good_archive,
            root_name,
            version="0.6.5",
            version_tag="0.6.5",
            arch="arm64",
            package_suffix="-opengl",
            runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
        )

        wrong_runtime_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-wrong-runtime.tar.xz"
        wrong_runtime_root_name = create_symbol_archive(
            wrong_runtime_archive,
            runtime_archive="openq4-0.6.5-macos-arm64-metal.dmg",
        )
        expect_runtime_error(
            "runtime_archive",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                wrong_runtime_archive,
                wrong_runtime_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with wrong runtime archive",
        )

        wrong_symbol_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-wrong-symbol.tar.xz"
        wrong_symbol_root_name = create_symbol_archive(
            wrong_symbol_archive,
            symbol_archive="openq4-0.6.5-macos-arm64-metal-symbols.tar.xz",
        )
        expect_runtime_error(
            "symbol_archive",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                wrong_symbol_archive,
                wrong_symbol_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with wrong symbol archive",
        )

        wrong_suffix_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-wrong-suffix.tar.xz"
        wrong_suffix_root_name = create_symbol_archive(wrong_suffix_archive, package_suffix="-metal")
        expect_runtime_error(
            "package_suffix",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                wrong_suffix_archive,
                wrong_suffix_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with wrong package suffix",
        )

        duplicate_header_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-duplicate-header.tar.xz"
        duplicate_header_root_name = create_symbol_archive(duplicate_header_archive, duplicate_key="runtime_archive")
        expect_runtime_error(
            "duplicate key",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                duplicate_header_archive,
                duplicate_header_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with duplicate header key",
        )

        unexpected_header_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-extra-header.tar.xz"
        unexpected_header_root_name = create_symbol_archive(unexpected_header_archive, duplicate_key="release_channel")
        expect_runtime_error(
            "unexpected header key",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                unexpected_header_archive,
                unexpected_header_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with unexpected header key",
        )

        unsafe_runtime_name_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-unsafe-runtime.tar.xz"
        unsafe_runtime_name_root_name = create_symbol_archive(
            unsafe_runtime_name_archive,
            runtime_archive="nested/openq4-0.6.5-macos-arm64-opengl.dmg",
        )
        expect_runtime_error(
            "unsafe archive filename",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                unsafe_runtime_name_archive,
                unsafe_runtime_name_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with path-like runtime archive name",
        )

        unsafe_symbol_name_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-unsafe-symbol.tar.xz"
        unsafe_symbol_name_root_name = create_symbol_archive(
            unsafe_symbol_name_archive,
            symbol_archive="nested/openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz",
        )
        expect_runtime_error(
            "unsafe archive filename",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                unsafe_symbol_name_archive,
                unsafe_symbol_name_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with path-like symbol archive name",
        )

        duplicate_binary_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-duplicate-binary.tar.xz"
        duplicate_binary_root_name = create_symbol_archive(
            duplicate_binary_archive,
            manifest_replacements=(("- path=openQ4-ded_arm64", "- path=openQ4-client_arm64"),),
        )
        expect_runtime_error(
            "duplicate binary entry",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                duplicate_binary_archive,
                duplicate_binary_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with duplicate binary path",
        )

        bad_hash_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-bad-hash.tar.xz"
        bad_hash_root_name = create_symbol_archive(
            bad_hash_archive,
            manifest_replacements=(("sha256=" + "0" * 64, "sha256=not-a-sha256"),),
        )
        expect_runtime_error(
            "invalid sha256",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                bad_hash_archive,
                bad_hash_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with invalid sha256",
        )

        bad_size_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-bad-size.tar.xz"
        bad_size_root_name = create_symbol_archive(
            bad_size_archive,
            manifest_replacements=(("  size=1", "  size=0"),),
        )
        expect_runtime_error(
            "invalid size",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                bad_size_archive,
                bad_size_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with invalid binary size",
        )

        bad_uuid_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-bad-uuid.tar.xz"
        bad_uuid_root_name = create_symbol_archive(
            bad_uuid_archive,
            manifest_replacements=(
                (
                    "macho_uuid=UUID: 00000000-0000-0000-0000-000000000001 (arm64) openQ4",
                    "macho_uuid=UUID: 00000000-0000-0000-0000-000000000001 (x86_64) openQ4",
                ),
            ),
        )
        expect_runtime_error(
            "invalid macho_uuid",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                bad_uuid_archive,
                bad_uuid_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with wrong Mach-O UUID architecture",
        )

        wrong_dsym_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-wrong-dsym.tar.xz"
        wrong_dsym_root_name = create_symbol_archive(
            wrong_dsym_archive,
            manifest_replacements=(
                ("dsym=dSYMs/openQ4-client_arm64.dSYM", "dsym=dSYMs/openQ4-ded_arm64.dSYM"),
            ),
        )
        expect_runtime_error(
            "dsym is",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                wrong_dsym_archive,
                wrong_dsym_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with wrong dSYM mapping",
        )

        missing_binary_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-missing-binary.tar.xz"
        missing_binary_root_name = create_symbol_archive(
            missing_binary_archive,
            manifest_replacements=(("- path=openQ4.app/Contents/Frameworks/game_arm64.dylib", "- path=openQ4.app/Contents/Frameworks/game-extra_arm64.dylib"),),
        )
        expect_runtime_error(
            "missing binary entries",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                missing_binary_archive,
                missing_binary_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with missing expected binary",
        )

        unexpected_binary_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-unexpected-binary.tar.xz"
        unexpected_binary_root_name = create_symbol_archive(
            unexpected_binary_archive,
            manifest_replacements=(
                (
                    "  dsym=dSYMs/game_arm64.dylib.dSYM",
                    "  dsym=dSYMs/game_arm64.dylib.dSYM\n"
                    "- path=baseoq4/game-extra_arm64.dylib\n"
                    f"  sha256={'5' * 64}\n"
                    "  size=1\n"
                    "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000006 (arm64) game-extra_arm64.dylib\n"
                    "  dsym=dSYMs/game-extra_arm64.dylib.dSYM",
                ),
            ),
        )
        expect_runtime_error(
            "unexpected binary entries",
            lambda: PACKAGE.validate_macos_symbol_archive_contents(
                unexpected_binary_archive,
                unexpected_binary_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            ),
            "symbol archive manifest with unexpected binary",
        )

        missing_archive = work / "missing-symbols.tar.xz"
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                missing_archive,
                root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "was not created" not in str(exc):
                raise AssertionError(f"unexpected symbol archive missing-input rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted a missing archive")

        linked_archive = work / "linked-symbols.tar.xz"
        try:
            linked_archive.symlink_to(good_archive)
        except (OSError, NotImplementedError):
            linked_archive = None
        if linked_archive is not None:
            try:
                PACKAGE.validate_macos_symbol_archive_contents(
                    linked_archive,
                    root_name,
                    version="0.6.5",
                    version_tag="0.6.5",
                    arch="arm64",
                    package_suffix="-opengl",
                    runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
                )
            except RuntimeError as exc:
                if "must not be a symlink" not in str(exc):
                    raise AssertionError(f"unexpected symbol archive symlink rejection: {exc}") from exc
            else:
                raise AssertionError("symbol archive validator accepted a symlinked archive")

        bad_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-bad.tar.xz"
        bad_root_name = create_symbol_archive(bad_archive, leak_runtime=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                bad_archive,
                bad_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "runtime payload entries" not in str(exc):
                raise AssertionError(f"unexpected symbol archive rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted leaked runtime binaries")

        metadata_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-metadata.tar.xz"
        metadata_root_name = create_symbol_archive(metadata_archive, metadata_sidecar=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                metadata_archive,
                metadata_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "metadata/debug entry" not in str(exc):
                raise AssertionError(f"unexpected symbol archive metadata rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted macOS metadata sidecars")

        duplicate_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-duplicate.tar.xz"
        duplicate_root_name = create_symbol_archive(duplicate_archive, duplicate_manifest=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                duplicate_archive,
                duplicate_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "duplicate entry" not in str(exc):
                raise AssertionError(f"unexpected symbol archive duplicate rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted a duplicate member")

        casefold_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-casefold.tar.xz"
        casefold_root_name = create_symbol_archive(casefold_archive, casefold_manifest=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                casefold_archive,
                casefold_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "case-insensitive duplicate" not in str(exc):
                raise AssertionError(f"unexpected symbol archive casefold rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted a case-insensitive duplicate member")

        previous_symbol_member_cap = PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS
        PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS = 4
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                good_archive,
                root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "too many members" not in str(exc):
                raise AssertionError(f"unexpected symbol archive member-count rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted too many members")
        finally:
            PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS = previous_symbol_member_cap

        previous_symbol_total_cap = PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES
        PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES = 4
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                good_archive,
                root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "total expanded size" not in str(exc):
                raise AssertionError(f"unexpected symbol archive total-size rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted excessive total expanded size")
        finally:
            PACKAGE.MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES = previous_symbol_total_cap

        huge_manifest_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-huge-manifest.tar.xz"
        huge_manifest_root_name = create_symbol_archive(huge_manifest_archive, huge_manifest=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                huge_manifest_archive,
                huge_manifest_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "metadata member is too large" not in str(exc):
                raise AssertionError(f"unexpected symbol archive oversized-manifest rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted an oversized manifest")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_symbol_output_preflights() -> None:
    work = make_work_root()
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True, exist_ok=True)

        existing_root = work / "existing-symbol-root"
        existing_root.mkdir()
        (existing_root / "stale.txt").write_text("stale\n", encoding="utf-8")
        PACKAGE.prepare_macos_symbol_staging_root(existing_root, work)
        if not existing_root.is_dir() or any(existing_root.iterdir()):
            raise AssertionError("symbol staging root was not cleaned to an empty directory")

        file_root = work / "symbol-root-file"
        file_root.write_text("not a directory\n", encoding="utf-8")
        expect_runtime_error(
            "symbol staging root exists but is not a directory",
            lambda: PACKAGE.prepare_macos_symbol_staging_root(file_root, work),
            "symbol staging root file",
        )

        real_output = work / "real-output"
        real_output.mkdir()
        output_link = work / "output-link"
        if make_symlink(real_output, output_link, target_is_directory=True):
            expect_runtime_error(
                "symbol output directory must not be a symlink",
                lambda: PACKAGE.prepare_macos_symbol_staging_root(output_link / "symbols", output_link),
                "symbol output directory symlink",
            )

        real_symbol_root = work / "real-symbol-root"
        real_symbol_root.mkdir()
        symbol_root_link = work / "symbol-root-link"
        if make_symlink(real_symbol_root, symbol_root_link, target_is_directory=True):
            expect_runtime_error(
                "symbol staging root must not be a symlink",
                lambda: PACKAGE.prepare_macos_symbol_staging_root(symbol_root_link, work),
                "symbol staging root symlink",
            )

        dsym_parent = work / "dsyms"
        dsym_path = dsym_parent / "openQ4.app.dSYM"
        dsym_path.parent.mkdir()
        dsym_path.mkdir()
        (dsym_path / "stale.txt").write_text("stale\n", encoding="utf-8")
        PACKAGE.prepare_macos_dsym_output_path(dsym_path)
        if dsym_path.exists():
            raise AssertionError("stale dSYM output directory was not removed before dsymutil")

        dsym_file = dsym_parent / "file.dSYM"
        dsym_file.write_text("not a directory\n", encoding="utf-8")
        expect_runtime_error(
            "dSYM output exists but is not a directory",
            lambda: PACKAGE.prepare_macos_dsym_output_path(dsym_file),
            "dSYM output file",
        )

        real_dsym = work / "real-dsym"
        real_dsym.mkdir()
        dsym_link = work / "linked.dSYM"
        if make_symlink(real_dsym, dsym_link, target_is_directory=True):
            expect_runtime_error(
                "dSYM output must not be a symlink",
                lambda: PACKAGE.prepare_macos_dsym_output_path(dsym_link),
                "dSYM output symlink",
            )

        tar_root = work / "tar-root"
        tar_root.mkdir()
        (tar_root / "SYMBOLS.txt").write_text("symbols\n", encoding="utf-8")
        tar_root_link = work / "tar-root-link"
        if make_symlink(tar_root, tar_root_link, target_is_directory=True):
            expect_runtime_error(
                "symbol archive input root must not be a symlink",
                lambda: PACKAGE.create_macos_symbol_tarball(tar_root_link, work / "out.tar.xz"),
                "symbol tarball input root symlink",
            )
        expect_runtime_error(
            "symbol archive output must not be inside the symbol staging root",
            lambda: PACKAGE.create_macos_symbol_tarball(tar_root, tar_root / "out.tar.xz"),
            "symbol tarball output under staging root",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_release_workflow() -> None:
    workflow = read(".github/workflows/manual-release.yml")

    for token in (
        "Runtime package archive name must not look like a symbol/debug artifact",
        "Expected macOS dSYM symbol archive not found",
        "symbols_archive_path=",
        "Missing macOS symbolication manifest",
        "macOS runtime package directory must not contain .dSYM bundles",
        "Upload macOS dSYM symbols",
        "steps.package.outputs.symbols_archive_path",
        "openq4-release-${{ needs.metadata.outputs.version_tag }}-macos-${{ matrix.binary_arch }}${{ matrix.package_suffix }}-symbols",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-unsigned-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-unsigned-symbols.tar.xz",
    ):
        require(workflow, token, "manual release macOS symbol workflow")


def validate_docs_and_support_collector() -> None:
    symbol_doc = read("docs/dev/macos-symbolication.md")
    support_doc = read("docs/user/macos-support-data.md")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    support_collector = read("tools/macos/collect_macos_support_info.sh")
    evidence = read("docs/dev/macos-signoff-evidence.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")

    for token in (
        "# macOS Symbolication Workflow",
        "`openq4-<version>-macos-arm64-opengl-symbols.tar.xz`",
        "`openq4-<version>-macos-arm64-metal-symbols.tar.xz`",
        "`openQ4.app/Contents/MacOS/openQ4`",
        "`openQ4-client_arm64`",
        "`openQ4-ded_arm64`",
        "`openQ4.app/Contents/Frameworks/game_arm64.dylib`",
        "`SYMBOLS.txt`",
        "SHA-256",
        "`dwarfdump --uuid`",
        "support collector copies it into `package/SYMBOLS.txt`",
        "without requiring any macOS platform test",
    ):
        require(symbol_doc, token, "macOS symbolication documentation")

    for source, context in (
        (support_doc, "macOS support-data guide"),
        (package_policy, "macOS package policy"),
        (evidence, "macOS signoff evidence"),
        (release_completion, "release completion"),
        (release_notes, "curated release notes"),
    ):
        require(source, "SYMBOLS.txt", context)
        require(source, "dSYM", context)

    for token in (
        '"SYMBOLS.txt"',
        'copy_text_if_present "${PACKAGE_ROOT}/SYMBOLS.txt" "package/SYMBOLS.txt"',
        "package/SYMBOLS.txt",
    ):
        require(support_collector, token, "macOS support collector symbol manifest")


def validate_plan_status() -> None:
    plan = read("docs/dev/archive/2026-10-18/plans/2026-06-30-apple-support-no-macos-access.md")

    for token in (
        "## Phase 4: Make Symbolication Possible",
        "- [x] Add a macOS release/debug symbol artifact that is separate from the",
        "- [x] Keep `.dSYM` bundles out of runtime DMG/tarball packages.",
        "- [x] Ensure symbol artifacts cover:",
        "`openQ4.app/Contents/MacOS/openQ4`",
        "loose `openQ4-client_arm64`",
        "loose `openQ4-ded_arm64`",
        "`openQ4.app/Contents/Frameworks/game-sp_arm64.dylib`",
        "`openQ4.app/Contents/Frameworks/game-mp_arm64.dylib`",
        "- [x] Document how to pair a user `.ips` report with the correct symbol archive.",
        "- [x] Add package-script tests that symbol archives cannot be mistaken for",
        "Phase 4 implementation status",
        "tools/tests/macos_symbolication_policy.py",
    ):
        require(plan, token, "Phase 4 Apple support plan")


def validate_ci_and_local_wiring() -> None:
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_symbolication_policy.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_symbolication_policy.py", context)


def validate_discord_downloads_do_not_promote_symbols() -> None:
    announcer = read(".github/scripts/announce-release-discord.mjs")
    reject(announcer, "symbols.tar.xz\", \"macOS", "Discord download links should not promote debug symbols as runtime downloads")


def main() -> None:
    validate_packager_contract()
    validate_symbol_archive_fixture()
    validate_symbol_output_preflights()
    validate_release_workflow()
    validate_docs_and_support_collector()
    validate_plan_status()
    validate_ci_and_local_wiring()
    validate_discord_downloads_do_not_promote_symbols()
    print("macos_symbolication_policy: ok")


if __name__ == "__main__":
    main()
