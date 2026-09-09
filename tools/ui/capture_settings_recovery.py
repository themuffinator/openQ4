#!/usr/bin/env python3
"""Capture a real pending display journal, then recover it in a second process.

Default: prepare scripts/commands only. --execute launches two hidden, windowed
map profiles with mouse/controller input disabled. Images use the registered
engine screenshot command. Use a new output directory for every attempt.

The confirmed variant changes only the state token and checksum in real Pending
bytes after process one exits. This models a crash after the durable marker;
it is explicitly injected evidence, not a claim that Keep or a power cut ran.
"""
from __future__ import annotations

import argparse
from collections import Counter
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/ui/fixtures/display-settings-smoke.q4ui"
MAGIC = b"openQ4 settings recovery 1\n"
JOURNAL_NAME = "ui-settings-recovery.dat"
CONFIG_NAME = "openQ4Config.cfg"
READS = ("phase", "open", "dirty", "request", "confirmationVisible", "canConfirm",
         "canRevert", "canRetry", "remaining", "draftWidth", "draftHeight",
         "baselineWidth", "baselineHeight", "liveWidth", "liveHeight")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scripts() -> tuple[str, str]:
    before = ('rendererDisplayProbe report\n'
              'testGUI "settings-recovery.q4ui"\nwait 2\n'
              'openq4_retainedGui event "edit"\nwait 2\n'
              'openq4_retainedGui event "apply"\nwait 12\n'
              'openq4_retainedGui report\n')
    before += "".join(f'openq4_guiGet "{name}"\n' for name in READS)
    before += ('rendererDisplayProbe report\ngfxInfo\n'
               'screenshot "screenshots/settings-recovery-pending.tga"\n'
               'echo SETTINGS_RECOVERY_PENDING_COMPLETE\nquit\n')
    after = ('rendererDisplayProbe report\nwait 6\nrendererDisplayProbe report\n'
             'gfxInfo\nscreenshot "screenshots/settings-recovery-restored.tga"\n'
             'echo SETTINGS_RECOVERY_STARTUP_COMPLETE\nquit\n')
    return before, after


def command(profile: dict, runtime: Path, assets: Path, save: Path, mode: str,
            renderer: str, phase: str) -> list[str]:
    values = {
        "fs_basepath": str(assets), "fs_savepath": str(save), "fs_devpath": str(save),
        "fs_game": "baseoq4", "logFile": "2", "logFileName": f"logs/recovery-{phase}.log",
        "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_screen": "-1",
        "r_windowWidth": "1280", "r_windowHeight": "720", "r_mode": "-1",
        "r_customWidth": "1280", "r_customHeight": "720", "r_multiSamples": "0",
        "r_swapInterval": "1", "r_renderApi": renderer, "r_rendererSharedGui": "1",
        "r_rendererSharedInWorldGui": "0", "r_gamma": "1", "r_brightness": "1",
        "in_mouse": "0", "in_joystick": "0", "in_joystickRumble": "0",
        "g_autoScreenshot": "0", "g_autoSkipCinematics": "1",
        "g_autoExecAfterMapLoad": f"settings-recovery-{phase}.cfg",
        "g_autoExecAfterMapLoadDelayMs": "3000", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_maxfps": "60",
        "ui_autoJoin": "1" if mode == "mp" else "0", "ui_retainedTrace": "1",
        "ui_retainedScale": "1", "ui_retainedDensity": "1.25", "ui_retainedReducedMotion": "0",
    }
    overrides = {name.lower() for name in values}
    if phase == "startup":
        # The engine replays command-line +set commands after early journal
        # recovery. Reasserting the original dimensions there would overwrite
        # a successfully recovered Confirmed target and later archive them.
        # Keep the original archive as input, and suppress profile duplicates.
        del values["r_windowWidth"]
        del values["r_windowHeight"]
    tail = []
    original = profile["args"]
    index = 0
    while index < len(original):
        if original[index].lower() in ("+set", "+seta") and index + 2 < len(original):
            if original[index + 1].lower() not in overrides:
                tail.extend(original[index:index + 3])
            index += 3
        else:
            tail.append(original[index])
            index += 1
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    result = [str(executable)]
    for key, value in values.items():
        result.extend(("+set", key, value))
    result.extend(value.replace("${workspaceFolder}", str(ROOT)) for value in tail)
    return result


