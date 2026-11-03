#!/usr/bin/env python3
"""Fault-inject production competition artifact writers at their idFile boundary.

Existing storage contracts execute serialization and canonical path validation.
This fixture executes the three production filesystem adapters with controlled
short writes, failed writes, failed sync/rename/remove and exact capacity edges.
It does not simulate power loss or establish physical filesystem durability.
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
MATCH = ROOT / "src/mpgame/mp/match"
ADAPTERS = (
    ("Evidence", "MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES", "MatchEvidenceStorage.h", True),
    ("SeriesReport", "MP_SERIES_REPORT_MAX_JSON_BYTES", "MatchSeriesReport.h", True),
    ("SeriesRecovery", "MP_SERIES_RECOVERY_MAX_BYTES", "MatchSeriesRecovery.h", False),
)


def fixture(sources: dict[str, str]) -> str:
    adapters, calls = [], []
    for name, capacity, header, terminator in ADAPTERS:
        source = sources[name]
        cls = f"mpMatch{name}FileSystemWriter"
        limit = int(re.search(rf"{capacity}\s*=\s*(\d+)", (MATCH / header).read_text()).group(1))
        bound = "MP_SERIES_REPORT_STORAGE_JSON_BYTES" if name == "SeriesReport" else capacity
        prefix = f"MPMatch{name}" + ("" if name == "SeriesRecovery" else "Storage")
        root = re.search(r'static const char \*const \w+\s*=\s*"fs_savepath";', source).group(0)
        adapters.append(f'''
static const int {bound} = {limit};
{root}
// Path grammar is covered by the storage contracts. This fixture supplies a
// single accepted capability and verifies that the adapter applies the gate.
static bool {prefix}IsTemporaryQPath(const char *p) {{ return p && std::strcmp(p, "canonical.pending") == 0; }}
static bool {prefix}IsPromotionPair(const char *p, const char *q) {{
    return {prefix}IsTemporaryQPath(p) && q && std::strcmp(q, "canonical.json") == 0;
}}
class {cls} {{
public:
    explicit {cls}(idFileSystem *backend);
    int WriteTemp(const char *, const void *, int);
    bool Promote(const char *, const char *);
    bool RemoveTemp(const char *);
private:
    idFileSystem *fileSystemBackend;
}};
''')
        adapters += [block(source, signature) for signature in (
            f"{cls}::{cls}(", f"int {cls}::WriteTemp(", f"bool {cls}::Promote(", f"bool {cls}::RemoveTemp(")]
        calls.append(f'TestAdapter<{cls}>({limit - int(terminator)}, "{name}");')
    harness = (ROOT / "tools/tests/native/MatchArtifactIoFixture.cpp").read_text(encoding="utf-8")
    return harness.replace("// PRODUCTION_ADAPTERS", "\n".join(adapters)).replace("// TEST_ADAPTERS", "\n".join(calls))


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    sources = {name: (MATCH / f"Match{name}FileSystem.cpp").read_text(encoding="utf-8") for name, *_ in ADAPTERS}
    controls = (
        ("sync failure ignored", "if ( !synced ) {", "if ( false ) {"),
        ("short writes not completed", "while ( committedBytes < bytes )", "while ( committedBytes < bytes && committedBytes == 0 )"),
        ("write offset not advanced", "source + committedBytes, remaining", "source, remaining"),
        ("file handle leaked", "fileSystemBackend->CloseFile( file );", ""),
    )
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-artifact-io-", dir=ROOT / ".tmp") as temporary:
        directory = Path(temporary)
        def run(candidate: dict[str, str], name: str) -> subprocess.CompletedProcess:
            cpp, exe = directory / f"{name}.cpp", directory / (name + (".exe" if os.name == "nt" else ""))
            cpp.write_text(fixture(candidate), encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(exe)]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=directory, check=True)
            return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)
        result = run(sources, "adapters")
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        for index, (label, old, new) in enumerate(controls):
            for name in sources:
                if sources[name].count(old) != 1:
                    raise SystemExit(f"mutation anchor not unique: {name}: {label}")
                candidate = dict(sources)
                candidate[name] = candidate[name].replace(old, new, 1)
                result = run(candidate, f"control{index}{name}")
                if result.returncode == 0:
                    raise SystemExit(f"control did not fail: {name}: {label}")
    print(f"mp_match_artifact_io_state: ok (3 adapters; {len(controls) * len(sources)} reversal controls fail)")


if __name__ == "__main__":
    main()
