#!/usr/bin/env python3
"""Static contract for native macOS dedicated-server smoke coverage."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def main() -> None:
    runner = read("tools/tests/macos_dedicated_server_smoke.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    validator = read("tools/validation/openq4_validate.py")
    platform_support = read("docs/dev/platform-support.md")
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")

    for token in (
        'sys.platform != "darwin"',
        'f"openQ4-ded_{arch}"',
        'f"game_{arch}.dylib"',
        "executable.is_symlink()",
        "game_module.is_symlink()",
        'q4base / "pak001.pk4"',
        "zipfile.ZipFile",
        '"fs_validateOfficialPaks", "0"',
        '"g_allowAssetlessStartup", "1"',
        '"si_gameType", "dm"',
        '"s_noSound", "1"',
        '"net_serverDedicated", "1"',
        'IPV4_SELF_TEST_MARKER = "IPv4 network self-test: passed"',
        'CANONICAL_GAMETYPE_MARKER = \'"si_gameType" is:"DM"\'',
        "IPV4_SELF_TEST_MARKER,",
        "CANONICAL_GAMETYPE_MARKER,",
        '"+netIPv4SelfTest"',
        '"+si_gameType"',
        '"IPv4 network self-test: FAILED"',
        '"+wait", "1"',
        '"Selected game module: logical=\'game_mp\'"',
        '"game initialized."',
        '"--- Common Initialization Complete ---"',
        '"Type \'help\' for dedicated server info."',
        '"--------------- Game Shutdown ---------------"',
        '"ERROR:"',
        "subprocess.TimeoutExpired",
    ):
        require(runner, token, "macOS dedicated-server smoke runner")
    command = runner[runner.index("    command = [") : runner.index("\n    ]", runner.index("    command = ["))]
    if not (
        command.index('"si_gameType", "dm"')
        < command.index('"+netIPv4SelfTest"')
        < command.index('"+si_gameType"')
        < command.index('"+echo", READY_MARKER')
    ):
        raise AssertionError("macOS dedicated smoke must test IPv4 and canonical DM before its ready marker")
    reject(runner, "Program Files", "asset-free macOS dedicated-server smoke runner")
    reject(runner, "steamapps", "asset-free macOS dedicated-server smoke runner")

    for workflow, context, artifact in (
        (commit, "commit validation", "commit-macos-arm64-${{ matrix.artifact_suffix }}-dedicated-smoke"),
        (push, "push verification", "push-${{ matrix.artifact_name }}-dedicated-smoke"),
    ):
        require(workflow, "python tools/tests/macos_dedicated_server_smoke.py", f"{context} macOS smoke wiring")
        if workflow.count("tools/tests/macos_dedicated_server_smoke.py") < 2:
            raise AssertionError(f"{context} must compile and execute the macOS dedicated-server smoke runner")
        if workflow.count("tools/tests/macos_dedicated_server_smoke_contract.py") < 2:
            raise AssertionError(f"{context} must compile and execute the macOS dedicated-server smoke contract")
        require(workflow, artifact, f"{context} macOS smoke artifact")
        require(workflow, "if: always()", f"{context} macOS smoke artifact retention")

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation static contract"),
        (push, "push verification static contract"),
    ):
        require(source, "macos_dedicated_server_smoke_contract.py", context)

    require(platform_support, "asset-free macOS dedicated-server startup", "macOS platform support policy")
    require(plan, "hosted assetless dedicated-server smoke", "MAC-012 hosted coverage status")
    require(
        release_completion,
        "macOS dedicated-server builds now receive a hosted startup gate",
        "release completion macOS dedicated-server entry",
    )
    require(
        release_notes,
        "macOS dedicated-server builds now run an asset-free hosted startup smoke",
        "curated release notes macOS dedicated-server entry",
    )

    # The macOS dedicated server shares the SDL client's source list, which
    # drops the callback-free native input facade for the client's driver. It
    # links only if it takes the facade back, and never takes the driver.
    meson = read("meson.build")
    snapshot = meson.index("openq4_dedicated_sources = openq4_engine_sources")
    facade = meson.index("openq4_dedicated_sources += files('src/framework/NativeInputDispatchDisabled.cpp')")
    driver = meson.index("'src/sys/sdl3/NativeInputDriver.cpp')")
    guard = meson.rindex("if dedicated_shares_client_sources", 0, facade)
    if not snapshot < guard < facade < driver:
        raise AssertionError("macOS dedicated server: take back the disabled native input facade after the client snapshot")
    # Sharing is the default every target without its own list inherits.
    require(meson[snapshot:snapshot + 120], "\n  dedicated_shares_client_sources = true\n", "dedicated targets share the client list by default")
    for added in re.finditer(r"openq4_dedicated_sources\s*\+=\s*(files\([^)]*\)|\[[^\]]*\])", meson):
        reject(added.group(1), "NativeInputDriver", "the dedicated server never links the client's native input driver")
    for platform in ("  if windows_gl_free_ded\n", "  if host_system == 'linux'\n"):
        block = meson[meson.index(platform, snapshot):facade]
        require(block[:200], "dedicated_shares_client_sources = false", "dedicated targets with their own source list")
    sources = read("tools/build/meson_sources.py")
    require(sources, 'if args.target_kind == "client" and args.platform_backend == "sdl3":', "client-only facade removal")

    print("macos_dedicated_server_smoke_contract: ok")


if __name__ == "__main__":
    main()