def decode_journal(raw: bytes) -> dict:
    if len(raw) > 4 * 1024 * 1024 or not raw.startswith(MAGIC):
        raise ValueError("bad journal magic or size")
    checksum, separator, payload = raw[len(MAGIC):].partition(b"\n")
    if not separator or checksum != f"{zlib.crc32(payload):08x}".encode():
        raise ValueError("bad journal checksum")

    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate journal key")
            result[key] = value
        return result

    flat = json.loads(payload, object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"nonfinite {value}")))
    if (not isinstance(flat, dict) or type(flat.get("schema")) not in (int, float) or flat.get("schema") != 1 or
            flat.get("state") not in ("pending", "confirmed") or
            not re.fullmatch(r"[0-9a-f]{32}", str(flat.get("attempt", ""))) or flat.get("attempt") == "0" * 32):
        raise ValueError("bad journal schema, state or attempt")
    if any(not isinstance(value, (str, bool, int, float)) or
           isinstance(value, (int, float)) and (abs(value) > 1e12 or not math.isfinite(value)) for value in flat.values()):
        raise ValueError("invalid typed journal value")
    return flat


def confirmed_record(pending: bytes) -> bytes:
    if decode_journal(pending)["state"] != "pending":
        raise ValueError("injection requires a real Pending record")
    payload = pending[len(MAGIC) + 9:]
    if payload.count(b'"state":"pending"') != 1:
        raise ValueError("Pending record must have exact canonical state spelling")
    payload = payload.replace(b'"state":"pending"', b'"state":"confirmed"', 1)
    return MAGIC + f"{zlib.crc32(payload):08x}\n".encode() + payload


def records(log: str, prefix: str) -> list[dict[str, str]]:
    result = []
    for line in log.splitlines():
        if not line.startswith(prefix + " "):
            continue
        fields = line[len(prefix) + 1:].split()
        pairs = [field.split("=", 1) for field in fields]
        if any(len(pair) != 2 for pair in pairs) or len({pair[0] for pair in pairs}) != len(pairs):
            raise ValueError(f"malformed {prefix} record")
        result.append(dict(pairs))
    return result


def diagnostic_lines(log: str) -> list[str]:
    return [line for line in log.splitlines() if any(word in line for word in ("WARNING:", "ERROR:", "FATAL:"))]


def common_errors(log: str, mode: str, renderer: str, marker: str,
                  allowed_warnings: list[str]) -> list[str]:
    errors = []
    if sum(line.strip() == marker for line in log.splitlines()) != 1:
        errors.append("missing or repeated capture marker")
    map_name = "game/airdefense1" if mode == "sp" else "mp/q4dm1"
    if map_name not in log:
        errors.append("gameplay map identity missing")
    active = re.findall(r"Renderer API: requested=\S+ active=(\S+)", log)
    if not active or any(api != renderer for api in active):
        errors.append("wrong or missing active renderer")
    diagnostics = diagnostic_lines(log)
    if any("ERROR:" in line or "FATAL:" in line for line in diagnostics):
        errors.append("engine errors or fatal diagnostics")
    if Counter(diagnostics) - Counter(allowed_warnings):
        errors.append("new diagnostics compared with the explicit warning baseline")
    return errors


def configuration_values(text: str) -> dict[str, str]:
    values = {}
    for key, value in re.findall(r'^seta\s+(\S+)\s+"([^"\r\n]*)"\s*$', text, re.MULTILINE):
        if key in values:
            raise ValueError(f"duplicate archived CVar {key}")
        values[key] = value
    return values


