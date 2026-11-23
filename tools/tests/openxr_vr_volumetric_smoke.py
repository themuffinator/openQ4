#!/usr/bin/env python3
"""Shadowed volumetric lighting in VR stereo (REN-09) against the openQ4 OpenXR
test runtime (requires Pillow, a staged client built with OpenXR, and retail
assets).

Each eye is a view of its own: it marches the lights through its own depth and
its own projection, into the scattering target, and composites before fog. The
smoke holds a fixed view of airdefense1 with game time stopped and the Quality
shadow configuration, and checks:

- with the leaf on, rendererVolumetricStatus reports it effective in two main
  views a frame (one per eye), with shadow-mapped lights and no subview;
- both eyes change, and the scattered light alone (r_volumetricDebug 1) is
  present in both eyes;
- the eyes keep their parallax: they differ with the leaf as they do without
  it, and the scattered light itself differs between the eyes;
- the scattering targets hold still while the eyes alternate (no allocation
  between two status reports);
- the leaf off restores the baseline in both eyes.
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
SP_MAP, SP_POSE = "game/airdefense1", "10200 -6800 40 0 170 0"
SHADOW_STARTUP = {"r_useShadowMap": "1", "r_shadowMapCSM": "1", "r_shadowMapPointLights": "1"}


def mean_difference(a, b) -> float:
    from PIL import ImageChops, ImageStat
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean[:3]) / 3.0


def mean_level(image) -> float:
    from PIL import ImageStat
    return sum(ImageStat.Stat(image).mean[:3]) / 3.0


def status_after(text: str, tag: str, backend: str) -> tuple[dict, dict]:
    """The settings line and the backend line of the status printed after tag."""
    block = text.partition(tag)[2]
    settings = re.search(r"Volumetric lighting: (requested=.*)", block)
    view = re.search(rf"Volumetric lighting {backend}: (effective=.*)", block)
    fields = lambda match: dict(re.findall(r"(\w+)=(\S+)", match.group(1))) if match else {}
    return fields(settings), fields(view)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    options = parser.parse_args(argv)
    from PIL import Image

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-volumetric-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / options.render_api
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in list(game.glob("vr_marker_*.txt")) + list(profile.glob("xr_*.tga")):
        stale.unlink()
    (game / "logs/openq4.log").unlink(missing_ok=True)

    captures = ("off", "on", "scatter", "off_again")

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    script = [f"when {marker(name)} capture {profile / ('xr_' + name)}" for name in captures]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    settle = ["wait 3", "waitMsec 400", "wait 3"]

    def capture(name: str) -> list[str]:
        return [*settle, f"condump vr_marker_{name}.txt", *settle]

    commands = [
        "waitMsec 3000", "god", "notarget", f"setviewpos {SP_POSE}", "g_stopTime 1", "waitMsec 1000",
        "r_rendererFroxelVolumetrics 0", "r_volumetricDebug 0", *capture("off"),
        "r_rendererFroxelVolumetrics 1", "wait 30",
        "echo VR_VOLUMETRIC_STATUS_A", "rendererVolumetricStatus", "wait 60",
        "echo VR_VOLUMETRIC_STATUS_B", "rendererVolumetricStatus", *capture("on"),
        "r_volumetricDebug 1", *capture("scatter"),
        "r_volumetricDebug 0", "r_rendererFroxelVolumetrics 0", "wait 10",
        "echo VR_VOLUMETRIC_STATUS_OFF", "rendererVolumetricStatus", *capture("off_again"),
        "echo VR_VOLUMETRIC_COMPLETE", "quit",
    ]
    (game / "openxr_vr_volumetric_smoke.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"].startswith("(SP) Main menu — GL"))
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
        "g_autoExecAfterMapLoad": "openxr_vr_volumetric_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1", "s_noSound": "1",
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_roomScale": "0", "vr_aimLaser": "0",
        "vr_comfortVignette": "0", "r_temporalAA": "0", "r_multiSamples": "0", "r_postAA": "0",
        "r_motionBlur": "0", "r_rendererFroxelVolumetrics": "0", "r_volumetricDebug": "0",
        "r_renderApi": options.render_api, **SHADOW_STARTUP,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", SP_MAP]
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
    assert result.returncode == 0 and "VR_VOLUMETRIC_COMPLETE" in text, "the smoke did not complete"
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    for pattern in (r"Validation Error|VUID-", r"GLSL .* failed for builtin/volumetric"):
        found = re.search(pattern, text)
        assert not found, f"the log reports {found.group(0)}"

    backend = "Vulkan" if options.render_api == "vulkan" else "OpenGL"
    settings_a, view_a = status_after(text, "VR_VOLUMETRIC_STATUS_A", backend)
    settings_b, view_b = status_after(text, "VR_VOLUMETRIC_STATUS_B", backend)
    settings_off, _ = status_after(text, "VR_VOLUMETRIC_STATUS_OFF", backend)
    assert view_b, f"rendererVolumetricStatus printed no {backend} line"
    assert settings_b.get("requested") == "1" and view_b.get("effective") == "1", \
        f"the leaf is not effective in VR ({view_b.get('decline', 'no status')})"
    views, subviews = int(view_b.get("views", "0")), int(view_b.get("subviews", "0"))
    assert views - subviews >= 2, f"the leaf scattered in {views - subviews} main view(s) a frame, not both eyes"
    assert int(view_b.get("viewsDeclined", "0")) == 0, f"the leaf declined a view: {view_b.get('decline')}"
    mapped = int(view_b.get("shadowMapped", "0"))
    assert mapped >= 2, f"{mapped} shadow-mapped light passes over both eyes"
    allocations = int(settings_b.get("targetAllocations", "0")) - int(settings_a.get("targetAllocations", "0"))
    assert allocations == 0, f"the scattering targets were reallocated {allocations} times while the eyes held still"
    assert settings_off.get("requested") == "0", "the leaf still reports requested after it was turned off"

    def eye(name: str, side: str):
        with Image.open(profile / f"xr_{name}_{side}.tga") as image:
            return image.convert("RGB")

    eyes = {name: (eye(name, "left"), eye(name, "right")) for name in captures}
    effect = [mean_difference(eyes["on"][i], eyes["off"][i]) for i in (0, 1)]
    scatter = [mean_level(eyes["scatter"][i]) for i in (0, 1)]
    for index, side in enumerate(("left", "right")):
        assert mean_level(eyes["off"][index]) > 8.0, f"the {side} eye is dark"
        assert effect[index] > 0.05, f"the leaf did not change the {side} eye ({effect[index]:.3f})"
        assert scatter[index] > 0.05, f"no scattered light in the {side} eye ({scatter[index]:.3f})"
    assert min(effect) > 0.5 * max(effect), f"the eyes scatter unequally ({effect[0]:.3f} against {effect[1]:.3f})"
    parallax_off = mean_difference(eyes["off"][0], eyes["off"][1])
    parallax_on = mean_difference(eyes["on"][0], eyes["on"][1])
    parallax_scatter = mean_difference(eyes["scatter"][0], eyes["scatter"][1])
    assert parallax_on > 0.5 * parallax_off, \
        f"the eyes lost their parallax with the leaf ({parallax_on:.2f} against {parallax_off:.2f})"
    # one eye's scattering shown to both would read as zero here
    assert parallax_scatter > 0.02 * max(scatter), \
        f"both eyes show the same scattered light ({parallax_scatter:.4f})"
    restored = [mean_difference(eyes["off_again"][i], eyes["off"][i]) for i in (0, 1)]
    assert max(restored) < 0.02, f"the leaf off did not restore the baseline ({restored[0]:.4f}, {restored[1]:.4f})"
    identical = all(eyes["off_again"][i].tobytes() == eyes["off"][i].tobytes() for i in (0, 1))
    print(f"OpenXR VR volumetric smoke ({options.render_api}): PASS "
          f"(views {views}, subviews {subviews}, shadow-mapped passes {mapped}, target {view_b.get('target')}, "
          f"effect {effect[0]:.3f}/{effect[1]:.3f}, scatter {scatter[0]:.3f}/{scatter[1]:.3f}, "
          f"parallax {parallax_on:.2f}/{parallax_off:.2f}, scatter parallax {parallax_scatter:.3f}, "
          f"rollback {'identical' if identical else f'{max(restored):.4f}'}); evidence={profile}")


if __name__ == "__main__":
    main()
