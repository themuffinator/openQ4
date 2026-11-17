#!/usr/bin/env python3
"""Reference digests for tools/tests/native/Bc7CodecTest.cpp.

The native test decodes BLOCKS_PER_MODE seeded random blocks for each BC7 mode
(and a few reserved blocks with no mode bit) and compares the CRC-32 of each
mode's RGBA8 output with the table this script prints. The table comes from two
independent decoders, texture2ddecoder and Pillow, which must agree before it is
printed; Pillow decodes a reserved block to opaque black, so those take
texture2ddecoder's (and Direct3D's) transparent black.

Run it only to regenerate the table (pip install texture2ddecoder pillow):
    python tools/tests/bc7_reference.py
"""

from __future__ import annotations

import io
import struct
import sys
import zlib

BLOCKS_PER_MODE = 4096
RESERVED_BLOCKS = 16
SEED = 0x2468ACE1


class XorShift32:
    """The generator Bc7CodecTest.cpp uses, bit for bit."""

    def __init__(self, seed: int) -> None:
        self.state = seed

    def next(self) -> int:
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x


def make_blocks() -> list[list[bytes]]:
    rng = XorShift32(SEED)
    groups = []
    for mode in range(9):
        count = BLOCKS_PER_MODE if mode < 8 else RESERVED_BLOCKS
        group = []
        for _ in range(count):
            block = bytearray()
            for _ in range(4):
                block += struct.pack("<I", rng.next())
            if mode < 8:
                block[0] = ((block[0] & ~((1 << (mode + 1)) - 1)) | (1 << mode)) & 0xFF
            else:
                block[0] = 0
            group.append(bytes(block))
        groups.append(group)
    return groups


def decode_texture2ddecoder(block: bytes) -> bytes:
    import texture2ddecoder

    bgra = texture2ddecoder.decode_bc7(block, 4, 4)
    return bytes(x for p in range(16) for x in (bgra[p * 4 + 2], bgra[p * 4 + 1], bgra[p * 4], bgra[p * 4 + 3]))


def decode_pillow(blocks: list[bytes]) -> list[bytes]:
    from PIL import Image

    width, height = 4 * len(blocks), 4
    header = bytearray(128)
    struct.pack_into("<4sIIIIIII", header, 0, b"DDS ", 124, 0x81007, height, width, 16 * len(blocks), 0, 1)
    struct.pack_into("<II4s", header, 76, 32, 4, b"DX10")
    struct.pack_into("<I", header, 108, 0x1000)
    dx10 = struct.pack("<IIIII", 98, 3, 0, 1, 0)
    image = Image.open(io.BytesIO(bytes(header) + dx10 + b"".join(blocks))).convert("RGBA")
    pixels = image.tobytes()
    out = []
    for i in range(len(blocks)):
        out.append(b"".join(pixels[(y * width + i * 4) * 4:(y * width + i * 4 + 4) * 4] for y in range(4)))
    return out


def main() -> int:
    digests = []
    for mode, group in enumerate(make_blocks()):
        first = [decode_texture2ddecoder(block) for block in group]
        if mode < 8:
            second = decode_pillow(group)
            for i, (a, b) in enumerate(zip(first, second)):
                if a != b:
                    print(f"mode {mode} block {i}: texture2ddecoder and Pillow disagree", file=sys.stderr)
                    return 1
        digests.append(zlib.crc32(b"".join(first)) & 0xFFFFFFFF)
    print("static const uint32_t referenceCrc[9] = {")
    print("\t" + ", ".join(f"0x{d:08x}" for d in digests))
    print("};")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
