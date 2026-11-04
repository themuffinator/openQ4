#!/usr/bin/env python3
"""Execute the SP/MP collision-owner lifetime methods, including restore ordering."""
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
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/clip-owner-behavior')
    args = parser.parse_args()
    output = task_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    fixture = (ROOT/'tools/tests/native/ClipOwnerBehavior.cpp').read_text()
    signatures = ('void idClipModel::SetOwner(', 'idEntity *idClipModel::GetOwner(')
    implementations = []
    identities = {}
    for tree in ('game', 'mpgame'):
        path = ROOT/'src'/tree/'physics/Clip.cpp'
        source = path.read_text(encoding='utf-8-sig')
        methods = []
        for signature in signatures:
            start = source.index(signature)
            opening = source.index('{', start)
            methods.append(source[start:opening]+'{'+function_body(source, signature, str(path))+'}')
        implementations.append(methods)
        identities[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        # Preserve the existing object-reference slot, not a new wire field.
        assert 'WriteObject( GetOwner() )' in function_body(source, 'void idClipModel::Save(', str(path))
        restore = function_body(source, 'void idClipModel::Restore(', str(path))
        assert 'ReadObject( owner );\n\tSetOwner( owner );' in restore
        assert 'WriteInt( ownerSpawnId' not in source
        assert 'ownerSpawnId = 0;' in function_body(source, 'void idClipModel::Init(', str(path))
        assert 'ownerSpawnId = model->ownerSpawnId;' in function_body(
            source, 'idClipModel::idClipModel( const idClipModel *model )', str(path))
    assert implementations[0] == implementations[1], 'SP/MP collision-owner behavior diverged'
    projection = output/'behavior.cpp'
    projection.write_text(fixture.replace('// @PRODUCTION_METHODS@', '\n\n'.join(implementations[0])))
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
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
