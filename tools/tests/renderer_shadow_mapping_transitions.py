"""Engine-console transitions for the stock shadow-map runner.

No operating-system input is synthesized. These fixtures use temporary game
entities, isolated settings, and the registered engine screenshot command.
"""

import re

from renderer_shadow_mapping_movers import MOVER_SCENARIOS, mover_commands, validate_mover_reports
from renderer_shadow_mapping_outdoor import OUTDOOR_SCENARIO, outdoor_commands, validate_outdoor_reports

SCENARIO_MAPS = {
    "flashlight-cycle": "airdefense2",
    "caster-cycle": "airdefense2",
    "caster-cycle-point": "airdefense2",
    "caster-cycle-cutout": "airdefense2",
    # REN-03: the cutout cycle while the camera, and the flashlight it carries,
    # pans a full turn through every spawn and removal.
    "caster-cycle-cutout-sweep": "airdefense2",
    # REN-03: translucent casters (stock forcefield glass, translucent coverage
    # without an authored opt-out) with moments on, still and while turning.
    "caster-cycle-translucent": "airdefense2",
    "caster-cycle-translucent-sweep": "airdefense2",
    "resource-cycle": "airdefense2",
    "cascade-motion": "q4dm9",
    "emitter-lift": "storage2",
}
SCENARIO_MAPS.update({scenario: "airdefense2" for scenario in MOVER_SCENARIOS})
SCENARIO_MAPS[OUTDOOR_SCENARIO] = "airdefense1"
CUTOUT_CYCLES = ("caster-cycle-cutout", "caster-cycle-cutout-sweep")
TRANSLUCENT_CYCLES = ("caster-cycle-translucent", "caster-cycle-translucent-sweep")
CASTER_CYCLES = ("caster-cycle", "caster-cycle-point") + CUTOUT_CYCLES + TRANSLUCENT_CYCLES
TRANSLUCENT_MATERIAL = "textures/glass/forcefield"
# A whole turn in game time ends at the starting yaw, so the paused comparisons
# keep the cycle's original view. Game time can run slower than the wall clock
# in a hidden window, so the wait is generous; the light then rests 60 frames
# before the freeze, as the stationary cycle's casters do.
TURN = ["benchmarkViewSweep 360 1000", "waitMsec 4000"]
SWEEP = ["g_stopTime 0"] + TURN + ["wait 60", "g_stopTime 1"]


