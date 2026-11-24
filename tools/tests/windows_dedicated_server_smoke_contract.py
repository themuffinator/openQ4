#!/usr/bin/env python3
"""Static contract for native Windows x64 and ARM64 dedicated-server smoke coverage."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = "tools/tests/windows_dedicated_server_smoke.py"
CONTRACT = "tools/tests/windows_dedicated_server_smoke_contract.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(source: str, needles: tuple[str, ...], context: str) -> None:
    positions = []
    for needle in needles:
        require(source, needle, context)
        positions.append(source.index(needle))
    if positions != sorted(positions):
        raise AssertionError(f"{context} must run {' -> '.join(repr(needle) for needle in needles)} in that order")


def job_block(workflow: str, job_id: str, context: str) -> str:
    header = f"\n  {job_id}:\n"
    start = workflow.find(header)
    if start < 0:
        raise AssertionError(f"Missing job {job_id!r} in {context}")
    body_start = start + len(header)
    following = re.search(r"\n  [A-Za-z0-9_-]+:\n", workflow[body_start:])
    end = body_start + following.start() if following else len(workflow)
    return workflow[start:end]


def validate_runner() -> None:
    runner = read(RUNNER)
    for token in (
        'sys.platform != "win32"',
        '"x64": 0x8664',
        '"arm64": 0xAA64',
        "IsWow64Process2",
        'f"arm64 binaries cannot start on this {host_arch} host"',
        'f"openQ4-ded_{arch}.exe"',
        'f"game_{arch}.dll"',
        'executable.parent / "OpenAL32.dll"',
        'require_pe_arch(executable, arch, "dedicated-server executable")',
        'require_pe_arch(game_module, arch, "multiplayer game module")',
        'require_pe_arch(openal_runtime, arch, "OpenAL runtime")',
        "path.is_symlink()",
        'q4base / "pak001.pk4"',
        "zipfile.ZipFile",
        '"fs_validateOfficialPaks", "0"',
        '"g_allowAssetlessStartup", "1"',
        '"si_gameType", "dm"',
        '"s_noSound", "1"',
        '"net_serverDedicated", "1"',
        '"logFile", "2"',
        'IPV4_SELF_TEST_MARKER = "IPv4 network self-test: passed"',
        'CANONICAL_GAMETYPE_MARKER = \'"si_gameType" is:"DM"\'',
        "IPV4_SELF_TEST_MARKER,",
        "CANONICAL_GAMETYPE_MARKER,",
        '"+netIPv4SelfTest"',
        '"+si_gameType"',
        '"+wait", "1"',
        '"+quit"',
        '"Selected game module: logical=\'game_mp\'"',
        '"game initialized."',
        '"--- Common Initialization Complete ---"',
        '"Type \'help\' for dedicated server info."',
        '"--------------- Game Shutdown ---------------"',
        '"IPv4 network self-test: FAILED"',
        '"ERROR:"',
        '"FATAL:"',
        # Loader, fault and fatal-error dialogs must not hold the run open.
        "SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX",
        'crash_dir = executable.parent / "crashes"',
        'path.suffix.lower() == ".log"',
        "BLOCKED_GRACE_SECONDS",
        "process.kill()",
        "exit_code == 0",
        "and not timed_out",
        "and not blocked_by",
        "and not crash_files",
    ):
        require(runner, token, "Windows dedicated-server smoke runner")
    command = runner[runner.index("    command = [") : runner.index("\n    ]", runner.index("    command = ["))]
    require_order(
        command,
        ('"si_gameType", "dm"', '"+netIPv4SelfTest"', '"+si_gameType"', '"+echo", READY_MARKER', '"+quit"'),
        "Windows dedicated smoke command",
    )
    reject(runner, "Program Files", "asset-free Windows dedicated-server smoke runner")
    reject(runner, "steamapps", "asset-free Windows dedicated-server smoke runner")
    reject(runner, "CREATE_DEFAULT_ERROR_MODE", "Windows dedicated-server smoke runner error-mode inheritance")


def validate_smoke_job(job: str, context: str, arch: str, artifact: str, build_step: str) -> None:
    smoke = f"python tools/tests/windows_dedicated_server_smoke.py --arch {arch}"
    require(job, smoke, context)
    require(job, f"--output-root .tmp/windows-dedicated-smoke/{arch}", context)
    require(job, f"name: {artifact}", context)
    require(job, f"path: .tmp/windows-dedicated-smoke/{arch}", context)
    require_order(job, (build_step, smoke, f"name: {artifact}"), context)
    publish = job[job.rindex("- name:", 0, job.index(f"name: {artifact}")) : job.index(f"name: {artifact}")]
    require(publish, "if: always()", f"{context} smoke artifact retention")


def validate_arm64_job(job: str, context: str, profile: str) -> None:
    require(job, "runs-on: windows-11-arm", context)
    require(job, "OPENQ4_VS_TARGET_ARCH: arm64", context)
    validation = f"python tools/validation/openq4_validate.py {profile}"
    require_order(
        job,
        (
            'prepare_windows_openal.ps1 -Architecture "arm64"',
            "OPENQ4_OPENAL_ROOT=$outputRoot",
            validation,
        ),
        context,
    )
    # The bundled OpenAL import library is x64-only; an arm64 link needs the
    # OpenAL Soft package prepared above.
    require(job, '"--extra-setup-arg=-Dopenal_root_override=$env:OPENQ4_OPENAL_ROOT"', context)
    reject(job, 'prepare_windows_openal.ps1 -Architecture "x64"', context)


def validate_workflows() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    push_x64 = job_block(push, "windows-build", "push verification")
    validate_smoke_job(
        push_x64,
        "push verification Windows x64 job",
        "x64",
        "push-windows-x64-dedicated-smoke",
        "tools/validation/validate_push.ps1 --install",
    )
    push_arm64 = job_block(push, "windows-arm64-build", "push verification")
    validate_arm64_job(push_arm64, "push verification Windows ARM64 job", "push")
    validate_smoke_job(
        push_arm64,
        "push verification Windows ARM64 job",
        "arm64",
        "push-windows-arm64-dedicated-smoke",
        "python tools/validation/openq4_validate.py push --install",
    )

    commit_x64 = job_block(commit, "windows-x64", "commit validation")
    validate_smoke_job(
        commit_x64,
        "commit validation Windows x64 job",
        "x64",
        "commit-windows-x64-dedicated-smoke",
        "tools/validation/validate_pr.ps1",
    )
    commit_arm64 = job_block(commit, "windows-arm64", "commit validation")
    validate_arm64_job(commit_arm64, "commit validation Windows ARM64 job", "pr")
    validate_smoke_job(
        commit_arm64,
        "commit validation Windows ARM64 job",
        "arm64",
        "commit-windows-arm64-dedicated-smoke",
        "python tools/validation/openq4_validate.py pr",
    )

    for workflow, context in ((push, "push verification"), (commit, "commit validation")):
        # Both files are compiled by the script smoke; only the contract can
        # run there, because the smoke itself needs a staged Windows payload.
        for relative_path in (RUNNER, CONTRACT):
            require(workflow, f"            {relative_path} \\\n", f"{context} py_compile list")
        require(workflow, f"python {CONTRACT}", f"{context} static contract")


def validate_local_runner() -> None:
    validator = read("tools/validation/openq4_validate.py")
    require(validator, 'root / "tools" / "tests" / "windows_dedicated_server_smoke_contract.py"', "local validation runner")


def main() -> None:
    validate_runner()
    validate_workflows()
    validate_local_runner()
    print("windows_dedicated_server_smoke_contract: ok")


if __name__ == "__main__":
    main()
