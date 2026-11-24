#!/usr/bin/env python3
"""Run a deterministic, asset-free Windows dedicated-server startup smoke."""

from __future__ import annotations

import argparse
import ctypes
import json
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
READY_MARKER = "OPENQ4_WINDOWS_DEDICATED_SMOKE_READY"
IPV4_SELF_TEST_MARKER = "IPv4 network self-test: passed"
CANONICAL_GAMETYPE_MARKER = '"si_gameType" is:"DM"'
LOG_FILE_NAME = "windows-dedicated-smoke.log"
REQUIRED_MARKERS = (
    "Selected game module: logical='game_mp'",
    "------------- Initializing Game -------------",
    "game initialized.",
    IPV4_SELF_TEST_MARKER,
    CANONICAL_GAMETYPE_MARKER,
    READY_MARKER,
    "--- Common Initialization Complete ---",
    "Type 'help' for dedicated server info.",
    "--------------- Game Shutdown ---------------",
)
FATAL_MARKERS = (
    "ERROR:",
    # common->FatalError logs this prefix before Sys_Error parks the server.
    "FATAL:",
    "Error during initialization",
    "IPv4 network self-test: FAILED",
    "couldn't load game dynamic library",
    "couldn't find game DLL API",
    "wrong game DLL API version",
)

# IMAGE_FILE_HEADER.Machine for each architecture openQ4 packages on Windows.
PE_MACHINES = {
    "x64": 0x8664,
    "arm64": 0xAA64,
}

# The server inherits this error mode, so a missing or wrong-architecture DLL
# ends the process instead of raising a loader dialog that waits for a click.
SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002
SEM_NOOPENFILEERRORBOX = 0x8000

# The Windows server does not exit on a fatal error or a crash: Sys_Error keeps
# its console window open and the crash handler shows a message box, and both
# wait for a user. Once either shows up the run has failed, so collect output
# for a moment longer and stop it rather than sitting out the whole timeout.
BLOCKED_GRACE_SECONDS = 3.0
POLL_SECONDS = 0.2

NTSTATUS_NAMES = {
    0xC0000005: "STATUS_ACCESS_VIOLATION",
    0xC000001D: "STATUS_ILLEGAL_INSTRUCTION",
    0xC000007B: "STATUS_INVALID_IMAGE_FORMAT",
    0xC00000FD: "STATUS_STACK_OVERFLOW",
    0xC0000135: "STATUS_DLL_NOT_FOUND",
    0xC0000139: "STATUS_ENTRYPOINT_NOT_FOUND",
    0xC0000142: "STATUS_DLL_INIT_FAILED",
    0xC0000409: "STATUS_STACK_BUFFER_OVERRUN",
}


def pe_machine_name(machine: int) -> str:
    for arch, value in PE_MACHINES.items():
        if value == machine:
            return arch
    return f"0x{machine:04x}"


def read_pe_machine(path: Path) -> int:
    with path.open("rb") as handle:
        dos_header = handle.read(64)
        if len(dos_header) < 64 or dos_header[:2] != b"MZ":
            raise RuntimeError(f"not a PE image (no MZ header): {path}")
        handle.seek(struct.unpack_from("<I", dos_header, 0x3C)[0])
        nt_header = handle.read(6)
    if len(nt_header) < 6 or nt_header[:4] != b"PE\0\0":
        raise RuntimeError(f"not a PE image (no PE signature): {path}")
    return struct.unpack_from("<H", nt_header, 4)[0]


def require_pe_arch(path: Path, arch: str, label: str) -> str:
    machine = read_pe_machine(path)
    if machine != PE_MACHINES[arch]:
        raise RuntimeError(f"{label} is a {pe_machine_name(machine)} image, expected {arch}: {path}")
    return pe_machine_name(machine)


def native_host_arch() -> str:
    """Return the machine Windows runs on, even when this Python is emulated."""
    kernel32 = ctypes.WinDLL("kernel32")
    is_wow64_process2 = getattr(kernel32, "IsWow64Process2", None)
    if is_wow64_process2 is not None:
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        is_wow64_process2.argtypes = (
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ushort),
            ctypes.POINTER(ctypes.c_ushort),
        )
        is_wow64_process2.restype = ctypes.c_int
        process_machine = ctypes.c_ushort()
        native_machine = ctypes.c_ushort()
        if is_wow64_process2(
            kernel32.GetCurrentProcess(),
            ctypes.byref(process_machine),
            ctypes.byref(native_machine),
        ):
            arch = pe_machine_name(native_machine.value)
            if arch not in PE_MACHINES:
                raise RuntimeError(f"unsupported Windows smoke host machine: {arch}")
            return arch

    machine = platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        return "x64"
    if machine in {"arm64", "aarch64"}:
        return "arm64"
    raise RuntimeError(f"unsupported Windows smoke host architecture: {machine or '<empty>'}")


