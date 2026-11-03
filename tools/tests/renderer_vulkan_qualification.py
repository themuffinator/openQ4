#!/usr/bin/env python3
"""Provenance-bound Vulkan qualification runs for the Vulkan support gate.

Each suite drives the staged client against the retail Quake 4 assets with no
input (hidden window, engine-side captures only) and writes one report.json
that a reviewer can check against its retained artifacts:

- sp-sweep   every stock single-player map loads and draws its opening view
- mp-sweep   loopback-only listen servers host bot matches on stock MP maps
- soak       one process tours a ring of SP maps for several cycles with a
             vid_restart, sampling process memory and the Vulkan heaps
- benchmark  repeated synchronous `benchmark` runs per scene, validation off

Provenance: the source commit comes from the runtime's own version line, which
must carry a git SHA and must not be dirty, and is resolved to a full SHA in
this checkout. Every staged runtime file is hashed before and after the suite.
Each run records the retail pak checksums, the GPU and driver it used, and
hashes of its retained log, captures and process output. Sweeps compare every
Vulkan run's warnings with an OpenGL reference run of the same map.

  renderer_vulkan_qualification.py run --suite sp-sweep --output-dir .tmp/vkq/sp \
      --config vk=vulkan --config intel=vulkan:r_vkDevice=2 --config gl=gl
  renderer_vulkan_qualification.py self-test

See docs/dev/vulkan-support-evidence.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCRIPT_PATH = Path(__file__).resolve()
SCRIPT_DIR = SCRIPT_PATH.parent
ROOT = SCRIPT_DIR.parents[1]
HARNESS_RELATIVE_PATH = "tools/tests/renderer_vulkan_qualification.py"
BUDGET_CONTRACT_PATH = ROOT / "tools" / "validation" / "renderer_per_map_budgets.json"

REPORT_SCHEMA_VERSION = 1
REPORT_KIND = "openq4-vulkan-qualification-report"
SUITES = ("sp-sweep", "mp-sweep", "soak", "benchmark")

DONE_MARKER = "OPENQ4_VKQ_DONE"
STOP_MARKER = "OPENQ4_VKQ_STOP"
RESTART_MARKER = "OPENQ4_VKQ_RESTART"
MEMORY_MARKER = "OPENQ4_VKQ_MEM"

STOCK_SP_MAPS = (
    "game/airdefense1", "game/airdefense2", "game/hangar1", "game/hangar2",
    "game/mcc_landing", "game/mcc_1", "game/convoy1", "game/building_b",
    "game/convoy2", "game/convoy2b", "game/hub1", "game/hub2", "game/medlabs",
    "game/walker", "game/dispersal", "game/recomp", "game/putra", "game/waste",
    "game/mcc_2", "game/storage1", "game/storage2", "game/tram1", "game/tram1b",
    "game/process1", "game/process2", "game/network1", "game/network2",
    "game/core1", "game/core2",
)
STOCK_MP_MAPS = (
    "mp/q4dm1", "mp/q4dm2", "mp/q4dm3", "mp/q4dm4", "mp/q4dm5", "mp/q4dm6",
    "mp/q4dm7", "mp/q4dm8", "mp/q4dm9", "mp/q4dm10", "mp/q4dm11",
    "mp/q4tourney1", "mp/q4ctf1", "mp/q4ctf2", "mp/q4ctf3", "mp/q4ctf4",
    "mp/q4ctf5", "mp/q4ctf6", "mp/q4ctf7", "mp/q4ctf8",
)
SOAK_MAPS = (
    "game/airdefense1", "game/hangar1", "game/convoy1", "game/hub1",
    "game/medlabs", "game/recomp", "game/storage1", "game/core1",
)
BENCHMARK_MAPS = ("game/airdefense1", "game/storage2", "game/medlabs", "game/hub1")

DEFAULT_WIDTH = 1280
DEFAULT_HEIGHT = 720
DEFAULT_MP_BOTS = 4
DEFAULT_PORT = 28766
DEFAULT_BUDGET_P95_US = 20000
DEFAULT_BUDGET_P99_US = 28000

# A capture counts as a drawn view only with real spread: a black, cleared or
# loading frame has almost no luma variance and few distinct colours.
MIN_SCREENSHOT_STD_LUMA = 6.0
MIN_SCREENSHOT_DISTINCT_COLORS = 32

# Multiplayer gameplay is not deterministic: bots pick random characters, whose
# declarations load on demand, and ragdolls settle differently every run. These
# lines therefore differ between any two runs, renderer or not, and are the
# only warnings a Vulkan run may log that its OpenGL reference does not.
NONDETERMINISTIC_MP_WARNINGS = (
    re.compile(
        r"^WARNING: Loading non pre-cached "
        r"(?:material|entityDef|playerModel|skin|model) decl \S+$"
    ),
    re.compile(r"^WARNING: player\d+: body '[^']+' stuck in \d+ \(normal = "),
)

COLOR_ESCAPE = re.compile(r"\^(?:c\d{3}|\d)")
VERSION_LINE = re.compile(r"^openQ4 (?P<version>\S+)\s*$")
RENDER_API_LINE = re.compile(
    r"^Renderer API: requested=(?P<requested>\S+) active=(?P<active>\S+)"
)
VK_DEVICE_LINE = re.compile(r"^Vulkan: device (?P<index>\d+) '(?P<name>[^']*)'")
VK_DRIVER_LINE = re.compile(
    r"^Vulkan: driver '(?P<driver_name>[^']*)' '(?P<driver_info>[^']*)' "
    r"\(driverID (?P<driver_id>-?\d+), driverVersion 0x(?P<driver_version>[0-9a-f]{8}), "
    r"vendorID 0x(?P<vendor_id>[0-9a-f]{4,8}), deviceID 0x(?P<device_id>[0-9a-f]{4,8}), "
    r"deviceType (?P<device_type>\d+), api (?P<api>\d+\.\d+\.\d+)\)"
)
VK_VALIDATION_ENABLED = "Vulkan: validation enabled"
VK_VALIDATION_MESSAGE = "Vulkan validation:"
VUID = re.compile(r"VUID-[A-Za-z0-9_-]+")
GL_IDENTITY_LINE = re.compile(r"^GL_(?P<key>VENDOR|RENDERER|VERSION): (?P<value>.*)$")
CPU_LINE = re.compile(r"^CPU: (?P<cpu>.+)$")
PAK_LINE = re.compile(r"^Loaded pk4 (?P<path>.+?) with checksum 0x(?P<checksum>[0-9a-f]+)\s*$")
FATAL_LINE = re.compile(r"^(?:ERROR|FATAL): ")
WARNING_PREFIX = "WARNING:"
HEAP_LINE = re.compile(
    r"^Vulkan memory: heap (?P<heap>\d+) (?P<kind>\S+) used=(?P<used>[\d.]+) MiB "
    r"budget=(?P<budget>[\d.]+) MiB blocks=(?P<blocks>\d+) \((?P<block_mib>[\d.]+) MiB\) "
    r"allocations=(?P<allocations>\d+) \((?P<allocation_mib>[\d.]+) MiB\)"
)
BENCHMARK_LINE = re.compile(
    r"kpix:\s*(?P<kpix>\d+)\s+avg:\s*(?P<avg>[\d.]+)ms p50:\s*(?P<p50>\d+)ms "
    r"p95:\s*(?P<p95>\d+)ms p99:\s*(?P<p99>\d+)ms max:\s*(?P<max>\d+)ms "
    r"fps:\s*(?P<fps>[\d.]+) frames:\s*(?P<frames>\d+)"
)
HEX40 = re.compile(r"[0-9a-f]{40}")
CONFIG_ID = re.compile(r"[a-z][a-z0-9-]{0,15}")
CVAR_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
API_ALIASES = {"vk": "vulkan", "vulkan": "vulkan", "gl": "gl", "opengl": "gl"}
# These are report/launch invariants, not device/preset variations. In
# particular, hidden Vulkan renders offscreen and cannot qualify presentation.
FIXED_RUN_CVARS = {
    "r_renderapi", "r_hiddenwindow", "r_fullscreen", "r_borderless",
    "r_mode", "r_customwidth", "r_customheight", "r_windowwidth", "r_windowheight",
    "in_mouse", "in_joystick", "r_vkvalidation", "ui_autojoin",
    "fs_basepath", "fs_cdpath", "fs_savepath", "fs_devpath", "fs_game",
    "logfile", "logfilename", "g_autoexecaftermapload", "g_autoexecaftermaploaddelayms",
}

# Software rasterizers are regression coverage, never hardware qualification.
VK_PHYSICAL_DEVICE_TYPES = (1, 2)  # integrated, discrete
VK_SOFTWARE_DRIVER_IDS = (10, 13)  # SwiftShader, llvmpipe/lavapipe


@dataclass(frozen=True)
class RunConfig:
    id: str
    api: str
    cvars: tuple[tuple[str, str], ...]

    def as_dict(self) -> dict[str, Any]:
        return {"id": self.id, "api": self.api, "cvars": dict(self.cvars)}


def parse_config(text: str) -> RunConfig:
    """`id=api[:cvar=value,...]`, for example `intel=vulkan:r_vkDevice=2`."""
    ident, separator, rest = text.partition("=")
    if not separator or CONFIG_ID.fullmatch(ident) is None:
        raise ValueError(f"config {text!r}: expected id=api[:cvar=value,...]")
    api_text, _, cvar_text = rest.partition(":")
    api = API_ALIASES.get(api_text.strip().lower())
    if api is None:
        raise ValueError(f"config {text!r}: api must be vulkan or gl")
    cvars: list[tuple[str, str]] = []
    for item in (part for part in cvar_text.split(",") if part):
        name, equals, value = item.partition("=")
        if not equals or CVAR_NAME.fullmatch(name) is None:
            raise ValueError(f"config {text!r}: malformed cvar assignment {item!r}")
        if name.casefold() in FIXED_RUN_CVARS:
            raise ValueError(f"config {text!r}: {name} is fixed by the qualification runner")
        cvars.append((name, value))
    return RunConfig(ident, api, tuple(cvars))


def echo_marker(*parts: object) -> str:
    """A console echo of one marker line. The quotes keep the console lexer
    from splitting ids such as `sp-vk-airdefense1` at '-' and '/'."""
    return 'echo "' + " ".join(str(part) for part in parts) + '"'


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, root: Path, kind: str | None = None) -> dict[str, Any]:
    record: dict[str, Any] = {}
    if kind is not None:
        record["kind"] = kind
    record.update(
        path=path.relative_to(root).as_posix(),
        size=path.stat().st_size,
        sha256=sha256_file(path),
    )
    return record


def runtime_inventory(runtime_dir: Path) -> list[dict[str, Any]]:
    records = []
    for path in sorted(runtime_dir.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"staged runtime must not contain links: {path}")
        if path.is_file():
            records.append(file_record(path, runtime_dir))
    if not records:
        raise ValueError(f"staged runtime is empty: {runtime_dir}")
    return records


def host_platform() -> str:
    system = platform.system().lower()
    system = {"darwin": "macos"}.get(system, system)
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(
        machine, machine
    )
    return f"{system}-{arch}"


def parse_version(version: str) -> tuple[str | None, bool]:
    """Short git SHA and dirty flag from `0.13.2-dev+g9a07b853.dirty`."""
    _, plus, metadata = version.partition("+")
    if not plus:
        return None, False
    parts = metadata.split(".")
    sha = next(
        (part[1:] for part in parts if re.fullmatch(r"g[0-9a-f]{7,40}", part)), None
    )
    return sha, "dirty" in parts


def strip_colors(line: str) -> str:
    return COLOR_ESCAPE.sub("", line)


def parse_engine_log(text: str) -> dict[str, Any]:
    """Everything a qualification run claims, read from its own engine log."""
    lines = text.splitlines()
    parsed: dict[str, Any] = {
        "version": None,
        "commit": None,
        "dirty": None,
        "renderApi": None,
        "validationEnabled": False,
        "validationMessages": 0,
        "vuids": [],
        "fatal": [],
        "vulkanDevice": None,
        "vulkanDriver": None,
        "glIdentity": {},
        "cpu": None,
        "paks": [],
        "warnings": [],
        "markers": [],
        "heapReports": [],
        "benchmark": [],
    }
    if lines:
        match = VERSION_LINE.match(lines[0].strip())
        if match:
            parsed["version"] = match.group("version")
            parsed["commit"], parsed["dirty"] = parse_version(parsed["version"])
    vuids: set[str] = set()
    current_memory: dict[str, Any] | None = None
    for raw in lines:
        line = strip_colors(raw.rstrip("\r"))
        if line.startswith(WARNING_PREFIX):
            parsed["warnings"].append(line)
        if VK_VALIDATION_MESSAGE in line:
            parsed["validationMessages"] += 1
        vuids.update(VUID.findall(line))
        if line.startswith(VK_VALIDATION_ENABLED):
            parsed["validationEnabled"] = True
        if FATAL_LINE.match(line):
            parsed["fatal"].append(line[:300])
        heap = HEAP_LINE.match(line)
        if heap and current_memory is not None:
            current_memory["heaps"].append(
                {
                    "heap": int(heap.group("heap")),
                    "kind": heap.group("kind"),
                    "usedMiB": float(heap.group("used")),
                    "allocations": int(heap.group("allocations")),
                }
            )
            continue
        current_memory = None
        if line.startswith(MEMORY_MARKER + " "):
            fields = line.split()
            if len(fields) == 3 and fields[1].isdigit():
                current_memory = {"stop": int(fields[1]), "phase": fields[2], "heaps": []}
                parsed["heapReports"].append(current_memory)
            continue
        if line.startswith((DONE_MARKER, STOP_MARKER, RESTART_MARKER)):
            parsed["markers"].append(line.strip())
            continue
        api = RENDER_API_LINE.match(line)
        if api:
            parsed["renderApi"] = {
                "requested": api.group("requested"),
                "active": api.group("active"),
            }
            continue
        device = VK_DEVICE_LINE.match(line)
        if device:
            parsed["vulkanDevice"] = {"index": int(device.group("index")), "name": device.group("name")}
            continue
        driver = VK_DRIVER_LINE.match(line)
        if driver:
            parsed["vulkanDriver"] = {
                "driverName": driver.group("driver_name"),
                "driverInfo": driver.group("driver_info"),
                "driverId": int(driver.group("driver_id")),
                "driverVersion": "0x" + driver.group("driver_version"),
                "vendorId": "0x" + driver.group("vendor_id"),
                "deviceId": "0x" + driver.group("device_id"),
                "deviceType": int(driver.group("device_type")),
                "apiVersion": driver.group("api"),
            }
            continue
        identity = GL_IDENTITY_LINE.match(line)
        if identity:
            parsed["glIdentity"][identity.group("key").lower()] = identity.group("value").strip()
            continue
        cpu = CPU_LINE.match(line)
        if cpu:
            parsed["cpu"] = cpu.group("cpu").strip()
            continue
        pak = PAK_LINE.match(line)
        if pak:
            parsed["paks"].append({"path": pak.group("path"), "checksum": "0x" + pak.group("checksum")})
            continue
        bench = BENCHMARK_LINE.search(line)
        if bench:
            parsed["benchmark"].append(
                {
                    "kpix": int(bench.group("kpix")),
                    "avgMs": float(bench.group("avg")),
                    "p50Ms": int(bench.group("p50")),
                    "p95Ms": int(bench.group("p95")),
                    "p99Ms": int(bench.group("p99")),
                    "maxMs": int(bench.group("max")),
                    "fps": float(bench.group("fps")),
                    "frames": int(bench.group("frames")),
                }
            )
    parsed["vuids"] = sorted(vuids)
    return parsed


def normalize_warnings(warnings: list[str], replacements: dict[str, str]) -> set[str]:
    normalized: set[str] = set()
    for line in warnings:
        text = " ".join(line.split())
        for needle, token in replacements.items():
            if needle:
                text = text.replace(needle, token)
        normalized.add(text)
    return normalized


def warning_replacements(run_dir: Path, run_id: str) -> dict[str, str]:
    """Per-run strings that would make identical warnings differ between runs."""
    return {str(run_dir): "<run>", run_id: "<run-id>"}


def warning_digest(normalized: set[str]) -> str:
    return hashlib.sha256("\n".join(sorted(normalized)).encode("utf-8")).hexdigest()


def is_nondeterministic_mp_warning(line: str) -> bool:
    return any(pattern.search(line) for pattern in NONDETERMINISTIC_MP_WARNINGS)


def device_identity(api: str, parsed: dict[str, Any]) -> dict[str, Any] | None:
    if api == "vulkan":
        if parsed["vulkanDriver"] is None or parsed["vulkanDevice"] is None:
            return None
        identity = {"api": "vulkan", "name": parsed["vulkanDevice"]["name"]}
        identity.update(parsed["vulkanDriver"])
        identity["physical"] = (
            identity["deviceType"] in VK_PHYSICAL_DEVICE_TYPES
            and identity["driverId"] not in VK_SOFTWARE_DRIVER_IDS
        )
        return identity
    gl = parsed["glIdentity"]
    if not gl.get("renderer"):
        return None
    return {
        "api": "gl",
        "vendor": gl.get("vendor", ""),
        "renderer": gl.get("renderer", ""),
        "version": gl.get("version", ""),
    }


def read_tga(path: Path) -> tuple[int, int, int, bytes]:
    """Width, height, bytes per pixel and BGR(A) pixel data of a TGA capture."""
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError("truncated TGA header")
    id_length, colormap_type, image_type = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    depth = data[16]
    if colormap_type != 0 or image_type not in (2, 10) or depth not in (24, 32):
        raise ValueError(f"unsupported TGA type={image_type} depth={depth}")
    bpp = depth // 8
    offset = 18 + id_length
    expected = width * height * bpp
    if image_type == 2:
        pixels = data[offset:offset + expected]
    else:
        out = bytearray()
        cursor = offset
        while len(out) < expected and cursor < len(data):
            header = data[cursor]
            cursor += 1
            count = (header & 0x7F) + 1
            if header & 0x80:
                out += data[cursor:cursor + bpp] * count
                cursor += bpp
            else:
                out += data[cursor:cursor + bpp * count]
                cursor += bpp * count
        pixels = bytes(out[:expected])
    if width == 0 or height == 0 or len(pixels) != expected:
        raise ValueError("truncated TGA pixel data")
    return width, height, bpp, pixels


def screenshot_stats(path: Path) -> dict[str, Any]:
    width, height, bpp, pixels = read_tga(path)
    step = max(1, (width * height) // 120000)
    lumas: list[float] = []
    colors: set[tuple[int, int, int]] = set()
    for index in range(0, width * height, step):
        base = index * bpp
        blue, green, red = pixels[base], pixels[base + 1], pixels[base + 2]
        lumas.append(0.2126 * red + 0.7152 * green + 0.0722 * blue)
        colors.add((red >> 3, green >> 3, blue >> 3))
    return {
        "width": width,
        "height": height,
        "meanLuma": round(statistics.fmean(lumas), 3),
        "stdLuma": round(statistics.pstdev(lumas), 3),
        "distinctColors": len(colors),
    }


def screenshot_is_drawn(stats: dict[str, Any]) -> bool:
    return (
        stats.get("stdLuma", 0.0) >= MIN_SCREENSHOT_STD_LUMA
        and stats.get("distinctColors", 0) >= MIN_SCREENSHOT_DISTINCT_COLORS
    )


def git(root: Path, *arguments: str) -> str | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), *arguments],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def resolve_source_commit(root: Path, short: str, explicit: str | None) -> tuple[str | None, str | None]:
    """Full SHA for the runtime's version-line SHA, or an error."""
    if explicit:
        explicit = explicit.strip().lower()
        if HEX40.fullmatch(explicit) is None:
            return None, "--source-commit must be a full 40-character SHA"
        if not explicit.startswith(short):
            return None, f"--source-commit {explicit} does not match the runtime's g{short}"
        if git(root, "cat-file", "-t", explicit) != "commit":
            return None, f"--source-commit {explicit} is not a commit in {root}"
        return explicit, None
    resolved = git(root, "rev-parse", "--verify", "--quiet", f"{short}^{{commit}}")
    if resolved is None or HEX40.fullmatch(resolved) is None:
        return None, (
            f"cannot resolve the runtime's g{short} in {root}; fetch it or pass --source-commit"
        )
    return resolved, None


