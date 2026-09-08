#!/usr/bin/env python3
"""Check the composition-smoke fixture's matching panels in an engine TGA.

For premultiplied source-over on a constant backdrop B, a group at opacity .5
must equal (its full-opacity result + B)/2 at every matching pixel. This tests
the combined renderer output, including nested layers, gradients and glyphs.
Requires Pillow; reads the engine render target, never the host screen.
"""
import argparse
import json
from pathlib import Path
from PIL import Image


def verify(path: Path, density: float) -> dict:
    image = Image.open(path).convert('RGB')
    background = image.getpixel((4, 4))
    first, second = round(24*density), round(456*density)
    top, width, height = round(32*density), round(408*density), round(246*density)
    if second+width > image.width or top+height > image.height:
        raise ValueError('capture does not contain both complete panels')
    errors = []
    changed = 0
    for y in range(top, top+height):
        for x in range(width):
            reference = image.getpixel((first+x, y))
            faded = image.getpixel((second+x, y))
            changed += max(abs(reference[i]-background[i]) for i in range(3)) > 8
            errors.append(max(abs(faded[i]-(reference[i]+background[i])/2) for i in range(3)))
    errors.sort()
    result = {'path': str(path), 'density': density, 'pixels': len(errors),
              'non_background_pixels': changed, 'background': background,
              'max_channel_error': max(errors), 'p99_channel_error': errors[int(.99*(len(errors)-1))],
              'pixels_over_4': sum(error > 4 for error in errors)}
    result['passed'] = changed > 1000 and result['pixels_over_4'] == 0
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('screenshot', type=Path)
    parser.add_argument('--density', type=float, required=True)
    args = parser.parse_args()
    result = verify(args.screenshot, args.density)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