def inherit_quiet_error_mode() -> None:
    ctypes.WinDLL("kernel32").SetErrorMode(
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX
    )


def describe_exit_code(exit_code: int | None) -> str:
    if exit_code is None:
        return "none"
    status = exit_code & 0xFFFFFFFF
    if status < 0x80000000:
        return str(exit_code)
    name = NTSTATUS_NAMES.get(status)
    return f"0x{status:08X}" + (f" ({name})" if name else "")


def create_minimal_base(base_path: Path) -> Path:
    q4base = base_path / "q4base"
    q4base.mkdir(parents=True, exist_ok=False)
    pak_path = q4base / "pak001.pk4"
    with zipfile.ZipFile(pak_path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr(
            "openq4-windows-dedicated-smoke.txt",
            "Generated test-only media marker; contains no proprietary Quake 4 data.\n",
        )
    return pak_path


def read_text(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def find_log(home_path: Path) -> Path | None:
    candidates = sorted(home_path.rglob(LOG_FILE_NAME))
    return candidates[0] if candidates else None


def snapshot_crash_files(crash_dir: Path) -> dict[str, int]:
    if not crash_dir.is_dir():
        return {}
    return {path.name: path.stat().st_mtime_ns for path in crash_dir.iterdir() if path.is_file()}


def new_crash_files(crash_dir: Path, before: dict[str, int]) -> list[Path]:
    if not crash_dir.is_dir():
        return []
    return sorted(
        path
        for path in crash_dir.iterdir()
        if path.is_file() and before.get(path.name) != path.stat().st_mtime_ns
    )


def wait_for_server(
    process: subprocess.Popen,
    home_path: Path,
    crash_dir: Path,
    crashes_before: dict[str, int],
    deadline: float,
) -> tuple[int | None, bool, str]:
    """Wait for the server to exit; return (exit code, timed out, blocked reason)."""
    blocked_by = ""
    blocked_at = 0.0
    while True:
        exit_code = process.poll()
        if exit_code is not None:
            return exit_code, False, blocked_by
        now = time.monotonic()
        if blocked_by and now - blocked_at >= BLOCKED_GRACE_SECONDS:
            return None, False, blocked_by
        if now >= deadline:
            return None, True, blocked_by
        if not blocked_by:
            # The crash handler writes its .log only after the minidump is
            # complete, so waiting for it never cuts a dump short.
            crash_files = new_crash_files(crash_dir, crashes_before)
            if any(path.suffix.lower() == ".log" for path in crash_files):
                blocked_by = "the crash handler wrote a crash report"
            else:
                log_text = read_text(find_log(home_path))
                fatal = [marker for marker in FATAL_MARKERS if marker in log_text]
                if fatal:
                    blocked_by = f"the log recorded {fatal[0]!r}"
            if blocked_by:
                blocked_at = now
        time.sleep(POLL_SECONDS)


def tail(text: str, line_count: int = 80) -> str:
    return "\n".join(text.splitlines()[-line_count:])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install-root",
        type=Path,
        default=ROOT / ".install",
        help="Staged package root containing the Windows dedicated server and baseoq4 module.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / ".tmp" / "windows-dedicated-smoke",
        help="Parent directory for isolated runtime homes and diagnostic reports.",
    )
    parser.add_argument("--executable", type=Path, default=None, help="Override the staged dedicated-server executable.")
    parser.add_argument(
        "--arch",
        choices=tuple(PE_MACHINES),
        default=None,
        help="Architecture of the staged binaries (default: the host's native architecture).",
    )
    parser.add_argument("--timeout", type=float, default=60.0, help="Maximum runtime in seconds.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if sys.platform != "win32":
        raise RuntimeError("windows_dedicated_server_smoke must run natively on Windows")
    if args.timeout <= 0:
        raise RuntimeError("--timeout must be greater than zero")

    host_arch = native_host_arch()
    arch = args.arch or host_arch
    if arch == "arm64" and host_arch != "arm64":
        raise RuntimeError(f"arm64 binaries cannot start on this {host_arch} host")
    install_root = args.install_root.resolve()
    executable = (args.executable or install_root / f"openQ4-ded_{arch}.exe").resolve()
    game_module = install_root / "baseoq4" / f"game_{arch}.dll"
    # The server imports OpenAL32.dll directly, so the loader needs a matching
    # one beside the executable before any engine code runs.
    openal_runtime = executable.parent / "OpenAL32.dll"

    for path, label in (
        (executable, "dedicated-server executable"),
        (game_module, "multiplayer game module"),
        (openal_runtime, "OpenAL runtime"),
    ):
        if path.is_symlink():
            raise RuntimeError(f"{label} must not be a symlink: {path}")
        if not path.is_file():
            raise RuntimeError(f"{label} not found: {path}")
    pe_architectures = {
        "executable": require_pe_arch(executable, arch, "dedicated-server executable"),
        "gameModule": require_pe_arch(game_module, arch, "multiplayer game module"),
        "openal": require_pe_arch(openal_runtime, arch, "OpenAL runtime"),
    }

    packaged_q4base = install_root / "q4base"
    if packaged_q4base.is_dir() and any(path.is_file() for path in packaged_q4base.rglob("*")):
        raise RuntimeError(
            f"refusing assetless smoke because the staged package contains q4base files: {packaged_q4base}"
        )

    args.output_root.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix=f"{arch}-", dir=args.output_root.resolve()))
    base_path = run_dir / "minimal-base"
    home_path = run_dir / "home"
    home_path.mkdir()
    minimal_pak = create_minimal_base(base_path)
    stdout_path = run_dir / "stdout.txt"
    stderr_path = run_dir / "stderr.txt"
    report_path = run_dir / "report.json"
    crash_dir = executable.parent / "crashes"

    command = [
        str(executable),
        "+set", "fs_basepath", str(base_path),
        "+set", "fs_homepath", str(home_path),
        "+set", "fs_savepath", str(home_path),
        "+set", "fs_devpath", str(install_root),
        "+set", "fs_game", "baseoq4",
        "+set", "fs_validateOfficialPaks", "0",
        "+set", "g_allowAssetlessStartup", "1",
        "+set", "si_gameType", "dm",
        "+set", "s_noSound", "1",
        "+set", "net_serverDedicated", "1",
        "+set", "logFile", "2",
        "+set", "logFileName", f"logs/{LOG_FILE_NAME}",
        "+netIPv4SelfTest",
        "+si_gameType",
        "+echo", READY_MARKER,
        "+com_activeGameModule",
        "+wait", "1",
        "+quit",
    ]

    inherit_quiet_error_mode()
    crashes_before = snapshot_crash_files(crash_dir)
    started = time.monotonic()
    # The server is a GUI-subsystem program that prints to its own console
    # window, so the log file carries the markers; stdout and stderr are kept
    # for loader and runtime messages.
    with stdout_path.open("wb") as stdout_handle, stderr_path.open("wb") as stderr_handle:
        process = subprocess.Popen(
            command,
            cwd=install_root,
            stdin=subprocess.DEVNULL,
            stdout=stdout_handle,
            stderr=stderr_handle,
        )
        try:
            exit_code, timed_out, blocked_by = wait_for_server(
                process,
                home_path,
                crash_dir,
                crashes_before,
                started + args.timeout,
            )
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
    elapsed = time.monotonic() - started

    crash_files = []
    for crash_file in new_crash_files(crash_dir, crashes_before):
        copied = run_dir / "crashes" / crash_file.name
        copied.parent.mkdir(exist_ok=True)
        shutil.copy2(crash_file, copied)
        crash_files.append(str(copied))

    stdout = read_text(stdout_path)
    stderr = read_text(stderr_path)
    log_path = find_log(home_path)
    log_text = read_text(log_path)
    diagnostic_text = "\n".join(part for part in (stdout, stderr, log_text) if part)
    missing = [marker for marker in REQUIRED_MARKERS if marker not in diagnostic_text]
    fatal = [marker for marker in FATAL_MARKERS if marker in diagnostic_text]
    passed = (
        exit_code == 0
        and not timed_out
        and not blocked_by
        and log_path is not None
        and not missing
        and not fatal
        and not crash_files
    )

    report = {
        "status": "pass" if passed else "fail",
        "architecture": arch,
        "hostArchitecture": host_arch,
        "pythonArchitecture": platform.machine(),
        "emulated": arch != host_arch,
        "peArchitectures": pe_architectures,
        "executable": str(executable),
        "gameModule": str(game_module),
        "openalRuntime": str(openal_runtime),
        "minimalPak": str(minimal_pak),
        "log": str(log_path) if log_path else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "exitCode": exit_code,
        "exitStatus": describe_exit_code(exit_code),
        "timedOut": timed_out,
        "blockedBy": blocked_by,
        "crashFiles": crash_files,
        "elapsedSeconds": round(elapsed, 3),
        "missingMarkers": missing,
        "fatalMarkers": fatal,
        "command": command,
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"windows_dedicated_server_smoke: {report['status']} ({arch} on {host_arch} host, {elapsed:.2f}s)")
    print(f"  report: {report_path}")
    if not passed:
        print(
            f"  exit={describe_exit_code(exit_code)} timedOut={int(timed_out)} "
            f"logFound={int(log_path is not None)}"
        )
        if blocked_by:
            print(f"  stopped because {blocked_by}")
        if crash_files:
            print("  crash files:")
            for crash_file in crash_files:
                print(f"    - {crash_file}")
        if missing:
            print("  missing markers:")
            for marker in missing:
                print(f"    - {marker}")
        if fatal:
            print("  fatal markers:")
            for marker in fatal:
                print(f"    - {marker}")
        if diagnostic_text:
            print("  diagnostic tail:")
            for line in tail(diagnostic_text).splitlines():
                print(f"    {line}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
