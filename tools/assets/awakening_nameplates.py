"""Generate the Awakening dropship seat nameplates.

python tools/assets/awakening_nameplates.py [--out DIR] [--preview FILE]

The Awakening reuses Quake 4's dropship cabin, whose seats still carry the
original campaign's squad names. These plates name The Awakening's Falcon
squad instead. They are openQ4's own drawings (no retail pixels): a worn metal
frame around a dark ribbed strip with a squared-off arrow end, a status tab and
a vent grid, lettered in openQ4 Chain (content/baseoq4/pak0/fonts/chain.ttf).
Output is deterministic, so rerunning reproduces the committed files.
"""
import argparse
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "content/baseoq4/pak0/fonts/chain.ttf"
OUT = ROOT / "content/baseoq4/pak0/models/openq4/awakening/dropship"
NAMES = ["tak", "darnell", "navajas", "dekard", "townsend", "atkins", "mitchell", "markswell"]
W, H = 256, 64


def plate(name: str) -> Image.Image:
    rng = random.Random(f"openq4-awakening-nameplate-{name}")
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Frame: olive-grey painted metal with a bevel and fine grain.
    d.rectangle([2, 2, W - 1, H - 1], fill=(64, 64, 56, 255))
    d.line([2, 2, W - 1, 2], fill=(92, 92, 82, 255), width=2)
    d.line([2, 2, 2, H - 1], fill=(86, 86, 76, 255), width=2)
    d.line([2, H - 2, W - 1, H - 2], fill=(30, 30, 26, 255), width=2)
    d.line([W - 2, 2, W - 2, H - 1], fill=(34, 34, 30, 255), width=2)
    px = img.load()
    for y in range(3, H - 2):
        for x in range(3, W - 2):
            r, g, b, a = px[x, y]
            n = rng.randint(-7, 7)
            px[x, y] = (max(0, r + n), max(0, g + n), max(0, b + n - 1), a)

    # Dark ribbed strip with a squared arrow end, sunk into the frame.
    strip = [(10, 14), (196, 14), (212, 32), (196, 50), (10, 50)]
    d.polygon([(x + 1, y + 1) for x, y in strip], fill=(98, 98, 88, 255))
    d.polygon(strip, fill=(8, 8, 7, 255))
    for y in range(16, 50, 3):
        d.line([12, y, 205, y], fill=(26, 26, 23, 255))
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).polygon(strip, fill=255)
    ribs = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ribs)
    for y in range(16, 50, 3):
        rd.line([12, y, 205, y], fill=(30, 30, 26, 255))
    # A soft diagonal glint across the strip.
    glint = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glint).line([(120, 54), (168, 8)], fill=70, width=5)
    glint = glint.filter(ImageFilter.GaussianBlur(2.5))
    sheen = Image.new("RGBA", (W, H), (200, 205, 190, 255))
    sheen.putalpha(Image.composite(glint, Image.new("L", (W, H), 0), mask))
    img.alpha_composite(sheen)

    # Status tab (top right) and vent grid (bottom right).
    d = ImageDraw.Draw(img)
    d.polygon([(222, 6), (250, 6), (250, 18), (210, 18)], fill=(20, 22, 16, 255))
    d.polygon([(226, 8), (248, 8), (248, 16), (216, 16)], fill=(150, 196, 58, 255))
    d.line([(228, 9), (247, 9)], fill=(205, 235, 120, 255))
    for row in range(3):
        for col in range(3):
            x, y = 222 + col * 9, 28 + row * 9
            d.rectangle([x, y, x + 5, y + 5], fill=(30, 30, 27, 255))
            d.line([x + 1, y + 5, x + 5, y + 5], fill=(96, 96, 86, 255))

    # Lettering: fit the longest name, keep every plate at one size.
    size = 30
    font = ImageFont.truetype(str(FONT), size)
    widest = max(d.textlength(n.upper(), font=font) for n in NAMES)
    while widest > 172 and size > 12:
        size -= 1
        font = ImageFont.truetype(str(FONT), size)
        widest = max(d.textlength(n.upper(), font=font) for n in NAMES)
    text = name.upper()
    box = d.textbbox((0, 0), text, font=font)
    x, y = 20 - box[0], 32 - (box[1] + box[3]) / 2
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((x, y), text, font=font, fill=(200, 210, 190, 150))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(1.6)))
    d = ImageDraw.Draw(img)
    d.text((x, y), text, font=font, fill=(232, 234, 222, 255))
    # Light paint wear on the letters.
    px = img.load()
    for _ in range(120):
        wx, wy = rng.randint(14, 200), rng.randint(16, 48)
        r, g, b, a = px[wx, wy]
        if r > 150:
            px[wx, wy] = (r - 60, g - 60, b - 60, a)
    return img


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=OUT)
    parser.add_argument("--preview", type=Path, help="Also write a contact sheet PNG here")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    plates = []
    for name in NAMES:
        img = plate(name)
        # Uncompressed TGA keeps the committed bytes stable across Pillow versions.
        img.save(args.out / f"nameplate_{name}.tga", compression=None)
        plates.append(img)
    if args.preview:
        sheet = Image.new("RGBA", (W * 2 + 12, (H + 6) * 4 + 6), (90, 90, 90, 255))
        for i, img in enumerate(plates):
            sheet.alpha_composite(img, (6 + (i % 2) * (W + 0), 6 + (i // 2) * (H + 6)))
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
