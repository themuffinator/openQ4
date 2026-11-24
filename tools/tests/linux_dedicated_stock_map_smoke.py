#!/usr/bin/env python3
"""Validate a packaged Linux dedicated server with a native-Wayland client.

This opt-in hardware/runtime test requires the retail Quake 4 PK4s. It starts
the staged dedicated executable on ``mp/q4dm1``, waits until the server has
loaded the staged MP game module and map, connects the separately staged client
through native Wayland, captures active gameplay, and preserves an evidence
report under ``.tmp``.
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
from pathlib import Path
from typing import Any, Callable

from linux_physical_host_evidence import (
    collect_linux_host_evidence,
    reject_virtualized_physical_attestation,
)


ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA_VERSION = 1
REPORT_TYPE = "linux-wayland-stock-dedicated"
MAP_NAME = "mp/q4dm1"
SCREENSHOT_REL = "screenshots/linux-stock-dedicated/client.tga"
SERVER_LOG_NAME = "linux-stock-dedicated-server.log"
CLIENT_LOG_NAME = "linux-stock-dedicated-client.log"
SERVER_VIDEO_DRIVER_CANARY = "openq4-dedicated-must-not-init-video"

FATAL_PATTERNS = {
    "error": re.compile(r"^(?:\^[0-9])?ERROR:", re.MULTILINE),
    "fatal": re.compile(r"Fatal Error|Error during initialization", re.IGNORECASE),
    "gameModule": re.compile(
        r"couldn['’]t (?:load game dynamic library|find game DLL API)|wrong game DLL API version",
        re.IGNORECASE,
    ),
    "declChecksum": re.compile(r"decl[^\r\n]*checksum[^\r\n]*(?:mismatch|different)", re.IGNORECASE),
    "shader": re.compile(
        r"(?:shader compile|program link)[^\r\n]*(?:failed|error)|failed to compile",
        re.IGNORECASE,
    ),
    "gl": re.compile(
        r"\bGL_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY|STACK_(?:OVERFLOW|UNDERFLOW)|CONTEXT_LOST)\b"
        r"|could not initialize OpenGL|Unable to initialize OpenGL",
        re.IGNORECASE,
    ),
    "framebuffer": re.compile(
        r"\bGL_FRAMEBUFFER_(?:INCOMPLETE[A-Z0-9_]*|UNSUPPORTED|UNDEFINED)\b"
        r"|\b(?:framebuffer|FBO)\b[^\r\n]{0,64}\b(?:incomplete|unsupported)\b",
        re.IGNORECASE,
    ),
}

SERVER_VIDEO_PATTERNS = {
    "sdlVideo": re.compile(
        r"SDL(?:3)?:[^\r\n]*(?:Wayland|X11|video)|SDL (?:splash|system console)[^\r\n]*video",
        re.IGNORECASE,
    ),
    "windowSystem": re.compile(
        r"libEGL|MESA:\s*error|Gtk-WARNING|socket path[^\r\n]*wayland-|wayland-[^\r\n]*socket path",
        re.IGNORECASE,
    ),
    "videoCanary": re.compile(re.escape(SERVER_VIDEO_DRIVER_CANARY), re.IGNORECASE),
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
            f"packaged runtime architecture mismatch: {path} has ELF e_machine={machine}, expected {expected_machine} for {arch}"
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


def find_log(savepath: Path, name: str) -> Path | None:
    for candidate in (
        savepath / "baseoq4" / "logs" / name,
        savepath / "q4base" / "logs" / name,
        savepath / "logs" / name,
    ):
        if candidate.is_file():
            return candidate
    return None


def runtime_text(savepath: Path, log_name: str, stdout: Path, stderr: Path) -> str:
    return "\n".join(
        text
        for text in (
            read_text(find_log(savepath, log_name)),
            read_text(stdout),
            read_text(stderr),
        )
        if text
    )


def find_fatal_markers(text: str) -> list[str]:
    return [name for name, pattern in FATAL_PATTERNS.items() if pattern.search(text)]


def find_server_video_markers(text: str) -> list[str]:
    return [name for name, pattern in SERVER_VIDEO_PATTERNS.items() if pattern.search(text)]


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


def wait_for_server_ready(
    process: subprocess.Popen[str],
    read_diagnostics: Callable[[], str],
    port: int,
    expected_module_marker: str,
    timeout: float,
) -> tuple[bool, list[str], list[str]]:
    required = (
        expected_module_marker,
        "game initialized.",
        "--- Common Initialization Complete ---",
        f"Server spawned on port {port}.",
        "Server decl checksum:",
        f"Map: {MAP_NAME}",
        f"Dedicated map ready: {MAP_NAME}",
    )
    deadline = time.monotonic() + timeout
    missing = list(required)
    fatal: list[str] = []
    while time.monotonic() < deadline:
        text = read_diagnostics()
        missing = [marker for marker in required if marker not in text]
        fatal = find_fatal_markers(text)
        if not missing and not fatal:
            return True, [], []
        if fatal or process.poll() is not None:
            return False, missing, fatal
        time.sleep(0.25)
    return False, missing, find_fatal_markers(read_diagnostics())


def stop_process(process: subprocess.Popen[str], timeout: float, console_quit: bool) -> tuple[int | None, bool]:
    if process.poll() is not None:
        return process.returncode, process.returncode == 0

    if console_quit and process.stdin is not None:
        try:
            process.stdin.write("quit\n")
            process.stdin.flush()
            process.stdin.close()
            process.wait(timeout=timeout)
            return process.returncode, process.returncode == 0
        except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
            pass

    process.terminate()
    try:
        process.wait(timeout=min(timeout, 10.0))
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
    return process.returncode, False


def write_client_autoexec(savepath: Path, settle_frames: int, sample_msec: int) -> str:
    cfg_rel = "linux-stock-dedicated/client.cfg"
    payload = "\n".join(
        (
            f"wait {max(1, settle_frames)}",
            "framePacingReset",
            "r_rendererMetrics 1",
            "r_rendererGpuTimers 0",
            f"waitMsec {max(1, sample_msec)}",
            "rendererBenchmarkCapture",
            "r_rendererMetrics 0",
            "framePacingSnapshot",
            "gfxInfo",
            f'echo OPENQ4_STOCK_DEDICATED_CLIENT_CAPTURE "{MAP_NAME}"',
            f'screenshot "{SCREENSHOT_REL}"',
            "wait 5",
            "quit",
            "",
        )
    )
    screenshot_rel = Path(SCREENSHOT_REL)
    for game_dir in ("baseoq4", "q4base"):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
        (savepath / game_dir / screenshot_rel).parent.mkdir(parents=True, exist_ok=True)
    return cfg_rel


def find_screenshot(savepath: Path) -> Path | None:
    rel = Path(SCREENSHOT_REL)
    for game_dir in ("baseoq4", "q4base"):
        candidate = savepath / game_dir / rel
        if candidate.is_file():
            return candidate
    candidate = savepath / rel
    return candidate if candidate.is_file() else None


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


def extract_decl_checksum(text: str, role: str) -> str:
    matches = re.findall(rf"{re.escape(role)} decl checksum:\s*(0x[0-9A-Fa-f]+)", text)
    return matches[-1].lower() if matches else ""


def tier_contract_passes(text: str) -> bool:
    lines = [line for line in text.splitlines() if "Renderer tier contract:" in line]
    if not lines:
        return False
    line = lines[-1]
    return all(token in line for token in ("selectedReady=1", "requestedReady=1", "degraded=0", "failClosed=0", "missing=none"))


def benchmark_capture_has_samples(text: str) -> bool:
    lines = [line for line in text.splitlines() if "rendererBenchmark capture(" in line]
    if not lines:
        return False
    samples = re.search(r"\bsamples=(\d+)\b", lines[-1])
    return bool(samples and int(samples.group(1)) > 0)


def expected_module_marker(game_module: Path, arch: str) -> str:
    return (
        f"Selected game module: logical='game_mp' binary='game_{arch}' "
        f"path='{game_module}'"
    )


def write_reports(run_dir: Path, report: dict[str, Any]) -> tuple[Path, Path]:
    json_path = run_dir / "report.json"
    markdown_path = run_dir / "report.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Linux Dedicated Stock-Map Validation",
        "",
        f"- Status: **{report['status']}**",
        f"- Architecture: `{report['architecture']}`",
        f"- Map: `{report['map']}`",
        f"- Native Wayland: `{report['nativeWayland']}`",
        f"- Physical hardware attested by operator: `{report['executionEvidence']['physicalHardwareAttested']}`",
        f"- VM/emulator indicators detected: `{report['executionEvidence']['virtualMachineDetected']}`",
        f"- Container detected: `{report['executionEvidence']['containerDetected']}`",
        f"- Dedicated remained headless: `{report['serverHeadless']}`",
        f"- Dedicated/client engine logs: `{report['serverLogNonempty']}` / `{report['clientLogNonempty']}`",
        f"- Server/client declaration checksum: `{report['serverDeclChecksum'] or 'missing'}` / `{report['clientDeclChecksum'] or 'missing'}`",
        f"- Client screenshot: `{report['screenshot'].get('path', '')}` ({report['screenshot'].get('status', 'missing')})",
        f"- Server clean shutdown: `{report['serverCleanShutdown']}`",
        f"- Client clean shutdown: `{report['clientCleanShutdown']}`",
        f"- Elapsed seconds: `{report['elapsedSeconds']}`",
        "",
        "## Evidence",
        "",
        f"- Server log: `{report['paths']['serverLog']}`",
        f"- Client log: `{report['paths']['clientLog']}`",
        f"- Server stdout/stderr: `{report['paths']['serverStdout']}` / `{report['paths']['serverStderr']}`",
        f"- Client stdout/stderr: `{report['paths']['clientStdout']}` / `{report['paths']['clientStderr']}`",
    ]
    if report["missingMarkers"]:
        lines.extend(("", "## Missing or failed checks", ""))
        lines.extend(f"- {item}" for item in report["missingMarkers"])
    if report["fatalMarkers"]:
        lines.extend(("", "## Fatal markers", ""))
        lines.extend(f"- {item}" for item in report["fatalMarkers"])
    markdown_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return json_path, markdown_path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, default=ROOT / ".install", help="Staged package root.")
    parser.add_argument(
        "--dedicated-executable",
        type=Path,
        default=None,
        help="Override the packaged openQ4 dedicated-server executable.",
    )
    parser.add_argument(
        "--client-executable",
        type=Path,
        default=None,
        help="Override the packaged openQ4 client executable.",
    )
    parser.add_argument("--basepath", type=Path, required=True, help="Quake 4 install root containing q4base retail PK4s.")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / ".tmp" / "linux-dedicated-stock-map",
        help="Parent directory for isolated homes, logs, screenshot, and reports.",
    )
    parser.add_argument("--game-libs-repo", type=Path, default=None, help="Optional companion source repo for commit evidence.")
    parser.add_argument("--arch", choices=("x64", "arm64"), default=None, help="Override native host architecture detection.")
    parser.add_argument("--port", type=int, default=28120, help="Loopback dedicated-server port.")
    parser.add_argument("--wayland-display", default=os.environ.get("WAYLAND_DISPLAY") or None, help="Native Wayland socket name.")
    parser.add_argument(
        "--xdg-runtime-dir",
        type=Path,
        default=Path(os.environ["XDG_RUNTIME_DIR"]) if os.environ.get("XDG_RUNTIME_DIR") else None,
        help="Directory containing the Wayland socket.",
    )
    parser.add_argument("--server-ready-timeout", type=float, default=300.0, help="Seconds allowed for dedicated map readiness.")
    parser.add_argument("--client-timeout", type=float, default=1200.0, help="Seconds allowed for client load, capture, and clean exit.")
    parser.add_argument("--shutdown-timeout", type=float, default=60.0, help="Seconds allowed for a console-driven server shutdown.")
    parser.add_argument("--settle-frames", type=int, default=120, help="Active gameplay frames before the client capture window.")
    parser.add_argument("--sample-msec", type=int, default=2000, help="Real milliseconds sampled before client diagnostics/capture.")
    parser.add_argument(
        "--physical-hardware",
        action="store_true",
        help=(
            "Operator attestation that this run is on physical hardware; known VM/emulator "
            "indicators are inspected, recorded, and rejected."
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if sys.platform != "linux":
        raise RuntimeError("linux_dedicated_stock_map must run natively on Linux")
    if not 1 <= args.port <= 65535:
        raise RuntimeError("--port must be between 1 and 65535")
    if min(args.server_ready_timeout, args.client_timeout, args.shutdown_timeout) <= 0:
        raise RuntimeError("all timeouts must be greater than zero")
    if args.settle_frames <= 0 or args.sample_msec <= 0:
        raise RuntimeError("--settle-frames and --sample-msec must be greater than zero")

    native_arch = host_arch_tag()
    arch = args.arch or native_arch
    if arch != native_arch:
        raise RuntimeError(
            f"stock-map evidence must run natively: host is {native_arch}, but --arch requested {arch}"
        )
    host_evidence = collect_linux_host_evidence()
    if args.physical_hardware:
        reject_virtualized_physical_attestation(host_evidence)
    install_root = args.install_root.resolve()
    basepath = args.basepath.resolve()
    game_libs_repo = args.game_libs_repo.resolve() if args.game_libs_repo else None
    dedicated = (args.dedicated_executable or install_root / f"openQ4-ded_{arch}").resolve()
    client = (args.client_executable or install_root / f"openQ4-client_{arch}").resolve()
    game_module = install_root / "baseoq4" / f"game_{arch}.so"
    mod_manifest = install_root / "baseoq4" / "mod.json"
    openq4_pak0 = install_root / "baseoq4" / "pak0.pk4"
    openq4_pak1 = install_root / "baseoq4" / "pak1.pk4"
    retail_pak = basepath / "q4base" / "pak001.pk4"
    for description, path in (
        ("packaged dedicated server", dedicated),
        ("packaged client", client),
        ("packaged MP game module", game_module),
        ("packaged openQ4 mod manifest", mod_manifest),
        ("packaged openQ4 pak0.pk4", openq4_pak0),
        ("packaged openQ4 pak1.pk4", openq4_pak1),
        ("retail Quake 4 pak001.pk4", retail_pak),
    ):
        if not path.is_file():
            raise RuntimeError(f"{description} not found: {path}")
    for executable in (dedicated, client):
        if not os.access(executable, os.X_OK):
            raise RuntimeError(f"packaged executable is not executable: {executable}")

    packaged_q4base = install_root / "q4base"
    if packaged_q4base.is_dir() and any(path.is_file() for path in packaged_q4base.rglob("*")):
        raise RuntimeError(
            f"refusing stock-map evidence because the staged package contains q4base overrides: {packaged_q4base}"
        )

    elf_metadata = {
        "dedicated": validate_native_elf(dedicated, arch),
        "client": validate_native_elf(client, arch),
        "gameMp": validate_native_elf(game_module, arch),
    }

    if not args.wayland_display or Path(args.wayland_display).name != args.wayland_display:
        raise RuntimeError("--wayland-display must be a non-empty Wayland socket basename such as wayland-0")
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
    server_home = run_dir / "server-home"
    client_home = run_dir / "client-home"
    server_runtime_context = tempfile.TemporaryDirectory(prefix=f"openq4-ded-xdg-{arch}-")
    server_runtime = Path(server_runtime_context.name)
    server_home.mkdir()
    client_home.mkdir()
    server_runtime.chmod(0o700)
    if len(os.fsencode(str(server_runtime / "wayland-0"))) >= 108:
        raise RuntimeError(f"private dedicated XDG runtime socket path would exceed the Linux limit: {server_runtime}")
    for home in (server_home, client_home):
        (home / ".config" / "openq4").mkdir(parents=True)
        (home / ".local" / "share" / "openq4").mkdir(parents=True)
    cfg_rel = write_client_autoexec(client_home, args.settle_frames, args.sample_msec)

    server_stdout = run_dir / "server.stdout.txt"
    server_stderr = run_dir / "server.stderr.txt"
    client_stdout = run_dir / "client.stdout.txt"
    client_stderr = run_dir / "client.stderr.txt"

    server_command = [str(dedicated)]
    for name, value in (
        ("sys_allowMultipleInstances", "1"),
        ("in_tty", "0"),
        ("sys_consoleWindow", "0"),
        ("logFile", "2"),
        ("logFileName", f"logs/{SERVER_LOG_NAME}"),
        ("developer", "1"),
        ("fs_basepath", basepath),
        ("fs_homepath", server_home),
        ("fs_savepath", server_home),
        ("fs_devpath", install_root),
        ("fs_game", "baseoq4"),
        ("net_serverDedicated", "1"),
        ("net_port", args.port),
        ("si_pure", "0"),
        ("net_serverAllowServerMod", "1"),
        ("sv_cheats", "1"),
        ("si_gameType", "DM"),
        ("s_noSound", "1"),
    ):
        append_set(server_command, name, value)
    append_command(server_command, "spawnServer", MAP_NAME)

    client_command = [str(client)]
    for name, value in (
        ("sys_allowMultipleInstances", "1"),
        ("in_tty", "0"),
        ("logFile", "2"),
        ("logFileName", f"logs/{CLIENT_LOG_NAME}"),
        ("developer", "1"),
        ("fs_basepath", basepath),
        ("fs_homepath", client_home),
        ("fs_savepath", client_home),
        ("fs_devpath", install_root),
        ("fs_game", "baseoq4"),
        ("r_ignoreGLErrors", "0"),
        ("r_fullscreen", "0"),
        ("r_mode", "3"),
        ("r_multiSamples", "0"),
        ("r_postAA", "0"),
        ("r_shadows", "0"),
        ("r_glTier", "legacy"),
        ("r_renderer", "best"),
        ("r_rendererMetrics", "0"),
        ("r_rendererGpuTimers", "0"),
        ("com_machineSpec", "0"),
        ("com_maxfps", "60"),
        ("com_skipLoadingContinue", "1"),
        ("com_loadingContinueAutoAdvance", "1"),
        ("ui_autoJoin", "1"),
        ("ui_name", "DedicatedStockMapClient"),
        ("s_noSound", "1"),
        ("g_autoExecAfterMapLoad", cfg_rel),
        ("g_autoExecAfterMapLoadDelayMs", "1000"),
    ):
        append_set(client_command, name, value)
    append_command(client_command, "connect", f"127.0.0.1:{args.port}")

    base_environment = os.environ.copy()
    server_environment = base_environment.copy()
    server_environment.update(
        {
            "HOME": str(server_home),
            "XDG_CONFIG_HOME": str(server_home / ".config"),
            "XDG_DATA_HOME": str(server_home / ".local" / "share"),
            "XDG_RUNTIME_DIR": str(server_runtime),
        }
    )
    for name in (
        "DISPLAY",
        "WAYLAND_DISPLAY",
        "OPENQ4_FORCE_X11",
        "GDK_BACKEND",
    ):
        server_environment.pop(name, None)
    # This intentionally invalid value is a canary, not a fallback. A correct
    # dedicated binary never initializes SDL video, so it never resolves the
    # driver. Any accidental splash/console/video path emits a rejected marker.
    server_environment["SDL_VIDEO_DRIVER"] = SERVER_VIDEO_DRIVER_CANARY
    server_environment["SDL_VIDEODRIVER"] = SERVER_VIDEO_DRIVER_CANARY
    client_environment = base_environment.copy()
    client_environment.update(
        {
            "HOME": str(client_home),
            "XDG_CONFIG_HOME": str(client_home / ".config"),
            "XDG_DATA_HOME": str(client_home / ".local" / "share"),
            "XDG_RUNTIME_DIR": str(xdg_runtime_dir),
            "WAYLAND_DISPLAY": args.wayland_display,
            "SDL_VIDEO_DRIVER": "wayland",
            "SDL_VIDEODRIVER": "wayland",
        }
    )
    client_environment.pop("DISPLAY", None)
    client_environment.pop("OPENQ4_FORCE_X11", None)

    started = time.monotonic()
    server_process: subprocess.Popen[str] | None = None
    client_process: subprocess.Popen[str] | None = None
    server_exit: int | None = None
    client_exit: int | None = None
    server_ready = False
    server_clean = False
    client_timed_out = False
    client_abort_reason = ""
    server_missing: list[str] = []
    early_fatal: list[str] = []

    with (
        server_stdout.open("w", encoding="utf-8", errors="replace") as server_out,
        server_stderr.open("w", encoding="utf-8", errors="replace") as server_err,
        client_stdout.open("w", encoding="utf-8", errors="replace") as client_out,
        client_stderr.open("w", encoding="utf-8", errors="replace") as client_err,
    ):
        try:
            server_process = subprocess.Popen(
                server_command,
                cwd=install_root,
                env=server_environment,
                stdin=subprocess.PIPE,
                stdout=server_out,
                stderr=server_err,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            server_ready, server_missing, early_fatal = wait_for_server_ready(
                server_process,
                lambda: runtime_text(server_home, SERVER_LOG_NAME, server_stdout, server_stderr),
                args.port,
                expected_module_marker(game_module, arch),
                args.server_ready_timeout,
            )
            if server_ready:
                client_process = subprocess.Popen(
                    client_command,
                    cwd=install_root,
                    env=client_environment,
                    stdin=subprocess.DEVNULL,
                    stdout=client_out,
                    stderr=client_err,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                deadline = time.monotonic() + args.client_timeout
                while client_process.poll() is None and time.monotonic() < deadline:
                    if server_process.poll() is not None:
                        client_abort_reason = f"dedicated server exited during client run (code={server_process.returncode})"
                        break
                    client_fatal = find_fatal_markers(
                        runtime_text(client_home, CLIENT_LOG_NAME, client_stdout, client_stderr)
                    )
                    if client_fatal:
                        client_abort_reason = "client fatal marker(s): " + ", ".join(client_fatal)
                        break
                    time.sleep(0.5)
                if client_process.poll() is None:
                    client_timed_out = time.monotonic() >= deadline
                    if client_timed_out:
                        client_abort_reason = "client timeout"
                    client_exit, _ = stop_process(client_process, 10.0, False)
                else:
                    client_exit = client_process.returncode
        finally:
            if client_process is not None and client_process.poll() is None:
                client_exit, _ = stop_process(client_process, 10.0, False)
            if server_process is not None:
                server_exit, server_clean = stop_process(server_process, args.shutdown_timeout, server_ready)

    elapsed = time.monotonic() - started
    server_log = find_log(server_home, SERVER_LOG_NAME)
    client_log = find_log(client_home, CLIENT_LOG_NAME)
    server_text = runtime_text(server_home, SERVER_LOG_NAME, server_stdout, server_stderr)
    client_text = runtime_text(client_home, CLIENT_LOG_NAME, client_stdout, client_stderr)
    server_log_text = read_text(server_log)
    client_log_text = read_text(client_log)
    server_checksum = extract_decl_checksum(server_text, "Server")
    client_checksum = extract_decl_checksum(client_text, "Client")
    screenshot = validate_nontrivial_tga(find_screenshot(client_home))
    server_video_markers = find_server_video_markers(server_text)

    missing: list[str] = []
    if not server_ready:
        missing.extend(f"server readiness: {marker}" for marker in server_missing)
    if not server_log_text:
        missing.append("nonempty dedicated engine log")
    if not client_log_text:
        missing.append("nonempty client engine log")
    if server_video_markers:
        missing.append("dedicated remained headless (video markers: " + ", ".join(server_video_markers) + ")")
    server_after_markers = {
        "dedicated client connected": re.search(r"client \d+ connected\.", server_text) is not None,
        "dedicated SpawnPlayer": re.search(r"SpawnPlayer:\s*\d+", server_text) is not None,
        "dedicated clean game shutdown": "--------------- Game Shutdown ---------------" in server_text,
    }
    client_markers = {
        "native Wayland active": "SDL3: native Wayland active" in client_text,
        "client selected packaged game_mp": expected_module_marker(game_module, arch) in client_text,
        "client decl checksum": bool(client_checksum),
        "client received connect response": "received connect response from" in client_text,
        "client entered mp/q4dm1": f"Map: {MAP_NAME}" in client_text,
        "client SpawnPlayer": re.search(r"SpawnPlayer:\s*\d+", client_text) is not None,
        "client first active draw": "AutoExecAfterMapLoad: first active draw observed" in client_text,
        "client capture cfg executed": f"AutoExecAfterMapLoad: executed {cfg_rel}" in client_text,
        "client frame pacing snapshot": "Frame pacing snapshot (MP):" in client_text,
        "client selected renderer tier": "Selected renderer tier:" in client_text,
        "client renderer tier contract": tier_contract_passes(client_text),
        "client renderer benchmark samples": benchmark_capture_has_samples(client_text),
        "client screenshot write marker": f"Wrote {SCREENSHOT_REL}" in client_text,
        "client clean game shutdown": "--------------- Game Shutdown ---------------" in client_text,
    }
    missing.extend(name for name, passed in server_after_markers.items() if not passed)
    missing.extend(name for name, passed in client_markers.items() if not passed)
    expected_module = expected_module_marker(game_module, arch)
    server_sequence_failures = ordered_pattern_failures(
        server_log_text,
        (
            ("server selected packaged game_mp", re.compile(re.escape(expected_module))),
            ("server opened socket", re.compile(re.escape(f"Server spawned on port {args.port}."))),
            ("server declaration checksum", re.compile(r"Server decl checksum:\s*0x[0-9A-Fa-f]+")),
            ("server map load started", re.compile(re.escape(f"Map: {MAP_NAME}"))),
            ("server map became ready", re.compile(re.escape(f"Dedicated map ready: {MAP_NAME}"))),
            ("server accepted client", re.compile(r"client \d+ connected\.")),
            ("server spawned player", re.compile(r"SpawnPlayer:\s*\d+")),
            ("server shut game down", re.compile(re.escape("--------------- Game Shutdown ---------------"))),
        ),
    )
    client_sequence_failures = ordered_pattern_failures(
        client_log_text,
        (
            ("client selected packaged game_mp in order", re.compile(re.escape(expected_module))),
            ("client declaration checksum in order", re.compile(r"Client decl checksum:\s*0x[0-9A-Fa-f]+")),
            ("client connect response in order", re.compile(re.escape("received connect response from"))),
            ("client map load in order", re.compile(re.escape(f"Map: {MAP_NAME}"))),
            ("client player spawn in order", re.compile(r"SpawnPlayer:\s*\d+")),
            ("client first active draw in order", re.compile(re.escape("AutoExecAfterMapLoad: first active draw observed"))),
            ("client capture config in order", re.compile(re.escape(f"AutoExecAfterMapLoad: executed {cfg_rel}"))),
            ("client benchmark capture in order", re.compile(re.escape("rendererBenchmark capture("))),
            ("client screenshot in order", re.compile(re.escape(f"Wrote {SCREENSHOT_REL}"))),
            ("client game shutdown in order", re.compile(re.escape("--------------- Game Shutdown ---------------"))),
        ),
    )
    missing.extend(f"ordered lifecycle: {label}" for label in server_sequence_failures)
    missing.extend(f"ordered lifecycle: {label}" for label in client_sequence_failures)
    if not server_checksum:
        missing.append("server decl checksum")
    if not server_checksum or not client_checksum or server_checksum != client_checksum:
        missing.append(f"matching decl checksums ({server_checksum or 'missing'} != {client_checksum or 'missing'})")
    if screenshot.get("status") != "pass":
        missing.append(f"nontrivial client screenshot ({screenshot.get('status', 'missing')})")
    if client_timed_out:
        missing.append("client timeout")
    elif client_abort_reason:
        missing.append(client_abort_reason)
    if client_exit != 0:
        missing.append(f"client exit code {client_exit}")
    if not server_clean or server_exit != 0:
        missing.append(f"clean dedicated-server exit (code={server_exit}, clean={server_clean})")

    fatal = sorted(set(early_fatal + [f"server:{name}" for name in find_fatal_markers(server_text)] + [f"client:{name}" for name in find_fatal_markers(client_text)]))
    passed = not missing and not fatal
    binary_hashes = {
        "dedicated": sha256_file(dedicated),
        "client": sha256_file(client),
        "gameMp": sha256_file(game_module),
        "retailPak001": sha256_file(retail_pak),
    }
    report: dict[str, Any] = {
        "reportSchemaVersion": REPORT_SCHEMA_VERSION,
        "reportType": REPORT_TYPE,
        "status": "pass" if passed else "fail",
        "architecture": arch,
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "map": MAP_NAME,
        "port": args.port,
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
        "serverReady": server_ready,
        "serverHeadless": not server_video_markers,
        "serverVideoMarkers": server_video_markers,
        "serverLogNonempty": bool(server_log_text),
        "clientLogNonempty": bool(client_log_text),
        "serverLifecycleOrderFailures": server_sequence_failures,
        "clientLifecycleOrderFailures": client_sequence_failures,
        "serverExitCode": server_exit,
        "clientExitCode": client_exit,
        "clientAbortReason": client_abort_reason,
        "serverCleanShutdown": server_clean and server_exit == 0,
        "clientCleanShutdown": client_exit == 0 and client_markers["client clean game shutdown"],
        "serverDeclChecksum": server_checksum,
        "clientDeclChecksum": client_checksum,
        "matchingDeclChecksum": bool(server_checksum and server_checksum == client_checksum),
        "serverMarkers": server_after_markers,
        "clientMarkers": client_markers,
        "screenshot": screenshot,
        "missingMarkers": missing,
        "fatalMarkers": fatal,
        "elapsedSeconds": round(elapsed, 3),
        "openQ4Commit": git_commit(install_root.parent),
        "openQ4GameCommit": git_commit(game_libs_repo),
        "sha256": binary_hashes,
        "elf": elf_metadata,
        "paths": {
            "installRoot": str(install_root),
            "basepath": str(basepath),
            "gameModule": str(game_module),
            "serverHome": str(server_home),
            "serverXdgRuntimeDir": str(server_runtime),
            "clientHome": str(client_home),
            "serverLog": str(server_log) if server_log else "",
            "clientLog": str(client_log) if client_log else "",
            "serverStdout": str(server_stdout),
            "serverStderr": str(server_stderr),
            "clientStdout": str(client_stdout),
            "clientStderr": str(client_stderr),
        },
        "serverCommand": server_command,
        "clientCommand": client_command,
    }
    report_json, report_markdown = write_reports(run_dir, report)
    server_runtime_context.cleanup()

    print(f"linux_dedicated_stock_map: {report['status']} ({arch}, {elapsed:.2f}s)")
    print(f"  report: {report_json}")
    print(f"  summary: {report_markdown}")
    if not passed:
        for item in missing:
            print(f"  missing: {item}")
        for item in fatal:
            print(f"  fatal: {item}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
