#!/usr/bin/env python3
"""PNG and zlib codec conformance against CPython's zlib.

--generate-native OUT   writes PngFixtures.inc: PNG files compressed by the real
                        zlib across every colour type, bit depth, Adam7, tRNS,
                        filter type, IDAT split and compression strategy, each
                        with its expected RGBA8 decode, plus damaged files with
                        the result they must produce.
--verify DIR            checks what the native test wrote to DIR: zlib streams
                        from the openQ4 deflate encoder must decompress with the
                        real zlib, and openQ4-encoded PNGs must decode (with an
                        independent pure-Python reader) to their source pixels.
--run EXE DIR           runs the native test with DIR, then --verify DIR.
"""

from __future__ import annotations

import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"

# keep these in step with pngResult_t in src/imagetools/PNG/PNGCodec.h
PNG_OK = 0
PNG_ERR_SIGNATURE = 1
PNG_ERR_TRUNCATED = 2
PNG_ERR_CRC = 3
PNG_ERR_HEADER = 4
PNG_ERR_FORMAT = 5
PNG_ERR_UNSUPPORTED = 6

ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(kind: int, row: bytes, prior: bytes, bpp: int) -> bytes:
    out = bytearray(len(row))
    for i, x in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = prior[i]
        c = prior[i - bpp] if i >= bpp else 0
        pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[kind]
        out[i] = (x - pred) & 0xFF
    return bytes(out)


def unfilter_row(kind: int, row: bytes, prior: bytes, bpp: int) -> bytes:
    out = bytearray(len(row))
    for i, x in enumerate(row):
        a = out[i - bpp] if i >= bpp else 0
        b = prior[i]
        c = prior[i - bpp] if i >= bpp else 0
        if kind > 4:
            raise ValueError("bad filter")
        pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[kind]
        out[i] = (x + pred) & 0xFF
    return bytes(out)


def pack_samples(samples: list[int], depth: int) -> bytes:
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out = bytearray()
    per = 8 // depth
    for start in range(0, len(samples), per):
        value = 0
        group = samples[start : start + per]
        for i, s in enumerate(group):
            value |= s << (8 - depth * (i + 1))
        out.append(value)
    return bytes(out)


def reduce16(v: int) -> int:
    return (v * 255 + 32767) // 65535


def expected_pixel(color: int, depth: int, px: list[int], palette, trns) -> tuple[int, int, int, int]:
    if color == 3:
        index = px[0]
        if index < len(palette):
            r, g, b = palette[index]
            a = trns[index] if trns is not None and index < len(trns) else 255
            return r, g, b, a
        return 0, 0, 0, 255
    if depth == 16:
        conv = reduce16
    else:
        maxv = (1 << depth) - 1
        conv = lambda v: v * 255 // maxv  # noqa: E731
    if color == 0:
        g = conv(px[0])
        a = 0 if trns is not None and px[0] == trns[0] else 255
        return g, g, g, a
    if color == 4:
        g = conv(px[0])
        return g, g, g, conv(px[1])
    if color == 2:
        a = 0 if trns is not None and tuple(px) == tuple(trns) else 255
        return conv(px[0]), conv(px[1]), conv(px[2]), a
    return conv(px[0]), conv(px[1]), conv(px[2]), conv(px[3])