def harness_identity(root: Path, source_commit: str | None) -> dict[str, Any]:
    identity: dict[str, Any] = {
        "path": HARNESS_RELATIVE_PATH,
        "sha256": sha256_file(SCRIPT_PATH),
        "gitBlob": git(root, "hash-object", str(SCRIPT_PATH)),
        "matchesSourceCommit": False,
    }
    if source_commit and identity["gitBlob"]:
        committed = git(root, "rev-parse", f"{source_commit}:{HARNESS_RELATIVE_PATH}")
        identity["matchesSourceCommit"] = committed == identity["gitBlob"]
    return identity


def pak_origin(path: str, basepath: str, runtime_dir: Path) -> str:
    folded = path.replace("\\", "/").casefold()
    if basepath and folded.startswith(basepath.replace("\\", "/").casefold().rstrip("/") + "/"):
        return "basepath"
    if folded.startswith(str(runtime_dir).replace("\\", "/").casefold().rstrip("/") + "/"):
        return "runtime"
    return "other"


def pak_inventory(parsed: dict[str, Any], basepath: str, runtime_dir: Path) -> list[dict[str, Any]]:
    return [
        {
            "name": Path(pak["path"].replace("\\", "/")).name,
            "checksum": pak["checksum"],
            "origin": pak_origin(pak["path"], basepath, runtime_dir),
        }
        for pak in parsed["paks"]
    ]


