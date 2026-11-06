#!/usr/bin/env python3
"""Deterministic contract tests for per-map renderer CPU/GPU budgets."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
VALIDATION_DIR = ROOT / "tools" / "validation"
if str(VALIDATION_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATION_DIR))

import renderer_budget_contract as budget
import generate_pbr_fixture as pbr_fixture


def load_benchmark() -> ModuleType:
    path = ROOT / "tools" / "tests" / "renderer_gameplay_benchmark.py"
    spec = importlib.util.spec_from_file_location("openq4_renderer_gameplay_benchmark", path)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load renderer_gameplay_benchmark.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def timing_marker(
    *,
    map_name: str = "game/storage1",
    backend: str = "opengl",
    profile: str = "baseline",
    cpu_samples: int = 256,
    cpu: tuple[int, int, int] = (7000, 11000, 14000),
    gpu_available: bool = True,
    gpu_samples: int = 252,
    gpu: tuple[int, int, int] = (5000, 8000, 10000),
) -> str:
    return (
        f"{budget.MARKER_PREFIX} map={map_name} backend={backend} profile={profile} "
        f"cpuSamples={cpu_samples} cpuP50Us={cpu[0]} cpuP95Us={cpu[1]} cpuP99Us={cpu[2]} "
        f"gpuAvailable={int(gpu_available)} gpuSamples={gpu_samples} "
        f"gpuP50Us={gpu[0]} gpuP95Us={gpu[1]} gpuP99Us={gpu[2]}"
    )


def write_tga(path: Path, width: int = 1280, height: int = 720) -> None:
    header = bytearray(18)
    header[2] = 2
    struct.pack_into("<H", header, 12, width)
    struct.pack_into("<H", header, 14, height)
    header[16] = 24
    pixels = bytes((index * 17) % 256 for index in range(width * height * 3))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(header) + pixels)


def test_contract_and_marker() -> None:
    contract, binding = budget.load_contract(budget.DEFAULT_CONTRACT_PATH)
    assert contract["contractId"] == "stock-baseline-targets-v1"
    assert len(contract["budgets"]) == 16
    assert binding["path"] == "tools/validation/renderer_per_map_budgets.json"
    assert not Path(binding["path"]).is_absolute()

    marker = timing_marker()
    evidence, failures = budget.evaluate_timing_evidence(
        (marker,), contract, "game/storage1", "opengl", "baseline"
    )
    assert failures == [] and evidence["status"] == "pass"
    assert evidence["budgetId"] == "storage1-opengl-baseline"
    assert evidence["measurement"]["cpu"]["p95Us"] == 11000
    assert evidence["measurement"]["gpu"]["p99Us"] == 10000

    # A single console line mirrored once into stdout is accepted, but a
    # command that emits the same marker twice in one authoritative stream is
    # rejected as an ambiguous capture window.
    _, mirrored_failures = budget.parse_timing_evidence((marker, marker))
    assert mirrored_failures == []
    _, duplicate_failures = budget.parse_timing_evidence((marker + "\n" + marker,))
    assert any("contains 2" in failure for failure in duplicate_failures)
    _, conflicting_failures = budget.parse_timing_evidence(
        (marker, timing_marker(cpu=(7000, 12000, 14000)))
    )
    assert any("conflicting" in failure for failure in conflicting_failures)

    for changed, fragment in (
        ({"expected_map": "game/storage2"}, "does not match expected"),
        ({"expected_backend": "vulkan"}, "timing backend"),
        ({"expected_profile": "modern"}, "timing profile"),
    ):
        _, identity_failures = budget.evaluate_timing_evidence(
            (marker,),
            contract,
            changed.get("expected_map", "game/storage1"),
            changed.get("expected_backend", "opengl"),
            changed.get("expected_profile", "baseline"),
        )
        assert any(fragment in failure for failure in identity_failures)

    unavailable = timing_marker(
        gpu_available=False, gpu_samples=0, gpu=(-1, -1, -1)
    )
    _, unavailable_failures = budget.evaluate_timing_evidence(
        (unavailable,), contract, "game/storage1", "opengl", "baseline"
    )
    assert "required GPU frame timing is unavailable" in unavailable_failures
    _, malformed_unavailable = budget.parse_timing_evidence(
        (timing_marker(gpu_available=False, gpu_samples=1, gpu=(-1, -1, -1)),)
    )
    assert any("zero samples" in failure for failure in malformed_unavailable)

    _, sample_failures = budget.evaluate_timing_evidence(
        (timing_marker(cpu_samples=119, gpu_samples=63),),
        contract,
        "game/storage1",
        "opengl",
        "baseline",
    )
    assert any("CPU samples" in failure for failure in sample_failures)
    assert any("GPU samples" in failure for failure in sample_failures)
    _, threshold_failures = budget.evaluate_timing_evidence(
        (timing_marker(cpu=(7000, 20001, 28001), gpu=(5000, 20001, 28001)),),
        contract,
        "game/storage1",
        "opengl",
        "baseline",
    )
    assert sum("exceeds" in failure for failure in threshold_failures) == 4


def test_schema_rejection(base: Path) -> None:
    payload = json.loads(budget.DEFAULT_CONTRACT_PATH.read_text(encoding="utf-8"))
    payload["budgets"].append(dict(payload["budgets"][0]))
    duplicate = base / "duplicate.json"
    duplicate.write_text(json.dumps(payload), encoding="utf-8")
    try:
        budget.load_contract(duplicate)
    except ValueError as exc:
        assert "duplicate budget id" in str(exc)
    else:
        raise AssertionError("duplicate contract identity must fail")

    payload = json.loads(budget.DEFAULT_CONTRACT_PATH.read_text(encoding="utf-8"))
    payload["budgets"][0]["gpu"]["required"] = False
    optional_gpu = base / "optional-gpu.json"
    optional_gpu.write_text(json.dumps(payload), encoding="utf-8")
    try:
        budget.load_contract(optional_gpu)
    except ValueError as exc:
        assert "gpu.required must be true" in str(exc)
    else:
        raise AssertionError("promotion budget must fail closed on unavailable GPU timing")


def test_benchmark_replay(base: Path) -> None:
    harness = load_benchmark()
    relative_base = os.path.relpath(base, ROOT)
    assert harness.resolve_basepath(relative_base) == str(base.resolve())
    assert harness.resolve_basepath(str(base / "missing")) == ""
    contract, binding = budget.load_contract(budget.DEFAULT_CONTRACT_PATH)
    runtime_dir = base / "runtime"
    runtime_dir.mkdir()
    try:
        harness.validate_runtime_dir(runtime_dir, ROOT)
    except ValueError as exc:
        assert ".tmp\\stock-runtime" in str(exc) or ".tmp/stock-runtime" in str(exc)
    else:
        raise AssertionError("alternate benchmark runtime outside .tmp/stock-runtime must fail")
    suffix = ".exe" if os.name == "nt" else ""
    executable = runtime_dir / f"openQ4-client_{harness.host_arch()}{suffix}"
    executable.write_bytes(b"deterministic-client")
    fixture_paths = pbr_fixture.generate_fixture(
        runtime_dir, size=pbr_fixture.DEFAULT_TEXTURE_SIZE
    )
    launch_args = harness.parse_args(
        [
            "--cases",
            "mp-q4dm1-listen",
            "--render-api",
            "vk",
            "--set-cvar",
            "r_mode=5",
            "--dry-run",
        ]
    )
    launch_args.runtime_dir_path = runtime_dir
    launch_args.budget_contract = contract
    mp_spec = harness.build_specs(launch_args)[0]
    dry_mp = harness.run_mp_spec(
        ROOT,
        executable,
        base / "mp-plan",
        "",
        "test-run",
        mp_spec,
        0,
        launch_args,
    )

    def cvar_values(arguments: list[str], name: str) -> list[str]:
        return [
            arguments[index + 2]
            for index in range(len(arguments) - 2)
            if arguments[index] in ("+set", "+seta") and arguments[index + 1] == name
        ]

    assert cvar_values(dry_mp["serverArgs"], "ui_autoJoin") == ["1"]
    assert cvar_values(dry_mp["clientArgs"], "ui_autoJoin") == ["1"]
    assert cvar_values(dry_mp["serverArgs"], "si_pure") == ["1"]
    assert cvar_values(dry_mp["serverArgs"], "net_serverAllowServerMod") == ["0"]
    assert cvar_values(dry_mp["serverArgs"], "r_renderApi") == ["vulkan"]
    assert cvar_values(dry_mp["clientArgs"], "r_renderApi") == ["vulkan"]
    assert cvar_values(dry_mp["serverArgs"], "logFileName") == [
        f"logs/{harness.ROLE_LOG_NAME}"
    ]
    assert cvar_values(dry_mp["clientArgs"], "logFileName") == [
        f"logs/{harness.ROLE_LOG_NAME}"
    ]
    server_cfg = (
        base
        / "mp-plan"
        / "savepaths"
        / f"{mp_spec.id}_server"
        / "baseoq4"
        / dry_mp["serverAutoexecCfg"]
    ).read_text(encoding="utf-8")
    client_cfg = (
        base
        / "mp-plan"
        / "savepaths"
        / f"{mp_spec.id}_client"
        / "baseoq4"
        / dry_mp["clientAutoexecCfg"]
    ).read_text(encoding="utf-8")
    assert f"waitMsec {harness.MP_SERVER_CLIENT_GRACE_MSEC}" in server_cfg
    assert f"waitMsec {harness.MP_SERVER_CLIENT_GRACE_MSEC}" not in client_cfg
    for payload in (server_cfg, client_cfg):
        lines = payload.splitlines()
        reset_index = lines.index("framePacingReset")
        for name, value in harness.budget_display_contract()["cvars"].items():
            matches = [
                line
                for line in lines[:reset_index]
                if line.startswith(f"{name} ") or line.startswith(f"set {name} ")
            ]
            assert matches
            assert matches[-1] == f"{name} {value}"
        assert [
            line
            for line in lines[:reset_index]
            if line.startswith("r_mode ") or line.startswith("set r_mode ")
        ] == [
            "set r_mode 5",
            "r_mode -1",
        ]
    for arguments in (dry_mp["serverArgs"], dry_mp["clientArgs"]):
        for name, value in {
            "r_fullscreen": "0",
            "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1",
            "r_fullscreenDesktop": "0",
            "r_windowWidth": "1280",
            "r_windowHeight": "720",
            "r_mode": "-1",
            "r_customWidth": "1280",
            "r_customHeight": "720",
        }.items():
            assert cvar_values(arguments, name) == [value]
    assert dry_mp["displayContract"] == harness.budget_display_contract()
    assert cvar_values(dry_mp["serverArgs"], "fs_devpath") == [
        str(base / "mp-plan" / "savepaths" / f"{mp_spec.id}_server")
    ]
    savepath = base / "evidence" / "savepaths" / "case"
    log = savepath / "baseoq4" / "logs" / "case.log"
    stdout = base / "evidence" / "case.out.txt"
    stderr = base / "evidence" / "case.err.txt"
    screenshot = savepath / "baseoq4" / "screenshots" / "renderer-bench" / "sp_0.tga"
    log.parent.mkdir(parents=True)
    stdout.parent.mkdir(parents=True, exist_ok=True)
    marker = timing_marker(map_name="game/airdefense1")
    log.write_text(
        marker
        + "\nMap: tools/mv2\n"
        + "\nrendererBenchmark capture(preset=baseline samples=256 frame(avg=10 p50=8 p95=11 p99=14 max=15 latest=10 thresholds=20/28 pass=1)\n"
        + "Renderer benchmark: preset=baseline samples=256 frame(avg=10 p50=8 p95=11 p99=14 max=15) thresholds(p95=20 p99=28 pass=1)\n"
        + "Selected renderer tier: LowOverheadGL45\n"
        + "PBR material resources: records=3 resourceReady=3 modernReady=3 packed=3 separate=0 fallback=0\n"
        + "Modern GL executor: available, drawPlan=1 planDraws=5 planFallback=0 pbrOwners=2 pbrConsumed=4\n"
        + "Modern forward+: cvar=1, req=1 exec=1 resources=1 sceneColor=1 sceneDepth=1 program=1 cluster=1 draws=2 fallback=0\n"
        + "Modern visible frame: cvar=1, req=1 exec=1 resources=1 program=1 source=1 hybrid=1 backBuffer=1 composed=1\n"
        + "MODE: -1, 1280 x 720 windowed hz:N/A\n",
        encoding="utf-8",
    )
    stdout.write_text(log.read_text(encoding="utf-8"), encoding="utf-8")
    stderr.write_text("", encoding="utf-8")
    write_tga(screenshot)
    spec = harness.RunSpec(
        case_id="sp-airdefense1",
        mode="SP",
        map_name="game/airdefense1",
        budget_map_name="game/airdefense1",
        purpose="test",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="default",
        renderer="best",
        render_api="gl",
    )
    autoexec_cfg, _ = harness.write_autoexec_cfg(
        savepath,
        spec,
        "sp",
        "contract-test",
        360,
        600,
        0,
        harness.PBR_ACCEPTANCE_GL_CVARS,
        gpu_timers=True,
    )
    autoexec_path = savepath / "baseoq4" / autoexec_cfg
    role = harness.evaluate_role_result(
        spec,
        "sp",
        0,
        False,
        1.0,
        savepath,
        "case.log",
        stdout,
        stderr,
        "screenshots/renderer-bench/sp_0.tga",
        None,
        2.0,
        24,
        False,
        True,
        0.0,
        0.0,
        0.0,
        contract,
        "baseline",
        pbr_fixture_acceptance_required=True,
        autoexec_path=autoexec_path,
    )
    assert role["status"] == "pass", role["missing"]
    result = {
        "id": spec.id,
        "mode": "SP",
        "map": spec.map_name,
        "caseId": spec.case_id,
        "sceneContract": spec.scene_contract,
        "budgetMap": spec.budget_map_name,
        "expectedBackend": spec.expected_backend,
        "renderApi": spec.render_api,
        "pbrFixtureAcceptanceRequired": True,
        "displayContract": harness.budget_display_contract(),
        "purpose": "test",
        "tier": "auto",
        "maxfps": "240",
        "swapInterval": "0",
        "display": "windowed",
        "shadowPreset": "default",
        "renderer": "best",
        "status": "pass",
        "roles": [role],
    }
    harness.attach_result_artifacts(base / "evidence", [result])
    runtime_files = harness.collect_runtime_files(runtime_dir)
    metadata = {
        "generated": "2026-08-19 00:00:00 +0000",
        "host": "test",
        "executable": str(executable),
        "runtime": {
            "path": harness.path_hint(runtime_dir, ROOT),
            "executable": executable.relative_to(runtime_dir).as_posix(),
            "files": runtime_files,
        },
        "git": harness.git_state(ROOT),
        "budgetContract": binding,
        "budgetEnforced": True,
        "budgetDisplayContract": harness.budget_display_contract(),
        "basepath": "",
        "profile": "smoke",
        "benchmarkPreset": "baseline",
        "renderApi": "gl",
        "dryRun": False,
        "autoexecDelayMs": 1000,
        "settleFrames": 360,
        "mpClientDelayFrames": 480,
        "sampleFrames": 600,
        "sampleMsec": 0,
        "minPacingHz": 0.0,
        "maxP95Ms": 0.0,
        "maxP99Ms": 0.0,
        "profileCvars": {},
        "effectivePostMapCvars": harness.effective_post_map_cvars(
            harness.PBR_ACCEPTANCE_GL_CVARS
        ),
        "pbrFixtureAcceptanceRequired": True,
        "pbrFixtureBinding": harness.verify_procedural_pbr_fixture(
            runtime_dir, harness.PBR_FIXTURE_TEXTURE_SIZE
        ),
        "profileExecCommands": [],
        "launchCvars": {},
        "execCommands": [],
    }
    report_path, _ = harness.write_reports(base / "evidence", [result], metadata)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert harness.verify_benchmark_report(
        report, report_path.parent, ROOT, runtime_dir, executable, contract, binding
    ) == []

    for field, value in (("caseId", "sp-storage1-second"), ("sceneContract", {})):
        changed_scene = json.loads(json.dumps(report))
        changed_scene["results"][0][field] = value
        assert any("scene" in failure for failure in harness.verify_benchmark_report(
            changed_scene, report_path.parent, ROOT, runtime_dir, executable, contract, binding
        ))
    missing_scene = json.loads(json.dumps(report))
    missing_scene["results"][0]["roles"][0].pop("sceneEvidence")
    assert any("recorded scene evidence differs" in failure for failure in harness.verify_benchmark_report(
        missing_scene, report_path.parent, ROOT, runtime_dir, executable, contract, binding
    ))

    for field, expected_fragment in (
        ("effectivePostMapCvars", "post-map cvar provenance"),
        ("pbrFixtureAcceptanceRequired", "acceptance provenance"),
        ("pbrFixtureBinding", "binding provenance"),
        ("execCommands", "exec-command provenance"),
    ):
        missing_metadata = json.loads(json.dumps(report))
        missing_metadata["metadata"].pop(field)
        assert any(
            expected_fragment in failure
            for failure in harness.verify_benchmark_report(
                missing_metadata,
                report_path.parent,
                ROOT,
                runtime_dir,
                executable,
                contract,
                binding,
            )
        )

    missing_result_selection = json.loads(json.dumps(report))
    missing_result_selection["results"][0].pop("pbrFixtureAcceptanceRequired")
    assert any(
        "acceptance selection is missing" in failure
        for failure in harness.verify_benchmark_report(
            missing_result_selection,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    missing_role_evidence = json.loads(json.dumps(report))
    missing_role_evidence["results"][0]["roles"][0].pop("pbrFixtureEvidence")
    assert any(
        "recorded PBR fixture evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            missing_role_evidence,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    missing_config = json.loads(json.dumps(report))
    missing_config_role = missing_config["results"][0]["roles"][0]
    missing_config_role["artifacts"] = [
        item
        for item in missing_config_role["artifacts"]
        if item["kind"] != "benchmarkConfig"
    ]
    assert any(
        "recorded benchmarkConfig artifact is missing" in failure
        for failure in harness.verify_benchmark_report(
            missing_config,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    original_config = autoexec_path.read_text(encoding="utf-8")
    autoexec_path.write_text(
        original_config.replace("r_pbrmaterials 1", "r_pbrmaterials 0"),
        encoding="utf-8",
    )
    changed_config_report = json.loads(json.dumps(report))
    changed_config_role = changed_config_report["results"][0]["roles"][0]
    changed_config_artifact = next(
        item
        for item in changed_config_role["artifacts"]
        if item["kind"] == "benchmarkConfig"
    )
    changed_config_artifact.update(
        harness.file_record(autoexec_path, report_path.parent)
    )
    assert any(
        "benchmark config CVar provenance differs" in failure
        for failure in harness.verify_benchmark_report(
            changed_config_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    autoexec_path.write_text(original_config, encoding="utf-8")

    autoexec_path.write_text(
        original_config.replace(
            harness.EXEC_COMMANDS_BEGIN,
            harness.EXEC_COMMANDS_BEGIN + "\ngod",
        ),
        encoding="utf-8",
    )
    changed_commands_report = json.loads(json.dumps(report))
    changed_commands_role = changed_commands_report["results"][0]["roles"][0]
    changed_commands_artifact = next(
        item
        for item in changed_commands_role["artifacts"]
        if item["kind"] == "benchmarkConfig"
    )
    changed_commands_artifact.update(
        harness.file_record(autoexec_path, report_path.parent)
    )
    assert any(
        "benchmark config command provenance differs" in failure
        for failure in harness.verify_benchmark_report(
            changed_commands_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    autoexec_path.write_text(original_config, encoding="utf-8")

    autoexec_path.write_text(
        "echo PBR material resources: records=9 resourceReady=9 modernReady=9 "
        "packed=9 separate=0 fallback=0\n" + original_config,
        encoding="utf-8",
    )
    injected_config_report = json.loads(json.dumps(report))
    injected_config_role = injected_config_report["results"][0]["roles"][0]
    injected_config_artifact = next(
        item
        for item in injected_config_role["artifacts"]
        if item["kind"] == "benchmarkConfig"
    )
    injected_config_artifact.update(
        harness.file_record(autoexec_path, report_path.parent)
    )
    assert any(
        "benchmark config provenance is invalid" in failure
        for failure in harness.verify_benchmark_report(
            injected_config_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    autoexec_path.write_text(original_config, encoding="utf-8")

    original_fixture_orm = fixture_paths["orm"].read_bytes()
    fixture_paths["orm"].write_bytes(
        original_fixture_orm[:-1] + bytes((original_fixture_orm[-1] ^ 1,))
    )
    assert any(
        "controlled PBR fixture verification failed" in failure
        for failure in harness.verify_benchmark_report(
            report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    fixture_paths["orm"].write_bytes(original_fixture_orm)

    original_executable = executable.read_bytes()
    executable.write_bytes(b"mutated-runtime-client")
    assert any(
        "runtime file differs" in failure
        for failure in harness.verify_benchmark_report(
            report, report_path.parent, ROOT, runtime_dir, executable, contract, binding
        )
    )
    executable.write_bytes(original_executable)

    original_log = log.read_text(encoding="utf-8")
    log.write_text(original_log.replace("cpuP95Us=11000", "cpuP95Us=12000"), encoding="utf-8")
    changed_log_report = json.loads(json.dumps(report))
    changed_log_role = changed_log_report["results"][0]["roles"][0]
    changed_log_artifact = next(
        item for item in changed_log_role["artifacts"] if item["kind"] == "engineLog"
    )
    changed_log_artifact.update(harness.file_record(log, report_path.parent))
    assert any(
        "recorded renderer budget evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            changed_log_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    log.write_text(original_log, encoding="utf-8")

    log.write_text(
        original_log.replace("MODE: -1, 1280 x 720 windowed", "MODE: -1, 1279 x 720 windowed"),
        encoding="utf-8",
    )
    changed_mode_report = json.loads(json.dumps(report))
    changed_mode_role = changed_mode_report["results"][0]["roles"][0]
    changed_mode_artifact = next(
        item for item in changed_mode_role["artifacts"] if item["kind"] == "engineLog"
    )
    changed_mode_artifact.update(harness.file_record(log, report_path.parent))
    assert any(
        "runtime MODE" in failure or "recorded display evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            changed_mode_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    log.write_text(original_log, encoding="utf-8")

    original_screenshot = screenshot.read_bytes()
    changed_screenshot = bytearray(original_screenshot)
    struct.pack_into("<H", changed_screenshot, 12, 1279)
    screenshot.write_bytes(changed_screenshot)
    changed_screenshot_report = json.loads(json.dumps(report))
    changed_screenshot_role = changed_screenshot_report["results"][0]["roles"][0]
    changed_screenshot_artifact = next(
        item for item in changed_screenshot_role["artifacts"] if item["kind"] == "screenshot"
    )
    changed_screenshot_artifact.update(harness.file_record(screenshot, report_path.parent))
    assert any(
        "engine screenshot is 1279x720" in failure
        or "recorded display evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            changed_screenshot_report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    screenshot.write_bytes(original_screenshot)

    for field, value in (
        ("contractId", "wrong-contract-v1"),
        ("path", "tools/validation/wrong.json"),
        ("sha256", "0" * 64),
    ):
        changed_binding_report = json.loads(json.dumps(report))
        changed_binding_report["budgetContract"][field] = value
        assert any(
            "budget contract binding differs" in failure
            for failure in harness.verify_benchmark_report(
                changed_binding_report,
                report_path.parent,
                ROOT,
                runtime_dir,
                executable,
                contract,
                binding,
            )
        )

    changed_contract_payload = json.loads(
        budget.DEFAULT_CONTRACT_PATH.read_text(encoding="utf-8")
    )
    changed_contract_payload["budgets"][0]["cpu"]["p95Us"] = 19000
    changed_contract_path = base / "changed-contract.json"
    changed_contract_path.write_text(json.dumps(changed_contract_payload), encoding="utf-8")
    changed_contract, changed_binding = budget.load_contract(changed_contract_path)
    assert any(
        "budget contract binding differs" in failure
        for failure in harness.verify_benchmark_report(
            report,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            changed_contract,
            changed_binding,
        )
    )

    wrong_backend = json.loads(json.dumps(report))
    wrong_backend["results"][0]["expectedBackend"] = "vulkan"
    assert any(
        "backend/launch render API binding differs" in failure
        for failure in harness.verify_benchmark_report(
            wrong_backend, report_path.parent, ROOT, runtime_dir, executable, contract, binding
        )
    )
    wrong_measurement = json.loads(json.dumps(report))
    wrong_measurement["results"][0]["roles"][0]["budgetEvidence"]["measurement"]["cpu"]["p95Us"] = 1
    assert any(
        "recorded renderer budget evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            wrong_measurement, report_path.parent, ROOT, runtime_dir, executable, contract, binding
        )
    )

    wrong_display = json.loads(json.dumps(report))
    wrong_display["metadata"]["budgetDisplayContract"]["width"] = 1920
    assert any(
        "budget display contract differs" in failure
        for failure in harness.verify_benchmark_report(
            wrong_display, report_path.parent, ROOT, runtime_dir, executable, contract, binding
        )
    )
    wrong_result_display = json.loads(json.dumps(report))
    wrong_result_display["results"][0]["displayContract"]["cvars"]["r_borderless"] = "1"
    assert any(
        "launch display contract differs" in failure
        for failure in harness.verify_benchmark_report(
            wrong_result_display,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    wrong_runtime_evidence = json.loads(json.dumps(report))
    wrong_runtime_evidence["results"][0]["roles"][0]["displayEvidence"]["runtime"]["width"] = 1279
    assert any(
        "recorded display evidence differs" in failure
        for failure in harness.verify_benchmark_report(
            wrong_runtime_evidence,
            report_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    def functional_fixture_report(render_api: str, tier: str) -> tuple[dict, Path]:
        evidence_root = base / f"functional-{render_api}-{tier}"
        fixture_savepath = evidence_root / "savepaths" / "case"
        fixture_log = fixture_savepath / "baseoq4" / "logs" / "case.log"
        fixture_stdout = evidence_root / "case.out.txt"
        fixture_stderr = evidence_root / "case.err.txt"
        fixture_screenshot = (
            fixture_savepath
            / "baseoq4"
            / "screenshots"
            / "renderer-bench"
            / "sp_0.tga"
        )
        fixture_log.parent.mkdir(parents=True)
        fixture_stdout.parent.mkdir(parents=True, exist_ok=True)
        api_lines = (
            [
                "Selected renderer tier: " + harness.GL_TIER_RUNTIME_NAMES[tier],
                "Modern GL executor: available, drawPlan=1 planDraws=5 planFallback=0 pbrOwners=2 pbrConsumed=4",
                "Modern forward+: cvar=1, req=1 exec=1 resources=1 sceneColor=1 sceneDepth=1 program=1 cluster=1 draws=2 fallback=0",
                "Modern visible frame: cvar=1, req=1 exec=1 resources=1 program=1 source=1 hybrid=1 backBuffer=1 composed=1",
            ]
            if render_api == "gl"
            else ["Vulkan: native PBR direct interactions active (7 draws)"]
        )
        fixture_log_text = "\n".join(
            [
                "Map: tools/mv2",
                "PBR material resources: records=3 resourceReady=3 modernReady=3 packed=3 separate=0 fallback=0",
                *api_lines,
                "MODE: -1, 1280 x 720 windowed hz:N/A",
            ]
        ) + "\n"
        fixture_log.write_text(fixture_log_text, encoding="utf-8")
        fixture_stdout.write_text(fixture_log_text, encoding="utf-8")
        fixture_stderr.write_text("", encoding="utf-8")
        write_tga(fixture_screenshot)
        fixture_spec = harness.RunSpec(
            case_id=harness.PBR_FIXTURE_BENCHMARK_CASE,
            mode="SP",
            map_name=harness.PBR_FIXTURE_STOCK_MAP,
            budget_map_name=harness.PBR_FIXTURE_STOCK_MAP,
            purpose="functional PBR replay",
            path_name="spawn-static",
            tier=tier,
            maxfps="240",
            swap_interval="0",
            display_mode="windowed",
            shadow_preset="default",
            renderer="best",
            render_api=render_api,
        )
        fixture_cvars = harness.pbr_acceptance_fixture_cvars(render_api)
        fixture_cfg, _ = harness.write_autoexec_cfg(
            fixture_savepath,
            fixture_spec,
            "sp",
            "functional-test",
            360,
            600,
            0,
            fixture_cvars,
            gpu_timers=False,
            renderer_metrics=False,
        )
        fixture_cfg_path = fixture_savepath / "baseoq4" / fixture_cfg
        fixture_role = harness.evaluate_role_result(
            fixture_spec,
            "sp",
            0,
            False,
            1.0,
            fixture_savepath,
            "case.log",
            fixture_stdout,
            fixture_stderr,
            "screenshots/renderer-bench/sp_0.tga",
            None,
            2.0,
            24,
            False,
            False,
            0.0,
            0.0,
            0.0,
            contract,
            "baseline",
            pbr_fixture_acceptance_required=True,
            autoexec_path=fixture_cfg_path,
        )
        assert fixture_role["status"] == "pass", fixture_role["missing"]
        fixture_result = {
            "id": fixture_spec.id,
            "mode": "SP",
            "map": fixture_spec.map_name,
            "caseId": fixture_spec.case_id,
            "sceneContract": fixture_spec.scene_contract,
            "budgetMap": fixture_spec.budget_map_name,
            "expectedBackend": fixture_spec.expected_backend,
            "renderApi": fixture_spec.render_api,
            "pbrFixtureAcceptanceRequired": True,
            "displayContract": harness.budget_display_contract(),
            "purpose": fixture_spec.purpose,
            "tier": fixture_spec.tier,
            "maxfps": fixture_spec.maxfps,
            "swapInterval": fixture_spec.swap_interval,
            "display": fixture_spec.display_mode,
            "shadowPreset": fixture_spec.shadow_preset,
            "renderer": fixture_spec.renderer,
            "status": "pass",
            "roles": [fixture_role],
        }
        harness.attach_result_artifacts(evidence_root, [fixture_result])
        fixture_metadata = {
            "generated": "2026-08-23 00:00:00 +0000",
            "host": "test",
            "executable": str(executable),
            "runtime": {
                "path": harness.path_hint(runtime_dir, ROOT),
                "executable": executable.relative_to(runtime_dir).as_posix(),
                "files": harness.collect_runtime_files(runtime_dir),
            },
            "runtimeVerificationFailures": [],
            "git": harness.git_state(ROOT),
            "budgetContract": binding,
            "budgetEnforced": False,
            "budgetDisplayContract": None,
            "basepath": "",
            "profile": "interaction",
            "benchmarkPreset": "baseline",
            "renderApi": render_api,
            "dryRun": False,
            "autoexecDelayMs": 1000,
            "settleFrames": 360,
            "mpClientDelayFrames": 480,
            "sampleFrames": 600,
            "sampleMsec": 0,
            "minPacingHz": 0.0,
            "maxP95Ms": 0.0,
            "maxP99Ms": 0.0,
            "profileCvars": {},
            "effectivePostMapCvars": harness.effective_post_map_cvars(
                fixture_cvars
            ),
            "pbrFixtureAcceptanceRequired": True,
            "pbrFixtureBinding": harness.verify_procedural_pbr_fixture(
                runtime_dir, harness.PBR_FIXTURE_TEXTURE_SIZE
            ),
            "profileExecCommands": [],
            "launchCvars": {},
            "execCommands": [],
        }
        fixture_report_path, _ = harness.write_reports(
            evidence_root, [fixture_result], fixture_metadata
        )
        return (
            json.loads(fixture_report_path.read_text(encoding="utf-8")),
            fixture_report_path,
        )

    functional_gl, functional_gl_path = functional_fixture_report("gl", "gl45")
    assert harness.verify_benchmark_report(
        functional_gl,
        functional_gl_path.parent,
        ROOT,
        runtime_dir,
        executable,
        contract,
        binding,
    ) == []
    functional_vk, functional_vk_path = functional_fixture_report("vk", "auto")
    assert harness.verify_benchmark_report(
        functional_vk,
        functional_vk_path.parent,
        ROOT,
        runtime_dir,
        executable,
        contract,
        binding,
    ) == []
    functional_gl33, functional_gl33_path = functional_fixture_report("gl", "gl33")
    assert harness.verify_benchmark_report(
        functional_gl33,
        functional_gl33_path.parent,
        ROOT,
        runtime_dir,
        executable,
        contract,
        binding,
    ) == []

    ordinary_non_budget = json.loads(json.dumps(functional_gl))
    ordinary_non_budget["metadata"]["effectivePostMapCvars"] = {}
    ordinary_non_budget["metadata"]["pbrFixtureAcceptanceRequired"] = False
    ordinary_non_budget["metadata"]["pbrFixtureBinding"] = None
    ordinary_non_budget["results"][0]["pbrFixtureAcceptanceRequired"] = False
    assert any(
        "functional replay is accepted only" in failure
        for failure in harness.verify_benchmark_report(
            ordinary_non_budget,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    duplicated_functional_result = json.loads(json.dumps(functional_gl))
    duplicated_functional_result["results"].append(
        json.loads(json.dumps(duplicated_functional_result["results"][0]))
    )
    assert any(
        "exactly one controlled result" in failure
        for failure in harness.verify_benchmark_report(
            duplicated_functional_result,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    tampered_mode = json.loads(json.dumps(report))
    tampered_mode["budgetEnforced"] = False
    tampered_mode["metadata"]["budgetDisplayContract"] = None
    tampered_mode_failures = harness.verify_benchmark_report(
        tampered_mode,
        report_path.parent,
        ROOT,
        runtime_dir,
        executable,
        contract,
        binding,
    )
    assert any("config capture mode differs" in failure for failure in tampered_mode_failures)
    assert any("budget evidence must be empty" in failure for failure in tampered_mode_failures)

    tampered_budget_evidence = json.loads(json.dumps(functional_gl))
    tampered_budget_evidence["results"][0]["roles"][0]["budgetEvidence"] = {
        "status": "pass"
    }
    assert any(
        "budget evidence must be empty" in failure
        for failure in harness.verify_benchmark_report(
            tampered_budget_evidence,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    missing_null_display_contract = json.loads(json.dumps(functional_gl))
    missing_null_display_contract["metadata"].pop("budgetDisplayContract")
    assert any(
        "display contract differs" in failure
        for failure in harness.verify_benchmark_report(
            missing_null_display_contract,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    tampered_outcome = json.loads(json.dumps(functional_gl))
    tampered_outcome["results"][0]["roles"][0]["exitCode"] = 1
    tampered_outcome["results"][0]["roles"][0]["timedOut"] = True
    outcome_failures = harness.verify_benchmark_report(
        tampered_outcome,
        functional_gl_path.parent,
        ROOT,
        runtime_dir,
        executable,
        contract,
        binding,
    )
    assert any("exit code is not zero" in failure for failure in outcome_failures)
    assert any("timed-out state is not false" in failure for failure in outcome_failures)

    tampered_identity = json.loads(json.dumps(functional_gl))
    tampered_identity["results"][0]["map"] = "game/storage1"
    assert any(
        "case/result identity differs" in failure
        for failure in harness.verify_benchmark_report(
            tampered_identity,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    functional_log_artifact = next(
        item
        for item in functional_gl["results"][0]["roles"][0]["artifacts"]
        if item["kind"] == "engineLog"
    )
    functional_log_path = functional_gl_path.parent / functional_log_artifact["path"]
    functional_log_original = functional_log_path.read_text(encoding="utf-8")
    functional_log_path.write_text(
        functional_log_original.replace("Map: tools/mv2", "Map: game/storage1"),
        encoding="utf-8",
    )
    tampered_final_map = json.loads(json.dumps(functional_gl))
    tampered_final_map_artifact = next(
        item
        for item in tampered_final_map["results"][0]["roles"][0]["artifacts"]
        if item["kind"] == "engineLog"
    )
    tampered_final_map_artifact.update(
        harness.file_record(functional_log_path, functional_gl_path.parent)
    )
    assert any(
        "final engine Map differs" in failure
        for failure in harness.verify_benchmark_report(
            tampered_final_map,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )
    functional_log_path.write_text(functional_log_original, encoding="utf-8")

    tampered_tier = json.loads(json.dumps(functional_gl))
    tampered_tier_result = tampered_tier["results"][0]
    tampered_tier_result["tier"] = "gl43"
    tampered_tier_result["id"] = tampered_tier_result["id"].replace(
        "_gl45_", "_gl43_"
    )
    assert any(
        "selected tier differs" in failure
        for failure in harness.verify_benchmark_report(
            tampered_tier,
            functional_gl_path.parent,
            ROOT,
            runtime_dir,
            executable,
            contract,
            binding,
        )
    )

    for invalid_args in (
        ["--width", "1920"],
        ["--height", "1080"],
        ["--display-modes", "fullscreen"],
        ["--set-launch-cvar", "r_windowWidth=1920"],
        ["--set-launch-cvar", "r_renderApi=vk"],
        ["--set-launch-cvar", "r_pbrMaterials=1"],
    ):
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                harness.parse_args(invalid_args)
        except SystemExit as exc:
            assert exc.code == 2
        else:
            raise AssertionError(f"noncanonical budget display must fail: {invalid_args}")


def test_scene_views(base: Path) -> None:
    harness = load_benchmark()
    args = harness.parse_args(["--cases", "sp-storage1,sp-storage1-second", "--dry-run"])
    args.runtime_dir_path = base
    specs = harness.build_specs(args)
    for spec, expected_filter, expected_settle in zip(specs, ("first", "second"), (360, 720)):
        plan = harness.run_sp_spec(ROOT, base / "client", base / spec.case_id, "", "scene-test", spec, args)
        assert plan["args"][-3:] == ["+map", "game/storage1", expected_filter]
        cfg = (base / spec.case_id / "savepaths" / spec.id / "baseoq4" / plan["autoexecCfg"]).read_text()
        evidence = harness.parse_benchmark_config(cfg)
        assert evidence.entity_filter == expected_filter and evidence.scene_map == "game/storage1"
        assert evidence.settle_frames == expected_settle
        assert evidence.settle_msec == (6000 if expected_filter == "second" else 0)
        assert harness.parse_benchmark_config(cfg.replace("waitMsec 6000\n", "")).settle_msec == 0
        for mutation in (
            cfg.replace(f"echo {harness.VIEW_END}\n", ""),
            cfg.replace(f"echo {harness.VIEW_BEGIN}\n", f"echo {harness.VIEW_BEGIN}\n" * 2),
            cfg.replace(f"echo {harness.VIEW_END}\nopenq4_assertMapState game/storage1 {expected_filter}",
                        f"echo {harness.VIEW_END}\nopenq4_assertMapState game/storage1 other"),
        ):
            try:
                harness.parse_benchmark_config(mutation)
            except ValueError:
                pass
            else:
                raise AssertionError("modified scene scaffold must fail replay")

    def snapshot(marker: str, z: str = "4.2", yaw: str = "90.0", entry: str = "second") -> str:
        return (f"{marker} \nopenQ4 map state: map=game/storage1 entityFilter={entry} "
                f"expectedMap=game/storage1 expectedEntityFilter={entry}\n"
                f"origin: (-2592.0 2504.0 {z}) angles: (-0.0 {yaw} -0.0)\n")

    begin = snapshot(harness.VIEW_BEGIN)
    end = snapshot(harness.VIEW_END)
    clean = begin + end
    contract = specs[1].scene_contract
    evidence, failures = harness.evaluate_scene_view_evidence([("log", clean), ("stdout", clean)], contract)
    assert not failures and evidence["status"] == "pass"
    assert evidence["observations"][0]["positionDelta"] == 0
    # Angular wrap is equivalent; sub-unit eye-height rounding is tolerated.
    assert not harness.evaluate_scene_view_evidence(
        [("log", begin + snapshot(harness.VIEW_END, "4.3", "450.0"))], contract
    )[1]
    for payload in (
        begin, end + begin, clean + end,
        begin + snapshot(harness.VIEW_END, "72.9"),  # unsettled lift
        snapshot(harness.VIEW_BEGIN, "-64.5") + snapshot(harness.VIEW_END, "-64.5"),  # stationary wrong view
        begin + snapshot(harness.VIEW_END, yaw="94.0"),
        begin + snapshot(harness.VIEW_END, entry="first"),
        begin + snapshot(harness.VIEW_END, "nan"),
        begin + snapshot(harness.VIEW_END, "inf"),
        clean.replace("origin:", "missing:"),
    ):
        assert harness.evaluate_scene_view_evidence([("log", payload)], contract)[1], payload
    assert harness.evaluate_scene_view_evidence([("stdout", clean)], contract)[1]
    assert harness.evaluate_scene_view_evidence(
        [("log", clean), ("stdout", begin + snapshot(harness.VIEW_END, "4.3"))], contract
    )[1]
    intro = snapshot(harness.VIEW_BEGIN, "1000", entry="first") + snapshot(harness.VIEW_END, "2000", entry="first")
    assert not harness.evaluate_scene_view_evidence([("log", intro)], specs[0].scene_contract)[1]
    for command in (f"echo {harness.VIEW_END}", "echo openQ4 map state:", "echo origin: (0 0 0)"):
        try:
            harness.parse_exec_commands([command])
        except ValueError:
            pass
        else:
            raise AssertionError("scene evidence must not be injectable through user commands")


def test_fullscreen_pacing(base: Path) -> None:
    """Pacing uses its own display; an average cannot hide a frame over budget."""
    harness = load_benchmark()
    save = base / "fullscreen"
    log = save / "baseoq4/logs/case.log"
    shot = save / "baseoq4/screenshots/case.tga"
    log.parent.mkdir(parents=True)
    write_tga(shot, 2560, 1440)
    text = (
        "Selected renderer tier: TopGL46\n"
        "MODE: -1, 2560 x 1440 fullscreen policy=exclusive hz:N/A\n"
        "Frame pacing snapshot (SP): bound=frame-work, samples=2000, "
        "present=4.00 ms (250.0 Hz), p50=4.00 ms, p95=5.00 ms, "
        "p99=6.00 ms, max=8.00 ms\n"
        "Frame timing window (SP): samples=2000, span_ms=10000, mean_ms=5.000000\n"
    )
    log.write_text(text, encoding="utf-8")
    stdout, stderr = save / "out.txt", save / "err.txt"
    stdout.write_text("", encoding="utf-8")
    stderr.write_text("", encoding="utf-8")
    spec = harness.RunSpec("sp-airdefense1", "SP", "game/airdefense1", "game/airdefense1",
                           "pacing test", "spawn-static", "auto", "0", "0", "fullscreen", "default", "best", "gl")
    def evaluate(limit: float, budget: bool = False):
        return harness.evaluate_role_result(
            spec, "sp", 0, False, 12, save, "case.log", stdout, stderr,
            "screenshots/case.tga", None, 2, 24, False,
            require_benchmark=budget, min_pacing_hz=150,
            display_width=2560, display_height=1440, max_frame_ms=limit,
        )
    assert evaluate(0)["status"] == "pass"
    assert float(evaluate(0)["summary"]["pacingHz"]) == 200
    # A fast last view must not hide a slow full rotation.
    log.write_text(text.replace("mean_ms=5.000000", "mean_ms=8.000000"), encoding="utf-8")
    assert any("pacingHz=" in item for item in evaluate(0)["missing"])
    log.write_text(text, encoding="utf-8")
    assert evaluate(8)["status"] == "pass"
    slow = evaluate(6.66)
    assert slow["status"] == "fail" and any("pacingMax=" in item for item in slow["missing"])
    # The CPU/GPU budget contract remains windowed 1280x720.
    assert any("display evidence:" in item for item in evaluate(0, True)["missing"])
    log.write_text(text.replace(", max=8.00 ms", ""), encoding="utf-8")
    assert "frame pacing maximum" in evaluate(6.66)["missing"]
    log.write_text(text.replace("fullscreen", "windowed"), encoding="utf-8")
    assert any("display evidence:" in item for item in evaluate(0)["missing"])
    log.write_text(text, encoding="utf-8")
    write_tga(shot, 1280, 720)
    assert any("engine screenshot" in item for item in evaluate(0)["missing"])


def main() -> None:
    test_contract_and_marker()
    harness = load_benchmark()
    runtime_parent = ROOT / ".tmp" / "stock-runtime"
    runtime_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="renderer-runtime-contract-", dir=runtime_parent) as raw:
        runtime = Path(raw)
        assert harness.validate_runtime_dir(runtime, ROOT) == runtime.resolve()
    temp_parent = ROOT / ".tmp" / "renderer-budget-tests"
    temp_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="contract-", dir=temp_parent) as raw:
        base = Path(raw)
        test_schema_rejection(base)
        test_benchmark_replay(base)
        test_scene_views(base)
        test_fullscreen_pacing(base)
    print("renderer_budget_contract: ok")


if __name__ == "__main__":
    main()