def build_png(rng: random.Random, width: int, height: int, color: int, depth: int, interlace: int,
              use_trns: bool, strategy: int) -> tuple[bytes, bytes]:
    channels = CHANNELS[color]
    maxv = (1 << depth) - 1
    palette = []
    trns = None
    if color == 3:
        count = rng.randint(1, 1 << depth)
        palette = [(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(count)]
        if use_trns:
            trns = [rng.randrange(256) for _ in range(rng.randint(1, count))]
    # pixels: mix gradients, repeats and noise so every filter is exercised
    pixels = []
    for y in range(height):
        for x in range(width):
            px = []
            for ch in range(channels):
                mode = (x // 3 + y + ch) % 4
                if mode == 0:
                    v = (x * 37 + y * 11 + ch * 5) & maxv
                elif mode == 1:
                    v = rng.randrange(maxv + 1)
                elif mode == 2:
                    v = maxv
                else:
                    v = (x ^ y) & maxv
                px.append(v)
            if color == 3:
                # include out-of-range indices only for some images
                limit = len(palette) if rng.random() < 0.9 else maxv + 1
                px = [rng.randrange(limit)]
            pixels.append(px)
    if use_trns and color in (0, 2):
        # choose an existing pixel as the colour key so it matches somewhere
        key = pixels[rng.randrange(len(pixels))]
        trns = key[:channels]
    elif color in (0, 2):
        trns = None

    bpp_bits = channels * depth
    bpp = max(1, bpp_bits // 8)
    raw = bytearray()
    passes = ADAM7 if interlace else ((0, 0, 1, 1),)
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, width, dx))
        ys = list(range(y0, height, dy))
        if not xs or not ys:
            continue
        rowbytes = (len(xs) * bpp_bits + 7) // 8
        prior = bytes(rowbytes)
        for y in ys:
            samples = []
            for x in xs:
                samples.extend(pixels[y * width + x])
            row = pack_samples(samples, depth)
            kind = rng.randrange(5)
            raw.append(kind)
            raw += filter_row(kind, row, prior, bpp)
            prior = row

    level, zstrategy, wbits = strategy
    compressor = zlib.compressobj(level, zlib.DEFLATED, wbits, 9, zstrategy)
    stream = compressor.compress(bytes(raw)) + compressor.flush()

    out = bytearray(SIGNATURE)
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace))
    out += chunk(b"gAMA", struct.pack(">I", 45455))
    if color == 3:
        out += chunk(b"PLTE", bytes(c for rgb in palette for c in rgb))
    elif color in (2, 6) and rng.random() < 0.5:
        out += chunk(b"PLTE", bytes(rng.randrange(256) for _ in range(3 * 4)))  # suggested palette
    if trns is not None:
        if color == 3:
            out += chunk(b"tRNS", bytes(trns))
        else:
            out += chunk(b"tRNS", b"".join(struct.pack(">H", v) for v in trns))
    out += chunk(b"tEXt", b"Comment\x00openQ4 fixture")
    # split the stream into several IDAT chunks, sometimes with an empty one
    pos = 0
    while pos < len(stream):
        size = rng.choice((1, 2, 7, 64, 1 << 20))
        out += chunk(b"IDAT", stream[pos : pos + size])
        pos += size
        if rng.random() < 0.1:
            out += chunk(b"IDAT", b"")
    out += chunk(b"IEND", b"")

    expected = bytearray()
    for px in pixels:
        expected += bytes(expected_pixel(color, depth, px, palette, trns))
    return bytes(out), bytes(expected)


def damaged(rng: random.Random, good: bytes) -> list[tuple[str, bytes, int]]:
    cases = []
    cases.append(("signature", b"\x89PNX" + good[4:], PNG_ERR_SIGNATURE))
    # find the first IDAT and the IHDR
    ihdr_crc = 8 + 8 + 13
    bad_crc = bytearray(good)
    bad_crc[ihdr_crc] ^= 0x55
    cases.append(("IHDR CRC", bytes(bad_crc), PNG_ERR_CRC))
    cases.append(("truncated header", good[:20], PNG_ERR_TRUNCATED))
    bad_depth = bytearray(good[:8] + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 3, 2, 0, 0, 0)) + good[33:])
    cases.append(("invalid depth", bytes(bad_depth), PNG_ERR_HEADER))
    zero_width = good[:8] + chunk(b"IHDR", struct.pack(">IIBBBBB", 0, 4, 8, 2, 0, 0, 0)) + good[33:]
    cases.append(("zero width", zero_width, PNG_ERR_HEADER))
    critical = good[:33] + chunk(b"QxRZ", b"abc") + good[33:]
    cases.append(("unknown critical chunk", critical, PNG_ERR_UNSUPPORTED))
    ancillary = good[:33] + chunk(b"qxRZ", b"abc") + good[33:]
    cases.append(("unknown ancillary chunk", ancillary, PNG_OK))
    return cases


def simple_png(width: int, height: int, raw_rows: bytes, color: int = 2, depth: int = 8, idat_mutator=None) -> bytes:
    stream = zlib.compress(raw_rows, 6)
    if idat_mutator is not None:
        stream = idat_mutator(stream)
    return (SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0))
            + chunk(b"IDAT", stream) + chunk(b"IEND", b""))


def c_bytes(name: str, data: bytes) -> str:
    lines = [f"static const unsigned char {name}[{max(1, len(data))}] = {{"]
    for start in range(0, len(data), 24):
        lines.append("\t" + ",".join(str(b) for b in data[start : start + 24]) + ",")
    if not data:
        lines.append("\t0")
    lines.append("};")
    return "\n".join(lines)


