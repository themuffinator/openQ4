#!/usr/bin/env python3
"""Validate a collected macOS runtime signoff archive."""

from __future__ import annotations

import argparse
import re
import sys
import tarfile
import unicodedata
from pathlib import Path, PurePosixPath


class SignoffArchiveError(RuntimeError):
    pass


MAX_TEXT_MEMBER_BYTES = 2 * 1024 * 1024
MAX_ARCHIVE_MEMBER_BYTES = 64 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 4096
MAX_ARCHIVE_TOTAL_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_PATH_CHARS = 512
MAX_ARCHIVE_PATH_SEGMENT_CHARS = 128
MAX_RESULT_TOKEN_CHARS = 80
RESULT_TOKEN_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
MACOS_FORBIDDEN_ARCHIVE_NAMES = (
    ".DS_Store",
    "__MACOSX",
    ".fseventsd",
    ".Spotlight-V100",
    ".Trashes",
    "Icon\r",
)
MACOS_FORBIDDEN_ARCHIVE_PREFIXES = (
    "._",
)
MACOS_FORBIDDEN_ARCHIVE_SUFFIXES = (
    ".dSYM",
)


REQUIRED_REPORT_TOKENS = (
    "# macOS Runtime Signoff",
    "## Automated Evidence",
    "Bridge-specific build and staged install completed.",
    "Staged macOS payload integrity checks completed.",
    "Quake 4 asset basepath validation completed.",
    "Renderer smoke profile completed with retail Quake 4 assets.",
    "Multiplayer listen-server smoke completed with retail Quake 4 assets.",
    "MP game module path is present in the staged payload.",
    "macOS-facing renderer validation matrix completed.",
    "Desktop launcher was written for Finder/Terminal launch checks.",
    "- openQ4 commit:",
    "- openQ4 dirty:",
    "- `openQ4-game` commit:",
    "- `openQ4-game` dirty:",
    "Package layout contract is self-contained app",
    "## Manual Hardware Checklist",
    "mounted signed/notarized DMG",
    "Drag only openQ4.app",
    "whole package payload",
    "Contents/Resources",
    "Contents/Frameworks",
    "unrelated working directory",
    "fs_basepath",
    "fs_cdpath",
    "fs_savepath",
    "Gatekeeper assessment",
    "keyboard text entry",
    "SDL game controller",
    "audio output",
    "windowed, fullscreen",
    "matching OpenGL or Metal bridge",
    "multiplayer",
    "dedicated server",
    "## macOS Version",
    "## Xcode And SDK",
    "## Kernel",
    "## Hardware",
    "## Displays",
    "## Audio Devices",
    "## USB Devices",
    "## Bluetooth Devices",
    "## Staged Payload",
    "## Staged Binary Architectures",
    "openQ4-client_",
    "openQ4-ded_",
    "Dedicated server:",
    "baseoq4/game_",
)


REQUIRED_LOG_TOKENS = (
    "Configuring openQ4",
    "Compiling openQ4",
    "Staging openQ4 into .install",
    "Validated staged macOS payload",
    "Validated Quake 4 asset basepath",
    "Running openQ4 macOS renderer smoke",
    "Running openQ4 macOS multiplayer smoke",
    "Running macOS-facing renderer validation matrix",
    "Installed macOS launcher",
    "macOS runtime signoff report:",
)


def parse_bridges(value: str) -> tuple[str, ...]:
    bridges = tuple(part.strip() for part in value.split(",") if part.strip())
    if not bridges:
        raise SignoffArchiveError("At least one bridge must be requested.")
    invalid = [bridge for bridge in bridges if bridge not in {"opengl", "metal"}]
    if invalid:
        raise SignoffArchiveError(f"Unsupported bridge name(s): {', '.join(invalid)}")
    if len(set(bridges)) != len(bridges):
        raise SignoffArchiveError("Bridge list contains duplicates.")
    return bridges


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SignoffArchiveError(message)


