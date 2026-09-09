#!/usr/bin/env python3
"""Qualify final SDR color mapping using matched engine TGA screenshots.

Choose stable, uniform authored UI interiors, not text, antialiasing, gradients,
or animated scene pixels. Regions use decoded top-left pixel coordinates and
must include distinguishable samples in both image halves. Their vertically
reflected neutral neighborhoods must also be uniform. No whole-frame averaging
or image registration hides spatial errors. This checks screenshot pixels only;
capture provenance, renderer selection, ordinary present and HDR require separate
evidence. The captured alpha bytes are checked, not pre-readback blend alpha.

For the settled presentation-alias-smoke fixture at 1280x720, density 1.25 and
UI scale 1, candidate interiors after panel::rect = 32 32 432 200 are:
  --region panel:52,152,8,8 --region background:52,560,8,8
The oracle verifies their uniformity instead of assuming a fixture was rendered.

Example:
  python tools/tests/vk_display_color_mapping_capture.py neutral.tga corrected.tga \
    --brightness 1.25 --gamma 1.6 --region panel:52,152,8,8 \
    --region background:52,560,8,8
  python tools/tests/vk_display_color_mapping_capture.py --self-test

Only Python's standard library is required. The bounded reader accepts the
uncompressed 24/32-bit true-color TGA format written by the engine, including
top/bottom and left/right storage origins; it does not reinterpret gamma tags.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct('<BBBHHBHHHHBB')
MAX_PIXELS = 16 * 1024 * 1024


@dataclass(frozen=True)
class Region:
    name: str
    x: int
    y: int
    width: int
    height: int

    @classmethod
    def parse(cls, value: str):
        try:
            name, rectangle = value.split(':', 1)
            x, y, width, height = map(int, rectangle.split(','))
            if not name or len(name) > 80:
                raise ValueError('region name must contain 1 to 80 characters')
            return cls(name, x, y, width, height)
        except ValueError as error:
            raise argparse.ArgumentTypeError('expected NAME:X,Y,WIDTH,HEIGHT') from error


@dataclass
class Tga:
    width: int
    height: int
    components: int
    descriptor: int
    pixels: bytes
    sha256: str

    @classmethod
    def read(cls, path: Path):
        with path.open('rb') as stream:
            header = stream.read(HEADER.size)
            if len(header) != HEADER.size:
                raise ValueError(f'{path}: truncated TGA header')
            (id_length, color_map, image_type, map_first, map_length, map_depth,
             _x, _y, width, height, depth, descriptor) = HEADER.unpack(header)
            if (color_map or image_type != 2 or map_first or map_length or map_depth or
                    depth not in (24, 32) or descriptor & 0xc0 or
                    descriptor & 0x0f not in ((0,) if depth == 24 else (0, 8))):
                raise ValueError(f'{path}: expected uncompressed 24/32-bit true-color TGA')
            if not width or not height or width * height > MAX_PIXELS:
                raise ValueError(f'{path}: invalid or oversized TGA dimensions')
            identifier = stream.read(id_length)
            size = width * height * (depth // 8)
            pixels = stream.read(size)
            if len(identifier) != id_length or len(pixels) != size:
                raise ValueError(f'{path}: truncated TGA pixels')
            digest = hashlib.sha256(header + identifier + pixels)
            # Include an optional TGA footer/extension in provenance, bounded.
            trailer = stream.read(1024 * 1024 + 1)
            if len(trailer) > 1024 * 1024:
                raise ValueError(f'{path}: oversized TGA trailer')
            digest.update(trailer)
            return cls(width, height, depth // 8, descriptor, pixels, digest.hexdigest())

    def pixel(self, x: int, y: int):
        row = y if self.descriptor & 0x20 else self.height - 1 - y
        column = self.width - 1 - x if self.descriptor & 0x10 else x
        offset = (row * self.width + column) * self.components
        b, g, r = self.pixels[offset:offset + 3]
        return r, g, b, self.pixels[offset + 3] if self.components == 4 else 255


def mapped(rgb, brightness: float, gamma: float):
    return tuple(255 * min(1., max(0., channel / 255 * brightness)) ** (1 / gamma)
                 for channel in rgb[:3])


def separation(a, b):
    return max(abs(x - y) for x, y in zip(a[:3], b[:3]))


def spread(pixels):
    return max(max(pixel[channel] for pixel in pixels) - min(pixel[channel] for pixel in pixels)
               for channel in range(3))


def verify(neutral: Path, corrected: Path, brightness: float, gamma: float,
           regions: list[Region], tolerance: float = 2., uniformity: int = 2) -> dict:
    result = {'passed': False, 'errors': [], 'regions': [],
              'neutral': str(neutral), 'corrected': str(corrected)}
    errors = result['errors']
    try:
        if (not all(math.isfinite(v) for v in (brightness, gamma, tolerance)) or
                not 0 <= tolerance <= 4 or not 0 <= uniformity <= 4):
            raise ValueError('settings must be finite; tolerance and uniformity must be between 0 and 4')
        brightness = min(16., max(0., brightness))
        gamma = max(.001, gamma)
        if not 2 <= len(regions) <= 64 or len({r.name for r in regions}) != len(regions):
            raise ValueError('provide 2 to 64 regions with unique names')
        if sum(r.width * r.height for r in regions) > 262144:
            raise ValueError('sample regions exceed the 262144-pixel limit')
        first, second = Tga.read(neutral), Tga.read(corrected)
        if (first.width, first.height, first.components) != (second.width, second.height, second.components):
            raise ValueError('capture dimensions or pixel depths differ')
        result.update(width=first.width, height=first.height,
                      neutral_sha256=first.sha256, corrected_sha256=second.sha256,
                      brightness=brightness, gamma=gamma, tolerance=tolerance, uniformity=uniformity)
        identity_halves = set()
        omitted_signal = repeated_signal = 0.
        distinguish = max(8., 2 * tolerance + 2)
        for region in regions:
            x, y, w, h = region.x, region.y, region.width, region.height
            if (x < 0 or y < 0 or w < 2 or h < 2 or w * h < 16 or
                    x + w > first.width or y + h > first.height):
                raise ValueError(f'{region.name}: region must contain at least 16 interior pixels inside the image')
            reference, observed, reflected = [], [], []
            for row in range(y, y + h):
                for column in range(x, x + w):
                    reference.append(first.pixel(column, row))
                    observed.append(second.pixel(column, row))
                    reflected.append(first.pixel(column, first.height - 1 - row))
            expected = [mapped(pixel, brightness, gamma) for pixel in reference]
            alternative = [mapped(pixel, brightness, gamma) for pixel in reflected]
            max_error = max(separation(a, b) for a, b in zip(observed, expected))
            flipped_error = max(separation(a, b) for a, b in zip(observed, alternative))
            vertical_gap = min(separation(a, b) for a, b in zip(expected, alternative))
            alpha_error = max(abs(a[3] - b[3]) for a, b in zip(reference, observed))
            source_spread, reflected_spread = spread(reference), spread(reflected)
            report = {'name': region.name, 'rect': [x, y, w, h], 'pixels': w * h,
                      'neutral_rgb': list(reference[0][:3]), 'corrected_rgb': list(observed[0][:3]),
                      'expected_rgb': list(expected[0]), 'neutral_spread': source_spread,
                      'reflected_neutral_spread': reflected_spread,
                      'max_rgb_error': max_error, 'max_alpha_error': alpha_error,
                      'vertical_alternative_error': flipped_error,
                      'minimum_vertical_separation': vertical_gap}
            result['regions'].append(report)
            if source_spread > uniformity or reflected_spread > uniformity:
                errors.append(f'{region.name}: neutral or vertically reflected neighborhood is not uniform')
            if max_error > tolerance:
                errors.append(f'{region.name}: RGB error {max_error:.3f} exceeds {tolerance:g}')
            if alpha_error:
                errors.append(f'{region.name}: captured alpha changed')
            if vertical_gap >= distinguish and source_spread <= uniformity and reflected_spread <= uniformity:
                if y + h <= first.height / 2:
                    identity_halves.add('top')
                elif y >= first.height / 2:
                    identity_halves.add('bottom')
            omitted_signal = max(omitted_signal, max(separation(a, b) for a, b in zip(expected, reference)))
            repeated_signal = max(repeated_signal, max(
                separation(value, mapped(tuple(round(c) for c in value), brightness, gamma)) for value in expected))
        result.update(spatial_identity_halves=sorted(identity_halves),
                      omitted_pass_separation=omitted_signal, repeated_pass_separation=repeated_signal)
        if identity_halves != {'top', 'bottom'}:
            errors.append('uniform samples in both image halves must distinguish their vertical reflections')
        if min(omitted_signal, repeated_signal) < distinguish:
            errors.append('settings/regions cannot distinguish an omitted or repeated color pass')
        result['passed'] = not errors
    except (OSError, ValueError, OverflowError) as error:
        errors.append(str(error))
    return result


def self_test():
    """Synthetic TGA fixtures exercise the oracle, not a renderer implementation."""
    width, height = 48, 40
    regions = [Region('top', 6, 6, 4, 4), Region('bottom', 6, 30, 4, 4)]
    source = [[(64, 77, 89, 255) for _ in range(width)] for _ in range(height)]
    for y in range(4, 14):
        for x in range(4, 20):
            source[y][x] = (31, 37, 30, 255)

    def transform(pixels):
        # Independent rounded fixture construction, not the oracle's mapped().
        return [[tuple(round(255 * math.pow(min(1., c / 255 * 1.25), .625))
                       for c in pixel[:3]) + (pixel[3],) for pixel in row] for row in pixels]

    def write(path, pixels, descriptor=0x28, components=4):
        output = bytearray(HEADER.pack(0, 0, 2, 0, 0, 0, 0, 0,
                                      len(pixels[0]), len(pixels), components * 8, descriptor))
        rows = pixels if descriptor & 0x20 else pixels[::-1]
        for row in rows:
            for r, g, b, a in (row[::-1] if descriptor & 0x10 else row):
                output.extend((b, g, r, a) if components == 4 else (b, g, r))
        path.write_bytes(output)

    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vk-color-oracle-', dir=ROOT / '.tmp') as temporary:
        neutral, corrected = Path(temporary) / 'neutral.tga', Path(temporary) / 'corrected.tga'
        target = transform(source)

        def check(expected, samples=regions, **settings):
            result = verify(neutral, corrected, 1.25, 1.6, samples, **settings)
            assert result['passed'] == expected, result
            return result

        # Both storage origins describe the same visible top/bottom positions.
        for descriptor in (0x08, 0x18, 0x28, 0x38):
            write(neutral, source, descriptor)
            write(corrected, target, descriptor ^ 0x30)
            check(True)
        write(neutral, source, 0x20, 3)
        write(corrected, target, 0x00, 3)
        check(True)
        write(neutral, source)
        write(corrected, target[::-1])
        result = check(False)
        assert all(r['vertical_alternative_error'] < 1 for r in result['regions'])
        write(corrected, transform(target))
        check(False)  # Double mapping.
        write(corrected, source)
        check(False)  # Mapping omitted.
        write(corrected, target)
        check(False, [Region('top', 6, 6, 4, 4), Region('bottom', 48, 30, 4, 4)])
        check(False, [Region('top', 6, 6, 4, 4), Region('other', 8, 6, 4, 4)])
        check(False, [Region('top', 26, 6, 4, 4), Region('bottom', 26, 30, 4, 4)])
        changed = [row[:] for row in target]
        changed[6][6] = changed[6][6][:3] + (0,)
        write(corrected, changed)
        check(False)  # Alpha changed.
        changed[6][6] = (target[6][6][0] + 3,) + target[6][6][1:]
        write(corrected, changed)
        check(False)  # A single pixel cannot hide in an average.
        changed[6][6] = (target[6][6][0] + 1,) + target[6][6][1:]
        write(corrected, changed)
        check(True)  # Output quantization tolerance.
        changed = [row[:] for row in source]
        changed[6][6] = (55, 37, 30, 255)
        write(neutral, changed)
        write(corrected, transform(changed))
        check(False)  # Formula-correct scene pixels do not qualify as uniform UI.
        write(neutral, source)
        write(corrected, target[:-1])
        check(False)  # Dimension mismatch.
        write(corrected, target)
        original = corrected.read_bytes()
        for malformed in (b'', original[:17], original[:-1], original[:2] + b'\x0a' + original[3:],
                          original[:16] + b'\x10' + original[17:]):
            corrected.write_bytes(malformed)
            check(False)
        write(corrected, target)
        assert not verify(neutral, corrected, float('nan'), 1.6, regions)['passed']
        assert not verify(neutral, neutral, 1., 1., regions)['passed']
    print('vk_display_color_mapping_capture: synthetic oracle checks passed; no engine capture was validated')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('neutral', type=Path, nargs='?')
    parser.add_argument('corrected', type=Path, nargs='?')
    parser.add_argument('--brightness', type=float)
    parser.add_argument('--gamma', type=float)
    parser.add_argument('--region', type=Region.parse, action='append', default=[])
    parser.add_argument('--tolerance', type=float, default=2.)
    parser.add_argument('--uniformity', type=int, default=2)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.neutral is None or args.corrected is None or args.brightness is None or args.gamma is None:
        parser.error('neutral/corrected paths and --brightness/--gamma are required')
    result = verify(args.neutral, args.corrected, args.brightness, args.gamma,
                    args.region, args.tolerance, args.uniformity)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
