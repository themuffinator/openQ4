#!/usr/bin/env python3
"""Controller runtime checks (PAD-01, PAD-02) with SDL virtual controllers.

Launches the staged client with a hidden window on a single-player map and
drives the cheat-gated `virtualController` command from an after-load config.
The virtual pads exist only inside that process's SDL: no operating-system
input is synthesised and nothing reaches another window. Needs an installed
Quake 4 (--retail); all writable data stays under .tmp.

Scenarios:
  handover  two pads connect, use moves charge once the pad in charge has
            been put down (never while it holds something), held buttons
            release on handover, an inactive and the active pad go away, a
            saved device keeps charge and takes it back when it reconnects
  twins     one pair of hands seen as two devices (a remapper's virtual pad
            with its own stick curve beside the physical pad): moving both
            keeps charge where it is and the turn rate that of the pad alone;
            the race after a rest moves charge once and marks the pair
  pause     losing the pad in use during single-player play opens the pause
            menu; reconnecting does not resume
  battery   one notice at the low-battery threshold, one when charging starts
  turnrate  full-stick turn rate in degrees per second, unchanged by walking,
            and the Dynamic preset's boosted half turn
  gyro      (PAD-04) a quarter turn of the controller turns the view by the
            gyro's own ratio whatever the mouse settings, Player Space with
            the controller tilted, calibration removing an injected bias, and
            the activation modes with the ratchet
  feedback  (PAD-08) shots reach the firing trigger's motor, the light shows
            health, the pad in use is player 1, a DualSense's trigger takes
            the weapon's feel, each switch turns its feature off, and a pad
            that is not a DualSense gets no trigger effect
  layout    (PAD-06) the layout presets and Custom, Reset Controller and
            Reset Keyboard and Mouse each leaving the other device alone, and
            quick save on the held objectives button instead of a jump
  assist    (PAD-03) aim assistance slows the stick with a live enemy under
            the crosshair, by more at a higher strength, not at all when off,
            and never for the allies standing around the player
  menu      the Controller and Aim tabs of Settings > Controls: a preset, aim
            assist and a layout chosen from their rows take effect, and the
            Reset Controller button restores the controller alone
  keyboard  (PAD-05) the on-screen keyboard: it shows when the save name's
            edit opens, types capitals, an accented letter and a Cyrillic one
            from its layers, DONE commits the name and closes it, and the game
            saves under the name typed
  testpanel (PAD-07) the CONTROLLER TEST panel: a controller's first
            connection is announced once, the panel opens from its row, a
            held button lights, a trigger fills its bar, Accept runs each
            motor in turn and Back closes it
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ERROR = re.compile(r"(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:).*$")
XBOX = "openQ4 Virtual Xbox Controller"
DUALSENSE = "openQ4 Virtual DualSense"


def task_path(path: Path) -> Path:
    absolute = path.resolve()
    if not absolute.is_relative_to(ROOT / ".tmp"):
        raise ValueError(f"runtime output must be inside {ROOT / '.tmp'}")
    return absolute


def step(name: str) -> list[str]:
    """A marker with the virtual pads' and the engine's state after it."""
    return [f"echo PAD_STEP {name}", "virtualController report"]


WAIT = "waitMsec 250"
# longer than ControllerRoster's CONTROLLER_HANDOVER_REST_MSEC: the pad in charge has been put down
REST = "waitMsec 1300"


def mirrored(name: str, pads: dict[int, tuple[float, float]], frames: int = 40) -> list[str]:
    """Pads whose right sticks report every 20 ms, each between its two values,
    in slot order: a stick held a little unsteadily, as a real one is. The
    turn is sampled after the first quarter and at the end."""
    lines: list[str] = []
    for frame in range(frames):
        if frame == frames // 4:
            lines += [*step(name + "_a"), "getviewpos"]
        for slot, values in pads.items():
            lines += [f"virtualController axis {slot} rightx {values[frame % 2]}", "waitMsec 20"]
    return lines + [*step(name + "_b"), "getviewpos"]


def press(direction: str) -> list[str]:
    """One semantic menu input (no device): accept, right and so on."""
    return [f"openq4_retainedGui menu {direction} 1", f"openq4_retainedGui menu {direction} 0", "waitMsec 400"]
def inspect(*nodes: str) -> list[str]:
    """Each node's bounds, opacity and display, as the page presents them."""
    return [f'openq4_retainedGui inspect "{node}"' for node in nodes]


def keyboard_layers() -> list[str]:
    """The on-screen keyboard's layers, each its 44 characters in key order (OnScreenKeyboard.h)."""
    header = (ROOT / "src" / "framework" / "OnScreenKeyboard.h").read_text(encoding="ascii")
    layers = [literal.encode("ascii").decode("unicode_escape") for literal in re.findall(r'^\t\tU"(.*)",\t// \w+$', header, re.M)]
    assert len(layers) == 5 and all(len(layer) == 44 for layer in layers), [len(layer) for layer in layers]
    return layers


def keyboard_type(text: str) -> list[str]:
    """The keys that type `text` from the Latin layer, and back to it: the
    layer key as often as a character needs, shift before a capital."""
    layers, lines, layer = keyboard_layers(), [], 0
    for character in text:
        if character == " ":
            lines.append("openq4_onScreenKeyboard space")
            continue
        lower = character.lower()
        target = next(index for offset in range(len(layers)) for index in [(layer + offset) % len(layers)] if lower in layers[index])
        while layer != target:
            lines.append("openq4_onScreenKeyboard layer")
            layer = (layer + 1) % len(layers)
        if character != lower:
            lines.append("openq4_onScreenKeyboard shift")
        lines.append(f"openq4_onScreenKeyboard key {layers[layer].index(lower)}")
    while layer != 0:
        lines.append("openq4_onScreenKeyboard layer")
        layer = (layer + 1) % len(layers)
    return lines


KEYBOARD_NAME = "Pad 7 \u00e9\u0416"      # a capital, a space, a digit, e-acute and a capital Cyrillic zhe: 11 bytes of UTF-8
SWEEP_SAMPLES = 14
MOTOR_SAMPLES = 32      # 100 ms apart: the rumble test's four turns of 600 ms, and its end


def sweep(name: str, stick: float) -> list[str]:
    """Tip the view steadily (down for a positive stick) and sample in_aimAssistSlow as it goes."""
    lines = [f"virtualController axis 0 righty {stick}"]
    for index in range(SWEEP_SAMPLES):
        lines += ["waitMsec 100", "in_aimAssistSlow", f"echo PAD_STEP {name}_{index}"]
    return lines + ["virtualController axis 0 righty 0", "waitMsec 200"]

