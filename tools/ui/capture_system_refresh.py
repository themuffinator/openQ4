#!/usr/bin/env python3
"""Qualify external SYSTEM readback in windowed SP/MP gameplay.

Uses the normal Session page and semantic diagnostics, never OS/device input.
Capture output stays below .tmp; images come from the engine screenshot command.
This bounded regression does not accept the complete settings or UI product.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

import capture_system_page as page

ROOT = page.ROOT
MARKER = "SYSTEM_REFRESH_STAGE"
COMPLETE = "SYSTEM_REFRESH_COMPLETE"
FIELDS = ("open", "dirty", "canApply", "draftPreset", "baselinePreset",
          "draftBrightness", "baselineBrightness", "message", "numberDraftsPending")
NUMBER = "settings_brightness_number"
# The page opens on GRAPHICS; the Number answers only on its IMAGE QUALITY tab,
# which a pending typed number would then hold, so switch before typing.
SHOW_NUMBER = page.show_tab(page.TABS["settings_brightness"])


def stages():
    def row(name, commands, preset, brightness=1, *, baseline=None, dirty=0,
            message="#str_230007", local=0, conflict=None):
        return dict(name=name, commands=commands, conflict=conflict,
                    values=(1, dirty, 0, preset, preset, brightness,
                            brightness if baseline is None else baseline, message, local))
    return [
        row("open", ["applyPerformancePreset balanced", "set r_brightness 1", "openq4_system open"], "balanced"),
        row("minimum", ["applyPerformancePreset minimum"], "minimum"),
        row("restart", ["vid_restart"], "minimum"),
        row("ultra", ["applyPerformancePreset ultra"], "ultra"),
        row("conflict", [*SHOW_NUMBER, f"openq4_retainedGui focus {NUMBER}",
                         f"openq4_retainedGui number begin {NUMBER}",
                         f"openq4_retainedGui number select {NUMBER} 0 1",
                         f'openq4_retainedGui number replace {NUMBER} "1.1"',
                         f"openq4_retainedGui number commit {NUMBER}", "wait 6",
                         "applyPerformancePreset balanced"], "ultra", 1.1,
            baseline=1, dirty=1, message="#str_230011"),
        row("reopened", ["openq4_retainedGui event onBack", "wait 3",
                         "openq4_retainedGui event discard", "wait 6", "openq4_system open"], "balanced"),
        row("local", [*SHOW_NUMBER, f"openq4_retainedGui focus {NUMBER}",
                      f"openq4_retainedGui number begin {NUMBER}",
                      f"openq4_retainedGui number select {NUMBER} 0 1",
                      f'openq4_retainedGui number replace {NUMBER} "-"',
                      "set r_brightness 1.5"], "balanced", 1.5, local=1, conflict=1),
        row("resolved", [f"openq4_retainedGui number reload {NUMBER}"], "balanced", 1.5, conflict=0),
        row("final", ["reloadLanguage", "vid_restart"], "balanced", 1.5),
    ]


def script():
    lines = ["openq4_assertMenuActivation 10000", "wait 3"]
    for stage in stages():
        lines += [f'echo {MARKER} {stage["name"]}', *stage["commands"], "wait 8", "openq4_system report"]
        lines += [f'openq4_guiGet "{field}"' for field in FIELDS]
        lines += [f"openq4_retainedGui widget {NUMBER}", "openq4_retainedGui report",
                  f'screenshot "screenshots/refresh-{stage["name"]}.tga"']
    lines += [f"echo {COMPLETE}", "quit"]
    return "\n".join(lines) + "\n"


def qualify(log, mode):
    failures = []
    rows = stages()
    parts = re.split(rf"^{MARKER} (\w+)\s*$", log, flags=re.M)
    names = parts[1::2]
    if names != [row["name"] for row in rows]:
        failures.append("missing, duplicated or reordered stages")
    if COMPLETE not in log or "OPENQ4_MENU_ACTIVATION PASS" not in log:
        failures.append("missing gameplay/menu completion")
    for stage, name, block in zip(rows, names, parts[2::2]):
        wanted_route = (f"OPENQ4_SYSTEM enabled=1 active={page.PAGE} parent={page.PARENT} "
                        f"child=1 guiTest=0 menu=1 map=1 multiplayer={int(mode == 'mp')}")
        if wanted_route not in block:
            failures.append(name + ": not the normal in-game SYSTEM route")
        actual = re.findall(r"^GUI_VALUE ([^=]+)=(.*)$", block, re.M)
        if len(actual) != len(FIELDS):
            failures.append(name + ": incomplete readback")
            continue
        for (field, value), expected_field, expected in zip(actual, FIELDS, stage["values"]):
            if isinstance(expected, (int, float)):
                try:
                    matches = abs(float(value) - expected) < 0.00001
                except ValueError:
                    matches = False
            else:
                matches = value == expected
            if field != expected_field or not matches:
                failures.append(f"{name}: {field}={value}, expected {expected_field}={expected}")
        if stage["conflict"] is not None:
            number = re.findall(rf"^RETAINED_GUI_NUMBER id={NUMBER} (.*)$", block, re.M)
            if len(number) != 1 or f'conflict={stage["conflict"]}' not in number[0]:
                failures.append(name + ": numeric conflict did not match")
            if stage["conflict"] and (not number or "bytes=1 " not in number[0]):
                failures.append(name + ": unfinished numeric text was discarded")
        if " failed" in block or "openq4_retainedGui: requires" in block or "openq4_guiGet: unknown" in block:
            failures.append(name + ": semantic diagnostic failed")
    return {"passed": not failures, "stages": len(names), "failures": failures}


def active_renderer(log):
    matches = re.findall(r"^Renderer API: (?:requested=\S+ active=(\w+)|(\w+) \(module\))\s*$", log, re.M)
    return next((value for value in matches[-1] if value), None) if matches else None


def capture(args):
    output, runtime = args.output.resolve(), args.runtime.resolve()
    if output.exists() or not output.is_relative_to((ROOT / ".tmp").resolve()):
        raise ValueError("Use a new output directory under the project's .tmp")
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if not executable.is_file() or not (args.assets / "q4base").is_dir():
        raise ValueError("Staged client and installed Quake 4 assets are required")
    source = page.source_contract(runtime)
    for relative in ("src/ui/SettingsService.cpp", "src/ui/application/SettingsTransaction.cpp",
                     "src/ui/application/SettingsTransaction.h", "src/ui/retained/Interaction.cpp",
                     "src/ui/retained/Interaction.h", "src/ui/retained/Runtime.cpp",
                     "src/ui/UserInterfaceRetained.cpp", "tools/ui/capture_system_refresh.py"):
        source[relative] = page.digest(ROOT / relative)
    profiles = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8-sig"))["configurations"]
    prefix = "(SP) airdefense1 " if args.mode == "sp" else "(MP) q4dm1 "
    profile = next(p for p in profiles if p["name"].startswith(prefix) and p["name"].endswith("GL"))
    game = output / "save/baseoq4"
    game.mkdir(parents=True)
    cfg = game / "system-refresh.cfg"
    cfg.write_text(script(), encoding="utf-8")
    overrides = {"fs_basepath": str(args.assets.resolve()), "fs_savepath": str(output / "save"),
        "fs_devpath": str(output / "save"), "fs_game": "baseoq4", "logFile": "2", "logFileName": "logs/openq4.log",
        "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720", "r_mode": "-1",
        "r_customWidth": "1280", "r_customHeight": "720", "r_multiSamples": "0", "r_renderApi": args.renderer,
        "in_mouse": "0", "in_joystick": "0", "in_joystickRumble": "0", "g_autoScreenshot": "0",
        "g_autoSkipCinematics": "1", "g_autoExecAfterMapLoad": cfg.name, "g_autoExecAfterMapLoadDelayMs": "3000",
        "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1", "com_maxfps": "60",
        "ui_autoJoin": "1" if args.mode == "mp" else "0", "ui_retained": "1", "ui_retainedSystem": "1",
        "ui_retainedTrace": "1", "ui_retainedScale": "1", "ui_retainedDensity": "1", "ui_retainedTextScale": "1",
        page.MULTIPLE_INSTANCES: "1"}
    command = [str(executable)]
    for key, value in overrides.items():
        command += ["+set", key, value]
    index, original = 0, profile["args"]
    while index < len(original):
        count = 3 if original[index].lower() in ("+set", "+seta") and index + 2 < len(original) else 1
        if count == 1 or original[index + 1].lower() not in {k.lower() for k in overrides}:
            command += [v.replace("${workspaceFolder}", str(ROOT)) for v in original[index:index + count]]
        index += count
    binaries = [executable, *runtime.glob("renderer-*.dll"), *(runtime / "baseoq4").glob("game-*.dll")]
    binding = {str(p.relative_to(runtime)): page.digest(p) for p in binaries}
    for path in binaries:
        built = getattr(args, "build", ROOT / "builddir") / path.relative_to(runtime)
        if built.is_file() and page.digest(built) != binding[str(path.relative_to(runtime))]:
            raise ValueError("Re-stage current build before capture: " + path.name)
    metadata = {"status": "running", "mode": args.mode, "renderer": args.renderer, "profile": profile["name"],
        "command": command, "cwd": str(runtime), "source": source, "binaries": binding,
        "script_sha256": page.digest(cfg), "windowed": True, "hidden_window": True,
        "host_input_injection": False, "capture_method": "engine screenshot after map gameplay"}
    report = output / "capture.json"
    def save():
        report.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    save()
    options = {"env": dict(os.environ, TEMP=str(output), TMP=str(output))}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    started = time.monotonic()
    with (output / "process.log").open("w", encoding="utf-8") as process_log:
        process = subprocess.Popen(command, cwd=runtime, stdout=process_log, stderr=subprocess.STDOUT, **options)
        print(f"SYSTEM refresh {args.mode}/{args.renderer}: hidden windowed PID {process.pid}", flush=True)
        try:
            metadata["returncode"] = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
            metadata["status"] = "timeout"; save(); return 1
    metadata["seconds"] = round(time.monotonic() - started, 3)
    log_path = game / "logs/openq4.log"
    log = re.sub(r"\^[0-9]", "", log_path.read_text(encoding="utf-8", errors="replace")) if log_path.is_file() else ""
    metadata["log_sha256"] = page.digest(log_path) if log_path.is_file() else None
    metadata["qualification"] = qualify(log, args.mode)
    metadata["diagnostics"] = [line for line in log.splitlines() if any(s in line for s in ("WARNING:", "ERROR:", "FATAL:"))]
    metadata["active_renderer"] = active_renderer(log)
    valid = metadata["returncode"] == 0 and metadata["qualification"]["passed"] and metadata["active_renderer"] == args.renderer
    metadata["screenshots"] = []
    for stage in stages():
        path = game / f'screenshots/refresh-{stage["name"]}.tga'
        size = page.tga_dimensions(path.read_bytes()) if path.is_file() else None
        if size != [1280, 720] and size != (1280, 720):
            valid = False
        metadata["screenshots"].append({"path": str(path.relative_to(output)), "size": size,
                                        "sha256": page.digest(path) if path.is_file() else None})
    metadata["binaries_unchanged"] = binding == {str(p.relative_to(runtime)): page.digest(p) for p in binaries}
    current_source = page.source_contract(runtime)
    for relative in source.keys() - current_source.keys():
        current_source[relative] = page.digest(ROOT / relative)
    metadata["source_unchanged"] = source == current_source
    valid = valid and metadata["source_unchanged"] and metadata["binaries_unchanged"]
    metadata["status"] = "captured_pending_visual_and_warning_review" if valid else "failed"
    save()
    print(f'{metadata["status"]}: {report}', flush=True)
    return 0 if valid else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("sp", "mp"), required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, default=ROOT / ".install")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--build", type=Path, default=ROOT / "builddir",
                        help="build tree the runtime was staged from (builddir-perf for the VS Code builds)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
