#!/usr/bin/env python3
"""Build an openQ4 Windows installer from a packaged release directory."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from game_layer import PACKAGED_LAYER_GAME_DIRS  # noqa: E402
from openq4_pak import OPENQ4_PACK_NAMES  # noqa: E402


PRODUCT_NAME = "openQ4"
PRODUCT_PUBLISHER = "DarkMatter Productions"
SETUP_ICON_RELATIVE = Path("assets") / "icons" / "quake4.ico"
TEMPLATE_RELATIVE = Path("tools") / "build" / "openQ4Installer.iss.in"
SUPPORTED_ARCHES = ("x64", "x86", "arm64")
VERSION_RE = re.compile(r"^[0-9]+(?:\.[0-9]+){2,3}$")
FILENAME_TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


ARCHITECTURE_DIRECTIVES = {
    "x64": (
        "ArchitecturesAllowed=x64compatible",
        "ArchitecturesInstallIn64BitMode=x64compatible",
    ),
    "x86": ("", ""),
    "arm64": (
        "ArchitecturesAllowed=arm64",
        "ArchitecturesInstallIn64BitMode=arm64",
    ),
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compile an Inno Setup installer using an already-packaged openQ4 "
            "Windows release directory."
        )
    )
    parser.add_argument(
        "--package-dir",
        required=True,
        help="Prepared openQ4 Windows package directory to embed in the installer.",
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
        "--arch",
        default="x64",
        choices=SUPPORTED_ARCHES,
        help="Target binary architecture tag (default: x64).",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root (used for installer assets).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Directory for the generated installer and temporary .iss script.",
    )
    parser.add_argument(
        "--iscc",
        default=None,
        help="Optional explicit path to ISCC.exe.",
    )
    parser.add_argument(
        "--skip-compile",
        action="store_true",
        help="Only emit the generated .iss file and skip invoking ISCC.exe.",
    )
    return parser.parse_args(argv[1:])


def inno_string_literal(value: Path | str) -> str:
    return '"' + inno_string_contents(value) + '"'


def inno_string_contents(value: Path | str) -> str:
    return str(value).replace('"', '""')


def validate_version(value: str) -> str:
    normalized = value.strip()
    if VERSION_RE.fullmatch(normalized) is None:
        raise ValueError("version must be a dotted numeric release version such as 0.1.010")
    return normalized


def validate_filename_token(value: str, label: str) -> str:
    normalized = value.strip()
    if FILENAME_TOKEN_RE.fullmatch(normalized) is None:
        raise ValueError(f"{label} must be a file-name-safe token without slashes, spaces, or quotes")
    return normalized


def require_regular_file(path: Path, label: str) -> None:
    if path.is_symlink():
        raise FileNotFoundError(f"{label} must not be a symlink: {path}")
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")


def require_directory_arg(path: Path, label: str, *, must_exist: bool) -> Path:
    if path.is_symlink():
        raise FileNotFoundError(f"{label} must not be a symlink: {path}")
    if path.exists() and not path.is_dir():
        raise FileNotFoundError(f"{label} exists but is not a directory: {path}")
    if must_exist and not path.is_dir():
        raise FileNotFoundError(f"{label} not found: {path}")
    return path.resolve()


def validate_output_paths(output_dir: Path, script_path: Path, installer_path: Path) -> None:
    if output_dir.is_symlink():
        raise RuntimeError(f"installer output directory must not be a symlink: {output_dir}")
    if output_dir.exists() and not output_dir.is_dir():
        raise RuntimeError(f"installer output path exists but is not a directory: {output_dir}")
    for path in (script_path, installer_path):
        if path.is_symlink():
            raise RuntimeError(f"refusing to write installer output through symlink: {path}")
        if path.exists() and not path.is_file():
            raise RuntimeError(f"installer output path exists but is not a file: {path}")


def validate_package_dir(package_dir: Path, arch: str) -> None:
    required_paths = [
        package_dir / f"{PRODUCT_NAME}-client_{arch}.exe",
        package_dir / f"{PRODUCT_NAME}-client_{arch}.pdb",
        package_dir / f"{PRODUCT_NAME}-ded_{arch}.exe",
        package_dir / f"{PRODUCT_NAME}-ded_{arch}.pdb",
        package_dir / "OpenAL32.dll",
        package_dir / "README.html",
        package_dir / "LICENSE",
        package_dir / "LICENSING.md",
        package_dir / "LICENSES" / "QUAKE-4-SDK-EULA.rtf",
        package_dir / "docs" / "index.html",
        package_dir / "baseoq4" / "mod.json",
        *(package_dir / "baseoq4" / pack_name for pack_name in OPENQ4_PACK_NAMES),
        package_dir / "baseoq4" / f"game-sp_{arch}.dll",
        package_dir / "baseoq4" / f"game-sp_{arch}.pdb",
        package_dir / "baseoq4" / f"game-mp_{arch}.dll",
        package_dir / "baseoq4" / f"game-mp_{arch}.pdb",
    ]
    # a packaged game-library layer (q4xbase) must be complete: its mod.json, modules and symbols
    for layer in PACKAGED_LAYER_GAME_DIRS:
        layer_dir = package_dir / layer
        if layer_dir.is_symlink():
            raise FileNotFoundError(f"packaged game layer must not be a symlink: {layer_dir}")
        if layer_dir.exists():
            required_paths.append(layer_dir / "mod.json")
            for module in (f"game-sp_{arch}", f"game-mp_{arch}"):
                required_paths.append(layer_dir / f"{module}.dll")
                required_paths.append(layer_dir / f"{module}.pdb")

    missing = [path for path in required_paths if not path.is_file() and not path.is_symlink()]
    if missing:
        joined = "\n".join(f"  - {path}" for path in missing)
        raise FileNotFoundError(
            "required package payload files are missing:\n"
            f"{joined}"
        )
    symlinks = [path for path in required_paths if path.is_symlink()]
    if symlinks:
        joined = "\n".join(f"  - {path}" for path in symlinks)
        raise FileNotFoundError(
            "required package payload files must not be symlinks:\n"
            f"{joined}"
        )


def resolve_iscc_path(explicit_path: str | None) -> Path:
    candidates: list[Path] = []

    if explicit_path:
        candidates.append(Path(explicit_path))

    env_path = os.environ.get("INNO_SETUP_ISCC")
    if env_path:
        candidates.append(Path(env_path))

    for env_var in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(env_var)
        if not base:
            continue
        candidates.append(Path(base) / "Inno Setup 7" / "ISCC.exe")
        candidates.append(Path(base) / "Inno Setup 6" / "ISCC.exe")

    for program in ("ISCC.exe", "iscc"):
        resolved = shutil.which(program)
        if resolved:
            candidates.append(Path(resolved))

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    raise FileNotFoundError(
        "ISCC.exe was not found. Install Inno Setup or pass --iscc explicitly."
    )


def render_installer_script(
    template_text: str,
    *,
    package_dir: Path,
    output_dir: Path,
    version: str,
    version_tag: str,
    arch: str,
    setup_icon_file: Path,
) -> str:
    arch_allowed, arch_install_mode = ARCHITECTURE_DIRECTIVES[arch]
    installer_basename = f"openq4-{version_tag}-windows-{arch}-setup"

    replacements = {
        "@@APP_ARCH@@": arch,
        "@@APP_VERSION@@": version,
        "@@PACKAGE_SOURCE@@": inno_string_contents(package_dir),
        "@@LICENSE_FILE@@": inno_string_literal(package_dir / "LICENSE"),
        "@@LICENSING_FILE@@": inno_string_literal(package_dir / "LICENSING.md"),
        "@@OUTPUT_DIR@@": inno_string_literal(output_dir),
        "@@OUTPUT_BASENAME@@": installer_basename,
        "@@SETUP_ICON_FILE@@": inno_string_literal(setup_icon_file),
        "@@ARCH_ALLOWED_DIRECTIVE@@": arch_allowed,
        "@@ARCH_INSTALL_MODE_DIRECTIVE@@": arch_install_mode,
    }

    rendered = template_text
    for token, value in replacements.items():
        rendered = rendered.replace(token, value)
    return rendered


def compile_installer(iscc_path: Path, script_path: Path) -> None:
    completed = subprocess.run(
        [str(iscc_path), "/Qp", str(script_path)],
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"ISCC.exe failed with exit code {completed.returncode}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        version = validate_version(args.version)
        version_tag = validate_filename_token(args.version_tag, "version tag")
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        package_dir = require_directory_arg(Path(args.package_dir), "package directory", must_exist=True)
        source_root = require_directory_arg(Path(args.source_root), "source root", must_exist=True)
        output_dir = require_directory_arg(Path(args.output_dir), "installer output directory", must_exist=False)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        validate_package_dir(package_dir, args.arch)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    template_path = source_root / TEMPLATE_RELATIVE
    try:
        require_regular_file(template_path, "installer template")
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    setup_icon_file = source_root / SETUP_ICON_RELATIVE
    try:
        require_regular_file(setup_icon_file, "installer icon")
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    script_path = output_dir / f"openq4-{version_tag}-windows-{args.arch}-setup.iss"
    installer_path = output_dir / f"openq4-{version_tag}-windows-{args.arch}-setup.exe"
    try:
        validate_output_paths(output_dir, script_path, installer_path)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    script_text = render_installer_script(
        template_path.read_text(encoding="utf-8"),
        package_dir=package_dir,
        output_dir=output_dir,
        version=version,
        version_tag=version_tag,
        arch=args.arch,
        setup_icon_file=setup_icon_file.resolve(),
    )
    script_path.write_text(script_text, encoding="utf-8", newline="\n")

    if args.skip_compile:
        print(f"Generated installer script: {script_path}")
        print(f"Expected installer path: {installer_path}")
        return 0

    try:
        iscc_path = resolve_iscc_path(args.iscc)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        compile_installer(iscc_path, script_path)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not installer_path.is_file():
        print(
            f"error: expected installer executable not found: {installer_path}",
            file=sys.stderr,
        )
        return 1

    print(f"Built {PRODUCT_NAME} Windows installer {version}")
    print(f"Installer: {installer_path}")
    print(f"Installer script: {script_path}")
    print(f"Compiler: {iscc_path}")
    print(f"Publisher: {PRODUCT_PUBLISHER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