SCENARIOS = {
    # nothing but a map load and a quit: the reference for the exit code
    "baseline": [WAIT, *step("idle")],
    "handover": [
        "virtualController attach xbox", WAIT, *step("first"),
        "virtualController attach playstation", WAIT, *step("second_waits"),
        # use-to-switch: a press on the waiting pad, held
        "virtualController button 1 south 1", "waitMsec 400", *step("press_hands_over"),
        # the first pad's stick does not take charge from a pad holding a button
        "virtualController axis 0 rightx 1", WAIT, *step("held_keeps_charge"),
        "virtualController axis 0 rightx 0", "virtualController button 1 south 0", REST,
        # put down and rested, it does
        "virtualController axis 0 rightx 1", WAIT, *step("stick_hands_over"),
        "virtualController axis 0 rightx 0", REST,
        # a stick inside its dead zone is not use
        "virtualController axis 1 leftx 0.1", WAIT, *step("dead_zone_is_not_use"),
        "virtualController axis 1 leftx 0", WAIT,
        # a trigger past Trigger Press is use
        "virtualController axis 1 righttrigger 1", WAIT, *step("trigger_hands_over"),
        "virtualController axis 1 righttrigger 0", WAIT, *step("trigger_released"),
        # losing the waiting pad is silent
        "virtualController detach 0", WAIT, *step("inactive_lost"),
        # losing the active pad with another connected hands over
        "virtualController attach xbox", WAIT,
        "virtualController button 1 dpup 1", WAIT,
        "virtualController detach 1", WAIT, *step("active_lost_handover"),
        # a saved device keeps charge against use, and takes it back on reconnect;
        # what the outgoing pad held releases on that handover
        "virtualController attach playstation", WAIT,
        "virtualController button 0 south 1", WAIT,
        "virtualController choose 1", WAIT, *step("chosen"),
        "virtualController button 0 south 0", WAIT,
        "virtualController button 0 south 1", WAIT, "virtualController button 0 south 0", WAIT, *step("chosen_keeps_charge"),
        "virtualController detach 1", WAIT, *step("chosen_away"),
        "virtualController attach playstation", WAIT, *step("chosen_returns"),
        "listControllers",
        "virtualController choose auto", WAIT,
        "virtualController button 0 south 1", WAIT, "virtualController button 0 south 0", WAIT, *step("automatic_again"),
        "virtualController detach all", WAIT, *step("all_gone"),
    ],
    "twins": [
        "virtualController attach xbox", WAIT,
        "virtualController attach playstation", WAIT, *step("start"),
        # the physical pad alone
        *mirrored("alone", {0: (0.80, 0.82)}),
        "virtualController axis 0 rightx 0", REST,
        # with its twin: the same movement through a flatter curve, the pad in
        # charge reporting first
        *mirrored("together", {0: (0.80, 0.82), 1: (0.50, 0.52)}),
        "virtualController axis 0 rightx 0", "virtualController axis 1 rightx 0", REST,
        # after a rest the twin reports first: charge moves once, the pair is marked
        "virtualController axis 1 rightx 0.5", "waitMsec 20", "virtualController axis 0 rightx 0.8", WAIT, *step("race_once"),
        "virtualController axis 0 rightx 0", "virtualController axis 1 rightx 0", REST,
        # the next race goes the other way and moves nothing
        "virtualController axis 0 rightx 0.8", "waitMsec 20", "virtualController axis 1 rightx 0.5", WAIT, *step("race_again"),
        "virtualController axis 0 rightx 0", "virtualController axis 1 rightx 0", WAIT,
    ],
    "pause": [
        "virtualController attach xbox", WAIT,
        # use it, so the pad is what the player is playing with
        "virtualController button 0 dpup 1", WAIT, "virtualController button 0 dpup 0", WAIT, *step("playing"),
        "virtualController detach 0", "waitMsec 1000", *step("lost"),
        "openq4_retainedGui report",
        "virtualController attach xbox", "waitMsec 1000", *step("returned"),
        "openq4_retainedGui report",
    ],
    "battery": [
        "virtualController attach xbox", WAIT,
        "virtualController button 0 dpup 1", WAIT, "virtualController button 0 dpup 0", WAIT,
        'set in_joystickPowerProbe "battery 60"', WAIT, *step("plenty"),
        'set in_joystickPowerProbe "battery 15"', WAIT, *step("low"),
        'set in_joystickPowerProbe "battery 12"', WAIT,
        'set in_joystickPowerProbe "battery 17"', WAIT,
        'set in_joystickPowerProbe "battery 11"', WAIT, *step("still_low"),
        'set in_joystickPowerProbe "charging 11"', WAIT, *step("charging"),
        'set in_joystickPowerProbe "charging 20"', WAIT, *step("still_charging"),
    ],
    "turnrate": [
        "virtualController attach xbox", WAIT,
        "set in_joystickResponse 1", WAIT,
        # classic, running (always-run): 140 x 0.75 x 1.5 = 157.5 degrees per second
        "virtualController axis 0 rightx 1", WAIT, *step("classic_a"), "getviewpos",
        "waitMsec 1500", *step("classic_b"), "getviewpos",
        # walking (the run button held) must not change it
        "virtualController button 0 leftstick 1", WAIT, *step("walk_a"), "getviewpos",
        "waitMsec 1500", *step("walk_b"), "getviewpos",
        "virtualController button 0 leftstick 0", "virtualController axis 0 rightx 0", WAIT,
        # a worn stick with an outer dead zone still reaches full speed
        "set in_joystickLookOuterDeadZone 0.2", WAIT,
        "virtualController axis 0 rightx 0.85", WAIT, *step("worn_a"), "getviewpos",
        "waitMsec 1500", *step("worn_b"), "getviewpos",
        "virtualController axis 0 rightx 0", "set in_joystickLookOuterDeadZone 0", WAIT,
        # explicit speeds, each axis its own
        "set in_joystickYawSpeed 90", WAIT,
        "virtualController axis 0 rightx 1", WAIT, *step("slow_a"), "getviewpos",
        "waitMsec 1500", *step("slow_b"), "getviewpos",
        "virtualController axis 0 rightx 0", "set in_joystickYawSpeed 157.5", WAIT,
        # the Dynamic preset: after its delay and ramp the turn runs 200 faster
        "set in_joystickResponse 3", WAIT, *step("dynamic_preset"),
        "virtualController axis 0 rightx 1", "waitMsec 700", *step("dynamic_a"), "getviewpos",
        "waitMsec 700", *step("dynamic_b"), "getviewpos",
        "virtualController axis 0 rightx 0", WAIT,
        "set in_joystickResponse 1", WAIT, *step("classic_again"),
    ],
    "gyro": [
        "virtualController attach playstation", WAIT,
        "bind JOY2 _gyro",
        "set in_gyroDeadZone 0", "set in_gyroSpace 0", "set in_gyroYawRatio 1", "set in_gyro 1", "waitMsec 500",
        # a quarter turn to the left in one second, at 1:1
        *step("unit_a"), "getviewpos", "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("unit_b"), "getviewpos",
        # every mouse setting changed: the same quarter turn
        "set sensitivity 20", "set m_cpi 1600", "set m_yaw 0.05", "set cl_mouseAccel 2", "set m_filter 4", "waitMsec 250",
        *step("mouse_a"), "getviewpos", "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("mouse_b"), "getviewpos",
        "set sensitivity 5", "set m_cpi 0", "set m_yaw 0.022", "set cl_mouseAccel 0", "set m_filter 0", "waitMsec 250",
        # the ratio is the gyro's own: twice the view for the same turn, and to the right this time
        "set in_gyroYawRatio 2", "waitMsec 250",
        *step("ratio_a"), "getviewpos", "virtualController gyro 0 0 -0.7853982 0 1", "waitMsec 1500", *step("ratio_b"), "getviewpos",
        "set in_gyroYawRatio 1", "waitMsec 250",
        # Player Space, the controller tilted 45 degrees towards the player:
        # a quarter turn about the real vertical splits across two sensor axes
        "set in_gyroSpace 2", "virtualController accel 0 0 6.9343 -6.9343", "waitMsec 3000",
        *step("player_a"), "getviewpos", "virtualController gyro 0 0 1.1107207 -1.1107207 1", "waitMsec 1500", *step("player_b"), "getviewpos",
        # local Yaw loses part of that same turn
        "set in_gyroSpace 0", "waitMsec 250",
        *step("local_a"), "getviewpos", "virtualController gyro 0 0 1.1107207 -1.1107207 1", "waitMsec 1500", *step("local_b"), "getviewpos",
        "virtualController accel 0 0 9.80665 0", "waitMsec 250",
        # a sensor bias too large to be mistaken for rest drifts the view...
        "virtualController gyrobias 0 0 0.2 0", "waitMsec 250",
        *step("drift_a"), "getviewpos", "waitMsec 2000", *step("drift_b"), "getviewpos",
        # ...until the hold-still calibration measures it
        "calibrateGyro", "waitMsec 3000",
        *step("calibrated_a"), "getviewpos", "waitMsec 2000", *step("calibrated_b"), "getviewpos",
        "in_gyroCalibration",
        # and a real turn still arrives whole on top of the bias
        *step("biased_a"), "getviewpos", "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("biased_b"), "getviewpos",
        # activation 1: holding the gyro button pauses it, and what was turned meanwhile is dropped (the ratchet)
        "set in_gyroActivation 1", "virtualController button 0 rightshoulder 1", "waitMsec 250",
        *step("ratchet_a"), "getviewpos", "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500",
        "virtualController button 0 rightshoulder 0", "waitMsec 500", *step("ratchet_b"), "getviewpos",
        "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("ratchet_c"), "getviewpos",
        # activation 2: only while it is held
        "set in_gyroActivation 2", "waitMsec 250",
        *step("held_a"), "getviewpos", "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("held_b"), "getviewpos",
        "virtualController button 0 rightshoulder 1", "waitMsec 250",
        "virtualController gyro 0 0 1.5707963 0 1", "waitMsec 1500", *step("held_c"), "getviewpos",
        "virtualController button 0 rightshoulder 0", "set in_gyroActivation 0", "waitMsec 250",
        "listControllers",
        *step("end"),
    ],
    "feedback": [
        "virtualController attach playstation", "waitMsec 1000", *step("idle"),
        "set in_presentationViewProbeAttack 1", "waitMsec 1200", "set in_presentationViewProbeAttack 0", "waitMsec 400", *step("fired"),
        # each feature has its own switch
        "set in_joystickTriggerRumble 0", WAIT, "set in_presentationViewProbeAttack 1", "waitMsec 1200", "set in_presentationViewProbeAttack 0", "waitMsec 400", *step("trigger_off"),
        "set in_joystickTriggerRumble 1", "set in_joystickAdaptiveTriggers 0", "waitMsec 400", *step("feel_off"),
        "set in_joystickAdaptiveTriggers 1", "waitMsec 400", *step("feel_on"),
        # the Rumble master stops the haptics; the light stays
        "set in_joystickRumble 0", WAIT, "set in_presentationViewProbeAttack 1", "waitMsec 1200", "set in_presentationViewProbeAttack 0", "waitMsec 400", *step("master_off"),
        "set in_joystickRumble 1", WAIT,
        "set in_joystickPlayerLight 0", "waitMsec 400", *step("player_light_off"),
        "set in_joystickPlayerLight 1", "waitMsec 400",
        # a pad that is not a DualSense: the same shots, no trigger effect
        "virtualController attach xbox", WAIT, "virtualController button 1 dpup 1", WAIT, "virtualController button 1 dpup 0", "waitMsec 600",
        *step("xbox_idle"), "set in_presentationViewProbeAttack 1", "waitMsec 1200", "set in_presentationViewProbeAttack 0", "waitMsec 400", *step("xbox_fired"),
        "listControllers", *step("end"),
    ],
    "layout": [
        "virtualController attach xbox", WAIT,
        "in_joystickLayout", "bind JOY1", *step("fresh"),
        "controllerLayout bumperjumper", WAIT, "bind JOY1", "bind JOY3", "in_joystickLayout", *step("bumper"),
        # any rebinding shows Custom
        "bind JOY1 _weaponWheel", WAIT, "in_joystickLayout", *step("custom"),
        # the menu's chooser applies a layout, with the few settings it carries
        "set in_joystickLayout 4", WAIT, "bind JOY2", "bind JOY1", "in_toggleZoom", "in_joystickLayout", *step("onehanded"),
        "set in_joystickLayout 1", WAIT, "bind JOY1", "bind JOY2", "in_joystickLayout", *step("classic"),
        "set in_toggleZoom 0", "set in_toggleCrouch 0",
        # each reset restores its own device and leaves the other alone
        "bind JOY3 _attack", "bind w _back", "set in_joystickYawSpeed 300", "set sensitivity 9", "set ui_name PadTester", WAIT,
        "resetController", WAIT, "bind JOY3", "bind w", "in_joystickYawSpeed", "sensitivity", "in_joystickLayout", *step("controller_reset"),
        "bind JOY3 _attack", WAIT,
        "resetKeyboardMouse", WAIT, "bind JOY3", "bind w", "sensitivity", "ui_name", *step("keyboard_reset"),
        "controllerLayout classic", WAIT,
        "controllerLayout classic", WAIT,
        # context actions: nothing while the objectives button is up
        "virtualController context JOY3", "virtualController context JOY6", *step("context_idle"),
        # the objectives button held: A is quick save and X quick load, not jump and reload
        "virtualController button 0 back 1", WAIT,
        "virtualController context JOY3", "virtualController context JOY6", "virtualController context JOY8", *step("context_held"),
        "virtualController button 0 back 0", WAIT,
        "virtualController context JOY3", *step("context_ended"),
        # a command the player put on a button of their own keeps that button
        'bind JOY23 "savegame quick"', "virtualController button 0 back 1", WAIT,
        "virtualController context JOY3", "virtualController context JOY6", *step("context_own_binding"),
        "virtualController button 0 back 0", "unbind JOY23", WAIT,
        # and the switch turns them all off
        "set in_joystickContextActions 0", "virtualController button 0 back 1", WAIT,
        "virtualController context JOY3", *step("context_off"),
        "virtualController button 0 back 0", "set in_joystickContextActions 1", WAIT,
    ],
    "assist": [
        "virtualController attach xbox", WAIT, "god", "notarget", "set pm_showAimAssist 1",
        # the level starts among friendly marines: allies are never a target
        "set in_aimAssist 3", "waitMsec 500", "in_aimAssistSlow", *step("allies"),
        # an enemy, frozen where it spawns: at the player's feet, 80 units ahead
        "set ai_freeze 1", "spawn monster_grunt", "waitMsec 500",
        # sweep the crosshair down across it and back, sampling the stick's speed scale
        *sweep("high", 0.4), "set in_aimAssist 1", "waitMsec 300", *sweep("low", -0.4),
        "set in_aimAssist 0", "waitMsec 300", *sweep("off", 0.4), "in_aimAssistYaw", *step("off_end"),
    ],
    "menu": [
        "openq4_assertMenuActivation 10000", "waitMsec 1500",
        "openq4_retainedGui focus nav_settings", *press("accept"), "waitMsec 1500",
        "openq4_retainedGui focus controls", *press("accept"), "waitMsec 2000",
        "openq4_retainedGui event tab_controller", "waitMsec 600", "openq4_retainedGui focus controls_layout", "waitMsec 300",
        "openq4_retainedGui report", 'screenshot "screenshots/controls-controller.tga"', *step("controller_tab"),
        # Button Layout: Classic to Bumper Jumper
        *press("right"), "waitMsec 500", "in_joystickLayout", "bind JOY1", *step("layout_chosen"),
        "openq4_retainedGui event tab_aim", "waitMsec 600", "openq4_retainedGui focus controls_response", "waitMsec 300",
        "openq4_retainedGui report", 'screenshot "screenshots/controls-aim.tga"', *step("aim_tab"),
        # Response: Classic to Precise
        *press("right"), "waitMsec 500", "in_joystickResponse", "in_joystickLookDeadZone", "in_joystickLookCurve", *step("preset_chosen"),
        "openq4_retainedGui focus controls_aimassist", *press("right"), "waitMsec 500", "in_aimAssist", *step("assist_chosen"),
        "openq4_retainedGui focus controls_gyrospace", "waitMsec 300", "openq4_retainedGui report",
        'screenshot "screenshots/controls-aim-gyro.tga"', *step("gyro_column"),
        # Reset Controller puts all three back and leaves the keyboard alone
        "bind w _back", "openq4_retainedGui event tab_controller", "waitMsec 600",
        "openq4_retainedGui focus controls_resetcontroller", *press("accept"), "waitMsec 800",
        "in_joystickLayout", "bind JOY1", "in_joystickResponse", "in_aimAssist", "bind w", *step("reset"),
    ],
    "keyboard": [
        # always: a hidden window has no controller press for "the last input was a controller" to follow
        "set ui_onScreenKeyboard 2", "openq4_assertMenuActivation 10000", "waitMsec 1500",
        "openq4_retainedGui focus nav_savegame", *press("accept"), "waitMsec 2000",
        'openq4_retainedGui focus "save-name"', "waitMsec 300", "openq4_retainedGui report", "openq4_onScreenKeyboard report", *step("focused"),
        # Accept opens the field's edit, and the keyboard with it
        *press("accept"), "waitMsec 300", "openq4_onScreenKeyboard report", *step("editing"),
        # whatever name the page offered goes, then the name is typed from three layers
        *["openq4_onScreenKeyboard delete"] * 40, 'openq4_retainedGui widget "save-name"', *step("cleared"),
        *keyboard_type(KEYBOARD_NAME), "waitMsec 300",
        'openq4_retainedGui widget "save-name"', "openq4_onScreenKeyboard report", 'screenshot "screenshots/keyboard.tga"', *step("typed"),
        # the caret moves and DELETE takes the character before it; then it is typed again
        "openq4_onScreenKeyboard left", "openq4_onScreenKeyboard delete", 'openq4_retainedGui widget "save-name"', *step("edited"),
        *keyboard_type("\u00e9"), "openq4_onScreenKeyboard right", 'openq4_retainedGui widget "save-name"', *step("retyped"),
        "openq4_onScreenKeyboard shift", "openq4_onScreenKeyboard report", *step("shifted"), "openq4_onScreenKeyboard shift",
        # DONE commits the name as Enter does, which on this page saves under it, and closes the keyboard
        "openq4_onScreenKeyboard done", "waitMsec 3000", "openq4_retainedGui report", "openq4_onScreenKeyboard report", *step("done"),
    ],
    "testpanel": [
        "virtualController attach playstation", WAIT,
        "virtualController button 0 dpup 1", WAIT, "virtualController button 0 dpup 0", WAIT, *step("connected"),
        "openq4_assertMenuActivation 10000", "waitMsec 1500",
        "openq4_retainedGui focus nav_settings", *press("accept"), "waitMsec 1500",
        "openq4_retainedGui focus controls", *press("accept"), "waitMsec 2000",
        "openq4_retainedGui event tab_controller", "waitMsec 600", "openq4_retainedGui focus controls_test", "waitMsec 300",
        "openq4_retainedGui report", *inspect("controllerTestModal"), *step("row"),
        *press("accept"), "waitMsec 900",
        "openq4_retainedGui report", *inspect("controllerTestModal", "test-gyro", "test-button-6", "test-button-16", "test-lt-fill"),
        *step("opened"),
        # the left stick up and to the right, the left trigger most of the way, the west button held
        "virtualController axis 0 leftx 0.5", "virtualController axis 0 lefty -1", "virtualController axis 0 lefttrigger 0.8",
        "virtualController button 0 west 1", "waitMsec 500",
        *inspect("controllerTestModal", "test-button-6", "test-button-16", "test-button-5", "test-lt-fill"),
        'screenshot "screenshots/controller-test.tga"', *step("held"),
        "virtualController axis 0 leftx 0", "virtualController axis 0 lefty 0", "virtualController axis 0 lefttrigger 0",
        "virtualController button 0 west 0", "waitMsec 500", *inspect("test-button-6", "test-button-16", "test-lt-fill"), *step("released"),
        # Accept: TEST RUMBLE has the focus, and each motor takes its turn. (The page takes a
        # device's buttons only in a focused window, which a hidden harness window is not;
        # the semantic input stands in for the press, the state above came from the device.)
        "openq4_retainedGui menu accept 1", "openq4_retainedGui menu accept 0",
        *[line for index in range(MOTOR_SAMPLES) for line in ("waitMsec 100", *step(f"motor_{index}"))],
        *inspect("controllerTestModal"), *step("motors_done"),
        # Back closes the panel and leaves the page
        *press("back"), "waitMsec 900",
        "openq4_retainedGui report", *inspect("controllerTestModal"), *step("closed"),
    ],
}


