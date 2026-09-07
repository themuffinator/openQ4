#!/usr/bin/env python3
"""Capture individual stock maps with mapped, stencil, and disabled shadows.

Uses the repository's mode-specific launch configurations and staged client.
All runs use isolated settings, a bordered window, and engine screenshots.
No keyboard/mouse input is injected. Images still require visual review: a
successful process and allocated map do not establish correct occlusion.
"""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import random
import re
import subprocess

from renderer_shadow_mapping_transitions import SCENARIO_MAPS, transition_commands, validate_transition_reports
from renderer_shadow_mapping_movers import MOVER_SCENARIOS, validate_mover_images


ROOT = Path(__file__).resolve().parents[2]
MAPS = ("airdefense1", "airdefense2", "storage1", "storage2", "medlabs",
        "mcc_landing", "q4dm1", "q4dm9")
CAPTURES = ("mapped", "stencil", "unshadowed")
MP_VIEWS = {"q4dm1": (-4768, 6624, 324.25, 315),
            "q4dm9": (1528, -384, 452.25, 180)}
SP_VIEWS = {"airdefense1": (10200, -6800, 40, 0, 170, 0)}


def value(args: list[str], key: str) -> str | None:
    return args[args.index(key) + 1] if key in args else None


def setting(args: list[str], key: str, new_value: object) -> None:
    if key in args:
        args[args.index(key) + 1] = str(new_value)
    else:
        args[:0] = ["+set", key, str(new_value)]


def commands(map_name: str, extra: list[str], settle_lift: bool = True) -> list[str]:
    multiplayer = map_name.startswith("q4dm")
    result = ["wait 180", "god", "notarget", "g_showHud 0", "ui_showGun 0"]
    # Pin useful MP views: random spawn selection otherwise changes both
    # the receiver set and whether q4dm9's parallel light enters the view.
    if multiplayer:
        result += ["noclip", "setviewpos " + " ".join(map(str, MP_VIEWS[map_name])), "wait 5"]
    elif map_name in SP_VIEWS:
        # Reproduce the retained mapped/stencil Air Defense 1 camera. Wait
        # for the teleported presentation pose before freezing simulation;
        # an immediate viewpos can still report the previous frame's camera.
        result += ["noclip", "setviewpos " + " ".join(map(str, SP_VIEWS[map_name])), "wait 3"]
    if map_name == "storage2" and settle_lift:
        # The initial lift takes much longer than a normal spawn to reach
        # useful receivers. A frozen opening frame only tests the lift cab.
        result += ["waitMsec 85000"]
    if not multiplayer:
        result += ["g_stopTime 1"]
    result += extra
    result += ["getviewpos" if multiplayer else "viewpos", "wait 30", "gfxInfo"]
    for kind in CAPTURES:
        if kind == "stencil":
            result += ["r_useShadowMap 0", "wait 5"]
        elif kind == "unshadowed":
            result += ["r_shadows 0", "wait 5"]
        result += [f"echo SHADOW_MATRIX_{kind.upper()}",
                   f"screenshot screenshots/{kind}.tga", "wait 3"]
    return result + ["quit"]


