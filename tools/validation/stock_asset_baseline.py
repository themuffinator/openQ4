#!/usr/bin/env python3
"""Capture and verify the retail-PK4 openQ4 SP/MP compatibility baseline.

The harness is intentionally non-interactive. It launches only windowed
clients, drives registered console commands through generated cfg files, and
uses the engine's ``screenshot`` command. It never controls or captures host
mouse/keyboard input and never uses an operating-system screen-capture API.
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
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from zipfile import BadZipFile, ZipFile

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from renderer_budget_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    evaluate_timing_evidence,
    load_contract,
    verify_contract_binding,
    verify_recorded_evidence,
)

SCHEMA_VERSION = 10
RETAIL_MANIFEST_SCHEMA_VERSION = 1
RUNTIME_MANIFEST_SCHEMA_VERSION = 2
ASSET_COMPATIBILITY_MODEL = "retail-pk4-fallback-with-packaged-openq4-overlays-v1"
GIT_PROVENANCE_POLICY = "current-openq4-head-and-dirty-state-v1"
TOP_LEVEL_FAILURE_ARRAYS = (
    "assetComparisonFailures",
    "preflightFailures",
    "postCaptureVerificationFailures",
)
SP_MAP = "game/storage1"
MP_MAP = "mp/q4dm1"
SP_SAVE_NAME = "StockBaselineSP"
SP_DEMO_NAME = "stock_baseline_sp"
BASELINE_DIR = "stock-baseline"
DEFAULT_WIDTH = 1280
DEFAULT_HEIGHT = 720
DISPLAY_CONTRACT_ID = "bordered-window-1280x720-v1"
SP_SAVE_PREVIEW_DIMENSIONS = (320, 240)
SP_SAVE_PREVIEW_ANALYSIS_DIMENSIONS = (80, 60)
SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION = 0.75
SP_SAVE_PREVIEW_MIN_EXPOSURE_GAIN = 0.25
SP_SAVE_PREVIEW_MAX_EXPOSURE_GAIN = 4.0
SP_SAVE_PREVIEW_MIN_EXPOSURE_BIAS = -192.0
SP_SAVE_PREVIEW_MAX_EXPOSURE_BIAS = 192.0
SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR = 48.0
SP_SAVE_PREVIEW_COMPARISON_ALGORITHM = (
    "same-state-center-aspect-bilinear-luma-affine-rgb-v3"
)
MP_CLIENT_ACTIVE_MARKER = "OPENQ4_STOCK_BASELINE_MP_CLIENT_ACTIVE"
MP_CLIENT_ACTIVE_PATTERN = re.compile(
    rf"^{MP_CLIENT_ACTIVE_MARKER} client=\d+ spectating=0 wantSpectate=0 ingame=1 menu=0 disableHud=0$",
    re.MULTILINE,
)
MP_CLIENT_VIEW_MARKER = "OPENQ4_STOCK_BASELINE_MP_CLIENT_VIEW"
MP_CLIENT_VIEW_PATTERN = re.compile(
    rf"^{MP_CLIENT_VIEW_MARKER} gui=0$",
    re.MULTILINE,
)
MP_CLIENT_SCREENSHOT_WRITE_PATTERN = re.compile(
    rf"^Wrote screenshots/{BASELINE_DIR}/mp_client\.tga$",
    re.MULTILINE,
)
FATAL_PATTERNS = {
    "fatal": re.compile(r"\bFatal Error\b|^[ \t]*(?:\*+[ \t]*)?FATAL[ \t]*:", re.IGNORECASE | re.MULTILINE),
    "engineError": re.compile(r"^[ \t]*(?:\*+[ \t]*)?ERROR(?:[ \t]*:|[ \t]*$)", re.MULTILINE),
    "shaderFailure": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
    "vulkanValidation": re.compile(r"\bVulkan validation:|\bVUID-[A-Za-z0-9_.-]+\b", re.IGNORECASE),
    "graphicsError": re.compile(r"\bGL_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY|CONTEXT_LOST)\b|OpenGL\s+error", re.IGNORECASE),
}
RUNTIME_WINDOW_MODE_PATTERN = re.compile(
    r"^MODE:\s*([^,\r\n]+),\s*(\d+)\s+x\s+(\d+)\s+"
    r"(windowed|borderless|fullscreen)\b",
    re.IGNORECASE | re.MULTILINE,
)


def display_contract() -> dict[str, Any]:
    return {
        "contractId": DISPLAY_CONTRACT_ID,
        "width": DEFAULT_WIDTH,
        "height": DEFAULT_HEIGHT,
        "cvars": {
            "r_fullscreen": "0",
            "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1",
            "r_fullscreenDesktop": "0",
            "r_windowWidth": str(DEFAULT_WIDTH),
            "r_windowHeight": str(DEFAULT_HEIGHT),
            "r_mode": "-1",
            "r_customWidth": str(DEFAULT_WIDTH),
            "r_customHeight": str(DEFAULT_HEIGHT),
        },
    }


ROLE_EVIDENCE_CONTRACT: dict[str, dict[str, Any]] = {
    "sp-capture": {
        "marker": "OPENQ4_STOCK_BASELINE_SP_SAVE_LOAD_COMPLETE",
        "saveDir": "sp",
        "logName": "stock_baseline_sp.log",
        "budget": {"map": SP_MAP, "backend": "opengl", "profile": "baseline"},
        "expected": {
            "screenshot": f"baseoq4/screenshots/{BASELINE_DIR}/sp_after_load.tga",
            "saveReferenceScreenshot": (
                f"baseoq4/screenshots/{BASELINE_DIR}/sp_before_save.tga"
            ),
            "renderDemo": f"baseoq4/demos/{SP_DEMO_NAME}.demo",
            "savePayload": f"baseoq4/savegames/{SP_SAVE_NAME}.save",
            "savePreview": f"baseoq4/savegames/{SP_SAVE_NAME}.tga",
            "saveDescription": f"baseoq4/savegames/{SP_SAVE_NAME}.txt",
        },
    },
    "sp-demo-playback": {
        "marker": "frames rendered in",
        "saveDir": "sp",
        "logName": "stock_baseline_demo_playback.log",
        "expected": {},
    },
    "mp-server": {
        "marker": "OPENQ4_STOCK_BASELINE_MP_SERVER_COMPLETE",
        "saveDir": "mp-server",
        "logName": "stock_baseline_server.log",
        "budget": {"map": MP_MAP, "backend": "opengl", "profile": "baseline"},
        "expected": {
            "screenshot": f"baseoq4/screenshots/{BASELINE_DIR}/mp_server.tga",
            "renderDemo": f"baseoq4/demos/stock_baseline_mp_server.demo",
        },
    },
    "mp-client": {
        "marker": "OPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE",
        "requiredMarkers": [MP_CLIENT_ACTIVE_MARKER, MP_CLIENT_VIEW_MARKER],
        "saveDir": "mp-client",
        "logName": "stock_baseline_client.log",
        "budget": {"map": MP_MAP, "backend": "opengl", "profile": "baseline"},
        "expected": {
            "screenshot": f"baseoq4/screenshots/{BASELINE_DIR}/mp_client.tga",
            "renderDemo": f"baseoq4/demos/stock_baseline_mp_client.demo",
        },
    },
}


@dataclass
class RolePlan:
    role_id: str
    mode: str
    savepath: Path
    log_name: str
    marker: str
    args: list[str]
    expected: list[tuple[str, str]] = field(default_factory=list)
    stdout_path: Path | None = None
    stderr_path: Path | None = None


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def prepare_output_directory(output_dir: Path) -> None:
    if output_dir.exists():
        if not output_dir.is_dir():
            raise ValueError(f"evidence output path is not a directory: {output_dir}")
        if is_link_or_junction(output_dir):
            raise ValueError(f"evidence output directory must not be a link or junction: {output_dir}")
        if any(output_dir.iterdir()):
            raise ValueError(f"evidence output directory must be new or empty: {output_dir}")
    else:
        output_dir.mkdir(parents=True)


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        return "x64"
    if machine in {"arm64", "aarch64"}:
        return "arm64"
    return machine


def default_asset_root() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, root: Path) -> dict[str, Any]:
    return {
        "path": path.relative_to(root).as_posix(),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def is_link_or_junction(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    if path.is_symlink() or bool(is_junction and is_junction()):
        return True
    try:
        # Python before 3.12 has no Path.is_junction().  Inspect the path's own
        # Windows reparse-point bit instead of comparing resolved paths: every
        # ordinary child below an explicitly allowed junction resolves through
        # that parent and must not be misclassified as a nested link.
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except OSError:
        return False


def validate_asset_root(asset_root: Path, allow_asset_dir_links: bool = False) -> None:
    if not asset_root.is_dir():
        raise FileNotFoundError(f"Quake 4 asset root does not exist: {asset_root}")
    direct_game_dir = asset_root / "baseoq4"
    if direct_game_dir.is_dir() and any(direct_game_dir.iterdir()):
        raise ValueError(
            f"asset root contains non-empty {direct_game_dir}; fs_game=baseoq4 would load "
            "that unapproved content ahead of the stock q4base fallback. Use a clean asset "
            "view containing q4base/q4mp only."
        )
    for game_dir_name in ("q4base", "q4mp"):
        game_dir = asset_root / game_dir_name
        if game_dir.exists() and is_link_or_junction(game_dir) and not allow_asset_dir_links:
            raise ValueError(
                f"asset directory is a symlink or junction: {game_dir}; pass "
                "--allow-asset-dir-links only for an intentionally constructed clean view"
            )


def asset_directory_views(asset_root: Path) -> list[dict[str, Any]]:
    views: list[dict[str, Any]] = []
    for game_dir_name in ("q4base", "q4mp"):
        game_dir = asset_root / game_dir_name
        if game_dir.exists():
            views.append(
                {
                    "path": game_dir_name,
                    "linked": is_link_or_junction(game_dir),
                    "resolvedTarget": str(game_dir.resolve()),
                }
            )
    return views


def collect_pk4s(
    asset_root: Path, allow_asset_dir_links: bool = False
) -> list[dict[str, Any]]:
    validate_asset_root(asset_root, allow_asset_dir_links)
    files: list[Path] = []
    for game_dir_name in ("q4base", "q4mp"):
        game_dir = asset_root / game_dir_name
        if game_dir.is_dir():
            files.extend(path for path in game_dir.iterdir() if path.is_file() and path.suffix.casefold() == ".pk4")
    files.sort(key=lambda path: path.relative_to(asset_root).as_posix().casefold())
    if not files:
        raise FileNotFoundError(f"no retail PK4 files found under {asset_root}/q4base or q4mp")
    if not any(item.relative_to(asset_root).as_posix().casefold() == "q4base/pak001.pk4" for item in files):
        raise FileNotFoundError("retail asset set is missing q4base/pak001.pk4")
    return [file_record(path, asset_root) for path in files]


def collect_loose_asset_files(
    asset_root: Path, allow_asset_dir_links: bool = False
) -> list[dict[str, Any]]:
    """Hash every non-PK4 file below q4base/q4mp that the fallback can see."""
    validate_asset_root(asset_root, allow_asset_dir_links)
    files: list[Path] = []
    for game_dir_name in ("q4base", "q4mp"):
        game_dir = asset_root / game_dir_name
        if not game_dir.is_dir():
            continue
        for parent_name, dir_names, file_names in os.walk(game_dir, followlinks=False):
            parent = Path(parent_name)
            for dir_name in list(dir_names):
                directory = parent / dir_name
                if is_link_or_junction(directory):
                    raise ValueError(f"loose asset tree contains a symlink or junction: {directory}")
            for file_name in file_names:
                path = parent / file_name
                if is_link_or_junction(path):
                    raise ValueError(f"loose asset tree contains a symlink or junction: {path}")
                if path.suffix.casefold() != ".pk4":
                    files.append(path)
    files.sort(key=lambda path: path.relative_to(asset_root).as_posix().casefold())
    return [file_record(path, asset_root) for path in files]


def collect_overlay_pk4s(runtime_dir: Path) -> list[dict[str, Any]]:
    game_dir = runtime_dir / "baseoq4"
    if not game_dir.is_dir():
        return []
    files = sorted(
        (path for path in game_dir.iterdir() if path.is_file() and path.suffix.casefold() == ".pk4"),
        key=lambda path: path.name.casefold(),
    )
    return [file_record(path, runtime_dir) for path in files]


def normalized_pk4_member_path(name: str, archive_path: Path) -> str:
    """Return the case-preserving VFS path for one ordinary PK4 member."""

    normalized = name.replace("\\", "/")
    parts = normalized.split("/")
    if (
        not normalized
        or normalized.startswith("/")
        or any(part in {"", ".", ".."} for part in parts)
        or ":" in parts[0]
    ):
        raise ValueError(f"PK4 contains an unsafe or ambiguous member path: {archive_path}: {name!r}")
    return "/".join(parts)


def pk4_member_index(
    root: Path, records: list[dict[str, Any]], label: str
) -> dict[str, dict[str, Any]]:
    """Index archive members by the case-insensitive idTech virtual path."""

    index: dict[str, dict[str, Any]] = {}
    for record in records:
        relative = str(record.get("path", ""))
        relative_path = Path(relative)
        if not relative or relative_path.is_absolute() or ".." in relative_path.parts:
            raise ValueError(f"{label} manifest contains an unsafe PK4 path: {relative!r}")
        archive_path = root / relative_path
        if not archive_path.is_file():
            raise FileNotFoundError(f"{label} PK4 is missing: {archive_path}")
        try:
            with ZipFile(archive_path) as archive:
                for member in archive.infolist():
                    if member.is_dir():
                        continue
                    member_path = normalized_pk4_member_path(member.filename, archive_path)
                    key = member_path.casefold()
                    entry = index.setdefault(
                        key,
                        {"path": member_path, "archives": set()},
                    )
                    if member_path.casefold() == str(entry["path"]).casefold():
                        entry["path"] = min(str(entry["path"]), member_path)
                    entry["archives"].add(relative.replace("\\", "/"))
        except BadZipFile as exc:
            raise ValueError(f"{label} PK4 is not a readable ZIP archive: {archive_path}") from exc
    return index


def collect_retail_path_collisions(
    asset_root: Path,
    stock_pk4s: list[dict[str, Any]],
    runtime_dir: Path,
    overlay_pk4s: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Inventory packaged openQ4 members that supersede retail virtual paths."""

    if not overlay_pk4s:
        return []
    retail_index = pk4_member_index(asset_root, stock_pk4s, "retail")
    overlay_index = pk4_member_index(runtime_dir, overlay_pk4s, "openQ4 overlay")
    collisions: list[dict[str, Any]] = []
    for key in sorted(set(retail_index) & set(overlay_index)):
        retail = retail_index[key]
        overlay = overlay_index[key]
        collisions.append(
            {
                "path": str(retail["path"]),
                "retailPk4s": sorted(retail["archives"], key=str.casefold),
                "openQ4OverlayPk4s": sorted(overlay["archives"], key=str.casefold),
            }
        )
    collisions.sort(key=lambda item: str(item["path"]).casefold())
    return collisions


