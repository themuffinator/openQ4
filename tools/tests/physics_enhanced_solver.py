#!/usr/bin/env python3
"""Execute the enhanced physics kernels and telemetry natively (PHYS-02/03/04).

Compiles tools/tests/native/PhysicsEnhancedSolverTest.cpp against the
production headers src/game/physics/EnhancedSolver.h and PhysicsTelemetry.h,
which both game trees include, and runs it: boxed PGS accuracy and warm
starts, a contact solver that never adds kinetic energy, dropped and stacked
boxes that settle and sleep, a small fast body that cannot tunnel through a
thin wall, the impulse guard's momentum bookkeeping and the telemetry
percentiles and report format.
"""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    target = ROOT / '.tmp/physics/solver-tests'
    target.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CXX', 'clang++')
    exe = target / ('PhysicsEnhancedSolverTest' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', str(ROOT),
                    str(ROOT / 'tools/tests/native/PhysicsEnhancedSolverTest.cpp'), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return subprocess.run([str(exe)], cwd=ROOT).returncode


if __name__ == '__main__':
    sys.exit(main())
