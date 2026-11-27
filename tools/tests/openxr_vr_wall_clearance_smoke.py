#!/usr/bin/env python3
"""A VR gun pushed through a wall must not fire from the far side (single
player), against the openQ4 OpenXR test runtime (requires a staged client
built with OpenXR, the built test runtime and retail assets).

A single-player shot held in the hand leaves the weapon hand, not the eye, so
with room scale or an outstretched arm the muzzle can be on the far side of a
wall, door or crate. The game clears the shot's way from the body's own eye,
through the hand, to the launch point, with the shot's own clip mask, and
starts a blocked shot just in front of what it met.

The runtime's frame script places the head and hands and pulls the trigger
when marker files appear; nothing synthesises keyboard or mouse input.

Checks, on dispersal, standing against the large-shot clip that seals a
ledge over the tram trench (a nonsolid brush that stops rockets and other
large projectiles, not bullets):
- the control: a rocket fired with the hand drawn back, the muzzle short of
  the clip, bursts on it and hurts the player, on any build;
- a rocket fired with the weapon hand a metre ahead, through the clip,
  explodes against the clip in front of the player, so the player takes its
  splash (g_debugDamage prints the damage). A rocket started from the hand
  flew on across the trench and hurt no one: the clearance traced only what
  bullets stop;
- vr_debug logs that shot as cleared, starting on the near side of the clip,
  and the aim marker's dot on the clip where the rocket burst;
- a machine gun shot with the hand pushed through the floor and aimed down
  starts above the floor, and the marker's dot is where that shot's line
  meets the world;
- physically crouched, the clearance starts from the body's crouched eye
  rather than the tracking origin, which is lifted to the standing eye and
  could lie inside a low ceiling.

    python tools/tests/openxr_vr_wall_clearance_smoke.py --basepath <Quake 4>
        [--render-api gl|vulkan]
"""
from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))
import openxr_vr_playspace_smoke as smoke  # noqa: E402
from mp_round_remote_smoke import verify_runtime_is_current  # noqa: E402

MAP = "game/dispersal"
# The ledge south of the clip: floor at z -3728, the clip a 4-unit slab across
# x 6144..6400 at y -1444..-1440, from the floor to the ceiling at -3600.
# The view stands 32 units short of the clip, facing it (+y, yaw 90).
FLOOR_Z = -3728.0
CLIP_NEAR_Y = -1444.0
VIEW = "6272 -1476 -3660 90"
# weapon wheel slots, as player.def numbers them
MACHINEGUN, ROCKETLAUNCHER = 1, 6

NUMBER = r"(-?\d+(?:\.\d+)?)"
VEC = rf"\(\s*{NUMBER}\s+{NUMBER}\s+{NUMBER}\s*\)"
SHOT = re.compile(rf"VR shot (\w+) (cleared|clear) anchor {VEC} hand {VEC} start {VEC} impact {VEC} marker (?:{VEC}|none)")
DAMAGE = re.compile(r"client:0 health:(-?\d+) damage:(\d+)")


def shots(text: str) -> list[dict]:
    found = []
    for m in SHOT.finditer(text):
        g = m.groups()
        vec = lambda i: tuple(float(v) for v in g[i:i + 3])  # noqa: E731
        found.append({
            "kind": g[0], "cleared": g[1] == "cleared", "anchor": vec(2), "hand": vec(5), "start": vec(8),
            "impact": vec(11), "marker": vec(14) if g[14] is not None else None,
        })
    return found


