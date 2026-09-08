#!/usr/bin/env python3
"""Create curated release distributable archives for openQ4."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import unicodedata
from pathlib import Path
from zipfile import ZIP_DEFLATED, BadZipFile, ZipFile, ZipInfo

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from windows_runtime import (
    RuntimeFlavor,
    infer_runtime_flavor,
    list_staged_runtime_files,
)
from linux_metadata import (
    LinuxMetadataError,
    desktop_entry_exec as parse_linux_desktop_entry_exec,
    desktop_exec_command as parse_linux_desktop_exec_command,
)
from generate_release_docs import GeneratedDocSite, generate_release_docs_site
from gamelibs_stage_path import MANIFEST_NAME as GAMELIBS_STAGE_MANIFEST_NAME, stage_root as gamelibs_stage_root
from openq4_pak import (
    OPENQ4_PK4_FORBIDDEN_FILES,
    OPENQ4_PACK_NAMES,
    OPENQ4_REQUIRED_LOOSE_GAME_FILES,
    PAK0_NAME,
    copy_file_if_changed,
    copy_game_pk4,
    create_game_pk4 as create_openq4_game_pk4,
    is_relative_to,
)


PRODUCT_NAME = "openQ4"
GAME_DIR_NAME = "baseoq4"
RELEASE_README_PATH = Path("assets") / "release" / "README.html"
LICENSE_PATH = Path("LICENSE")
MACOS_SUPPORT_INFO_SCRIPT_PATH = Path("tools") / "macos" / "collect_macos_support_info.sh"
MACOS_SUPPORT_INFO_SCRIPT_NAME = "collect_macos_support_info.sh"
MACOS_SYMBOL_MANIFEST_NAME = "SYMBOLS.txt"
MACOS_SYMBOL_ARCHIVE_SUFFIX = ".tar.xz"
SUPPORTED_ARCHES = ("x64", "x86", "arm64", "universal2")

PLATFORM_EXECUTABLE_EXT = {
    "windows": ".exe",
    "linux": "",
    "macos": "",
}

PLATFORM_GAME_MODULE_EXT = {
    "windows": ".dll",
    "linux": ".so",
    "macos": ".dylib",
}

DEFAULT_ARCHIVE_FORMAT = {
    "windows": "zip",
    "linux": "tar.xz",
    "macos": "dmg",
}

ARCHIVE_SUFFIX = {
    "dmg": ".dmg",
    "zip": ".zip",
    "tar.gz": ".tar.gz",
    "tar.xz": ".tar.xz",
}

# Shared macOS compatibility floor: written to Info.plist LSMinimumSystemVersion
# and enforced against each packaged binary's Mach-O minimum-OS load command.
MACOS_MIN_SYSTEM_VERSION = "11.0"
MACOS_RUNTIME_LAYOUT_KEY = "OpenQ4RuntimeLayout"
MACOS_RUNTIME_LAYOUT_VALUE = "self-contained-v1"
MACOS_EXPECTED_PLIST_VALUES = {
    "CFBundleExecutable": "openQ4",
    "CFBundleDisplayName": "openQ4",
    "CFBundleIconFile": "openQ4.icns",
    "CFBundleIdentifier": "com.darkmatter.openq4",
    "CFBundleName": "openQ4",
    "CFBundlePackageType": "APPL",
    "LSMinimumSystemVersion": "11.0",
    "LSApplicationCategoryType": "public.app-category.games",
    "NSPrincipalClass": "NSApplication",
    MACOS_RUNTIME_LAYOUT_KEY: MACOS_RUNTIME_LAYOUT_VALUE,
}
MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES = (
    "/System/Library/",
    "/usr/lib/",
)
MACOS_FORBIDDEN_XATTRS = (
    "com.apple.quarantine",
)
MACOS_FORBIDDEN_ARCHIVE_NAMES = (
    ".DS_Store",
    "__MACOSX",
    ".fseventsd",
    ".Spotlight-V100",
    ".Trashes",
    "Icon\r",
)
MACOS_FORBIDDEN_ARCHIVE_PREFIXES = (
    "._",
)
MACOS_FORBIDDEN_ARCHIVE_SUFFIXES = (
    ".dSYM",
)
MACOS_PKGINFO_BYTES = b"APPL????"
MACOS_LOCALIZED_INFO_LOCALES = (
    "English",
    "French",
)
MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME = "OpenQ4PackageRoot.strings"
MACOS_PACKAGE_ROOT_ERROR_STRINGS = {
    "English": {
        "OpenQ4PackageRootMissingTitle": "openQ4.app adjacent package root is incomplete",
        "OpenQ4PackageRootMissingBody": "This legacy package layout needs openQ4.app, baseoq4/, openQ4-client_<arch>, and openQ4-ded_<arch> together in the same package folder. Current self-contained packages support moving only openQ4.app to /Applications; reinstall this package to use that layout.",
        "OpenQ4BundleRuntimeMissingTitle": "openQ4.app is incomplete",
        "OpenQ4BundleRuntimeMissingBody": "Reinstall the complete openQ4.app. Its game data and signed game modules must remain inside the application bundle.",
    },
    "French": {
        "OpenQ4PackageRootMissingTitle": "La racine de paquet adjacente a openQ4.app est incomplete",
        "OpenQ4PackageRootMissingBody": "Cette ancienne disposition de paquet necessite que openQ4.app, baseoq4/, openQ4-client_<arch> et openQ4-ded_<arch> restent ensemble dans le meme dossier. Les paquets autonomes actuels permettent de deplacer seulement openQ4.app vers /Applications ; reinstallez ce paquet pour utiliser cette disposition.",
        "OpenQ4BundleRuntimeMissingTitle": "openQ4.app est incomplete",
        "OpenQ4BundleRuntimeMissingBody": "Reinstallez l'application openQ4.app complete. Ses donnees de jeu et ses modules de jeu signes doivent rester dans l'application.",
    },
}
# macOS renderer payload. The Vulkan renderer module runs on macOS through
# MoltenVK, a Vulkan-on-Metal translation layer that openQ4 does not build and
# that meson therefore never installs. tools/build/prepare_macos_moltenvk.sh
# stages the pinned universal libMoltenVK.dylib into the install tree; both files
# are then relocated into openQ4.app/Contents/Frameworks beside the game modules.
MACOS_MOLTENVK_DYLIB_NAME = "libMoltenVK.dylib"
MACOS_MOLTENVK_PREPARE_SCRIPT = "tools/build/prepare_macos_moltenvk.sh"
MACOS_OPENAL_SOFT_DYLIB_NAME = "libopenal.1.dylib"
MACOS_OPENAL_SOFT_INSTALL_NAME = f"@rpath/{MACOS_OPENAL_SOFT_DYLIB_NAME}"
MACOS_OPENAL_SOFT_PREPARE_SCRIPT = "tools/build/prepare_macos_openal_soft.sh"
MACOS_OPENAL_SOFT_LICENSE_FILES = (
    "COPYING",
    "LICENSE-pffft",
    "LICENSE-fmt",
    "LICENSE-gsl",
    "SOURCE.md",
    "openal-soft-1.25.1.tar.gz",
)
MACOS_APP_FRAMEWORKS_DIR = Path("Contents") / "Frameworks"
MACOS_APP_RESOURCES_DIR = Path("Contents") / "Resources"
MACOS_APP_OPENAL_LICENSE_DIR = MACOS_APP_RESOURCES_DIR / "licenses" / "openal-soft"
MACOS_APP_GAME_DATA_DIR = MACOS_APP_RESOURCES_DIR / GAME_DIR_NAME
MACOS_APP_SPLASH_DIR = MACOS_APP_RESOURCES_DIR / "assets" / "splash"
MACOS_EXPECTED_APP_BUNDLE_DIRS = (
    "Contents",
    "Contents/Frameworks",
    "Contents/MacOS",
    "Contents/Resources",
    f"Contents/Resources/{GAME_DIR_NAME}",
    "Contents/Resources/assets",
    "Contents/Resources/assets/splash",
    "Contents/Resources/licenses",
    "Contents/Resources/licenses/openal-soft",
    "Contents/Resources/English.lproj",
    "Contents/Resources/French.lproj",
)
MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_DIRS = (
    "Contents/_CodeSignature",
)
MACOS_EXPECTED_APP_BUNDLE_FILES = (
    "Contents/Info.plist",
    "Contents/PkgInfo",
    "Contents/MacOS/openQ4",
    "Contents/Resources/openQ4.icns",
    "Contents/Resources/VERSION.txt",
    f"Contents/Resources/{GAME_DIR_NAME}/mod.json",
    f"Contents/Resources/{GAME_DIR_NAME}/pak0.pk4",
    f"Contents/Resources/{GAME_DIR_NAME}/pak1.pk4",
    "Contents/Resources/assets/splash/quake4_rt_bitmap_4001.bmp",
    "Contents/Resources/licenses/openal-soft/COPYING",
    "Contents/Resources/licenses/openal-soft/LICENSE-pffft",
    "Contents/Resources/licenses/openal-soft/LICENSE-fmt",
    "Contents/Resources/licenses/openal-soft/LICENSE-gsl",
    "Contents/Resources/licenses/openal-soft/SOURCE.md",
    "Contents/Resources/licenses/openal-soft/openal-soft-1.25.1.tar.gz",
    "Contents/Resources/English.lproj/InfoPlist.strings",
    "Contents/Resources/French.lproj/InfoPlist.strings",
    f"Contents/Resources/English.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
    f"Contents/Resources/French.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
)
MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_FILES = (
    "Contents/_CodeSignature/CodeResources",
)
MAX_MACOS_METADATA_MEMBER_BYTES = 64 * 1024
MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES = 256 * 1024
MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS = 4096
MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS = 512
MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES = 8 * 1024 * 1024 * 1024
MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES = 2 * 1024 * 1024 * 1024
MACOS_TAR_ARCHIVE_READ_MODES = {
    "tar.gz": "r:gz",
    "tar.xz": "r:xz",
}
MACOS_SUPPORT_INFO_REQUIRED_TOKENS = (
    "#!/bin/sh",
    "OPENQ4_PACKAGE_ROOT",
    "HOME_DIR=${HOME:-}",
    "ARCHIVE_TMP",
    "MAX_SUPPORT_TEXT_BYTES",
    "MAX_CRASH_REPORT_BYTES",
    "MAX_SUPPORT_ARCHIVE_BYTES",
    "write_bounded_report()",
    "write_openq4_log_candidate_paths()",
    "write_openq4_renderer_config_candidate_paths()",
    "sanitize_text()",
    "redact_text()",
    "limit_stream_tail()",
    "Support report output is limited to the final",
    "contains_control_chars()",
    "Support package root must not contain control characters",
    "Support output directory must not contain control characters",
    "HOME was not set; home-scoped openq4.log files were skipped.",
    "HOME was not set; saved openQ4Config.cfg paths were skipped.",
    "logs/renderer-config.txt",
    "Only renderer and performance settings are copied",
    "HOME was not set; the macOS DiagnosticReports directory could not be located.",
    ".XXXXXX.tar.gz.tmp",
    "does not dump the environment",
    "does not launch openQ4",
    "does not copy retail q4base PK4 assets",
    "truncated copy failed; source was not copied",
    "COPYFILE_DISABLE=1 tar -czf",
    "COPYFILE_DISABLE=1 tar -tzf",
    "Support archive is empty or unreadable before publish",
    "Support archive validation failed before publish",
    "Support archive is too large before publish",
    'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"',
)
MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS = (
    "printenv",
    "env >",
    "set >",
    "openQ4-client_arm64 >",
    "openQ4-client_arm64 2>",
    "openQ4-client_x64 >",
    "openQ4-client_x64 2>",
    "openQ4-client_x86 >",
    "openQ4-client_x86 2>",
    "openQ4-ded_arm64 >",
    "openQ4-ded_arm64 2>",
    "openQ4-ded_x64 >",
    "openQ4-ded_x64 2>",
    "openQ4-ded_x86 >",
    "openQ4-ded_x86 2>",
    "xattr -l",
    "xattr -p",
    "xattr -w",
    "|| cat",
    'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat',
)
DETERMINISTIC_ARCHIVE_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
DETERMINISTIC_TAR_MTIME = 0

MACOS_LIPO_ARCHES = {
    "arm64": frozenset(("arm64",)),
    "x64": frozenset(("x86_64",)),
    "x86": frozenset(("i386",)),
    "universal2": frozenset(("arm64", "x86_64")),
}
MACOS_SIGNING_MODES = (
    "ad-hoc",
    "developer-id",
)
VERSION_REPOSITORY_METADATA_KEYS = (
    "openq4_commit",
    "openq4_dirty",
    "openq4_game_commit",
    "openq4_game_dirty",
)
MACOS_FORBIDDEN_ENTITLEMENTS = {
    "com.apple.security.app-sandbox": (
        "openQ4 direct-distribution packages are not App Sandbox-ready because "
        "they need explicit access to user-selected Quake 4 assets, saves, logs, "
        "and staged runtime overlays"
    ),
    "com.apple.security.get-task-allow": (
        "get-task-allow is a development/debug entitlement and must not be present "
        "in release signing inputs"
    ),
}


class MacOSSigningConfig:
    def __init__(
        self,
        *,
        mode: str,
        identity: str,
        hardened_runtime: bool,
        timestamp: bool,
        entitlements: Path | None,
        notarize: bool,
        notary_keychain_profile: str,
        notary_keychain: Path | None,
    ) -> None:
        self.mode = mode
        self.identity = identity
        self.hardened_runtime = hardened_runtime
        self.timestamp = timestamp
        self.entitlements = entitlements
        self.notarize = notarize
        self.notary_keychain_profile = notary_keychain_profile
        self.notary_keychain = notary_keychain


def env_flag(name: str) -> bool:
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package openQ4 release artifacts into a release archive."
    )
    parser.add_argument(
        "--platform",
        required=True,
        choices=sorted(PLATFORM_GAME_MODULE_EXT.keys()),
        help="Target runner platform (windows/linux/macos).",
    )
    parser.add_argument(
        "--arch",
        default="x64",
        choices=SUPPORTED_ARCHES,
        help="Target binary architecture tag (default: x64).",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="Human-readable release version string.",
    )
    parser.add_argument(
        "--version-tag",
        required=True,
        help="File-safe release version tag.",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root.",
    )
    parser.add_argument(
        "--install-dir",
        default=None,
        help="Install directory to package (defaults to <source-root>/.install).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Output directory for the generated package artifacts.",
    )
    parser.add_argument(
        "--archive-format",
        choices=sorted(ARCHIVE_SUFFIX.keys()),
        default=None,
        help=(
            "Archive format for the top-level package (default: platform-specific; "
            "windows=zip, linux=tar.xz, macos=dmg)."
        ),
    )
    parser.add_argument(
        "--package-suffix",
        default="",
        help=(
            "Optional file-name suffix appended after the platform/arch stem, "
            "for release variants such as -opengl or -metal."
        ),
    )
    parser.add_argument(
        "--allow-missing-binaries",
        action="store_true",
        help=(
            "Allow packaging to continue when required platform binaries are missing. "
            "Useful for host bring-up previews."
        ),
    )
    parser.add_argument(
        "--macos-signing-mode",
        choices=MACOS_SIGNING_MODES,
        default=os.environ.get("OPENQ4_MACOS_SIGNING_MODE", "ad-hoc"),
        help=(
            "macOS signing mode. Use ad-hoc for local/debug packaging and developer-id "
            "for release packages that will be notarized."
        ),
    )
    parser.add_argument(
        "--macos-code-sign-identity",
        default=os.environ.get("OPENQ4_MACOS_CODE_SIGN_IDENTITY", ""),
        help="Developer ID Application identity for --macos-signing-mode=developer-id.",
    )
    parser.add_argument(
        "--macos-entitlements",
        default=os.environ.get("OPENQ4_MACOS_ENTITLEMENTS", ""),
        help="Optional entitlements plist passed to codesign for macOS release signing.",
    )
    parser.add_argument(
        "--macos-notarize",
        action="store_true",
        default=env_flag("OPENQ4_MACOS_NOTARIZE"),
        help="Submit the signed macOS package payload to Apple notarization and staple the app ticket.",
    )
    parser.add_argument(
        "--macos-notary-keychain-profile",
        default=os.environ.get("OPENQ4_MACOS_NOTARY_KEYCHAIN_PROFILE", ""),
        help="notarytool keychain profile used when --macos-notarize is enabled.",
    )
    parser.add_argument(
        "--macos-notary-keychain",
        default=os.environ.get("OPENQ4_MACOS_NOTARY_KEYCHAIN", ""),
        help="Optional keychain path that contains the notarytool keychain profile.",
    )
    return parser.parse_args(argv[1:])


def normalize_package_suffix(raw_suffix: str) -> str:
    suffix = raw_suffix.strip()
    if suffix == "":
        return ""
    if not suffix.startswith("-"):
        suffix = "-" + suffix
    if not re.fullmatch(r"-[A-Za-z0-9][A-Za-z0-9._-]*", suffix):
        raise ValueError(
            "package suffix must be empty or a file-name-safe value such as -opengl or -metal"
        )
    return suffix


PACKAGE_VERSION_RE = re.compile(
    r"^[0-9]+(?:\.[0-9]+){2}(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?(?:\+[0-9A-Za-z][0-9A-Za-z.-]*)?$"
)


def validate_package_version(value: str) -> str:
    version = value.strip()
    if PACKAGE_VERSION_RE.fullmatch(version) is None:
        raise ValueError(
            "package version must be a semver-style value without spaces, slashes, or control characters"
        )
    return version


def validate_package_path_token(value: str, label: str) -> str:
    token = value.strip()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", token):
        raise ValueError(f"{label} must be a file-name-safe token without slashes or spaces")
    return token


def resolve_package_root(output_dir: Path, package_stem: str) -> Path:
    raw_package_root = output_dir / package_stem
    if raw_package_root.is_symlink():
        raise ValueError(f"package root must not be a symlink: {raw_package_root}")
    if raw_package_root.exists() and not raw_package_root.is_dir():
        raise ValueError(f"package root exists but is not a directory: {raw_package_root}")

    package_root = raw_package_root.resolve()
    resolved_output_dir = output_dir.resolve()
    if package_root == resolved_output_dir or not is_relative_to(package_root, resolved_output_dir):
        raise ValueError(f"package root escapes output directory: {package_root}")
    return package_root


def require_package_directory(path: Path, label: str, *, must_exist: bool) -> Path:
    if path.is_symlink():
        raise ValueError(f"{label} must not be a symlink: {path}")
    if path.exists() and not path.is_dir():
        raise ValueError(f"{label} exists but is not a directory: {path}")
    if must_exist and not path.is_dir():
        raise ValueError(f"{label} not found: {path}")
    return path.resolve()


def prepare_archive_output_path(archive_path: Path, label: str) -> None:
    if archive_path.parent.is_symlink():
        raise RuntimeError(f"{label} parent must not be a symlink: {archive_path.parent}")
    if archive_path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {archive_path}")
    if archive_path.exists():
        if archive_path.is_dir():
            raise RuntimeError(f"{label} path is a directory: {archive_path}")
        if not archive_path.is_file():
            raise RuntimeError(f"{label} path is not a regular file: {archive_path}")
        archive_path.unlink()


def validate_archive_output_outside_input_tree(input_root: Path, archive_path: Path, label: str) -> None:
    try:
        if is_relative_to(archive_path.resolve(strict=False), input_root.resolve()):
            raise RuntimeError(f"{label} must not be inside the archive input tree: {archive_path}")
    except OSError as exc:
        raise RuntimeError(f"{label} path could not be resolved safely: {exc}") from exc


def prepare_macos_symbol_staging_root(symbol_root: Path, output_dir: Path) -> None:
    if output_dir.is_symlink():
        raise RuntimeError(f"macOS symbol output directory must not be a symlink: {output_dir}")
    if symbol_root.is_symlink():
        raise RuntimeError(f"macOS symbol staging root must not be a symlink: {symbol_root}")
    if symbol_root.exists():
        if not symbol_root.is_dir():
            raise RuntimeError(f"macOS symbol staging root exists but is not a directory: {symbol_root}")
        shutil.rmtree(symbol_root)
    symbol_root.mkdir(parents=True, exist_ok=True)
    if symbol_root.is_symlink() or not symbol_root.is_dir():
        raise RuntimeError(f"macOS symbol staging root must be a real directory: {symbol_root}")


def prepare_macos_dsym_output_path(dsym_path: Path) -> None:
    if dsym_path.parent.is_symlink():
        raise RuntimeError(f"macOS dSYM output parent must not be a symlink: {dsym_path.parent}")
    if dsym_path.is_symlink():
        raise RuntimeError(f"macOS dSYM output must not be a symlink: {dsym_path}")
    if dsym_path.exists():
        if not dsym_path.is_dir():
            raise RuntimeError(f"macOS dSYM output exists but is not a directory: {dsym_path}")
        shutil.rmtree(dsym_path)
    dsym_path.parent.mkdir(parents=True, exist_ok=True)
    if dsym_path.parent.is_symlink() or not dsym_path.parent.is_dir():
        raise RuntimeError(f"macOS dSYM output parent must be a real directory: {dsym_path.parent}")


def copy_regular_file(source: Path, destination: Path) -> None:
    if source.is_symlink():
        raise RuntimeError(f"refusing to package symlinked file: {source}")
    if not source.is_file():
        raise FileNotFoundError(f"required package file not found: {source}")
    copy_file_if_changed(source, destination)


def copy_regular_tree(source_root: Path, destination_root: Path) -> None:
    if source_root.is_symlink():
        raise RuntimeError(f"refusing to package symlinked directory: {source_root}")
    if not source_root.is_dir():
        raise FileNotFoundError(f"required package directory not found: {source_root}")

    for path in sorted(source_root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"refusing to package symlink from tree: {path}")
        relative_path = path.relative_to(source_root)
        destination = destination_root / relative_path
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            copy_regular_file(path, destination)
        else:
            raise RuntimeError(f"refusing to package non-regular file from tree: {path}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def get_required_root_binaries(platform: str, arch: str) -> tuple[str, str]:
    exe_ext = PLATFORM_EXECUTABLE_EXT[platform]
    return (
        f"{PRODUCT_NAME}-client_{arch}{exe_ext}",
        f"{PRODUCT_NAME}-ded_{arch}{exe_ext}",
    )


def get_required_renderer_module_binaries(platform: str, arch: str) -> tuple[str, ...]:
    if platform == "macos":
        # darwin builds only the Vulkan renderer module: build_renderer_gl stays
        # off there because the macOS client still links its OpenGL renderer
        # statically. Requiring renderer-gl here would fail every macOS package.
        return (f"renderer-vk_{arch}{PLATFORM_GAME_MODULE_EXT[platform]}",)

    if platform not in ("windows", "linux"):
        return ()

    module_ext = PLATFORM_GAME_MODULE_EXT[platform]
    return (
        f"renderer-gl_{arch}{module_ext}",
        f"renderer-vk_{arch}{module_ext}",
    )


def get_required_game_module_binaries(platform: str, arch: str) -> tuple[str, str]:
    module_ext = PLATFORM_GAME_MODULE_EXT[platform]
    return (
        f"game-sp_{arch}{module_ext}",
        f"game-mp_{arch}{module_ext}",
    )


def get_required_windows_root_symbols(arch: str) -> tuple[str, str]:
    return (
        f"{PRODUCT_NAME}-client_{arch}.pdb",
        f"{PRODUCT_NAME}-ded_{arch}.pdb",
    )


def get_required_windows_game_symbols(arch: str) -> tuple[str, str]:
    return (
        f"game-sp_{arch}.pdb",
        f"game-mp_{arch}.pdb",
    )


def write_text_file(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        path.unlink()
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def ensure_posix_executable(path: Path) -> None:
    if os.name == "nt":
        return
    os.chmod(path, path.stat().st_mode | 0o755)


def macos_forbidden_xattrs(path: Path) -> list[str]:
    try:
        names = os.listxattr(path)
    except (AttributeError, OSError):
        return []
    return sorted(name for name in names if name in MACOS_FORBIDDEN_XATTRS)


def strip_macos_forbidden_xattrs(root: Path) -> None:
    if sys.platform != "darwin":
        return

    for path in [root, *root.rglob("*")]:
        for attr in macos_forbidden_xattrs(path):
            try:
                os.removexattr(path, attr)
            except OSError as exc:
                raise RuntimeError(f"failed to remove {attr} from macOS package path: {path}") from exc


def validate_no_macos_forbidden_xattrs(root: Path) -> None:
    offenders: list[tuple[Path, list[str]]] = []
    for path in [root, *root.rglob("*")]:
        bad_attrs = macos_forbidden_xattrs(path)
        if bad_attrs:
            offenders.append((path, bad_attrs))

    if offenders:
        joined = ", ".join(
            f"{path.relative_to(root).as_posix() or '.'}: {','.join(attrs)}"
            for path, attrs in offenders[:10]
        )
        raise RuntimeError(f"macOS package contains forbidden extended attributes: {joined}")


def is_macos_metadata_sidecar_path(path: Path) -> bool:
    for part in path.parts:
        normalized_part = part.lower()
        if any(normalized_part == name.lower() for name in MACOS_FORBIDDEN_ARCHIVE_NAMES):
            return True
        if any(normalized_part.startswith(prefix.lower()) for prefix in MACOS_FORBIDDEN_ARCHIVE_PREFIXES):
            return True
    return False


def is_macos_non_runtime_metadata_path(path: Path) -> bool:
    if is_macos_metadata_sidecar_path(path):
        return True
    for part in path.parts:
        normalized_part = part.lower()
        if any(normalized_part.endswith(suffix.lower()) for suffix in MACOS_FORBIDDEN_ARCHIVE_SUFFIXES):
            return True
    return False


def validate_no_macos_metadata_artifacts(root: Path) -> None:
    offenders = [
        path
        for path in root.rglob("*")
        if is_macos_non_runtime_metadata_path(path.relative_to(root))
    ]
    if offenders:
        joined = ", ".join(path.relative_to(root).as_posix() for path in offenders[:10])
        raise RuntimeError(f"macOS package contains non-runtime metadata/debug entries: {joined}")


def macos_casefold_path_key(path: str) -> str:
    return unicodedata.normalize("NFC", path).casefold()


def validate_no_macos_casefold_path_collisions(root: Path) -> None:
    seen_paths: dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        key = macos_casefold_path_key(relative)
        previous = seen_paths.get(key)
        if previous is not None and previous != relative:
            raise RuntimeError(
                "macOS package contains case-insensitive duplicate paths: "
                f"{previous}, {relative}"
            )
        seen_paths[key] = relative


def validate_no_package_symlinks(root: Path) -> None:
    symlinks = [path for path in root.rglob("*") if path.is_symlink()]
    if symlinks:
        joined = ", ".join(path.relative_to(root).as_posix() for path in symlinks[:10])
        raise RuntimeError(f"macOS package contains symlink entries: {joined}")


def validate_no_package_special_files(root: Path) -> None:
    if os.name == "nt":
        return

    special_files = [
        path
        for path in root.rglob("*")
        if not path.is_symlink() and not path.is_file() and not path.is_dir()
    ]
    if special_files:
        joined = ", ".join(path.relative_to(root).as_posix() for path in special_files[:10])
        raise RuntimeError(f"macOS package contains special file entries: {joined}")


def validate_macos_package_file_modes(root: Path) -> None:
    if os.name == "nt":
        return

    offenders: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        mode = path.stat().st_mode & 0o7777
        if mode & 0o7000:
            offenders.append(f"{path.relative_to(root).as_posix()} has special mode bits {mode:o}")
        elif mode & 0o022:
            offenders.append(f"{path.relative_to(root).as_posix()} is group/other writable ({mode:o})")

    if offenders:
        raise RuntimeError("macOS package contains unsafe file modes: " + ", ".join(offenders[:10]))


def validate_macos_dmg_source_tree(package_root: Path) -> None:
    validate_no_package_symlinks(package_root)
    validate_no_package_special_files(package_root)
    validate_macos_package_file_modes(package_root)
    validate_macos_package_support_collector(package_root)
    validate_no_macos_metadata_artifacts(package_root)
    validate_no_macos_casefold_path_collisions(package_root)
    validate_no_macos_forbidden_xattrs(package_root)


def is_macos_root_engine_binary_name(name: str) -> bool:
    return name.startswith(f"{PRODUCT_NAME}-client_") or name.startswith(f"{PRODUCT_NAME}-ded_")


def validate_macos_package_root_engine_binaries(package_root: Path, arch: str) -> None:
    expected_names = set(get_required_root_binaries("macos", arch))
    offenders = sorted(
        path.name
        for path in package_root.iterdir()
        if path.is_file()
        and is_macos_root_engine_binary_name(path.name)
        and path.name not in expected_names
    )
    if offenders:
        joined = ", ".join(offenders[:10])
        raise RuntimeError(f"macOS package contains stale or mismatched root engine binaries: {joined}")


def parse_version_manifest_bytes(data: bytes, label: str) -> dict[str, str]:
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} version manifest is not UTF-8") from exc

    if not lines or lines[0].strip() != PRODUCT_NAME:
        raise RuntimeError(f"{label} version manifest has invalid product header")

    values: dict[str, str] = {}
    for line in lines[1:]:
        if not line.strip():
            continue
        if "=" not in line:
            raise RuntimeError(f"{label} version manifest contains malformed line: {line!r}")
        key, value = line.split("=", 1)
        key = key.strip()
        if key == "":
            raise RuntimeError(f"{label} version manifest contains an empty key")
        if key in values:
            raise RuntimeError(f"{label} version manifest contains duplicate key: {key}")
        values[key] = value.strip()
    return values


def validate_version_manifest_bytes(
    data: bytes,
    label: str,
    *,
    version: str,
    version_tag: str,
    platform: str,
    arch: str,
) -> None:
    validate_macos_metadata_bytes_size(data, f"{label} version manifest")
    values = parse_version_manifest_bytes(data, label)
    expected = {
        "version": version,
        "version_tag": version_tag,
        "platform": platform,
        "arch": arch,
    }
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise RuntimeError(
                f"{label} version manifest {key} is {actual!r}; expected {expected_value!r}"
            )


def validate_macos_metadata_bytes_size(data: bytes, label: str) -> None:
    if len(data) > MAX_MACOS_METADATA_MEMBER_BYTES:
        raise RuntimeError(f"{label} is too large ({len(data)} bytes)")


def validate_macos_support_info_script_bytes(data: bytes, label: str) -> None:
    if len(data) > MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES:
        raise RuntimeError(f"{label} is too large ({len(data)} bytes)")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} is not UTF-8") from exc
    if "\x00" in text:
        raise RuntimeError(f"{label} contains NUL bytes")
    if "\r" in text:
        raise RuntimeError(f"{label} contains CRLF or carriage returns")
    for token in MACOS_SUPPORT_INFO_REQUIRED_TOKENS:
        if token not in text:
            raise RuntimeError(f"{label} is missing required marker {token!r}")
    for token in MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS:
        if token in text:
            raise RuntimeError(f"{label} contains forbidden privacy/no-launch pattern {token!r}")


def validate_macos_package_support_collector(package_root: Path) -> None:
    script_path = package_root / MACOS_SUPPORT_INFO_SCRIPT_NAME
    if not script_path.is_file():
        raise RuntimeError(f"macOS package support collector is missing: {script_path}")
    if not os.access(script_path, os.X_OK):
        raise RuntimeError(f"macOS package support collector is not executable: {script_path}")
    try:
        script_bytes = script_path.read_bytes()
    except OSError as exc:
        raise RuntimeError(f"macOS package support collector is unreadable: {script_path}") from exc
    validate_macos_support_info_script_bytes(script_bytes, "macOS package support collector")


def validate_macos_code_resources_bytes(data: bytes, label: str) -> None:
    validate_macos_metadata_bytes_size(data, label)
    if not data:
        raise RuntimeError(f"{label} is empty")
    try:
        values = plistlib.loads(data)
    except (plistlib.InvalidFileException, TypeError, ValueError) as exc:
        raise RuntimeError(f"{label} is not a valid plist") from exc
    if not isinstance(values, dict):
        raise RuntimeError(f"{label} must contain a dictionary root")


def parse_macos_localized_info_strings(data: bytes, label: str) -> dict[str, str]:
    validate_macos_metadata_bytes_size(data, label)
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} is not UTF-8") from exc

    values: dict[str, str] = {}
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or (line.startswith("/*") and line.endswith("*/")):
            continue
        match = re.fullmatch(r'([A-Za-z0-9_]+)\s*=\s*"([^"\\]*)";', line)
        if match is None:
            raise RuntimeError(f"{label} contains malformed line {line_number}: {raw_line!r}")
        key, value = match.groups()
        if key in values:
            raise RuntimeError(f"{label} contains duplicate key: {key}")
        values[key] = value
    return values


def validate_macos_localized_info_bytes(data: bytes, label: str, version: str) -> None:
    values = parse_macos_localized_info_strings(data, label)

    expected_values = {
        "CFBundleName": "openQ4",
        "CFBundleShortVersionString": version,
        "CFBundleGetInfoString": f"openQ4 version {version}, Copyright 2026 DarkMatter Productions",
        "NSHumanReadableCopyright": "Copyright 2026 DarkMatter Productions",
    }
    for key, expected in expected_values.items():
        actual = values.get(key)
        if actual != expected:
            raise RuntimeError(f"{label} {key} is {actual!r}; expected {expected!r}")


def validate_macos_package_root_error_bytes(data: bytes, label: str, locale: str) -> None:
    values = parse_macos_localized_info_strings(data, label)
    expected_values = MACOS_PACKAGE_ROOT_ERROR_STRINGS.get(locale)
    if expected_values is None:
        raise RuntimeError(f"{label} has no expected localized package-root error strings")

    for key, expected in expected_values.items():
        actual = values.get(key)
        if actual != expected:
            raise RuntimeError(f"{label} {key} is {actual!r}; expected {expected!r}")


def macos_package_version_tag_from_name(package_root: Path, arch: str) -> str:
    name = package_root.name
    prefix = "openq4-"
    marker = f"-macos-{arch}"
    if not name.startswith(prefix) or marker not in name:
        raise RuntimeError(f"macOS package directory name cannot provide version tag: {name}")
    return name[len(prefix) : name.index(marker)]


def macos_package_suffix_from_name(package_root: Path, arch: str) -> str:
    name = package_root.name
    marker = f"-macos-{arch}"
    if marker not in name:
        raise RuntimeError(f"macOS package directory name cannot provide package suffix: {name}")
    return name[name.index(marker) + len(marker) :]


def validate_macos_version_manifests(package_root: Path, arch: str, version: str, version_tag: str) -> None:
    manifests = {
        "macOS package": package_root / "VERSION.txt",
        "macOS app": package_root / "openQ4.app" / "Contents" / "Resources" / "VERSION.txt",
    }
    for label, manifest_path in manifests.items():
        try:
            data = manifest_path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"{label} version manifest is unreadable: {manifest_path}") from exc
        validate_version_manifest_bytes(
            data,
            label,
            version=version,
            version_tag=version_tag,
            platform="macos",
            arch=arch,
        )


def validate_macos_localized_info_files(package_root: Path, version: str) -> None:
    for locale in MACOS_LOCALIZED_INFO_LOCALES:
        path = package_root / "openQ4.app" / "Contents" / "Resources" / f"{locale}.lproj" / "InfoPlist.strings"
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"macOS localized InfoPlist.strings is unreadable: {path}") from exc
        validate_macos_localized_info_bytes(data, f"macOS {locale} InfoPlist.strings", version)

        error_path = package_root / "openQ4.app" / "Contents" / "Resources" / f"{locale}.lproj" / MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME
        try:
            error_data = error_path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"macOS localized package-root error strings are unreadable: {error_path}") from exc
        validate_macos_package_root_error_bytes(
            error_data,
            f"macOS {locale} package-root error strings",
            locale,
        )


def macos_expected_lipo_arches(arch: str) -> frozenset[str]:
    expected = MACOS_LIPO_ARCHES.get(arch)
    if expected is None:
        raise RuntimeError(f"macOS package architecture {arch!r} has no lipo mapping")
    return expected


def macos_expected_lipo_arch(arch: str) -> str:
    """Return the singleton Mach-O slice for compatibility with thin-only callers."""
    expected = macos_expected_lipo_arches(arch)
    if len(expected) != 1:
        raise RuntimeError(f"macOS package architecture {arch!r} is not a thin architecture")
    return next(iter(expected))


def macos_lipo_arches(binary_path: Path) -> set[str]:
    if sys.platform != "darwin":
        return set()

    lipo_path = shutil.which("lipo")
    if lipo_path is None:
        raise RuntimeError("macOS architecture validation requires lipo")

    completed = subprocess.run(
        [lipo_path, "-archs", str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"lipo failed for macOS binary {binary_path}: {message}")

    return set(completed.stdout.strip().split())


def validate_macos_binary_architectures(binary_paths: list[Path], arch: str) -> None:
    if sys.platform != "darwin":
        return

    expected_arches = macos_expected_lipo_arches(arch)
    for binary_path in binary_paths:
        actual_arches = macos_lipo_arches(binary_path)
        if actual_arches != expected_arches:
            actual = ", ".join(sorted(actual_arches)) or "<none>"
            raise RuntimeError(
                f"macOS binary architecture mismatch: {binary_path}: "
                f"expected {', '.join(sorted(expected_arches))}, found {actual}"
            )


def validate_macos_prebuilt_binary_architectures(binary_paths: list[Path], arch: str) -> None:
    """Slice check for vendored third-party Mach-O payloads.

    openQ4-built binaries must match the package architecture exactly. A
    prebuilt universal dependency such as libMoltenVK.dylib is shipped as
    vendored (arm64 + x86_64) even inside a thin package, because re-slicing it
    would invalidate its upstream signature for no runtime benefit -- dyld picks
    the right slice. The requirement is only that every slice this package needs
    is present.
    """

    if sys.platform != "darwin":
        return

    expected_arches = macos_expected_lipo_arches(arch)
    for binary_path in binary_paths:
        actual_arches = macos_lipo_arches(binary_path)
        missing_arches = expected_arches - actual_arches
        if missing_arches:
            actual = ", ".join(sorted(actual_arches)) or "<none>"
            raise RuntimeError(
                "macOS prebuilt binary is missing required architecture slices: "
                f"{binary_path}: needs {', '.join(sorted(missing_arches))}, found {actual}"
            )


def run_macos_command(command: list[str], *, label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"{label} failed: {message}")
    return completed


def run_macos_codesign(command: list[str], *, label: str) -> subprocess.CompletedProcess[str]:
    return run_macos_command(command, label=label)


def validate_macos_entitlements_file(entitlements: Path) -> None:
    try:
        with entitlements.open("rb") as handle:
            entitlement_values = plistlib.load(handle)
    except (plistlib.InvalidFileException, TypeError, ValueError) as exc:
        raise RuntimeError(f"macOS entitlements file is not a valid plist: {entitlements}") from exc
    except OSError as exc:
        raise RuntimeError(f"macOS entitlements file is unreadable: {entitlements}") from exc

    if not isinstance(entitlement_values, dict):
        raise RuntimeError(f"macOS entitlements file must contain a dictionary root: {entitlements}")

    forbidden_keys = []
    for key, reason in MACOS_FORBIDDEN_ENTITLEMENTS.items():
        if entitlement_values.get(key):
            forbidden_keys.append(f"{key} ({reason})")

    if forbidden_keys:
        joined = "; ".join(forbidden_keys)
        raise RuntimeError(f"macOS entitlements file contains unsupported release entitlements: {joined}")


def require_macos_signing_file(raw_path: Path, label: str) -> Path:
    if raw_path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {raw_path}")
    path = raw_path.resolve()
    if not path.is_file():
        raise RuntimeError(f"{label} does not exist: {path}")
    return path


def resolve_macos_signing_config(args: argparse.Namespace) -> MacOSSigningConfig:
    entitlements = (
        require_macos_signing_file(Path(args.macos_entitlements), "macOS entitlements file")
        if args.macos_entitlements
        else None
    )
    if entitlements is not None:
        validate_macos_entitlements_file(entitlements)

    if args.macos_signing_mode == "ad-hoc":
        if args.macos_notarize:
            raise RuntimeError("macOS notarization requires --macos-signing-mode=developer-id")
        return MacOSSigningConfig(
            mode="ad-hoc",
            identity="-",
            hardened_runtime=False,
            timestamp=False,
            entitlements=entitlements,
            notarize=False,
            notary_keychain_profile="",
            notary_keychain=None,
        )

    identity = args.macos_code_sign_identity.strip()
    if identity == "":
        raise RuntimeError("macOS Developer ID signing requires --macos-code-sign-identity")

    notary_keychain_profile = args.macos_notary_keychain_profile.strip()
    if args.macos_notarize and notary_keychain_profile == "":
        raise RuntimeError("macOS notarization requires --macos-notary-keychain-profile")

    notary_keychain = (
        require_macos_signing_file(Path(args.macos_notary_keychain), "macOS notary keychain")
        if args.macos_notary_keychain
        else None
    )

    return MacOSSigningConfig(
        mode="developer-id",
        identity=identity,
        hardened_runtime=True,
        timestamp=True,
        entitlements=entitlements,
        notarize=args.macos_notarize,
        notary_keychain_profile=notary_keychain_profile,
        notary_keychain=notary_keychain,
    )


def macos_embedded_game_module_paths(package_root: Path, arch: str) -> tuple[Path, Path]:
    framework_root = package_root / "openQ4.app" / MACOS_APP_FRAMEWORKS_DIR
    return (
        framework_root / f"game-sp_{arch}.dylib",
        framework_root / f"game-mp_{arch}.dylib",
    )


def macos_embedded_renderer_module_path(package_root: Path, arch: str) -> Path:
    return package_root / "openQ4.app" / MACOS_APP_FRAMEWORKS_DIR / f"renderer-vk_{arch}.dylib"


def macos_embedded_moltenvk_path(package_root: Path) -> Path:
    return package_root / "openQ4.app" / MACOS_APP_FRAMEWORKS_DIR / MACOS_MOLTENVK_DYLIB_NAME


def macos_embedded_openal_soft_path(package_root: Path) -> Path:
    return package_root / "openQ4.app" / MACOS_APP_FRAMEWORKS_DIR / MACOS_OPENAL_SOFT_DYLIB_NAME


def macos_embedded_library_paths(package_root: Path, arch: str) -> list[Path]:
    """Every Mach-O library nested inside openQ4.app/Contents/Frameworks.

    These are signed inside-out and never receive the app entitlements, which
    belong to the main executable alone.
    """

    sp_module, mp_module = macos_embedded_game_module_paths(package_root, arch)
    return [
        sp_module,
        mp_module,
        macos_embedded_renderer_module_path(package_root, arch),
        macos_embedded_moltenvk_path(package_root),
        macos_embedded_openal_soft_path(package_root),
    ]


def macos_signable_targets(package_root: Path, arch: str) -> list[Path]:
    return [
        package_root / f"{PRODUCT_NAME}-client_{arch}",
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        *macos_embedded_library_paths(package_root, arch),
    ]


def macos_loadable_module_install_names(package_root: Path, arch: str) -> dict[Path, str]:
    sp_module, mp_module = macos_embedded_game_module_paths(package_root, arch)
    renderer_module = macos_embedded_renderer_module_path(package_root, arch)
    # Meson's Darwin install depfixer may replace linker-authored IDs with the
    # absolute staging destination for every installed dylib. Normalize every
    # openQ4 loadable module after it enters the app bundle. libMoltenVK's
    # package-relative ID is set and asserted by its pinned provider script.
    # OpenAL Soft is likewise prepared outside Meson, but it is included here
    # because its @rpath identity is part of the package contract.
    return {
        sp_module: f"@loader_path/game-sp_{arch}.dylib",
        mp_module: f"@loader_path/game-mp_{arch}.dylib",
        renderer_module: f"@loader_path/renderer-vk_{arch}.dylib",
        macos_embedded_openal_soft_path(package_root): MACOS_OPENAL_SOFT_INSTALL_NAME,
    }


def normalize_macos_loadable_module_install_names(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    expected_arches = sorted(macos_expected_lipo_arches(arch))
    pending_updates: list[tuple[Path, str]] = []
    for binary_path, expected_install_name in macos_loadable_module_install_names(package_root, arch).items():
        require_packaged_executable(binary_path, "macOS loadable module install-name normalization binary")
        if any(
            macos_otool_install_name(binary_path, macho_arch=macho_arch) != expected_install_name
            for macho_arch in expected_arches
        ):
            pending_updates.append((binary_path, expected_install_name))

    if not pending_updates:
        return

    install_name_tool_path = shutil.which("install_name_tool")
    if install_name_tool_path is None:
        raise RuntimeError("macOS install-name normalization requires install_name_tool")

    for binary_path, expected_install_name in pending_updates:
        run_macos_command(
            [install_name_tool_path, "-id", expected_install_name, str(binary_path)],
            label=f"setting macOS loadable module install name for {binary_path}",
        )


def macos_codesign_target(
    codesign_path: str,
    target: Path,
    config: MacOSSigningConfig,
    *,
    include_entitlements: bool = True,
) -> None:
    command = [
        codesign_path,
        "--force",
        "--sign",
        config.identity,
    ]
    if config.hardened_runtime:
        command += ["--options", "runtime"]
    if config.timestamp:
        command.append("--timestamp")
    else:
        command.append("--timestamp=none")
    if include_entitlements and config.entitlements is not None:
        command += ["--entitlements", str(config.entitlements)]
    command.append(str(target))

    run_macos_codesign(command, label=f"{config.mode} signing {target}")


def sign_macos_payload(package_root: Path, arch: str, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin":
        if config.mode == "developer-id" or config.notarize:
            raise RuntimeError("macOS Developer ID signing and notarization require a macOS host")
        return

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    app_root = package_root / "openQ4.app"
    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    app_executable = app_root / "Contents" / "MacOS" / "openQ4"

    embedded_modules = set(macos_embedded_library_paths(package_root, arch))
    for target in macos_signable_targets(package_root, arch):
        if target in embedded_modules:
            # Nested libraries are signed inside-out, but app entitlements belong
            # only to the main executable rather than copied onto library code.
            # This covers the game modules, the Vulkan renderer module, and the
            # vendored libMoltenVK.dylib, which ships ad-hoc signed upstream and
            # must be re-signed with the release identity before notarization.
            macos_codesign_target(codesign_path, target, config, include_entitlements=False)
        else:
            macos_codesign_target(codesign_path, target, config)

    # The app bundle can receive a bundle-scoped executable signature; keep the
    # loose client signed independently as a standalone binary.
    shutil.copy2(client_binary, app_executable)
    ensure_posix_executable(app_executable)
    macos_codesign_target(codesign_path, app_root, config)


def ad_hoc_sign_macos_payload(package_root: Path, arch: str) -> None:
    sign_macos_payload(
        package_root,
        arch,
        MacOSSigningConfig(
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


def verify_macos_codesignature(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    verify_targets = [
        *macos_signable_targets(package_root, arch),
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        package_root / "openQ4.app",
    ]
    for target in verify_targets:
        run_macos_codesign(
            [codesign_path, "--verify", "--strict", "--verbose=2", str(target)],
            label=f"code signature verification for {target}",
        )


def macos_codesign_details(target: Path) -> str:
    if sys.platform != "darwin":
        return ""

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS code-sign validation requires codesign")

    completed = run_macos_codesign(
        [codesign_path, "-dv", "--verbose=4", str(target)],
        label=f"code signature detail inspection for {target}",
    )
    return completed.stdout + completed.stderr


def verify_macos_developer_id_signature(package_root: Path, arch: str, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or config.mode != "developer-id":
        return

    verify_targets = [
        *macos_signable_targets(package_root, arch),
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        package_root / "openQ4.app",
    ]
    for target in verify_targets:
        details = macos_codesign_details(target)
        if "Authority=Developer ID Application:" not in details:
            raise RuntimeError(f"macOS target is not Developer ID Application signed: {target}")
        if "Signature=adhoc" in details:
            raise RuntimeError(f"macOS target is still ad-hoc signed: {target}")
        if config.hardened_runtime and "Runtime Version=" not in details:
            raise RuntimeError(f"macOS target is missing Hardened Runtime metadata: {target}")


def notarize_macos_app_bundle(package_root: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or not config.notarize:
        return

    for tool_name in ("ditto", "xcrun", "spctl"):
        if shutil.which(tool_name) is None:
            raise RuntimeError(f"macOS notarization requires {tool_name}")

    validate_macos_dmg_source_tree(package_root)
    app_root = package_root / "openQ4.app"
    notary_archive = package_root.parent / f"{package_root.name}-notary.zip"
    prepare_archive_output_path(notary_archive, "macOS notarization archive output")

    run_macos_command(
        ["ditto", "-c", "-k", "--keepParent", str(package_root), str(notary_archive)],
        label=f"creating notarization archive {notary_archive}",
    )

    notary_command = [
        "xcrun",
        "notarytool",
        "submit",
        str(notary_archive),
        "--keychain-profile",
        config.notary_keychain_profile,
        "--wait",
        "--timeout",
        "45m",
    ]
    if config.notary_keychain is not None:
        notary_command += ["--keychain", str(config.notary_keychain)]
    try:
        run_macos_command(notary_command, label=f"notarizing {package_root}")
    finally:
        try:
            notary_archive.unlink()
        except OSError:
            pass

    run_macos_command(["xcrun", "stapler", "staple", str(app_root)], label=f"stapling {app_root}")
    run_macos_command(["xcrun", "stapler", "validate", str(app_root)], label=f"validating stapled ticket for {app_root}")
    run_macos_command(
        ["spctl", "--assess", "--type", "execute", "--verbose=4", str(app_root)],
        label=f"Gatekeeper assessment for {app_root}",
    )


def validate_macos_dmg_image(dmg_path: Path) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("macOS DMG validation requires a macOS host")

    hdiutil_path = shutil.which("hdiutil")
    if hdiutil_path is None:
        raise RuntimeError("macOS DMG validation requires hdiutil")

    run_macos_command([hdiutil_path, "verify", str(dmg_path)], label=f"verifying macOS DMG {dmg_path}")
    run_macos_command([hdiutil_path, "imageinfo", str(dmg_path)], label=f"reading macOS DMG info for {dmg_path}")


def sign_macos_dmg_image(dmg_path: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or config.mode != "developer-id":
        return

    codesign_path = shutil.which("codesign")
    if codesign_path is None:
        raise RuntimeError("macOS DMG signing requires codesign")

    run_macos_codesign(
        [codesign_path, "--force", "--sign", config.identity, "--timestamp", str(dmg_path)],
        label=f"{config.mode} signing macOS DMG {dmg_path}",
    )


def verify_macos_dmg_developer_id_signature(dmg_path: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or config.mode != "developer-id":
        return

    details = macos_codesign_details(dmg_path)
    if "Authority=Developer ID Application:" not in details:
        raise RuntimeError(f"macOS DMG is not Developer ID Application signed: {dmg_path}")
    if "Signature=adhoc" in details:
        raise RuntimeError(f"macOS DMG is still ad-hoc signed: {dmg_path}")


def notarize_macos_dmg_image(dmg_path: Path, config: MacOSSigningConfig) -> None:
    if sys.platform != "darwin" or not config.notarize:
        return

    xcrun_path = shutil.which("xcrun")
    if xcrun_path is None:
        raise RuntimeError("macOS DMG notarization requires xcrun")

    notary_command = [
        xcrun_path,
        "notarytool",
        "submit",
        str(dmg_path),
        "--keychain-profile",
        config.notary_keychain_profile,
        "--wait",
        "--timeout",
        "45m",
    ]
    if config.notary_keychain is not None:
        notary_command += ["--keychain", str(config.notary_keychain)]

    run_macos_command(notary_command, label=f"notarizing macOS DMG {dmg_path}")
    run_macos_command([xcrun_path, "stapler", "staple", str(dmg_path)], label=f"stapling macOS DMG {dmg_path}")
    run_macos_command(
        [xcrun_path, "stapler", "validate", str(dmg_path)],
        label=f"validating stapled macOS DMG ticket for {dmg_path}",
    )


def package_git_value(root: Path, *args: str) -> str:
    if not (root / ".git").exists():
        return ""

    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def package_git_dirty(root: Path) -> str:
    if not package_git_value(root, "rev-parse", "--is-inside-work-tree"):
        return "unavailable"
    return "true" if package_git_value(root, "status", "--porcelain") else "false"


def clean_version_metadata_value(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "unavailable"

    text = str(value).strip()
    if not text or "\n" in text or "\r" in text:
        return "unavailable"
    return text


def read_staged_repository_metadata(source_root: Path, platform: str = "windows", arch: str = "x64") -> dict[str, str]:
    # Universal2 provenance comes from the verified thin-package records; it
    # has no corresponding Meson source stage of its own.
    if arch == "universal2":
        return {}
    manifest_path = gamelibs_stage_root(source_root, platform, arch) / GAMELIBS_STAGE_MANIFEST_NAME
    if not manifest_path.is_file() or manifest_path.is_symlink():
        return {}

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(manifest, dict):
        return {}

    return {
        "openq4_commit": clean_version_metadata_value(manifest.get("projectGitCommit")),
        "openq4_dirty": clean_version_metadata_value(manifest.get("projectGitDirty")),
        "openq4_game_commit": clean_version_metadata_value(manifest.get("gameLibsGitCommit")),
        "openq4_game_dirty": clean_version_metadata_value(manifest.get("gameLibsGitDirty")),
    }


def collect_package_repository_metadata(source_root: Path, platform: str = "windows", arch: str = "x64") -> dict[str, str]:
    metadata = {key: "unavailable" for key in VERSION_REPOSITORY_METADATA_KEYS}
    staged_metadata = read_staged_repository_metadata(source_root, platform, arch)
    for key, value in staged_metadata.items():
        if value != "unavailable":
            metadata[key] = value

    if metadata["openq4_commit"] == "unavailable":
        metadata["openq4_commit"] = clean_version_metadata_value(
            package_git_value(source_root, "rev-parse", "--verify", "HEAD")
        )
    if metadata["openq4_dirty"] == "unavailable":
        metadata["openq4_dirty"] = package_git_dirty(source_root)

    gamelibs_env = os.environ.get("OPENQ4_GAMELIBS_REPO", "").strip()
    gamelibs_root = Path(gamelibs_env) if gamelibs_env else source_root.parent / "openQ4-game"
    if metadata["openq4_game_commit"] == "unavailable":
        metadata["openq4_game_commit"] = clean_version_metadata_value(
            package_git_value(gamelibs_root, "rev-parse", "--verify", "HEAD")
        )
    if metadata["openq4_game_dirty"] == "unavailable":
        metadata["openq4_game_dirty"] = package_git_dirty(gamelibs_root)

    return metadata


def write_version_manifest(
    destination: Path,
    *,
    version: str,
    version_tag: str,
    platform: str,
    arch: str,
    repository_metadata: dict[str, str] | None = None,
) -> None:
    lines = [
        PRODUCT_NAME,
        f"version={version}",
        f"version_tag={version_tag}",
        f"platform={platform}",
        f"arch={arch}",
    ]
    if repository_metadata is not None:
        for key in VERSION_REPOSITORY_METADATA_KEYS:
            lines.append(f"{key}={clean_version_metadata_value(repository_metadata.get(key))}")

    write_text_file(destination, lines)


def macos_symbol_archive_stem(version_tag: str, arch: str, package_suffix: str) -> str:
    return f"openq4-{version_tag}-macos-{arch}{package_suffix}-symbols"


def macos_symbol_targets(package_root: Path, arch: str) -> list[tuple[Path, Path, Path]]:
    sp_module, mp_module = macos_embedded_game_module_paths(package_root, arch)
    return [
        (
            Path("openQ4.app") / "Contents" / "MacOS" / "openQ4",
            package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
            Path("dSYMs") / "openQ4.app.dSYM",
        ),
        (
            Path(f"{PRODUCT_NAME}-client_{arch}"),
            package_root / f"{PRODUCT_NAME}-client_{arch}",
            Path("dSYMs") / f"{PRODUCT_NAME}-client_{arch}.dSYM",
        ),
        (
            Path(f"{PRODUCT_NAME}-ded_{arch}"),
            package_root / f"{PRODUCT_NAME}-ded_{arch}",
            Path("dSYMs") / f"{PRODUCT_NAME}-ded_{arch}.dSYM",
        ),
        (
            Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"game-sp_{arch}.dylib",
            sp_module,
            Path("dSYMs") / f"game-sp_{arch}.dylib.dSYM",
        ),
        (
            Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"game-mp_{arch}.dylib",
            mp_module,
            Path("dSYMs") / f"game-mp_{arch}.dylib.dSYM",
        ),
        (
            Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"renderer-vk_{arch}.dylib",
            macos_embedded_renderer_module_path(package_root, arch),
            Path("dSYMs") / f"renderer-vk_{arch}.dylib.dSYM",
        ),
        # DELIBERATE EXCLUSION: openQ4.app/Contents/Frameworks/libMoltenVK.dylib
        # is NOT listed here and must not be added. MoltenVK is a third-party
        # prebuilt binary that openQ4 does not compile, so there is no DWARF for
        # dsymutil to harvest and no dSYM to pair against it. Adding it would
        # make create_macos_symbol_archive() fail on every macOS package. Crash
        # reports that land inside MoltenVK are symbolicated against the
        # upstream MoltenVK release, not against this archive.
    ]


def macos_macho_uuid(binary_path: Path) -> str:
    if sys.platform != "darwin":
        return "unavailable"

    dwarfdump_path = shutil.which("dwarfdump")
    if dwarfdump_path is None:
        raise RuntimeError("macOS symbol manifest generation requires dwarfdump")

    completed = run_macos_command(
        [dwarfdump_path, "--uuid", str(binary_path)],
        label=f"reading Mach-O UUID for {binary_path}",
    )
    uuid_lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return "; ".join(uuid_lines) if uuid_lines else "unavailable"


def macos_macho_uuid_pairs(binary_path: Path, label: str) -> frozenset[tuple[str, str]]:
    if sys.platform != "darwin":
        return frozenset()

    dwarfdump_path = shutil.which("dwarfdump")
    if dwarfdump_path is None:
        raise RuntimeError("macOS dSYM UUID validation requires dwarfdump")

    completed = run_macos_command(
        [dwarfdump_path, "--uuid", str(binary_path)],
        label=f"reading Mach-O UUIDs for {label}",
    )
    pairs: set[tuple[str, str]] = set()
    for line in completed.stdout.splitlines():
        match = re.fullmatch(
            r"UUID: ([0-9A-Fa-f-]{36}) \(([A-Za-z0-9_]+)\) .+",
            line.strip(),
        )
        if match is None:
            raise RuntimeError(f"macOS dSYM UUID output is malformed for {label}: {line!r}")
        pair = (match.group(1).lower(), match.group(2))
        if pair in pairs:
            raise RuntimeError(f"macOS dSYM UUID output has duplicate slices for {label}: {line!r}")
        pairs.add(pair)
    if not pairs:
        raise RuntimeError(f"macOS dSYM UUID output is empty for {label}")
    return frozenset(pairs)


def validate_macos_dsym_matches_binary(binary_path: Path, dsym_path: Path) -> None:
    binary_uuids = macos_macho_uuid_pairs(binary_path, f"binary {binary_path}")
    dsym_uuids = macos_macho_uuid_pairs(dsym_path, f"dSYM {dsym_path}")
    if binary_uuids != dsym_uuids:
        raise RuntimeError(
            "macOS dSYM UUID slices do not match the distributed binary: "
            f"{binary_path}: binary={sorted(binary_uuids)} dSYM={sorted(dsym_uuids)}"
        )


def write_macos_symbol_manifest(
    package_root: Path,
    *,
    version: str,
    version_tag: str,
    arch: str,
    package_suffix: str,
    runtime_archive_name: str,
) -> Path:
    symbol_archive_name = f"{macos_symbol_archive_stem(version_tag, arch, package_suffix)}{MACOS_SYMBOL_ARCHIVE_SUFFIX}"
    lines = [
        "openQ4 macOS symbols",
        "format=1",
        f"version={version}",
        f"version_tag={version_tag}",
        "platform=macos",
        f"arch={arch}",
        f"package_suffix={package_suffix or '<none>'}",
        f"runtime_archive={runtime_archive_name}",
        f"symbol_archive={symbol_archive_name}",
        "",
        "binaries:",
    ]

    for relative_path, binary_path, dsym_relative_path in macos_symbol_targets(package_root, arch):
        require_non_empty_package_file(binary_path, f"macOS symbol manifest binary {relative_path.as_posix()}")
        lines.extend(
            [
                f"- path={relative_path.as_posix()}",
                f"  sha256={sha256_file(binary_path)}",
                f"  size={binary_path.stat().st_size}",
                f"  macho_uuid={macos_macho_uuid(binary_path)}",
                f"  dsym={dsym_relative_path.as_posix()}",
            ]
        )

    manifest_path = package_root / MACOS_SYMBOL_MANIFEST_NAME
    write_text_file(manifest_path, lines)
    return manifest_path


def validate_macos_symbol_manifest_bytes(
    data: bytes,
    label: str,
    *,
    version: str,
    version_tag: str,
    arch: str,
    package_suffix: str,
    runtime_archive_name: str,
    symbol_archive_name: str,
) -> None:
    validate_macos_metadata_bytes_size(data, label)
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"{label} is not UTF-8") from exc

    lines = text.splitlines()
    if not lines or lines[0].strip() != "openQ4 macOS symbols":
        raise RuntimeError(f"{label} has invalid header")

    header_values: dict[str, str] = {}
    for line in lines[1:]:
        stripped = line.strip()
        if stripped == "binaries:":
            break
        if not stripped:
            continue
        if "=" not in stripped:
            raise RuntimeError(f"{label} contains malformed header line: {line}")
        key, value = stripped.split("=", 1)
        if key in header_values:
            raise RuntimeError(f"{label} contains duplicate key: {key}")
        header_values[key] = value

    expected_values = {
        "format": "1",
        "version": version,
        "version_tag": version_tag,
        "platform": "macos",
        "arch": arch,
        "package_suffix": package_suffix or "<none>",
        "symbol_archive": symbol_archive_name,
    }
    allowed_header_keys = set(expected_values) | {"runtime_archive"}
    unexpected_header_keys = sorted(set(header_values) - allowed_header_keys)
    if unexpected_header_keys:
        raise RuntimeError(f"{label} contains unexpected header key: {', '.join(unexpected_header_keys)}")

    symbol_archive = header_values.get("symbol_archive")
    if symbol_archive is not None:
        validate_macos_manifest_archive_filename(
            symbol_archive,
            f"{label} symbol_archive",
            (MACOS_SYMBOL_ARCHIVE_SUFFIX,),
        )

    runtime_archive = header_values.get("runtime_archive")
    if runtime_archive is None:
        raise RuntimeError(f"{label} is missing required token: runtime_archive=")
    validate_macos_manifest_archive_filename(
        runtime_archive,
        f"{label} runtime_archive",
        tuple(ARCHIVE_SUFFIX.values()),
    )

    for key, expected in expected_values.items():
        actual = header_values.get(key)
        if actual != expected:
            raise RuntimeError(f"{label} {key} is {actual!r}; expected {expected!r}")
    if runtime_archive_name and runtime_archive != runtime_archive_name:
        raise RuntimeError(f"{label} runtime_archive is {runtime_archive!r}; expected {runtime_archive_name!r}")

    expected_binaries = {
        "openQ4.app/Contents/MacOS/openQ4": "dSYMs/openQ4.app.dSYM",
        f"{PRODUCT_NAME}-client_{arch}": f"dSYMs/{PRODUCT_NAME}-client_{arch}.dSYM",
        f"{PRODUCT_NAME}-ded_{arch}": f"dSYMs/{PRODUCT_NAME}-ded_{arch}.dSYM",
        f"openQ4.app/Contents/Frameworks/game-sp_{arch}.dylib": f"dSYMs/game-sp_{arch}.dylib.dSYM",
        f"openQ4.app/Contents/Frameworks/game-mp_{arch}.dylib": f"dSYMs/game-mp_{arch}.dylib.dSYM",
        # Mirrors macos_symbol_targets(); libMoltenVK.dylib is intentionally
        # absent because it is third-party and has no dSYM.
        f"openQ4.app/Contents/Frameworks/renderer-vk_{arch}.dylib": f"dSYMs/renderer-vk_{arch}.dylib.dSYM",
    }
    if "binaries:" not in [line.strip() for line in lines]:
        raise RuntimeError(f"{label} is missing required token: binaries:")

    binary_records: dict[str, dict[str, str]] = {}
    current_path: str | None = None
    in_binaries = False
    for line in lines[1:]:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "binaries:":
            in_binaries = True
            current_path = None
            continue
        if not in_binaries:
            continue
        if stripped.startswith("- path="):
            current_path = stripped.removeprefix("- path=")
            validate_macos_archive_name(current_path, "")
            if current_path in binary_records:
                raise RuntimeError(f"{label} contains duplicate binary entry: {current_path}")
            binary_records[current_path] = {}
            continue
        if current_path is None:
            raise RuntimeError(f"{label} contains binary metadata before a path: {line}")
        if "=" not in stripped:
            raise RuntimeError(f"{label} contains malformed binary line: {line}")
        key, value = stripped.split("=", 1)
        if key not in {"sha256", "size", "macho_uuid", "dsym"}:
            raise RuntimeError(f"{label} contains unexpected binary field: {key}")
        if key in binary_records[current_path]:
            raise RuntimeError(f"{label} contains duplicate binary field {key}: {current_path}")
        binary_records[current_path][key] = value

    missing_binaries = sorted(set(expected_binaries) - set(binary_records))
    if missing_binaries:
        raise RuntimeError(f"{label} is missing binary entries: {', '.join(missing_binaries)}")
    unexpected_binaries = sorted(set(binary_records) - set(expected_binaries))
    if unexpected_binaries:
        raise RuntimeError(f"{label} contains unexpected binary entries: {', '.join(unexpected_binaries)}")

    for binary_path, expected_dsym in expected_binaries.items():
        record = binary_records[binary_path]
        for field in ("sha256", "size", "macho_uuid", "dsym"):
            if field not in record:
                raise RuntimeError(f"{label} binary {binary_path} is missing {field}")
        if re.fullmatch(r"[0-9a-f]{64}", record["sha256"]) is None:
            raise RuntimeError(f"{label} binary {binary_path} has invalid sha256")
        if re.fullmatch(r"[0-9]+", record["size"]) is None or int(record["size"]) <= 0:
            raise RuntimeError(f"{label} binary {binary_path} has invalid size")
        macho_uuid = record["macho_uuid"].strip()
        expected_macho_arches = macos_expected_lipo_arches(arch)
        uuid_records = [item.strip() for item in macho_uuid.split(";") if item.strip()]
        uuid_arches: set[str] = set()
        valid_uuid_records = bool(uuid_records)
        for uuid_record in uuid_records:
            match = re.fullmatch(
                r"UUID: [0-9A-Fa-f-]{36} \(([A-Za-z0-9_]+)\) .+",
                uuid_record,
            )
            if match is None or match.group(1) in uuid_arches:
                valid_uuid_records = False
                break
            uuid_arches.add(match.group(1))
        if (
            not valid_uuid_records
            or any(ord(character) < 32 or ord(character) == 127 for character in macho_uuid)
            or uuid_arches != expected_macho_arches
        ):
            raise RuntimeError(f"{label} binary {binary_path} has invalid macho_uuid")
        if record["dsym"] != expected_dsym:
            raise RuntimeError(
                f"{label} binary {binary_path} dsym is {record['dsym']!r}; expected {expected_dsym!r}"
            )


def create_macos_dsym_bundle(binary_path: Path, dsym_path: Path) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("macOS dSYM generation requires a macOS host")

    dsymutil_path = shutil.which("dsymutil")
    if dsymutil_path is None:
        raise RuntimeError("macOS dSYM generation requires dsymutil")

    prepare_macos_dsym_output_path(dsym_path)

    run_macos_command(
        [dsymutil_path, str(binary_path), "-o", str(dsym_path)],
        label=f"generating dSYM bundle for {binary_path}",
    )

    dwarf_dir = dsym_path / "Contents" / "Resources" / "DWARF"
    if not dwarf_dir.is_dir() or not any(path.is_file() for path in dwarf_dir.iterdir()):
        raise RuntimeError(f"generated dSYM bundle has no DWARF payload: {dsym_path}")


def create_macos_symbol_tarball(symbol_root: Path, archive_path: Path) -> None:
    if symbol_root.is_symlink():
        raise RuntimeError(f"macOS symbol archive input root must not be a symlink: {symbol_root}")
    if not symbol_root.is_dir():
        raise RuntimeError(f"macOS symbol archive input root was not found: {symbol_root}")
    try:
        if is_relative_to(archive_path.resolve(strict=False), symbol_root.resolve()):
            raise RuntimeError(
                f"macOS symbol archive output must not be inside the symbol staging root: {archive_path}"
            )
    except OSError as exc:
        raise RuntimeError(f"macOS symbol archive paths could not be resolved safely: {exc}") from exc
    prepare_archive_output_path(archive_path, "macOS symbol archive output")

    with tarfile.open(archive_path, "w:xz") as archive:
        for path in sorted(symbol_root.rglob("*")):
            if path.is_symlink():
                raise RuntimeError(f"macOS symbol archive input contains a symlink: {path}")
            if path.is_dir():
                continue
            if not path.is_file():
                raise RuntimeError(f"macOS symbol archive input contains a non-regular file: {path}")

            relative_path = path.relative_to(symbol_root)
            arcname = (Path(symbol_root.name) / relative_path).as_posix()
            info = archive.gettarinfo(path, arcname=arcname)
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            info.mtime = DETERMINISTIC_TAR_MTIME
            info.mode = 0o644
            with path.open("rb") as handle:
                archive.addfile(info, handle)


def validate_macos_symbol_archive_contents(
    archive_path: Path,
    symbol_root_name: str,
    *,
    version: str,
    version_tag: str,
    arch: str,
    package_suffix: str,
    runtime_archive_name: str,
) -> None:
    if archive_path.is_symlink():
        raise RuntimeError(f"macOS symbol archive path must not be a symlink: {archive_path}")
    if not archive_path.is_file():
        raise RuntimeError(f"macOS symbol archive was not created: {archive_path}")

    package_prefix = symbol_root_name + "/"
    expected_symbol_archive_name = f"{macos_symbol_archive_stem(version_tag, arch, package_suffix)}{MACOS_SYMBOL_ARCHIVE_SUFFIX}"
    expected_entries = {
        f"{package_prefix}{MACOS_SYMBOL_MANIFEST_NAME}",
        f"{package_prefix}dSYMs/openQ4.app.dSYM/Contents/Resources/DWARF/openQ4",
        f"{package_prefix}dSYMs/{PRODUCT_NAME}-client_{arch}.dSYM/Contents/Resources/DWARF/{PRODUCT_NAME}-client_{arch}",
        f"{package_prefix}dSYMs/{PRODUCT_NAME}-ded_{arch}.dSYM/Contents/Resources/DWARF/{PRODUCT_NAME}-ded_{arch}",
        f"{package_prefix}dSYMs/game-sp_{arch}.dylib.dSYM/Contents/Resources/DWARF/game-sp_{arch}.dylib",
        f"{package_prefix}dSYMs/game-mp_{arch}.dylib.dSYM/Contents/Resources/DWARF/game-mp_{arch}.dylib",
        f"{package_prefix}dSYMs/renderer-vk_{arch}.dylib.dSYM/Contents/Resources/DWARF/renderer-vk_{arch}.dylib",
    }
    forbidden_runtime_entries = {
        f"{package_prefix}openQ4.app/Contents/MacOS/openQ4",
        f"{package_prefix}{PRODUCT_NAME}-client_{arch}",
        f"{package_prefix}{PRODUCT_NAME}-ded_{arch}",
        f"{package_prefix}openQ4.app/Contents/Frameworks/game-sp_{arch}.dylib",
        f"{package_prefix}openQ4.app/Contents/Frameworks/game-mp_{arch}.dylib",
        f"{package_prefix}openQ4.app/Contents/Frameworks/renderer-vk_{arch}.dylib",
        # libMoltenVK has no dSYM, so it must never appear in the symbol archive
        # at all -- if it does, someone copied runtime payload in by mistake.
        f"{package_prefix}openQ4.app/Contents/Frameworks/{MACOS_MOLTENVK_DYLIB_NAME}",
        # OpenAL Soft is third-party runtime code and likewise has no openQ4 dSYM.
        f"{package_prefix}openQ4.app/Contents/Frameworks/{MACOS_OPENAL_SOFT_DYLIB_NAME}",
    }

    entry_names: set[str] = set()
    casefold_entry_names: dict[str, str] = {}
    manifest_bytes: bytes | None = None
    try:
        archive_context = tarfile.open(archive_path, "r:xz")
        try:
            members = archive_context.getmembers()
        except tarfile.TarError:
            archive_context.close()
            raise
    except tarfile.TarError as exc:
        raise RuntimeError(f"macOS symbol archive is not a valid xz-compressed tar archive: {archive_path}") from exc
    with archive_context as archive:
        if len(members) > MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS:
            raise RuntimeError(
                "macOS symbol archive contains too many members: "
                f"{len(members)} (max {MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS})"
            )
        total_member_bytes = 0
        for member in members:
            name = member.name.rstrip("/")
            if not name:
                continue
            if member.issym() or member.islnk():
                raise RuntimeError(f"macOS symbol archive contains symlink entry: {name}")
            if not member.isfile():
                raise RuntimeError(f"macOS symbol archive contains non-regular entry: {name}")
            total_member_bytes += member.size
            if total_member_bytes > MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES:
                raise RuntimeError(
                    "macOS symbol archive total expanded size is too large: "
                    f"{total_member_bytes} bytes (max {MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES})"
                )
            validate_macos_archive_name(name, package_prefix)
            if is_macos_metadata_sidecar_path(Path(name)):
                raise RuntimeError(f"macOS symbol archive contains non-runtime metadata/debug entry: {name}")
            validate_macos_archive_metadata_member_size(name, member.size)
            validate_macos_archive_mode(name, member.mode & 0o7777)
            if name in entry_names:
                raise RuntimeError(f"macOS symbol archive contains duplicate entry: {name}")
            casefold_key = macos_casefold_path_key(name)
            previous = casefold_entry_names.get(casefold_key)
            if previous is not None and previous != name:
                raise RuntimeError(
                    "macOS symbol archive contains case-insensitive duplicate entries: "
                    f"{previous}, {name}"
                )
            entry_names.add(name)
            casefold_entry_names[casefold_key] = name
            if name == f"{package_prefix}{MACOS_SYMBOL_MANIFEST_NAME}":
                extracted = archive.extractfile(member)
                if extracted is not None:
                    manifest_bytes = extracted.read()

    missing_entries = sorted(expected_entries - entry_names)
    if missing_entries:
        joined = ", ".join(missing_entries)
        raise RuntimeError(f"macOS symbol archive is missing required dSYM entries: {joined}")

    leaked_runtime = sorted(forbidden_runtime_entries & entry_names)
    if leaked_runtime:
        joined = ", ".join(leaked_runtime)
        raise RuntimeError(f"macOS symbol archive contains runtime payload entries: {joined}")

    if manifest_bytes is None:
        raise RuntimeError("macOS symbol archive manifest is missing")
    validate_macos_symbol_manifest_bytes(
        manifest_bytes,
        "macOS symbol archive manifest",
        version=version,
        version_tag=version_tag,
        arch=arch,
        package_suffix=package_suffix,
        runtime_archive_name=runtime_archive_name,
        symbol_archive_name=expected_symbol_archive_name,
    )


def create_macos_symbol_archive(
    package_root: Path,
    output_dir: Path,
    *,
    version: str,
    version_tag: str,
    arch: str,
    package_suffix: str,
    runtime_archive_name: str,
) -> Path:
    symbol_stem = macos_symbol_archive_stem(version_tag, arch, package_suffix)
    symbol_root = output_dir / symbol_stem
    symbol_archive_path = output_dir / f"{symbol_stem}{MACOS_SYMBOL_ARCHIVE_SUFFIX}"
    prepare_macos_symbol_staging_root(symbol_root, output_dir)

    copy_regular_file(package_root / MACOS_SYMBOL_MANIFEST_NAME, symbol_root / MACOS_SYMBOL_MANIFEST_NAME)
    for _relative_path, binary_path, dsym_relative_path in macos_symbol_targets(package_root, arch):
        require_packaged_executable(binary_path, "macOS dSYM source binary")
        dsym_path = symbol_root / dsym_relative_path
        create_macos_dsym_bundle(binary_path, dsym_path)
        validate_macos_dsym_matches_binary(binary_path, dsym_path)

    create_macos_symbol_tarball(symbol_root, symbol_archive_path)
    validate_macos_symbol_archive_contents(
        symbol_archive_path,
        symbol_root.name,
        version=version,
        version_tag=version_tag,
        arch=arch,
        package_suffix=package_suffix,
        runtime_archive_name=runtime_archive_name,
    )
    return symbol_archive_path


def parse_stable_base_version(version: str) -> tuple[int, int, int] | None:
    parts = version.split(".")
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        return None
    return tuple(int(part) for part in parts)


def is_stable_base_version(version: str) -> bool:
    return parse_stable_base_version(version) is not None


def validate_packaged_mod_manifest(package_game_dir: Path, version: str) -> None:
    release_version = parse_stable_base_version(version)
    if release_version is None:
        return

    manifest_path = package_game_dir / "mod.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"packaged mod manifest is unreadable: {manifest_path}") from exc

    mismatches = []
    if manifest.get("version") != version:
        mismatches.append(f"version={manifest.get('version')!r}")

    required_version_text = manifest.get("requiredopenQ4Version")
    required_version = (
        parse_stable_base_version(required_version_text)
        if isinstance(required_version_text, str)
        else None
    )
    if required_version is None:
        mismatches.append(f"requiredopenQ4Version={required_version_text!r}")
    elif required_version > release_version:
        mismatches.append(
            f"requiredopenQ4Version={required_version_text!r} "
            f"(requires newer than package {version})"
        )

    if mismatches:
        joined = ", ".join(mismatches)
        raise RuntimeError(
            f"packaged {GAME_DIR_NAME}/mod.json does not satisfy release {version}: {joined}"
        )


def write_macos_localized_info_strings(app_contents: Path, version: str) -> None:
    localized_info_lines = [
        "/* Localized versions of Info.plist keys */",
        "",
        'CFBundleName = "openQ4";',
        f'CFBundleShortVersionString = "{version}";',
        f'CFBundleGetInfoString = "openQ4 version {version}, Copyright 2026 DarkMatter Productions";',
        'NSHumanReadableCopyright = "Copyright 2026 DarkMatter Productions";',
    ]

    for locale in ("English", "French"):
        write_text_file(
            app_contents / "Resources" / f"{locale}.lproj" / "InfoPlist.strings",
            localized_info_lines,
        )


def write_macos_package_root_error_strings(app_contents: Path) -> None:
    for locale, localized_strings in MACOS_PACKAGE_ROOT_ERROR_STRINGS.items():
        lines = [
            "/* Localized startup error for incomplete adjacent package roots */",
            "",
        ]
        for key, value in localized_strings.items():
            lines.append(f'{key} = "{value}";')

        write_text_file(
            app_contents / "Resources" / f"{locale}.lproj" / MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME,
            lines,
        )


def copy_release_collateral(source_root: Path, package_root: Path, platform: str) -> None:
    collateral = (
        (source_root / RELEASE_README_PATH, package_root / "README.html"),
        (source_root / LICENSE_PATH, package_root / "LICENSE"),
        (source_root / "LICENSES" / "KHRONOS-GLES-MIT.txt", package_root / "licenses" / "KHRONOS-GLES-MIT.txt"),
        (source_root / "CONTRIBUTORS.md", package_root / "CONTRIBUTORS.md"),
    )

    for source, destination in collateral:
        copy_regular_file(source, destination)

    if platform == "macos":
        source = source_root / MACOS_SUPPORT_INFO_SCRIPT_PATH
        validate_macos_support_info_script_bytes(
            source.read_bytes(),
            "macOS support collector",
        )
        destination = package_root / MACOS_SUPPORT_INFO_SCRIPT_NAME
        copy_regular_file(source, destination)
        ensure_posix_executable(destination)


def generate_release_documentation(
    *,
    source_root: Path,
    package_root: Path,
    version: str,
    platform: str,
    arch: str,
) -> GeneratedDocSite:
    return generate_release_docs_site(
        source_root=source_root,
        output_root=package_root / "docs",
        version=version,
        platform=platform,
        arch=arch,
    )


def copy_required_binaries(
    platform: str,
    arch: str,
    install_dir: Path,
    package_root: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    runtime_binaries = (
        *get_required_root_binaries(platform, arch),
        *get_required_renderer_module_binaries(platform, arch),
    )
    for filename in runtime_binaries:
        source = install_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required distributable not found: {source}")
        destination = package_root / filename
        copy_regular_file(source, destination)
        if platform in ("linux", "macos"):
            ensure_posix_executable(destination)

    if platform == "windows":
        missing_required.extend(
            copy_required_windows_runtime(
                arch=arch,
                install_dir=install_dir,
                package_root=package_root,
                allow_missing_binaries=allow_missing_binaries,
            )
        )

    return missing_required


def copy_required_windows_runtime(
    arch: str,
    install_dir: Path,
    package_root: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []
    runtime_files = list_staged_runtime_files(install_dir)

    runtime_flavor = infer_runtime_flavor(install_dir)
    if runtime_flavor != RuntimeFlavor.NONE:
        raise RuntimeError(
            "Windows package staging detected MSVC/UCRT runtime imports. "
            "Public openQ4 packages must be built with the static CRT policy."
        )

    for source in runtime_files:
        copy_regular_file(source, package_root / source.name)

    if not runtime_files:
        if allow_missing_binaries:
            missing_required.append("OpenAL32.dll")
        else:
            raise FileNotFoundError(
                f"required Windows runtime not found for {arch}: {install_dir / 'OpenAL32.dll'}"
            )

    return missing_required


def copy_required_game_binaries(
    platform: str,
    arch: str,
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_game_module_binaries(platform, arch):
        source = install_game_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required game module not found: {source}")
        destination = package_game_dir / filename
        copy_regular_file(source, destination)
        if platform == "macos":
            ensure_posix_executable(destination)

    return missing_required


def copy_required_windows_symbols(
    arch: str,
    install_dir: Path,
    package_root: Path,
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_windows_root_symbols(arch):
        source = install_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required Windows diagnostic symbol not found: {source}")
        copy_regular_file(source, package_root / filename)

    for filename in get_required_windows_game_symbols(arch):
        source = install_game_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(f"{GAME_DIR_NAME}/{filename}")
                continue
            raise FileNotFoundError(f"required Windows game diagnostic symbol not found: {source}")
        copy_regular_file(source, package_game_dir / filename)

    return missing_required


def copy_required_loose_game_files(
    install_game_dir: Path,
    package_game_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for relative_path in sorted(OPENQ4_REQUIRED_LOOSE_GAME_FILES):
        source = install_game_dir / Path(relative_path)
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(relative_path)
                continue
            raise FileNotFoundError(f"required loose game file not found: {source}")

        destination = package_game_dir / Path(relative_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        copy_regular_file(source, destination)

    return missing_required


def create_game_pk4(
    install_game_dir: Path, destination_pk4: Path, pak_name: str = PAK0_NAME
) -> tuple[int, list[str], list[str]]:
    result = create_openq4_game_pk4(install_game_dir, destination_pk4, pak_name=pak_name)
    return result.added_files, result.skipped_samples, result.missing_required


def create_macos_dmg(package_root: Path, archive_path: Path) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("macOS DMG packaging requires a macOS host")

    validate_macos_dmg_source_tree(package_root)
    validate_archive_output_outside_input_tree(package_root, archive_path, "macOS DMG output")
    prepare_archive_output_path(archive_path, "macOS DMG output")
    hdiutil_path = shutil.which("hdiutil")
    if hdiutil_path is None:
        raise RuntimeError("macOS DMG packaging requires hdiutil")

    run_macos_command(
        [
            hdiutil_path,
            "create",
            "-volname",
            package_root.name,
            "-srcfolder",
            str(package_root),
            "-format",
            "UDZO",
            "-ov",
            str(archive_path),
        ],
        label=f"creating macOS DMG {archive_path}",
    )


def create_release_archive(
    package_root: Path,
    archive_path: Path,
    archive_format: str,
    executable_relative_paths: set[Path] | None = None,
) -> None:
    validate_archive_output_outside_input_tree(package_root, archive_path, "release archive output")
    prepare_archive_output_path(archive_path, "release archive output")

    symlinks = [path for path in package_root.rglob("*") if path.is_symlink()]
    if symlinks:
        joined = ", ".join(path.relative_to(package_root).as_posix() for path in symlinks[:10])
        raise RuntimeError(f"release archive input contains symlink entries: {joined}")

    executable_archive_paths = {
        relative_path.as_posix()
        for relative_path in (executable_relative_paths or set())
    }

    if archive_format == "dmg":
        create_macos_dmg(package_root, archive_path)
        return

    if archive_format == "zip":
        with ZipFile(archive_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(package_root.rglob("*")):
                if not path.is_file():
                    continue
                relative_path = path.relative_to(package_root)
                arcname = (Path(package_root.name) / relative_path).as_posix()
                info = ZipInfo(arcname, date_time=DETERMINISTIC_ARCHIVE_TIMESTAMP)
                info.compress_type = ZIP_DEFLATED
                mode = 0o100644
                if relative_path.as_posix() in executable_archive_paths:
                    mode = 0o100755
                info.external_attr = mode << 16
                info.create_system = 3
                with path.open("rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
        return

    mode = {"tar.gz": "w:gz", "tar.xz": "w:xz"}[archive_format]
    with tarfile.open(archive_path, mode) as archive:
        for path in sorted(package_root.rglob("*")):
            if not path.is_file():
                continue
            relative_path = path.relative_to(package_root)
            arcname = (Path(package_root.name) / relative_path).as_posix()
            info = archive.gettarinfo(path, arcname=arcname)
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            info.mtime = DETERMINISTIC_TAR_MTIME
            info.mode = 0o644
            if relative_path.as_posix() in executable_archive_paths:
                info.mode = 0o755
            with path.open("rb") as handle:
                archive.addfile(info, handle)


def get_package_executable_archive_paths(
    platform: str, arch: str, copied_linux_launchers: list[str]
) -> set[Path]:
    if platform not in ("linux", "macos"):
        return set()

    executable_paths = {Path(filename) for filename in get_required_root_binaries(platform, arch)}
    if platform == "linux":
        executable_paths.update(Path(filename) for filename in copied_linux_launchers)
    elif platform == "macos":
        executable_paths.add(Path("openQ4.app") / "Contents" / "MacOS" / "openQ4")
        executable_paths.add(Path(MACOS_SUPPORT_INFO_SCRIPT_NAME))
        executable_paths.update(
            {
                Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"game-sp_{arch}.dylib",
                Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"game-mp_{arch}.dylib",
                Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / f"renderer-vk_{arch}.dylib",
                Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / MACOS_MOLTENVK_DYLIB_NAME,
                Path("openQ4.app") / MACOS_APP_FRAMEWORKS_DIR / MACOS_OPENAL_SOFT_DYLIB_NAME,
            }
        )

    return executable_paths


def validate_macos_plist_values(
    plist: dict,
    label: str,
    version: str | None = None,
    *,
    require_self_contained_runtime: bool = False,
) -> None:
    if not isinstance(plist, dict):
        raise RuntimeError(f"{label} must contain a dictionary root")

    for key, expected in MACOS_EXPECTED_PLIST_VALUES.items():
        if key == MACOS_RUNTIME_LAYOUT_KEY and not require_self_contained_runtime:
            continue
        if plist.get(key) != expected:
            raise RuntimeError(f"{label} {key} is {plist.get(key)!r}; expected {expected!r}")

    if version is not None:
        for key in ("CFBundleShortVersionString", "CFBundleVersion"):
            if plist.get(key) != version:
                raise RuntimeError(f"{label} {key} is {plist.get(key)!r}; expected {version!r}")

    if plist.get("NSHighResolutionCapable") is not True:
        raise RuntimeError(f"{label} must set NSHighResolutionCapable=true")
    if plist.get("NSSupportsAutomaticGraphicsSwitching") is not True:
        raise RuntimeError(f"{label} must set NSSupportsAutomaticGraphicsSwitching=true")


def validate_macos_archive_name(name: str, package_prefix: str) -> None:
    parts = name.split("/")
    if (
        name.startswith("/")
        or "\\" in name
        or re.match(r"^[A-Za-z]:", name) is not None
        or any(ord(character) < 32 or ord(character) == 127 for character in name)
        or "/../" in f"/{name}/"
        or "" in parts
        or any(part in (".", "..") for part in parts)
        or not name.startswith(package_prefix)
    ):
        raise RuntimeError(f"macOS archive contains unsafe or out-of-package path: {name}")


def validate_macos_manifest_archive_filename(value: str, label: str, allowed_suffixes: tuple[str, ...]) -> None:
    if (
        "/" in value
        or "\\" in value
        or re.match(r"^[A-Za-z]:", value) is not None
        or any(ord(character) < 32 or ord(character) == 127 for character in value)
        or value.startswith(".")
        or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", value)
    ):
        raise RuntimeError(f"{label} contains unsafe archive filename: {value}")
    if not any(value.endswith(suffix) for suffix in allowed_suffixes):
        suffixes = ", ".join(allowed_suffixes)
        raise RuntimeError(f"{label} has unsupported archive suffix: {value} (expected one of: {suffixes})")


def validate_macos_archive_mode(name: str, mode: int) -> None:
    if mode & 0o7000:
        raise RuntimeError(f"macOS archive entry has special mode bits: {name} ({mode:o})")
    if mode & 0o022:
        raise RuntimeError(f"macOS archive entry is group/other writable: {name} ({mode:o})")


def is_macos_bounded_metadata_entry(name: str) -> bool:
    return (
        name.endswith("/Info.plist")
        or name.endswith("/PkgInfo")
        or name.endswith("/VERSION.txt")
        or name.endswith(f"/{MACOS_SYMBOL_MANIFEST_NAME}")
        or name.endswith(".lproj/InfoPlist.strings")
        or name.endswith(f".lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}")
        or name.endswith("/_CodeSignature/CodeResources")
    )


def validate_macos_archive_metadata_member_size(name: str, size: int) -> None:
    if is_macos_bounded_metadata_entry(name) and size > MAX_MACOS_METADATA_MEMBER_BYTES:
        raise RuntimeError(f"macOS archive metadata member is too large: {name} ({size} bytes)")


def read_macos_zip_member(archive: ZipFile, name: str, archive_path: Path) -> bytes:
    try:
        return archive.read(name)
    except BadZipFile as exc:
        raise RuntimeError(f"macOS archive is not a valid zip archive: {archive_path}") from exc


def record_macos_archive_entry(
    entry_names: set[str],
    casefold_entry_names: dict[str, str],
    name: str,
    package_prefix: str,
) -> None:
    validate_macos_archive_name(name, package_prefix)
    if name in entry_names:
        raise RuntimeError(f"macOS archive contains duplicate entry: {name}")
    casefold_key = macos_casefold_path_key(name)
    previous = casefold_entry_names.get(casefold_key)
    if previous is not None and previous != name:
        raise RuntimeError(
            "macOS archive contains case-insensitive duplicate entries: "
            f"{previous}, {name}"
        )
    entry_names.add(name)
    casefold_entry_names[casefold_key] = name


def validate_macos_archive_contents(
    package_root: Path, archive_path: Path, archive_format: str, arch: str, version: str
) -> None:
    if archive_path.is_symlink():
        raise RuntimeError(f"macOS archive path must not be a symlink: {archive_path}")
    if not archive_path.is_file():
        raise RuntimeError(f"macOS archive was not created: {archive_path}")
    if archive_format not in {"tar.gz", "tar.xz", "zip"}:
        raise RuntimeError(f"Unsupported macOS archive format for validation: {archive_format}")

    package_prefix = package_root.name + "/"
    app_bundle_prefix = f"{package_prefix}openQ4.app/"
    expected_app_bundle_entries = {
        f"{app_bundle_prefix}{relative_path}"
        for relative_path in MACOS_EXPECTED_APP_BUNDLE_FILES
    }
    embedded_sp_module_entry = f"{app_bundle_prefix}Contents/Frameworks/game-sp_{arch}.dylib"
    embedded_mp_module_entry = f"{app_bundle_prefix}Contents/Frameworks/game-mp_{arch}.dylib"
    embedded_renderer_module_entry = f"{app_bundle_prefix}Contents/Frameworks/renderer-vk_{arch}.dylib"
    embedded_moltenvk_entry = f"{app_bundle_prefix}Contents/Frameworks/{MACOS_MOLTENVK_DYLIB_NAME}"
    embedded_openal_soft_entry = f"{app_bundle_prefix}Contents/Frameworks/{MACOS_OPENAL_SOFT_DYLIB_NAME}"
    expected_app_bundle_entries.update(
        {
            embedded_sp_module_entry,
            embedded_mp_module_entry,
            embedded_renderer_module_entry,
            embedded_moltenvk_entry,
            embedded_openal_soft_entry,
        }
    )
    optional_app_bundle_entries = {
        f"{app_bundle_prefix}{relative_path}"
        for relative_path in MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_FILES
    }
    client_entry = f"{package_prefix}openQ4-client_{arch}"
    dedicated_entry = f"{package_prefix}openQ4-ded_{arch}"
    app_executable_entry = f"{package_prefix}openQ4.app/Contents/MacOS/openQ4"
    support_info_entry = f"{package_prefix}{MACOS_SUPPORT_INFO_SCRIPT_NAME}"
    expected_entries = {
        client_entry,
        dedicated_entry,
        embedded_sp_module_entry,
        embedded_mp_module_entry,
        embedded_renderer_module_entry,
        embedded_moltenvk_entry,
        embedded_openal_soft_entry,
        f"{app_bundle_prefix}Contents/Resources/{GAME_DIR_NAME}/mod.json",
        f"{app_bundle_prefix}Contents/Resources/{GAME_DIR_NAME}/pak0.pk4",
        f"{app_bundle_prefix}Contents/Resources/{GAME_DIR_NAME}/pak1.pk4",
        support_info_entry,
        f"{package_prefix}{MACOS_SYMBOL_MANIFEST_NAME}",
        f"{package_prefix}VERSION.txt",
        f"{package_prefix}openQ4.app/Contents/Info.plist",
        f"{package_prefix}openQ4.app/Contents/PkgInfo",
        app_executable_entry,
        f"{package_prefix}openQ4.app/Contents/Resources/openQ4.icns",
        f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt",
        f"{package_prefix}openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings",
        f"{package_prefix}openQ4.app/Contents/Resources/French.lproj/InfoPlist.strings",
        f"{package_prefix}openQ4.app/Contents/Resources/English.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
        f"{package_prefix}openQ4.app/Contents/Resources/French.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
    }
    executable_entries = {
        client_entry,
        dedicated_entry,
        app_executable_entry,
        support_info_entry,
        embedded_sp_module_entry,
        embedded_mp_module_entry,
        embedded_renderer_module_entry,
        embedded_moltenvk_entry,
        embedded_openal_soft_entry,
    }
    plist_entry = f"{package_prefix}openQ4.app/Contents/Info.plist"

    modes: dict[str, int] = {}
    entry_names: set[str] = set()
    casefold_entry_names: dict[str, str] = {}
    plist_bytes: bytes | None = None
    pkginfo_bytes: bytes | None = None
    root_version_bytes: bytes | None = None
    app_version_bytes: bytes | None = None
    symbol_manifest_bytes: bytes | None = None
    support_info_bytes: bytes | None = None
    localized_info_bytes: dict[str, bytes] = {}
    localized_package_root_error_bytes: dict[str, bytes] = {}
    code_resources_bytes: bytes | None = None
    package_version_tag = macos_package_version_tag_from_name(package_root, arch)
    code_resources_entry = f"{package_prefix}openQ4.app/Contents/_CodeSignature/CodeResources"

    if archive_format == "zip":
        try:
            archive_context = ZipFile(archive_path, "r")
        except BadZipFile as exc:
            raise RuntimeError(f"macOS archive is not a valid zip archive: {archive_path}") from exc
        with archive_context as archive:
            members = archive.infolist()
            if len(members) > MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS:
                raise RuntimeError(
                    "macOS archive contains too many members: "
                    f"{len(members)} (max {MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS})"
                )
            total_member_bytes = 0
            for info in members:
                name = info.filename.rstrip("/")
                if not name:
                    continue
                if info.is_dir():
                    raise RuntimeError(f"macOS archive contains non-regular entry: {name}")
                mode_type = (info.external_attr >> 16) & 0o170000
                if info.create_system == 3 and mode_type == stat.S_IFLNK:
                    raise RuntimeError(f"macOS archive contains symlink entry: {name}")
                if info.create_system == 3 and mode_type not in (0, stat.S_IFREG):
                    raise RuntimeError(f"macOS archive contains non-regular entry: {name}")
                total_member_bytes += info.file_size
                if total_member_bytes > MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES:
                    raise RuntimeError(
                        "macOS archive total expanded size is too large: "
                        f"{total_member_bytes} bytes (max {MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES})"
                    )
                record_macos_archive_entry(entry_names, casefold_entry_names, name, package_prefix)
                validate_macos_archive_metadata_member_size(name, info.file_size)
                modes[name] = (info.external_attr >> 16) & 0o7777
                validate_macos_archive_mode(name, modes[name])
            if plist_entry in entry_names:
                plist_bytes = read_macos_zip_member(archive, plist_entry, archive_path)
            pkginfo_entry = f"{package_prefix}openQ4.app/Contents/PkgInfo"
            if pkginfo_entry in entry_names:
                pkginfo_bytes = read_macos_zip_member(archive, pkginfo_entry, archive_path)
            root_version_entry = f"{package_prefix}VERSION.txt"
            app_version_entry = f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt"
            symbol_manifest_entry = f"{package_prefix}{MACOS_SYMBOL_MANIFEST_NAME}"
            if root_version_entry in entry_names:
                root_version_bytes = read_macos_zip_member(archive, root_version_entry, archive_path)
            if app_version_entry in entry_names:
                app_version_bytes = read_macos_zip_member(archive, app_version_entry, archive_path)
            if symbol_manifest_entry in entry_names:
                symbol_manifest_bytes = read_macos_zip_member(archive, symbol_manifest_entry, archive_path)
            if support_info_entry in entry_names:
                support_info_bytes = read_macos_zip_member(archive, support_info_entry, archive_path)
            for locale in MACOS_LOCALIZED_INFO_LOCALES:
                entry = f"{package_prefix}openQ4.app/Contents/Resources/{locale}.lproj/InfoPlist.strings"
                if entry in entry_names:
                    localized_info_bytes[locale] = read_macos_zip_member(archive, entry, archive_path)
                error_entry = f"{package_prefix}openQ4.app/Contents/Resources/{locale}.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}"
                if error_entry in entry_names:
                    localized_package_root_error_bytes[locale] = read_macos_zip_member(archive, error_entry, archive_path)
            if code_resources_entry in entry_names:
                code_resources_bytes = read_macos_zip_member(archive, code_resources_entry, archive_path)
    else:
        try:
            archive_context = tarfile.open(archive_path, MACOS_TAR_ARCHIVE_READ_MODES[archive_format])
            try:
                members = archive_context.getmembers()
            except tarfile.TarError:
                archive_context.close()
                raise
        except tarfile.TarError as exc:
            raise RuntimeError(f"macOS archive is not a valid {archive_format} archive: {archive_path}") from exc
        with archive_context as archive:
            if len(members) > MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS:
                raise RuntimeError(
                    "macOS archive contains too many members: "
                    f"{len(members)} (max {MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS})"
                )
            total_member_bytes = 0
            for member in members:
                name = member.name.rstrip("/")
                if not name:
                    continue
                if member.issym() or member.islnk():
                    raise RuntimeError(f"macOS archive contains symlink entry: {name}")
                if not member.isfile():
                    raise RuntimeError(f"macOS archive contains non-regular entry: {name}")
                total_member_bytes += member.size
                if total_member_bytes > MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES:
                    raise RuntimeError(
                        "macOS archive total expanded size is too large: "
                        f"{total_member_bytes} bytes (max {MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES})"
                    )
                record_macos_archive_entry(entry_names, casefold_entry_names, name, package_prefix)
                validate_macos_archive_metadata_member_size(name, member.size)
                modes[name] = member.mode & 0o7777
                validate_macos_archive_mode(name, modes[name])
                if name == plist_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        plist_bytes = extracted.read()
                elif name == f"{package_prefix}openQ4.app/Contents/PkgInfo":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        pkginfo_bytes = extracted.read()
                elif name == f"{package_prefix}VERSION.txt":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        root_version_bytes = extracted.read()
                elif name == f"{package_prefix}openQ4.app/Contents/Resources/VERSION.txt":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        app_version_bytes = extracted.read()
                elif name == f"{package_prefix}{MACOS_SYMBOL_MANIFEST_NAME}":
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        symbol_manifest_bytes = extracted.read()
                elif name == support_info_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        support_info_bytes = extracted.read()
                elif name.startswith(f"{package_prefix}openQ4.app/Contents/Resources/") and name.endswith(".lproj/InfoPlist.strings"):
                    locale = Path(name).parts[-2].replace(".lproj", "")
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        localized_info_bytes[locale] = extracted.read()
                elif name.startswith(f"{package_prefix}openQ4.app/Contents/Resources/") and name.endswith(f".lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}"):
                    locale = Path(name).parts[-2].replace(".lproj", "")
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        localized_package_root_error_bytes[locale] = extracted.read()
                elif name == code_resources_entry:
                    extracted = archive.extractfile(member)
                    if extracted is not None:
                        code_resources_bytes = extracted.read()

    bad_metadata_names = [
        name
        for name in sorted(entry_names)
        if is_macos_non_runtime_metadata_path(Path(name))
    ]
    if bad_metadata_names:
        joined = ", ".join(bad_metadata_names[:5])
        raise RuntimeError(f"macOS archive contains non-runtime metadata/debug entries: {joined}")

    missing_entries = sorted(expected_entries - entry_names)
    if missing_entries:
        joined = ", ".join(missing_entries)
        raise RuntimeError(f"macOS archive is missing required entries: {joined}")

    unexpected_app_bundle_entries = sorted(
        name
        for name in entry_names
        if name.startswith(app_bundle_prefix)
        and name not in expected_app_bundle_entries
        and name not in optional_app_bundle_entries
    )
    if unexpected_app_bundle_entries:
        joined = ", ".join(unexpected_app_bundle_entries[:10])
        raise RuntimeError(f"macOS archive app bundle contains unexpected entries: {joined}")
    if code_resources_entry in entry_names:
        if code_resources_bytes is None:
            raise RuntimeError(f"macOS archive code signature resources are unreadable: {code_resources_entry}")
        validate_macos_code_resources_bytes(
            code_resources_bytes,
            "macOS archive code signature resources",
        )

    expected_game_modules = {
        embedded_sp_module_entry,
        embedded_mp_module_entry,
    }
    unexpected_game_modules = sorted(
        name
        for name in entry_names
        if (
            name.startswith(f"{package_prefix}{GAME_DIR_NAME}/game-")
            or name.startswith(f"{app_bundle_prefix}Contents/Frameworks/game-")
            or name.startswith(f"{app_bundle_prefix}Contents/Resources/{GAME_DIR_NAME}/game-")
        )
        and Path(name).name.lower().endswith((".dll", ".so", ".dylib"))
        and name not in expected_game_modules
    )
    if unexpected_game_modules:
        joined = ", ".join(unexpected_game_modules[:5])
        raise RuntimeError(
            f"macOS archive contains stale or mismatched game modules, including wrong-platform entries: {joined}"
        )

    # Same sweep for renderer modules: a leftover renderer-vk_x64.dylib inside an
    # arm64 package, or a renderer-gl module that darwin never builds, must not
    # ride along beside the one module the loader will actually open.
    expected_renderer_modules = {embedded_renderer_module_entry}
    unexpected_renderer_modules = sorted(
        name
        for name in entry_names
        if (
            name.startswith(f"{package_prefix}renderer-")
            or name.startswith(f"{app_bundle_prefix}Contents/Frameworks/renderer-")
            or name.startswith(f"{app_bundle_prefix}Contents/Resources/{GAME_DIR_NAME}/renderer-")
        )
        and Path(name).name.lower().endswith((".dll", ".so", ".dylib"))
        and name not in expected_renderer_modules
    )
    if unexpected_renderer_modules:
        joined = ", ".join(unexpected_renderer_modules[:5])
        raise RuntimeError(
            "macOS archive contains stale or mismatched renderer modules, "
            f"including wrong-architecture entries: {joined}"
        )

    # libMoltenVK.dylib must appear exactly once, inside the app bundle. A second
    # copy loose in the package root would be a different (possibly unsigned)
    # image than the one SDL and volk pin at runtime.
    unexpected_moltenvk = sorted(
        name
        for name in entry_names
        if Path(name).name == MACOS_MOLTENVK_DYLIB_NAME and name != embedded_moltenvk_entry
    )
    if unexpected_moltenvk:
        joined = ", ".join(unexpected_moltenvk[:5])
        raise RuntimeError(f"macOS archive contains misplaced MoltenVK runtime copies: {joined}")

    unexpected_openal_soft = sorted(
        name
        for name in entry_names
        if Path(name).name == MACOS_OPENAL_SOFT_DYLIB_NAME and name != embedded_openal_soft_entry
    )
    if unexpected_openal_soft:
        joined = ", ".join(unexpected_openal_soft[:5])
        raise RuntimeError(f"macOS archive contains misplaced OpenAL Soft runtime copies: {joined}")

    expected_root_binaries = {client_entry, dedicated_entry}
    unexpected_root_binaries = sorted(
        name
        for name in entry_names
        if name.startswith(package_prefix)
        and "/" not in name.removeprefix(package_prefix)
        and is_macos_root_engine_binary_name(Path(name).name)
        and name not in expected_root_binaries
    )
    if unexpected_root_binaries:
        joined = ", ".join(unexpected_root_binaries[:5])
        raise RuntimeError(
            f"macOS archive contains stale or mismatched root engine binaries, including wrong-architecture entries: {joined}"
        )

    for entry in sorted(executable_entries):
        if modes.get(entry, 0) & 0o111 == 0:
            raise RuntimeError(f"macOS archive entry is not executable: {entry}")

    if plist_bytes is None:
        raise RuntimeError(f"macOS archive Info.plist is unreadable: {plist_entry}")
    if pkginfo_bytes != MACOS_PKGINFO_BYTES:
        raise RuntimeError("macOS archive PkgInfo is missing or invalid")
    if root_version_bytes is None or app_version_bytes is None:
        raise RuntimeError("macOS archive version manifests are unreadable")
    if symbol_manifest_bytes is None:
        raise RuntimeError("macOS archive symbol manifest is unreadable")
    if support_info_bytes is None:
        raise RuntimeError("macOS archive support collector is unreadable")
    validate_macos_support_info_script_bytes(
        support_info_bytes,
        "macOS archive support collector",
    )
    validate_version_manifest_bytes(
        root_version_bytes,
        "macOS archive package",
        version=version,
        version_tag=package_version_tag,
        platform="macos",
        arch=arch,
    )
    validate_version_manifest_bytes(
        app_version_bytes,
        "macOS archive app",
        version=version,
        version_tag=package_version_tag,
        platform="macos",
        arch=arch,
    )
    validate_macos_symbol_manifest_bytes(
        symbol_manifest_bytes,
        "macOS archive symbol manifest",
        version=version,
        version_tag=package_version_tag,
        arch=arch,
        package_suffix=macos_package_suffix_from_name(package_root, arch),
        runtime_archive_name=archive_path.name,
        symbol_archive_name=f"{package_root.name}-symbols{MACOS_SYMBOL_ARCHIVE_SUFFIX}",
    )
    for locale in MACOS_LOCALIZED_INFO_LOCALES:
        data = localized_info_bytes.get(locale)
        if data is None:
            raise RuntimeError(f"macOS archive missing {locale} localized InfoPlist.strings")
        validate_macos_localized_info_bytes(data, f"macOS archive {locale} InfoPlist.strings", version)
        error_data = localized_package_root_error_bytes.get(locale)
        if error_data is None:
            raise RuntimeError(f"macOS archive missing {locale} localized package-root error strings")
        validate_macos_package_root_error_bytes(
            error_data,
            f"macOS archive {locale} package-root error strings",
            locale,
        )
    try:
        validate_macos_plist_values(
            plistlib.loads(plist_bytes),
            "macOS archive Info.plist",
            version,
            require_self_contained_runtime=True,
        )
    except plistlib.InvalidFileException as exc:
        raise RuntimeError(f"macOS archive Info.plist is invalid: {plist_entry}") from exc


def macos_otool_dependencies(binary_path: Path, *, macho_arch: str | None = None) -> list[str]:
    if sys.platform != "darwin":
        return []

    otool_path = shutil.which("otool")
    if otool_path is None:
        raise RuntimeError("macOS dependency validation requires otool")

    completed = subprocess.run(
        [otool_path, "-L", *(["-arch", macho_arch] if macho_arch else []), str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"otool failed for macOS binary {binary_path}: {message}")

    dependencies: list[str] = []
    for line in completed.stdout.splitlines()[1:]:
        dependency = line.strip().split(" (", 1)[0].strip()
        if dependency:
            dependencies.append(dependency)

    if binary_path.suffix == ".dylib" and dependencies:
        # The first entry for a dylib is its install name, not a library it loads.
        dependencies = dependencies[1:]

    return dependencies


def macos_otool_install_name(binary_path: Path, *, macho_arch: str | None = None) -> str:
    if sys.platform != "darwin" or binary_path.suffix != ".dylib":
        return ""

    otool_path = shutil.which("otool")
    if otool_path is None:
        raise RuntimeError("macOS install-name validation requires otool")

    completed = subprocess.run(
        [otool_path, "-D", *(["-arch", macho_arch] if macho_arch else []), str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"otool failed for macOS dylib {binary_path}: {message}")

    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return lines[1] if len(lines) > 1 else ""


def macos_dependency_validation_binaries(package_root: Path, arch: str) -> list[Path]:
    return [
        package_root / f"{PRODUCT_NAME}-client_{arch}",
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
        *macos_embedded_library_paths(package_root, arch),
    ]


def macos_otool_minimum_os_version(binary_path: Path, *, macho_arch: str | None = None) -> str:
    if sys.platform != "darwin":
        return ""

    otool_path = shutil.which("otool")
    if otool_path is None:
        raise RuntimeError("macOS minimum-OS validation requires otool")

    completed = subprocess.run(
        [otool_path, "-l", *(["-arch", macho_arch] if macho_arch else []), str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"otool failed for macOS binary {binary_path}: {message}")

    build_version_minos = ""
    version_min_macosx = ""
    current_command = ""
    for line in completed.stdout.splitlines():
        fields = line.strip().split()
        if len(fields) < 2:
            continue
        if fields[0] == "cmd":
            current_command = fields[1]
        elif current_command == "LC_BUILD_VERSION" and fields[0] == "minos":
            build_version_minos = fields[1]
        elif current_command == "LC_VERSION_MIN_MACOSX" and fields[0] == "version":
            version_min_macosx = fields[1]

    return build_version_minos or version_min_macosx


def macos_version_components(version: str, label: str) -> tuple[int, ...]:
    try:
        components = tuple(int(component) for component in version.split("."))
    except ValueError as exc:
        raise RuntimeError(f"{label} is not a dotted-numeric macOS version: {version!r}") from exc

    # Trailing zero components are insignificant ("11.0.0" == "11.0" == "11").
    while components and components[-1] == 0:
        components = components[:-1]
    return components


def validate_macos_binary_minimum_os_versions(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    declared_floor = macos_version_components(
        MACOS_MIN_SYSTEM_VERSION, "macOS declared minimum system version"
    )
    for binary_path in macos_dependency_validation_binaries(package_root, arch):
        require_packaged_executable(binary_path, "macOS minimum-OS validation binary")
        for macho_arch in sorted(macos_expected_lipo_arches(arch)):
            minimum_os = macos_otool_minimum_os_version(binary_path, macho_arch=macho_arch)
            if not minimum_os:
                raise RuntimeError(
                    "macOS binary is missing LC_BUILD_VERSION/LC_VERSION_MIN_MACOSX "
                    f"minimum-OS metadata: {binary_path} ({macho_arch})"
                )
            binary_floor = macos_version_components(
                minimum_os, f"macOS binary minimum-OS metadata for {binary_path} ({macho_arch})"
            )
            if binary_floor > declared_floor:
                raise RuntimeError(
                    "macOS binary minimum OS exceeds the declared "
                    f"LSMinimumSystemVersion floor: {binary_path} ({macho_arch}): "
                    f"{minimum_os} > {MACOS_MIN_SYSTEM_VERSION}"
                )


def validate_macos_binary_dependencies(package_root: Path, arch: str) -> None:
    if sys.platform != "darwin":
        return

    binary_paths = macos_dependency_validation_binaries(package_root, arch)

    for binary_path in binary_paths:
        require_packaged_executable(binary_path, "macOS dependency validation binary")

    # Everything openQ4 compiles must be sliced exactly like the package. The
    # vendored libMoltenVK.dylib is a prebuilt universal binary, so it legitimately
    # carries both slices even in a thin package; it only has to contain the ones
    # this package needs.
    moltenvk_path = macos_embedded_moltenvk_path(package_root)
    validate_macos_binary_architectures(
        [binary_path for binary_path in binary_paths if binary_path != moltenvk_path],
        arch,
    )
    validate_macos_prebuilt_binary_architectures([moltenvk_path], arch)

    for binary_path in binary_paths:
        for macho_arch in sorted(macos_expected_lipo_arches(arch)):
            rejected_dependencies = [
                dependency
                for dependency in macos_otool_dependencies(binary_path, macho_arch=macho_arch)
                if not dependency.startswith(MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES)
                and dependency != MACOS_OPENAL_SOFT_INSTALL_NAME
            ]
            if rejected_dependencies:
                joined = ", ".join(rejected_dependencies)
                raise RuntimeError(
                    "macOS binary has unbundled non-system dependencies: "
                    f"{binary_path} ({macho_arch}): {joined}"
                )

    # The executable must bind the bundled runtime through its package-relative
    # identity. Absolute Homebrew/MacPorts paths would pass on the build host and
    # fail on players' systems, while Apple's framework would silently restore
    # the exhausted implementation this package is intended to replace.
    for binary_path in (
        package_root / f"{PRODUCT_NAME}-client_{arch}",
        package_root / "openQ4.app" / "Contents" / "MacOS" / "openQ4",
    ):
        for macho_arch in sorted(macos_expected_lipo_arches(arch)):
            dependencies = macos_otool_dependencies(binary_path, macho_arch=macho_arch)
            openal_dependencies = [
                dependency
                for dependency in dependencies
                if "openal" in dependency.lower()
            ]
            if openal_dependencies != [MACOS_OPENAL_SOFT_INSTALL_NAME]:
                raise RuntimeError(
                    "macOS client does not use exactly one bundled OpenAL Soft dependency: "
                    f"{binary_path} ({macho_arch}): {openal_dependencies}"
                )

    expected_install_names = macos_loadable_module_install_names(package_root, arch)
    for binary_path, expected_install_name in expected_install_names.items():
        for macho_arch in sorted(macos_expected_lipo_arches(arch)):
            actual_install_name = macos_otool_install_name(binary_path, macho_arch=macho_arch)
            if actual_install_name != expected_install_name:
                raise RuntimeError(
                    "macOS loadable module install name is not package-relative: "
                    f"{binary_path} ({macho_arch}): {actual_install_name!r}; expected {expected_install_name!r}"
                )


def copy_optional_share_tree(platform: str, install_dir: Path, package_root: Path) -> bool:
    if platform != "linux":
        return False

    share_source = install_dir / "share"
    if not share_source.is_dir():
        return False

    share_dest = package_root / "share"
    copy_regular_tree(share_source, share_dest)
    return True


def copy_optional_linux_launchers(install_dir: Path, package_root: Path) -> list[str]:
    copied: list[str] = []

    for filename in ("openQ4-steamdeck",):
        source = install_dir / filename
        if not source.is_file():
            continue

        destination = package_root / filename
        copy_regular_file(source, destination)
        os.chmod(destination, 0o755)
        copied.append(filename)

    return copied


def desktop_entry_exec(path: Path) -> str:
    try:
        return parse_linux_desktop_entry_exec(path)
    except LinuxMetadataError as exc:
        raise RuntimeError(str(exc)) from exc


def desktop_exec_command(exec_line: str) -> str:
    try:
        return parse_linux_desktop_exec_command(exec_line)
    except LinuxMetadataError as exc:
        raise ValueError(str(exc)) from exc


def require_packaged_executable(path: Path, label: str, *, allow_missing: bool = False) -> None:
    if not path.is_file():
        if allow_missing:
            return
        raise RuntimeError(f"{label} is missing from the package: {path}")
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"{label} is not executable in the package: {path}")


def validate_linux_steamdeck_launcher(path: Path, expected_client: str) -> None:
    try:
        launcher = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"Linux Steam Deck launcher is unreadable: {path}") from exc

    required_tokens = (
        "OPENQ4_STEAMDECK",
        "OPENQ4_FORCE_X11",
        "SDL_VIDEO_DRIVER=x11",
        "SDL_VIDEODRIVER=x11",
        "+set com_platformProfile steamdeck",
    )
    for token in required_tokens:
        if token not in launcher:
            raise RuntimeError(f"Linux Steam Deck launcher is missing {token!r}: {path}")

    if "WAYLAND_DISPLAY" in launcher:
        raise RuntimeError(f"Linux Steam Deck launcher still forces X11 from Wayland session state: {path}")

    if expected_client not in launcher:
        raise RuntimeError(
            f"Linux Steam Deck launcher does not exec the packaged client {expected_client!r}: {path}"
        )


def validate_linux_package_metadata(package_root: Path, arch: str, *, allow_missing_binaries: bool = False) -> None:
    client_binary = f"{PRODUCT_NAME}-client_{arch}"
    require_packaged_executable(
        package_root / client_binary,
        "Linux client binary",
        allow_missing=allow_missing_binaries,
    )
    require_packaged_executable(
        package_root / f"{PRODUCT_NAME}-ded_{arch}",
        "Linux dedicated-server binary",
        allow_missing=allow_missing_binaries,
    )
    steamdeck_launcher = package_root / "openQ4-steamdeck"
    require_packaged_executable(steamdeck_launcher, "Linux Steam Deck launcher")
    validate_linux_steamdeck_launcher(steamdeck_launcher, client_binary)

    desktop_dir = package_root / "share" / "applications"
    expected_exec = {
        "openq4.desktop": client_binary,
        "openq4-steamdeck.desktop": "openQ4-steamdeck",
    }
    for filename, expected_command in expected_exec.items():
        desktop_path = desktop_dir / filename
        if not desktop_path.is_file():
            raise RuntimeError(f"Linux desktop entry is missing from the package: {desktop_path}")

        try:
            exec_command = desktop_exec_command(desktop_entry_exec(desktop_path))
        except ValueError as exc:
            raise RuntimeError(f"Linux desktop entry {desktop_path} has an invalid Exec line: {exc}") from exc
        if exec_command != expected_command:
            raise RuntimeError(
                f"Linux desktop entry {desktop_path} points at {exec_command or '<empty>'!r}; "
                f"expected {expected_command!r}"
            )


def require_non_empty_package_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"{label} is missing: {path}")
    if path.stat().st_size == 0:
        raise RuntimeError(f"{label} is empty: {path}")


def validate_macos_app_bundle(package_root: Path, app_root: Path, arch: str, version: str) -> None:
    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    require_packaged_executable(client_binary, "macOS client binary")
    embedded_sp_module, embedded_mp_module = macos_embedded_game_module_paths(package_root, arch)

    expected_bundle_dirs = {Path(relative_path) for relative_path in MACOS_EXPECTED_APP_BUNDLE_DIRS}
    embedded_renderer_module = macos_embedded_renderer_module_path(package_root, arch)
    embedded_moltenvk = macos_embedded_moltenvk_path(package_root)
    embedded_openal_soft = macos_embedded_openal_soft_path(package_root)

    expected_bundle_files = {Path(relative_path) for relative_path in MACOS_EXPECTED_APP_BUNDLE_FILES}
    expected_bundle_files.update(
        {
            embedded_sp_module.relative_to(app_root),
            embedded_mp_module.relative_to(app_root),
            embedded_renderer_module.relative_to(app_root),
            embedded_moltenvk.relative_to(app_root),
            embedded_openal_soft.relative_to(app_root),
        }
    )
    optional_signature_dirs = {Path(relative_path) for relative_path in MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_DIRS}
    optional_signature_files = {Path(relative_path) for relative_path in MACOS_OPTIONAL_APP_BUNDLE_SIGNATURE_FILES}
    allowed_bundle_dirs = expected_bundle_dirs | optional_signature_dirs
    allowed_bundle_files = expected_bundle_files | optional_signature_files
    actual_bundle_dirs = {
        path.relative_to(app_root)
        for path in app_root.rglob("*")
        if path.is_dir()
    }
    actual_bundle_files = {
        path.relative_to(app_root)
        for path in app_root.rglob("*")
        if path.is_file()
    }
    unexpected_bundle_files = sorted(actual_bundle_files - allowed_bundle_files)
    if unexpected_bundle_files:
        joined = ", ".join(path.as_posix() for path in unexpected_bundle_files[:10])
        raise RuntimeError(f"macOS app bundle contains unexpected files: {joined}")
    missing_bundle_dirs = sorted(expected_bundle_dirs - actual_bundle_dirs)
    if missing_bundle_dirs:
        joined = ", ".join(path.as_posix() for path in missing_bundle_dirs[:10])
        raise RuntimeError(f"macOS app bundle is missing required directories: {joined}")
    unexpected_bundle_dirs = sorted(actual_bundle_dirs - allowed_bundle_dirs)
    if unexpected_bundle_dirs:
        joined = ", ".join(path.as_posix() for path in unexpected_bundle_dirs[:10])
        raise RuntimeError(f"macOS app bundle contains unexpected directories: {joined}")
    signature_dir = Path("Contents/_CodeSignature")
    signature_resources = Path("Contents/_CodeSignature/CodeResources")
    if signature_dir in actual_bundle_dirs or signature_resources in actual_bundle_files:
        if signature_resources not in actual_bundle_files:
            raise RuntimeError("macOS app code signature resources are missing")
        try:
            signature_bytes = (app_root / signature_resources).read_bytes()
        except OSError as exc:
            raise RuntimeError("macOS app code signature resources are unreadable") from exc
        validate_macos_code_resources_bytes(
            signature_bytes,
            "macOS app code signature resources",
        )

    package_game_dir = package_root / GAME_DIR_NAME
    if package_game_dir.exists():
        raise RuntimeError(
            f"macOS package retained adjacent {GAME_DIR_NAME}/ instead of embedding it in openQ4.app: {package_game_dir}"
        )

    app_contents = app_root / "Contents"
    app_plist = app_contents / "Info.plist"
    app_executable = app_contents / "MacOS" / "openQ4"
    app_icon = app_contents / "Resources" / "openQ4.icns"
    app_version = app_contents / "Resources" / "VERSION.txt"
    app_pkginfo = app_contents / "PkgInfo"

    require_non_empty_package_file(app_plist, "macOS app bundle Info.plist")
    if not app_executable.is_file() or not os.access(app_executable, os.X_OK):
        raise RuntimeError(f"macOS app executable is missing or not executable: {app_executable}")
    require_non_empty_package_file(app_icon, "macOS app bundle icon")
    require_non_empty_package_file(app_version, "macOS app bundle version manifest")
    for relative_path in (
        MACOS_APP_GAME_DATA_DIR / "mod.json",
        MACOS_APP_GAME_DATA_DIR / "pak0.pk4",
        MACOS_APP_GAME_DATA_DIR / "pak1.pk4",
        MACOS_APP_SPLASH_DIR / "quake4_rt_bitmap_4001.bmp",
    ):
        require_non_empty_package_file(app_root / relative_path, f"macOS embedded runtime file {relative_path}")
    require_packaged_executable(embedded_sp_module, "macOS embedded SP game module")
    require_packaged_executable(embedded_mp_module, "macOS embedded MP game module")
    require_packaged_executable(embedded_renderer_module, "macOS embedded Vulkan renderer module")
    require_packaged_executable(embedded_moltenvk, "macOS embedded MoltenVK runtime")
    require_packaged_executable(embedded_openal_soft, "macOS embedded OpenAL Soft runtime")
    if not app_pkginfo.is_file() or app_pkginfo.read_bytes() != MACOS_PKGINFO_BYTES:
        raise RuntimeError(f"macOS app bundle is missing a valid PkgInfo file: {app_pkginfo}")

    try:
        plist = plistlib.loads(app_plist.read_bytes())
    except (OSError, plistlib.InvalidFileException) as exc:
        raise RuntimeError(f"macOS app Info.plist is unreadable: {app_plist}") from exc

    validate_macos_plist_values(
        plist,
        "macOS app Info.plist",
        version,
        require_self_contained_runtime=True,
    )


def macos_moltenvk_source_candidates(install_dir: Path) -> tuple[Path, ...]:
    return (
        install_dir / MACOS_MOLTENVK_DYLIB_NAME,
        install_dir / "Frameworks" / MACOS_MOLTENVK_DYLIB_NAME,
    )


def resolve_macos_moltenvk_source(install_dir: Path) -> Path:
    """Locate the staged MoltenVK runtime, or explain how to stage it.

    MoltenVK is a Vulkan-on-Metal translation layer. It is not produced by the
    meson build, so it will simply be absent from the install tree unless it has
    been staged first. Fail with the fix instead of a bare lookup error.
    """

    candidates = macos_moltenvk_source_candidates(install_dir)
    for candidate in candidates:
        if candidate.is_symlink():
            raise RuntimeError(f"macOS MoltenVK runtime must not be a symlink: {candidate}")
        if candidate.is_file():
            return candidate

    searched = ", ".join(str(candidate) for candidate in candidates)
    raise RuntimeError(
        f"macOS package is missing the bundled MoltenVK runtime ({MACOS_MOLTENVK_DYLIB_NAME}). "
        "The Vulkan renderer module reaches the GPU on macOS through MoltenVK, a "
        "Vulkan-on-Metal translation layer that openQ4 does not build, so meson never "
        "installs it. Stage the pinned universal libMoltenVK.dylib before packaging: "
        f"{MACOS_MOLTENVK_PREPARE_SCRIPT} --output-dir {install_dir}. "
        f"Searched: {searched}"
    )


def resolve_macos_openal_soft_source(install_dir: Path) -> Path:
    candidate = install_dir / "Frameworks" / MACOS_OPENAL_SOFT_DYLIB_NAME
    if candidate.is_symlink():
        raise RuntimeError(f"macOS OpenAL Soft runtime must not be a symlink: {candidate}")
    if candidate.is_file() and candidate.stat().st_size > 0:
        return candidate
    raise RuntimeError(
        f"macOS package is missing the bundled OpenAL Soft runtime ({MACOS_OPENAL_SOFT_DYLIB_NAME}). "
        "Build and stage the pinned runtime before packaging: "
        f"{MACOS_OPENAL_SOFT_PREPARE_SCRIPT} --verify-only --output-root <prepared-root> "
        f"--stage-install-dir {install_dir}. Searched: {candidate}"
    )


def copy_macos_openal_soft_licenses(install_dir: Path, app_root: Path) -> None:
    source_root = install_dir / "licenses" / "openal-soft"
    destination_root = app_root / MACOS_APP_OPENAL_LICENSE_DIR
    destination_root.mkdir(parents=True, exist_ok=True)
    for filename in MACOS_OPENAL_SOFT_LICENSE_FILES:
        source = source_root / filename
        require_non_empty_package_file(source, f"macOS OpenAL Soft license/source payload {filename}")
        if source.is_symlink():
            raise RuntimeError(f"macOS OpenAL Soft license/source payload must not be a symlink: {source}")
        copy_regular_file(source, destination_root / filename)


def create_macos_app_bundle(
    package_root: Path,
    install_dir: Path,
    arch: str,
    version: str,
    version_tag: str,
    repository_metadata: dict[str, str] | None = None,
) -> Path:
    app_root = package_root / "openQ4.app"
    app_contents = app_root / "Contents"
    app_frameworks = app_root / MACOS_APP_FRAMEWORKS_DIR
    app_macos = app_contents / "MacOS"
    app_resources = app_contents / "Resources"

    app_frameworks.mkdir(parents=True, exist_ok=True)
    app_macos.mkdir(parents=True, exist_ok=True)
    app_resources.mkdir(parents=True, exist_ok=True)

    client_binary = package_root / f"{PRODUCT_NAME}-client_{arch}"
    if not client_binary.is_file():
        raise RuntimeError(f"macOS client binary is missing before app bundle creation: {client_binary}")

    app_executable = app_macos / "openQ4"
    copy_regular_file(client_binary, app_executable)
    os.chmod(app_executable, 0o755)

    staged_game_dir = package_root / GAME_DIR_NAME
    if not staged_game_dir.is_dir():
        raise RuntimeError(
            f"macOS staged game directory is missing before self-contained app creation: {staged_game_dir}"
        )
    staged_sp_module = staged_game_dir / f"game-sp_{arch}.dylib"
    staged_mp_module = staged_game_dir / f"game-mp_{arch}.dylib"
    for path, label in (
        (staged_game_dir / "mod.json", "macOS staged game metadata"),
        (staged_game_dir / "pak0.pk4", "macOS staged pak0"),
        (staged_game_dir / "pak1.pk4", "macOS staged pak1"),
    ):
        require_non_empty_package_file(path, label)
    require_packaged_executable(staged_sp_module, "macOS staged SP game module")
    require_packaged_executable(staged_mp_module, "macOS staged MP game module")

    embedded_game_dir = app_root / MACOS_APP_GAME_DATA_DIR
    shutil.move(str(staged_game_dir), str(embedded_game_dir))
    shutil.move(
        str(embedded_game_dir / staged_sp_module.name),
        str(app_frameworks / staged_sp_module.name),
    )
    shutil.move(
        str(embedded_game_dir / staged_mp_module.name),
        str(app_frameworks / staged_mp_module.name),
    )

    # The renderer module is installed beside the engine binaries, so it lands in
    # the package root first. Move it into the same signed Contents/Frameworks
    # code directory the game modules use; RM_ResolveModulePath() searches the
    # trusted module root, which is exactly that directory.
    staged_renderer_module = package_root / f"renderer-vk_{arch}.dylib"
    require_packaged_executable(staged_renderer_module, "macOS staged Vulkan renderer module")
    embedded_renderer_module = app_frameworks / staged_renderer_module.name
    shutil.move(str(staged_renderer_module), str(embedded_renderer_module))
    ensure_posix_executable(embedded_renderer_module)

    # MoltenVK is staged separately (see resolve_macos_moltenvk_source) and is
    # copied, not moved, because it never lands in the package root. It must sit
    # beside the renderer module: both SDL and volk pin
    # <exe>/../Frameworks/libMoltenVK.dylib so the engine and the module bind the
    # same image.
    staged_moltenvk = resolve_macos_moltenvk_source(install_dir)
    embedded_moltenvk = app_frameworks / MACOS_MOLTENVK_DYLIB_NAME
    copy_regular_file(staged_moltenvk, embedded_moltenvk)
    ensure_posix_executable(embedded_moltenvk)

    # OpenAL Soft replaces Apple's deprecated, buffer-limited OpenAL framework.
    # Keep the dynamically linked runtime beside the other signed libraries and
    # carry its exact license and corresponding source in Resources.
    staged_openal_soft = resolve_macos_openal_soft_source(install_dir)
    embedded_openal_soft = app_frameworks / MACOS_OPENAL_SOFT_DYLIB_NAME
    copy_regular_file(staged_openal_soft, embedded_openal_soft)
    ensure_posix_executable(embedded_openal_soft)
    copy_macos_openal_soft_licenses(install_dir, app_root)

    staged_splash_dir = package_root / "assets" / "splash"
    staged_splash = staged_splash_dir / "quake4_rt_bitmap_4001.bmp"
    if not staged_splash.is_file():
        installed_splash = install_dir / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp"
        require_non_empty_package_file(installed_splash, "macOS staged splash image")
        staged_splash_dir.mkdir(parents=True, exist_ok=True)
        copy_regular_file(installed_splash, staged_splash)
    require_non_empty_package_file(staged_splash, "macOS staged splash image")
    embedded_splash_dir = app_root / MACOS_APP_SPLASH_DIR
    embedded_splash_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(staged_splash_dir), str(embedded_splash_dir))
    staged_assets_dir = package_root / "assets"
    if staged_assets_dir.is_dir() and not any(staged_assets_dir.iterdir()):
        staged_assets_dir.rmdir()

    icns_candidates = [
        install_dir / "openQ4.icns",
        install_dir / "quake4.icns",
    ]
    for icns_source in icns_candidates:
        if icns_source.is_file():
            copy_regular_file(icns_source, app_resources / "openQ4.icns")
            break
    if not (app_resources / "openQ4.icns").is_file():
        expected = ", ".join(str(path) for path in icns_candidates)
        raise RuntimeError(f"macOS app icon source was not found in staged install. Expected one of: {expected}")

    info_plist = app_contents / "Info.plist"
    write_text_file(
        info_plist,
        [
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">",
            "<plist version=\"1.0\">",
            "<dict>",
            "<key>CFBundleDevelopmentRegion</key>",
            "<string>English</string>",
            "<key>CFBundleDisplayName</key>",
            "<string>openQ4</string>",
            "<key>CFBundleExecutable</key>",
            "<string>openQ4</string>",
            "<key>CFBundleIconFile</key>",
            "<string>openQ4.icns</string>",
            "<key>CFBundleIdentifier</key>",
            "<string>com.darkmatter.openq4</string>",
            "<key>CFBundleInfoDictionaryVersion</key>",
            "<string>6.0</string>",
            "<key>CFBundleName</key>",
            "<string>openQ4</string>",
            "<key>CFBundlePackageType</key>",
            "<string>APPL</string>",
            "<key>CFBundleShortVersionString</key>",
            f"<string>{version}</string>",
            "<key>CFBundleVersion</key>",
            f"<string>{version}</string>",
            "<key>LSMinimumSystemVersion</key>",
            f"<string>{MACOS_MIN_SYSTEM_VERSION}</string>",
            "<key>LSApplicationCategoryType</key>",
            "<string>public.app-category.games</string>",
            "<key>NSPrincipalClass</key>",
            "<string>NSApplication</string>",
            "<key>NSHighResolutionCapable</key>",
            "<true/>",
            "<key>NSSupportsAutomaticGraphicsSwitching</key>",
            "<true/>",
            f"<key>{MACOS_RUNTIME_LAYOUT_KEY}</key>",
            f"<string>{MACOS_RUNTIME_LAYOUT_VALUE}</string>",
            "</dict>",
            "</plist>",
        ],
    )
    (app_contents / "PkgInfo").write_bytes(MACOS_PKGINFO_BYTES)
    write_macos_localized_info_strings(app_contents, version)
    write_macos_package_root_error_strings(app_contents)
    write_version_manifest(
        app_resources / "VERSION.txt",
        version=version,
        version_tag=version_tag,
        platform="macos",
        arch=arch,
        repository_metadata=repository_metadata,
    )

    return app_root


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.arch == "universal2" and args.platform != "macos":
        print("error: universal2 is only supported for macOS packages", file=sys.stderr)
        return 1

    archive_format = args.archive_format or DEFAULT_ARCHIVE_FORMAT[args.platform]
    archive_suffix = ARCHIVE_SUFFIX[archive_format]
    if archive_format == "dmg" and args.platform != "macos":
        print("error: archive format 'dmg' is only supported for macOS packages", file=sys.stderr)
        return 1

    raw_source_root = Path(args.source_root)
    raw_install_dir = Path(args.install_dir) if args.install_dir is not None else raw_source_root / ".install"
    raw_output_dir = Path(args.output_dir)

    try:
        source_root = require_package_directory(raw_source_root, "source root", must_exist=True)
        install_dir = require_package_directory(raw_install_dir, "install directory", must_exist=True)
        output_dir = require_package_directory(raw_output_dir, "output directory", must_exist=False)
        install_game_dir = require_package_directory(
            install_dir / GAME_DIR_NAME,
            f"{GAME_DIR_NAME} directory",
            must_exist=True,
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        version = validate_package_version(args.version)
        version_tag = validate_package_path_token(args.version_tag, "version tag")
        package_suffix = normalize_package_suffix(args.package_suffix)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    repository_metadata = collect_package_repository_metadata(source_root, args.platform, args.arch)

    package_stem = f"openq4-{version_tag}-{args.platform}-{args.arch}{package_suffix}"
    archive_path = output_dir / f"{package_stem}{archive_suffix}"
    macos_symbol_archive_path: Path | None = None
    try:
        package_root = resolve_package_root(output_dir, package_stem)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True, exist_ok=True)

    try:
        copy_release_collateral(source_root, package_root, args.platform)
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    try:
        generated_docs = generate_release_documentation(
            source_root=source_root,
            package_root=package_root,
            version=version,
            platform=args.platform,
            arch=args.arch,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if generated_docs.page_count <= 0 or not generated_docs.index_path.is_file():
        print("error: release HTML documentation was not generated correctly", file=sys.stderr)
        return 1

    write_version_manifest(
        package_root / "VERSION.txt",
        version=version,
        version_tag=version_tag,
        platform=args.platform,
        arch=args.arch,
        repository_metadata=repository_metadata,
    )
    missing_required = copy_required_binaries(
        args.platform,
        args.arch,
        install_dir,
        package_root,
        args.allow_missing_binaries,
    )

    package_game_dir = package_root / GAME_DIR_NAME
    package_game_dir.mkdir(parents=True, exist_ok=True)
    missing_game_modules = copy_required_game_binaries(
        args.platform,
        args.arch,
        install_game_dir,
        package_game_dir,
        args.allow_missing_binaries,
    )
    missing_symbols: list[str] = []
    if args.platform == "windows":
        missing_symbols = copy_required_windows_symbols(
            args.arch,
            install_dir,
            package_root,
            install_game_dir,
            package_game_dir,
            args.allow_missing_binaries,
        )
    missing_loose_game_files = copy_required_loose_game_files(
        install_game_dir,
        package_game_dir,
        args.allow_missing_binaries,
    )
    try:
        validate_packaged_mod_manifest(package_game_dir, version)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    source_content_dir = source_root / "content" / GAME_DIR_NAME
    pk4_results = []
    for pak_name in OPENQ4_PACK_NAMES:
        game_pk4_path = package_game_dir / pak_name
        staged_game_pk4_path = install_game_dir / pak_name
        source_pack_dir = source_content_dir / Path(pak_name).stem

        if staged_game_pk4_path.is_file():
            pk4_result = copy_game_pk4(staged_game_pk4_path, game_pk4_path, pak_name=pak_name)
        elif source_pack_dir.is_dir():
            pk4_result = create_openq4_game_pk4(source_pack_dir, game_pk4_path, pak_name=pak_name)
        else:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} was not staged and source pack directory is missing: {source_pack_dir}",
                file=sys.stderr,
            )
            return 1

        if pk4_result.added_files == 0:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} packaging found no eligible files after filtering",
                file=sys.stderr,
            )
            return 1
        if pk4_result.missing_required:
            print(
                f"error: {GAME_DIR_NAME}/{pak_name} packaging is missing required runtime files:",
                file=sys.stderr,
            )
            for rel in pk4_result.missing_required:
                print(f"  - {rel}", file=sys.stderr)
            return 1

        pk4_results.append((pak_name, pk4_result))

    copied_share = copy_optional_share_tree(args.platform, install_dir, package_root)
    copied_linux_launchers: list[str] = []
    if args.platform == "linux":
        copied_linux_launchers = copy_optional_linux_launchers(install_dir, package_root)
        try:
            validate_linux_package_metadata(
                package_root,
                args.arch,
                allow_missing_binaries=args.allow_missing_binaries,
            )
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    macos_app_bundle = None
    if args.platform == "macos":
        macos_app_bundle = create_macos_app_bundle(
            package_root,
            install_dir,
            args.arch,
            version,
            version_tag,
            repository_metadata,
        )
        try:
            validate_no_package_symlinks(package_root)
            validate_no_package_special_files(package_root)
            validate_macos_package_file_modes(package_root)
            validate_no_macos_metadata_artifacts(package_root)
            validate_no_macos_casefold_path_collisions(package_root)
            strip_macos_forbidden_xattrs(package_root)
            validate_no_macos_forbidden_xattrs(package_root)
            validate_macos_package_root_engine_binaries(package_root, args.arch)
            normalize_macos_loadable_module_install_names(package_root, args.arch)
            macos_signing = resolve_macos_signing_config(args)
            sign_macos_payload(package_root, args.arch, macos_signing)
            write_macos_symbol_manifest(
                package_root,
                version=version,
                version_tag=version_tag,
                arch=args.arch,
                package_suffix=package_suffix,
                runtime_archive_name=archive_path.name,
            )
            validate_no_macos_metadata_artifacts(package_root)
            validate_no_macos_casefold_path_collisions(package_root)
            strip_macos_forbidden_xattrs(package_root)
            validate_no_macos_forbidden_xattrs(package_root)
            validate_macos_package_root_engine_binaries(package_root, args.arch)
            validate_macos_version_manifests(package_root, args.arch, version, version_tag)
            validate_macos_localized_info_files(package_root, version)
            validate_macos_app_bundle(package_root, macos_app_bundle, args.arch, version)
            validate_macos_binary_dependencies(package_root, args.arch)
            validate_macos_binary_minimum_os_versions(package_root, args.arch)
            verify_macos_codesignature(package_root, args.arch)
            verify_macos_developer_id_signature(package_root, args.arch, macos_signing)
            notarize_macos_app_bundle(package_root, macos_signing)
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    archive_executable_paths = get_package_executable_archive_paths(
        args.platform,
        args.arch,
        copied_linux_launchers,
    )

    try:
        create_release_archive(
            package_root,
            archive_path,
            archive_format,
            archive_executable_paths,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.platform == "macos":
        try:
            if archive_format == "dmg":
                validate_macos_dmg_image(archive_path)
                sign_macos_dmg_image(archive_path, macos_signing)
                verify_macos_dmg_developer_id_signature(archive_path, macos_signing)
                notarize_macos_dmg_image(archive_path, macos_signing)
                validate_macos_dmg_image(archive_path)
            else:
                validate_macos_archive_contents(
                    package_root,
                    archive_path,
                    archive_format,
                    args.arch,
                    version,
                )
            macos_symbol_archive_path = create_macos_symbol_archive(
                package_root,
                output_dir,
                version=version,
                version_tag=version_tag,
                arch=args.arch,
                package_suffix=package_suffix,
                runtime_archive_name=archive_path.name,
            )
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    print(f"Packaged openQ4 release {version} for {args.platform}")
    print(f"Package directory: {package_root}")
    print(f"Release archive: {archive_path}")
    print(f"Archive format: {archive_format}")
    print(f"Version manifest: {package_root / 'VERSION.txt'}")
    print(f"Documentation portal: {generated_docs.index_path} ({generated_docs.page_count} pages)")
    print("openQ4 pk4s:")
    for pak_name, pk4_result in pk4_results:
        print(
            f"  - {package_game_dir / pak_name} "
            f"({pk4_result.added_files} files, md5 {pk4_result.md5_hex})"
        )
    if copied_share:
        print(f"Share payload: {package_root / 'share'}")
    if copied_linux_launchers:
        print("Linux launchers:")
        for filename in copied_linux_launchers:
            print(f"  - {package_root / filename}")
    if missing_symbols:
        print("Missing Windows diagnostic symbols allowed by --allow-missing-binaries:")
        for filename in missing_symbols:
            print(f"  - {filename}")
    if macos_app_bundle is not None:
        print(f"macOS app bundle: {macos_app_bundle}")
    if macos_symbol_archive_path is not None:
        print(f"macOS dSYM symbol archive: {macos_symbol_archive_path}")
    if missing_required:
        print("Missing required runtime binaries:")
        for filename in missing_required:
            print(f"  - {filename}")
    if missing_game_modules:
        print("Missing required game modules:")
        for filename in missing_game_modules:
            print(f"  - {filename}")
    if missing_loose_game_files:
        print("Missing required loose game files:")
        for relative_path in missing_loose_game_files:
            print(f"  - {relative_path}")
    skipped_samples = [
        f"{pak_name}:{sample}"
        for pak_name, pk4_result in pk4_results
        for sample in pk4_result.skipped_samples
    ]
    if skipped_samples:
        print("Filtered sample paths:")
        for rel in skipped_samples:
            print(f"  - {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
