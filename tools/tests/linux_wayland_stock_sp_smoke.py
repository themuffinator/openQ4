#!/usr/bin/env python3
"""Validate packaged stock SP gameplay, audio initialization, and save/load on Wayland.

This is an opt-in native-Linux runtime test because it requires the retail
Quake 4 PK4s. It launches the staged SP client through a real Wayland socket,
enters ``game/airdefense1``, saves and restores an isolated slot through two
``g_autoExecAfterMapLoad`` configs, captures post-restore gameplay, and writes
an evidence bundle under ``.tmp`` (or an explicitly selected output root).

The automated audio check proves OpenAL and the engine sound system initialized
without errors. It cannot hear the speakers. Audible playback and physical
hardware are recorded only as explicit operator attestations.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import stat
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path
from typing import Any

from linux_physical_host_evidence import (
    collect_linux_host_evidence,
    reject_virtualized_physical_attestation,
)


ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA_VERSION = 1
REPORT_TYPE = "linux-wayland-stock-sp"
MAP_NAME = "game/airdefense1"
DEFAULT_SAVE_SLOT = "linux_wayland_roundtrip"
LOG_NAME = "linux-wayland-stock-sp.log"
SCREENSHOT_REL = "screenshots/linux-wayland-stock-sp/after-load.tga"
FIRST_CFG_REL = "linux_wayland_stock_sp/save_and_load.cfg"
RESTORED_CFG_REL = "linux_wayland_stock_sp/after_load.cfg"

FATAL_PATTERNS = {
    "error": re.compile(r"^(?:\^[0-9])?ERROR:", re.MULTILINE),
    "fatal": re.compile(r"Fatal Error|Error during initialization|Sys_Error:", re.IGNORECASE),
    "signal": re.compile(r"signal caught|segmentation fault", re.IGNORECASE),
    "gameModule": re.compile(
        r"couldn['’]t (?:load game dynamic library|find game DLL API)|wrong game DLL API version",
        re.IGNORECASE,
    ),
    "shader": re.compile(
        r"(?:shader compile|program link)[^\r\n]*(?:failed|error)|failed to compile",
        re.IGNORECASE,
    ),
}

GL_ERROR_PATTERNS = {
    "glError": re.compile(
        r"\bGL_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY|STACK_(?:OVERFLOW|UNDERFLOW)|CONTEXT_LOST)\b",
        re.IGNORECASE,
    ),
    "glInitialization": re.compile(
        r"could not initialize OpenGL|Unable to initialize OpenGL|failed to create (?:an )?OpenGL context",
        re.IGNORECASE,
    ),
    "framebuffer": re.compile(
        r"\bGL_FRAMEBUFFER_(?:INCOMPLETE[A-Z0-9_]*|UNSUPPORTED|UNDEFINED)\b"
        r"|\b(?:framebuffer|FBO)\b[^\r\n]{0,64}\b(?:incomplete|unsupported)\b",
        re.IGNORECASE,
    ),
}

AUDIO_ERROR_PATTERNS = {
    "openalLibrary": re.compile(
        r"(?:could not|couldn['’]t|failed to) (?:load|initialize)[^\r\n]*OpenAL",
        re.IGNORECASE,
    ),
    "openalDevice": re.compile(
        r"(?:OpenAL|alcOpenDevice)[^\r\n]*(?:failed|failure|unable to open|returned null)",
        re.IGNORECASE,
    ),
    "openalRuntime": re.compile(
        r"AL lib:\s*\(EE\)|\bALC?_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY)\b",
        re.IGNORECASE,
    ),
    "soundInitialization": re.compile(
        r"sound system[^\r\n]*(?:failed|not initialized)|could not initialize[^\r\n]*sound",
        re.IGNORECASE,
    ),
}


def host_arch_tag() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x64"
    if machine in {"aarch64", "arm64"}:
        return "arm64"
    raise RuntimeError(f"unsupported Linux gameplay host architecture: {machine or '<empty>'}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_commit(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    try:
        completed = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


def validate_native_elf(path: Path, arch: str) -> dict[str, Any]:
    with path.open("rb") as stream:
        header = stream.read(64)
    expected_machine = {"x64": 62, "arm64": 183}[arch]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise RuntimeError(f"packaged runtime is not an ELF file: {path}")
    if header[4] != 2 or header[5] != 1:
        raise RuntimeError(f"packaged runtime is not little-endian ELF64: {path}")
    machine = struct.unpack_from("<H", header, 18)[0]
    if machine != expected_machine:
        raise RuntimeError(
            f"packaged runtime architecture mismatch: {path} has ELF e_machine={machine}, "
            f"expected {expected_machine} for {arch}"
        )
    return {"class": "ELF64", "data": "little-endian", "machine": machine}


def append_set(command: list[str], name: str, value: Any) -> None:
    command.extend(("+set", name, str(value)))


def append_command(command: list[str], name: str, *values: Any) -> None:
    command.append("+" + name)
    command.extend(str(value) for value in values)


def read_text(path: Path | None) -> str:
    if path is None or not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def find_log(savepath: Path) -> Path | None:
    for candidate in (
        savepath / "baseoq4" / "logs" / LOG_NAME,
        savepath / "q4base" / "logs" / LOG_NAME,
        savepath / "logs" / LOG_NAME,
    ):
        if candidate.is_file():
            return candidate
    return None


def runtime_text(savepath: Path, stdout: Path, stderr: Path) -> str:
    return "\n".join(
        text
        for text in (
            read_text(find_log(savepath)),
            read_text(stdout),
            read_text(stderr),
        )
        if text
    )


def matching_patterns(text: str, patterns: dict[str, re.Pattern[str]]) -> list[str]:
    return [name for name, pattern in patterns.items() if pattern.search(text)]


def ordered_pattern_failures(
    text: str,
    patterns: tuple[tuple[str, re.Pattern[str]], ...],
) -> list[str]:
    failures: list[str] = []
    cursor = 0
    for label, pattern in patterns:
        match = pattern.search(text, cursor)
        if match is None:
            failures.append(label)
            continue
        cursor = match.end()
    return failures


def validate_nontrivial_tga(path: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {"status": "missing", "path": str(path) if path else ""}
    if path is None or not path.is_file():
        return result

    data = path.read_bytes()
    result.update({"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    if len(data) < 18:
        result["status"] = "invalid-header"
        return result

    id_length, color_map_type, image_type = data[0], data[1], data[2]
    width = struct.unpack_from("<H", data, 12)[0]
    height = struct.unpack_from("<H", data, 14)[0]
    bits = data[16]
    result.update({"width": width, "height": height, "bitsPerPixel": bits, "imageType": image_type})
    if color_map_type != 0 or image_type != 2 or bits not in (24, 32) or width < 320 or height < 200:
        result["status"] = "unsupported-or-too-small"
        return result

    pixel_size = bits // 8
    pixel_count = width * height
    start = 18 + id_length
    end = start + pixel_count * pixel_size
    if len(data) < end or len(data) < 65536:
        result["status"] = "truncated-or-tiny"
        return result

    step = max(1, pixel_count // 4096)
    colours: set[tuple[int, int, int]] = set()
    minimum = 255
    maximum = 0
    for pixel_index in range(0, pixel_count, step):
        offset = start + pixel_index * pixel_size
        b, g, r = data[offset], data[offset + 1], data[offset + 2]
        colours.add((r, g, b))
        minimum = min(minimum, r, g, b)
        maximum = max(maximum, r, g, b)
    result.update({"sampledUniqueColours": len(colours), "sampledChannelRange": maximum - minimum})
    result["status"] = "pass" if len(colours) >= 16 and maximum - minimum >= 12 else "flat-image"
    return result


def write_autoexec_configs(home: Path, save_slot: str, settle_frames: int) -> None:
    first_payload = "\n".join(
        (
            "echo OPENQ4_WAYLAND_SP_SAVE_REQUEST",
            f"saveGame {save_slot}",
            "echo OPENQ4_WAYLAND_SP_SAVE_RETURNED",
            f"set g_autoExecAfterMapLoad {RESTORED_CFG_REL}",
            "set g_autoExecAfterMapLoadDelayMs 1000",
            "echo OPENQ4_WAYLAND_SP_LOAD_REQUEST",
            f"loadGame {save_slot}",
            "",
        )
    )
    restored_payload = "\n".join(
        (
            "echo OPENQ4_WAYLAND_SP_RESTORE_ACTIVE",
            f"wait {max(1, settle_frames)}",
            "gfxInfo",
            f'echo OPENQ4_WAYLAND_SP_POST_RESTORE_CAPTURE "{MAP_NAME}"',
            f'screenshot "{SCREENSHOT_REL}"',
            "wait 5",
            "quit",
            "",
        )
    )
    for game_dir in ("baseoq4", "q4base"):
        first = home / game_dir / FIRST_CFG_REL
        restored = home / game_dir / RESTORED_CFG_REL
        first.parent.mkdir(parents=True, exist_ok=True)
        restored.parent.mkdir(parents=True, exist_ok=True)
        first.write_text(first_payload, encoding="utf-8")
        restored.write_text(restored_payload, encoding="utf-8")
        (home / game_dir / SCREENSHOT_REL).parent.mkdir(parents=True, exist_ok=True)


def find_screenshot(home: Path) -> Path | None:
    for game_dir in ("baseoq4", "q4base"):
        candidate = home / game_dir / SCREENSHOT_REL
        if candidate.is_file():
            return candidate
    candidate = home / SCREENSHOT_REL
    return candidate if candidate.is_file() else None


def validate_save_files(home: Path, save_slot: str) -> dict[str, Any]:
    minimum_bytes = {".save": 4096, ".tga": 65536, ".txt": 1}
    save_dir: Path | None = None
    files: dict[str, Path] = {}
    for game_dir in ("baseoq4", "q4base"):
        candidate_dir = home / game_dir / "savegames"
        candidate_files = {suffix: candidate_dir / f"{save_slot}{suffix}" for suffix in minimum_bytes}
        if all(path.is_file() for path in candidate_files.values()):
            save_dir = candidate_dir
            files = candidate_files
            break

    result: dict[str, Any] = {
        "status": "missing",
        "directory": str(save_dir) if save_dir else "",
        "files": {},
        "temporaryFiles": [],
    }
    if save_dir is None:
        return result

    valid = True
    for suffix, path in files.items():
        size = path.stat().st_size
        file_result = {
            "path": str(path),
            "bytes": size,
            "minimumBytes": minimum_bytes[suffix],
            "sha256": sha256_file(path),
            "valid": size >= minimum_bytes[suffix],
        }
        if suffix == ".save":
            data = path.read_bytes()
            integrity: dict[str, Any] = {"status": "invalid"}
            if len(data) >= 16:
                marker, trailer_version, protected_length, stored_crc = struct.unpack_from(
                    "<4I", data, len(data) - 16
                )
                expected_marker = int.from_bytes(b"OQ4I", "little")
                length_valid = protected_length == len(data) - 16
                calculated_crc = (
                    zlib.crc32(data[:protected_length]) & 0xFFFFFFFF
                    if 0 <= protected_length <= len(data) - 16
                    else None
                )
                integrity = {
                    "status": "pass"
                    if marker == expected_marker
                    and trailer_version == 1
                    and length_valid
                    and calculated_crc == stored_crc
                    else "invalid",
                    "marker": marker.to_bytes(4, "little").decode("ascii", errors="replace"),
                    "version": trailer_version,
                    "protectedBytes": protected_length,
                    "storedCrc32": f"{stored_crc:08x}",
                    "calculatedCrc32": f"{calculated_crc:08x}" if calculated_crc is not None else "",
                    "lengthValid": length_valid,
                }
            file_result["integrity"] = integrity
            file_result["valid"] = file_result["valid"] and integrity["status"] == "pass"
        result["files"][suffix.removeprefix(".")] = file_result
        valid = valid and file_result["valid"]
    temporary_files = sorted(str(path) for path in save_dir.glob(f"{save_slot}.*.tmp") if path.is_file())
    result["temporaryFiles"] = temporary_files
    result["status"] = "pass" if valid and not temporary_files else "invalid"
    return result


def expected_module_marker(game_module: Path, arch: str) -> str:
    return (
        f"Selected game module: logical='game_sp' binary='game_{arch}' "
        f"path='{game_module}'"
    )


def extract_active_audio_device(text: str) -> str:
    matches = re.findall(r"^OpenAL active device:\s*(.+?)\s*$", text, re.MULTILINE)
    return matches[-1] if matches else ""


def write_reports(run_dir: Path, report: dict[str, Any]) -> tuple[Path, Path]:
    json_path = run_dir / "report.json"
    markdown_path = run_dir / "report.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    audio = report["audioEvidence"]
    execution = report["executionEvidence"]
    lines = [
        "# Linux Wayland Stock SP Save/Load Validation",
        "",
        f"- Status: **{report['status']}**",
        f"- Architecture: `{report['architecture']}`",
        f"- Map / save slot: `{report['map']}` / `{report['saveSlot']}`",
        f"- Native process architecture: `{execution['nativeProcessArchitecture']}`",
        f"- Native Wayland: `{report['nativeWayland']}`",
        f"- Physical hardware attested by operator: `{execution['physicalHardwareAttested']}`",
        f"- VM/emulator indicators detected: `{execution['virtualMachineDetected']}`",
        f"- Container detected: `{execution['containerDetected']}`",
        f"- OpenAL + sound software initialized: `{audio['softwareInitializationPassed']}`",
        f"- Audible playback verified by a human: `{audio['humanAudiblePlaybackVerified']}`",
        f"- Post-restore screenshot: `{report['screenshot'].get('status', 'missing')}`",
        f"- Save-file set: `{report['saveFiles'].get('status', 'missing')}`",
        f"- Clean exit: `{report['cleanExit']}`",
        f"- Elapsed seconds: `{report['elapsedSeconds']}`",
        "",
        "> The automated harness verifies software audio initialization only. It never claims that sound was heard.",
        "",
        "## Evidence",
        "",
        f"- Engine log: `{report['paths']['log']}`",
        f"- stdout / stderr: `{report['paths']['stdout']}` / `{report['paths']['stderr']}`",
        f"- Screenshot: `{report['screenshot'].get('path', '')}`",
    ]
    if report["missingMarkers"]:
        lines.extend(("", "## Missing or failed checks", ""))
        lines.extend(f"- {item}" for item in report["missingMarkers"])
    for heading, key in (
        ("Fatal markers", "fatalMarkers"),
        ("OpenGL error markers", "glErrorMarkers"),
        ("Audio error markers", "audioErrorMarkers"),
    ):
        if report[key]:
            lines.extend(("", f"## {heading}", ""))
            lines.extend(f"- {item}" for item in report[key])
    markdown_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return json_path, markdown_path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, default=ROOT / ".install", help="Staged package root.")
    parser.add_argument(
        "--client-executable",
        type=Path,
        default=None,
        help="Override the packaged openQ4 SP client executable.",
    )
    parser.add_argument(
        "--basepath",
        type=Path,
        required=True,
        help="Quake 4 install root containing unmodified q4base retail PK4s.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / ".tmp" / "linux-wayland-stock-sp",
        help="Parent directory for isolated home, logs, saves, screenshot, and reports.",
    )
    parser.add_argument("--game-libs-repo", type=Path, default=None, help="Optional companion source repo for commit evidence.")
    parser.add_argument("--arch", choices=("x64", "arm64"), default=None, help="Override native host architecture detection.")
    parser.add_argument("--save-slot", default=DEFAULT_SAVE_SLOT, help="Isolated save slot (letters, digits, or '_' only).")
    parser.add_argument("--wayland-display", default=os.environ.get("WAYLAND_DISPLAY") or None, help="Native Wayland socket name.")
    parser.add_argument(
        "--xdg-runtime-dir",
        type=Path,
        default=Path(os.environ["XDG_RUNTIME_DIR"]) if os.environ.get("XDG_RUNTIME_DIR") else None,
        help="Directory containing the live Wayland socket.",
    )
    parser.add_argument("--timeout", type=float, default=900.0, help="Seconds allowed for map load, save/load, capture, and exit.")
    parser.add_argument("--settle-frames", type=int, default=60, help="Active post-restore frames before capture.")
    parser.add_argument(
        "--physical-hardware",
        action="store_true",
        help=(
            "Operator attestation that this run is on physical hardware; known VM/emulator "
            "indicators are inspected, recorded, and rejected."
        ),
    )
    parser.add_argument(
        "--human-audio-playback-verified",
        action="store_true",
        help="Operator attestation that audible playback was heard; automation never infers this.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if sys.platform != "linux":
        raise RuntimeError("linux_wayland_stock_sp_smoke must run natively on Linux")
    if args.timeout <= 0 or args.settle_frames <= 0:
        raise RuntimeError("--timeout and --settle-frames must be greater than zero")
    # The id command lexer treats '-' as punctuation when this value is
    # interpolated into the generated saveGame/loadGame config commands.
    if re.fullmatch(r"[A-Za-z0-9_]{1,64}", args.save_slot) is None:
        raise RuntimeError("--save-slot must contain 1-64 letters, digits, or '_' characters")

    native_arch = host_arch_tag()
    arch = args.arch or native_arch
    if arch != native_arch:
        raise RuntimeError(
            f"stock SP evidence must run natively: host is {native_arch}, but --arch requested {arch}"
        )
    host_evidence = collect_linux_host_evidence()
    if args.physical_hardware:
        reject_virtualized_physical_attestation(host_evidence)

    install_root = args.install_root.resolve()
    basepath = args.basepath.resolve()
    game_libs_repo = args.game_libs_repo.resolve() if args.game_libs_repo else None
    client = (args.client_executable or install_root / f"openQ4-client_{arch}").resolve()
    game_module = (install_root / "baseoq4" / f"game_{arch}.so").resolve()
    mod_manifest = install_root / "baseoq4" / "mod.json"
    openq4_pak0 = install_root / "baseoq4" / "pak0.pk4"
    openq4_pak1 = install_root / "baseoq4" / "pak1.pk4"
    retail_pak = (basepath / "q4base" / "pak001.pk4").resolve()
    for description, path in (
        ("packaged client", client),
        ("packaged SP game module", game_module),
        ("packaged openQ4 mod manifest", mod_manifest),
        ("packaged openQ4 pak0.pk4", openq4_pak0),
        ("packaged openQ4 pak1.pk4", openq4_pak1),
        ("retail Quake 4 pak001.pk4", retail_pak),
    ):
        if not path.is_file():
            raise RuntimeError(f"{description} not found: {path}")
    if not os.access(client, os.X_OK):
        raise RuntimeError(f"packaged client is not executable: {client}")

    packaged_q4base = install_root / "q4base"
    if packaged_q4base.is_dir() and any(path.is_file() for path in packaged_q4base.rglob("*")):
        raise RuntimeError(
            f"refusing stock SP evidence because the staged package contains q4base overrides: {packaged_q4base}"
        )

    elf_metadata = {
        "client": validate_native_elf(client, arch),
        "gameSp": validate_native_elf(game_module, arch),
    }

    if not args.wayland_display or Path(args.wayland_display).name != args.wayland_display:
        raise RuntimeError("--wayland-display must be a non-empty socket basename such as wayland-0")
    if args.xdg_runtime_dir is None:
        raise RuntimeError("--xdg-runtime-dir or XDG_RUNTIME_DIR is required")
    xdg_runtime_dir = args.xdg_runtime_dir.resolve()
    wayland_socket = xdg_runtime_dir / args.wayland_display
    try:
        socket_mode = wayland_socket.stat().st_mode
    except OSError as exc:
        raise RuntimeError(f"Wayland socket not found: {wayland_socket}") from exc
    if not stat.S_ISSOCK(socket_mode):
        raise RuntimeError(f"Wayland display path is not a live Unix socket: {wayland_socket}")

    args.output_root.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix=f"{arch}-", dir=args.output_root.resolve()))
    home = run_dir / "home"
    (home / ".config" / "openq4").mkdir(parents=True)
    (home / ".local" / "share" / "openq4").mkdir(parents=True)
    write_autoexec_configs(home, args.save_slot, args.settle_frames)

    stdout_path = run_dir / "stdout.txt"
    stderr_path = run_dir / "stderr.txt"
    command = [str(client)]
    for name, value in (
        ("sys_allowMultipleInstances", "1"),
        ("in_tty", "0"),
        ("logFile", "2"),
        ("logFileName", f"logs/{LOG_NAME}"),
        ("developer", "1"),
        ("fs_basepath", basepath),
        ("fs_homepath", home),
        ("fs_savepath", home),
        ("fs_devpath", install_root),
        ("fs_game", "baseoq4"),
        ("si_gameType", "singleplayer"),
        ("r_ignoreGLErrors", "0"),
        ("r_fullscreen", "0"),
        ("r_mode", "3"),
        ("r_multiSamples", "0"),
        ("r_postAA", "0"),
        ("r_shadows", "0"),
        ("r_glTier", "legacy"),
        ("r_renderer", "best"),
        ("com_machineSpec", "0"),
        ("com_maxfps", "60"),
        ("com_skipLoadingContinue", "1"),
        ("com_loadingContinueAutoAdvance", "1"),
        ("g_autoSkipCinematics", "1"),
        ("s_noSound", "0"),
        ("g_autoExecAfterMapLoad", FIRST_CFG_REL),
        ("g_autoExecAfterMapLoadDelayMs", "1000"),
    ):
        append_set(command, name, value)
    append_command(command, "map", MAP_NAME)

    environment = os.environ.copy()
    environment.update(
        {
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / ".config"),
            "XDG_DATA_HOME": str(home / ".local" / "share"),
            "XDG_RUNTIME_DIR": str(xdg_runtime_dir),
            "WAYLAND_DISPLAY": args.wayland_display,
            "SDL_VIDEO_DRIVER": "wayland",
            "SDL_VIDEODRIVER": "wayland",
        }
    )
    environment.pop("DISPLAY", None)
    environment.pop("OPENQ4_FORCE_X11", None)

    started = time.monotonic()
    process: subprocess.Popen[str] | None = None
    timed_out = False
    abort_reason = ""
    with (
        stdout_path.open("w", encoding="utf-8", errors="replace") as stdout,
        stderr_path.open("w", encoding="utf-8", errors="replace") as stderr,
    ):
        try:
            process = subprocess.Popen(
                command,
                cwd=install_root,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            deadline = time.monotonic() + args.timeout
            while process.poll() is None and time.monotonic() < deadline:
                current_text = runtime_text(home, stdout_path, stderr_path)
                current_errors = (
                    matching_patterns(current_text, FATAL_PATTERNS)
                    + matching_patterns(current_text, GL_ERROR_PATTERNS)
                    + matching_patterns(current_text, AUDIO_ERROR_PATTERNS)
                )
                if current_errors:
                    abort_reason = "runtime error marker(s): " + ", ".join(sorted(set(current_errors)))
                    break
                time.sleep(0.5)
            if process.poll() is None:
                timed_out = time.monotonic() >= deadline
                if timed_out:
                    abort_reason = "runtime timeout"
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=10)

    elapsed = time.monotonic() - started
    exit_code = process.returncode if process else None
    log_path = find_log(home)
    log_text = read_text(log_path)
    combined_text = runtime_text(home, stdout_path, stderr_path)
    screenshot = validate_nontrivial_tga(find_screenshot(home))
    save_files = validate_save_files(home, args.save_slot)
    fatal_markers = matching_patterns(combined_text, FATAL_PATTERNS)
    gl_error_markers = matching_patterns(combined_text, GL_ERROR_PATTERNS)
    audio_error_markers = matching_patterns(combined_text, AUDIO_ERROR_PATTERNS)

    expected_module = expected_module_marker(game_module, arch)
    active_audio_device = extract_active_audio_device(log_text)
    audio_markers = {
        "OpenAL setup began": "Setup OpenAL device and context" in log_text,
        "OpenAL ALC version reported": re.search(r"^OpenAL ALC version:\s*\S+", log_text, re.MULTILINE) is not None,
        "OpenAL active device reported": bool(active_audio_device),
        "engine sound system initialized": "sound system initialized." in log_text,
    }
    client_markers = {
        "nonempty engine log": bool(log_text),
        "native Wayland active": "SDL3: native Wayland active" in log_text,
        "selected packaged game_sp": expected_module in log_text,
        "common initialization completed": "--- Common Initialization Complete ---" in log_text,
        "cinematic auto-skip configured": re.search(r"\bset g_autoSkipCinematics 1\b", log_text) is not None,
        "sound enabled": re.search(r"\bset s_noSound 0\b", log_text) is not None,
        "initial map entered": f"Map: {MAP_NAME}" in log_text,
        "initial player spawned": re.search(r"SpawnPlayer:\s*\d+", log_text) is not None,
        "initial gameplay config executed": f"AutoExecAfterMapLoad: executed {FIRST_CFG_REL}" in log_text,
        "save completed": f"Saved '{args.save_slot}'" in log_text,
        "save load began": re.search(r"loading a v\d+ savegame", log_text) is not None,
        "save restore initialized": "---------- Game Map Init SaveGame -----------" in log_text,
        "second gameplay config executed": f"AutoExecAfterMapLoad: executed {RESTORED_CFG_REL}" in log_text,
        "post-restore active marker": "OPENQ4_WAYLAND_SP_RESTORE_ACTIVE" in log_text,
        "post-restore screenshot write": f"Wrote {SCREENSHOT_REL}" in log_text,
        "clean game shutdown": "--------------- Game Shutdown ---------------" in log_text,
    }
    lifecycle_counts = {
        "mapInitialization": log_text.count(f"Map: {MAP_NAME}"),
        "firstActiveDraw": log_text.count("AutoExecAfterMapLoad: first active draw observed"),
        "gameMapShutdown": log_text.count("------------ Game Map Shutdown --------------"),
    }
    second_lifecycle = (
        lifecycle_counts["mapInitialization"] >= 2
        and lifecycle_counts["firstActiveDraw"] >= 2
        and client_markers["save restore initialized"]
        and client_markers["second gameplay config executed"]
        and client_markers["post-restore active marker"]
    )
    lifecycle_order_failures = ordered_pattern_failures(
        log_text,
        (
            ("software sound initialized", re.compile(re.escape("sound system initialized."))),
            ("native Wayland selected", re.compile(re.escape("SDL3: native Wayland active"))),
            ("packaged SP module selected", re.compile(re.escape(expected_module))),
            ("initial map load", re.compile(re.escape(f"Map: {MAP_NAME}"))),
            ("initial player spawn", re.compile(r"SpawnPlayer:\s*\d+")),
            ("initial active draw", re.compile(re.escape("AutoExecAfterMapLoad: first active draw observed"))),
            ("save/load config executed", re.compile(re.escape(f"AutoExecAfterMapLoad: executed {FIRST_CFG_REL}"))),
            ("save requested", re.compile(re.escape("OPENQ4_WAYLAND_SP_SAVE_REQUEST"))),
            ("save completed", re.compile(re.escape(f"Saved '{args.save_slot}'"))),
            ("load requested", re.compile(re.escape("OPENQ4_WAYLAND_SP_LOAD_REQUEST"))),
            ("savegame stream opened", re.compile(r"loading a v\d+ savegame")),
            ("old map shut down", re.compile(re.escape("------------ Game Map Shutdown --------------"))),
            ("restored map load", re.compile(re.escape(f"Map: {MAP_NAME}"))),
            ("savegame map initialized", re.compile(re.escape("---------- Game Map Init SaveGame -----------"))),
            ("restored config executed", re.compile(re.escape(f"AutoExecAfterMapLoad: executed {RESTORED_CFG_REL}"))),
            ("restored gameplay active", re.compile(re.escape("OPENQ4_WAYLAND_SP_RESTORE_ACTIVE"))),
            ("post-restore capture", re.compile(re.escape(f"Wrote {SCREENSHOT_REL}"))),
            ("clean game shutdown", re.compile(re.escape("--------------- Game Shutdown ---------------"))),
        ),
    )

    missing: list[str] = [name for name, passed in audio_markers.items() if not passed]
    missing.extend(name for name, passed in client_markers.items() if not passed)
    missing.extend(f"ordered lifecycle: {name}" for name in lifecycle_order_failures)
    if not second_lifecycle:
        missing.append(
            "second active gameplay lifecycle "
            f"(maps={lifecycle_counts['mapInitialization']}, activeDraws={lifecycle_counts['firstActiveDraw']})"
        )
    if save_files.get("status") != "pass":
        missing.append(f"complete non-temporary save-file set ({save_files.get('status', 'missing')})")
    if screenshot.get("status") != "pass":
        missing.append(f"nontrivial post-restore screenshot ({screenshot.get('status', 'missing')})")
    if timed_out:
        missing.append("runtime timeout")
    elif abort_reason:
        missing.append(abort_reason)
    clean_exit = exit_code == 0 and client_markers["clean game shutdown"]
    if not clean_exit:
        missing.append(f"clean client exit (code={exit_code})")

    software_audio_passed = all(audio_markers.values()) and not audio_error_markers
    passed = not missing and not fatal_markers and not gl_error_markers and not audio_error_markers
    report: dict[str, Any] = {
        "reportSchemaVersion": REPORT_SCHEMA_VERSION,
        "reportType": REPORT_TYPE,
        "status": "pass" if passed else "fail",
        "architecture": arch,
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "map": MAP_NAME,
        "saveSlot": args.save_slot,
        "nativeWayland": client_markers["native Wayland active"],
        "waylandDisplay": args.wayland_display,
        "waylandSocket": str(wayland_socket),
        "executionEvidence": {
            "nativeProcessArchitecture": True,
            "hostArchitecture": native_arch,
            "physicalHardwareAttested": bool(args.physical_hardware),
            "physicalHardwareAttestationSource": (
                "operator CLI flag; known-virtualization inspection passed"
                if args.physical_hardware
                else "not provided"
            ),
            "virtualMachineDetected": host_evidence["virtualMachineDetected"],
            "containerDetected": host_evidence["containerDetected"],
            "virtualizationInspection": host_evidence,
        },
        "audioEvidence": {
            "softwareInitializationPassed": software_audio_passed,
            "softwareMarkers": audio_markers,
            "activeDeviceReported": bool(active_audio_device),
            "activeDevice": active_audio_device,
            "humanAudiblePlaybackVerified": bool(args.human_audio_playback_verified),
            "humanPlaybackAttestationSource": (
                "operator CLI flag" if args.human_audio_playback_verified else "not provided"
            ),
            "automationHeardAudio": False,
            "scope": "Automation verifies OpenAL and engine sound initialization; audible output requires a human.",
        },
        "secondActiveGameplayLifecycle": second_lifecycle,
        "lifecycleCounts": lifecycle_counts,
        "lifecycleOrderFailures": lifecycle_order_failures,
        "clientMarkers": client_markers,
        "saveFiles": save_files,
        "screenshot": screenshot,
        "cleanExit": clean_exit,
        "exitCode": exit_code,
        "abortReason": abort_reason,
        "missingMarkers": missing,
        "fatalMarkers": fatal_markers,
        "glErrorMarkers": gl_error_markers,
        "audioErrorMarkers": audio_error_markers,
        "elapsedSeconds": round(elapsed, 3),
        "openQ4Commit": git_commit(ROOT),
        "openQ4GameCommit": git_commit(game_libs_repo),
        "sha256": {
            "client": sha256_file(client),
            "gameSp": sha256_file(game_module),
            "retailPak001": sha256_file(retail_pak),
        },
        "elf": elf_metadata,
        "paths": {
            "installRoot": str(install_root),
            "basepath": str(basepath),
            "gameModule": str(game_module),
            "home": str(home),
            "log": str(log_path) if log_path else "",
            "stdout": str(stdout_path),
            "stderr": str(stderr_path),
        },
        "command": command,
        "environmentContract": {
            "SDL_VIDEO_DRIVER": "wayland",
            "SDL_VIDEODRIVER": "wayland",
            "DISPLAYRemoved": "DISPLAY" not in environment,
            "OPENQ4_FORCE_X11Removed": "OPENQ4_FORCE_X11" not in environment,
        },
    }
    report_json, report_markdown = write_reports(run_dir, report)

    print(f"linux_wayland_stock_sp_smoke: {report['status']} ({arch}, {elapsed:.2f}s)")
    print(f"  report: {report_json}")
    print(f"  summary: {report_markdown}")
    print(
        "  audio: software initialization "
        f"{'passed' if software_audio_passed else 'failed'}; human audible playback "
        f"{'attested' if args.human_audio_playback_verified else 'not verified'}"
    )
    if not passed:
        for item in missing:
            print(f"  missing: {item}")
        for category, markers in (
            ("fatal", fatal_markers),
            ("OpenGL", gl_error_markers),
            ("audio", audio_error_markers),
        ):
            for marker in markers:
                print(f"  {category}: {marker}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
