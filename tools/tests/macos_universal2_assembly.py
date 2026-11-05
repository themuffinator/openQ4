#!/usr/bin/env python3
"""Cross-platform contract tests for fail-closed macOS universal2 assembly."""

from __future__ import annotations

import importlib.util
import json
import shutil
import stat
from types import SimpleNamespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSEMBLER_PATH = ROOT / "tools" / "build" / "assemble_macos_universal2.py"
PACKAGER_PATH = ROOT / "tools" / "build" / "package_nightly.py"
VALIDATOR_PATH = ROOT / "tools" / "validation" / "openq4_validate.py"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ASSEMBLER = load_module(ASSEMBLER_PATH, "openq4_macos_universal2_assembler")
PACKAGER = load_module(PACKAGER_PATH, "openq4_macos_universal2_packager")
VALIDATOR = load_module(VALIDATOR_PATH, "openq4_macos_universal2_validator")


def write_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)


def populate_staging(root: Path, arch: str, *, shared_value: bytes = b"same\n") -> None:
    shared = {
        "openQ4.icns": shared_value,
        "collect_macos_support_info.sh": b"#!/bin/sh\n",
        "assets/splash/quake4_rt_bitmap_4001.bmp": b"bmp\n",
        "baseoq4/mod.json": b"{}\n",
        "baseoq4/pak0.pk4": b"pak0\n",
        "baseoq4/pak1.pk4": b"pak1\n",
        "baseoq4/pak2.pk4": b"pak2\n",
        "licenses/openal-soft/COPYING": b"lgpl\n",
        "licenses/openal-soft/LICENSE-pffft": b"pffft-license\n",
        "licenses/openal-soft/LICENSE-fmt": b"fmt-license\n",
        "licenses/openal-soft/LICENSE-gsl": b"gsl-license\n",
        "licenses/openal-soft/SOURCE.md": b"source\n",
        "licenses/openal-soft/openal-soft-1.25.2.tar.gz": b"source-archive\n",
        # libMoltenVK.dylib is third-party and already universal upstream, so both
        # thin trees stage the SAME bytes and it flows through as ordinary shared
        # payload rather than as a lipo-merged code key. The literal is deliberately
        # arch-independent: staging different MoltenVK builds per arch is exactly
        # what validate_matching_inputs() must reject.
        ASSEMBLER.MOLTENVK_DYLIB_NAME: b"moltenvk-universal\n",
    }
    for relative, data in shared.items():
        write_file(root / relative, data, 0o755 if relative.endswith((".sh", ".dylib")) else 0o644)
    for key, relative in ASSEMBLER.thin_code_paths(arch).items():
        write_file(root / relative, f"{key}-{arch}\n".encode(), 0o755)


def thin_manifest(arch: str, shared_records: dict[str, dict[str, object]]) -> dict[str, object]:
    binaries = {}
    for key, relative in ASSEMBLER.thin_code_paths(arch).items():
        binaries[key] = {
            "path": relative.as_posix(),
            "sha256": "0" * 64,
            "size": 1,
            "mode": 0o755,
            "machoArchitectures": sorted(ASSEMBLER.THIN_MACHO_ARCHES[arch]),
            "minimumOS": "11.0",
            "installName": ASSEMBLER.expected_install_name(key, arch),
            "dependencies": ["/usr/lib/libSystem.B.dylib"],
        }
    return {
        "format": 1,
        "architecture": arch,
        "machoArchitectures": sorted(ASSEMBLER.THIN_MACHO_ARCHES[arch]),
        "projectGitCommit": "a" * 40,
        "projectGitDirty": False,
        "gameLibsGitCommit": "b" * 40,
        "gameLibsGitDirty": False,
        "stagedSourceSha256": "c" * 64,
        "graphicsBridge": "metal",
        "openALProvider": "system",
        "deploymentTarget": "11.0",
        "buildType": "release",
        "sharedPayloadSha256": ASSEMBLER.canonical_json_sha256(list(shared_records.values())),
        "sharedFileCount": len(shared_records),
        "binaries": binaries,
    }


def expect_error(fragment: str, callback, label: str) -> None:
    try:
        callback()
    except ASSEMBLER.Universal2Error as exc:
        if fragment not in str(exc):
            raise AssertionError(f"{label}: unexpected error: {exc}") from exc
    else:
        raise AssertionError(f"{label}: expected Universal2Error")


