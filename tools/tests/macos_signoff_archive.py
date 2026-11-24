#!/usr/bin/env python3
"""Regression checks for macOS signoff archive validation."""

from __future__ import annotations

import importlib.util
import io
import sys
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True


def load_validator():
    validator_path = ROOT / "tools" / "macos" / "validate_signoff_archive.py"
    spec = importlib.util.spec_from_file_location("validate_signoff_archive_for_test", validator_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import signoff archive validator: {validator_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def report_text(bridge: str, *, completed: bool) -> str:
    checkbox = "x" if completed else " "
    return f"""# macOS Runtime Signoff

- Date (UTC): 2026-06-20T12:00:00Z
- Host: test-mac
- Architecture: arm64
- Architecture policy: arm64-only experimental release matrix
- OS matrix role: latest-public-macos
- Graphics bridge: {bridge}
- OpenAL provider: apple_framework
- Build directory: builddir-{bridge}
- openQ4 commit: 1111111111111111111111111111111111111111
- openQ4 dirty: false
- `openQ4-game` commit: 2222222222222222222222222222222222222222
- `openQ4-game` dirty: false
- Asset basepath: /Users/test/openq4-work/Quake4
- Client: /Users/test/openq4-work/openQ4/.install/openQ4-client_arm64
- Dedicated server: /Users/test/openq4-work/openQ4/.install/openQ4-ded_arm64
- Results: /Users/test/openq4-work/results/testrun-signoff-{bridge}

## Automated Evidence
- [x] Bridge-specific build and staged install completed.
- [x] Staged macOS payload integrity checks completed.
- [x] Quake 4 asset basepath validation completed.
- [x] Renderer smoke profile completed with retail Quake 4 assets.
- [x] Multiplayer listen-server smoke completed with retail Quake 4 assets.
- [x] MP game module path is present in the staged payload.
- [x] macOS-facing renderer validation matrix completed.
- [x] Desktop launcher was written for Finder/Terminal launch checks.
- [x] Package layout contract is self-contained app: data in Contents/Resources/baseoq4 and signed game modules in Contents/Frameworks.
- Renderer smoke output: /Users/test/openq4-work/results/testrun-signoff-{bridge}/renderer-smoke
- Renderer MP smoke output: /Users/test/openq4-work/results/testrun-signoff-{bridge}/renderer-mp-smoke
- Renderer matrix output: /Users/test/openq4-work/results/testrun-signoff-{bridge}/renderer-matrix
- Workflow log: /Users/test/openq4-work/results/testrun-signoff-{bridge}/openq4-macos-workflow.log

## Manual Hardware Checklist
- [{checkbox}] Launch openQ4 from Finder or the Desktop launcher and enter a single-player map.
- [{checkbox}] Launch openQ4.app from the mounted signed/notarized DMG or final release image and enter a single-player map.
- [{checkbox}] Drag only openQ4.app to /Applications or another user-writable location, launch it there, and enter a single-player map.
- [{checkbox}] Copy the whole package payload to a user-writable location and verify the loose client, dedicated server, and support collector still use the sibling app runtime.
- [{checkbox}] Confirm the copied app contains baseoq4 data under Contents/Resources and the signed game module under Contents/Frameworks, with no adjacent baseoq4 duplicate.
- [{checkbox}] Launch the app executable from Terminal with an unrelated working directory.
- [{checkbox}] Confirm fs_basepath, fs_cdpath, and fs_savepath in logs for Finder/copied package and Terminal launches.
- [{checkbox}] Confirm Gatekeeper assessment for signed/notarized DMGs, or record unsigned/unnotarized approval friction for development archives.
- [{checkbox}] Verify keyboard text entry, console toggle, mouse-look, clicks, and wheel input.
- [{checkbox}] Verify at least one SDL game controller, including hotplug and rumble when hardware supports it.
- [{checkbox}] Verify audio output, volume changes, and at least one device switch or reconnect.
- [{checkbox}] Verify windowed, fullscreen, selected-display, and HiDPI/Retina behavior on attached displays.
- [{checkbox}] Verify the matching OpenGL or Metal bridge package path in actual gameplay, not only at the main menu.
- [{checkbox}] Launch multiplayer, load the mp/q4dm1 listen-server path, confirm the game module loads as game_mp, connect a local client, and exit cleanly.
- [{checkbox}] Launch the dedicated server binary, load an MP server configuration far enough to initialize the game module as game_mp, then shut it down cleanly.

## macOS Version
```text
ProductName:        macOS
ProductVersion:     15.5
BuildVersion:       24F74
```

## Xcode And SDK
```text
Xcode: Xcode 16.4 Build version 16F6
macOS SDK: 15.5
macOS SDK path: /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.5.sdk
```

## Kernel
```text
Darwin test-mac 24.5.0 arm64
```

## Hardware
```text
Hardware:

    Hardware Overview:

      Model Name: Mac mini
      Chip: Apple M2
      Total Number of Cores: 8
```

## Displays
```text
Apple Studio Display
```

## Audio Devices
```text
External Headphones
```

## USB Devices
```text
Game Controller
```

## Bluetooth Devices
```text
Wireless Controller
```

## Staged Payload
```text
/Users/test/openq4-work/openQ4/.install/openQ4-client_arm64
/Users/test/openq4-work/openQ4/.install/openQ4-ded_arm64
/Users/test/openq4-work/openQ4/.install/baseoq4/game_arm64.dylib
```

## Staged Binary Architectures
```text
/Users/test/openq4-work/openQ4/.install/openQ4-client_arm64: arm64
/Users/test/openq4-work/openQ4/.install/openQ4-ded_arm64: arm64
/Users/test/openq4-work/openQ4/.install/baseoq4/game_arm64.dylib: arm64
```
"""


def log_text(bridge: str) -> str:
    return f"""Configuring openQ4 (debug, backend=sdl3, macos_graphics_bridge={bridge}, macos_openal_provider=apple_framework)
Compiling openQ4
Staging openQ4 into .install
Validated staged macOS payload for arm64.
Validated Quake 4 asset basepath: /Users/test/openq4-work/Quake4 (25 q4base PK4 files).
Running openQ4 macOS renderer smoke
Running openQ4 macOS multiplayer smoke
Running macOS-facing renderer validation matrix
Installed macOS launcher: /Users/test/Desktop/openQ4.command
macOS runtime signoff report: /Users/test/openq4-work/results/testrun-signoff-{bridge}/macos-runtime-signoff.md
"""


def add_bytes(archive: tarfile.TarFile, name: str, data: bytes, *, mode: int = 0o644) -> None:
    member = tarfile.TarInfo(name)
    member.mode = mode
    member.size = len(data)
    archive.addfile(member, io.BytesIO(data))


def add_file(archive: tarfile.TarFile, name: str, text: str) -> None:
    add_bytes(archive, name, text.encode("utf-8"))


def write_archive(path: Path, *, bridges: tuple[str, ...], completed: bool = False) -> None:
    with tarfile.open(path, "w:gz") as archive:
        for bridge in bridges:
            root = f"testrun-signoff-{bridge}"
            directory = tarfile.TarInfo(root)
            directory.type = tarfile.DIRTYPE
            directory.mode = 0o755
            archive.addfile(directory)
            add_file(archive, f"{root}/macos-runtime-signoff.md", report_text(bridge, completed=completed))
            add_file(archive, f"{root}/openq4-macos-workflow.log", log_text(bridge))
            add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
            add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
            add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_huge_log_archive(path: Path, *, bridge: str, max_text_member_bytes: int) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = f"testrun-signoff-{bridge}"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text(bridge, completed=True))
        add_bytes(archive, f"{root}/openq4-macos-workflow.log", b"x" * (max_text_member_bytes + 1))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_huge_payload_archive(path: Path, *, bridge: str, member_size: int) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = f"testrun-signoff-{bridge}"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text(bridge, completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text(bridge))
        add_bytes(archive, f"{root}/renderer-smoke/oversized.bin", b"x" * member_size)
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_archive_with_unexpected_top_dir(path: Path) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = "testrun-signoff-opengl"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text("opengl", completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text("opengl"))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")
        add_file(archive, "testrun-build-opengl/openq4-macos-workflow.log", "stale\n")


def write_archive_with_metadata_under_result(path: Path) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = "testrun-signoff-opengl"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text("opengl", completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text("opengl"))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")
        add_file(archive, f"{root}/renderer-smoke/.DS_Store", "finder\n")


def write_archive_with_case_duplicate(path: Path) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = "testrun-signoff-opengl"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text("opengl", completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text("opengl"))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-smoke/REPORT.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_archive_with_output_dirs_only(path: Path) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = "testrun-signoff-opengl"
        for name in (
            root,
            f"{root}/renderer-smoke",
            f"{root}/renderer-mp-smoke",
            f"{root}/renderer-matrix",
        ):
            directory = tarfile.TarInfo(name)
            directory.type = tarfile.DIRTYPE
            directory.mode = 0o755
            archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text("opengl", completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text("opengl"))


def write_archive_with_report(path: Path, *, bridge: str, report: str) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = f"testrun-signoff-{bridge}"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report)
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text(bridge))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_archive_with_log(path: Path, *, bridge: str, log: str) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = f"testrun-signoff-{bridge}"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text(bridge, completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log)
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_archive_with_text_control(path: Path, *, bridge: str, report: bool) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = f"testrun-signoff-{bridge}"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        report_body = report_text(bridge, completed=True)
        log_body = log_text(bridge)
        if report:
            report_body += "\nNUL:\x00\n"
        else:
            log_body += "\nNUL:\x00\n"
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_body)
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_body)
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_archive_with_control_character_member(path: Path) -> None:
    with tarfile.open(path, "w:gz") as archive:
        root = "testrun-signoff-opengl"
        directory = tarfile.TarInfo(root)
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        add_file(archive, f"{root}/macos-runtime-signoff.md", report_text("opengl", completed=True))
        add_file(archive, f"{root}/openq4-macos-workflow.log", log_text("opengl"))
        add_file(archive, f"{root}/renderer-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-mp-smoke/report.json", "{}\n")
        add_file(archive, f"{root}/renderer-smoke/bad\nname.txt", "bad\n")
        add_file(archive, f"{root}/renderer-matrix/report.md", "# ok\n")


