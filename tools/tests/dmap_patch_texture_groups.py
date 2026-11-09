#!/usr/bin/env python3
"""Run production dmap texture grouping on curve and brush-side triangles, with a legacy control.

Curve and model triangles keep their own texture coordinates, and the optimizer
welds coincident vertices in a group, so a triangle may only join a group whose
mapping reproduces its coordinates (issue #209). The control builds the fixture
with id's fit and comparison and must accept the road seam the fix refuses.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CONSTANTS = re.compile(r"#define\s+TEXTURE_OFFSET_EQUAL_EPSILON.*?\}\s*texVecSource_t;", re.S)


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


def unit(production: str) -> str:
    constants = CONSTANTS.search(production)
    if not constants:
        raise SystemExit("usurface.cpp must define the texture epsilons and texVecSource_t together")
    pieces = {
        "// @CONSTANTS@": constants.group(0),
        "// @TEXVECSMATCH@": function(production, "static bool TexVecsMatch("),
        "// @PRODUCTION@": "\n".join([function(production, "static bool GroupAcceptsTris("),
                                      function(production, "static int TexVecForTri(")]),
    }
    text = (ROOT / "tools/tests/native/DmapPatchTextureGroupsTest.cpp").read_text(encoding="utf-8")
    for marker, code in pieces.items():
        if marker not in text:
            raise SystemExit(f"fixture marker {marker} is missing")
        text = text.replace(marker, code)
    return text


def build_and_run(compiler: str, sanitize: bool, work: Path, name: str, source_text: str,
                  defines: list[str]) -> subprocess.CompletedProcess:
    source = work / f"{name}.cpp"
    source.write_text(source_text, encoding="utf-8")
    executable = work / (f"{name}.exe" if os.name == "nt" else name)
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/O2", "/W4", "/WX",
                   *[f"/D{d}" for d in defines], str(source), "/Fe" + str(executable)]
    else:
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                   *[f"-D{d}" for d in defines], str(source), "-o", str(executable)]
        if sanitize:
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=work, check=True, timeout=120)
    environment = os.environ.copy()
    if sanitize and os.name == "nt":
        runtime = subprocess.run([compiler, "-print-runtime-dir"], cwd=work,
                                 check=True, capture_output=True, text=True).stdout.strip()
        if not Path(runtime).is_dir():
            runtime = str(Path(runtime).parent / "windows")
        environment["PATH"] = runtime + os.pathsep + environment.get("PATH", "")
    return subprocess.run([str(executable)], cwd=work, env=environment, capture_output=True, text=True, timeout=60)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--production", type=Path, default=ROOT / "src/tools/compilers/dmap/usurface.cpp",
                        help="usurface.cpp to test")
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    if args.sanitize and Path(compiler).name.lower() in ("cl", "cl.exe"):
        parser.error("--sanitize requires clang++ or g++")
    text = unit(args.production.read_text(encoding="utf-8"))
    work_root = ROOT / ".tmp" / "dmap-patch-texture-groups"
    work_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work_root) as directory:
        work = Path(directory)
        result = build_and_run(compiler, args.sanitize, work, "test", text, [])
        print(result.stdout, end="")
        if result.returncode != 0:
            raise SystemExit(result.stderr or f"test exited with {result.returncode}")
        control = build_and_run(compiler, args.sanitize, work, "legacy", text, ["LEGACY_GROUPING"])
        if control.returncode == 0 or "seam" not in control.stderr:
            raise SystemExit("the legacy grouping control did not reproduce the welded road seam: " +
                             (control.stdout + control.stderr).strip())
        print("dmap_patch_texture_groups: legacy grouping control merges the road seam (" +
              control.stderr.strip().splitlines()[0] + ")")


if __name__ == "__main__":
    main()
