#!/usr/bin/env python3
"""Regression checks for macOS signoff evidence plumbing."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_evidence_index() -> None:
    evidence = read("docs/dev/macos-signoff-evidence.md")

    for token in (
        "# macOS Signoff Evidence Index",
        "tools/macos/record_signoff_evidence.py",
        "macOS stays a preview, not first-class, unless the current release entry below points to a completed-checklist archive",
        "-RequireCompletedSignoffChecklist",
        "--require-completed-checklist",
        "Archive SHA-256",
        "openQ4 commit",
        "`openQ4-game` commit",
        "Architecture policy",
        "CPU architecture",
        "OS matrix role",
        "macOS floor evidence",
        "Latest public macOS evidence",
        "Xcode version",
        "macOS SDK version",
        "OpenAL provider",
        "Graphics bridges tested",
        "`renderer-smoke`",
        "`renderer-mp-smoke`",
        "`renderer-matrix`",
        "MP loaded the game module as `game_mp`, started `mp/q4dm1`, connected a local client",
        "Dedicated server startup was covered",
        "Mounted DMG launch coverage",
        "Copied package launch coverage",
        "App-only move behavior",
        "Gatekeeper assessment",
        "Required release-note limitations",
        "No accepted completed-checklist macOS signoff archive has been recorded yet.",
    ):
        require(evidence, token, "macOS signoff evidence index")


def validate_release_completion_gate() -> None:
    release = read("docs/dev/release-completion.md")

    for token in (
        "For macOS support claims, complete the \"macOS Evidence Gate\"",
        "## macOS Evidence Gate",
        "`docs/dev/macos-signoff-evidence.md` has a current release entry",
        "archive SHA-256",
        "`openQ4-game` commit",
        "-RequireCompletedSignoffChecklist",
        "validate_signoff_archive.py <archive> --require-completed-checklist",
        "docs/dev/macos-support-matrix-policy.md",
        "macOS floor-version evidence",
        "Latest-public-macOS evidence",
        "`renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix`",
        "MP loaded the game module as `game_mp`, started `mp/q4dm1`, connected a local client",
        "Curated release notes in `docs/dev/releases/vX.Y.Z.md`",
        "record the accepted archive in `docs/dev/macos-signoff-evidence.md`",
        "docs/dev/macos-package-layout-and-release-policy.md",
    ):
        require(release, token, "release completion macOS evidence gate")


def validate_plan_status() -> None:
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")

    for token in (
        "Phase 0: Evidence plumbing",
        "- [x] Create the macOS signoff evidence index.",
        "- [x] Add release-completion fields for macOS evidence.",
        "- [x] Add MP-specific signoff checklist entries.",
        "Phase 0 implementation status",
        "`docs/dev/macos-signoff-evidence.md`",
        "`renderer-mp-smoke`",
    ):
        require(plan, token, "macOS compatibility/support plan Phase 0 status")


def validate_workflow_and_archive_contract() -> None:
    workflow_doc = read("docs/dev/macos-vm-testing-workflow.md")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    archive_validator = read("tools/macos/validate_signoff_archive.py")
    recorder = read("tools/macos/record_signoff_evidence.py")

    for token in (
        "multiplayer `renderer-mp-smoke` output",
        "renderer_gameplay_benchmark.py --profile smoke --cases mp-q4dm1-listen",
        "record_signoff_evidence.py",
    ):
        require(workflow_doc, token, "macOS VM workflow MP evidence documentation")

    for token in (
        "run_mp_smoke",
        "--cases mp-q4dm1-listen",
        '--output-dir "${run_dir}/renderer-mp-smoke"',
        "Multiplayer listen-server smoke completed with retail Quake 4 assets.",
        "MP game module path is present in the staged payload.",
    ):
        require(guest, token, "macOS guest MP evidence generation")

    for token in (
        "Multiplayer listen-server smoke completed with retail Quake 4 assets.",
        "does not reference its renderer-mp-smoke output directory",
        "renderer-mp-smoke output.",
        "MAX_ARCHIVE_PATH_CHARS",
        "MAX_ARCHIVE_PATH_SEGMENT_CHARS",
        "Archive path is too long",
        "Archive path segment is too long",
        "## Xcode And SDK",
        "Architecture policy:",
        "OS matrix role:",
        "## Hardware",
    ):
        require(archive_validator, token, "macOS signoff archive MP evidence validation")

    for token in (
        "validate_signoff_archive",
        "sha256_file",
        "--require-completed-checklist",
        "update_index",
        "## Current Release Evidence",
        "## Evidence History",
        "Architecture policy",
        "OS matrix role",
        "Xcode version",
        "macOS SDK version",
    ):
        require(recorder, token, "macOS signoff evidence recorder")


def validate_ci_and_local_wiring() -> None:
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    local_runner = read("tools/validation/openq4_validate.py")

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "tools/tests/macos_evidence_plumbing.py", context)
        require(source, "python tools/tests/macos_evidence_plumbing.py", context)
        require(source, "tools/tests/macos_evidence_recording.py", context)
        require(source, "python tools/tests/macos_evidence_recording.py", context)
        require(source, "tools/tests/macos_matrix_policy.py", context)
        require(source, "python tools/tests/macos_matrix_policy.py", context)
        require(source, "tools/tests/macos_package_policy.py", context)
        require(source, "python tools/tests/macos_package_policy.py", context)

    require(local_runner, "macos_evidence_plumbing.py", "local validation runner")
    require(local_runner, "macos_evidence_recording.py", "local validation runner")
    require(local_runner, "macos_matrix_policy.py", "local validation runner")
    require(local_runner, "macos_package_policy.py", "local validation runner")


def main() -> None:
    validate_evidence_index()
    validate_release_completion_gate()
    validate_plan_status()
    validate_workflow_and_archive_contract()
    validate_ci_and_local_wiring()
    print("macos_evidence_plumbing: ok")


if __name__ == "__main__":
    main()
