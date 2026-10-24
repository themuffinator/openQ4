#!/usr/bin/env python3
"""Execute the simulated network condition cases against src/sys/NetworkImpairment.h.

tools/tests/native/NetworkImpairmentCases.h holds the cases (fixed delay and
order, jitter bounds and reordering, loss and duplication rates, seed
repeatability, oversize discards, a 9000-byte datagram, clock wrap and
capacity). CoreSafetyTest.cpp runs them in the native suite; this driver also
compiles them against single-reversal copies of the header and requires each
copy to fail, so the cases cannot quietly stop checking what they claim.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = "src/sys/NetworkImpairment.h"
MAIN = """#include "tools/tests/native/NetworkImpairmentCases.h"

int main() {
	const int failed = ExerciseNetworkImpairment();
	std::printf( "network impairment cases: %s\\n", failed ? "FAILED" : "ok" );
	return failed ? 1 : 0;
}
"""

CONTROLS = (
    ("release ignores the due time", "if ( !AtOrBefore( entries[i].due, now ) ) {\n\t\t\t\t\tcontinue;\n\t\t\t\t}", ""),
    ("no duplication", "const int copies = random.Chance( settings.duplicatePercent ) ? 2 : 1;", "const int copies = 1;"),
    ("no jitter", "static_cast<uint32_t>( settings.latencyMsec ) +\n\t\t\t\tstatic_cast<uint32_t>( random.UpTo( settings.jitterMsec ) ) );",
     "static_cast<uint32_t>( settings.latencyMsec ) );"),
    ("clock wrap compared directly",
     "return static_cast<int32_t>( static_cast<uint32_t>( a ) - static_cast<uint32_t>( b ) ) <= 0;", "return a <= b;"),
    ("an oversize datagram stops the drain", "stats.discarded++;\n\t\t\t\tcontinue;", "stats.discarded++;\n\t\t\t\treturn false;"),
    ("no capacity limit", "if ( static_cast<int>( entries.size() ) >= MAX_QUEUED_DATAGRAMS ||", "if ( false &&"),
)


def compile_and_run(scratch: Path, name: str, compiler: str) -> int:
    source = scratch / f"{name}.cpp"
    exe = scratch / (f"{name}.exe" if os.name == "nt" else name)
    source.write_text(MAIN, encoding="utf-8")
    command = [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", f"-I{scratch / 'include'}", f"-I{ROOT}",
               str(source), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode != 0:
        raise SystemExit(f"network_impairment_state: {name} does not build\n{built.stderr}")
    return subprocess.run([str(exe)], capture_output=True, text=True).returncode


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the network impairment cases")
    text = (ROOT / HEADER).read_text(encoding="utf-8").replace("\r\n", "\n")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="network-impairment-", dir=output) as directory:
        scratch = Path(directory)
        header = scratch / "include" / HEADER
        header.parent.mkdir(parents=True)
        header.write_text(text, encoding="utf-8")
        if compile_and_run(scratch, "baseline", compiler) != 0:
            raise SystemExit("network_impairment_state: FAILED (the cases fail against the real header)")
        for index, (label, original, mutated) in enumerate(CONTROLS):
            if text.count(original) != 1:
                raise SystemExit(f"network_impairment_state: control anchor not unique: {label}")
            header.write_text(text.replace(original, mutated, 1), encoding="utf-8")
            if compile_and_run(scratch, f"control{index}", compiler) == 0:
                raise SystemExit(f"network_impairment_state: control did not fail: {label}")
    print(f"network_impairment_state: ok ({len(CONTROLS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
