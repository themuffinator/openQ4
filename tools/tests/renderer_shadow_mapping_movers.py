"""Real door teams, portals, and moving caster/receiver regression fixtures.

Uses stock entities/models, game-console commands, and engine screenshots.
Fixed simulation tics make a paused pose comparable across cached/fresh depth.
Live captures and door diagnostics separately establish that motion occurred.
"""

import re
from pathlib import Path


MOVER_SCENARIOS = ("door-portal", "door-projected", "door-point", "door-budget")
PHASES = ("closed", "opening", "reversing", "reclosed", "open", "returned")


def mover_commands(scenario: str) -> tuple[list[str], tuple[str, ...]]:
    portal = scenario == "door-portal"
    door = "func_door_63" if portal else "shadow_door_left"
    pose = "-620 1776 288 0 0 0" if portal else "-1000 1568 84 0 -90 0"
    away = "-1000 1568 84 0 -90 0" if portal else "-620 1776 288 0 0 0"
    commands = ["g_renderFastNoPost 1", "g_renderFastNoPostDirect 1", "com_fixedTic 1",
                "r_shadowMapSize 512", "r_shadowMapPointSize 256", "r_shadowMapReportInterval 6",
                "gfxInfo", "noclip", "g_stopTime 0", "setviewpos " + pose,
                "testFlashlight " + ("0" if scenario == "door-point" else "1")]
    captures = []
    if portal:
        commands.append(f"testDoor {door} close")
    else:
        # These models face X in their local space. After yaw=90 the left
        # leaf opens along +X, the right along -X (not through one another).
        for side, direction in (("left", 0), ("right", 180)):
            commands.append(
                f"spawn func_door name shadow_door_{side} "
                f"model models/mapobjects/strogg/doors/generic/prison/door_{side}.lwo "
                'origin "-1000 1300 32" angle 90 '
                f"movedir {direction} team shadow_door wait -1 no_touch 1 time 1 lip 0")
        if scenario == "door-point":
            commands.append("r_singleLight 168")
    commands.extend(["wait 40", "g_stopTime 1"])

    def capture(label: str, *settings: str) -> None:
        commands.extend(settings)
        commands.extend(["wait 6", f"testDoor {door}", f"echo SHADOW_TRANSITION_{label}",
                         f"screenshot screenshots/{label}.tga", "wait 3"])
        captures.append(label)

    def compare(label: str) -> None:
        budget = 1 if scenario == "door-budget" and label != "closed" else 0
        capture(label + "_cached", "r_shadows 1", "r_useShadowMap 1",
                "r_shadowMapStaticCache 1", f"r_shadowMapMaxUpdatesPerView {budget}")
        capture(label + "_fresh", "r_shadowMapMaxUpdatesPerView 0", "r_shadowMapStaticCache 0")
        capture(label + "_fresh_repeat")
        capture(label + "_unshadowed", "r_shadows 0")
        commands.extend(["r_shadows 1", "r_shadowMapStaticCache 1",
                         f"r_shadowMapMaxUpdatesPerView {budget}", "wait 6"])

    def move(label: str, action: str, ticks: int, live: bool = False) -> None:
        commands.extend(["g_stopTime 0", f"testDoor {door} {action}", f"wait {ticks}"])
        if live:
            # Do not stop simulation until this render-target capture finishes.
            commands.extend([f"testDoor {door}", f"echo SHADOW_MOTION_{label}",
                             f"screenshot screenshots/{label}_live.tga", "wait 1"])
            captures.append(label + "_live")
        commands.append("g_stopTime 1")
        if label == "opening":
            commands.append("g_presentationClockCapture 24")
        compare(label)
        if label == "opening":
            commands.append("g_presentationClockCapture 0")

    compare("closed")
    move("opening", "open", 10 if portal else 24, live=True)
    move("reversing", "close", 4 if portal else 8, live=True)
    move("reclosed", "close", 45 if portal else 70)
    move("open", "open", 45 if portal else 70)
    # Keep the cache alive while the camera is elsewhere and the leaves close.
    # The return also exercises portal connectivity and receiver-set changes.
    commands.extend(["g_stopTime 0", "setviewpos " + away, "wait 6",
                     f"testDoor {door} close", "wait 70", f"testDoor {door}"])
    if scenario == "door-point":
        # No selected light reaches this view. Exercise overlay/report reads
        # after the old frame arena has been recycled, not only with the HUD off.
        commands.extend(["r_shadowMapDebugOverlay 1", "wait 6",
                         "screenshot screenshots/away_overlay.tga", "wait 3",
                         "r_shadowMapDebugOverlay 0"])
        captures.append("away_overlay")
    commands.extend(["setviewpos " + pose, "wait 6", "g_stopTime 1"])
    compare("returned")
    commands.extend(["r_shadows 1", "r_shadowMapStaticCache 1", "r_shadowMapMaxUpdatesPerView 0"])
    return commands, tuple(captures)