def is_shared_library(path: Path) -> bool:
    name = path.name.casefold()
    return name.endswith((".dll", ".dylib")) or name.endswith(".so") or ".so." in name


def runtime_kind(path: Path, executable: Path) -> str:
    if path.resolve() == executable.resolve():
        return "clientExecutable"
    name = path.name.casefold()
    if name.startswith("openq4-ded_"):
        return "dedicatedServerExecutable" if path.suffix.casefold() != ".pdb" else "diagnosticSymbols"
    if name.startswith("openq4-client_") and path.suffix.casefold() == ".pdb":
        return "diagnosticSymbols"
    if name.startswith("renderer-gl_"):
        return "rendererModuleOpenGL" if path.suffix.casefold() != ".pdb" else "diagnosticSymbols"
    if name.startswith("renderer-vk_"):
        return "rendererModuleVulkan" if path.suffix.casefold() != ".pdb" else "diagnosticSymbols"
    if name.startswith("game_"):
        # one module serves both game_sp and game_mp
        return "gameModule" if path.suffix.casefold() != ".pdb" else "diagnosticSymbols"
    if path.suffix.casefold() == ".pdb":
        return "diagnosticSymbols"
    return "runtimeLibrary"


def collect_runtime_files(runtime_dir: Path, executable: Path) -> list[dict[str, Any]]:
    """Hash the selected client and every packaged shared library it may load."""
    candidates = {executable.resolve(): executable}
    for parent_name, dir_names, file_names in os.walk(runtime_dir, followlinks=False):
        parent = Path(parent_name)
        for dir_name in list(dir_names):
            directory = parent / dir_name
            if is_link_or_junction(directory):
                raise ValueError(f"runtime tree contains a symlink or junction: {directory}")
        for file_name in file_names:
            path = parent / file_name
            if is_link_or_junction(path):
                raise ValueError(f"runtime tree contains a symlink or junction: {path}")
            lower_name = path.name.casefold()
            if (
                is_shared_library(path)
                or lower_name.startswith("openq4-client_")
                or lower_name.startswith("openq4-ded_")
                or path.suffix.casefold() == ".pdb"
            ):
                candidates[path.resolve()] = path

    records = [
        {"kind": runtime_kind(path, executable), **file_record(path, runtime_dir)}
        for path in candidates.values()
    ]
    records.sort(key=lambda item: (item["kind"], item["path"].casefold()))
    kinds = {item["kind"] for item in records}
    required = {"clientExecutable", "gameModule"}
    if sys.platform != "darwin":
        required.add("rendererModuleOpenGL")
    missing = sorted(required - kinds)
    if missing:
        raise FileNotFoundError("runtime package is missing: " + ", ".join(missing))
    return records


def collect_overlay_loose_files(
    runtime_dir: Path, runtime_files: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], list[str]]:
    """Hash loose fs_game files and reject content outside the minimal package surface."""
    game_dir = runtime_dir / "baseoq4"
    if not game_dir.is_dir():
        return [], [f"openQ4 overlay directory is missing: {game_dir}"]
    runtime_paths = {item["path"].casefold() for item in runtime_files}
    files: list[Path] = []
    for parent_name, dir_names, file_names in os.walk(game_dir, followlinks=False):
        parent = Path(parent_name)
        for dir_name in list(dir_names):
            directory = parent / dir_name
            if is_link_or_junction(directory):
                raise ValueError(f"openQ4 overlay contains a symlink or junction: {directory}")
        for file_name in file_names:
            path = parent / file_name
            if is_link_or_junction(path):
                raise ValueError(f"openQ4 overlay contains a symlink or junction: {path}")
            if path.suffix.casefold() != ".pk4":
                files.append(path)
    files.sort(key=lambda path: path.relative_to(runtime_dir).as_posix().casefold())
    records = [file_record(path, runtime_dir) for path in files]
    allowed = runtime_paths | {"baseoq4/mod.json"}
    unexpected = [
        f"unexpected loose openQ4 overlay file: {item['path']}"
        for item in records
        if item["path"].casefold() not in allowed
        and not (
            Path(item["path"]).parent.as_posix().casefold() == "baseoq4"
            and Path(item["path"]).suffix.casefold() == ".pdb"
        )
    ]
    return records, unexpected


def asset_manifest_from_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    records = payload.get("stockPk4s")
    if records is None and isinstance(payload.get("assets"), dict):
        records = payload["assets"].get("stockPk4s")
    if not isinstance(records, list):
        raise ValueError("expected asset manifest does not contain a stockPk4s array")
    return records


def loose_manifest_from_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    records = payload.get("looseFiles")
    if records is None and isinstance(payload.get("assets"), dict):
        records = payload["assets"].get("looseFiles")
    if records is None:
        return []
    if not isinstance(records, list):
        raise ValueError("expected asset manifest looseFiles field is not an array")
    return records


def compare_file_records(
    expected: list[dict[str, Any]], actual: list[dict[str, Any]], label: str
) -> list[str]:
    failures: list[str] = []
    expected_by_path = {str(item.get("path", "")).casefold(): item for item in expected}
    actual_by_path = {str(item.get("path", "")).casefold(): item for item in actual}
    for path in sorted(set(expected_by_path) - set(actual_by_path)):
        failures.append(f"missing {label}: {expected_by_path[path].get('path', path)}")
    for path in sorted(set(actual_by_path) - set(expected_by_path)):
        failures.append(f"unexpected {label}: {actual_by_path[path].get('path', path)}")
    for path in sorted(set(expected_by_path) & set(actual_by_path)):
        expected_item = expected_by_path[path]
        actual_item = actual_by_path[path]
        for key in ("size", "sha256"):
            if expected_item.get(key) != actual_item.get(key):
                failures.append(
                    f"{label} {actual_item.get('path', path)} {key} differs: "
                    f"expected {expected_item.get(key)!r}, got {actual_item.get(key)!r}"
                )
    return failures


def compare_asset_records(expected: list[dict[str, Any]], actual: list[dict[str, Any]]) -> list[str]:
    return compare_file_records(expected, actual, "retail PK4")