def transition_commands(scenario: str) -> tuple[list[str], tuple[str, ...]]:
    if scenario == OUTDOOR_SCENARIO:
        return outdoor_commands()
    if scenario in MOVER_SCENARIOS:
        return mover_commands(scenario)
    # These GameLib fast-post controls exist only in the SP module.
    commands = [] if scenario == "cascade-motion" else ["g_renderFastNoPost 1", "g_renderFastNoPostDirect 1"]
    captures = []

    def capture(label: str, *settings: str) -> None:
        commands.extend(settings)
        # A light that has just been placed counts as moving for seventy
        # milliseconds and is drawn directly meanwhile, not cached. Thirty
        # frames are less than that on a fast enough machine, so wait in wall
        # time first.
        commands.extend(["waitMsec 150", "wait 30", f"echo SHADOW_TRANSITION_{label}",
                         f"screenshot screenshots/{label}.tga", "wait 3"])
        captures.append(label)

    def cached_and_fresh(label: str) -> None:
        capture(label + "_cached", "r_shadowMapStaticCache 1")
        capture(label + "_fresh", "r_shadowMapStaticCache 0")
        if scenario in CASTER_CYCLES:
            capture(label + "_fresh_repeat")

    if scenario == "flashlight-cycle":
        capture("light_off", "testFlashlight 0")
        capture("light_on", "testFlashlight 1")
        capture("light_budget", "r_shadowMapStaticCache 0", "r_shadowMapMaxUpdatesPerView 1")
        # Engine teleports exercise the actual weapon light's moving projection.
        commands.extend(["noclip", "g_stopTime 0", "setviewpos -1040 1540 100 0 -70 0", "wait 5", "g_stopTime 1"])
        capture("light_turned")
        commands.extend(["g_stopTime 0", "setviewpos -1000 1568 84 0 -90 0", "wait 5", "g_stopTime 1"])
        capture("light_returned", "r_shadowMapStaticCache 1")
        capture("light_off_again", "testFlashlight 0")
        capture("light_on_again", "testFlashlight 1")
    elif scenario in CASTER_CYCLES:
        sweep = scenario.endswith("-sweep")
        commands.extend(["r_shadowMapPointSize 256", "r_shadowMapSize 512"])
        # Settle the opening scene before freezing the empty reference. A
        # fixed number of rendered frames can leave different transient map
        # geometry in the flashlight on faster backends.
        commands.extend(["g_stopTime 0", "waitMsec 3000", "g_stopTime 1"])
        if scenario == "caster-cycle-point":
            # Isolate a steady retail light: the full scene includes animated
            # and sound-reactive lighting. Repeat fresh captures as a control.
            commands.extend(["r_singleLight 168", "testFlashlight 0"])
        else:
            commands.append("testFlashlight 1")
        # A static entity with perforated coverage must keep casting through
        # its holes after its transform has settled. This shipped material's
        # coverage cannot change, so it is cached with the static depth
        # (R_ShadowMapCutoutCoverageAnimates); the cached and fresh captures
        # below are what hold the baked cutout to the live one.
        material = (" shader textures/terminal/square_hole" if scenario in CUTOUT_CYCLES
                    else f" shader {TRANSLUCENT_MATERIAL}" if scenario in TRANSLUCENT_CYCLES else "")
        if scenario in TRANSLUCENT_CYCLES:
            commands.append("r_shadowMapTranslucentMoments 1")
        cached_and_fresh("empty")
        commands.append("g_stopTime 0")
        # Deliberately overlapping stock models in a lit retail corridor;
        # this supplies receiving floors absent from the old tools/mv2 fixture.
        for row in range(2):
            for column in range(3):
                for level in range(2):
                    name = f"shadow_round2_{row}_{column}_{level}"
                    commands.append(
                        f"spawn func_static name {name} "
                        "model models/mapobjects/strogg/crates/crate1_small.lwo "
                        f'origin "{-1060 + 60 * column} {1300 - 70 * row} {35 + 48 * level}"'
                        + material
                    )
        if sweep:
            # Spawn while turning: casters enter a light that is moving.
            commands.extend(TURN)
        commands.extend(["wait 60", "g_stopTime 1"])
        cached_and_fresh("twelve")
        for row in range(2):
            for column in range(3):
                commands.append(f"remove shadow_round2_{row}_{column}_1")
        if sweep:
            commands.extend(SWEEP)
        cached_and_fresh("six")
        for row in range(2):
            for column in range(3):
                commands.append(f"remove shadow_round2_{row}_{column}_0")
        if sweep:
            commands.extend(SWEEP)
        cached_and_fresh("removed")
    elif scenario == "emitter-lift":
        # Catch the moving lift/emitter before the normal runner's 85-second
        # arrival wait. Each comparison freezes that phase of the real map.
        commands.extend(["r_shadowMapStaticCache 0"])
        for index in range(4):
            if index:
                commands.extend(["r_useShadowMap 1", "r_shadows 1",
                                 "g_stopTime 0", "waitMsec 30000", "g_stopTime 1"])
            capture(f"lift_{index}_mapped", "r_useShadowMap 1", "r_shadows 1")
            capture(f"lift_{index}_stencil", "r_useShadowMap 0")
            capture(f"lift_{index}_unshadowed", "r_shadows 0")
        commands.extend(["r_useShadowMap 1", "r_shadows 1"])
    elif scenario == "resource-cycle":
        commands.append("testFlashlight 1")
        capture("size_small", "r_shadowMapPointSize 128", "r_shadowMapSize 256")
        capture("size_large", "r_shadowMapPointSize 1024", "r_shadowMapSize 1024")
        capture("manual_compare", "r_shadowMapPointDepthCompare 0", "r_shadowMapDepthCompare 0")
        capture("hardware_compare", "r_shadowMapPointDepthCompare 1", "r_shadowMapDepthCompare 1")
        capture("moments_on", "r_shadowMapTranslucentMoments 1")
        capture("moments_off", "r_shadowMapTranslucentMoments 0")
        capture("size_restored", "r_shadowMapPointSize 512", "r_shadowMapSize 1024")
        capture("cache_disabled", "r_shadowMapStaticCache 0")
        capture("cache_restored", "r_shadowMapStaticCache 1")
    elif scenario == "cascade-motion":
        commands.extend(["r_singleLight 179", "r_shadowMapCacheCSM 1", "r_shadowMapCascadeCount 4",
                         "r_shadowMapCascadeBlend 0.15", "r_shadowMapCascadeDistance 1536"])
        # Translate, turn, revisit, then change the fitted cascade contract.
        # Stay inside light 179's courtyard so its shadow reaches visible floor.
        # The normal MP spawn view is nearly unlit when this light is isolated.
        poses = ((1000, -1024, 450, 180), (1000.25, -1024, 450, 180),
                 (976, -1024, 450, 180), (912, -1024, 450, 170),
                 (1000, -1024, 450, 180))
        for index, pose in enumerate(poses):
            commands.append("setviewpos " + " ".join(map(str, pose)))
            commands.append("getviewpos")
            cached_and_fresh(f"pose_{index}")
            capture(f"pose_{index}_unshadowed", "r_shadows 0")
            commands.append("r_shadows 1")
        commands.extend(["r_shadowMapCascadeBlend 0.4", "r_shadowMapCascadeDistance 768"])
        cached_and_fresh("refit")
        commands.extend(["r_shadowMapCascadeBlend 0.15", "r_shadowMapCascadeDistance 1536"])
        cached_and_fresh("refit_restored")
        commands.extend(["r_singleLight -1", "setviewpos 1528 -384 452.25 180", "wait 5"])
    else:
        raise ValueError(f"Unknown transition scenario: {scenario}")
    commands.extend(["r_shadows 1", "r_useShadowMap 1", "r_shadowMapStaticCache 1"])
    return commands, tuple(captures)


