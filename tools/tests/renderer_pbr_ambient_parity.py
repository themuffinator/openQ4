#!/usr/bin/env python3
"""Validate ambient radiance independently, with an optional admitted GL pair."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageFilter

import pbr_reference as reference
import renderer_vulkan_pbr_ambient as capture


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def linear(value):
    value /= 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


# The shared paired allowance (renderer_pbr_native_gl_parity.py): at most
# 0.05% of the frame may differ beyond the encoded allowance as a one-pixel
# edge shift. View-dependent ambient specular makes grazing silhouette pixels
# steep, so their last bit of interpolation can move a few bytes.
EDGE_SHIFT_LIMIT = 0.0005

# REN-05 (23 November 2026). Mesa's software Vulkan driver, lavapipe, decodes
# an 8-bit texel k as float(k) * float(1 / 255), one float step above the
# correctly rounded k / 255 for the laboratory's metallic (102) and AO (192)
# bytes. The scalar material and its packed and separate texture forms are
# therefore different float inputs there, and a pixel whose shaded value sits
# on a rounding boundary can land one byte apart: one pixel of 1,024,000 on
# 5 October, none at d30280aa2 (three runs), and one again (blue 37 against
# 36 at 689,434) when the scalar form is given the exactly rounded quotients.
# Giving it the reciprocal products instead makes the forms identical. Allow
# at most this many one-byte pixels between those forms on lavapipe only;
# every other device, and every other exact pair on lavapipe, stays exact.
LAVAPIPE_LAYOUT_PIXELS = 4
LAVAPIPE_LAYOUT_PAIRS = {('scalar', 'packed'), ('scalar', 'separate')}


def lavapipe(report):
    """Whether every engine log of a Vulkan run names lavapipe as its driver.

    The device comes from the run's own logs (their digests are verified
    before this is asked), not from an option an operator could forget.
    """
    logs = {row['log'] for row in report['results'] if row.get('backend') == 'vk' and row.get('log')}
    return bool(logs) and all(
        re.search(r"^Vulkan: driver 'llvmpipe' .*\(driverID 13,", Path(log).read_text(encoding='utf-8', errors='replace'), re.M)
        for log in logs)


def neighbourhood(image):
    """Per-channel minimum and maximum of each pixel's 3x3 neighbourhood."""
    padded = np.pad(image, ((1, 1), (1, 1), (0, 0)), mode='edge')
    height, width = image.shape[:2]
    shifted = np.stack([padded[y:y + height, x:x + width] for y in range(3) for x in range(3)])
    return shifted.min(axis=0), shifted.max(axis=0)


def edge_shifts(a, b, error):
    """Pixels beyond the allowance that are one-pixel edge shifts: each value
    lies within two bytes of the other capture's 3x3 range, or the pixel sits
    on a real edge (contrast of at least 24) and differs by at most half of
    it, exactly as the native parity tool judges them."""
    low_a, high_a = neighbourhood(a)
    low_b, high_b = neighbourhood(b)
    within = (np.all((a >= low_b - 2) & (a <= high_b + 2), axis=2)
              & np.all((b >= low_a - 2) & (b <= high_a + 2), axis=2))
    contrast = np.maximum((high_a - low_a).max(axis=2), (high_b - low_b).max(axis=2))
    return (error > 2) & (within | ((contrast >= 24) & (error * 2 <= contrast)))


