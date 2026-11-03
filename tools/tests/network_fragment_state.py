#!/usr/bin/env python3
"""Execute production fragment sending/reassembly against deterministic datagrams.

The fixture substitutes the port, byte-message interface and decompression boundary;
SendNextFragment and Process run verbatim. This qualifies channel assembly, not
compression, real sockets, path MTU discovery or cross-machine interoperability.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from mp_lag_compensation_state import block

ROOT = Path(__file__).resolve().parents[2]


def fixture(source: str) -> str:
    definitions = "\n".join(re.findall(r"^#define\s+(?:MAX_PACKETLEN6?|FRAGMENT_SIZE6?|FRAGMENT_BIT)\b.*", source, re.M))
    header = (ROOT / "src/framework/async/MsgChannel.h").read_text(encoding="utf-8")
    definitions += "\n" + re.search(r"^#define\s+MAX_MESSAGE_SIZE\b.*", header, re.M).group(0)
    bodies = "\n".join(block(source, name) for name in (
        "static int Net_FragmentSize(", "void idMsgChannel::SendNextFragment(", "bool idMsgChannel::Process("))
    harness = (ROOT / "tools/tests/native/MsgChannelFragmentFixture.cpp").read_text(encoding="utf-8")
    return harness.replace("// PRODUCTION_DEFINITIONS", definitions).replace("// PRODUCTION_FUNCTIONS", bodies)


def main() -> None:
    source = (ROOT / "src/framework/async/MsgChannel.cpp").read_text(encoding="utf-8")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    controls = (
        ("older sequence discards newer assembly", "if ( sequence < fragmentSequence ) {", "if ( false ) {"),
        ("older plain payload overwrites newer fragments", "if ( sequence < fragmentSequence ) {", "if ( fragmented && sequence < fragmentSequence ) {"),
        ("oversized fragment accepted", " || fragLength > Net_FragmentSize( remoteAddress )", ""),
        ("exact multiple lacks final empty fragment",
         " && fragLength != Net_FragmentSize( remoteAddress )", ""),
        ("IPv6 uses IPv4 fragment size", "adr.type == NA_IP6 ? FRAGMENT_SIZE6 : FRAGMENT_SIZE", "FRAGMENT_SIZE"),
        ("duplicate sequence accepted", "sequence <= incomingSequence", "sequence < incomingSequence"),
        ("fragments must arrive in order", "const unsigned int fragmentBit =", "if ( fragStart != fragmentLength ) { return false; } const unsigned int fragmentBit ="),
        ("duplicates counted twice", "if ( fragmentReceived & fragmentBit ) {", "if ( false ) {"),
    )
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="network-fragments-", dir=output) as temporary:
        directory = Path(temporary)
        def run(code: str, name: str) -> subprocess.CompletedProcess:
            cpp = directory / f"{name}.cpp"
            exe = directory / (name + (".exe" if os.name == "nt" else ""))
            cpp.write_text(fixture(code), encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                       "-Wno-sign-compare", str(cpp), "-o", str(exe)]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=directory, check=True)
            return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)
        result = run(source, "fragments")
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        for index, (label, original, replacement) in enumerate(controls):
            if source.count(original) != 1:
                raise SystemExit(f"mutation anchor not unique: {label}")
            result = run(source.replace(original, replacement, 1), f"control{index}")
            if result.returncode == 0:
                raise SystemExit(f"control did not fail: {label}")
    print(f"network_fragment_state: ok ({len(controls)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
