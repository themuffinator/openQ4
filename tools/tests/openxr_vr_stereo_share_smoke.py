#!/usr/bin/env python3
"""Stereo work sharing (VR-04) against the openQ4 OpenXR test runtime
(requires Pillow, a staged client built with OpenXR, and retail assets).

Two eyes render one game frame. Work that does not depend on the eye is done
once and shared; work that does is done for each eye. In front of the player
on airdefense1 the smoke spawns three flat test screens, as fixtures in its
own profile (never shipped content):

- a world GUI. Its quads are in the GUI's own 640x480 space, so the second eye
  emits the quads the first eye drew instead of running the GUI again;
- a remote camera (remoteRenderMap). The camera does not follow the viewer,
  so it renders once per frame for both eyes;
- a mirror render map. The mirror camera follows the viewer, so each eye
  renders its own: reusing the first eye's capture showed the second eye
  the first eye's reflection.

With game time stopped it counts each frame's work (r_showDynamic) flat, in
stereo with GUI sharing off (r_vrStereoGuiShare 0), and in stereo with it on,
and requires: stereo GUI surfaces equal those with sharing off while every
second eye's GUI is shared (half the redraws); twice the flat mirror renders;
the flat remote-camera renders. Both eyes captured with sharing on must be no
further from either of two captures with it off, taken the same number of
frames before and after, than those two are from each other: on OpenGL the
sky and the mirror keep moving with game time stopped.

Each eye must also show the GUI's blue backdrop. On OpenGL the eyes once lost
the GUI wherever the portal sky sat behind it: the scene target's present
discarded every far-depth pixel to let the sky through, and a GUI writes no
depth. The sky now goes under the scene before the root draws.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/validation"))
from generate_pbr_validation_map import backface_plane_ase  # noqa: E402

MAP = "game/airdefense1"
EYE = (10310.89, -6950.34, 6.96)
# 40 degrees left of the start's facing, away from the dropship hull: with every
# eye culling correctly, the hull, the wreck and the dune hid the screens there
YAW = 190.7
PREFIX = "textures/openq4/vr_share"
MODEL = "models/openq4/vr_share/screen.ase"
MATERIALS = f"""{PREFIX}/gui
{{
	translucent
	noshadows
	discrete
	guiSurf entity
}}
{PREFIX}/remote
{{
	noshadows
	discrete
	{{
		remoteRenderMap 256 256
		map textures/common/camera1.tga
		scale 1, -1
	}}
}}
{PREFIX}/mirror
{{
	noshadows
	discrete
	{{
		mirrorRenderMap 256 256
	}}
}}
"""
# no text: colour blocks only, so the fixture needs no language strings
GUI = """windowDef Desktop
{
	rect 0, 0, 640, 480
	backcolor 0.1, 0.3, 0.6, 1
	visible 1
	windowDef Block
	{
		rect 80, 120, 480, 240
		backcolor 0.9, 0.6, 0.1, 1
		visible 1
	}
}
"""
COUNTERS = re.compile(r"guis:(\d+) guisShared:(\d+) decals:\d+ eyeSubviews:(\d+) remoteSubviews:(\d+)")


def screen_spawns() -> list[str]:
    forward = (math.cos(math.radians(YAW)), math.sin(math.radians(YAW)))
    right = (math.cos(math.radians(YAW - 90)), math.sin(math.radians(YAW - 90)))

    def at(distance: float, lateral: float, up: float = 8.0) -> str:
        x = EYE[0] + forward[0] * distance + right[0] * lateral
        y = EYE[1] + forward[1] * distance + right[1] * lateral
        return f"{x:.2f} {y:.2f} {EYE[2] + up:.2f}"

    # the quad faces -y at angle 0; turn it to face the player
    facing = f"{(YAW + 180.0 - 270.0) % 360.0:.1f}"
    # the camera looks away from the screens, so its view holds no test GUI
    camera = f'spawn func_cameraview name vr_share_camera origin "{at(0, 0, 160)}" angle {(YAW + 180.0) % 360.0:.1f}'
    # 144-unit screens: the GUI and remote camera side by side, the mirror above the GUI against the
    # sky, all raised clear of the ground (lower, the terrain cuts into their bottom edges)
    screens = [(-155, 230.0, "mirror", ""), (0, 80.0, "remote", " cameraTarget vr_share_camera"),
               (-155, 80.0, "gui", ' gui "guis/openq4_vr_share.gui"')]
    return [camera] + [f'spawn func_static name vr_share_{kind} model "{MODEL}" shader "{PREFIX}/{kind}" '
                       f'origin "{at(240, lateral, up)}" angle {facing} solid 0{extra}'
                       for lateral, up, kind, extra in screens]


# The GUI's backdrop covers about 25,000 pixels of an 800x880 eye; a GUI lost to
# the sky behind it kept about 1,300.
GUI_MIN_PIXELS = 8000


def gui_backdrop_pixels(image) -> int:
    """Pixels of the fixture GUI's blue backdrop (backcolor 0.1, 0.3, 0.6)."""
    return sum(1 for r, g, b in image.getdata() if b > 110 and b > r + 50 and b > g + 20)