def compare(paths):
    reports = [json.loads(path.read_text()) for path in paths]
    paired = len(paths) == 2
    backends = ('gl', 'vk') if paired else ('vk',)
    failures, checks, captures = [], {}, []
    for key in ('fixture', 'compiledMapSHA256', 'harnessSHA256', 'ambientHarnessSHA256',
                'requestedSamples', 'basepath'):
        if paired and reports[0][key] != reports[1][key]:
            raise ValueError('incompatible paired provenance: ' + key)
    # An unchanged GL library may qualify both sides of a native revision.
    # Retain that revision difference explicitly; no other runtime input may
    # change, including the client, GL renderer and game modules.
    runtimes = [report['runtimeSHA256'] for report in reports]
    if paired and runtimes[0].keys() != runtimes[1].keys():
        raise ValueError('incompatible runtime file sets')
    revisions = {name: [runtime[name] for runtime in runtimes] for name in runtimes[0]
                 if runtimes[0][name] != runtimes[1][name]} if paired else {}
    if any(not Path(name).name.startswith('renderer-vk') for name in revisions):
        raise ValueError('runtime inputs changed outside the native renderer')
    samples = reports[0]['requestedSamples']
    if samples not in (0, 4):
        raise ValueError('unknown sample count')
    expected_profile = capture.configure(reports[0]['fixture'], samples)
    for backend, report in zip(backends, reports):
        if not report['complete'] or not report['runtimeUnchanged']:
            raise ValueError('incomplete or changed runtime: ' + backend)
        expected = dict(expected_profile)
        if backend == 'gl':
            expected.pop('ambient-alpha-fault')
        if report['ambientProfile'] != expected:
            raise ValueError('capture does not contain the required controls: ' + backend)
        images = {}
        for row in report['results']:
            name = row['case'].removeprefix('ambient-')
            if row['backend'] != backend or row['failures']:
                raise ValueError(f'{backend}/{name}: unqualified capture {row["failures"]}')
            for kind in ('screenshot', 'log'):
                if digest(row[kind]) != row['sha256'][kind]:
                    raise ValueError(f'{backend}/{name}: changed {kind}')
            telemetry = '\n'.join(row['telemetry'])
            if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b', telemetry):
                raise ValueError(f'{backend}/{name}: missing actual MSAA proof')
            images[name] = np.asarray(Image.open(row['screenshot']).convert('RGB'), dtype=np.int16)
            if images[name].shape != (800, 1280, 3):
                raise ValueError(f'{backend}/{name}: invalid extent')
            if backend == 'vk' and name.startswith('alpha-') and name != 'alpha-classic':
                spec = expected['ambient-' + name]
                count = 2 if spec['double'] else spec['lights']
                state = dict(re.findall(r'(\w+)=([^ ]+)', next(
                    (s for s in row['telemetry'] if s.startswith('Vulkan: native PBR transparency:')), '')))
                wanted = {'admitted': '0' if spec['fault'] else '1',
                          'reason': 'direct-geometry' if spec['fault'] else 'ready',
                          'requiredAtLeast': str(count), 'ready': '0' if spec['fault'] else '1',
                          'recorded': '0' if spec['fault'] else str(count),
                          'surfaces': '0' if spec['fault'] else '1',
                          'composites': str(0 if spec['fault'] or spec['debug'] else count)}
                if any(state.get(key) != value for key, value in wanted.items()):
                    failures.append(f'{backend}/{name}: incorrect complete ambient ownership')
                checks[backend + '/' + name + '/ownership'] = {'expected': wanted, 'actual': state}
        if len(report['results']) != len(expected) or set(images) != {n.removeprefix('ambient-') for n in expected}:
            raise ValueError('missing or duplicate controls: ' + backend)
        captures.append(images)

    # Use a fully covered interior separately from the raw full-frame result:
    # GL and Vulkan use reflected 4x sample positions on the qualified GPU.
    def interior(image):
        mask = (image[:, :, 0] == 0) & (image[:, :, 1] == 255) & (image[:, :, 2] == 0)
        return np.asarray(Image.fromarray(mask.astype(np.uint8) * 255).filter(ImageFilter.MinFilter(7))) == 255

    nov, variance = reference.sampling_sphere()
    nov = np.nan_to_num(nov, nan=-1.0)
    variance = np.nan_to_num(variance)
    for backend, report, images in zip(backends, reports, captures):
        mask = interior(images['owned-dark'])
        if np.count_nonzero(mask) < 10000:
            raise ValueError(backend + ': missing complete PBR coverage')

        # Every additive draw into an 8-bit framebuffer rounds on its own (the
        # classic OpenGL owner's frame here; Vulkan keeps a float scene
        # target), so each draw after the first may add half a byte.
        def values(name, target, area=mask, draws=1):
            target = np.asarray(target)
            actual = images[name][area]
            if target.ndim == 3:
                target = target[area]
            error = float(np.abs(actual - target).max())
            limit = 1.1 + 0.5 * (draws - 1)
            checks[backend + '/' + name + '/oracle'] = {'pixels': len(actual), 'maximumByteError': error,
                                                        'draws': draws, 'limit': limit}
            if error > limit:
                failures.append(f'{backend}/{name}: independent radiance/composition mismatch ({error:.3f})')

        # The classic light term of the white fixture: what a white classic
        # surface would show. PBR receives pi times its decoded value as
        # irradiance (PBRClassicLightIrradiance), so the uniform environment
        # an ambient light stands for has radiance decode(term).
        light = reference.decode(np.asarray((1, 0.5, 0.25)))

        # An authored ambient light is a uniform environment: Fresnel-weighted
        # diffuse with multi-bounce AO plus the split-sum specular, view
        # dependent, evaluated per pixel on the specimen sphere where N.V is
        # at least 0.6 (grazing pixels follow the tessellated mesh's
        # interpolated normals, not the analytic sphere). Every light stage is its own draw into the display-referred
        # framebuffer, encoded on its own exactly like a classic interaction.
        flat = mask & (nov >= 0.6)

        def ambient(rgb=(188, 188, 188), metallic=0.0, rough=0.5, ao=1.0, stages=1):
            radiance = reference.uniform_environment(light, reference.byte_decode(rgb), metallic,
                                                     reference.filtered_roughness(rough, variance), ao, nov)
            # The display-referred framebuffer saturates at white.
            return np.minimum(reference.encode(radiance) * stages * 255, 255)

        data = dict(metallic=102 / 255, rough=128 / 255, ao=192 / 255)
        for name, stages in (('scalar', 1), ('packed', 1), ('separate', 1),
                             ('two', 2), ('two-stages', 2), ('restored', 1)):
            values(name, ambient(stages=stages, **data), flat, draws=stages)
        values('ao-zero', (0, 0, 0))
        values('dark', (0, 0, 0))
        for kind in ('dielectric', 'metal'):
            for index, rough in ((0, 0.045), (5, 1.0)):
                values(f'{kind}-{index}', ambient((245, 160, 105) if kind == 'metal' else (188, 188, 188),
                                                   metallic=float(kind == 'metal'), rough=rough), flat)
        values('normal-zero', ambient(), flat)
        cutout_mask = interior(images['cutout-owned'])
        cutout_pixels = int(np.count_nonzero(cutout_mask & flat))
        checks[backend + '/cutout/coverage'] = {'interiorPixels': cutout_pixels}
        if cutout_pixels < 1000:
            failures.append(backend + ': insufficient cutout coverage')
        else:
            values('cutout', ambient(rough=0.25), cutout_mask & flat)
        for name in ('owned', 'owned-two'):
            values(name, (0, 255, 0))
        for name in ('emission-dark', 'emission', 'emission-two'):
            values(name, [min(255, 255 * float(reference.encode(linear(c) * 4))) for c in (30, 200, 255)])
        alpha = 112 / 255
        # The ordered composite: the coverage keeps 1 - alpha of the
        # background, then each light stage adds alpha times its encoded
        # radiance.
        translucent = ambient((50, 180, 255), rough=0.2)
        for suffix, stages in (('dark', 0), ('one', 1), ('two', 2), ('stages', 2)):
            target = images['background-' + suffix] * (1 - alpha) + translucent * alpha * stages
            values('alpha-' + suffix, target, flat, draws=1 + stages)
        values('alpha-owned', images['background-one'] * (1 - alpha) + np.asarray((0, 112, 0)))
        # Equal materials and restorations render identically; the normal
        # encodings carry the same map (tangent RG reconstructs Z: one byte).
        exact_pairs = [('scalar', name) for name in ('packed', 'separate', 'restored')]
        exact_pairs += [('normal-xyz', 'normal-agb')]
        exact_pairs += [('alpha-one', 'alpha-' + name) for name in
                        ('restored', 'image-reload', 'partial-restart', 'full-restart')]
        exact_pairs += [('two', 'two-stages')]
        if backend == 'vk':
            exact_pairs += [('alpha-classic', 'alpha-fault')]
        near_pairs = [('normal-xyz', 'normal-rg')]
        for a, b in near_pairs:
            error = int(np.abs(images[a] - images[b]).max())
            checks[f'{backend}/{a}/{b}'] = {'maximumByteError': error, 'limit': 1}
            if error > 1:
                failures.append(f'{backend}/{a}/{b}: normal encodings disagree ({error})')
        bounded = backend == 'vk' and lavapipe(report)
        for a, b in exact_pairs:
            delta = np.abs(images[a] - images[b]).max(axis=2)
            error = int(delta.max())
            checks[f'{backend}/{a}/{b}'] = {'maximumByteError': error}
            if bounded and (a, b) in LAVAPIPE_LAYOUT_PAIRS:
                pixels = int(np.count_nonzero(delta))
                checks[f'{backend}/{a}/{b}'].update(pixels=pixels, lavapipeLimitPixels=LAVAPIPE_LAYOUT_PIXELS)
                if error > 1 or pixels > LAVAPIPE_LAYOUT_PIXELS:
                    failures.append(f'{backend}/{a}/{b}: material layouts differ beyond the lavapipe '
                                    f'texel-decode allowance ({pixels} pixels, {error} bytes)')
            elif error:
                failures.append(f'{backend}/{a}/{b}: material, restoration or rollback changed ({error})')

    parity = {}
    if paired:
        mask = interior(captures[0]['owned-dark']) & interior(captures[1]['owned-dark'])
        for name, a in captures[0].items():
            b = captures[1][name]
            delta = np.abs(a - b)
            error = int(delta.max())
            interior_error = int(delta[mask].max())
            # Alpha-test boundaries also vary with API coverage. Its absolute
            # radiance is checked above on each backend's actual retained samples.
            if name.startswith('cutout'):
                selected = interior(captures[0]['cutout-owned']) & interior(captures[1]['cutout-owned'])
                interior_error = int(delta[selected].max())
            pixel_error = delta.max(axis=2)
            shifted = edge_shifts(a, b, pixel_error)
            judged_error = int(pixel_error[~shifted].max())
            shifted_fraction = float(np.count_nonzero(shifted)) / pixel_error.size
            parity[name] = {'maximumByteError': error, 'pixelsAbove2': int(np.count_nonzero(pixel_error > 2)),
                            'interiorMaximumByteError': interior_error, 'edgeShiftedPixels': int(np.count_nonzero(shifted)),
                            'edgeShiftedFraction': shifted_fraction, 'judgedMaximumByteError': judged_error}
            whole_frame = judged_error <= 2 and shifted_fraction <= EDGE_SHIFT_LIMIT
            if interior_error > 2 or (samples == 0 and not whole_frame and not name.startswith('cutout')):
                failures.append(f'{name}: GL/native paired rendering mismatch ({error}, interior {interior_error}, '
                                f'{int(np.count_nonzero(shifted))} edge shifts)')
    return {'status': 'fail' if failures else 'pass', 'samples': samples,
            'fullFrameStatus': ('pass' if all(v['judgedMaximumByteError'] <= 2 and v['edgeShiftedFraction'] <= EDGE_SHIFT_LIMIT
                                              for v in parity.values()) else 'fail') if paired else 'not-run',
            'scope': 'Absolute ambient lighting and alpha composition, material invariants, native rollback and lifecycle. GL pairing requires an admitted reference; its absence is not a parity pass.',
            'inputs': {str(p.resolve()): digest(p) for p in paths}, 'harnessSHA256': digest(__file__),
            'nativeRevisionDifferences': revisions,
            'lavapipeLayoutAllowance': {'active': lavapipe(reports[-1]), 'pixels': LAVAPIPE_LAYOUT_PIXELS,
                                        'pairs': sorted('/'.join(pair) for pair in LAVAPIPE_LAYOUT_PAIRS)},
            'checks': checks, 'parity': parity, 'failures': failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gl', type=Path, help='optional reference; blocked or fallback captures are rejected')
    parser.add_argument('--vk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    proof = compare((args.gl, args.vk) if args.gl else (args.vk,))
    args.output.write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({k: proof[k] for k in ('status', 'fullFrameStatus', 'failures')}, indent=2))
    return int(proof['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