def test_tree_classification_and_shared_matching() -> None:
    work = ROOT / ".tmp" / "macos-universal2-assembly-contract"
    arm_root = work / "arm64"
    x64_root = work / "x64"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        populate_staging(x64_root, "x64")
        arm_code, arm_shared = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        x64_code, x64_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        # POLICY UPDATE: the required code set grew from four to five when the
        # Vulkan renderer module (renderer-vk) became a macOS build product. The
        # check itself is still driven by CODE_KEYS, so only the message needed to
        # catch up.
        if set(arm_code) != set(ASSEMBLER.CODE_KEYS) or set(x64_code) != set(ASSEMBLER.CODE_KEYS):
            raise AssertionError("thin staging classifier did not find the six required binaries")
        if arm_shared != x64_shared:
            raise AssertionError("byte-identical shared payloads did not normalize equally")

        arm_manifest = thin_manifest("arm64", arm_shared)
        x64_manifest = thin_manifest("x64", x64_shared)
        ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, x64_shared)

        x64_manifest["graphicsBridge"] = "opengl"
        expect_error(
            "graphicsBridge",
            lambda: ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, x64_shared),
            "mismatched graphics bridges",
        )
        x64_manifest["graphicsBridge"] = "metal"

        write_file(x64_root / "openQ4.icns", b"different\n")
        _, changed_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        expect_error(
            "shared payloads are not identical",
            lambda: ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, changed_shared),
            "mismatched shared payloads",
        )

        write_file(arm_root / "openQ4-client_x64", b"stale\n", 0o755)
        expect_error(
            "stale or mismatched code file",
            lambda: ASSEMBLER.classify_staged_tree(arm_root, "arm64"),
            "stale thin binary",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def populate_layer(root: Path, arch: str) -> None:
    for key, relative in ASSEMBLER.optional_thin_code_paths(arch).items():
        write_file(root / relative, f"{key}-{arch}\n".encode(), 0o755)
    write_file(root / "q4xbase" / "mod.json", b'{"layer": "awakening"}\n')


def with_layer_binaries(manifest: dict[str, object], arch: str, code_records: dict[str, dict[str, object]]) -> dict[str, object]:
    binaries = manifest["binaries"]
    assert isinstance(binaries, dict)
    for key in ASSEMBLER.optional_thin_code_paths(arch):
        binaries[key] = {
            **binaries["game-sp"],
            "path": code_records[key]["path"],
            "installName": ASSEMBLER.expected_install_name(key, arch),
        }
    for key, record in code_records.items():
        binaries[key].update(record)
    return manifest


def test_game_layer_slices() -> None:
    """Current packages reject retired modules; historical fixtures stay readable."""
    if ASSEMBLER.optional_thin_code_paths("arm64") or ASSEMBLER.optional_universal_code_paths():
        raise AssertionError("current packages must use only the unified baseoq4 modules")
    work = ROOT / ".tmp" / "macos-universal2-retired-layer-contract"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(work, "arm64")
        write_file(work / "q4xbase/game-sp_arm64.dylib", b"retired\n", 0o755)
        expect_error("stale or mismatched code file",
                     lambda: ASSEMBLER.classify_staged_tree(work, "arm64"),
                     "retired expansion module in current staging")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    original_layers = ASSEMBLER.PACKAGED_LAYER_GAME_DIRS
    try:
        ASSEMBLER.PACKAGED_LAYER_GAME_DIRS = ("q4xbase",)
        test_historical_game_layer_slices()
    finally:
        ASSEMBLER.PACKAGED_LAYER_GAME_DIRS = original_layers


def test_historical_game_layer_slices() -> None:
    """Validate archived dual-module fixtures under their explicit historical policy."""
    if ASSEMBLER.layer_code_keys("q4xbase") != ("q4xbase/game-sp", "q4xbase/game-mp"):
        raise AssertionError("q4xbase must contribute exactly its SP and MP modules")
    if ASSEMBLER.expected_install_name("q4xbase/game-sp", "arm64") != "@loader_path/game-sp_arm64.dylib":
        raise AssertionError("a layer module keeps its own file name as its install name")
    if ASSEMBLER.optional_universal_code_paths()["q4xbase/game-mp"] != Path("q4xbase") / "game-mp_universal2.dylib":
        raise AssertionError("the merged layer module belongs in q4xbase/")

    work = ROOT / ".tmp" / "macos-universal2-layer-contract"
    arm_root = work / "arm64"
    x64_root = work / "x64"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        populate_staging(x64_root, "x64")
        populate_layer(arm_root, "arm64")
        populate_layer(x64_root, "x64")
        arm_code, arm_shared = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        x64_code, x64_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        expected_keys = {*ASSEMBLER.CODE_KEYS, "q4xbase/game-sp", "q4xbase/game-mp"}
        if set(arm_code) != expected_keys or set(x64_code) != expected_keys:
            raise AssertionError("a staged layer's modules must be recorded as code")
        if "q4xbase/mod.json" not in arm_shared or arm_shared != x64_shared:
            raise AssertionError("the layer's mod.json is shared payload, identical in both slices")

        arm_manifest = with_layer_binaries(thin_manifest("arm64", arm_shared), "arm64", arm_code)
        x64_manifest = with_layer_binaries(thin_manifest("x64", x64_shared), "x64", x64_code)
        ASSEMBLER.validate_thin_manifest_metadata(arm_manifest, "arm64")
        ASSEMBLER.validate_thin_manifest_metadata(x64_manifest, "x64")
        ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, x64_shared)
        if ASSEMBLER.staged_layer_keys(arm_manifest["binaries"]) != ("q4xbase/game-sp", "q4xbase/game-mp"):
            raise AssertionError("the recorded layer keys decide which modules are merged")

        # a layer in one slice only would leave the other architecture without it
        bare_x64 = thin_manifest("x64", x64_shared)
        expect_error(
            "must be staged in both thin slices or in neither",
            lambda: ASSEMBLER.validate_matching_inputs(arm_manifest, bare_x64, arm_shared, x64_shared),
            "layer staged in one slice",
        )

        # a manifest that records half a layer is corrupt
        half = json.loads(json.dumps(arm_manifest))
        del half["binaries"]["q4xbase/game-mp"]
        expect_error(
            "records an incomplete q4xbase/ layer",
            lambda: ASSEMBLER.validate_thin_manifest_metadata(half, "arm64"),
            "manifest with half a layer",
        )
        unknown = json.loads(json.dumps(arm_manifest))
        unknown["binaries"]["q4mp/game-sp"] = unknown["binaries"]["q4xbase/game-sp"]
        expect_error(
            "invalid binary set",
            lambda: ASSEMBLER.validate_thin_manifest_metadata(unknown, "arm64"),
            "manifest with an unknown layer",
        )

        # the staged tree must hold the whole layer, and only this slice's modules
        write_file(arm_root / "q4xbase" / "game-sp_x64.dylib", b"stale\n", 0o755)
        expect_error(
            "stale or mismatched code file",
            lambda: ASSEMBLER.classify_staged_tree(arm_root, "arm64"),
            "stale layer module",
        )
        (arm_root / "q4xbase" / "game-sp_x64.dylib").unlink()
        (x64_root / "q4xbase" / "game-mp_x64.dylib").unlink()
        expect_error(
            "incomplete q4xbase/ layer",
            lambda: ASSEMBLER.classify_staged_tree(x64_root, "x64"),
            "layer without its MP module",
        )
        (arm_root / "q4xbase" / "mod.json").unlink()
        expect_error(
            "incomplete q4xbase/ layer",
            lambda: ASSEMBLER.classify_staged_tree(arm_root, "arm64"),
            "layer without its mod.json",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_host_signed_moltenvk_matching() -> None:
    work = ROOT / ".tmp" / "macos-universal2-moltenvk-signature-contract"
    arm_root = work / "arm64"
    x64_root = work / "x64"
    original_require_tool = ASSEMBLER.require_tool
    original_run_command = ASSEMBLER.run_command
    original_validate_arches = ASSEMBLER.validate_exact_arches
    observed: list[tuple[str, ...]] = []
    arch_checks: list[tuple[Path, frozenset[str]]] = []
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        populate_staging(x64_root, "x64")
        unsigned_payload = b"identical-unsigned-moltenvk\n"
        signature_marker = b"HOST-SIGNATURE:"
        write_file(
            arm_root / ASSEMBLER.MOLTENVK_DYLIB_NAME,
            unsigned_payload + signature_marker + b"arm\n",
            0o755,
        )
        write_file(
            x64_root / ASSEMBLER.MOLTENVK_DYLIB_NAME,
            unsigned_payload + signature_marker + b"x64\n",
            0o755,
        )
        _, arm_shared = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        _, x64_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        if arm_shared == x64_shared:
            raise AssertionError("host-specific MoltenVK signatures unexpectedly produced equal raw records")

        ASSEMBLER.require_tool = lambda name: name
        ASSEMBLER.validate_exact_arches = lambda path, expected: arch_checks.append((Path(path), expected))

        def fake_run(command: list[str], label: str):
            observed.append(tuple(command))
            if "--remove-signature" in command:
                temporary_copy = Path(command[-1])
                signed_bytes = temporary_copy.read_bytes()
                unsigned_bytes, marker, _signature = signed_bytes.rpartition(signature_marker)
                if not marker:
                    raise AssertionError("fake MoltenVK input had no host-signature marker")
                temporary_copy.write_bytes(unsigned_bytes)
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        ASSEMBLER.run_command = fake_run
        normalized_arm = ASSEMBLER.signature_normalized_shared_records(arm_root, arm_shared)
        normalized_x64 = ASSEMBLER.signature_normalized_shared_records(x64_root, x64_shared)
        if normalized_arm != normalized_x64:
            raise AssertionError("signature-stripped MoltenVK shared records did not match")
        ASSEMBLER.validate_matching_inputs(
            thin_manifest("arm64", arm_shared),
            thin_manifest("x64", x64_shared),
            normalized_arm,
            normalized_x64,
        )
        expected_sources = (
            arm_root / ASSEMBLER.MOLTENVK_DYLIB_NAME,
            x64_root / ASSEMBLER.MOLTENVK_DYLIB_NAME,
        )
        if arch_checks != [
            (source, ASSEMBLER.UNIVERSAL_MACHO_ARCHES) for source in expected_sources
        ]:
            raise AssertionError(f"MoltenVK exact-slice validation was not applied: {arch_checks!r}")
        expected_shapes = (
            ("codesign", "--verify", "--strict", "--all-architectures"),
            ("codesign", "--remove-signature"),
            ("codesign", "--verify", "--strict", "--all-architectures"),
            ("codesign", "--remove-signature"),
        )
        observed_shapes = tuple(
            command[:-1] for command in observed
        )
        if observed_shapes != expected_shapes:
            raise AssertionError(f"unexpected MoltenVK signature-normalization command order: {observed!r}")
        if observed[0][-1] != str(expected_sources[0]) or observed[2][-1] != str(expected_sources[1]):
            raise AssertionError("MoltenVK signatures were not verified against both recorded inputs")
        if observed[1][-1] in {str(source) for source in expected_sources} or observed[3][-1] in {
            str(source) for source in expected_sources
        }:
            raise AssertionError("MoltenVK signature normalization mutated a recorded thin input")

        write_file(
            expected_sources[1],
            b"different-unsigned-moltenvk\n" + signature_marker + b"x64\n",
            0o755,
        )
        _, changed_x64_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        changed_x64_normalized = ASSEMBLER.signature_normalized_shared_records(x64_root, changed_x64_shared)
        expect_error(
            "different content/mode: ['libMoltenVK.dylib']",
            lambda: ASSEMBLER.validate_matching_inputs(
                thin_manifest("arm64", arm_shared),
                thin_manifest("x64", changed_x64_shared),
                normalized_arm,
                changed_x64_normalized,
            ),
            "different unsigned MoltenVK payloads",
        )
    finally:
        ASSEMBLER.require_tool = original_require_tool
        ASSEMBLER.run_command = original_run_command
        ASSEMBLER.validate_exact_arches = original_validate_arches
        shutil.rmtree(work, ignore_errors=True)


def test_thin_manifest_metadata_reinspection_contract() -> None:
    work = ROOT / ".tmp" / "macos-universal2-metadata-contract"
    arm_root = work / "arm64"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        code_records, shared_records = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        manifest = thin_manifest("arm64", shared_records)
        for key, record in code_records.items():
            manifest["binaries"][key].update(record)

        ASSEMBLER.validate_thin_manifest_metadata(manifest, "arm64")
        manifest["binaries"]["client"]["dependencies"] = ["/usr/lib/libSystem.B.dylib", "/System/Library/Frameworks/Cocoa.framework/Cocoa"]
        expect_error(
            "invalid client dependencies",
            lambda: ASSEMBLER.validate_thin_manifest_metadata(manifest, "arm64"),
            "unsorted thin dependency manifest",
        )
        manifest["binaries"]["client"]["dependencies"] = ["/usr/lib/libSystem.B.dylib"]

        original_collect = ASSEMBLER.collect_binary_metadata
        original_validate_arches = ASSEMBLER.validate_exact_arches
        try:
            inspected = {key: dict(record) for key, record in manifest["binaries"].items()}
            ASSEMBLER.collect_binary_metadata = lambda root, arch, deployment_target: inspected
            ASSEMBLER.validate_exact_arches = lambda path, expected: None
            ASSEMBLER.validate_recorded_tree(arm_root, "arm64", manifest)

            inspected["game-sp"]["minimumOS"] = "12.0"
            expect_error(
                "Mach-O metadata changed after recording",
                lambda: ASSEMBLER.validate_recorded_tree(arm_root, "arm64", manifest),
                "redetected thin Mach-O metadata mismatch",
            )
        finally:
            ASSEMBLER.collect_binary_metadata = original_collect
            ASSEMBLER.validate_exact_arches = original_validate_arches
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_source_provenance_validation() -> None:
    work = ROOT / ".tmp" / "macos-universal2-source-manifest-contract"
    manifest_path = work / "manifest.json"
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True)
        manifest = {
            "format": 1,
            "projectGitCommit": "a" * 40,
            "projectGitDirty": False,
            "gameLibsGitCommit": "b" * 40,
            "gameLibsGitDirty": False,
            "fileCount": 2,
            "files": [
                {"path": "src/game/A.cpp", "sha256": "1" * 64},
                {"path": "src/mpgame/B.cpp", "sha256": "2" * 64},
            ],
        }
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        loaded = ASSEMBLER.load_source_manifest(manifest_path)
        if loaded["projectGitCommit"] != "a" * 40 or loaded["stagedSourceSha256"] == "":
            raise AssertionError("valid staged-source provenance was not loaded")

        manifest["projectGitDirty"] = True
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        expect_error(
            "clean source trees",
            lambda: ASSEMBLER.load_source_manifest(manifest_path),
            "dirty source provenance",
        )

        manifest["projectGitDirty"] = False
        manifest["files"][1]["path"] = "../escape"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        expect_error(
            "unsafe entry",
            lambda: ASSEMBLER.load_source_manifest(manifest_path),
            "unsafe staged-source path",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_exact_slice_contracts() -> None:
    expected = frozenset(("arm64", "x86_64"))
    if PACKAGER.macos_expected_lipo_arches("universal2") != expected:
        raise AssertionError("packager universal2 mapping is not the exact dual-slice set")
    if VALIDATOR.macos_expected_lipo_arches("universal2") != expected:
        raise AssertionError("staged validator universal2 mapping is not the exact dual-slice set")
    if PACKAGER.macos_expected_lipo_arch("arm64") != "arm64":
        raise AssertionError("thin compatibility mapping changed")
    try:
        PACKAGER.macos_expected_lipo_arch("universal2")
    except RuntimeError:
        pass
    else:
        raise AssertionError("singleton compatibility mapping accepted universal2")


def test_merge_binary_normalizes_and_signs_fused_output() -> None:
    if set(ASSEMBLER.universal_code_paths()) != set(ASSEMBLER.CODE_KEYS):
        raise AssertionError("universal2 output map does not cover every required code binary")

    work = ROOT / ".tmp" / "macos-universal2-merge-order-contract"
    original_require_tool = ASSEMBLER.require_tool
    original_run_command = ASSEMBLER.run_command
    original_validate_arches = ASSEMBLER.validate_exact_arches
    original_install_name = ASSEMBLER.install_name
    original_require_export = ASSEMBLER.require_module_entry_export
    observed: list[tuple[str, ...]] = []
    arch_checks: list[tuple[Path, frozenset[str]]] = []
    install_name_checks: list[tuple[Path, str]] = []
    export_checks: list[tuple[Path, str, str]] = []
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True)
        arm_path = work / "game-mp-arm64"
        x64_path = work / "game-mp-x64"
        module_output = work / "game-mp-universal2.dylib"
        dedicated_output = work / "openQ4-ded-universal2"
        write_file(arm_path, b"immutable-arm\n", 0o755)
        write_file(x64_path, b"immutable-x64\n", 0o755)
        source_bytes = (arm_path.read_bytes(), x64_path.read_bytes())

        ASSEMBLER.require_tool = lambda name: name

        def fake_run(command: list[str], label: str):
            observed.append(tuple(command))
            if command[0] == "lipo":
                output = Path(command[command.index("-output") + 1])
                output.write_bytes(b"fused\n")
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        ASSEMBLER.run_command = fake_run
        ASSEMBLER.validate_exact_arches = lambda path, expected: arch_checks.append((Path(path), expected))

        def fake_install_name(path: Path, *, macho_arch: str) -> str:
            install_name_checks.append((Path(path), macho_arch))
            return ASSEMBLER.expected_install_name("game-mp", ASSEMBLER.UNIVERSAL_ARCH)

        ASSEMBLER.install_name = fake_install_name
        ASSEMBLER.require_module_entry_export = (
            lambda path, *, macho_arch, code_key: export_checks.append((Path(path), macho_arch, code_key))
        )

        ASSEMBLER.merge_binary(arm_path, x64_path, module_output, "game-mp")
        universal_id = ASSEMBLER.expected_install_name("game-mp", ASSEMBLER.UNIVERSAL_ARCH)
        expected_module_commands = [
            ("lipo", "-create", str(arm_path), str(x64_path), "-output", str(module_output)),
            ("install_name_tool", "-id", universal_id, str(module_output)),
            ("codesign", "--force", "--sign", "-", str(module_output)),
            ("codesign", "--verify", "--strict", "--all-architectures", str(module_output)),
        ]
        if observed != expected_module_commands:
            raise AssertionError(f"unexpected universal2 module merge command order: {observed!r}")
        if any("--remove-signature" in command for command in observed):
            raise AssertionError("universal2 merge removed a thin or fused signature before Mach-O rewriting")
        if any(command[-1] in {str(arm_path), str(x64_path)} for command in observed[1:]):
            raise AssertionError("universal2 module normalization or signing mutated a thin input")
        if (arm_path.read_bytes(), x64_path.read_bytes()) != source_bytes:
            raise AssertionError("universal2 module merge changed a recorded thin input")
        expected_slices = sorted(ASSEMBLER.UNIVERSAL_MACHO_ARCHES)
        if install_name_checks != [(module_output, macho_arch) for macho_arch in expected_slices]:
            raise AssertionError(f"universal2 module IDs were not checked per slice: {install_name_checks!r}")
        if export_checks != [(module_output, macho_arch, "game-mp") for macho_arch in expected_slices]:
            raise AssertionError(f"universal2 module exports were not checked per slice: {export_checks!r}")
        if arch_checks != [(module_output, ASSEMBLER.UNIVERSAL_MACHO_ARCHES)]:
            raise AssertionError(f"universal2 module exact slices were not checked: {arch_checks!r}")

        observed.clear()
        arch_checks.clear()
        install_name_checks.clear()
        export_checks.clear()
        ASSEMBLER.merge_binary(arm_path, x64_path, dedicated_output, "dedicated")
        expected_dedicated_commands = [
            ("lipo", "-create", str(arm_path), str(x64_path), "-output", str(dedicated_output)),
            ("codesign", "--force", "--sign", "-", str(dedicated_output)),
            ("codesign", "--verify", "--strict", "--all-architectures", str(dedicated_output)),
        ]
        if observed != expected_dedicated_commands:
            raise AssertionError(f"unexpected universal2 executable merge command order: {observed!r}")
        if install_name_checks or export_checks:
            raise AssertionError("universal2 executable merge applied loadable-module metadata checks")
        if arch_checks != [(dedicated_output, ASSEMBLER.UNIVERSAL_MACHO_ARCHES)]:
            raise AssertionError(f"universal2 executable exact slices were not checked: {arch_checks!r}")
        if (arm_path.read_bytes(), x64_path.read_bytes()) != source_bytes:
            raise AssertionError("universal2 executable merge changed a recorded thin input")
    finally:
        ASSEMBLER.require_tool = original_require_tool
        ASSEMBLER.run_command = original_run_command
        ASSEMBLER.validate_exact_arches = original_validate_arches
        ASSEMBLER.install_name = original_install_name
        ASSEMBLER.require_module_entry_export = original_require_export
        shutil.rmtree(work, ignore_errors=True)