def section(text: str, start: str, end: str) -> str:
    return smoke.after(text, start).partition(end)[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    options = parser.parse_args(argv)

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-wall-clearance-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")

    profile = output / options.render_api
    game = profile / "baseoq4"

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    def switch(slot: int) -> list[str]:
        return ["testWeaponWheel open", "waitMsec 150", f"testWeaponWheel slot {slot}", "waitMsec 150",
                "testWeaponWheel release", "waitMsec 1800"]

    def fire(name: str, hold_msec: int) -> list[str]:
        return [f"condump vr_marker_fire_{name}.txt", f"waitMsec {hold_msec}",
                f"condump vr_marker_release_{name}.txt", "waitMsec 300"]

    # stage metres, y up, -z ahead; the hand's pitch is negative down
    script = [
        # the control: the hand drawn back beside the head, so even the muzzle
        # stays short of the clip and the rocket bursts on it from either build
        f"when {marker('short')} hand right 0.05 1.45 0.10 0 0 0",
        # a metre ahead at chest height, level: the gun reaches through the clip
        f"when {marker('reach')} hand right 0.05 1.45 -1.00 0 0 0",
        # 0.3 m under the floor, a little ahead, aimed 30 degrees down
        f"when {marker('floor')} hand right 0.05 -0.30 -0.45 0 -30 0",
        # crouched half a metre, the hand level ahead of the chest
        f"when {marker('crouch')} head 0 0 0 0 1.10 0",
        f"when {marker('crouch')} hand right 0.05 0.95 -0.45 0 0 0",
    ]
    for name in ("short", "rocket", "floor", "crouch"):
        script += [f"when {marker('fire_' + name)} button right trigger 1",
                   f"when {marker('release_' + name)} button right trigger 0"]

    commands = [
        "waitMsec 2500", "notarget", "g_debugDamage 1", f"setviewpos {VIEW}", "waitMsec 800",
        "vr_recenter", "waitMsec 400", "vr_aimLaser 1", "ui_autoSwitch 0", "give all", "waitMsec 500",
        *switch(ROCKETLAUNCHER),
        "condump vr_marker_short.txt", "waitMsec 600",
        "echo VR_WALL_SHORT", *fire("short", 120), "waitMsec 1500", "echo VR_WALL_SHORT_DONE",
        "condump vr_marker_reach.txt", "waitMsec 600",
        "echo VR_WALL_ROCKET", *fire("rocket", 120), "waitMsec 1500", "echo VR_WALL_ROCKET_DONE",
        *switch(MACHINEGUN),
        "condump vr_marker_floor.txt", "waitMsec 600",
        "echo VR_WALL_FLOOR", *fire("floor", 40), "echo VR_WALL_FLOOR_DONE",
        "condump vr_marker_crouch.txt", "waitMsec 1500",
        "echo VR_WALL_CROUCH", "getviewpos", *fire("crouch", 40), "echo VR_WALL_CROUCH_DONE",
        "echo VR_PLAYSPACE_COMPLETE", "quit",
    ]
    text = smoke.run(options, staged, executable, manifest, profile, "stage", commands, script,
                     map_name=MAP, extra={"s_noSound": "1"})

    failures: list[str] = []

    # --- the control: a rocket launched short of the clip bursts on it
    if not DAMAGE.findall(section(text, "VR_WALL_SHORT", "VR_WALL_SHORT_DONE")):
        failures.append("the control rocket, fired with the hand short of the clip, did not hurt the player: "
                        "the rocket did not fire or the scene has changed")

    # --- the rocket through the large-shot clip
    rocket = section(text, "VR_WALL_ROCKET", "VR_WALL_ROCKET_DONE")
    damage = DAMAGE.findall(rocket)
    if not damage:
        failures.append("a rocket fired through the large-shot clip did not burst against it: the player took no "
                        "splash, so it started on the far side")
    rocket_shots = [s for s in shots(rocket) if s["kind"] == "projectile"]
    if not rocket_shots:
        failures.append("vr_debug logged no hand-held projectile shot through the clip")
    else:
        shot = rocket_shots[0]
        if not shot["cleared"]:
            failures.append(f"the rocket's way through the clip was not cleared: {shot}")
        if shot["start"][1] > CLIP_NEAR_Y:
            failures.append(f"the rocket started at y {shot['start'][1]:.1f}, past the clip's near face {CLIP_NEAR_Y}")
        if shot["hand"][1] <= CLIP_NEAR_Y:
            failures.append(f"the hand did not reach through the clip (y {shot['hand'][1]:.1f})")
        if shot["marker"] is None or abs(shot["marker"][1] - CLIP_NEAR_Y) > 2.0:
            failures.append(f"the aim marker should sit on the clip where the rocket bursts: {shot['marker']}")

    # --- the machine gun through the floor
    floor = [s for s in shots(section(text, "VR_WALL_FLOOR", "VR_WALL_FLOOR_DONE")) if s["kind"] == "hitscan"]
    if not floor:
        failures.append("vr_debug logged no hand-held hitscan shot through the floor")
    else:
        shot = floor[0]
        if shot["hand"][2] >= FLOOR_Z:
            failures.append(f"the hand did not reach through the floor (z {shot['hand'][2]:.1f})")
        if not shot["cleared"] or shot["start"][2] <= FLOOR_Z:
            failures.append(f"a shot with the hand through the floor started at z {shot['start'][2]:.1f}, "
                            f"below the floor at {FLOOR_Z}")
        if abs(shot["impact"][2] - FLOOR_Z) > 2.0:
            failures.append(f"the shot aimed down from above the floor should strike it: impact {shot['impact']}")
        if shot["marker"] is None or math.dist(shot["marker"], shot["impact"]) > 2.0:
            failures.append(f"the aim marker {shot['marker']} is not where the shot lands {shot['impact']}")

    # --- crouched: the anchor is the body's own eye
    crouch_text = section(text, "VR_WALL_CROUCH", "VR_WALL_CROUCH_DONE")
    crouch = [s for s in shots(crouch_text) if s["kind"] == "hitscan"]
    if not crouch:
        failures.append("vr_debug logged no hand-held shot while crouched")
    else:
        eye = smoke.view(text, "VR_WALL_CROUCH")
        if eye[2] > -3660.0 - 20.0:
            failures.append(f"the head half a metre down did not crouch the body (eye z {eye[2]:.1f})")
        if abs(crouch[0]["anchor"][2] - eye[2]) > 1.0:
            failures.append(f"crouched, the clearance started at z {crouch[0]['anchor'][2]:.1f}, "
                            f"not the body's eye at {eye[2]:.1f}")

    for failure in failures:
        print("FAIL:", failure)
    print(f"OpenXR VR wall clearance ({options.render_api}): {'FAILED' if failures else 'passed'}; "
          f"rocket splash {damage[:1]}; evidence={profile}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
