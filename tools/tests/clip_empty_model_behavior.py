#!/usr/bin/env python3
"""Execute the SP/MP trace-list filter that drops clip models with no collision geometry.

A client links an idActor combat model built from modelDefHandle == -1 before
the entity is first presented (prediction frames skip Present). That clip model
has no collision model, trace model or render handle, and handing it to
collisionModelManager->Contents or Rotation is a FatalError. Both game trees must
drop such models in idClip::GetTraceClipModels, whether or not a pass entity is
given, and every query loop must keep skipping the NULL slots it leaves.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from awakening_gameplay_contract import function_body
from campaign_runtime import ROOT, task_path


def definition(source: str, signature: str, context: str) -> str:
    start = source.index(signature)
    opening = source.index('{', start)
    return source[start:opening] + '{' + function_body(source, signature, context) + '}'


def without_comments(text: str) -> str:
    return re.sub(r'\s+', ' ', re.sub(r'//[^\n]*', '', text)).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/clip-empty-model-behavior')
    args = parser.parse_args()
    output = task_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    fixture = (ROOT/'tools/tests/native/ClipEmptyModelBehavior.cpp').read_text()
    implementations = []
    identities = {}
    for tree in ('game', 'mpgame'):
        source_path = ROOT/'src'/tree/'physics/Clip.cpp'
        header_path = ROOT/'src'/tree/'physics/Clip.h'
        source = source_path.read_text(encoding='utf-8-sig')
        header = header_path.read_text(encoding='utf-8-sig')
        methods = [
            definition(header, 'ID_INLINE bool idClipModel::HasCollisionModel(', str(header_path)),
            definition(source, 'int idClip::GetTraceClipModels(', str(source_path)),
        ]
        implementations.append(methods)
        for path in (source_path, header_path):
            identities[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        # Every query loop over the filtered list must skip the slots it clears.
        loads = re.findall(r'touch = clipModelList\[\s*i\s*\];', source)
        guarded = re.findall(r'touch = clipModelList\[\s*i\s*\];\s*if \( !touch \) \{\s*continue;', source)
        assert loads and len(loads) == len(guarded), f'{source_path}: a clip query loop lost its NULL guard'
    assert [without_comments(m) for m in implementations[0]] == \
        [without_comments(m) for m in implementations[1]], 'SP/MP empty clip model filter diverged'
    projection = output/'behavior.cpp'
    projection.write_text(fixture.replace('// @PRODUCTION_METHODS@', '\n\n'.join(implementations[1])))
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(projection), '-o', str(binary)], cwd=output, env=env,
                           text=True, capture_output=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        print(build.stdout+build.stderr, end='')
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=output, env=env, text=True, capture_output=True)
    (output/'result.json').write_text(json.dumps(dict(passed=run.returncode == 0,
        output=run.stdout+run.stderr, sources=identities), indent=2)+'\n')
    print(run.stdout+run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
