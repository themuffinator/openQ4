#!/usr/bin/env python3
"""Expose the production memory-file class and methods to the native harness.

Only engine services (the allocator, error reporting, string and base-file types)
are substituted by the harness. The file operations themselves are compiled
verbatim, with source locations retained for compiler and sanitizer diagnostics.
"""

from pathlib import Path
import sys


def section(path: Path, start: str, end: str) -> str:
    source = path.read_text(encoding="utf-8")
    begin = source.index(start)
    finish = source.index(end, begin)
    line = source.count("\n", 0, begin) + 1
    return f'#line {line} "{path.resolve().as_posix()}"\n' + source[begin:finish]


if __name__ == "__main__":
    header, implementation, output = map(Path, sys.argv[1:])
    generated = section(header, "class idFile_Memory :", "\n\nclass idFile_BitMsg :")
    generated += "\n" + section(
        implementation,
        "idFile_Memory::idFile_Memory( void )",
        "\n/*\n=================================================================================\n\nidFile_BitMsg",
    )
    output.write_text(generated, encoding="utf-8")
