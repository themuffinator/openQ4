#!/usr/bin/env python3
"""Run a focused clang-tidy gate over openQ4 input-safety code.

Meson's Windows compilation database describes the MSVC build accurately, but
clang-tidy cannot consume MSVC's binary PCH or its Windows-1252 charset flags.
This tool writes a narrow, temporary compilation database that preserves the
real production include paths, defines, language mode, and other compile flags
while replacing only those incompatible PCH/charset arguments.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parents[2]
PRODUCTION_SOURCES = (
    ROOT / "src" / "framework" / "UsercmdGen.cpp",
    ROOT / "src" / "idlib" / "Lexer.cpp",
    ROOT / "src" / "idlib" / "Base64.cpp",
    ROOT / "src" / "renderer" / "ModelDecal.cpp",
    ROOT / "src" / "ui" / "SimpleWindow.cpp",
)
PRODUCTION_HEADERS = (
    ROOT / "src" / "idlib" / "NumericString.h",
    ROOT / "src" / "idlib" / "Token.h",
)
SAFETY_TEST_SOURCE = ROOT / "tools" / "tests" / "native" / "CoreSafetyTest.cpp"
DEFAULT_OUTPUT_DIR = ROOT / ".tmp" / "clang-tidy-input-safety"
MSVC_DRIVER_MODE = "--driver-mode=cl"
MSVC_STYLE_FLAG_PREFIXES = (
    "/nologo",
    "/eh",
    "/zc:",
    "/permissive-",
    "/std:c++",
    "/showincludes",
)

CHECKS = (
    "-*",
    "clang-analyzer-core.*",
    "clang-analyzer-security.*",
    "clang-analyzer-cplusplus.NewDelete*",
    "clang-analyzer-cplusplus.PlacementNew",
    "clang-analyzer-optin.cplusplus.UninitializedObject",
    "clang-analyzer-unix.Malloc",
    "clang-analyzer-unix.MismatchedDeallocator",
    "clang-analyzer-deadcode.DeadStores",
)
REQUIRED_ENABLED_CHECKS = (
    "clang-analyzer-core.CallAndMessage",
    "clang-analyzer-security.insecureAPI.strcpy",
    "clang-analyzer-cplusplus.NewDelete",
    "clang-analyzer-cplusplus.PlacementNew",
    "clang-analyzer-optin.cplusplus.UninitializedObject",
    "clang-analyzer-unix.Malloc",
    "clang-analyzer-unix.MismatchedDeallocator",
    "clang-analyzer-deadcode.DeadStores",
)
HEADER_FILTER = r".*[\\/]src[\\/]idlib[\\/](?:NumericString|Token)[.]h$"


class AnalysisError(RuntimeError):
    """A deterministic input or toolchain failure in the analysis lane."""


def _windows_command_line_to_argv(command: str) -> list[str]:
    """Use Windows' command-line parser so quoted MSVC paths stay intact."""

    command_line_to_argv = ctypes.windll.shell32.CommandLineToArgvW
    command_line_to_argv.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    command_line_to_argv.restype = ctypes.POINTER(ctypes.c_wchar_p)
    local_free = ctypes.windll.kernel32.LocalFree
    local_free.argtypes = [ctypes.c_void_p]
    local_free.restype = ctypes.c_void_p

    argument_count = ctypes.c_int()
    argument_array = command_line_to_argv(command, ctypes.byref(argument_count))
    if not argument_array:
        raise AnalysisError("could not parse the Windows compilation command")
    try:
        return [argument_array[index] for index in range(argument_count.value)]
    finally:
        local_free(argument_array)


def compile_arguments(entry: dict[str, Any]) -> list[str]:
    """Return a compilation-database entry as an argument vector."""

    arguments = entry.get("arguments")
    if isinstance(arguments, list) and arguments and all(isinstance(value, str) for value in arguments):
        return list(arguments)

    command = entry.get("command")
    if not isinstance(command, str) or not command.strip():
        raise AnalysisError("compilation database entry has neither arguments nor a command")
    if os.name == "nt":
        return _windows_command_line_to_argv(command)
    return shlex.split(command)


def resolve_entry_source(entry: dict[str, Any]) -> Path:
    directory = entry.get("directory")
    source = entry.get("file")
    if not isinstance(directory, str) or not isinstance(source, str):
        raise AnalysisError("compilation database entry is missing its directory or file")
    source_path = Path(source)
    if not source_path.is_absolute():
        source_path = Path(directory) / source_path
    return source_path.resolve()


