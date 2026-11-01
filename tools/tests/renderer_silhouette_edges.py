#!/usr/bin/env python3
"""Run the production silhouette edge builder on surfaces past the old 65536-edge scratch."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function_span(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--production", type=Path, default=ROOT / "src/rendergeo/RenderGeometryTriSurf.cpp",
                        help="RenderGeometryTriSurf.cpp to test, for checking an older revision")
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    production = args.production.read_text(encoding="utf-8")
    state_start = production.index("const int MIN_SIL_EDGES" if "const int MIN_SIL_EDGES" in production
                                   else "const int MAX_SIL_EDGES")
    state_end = production.index("static int\t\t\tnumPlanes;", state_start) + len("static int\t\t\tnumPlanes;")
    if "static void R_SetSilEdgeCapacity(" in production:
        capacity = function_span(production, "static void R_SetSilEdgeCapacity(")
    else:
        capacity = "#define OPENQ4_FIXED_SIL_EDGE_SCRATCH"
    edges_start = production.index("static int c_duplicatedEdges, c_tripledEdges;")
    edges_end = production.index("\n}", production.index("void R_IdentifySilEdges(", edges_start)) + 2
    hash_index = "\n".join(
        line for path in ("src/idlib/containers/HashIndex.h", "src/idlib/containers/HashIndex.cpp")
        for line in (ROOT / path).read_text(encoding="utf-8").splitlines()
        if not line.startswith(("#include", "#pragma hdrstop")))
    fixture = (ROOT / "tools/tests/native/SilhouetteEdgeCapacityTest.cpp").read_text(encoding="utf-8")
    unit = (fixture.replace("// @HASHINDEX@", hash_index)
            .replace("// @PRODUCTION_STATE@", production[state_start:state_end])
            .replace("// @PRODUCTION_CAPACITY@", capacity)
            .replace("// @PRODUCTION_EDGES@", production[edges_start:edges_end]))
    work_root = ROOT / ".tmp" / "renderer-silhouette-edges"
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
        subprocess.run([str(executable)], cwd=work, env=environment, check=True, timeout=120)


if __name__ == "__main__":
    main()
