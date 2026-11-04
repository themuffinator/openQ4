#!/usr/bin/env python3
"""Exercise production fire-volume and camera transitions without proprietary assets."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from awakening_gameplay_contract import function_body
from campaign_runtime import ROOT, task_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/awakening-m03-m04/native')
    args = parser.parse_args()
    output = task_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'src/game/awakening/FireFX.cpp'
    text = source.read_text()
    methods = []
    for name in ('Damage', 'UpdateContents', 'ShrinkTargetLights'):
        signature = f'void riFireFX::{name}('
        start = text.index(signature)
        opening = text.index('{', start)
        methods.append(text[start:opening]+'{'+function_body(text, signature, str(source))+'}')
    camera = ROOT/'src/game/awakening/ai/Monster_Turret.cpp'
    camera_text = camera.read_text()
    signature = 'stateResult_t riMonsterTurret::State_Combat('
    start = camera_text.index(signature)
    opening = camera_text.index('{', start)
    methods.append(camera_text[start:opening]+'{'+function_body(camera_text, signature, str(camera))+'}')
    fixture = ROOT/'tools/tests/native/AwakeningChapterBehavior.cpp'
    projection = output/'behavior.cpp'
    projection.write_text(fixture.read_text().replace('// @PRODUCTION_METHODS@', '\n'.join(methods)))
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            str(projection), '-o', str(binary)], cwd=output, env=env,
                           capture_output=True, text=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        print(build.stdout+build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=output, env=env, capture_output=True, text=True)
    (output/'run.log').write_text(run.stdout+run.stderr)
    (output/'result.json').write_text(json.dumps(dict(passed=run.returncode == 0,
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        camera_sha256=hashlib.sha256(camera.read_bytes()).hexdigest(), output=run.stdout+run.stderr), indent=2))
    print(run.stdout+run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