def choose_production_entry(database: Sequence[dict[str, Any]], source: Path) -> dict[str, Any]:
    """Select one deterministic client command when Meson lists many targets."""

    source = source.resolve()
    matching = [entry for entry in database if resolve_entry_source(entry) == source]
    if not matching:
        raise AnalysisError(f"no compilation database entry found for {source}")

    def priority(entry: dict[str, Any]) -> tuple[int, str]:
        arguments = compile_arguments(entry)
        joined = " ".join(arguments).lower()
        if "openq4-client" in joined:
            rank = 0
        elif "openq4-ded" in joined:
            rank = 1
        else:
            rank = 2
        return rank, joined

    return min(matching, key=priority)


def _is_msvc_driver(arguments: Sequence[str]) -> bool:
    if not arguments:
        return False
    executable = Path(arguments[0].strip('"')).name.lower()
    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        return True
    # Fall back to the flags themselves. A compiler launcher (ccache/sccache)
    # or any driver spelling we do not recognise would otherwise drop us into
    # the gcc branch, which silently leaves the MSVC PCH/charset flags in place
    # and clang-tidy in the wrong driver mode - the command then fails with
    # "no such file or directory: '/EHsc'" instead of analysing anything.
    # These prefixes have no POSIX-path meaning, so they cannot false-positive
    # on a Unix compile command.
    return any(argument.lower().startswith(MSVC_STYLE_FLAG_PREFIXES) for argument in arguments[1:])


def _with_msvc_driver_mode(arguments: list[str]) -> list[str]:
    """clang-tidy only understands MSVC '/' flags in cl driver mode.

    Meson records the driver as a bare 'cl', which clang's tooling does not map
    to cl mode on its own, so without this every '/EHsc'-style flag is parsed as
    an input path ("no such file or directory: '/EHsc'") and the forced include
    of precompiled.h never happens.
    """

    if not arguments or not _is_msvc_driver(arguments):
        return arguments
    if any(argument.startswith("--driver-mode=") for argument in arguments):
        return arguments
    return [arguments[0], MSVC_DRIVER_MODE, *arguments[1:]]


def sanitize_compile_arguments(arguments: Sequence[str], precompiled_header: Path) -> list[str]:
    """Remove only clang-incompatible PCH/charset state from real build flags."""

    if not arguments:
        raise AnalysisError("production compilation command is empty")

    msvc = _is_msvc_driver(arguments)
    forced_header = str(precompiled_header.resolve())
    sanitized: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        lowered = argument.lower()

        if msvc:
            # Object/PDB/exe outputs name a single artifact, so they are
            # rejected outright once clang-tidy adds its own input file
            # ("cannot specify '/Fo...' when compiling multiple source
            # files"). This is the MSVC spelling of the -o stripping clang's
            # own tooling already does for gcc-style commands.
            if lowered.startswith(("/fo", "/fd", "/fe")):
                index += 1
                continue
            if lowered == "/yu":
                index += 1
                continue
            if lowered == "/fp":
                if index + 1 >= len(arguments):
                    raise AnalysisError("MSVC PCH-output flag is missing its path")
                index += 2
                continue
            if lowered in {"/source-charset", "/execution-charset"}:
                index += 1
                continue
            if lowered.startswith(("/yu", "/fp", "/source-charset:", "/execution-charset:")):
                index += 1
                continue
            if lowered == "/fi":
                if index + 1 >= len(arguments):
                    raise AnalysisError("MSVC forced-include flag is missing its header")
                included = arguments[index + 1]
                if Path(included).name.lower() == "precompiled.h":
                    sanitized.extend(("/FI", forced_header))
                else:
                    sanitized.extend((argument, included))
                index += 2
                continue
            if lowered.startswith("/fi") and Path(argument[3:]).name.lower() == "precompiled.h":
                sanitized.append(f"/FI{forced_header}")
                index += 1
                continue
        else:
            if argument in {"-fpch-preprocess", "-Winvalid-pch"}:
                index += 1
                continue
            if lowered == "-include-pch":
                if index + 1 >= len(arguments):
                    raise AnalysisError("PCH include flag is missing its path")
                index += 2
                continue
            if lowered == "-include":
                if index + 1 >= len(arguments):
                    raise AnalysisError("forced-include flag is missing its header")
                included = arguments[index + 1]
                if Path(included).name.lower() == "precompiled.h":
                    sanitized.extend((argument, forced_header))
                else:
                    sanitized.extend((argument, included))
                index += 2
                continue

        sanitized.append(argument)
        index += 1

    return _with_msvc_driver_mode(sanitized)


