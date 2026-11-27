#!/usr/bin/env python3
"""Check VR's event haptics against the openQ4 OpenXR test runtime (requires a
staged client built with OpenXR and retail assets; --game q4xbase also needs
The Awakening's content through --awakening).

As in openxr_vr_smoke.py, the test runtime (tools/tests/openxr/
OpenXRTestRuntime.cpp, selected for this process only through
XR_RUNTIME_JSON) simulates the headset and controllers and logs every
xrApplyHapticFeedback with its hand, amplitude and duration. Console commands
drive the game (give, _impulse, setviewpos and the cheat-gated
testVRHaptics), and the runtime's script follows them through marker files
the engine writes with condump. No keyboard or mouse input is synthesised.

Every pulse checked here pairs with something the player hears or sees, and
each has its own duration, so the runtime's log tells them apart:

- draw (35 ms, weapon hand): a weapon coming up after a switch;
- reload (25 ms, off hand): each sound the reload animation plays on the
  view weapon (the machinegun's magazine out, magazine in and bolt; the
  shotgun's shells; The Awakening's goob gun);
- hit (90 ms, both hands): from the head's right the right hand feels it
  whole and the left a quarter of it, from its left the other way round, from
  straight ahead both alike; with the head turned 90 degrees left, a hit from
  the room's forward comes from the head's right (the hands follow the head,
  not the body);
- blast (150 ms, both hands): the rocket launcher's impact effect shakes the
  hands with its screen shake, harder near than far, and not at all beyond
  its reach, though it does no damage;
- landing (80 ms, both hands): a fall of 200 units jolts the hands, while a
  drop no deeper than a jump does not;
- vr_hapticEvents 0 silences blasts and draws but keeps the shot's pulse
  (40 ms, weapon hand);
- the log carries no errors.

With --game q4xbase only the guns' draws and reloads and vr_hapticEvents 0
are checked: The Awakening's first map opens in a battle whose own fire and
artillery would hit and shake the player mid-check, and the hits, blasts and
landings run the same code in both campaigns.

The same events through a gamepad, and desktop play, are not covered here:
none of the new pulses reaches either, and the shots, hits and beams there
are unchanged.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]

DRAW_NS, RELOAD_NS, HIT_NS, BLAST_NS, LANDING_NS, SHOT_NS = (
    35_000_000, 25_000_000, 90_000_000, 150_000_000, 80_000_000, 40_000_000)
RIGHT, LEFT = "/user/hand/right", "/user/hand/left"
# airdefense1's start, on open level ground facing the battlefield (x y z yaw)
Q4_GROUND_POSE = (10310.89, -6950.34, 6.96, 150.7)

CAMPAIGNS = {
    "baseoq4": {
        "map": "game/airdefense1",
        # (give, select impulse, rounds to fire before reloading)
        "reloads": [("weapon_machinegun", 1, 600), ("weapon_shotgun", 2, 150)],
        # hits, blasts and the fall: game code shared by both campaigns, so
        # checked where a quiet start lets nothing else hit or shake the player
        "combat": True,
    },
    "q4xbase": {
        "map": "game/m01_stranarus_trench1",
        "reloads": [("weapon_goobgun", 11, 400)],
        # The Awakening's first map opens in a battle whose own fire and
        # artillery hit and shake the player; its weapons are what is new here
        "combat": False,
    },
}


def read_events(path: Path) -> list[dict]:
    events = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def frame_guarded(commands: list[str]) -> list[str]:
    """Frames run after every real-time wait and before every marker (see openxr_vr_smoke.py)."""
    guarded = []
    for command in commands:
        if command.startswith("condump vr_marker_"):
            guarded.append("wait 2")
        guarded.append(command)
        if command.startswith("waitMsec "):
            guarded.append("wait 3")
    return guarded


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--game", choices=sorted(CAMPAIGNS), default="baseoq4")
    parser.add_argument("--awakening", type=Path, help="the folder holding q4xbase (fs_awakeningpath), for --game q4xbase")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--eye-size", default="800x880")
    parser.add_argument("--reuse", action="store_true", help="check the last run's logs again without launching")
    options = parser.parse_args(argv)
    campaign = CAMPAIGNS[options.game]

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    if options.game == "q4xbase" and not (options.awakening and (options.awakening / "q4xbase").is_dir()):
        parser.error("--game q4xbase needs --awakening, the folder holding q4xbase")
    verify_runtime_is_current(staged, executable)

    output = (options.output_dir or ROOT / ".tmp/openxr-vr-haptics").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / options.game
    game = profile / options.game
    game.mkdir(parents=True, exist_ok=True)
    for stale in game.glob("vr_marker_*.txt"):
        stale.unlink()
    xr_log = profile / "xr.jsonl"
    log_path = game / "logs/openq4.log"
    if not options.reuse:
        xr_log.unlink(missing_ok=True)
        log_path.unlink(missing_ok=True)

    stages: list[str] = []
    stage_lines: list[int] = []
    script: list[str] = []
    commands: list[str] = ["waitMsec 3000", "echo VR_HAPTICS_ACTIVE", "notarget", "bind JOY15 _attack"]
    if not campaign["combat"]:
        # no hit is checked there, and the battle (and a goob shot's own
        # splash) must not wear the player down before the last shot
        commands.append("god")

    def stage(name: str, runtime_command: str = "head 0 0 0") -> None:
        """Opens a stage: the runtime's next script command splits its log there."""
        stages.append(name)
        stage_lines.append(len(script))
        script.append(f"when {game / f'vr_marker_{name}.txt'} {runtime_command}")
        commands.extend([f"echo VR_STAGE_{name}", f"condump vr_marker_{name}.txt", "waitMsec 300"])

    def fire(name: str, msec: int) -> None:
        script.append(f"when {game / f'vr_marker_{name}_on.txt'} button right trigger 1")
        script.append(f"when {game / f'vr_marker_{name}_off.txt'} button right trigger 0")
        commands.extend([f"condump vr_marker_{name}_on.txt", f"waitMsec {msec}",
                         f"condump vr_marker_{name}_off.txt", "waitMsec 500"])

    # draw and reload, for each gun
    for index, (weapon, impulse, rounds_msec) in enumerate(campaign["reloads"]):
        # from the blaster, so the gun is drawn whatever the map started with
        # (give may switch to a new gun itself)
        # (and ammunition beyond the clip, or there is nothing to reload from)
        commands.extend([f"give {weapon}", "give ammo", "waitMsec 500", "_impulse0", "waitMsec 2500"])
        stage(f"draw{index}")
        commands.extend([f"_impulse{impulse}", "waitMsec 2500"])
        stage(f"fire{index}")
        fire(f"shots{index}", rounds_msec)
        stage(f"reload{index}")
        commands.extend(["_impulse13", "waitMsec 4500"])
    if campaign["combat"]:
        # hits from the head's right, left and front; then from the room's forward
        # with the head turned 90 degrees left, which puts it on the head's right
        for name, degrees in (("hit_right", 90), ("hit_left", -90), ("hit_front", 0)):
            stage(name)
            commands.extend([f"testVRHaptics hit {degrees} 10", "waitMsec 1200"])
        stage("hit_turned", "head 90 0 0")
        commands.extend(["waitMsec 600", "testVRHaptics hit 0 10 room", "waitMsec 1200"])
        # blasts near, farther and out of reach (the impact's shake reaches 600 units)
        for name, distance in (("blast_near", 150), ("blast_mid", 400), ("blast_far", 900)):
            stage(name)
            commands.extend([f"testVRHaptics blast {distance}", "waitMsec 1500"])
    # vr_hapticEvents 0: no blast or draw, the shot still pulses
    stage("events_off")
    weapon, impulse, _ = campaign["reloads"][0]
    # (the shot first, from the gun in hand since its reload: a gun still
    # coming up after the swap would not fire)
    commands.extend(["vr_hapticEvents 0", "testVRHaptics blast 150", "waitMsec 1200"])
    fire("shots_off", 600)
    commands.extend(["_impulse0", "waitMsec 2500", f"_impulse{impulse}", "waitMsec 3000", "vr_hapticEvents 1"])
    if campaign["combat"]:
        x, y, z, yaw = Q4_GROUND_POSE
        stage("landing_hop")
        commands.extend([f"setviewpos {x} {y} {z + 30} {yaw}", "waitMsec 2000"])
        stage("landing_fall")
        commands.extend([f"setviewpos {x} {y} {z + 200} {yaw}", "waitMsec 3000"])
    stage("done")
    commands.extend(["echo VR_HAPTICS_COMPLETE", "quit"])

    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    (game / "openxr_vr_haptics.cfg").write_text("\n".join(frame_guarded(commands)) + "\n", encoding="utf-8")

    settings = {
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "0",
        "fs_game": options.game, "si_gameType": "singleplayer", "ui_retained": "1",
        "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_haptics.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1", "com_skipLogoVideos": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        "vr_enable": "1", "vr_setupDone": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_leftHanded": "0",
        "vr_hapticStrength": "1", "vr_hapticEvents": "1", "vr_menuHaptics": "0",
        "r_renderApi": options.render_api,
    }
    if options.game == "q4xbase":
        settings["fs_awakeningpath"] = str(options.awakening.resolve())
    args: list[str] = []
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", campaign["map"]]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")

    environment = dict(os.environ)
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(xr_log),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": options.eye_size,
        "OPENQ4_XR_TEST_RATE": "90",
        "SDL_AUDIODRIVER": "dummy",
    })
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    timed_out = False
    returncode = 0
    if not options.reuse:
        with (profile / "process.log").open("w", encoding="utf-8") as log:
            try:
                returncode = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT,
                                            env=environment, startupinfo=startup, timeout=options.timeout).returncode
            except subprocess.TimeoutExpired:
                timed_out = True

    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    events = read_events(xr_log)
    (profile / "xr_events.json").write_text(json.dumps(events, indent=2), encoding="utf-8")

    assert "VR_HAPTICS_ACTIVE" in text, "gameplay not reached"
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    assert not timed_out, f"the client did not finish within {options.timeout} s"
    assert returncode == 0, returncode
    assert "VR_HAPTICS_COMPLETE" in text, "the script did not complete"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), "errors in the log"

    # Split the runtime's log at each stage's script command: the runtime runs
    # its script in order, one command for each stage and two for each burst
    # of fire, so the stage commands are found by their place in the script.
    commands_run = [i for i, e in enumerate(events) if e.get("event") == "script_command"]
    assert len(commands_run) == len(script), f"the runtime ran {len(commands_run)} of {len(script)} script commands"
    bounds = [commands_run[i] for i in stage_lines] + [len(events)]
    pulses: dict[str, list[dict]] = {}
    for name, start, end in zip(stages, bounds, bounds[1:]):
        pulses[name] = [e for e in events[start:end] if e.get("event") == "haptic"]

    def logged(name: str) -> str:
        following = stages[stages.index(name) + 1] if stages.index(name) + 1 < len(stages) else None
        part = text.partition(f"VR_STAGE_{name}")[2]
        return part.partition(f"VR_STAGE_{following}")[0] if following else part

    def of(name: str, duration: int) -> list[dict]:
        return [e for e in pulses[name] if e["duration_ns"] == duration]

    summary = []
    # draws and reloads, in each gun's own hand
    for index, (weapon, _, _) in enumerate(campaign["reloads"]):
        draws = of(f"draw{index}", DRAW_NS)
        assert draws and all(e["hand"] == RIGHT for e in draws), f"drawing {weapon} did not pulse the weapon hand: {pulses[f'draw{index}']}"
        assert "VR haptic draw: weapon hand" in logged(f"draw{index}"), f"no draw pulse logged for {weapon}"
        shots = of(f"fire{index}", SHOT_NS)
        assert shots, f"firing {weapon} did not pulse the weapon hand"
        reloads = of(f"reload{index}", RELOAD_NS)
        assert reloads, f"reloading {weapon} did not tick the off hand: {pulses[f'reload{index}']}"
        assert all(e["hand"] == LEFT for e in reloads), f"a reload step must tick the off (left) hand: {reloads}"
        summary.append(f"{weapon} {len(reloads)} reload ticks")
    # every logged tick reached the runtime, and nothing else lasts 25 ms
    all_reloads = [e for e in events if e.get("event") == "haptic" and e["duration_ns"] == RELOAD_NS]
    reload_lines = text.count("VR haptic reload: off hand")
    assert len(all_reloads) == reload_lines, f"{len(all_reloads)} reload ticks reached the runtime, {reload_lines} logged"

    if campaign["combat"]:
        # hits weighted toward the side they came from, placed by the head
        def hit_split(name: str) -> tuple[float, float]:
            hits = of(name, HIT_NS)
            hands = {e["hand"]: e["amplitude"] for e in hits}
            assert len(hits) == 2 and set(hands) == {LEFT, RIGHT}, f"{name}: a hit should pulse both hands once: {pulses[name]}"
            return hands[LEFT], hands[RIGHT]
        left, right = hit_split("hit_right")
        assert right > 0.3 and abs(left - right * 0.25) < 0.02, f"a hit from the right: right {right:.3f}, left {left:.3f}"
        left, right = hit_split("hit_left")
        assert left > 0.3 and abs(right - left * 0.25) < 0.02, f"a hit from the left: left {left:.3f}, right {right:.3f}"
        left, right = hit_split("hit_front")
        assert abs(left - right) < 0.02, f"a hit from ahead should pulse both hands alike: left {left:.3f}, right {right:.3f}"
        left, right = hit_split("hit_turned")
        assert right > 0.3 and abs(left - right * 0.25) < 0.05, \
            f"with the head turned left, a hit from the room's forward comes from its right: right {right:.3f}, left {left:.3f}"
        assert re.search(r"VR haptic hit: both hands, amplitude [\d.]+ for 90 ms, side 0\.9\d|side 1\.00", logged("hit_right")), \
            "the hit from the right did not log its side"

        # blasts: near harder than farther, out of reach nothing, no damage
        def blast(name: str) -> float:
            found = of(name, BLAST_NS)
            if not found:
                return 0.0
            hands = {e["hand"]: e["amplitude"] for e in found}
            assert set(hands) == {LEFT, RIGHT} and abs(hands[LEFT] - hands[RIGHT]) < 0.01, f"{name}: {found}"
            assert not of(name, HIT_NS), f"{name}: the blast must not hurt"
            return hands[RIGHT]
        near, mid, far = blast("blast_near"), blast("blast_mid"), blast("blast_far")
        assert near > 0.5, f"a rocket's blast 150 units away should pulse both hands firmly ({near:.3f})"
        assert 0.0 < mid < near, f"a blast farther away should pulse more softly ({near:.3f} near, {mid:.3f} at 400)"
        assert far == 0.0, f"a blast beyond the shake's reach should not pulse ({far:.3f})"
        summary.append(f"blasts {near:.2f}/{mid:.2f}/{far:.2f}")

    # vr_hapticEvents 0 keeps the shots
    assert not of("events_off", BLAST_NS) and not of("events_off", DRAW_NS), \
        f"vr_hapticEvents 0 should silence blasts and draws: {pulses['events_off']}"
    assert of("events_off", SHOT_NS), "vr_hapticEvents 0 must keep the shot's pulse"

    if campaign["combat"]:
        assert not of("landing_hop", LANDING_NS), f"a drop no deeper than a jump should not jolt the hands: {pulses['landing_hop']}"
        landings = of("landing_fall", LANDING_NS)
        hands = {e["hand"]: e["amplitude"] for e in landings}
        assert set(hands) == {LEFT, RIGHT} and min(hands.values()) > 0.6, f"a 200-unit fall should jolt both hands: {landings}"
        assert "VR haptic landing: both hands" in logged("landing_fall"), "the landing pulse was not logged"
        summary.append(f"landing {hands[RIGHT]:.2f}")

    print(f"OpenXR VR haptics smoke ({options.game}, {options.render_api}): PASS ({', '.join(summary)}); evidence={profile}")


if __name__ == "__main__":
    main()
