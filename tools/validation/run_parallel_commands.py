#!/usr/bin/env python3
"""Run independent validation commands concurrently, reporting them in order.

Reads one command per line from standard input (blank lines and lines starting
with # are ignored). A leading "python" runs under this interpreter. Commands
run at once up to --jobs (default: OPENQ4_PYTHON_TEST_JOBS or the CPU count),
each with its own captured output, which is printed in the input order once that
command finishes, so the log reads as a sequential run. Exits non-zero when any
command failed, after listing every failure.

The workflows' script-smoke steps feed their check lists through this. Every
check runs in its own process and writes only to fresh temporary directories,
which is what lets them overlap. The runner creates the repository's .tmp
before starting any of them.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError(f"expected a positive integer, got {value!r}")
    return parsed


def default_jobs() -> int:
    configured = os.environ.get("OPENQ4_PYTHON_TEST_JOBS", "").strip()
    if configured:
        return positive_int(configured)
    return os.cpu_count() or 1


def parse_commands(text: str) -> list[list[str]]:
    commands = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        argv = shlex.split(line)
        if argv[0] == "python":
            argv[0] = sys.executable
        commands.append(argv)
    return commands


def run_captured(argv: list[str]) -> tuple[int, str, float]:
    started = time.monotonic()
    completed = subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return completed.returncode, completed.stdout, time.monotonic() - started


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", "-j", type=positive_int, default=None, help="Commands to run at once.")
    args = parser.parse_args(argv)
    jobs = args.jobs or default_jobs()
    commands = parse_commands(sys.stdin.read())
    # Checks put their temporary directories under .tmp without creating it; run
    # in sequence an earlier check always had, but in parallel any can go first.
    (ROOT / ".tmp").mkdir(exist_ok=True)

    print(f"==> {len(commands)} validation command(s) with {jobs} parallel job(s)", flush=True)
    started = time.monotonic()
    failures: list[str] = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_captured, command) for command in commands]
        for command, future in zip(commands, futures):
            shown = shlex.join(["python", *command[1:]] if command[0] == sys.executable else command)
            returncode, output, seconds = future.result()
            print(f"\n==> {shown}", flush=True)
            if output:
                print(output, end="" if output.endswith("\n") else "\n", flush=True)
            print(f"({seconds:.1f}s, exit {returncode})", flush=True)
            if returncode != 0:
                failures.append(f"{shown} (exit {returncode})")

    print(f"\n==> Finished {len(commands)} command(s) in {time.monotonic() - started:.0f}s", flush=True)
    if failures:
        print(f"{len(failures)} validation command(s) failed:", flush=True)
        for failure in failures:
            print(f"  - {failure}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