def validate_member(member: tarfile.TarInfo) -> None:
    name = member.name
    path = PurePosixPath(name)
    parts = name.split("/")
    require(name != "", "Archive contains an empty path.")
    require(
        len(name) <= MAX_ARCHIVE_PATH_CHARS,
        f"Archive path is too long: {len(name)} characters (max {MAX_ARCHIVE_PATH_CHARS})",
    )
    require(
        not any(ord(character) < 32 or ord(character) == 127 for character in name),
        f"Archive path contains a control character: {name!r}",
    )
    require("\\" not in name, f"Archive path uses backslashes: {name}")
    require(not path.is_absolute(), f"Archive path is absolute: {name}")
    require(".." not in path.parts, f"Archive path escapes through '..': {name}")
    require("" not in parts, f"Archive path contains an empty segment: {name}")
    require(not any(part == "." for part in parts), f"Archive path contains a dot segment: {name}")
    for part in parts:
        require(
            len(part) <= MAX_ARCHIVE_PATH_SEGMENT_CHARS,
            f"Archive path segment is too long: {len(part)} characters "
            f"(max {MAX_ARCHIVE_PATH_SEGMENT_CHARS}) in {name}",
        )
    require(member.isfile() or member.isdir(), f"Archive contains a non-regular entry: {name}")
    mode = member.mode & 0o7777
    require(mode & 0o7000 == 0, f"Archive member has special mode bits: {name} ({mode:o})")
    require(mode & 0o022 == 0, f"Archive member is group/other writable: {name} ({mode:o})")
    if member.isfile():
        require(
            member.size <= MAX_ARCHIVE_MEMBER_BYTES,
            f"Archive member is too large: {name} ({member.size} bytes)",
        )


def signoff_casefold_path_key(name: str) -> str:
    return unicodedata.normalize("NFC", name).casefold()


def is_macos_non_runtime_metadata_path(name: str) -> bool:
    for part in PurePosixPath(name).parts:
        normalized_part = part.casefold()
        if any(normalized_part == forbidden.casefold() for forbidden in MACOS_FORBIDDEN_ARCHIVE_NAMES):
            return True
        if any(normalized_part.startswith(prefix.casefold()) for prefix in MACOS_FORBIDDEN_ARCHIVE_PREFIXES):
            return True
        if any(normalized_part.endswith(suffix.casefold()) for suffix in MACOS_FORBIDDEN_ARCHIVE_SUFFIXES):
            return True
    return False


def validate_text_member_contents(name: str, text: str) -> None:
    for character in text:
        if character in ("\n", "\r", "\t"):
            continue
        require(
            ord(character) >= 32 and ord(character) != 127,
            f"Archive text member contains an unsupported control character: {name}",
        )


def read_text(archive: tarfile.TarFile, name: str) -> str:
    try:
        member = archive.getmember(name)
    except KeyError as exc:
        raise SignoffArchiveError(f"Missing required archive member: {name}") from exc
    require(member.isfile(), f"Required archive member is not a file: {name}")
    require(
        member.size <= MAX_TEXT_MEMBER_BYTES,
        f"Archive text member is too large: {name} ({member.size} bytes)",
    )
    stream = archive.extractfile(member)
    require(stream is not None, f"Unable to read archive member: {name}")
    try:
        text = stream.read().decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SignoffArchiveError(f"Archive member is not UTF-8 text: {name}") from exc
    validate_text_member_contents(name, text)
    return text


def infer_run_id(top_dirs: set[str], action: str, bridges: tuple[str, ...]) -> str:
    inferred: str | None = None
    for bridge in bridges:
        suffix = f"-{action}-{bridge}"
        matches = sorted(directory for directory in top_dirs if directory.endswith(suffix))
        require(len(matches) == 1, f"Expected exactly one *{suffix} directory, found {len(matches)}.")
        run_id = matches[0][: -len(suffix)]
        require(run_id != "", f"Could not infer run ID from {matches[0]}.")
        if inferred is None:
            inferred = run_id
        else:
            require(run_id == inferred, "Bridge result directories do not share the same run ID.")
    return inferred or ""


