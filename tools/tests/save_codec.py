#!/usr/bin/env python3
"""Compile the portable savegame codecs and check their golden bytes (Q4-06 stage 2).

Builds tools/tests/native/SaveCodecTest.cpp against src/idlib/SaveCodec.h with the
given compiler and runs it: every codec's bytes must equal the fixed hex in the
test on this platform. Mutation controls then compile the test against altered
copies of the header (field order, flag bit order, bool width, removed bounds
checks) and require each to fail, so the golden checks are shown to bite.
No engine launch; scratch output stays under the repository .tmp.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/idlib/SaveCodec.h"
TEST = ROOT / "tools/tests/native/SaveCodecTest.cpp"
TIMEOUT = 600

# (name, exact text in the header, replacement). Each must change behaviour the
# test pins; the replacement text must still compile.
MUTATIONS = [
    ("flag bit order", "( ( flags[ i ] ? 1u : 0u ) << ( i - base ) )",
     "( ( flags[ i ] ? 1u : 0u ) << ( 7 - ( i - base ) ) )"),
    ("flag reader bit order", "flags[ i ] = ( ( value >> ( i - base ) ) & 1u ) != 0;",
     "flags[ i ] = ( ( value >> ( 7 - ( i - base ) ) ) & 1u ) != 0;"),
    ("trace model header order", "\tw.WriteInt( static_cast<int>( trm.type ) );\n\tw.WriteInt( trm.numVerts );",
     "\tw.WriteInt( trm.numVerts );\n\tw.WriteInt( static_cast<int>( trm.type ) );"),
    ("trace model bool as int", "\tw.WriteBool( trm.isConvex );", "\tw.WriteInt( trm.isConvex ? 1 : 0 );"),
    ("count bound removed", "if ( count < 0 || count > maximum ) {", "if ( count < 0 ) {"),
    ("interpolator drops deceleration", "\tw.WriteFloat( lerp.GetDeceleration() );\n", ""),
    ("joint modifier mode order",
     "\tw.WriteInt( static_cast<int>( mod.transform_pos ) );\n\tw.WriteInt( static_cast<int>( mod.transform_axis ) );",
     "\tw.WriteInt( static_cast<int>( mod.transform_axis ) );\n\tw.WriteInt( static_cast<int>( mod.transform_pos ) );"),
    ("path target field order", "\tw.WriteFloat( target.minDistance );\n\tw.WriteFloat( target.throttle );",
     "\tw.WriteFloat( target.throttle );\n\tw.WriteFloat( target.minDistance );"),
]


def run(command: list[str], env: dict) -> tuple[int, str]:
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT, check=False)
        return result.returncode, result.stdout
    except subprocess.TimeoutExpired as error:
        return 124, str(error)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--no-mutations", action="store_true")
    args = parser.parse_args()
    compiler = args.compiler or next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("A C++20 compiler is required")
    msvc = Path(compiler).name.lower() in ("cl", "cl.exe")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="save-codec-", dir=ROOT / ".tmp"))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))

    def build(output: Path, overlay: Path | None) -> list[str]:
        includes = ([overlay] if overlay else []) + [ROOT]
        if msvc:
            return [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/wd4100", "/MTd", "/Od",
                    *["/I" + str(p) for p in includes], str(TEST),
                    "/Fo" + str(output.with_suffix(".obj")), "/Fe" + str(output)]
        return [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                *[item for p in includes for item in ("-I", str(p))], str(TEST), "-o", str(output)]

    suffix = ".exe" if os.name == "nt" else ""
    executable = scratch / ("SaveCodecTest" + suffix)
    code, output = run(build(executable, None), env)
    print(output, end="")
    if code:
        print(f"save_codec: compile failed ({code})")
        return code
    code, output = run([str(executable)], env)
    print(output, end="")
    if code:
        print(f"save_codec: golden test failed ({code})")
        return code

    if not args.no_mutations:
        original = HEADER.read_text(encoding="utf-8")
        survivors = []
        for index, (name, before, after) in enumerate(MUTATIONS):
            if original.count(before) != 1:
                print(f"save_codec: mutation '{name}' no longer matches the header")
                return 1
            overlay = scratch / f"overlay{index}"
            target = overlay / "src/idlib/SaveCodec.h"
            target.parent.mkdir(parents=True)
            target.write_text(original.replace(before, after), encoding="utf-8")
            mutant = scratch / f"mutation{index}{suffix}"
            code, output = run(build(mutant, overlay), env)
            if code:
                print(output, end="")
                print(f"save_codec: mutation '{name}' did not compile")
                return 1
            code, _ = run([str(mutant)], env)
            if code == 124:
                print(f"save_codec: mutation '{name}' timed out; no failure was observed")
                return 1
            if code == 0:
                survivors.append(name)
            print(f"mutation '{name}': {'SURVIVED' if code == 0 else 'caught'}")
        if survivors:
            print("save_codec: golden checks missed " + ", ".join(survivors))
            return 1
    shutil.rmtree(scratch, ignore_errors=True)
    print(f"save_codec: ok ({'no mutations' if args.no_mutations else f'{len(MUTATIONS)} mutations caught'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
