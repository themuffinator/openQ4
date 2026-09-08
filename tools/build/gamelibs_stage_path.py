#!/usr/bin/env python3
"""Locate the target-specific GameLibs source stage used by Meson."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


MANIFEST_NAME = "openq4_gamelibs_stage_manifest.json"


def stage_root(source_root: Path, system: str, cpu_family: str) -> Path:
    system = {"macos": "darwin"}.get(system, system)
    arch = {"x86_64": "x64", "aarch64": "arm64"}.get(cpu_family, cpu_family)
    if system not in {"windows", "linux", "darwin", "android"}:
        raise ValueError(f"unsupported GameLibs target system: {system}")
    if arch not in {"x64", "x86", "arm64"}:
        raise ValueError(f"unsupported GameLibs target architecture: {arch}")
    name = "gamelibs_stage" if system == "windows" else f"gamelibs_stage-{system}-{arch}"
    return source_root / ".tmp" / name


def build_stage_root(source_root: Path, build_dir: Path) -> Path:
    # Use the target machine, not the machine running this script: a Windows
    # Android build and a Linux ARM64 cross build each have a different stage.
    machines = json.loads((build_dir / "meson-info" / "intro-machines.json").read_text(encoding="utf-8"))
    host = machines["host"]
    return stage_root(source_root, host["system"], host["cpu_family"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--manifest", action="store_true")
    args = parser.parse_args()
    try:
        path = build_stage_root(args.source_root.resolve(), args.build_dir)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.error(f"cannot determine configured GameLibs stage: {exc}")
    if args.manifest:
        path /= MANIFEST_NAME
    print(path.as_posix())


if __name__ == "__main__":
    main()
