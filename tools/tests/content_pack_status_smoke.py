#!/usr/bin/env python3
"""Qualify the localized missing- and mismatched-pack startup report (PKG-01).

A client runtime that lacks a client-only pack, or carries a stale one, must
stop at startup with a report in the player's language that names the pack,
says what it holds and where to get it. The core pack, pak0, carries the
string tables, so a problem with it is reported at once in English.

A copy of the core pack in a mod's folder (#219: a player copied an older
pak0.pk4 next to a texture pack) must be skipped with a warning rather than
outrank the current one, while the mod's own packs, including one that only
overrides openQ4's defaults config, still mount.

Each case runs the staged client from a fixed, reusable runtime (default
.tmp/runtime/pack-diagnostics) refreshed from the staged package by hard link.
The client shows its fatal report in a console window and waits for the
player, so the harness reads the log and then ends the process.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "build"))
from openq4_pak import OPENQ4_CONTENT_PACKS  # noqa: E402

SUFFIX = ".exe" if os.name == "nt" else ""
STRINGS = ROOT / "content" / "baseoq4" / "pak0" / "strings"
FATAL_MARKER = "\nFATAL: "  # how common->FatalError reaches the log


def lang_value(language: str, key: str) -> str:
    for line in (STRINGS / f"{language}_openq4.lang").read_text(encoding="utf-8").splitlines():
        parts = line.strip().split("\t")
        if len(parts) >= 2 and parts[0] == f'"{key}"':
            return parts[-1].strip('"')
    raise KeyError(f"{key} missing from {language}_openq4.lang")


def link_or_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        destination.unlink()
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def stage(package: Path, runtime: Path, drop: str | None, stale: str | None, leftover: str | None = None) -> None:
    if runtime.exists():
        for path in sorted(runtime.rglob("*"), reverse=True):
            if path.is_symlink():
                raise RuntimeError(f"refusing to clear a runtime that contains a link: {path}")
    shutil.rmtree(runtime, ignore_errors=True)
    for source in package.iterdir():
        if source.is_file() and source.suffix.lower() in (".exe", ".dll", ".so", ".dylib", "") and source.name.startswith(
            ("openQ4-client", "renderer-", "OpenAL32", "libopenal")
        ):
            link_or_copy(source, runtime / source.name)
    for source in (package / "baseoq4").iterdir():
        if source.is_file() and (source.name == "mod.json" or source.name.startswith("game_") or source.suffix == ".pk4"):
            if source.name == drop:
                continue
            if source.name == stale:
                destination = runtime / "baseoq4" / source.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                # A valid pack with other bytes: one left over from another release.
                with zipfile.ZipFile(destination, "w") as archive:
                    archive.writestr("readme.txt", "a pack from another openQ4 release\n")
                continue
            link_or_copy(source, runtime / "baseoq4" / source.name)
    if leftover:
        # A retired or old overlay pack an update archive could not delete.
        with zipfile.ZipFile(runtime / "baseoq4" / leftover, "w") as archive:
            archive.writestr("strings/english_zz_leftover.lang", '{\n\t"#str_231700"\t"LEFTOVER PACK WAS MOUNTED"\n}\n')


MOD_DIR = "pakcopyprobe"


def stage_mod(home: Path, runtime: Path) -> None:
    """A mod folder holding an old copy of openQ4's core pack and a pack of its own."""
    mod = home / MOD_DIR
    mod.mkdir(parents=True)
    runtime_manifest = json.loads((runtime / "baseoq4" / "mod.json").read_text(encoding="utf-8"))
    manifest = {
        "name": "Pack copy probe", "version": "1.0", "releaseDate": "2026-11-17",
        "website": "https://github.com/themuffinator/openQ4", "author": "openQ4 tests",
        "requiredopenQ4Version": runtime_manifest["requiredopenQ4Version"],
    }
    (mod / "mod.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    with zipfile.ZipFile(mod / "pak0.pk4", "w") as archive:
        # The two files every openQ4 core pack carries, as an older release had them.
        archive.writestr("openq4_defaults.cfg", "// openQ4 defaults from an older release\n")
        archive.writestr("strings/english_openq4.lang", '{\n\t"#str_231700"\t"STALE CORE COPY WAS MOUNTED"\n}\n')
    with zipfile.ZipFile(mod / "pak1.pk4", "w") as archive:
        # A mod may override openQ4's defaults config on its own; that alone is not a copy.
        archive.writestr("openq4_defaults.cfg", "// the mod's own defaults\n")


def run_case(args, label: str, language: str, drop: str | None, stale: str | None,
             leftover: str | None = None, quit_after_start: bool = False, mod: bool = False) -> dict:
    runtime = args.runtime.resolve()
    stage(args.runtime_dir.resolve(), runtime, drop, stale, leftover)
    home = args.output_dir.resolve() / label
    log = home / "baseoq4" / "logs" / "openq4.log"
    home.mkdir(parents=True)
    if mod:
        stage_mod(home, runtime)
        log = home / MOD_DIR / "logs" / "openq4.log"
    cvars = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(home), "fs_devpath": str(home),
        "sys_lang": language, "logFile": "2", "logFileName": "logs/openq4.log", "s_noSound": "1",
        "r_hiddenWindow": "1", "r_fullscreen": "0", "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "360",
        "in_mouse": "0", "in_joystick": "0", "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    if mod:
        cvars["fs_game"] = MOD_DIR
    command = [str(runtime / f"openQ4-client_x64{SUFFIX}")] + [p for k, v in cvars.items() for p in ("+set", k, v)]
    if quit_after_start:
        command += ["+echo", "PACK_STATUS_STARTED", "+quit"]
    process = subprocess.Popen(command, cwd=runtime, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = ""
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
            if FATAL_MARKER in text or process.poll() is not None:
                time.sleep(1.0)
                text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
                break
            time.sleep(0.25)
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
    fatal = text[text.find(FATAL_MARKER) + 1:] if FATAL_MARKER in text else ""
    return {"label": label, "language": language, "dropped": drop, "stale": stale, "leftover": leftover,
            "fatal": fatal[:4000], "started": "PACK_STATUS_STARTED" in text,
            "pack_lines": [line for line in text.splitlines()
                           if ".pk4" in line and ("baseoq4" in line or MOD_DIR in line)][:20]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install", help="staged package")
    parser.add_argument("--runtime", type=Path, default=ROOT / ".tmp/runtime/pack-diagnostics")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    args.output_dir.resolve().mkdir(parents=True, exist_ok=False)

    loadscreens = next(spec for spec in OPENQ4_CONTENT_PACKS if spec.name == "pak2.pk4")
    grids = next(spec for spec in OPENQ4_CONTENT_PACKS if spec.name == "pak1.pk4")
    failures: list[str] = []
    cases = [
        run_case(args, "german-missing-loadscreens", "german", "pak2.pk4", None),
        run_case(args, "french-stale-light-grids", "french", None, "pak1.pk4"),
        run_case(args, "english-missing-core", "english", "pak0.pk4", None),
        run_case(args, "english-leftover-overlay", "english", None, None, leftover="pak7.pk4", quit_after_start=True),
        run_case(args, "english-core-copy-in-mod", "english", None, None, quit_after_start=True, mod=True),
    ]

    def expect(case: dict, needle: str, what: str) -> None:
        if needle not in case["fatal"]:
            failures.append(f"{case['label']}: missing {what}: {needle!r}")

    german, french, core, leftover, core_copy = cases
    expect(german, lang_value("german", "#str_231700"), "German headline")
    expect(german, lang_value("german", "#str_231704") % ("pak2.pk4", lang_value("german", loadscreens.contents_string), "baseoq4"),
           "German missing-pack line")
    expect(german, "https://github.com/themuffinator/openQ4/releases", "where to get the pack")
    expect(german, "missing baseoq4/pak2.pk4, loadscreens", "English support detail")
    expect(french, lang_value("french", "#str_231705") % ("pak1.pk4", lang_value("french", grids.contents_string), "baseoq4"),
           "French mismatched-pack line")
    expect(core, "openQ4 runtime content packs in 'baseoq4' are missing or modified", "core-pack English report")
    expect(core, "missing baseoq4/pak0.pk4, core runtime content", "core-pack detail")
    if leftover["fatal"] or not leftover["started"]:
        failures.append(f"english-leftover-overlay: a leftover pack must not stop startup: {leftover['fatal'][:300]}")
    if not any("Ignoring" in line and "pak7.pk4" in line and "does not use it" in line for line in leftover["pack_lines"]):
        failures.append("english-leftover-overlay: the leftover pak7.pk4 was not reported as ignored")
    if any("Loaded pk4" in line and "pak7.pk4" in line for line in leftover["pack_lines"]):
        failures.append("english-leftover-overlay: the leftover pak7.pk4 was mounted")
    copy_label = "english-core-copy-in-mod"

    def mod_pack(name: str) -> tuple[str, str]:
        return (f"{MOD_DIR}/{name}", f"{MOD_DIR}\\{name}")
    if core_copy["fatal"] or not core_copy["started"]:
        failures.append(f"{copy_label}: a core-pack copy in a mod folder must not stop startup: {core_copy['fatal'][:300]}")
    if not any("Ignoring" in line and any(p in line for p in mod_pack("pak0.pk4")) and "copy of openQ4's own content pack" in line
               for line in core_copy["pack_lines"]):
        failures.append(f"{copy_label}: the copy {MOD_DIR}/pak0.pk4 was not reported as ignored")
    if any("Loaded pk4" in line and any(p in line for p in mod_pack("pak0.pk4")) for line in core_copy["pack_lines"]):
        failures.append(f"{copy_label}: the copy {MOD_DIR}/pak0.pk4 was mounted")
    if not any("Loaded pk4" in line and any(p in line for p in mod_pack("pak1.pk4")) for line in core_copy["pack_lines"]):
        failures.append(f"{copy_label}: the mod's own {MOD_DIR}/pak1.pk4 was not mounted")
    report ={"status": "fail" if failures else "pass", "cases": cases, "failures": failures}
    (args.output_dir.resolve() / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
