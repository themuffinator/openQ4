#!/usr/bin/env python3
"""Execute the enhanced physics kernels and telemetry natively (PHYS-02/03/04).

Compiles tools/tests/native/PhysicsEnhancedSolverTest.cpp against the
production headers src/game/physics/EnhancedSolver.h and PhysicsTelemetry.h,
which both game trees include, and runs it: boxed PGS accuracy and warm
starts, a contact solver that never adds kinetic energy, dropped and stacked
boxes that settle and sleep, a small fast body that cannot tunnel through a
thin wall, the impulse guard's momentum bookkeeping and the telemetry
percentiles and report format.

It also holds one source contract both physics modes depend on: idlib's
_FAST_MATH stays undefined on every platform. The Linux and macOS ports used
to define it, which made idMath::Sqrt and InvSqrt table approximations there
and exact sqrtf on Windows. The last-bit difference changed which frame a
ragdoll's vertical thigh bone got at load (idAF::LoadBody takes a fallback
frame only when two joints share x and y exactly), so the knee limits of every
map-placed ragdoll pose pointed sideways on Linux and the figures blew apart
at up to 1,600 units/s in either physics mode.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def exact_math_contract() -> None:
    header = (ROOT / 'src/idlib/math/Math.h').read_text(encoding='latin-1')
    defines = [line.strip() for line in header.splitlines() if re.match(r'\s*#\s*define\s+_FAST_MATH\b', line)]
    if defines:
        raise SystemExit('src/idlib/math/Math.h defines _FAST_MATH (%s): idMath::Sqrt and InvSqrt must be the exact '
                         'libm calls on every platform, or float results differ from Windows in the last bit and '
                         'map-placed ragdolls blow apart (see this test\'s docstring)' % defines[0])
    if '#ifdef _FAST_MATH' not in header or 'return( sqrtf( x ) )' not in header:
        raise SystemExit('src/idlib/math/Math.h no longer has the exact idMath::Sqrt this contract checks; update the test')


def main() -> int:
    exact_math_contract()
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
