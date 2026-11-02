#!/usr/bin/env python3
"""Qualify save-slot durability on a real filesystem with production code.

Compiles the production slot commit, recovery and CRC-32 (extracted from
Session.cpp by generate_checkpoint_commit_test.py) into a native harness, then
kills child processes after every commit step and at every staging point and
requires recovery to leave one whole save. --benchmark also times staged write,
sync, both CRC scans and the three-file commit for large payloads.

No engine launch and no input. A killed process is not a power cut: results
qualify process interruption on the tested filesystem only. --dir selects the
filesystem; by default a scratch directory under the repository's .tmp is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ROOT / "src/framework/Session.cpp",
    ROOT / "src/framework/DurableFile.h",
    ROOT / "src/framework/DurableFile.cpp",
    ROOT / "src/framework/SaveGameTransaction.h",
    ROOT / "src/idlib/hashing/CRC32.cpp",
    ROOT / "tools/tests/generate_checkpoint_commit_test.py",
    ROOT / "tools/tests/native/SaveDurabilityTest.cpp",
    Path(__file__).resolve(),
)
COMMAND_TIMEOUT = 1800


def run(command, env):
    began = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=COMMAND_TIMEOUT, check=False)
        output, code = result.stdout, result.returncode
    except subprocess.TimeoutExpired as expired:
        partial = expired.stdout or ""
        output = (partial.decode("utf-8", "replace") if isinstance(partial, bytes) else partial)
        output += f"\nTIMEOUT: no exit after {COMMAND_TIMEOUT} s\n"
        code = 124
    return output, code, round(time.monotonic() - began, 3)


def filesystem_of(path: Path) -> str:
    """Best-effort filesystem name for the evidence record."""
    if os.name == "nt":
        import ctypes
        name = ctypes.create_unicode_buffer(64)
        root = os.path.splitdrive(str(path.resolve()))[0] + "\\"
        if ctypes.windll.kernel32.GetVolumeInformationW(root, None, 0, None, None, None, name, len(name)):
            return name.value
        return "unknown"
    try:
        output = subprocess.run(["df", "-T", str(path)], text=True, capture_output=True, check=False).stdout.splitlines()
        return output[1].split()[1] if len(output) > 1 else "unknown"
    except OSError:
        return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--dir", type=Path, help="existing directory on the filesystem under test")
    parser.add_argument("--benchmark", action="store_true", help="also time large payloads")
    parser.add_argument("--sizes", type=int, nargs="+", default=[1, 4, 16, 64, 256, 512], help="benchmark sizes in MiB")
    parser.add_argument("--repeat", type=int, default=5)
    args = parser.parse_args()
    compiler = args.compiler or next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("A C++20 compiler is required")
    msvc = Path(compiler).name.lower() in ("cl", "cl.exe")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="savegame-durability-", dir=temporary))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    target = (args.dir.resolve() if args.dir else scratch)
    if not target.is_dir():
        raise RuntimeError(f"--dir must be an existing directory: {target}")
    slot = Path(tempfile.mkdtemp(prefix="slot-", dir=target))
    if args.benchmark:
        needed = max(args.sizes) * 3 * 1024 * 1024
        free = shutil.disk_usage(slot).free
        if free < needed + (2 << 30):
            raise RuntimeError(f"benchmark needs {needed >> 20} MiB plus 2 GiB headroom; {free >> 20} MiB free at {slot}")

    includes = [scratch, ROOT]
    executable = scratch / ("SaveDurabilityTest.exe" if os.name == "nt" else "SaveDurabilityTest")
    if msvc:
        compile_command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/MT", "/O2", "/W3",
                           "/D_CRT_SECURE_NO_WARNINGS", "/D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING",
                           *["/I" + str(p) for p in includes],
                           str(SOURCES[6]), str(SOURCES[2]), "/Fo" + str(scratch) + "\\", "/Fe" + str(executable)]
    else:
        # The extracted production block also carries job helpers this harness never calls.
        compile_command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                           "-Wno-unused-function",
                           "-pthread", *[item for p in includes for item in ("-I", str(p))],
                           str(SOURCES[6]), str(SOURCES[2]), "-o", str(executable)]
    commands = [
        [sys.executable, str(SOURCES[5]), str(SOURCES[0]), str(scratch / "CheckpointCommitUnderTest.inc"),
         str(scratch / "CheckpointRecoveryUnderTest.inc"), str(scratch / "CheckpointPendingUnderTest.inc")],
        compile_command,
        [str(executable), str(slot), "matrix"],
    ]
    if args.benchmark:
        commands.append([str(executable), str(slot), "bench", *map(str, args.sizes), "--repeat", str(args.repeat)])

    evidence = {
        "platform": platform.platform(),
        "filesystem": filesystem_of(slot),
        "directory": str(slot),
        "scope": "Production save commit/recovery on real files; process kills, not power loss",
        "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES},
        "commands": [],
        "benchmark": [],
        "passed": False,
    }
    status = 0
    with (scratch / "test.log").open("w", encoding="utf-8") as log:
        for command in commands:
            log.write(json.dumps(command) + "\n")
            output, code, elapsed = run(command, env)
            log.write(output + f"\nexit_code={code}\nelapsed_seconds={elapsed}\n")
            log.flush()
            evidence["commands"].append({"args": command, "exit_code": code, "elapsed_seconds": elapsed})
            if command[-1:] != ["matrix"] and "bench" in command:
                evidence["benchmark"] = [json.loads(line) for line in output.splitlines() if line.startswith("{")]
            else:
                print(output, end="")
            if code:
                print(output, end="")
                status = code
                break
    summary = {}
    for row in evidence["benchmark"]:
        summary.setdefault(row["mib"], []).append(row)
    evidence["benchmark_median_ms"] = {
        str(mib): {key: round(statistics.median(r[key] for r in rows), 3) for key in rows[0] if key.endswith("_ms")}
        for mib, rows in summary.items()}
    for mib, medians in evidence["benchmark_median_ms"].items():
        print(f"{mib:>4} MiB median: " + ", ".join(f"{k[:-3]} {v:.1f} ms" for k, v in medians.items()))
    shutil.rmtree(slot, ignore_errors=True)
    evidence["passed"] = status == 0
    evidence["log_sha256"] = hashlib.sha256((scratch / "test.log").read_bytes()).hexdigest()
    (scratch / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"Save durability evidence ({evidence['filesystem']}): {scratch / 'result.json'}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