def write_bad_member_archive(path: Path, name: str, *, duplicate: bool = False) -> None:
    with tarfile.open(path, "w:gz") as archive:
        add_file(archive, name, "bad\n")
        if duplicate:
            add_file(archive, name, "duplicate\n")


def write_bad_mode_member_archive(path: Path, name: str, *, mode: int) -> None:
    with tarfile.open(path, "w:gz") as archive:
        add_bytes(archive, name, b"bad\n", mode=mode)


def expect_error(fragment: str, callback) -> None:
    try:
        callback()
    except Exception as exc:
        if exc.__class__.__name__ != "SignoffArchiveError":
            raise
        if fragment not in str(exc):
            raise AssertionError(f"Unexpected validation error: {exc}") from exc
        return
    raise AssertionError(f"Expected validation error containing {fragment!r}")


def main() -> int:
    validator = load_validator()
    with tempfile.TemporaryDirectory(prefix="openq4-macos-signoff-") as temp_root:
        temp = Path(temp_root)
        good = temp / "openq4-macos-results-testrun.tar.gz"
        write_archive(good, bridges=("opengl", "metal"), completed=False)
        run_id = validator.validate_signoff_archive(
            good,
            run_id=None,
            action="signoff",
            bridges=("opengl", "metal"),
            require_completed_checklist=False,
        )
        if run_id != "testrun":
            raise AssertionError(f"Unexpected inferred run ID: {run_id}")

        symlink_archive = temp / "openq4-macos-results-symlink.tar.gz"
        try:
            symlink_archive.symlink_to(good.name)
        except (OSError, NotImplementedError):
            pass
        else:
            expect_error(
                "must not be a symlink",
                lambda: validator.validate_signoff_archive(
                    symlink_archive,
                    run_id="testrun",
                    action="signoff",
                    bridges=("opengl", "metal"),
                    require_completed_checklist=False,
                ),
            )

        completed_dir = temp / "completed"
        completed_dir.mkdir()
        completed = completed_dir / "openq4-macos-results-testrun.tar.gz"
        write_archive(completed, bridges=("opengl", "metal"), completed=True)
        validator.validate_signoff_archive(
            completed,
            run_id="testrun",
            action="signoff",
            bridges=("opengl", "metal"),
            require_completed_checklist=True,
        )

        renamed_completed = temp / "openq4-macos-results-renamed.tar.gz"
        write_archive(renamed_completed, bridges=("opengl", "metal"), completed=True)
        expect_error(
            "file name does not match run ID",
            lambda: validator.validate_signoff_archive(
                renamed_completed,
                run_id="testrun",
                action="signoff",
                bridges=("opengl", "metal"),
                require_completed_checklist=True,
            ),
        )

        expect_error(
            "still has open manual checklist",
            lambda: validator.validate_signoff_archive(
                good,
                run_id="testrun",
                action="signoff",
                bridges=("opengl", "metal"),
                require_completed_checklist=True,
            ),
        )

        missing_metal = temp / "openq4-macos-results-missing.tar.gz"
        write_archive(missing_metal, bridges=("opengl",), completed=True)
        expect_error(
            "Expected exactly one *-signoff-metal directory",
            lambda: validator.validate_signoff_archive(
                missing_metal,
                run_id=None,
                action="signoff",
                bridges=("opengl", "metal"),
                require_completed_checklist=False,
            ),
        )

        duplicate_member = temp / "openq4-macos-results-duplicate.tar.gz"
        write_bad_member_archive(
            duplicate_member,
            "testrun-signoff-opengl/openq4-macos-workflow.log",
            duplicate=True,
        )
        expect_error(
            "duplicate member",
            lambda: validator.validate_signoff_archive(
                duplicate_member,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        control_character_member = temp / "openq4-macos-results-control-character.tar.gz"
        write_archive_with_control_character_member(control_character_member)
        expect_error(
            "control character",
            lambda: validator.validate_signoff_archive(
                control_character_member,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        empty_segment = temp / "openq4-macos-results-empty-segment.tar.gz"
        write_bad_member_archive(empty_segment, "testrun-signoff-opengl//bad.txt")
        expect_error(
            "empty segment",
            lambda: validator.validate_signoff_archive(
                empty_segment,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        dot_segment = temp / "openq4-macos-results-dot-segment.tar.gz"
        write_bad_member_archive(dot_segment, "testrun-signoff-opengl/./bad.txt")
        expect_error(
            "dot segment",
            lambda: validator.validate_signoff_archive(
                dot_segment,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        long_path = temp / "openq4-macos-results-long-path.tar.gz"
        long_path_name = "testrun-signoff-opengl/" + "/".join(("segment%02d" % index) for index in range(70))
        write_bad_member_archive(long_path, f"{long_path_name}/bad.txt")
        expect_error(
            "Archive path is too long",
            lambda: validator.validate_signoff_archive(
                long_path,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        long_segment = temp / "openq4-macos-results-long-segment.tar.gz"
        long_segment_name = "x" * (validator.MAX_ARCHIVE_PATH_SEGMENT_CHARS + 1)
        write_bad_member_archive(long_segment, f"testrun-signoff-opengl/{long_segment_name}/bad.txt")
        expect_error(
            "Archive path segment is too long",
            lambda: validator.validate_signoff_archive(
                long_segment,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        group_writable_member = temp / "openq4-macos-results-group-writable.tar.gz"
        write_bad_mode_member_archive(
            group_writable_member,
            "testrun-signoff-opengl/openq4-macos-workflow.log",
            mode=0o664,
        )
        expect_error(
            "group/other writable",
            lambda: validator.validate_signoff_archive(
                group_writable_member,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        special_mode_member = temp / "openq4-macos-results-special-mode.tar.gz"
        write_bad_mode_member_archive(
            special_mode_member,
            "testrun-signoff-opengl/openq4-macos-workflow.log",
            mode=0o4644,
        )
        expect_error(
            "special mode bits",
            lambda: validator.validate_signoff_archive(
                special_mode_member,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        huge_log = temp / "openq4-macos-results-huge-log.tar.gz"
        write_huge_log_archive(huge_log, bridge="opengl", max_text_member_bytes=validator.MAX_TEXT_MEMBER_BYTES)
        expect_error(
            "text member is too large",
            lambda: validator.validate_signoff_archive(
                huge_log,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        control_text_report = temp / "openq4-macos-results-control-text-report.tar.gz"
        write_archive_with_text_control(control_text_report, bridge="opengl", report=True)
        expect_error(
            "unsupported control character",
            lambda: validator.validate_signoff_archive(
                control_text_report,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        control_text_log = temp / "openq4-macos-results-control-text-log.tar.gz"
        write_archive_with_text_control(control_text_log, bridge="opengl", report=False)
        expect_error(
            "unsupported control character",
            lambda: validator.validate_signoff_archive(
                control_text_log,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        huge_payload = temp / "openq4-macos-results-huge-payload.tar.gz"
        previous_member_cap = validator.MAX_ARCHIVE_MEMBER_BYTES
        validator.MAX_ARCHIVE_MEMBER_BYTES = 32
        try:
            write_huge_payload_archive(huge_payload, bridge="opengl", member_size=33)
            expect_error(
                "Archive member is too large",
                lambda: validator.validate_signoff_archive(
                    huge_payload,
                    run_id="testrun",
                    action="signoff",
                    bridges=("opengl",),
                    require_completed_checklist=False,
                ),
            )
        finally:
            validator.MAX_ARCHIVE_MEMBER_BYTES = previous_member_cap

        previous_member_count_cap = validator.MAX_ARCHIVE_MEMBERS
        validator.MAX_ARCHIVE_MEMBERS = 4
        try:
            expect_error(
                "too many members",
                lambda: validator.validate_signoff_archive(
                    completed,
                    run_id="testrun",
                    action="signoff",
                    bridges=("opengl",),
                    require_completed_checklist=False,
                ),
            )
        finally:
            validator.MAX_ARCHIVE_MEMBERS = previous_member_count_cap

        previous_total_cap = validator.MAX_ARCHIVE_TOTAL_BYTES
        validator.MAX_ARCHIVE_TOTAL_BYTES = 64
        try:
            expect_error(
                "total expanded size",
                lambda: validator.validate_signoff_archive(
                    completed,
                    run_id="testrun",
                    action="signoff",
                    bridges=("opengl",),
                    require_completed_checklist=False,
                ),
            )
        finally:
            validator.MAX_ARCHIVE_TOTAL_BYTES = previous_total_cap

        missing_client_report = temp / "openq4-macos-results-missing-client.tar.gz"
        write_archive_with_report(
            missing_client_report,
            bridge="opengl",
            report=report_text("opengl", completed=True).replace(
                "- Client: /Users/test/openq4-work/openQ4/.install/openQ4-client_arm64",
                "- Client: not found",
            ),
        )
        expect_error(
            "staged client path",
            lambda: validator.validate_signoff_archive(
                missing_client_report,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        missing_dedicated_report = temp / "openq4-macos-results-missing-dedicated.tar.gz"
        write_archive_with_report(
            missing_dedicated_report,
            bridge="opengl",
            report=report_text("opengl", completed=True).replace(
                "- Dedicated server: /Users/test/openq4-work/openQ4/.install/openQ4-ded_arm64",
                "- Dedicated server: not found",
            ),
        )
        expect_error(
            "staged dedicated server path",
            lambda: validator.validate_signoff_archive(
                missing_dedicated_report,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        wrong_smoke_report = temp / "openq4-macos-results-wrong-smoke-report.tar.gz"
        write_archive_with_report(
            wrong_smoke_report,
            bridge="opengl",
            report=report_text("opengl", completed=True).replace(
                "- Renderer smoke output: /Users/test/openq4-work/results/testrun-signoff-opengl/renderer-smoke",
                "- Renderer smoke output: /tmp/openq4-wrong-renderer-smoke",
            ),
        )
        expect_error(
            "renderer-smoke output directory",
            lambda: validator.validate_signoff_archive(
                wrong_smoke_report,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        wrong_mp_smoke_report = temp / "openq4-macos-results-wrong-mp-smoke-report.tar.gz"
        write_archive_with_report(
            wrong_mp_smoke_report,
            bridge="opengl",
            report=report_text("opengl", completed=True).replace(
                "- Renderer MP smoke output: /Users/test/openq4-work/results/testrun-signoff-opengl/renderer-mp-smoke",
                "- Renderer MP smoke output: /tmp/openq4-wrong-renderer-mp-smoke",
            ),
        )
        expect_error(
            "renderer-mp-smoke output directory",
            lambda: validator.validate_signoff_archive(
                wrong_mp_smoke_report,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        wrong_report_log = temp / "openq4-macos-results-wrong-report-log.tar.gz"
        write_archive_with_log(
            wrong_report_log,
            bridge="opengl",
            log=log_text("opengl").replace(
                "macOS runtime signoff report: /Users/test/openq4-work/results/testrun-signoff-opengl/macos-runtime-signoff.md",
                "macOS runtime signoff report: /tmp/openq4-wrong/macos-runtime-signoff.md",
            ),
        )
        expect_error(
            "expected signoff report path",
            lambda: validator.validate_signoff_archive(
                wrong_report_log,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        expect_error(
            "Invalid signoff archive action token",
            lambda: validator.validate_signoff_archive(
                completed,
                run_id="testrun",
                action="../signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        expect_error(
            "Invalid signoff archive run ID token",
            lambda: validator.validate_signoff_archive(
                completed,
                run_id="../testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        expect_error(
            "token is too long",
            lambda: validator.validate_signoff_archive(
                completed,
                run_id="a" * (validator.MAX_RESULT_TOKEN_CHARS + 1),
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        expect_error(
            "action token is too long",
            lambda: validator.validate_signoff_archive(
                completed,
                run_id="testrun",
                action="a" * (validator.MAX_RESULT_TOKEN_CHARS + 1),
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        expect_error(
            "Bridge list contains duplicates",
            lambda: validator.parse_bridges("opengl,opengl"),
        )

        unexpected_top_dir = temp / "openq4-macos-results-unexpected-top-dir.tar.gz"
        write_archive_with_unexpected_top_dir(unexpected_top_dir)
        expect_error(
            "unexpected top-level result directories",
            lambda: validator.validate_signoff_archive(
                unexpected_top_dir,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        metadata_under_result = temp / "openq4-macos-results-metadata.tar.gz"
        write_archive_with_metadata_under_result(metadata_under_result)
        expect_error(
            "non-runtime macOS metadata",
            lambda: validator.validate_signoff_archive(
                metadata_under_result,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        case_duplicate = temp / "openq4-macos-results-case-duplicate.tar.gz"
        write_archive_with_case_duplicate(case_duplicate)
        expect_error(
            "case-insensitive duplicate",
            lambda: validator.validate_signoff_archive(
                case_duplicate,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

        output_dirs_only = temp / "openq4-macos-results-output-dirs-only.tar.gz"
        write_archive_with_output_dirs_only(output_dirs_only)
        expect_error(
            "missing renderer-smoke output",
            lambda: validator.validate_signoff_archive(
                output_dirs_only,
                run_id="testrun",
                action="signoff",
                bridges=("opengl",),
                require_completed_checklist=False,
            ),
        )

    print("macos_signoff_archive: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
