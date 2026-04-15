#!/usr/bin/env python3
"""Build an OpenQ4 Windows installer from a packaged release directory."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


PRODUCT_NAME = "OpenQ4"
PRODUCT_PUBLISHER = "DarkMatter Productions"
SETUP_ICON_RELATIVE = Path("assets") / "icons" / "quake4.ico"
TEMPLATE_RELATIVE = Path("tools") / "build" / "OpenQ4Installer.iss.in"
SUPPORTED_ARCHES = ("x64", "x86", "arm64")


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
            "Compile an Inno Setup installer using an already-packaged OpenQ4 "
            "Windows release directory."
        )
    )
    parser.add_argument(
        "--package-dir",
        required=True,
        help="Prepared OpenQ4 Windows package directory to embed in the installer.",
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
        help="OpenQ4 repository root (used for installer assets).",
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
    return '"' + str(value).replace('"', '""') + '"'


def validate_package_dir(package_dir: Path, arch: str) -> None:
    required_paths = [
        package_dir / f"{PRODUCT_NAME}-client_{arch}.exe",
        package_dir / f"{PRODUCT_NAME}-ded_{arch}.exe",
        package_dir / "README.html",
        package_dir / "LICENSE",
        package_dir / "docs" / "index.html",
        package_dir / "baseoq4" / "mod.json",
        package_dir / "baseoq4" / "pak0.pk4",
    ]

    missing = [path for path in required_paths if not path.is_file()]
    if missing:
        joined = "\n".join(f"  - {path}" for path in missing)
        raise FileNotFoundError(
            "required package payload files are missing:\n"
            f"{joined}"
        )

    loadscreen_dir = package_dir / "baseoq4" / "gfx" / "guis" / "loadscreens"
    if not loadscreen_dir.is_dir():
        raise FileNotFoundError(
            "required package payload directory is missing:\n"
            f"  - {loadscreen_dir}"
        )

    has_loadscreen_files = any(path.is_file() for path in loadscreen_dir.rglob("*"))
    if not has_loadscreen_files:
        raise FileNotFoundError(
            "required package payload directory has no files:\n"
            f"  - {loadscreen_dir}"
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
        "@@PACKAGE_SOURCE@@": str(package_dir),
        "@@LICENSE_FILE@@": inno_string_literal(package_dir / "LICENSE"),
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

    package_dir = Path(args.package_dir).resolve()
    source_root = Path(args.source_root).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not package_dir.is_dir():
        print(f"error: package directory not found: {package_dir}", file=sys.stderr)
        return 1

    try:
        validate_package_dir(package_dir, args.arch)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    template_path = source_root / TEMPLATE_RELATIVE
    if not template_path.is_file():
        print(f"error: installer template not found: {template_path}", file=sys.stderr)
        return 1

    setup_icon_file = source_root / SETUP_ICON_RELATIVE
    if not setup_icon_file.is_file():
        print(f"error: installer icon not found: {setup_icon_file}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    script_path = output_dir / f"openq4-{args.version_tag}-windows-{args.arch}-setup.iss"
    script_text = render_installer_script(
        template_path.read_text(encoding="utf-8"),
        package_dir=package_dir,
        output_dir=output_dir,
        version=args.version,
        version_tag=args.version_tag,
        arch=args.arch,
        setup_icon_file=setup_icon_file.resolve(),
    )
    script_path.write_text(script_text, encoding="utf-8", newline="\n")

    installer_path = output_dir / f"openq4-{args.version_tag}-windows-{args.arch}-setup.exe"

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

    print(f"Built {PRODUCT_NAME} Windows installer {args.version}")
    print(f"Installer: {installer_path}")
    print(f"Installer script: {script_path}")
    print(f"Compiler: {iscc_path}")
    print(f"Publisher: {PRODUCT_PUBLISHER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
