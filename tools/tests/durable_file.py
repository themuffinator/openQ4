#!/usr/bin/env python3
"""Compile exact production I/O and exercise native files plus injected faults.

No engine launch. Keeps command/output/source-hash provenance under repository
.tmp. Cross-platform code execution is only claimed for the running platform;
the suite explicitly reports unsupported filesystem fixtures (e.g. DrvFS FIFO).
"""
from __future__ import annotations

import hashlib
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ROOT / "src/framework/DurableFile.h",
    ROOT / "src/framework/DurableFile.cpp",
    ROOT / "tools/tests/native/DurableFileTest.cpp",
    Path(__file__).resolve(),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    compiler = args.compiler or next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("A C++20 compiler is required")
    msvc = Path(compiler).name.lower() in ('cl', 'cl.exe')
    if args.sanitizers and (os.name == 'nt' or msvc):
        raise RuntimeError('This harness qualifies sanitizers with POSIX GCC/Clang')
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="durable-file-", dir=temporary))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    executable = scratch / ("DurableFileTest.exe" if os.name == "nt" else "DurableFileTest")
    production = scratch / ("DurableFile.obj" if os.name == "nt" else "DurableFile.o")
    macro_source = scratch / "macro_compile.cpp"
    # Engine PCH may already have included Windows.h before this standalone
    # source sets NOMINMAX. Exercise the production body with both legacy macros.
    macro_source.write_text("""#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
#include "src/framework/DurableFile.cpp"
""", encoding="utf-8")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror"]
    if args.sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    if os.name != "nt":
        flags.append("-pthread")
    def compile_command(source, output, *, object_only=False, overlay=None):
        includes = ([overlay] if overlay else []) + [ROOT]
        if msvc:
            return [compiler, '/nologo', '/std:c++20', '/EHsc', '/MTd', '/Od', '/Z7',
                    *['/I'+str(p) for p in includes],
                    *(['/c'] if object_only else []), str(source),
                    '/Fo'+str(output if object_only else output.with_suffix('.obj')),
                    *([] if object_only else ['/Fe'+str(output)])]
        return [compiler, *flags, *[item for p in includes for item in ('-I', str(p))],
                *(['-c'] if object_only else []), str(source), '-o', str(output)]
    commands = [
        compile_command(SOURCES[1], production, object_only=True),
        compile_command(macro_source, scratch / "macro_compile.o", object_only=True),
        compile_command(SOURCES[2], executable),
        [str(executable), str(scratch)],
    ]
    evidence = {
        "platform": platform.platform(),
        "scope": "Native exact-path I/O, collision-safe new-file publication and cooperating-process lease; no power-cut qualification",
        "sources": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in SOURCES},
        "commands": [],
        "passed": False,
        "mutations": [],
    }
    status = 0
    with (scratch / "test.log").open("w", encoding="utf-8") as log:
        for command in commands:
            log.write(json.dumps(command) + "\n")
            result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding="utf-8",
                                    errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=90, check=False)
            log.write(result.stdout + f"\nexit_code={result.returncode}\n")
            log.flush()
            print(result.stdout, end="")
            evidence["commands"].append({"args": command, "exit_code": result.returncode})
            if result.returncode:
                status = result.returncode
                break
        if status == 0 and args.mutations:
            original = SOURCES[1].read_text(encoding='utf-8')
            begin = original.index('DurableCreateResult Create(')
            end = original.index('\nbool Remove(', begin)
            body = original[begin:end]
            native_before = ('MoveFileExW(temp.name.c_str(),path.full.c_str(),MOVEFILE_WRITE_THROUGH)' if os.name == 'nt' else
                             'linkat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str(),0)')
            native_after = ('MoveFileExW(temp.name.c_str(),path.full.c_str(),MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING)' if os.name == 'nt' else
                            'renameat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str())')
            cases = {
                'overwrite-racing-creator': (native_before, native_after),
                'collision-as-created': ('if (!missing) { error.clear(); return DurableCreateResult::Exists; }',
                                         'if (!missing) { error.clear(); return DurableCreateResult::Created; }'),
                'omit-payload': ('!WriteAll(temp.file.value,bytes,error)', '(static_cast<void>(bytes), false)'),
                'omit-file-sync': ('!Sync(temp.file.value,Point::FileSync,error)', 'false'),
                'omit-close-check': ('!temp.file.Close(error,Point::CloseWrite)', '(temp.file.Close(error,Point::CloseWrite), false)'),
                'omit-native-failure': ('if (Fail(Point::CreatePublication))', 'if (false && Fail(Point::CreatePublication))'),
                'omit-namespace-sync': (('if (Fail(Point::MetadataSync))' if os.name == 'nt' else '!Sync(parent.value,Point::MetadataSync,error)'),
                                        ('if (false && Fail(Point::MetadataSync))' if os.name == 'nt' else 'false')),
            }
            for name, (before, after) in cases.items():
                assert body.count(before) == 1, (name, body.count(before))
                overlay = scratch / name
                changed = overlay / 'src/framework/DurableFile.cpp'
                changed.parent.mkdir(parents=True)
                changed.write_text(original[:begin] + body.replace(before, after) + original[end:], encoding='utf-8')
                shutil.copyfile(SOURCES[0], changed.with_suffix('.h'))
                binary = overlay / executable.name
                record = {'name': name, 'commands': [], 'expected_rejection': False}
                for step, command in [('compile', compile_command(SOURCES[2], binary, overlay=overlay)),
                                      ('run', [str(binary), str(overlay)])]:
                    result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding='utf-8',
                                            errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90, check=False)
                    log.write(json.dumps(command)+'\n'+result.stdout+f'\nexit_code={result.returncode}\n');log.flush()
                    record['commands'].append({'step': step, 'args': command, 'exit_code': result.returncode, 'output': result.stdout})
                    if step == 'compile' and result.returncode:
                        status = result.returncode;break
                    if step == 'run':
                        record['expected_rejection'] = result.returncode != 0 and 'FAIL:' in result.stdout
                        if not record['expected_rejection']:status = 1
                evidence['mutations'].append(record)
                print(name, 'rejected' if record['expected_rejection'] else 'FAILED')
                if status:break
    evidence['sources_after'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in SOURCES}
    evidence['sources_unchanged'] = evidence['sources'] == evidence['sources_after']
    if not evidence['sources_unchanged']: status = 1
    evidence["passed"] = status == 0
    evidence["log_sha256"] = hashlib.sha256((scratch / "test.log").read_bytes()).hexdigest()
    evidence['artifacts'] = {str(p.relative_to(scratch)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(scratch.rglob('*')) if p.is_file() and not p.is_symlink()}
    (scratch / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"Durable file test evidence: {scratch / 'result.json'}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