def image_record(path: Path, expected: tuple[int, int], *, preview_dir: Path | None = None, preview_prefix: str = "") -> dict:
    raw = path.read_bytes()
    if len(raw) < 18:
        raise ValueError("truncated TGA header")
    width, height = struct.unpack_from("<HH", raw, 12)
    if (width, height) != expected or raw[1] != 0 or raw[2] != 2 or raw[16] not in (24, 32):
        raise ValueError("wrong engine image dimensions or TGA encoding")
    if len(raw) < 18 + raw[0] + width * height * (raw[16] // 8):
        raise ValueError("truncated engine image pixels")
    result = {"path": str(path), "width": width, "height": height, "sha256": digest(path)}
    if preview_dir is not None:
        # Lossless conversion of the registered engine screenshot, never OS
        # capture. Include case/phase and content hash to avoid stale basename
        # previews being mistaken for a different run's image.
        channels = raw[16] // 8
        stride = width * channels
        pixels = raw[18 + raw[0]:18 + raw[0] + stride * height]
        rows = []
        for y in range(height):
            source_y = y if raw[17] & 0x20 else height - 1 - y
            source = pixels[source_y * stride:(source_y + 1) * stride]
            line = bytearray(source)
            line[0::channels], line[2::channels] = source[2::channels], source[0::channels]
            if raw[17] & 0x10:
                line = b"".join(line[x * channels:(x + 1) * channels] for x in range(width - 1, -1, -1))
            rows.append(b"\0" + line)
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)) +
               chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
        preview_dir.mkdir(exist_ok=True)
        preview = preview_dir / f"{preview_prefix}-{result['sha256'][:16]}.png"
        preview.write_bytes(png)
        result["preview"] = {"path": str(preview), "sha256": digest(preview), "conversion": "lossless RGB/RGBA from engine TGA"}
    return result


def pending_errors(log: str, flat: dict, config: str) -> list[str]:
    errors = []
    operations = records(log, "UI_SETTINGS")
    operations = [row for row in operations if "operation" in row]
    if ([row.get("operation") for row in operations] !=
            ["settings.system.begin", "settings.system.edit", "settings.system.apply"] or
            any(row.get("result") != "0" for row in operations) or
            len({row.get("owner") for row in operations}) != 1):
        errors.append("unexpected settings operation, result or owner")
    if any(not row.get("owner", "").isdigit() or int(row["owner"]) <= 0 for row in operations):
        errors.append("settings operation has no valid owner")
    device, views, present = records(log, "UI_SETTINGS_DEVICE"), records(log, "UI_SETTINGS_VIEW"), records(log, "UI_SETTINGS_PRESENT")
    if len(device) != 1 or device[0].get("restore") != "0" or not views or len(present) != 1:
        errors.append("missing or repeated device/view/first-present evidence")
    else:
        if any(view.get(key) != present[0].get(key) for view in views for key in ("owner", "request", "epoch", "generation", "failures")):
            errors.append("confirmation identity changed after owner draw")
        if not operations or views[0].get("owner") != operations[0].get("owner") or not views[0].get("request", "").isdigit():
            errors.append("confirmation owner/request mismatch")
        if any(int(present[0][key]) <= int(views[-1][key]) for key in ("submitted", "presented")):
            errors.append("confirmation lacked a new submit and present after owning view draw")
        if any(device[0].get(key) != present[0].get(key) for key in ("epoch", "generation", "failures")):
            errors.append("confirmation device changed or added a failure")
        if present[0].get("failures") != "0":
            errors.append("confirmation had a presentation failure")
        lines = log.splitlines()
        indexes = lambda prefix: [index for index, line in enumerate(lines) if line.startswith(prefix + " ")]
        if not indexes("UI_SETTINGS_DEVICE")[0] < min(indexes("UI_SETTINGS_VIEW")) <= max(indexes("UI_SETTINGS_VIEW")) < indexes("UI_SETTINGS_PRESENT")[0]:
            errors.append("device creation, owner view and fresh present were observed out of order")
    reads = re.findall(r"^GUI_VALUE ([^=]+)=(.*)$", log, re.MULTILINE)
    if [name for name, _ in reads] != list(READS):
        errors.append("missing, repeated or reordered confirmation readbacks")
    values = dict(reads)
    expected = {"phase": "2", "open": "1", "dirty": "1", "confirmationVisible": "1",
                "canConfirm": "1", "canRevert": "1", "canRetry": "0", "draftWidth": "960",
                "draftHeight": "540", "baselineWidth": "1280", "baselineHeight": "720",
                "liveWidth": "960", "liveHeight": "540"}
    if any(values.get(key) != value for key, value in expected.items()):
        errors.append("unconfirmed UI state differs from the frozen attempt")
    try:
        if not math.isfinite(float(values["remaining"])) or not 0 < float(values["remaining"]) <= 15:
            errors.append("confirmation countdown invalid")
        if int(values["request"]) <= 0 or present and values["request"] != present[0]["request"]:
            errors.append("displayed confirmation request mismatch")
    except (KeyError, ValueError):
        errors.append("malformed countdown or request")
    if flat["state"] != "pending" or records(log, "UI_SETTINGS_JOURNAL") != [{"state": "pending", "durable": "1"}]:
        errors.append("first process did not retain exactly one Pending journal")
    archived = configuration_values(config)
    for key, value in (("r_windowWidth", 1280), ("r_windowHeight", 720)):
        if flat.get("baseline." + key) != value or flat.get("target." + key) != (960 if key.endswith("Width") else 540):
            errors.append("journal dimensions differ from authored operation")
        if archived.get(key) != str(value):
            errors.append("unconfirmed dimensions reached archive or baseline archive is absent")
    probe = records(log, "DISPLAY_PROBE")
    if len(probe) != 2 or any(row.get("operation") != "report" or row.get("result") != "1" for row in probe):
        errors.append("missing read-only before/after renderer reports")
    elif (probe[0].get("logical") != "1280x720" or probe[1].get("logical") != "960x540" or
          int(probe[1]["restart"]) != int(probe[0]["restart"]) + 1 or probe[1]["failures"] != probe[0]["failures"]):
        errors.append("apply did not perform exactly one successful dimension restart")
    if any(any(row.get(key) != value for key, value in {"hidden": "1", "fullscreen": "0", "ready": "1", "available": "1",
           "samples": "0", "interval": "1", "failures": "0", "native": "0"}.items()) or int(row.get("valid", "0")) & 3 != 3 for row in probe):
        errors.append("pending renderer policy, readiness or observed parameters differ")
    return errors


