#!/usr/bin/env python3
"""Focused safety tests for Linux ARM64 release evidence binding."""

from __future__ import annotations

import ast
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from copy import deepcopy
from dataclasses import replace
from pathlib import Path
from types import ModuleType
from typing import Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
HELPER_PATH = REPO_ROOT / "tools" / "build" / "verify_linux_arm64_release_evidence.py"
SP_RUNNER_PATH = REPO_ROOT / "tools" / "tests" / "linux_wayland_stock_sp_smoke.py"
DEDICATED_RUNNER_PATH = REPO_ROOT / "tools" / "tests" / "linux_dedicated_stock_map_smoke.py"


def load_helper() -> ModuleType:
    spec = importlib.util.spec_from_file_location(
        "verify_linux_arm64_release_evidence", HELPER_PATH
    )
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


HELPER = load_helper()

EXPECTED = HELPER.CandidateMetadata(
    openq4_commit="1" * 40,
    openq4_game_commit="2" * 40,
    release_version="0.8.1",
    version_tag="0.8.1",
    release_tag="v0.8.1",
    package_filename="openq4-0.8.1-linux-arm64.tar.xz",
)

FIXTURE_BYTES = {
    "client": b"synthetic ARM64 client\x00\x01\n",
    "dedicated": b"synthetic ARM64 dedicated server\x02\n",
    "game": b"synthetic ARM64 game module\x03\n",
}

ACCEPTED_REVIEW_VALUES = {
    "reviewer": "Synthetic Reviewer",
    "reviewed_at": "2026-07-14",
    "hardware_and_os": "Physical AArch64 test system and Linux distribution recorded",
    "compositor_and_graphics": "Native Wayland compositor and desktop OpenGL recorded",
    "audio_and_input_devices": "Audio keyboard mouse and controller devices recorded",
    "sp_evidence": "Single-player gameplay and save-load report retained",
    "mp_evidence": "Multiplayer host and client gameplay report retained",
    "dedicated_server_evidence": "Dedicated server and client gameplay report retained",
    "audio_input_display_evidence": "Audio input fullscreen and display checks retained",
    "logs_and_screenshots": "Runtime logs reports and gameplay screenshots retained",
    "accepted_limitations": "No candidate-specific limitations were accepted",
}


def expect_error(action: Callable[[], object], expected_text: str, label: str) -> None:
    try:
        action()
    except RuntimeError as exc:
        if expected_text not in str(exc):
            raise AssertionError(
                f"{label} failed with the wrong error: {exc!s}"
            ) from exc
    else:
        raise AssertionError(f"{label} unexpectedly succeeded")


