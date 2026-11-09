#!/usr/bin/env python3
"""Run silent SP/save, MP/touch, SP rotation, lifecycle or MP resume diagnostics on an isolated Android emulator.

Requires a booted AOSP/userdebug AVD and the separately verified emulator APK.
No OS pointer/keyboard input or OS screen capture is used. Retail files are
copied from the caller's installation; the APK contains no retail assets.

--mode lifecycle applies a touch layout editor record through the setup
preferences, plays, sends the app to the background through the activity
manager (the Home intent, not input), requires the "Suspended" save, ends the
backgrounded process with `am kill` as Android's memory management would,
relaunches it and loads that save. --mode resume joins the dedicated server
at --server, stays in the background for --away seconds and returns, then
requires the connection to survive (a stay shorter than the time-out) or the
client to reconnect (a longer one).
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/build"))
from verify_android_apk import verify_payload, verify_tools

PACKAGE = "com.darkmatter.openq4.emulator"
SAVE_ROOT = "files/saves/baseoq4"
SLOT = "AndroidEmulatorSmoke"
CAPTURES = ("emulator-spawn.tga", "emulator-moved.tga", "emulator-restored.tga")
ROTATE_CAPTURES = ("emulator-spawn.tga", "emulator-portrait.tga", "emulator-moved.tga", "emulator-restored.tga")
# Rotation mode applies Interface setup's "Landscape and portrait" (version 2
# record) and turns the AVD through its rotation setting, never through input.
SETUP_PREFS = "shared_prefs/setup.xml"
ROTATE_INTERFACE = "2:0a4d0e2f5c6b7a8998a7b6c5d4e3f201:0:0:0:1"
# Lifecycle mode: the layout editor's applied record, as Apply writes it -
# Fire moved 160 dp from the right and 120 dp from the bottom at 125%, Jump
# hidden, Zoom faded to 20%; portrait untouched. The engine reads it with the
# touch settings at foreground entry.
SUSPEND_SLOT = "Suspended"
UNTOUCHED = "*,*,100,35,0"
LIFECYCLE_TOUCH = "1:0:10:5"
LIFECYCLE_LAYOUT = ("1|" + ";".join(["-160,-120,125,35,0", "*,*,100,35,1", UNTOUCHED, UNTOUCHED, UNTOUCHED,
                                     "*,*,100,20,0", UNTOUCHED, UNTOUCHED, UNTOUCHED])
                    + "|" + ";".join([UNTOUCHED] * 9))
LIFECYCLE_CAPTURES = ("emulator-spawn.tga", "emulator-moved.tga", "emulator-restored.tga")
LIFECYCLE_TAIL = """echo EMULATOR_BACKGROUND_REQUEST
waitMsec 600000
"""
RESUMED = """echo EMULATOR_SUSPEND_RESTORED
waitMsec 1000
viewpos
touchControlsTest layout
screenshot "screenshots/emulator-restored.tga"
echo EMULATOR_SMOKE_COMPLETE
quit
"""
# Runs after each map load: the join, and again after a reconnect, which the
# harness counts. Re-arming g_autoExecAfterMapLoad from here would fire at once.
RESUME_GAMEPLAY = """waitMsec 2000
echo EMULATOR_GAMEPLAY_READY
getviewpos
echo EMULATOR_BACKGROUND_REQUEST
"""
# Portrait thumb deck (section 13.6): the stick in the deck's leading half,
# a look drag in the view above it, Fire by name.
ROTATE_GAMEPLAY = f"""god
notarget
give all
waitMsec 2000
echo EMULATOR_GAMEPLAY_READY
viewpos
touchControlsTest layout
screenshot "screenshots/emulator-spawn.tga"
echo EMULATOR_ROTATE_PORTRAIT
waitMsec 12000
echo EMULATOR_PORTRAIT_READY
viewpos
touchControlsTest layout
touchControlsTest state
screenshot "screenshots/emulator-portrait.tga"
touchControlsTest down 1 0.15 0.88
touchControlsTest move 1 0.15 0.80
touchControlsTest down 2 fire
touchControlsTest down 3 0.45 0.25
touchControlsTest move 3 0.55 0.27
waitMsec 1500
touchControlsTest cancel 1 0.15 0.80
touchControlsTest cancel 2 fire
touchControlsTest up 3 0.55 0.27
waitMsec 2000
wait 60
echo EMULATOR_TOUCH_RELEASED
viewpos
touchControlsTest state
screenshot "screenshots/emulator-moved.tga"
waitMsec 1000
echo EMULATOR_TOUCH_NEUTRAL
viewpos
echo EMULATOR_ROTATE_LANDSCAPE
waitMsec 12000
echo EMULATOR_LANDSCAPE_RESTORED
viewpos
touchControlsTest layout
saveGame {SLOT}
waitMsec 700
set g_autoExecAfterMapLoad emulator_restored.cfg
loadGame {SLOT}
"""
SCRIPTS = {
    "autoexec.cfg": """set r_fullscreen 0
set r_borderless 0
set r_borderlessDefaultMigrated 1
set r_mode -1
set r_customWidth 960
set r_customHeight 540
set com_machineSpec 0
set image_downSize 1
set image_downSizeLimit 256
set image_downSizeBump 1
set image_downSizeBumpLimit 256
set image_downSizeSpecular 1
set image_downSizeSpecularLimit 128
set com_maxfps 60
set ui_retained 1
set ui_autoJoin 0
set in_mouse 0
set in_joystick 0
set in_tty 0
set in_touchscreen 1
set in_touchControls 1
set s_noSound 1
set developer 1
set com_skipLoadingContinue 1
set g_autoExecAfterMapLoad emulator_smoke.cfg
set g_autoExecAfterMapLoadDelayMs 500
wait 10
devmap game/hangar1
""",
    "emulator_smoke.cfg": f"""god
notarget
give all
waitMsec 2000
echo EMULATOR_GAMEPLAY_READY
viewpos
touchControlsTest layout
touchControlsTest state
screenshot "screenshots/emulator-spawn.tga"
touchControlsTest down 1 0.15 0.76
touchControlsTest move 1 0.15 0.60
touchControlsTest down 2 fire
touchControlsTest down 3 0.51 0.23
touchControlsTest move 3 0.57 0.25
waitMsec 1500
touchControlsTest cancel 1 0.15 0.60
touchControlsTest cancel 2 fire
touchControlsTest up 3 0.57 0.25
waitMsec 2000
wait 60
echo EMULATOR_TOUCH_RELEASED
viewpos
touchControlsTest state
screenshot "screenshots/emulator-moved.tga"
waitMsec 1000
echo EMULATOR_TOUCH_NEUTRAL
viewpos
touchControlsTest state
saveGame {SLOT}
waitMsec 700
set g_autoExecAfterMapLoad emulator_restored.cfg
loadGame {SLOT}
""",
    "emulator_restored.cfg": """echo EMULATOR_SAVE_RESTORED
