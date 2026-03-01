#!/usr/bin/env python3
"""Create curated nightly distributable archives for OpenQ4."""

from __future__ import annotations

import argparse
import shutil
import sys
import tarfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


PRODUCT_NAME = "OpenQ4"
SUPPORTED_ARCHES = ("x64", "x86", "arm64")

PLATFORM_EXECUTABLE_EXT = {
    "windows": ".exe",
    "linux": "",
    "macos": "",
}

PLATFORM_BSE_BINARY = {
    "windows": "libbse-q4.dll",
    "linux": "libbse-q4.so",
    "macos": "libbse-q4.dylib",
}

PLATFORM_GAME_MODULE_EXT = {
    "windows": ".dll",
    "linux": ".so",
    "macos": ".dylib",
}

DEFAULT_ARCHIVE_FORMAT = {
    "windows": "zip",
    "linux": "tar.xz",
    "macos": "tar.gz",
}

ARCHIVE_SUFFIX = {
    "zip": ".zip",
    "tar.gz": ".tar.gz",
    "tar.xz": ".tar.xz",
}

OPENQ4_EXCLUDED_DIRS = {"logs", "screenshots"}
OPENQ4_PK4_EXCLUDED_SUFFIXES = {
    ".dll",
    ".so",
    ".dylib",
    ".pdb",
    ".lib",
    ".exp",
    ".ilk",
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package OpenQ4 nightly artifacts into a release archive."
    )
    parser.add_argument(
        "--platform",
        required=True,
        choices=sorted(PLATFORM_BSE_BINARY.keys()),
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
        help="Human-readable nightly version string.",
    )
    parser.add_argument(
        "--version-tag",
        required=True,
        help="File-safe nightly version tag.",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="OpenQ4 repository root.",
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
            "windows=zip, linux=tar.xz, macos=tar.gz)."
        ),
    )
    parser.add_argument(
        "--allow-missing-binaries",
        action="store_true",
        help=(
            "Allow packaging to continue when required platform binaries are missing. "
            "Useful for host bring-up/nightly previews."
        ),
    )
    return parser.parse_args(argv[1:])


def get_required_root_binaries(platform: str, arch: str) -> tuple[str, str]:
    exe_ext = PLATFORM_EXECUTABLE_EXT[platform]
    return (
        f"{PRODUCT_NAME}-client_{arch}{exe_ext}",
        f"{PRODUCT_NAME}-ded_{arch}{exe_ext}",
    )


def get_required_game_module_binaries(platform: str, arch: str) -> tuple[str, str]:
    module_ext = PLATFORM_GAME_MODULE_EXT[platform]
    return (
        f"game-sp_{arch}{module_ext}",
        f"game-mp_{arch}{module_ext}",
    )


def copy_required_binaries(
    platform: str,
    arch: str,
    install_dir: Path,
    package_root: Path,
    allow_missing_binaries: bool,
) -> tuple[list[str], list[str]]:
    copied_optional: list[str] = []
    missing_required: list[str] = []

    for filename in get_required_root_binaries(platform, arch):
        source = install_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required distributable not found: {source}")
        shutil.copy2(source, package_root / filename)

    optional_bse = PLATFORM_BSE_BINARY[platform]
    optional_bse_source = install_dir / optional_bse
    if optional_bse_source.is_file():
        shutil.copy2(optional_bse_source, package_root / optional_bse)
        copied_optional.append(optional_bse)

    return copied_optional, missing_required


def copy_required_game_binaries(
    platform: str,
    arch: str,
    install_openq4_dir: Path,
    package_openq4_dir: Path,
    allow_missing_binaries: bool,
) -> list[str]:
    missing_required: list[str] = []

    for filename in get_required_game_module_binaries(platform, arch):
        source = install_openq4_dir / filename
        if not source.is_file():
            if allow_missing_binaries:
                missing_required.append(filename)
                continue
            raise FileNotFoundError(f"required game module not found: {source}")
        shutil.copy2(source, package_openq4_dir / filename)

    return missing_required