def validate_result_token(label: str, value: str) -> None:
    require(
        len(value) <= MAX_RESULT_TOKEN_CHARS,
        f"Invalid signoff archive {label} token is too long: "
        f"{len(value)} characters (max {MAX_RESULT_TOKEN_CHARS})",
    )
    require(
        RESULT_TOKEN_PATTERN.fullmatch(value) is not None,
        f"Invalid signoff archive {label} token: {value}",
    )


def has_file_under(file_names: set[str], prefix: str) -> bool:
    normalized = prefix.rstrip("/") + "/"
    return any(name.startswith(normalized) and name != normalized for name in file_names)


def expected_signoff_archive_name(run_id: str) -> str:
    return f"openq4-macos-results-{run_id}.tar.gz"


def validate_report(
    report: str,
    *,
    bridge: str,
    result_dir: str,
    require_completed_checklist: bool,
) -> None:
    for token in REQUIRED_REPORT_TOKENS:
        require(token in report, f"{bridge} signoff report is missing {token!r}.")
    require(f"- Graphics bridge: {bridge}" in report, f"{bridge} signoff report has wrong bridge metadata.")
    require("- Architecture policy:" in report, f"{bridge} signoff report is missing architecture policy metadata.")
    require("- OS matrix role:" in report, f"{bridge} signoff report is missing OS matrix role metadata.")
    require("- OpenAL provider:" in report, f"{bridge} signoff report is missing OpenAL provider metadata.")
    require("- Build directory:" in report, f"{bridge} signoff report is missing build directory metadata.")
    require("- Results:" in report, f"{bridge} signoff report is missing result directory metadata.")
    require("- Client: not found" not in report, f"{bridge} signoff report did not record a staged client path.")
    require(
        "- Dedicated server: not found" not in report,
        f"{bridge} signoff report did not record a staged dedicated server path.",
    )
    require(f"{result_dir}" in report, f"{bridge} signoff report does not reference its expected result directory.")
    require(
        f"{result_dir}/renderer-smoke" in report,
        f"{bridge} signoff report does not reference its renderer-smoke output directory.",
    )
    require(
        f"{result_dir}/renderer-mp-smoke" in report,
        f"{bridge} signoff report does not reference its renderer-mp-smoke output directory.",
    )
    require(
        f"{result_dir}/renderer-matrix" in report,
        f"{bridge} signoff report does not reference its renderer-matrix output directory.",
    )
    if require_completed_checklist:
        open_items = [line for line in report.splitlines() if re.match(r"^- \[ \] ", line)]
        require(not open_items, f"{bridge} signoff report still has open manual checklist item(s).")


def validate_log(log_text: str, *, bridge: str, result_dir: str) -> None:
    for token in REQUIRED_LOG_TOKENS:
        require(token in log_text, f"{bridge} workflow log is missing {token!r}.")
    require(
        f"macos_graphics_bridge={bridge}" in log_text,
        f"{bridge} workflow log does not show the selected graphics bridge.",
    )
    require(
        "macos_openal_provider=" in log_text,
        f"{bridge} workflow log does not show the selected OpenAL provider.",
    )
    require(
        f"{result_dir}/macos-runtime-signoff.md" in log_text,
        f"{bridge} workflow log does not reference the expected signoff report path.",
    )


