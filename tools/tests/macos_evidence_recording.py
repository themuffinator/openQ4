#!/usr/bin/env python3
"""Regression checks for macOS signoff evidence recording."""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_signoff_fixture():
    fixture_path = ROOT / "tools" / "tests" / "macos_signoff_archive.py"
    spec = importlib.util.spec_from_file_location("macos_signoff_archive_fixture", fixture_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import signoff fixture: {fixture_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_recorder():
    recorder_path = ROOT / "tools" / "macos" / "record_signoff_evidence.py"
    spec = importlib.util.spec_from_file_location("record_signoff_evidence_for_test", recorder_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import signoff evidence recorder: {recorder_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def main() -> int:
    fixture = load_signoff_fixture()
    recorder_module = load_recorder()
    validator = recorder_module.load_validator()
    recorder = (ROOT / "tools" / "macos" / "record_signoff_evidence.py").read_text(encoding="utf-8")
    for token in (
        "def resolve_recording_input",
        "reject_symlink and path.is_symlink()",
        'args.archive = resolve_recording_input(args.archive, "Signoff archive")',
        "if args.update_index:",
        'args.index = resolve_recording_input(args.index, "Evidence index")',
        "def validate_evidence_text",
        "contains control characters",
        "MAX_EVIDENCE_TEXT_VALUE_CHARS",
        "def write_index_text_atomic",
        "tempfile.NamedTemporaryFile",
        "Evidence index parent must not be a symlink",
        "temp_path.replace(index_path)",
        "temp_path.unlink(missing_ok=True)",
        "PACKAGE_ARTIFACT_PATTERN",
        "(?P<bridge>opengl|metal)",
        "artifact_bridges",
        "unexpected_bridges",
        "validator.read_text(archive, report_name)",
        "reports = read_reports(args.archive, run_id=run_id, action=args.action, bridges=bridges, validator=validator)",
        "except (RuntimeError, tarfile.TarError, OSError, UnicodeDecodeError) as exc",
    ):
        require(recorder, token, "macOS signoff evidence symlink guard")

    with tempfile.TemporaryDirectory(prefix="openq4-macos-evidence-") as temp_root:
        temp = Path(temp_root)
        archive = temp / "openq4-macos-results-testrun.tar.gz"
        index = temp / "macos-signoff-evidence.md"
        fixture.write_archive(archive, bridges=("opengl", "metal"), completed=True)
        shutil.copy2(ROOT / "docs" / "dev" / "macos-signoff-evidence.md", index)
        opengl_only_dir = temp / "opengl-only"
        opengl_only_dir.mkdir()
        opengl_only_archive = opengl_only_dir / "openq4-macos-results-testrun.tar.gz"
        fixture.write_archive(opengl_only_archive, bridges=("opengl",), completed=True)
        oversized_report_archive = temp / "openq4-macos-results-oversized-report.tar.gz"
        fixture.write_archive_with_report(
            oversized_report_archive,
            bridge="opengl",
            report=fixture.report_text("opengl", completed=True) + ("x" * validator.MAX_TEXT_MEMBER_BYTES),
        )

        try:
            recorder_module.read_reports(
                oversized_report_archive,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                validator=validator,
            )
        except RuntimeError as exc:
            require(str(exc), "Archive text member is too large", "bounded evidence report parser")
        else:
            raise AssertionError("record_signoff_evidence parsed an oversized signoff report member")

        command = [
            sys.executable,
            str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
            str(archive),
            "--version",
            "vtest",
            "--package-artifact",
            "openq4-vtest-macos-arm64-opengl.dmg",
            "--package-artifact",
            "openq4-vtest-macos-arm64-metal.dmg",
            "--signing-status",
            "signed and notarized test fixture",
            "--openq4-commit",
            "openq4-test-commit",
            "--gamelibs-commit",
            "openq4-game-test-commit",
            "--release-note-limitation",
            "Apple Silicon/arm64 test limitation",
            "--index",
            str(index),
            "--update-index",
        ]
        result = subprocess.run(
            command,
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        output = result.stdout
        updated_index = index.read_text(encoding="utf-8")
        leftover_index_temps = sorted(temp.glob(".macos-signoff-evidence.md.*.tmp"))
        if leftover_index_temps:
            raise AssertionError(f"record_signoff_evidence left temporary index files: {leftover_index_temps}")

        for token in (
            "### vtest",
            "testrun",
            "Archive SHA-256",
            "passed with --require-completed-checklist",
            "openq4-test-commit",
            "openq4-game-test-commit",
            "Mac mini",
            "Apple M2",
            "Architecture policy",
            "arm64-only experimental release matrix",
            "OS matrix role",
            "latest-public-macos",
            "Latest public macOS evidence",
            "covered by this evidence role: latest-public-macos",
            "Xcode 16.4",
            "macOS SDK version",
            "renderer-mp-smoke",
            "MP loaded the game module as `game_mp`, started `mp/q4dm1`",
            "Package layout contract",
            "Mounted DMG launch coverage",
            "fs_basepath, fs_cdpath, and fs_savepath",
            "Gatekeeper assessment",
            "Updated macOS signoff evidence index",
        ):
            require(output, token, "recorded evidence output")

        for token in (
            "Generated by `tools/macos/record_signoff_evidence.py`",
            "- [x] Completed macOS signoff evidence is recorded for run `testrun`.",
            "openq4-vtest-macos-arm64-opengl.dmg, openq4-vtest-macos-arm64-metal.dmg",
            "Apple Silicon/arm64 test limitation",
            "Architecture policy, CPU architecture, and OS matrix role were recorded",
            "Xcode and macOS SDK versions were recorded",
            "macOS floor/latest public signoff coverage was recorded",
            "Dragging only `openQ4.app`",
            "Whole-package copied launch was checked",
            "First-class macOS release artifacts are signed/notarized DMGs",
            "### vtest",
        ):
            require(updated_index, token, "updated evidence index")

        reject(
            updated_index,
            "No accepted completed-checklist macOS signoff archive has been recorded yet.",
            "updated evidence index",
        )

        unsafe_artifact = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(archive),
                "--version",
                "vtest",
                "--package-artifact",
                "../openq4-vtest-macos-arm64-opengl.dmg",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if unsafe_artifact.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted an unsafe package artifact path")
        require(unsafe_artifact.stderr, "macOS signoff evidence recording failed", "unsafe package artifact error")
        require(unsafe_artifact.stderr, "safe filename", "unsafe package artifact rejection")

        missing_bridge_artifact = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(archive),
                "--version",
                "vtest",
                "--package-artifact",
                "openq4-vtest-macos-arm64-opengl.dmg",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if missing_bridge_artifact.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted package artifacts missing a bridge")
        require(missing_bridge_artifact.stderr, "macOS signoff evidence recording failed", "missing bridge artifact error")
        require(missing_bridge_artifact.stderr, "missing the metal bridge artifact", "missing bridge artifact rejection")

        ambiguous_bridge_artifact = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(archive),
                "--version",
                "vtest",
                "--package-artifact",
                "openq4-vtest-macos-arm64-opengl.dmg",
                "--package-artifact",
                "openq4-vtest-macos-arm64-metallic.dmg",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if ambiguous_bridge_artifact.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted an imprecise bridge artifact name")
        require(
            ambiguous_bridge_artifact.stderr,
            "macOS signoff evidence recording failed",
            "ambiguous bridge artifact error",
        )
        require(
            ambiguous_bridge_artifact.stderr,
            "exact macOS arm64 openQ4 opengl/metal package artifact",
            "ambiguous bridge artifact rejection",
        )

        extra_bridge_artifact = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(opengl_only_archive),
                "--version",
                "vtest",
                "--bridges",
                "opengl",
                "--package-artifact",
                "openq4-vtest-macos-arm64-opengl.dmg",
                "--package-artifact",
                "openq4-vtest-macos-arm64-metal.dmg",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if extra_bridge_artifact.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted an unrequested bridge artifact")
        require(
            extra_bridge_artifact.stderr,
            "macOS signoff evidence recording failed",
            "extra bridge artifact error",
        )
        require(
            extra_bridge_artifact.stderr,
            "unrequested bridge artifact",
            "extra bridge artifact rejection",
        )

        unsafe_metadata = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(archive),
                "--version",
                "vtest",
                "--package-artifact",
                "openq4-vtest-macos-arm64-opengl.dmg",
                "--package-artifact",
                "openq4-vtest-macos-arm64-metal.dmg",
                "--signing-status",
                "signed\n## injected heading",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if unsafe_metadata.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted control characters in evidence metadata")
        require(unsafe_metadata.stderr, "macOS signoff evidence recording failed", "unsafe metadata error")
        require(unsafe_metadata.stderr, "signing status contains control characters", "unsafe metadata rejection")

        malformed_archive = temp / "openq4-macos-results-malformed.tar.gz"
        malformed_archive.write_bytes(b"not a gzip archive\n")
        malformed_result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                str(malformed_archive),
                "--version",
                "vtest",
                "--package-artifact",
                "openq4-vtest-macos-arm64-opengl.dmg",
                "--package-artifact",
                "openq4-vtest-macos-arm64-metal.dmg",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if malformed_result.returncode == 0:
            raise AssertionError("record_signoff_evidence accepted a malformed signoff archive")
        require(malformed_result.stderr, "macOS signoff evidence recording failed", "malformed archive error")

        symlink_archive = temp / "openq4-macos-results-symlink.tar.gz"
        try:
            symlink_archive.symlink_to(archive.name)
        except (OSError, NotImplementedError):
            pass
        else:
            symlink_result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                    str(symlink_archive),
                    "--version",
                    "vtest",
                    "--package-artifact",
                    "openq4-vtest-macos-arm64-opengl.dmg",
                    "--package-artifact",
                    "openq4-vtest-macos-arm64-metal.dmg",
                ],
                cwd=str(ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if symlink_result.returncode == 0:
                raise AssertionError("record_signoff_evidence accepted a symlinked signoff archive")
            require(symlink_result.stderr, "macOS signoff evidence recording failed", "symlink archive error")
            require(symlink_result.stderr, "Signoff archive must not be a symlink", "symlink archive rejection")

        symlink_index = temp / "macos-signoff-evidence-symlink.md"
        try:
            symlink_index.symlink_to(index.name)
        except (OSError, NotImplementedError):
            pass
        else:
            symlink_index_result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "macos" / "record_signoff_evidence.py"),
                    str(archive),
                    "--version",
                    "vtest",
                    "--package-artifact",
                    "openq4-vtest-macos-arm64-opengl.dmg",
                    "--package-artifact",
                    "openq4-vtest-macos-arm64-metal.dmg",
                    "--index",
                    str(symlink_index),
                    "--update-index",
                ],
                cwd=str(ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if symlink_index_result.returncode == 0:
                raise AssertionError("record_signoff_evidence accepted a symlinked evidence index")
            require(symlink_index_result.stderr, "macOS signoff evidence recording failed", "symlink index error")
            require(symlink_index_result.stderr, "Evidence index must not be a symlink", "symlink index rejection")

    print("macos_evidence_recording: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
