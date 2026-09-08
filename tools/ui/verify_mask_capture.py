#!/usr/bin/env python3
"""Verify mask-smoke.q4ui against an independent area-integrated mask.

Reads an engine TGA. Compare the masked panel with its matching reference,
including nested control masks, group opacity, glyphs, and transparent holes.
The oracle integrates horizontal intervals across each pixel at 128 y samples;
it solves the authored cubic hole directly and does not use the runtime tessellator.
Requires Pillow and NumPy.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def coverage(width: int, height: int, density: float) -> np.ndarray:
    result = np.empty((height, width), dtype=np.float64)
    left = np.arange(width, dtype=np.float64) / density
    right = left + 1/density
    k = .5522847498307936
    for row in range(height):
        y = (row + (np.arange(128)+.5)/128)/density
        low, high = np.maximum(0, 64-y), np.minimum(408, 590-y)
        high = np.where((y >= 0) & (y < 246), high, low)
        # In the upper/lower right quadrant, y(t) rises monotonically from
        # zero to ry while x(t) falls from rx to zero. Bisect y for x.
        normalized_y = np.abs((y-112)/38)
        a, b = np.zeros(128), np.ones(128)
        for _ in range(36):
            t = (a+b)/2
            curve_y = 3*(1-t)**2*t*k + 3*(1-t)*t*t + t**3
            a = np.where(curve_y < normalized_y, t, a)
            b = np.where(curve_y >= normalized_y, t, b)
        t = (a+b)/2
        hole_radius = 66*((1-t)**3+3*(1-t)**2*t+3*(1-t)*t*t*k)
        hole_radius = np.where(normalized_y < 1, hole_radius, 0)
        span = np.maximum(0, np.minimum(right[:, None], high)-np.maximum(left[:, None], low))
        hole = np.maximum(0, np.minimum(np.minimum(right[:, None], high), 204+hole_radius)
                          - np.maximum(np.maximum(left[:, None], low), 204-hole_radius))
        result[row] = np.mean(span-hole, axis=1)*density
    return result


def verify(path: Path, density: float) -> dict:
    if not np.isfinite(density) or density <= 0 or any(abs(value*density-round(value*density)) > 1e-6 for value in (24, 456, 32, 408)):
        raise ValueError('density must keep the reference and masked panels on matching pixel phases')
    image = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64)
    background = image[4, 4]
    first, second = round(24*density), round(456*density)
    top, width, height = round(32*density), round(408*density), round(246*density)
    if second+width > image.shape[1] or top+height > image.shape[0]:
        raise ValueError('capture does not contain both complete panels')
    reference = image[top:top+height, first:first+width]
    masked = image[top:top+height, second:second+width]
    area = coverage(width, height, density)
    alpha = area*(1-.75*(np.arange(width)+.5)/(408*density))
    expected = reference*alpha[:, :, None] + background*(1-alpha[:, :, None])
    errors = np.max(np.abs(masked-expected), axis=2)
    changed = int(np.count_nonzero(np.max(np.abs(reference-background), axis=2) > 8))
    hidden = area < 1e-9
    result = {'path': str(path), 'density': density, 'pixels': int(errors.size),
              'non_background_pixels': changed, 'background': background.tolist(),
              'max_channel_error': float(np.max(errors)), 'p99_channel_error': float(np.quantile(errors, .99)),
              'pixels_over_5': int(np.count_nonzero(errors > 5)),
              'hidden_pixels': int(np.count_nonzero(hidden)),
              'leaked_hidden_pixels': int(np.count_nonzero(np.max(np.abs(masked-background), axis=2)[hidden] > 1))}
    result['passed'] = changed > 1000 and result['pixels_over_5'] == 0 and result['leaked_hidden_pixels'] == 0
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('screenshot', type=Path)
    parser.add_argument('--density', type=float, required=True)
    args = parser.parse_args()
    result = verify(args.screenshot, args.density)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
