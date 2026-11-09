#!/usr/bin/env python3
"""Expose the production bit-stream compressors to the dependency-light native test.

Compressor.cpp keeps every compressor class private to the file, each under a
banner comment naming it. This copies out the sections the network channel and
demo files rely on, so the test compiles the shipped code rather than a copy.
"""
from pathlib import Path
import re
import sys

SECTIONS = (
    "idCompressor_None",
    "idCompressor_BitStream",
    "idCompressor_RunLength_ZeroBased",
    "idCompressor_LZW",
)

# the banner's opening: some go on to describe the class before they close
BANNER = re.compile(r"^/\*\n={40,}\n\n\t(idCompressor\w*)\n", re.MULTILINE)


def generate(implementation: Path, output: Path) -> None:
    source = implementation.read_text(encoding="latin-1").replace("\r\n", "\n")
    banners = [(match.start(), match.group(1)) for match in BANNER.finditer(source)]
    names = [name for _, name in banners]
    generated = ""
    for wanted in SECTIONS:
        if names.count(wanted) != 1:
            raise SystemExit(f"{implementation}: expected one '{wanted}' section, found {names.count(wanted)}")
        index = names.index(wanted)
        begin = banners[index][0]
        end = banners[index + 1][0] if index + 1 < len(banners) else len(source)
        line = source.count("\n", 0, begin) + 1
        generated += f'\n#line {line} "{implementation.resolve().as_posix()}"\n'
        generated += source[begin:end]
    output.write_text(generated, encoding="latin-1")


if __name__ == "__main__":
    generate(*map(Path, sys.argv[1:]))