def startup_errors(log: str, flat: dict, config: str, journal_exists: bool) -> list[str]:
    errors = []
    if log.count("UI_SETTINGS startup_recovery=complete") != 1 or journal_exists:
        errors.append("startup recovery did not durably finish and retire its journal")
    if any("operation" in row for row in records(log, "UI_SETTINGS")) or "RETAINED_GUI_LOADED" in log:
        errors.append("startup recovery replayed a GUI owner or action")
    if records(log, "UI_SETTINGS_DEVICE"):
        errors.append("startup unexpectedly used an application device restart")
    direction = "displayTarget." if flat["state"] == "confirmed" else "displayRestore."
    wanted = {"logical": f'{int(flat[direction+"actual.logicalWidth"])}x{int(flat[direction+"actual.logicalHeight"])}',
              "samples": str(int(flat[direction+"samples"])), "interval": str(int(flat[direction+"interval"])),
              "hidden": "1", "fullscreen": "0", "result": "1", "observed": "1", "ready": "1",
              "window": "1", "available": "1", "native": "0", "operation": "report"}
    if flat[direction+"check.pixels"]:
        wanted["pixel"] = f'{int(flat[direction+"actual.pixelWidth"])}x{int(flat[direction+"actual.pixelHeight"])}'
    startup = records(log, "UI_SETTINGS_STARTUP")
    if len(startup) != 1:
        errors.append("missing or repeated startup presentation trace")
    else:
        row = startup[0]
        if (row.get("approved") != str(int(flat["state"] == "confirmed")) or row.get("failures") != "0" or
                row.get("width") != str(int(flat[direction+"actual.logicalWidth"])) or
                row.get("height") != str(int(flat[direction+"actual.logicalHeight"]))):
            errors.append("startup committed the wrong approval, dimensions or presentation failure")
        if (int(row["epoch"]) <= 0 or int(row["generation"]) <= 0 or
                int(row["submitted"]) <= int(row["initialSubmitted"]) or
                int(row["presented"]) <= int(row["initialPresented"])):
            errors.append("startup committed before a fresh submit and present")
    probe = records(log, "DISPLAY_PROBE")
    if len(probe) != 2 or any(any(row.get(key) != value for key, value in wanted.items()) for row in probe):
        errors.append("cold-start actual display differs from saved recovery direction")
    else:
        if startup and any(probe[0].get(key) != startup[0].get(key) for key in ("epoch", "generation", "failures")):
            errors.append("gameplay display differs from qualified startup device")
        if any(probe[0][key] != probe[1][key] for key in ("epoch", "generation", "failures", "restart")):
            errors.append("cold-start display drifted or failed after recovery")
        if any(int(probe[1][key]) - int(probe[0][key]) != 6 for key in ("submitted", "presented")):
            errors.append("cold-start gameplay did not present six fresh frames")
        if any(int(row.get("valid", "0")) & 3 != 3 for row in probe):
            errors.append("cold-start actual sample/interval parameters unavailable")
    archived = configuration_values(config)
    chosen = "target." if flat["state"] == "confirmed" else "baseline."
    for key in ("r_windowWidth", "r_windowHeight"):
        if archived.get(key) != str(int(flat[chosen+key])):
            errors.append("startup archive does not contain the recovered dimensions")
    return errors


