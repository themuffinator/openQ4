#!/usr/bin/env python3
"""Press screens with a fingertip in VR, against the openQ4 OpenXR test runtime
(requires a staged client built with OpenXR, and retail assets).

Nothing synthesises keyboard or mouse input. The test runtime
(tools/tests/openxr/OpenXRTestRuntime.cpp) moves the scripted controllers when
marker files appear, and this harness creates those markers itself, one small
step of a hand at a time, while it reads the game's log: the game reports
(vr_debug 1) how far the fingertip is from a screen, so the approach stops
where the check needs it, whatever the screen's exact geometry or the drawn
hand's fingertip. Console commands reach the game through a cfg it polls.

Checks, in single player:
- world screens (a lift call: airdefense2's func_elevator_3 calls by default;
  with --game q4xbase the lift call of Prison's dead lift): the weapon hand's laser
  focuses the screen and ticks that hand as the cursor moves onto its button;
  a free fingertip resting about 3 cm in front of the screen hovers its cursor
  for two seconds and never clicks; a poke clicks exactly once, at contact
  (within 5 mm), runs the screen's action (the lift call activates the lift),
  ticks the poking hand firmly and releases once as the finger draws back;
- whose fingertip: the off hand's as the arms draw it with vr_body 2, a point
  ahead of the controller with vr_body 1 and 0; the weapon hand's once a
  script has taken the weapon away; the off hand's mirrored with
  vr_leftHanded 1;
- in The Awakening the trial holds the blaster: the freeze gun Prison starts
  with has no measured hold, so its support hand stays on the gun;
- the off-hand menu panel (vr_menuAnchor 1): the weapon hand's fingertip
  resting 3 cm in front of RESUME clicks nothing and the pause menu stays up;
  a poke presses RESUME, ticks the hand firmly, and the menu closes; with
  vr_leftHanded 1 the panel stands on the right hand and the left presses it;
- the log carries no errors.

What stays unverified without a headset: the drawn fingertip's place on a
real hand, how the contact distances feel, and the haptic strengths
(docs/dev/vr-headset-qualification.md).
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]

UNITS_PER_METRE = 39.37          # vr_worldScale's default
FINGERTIP_REACH_M = 0.08         # VRTouchCore.h VR_FINGERTIP_REACH
HEAD = (0.0, 1.6, 0.0)           # the runtime's head in the stage space
STEP_M = 0.004                   # each step of an approach
STEPS = 76                       # from 20 cm out to 10 cm through, by the controller's fingertip
STEP_SECONDS = 0.09
REST_TARGET_MM = 35.0            # the rest stops this close, or closer
CONTACT_MM = 5.0                 # VRTouchCore.h VR_TOUCH_CONTACT
EYE_OUT_M = 0.45                 # the eye stands this far in front of a screen
EYE_UP_M = 0.15                  # and this far above it

# The hand panel (vr_menuAnchor 1): VRPanelCore.h stands it half its height
# plus 6 cm above the off hand, turned to the head; RESUME is the first of the
# pause column's nine 70 dp buttons, 4 dp apart, centred on the 720 dp canvas.
PANEL_WIDTH_M = 0.5
PANEL_ASPECT = 16.0 / 9.0
OFF_HAND_REST = (-0.2, 1.25, -0.35)
RESUME = (0.5, 64.0 / 720.0)

# The screens: a guess at each one's centre and the way it faces (world
# units), which a laser ray along the guess corrects before any touch.
GAMES = {
    "baseoq4": {
        "map": "game/airdefense2",
        # func_elevator_3's calls: the lift starts at the first floor, so its
        # call there is greyed out and the second floor's is live; each call
        # sends the lift to its floor, and the live call changes sides
        "screens": [
            ("func_elevator_3_gui3", (-555.125, 1554.0, 276.25)),
            ("func_elevator_3_gui2", (-555.125, 1554.0, 68.25)),
        ],
        "normal": (-19.0, 0.0, 4.5),
        "action": "activate",
    },
    "q4xbase": {
        "map": "game/m04_prison",
        # the dead lift's lower call box (a small monitor model tilted 20
        # degrees up, facing south), live at the map's start
        "screens": [("deadLift_callbox_1", (-692.0, 1931.0, -3.0))],
        "normal": (0.0, -0.939693, 0.34202),
        "action": "activate",
    },
}


def unit(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def add(a, b, s=1.0):
    return tuple(a[i] + b[i] * s for i in range(3))


def sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


class Geometry:
    """A screen's approach in the stage space of an eye placed before it."""

    def __init__(self, normal):
        self.normal = unit(normal)
        flat = unit((self.normal[0], self.normal[1], 0.0))
        # the eye faces the screen
        self.yaw = math.degrees(math.atan2(-flat[1], -flat[0]))

    def eye(self, centre):
        s = UNITS_PER_METRE
        return add(add(centre, self.normal, EYE_OUT_M * s), (0.0, 0.0, 1.0), EYE_UP_M * s)

    def to_stage(self, offset_world):
        """A world offset from the eye as a stage-space offset (metres)."""
        yaw = math.radians(self.yaw)
        forward = (math.cos(yaw), math.sin(yaw), 0.0)
        right = (math.sin(yaw), -math.cos(yaw), 0.0)
        s = UNITS_PER_METRE
        return (dot(offset_world, right) / s, offset_world[2] / s, -dot(offset_world, forward) / s)

    def point(self, along_m):
        """The stage point along_m metres in front of the screen's centre."""
        # the eye stands at eye( centre ): from it, the point is along the
        # normal less the eye's own distance out, and the eye's height below
        s = UNITS_PER_METRE
        world = add(tuple(c * ( along_m - EYE_OUT_M ) * s for c in self.normal), (0.0, 0.0, 1.0), -EYE_UP_M * s)
        offset = self.to_stage(world)
        return (offset[0], HEAD[1] + offset[1], offset[2])

    def into(self):
        """The stage direction into the screen."""
        d = self.to_stage(tuple(-c for c in self.normal))
        return unit(d)