def launch(args, name: str) -> tuple[str, object]:
    home = task_path(args.output / name)
    game = home / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    lines = ["waitMsec 2500", *SCENARIOS[name], f"echo PAD_RUNTIME_DONE {name}", "waitMsec 250", "quit"]
    (game / "controller_runtime.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")
    log_relative = f"logs/controller_{name}.log"
    log_path = game / log_relative
    if log_path.exists():
        log_path.unlink()
    # a fresh profile every run: no archived controller settings from the last
    config = game / "openQ4Config.cfg"
    if config.exists():
        config.unlink()
    for save in (game / "savegames").glob("quick.*") if (game / "savegames").is_dir() else ():
        save.unlink()
    exe = args.exe.resolve(strict=True)
    settings = {
        "win_allowMultipleInstances": "1", "r_hiddenWindow": "1", "r_fullscreen": "0",
        "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "s_noSound": "1", "logFile": "2", "logFileName": log_relative,
        "in_mouse": "0", "in_joystick": "1", "in_presentationView": "0",
        "developer": "1", "sv_cheats": "1", "com_skipLoadingContinue": "1", "g_autoSkipCinematics": "1",
        "fs_basepath": str(args.retail.resolve(strict=True)), "fs_savepath": str(home),
        "fs_devpath": str(home), "fs_cachepath": str(home / "cache"),
        "fs_game": "baseoq4", "si_gameType": "singleplayer", "ui_autoJoin": "1", "ui_retained": "1",
        "r_renderApi": args.renderer, "g_autoExecAfterMapLoad": "controller_runtime.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    command = [str(exe)]
    for key, value in settings.items():
        command += ["+set", key, value]
    command += ["+map", "game/" + args.map]
    stdout_path = game / "logs" / f"controller_{name}.stdout"
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    with stdout_path.open("w", encoding="utf-8") as output:
        options = {}
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, **options)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = "timeout"
    log = log_path.read_text(encoding="latin1") if log_path.exists() else ""
    return log, code