def test_thin_payload_preparation_contract() -> None:
    expected_paths = ASSEMBLER.thin_code_paths("arm64")
    original_require_directory = ASSEMBLER.require_directory
    original_require_regular_file = ASSEMBLER.require_regular_file
    original_require_tool = ASSEMBLER.require_tool
    original_validate_arches = ASSEMBLER.validate_exact_arches
    original_install_name = ASSEMBLER.install_name
    original_run_command = ASSEMBLER.run_command
    observed: list[tuple[str, ...]] = []
    install_names = {
        expected_paths["game-sp"]: "/absolute/game-sp_arm64.dylib",
        expected_paths["game-mp"]: ASSEMBLER.expected_install_name("game-mp", "arm64"),
        expected_paths["renderer-vk"]: "/absolute/renderer-vk_arm64.dylib",
        expected_paths["openal-soft"]: "/absolute/libopenal.1.dylib",
    }
    try:
        ASSEMBLER.require_directory = lambda path, label: Path(path)
        ASSEMBLER.require_regular_file = lambda path, label, executable=False: Path(path)
        ASSEMBLER.require_tool = lambda name: name
        ASSEMBLER.validate_exact_arches = lambda path, expected: None

        def fake_install_name(path: Path, *, macho_arch: str) -> str:
            relative = Path(path).relative_to("/thin")
            return install_names[relative]

        def fake_run(command: list[str], label: str):
            observed.append(tuple(command))
            if command[0] == "install_name_tool":
                relative = Path(command[-1]).relative_to("/thin")
                install_names[relative] = command[2]
            return None

        ASSEMBLER.install_name = fake_install_name
        ASSEMBLER.run_command = fake_run
        ASSEMBLER.prepare_thin_payload(Path("/thin"), "arm64")

        sp_path = str(Path("/thin") / expected_paths["game-sp"])
        mp_path = str(Path("/thin") / expected_paths["game-mp"])
        renderer_path = str(Path("/thin") / expected_paths["renderer-vk"])
        openal_path = str(Path("/thin") / expected_paths["openal-soft"])
        expected_commands = [
            (
                "install_name_tool",
                "-id",
                ASSEMBLER.expected_install_name("game-sp", "arm64"),
                sp_path,
            ),
            ("codesign", "--force", "--sign", "-", sp_path),
            ("codesign", "--verify", "--strict", sp_path),
            ("codesign", "--force", "--sign", "-", mp_path),
            ("codesign", "--verify", "--strict", mp_path),
            (
                "install_name_tool",
                "-id",
                ASSEMBLER.expected_install_name("renderer-vk", "arm64"),
                renderer_path,
            ),
            ("codesign", "--force", "--sign", "-", renderer_path),
            ("codesign", "--verify", "--strict", renderer_path),
            (
                "install_name_tool",
                "-id",
                ASSEMBLER.expected_install_name("openal-soft", "arm64"),
                openal_path,
            ),
            ("codesign", "--force", "--sign", "-", openal_path),
            ("codesign", "--verify", "--strict", openal_path),
        ]
        if observed != expected_commands:
            raise AssertionError(f"unexpected thin preparation commands: {observed!r}")
    finally:
        ASSEMBLER.require_directory = original_require_directory
        ASSEMBLER.require_regular_file = original_require_regular_file
        ASSEMBLER.require_tool = original_require_tool
        ASSEMBLER.validate_exact_arches = original_validate_arches
        ASSEMBLER.install_name = original_install_name
        ASSEMBLER.run_command = original_run_command


