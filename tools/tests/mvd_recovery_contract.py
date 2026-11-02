#!/usr/bin/env python3
"""Execute production MVD recovery of interrupted recordings.

tools/tests/native/MvdRecoveryFixture.cpp writes a synthetic recording with the
production writer into an in-memory filesystem, cuts the staged .mvd.part at
every byte, corrupts a record, leaves a stale index from a half-finished
finalize and damages the header, then runs idMultiViewDemo::Recover on each.
Every result is read back with the production reader and held to the clean-end
rules playback enforces: exactly the records that were complete, one rebuilt
timeline index the header points at, and an end record whose counts match.
The source is never modified and nothing is left behind when recovery fails.

The format constants, reader, writer and Recover itself are spliced in verbatim
from src/framework/async/MultiViewDemo.cpp. Single-reversal controls then re-run
the fixture against mutated production source and require each to fail.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HELPERS = (
    "static bool MVD_WriteExact(",
    "static bool MVD_ReadExact(",
    "static unsigned int MVD_ReadLittleUInt(",
    "static unsigned short MVD_ReadLittleUShort(",
    "static bool MVD_PathExists(",
    "static bool MVD_IsLiveRecord(",
)

FUNCTIONS = (
    "bool idMultiViewDemo::WriteHeader()",
    "bool idMultiViewDemo::ReadHeader(",
    "bool idMultiViewDemo::WriteRecord(",
    "idMultiViewDemo::readResult_t idMultiViewDemo::ReadRecord(",
    "bool idMultiViewDemo::WriteIndexRecord()",
    "bool idMultiViewDemo::WriteNavigationRecords()",
    "bool idMultiViewDemo::NoteNavigationRecord(",
    "idStr idMultiViewDemo::BuildRecordingName(",
    "bool idMultiViewDemo::Recover(",
)

ENUMS = (
    "enum mvdRecordType_t {",
    "enum mvdRecordFlags_t {",
    "enum mvdFeature_t {",
    "enum mvdOptionalFeature_t {",
)

MUTATIONS = (
    (
        "a stale index is copied instead of rebuilt",
        "if ( record.type == MVD_RECORD_INDEX || record.type == MVD_RECORD_END ||\n",
        "if ( record.type == MVD_RECORD_END ||\n",
    ),
    (
        "the end record does not count the rebuilt index",
        "\t\tpayload.WriteInt( recordCount );\n\t\tok = WriteRecord( MVD_RECORD_END",
        "\t\tpayload.WriteInt( recordCount - 1 );\n\t\tok = WriteRecord( MVD_RECORD_END",
    ),
    (
        "a recording with no complete snapshot is published",
        "if ( ok && snapshotCount == 0 ) {",
        "if ( false ) {",
    ),
    (
        "stale marker and keyframe directories are copied instead of rebuilt",
        "if ( record.type == MVD_RECORD_INDEX || record.type == MVD_RECORD_END ||\n\t\t\t record.type == MVD_RECORD_MARKER_TABLE || record.type == MVD_RECORD_KEYFRAME_TABLE ) {",
        "if ( record.type == MVD_RECORD_INDEX || record.type == MVD_RECORD_END ) {",
    ),
    (
        "recovered markers and keyframes are not collected for their directories",
        "\t\t\tNoteNavigationRecord( record, outputOffset );\n",
        "\n",
    ),
    (
        "a failed recovery leaves its staged output behind",
        "\t\tfileSystem->RemoveFile( tempFileName.c_str(), \"fs_savepath\" );\n",
        "\n",
    ),
)


def block(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_fixture(mvd: str) -> str:
    enums = "\n".join(block(mvd, signature) + ";" for signature in ENUMS)
    constants = "\n".join(re.findall(r"^static const [^;]*MVD_[^;]*;", mvd, re.M))
    helpers = "\n".join(block(mvd, signature) for signature in HELPERS)
    bodies = "\n".join(block(mvd, signature) for signature in FUNCTIONS)
    harness = (ROOT / "tools/tests/native/MvdRecoveryFixture.cpp").read_text(encoding="utf-8")
    return (harness
            .replace("// PRODUCTION_CONSTANTS", enums + "\n" + constants)
            .replace("// PRODUCTION_FUNCTIONS", helpers + "\n" + bodies))


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", f"-I{ROOT}", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-Wno-unused-const-variable", "-Wno-unused-function", "-Wno-unused-variable",
               str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)


def main() -> None:
    mvd = (ROOT / "src/framework/async/MultiViewDemo.cpp").read_text(encoding="utf-8")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the MVD recovery fixture")

    # The command is registered and the library and guides describe it.
    for token in ('cmdSystem->AddCommand( "mvdRecover", Recover_f',
                  "bool idMultiViewDemo::Recover( const idCmdArgs &args )"):
        if token not in mvd:
            raise SystemExit(f"mvd_recovery_contract: missing {token!r}")

    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mvd-recovery-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(mvd), path, "recovery", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mvd_recovery_contract: FAILED\n{result.stdout}{result.stderr}")
        summary = result.stdout.strip()

        for index, (label, original, mutated) in enumerate(MUTATIONS):
            if mvd.count(original) != 1:
                raise SystemExit(f"mvd_recovery_contract: mutation anchor not unique: {label}")
            control = compile_and_run(build_fixture(mvd.replace(original, mutated, 1)), path, f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mvd_recovery_contract: control did not fail: {label}")

    print(f"mvd_recovery_contract: ok - {summary}; {len(MUTATIONS)} single-reversal controls fail")


if __name__ == "__main__":
    main()
