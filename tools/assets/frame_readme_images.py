#!/usr/bin/env python3
"""Frame the README screenshots in the openQ4 instrument-plate style.

Each source image under assets/docs/img/ is clipped to an asymmetric plate
(two diagonally opposite corners cut deeply at 45 degrees, the other two
slightly), lit by a soft olive rim along its framed edges, and wrapped in two
open olive rails that start at the deep cuts and dissolve toward the trailing
ends, each with a raised 45-degree notch and a triangle marker. Everything
outside the plate stays transparent, so the frame sits on GitHub's light and
dark themes alike. See docs/dev/ui-visual-design.md, section 2.

The sources are left untouched; each framed copy is written next to its
source with a "-framed" suffix. Re-run after replacing a source image:

    python tools/assets/frame_readme_images.py
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
IMAGE_DIR = ROOT / "assets" / "docs" / "img"
SOURCES = (
    "readme-airdefense1-cinematic.png",
    "readme-bloom-hdr.png",
    "readme-lightgrid.png",
    "readme-crt.png",
)

SS = 4  # supersampling factor for the vector layers

RAIL = (0xA0, 0xA0, 0x40)    # marine.rail.card
MARKER = (0x90, 0x9A, 0x49)  # marine.marker
RIM = (0x62, 0x69, 0x34)     # marine.rim


def plate_polygon(x0: float, y0: float, x1: float, y1: float, deep: float, slight: float) -> list[tuple[float, float]]:
    """Clockwise from the top edge: deep cuts top-left and bottom-right."""
    return [
        (x0 + deep, y0),
        (x1 - slight, y0),
        (x1, y0 + slight),
        (x1, y1 - deep),
        (x1 - deep, y1),
        (x0 + slight, y1),
        (x0, y1 - slight),
        (x0, y0 + deep),
    ]


def polyline_length(points: list[tuple[float, float]]) -> float:
    return sum(math.dist(a, b) for a, b in zip(points, points[1:]))


def draw_fading_polyline(draw: ImageDraw.ImageDraw, points, width: float, color, fade: float) -> None:
    """Draw a polyline whose alpha is full in the middle and falls to zero over
    `fade` (a fraction of the total length) at both ends."""
    total = polyline_length(points)
    fade_len = max(total * fade, 1.0)
    step = max(width * 0.5, 1.0)
    travelled = 0.0
    for a, b in zip(points, points[1:]):
        seg = math.dist(a, b)
        n = max(int(seg / step), 1)
        for i in range(n):
            t0, t1 = i / n, (i + 1) / n
            p0 = (a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0)
            p1 = (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)
            d = travelled + seg * (t0 + t1) / 2
            alpha = min(1.0, d / fade_len, (total - d) / fade_len)
            alpha = alpha * alpha * (3 - 2 * alpha)  # smoothstep
            draw.line([p0, p1], fill=(*color, int(255 * alpha)), width=max(int(round(width)), 1))
        travelled += seg
    # Round the joints so the 45-degree turns stay solid.
    r = width / 2
    for p in points[1:-1]:
        draw.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=(*color, 255))


def frame_image(src: Image.Image) -> Image.Image:
    w, h = src.size
    unit = min(w, h)
    pad = round(unit * 0.05)            # transparent margin holding the rails
    gap = max(round(unit * 0.012), 3)     # air between the plate and its rail
    deep = round(unit * 0.06)
    slight = round(unit * 0.018)
    rail_w = max(unit * 0.0045, 1.6)
    notch = round(unit * 0.012)           # how far a notch rises off its rail

    cw, ch = w + 2 * pad, h + 2 * pad
    x0, y0, x1, y1 = pad, pad, pad + w, pad + h

    # Plate mask, supersampled for clean 45-degree edges.
    mask_ss = Image.new("L", (cw * SS, ch * SS), 0)
    ImageDraw.Draw(mask_ss).polygon(
        [(x * SS, y * SS) for x, y in plate_polygon(x0, y0, x1, y1, deep, slight)], fill=255
    )
    mask = mask_ss.resize((cw, ch), Image.LANCZOS)

    # Rim light: a soft olive glow just inside the framed edges, added to the image.
    plate = Image.new("RGB", (cw, ch), (0, 0, 0))
    plate.paste(src.convert("RGB"), (pad, pad))
    edge = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(3)))
    rim_radius = max(unit * 0.03, 4)
    rim = edge.filter(ImageFilter.GaussianBlur(rim_radius))
    rim = rim.point(lambda v: min(255, int(v * 6)))
    rim = ImageChops.multiply(rim, mask)
    rim_rgb = Image.merge("RGB", [rim.point(lambda v, c=c: v * c * 0.37 / 255) for c in RIM])
    plate = ImageChops.add(plate, rim_rgb)

    out = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    out.paste(plate, (0, 0), mask)

    # Rails: two open L-brackets offset outside the plate, one per deep cut.
    rails = Image.new("RGBA", (cw * SS, ch * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(rails)
    o = gap + rail_w / 2
    rd = deep + o * (math.sqrt(2) - 1)    # keep the cut rail parallel to the plate cut
    s = SS

    def p(x: float, y: float) -> tuple[float, float]:
        return (x * s, y * s)

    # Leading rail: up the left edge, through the top-left cut, along the top,
    # stepping out over a raised notch with 45-degree risers.
    lx, ty = x0 - o, y0 - o
    n0 = x0 + deep + (w - deep) * 0.10
    n1 = n0 + (w - deep) * 0.08
    leading = [
        p(lx, y0 + h * 0.62),
        p(lx, ty + rd),
        p(lx + rd, ty),
        p(n0, ty),
        p(n0 + notch, ty - notch),
        p(n1 - notch, ty - notch),
        p(n1, ty),
        p(x0 + w * 0.58, ty),
    ]
    draw_fading_polyline(draw, leading, rail_w * s, RAIL, fade=0.32)

    # Trailing rail: the mirror image through the bottom-right cut.
    rx, by = x1 + o, y1 + o
    m0 = x1 - deep - (w - deep) * 0.10
    m1 = m0 - (w - deep) * 0.08
    trailing = [
        p(rx, y1 - h * 0.62),
        p(rx, by - rd),
        p(rx - rd, by),
        p(m0, by),
        p(m0 - notch, by + notch),
        p(m1 + notch, by + notch),
        p(m1, by),
        p(x1 - w * 0.58, by),
    ]
    draw_fading_polyline(draw, trailing, rail_w * s, RAIL, fade=0.32)

    # Triangle markers follow the raised notches, clear of the rail.
    size = notch * 1.4
    for (ax, ay), flip in (((n1 + notch * 1.2, ty - notch * 1.3), False),
                           ((m1 - notch * 1.2, by + notch * 1.3), True)):
        if not flip:  # the upper-trailing half of a square
            tri = [p(ax, ay - size / 2), p(ax + size, ay - size / 2), p(ax + size, ay + size / 2)]
        else:
            tri = [p(ax, ay + size / 2), p(ax - size, ay + size / 2), p(ax - size, ay - size / 2)]
        draw.polygon(tri, fill=(*MARKER, 230))

    # Small ticks mark the two slight corners.
    tick = unit * 0.03
    tw = max(int(round(rail_w * s * 0.8)), 1)
    tr = [p(x1 + o - tick, y0 - o), p(x1 + o - slight * 0.6, y0 - o), p(x1 + o, y0 - o + slight * 0.6), p(x1 + o, y0 - o + tick)]
    bl = [p(x0 - o + tick, y1 + o), p(x0 - o + slight * 0.6, y1 + o), p(x0 - o, y1 + o - slight * 0.6), p(x0 - o, y1 + o - tick)]
    for pts in (tr, bl):
        draw.line(pts, fill=(*RAIL, 150), width=tw, joint="curve")

    rails = rails.resize((cw, ch), Image.LANCZOS)
    return Image.alpha_composite(out, rails)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*", default=SOURCES, help="source file names under assets/docs/img")
    args = parser.parse_args()
    for name in args.names:
        src_path = IMAGE_DIR / name
        dst_path = src_path.with_name(src_path.stem + "-framed.png")
        framed = frame_image(Image.open(src_path))
        framed.save(dst_path, optimize=True)
        print(f"{dst_path.relative_to(ROOT).as_posix()}: {framed.size[0]}x{framed.size[1]}, {dst_path.stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
