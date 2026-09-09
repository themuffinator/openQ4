#!/usr/bin/env python3
"""Compile exact production I/O and exercise native files plus injected faults.

No engine launch. Keeps command/output/source-hash provenance under repository
.tmp. Cross-platform code execution is only claimed for the running platform;
the suite explicitly reports unsupported filesystem fixtures (e.g. DrvFS FIFO).
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ROOT / "src/framework/DurableFile.h",
    ROOT / "src/framework/DurableFile.cpp",
    ROOT / "tools/tests/native/DurableFileTest.cpp",
    Path(__file__).resolve(),
)


def main() -> int:
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("A C++20 compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="durable-file-", dir=temporary))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    executable = scratch / ("DurableFileTest.exe" if os.name == "nt" else "DurableFileTest")
    production = scratch / ("DurableFile.obj" if os.name == "nt" else "DurableFile.o")
    macro_source = scratch / "macro_compile.cpp"
    # Engine PCH may already have included Windows.h before this standalone
    # source sets NOMINMAX. Exercise the production body with both legacy macros.
    macro_source.write_text("""#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
#include "src/framework/DurableFile.cpp"
""", encoding="utf-8")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT)]
    if os.name != "nt":
        flags.append("-pthread")
    commands = [
        [compiler, *flags, "-c", str(SOURCES[1]), "-o", str(production)],
        [compiler, *flags, "-c", str(macro_source), "-o", str(scratch / "macro_compile.o")],
        [compiler, *flags, str(SOURCES[2]), "-o", str(executable)],
        [str(executable), str(scratch)],
    ]
    evidence = {
        "platform": platform.platform(),
        "scope": "Native exact-path I/O and cooperating-process lease; no power-cut qualification",
        "sources": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in SOURCES},
        "commands": [],
        "passed": False,
    }
    status = 0
    with (scratch / "test.log").open("w", encoding="utf-8") as log:
        for command in commands:
            log.write(json.dumps(command) + "\n")
            result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding="utf-8",
                                    errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=90, check=False)
            log.write(result.stdout + f"\nexit_code={result.returncode}\n")
            log.flush()
            print(result.stdout, end="")
            evidence["commands"].append({"args": command, "exit_code": result.returncode})
            if result.returncode:
                status = result.returncode
                break
    evidence["passed"] = status == 0
    evidence["log_sha256"] = hashlib.sha256((scratch / "test.log").read_bytes()).hexdigest()
    (scratch / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"Durable file test evidence: {scratch / 'result.json'}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
