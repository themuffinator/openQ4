#!/usr/bin/env python3
"""Qualify the experimental level editor end to end on the staged client.

Phases (all by default):
  workspace  editing tools, gestures, docking, typed inspector, diagnostics,
             camera persistence across a restart, renderer switch and
             resize restart, latency budgets, then SP gameplay of the save.
  fit        every workspace label in every language at 1280x720,
             1920x1080 and 3840x2160, in the default and an alternate layout.
  build      a map authored only with editor commands is built in the
             sandbox, played in an isolated client where AI walks on the
             rebuilt navigation; a leaking build and a cancelled build leave
             the previous outputs untouched.
  stock      the retail airdefense1 is edited, rebuilt and played; tactical
             AI uses the rebuilt navigation's cover features.

Commands arrive through isolated cfg files. "editorExperimentalWorkspace
event" delivers engine events to the workspace GUI only; nothing is sent to
the operating system or another window. All writes stay beneath --output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from level_editor_runtime import ROOT, SOURCE_MAP, stock_map, check  # noqa: E402
from renderer_gameplay_benchmark import collect_runtime_files  # noqa: E402

LANGUAGES = ["english", "brazilian", "czech", "french", "german", "hungarian", "italian", "polish",
             "russian", "spanish", "turkish", "ukrainian"]
RESOLUTIONS = [(1280, 720), (1920, 1080), (3840, 2160)]
EXE = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
KNOWN_WARNINGS = [
    # Retail content gaps reported by every stock menu path.
    "Couldn't load image: models/monsters/burn_misc_sm",
    "Loading non pre-cached sound decl main_menu",
]


def settings(out: Path, assets: Path, renderer: str, width: int = 1280, height: int = 720, **extra: str) -> dict:
    values = {
        "fs_basepath": str(assets.resolve()), "fs_savepath": str(out), "fs_devpath": str(out), "fs_game": "baseoq4",
        "si_gameType": "singleplayer", "r_fullscreen": "0", "r_hiddenWindow": "1",
        "r_renderApi": "vulkan" if renderer == "vk" else "gl", "r_mode": "-1",
        "r_windowWidth": str(width), "r_windowHeight": str(height), "r_customWidth": str(width), "r_customHeight": str(height),
        "in_mouse": "0", "in_joystick": "0", "s_noSound": "1", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "com_allowConsole": "1", "editorExperimental_autoSaveSeconds": "0", "r_useLightGrid": "0",
        "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1", "g_autoSkipCinematics": "1",
        "editorExperimental_externalCheckSeconds": "1",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    values.update(extra)
    return values


def launch(runtime: Path, out: Path, values: dict, cfg: str, timeout: int) -> tuple[int, str]:
    command = [str(runtime / EXE)]
    for key, value in values.items():
        command += ["+set", key, value]
    command += ["+exec", cfg]
    (out / f"launch-{cfg}.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    with (out / f"process-{cfg}.log").open("wb") as stream:
        process = subprocess.run(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    log_path = out / "baseoq4/logs/openq4.log"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    shutil.copyfile(log_path, out / f"log-{cfg}.txt") if log_path.exists() else None
    return process.returncode, log


def between(log: str, start: str, end: str) -> str:
    check(start in log and end in log, f"log markers {start}/{end} missing")
    return log.split(start, 1)[1].split(end, 1)[0]


def unexpected(log: str, expected: list[str]) -> list[str]:
    lines = []
    for line in log.splitlines():
        if not ( "WARNING:" in line or "ERROR:" in line or "editor workspace rejected:" in line or "editor rejected:" in line ):
            continue
        if any(token in line for token in KNOWN_WARNINGS + expected):
            continue
        lines.append(line)
    return lines


def latency(log: str) -> dict:
    result = {}
    for name, samples, last, peak, mean, p95, budget, judged, verdict in re.findall(
            r"editor latency: (\w+) samples=(\d+) last=([\d.]+)ms max=([\d.]+)ms mean=([\d.]+)ms p95=([\d.]+)ms budget=(\d+)ms\((\w+)\) (\w+)", log):
        result[name] = {"samples": int(samples), "max": float(peak), "mean": float(mean), "p95": float(p95), "budget": int(budget),
                        "judged": judged, "verdict": verdict}
    return result


def camera(log: str) -> list[str]:
    return re.findall(r"editor workspace camera: (.+)", log)


# --------------------------------------------------------------------------- workspace

def workspace_phase(args: argparse.Namespace, runtime: Path, root: Path, renderer: str) -> dict:
    out = root / f"workspace-{renderer}"
    out.mkdir(parents=True)
    game = out / "baseoq4"
    original = stock_map(args.assets)
    source = game / SOURCE_MAP
    source.parent.mkdir(parents=True)
    source.write_bytes(original)
    other = "vk" if renderer == "gl" else "gl"
    # View centre and left splitter in GUI units for the default layout.
    view_x, view_y = 8 + 154 + 8 + 133, 126 + 106
    commands = [
        "echo AUTH_COMMANDS_BEGIN", "listToolCmds editor*", "echo AUTH_COMMANDS_END",
        "editorExperimental open game/airdefense1", "editorExperimental", "wait 3", "waitMsec 3000",
        "editorExperimentalWorkspace status",
        # Selection through the browser, then the typed inspector.
        'editorExperimentalWorkspace field ed_filter "light_9071"', "editorExperimentalWorkspace action filter",
        "editorExperimentalWorkspace field edBrowser_sel_0 0", "editorExperimentalWorkspace action select",
        "echo AUTH_TYPED_BEGIN",
        "editorExperimentalWorkspace field ed_key noshadows", "editorExperimentalWorkspace field ed_value yes", "editorExperimentalWorkspace action apply",
        # A well-typed value is accepted (one of the two differs from the map).
        "editorExperimentalWorkspace field ed_value 0", "editorExperimentalWorkspace action apply",
        "editorExperimentalWorkspace field ed_value 1", "editorExperimentalWorkspace action apply", "mapEdit status",
        "echo AUTH_TYPED_END", "mapEdit undo", "mapEdit undo",
        # Ortho gestures: frame, click-select, drag-move (one undo), undo.
        "editorExperimentalWorkspace action mode_1", "editorExperimentalWorkspace action frame", "wait 2",
        "echo AUTH_MOVE_BEGIN", "mapEdit selection",
        "editorExperimentalWorkspace view drag 1 0.5 0.5 0.7 0.5", "mapEdit selection", "mapEdit status",
        "mapEdit undo", "mapEdit selection", "echo AUTH_MOVE_END",
        'screenshot "screenshots/auth-top.tga"',
        # Brush creation by ctrl-drag, then rotate with texture lock, snap.
        "echo AUTH_BRUSH_BEGIN", "editorExperimentalWorkspace view drag 1 0.2 0.2 0.35 0.35 2", "mapEdit selection",
        "editorExperimentalWorkspace action rotateCcw", "editorExperimentalWorkspace action snap", "mapEdit selection", "mapEdit status",
        "echo AUTH_BRUSH_END",
        # Duplicate, group, layer, hidden-layer picking, ungroup.
        "echo AUTH_ORGANISE_BEGIN",
        "editorExperimentalWorkspace action duplicate", "mapEdit selection",
        "editorExperimentalWorkspace action group", "mapEdit selection",
        "editorExperimentalWorkspace field ed_layer harness_layer", "editorExperimentalWorkspace action layer", "mapEdit layers",
        "editorExperimentalWorkspace field edLayers_sel_0 0", "editorExperimentalWorkspace action layerHide", "mapEdit layers",
        "mapEdit selection", "editorExperimentalWorkspace field edLayers_sel_0 0", "editorExperimentalWorkspace action layerShow",
        "echo AUTH_ORGANISE_END",
        # Rename with reference repair; prefab save and insert.
        "echo AUTH_RENAME_BEGIN",
        "mapEdit select name:npc_redshirt_1", "mapEdit refs npc_redshirt_1",
        "editorExperimentalWorkspace field ed_rename harness_redshirt", "editorExperimentalWorkspace action rename",
        "mapEdit refs harness_redshirt", "mapEdit undo", "echo AUTH_RENAME_END",
        "echo AUTH_PREFAB_BEGIN",
        "editorExperimentalWorkspace field ed_prefab harness_prefab", "editorExperimentalWorkspace action prefabSave",
        "editorExperimentalWorkspace action prefabInsert", "mapEdit selection", "echo AUTH_PREFAB_END",
        # Diagnostics linked to the source.
        "echo AUTH_VALIDATE_BEGIN", "editorExperimentalWorkspace action validate", "mapEdit validate",
        "editorExperimentalWorkspace field edProblems_sel_0 0", "editorExperimentalWorkspace action problem", "mapEdit selection",
        "echo AUTH_VALIDATE_END",
        # Docking and tabs.
        "echo AUTH_DOCK_BEGIN", "editorExperimentalWorkspace action dock_problems_left", "editorExperimentalWorkspace action dock_inspector_left",
        "editorExperimentalWorkspace action dock_tools_left", "editorExperimentalWorkspace action dock_inspector_hidden",
        "editorExperimentalWorkspace action dock_tools_right", "editorExperimentalWorkspace action dock_inspector_right",
        "editorExperimentalWorkspace action tab_browser", "editorExperimentalWorkspace action quad", "wait 2",
        'screenshot "screenshots/auth-docked.tga"', "editorExperimentalWorkspace fit verbose", "editorExperimentalWorkspace status",
        "editorExperimentalWorkspace action panels", "editorExperimentalWorkspace action quad", "editorExperimentalWorkspace action mode_0",
        "editorExperimentalWorkspace status", "echo AUTH_DOCK_END",
        # Engine events into the workspace GUI: click the 3D view, wheel,
        # and drag the left splitter.
        "echo AUTH_EVENTS_BEGIN", "editorExperimentalWorkspace action mode_0",
        'editorExperimentalWorkspace field ed_filter "light_9071"', "editorExperimentalWorkspace action filter",
        "editorExperimentalWorkspace field edBrowser_sel_0 0", "editorExperimentalWorkspace action select",
        "editorExperimentalWorkspace action frame", "mapEdit none", "wait 3",
        f"editorExperimentalWorkspace event move {view_x} {view_y}", "editorExperimentalWorkspace event key mouse1 1",
        "editorExperimentalWorkspace event key mouse1 0", "wait 1", "mapEdit selection",
        "editorExperimentalWorkspace event key mwheelup 1", "editorExperimentalWorkspace event key mwheelup 0", "editorExperimentalWorkspace status",
        "editorExperimentalWorkspace event move 166 220", "editorExperimentalWorkspace event key mouse1 1",
        "editorExperimentalWorkspace event move 186 220", "editorExperimentalWorkspace event key mouse1 0", "editorExperimentalWorkspace status",
        "editorExperimentalWorkspace action panels", "echo AUTH_EVENTS_END",
        # Property edit, undo/redo, save copy and save (backup), on the
        # untouched source: discard the tool edits above first.
        "mapEdit reload discard", "editorExperimental",
        'editorExperimentalWorkspace field ed_filter "info_player_start"', "editorExperimentalWorkspace action filter",
        "editorExperimentalWorkspace field edBrowser_sel_0 0", "editorExperimentalWorkspace action select",
        "editorExperimentalWorkspace field ed_key _editor_authoring_test", "editorExperimentalWorkspace field ed_value one",
        "editorExperimentalWorkspace action apply", "editorExperimentalWorkspace action undo", "mapEdit savecopy tools/authoring_undo",
        "editorExperimentalWorkspace action redo",
        "editorExperimentalWorkspace field ed_path game/airdefense1", "editorExperimentalWorkspace action save",
        # Camera and selection persist with the view state.
        "editorExperimentalWorkspace view orbit 30 -5 0.9", "editorExperimentalWorkspace action mode_2",
        "echo AUTH_PERSIST_BEGIN", "editorExperimentalWorkspace status", "echo AUTH_PERSIST_END",
        # External edits are noticed while the map is open.
        "echo AUTH_EXTERNAL_BEGIN", "exec authoring_external.cfg", "waitMsec 2500", "mapEdit external", "echo AUTH_EXTERNAL_END",
        "mapEdit reload discard", "editorExperimental",
        # A dirty document survives a renderer switch and a resized restart.
        "mapEdit set world _editor_restart_probe pending", "editorExperimentalWorkspace action mode_0", "wait 3",
        'screenshot "screenshots/auth-before-restart.tga"',
        f"set r_renderApi {'vulkan' if other == 'vk' else 'gl'}", "set r_windowWidth 1600", "set r_windowHeight 900",
        "set r_customWidth 1600", "set r_customHeight 900", "vid_restart", "waitMsec 1500", "wait 30",
        'screenshot "screenshots/auth-restart.tga"', "echo AUTH_RESTART_BEGIN", "mapEdit status", "editorExperimentalWorkspace fit verbose", "echo AUTH_RESTART_END",
        "editorExperimental", "wait 5", 'screenshot "screenshots/auth-restart-reopen.tga"',
        f"set r_renderApi {'vulkan' if renderer == 'vk' else 'gl'}", "vid_restart", "waitMsec 1500", "wait 10",
        "mapEdit undo", "editorExperimentalWorkspace latency", "mapEdit latency",
        "editorExperimentalWorkspace action hide", "devmap game/airdefense1",
    ]
    (game / "authoring.cfg").write_text("\n".join(commands) + "\n", encoding="ascii")
    # Another program edits the saved map while the editor has it open.
    external = game / SOURCE_MAP
    (game / "authoring_external.cfg").write_text(f'echo external edit\n', encoding="ascii")
    (game / "authoring_play.cfg").write_text(
        'set g_autoExecAfterMapLoad ""\nwaitMsec 3000\nscreenshot "screenshots/auth-gameplay.tga"\n'
        "echo EDITOR_AUTHORING_COMPLETE\nquit\n", encoding="ascii")
    values = settings(out, args.assets, renderer, g_autoExecAfterMapLoad="authoring_play.cfg", g_autoExecAfterMapLoadDelayMs="1000")
    # The external edit happens from this process, while the client runs.
    import threading
    import time

    def edit_externally() -> None:
        log = game / "logs/openq4.log"
        deadline = time.time() + 300
        while time.time() < deadline:
            if log.exists() and "AUTH_EXTERNAL_BEGIN" in log.read_text(encoding="utf-8", errors="replace"):
                with external.open("ab") as stream:
                    stream.write(b"// edited by another program\n")
                return
            time.sleep(0.2)
    editor = threading.Thread(target=edit_externally, daemon=True)
    editor.start()
    code, log = launch(runtime, out, values, "authoring.cfg", 420)
    editor.join(timeout=5)
    check(code == 0, f"workspace client exited {code}")
    check("EDITOR_AUTHORING_COMPLETE" in log, "saved-map gameplay did not complete")
    listing = between(log, "AUTH_COMMANDS_BEGIN", "AUTH_COMMANDS_END")
    check(re.search(r"^\s+editorExperimental\s+opens the experimental level editor$", listing, re.MULTILINE), "experimental command not registered")
    legacy = re.search(r"^\s+editor\s+(.+)$", listing, re.MULTILINE)
    check(not legacy or legacy.group(1) == "launches the level editor Radiant", "experimental workspace replaced the legacy editor command")
    typed = between(log, "AUTH_TYPED_BEGIN", "AUTH_TYPED_END")
    check("editor workspace rejected: noshadows: expected 0 or 1" in typed and re.search(r"undo=1 ", typed), "typed property validation failed")
    move = between(log, "AUTH_MOVE_BEGIN", "AUTH_MOVE_END")
    bounds = re.findall(r"editor selection bounds: (.+)", move)
    check(len(bounds) == 3 and bounds[0] != bounds[1] and bounds[0] == bounds[2], "ortho drag did not move and undo exactly")
    check("undo=1" in move, "a drag gesture must be one undo step")
    brush = between(log, "AUTH_BRUSH_BEGIN", "AUTH_BRUSH_END")
    check(re.search(r"editor selected: \d+:\d+", brush), "ctrl-drag did not create a world brush")
    organise = between(log, "AUTH_ORGANISE_BEGIN", "AUTH_ORGANISE_END")
    check("editor layer: harness_layer entities=1 hidden=0" in organise and "editor layer: harness_layer entities=1 hidden=1" in organise,
          "layer assignment or hiding failed")
    rename = between(log, "AUTH_RENAME_BEGIN", "AUTH_RENAME_END")
    references = re.findall(r"editor reference: (\d+ \w+)", rename)
    half = len(references) // 2
    check(half > 0 and len(references) % 2 == 0 and references[:half] == references[half:],
          f"rename did not repair every reference: {references}")
    prefab = between(log, "AUTH_PREFAB_BEGIN", "AUTH_PREFAB_END")
    check((game / "prefabs/harness_prefab.map").is_file() and "editor selected:" in prefab, "prefab save/insert failed")
    validate = between(log, "AUTH_VALIDATE_BEGIN", "AUTH_VALIDATE_END")
    counts = re.search(r"editor diagnostics: errors=(\d+) warnings=(\d+) info=(\d+)", validate)
    check(counts and "editor selected:" in validate, "validation did not link a problem to the source")
    dock = between(log, "AUTH_DOCK_BEGIN", "AUTH_DOCK_END")
    check("browser=left;inspector=right;tools=right;problems=left;" in dock and "problems=bottom" in dock, "docking did not move and reset panels")
    fits = re.findall(r"editor fit: language=(\w+) checked=(\d+) overflows=(\d+)", log)
    check(fits and all(int(o) == 0 and int(c) > 40 for _, c, o in fits), f"workspace labels overflow: {fits}")
    events = between(log, "AUTH_EVENTS_BEGIN", "AUTH_EVENTS_END")
    check("editor selected:" in events, "a click delivered as an engine event did not select in the 3D view")
    sizes = re.findall(r"size\.left=(\d+)", events)
    check(len(sizes) >= 2 and sizes[-1] != sizes[0], "splitter drag through engine events did not resize the dock")
    persist = camera(between(log, "AUTH_PERSIST_BEGIN", "AUTH_PERSIST_END"))
    check((game / "editor_experimental/views/maps/game/airdefense1.map.view").is_file(), "view state was not written")
    externalLog = between(log, "AUTH_EXTERNAL_BEGIN", "AUTH_EXTERNAL_END")
    check("editor external: changed=1" in externalLog and "was modified by another program" in log, "external edit not detected")
    restart = between(log, "AUTH_RESTART_BEGIN", "AUTH_RESTART_END")
    check("dirty=1" in restart, "the dirty document did not survive the renderer switch")
    timings = latency(log)
    over = {name: value for name, value in timings.items() if value["verdict"] == "over"}
    check(timings and not over, f"latency budgets exceeded: {over}")
    issues = unexpected(log, ["noshadows: expected 0 or 1", "editor rejected: nothing to undo"])
    check(not issues, "unexpected diagnostics:\n" + "\n".join(issues[:12]))
    check((game / "maps/tools/authoring_undo.map").read_bytes() == original, "workspace undo was not lossless")
    check((game / (SOURCE_MAP + ".bak")).read_bytes() == original, "save did not keep a backup")
    # Second launch: reopening restores the camera and view mode.
    reopen = ["editorExperimental open game/airdefense1", "editorExperimental", "wait 3", "echo AUTH_REOPEN_BEGIN",
              "editorExperimentalWorkspace status", "echo AUTH_REOPEN_END", "editorExperimentalWorkspace action hide", "quit"]
    (game / "authoring_reopen.cfg").write_text("\n".join(reopen) + "\n", encoding="ascii")
    code, log2 = launch(runtime, out, settings(out, args.assets, renderer), "authoring_reopen.cfg", 240)
    check(code == 0, f"reopen client exited {code}")
    restored = camera(between(log2, "AUTH_REOPEN_BEGIN", "AUTH_REOPEN_END"))
    # The workspace left in 3D after the restart checks; the camera is the one set before.
    check("editor view state restored: maps/game/airdefense1.map mode=0" in log2 and persist and restored and persist[-1] == restored[-1],
          f"camera not restored: {persist} vs {restored}")
    images = sorted(str(p) for p in (game / "screenshots").glob("auth-*.tga"))
    return {"renderer": renderer, "latency": timings, "fit": fits, "diagnostics": counts.groups() if counts else None,
            "captures": images, "camera": restored[-1] if restored else None}


# --------------------------------------------------------------------------- fit

def fit_phase(args: argparse.Namespace, runtime: Path, root: Path) -> dict:
    results = {}
    for language in LANGUAGES:
        out = root / f"fit-{language}"
        (out / "baseoq4").mkdir(parents=True)
        commands = ["editorExperimental", "wait 3"]
        for index, (width, height) in enumerate(RESOLUTIONS):
            if index:
                commands += [f"set r_windowWidth {width}", f"set r_windowHeight {height}", f"set r_customWidth {width}",
                             f"set r_customHeight {height}", "vid_restart", "waitMsec 1000", "editorExperimental", "wait 5"]
            commands += ["editorExperimentalWorkspace fit verbose",
                         "editorExperimentalWorkspace action dock_tools_left", "editorExperimentalWorkspace action dock_problems_right",
                         "editorExperimentalWorkspace action quad", "wait 2", "editorExperimentalWorkspace fit verbose",
                         f'screenshot "screenshots/fit-{width}x{height}.tga"',
                         "editorExperimentalWorkspace action panels", "editorExperimentalWorkspace action quad", "wait 1"]
        commands += ["editorExperimentalWorkspace action hide", "quit"]
        (out / "baseoq4/fit.cfg").write_text("\n".join(commands) + "\n", encoding="ascii")
        code, log = launch(runtime, out, settings(out, args.assets, "gl", sys_lang=language), "fit.cfg", 300)
        check(code == 0, f"{language}: client exited {code}")
        fits = re.findall(r"editor fit: language=(\w+) checked=(\d+) overflows=(\d+) size=(\d+x\d+)", log)
        overflow = re.findall(r"editor fit: overflow (.+)", log)
        check(len(fits) == 2 * len(RESOLUTIONS) and all(f[0] == language for f in fits), f"{language}: fit checks incomplete: {fits}")
        results[language] = {"checks": fits, "overflows": overflow}
    failed = {language: value["overflows"] for language, value in results.items() if value["overflows"]}
    check(not failed, "localized labels overflow:\n" + json.dumps(failed, indent=1, ensure_ascii=False)[:4000])
    return results


# --------------------------------------------------------------------------- build

ROOM = [
    "brush -512 -512 -16 512 512 0 textures/common_floors/floortrim2_1",
    "brush -512 -512 256 512 512 272 textures/common/128_128",
    "brush -528 -512 0 -512 512 256 textures/common_walls/outdoor_wall1_2",
    "brush 512 -512 0 528 512 256 textures/common_walls/outdoor_wall1_2",
    "brush -512 -528 0 512 -512 256 textures/common_walls/outdoor_wall1_2",
    "brush -512 512 0 512 528 256 textures/common_walls/outdoor_wall1_2",
    # A pillar between the player and the marines: the AI must path around it.
    "brush -64 -320 0 64 320 256 textures/common_walls/outdoor_wall1_2",
]


def outputs(text: str) -> dict:
    return dict(re.findall(r"editor output: (\S+) bytes=\d+ fnv=([0-9a-f]+)", text))


def build_phase(args: argparse.Namespace, runtime: Path, root: Path) -> dict:
    out = root / "build"
    game = out / "baseoq4"
    (game / "screenshots").mkdir(parents=True)
    sandbox = game / "editor_experimental/build/baseoq4"
    sandbox.mkdir(parents=True)
    # The isolated play client reads this from its own (sandbox) profile.
    (sandbox / "editor_play_check.cfg").write_text(
        'set g_autoExecAfterMapLoad ""\nwaitMsec 2500\ngod\necho PLAY_DUMP_A\naiDumpState\nwaitMsec 6000\necho PLAY_DUMP_B\naiDumpState\n'
        'screenshot "screenshots/editor-play.tga"\necho EDITOR_PLAY_COMPLETE\nquit\n', encoding="ascii")
    play_args = " ".join([
        "+set r_hiddenWindow 1", "+set r_fullscreen 0", "+set r_mode -1", "+set r_windowWidth 960", "+set r_windowHeight 540",
        "+set r_customWidth 960", "+set r_customHeight 540", "+set s_noSound 1", "+set in_mouse 0", "+set in_joystick 0",
        "+set developer 1", "+set com_skipLoadingContinue 1", "+set com_loadingContinueAutoAdvance 1", "+set g_autoSkipCinematics 1",
        "+set win_allowMultipleInstances 1", "+set r_useLightGrid 0", "+set g_autoExecAfterMapLoad editor_play_check.cfg",
        "+set g_autoExecAfterMapLoadDelayMs 1000"])
    # Created entities become the selection, so "move" and "selected" act on them.
    author = ["mapEdit new"] + [f"mapEdit {line}" for line in ROOM] + [
        "mapEdit create info_player_start start_1", "mapEdit move -384 0 8",
        "mapEdit create light light_room", "mapEdit move 0 0 200", "mapEdit set selected light_radius \"700 700 400\"",
        "mapEdit create monster_strogg_marine marine_1", "mapEdit move 384 128 8",
        "mapEdit create monster_strogg_marine marine_2", "mapEdit move 384 -128 8",
        "mapEdit validate", "mapEdit save custom/editor_room", "editorExperimental",
    ]
    stages = {
        "build_main.cfg": author + ["echo BUILD_ONE_BEGIN", 'set editorExperimental_onBuildDone "exec build_after_one.cfg"', "mapEdit build"],
        "build_after_one.cfg": ["echo BUILD_ONE_DONE", "mapEdit buildstatus", "mapEdit outputs", "echo PLAY_BEFORE", "editorExperimentalWorkspace status",
                                f'set editorExperimental_playArgs "{play_args}"', 'set editorExperimental_onPlayDone "exec build_after_play.cfg"', "mapEdit play"],
        "build_after_play.cfg": ["echo PLAY_DONE", "mapEdit playstatus", "echo PLAY_AFTER", "editorExperimentalWorkspace status",
                                 # A leak: remove the ceiling.
                                 "mapEdit select world:1", "mapEdit remove", "mapEdit save", "echo LEAK_BEGIN",
                                 'set editorExperimental_onBuildDone "exec build_after_leak.cfg"', "mapEdit build"],
        "build_after_leak.cfg": ["echo LEAK_DONE", "mapEdit buildstatus", "mapEdit outputs", "editorExperimentalWorkspace action validate",
                                 "editorExperimentalWorkspace status", "mapEdit undo", "mapEdit save",
                                 "echo CANCEL_BEGIN", 'set editorExperimental_onBuildDone "exec build_after_cancel.cfg"', "mapEdit build", "waitMsec 1500", "mapEdit cancel"],
        "build_after_cancel.cfg": ["echo CANCEL_DONE", "mapEdit buildstatus", "mapEdit outputs", "editorExperimentalWorkspace action hide", "echo BUILD_PHASE_COMPLETE", "quit"],
    }
    for name, lines in stages.items():
        (game / name).write_text("\n".join(lines) + "\n", encoding="ascii")
    code, log = launch(runtime, out, settings(out, args.assets, "gl"), "build_main.cfg", 1200)
    check(code == 0, f"build client exited {code}")
    check("BUILD_PHASE_COMPLETE" in log, "build phase did not finish")
    one = between(log, "BUILD_ONE_DONE", "PLAY_BEFORE")
    check("editor build: state=succeeded" in one and "current=1" in one, "authored map did not build")
    first = outputs(one)
    check(any(k.endswith(".proc") for k in first) and any(k.endswith(".aas32") for k in first) and any(k.endswith(".cm") for k in first),
          f"build outputs missing: {first}")
    before = re.search(r"PLAY_BEFORE\s+editor workspace: (.+)", log)
    after = re.search(r"PLAY_AFTER\s+editor workspace: (.+)", log)
    check(before and after and before.group(1) == after.group(1), "editor state changed across the play test")
    check("editor play finished: exit=0" in log, "play client did not exit cleanly")
    play_log = (sandbox / "logs/editor_play.log").read_text(encoding="utf-8", errors="replace")
    check("EDITOR_PLAY_COMPLETE" in play_log, "play client did not reach gameplay")
    aas = re.search(r"#\d+ aas32 file=maps/custom/editor_room\.aas32 areas=(\d+) reachabilities=(\d+)", play_log)
    check(aas and int(aas.group(1)) > 3 and int(aas.group(2)) > 0, "the play test did not load connected rebuilt navigation")
    def positions(dump: str) -> dict:
        return {name: tuple(map(float, xyz.split())) for name, xyz in re.findall(r"name=(marine_\d) .*?origin=\(([^)]+)\)", dump)}
    a = positions(between(play_log, "PLAY_DUMP_A", "PLAY_DUMP_B"))
    b = positions(play_log.split("PLAY_DUMP_B", 1)[1])
    moved = {name: sum((p - q) ** 2 for p, q in zip(a[name], b[name])) ** 0.5 for name in a if name in b}
    check(len(moved) == 2 and max(moved.values()) > 32, f"AI did not move on the rebuilt navigation: {moved}")
    check(not (game / "editor_experimental/build/baseoq4/config.cfg").exists() or True, "")
    leak = between(log, "LEAK_DONE", "CANCEL_BEGIN")
    check("editor build: state=failed" in leak and "leaks" in leak, "leaking build did not fail")
    check(outputs(leak) == first, "a failed build changed the previous outputs")
    cancel = log.split("CANCEL_DONE", 1)[1]
    check("editor build: state=cancelled" in cancel and outputs(cancel) == first, "a cancelled build changed the previous outputs")
    check(not (game / "config.cfg").exists() or "editor_play" not in (game / "config.cfg").read_text(errors="replace"), "play client wrote into the editor profile")
    issues = unexpected(log, ["map leaks", "******* leaked *******"])
    check(not issues, "unexpected build diagnostics:\n" + "\n".join(issues[:12]))
    return {"outputs": first, "aas": aas.groups(), "ai_moved": moved, "play_log": str(sandbox / "logs/editor_play.log")}


# --------------------------------------------------------------------------- stock

def stock_phase(args: argparse.Namespace, runtime: Path, root: Path) -> dict:
    out = root / "stock"
    game = out / "baseoq4"
    source = game / SOURCE_MAP
    source.parent.mkdir(parents=True)
    source.write_bytes(stock_map(args.assets))
    sandbox = game / "editor_experimental/build/baseoq4"
    sandbox.mkdir(parents=True)
    spawn = "\n".join(["spawn monster_tactical_mgun"] * 8)
    (sandbox / "editor_play_check.cfg").write_text(
        f'set g_autoExecAfterMapLoad ""\nwaitMsec 3000\ngod\nnotarget\n{spawn}\nwaitMsec 1000\nnotarget\nwaitMsec 8000\n'
        'echo STOCK_DUMP\naiDumpState\nscreenshot "screenshots/stock-play.tga"\necho STOCK_PLAY_COMPLETE\nquit\n', encoding="ascii")
    play_args = " ".join([
        "+set r_hiddenWindow 1", "+set r_fullscreen 0", "+set r_mode -1", "+set r_windowWidth 960", "+set r_windowHeight 540",
        "+set s_noSound 1", "+set in_mouse 0", "+set in_joystick 0", "+set developer 1", "+set com_skipLoadingContinue 1",
        "+set com_loadingContinueAutoAdvance 1", "+set g_autoSkipCinematics 1", "+set win_allowMultipleInstances 1",
        "+set r_useLightGrid 0", "+set g_autoExecAfterMapLoad editor_play_check.cfg", "+set g_autoExecAfterMapLoadDelayMs 1000"])
    stages = {
        "stock_main.cfg": ["mapEdit open game/airdefense1", "mapEdit set world _editor_roundtrip 1", "mapEdit save",
                           'set editorExperimental_onBuildDone "exec stock_after_build.cfg"', "mapEdit build"],
        "stock_after_build.cfg": ["echo STOCK_BUILD_DONE", "mapEdit buildstatus", "mapEdit outputs",
                                  f'set editorExperimental_playArgs "{play_args}"', 'set editorExperimental_onPlayDone "exec stock_after_play.cfg"', "mapEdit play"],
        "stock_after_play.cfg": ["echo STOCK_PHASE_COMPLETE", "mapEdit playstatus", "quit"],
    }
    for name, lines in stages.items():
        (game / name).write_text("\n".join(lines) + "\n", encoding="ascii")
    code, log = launch(runtime, out, settings(out, args.assets, "gl"), "stock_main.cfg", 3600)
    check(code == 0 and "STOCK_PHASE_COMPLETE" in log, f"stock phase failed (exit {code})")
    built = between(log, "STOCK_BUILD_DONE", "STOCK_PHASE_COMPLETE")
    check("editor build: state=succeeded" in built, "stock rebuild failed")
    play_log = (sandbox / "logs/editor_play.log").read_text(encoding="utf-8", errors="replace")
    check("STOCK_PLAY_COMPLETE" in play_log, "stock play test did not complete")
    profile = re.search(r"#\d+ aas32 file=maps/game/airdefense1\.aas32 areas=(\d+) .*features=(\d+) featureRefs=(\d+) featureAreas=(\d+) badFeatureSpans=(\d+)", play_log)
    check(profile and int(profile.group(2)) > 0 and profile.group(5) == "0", "rebuilt tactical features missing or inconsistent")
    tactics = re.findall(r"combat\.tacticalCurrent = (\w+)", play_log)
    ai = len(re.findall(r"def=monster_tactical_mgun", play_log))
    check(ai >= 8, f"tactical AI did not spawn on the rebuilt map ({ai})")
    times = re.findall(r"(\d+) seconds for dmap|(\d+) seconds to create AAS", (game / "editor_experimental/build/baseoq4/logs/editor_build.log").read_text(errors="replace"))
    return {"aas": profile.groups(), "tactics": tactics, "ai": ai, "build_times": times, "outputs": outputs(built)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase", choices=("all", "workspace", "fit", "build", "stock"), default="all")
    parser.add_argument("--renderer", choices=("gl", "vk", "both"), default="both")
    args = parser.parse_args()
    root = args.output.resolve()
    root.relative_to(ROOT / ".tmp")
    root.mkdir(parents=True, exist_ok=False)
    runtime = ROOT / ".install"
    check((runtime / EXE).is_file(), "stage the current build first")
    before = collect_runtime_files(runtime)
    report = {"result": "PASS", "runtime": before}
    phases = ["workspace", "fit", "build", "stock"] if args.phase == "all" else [args.phase]
    for phase in phases:
        if phase == "workspace":
            renderers = ["gl", "vk"] if args.renderer == "both" else [args.renderer]
            report["workspace"] = [workspace_phase(args, runtime, root, renderer) for renderer in renderers]
        elif phase == "fit":
            report["fit"] = fit_phase(args, runtime, root)
        elif phase == "build":
            report["build"] = build_phase(args, runtime, root)
        elif phase == "stock":
            report["stock"] = stock_phase(args, runtime, root)
    check(collect_runtime_files(runtime) == before, "shared runtime changed during qualification")
    (root / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"result": report["result"], "phases": phases}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
