#!/usr/bin/env python3
"""Run production dmap triangle regeneration on zero-area triangles, with a mutant."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GUARD = re.compile(r"\n[ \t]*//[^\n]*no area[^\n]*\n[ \t]*//[^\n]*\n[ \t]*if \( MapTriArea\( tri \) <= 0\.0f \) \{"
                   r"\s*FreeTri\( tri \);\s*continue;\s*\}\n")


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


def unit(optimize: str) -> str:
    vector = (ROOT / "src/idlib/math/Vector.h").read_text(encoding="utf-8")
    plane = (ROOT / "src/idlib/math/Plane.h").read_text(encoding="utf-8")
    winding = (ROOT / "src/idlib/geometry/Winding.cpp").read_text(encoding="utf-8")
    tritools = (ROOT / "src/tools/compilers/dmap/tritools.cpp").read_text(encoding="utf-8")
    pieces = {
        "// @IDVEC3@": [function(vector, "ID_INLINE float idVec3::Normalize( void )"),
                        function(vector, "ID_INLINE bool idVec3::FixDegenerateNormal( void )")],
        "// @IDPLANE@": [function(plane, "ID_INLINE float idPlane::Normalize( bool fixDegenerate )"),
                         function(plane, "ID_INLINE bool idPlane::FixDegenerateNormal( void )"),
                         function(plane, "ID_INLINE bool idPlane::FromPoints( const idVec3 &p1, const idVec3 &p2, "
                                         "const idVec3 &p3, bool fixDegenerate )")],
        "// @WINDING@": [function(winding, "float idWinding::TriangleArea(")],
        "// @TRITOOLS@": [function(tritools, "float MapTriArea("), function(tritools, "void\tPlaneForTri(")],
        "// @PRODUCTION@": [function(optimize, "static void FreeOptTriangles("),
                            function(optimize, "static\tvoid\tRegenerateTriangles(")],
    }
    text = (ROOT / "tools/tests/native/DmapOptimizerZeroAreaTest.cpp").read_text(encoding="utf-8")
    for marker, code in pieces.items():
        text = text.replace(marker, "\n".join(code))
    return text


def build_and_run(compiler: str, sanitize: bool, work: Path, name: str, source_text: str) -> subprocess.CompletedProcess:
    source = work / f"{name}.cpp"
    source.write_text(source_text, encoding="utf-8")
    executable = work / (f"{name}.exe" if os.name == "nt" else name)
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/O2", "/W4", "/WX", str(source), "/Fe" + str(executable)]
    else:
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)]
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--production", type=Path, default=ROOT / "src/tools/compilers/dmap/optimize.cpp",
                        help="optimize.cpp to test, for checking an older revision")
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    if args.sanitize and Path(compiler).name.lower() in ("cl", "cl.exe"):
        parser.error("--sanitize requires clang++ or g++")
    production = args.production.read_text(encoding="utf-8")
    work_root = ROOT / ".tmp" / "dmap-optimizer-zero-area"
    work_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work_root) as directory:
        work = Path(directory)
        result = build_and_run(compiler, args.sanitize, work, "test", unit(production))
        print(result.stdout, end="")
        if result.returncode != 0:
            raise SystemExit(result.stderr or f"test exited with {result.returncode}")
        # The mutant removes the zero-area check, as before the fix; skip it when
        # testing a revision that never had one.
        if len(GUARD.findall(production)) == 1:
            result = build_and_run(compiler, args.sanitize, work, "mutant", unit(GUARD.sub("\n", production)))
            if result.returncode == 0 or "zero-area" not in result.stderr:
                raise SystemExit("the mutant without the zero-area check was not caught: " +
                                 (result.stdout + result.stderr).strip())
            print("dmap_optimizer_zero_area: mutant without the zero-area check rejected (" + result.stderr.strip() + ")")
        elif args.production == parser.get_default("production"):
            raise SystemExit("RegenerateTriangles must discard zero-area triangles before its facing test")


if __name__ == "__main__":
    main()