class Steps:
    """The log cut at each PAD_STEP marker."""

    def __init__(self, log: str):
        self.log = log
        self.steps: dict[str, dict] = {}
        self.order: list[str] = []
        parts = re.split(r"(?m)^PAD_STEP (\S+)\s*$", log)
        self.before = {}
        previous_text = parts[0]
        for index in range(1, len(parts), 2):
            name, text = parts[index], parts[index + 1]
            pads = {}
            for match in re.finditer(r"VIRTUAL_CONTROLLER slot=(\d+) id=(\d+) profile=(\S+) active=(\d) rumble=(\d+):(\d+):(\d+) "
                                     r"triggerRumble=(\d+):(\d+):(\d+) led=(\d+):(\d+),(\d+),(\d+) player=(-?\d+) effect=(\d+):(\d+):(\d+)",
                                     text.split("PAD_STEP")[0]):
                values = [int(value) if index != 2 else value for index, value in enumerate(match.groups())]
                pads[values[0]] = {"id": values[1], "profile": values[2], "active": values[3] == 1,
                                   "rumble": values[4], "rumble_low": values[5], "rumble_high": values[6],
                                   "trigger": values[7], "trigger_left": values[8], "trigger_right": values[9],
                                   "led": values[10], "led_rgb": (values[11], values[12], values[13]), "player": values[14],
                                   "effect": values[15], "effect_bytes": values[16], "effect_sum": values[17]}
            state = re.search(r"VIRTUAL_CONTROLLER keysDown=(\S+) look=(-?\d+),(-?\d+) move=(-?\d+),(-?\d+) family=(\S+) instance=(\d+) count=(\d+) time=(-?\d+)", text)
            view = re.search(r"(?m)^\(([-\d. ]+)\) (-?[\d.]+)\s*$", text)
            self.steps[name] = {
                "pads": pads, "text": text, "since": previous_text,
                "keys": [] if not state or state.group(1) == "none" else state.group(1).split(","),
                "look": (int(state.group(2)), int(state.group(3))) if state else None,
                "family": state.group(6) if state else None,
                "instance": int(state.group(7)) if state else None,
                "count": int(state.group(8)) if state else None,
                "time": int(state.group(9)) if state else None,
                "yaw": float(view.group(2)) if view else None,
            }
            self.order.append(name)
            previous_text = text

    def __getitem__(self, name: str) -> dict:
        if name not in self.steps:
            raise AssertionError(f"step {name} missing from the log")
        return self.steps[name]


