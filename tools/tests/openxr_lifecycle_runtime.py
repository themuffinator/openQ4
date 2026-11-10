#!/usr/bin/env python3
"""Exercise XR loss and recovery during windowed gameplay without controller input.

Uses the existing test runtime through a process-local loader manifest. Desktop
captures come from the engine screenshot command. On OpenGL the eye captures
are the engine's own eye render textures; the Vulkan backend keeps its eye
targets to itself, so there the test runtime captures the eye images the
engine submitted, synchronised by marker files the engine writes with condump.
With --render-api vulkan the runtime also creates the renderer's instance and
device, and --validation fails the run on any Khronos validation message. No
system runtime registration or keyboard/mouse/controller input is changed or
synthesized.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--manifest", type=Path, default=ROOT / "builddir/openq4_xr_test_runtime.json")
    parser.add_argument("--fault", default="xrWaitSwapchainImage:2:timeout")
    parser.add_argument("--creation-fault", action="store_true", help="arm before the first XR session is created")
    parser.add_argument("--mode", choices=("SP", "MP"), default="SP")
    parser.add_argument("--port", type=int, default=29104)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--validation", action="store_true", help="Vulkan: fail on Khronos validation messages")
    parser.add_argument("--windowless", action="store_true",
                        help="Vulkan: VR frames open without the window's image (r_vkVRWindowlessTest)")
    args = parser.parse_args()
    vulkan = args.render_api == "vulkan"
    if (args.validation or args.windowless) and not vulkan:
        parser.error("--validation and --windowless need --render-api vulkan")
    if args.fault != "none" and not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*:[1-9][0-9]*:[a-z]+", args.fault):
        parser.error("fault must be call:positive-occurrence:value or none")
    runtime, output, manifest = args.runtime_dir.resolve(), args.output_dir.resolve(), args.manifest.resolve()
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    verify_runtime_is_current(runtime, executable.name)
    runtime_library = manifest.parent / json.loads(manifest.read_text())["runtime"]["library_path"]
    output.mkdir(parents=True, exist_ok=False)
    save = output / "save"
    game = save / "baseoq4"
    game.mkdir(parents=True)
    arm, receipt, xr_log = game / "xr_fault_arm.txt", output / "fault.once", output / "xr.jsonl"
    # Vulkan eye captures: the runtime writes the submitted eye images when the
    # engine's marker files appear
    eye_marks = {"restart": game / "xr_capture_restart.txt", "after_capture": game / "xr_capture_after.txt"}
    eye_prefix = {name: output / f"rt_{name}" for name in eye_marks}
    if vulkan:
        (output / "xr_script.txt").write_text(
            "".join(f"when {eye_marks[name]} capture {eye_prefix[name]}\n" for name in eye_marks), encoding="utf-8")
    # Vulkan: the first VR start restarts the renderer once, so the runtime
    # creates its device, before anything is armed; vr_restart then keeps the
    # instance that made it. A new instance after instance or runtime loss
    # restarts the renderer again. The validation layer slows stereo frames
    # several times, so the waits that need frames to pass grow with it.
    instance_fault = args.fault.endswith((":instance", ":runtime"))
    slow = vulkan and args.validation
    auto_wait = 6000 if slow else 3500
    restart_wait = (7000 if instance_fault else 4000) if vulkan else 1800
    if slow and args.fault.endswith(":tracking"):
        # the runtime reports 90 frames of lost tracking; let them pass first
        restart_wait += 12000
    capture_wait = 1500 if slow else 300
    commands = ["waitMsec 1500", "god", "notarget", "getviewpos", "echo XR_FAULT_GAMEPLAY_BEGIN"]
    if args.mode == "MP":
        commands += ["openq4_reportMPState", "openq4_assertMPGameplayView"]
    if vulkan:
        # the renderer restart itself takes seconds; then the session needs frames
        commands += ["vr_enable 1", "vr_restart", "waitMsec 6000", "wait 10", "waitMsec 4000",
                     "echo XR_FAULT_VULKAN_DEVICE", "vr_status"]
    if args.creation_fault:
        commands += ["condump xr_fault_arm.txt"]
    commands += ["vr_enable 1", "vr_restart", "waitMsec 500" if args.creation_fault else "waitMsec 2000",
                 "echo XR_FAULT_BEFORE", "vr_status", 'screenshot "screenshots/xr_before.tga"']
    if not args.creation_fault:
        commands += ["condump xr_fault_arm.txt"]
    commands += ["waitMsec 700", "echo XR_FAULT_AFTER", "vr_status", 'screenshot "screenshots/xr_fallback.tga"',
                 f"waitMsec {auto_wait}", "echo XR_FAULT_AUTO", "vr_status", "vr_restart", f"waitMsec {restart_wait}"]
    if vulkan:
        # a renderer restart blocks the wait above; the session needs frames after it
        commands += ["wait 10", "waitMsec 3000"]
    commands += ["echo XR_FAULT_RESTART", "vr_status", "getviewpos"]
    if vulkan:
        commands += ["condump xr_capture_restart.txt", f"waitMsec {capture_wait}",
                     'screenshot "screenshots/xr_restart.tga"', "waitMsec 250",
                     "condump xr_capture_after.txt", f"waitMsec {capture_wait}"]
    else:
        commands += ['screenshot image _vrEyeColor0 "screenshots/xr_left.tga"',
                     'screenshot image _vrEyeColor1 "screenshots/xr_right.tga"',
                     'screenshot "screenshots/xr_restart.tga"', "waitMsec 250",
                     'screenshot image _vrEyeColor0 "screenshots/xr_after_capture.tga"']
    commands += ["vr_enable 0", "waitMsec 500", "echo XR_FAULT_DESKTOP", "vr_status",
                 'screenshot "screenshots/xr_desktop.tga"']
    if args.mode == "MP":
        commands += ["openq4_reportMPState", "openq4_assertMPGameplayView"]
    commands += ["echo XR_FAULT_GAMEPLAY_END", "quit"]
    write_script(game / "xr_fault.cfg", commands)
    settings = {
        "fs_game": "baseoq4", "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save), "fs_devpath": str(save),
        "com_gameMode": args.mode, "si_gameType": "DM" if args.mode == "MP" else "singleplayer",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "480",
        "r_windowWidth": "640", "r_windowHeight": "480", "r_renderApi": args.render_api, "r_swapInterval": "0",
        "com_maxfps": "60", "ui_retained": "1", "ui_autoJoin": "1", "com_skipLoadingContinue": "1",
        "g_autoSkipCinematics": "1", "g_openQ4Storage1NoDropPodState": "1", "com_loadingContinueAutoAdvance": "1",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "vr_enable": "0", "vr_debug": "1",
        "vr_roomScale": "0", "vr_physicalCrouch": "0", "vr_aimMode": "0", "vr_hapticStrength": "0",
        # the captures compare gameplay before and after the fault: keep single
        # player running across the loss instead of pausing it on the menu
        "vr_pauseOnFocusLoss": "0",
        "g_autoExecAfterMapLoad": "xr_fault.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
        "net_port": str(args.port), "net_enableIPv6": "0",
    }
    if vulkan:
        settings.update(r_vkValidation="1" if args.validation else "0",
                        r_vkVRWindowlessTest="1" if args.windowless else "0")
    if args.mode == "MP":
        settings.update(net_allowCheats="1", sv_cheats="1", net_ip="127.0.0.1", net_serverDedicated="0",
                        net_LANServer="1", si_pure="0", si_maxPlayers="4", bot_minPlayers="0")
    command = [str(executable), *[part for key, value in settings.items() for part in ("+set", key, value)]]
    command += ["+spawnServer", "mp/q4dm1"] if args.mode == "MP" else ["+map", "game/storage1"]
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith("OPENQ4_XR_TEST_"):
            del environment[key]
    environment.update(XR_RUNTIME_JSON=str(manifest), OPENQ4_XR_TEST_LOG=str(xr_log), OPENQ4_XR_TEST_NO_INPUT="1",
                       OPENQ4_XR_TEST_EYE_SIZE="400x440", OPENQ4_XR_TEST_RATE="60",
                       OPENQ4_XR_TEST_FAULT_AFTER=str(arm), OPENQ4_XR_TEST_FAULT_ONCE=str(receipt))
    if args.fault != "none":
        environment["OPENQ4_XR_TEST_FAULT"] = args.fault
    if vulkan:
        environment["OPENQ4_XR_TEST_SCRIPT"] = str(output / "xr_script.txt")
    sources = ["src/sys/openxr/OpenXRSystem.cpp", "src/game/PlayerView.cpp", "src/mpgame/PlayerView.cpp",
               "src/renderer/RenderSystem_vr.cpp", "src/renderer/OpenGL/gl_VRPresentation.cpp",
               "src/renderer/Vulkan/vk_VRPresentation.cpp", "src/renderer/Vulkan/vk_GuiExecutor.cpp",
               "tools/tests/openxr/OpenXRTestRuntime.cpp",
               "tools/tests/openxr_lifecycle_runtime.py"]
    report = {"status": "fail", "mode": args.mode, "fault": args.fault, "creation_fault": args.creation_fault,
              "render_api": args.render_api, "validation": args.validation, "windowless": args.windowless,
              "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_sha256": {name: digest(ROOT / name) for name in sources},
              "command": command, "test_runtime": str(runtime_library), "captures": {}, "failures": [],
              "runtime_sha256": {(str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)): digest(path) for path in
                                 [executable, runtime_library, runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4",
                                  *sorted((runtime / "baseoq4").glob("game-*.dll" if os.name == "nt" else "game-*.so"))]},
              "input": "all test-runtime controller actions inactive; physical input disabled; no input injection",
              "capture": f"engine screenshot{'' if not vulkan else ' plus test-runtime eye captures'}; "
                         f"hidden 640x480 {args.render_api} window; simulated XR, no physical-headset claim"}
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    with (output / "console.log").open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=runtime, env=environment, startupinfo=startup, stdout=stream, stderr=subprocess.STDOUT)
        try:
            report["exit_code"] = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=20)
            report["exit_code"] = process.returncode
            report["failures"].append("engine timeout")
    log = read_log(game / "logs/openq4.log")
    events = [json.loads(line) for line in xr_log.read_text().splitlines() if line.strip()] if xr_log.exists() else []
    report["events"] = events
    if report["exit_code"] != 0:
        report["failures"].append("engine did not exit cleanly")
    if re.search(r"FATAL ERROR|Unknown command", log):
        report["failures"].append("engine reported a fatal error or unknown fixture command")
    if vulkan:
        if "OpenXR: the runtime created the Vulkan renderer's device" not in log:
            report["failures"].append("the XR runtime did not create the Vulkan renderer's device")
        ready = log.split("XR_FAULT_VULKAN_DEVICE", 1)[-1].split("XR_FAULT_BEFORE", 1)[0]
        if "running, focused" not in ready:
            report["failures"].append("the Vulkan session did not run on the runtime-made device before the fault")
        if args.validation:
            if "Vulkan: validation enabled" not in log:
                report["failures"].append("the Vulkan validation layer was not active")
            messages = [line for line in log.splitlines() if "VUID" in line or "Validation Error" in line]
            if messages:
                report["failures"].append("Vulkan validation reported: " + " | ".join(messages[:5]))
        if args.windowless and "Vulkan: a VR frame went on without the window's image" not in log:
            report["failures"].append("no VR frame opened without the window's image")
    for marker in ("XR_FAULT_GAMEPLAY_BEGIN", "XR_FAULT_AFTER", "XR_FAULT_RESTART", "XR_FAULT_DESKTOP", "XR_FAULT_GAMEPLAY_END"):
        if marker not in log:
            report["failures"].append("missing " + marker)
    faults = [e for e in events if e["event"] == "injected_fault"]
    if len(faults) != (0 if args.fault == "none" else 1):
        report["failures"].append("fault did not fire exactly once")
    for event in events:
        if event["event"] in ("invalid_handle", "call_after_loss", "end_frame_invalid", "haptic", "ownership_error"):
            report["failures"].append("runtime violation: " + json.dumps(event))
        if event["event"] == "destroy_instance" and event.get("live_handles", -1) != 0:
            report["failures"].append("instance leaked handles")
    if sum(e["event"] == "create_instance" for e in events) != sum(e["event"] == "destroy_instance" for e in events):
        report["failures"].append("unbalanced instances")
    if sum(e["event"] == "create_session" for e in events) != sum(e["event"] == "destroy_session" for e in events):
        report["failures"].append("unbalanced sessions")
    restart = log.split("XR_FAULT_RESTART", 1)[-1].split("XR_FAULT_DESKTOP", 1)[0]
    if "running, focused" not in restart:
        report["failures"].append("manual restart did not restore focused XR gameplay")
    if args.fault != "none" and not args.fault.endswith(":tracking"):
        fallback = log.split("XR_FAULT_AFTER", 1)[-1].split("XR_FAULT_AUTO", 1)[0]
        if "session: none" not in fallback:
            report["failures"].append("fault did not retire the session before fallback capture")
        automatic = log.split("XR_FAULT_AUTO", 1)[-1].split("XR_FAULT_RESTART", 1)[0].split("OpenXR: session closed", 1)[0]
        expected = "session: none" if args.fault.endswith((":instance", ":runtime")) else "running, focused"
        if expected not in automatic:
            report["failures"].append("automatic recovery policy did not match the fault scope")
    if args.fault.endswith(":tracking") and faults:
        after = events[events.index(faults[0]) + 1:]
        if not any(e["event"] == "end_frame" and e.get("layers") and
                   all(layer.get("type") != "projection" for layer in e["layers"]) for e in after):
            report["failures"].append("invalid tracking did not fall back to the virtual screen")
    if args.mode == "MP" and len(re.findall(r"^MP_PLAYER slot=0 [^\n]*ingame=1", log, re.M)) < 2:
        report["failures"].append("MP gameplay was not preserved")
    captures = {name: game / f"screenshots/xr_{name}.tga" for name in ("before", "fallback", "restart", "desktop")}
    if vulkan:
        captures.update(left=Path(f"{eye_prefix['restart']}_left.tga"), right=Path(f"{eye_prefix['restart']}_right.tga"),
                        after_capture=Path(f"{eye_prefix['after_capture']}_left.tga"))
    else:
        captures.update({name: game / f"screenshots/xr_{name}.tga" for name in ("left", "right", "after_capture")})
    for name, path in captures.items():
        if path.is_file():
            report["captures"][name] = {"path": str(path), "sha256": digest(path)}
        else:
            report["failures"].append("missing capture " + name)
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "fault", "exit_code", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
