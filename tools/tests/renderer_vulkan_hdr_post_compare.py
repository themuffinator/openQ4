#!/usr/bin/env python3
"""Compare qualified GL/native HDR post captures with identical scene inputs."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

import renderer_pbr_laboratory as lab


def load(path):
    report = json.loads(path.read_text())
    assert report['complete'] and report['runtimeUnchanged'] and not report['limited'], path
    assert report['postProof']['status'] == 'pass' and not report['postProof']['failures'], path
    for name, digest in report['harnessSources'].items():
        assert lab.digest(path.parent / 'harness' / name) == digest, (path, name)
    rows = {}
    for row in report['results']:
        assert not row['failures'] and not row['diagnostics'], row['case']
        for key in ('screenshot', 'log'):
            assert lab.digest(Path(row[key])) == row['sha256'][key], (row['case'], key)
        assert row['case'] not in rows
        rows[row['case']] = row
    assert rows.keys() == report['postProfile'].keys()
    return report, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vk', type=Path, required=True)
    parser.add_argument('--gl', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    vk, native = load(args.vk)
    gl, reference = load(args.gl)
    for key in ('fixture', 'compiledMapSHA256', 'harnessSHA256', 'harnessSources',
                'requestedSamples', 'runtimeSHA256', 'basepath'):
        assert vk[key] == gl[key], key
    assert vk['requestedSamples'] == 0, 'Different API sample locations need separate 4x coverage qualification.'
    paired = {name for name, spec in vk['postProfile'].items() if spec['pair']}
    assert paired == reference.keys()
    checks = {}
    for name in sorted(paired):
        assert vk['postProfile'][name] == gl['postProfile'][name], name
        with Image.open(native[name]['screenshot']) as a, Image.open(reference[name]['screenshot']) as b:
            delta = np.abs(np.asarray(a.convert('RGB'), dtype=np.int16) - np.asarray(b.convert('RGB'), dtype=np.int16))
        checks[name] = dict(maximumByteError=int(delta.max()),
            pixelsAbove2=int(np.count_nonzero(delta.max(axis=2) > 2)), meanByteError=float(delta.mean()))
    cubes = [report['postProof']['checks'].get('cube-captures', {}) for report in (vk, gl)]
    assert all(set(cube) == {'px', 'nx', 'py', 'ny', 'pz', 'nz'} for cube in cubes), 'missing HDR cubemap faces'
    for face in sorted(cubes[0]):
        arrays = []
        for cube in cubes:
            artifact = cube[face]
            path = Path(artifact['path'])
            assert lab.digest(path) == artifact['sha256'], path
            with Image.open(path) as image:
                assert image.size == (256, 256), path
                arrays.append(np.asarray(image.convert('RGB'), dtype=np.int16))
        delta = np.abs(arrays[0] - arrays[1])
        checks['cube/' + face] = dict(maximumByteError=int(delta.max()),
            pixelsAbove2=int(np.count_nonzero(delta.max(axis=2) > 2)), meanByteError=float(delta.mean()))
    passed = all(check['maximumByteError'] <= 2 for check in checks.values())
    proof = dict(status='pass' if passed else 'fail',
        scope='Paired full-frame bloom/exposure and recovery controls at 0x; native failure injection is independently qualified.',
        comparisons=checks, inputs={str(path.resolve()): lab.digest(path) for path in (args.vk, args.gl)},
        harnessSHA256=lab.digest(Path(__file__)))
    args.output.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=proof['status'], pairs=len(checks), maximumByteError=max(c['maximumByteError'] for c in checks.values()))))
    return int(not passed)


if __name__ == '__main__':
    raise SystemExit(main())