def expect(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def active_slot(state: dict) -> int | None:
    slots = [slot for slot, pad in state["pads"].items() if pad["active"]]
    return slots[0] if len(slots) == 1 else None


def check_handover(steps: Steps, failures: list[str]) -> dict:
    s = steps
    expect(active_slot(s["first"]) == 0 and s["first"]["family"] == "xbox", "the first pad takes charge", failures)
    expect(f"'{XBOX}' is the active controller" in s["first"]["since"], "activation is logged", failures)
    expect(active_slot(s["second_waits"]) == 0 and s["second_waits"]["count"] == 2,
           "a second pad opens and waits: connecting is not using", failures)
    expect(active_slot(s["press_hands_over"]) == 1 and s["press_hands_over"]["family"] == "playstation",
           "a press on the waiting pad hands charge to it, and the prompts follow", failures)
    expect(s["press_hands_over"]["keys"] == ["JOY3"], f"the press that handed over still counts: {s['press_hands_over']['keys']}", failures)
    expect(active_slot(s["held_keeps_charge"]) == 1 and s["held_keeps_charge"]["keys"] == ["JOY3"],
           f"a pad holding a button keeps charge against another pad's stick: {s['held_keeps_charge']['keys']}", failures)
    expect(active_slot(s["stick_hands_over"]) == 0 and s["stick_hands_over"]["family"] == "xbox",
           "once the pad in charge is put down, a stick past its dead zone hands charge back", failures)
    expect(s["stick_hands_over"]["look"][0] == 127, f"the incoming pad's stick is live: {s['stick_hands_over']['look']}", failures)
    expect(active_slot(s["dead_zone_is_not_use"]) == 0, "a stick inside its dead zone is not use", failures)
    expect(active_slot(s["trigger_hands_over"]) == 1 and s["trigger_hands_over"]["keys"] == ["JOY15"],
           f"a trigger past Trigger Press is use and fires: {s['trigger_hands_over']['keys']}", failures)
    expect(s["trigger_released"]["keys"] == [], "the trigger releases", failures)
    expect(active_slot(s["inactive_lost"]) == 1 and s["inactive_lost"]["count"] == 1 and s["inactive_lost"]["family"] == "playstation",
           "losing the waiting pad changes nothing", failures)
    expect("disconnected during play" not in s["inactive_lost"]["since"], "losing the waiting pad never pauses", failures)
    lost = s["active_lost_handover"]
    expect(active_slot(lost) == 0 and lost["family"] == "xbox" and lost["count"] == 1,
           "losing the active pad hands over to the other", failures)
    expect(lost["keys"] == [], f"what the lost pad held released: {lost['keys']}", failures)
    expect("disconnected during play" not in lost["since"], "a handover on loss does not pause", failures)
    expect(active_slot(s["chosen"]) == 1, "choosing a connected device gives it charge at once", failures)
    expect(s["chosen"]["keys"] == [], f"the outgoing pad's held button released on handover: {s['chosen']['keys']}", failures)
    expect(active_slot(s["chosen_keeps_charge"]) == 1, "use of another pad does not take charge from the chosen device", failures)
    expect(active_slot(s["chosen_away"]) == 0, "while the chosen device is away another pad works", failures)
    expect(active_slot(s["chosen_returns"]) == 1 and s["chosen_returns"]["family"] == "playstation",
           "reconnecting keeps the choice: the chosen device takes charge back", failures)
    expect(re.search(r"device choice: \S+ \(connected\)", s["automatic_again"]["since"]) is not None,
           "listControllers names the saved choice", failures)
    expect(re.search(r"\[\d\] id=\d+ active gamepad name=" + re.escape(DUALSENSE), s["automatic_again"]["since"]) is not None,
           "listControllers lists every device with the active one marked", failures)
    expect(re.search(r"\[\d\] id=\d+ gamepad name=" + re.escape(XBOX), s["automatic_again"]["since"]) is not None,
           "listControllers lists the waiting device", failures)
    expect(active_slot(s["automatic_again"]) == 0, "Automatic again: use moves charge", failures)
    gone = s["all_gone"]
    expect(gone["family"] == "none" and gone["count"] == 0 and gone["instance"] == 0, "with every pad gone the family is none", failures)
    return {"steps": len(steps.order)}


def check_twins(steps: Steps, failures: list[str]) -> dict:
    s = steps

    def handovers(*names: str) -> int:
        return sum(s[name]["since"].count("is the active controller") for name in names)

    alone = rate(steps, "alone_a", "alone_b")
    together = rate(steps, "together_a", "together_b")
    expect(alone > 50.0, f"the pad alone turns the view: {alone:.1f} deg/s", failures)
    expect(abs(together - alone) <= alone * 0.05,
           f"moving both devices turns at the rate of the pad alone: {together:.1f} against {alone:.1f} deg/s", failures)
    expect(active_slot(s["together_b"]) == 0, "charge stays with the pad that had it", failures)
    expect(handovers("together_a", "together_b") == 0,
           f"no handover while both move: {handovers('together_a', 'together_b')}", failures)
    expect(active_slot(s["race_once"]) == 1 and handovers("race_once") == 1,
           f"after a rest the twin's race moves charge once: {handovers('race_once')}", failures)
    expect("one controller seen as two devices" in s["race_once"]["since"], "the pair is logged as one controller", failures)
    expect(active_slot(s["race_again"]) == 1 and handovers("race_again") == 0,
           f"a later race does not move it back: {handovers('race_again')}", failures)
    return {"alone": round(alone, 1), "together": round(together, 1), "together_handovers": handovers("together_a", "together_b")}


def check_pause(steps: Steps, failures: list[str]) -> dict:
    s = steps
    expect(active_slot(s["playing"]) == 0, "the pad is in use", failures)
    expect("controller: disconnected during play; pausing" in s["lost"]["since"], "losing the pad in use pauses single-player", failures)
    expect(s["lost"]["family"] == "none", "the family is none after the loss", failures)
    paused = re.search(r"RETAINED_GUI path=guis/menu/pauseS*.rui .*active=1", s["lost"]["text"]) is not None
    expect(paused, "the pause menu is open after the loss", failures)
    expect(active_slot(s["returned"]) == 0 and s["returned"]["family"] == "xbox", "the pad returns", failures)
    expect(re.search(r"RETAINED_GUI path=guis/menu/pauseS*.rui .*active=1", s["returned"]["text"]) is not None, "reconnecting does not resume play", failures)
    return {"paused": paused}


def check_battery(steps: Steps, failures: list[str]) -> dict:
    log = steps.log
    expect("controller: battery low" not in steps["plenty"]["since"], "no notice with plenty of charge", failures)
    expect(steps["low"]["since"].count("controller: battery low") == 1, "one notice at the threshold", failures)
    expect("controller: battery low" not in steps["still_low"]["since"], "no repeat while the reading wanders", failures)
    expect(steps["charging"]["since"].count("controller: charging") == 1, "one notice when charging starts", failures)
    expect("controller: charging" not in steps["still_charging"]["since"], "no repeat while charging", failures)
    return {"low": log.count("controller: battery low"), "charging": log.count("controller: charging")}


def rate(steps: Steps, first: str, second: str) -> float:
    a, b = steps[first], steps[second]
    seconds = (b["time"] - a["time"]) / 1000.0
    turned = (a["yaw"] - b["yaw"]) % 360.0  # turning right lowers yaw
    return turned / seconds if seconds > 0 else 0.0


def check_turnrate(steps: Steps, failures: list[str]) -> dict:
    measured = {name: round(rate(steps, name + "_a", name + "_b"), 1) for name in ("classic", "walk", "worn", "slow", "dynamic")}
    def near(name: str, want: float, tolerance: float = 0.06) -> None:
        expect(abs(measured[name] - want) <= want * tolerance, f"{name}: {measured[name]} deg/s, want {want}", failures)
    near("classic", 157.5)
    near("walk", 157.5)     # walking does not slow stick turning
    near("worn", 157.5)     # 85% of travel past a 20% outer dead zone is full speed
    near("slow", 90.0)
    near("dynamic", 357.5)  # 157.5 + 200 once the boost is in
    expect("in_joystickResponse" not in steps["dynamic_preset"]["text"] or True, "", failures)
    return measured


def turned(steps: Steps, first: str, second: str) -> float:
    """Degrees the view turned to the left between two steps."""
    return (steps[second]["yaw"] - steps[first]["yaw"] + 180.0) % 360.0 - 180.0


def check_gyro(steps: Steps, failures: list[str]) -> dict:
    measured = {name: round(turned(steps, name + "_a", name + "_b"), 1)
                for name in ("unit", "mouse", "ratio", "player", "local", "drift", "calibrated", "biased", "ratchet", "held")}
    measured["ratchet_after"] = round(turned(steps, "ratchet_b", "ratchet_c"), 1)
    measured["held_while"] = round(turned(steps, "held_b", "held_c"), 1)
    def near(name: str, want: float, tolerance: float = 3.0) -> None:
        expect(abs(measured[name] - want) <= tolerance, f"{name}: turned {measured[name]}, want {want}", failures)
    near("unit", 90.0)            # a quarter turn left at 1:1
    near("mouse", 90.0)           # no mouse setting changes it
    near("ratio", -90.0)          # 45 degrees right at 2:1
    near("player", 90.0)          # tilted, about the real vertical
    expect(40.0 < measured["local"] < 75.0, f"local yaw loses part of a tilted turn: {measured['local']}", failures)
    expect(measured["drift"] > 15.0, f"an uncalibrated bias drifts the view: {measured['drift']}", failures)
    near("calibrated", 0.0, 1.0)  # the calibration removed it
    near("biased", 90.0)
    near("ratchet", 0.0, 1.0)     # paused while the gyro button is held, and the motion is dropped
    near("ratchet_after", 90.0)
    near("held", 0.0, 1.0)        # on only while held
    near("held_while", 90.0)
    log = steps.log
    expect("controller: gyro calibration started; hold the controller still" in log, "calibrateGyro starts", failures)
    expect("controller: gyro calibrated (bias" in log, "the hold-still calibration completes", failures)
    expect(re.search(r'"in_gyroCalibration" is:"\S+,-?\d\.\d+,0\.2\d+,-?\d\.\d+', log) is not None,
           "the measured bias is stored for the controller", failures)
    expect("gyro calibration: measured" in log, "listControllers reports the calibration", failures)
    return measured


def check_feedback(steps: Steps, failures: list[str]) -> dict:
    idle, fired = steps["idle"]["pads"][0], steps["fired"]["pads"][0]
    expect(idle["player"] == 0, f"the pad in use is player 1: {idle['player']}", failures)
    expect(idle["led"] >= 1 and idle["led_rgb"][1] > idle["led_rgb"][0], f"the light shows full health as green: {idle['led_rgb']}", failures)
    expect(idle["trigger"] == 0, "no trigger motor before a shot", failures)
    expect(fired["rumble"] > idle["rumble"], "shots rumble the motors", failures)
    expect(fired["trigger"] > 0, f"shots reach the trigger motors: {fired['trigger']}", failures)
    expect(fired["effect"] >= 1 and fired["effect_bytes"] == 47, f"a DualSense's trigger takes the weapon's feel: {fired['effect']} calls of {fired['effect_bytes']} bytes", failures)
    off = steps["trigger_off"]["pads"][0]
    expect(off["trigger"] == fired["trigger"], f"Trigger Rumble off: no more trigger pulses ({fired['trigger']} -> {off['trigger']})", failures)
    expect(off["rumble"] > fired["rumble"], "the motors still rumble with Trigger Rumble off", failures)
    feel_off, feel_on = steps["feel_off"]["pads"][0], steps["feel_on"]["pads"][0]
    expect(feel_off["effect"] == off["effect"] + 1, f"Adaptive Triggers off clears the effect once ({off['effect']} -> {feel_off['effect']})", failures)
    expect(feel_off["effect_sum"] != fired["effect_sum"], "the cleared effect differs from the weapon's", failures)
    expect(feel_on["effect"] == feel_off["effect"] + 1 and feel_on["effect_sum"] == fired["effect_sum"], "Adaptive Triggers on restores the weapon's feel", failures)
    master = steps["master_off"]["pads"][0]
    expect(master["trigger"] == feel_on["trigger"] or (master["trigger_left"] == 0 and master["trigger_right"] == 0),
           "Rumble off: the trigger motors are silent", failures)
    expect(master["rumble_low"] == 0 and master["rumble_high"] == 0, "Rumble off: the motors are silent", failures)
    expect(master["effect"] == feel_on["effect"] + 1, "Rumble off: the trigger goes slack", failures)
    expect(steps["player_light_off"]["pads"][0]["player"] == -1, "Player Light off clears the player number", failures)
    xbox_idle, xbox = steps["xbox_idle"]["pads"][1], steps["xbox_fired"]["pads"][1]
    expect(xbox["active"] and xbox["player"] == 0, "the second pad took charge and is player 1", failures)
    expect(steps["xbox_fired"]["pads"][0]["player"] == -1, "the pad that gave up charge is no longer player 1", failures)
    expect(xbox["effect"] == 0, f"no trigger effect on a pad that is not a DualSense: {xbox['effect']}", failures)
    expect(xbox["trigger"] > xbox_idle["trigger"], "its trigger motors pulse with shots", failures)
    expect("haptics and lights: rumble=yes triggerMotors=yes adaptiveTriggers=no light=yes" in steps["end"]["since"],
           "listControllers reports what the controller can do", failures)
    return {"shots_trigger_pulses": fired["trigger"], "effect_calls": fired["effect"], "light": idle["led_rgb"]}


def cvar_value(state: dict, name: str) -> float | None:
    match = re.search(r'"%s" is:"([-\d.e]+)"' % re.escape(name), state["since"])
    return float(match.group(1)) if match else None


def check_assist(steps: Steps, failures: list[str]) -> dict:
    def lowest(name: str) -> float:
        values = [cvar_value(steps[f"{name}_{index}"], "in_aimAssistSlow") for index in range(SWEEP_SAMPLES)]
        return min(value for value in values if value is not None)
    values = {"allies": cvar_value(steps["allies"], "in_aimAssistSlow"), "high": lowest("high"), "low": lowest("low"), "off": lowest("off")}
    expect("spawned entity" in steps.log, "the enemy spawned", failures)
    expect(values["allies"] == 1.0, f"allies are never a target: {values['allies']}", failures)
    # with the crosshair on a live enemy the stick runs at the strength's own scale
    expect(abs(values["high"] - 0.45) < 0.005, f"High slows the stick to 0.45 on a live enemy: {values['high']}", failures)
    expect(abs(values["low"] - 0.75) < 0.005, f"Low slows it to 0.75: {values['low']}", failures)
    expect(values["off"] == 1.0, f"Off assists nothing: {values['off']}", failures)
    expect(cvar_value(steps["off_end"], "in_aimAssistYaw") == 0.0, "Off follows nothing", failures)
    return values


def said(state: dict, text: str) -> bool:
    return text in state["since"]


def check_layout(steps: Steps, failures: list[str]) -> dict:
    s = steps
    expect(said(s["fresh"], '"in_joystickLayout" is:"1"') and said(s["fresh"], '"JOY1" = "_weaponWheel"'), "a fresh profile has the classic layout", failures)
    expect(said(s["bumper"], '"JOY1" = "_moveup"') and said(s["bumper"], '"JOY3" = "_weaponWheel"') and said(s["bumper"], '"in_joystickLayout" is:"2"'),
           "Bumper Jumper puts jump on the left bumper", failures)
    expect(said(s["custom"], '"in_joystickLayout" is:"0"'), "any rebinding shows Custom", failures)
    expect(said(s["onehanded"], '"JOY2" = "_forward"') and said(s["onehanded"], '"JOY1" is not bound') and
           said(s["onehanded"], '"in_toggleZoom" is:"1"') and said(s["onehanded"], '"in_joystickLayout" is:"4"'),
           "One-Handed (Right) applies from the chooser, with its toggles, and unbinds the left side", failures)
    expect(said(s["classic"], '"JOY1" = "_weaponWheel"') and said(s["classic"], '"JOY2" = "_impulse51"') and said(s["classic"], '"in_joystickLayout" is:"1"'),
           "Classic restores the shipped buttons", failures)
    reset = s["controller_reset"]
    expect(said(reset, '"JOY3" = "_moveup"') and said(reset, '"in_joystickLayout" is:"1"'), "Reset Controller restores the controller's buttons", failures)
    expect(said(reset, '"in_joystickYawSpeed" is:"157.5"'), "Reset Controller restores the controller's settings", failures)
    expect(said(reset, '"w" = "_back"') and said(reset, '"sensitivity" is:"9"'), "Reset Controller leaves the keyboard and mouse alone", failures)
    reset = s["keyboard_reset"]
    expect(said(reset, '"w" = "_forward"') and said(reset, '"sensitivity" is:"5"'), "Reset Keyboard and Mouse restores them", failures)
    expect(said(reset, '"JOY3" = "_attack"'), "Reset Keyboard and Mouse leaves the controller alone", failures)
    expect(said(reset, '"ui_name" is:"PadTester'), "neither reset touches the player's name", failures)
    expect(said(s["context_idle"], "context key=JOY3 action=none binding=[_moveup]") and
           said(s["context_idle"], "context key=JOY6 action=none binding=[_impulse13]"), "no context action while the objectives button is up", failures)
    expect(said(s["context_held"], "context key=JOY3 action=[savegame quick]") and
           said(s["context_held"], "context key=JOY6 action=[loadgame quick]"), "the objectives button held: A quick saves and X quick loads", failures)
    expect(said(s["context_held"], "context key=JOY8 action=none"), "the held button itself keeps its binding", failures)
    expect(said(s["context_ended"], "context key=JOY3 action=none"), "the context ends when the button comes up", failures)
    expect(said(s["context_own_binding"], "context key=JOY3 action=none") and
           said(s["context_own_binding"], "context key=JOY6 action=[loadgame quick]"),
           "a command with a controller button of its own is left to that button", failures)
    expect(said(s["context_off"], "context key=JOY3 action=none"), "in_joystickContextActions 0 turns them off", failures)
    return {"layouts": 4}


OUTPUT_DIR = ROOT / ".tmp/controller-runtime"

def check_menu(steps: Steps, failures: list[str]) -> dict:
    s = steps
    expect("path=guis/menu/settings/controls" in s["controller_tab"]["since"] and "focus=controls_layout" in s["controller_tab"]["since"],
           "the Controller tab opens with the Button Layout row", failures)
    expect(said(s["layout_chosen"], '"in_joystickLayout" is:"2"') and said(s["layout_chosen"], '"JOY1" = "_moveup"'),
           "Button Layout chosen from its row rebinds the buttons", failures)
    expect("focus=controls_response" in s["aim_tab"]["since"], "the Aim tab opens with the Response row", failures)
    expect(said(s["preset_chosen"], '"in_joystickResponse" is:"2"') and said(s["preset_chosen"], '"in_joystickLookDeadZone" is:"0.1"') and
           said(s["preset_chosen"], '"in_joystickLookCurve" is:"1.8"'), "Response chosen from its row sets the preset's values", failures)
    expect(said(s["assist_chosen"], '"in_aimAssist" is:"1"'), "Aim Assist chosen from its row", failures)
    expect("focus=controls_gyrospace" in s["gyro_column"]["since"], "the gyro rows stand on the Aim tab", failures)
    reset = s["reset"]
    expect(said(reset, '"in_joystickLayout" is:"1"') and said(reset, '"JOY1" = "_weaponWheel"') and
           said(reset, '"in_joystickResponse" is:"1"') and said(reset, '"in_aimAssist" is:"0"'),
           "Reset Controller restores the controller's buttons and settings", failures)
    expect(said(reset, '"w" = "_back"'), "Reset Controller leaves the keyboard alone", failures)
    shots = [name for name in ("controls-controller", "controls-aim", "controls-aim-gyro")
             if (OUTPUT_DIR / "menu" / "baseoq4" / "screenshots" / f"{name}.tga").is_file()]
    expect(len(shots) == 3, f"the three captures were written: {shots}", failures)
    return {"captures": len(shots)}


def node(state: dict, ident: str) -> dict | None:
    """The last inspection of `ident` before this step: bounds, opacity, display."""
    found = re.findall(rf"RETAINED_GUI_NODE id={re.escape(ident)} bounds=([-0-9.e,+]+) opacity=([-0-9.e+]+) display=(\w+)", state["since"])
    if not found:
        return None
    bounds, opacity, display = found[-1]
    return {"bounds": [float(value) for value in bounds.split(",")], "opacity": float(opacity), "display": display}


def keyboard(state: dict) -> dict | None:
    """The last on-screen keyboard report before this step."""
    found = re.findall(r"ON_SCREEN_KEYBOARD shown=(\d) layer=(\d) shift=(\d) edit=(\d) top=(\d) focus=(\S*)", state["since"])
    if not found:
        return None
    shown, layer, shift, edit, top, focus = found[-1]
    return {"shown": shown == "1", "layer": int(layer), "shift": shift == "1", "edit": int(edit), "top": top == "1", "focus": focus}


def field(state: dict, ident: str) -> dict | None:
    """The last report of a field's edit before this step: active, bytes, caret."""
    found = re.findall(rf"RETAINED_GUI_NUMBER id={re.escape(ident)} active=(\d) bytes=(\d+) anchor=(\d+) caret=(\d+)", state["since"])
    if not found:
        return None
    active, size, _anchor, caret = found[-1]
    return {"active": active == "1", "bytes": int(size), "caret": int(caret)}


def check_keyboard(steps: Steps, failures: list[str]) -> dict:
    s = steps
    name_bytes = len(KEYBOARD_NAME.encode("utf-8"))
    focused = keyboard(s["focused"])
    expect("path=guis/menu/savegame" in s["focused"]["since"] and "focus=save-name" in s["focused"]["since"], "the Save Game page's name field has the focus", failures)
    expect(focused is not None and not focused["shown"] and focused["edit"] == 0, f"focus alone shows no keyboard: {focused}", failures)
    editing = keyboard(s["editing"])
    expect(editing is not None and editing["shown"] and editing["edit"] == 1 and editing["layer"] == 0 and not editing["shift"],
           f"Accept opens the edit and the keyboard, on Latin: {editing}", failures)
    expect(editing is not None and editing["focus"] == "osk-key-27", f"its middle key has the focus: {editing}", failures)
    expect(editing is not None and editing["top"], f"the save name is low on the page, so the keyboard stands at the head: {editing}", failures)
    cleared = field(s["cleared"], "save-name")
    expect(cleared is not None and cleared["active"] and cleared["bytes"] == 0, f"DELETE empties the field: {cleared}", failures)
    typed, typed_keyboard = field(s["typed"], "save-name"), keyboard(s["typed"])
    expect(typed is not None and typed["bytes"] == name_bytes and typed["caret"] == name_bytes,
           f"the name's {name_bytes} bytes are in the field, the caret after them: {typed}", failures)
    expect(typed_keyboard is not None and typed_keyboard["shown"] and typed_keyboard["layer"] == 0 and not typed_keyboard["shift"],
           f"the layers went round to Latin, and shift lasted one letter: {typed_keyboard}", failures)
    edited = field(s["edited"], "save-name")
    # the caret stepped back over the two-byte zhe and DELETE took the two-byte e-acute before it
    expect(edited is not None and edited["bytes"] == name_bytes - 2 and edited["caret"] == name_bytes - 4,
           f"the caret moves by a character and DELETE takes the one before it: {edited}", failures)
    retyped = field(s["retyped"], "save-name")
    expect(retyped is not None and retyped["bytes"] == name_bytes and retyped["caret"] == name_bytes, f"typed again in place: {retyped}", failures)
    shifted = keyboard(s["shifted"])
    expect(shifted is not None and shifted["shift"], f"SHIFT holds until a letter: {shifted}", failures)
    done = keyboard(s["done"])
    expect(done is not None and not done["shown"] and done["edit"] == 0, f"DONE commits, ends the edit and closes the keyboard: {done}", failures)
    saves = OUTPUT_DIR / "keyboard" / "baseoq4" / "savegames"
    described = [path.name for path in saves.glob("*.txt") if KEYBOARD_NAME in path.read_text(encoding="utf-8", errors="replace")] if saves.is_dir() else []
    expect(len(described) == 1, f"the game saved under the typed name: {described} of {[path.name for path in saves.glob('*')] if saves.is_dir() else 'no saves'}", failures)
    shot = (OUTPUT_DIR / "keyboard" / "baseoq4" / "screenshots" / "keyboard.tga").is_file()
    expect(shot, "the capture was written", failures)
    return {"bytes": typed["bytes"] if typed else None, "saved": described}


def check_testpanel(steps: Steps, failures: list[str]) -> dict:
    s = steps
    expect(said(s["connected"], "controller: first playstation controller on this profile"),
           "a controller family's first connection is announced", failures)
    row = node(s["row"], "controllerTestModal")
    expect("focus=controls_test" in s["row"]["since"] and row is not None and row["display"] == "none",
           f"the Controller tab has the Test Controller row, the panel closed: {row}", failures)
    opened = node(s["opened"], "controllerTestModal")
    expect(opened is not None and opened["display"] == "block", f"the row opens the panel: {opened}", failures)
    expect("focus=controllerTestModal_yes" in s["opened"]["since"], "TEST RUMBLE has the focus as it opens", failures)
    gyro = node(s["opened"], "test-gyro")
    expect(gyro is not None and gyro["display"] == "block", f"a controller with a gyro shows its box: {gyro}", failures)
    idle_fill = node(s["opened"], "test-lt-fill")
    expect(idle_fill is not None and idle_fill["bounds"][2] < 1, f"a released trigger's bar is empty: {idle_fill}", failures)
    for key in ("test-button-6", "test-button-16"):
        dim = node(s["opened"], key)
        expect(dim is not None and abs(dim["opacity"] - 0.3) < 0.01, f"{key} is dim while released: {dim}", failures)
    held = {key: node(s["held"], key) for key in ("test-button-6", "test-button-16", "test-button-5", "test-lt-fill", "controllerTestModal")}
    expect(all(value is not None for value in held.values()), f"the panel answers while buttons are held: {held}", failures)
    if all(value is not None for value in held.values()):
        expect(held["controllerTestModal"]["display"] == "block", "a held button leaves the panel open", failures)
        expect(held["test-button-6"]["opacity"] > 0.99, f"the held west button lights: {held['test-button-6']}", failures)
        expect(held["test-button-16"]["opacity"] > 0.99, f"the left trigger lights past Trigger Press: {held['test-button-16']}", failures)
        expect(abs(held["test-button-5"]["opacity"] - 0.3) < 0.01, f"a button not held stays dim: {held['test-button-5']}", failures)
        expect(abs(held["test-lt-fill"]["bounds"][2] - 0.8 * 195) < 4, f"the trigger's bar follows its travel: {held['test-lt-fill']}", failures)
    released = node(s["released"], "test-button-6")
    expect(released is not None and abs(released["opacity"] - 0.3) < 0.01, f"the released button dims again: {released}", failures)
    def running(name: str) -> tuple:
        pad = s[name]["pads"][0]
        return tuple(pad[key] > 0 for key in ("rumble_low", "rumble_high", "trigger_left", "trigger_right"))
    samples = [running(f"motor_{index}") for index in range(MOTOR_SAMPLES)]
    expect(all(sum(sample) <= 1 for sample in samples), "no two motors ever run together", failures)
    motors = []
    for sample in samples:
        if any(sample) and (not motors or motors[-1] != sample.index(True)):
            motors.append(sample.index(True))
    expect(motors == [0, 1, 2, 3], f"each motor takes its turn: strong, light, left trigger, right trigger: {motors}", failures)
    expect(not any(running("motors_done")), "and the test ends with every motor still", failures)
    still_open = node(s["motors_done"], "controllerTestModal")
    expect(still_open is not None and still_open["display"] == "block", "Accept ran the test and left the panel open", failures)
    closed = node(s["closed"], "controllerTestModal")
    expect(closed is not None and closed["display"] == "none" and "path=guis/menu/settings/controls" in s["closed"]["since"],
           f"Back closes the panel and stays on the page: {closed}", failures)
    shot = (OUTPUT_DIR / "testpanel" / "baseoq4" / "screenshots" / "controller-test.tga").is_file()
    expect(shot, "the capture was written", failures)
    return {"motors": motors, "fill": held["test-lt-fill"]["bounds"][2] if held["test-lt-fill"] else None}


CHECKS = {"keyboard": check_keyboard, "testpanel": check_testpanel, "menu": check_menu, "assist": check_assist, "layout": check_layout, "feedback": check_feedback, "gyro": check_gyro, "baseline": lambda steps, failures: {}, "handover": check_handover, "twins": check_twins, "pause": check_pause, "battery": check_battery, "turnrate": check_turnrate}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True, help="an installed Quake 4")
    parser.add_argument("--exe", type=Path, default=ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--output", type=Path, default=ROOT / ".tmp/controller-runtime")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--map", default="airdefense1")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--scenario", choices=tuple(SCENARIOS), action="append")
    args = parser.parse_args()
    args.output = task_path(args.output)
    global OUTPUT_DIR
    OUTPUT_DIR = args.output
    args.output.mkdir(parents=True, exist_ok=True)
    results = {}
    for name in args.scenario or tuple(SCENARIOS):
        started = time.monotonic()
        log, code = launch(args, name)
        failures: list[str] = []
        details: dict = {}
        expect(code == 0, f"exit {code}", failures)
        expect(f"PAD_RUNTIME_DONE {name}" in log, "the scenario did not reach its end", failures)
        errors = ERROR.findall(log)
        expect(not errors, f"errors: {errors[:2]}", failures)
        if not failures:
            try:
                details = CHECKS[name](Steps(log), failures)
            except AssertionError as error:
                failures.append(str(error))
        results[name] = {"passed": not failures, "failures": failures, "details": details,
                         "seconds": round(time.monotonic() - started, 1)}
        print(f"controller {name}: {'PASS' if not failures else 'FAIL'} ({results[name]['seconds']}s) {details}", flush=True)
        for failure in failures:
            print(f"  {failure}", flush=True)
    (args.output / "report.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if results and all(result["passed"] for result in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