def validate_transition_reports(scenario: str | None, text: str) -> list[str]:
    """Require real fixture admission, not just a successful process/capture."""
    if scenario == OUTDOOR_SCENARIO:
        return validate_outdoor_reports(text)
    if scenario in MOVER_SCENARIOS:
        return validate_mover_reports(scenario, text)
    if scenario not in ("caster-cycle",) + CUTOUT_CYCLES + TRANSLUCENT_CYCLES:
        return []
    failures = []
    latest = None
    translucent = None
    cache_reused = False
    seen = set()
    additions = {"empty": 0, "twelve": 24, "six": 12, "removed": 0}
    for line in text.splitlines():
        if "'gfx/lights/flashlight'" in line:
            if line.startswith("SM light["):
                match = re.search(r"casters\(total=(\d+) alpha=(\d+)", line)
                if match:
                    latest = tuple(map(int, match.groups()))
                moments = re.search(r"\btrans=(\d+)", line)
                translucent = int(moments[1]) if moments else None
            elif line.startswith("SM pass light["):
                match = re.search(r"casters\(static=(\d+) dynamic=(\d+) alpha=(\d+)", line)
                if match:
                    static, dynamic, alpha = map(int, match.groups())
                    latest = (static + dynamic, alpha)
                cache_reused = "GLOBAL=reuse" in line
            elif line.startswith("SM pass global["):
                cache_reused = "result=cache-reuse" in line
        phase = re.fullmatch(r"SHADOW_TRANSITION_(empty|twelve|six|removed)_(cached|fresh|fresh_repeat)\s*", line)
        if phase:
            seen.add(phase.groups())
            extra = additions[phase[1]]
            expected = (30 + extra, extra if scenario in CUTOUT_CYCLES else 0)
            # The opening flashlight can still include one transient scene
            # caster. Require the exact added/removed sets after spawning.
            opening_extra = phase[1] == "empty" and latest == (31, 0)
            if latest != expected and not opening_extra:
                failures.append(f"{phase[1]}_{phase[2]}: flashlight total/alpha casters {latest}, expected {expected}")
            if (scenario in CUTOUT_CYCLES and phase[1] in ("twelve", "six")
                    and phase[2] == "cached" and not cache_reused):
                failures.append(f"{phase[1]}_cached: opaque flashlight depth was not reused")
            # OpenGL reports translucent casters on its light line; they must be
            # exactly the spawned crates.
            if scenario in TRANSLUCENT_CYCLES and translucent is not None and translucent != extra:
                failures.append(f"{phase[1]}_{phase[2]}: {translucent} translucent casters, expected {extra}")
    if len(seen) != 12:
        failures.append(f"Only {len(seen)}/12 caster transition phases were reported")
    if scenario in TRANSLUCENT_CYCLES and "Vulkan shadow cache:" in text and \
            "first translucent shadow moment pass" not in text:
        failures.append("Vulkan never drew a translucent shadow moment pass for the crates")
    if scenario.endswith("-sweep"):
        turns = re.findall(r"benchmarkViewSweep: complete, 360\.0 degrees", text)
        if len(turns) != 3:
            failures.append(f"{len(turns)}/3 camera turns completed during the cutout cycle")
    return failures


def validate_caster_images(save) -> tuple[list[str], dict]:
    """Cached shadow depth must look like a fresh render at every caster phase.

    The fresh/fresh-repeat pair is the control for anything that still
    animates while game time is paused. Values are 8-bit colour units.
    """
    from PIL import Image, ImageChops, ImageStat

    failures, metrics = [], {}
    for phase in ("empty", "twelve", "six", "removed"):
        paths = {kind: save / f"baseoq4/screenshots/{phase}_{kind}.tga" for kind in ("cached", "fresh", "fresh_repeat")}
        if not all(path.is_file() for path in paths.values()):
            continue  # The common runner reports missing captures.
        frames = {kind: Image.open(path).convert("RGB") for kind, path in paths.items()}
        def difference(kind: str) -> float:
            return sum(ImageStat.Stat(ImageChops.difference(frames[kind], frames["fresh"])).mean) / 3
        cached, repeat = difference("cached"), difference("fresh_repeat")
        metrics[phase] = {"cached_fresh_mae": cached, "fresh_repeat_mae": repeat}
        if cached > repeat + 0.25:
            failures.append(f"{phase}: cached/fresh error {cached:.4f} exceeds fresh control {repeat:.4f} + 0.25")
    return failures, metrics