def aim(direction):
    """The runtime's yaw (left about +Y) and pitch (up about +X) aiming along direction."""
    dx, dy, dz = direction
    return math.degrees(math.atan2(-dx, -dz)), math.degrees(math.atan2(dy, math.hypot(dx, dz)))


def pose(position, direction):
    yaw, pitch = aim(direction)
    return f"{position[0]:.4f} {position[1]:.4f} {position[2]:.4f} {yaw:.3f} {pitch:.3f} 0"


def hand_line(geometry: Geometry, distance_m: float) -> str:
    """The controller whose fingertip (VR_FINGERTIP_REACH ahead) is distance_m in front of the screen."""
    direction = geometry.into()
    grip = add(geometry.point(distance_m), direction, -FINGERTIP_REACH_M)
    return pose(grip, direction)


def away(side: str) -> str:
    """A hand lowered and pointing at the floor, well clear of everything."""
    x = 0.25 if side == "right" else -0.25
    return f"{x:.2f} 1.0 -0.15 0 -70 0"


def panel_geometry(off_side: str):
    hand = OFF_HAND_REST if off_side == "left" else (-OFF_HAND_REST[0], OFF_HAND_REST[1], OFF_HAND_REST[2])
    height = PANEL_WIDTH_M / PANEL_ASPECT
    centre = (hand[0], hand[1] + height * 0.5 + 0.06, hand[2])
    d = sub(HEAD, centre)
    yaw = math.atan2(d[0], d[2])
    pitch = math.atan2(d[1], math.hypot(d[0], d[2]))
    right = (math.cos(yaw), 0.0, -math.sin(yaw))
    up = (-math.sin(pitch) * math.sin(yaw), math.cos(pitch), -math.sin(pitch) * math.cos(yaw))
    x, y = (RESUME[0] - 0.5) * PANEL_WIDTH_M, (0.5 - RESUME[1]) * height
    target = tuple(centre[i] + right[i] * x + up[i] * y for i in range(3))
    return hand, target, unit(d)


def panel_line(off_side: str, distance_m: float) -> str:
    _, target, normal = panel_geometry(off_side)
    direction = tuple(-c for c in normal)
    tip = add(target, normal, distance_m)
    return pose(add(tip, direction, -FINGERTIP_REACH_M), direction)


