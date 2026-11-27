#!/usr/bin/env python3
"""VR headset detection and VR Mode (roadmap VR-08), against the openQ4 OpenXR
test runtime (requires a staged client built with OpenXR and retail assets).

Detection has two stages. Stage one reads which OpenXR runtime the loader
would load without loading it; stage two asks that runtime for a headset,
and runs only when VR Mode is On or Automatic, or for vr_detect. The test
runtime is selected per process through XR_RUNTIME_JSON, so no system-wide
OpenXR registration is read or changed, and OPENQ4_XR_TEST_HEADSET makes it a
runtime with no headset ("absent") or one whose headset connects when a
marker file appears ("after:<file>"). Nothing synthesises input: console
commands drive the client, through a startup cfg at the title and
g_autoExecAfterMapLoad in a level.

Cases, each a fresh profile unless it says otherwise:
- no-runtime: XR_RUNTIME_JSON names a missing manifest. Stage one finds no
  runtime; the title does not ask; Automatic chosen at the title creates no
  instance; vr_detect says no runtime answered.
- offer-no: a runtime with no headset and VR Mode off. Stage one names the
  runtime and the runtime is never loaded (its log never opens: no
  instance, no xrGetSystem); the title asks once; NO keeps VR Mode off and is
  remembered. offer-again relaunches the same profile: the title does not
  ask again.
- offer-yes: a runtime with a headset and VR Mode off. YES makes VR Mode
  Automatic, which asks the runtime at once and presents to the headset;
  the first VR entry opens VR SETUP over the title and marks it shown.
- auto-late: Automatic, no headset at launch. The launch asks the runtime and
  settles on the desktop when no headset answers within its window; a
  headset connected later is offered (a notice) instead of taking over;
  vr_start takes it and the session presents.
- auto-launch: Automatic with a headset at launch and the automatic renderer
  choice (r_renderApi best). The headset is found before the renderer is
  chosen, so it is OpenGL, and a level plays in stereo.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]


def read_events(path: Path) -> list[dict]:
    if not path.is_file():
        return []
    events = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return events


class Runner:
    def __init__(self, options, staged: Path, executable: str, manifest: Path, output: Path):
        self.options, self.staged, self.executable, self.manifest, self.output = options, staged, executable, manifest, output
        launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
        launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
        self.base_args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]

    def run(self, name: str, settings: dict, commands: list[str], *, environment: dict, level: str | None = None,
            fresh: bool = True) -> tuple[str, list[dict], Path]:
        profile = self.output / name
        game = profile / "baseoq4"
        if fresh and profile.exists():
            shutil.rmtree(profile)
        game.mkdir(parents=True, exist_ok=True)
        for stale in game.glob("vr_marker_*.txt"):
            stale.unlink()
        xr_log = profile / "xr.jsonl"
        xr_log.unlink(missing_ok=True)
        log = game / "logs/openq4.log"
        log.unlink(missing_ok=True)
        cfg = f"openxr_vr_detection_{name}.cfg"
        (game / cfg).write_text("\n".join(commands + ["echo VR_DETECTION_SMOKE_COMPLETE", "quit"]) + "\n", encoding="utf-8")
        args = list(self.base_args)
        values = {
            "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
            "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
            "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720", "r_swapInterval": "0", "com_maxfps": "60",
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(self.options.basepath.resolve()), "fs_savepath": str(profile), "fs_devpath": str(profile),
            "com_allowConsole": "1", "com_skipLogoVideos": "1", "vr_debug": "1",
            "r_renderApi": "gl",
            **settings,
        }
        for key, value in values.items():
            args += ["+set", key, value]
        if level:
            args += ["+set", "g_autoExecAfterMapLoad", cfg, "+set", "g_autoExecAfterMapLoadDelayMs", "1000",
                     "+set", "g_autoSkipCinematics", "1", "+set", "com_skipLoadingContinue", "1",
                     "+set", "com_loadingContinueAutoAdvance", "1", "+map", level]
        else:
            args += ["+exec", cfg]
        command = [str(self.staged / self.executable), *args]
        (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        env = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_") and k != "XR_RUNTIME_JSON"}
        env.update({"OPENQ4_XR_TEST_LOG": str(xr_log), "OPENQ4_XR_TEST_EYE_SIZE": "800x880", "OPENQ4_XR_TEST_RATE": "90"})
        env.update(environment)
        startup = None
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
        with (profile / "process.log").open("w", encoding="utf-8") as output:
            try:
                result = subprocess.run(command, cwd=self.staged, stdout=output, stderr=subprocess.STDOUT, env=env,
                                        startupinfo=startup, timeout=self.options.timeout)
            except subprocess.TimeoutExpired:
                raise AssertionError(f"{name}: the client did not finish within {self.options.timeout} s")
        text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
        assert "VR_DETECTION_SMOKE_COMPLETE" in text, f"{name}: the script did not complete (see {log})"
        assert result.returncode == 0, f"{name}: exit {result.returncode}"
        assert not re.search(r"FATAL|ERROR:|Unknown command", text), f"{name}: errors in the log ({log})"
        print(f"  {name}: ok")
        return text, read_events(xr_log), profile


def marker(profile: Path, name: str) -> str:
    return str(profile / "baseoq4" / f"vr_marker_{name}.txt")


def after(text: str, token: str) -> str:
    assert token in text, f"missing {token!r}"
    return text.partition(token)[2]


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--cases", default="no-runtime,offer-no,offer-yes,auto-late,auto-launch")
    options = parser.parse_args(argv)
    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-detection").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    runner = Runner(options, staged, executable, manifest, output)
    cases = set(options.cases.split(","))
    print("OpenXR VR detection smoke:")
    off = {"vr_enable": "0", "vr_offerAnswered": "0", "vr_setupDone": "0"}

    if "no-runtime" in cases:
        missing = output / "no-runtime-manifest.json"
        text, events, _ = runner.run("no-runtime", off, [
            "disconnect", "waitMsec 4000", "vr_offer status", "vr_enable 2", "waitMsec 1500", "vr_status",
            "vr_detect", "waitMsec 1500", "echo VR_NO_RUNTIME_END"],
            environment={"XR_RUNTIME_JSON": str(missing)})
        assert "VR_RUNTIME source=none (XR_RUNTIME_JSON names a manifest that cannot be read)" in text, "stage one found a runtime"
        assert re.search(r'VR_OFFER skip reason="no OpenXR runtime is installed"', text), "the title asked with no runtime"
        assert "VR_OFFER show" not in text
        assert "VR_MODE mode=automatic" in text, "choosing Automatic was not applied"
        assert "VR_DETECT runtime=none headset=no" in text, "vr_detect did not answer that no runtime answered"
        assert not events, "a missing runtime still logged events"

    if "offer-no" in cases:
        environment = {"XR_RUNTIME_JSON": str(manifest), "OPENQ4_XR_TEST_HEADSET": "absent"}
        text, events, profile = runner.run("offer", off, [
            "disconnect", "waitMsec 5000", "vr_offer status", "vr_offer no", "waitMsec 500", "vr_offer status", "vr_status"],
            environment=environment)
        assert re.search(r'VR_RUNTIME source=XR_RUNTIME_JSON name="[^"]+" manifest=', text), "stage one did not name the test runtime"
        assert "VR_MODE boot mode=off" in text
        assert text.count("VR_OFFER show") == 1, "the title did not ask exactly once"
        assert "VR_OFFER answer=no by=console mode=0" in text, "NO was not applied"
        status = after(text, "VR_OFFER answer=no")
        assert "VR_OFFER status due=0" in status and "answered=1" in status, "the answer was not kept"
        assert not events and not (profile / "xr.jsonl").exists(), \
            "with VR Mode off the runtime was loaded (stage one must not create an instance)"
        # the answer outlives the run: the next launch does not ask
        text, events, _ = runner.run("offer", {"vr_setupDone": "0"}, ["disconnect", "waitMsec 4000", "vr_offer status"],
                                     environment=environment, fresh=False)
        assert 'VR_OFFER skip reason="answered"' in text and "VR_OFFER show" not in text, "the title asked again after NO"
        assert not events, "the relaunch loaded the runtime with VR Mode off"
        print("  offer-again: ok")

    if "offer-yes" in cases:
        text, events, _ = runner.run("offer-yes", off, [
            "disconnect", "waitMsec 5000", "vr_offer yes", "waitMsec 8000", "vr_status", "echo VR_SETUP_DONE_IS", "vr_setupDone"],
            environment={"XR_RUNTIME_JSON": str(manifest)})
        assert "VR_OFFER answer=yes by=console mode=2" in text, "YES did not make VR Mode Automatic"
        rest = after(text, "VR_OFFER answer=yes")
        assert "VR_MODE mode=automatic" in rest and re.search(r'VR_MODE decision=vr headset="openQ4 simulated headset"', rest), \
            "Automatic did not find the headset after YES"
        assert "OpenXR: presenting to the headset" in rest, "YES did not start VR"
        assert "VR_SETUP shown reason=first-entry" in rest, "the first VR entry did not open VR SETUP"
        assert re.search(r'"vr_setupDone" is:"1"', after(text, "VR_SETUP_DONE_IS")), "VR SETUP was not marked shown"
        assert any(e.get("event") == "create_instance" for e in events)

    if "auto-late" in cases:
        name = "auto-late"
        profile = output / name
        connect = marker(profile, "connect")
        text, events, _ = runner.run(name, {"vr_enable": "2", "vr_offerAnswered": "1", "vr_setupDone": "1"}, [
            "disconnect", "waitMsec 24000", "echo VR_LATE_BEFORE", "vr_status", "condump vr_marker_connect.txt", "waitMsec 6000",
            "echo VR_LATE_CONNECTED", "vr_status", "vr_start", "waitMsec 8000", "echo VR_LATE_TAKEN", "vr_status"],
            environment={"XR_RUNTIME_JSON": str(manifest), "OPENQ4_XR_TEST_HEADSET": f"after:{connect}"})
        assert "VR_MODE boot mode=automatic" in text
        before = text.partition("VR_LATE_BEFORE")[0]
        assert "VR_MODE decision=desktop reason=no-headset" in before, "Automatic did not settle on the desktop without a headset"
        assert "presenting to the headset" not in before
        connected = after(text, "VR_LATE_CONNECTED").partition("VR_LATE_TAKEN")[0]
        offered = text.partition("VR_LATE_CONNECTED")[0]
        assert re.search(r'VR_MODE offer headset="openQ4 simulated headset"', offered), "the late headset was not offered"
        assert "VR: a headset is offered in the menus" in offered, "the session raised no notice"
        assert "presenting to the headset" not in offered, "a late headset took over without the player"
        assert "a headset is offered (vr_start)" in connected
        taken = after(text, "VR_LATE_TAKEN")
        rest = text.partition("VR_LATE_TAKEN")[0].partition("VR_LATE_CONNECTED")[2]
        assert "VR_MODE decision=vr reason=offer-taken" in rest and "OpenXR: presenting to the headset" in rest, \
            "vr_start did not take the offered headset"
        assert "VR Mode: automatic, playing in VR" in taken
        answers = [e.get("headset") for e in events if e.get("event") == "get_system"]
        assert answers and answers[0] is False and True in answers, f"the runtime was not asked before and after: {answers}"

    if "auto-launch" in cases:
        text, events, _ = runner.run("auto-launch", {"vr_enable": "2", "vr_offerAnswered": "1", "vr_setupDone": "1",
                                                     "r_renderApi": "best", "vr_aimMode": "1"},
                                     ["waitMsec 3000", "echo VR_AUTO_LAUNCH_RENDERER", "r_actualRenderApi", "vr_status"],
                                     environment={"XR_RUNTIME_JSON": str(manifest)}, level="game/airdefense1")
        boot = text.partition("Loading renderer module")[0]
        assert re.search(r'VR_MODE decision=vr headset="openQ4 simulated headset"', boot), \
            "Automatic did not find the headset before the renderer was chosen"
        assert re.search(r'"r_actualRenderApi" is:"gl"', after(text, "VR_AUTO_LAUNCH_RENDERER")), \
            "the automatic renderer choice did not keep VR on OpenGL"
        assert "OpenXR: presenting to the headset" in text
        frames = [e for e in events if e.get("event") == "end_frame" and e.get("layers")]
        assert any([layer.get("type") for layer in f["layers"]] == ["projection", "quad"] for f in frames), \
            "the level never presented a stereo projection layer with the HUD"
    print(f"OpenXR VR detection smoke: PASS ({', '.join(sorted(cases))}); evidence={output}")


if __name__ == "__main__":
    main()