def validate_signoff_archive(
    archive_path: Path,
    *,
    run_id: str | None,
    action: str,
    bridges: tuple[str, ...],
    require_completed_checklist: bool,
) -> str:
    require(archive_path.is_file(), f"Archive was not found: {archive_path}")
    require(not archive_path.is_symlink(), f"Archive path must not be a symlink: {archive_path}")
    validate_result_token("action", action)
    if run_id is not None:
        validate_result_token("run ID", run_id)

    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        require(members, "Archive is empty.")
        require(
            len(members) <= MAX_ARCHIVE_MEMBERS,
            f"Archive contains too many members: {len(members)} (max {MAX_ARCHIVE_MEMBERS})",
        )
        seen_members: set[str] = set()
        seen_casefold_members: dict[str, str] = {}
        file_names: set[str] = set()
        total_file_bytes = 0
        for member in members:
            validate_member(member)
            if member.isfile():
                total_file_bytes += member.size
                require(
                    total_file_bytes <= MAX_ARCHIVE_TOTAL_BYTES,
                    "Archive total expanded size is too large: "
                    f"{total_file_bytes} bytes (max {MAX_ARCHIVE_TOTAL_BYTES})",
                )
            normalized_name = member.name.rstrip("/")
            if normalized_name:
                require(
                    normalized_name not in seen_members,
                    f"Archive contains a duplicate member: {normalized_name}",
                )
                casefold_name = signoff_casefold_path_key(normalized_name)
                previous_name = seen_casefold_members.get(casefold_name)
                require(
                    previous_name is None or previous_name == normalized_name,
                    "Archive contains case-insensitive duplicate members: "
                    f"{previous_name}, {normalized_name}",
                )
                require(
                    not is_macos_non_runtime_metadata_path(normalized_name),
                    f"Archive contains non-runtime macOS metadata/debug entry: {normalized_name}",
                )
                seen_members.add(normalized_name)
                seen_casefold_members[casefold_name] = normalized_name
                if member.isfile():
                    file_names.add(normalized_name)

        member_names = {member.name.rstrip("/") for member in members}
        top_dirs = {PurePosixPath(name).parts[0] for name in member_names if PurePosixPath(name).parts}
        effective_run_id = run_id or infer_run_id(top_dirs, action, bridges)
        validate_result_token("run ID", effective_run_id)
        expected_result_dirs = {f"{effective_run_id}-{action}-{bridge}" for bridge in bridges}
        unexpected_top_dirs = sorted(top_dirs - expected_result_dirs)
        require(
            not unexpected_top_dirs,
            "Archive contains unexpected top-level result directories: "
            + ", ".join(unexpected_top_dirs[:10]),
        )

        for bridge in bridges:
            result_dir = f"{effective_run_id}-{action}-{bridge}"
            require(result_dir in top_dirs, f"Missing result directory for {bridge}: {result_dir}")

            report_name = f"{result_dir}/macos-runtime-signoff.md"
            log_name = f"{result_dir}/openq4-macos-workflow.log"
            report = read_text(archive, report_name)
            log_text = read_text(archive, log_name)
            validate_report(
                report,
                bridge=bridge,
                result_dir=result_dir,
                require_completed_checklist=require_completed_checklist,
            )
            validate_log(log_text, bridge=bridge, result_dir=result_dir)

            require(
                has_file_under(file_names, f"{result_dir}/renderer-smoke"),
                f"{bridge} signoff archive is missing renderer-smoke output.",
            )
            require(
                has_file_under(file_names, f"{result_dir}/renderer-mp-smoke"),
                f"{bridge} signoff archive is missing renderer-mp-smoke output.",
            )
            require(
                has_file_under(file_names, f"{result_dir}/renderer-matrix"),
                f"{bridge} signoff archive is missing renderer-matrix output.",
            )

        expected_archive_name = expected_signoff_archive_name(effective_run_id)
        require(
            archive_path.name == expected_archive_name,
            "Archive file name does not match run ID: "
            f"{archive_path.name} (expected {expected_archive_name})",
        )

    return effective_run_id


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="Collected openQ4 macOS signoff .tar.gz archive.")
    parser.add_argument("--run-id", default="", help="Expected run ID. Defaults to inferring from result directories.")
    parser.add_argument("--action", default="signoff", help="Expected guest action in result directory names.")
    parser.add_argument("--bridges", default="opengl,metal", help="Comma-separated bridge list to require.")
    parser.add_argument(
        "--require-completed-checklist",
        action="store_true",
        help="Fail if any manual hardware checklist item remains unchecked.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        run_id = validate_signoff_archive(
            args.archive,
            run_id=args.run_id or None,
            action=args.action,
            bridges=parse_bridges(args.bridges),
            require_completed_checklist=args.require_completed_checklist,
        )
    except (SignoffArchiveError, tarfile.TarError) as exc:
        print(f"macOS signoff archive validation failed: {exc}", file=sys.stderr)
        return 1

    print(f"macOS signoff archive validation passed: {args.archive} (run ID {run_id})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
