#!/usr/bin/env python3
"""Test pinned patched SDL clipboard method bodies with counted platform doubles.

Copies two source files into a temporary miniature checkout, applies the actual
package patch, then compiles exact method bodies. SDL/Windows calls are doubles;
no clipboard or game operation occurs. Never modifies the supplied SDL tree.
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
FILES = ["src/video/windows/SDL_windowsclipboard.c", "src/video/SDL_clipboard.c"]
PATCH = ROOT / "subprojects/packagefiles/sdl3/clipboard-status.patch"
SUPPORT = ROOT / "tools/tests/native/SdlClipboardStatusTest.cpp"
WRAP = ROOT / "subprojects/sdl3.wrap"
LEXER = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def provision_source(explicit: Path | None, specification: dict[str, str], scratch: Path) -> tuple[Path, dict, list[Path]]:
    """Use an existing tree, or read only the tested members from the pinned tar.

    CI runs this test before Meson provisions subprojects. Never extract an
    archive wholesale, and never silently replace an explicitly selected tree.
    """
    directory = specification["directory"]
    filename = specification["source_filename"]
    expected_hash = specification["source_hash"]
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", directory) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", filename):
        raise RuntimeError("SDL archive directory and filename must be plain names")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
        raise RuntimeError("SDL source_hash must be a SHA-256 digest")
    selected = explicit if explicit is not None else ROOT / "subprojects" / directory
    if selected.exists() or explicit is not None:
        if not selected.is_dir():
            raise RuntimeError(f"SDL source directory is missing or invalid: {selected}")
        for name in [*FILES, "LICENSE.txt"]:
            if not (selected / name).is_file():
                raise RuntimeError(f"Required SDL source file is missing: {selected / name}")
        return selected, {"kind": "explicit-tree" if explicit is not None else "default-tree", "path": str(selected)}, []

    cached = ROOT / "subprojects/packagecache" / filename
    archive = cached if cached.exists() else scratch / filename
    origin = "packagecache" if cached.exists() else "download"
    if origin == "download":
        url = specification["source_url"]
        if not url.startswith("https://"):
            raise RuntimeError("Pinned SDL source download must use HTTPS")
        with urllib.request.urlopen(url, timeout=30) as response, archive.open("wb") as output:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > 128 * 1024 * 1024:
                    raise RuntimeError("Pinned SDL archive exceeds the download bound")
                output.write(chunk)
    if not archive.is_file() or archive.stat().st_size > 128 * 1024 * 1024 or sha(archive) != expected_hash:
        raise RuntimeError("Pinned SDL archive SHA-256 does not match source_hash")

    members = {f"{directory}/{name}": name for name in [*FILES, "LICENSE.txt"]}
    contents: dict[str, bytes] = {}
    with tarfile.open(archive, "r:gz") as package:
        for member in package:
            if member.name not in members:
                continue
            name = members[member.name]
            if name in contents or not member.isfile() or member.size < 0 or member.size > 4 * 1024 * 1024:
                raise RuntimeError(f"Pinned SDL member is duplicated, non-regular or oversized: {member.name}")
            stream = package.extractfile(member)
            if stream is None:
                raise RuntimeError(f"Cannot read pinned SDL member: {member.name}")
            with stream:
                data = stream.read(member.size + 1)
            if len(data) != member.size:
                raise RuntimeError(f"Pinned SDL member size mismatch: {member.name}")
            contents[name] = data
    if set(contents) != set(members.values()):
        raise RuntimeError("Pinned SDL archive is missing required source or licence members")
    selected = scratch / "source"
    for name, data in contents.items():
        target = selected / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return selected, {"kind": origin, "archive": str(archive), "archive_sha256": expected_hash,
                      "source_url": specification["source_url"],
                      "members": {f"{directory}/{name}": hashlib.sha256(data).hexdigest() for name, data in contents.items()}}, [archive]


def body(source: str, signature: str) -> str:
    if source.count(signature) != 1:
        raise AssertionError(f"Production method is not unique: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for token in LEXER.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise AssertionError(f"Unterminated production method: {signature}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-source", type=Path, help="Existing SDL source tree; an explicit missing tree is an error")
    args = parser.parse_args()
    compiler = next((found for name in ("clang++", "g++", "c++") if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("A C++17 compiler is required")
    wrap = configparser.ConfigParser()
    wrap.read(WRAP, encoding="utf-8")
    if wrap["wrap-file"]["directory"] != "SDL3-3.4.10" or "sdl3/clipboard-status.patch" not in wrap["wrap-file"].get("diff_files", "").split(", "):
        raise RuntimeError("The tested pinned SDL clipboard patch must be registered")
    if b"\r" in PATCH.read_bytes():
        raise RuntimeError("Package patch must remain LF for archive application")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="sdl-clipboard-status-", dir=temporary))
    staged = scratch / "sdl"
    staged.mkdir()
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    source, provenance, archives = provision_source(args.sdl_source, dict(wrap["wrap-file"]), scratch)
    paths = [*map(lambda p: source / p, FILES), source / "LICENSE.txt", *archives, WRAP, PATCH, SUPPORT, Path(__file__).resolve()]
    before = {str(path): sha(path) for path in paths}
    evidence = {"passed": False, "platform": platform.platform(), "sources": before, "source_provision": provenance, "commands": [],
                "scope": "Actual patched SDL method bodies with counted Windows/SDL boundary doubles; no native clipboard, power failure or full SDL lifecycle claim"}
    log_path = scratch / "test.log"
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        def execute(command: list[str], cwd: Path = scratch) -> subprocess.CompletedProcess[str]:
            result = subprocess.run(command, cwd=cwd, env=env, text=True, encoding="utf-8", errors="replace",
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90, check=False)
            log.write(json.dumps(command) + "\n" + result.stdout + f"\nexit_code={result.returncode}\n")
            log.flush()
            evidence["commands"].append({"args": command, "exit_code": result.returncode})
            return result
        try:
            for name in FILES:
                target = staged / name
                target.parent.mkdir(parents=True, exist_ok=True)
                # Hash original bytes above; normalize only the temporary archive
                # projection and extracted C++ bodies, never the supplied source.
                target.write_text((source / name).read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
            if execute(["git", "init", "-q"], staged).returncode:
                raise RuntimeError("Cannot create isolated patch validation checkout")
            applicable = execute(["git", "apply", "--check", str(PATCH)], staged).returncode == 0
            if not applicable:
                if execute(["git", "apply", "--reverse", "--check", str(PATCH)], staged).returncode:
                    raise RuntimeError("Source matches neither original nor exactly patched clipboard methods")
                if execute(["git", "apply", "--reverse", str(PATCH)], staged).returncode:
                    raise RuntimeError("Cannot reconstruct original temporary patch inputs")
            evidence["source_was_already_patched"] = not applicable
            if execute(["git", "apply", "--check", str(PATCH)], staged).returncode or execute(["git", "apply", str(PATCH)], staged).returncode:
                raise RuntimeError("Fresh package patch application failed")
            windows, shared = [(staged / name).read_text(encoding="utf-8") for name in FILES]
            selections = [(windows, "static BOOL WIN_OpenClipboard("), (windows, "static void WIN_CloseClipboard("),
                          (windows, "static bool WIN_SetClipboardText("), (windows, "bool WIN_SetClipboardData("),
                          (windows, "void *WIN_GetClipboardData("), (windows, "bool WIN_HasClipboardData("),
                          (shared, "const void * SDLCALL SDL_ClipboardTextCallback("),
                          (shared, "bool SDL_SetClipboardText("), (shared, "char *SDL_GetClipboardText(")]
            methods = "\n\n".join(body(source, signature) for source, signature in selections)
            evidence["methods_sha256"] = hashlib.sha256(methods.encode()).hexdigest()
            support = SUPPORT.read_text(encoding="utf-8")
            marker = "// PRODUCTION_METHODS"
            if support.count(marker) != 1:
                raise RuntimeError("Production insertion marker is not unique")
            combined = support.replace(marker, methods)
            mutations = [
                ("retry-error", '    WIN_SetError("Couldn\'t open clipboard");', "    (void)0;"),
                ("retry-error-identity", "    SetLastError(last_error);", "    SetLastError(0);\n    (void)last_error;"),
                ("lock-false-success", '                result = WIN_SetError("Couldn\'t lock clipboard data");', "                result = true;"),
                ("transfer-leak", '                result = WIN_SetError("Couldn\'t set clipboard data");\n                GlobalFree(hMem);',
                 '                result = WIN_SetError("Couldn\'t set clipboard data");'),
                ("empty-false-success", "        if (!EmptyClipboard()) {", "        if ((EmptyClipboard(), false)) {"),
                ("copy-false-success", "        if (!copy) {", "        if (false) {"),
                ("read-null-fallback", "        *size = text ? SDL_strlen(text) : 0;", "        *size = SDL_strlen(text);"),
                ("read-raw-ansi", "                hMem = GetClipboardData(CF_UNICODETEXT);", "                hMem = GetClipboardData(CF_TEXT);"),
                ("read-conversion-false-success", '                            SDL_SetError("Couldn\'t convert clipboard text to UTF-8");', "                            (void)0;"),
                ("primary-error-overwritten", "        // WIN_OpenClipboard preserved the failed OpenClipboard error.\n        result = false;",
                 '        result = WIN_SetError("Couldn\'t open clipboard");'),
            ]
            cases = [("production", combined)]
            for name, old, new in mutations:
                if methods.count(old) != 1:
                    raise RuntimeError(f"Mutation anchor not unique: {name}")
                cases.append((name, support.replace(marker, methods.replace(old, new))))
            for name, source in cases:
                cpp = scratch / (name + ".cpp")
                cpp.write_text(source, encoding="utf-8", newline="\n")
                binary = scratch / (name + (".exe" if os.name == "nt" else ""))
                # SDL's callback ABI intentionally has unused parameters.
                compiled = execute([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", str(cpp), "-o", str(binary)])
                if compiled.returncode:
                    raise RuntimeError(f"{name} compilation failed: {compiled.stdout}")
                ran = execute([str(binary)])
                print(f"{name}: {ran.stdout.strip()}")
                if name == "production" and ran.returncode:
                    raise RuntimeError("Production clipboard methods failed")
                if name != "production" and (ran.returncode == 0 or "FAIL:" not in ran.stdout):
                    raise RuntimeError(f"{name} mutation was not rejected by a behavioral assertion")
            evidence["mutation_count"] = len(mutations)
            evidence["passed"] = True
        except Exception as failure:
            evidence["failure"] = str(failure)
            print(failure)
    evidence["sources_unchanged"] = before == {str(path): sha(path) for path in paths}
    evidence["passed"] &= evidence["sources_unchanged"]
    evidence["log_sha256"] = sha(log_path)
    report = scratch / "result.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"SDL clipboard status evidence: {report}")
    return 0 if evidence["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
