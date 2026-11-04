#!/usr/bin/env python3
"""Execute SP/MP medic follower-yield guards without requiring game assets."""
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
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/medic-yield-behavior')
    args = parser.parse_args()
    output = task_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    fixture = (ROOT/'tools/tests/native/MedicYieldBehavior.cpp').read_text()
    implementations = []
    identities = {}
    for tree in ('game', 'mpgame'):
        methods = []
        for file, signatures in (
            ('AI.cpp', ('bool idAI::RequestMoveAside(',)),
            ('AAS_Find.cpp', ('rvAASFindGoalOutOfRange::rvAASFindGoalOutOfRange(',
                              'bool rvAASFindGoalOutOfRange::TestPoint (')),
        ):
            path = ROOT/'src'/tree/'ai'/file
            source = path.read_text(encoding='utf-8-sig')
            for signature in signatures:
                start = source.index(signature)
                opening = source.index('{', start)
                methods.append(source[start:opening]+'{'+function_body(source, signature, str(path))+'}')
            identities[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        implementations.append(methods)
    assert implementations[0] == implementations[1], 'SP/MP follower yield behavior diverged'
    projection = output/'behavior.cpp'
    projection.write_text(fixture.replace('// @PRODUCTION_METHODS@', '\n\n'.join(implementations[0])))
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            str(projection), '-o', str(binary)], cwd=output, env=env,
                           text=True, capture_output=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=output, env=env, text=True, capture_output=True)
    (output/'result.json').write_text(json.dumps(dict(passed=run.returncode == 0,
        output=run.stdout+run.stderr, sources=identities), indent=2)+'\n')
    print(run.stdout+run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