def create_openq4_pk4(
    install_openq4_dir: Path, destination_pk4: Path
) -> tuple[int, list[str]]:
    added_files = 0
    skipped_samples: list[str] = []

    with ZipFile(destination_pk4, "w", compression=ZIP_DEFLATED, compresslevel=9) as pk4:
        for path in sorted(install_openq4_dir.rglob("*")):
            if not path.is_file():
                continue

            rel = path.relative_to(install_openq4_dir)
            rel_parts_lower = {part.lower() for part in rel.parts}

            if rel_parts_lower & OPENQ4_EXCLUDED_DIRS:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            if path.suffix.lower() in OPENQ4_PK4_EXCLUDED_SUFFIXES:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            pk4.write(path, arcname=rel.as_posix())
            added_files += 1

    return added_files, skipped_samples


def create_release_archive(
    package_root: Path, archive_path: Path, archive_format: str
) -> None:
    if archive_path.exists():
        archive_path.unlink()

    if archive_format == "zip":
        with ZipFile(archive_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(package_root.rglob("*")):
                if not path.is_file():
                    continue
                arcname = (Path(package_root.name) / path.relative_to(package_root)).as_posix()
                archive.write(path, arcname=arcname)
        return

    mode = {"tar.gz": "w:gz", "tar.xz": "w:xz"}[archive_format]
    with tarfile.open(archive_path, mode) as archive:
        for path in sorted(package_root.rglob("*")):
            if not path.is_file():
                continue
            arcname = (Path(package_root.name) / path.relative_to(package_root)).as_posix()
            archive.add(path, arcname=arcname, recursive=False)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    archive_format = args.archive_format or DEFAULT_ARCHIVE_FORMAT[args.platform]
    archive_suffix = ARCHIVE_SUFFIX[archive_format]

    source_root = Path(args.source_root).resolve()
    install_dir = (
        Path(args.install_dir).resolve()
        if args.install_dir is not None
        else (source_root / ".install").resolve()
    )
    output_dir = Path(args.output_dir).resolve()

    if not install_dir.is_dir():
        print(f"error: install directory not found: {install_dir}", file=sys.stderr)
        return 1

    readme_path = source_root / "README.md"
    if not readme_path.is_file():
        print(f"error: README.md not found at {readme_path}", file=sys.stderr)
        return 1

    install_openq4_dir = install_dir / "openq4"
    if not install_openq4_dir.is_dir():
        print(f"error: openq4 directory not found: {install_openq4_dir}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    package_stem = f"openq4-{args.version_tag}-{args.platform}"
    package_root = output_dir / package_stem
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True, exist_ok=True)

    shutil.copy2(readme_path, package_root / "README.md")
    copied_optional, missing_required = copy_required_binaries(
        args.platform,
        args.arch,
        install_dir,
        package_root,
        args.allow_missing_binaries,
    )

    openq4_package_dir = package_root / "openq4"
    openq4_package_dir.mkdir(parents=True, exist_ok=True)
    missing_game_modules = copy_required_game_binaries(
        args.platform,
        args.arch,
        install_openq4_dir,
        openq4_package_dir,
        args.allow_missing_binaries,
    )

    openq4_pk4_name = "pak0.pk4"
    openq4_pk4_path = openq4_package_dir / openq4_pk4_name

    added_files, skipped_samples = create_openq4_pk4(
        install_openq4_dir, openq4_pk4_path
    )
    if added_files == 0:
        print(
            "error: openq4 pk4 packaging found no eligible files after filtering",
            file=sys.stderr,
        )
        return 1

    archive_path = output_dir / f"{package_stem}{archive_suffix}"
    create_release_archive(package_root, archive_path, archive_format)

    print(f"Packaged OpenQ4 nightly {args.version} for {args.platform}")
    print(f"Package directory: {package_root}")
    print(f"Release archive: {archive_path}")
    print(f"Archive format: {archive_format}")
    print(f"OpenQ4 pk4: {openq4_pk4_path} ({added_files} files)")
    if missing_required:
        print("Missing required runtime binaries:")
        for filename in missing_required:
            print(f"  - {filename}")
    if missing_game_modules:
        print("Missing required game modules:")
        for filename in missing_game_modules:
            print(f"  - {filename}")
    if not copied_optional:
        print("Optional runtime omitted: libbse-q4 was not present in install directory.")
    if skipped_samples:
        print("Filtered sample paths:")
        for rel in skipped_samples:
            print(f"  - {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
