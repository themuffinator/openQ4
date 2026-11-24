#!/usr/bin/env python3
"""Deterministic fuzz-style checks for Linux package metadata parsers."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import random
import shutil
import string
import subprocess
import sys
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORK_BASE = ROOT / ".tmp" / "linux-metadata-fuzz"
WORK = WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"
STAGE_SCRIPT = ROOT / "tools" / "build" / "stage_gamelibs.py"
MANIFEST_NAME = "openq4_gamelibs_stage_manifest.json"


def load_module(module_name: str, path: Path):
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module spec for {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VALIDATOR = load_module("openq4_validate_under_test", ROOT / "tools" / "validation" / "openq4_validate.py")
PACKAGER = load_module("package_nightly_under_test", ROOT / "tools" / "build" / "package_nightly.py")
SHARED_METADATA = load_module("linux_metadata_under_test", ROOT / "tools" / "build" / "linux_metadata.py")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def staged_exec_line(content: str) -> str:
    path = WORK / "desktop-staged.desktop"
    write_text(path, content)
    return VALIDATOR.desktop_entry_exec(path, ROOT)


def packaged_exec_line(content: str) -> str:
    path = WORK / "desktop-packaged.desktop"
    write_text(path, content)
    return PACKAGER.desktop_entry_exec(path)


def assert_raises(callback, label: str) -> None:
    try:
        callback()
    except (RuntimeError, ValueError, VALIDATOR.ValidationError):
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def validate_desktop_parser_cases() -> None:
    valid = (
        "\ufeff[Desktop Entry]\nName=openQ4\nExec=openQ4-client_x64 %f\n",
        "[Other]\nExec=wrong\n[Desktop Entry]\nExec=\"openQ4-client_x64\" --safe\n",
        "# comment\n[Desktop Entry]\n  Exec= openQ4-steamdeck  \n",
    )
    expected = ("openQ4-client_x64", "openQ4-client_x64", "openQ4-steamdeck")

    for content, expected_command in zip(valid, expected):
        staged_line = staged_exec_line(content)
        packaged_line = packaged_exec_line(content)
        if VALIDATOR.desktop_exec_command(staged_line) != expected_command:
            raise AssertionError(f"staged parser returned wrong command for {content!r}")
        if PACKAGER.desktop_exec_command(packaged_line) != expected_command:
            raise AssertionError(f"packaged parser returned wrong command for {content!r}")

    invalid_entries = (
        "[Desktop Entry]\nExec=openQ4-client_x64\nExec=openQ4-ded_x64\n",
        "[Desktop Entry]\nExec=\nExec=openQ4-client_x64\n",
        "[Desktop Entry]\nExec=openQ4-client_x64\x00--bad\n",
    )
    for content in invalid_entries:
        assert_raises(lambda content=content: staged_exec_line(content), f"staged desktop entry {content!r}")
        assert_raises(lambda content=content: packaged_exec_line(content), f"packaged desktop entry {content!r}")

    invalid_exec_lines = (
        '"openQ4-client_x64',
        "%f openQ4-client_x64",
        "openQ4-client_x64\x00--bad",
        "openQ4-client_x64\n--bad",
    )
    for exec_line in invalid_exec_lines:
        assert_raises(lambda exec_line=exec_line: VALIDATOR.desktop_exec_command(exec_line), "staged Exec parser")
        assert_raises(lambda exec_line=exec_line: PACKAGER.desktop_exec_command(exec_line), "packaged Exec parser")


def validate_desktop_parser_fuzz() -> None:
    rng = random.Random(0x04A11CE)
    alphabet = string.ascii_letters + string.digits + " _.-+@,;:%'\"\\/$[](){}"

    for index in range(300):
        payload = "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 80)))
        if index % 17 == 0:
            payload += '"'
        if index % 23 == 0:
            payload = "%f " + payload
        if index % 29 == 0:
            payload += "\x00"

        content = f"[Desktop Entry]\nName=openQ4\nExec={payload}\n"
        results: list[tuple[str, str]] = []
        for label, entry_parser, command_parser in (
            ("staged", staged_exec_line, VALIDATOR.desktop_exec_command),
            ("packaged", packaged_exec_line, PACKAGER.desktop_exec_command),
        ):
            try:
                command = command_parser(entry_parser(content))
            except (RuntimeError, ValueError, VALIDATOR.ValidationError):
                results.append((label, "error"))
            else:
                if any(char in command for char in "\x00\r\n\t"):
                    raise AssertionError(f"{label} parser accepted unsafe command token: {command!r}")
                results.append((label, command))

        if (results[0][1] == "error") != (results[1][1] == "error"):
            raise AssertionError(f"desktop parser disagreement for fuzz payload {payload!r}: {results}")
        if results[0][1] != "error" and results[0][1] != results[1][1]:
            raise AssertionError(f"desktop parser command mismatch for fuzz payload {payload!r}: {results}")


def random_component(rng: random.Random) -> str:
    alphabet = string.ascii_lowercase + string.digits + " _.-+@,()[]"
    middle = "".join(rng.choice(alphabet) for _ in range(rng.randint(1, 18))).strip(" .")
    if not middle:
        middle = "x"
    return "q" + middle + "4"


def validate_stage_manifest_fuzz() -> None:
    rng = random.Random(0x514A6E)
    project_root = WORK / "stage" / "openQ4"
    gamelibs_root = WORK / "stage" / "openQ4-game"
    stage_root = project_root / ".tmp" / "gamelibs_stage"

    write_text(gamelibs_root / "src" / "game" / "Game_local.cpp", "// canonical game\n")
    write_text(gamelibs_root / "src" / "game" / "Game_local.cpp", "// canonical multiplayer game\n")
    write_text(project_root / "src" / "idlib" / "idlib_public.h", "// idlib\n")
    write_text(project_root / "src" / "renderer" / "RenderWorld.h", "// renderer\n")

    for index in range(60):
        depth = rng.randint(1, 4)
        parts = [random_component(rng) for _ in range(depth)]
        suffix = ".cpp" if index % 3 else ".h"
        path = gamelibs_root / "src" / "game" / Path(*parts).with_suffix(suffix)
        write_text(path, f"// fuzz source {index}\n")

    result = subprocess.run(
        [sys.executable, str(STAGE_SCRIPT), str(project_root), str(gamelibs_root), str(stage_root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(f"stage_gamelibs.py failed fuzz staging: {result.stderr}")

    manifest_path = stage_root / MANIFEST_NAME
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files")
    if not isinstance(files, list) or manifest.get("fileCount") != len(files):
        raise AssertionError("fuzz stage manifest has an inconsistent file count")

    paths = [entry.get("path") for entry in files]
    if len(paths) != len(set(paths)):
        raise AssertionError("fuzz stage manifest contains duplicate paths")

    for entry in files:
        rel = entry.get("path")
        expected_hash = entry.get("sha256")
        if not isinstance(rel, str) or rel.startswith("/") or ".." in Path(rel).parts:
            raise AssertionError(f"fuzz stage manifest contains unsafe path: {rel!r}")
        staged_file = stage_root / rel
        if not staged_file.is_file():
            raise AssertionError(f"fuzz stage manifest references missing file: {rel!r}")
        if expected_hash != sha256(staged_file):
            raise AssertionError(f"fuzz stage manifest hash mismatch: {rel!r}")


def validate_source_contracts() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    packager = (ROOT / "tools" / "build" / "package_nightly.py").read_text(encoding="utf-8")
    shared = (ROOT / "tools" / "build" / "linux_metadata.py").read_text(encoding="utf-8")
    validation_runner = validator
    release_notes = (ROOT / "docs/dev" / "release-completion.md").read_text(encoding="utf-8")
    plan = (ROOT / "docs/dev/archive/2026-10-18" / "plans" / "2026-06-20-linux.md").read_text(encoding="utf-8")

    for token in (
        "more than one Exec key",
        "Exec line contains control data",
        "desktop-entry field code",
        "shlex.split(exec_line, posix=True)",
    ):
        if token not in shared:
            raise AssertionError(f"missing {token!r} in shared Linux metadata parser")
    if "except ValueError:\n        parts = exec_line.split()" in shared:
        raise AssertionError("shared Linux metadata parser still falls back to unsafe whitespace splitting")

    for source, context in ((validator, "staged validation"), (packager, "release packaging")):
        for token in (
            "LinuxMetadataError",
            "parse_linux_desktop_entry_exec",
            "parse_linux_desktop_exec_command",
        ):
            if token not in source:
                raise AssertionError(f"missing shared parser delegation token {token!r} in {context}")
        if "shlex.split(exec_line, posix=True)" in source:
            raise AssertionError(f"{context} still carries a local desktop Exec parser")

    for token in (
        "linux_metadata_fuzz.py",
        "tools/tests/linux_metadata_fuzz.py",
        "python tools/tests/linux_metadata_fuzz.py",
    ):
        if token not in validation_runner and token not in read_workflows():
            raise AssertionError(f"missing fuzz wiring token: {token}")

    if "Parser/staging fuzz smoke" not in plan:
        raise AssertionError("Linux audit status does not mention parser/staging fuzz smoke")
    if "deterministic fuzz smoke" not in release_notes:
        raise AssertionError("release notes do not mention deterministic fuzz smoke")


def read_workflows() -> str:
    return (
        (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
        + "\n"
        + (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    )


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_desktop_parser_cases()
        validate_desktop_parser_fuzz()
        validate_stage_manifest_fuzz()
        validate_source_contracts()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("linux_metadata_fuzz: ok")


if __name__ == "__main__":
    main()
