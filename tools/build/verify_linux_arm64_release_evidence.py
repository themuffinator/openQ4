#!/usr/bin/env python3
"""Validate candidate-bound Linux ARM64 release signoff evidence."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import re
import stat
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO, Mapping


SCHEMA_VERSION = 3
MAX_MANIFEST_BYTES = 64 * 1024
MAX_RUNTIME_REPORT_BYTES = 2 * 1024 * 1024
HASH_CHUNK_BYTES = 1024 * 1024

GIT_SHA_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
DECL_CHECKSUM_RE = re.compile(r"0x[0-9a-f]{8}")
RELEASE_VALUE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+_-]{0,127}")
REVIEW_PLACEHOLDER_RE = re.compile(
    r"(?:\b(?:pending|placeholder|tbd|todo)\b"
    r"|^(?:needs\s+review|not\s+(?:applicable|available|provided|recorded)"
    r"|to\s+be\s+(?:added|determined)|unknown)\b"
    r"|^n\s*/?\s*a\b|^x+$)",
    re.IGNORECASE,
)
REVIEW_NON_LIMITATION_PLACEHOLDER_RE = re.compile(
    r"^(?:none|no\s+evidence|not\s+tested|untested)\b", re.IGNORECASE
)

REVIEW_MINIMUM_LENGTHS: Mapping[str, int] = {
    "reviewer": 2,
    "hardware_and_os": 12,
    "compositor_and_graphics": 12,
    "audio_and_input_devices": 12,
    "sp_evidence": 12,
    "mp_evidence": 12,
    "dedicated_server_evidence": 12,
    "audio_input_display_evidence": 12,
    "logs_and_screenshots": 12,
    "accepted_limitations": 4,
}
REVIEW_PLACEHOLDER_VALUES = frozenset(
    {
        "-",
        "?",
        "x",
        "xx",
        "later",
        "n/a",
        "na",
        "needs review",
        "not applicable",
        "not available",
        "not provided",
        "not recorded",
        "to be added",
        "to be determined",
        "unknown",
    }
)

ROOT_KEYS = frozenset(
    {"schema_version", "status", "review", "candidate", "sha256", "runtime_reports"}
)
REVIEW_KEYS = frozenset(
    {
        "reviewer",
        "reviewed_at",
        "hardware_and_os",
        "compositor_and_graphics",
        "audio_and_input_devices",
        "sp_evidence",
        "mp_evidence",
        "dedicated_server_evidence",
        "audio_input_display_evidence",
        "logs_and_screenshots",
        "accepted_limitations",
    }
)
CANDIDATE_KEYS = frozenset(
    {
        "openq4_commit",
        "openq4_game_commit",
        "release_version",
        "version_tag",
        "release_tag",
        "package_filename",
    }
)
# One game module serves both modes, so one hash covers the SP and MP reports' module.
SHA256_KEYS = frozenset({"archive", "client", "dedicated", "game"})
RUNTIME_REPORT_KEYS = frozenset({"stock_sp", "dedicated"})

EXECUTION_EVIDENCE_KEYS = frozenset(
    {
        "nativeProcessArchitecture",
        "hostArchitecture",
        "physicalHardwareAttested",
        "physicalHardwareAttestationSource",
        "virtualMachineDetected",
        "containerDetected",
        "virtualizationInspection",
    }
)
VIRTUALIZATION_INSPECTION_KEYS = frozenset(
    {
        "schemaVersion",
        "scope",
        "virtualMachineDetected",
        "containerDetected",
        "virtualizationIndicators",
        "systemdDetectVirt",
        "hostIdentity",
        "cpuHypervisorFlag",
    }
)
SYSTEMD_DETECT_VIRT_KEYS = frozenset({"vm", "container"})
SYSTEMD_PROBE_KEYS = frozenset({"available", "returnCode", "identifier", "error"})
HOST_IDENTITY_KEYS = frozenset(
    {
        "hypervisorType",
        "dmiSysVendor",
        "dmiProductName",
        "dmiProductVersion",
        "dmiBoardVendor",
        "dmiBoardName",
        "deviceTreeModel",
        "deviceTreeHypervisorCompatible",
        "kernelRelease",
    }
)
ELF_METADATA_KEYS = frozenset({"class", "data", "machine"})

SP_REPORT_KEYS = frozenset(
    {
        "reportSchemaVersion",
        "reportType",
        "status",
        "architecture",
        "host",
        "map",
        "saveSlot",
        "nativeWayland",
        "waylandDisplay",
        "waylandSocket",
        "executionEvidence",
        "audioEvidence",
        "secondActiveGameplayLifecycle",
        "lifecycleCounts",
        "lifecycleOrderFailures",
        "clientMarkers",
        "saveFiles",
        "screenshot",
        "cleanExit",
        "exitCode",
        "abortReason",
        "missingMarkers",
        "fatalMarkers",
        "glErrorMarkers",
        "audioErrorMarkers",
        "elapsedSeconds",
        "openQ4Commit",
        "openQ4GameCommit",
        "sha256",
        "elf",
        "paths",
        "command",
        "environmentContract",
    }
)
SP_AUDIO_EVIDENCE_KEYS = frozenset(
    {
        "softwareInitializationPassed",
        "softwareMarkers",
        "activeDeviceReported",
        "activeDevice",
        "humanAudiblePlaybackVerified",
        "humanPlaybackAttestationSource",
        "automationHeardAudio",
        "scope",
    }
)
SP_HASH_KEYS = frozenset({"client", "gameSp", "retailPak001"})
SP_ELF_KEYS = frozenset({"client", "gameSp"})
SP_ENVIRONMENT_KEYS = frozenset(
    {"SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER", "DISPLAYRemoved", "OPENQ4_FORCE_X11Removed"}
)
SP_LIFECYCLE_COUNT_KEYS = frozenset(
    {"mapInitialization", "firstActiveDraw", "gameMapShutdown"}
)
SP_AUDIO_MARKER_KEYS = frozenset(
    {
        "OpenAL setup began",
        "OpenAL ALC version reported",
        "OpenAL active device reported",
        "engine sound system initialized",
    }
)
SP_CLIENT_MARKER_KEYS = frozenset(
    {
        "nonempty engine log",
        "native Wayland active",
        "selected packaged game_sp",
        "common initialization completed",
        "cinematic auto-skip configured",
        "sound enabled",
        "initial map entered",
        "initial player spawned",
        "initial gameplay config executed",
        "save completed",
        "save load began",
        "save restore initialized",
        "second gameplay config executed",
        "post-restore active marker",
        "post-restore screenshot write",
        "clean game shutdown",
    }
)
SCREENSHOT_RESULT_KEYS = frozenset(
    {
        "status",
        "path",
        "bytes",
        "sha256",
        "width",
        "height",
        "bitsPerPixel",
        "imageType",
        "sampledUniqueColours",
        "sampledChannelRange",
    }
)
SAVE_RESULT_KEYS = frozenset({"status", "directory", "files", "temporaryFiles"})
SAVE_FILE_KEYS = frozenset({"save", "tga", "txt"})
SAVE_FILE_RESULT_KEYS = frozenset(
    {"path", "bytes", "minimumBytes", "sha256", "valid"}
)

DEDICATED_REPORT_KEYS = frozenset(
    {
        "reportSchemaVersion",
        "reportType",
        "status",
        "architecture",
        "host",
        "map",
        "port",
        "nativeWayland",
        "waylandDisplay",
        "waylandSocket",
        "executionEvidence",
        "serverReady",
        "serverHeadless",
        "serverVideoMarkers",
        "serverLogNonempty",
        "clientLogNonempty",
        "serverLifecycleOrderFailures",
        "clientLifecycleOrderFailures",
        "serverExitCode",
        "clientExitCode",
        "clientAbortReason",
        "serverCleanShutdown",
        "clientCleanShutdown",
        "serverDeclChecksum",
        "clientDeclChecksum",
        "matchingDeclChecksum",
        "serverMarkers",
        "clientMarkers",
        "screenshot",
        "missingMarkers",
        "fatalMarkers",
        "elapsedSeconds",
        "openQ4Commit",
        "openQ4GameCommit",
        "sha256",
        "elf",
        "paths",
        "serverCommand",
        "clientCommand",
    }
)
DEDICATED_HASH_KEYS = frozenset({"dedicated", "client", "gameMp", "retailPak001"})
DEDICATED_ELF_KEYS = frozenset({"dedicated", "client", "gameMp"})
DEDICATED_SERVER_MARKER_KEYS = frozenset(
    {
        "dedicated client connected",
        "dedicated SpawnPlayer",
        "dedicated clean game shutdown",
    }
)
DEDICATED_CLIENT_MARKER_KEYS = frozenset(
    {
        "native Wayland active",
        "client selected packaged game_mp",
        "client decl checksum",
        "client received connect response",
        "client entered mp/q4dm1",
        "client SpawnPlayer",
        "client first active draw",
        "client capture cfg executed",
        "client frame pacing snapshot",
        "client selected renderer tier",
        "client renderer tier contract",
        "client renderer benchmark samples",
        "client screenshot write marker",
        "client clean game shutdown",
    }
)

VM_IDENTITY_PATTERN = re.compile(
    r"\b(?:qemu|kvm|vmware|virtualbox|parallels|xen|bochs|bhyve|hyper-v|"
    r"virtual machine|amazon ec2|google compute engine|openstack|digitalocean|"
    r"windows subsystem for linux|wsl)\b",
    re.IGNORECASE,
)
CONTAINER_DISCLOSURE_PATTERN = re.compile(
    r"\b(?:container(?:ized)?|docker|podman|lxc|systemd-nspawn)\b",
    re.IGNORECASE,
)

ARM64_RUNTIME_PATHS: Mapping[str, PurePosixPath] = {
    "client": PurePosixPath("openQ4-client_arm64"),
    "dedicated": PurePosixPath("openQ4-ded_arm64"),
    "game": PurePosixPath("baseoq4/game_arm64.so"),
}


@dataclass(frozen=True)
class CandidateMetadata:
    openq4_commit: str
    openq4_game_commit: str
    release_version: str
    version_tag: str
    release_tag: str
    package_filename: str


@dataclass(frozen=True)
class RuntimeReportPaths:
    stock_sp: Path
    dedicated: Path


@dataclass(frozen=True)
class Evidence:
    status: str
    review: Mapping[str, str]
    candidate: CandidateMetadata
    sha256: Mapping[str, str]
    runtime_reports: Mapping[str, str]


def _strict_keys(value: Mapping[str, Any], expected: frozenset[str], label: str) -> None:
    actual = set(value)
    missing = sorted(expected - actual)
    unknown = sorted(actual - expected)
    if missing or unknown:
        details: list[str] = []
        if missing:
            details.append("missing keys: " + ", ".join(missing))
        if unknown:
            details.append("unknown keys: " + ", ".join(unknown))
        raise RuntimeError(f"{label} has an invalid schema ({'; '.join(details)})")


def _require_table(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} must be a TOML table")
    return value


def _require_plain_string(value: Any, label: str, *, maximum: int = 4096) -> str:
    if not isinstance(value, str):
        raise RuntimeError(f"{label} must be a string")
    if not value or value != value.strip():
        raise RuntimeError(f"{label} must be non-empty and have no surrounding whitespace")
    if len(value) > maximum:
        raise RuntimeError(f"{label} is too long")
    if any(ord(character) < 32 or ord(character) == 127 for character in value):
        raise RuntimeError(f"{label} contains a control character")
    return value


def _require_string_or_empty(value: Any, label: str, *, maximum: int = 4096) -> str:
    if not isinstance(value, str):
        raise RuntimeError(f"{label} must be a string")
    if len(value) > maximum:
        raise RuntimeError(f"{label} is too long")
    if any(ord(character) < 32 or ord(character) == 127 for character in value):
        raise RuntimeError(f"{label} contains a control character")
    return value


def _require_exact_string(value: Any, expected: str, label: str) -> str:
    value = _require_plain_string(value, label, maximum=max(128, len(expected)))
    if value != expected:
        raise RuntimeError(f"{label} must be exactly {expected!r}; got {value!r}")
    return value


def _require_bool(value: Any, label: str, *, expected: bool | None = None) -> bool:
    if type(value) is not bool:
        raise RuntimeError(f"{label} must be a JSON boolean")
    if expected is not None and value is not expected:
        raise RuntimeError(f"{label} must be {str(expected).lower()}")
    return value


def _require_int(value: Any, label: str, *, expected: int | None = None) -> int:
    if type(value) is not int:
        raise RuntimeError(f"{label} must be a JSON integer")
    if expected is not None and value != expected:
        raise RuntimeError(f"{label} must be {expected}; got {value}")
    return value


def _require_number(value: Any, label: str) -> float:
    if type(value) not in {int, float} or not math.isfinite(value):
        raise RuntimeError(f"{label} must be a finite JSON number")
    if value < 0:
        raise RuntimeError(f"{label} must not be negative")
    return float(value)


def _require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise RuntimeError(f"{label} must be a JSON array")
    return value


def _require_empty_list(value: Any, label: str) -> None:
    entries = _require_list(value, label)
    if entries:
        raise RuntimeError(f"{label} must be empty for accepted runtime evidence")


def _require_allowed_keys(
    value: Mapping[str, Any], allowed: frozenset[str], label: str
) -> None:
    unknown = sorted(set(value).difference(allowed))
    if unknown:
        raise RuntimeError(f"{label} has an invalid schema (unknown keys: {', '.join(unknown)})")


def _require_all_true_table(
    value: Any,
    label: str,
    expected_keys: frozenset[str],
) -> Mapping[str, Any]:
    table = _require_table(value, label)
    _strict_keys(table, expected_keys, label)
    failed: list[str] = []
    for key, marker_value in table.items():
        if not isinstance(key, str) or not key:
            raise RuntimeError(f"{label} contains an invalid marker name")
        if not _require_bool(marker_value, f"{label}.{key}"):
            failed.append(key)
    if failed:
        raise RuntimeError(f"{label} contains failed markers: {', '.join(sorted(failed))}")
    return table


def require_git_sha(value: Any, label: str) -> str:
    value = _require_plain_string(value, label, maximum=40)
    if GIT_SHA_RE.fullmatch(value) is None:
        raise RuntimeError(f"{label} must be a canonical lowercase 40-character Git SHA")
    return value


def require_sha256(value: Any, label: str) -> str:
    value = _require_plain_string(value, label, maximum=64)
    if SHA256_RE.fullmatch(value) is None:
        raise RuntimeError(f"{label} must be a canonical lowercase 64-character SHA-256")
    return value


def require_release_value(value: Any, label: str) -> str:
    value = _require_plain_string(value, label, maximum=128)
    if RELEASE_VALUE_RE.fullmatch(value) is None:
        raise RuntimeError(f"{label} contains unsupported characters")
    return value


def require_review_value(value: Any, field_name: str) -> str:
    value = _require_plain_string(value, f"review.{field_name}")
    normalized = " ".join(value.casefold().split()).strip(" .,:;!?-_()[]{}")
    if (
        not normalized
        or not any(character.isalnum() for character in value)
        or normalized in REVIEW_PLACEHOLDER_VALUES
        or REVIEW_PLACEHOLDER_RE.search(normalized) is not None
        or (
            field_name != "accepted_limitations"
            and REVIEW_NON_LIMITATION_PLACEHOLDER_RE.search(normalized) is not None
        )
    ):
        raise RuntimeError(f"review.{field_name} contains a placeholder value")

    if field_name == "reviewed_at":
        if re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", value) is None:
            raise RuntimeError("review.reviewed_at must be an ISO 8601 date in YYYY-MM-DD form")
        try:
            datetime.date.fromisoformat(value)
        except ValueError as exc:
            raise RuntimeError(
                "review.reviewed_at must be a valid ISO 8601 calendar date"
            ) from exc
        return value

    minimum = REVIEW_MINIMUM_LENGTHS[field_name]
    if len(value) < minimum:
        raise RuntimeError(
            f"review.{field_name} is too short to be meaningful "
            f"(minimum {minimum} characters)"
        )
    return value


def require_package_filename(value: Any, label: str) -> str:
    value = _require_plain_string(value, label, maximum=255)
    valid_suffix = value.endswith(
        ("-linux-arm64.tar.xz", "-linux-arm64-preview.tar.xz")
    )
    if (
        "/" in value
        or "\\" in value
        or ".." in value
        or not value.startswith("openq4-")
        or not valid_suffix
    ):
        raise RuntimeError(
            f"{label} must be a safe Linux ARM64 .tar.xz package filename"
        )
    return value


def validate_expected_candidate(candidate: CandidateMetadata) -> CandidateMetadata:
    return CandidateMetadata(
        openq4_commit=require_git_sha(candidate.openq4_commit, "expected openQ4 commit"),
        openq4_game_commit=require_git_sha(
            candidate.openq4_game_commit, "expected openQ4-game commit"
        ),
        release_version=require_release_value(
            candidate.release_version, "expected release version"
        ),
        version_tag=require_release_value(candidate.version_tag, "expected version tag"),
        release_tag=require_release_value(candidate.release_tag, "expected release tag"),
        package_filename=require_package_filename(
            candidate.package_filename, "expected package filename"
        ),
    )


def _lstat(path: Path, label: str) -> os.stat_result:
    try:
        return path.lstat()
    except OSError as exc:
        raise RuntimeError(f"{label} is unavailable: {path}") from exc


def require_regular_file(path: Path, label: str) -> os.stat_result:
    file_stat = _lstat(path, label)
    if stat.S_ISLNK(file_stat.st_mode):
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    if not stat.S_ISREG(file_stat.st_mode):
        raise RuntimeError(f"{label} must be a regular file: {path}")
    return file_stat


def require_directory(path: Path, label: str) -> Path:
    directory_stat = _lstat(path, label)
    if stat.S_ISLNK(directory_stat.st_mode):
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    if not stat.S_ISDIR(directory_stat.st_mode):
        raise RuntimeError(f"{label} must be a directory: {path}")
    return path


def _same_open_file(file_handle: BinaryIO, expected_stat: os.stat_result, label: str) -> None:
    opened_stat = os.fstat(file_handle.fileno())
    if not stat.S_ISREG(opened_stat.st_mode):
        raise RuntimeError(f"{label} changed and is no longer a regular file")
    if (opened_stat.st_dev, opened_stat.st_ino) != (
        expected_stat.st_dev,
        expected_stat.st_ino,
    ):
        raise RuntimeError(f"{label} changed while it was being opened")


def _open_verified(path: Path, label: str) -> tuple[BinaryIO, os.stat_result]:
    expected_stat = require_regular_file(path, label)
    try:
        file_handle = path.open("rb")
    except OSError as exc:
        raise RuntimeError(f"{label} is unreadable: {path}") from exc
    try:
        _same_open_file(file_handle, expected_stat, label)
    except Exception:
        file_handle.close()
        raise
    return file_handle, expected_stat


def sha256_file(path: Path, label: str) -> str:
    digest = hashlib.sha256()
    file_handle, _ = _open_verified(path, label)
    with file_handle:
        while chunk := _read_chunk(file_handle, label):
            digest.update(chunk)
    return digest.hexdigest()


def _read_chunk(file_handle: BinaryIO, label: str) -> bytes:
    try:
        return file_handle.read(HASH_CHUNK_BYTES)
    except OSError as exc:
        raise RuntimeError(f"{label} became unreadable") from exc


def files_equal(first: Path, second: Path, label: str) -> bool:
    first_handle, first_stat = _open_verified(first, f"staged {label}")
    try:
        second_handle, second_stat = _open_verified(second, f"packaged {label}")
    except Exception:
        first_handle.close()
        raise
    with first_handle, second_handle:
        if first_stat.st_size != second_stat.st_size:
            return False
        while True:
            first_chunk = _read_chunk(first_handle, f"staged {label}")
            second_chunk = _read_chunk(second_handle, f"packaged {label}")
            if first_chunk != second_chunk:
                return False
            if not first_chunk:
                return True


def runtime_file(root: Path, relative_path: PurePosixPath, label: str) -> Path:
    current = require_directory(root, f"{label} root")
    parts = relative_path.parts
    for component in parts[:-1]:
        current = require_directory(current / component, f"{label} directory")
    path = current / parts[-1]
    require_regular_file(path, label)
    return path


def read_manifest(path: Path) -> Mapping[str, Any]:
    file_handle, manifest_stat = _open_verified(path, "Linux ARM64 evidence manifest")
    with file_handle:
        if manifest_stat.st_size > MAX_MANIFEST_BYTES:
            raise RuntimeError(
                "Linux ARM64 evidence manifest exceeds the maximum size of "
                f"{MAX_MANIFEST_BYTES} bytes"
            )
        try:
            contents = file_handle.read(MAX_MANIFEST_BYTES + 1)
        except OSError as exc:
            raise RuntimeError(f"Linux ARM64 evidence manifest is unreadable: {path}") from exc
    if len(contents) > MAX_MANIFEST_BYTES:
        raise RuntimeError(
            "Linux ARM64 evidence manifest exceeds the maximum size of "
            f"{MAX_MANIFEST_BYTES} bytes"
        )
    try:
        text = contents.decode("utf-8")
    except UnicodeError as exc:
        raise RuntimeError("Linux ARM64 evidence manifest is not valid UTF-8") from exc
    try:
        document = tomllib.loads(text)
    except tomllib.TOMLDecodeError as exc:
        raise RuntimeError(f"Linux ARM64 evidence manifest is malformed TOML: {exc}") from exc
    if not isinstance(document, dict):
        raise RuntimeError("Linux ARM64 evidence manifest root must be a TOML table")
    return document


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number: {value}")


def read_runtime_report(path: Path, label: str) -> tuple[Mapping[str, Any], str]:
    file_handle, report_stat = _open_verified(path, label)
    with file_handle:
        if report_stat.st_size > MAX_RUNTIME_REPORT_BYTES:
            raise RuntimeError(
                f"{label} exceeds the maximum size of {MAX_RUNTIME_REPORT_BYTES} bytes"
            )
        try:
            contents = file_handle.read(MAX_RUNTIME_REPORT_BYTES + 1)
        except OSError as exc:
            raise RuntimeError(f"{label} is unreadable: {path}") from exc
    if len(contents) > MAX_RUNTIME_REPORT_BYTES:
        raise RuntimeError(
            f"{label} exceeds the maximum size of {MAX_RUNTIME_REPORT_BYTES} bytes"
        )
    try:
        text = contents.decode("utf-8")
    except UnicodeError as exc:
        raise RuntimeError(f"{label} is not valid UTF-8") from exc
    try:
        document = json.loads(
            text,
            object_pairs_hook=_unique_json_object,
            parse_constant=_reject_json_constant,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise RuntimeError(f"{label} is malformed JSON: {exc}") from exc
    if not isinstance(document, dict):
        raise RuntimeError(f"{label} root must be a JSON object")
    return document, hashlib.sha256(contents).hexdigest()


def _candidate_from_table(table: Mapping[str, Any]) -> CandidateMetadata:
    _strict_keys(table, CANDIDATE_KEYS, "candidate table")
    return CandidateMetadata(
        openq4_commit=require_git_sha(table["openq4_commit"], "candidate openQ4 commit"),
        openq4_game_commit=require_git_sha(
            table["openq4_game_commit"], "candidate openQ4-game commit"
        ),
        release_version=require_release_value(
            table["release_version"], "candidate release version"
        ),
        version_tag=require_release_value(table["version_tag"], "candidate version tag"),
        release_tag=require_release_value(table["release_tag"], "candidate release tag"),
        package_filename=require_package_filename(
            table["package_filename"], "candidate package filename"
        ),
    )


def _require_string_list(value: Any, label: str) -> list[str]:
    entries = _require_list(value, label)
    result: list[str] = []
    for index, entry in enumerate(entries):
        result.append(_require_string_or_empty(entry, f"{label}[{index}]"))
    return result


def _validate_screenshot_result(value: Any, label: str) -> None:
    result = _require_table(value, label)
    _strict_keys(result, SCREENSHOT_RESULT_KEYS, label)
    _require_exact_string(result["status"], "pass", f"{label}.status")
    _require_plain_string(result["path"], f"{label}.path")
    byte_count = _require_int(result["bytes"], f"{label}.bytes")
    require_sha256(result["sha256"], f"{label}.sha256")
    width = _require_int(result["width"], f"{label}.width")
    height = _require_int(result["height"], f"{label}.height")
    bits_per_pixel = _require_int(result["bitsPerPixel"], f"{label}.bitsPerPixel")
    _require_int(result["imageType"], f"{label}.imageType", expected=2)
    unique_colours = _require_int(
        result["sampledUniqueColours"], f"{label}.sampledUniqueColours"
    )
    channel_range = _require_int(
        result["sampledChannelRange"], f"{label}.sampledChannelRange"
    )
    if width < 320 or height < 200:
        raise RuntimeError(f"{label} must be at least 320x200")
    if bits_per_pixel not in {24, 32}:
        raise RuntimeError(f"{label}.bitsPerPixel must be 24 or 32")
    minimum_bytes = 18 + width * height * (bits_per_pixel // 8)
    if byte_count < max(65536, minimum_bytes):
        raise RuntimeError(f"{label}.bytes is too small for the recorded TGA geometry")
    if unique_colours < 16:
        raise RuntimeError(f"{label}.sampledUniqueColours must be at least 16")
    if not 12 <= channel_range <= 255:
        raise RuntimeError(f"{label}.sampledChannelRange must be between 12 and 255")


def _validate_save_result(value: Any, label: str) -> None:
    result = _require_table(value, label)
    _strict_keys(result, SAVE_RESULT_KEYS, label)
    _require_exact_string(result["status"], "pass", f"{label}.status")
    _require_plain_string(result["directory"], f"{label}.directory")
    _require_empty_list(result["temporaryFiles"], f"{label}.temporaryFiles")
    files_label = f"{label}.files"
    files = _require_table(result["files"], files_label)
    _strict_keys(files, SAVE_FILE_KEYS, files_label)
    minimum_bytes = {"save": 4096, "tga": 65536, "txt": 1}
    for key in sorted(SAVE_FILE_KEYS):
        entry_label = f"{files_label}.{key}"
        entry = _require_table(files[key], entry_label)
        _strict_keys(entry, SAVE_FILE_RESULT_KEYS, entry_label)
        _require_plain_string(entry["path"], f"{entry_label}.path")
        byte_count = _require_int(entry["bytes"], f"{entry_label}.bytes")
        _require_int(
            entry["minimumBytes"],
            f"{entry_label}.minimumBytes",
            expected=minimum_bytes[key],
        )
        require_sha256(entry["sha256"], f"{entry_label}.sha256")
        _require_bool(entry["valid"], f"{entry_label}.valid", expected=True)
        if byte_count < minimum_bytes[key]:
            raise RuntimeError(
                f"{entry_label}.bytes is below its required minimum {minimum_bytes[key]}"
            )


def _systemd_probe_detected(value: Any, label: str) -> bool:
    probe = _require_table(value, label)
    _strict_keys(probe, SYSTEMD_PROBE_KEYS, label)
    available = _require_bool(probe["available"], f"{label}.available")
    return_code = probe["returnCode"]
    if return_code is not None:
        _require_int(return_code, f"{label}.returnCode")
    identifier = _require_string_or_empty(
        probe["identifier"], f"{label}.identifier", maximum=512
    ).strip().lower()
    _require_string_or_empty(probe["error"], f"{label}.error", maximum=512)
    if available is not (return_code is not None):
        raise RuntimeError(
            f"{label}.available and returnCode describe an impossible probe state"
        )
    if not available and identifier:
        raise RuntimeError(f"{label}.identifier must be empty when the probe is unavailable")
    if available and return_code == 0 and identifier in {"", "none"}:
        raise RuntimeError(f"{label} succeeded without a virtualization identifier")
    if available and return_code != 0 and identifier not in {"", "none"}:
        raise RuntimeError(
            f"{label} failed but still recorded a positive virtualization identifier"
        )
    return bool(available and return_code == 0 and identifier and identifier != "none")


def _validate_execution_evidence(value: Any, label: str) -> bool:
    execution = _require_table(value, label)
    _strict_keys(execution, EXECUTION_EVIDENCE_KEYS, label)
    _require_bool(
        execution["nativeProcessArchitecture"],
        f"{label}.nativeProcessArchitecture",
        expected=True,
    )
    _require_exact_string(execution["hostArchitecture"], "arm64", f"{label}.hostArchitecture")
    _require_bool(
        execution["physicalHardwareAttested"],
        f"{label}.physicalHardwareAttested",
        expected=True,
    )
    _require_exact_string(
        execution["physicalHardwareAttestationSource"],
        "operator CLI flag; known-virtualization inspection passed",
        f"{label}.physicalHardwareAttestationSource",
    )
    _require_bool(
        execution["virtualMachineDetected"],
        f"{label}.virtualMachineDetected",
        expected=False,
    )
    container_detected = _require_bool(
        execution["containerDetected"], f"{label}.containerDetected"
    )

    inspection_label = f"{label}.virtualizationInspection"
    inspection = _require_table(execution["virtualizationInspection"], inspection_label)
    _strict_keys(inspection, VIRTUALIZATION_INSPECTION_KEYS, inspection_label)
    _require_int(inspection["schemaVersion"], f"{inspection_label}.schemaVersion", expected=1)
    scope = _require_plain_string(inspection["scope"], f"{inspection_label}.scope")
    if "does not prove physical hardware" not in scope.casefold():
        raise RuntimeError(
            f"{inspection_label}.scope must preserve the limitation that inspection does not prove physical hardware"
        )
    _require_bool(
        inspection["virtualMachineDetected"],
        f"{inspection_label}.virtualMachineDetected",
        expected=False,
    )
    if (
        _require_bool(inspection["containerDetected"], f"{inspection_label}.containerDetected")
        is not container_detected
    ):
        raise RuntimeError(f"{inspection_label}.containerDetected disagrees with {label}")
    _require_empty_list(
        inspection["virtualizationIndicators"],
        f"{inspection_label}.virtualizationIndicators",
    )
    _require_bool(
        inspection["cpuHypervisorFlag"],
        f"{inspection_label}.cpuHypervisorFlag",
        expected=False,
    )

    systemd_label = f"{inspection_label}.systemdDetectVirt"
    systemd = _require_table(inspection["systemdDetectVirt"], systemd_label)
    _strict_keys(systemd, SYSTEMD_DETECT_VIRT_KEYS, systemd_label)
    if _systemd_probe_detected(systemd["vm"], f"{systemd_label}.vm"):
        raise RuntimeError(f"{systemd_label}.vm positively identifies virtualization")
    systemd_container_detected = _systemd_probe_detected(
        systemd["container"], f"{systemd_label}.container"
    )
    if systemd_container_detected is not container_detected:
        raise RuntimeError(
            f"{systemd_label}.container disagrees with recorded containerDetected"
        )

    identity_label = f"{inspection_label}.hostIdentity"
    identity = _require_table(inspection["hostIdentity"], identity_label)
    _require_allowed_keys(identity, HOST_IDENTITY_KEYS, identity_label)
    if "kernelRelease" not in identity:
        raise RuntimeError(f"{identity_label} is missing kernelRelease")
    normalized_identity: dict[str, str] = {}
    for key, identity_value in identity.items():
        normalized_identity[key] = _require_plain_string(
            identity_value, f"{identity_label}.{key}", maximum=4096
        )
    for key in ("hypervisorType", "deviceTreeHypervisorCompatible"):
        if normalized_identity.get(key):
            raise RuntimeError(f"{identity_label}.{key} identifies a hypervisor")
    identity_text = " ".join(normalized_identity.values())
    if VM_IDENTITY_PATTERN.search(identity_text):
        raise RuntimeError(f"{identity_label} contains a known VM/emulator identity")
    if re.search(
        r"(?:microsoft|\bwsl\b)",
        normalized_identity["kernelRelease"],
        re.IGNORECASE,
    ):
        raise RuntimeError(f"{identity_label}.kernelRelease identifies WSL")
    return container_detected


def _validate_optional_source_commit(value: Any, expected: str, label: str) -> None:
    value = _require_string_or_empty(value, label, maximum=40)
    if not value:
        return
    value = require_git_sha(value, label)
    if value != expected:
        raise RuntimeError(f"{label} {value} does not match candidate commit {expected}")


def _validate_elf_metadata(value: Any, expected_keys: frozenset[str], label: str) -> None:
    elf = _require_table(value, label)
    _strict_keys(elf, expected_keys, label)
    for key in sorted(expected_keys):
        entry_label = f"{label}.{key}"
        entry = _require_table(elf[key], entry_label)
        _strict_keys(entry, ELF_METADATA_KEYS, entry_label)
        _require_exact_string(entry["class"], "ELF64", f"{entry_label}.class")
        _require_exact_string(
            entry["data"], "little-endian", f"{entry_label}.data"
        )
        _require_int(entry["machine"], f"{entry_label}.machine", expected=183)


def _validate_report_hashes(
    value: Any,
    expected_keys: frozenset[str],
    expected_hashes: Mapping[str, str],
    label: str,
) -> Mapping[str, str]:
    hashes = _require_table(value, label)
    _strict_keys(hashes, expected_keys, label)
    canonical: dict[str, str] = {}
    for key in sorted(expected_keys):
        canonical[key] = require_sha256(hashes[key], f"{label}.{key}")
        expected = expected_hashes.get(key)
        if expected is not None and canonical[key] != expected:
            raise RuntimeError(
                f"{label}.{key} {canonical[key]} does not match accepted candidate hash {expected}"
            )
    return canonical


def _validate_wayland_identity(report: Mapping[str, Any], label: str) -> None:
    _require_bool(report["nativeWayland"], f"{label}.nativeWayland", expected=True)
    display = _require_plain_string(
        report["waylandDisplay"], f"{label}.waylandDisplay", maximum=255
    )
    if "/" in display or "\\" in display or display in {".", ".."}:
        raise RuntimeError(f"{label}.waylandDisplay must be a socket basename")
    _require_plain_string(
        report["waylandSocket"], f"{label}.waylandSocket", maximum=4096
    )


def _validate_stock_sp_report(
    report: Mapping[str, Any], evidence: Evidence
) -> tuple[str, bool]:
    label = "stock SP runtime report"
    _strict_keys(report, SP_REPORT_KEYS, label)
    _require_int(report["reportSchemaVersion"], f"{label}.reportSchemaVersion", expected=1)
    _require_exact_string(report["reportType"], "linux-wayland-stock-sp", f"{label}.reportType")
    _require_exact_string(report["status"], "pass", f"{label}.status")
    _require_exact_string(report["architecture"], "arm64", f"{label}.architecture")
    _require_plain_string(report["host"], f"{label}.host")
    _require_exact_string(report["map"], "game/airdefense1", f"{label}.map")
    _require_plain_string(report["saveSlot"], f"{label}.saveSlot", maximum=64)
    _validate_wayland_identity(report, label)
    container_detected = _validate_execution_evidence(
        report["executionEvidence"], f"{label}.executionEvidence"
    )

    audio_label = f"{label}.audioEvidence"
    audio = _require_table(report["audioEvidence"], audio_label)
    _strict_keys(audio, SP_AUDIO_EVIDENCE_KEYS, audio_label)
    _require_bool(
        audio["softwareInitializationPassed"],
        f"{audio_label}.softwareInitializationPassed",
        expected=True,
    )
    _require_all_true_table(
        audio["softwareMarkers"],
        f"{audio_label}.softwareMarkers",
        SP_AUDIO_MARKER_KEYS,
    )
    _require_bool(
        audio["activeDeviceReported"],
        f"{audio_label}.activeDeviceReported",
        expected=True,
    )
    _require_plain_string(audio["activeDevice"], f"{audio_label}.activeDevice")
    # This is intentionally type-checked but not required to be true. Audible
    # playback remains a separate human review item; automation cannot hear it.
    _require_bool(
        audio["humanAudiblePlaybackVerified"],
        f"{audio_label}.humanAudiblePlaybackVerified",
    )
    _require_plain_string(
        audio["humanPlaybackAttestationSource"],
        f"{audio_label}.humanPlaybackAttestationSource",
    )
    _require_bool(
        audio["automationHeardAudio"],
        f"{audio_label}.automationHeardAudio",
        expected=False,
    )
    _require_plain_string(audio["scope"], f"{audio_label}.scope")

    _require_bool(
        report["secondActiveGameplayLifecycle"],
        f"{label}.secondActiveGameplayLifecycle",
        expected=True,
    )
    counts_label = f"{label}.lifecycleCounts"
    counts = _require_table(report["lifecycleCounts"], counts_label)
    _strict_keys(counts, SP_LIFECYCLE_COUNT_KEYS, counts_label)
    if _require_int(counts["mapInitialization"], f"{counts_label}.mapInitialization") < 2:
        raise RuntimeError(f"{counts_label}.mapInitialization must be at least 2")
    if _require_int(counts["firstActiveDraw"], f"{counts_label}.firstActiveDraw") < 2:
        raise RuntimeError(f"{counts_label}.firstActiveDraw must be at least 2")
    if _require_int(counts["gameMapShutdown"], f"{counts_label}.gameMapShutdown") < 1:
        raise RuntimeError(f"{counts_label}.gameMapShutdown must be at least 1")
    _require_empty_list(report["lifecycleOrderFailures"], f"{label}.lifecycleOrderFailures")
    _require_all_true_table(
        report["clientMarkers"], f"{label}.clientMarkers", SP_CLIENT_MARKER_KEYS
    )
    _validate_save_result(report["saveFiles"], f"{label}.saveFiles")
    _validate_screenshot_result(report["screenshot"], f"{label}.screenshot")
    _require_bool(report["cleanExit"], f"{label}.cleanExit", expected=True)
    _require_int(report["exitCode"], f"{label}.exitCode", expected=0)
    if _require_string_or_empty(report["abortReason"], f"{label}.abortReason"):
        raise RuntimeError(f"{label}.abortReason must be empty")
    for key in (
        "missingMarkers",
        "fatalMarkers",
        "glErrorMarkers",
        "audioErrorMarkers",
    ):
        _require_empty_list(report[key], f"{label}.{key}")
    _require_number(report["elapsedSeconds"], f"{label}.elapsedSeconds")
    _validate_optional_source_commit(
        report["openQ4Commit"], evidence.candidate.openq4_commit, f"{label}.openQ4Commit"
    )
    _validate_optional_source_commit(
        report["openQ4GameCommit"],
        evidence.candidate.openq4_game_commit,
        f"{label}.openQ4GameCommit",
    )
    hashes = _validate_report_hashes(
        report["sha256"],
        SP_HASH_KEYS,
        {"client": evidence.sha256["client"], "gameSp": evidence.sha256["game"]},
        f"{label}.sha256",
    )
    _validate_elf_metadata(report["elf"], SP_ELF_KEYS, f"{label}.elf")
    _require_table(report["paths"], f"{label}.paths")
    _require_string_list(report["command"], f"{label}.command")

    environment_label = f"{label}.environmentContract"
    environment = _require_table(report["environmentContract"], environment_label)
    _strict_keys(environment, SP_ENVIRONMENT_KEYS, environment_label)
    _require_exact_string(
        environment["SDL_VIDEO_DRIVER"], "wayland", f"{environment_label}.SDL_VIDEO_DRIVER"
    )
    _require_exact_string(
        environment["SDL_VIDEODRIVER"], "wayland", f"{environment_label}.SDL_VIDEODRIVER"
    )
    _require_bool(
        environment["DISPLAYRemoved"], f"{environment_label}.DISPLAYRemoved", expected=True
    )
    _require_bool(
        environment["OPENQ4_FORCE_X11Removed"],
        f"{environment_label}.OPENQ4_FORCE_X11Removed",
        expected=True,
    )
    return hashes["retailPak001"], container_detected


def _validate_dedicated_report(
    report: Mapping[str, Any], evidence: Evidence
) -> tuple[str, bool]:
    label = "dedicated runtime report"
    _strict_keys(report, DEDICATED_REPORT_KEYS, label)
    _require_int(report["reportSchemaVersion"], f"{label}.reportSchemaVersion", expected=1)
    _require_exact_string(
        report["reportType"], "linux-wayland-stock-dedicated", f"{label}.reportType"
    )
    _require_exact_string(report["status"], "pass", f"{label}.status")
    _require_exact_string(report["architecture"], "arm64", f"{label}.architecture")
    _require_plain_string(report["host"], f"{label}.host")
    _require_exact_string(report["map"], "mp/q4dm1", f"{label}.map")
    port = _require_int(report["port"], f"{label}.port")
    if not 1 <= port <= 65535:
        raise RuntimeError(f"{label}.port must be between 1 and 65535")
    _validate_wayland_identity(report, label)
    container_detected = _validate_execution_evidence(
        report["executionEvidence"], f"{label}.executionEvidence"
    )
    for key in (
        "serverReady",
        "serverHeadless",
        "serverLogNonempty",
        "clientLogNonempty",
        "serverCleanShutdown",
        "clientCleanShutdown",
        "matchingDeclChecksum",
    ):
        _require_bool(report[key], f"{label}.{key}", expected=True)
    for key in (
        "serverVideoMarkers",
        "serverLifecycleOrderFailures",
        "clientLifecycleOrderFailures",
        "missingMarkers",
        "fatalMarkers",
    ):
        _require_empty_list(report[key], f"{label}.{key}")
    _require_int(report["serverExitCode"], f"{label}.serverExitCode", expected=0)
    _require_int(report["clientExitCode"], f"{label}.clientExitCode", expected=0)
    if _require_string_or_empty(report["clientAbortReason"], f"{label}.clientAbortReason"):
        raise RuntimeError(f"{label}.clientAbortReason must be empty")
    server_checksum = _require_plain_string(
        report["serverDeclChecksum"], f"{label}.serverDeclChecksum", maximum=128
    )
    client_checksum = _require_plain_string(
        report["clientDeclChecksum"], f"{label}.clientDeclChecksum", maximum=128
    )
    if DECL_CHECKSUM_RE.fullmatch(server_checksum) is None:
        raise RuntimeError(
            f"{label}.serverDeclChecksum must be a canonical lowercase 32-bit hexadecimal checksum"
        )
    if DECL_CHECKSUM_RE.fullmatch(client_checksum) is None:
        raise RuntimeError(
            f"{label}.clientDeclChecksum must be a canonical lowercase 32-bit hexadecimal checksum"
        )
    if server_checksum != client_checksum:
        raise RuntimeError(f"{label} declaration checksums do not match")
    _require_all_true_table(
        report["serverMarkers"],
        f"{label}.serverMarkers",
        DEDICATED_SERVER_MARKER_KEYS,
    )
    _require_all_true_table(
        report["clientMarkers"],
        f"{label}.clientMarkers",
        DEDICATED_CLIENT_MARKER_KEYS,
    )
    _validate_screenshot_result(report["screenshot"], f"{label}.screenshot")
    _require_number(report["elapsedSeconds"], f"{label}.elapsedSeconds")
    _validate_optional_source_commit(
        report["openQ4Commit"], evidence.candidate.openq4_commit, f"{label}.openQ4Commit"
    )
    _validate_optional_source_commit(
        report["openQ4GameCommit"],
        evidence.candidate.openq4_game_commit,
        f"{label}.openQ4GameCommit",
    )
    hashes = _validate_report_hashes(
        report["sha256"],
        DEDICATED_HASH_KEYS,
        {
            "client": evidence.sha256["client"],
            "dedicated": evidence.sha256["dedicated"],
            "gameMp": evidence.sha256["game"],
        },
        f"{label}.sha256",
    )
    _validate_elf_metadata(report["elf"], DEDICATED_ELF_KEYS, f"{label}.elf")
    _require_table(report["paths"], f"{label}.paths")
    _require_string_list(report["serverCommand"], f"{label}.serverCommand")
    _require_string_list(report["clientCommand"], f"{label}.clientCommand")
    return hashes["retailPak001"], container_detected


def validate_runtime_reports(evidence: Evidence, paths: RuntimeReportPaths) -> None:
    documents: dict[str, Mapping[str, Any]] = {}
    for key, path in (("stock_sp", paths.stock_sp), ("dedicated", paths.dedicated)):
        label = f"Linux ARM64 {key.replace('_', ' ')} runtime report"
        document, report_hash = read_runtime_report(path, label)
        if report_hash != evidence.runtime_reports[key]:
            raise RuntimeError(
                f"{key} runtime report SHA-256 {report_hash} does not match accepted evidence "
                f"{evidence.runtime_reports[key]}"
            )
        documents[key] = document

    stock_retail_hash, stock_container = _validate_stock_sp_report(
        documents["stock_sp"], evidence
    )
    dedicated_retail_hash, dedicated_container = _validate_dedicated_report(
        documents["dedicated"], evidence
    )
    if stock_retail_hash != dedicated_retail_hash:
        raise RuntimeError(
            "stock SP and dedicated runtime reports used different retail pak001.pk4 bytes"
        )
    if stock_container or dedicated_container:
        disclosure = " ".join(
            (
                evidence.review["hardware_and_os"],
                evidence.review["accepted_limitations"],
            )
        )
        if CONTAINER_DISCLOSURE_PATTERN.search(disclosure) is None:
            raise RuntimeError(
                "runtime evidence detected a container, but review.hardware_and_os or "
                "review.accepted_limitations does not explicitly disclose it"
            )


def parse_evidence(document: Mapping[str, Any], *, require_accepted: bool) -> Evidence:
    _strict_keys(document, ROOT_KEYS, "manifest root")
    if type(document["schema_version"]) is not int or document["schema_version"] != SCHEMA_VERSION:
        raise RuntimeError(
            f"manifest schema_version must be the integer {SCHEMA_VERSION}"
        )

    status_value = _require_plain_string(document["status"], "manifest status", maximum=8)
    if status_value not in {"pending", "accepted"}:
        raise RuntimeError('manifest status must be exactly "pending" or "accepted"')
    if require_accepted and status_value != "accepted":
        raise RuntimeError(
            'Linux ARM64 release evidence status must be "accepted"; '
            f"got {status_value!r}"
        )

    review_table = _require_table(document["review"], "review")
    _strict_keys(review_table, REVIEW_KEYS, "review table")
    if status_value == "accepted":
        review = {
            key: require_review_value(review_table[key], key)
            for key in sorted(REVIEW_KEYS)
        }
    else:
        review = {
            key: _require_plain_string(review_table[key], f"review.{key}")
            for key in sorted(REVIEW_KEYS)
        }

    candidate = _candidate_from_table(_require_table(document["candidate"], "candidate"))
    hash_table = _require_table(document["sha256"], "sha256")
    _strict_keys(hash_table, SHA256_KEYS, "sha256 table")
    hashes = {
        key: require_sha256(hash_table[key], f"sha256.{key}")
        for key in sorted(SHA256_KEYS)
    }
    runtime_report_table = _require_table(document["runtime_reports"], "runtime_reports")
    _strict_keys(runtime_report_table, RUNTIME_REPORT_KEYS, "runtime_reports table")
    if status_value == "accepted":
        runtime_reports = {
            key: require_sha256(
                runtime_report_table[key], f"runtime_reports.{key}"
            )
            for key in sorted(RUNTIME_REPORT_KEYS)
        }
    else:
        runtime_reports = {}
        for key in sorted(RUNTIME_REPORT_KEYS):
            value = _require_plain_string(
                runtime_report_table[key], f"runtime_reports.{key}", maximum=7
            )
            if value != "pending":
                raise RuntimeError(
                    f"pending runtime_reports.{key} must be exactly \"pending\""
                )
            runtime_reports[key] = value
    return Evidence(
        status=status_value,
        review=review,
        candidate=candidate,
        sha256=hashes,
        runtime_reports=runtime_reports,
    )


def _compare_candidate(actual: CandidateMetadata, expected: CandidateMetadata) -> None:
    for field_name in (
        "openq4_commit",
        "openq4_game_commit",
        "release_version",
        "version_tag",
        "release_tag",
        "package_filename",
    ):
        actual_value = getattr(actual, field_name)
        expected_value = getattr(expected, field_name)
        if actual_value != expected_value:
            raise RuntimeError(
                f"candidate {field_name} {actual_value!r} does not match expected "
                f"release value {expected_value!r}"
            )


def validate_evidence_manifest(
    path: Path,
    expected: CandidateMetadata,
    runtime_reports: RuntimeReportPaths,
) -> Evidence:
    expected = validate_expected_candidate(expected)
    evidence = parse_evidence(read_manifest(path), require_accepted=True)
    _compare_candidate(evidence.candidate, expected)
    validate_runtime_reports(evidence, runtime_reports)
    return evidence


def _require_archive(archive: Path, expected: CandidateMetadata) -> Path:
    require_regular_file(archive, "Linux ARM64 release archive")
    if archive.name != expected.package_filename:
        raise RuntimeError(
            f"archive filename {archive.name!r} does not match expected package filename "
            f"{expected.package_filename!r}"
        )
    return archive


def collect_candidate_hashes(
    runtime_root: Path,
    package_dir: Path,
    archive: Path,
    expected: CandidateMetadata,
) -> Mapping[str, str]:
    expected = validate_expected_candidate(expected)
    require_directory(runtime_root, "staged runtime root")
    require_directory(package_dir, "package directory")
    _require_archive(archive, expected)

    hashes: dict[str, str] = {}
    for key, relative_path in ARM64_RUNTIME_PATHS.items():
        staged = runtime_file(runtime_root, relative_path, f"staged {relative_path.as_posix()}")
        packaged = runtime_file(package_dir, relative_path, f"packaged {relative_path.as_posix()}")
        if not files_equal(staged, packaged, relative_path.as_posix()):
            raise RuntimeError(
                f"packaged {relative_path.as_posix()} does not exactly match the staged binary"
            )
        staged_hash = sha256_file(staged, f"staged {relative_path.as_posix()}")
        packaged_hash = sha256_file(packaged, f"packaged {relative_path.as_posix()}")
        if packaged_hash != staged_hash:
            raise RuntimeError(
                f"packaged {relative_path.as_posix()} changed while candidate hashes were collected"
            )
        hashes[key] = staged_hash
    hashes["archive"] = sha256_file(archive, "Linux ARM64 release archive")
    return hashes


def verify_release_evidence(
    manifest: Path,
    runtime_root: Path,
    package_dir: Path,
    archive: Path,
    expected: CandidateMetadata,
    runtime_reports: RuntimeReportPaths,
) -> Evidence:
    evidence = validate_evidence_manifest(manifest, expected, runtime_reports)
    actual_hashes = collect_candidate_hashes(runtime_root, package_dir, archive, expected)
    for key in ("client", "dedicated", "game", "archive"):
        if actual_hashes[key] != evidence.sha256[key]:
            raise RuntimeError(
                f"{key} SHA-256 {actual_hashes[key]} does not match accepted evidence "
                f"{evidence.sha256[key]}"
            )
    return evidence


def _toml_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def render_pending_candidate(expected: CandidateMetadata, hashes: Mapping[str, str]) -> str:
    expected = validate_expected_candidate(expected)
    _strict_keys(hashes, SHA256_KEYS, "candidate hashes")
    canonical_hashes = {
        key: require_sha256(hashes[key], f"candidate sha256.{key}")
        for key in SHA256_KEYS
    }
    lines = [
        f"schema_version = {SCHEMA_VERSION}",
        'status = "pending"',
        "",
        "[review]",
        'reviewer = "pending"',
        'reviewed_at = "pending"',
        'hardware_and_os = "pending"',
        'compositor_and_graphics = "pending"',
        'audio_and_input_devices = "pending"',
        'sp_evidence = "pending"',
        'mp_evidence = "pending"',
        'dedicated_server_evidence = "pending"',
        'audio_input_display_evidence = "pending"',
        'logs_and_screenshots = "pending"',
        'accepted_limitations = "pending"',
        "",
        "[runtime_reports]",
        'stock_sp = "pending"',
        'dedicated = "pending"',
        "",
        "[candidate]",
        f"openq4_commit = {_toml_quote(expected.openq4_commit)}",
        f"openq4_game_commit = {_toml_quote(expected.openq4_game_commit)}",
        f"release_version = {_toml_quote(expected.release_version)}",
        f"version_tag = {_toml_quote(expected.version_tag)}",
        f"release_tag = {_toml_quote(expected.release_tag)}",
        f"package_filename = {_toml_quote(expected.package_filename)}",
        "",
        "[sha256]",
        f"archive = {_toml_quote(canonical_hashes['archive'])}",
        f"client = {_toml_quote(canonical_hashes['client'])}",
        f"dedicated = {_toml_quote(canonical_hashes['dedicated'])}",
        f"game = {_toml_quote(canonical_hashes['game'])}",
        "",
    ]
    return "\n".join(lines)


def write_candidate(
    output: Path,
    runtime_root: Path,
    package_dir: Path,
    archive: Path,
    expected: CandidateMetadata,
) -> Mapping[str, str]:
    hashes = collect_candidate_hashes(runtime_root, package_dir, archive, expected)
    contents = render_pending_candidate(expected, hashes)
    if len(contents.encode("utf-8")) > MAX_MANIFEST_BYTES:
        raise RuntimeError("generated Linux ARM64 evidence manifest is unexpectedly oversized")

    if output.is_symlink():
        raise RuntimeError(f"candidate output must not be a symlink: {output}")
    if output.exists() and not output.is_file():
        raise RuntimeError(f"candidate output must be a regular file: {output}")
    require_directory(output.parent, "candidate output directory")
    try:
        output.write_text(contents, encoding="utf-8", newline="\n")
    except OSError as exc:
        raise RuntimeError(f"could not write pending evidence candidate: {output}") from exc
    return hashes


def _add_expected_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--expected-openq4-commit", required=True)
    parser.add_argument("--expected-openq4-game-commit", required=True)
    parser.add_argument("--expected-release-version", required=True)
    parser.add_argument("--expected-version-tag", required=True)
    parser.add_argument("--expected-release-tag", required=True)
    parser.add_argument("--expected-package-filename", required=True)


def _add_runtime_report_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--stock-sp-report",
        required=True,
        type=Path,
        help="Exact stock-SP report.json committed with the accepted evidence.",
    )
    parser.add_argument(
        "--dedicated-report",
        required=True,
        type=Path,
        help="Exact dedicated/client report.json committed with the accepted evidence.",
    )


def _runtime_report_paths_from_args(args: argparse.Namespace) -> RuntimeReportPaths:
    return RuntimeReportPaths(
        stock_sp=args.stock_sp_report,
        dedicated=args.dedicated_report,
    )


def _expected_from_args(args: argparse.Namespace) -> CandidateMetadata:
    return CandidateMetadata(
        openq4_commit=args.expected_openq4_commit,
        openq4_game_commit=args.expected_openq4_game_commit,
        release_version=args.expected_release_version,
        version_tag=args.expected_version_tag,
        release_tag=args.expected_release_tag,
        package_filename=args.expected_package_filename,
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    validate_parser = commands.add_parser(
        "validate",
        help="validate accepted metadata and bound runtime reports without release package files",
    )
    validate_parser.add_argument("--manifest", required=True, type=Path)
    validate_parser.add_argument(
        "--print-archive-sha256",
        action="store_true",
        help="print only the accepted archive SHA-256 after validation",
    )
    _add_runtime_report_arguments(validate_parser)
    _add_expected_arguments(validate_parser)

    verify_parser = commands.add_parser(
        "verify", help="verify accepted evidence against staged and packaged release bytes"
    )
    verify_parser.add_argument("--manifest", required=True, type=Path)
    verify_parser.add_argument("--runtime-root", required=True, type=Path)
    verify_parser.add_argument("--package-dir", required=True, type=Path)
    verify_parser.add_argument("--archive", required=True, type=Path)
    _add_runtime_report_arguments(verify_parser)
    _add_expected_arguments(verify_parser)

    candidate_parser = commands.add_parser(
        "write-candidate", help="write a pending template bound to actual release bytes"
    )
    candidate_parser.add_argument("--output", required=True, type=Path)
    candidate_parser.add_argument("--runtime-root", required=True, type=Path)
    candidate_parser.add_argument("--package-dir", required=True, type=Path)
    candidate_parser.add_argument("--archive", required=True, type=Path)
    _add_expected_arguments(candidate_parser)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    expected = _expected_from_args(args)
    try:
        if args.command == "validate":
            evidence = validate_evidence_manifest(
                args.manifest,
                expected,
                _runtime_report_paths_from_args(args),
            )
            if args.print_archive_sha256:
                print(evidence.sha256["archive"])
            else:
                print(
                    "accepted Linux ARM64 evidence metadata and runtime reports verified: "
                    f"{evidence.candidate.package_filename}"
                )
        elif args.command == "verify":
            evidence = verify_release_evidence(
                args.manifest,
                args.runtime_root,
                args.package_dir,
                args.archive,
                expected,
                _runtime_report_paths_from_args(args),
            )
            print(
                "accepted Linux ARM64 release evidence verified: "
                f"{evidence.candidate.package_filename} sha256={evidence.sha256['archive']}"
            )
        elif args.command == "write-candidate":
            hashes = write_candidate(
                args.output,
                args.runtime_root,
                args.package_dir,
                args.archive,
                expected,
            )
            print(
                f"pending Linux ARM64 evidence candidate written: {args.output} "
                f"archive_sha256={hashes['archive']}"
            )
        else:  # argparse makes this unreachable.
            raise RuntimeError(f"unsupported command: {args.command}")
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