def find_client(runtime_dir: Path) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    preferred = runtime_dir / f"openQ4-client_{host_arch()}{suffix}"
    if preferred.is_file():
        return preferred
    candidates = sorted(runtime_dir.glob(f"openQ4-client_*{suffix}"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"openQ4 client executable not found under {runtime_dir}")


def validate_runtime_dir(runtime_dir: Path, source_root: Path) -> Path:
    """Require canonical .install or an ordinary isolated temporary package."""
    source_absolute = source_root.absolute()
    runtime_absolute = runtime_dir.absolute()
    if is_link_or_junction(source_absolute):
        raise ValueError(f"source root must not be a link or junction: {source_absolute}")
    try:
        relative_to_source = runtime_absolute.relative_to(source_absolute)
    except ValueError as exc:
        raise ValueError(
            f"runtime directory must stay below the source root {source_absolute}: {runtime_absolute}"
        ) from exc
    current = source_absolute
    for part in relative_to_source.parts:
        current /= part
        if current.exists() and is_link_or_junction(current):
            raise ValueError(
                f"runtime directory ancestry must not contain a link or junction: {current}"
            )
    resolved = runtime_absolute.resolve()
    if not resolved.is_dir():
        raise FileNotFoundError(f"runtime directory does not exist: {resolved}")
    canonical = (source_root / ".install").resolve()
    temporary_parent = (source_root / ".tmp" / "stock-runtime").resolve()
    if resolved != canonical:
        try:
            relative = resolved.relative_to(temporary_parent)
        except ValueError as exc:
            raise ValueError(
                f"alternate runtime directory must stay below {temporary_parent}: {resolved}"
            ) from exc
        if not relative.parts:
            raise ValueError(
                f"alternate runtime directory must be a named child below {temporary_parent}"
            )
    return resolved


def add_set(args: list[str], name: str, value: Any) -> None:
    args.extend(("+set", name, str(value)))


def add_command(args: list[str], name: str, *values: Any) -> None:
    args.append("+" + name)
    args.extend(str(value) for value in values)


def write_cfg(savepath: Path, relative: str, lines: list[str]) -> None:
    payload = "\n".join(lines) + "\n"
    # baseoq4 is authoritative; q4base mirrors the file for legacy path
    # diagnostics without adding any asset override to the repository.
    for game_dir in ("baseoq4", "q4base"):
        path = savepath / game_dir / Path(relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(payload, encoding="utf-8")


def display_cfg_lines(width: int, height: int) -> list[str]:
    """Reassert the measured display contract after any game-module reload."""
    return [
        "r_rendererSharedGui 0",
        "r_rendererSharedInWorldGui 0",
        "r_rendererSharedCinematicPost 0",
        "r_rendererSharedSpecialFrame 0",
        "r_rendererSharedWorldAmbient 0",
        "r_rendererSharedWorldInteraction 0",
        "r_rendererSharedWorldFogBlend 0",
        "r_rendererSharedSubview 0",
        "r_rendererSharedDeform 0",
        "r_fullscreen 0",
        "r_borderless 0",
        "r_borderlessDefaultMigrated 1",
        "r_fullscreenDesktop 0",
        f"r_windowWidth {width}",
        f"r_windowHeight {height}",
        "r_mode -1",
        f"r_customWidth {width}",
        f"r_customHeight {height}",
    ]


def common_args(
    runtime_dir: Path,
    asset_root: Path,
    savepath: Path,
    log_name: str,
    cfg_path: str | None,
    width: int,
    height: int,
) -> list[str]:
    args: list[str] = []
    add_set(args, "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    add_set(args, "logFile", 2)
    add_set(args, "logFileName", f"logs/{log_name}")
    add_set(args, "developer", 1)
    add_set(args, "r_ignoreGLErrors", 0)
    add_set(args, "r_fullscreen", 0)
    add_set(args, "in_mouse", 0)
    add_set(args, "r_borderless", 0)
    # Suppress the one-time legacy migration before the renderer starts.  A
    # queued r_borderless=0 alone is too late: the migration otherwise creates
    # the initial window as desktop-sized borderless before command execution.
    add_set(args, "r_borderlessDefaultMigrated", 1)
    add_set(args, "r_fullscreenDesktop", 0)
    add_set(args, "r_windowWidth", width)
    add_set(args, "r_windowHeight", height)
    add_set(args, "r_mode", -1)
    add_set(args, "r_customWidth", width)
    add_set(args, "r_customHeight", height)
    add_set(args, "r_swapInterval", 0)
    add_set(args, "r_renderApi", "gl")
    add_set(args, "r_rendererSharedGui", 0)
    add_set(args, "r_rendererSharedInWorldGui", 0)
    add_set(args, "r_rendererSharedCinematicPost", 0)
    add_set(args, "r_rendererSharedSpecialFrame", 0)
    add_set(args, "r_rendererSharedWorldAmbient", 0)
    add_set(args, "r_rendererSharedWorldInteraction", 0)
    add_set(args, "r_rendererSharedWorldFogBlend", 0)
    add_set(args, "r_rendererSharedSubview", 0)
    add_set(args, "r_rendererSharedDeform", 0)
    add_set(args, "r_rendererBenchmarkPreset", "baseline")
    add_set(args, "r_rendererMetrics", 0)
    add_set(args, "r_rendererGpuTimers", 1)
    add_set(args, "com_maxfps", 240)
    add_set(args, "com_skipLoadingContinue", 1)
    add_set(args, "com_loadingContinueAutoAdvance", 1)
    # Milestone B evidence records the exact cache/preload mode and its bounded
    # per-generation counters in every role log.
    add_set(args, "com_levelLoadModernization", 1)
    add_set(args, "com_levelLoadCache", 1)
    add_set(args, "com_levelLoadCacheWrite", 1)
    add_set(args, "com_levelLoadPreload", 1)
    add_set(args, "com_levelLoadCacheReport", 1)
    add_set(args, "g_autoSkipCinematics", 1)
    add_set(args, "g_autoScreenshot", 0)
    add_set(args, "sv_cheats", 1)
    if cfg_path:
        add_set(args, "g_autoExecAfterMapLoad", cfg_path)
        add_set(args, "g_autoExecAfterMapLoadDelayMs", 1000)
    add_set(args, "fs_basepath", asset_root)
    add_set(args, "fs_savepath", savepath)
    # The process is launched with this exact recorded runtime directory as its
    # working directory, so the engine's locked fs_cdpath mounts that staged
    # package. Keep fs_devpath in the isolated evidence tree: retail-style
    # generated collision caches and other developer outputs must never mutate
    # the package after it is hashed.
    add_set(args, "fs_devpath", savepath)
    add_set(args, "fs_game", "baseoq4")
    # Re-apply the complete display contract after queued +set commands.  This
    # makes the renderer state, not only the eventual CVar values, authoritative.
    add_command(args, "vid_restart")
    return args


def expected_role_arguments(
    role: str,
    runtime_dir: Path,
    asset_root: Path,
    output_dir: Path,
    width: int,
    height: int,
    mp_port: int,
) -> list[str]:
    """Build the one canonical command line accepted for an evidence role."""

    if role not in ROLE_EVIDENCE_CONTRACT:
        raise ValueError(f"unknown baseline role: {role}")
    contract = ROLE_EVIDENCE_CONTRACT[role]
    savepath = output_dir / "savepaths" / contract["saveDir"]
    cfg_by_role = {
        "sp-capture": f"{BASELINE_DIR}/sp_stage1.cfg",
        "sp-demo-playback": None,
        "mp-server": f"{BASELINE_DIR}/server.cfg",
        "mp-client": f"{BASELINE_DIR}/client.cfg",
    }
    args = common_args(
        runtime_dir,
        asset_root,
        savepath,
        contract["logName"],
        cfg_by_role[role],
        width,
        height,
    )
    if role == "sp-capture":
        add_set(args, "si_gameType", "singleplayer")
        add_command(args, "map", SP_MAP)
    elif role == "sp-demo-playback":
        add_command(args, "gfxInfo")
        add_command(args, "timeDemoQuit", SP_DEMO_NAME)
    else:
        # Automated MP evidence always exercises the archived auto-join path.
        # Join/spectator-menu tests are separate and explicitly set this to 0.
        restart_index = args.index("+vid_restart")
        args[restart_index:restart_index] = ("+set", "ui_autoJoin", "1")
        if role == "mp-server":
            add_set(args, "net_serverDedicated", 0)
            add_set(args, "net_port", mp_port)
            add_set(args, "si_pure", 1)
            add_set(args, "net_serverAllowServerMod", 0)
            add_set(args, "si_gameType", "DM")
            add_command(args, "spawnServer", MP_MAP)
        else:
            add_set(args, "ui_name", "StockBaselineClient")
            add_command(args, "connect", f"127.0.0.1:{mp_port}")
    return args


def prepare_plans(
    runtime_dir: Path,
    asset_root: Path,
    output_dir: Path,
    width: int,
    height: int,
    mp_port: int,
) -> dict[str, RolePlan]:
    plans: dict[str, RolePlan] = {}

    sp_savepath = output_dir / "savepaths" / "sp"
    sp_stage1 = f"{BASELINE_DIR}/sp_stage1.cfg"
    sp_stage2 = f"{BASELINE_DIR}/sp_after_load.cfg"
    write_cfg(
        sp_savepath,
        sp_stage1,
        [
            "waitMsec 5000",
            "god",
            "notarget",
            f"recordDemo {SP_DEMO_NAME}",
            "waitMsec 2500",
            "stopRecording",
            f'screenshot "screenshots/{BASELINE_DIR}/sp_before_save.tga"',
            f"saveGame {SP_SAVE_NAME}",
            "waitMsec 1000",
            f'set g_autoExecAfterMapLoad "{sp_stage2}"',
            "set g_autoExecAfterMapLoadDelayMs 1000",
            f"loadGame {SP_SAVE_NAME}",
        ],
    )
    write_cfg(
        sp_savepath,
        sp_stage2,
        [
            "echo OPENQ4_STOCK_BASELINE_SP_SAVE_LOAD_COMPLETE",
            "waitMsec 2500",
            *display_cfg_lines(width, height),
            "framePacingReset",
            "r_rendererMetrics 1",
            "waitMsec 2500",
            "rendererBenchmarkCapture",
            "r_rendererMetrics 0",
            "framePacingSnapshot",
            "gfxInfo",
            f'screenshot "screenshots/{BASELINE_DIR}/sp_after_load.tga"',
            "wait 5",
            "quit",
        ],
    )
    sp_args = expected_role_arguments(
        "sp-capture", runtime_dir, asset_root, output_dir, width, height, mp_port
    )
    plans["sp-capture"] = RolePlan(
        "sp-capture",
        "SP",
        sp_savepath,
        "stock_baseline_sp.log",
        "OPENQ4_STOCK_BASELINE_SP_SAVE_LOAD_COMPLETE",
        sp_args,
        [
            ("screenshot", f"baseoq4/screenshots/{BASELINE_DIR}/sp_after_load.tga"),
            (
                "saveReferenceScreenshot",
                f"baseoq4/screenshots/{BASELINE_DIR}/sp_before_save.tga",
            ),
            ("renderDemo", f"baseoq4/demos/{SP_DEMO_NAME}.demo"),
            ("savePayload", f"baseoq4/savegames/{SP_SAVE_NAME}.save"),
            ("savePreview", f"baseoq4/savegames/{SP_SAVE_NAME}.tga"),
            ("saveDescription", f"baseoq4/savegames/{SP_SAVE_NAME}.txt"),
        ],
    )

    playback_args = expected_role_arguments(
        "sp-demo-playback",
        runtime_dir,
        asset_root,
        output_dir,
        width,
        height,
        mp_port,
    )
    plans["sp-demo-playback"] = RolePlan(
        "sp-demo-playback",
        "SP demo playback",
        sp_savepath,
        "stock_baseline_demo_playback.log",
        "frames rendered in",
        playback_args,
    )

    for role_id, role_name, wait_msec in (
        # Leave enough headroom for a completely cold loopback client to build
        # its isolated binary image/animation and collision caches before the
        # listen server records its own evidence and exits. The server still
        # finishes inside the shared four-minute default role timeout after a
        # representative cold q4dm1 load.
        ("mp-server", "server", 120000),
        # At roughly 48-50 fps, five seconds does not replace all 256 timing
        # samples retained across the first active post-load frame. Give the
        # client a full steady-state window before enforcing renderer budgets.
        ("mp-client", "client", 10000),
    ):
        savepath = output_dir / "savepaths" / role_id
        cfg = f"{BASELINE_DIR}/{role_name}.cfg"
        marker = f"OPENQ4_STOCK_BASELINE_MP_{role_name.upper()}_COMPLETE"
        demo_name = f"stock_baseline_mp_{role_name}"
        role_commands = [
            *display_cfg_lines(width, height),
            "framePacingReset",
            "r_rendererMetrics 1",
            "r_rendererGpuTimers 1",
            f"waitMsec {wait_msec}",
        ]
        if role_name == "client":
            role_commands.extend(
                (
                    "openq4_assertMPClientActive",
                    "openq4_assertMPGameplayView",
                )
            )
        role_commands.extend(
            (
                f"recordDemo {demo_name}",
                "waitMsec 2500",
                "stopRecording",
                "rendererBenchmarkCapture",
                "r_rendererMetrics 0",
                "framePacingSnapshot",
                "gfxInfo",
            )
        )
        if role_name == "client":
            role_commands.extend(
                ("openq4_assertMPClientActive", "openq4_assertMPGameplayView")
            )
        role_commands.append(f'screenshot "screenshots/{BASELINE_DIR}/mp_{role_name}.tga"')
        if role_name == "client":
            role_commands.extend(
                ("openq4_assertMPClientActive", "openq4_assertMPGameplayView")
            )
        role_commands.extend((f"echo {marker}", "wait 5", "quit"))
        write_cfg(
            savepath,
            cfg,
            role_commands,
        )
        log_name = f"stock_baseline_{role_name}.log"
        role_args = expected_role_arguments(
            role_id, runtime_dir, asset_root, output_dir, width, height, mp_port
        )
        plans[role_id] = RolePlan(
            role_id,
            "MP",
            savepath,
            log_name,
            marker,
            role_args,
            [
                ("screenshot", f"baseoq4/screenshots/{BASELINE_DIR}/mp_{role_name}.tga"),
                ("renderDemo", f"baseoq4/demos/{demo_name}.demo"),
            ],
        )

    for plan in plans.values():
        plan.stdout_path = output_dir / f"{plan.role_id}.stdout.txt"
        plan.stderr_path = output_dir / f"{plan.role_id}.stderr.txt"
    return plans


def plan_record(plan: RolePlan) -> dict[str, Any]:
    return {
        "role": plan.role_id,
        "mode": plan.mode,
        "windowed": True,
        "captureMethod": "engine screenshot command",
        "savepath": str(plan.savepath),
        "logName": plan.log_name,
        "marker": plan.marker,
        "requiredMarkers": ROLE_EVIDENCE_CONTRACT[plan.role_id].get("requiredMarkers", []),
        "arguments": plan.args,
        "expected": [{"kind": kind, "path": path} for kind, path in plan.expected],
    }


def planned_cvar_values(arguments: Any, name: str) -> list[str]:
    if not isinstance(arguments, list):
        return []
    values: list[str] = []
    for index in range(len(arguments) - 2):
        if arguments[index] in {"+set", "+seta"} and arguments[index + 1] == name:
            values.append(str(arguments[index + 2]))
    return values


def planned_command_values(arguments: Any, name: str) -> list[list[str]]:
    if not isinstance(arguments, list):
        return []
    values: list[list[str]] = []
    command = "+" + name
    for index, argument in enumerate(arguments):
        if argument != command:
            continue
        command_values: list[str] = []
        for value in arguments[index + 1 :]:
            if isinstance(value, str) and value.startswith("+"):
                break
            command_values.append(str(value))
        values.append(command_values)
    return values


def launch(executable: Path, plan: RolePlan, cwd: Path) -> subprocess.Popen[Any]:
    assert plan.stdout_path is not None and plan.stderr_path is not None
    stdout = plan.stdout_path.open("w", encoding="utf-8", errors="replace")
    stderr = plan.stderr_path.open("w", encoding="utf-8", errors="replace")
    try:
        process = subprocess.Popen([str(executable), *plan.args], cwd=cwd, stdout=stdout, stderr=stderr)
    finally:
        stdout.close()
        stderr.close()
    return process


def wait_process(process: subprocess.Popen[Any], timeout: int) -> tuple[int, bool]:
    try:
        return process.wait(timeout=timeout), False
    except subprocess.TimeoutExpired:
        process.kill()
        return process.wait(timeout=10), True


def wait_processes_until(
    processes: dict[str, subprocess.Popen[Any]], deadline: float
) -> dict[str, tuple[int, bool]]:
    """Wait for a process group without giving sequential waits fresh budgets."""

    pending = dict(processes)
    results: dict[str, tuple[int, bool]] = {}
    while pending:
        for name, process in list(pending.items()):
            exit_code = process.poll()
            if exit_code is not None:
                results[name] = (exit_code, False)
                del pending[name]
        if not pending:
            break
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(0.05, remaining))

    # Kill every process still pending before reaping any of them. This keeps
    # the absolute deadline common to the whole group even if process teardown
    # takes time on one platform.
    for process in pending.values():
        try:
            process.kill()
        except OSError:
            # The process may have exited between the final poll and kill.
            pass
    for name, process in pending.items():
        results[name] = (process.wait(timeout=10), True)
    return results


def find_log(plan: RolePlan) -> Path | None:
    for game_dir in ("baseoq4", "q4base"):
        candidate = plan.savepath / game_dir / "logs" / plan.log_name
        if candidate.is_file():
            return candidate
    return None


def read_text(path: Path | None) -> str:
    if path is None or not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def collect_role_diagnostics(
    log_path: Path | None,
    stdout_path: Path | None,
    stderr_path: Path | None,
) -> tuple[str, str]:
    """Return authoritative engine diagnostics and all captured diagnostics.

    On POSIX, the engine can mirror the same diagnostic line to both its
    logfile and stdout. Ordered and exact lifecycle proofs therefore use only
    the engine logfile, while the combined text remains useful for fail-closed
    fatal/error scanning across every captured channel.
    """

    engine_log = read_text(log_path)
    all_diagnostics = "\n".join(
        (engine_log, read_text(stdout_path), read_text(stderr_path))
    )
    return engine_log, all_diagnostics


def runtime_window_evidence(diagnostics: str) -> tuple[str, int, int, str] | None:
    modes = RUNTIME_WINDOW_MODE_PATTERN.findall(diagnostics)
    if not modes:
        return None
    selector, width, height, mode = modes[-1]
    return selector.strip(), int(width), int(height), mode.casefold()


def mp_client_active_proof_failure(diagnostics: str) -> str | None:
    active_proofs = list(MP_CLIENT_ACTIVE_PATTERN.finditer(diagnostics))
    view_proofs = list(MP_CLIENT_VIEW_PATTERN.finditer(diagnostics))
    screenshot_writes = list(MP_CLIENT_SCREENSHOT_WRITE_PATTERN.finditer(diagnostics))
    if len(screenshot_writes) != 1:
        return "exactly one MP client screenshot-write marker is required"
    completion_pattern = re.compile(
        rf"^{re.escape(ROLE_EVIDENCE_CONTRACT['mp-client']['marker'])}[ \t]*$",
        re.MULTILINE,
    )
    completions = list(completion_pattern.finditer(diagnostics))
    if len(completions) != 1:
        return "exactly one MP client completion marker is required"
    screenshot_write = screenshot_writes[0]
    completion = completions[0]
    if completion.start() <= screenshot_write.end():
        return "MP client completion marker must follow the screenshot write"
    if len(active_proofs) < 2 or len(view_proofs) < 2:
        return (
            "two exact active-player and gameplay-view proofs "
            "(before and after screenshot) are required"
        )
    for label, proofs in (("active-player", active_proofs), ("gameplay-view", view_proofs)):
        before = [proof for proof in proofs if proof.end() <= screenshot_write.start()]
        after = [
            proof
            for proof in proofs
            if proof.start() >= screenshot_write.end() and proof.end() <= completion.start()
        ]
        if not before or not after:
            return f"exact {label} proofs must bracket the MP client screenshot write"
        if completion.start() - after[-1].end() > 512:
            return f"final {label} proof is not immediately before the completion marker"
    return None


def runtime_window_failure(
    diagnostics: str,
    requested_width: int | None = None,
    requested_height: int | None = None,
) -> str | None:
    evidence = runtime_window_evidence(diagnostics)
    if evidence is None:
        return "runtime display mode evidence missing"
    selector, actual_width, actual_height, actual_mode = evidence
    if actual_mode != "windowed":
        return f"runtime display mode is {actual_mode}, not bordered windowed"
    if selector != "-1":
        return f"runtime display mode selector is {selector}, not canonical -1"
    if requested_width is not None and requested_height is not None:
        if (actual_width, actual_height) != (requested_width, requested_height):
            return (
                f"runtime window/render size is {actual_width}x{actual_height}, "
                f"expected exact budget workload {requested_width}x{requested_height}"
            )
    return None


def decode_tga_rgb(data: bytes) -> tuple[int, int, bytes] | str:
    """Decode the uncompressed true-colour TGA subset written by openQ4."""

    if len(data) < 18:
        return "TGA header is truncated"
    id_length = data[0]
    color_map_type = data[1]
    image_type = data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    if width <= 0 or height <= 0:
        return f"TGA has invalid dimensions {width}x{height}"
    bits_per_pixel = data[16]
    if color_map_type != 0:
        return "TGA colour maps are not permitted"
    if image_type != 2:
        return f"TGA image type {image_type} is not uncompressed true-colour"
    if bits_per_pixel not in (24, 32):
        return f"TGA pixel depth {bits_per_pixel} is not 24 or 32 bits"
    if data[17] & 0xC0:
        return "TGA interleaved image data is not permitted"

    bytes_per_pixel = bits_per_pixel // 8
    payload_offset = 18 + id_length
    payload_size = width * height * bytes_per_pixel
    if len(data) != payload_offset + payload_size:
        return (
            f"TGA payload length {len(data) - payload_offset} differs from "
            f"{payload_size} bytes for {width}x{height}x{bits_per_pixel}"
        )

    source = memoryview(data)[payload_offset:]
    source_stride = width * bytes_per_pixel
    top_origin = bool(data[17] & 0x20)
    right_origin = bool(data[17] & 0x10)
    rgb = bytearray(width * height * 3)
    for y in range(height):
        source_y = y if top_origin else height - 1 - y
        source_row = source[source_y * source_stride : (source_y + 1) * source_stride]
        for x in range(width):
            source_x = width - 1 - x if right_origin else x
            source_pixel = source_x * bytes_per_pixel
            target_pixel = (y * width + x) * 3
            rgb[target_pixel] = source_row[source_pixel + 2]
            rgb[target_pixel + 1] = source_row[source_pixel + 1]
            rgb[target_pixel + 2] = source_row[source_pixel]
    return width, height, bytes(rgb)


def recursive_scaled_strip_metrics(
    rgb: bytes, width: int, height: int, factor: int
) -> tuple[float, float] | None:
    """Compare the top half with the recursive lower-left strip signature."""

    if factor not in (2, 4) or width % factor or (height // 2) % factor:
        return None
    target_width = width // factor
    target_height = (height // 2) // factor
    if target_width < 1 or target_height < 1:
        return None
    target_y = height - height // factor
    if target_y + target_height > height:
        return None

    count = target_width * target_height
    source_luma_sum = 0
    target_luma_sum = 0
    source_luma_squared = 0
    target_luma_squared = 0
    luma_products = 0
    absolute_rgb_error = 0
    block_pixels = factor * factor

    for y in range(target_height):
        for x in range(target_width):
            source_rgb = [0, 0, 0]
            for block_y in range(factor):
                source_y = y * factor + block_y
                for block_x in range(factor):
                    source_x = x * factor + block_x
                    source_index = (source_y * width + source_x) * 3
                    source_rgb[0] += rgb[source_index]
                    source_rgb[1] += rgb[source_index + 1]
                    source_rgb[2] += rgb[source_index + 2]
            source_rgb = [
                (channel + block_pixels // 2) // block_pixels for channel in source_rgb
            ]
            target_index = ((target_y + y) * width + x) * 3
            target_rgb = rgb[target_index : target_index + 3]
            absolute_rgb_error += sum(
                abs(source_rgb[channel] - target_rgb[channel]) for channel in range(3)
            )
            source_luma = (
                77 * source_rgb[0] + 150 * source_rgb[1] + 29 * source_rgb[2]
            ) >> 8
            target_luma = (
                77 * target_rgb[0] + 150 * target_rgb[1] + 29 * target_rgb[2]
            ) >> 8
            source_luma_sum += source_luma
            target_luma_sum += target_luma
            source_luma_squared += source_luma * source_luma
            target_luma_squared += target_luma * target_luma
            luma_products += source_luma * target_luma

    source_variance_term = count * source_luma_squared - source_luma_sum**2
    target_variance_term = count * target_luma_squared - target_luma_sum**2
    if source_variance_term <= count * count or target_variance_term <= count * count:
        return None
    correlation = (
        count * luma_products - source_luma_sum * target_luma_sum
    ) / (source_variance_term * target_variance_term) ** 0.5
    mean_absolute_error = absolute_rgb_error / (count * 3)
    return correlation, mean_absolute_error


def recursive_scaled_strip_failure(rgb: bytes, width: int, height: int) -> str | None:
    if width < 16 or height < 16 or width % 4 or height % 8:
        return None
    metrics = [recursive_scaled_strip_metrics(rgb, width, height, factor) for factor in (2, 4)]
    if all(
        metric is not None and metric[0] >= 0.98 and metric[1] <= 3.0
        for metric in metrics
    ):
        return "TGA contains recursive scaled-strip repetition"
    return None


def validate_tga(path: Path, expected_dimensions: tuple[int, int] | None = None) -> str | None:
    decoded = decode_tga_rgb(path.read_bytes())
    if isinstance(decoded, str):
        return decoded
    width, height, rgb = decoded
    if expected_dimensions is not None and (width, height) != expected_dimensions:
        return (
            f"TGA dimensions {width}x{height} differ from expected "
            f"{expected_dimensions[0]}x{expected_dimensions[1]}"
        )
    pixel_count = width * height
    sample_step = max(1, pixel_count // 4096)
    sampled_luma = [
        (77 * rgb[index] + 150 * rgb[index + 1] + 29 * rgb[index + 2]) >> 8
        for index in range(0, len(rgb), sample_step * 3)
    ]
    if sampled_luma and max(sampled_luma) - min(sampled_luma) <= 2:
        return "TGA image is blank or near-solid"
    strip_failure = recursive_scaled_strip_failure(rgb, width, height)
    if strip_failure:
        return strip_failure
    return None


def center_aspect_resample_rgb(
    rgb: bytes,
    source_width: int,
    source_height: int,
    output_width: int,
    output_height: int,
) -> bytes:
    """Centre-crop and bilinearly resize RGB pixels using integer arithmetic."""

    if (
        source_width < 1
        or source_height < 1
        or output_width < 1
        or output_height < 1
        or len(rgb) != source_width * source_height * 3
    ):
        raise ValueError("invalid RGB dimensions or payload for centre-aspect resampling")

    crop_x = 0
    crop_y = 0
    crop_width = source_width
    crop_height = source_height
    source_aspect_product = source_width * output_height
    output_aspect_product = source_height * output_width
    if source_aspect_product > output_aspect_product:
        crop_width = max(1, source_height * output_width // output_height)
        crop_x = (source_width - crop_width) // 2
    elif source_aspect_product < output_aspect_product:
        crop_height = max(1, source_width * output_height // output_width)
        crop_y = (source_height - crop_height) // 2

    output = bytearray(output_width * output_height * 3)
    crop_max_x = crop_x + crop_width - 1
    crop_max_y = crop_y + crop_height - 1
    x_denominator = 2 * output_width
    y_denominator = 2 * output_height
    interpolation_denominator = x_denominator * y_denominator

    for output_y in range(output_height):
        source_y_numerator = (2 * output_y + 1) * crop_height - output_height
        source_y = crop_y + source_y_numerator // y_denominator
        y_fraction = source_y_numerator % y_denominator
        if source_y < crop_y:
            source_y = crop_y
            y_fraction = 0
        elif source_y >= crop_max_y:
            source_y = crop_max_y
            y_fraction = 0
        next_y = min(source_y + 1, crop_max_y)

        for output_x in range(output_width):
            source_x_numerator = (2 * output_x + 1) * crop_width - output_width
            source_x = crop_x + source_x_numerator // x_denominator
            x_fraction = source_x_numerator % x_denominator
            if source_x < crop_x:
                source_x = crop_x
                x_fraction = 0
            elif source_x >= crop_max_x:
                source_x = crop_max_x
                x_fraction = 0
            next_x = min(source_x + 1, crop_max_x)

            top_left = (source_y * source_width + source_x) * 3
            top_right = (source_y * source_width + next_x) * 3
            bottom_left = (next_y * source_width + source_x) * 3
            bottom_right = (next_y * source_width + next_x) * 3
            target = (output_y * output_width + output_x) * 3
            for channel in range(3):
                top = (
                    rgb[top_left + channel] * (x_denominator - x_fraction)
                    + rgb[top_right + channel] * x_fraction
                )
                bottom = (
                    rgb[bottom_left + channel] * (x_denominator - x_fraction)
                    + rgb[bottom_right + channel] * x_fraction
                )
                output[target + channel] = (
                    top * (y_denominator - y_fraction)
                    + bottom * y_fraction
                    + interpolation_denominator // 2
                ) // interpolation_denominator
    return bytes(output)


def rgb_similarity_metrics(reference_rgb: bytes, candidate_rgb: bytes) -> tuple[float, float] | str:
    """Return Pearson luma correlation and mean absolute RGB error."""

    if not reference_rgb or len(reference_rgb) != len(candidate_rgb) or len(reference_rgb) % 3:
        return "RGB comparison payloads differ or are empty"
    pixel_count = len(reference_rgb) // 3
    reference_luma_sum = 0
    candidate_luma_sum = 0
    reference_luma_squared = 0
    candidate_luma_squared = 0
    luma_products = 0
    absolute_rgb_error = 0
    for index in range(0, len(reference_rgb), 3):
        reference_luma = (
            77 * reference_rgb[index]
            + 150 * reference_rgb[index + 1]
            + 29 * reference_rgb[index + 2]
        ) >> 8
        candidate_luma = (
            77 * candidate_rgb[index]
            + 150 * candidate_rgb[index + 1]
            + 29 * candidate_rgb[index + 2]
        ) >> 8
        reference_luma_sum += reference_luma
        candidate_luma_sum += candidate_luma
        reference_luma_squared += reference_luma * reference_luma
        candidate_luma_squared += candidate_luma * candidate_luma
        luma_products += reference_luma * candidate_luma
        absolute_rgb_error += (
            abs(reference_rgb[index] - candidate_rgb[index])
            + abs(reference_rgb[index + 1] - candidate_rgb[index + 1])
            + abs(reference_rgb[index + 2] - candidate_rgb[index + 2])
        )

    reference_variance_term = (
        pixel_count * reference_luma_squared - reference_luma_sum**2
    )
    candidate_variance_term = (
        pixel_count * candidate_luma_squared - candidate_luma_sum**2
    )
    if reference_variance_term <= 0 or candidate_variance_term <= 0:
        return "RGB comparison has insufficient luma variance"
    correlation = (
        pixel_count * luma_products - reference_luma_sum * candidate_luma_sum
    ) / (reference_variance_term * candidate_variance_term) ** 0.5
    mean_absolute_rgb_error = absolute_rgb_error / (pixel_count * 3)
    return correlation, mean_absolute_rgb_error


def exposure_compensated_rgb_metrics(
    reference_rgb: bytes, candidate_rgb: bytes
) -> tuple[float, float, float] | str:
    """Fit one bounded luma exposure transform and return its RGB residual."""

    if not reference_rgb or len(reference_rgb) != len(candidate_rgb) or len(reference_rgb) % 3:
        return "RGB comparison payloads differ or are empty"
    pixel_count = len(reference_rgb) // 3
    reference_luma_sum = 0
    candidate_luma_sum = 0
    candidate_luma_squared = 0
    luma_products = 0
    for index in range(0, len(reference_rgb), 3):
        reference_luma = (
            77 * reference_rgb[index]
            + 150 * reference_rgb[index + 1]
            + 29 * reference_rgb[index + 2]
        ) >> 8
        candidate_luma = (
            77 * candidate_rgb[index]
            + 150 * candidate_rgb[index + 1]
            + 29 * candidate_rgb[index + 2]
        ) >> 8
        reference_luma_sum += reference_luma
        candidate_luma_sum += candidate_luma
        candidate_luma_squared += candidate_luma * candidate_luma
        luma_products += reference_luma * candidate_luma

    candidate_variance_term = (
        pixel_count * candidate_luma_squared - candidate_luma_sum**2
    )
    if candidate_variance_term <= 0:
        return "RGB comparison has insufficient luma variance"
    covariance_term = (
        pixel_count * luma_products - reference_luma_sum * candidate_luma_sum
    )
    exposure_gain = covariance_term / candidate_variance_term
    exposure_gain = max(
        SP_SAVE_PREVIEW_MIN_EXPOSURE_GAIN,
        min(SP_SAVE_PREVIEW_MAX_EXPOSURE_GAIN, exposure_gain),
    )
    reference_luma_mean = reference_luma_sum / pixel_count
    candidate_luma_mean = candidate_luma_sum / pixel_count
    exposure_bias = reference_luma_mean - exposure_gain * candidate_luma_mean
    exposure_bias = max(
        SP_SAVE_PREVIEW_MIN_EXPOSURE_BIAS,
        min(SP_SAVE_PREVIEW_MAX_EXPOSURE_BIAS, exposure_bias),
    )

    absolute_rgb_error = 0
    for reference_channel, candidate_channel in zip(reference_rgb, candidate_rgb):
        compensated_channel = int(exposure_gain * candidate_channel + exposure_bias + 0.5)
        compensated_channel = max(0, min(255, compensated_channel))
        absolute_rgb_error += abs(reference_channel - compensated_channel)
    mean_absolute_rgb_error = absolute_rgb_error / len(reference_rgb)
    return exposure_gain, exposure_bias, mean_absolute_rgb_error


def save_preview_comparison(
    reference_path: Path, preview_path: Path
) -> tuple[dict[str, Any] | None, str | None]:
    """Compare the save preview with the adjacent pre-save frame from the same SP run."""

    reference = decode_tga_rgb(reference_path.read_bytes())
    if isinstance(reference, str):
        return None, f"same-state save-reference screenshot cannot be compared: {reference}"
    preview = decode_tga_rgb(preview_path.read_bytes())
    if isinstance(preview, str):
        return None, f"save preview cannot be compared: {preview}"
    reference_width, reference_height, reference_rgb = reference
    preview_width, preview_height, preview_rgb = preview
    analysis_width, analysis_height = SP_SAVE_PREVIEW_ANALYSIS_DIMENSIONS
    reference_rgb = center_aspect_resample_rgb(
        reference_rgb,
        reference_width,
        reference_height,
        analysis_width,
        analysis_height,
    )
    candidate_rgb = center_aspect_resample_rgb(
        preview_rgb,
        preview_width,
        preview_height,
        analysis_width,
        analysis_height,
    )
    metrics = rgb_similarity_metrics(reference_rgb, candidate_rgb)
    if isinstance(metrics, str):
        return None, metrics
    luma_correlation, mean_absolute_rgb_error = metrics
    compensated_metrics = exposure_compensated_rgb_metrics(reference_rgb, candidate_rgb)
    if isinstance(compensated_metrics, str):
        return None, compensated_metrics
    exposure_gain, exposure_bias, compensated_mean_absolute_rgb_error = compensated_metrics
    record = {
        "algorithm": SP_SAVE_PREVIEW_COMPARISON_ALGORITHM,
        "referenceArtifact": "saveReferenceScreenshot",
        "analysisDimensions": {"width": analysis_width, "height": analysis_height},
        "lumaCorrelation": round(luma_correlation, 6),
        "meanAbsoluteRgbError": round(mean_absolute_rgb_error, 3),
        "minimumLumaCorrelation": SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION,
        "exposureCompensation": {
            "model": "shared-luma-affine",
            "gain": round(exposure_gain, 6),
            "bias": round(exposure_bias, 3),
            "minimumGain": SP_SAVE_PREVIEW_MIN_EXPOSURE_GAIN,
            "maximumGain": SP_SAVE_PREVIEW_MAX_EXPOSURE_GAIN,
            "minimumBias": SP_SAVE_PREVIEW_MIN_EXPOSURE_BIAS,
            "maximumBias": SP_SAVE_PREVIEW_MAX_EXPOSURE_BIAS,
        },
        "exposureCompensatedMeanAbsoluteRgbError": round(
            compensated_mean_absolute_rgb_error, 3
        ),
        "maximumExposureCompensatedMeanAbsoluteRgbError": (
            SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
        ),
    }
    policy_failures: list[str] = []
    if luma_correlation < SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION:
        policy_failures.append(
            f"luma correlation {luma_correlation:.6f} is below "
            f"{SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION:.6f}"
        )
    if compensated_mean_absolute_rgb_error > SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR:
        policy_failures.append(
            "exposure-compensated mean RGB error "
            f"{compensated_mean_absolute_rgb_error:.3f} exceeds "
            f"{SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR:.3f}"
        )
    failure = "; ".join(policy_failures) if policy_failures else None
    return record, failure


def evaluate_role(
    plan: RolePlan,
    output_dir: Path,
    exit_code: int,
    timed_out: bool,
    budget_contract: dict[str, Any] | None = None,
) -> dict[str, Any]:
    log_path = find_log(plan)
    authoritative_diagnostics, all_diagnostics = collect_role_diagnostics(
        log_path, plan.stdout_path, plan.stderr_path
    )
    failures: list[str] = []
    if timed_out:
        failures.append("process timeout")
    if exit_code != 0:
        failures.append(f"exit code {exit_code}")
    if log_path is None:
        failures.append("engine log missing")
    if plan.marker.casefold() not in authoritative_diagnostics.casefold():
        failures.append(f"completion marker missing: {plan.marker}")
    for marker in ROLE_EVIDENCE_CONTRACT[plan.role_id].get("requiredMarkers", []):
        if marker.casefold() not in authoritative_diagnostics.casefold():
            failures.append(f"required gameplay marker missing: {marker}")
    if plan.role_id == "mp-client":
        active_failure = mp_client_active_proof_failure(authoritative_diagnostics)
        if active_failure:
            failures.append(active_failure)
    width_values = planned_cvar_values(plan.args, "r_windowWidth")
    height_values = planned_cvar_values(plan.args, "r_windowHeight")
    requested_width = int(width_values[-1]) if len(width_values) == 1 else None
    requested_height = int(height_values[-1]) if len(height_values) == 1 else None
    display_failure = runtime_window_failure(
        authoritative_diagnostics, requested_width, requested_height
    )
    if display_failure:
        failures.append(display_failure)
    for name, pattern in FATAL_PATTERNS.items():
        count = len(pattern.findall(all_diagnostics))
        if count:
            failures.append(f"{name} diagnostics={count}")

    budget_evidence: dict[str, Any] = {}
    budget_identity = ROLE_EVIDENCE_CONTRACT[plan.role_id].get("budget")
    if budget_identity is not None:
        if budget_contract is None:
            budget_contract, _ = load_contract(DEFAULT_CONTRACT_PATH)
        budget_evidence, budget_failures = evaluate_timing_evidence(
            (
                read_text(log_path),
                read_text(plan.stdout_path),
                read_text(plan.stderr_path),
            ),
            budget_contract,
            budget_identity["map"],
            budget_identity["backend"],
            budget_identity["profile"],
        )
        failures.extend(f"renderer budget: {failure}" for failure in budget_failures)

    artifacts: list[dict[str, Any]] = []
    if log_path is not None:
        artifacts.append({"kind": "engineLog", **file_record(log_path, output_dir)})
    for kind, path in (("processStdout", plan.stdout_path), ("processStderr", plan.stderr_path)):
        if path is None or not path.is_file():
            failures.append(f"{kind} capture missing")
        else:
            artifacts.append({"kind": kind, **file_record(path, output_dir)})
    for kind, relative in plan.expected:
        path = plan.savepath / Path(relative)
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"missing or empty {kind}: {relative}")
            continue
        if kind in ("screenshot", "saveReferenceScreenshot", "savePreview"):
            runtime_evidence = runtime_window_evidence(authoritative_diagnostics)
            expected_dimensions = SP_SAVE_PREVIEW_DIMENSIONS if kind == "savePreview" else (
                (runtime_evidence[1], runtime_evidence[2]) if runtime_evidence is not None else None
            )
            tga_failure = validate_tga(path, expected_dimensions)
            if tga_failure:
                failures.append(f"{relative}: {tga_failure}")
        artifacts.append({"kind": kind, **file_record(path, output_dir)})

    result = {
        "role": plan.role_id,
        "mode": plan.mode,
        "status": "pass" if not failures else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "failures": failures,
        "artifacts": artifacts,
        "budgetEvidence": budget_evidence,
    }
    if plan.role_id == "sp-capture":
        expected_by_kind = {kind: plan.savepath / Path(relative) for kind, relative in plan.expected}
        reference_path = expected_by_kind.get("saveReferenceScreenshot")
        preview_path = expected_by_kind.get("savePreview")
        if (
            reference_path is not None
            and preview_path is not None
            and reference_path.is_file()
            and preview_path.is_file()
        ):
            comparison, comparison_failure = save_preview_comparison(
                reference_path, preview_path
            )
            if comparison is not None:
                result["savePreviewComparison"] = comparison
            if comparison_failure:
                failures.append(
                    "save preview differs from same-state save reference: "
                    f"{comparison_failure}"
                )
        else:
            failures.append("save preview comparison artifacts are unavailable")
        result["status"] = "pass" if not failures else "fail"
    return result


def run_capture(
    runtime_dir: Path,
    executable: Path,
    output_dir: Path,
    plans: dict[str, RolePlan],
    timeout: int,
    mp_client_delay: int,
    budget_contract: dict[str, Any],
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []

    sp_plan = plans["sp-capture"]
    sp_process = launch(executable, sp_plan, runtime_dir)
    exit_code, timed_out = wait_process(sp_process, timeout)
    results.append(evaluate_role(sp_plan, output_dir, exit_code, timed_out, budget_contract))
    if results[-1]["status"] == "pass":
        playback_plan = plans["sp-demo-playback"]
        playback_process = launch(executable, playback_plan, runtime_dir)
        exit_code, timed_out = wait_process(playback_process, timeout)
        results.append(evaluate_role(playback_plan, output_dir, exit_code, timed_out, budget_contract))
    else:
        results.append({
            "role": "sp-demo-playback",
            "mode": "SP demo playback",
            "status": "fail",
            "exitCode": None,
            "timedOut": False,
            "failures": ["not run because SP capture/save-load failed"],
            "artifacts": [],
        })

    server_plan = plans["mp-server"]
    client_plan = plans["mp-client"]
    mp_deadline = time.monotonic() + timeout
    server_process = launch(executable, server_plan, runtime_dir)
    time.sleep(max(1, mp_client_delay))
    client_process = launch(executable, client_plan, runtime_dir)
    mp_results = wait_processes_until(
        {"server": server_process, "client": client_process}, mp_deadline
    )
    server_exit, server_timeout = mp_results["server"]
    client_exit, client_timeout = mp_results["client"]
    results.append(evaluate_role(server_plan, output_dir, server_exit, server_timeout, budget_contract))
    results.append(evaluate_role(client_plan, output_dir, client_exit, client_timeout, budget_contract))
    return results


def git_state(root: Path) -> dict[str, Any]:
    def run(*args: str) -> str:
        completed = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=False)
        return completed.stdout.strip() if completed.returncode == 0 else ""

    return {
        "policy": GIT_PROVENANCE_POLICY,
        "root": str(root.resolve()),
        "revision": run("rev-parse", "HEAD"),
        "dirty": bool(run("status", "--porcelain")),
    }


def verify_recorded_files(
    report: dict[str, Any],
    report_dir: Path,
    asset_root: Path,
    runtime_dir: Path | None = None,
    budget_contract: dict[str, Any] | None = None,
    budget_binding: dict[str, Any] | None = None,
) -> list[str]:
    failures: list[str] = []
    if budget_contract is None or budget_binding is None:
        budget_contract, budget_binding = load_contract(DEFAULT_CONTRACT_PATH)
    if report.get("status") != "pass":
        failures.append(f"baseline report status is {report.get('status')!r}, not 'pass'")
    if report.get("dryRun") is not False:
        failures.append("passing baseline must record dryRun=false")
    for field_name in TOP_LEVEL_FAILURE_ARRAYS:
        field_value = report.get(field_name)
        if not isinstance(field_value, list):
            failures.append(f"baseline {field_name} field is missing or is not an array")
        elif field_value:
            failures.append(f"baseline {field_name} field is nonempty")
    failures.extend(
        verify_contract_binding(
            report.get("budgetContract"), budget_contract, budget_binding
        )
    )

    current_git = git_state(repo_root())
    recorded_git = report.get("git")
    if not isinstance(recorded_git, dict):
        failures.append("baseline report does not contain git provenance")
    else:
        revision = recorded_git.get("revision")
        if (
            recorded_git.get("policy") != GIT_PROVENANCE_POLICY
            or recorded_git.get("root") != current_git.get("root")
            or not isinstance(revision, str)
            or re.fullmatch(r"[0-9a-f]{40}", revision) is None
            or not isinstance(recorded_git.get("dirty"), bool)
        ):
            failures.append("baseline git provenance policy/shape differs")
        if not current_git.get("revision"):
            failures.append("current openQ4 git revision is unavailable")
        elif revision != current_git.get("revision"):
            failures.append(
                "baseline git revision is not the current openQ4 HEAD: "
                f"recorded {revision!r}, current {current_git.get('revision')!r}"
            )
        if isinstance(recorded_git.get("dirty"), bool) and (
            recorded_git.get("dirty") != current_git.get("dirty")
        ):
            failures.append("baseline git dirty state differs from the current checkout")

    assets_value = report.get("assets")
    if not isinstance(assets_value, dict):
        failures.append("baseline report assets field is missing or is not an object")
        assets: dict[str, Any] = {}
    else:
        assets = assets_value
    selected_asset_root = asset_root.resolve()
    recorded_asset_root = assets.get("root")
    if not isinstance(recorded_asset_root, str) or not recorded_asset_root:
        failures.append("baseline report does not record its retail asset root")
    elif Path(recorded_asset_root).resolve() != selected_asset_root:
        failures.append(
            "retail asset root differs: "
            f"recorded {Path(recorded_asset_root).resolve()}, selected {selected_asset_root}"
        )
    allow_value = assets.get("allowLinkedGameDirectories")
    if not isinstance(allow_value, bool):
        failures.append("allowLinkedGameDirectories must be a boolean")
        allow_asset_dir_links = False
    else:
        allow_asset_dir_links = allow_value

    expected_binding = report.get("expectedAssets", {})
    if (
        not isinstance(expected_binding, dict)
        or expected_binding.get("supplied") is not True
        or not isinstance(expected_binding.get("path"), str)
        or not expected_binding.get("path")
        or not isinstance(expected_binding.get("sha256"), str)
        or re.fullmatch(r"[0-9a-f]{64}", expected_binding.get("sha256", "")) is None
    ):
        failures.append("passing baseline is not bound to a recorded --expected-assets manifest")
    else:
        expected_path = Path(expected_binding["path"])
        if not expected_path.is_absolute():
            failures.append("recorded expected-assets manifest path is not absolute")
        elif not expected_path.is_file():
            failures.append(f"recorded expected-assets manifest is missing: {expected_path}")
        elif is_link_or_junction(expected_path):
            failures.append(f"recorded expected-assets manifest must not be a link: {expected_path}")
        else:
            actual_expected_sha256 = sha256_file(expected_path)
            if actual_expected_sha256 != expected_binding["sha256"]:
                failures.append(
                    "recorded expected-assets manifest SHA-256 differs: "
                    f"expected {expected_binding['sha256']}, got {actual_expected_sha256}"
                )
            try:
                expected_payload = json.loads(expected_path.read_text(encoding="utf-8"))
                if not isinstance(expected_payload, dict):
                    raise ValueError("expected asset manifest root is not an object")
                expected_records = asset_manifest_from_payload(expected_payload)
                recorded_records = asset_manifest_from_payload(assets)
                if expected_records != recorded_records:
                    failures.append(
                        "recorded retail PK4 inventory differs from the bound expected-assets manifest"
                    )
                expected_loose = loose_manifest_from_payload(expected_payload)
                recorded_loose = loose_manifest_from_payload(assets)
                if expected_loose != recorded_loose:
                    failures.append(
                        "recorded loose retail inventory differs from the bound expected-assets manifest"
                    )
            except (json.JSONDecodeError, OSError, ValueError) as exc:
                failures.append(f"recorded expected-assets manifest is invalid: {exc}")

    current_assets: list[dict[str, Any]] | None = None
    try:
        current_assets = collect_pk4s(selected_asset_root, allow_asset_dir_links)
        failures.extend(compare_asset_records(asset_manifest_from_payload(assets), current_assets))
        if assets.get("directoryViews") != asset_directory_views(selected_asset_root):
            failures.append("recorded retail directory views differ")
        current_loose = collect_loose_asset_files(selected_asset_root, allow_asset_dir_links)
        failures.extend(
            compare_file_records(loose_manifest_from_payload(assets), current_loose, "loose asset file")
        )
        if current_loose:
            failures.append(
                f"retail fallback asset root contains {len(current_loose)} loose q4base/q4mp files"
            )
        if loose_manifest_from_payload(assets):
            failures.append("recorded baseline includes loose q4base/q4mp files")
    except (OSError, ValueError) as exc:
        failures.append(str(exc))

    recorded_runtime_root = report.get("runtimeRoot")
    selected_runtime_dir: Path | None = runtime_dir
    if not isinstance(recorded_runtime_root, str) or not recorded_runtime_root:
        failures.append("baseline report does not record its runtime root")
    else:
        recorded_runtime_dir = Path(recorded_runtime_root).absolute()
        if selected_runtime_dir is None:
            selected_runtime_dir = recorded_runtime_dir
        elif selected_runtime_dir.resolve() != recorded_runtime_dir:
            failures.append(
                "runtime root differs: "
                f"recorded {recorded_runtime_dir}, selected {selected_runtime_dir.resolve()}"
            )
    if selected_runtime_dir is None:
        selected_runtime_dir = repo_root() / ".install"
    current_overlay_pk4s: list[dict[str, Any]] | None = None
    try:
        selected_runtime_dir = validate_runtime_dir(selected_runtime_dir, repo_root())
        executable = find_client(selected_runtime_dir)
        current_runtime = collect_runtime_files(selected_runtime_dir, executable)
        failures.extend(
            compare_file_records(report.get("runtimeFiles", []), current_runtime, "runtime file")
        )
        current_overlay_pk4s = collect_overlay_pk4s(selected_runtime_dir)
        failures.extend(
            compare_file_records(
                assets.get("openQ4OverlayPk4s", []), current_overlay_pk4s, "openQ4 overlay PK4"
            )
        )
        current_overlay_loose, unexpected = collect_overlay_loose_files(
            selected_runtime_dir, current_runtime
        )
        failures.extend(
            compare_file_records(
                assets.get("openQ4OverlayLooseFiles", []),
                current_overlay_loose,
                "openQ4 overlay loose file",
            )
        )
        failures.extend(unexpected)
    except (OSError, ValueError) as exc:
        failures.append(str(exc))

    recorded_collisions = assets.get("retailPathCollisions")
    if assets.get("compatibilityModel") != ASSET_COMPATIBILITY_MODEL:
        failures.append("asset compatibility model differs")
    if assets.get("retailArchiveBytesMatchExpected") is not True:
        failures.append("report does not establish that retail archive bytes match the expected manifest")
    if not isinstance(recorded_collisions, list):
        failures.append("retailPathCollisions field is missing or is not an array")
        recorded_collisions = []
    collision_count = assets.get("retailPathCollisionCount")
    if not isinstance(collision_count, int) or isinstance(collision_count, bool):
        failures.append("retailPathCollisionCount field is missing or is not an integer")
    elif collision_count != len(recorded_collisions):
        failures.append("retailPathCollisionCount does not match the recorded collision inventory")
    namespace_untouched = assets.get("retailPathNamespaceUntouched")
    if namespace_untouched is not (len(recorded_collisions) == 0):
        failures.append("retailPathNamespaceUntouched claim differs from the collision inventory")
    if current_assets is not None and current_overlay_pk4s is not None:
        try:
            current_collisions = collect_retail_path_collisions(
                selected_asset_root,
                current_assets,
                selected_runtime_dir,
                current_overlay_pk4s,
            )
            if recorded_collisions != current_collisions:
                failures.append(
                    "recorded retail/openQ4 virtual-path collision inventory differs"
                )
        except (OSError, ValueError) as exc:
            failures.append(str(exc))

    safety = report.get("safety", {})
    window_size = safety.get("windowSize", {}) if isinstance(safety, dict) else {}
    width = window_size.get("width") if isinstance(window_size, dict) else None
    height = window_size.get("height") if isinstance(window_size, dict) else None
    if (
        not isinstance(safety, dict)
        or safety.get("windowedOnly") is not True
        or safety.get("borderless") is not False
        or safety.get("displayContract") != display_contract()
        or safety.get("engineScreenshotOnly") is not True
        or safety.get("operatingSystemCapture") is not False
        or safety.get("inputInjection") is not False
        or not isinstance(width, int)
        or not isinstance(height, int)
        or width != DEFAULT_WIDTH
        or height != DEFAULT_HEIGHT
    ):
        failures.append("baseline safety/window contract differs")

    mp_port_value = report.get("mpPort")
    if (
        not isinstance(mp_port_value, int)
        or isinstance(mp_port_value, bool)
        or not (1024 <= mp_port_value <= 65535)
    ):
        failures.append("baseline mpPort is missing or outside 1024..65535")
        mp_port = 28140
    else:
        mp_port = mp_port_value

    plans_value = report.get("plan")
    if not isinstance(plans_value, list):
        failures.append("baseline plan field is missing or is not an array")
        plans: list[Any] = []
    else:
        plans = plans_value
    plan_by_role = {
        plan.get("role"): plan for plan in plans if isinstance(plan, dict) and plan.get("role")
    }
    if len(plans) != len(ROLE_EVIDENCE_CONTRACT) or set(plan_by_role) != set(ROLE_EVIDENCE_CONTRACT):
        failures.append("baseline plan does not contain each required role exactly once")
    for role, contract in ROLE_EVIDENCE_CONTRACT.items():
        plan = plan_by_role.get(role, {})
        expected_mode = "SP demo playback" if role == "sp-demo-playback" else (
            "SP" if role == "sp-capture" else "MP"
        )
        canonical_savepath = report_dir / "savepaths" / contract["saveDir"]
        if plan.get("mode") != expected_mode:
            failures.append(f"{role}: plan mode differs")
        if plan.get("savepath") != str(canonical_savepath):
            failures.append(f"{role}: plan savepath differs")
        if plan.get("logName") != contract["logName"]:
            failures.append(f"{role}: plan log name differs")
        if plan.get("marker") != contract["marker"]:
            failures.append(f"{role}: plan completion marker differs")
        if plan.get("requiredMarkers", []) != contract.get("requiredMarkers", []):
            failures.append(f"{role}: plan required gameplay markers differ")
        if plan.get("windowed") is not True or plan.get("captureMethod") != "engine screenshot command":
            failures.append(f"{role}: plan safety/capture contract differs")
        launch_contract = {
            "r_rendererSharedGui": "0",
            "r_rendererSharedInWorldGui": "0",
            "r_rendererSharedCinematicPost": "0",
            "r_rendererSharedSpecialFrame": "0",
            "r_rendererSharedWorldAmbient": "0",
            "r_rendererSharedWorldInteraction": "0",
            "r_rendererSharedWorldFogBlend": "0",
            "r_rendererSharedSubview": "0",
            "r_rendererSharedDeform": "0",
            "r_fullscreen": "0",
            "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1",
            "r_fullscreenDesktop": "0",
            "r_windowWidth": str(width),
            "r_windowHeight": str(height),
            "r_renderApi": "gl",
            "fs_basepath": str(selected_asset_root),
            "fs_savepath": str(canonical_savepath),
            "fs_devpath": str(canonical_savepath),
            "fs_game": "baseoq4",
        }
        if role in ("mp-server", "mp-client"):
            launch_contract["ui_autoJoin"] = "1"
        if role == "sp-capture":
            launch_contract["si_gameType"] = "singleplayer"
        elif role == "mp-server":
            launch_contract.update(
                {
                    "net_serverDedicated": "0",
                    "net_port": str(mp_port),
                    "si_pure": "1",
                    "net_serverAllowServerMod": "0",
                    "si_gameType": "DM",
                }
            )
        for cvar, expected_value in launch_contract.items():
            if planned_cvar_values(plan.get("arguments"), cvar) != [expected_value]:
                failures.append(f"{role}: launch CVar {cvar} differs")
        command_contract: dict[str, list[list[str]]] = {}
        if role == "sp-capture":
            command_contract["map"] = [[SP_MAP]]
        elif role == "sp-demo-playback":
            command_contract["timeDemoQuit"] = [[SP_DEMO_NAME]]
        elif role == "mp-server":
            command_contract["spawnServer"] = [[MP_MAP]]
        else:
            command_contract["connect"] = [[f"127.0.0.1:{mp_port}"]]
        for command, expected_values in command_contract.items():
            if planned_command_values(plan.get("arguments"), command) != expected_values:
                failures.append(f"{role}: launch command {command} differs")
        arguments = plan.get("arguments", [])
        if not isinstance(arguments, list) or arguments.count("+vid_restart") != 1:
            failures.append(f"{role}: launch must contain exactly one post-CVar vid_restart")
        elif any(
            cvar in arguments and arguments.index(cvar) > arguments.index("+vid_restart")
            for cvar in (
                "r_fullscreen",
                "r_borderless",
                "r_borderlessDefaultMigrated",
                "r_fullscreenDesktop",
                "r_windowWidth",
                "r_windowHeight",
                "r_renderApi",
                "r_rendererSharedGui",
                "r_rendererSharedInWorldGui",
                "r_rendererSharedCinematicPost",
                "r_rendererSharedSpecialFrame",
                "r_rendererSharedWorldAmbient",
                "r_rendererSharedWorldInteraction",
                "r_rendererSharedWorldFogBlend",
                "r_rendererSharedSubview",
                "r_rendererSharedDeform",
                "fs_basepath",
                "fs_savepath",
                "fs_devpath",
                "fs_game",
                *(("ui_autoJoin",) if role in ("mp-server", "mp-client") else ()),
            )
        ):
            failures.append(f"{role}: vid_restart must follow the display/filesystem launch CVars")
        if planned_cvar_values(plan.get("arguments"), "fs_cdpath"):
            failures.append(f"{role}: launch must not override locked fs_cdpath")
        expected_arguments = expected_role_arguments(
            role,
            selected_runtime_dir,
            selected_asset_root,
            report_dir,
            width if isinstance(width, int) else DEFAULT_WIDTH,
            height if isinstance(height, int) else DEFAULT_HEIGHT,
            mp_port,
        )
        if arguments != expected_arguments:
            failures.append(f"{role}: launch arguments differ from the exact role contract")
        expected_artifacts = [
            {"kind": kind, "path": path} for kind, path in contract["expected"].items()
        ]
        if plan.get("expected") != expected_artifacts:
            failures.append(f"{role}: plan expected-artifact contract differs")

    results_value = report.get("results")
    if not isinstance(results_value, list):
        failures.append("baseline results field is missing or is not an array")
        results: list[Any] = []
    else:
        results = results_value
    result_by_role = {
        result.get("role"): result
        for result in results
        if isinstance(result, dict) and result.get("role")
    }
    if len(results) != len(ROLE_EVIDENCE_CONTRACT) or set(result_by_role) != set(ROLE_EVIDENCE_CONTRACT):
        failures.append("baseline results do not contain each required role exactly once")

    for result in results:
        for artifact in result.get("artifacts", []):
            path = report_dir / Path(str(artifact.get("path", "")))
            if not path.is_file():
                failures.append(f"recorded artifact is missing: {artifact.get('path')}")
                continue
            if path.stat().st_size != artifact.get("size"):
                failures.append(f"recorded artifact size differs: {artifact.get('path')}")
            elif sha256_file(path) != artifact.get("sha256"):
                failures.append(f"recorded artifact SHA-256 differs: {artifact.get('path')}")
    for role, contract in ROLE_EVIDENCE_CONTRACT.items():
        result = result_by_role.get(role, {})
        expected_mode = "SP demo playback" if role == "sp-demo-playback" else (
            "SP" if role == "sp-capture" else "MP"
        )
        if (
            result.get("status") != "pass"
            or result.get("mode") != expected_mode
            or result.get("exitCode") != 0
            or result.get("timedOut") is not False
            or result.get("failures") != []
        ):
            failures.append(f"{role}: passing result lifecycle contract is not satisfied")
        artifacts = result.get("artifacts", [])
        artifact_by_kind = {
            item.get("kind"): item for item in artifacts if isinstance(item, dict) and item.get("kind")
        }
        required_kinds = {"engineLog", "processStdout", "processStderr", *contract["expected"]}
        if len(artifacts) != len(required_kinds) or set(artifact_by_kind) != required_kinds:
            failures.append(f"{role}: required artifact kinds differ")
            continue
        for kind, relative in contract["expected"].items():
            expected_suffix = f"savepaths/{'sp' if role.startswith('sp-') else role}/{relative}"
            if str(artifact_by_kind[kind].get("path", "")).replace("\\", "/") != expected_suffix:
                failures.append(f"{role}: {kind} artifact path differs")
        for kind in ("processStdout", "processStderr"):
            if artifact_by_kind[kind].get("path") != f"{role}.{'stdout' if kind == 'processStdout' else 'stderr'}.txt":
                failures.append(f"{role}: {kind} artifact path differs")
        log_path = str(artifact_by_kind["engineLog"].get("path", "")).replace("\\", "/")
        allowed_log_paths = {
            f"savepaths/{contract['saveDir']}/{game_dir}/logs/{contract['logName']}"
            for game_dir in ("baseoq4", "q4base")
        }
        if log_path not in allowed_log_paths:
            failures.append(f"{role}: engineLog artifact path differs")
        authoritative_diagnostics, all_diagnostics = collect_role_diagnostics(
            report_dir / Path(str(artifact_by_kind["engineLog"].get("path", ""))),
            report_dir / Path(str(artifact_by_kind["processStdout"].get("path", ""))),
            report_dir / Path(str(artifact_by_kind["processStderr"].get("path", ""))),
        )
        budget_identity = contract.get("budget")
        if budget_identity is not None:
            budget_sources = (
                read_text(report_dir / Path(str(artifact_by_kind["engineLog"].get("path", "")))),
                read_text(report_dir / Path(str(artifact_by_kind["processStdout"].get("path", "")))),
                read_text(report_dir / Path(str(artifact_by_kind["processStderr"].get("path", "")))),
            )
            failures.extend(
                f"{role}: {failure}"
                for failure in verify_recorded_evidence(
                    result.get("budgetEvidence"),
                    budget_sources,
                    budget_contract,
                    budget_identity["map"],
                    budget_identity["backend"],
                    budget_identity["profile"],
                )
            )
        elif result.get("budgetEvidence") not in ({}, None):
            failures.append(f"{role}: unexpected renderer budget evidence")
        if contract["marker"].casefold() not in authoritative_diagnostics.casefold():
            failures.append(f"{role}: completion marker is absent from verified diagnostics")
        for marker in contract.get("requiredMarkers", []):
            if marker.casefold() not in authoritative_diagnostics.casefold():
                failures.append(f"{role}: required gameplay marker is absent: {marker}")
        if role == "mp-client":
            active_failure = mp_client_active_proof_failure(authoritative_diagnostics)
            if active_failure:
                failures.append(f"{role}: {active_failure}")
        for name, pattern in FATAL_PATTERNS.items():
            count = len(pattern.findall(all_diagnostics))
            if count:
                failures.append(f"{role}: {name} diagnostics={count}")
        display_failure = runtime_window_failure(authoritative_diagnostics, width, height)
        if display_failure:
            failures.append(f"{role}: {display_failure}")
        runtime_evidence = runtime_window_evidence(authoritative_diagnostics)
        if runtime_evidence is not None:
            for screenshot_kind in ("screenshot", "saveReferenceScreenshot"):
                if screenshot_kind not in artifact_by_kind:
                    continue
                screenshot_path = report_dir / Path(
                    str(artifact_by_kind[screenshot_kind].get("path", ""))
                )
                if not screenshot_path.is_file():
                    continue
                tga_failure = validate_tga(
                    screenshot_path, (runtime_evidence[1], runtime_evidence[2])
                )
                if tga_failure:
                    failures.append(f"{role}: {screenshot_kind}: {tga_failure}")
        if "savePreview" in artifact_by_kind:
            preview_path = report_dir / Path(
                str(artifact_by_kind["savePreview"].get("path", ""))
            )
            if preview_path.is_file():
                tga_failure = validate_tga(preview_path, SP_SAVE_PREVIEW_DIMENSIONS)
                if tga_failure:
                    failures.append(f"{role}: save preview: {tga_failure}")
        if role == "sp-capture":
            reference_path = report_dir / Path(
                str(artifact_by_kind["saveReferenceScreenshot"].get("path", ""))
            )
            preview_path = report_dir / Path(
                str(artifact_by_kind["savePreview"].get("path", ""))
            )
            if reference_path.is_file() and preview_path.is_file():
                comparison, comparison_failure = save_preview_comparison(
                    reference_path, preview_path
                )
                if comparison is None:
                    failures.append(
                        f"{role}: save preview comparison could not be computed: "
                        f"{comparison_failure or 'unknown error'}"
                    )
                else:
                    if result.get("savePreviewComparison") != comparison:
                        failures.append(
                            f"{role}: recorded save-preview comparison metrics differ"
                        )
                    if comparison_failure:
                        failures.append(
                            f"{role}: save preview differs from same-state save reference: "
                            f"{comparison_failure}"
                        )
            else:
                failures.append(f"{role}: save preview comparison artifacts are unavailable")
    return failures


def write_reports(output_dir: Path, payload: dict[str, Any]) -> tuple[Path, Path, Path, Path]:
    report_json = output_dir / "stock_asset_baseline_report.json"
    report_md = output_dir / "stock_asset_baseline_report.md"
    asset_json = output_dir / "stock_pk4_manifest.json"
    runtime_json = output_dir / "openq4_runtime_manifest.json"
    report_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    asset_json.write_text(
        json.dumps(
            {
                "schemaVersion": RETAIL_MANIFEST_SCHEMA_VERSION,
                "allowLinkedGameDirectories": payload["assets"]["allowLinkedGameDirectories"],
                "directoryViews": payload["assets"]["directoryViews"],
                "stockPk4s": payload["assets"]["stockPk4s"],
                "looseFiles": payload["assets"]["looseFiles"],
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )
    runtime_json.write_text(
        json.dumps(
            {
                "schemaVersion": RUNTIME_MANIFEST_SCHEMA_VERSION,
                "runtimeRoot": payload["runtimeRoot"],
                "runtimeFiles": payload["runtimeFiles"],
                "openQ4OverlayPk4s": payload["assets"]["openQ4OverlayPk4s"],
                "openQ4OverlayLooseFiles": payload["assets"]["openQ4OverlayLooseFiles"],
                "compatibilityModel": payload["assets"]["compatibilityModel"],
                "retailPathCollisionCount": payload["assets"]["retailPathCollisionCount"],
                "retailPathCollisions": payload["assets"]["retailPathCollisions"],
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )

    results = payload.get("results", [])
    collisions = payload["assets"].get("retailPathCollisions", [])
    collision_counts_by_overlay: dict[str, int] = {}
    for collision in collisions:
        for archive in collision.get("openQ4OverlayPk4s", []):
            collision_counts_by_overlay[archive] = collision_counts_by_overlay.get(archive, 0) + 1
    lines = [
        "# Retail-PK4 Compatibility Baseline With Packaged openQ4 Overlays",
        "",
        f"- Status: **{payload['status']}**",
        f"- Generated: {payload['generatedUtc']}",
        f"- Compatibility model: `{payload['assets']['compatibilityModel']}`",
        f"- Git revision: `{payload['git']['revision'] or 'unavailable'}` (dirty: `{str(payload['git']['dirty']).lower()}`)",
        f"- Git provenance policy: `{payload['git']['policy']}`",
        f"- Per-map renderer budget contract: `{payload.get('budgetContract', {}).get('contractId', 'missing')}` (`{payload.get('budgetContract', {}).get('sha256', 'missing')}`)",
        f"- Retail fallback asset root: `{payload['assets']['root']}`",
        f"- Linked q4base/q4mp view allowed: `{str(payload['assets']['allowLinkedGameDirectories']).lower()}`",
        f"- Retail PK4s: {len(payload['assets']['stockPk4s'])}",
        f"- Loose q4base/q4mp files: {len(payload['assets']['looseFiles'])} (must be zero)",
        "- Retail archive bytes match bound expected manifest: "
        f"`{str(payload['assets']['retailArchiveBytesMatchExpected']).lower()}`",
        f"- Expected-assets manifest bound: `{str(payload['expectedAssets']['supplied']).lower()}`",
        f"- Expected-assets SHA-256: `{payload['expectedAssets']['sha256'] or 'not supplied'}`",
        f"- openQ4 runtime root: `{payload['runtimeRoot']}`",
        f"- openQ4 runtime files: {len(payload['runtimeFiles'])}",
        f"- openQ4 overlay PK4s: {len(payload['assets']['openQ4OverlayPk4s'])}",
        f"- openQ4 loose overlay files: {len(payload['assets']['openQ4OverlayLooseFiles'])}",
        f"- Retail virtual paths superseded by packaged overlays: {len(collisions)}",
        "- Retail virtual-path namespace untouched: "
        f"`{str(payload['assets']['retailPathNamespaceUntouched']).lower()}`",
        f"- MP loopback port: {payload['mpPort']}",
        f"- Display: `{payload.get('safety', {}).get('displayContract', {}).get('contractId', 'not promotable') if payload.get('safety', {}).get('displayContract') else 'not promotable'}`",
        "- Screenshots: engine `screenshot` command only",
        "- Input automation: none",
        "",
        "## Asset directory view",
        "",
        "| Path | Linked | Resolved target |",
        "|---|---|---|",
    ]
    for view in payload["assets"]["directoryViews"]:
        lines.append(
            f"| `{view['path']}` | `{str(view['linked']).lower()}` | `{view['resolvedTarget']}` |"
        )
    lines += [
        "",
        "## Packaged-overlay precedence",
        "",
    ]
    if collisions:
        lines.append(
            "The verified retail PK4 archive bytes are unchanged, but packaged openQ4 "
            f"overlays supersede **{len(collisions)}** retail virtual paths. This is a "
            "retail-asset compatibility run, not an overlay-free or stock-only run."
        )
        lines.append("")
        for archive, count in sorted(collision_counts_by_overlay.items(), key=lambda item: item[0].casefold()):
            noun = "path" if count == 1 else "paths"
            lines.append(f"- `{archive}` supersedes {count} retail virtual {noun}.")
        lines.append("")
        lines.append(
            "The complete path-by-path collision inventory is recorded in the JSON report."
        )
    else:
        lines.append(
            "No member of a packaged openQ4 overlay PK4 supersedes a path present in the "
            "verified retail PK4 set."
        )
    lines += [
        "",
        "## Results",
        "",
        "| Status | Role | Mode | Artifacts | Failures |",
        "|---|---|---|---:|---|",
    ]
    for result in results:
        failures = "; ".join(result.get("failures", [])) or "none"
        lines.append(
            f"| {result.get('status')} | `{result.get('role')}` | {result.get('mode')} | "
            f"{len(result.get('artifacts', []))} | {failures} |"
        )
    budget_results = [
        result for result in results if result.get("budgetEvidence")
    ]
    if budget_results:
        lines += [
            "",
            "## Per-map CPU/GPU budget evidence",
            "",
            "| Status | Role | Map | Backend | Profile | CPU samples / P95 / P99 | GPU samples / P95 / P99 | Budget |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for result in budget_results:
            evidence = result["budgetEvidence"]
            measurement = evidence.get("measurement", {})
            cpu = measurement.get("cpu", {})
            gpu = measurement.get("gpu", {})
            gpu_summary = (
                f"{gpu.get('samples', '?')} / {gpu.get('p95Us', '?')} / {gpu.get('p99Us', '?')} us"
                if gpu.get("available")
                else "unavailable"
            )
            lines.append(
                f"| {evidence.get('status', 'fail')} | `{result.get('role')}` | "
                f"`{measurement.get('map', 'missing')}` | `{measurement.get('backend', 'missing')}` | "
                f"`{measurement.get('profile', 'missing')}` | {cpu.get('samples', '?')} / "
                f"{cpu.get('p95Us', '?')} / {cpu.get('p99Us', '?')} us | {gpu_summary} | "
                f"`{evidence.get('budgetId', 'missing')}` |"
            )
    sp_capture = next(
        (result for result in results if result.get("role") == "sp-capture"), None
    )
    comparison = (
        sp_capture.get("savePreviewComparison", {})
        if isinstance(sp_capture, dict)
        else {}
    )
    if comparison:
        dimensions = comparison.get("analysisDimensions", {})
        exposure = comparison.get("exposureCompensation", {})
        lines += [
            "",
            "## SP save-preview coherence",
            "",
            f"- Algorithm: `{comparison.get('algorithm')}`",
            f"- Reference artifact: `{comparison.get('referenceArtifact')}`",
            f"- Analysis size: {dimensions.get('width')}x{dimensions.get('height')}",
            f"- Luma correlation: {comparison.get('lumaCorrelation')} "
            f"(minimum {comparison.get('minimumLumaCorrelation')})",
            f"- Raw mean absolute RGB error: {comparison.get('meanAbsoluteRgbError')}",
            f"- Exposure compensation: gain {exposure.get('gain')}, "
            f"bias {exposure.get('bias')}",
            "- Exposure-compensated mean absolute RGB error: "
            f"{comparison.get('exposureCompensatedMeanAbsoluteRgbError')} "
            "(maximum "
            f"{comparison.get('maximumExposureCompensatedMeanAbsoluteRgbError')})",
        ]
    if payload.get("assetComparisonFailures"):
        lines += ["", "## Asset comparison failures", ""]
        lines.extend(f"- {failure}" for failure in payload["assetComparisonFailures"])
    if payload.get("preflightFailures"):
        lines += ["", "## Runtime/overlay preflight failures", ""]
        lines.extend(f"- {failure}" for failure in payload["preflightFailures"])
    if payload.get("postCaptureVerificationFailures"):
        lines += ["", "## Post-capture verification failures", ""]
        lines.extend(f"- {failure}" for failure in payload["postCaptureVerificationFailures"])
    lines += [
        "",
        "## Manual review still required",
        "",
        "Automated pass status proves file identity, map lifecycle, render-demo playback, save/restore completion, artifact integrity, broad save-preview coherence with the same-state pre-save frame, and that captured diagnostics contain none of the harness denylist classes: fatal errors, engine `ERROR` records, shader compile/program-link failures, Vulkan validation messages or VUIDs, and OpenGL errors. It does not assert warning-free logs; other warnings remain retained for manual review. A human must still review the engine screenshots for visual correctness and play representative SP/MP sequences for behavior, audio, and input feel.",
    ]
    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md, asset_json, runtime_json


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", default=default_asset_root(), help="Quake 4 installation root containing q4base/ and optionally q4mp/.")
    parser.add_argument(
        "--runtime-dir",
        default="",
        help=(
            "Resolved openQ4 package directory containing the client and baseoq4/. "
            "Defaults to repository .install; alternates must be fresh ordinary directories below .tmp/stock-runtime/."
        ),
    )
    parser.add_argument("--output-dir", default="", help="Evidence directory; defaults to .tmp/stock-baseline/<UTC timestamp>.")
    parser.add_argument("--expected-assets", default="", help="Optional prior stock_pk4_manifest.json or baseline report. Any mismatch fails before launch.")
    parser.add_argument("--budget-contract", default=str(DEFAULT_CONTRACT_PATH), help="Versioned per-map CPU/GPU budget JSON bound by stable id and SHA-256.")
    parser.add_argument(
        "--allow-asset-dir-links",
        action="store_true",
        help="Permit q4base/q4mp symlinks or junctions in an intentionally clean asset view; resolved targets are recorded and nested links remain forbidden.",
    )
    parser.add_argument("--verify-report", default="", help="Verify an existing report's PK4 and artifact hashes without launching openQ4.")
    parser.add_argument("--dry-run", action="store_true", help="Write the exact windowed launch/config plan and manifests without launching openQ4.")
    parser.add_argument("--list", action="store_true", help="List baseline cases and safety invariants without hashing or launching.")
    parser.add_argument(
        "--timeout",
        type=int,
        default=180,
        help="Maximum seconds for each SP role and for the shared MP server/client session.",
    )
    parser.add_argument("--mp-client-delay", type=int, default=8, help="Seconds between starting the listen server and loopback client.")
    parser.add_argument("--mp-port", type=int, default=28140, help="Loopback listen-server UDP port.")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    args = parser.parse_args(argv)
    if args.width < 640 or args.height < 480:
        parser.error("baseline window must be at least 640x480")
    if (
        not args.dry_run
        and not args.verify_report
        and (args.width, args.height) != (DEFAULT_WIDTH, DEFAULT_HEIGHT)
    ):
        parser.error(
            f"passing baseline budget evidence requires the exact bordered "
            f"{DEFAULT_WIDTH}x{DEFAULT_HEIGHT} display contract"
        )
    if not (1024 <= args.mp_port <= 65535):
        parser.error("--mp-port must be between 1024 and 65535")
    if args.timeout < 30:
        parser.error("--timeout must be at least 30 seconds")
    return args


def print_list() -> None:
    print(
        f"SP: {SP_MAP} -> render demo -> save-reference screenshot -> save -> "
        "load -> post-load engine screenshot"
    )
    print(f"SP demo: play back {SP_DEMO_NAME}.demo with timeDemoQuit")
    print(f"MP: {MP_MAP} listen server + loopback client -> demos + engine screenshots")
    print("Safety: r_fullscreen=0, fixed window, no OS capture, no mouse/keyboard injection")


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list:
        print_list()
        return 0
    if not args.asset_root:
        raise ValueError("--asset-root is required on this platform")
    asset_root = Path(args.asset_root).resolve()
    source_root = repo_root()
    budget_contract, budget_binding = load_contract(Path(args.budget_contract))
    requested_runtime_dir = Path(args.runtime_dir).absolute() if args.runtime_dir else None

    if args.verify_report:
        report_path = Path(args.verify_report).resolve()
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if report.get("schemaVersion") != SCHEMA_VERSION:
            raise ValueError(f"unsupported baseline report schema: {report.get('schemaVersion')!r}")
        failures = verify_recorded_files(
            report,
            report_path.parent,
            asset_root,
            requested_runtime_dir,
            budget_contract,
            budget_binding,
        )
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        if not failures:
            print("stock_asset_baseline verification: pass")
        return 1 if failures else 0

    runtime_dir = validate_runtime_dir(
        requested_runtime_dir or source_root / ".install", source_root
    )
    executable = find_client(runtime_dir)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else source_root / ".tmp" / "stock-baseline" / timestamp
    prepare_output_directory(output_dir)

    stock_pk4s = collect_pk4s(asset_root, args.allow_asset_dir_links)
    loose_files = collect_loose_asset_files(asset_root, args.allow_asset_dir_links)
    runtime_files = collect_runtime_files(runtime_dir, executable)
    overlay_pk4s = collect_overlay_pk4s(runtime_dir)
    overlay_loose_files, preflight_failures = collect_overlay_loose_files(
        runtime_dir, runtime_files
    )
    try:
        retail_path_collisions = collect_retail_path_collisions(
            asset_root, stock_pk4s, runtime_dir, overlay_pk4s
        )
    except (OSError, ValueError) as exc:
        retail_path_collisions = []
        preflight_failures.append(str(exc))
    asset_failures: list[str] = []
    expected_assets_path: Path | None = None
    expected_assets_sha256 = ""
    directory_views = asset_directory_views(asset_root)
    if loose_files:
        asset_failures.append(
            f"retail fallback asset root contains {len(loose_files)} loose q4base/q4mp files; "
            "use a PK4-only asset view"
        )
    if args.expected_assets:
        expected_assets_path = Path(args.expected_assets).resolve()
        expected_assets_sha256 = sha256_file(expected_assets_path)
        expected_payload = json.loads(expected_assets_path.read_text(encoding="utf-8"))
        if not isinstance(expected_payload, dict):
            raise ValueError("expected asset manifest root is not an object")
        asset_failures += compare_asset_records(asset_manifest_from_payload(expected_payload), stock_pk4s)
        asset_failures += compare_file_records(
            loose_manifest_from_payload(expected_payload), loose_files, "loose asset file"
        )
    elif not args.dry_run:
        asset_failures.append(
            "passing capture requires --expected-assets from a separately generated PK4-only manifest"
        )
    plans = prepare_plans(
        runtime_dir, asset_root, output_dir, args.width, args.height, args.mp_port
    )

    if asset_failures or preflight_failures:
        results: list[dict[str, Any]] = []
    elif args.dry_run:
        results = [
            {"role": plan.role_id, "mode": plan.mode, "status": "planned", "failures": [], "artifacts": [], "budgetEvidence": {}}
            for plan in plans.values()
        ]
    else:
        results = run_capture(
            runtime_dir,
            executable,
            output_dir,
            plans,
            args.timeout,
            args.mp_client_delay,
            budget_contract,
        )

    passed = bool(results) and all(result["status"] == "pass" for result in results)
    status = (
        "planned"
        if args.dry_run and not asset_failures and not preflight_failures
        else ("pass" if passed and not asset_failures and not preflight_failures else "fail")
    )
    payload = {
        "schemaVersion": SCHEMA_VERSION,
        "generatedUtc": utc_now(),
        "status": status,
        "dryRun": args.dry_run,
        "git": git_state(source_root),
        "budgetContract": budget_binding,
        "mpPort": args.mp_port,
        "runtimeRoot": str(runtime_dir),
        "host": {
            "platform": platform.platform(),
            "architecture": platform.machine(),
        },
        "runtimeFiles": runtime_files,
        "expectedAssets": {
            "supplied": expected_assets_path is not None,
            "path": str(expected_assets_path) if expected_assets_path is not None else "",
            "sha256": expected_assets_sha256,
        },
        "assets": {
            "compatibilityModel": ASSET_COMPATIBILITY_MODEL,
            "root": str(asset_root),
            "allowLinkedGameDirectories": args.allow_asset_dir_links,
            "directoryViews": directory_views,
            "stockPk4s": stock_pk4s,
            "looseFiles": loose_files,
            "openQ4OverlayPk4s": overlay_pk4s,
            "openQ4OverlayLooseFiles": overlay_loose_files,
            "retailArchiveBytesMatchExpected": (
                expected_assets_path is not None and not asset_failures
            ),
            "retailPathNamespaceUntouched": not retail_path_collisions,
            "retailPathCollisionCount": len(retail_path_collisions),
            "retailPathCollisions": retail_path_collisions,
        },
        "assetComparisonFailures": asset_failures,
        "preflightFailures": preflight_failures,
        "postCaptureVerificationFailures": [],
        "safety": {
            "windowedOnly": True,
            "borderless": False,
            "windowSize": {"width": args.width, "height": args.height},
            "displayContract": display_contract() if not args.dry_run else None,
            "engineScreenshotOnly": True,
            "operatingSystemCapture": False,
            "inputInjection": False,
        },
        "plan": [plan_record(plan) for plan in plans.values()],
        "results": results,
    }
    if status == "pass":
        # Re-hash the retail assets, staged runtime/overlays, and every captured
        # artifact before declaring success.  This catches a launch that writes
        # through an incorrectly mounted package path as well as any concurrent
        # or stale-file mutation during the run.
        post_capture_failures = verify_recorded_files(
            payload,
            output_dir,
            asset_root,
            runtime_dir,
            budget_contract,
            budget_binding,
        )
        payload["postCaptureVerificationFailures"] = post_capture_failures
        if post_capture_failures:
            status = "fail"
            payload["status"] = status
    report_json, report_md, asset_json, runtime_json = write_reports(output_dir, payload)
    print(f"wrote {report_json}")
    print(f"wrote {report_md}")
    print(f"wrote {asset_json}")
    print(f"wrote {runtime_json}")
    for failure in asset_failures:
        print(f"error: {failure}", file=sys.stderr)
    for failure in preflight_failures:
        print(f"error: {failure}", file=sys.stderr)
    for failure in payload["postCaptureVerificationFailures"]:
        print(f"error: {failure}", file=sys.stderr)
    if args.dry_run:
        return 1 if asset_failures or preflight_failures else 0
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from None