class Client:
    """The game's log, read as it grows, and the cfg it polls for commands."""

    def __init__(self, log: Path, game: Path, process: subprocess.Popen):
        self.log = log
        self.game = game
        self.process = process
        self.buffer = ""
        self.offset = 0
        self.acks = 0

    def text(self) -> str:
        if self.log.is_file():
            with self.log.open("rb") as f:
                f.seek(self.offset)
                data = f.read()
            self.offset += len(data)
            self.buffer += data.decode("utf-8", errors="replace")
        return self.buffer

    def mark(self) -> int:
        return len(self.text())

    def since(self, mark: int) -> str:
        return self.text()[mark:]

    def wait(self, pattern: str, mark: int, timeout: float):
        end = time.monotonic() + timeout
        while True:
            found = re.search(pattern, self.since(mark))
            if found or time.monotonic() > end:
                return found
            if self.process.poll() is not None:
                raise AssertionError(f"the client exited (code {self.process.returncode})")
            time.sleep(0.05)

    def console(self, commands: list[str], timeout: float = 30.0):
        self.acks += 1
        tag = f"VR_TOUCH_ACK_{self.acks}"
        mark = self.mark()
        (self.game / "vr_touch_cmd.cfg").write_text("\n".join(commands + [f"echo {tag}"]) + "\n", encoding="utf-8")
        found = self.wait(re.escape(tag), mark, timeout)
        (self.game / "vr_touch_cmd.cfg").write_text("\n", encoding="utf-8")
        assert found, f"the game never ran {commands}"

    def marker(self, name: str, seconds: float = 0.0):
        (self.game / f"vr_touch_{name}.txt").write_text("\n", encoding="utf-8")
        if seconds:
            time.sleep(seconds)


