#!/usr/bin/env python3
"""REN-03 scan: shadow maps against stencil shadows, one stock light at a time.

Opt-in, local and hardware-bound. Reads a stock map's light entities from the
installed Quake 4 packs, selects shadow-casting lights (by default the point
lights whose source, origin plus light_center, lies outside their own radius
box; --select also takes the ones drawn through one distant projection,
projectors, parallel lights and nodynamicshadows lights), and for each one:

- isolates it with r_singleLight (listRenderLightDefs prints each render
  light's origin, which identifies the authored light),
- freezes game time with the player's own shadow, the HUD and the view weapon
  off, and
- captures six views from the light's entity origin (four headings, up and
  down), or from each --position, with stencil shadows, with shadow maps and
  with no shadows.

No input is synthesized. Per light it reports, for the worst view, the share
of pixels the map shadows and stencil lights ("map only"), the reverse
("stencil only") and how much of the view stencil shadows at all. A JSON
report and one stencil | mapped | unshadowed strip per light go to
--output-dir.

--spawn puts stock entities into the level (the game's own spawn command)
and reports, per view, the pixels they changed: how bright they are without
shadows, under stencil and under maps. With --view that is a receiver placed
behind chosen geometry under one chosen light, for example a noSelfShadow
crate behind noSelfShadow world geometry.

The scan measures; it does not judge. A soft map edge against a hard stencil
edge differs both ways by the same amount, and a scene that sways between
captures adds to both. A large one-sided share is the signal.

The OpenGL client can stall in renderer startup while other clients start on
the same GPU; a stalled launch is retried.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from renderer_temporal_stability import luma, read_tga  # noqa: E402

DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "shadow-stencil-parity-scan"
VIEWS = ((0, 0), (0, 90), (0, 180), (0, 270), (80, 0), (-80, 0))
MODES = (("stencil", {"r_shadows": "1", "r_useShadowMap": "0"}),
         ("mapped", {"r_shadows": "1", "r_useShadowMap": "1"}),
         ("none", {"r_shadows": "0", "r_useShadowMap": "0"}))
LAUNCH_SECONDS = 300


def read_map(basepath: Path, map_name: str) -> str:
    for pk4 in sorted((basepath / "q4base").glob("*.pk4"), reverse=True):
        with zipfile.ZipFile(pk4) as archive:
            for member in archive.namelist():
                if member.lower() == f"maps/{map_name}.map":
                    return archive.read(member).decode("latin-1")
    raise SystemExit(f"maps/{map_name}.map is not in the packs under {basepath}")


def entities(text: str):
    depth = 0
    start = 0
    for index, char in enumerate(text):
        if char == "{":
            if depth == 0:
                start = index
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                yield text[start:index + 1]


def distant_point(centre: list[float], radius: list[float]) -> bool:
    """R_ShadowMapUsesDistantPointProjection: the source sees the whole box in front of it."""
    distance = sum(value * value for value in centre) ** 0.5
    if distance <= 0.0:
        return False
    extent = sum(abs(c / distance * r) for c, r in zip(centre, radius))
    return distance > extent + max(1.0, distance * 0.01)


def map_lights(text: str) -> list[dict]:
    """Shadow-casting lights of a map: point lights (cube or distant projection), parallel
    lights and projectors, each with the class the shadow-map path gives it."""
    lights = []
    for block in entities(text):
        head = block.split("{", 2)[1] if block.count("{") > 1 else block
        pairs = dict(re.findall(r'^\s*"([^"]+)"\s+"([^"]*)"', head, flags=re.M))
        if pairs.get("classname") != "light" or pairs.get("noshadows") == "1":
            continue
        origin = [float(v) for v in pairs.get("origin", "0 0 0").split()]
        light = {"name": pairs.get("name"), "origin": origin,
                 "noDynamicShadows": pairs.get("nodynamicshadows") == "1"}
        if "light_target" in pairs:
            target = [float(v) for v in pairs["light_target"].split()]
            reach = sum(value * value for value in target) ** 0.5
            light.update(kind="projected", radius=[reach] * 3, centre=[0.0, 0.0, 0.0], sourceOutsideBy=0.0)
        elif "light_radius" in pairs:
            radius = [float(v) for v in pairs["light_radius"].split()]
            centre = [float(v) for v in pairs.get("light_center", "0 0 0").split()]
            kind = ("parallel" if pairs.get("parallel") == "1"
                    else "distant" if distant_point(centre, radius) else "cube")
            light.update(kind=kind, radius=radius, centre=centre,
                         sourceOutsideBy=max(abs(c) - r for c, r in zip(centre, radius)))
        else:
            continue
        lights.append(light)
    return lights


def point_lights(text: str) -> list[dict]:
    return [light for light in map_lights(text) if light["kind"] in ("cube", "distant")]


def capture_commands(prefix: str) -> list[str]:
    """One picture per mode, each from interactions built under that mode.

    An interaction keeps the stencil volumes it was built with. Built while
    shadow maps were on, a prelit light's world surfaces each carry a volume of
    their own, uncut, and with shadow maps then turned off those are drawn
    beside the prelight volume: a stencil picture no stencil session shows.
    regenerateWorld rebuilds every interaction under the settings in force.
    """
    commands = []
    for mode, cvars in MODES:
        commands += [f"set {name} {value}" for name, value in cvars.items()]
        commands += ["regenerateWorld", "wait 14", f"screenshot {prefix}_{mode}.tga", "wait 2"]
    return commands


def view_poses(light: dict, positions: list[list[float]], exact: list[tuple] = ()) -> list[tuple]:
    """The exact views when given; otherwise six from each position: four headings, up and down."""
    if exact:
        return list(exact)
    return [(x, y, z, pitch, yaw) for x, y, z in (positions or [light["origin"]]) for pitch, yaw in VIEWS]


def plain_capture(path: str) -> list[str]:
    """Every light, no shadows: the picture a spawned entity is found in."""
    return ["set r_singleLight -1", "set r_shadows 0", "set r_useShadowMap 0", "regenerateWorld", "wait 14",
            f"screenshot {path}", "wait 2"]


def launch(runtime: Path, run_dir: Path, backend: str, map_name: str, cfg: list[str],
           extra: dict[str, str]) -> Path:
    game_dir = run_dir / "baseoq4"
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    settings = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": 1,
        "logFile": 2, "logFileName": "logs/scan.log", "developer": 1, "fs_savepath": run_dir,
        "fs_devpath": run_dir, "fs_game": "baseoq4", "com_skipLoadingContinue": 1,
        "com_loadingContinueAutoAdvance": 1, "g_autoSkipCinematics": 1, "com_skipLogoVideos": 1,
        "in_mouse": 0, "com_maxfps": 0, "r_swapInterval": 0, "r_fullscreen": 0, "r_mode": -1,
        "r_customWidth": 1280, "r_customHeight": 720, "r_windowWidth": 1280, "r_windowHeight": 720,
        "r_borderless": 0, "r_borderlessDefaultMigrated": 1, "r_hiddenWindow": 1, "r_motionBlur": 0,
        "ui_retained": 1, "ui_autoJoin": 0, "com_fixedTic": 1,
        "r_renderApi": "vulkan" if backend == "vk" else "gl", "r_temporalAA": 0, "r_multiSamples": 0,
        "g_showPlayerShadow": 0, "g_showHud": 0, "ui_showGun": 0,
        # the Quality shadow configuration, decided before the map loads
        "r_useShadowMap": 1, "r_shadowMapCSM": 1, "r_shadowMapPointLights": 1,
        "g_autoExecAfterMapLoad": "scan.cfg", "g_autoExecAfterMapLoadDelayMs": 1500, **extra,
    }
    command = [str(exe)]
    for key, value in settings.items():
        command += ["+set", key, str(value)]
    command += ["+devmap", map_name]
    for attempt in range(3):
        if run_dir.exists():
            shutil.rmtree(run_dir)
        game_dir.mkdir(parents=True)
        (game_dir / "scan.cfg").write_text("\n".join(cfg) + "\n", encoding="ascii")
        process = subprocess.Popen(command, cwd=runtime)
        try:
            process.wait(timeout=LAUNCH_SECONDS)
            return game_dir
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print(f"launch stalled (attempt {attempt + 1}), retrying", flush=True)
    raise SystemExit(f"the client did not finish in three launches: {run_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 (holds q4base)")
    parser.add_argument("--map", required=True, help="stock map, for example game/mcc_landing")
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--select", choices=("off-centre", "distant", "nodynamic", "projected", "parallel", "all"),
                        default="off-centre",
                        help="point lights whose source lies outside their box; those of them drawn through one "
                             "distant projection; nodynamicshadows lights; projectors; parallel lights; or all")
    parser.add_argument("--names", default="", help="comma-separated light names; overrides --select")
    parser.add_argument("--render-lights", default="", metavar="INDEX,...",
                        help="render light indexes from listRenderLightDefs, for lights the map's entity origins "
                             "do not identify (a light bound to a mover, such as storage2's alarm lights); "
                             "overrides --select and --names")
    parser.add_argument("--max-lights", type=int, default=16)
    parser.add_argument("--min-radius", type=float, default=64.0)
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE", help="extra startup cvar")
    parser.add_argument("--position", action="append", default=[], metavar="X,Y,Z",
                        help="view from here instead of from each light's entity origin (repeatable): a light "
                             "whose origin is buried shows what no player sees; write --position=X,Y,Z when "
                             "X is negative")
    parser.add_argument("--view", action="append", default=[], metavar="X,Y,Z,PITCH,YAW",
                        help="one exact view (repeatable); replaces the six views from each position; write "
                             "--view=X,... when X is negative")
    parser.add_argument("--spawn", action="append", default=[], metavar="ARGS",
                        help="arguments of a spawn command for a stock entity, for example 'func_static name "
                             "probe model models/mapobjects/marine/equipment/crates/crate1.lwo origin \"437 -1575 "
                             "98\"' (repeatable); the pixels the entities change are reported per view")
    args = parser.parse_args()

    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    extra = dict(item.split("=", 1) for item in args.set)
    names = {name for name in args.names.split(",") if name}
    lights = map_lights(read_map(args.basepath, args.map))
    if names:
        lights = [light for light in lights if light["name"] in names]
    else:
        lights = [light for light in lights if min(light["radius"]) >= args.min_radius]
        if args.select == "off-centre":
            lights = [light for light in lights if light["kind"] in ("cube", "distant")
                      and light["sourceOutsideBy"] > 0 and not light["noDynamicShadows"]]
        elif args.select == "nodynamic":
            lights = [light for light in lights if light["noDynamicShadows"]]
        elif args.select in ("distant", "projected", "parallel"):
            lights = [light for light in lights if light["kind"] == args.select and not light["noDynamicShadows"]]
    lights.sort(key=lambda light: (-light["sourceOutsideBy"], -max(light["radius"])))
    positions = [[float(value) for value in item.split(",")] for item in args.position]
    exact = [tuple(float(value) for value in item.split(",")) for item in args.view]
    lights = lights[:args.max_lights]
    render_lights = [int(item) for item in args.render_lights.split(",") if item]
    if not lights and not render_lights:
        print("no light selected")
        return 0

    tag = args.map.replace("/", "_")
    report = {"map": args.map, "runtime": str(runtime), "select": args.select, "extra": extra, "lights": []}
    for backend in [name for name in args.backends.split(",") if name]:
        game = launch(runtime, output / f"{backend}-{tag}-list", backend, args.map,
                      ["wait 60", "listRenderLightDefs", "wait 2", "quit"], extra)
        log = (game / "logs" / "scan.log").read_text(encoding="utf-8", errors="replace")
        index_at = {}
        listed = {}
        for line in log.splitlines():
            match = re.match(r"\s*(\d+):\s+\d+ intr\s+\d+ refs (\S+) at (.*)$", line)
            if match:
                index_at[match.group(3).strip()] = int(match.group(1))
                listed[int(match.group(1))] = (match.group(2), [float(v) for v in match.group(3).split()])
        cfg = ["god", "notarget", "noclip", "wait 30"]
        selected = []
        if render_lights:
            # named by index: the light is where the list found it
            for number, index in enumerate(render_lights):
                if index not in listed:
                    print(f"{backend} render light {index}: not in the list")
                    continue
                selected.append((number, {"name": f"render{index}", "kind": listed[index][0].split("/")[-1][:9],
                                          "origin": listed[index][1], "radius": [0.0] * 3, "centre": [0.0] * 3,
                                          "sourceOutsideBy": 0.0, "noDynamicShadows": False}, index))
        for number, light in enumerate([] if render_lights else lights):
            key = " ".join(str(int(value)) for value in light["origin"])
            if key not in index_at:
                print(f"{backend} {light['name']}: no render light at {key}")
                continue
            selected.append((number, light, index_at[key]))

        def pose(view: tuple) -> list[str]:
            x, y, z, pitch, yaw = view
            return [f"setviewpos {x:.0f} {y:.0f} {z:.0f} {pitch:.0f} {yaw:.0f} 0", "wait 20", "g_stopTime 1", "wait 6"]

        if args.spawn:
            # each view first without the entities, then with them
            for number, light, index in selected:
                for view, placed in enumerate(view_poses(light, positions, exact)):
                    cfg += pose(placed) + plain_capture(f"screenshots/l{number}_v{view}_empty.tga") + ["g_stopTime 0"]
            cfg += [f"spawn {item}" for item in args.spawn] + ["wait 30"]
        for number, light, index in selected:
            for view, placed in enumerate(view_poses(light, positions, exact)):
                cfg += pose(placed)
                if args.spawn:
                    cfg += plain_capture(f"screenshots/l{number}_v{view}_full.tga")
                cfg.append(f"set r_singleLight {index}")
                cfg += capture_commands(f"screenshots/l{number}_v{view}")
                cfg.append("g_stopTime 0")
        cfg += ["wait 5", "quit"]
        game = launch(runtime, output / f"{backend}-{tag}-views", backend, args.map, cfg, extra)
        shots = game / "screenshots"
        for number, light, index in selected:
            worst = {"mapOnly": 0.0, "stencilOnly": 0.0, "view": -1}
            stencil_shadow = 0.0
            strip = None
            receivers = []
            for view in range(len(view_poses(light, positions, exact))):
                paths = {mode: shots / f"l{number}_v{view}_{mode}.tga" for mode, _ in MODES}
                if not all(path.is_file() for path in paths.values()):
                    continue
                images = {mode: read_tga(path) for mode, path in paths.items()}
                y = {mode: luma(image) for mode, image in images.items()}
                empty, full = (shots / f"l{number}_v{view}_{kind}.tga" for kind in ("empty", "full"))
                if empty.is_file() and full.is_file():
                    # the spawned entities: what they changed with every light on and no shadows
                    mask = np.abs(luma(read_tga(full)) - luma(read_tga(empty))) > 8.0
                    receiver = {"view": view, "pixels": int(mask.sum())}
                    if receiver["pixels"]:
                        receiver.update({mode: float(y[mode][mask].mean()) for mode, _ in MODES})
                        receiver["mapOnly"] = float(np.mean((y["stencil"] - y["mapped"])[mask] > 12.0))
                        receiver["stencilOnly"] = float(np.mean((y["mapped"] - y["stencil"])[mask] > 12.0))
                    receivers.append(receiver)
                    print(f"{backend} {light['name']:16} view {view}: spawned receiver {receiver['pixels']} px, "
                          + (f"brightness unshadowed {receiver['none']:.2f}, stencil {receiver['stencil']:.2f}, "
                             f"mapped {receiver['mapped']:.2f}; map only {100 * receiver['mapOnly']:.2f}%, "
                             f"stencil only {100 * receiver['stencilOnly']:.2f}%" if receiver["pixels"]
                             else "none in view"), flush=True)
                map_only = float(np.mean((y["stencil"] - y["mapped"]) > 12.0))
                stencil_shadow = max(stencil_shadow, float(np.mean(y["none"] - y["stencil"] > 6.0)))
                worst["stencilOnly"] = max(worst["stencilOnly"], float(np.mean((y["mapped"] - y["stencil"]) > 12.0)))
                if strip is None or map_only > worst["mapOnly"]:
                    worst["view"] = view
                    strip = np.concatenate([images[mode] for mode, _ in MODES], axis=1)
                worst["mapOnly"] = max(worst["mapOnly"], map_only)
            entry = {"backend": backend, "light": light["name"], "kind": light["kind"], "renderLight": index,
                     "sourceOutsideBy": light["sourceOutsideBy"], "noDynamicShadows": light["noDynamicShadows"],
                     "stencilShadow": stencil_shadow, **worst}
            if receivers:
                entry["receivers"] = receivers
            report["lights"].append(entry)
            if strip is not None:
                try:
                    from PIL import Image
                    Image.fromarray(strip.clip(0, 255).astype(np.uint8)).save(
                        output / f"{backend}-{tag}-{light['name']}.png")
                except ImportError:
                    pass
            print(f"{backend} {light['name']:16} {light['kind']:9} source outside by {light['sourceOutsideBy']:7.0f}  "
                  f"map only {100 * worst['mapOnly']:6.2f}%  stencil only {100 * worst['stencilOnly']:6.2f}%  "
                  f"stencil shadows {100 * stencil_shadow:6.2f}%  (view {worst['view']})", flush=True)
    (output / f"report-{tag}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"report: {output / f'report-{tag}.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
