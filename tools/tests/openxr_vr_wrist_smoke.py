#!/usr/bin/env python3
"""Captures of the VR wrist displays (vr_hudMode 1, VR-09) on the simulated headset.

The simulated OpenXR runtime (tools/tests/openxr) holds both hands up in front
of a head that looks down at them, as a player glances at a watch, and
captures both eyes and the HUD's layer; console commands set what the
displays show (testStatusBar, testDamage, the weapon wheel's test command),
so nothing synthesises input. One run per case:

    sp          Quake 4, airdefense1: the machinegun just chosen (its name), at
                rest, low health with an empty magazine (RELOAD), a pickup, an
                objective and a boss, the blaster's infinity, a hit's
                direction round the line of sight, and the same left-handed
    strogg      Quake 4 after the Stroggification (storage2's player_strogg):
                the Strogg HUD's colours
    awakening   Quake 4: The Awakening (fs_game q4xbase), its first level
    mp          a multiplayer match (mp/q4dm1, a listen server)

    python tools/tests/openxr_vr_wrist_smoke.py --build-dir <builddir> [--staged .install]
        [--cases sp,strogg,awakening,mp] [--awakening-home .home/q4xbase]

Writes <output>/<case>-<api>/<shot>.png (the left eye, the right one for the
left-handed shot, brightened as the headset shows it) and sheet-<api>.png. It checks that every capture was
written and lit, that with the status on the wrists the HUD's layer in front
of the eyes stays empty (but for a hit's arcs, which draw in the eyes), and
that the HUD choice (vr_hudMode 0) puts the HUD back on it (multiplayer keeps
its score, timer and chat overlay there either way); judging the
displays and their legibility is for the eye and a headset
(docs/dev/vr-headset-qualification.md).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))
from openxr_vr_playspace_smoke import POSE, frame_guarded  # noqa: E402

# The glance: the head 1.62 m up looking 38 degrees down at both hands held
# before the chest, the weapon hand to the right (stage space: metres, y up,
# -z ahead; yaw turns left). Left-handed, everything mirrors across x = 0.
GLANCE_HEAD = (0.0, -38.0, 0.0, 0.0, 1.62, 0.0)
WEAPON_HAND = (0.17, 1.22, -0.34, 10.0, 0.0, 0.0)
OFF_HAND = (-0.15, 1.24, -0.32, -15.0, 10.0, 0.0)
GLANCE_LENS = 64.0
# A hit is read level, where the ring sits round the line of sight.
LEVEL_HEAD = (0.0, 0.0, 0.0, 0.0, 1.62, 0.0)

CASES = ("sp", "strogg", "awakening", "mp")


def pose(kind: str, side: str, values, left: bool) -> str:
    if kind == "head":
        yaw, pitch, roll, x, y, z = values
        if left:
            yaw, roll, x = -yaw, -roll, -x
        return f"head {yaw:g} {pitch:g} {roll:g} {x:g} {y:g} {z:g}"
    x, y, z, yaw, pitch, roll = values
    if left:
        x, yaw, roll = -x, -yaw, -roll
    return f"hand {side} {x:g} {y:g} {z:g} {yaw:g} {pitch:g} {roll:g}"


class Shots:
    """The frame script and console commands of one run."""

    def __init__(self, profile: Path, game_dir: str) -> None:
        self.profile = profile
        self.game = profile / game_dir
        self.script: list[str] = []
        self.commands: list[str] = []
        self.names: list[str] = []

    def marker(self, name: str) -> str:
        return str(self.game / f"vr_marker_{name}.txt")

    def glance(self, name: str, left: bool = False, head=GLANCE_HEAD, settle: int = 600) -> None:
        weapon_side, off_side = ("left", "right") if left else ("right", "left")
        for command in (f"lens {GLANCE_LENS:g}", pose("head", "", head, left), pose("hand", weapon_side, WEAPON_HAND, left),
                        pose("hand", off_side, OFF_HAND, left), f"button {off_side} squeeze 0", f"button {weapon_side} squeeze 0"):
            self.script.append(f"when {self.marker('pose_' + name)} {command}")
        self.commands += [f"condump vr_marker_pose_{name}.txt", f"waitMsec {settle}"]

    def capture(self, name: str) -> None:
        self.script.append(f"when {self.marker('cap_' + name)} capture {self.profile / ('xr_' + name)}")
        self.commands += [f"condump vr_marker_cap_{name}.txt", "waitMsec 400"]
        self.names.append(name)


def weapon_slot(slot: int) -> list[str]:
    return ["testWeaponWheel open", "waitMsec 150", f"testWeaponWheel slot {slot}", "waitMsec 150", "testWeaponWheel release"]


def build_case(case: str, profile: Path, game_dir: str) -> Shots:
    shots = Shots(profile, game_dir)
    c = shots.commands
    c += ["waitMsec 2500", "god", "notarget", "vr_recenter", "waitMsec 400", "vr_aimLaser 0", "vr_leftHanded 0",
          "ui_autoSwitch 0", "echo VR_WRIST_ACTIVE"]
    if case == "mp":
        # the match's own weapon; the wheel is single player's
        shots.glance("mp_rest", settle=1200)
        shots.capture("mp_rest")
        c += ["testStatusBar stats 20 35", "testStatusBar consume 100", "waitMsec 300"]
        shots.capture("mp_low")
        shots.glance("mp_hit", head=LEVEL_HEAD)
        c += ["god", "testDamage damage_triggerhurt_5 90", "god", "waitMsec 150"]
        shots.capture("mp_hit")
        return shots
    c += ["give all", "waitMsec 500"]
    # the machinegun, its name showing for a moment after the change
    shots.glance(f"{case}_weapon")
    c += [*weapon_slot(1), "waitMsec 900"]
    shots.capture(f"{case}_weapon")
    c += ["waitMsec 2500"]
    shots.capture(f"{case}_rest")
    # low health, the magazine run dry
    c += ["testStatusBar stats 18 0", "testStatusBar consume 200", "waitMsec 300"]
    shots.capture(f"{case}_low")
    if case != "sp":
        return shots
    # a pickup, a new objective and a boss on the off hand
    c += ["testStatusBar stats 100 50", "testStatusBar pickup item_armor_large", "waitMsec 100",
          'testStatusBar objective "Reach the communications tower"', "testStatusBar boss", "waitMsec 300"]
    shots.capture("sp_notices")
    # the blaster needs no ammunition
    c += [*weapon_slot(0), "waitMsec 3000"]
    shots.capture("sp_blaster")
    # a hit from the left, round the line of sight
    shots.glance("sp_hit", head=LEVEL_HEAD)
    c += ["god", "testDamage damage_triggerhurt_5 90", "god", "waitMsec 150"]
    shots.capture("sp_hit")
    # the other choice, HUD: the status back in front of the eyes, no wrists
    c += ["vr_hudMode 0", "waitMsec 400"]
    shots.capture("sp_hudmode")
    c += ["vr_hudMode 1", "waitMsec 200"]
    # left-handed: the wrists swap
    c += ["vr_leftHanded 1", "waitMsec 300", *weapon_slot(1), "waitMsec 3000"]
    shots.glance("sp_left", left=True)
    shots.capture("sp_left")
    c += ["vr_leftHanded 0"]
    return shots


def launch_args(case: str) -> list[str]:
    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    name = "(MP) Main menu" if case == "mp" else "(SP) Main menu — GL"
    launch = next(c for c in launches["configurations"] if c["name"].startswith(name))
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    if case == "mp":
        # the launch starts at the join screen; this player joins its own match
        for i in range(len(args) - 2):
            if args[i] == "+set" and args[i + 1] == "ui_autoJoin":
                args[i + 2] = "1"
    return args


def run_case(case: str, options, staged: Path, executable: str, manifest: Path, profile: Path) -> list[str]:
    # the markers live in the game directory the client writes to
    game_dir = "q4xbase" if case == "awakening" else "baseoq4"
    shots = build_case(case, profile, game_dir)
    game = profile / game_dir
    game.mkdir(parents=True, exist_ok=True)
    for stale in game.glob("vr_marker_*.txt"):
        stale.unlink()
    for stale in profile.glob("xr_*.tga"):
        stale.unlink()
    log_path = game / "logs/openq4.log"
    log_path.unlink(missing_ok=True)
    (profile / "xr_script.txt").write_text("\n".join(shots.script) + "\n", encoding="utf-8")
    commands = shots.commands + ["echo VR_WRIST_COMPLETE", "quit"]
    (game / "openxr_vr_wrist_smoke.cfg").write_text("\n".join(frame_guarded(commands)) + "\n", encoding="utf-8")

    settings = {
        "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_wrist_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        # the profile persists between runs: every setting this smoke reads starts fresh
        "vr_enable": "1", "vr_setupDone": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_roomScale": "0",
        "vr_physicalCrouch": "0", "vr_playSpace": "2", "vr_standingHeight": "0", "vr_body": "2",
        "vr_hudMode": "1", "vr_wristWidth": "0.11", "vr_comfortVignette": "0", "ui_hudDamageDirection": "1",
        "r_renderApi": options.render_api,
    }
    args = launch_args(case)
    if case == "awakening":
        settings["fs_game"] = "q4xbase"
    if case == "mp":
        settings.update({"net_serverDedicated": "0", "si_gameType": "DM", "sv_cheats": "1", "net_allowCheats": "1",
                         "net_serverAllowServerMod": "0", "net_ip": "127.0.0.1", "net_LANServer": "1"})
    for key, value in settings.items():
        args += ["+set", key, value]
    level = {"sp": "game/airdefense1", "strogg": "game/storage2", "awakening": "game/m01_stranarus_trench1"}.get(case)
    args += ["+spawnServer", "mp/q4dm1"] if case == "mp" else ["+map", level]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    environment = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_")}
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(profile / "xr.jsonl"),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": options.eye,
        "OPENQ4_XR_TEST_RATE": "90",
        "OPENQ4_XR_TEST_SPACES": "stage",
        "SDL_AUDIODRIVER": "dummy",
    })
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    with (profile / "process.log").open("w", encoding="utf-8") as log:
        try:
            result = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT,
                                    env=environment, startupinfo=startup, timeout=options.timeout)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"the client did not finish within {options.timeout} s ({case})")
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    failures = []
    if "OpenXR: presenting to the headset" not in text:
        failures.append(f"{case}: the session never began")
    if result.returncode != 0:
        failures.append(f"{case}: exit code {result.returncode}")
    if "VR_WRIST_COMPLETE" not in text:
        failures.append(f"{case}: the script did not complete")
    errors = re.findall(r".*(?:FATAL|ERROR:|OpenXR: .* failed|Unknown command).*", text)
    if errors:
        failures.append(f"{case}: errors in the log: {errors[:3]}")
    return failures + [f"@{name}" for name in shots.names]


def prepare_awakening(profile: Path, home: Path) -> None:
    """A private q4xbase save path that sees the expansion's content without writing into it: its content
    directories linked file by file (hard links), never logs, saves, caches or configs."""
    target = profile / "q4xbase"
    target.mkdir(parents=True, exist_ok=True)
    skip = {"logs", "savegames", "generated", "cfg", "_oq4"}
    for entry in home.iterdir():
        if entry.name in skip or entry.name.endswith((".cfg", ".dat", ".log")) or entry.name.startswith("vr_marker_"):
            continue
        destination = target / entry.name
        if destination.exists():
            continue
        if entry.is_dir():
            shutil.copytree(entry, destination, copy_function=os.link)
        else:
            os.link(entry, destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir")
    parser.add_argument("--staged", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, default=Path(r"E:\SteamLibrary\steamapps\common\Quake 4"))
    parser.add_argument("--awakening-home", type=Path, default=ROOT / ".home" / "q4xbase",
                        help="a q4xbase directory holding The Awakening's content")
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp" / "vr-wrist-smoke")
    parser.add_argument("--eye", default="1200x1320", help="eye size the runtime recommends")
    parser.add_argument("--timeout", type=int, default=600)
    options = parser.parse_args()

    cases = [c for c in options.cases.split(",") if c]
    for case in cases:
        if case not in CASES:
            parser.error(f"unknown case {case} (one of {', '.join(CASES)})")
    manifest = options.build_dir.resolve() / "openq4_xr_test_runtime.json"
    if not manifest.exists():
        parser.error(f"no simulated runtime at {manifest}: build the tree first")
    staged = options.staged.resolve()
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    if not (staged / executable).is_file():
        parser.error(f"no staged client in {staged}")
    output = options.output.resolve()

    from PIL import Image, ImageStat

    failures: list[str] = []
    pictures: list[tuple[str, Image.Image]] = []
    for case in cases:
        profile = output / f"{case}-{options.render_api}"
        if case == "awakening":
            if not (options.awakening_home / "maps").is_dir():
                failures.append(f"awakening: no expansion content at {options.awakening_home}")
                continue
            prepare_awakening(profile, options.awakening_home.resolve())
        result = run_case(case, options, staged, executable, manifest, profile)
        failures += [r for r in result if not r.startswith("@")]
        for name in (r[1:] for r in result if r.startswith("@")):
            eyes = [profile / f"xr_{name}_{eye}.tga" for eye in ("left", "right")]
            quad = profile / f"xr_{name}_quad1.tga"  # the HUD's layer, after the eyes' projection
            if not all(path.exists() for path in eyes):
                failures.append(f"{case}: {name} was not captured")
                continue
            # left-handed, the right eye sees the swapped wrists whole
            left = Image.open(eyes[1] if name.endswith("_left") else eyes[0]).convert("RGB")
            if max(ImageStat.Stat(left).mean) < 2.0:
                failures.append(f"{case}: {name} is black")
            # the HUD's layer carries nothing of the status in front of the eyes:
            # away from the hit and captions it is empty
            if not quad.exists():
                failures.append(f"{case}: {name}: no capture of the HUD's layer")
            elif not name.endswith("_hit") and case != "mp":
                # (multiplayer keeps its score, timer and chat overlay in front, the user's choice)
                coverage = ImageStat.Stat(Image.open(quad).convert("RGBA").getchannel("A")).mean[0]
                if name.endswith("_hudmode") and coverage <= 1.0:
                    failures.append(f"{case}: {name}: the HUD choice shows no HUD in front of the eyes")
                elif not name.endswith("_hudmode") and coverage > 1.0:
                    failures.append(f"{case}: {name}: the HUD's layer is not empty in front of the eyes ({coverage:.1f})")
            shown = left.point(lambda v: int(255.0 * (v / 255.0) ** 0.6))
            shown.save(profile / f"{name}.png")
            pictures.append((f"{case}: {name}", shown))

    if pictures:
        w, h = pictures[0][1].size
        scale = 0.4
        tw, th = int(w * scale), int(h * scale)
        columns = 4
        rows = (len(pictures) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * tw, rows * th), (0, 0, 0))
        for i, (_label, picture) in enumerate(pictures):
            sheet.paste(picture.resize((tw, th), Image.LANCZOS), ((i % columns) * tw, (i // columns) * th))
        sheet.save(output / f"sheet-{options.render_api}.png")

    for failure in failures:
        print("FAIL:", failure)
    print(f"OpenXR VR wrist displays ({options.render_api}): {len(pictures)} captures; evidence={output}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
