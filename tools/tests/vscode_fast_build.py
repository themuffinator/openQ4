#!/usr/bin/env python3
"""Regression checks for the VS Code fast default build path, launch configurations and the Codex actions that run them."""

from __future__ import annotations

import json
import re
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CODEX_RUNNER = ".codex/scripts/run-vscode-entry.ps1"
# How the Codex setup script and every action run a VS Code entry. Inside the
# double quotes, $ and ` would expand in PowerShell and % in cmd.
CODEX_COMMAND = re.compile(
    r'powershell -NoProfile -ExecutionPolicy Bypass -File \.codex\\scripts\\run-vscode-entry\.ps1 (task|launch) "([^"$`%]+)"'
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def find_task(tasks: list[dict[str, object]], label: str) -> dict[str, object]:
    for task in tasks:
        if task.get("label") == label:
            return task
    raise AssertionError(f"Missing VS Code task {label!r}")


def validate_tasks() -> None:
    tasks = json.loads(read(".vscode/tasks.json"))["tasks"]
    default_tasks = [
        task
        for task in tasks
        if isinstance(task.get("group"), dict) and task["group"].get("kind") == "build" and task["group"].get("isDefault") is True
    ]
    if len(default_tasks) != 1:
        raise AssertionError(f"Expected exactly one default build task, found {len(default_tasks)}")

    fast_build = find_task(tasks, "Build openQ4 (Meson Optimized)")
    if fast_build is not default_tasks[0]:
        raise AssertionError("Build openQ4 (Meson Optimized) must be the default VS Code build task")
    if fast_build.get("dependsOn"):
        raise AssertionError("Fast default build must not depend on configure or full install tasks")
    if "fastbuild" not in fast_build.get("args", []):
        raise AssertionError("Fast default build must invoke meson-task.ps1 fastbuild")

    full_build = find_task(tasks, "Full Build and Stage openQ4 (Meson Optimized)")
    if full_build.get("dependsOrder") != "sequence":
        raise AssertionError("Full build task must keep ordered configure, compile, install steps")
    for label in (
        "Configure openQ4 (Meson Optimized)",
        "Compile openQ4 (Meson Optimized)",
        "Stage openQ4 Install Tree (Meson Optimized)",
    ):
        if label not in full_build.get("dependsOn", []):
            raise AssertionError(f"Full build task is missing dependency {label!r}")

    for task in tasks:
        if "fs_basepath" in task.get("args", []):
            raise AssertionError(f"VS Code task {task.get('label')!r} must leave fs_basepath to the engine's install discovery")


def validate_wrapper() -> None:
    wrapper = read(".vscode/meson-task.ps1")
    require(wrapper, "[ValidateSet('setup', 'compile', 'install', 'fastbuild')]", "VS Code Meson wrapper actions")
    require(wrapper, "stage_fast_install.py", "VS Code fast build staging script")
    # The launch configurations run whatever this build stages into .install, so it
    # has to be optimized. Configuring it as a plain debug build measured 146.5 Hz
    # against 315.7 Hz for the same source on game/airdefense1.
    require(wrapper, "'debugoptimized'", "VS Code build must configure an optimized buildtype")
    reject(wrapper, "'debug',", "VS Code build must not configure an unoptimized buildtype")
    # Release packages also set b_ndebug=true; keep the launch tree on the same
    # options, and reconfigure an existing build directory that predates them.
    require(wrapper, "'b_ndebug' = 'true'", "VS Code build must compile out C asserts like the release packages")
    require(wrapper, "Get-StaleBuildOptions $buildDir", "VS Code compile reconfigures stale build options")
    require(wrapper, "check_staged_content_edits.py", "VS Code fast build staged content guard")
    require(wrapper, "'compile',", "VS Code fast build compiles through Meson")
    require(wrapper, "'--install-dir',", "VS Code fast build stages .install incrementally")

    stager = read("tools/build/stage_fast_install.py")
    require(stager, "copy_file_if_changed", "fast install copy-if-changed behavior")
    require(
        stager,
        "from windows_runtime import cleanup_windows_stage_target, is_windows_host",
        "shared Windows stage hygiene helper",
    )
    require(
        stager,
        "cleanup_windows_stage_target(install_dir)",
        "fast-build Windows stale-runtime cleanup",
    )
    require(stager, '"renderer-gl_*.dll"', "fast install renderer staging")
    require(stager, '"--temporary-runtime"', "isolated compatibility runtime staging")
    require(stager, 'source_root / ".tmp" / "stock-runtime"', "temporary runtime containment")
    require(stager, "*OPENQ4_PACK_NAMES,", "fast install stages every table pack")
    reject(stager, '"*.lib",\n    "pak0.pk4"', "fast install must not copy linker artifacts as runtime content")

    windows_runtime = read("tools/build/windows_runtime.py")
    require(
        windows_runtime,
        "WINDOWS_STALE_STAGE_FILE_MANIFEST",
        "narrow Windows stale-runtime cleanup manifest",
    )
    require(
        windows_runtime,
        '"baseoq4/skins"',
        "known empty Windows stage directory manifest",
    )
    require(
        windows_runtime,
        "cleanup_results[str(target)] = cleanup_windows_stage_target(target)",
        "full-install Windows stale-runtime cleanup",
    )


def set_values(args: list[object], key: str) -> list[str]:
    return [
        str(args[index + 2])
        for index, token in enumerate(args[:-2])
        if token in ("+set", "+seta") and args[index + 1] == key
    ]


def validate_launch_configs() -> None:
    launch = json.loads(read(".vscode/launch.json"))
    mp_configs = []
    previous = ("", 0)
    for config in launch.get("configurations", []):
        name = str(config.get("name", ""))
        args = config.get("args", [])
        if "preLaunchTask" in config:
            raise AssertionError(f"Launch config {name!r} must not define preLaunchTask")
        # r_renderApi is archived, so each launch pins its own renderer and says
        # which one it is, rather than running whatever .home last saved.
        render_api = set_values(args, "r_renderApi")
        suffix = {"gl": " \u2014 GL", "vulkan": " \u2014 Vulkan"}.get(render_api[0]) if len(render_api) == 1 else None
        if suffix is None or not name.endswith(suffix):
            raise AssertionError(f"Launch config {name!r} must set r_renderApi exactly once and end with its renderer")
        # Launches run the retained (RmlUi) interface whatever the archived
        # ui_retained holds; one that compares against the stock GUIs says so.
        retained = "0" if " - Stock UI " in name else "1"
        if set_values(args, "ui_retained") != [retained]:
            raise AssertionError(f"Launch config {name!r} must set ui_retained exactly once to {retained}")
        if set_values(args, "fs_basepath"):
            raise AssertionError(f"Launch config {name!r} must leave fs_basepath to the engine's install discovery")
        # Groups put separators in the Run and Debug list, which VS Code sorts by
        # group name and then order; the file lists them the same way.
        presentation = config.get("presentation")
        group = presentation.get("group") if isinstance(presentation, dict) else None
        order = presentation.get("order") if isinstance(presentation, dict) else None
        if not isinstance(group, str) or not group or not isinstance(order, int):
            raise AssertionError(f"Launch config {name!r} must have a presentation group and order")
        if (group, order) <= previous:
            raise AssertionError(f"Launch config {name!r} is listed out of its Run and Debug order")
        previous = (group, order)
        if "(MP)" in name:
            mp_configs.append(config)
            # The interactive configurations start at the join screen, which is the
            # shipped default; the automated MP profiles below still pin it to 1.
            if set_values(args, "ui_autoJoin") != ["0"]:
                raise AssertionError(f"MP launch config {name!r} must set ui_autoJoin exactly once to 0")
    if not mp_configs:
        raise AssertionError("Expected at least one VS Code MP launch configuration")

    sys.path.insert(0, str(ROOT / "tools" / "debug"))
    import generate_vscode_launch

    if launch != generate_vscode_launch.build():
        raise AssertionError(
            ".vscode/launch.json is generated; edit tools/debug/generate_vscode_launch.py and rerun it"
        )


def validate_codex_actions() -> None:
    # The actions once ran entries by position, and regrouping launch.json moved
    # them all onto other entries. Each now names its entry, which has to exist.
    entries = {
        "task": [task.get("label") for task in json.loads(read(".vscode/tasks.json"))["tasks"]],
        "launch": [config.get("name") for config in json.loads(read(".vscode/launch.json"))["configurations"]],
    }

    def resolve(command: object, context: str) -> tuple[str, str]:
        match = CODEX_COMMAND.fullmatch(command) if isinstance(command, str) else None
        if match is None:
            raise AssertionError(f"{context} must run one quoted entry through {CODEX_RUNNER}, not {command!r}")
        kind, entry = match.groups()
        if entry.isdigit():
            raise AssertionError(f"{context} must name its {kind} rather than give its position")
        if entries[kind].count(entry) != 1:
            raise AssertionError(f"{context} runs {kind} {entry!r}, which .vscode does not define exactly once")
        return kind, entry

    environment = tomllib.loads(read(".codex/environments/openq4.toml"))
    if resolve(environment["setup"]["win32"]["script"], "Codex setup script")[0] != "task":
        raise AssertionError("Codex setup script must run a task")
    names = []
    for action in environment.get("actions", []):
        name = action.get("name")
        context = f"Codex action {name!r}"
        if resolve(action.get("command"), context)[1] != name:
            raise AssertionError(f"{context} must run the entry it is named after")
        # The commands are Windows command lines. The Codex app reads platform and
        # drops any other key, such as platforms, then offers the action on every OS.
        if action.get("platform") != "win32":
            raise AssertionError(f'{context} must set platform = "win32"')
        names.append(name)
    if not names or len(set(names)) != len(names):
        raise AssertionError("Codex actions must exist and have distinct names")

    # Windows PowerShell reads a file without a byte order mark as ANSI, so the
    # runner has to stay ASCII and read launch.json as UTF-8 for an em dash to match.
    runner = read(CODEX_RUNNER)
    if not runner.isascii():
        raise AssertionError(f"{CODEX_RUNNER} must stay ASCII")
    require(runner, "Get-Content -LiteralPath $Path -Raw -Encoding UTF8", "Codex runner JSON reads")


def validate_mp_autojoin_policy() -> None:
    listen_script = read("tools/debug/start_listen_server_client.ps1")
    if listen_script.count('"+set", "ui_autoJoin", "1"') != 2:
        raise AssertionError("MP listen-server helper must enable auto-join for host and client")

    renderdoc_script = read("tools/debug/renderdoc_capture.ps1")
    if renderdoc_script.count('"+set", "ui_autoJoin", "1"') != 1:
        raise AssertionError("MP RenderDoc helper must enable auto-join for its listen host")

    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    for target in ("server_args", "client_args"):
        require(
            benchmark,
            f'append_set({target}, "ui_autoJoin", "1")',
            f"MP renderer benchmark {target}",
        )

    baseline = read("tools/validation/stock_asset_baseline.py")
    require(
        baseline,
        'args[restart_index:restart_index] = ("+set", "ui_autoJoin", "1")',
        "stock baseline MP roles",
    )
    require(
        baseline,
        'launch_contract["ui_autoJoin"] = "1"',
        "recorded stock baseline MP contract",
    )

    guide = read("AGENTS.md")
    require(
        guide,
        "keep an explicit `+set ui_autoJoin 1`",
        "agent MP test policy",
    )
    require(
        guide,
        "pass `+set ui_autoJoin 0`",
        "interactive join-screen launch policy",
    )


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    for haystack, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "vscode_fast_build.py", context)


def main() -> None:
    validate_tasks()
    validate_wrapper()
    validate_launch_configs()
    validate_codex_actions()
    validate_mp_autojoin_policy()
    validate_validation_coverage()
    print("vscode_fast_build: ok")


if __name__ == "__main__":
    main()
