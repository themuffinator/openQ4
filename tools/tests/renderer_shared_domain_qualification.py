#!/usr/bin/env python3
"""REN-01 runtime qualification of the nine shared classic rendering domains.

Opt-in, local and hardware-bound: it launches the staged client against the
installed Quake 4 assets. For every scene it starts one client per backend and
render configuration, freezes game time at a fixed stock pose and, for each
domain the scene exercises, takes engine screenshots and `gfxInfo` ownership
records of:

- the frame with every shared domain off (the classic reference);
- the frame with only that domain on;
- a visible-content control: the classic frame with that domain's content
  suppressed (for example r_skipInteractions), proving the domain's work shows;
- a rollback pair: a deliberately unsupported condition with the domain off
  and on, which must produce a named fallback, no ownership and the classic
  image;
- every domain on together, and the reference again (frozen-scene noise).

A domain passes on a backend when the active backend reports nonzero owned
views with no coverage mismatch, the domain-on frame is byte-identical to the
reference, the control differs visibly, the rollback is named and
byte-identical to its classic twin, the inactive backend records nothing, and
the log carries no engine errors or Vulkan validation messages. No input is
synthesized. A JSON report, a Markdown summary and the screenshots go to
--output-dir.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME = ROOT / ".install"
DEFAULT_OUTPUT = ROOT / ".tmp" / "shared-domain-qualification"
WIDTH = 1280
HEIGHT = 720

DOMAIN_CVARS = {
    "gui": "r_rendererSharedGui",
    "inworldgui": "r_rendererSharedInWorldGui",
    "ambient": "r_rendererSharedWorldAmbient",
    "interaction": "r_rendererSharedWorldInteraction",
    "fogblend": "r_rendererSharedWorldFogBlend",
    "deform": "r_rendererSharedDeform",
    "subview": "r_rendererSharedSubview",
    "cinematicpost": "r_rendererSharedCinematicPost",
    "specialframe": "r_rendererSharedSpecialFrame",
}

# Render configurations. "plain" is the classic presentation the domains were
# first built against: the scene draws straight into the window, without the
# scene target, temporal resolve or post chain. "default" leaves every
# setting at the machine's own preset, which is what players run.
CONFIGS = {
    "plain": {
        "g_renderFastNoPost": "1",
        "g_renderFastNoPostDirect": "1",
        "g_renderCasUpscale": "0",
        "r_postAA": "0",
        "r_temporalAA": "0",
        "r_dynamicResolution": "0",
        "r_screenFraction": "100",
        "r_bloom": "0",
        "r_motionBlur": "0",
        "r_ssao": "0",
        "r_hdrToneMap": "0",
        "r_multiSamples": "0",
    },
    "default": {},
    # The Quality and higher presets multisample the game's forward scene
    # target; the default (Enhanced on the reference machine) does not.
    "msaa": {"r_multiSamples": "4"},
}

FAILURE_PATTERNS = (
    re.compile(r"^\s*(?:\^\d)?ERROR\b"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Vulkan validation:|VUID-"),
    re.compile(r"committed view lost its prepared transaction"),
    # A warning against the validation materials means a fixture is not what
    # its scene claims to test.
    re.compile(r"WARNING:.*\bren01/"),
)


@dataclass
class Check:
    """One domain exercised in a scene."""

    domain: str
    # Extra cvars set together with the domain (deform needs a consumer).
    with_cvars: dict = field(default_factory=dict)
    # Classic-frame control that removes this domain's content.
    control: dict = field(default_factory=dict)
    # Console commands that apply the control and, afterwards, undo it.
    control_cmds: tuple = ((), ())
    # Unsupported condition for the zero-commit rollback pair (may be empty).
    rollback: dict = field(default_factory=dict)
    # Minimum changed channel bytes between reference and control.
    min_delta: int = 2000
    # Ownership scope for the GUI domain line ("root" or "world").
    scope: str = ""
    # The scene's content must make the domain refuse its view: the check then
    # requires no ownership, a named fallback and the classic image.
    expect_decline: bool = False


@dataclass
class Scene:
    id: str
    map: str
    pose: str
    purpose: str
    checks: list
    launch: dict = field(default_factory=dict)
    # Commands before the pose is set, and after the scene is frozen.
    commands: list = field(default_factory=list)
    frozen_commands: list = field(default_factory=list)
    mode: str = "SP"
    settle_frames: int = 90
    # Validation-only files written below the isolated save path (never shipped).
    files: dict = field(default_factory=dict)
    # Load the compiled validation fixture (FIXTURE_MAP) into the save path.
    fixture: bool = False
    # Content on the wall clock (GUI videos): parity is recorded, not required.
    wall_clock: bool = False
    # "config-backend" runs that cannot be compared, mapped to the reason.
    skip: dict = field(default_factory=dict)


FIXTURE_FILES: dict = {}


# A frozen scene re-captures identical images, and a skipped direct mirror
# leaves last frame's pixels in place on Vulkan, so skipping subviews shows no
# change. Every subview surface in the validation rooms is a func_static
# entity: hiding non-world entities removes the surfaces and their subviews.
SUBVIEW_CONTROL = {"control": {"r_skipEntities": "1"}}

AIRDEFENSE1_POSE = "10200 -6800 40 0 170 0"

# ---------------------------------------------------------------------------
# Validation fixture for the subview forms stock Quake 4 content never uses.
# Stock maps provide a direct mirror (medlabs) and remote-camera screens
# (tram1); no stock material authors x-ray, refraction, reflection or dynamic
# mirror render maps, or a post stage reading _currentDepth. This sealed room
# uses only stock textures, models and GUIs plus the test materials below. It
# is written into the run's isolated save path, compiled by the staged
# engine's dmap and never committed or shipped.
FIXTURE_MAP = "maps/ren01/subviews"
FIXTURE_MATERIALS = """// REN-01 validation materials; written to an isolated save path, never shipped.
ren01/xray
{
	translucent
	noshadows
	discrete
	{
		blend	blend
		xrayRenderMap	256 256
	}
}
ren01/refraction
{
	translucent
	noshadows
	discrete
	{
		refractionRenderMap	256 256
	}
	{
		map	_refractionRender
		screen
		rgb	0.5
	}
}
ren01/reflection
{
	noshadows
	discrete
	{
		reflectionRenderMap	256 256
	}
	{
		map	_reflectionRender
		screen
	}
}
ren01/mirror
{
	noshadows
	discrete
	{
		mirrorRenderMap	256 256
	}
}
ren01/currentDepth
{
	translucent
	noshadows
	discrete
	sort	postProcess
	{
		blend	add
		map	_currentDepth
		rgb	0.25
	}
}
ren01/videoPost
{
	translucent
	noshadows
	discrete
	sort	postProcess
	{
		blend	add
		videoMap	loop video/idlogo.roq
		rgb	1
	}
	{
		blend	blend
		map	_currentRender
		alpha	0.35
	}
}
"""


def _brush(x1: float, y1: float, z1: float, x2: float, y2: float, z2: float,
           faces: dict | None = None, default: str = "textures/common/caulk") -> str:
    faces = faces or {}
    tex = "( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) )"
    planes = (("+x", f"1 0 0 {-x2:g}"), ("-x", f"-1 0 0 {x1:g}"), ("+y", f"0 1 0 {-y2:g}"),
              ("-y", f"0 -1 0 {y1:g}"), ("+z", f"0 0 1 {-z2:g}"), ("-z", f"0 0 -1 {z1:g}"))
    lines = [f'  ( {plane} ) {tex} "{faces.get(side, default)}"' for side, plane in planes]
    return " brushDef3\n {\n" + "\n".join(lines) + "\n }\n"


def _room(x1: float, y1: float, x2: float, y2: float, top: float, wall: str, thick: float = 16) -> list:
    return [
        _brush(x1 - thick, y1 - thick, -thick, x2 + thick, y2 + thick, 0, {"+z": wall}),
        _brush(x1 - thick, y1 - thick, top, x2 + thick, y2 + thick, top + thick, {"-z": wall}),
        _brush(x1 - thick, y1 - thick, 0, x1, y2 + thick, top, {"+x": wall}),
        _brush(x2, y1 - thick, 0, x2 + thick, y2 + thick, top, {"-x": wall}),
        _brush(x1, y1 - thick, 0, x2, y1, top, {"+y": wall}),
        _brush(x1, y2, 0, x2, y2 + thick, top, {"-y": wall}),
    ]


def fixture_map() -> str:
    """Two sealed rooms. Room A puts every subview surface on its north wall,
    so none can see another: mirror-type cameras look south, refraction and
    x-ray look into the wall, and the remote camera faces south. Room B, with
    no line of sight to A, has a direct mirror facing a capture-backed mirror:
    a genuine nested direct/capture chain."""
    wall = "textures/common_trims/trim2_1"
    world = (_room(-384, -384, 384, 384, 256, wall) + _room(-256, 640, 256, 1088, 256, wall)
             + _room(640, -384, 1024, 0, 256, wall) + _room(640, 256, 1024, 640, 256, wall)
             + _room(-256, -1088, 256, -640, 256, wall))
    out = ["Version 3", "// entity 0", "{", '"classname" "worldspawn"']
    for index, brush in enumerate(world):
        out += [f"// primitive {index}", "{", brush.rstrip(), "}"]
    out.append("}")
    entities = [
        {"classname": "info_player_start", "name": "info_player_start_1", "origin": "0 -200 0", "angle": "90"},
        {"classname": "light", "name": "light_a", "origin": "0 0 200", "light_radius": "640 640 320",
         "_color": "1 0.95 0.9"},
        {"classname": "light", "name": "light_b", "origin": "0 864 200", "light_radius": "400 400 320",
         "_color": "0.9 0.95 1"},
        {"classname": "func_static", "name": "ren01_crate", "origin": "0 -220 0",
         "model": "models/mapobjects/strogg/crates/crate1_small.lwo"},
        {"classname": "func_static", "name": "ren01_crate_b", "origin": "0 864 0",
         "model": "models/mapobjects/strogg/crates/crate1_small.lwo"},
        {"classname": "func_cameraview", "name": "ren01cam", "origin": "0 0 128", "angle": "270"},
        {"classname": "light", "name": "light_c", "origin": "832 -192 200", "light_radius": "320 320 320"},
        {"classname": "light", "name": "light_d", "origin": "832 448 200", "light_radius": "320 320 320"},
        {"classname": "func_static", "name": "ren01_crate_c", "origin": "760 -260 0",
         "model": "models/mapobjects/strogg/crates/crate1_small.lwo"},
        {"classname": "func_static", "name": "ren01_crate_d", "origin": "760 380 0",
         "model": "models/mapobjects/strogg/crates/crate1_small.lwo"},
    ]
    panels = (("ren01_reflection", "ren01/reflection"), ("ren01_dynamic_mirror", "ren01/mirror"),
              ("ren01_current_depth", "ren01/currentDepth"), ("ren01_remote_screen", "textures/cameraview1"),
              ("ren01_direct_mirror", "textures/common/mirror"), ("ren01_video_post", "ren01/videoPost"))
    brushes = {}
    for index, (name, material) in enumerate(panels):
        x0 = -350 + index * 118
        entity = {"classname": "func_static", "name": name, "model": name}
        if name == "ren01_remote_screen":
            entity.update({"gui": "guis/common/strogg/camera1.gui", "cameraTarget": "ren01cam"})
        entities.append(entity)
        brushes[name] = _brush(x0, 384 - 16, 64, x0 + 80, 384 - 8, 192, {"-y": material})
    # X-ray and refraction draw from the viewer's own camera, so each gets a
    # room of its own where that camera sees no other subview surface.
    for name, material, y in (("ren01_xray", "ren01/xray", 0), ("ren01_refraction", "ren01/refraction", 640)):
        entities.append({"classname": "func_static", "name": name, "model": name})
        brushes[name] = _brush(768, y - 16, 64, 896, y - 8, 192, {"-y": material})
    entities.append({"classname": "func_static", "name": "ren01_nest_direct", "model": "ren01_nest_direct"})
    brushes["ren01_nest_direct"] = _brush(-128, 1088 - 16, 32, 128, 1088 - 8, 224,
                                          {"-y": "textures/common/mirror"})
    entities.append({"classname": "func_static", "name": "ren01_nest_capture", "model": "ren01_nest_capture"})
    brushes["ren01_nest_capture"] = _brush(-128, 640 + 8, 32, 128, 640 + 16, 224, {"+y": "ren01/mirror"})
    # Room E: a direct mirror that sees an x-ray panel. The x-ray child copies
    # its parent mirror view, clip plane included (authentic classic
    # behaviour), which the subview domain must refuse for the whole tree.
    entities += [
        {"classname": "light", "name": "light_e", "origin": "0 -864 200", "light_radius": "400 400 320"},
        {"classname": "func_static", "name": "ren01_crate_e", "origin": "0 -864 0",
         "model": "models/mapobjects/strogg/crates/crate1_small.lwo"},
        {"classname": "func_static", "name": "ren01_malformed_mirror", "model": "ren01_malformed_mirror"},
        {"classname": "func_static", "name": "ren01_malformed_xray", "model": "ren01_malformed_xray"},
    ]
    brushes["ren01_malformed_mirror"] = _brush(-128, -1088 + 8, 32, 128, -1088 + 16, 224,
                                               {"+y": "textures/common/mirror"})
    brushes["ren01_malformed_xray"] = _brush(-64, -640 - 16, 64, 64, -640 - 8, 192, {"-y": "ren01/xray"})
    for number, entity in enumerate(entities, start=1):
        out += [f"// entity {number}", "{"]
        out += [f'"{key}" "{value}"' for key, value in entity.items()]
        if entity["name"] in brushes:
            out += ["// primitive 0", "{", brushes[entity["name"]].rstrip(), "}"]
        out.append("}")
    return "\n".join(out) + "\n"


def prepare_fixture(runtime: Path, output: Path, timeout: int) -> dict:
    """Compile the validation map once; returns {relative path: text-or-bytes}."""
    root = output / "fixture-compile"
    if root.exists():
        shutil.rmtree(root)
    game = root / "baseoq4"
    (game / "maps" / "ren01").mkdir(parents=True)
    (game / "materials").mkdir(parents=True)
    (game / f"{FIXTURE_MAP}.map").write_text(fixture_map(), encoding="ascii")
    (game / "materials" / "ren01_validation.mtr").write_text(FIXTURE_MATERIALS, encoding="ascii")
    command = [str(client_executable(runtime)), "+set", "win_allowMultipleInstances", "1",
               "+set", "r_fullscreen", "0", "+set", "in_mouse", "0", "+set", "developer", "1",
               "+set", "logFile", "2", "+set", "logFileName", "logs/dmap.log",
               "+set", "fs_savepath", str(root), "+set", "fs_devpath", str(root), "+set", "fs_game", "baseoq4",
               "+dmap", FIXTURE_MAP, "+quit"]
    subprocess.run(command, cwd=runtime, timeout=timeout, check=False)
    # The engine locks fs_cdpath to its own content root, so dmap writes the
    # .proc beside the staged runtime. Move it into the isolated tree and leave
    # the staged package as it was.
    staged_proc = runtime / "baseoq4" / f"{FIXTURE_MAP}.proc"
    proc = game / f"{FIXTURE_MAP}.proc"
    if staged_proc.is_file():
        shutil.move(str(staged_proc), str(proc))
        try:
            staged_proc.parent.rmdir()
        except OSError:
            pass
    log = (game / "logs" / "dmap.log")
    log_text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    if not proc.is_file() or "leaked" in log_text.lower():
        raise RuntimeError(f"fixture dmap failed; see {log}")
    files = {}
    for path in game.rglob("*"):
        if path.is_file() and path.suffix in (".map", ".mtr", ".proc", ".cm"):
            files[path.relative_to(game).as_posix()] = path.read_bytes()
    return files

SCENES = [
    Scene(
        id="ad1",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="stock outdoor gameplay view: lighting, shadows, fog and the authored post tail",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        checks=[
            # The first-person weapon draws with a depth hack, which the
            # interaction domain does not own: the whole view stays classic.
            Check("interaction", control={"r_skipInteractions": "1"},
                  rollback={"ui_showGun": "1"}),
            Check("cinematicpost", control={"r_skipPostProcess": "1"}),
        ],
    ),
    # The same view with only classic stencil shadows, and with every light
    # class shadow-mapped (point cubes included), so the interaction domain is
    # qualified against each shadow ownership mode the presets can select.
    Scene(
        id="ad1stencil",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="the stock outdoor view with stencil shadows only",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_shadows": "1", "r_useShadowMap": "0"},
        checks=[
            Check("interaction", control={"r_skipInteractions": "1"}, rollback={"ui_showGun": "1"}),
        ],
        skip={"msaa-gl": "classic OpenGL stencil shadows with 4x MSAA flip isolated pixels between "
                         "identical frozen frames, with every domain off"},
    ),
    # Every light shadow-mapped, as the Quality and higher presets do, with
    # the static shadow cache on (the shipped default). The distant point
    # light's projected map has live casters, so classic reads its LOCAL map
    # where it lives in the cache and composes the live casters over a copy of
    # its GLOBAL map; the domain must consume the same cached maps.
    Scene(
        id="ad1mapped",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="the stock outdoor view with every light shadow-mapped (point cubes and a distant point "
                "light's projected map), static shadow cache on",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_shadows": "1", "r_useShadowMap": "1",
                "r_shadowMapPointLights": "1"},
        checks=[
            Check("interaction", control={"r_skipInteractions": "1"}, rollback={"ui_showGun": "1"}),
        ],
    ),
    # Dynamic casters: a CPU-skinned stock character (a receiver and a caster,
    # like every animated MD5) and a spawned stock crate under a stock
    # projected test light and a point light.
    Scene(
        id="mv2casters",
        map="maps/tools/mv2",
        pose="0 -192 96 20 90 0",
        purpose="a CPU-skinned stock character and a spawned crate as dynamic receivers and shadow "
                "casters under projected and point test lights",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_skipSubviews": "1", "r_useShadowMap": "1",
                "r_shadowMapPointLights": "1"},
        commands=["testModel char_doctor",
                  'spawn func_static model models/mapobjects/strogg/crates/crate1_medium.lwo origin "-90 -70 -5.7"',
                  'testPointLight 300 origin "96 -128 0"',
                  "testLight"],
        checks=[
            Check("interaction", control={"r_skipInteractions": "1"}, rollback={"ui_showGun": "1"}),
        ],
    ),
    # The world-ambient domain owns ambient-only views by design (no light,
    # decal, BSE, post-process or GUI surface), so the stock mv2 tools shell
    # is drawn with every light suppressed, as the domain's own evidence was.
    Scene(
        id="mv2amb",
        map="maps/tools/mv2",
        pose="0 0 256 80 0 0",
        purpose="the stock mv2 tools floor with every light suppressed: world ambient",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_singleLight": "2147483647",
                "r_skipSubviews": "1", "r_useLightGrid": "0"},
        checks=[
            # The first-person weapon's depth hack is a whole-view boundary.
            Check("ambient", control={"r_skipAmbient": "1"}, rollback={"ui_showGun": "1"}),
        ],
    ),
    Scene(
        id="mv2lit",
        map="maps/tools/mv2",
        pose="0 0 256 80 0 0",
        purpose="the stock mv2 tools floor under its own lights: ambient phases beside classic lighting",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_skipSubviews": "1"},
        checks=[
            Check("ambient", control={"r_skipAmbient": "1"}, rollback={"ui_showGun": "1"}),
        ],
    ),
    Scene(
        id="mv2deform",
        map="maps/tools/mv2",
        pose="0 0 256 80 0 0",
        purpose="the same view with the stock shaderDemos/move deforming material: material deform",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_singleLight": "2147483647",
                "r_skipSubviews": "1", "r_useLightGrid": "0",
                "r_materialOverride": "shaderDemos/move"},
        checks=[
            # Deform has no view of its own: the ambient domain consumes it.
            # With deform ownership off the ambient view must fall back.
            Check("deform", with_cvars={"r_rendererSharedWorldAmbient": "1"},
                  control={"r_skipDeforms": "1"},
                  rollback={"r_rendererSharedDeform": "0"}),
        ],
    ),
    Scene(
        id="ad1fog",
        map="game/airdefense1",
        pose="9600 -2600 -300 20 45 0",
        purpose="inside a stock fog light's volume near the airdefense1 bunker",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        checks=[
            Check("fogblend", control={"r_skipFogLights": "1", "r_skipBlendLights": "1"},
                  rollback={"r_showOverDraw": "1"}),
        ],
    ),
    Scene(
        id="ad1hud",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="the stock (legacy) HUD over a frozen outdoor view: root GUI",
        launch={"g_showHud": "1", "ui_showGun": "0", "ui_retained": "0"},
        checks=[
            Check("gui", scope="root", control={"g_showHud": "0"},
                  rollback={"r_showOverDraw": "1"}),
        ],
    ),
    Scene(
        id="ad1screen",
        map="game/airdefense1",
        pose="9719 -6767 0 0 225 0",
        purpose="the stock vidcomm1 monitor near the airdefense1 start: in-world GUI",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        checks=[
            Check("inworldgui", scope="world", control={"r_skipGuiShaders": "1"},
                  rollback={"r_showOverDraw": "1"}),
        ],
    ),
    # Raven's controller blur. Single-player scripts route setSpecialEffect to
    # the game's own blur post material; only the multiplayer game drives the
    # renderer controller (join screen, Arena). r_forceSpecialEffects drives
    # that same controller on a frozen single-player view, with the stock blur
    # parameters, and needs no listen server.
    Scene(
        id="ad1blur",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="Raven's special-effect controller blur (as the multiplayer join screen uses it) over a frozen stock view",
        # The stock HUD (frozen on the game clock) follows the 3D view: OpenGL
        # composites the controller blur when the next 2D view starts.
        launch={"g_showHud": "1", "ui_showGun": "0", "ui_retained": "0", "r_forceSpecialEffects": "1"},
        checks=[
            Check("specialframe", control={"r_forceSpecialEffects": "0"},
                  rollback={"r_rendererSharedFaultInject": "2"}),
        ],
    ),
    # Render-demo playback of a frozen view. Playback draws through the
    # renderer's own scene target, which must start every frame cleared: the
    # mv2 void above the horizon once kept each frame's bloom and brightened
    # from step to step on OpenGL under the default preset.
    Scene(
        id="mv2demo",
        map="maps/tools/mv2",
        pose="0 0 256 80 0 0",
        purpose="playback of a render demo recorded from the frozen, lit stock mv2 floor",
        launch={"g_showHud": "0", "ui_showGun": "0", "r_skipSubviews": "1"},
        frozen_commands=["recordDemo ren01demo", "wait 900", "stopRecording", "wait 5",
                         "playDemo ren01demo", "wait 30"],
        checks=[
            Check("specialframe", control={"r_skipAmbient": "1"},
                  rollback={"r_rendererSharedFaultInject": "2"}),
        ],
    ),
    # A root 2D view that plays a stock cinematic. GUI videos run on the wall
    # clock (com_frameRealTime), which no capture can pin, so this scene proves
    # ownership, visibility and rollback; exact parity of cinematic stages is
    # proven on the frozen game clock by the fixture's authored-post video.
    Scene(
        id="rootvideo",
        map="game/airdefense1",
        pose=AIRDEFENSE1_POSE,
        purpose="a root 2D view playing the stock video/idlogo cinematic (validation GUI; wall clock)",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        files={"guis/ren01_video.gui": (
            "windowDef Desktop\n{\n\trect 0,0,640,480\n\tbackcolor 0,0,0,1\n"
            "\twindowDef Video\n\t{\n\t\trect 0,0,640,480\n\t\tbackground \"video/idlogo\"\n"
            "\t\tmatcolor 1,1,1,1\n\t}\n}\n")},
        frozen_commands=["testGUI guis/ren01_video.gui", "wait 60"],
        wall_clock=True,
        checks=[
            Check("cinematicpost", control={"r_skipDynamicTextures": "1"},
                  rollback={"r_rendererSharedFaultInject": "4"}),
        ],
    ),
    Scene(
        id="fixpanels",
        map=FIXTURE_MAP,
        pose="0 -100 128 0 90 0",
        purpose="validation room A: reflection, dynamic-mirror, remote-camera and direct-mirror subviews side by side, and a _currentDepth post stage",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        fixture=True,
        checks=[
            Check("subview", **SUBVIEW_CONTROL,
                  rollback={"r_rendererSharedFaultInject": "1"}),
            Check("cinematicpost", control={"r_skipPostProcess": "1"},
                  rollback={"r_skipPostProcess": "1"}),
        ],
    ),
    Scene(
        id="fixxray",
        map=FIXTURE_MAP,
        pose="832 -300 128 0 90 0",
        purpose="validation room C: an x-ray capture alone in view",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        fixture=True,
        checks=[
            Check("subview", **SUBVIEW_CONTROL,
                  rollback={"r_rendererSharedFaultInject": "1"}),
        ],
    ),
    Scene(
        id="fixrefraction",
        map=FIXTURE_MAP,
        pose="832 340 128 0 90 0",
        purpose="validation room D: a refraction capture alone in view",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        fixture=True,
        checks=[
            Check("subview", **SUBVIEW_CONTROL,
                  rollback={"r_rendererSharedFaultInject": "1"}),
        ],
    ),
    Scene(
        id="fixnested",
        map=FIXTURE_MAP,
        pose="0 700 128 0 90 0",
        purpose="validation room B: a direct mirror facing a capture-backed mirror, a nested direct/capture chain",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        fixture=True,
        checks=[
            Check("subview", **SUBVIEW_CONTROL,
                  rollback={"r_rendererSharedFaultInject": "1"}),
        ],
    ),
    Scene(
        id="fixmalformed",
        map=FIXTURE_MAP,
        pose="0 -720 128 0 270 0",
        purpose="validation room E: a direct mirror whose x-ray descendant inherits the mirror's clip plane; the whole tree must decline",
        launch={"g_showHud": "0", "ui_showGun": "0"},
        fixture=True,
        checks=[
            Check("subview", **SUBVIEW_CONTROL, expect_decline=True),
        ],
    ),
]


def step_list(scene: Scene) -> list[tuple]:
    """Ordered (name, cvars[, before, after]) steps; each starts with every domain off."""
    steps: list[tuple] = [("ref", {})]
    for check in scene.checks:
        on = {DOMAIN_CVARS[check.domain]: "1", **check.with_cvars}
        steps.append((f"{check.domain}_on", on))
        if check.control or check.control_cmds[0]:
            steps.append((f"{check.domain}_ctl", dict(check.control),
                          tuple(check.control_cmds[0]), tuple(check.control_cmds[1])))
        if check.rollback:
            steps.append((f"{check.domain}_rbref", dict(check.rollback)))
            steps.append((f"{check.domain}_rb", {**on, **check.rollback}))
    every = {DOMAIN_CVARS[check.domain]: "1" for check in scene.checks}
    for check in scene.checks:
        every.update(check.with_cvars)
    steps.append(("all", every))
    steps.append(("refagain", {}))
    return steps


def reset_values(scene: Scene) -> dict:
    """Value each touched cvar returns to between steps."""
    values = {cvar: "0" for cvar in DOMAIN_CVARS.values()}
    defaults = {
        "g_showHud": scene.launch.get("g_showHud", "1"),
        "r_showOverDraw": "0", "r_skipTranslucent": "0", "r_skipInteractions": "0",
        "r_skipAmbient": "0", "r_skipFogLights": "0", "r_skipBlendLights": "0",
        "r_skipPostProcess": "0", "r_skipSubviews": "0", "r_skipDeforms": "0",
        "r_skipGuiShaders": "0", "r_singleTriangle": "0", "r_skipEntities": "0",
    }
    for check in scene.checks:
        for name in list(check.control) + list(check.rollback) + list(check.with_cvars):
            values.setdefault(name, scene.launch.get(name, defaults.get(name, "0")))
    return values


def build_cfg(scene: Scene, steps: list[tuple]) -> str:
    lines = ["god", "notarget", "noclip"]
    lines += list(scene.commands)
    if scene.pose:
        lines.append(f"setviewpos {scene.pose}")
    lines += [f"wait {scene.settle_frames}", "g_stopTime 1", "wait 60"]
    lines += list(scene.frozen_commands)
    resets = reset_values(scene)
    for step in steps:
        name, cvars = step[0], step[1]
        before = step[2] if len(step) > 2 else ()
        after = step[3] if len(step) > 3 else ()
        lines += [f"set {cvar} {value}" for cvar, value in resets.items()]
        lines += [f"set {cvar} {value}" for cvar, value in cvars.items()]
        lines += list(before)
        lines += ["wait 30", f"echo REN01_STEP {name}", "gfxInfo",
                  f"screenshot screenshots/{name}.tga", "wait 5"]
        lines += list(after)
    lines += ["wait 5", "quit"]
    return "\n".join(lines) + "\n"


def fields_of(line: str) -> dict:
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def split_steps(log_text: str) -> dict[str, list[str]]:
    blocks: dict[str, list[str]] = {}
    current = None
    for raw in log_text.splitlines():
        match = re.search(r"REN01_STEP (\S+)", raw)
        if match:
            current = match.group(1)
            blocks[current] = []
            continue
        if current is not None:
            blocks[current].append(raw.strip())
    return blocks


def slashed(value: str, index: int) -> int:
    try:
        return int(value.split("/")[index])
    except (ValueError, IndexError, AttributeError):
        return 0


def ownership(domain: str, scope: str, lines: list[str], backend: str) -> dict:
    """Active/inactive backend ownership for one domain from a gfxInfo block."""
    active = "GL" if backend == "gl" else "VK"
    inactive = "VK" if backend == "gl" else "GL"
    result = {"owned": 0, "fallback": 0, "mismatch": 0, "inactive": 0,
              "failures": [], "found": False}

    def first(prefix: str) -> dict | None:
        for line in lines:
            if line.startswith(prefix):
                return fields_of(line)
        return None

    def views(prefix: str) -> list[dict]:
        return [fields_of(line) for line in lines if line.startswith(prefix)]

    if domain in ("gui", "inworldgui"):
        summary = first("Renderer shared classic GUI:")
        rows = [row for row in views("Renderer shared classic GUI view[")
                if row.get("scope") == (scope or ("world" if domain == "inworldgui" else "root"))]
        if summary is not None:
            result["found"] = True
            for row in rows:
                outcome = slashed(row.get(active, ""), 0)
                if outcome == 1:
                    result["owned"] += 1
                elif outcome == 2:
                    result["fallback"] += 1
                    result["failures"].append(row.get(active, "").split("/")[1] if "/" in row.get(active, "") else "?")
                elif row.get("ready") == "0":
                    result["failures"].append(row.get("failure", "?"))
                if slashed(row.get(inactive, ""), 0) != 0:
                    result["inactive"] += 1
    elif domain == "ambient":
        summary = first("Renderer shared world ambient:")
        if summary is not None:
            result["found"] = True
            result["owned"] = slashed(summary.get(active), 0)
            result["fallback"] = slashed(summary.get(active), 1)
            result["inactive"] = slashed(summary.get(inactive), 0) + slashed(summary.get(inactive), 1)
            for row in views("Renderer shared world ambient view["):
                if row.get("ready") == "0":
                    result["failures"].append(f"{row.get('failure')}:{row.get('detail')}")
                elif slashed(row.get(active, ""), 0) == 2:
                    result["failures"].append(row.get(active, "").split("/")[1])
    elif domain == "interaction":
        summary = first("Renderer shared interaction:")
        if summary is not None:
            result["found"] = True
            result["owned"] = slashed(summary.get(active), 0)
            result["fallback"] = slashed(summary.get(active), 1)
            result["mismatch"] = slashed(summary.get(active), 2)
            result["inactive"] = sum(slashed(summary.get(inactive), i) for i in range(3))
            result["modes"] = sorted({row.get("mode", "?") for row in views("Renderer shared interaction view[")})
            result["shadow"] = summary.get("shadow", "")
            result["maps"] = summary.get("maps", "")
            for row in views("Renderer shared interaction view["):
                if row.get("ready") == "0":
                    result["failures"].append(f"{row.get('failure')}:{row.get('detail')}")
                elif slashed(row.get(active, ""), 0) == 2:
                    result["failures"].append(row.get(active, "").split("/")[1])
    elif domain == "fogblend":
        backend_name = "GL" if backend == "gl" else "Vulkan"
        other_name = "Vulkan" if backend == "gl" else "GL"
        for row in views("classicFogBlendDomain backend="):
            result["found"] = True
            if row.get("backend") == backend_name:
                result["owned"] = int(row.get("ownedViews", 0))
                result["fallback"] = int(row.get("fallbackViews", 0))
                result["mismatch"] = int(row.get("mismatches", 0)) + int(row.get("duplicate", 0))
                # Views the domain never prepared (a portal sky, a subview)
                # still reach the backend gate; they draw classic and are
                # reported, not failed.
                result["untracked"] = int(row.get("untracked", 0))
            elif row.get("backend") == other_name:
                result["inactive"] = int(row.get("ownedViews", 0)) + int(row.get("fallbackViews", 0))
        key = "GL" if backend == "gl" else "Vulkan"
        for row in views("classicFogBlendDomain view["):
            if row.get("ready") == "0":
                result["failures"].append(f"{row.get('failure')}:{row.get('detail')}")
            elif slashed(row.get(key, ""), 0) == 2:
                parts = row.get(key, "").split("/")
                result["failures"].append(f"{parts[1]}:{parts[2]}" if len(parts) > 2 else "?")
    elif domain == "subview":
        backend_name = "GL" if backend == "gl" else "Vulkan"
        other_name = "Vulkan" if backend == "gl" else "GL"
        for row in views("classicSubviewDomain backend="):
            result["found"] = True
            if row.get("backend") == backend_name:
                result["owned"] = int(row.get("ownedViews", 0))
                result["fallback"] = int(row.get("fallbackViews", 0))
                result["mismatch"] = int(row.get("mismatches", 0)) + int(row.get("duplicate", 0))
                result["untracked"] = int(row.get("untracked", 0))
            elif row.get("backend") == other_name:
                result["inactive"] = int(row.get("ownedViews", 0)) + int(row.get("fallbackViews", 0))
        # Views the domain itself refused (with a named failure) never reach a
        # backend record; count them as declines too.
        summary = first("classicSubviewDomain requested")
        if summary is not None:
            result["domainFallback"] = int(summary.get("fallback", 0))
        for row in views("classicSubviewDomain view["):
            if row.get("ready") == "0":
                result["failures"].append(row.get("failure", "?"))
    elif domain == "cinematicpost":
        summary = first("Renderer shared cinematic/post:")
        if summary is not None:
            result["found"] = True
            result["owned"] = slashed(summary.get(active), 0)
            result["fallback"] = slashed(summary.get(active), 1)
            result["mismatch"] = slashed(summary.get(active), 2) + slashed(summary.get(active), 3)
            result["inactive"] = sum(slashed(summary.get(inactive), i) for i in range(4))
            result["failures"].append(summary.get("status", "?"))
    elif domain == "specialframe":
        summary = first("Renderer shared special frame:")
        if summary is not None:
            result["found"] = True
            result["owned"] = slashed(summary.get(active), 0)
            result["fallback"] = slashed(summary.get(active), 1)
            result["inactive"] = slashed(summary.get(inactive), 0) + slashed(summary.get(inactive), 1)
            result["failures"].append(summary.get("status", "?"))
    elif domain == "deform":
        summary = first("Renderer shared material deform:")
        if summary is not None:
            result["found"] = True
            completed = int(summary.get("completed", 0)) + int(summary.get("empty", 0))
            # Deform has no view of its own: it counts as owned when a consumer
            # domain owned its view and every record completed.
            consumer = ownership("ambient", "", lines, backend)
            result["owned"] = consumer["owned"] if completed > 0 else 0
            result["fallback"] = consumer["fallback"]
            failed = sum(int(summary.get(key, 0)) for key in ("skipped", "failed", "unsupported", "invalid"))
            if failed:
                result["failures"].append(f"records failed={failed}")
            result["failures"] += consumer["failures"]
    return result


def load_rgb(path: Path):
    from PIL import Image
    if not path.is_file():
        return None
    image = Image.open(path).convert("RGB")
    return image.size[0], image.size[1], image.tobytes()


def compare(a, b, noise: set | None = None) -> dict:
    if a is None or b is None or a[:2] != b[:2]:
        return {"comparable": False}
    first, second = a[2], b[2]
    noise = noise or set()
    changed = 0
    total = 0
    maximum = 0
    for index, (x, y) in enumerate(zip(first, second)):
        if x != y and index // 3 not in noise:
            delta = abs(x - y)
            changed += 1
            total += delta
            maximum = max(maximum, delta)
    return {"comparable": True, "identical": changed == 0, "changedBytes": changed,
            "meanAbs": round(total / len(first), 5), "maxAbs": maximum}


def noise_mask(a, b) -> set | None:
    if a is None or b is None or a[:2] != b[:2]:
        return None
    return {i // 3 for i in range(len(a[2])) if a[2][i] != b[2][i]}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def client_executable(runtime: Path) -> Path:
    return runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")


def package_identity(runtime: Path) -> dict:
    """SHA-256 of the staged files that decide what is qualified."""
    patterns = ("openQ4-client_*", "renderer-*", "baseoq4/game-*", "baseoq4/pak*.pk4")
    identity = {}
    for pattern in patterns:
        for path in sorted(runtime.glob(pattern)):
            if path.is_file() and path.suffix.lower() not in (".pdb", ".lib", ".exp"):
                identity[path.relative_to(runtime).as_posix()] = sha256(path)
    return identity


def run_scene(runtime: Path, output: Path, scene: Scene, backend: str, config: str,
              timeout: int) -> dict:
    run_dir = output / f"{scene.id}-{config}-{backend}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    game_dir = run_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    steps = step_list(scene)
    (game_dir / "ren01.cfg").write_text(build_cfg(scene, steps), encoding="ascii")
    for relative, content in {**(FIXTURE_FILES if scene.fixture else {}), **scene.files}.items():
        target = game_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            target.write_bytes(content)
        else:
            target.write_text(content, encoding="ascii")
    command = [str(client_executable(runtime))]

    def setv(name, value):
        command.extend(["+set", name, str(value)])

    setv("win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances", 1)
    base = [("logFile", 2), ("logFileName", "logs/ren01.log"), ("developer", 1),
            ("fs_savepath", run_dir), ("fs_devpath", run_dir), ("fs_game", "baseoq4"),
            ("com_skipLoadingContinue", 1), ("com_loadingContinueAutoAdvance", 1),
            ("g_autoSkipCinematics", 1), ("com_maxfps", 0), ("r_swapInterval", 0),
            ("r_fullscreen", 0), ("r_borderless", 0), ("r_borderlessDefaultMigrated", 1),
            ("r_fullscreenDesktop", 0), ("r_windowWidth", WIDTH), ("r_windowHeight", HEIGHT),
            ("r_mode", -1), ("r_customWidth", WIDTH),
            ("r_customHeight", HEIGHT), ("ui_retained", 1), ("ui_autoJoin", 0),
            ("com_fixedTic", 1), ("in_mouse", 0),
            ("r_renderApi", "vulkan" if backend == "vk" else "gl"),
            ("g_autoExecAfterMapLoad", "ren01.cfg"), ("g_autoExecAfterMapLoadDelayMs", 0)]
    for name, value in base:
        setv(name, value)
    for name, value in CONFIGS[config].items():
        setv(name, value)
    for name, value in scene.launch.items():
        setv(name, value)
    for cvar in DOMAIN_CVARS.values():
        setv(cvar, 0)
    command += ["+devmap", scene.map]
    started = time.time()
    try:
        exit_code = subprocess.run(command, cwd=runtime, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        exit_code = "timeout"
    log_path = game_dir / "logs" / "ren01.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    blocks = split_steps(log_text)
    shots = {step[0]: game_dir / "screenshots" / f"{step[0]}.tga" for step in steps}
    images = {name: load_rgb(path) for name, path in shots.items()}
    noise = noise_mask(images.get("ref"), images.get("refagain"))
    result = {
        "scene": scene.id, "map": scene.map, "pose": scene.pose, "purpose": scene.purpose,
        "backend": backend,
        "config": config, "exitCode": exit_code, "seconds": round(time.time() - started, 1),
        "build": log_text.splitlines()[0] if log_text else "",
        "log": str(log_path), "noisePixels": len(noise) if noise is not None else None,
        "failuresInLog": [line for line in log_text.splitlines()
                          if any(p.search(line) for p in FAILURE_PATTERNS)][:10],
        "screenshots": {name: {"path": str(path), "sha256": sha256(path) if path.is_file() else None}
                        for name, path in shots.items()},
        "domains": {},
    }
    problems: list[str] = []
    if exit_code != 0:
        problems.append(f"exit code {exit_code}")
    problems += [f"log: {line.strip()[:200]}" for line in result["failuresInLog"]]
    missing = [name for name, image in images.items() if image is None]
    if missing:
        problems.append(f"missing screenshots {missing}")
    if noise is not None and noise and not scene.wall_clock:
        problems.append(f"{len(noise)} pixels differ between the two references (scene not frozen)")
    reference = images.get("ref")
    ref_owned = {check.domain: ownership(check.domain, check.scope, blocks.get("ref", []), backend)
                 for check in scene.checks}
    for check in scene.checks:
        name = check.domain
        record: dict = {"pass": False, "problems": []}
        on = ownership(name, check.scope, blocks.get(f"{name}_on", []), backend)
        record["on"] = on
        record["offOwnership"] = ref_owned[name]
        record["parity"] = compare(reference, images.get(f"{name}_on"), noise)
        if check.control or check.control_cmds[0]:
            record["control"] = compare(reference, images.get(f"{name}_ctl"))
        if check.rollback:
            rb = ownership(name, check.scope, blocks.get(f"{name}_rb", []), backend)
            record["rollback"] = rb
            record["rollbackParity"] = compare(images.get(f"{name}_rbref"), images.get(f"{name}_rb"), noise)
        record["all"] = ownership(name, check.scope, blocks.get("all", []), backend)
        record["allParity"] = compare(reference, images.get("all"), noise)
        issues = record["problems"]
        if not on["found"]:
            issues.append("no ownership record")
        if check.expect_decline:
            # A view the domain must refuse: nothing owned, a named fallback,
            # and (below) the classic image.
            if on["owned"]:
                issues.append(f"declining view owned {on['owned']}")
            if max(on["fallback"], on.get("domainFallback", 0)) <= 0 or not on["failures"]:
                issues.append("declining view recorded no named fallback")
        elif on["owned"] <= 0:
            issues.append(f"no owned view (fallback={on['fallback']} reasons={sorted(set(on['failures']))[:4]})")
        if on["mismatch"]:
            issues.append(f"coverage mismatch {on['mismatch']}")
        if on["inactive"]:
            issues.append(f"inactive backend recorded {on['inactive']}")
        off = ref_owned[name]
        if off["owned"] or off["fallback"]:
            issues.append("domain recorded work while off")
        if not record["parity"].get("identical") and not scene.wall_clock:
            issues.append(f"domain-on frame differs from classic ({record['parity'].get('changedBytes')} bytes)")
        if "control" in record and record["control"].get("changedBytes", 0) < check.min_delta:
            issues.append(f"control shows no visible content ({record['control'].get('changedBytes')} bytes)")
        if check.rollback:
            rb = record["rollback"]
            if rb["owned"]:
                issues.append(f"rollback still owned {rb['owned']}")
            if rb["fallback"] <= 0:
                issues.append("rollback recorded no fallback")
            if not record["rollbackParity"].get("identical") and not scene.wall_clock:
                issues.append(f"rollback differs from classic ({record['rollbackParity'].get('changedBytes')} bytes)")
        if not record["allParity"].get("identical") and not scene.wall_clock:
            issues.append(f"all-domains frame differs from classic ({record['allParity'].get('changedBytes')} bytes)")
        record["pass"] = not issues
        result["domains"][name] = record
    result["problems"] = problems
    result["pass"] = not problems and all(d["pass"] for d in result["domains"].values())
    return result


def markdown(report: dict) -> str:
    lines = ["# Shared classic domain qualification (REN-01)", "",
             f"Runtime `{report['runtime']}`; build `{report.get('build', '?')}`.", "",
             "| Scene | Config | Backend | Domain | Owned/fallback | On = classic | Control bytes | Rollback | Result |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for run in report["runs"]:
        for name, record in run["domains"].items():
            on = record["on"]
            rollback = "-"
            if "rollback" in record:
                rb = record["rollback"]
                rollback = f"{rb['owned']}/{rb['fallback']} {'same' if record['rollbackParity'].get('identical') else 'DIFF'}"
            lines.append(
                f"| {run['scene']} | {run['config']} | {run['backend']} | {name} | {on['owned']}/{on['fallback']} | "
                f"{'yes' if record['parity'].get('identical') else 'no'} | "
                f"{record.get('control', {}).get('changedBytes', '-')} | {rollback} | "
                f"{'PASS' if record['pass'] else 'FAIL: ' + '; '.join(record['problems'])} |")
        for problem in run["problems"]:
            lines.append(f"| {run['scene']} | {run['config']} | {run['backend']} | (run) | | | | | FAIL: {problem} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--backends", default="gl,vk")
    parser.add_argument("--configs", default="plain,default")
    parser.add_argument("--scenes", default="", help="comma-separated scene ids (default: all)")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not client_executable(runtime).is_file():
        print(f"no client in {runtime}", file=sys.stderr)
        return 2
    wanted = {name for name in args.scenes.split(",") if name}
    report: dict = {"runtime": str(runtime), "runs": [], "package": package_identity(runtime)}
    failed = False
    if any(scene.fixture and (not wanted or scene.id in wanted) for scene in SCENES):
        FIXTURE_FILES.update(prepare_fixture(runtime, output, args.timeout))
        report["fixture"] = {path: hashlib.sha256(content).hexdigest()
                             for path, content in sorted(FIXTURE_FILES.items())}
    for scene in SCENES:
        if wanted and scene.id not in wanted:
            continue
        for config in [name for name in args.configs.split(",") if name]:
            for backend in [name for name in args.backends.split(",") if name]:
                reason = scene.skip.get(f"{config}-{backend}")
                if reason:
                    report.setdefault("skipped", []).append(
                        {"scene": scene.id, "config": config, "backend": backend, "reason": reason})
                    print(f"SKIP {scene.id} {config} {backend}: {reason}")
                    continue
                result = run_scene(runtime, output, scene, backend, config, args.timeout)
                report["runs"].append(result)
                report.setdefault("build", result["build"])
                failed |= not result["pass"]
                print(f"{'PASS' if result['pass'] else 'FAIL'} {scene.id} {config} {backend} ({result['seconds']} s)")
                for problem in result["problems"]:
                    print(f"    run: {problem}")
                for name, record in result["domains"].items():
                    on = record["on"]
                    status = "pass" if record["pass"] else "; ".join(record["problems"])
                    print(f"    {name}: owned {on['owned']} fallback {on['fallback']}: {status}")
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (output / "report.md").write_text(markdown(report), encoding="utf-8")
    print(f"report: {output / 'report.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