def source_manifest() -> dict[str, str]:
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "--",
                                     "src", "meson.build", "meson_options.txt", "tools/build"], cwd=ROOT, text=True).splitlines()
    return {name: digest(ROOT / name) for name in sorted(set(names)) if (ROOT / name).is_file()}


def run_process(cmd: list[str], runtime: Path, output: Path, phase: str, timeout: int) -> dict:
    started = time.monotonic()
    options = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options = {"startupinfo": startup, "creationflags": subprocess.CREATE_NO_WINDOW}
    with (output / f"process-{phase}.log").open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(cmd, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, **options)
        print(f"Recovery {phase}: PID {process.pid}; hidden/windowed, host input disabled.", flush=True)
        timed_out = False
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            code = process.returncode
    return {"returncode": code, "timed_out": timed_out, "seconds": round(time.monotonic()-started, 3),
            "process_log_sha256": digest(output / f"process-{phase}.log")}


def capture(args: argparse.Namespace) -> int:
    output, runtime, assets = args.output.resolve(), args.runtime.resolve(), args.assets.resolve()
    if not output.is_relative_to(ROOT / ".tmp"):
        raise ValueError("capture output must be a new private directory under repository .tmp")
    if output.exists():
        raise ValueError("use a new output directory; previous evidence is immutable")
    profiles = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8-sig"))["configurations"]
    prefix = "(SP) airdefense1 " if args.mode == "sp" else "(MP) q4dm1 "
    profile = next(item for item in profiles if item["name"].startswith(prefix) and item["name"].endswith("GL"))
    save = output / "save"
    game = save / "baseoq4"
    commands = {phase: command(profile, runtime, assets, save, args.mode, args.renderer, phase) for phase in ("pending", "startup")}
    if not Path(commands["pending"][0]).is_file():
        raise ValueError("staged client is absent")
    game.mkdir(parents=True)
    shutil.copyfile(FIXTURE, game / "settings-recovery.q4ui")
    cfgs = {}
    for phase, script in zip(("pending", "startup"), scripts()):
        path = game / f"settings-recovery-{phase}.cfg"
        path.write_text(script, encoding="utf-8")
        cfgs[phase] = {"path": str(path), "sha256": digest(path), "text": script}
    binary_paths = [Path(commands["pending"][0])]
    binary_paths += [path for pattern in ("renderer-*", "baseoq4/game-*") for path in runtime.glob(pattern)
                     if path.is_file() and path.suffix.lower() in (".dll", ".so", ".dylib")]
    binaries = {str(path.relative_to(runtime)): digest(path) for path in sorted(binary_paths)}
    sources = source_manifest()
    allowed = []
    if args.warning_baseline:
        allowed = diagnostic_lines(re.sub(r"\^[0-9]", "", args.warning_baseline.read_text(encoding="utf-8", errors="replace")))
        allowed = [line for line in allowed if "WARNING:" in line and "ERROR:" not in line and "FATAL:" not in line]
    metadata = {"status": "prepared", "mode": args.mode, "renderer": args.renderer, "state": args.state,
                "profile": profile["name"], "command": commands, "cwd": str(runtime), "scripts": cfgs,
                "windowed": True, "hidden_window": True, "host_input_injection": False, "os_capture": False,
                "capture_method": "registered engine screenshot command", "binaries": binaries, "sources": sources,
                "runner_sha256": digest(Path(__file__)), "fixture_sha256": digest(FIXTURE),
                "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "phases": {},
                "limits": ["Hidden windowed diagnostic; visible window-manager/fullscreen and other-platform behavior unqualified.",
                           "Pending uses orderly quit while unconfirmed; no process kill or power-loss simulation.",
                           "Confirmed variant injects only the approved marker/CRC into real recorded Pending bytes.",
                           "This is recovery-service evidence, not full UI replacement acceptance."]}
    if args.warning_baseline:
        metadata["warning_baseline"] = {"path": str(args.warning_baseline), "sha256": digest(args.warning_baseline), "warnings": allowed}

    def report():
        (output / "capture.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    report()
    if not args.execute:
        print(f"Prepared without launching: {output / 'capture.json'}")
        return 0
    errors = []
    try:
        for phase in ("pending", "startup"):
            metadata["status"] = f"running_{phase}"
            report()
            measured = run_process(commands[phase], runtime, output, phase, args.timeout)
            metadata["phases"][phase] = measured
            if measured["returncode"] != 0 or measured["timed_out"]:
                raise ValueError(f"{phase} process failed or timed out")
            log_path = game / f"logs/recovery-{phase}.log"
            log = re.sub(r"\^[0-9]", "", log_path.read_text(encoding="utf-8", errors="replace"))
            measured.update(log_path=str(log_path), log_sha256=digest(log_path), diagnostics=diagnostic_lines(log),
                            trace=[line for line in log.splitlines() if line.startswith(("UI_SETTINGS", "DISPLAY_PROBE", "GUI_VALUE"))])
            config_path = game / CONFIG_NAME
            config = config_path.read_text(encoding="utf-8", errors="strict")
            shutil.copyfile(config_path, output / f"configuration-{phase}.cfg")
            measured["configuration_sha256"] = digest(config_path)
            errors += common_errors(log, args.mode, args.renderer, f"SETTINGS_RECOVERY_{phase.upper()}_COMPLETE", allowed)
            if phase == "pending":
                raw = (game / JOURNAL_NAME).read_bytes()
                (output / "journal-pending.dat").write_bytes(raw)
                flat = decode_journal(raw)
                errors += pending_errors(log, flat, config)
                measured["journal_sha256"] = hashlib.sha256(raw).hexdigest()
                measured["screenshot"] = image_record(game / "screenshots/settings-recovery-pending.tga", (960, 540),
                                                       preview_dir=output / "review", preview_prefix=output.name + "-pending")
                if errors:
                    break
                if args.state == "confirmed":
                    raw = confirmed_record(raw)
                    # Both engine processes are stopped. This is an explicitly
                    # injected crash-fixture marker, never a durable-I/O test.
                    (game / JOURNAL_NAME).write_bytes(raw)
                    flat = decode_journal(raw)
                (output / "journal-startup-input.dat").write_bytes(raw)
                metadata["recovery_input"] = {"sha256": hashlib.sha256(raw).hexdigest(), "fields": flat,
                                              "origin": "injected_confirmed_marker" if args.state == "confirmed" else "actual_unconfirmed_quit"}
            else:
                errors += startup_errors(log, flat, config, (game / JOURNAL_NAME).exists())
                size = (960, 540) if args.state == "confirmed" else (1280, 720)
                measured["screenshot"] = image_record(game / "screenshots/settings-recovery-restored.tga", size,
                                                       preview_dir=output / "review", preview_prefix=output.name + "-startup")
        metadata["sources_unchanged"] = sources == source_manifest()
        metadata["binaries_unchanged"] = binaries == {name: digest(runtime / name) for name in binaries}
        if not metadata["sources_unchanged"] or not metadata["binaries_unchanged"]:
            errors.append("source or staged binary inputs changed during capture")
    except (OSError, ValueError, KeyError) as error:
        errors.append(str(error))
    metadata.update(status="failed" if errors else "captured_pending_visual_review", errors=errors)
    report()
    print(f"{metadata['status']}: {output / 'capture.json'}", flush=True)
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("sp", "mp"), required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, default=ROOT / ".install")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--state", choices=("pending", "confirmed"), default="pending")
    parser.add_argument("--warning-baseline", type=Path, help="Explicit already-reviewed log; extra warnings fail.")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--execute", action="store_true", help="Actually launch both processes; default only prepares commands/scripts.")
    args = parser.parse_args()
    if not 10 <= args.timeout <= 600:
        parser.error("timeout must be 10..600 seconds")
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