def inspect_run(save: Path, code: int | str, map_name: str,
                captures: tuple[str, ...] = CAPTURES,
                fail_on_missing_casters: bool = False,
                scenario: str | None = None) -> dict:
    log = save / "baseoq4/logs/shadow-matrix.log"
    text = re.sub(r"\^\d", "", log.read_text(errors="replace")) if log.exists() else ""
    lines = text.splitlines()
    mapped_lines = text.split("SHADOW_MATRIX_MAPPED", 1)[0].splitlines()
    failures = validate_transition_reports(scenario, text)
    missing_casters = list(dict.fromkeys(line for line in lines
                                        if line.startswith("SM missing caster:")))
    # Transition fixtures use supported casters. Ordinary stock-map captures
    # may legitimately encounter authored stencil-only surfaces.
    if fail_on_missing_casters:
        failures.extend(missing_casters)
    multiplayer = map_name.startswith("q4dm")
    if code != 0:
        failures.append(f"Process exit: {code}")
    expected_api = save.name.rsplit("-", 1)[-1]
    if expected_api in ("gl", "vulkan") and not re.search(
            rf"^Renderer API: {expected_api} \(module\)$", text, re.MULTILINE):
        failures.append(f"Requested {expected_api} renderer module was not confirmed")
    if not re.search(r"MODE:.*960 x 540 windowed", text):
        failures.append("Bordered 960x540 window was not confirmed")
    if multiplayer and not re.search(r"spectate\s*=\s*0|spectate\s+0", text):
        failures.append("Local multiplayer gameplay join was not confirmed")
    poses = re.findall(r"^\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) ([-\d.]+)$", text, re.MULTILINE)
    if multiplayer and (not poses or any(abs(float(actual) - expected) > 1.0
            for actual, expected in zip(poses[-1], MP_VIEWS[map_name]))):
        failures.append("Requested multiplayer camera position was not confirmed")
    if map_name in SP_VIEWS:
        sp_poses = re.findall(r"^origin: \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) "
                              r"angles: \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)$",
                              text, re.MULTILINE)
        if not sp_poses or any(abs(float(actual) - expected) > 1.0
                for actual, expected in zip(sp_poses[-1], SP_VIEWS[map_name])):
            failures.append("Requested single-player comparison camera was not confirmed")
    for kind in captures:
        path = save / f"baseoq4/screenshots/{kind}.tga"
        if not path.is_file() or path.stat().st_size < 18:
            failures.append(f"Missing engine capture: {kind}")
            continue
        header = path.read_bytes()[:18]
        if (int.from_bytes(header[12:14], "little"),
                int.from_bytes(header[14:16], "little")) != (960, 540):
            failures.append(f"Unexpected capture dimensions: {kind}")
    if not all(f"SHADOW_MATRIX_{kind.upper()}" in text for kind in CAPTURES):
        failures.append("Mapped/stencil/unshadowed capture sequence was not completed")
    failures += list(dict.fromkeys(line for line in lines if any(token in line for token in (
        "required shadow resource unavailable", "result=render-fail", "result=mask-fail",
        "VUID-", "Unknown command", "screenshot: bad dimensions",
        "tracked texture unit", "invalid tracked texture unit",
        "frame geometry ring overflow", "entity not found",
        "self-test FAILED", "self-test failed",
        "failed to resume the main rendering scope"))))
    image_metrics = {}
    if scenario in MOVER_SCENARIOS:
        image_failures, image_metrics = validate_mover_images(save)
        failures.extend(image_failures)
    return {
        "case": save.name, "exit": code, "failures": failures,
        "viewpos": [line for line in lines if ("origin:" in line and "angles:" in line)
                    or re.fullmatch(r"\([-\d. ]+\) [-\d.]+", line)],
        "summary": [line for line in mapped_lines if line.startswith(("SM summary:", "Vulkan shadow cache:"))][-1:],
        "missing_casters": missing_casters,
        "warnings": sorted(set(line for line in lines if "WARNING:" in line)),
        "log": str(log), "captures": str(save / "baseoq4/screenshots"),
        "image_metrics": image_metrics,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--maps", nargs="+", help="Stock map names from the SP launch catalog, or q4dm1/q4dm9")
    selection.add_argument("--random-maps", type=int, metavar="COUNT",
                           help="Sample stock SP maps from the launch catalog without replacement")
    parser.add_argument("--seed", type=int, default=20260905,
                        help="Reproducible random-map seed (default: 20260905)")
    parser.add_argument("--scenario", choices=tuple(SCENARIO_MAPS), help="Exercise state transitions in the scenario's stock map")
    parser.add_argument("--apis", nargs="+", choices=("gl", "vulkan"), default=["gl", "vulkan"])
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".tmp" / ("shadow-maps-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
    parser.add_argument("--binary", type=Path, default=ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--extra-cfg", type=Path, help="Additional engine commands before the three captures")
    parser.add_argument("--dry-run", action="store_true", help="Write launch/config files without starting the game")
    parser.add_argument("--timeout", type=int, default=180, help="Maximum seconds for each game process (default: 180)")
    opts = parser.parse_args()
    if opts.timeout <= 0:
        parser.error("The process timeout must be positive")
    configs = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))["configurations"]
    stock_sp_maps = sorted({qualified.removeprefix("game/") for config in configs
                            if (qualified := value(config.get("args", []), "+map"))
                            and qualified.startswith("game/")
                            and value(config["args"], "r_renderApi") == "gl"})
    if opts.random_maps is not None:
        if opts.scenario:
            parser.error("A transition scenario cannot select random maps")
        if not 1 <= opts.random_maps <= len(stock_sp_maps):
            parser.error(f"The random map count must be between 1 and {len(stock_sp_maps)}")
        opts.maps = random.Random(opts.seed).sample(stock_sp_maps, opts.random_maps)
    if opts.maps is None:
        opts.maps = [SCENARIO_MAPS[opts.scenario]] if opts.scenario else list(MAPS)
    unknown = set(opts.maps) - set(stock_sp_maps) - set(MP_VIEWS)
    if unknown:
        parser.error("Maps have no supported launch profile: " + ", ".join(sorted(unknown)))
    if opts.scenario and opts.maps != [SCENARIO_MAPS[opts.scenario]]:
        parser.error("The selected scenario requires map " + SCENARIO_MAPS[opts.scenario])
    if len(set(opts.maps)) != len(opts.maps) or len(set(opts.apis)) != len(opts.apis):
        parser.error("Map and API selections must not contain duplicates")
    output = opts.output.resolve()
    if output.exists():
        parser.error("Use a new output directory so old captures cannot pass a new run")
    extra = opts.extra_cfg.read_text(encoding="utf-8").splitlines() if opts.extra_cfg else []
    captures = CAPTURES
    if opts.scenario:
        transition, additional_captures = transition_commands(opts.scenario)
        extra = transition + extra
        captures = additional_captures + CAPTURES
    results = []
    output.mkdir(parents=True)
    (output / "selection.json").write_text(json.dumps({
        "maps": opts.maps, "apis": opts.apis, "scenario": opts.scenario,
        "seed": opts.seed if opts.random_maps is not None else None,
        "random_pool": stock_sp_maps if opts.random_maps is not None else [],
    }, indent=2), encoding="utf-8")
    for map_name in opts.maps:
        multiplayer = map_name.startswith("q4dm")
        action = "+spawnServer" if multiplayer else "+map"
        qualified = ("mp/" if multiplayer else "game/") + map_name
        source = next(c for c in configs if value(c["args"], action) == qualified
                      and value(c["args"], "r_renderApi") == "gl"
                      and (not multiplayer or value(c["args"], "net_serverDedicated") == "0"))
        for api in opts.apis:
            save = output / f"{map_name}-{api}"
            cfg = save / "baseoq4/shadow-matrix.cfg"
            cfg.parent.mkdir(parents=True)
            cfg.write_text("\n".join(commands(map_name, extra,
                           settle_lift=opts.scenario != "emitter-lift")) + "\n", encoding="utf-8")
            args = [a.replace("${workspaceFolder}", str(ROOT)) for a in source["args"]]
            for key, new_value in {
                "fs_savepath": save, "fs_devpath": save,
                "logFile": 2, "logFileName": "logs/shadow-matrix.log",
                "r_renderApi": api, "r_vkValidation": 1,
                "r_fullscreen": 0, "r_fullscreenDesktop": 0,
                "r_borderless": 0, "r_borderlessDefaultMigrated": 1,
                "r_mode": -1, "r_customWidth": 960, "r_customHeight": 540,
                "r_windowWidth": 960, "r_windowHeight": 540,
                "in_mouse": 0, "in_nograb": 1,
                "com_skipLoadingContinue": 1, "com_allowConsole": 1,
                "com_maxfps": 120, "r_swapInterval": 0,
                "g_stopTime": 0, "g_autoScreenshot": 0, "g_autoSkipCinematics": 1,
                "r_shadows": 1, "r_useShadowMap": 1, "r_shadowMapPointLights": 1,
                "r_shadowMapCSM": 1, "r_shadowMapCacheCSM": 1, "r_shadowMapStaticCache": 1,
                "r_shadowMapMaxUpdatesPerView": 0, "r_shadowMapReport": 2,
                "r_multiSamples": 0, "sv_cheats": 1,
            }.items():
                setting(args, key, new_value)
            if multiplayer:
                setting(args, "ui_autoJoin", 1)
                setting(args, "si_pure", 0)
                setting(args, "net_allowCheats", 1)
            if map_name == "storage1":
                setting(args, "g_openQ4Storage1NoDropPodState", 1)
            args += ["+exec", "shadow-matrix.cfg"]
            launch = {"source": source["name"], "binary": str(opts.binary.resolve()),
                      "cwd": str(ROOT / ".install"), "args": args}
            (save / "launch.json").write_text(json.dumps(launch, indent=2), encoding="utf-8")
            if opts.dry_run:
                continue
            print(f"Testing {map_name} / {api}", flush=True)
            with (save / "stdout.txt").open("w") as stdout, (save / "stderr.txt").open("w") as stderr:
                process = subprocess.Popen([launch["binary"], *args], cwd=launch["cwd"],
                                           stdout=stdout, stderr=stderr,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                try:
                    code = process.wait(timeout=opts.timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    code = "timeout"
            result = inspect_run(save, code, map_name, captures,
                                 fail_on_missing_casters=bool(opts.scenario),
                                 scenario=opts.scenario)
            results.append(result)
            (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            print(json.dumps({key: result[key] for key in ("case", "exit", "failures", "summary")}), flush=True)
    return int(any(result["failures"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
