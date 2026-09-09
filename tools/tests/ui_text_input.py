#!/usr/bin/env python3
"""Compile real UTF-8 codec and clipboard TUs with counted SDL doubles.

No live clipboard, input route, engine or GUI launch. Full translation units are
compiled, so source extraction cannot depend on checkout newline conventions.
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

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["src/ui/retained/TextInput.h", "src/ui/retained/TextInput.cpp",
           "src/sys/sdl3/TextClipboard.h", "src/sys/sdl3/TextClipboard.cpp",
           "tools/tests/native/UiTextInputTest.cpp", "tools/tests/native/SdlTextClipboardTest.cpp",
           "tools/tests/ui_text_input.py"]
SDL_DOUBLE = """#pragma once
#include <cstddef>
extern "C" {
bool SDL_IsMainThread();
bool SDL_ClearError();
char* SDL_GetClipboardText();
bool SDL_SetClipboardText(const char*);
void SDL_free(void*);
const char* SDL_GetError();
}
"""


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-include", type=Path, help="Optional real pinned SDL3 include directory for compile-only ABI verification")
    args = parser.parse_args()
    compiler = next((found for name in ("clang++", "g++", "c++") if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("A C++17 compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="text-input-", dir=temporary))
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    stub = scratch / "include" / "SDL3" / "SDL.h"
    stub.parent.mkdir(parents=True)
    stub.write_text(SDL_DOUBLE, encoding="utf-8", newline="\n")
    flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT)]
    executable_suffix = ".exe" if os.name == "nt" else ""
    codec = ROOT / SOURCES[1]
    clipboard = ROOT / SOURCES[3]
    commands: list[list[str]] = []
    codec_exe = scratch / ("UiTextInputTest" + executable_suffix)
    commands.append([compiler, *flags, str(codec), str(ROOT / SOURCES[4]), "-o", str(codec_exe)])
    commands.append([str(codec_exe)])
    for name, definitions in (("sdl-client", ["-DUSE_SDL3", "-DOPENQ4_SDL3_CHECKED_CLIPBOARD=1"]),
                              ("unverified-sdl", ["-DUSE_SDL3"]), ("unverified-sdl-zero", ["-DUSE_SDL3", "-DOPENQ4_SDL3_CHECKED_CLIPBOARD=0"]),
                              ("no-sdl", []), ("dedicated", ["-DUSE_SDL3", "-DID_DEDICATED", "-DOPENQ4_SDL3_CHECKED_CLIPBOARD=1"])):
        executable = scratch / (name + executable_suffix)
        commands.append([compiler, *flags, *definitions, "-I", str(stub.parents[1]), str(codec), str(clipboard),
                         str(ROOT / SOURCES[5]), "-o", str(executable)])
        commands.append([str(executable)])
    if args.sdl_include:
        if not (args.sdl_include / "SDL3" / "SDL.h").is_file():
            raise RuntimeError("Real SDL include directory does not contain SDL3/SDL.h")
        commands.append([compiler, *flags, "-DUSE_SDL3", "-DOPENQ4_SDL3_CHECKED_CLIPBOARD=1", "-I", str(args.sdl_include), "-c", str(clipboard),
                         "-o", str(scratch / "real-sdl-header.o")])
    source_hashes = {name: sha(ROOT / name) for name in SOURCES}
    evidence = {"passed": False, "platform": platform.platform(), "sources": source_hashes,
                "scope": "Pure scalar transport + production clipboard TU with counted SDL doubles; no live clipboard, owner, editor, shaping or IME route",
                "stub_sha256": sha(stub), "commands": []}
    if args.sdl_include:
        evidence["real_sdl_headers"] = {str(path): sha(path) for path in sorted((args.sdl_include / "SDL3").rglob("*.h"))}
    status = 0
    log_path = scratch / "test.log"
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        for command in commands:
            log.write(json.dumps(command) + "\n")
            result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding="utf-8", errors="replace",
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90, check=False)
            log.write(result.stdout + f"\nexit_code={result.returncode}\n")
            log.flush()
            print(result.stdout, end="")
            evidence["commands"].append({"args": command, "exit_code": result.returncode})
            if result.returncode:
                status = result.returncode
                break
    evidence["sources_unchanged"] = source_hashes == {name: sha(ROOT / name) for name in SOURCES}
    evidence["passed"] = status == 0 and evidence["sources_unchanged"]
    evidence["log_sha256"] = sha(log_path)
    report = scratch / "result.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Text input evidence: {report}")
    return 0 if evidence["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