waitMsec 1000
viewpos
screenshot "screenshots/emulator-restored.tga"
echo EMULATOR_SMOKE_COMPLETE
quit
""",
}


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def scripts_for_mode(mode: str, server: str | None = None) -> dict[str, str]:
    scripts = dict(SCRIPTS)
    if mode == "lifecycle":
        scripts.pop("emulator_restored.cfg")
        scripts["emulator_smoke.cfg"] = scripts["emulator_smoke.cfg"].split(f"saveGame {SLOT}", 1)[0] + LIFECYCLE_TAIL
    elif mode == "resume":
        if not server or not re.fullmatch(r"\d{1,3}(?:\.\d{1,3}){3}:\d{2,5}", server):
            raise ValueError("Resume mode needs --server <IPv4>:<port>")
        scripts.pop("emulator_restored.cfg")
        scripts["autoexec.cfg"] = scripts["autoexec.cfg"].replace(
            "devmap game/hangar1", f"""set net_enableIPv6 0
connect {server}""")
        scripts["emulator_smoke.cfg"] = RESUME_GAMEPLAY
    elif mode == "mp":
        scripts.pop("emulator_restored.cfg")
        scripts["autoexec.cfg"] = scripts["autoexec.cfg"].replace(
            "devmap game/hangar1", """set ui_autoJoin 1
set ui_spectate Play
set net_ip 127.0.0.1
set net_enableIPv6 0
set net_port 28240
set net_allowCheats 1
set si_pure 0
spawnServer mp/q4dm1""")
        scripts["emulator_smoke.cfg"] = scripts["emulator_smoke.cfg"].split(f"saveGame {SLOT}", 1)[0] + """screenshot "screenshots/emulator-neutral.tga"
