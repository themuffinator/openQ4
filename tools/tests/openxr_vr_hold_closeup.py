#!/usr/bin/env python3
"""Close-up captures of the VR weapon hold, right- and left-handed, gun by gun.

The simulated OpenXR runtime (tools/tests/openxr) frames each held gun from a
few fixed places with a narrow lens ("lens" in its frame script), so the hands
on the grip fill the picture. Every left-handed framing is the right-handed one
mirrored, so a pair sheet sets each right-handed capture beside the
left-handed one flipped back: the arms should match, the gun itself stays as
modelled.

    python tools/tests/openxr_vr_hold_closeup.py --build-dir builddir
        [--render-api gl|vulkan] [--weapons machinegun,shotgun] [--framings front,free]

Writes <output>/<weapon>_<framing>_<hand>.png, a <weapon>_pairs.png sheet per
gun and sheet.png with every gun. It checks only that every capture was
written and is lit; judging the hold is for the eye.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))
import openxr_vr_playspace_smoke as smoke  # noqa: E402

# the weapon wheel slot of each stock gun, as player.def numbers them
WEAPONS = {
    "blaster": 0, "machinegun": 1, "shotgun": 2, "hyperblaster": 3, "grenadelauncher": 4, "nailgun": 5,
    "rocketlauncher": 6, "railgun": 7, "lightninggun": 8, "dmg": 9, "napalmgun": 10,
}

# Right-handed framings, in the stage space (metres, y up, -z ahead; yaw turns
# left): the head, the weapon hand, the support hand, whether the support hand
# holds the gun (two-handed) and the lens across each eye (degrees). The
# left-handed framing is each one mirrored across x = 0.
FRAMINGS = {
    # both hands on the gun, seen from behind and above it
    "front": ((0.0, -28.0, 0.0, 0.0, 1.62, 0.0), (0.05, 1.40, -0.36, 0.0, 0.0, 0.0), (0.05, 1.38, -0.60, 0.0, 0.0, 0.0), True, 64.0),
    # the support hand let go, held off to the side
    "free": ((0.0, -28.0, 0.0, 0.0, 1.62, 0.0), (0.05, 1.40, -0.36, 0.0, 0.0, 0.0), (-0.28, 1.28, -0.34, 25.0, -10.0, 0.0), False, 70.0),
    # the gun turned across the view, the grip hand's knuckles towards the
    # head (the body follows the head, so the head stays put)
    "side": ((0.0, -30.0, 0.0, 0.0, 1.62, 0.0), (0.10, 1.38, -0.38, 70.0, 0.0, 0.0), (-0.30, 1.20, -0.20, 20.0, -20.0, 0.0), False, 56.0),
    # straight down on the grip
    "top": ((0.0, -80.0, 0.0, 0.05, 1.82, -0.36), (0.05, 1.40, -0.36, 0.0, 0.0, 0.0), (0.05, 1.38, -0.60, 0.0, 0.0, 0.0), True, 60.0),
}


def head_command(head, left: bool) -> str:
    yaw, pitch, roll, x, y, z = head
    if left:
        yaw, roll, x = -yaw, -roll, -x
    return f"head {yaw:g} {pitch:g} {roll:g} {x:g} {y:g} {z:g}"


def hand_command(side: str, pose, left: bool) -> str:
    x, y, z, yaw, pitch, roll = pose
    if left:
        x, yaw, roll = -x, -yaw, -roll
    return f"hand {side} {x:g} {y:g} {z:g} {yaw:g} {pitch:g} {roll:g}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir")
    parser.add_argument("--staged", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, default=Path(r"E:\SteamLibrary\steamapps\common\Quake 4"))
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--weapons", default="machinegun,shotgun,hyperblaster,nailgun,railgun,rocketlauncher")
    parser.add_argument("--framings", default=",".join(FRAMINGS))
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp" / "vr-hold-closeup")
    parser.add_argument("--eye", default="960x1056", help="eye size the runtime recommends")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    weapons = [w for w in args.weapons.split(",") if w]
    framings = [f for f in args.framings.split(",") if f]
    for name in weapons:
        if name not in WEAPONS:
            parser.error(f"unknown weapon {name} (one of {', '.join(WEAPONS)})")
    for name in framings:
        if name not in FRAMINGS:
            parser.error(f"unknown framing {name} (one of {', '.join(FRAMINGS)})")
    manifest = args.build_dir.resolve() / "openq4_xr_test_runtime.json"
    if not manifest.exists():
        parser.error(f"no simulated runtime at {manifest}: build the tree first")

    profile = args.output.resolve() / args.render_api
    game = profile / "baseoq4"
    marker = lambda name: str(game / f"vr_marker_{name}.txt")  # noqa: E731

    script: list[str] = []
    commands = ["waitMsec 2500", "god", "notarget", f"setviewpos {smoke.POSE}", "waitMsec 800", "vr_recenter",
                "waitMsec 400", "vr_roomScale 1", "vr_aimLaser 0", "vr_body 2", "vr_leftHanded 0", "ui_autoSwitch 0", "give all",
                "waitMsec 500"]
    shots: list[tuple[str, str, str]] = []
    # Every gun right-handed, then every gun left-handed: the wheel swaps back to
    # the last weapon when asked for the one already held, so no gun is asked
    # for twice in a row ("give all" without the auto-switch leaves the blaster
    # up).
    current = "blaster"
    for hand in ("right", "left"):
        left = hand == "left"
        # Both grips let go before the hands swap: a grip still held would turn
        # into the other hand's grip, and its binding would change the weapon.
        script += [f"when {marker('release_' + hand)} button left squeeze 0",
                   f"when {marker('release_' + hand)} button right squeeze 0"]
        commands += [f"condump vr_marker_release_{hand}.txt", "waitMsec 300",
                     f"vr_leftHanded {1 if left else 0}", "waitMsec 300"]
        for weapon in weapons:
            if weapon != current:
                commands += ["testWeaponWheel open", "waitMsec 150", f"testWeaponWheel slot {WEAPONS[weapon]}",
                             "waitMsec 150", "testWeaponWheel release", "waitMsec 1800"]
                current = weapon
            for framing in framings:
                head, gun_hand, support, two_handed, lens = FRAMINGS[framing]
                name = f"{weapon}_{framing}_{hand}"
                weapon_side, support_side = ("left", "right") if left else ("right", "left")
                for command in (f"lens {lens:g}", head_command(head, left), hand_command(weapon_side, gun_hand, left),
                                hand_command(support_side, support, left),
                                f"button {support_side} squeeze {1 if two_handed else 0}",
                                f"button {weapon_side} squeeze 0"):
                    script.append(f"when {marker('pose_' + name)} {command}")
                script.append(f"when {marker('cap_' + name)} capture {profile / ('xr_' + name)}")
                commands += [f"condump vr_marker_pose_{name}.txt", "waitMsec 900",
                             f"condump vr_marker_cap_{name}.txt", "waitMsec 300"]
                shots.append((weapon, framing, hand))
    commands += ["vr_leftHanded 0", "echo VR_PLAYSPACE_COMPLETE", "quit"]

    os.environ.setdefault("OPENQ4_ALLOW_STALE_RUNTIME", "1")
    original_run = smoke.subprocess.run

    def run_with_eye(command, **kwargs):
        kwargs["env"]["OPENQ4_XR_TEST_EYE_SIZE"] = args.eye
        return original_run(command, **kwargs)

    smoke.subprocess.run = run_with_eye
    options = SimpleNamespace(basepath=args.basepath, timeout=args.timeout, render_api=args.render_api)
    staged = args.staged.resolve()
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    smoke.run(options, staged, executable, manifest, profile, "stage", commands, script)

    from PIL import Image, ImageOps, ImageStat

    failures = []
    images: dict[tuple[str, str, str], Image.Image] = {}
    for weapon, framing, hand in shots:
        # each hand's own eye: the left eye of a right-handed framing mirrors the
        # right eye of the left-handed one
        eye = "left" if hand == "right" else "right"
        path = profile / f"xr_{weapon}_{framing}_{hand}_{eye}.tga"
        if not path.exists():
            failures.append(f"{path.name} was not written")
            continue
        image = Image.open(path).convert("RGB").point(lambda v: int(255.0 * (v / 255.0) ** 0.6))
        if max(ImageStat.Stat(image).mean) < 4.0:
            failures.append(f"{path.name} is black")
        image.save(profile / f"{weapon}_{framing}_{hand}.png")
        images[(weapon, framing, hand)] = image

    def sheet(rows: list[list[Image.Image]], path: Path, scale: float) -> None:
        if not rows or not rows[0]:
            return
        w, h = rows[0][0].size
        tw, th = int(w * scale), int(h * scale)
        columns = max(len(r) for r in rows)
        out = Image.new("RGB", (columns * tw, len(rows) * th), (0, 0, 0))
        for y, row in enumerate(rows):
            for x, image in enumerate(row):
                out.paste(image.resize((tw, th), Image.LANCZOS), (x * tw, y * th))
        out.save(path)

    every = []
    for weapon in weapons:
        rows = []
        for framing in framings:
            right, left = images.get((weapon, framing, "right")), images.get((weapon, framing, "left"))
            if right is not None and left is not None:
                rows.append([right, ImageOps.mirror(left)])
        sheet(rows, profile / f"{weapon}_pairs.png", 0.5)
        row = [images[(weapon, f, h)] for f in framings for h in ("right", "left") if (weapon, f, h) in images]
        if row:
            every.append(row)
    sheet(every, profile / "sheet.png", 0.25)

    for failure in failures:
        print("FAIL:", failure)
    print(f"OpenXR VR hold close-ups ({args.render_api}): {len(images)}/{len(shots)} captures; evidence={profile}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
