#!/usr/bin/env python3
"""Run production dmap T-junction fixing against slivers that used to multiply without bound."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--production", type=Path, default=ROOT / "src/tools/compilers/dmap/tritjunction.cpp",
                        help="tritjunction.cpp to test, for checking an older revision")
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    production = args.production.read_text(encoding="utf-8")
    start = production.index("#define\tSNAP_FRACTIONS")
    end = production.index("\n}", production.index("static mapTri_t\t*FixTriangleAgainstHash(", start)) + 2
    fixture = (ROOT / "tools/tests/native/DmapTjunctionSliverTest.cpp").read_text(encoding="utf-8")
    unit = fixture.replace("// @PRODUCTION@", production[start:end])
    work_root = ROOT / ".tmp" / "dmap-tjunction-slivers"
    work_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work_root) as directory:
        work = Path(directory)
        source = work / "test.cpp"
        source.write_text(unit, encoding="utf-8")
        executable = work / ("test.exe" if os.name == "nt" else "test")
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/O2", "/W4", "/WX", str(source), "/Fe" + str(executable)]
            if args.sanitize:
                parser.error("--sanitize requires clang++ or g++")
        else:
            command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)]
            if args.sanitize:
                command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=work, check=True, timeout=120)
        environment = os.environ.copy()
        if args.sanitize and os.name == "nt":
            runtime = subprocess.run([compiler, "-print-runtime-dir"], cwd=work,
                                     check=True, capture_output=True, text=True).stdout.strip()
            if not Path(runtime).is_dir():
                runtime = str(Path(runtime).parent / "windows")
            environment["PATH"] = runtime + os.pathsep + environment.get("PATH", "")
        subprocess.run([str(executable)], cwd=work, env=environment, check=True, timeout=60)


if __name__ == "__main__":
    main()