def generate(out_path: Path) -> None:
    rng = random.Random(0x0B54)
    sizes = ((1, 1), (2, 1), (1, 5), (5, 1), (3, 3), (8, 8), (9, 9), (13, 7), (17, 4), (33, 2))
    strategies = (
        (0, zlib.Z_DEFAULT_STRATEGY, 15),
        (1, zlib.Z_DEFAULT_STRATEGY, 15),
        (6, zlib.Z_DEFAULT_STRATEGY, 15),
        (9, zlib.Z_DEFAULT_STRATEGY, 9),
        (9, zlib.Z_FIXED, 15),
        (6, zlib.Z_HUFFMAN_ONLY, 12),
        (6, zlib.Z_RLE, 15),
        (6, zlib.Z_FILTERED, 15),
    )
    decls = []
    table = []
    index = 0
    for color, depths in DEPTHS.items():
        for depth in depths:
            for interlace in (0, 1):
                for use_trns in ((False, True) if color in (0, 2, 3) else (False,)):
                    for _ in range(2):
                        width, height = rng.choice(sizes)
                        strategy = strategies[index % len(strategies)]
                        png, rgba = build_png(rng, width, height, color, depth, interlace, use_trns, strategy)
                        decls.append(c_bytes(f"fixturePng{index}", png))
                        decls.append(c_bytes(f"fixtureRgba{index}", rgba))
                        label = f"ct{color} d{depth} i{interlace}{' trns' if use_trns else ''} {width}x{height} z{strategy[0]}/{strategy[1]}/{strategy[2]}"
                        table.append(f'\t{{ "{label}", fixturePng{index}, sizeof( fixturePng{index} ), '
                                     f'fixtureRgba{index}, {len(rgba)}, {width}u, {height}u, {PNG_OK} }},')
                        index += 1

    # a larger image whose stream crosses many fast-path refills and blocks
    big_w, big_h = 211, 157
    png, rgba = build_png(rng, big_w, big_h, 6, 8, 0, False, (6, zlib.Z_DEFAULT_STRATEGY, 15))
    decls.append(c_bytes(f"fixturePng{index}", png))
    decls.append(c_bytes(f"fixtureRgba{index}", rgba))
    table.append(f'\t{{ "large rgba", fixturePng{index}, sizeof( fixturePng{index} ), fixtureRgba{index}, {len(rgba)}, {big_w}u, {big_h}u, {PNG_OK} }},')
    index += 1

    good, good_rgba = build_png(rng, 6, 4, 2, 8, 0, False, (6, zlib.Z_DEFAULT_STRATEGY, 15))
    for label, data, expected in damaged(rng, good):
        decls.append(c_bytes(f"fixturePng{index}", data))
        rgba_name = "NULL" if expected != PNG_OK else f"fixtureRgba{index}"
        if expected == PNG_OK:
            decls.append(c_bytes(f"fixtureRgba{index}", good_rgba))
        table.append(f'\t{{ "{label}", fixturePng{index}, sizeof( fixturePng{index} ), {rgba_name}, '
                     f'{len(good_rgba) if expected == PNG_OK else 0}, 6u, 4u, {expected} }},')
        index += 1

    # stream-level damage on a tiny RGB image
    rows = b"\x00" + bytes(range(12)) + b"\x02" + bytes(12)
    stream_cases = (
        ("bad filter type", simple_png(4, 2, b"\x05" + rows[1:]), PNG_ERR_FORMAT),
        ("bad adler", simple_png(4, 2, rows, idat_mutator=lambda s: s[:-1] + bytes([s[-1] ^ 1])), PNG_ERR_FORMAT),
        ("short image data", simple_png(4, 2, rows[:20]), PNG_ERR_TRUNCATED),
        ("truncated stream", simple_png(4, 2, rows, idat_mutator=lambda s: s[: len(s) // 2]), PNG_ERR_TRUNCATED),
        ("palette missing", simple_png(4, 2, rows, color=3), PNG_ERR_FORMAT),
    )
    for label, data, expected in stream_cases:
        decls.append(c_bytes(f"fixturePng{index}", data))
        table.append(f'\t{{ "{label}", fixturePng{index}, sizeof( fixturePng{index} ), NULL, 0, 4u, 2u, {expected} }},')
        index += 1

    # raw zlib streams from the real implementation for the inflate test
    corpus = bytearray()
    text = b"openQ4 restores Quake 4 on modern systems. " * 40
    corpus += text
    corpus += bytes(rng.randrange(256) for _ in range(5000))
    corpus += bytes(3000)
    corpus += bytes((i * 7) & 0xFF for i in range(70000))
    decls.append(c_bytes("zlibCorpus", bytes(corpus)))
    zlib_rows = []
    for zi, (level, zstrategy, wbits) in enumerate(strategies):
        compressor = zlib.compressobj(level, zlib.DEFLATED, wbits, 9, zstrategy)
        stream = compressor.compress(bytes(corpus)) + compressor.flush()
        decls.append(c_bytes(f"zlibStream{zi}", stream))
        zlib_rows.append(f"\t{{ zlibStream{zi}, sizeof( zlibStream{zi} ) }},")

    text_out = [
        "// Generated by tools/tests/png_codec.py --generate-native; do not edit.",
        "#include <stddef.h>",
        *decls,
        "struct pngFixture_t { const char *label; const unsigned char *png; size_t pngSize; "
        "const unsigned char *rgba; size_t rgbaSize; unsigned width; unsigned height; int result; };",
        "static const pngFixture_t pngFixtures[] = {",
        *table,
        "};",
        "struct zlibFixture_t { const unsigned char *data; size_t size; };",
        "static const zlibFixture_t zlibFixtures[] = {",
        *zlib_rows,
        "};",
        "",
    ]
    out_path.write_text("\n".join(text_out), encoding="utf-8", newline="\n")


def read_png(data: bytes) -> tuple[int, int, int, bytes]:
    """Independent decoder for the 8-bit, non-interlaced files openQ4 writes."""
    if data[:8] != SIGNATURE:
        raise AssertionError("missing signature")
    pos = 8
    idat = bytearray()
    header = None
    seen_end = False
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length : pos + 12 + length])[0]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise AssertionError(f"bad CRC on {kind!r}")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            seen_end = True
            break
        pos += 12 + length
    if header is None or not seen_end:
        raise AssertionError("missing IHDR or IEND")
    width, height, depth, color, comp, filt, interlace = header
    if depth != 8 or comp or filt or interlace:
        raise AssertionError(f"unexpected header {header}")
    channels = CHANNELS[color]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    if len(raw) != height * (stride + 1):
        raise AssertionError("wrong image data size")
    prior = bytes(stride)
    rgba = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        row = unfilter_row(raw[start], raw[start + 1 : start + 1 + stride], prior, channels)
        prior = row
        for x in range(width):
            px = row[x * channels : (x + 1) * channels]
            if color == 0:
                rgba += bytes((px[0], px[0], px[0], 255))
            elif color == 4:
                rgba += bytes((px[0], px[0], px[0], px[1]))
            elif color == 2:
                rgba += bytes((px[0], px[1], px[2], 255))
            else:
                rgba += bytes(px)
    return width, height, color, bytes(rgba)


def verify(directory: Path) -> None:
    checked = 0
    for stream_path in sorted(directory.glob("zlib_*.bin")):
        source = stream_path.with_suffix(".raw").read_bytes()
        if zlib.decompress(stream_path.read_bytes()) != source:
            raise AssertionError(f"{stream_path.name}: zlib output differs from the source")
        checked += 1
    for png_path in sorted(directory.glob("enc_*.png")):
        expected = png_path.with_suffix(".rgba").read_bytes()
        width, height = struct.unpack("<II", expected[:8])
        color_expected = expected[8]
        got_w, got_h, color, rgba = read_png(png_path.read_bytes())
        if (got_w, got_h) != (width, height):
            raise AssertionError(f"{png_path.name}: size {got_w}x{got_h}, expected {width}x{height}")
        if color != color_expected:
            raise AssertionError(f"{png_path.name}: colour type {color}, expected {color_expected}")
        if rgba != expected[9:]:
            raise AssertionError(f"{png_path.name}: pixels differ")
        checked += 1
    if checked < 20:
        raise AssertionError(f"only {checked} encoder outputs found in {directory}")
    print(f"png codec: {checked} openQ4 encoder outputs verified with CPython zlib")


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "--generate-native":
        generate(Path(argv[2]))
        return 0
    if len(argv) == 3 and argv[1] == "--verify":
        verify(Path(argv[2]))
        return 0
    if len(argv) == 4 and argv[1] == "--run":
        directory = Path(argv[3])
        directory.mkdir(parents=True, exist_ok=True)
        for stale in list(directory.glob("enc_*")) + list(directory.glob("zlib_*")):
            stale.unlink()
        completed = subprocess.run([argv[2], str(directory)], check=False)
        if completed.returncode != 0:
            print(f"png codec: native test failed with exit code {completed.returncode}", file=sys.stderr)
            return 1
        verify(directory)
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
