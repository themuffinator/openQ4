#!/usr/bin/env python3
"""Generate or record macOS signoff evidence from a validated archive."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INDEX = ROOT / "docs" / "dev" / "macos-signoff-evidence.md"
DEFAULT_GAMELIBS_ROOT = ROOT
NO_COMPLETED_STATUS = "- [ ] No completed macOS first-class support evidence is recorded yet."
EVIDENCE_HISTORY_HEADING = "## Evidence History"
CURRENT_RELEASE_HEADING = "## Current Release Evidence"
PACKAGE_ARTIFACT_SUFFIXES = (".dmg", ".tar.gz", ".tar.xz", ".zip")
PACKAGE_ARTIFACT_PATTERN = re.compile(
    r"^openq4-[A-Za-z0-9._+-]+-macos-arm64-(?P<bridge>opengl|metal)(?:-unsigned)?"
    r"(?P<suffix>\.dmg|\.tar\.gz|\.tar\.xz|\.zip)$"
)
MAX_EVIDENCE_TEXT_VALUE_CHARS = 2048


@dataclass(frozen=True)
class ReportData:
    bridge: str
    metadata: dict[str, str]
    sections: dict[str, str]


@dataclass(frozen=True)
class EvidenceData:
    version: str
    support_tier: str
    run_id: str
    archive_reference: str
    archive_sha256: str
    validator_command: str
    validator_result: str
    openq4_commit: str
    gamelibs_commit: str
    package_artifacts: tuple[str, ...]
    signing_status: str
    architecture_policy: str
    architecture: str
    cpu_architecture: str
    os_matrix_role: str
    macos_floor_evidence: str
    latest_macos_evidence: str
    macos_version: str
    xcode_version: str
    macos_sdk_version: str
    kernel: str
    hardware_model: str
    cpu: str
    gpu_display: str
    openal_provider: str
    graphics_bridges: str
    sp_coverage: str
    mp_coverage: str
    dedicated_coverage: str
    finder_coverage: str
    terminal_coverage: str
    package_layout_contract: str
    mounted_dmg_coverage: str
    copied_package_coverage: str
    app_only_move_behavior: str
    path_resolution_log_coverage: str
    gatekeeper_assessment: str
    input_devices: str
    audio_devices: str
    display_modes: str
    known_exceptions: tuple[str, ...]
    release_note_limitations: tuple[str, ...]
    generated_date: str


def load_validator():
    validator_path = ROOT / "tools" / "macos" / "validate_signoff_archive.py"
    spec = importlib.util.spec_from_file_location("validate_signoff_archive_for_recording", validator_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to import signoff archive validator: {validator_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_git_commit(path: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "not recorded"
    return result.stdout.strip() or "not recorded"


def split_report_sections(report: str) -> tuple[dict[str, str], dict[str, str]]:
    metadata: dict[str, str] = {}
    sections: dict[str, list[str]] = {}
    current_section = ""

    for raw_line in report.replace("\r\n", "\n").splitlines():
        line = raw_line.rstrip("\n")
        if line.startswith("## "):
            current_section = line[3:].strip()
            sections.setdefault(current_section, [])
            continue
        if current_section:
            sections[current_section].append(line)
        elif line.startswith("- ") and ": " in line:
            key, value = line[2:].split(": ", 1)
            metadata[key.strip()] = value.strip()

    return metadata, {key: "\n".join(value).strip() for key, value in sections.items()}


def strip_fenced_text(section: str) -> str:
    lines: list[str] = []
    in_fence = False
    saw_fence = False
    for line in section.splitlines():
        stripped = line.strip()
        if stripped.startswith("```"):
            in_fence = not in_fence
            saw_fence = True
            continue
        if in_fence or not saw_fence:
            lines.append(line)
    return "\n".join(lines).strip()


def compact_lines(text: str, *, max_lines: int = 4) -> str:
    lines = [line.strip() for line in strip_fenced_text(text).splitlines() if line.strip()]
    if not lines:
        return "not recorded"
    return " / ".join(lines[:max_lines])


def first_match(text: str, patterns: tuple[str, ...]) -> str:
    raw = strip_fenced_text(text)
    for pattern in patterns:
        match = re.search(pattern, raw, re.MULTILINE)
        if match:
            return match.group(1).strip()
    return ""


def read_reports(
    archive_path: Path,
    *,
    run_id: str,
    action: str,
    bridges: tuple[str, ...],
    validator,
) -> list[ReportData]:
    reports: list[ReportData] = []
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            for bridge in bridges:
                result_dir = f"{run_id}-{action}-{bridge}"
                report_name = f"{result_dir}/macos-runtime-signoff.md"
                report = validator.read_text(archive, report_name)
                metadata, sections = split_report_sections(report)
                reports.append(ReportData(bridge=bridge, metadata=metadata, sections=sections))
    except KeyError as exc:
        raise RuntimeError(f"Validated signoff archive is missing an expected report member: {exc}") from exc
    except UnicodeDecodeError as exc:
        raise RuntimeError("Validated signoff archive report is not UTF-8 text") from exc
    return reports


def unique_metadata(reports: list[ReportData], key: str) -> str:
    values = tuple(dict.fromkeys(report.metadata.get(key, "").strip() for report in reports if report.metadata.get(key, "").strip()))
    if not values:
        return "not recorded"
    return ", ".join(values)


def first_section(reports: list[ReportData], section: str) -> str:
    for report in reports:
        value = report.sections.get(section, "").strip()
        if value:
            return value
    return ""


def parse_macos_version(section: str) -> str:
    raw = strip_fenced_text(section)
    product = first_match(raw, (r"ProductName:\s*(.+)",))
    version = first_match(raw, (r"ProductVersion:\s*(.+)",))
    build = first_match(raw, (r"BuildVersion:\s*(.+)",))
    parts = [part for part in (product, version, f"build {build}" if build else "") if part]
    return " ".join(parts) if parts else compact_lines(section)


def parse_xcode_version(section: str) -> str:
    return first_match(section, (r"^Xcode:\s*(.+)$", r"^(Xcode\s+.+)$")) or compact_lines(section, max_lines=1)


def parse_macos_sdk_version(section: str) -> str:
    return first_match(section, (r"^macOS SDK:\s*(.+)$", r"^SDK Version:\s*(.+)$")) or "not recorded"


def matrix_evidence_status(os_matrix_role: str, target: str) -> str:
    normalized = os_matrix_role.casefold()
    if target == "floor" and "floor" in normalized:
        return f"covered by this evidence role: {os_matrix_role}"
    if target == "latest" and "latest" in normalized:
        return f"covered by this evidence role: {os_matrix_role}"
    return "not recorded; required before first-class promotion"


def normalize_items(items: list[str], default: str) -> tuple[str, ...]:
    cleaned = tuple(item.strip() for item in items if item.strip())
    return cleaned or (default,)


def artifact_casefold_key(value: str) -> str:
    return value.casefold()


def validate_package_artifacts(artifacts: list[str], bridges: tuple[str, ...]) -> tuple[str, ...]:
    cleaned = tuple(artifact.strip() for artifact in artifacts if artifact.strip())
    if not cleaned:
        return ()

    seen: dict[str, str] = {}
    artifact_bridges: set[str] = set()
    for artifact in cleaned:
        if (
            "/" in artifact
            or "\\" in artifact
            or any(ord(character) < 32 or ord(character) == 127 for character in artifact)
        ):
            raise RuntimeError(f"Package artifact name is not a safe filename: {artifact!r}")
        match = PACKAGE_ARTIFACT_PATTERN.fullmatch(artifact)
        if match is None:
            raise RuntimeError(
                "Package artifact is not an exact macOS arm64 openQ4 "
                f"opengl/metal package artifact: {artifact}"
            )
        if match.group("suffix") not in PACKAGE_ARTIFACT_SUFFIXES:
            raise RuntimeError(f"Package artifact has unsupported archive suffix: {artifact}")
        artifact_bridges.add(match.group("bridge"))
        key = artifact_casefold_key(artifact)
        previous = seen.get(key)
        if previous is not None:
            raise RuntimeError(f"Package artifact names contain a case-insensitive duplicate: {previous}, {artifact}")
        seen[key] = artifact

    requested_bridges = set(bridges)
    unexpected_bridges = sorted(artifact_bridges - requested_bridges)
    if unexpected_bridges:
        raise RuntimeError(
            "Package artifacts include unrequested bridge artifact(s): "
            + ", ".join(unexpected_bridges)
        )

    for bridge in bridges:
        if bridge not in artifact_bridges:
            raise RuntimeError(f"Package artifacts are missing the {bridge} bridge artifact.")
    return cleaned


def validate_evidence_text(label: str, value: str, *, allow_empty: bool = True) -> str:
    cleaned = value.strip()
    if not cleaned:
        if allow_empty:
            return ""
        raise RuntimeError(f"{label} must not be empty.")
    if len(cleaned) > MAX_EVIDENCE_TEXT_VALUE_CHARS:
        raise RuntimeError(
            f"{label} is too long: {len(cleaned)} characters "
            f"(max {MAX_EVIDENCE_TEXT_VALUE_CHARS})"
        )
    if any(ord(character) < 32 or ord(character) == 127 for character in cleaned):
        raise RuntimeError(f"{label} contains control characters.")
    return cleaned


def validate_evidence_text_items(label: str, values: list[str]) -> list[str]:
    return [
        validate_evidence_text(f"{label} entry", value, allow_empty=False)
        for value in values
        if value.strip()
    ]


def validate_recording_arguments(args: argparse.Namespace) -> None:
    args.version = validate_evidence_text("version", args.version)
    args.support_tier = validate_evidence_text("support tier", args.support_tier, allow_empty=False)
    args.artifact_url = validate_evidence_text("artifact URL", args.artifact_url)
    args.signing_status = validate_evidence_text("signing status", args.signing_status)
    args.openq4_commit = validate_evidence_text("openQ4 commit", args.openq4_commit)
    args.gamelibs_commit = validate_evidence_text("openQ4-game commit", args.gamelibs_commit)
    args.known_exception = validate_evidence_text_items("known exception", args.known_exception)
    args.release_note_limitation = validate_evidence_text_items(
        "release-note limitation",
        args.release_note_limitation,
    )


def resolve_recording_input(path: Path, label: str, *, reject_symlink: bool = True) -> Path:
    if reject_symlink and path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    return path.resolve()


def build_evidence(args: argparse.Namespace, *, run_id: str, bridges: tuple[str, ...], reports: list[ReportData]) -> EvidenceData:
    hardware = first_section(reports, "Hardware")
    displays = first_section(reports, "Displays")
    audio = first_section(reports, "Audio Devices")
    usb = first_section(reports, "USB Devices")
    bluetooth = first_section(reports, "Bluetooth Devices")
    toolchain = first_section(reports, "Xcode And SDK")
    architecture = unique_metadata(reports, "Architecture")
    os_matrix_role = unique_metadata(reports, "OS matrix role")

    hardware_model = first_match(
        hardware,
        (
            r"Model Name:\s*(.+)",
            r"Model Identifier:\s*(.+)",
        ),
    ) or "not recorded"
    cpu = first_match(
        hardware,
        (
            r"Chip:\s*(.+)",
            r"Processor Name:\s*(.+)",
            r"CPU Type:\s*(.+)",
        ),
    ) or "not recorded"
    gpu_display = first_match(
        displays,
        (
            r"Chipset Model:\s*(.+)",
            r"Display Type:\s*(.+)",
        ),
    ) or compact_lines(displays, max_lines=2)

    archive_ref = args.artifact_url or str(args.archive)
    validator_command = (
        f"python tools/macos/validate_signoff_archive.py {args.archive} "
        f"--run-id {run_id} --bridges {','.join(bridges)} --require-completed-checklist"
    )

    return EvidenceData(
        version=args.version or "unversioned",
        support_tier=args.support_tier,
        run_id=run_id,
        archive_reference=archive_ref,
        archive_sha256=sha256_file(args.archive),
        validator_command=validator_command,
        validator_result="passed with --require-completed-checklist",
        openq4_commit=args.openq4_commit or run_git_commit(ROOT),
        gamelibs_commit=args.gamelibs_commit or run_git_commit(args.gamelibs_root),
        package_artifacts=validate_package_artifacts(args.package_artifact, bridges) or ("not recorded",),
        signing_status=args.signing_status or "not recorded",
        architecture_policy=unique_metadata(reports, "Architecture policy"),
        architecture=architecture,
        cpu_architecture=architecture,
        os_matrix_role=os_matrix_role,
        macos_floor_evidence=matrix_evidence_status(os_matrix_role, "floor"),
        latest_macos_evidence=matrix_evidence_status(os_matrix_role, "latest"),
        macos_version=parse_macos_version(first_section(reports, "macOS Version")),
        xcode_version=parse_xcode_version(toolchain),
        macos_sdk_version=parse_macos_sdk_version(toolchain),
        kernel=compact_lines(first_section(reports, "Kernel"), max_lines=1),
        hardware_model=hardware_model,
        cpu=cpu,
        gpu_display=gpu_display,
        openal_provider=unique_metadata(reports, "OpenAL provider"),
        graphics_bridges=", ".join(bridges),
        sp_coverage="renderer-smoke evidence present and completed manual checklist validated",
        mp_coverage="renderer-mp-smoke evidence present and completed manual checklist validated",
        dedicated_coverage="staged dedicated server path recorded and completed manual checklist validated",
        finder_coverage="completed manual checklist validated",
        terminal_coverage="completed manual checklist validated",
        package_layout_contract="self-contained openQ4.app: data in Contents/Resources/baseoq4 and signed modules in Contents/Frameworks",
        mounted_dmg_coverage="completed manual checklist validated or recorded as an unsigned-package exception",
        copied_package_coverage="completed manual checklist validated",
        app_only_move_behavior="completed manual checklist validated for dragging openQ4.app independently to a user-writable location",
        path_resolution_log_coverage="fs_basepath, fs_cdpath, and fs_savepath confirmed by completed manual checklist",
        gatekeeper_assessment="completed manual checklist validated for signed/notarized DMGs or unsigned development archive friction",
        input_devices=compact_lines("\n".join((usb, bluetooth)), max_lines=6),
        audio_devices=compact_lines(audio, max_lines=4),
        display_modes="completed manual checklist validated",
        known_exceptions=normalize_items(args.known_exception, "none recorded"),
        release_note_limitations=normalize_items(
            args.release_note_limitation,
            "macOS support remains a preview for Apple Silicon/arm64 unless release notes state otherwise",
        ),
        generated_date=datetime.now(timezone.utc).strftime("%Y-%m-%d"),
    )


def bullet_value(label: str, value: str) -> str:
    return f"- {label}: {value}"


def bullet_list_value(label: str, values: tuple[str, ...]) -> str:
    if len(values) == 1:
        return bullet_value(label, values[0])
    rendered = ", ".join(values)
    return bullet_value(label, rendered)


def format_fields(data: EvidenceData) -> str:
    lines = [
        bullet_value("Release/version", data.version),
        bullet_value("Support tier", data.support_tier),
        bullet_value("Run ID", data.run_id),
        bullet_value("Archive path or external artifact URL", data.archive_reference),
        bullet_value("Archive SHA-256", data.archive_sha256),
        bullet_value("Validator command", data.validator_command),
        bullet_value("Validator result", data.validator_result),
        bullet_value("openQ4 commit", data.openq4_commit),
        bullet_value("`openQ4-game` commit", data.gamelibs_commit),
        bullet_list_value("Package artifacts", data.package_artifacts),
        bullet_value("Signing/notarization status", data.signing_status),
        bullet_value("Architecture policy", data.architecture_policy),
        bullet_value("Architecture", data.architecture),
        bullet_value("CPU architecture", data.cpu_architecture),
        bullet_value("OS matrix role", data.os_matrix_role),
        bullet_value("macOS floor evidence", data.macos_floor_evidence),
        bullet_value("Latest public macOS evidence", data.latest_macos_evidence),
        bullet_value("macOS version", data.macos_version),
        bullet_value("Xcode version", data.xcode_version),
        bullet_value("macOS SDK version", data.macos_sdk_version),
        bullet_value("Kernel", data.kernel),
        bullet_value("Hardware model", data.hardware_model),
        bullet_value("CPU", data.cpu),
        bullet_value("GPU/display", data.gpu_display),
        bullet_value("OpenAL provider", data.openal_provider),
        bullet_value("Graphics bridges tested", data.graphics_bridges),
        bullet_value("SP coverage", data.sp_coverage),
        bullet_value("MP coverage", data.mp_coverage),
        bullet_value("Dedicated server coverage", data.dedicated_coverage),
        bullet_value("Finder launch coverage", data.finder_coverage),
        bullet_value("Terminal launch coverage", data.terminal_coverage),
        bullet_value("Package layout contract", data.package_layout_contract),
        bullet_value("Mounted DMG launch coverage", data.mounted_dmg_coverage),
        bullet_value("Copied package launch coverage", data.copied_package_coverage),
        bullet_value("App-only move behavior", data.app_only_move_behavior),
        bullet_value("Path resolution log coverage", data.path_resolution_log_coverage),
        bullet_value("Gatekeeper assessment", data.gatekeeper_assessment),
        bullet_value("Input devices", data.input_devices),
        bullet_value("Audio devices", data.audio_devices),
        bullet_value("Display modes", data.display_modes),
        bullet_list_value("Known exceptions", data.known_exceptions),
        bullet_list_value("Required release-note limitations", data.release_note_limitations),
    ]
    return "\n".join(lines)


def format_checklist(data: EvidenceData) -> str:
    return "\n".join(
        (
            "- [x] Archive validates with `--require-completed-checklist`.",
            "- [x] OpenGL report is present and complete.",
            "- [x] Metal bridge report is present and complete.",
            "- [x] `renderer-smoke` output exists for OpenGL.",
            "- [x] `renderer-smoke` output exists for Metal.",
            "- [x] `renderer-mp-smoke` output exists for OpenGL.",
            "- [x] `renderer-mp-smoke` output exists for Metal.",
            "- [x] `renderer-matrix` output exists for OpenGL.",
            "- [x] `renderer-matrix` output exists for Metal.",
            "- [x] SP entered an in-game map on real Apple hardware.",
            "- [x] MP loaded the game module as `game_mp`, started `mp/q4dm1`, connected a local client, and exited cleanly.",
            "- [x] Dedicated server startup was covered or explicitly documented as unsupported for this release.",
            "- [x] Finder or Desktop launcher startup was checked.",
            "- [x] Terminal startup was checked.",
            "- [x] Mounted signed/notarized DMG launch was checked, or unsigned archive behavior was recorded as an unsigned-package exception.",
            "- [x] Dragging only `openQ4.app` to `/Applications` or another user-writable location was checked.",
            "- [x] Whole-package copied launch was checked for loose client, dedicated-server, and support-tool sibling-runtime discovery.",
            "- [x] `fs_basepath`, `fs_cdpath`, and `fs_savepath` were confirmed in logs for Finder/copied package and terminal launches.",
            "- [x] Gatekeeper assessment was checked for signed/notarized DMGs, or unsigned/unnotarized approval friction was recorded for development archives.",
            "- [x] Keyboard and mouse input were checked.",
            "- [x] Controller hotplug/rumble was checked, or unavailable hardware was recorded as an exception.",
            "- [x] Audio output, volume changes, and at least one device switch or reconnect were checked.",
            "- [x] Windowed, fullscreen, selected-display, and HiDPI/Retina behavior were checked.",
            "- [x] The app contained data under `Contents/Resources/baseoq4` and the signed game module under `Contents/Frameworks`, with no adjacent `baseoq4` duplicate.",
            "- [x] First-class macOS release artifacts are signed/notarized DMGs, or the release stays a preview and unsigned artifacts are labeled as fallback output.",
            "- [x] Architecture policy, CPU architecture, and OS matrix role were recorded.",
            "- [x] Xcode and macOS SDK versions were recorded.",
            "- [x] macOS floor/latest public signoff coverage was recorded or left as an explicit first-class promotion requirement.",
            "- [ ] Release completion links to this evidence.",
            "- [ ] Curated release notes reflect the tested support level and limitations.",
        )
    )


def format_history_record(data: EvidenceData) -> str:
    heading = f"### {data.version} - {data.generated_date} - {data.run_id}"
    return f"{heading}\n\n{format_fields(data)}\n\nChecklist:\n\n{format_checklist(data)}\n"


def format_current_release(data: EvidenceData) -> str:
    return (
        "Generated by `tools/macos/record_signoff_evidence.py`. Re-run the recorder when replacing the accepted archive.\n\n"
        f"{format_fields(data)}\n\n"
        "Current release checklist:\n\n"
        f"{format_checklist(data)}\n"
    )


def replace_section(text: str, heading: str, replacement_body: str) -> str:
    pattern = re.compile(rf"(^## {re.escape(heading[3:])}\n)(.*?)(?=^## |\Z)", re.MULTILINE | re.DOTALL)
    match = pattern.search(text)
    if not match:
        raise RuntimeError(f"Could not find section heading {heading!r}.")
    return text[: match.start(2)] + "\n" + replacement_body.rstrip() + "\n\n" + text[match.end(2) :]


def update_current_status(text: str, data: EvidenceData) -> str:
    replacement = "\n".join(
        (
            f"- [x] Completed macOS signoff evidence is recorded for run `{data.run_id}`.",
            "- [x] OpenGL and Metal bridge result directories are recorded for the current evidence.",
            "- [x] Current evidence includes `renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix` for each bridge.",
            "- [x] Current evidence records architecture policy, CPU architecture, OS matrix role, Xcode version, and macOS SDK version.",
            "- [x] Current evidence records whether the macOS floor and latest public macOS signoff targets are covered or still required.",
            "- [x] Current evidence was validated with completed manual checklist items for SP, MP, Finder launch, terminal launch, mounted-DMG launch, copied-package launch, app-only move behavior, path-resolution logs, Gatekeeper, input, audio, display, and package behavior.",
            "- [ ] `docs/dev/release-completion.md` must reference the accepted evidence before macOS support claims are promoted.",
        )
    )
    return replace_section(text, "## Current Status", replacement)


def update_evidence_history(text: str, record: str) -> str:
    marker = f"{EVIDENCE_HISTORY_HEADING}\n\n"
    if marker not in text:
        raise RuntimeError(f"Could not find {EVIDENCE_HISTORY_HEADING!r}.")
    before, after = text.split(marker, 1)
    after = after.replace("No accepted completed-checklist macOS signoff archive has been recorded yet.\n\n", "", 1)
    insertion_point = after.find("Add new completed evidence records above this line")
    if insertion_point == -1:
        raise RuntimeError("Could not find evidence-history insertion point.")
    updated_after = after[:insertion_point] + record.rstrip() + "\n\n" + after[insertion_point:]
    return before + marker + updated_after


def write_index_text_atomic(index_path: Path, text: str) -> None:
    if index_path.parent.is_symlink():
        raise RuntimeError(f"Evidence index parent must not be a symlink: {index_path.parent}")

    temp_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            newline="\n",
            dir=index_path.parent,
            prefix=f".{index_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temp_file:
            temp_path = Path(temp_file.name)
            temp_file.write(text)
        temp_path.replace(index_path)
    except Exception:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)
        raise


def update_index(index_path: Path, data: EvidenceData) -> None:
    if index_path.is_symlink():
        raise RuntimeError(f"Evidence index must not be a symlink: {index_path}")
    text = index_path.read_text(encoding="utf-8")
    text = update_current_status(text, data)
    text = replace_section(text, CURRENT_RELEASE_HEADING, format_current_release(data))
    text = update_evidence_history(text, format_history_record(data))
    write_index_text_atomic(index_path, text)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="Collected macOS signoff archive.")
    parser.add_argument("--run-id", default="", help="Expected run ID. Defaults to inferring from result directories.")
    parser.add_argument("--bridges", default="opengl,metal", help="Comma-separated bridge list to require.")
    parser.add_argument("--action", default="signoff", help="Expected action token in result directory names.")
    parser.add_argument("--version", default="", help="Release or candidate version label.")
    parser.add_argument("--support-tier", default="preview", help="Support tier to record.")
    parser.add_argument("--artifact-url", default="", help="External URL for the archived evidence, if available.")
    parser.add_argument("--package-artifact", action="append", default=[], help="Package artifact name. Repeat for multiple artifacts.")
    parser.add_argument("--signing-status", default="", help="Signing/notarization status for the tested packages.")
    parser.add_argument("--openq4-commit", default="", help="openQ4 commit. Defaults to git rev-parse HEAD.")
    parser.add_argument("--gamelibs-root", type=Path, default=DEFAULT_GAMELIBS_ROOT, help="Game source checkout; defaults to the canonical engine checkout.")
    parser.add_argument("--gamelibs-commit", default="", help="openQ4-game commit. Defaults to git rev-parse HEAD in --gamelibs-root.")
    parser.add_argument("--known-exception", action="append", default=[], help="Known exception to record. Repeat as needed.")
    parser.add_argument("--release-note-limitation", action="append", default=[], help="Release-note limitation to record. Repeat as needed.")
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX, help="Evidence index to update with --update-index.")
    parser.add_argument("--update-index", action="store_true", help="Update the evidence index in place after validation.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        args.archive = resolve_recording_input(args.archive, "Signoff archive")
        args.gamelibs_root = resolve_recording_input(args.gamelibs_root, "GameLibs root", reject_symlink=False)
        if args.update_index:
            args.index = resolve_recording_input(args.index, "Evidence index")
        else:
            args.index = args.index.resolve()
        validate_recording_arguments(args)

        validator = load_validator()
        bridges = validator.parse_bridges(args.bridges)
        run_id = validator.validate_signoff_archive(
            args.archive,
            run_id=args.run_id or None,
            action=args.action,
            bridges=bridges,
            require_completed_checklist=True,
        )
        reports = read_reports(args.archive, run_id=run_id, action=args.action, bridges=bridges, validator=validator)
        evidence = build_evidence(args, run_id=run_id, bridges=bridges, reports=reports)

        record = format_history_record(evidence)
        print(record)

        if args.update_index:
            update_index(args.index, evidence)
            print(f"Updated macOS signoff evidence index: {args.index}")
        return 0
    except (RuntimeError, tarfile.TarError, OSError, UnicodeDecodeError) as exc:
        print(f"macOS signoff evidence recording failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