echo EMULATOR_SMOKE_COMPLETE
quit
"""
        scripts["emulator_smoke.cfg"] = scripts["emulator_smoke.cfg"].replace("\nviewpos\n", "\ngetviewpos\n")
        # MP spawn selection is random. Use the recorded floor-level Q4DM1
        # start so subsequent runs measure the same corridor and collision.
        scripts["emulator_smoke.cfg"] = scripts["emulator_smoke.cfg"].replace(
            "\nwaitMsec 2000\necho EMULATOR_GAMEPLAY_READY",
            "\nsetviewpos -3336 3760 260.25 90\nwaitMsec 2000\necho EMULATOR_GAMEPLAY_READY",
        )
    elif mode == "rotate":
        scripts["emulator_smoke.cfg"] = ROTATE_GAMEPLAY
    elif mode != "sp":
        raise ValueError("Unknown emulator game mode")
    scripts["emulator_smoke.cfg"] = "com_activeGameModule\n" + scripts["emulator_smoke.cfg"]
    return scripts


def lifecycle_relaunch_autoexec() -> str:
    """The second launch's autoexec: the same settings, then CONTINUE's load of the suspension save."""
    return SCRIPTS["autoexec.cfg"].replace("set g_autoExecAfterMapLoad emulator_smoke.cfg",
                                           "set g_autoExecAfterMapLoad emulator_resumed.cfg").replace(
        "devmap game/hangar1", f"loadGame {SUSPEND_SLOT}")


def layout_after_marker(log: str, marker: str) -> dict:
    """The touchControlsTest layout printed after an engine marker."""
    block = log.split("EMULATOR_" + marker, 1)
    if len(block) != 2:
        raise ValueError(f"Missing layout marker: {marker}")
    block = block[1].split("EMULATOR_", 1)[0]
    size = re.search(r"touch controls: (\w+), (\d+)x(\d+) px", block)
    if not size:
        raise ValueError(f"No touch layout after {marker}")
    controls = {name: (float(x), float(y)) for name, x, y in
                re.findall(r"(?m)^\s+(\w+)\s+at ([-0-9.]+) ([-0-9.]+), radius", block)}
    radii = {name: float(r) for name, r in re.findall(r"(?m)^\s+(\w+)\s+at [-0-9.]+ [-0-9.]+, radius ([-0-9.]+)", block)}
    opacity = {name: float(o) for name, o in re.findall(r"(?m)^\s+(\w+)\s+at .*, opacity ([-0-9.]+)", block)}
    hidden = sorted(re.findall(r"(?m)^\s+(\w+)\s+at .*, hidden$", block))
    dp = re.search(r"px, ([0-9.]+) px per dp(, edited|, edits rejected)?", block)
    return {"state": size.group(1), "width": int(size.group(2)), "height": int(size.group(3)), "controls": controls,
            "radii": radii, "opacity": opacity, "hidden": hidden, "dp": float(dp.group(1)) if dp else None,
            "edited": bool(dp and dp.group(2) == ", edited")}


def pose_after_marker(log: str, marker: str, mode: str = "sp") -> list[float]:
    section = log.split("EMULATOR_" + marker, 1)
    if len(section) != 2:
        raise ValueError(f"Missing view marker: {marker}")
    # Map/renderer messages can arrive between the marker and viewpos after
    # restoring. Never borrow a pose from the following measurement, though.
    block = section[1].split("EMULATOR_", 1)[0]
    pattern = (r"(?m)^origin: \(([^)]+)\) angles: \(([^)]+)\)" if mode != "mp"
               else r"(?m)^\(([^)]+)\) ([-+0-9.eE]+)\s*$")
    matches = re.findall(pattern, block)
    if len(matches) != 1:
        raise ValueError(f"Expected one view sample at {marker}, found {len(matches)}")
    pose = [float(value) for group in matches[0] for value in group.split()]
    if len(pose) != (4 if mode == "mp" else 6) or not all(math.isfinite(v) for v in pose):
        raise ValueError(f"Invalid view sample at {marker}")
    return pose


class Probe:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.out = args.output.resolve()
        if not self.out.is_relative_to((ROOT / ".tmp").resolve()):
            raise ValueError("Evidence must stay under this checkout's .tmp directory")
        self.out.mkdir(parents=True, exist_ok=False)
        (self.out / "runner.py").write_bytes(Path(__file__).read_bytes())
        self.pid = None
        self.mode = args.mode
        self.scripts = scripts_for_mode(self.mode, getattr(args, "server", None))
        self.captures = {"sp": CAPTURES, "rotate": ROTATE_CAPTURES, "lifecycle": LIFECYCLE_CAPTURES,
                         "resume": ()}.get(self.mode, (*CAPTURES[:2], "emulator-neutral.tga"))
        slot = SUSPEND_SLOT if self.mode == "lifecycle" else SLOT
        self.save_names = (tuple(f"savegames/{slot}.{ext}" for ext in ("save", "txt", "tga"))
                           if self.mode in ("sp", "rotate", "lifecycle") else ())
        self.pids = []
        self.lifecycle = {}
        self.report = {"status": "running", "serial": args.serial, "package": PACKAGE, "mode": self.mode,
                       "capture_method": "engine screenshot", "audio": "disabled",
                       "input": "engine diagnostic commands; no OS input",
                       "launch": "internal native Activity from rooted diagnostic AVD",
                       "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
        self.created = {}
        self.before_config = None
        self.prepared = False
        self.before_prefs = None
        self.prefs_written = None
        self.rotation = None
        self.rotated = []

    def write_report(self):
        (self.out / "report.json").write_text(json.dumps(self.report, indent=2) + "\n", encoding="utf-8")

    def adb(self, *arguments: str, check: bool = True, timeout: int = 30,
            input_data: bytes | None = None) -> subprocess.CompletedProcess:
        result = subprocess.run([str(self.args.adb), "-s", self.args.serial, *arguments],
                                capture_output=True, input=input_data, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError(f"adb {arguments!r}: {result.stdout.decode(errors='replace')}"
                               f"{result.stderr.decode(errors='replace')}")
        return result

    def path(self, name: str) -> str:
        if not re.fullmatch(r"[A-Za-z0-9_.\-/]+", name) or ".." in name.split("/") or name.startswith("/"):
            raise ValueError("Unexpected diagnostic path")
        path = SAVE_ROOT + "/" + name
        # Refuse links at any path component before reading/removing test data.
        parts = path.split("/")
        for index in range(1, len(parts) + 1):
            candidate = "/".join(parts[:index])
            if self.private_test("-L", candidate):
                raise ValueError(f"Linked diagnostic path: {candidate}")
        return path

    def private_test(self, flag: str, path: str) -> bool:
        if flag not in ("-L", "-f") or not re.fullmatch(r"[A-Za-z0-9_.\-/]+", path):
            raise ValueError("Unexpected private-file test")
        # Android 7 can run the shell builtin but cannot exec a standalone
        # `test` under run-as. A command error must never mean "file absent".
        result = self.adb("shell", "run-as", PACKAGE, "sh", "-c", f"'test {flag} {path}'", check=False)
        if result.returncode not in (0, 1) or result.stdout.strip() or result.stderr.strip():
            raise RuntimeError("Private-file test failed: " + (result.stdout + result.stderr).decode(errors="replace"))
        return result.returncode == 0

    def read(self, name: str) -> bytes | None:
        path = self.path(name)
        if not self.private_test("-f", path):
            return None
        return self.adb("exec-out", "run-as", PACKAGE, "cat", path).stdout

    def put(self, name: str, data: bytes):
        local = self.out / ("input-" + name.replace("/", "-"))
        local.write_bytes(data)
        # exec-in uses binary stdin on Windows; ordinary shell stdin still
        # translates CRLF there, even with -T. It also escapes each argument,
        # unlike adb shell, so do not add another layer of quotes.
        self.adb("exec-in", "run-as", PACKAGE, "sh", "-c",
                 f"cat > {self.path(name)}", input_data=data)
        actual = self.read(name)
        for attempt in range(2):
            if actual is not None:
                break
            self.report.setdefault("script_visibility_retries", []).append({"file": name, "attempt": attempt + 1})
            time.sleep(1)
            actual = self.read(name)
        if actual != data:
            if actual is not None:
                (self.out / ("failed-write-" + name.replace("/", "-"))).write_bytes(actual)
            raise ValueError(f"Device write mismatch: {name}; expected {len(data)} bytes, "
                             f"actual {None if actual is None else len(actual)}")

    def prefs_path(self) -> str:
        for candidate in ("shared_prefs", SETUP_PREFS):
            if self.private_test("-L", candidate):
                raise ValueError(f"Linked preferences path: {candidate}")
        return SETUP_PREFS

    def read_prefs(self) -> bytes | None:
        path = self.prefs_path()
        return self.adb("exec-out", "run-as", PACKAGE, "cat", path).stdout if self.private_test("-f", path) else None

    def apply_rotation_setup(self):
        """Record the AVD's rotation and the app's setup preferences, then select portrait-capable play."""
        values = {}
        for key in ("accelerometer_rotation", "user_rotation"):
            values[key] = self.adb("shell", "settings", "get", "system", key).stdout.decode().strip()
            if not re.fullmatch(r"\d+|null", values[key]):
                raise ValueError(f"Unexpected rotation setting {key}: {values[key]}")
        self.rotation = values
        self.report["original_rotation"] = values
        self.set_rotation(0)
        self.adb("shell", "settings", "put", "system", "accelerometer_rotation", "0")
        self.write_setup({"interface_settings": ROTATE_INTERFACE})
        self.report["interface_record"] = ROTATE_INTERFACE

    def write_setup(self, entries: dict[str, str]):
        """Replace these setup preference strings, keeping the original for restoration."""
        self.before_prefs = self.read_prefs()
        if self.before_prefs is not None:
            (self.out / "before-setup.xml").write_bytes(self.before_prefs)
            root = ET.fromstring(self.before_prefs)
        else:
            root = ET.Element("map")
        for entry in [e for e in root if e.get("name") in entries]:
            root.remove(entry)
        for name, value in entries.items():
            ET.SubElement(root, "string", name=name).text = value
        data = b"<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n" + ET.tostring(root, encoding="unicode").encode()
        self.prefs_written = data
        self.adb("shell", "run-as", PACKAGE, "mkdir", "-p", "shared_prefs")
        self.adb("exec-in", "run-as", PACKAGE, "sh", "-c", f"cat > {self.prefs_path()}", input_data=data)
        if self.read_prefs() != data:
            raise ValueError("Setup preferences write mismatch")

    def go_home(self):
        """Background the app with the activity manager's Home intent; no input reaches the device."""
        result = self.adb("shell", "am", "start", "-W", "-a", "android.intent.action.MAIN",
                          "-c", "android.intent.category.HOME", timeout=60)
        return datetime.datetime.now(datetime.timezone.utc).isoformat(), result

    def wait_for_pid(self, present: bool, seconds: int) -> str:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            pid = self.adb("shell", "pidof", PACKAGE, check=False).stdout.decode().strip()
            if bool(pid) == present:
                return pid
            time.sleep(1)
        raise TimeoutError(f"Application process {'did not start' if present else 'did not end'}")

    def suspend_and_kill(self):
        """Lifecycle: background, require the suspension save, end the process as the system would, relaunch."""
        at, _ = self.go_home()
        self.lifecycle["backgrounded_utc"] = at
        deadline = time.monotonic() + 60
        save = None
        while time.monotonic() < deadline:
            log = self.read("logs/openq4.log") or b""
            save = self.read(f"savegames/{SUSPEND_SLOT}.save")
            if b"Saving 'Suspended' as the app goes to the background" in log and save:
                break
            time.sleep(1)
        else:
            raise ValueError("No suspension save after the app went to the background")
        (self.out / "suspended-before-kill.log").write_bytes(log)
        self.lifecycle["suspension_save_sha256"] = hashlib.sha256(save).hexdigest()
        self.lifecycle["suspension_save_bytes"] = len(save)
        pid = self.adb("shell", "pidof", PACKAGE, check=False).stdout.decode().strip()
        if pid != self.pid:
            raise ValueError("The backgrounded process changed before it was ended")
        state = self.adb("shell", "dumpsys", "activity", "processes", PACKAGE, check=False).stdout.decode(errors="replace")
        (self.out / "background-process-state.txt").write_text(state, encoding="utf-8")
        # As Android's low-memory killer would: the app is told nothing more.
        self.adb("shell", "am", "kill", PACKAGE)
        self.wait_for_pid(False, 30)
        self.lifecycle["killed_pid"] = self.pid
        self.lifecycle["killed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        relaunch = lifecycle_relaunch_autoexec()
        self.created["autoexec.cfg"] = relaunch.encode()
        self.put("autoexec.cfg", relaunch.encode())
        self.created["emulator_resumed.cfg"] = RESUMED.encode()
        self.put("emulator_resumed.cfg", RESUMED.encode())
        self.report["created_script_sha256"] = {n: hashlib.sha256(d).hexdigest() for n, d in self.created.items()}
        result = self.adb("shell", "am", "start", "-W", "-n", PACKAGE + "/com.darkmatter.openq4.OpenQ4Activity",
                          "--ez", "multiplayer", "false", timeout=60)
        (self.out / "relaunch.log").write_bytes(result.stdout + result.stderr)
        self.pid = None

    def away_and_return(self):
        """Resume: stay in the background for --away seconds, then bring the same process back."""
        at, _ = self.go_home()
        self.lifecycle["backgrounded_utc"] = at
        time.sleep(self.args.away)
        result = self.adb("shell", "am", "start", "-W", "-n", PACKAGE + "/com.darkmatter.openq4.OpenQ4Activity", timeout=60)
        (self.out / "return.log").write_bytes(result.stdout + result.stderr)
        self.lifecycle["returned_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        self.lifecycle["away_seconds"] = self.args.away

    def set_rotation(self, rotation: int):
        self.adb("shell", "settings", "put", "system", "user_rotation", str(rotation))
        self.rotated.append({"user_rotation": rotation, "at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()})

    def restore_rotation_setup(self):
        if self.prefs_written is not None:
            current = self.read_prefs()
            if current != self.prefs_written:
                (self.out / "after-setup.xml").write_bytes(current or b"")
                raise ValueError("Setup preferences changed during the run; refusing to restore")
            if self.before_prefs is not None:
                self.adb("exec-in", "run-as", PACKAGE, "sh", "-c", f"cat > {self.prefs_path()}", input_data=self.before_prefs)
            else:
                self.adb("shell", "run-as", PACKAGE, "rm", self.prefs_path())
            self.report["original_setup_restored"] = True
        if self.rotation is not None:
            for key, value in self.rotation.items():
                if value == "null":
                    self.adb("shell", "settings", "delete", "system", key)
                else:
                    self.adb("shell", "settings", "put", "system", key, value)
            self.report["original_rotation_restored"] = True

    def remote_sha(self, remote: str) -> str | None:
        exists = self.adb("shell", "test", "-f", remote, check=False)
        if exists.returncode not in (0, 1) or exists.stdout.strip() or exists.stderr.strip():
            raise RuntimeError("Retail-file test failed: " + (exists.stdout + exists.stderr).decode(errors="replace"))
        if exists.returncode:
            return None
        result = self.adb("shell", "sha256sum", remote, check=False, timeout=60)
        values = result.stdout.decode(errors="replace").split()
        if result.returncode == 0 and values and re.fullmatch(r"[a-f0-9]{64}", values[0]):
            return values[0]
        # API 24's system image has no sha256sum. Verify the same bytes on the
        # host without retaining another retail copy or buffering a whole PK4.
        temporary = self.out / "retail-verification-readback.pk4"
        if temporary.exists() or temporary.is_symlink():
            raise ValueError("Unexpected existing retail readback file")
        try:
            self.adb("pull", remote, str(temporary), timeout=180)
            return sha(temporary)
        finally:
            if temporary.is_symlink() or not temporary.resolve().is_relative_to(self.out):
                raise ValueError("Retail readback path changed")
            temporary.unlink(missing_ok=True)

    def collect(self):
        for name in ("logs/openq4.log", "openQ4Config.cfg"):
            data = self.read(name)
            if data is not None:
                (self.out / Path(name).name).write_bytes(data)
        for name in self.captures:
            data = self.read("screenshots/" + name)
            if data is not None:
                (self.out / name).write_bytes(data)
        for name in ("logcat", "exit-info"):
            arguments = ("logcat", "-d") if name == "logcat" else ("shell", "dumpsys", "activity", "exit-info", PACKAGE)
            result = self.adb(*arguments, check=False)
            (self.out / (name + ".txt")).write_bytes(result.stdout + result.stderr)

    def run(self):
        if self.adb("shell", "getprop", "ro.kernel.qemu").stdout.strip() != b"1":
            raise ValueError("Refusing to run on a physical device")
        if self.adb("shell", "getprop", "sys.boot_completed").stdout.strip() != b"1":
            raise ValueError("Emulator has not completed boot")
        if self.adb("shell", "pidof", PACKAGE, check=False).stdout.strip():
            raise ValueError("Diagnostic application is already running")
        # Root is required to launch the internal Activity on this isolated AVD.
        # It also permits capability queries denied to shell on older images.
        self.adb("root")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            result = self.adb("shell", "id", check=False, timeout=5)
            if b"uid=0(" in result.stdout:
                break
            time.sleep(1)
        else:
            raise ValueError("A rooted AOSP/userdebug emulator is required")
        for prop in ("ro.build.version.sdk", "ro.build.version.release", "ro.build.fingerprint",
                     "ro.product.cpu.abi", "ro.opengles.version"):
            self.report[prop] = self.adb("shell", "getprop", prop).stdout.decode().strip()
        if self.report["ro.product.cpu.abi"] != "x86_64":
            raise ValueError("Expected the diagnostic x86_64 AVD")
        if int(self.report["ro.opengles.version"] or "0") < 0x30000:
            raise ValueError("The emulator image/renderer does not advertise required OpenGL ES 3.0")
        verify_tools(self.args.apk, self.args.build_tools, self.args.certificate, self.args.version, "emulator")
        verify_payload(self.args.apk, self.args.stage, self.args.version, "emulator")
        self.report["apk_sha256"] = sha(self.args.apk)
        self.report["native_sha256"] = {p.name: sha(p) for p in sorted((self.args.stage / "lib").glob("*.so"))}
        self.report["source_head"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        names = subprocess.check_output(["git", "ls-files", "-m", "-o", "--exclude-standard"], cwd=ROOT, text=True).splitlines()
        self.report["changed_source_sha256"] = {n: sha(ROOT / n) for n in names if (ROOT / n).is_file()}
        self.write_report()
        installed = self.adb("shell", "pm", "path", PACKAGE, check=False).stdout.decode().strip()
        match = re.fullmatch(r"package:(/data/app/[A-Za-z0-9_~./+=-]+/base\.apk)", installed)
        if match and self.remote_sha(match.group(1)) == self.report["apk_sha256"]:
            self.report["installation"] = "retained identical installed APK (full SHA-256 match)"
            (self.out / "install.log").write_text(self.report["installation"] + "\n")
            print("Verified identical installed APK; keeping app data.", flush=True)
        else:
            print("Verified emulator APK; installing without clearing app data.", flush=True)
            result = self.adb("install", "-r", "--no-streaming", str(self.args.apk), timeout=180)
            (self.out / "install.log").write_bytes(result.stdout + result.stderr)
            self.report["installation"] = "adb install -r"
        uid = self.adb("shell", "run-as", PACKAGE, "id", "-u").stdout.decode().strip()
        if not re.fullmatch(r"1\d{4}", uid):
            raise ValueError("Expected an application UID in the emulator's primary user")
        retail = f"/sdcard/Android/data/{PACKAGE}/files/q4base"
        self.adb("shell", "mkdir", "-p", retail)
        # Root adbd otherwise creates an inaccessible root-owned 2770 directory
        # on recent Android FUSE. Give only this app's test copies to its UID.
        self.adb("shell", "chown", uid, retail)
        self.report["retail_owner_uid"] = int(uid)
        self.report["retail_sha256"] = {}
        assets = [p for p in sorted(self.args.assets.iterdir()) if p.is_file()
                  and re.fullmatch(r"(?:pak\d{3}|zpak_english(?:_\d{2})?)\.pk4", p.name)]
        if not assets or not (self.args.assets / "pak001.pk4").is_file():
            raise ValueError("Expected an installed retail q4base directory")
        for index, path in enumerate(assets, 1):
            checksum = sha(path)
            remote = retail + "/" + path.name
            if self.remote_sha(remote) != checksum:
                self.adb("push", str(path), remote, timeout=180)
                actual = self.remote_sha(remote)
                # Older emulated external storage can briefly retain a
                # negative stat result across adbd's completed write. Retry
                # absence only; an actual checksum mismatch always fails.
                for attempt in range(2):
                    if actual is not None:
                        break
                    self.report.setdefault("retail_visibility_retries", []).append(
                        {"file": path.name, "attempt": attempt + 1, "result": "not visible"})
                    time.sleep(1)
                    actual = self.remote_sha(remote)
                if actual != checksum:
                    raise ValueError(f"Retail copy mismatch: {path.name}; expected {checksum}, actual {actual}")
            self.report["retail_sha256"][path.name] = checksum
            if index % 8 == 0 or index == len(assets):
                print(f"Verified {index}/{len(assets)} retail archives.", flush=True)
        self.adb("shell", "chown", uid, *(retail + "/" + p.name for p in assets))
        page_query = self.adb("shell", "getconf", "PAGE_SIZE", check=False)
        if page_query.returncode == 0 and page_query.stdout.strip().isdigit():
            self.report["page_size"] = int(page_query.stdout.strip())
            self.report["page_size_source"] = "getconf PAGE_SIZE"
        else:
            smaps = self.adb("shell", "cat", "/proc/self/smaps").stdout.decode().replace("\r\n", "\n")
            page = re.search(r"(?m)^KernelPageSize:\s+(\d+) kB$", smaps)
            if not page:
                raise ValueError("Cannot determine guest page size")
            self.report["page_size"] = int(page.group(1)) * 1024
            self.report["page_size_source"] = "/proc/self/smaps KernelPageSize"
        self.adb("shell", "run-as", PACKAGE, "mkdir", "-p", SAVE_ROOT)
        for name in (*self.scripts, *self.save_names, *(("emulator_resumed.cfg",) if self.mode == "lifecycle" else ()),
                     *("screenshots/" + name for name in self.captures)):
            if self.read(name) is not None:
                raise ValueError(f"Existing test file must be reviewed before reuse: {name}")
        self.before_config = self.read("openQ4Config.cfg")
        if self.before_config is not None:
            (self.out / "before-openQ4Config.cfg").write_bytes(self.before_config)
        old_log = self.read("logs/openq4.log")
        if old_log is not None:
            (self.out / "before-openq4.log").write_bytes(old_log)
        self.prepared = True
        if self.mode == "rotate":
            self.apply_rotation_setup()
        elif self.mode == "lifecycle":
            self.write_setup({"touch_settings": LIFECYCLE_TOUCH, "touch_layout": LIFECYCLE_LAYOUT})
            self.report["touch_layout_record"] = LIFECYCLE_LAYOUT
        if old_log is not None:
            if self.read("logs/openq4.log") != old_log:
                raise ValueError("Log changed during preparation")
            self.adb("shell", "run-as", PACKAGE, "rm", self.path("logs/openq4.log"))
        for name, text in self.scripts.items():
            data = text.encode()
            # Track attempted writes too: a failed readback must not leave an
            # untracked autoexec script in the diagnostic application.
            self.created[name] = data
            self.report["created_script_sha256"] = {n: hashlib.sha256(d).hexdigest() for n, d in self.created.items()}
            self.put(name, data)
        self.write_report()
        print(f"Retail files verified; launching silent {self.mode.upper()} diagnostics.", flush=True)
        result = self.adb("shell", "am", "start", "-W", "-n",
                          PACKAGE + "/com.darkmatter.openq4.OpenQ4Activity",
                          "--ez", "multiplayer", "true" if self.mode in ("mp", "resume") else "false", timeout=60)
        requests = {"EMULATOR_ROTATE_PORTRAIT": 1, "EMULATOR_ROTATE_LANDSCAPE": 0}
        (self.out / "launch.log").write_bytes(result.stdout + result.stderr)
        deadline = time.monotonic() + self.args.timeout
        last_marker = ""
        while time.monotonic() < deadline:
            pid = self.adb("shell", "pidof", PACKAGE, check=False).stdout.decode().strip()
            if pid:
                if self.pid is None:
                    self.pid = pid
                    self.pids.append(pid)
                elif pid != self.pid:
                    raise ValueError("Native game process was replaced")
            data = self.read("logs/openq4.log") or b""
            if re.search(rb"(?:^|\n)(?:ERROR:|FATAL:|Fatal)", data):
                self.collect()
                raise ValueError("Engine reported a fatal error; inspect openq4.log")
            markers = re.findall(rb"EMULATOR_(?:GAMEPLAY_READY|ROTATE_PORTRAIT|ROTATE_LANDSCAPE|TOUCH_RELEASED"
                                 rb"|SAVE_RESTORED|SUSPEND_RESTORED|BACKGROUND_REQUEST|RECONNECTED|SMOKE_COMPLETE)", data)
            if markers and markers[-1].decode() != last_marker:
                last_marker = markers[-1].decode()
                print(last_marker, flush=True)
                # The engine waits after each request; turn the AVD at once.
                if self.mode == "rotate" and last_marker in requests:
                    self.set_rotation(requests.pop(last_marker))
                if (last_marker == "EMULATOR_BACKGROUND_REQUEST" and self.mode == "resume"
                        and "backgrounded_utc" not in self.lifecycle and b"#str_04320" in data):
                    # A slow join (a loaded host) can outlast the server's time-out
                    # before the probe starts; that run measures nothing.
                    raise ValueError("The server dropped the client during the join, before the background stay; rerun")
                if last_marker == "EMULATOR_BACKGROUND_REQUEST" and "backgrounded_utc" not in self.lifecycle:
                    if self.mode == "lifecycle":
                        self.suspend_and_kill()
                        (self.out / "first-run-openq4.log").write_bytes(data)
                        last_marker = ""
                        continue
                    if self.mode == "resume":
                        self.away_and_return()
            after = data.split(b"EMULATOR_BACKGROUND_REQUEST", 1)[1] if b"EMULATOR_BACKGROUND_REQUEST" in data else b""
            done = (b"Network: the connection survived the background" in after if self.args.away < 40 else
                    b"Network: reconnecting after the background" in after and re.search(rb"msec to load mp/", after.split(
                        b"Network: reconnecting after the background", 1)[1])) if self.mode == "resume" and "returned_utc" in self.lifecycle else False
            if done:
                # The pass condition is in the log (g_autoExecAfterMapLoad fires once a
                # session, so a reconnect's map load is the marker). Give a lost
                # connection time to show before ending the client, which would play on.
                time.sleep(15)
                (self.out / "resumed-openq4.log").write_bytes(self.read("logs/openq4.log") or data)
                self.adb("shell", "am", "force-stop", PACKAGE)
                self.wait_for_pid(False, 30)
                break
            if self.pid and not pid:
                break
            time.sleep(2)
        else:
            raise TimeoutError("Gameplay/save probe did not exit within its deadline")
        self.collect()
        self.assess()

    def assess(self):
        if self.mode == "lifecycle":
            return self.assess_lifecycle()
        if self.mode == "resume":
            return self.assess_resume()
        log = (self.out / "openq4.log").read_text(errors="replace")
        for marker in ("EMULATOR_GAMEPLAY_READY", "EMULATOR_TOUCH_RELEASED", "EMULATOR_SMOKE_COMPLETE",
                       *(("EMULATOR_SAVE_RESTORED",) if self.mode != "mp" else ()),
                       *(("EMULATOR_PORTRAIT_READY", "EMULATOR_LANDSCAPE_RESTORED") if self.mode == "rotate" else ())):
            if marker not in log:
                raise ValueError(f"Missing engine completion marker: {marker}")
        if re.search(r"(?:^|\n)(?:ERROR:|FATAL|Fatal)", log):
            raise ValueError("Engine reports a fatal error")
        if f'"com_activeGameModule" is:"game_{"mp" if self.mode == "mp" else "sp"}"' not in log:
            raise ValueError("Requested game module was not active")
        poses = {}
        for marker in ("GAMEPLAY_READY", "TOUCH_RELEASED", "TOUCH_NEUTRAL",
                       *(("SAVE_RESTORED",) if self.mode != "mp" else ()),
                       *(("PORTRAIT_READY", "LANDSCAPE_RESTORED") if self.mode == "rotate" else ())):
            poses[marker] = pose_after_marker(log, marker, self.mode)
        # Rotation measures the portrait drive from the pose rotation left.
        start = "PORTRAIT_READY" if self.mode == "rotate" else "GAMEPLAY_READY"
        movement = math.dist(poses[start][:3], poses["TOUCH_RELEASED"][:3])
        yaw = 3 if self.mode == "mp" else 4
        yaw_change = abs((poses["TOUCH_RELEASED"][yaw] - poses[start][yaw] + 180) % 360 - 180)
        # Retain measured values even when the behavioral assertion fails.
        self.report.update(poses=poses, movement_distance=movement, yaw_change=yaw_change)
        if movement < 20 or yaw_change < 1:
            raise ValueError("Touch probe did not move and turn the player")
        if poses["TOUCH_RELEASED"] != poses["TOUCH_NEUTRAL"]:
            raise ValueError("Player did not settle after touch release")
        if self.mode != "mp" and any(abs(a - b) > 0.2 for a, b in zip(poses["TOUCH_NEUTRAL"], poses["SAVE_RESTORED"])):
            raise ValueError("Save restore changed the settled player pose")
        if self.mode == "rotate":
            self.assess_rotation(log, poses)
        released = log.split("EMULATOR_TOUCH_RELEASED", 1)[1].split("EMULATOR_TOUCH_NEUTRAL", 1)[0]
        if "pressed: none, stick free" not in released:
            raise ValueError("Touch release left an action held")
        if int(self.report["ro.build.version.sdk"]) >= 30:
            exit_info = (self.out / "exit-info.txt").read_text(errors="replace")
            if not re.search(r"pid=" + re.escape(self.pid) + r"\b[^\n]*\n[^\n]*reason=1 \(EXIT_SELF\)[^\n]*status=0\b", exit_info):
                raise ValueError("No successful native self-exit record for the tested process")
        captures = {}
        for name in self.captures:
            path = self.out / name
            if not path.is_file() or path.stat().st_size < 18:
                raise ValueError(f"Missing engine capture: {name}")
            header = path.read_bytes()[:18]
            captures[name] = {"sha256": sha(path), "width": int.from_bytes(header[12:14], "little"),
                              "height": int.from_bytes(header[14:16], "little")}
            if min(captures[name]["width"], captures[name]["height"]) < 300:
                raise ValueError("Unexpected engine capture size")
        if self.mode == "rotate" and not (captures["emulator-portrait.tga"]["height"] > captures["emulator-portrait.tga"]["width"]
                                          and captures["emulator-spawn.tga"]["width"] > captures["emulator-spawn.tga"]["height"]):
            raise ValueError("Engine captures did not follow the rotation")
        self.report.update(status="pass", engine_pid=self.pid, captures=captures, poses=poses,
                           movement_distance=movement, yaw_change=yaw_change,
                           pose_format="XYZ, pitch/yaw/roll" if self.mode == "sp" else "XYZ, yaw",
                           scope="Native " + ("SP rotation" if self.mode == "rotate" else self.mode.upper())
                           + ", engine touch commands"
                           + (" and same-build save/load" if self.mode != "mp" else "; local listen server, no remote peers")
                           + ("; AVD rotation setting, landscape-portrait-landscape mid-match" if self.mode == "rotate" else "")
                           + "; visual review separate")

    def assess_lifecycle(self):
        first = (self.out / "first-run-openq4.log").read_text(errors="replace")
        log = (self.out / "openq4.log").read_text(errors="replace")
        for text, marker in ((first, "EMULATOR_GAMEPLAY_READY"), (first, "EMULATOR_TOUCH_NEUTRAL"),
                             (first, "EMULATOR_BACKGROUND_REQUEST"), (log, "EMULATOR_SUSPEND_RESTORED"),
                             (log, "EMULATOR_SMOKE_COMPLETE")):
            if marker not in text:
                raise ValueError(f"Missing engine marker: {marker}")
        for text in (first, log):
            if re.search(r"(?:^|\n)(?:ERROR:|FATAL|Fatal)", text):
                raise ValueError("Engine reports a fatal error")
        # The applied editor record reached the engine: Fire placed and enlarged,
        # Jump hidden, Zoom faded, and the same again after the relaunch.
        layouts = {"first": layout_after_marker(first, "GAMEPLAY_READY"), "relaunched": layout_after_marker(log, "SUSPEND_RESTORED")}
        self.report["touch_layouts"] = layouts
        for name, layout in layouts.items():
            dp, w, h = layout["dp"], layout["width"], layout["height"]
            fire = layout["controls"].get("fire")
            if not layout["edited"] or not dp or fire is None:
                raise ValueError(f"The layout editor record was not applied ({name})")
            if abs(fire[0] - (w - 160 * dp) / w) > 0.002 or abs(fire[1] - (h - 120 * dp) / h) > 0.002:
                raise ValueError(f"Fire is not where the record placed it ({name}): {fire}")
            if layout["hidden"] != ["jump"] or abs(layout["opacity"].get("zoom", 0) - 0.20) > 0.001:
                raise ValueError(f"Hidden or faded controls differ from the record ({name})")
            if abs(layout["radii"]["fire"] - 1.25 * 44 * dp / w) > 0.002:
                raise ValueError(f"Fire is not at 125% ({name})")
        poses = {marker: pose_after_marker(first, marker) for marker in ("GAMEPLAY_READY", "TOUCH_RELEASED", "TOUCH_NEUTRAL")}
        poses["SUSPEND_RESTORED"] = pose_after_marker(log, "SUSPEND_RESTORED")
        movement = math.dist(poses["GAMEPLAY_READY"][:3], poses["TOUCH_RELEASED"][:3])
        self.report.update(poses=poses, movement_distance=movement)
        if movement < 20:
            raise ValueError("The edited layout's controls did not move the player")
        if any(abs(a - b) > 0.2 for a, b in zip(poses["TOUCH_NEUTRAL"], poses["SUSPEND_RESTORED"])):
            raise ValueError("The suspension save did not restore the player where the app was left")
        if len(self.pids) != 2 or self.pids[0] == self.pids[1]:
            raise ValueError("Expected the original process and one relaunched process")
        exit_info = (self.out / "exit-info.txt").read_text(errors="replace")
        killed = re.search(r"pid=" + re.escape(self.pids[0]) + r"\b[^\n]*\n[^\n]*reason=(\d+) \(([A-Z_ ]+)\) subreason=(\d+) \(([A-Z_ ]+)\)", exit_info)
        self.lifecycle["killed_exit_reason"] = f"{killed.group(2)} / {killed.group(4)}" if killed else None
        if int(self.report["ro.build.version.sdk"]) >= 30:
            # `am kill` records USER REQUESTED / KILL BACKGROUND: the process
            # ended in the background with no further call into the app.
            if not killed or killed.group(4) != "KILL BACKGROUND":
                raise ValueError("The first process did not end by the background kill")
            if not re.search(r"pid=" + re.escape(self.pids[1]) + r"\b[^\n]*\n[^\n]*reason=1 \(EXIT_SELF\)[^\n]*status=0\b", exit_info):
                raise ValueError("No successful native self-exit record for the relaunched process")
        captures = self.check_captures()
        self.report.update(status="pass", engine_pids=self.pids, captures=captures, lifecycle=self.lifecycle,
                           scope="Native SP: layout editor record through setup preferences and JNI, touch play, "
                                 "background through the Home intent, the Suspended save, `am kill` of the backgrounded "
                                 "process, relaunch and load of that save; visual review separate")

    def assess_resume(self):
        log = (self.out / "resumed-openq4.log").read_text(errors="replace")
        for marker in ("EMULATOR_GAMEPLAY_READY", "EMULATOR_BACKGROUND_REQUEST"):
            if marker not in log:
                raise ValueError(f"Missing engine marker: {marker}")
        if '"com_activeGameModule" is:"game_mp"' not in log:
            raise ValueError("The multiplayer module was not active")
        back = log.split("EMULATOR_BACKGROUND_REQUEST", 1)[1]
        away = re.search(r"Network: back from (\d+) ms in the background", back)
        reconnect = re.search(r"Network: reconnecting after the background \(([^)]*)\)", back)
        survived = "Network: the connection survived the background" in back
        self.lifecycle.update(reported_ms=int(away.group(1)) if away else None,
                              reconnect_reason=reconnect.group(1) if reconnect else None, connection_survived=survived)
        if self.args.away >= 40:
            if not reconnect or "beyond the time-out" not in reconnect.group(1):
                raise ValueError("A stay beyond the time-out did not reconnect at once")
            if not re.search(r"msec to load mp/", back.split("Network: reconnecting after the background", 1)[1]):
                raise ValueError("The client did not return to the game after reconnecting")
            if any(box in back for box in ("#str_04320", "#str_04328", "#str_04329")):
                raise ValueError("The client showed a lost connection instead of reconnecting")
        else:
            if not away or not survived or reconnect or "#str_04320" in back:
                raise ValueError("A short stay did not keep its connection")
        self.report.update(status="pass", engine_pids=self.pids, lifecycle=self.lifecycle,
                           scope=f"Native MP client of the dedicated server at {self.args.server}: background through the "
                                 f"Home intent for {self.args.away} s and return; "
                                 + ("immediate reconnect" if self.args.away >= 40 else "connection kept"))

    def check_captures(self) -> dict:
        captures = {}
        for name in self.captures:
            path = self.out / name
            if not path.is_file() or path.stat().st_size < 18:
                raise ValueError(f"Missing engine capture: {name}")
            header = path.read_bytes()[:18]
            captures[name] = {"sha256": sha(path), "width": int.from_bytes(header[12:14], "little"),
                              "height": int.from_bytes(header[14:16], "little")}
        return captures

    def assess_rotation(self, log: str, poses: dict):
        layouts = {marker: layout_after_marker(log, marker)
                   for marker in ("GAMEPLAY_READY", "PORTRAIT_READY", "LANDSCAPE_RESTORED")}
        self.report["touch_layouts"] = layouts
        for marker in ("GAMEPLAY_READY", "LANDSCAPE_RESTORED"):
            if not layouts[marker]["width"] > layouts[marker]["height"]:
                raise ValueError(f"Window not landscape at {marker}")
        portrait = layouts["PORTRAIT_READY"]
        if not portrait["height"] > portrait["width"] or portrait["state"] != "active":
            raise ValueError("Window did not turn to portrait with the touch controls active")
        c = portrait["controls"]
        deck = 0.5 + 0.375 * portrait["width"] / portrait["height"]
        if not (abs(c["fire"][0] - 0.75) < 0.01 and c["weapon"][1] < c["fire"][1]
                and c["jump"][0] > c["fire"][0] and c["crouch"][0] > c["fire"][0]
                and c["zoom"][0] < c["fire"][0] and c["reload"][0] < c["fire"][0]
                and all(c[n][0] > 0.5 and c[n][1] > deck for n in ("fire", "jump", "crouch", "reload", "zoom"))):
            raise ValueError("Portrait touch controls are not the section 13.6 thumb deck")
        # Turning the device is a resize: the player neither moves nor turns.
        for before, after in (("GAMEPLAY_READY", "PORTRAIT_READY"), ("TOUCH_NEUTRAL", "LANDSCAPE_RESTORED")):
            if any(abs(a - b) > 0.2 for a, b in zip(poses[before], poses[after])):
                raise ValueError(f"Rotation changed the player pose ({before} -> {after})")
        self.report["rotation_requests"] = self.rotated

    def cleanup(self):
        if not self.prepared:
            return
        pid = self.adb("shell", "pidof", PACKAGE, check=False).stdout.strip()
        if pid:
            self.report["forced_stop"] = True
            self.adb("shell", "am", "force-stop", PACKAGE)
            self.collect()
        else:
            self.report["forced_stop"] = False
        retired = {}
        for name, expected in self.created.items():
            actual = self.read(name)
            if actual is not None and name == "autoexec.cfg" and self.mode == "lifecycle" and actual == SCRIPTS["autoexec.cfg"].encode():
                expected = actual	# a failure before the relaunch left the first script
            if actual is None:
                # An attempted write may have failed before creating a file.
                continue
            if actual != expected:
                raise ValueError(f"Test script changed; refusing cleanup: {name}")
            self.adb("shell", "run-as", PACKAGE, "rm", self.path(name))
            retired[name] = hashlib.sha256(expected).hexdigest()
        for name in (*self.save_names, *("screenshots/" + name for name in self.captures)):
            data = self.read(name)
            if data is not None:
                # Preserve all generated evidence before retiring its device copy.
                (self.out / Path(name).name).write_bytes(data)
                self.adb("shell", "run-as", PACKAGE, "rm", self.path(name))
                retired[name] = hashlib.sha256(data).hexdigest()
        if self.before_config is not None:
            self.put("openQ4Config.cfg", self.before_config)
        elif self.read("openQ4Config.cfg") is not None:
            self.adb("shell", "run-as", PACKAGE, "rm", self.path("openQ4Config.cfg"))
        self.report["retired_test_files"] = retired
        self.report["original_config_restored"] = True
        self.restore_rotation_setup()
        self.report["lifecycle"] = self.lifecycle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("adb", "apk", "stage", "build-tools", "certificate", "assets", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--mode", choices=("sp", "mp", "rotate", "lifecycle", "resume"), default="sp")
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--server", help="resume mode: the dedicated server, <IPv4>:<port>, as the emulator reaches it")
    parser.add_argument("--away", type=int, default=50, help="resume mode: seconds in the background (40 or more is beyond the time-out)")
    args = parser.parse_args()
    if not re.fullmatch(r"emulator-\d{4,5}", args.serial):
        parser.error("Only an explicitly selected emulator serial is accepted")
    if not 30 <= args.timeout <= 1500:
        parser.error("Timeout must be between 30 and 1500 seconds")
    if args.mode == "resume" and (not args.server or not 5 <= args.away <= 300):
        parser.error("Resume mode needs --server and --away between 5 and 300 seconds")
    for name in ("adb", "apk", "stage", "build_tools", "certificate", "assets"):
        setattr(args, name, getattr(args, name).resolve())
    probe = Probe(args)
    try:
        probe.run()
    except Exception as error:
        probe.report.update(status="fail", error=str(error), engine_pid=probe.pid)
        raise
    finally:
        try:
            probe.cleanup()
        except Exception as error:
            probe.report.update(status="fail", cleanup_error=str(error))
            raise
        finally:
            probe.write_report()
    scope = {"sp": "SP gameplay/save", "mp": "MP gameplay", "rotate": "SP rotation/portrait deck/save",
             "lifecycle": "SP touch layout/suspension save/process death/restore", "resume": "MP background/resume"}[args.mode]
    print(f"PASS: silent emulator {scope} probe; inspect the retained engine captures.", flush=True)


if __name__ == "__main__":
    main()
