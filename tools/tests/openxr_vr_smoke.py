#!/usr/bin/env python3
"""Play single player in VR against the openQ4 OpenXR test runtime (requires
Pillow, a staged client built with OpenXR, and retail assets).

The test runtime (tools/tests/openxr/OpenXRTestRuntime.cpp, built beside the
client) simulates a stereo headset with off-axis eyes and two controllers. It
is selected for this process only through XR_RUNTIME_JSON, so no system-wide
OpenXR registration is read or changed. No keyboard or mouse input is
synthesised: console commands drive the game, and the runtime's frame script
is synchronised to them through marker files the engine writes with condump.

With --render-api vulkan the client runs the Vulkan renderer: the runtime
creates its instance and device (XR_KHR_vulkan_enable2), the session binds
them, and --validation turns the Khronos validation layer on and fails on any
message it reports. Every other check is the same.

Checks:
- the session binds the renderer's OpenGL context, or the Vulkan device the
  runtime created, and presents loading as a quad layer before gameplay;
- gameplay submits a stereo projection layer plus the HUD quad, both eyes are
  lit and differ (parallax), and a scripted head turn changes what they see;
- with game time stopped, the weapon hand's aim marker (vr_aimLaser) is the
  only difference between captures: one small red dot in each eye, at the
  same height in both, whose disparity puts it in front of the player at a
  plausible range, and a beam that adds more; left-handed (vr_leftHanded)
  the left controller aims instead, so its dot lands left of the right
  hand's;
- two hands: the off-hand grip squeezed on the foregrip raises the aim (the
  dot in both eyes), buzzes the off hand and opens no weapon wheel;
- the comfort vignette: a smooth turn at full strength blacks out the eyes'
  top corners and leaves their centres clear;
- the trigger fires the weapon through its default binding, and each shot
  pulses the weapon hand's controller (a 40 ms vibration on the right hand);
- the machinegun's zoom magnifies the eyes and puts no scope picture on the
  head-locked HUD;
- the weapon wheel: the off-hand grip squeezed away from the gun opens it,
  raising the weapon hand hovers the blaster at its top and releasing the grip
  switches to it (under the retained interface the hand's turn once went
  unread, so the wheel could only ever keep the weapon in hand);
- room scale: half a metre's step forward walks the body after the head, so
  the head ends up within a few units of the tracking origin, while with
  vr_roomScale 0 the same step leaves the whole distance as a lean;
- physical crouch: a head 0.5 m down crouches the body (its eye drops by the
  crouch) and raising it stands the body again;
- the off-hand stick walks the body where the head faces, and with
  vr_moveDirection 1 where the off hand points;
- a world GUI (airdefense1's exit panel, never clicked) takes focus where the
  weapon hand points, with its cursor where the hand's ray meets it, while
  the eye's ray along the same aim passes over the panel;
- culling hides nothing either eye shows: over open ground, with game time
  stopped, turning r_useCulling off leaves both eyes as they were (unnormalized
  off-axis frustum planes once culled a whole outdoor area from one eye);
- a stick snap turn turns the body by vr_snapTurnAngle and a controller
  trigger press reaches the binding system;
- the controller's menu button opens the pause menu on an opaque, world-locked
  virtual screen in front of the still-rendered world, the weapon hand points
  at it (the pointer is drawn where the ray meets the screen) and the trigger
  clicks Resume there, which closes the menu even though the desktop window
  holds no focus;
- losing the controllers to the runtime (its own menu, or the headset coming
  off) pauses single player on the pause menu, which stays up when they return;
- a vehicle in stereo: a walker, spawned and entered by script and turned 45
  degrees in the same breath (as m07's race turns its speeder), seats the
  player facing its new heading, turns its cockpit 45 degrees after the head
  while the eye, on the cockpit's turning axis, stays put, and the headset
  keeps the stereo view;
- the runtime can end VR: the game carries on on the desktop, keeps
  vr_enable for the next launch and waits for vr_restart;
- the log carries no errors.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]

# Where the scripted controller points: straight ahead from this far right of
# and above the eyes, onto a screen vr_screenWidth metres wide hung straight
# ahead (the simulated head looks down -Z from 1.6 m). This is the pause
# menu's Resume row.
POINTER_RIGHT_M = -0.84
POINTER_UP_M = 0.15
SCREEN_WIDTH_M = 3.0

# The test runtime's eyes (OpenXRTestRuntime.cpp EyeFov, TR_IPD): signed
# angles of each frustum, and their separation in metres.
EYE_FOV = {"left": (-0.94, 0.78, 0.86, -0.92), "right": (-0.78, 0.94, 0.86, -0.92)}
EYE_SEPARATION_M = 0.064
# For the aim marker the weapon hand points 10 degrees down from 0.35 m below
# the eyes, so the shot meets the ground a few metres ahead.
LASER_HAND_POSE = "0.2 1.25 -0.35 0 -10 0"
# Left-handed (vr_leftHanded), the same aim from the left controller, 0.4 m
# to the left, so its dot lands left of the right hand's.
LEFT_LASER_HAND_POSE = "-0.2 1.25 -0.35 0 -10 0"
# The off hand on the foregrip: 0.3 m along the laser pose's aim (10 degrees
# down) and 5 cm above it, so holding the gun in both hands levels the aim.
FOREGRIP_POSE = "0.2 1.248 -0.645 0 0 0"
# Open ground ahead of airdefense1's start for a spawned walker; airdefense1
# has walkers of its own, so it is precached.
VEHICLE_ORIGIN = "10094 -6825 60"
# Where the stick walks start: the smoke's standing spot near airdefense1's
# start, facing the battlefield (x y z yaw, as getviewpos prints it).
STICK_WALK_POSE = "10310.89 -6950.34 6.96 150.7"
# airdefense1's one interactive world GUI is its exit panel, a 26-unit screen
# at x 8367.875 facing west with its centre at y -3488, z 80. The eye goes
# 29.5 units above that centre and the hand 0.75 m (29.5 units) below the head,
# 0.2 m right of it, both aiming level and due east: only the hand's ray meets
# the panel, at its centre. Nothing clicks it, since it ends the level.
GUI_POSE = "8320 -3480.1 109.5 0"
GUI_HAND_POSE = "0.2 0.85 -0.35 0 0 0"
# Over the open ground past the start, looking northwest: unnormalized off-axis
# frustum planes once culled the whole outdoor area's world model (terrain and
# the Strogg base) from the left eye here, the right eye still drawing it.
CULL_POSE = "10094 -6825 130 150.7"


def read_events(path: Path) -> list[dict]:
    events = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def body_yaws(text: str) -> list[float]:
    return [float(value) for value in re.findall(r"body yaw (-?\d+(?:\.\d+)?)", text)]


def yaw_delta(before: float, after: float) -> float:
    return (after - before + 180.0) % 360.0 - 180.0


def frame_guarded(commands: list[str]) -> list[str]:
    """Lets frames run after every real-time wait and before every marker.

    The runtime acts on the frame after a marker appears, and the script's
    waits are in real time. A busy machine (peers building and testing on it)
    can drop the stereo world to a few frames a second or stall it for over a
    second: one frame begun before a cvar change then reached a capture after
    it (a "laser off" frame still showed the dot), and a 1.2 s wait passed
    with no frame at all, so vr_status reported the head before it moved.
    """
    guarded = []
    for command in commands:
        if command.startswith("condump vr_marker_"):
            guarded.append("wait 2")
        guarded.append(command)
        if command.startswith("waitMsec "):
            guarded.append("wait 3")
    return guarded


def changed_box(before, after, threshold=48):
    """The bounding box of pixels whose largest channel changed by more than threshold."""
    from PIL import ImageChops
    difference = ImageChops.difference(before, after)
    mask = difference.split()[0].point(lambda v: 255 if v > threshold else 0)
    for channel in difference.split()[1:]:
        mask = ImageChops.lighter(mask, channel.point(lambda v: 255 if v > threshold else 0))
    count = mask.histogram()[255]
    return mask.getbbox(), count


def eye_tangents(eye: str, x: float, y: float, width: int, height: int) -> tuple[float, float]:
    """A pixel position on an eye image as tangents right and up of that eye's axis."""
    left, right, up, down = (math.tan(a) for a in EYE_FOV[eye])
    u = (x + 0.5) / width
    v = (y + 0.5) / height
    return left + u * (right - left), up - v * (up - down)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--map", default="game/airdefense1")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--validation", action="store_true",
                        help="Vulkan: run with the Khronos validation layer and fail on its messages")
    parser.add_argument("--eye-size", default="1200x1320",
                        help="the simulated headset's eye size; smaller keeps a slow GPU above the 10 fps "
                             "the comfort vignette's turn rate needs (each frame's turn step stops at 0.1 s)")
    options = parser.parse_args(argv)
    vulkan = options.render_api == "vulkan"
    from PIL import Image, ImageChops, ImageStat

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)

    output = (options.output_dir or ROOT / ".tmp/openxr-vr-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / ( "sp-vulkan" if vulkan else "sp" )
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in list(game.glob("vr_marker_*.txt")) + list(profile.glob("xr_*.tga")) + list(profile.glob("xr_*.png")):
        stale.unlink()
    xr_log = profile / "xr.jsonl"
    xr_log.unlink(missing_ok=True)

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    # The runtime acts on the frame after each marker appears.
    script = [
        f"when {marker('gameplay')} capture {profile / 'xr_gameplay'}",
        f"when {marker('laser_aim')} hand right {LASER_HAND_POSE}",
        f"when {marker('laser_off')} capture {profile / 'xr_laser_off'}",
        f"when {marker('laser_dot')} capture {profile / 'xr_laser_dot'}",
        f"when {marker('laser_beam')} capture {profile / 'xr_laser_beam'}",
        f"when {marker('left_aim')} hand left {LEFT_LASER_HAND_POSE}",
        f"when {marker('left_off')} capture {profile / 'xr_left_off'}",
        f"when {marker('left_dot')} capture {profile / 'xr_left_dot'}",
        f"when {marker('two_grab')} hand left {FOREGRIP_POSE}",
        f"when {marker('two_squeeze')} button left squeeze 1",
        f"when {marker('two_off')} capture {profile / 'xr_two_off'}",
        f"when {marker('two_dot')} capture {profile / 'xr_two_dot'}",
        f"when {marker('two_release')} button left squeeze 0",
        f"when {marker('smooth')} stick right 1 0",
        f"when {marker('vignette')} capture {profile / 'xr_vignette'}",
        f"when {marker('smooth_stop')} stick right 0 0",
        f"when {marker('laser_done')} hand right 0.2 1.25 -0.35 0 0 0",
        f"when {marker('laser_done')} hand left -0.2 1.25 -0.35 0 0 0",
        f"when {marker('fire')} button right trigger 1",
        f"when {marker('fire_release')} button right trigger 0",
        f"when {marker('zoom_off')} capture {profile / 'xr_zoom_off'}",
        f"when {marker('zoom')} button left trigger 1",
        f"when {marker('zoom_on')} capture {profile / 'xr_zoom_on'}",
        f"when {marker('unzoom')} button left trigger 0",
        f"when {marker('room_walk')} head 0 0 0 0 1.6 -0.5",
        f"when {marker('room_lean')} head 0 0 0 0 1.6 -1.0",
        f"when {marker('room_back')} head 0 0 0 0 1.6 0",
        f"when {marker('crouch')} head 0 0 0 0 1.1 0",
        f"when {marker('rise')} head 0 0 0 0 1.6 0",
        f"when {marker('stick_head')} stick left 0 1",
        f"when {marker('stick_head_stop')} stick left 0 0",
        f"when {marker('offhand_left')} hand left -0.2 1.25 -0.35 90 0 0",
        f"when {marker('stick_hand')} stick left 0 1",
        f"when {marker('stick_hand_stop')} stick left 0 0",
        f"when {marker('gui_point')} hand right {GUI_HAND_POSE}",
        f"when {marker('gui_done')} hand right 0.2 1.25 -0.35 0 0 0",
        f"when {marker('cull_on')} capture {profile / 'xr_cull_on'}",
        f"when {marker('cull_off')} capture {profile / 'xr_cull_off'}",
        f"when {marker('turn')} head 40 0 0",
        f"when {marker('turned')} capture {profile / 'xr_turned'}",
        f"when {marker('snap')} head 0 0 0",
        f"when {marker('snap')} stick right 1 0",
        f"when {marker('snapback')} stick right 0 0",
        f"when {marker('trigger')} button right trigger 1",
        f"when {marker('release')} button right trigger 0",
        f"when {marker('menu')} button left menu 1",
        f"when {marker('menu_release')} button left menu 0",
        f"when {marker('point')} hand right {POINTER_RIGHT_M} {1.6 + POINTER_UP_M} 0 0 0 0",
        f"when {marker('pointed')} capture {profile / 'xr_menu'}",
        f"when {marker('click')} button right trigger 1",
        f"when {marker('unclick')} button right trigger 0",
        f"when {marker('vehicle_look')} head -45 0 0",
        f"when {marker('vehicle_cap')} capture {profile / 'xr_vehicle'}",
        f"when {marker('vehicle_back')} head 0 0 0",
        f"when {marker('wheel_hands')} hand left -0.3 1.2 -0.2 0 0 0",
        f"when {marker('wheel_open')} button left squeeze 1",
        f"when {marker('wheel_point')} hand right 0.2 1.25 -0.35 0 20 0",
        f"when {marker('wheel_close')} button left squeeze 0",
        f"when {marker('wheel_done')} hand right {LASER_HAND_POSE}",
        f"when {marker('unfocus')} focus 0",
        f"when {marker('refocus')} focus 1",
        f"when {marker('exit')} exit",
    ]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")

    # A key binding appends to the command buffer behind whatever still waits
    # there, so the trigger's binding runs the rest of the script itself.
    commands = [
        "waitMsec 3000", "echo VR_GAMEPLAY_ACTIVE", "vr_status",
        # the profile persists between runs: start from the default trigger binding
        "bind JOY15 _attack",
        "condump vr_marker_gameplay.txt", "waitMsec 1000",
        # the aim marker against a frozen world: only the marker may change
        "condump vr_marker_laser_aim.txt", "g_stopTime 1", "vr_aimLaser 0", "waitMsec 800",
        "condump vr_marker_laser_off.txt", "waitMsec 500", "vr_aimLaser 1", "waitMsec 500",
        "condump vr_marker_laser_dot.txt", "waitMsec 500", "vr_aimLaser 2", "waitMsec 500",
        "condump vr_marker_laser_beam.txt", "waitMsec 500", "vr_aimLaser 1",
        # left-handed: the left controller aims, still against the frozen world
        "vr_leftHanded 1", "condump vr_marker_left_aim.txt", "waitMsec 400", "vr_aimLaser 0", "waitMsec 500",
        "condump vr_marker_left_off.txt", "waitMsec 500", "vr_aimLaser 1", "waitMsec 500",
        "condump vr_marker_left_dot.txt", "waitMsec 500", "vr_leftHanded 0",
        # two hands: the off-hand grip closing on the foregrip steadies the gun
        # along both palms instead of opening the weapon wheel
        "echo VR_TWO_HANDS", "condump vr_marker_two_grab.txt", "waitMsec 400",
        "condump vr_marker_two_squeeze.txt", "waitMsec 500", "vr_aimLaser 0", "waitMsec 500",
        "condump vr_marker_two_off.txt", "waitMsec 500", "vr_aimLaser 1", "waitMsec 500",
        "condump vr_marker_two_dot.txt", "waitMsec 500",
        "condump vr_marker_two_release.txt", "waitMsec 400", "echo VR_TWO_HANDS_DONE",
        # comfort: a smooth turn darkens each eye's edges around a clear centre
        "vr_turnMode 1", "vr_comfortVignette 1", "condump vr_marker_smooth.txt", "waitMsec 600",
        "condump vr_marker_vignette.txt", "waitMsec 300",
        "condump vr_marker_smooth_stop.txt", "waitMsec 300", "vr_turnMode 0", "vr_comfortVignette 0.5",
        "g_stopTime 0",
        "condump vr_marker_laser_done.txt", "waitMsec 500",
        # the right trigger's default binding fires the weapon
        "echo VR_FIRE", "condump vr_marker_fire.txt", "waitMsec 400",
        "condump vr_marker_fire_release.txt", "waitMsec 600",
        # a scoped weapon's zoom magnifies each eye and puts no scope picture
        # on the head-locked HUD while the controller aims
        # (give may switch to the new gun itself, so select it by its impulse)
        "give weapon_machinegun", "waitMsec 500", "_impulse1", "waitMsec 2500",
        "condump vr_marker_zoom_off.txt", "waitMsec 400",
        "condump vr_marker_zoom.txt", "waitMsec 1500", "condump vr_marker_zoom_on.txt", "waitMsec 400",
        "condump vr_marker_unzoom.txt", "waitMsec 800",
        # the weapon wheel: the off hand squeezes away from the gun, the weapon
        # hand rises to the blaster at the wheel's top, and letting go takes it
        "condump vr_marker_wheel_hands.txt", "waitMsec 300", "echo VR_WHEEL_BEFORE", "testWeaponWheel",
        "condump vr_marker_wheel_open.txt", "waitMsec 600",
        "condump vr_marker_wheel_point.txt", "waitMsec 600", "echo VR_WHEEL_POINTED", "testWeaponWheel",
        "condump vr_marker_wheel_close.txt", "waitMsec 1500", "echo VR_WHEEL_AFTER", "testWeaponWheel",
        "condump vr_marker_wheel_done.txt", "_impulse1", "waitMsec 2500",
        # room scale: a step forward walks the body; without it the step is a lean
        "vr_roomScale 1", "condump vr_marker_room_walk.txt", "waitMsec 1200", "echo VR_ROOM_WALKED", "vr_status",
        "vr_roomScale 0", "condump vr_marker_room_lean.txt", "waitMsec 1000", "echo VR_ROOM_LEANED", "vr_status",
        "condump vr_marker_room_back.txt", "waitMsec 600", "vr_recenter", "vr_roomScale 1", "waitMsec 600",
        # physical crouch: a head 0.5 m down crouches the body, and rising stands it
        "echo VR_STANDING", "getviewpos", "condump vr_marker_crouch.txt", "waitMsec 1500",
        "echo VR_CROUCHED", "getviewpos", "condump vr_marker_rise.txt", "waitMsec 1500",
        "echo VR_RISEN", "getviewpos",
        # walking with the off-hand stick: where the head faces, then
        # (vr_moveDirection 1) where the off hand points, 90 degrees left. It
        # tests how the stick becomes movement, so both walks start from the
        # same spot and facing (the smooth turn above ends wherever its timing
        # leaves it) and fly in noclip, where neither slope nor rock bends them
        "noclip", f"setviewpos {STICK_WALK_POSE}", "waitMsec 500",
        "echo VR_STICK_HEAD", "getviewpos", "condump vr_marker_stick_head.txt", "waitMsec 600",
        "condump vr_marker_stick_head_stop.txt", "waitMsec 600", "echo VR_STICK_HEAD_DONE", "getviewpos",
        "vr_moveDirection 1", "condump vr_marker_offhand_left.txt", f"setviewpos {STICK_WALK_POSE}", "waitMsec 500",
        "echo VR_STICK_HAND", "getviewpos", "condump vr_marker_stick_hand.txt", "waitMsec 600",
        "condump vr_marker_stick_hand_stop.txt", "waitMsec 600", "echo VR_STICK_HAND_DONE", "getviewpos",
        # a world GUI answers the weapon hand where it points, as the laser
        # does, not the eye a forearm above it
        # (the hand takes its pose first: the panel takes focus, and logs where,
        # the moment the aim meets it, and the aim is the hand's at once)
        "vr_moveDirection 0", "condump vr_marker_gui_point.txt", "waitMsec 400", f"setviewpos {GUI_POSE}", "waitMsec 900",
        "echo VR_GUI_POINTED", "condump vr_marker_gui_done.txt", "waitMsec 300",
        # culling never hides what an eye shows: in a frozen world, turning it
        # off must leave both eyes as they were
        "g_stopTime 1", f"setviewpos {CULL_POSE}", "waitMsec 800", "condump vr_marker_cull_on.txt", "waitMsec 500",
        "r_useCulling 0", "waitMsec 500", "condump vr_marker_cull_off.txt", "waitMsec 500",
        "reset r_useCulling", "g_stopTime 0",
        "noclip", f"setviewpos {STICK_WALK_POSE}", "waitMsec 500",
        "condump vr_marker_turn.txt", "waitMsec 700",
        "condump vr_marker_turned.txt", "waitMsec 700",
        "echo VR_SNAP_BEFORE", "vr_status",
        "condump vr_marker_snap.txt", "waitMsec 500",
        "condump vr_marker_snapback.txt", "waitMsec 500",
        "echo VR_SNAP_AFTER", "vr_status",
        'bind JOY15 "echo VR_TRIGGER_BIND_RAN; exec openxr_vr_smoke_menu.cfg"',
        "condump vr_marker_trigger.txt",
    ]
    menu_commands = [
        "bind JOY15 _attack",
        "condump vr_marker_release.txt", "waitMsec 500",
        "condump vr_marker_menu.txt", "waitMsec 300",
        "condump vr_marker_menu_release.txt", "waitMsec 1500",
        "echo VR_MENU_OPEN",
        "condump vr_marker_point.txt", "waitMsec 700",
        "condump vr_marker_pointed.txt", "waitMsec 700",
        "condump vr_marker_click.txt", "waitMsec 300",
        # Resume dispatches on release, then the menu takes 250 ms to go; a
        # debug build draws the paused stereo world at only ~13 fps
        "condump vr_marker_unclick.txt", "waitMsec 2000",
        # a walker in stereo: the cockpit turns after the head, about the eye
        f'spawn vehicle_walker name vr_smoke_walker origin "{VEHICLE_ORIGIN}" angle 150.7', "waitMsec 2500",
        # turned in the same breath as the seating, as m07's race turns its
        # speeder: the seat must follow the turn, not the facing it boarded at
        'script "$player1.enterVehicle( $vr_smoke_walker ); $vr_smoke_walker.setAngles( \'0 105.7 0\' );"',
        "waitMsec 3000",
        "echo VR_VEHICLE_IN", "getviewpos",
        "condump vr_marker_vehicle_look.txt", "waitMsec 2500",
        "echo VR_VEHICLE_LOOKED", "getviewpos",
        "condump vr_marker_vehicle_cap.txt", "waitMsec 500",
        "condump vr_marker_vehicle_back.txt", "waitMsec 300",
        'script "$player1.exitVehicle( 1 );"', "waitMsec 1000", "echo VR_VEHICLE_OUT",
        # the runtime's own menu takes the controllers (or the headset comes
        # off): single player pauses on the menu and stays there when they return
        "condump vr_marker_unfocus.txt", "waitMsec 1500", "echo VR_UNFOCUSED",
        "condump vr_marker_refocus.txt", "waitMsec 800", "echo VR_REFOCUSED",
        "condump vr_marker_exit.txt", "waitMsec 1500",
        "echo VR_AFTER_EXIT", "vr_status", "vr_enable",
        "echo VR_SMOKE_COMPLETE", "quit",
    ]
    (game / "openxr_vr_smoke.cfg").write_text("\n".join(frame_guarded(commands)) + "\n", encoding="utf-8")
    (game / "openxr_vr_smoke_menu.cfg").write_text("\n".join(frame_guarded(menu_commands)) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_turnMode": "0", "vr_snapTurnAngle": "45",
        "vr_screenDistance": "2.5", "vr_screenWidth": str(SCREEN_WIDTH_M), "vr_leftHanded": "0",
        # after the launch configuration's own r_renderApi, so this one wins
        "r_renderApi": options.render_api,
    }
    if vulkan:
        settings["r_vkValidation"] = "1" if options.validation else "0"
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", options.map]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")

    environment = dict(os.environ)
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(xr_log),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": options.eye_size,
        "OPENQ4_XR_TEST_RATE": "90",
    })
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    timed_out = False
    with (profile / "process.log").open("w", encoding="utf-8") as log:
        try:
            result = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT,
                                    env=environment, startupinfo=startup, timeout=options.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True

    text = (game / "logs/openq4.log").read_text(encoding="utf-8", errors="replace")
    events = read_events(xr_log)
    (profile / "xr_events.json").write_text(json.dumps(events, indent=2), encoding="utf-8")

    assert "VR_GAMEPLAY_ACTIVE" in text, "gameplay not reached"
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    if timed_out:
        assert "VR_TRIGGER_BIND_RAN" in text, "the controller trigger never reached its binding (the script waited on it)"
        raise AssertionError(f"the client did not finish within {options.timeout} s")
    assert result.returncode == 0, result.returncode
    assert "VR_SMOKE_COMPLETE" in text, "script did not complete"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), "errors in the log"

    sessions = [e for e in events if e.get("event") == "create_session"]
    if vulkan:
        assert re.search(r"Vulkan: device \d+ '", text), "the Vulkan renderer did not start"
        # the runtime made the renderer's instance and device, and the session binds that device
        assert [e.get("result") for e in events if e.get("event") == "create_vulkan_instance"][:1] == [0], \
            "the runtime did not create the Vulkan instance"
        assert [e.get("result") for e in events if e.get("event") == "create_vulkan_device"][:1] == [0], \
            "the runtime did not create the Vulkan device"
        assert "OpenXR: the runtime created the Vulkan renderer's device" in text, "the renderer made its own device"
        assert sessions and sessions[0].get("binding") == "vulkan", sessions
        assert any(e.get("event") == "create_swapchain" and e.get("api") == "vulkan" for e in events), \
            "no Vulkan swapchain was created"
        if options.validation:
            assert "Vulkan: validation enabled" in text, "the validation layer was not active"
            messages = [line for line in text.splitlines() if "VUID" in line or "Validation Error" in line]
            assert not messages, "Vulkan validation reported:\n" + "\n".join(messages[:10])
    else:
        assert sessions and sessions[0].get("binding", "").startswith("opengl_"), sessions
    assert any(e.get("event") == "begin_session" for e in events), "xrBeginSession never ran"
    # the runtime's binding UI shows these names: they come from the language table
    localized = sorted(e["localized"] for e in events if e.get("event") == "create_action")
    assert localized == sorted(["Aim pose", "Grip pose", "Trigger", "Grip", "Thumbstick", "Thumbstick click",
                                "Primary button", "Secondary button", "Menu", "Vibration"]), localized
    assert [e["localized"] for e in events if e.get("event") == "create_action_set"] == ["Gameplay"], \
        "the action set name is not localised"
    assert any(e.get("event") == "end_session" for e in events), "the runtime's exit request never ended the session"
    assert any(e.get("event") == "destroy_session" for e in events), "the session was not destroyed"
    assert not any(e.get("event") in ("end_frame_invalid", "create_session_rejected", "create_swapchain_rejected")
                   for e in events), "the runtime rejected a submission"
    frames = [e for e in events if e.get("event") == "end_frame"]
    assert any([layer["type"] for layer in f["layers"]] == ["quad"] for f in frames), \
        "loading never presented as a lone virtual-screen quad"
    stereo = [f for f in frames if [layer["type"] for layer in f["layers"]] == ["projection", "quad"]]
    assert stereo, "gameplay never presented a stereo projection layer with the HUD"
    assert any(f["layers"][1]["flags"] & 0x2 for f in stereo), \
        "the gameplay HUD quad must blend with source alpha"

    captures = {}
    for e in events:
        if e.get("event") == "capture":
            stage = next(s for s in ("gameplay", "turned", "menu", "laser_off", "laser_dot", "laser_beam",
                                     "left_off", "left_dot", "two_off", "two_dot", "vignette", "zoom_off",
                                     "zoom_on", "vehicle", "cull_on", "cull_off")
                         if f"xr_{s}_" in Path(e["path"]).name)
            captures[f"{stage}_{e['layer']}"] = e
    for name in ("gameplay_left", "gameplay_right", "gameplay_quad1", "turned_left", "turned_right", "menu_quad1",
                 "laser_off_left", "laser_off_right", "laser_dot_left", "laser_dot_right",
                 "laser_beam_left", "laser_beam_right", "left_off_left", "left_off_right", "left_dot_left",
                 "left_dot_right", "two_off_left", "two_off_right", "two_dot_left",
                 "two_dot_right", "vignette_left", "vignette_right", "zoom_off_left", "zoom_on_left",
                 "zoom_on_quad1", "vehicle_left", "vehicle_right",
                 "cull_on_left", "cull_on_right", "cull_off_left", "cull_off_right"):
        assert name in captures and captures[name]["written"], f"missing capture {name}: {sorted(captures)}"
    for name in ("gameplay_left", "gameplay_right", "turned_left", "turned_right", "vehicle_left", "vehicle_right"):
        assert captures[name]["lit_fraction"] > 0.2, f"eye capture {name} is mostly black: {captures[name]}"

    def image(prefix: str, layer: str):
        return Image.open(profile / f"{prefix}_{layer}.tga").convert("RGB")

    left, right = image("xr_gameplay", "left"), image("xr_gameplay", "right")
    eye_difference = max(ImageStat.Stat(ImageChops.difference(left, right)).mean)
    assert eye_difference > 1.0, f"the eyes are identical: no parallax ({eye_difference:.3f})"
    # culling hides nothing an eye shows: every off-axis frustum plane is a unit
    # plane, so the radius test cannot throw away a box reaching into the eye
    culled_away = {}
    for eye in ("left", "right"):
        mask = ImageChops.difference(image("xr_cull_on", eye), image("xr_cull_off", eye)).convert("L")
        mask = mask.point(lambda v: 255 if v > 24 else 0)
        culled_away[eye] = mask.histogram()[255] / (mask.size[0] * mask.size[1])
        assert culled_away[eye] < 0.005, \
            f"turning culling off changed {culled_away[eye]:.1%} of the {eye} eye: culling hid geometry the eye shows"
    turned = image("xr_turned", "left")
    turn_difference = max(ImageStat.Stat(ImageChops.difference(left, turned)).mean)
    assert turn_difference > 4.0, f"a 40 degree head turn barely changed the view ({turn_difference:.3f})"
    # room scale: the body walked the step; with it off the step stayed a lean
    def head_forward(after: str) -> float:
        found = re.search(r"head tracked \( (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) \)", text.partition(after)[2])
        assert found, f"vr_status after {after} did not report a tracked head"
        return float(found.group(1))
    walked = head_forward("VR_ROOM_WALKED")
    leaned = head_forward("VR_ROOM_LEANED")
    step_units = 0.5 * 39.37
    assert 0.0 <= walked <= 4.5, f"a 0.5 m step should walk the body and leave the head within 4 units, not {walked:.1f}"
    assert abs(leaned - walked - step_units) < 1.5, \
        f"with vr_roomScale 0 another 0.5 m step should stay a {step_units:.1f}-unit lean, moved {leaned - walked:.1f}"

    # zoom: the scoped machinegun magnifies the eye, which changes nearly every
    # pixel, and the HUD layer stays a light overlay rather than a scope picture
    zoom_difference = max(ImageStat.Stat(ImageChops.difference(image("xr_zoom_off", "left"),
                                                               image("xr_zoom_on", "left"))).mean)
    assert zoom_difference > 4.0, f"zooming the machinegun barely changed the view ({zoom_difference:.3f})"
    with Image.open(profile / "xr_zoom_on_quad1.tga") as zoom_hud:
        zoom_alpha = zoom_hud.convert("RGBA").getchannel("A")
        zoom_covered = sum(zoom_alpha.histogram()[16:]) / float(zoom_hud.width * zoom_hud.height)
    assert zoom_covered < 0.2, f"the zoomed HUD carries a head-locked scope picture ({zoom_covered:.3f} covered)"

    # physical crouch: the body's eye drops by the crouch (68 to 32 units) and comes back
    def eye_height(after: str) -> float:
        found = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)\s+-?\d", text.partition(after)[2])
        assert found, f"getviewpos after {after} printed no view position"
        return float(found.group(3))
    standing, crouched, risen = eye_height("VR_STANDING"), eye_height("VR_CROUCHED"), eye_height("VR_RISEN")
    assert 30.0 < standing - crouched < 42.0, \
        f"a head 0.5 m down should crouch the body ({standing:.1f} to {crouched:.1f})"
    assert abs(risen - standing) < 3.0, f"raising the head should stand the body again ({standing:.1f}, {risen:.1f})"

    # the off-hand stick walks where the head faces, and with vr_moveDirection 1
    # where the off hand points, here 90 degrees left of the facing
    def walked(start: str, end: str) -> tuple[float, float]:
        poses = []
        for tag in (start, end):
            found = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)\s+(-?\d+(?:\.\d+)?)",
                              text.partition(tag)[2])
            assert found, f"getviewpos after {tag} printed no view position"
            poses.append([float(found.group(i)) for i in (1, 2, 4)])
        (x0, y0, facing), (x1, y1, _) = poses
        heading = math.degrees(math.atan2(y1 - y0, x1 - x0))
        return math.hypot(x1 - x0, y1 - y0), yaw_delta(facing, heading)
    head_walk, head_off = walked("VR_STICK_HEAD", "VR_STICK_HEAD_DONE")
    assert head_walk > 10.0, f"the stick barely walked the body ({head_walk:.1f} units)"
    assert abs(head_off) < 20.0, f"the stick should walk where the head faces, walked {head_off:.0f} degrees off it"
    hand_walk, hand_off = walked("VR_STICK_HAND", "VR_STICK_HAND_DONE")
    assert hand_walk > 10.0, f"the stick barely walked the body with vr_moveDirection 1 ({hand_walk:.1f} units)"
    assert abs(hand_off - 90.0) < 25.0, \
        f"with vr_moveDirection 1 the stick should walk where the off hand points, 90 degrees left, not {hand_off:.0f}"

    # a world GUI takes focus where the weapon hand points: the eye's ray from
    # the same aim passes over the exit panel, the hand's meets its centre
    gui_focus = re.search(r"VR GUI focus func_static_53425 at (\d+\.\d+) (\d+\.\d+)",
                          text.partition("VR_STICK_HAND_DONE")[2].partition("VR_GUI_POINTED")[0])
    assert gui_focus, "pointing the weapon hand at airdefense1's exit panel did not focus it"
    gui_cursor = (float(gui_focus.group(1)), float(gui_focus.group(2)))
    assert all(abs(c - 0.5) < 0.15 for c in gui_cursor), \
        f"the exit panel's cursor should sit where the hand points, at its centre, not {gui_cursor}"

    # a shot pulses the weapon hand, and only it (a hit would pulse both, for 90 ms)
    pulses = [e for e in events if e.get("event") == "haptic" and e.get("duration_ns") == 40_000_000]
    assert pulses, "firing the weapon did not vibrate a controller"
    assert all(e.get("hand") == "/user/hand/right" and 0.4 <= e["amplitude"] <= 1.0 for e in pulses), \
        f"a shot must pulse only the right (weapon) hand: {pulses}"

    # the aim marker: the same dot in both eyes, fusing in front of the player
    dots = {}
    for eye in ("left", "right"):
        off, dot, beam = image("xr_laser_off", eye), image("xr_laser_dot", eye), image("xr_laser_beam", eye)
        box, dot_pixels = changed_box(off, dot)
        assert box is not None, f"the aim marker changed nothing in the {eye} eye"
        assert box[2] - box[0] <= 24 and box[3] - box[1] <= 24, \
            f"the {eye} eye changed beyond a small dot with game time stopped: {box}"
        x, y = (box[0] + box[2] - 1) / 2.0, (box[1] + box[3] - 1) / 2.0
        # a red dot round a paler hot centre
        red = sum(1 for r, g, b in dot.crop(box).getdata() if r > 150 and r > g + 60 and r > b + 60)
        assert red >= 4, f"the {eye} eye's marker is not a red dot ({red} red pixels in {box})"
        _, beam_pixels = changed_box(off, beam)
        assert beam_pixels > dot_pixels * 2, f"the {eye} eye's beam adds too little ({beam_pixels} vs {dot_pixels} pixels)"
        dots[eye] = eye_tangents(eye, x, y, dot.width, dot.height)
    (left_x, left_y), (right_x, right_y) = dots["left"], dots["right"]
    assert abs(left_y - right_y) < 0.01, f"the marker sits at different heights in the two eyes ({left_y:.4f} vs {right_y:.4f})"
    disparity = left_x - right_x
    assert disparity > 0.0, f"the marker's disparity puts it behind the eyes ({disparity:.5f})"
    marker_depth = EYE_SEPARATION_M / disparity
    assert 1.0 < marker_depth < 40.0, f"the marker fuses at {marker_depth:.2f} m, not on the ground a few metres ahead"

    # left-handed (vr_leftHanded): the left controller aims, so its dot lands
    # left of the right hand's, which aimed the same way from 0.4 m further right
    left_handed_shift = []
    for eye in ("left", "right"):
        left_box, _ = changed_box(image("xr_left_off", eye), image("xr_left_dot", eye))
        assert left_box is not None, f"the left-handed aim marker changed nothing in the {eye} eye"
        assert left_box[2] - left_box[0] <= 24 and left_box[3] - left_box[1] <= 24, \
            f"the {eye} eye changed beyond a small dot when aiming left-handed: {left_box}"
        right_box, _ = changed_box(image("xr_laser_off", eye), image("xr_laser_dot", eye))
        shift = (right_box[0] + right_box[2]) / 2.0 - (left_box[0] + left_box[2]) / 2.0
        assert shift > 30.0, f"the left hand's dot should land left of the right hand's in the {eye} eye ({shift:.0f} px)"
        left_handed_shift.append(shift)

    # two hands on the gun: the squeeze held the gun instead of opening the
    # weapon wheel, buzzed the off hand, and the aim rose with the front palm
    two_hands = text.partition("VR_TWO_HANDS")[2].partition("VR_TWO_HANDS_DONE")[0]
    assert "OpenXR: two-handed aim held" in two_hands, "the off-hand squeeze on the foregrip did not hold the gun"
    assert "OpenXR: two-handed aim released" in two_hands, "letting go of the foregrip did not release the gun"
    assert "OpenXR: JOY1 down" not in two_hands, "the foregrip squeeze also opened the weapon wheel"
    assert any(e.get("event") == "haptic" and e.get("hand") == "/user/hand/left" and e.get("duration_ns") == 20_000_000
               for e in events), "taking the foregrip did not buzz the off hand"
    for eye in ("left", "right"):
        two_dot = image("xr_two_dot", eye)
        box, _ = changed_box(image("xr_two_off", eye), two_dot)
        assert box is not None, f"the two-handed aim marker changed nothing in the {eye} eye"
        x, y = (box[0] + box[2] - 1) / 2.0, (box[1] + box[3] - 1) / 2.0
        _, two_y = eye_tangents(eye, x, y, two_dot.width, two_dot.height)
        assert two_y > dots[eye][1] + 0.05, \
            f"holding the gun in both hands should raise the {eye} eye's aim ({dots[eye][1]:.3f} to {two_y:.3f})"

    # the comfort vignette: turning smoothly at full strength blacks out the
    # top corners, which show sky without it, and leaves the centre clear
    for eye in ("left", "right"):
        plain, vignette = image("xr_laser_dot", eye).convert("L"), image("xr_vignette", eye).convert("L")
        w, h = vignette.size
        corners = [(0, 0, w // 10, h // 10), (w - w // 10, 0, w, h // 10)]
        plain_corner = min(ImageStat.Stat(plain.crop(box)).mean[0] for box in corners)
        dark_corner = max(ImageStat.Stat(vignette.crop(box)).mean[0] for box in corners)
        centre = ImageStat.Stat(vignette.crop((w * 3 // 10, h * 3 // 10, w * 7 // 10, h * 7 // 10))).mean[0]
        assert plain_corner > 10.0, f"the {eye} eye's top corners should show sky before the vignette ({plain_corner:.1f})"
        assert dark_corner < 3.0, f"a smooth turn should black out the {eye} eye's corners ({dark_corner:.1f})"
        assert centre > 10.0, f"the vignette must leave the {eye} eye's centre clear ({centre:.1f})"

    with Image.open(profile / "xr_gameplay_quad1.tga") as hud:
        alpha = hud.convert("RGBA").getchannel("A")
        covered = sum(alpha.histogram()[16:]) / float(hud.width * hud.height)
    assert 0.0 < covered < 0.9, f"the HUD layer should be a partial overlay ({covered:.3f} covered)"

    before = text.partition("VR_SNAP_BEFORE")[2].partition("VR_SNAP_AFTER")[0]
    after = text.partition("VR_SNAP_AFTER")[2]
    yaws_before, yaws_after = body_yaws(before), body_yaws(after)
    assert yaws_before and yaws_after, "vr_status did not report the body yaw"
    turn = yaw_delta(yaws_before[0], yaws_after[0])
    assert abs(turn + 45.0) < 1.0, f"a right snap turn should turn the body -45 degrees, turned {turn:.2f}"
    assert "VR_TRIGGER_BIND_RAN" in text, "the controller trigger never reached its binding"

    # the pause menu: the menu button opened it, the screen hangs opaque and
    # world-locked in front of the paused world, and the pointer sits where
    # the controller's ray meets it
    menu_log = text.partition("VR_TRIGGER_BIND_RAN")[2]
    assert "OpenXR: JOY7 down" in menu_log, "the controller's menu button never posted its key"
    assert "VR_MENU_OPEN" in menu_log, "the menu stage did not run"
    # the runtime runs its script in order, so each capture's frame is found
    # by its place among the script's captures
    capture_frames = [e["frame"] for e in events if e.get("event") == "script_command" and e.get("command") == "capture"]
    capture_order = [Path(line.split()[-1]).name for line in script if line.split()[2] == "capture"]
    assert len(capture_frames) == len(capture_order), f"captures ran {len(capture_frames)} of {len(capture_order)}"
    menu_capture = capture_frames[capture_order.index("xr_menu")]
    vehicle_capture = capture_frames[capture_order.index("xr_vehicle")]
    menu_frames = [f for f in frames if menu_capture <= f["frame"] < vehicle_capture]
    assert menu_frames, "no frame followed the menu capture"
    screen_layer = menu_frames[0]["layers"][-1]
    assert screen_layer["type"] == "quad" and not screen_layer["flags"] & 0x2 and abs(screen_layer["z"] + 2.5) < 0.01, \
        f"the pause menu should hang an opaque screen 2.5 m ahead: {menu_frames[0]}"
    with Image.open(profile / "xr_menu_quad1.tga") as screen:
        screen = screen.convert("RGB")
        screen_height_m = SCREEN_WIDTH_M * screen.height / screen.width
        px = round((0.5 + POINTER_RIGHT_M / SCREEN_WIDTH_M) * screen.width)
        py = round((0.5 - POINTER_UP_M / screen_height_m) * screen.height)
        # the dot is white, ringed in black, about 16 px across at 1080 lines
        ring = max(6, round(screen.height * 12 / 1080))
        centre = min(screen.getpixel((px + dx, py + dy))[c] for dx in (-1, 0, 1) for dy in (-1, 0, 1) for c in range(3))
        border = max(max(screen.getpixel((px + dx, py + dy))) for dx, dy in ((ring, 0), (-ring, 0), (0, ring), (0, -ring)))
    assert centre > 200, f"no pointer at ({px}, {py}) on the menu screen (darkest centre channel {centre})"
    assert border < 90, f"the pointer at ({px}, {py}) lacks its dark ring (brightest ring channel {border})"
    assert "OpenXR: MOUSE1 down" in menu_log and "OpenXR: MOUSE1 up" in menu_log, \
        "the trigger did not click as a mouse button while pointing at the menu"
    # the hidden test window never has focus, as a desktop window seldom does
    # while the player wears the headset: the click must still reach the menu
    assert any([layer["type"] for layer in f["layers"]] == ["projection", "quad"] and f["layers"][1]["flags"] & 0x2
               for f in menu_frames), "clicking Resume with the pointer did not close the pause menu"

    # a walker in stereo: its cockpit turns after the head, about the eye,
    # which sits on the cockpit's turning axis (off it, a quarter turn would
    # swing the eye some 70 units; pitching the cockpit may move it a unit or
    # two, as a nod does), and the headset keeps the stereo view with the HUD
    assert "VR vehicle rvVehicleWalker: its turns carry the view" in menu_log, "entering the walker seated no VR view"
    def view_pose(after: str) -> list[float]:
        found = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)\s+(-?\d+(?:\.\d+)?)",
                          menu_log.partition(after)[2])
        assert found, f"getviewpos after {after} printed no view position"
        return [float(found.group(i)) for i in range(1, 5)]
    seated, looked = view_pose("VR_VEHICLE_IN"), view_pose("VR_VEHICLE_LOOKED")
    # the script turned the walker 45 degrees as it seated the player; a seat
    # that kept its boarding facing would leave the cockpit chasing 150.7
    assert abs(yaw_delta(seated[3], 105.7)) < 3.0, \
        f"the seat should follow the walker's turn right after boarding and look along 105.7, not {seated[3]:.1f}"
    cockpit_turn = yaw_delta(seated[3], looked[3])
    assert abs(cockpit_turn + 45.0) < 3.0, f"the walker's cockpit should turn 45 degrees right after the head, not {cockpit_turn:.1f}"
    eye_moved = math.dist(seated[:3], looked[:3])
    assert eye_moved < 4.0, f"turning the cockpit moved the eye {eye_moved:.1f} units"
    vehicle_frames = [f for f in frames if f["frame"] >= vehicle_capture]
    assert vehicle_frames and [layer["type"] for layer in vehicle_frames[0]["layers"]] == ["projection", "quad"], \
        f"the walker should present a stereo view and the HUD: {vehicle_frames[:1]}"
    # the head aims the cockpit, so the HUD and its sights hang on the line of
    # sight even though the controller aims on foot (where it hangs lower)
    assert abs(vehicle_frames[0]["layers"][1]["y"]) < 0.01, \
        f"in the walker the HUD should hang on the line of sight: {vehicle_frames[0]['layers'][1]}"

    # the weapon wheel follows the weapon hand: raised, it hovers the blaster
    # (slot 0, at the top) and releasing the grip switches to it
    def wheel(tag: str) -> dict[str, int]:
        found = re.search(r"WEAPON_WHEEL active=(\d+) slot=(-?\d+) slots=\d+ current=(-?\d+) ideal=(-?\d+)", text.partition(tag)[2])
        assert found, f"testWeaponWheel printed no state after {tag}"
        return dict(zip(("active", "slot", "current", "ideal"), (int(group) for group in found.groups())))
    wheel_before, wheel_pointed, wheel_after = wheel("VR_WHEEL_BEFORE"), wheel("VR_WHEEL_POINTED"), wheel("VR_WHEEL_AFTER")
    assert wheel_before["active"] == 0 and wheel_before["current"] > 0, f"the wheel stage should start on the machinegun: {wheel_before}"
    assert wheel_pointed["active"] == 1, f"the off-hand grip away from the gun did not open the weapon wheel: {wheel_pointed}"
    assert wheel_pointed["slot"] == 0, f"raising the weapon hand should hover the blaster at the wheel's top: {wheel_pointed}"
    assert wheel_after["active"] == 0 and wheel_after["ideal"] == 0, \
        f"releasing the grip should switch to the hovered blaster: {wheel_after}"

    # losing the controllers to the runtime pauses single player: the pause
    # menu's opaque, world-locked screen replaces the HUD, and it is still there
    # once the controllers are back, for the player to resume from
    unfocused_log = menu_log.partition("VR_VEHICLE_OUT")[2].partition("VR_UNFOCUSED")[0]
    assert "VR: the headset lost input focus; single player is paused on the menu" in unfocused_log, \
        "losing input focus in gameplay did not pause single player"
    focus_frames = [e["frame"] for e in events if e.get("event") == "script_command" and e.get("command") == "focus"]
    assert len(focus_frames) == 2, f"the focus script ran {len(focus_frames)} of 2 commands"
    paused_frames = [f for f in frames if f["frame"] > focus_frames[0]]
    first_paused = next((i for i, f in enumerate(paused_frames)
                         if [layer["type"] for layer in f["layers"]] == ["projection", "quad"]
                         and not f["layers"][1]["flags"] & 0x2 and abs(f["layers"][1]["z"] + 2.5) < 0.01), None)
    assert first_paused is not None, \
        f"after losing focus the pause menu should hang 2.5 m ahead of the paused world: {paused_frames[:3]}"
    assert not any(layer["type"] == "quad" and layer["flags"] & 0x2
                   for f in paused_frames[first_paused:] for layer in f["layers"]), \
        f"the pause menu should stay up once the controllers are back: {paused_frames[first_paused:][:4]}"

    # the runtime ends VR: the game carries on and keeps the player's setting
    before_exit, _, after_exit = menu_log.partition("VR_AFTER_EXIT")
    assert "OpenXR: the runtime ended the VR session" in before_exit, "the runtime's exit request was not honoured"
    assert "the runtime ended VR; vr_restart starts it again" in after_exit, "vr_status does not report the ended session"
    assert re.search(r'"vr_enable" is:"1"', after_exit), "ending VR from the runtime must leave vr_enable as the player set it"

    for name in ("left", "right", "quad1"):
        image("xr_gameplay", name).save(profile / f"xr_gameplay_{name}.png")
    turned.save(profile / "xr_turned_left.png")
    image("xr_menu", "quad1").save(profile / "xr_menu_quad1.png")
    for name in ("off", "dot", "beam"):
        for eye in ("left", "right"):
            image(f"xr_laser_{name}", eye).save(profile / f"xr_laser_{name}_{eye}.png")
    print(f"OpenXR VR smoke ({options.render_api}): PASS (eye difference {eye_difference:.2f}, head turn {turn_difference:.2f}, "
          f"HUD coverage {covered:.3f}, aim marker at {marker_depth:.2f} m, left hand's dot "
          f"{min(left_handed_shift):.0f} px left, stick walks {head_off:.0f} and {hand_off:.0f} degrees off the facing, "
          f"exit panel cursor at {gui_cursor[0]:.2f},{gui_cursor[1]:.2f}, culling hid "
          f"{culled_away['left']:.1%}/{culled_away['right']:.1%} of the eyes, pointer at {px},{py}, "
          f"walker cockpit turned {cockpit_turn:.1f}); evidence={profile}")


if __name__ == "__main__":
    main()