def make_universal_symbol_manifest() -> bytes:
    version = "0.1.10"
    version_tag = "universal2-ci"
    arch = "universal2"
    suffix = "-opengl"
    runtime_archive = "openq4-universal2-ci-macos-universal2-opengl.tar.xz"
    symbol_archive = "openq4-universal2-ci-macos-universal2-opengl-symbols.tar.xz"
    records = (
        ("openQ4.app/Contents/MacOS/openQ4", "openQ4.app.dSYM"),
        ("openQ4-client_universal2", "openQ4-client_universal2.dSYM"),
        ("openQ4-ded_universal2", "openQ4-ded_universal2.dSYM"),
        ("openQ4.app/Contents/Frameworks/game-sp_universal2.dylib", "game-sp_universal2.dylib.dSYM"),
        ("openQ4.app/Contents/Frameworks/game-mp_universal2.dylib", "game-mp_universal2.dylib.dSYM"),
        # The fused renderer module gets a dSYM like any other openQ4-built code.
        # libMoltenVK.dylib rides in the same Frameworks directory but has no
        # record here: it is third-party, ships no DWARF, and the packager's
        # expected-binary set excludes it.
        ("openQ4.app/Contents/Frameworks/renderer-vk_universal2.dylib", "renderer-vk_universal2.dylib.dSYM"),
    )
    lines = [
        "openQ4 macOS symbols",
        "format=1",
        f"version={version}",
        f"version_tag={version_tag}",
        "platform=macos",
        f"arch={arch}",
        f"package_suffix={suffix}",
        f"runtime_archive={runtime_archive}",
        f"symbol_archive={symbol_archive}",
        "",
        "binaries:",
    ]
    for index, (path, dsym) in enumerate(records, start=1):
        lines.extend(
            (
                f"- path={path}",
                f"  sha256={index:064x}",
                "  size=1",
                "  macho_uuid="
                f"UUID: 00000000-0000-0000-0000-{index:012x} (arm64) {path}; "
                f"UUID: 10000000-0000-0000-0000-{index:012x} (x86_64) {path}",
                f"  dsym=dSYMs/{dsym}",
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def test_universal_symbol_manifest_contract() -> None:
    data = make_universal_symbol_manifest()
    kwargs = {
        "version": "0.1.10",
        "version_tag": "universal2-ci",
        "arch": "universal2",
        "package_suffix": "-opengl",
        "runtime_archive_name": "openq4-universal2-ci-macos-universal2-opengl.tar.xz",
        "symbol_archive_name": "openq4-universal2-ci-macos-universal2-opengl-symbols.tar.xz",
    }
    PACKAGER.validate_macos_symbol_manifest_bytes(data, "universal2 symbol manifest", **kwargs)
    bad_data = data.replace(b"(x86_64)", b"(arm64)")
    try:
        PACKAGER.validate_macos_symbol_manifest_bytes(
            bad_data,
            "bad universal2 symbol manifest",
            **kwargs,
        )
    except RuntimeError as exc:
        if "invalid macho_uuid" not in str(exc):
            raise AssertionError(f"unexpected universal2 UUID rejection: {exc}") from exc
    else:
        raise AssertionError("universal2 symbol manifest accepted duplicate arm64 UUID slices")


def test_universal_dsym_uuid_pair_contract() -> None:
    original_platform = PACKAGER.sys.platform
    original_which = PACKAGER.shutil.which
    original_command = PACKAGER.run_macos_command
    binary = Path("/tmp/openQ4-client_universal2")
    dsym = Path("/tmp/openQ4-client_universal2.dSYM")
    binary_output = (
        "UUID: 00000000-0000-0000-0000-000000000001 (arm64) /tmp/openQ4-client_universal2\n"
        "UUID: 00000000-0000-0000-0000-000000000002 (x86_64) /tmp/openQ4-client_universal2\n"
    )
    try:
        PACKAGER.sys.platform = "darwin"
        PACKAGER.shutil.which = lambda name: name
        PACKAGER.run_macos_command = lambda command, *, label: SimpleNamespace(stdout=binary_output)
        PACKAGER.validate_macos_dsym_matches_binary(binary, dsym)

        mismatched_output = binary_output.replace("000000000002", "000000000003")
        PACKAGER.run_macos_command = (
            lambda command, *, label: SimpleNamespace(
                stdout=mismatched_output if str(command[-1]).endswith(".dSYM") else binary_output
            )
        )
        try:
            PACKAGER.validate_macos_dsym_matches_binary(binary, dsym)
        except RuntimeError as exc:
            if "UUID slices do not match" not in str(exc):
                raise AssertionError(f"unexpected dSYM UUID mismatch error: {exc}") from exc
        else:
            raise AssertionError("universal2 dSYM UUID validation accepted a mismatched x86_64 slice")
    finally:
        PACKAGER.sys.platform = original_platform
        PACKAGER.shutil.which = original_which
        PACKAGER.run_macos_command = original_command


def test_static_fail_closed_contract() -> None:
    source = ASSEMBLER_PATH.read_text(encoding="utf-8")
    required = (
        "THIN_MANIFEST_NAME",
        "projectGitCommit",
        "gameLibsGitCommit",
        "stagedSourceSha256",
        "graphicsBridge",
        "openALProvider",
        "deploymentTarget",
        "buildType",
        "sharedPayloadSha256",
        "signatureNormalizedSharedPayloadSha256",
        "validate_exact_arches",
        "minimum_os_version(path, macho_arch=macho_arch)",
        "dependencies(path, macho_arch=macho_arch",
        'require_tool("lipo")',
        'require_tool("otool")',
        'require_tool("install_name_tool")',
        'require_tool("codesign")',
        '"--all-architectures"',
        'require_tool("nm")',
        '"-create"',
        '"--remove-signature"',
        "GetGameAPI",
        "thin shared payloads are not identical",
        "thin macOS provenance/build settings differ",
        "universal2 output root must not overlap",
        "universal2 output root must not be a symlink",
        "assembly manifest must not be a symlink",
        "assembly manifest must be outside",
        "os.replace(temporary_root, output_root)",
    )
    for token in required:
        if token not in source:
            raise AssertionError(f"universal2 assembler is missing fail-closed token: {token}")
    if "shutil.copytree" in source:
        raise AssertionError("universal2 assembler must not blindly copy an input staging tree")

    packager_source = PACKAGER_PATH.read_text(encoding="utf-8")
    for token in (
        "macos_macho_uuid_pairs",
        "validate_macos_dsym_matches_binary",
        "macOS dSYM UUID slices do not match the distributed binary",
        "validate_macos_dsym_matches_binary(binary_path, dsym_path)",
        "macos_otool_minimum_os_version(binary_path, macho_arch=macho_arch)",
        "macos_otool_dependencies(binary_path, macho_arch=macho_arch)",
    ):
        if token not in packager_source:
            raise AssertionError(f"universal2 package-symbol contract is missing: {token}")


def main() -> int:
    test_tree_classification_and_shared_matching()
    test_game_layer_slices()
    test_host_signed_moltenvk_matching()
    test_thin_manifest_metadata_reinspection_contract()
    test_source_provenance_validation()
    test_exact_slice_contracts()
    test_merge_binary_normalizes_and_signs_fused_output()
    test_thin_payload_preparation_contract()
    test_universal_symbol_manifest_contract()
    test_universal_dsym_uuid_pair_contract()
    test_static_fail_closed_contract()
    print("macOS universal2 assembly contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
