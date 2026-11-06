#!/usr/bin/env python3
"""Per-eye temporal AA in VR (VR-05) against the openQ4 OpenXR test runtime
(requires Pillow, a staged client built with OpenXR, and retail assets).

Each eye keeps its own temporal history: its view mixes its eye into its
temporal identity, so it reprojects from its own previous frame, and backends
and the game keep its history images and velocity state apart. With one shared
history the eyes alternated it, and every resolve reset (the other eye is a
camera 6.4 cm away). The smoke holds a fixed view of airdefense1 with game
time stopped and checks, in single player (the game's scene chain) or with
--mp in multiplayer (the backend's own temporal scene):

- r_temporalAA 1 resolves both eyes from history: most resolves keep history,
  no per-view history resets beyond the first samples, no fallbacks;
- both eyes are lit and still differ (parallax survives the resolve), and a
  converged temporal eye stays close to the same eye without temporal AA;
- a 40-degree head turn resets or rejects history rather than smearing: the
  turned eye differs from the still one as much as it does without TAA.
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
SP_MAP, SP_POSE = "game/airdefense1", "10310.89 -6950.34 6.96 150.7"
# (multiplayer cannot stop game time; noclip holds the camera still in the arena)
MP_MAP, MP_POSE = "mp/q4dm8", "700 60 68 0"


def mean_difference(a, b) -> float:
    from PIL import ImageChops, ImageStat
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean[:3]) / 3.0


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
    parser.add_argument("--mp", action="store_true", help=f"host {MP_MAP}: the backend's temporal scene path")
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
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-temporal-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / f"{'mp' if options.mp else 'sp'}-{options.render_api}"
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in list(game.glob("vr_marker_*.txt")) + list(profile.glob("xr_*.tga")):
        stale.unlink()
    (game / "logs/openq4.log").unlink(missing_ok=True)

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    script = [
        f"when {marker('off')} capture {profile / 'xr_taa_off'}",
        f"when {marker('on')} capture {profile / 'xr_taa_on'}",
        f"when {marker('turn')} head 40 0 0",
        f"when {marker('turned_on')} capture {profile / 'xr_turned_on'}",
        f"when {marker('turned_off')} capture {profile / 'xr_turned_off'}",
        f"when {marker('back')} head 0 0 0",
    ]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    settle = ["wait 3", "waitMsec 400", "wait 3"]
    commands = [
        "waitMsec 3000", "god", "notarget", *(["noclip"] if options.mp else []),
        f"setviewpos {MP_POSE if options.mp else SP_POSE}", "g_stopTime 1", "waitMsec 1000",
        # without temporal AA: the reference eyes
        "r_temporalAA 0", *settle, "condump vr_marker_off.txt", *settle,
        # with it: history from both eyes' own previous frames
        "r_temporalAA 1", "wait 30", "rendererTemporalPresentationStatus reset", "wait 120",
        "echo VR_TAA_STATUS", "rendererTemporalPresentationStatus",
        "condump vr_marker_on.txt", *settle,
        # a fast head turn: history must not smear across it
        "condump vr_marker_turn.txt", "wait 2", "condump vr_marker_turned_on.txt", *settle,
        "r_temporalAA 0", *settle, "condump vr_marker_turned_off.txt", *settle,
        "condump vr_marker_back.txt", *settle,
        "echo VR_TAA_COMPLETE", "quit",
    ]
    (game / "openxr_vr_temporal_smoke.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    name = "(MP) Main menu" if options.mp else "(SP) Main menu — GL"
    launch = next(c for c in launches["configurations"] if c["name"].startswith(name))
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    for i in range(len(args) - 2):
        if args[i] == "+set" and args[i + 1] == "ui_autoJoin":
            args[i + 2] = "1"
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60", "r_vkValidation": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_temporal_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1", "s_noSound": "1",
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_roomScale": "0", "vr_aimLaser": "0",
        "vr_comfortVignette": "0", "r_temporalAA": "0", "r_multiSamples": "0", "r_postAA": "0",
        "r_renderApi": options.render_api,
    }
    if options.mp:
        settings.update({"net_serverDedicated": "0", "si_gameType": "DM", "sv_cheats": "1", "net_allowCheats": "1",
                         "net_ip": "127.0.0.1", "net_LANServer": "1", "si_pure": "0", "bot_minPlayers": "0"})
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+spawnServer", MP_MAP] if options.mp else ["+map", SP_MAP]
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
    assert result.returncode == 0 and "VR_TAA_COMPLETE" in text, "the smoke did not complete"
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    status = text.partition("VR_TAA_STATUS")[2]
    found = re.search(r"Temporal resolve: history=(\d+) seeded=(\d+) capture=(\d+) spatial=(\d+) fallback=(\d+) "
                      r"velocityNotAttempted=(\d+) velocityNone=(\d+) velocityPartial=(\d+) velocityComplete=(\d+)", status)
    assert found, "rendererTemporalPresentationStatus printed no resolve line"
    history, seeded, capture, spatial, fallback = (int(found.group(i)) for i in range(1, 6))
    velocity_complete = int(found.group(9))
    resolves = history + seeded + capture + spatial + fallback
    # (the status prints between frames, on the virtual screen, which requests
    # no temporal resolve; the eyes' resolves below show it was requested)
    assert resolves >= 150, f"too few temporal resolves for two eyes over 120 frames ({resolves})"
    assert history >= 0.9 * resolves, f"each eye should resolve from its own history: {history} of {resolves}"
    assert fallback == 0, f"{fallback} temporal resolves fell back"
    resets = re.search(r"Temporal view history: none=(\d+) (.*)", status)
    assert resets, "no view-history line"
    others = {k: int(v) for k, v in re.findall(r"([a-z-]+)=(\d+)", resets.group(2))}
    cut = sum(v for k, v in others.items() if k not in ("first-sample",))
    assert cut <= 2, f"eyes kept cutting each other's history: {others}"

    def eye(prefix: str, side: str):
        with Image.open(profile / f"{prefix}_{side}.tga") as image:
            return image.convert("RGB")

    off_l, off_r, on_l, on_r = eye("xr_taa_off", "left"), eye("xr_taa_off", "right"), eye("xr_taa_on", "left"), eye("xr_taa_on", "right")
    for name, image in (("off left", off_l), ("on left", on_l), ("on right", on_r)):
        from PIL import ImageStat
        assert sum(ImageStat.Stat(image).mean[:3]) / 3 > 8.0, f"the {name} eye is dark"
    parallax_off, parallax_on = mean_difference(off_l, off_r), mean_difference(on_l, on_r)
    assert parallax_on > 0.5 * parallax_off, f"the eyes lost their parallax under TAA ({parallax_on:.2f} vs {parallax_off:.2f})"
    converged = mean_difference(on_l, off_l)
    assert converged < 6.0, f"the converged temporal eye drifted from the same eye without TAA ({converged:.2f})"
    turned_on, turned_off = eye("xr_turned_on", "left"), eye("xr_turned_off", "left")
    turn_on, turn_off = mean_difference(on_l, turned_on), mean_difference(off_l, turned_off)
    assert turn_on > 0.6 * turn_off, f"history smeared across the head turn ({turn_on:.2f} vs {turn_off:.2f})"
    print(f"OpenXR VR temporal smoke ({'mp' if options.mp else 'sp'} {options.render_api}): PASS "
          f"(history {history}/{resolves}, velocity complete {velocity_complete}, parallax {parallax_on:.2f}/{parallax_off:.2f}, "
          f"converged diff {converged:.2f}, turn {turn_on:.2f}/{turn_off:.2f}); evidence={profile}")


if __name__ == "__main__":
    main()
