#!/usr/bin/env python3
"""Actual platform/pushed queue storage with private disposition sidecars.

Compiles production Windows/POSIX queue methods and the full EventLoop against
the existing counted allocators/files. No platform input, engine or provider is
activated. Temporary source projections normalize newlines; hashes bind originals.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import sys_event_queue_ownership as queues
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["src/sys/EventDisposition.h", "src/sys/EventDisposition.cpp",
    "src/sys/EventQueueContinuity.h", "src/sys/EventQueueContinuity.cpp", "src/sys/KeyEventMetadata.h",
    "src/sys/sys_public.h", "src/sys/win32/win_main.cpp", "src/sys/posix/posix_main.cpp",
    "src/framework/EventLoop.h", "src/framework/EventLoop.cpp",
    "tools/tests/native/EventJournalTest.cpp", "tools/tests/native/EventDispositionPlatformTest.cpp",
    "tools/tests/native/EventDispositionPushedTest.cpp", "tools/tests/event_disposition_storage.py",
    "tools/tests/sys_event_queue_ownership.py", "tools/tests/filesystem_case_segments.py"]

SOURCES += ['src/sys/EventRetirement.h', 'src/framework/NativeInputRoute.h', 'src/framework/NativeInputRoute.cpp', 'src/ui/retained/TextInputBroker.h', 'src/ui/retained/TextInput.h', 'src/ui/retained/NativeTextDocument.h']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def replace(source, old, new, count=1):
    if source.count(old) != count:
        raise RuntimeError(f"Mutation anchor count {source.count(old)} != {count}: {old!r}")
    return source.replace(old, new)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--msvc-debug", action="store_true")
    parser.add_argument("--no-mutations", action="store_true")
    args = parser.parse_args()
    compiler = args.compiler or next((v for n in ("clang++", "g++", "c++") if (v := shutil.which(n))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    msvc = Path(compiler).name.lower() in ("cl", "cl.exe")
    if args.msvc_debug and not msvc:
        raise RuntimeError("--msvc-debug requires cl.exe")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="event-disposition-storage-", dir=ROOT / ".tmp"))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    if args.sanitizers:
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    before = {p: sha(ROOT / p) for p in SOURCES}
    evidence = {"passed": False, "platform": platform.platform(), "sources": before, "cases": [],
        "scope": "Actual platform/pushed storage, counted ownership and unchanged legacy journal ABI; no native activation, concurrent legacy producer, retirement replay, terminal delivery or OS input qualification"}
    queues.ROOT = ROOT
    service = (ROOT / "src/sys/EventDisposition.cpp").read_text(encoding="utf-8")
    loop = (ROOT / "src/framework/EventLoop.cpp").read_text(encoding="utf-8")
    journal = (ROOT / "tools/tests/native/EventJournalTest.cpp").read_text(encoding="utf-8")
    journal = replace(journal, "int main() {", "static void LegacyJournalMain() {")
    units = {}
    for target in ("windows", "posix"):
        path, body, _ = queues.production(target)
        source = (ROOT / path).read_text(encoding="utf-8")
        body = replace(body, "int main() {", "static void LegacyQueueMain() {")
        body += "\n" + function_body(source, "bool Sys_QueTrackedEvent(")
        body += "\n" + function_body(source, "sysEventTransfer_t Sys_TakeEventWithDisposition(")
        body += '\n#include "src/sys/EventDisposition.cpp"\n#include "tools/tests/native/EventDispositionPlatformTest.cpp"\n'
        units[target] = body
    units["pushed"] = journal + '\n#include "tools/tests/native/EventDispositionPushedTest.cpp"\n'
    # Each alternate compiles a changed production body and must fail a behavioral
    # assertion, never compilation/sanitizer failure alone.
    mutations = []
    if not args.no_mutations:
        mutations = [
            ("epoch-unchecked", "windows", "service", "tag.dispatchEpoch == Sys_EventDispositionEpoch()", "tag.dispatchEpoch != 0", 1),
            ("continuity-unchecked", "windows", "service", "tag.streamToken == Sys_EventQueueToken()", "tag.streamToken != 0", 1),
            ("epoch-reused", "windows", "service", "dispositionEpoch = 0; return true;", "dispositionEpoch = 0; dispositionHighwater = 0; return true;", 1),
            ("worker-epoch", "windows", "service", "dispositionBound && dispositionThread == std::this_thread::get_id() ? dispositionEpoch : 0", "dispositionBound ? dispositionEpoch : 0", 1),
            ("platform-tag-lost", "windows", "unit", "eventDispositionTags[slot] = tag;", "eventDispositionTags[slot] = {};", 1),
            ("platform-legacy-bypass", "posix", "unit", "if (!eventDispositionTags[eventTail & MASK_QUED_EVENTS].Empty())", "if (false && !eventDispositionTags[eventTail & MASK_QUED_EVENTS].Empty())", 1),
            ("platform-stale-take", "windows", "unit", "if (!ownedTag.Empty() && !Sys_EventDispositionTagCurrent(ownedTag))", "if (false && !ownedTag.Empty() && !Sys_EventDispositionTagCurrent(ownedTag))", 1),
            ("platform-full-ownership", "posix", "unit", "return false; // No eviction, ownership transfer or callback on failed admission.", "event = {}; tag = {}; return false; // Deliberate ownership loss.", 1),
            ("platform-stale-slot", "windows", "unit", "eventQue[slot] = {}; eventDispositionTags[slot] = {};", "eventQue[slot] = {};", 1),
            ("pushed-tag-lost", "pushed", "loop", "com_pushedDisposition[slot] = tag;", "com_pushedDisposition[slot] = {};", 1),
            ("pushed-legacy-bypass", "pushed", "loop", "if ( !com_pushedDisposition[com_pushedEventsTail & (MAX_PUSHED_EVENTS-1)].Empty() )", "if ( false && !com_pushedDisposition[com_pushedEventsTail & (MAX_PUSHED_EVENTS-1)].Empty() )", 1),
            ("pushed-stale-take", "pushed", "loop", "if ( !ownedTag.Empty() && !Sys_EventDispositionTagCurrent( ownedTag ) )", "if ( false && !ownedTag.Empty() && !Sys_EventDispositionTagCurrent( ownedTag ) )", 1),
            ("pushed-journal-admission", "pushed", "loop", "!Sys_EventDispositionTagCurrent( tag ) || com_journal.GetInteger() != 0 ||", "!Sys_EventDispositionTagCurrent( tag ) ||", 1),
            ("pushed-cleanup-lost", "pushed", "loop", "\tClearPushedEvents();", "\t// Deliberate pending cleanup omission.", 2),
            ("pushed-full-ownership", "pushed", "loop", "return false; // Preserve all queued ownership and both caller inputs.", "event = {}; tag = {}; return false; // Deliberate ownership loss.", 1),
        ]
    cases = [(target, target, None, None, None, 0) for target in units] + mutations
    log_path = scratch / "test.log"
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        def run(command):
            result = subprocess.run(command, cwd=scratch, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=120, check=False)
            log.write(json.dumps(command) + "\n" + result.stdout + f"\nexit_code={result.returncode}\n")
            log.flush()
            return result
        try:
            for name, target, part, old, new, count in cases:
                variant = scratch / name
                variant.mkdir()
                texts = {"unit": units[target], "service": service, "loop": loop}
                if part:
                    texts[part] = replace(texts[part], old, new, count)
                for relative in ["src/framework/EventLoop.h", "src/sys/EventDisposition.h", "src/sys/EventQueueContinuity.h", "src/sys/KeyEventMetadata.h"] + ['src/sys/EventRetirement.h', 'src/framework/NativeInputRoute.h', 'src/framework/NativeInputRoute.cpp', 'src/ui/retained/TextInputBroker.h', 'src/ui/retained/TextInput.h', 'src/ui/retained/NativeTextDocument.h']:
                    destination = variant / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / relative, destination)
                for relative, text in (("src/sys/EventDisposition.cpp", texts["service"]), ("src/framework/EventLoop.cpp", texts["loop"]), ("unit.cpp", texts["unit"]),
                    ("JournalEventContract.h", "#pragma once\n" + queues.event_types())):
                    destination = variant / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_text(text, encoding="utf-8", newline="\n")
                output = variant / ("test.exe" if os.name == "nt" else "test")
                sources = [str(variant / "unit.cpp"), str(ROOT / "src/sys/EventQueueContinuity.cpp"), str(ROOT / "src/framework/NativeInputRoute.cpp")]
                if target == "pushed":
                    sources.append(str(variant / "src/sys/EventDisposition.cpp"))
                if msvc:
                    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/wd4100", "/MTd" if args.msvc_debug else "/MT",
                        "/Od" if args.msvc_debug else "/O2", "/I" + str(variant), "/I" + str(ROOT), *sources,
                        "/Fo" + str(variant) + os.sep, "/Fe" + str(output)]
                else:
                    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"] if args.sanitizers else []
                    if os.name != "nt":
                        flags.append("-pthread")
                    command = [compiler, "-std=c++20", "-O1" if args.sanitizers else "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", *flags,
                        "-I", str(variant), "-I", str(ROOT), *sources, "-o", str(output)]
                compiled = run(command)
                if compiled.returncode:
                    raise RuntimeError(name + " compilation failed:\n" + compiled.stdout)
                result = run([str(output)])
                print(name + ": " + result.stdout.strip(), flush=True)
                evidence["cases"].append({"name": name, "target": target, "command": command, "exit_code": result.returncode,
                    "output": result.stdout, "binary_sha256": sha(output), "projection_sha256": sha(variant / "unit.cpp")})
                if not part and result.returncode:
                    raise RuntimeError(name + " production failure")
                if part and (result.returncode == 0 or "FAIL:" not in result.stdout):
                    raise RuntimeError(name + " did not fail a behavioral assertion")
            evidence["passed"] = True
            evidence["mutation_count"] = len(mutations)
        except Exception as error:
            evidence["failure"] = str(error)
            print(error, flush=True)
    evidence["sources_unchanged"] = before == {p: sha(ROOT / p) for p in SOURCES}
    evidence["passed"] &= evidence["sources_unchanged"]
    evidence["log_sha256"] = sha(log_path)
    (scratch / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Storage evidence: " + str(scratch / "result.json"), flush=True)
    return 0 if evidence["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