def safety_test_arguments(production_arguments: Sequence[str], source: Path, root: Path) -> list[str]:
    """Build a minimal companion TU command using the production driver mode."""

    if not production_arguments:
        raise AnalysisError("cannot derive the safety-test command without a compiler driver")
    compiler = production_arguments[0]
    if _is_msvc_driver(production_arguments):
        return _with_msvc_driver_mode([
            compiler,
            f"/I{root.resolve()}",
            "/std:c++17",
            "/EHsc",
            "/permissive-",
            "/c",
            str(source.resolve()),
        ])
    return [
        compiler,
        f"-I{root.resolve()}",
        "-std=c++17",
        "-c",
        str(source.resolve()),
    ]


def build_analysis_database(build_dir: Path, root: Path = ROOT) -> list[dict[str, Any]]:
    database_path = build_dir / "compile_commands.json"
    try:
        raw_database = json.loads(database_path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError as exc:
        raise AnalysisError(f"compilation database not found: {database_path}") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise AnalysisError(f"could not read compilation database {database_path}: {exc}") from exc
    if not isinstance(raw_database, list) or not all(isinstance(entry, dict) for entry in raw_database):
        raise AnalysisError(f"compilation database must contain a JSON array: {database_path}")

    production_sources = tuple(root / source.relative_to(ROOT) for source in PRODUCTION_SOURCES)
    precompiled_header = root / "src" / "idlib" / "precompiled.h"
    safety_source = root / "tools" / "tests" / "native" / "CoreSafetyTest.cpp"
    analysis_database: list[dict[str, Any]] = []
    primary_arguments: list[str] | None = None
    primary_directory = ""
    for production_source in production_sources:
        production_entry = choose_production_entry(raw_database, production_source)
        production_arguments = sanitize_compile_arguments(compile_arguments(production_entry), precompiled_header)
        directory = str(Path(production_entry["directory"]).resolve())
        analysis_database.append(
            {
                "directory": directory,
                "arguments": production_arguments,
                "file": str(production_source.resolve()),
            }
        )
        if primary_arguments is None:
            primary_arguments = production_arguments
            primary_directory = directory

    if primary_arguments is None:
        raise AnalysisError("input-safety analysis has no production translation units")
    analysis_database.append(
        {
            "directory": primary_directory,
            "arguments": safety_test_arguments(primary_arguments, safety_source, root),
            "file": str(safety_source.resolve()),
        }
    )
    return analysis_database


def validate_output_dir(output_dir: Path, root: Path = ROOT) -> Path:
    """Keep generated analysis state inside the repository's ignored .tmp tree."""

    temporary_root = (root / ".tmp").resolve()
    if output_dir.is_symlink():
        raise AnalysisError(f"analysis output directory must not be a symlink: {output_dir}")
    if output_dir.exists() and not output_dir.is_dir():
        raise AnalysisError(f"analysis output path must be a directory: {output_dir}")
    resolved = output_dir.resolve()
    try:
        resolved.relative_to(temporary_root)
    except ValueError as exc:
        raise AnalysisError(f"analysis output directory must be under {temporary_root}: {resolved}") from exc
    return resolved


def write_analysis_database(database: Sequence[dict[str, Any]], output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    database_path = output_dir / "compile_commands.json"
    temporary_path = output_dir / "compile_commands.json.tmp"
    if temporary_path.is_symlink():
        raise AnalysisError(f"temporary analysis database must not be a symlink: {temporary_path}")
    temporary_path.write_text(json.dumps(list(database), indent=2) + "\n", encoding="utf-8")
    temporary_path.replace(database_path)
    return database_path


def find_clang_tidy(requested: str) -> str:
    if requested:
        candidate = Path(requested)
        if candidate.parent != Path(".") or candidate.is_absolute():
            if not candidate.is_file():
                raise AnalysisError(f"clang-tidy executable not found: {candidate}")
            return str(candidate.resolve())
        located = shutil.which(requested)
        if located:
            return located
        raise AnalysisError(f"clang-tidy executable not found on PATH: {requested}")

    located = shutil.which("clang-tidy")
    if located:
        return located
    if os.name == "nt":
        llvm_candidate = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang-tidy.exe"
        if llvm_candidate.is_file():
            return str(llvm_candidate)
    raise AnalysisError("clang-tidy was not found; install LLVM or pass --clang-tidy")


def clang_tidy_command(
    executable: str,
    database_dir: Path,
    root: Path = ROOT,
    msvc: bool = False,
) -> list[str]:
    production_sources = tuple(root / source.relative_to(ROOT) for source in PRODUCTION_SOURCES)
    safety_test_source = root / "tools" / "tests" / "native" / "CoreSafetyTest.cpp"
    command = [
        executable,
        *(str(source.resolve()) for source in production_sources),
        str(safety_test_source.resolve()),
        "-p",
        str(database_dir.resolve()),
    ]
    if msvc:
        # Also force the driver mode on the command line. The database entries
        # already carry it, but this keeps the gate correct even if clang-tidy
        # resolves a command from somewhere other than our sanitized database.
        command.append(f"--extra-arg-before={MSVC_DRIVER_MODE}")
        # clang-tidy parses with its own clang, whose version may trail the
        # MSVC STL's floor (STL1000: MSVC 14.52 wants Clang 22 while the CI
        # image ships LLVM 20). The STL's escape hatch keeps the gate on our
        # code; it changes nothing when the versions already match.
        command.append("--extra-arg=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
    command += [
        f"--checks={','.join(CHECKS)}",
        "--warnings-as-errors=*",
        f"--header-filter={HEADER_FILTER}",
        "--quiet",
    ]
    return command


def validate_enabled_checks(executable: str) -> None:
    """Fail when an older clang-tidy would silently ignore part of the gate."""

    try:
        result = subprocess.run(
            [executable, "--list-checks", f"--checks={','.join(CHECKS)}"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        raise AnalysisError(f"could not launch clang-tidy: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise AnalysisError(f"could not query enabled clang-tidy checks: {detail}")
    enabled = {line.strip() for line in result.stdout.splitlines() if line.strip().startswith("clang-")}
    missing = [check for check in REQUIRED_ENABLED_CHECKS if check not in enabled]
    if missing:
        raise AnalysisError(f"clang-tidy is missing required checks: {', '.join(missing)}")


def display_command(arguments: Sequence[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(arguments)
    return shlex.join(arguments)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "builddir",
        help="Meson build directory containing compile_commands.json (default: builddir)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="temporary sanitized database directory; must remain under .tmp",
    )
    parser.add_argument("--clang-tidy", default="", help="clang-tidy executable or command name")
    parser.add_argument("--dry-run", action="store_true", help="write the analysis database and print the command only")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        for required in (*PRODUCTION_SOURCES, *PRODUCTION_HEADERS, SAFETY_TEST_SOURCE):
            if not required.is_file():
                raise AnalysisError(f"required input-safety source is missing: {required}")

        build_dir = args.build_dir.resolve()
        output_dir = validate_output_dir(args.output_dir)
        database = build_analysis_database(build_dir)
        write_analysis_database(database, output_dir)

        msvc = bool(database) and _is_msvc_driver(database[0]["arguments"])

        executable = (args.clang_tidy or "clang-tidy") if args.dry_run else find_clang_tidy(args.clang_tidy)
        command = clang_tidy_command(executable, output_dir, msvc=msvc)
        if args.dry_run:
            print(display_command(command))
            return 0

        validate_enabled_checks(executable)
        try:
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
        except OSError as exc:
            raise AnalysisError(f"could not launch clang-tidy: {exc}") from exc
        if result.returncode != 0:
            # Echo what we actually asked for. A failure here is usually the
            # analysis lane misreading the build's compile command rather than
            # a real finding, and without this the log shows only the fallout.
            print(f"driver: {'msvc' if msvc else 'gcc'}", file=sys.stderr)
            print(f"command: {display_command(command)}", file=sys.stderr)
            for entry in database:
                print(f"entry {entry['file']}:", file=sys.stderr)
                print(f"  {display_command(entry['arguments'])}", file=sys.stderr)
            if result.stdout:
                print(result.stdout, end="", file=sys.stdout)
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
            return result.returncode
        print("clang-tidy input-safety analysis: ok")
        return 0
    except AnalysisError as exc:
        print(f"clang-tidy input-safety analysis: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