def hover_distances(text: str) -> list[float]:
    return [float(v) for v in re.findall(r"VR GUI touch hover (-?\d+\.\d) mm", text)]


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--game", choices=sorted(GAMES), default="baseoq4",
                        help="the campaign: baseoq4 (Quake 4) or q4xbase (The Awakening)")
    parser.add_argument("--awakening-content", type=Path,
                        help="with --game q4xbase: The Awakening's installed content (default: .home/q4xbase)")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--eye-size", default="800x880")
    parser.add_argument("--quick", action="store_true", help="one world trial and one panel trial (vr_body 2)")
    parser.add_argument("--trials", help="comma-separated indices of the world trials to run (default: all)")
    parser.add_argument("--debug", type=int, default=1, help="vr_debug (2 also reports each fingertip once a second)")
    options = parser.parse_args(argv)
    setup = GAMES[options.game]

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)

    output = (options.output_dir or ROOT / ".tmp/openxr-vr-touch").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / options.game
    game_dir = profile / options.game
    game_dir.mkdir(parents=True, exist_ok=True)
    if options.game == "q4xbase":
        # The Awakening's content, hard-linked into this run's own profile so
        # nothing is written to the source (its logs, saves, caches and config stay out)
        source = (options.awakening_content or ROOT / ".home/q4xbase").resolve()
        if not (source / "maps").is_dir():
            parser.error(f"The Awakening's content was not found: {source} (--awakening-content)")
        for entry in source.iterdir():
            target = game_dir / entry.name
            if entry.name in ("logs", "savegames", "generated", "cfg") or entry.suffix == ".cfg" or target.exists():
                continue
            if entry.is_dir():
                for path in entry.rglob("*"):
                    if path.is_file():
                        link = target / path.relative_to(entry)
                        link.parent.mkdir(parents=True, exist_ok=True)
                        os.link(path, link)
            elif entry.is_file():
                os.link(entry, target)
    for stale in list(game_dir.glob("vr_touch_*.txt")) + list(game_dir.glob("vr_touch_*.cfg")):
        stale.unlink()
    log_path = game_dir / "logs/openq4.log"
    log_path.unlink(missing_ok=True)
    xr_log = profile / "xr.jsonl"
    xr_log.unlink(missing_ok=True)

    # the world trials: (vr_body, left-handed, which hand pokes), and whether
    # the hand panel is checked in that configuration afterwards
    if options.game == "q4xbase":
        # Prison starts with the freeze gun, whose hold is not measured (its
        # arms stay as animated, so a touch comes from the controller); the
        # blaster's arms reach the hands, so its drawn fingertip touches
        trials = [dict(body=2, left=False, poker="off", panel=True, setup=["give weapon_blaster", "_impulse0"])]
    elif options.quick:
        trials = [dict(body=2, left=False, poker="off", panel=True)]
    else:
        trials = [
            dict(body=2, left=False, poker="off", panel=True),
            dict(body=2, left=False, poker="weapon", panel=False),
            dict(body=2, left=True, poker="off", panel=True),
            dict(body=1, left=False, poker="off", panel=False),
            dict(body=0, left=False, poker="off", panel=False),
        ]
    if options.trials:
        trials = [trials[int(i)] for i in options.trials.split(",")]
    geometry = Geometry(setup["normal"])
    steps = [0.20 - STEP_M * k for k in range(STEPS)]
    attempts = 6

    def marker_path(name: str) -> Path:
        return game_dir / f"vr_touch_{name}.txt"

    script = []
    for t, trial in enumerate(trials):
        weapon = "left" if trial["left"] else "right"
        off = "right" if trial["left"] else "left"
        poker = weapon if trial["poker"] == "weapon" else off
        trial.update(weapon=weapon, off=off, hand=poker)
        script += [f"when {marker_path(f't{t}_init')} hand {weapon} {away(weapon)}",
                   f"when {marker_path(f't{t}_init')} hand {off} {away(off)}"]
        # the laser along the approach, from 25 cm out
        laser = pose(geometry.point(0.25), geometry.into())
        for a in range(attempts):
            script += [f"when {marker_path(f't{t}_cal{a}')} hand {weapon} {laser}",
                       f"when {marker_path(f't{t}_caloff{a}')} hand {weapon} {away(weapon)}"]
        for phase in ("s", "p", "r"):
            script += [f"when {marker_path(f't{t}_{phase}{k}')} hand {poker} {hand_line(geometry, d)}"
                       for k, d in enumerate(steps)]
        script.append(f"when {marker_path(f't{t}_back')} hand {poker} {hand_line(geometry, steps[0])}")
        script.append(f"when {marker_path(f't{t}_done')} hand {poker} {away(poker)}")
        if trial["panel"]:
            hand, _, _ = panel_geometry(off)
            panel_steps = [0.12 - STEP_M * k for k in range(45)]
            script += [f"when {marker_path(f'm{t}_hands')} hand {off} {hand[0]:.3f} {hand[1]:.3f} {hand[2]:.3f} 0 0 0",
                       f"when {marker_path(f'm{t}_hands')} hand {weapon} {panel_line(off, panel_steps[0])}",
                       f"when {marker_path(f'm{t}_menu')} button left menu 1",
                       f"when {marker_path(f'm{t}_menuup')} button left menu 0",
                       f"when {marker_path(f'm{t}_rest')} hand {weapon} {panel_line(off, 0.03)}",
                       f"when {marker_path(f'm{t}_stamp_rest')} pad left 0 0",
                       f"when {marker_path(f'm{t}_away')} hand {weapon} {panel_line(off, panel_steps[0])}",
                       f"when {marker_path(f'm{t}_stamp_after')} pad left 0 0",
                       f"when {marker_path(f'm{t}_done')} hand {weapon} {away(weapon)}"]
            for phase in ("p", "r"):
                script += [f"when {marker_path(f'm{t}_{phase}{k}')} hand {weapon} {panel_line(off, d)}"
                           for k, d in enumerate(panel_steps)]
            trial["panel_steps"] = len(panel_steps)
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")

    (game_dir / "vr_touch_cmd.cfg").write_text("\n", encoding="utf-8")
    (game_dir / "vr_touch_loop.cfg").write_text("exec vr_touch_cmd.cfg\nwaitMsec 100\nexec vr_touch_loop.cfg\n", encoding="utf-8")
    (game_dir / "openxr_vr_touch.cfg").write_text("\n".join([
        "waitMsec 3000", "god", "notarget", "noclip", "echo VR_TOUCH_READY", "exec vr_touch_loop.cfg",
    ]) + "\n", encoding="utf-8")

    launch_name = "(SP) Awakening main menu — GL" if options.game == "q4xbase" else "(SP) Main menu — GL"
    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == launch_name)
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_touch.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        "vr_enable": "1", "vr_setupDone": "1", "vr_debug": str(options.debug), "vr_aimMode": "1", "vr_turnMode": "0",
        "vr_leftHanded": "0", "vr_body": "2", "vr_menuAnchor": "1", "vr_menuWidth": str(PANEL_WIDTH_M),
        "vr_hudMode": "1", "vr_menuHaptics": "1", "vr_hapticStrength": "1", "vr_worldScale": str(UNITS_PER_METRE),
        "vr_roomScale": "1", "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", setup["map"]]
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
    results = {"game": options.game, "trials": []}
    process_log = (profile / "process.log").open("w", encoding="utf-8")
    process = subprocess.Popen(command, cwd=staged, stdout=process_log, stderr=subprocess.STDOUT,
                               env=environment, startupinfo=startup)
    client = Client(log_path, game_dir, process)
    deadline = time.monotonic() + options.timeout
    screen_index = 0
    try:
        assert client.wait(r"VR_TOUCH_READY", 0, options.timeout), "gameplay was never reached"
        assert "OpenXR: presenting to the headset" in client.text(), "the session never began"
        for t, trial in enumerate(trials):
            assert time.monotonic() < deadline, "out of time"
            record = {k: trial[k] for k in ("body", "left", "poker", "hand")}
            results["trials"].append(record)
            weapon, hand = trial["weapon"], trial["hand"]
            commands = [f"vr_body {trial['body']}", f"vr_leftHanded {int(trial['left'])}"]
            if trial["poker"] == "weapon":
                commands.append('script "$player1.disableWeapon();"')
            client.console(commands + trial.get("setup", []))
            if trial.get("setup"):
                time.sleep(2.0)
            client.marker(f"t{t}_init", 0.6)

            # the laser along the approach finds the live screen and where it
            # really is, and ticks the weapon hand on its button
            name = centre = None
            hit = None
            for a in range(attempts):
                candidate, guess = setup["screens"][screen_index % len(setup["screens"])]
                client.console([f"setviewpos {' '.join(f'{c:.2f}' for c in geometry.eye(guess))} {geometry.yaw:.2f}"])
                time.sleep(0.4)
                mark = client.mark()
                client.marker(f"t{t}_cal{a}")
                hit = client.wait(rf"VR GUI focus {re.escape(candidate)} at [\d.]+ [\d.]+, world (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", mark, 2.0)
                tick = client.wait(rf"VR GUI hover tick, {weapon} hand", mark, 1.0) if hit else None
                client.marker(f"t{t}_caloff{a}", 0.6)
                if hit:
                    name = candidate
                    centre = tuple(float(v) for v in hit.groups())
                    record["laser_tick"] = bool(tick)
                    break
                # the other call, while the lift travels
                screen_index += 1
                time.sleep(1.5)
            assert name, f"trial {t}: no screen took the laser's focus"
            record["screen"] = name
            client.console([f"setviewpos {' '.join(f'{c:.2f}' for c in geometry.eye(centre))} {geometry.yaw:.2f}"])
            time.sleep(0.5)

            # rest: approach until the fingertip hovers about 3 cm out, then hold
            mark = client.mark()
            rest_step = None
            for k in range(STEPS):
                client.marker(f"t{t}_s{k}", STEP_SECONDS)
                distances = hover_distances(client.since(mark))
                if distances and distances[-1] <= REST_TARGET_MM:
                    rest_step = k
                    break
            assert rest_step is not None, f"trial {t}: the fingertip never hovered within {REST_TARGET_MM} mm of {name}"
            time.sleep(2.0)
            rest = client.since(mark)
            near = re.search(rf"VR GUI touch near {re.escape(name)}, (\w+) hand, (drawn|controller) fingertip", rest)
            record["source"] = near.group(2) if near else None
            record["near_hand"] = near.group(1) if near else None
            record["rest_mm"] = hover_distances(rest)[-1]
            record["rest_pressed"] = "VR GUI touch press" in rest
            record["hover_tick"] = f"VR GUI hover tick, {hand} hand" in rest
            client.marker(f"t{t}_back", 0.8)

            # poke: on in until it presses, 12 mm further, then back out until it lets go
            mark = client.mark()
            pressed = None
            for k in range(STEPS):
                client.marker(f"t{t}_p{k}", STEP_SECONDS)
                if "VR GUI touch press" in client.since(mark):
                    pressed = k
                    break
            assert pressed is not None, f"trial {t}: poking {name} never pressed it"
            deepest = min(STEPS - 1, pressed + 3)
            for k in range(pressed + 1, deepest + 1):
                client.marker(f"t{t}_p{k}", STEP_SECONDS)
            for k in range(deepest, -1, -1):
                client.marker(f"t{t}_r{k}", STEP_SECONDS)
                if "VR GUI touch release" in client.since(mark):
                    break
            client.marker(f"t{t}_done", 0.5)
            poke = client.since(mark)
            presses = re.findall(r"VR GUI touch press (\S+) at [\d.]+ [\d.]+, (-?[\d.]+) mm(?:: ([^\n]*))?", poke)
            # a release as the finger draws back, or as the screen goes inactive under it
            releases = [line for line in poke.splitlines() if "VR GUI touch release" in line]
            record["presses"] = [{"screen": p[0], "mm": float(p[1]), "command": p[2]} for p in presses]
            record["releases"] = releases
            if trial["poker"] == "weapon":
                client.console(['script "$player1.enableWeapon();"'])
            # the live call changes sides with the lift
            screen_index += 1

            if trial["panel"]:
                panel = {}
                record["panel"] = panel
                client.marker(f"m{t}_hands", 0.8)
                mark = client.mark()
                client.marker(f"m{t}_menu", 0.3)
                client.marker(f"m{t}_menuup")
                grown = client.wait(rf"OpenXR: the menu panel grows out of the {trial['off']} hand", mark, 8.0)
                assert grown, f"trial {t}: the pause menu did not grow out of the {trial['off']} hand"
                time.sleep(1.0)
                mark = client.mark()
                client.marker(f"m{t}_rest", 2.0)
                client.marker(f"m{t}_stamp_rest", 0.3)
                panel["rest_pressed"] = "fingertip press on the hand panel" in client.since(mark)
                client.marker(f"m{t}_away", 0.5)
                mark = client.mark()
                pressed = None
                for k in range(trial["panel_steps"]):
                    client.marker(f"m{t}_p{k}", STEP_SECONDS)
                    if "fingertip press on the hand panel" in client.since(mark):
                        pressed = k
                        break
                assert pressed is not None, f"trial {t}: poking RESUME on the hand panel never pressed it"
                deepest = min(trial["panel_steps"] - 1, pressed + 2)
                for k in range(pressed + 1, deepest + 1):
                    client.marker(f"m{t}_p{k}", STEP_SECONDS)
                for k in range(deepest, -1, -1):
                    client.marker(f"m{t}_r{k}", STEP_SECONDS)
                    if "fingertip release on the hand panel" in client.since(mark):
                        break
                time.sleep(1.5)
                client.marker(f"m{t}_stamp_after", 0.4)
                client.marker(f"m{t}_done", 0.3)
                poke = client.since(mark)
                found = re.findall(r"fingertip press on the hand panel at ([\d.]+) ([\d.]+), (-?[\d.]+) mm", poke)
                panel["presses"] = [{"u": float(u), "v": float(v), "mm": float(mm)} for u, v, mm in found]
                panel["releases"] = poke.count("fingertip release on the hand panel")
        (game_dir / "vr_touch_cmd.cfg").write_text("echo VR_TOUCH_COMPLETE\nquit\n", encoding="utf-8")
        process.wait(timeout=90)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=30)
        process_log.close()

    text = client.text() if log_path.is_file() else ""
    events = []
    if xr_log.is_file():
        for line in xr_log.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip():
                events.append(json.loads(line))
    (profile / "xr_events.json").write_text(json.dumps(events, indent=2), encoding="utf-8")
    haptics = [e for e in events if e.get("event") == "haptic"]
    results["haptics"] = [(e.get("hand"), e.get("amplitude"), e.get("duration_ns")) for e in haptics]
    (profile / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")

    assert "VR_TOUCH_COMPLETE" in text, "the script did not complete"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), "errors in the log"

    def ticks(side: str, duration_ms: int) -> int:
        return sum(1 for e in haptics if str(e.get("hand", "")).endswith(f"/{side}")
                   and e.get("duration_ns") == duration_ms * 1000000)

    for t, record in enumerate(results["trials"]):
        where = f"trial {t} (vr_body {record['body']}, {'left' if record['left'] else 'right'}-handed, {record['hand']} hand)"
        assert record.get("laser_tick"), f"{where}: the laser on {record['screen']} gave the weapon hand no hover tick"
        expected = "drawn" if record["poker"] == "off" and record["body"] == 2 else "controller"
        assert record["source"] == expected, f"{where}: the {expected} fingertip should touch, not {record['source']}"
        assert record["near_hand"] == record["hand"], f"{where}: the {record['near_hand']} hand touched instead"
        assert not record["rest_pressed"], f"{where}: resting {record['rest_mm']} mm from {record['screen']} clicked it"
        assert 10.0 <= record["rest_mm"] <= REST_TARGET_MM, f"{where}: the rest held at {record['rest_mm']} mm"
        assert record["hover_tick"], f"{where}: the fingertip gave its hand no hover tick on {record['screen']}"
        assert len(record["presses"]) == 1, f"{where}: one poke should press once: {record['presses']}"
        assert record["presses"][0]["mm"] <= CONTACT_MM, f"{where}: pressed at {record['presses'][0]['mm']} mm, before contact"
        assert len(record["releases"]) == 1, f"{where}: one poke should release once: {record['releases']}"
        commands = record["presses"][0]["command"] + " " + " ".join(record["releases"])
        assert GAMES[options.game]["action"] in commands, f"{where}: the poke did not run {record['screen']}'s action: {commands!r}"
        panel = record.get("panel")
        if panel is not None:
            assert not panel["rest_pressed"], f"{where}: a fingertip resting 3 cm from the hand panel clicked it"
            assert len(panel["presses"]) == 1 and panel["releases"] == 1, f"{where}: one poke of the panel: {panel}"
            press = panel["presses"][0]
            assert abs(press["u"] - RESUME[0]) < 0.06 and abs(press["v"] - RESUME[1]) < 0.04, \
                f"{where}: the panel press landed at {press['u']:.3f} {press['v']:.3f}, not on RESUME"
            assert press["mm"] <= CONTACT_MM, f"{where}: the panel pressed at {press['mm']} mm"

    # the hover and press ticks, in the hands that pointed and touched
    for record in results["trials"]:
        side = "left" if record["left"] else "right"
        assert ticks(side, 12) >= 1, f"no 12 ms hover tick in the {side} (weapon) hand"
        assert ticks(record["hand"], 25) >= 1, f"no 25 ms press tick in the {record['hand']} hand"

    # the panel menu was up while the finger rested and closed after the poke
    stamps = [e["frame"] for e in events if e.get("event") == "script_command" and e.get("command") == "pad"]
    frames = [e for e in events if e.get("event") == "end_frame"]

    def layers_at(frame: int) -> int:
        before = [f for f in frames if f["frame"] <= frame]
        return len(before[-1]["layers"]) if before else 0

    panels = [r for r in results["trials"] if r.get("panel") is not None]
    assert len(stamps) == 2 * len(panels), f"the panel stamps ran {len(stamps)} times for {len(panels)} panels"
    for i, record in enumerate(panels):
        rest, after = stamps[2 * i], stamps[2 * i + 1]
        assert layers_at(rest) == 2, f"the pause menu was not up while the finger rested near it ({layers_at(rest)} layers)"
        assert layers_at(after) == 3, f"pressing RESUME with the fingertip did not close the menu ({layers_at(after)} layers)"
    print(json.dumps(results, indent=2))
    print("VR touch smoke passed")


if __name__ == "__main__":
    main()