def sha256_bytes(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def write_runtime_fixture(root: Path) -> None:
    for key, relative_path in HELPER.ARM64_RUNTIME_PATHS.items():
        output = root.joinpath(*relative_path.parts)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(FIXTURE_BYTES[key])


def accepted_manifest_text(
    pending_text: str,
    runtime_report_hashes: dict[str, str],
) -> str:
    result = pending_text.replace('status = "pending"', 'status = "accepted"', 1)
    for key, value in ACCEPTED_REVIEW_VALUES.items():
        marker = f'{key} = "pending"'
        if marker not in result:
            raise AssertionError(f"pending candidate omitted review field {key}")
        result = result.replace(marker, f'{key} = "{value}"', 1)
    for key, value in runtime_report_hashes.items():
        marker = f'{key} = "pending"'
        if marker not in result:
            raise AssertionError(f"pending candidate omitted runtime report field {key}")
        result = result.replace(marker, f'{key} = "{value}"', 1)
    return result


def execution_evidence() -> dict[str, object]:
    unavailable_probe = {
        "available": False,
        "returnCode": None,
        "identifier": "",
        "error": "command not found",
    }
    inspection = {
        "schemaVersion": 1,
        "scope": (
            "Best-effort rejection of known VM/emulator indicators; absence of an "
            "indicator does not prove physical hardware and operator attestation remains required."
        ),
        "virtualMachineDetected": False,
        "containerDetected": False,
        "virtualizationIndicators": [],
        "systemdDetectVirt": {
            "vm": dict(unavailable_probe),
            "container": dict(unavailable_probe),
        },
        "hostIdentity": {
            "deviceTreeModel": "Synthetic physical AArch64 board",
            "kernelRelease": "6.8.0-aarch64",
        },
        "cpuHypervisorFlag": False,
    }
    return {
        "nativeProcessArchitecture": True,
        "hostArchitecture": "arm64",
        "physicalHardwareAttested": True,
        "physicalHardwareAttestationSource": (
            "operator CLI flag; known-virtualization inspection passed"
        ),
        "virtualMachineDetected": False,
        "containerDetected": False,
        "virtualizationInspection": inspection,
    }


def elf_metadata(keys: tuple[str, ...]) -> dict[str, object]:
    return {
        key: {"class": "ELF64", "data": "little-endian", "machine": 183}
        for key in keys
    }


def screenshot_result(name: str) -> dict[str, object]:
    width = 320
    height = 200
    bits_per_pixel = 24
    return {
        "status": "pass",
        "path": f"/tmp/openq4/{name}.tga",
        "bytes": 18 + width * height * (bits_per_pixel // 8),
        "sha256": sha256_bytes(f"synthetic {name} screenshot".encode("utf-8")),
        "width": width,
        "height": height,
        "bitsPerPixel": bits_per_pixel,
        "imageType": 2,
        "sampledUniqueColours": 64,
        "sampledChannelRange": 128,
    }


def save_result() -> dict[str, object]:
    minimum_bytes = {"save": 4096, "tga": 65536, "txt": 1}
    return {
        "status": "pass",
        "directory": "/tmp/openq4/baseoq4/savegames",
        "files": {
            key: {
                "path": f"/tmp/openq4/baseoq4/savegames/linux_wayland_roundtrip.{key}",
                "bytes": minimum,
                "minimumBytes": minimum,
                "sha256": sha256_bytes(f"synthetic {key} save artifact".encode("utf-8")),
                "valid": True,
            }
            for key, minimum in minimum_bytes.items()
        },
        "temporaryFiles": [],
    }


def runtime_report_documents(candidate_hashes: dict[str, str]) -> dict[str, dict[str, object]]:
    retail_hash = sha256_bytes(b"synthetic unmodified retail pak001.pk4")
    stock_sp = {
        "reportSchemaVersion": 1,
        "reportType": "linux-wayland-stock-sp",
        "status": "pass",
        "architecture": "arm64",
        "host": "Linux 6.8.0 aarch64",
        "map": "game/airdefense1",
        "saveSlot": "linux_wayland_roundtrip",
        "nativeWayland": True,
        "waylandDisplay": "wayland-0",
        "waylandSocket": "/run/user/1000/wayland-0",
        "executionEvidence": execution_evidence(),
        "audioEvidence": {
            "softwareInitializationPassed": True,
            "softwareMarkers": {
                "OpenAL setup began": True,
                "OpenAL ALC version reported": True,
                "OpenAL active device reported": True,
                "engine sound system initialized": True,
            },
            "activeDeviceReported": True,
            "activeDevice": "Synthetic OpenAL device",
            "humanAudiblePlaybackVerified": False,
            "humanPlaybackAttestationSource": "not provided",
            "automationHeardAudio": False,
            "scope": "Automation verifies software initialization; audible output requires a human.",
        },
        "secondActiveGameplayLifecycle": True,
        "lifecycleCounts": {
            "mapInitialization": 2,
            "firstActiveDraw": 2,
            "gameMapShutdown": 1,
        },
        "lifecycleOrderFailures": [],
        "clientMarkers": {key: True for key in HELPER.SP_CLIENT_MARKER_KEYS},
        "saveFiles": save_result(),
        "screenshot": screenshot_result("stock-sp"),
        "cleanExit": True,
        "exitCode": 0,
        "abortReason": "",
        "missingMarkers": [],
        "fatalMarkers": [],
        "glErrorMarkers": [],
        "audioErrorMarkers": [],
        "elapsedSeconds": 1.5,
        "openQ4Commit": EXPECTED.openq4_commit,
        "openQ4GameCommit": EXPECTED.openq4_game_commit,
        "sha256": {
            "client": candidate_hashes["client"],
            "gameSp": candidate_hashes["game"],
            "retailPak001": retail_hash,
        },
        "elf": elf_metadata(("client", "gameSp")),
        "paths": {"installRoot": "/tmp/openq4"},
        "command": ["/tmp/openq4/openQ4-client_arm64"],
        "environmentContract": {
            "SDL_VIDEO_DRIVER": "wayland",
            "SDL_VIDEODRIVER": "wayland",
            "DISPLAYRemoved": True,
            "OPENQ4_FORCE_X11Removed": True,
        },
    }
    dedicated = {
        "reportSchemaVersion": 1,
        "reportType": "linux-wayland-stock-dedicated",
        "status": "pass",
        "architecture": "arm64",
        "host": "Linux 6.8.0 aarch64",
        "map": "mp/q4dm1",
        "port": 28120,
        "nativeWayland": True,
        "waylandDisplay": "wayland-0",
        "waylandSocket": "/run/user/1000/wayland-0",
        "executionEvidence": execution_evidence(),
        "serverReady": True,
        "serverHeadless": True,
        "serverVideoMarkers": [],
        "serverLogNonempty": True,
        "clientLogNonempty": True,
        "serverLifecycleOrderFailures": [],
        "clientLifecycleOrderFailures": [],
        "serverExitCode": 0,
        "clientExitCode": 0,
        "clientAbortReason": "",
        "serverCleanShutdown": True,
        "clientCleanShutdown": True,
        "serverDeclChecksum": "0x3599935c",
        "clientDeclChecksum": "0x3599935c",
        "matchingDeclChecksum": True,
        "serverMarkers": {
            key: True for key in HELPER.DEDICATED_SERVER_MARKER_KEYS
        },
        "clientMarkers": {
            key: True for key in HELPER.DEDICATED_CLIENT_MARKER_KEYS
        },
        "screenshot": screenshot_result("dedicated-client"),
        "missingMarkers": [],
        "fatalMarkers": [],
        "elapsedSeconds": 2.0,
        "openQ4Commit": EXPECTED.openq4_commit,
        "openQ4GameCommit": EXPECTED.openq4_game_commit,
        "sha256": {
            "dedicated": candidate_hashes["dedicated"],
            "client": candidate_hashes["client"],
            "gameMp": candidate_hashes["game"],
            "retailPak001": retail_hash,
        },
        "elf": elf_metadata(("dedicated", "client", "gameMp")),
        "paths": {"installRoot": "/tmp/openq4"},
        "serverCommand": ["/tmp/openq4/openQ4-ded_arm64"],
        "clientCommand": ["/tmp/openq4/openQ4-client_arm64"],
    }
    return {"stock_sp": stock_sp, "dedicated": dedicated}


def write_json(path: Path, document: object) -> bytes:
    contents = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
    path.write_bytes(contents)
    return contents


def generated_report_root_keys(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    matches: list[set[str]] = []
    for node in ast.walk(tree):
        if (
            isinstance(node, ast.AnnAssign)
            and isinstance(node.target, ast.Name)
            and node.target.id == "report"
            and isinstance(node.value, ast.Dict)
        ):
            keys = {
                key.value
                for key in node.value.keys
                if isinstance(key, ast.Constant) and isinstance(key.value, str)
            }
            matches.append(keys)
    if len(matches) != 1:
        raise AssertionError(f"expected one annotated report dictionary in {path}; got {len(matches)}")
    return matches[0]


def generated_named_dict_keys(path: Path, variable_name: str) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    matches: list[set[str]] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.Dict):
            continue
        if not any(
            isinstance(target, ast.Name) and target.id == variable_name
            for target in node.targets
        ):
            continue
        keys = {
            key.value
            for key in node.value.keys
            if isinstance(key, ast.Constant) and isinstance(key.value, str)
        }
        matches.append(keys)
    if len(matches) != 1:
        raise AssertionError(
            f"expected one {variable_name} dictionary in {path}; got {len(matches)}"
        )
    return matches[0]


def main() -> None:
    for path, expected_keys in (
        (SP_RUNNER_PATH, HELPER.SP_REPORT_KEYS),
        (DEDICATED_RUNNER_PATH, HELPER.DEDICATED_REPORT_KEYS),
    ):
        actual_keys = generated_report_root_keys(path)
        if actual_keys != expected_keys:
            raise AssertionError(
                f"runtime report/verifier root schema drift in {path.name}: "
                f"missing={sorted(expected_keys - actual_keys)}, "
                f"unknown={sorted(actual_keys - expected_keys)}"
            )

    for path, variable_name, expected_keys in (
        (SP_RUNNER_PATH, "audio_markers", HELPER.SP_AUDIO_MARKER_KEYS),
        (SP_RUNNER_PATH, "client_markers", HELPER.SP_CLIENT_MARKER_KEYS),
        (
            DEDICATED_RUNNER_PATH,
            "server_after_markers",
            HELPER.DEDICATED_SERVER_MARKER_KEYS,
        ),
        (
            DEDICATED_RUNNER_PATH,
            "client_markers",
            HELPER.DEDICATED_CLIENT_MARKER_KEYS,
        ),
    ):
        actual_keys = generated_named_dict_keys(path, variable_name)
        if actual_keys != expected_keys:
            raise AssertionError(
                f"runtime report/verifier marker schema drift in {path.name} "
                f"({variable_name}): missing={sorted(expected_keys - actual_keys)}, "
                f"unknown={sorted(actual_keys - expected_keys)}"
            )

    with tempfile.TemporaryDirectory(prefix="openq4-arm64-release-evidence-") as temp_dir:
        temp_root = Path(temp_dir)
        runtime_root = temp_root / "runtime"
        package_dir = temp_root / "package"
        runtime_root.mkdir()
        package_dir.mkdir()
        write_runtime_fixture(runtime_root)
        write_runtime_fixture(package_dir)

        archive = temp_root / EXPECTED.package_filename
        archive_bytes = b"synthetic deterministic Linux ARM64 release archive\x00\xff"
        archive.write_bytes(archive_bytes)
        candidate_path = temp_root / "candidate.toml"

        # Candidate generation must bind the template to all actual release bytes.
        generated_hashes = HELPER.write_candidate(
            candidate_path, runtime_root, package_dir, archive, EXPECTED
        )
        pending_text = candidate_path.read_text(encoding="utf-8")
        pending = HELPER.parse_evidence(
            HELPER.read_manifest(candidate_path), require_accepted=False
        )
        if pending.status != "pending" or set(pending.review.values()) != {"pending"}:
            raise AssertionError("generated candidate is not explicitly pending review")
        if set(pending.runtime_reports.values()) != {"pending"}:
            raise AssertionError("generated candidate does not leave runtime reports pending")
        if pending.candidate != EXPECTED:
            raise AssertionError("generated candidate metadata changed")
        expected_hashes = {
            **{key: sha256_bytes(value) for key, value in FIXTURE_BYTES.items()},
            "archive": sha256_bytes(archive_bytes),
        }
        if dict(generated_hashes) != expected_hashes or dict(pending.sha256) != expected_hashes:
            raise AssertionError("generated candidate hashes do not match fixture bytes")

        preview_named_archive = temp_root / "openq4-0.8.1-linux-arm64-preview.tar.xz"
        preview_named_archive.write_bytes(archive_bytes)
        expect_error(
            lambda: HELPER.write_candidate(
                temp_root / "preview-named-candidate.toml",
                runtime_root,
                package_dir,
                preview_named_archive,
                EXPECTED,
            ),
            "does not match expected package filename",
            "preview-named first-class candidate rejection",
        )

        report_documents = runtime_report_documents(expected_hashes)
        stock_sp_report = temp_root / "stock-sp-report.json"
        dedicated_report = temp_root / "dedicated-report.json"
        report_contents = {
            "stock_sp": write_json(stock_sp_report, report_documents["stock_sp"]),
            "dedicated": write_json(dedicated_report, report_documents["dedicated"]),
        }
        report_hashes = {
            key: sha256_bytes(contents) for key, contents in report_contents.items()
        }
        report_paths = HELPER.RuntimeReportPaths(
            stock_sp=stock_sp_report,
            dedicated=dedicated_report,
        )

        def validate_manifest(
            path: Path,
            expected: object = EXPECTED,
            reports: object = report_paths,
        ) -> object:
            return HELPER.validate_evidence_manifest(path, expected, reports)

        # Pending records can be generated and inspected but never authorize a release.
        expect_error(
            lambda: validate_manifest(candidate_path),
            'status must be "accepted"',
            "pending evidence rejection",
        )

        unreviewed_path = temp_root / "accepted-but-unreviewed.toml"
        unreviewed_path.write_text(
            pending_text.replace('status = "pending"', 'status = "accepted"', 1),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(unreviewed_path),
            "contains a placeholder value",
            "accepted-but-pending qualitative review rejection",
        )

        accepted_path = temp_root / "accepted.toml"
        accepted_text = accepted_manifest_text(pending_text, report_hashes)
        accepted_path.write_text(accepted_text, encoding="utf-8", newline="\n")
        accepted = validate_manifest(accepted_path)
        if accepted.status != "accepted":
            raise AssertionError("accepted fixture did not validate")
        HELPER.verify_release_evidence(
            accepted_path,
            runtime_root,
            package_dir,
            archive,
            EXPECTED,
            report_paths,
        )

        cli_result = subprocess.run(
            [
                sys.executable,
                str(HELPER_PATH),
                "validate",
                "--manifest",
                str(accepted_path),
                "--stock-sp-report",
                str(stock_sp_report),
                "--dedicated-report",
                str(dedicated_report),
                "--expected-openq4-commit",
                EXPECTED.openq4_commit,
                "--expected-openq4-game-commit",
                EXPECTED.openq4_game_commit,
                "--expected-release-version",
                EXPECTED.release_version,
                "--expected-version-tag",
                EXPECTED.version_tag,
                "--expected-release-tag",
                EXPECTED.release_tag,
                "--expected-package-filename",
                EXPECTED.package_filename,
                "--print-archive-sha256",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if cli_result.returncode != 0:
            raise AssertionError(
                "archive-hash-only validation failed:\n"
                + cli_result.stdout
                + cli_result.stderr
            )
        if cli_result.stdout.strip() != expected_hashes["archive"]:
            raise AssertionError(
                "archive-hash-only validation emitted an unexpected value: "
                f"{cli_result.stdout!r}"
            )

        def runtime_paths_with(key: str, path: Path) -> object:
            return HELPER.RuntimeReportPaths(
                stock_sp=path if key == "stock_sp" else stock_sp_report,
                dedicated=path if key == "dedicated" else dedicated_report,
            )

        def expect_raw_report_error(
            key: str,
            name: str,
            contents: bytes,
            expected_error: str,
            *,
            bind_modified_hash: bool = True,
        ) -> None:
            report_path = temp_root / f"invalid-{name}.json"
            report_path.write_bytes(contents)
            manifest_path = temp_root / f"invalid-{name}.toml"
            manifest_text = accepted_text
            if bind_modified_hash:
                manifest_text = manifest_text.replace(
                    report_hashes[key], sha256_bytes(contents), 1
                )
            manifest_path.write_text(manifest_text, encoding="utf-8", newline="\n")
            alternate_paths = runtime_paths_with(key, report_path)
            selected_manifest = manifest_path if bind_modified_hash else accepted_path
            expect_error(
                lambda: validate_manifest(
                    selected_manifest, EXPECTED, alternate_paths
                ),
                expected_error,
                f"{name} runtime report rejection",
            )

        def expect_modified_report_error(
            key: str,
            name: str,
            mutation: Callable[[dict[str, object]], None],
            expected_error: str,
        ) -> None:
            document = deepcopy(report_documents[key])
            mutation(document)
            contents = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
            expect_raw_report_error(key, name, contents, expected_error)

        # The report hash is checked before its contents, so even harmless byte
        # drift cannot be substituted for the exact accepted JSON file.
        expect_raw_report_error(
            "stock_sp",
            "unbound-byte-drift",
            report_contents["stock_sp"] + b"\n",
            "stock_sp runtime report SHA-256",
            bind_modified_hash=False,
        )
        expect_modified_report_error(
            "stock_sp",
            "unknown-root-field",
            lambda document: document.update({"unexpected": "rejected"}),
            "unknown keys: unexpected",
        )

        def remove_report_type(document: dict[str, object]) -> None:
            document.pop("reportType")

        expect_modified_report_error(
            "stock_sp",
            "missing-report-type",
            remove_report_type,
            "missing keys: reportType",
        )
        expect_modified_report_error(
            "stock_sp",
            "wrong-architecture",
            lambda document: document.update({"architecture": "x64"}),
            "must be exactly 'arm64'",
        )
        expect_modified_report_error(
            "stock_sp",
            "non-wayland",
            lambda document: document.update({"nativeWayland": False}),
            "nativeWayland must be true",
        )

        def remove_physical_attestation(document: dict[str, object]) -> None:
            execution = document["executionEvidence"]
            assert isinstance(execution, dict)
            execution["physicalHardwareAttested"] = False

        expect_modified_report_error(
            "stock_sp",
            "missing-physical-attestation",
            remove_physical_attestation,
            "physicalHardwareAttested must be true",
        )

        def insert_virtual_machine_indicator(document: dict[str, object]) -> None:
            execution = document["executionEvidence"]
            assert isinstance(execution, dict)
            inspection = execution["virtualizationInspection"]
            assert isinstance(inspection, dict)
            identity = inspection["hostIdentity"]
            assert isinstance(identity, dict)
            identity["deviceTreeModel"] = "QEMU Virtual Machine"

        expect_modified_report_error(
            "stock_sp",
            "virtual-machine-identity",
            insert_virtual_machine_indicator,
            "known VM/emulator identity",
        )

        def replace_sp_markers_with_arbitrary_true_marker(
            document: dict[str, object],
        ) -> None:
            document["clientMarkers"] = {"synthetic unrelated marker": True}

        expect_modified_report_error(
            "stock_sp",
            "arbitrary-sp-marker",
            replace_sp_markers_with_arbitrary_true_marker,
            "missing keys:",
        )

        def replace_audio_markers_with_arbitrary_true_marker(
            document: dict[str, object],
        ) -> None:
            audio = document["audioEvidence"]
            assert isinstance(audio, dict)
            audio["softwareMarkers"] = {"synthetic unrelated marker": True}

        expect_modified_report_error(
            "stock_sp",
            "arbitrary-audio-marker",
            replace_audio_markers_with_arbitrary_true_marker,
            "missing keys:",
        )

        expect_modified_report_error(
            "stock_sp",
            "status-only-save-result",
            lambda document: document.update({"saveFiles": {"status": "pass"}}),
            "missing keys:",
        )
        expect_modified_report_error(
            "stock_sp",
            "status-only-screenshot-result",
            lambda document: document.update({"screenshot": {"status": "pass"}}),
            "missing keys:",
        )

        def insert_impossible_systemd_probe(document: dict[str, object]) -> None:
            execution = document["executionEvidence"]
            assert isinstance(execution, dict)
            inspection = execution["virtualizationInspection"]
            assert isinstance(inspection, dict)
            systemd = inspection["systemdDetectVirt"]
            assert isinstance(systemd, dict)
            vm_probe = systemd["vm"]
            assert isinstance(vm_probe, dict)
            vm_probe.update(
                {
                    "available": True,
                    "returnCode": None,
                    "identifier": "",
                    "error": "",
                }
            )

        expect_modified_report_error(
            "stock_sp",
            "impossible-systemd-probe-state",
            insert_impossible_systemd_probe,
            "describe an impossible probe state",
        )

        def record_container_execution(document: dict[str, object]) -> None:
            execution = document["executionEvidence"]
            assert isinstance(execution, dict)
            execution["containerDetected"] = True
            inspection = execution["virtualizationInspection"]
            assert isinstance(inspection, dict)
            inspection["containerDetected"] = True
            systemd = inspection["systemdDetectVirt"]
            assert isinstance(systemd, dict)
            container_probe = systemd["container"]
            assert isinstance(container_probe, dict)
            container_probe.update(
                {
                    "available": True,
                    "returnCode": 0,
                    "identifier": "docker",
                    "error": "",
                }
            )

        expect_modified_report_error(
            "stock_sp",
            "undisclosed-container",
            record_container_execution,
            "does not explicitly disclose it",
        )

        container_document = deepcopy(report_documents["stock_sp"])
        record_container_execution(container_document)
        container_contents = (
            json.dumps(container_document, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
        container_report = temp_root / "disclosed-container-report.json"
        container_report.write_bytes(container_contents)
        container_manifest = temp_root / "disclosed-container.toml"
        disclosed_hardware = (
            "Physical AArch64 host; smoke report executed inside a Docker container"
        )
        container_manifest.write_text(
            accepted_text.replace(
                report_hashes["stock_sp"], sha256_bytes(container_contents), 1
            ).replace(
                f'hardware_and_os = "{ACCEPTED_REVIEW_VALUES["hardware_and_os"]}"',
                f'hardware_and_os = "{disclosed_hardware}"',
                1,
            ),
            encoding="utf-8",
            newline="\n",
        )
        validate_manifest(
            container_manifest,
            EXPECTED,
            runtime_paths_with("stock_sp", container_report),
        )

        def replace_client_hash(document: dict[str, object]) -> None:
            hashes = document["sha256"]
            assert isinstance(hashes, dict)
            hashes["client"] = "0" * 64

        expect_modified_report_error(
            "stock_sp",
            "wrong-client-hash",
            replace_client_hash,
            "does not match accepted candidate hash",
        )
        expect_modified_report_error(
            "dedicated",
            "failed-dedicated-readiness",
            lambda document: document.update({"serverReady": False}),
            "serverReady must be true",
        )
        expect_modified_report_error(
            "dedicated",
            "noncanonical-declaration-checksums",
            lambda document: document.update(
                {"serverDeclChecksum": "same", "clientDeclChecksum": "same"}
            ),
            "canonical lowercase 32-bit hexadecimal checksum",
        )

        def replace_dedicated_markers_with_arbitrary_true_marker(
            document: dict[str, object],
        ) -> None:
            document["serverMarkers"] = {"synthetic unrelated marker": True}

        expect_modified_report_error(
            "dedicated",
            "arbitrary-dedicated-marker",
            replace_dedicated_markers_with_arbitrary_true_marker,
            "missing keys:",
        )
        
        def replace_retail_hash(document: dict[str, object]) -> None:
            hashes = document["sha256"]
            assert isinstance(hashes, dict)
            hashes["retailPak001"] = "f" * 64

        expect_modified_report_error(
            "dedicated",
            "different-retail-media",
            replace_retail_hash,
            "used different retail pak001.pk4 bytes",
        )

        duplicate_key_contents = report_contents["stock_sp"].replace(
            b"{\n",
            b'{\n  "reportType": "linux-wayland-stock-sp",\n',
            1,
        )
        expect_raw_report_error(
            "stock_sp",
            "duplicate-json-key",
            duplicate_key_contents,
            "duplicate JSON object key: reportType",
        )
        non_finite_contents = report_contents["stock_sp"].replace(
            b'"elapsedSeconds": 1.5', b'"elapsedSeconds": NaN', 1
        )
        expect_raw_report_error(
            "stock_sp",
            "non-finite-json-number",
            non_finite_contents,
            "non-finite JSON number: NaN",
        )
        expect_raw_report_error(
            "stock_sp",
            "malformed-json",
            b"{\n",
            "malformed JSON",
        )
        expect_raw_report_error(
            "stock_sp",
            "oversized-json",
            b" " * (HELPER.MAX_RUNTIME_REPORT_BYTES + 1),
            "exceeds the maximum size",
        )

        report_symlink = temp_root / "runtime-report-symlink.json"
        try:
            os.symlink(stock_sp_report, report_symlink)
        except OSError:
            pass
        else:
            expect_error(
                lambda: validate_manifest(
                    accepted_path,
                    EXPECTED,
                    runtime_paths_with("stock_sp", report_symlink),
                ),
                "must not be a symlink",
                "symlink runtime report rejection",
            )

        review_failures = (
            ("reviewer", "x", "placeholder value"),
            ("reviewer", "---", "placeholder value"),
            ("reviewer", "None", "placeholder value"),
            ("reviewed_at", "2026-02-30", "valid ISO 8601 calendar date"),
            ("reviewed_at", "14-07-2026", "ISO 8601 date in YYYY-MM-DD form"),
            ("hardware_and_os", "short", "too short to be meaningful"),
            ("hardware_and_os", "xxxxxxxxxxxx", "placeholder value"),
            ("sp_evidence", "TODO: attach it", "placeholder value"),
            ("sp_evidence", "not recorded yet", "placeholder value"),
            ("mp_evidence", "no evidence retained", "placeholder value"),
            ("accepted_limitations", "n/a", "placeholder value"),
        )
        for field_name, invalid_value, expected_error in review_failures:
            invalid_review_path = temp_root / f"invalid-review-{field_name}.toml"
            valid_value = ACCEPTED_REVIEW_VALUES[field_name]
            invalid_review_path.write_text(
                accepted_text.replace(
                    f'{field_name} = "{valid_value}"',
                    f'{field_name} = "{invalid_value}"',
                    1,
                ),
                encoding="utf-8",
            )
            expect_error(
                lambda path=invalid_review_path: validate_manifest(path),
                expected_error,
                f"invalid review.{field_name} rejection",
            )

        no_limitations_path = temp_root / "accepted-no-limitations.toml"
        no_limitations_path.write_text(
            accepted_text.replace(
                f'accepted_limitations = "{ACCEPTED_REVIEW_VALUES["accepted_limitations"]}"',
                'accepted_limitations = "none"',
                1,
            ),
            encoding="utf-8",
        )
        validate_manifest(no_limitations_path)

        # Unknown keys, non-canonical hashes, malformed TOML, and candidate drift fail closed.
        unknown_path = temp_root / "unknown.toml"
        unknown_path.write_text(
            accepted_text.replace(
                'status = "accepted"',
                'status = "accepted"\nunexpected = "not allowed"',
                1,
            ),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(unknown_path),
            "unknown keys: unexpected",
            "unknown manifest key rejection",
        )

        missing_key_path = temp_root / "missing-key.toml"
        missing_key_path.write_text(
            accepted_text.replace(
                f'release_tag = "{EXPECTED.release_tag}"\n', "", 1
            ),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(missing_key_path),
            "missing keys: release_tag",
            "missing manifest key rejection",
        )

        uppercase_hash_path = temp_root / "uppercase-hash.toml"
        client_hash = expected_hashes["client"]
        uppercase_hash_path.write_text(
            accepted_text.replace(client_hash, client_hash.upper(), 1), encoding="utf-8"
        )
        expect_error(
            lambda: validate_manifest(uppercase_hash_path),
            "canonical lowercase 64-character SHA-256",
            "uppercase hash rejection",
        )

        missing_report_hash_path = temp_root / "missing-runtime-report-hash.toml"
        missing_report_hash_path.write_text(
            accepted_text.replace(
                f'stock_sp = "{report_hashes["stock_sp"]}"\n', "", 1
            ),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(missing_report_hash_path),
            "missing keys: stock_sp",
            "missing runtime report hash rejection",
        )

        unknown_report_hash_path = temp_root / "unknown-runtime-report-hash.toml"
        unknown_report_hash_path.write_text(
            accepted_text.replace(
                "[runtime_reports]\n",
                '[runtime_reports]\nunexpected = "' + ("a" * 64) + '"\n',
                1,
            ),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(unknown_report_hash_path),
            "unknown keys: unexpected",
            "unknown runtime report hash rejection",
        )

        uppercase_report_hash_path = temp_root / "uppercase-runtime-report-hash.toml"
        uppercase_report_hash_path.write_text(
            accepted_text.replace(
                report_hashes["stock_sp"], report_hashes["stock_sp"].upper(), 1
            ),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(uppercase_report_hash_path),
            "canonical lowercase 64-character SHA-256",
            "uppercase runtime report hash rejection",
        )

        legacy_schema_path = temp_root / "legacy-schema.toml"
        legacy_schema_path.write_text(
            accepted_text.replace("schema_version = 3", "schema_version = 2", 1),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(legacy_schema_path),
            "schema_version must be the integer 3",
            "legacy schema rejection",
        )

        malformed_path = temp_root / "malformed.toml"
        malformed_path.write_text("schema_version = [\n", encoding="utf-8")
        expect_error(
            lambda: validate_manifest(malformed_path),
            "malformed TOML",
            "malformed TOML rejection",
        )

        for field_name, mismatched_value in (
            ("openq4_commit", "3" * 40),
            ("openq4_game_commit", "4" * 40),
            ("release_version", "0.8.2"),
            ("version_tag", "0.8.2"),
            ("release_tag", "v0.8.2"),
            ("package_filename", "openq4-0.8.2-linux-arm64.tar.xz"),
        ):
            mismatched_expected = replace(EXPECTED, **{field_name: mismatched_value})
            expect_error(
                lambda candidate=mismatched_expected: validate_manifest(
                    accepted_path, candidate
                ),
                "does not match expected release value",
                f"candidate {field_name} mismatch rejection",
            )

        malformed_commit_path = temp_root / "malformed-commit.toml"
        malformed_commit_path.write_text(
            accepted_text.replace(EXPECTED.openq4_commit, "abc123", 1),
            encoding="utf-8",
        )
        expect_error(
            lambda: validate_manifest(malformed_commit_path),
            "canonical lowercase 40-character Git SHA",
            "malformed candidate commit rejection",
        )

        # A package-only mutation proves staged/package byte equality is enforced.
        client_relative = HELPER.ARM64_RUNTIME_PATHS["client"]
        packaged_client = package_dir.joinpath(*client_relative.parts)
        packaged_client.write_bytes(FIXTURE_BYTES["client"] + b"tampered")
        expect_error(
            lambda: HELPER.verify_release_evidence(
                accepted_path, runtime_root, package_dir, archive, EXPECTED, report_paths
            ),
            "does not exactly match the staged binary",
            "packaged binary tampering rejection",
        )
        expect_error(
            lambda: HELPER.write_candidate(
                temp_root / "invalid-candidate.toml",
                runtime_root,
                package_dir,
                archive,
                EXPECTED,
            ),
            "does not exactly match the staged binary",
            "mismatched candidate generation rejection",
        )
        packaged_client.write_bytes(FIXTURE_BYTES["client"])

        # Mutating both copies still fails their recorded accepted hash.
        dedicated_relative = HELPER.ARM64_RUNTIME_PATHS["dedicated"]
        staged_dedicated = runtime_root.joinpath(*dedicated_relative.parts)
        packaged_dedicated = package_dir.joinpath(*dedicated_relative.parts)
        staged_dedicated.write_bytes(FIXTURE_BYTES["dedicated"] + b"same mutation")
        packaged_dedicated.write_bytes(FIXTURE_BYTES["dedicated"] + b"same mutation")
        expect_error(
            lambda: HELPER.verify_release_evidence(
                accepted_path, runtime_root, package_dir, archive, EXPECTED, report_paths
            ),
            "dedicated SHA-256",
            "accepted binary hash tampering rejection",
        )
        staged_dedicated.write_bytes(FIXTURE_BYTES["dedicated"])
        packaged_dedicated.write_bytes(FIXTURE_BYTES["dedicated"])

        archive.write_bytes(archive_bytes + b"tampered")
        expect_error(
            lambda: HELPER.verify_release_evidence(
                accepted_path, runtime_root, package_dir, archive, EXPECTED, report_paths
            ),
            "archive SHA-256",
            "archive tampering rejection",
        )
        archive.write_bytes(archive_bytes)

        oversized_path = temp_root / "oversized.toml"
        oversized_path.write_bytes(b"#" * (HELPER.MAX_MANIFEST_BYTES + 1))
        expect_error(
            lambda: validate_manifest(oversized_path),
            "exceeds the maximum size",
            "oversized manifest rejection",
        )

        nonfile_path = temp_root / "manifest-directory"
        nonfile_path.mkdir()
        expect_error(
            lambda: validate_manifest(nonfile_path),
            "must be a regular file",
            "non-file manifest rejection",
        )

        symlink_path = temp_root / "evidence-symlink.toml"
        try:
            os.symlink(accepted_path, symlink_path)
        except OSError:
            # Windows test hosts may not grant symlink creation rights.
            pass
        else:
            expect_error(
                lambda: validate_manifest(symlink_path),
                "must not be a symlink",
                "symlink manifest rejection",
            )

    print("linux_arm64_release_evidence: ok")


if __name__ == "__main__":
    main()