def frame_counts(text: str, begin: str, end: str) -> dict:
    section = text.partition(begin)[2].partition(end)[0]
    rows = [tuple(int(v) for v in m.groups()) for m in COUNTERS.finditer(section)]
    # the first and last lines can straddle the cvar change
    rows = rows[1:-1]
    assert len(rows) >= 4, f"too few counter frames between {begin} and {end} ({len(rows)})"
    keys = ("guis", "shared", "eye_subviews", "remote_subviews")
    return {k: int(statistics.median(r[i] for r in rows)) for i, k in enumerate(keys)}


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    options = parser.parse_args(argv)
    from PIL import Image, ImageChops, ImageStat

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-stereo-share-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / options.render_api
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in list(game.glob("vr_marker_*.txt")) + list(profile.glob("xr_*.tga")):
        stale.unlink()
    (game / "logs/openq4.log").unlink(missing_ok=True)
    fixtures = {f"materials/openq4_vr_share.mtr": MATERIALS, "guis/openq4_vr_share.gui": GUI,
                MODEL: backface_plane_ase().replace("textures/openq4/pbr_lab/neutral", f"{PREFIX}/remote")}
    for name, content in fixtures.items():
        (game / name).parent.mkdir(parents=True, exist_ok=True)
        (game / name).write_text(content, encoding="utf-8")

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    script = [f"when {marker(name)} capture {profile / ('xr_' + name)}" for name in ("on", "off", "off2")]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    restart = 6000 if options.render_api == "vulkan" else 2500
    settle = ["wait 3", "waitMsec 400", "wait 3"]

    def count(tag: str) -> list[str]:
        return [f"echo VR_SHARE_{tag}", "r_showDynamic 1", "wait 10", "r_showDynamic 0", f"echo VR_SHARE_{tag}_END"]

    commands = [
        "waitMsec 2500", "god", "notarget", f"setviewpos {EYE[0]} {EYE[1]} {EYE[2]} {YAW}", "waitMsec 500",
        *screen_spawns(), "wait 30", "g_stopTime 1", "waitMsec 1000",
        *count("FLAT"),
        # On OpenGL the sky and the mirror keep moving with game time stopped, so the shared capture sits
        # midway between two captures with sharing off, the same number of frames from each
        "r_vrStereoGuiShare 0", "vr_enable 1", "vr_restart", f"waitMsec {restart}", "wait 30",
        *count("OFF"), *settle, "condump vr_marker_off.txt", *settle,
        "r_vrStereoGuiShare 1", "wait 10", *count("STEREO"), *settle, "condump vr_marker_on.txt", *settle,
        "r_vrStereoGuiShare 0", "wait 10", "wait 10", *settle, "condump vr_marker_off2.txt", *settle,
        "r_vrStereoGuiShare 1", "echo VR_SHARE_COMPLETE", "quit",
    ]
    (game / "openxr_vr_stereo_share_smoke.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60", "r_vkValidation": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_stereo_share_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1", "s_noSound": "1",
        "vr_enable": "0", "vr_debug": "0", "vr_aimMode": "1", "vr_roomScale": "0", "vr_aimLaser": "0",
        "vr_comfortVignette": "0", "r_temporalAA": "0", "r_multiSamples": "0", "r_postAA": "0",
        "r_vrStereoGuiShare": "1", "r_showDynamic": "0", "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", MAP]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    environment = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_")}
    environment.update({"XR_RUNTIME_JSON": str(manifest), "OPENQ4_XR_TEST_EYE_SIZE": "800x880",
                        "OPENQ4_XR_TEST_RATE": "60", "OPENQ4_XR_TEST_LOG": str(profile / "xr.jsonl"),
                        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt")})
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    with (profile / "process.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT, env=environment,
                                startupinfo=startup, timeout=options.timeout)
    text = (game / "logs/openq4.log").read_text(encoding="utf-8", errors="replace")
    assert result.returncode == 0 and "VR_SHARE_COMPLETE" in text, "the smoke did not complete"
    assert "OpenXR: presenting to the headset" in text, "the session never began"

    flat = frame_counts(text, "VR_SHARE_FLAT", "VR_SHARE_FLAT_END")
    stereo = frame_counts(text, "VR_SHARE_STEREO", "VR_SHARE_STEREO_END")
    off = frame_counts(text, "VR_SHARE_OFF", "VR_SHARE_OFF_END")
    counts = {"flat": flat, "stereo": stereo, "stereo_share_off": off}
    assert flat["guis"] >= 1 and flat["eye_subviews"] >= 1 and flat["remote_subviews"] >= 1, \
        f"the flat view must see the GUI, mirror and remote screens: {flat}"
    assert flat["shared"] == 0 and off["shared"] == 0, f"flat frames and sharing off never share GUI quads: {counts}"
    assert stereo["guis"] == off["guis"] == 2 * flat["guis"], f"stereo draws each eye's GUI surfaces: {counts}"
    assert 2 * stereo["shared"] == stereo["guis"], f"every second eye's GUI must be shared: {counts}"
    assert stereo["eye_subviews"] == 2 * flat["eye_subviews"], f"each eye renders its own mirror: {counts}"
    assert stereo["remote_subviews"] == flat["remote_subviews"], f"a remote camera renders once per frame: {counts}"

    def eye(prefix: str, side: str):
        with Image.open(profile / f"xr_{prefix}_{side}.tga") as image:
            return image.convert("RGB")

    def difference(a, b) -> float:
        return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean[:3]) / 3.0

    images = {}
    for side in ("left", "right"):
        on, first, second = eye("on", side), eye("off", side), eye("off2", side)
        assert sum(ImageStat.Stat(on).mean[:3]) / 3 > 8.0, f"the {side} eye is dark"
        control = difference(first, second)
        shared = max(difference(on, first), difference(on, second))
        gui = gui_backdrop_pixels(on)
        images[side] = {"control": round(control, 4), "share_vs_off": round(shared, 4), "gui_pixels": gui}
        assert shared <= control + 0.01, f"the {side} eye changed with GUI sharing ({shared:.4f}, control {control:.4f})"
        assert gui >= GUI_MIN_PIXELS, f"the {side} eye lost the world GUI ({gui} backdrop pixels)"
    report = {"status": "pass", "render_api": options.render_api, "counts": counts, "images": images,
              "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}
    (profile / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"OpenXR VR stereo share smoke ({options.render_api}): PASS {counts} {images}; evidence={profile}")


if __name__ == "__main__":
    main()
