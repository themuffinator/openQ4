#!/usr/bin/env python3
"""Execute production boost methods and optionally transform installed M07 scripts."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from campaign_runtime import ROOT, task_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--awakening', type=Path, help='User-supplied q4xbase; never copied into the fixture')
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/awakening-m07/behavior')
    args = parser.parse_args()
    output = task_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    boost = ROOT/'src/game/awakening/vehicle/SpeederBike.cpp'
    mission = ROOT/'src/game/awakening/M07MissionScript.inl'
    fixture = ROOT/'tools/tests/native/AwakeningM07Behavior.cpp'
    code = fixture.read_text().replace('// @MISSION@', mission.read_text())
    # Keep the real class and all methods, not a duplicate state machine.
    code = code.replace('// @BOOST@', 'class riVehiclePartBoost' + boost.read_text().split('class riVehiclePartBoost', 1)[1])
    source = output/'behavior.cpp'
    source.write_text(code)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TEMP': str(output), 'TMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], cwd=output, env=env, capture_output=True, text=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        print(build.stdout+build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)]+([str(args.awakening.resolve())] if args.awakening else []),
                         cwd=output, env=env, capture_output=True, text=True)
    (output/'run.log').write_text(run.stdout+run.stderr)
    (output/'report.json').write_text(json.dumps(dict(passed=run.returncode == 0, output=run.stdout+run.stderr,
        sources={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (boost, mission, fixture)}), indent=2)+'\n')
    print(run.stdout+run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