def paks_digest(paks: list[dict[str, Any]]) -> str:
    """Runs record a digest of their pak list; the report keeps the list once."""
    canonical = json.dumps(paks, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def load_budget_contract() -> tuple[dict[tuple[str, str], tuple[int, int]], dict[str, Any]]:
    budgets: dict[tuple[str, str], tuple[int, int]] = {}
    identity: dict[str, Any] = {"path": None, "contractId": None, "sha256": None}
    try:
        raw = BUDGET_CONTRACT_PATH.read_bytes()
        contract = json.loads(raw.decode("utf-8"))
    except (OSError, ValueError):
        return budgets, identity
    identity = {
        "path": BUDGET_CONTRACT_PATH.relative_to(ROOT).as_posix(),
        "contractId": contract.get("contractId"),
        "sha256": hashlib.sha256(raw).hexdigest(),
    }
    for budget in contract.get("budgets", []):
        if budget.get("profile") != "baseline" or not isinstance(budget.get("cpu"), dict):
            continue
        budgets[(budget.get("map"), budget.get("backend"))] = (
            int(budget["cpu"]["p95Us"]),
            int(budget["cpu"]["p99Us"]),
        )
    return budgets, identity


class MemorySampler:
    """Samples a client's private memory periodically and at soak stops."""

    def __init__(self, process: subprocess.Popen, log_path: Path, started: float) -> None:
        self.process = process
        self.log_path = log_path
        self.started = started
        self.samples: list[dict[str, Any]] = []
        self.stops: dict[int, dict[str, Any]] = {}
        self.metric = {"nt": "privateBytes", "posix": "residentBytes"}.get(os.name, "residentBytes")
        self._stop = threading.Event()
        self._threads = [
            threading.Thread(target=self._periodic, daemon=True),
            threading.Thread(target=self._follow_log, daemon=True),
        ]

    def start(self) -> None:
        for thread in self._threads:
            thread.start()

    def join(self) -> None:
        self._stop.set()
        for thread in self._threads:
            thread.join(timeout=15)

    def _value(self) -> int | None:
        if os.name == "nt":
            return _windows_private_bytes(self.process)
        status = Path(f"/proc/{self.process.pid}/status")
        if status.exists():
            try:
                fields = dict(
                    line.split(":", 1) for line in status.read_text().splitlines() if ":" in line
                )
            except OSError:
                return None
            for key in ("RssAnon", "VmRSS"):
                if key in fields:
                    return int(fields[key].split()[0]) * 1024
            return None
        try:
            completed = subprocess.run(
                ["ps", "-o", "rss=", "-p", str(self.process.pid)],
                capture_output=True, text=True, check=False,
            )
            return int(completed.stdout.strip()) * 1024 if completed.stdout.strip() else None
        except (OSError, ValueError):
            return None

    def _periodic(self) -> None:
        while self.process.poll() is None and not self._stop.is_set():
            value = self._value()
            if value is not None:
                self.samples.append({"seconds": round(time.time() - self.started, 1), "bytes": value})
            self._stop.wait(10.0)

    def _follow_log(self) -> None:
        position = 0
        pending = ""
        pattern = re.compile(rf"^{STOP_MARKER} (\d+) (\S+)\s*$")
        while self.process.poll() is None and not self._stop.is_set():
            try:
                with self.log_path.open("rb") as stream:
                    stream.seek(position)
                    chunk = stream.read()
                    position = stream.tell()
            except OSError:
                chunk = b""
            pending += chunk.decode("utf-8", errors="replace")
            *complete, pending = pending.split("\n")
            for line in complete:
                match = pattern.match(strip_colors(line.strip()))
                if match and int(match.group(1)) not in self.stops:
                    value = self._value()
                    if value is not None:
                        self.stops[int(match.group(1))] = {
                            "map": match.group(2),
                            "seconds": round(time.time() - self.started, 1),
                            "bytes": value,
                        }
            self._stop.wait(0.5)


def _windows_private_bytes(process: subprocess.Popen) -> int | None:
    import ctypes
    from ctypes import wintypes

    class ProcessMemoryCountersEx(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
            ("PrivateUsage", ctypes.c_size_t),
        ]

    counters = ProcessMemoryCountersEx()
    counters.cb = ctypes.sizeof(counters)
    psapi = ctypes.WinDLL("psapi")
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = getattr(process, "_handle", None)
    if handle is None:
        return None
    if not psapi.GetProcessMemoryInfo(wintypes.HANDLE(int(handle)), ctypes.byref(counters), counters.cb):
        return None
    return int(counters.PrivateUsage)


class Qualification:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.suite: str = args.suite
        self.output = args.output_dir.resolve()
        self.runtime = args.runtime_dir.resolve()
        self.basepath = args.basepath
        self.configs: list[RunConfig] = args.configs
        self.reference: str | None = args.reference
        self.failures: list[str] = []
        self.runs: list[dict[str, Any]] = []
        self.parsed: dict[str, dict[str, Any]] = {}
        self.paks: dict[str, list[dict[str, Any]]] = {}

    def validation_enabled(self) -> bool:
        return self.suite != "benchmark" and not self.args.no_validation

    def common_cvars(self, cfg_name: str) -> dict[str, str]:
        return {
            "r_vkValidation": "1" if self.validation_enabled() else "0",
            "r_hiddenWindow": "1",
            "r_fullscreen": "0",
            "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1",
            "in_mouse": "0",
            "in_joystick": "0",
            "s_noSound": "1",
            "r_mode": "-1",
            "r_windowWidth": str(DEFAULT_WIDTH),
            "r_windowHeight": str(DEFAULT_HEIGHT),
            "r_customWidth": str(DEFAULT_WIDTH),
            "r_customHeight": str(DEFAULT_HEIGHT),
            "r_swapInterval": "0",
            "com_maxFps": "120",
            "com_skipLoadingContinue": "1",
            "g_autoSkipCinematics": "1",
            "g_autoExecAfterMapLoad": cfg_name,
            "g_autoExecAfterMapLoadDelayMs": "1000",
            "net_serverDedicated": "0",
            "ui_autoJoin": "1",
            "net_port": str(DEFAULT_PORT),
            # loopback only: no firewall prompt and no master-server heartbeat
            "net_ip": "127.0.0.1",
            "net_enableIPv6": "0",
            "net_LANServer": "1",
        }

    def launch(
        self,
        run_id: str,
        config: RunConfig,
        map_name: str,
        cfgs: dict[str, list[str]],
        first_cfg: str,
        timeout: int,
        extra_cvars: dict[str, str],
        sample_memory: bool = False,
    ) -> dict[str, Any]:
        import renderer_validation_matrix as matrix

        run_dir = self.output / "runs" / run_id
        if run_dir.exists():
            shutil.rmtree(run_dir)
        savepath = run_dir / "savepath"
        gamepath = savepath / "baseoq4"
        (gamepath / "screenshots").mkdir(parents=True)
        for name, lines in cfgs.items():
            (gamepath / name).write_text("\n".join(lines) + "\n", encoding="utf-8")
        args = matrix.common_args(self.runtime, run_id, self.basepath, savepath, False)
        cvars = {"r_renderApi": config.api}
        cvars.update(self.common_cvars(first_cfg))
        cvars.update(extra_cvars)
        cvars.update(dict(config.cvars))
        for name, value in cvars.items():
            args += ["+set", name, value]
        multiplayer = map_name.startswith("mp/")
        args += ["+spawnServer" if multiplayer else "+map", map_name]
        log_path = gamepath / "logs" / f"openq4_validation_{matrix.sanitize_case_id(run_id)}.log"
        executable = matrix.find_client_executable(self.runtime)
        started = time.time()
        sampler = None
        with (run_dir / "stdout.txt").open("w", encoding="utf-8", errors="replace") as stdout, (
            run_dir / "stderr.txt"
        ).open("w", encoding="utf-8", errors="replace") as stderr:
            process = subprocess.Popen(
                [str(executable), *args],
                cwd=str(self.runtime),
                stdout=stdout,
                stderr=stderr,
                stdin=subprocess.DEVNULL,
            )
            if sample_memory:
                sampler = MemorySampler(process, log_path, started)
                sampler.start()
            timed_out = False
            try:
                exit_code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                exit_code = process.wait(timeout=30)
        if sampler is not None:
            sampler.join()
        elapsed = round(time.time() - started, 1)
        # A fresh savepath regenerates the image cache (tens of MB per run);
        # keep logs, captures and cfgs so long suites cannot fill the disk.
        for generated in list(savepath.rglob("generated")):
            if generated.is_dir():
                shutil.rmtree(generated, ignore_errors=True)
        text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
        parsed = parse_engine_log(text)
        self.parsed[run_id] = parsed
        self.paks[run_id] = pak_inventory(parsed, self.basepath, self.runtime)
        artifacts: list[dict[str, Any]] = []
        if log_path.is_file():
            artifacts.append(file_record(log_path, self.output, "engineLog"))
        for kind, name in (("processStdout", "stdout.txt"), ("processStderr", "stderr.txt")):
            artifacts.append(file_record(run_dir / name, self.output, kind))
        for name in sorted(cfgs):
            artifacts.append(file_record(gamepath / name, self.output, "config"))
        shots = sorted((gamepath / "screenshots").glob("*.tga"))
        screenshots = []
        for shot in shots:
            shot_record = file_record(shot, self.output, "screenshot")
            try:
                shot_record["stats"] = screenshot_stats(shot)
            except (OSError, ValueError) as exc:
                shot_record["stats"] = {"error": str(exc)}
            artifacts.append(shot_record)
            screenshots.append(shot_record)
        record: dict[str, Any] = {
            "id": run_id,
            "config": config.id,
            "api": config.api,
            "map": map_name,
            "exitCode": exit_code,
            "timedOut": timed_out,
            "elapsedSeconds": elapsed,
            "completed": f"{DONE_MARKER} {run_id}" in parsed["markers"],
            "version": parsed["version"],
            "renderApi": parsed["renderApi"],
            "validationEnabled": parsed["validationEnabled"],
            "validationMessages": parsed["validationMessages"],
            "vuids": parsed["vuids"],
            "fatal": parsed["fatal"][:10],
            "device": device_identity(config.api, parsed),
            "paksDigest": paks_digest(self.paks[run_id]),
            "warnings": {
                "lines": len(parsed["warnings"]),
                "unique": len(set(parsed["warnings"])),
            },
            "artifacts": artifacts,
        }
        if sampler is not None:
            record["memory"] = {
                "metric": sampler.metric,
                "samples": sampler.samples,
                "stops": {str(index): value for index, value in sorted(sampler.stops.items())},
            }
        self.check_run(record, screenshots)
        normalized = normalize_warnings(parsed["warnings"], warning_replacements(run_dir, run_id))
        record["warnings"]["digest"] = warning_digest(normalized)
        parsed["normalizedWarnings"] = normalized
        (run_dir / "record.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        return record

    def check_run(self, record: dict[str, Any], screenshots: list[dict[str, Any]]) -> None:
        problems: list[str] = []
        if record["exitCode"] != 0 or record["timedOut"]:
            problems.append(f"exit={record['exitCode']} timedOut={record['timedOut']}")
        if not record["completed"]:
            problems.append("the qualification cfg did not run to its completion marker")
        if record["version"] is None:
            problems.append("the engine log has no version line")
        if record["renderApi"] is None or record["renderApi"]["active"] != record["api"]:
            problems.append(f"active renderer is {record['renderApi']!r}, expected {record['api']}")
        if record["fatal"]:
            problems.append(f"error lines: {record['fatal'][:2]}")
        if record["api"] == "vulkan":
            if self.validation_enabled() and not record["validationEnabled"]:
                problems.append("Vulkan validation layers were requested but not enabled")
            if record["validationMessages"] or record["vuids"]:
                problems.append(
                    f"{record['validationMessages']} validation messages, VUIDs {record['vuids'][:5]}"
                )
        if record["device"] is None:
            problems.append("the log does not identify the GPU and driver")
        if not self.paks.get(record["id"]):
            problems.append("the log lists no loaded paks")
        if self.suite != "benchmark":
            if not screenshots:
                problems.append("no capture was written")
            for shot in screenshots:
                if not screenshot_is_drawn(shot.get("stats", {})):
                    problems.append(f"capture {shot['path']} is blank or not drawn: {shot.get('stats')}")
        record["status"] = "fail" if problems else "pass"
        record["problems"] = problems
        for problem in problems:
            self.failures.append(f"{record['id']}: {problem}")

    def sweep(self) -> list[dict[str, Any]]:
        multiplayer = self.suite == "mp-sweep"
        maps = self.args.maps or (STOCK_MP_MAPS if multiplayer else STOCK_SP_MAPS)
        comparisons: list[dict[str, Any]] = []
        for map_name in maps:
            short = map_name.split("/")[-1]
            by_config: dict[str, dict[str, Any]] = {}
            for config in self.configs:
                run_id = f"{'mp' if multiplayer else 'sp'}-{config.id}-{short}"
                cfg = []
                if multiplayer:
                    cfg.append(f"bot_minPlayers {self.args.bots}")
                cfg += [
                    f"wait {self.args.settle_frames}",
                    f'screenshot "screenshots/{run_id}.tga"',
                    "wait 5",
                    "gfxInfo",
                    echo_marker(DONE_MARKER, run_id),
                    "quit",
                ]
                extra = {}
                if multiplayer:
                    extra["si_gameType"] = "CTF" if "ctf" in short else "DM"
                else:
                    extra["si_gameType"] = "singleplayer"
                record = self.run_or_resume(
                    run_id, config, map_name, {"vkq.cfg": cfg}, "vkq.cfg", self.args.timeout, extra
                )
                self.runs.append(record)
                by_config[config.id] = record
                print(
                    f"{map_name:22s} {config.id:8s} {record['status']:4s} exit={record['exitCode']} "
                    f"vuids={len(record['vuids'])} warnings={record['warnings']['unique']} "
                    f"device={(record['device'] or {}).get('name') or (record['device'] or {}).get('renderer')}",
                    flush=True,
                )
            comparisons += self.compare_warnings(map_name, by_config, multiplayer)
        return comparisons

    def compare_warnings(
        self, map_name: str, by_config: dict[str, dict[str, Any]], multiplayer: bool
    ) -> list[dict[str, Any]]:
        if self.reference is None or self.reference not in by_config:
            return []
        reference = self.parsed[by_config[self.reference]["id"]].get("normalizedWarnings", set())
        comparisons = []
        for config_id, record in by_config.items():
            if config_id == self.reference or record["api"] != "vulkan":
                continue
            extra = self.parsed[record["id"]].get("normalizedWarnings", set()) - reference
            allowed = sorted(line for line in extra if multiplayer and is_nondeterministic_mp_warning(line))
            unexplained = sorted(extra - set(allowed))
            comparison = {
                "map": map_name,
                "config": config_id,
                "reference": self.reference,
                "extraWarnings": unexplained[:50],
                "extraWarningCount": len(unexplained),
                "nondeterministicWarnings": len(allowed),
                "status": "fail" if unexplained else "pass",
            }
            if unexplained:
                self.failures.append(
                    f"{map_name}/{config_id}: {len(unexplained)} warnings the {self.reference} "
                    f"reference does not log, first {unexplained[0]!r}"
                )
            comparisons.append(comparison)
        return comparisons

    def soak(self) -> dict[str, Any]:
        maps = self.args.maps or SOAK_MAPS
        stops = [map_name for _ in range(self.args.cycles) for map_name in maps]
        restart_stop = len(maps) // 2
        results = {}
        for config in self.configs:
            run_id = f"soak-{config.id}"
            cfgs: dict[str, list[str]] = {}
            half = self.args.frames_per_stop // 2
            for index, map_name in enumerate(stops):
                lines = [
                    "god",
                    "notarget",
                    "wait 60",
                    echo_marker(MEMORY_MARKER, index, "begin"),
                    "rendererVulkanMemoryInfo",
                    f"wait {half}",
                ]
                if index == restart_stop:
                    lines += ["vid_restart", "wait 120", echo_marker(RESTART_MARKER, index)]
                lines += [
                    f"wait {half}",
                    f'screenshot "screenshots/{run_id}_{index:02d}.tga"',
                    "wait 5",
                    echo_marker(MEMORY_MARKER, index, "end"),
                    "rendererVulkanMemoryInfo",
                    echo_marker(STOP_MARKER, index, map_name),
                    "wait 120",
                ]
                if index + 1 < len(stops):
                    lines += [
                        f"set g_autoExecAfterMapLoad vkq_{index + 1:02d}.cfg",
                        f"map {stops[index + 1]}",
                    ]
                else:
                    lines += ["gfxInfo", echo_marker(DONE_MARKER, run_id), "quit"]
                cfgs[f"vkq_{index:02d}.cfg"] = lines
            timeout = len(stops) * 600 + 300
            record = self.run_or_resume(
                run_id, config, stops[0], cfgs, "vkq_00.cfg", timeout,
                {"si_gameType": "singleplayer"}, sample_memory=True,
            )
            self.runs.append(record)
            results[config.id] = self.analyze_soak(record, maps, stops, restart_stop)
        return {
            "maps": list(maps),
            "cycles": self.args.cycles,
            "framesPerStop": self.args.frames_per_stop,
            "restartStop": restart_stop,
            "results": results,
        }

    def analyze_soak(
        self, record: dict[str, Any], maps: tuple[str, ...] | list[str], stops: list[str], restart_stop: int
    ) -> dict[str, Any]:
        parsed = self.parsed.get(record["id"], {})
        markers = parsed.get("markers", [])
        reached = [
            int(marker.split()[1]) for marker in markers if marker.startswith(STOP_MARKER + " ")
        ]
        restarted = any(marker.startswith(RESTART_MARKER + " ") for marker in markers)
        allocations: dict[int, int] = {}
        for report in parsed.get("heapReports", []):
            if report["phase"] == "begin" and report["heaps"]:
                allocations[report["stop"]] = sum(heap["allocations"] for heap in report["heaps"])
        per_map = []
        stop_memory = (record.get("memory") or {}).get("stops", {})
        for slot, map_name in enumerate(maps):
            indices = [slot + cycle * len(maps) for cycle in range(self.args.cycles)]
            per_map.append(
                {
                    "map": map_name,
                    "allocations": [allocations.get(index) for index in indices],
                    "processBytes": [
                        (stop_memory.get(str(index)) or {}).get("bytes") for index in indices
                    ],
                }
            )
        analysis = {
            "run": record["id"],
            "stopsReached": len(set(reached)),
            "expectedStops": len(stops),
            "restarted": restarted,
            "elapsedSeconds": record["elapsedSeconds"],
            "perMap": per_map,
            "maxAllocationGrowth": growth(per_map, "allocations"),
            "maxProcessGrowth": growth(per_map, "processBytes"),
        }
        problems = []
        if analysis["stopsReached"] != len(stops):
            problems.append(f"reached {analysis['stopsReached']} of {len(stops)} stops")
        if not restarted:
            problems.append("the vid_restart stop did not complete")
        if record["api"] == "vulkan" and analysis["maxAllocationGrowth"] is None:
            problems.append("no comparable Vulkan heap reports across cycles")
        if analysis["maxProcessGrowth"] is None:
            problems.append("no comparable process memory samples across cycles")
        analysis["status"] = "fail" if problems else "pass"
        for problem in problems:
            self.failures.append(f"{record['id']}: {problem}")
        return analysis

    def benchmark(self) -> dict[str, Any]:
        maps = self.args.maps or BENCHMARK_MAPS
        budgets, contract = load_budget_contract()
        rows: dict[str, dict[str, list[dict[str, Any]]]] = {}
        for repeat in range(self.args.repeats):
            for map_name in maps:
                short = map_name.split("/")[-1]
                for config in self.configs:
                    run_id = f"bench-{config.id}-{short}-{repeat}"
                    cfg = [
                        f"wait {self.args.settle_frames}",
                        "g_stopTime 1",
                        "wait 60",
                        "benchmark",
                        "wait 5",
                        "gfxInfo",
                        echo_marker(DONE_MARKER, run_id),
                        "quit",
                    ]
                    record = self.run_or_resume(
                        run_id, config, map_name, {"vkq.cfg": cfg}, "vkq.cfg", self.args.timeout,
                        {"si_gameType": "singleplayer"},
                    )
                    self.runs.append(record)
                    lines = self.parsed.get(record["id"], {}).get("benchmark", [])
                    full = max(lines, key=lambda line: line["kpix"]) if lines else None
                    record["benchmark"] = full
                    if full is None:
                        self.failures.append(f"{run_id}: no benchmark result line")
                    rows.setdefault(map_name, {}).setdefault(config.id, []).append(
                        {"run": run_id, **(full or {})}
                    )
                    print(f"{run_id:32s} {record['status']} {full}", flush=True)
        scenes = []
        for map_name, by_config in rows.items():
            for config in self.configs:
                runs = [run for run in by_config.get(config.id, []) if "p95Ms" in run]
                backend = "vulkan" if config.api == "vulkan" else "opengl"
                p95_budget, p99_budget = budgets.get(
                    (map_name, backend), (DEFAULT_BUDGET_P95_US, DEFAULT_BUDGET_P99_US)
                )
                scene = {
                    "map": map_name,
                    "config": config.id,
                    "api": config.api,
                    "runs": len(runs),
                    "medianAvgMs": round(statistics.median(r["avgMs"] for r in runs), 3) if runs else None,
                    "medianP95Ms": statistics.median(r["p95Ms"] for r in runs) if runs else None,
                    "medianP99Ms": statistics.median(r["p99Ms"] for r in runs) if runs else None,
                    "budgetP95Us": p95_budget,
                    "budgetP99Us": p99_budget,
                }
                within = (
                    bool(runs)
                    and scene["medianP95Ms"] * 1000 <= p95_budget
                    and scene["medianP99Ms"] * 1000 <= p99_budget
                )
                scene["status"] = "pass" if within else "fail"
                if not within:
                    self.failures.append(f"benchmark {map_name}/{config.id} is outside its budget: {scene}")
                scenes.append(scene)
        return {"repeats": self.args.repeats, "budgetContract": contract, "scenes": scenes}

    def run_or_resume(
        self,
        run_id: str,
        config: RunConfig,
        map_name: str,
        cfgs: dict[str, list[str]],
        first_cfg: str,
        timeout: int,
        extra_cvars: dict[str, str],
        sample_memory: bool = False,
    ) -> dict[str, Any]:
        record_path = self.output / "runs" / run_id / "record.json"
        if self.args.resume and record_path.is_file():
            record = json.loads(record_path.read_text(encoding="utf-8"))
            intact = record.get("status") == "pass" and all(
                (self.output / artifact["path"]).is_file()
                and sha256_file(self.output / artifact["path"]) == artifact["sha256"]
                for artifact in record.get("artifacts", [])
            )
            log = next((a for a in record.get("artifacts", []) if a.get("kind") == "engineLog"), None)
            if intact and log is not None:
                text = (self.output / log["path"]).read_text(encoding="utf-8", errors="replace")
                parsed = parse_engine_log(text)
                parsed["normalizedWarnings"] = normalize_warnings(
                    parsed["warnings"], warning_replacements(self.output / "runs" / run_id, run_id)
                )
                self.parsed[run_id] = parsed
                self.paks[run_id] = pak_inventory(parsed, self.basepath, self.runtime)
                for problem in record.get("problems", []):
                    self.failures.append(f"{run_id}: {problem}")
                print(f"{run_id}: resumed from its retained record", flush=True)
                return record
        return self.launch(run_id, config, map_name, cfgs, first_cfg, timeout, extra_cvars, sample_memory)


def growth(per_map: list[dict[str, Any]], key: str) -> float | None:
    """Largest last-cycle/first-cycle ratio of `key` across the soak's maps."""
    ratios = []
    for entry in per_map:
        values = entry[key]
        if len(values) < 2 or values[0] in (None, 0) or values[-1] is None:
            return None
        ratios.append(values[-1] / values[0])
    return round(max(ratios), 4) if ratios else None


def summarize_devices(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    devices: dict[str, dict[str, Any]] = {}
    for run in runs:
        device = run.get("device")
        if not device:
            continue
        key = json.dumps({"config": run["config"], **device}, sort_keys=True)
        devices.setdefault(key, {"config": run["config"], **device})
    return sorted(devices.values(), key=lambda device: (device["config"], json.dumps(device, sort_keys=True)))


def contact_sheets(output: Path, runs: list[dict[str, Any]]) -> None:
    try:
        from PIL import Image
    except ImportError:
        print("contact sheets skipped: Pillow is not installed", flush=True)
        return
    by_map: dict[str, list[Path]] = {}
    for run in runs:
        for artifact in run.get("artifacts", []):
            if artifact.get("kind") == "screenshot":
                by_map.setdefault(run["map"], []).append(output / artifact["path"])
    sheets = output / "sheets"
    sheets.mkdir(exist_ok=True)
    for map_name, paths in by_map.items():
        images = [Image.open(path).convert("RGB").resize((480, 270)) for path in paths[:6]]
        canvas = Image.new("RGB", (480 * len(images), 270))
        for index, image in enumerate(images):
            canvas.paste(image, (480 * index, 0))
        canvas.save(sheets / (map_name.replace("/", "_") + ".png"))


def run_suite(args: argparse.Namespace) -> int:
    sys.path.insert(0, str(SCRIPT_DIR))
    import renderer_validation_matrix as matrix

    output: Path = args.output_dir.resolve()
    runtime: Path = args.runtime_dir.resolve()
    if output.exists() and any(output.iterdir()) and not args.resume:
        print(f"output directory must be new or empty (or pass --resume): {output}", file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    if args.reference is not None and args.reference not in {c.id for c in args.configs}:
        print(f"--reference {args.reference} is not one of the configs", file=sys.stderr)
        return 2
    if args.reference is not None and next(c for c in args.configs if c.id == args.reference).api != "gl":
        print("--reference must name an OpenGL config", file=sys.stderr)
        return 2

    print(f"hashing the staged runtime {runtime} ...", flush=True)
    inventory = runtime_inventory(runtime)
    state_path = output / "state.json"
    settings = {
        "suite": args.suite,
        "evidenceScope": {
            "window": "hidden",
            "vulkanPresentation": "offscreen",
            "displayLatencyMeasured": False,
            "supportPromotion": False,
            "timing": "synchronous-offscreen-benchmark" if args.suite == "benchmark" else "functional",
        },
        "configs": [config.as_dict() for config in args.configs],
        "reference": args.reference,
        "maps": list(args.maps) if args.maps else None,
        "width": DEFAULT_WIDTH,
        "height": DEFAULT_HEIGHT,
        "validation": args.suite != "benchmark" and not args.no_validation,
        "timeoutSeconds": args.timeout,
        "settleFrames": args.settle_frames,
        "bots": args.bots,
        "cycles": args.cycles,
        "framesPerStop": args.frames_per_stop,
        "repeats": args.repeats,
        "basepath": args.basepath,
    }
    if args.resume and state_path.is_file():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        if state.get("runtime") != inventory or state.get("settings") != settings:
            print("--resume: the staged runtime or the suite settings changed; start a new output directory", file=sys.stderr)
            return 2
    state_path.write_text(json.dumps({"runtime": inventory, "settings": settings}, indent=2) + "\n", encoding="utf-8")

    qualification = Qualification(args)
    comparisons: list[dict[str, Any]] = []
    soak = None
    benchmark = None
    if args.suite in ("sp-sweep", "mp-sweep"):
        comparisons = qualification.sweep()
    elif args.suite == "soak":
        soak = qualification.soak()
    else:
        benchmark = qualification.benchmark()

    print("re-hashing the staged runtime ...", flush=True)
    unchanged = runtime_inventory(runtime) == inventory
    failures = qualification.failures
    if not unchanged:
        failures.append("the staged runtime changed during the suite")

    versions = sorted({run["version"] for run in qualification.runs if run.get("version")})
    source: dict[str, Any] = {"commit": None, "shortCommit": None, "version": None, "dirty": None}
    if len(versions) != 1:
        failures.append(f"runs report {len(versions)} different engine versions: {versions}")
    else:
        short, dirty = parse_version(versions[0])
        source.update(version=versions[0], shortCommit=short, dirty=dirty)
        if short is None:
            failures.append(f"version {versions[0]} carries no git SHA")
        elif dirty:
            failures.append(f"version {versions[0]} was built from a dirty tree")
        else:
            commit, error = resolve_source_commit(ROOT, short, args.source_commit)
            source["commit"] = commit
            if error:
                failures.append(error)

    pak_sets = {run.get("paksDigest") for run in qualification.runs}
    if len(pak_sets) != 1:
        failures.append("runs loaded different pak sets")
    paks = qualification.paks.get(qualification.runs[0]["id"], []) if qualification.runs else []
    if not any(pak["origin"] == "basepath" for pak in paks):
        failures.append("no retail pak was loaded from the basepath")

    if args.contact_sheets and args.suite in ("sp-sweep", "mp-sweep"):
        contact_sheets(output, qualification.runs)

    report = {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "kind": REPORT_KIND,
        "suite": args.suite,
        "status": "pass" if not failures else "fail",
        "generated": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "harness": harness_identity(ROOT, source["commit"]),
        "source": source,
        "host": {
            "platform": host_platform(),
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
            "cpu": next(
                (parsed["cpu"] for parsed in qualification.parsed.values() if parsed.get("cpu")), None
            ),
        },
        "runtime": {
            "executable": matrix.find_client_executable(runtime).name,
            "files": inventory,
            "unchangedAfterRuns": unchanged,
        },
        "assets": {"paks": paks},
        "settings": settings,
        "configs": [config.as_dict() for config in args.configs],
        "reference": args.reference,
        "devices": summarize_devices(qualification.runs),
        "runs": qualification.runs,
        "comparisons": comparisons,
        "soak": soak,
        "benchmark": benchmark,
        "failures": failures,
    }
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(f"{report['status']}: {len(qualification.runs)} runs, {len(failures)} failures -> {report_path}")
    for failure in failures[:40]:
        print("FAIL:", failure)
    return 0 if not failures else 1


def write_test_tga(path: Path, width: int, height: int, pattern: bool) -> None:
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0)
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            if pattern:
                pixels += bytes(((x * 7) % 256, (y * 5) % 256, ((x + y) * 3) % 256))
            else:
                pixels += b"\x00\x00\x00"
    path.write_bytes(header + bytes(pixels))


def run_self_test() -> int:
    failures: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    check(parse_version("0.13.2-dev+g9a07b853.dirty") == ("9a07b853", True), "dirty version")
    check(echo_marker(DONE_MARKER, "sp-vk-a") == f'echo "{DONE_MARKER} sp-vk-a"', "quoted echo marker")
    check(parse_version("0.13.2-dev+g9a07b853") == ("9a07b853", False), "clean version")
    check(parse_version("0.13.2") == (None, False), "release version without SHA")
    config = parse_config("intel=vulkan:r_vkDevice=2,r_multiSamples=4")
    check(config == RunConfig("intel", "vulkan", (("r_vkDevice", "2"), ("r_multiSamples", "4"))), "config")
    for bad in ("Intel=vulkan", "vk=metal", "vk=vulkan:r_renderApi=gl", "vk=vulkan:junk", "novalue",
                "vk=vulkan:R_HIDDENWINDOW=0", "vk=vulkan:r_fullscreen=1", "vk=vulkan:in_mouse=1",
                "vk=vulkan:r_vkValidation=0", "vk=vulkan:fs_savepath=elsewhere"):
        try:
            parse_config(bad)
        except ValueError:
            continue
        failures.append(f"config {bad!r} was accepted")

    log = "\n".join(
        [
            "openQ4 0.13.2-dev+g1234abcd",
            "Loaded pk4 C:\\Games\\Quake 4\\q4base\\pak001.pk4 with checksum 0xf2cbc998",
            "Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)",
            "Vulkan: device 0 'Example GPU' (queue family 0, timestampValidBits=64, timestampPeriod=1.000000ns)",
            "Vulkan: driver 'Example' '1.2.3' (driverID 4, driverVersion 0x12345678, vendorID 0x10de, "
            "deviceID 0x28e0, deviceType 2, api 1.4.325)",
            "^3WARNING: ^1Couldn't load image: foo",
            "^3WARNING: ^1Vulkan validation: Validation Error: [ VUID-vkCmdDraw-None-08600 ] bad",
            f"{MEMORY_MARKER} 0 begin",
            "Vulkan memory: heap 0 device-local used=1059.4 MiB budget=6364.8 MiB blocks=59 (1059.4 MiB) "
            "allocations=1931 (798.6 MiB)",
            "Vulkan memory: heap 1 host used=32.0 MiB budget=16263.4 MiB blocks=1 (32.0 MiB) "
            "allocations=2 (0.0 MiB)",
            f"{STOP_MARKER} 0 game/airdefense1",
            "kpix:  921  avg:  3.2ms p50:  3ms p95:  6ms p99:  8ms max:  9ms fps:307.7 frames:308",
            "kpix:  589  avg:  2.5ms p50:  2ms p95:  4ms p99:  5ms max:  6ms fps:397.6 frames:398",
            "Renderer API: requested=vulkan active=vulkan disposition=module",
            "********************",
            "ERROR: something broke",
            f"{DONE_MARKER} sp-vk-airdefense1",
        ]
    )
    parsed = parse_engine_log(log)
    check(parsed["commit"] == "1234abcd" and parsed["dirty"] is False, "log version")
    check(parsed["validationEnabled"], "validation enabled")
    check(parsed["validationMessages"] == 1, "validation message count")
    check(parsed["vuids"] == ["VUID-vkCmdDraw-None-08600"], "VUID list")
    check(parsed["fatal"] == ["ERROR: something broke"], "fatal lines")
    check(len(parsed["warnings"]) == 2 and parsed["warnings"][0] == "WARNING: Couldn't load image: foo", "warnings")
    check(parsed["renderApi"] == {"requested": "vulkan", "active": "vulkan"}, "render API")
    check(parsed["paks"] == [{"path": "C:\\Games\\Quake 4\\q4base\\pak001.pk4", "checksum": "0xf2cbc998"}], "paks")
    check(parsed["cpu"] is None and parse_engine_log("CPU: Example CPU (x64)")["cpu"] == "Example CPU (x64)", "CPU line")
    check(len(parsed["heapReports"]) == 1 and sum(h["allocations"] for h in parsed["heapReports"][0]["heaps"]) == 1933, "heaps")
    check(f"{DONE_MARKER} sp-vk-airdefense1" in parsed["markers"], "done marker")
    check(len(parsed["benchmark"]) == 2 and parsed["benchmark"][0]["p99Ms"] == 8, "benchmark lines")
    device = device_identity("vulkan", parsed)
    check(device is not None and device["vendorId"] == "0x10de" and device["physical"], "physical device")
    soft = parse_engine_log(log.replace("deviceType 2", "deviceType 4"))
    check(device_identity("vulkan", soft)["physical"] is False, "CPU device type is not physical")
    lavapipe = parse_engine_log(log.replace("driverID 4", "driverID 13"))
    check(device_identity("vulkan", lavapipe)["physical"] is False, "lavapipe is not physical")
    check(device_identity("vulkan", parse_engine_log("openQ4 1.0")) is None, "missing driver line")
    check(
        pak_inventory(parsed, "C:\\Games\\Quake 4", Path("C:/openQ4/.install"))[0]["origin"] == "basepath",
        "retail pak origin",
    )
    check(paks_digest([{"name": "a", "checksum": "0x1", "origin": "basepath"}])
          == paks_digest([{"origin": "basepath", "checksum": "0x1", "name": "a"}]), "pak digest is canonical")

    check(is_nondeterministic_mp_warning("WARNING: Loading non pre-cached playerModel decl model_player_marine"), "bot precache allowlist")
    check(is_nondeterministic_mp_warning("WARNING: player3: body 'LUpperLeg' stuck in 2 (normal = 0.1 0.2 0.9, depth = 1.0)"), "ragdoll allowlist")
    check(not is_nondeterministic_mp_warning("WARNING: Vulkan: restarting the renderer to recover presentation (1 of 3 this session)"), "renderer warnings are never allowlisted")
    check(not is_nondeterministic_mp_warning("WARNING: Loading non pre-cached material decl textures/foo extra"), "allowlist is anchored")
    normalized = normalize_warnings(["WARNING:  a  b C:\\run\\x"], {"C:\\run": "<run>"})
    check(normalized == {"WARNING: a b <run>\\x"}, "warning normalization")

    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vulkan-qualification-', dir=ROOT / '.tmp') as scratch:
        drawn = Path(scratch) / "drawn.tga"
        blank = Path(scratch) / "blank.tga"
        write_test_tga(drawn, 64, 36, True)
        write_test_tga(blank, 64, 36, False)
        check(screenshot_is_drawn(screenshot_stats(drawn)), "drawn capture")
        check(not screenshot_is_drawn(screenshot_stats(blank)), "blank capture")
        truncated = Path(scratch) / "truncated.tga"
        truncated.write_bytes(drawn.read_bytes()[:100])
        try:
            screenshot_stats(truncated)
            failures.append("truncated capture was accepted")
        except ValueError:
            pass

    per_map = [
        {"map": "a", "allocations": [100, 108], "processBytes": [1000, 1100]},
        {"map": "b", "allocations": [200, 201], "processBytes": [2000, 1900]},
    ]
    check(growth(per_map, "allocations") == 1.08 and growth(per_map, "processBytes") == 1.1, "soak growth")
    check(growth([{"map": "a", "allocations": [100, None]}], "allocations") is None, "incomplete soak growth")
    check(re.fullmatch(r"[a-z]+-(x64|arm64|x86)", host_platform()) is not None, "host platform name")

    for failure in failures:
        print("FAIL:", failure)
    print(f"renderer_vulkan_qualification self-test: {'pass' if not failures else 'fail'}")
    return 0 if not failures else 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("self-test", help="check log parsing and analysis on synthetic data")
    run = commands.add_parser("run", help="run one qualification suite")
    run.add_argument("--suite", choices=SUITES, required=True)
    run.add_argument("--output-dir", type=Path, required=True)
    run.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    run.add_argument("--basepath", required=True, help="retail Quake 4 directory the runs load")
    run.add_argument(
        "--config", dest="configs", action="append", type=parse_config,
        help="id=api[:cvar=value,...]; repeatable (default: vk=vulkan and gl=gl)",
    )
    run.add_argument("--reference", default=None, help="OpenGL config whose warnings Vulkan runs are compared with")
    run.add_argument("--maps", type=lambda text: tuple(m for m in text.split(",") if m), default=())
    run.add_argument("--source-commit", default=None, help="full SHA of the runtime's build (default: resolve its version-line SHA)")
    run.add_argument("--timeout", type=int, default=420, help="seconds per sweep or benchmark run")
    run.add_argument("--settle-frames", type=int, default=None)
    run.add_argument("--bots", type=int, default=DEFAULT_MP_BOTS)
    run.add_argument("--cycles", type=int, default=2)
    run.add_argument("--frames-per-stop", type=int, default=3600)
    run.add_argument("--repeats", type=int, default=5)
    run.add_argument("--no-validation", action="store_true", help="sweeps and soaks without validation layers")
    run.add_argument("--contact-sheets", action="store_true", help="write per-map capture strips (needs Pillow)")
    run.add_argument("--resume", action="store_true", help="reuse intact passing runs of an interrupted suite")
    args = parser.parse_args(argv)
    if args.command == "run":
        if not args.configs:
            args.configs = [parse_config("vk=vulkan"), parse_config("gl=gl")]
        ids = [config.id for config in args.configs]
        if len(set(ids)) != len(ids):
            parser.error("config ids must be unique")
        if args.reference is None and args.suite in ("sp-sweep", "mp-sweep"):
            args.reference = next((c.id for c in args.configs if c.api == "gl"), None)
        if args.settle_frames is None:
            args.settle_frames = {"mp-sweep": 400, "benchmark": 300}.get(args.suite, 120)
        args.basepath = str(Path(args.basepath).resolve())
        if not (Path(args.basepath) / "q4base").is_dir():
            parser.error(f"--basepath {args.basepath} has no q4base directory")
        if args.cycles < 2:
            parser.error("--cycles must be at least 2 so growth can be measured")
        if args.frames_per_stop < 240:
            parser.error("--frames-per-stop must be at least 240")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.command == "self-test":
        return run_self_test()
    return run_suite(args)


if __name__ == "__main__":
    raise SystemExit(main())