def validate_mover_reports(scenario: str, text: str) -> list[str]:
    failures = []
    portal = scenario == "door-portal"
    names = ("func_door_63", "func_door_64") if portal else ("shadow_door_left", "shadow_door_right")
    latest = {}
    closed = {}
    seen = set()
    moving = set()
    mapped_pass = False
    cache_hit = False
    cached_phases = set()
    frame_lights = set()
    max_frame_lights = 0
    vulkan = False
    state_for_phase = {"closed": 0, "opening": 2, "reversing": 3,
                       "reclosed": 0, "open": 1, "returned": 0}
    for line in text.splitlines():
        if line.startswith("Vulkan shadow cache:"):
            vulkan = True
            max_frame_lights = max(max_frame_lights, len(frame_lights))
            frame_lights.clear()
        light = re.match(r"SM pass light\[(\d+)\]", line)
        if light:
            frame_lights.add(int(light[1]))
        target = ("[168] 'lights/round'" if scenario == "door-point" else "'gfx/lights/flashlight'")
        if target in line:
            if line.startswith("SM pass global["):
                mapped_pass = "result=mapped" in line or "result=cache-reuse" in line
                cache_hit = "result=cache-reuse" in line
            elif line.startswith("SM pass light["):
                mapped_pass = any("GLOBAL=" + mode in line for mode in ("reuse", "publish", "scratch", "alias"))
                cache_hit = "GLOBAL=reuse" in line
        status = re.fullmatch(r"testDoor: name=(\S+) time=(\d+) state=(\d+) origin=\(([^)]+)\) "
                              r"velocity=\(([^)]+)\) portal=(\d+) blocking=(-?\d+)\s*", line)
        if status:
            name, time, state, origin, velocity, handle, blocking = status.groups()
            latest[name] = (int(state), tuple(map(float, origin.split())),
                            tuple(map(float, velocity.split())), int(handle), int(blocking))
        motion = re.fullmatch(r"SHADOW_MOTION_(opening|reversing)\s*", line)
        phase = re.fullmatch(r"SHADOW_TRANSITION_(closed|opening|reversing|reclosed|open|returned)"
                            r"_(cached|fresh|fresh_repeat|unshadowed)\s*", line)
        if not phase and not motion:
            continue
        label = phase[1] if phase else motion[1]
        if phase:
            seen.add(phase.groups())
            if phase[2] != "unshadowed" and not mapped_pass:
                failures.append(f"{phase[1]}_{phase[2]}: target light did not report a mapped GLOBAL pass")
            if phase[2] == "cached" and cache_hit:
                cached_phases.add(label)
        else:
            moving.add(label)
        for name in names:
            if name not in latest:
                failures.append(f"{label}: no diagnostic for door leaf {name}")
                continue
            state, origin, velocity, handle, blocking = latest[name]
            if state != state_for_phase[label]:
                failures.append(f"{label}: {name} state {state}, expected {state_for_phase[label]}")
            if motion and not any(abs(value) > 0.1 for value in velocity):
                failures.append(f"{label}: {name} did not move during the live capture")
            if label == "closed":
                closed[name] = origin
            elif name in closed:
                displaced = any(abs(a - b) > 0.01 for a, b in zip(origin, closed[name]))
                if displaced != (label in ("opening", "reversing", "open")):
                    failures.append(f"{label}: unexpected {name} displacement from closed pose")
            if portal and (handle <= 0 or blocking != (7 if state_for_phase[label] == 0 else 0)):
                failures.append(f"{label}: {name} portal {handle} blocking {blocking}")
        # Each capture must have its own intervening light and team reports;
        # an old mapped result must not qualify a later empty/fallback view.
        mapped_pass = False
        cache_hit = False
        latest.clear()
    if len(seen) != len(PHASES) * 4 or len(moving) != 2:
        failures.append(f"Missing mover phases: {len(seen)}/24 comparisons, {len(moving)}/2 live captures")
    if scenario != "door-point" and "closed" not in cached_phases:
        failures.append("The stationary flashlight pass did not exercise resident cache reuse")
    if portal and vulkan and max(max_frame_lights, len(frame_lights)) <= 64:
        failures.append("The full portal scene did not exercise more than 64 prepared shadow lights")
    clock = re.findall(r"presentationClockSample rt=\d+ t=(\d+) f=([\d.]+)", text)
    if len(clock) != 24 or len({time for time, _ in clock}) != 1 or any(float(fraction) != 1.0 for _, fraction in clock):
        failures.append("Paused door presentation did not hold one authoritative pose for 24 draw samples")
    return list(dict.fromkeys(failures))


def validate_mover_images(save: Path) -> tuple[list[str], dict]:
    from PIL import Image, ImageChops, ImageStat

    failures = []
    metrics = {}
    for phase in PHASES:
        paths = {kind: save / f"baseoq4/screenshots/{phase}_{kind}.tga"
                 for kind in ("cached", "fresh", "fresh_repeat", "unshadowed")}
        if not all(path.is_file() for path in paths.values()):
            continue  # The common runner reports missing captures.
        frames = {kind: Image.open(path).convert("RGB") for kind, path in paths.items()}
        def difference(kind: str) -> float:
            return sum(ImageStat.Stat(ImageChops.difference(frames[kind], frames["fresh"])).mean) / 3
        cached, repeat, unshadowed = (difference(kind) for kind in ("cached", "fresh_repeat", "unshadowed"))
        metrics[phase] = {"cached_fresh_mae": cached, "fresh_repeat_mae": repeat,
                          "unshadowed_fresh_mae": unshadowed}
        # Fresh/fresh is the independent control for effects that still animate
        # while game simulation is paused. Values are in 8-bit colour units.
        if cached > repeat + 0.25:
            failures.append(f"{phase}: cached/fresh error {cached:.4f} exceeds fresh control {repeat:.4f} + 0.25")
        if unshadowed <= 0.01:
            failures.append(f"{phase}: shadow-disabled control did not establish visible shadowing")
    return failures, metrics
